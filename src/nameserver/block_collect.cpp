/*
 * (C) 2007-2010 Alibaba Group Holding Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 * Version: $Id
 *
 * Authors:
 *   duanfei <duanfei@taobao.com>
 *      - initial release
 *
 */
#include <tbsys.h>
#include "common/parameter.h"
#include "global_factory.h"
#include "block_collect.h"
#include "server_collect.h"
#include "layout_manager.h"

using namespace tfs::common;
using namespace tbsys;

namespace tfs
{
  namespace nameserver
  {
    BlockCollect::BlockCollect(const uint64_t block_id, const time_t now):
      BaseObject<LayoutManager>(now),
      last_leave_time_(0),
      task_expired_time_(0),
      reserve_bit_(0),
      choose_master_(BLOCK_CHOOSE_MASTER_COMPLETE_FLAG_NO),
      create_flag_(BLOCK_CREATE_FLAG_NO),
      in_replicate_queue_(BLOCK_IN_REPLICATE_QUEUE_NO),
      has_lease_(BLOCK_HAS_LEASE_FLAG_NO)
    {
      memset(&info_, 0, sizeof(info_));
      info_.block_id_ = block_id;
      servers_.reserve(SYSPARAM_NAMESERVER.max_replication_);
    }

    BlockCollect::BlockCollect(const uint64_t block_id):
      BaseObject<LayoutManager>(0),
      last_leave_time_(0),
      task_expired_time_(0),
      reserve_bit_(0),
      choose_master_(BLOCK_CHOOSE_MASTER_COMPLETE_FLAG_NO),
      create_flag_(BLOCK_CREATE_FLAG_NO),
      in_replicate_queue_(BLOCK_IN_REPLICATE_QUEUE_NO),
      has_lease_(BLOCK_HAS_LEASE_FLAG_NO)
    {
      info_.family_id_ = INVALID_FAMILY_ID;
      info_.block_id_ = block_id;
      //for query
    }

    BlockCollect::~BlockCollect()
    {

    }

    int BlockCollect::add(bool& writable, bool& master, const uint64_t server, const BlockInfoV2* info, const int64_t family_id, const time_t now)
    {
      master = false;
      writable  = false;
      int32_t ret = (INVALID_SERVER_ID != server) ? TFS_SUCCESS : EXIT_PARAMETER_ERROR;
      if (TFS_SUCCESS == ret)
      {
        dump(TBSYS_LOG_LEVEL(DEBUG));
        if ((NULL != info) && (info->version_ > info_.version_))
        {
          set_version(info->version_);
          update_info(*info);
        }
        writable = !is_full() && !IS_VERFIFY_BLOCK(id());
        const int32_t size = servers_.size();
        SERVER_ITER pos = servers_.end();
        ServerItem* result = get_(server);
        if (NULL == result)
        {
          ret = size < MAX_REPLICATION_NUM ? TFS_SUCCESS : EXIT_COPIES_OUT_OF_LIMIT;
          if (TFS_SUCCESS == ret)
          {
            if (!has_master_())
            {
              bool force_master = is_in_family() ? true: servers_.size() >= 1U;
              const int32_t random_value = random() % SYSPARAM_NAMESERVER.max_replication_;
              if (0 == random_value || force_master)
              {
                choose_master_ = BLOCK_CHOOSE_MASTER_COMPLETE_FLAG_YES;
                pos = servers_.begin();
              }
            }
            ServerItem item;
            item.server_    = server;
            item.family_id_ = family_id;
            item.version_   = (NULL == info) ? BLOCK_VERSION_INIT_VALUE : info->version_;
            servers_.insert(pos, item);
          }
        }
        else
        {
          if (NULL != info && result->version_ != info->version_)
            const_cast<ServerItem*>(result)->version_ = info->version_;
          if (result->family_id_ < family_id)
            const_cast<ServerItem*>(result)->family_id_ = family_id;
          if (!has_master_())
          {
            bool force_master = is_in_family() ? true: servers_.size() >= 2U;
            const int32_t random_value = random() % SYSPARAM_NAMESERVER.max_replication_;
            if (0 == random_value || force_master)
            {
              choose_master_ = BLOCK_CHOOSE_MASTER_COMPLETE_FLAG_YES;
              ServerItem new_item = *result;
              remove(new_item.server_, now);
              servers_.insert(servers_.begin(), new_item);
            }
          }
        }
        if (TFS_SUCCESS == ret)
        {
          master = is_master(server);
        }
      }
      return ret;
    }

