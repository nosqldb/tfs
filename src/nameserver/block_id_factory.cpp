/*
 * (C) 2007-2010 Alibaba Group Holding Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 * Version: $Id: block_id_factory.cpp 2014 2011-01-06 07:41:45Z duanfei $
 *
 * Authors:
 *   duanfei <duanfei@taobao.com>
 *      - initial release
 *
 */
#include "common/error_msg.h"
#include "common/directory_op.h"
#include "common/func.h"
#include "block_id_factory.h"
#include "oplog_sync_manager.h"
#include "ns_define.h"

namespace tfs
{
  namespace nameserver
  {
    const uint16_t BlockIdFactory::BLOCK_START_NUMBER = 100;
    const uint32_t BlockIdFactory::SKIP_BLOCK_NUMBER  = 100000;
    const uint16_t BlockIdFactory::FLUSH_BLOCK_NUMBER  = 100;
    const uint64_t BlockIdFactory::MAX_BLOCK_ID = 0xFFFFFFFFFFFFFFFF -1;
    BlockIdFactory::BlockIdFactory(OpLogSyncManager& manager):
      manager_(manager),
      global_id_(BLOCK_START_NUMBER),
      last_flush_id_(0),
      fd_(-1)
    {

    }

    BlockIdFactory::~BlockIdFactory()
    {

    }

    int BlockIdFactory::initialize(const std::string& path)
    {
      uint64_t local_id = 0;
      uint64_t remote_id = 0;
      int32_t ret = path.empty() ? common::EXIT_GENERAL_ERROR : common::TFS_SUCCESS;
      if (common::TFS_SUCCESS == ret)
      {
        if (!common::DirectoryOp::create_full_path(path.c_str()))
        {
          TBSYS_LOG(ERROR, "create directory: %s errors : %s", path.c_str(), strerror(errno));
          ret = common::EXIT_GENERAL_ERROR;
        }
        if (common::TFS_SUCCESS == ret)
        {
          std::string fs_path = path + "/ns.meta";
          fd_ = ::open(fs_path.c_str(), O_RDWR | O_CREAT, 0600);
          if (fd_ < 0)
          {
            TBSYS_LOG(ERROR, "open file %s failed, errors: %s", fs_path.c_str(), strerror(errno));
            ret = common::EXIT_GENERAL_ERROR;
          }
        }
        if (common::TFS_SUCCESS == ret)
        {
          char data[common::INT64_SIZE];
          int32_t length = ::read(fd_, data, common::INT64_SIZE);
          if (length == common::INT64_SIZE)//read successful
          {
            int64_t pos = 0;
            ret = common::Serialization::get_int64(data, common::INT64_SIZE, pos, reinterpret_cast<int64_t*>(&local_id));
            if (common::TFS_SUCCESS != ret)
            {
              TBSYS_LOG(ERROR, "serialize global block id error, ret: %d", ret);
            }
          }
        }
      }


      if (common::TFS_SUCCESS == ret)
      {
        /* query block id from tair */
        manager_.query_global_block_id(remote_id);

        global_id_ = std::max(local_id, remote_id);
        last_flush_id_ = global_id_;
        global_id_ += SKIP_BLOCK_NUMBER;

        TBSYS_LOG(INFO, "local id %lu, remote id %lu", local_id, remote_id);

        if (remote_id > local_id)
        {
          flush_(global_id_);
        }
      }

      return ret;
    }

    int BlockIdFactory::destroy()
    {
      int32_t ret = common::TFS_SUCCESS;
      if (fd_ > 0)
      {
        ret = flush_(global_id_);
        ::close(fd_);
      }
      return ret;
    }

    uint64_t BlockIdFactory::generation(const bool verify)
    {
      mutex_.lock();
      uint64_t id = ++global_id_;
      assert(id <= MAX_BLOCK_ID);
      bool flush_flag = false;
      if (global_id_ - last_flush_id_ >= FLUSH_BLOCK_NUMBER)
      {
        flush_flag = true;
        last_flush_id_ = global_id_; // check and set within lock for muti-thread safe
      }
      mutex_.unlock();
      int32_t ret = common::TFS_SUCCESS;
      if (flush_flag)
      {
        ret = flush_(id);
        if (common::TFS_SUCCESS != ret)
        {
          TBSYS_LOG(WARN, "update global block id failed, id: %"PRI64_PREFIX"u, ret: %d", id, ret);
        }
      }
      if (common::TFS_SUCCESS == ret)
      {
        if (verify)
          id |= 0x8000000000000000;
      }
      return id;
    }

    int BlockIdFactory::update(const uint64_t id)
    {
      bool flush_flag = false;
      uint64_t tmp_id = IS_VERFIFY_BLOCK(id) ? id & 0x7FFFFFFFFFFFFFFF : id;
      int32_t ret = (common::INVALID_BLOCK_ID == id) ? common::EXIT_PARAMETER_ERROR : common::TFS_SUCCESS;
      if (common::TFS_SUCCESS == ret)
      {
        tbutil::Mutex::Lock lock(mutex_);
        global_id_ = std::max(global_id_, tmp_id);
        if (global_id_ - last_flush_id_ >= FLUSH_BLOCK_NUMBER)
        {
          flush_flag = true;
          last_flush_id_ = global_id_;
        }
      }
      if (flush_flag)
      {
        ret = flush_(global_id_);
        if (common::TFS_SUCCESS != ret)
        {
          TBSYS_LOG(WARN, "flush global block id failed, id: %"PRI64_PREFIX"u, ret: %d", tmp_id, ret);
        }
      }
      return ret;
    }

    uint64_t BlockIdFactory::skip(const int32_t num)
    {
      mutex_.lock();
      global_id_ += num;
      uint64_t id = global_id_;
      mutex_.unlock();
      int32_t ret = update(id);
      if (common::TFS_SUCCESS != ret)
      {
        TBSYS_LOG(WARN, "update global block id failed, id: %"PRI64_PREFIX"u, ret: %d", id, ret);
      }
      return id;
    }

    uint64_t BlockIdFactory::get() const
    {
      mutex_.lock();
      uint64_t id = global_id_;
      mutex_.unlock();
      return id;
    }

    int BlockIdFactory::flush_(const uint64_t id) const
    {
      assert(fd_ != -1);
      char data[common::INT64_SIZE];
      int64_t pos = 0;
      int32_t ret = common::Serialization::set_int64(data, common::INT64_SIZE, pos, id);
      if (common::TFS_SUCCESS == ret)
      {
        int32_t offset = 0;
        int32_t length = 0;
        int32_t count  = 0;
        ::lseek(fd_, 0, SEEK_SET);
        do
        {
          ++count;
          length = ::write(fd_, (data + offset), (common::INT64_SIZE - offset));
          if (length > 0)
          {
            offset += length;
          }
        }
        while (count < 3 && offset < common::INT64_SIZE);
        ret = common::INT64_SIZE == offset ? common::TFS_SUCCESS : common::TFS_ERROR;
        if (common::TFS_SUCCESS == ret)
          fsync(fd_);
      }
      return ret;
    }
  }/** nameserver **/
}/** tfs **/
