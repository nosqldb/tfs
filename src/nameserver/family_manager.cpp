/*
 * (C) 2007-2012 Alibaba Group Holding Limited.
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

#include "global_factory.h"
#include "layout_manager.h"
#include "family_manager.h"

using namespace tfs::common;

namespace tfs
{
  namespace nameserver
  {
    static const int32_t MAX_FAMILY_CHUNK_NUM_SLOT_DEFALUT = 512;
    static const int32_t MAX_FAMILY_CHUNK_NUM_SLOT_EXPAND_DEFAULT = 256;
    static const float   MAX_FAMILY_CHUNK_NUM_SLOT_EXPAND_RATION_DEFAULT = 0.1;
    int FamilyManager::MarshallingItem::insert(const uint64_t server, const uint64_t block)
    {
      int32_t ret = (INVALID_SERVER_ID != server && INVALID_BLOCK_ID != block) ? TFS_SUCCESS : EXIT_PARAMETER_ERROR;
      if (TFS_SUCCESS == ret)
      {
        int32_t index = -1;
        bool exist    = false;
        last_update_time_ = Func::get_monotonic_time();
        for (int32_t i = 0; i < MAX_SINGLE_RACK_SERVER_NUM && !exist; i++)
        {
          exist = slot_[i].first == server;
          if (!exist && index < 0)
          {
            if (INVALID_SERVER_ID == slot_[i].first)
              index = i;
          }
        }
        ret = !exist ? TFS_SUCCESS : EXIT_ELEMENT_EXIST;
        ret = (TFS_SUCCESS == ret && index >= 0 && index < MAX_SINGLE_RACK_SERVER_NUM) ? TFS_SUCCESS : EXIT_OUT_OF_RANGE;
        if (TFS_SUCCESS == ret)
        {
          slot_[index].first = server;
          slot_[index].second= block;
          ++slot_num_;
        }
      }
      return ret;
    }

    int FamilyManager::MarshallingItem::choose_item_random(std::pair<uint64_t, uint64_t>& out)
    {
      int32_t ret = slot_num_ > 0  ? TFS_SUCCESS : EXIT_MARSHALLING_ITEM_QUEUE_EMPTY;
      if (TFS_SUCCESS == ret)
      {
        int32_t index = 0;
        int32_t random_num = 0;
        const int32_t MAX_RANDOM_NUM = 4;
        last_update_time_ = Func::get_monotonic_time();
        do
        {
          index = random() % MAX_SINGLE_RACK_SERVER_NUM;
          ret = INVALID_SERVER_ID != slot_[index].first ? TFS_SUCCESS : EXIT_MARSHALLING_ITEM_QUEUE_EMPTY;
        }
        while (random_num++ < MAX_RANDOM_NUM && TFS_SUCCESS != ret);
        if (TFS_SUCCESS != ret)
        {
          index = 0;
          do
          {
            ret = INVALID_SERVER_ID != slot_[index].first ? TFS_SUCCESS : EXIT_MARSHALLING_ITEM_QUEUE_EMPTY;
          }
          while (TFS_SUCCESS != ret && ++index < MAX_SINGLE_RACK_SERVER_NUM);
        }
        if (TFS_SUCCESS == ret && index >= 0 && index < MAX_SINGLE_RACK_SERVER_NUM)
        {
          --slot_num_;
          out = slot_[index];
          slot_[index].first = INVALID_SERVER_ID;
          slot_[index].second= INVALID_BLOCK_ID;
        }
      }
      return ret;
    }

    #ifdef TFS_GTEST
    bool FamilyManager::MarshallingItem::get(std::pair<uint64_t, uint64_t>& pair, const uint64_t server, const uint64_t block) const
    {
      bool exist = false;
      for (int32_t index = 0; index < MAX_SINGLE_RACK_SERVER_NUM && !exist; ++index)
      {
        exist = ((slot_[index].first == server) && (slot_[index].second == block));
        if (exist)
          pair = slot_[index];
      }
      return exist;
    }
    #endif

    void FamilyManager::MarshallingItem::dump(const int32_t level, const char* format) const
    {
      if (level <= TBSYS_LOGGER._level)
      {
        std::stringstream str;
        str << " rack: " << rack_ << " slot_num: " << slot_num_ << " last_update_time: " << last_update_time_;
        for (int32_t index = 0; index < MAX_SINGLE_RACK_SERVER_NUM; ++index)
        {
          if (slot_[index].first != INVALID_SERVER_ID)
            str << " server: " << tbsys::CNetUtil::addrToString(slot_[index].first) << " block: " << slot_[index].second;
        }
        TBSYS_LOGGER.logMessage(level, __FILE__, __LINE__, __FUNCTION__, pthread_self(),"%s %s", NULL == format ? "" : format, str.str().c_str());
      }
    }

    FamilyManager::FamilyManager(LayoutManager& manager):
      manager_(manager),
      marshalling_queue_(MAX_RACK_NUM, 1, 0.1)
    {
      for (int32_t i = 0; i < MAX_FAMILY_CHUNK_NUM; ++i)
      {
        max_family_ids_[i] = INVALID_FAMILY_ID;
        families_[i] = new (std::nothrow)TfsSortedVector<FamilyCollect*, FamilyIdCompare>(MAX_FAMILY_CHUNK_NUM_SLOT_DEFALUT,
            MAX_FAMILY_CHUNK_NUM_SLOT_EXPAND_DEFAULT, MAX_FAMILY_CHUNK_NUM_SLOT_EXPAND_RATION_DEFAULT);
        assert(families_[i]);
      }
    }

    FamilyManager::~FamilyManager()
    {
      for (int32_t i = 0; i < MAX_FAMILY_CHUNK_NUM; ++i)
      {
        FAMILY_MAP_ITER iter = families_[i]->begin();
        for (; iter != families_[i]->end(); ++iter)
        {
          tbsys::gDelete((*iter));
        }
        tbsys::gDelete(families_[i]);
      }
      MARSHALLING_MAP_ITER it = marshalling_queue_.begin();
      for (; it != marshalling_queue_.end(); ++it)
      {
        tbsys::gDelete((*it));
      }
    }

    int FamilyManager::insert(const int64_t family_id, const int32_t family_aid_info,
          const common::ArrayHelper<std::pair<uint64_t, int32_t> >& members, const time_t now)
    {
      RWLock::Lock lock(get_mutex_(family_id), WRITE_LOCKER);
      return insert_(family_id, family_aid_info, members, now);
    }

    bool FamilyManager::exist(const int64_t family_id, const uint64_t block) const
    {
      bool ret = (INVALID_FAMILY_ID != family_id && INVALID_BLOCK_ID != block);
      if (ret)
      {
        RWLock::Lock lock(get_mutex_(family_id), READ_LOCKER);
        FamilyCollect* family = get_(family_id);
        ret = family->exist(block);
      }
      return ret;
    }

    bool FamilyManager::exist(int32_t& version, const int64_t family_id, const uint64_t block, const int32_t new_version)
    {
      version = 0;
      bool ret = (INVALID_FAMILY_ID != family_id && INVALID_BLOCK_ID != block && new_version > 0);
      if (ret)
      {
        RWLock::Lock lock(get_mutex_(family_id), READ_LOCKER);
        FamilyCollect* family = get_(family_id);
        ret = family->exist(version, block, new_version);
      }
      return ret;
    }

    int FamilyManager::update(const int64_t family_id, const uint64_t block, const int32_t new_version)
    {
      int32_t ret = (INVALID_FAMILY_ID != family_id && INVALID_BLOCK_ID != block && new_version > 0) ? TFS_SUCCESS : EXIT_PARAMETER_ERROR;
      if (TFS_SUCCESS == ret)
      {
        RWLock::Lock lock(get_mutex_(family_id), WRITE_LOCKER);
        FamilyCollect* family = get_(family_id);
        ret = (NULL != family) ? TFS_SUCCESS : EXIT_NO_FAMILY;
        if (TFS_SUCCESS == ret)
        {
          ret = family->update(block, new_version);
        }
      }
      return ret;
    }

    int FamilyManager::remove(FamilyCollect*& object, const int64_t family_id)
    {
      object = NULL;
      int32_t ret = (INVALID_FAMILY_ID != family_id) ? TFS_SUCCESS : EXIT_PARAMETER_ERROR;
      if (TFS_SUCCESS == ret)
      {
        RWLock::Lock lock(get_mutex_(family_id), WRITE_LOCKER);
        FamilyCollect query(family_id);
        object = families_[get_chunk_(family_id)]->erase(&query);
        ret = NULL != object ? TFS_SUCCESS : EXIT_NO_FAMILY;
      }
      return ret;
    }

    FamilyCollect* FamilyManager::get(const int64_t family_id) const
    {
      RWLock::Lock lock(get_mutex_(family_id), READ_LOCKER);
      return get_(family_id);
    }

    bool FamilyManager::scan(common::ArrayHelper<FamilyCollect*>& result, int64_t& begin, const int32_t count) const
    {
      bool end  = false;
      int32_t actual = 0;
      int32_t next = get_chunk_(begin);
      FamilyCollect query(begin);
      bool all_over = next >= MAX_FAMILY_CHUNK_NUM;
      for (; next < MAX_FAMILY_CHUNK_NUM && actual < count;)
      {
        mutexs_[next].rdlock();
        FAMILY_MAP_ITER iter = ((0 == begin) || end) ? families_[next]->begin() : families_[next]->lower_bound(&query);
        for (; iter != families_[next]->end(); ++iter)
        {
          result.push_back(*iter);
          if (++actual >= count)
          {
            ++iter;
            break;
          }
        }
        end = (families_[next]->end() == iter);
        all_over = ((next == MAX_FAMILY_CHUNK_NUM - 1) && end);
        if (!end)
          begin = (*iter)->get_family_id();
        mutexs_[next].unlock();
        if (end)
        {
          ++next;
          begin = 0;
          while ((0 == begin) && (next < MAX_FAMILY_CHUNK_NUM))
          {
            mutexs_[next].rdlock();
            if (!families_[next]->empty())
            {
              begin = (*families_[next]->begin())->get_family_id();
              mutexs_[next].unlock();
            }
            else
            {
              mutexs_[next].unlock();
              ++next;
            }
          }
        }
      }
      return all_over;
    }

    int FamilyManager::scan(common::SSMScanParameter& param, int32_t& next, bool& all_over,
        bool& cutover, const int32_t should) const
    {
      int32_t actual = 0;
      FAMILY_MAP_ITER iter;
      all_over = next >= MAX_FAMILY_CHUNK_NUM;
      while (next < MAX_FAMILY_CHUNK_NUM && actual < should)
      {
        RWLock::Lock lock(mutexs_[next], READ_LOCKER);
        FamilyCollect query(param.addition_param1_);
        iter = cutover ?  families_[next]->begin() : families_[next]->lower_bound(&query);
        if (cutover && iter != families_[next]->end())
          param.addition_param1_ = (*iter)->get_family_id();
        for(;families_[next]->end() != iter; ++iter)
        {
          if (TFS_SUCCESS == (*iter)->scan(param))
          {
            if (++actual >= should)
            {
              ++iter;
              break;
            }
          }
        }
        cutover = families_[next]->end() == iter;
        if (!cutover)
          param.addition_param2_ = (*iter)->get_family_id();
        else
          ++next;
      }
      all_over = (next >= MAX_FAMILY_CHUNK_NUM) ? true : (MAX_FAMILY_CHUNK_NUM - 1 == next) ? cutover : false;
      return actual;
    }

    int FamilyManager::get_members(common::ArrayHelper<std::pair<uint64_t, int32_t> >& members, const int64_t family_id) const
    {
      int32_t ret = (INVALID_FAMILY_ID != family_id && members.get_array_size() > 0 ) ? TFS_SUCCESS : EXIT_PARAMETER_ERROR;
      if (TFS_SUCCESS == ret)
      {
        RWLock::Lock lock(get_mutex_(family_id), READ_LOCKER);
        ret = get_members_(members, family_id);
      }
      return ret;
    }

    int FamilyManager::get_members(common::ArrayHelper<common::FamilyMemberInfo>& members,
          const common::ArrayHelper<common::FamilyMemberInfo>& abnormal_members, const int64_t family_id) const
    {
      int32_t ret = (INVALID_FAMILY_ID != family_id && members.get_array_size() > 0) ? TFS_SUCCESS : EXIT_PARAMETER_ERROR;
      if (TFS_SUCCESS == ret)
      {
        BlockManager& bm = manager_.get_block_manager();
        std::pair<uint64_t, int32_t> result[MAX_MARSHALLING_NUM];
        common::ArrayHelper<std::pair<uint64_t, int32_t> > helper(MAX_MARSHALLING_NUM, result);
        get_mutex_(family_id).rdlock();
        ret = get_members_(helper, family_id);
        get_mutex_(family_id).unlock();
        if (TFS_SUCCESS == ret)
        {
          FamilyMemberInfo info;
          for (int64_t index = 0; index < helper.get_array_index() && TFS_SUCCESS == ret; ++index)
          {
            std::pair<uint64_t, int32_t>* item = helper.at(index);
            info.block_   = item->first;
            info.version_ = item->second;
            info.server_  = INVALID_SERVER_ID;
            info.status_  = FAMILY_MEMBER_STATUS_ABNORMAL;
            FamilyMemberInfo* pbfmi = abnormal_members.get(info);
            if (NULL == pbfmi)
            {
              info.server_ =  bm.get_master(item->first);
              ret = (INVALID_SERVER_ID != info.server_) ? TFS_SUCCESS : EXIT_DATASERVER_NOT_FOUND;
              if (TFS_SUCCESS == ret)
                info.status_ = FAMILY_MEMBER_STATUS_NORMAL;
            }
            if (TFS_SUCCESS == ret)
              members.push_back(info);
          }
        }
      }
      return ret;
    }

    int FamilyManager::get_members(common::ArrayHelper<std::pair<uint64_t, uint64_t> >& members, int32_t& family_aid_info, const int64_t family_id) const
    {
      int32_t ret = (INVALID_FAMILY_ID != family_id) ? TFS_SUCCESS : EXIT_PARAMETER_ERROR;
      if (TFS_SUCCESS == ret)
      {
        BlockManager& bm = manager_.get_block_manager();
        std::pair<uint64_t, int32_t> result[MAX_MARSHALLING_NUM];
        common::ArrayHelper<std::pair<uint64_t, int32_t> > helper(MAX_MARSHALLING_NUM, result);
        FamilyCollect* family = get(family_id);
        ret = NULL != family ? TFS_SUCCESS : EXIT_NO_FAMILY;
        if (TFS_SUCCESS == ret)
        {
          family_aid_info = family->get_family_aid_info();
          ret = get_members_(helper, family_id);
        }
        if (TFS_SUCCESS == ret)
        {
          for (int64_t index = 0; index < helper.get_array_index(); ++index)
          {
            std::pair<uint64_t, int32_t>* item = helper.at(index);
            assert(NULL != item);
            std::pair<uint64_t, uint64_t> new_item;
            new_item.first = item->first;
            new_item.second = bm.get_master(item->first);
            members.push_back(new_item);
          }
        }
      }
      return ret;
    }

    bool FamilyManager::push_to_reinstate_or_dissolve_queue(FamilyCollect* family, const int32_t type)
    {
      bool ret = ((NULL != family) && (!family->in_reinstate_or_dissolve_queue()));
      if (ret)
      {
        const char* str = type == PLAN_TYPE_EC_DISSOLVE ? "dissolve" : type == PLAN_TYPE_EC_REINSTATE ? "reinstate" : "unknow";
        TBSYS_LOG(DEBUG, "family %"PRI64_PREFIX"d mybe lack of backup, we'll %s", family->get_family_id(), str);
        family->set_in_reinstate_or_dissolve_queue(FAMILY_IN_REINSTATE_OR_DISSOLVE_QUEUE_YES);
        tbutil::Mutex::Lock lock(reinstate_or_dissolve_queue_mutex_);
        reinstate_or_dissolve_queue_.push_back(family->get_family_id());
      }
      return ret;
    }

    FamilyCollect* FamilyManager::pop_from_reinstate_or_dissolve_queue()
    {
      FamilyCollect* family = NULL;
      if (!reinstate_or_dissolve_queue_.empty())
      {
        reinstate_or_dissolve_queue_mutex_.lock();
        int64_t family_id = reinstate_or_dissolve_queue_.front();
        reinstate_or_dissolve_queue_.pop_front();
        reinstate_or_dissolve_queue_mutex_.unlock();
        family = get(family_id);
        if (NULL == family)
          TBSYS_LOG(INFO, "family %"PRI64_PREFIX"d mybe delete, don't reinstate or dissolve", family_id);
        else
          family->set_in_reinstate_or_dissolve_queue(FAMILY_IN_REINSTATE_OR_DISSOLVE_QUEUE_NO);
      }
      return family;
    }

    bool FamilyManager::reinstate_or_dissolve_queue_empty() const
    {
      return reinstate_or_dissolve_queue_.empty();
    }

    int64_t FamilyManager::get_reinstate_or_dissolve_queue_size() const
    {
      tbutil::Mutex::Lock lock(reinstate_or_dissolve_queue_mutex_);
      return reinstate_or_dissolve_queue_.size();
    }

    void FamilyManager::clear_reinstate_or_dissolve_queue()
    {
      tbutil::Mutex::Lock lock(reinstate_or_dissolve_queue_mutex_);
      reinstate_or_dissolve_queue_.clear();
    }

    bool FamilyManager::push_block_to_marshalling_queues(const BlockCollect* block, const time_t now)
    {
      bool ret = (NULL != block);
      if (ret)
      {
        uint64_t servers[MAX_REPLICATION_NUM];
        common::ArrayHelper<uint64_t> helper(MAX_REPLICATION_NUM, servers);
        if (ret = (manager_.get_block_manager().need_marshalling(helper, block, now)
          && !manager_.get_task_manager().exist_block(block->id())
          && !manager_.get_task_manager().exist(helper)))
        {
          uint32_t rack = 0;
          int64_t  index = 0;
          uint64_t server = INVALID_SERVER_ID;
          for (; index < helper.get_array_index() && ret; ++index)
          {
            server = *helper.at(index);
            ret = manager_.get_task_manager().has_space_do_task_in_machine(server);
            if (ret)
            {
              rack = SYSPARAM_NAMESERVER.group_mask_ > 0 ? Func::get_lan(server, SYSPARAM_NAMESERVER.group_mask_)
                     : random() % MAX_RACK_NUM;
              ret = push_block_to_marshalling_queues(rack, server, block->id());
            }
          }
          ret = (ret && index == helper.get_array_index());
        }
      }
      return ret;
    }

    int FamilyManager::push_block_to_marshalling_queues(const uint32_t rack, const uint64_t server, const uint64_t block)
    {
      int32_t ret = (INVALID_RACK_ID != rack && INVALID_BLOCK_ID != block && INVALID_SERVER_ID != server) ? TFS_SUCCESS : EXIT_PARAMETER_ERROR;
      if (TFS_SUCCESS == ret)
      {
        RWLock::Lock lock(marshallin_queue_mutex_, WRITE_LOCKER);
        MarshallingItem query;
        query.rack_ = rack;
        MarshallingItem* item = NULL;
        MARSHALLING_MAP_ITER iter = marshalling_queue_.find(&query);
        if (iter == marshalling_queue_.end())
        {
          MarshallingItem* result = NULL;
          item = new (std::nothrow)MarshallingItem();
          assert(item);
          memset(item, 0, sizeof(MarshallingItem));
          item->rack_ = rack;
          item->last_update_time_ = Func::get_monotonic_time();
          ret = marshalling_queue_.insert_unique(result, item);
          assert(TFS_SUCCESS == ret);
        }
        else
        {
          item = (*iter);
        }
        assert(item);
        ret = item->insert(server, block);
      }
      return ret;
    }

    int FamilyManager::marshalling_queue_timeout(const time_t now)
    {
      const int32_t MAX_CLEANUP_NUM = 8;
      MarshallingItem* item = NULL;
      MarshallingItem* items[MAX_CLEANUP_NUM];
      ArrayHelper<MarshallingItem*> helper(MAX_CLEANUP_NUM, items);
      RWLock::Lock lock(marshallin_queue_mutex_, WRITE_LOCKER);
      MARSHALLING_MAP_ITER iter = marshalling_queue_.begin();
      for (;(helper.get_array_index() < MAX_CLEANUP_NUM) && (iter != marshalling_queue_.end()); ++iter)
      {
        item = (*iter);
        if (item->last_update_time_ + SYSPARAM_NAMESERVER.max_marshalling_queue_timeout_ < now)
        {
          helper.push_back(item);
        }
      }

      for (int64_t index = 0; index < helper.get_array_index(); ++index)
      {
        item = *helper.at(index);
        marshalling_queue_.erase(item);
        tbsys::gDelete(item);
      }
      return helper.get_array_index();
    }

    int64_t FamilyManager::get_marshalling_queue_size() const
    {
      RWLock::Lock lock(marshallin_queue_mutex_, READ_LOCKER);
      return marshalling_queue_.size();
    }

    int FamilyManager::create_family_choose_data_members(common::ArrayHelper<std::pair<uint64_t, uint64_t> >& members,
        const int32_t data_member_num)
    {
      int32_t ret = (members.get_array_size() > 0 &&  data_member_num > 0 && data_member_num <= MAX_DATA_MEMBER_NUM)
          ? TFS_SUCCESS : EXIT_PARAMETER_ERROR;
      if (TFS_SUCCESS == ret && !marshalling_queue_.empty())
      {
        int32_t choose_num = 0;
        members.clear();
        std::pair<uint64_t, uint64_t> result;
        const int32_t MAX_LOOP_NUM = data_member_num * 2;
        uint32_t lans[data_member_num];
        uint64_t blocks[data_member_num];
        common::ArrayHelper<uint32_t> helper(data_member_num, lans);
        common::ArrayHelper<uint64_t> arrays(data_member_num, blocks);
        do
        {
          time_t now = Func::get_monotonic_time();
          marshallin_queue_mutex_.rdlock();
          ret = marshalling_queue_.empty() ? EXIT_MARSHALLING_ITEM_QUEUE_EMPTY : TFS_SUCCESS;
          if (TFS_SUCCESS == ret)
          {
            int32_t random_index = random() % marshalling_queue_.size();
            MarshallingItem* item = marshalling_queue_.at(random_index);
            assert(item);
            result.first = INVALID_SERVER_ID;
            result.second = INVALID_BLOCK_ID;
            ret = item->choose_item_random(result);
          }
          marshallin_queue_mutex_.unlock();
          if (TFS_SUCCESS == ret)
          {
            uint32_t lan = Func::get_lan(result.first, SYSPARAM_NAMESERVER.group_mask_);
            //这里没办法精确控制1个SERVER只做一个任务，为了保险需要要DS来做保证，NS只是尽量保证
            #ifdef TFS_GTEST
            bool valid = true;
            UNUSED(now);
            #else
            bool valid = (manager_.get_block_manager().need_marshalling(result.second, now)
                        && !members.exist(result)
                        && !arrays.exist(result.second)
                        && !manager_.get_task_manager().exist_block(result.second)
                        && !manager_.get_task_manager().exist_server(result.first)
                        && manager_.get_task_manager().has_space_do_task_in_machine(result.first)
                        && !helper.exist(lan));
            #endif
            if (valid)
            {
              arrays.push_back(result.second);
              members.push_back(result);
              helper.push_back(lan);
            }
          }
        }
        while (TFS_SUCCESS == ret && members.get_array_index() < data_member_num && ++choose_num < MAX_LOOP_NUM);
      }
      return ret;
    }

    int FamilyManager::create_family_choose_check_members(common::ArrayHelper<std::pair<uint64_t, uint64_t> >& members,
        common::ArrayHelper<uint64_t>& already_exist, const int32_t check_member_num)
    {
      int32_t ret = (members.get_array_size() > 0 &&  check_member_num > 0 && check_member_num <= MAX_CHECK_MEMBER_NUM)
          ? TFS_SUCCESS : EXIT_PARAMETER_ERROR;
      if (TFS_SUCCESS == ret)
      {
        const int32_t MAX_CHOOSE_NUM = check_member_num * 2;
        uint64_t result[MAX_CHOOSE_NUM];
        ArrayHelper<uint64_t> helper(MAX_CHOOSE_NUM, result);
        manager_.get_server_manager().choose_create_block_target_server(already_exist, helper, MAX_CHOOSE_NUM);
        ret = helper.get_array_index() < check_member_num ? EXIT_CHOOSE_TARGET_SERVER_INSUFFICIENT_ERROR : TFS_SUCCESS;
        if (TFS_SUCCESS == ret)
        {
          for (int32_t index = 0; index < helper.get_array_index(); ++index)
          {
            uint64_t server = *helper.at(index);
            if (manager_.get_task_manager().has_space_do_task_in_machine(server))
            {
              uint64_t block = manager_.get_alive_block_id(true);
              members.push_back(std::make_pair(server, block));
            }
          }
        }
      }
      return ret;
    }

    int FamilyManager::reinstate_family_choose_members(common::ArrayHelper<uint64_t>& results,
        const int64_t family_id, const int32_t family_aid_info, const int32_t member_num)
    {
      UNUSED(family_aid_info);
      BlockManager& bm = manager_.get_block_manager();
      ServerManager& sm = manager_.get_server_manager();
      int32_t ret = (INVALID_FAMILY_ID != family_id && results.get_array_size() > 0
          && CHECK_MEMBER_NUM(member_num)) ? TFS_SUCCESS : EXIT_PARAMETER_ERROR;
      if (TFS_SUCCESS == ret)
      {
        uint64_t servers[MAX_MARSHALLING_NUM];
        common::ArrayHelper<uint64_t> helper2(MAX_MARSHALLING_NUM, servers);
        uint64_t choose_results[member_num];
        ArrayHelper<uint64_t > choose_result_helper(member_num, choose_results);
        std::pair<uint64_t, int32_t> result[MAX_MARSHALLING_NUM];
        common::ArrayHelper<std::pair<uint64_t, int32_t> > helper(MAX_MARSHALLING_NUM, result);
        ret = get_members_(helper, family_id);
        if (TFS_SUCCESS == ret)
        {
          for (int64_t index = 0; index < helper.get_array_index(); ++index)
          {
            std::pair<uint64_t, int32_t>* item = helper.at(index);
            assert(NULL != item);
            uint64_t server = bm.get_master(item->first);
            if (INVALID_SERVER_ID != server)
              helper2.push_back(server);
          }

          sm.choose_create_block_target_server(helper2, choose_result_helper, member_num);
          ret = choose_result_helper.get_array_index() == member_num ? TFS_SUCCESS : EXIT_CHOOSE_TARGET_SERVER_INSUFFICIENT_ERROR;
        }
        if (TFS_SUCCESS == ret)
        {
          for (int64_t index = 0; index < choose_result_helper.get_array_index(); ++index)
          {
            uint64_t server = *choose_result_helper.at(index);
            results.push_back(server);
          }
        }
      }
      return ret;
    }

    int FamilyManager::dissolve_family_choose_member_targets_server(common::ArrayHelper<std::pair<uint64_t, uint64_t> >& results,
        const int64_t family_id, const int32_t family_aid_info)
    {
      BlockManager& bm = manager_.get_block_manager();
      int32_t ret = (INVALID_FAMILY_ID != family_id && results.get_array_size() > 0) ? TFS_SUCCESS : EXIT_PARAMETER_ERROR;
      if (TFS_SUCCESS == ret)
      {
        int64_t index = 0;
        ServerCollect* target = NULL;
        uint64_t servers[MAX_REPLICATION_NUM];
        common::ArrayHelper<uint64_t> helper2(MAX_REPLICATION_NUM, servers);
        const int32_t DATA_MEMBER_NUM  = GET_DATA_MEMBER_NUM(family_aid_info);
        const int32_t CHECK_MEMBER_NUM = GET_CHECK_MEMBER_NUM(family_aid_info);
        const int32_t MEMBER_NUM = DATA_MEMBER_NUM + CHECK_MEMBER_NUM;
        std::pair<uint64_t, int32_t> result[MAX_MARSHALLING_NUM];
        common::ArrayHelper<std::pair<uint64_t, int32_t> > helper(MAX_MARSHALLING_NUM, result);
        ret = get_members_(helper, family_id);
        if (TFS_SUCCESS == ret)
        {
          ret = helper.get_array_index() == MEMBER_NUM ? TFS_SUCCESS : EXIT_FAMILY_MEMBER_INFO_ERROR;
        }
        if (TFS_SUCCESS == ret)
        {
          for (index = 0; index < DATA_MEMBER_NUM && TFS_SUCCESS == ret; ++index)
          {
            target = NULL;
            helper2.clear();
            std::pair<uint64_t, int32_t>* item = helper.at(index);
            assert(NULL != item);
            uint64_t server = bm.get_master(item->first);
            if (INVALID_SERVER_ID != server)
            {
              helper2.push_back(server);
              ret = manager_.get_server_manager().choose_replicate_target_server(target, helper2);
              if (TFS_SUCCESS == ret)
                results.push_back(std::make_pair(target->id(), item->first));
            }
            else
            {
              results.push_back(std::make_pair(INVALID_SERVER_ID, item->first));
            }
          }
          for (; index < MEMBER_NUM && TFS_SUCCESS == ret; ++index)
          {
            std::pair<uint64_t, int32_t>* item = helper.at(index);
            results.push_back(std::make_pair(INVALID_SERVER_ID, item->first));
          }
        }
      }
      return ret;
    }

    bool FamilyManager::check_need_reinstate(ArrayHelper<FamilyMemberInfo>& reinstate_members,
          const FamilyCollect* family, const time_t now) const
    {
      reinstate_members.clear();
      BlockManager& bm = manager_.get_block_manager();
      TaskManager& tm  = manager_.get_task_manager();
      int32_t ret = (NULL != family && reinstate_members.get_array_size() > 0) ? TFS_SUCCESS : EXIT_PARAMETER_ERROR;
      if (TFS_SUCCESS == ret)
      {
        ret = tm.exist_family(family->get_family_id()) ? EXIT_FAMILY_EXISTED_IN_TASK_QUEUE_ERROR : TFS_SUCCESS;
      }
      if (TFS_SUCCESS == ret && family->check_need_reinstate(now))
      {
        std::pair<uint64_t, int32_t> result[MAX_MARSHALLING_NUM];
        common::ArrayHelper<std::pair<uint64_t, int32_t> > helper(MAX_MARSHALLING_NUM, result);
        BlockCollect* pblock = NULL;
        ret = get_members_(helper, family->get_family_id());
        if (TFS_SUCCESS == ret)
        {
          for (int64_t index = 0; index < helper.get_array_index(); ++index)
          {
            std::pair<uint64_t, int32_t>* item = helper.at(index);
            pblock = bm.get(item->first);
            if ((NULL != pblock) && (bm.need_reinstate(pblock, now)))
            {
              FamilyMemberInfo info;
              info.block_   = pblock->id();
              info.version_ = pblock->get_version();
              info.server_  = INVALID_SERVER_ID;
              reinstate_members.push_back(info);
            }
          }
        }
      }
      return (reinstate_members.get_array_index() > 0
              && reinstate_members.get_array_index() <= family->get_check_member_num());
    }

    bool FamilyManager::check_need_dissolve(const FamilyCollect* family, const common::ArrayHelper<FamilyMemberInfo>& need_reinstate_members, const time_t now) const
    {
      bool ret = (NULL != family);
      if (ret)
      {
        ret = !manager_.get_task_manager().exist_family(family->get_family_id());
        if (ret && (ret = family->check_need_dissolve(now)))
          ret = need_reinstate_members.get_array_index() > family->get_check_member_num();
      }
      return ret;
    }

    bool FamilyManager::check_need_compact(const FamilyCollect* family, const time_t now) const
    {
      TaskManager& tm  = manager_.get_task_manager();
      BlockManager& bm = manager_.get_block_manager();
      bool ret = (NULL != family);
      if (ret)
        ret = !tm.exist_family(family->get_family_id());
      if (ret)
        ret = CHECK_MEMBER_NUM_V2(family->get_data_member_num(),  family->get_check_member_num());
      if (ret)
      {
        const int32_t DATA_MEMBER_NUM = family->get_data_member_num();
        std::pair<uint64_t, int32_t> result[MAX_MARSHALLING_NUM];
        common::ArrayHelper<std::pair<uint64_t, int32_t> > helper(MAX_MARSHALLING_NUM, result);
        ret = (TFS_SUCCESS == get_members_(helper, family->get_family_id()));
        if (ret)
        {
          BlockCollect* pblock = NULL;
          int32_t need_compact_count = 0;
          for (int64_t index = 0; index < helper.get_array_index(); ++index)
          {
            std::pair<uint64_t, int32_t>* item = helper.at(index);
            assert(NULL != item);
            pblock = bm.get(item->first);
            if ((NULL != pblock) && !IS_VERFIFY_BLOCK(pblock->id()) && (bm.need_compact(pblock, now, false)))
              ++need_compact_count;
          }
          const int32_t ratio = static_cast<int32_t>(((static_cast<float>(need_compact_count) / static_cast<float>(DATA_MEMBER_NUM)) * 100));
          ret = (ratio >= SYSPARAM_NAMESERVER.compact_family_member_ratio_);
        }
      }
      return ret;
    }

    void FamilyManager::dump_marshalling_queue(const int32_t level, const char* format) const
    {
      if (level <= TBSYS_LOGGER._level)
      {
        RWLock::Lock lock(marshallin_queue_mutex_, READ_LOCKER);
        MARSHALLING_MAP_ITER iter = marshalling_queue_.begin();
        for (; iter != marshalling_queue_.end(); ++iter)
        {
          (*iter)->dump(level, format);
        }
      }
    }

    void FamilyManager::clear_marshalling_queue()
    {
      RWLock::Lock lock(marshallin_queue_mutex_, WRITE_LOCKER);
      MARSHALLING_MAP_ITER it = marshalling_queue_.begin();
      for (; it != marshalling_queue_.end(); ++it)
      {
        tbsys::gDelete((*it));
      }
      marshalling_queue_.clear();
    }

    void FamilyManager::set_task_expired_time(const int64_t family_id, const int64_t now, const int32_t step)
    {
      RWLock::Lock lock(get_mutex_(family_id), READ_LOCKER);
      FamilyCollect* family = get_(family_id);
      if (NULL != family)
        family->set(now, step);
    }

    FamilyCollect* FamilyManager::get_(const int64_t family_id) const
    {
      FamilyCollect* family = NULL;
      int32_t ret = (INVALID_FAMILY_ID != family_id) ? TFS_SUCCESS : EXIT_PARAMETER_ERROR;
      if (TFS_SUCCESS == ret)
      {
        int32_t index = get_chunk_(family_id);
        FamilyCollect query(family_id);
        FAMILY_MAP_ITER iter = families_[index]->find(&query);
        family = families_[index]->end() == iter ? NULL : (*iter);
      }
      return family;
    }

    int FamilyManager::insert_(const int64_t family_id, const int32_t family_aid_info,
          const common::ArrayHelper<std::pair<uint64_t, int32_t> >& members, const time_t now)
    {
      const int32_t chunk = get_chunk_(family_id);
      int32_t ret = (INVALID_FAMILY_ID != family_id && members.get_array_index() > 0) ?  TFS_SUCCESS : EXIT_PARAMETER_ERROR;
      if (TFS_SUCCESS == ret)
      {
        FamilyCollect* result = NULL;
        FamilyCollect* family = new (std::nothrow)FamilyCollect(family_id, family_aid_info, now);
        assert(NULL != family);
        ret = family->add(members);
        if (TFS_SUCCESS == ret)
        {
          ret = families_[chunk]->insert_unique(result, family);
          if (EXIT_ELEMENT_EXIST == ret)
            assert(result);
        }
        if (TFS_SUCCESS != ret)
          tbsys::gDelete(family);
      }
      if (TFS_SUCCESS == ret)
      {
        int64_t max_family_id = max_family_ids_[chunk];
        if (max_family_id < family_id)
          max_family_ids_[chunk] = family_id;
      }
      return ret;
    }

    int FamilyManager::del_family(const int64_t family_id)
    {
      int32_t family_aid_info = 0;
      std::pair<uint64_t, uint64_t> members[MAX_MARSHALLING_NUM];
      common::ArrayHelper<std::pair<uint64_t, uint64_t> > helper(MAX_MARSHALLING_NUM, members);
      int32_t ret = get_members(helper, family_aid_info, family_id);
      NsRuntimeGlobalInformation& ngi = GFactory::get_runtime_info();
      BlockManager& block_manager = manager_.get_block_manager();
      if (TFS_SUCCESS == ret)
      {
        const int64_t now = Func::get_monotonic_time();
        for (int32_t index = 0; index < helper.get_array_index(); ++index)
        {
          std::pair<uint64_t, uint64_t>* item = helper.at(index);
          assert(NULL != item);
          block_manager.set_family_id(item->first, item->second, INVALID_FAMILY_ID);
          if (IS_VERFIFY_BLOCK(item->first))
          {
            ServerItem si;
            si.family_id_ = family_id;
            si.server_ = item->second;
            si.version_ = -1;
            block_manager.push_to_delete_queue(item->first, si, ngi.is_master());
            BlockCollect* pblock = NULL;
            block_manager.remove(pblock, item->first);
            manager_.get_gc_manager().insert(pblock, now);
          }
        }

        FamilyCollect* family = NULL;
        remove(family, family_id);
        manager_.get_gc_manager().insert(family, now);
      }
      return ret;
    }

    int FamilyManager::get_members_(common::ArrayHelper<std::pair<uint64_t, int32_t> >& members, const int64_t family_id) const
    {
      int32_t ret = (INVALID_FAMILY_ID != family_id && members.get_array_size() > 0 ) ? TFS_SUCCESS : EXIT_PARAMETER_ERROR;
      if (TFS_SUCCESS == ret)
      {
        FamilyCollect* family = get_(family_id);
        ret = (NULL != family) ? TFS_SUCCESS : EXIT_NO_FAMILY;
        if (TFS_SUCCESS == ret)
        {
          family->get_members(members);
        }
      }
      return ret;
    }

    common::RWLock& FamilyManager::get_mutex_(const int64_t family_id) const
    {
      return mutexs_[get_chunk_(family_id)];
    }

    int32_t FamilyManager::get_chunk_(const int64_t family_id) const
    {
      return family_id % MAX_FAMILY_CHUNK_NUM;
    }
  }/** end namespace nameserver **/
}/** end namespace tfs **/