    int BlockCollect::remove(const uint64_t server, const time_t now)
    {
      int32_t ret = (INVALID_SERVER_ID != server) ? TFS_SUCCESS : EXIT_PARAMETER_ERROR;
      if (TFS_SUCCESS == ret)
      {
        last_leave_time_ =  now;
        ret = EXIT_DATASERVER_NOT_FOUND;
        SERVER_ITER iter = servers_.begin();
        for (int32_t index = 0; iter != servers_.end() && TFS_SUCCESS != ret; ++iter, ++index)
        {
          ret = (server == iter->server_) ? TFS_SUCCESS : EXIT_DATASERVER_NOT_FOUND;
          if (TFS_SUCCESS == ret)
          {
            servers_.erase(iter);
            if (has_master_() && 0 == index)
              choose_master_ = BLOCK_CHOOSE_MASTER_COMPLETE_FLAG_NO;
          }
        }
      }
      return ret;
    }

    bool BlockCollect::exist(const uint64_t server) const
    {
      bool result = false;
      CONST_SERVER_ITER iter = servers_.begin();
      for (; iter != servers_.end() && !result; ++iter)
      {
        result = server == iter->server_;
      }
      return result;
    }

    bool BlockCollect::is_master(const uint64_t server) const
    {
      bool result = (!servers_.empty() && common::INVALID_SERVER_ID != server);
      if (result)
      {
        if (is_in_family())
        {
          if (has_master_())
            result = server == servers_[0].server_;
          else
            result = (exist(server) && servers_.size() == 1U);
        }
        else
        {
          result = has_master_() && server == servers_[0].server_;
        }
      }
      return result;
    }

    void BlockCollect::get_servers(ArrayHelper<uint64_t>& servers) const
    {
      CONST_SERVER_ITER iter = servers_.begin();
      for (; iter != servers_.end(); ++iter)
      {
        servers.push_back(iter->server_);
      }
    }

    uint64_t BlockCollect::get_master() const
    {
      CONST_SERVER_ITER iter = servers_.begin();
      uint64_t result = INVALID_SERVER_ID;
      if (!servers_.empty()
        && (is_in_family() || has_master_()))
      {
        result = iter->server_;
      }
      return result;
    }

    uint64_t BlockCollect::get_family_server(const int64_t family_id) const
    {
      uint64_t result = INVALID_SERVER_ID;
      if (is_in_family())
      {
        CONST_SERVER_ITER iter = servers_.begin();
        // make sure family_id correct
        for (; iter != servers_.end(); ++iter)
        {
          if (iter->family_id_ == family_id)
          {
            result = iter->server_;
            break;
          }
        }
      }
      return result;
    }

    /**
     * to check a block if replicate
     * @return: -1: none, 0: normal, 1: emergency
     */
    PlanPriority BlockCollect::check_replicate(const time_t now) const
    {
      PlanPriority priority = PLAN_PRIORITY_NONE;
      if (!is_creating() && !is_in_family() && task_expired_timeout_(now) && !IS_VERFIFY_BLOCK(id())
        && leave_timeout_(now) && !in_replicate_queue() && !has_valid_lease(now))
      {
        const int32_t size = servers_.size();
        if (size <= 0)
        {
          TBSYS_LOG(WARN, "block: %" PRI64_PREFIX "u has been lost, do not replicate", info_.block_id_);
        }
        else
        {
          if (size < SYSPARAM_NAMESERVER.max_replication_)
            priority = PLAN_PRIORITY_NORMAL;
          if (1 == size && SYSPARAM_NAMESERVER.max_replication_ > 1)
            priority = PLAN_PRIORITY_EMERGENCY;
        }
      }
      return priority;
    }

