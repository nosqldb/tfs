/*
 * (C) 2007-2010 Alibaba Group Holding Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 * Version: $Id: dataserver_define.h 643 2011-08-02 07:38:33Z duanfei@taobao.com $
 *
 * Authors:
 *   duanfei<duanfei@taobao.com>
 *      - initial release
 *
 */
#include <Time.h>
#include "ds_define.h"
#include "common/internal.h"
#include "common/error_msg.h"
#include "common/new_client.h"
#include "common/client_manager.h"

namespace tfs
{
  namespace dataserver
  {
    int SuperBlockInfo::dump(std::stringstream& stream) const
    {
      stream << "version: " << version_ <<  ",mount tag: " << mount_tag_ << ",mount point: " << mount_point_
        << ",mount_time: " <<tbutil::Time::seconds(mount_time_).toDateTime() << ",mout_fs_type: " << mount_fs_type_
        << ",superblock_reserve_offset: " << superblock_reserve_offset_ << ",block_index_offset: " << block_index_offset_
        << ",max_block_index_element_count: " << max_block_index_element_count_ << ",total_main_block_count: " << total_main_block_count_
        << ",used_main_block_count: " << used_main_block_count_
        << ",max_main_block_size: " << max_main_block_size_ << ",max_extend_block_size: " << max_extend_block_size_
        << ",hash_bucket_count: " << hash_bucket_count_ <<  " ,max_hash_bucket_count: " << max_hash_bucket_count_ <<",max_mmap_size: " <<mmap_option_.max_mmap_size_
        << ",first_mmap_size: " << mmap_option_.first_mmap_size_ << ",per_mmap_size: " << mmap_option_.per_mmap_size_
        << ",max_use_block_ratio: " << max_use_block_ratio_ << ",max_use_hash_bucket_ratio: " <<max_use_hash_bucket_ratio_ <<std::endl;
      return common::TFS_SUCCESS;
    }

    BlockIndex::BlockIndex():
      logic_block_id_(common::INVALID_BLOCK_ID),
      physical_block_id_(common::INVALID_PHYSICAL_BLOCK_ID),
      next_index_(0),
      prev_index_(0),
      index_(-1),
      status_(BLOCK_CREATE_COMPLETE_STATUS_UNCOMPLETE),
      split_flag_(BLOCK_SPLIT_FLAG_NO),
      split_status_(BLOCK_SPLIT_STATUS_UNCOMPLETE),
      reserve_(0)
    {

    }

    DsRuntimeGlobalInformation::DsRuntimeGlobalInformation():
      ns_vip_port_(common::INVALID_SERVER_ID),
      master_ns_ip_port_(common::INVALID_SERVER_ID),
      max_mr_network_bandwidth_mb_(common::DEFAULT_MAX_MR_NETWORK_CAPACITY_MB),
      max_rw_network_bandwidth_mb_(common::DEFAULT_MAX_RW_NETWORK_CAPACITY_MB),
      verify_index_reserved_space_ratio_(common::VERIFY_INDEX_RESERVED_SPACKE_DEFAULT_RATIO),
      check_integrity_interval_days_(common::CHECK_INTEGRITY_INTERVAL_DAYS_DEFAULT),
      global_switch_(common::ENABLE_VERSION_CHECK | common::ENABLE_READ_STATSTICS),
      is_reporting_block_(false)
    {
      memset(&information_, 0, sizeof(information_));
      max_mr_network_bandwidth_mb_ = 0;
      max_rw_network_bandwidth_mb_ = 0;
      max_block_size_ = 0;
      max_write_file_count_ = 0;
      status_ = common::DATASERVER_STATUS_DEAD;
    }

    void DsRuntimeGlobalInformation::startup()
    {
      information_.startup_time_ = time(NULL);
      status_ = common::DATASERVER_STATUS_ALIVE;
    }

    void DsRuntimeGlobalInformation::destroy()
    {
      status_ = common::DATASERVER_STATUS_DEAD;
    }

    bool DsRuntimeGlobalInformation::is_destroyed() const
    {
      return status_ == common::DATASERVER_STATUS_DEAD;
    }

    DsRuntimeGlobalInformation& DsRuntimeGlobalInformation::instance()
    {
      static DsRuntimeGlobalInformation instance_;
      return instance_;
    }

    void interruptable_usleep(const int64_t usec)
    {
      assert(usec >= 0);
      int64_t elapsed = 0;
      int64_t t = 0;
      DsRuntimeGlobalInformation& ds_info = DsRuntimeGlobalInformation::instance();
      while (elapsed < usec && !ds_info.is_destroyed())
      {
        t = std::min(100000L, usec - elapsed);
        usleep(t);
        elapsed += t;
      }
    }

    void DsRuntimeGlobalInformation::dump(const int32_t level, const char* file, const int32_t line,
        const char* function, const char* format, ...)
    {
      UNUSED(level);
      UNUSED(file);
      UNUSED(line);
      UNUSED(function);
      UNUSED(format);
    }
  }/** end namespace dataserver **/
}/** end namespace tfs **/