    /**
     * to check a block if compact
     * @return: return true if need compact
     */
    bool BlockCollect::check_compact(const time_t now, const bool check_in_family) const
    {
      bool ret = false;
      bool check = check_in_family ? !is_in_family() : true;
      if (!is_creating() && check && !in_replicate_queue() && task_expired_timeout_(now) && is_full()
          && !has_valid_lease(now) && check_copies_complete() && !IS_VERFIFY_BLOCK(id()))
      {
        if ((info_.file_count_ > 0)
            && (info_.size_ > 0)
            && (info_.del_file_count_ > 0)
            && (info_.del_size_ > 0))
        {
          const int32_t delete_file_num_ratio = get_delete_file_num_ratio_();
          const int32_t delete_size_ratio = get_delete_file_size_ratio_();
          const int32_t update_file_num_ratio = get_update_file_num_ratio_();
          const int32_t update_size_ratio = get_update_file_size_ratio_();
          if ((delete_file_num_ratio >  SYSPARAM_NAMESERVER.compact_delete_ratio_)
              || (delete_size_ratio > SYSPARAM_NAMESERVER.compact_delete_ratio_)
              || (update_file_num_ratio >  SYSPARAM_NAMESERVER.compact_update_ratio_)
              || (update_size_ratio > SYSPARAM_NAMESERVER.compact_update_ratio_))
          {
            TBSYS_LOG(DEBUG, "block: %" PRI64_PREFIX "u need compact", info_.block_id_);
            ret = true;
          }
        }
      }
      return ret;
    }

    bool BlockCollect::check_balance(const time_t now) const
    {
      return (!is_creating() && !in_replicate_queue() && !has_valid_lease(now)
              && task_expired_timeout_(now) && check_copies_complete());
    }

    bool BlockCollect::check_marshalling(const time_t now) const
    {
      bool ret = (!is_creating() && !is_in_family() && !in_replicate_queue() && !has_valid_lease(now) && is_full()
              && expire(now) && check_copies_complete() && size() <= MAX_MARSHALLING_BLOCK_SIZE_LIMIT
              && !IS_VERFIFY_BLOCK(id()) && info_.file_count_ <= MAX_MARSHALLING_FILE_COUNT);
      if (ret)
      {
        const int32_t server_size = servers_.size();
        ret = (server_size >= SYSPARAM_NAMESERVER.max_replication_  && is_full() && size() <= MAX_MARSHALLING_BLOCK_SIZE_LIMIT);
        if (ret)
        {
          const int32_t delete_file_num_ratio = get_delete_file_num_ratio_();
          const int32_t delete_size_ratio = get_delete_file_size_ratio_();
          const int32_t marshalling_visit_time = (time(NULL) - info_.last_access_time_) / 86400;
          ret = (delete_file_num_ratio <= SYSPARAM_NAMESERVER.marshalling_delete_ratio_
                && delete_size_ratio <= SYSPARAM_NAMESERVER.marshalling_delete_ratio_
                && marshalling_visit_time >= SYSPARAM_NAMESERVER.marshalling_visit_time_);
        }
      }
      return ret;
    }

    bool BlockCollect::check_reinstate(const time_t now) const
    {
      return (!is_creating() && is_in_family() && servers_.empty()
        && task_expired_timeout_(now) && leave_timeout_(now));
    }

    static uint32_t* query_item(uint32_t* array, const uint32_t lan)
    {
      assert(NULL != array);
      uint32_t* result = NULL;
      for (int32_t index = 0; index < MAX_REPLICATION_NUM && 0U != lan && NULL == result; ++index)
      {
        if (array[index] == lan)
          result = &array[index];
        else if (0U == array[index])
          result = &array[index];
      }
      return result;
    }

    bool BlockCollect::resolve_invalid_copies(common::ArrayHelper<ServerItem>& invalids,
      common::ArrayHelper<ServerItem>& clean_familyinfo, const time_t now, const common::ArrayHelper<ServerRack>& server_rack_helper)
    {
      invalids.clear();
      clean_familyinfo.clear();
      assert(invalids.get_array_size() >= MAX_REPLICATION_NUM);
      bool ret = (!is_creating() && expire(now) && servers_.size() >= 1U);
      if (ret)
      {
        const int32_t MAX_COPIES = is_in_family() ? 1 : common::SYSPARAM_NAMESERVER.max_replication_;
        uint32_t lans[MAX_REPLICATION_NUM] = {0};
        int32_t max_version = -1, overage = 0;
        SERVER_ITER iter = servers_.begin();
        if (INVALID_FAMILY_ID != info_.family_id_)
        {
          for (iter = servers_.begin(); iter != servers_.end(); ++iter)
          {
            if (iter->family_id_ != info_.family_id_)
              invalids.push_back((*iter));
          }
        }
        else
        {
          for (iter = servers_.begin(); iter != servers_.end(); ++iter)
          {
            if (INVALID_FAMILY_ID != iter->family_id_)
            {
              clean_familyinfo.push_back((*iter));
              iter->family_id_ = INVALID_FAMILY_ID;
            }
          }
        }
        overage = servers_.size() - invalids.get_array_index();
        if (overage > 0)
        {
          for (iter = servers_.begin(); iter != servers_.end(); ++iter)
          {
            if (max_version < iter->version_ && !invalids.exist((*iter)))
              max_version = iter->version_;
          }
          info_.version_ = max_version;

          for (iter = servers_.begin(); iter != servers_.end(); ++iter)
          {
            if (iter->version_ != max_version && !invalids.exist((*iter)))
              invalids.push_back((*iter));
          }
        }

        overage = servers_.size() - invalids.get_array_index();
        for (iter = servers_.begin(); overage > 1 && iter != servers_.end(); ++iter)
        {
          if (!invalids.exist((*iter)))
          {
            ServerRack rack(iter->server_, 0);
            ServerRack* prack = server_rack_helper.get(rack);
            if (NULL == prack)
            {
              continue;
            }
            uint32_t lan = prack->rack_id_;
            uint32_t* result = query_item(lans, lan);
            if (NULL != result)
            {
              if (0U == (*result))
                *result = lan;
              else
                invalids.push_back((*iter));
            }
          }
        }

        overage = servers_.size() - invalids.get_array_index() - MAX_COPIES;
        REVERSE_SERVER_ITER it = servers_.rbegin();
        for (; it != servers_.rend() && overage > 0; ++it)
        {
          if (!invalids.exist((*it)))
          {
            invalids.push_back((*it));
            --overage;
          }
        }
        assert(servers_.size() >= (uint64_t)invalids.get_array_index());

        if (invalids.get_array_index() > 0)
        {
          std::stringstream all, abnormal;
          print_int64(invalids, abnormal);
          print_int64(servers_, all);
          TBSYS_LOG(INFO, "block: %lu, resolve_version_conflict: all %s, abnormal %s", id(), all.str().c_str(), abnormal.str().c_str());
        }
      }
      return ret;
    }

    int BlockCollect::scan(SSMScanParameter& param) const
    {
      int16_t child_type = param.child_type_;
      bool has_dump = (child_type & SSM_CHILD_BLOCK_TYPE_FULL) ? is_full() : true;
      if (has_dump)
      {
        if (child_type & SSM_CHILD_BLOCK_TYPE_INFO)
        {
          int64_t pos = 0;
          param.data_.ensureFree(info_.length());
          int32_t ret = info_.serialize(param.data_.getFree(), param.data_.getFreeLen(), pos);
          if (TFS_SUCCESS == ret)
            param.data_.pourData(info_.length());
        }
        if (child_type & SSM_CHILD_BLOCK_TYPE_SERVER)
        {
          uint8_t count = 0;
          param.data_.writeInt8(count);
          CONST_SERVER_ITER iter = servers_.begin();
          for (; iter != servers_.end(); ++iter)
          {
            ++count;
            param.data_.writeInt64(iter->server_);
            param.data_.writeInt64(iter->family_id_);
            param.data_.writeInt32(iter->version_);
          }
          // data addr will change when expand, so can't keep absolute addr
          unsigned char* pdata = reinterpret_cast<unsigned char*>(param.data_.getFree() - count * (INT64_SIZE + INT_SIZE + INT64_SIZE) - INT8_SIZE);
          param.data_.fillInt8(pdata, count);
          if (count != servers_.size())
          {
            dump(TBSYS_LOG_LEVEL(WARN));
          }
        }
        if (child_type & SSM_CHILD_BLOCK_TYPE_STATUS)
        {
          param.data_.writeInt64(get());
          param.data_.writeInt8(create_flag_);
          param.data_.writeInt8(in_replicate_queue_);
          param.data_.writeInt8(has_lease_);
          param.data_.writeInt8(choose_master_);
          param.data_.writeInt64(last_leave_time_);
        }
      }
      return has_dump ? TFS_SUCCESS : TFS_ERROR;
    }

    int BlockCollect::apply_lease(const uint64_t server, const time_t now, const int32_t step, const bool update,
        common::ArrayHelper<ServerItem>& helper, common::ArrayHelper<ServerItem>& clean_familyinfo,
        const common::ArrayHelper<ServerRack>& server_rack_helper)
    {
      int32_t ret = (INVALID_SERVER_ID != server) ? TFS_SUCCESS : EXIT_PARAMETER_ERROR;
      if (TFS_SUCCESS == ret)
      {
        ret = IS_VERFIFY_BLOCK(id()) ? EXIT_VERIYF_BLOCK_CANNOT_APPLY_LESAE : TFS_SUCCESS;
      }
      if (TFS_SUCCESS == ret)
      {
        ret = has_valid_lease(now) ? EXIT_LEASE_EXISTED : TFS_SUCCESS;
        if (TFS_SUCCESS == ret)
        {
          ret = expire(now) ? TFS_SUCCESS : EXIT_APPLY_BLOCK_SAFE_MODE_TIME_ERROR;
        }
        else
        {
          if (update)
          {
            ret = TFS_SUCCESS;
          }
        }
      }
      if (TFS_SUCCESS == ret)
      {
        resolve_invalid_copies(helper, clean_familyinfo, now, server_rack_helper);
        for (int64_t index = 0; index < helper.get_array_index(); ++index)
        {
          ServerItem* item = helper.at(index);
          remove(item->server_, now);
        }
      }

      if (TFS_SUCCESS == ret)
      {
        if (update && !has_master_())//TODO
        {
          ServerItem* result = get_(server);
          if (NULL != result)
          {
            ServerItem new_item = *result;
            remove(server, now);
            servers_.insert(servers_.begin(), new_item);
            choose_master_ = BLOCK_CHOOSE_MASTER_COMPLETE_FLAG_YES;
          }
        }
      }

      if (TFS_SUCCESS == ret)
      {
        bool check = update ? true : (!is_in_family() && is_writable());
        if (check)
        {
          if (update &&
              (SYSPARAM_NAMESERVER.global_switch_ & ENABLE_INCOMPLETE_UPDATE))
          {
            check = is_master(server);
          }
          else
          {
            check = is_master(server) && check_copies_complete();
          }
        }
        ret = check ? TFS_SUCCESS : EXIT_CANNOT_APPLY_LEASE;
      }
      if (TFS_SUCCESS == ret)
      {
        set(now, step);
        set_has_lease(BLOCK_HAS_LEASE_FLAG_YES);
      }
      return ret;
    }

    int BlockCollect::renew_lease(const uint64_t server, const time_t now, const int32_t step, const bool update,
        const common::BlockInfoV2& info,common::ArrayHelper<ServerItem>& helper, common::ArrayHelper<ServerItem>& clean_familyinfo,
        const common::ArrayHelper<ServerRack>& server_rack_helper)
    {
      int32_t ret = (INVALID_SERVER_ID != server) ? TFS_SUCCESS : EXIT_PARAMETER_ERROR;
      if (TFS_SUCCESS == ret)
      {
        ret = IS_VERFIFY_BLOCK(id()) ? EXIT_VERIYF_BLOCK_CANNOT_APPLY_LESAE : TFS_SUCCESS;
      }
      if (TFS_SUCCESS == ret)
      {
        ret = has_valid_lease(now) ? TFS_SUCCESS: EXIT_LEASE_EXPIRED;
      }
      const int32_t diff = info.version_ - info_.version_;
      if (TFS_SUCCESS == ret && diff > 0)
      {
        update_info(info);
        set_version(info.version_);
        update_all_version(diff);
      }
      if (TFS_SUCCESS == ret)
      {
        resolve_invalid_copies(helper, clean_familyinfo, now, server_rack_helper);
        for (int64_t index = 0; index < helper.get_array_index(); ++index)
        {
          ServerItem* item = helper.at(index);
          remove(item->server_, now);
        }
      }

      if (TFS_SUCCESS == ret)
      {
        if (update && !has_master_())
        {
          ServerItem* result = get_(server);
          ServerItem new_item = *result;
          remove(server, now);
          servers_.insert(servers_.begin(), new_item);
          choose_master_ = BLOCK_CHOOSE_MASTER_COMPLETE_FLAG_YES;
        }
      }

      if (TFS_SUCCESS == ret)
      {
        bool check = update ? true : (!is_in_family() && is_writable());
        ret = check && is_master(server) && check_copies_complete() ? TFS_SUCCESS : EXIT_CANNOT_RENEW_LEASE;
      }
      if (TFS_SUCCESS == ret)
      {
        set(now, step);
      }
      return ret;
    }

    int BlockCollect::giveup_lease(const uint64_t server, const time_t now, const common::BlockInfoV2* info)
    {
      UNUSED(server);
      int32_t ret = (INVALID_SERVER_ID != server) ? TFS_SUCCESS : EXIT_PARAMETER_ERROR;
      if (TFS_SUCCESS == ret)
      {
        set(now, 0);
        set_has_lease(BLOCK_HAS_LEASE_FLAG_NO);
        if (NULL != info && info->version_ > info_.version_)
        {
          const int32_t diff = info->version_ - info_.version_;
          update_info(*info);
          set_version(info->version_);
          update_all_version(diff);
        }
      }
      return ret;
    }

    void BlockCollect::update_version(const common::ArrayHelper<uint64_t>& helper, const int32_t step)
    {
      for (int64_t index = 0; index < helper.get_array_index(); ++index)
      {
        ServerItem* item = get_(*helper.at(index));
        if (NULL != item)
          item->version_ += step;
      }
    }

    void BlockCollect::update_version(const uint64_t server, const int32_t version)
    {
      ServerItem* item = get_(server);
      if (NULL != item)
        item->version_ = version;
    }

    void BlockCollect::update_family_id(const uint64_t server, const int64_t family_id)
    {
      ServerItem* item = get_(server);
      if (NULL != item)
        item->family_id_ = family_id;
    }

    void BlockCollect::update_all_version(const int32_t step)
    {
      SERVER_ITER iter = servers_.begin();
      for (; iter != servers_.end(); ++iter)
      {
        iter->version_+= step;
      }
    }

    void BlockCollect::update_info(const common::BlockInfoV2& info)
    {
      info_.size_ = info.size_;
      info_.file_count_ = info.file_count_;
      info_.del_size_   = info.del_size_;
      info_.del_file_count_ = info.del_file_count_;
      info_.update_size_    = info.update_size_;
      info_.update_file_count_ = info.update_file_count_;
      info_.last_access_time_  = info.last_access_time_;
    }

    void BlockCollect::dump(int32_t level, const char* file, const int32_t line, const char* function, const pthread_t thid) const
    {
      if (level <= TBSYS_LOGGER._level)
      {
        std::stringstream str;
        CONST_SERVER_ITER iter = servers_.begin();
        for (; iter != servers_.end(); ++iter)
        {
          str << CNetUtil::addrToString(iter->server_) << ":" << iter->family_id_ << ":" << iter->version_<< "/";
        }
        int64_t now = Func::get_monotonic_time();
        TBSYS_LOGGER.logMessage(level, file, line, function,thid,
            "dump block information: family id: %" PRI64_PREFIX "d, block_id: %" PRI64_PREFIX "u, version: %d, file_count: %d,\
            size: %d, del_file_count: %d, del_size: %d, update_file_count: %d, update_file_size: %d, is_creating: %s,\
            in_replicate_queue: %s, has_lease: %s, expire time: %" PRI64_PREFIX "d, has_valild_lease: %s, servers: %s, \
            server_size: %zd, last_leave_time: %" PRI64_PREFIX "d, choose_master: %d, now: %" PRI64_PREFIX "d",
            info_.family_id_, info_.block_id_, info_.version_, info_.file_count_,
            info_.size_, info_.del_file_count_, info_.del_size_,info_.update_file_count_,
            info_.update_size_,is_creating() ? "yes" : "no", in_replicate_queue() ? "yes" : "no",
            has_lease() ? "yes" : "no", get(), has_valid_lease(now) ? "yes" : "now", str.str().c_str(),
            servers_.size(), last_leave_time_, choose_master_, now);
      }
    }

    void BlockCollect::callback(void* args, LayoutManager& manager)
    {
      UNUSED(args);
      uint64_t servers[MAX_REPLICATION_NUM];
      ArrayHelper<uint64_t> helper(MAX_REPLICATION_NUM, servers);
      manager.get_block_manager().get_mutex_(id()).wrlock();
      get_servers(helper);
      servers_.clear();
      manager.get_block_manager().get_mutex_(id()).unlock();
      for (int64_t index = 0; index < helper.get_array_index(); ++index)
      {
        uint64_t server = *helper.at(index);
        ServerCollect* pserver = manager.get_server_manager().get(server);
        manager.get_server_manager().relieve_relation(pserver, id());
      }
    }

    ServerItem* BlockCollect::get_(const uint64_t server)
    {
      ServerItem* result = NULL;
      SERVER_ITER iter = servers_.begin();
      for (; iter != servers_.end() && NULL == result; ++iter)
      {
        if (server == iter->server_)
        {
          result = &(*iter);
        }
      }
      return result;
    }

  }/** end namesapce nameserver **/
}/** end namesapce tfs **/
