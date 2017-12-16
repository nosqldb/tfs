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

#include "common/lock.h"
#include "global_factory.h"
#include "block_manager.h"
#include "server_collect.h"
#include "server_manager.h"
#include "layout_manager.h"

using namespace tfs::common;

namespace tfs
{
  namespace nameserver
  {
    static const int32_t MAX_BLOCK_CHUNK_SLOT_DEFALUT = 1024;
    static const int32_t MAX_BLOCK_CHUNK_SLOT_EXPAND_DEFAULT = 1024;
    static const float   MAX_BLOCK_CHUNK_SLOT_EXPAND_RATION_DEFAULT = 0.1;
    BlockManager::BlockManager(LayoutManager& manager):
      manager_(manager),
      last_traverse_block_(0)
    {
      for (int32_t i = 0; i < MAX_BLOCK_CHUNK_NUMS; i++)
      {
        blocks_[i] = new (std::nothrow)common::TfsSortedVector<BlockCollect*, BlockIdCompare>(MAX_BLOCK_CHUNK_SLOT_DEFALUT,
            MAX_BLOCK_CHUNK_SLOT_EXPAND_DEFAULT, MAX_BLOCK_CHUNK_SLOT_EXPAND_RATION_DEFAULT);
        assert(blocks_[i]);
      }
    }

    BlockManager::~BlockManager()
    {
      BlockCollect* block = NULL;
      for (int32_t i = 0; i < MAX_BLOCK_CHUNK_NUMS; i++)
      {
        for (int32_t j = 0; j < blocks_[i]->size(); j++)
        {
          block = blocks_[i]->at(j);
          tbsys::gDelete(block);
        }
        tbsys::gDelete(blocks_[i]);
      }
    }

    #ifdef TFS_GTEST
    void BlockManager::clear_()
    {
      for (int32_t i = 0; i < MAX_BLOCK_CHUNK_NUMS; i++)
      {
        blocks_[i]->clear();
      }
    }
    #endif

    BlockCollect* BlockManager::insert(const uint64_t block, const time_t now, const bool set)
    {
      RWLock::Lock lock(get_mutex_(block), WRITE_LOCKER);
      return insert_(block, now, set);
    }

    bool BlockManager::remove(BlockCollect*& object, const uint64_t block)
    {
      object = NULL;
      RWLock::Lock lock(get_mutex_(block), WRITE_LOCKER);
      object = remove_(block);
      return true;
    }

    BlockCollect* BlockManager::remove_(const uint64_t block)
    {
      BlockCollect query(block);
      return blocks_[get_chunk_(block)]->erase(&query);
    }

    BlockCollect* BlockManager::insert_(const uint64_t block_id, const time_t now, const bool set)
    {
      BlockCollect* block = new (std::nothrow)BlockCollect(block_id, set ? now : now + SYSPARAM_NAMESERVER.block_safe_mode_time_);
      assert(NULL != block);
      BlockCollect* result = NULL;
      int ret = blocks_[get_chunk_(block_id)]->insert_unique(result, block);
      if (TFS_SUCCESS != ret)
      {
        tbsys::gDelete(block);
        if (EXIT_ELEMENT_EXIST == ret)
        {
          assert(NULL != result);
          if (set)
            result->set_create_flag(BLOCK_CREATE_FLAG_YES);
        }
      }
      else
      {
        assert(NULL != result);
        if (set)
          result->set_create_flag(BLOCK_CREATE_FLAG_YES);
      }
      return result;
    }


    BlockCollect* BlockManager::get(const uint64_t block) const
    {
      RWLock::Lock lock(get_mutex_(block), READ_LOCKER);
      return get_(block);
    }

    bool BlockManager::push_to_delete_queue(const uint64_t block, const ServerItem& item, const bool master)
    {
      if (master)
      {
        tbutil::Mutex::Lock lock(delete_queue_mutex_);
        delete_queue_.push_back(std::make_pair(block, item));
      }
      return true;
    }

    bool BlockManager::pop_from_delete_queue_(std::pair<uint64_t,ServerItem>& output, const bool master)
    {
      bool ret = false;
      if (master)
      {
        tbutil::Mutex::Lock lock(delete_queue_mutex_);
        ret = !delete_queue_.empty();
        if (ret)
        {
          output = delete_queue_.front();
          delete_queue_.pop_front();
        }
      }
      return ret;
    }

    bool BlockManager::pop_from_delete_queue(std::pair<uint64_t,ServerItem>& output, const bool master)
    {
      bool ret = false;
      BlockCollect* block = NULL;
      ServerCollect* server = NULL;
      int32_t loop = 0;
      const int32_t MAX_LOOP_NUM = delete_queue_.size();
      const int8_t MIN_REPLICATE = SYSPARAM_NAMESERVER.max_replication_;
      while (loop++ < MAX_LOOP_NUM && !ret && pop_from_delete_queue_(output, master))
      {
        block = get(output.first);
        server = manager_.get_server_manager().get(output.second.server_);
        ret = (NULL != block) && (NULL != server) && !block->exist(server->id());
        if (ret)
        {
          get_mutex_(block->id()).rdlock();
          int8_t size = block->get_servers_size();
          bool in_family = block->is_in_family();
          ret = in_family ? size > 0 : size >= MIN_REPLICATE;
          get_mutex_(block->id()).unlock();
          if (!ret && size > 0)
            push_to_delete_queue(output.first, output.second, master);// delete after replicating finished
        }
      }
      return ret;
    }

    bool BlockManager::delete_queue_empty() const
    {
      return delete_queue_.empty();
    }

    void BlockManager::clear_delete_queue()
    {
      tbutil::Mutex::Lock lock(delete_queue_mutex_);
      delete_queue_.clear();
    }

    bool BlockManager::push_to_emergency_replicate_queue(BlockCollect* block)
    {
      bool ret = ((NULL != block) && (!block->in_replicate_queue()));
      if (ret)
      {
        TBSYS_LOG(DEBUG, "block %"PRI64_PREFIX"u maybe lack of backup, we'll replicate", block->id());
        block->set_in_replicate_queue(BLOCK_IN_REPLICATE_QUEUE_YES);
        tbutil::Mutex::Lock lock(emergency_replicate_queue_mutex_);
        emergency_replicate_queue_.push_back(block->id());
      }
      return ret;
    }

    BlockCollect* BlockManager::pop_from_emergency_replicate_queue()
    {
      BlockCollect* block = NULL;
      if (!emergency_replicate_queue_.empty())
      {
        emergency_replicate_queue_mutex_.lock();
        uint64_t id = emergency_replicate_queue_.front();
        emergency_replicate_queue_.pop_front();
        emergency_replicate_queue_mutex_.unlock();
        block = get(id);
        if (NULL == block)
          TBSYS_LOG(INFO, "block: %"PRI64_PREFIX"u maybe lost,don't replicate", id);
        else
          block->set_in_replicate_queue(BLOCK_IN_REPLICATE_QUEUE_NO);
      }
      return block;
    }

    bool BlockManager::has_emergency_replicate_in_queue() const
    {
      return !emergency_replicate_queue_.empty();
    }

    int64_t BlockManager::get_emergency_replicate_queue_size() const
    {
      return emergency_replicate_queue_.size();
    }

    void BlockManager::clear_emergency_replicate_queue()
    {
      tbutil::Mutex::Lock lock(emergency_replicate_queue_mutex_);
      emergency_replicate_queue_.clear();
    }

    void BlockManager::push_to_clean_familyinfo_queue(const uint64_t block, const ServerItem& item, const bool master)
    {
      if (master)
      {
        tbutil::Mutex::Lock lock(clean_familyinfo_queue_mutex_);
        clean_familyinfo_queue_.push_back(std::make_pair(block, item));
      }
    }

    void BlockManager::pop_from_clean_familyinfo_queue(std::pair<uint64_t, ServerItem>& output, const bool master)
    {
      if (master)
      {
        tbutil::Mutex::Lock lock(clean_familyinfo_queue_mutex_);
        output = clean_familyinfo_queue_.front();
        clean_familyinfo_queue_.pop_front();
      }
    }

    bool BlockManager::clean_familyinfo_queue_empty() const
    {
      return clean_familyinfo_queue_.empty();
    }

    void BlockManager::clear_familyinfo_queue()
    {
      tbutil::Mutex::Lock lock(clean_familyinfo_queue_mutex_);
      clean_familyinfo_queue_.clear();
    }

    bool BlockManager::scan(common::ArrayHelper<BlockCollect*>& result, uint64_t& begin, const int32_t count) const
    {
      bool end  = false;
      int32_t actual = 0;
      int32_t next = get_chunk_(begin);
      BlockCollect query(begin);
      bool all_over = next >= MAX_BLOCK_CHUNK_NUMS;
      for (; next < MAX_BLOCK_CHUNK_NUMS && actual < count;)
      {
        rwmutex_[next].rdlock();
        BLOCK_MAP_ITER iter = ((0 == begin) || end) ? blocks_[next]->begin() : blocks_[next]->lower_bound(&query);
        for (; iter != blocks_[next]->end(); ++iter)
        {
          result.push_back(*iter);
          if (++actual >= count)
          {
            ++iter;
            break;
          }
        }
        end = (blocks_[next]->end() == iter);
        if (!end)
          begin = (*iter)->id();
        rwmutex_[next].unlock();
        if (end)
        {
          ++next;
          begin = 0;
          while ((0 == begin) && (next < MAX_BLOCK_CHUNK_NUMS))
          {
            rwmutex_[next].rdlock();
            if (!blocks_[next]->empty())
            {
              begin = (*blocks_[next]->begin())->id();
              rwmutex_[next].unlock();
            }
            else
            {
              rwmutex_[next].unlock();
              ++next;
            }
          }
        }
      }
      all_over = (next == MAX_BLOCK_CHUNK_NUMS);
      return all_over;
    }

    int BlockManager::scan(common::SSMScanParameter& param, int32_t& next, bool& all_over,
        bool& cutover, const int32_t should) const
    {
      int32_t actual = 0;
      BLOCK_MAP_ITER iter;
      all_over = next >= MAX_BLOCK_CHUNK_NUMS;
      while (next < MAX_BLOCK_CHUNK_NUMS && actual < should)
      {
        RWLock::Lock lock(rwmutex_[next], READ_LOCKER);
        BlockCollect query(param.addition_param1_);
        iter = cutover ?  blocks_[next]->begin() : blocks_[next]->lower_bound(&query);
        if (cutover && iter != blocks_[next]->end())
          param.addition_param1_ = (*iter)->id();
        for(;blocks_[next]->end() != iter; ++iter)
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
        cutover = blocks_[next]->end() == iter;
        if (!cutover)
          param.addition_param2_ = (*iter)->id();
        else
          ++next;
      }
      all_over = (next >= MAX_BLOCK_CHUNK_NUMS) ? true : (MAX_BLOCK_CHUNK_NUMS - 1 == next) ? cutover : false;
      return actual;
    }

    bool BlockManager::exist(const uint64_t block) const
    {
      RWLock::Lock lock(get_mutex_(block), READ_LOCKER);
      BlockCollect* pblock = get_(block);
      return (NULL != pblock && pblock->id() == block);
    }

    int BlockManager::get_servers(ArrayHelper<uint64_t>& servers, const BlockCollect* block) const
    {
      int32_t ret = (NULL == block) ? EXIT_NO_BLOCK : TFS_SUCCESS;
      if (TFS_SUCCESS == ret)
      {
        RWLock::Lock lock(get_mutex_(block->id()), READ_LOCKER);
        ret = get_servers_(servers, block);
      }
      return ret;
    }

    int BlockManager::get_servers(ArrayHelper<uint64_t>& servers, const uint64_t block) const
    {
      RWLock::Lock lock(get_mutex_(block), READ_LOCKER);
      BlockCollect* pblock = get_(block);
      return get_servers_(servers, pblock);
    }

    int BlockManager::get_servers_size(const uint64_t block) const
    {
      RWLock::Lock lock(get_mutex_(block), READ_LOCKER);
      BlockCollect* pblock = get_(block);
      return (NULL != pblock) ? pblock->get_servers_size() : 0;
    }

    int BlockManager::get_servers_size(const BlockCollect* const pblock) const
    {
      int32_t size = 0;
      if (NULL != pblock)
      {
        RWLock::Lock lock(get_mutex_(pblock->id()), READ_LOCKER);
        size = pblock->get_servers_size();
      }
      return size;
    }

    uint64_t BlockManager::get_master(const uint64_t block) const
    {
      RWLock::Lock lock(get_mutex_(block), READ_LOCKER);
      BlockCollect* pblock = get_(block);
      return (NULL != pblock) ? pblock->get_master() : INVALID_SERVER_ID;
    }

    bool BlockManager::exist(const BlockCollect* block, const ServerCollect* server) const
    {
      bool ret = (NULL != block && NULL != server);
      if (ret)
      {
        RWLock::Lock lock(get_mutex_(block->id()), READ_LOCKER);
        ret = block->exist(server->id());
      }
      return ret;
    }

    RWLock& BlockManager::get_mutex_(const uint64_t block) const
    {
      return rwmutex_[get_chunk_(block)];
    }

    int BlockManager::update_relation(std::vector<uint64_t>& expires, ServerCollect* server,
        const common::ArrayHelper<common::BlockInfoV2>& blocks, const time_t now)
    {
      int32_t ret = (NULL != server) ? TFS_SUCCESS : EXIT_PARAMETER_ERROR;
      if (TFS_SUCCESS == ret)
      {
        uint64_t left_less_array[MAX_SINGLE_DISK_BLOCK_COUNT];
        BlockInfoV2* right_less_array[MAX_SINGLE_DISK_BLOCK_COUNT];
        BlockInfoV2* same_array[MAX_SINGLE_DISK_BLOCK_COUNT];
        ArrayHelper<uint64_t> left_less_helper(MAX_SINGLE_DISK_BLOCK_COUNT, left_less_array);
        ArrayHelper<BlockInfoV2*> right_less_helper(MAX_SINGLE_DISK_BLOCK_COUNT, right_less_array);
        ArrayHelper<BlockInfoV2*> same_helper(MAX_SINGLE_DISK_BLOCK_COUNT, same_array);

        server->diff(blocks, left_less_helper, right_less_helper, same_helper);
        TBSYS_LOG(INFO, "server : %s update relation, input: %"PRI64_PREFIX"d, same: %"PRI64_PREFIX"d, left: %"PRI64_PREFIX"d, right: %"PRI64_PREFIX"d",
            tbsys::CNetUtil::addrToString(server->id()).c_str(), blocks.get_array_index(), same_helper.get_array_index(),
            left_less_helper.get_array_index(), right_less_helper.get_array_index());

        ret = update_relation_(same_helper, now, expires, server);
        if (TFS_SUCCESS == ret)
        {
          ret = update_relation_(right_less_helper, now, expires, server);
        }
        if (TFS_SUCCESS == ret)
        {
          for (int64_t index = 0; index < left_less_helper.get_array_index(); ++index)
          {
            uint64_t block_id = *left_less_helper.at(index);
            BlockCollect* pblock = this->get(block_id);
            if (NULL != pblock && 0 != pblock->get_last_leave_time())
              manager_.relieve_relation(pblock, server, now, true);
          }
        }
      }
      return ret;
    }

    int BlockManager::update_relation_(const common::ArrayHelper<common::BlockInfoV2*>& blocks, const time_t now,
        std::vector<uint64_t>& cleanup_family_id_array, ServerCollect* server)
    {
      int32_t ret = (NULL != server && server->is_alive()) ? TFS_SUCCESS : EXIT_PARAMETER_ERROR;
      if (TFS_SUCCESS == ret)
      {
        cleanup_family_id_array.clear();
        for (int64_t i= 0; i < blocks.get_array_index(); ++i)
        {
          bool writable = false, master = false, isnew= false;
          BlockInfoV2* info = *blocks.at(i);
          uint64_t block_id = info->block_id_;

          if (INVALID_BLOCK_ID == info->block_id_)
            continue;

          get_mutex_(block_id).wrlock();
          BlockCollect* block = get_(block_id);
          isnew = (NULL == block);
          if (isnew)
            block = insert_(block_id, now, false);
          ret = NULL != block ? TFS_SUCCESS : EXIT_BLOCK_NOT_FOUND;
          if (TFS_SUCCESS == ret)
          {
            ret = block->has_valid_lease(now) ? EXIT_CANNOT_ACCEPT_THIS_COPIES_ERROR : TFS_SUCCESS;
          }
          if (TFS_SUCCESS == ret)
          {
            ret = build_relation_(block, writable, master, server->id(), info, info->family_id_, now, false);
          }
          get_mutex_(block_id).unlock();

          if (TFS_SUCCESS == ret)
          {
            manager_.get_server_manager().build_relation(server, block->id(), writable, master);
          }

          if (TFS_SUCCESS != ret)
            ret = TFS_SUCCESS;
        }
      }
      return ret;
    }

    int BlockManager::build_relation(BlockCollect* block, bool& writable, bool& master,
        const uint64_t server, const BlockInfoV2* info, const int64_t family_id, const int64_t now, const bool set)
    {
      int32_t ret = ((NULL != block) && (INVALID_SERVER_ID != server)) ? TFS_SUCCESS : EXIT_PARAMETER_ERROR;
      if (TFS_SUCCESS == ret)
      {
        RWLock::Lock lock(get_mutex_(block->id()), WRITE_LOCKER);
        ret = build_relation_(block, writable, master, server, info, family_id, now, set);
      }
      return ret;
    }

    int BlockManager::build_relation_(BlockCollect* block, bool& writable, bool& master,
        const uint64_t server, const BlockInfoV2* info, const int64_t family_id, const int64_t now, const bool set)
    {
      int32_t ret = ((NULL != block) && (INVALID_SERVER_ID != server)) ? TFS_SUCCESS : EXIT_PARAMETER_ERROR;
      if (TFS_SUCCESS == ret)
      {
        ret = block->add(writable, master, server, info, family_id, now);
        if (EXIT_SERVER_EXISTED == ret)
          ret = TFS_SUCCESS;
        if (set && TFS_SUCCESS == ret)
          block->set_create_flag(BLOCK_CREATE_FLAG_NO);
      }
      return ret;
    }

    BlockCollect* BlockManager::get_(const uint64_t block) const
    {
      int32_t index = get_chunk_(block);
      BlockCollect query(block);
      BLOCK_MAP_ITER iter = blocks_[index]->find(&query);
      return blocks_[index]->end() == iter ? NULL : (*iter);
    }

    int BlockManager::update_block_info(const common::BlockInfoV2& info, BlockCollect* block)
    {
      int32_t ret = (NULL != block) ? TFS_SUCCESS : EXIT_PARAMETER_ERROR;
      if (TFS_SUCCESS == ret)
      {
        RWLock::Lock lock(get_mutex_(block->id()), WRITE_LOCKER);
        int32_t diff =  info.version_ - block->get_version();
        if (diff > 0)
        {
          block->set_version(info.version_);
          block->update_info(info);
          block->update_all_version(diff);
        }
      }
      return ret;
    }

    int BlockManager::set_family_id(const uint64_t block, const uint64_t server, const uint64_t family_id)
    {
      RWLock::Lock lock(get_mutex_(block), WRITE_LOCKER);
      BlockCollect* pblock = get_(block);
      int32_t ret = (NULL != pblock) ? TFS_SUCCESS : EXIT_BLOCK_NOT_FOUND;
      if (TFS_SUCCESS == ret)
      {
        pblock->set_family_id(family_id);
        if (INVALID_SERVER_ID != server)
          pblock->update_family_id(server, family_id);
      }
      return ret;
    }

    int BlockManager::relieve_relation(BlockCollect* block, const uint64_t server, const time_t now)
    {
      RWLock::Lock lock(get_mutex_(block->id()), WRITE_LOCKER);
      return ((NULL != block) && (INVALID_SERVER_ID != server)) ? block->remove(server, now) : EXIT_PARAMETER_ERROR;
    }

    bool BlockManager::need_replicate(const BlockCollect* block) const
    {
      RWLock::Lock lock(get_mutex_(block->id()), READ_LOCKER);
      return (NULL != block) ? (block->get_servers_size() < SYSPARAM_NAMESERVER.max_replication_ && !block->is_in_family()) : false;
    }

    bool BlockManager::need_replicate(const BlockCollect* block, const time_t now) const
    {
      RWLock::Lock lock(get_mutex_(block->id()), READ_LOCKER);
      return (NULL != block) ? (block->check_replicate(now) >= PLAN_PRIORITY_NORMAL) : false;
    }

    bool BlockManager::need_replicate(ArrayHelper<uint64_t>& servers, PlanPriority& priority,
        const BlockCollect* block, const time_t now) const
    {
      bool ret = NULL != block;
      if (ret)
      {
        get_mutex_(block->id()).rdlock();
        priority = block->check_replicate(now);
        ret = (priority >= PLAN_PRIORITY_NORMAL);
        if (ret)
          block->get_servers(servers);
        get_mutex_(block->id()).unlock();
        ret = (ret && !manager_.get_task_manager().exist_block(block->id()));
      }
      return ret;
    }

    bool BlockManager::need_compact(const BlockCollect* block, const time_t now, const bool check_in_family) const
    {
      RWLock::Lock lock(get_mutex_(block->id()), READ_LOCKER);
      return (NULL != block) ? (block->check_compact(now, check_in_family)): false;
    }

    bool BlockManager::need_compact(ArrayHelper<uint64_t>& servers, const BlockCollect* block, const time_t now, const bool check_in_family) const
    {
      bool ret = (NULL != block);
      if (ret)
      {
        get_mutex_(block->id()).rdlock();
        ret = ((block->check_compact(now,check_in_family)));
        if (ret)
          block->get_servers(servers);
        get_mutex_(block->id()).unlock();
        ret = (ret && !manager_.get_task_manager().exist_block(block->id())
            && !manager_.get_task_manager().exist(servers));
      }
      return ret;
    }

    bool BlockManager::need_balance(common::ArrayHelper<uint64_t>& servers, const BlockCollect* block, const time_t now) const
    {
      bool ret = (NULL != block);
      if (ret)
      {
        get_mutex_(block->id()).rdlock();
        ret = ((block->check_balance(now)));
        if (ret)
          block->get_servers(servers);
        get_mutex_(block->id()).unlock();
        ret = ret && !manager_.get_task_manager().exist_block(block->id());
      }
      return ret;
    }

    bool BlockManager::need_marshalling(const uint64_t block, const time_t now)
    {
      RWLock::Lock lock(get_mutex_(block), READ_LOCKER);
      BlockCollect* pblock = get_(block);
      return  (NULL != pblock) ? (pblock->check_marshalling(now)) : false;
    }

    bool BlockManager::need_marshalling(const BlockCollect* block, const time_t now)
    {
      RWLock::Lock lock(get_mutex_(block->id()), READ_LOCKER);
      return  (NULL != block) ? (block->check_marshalling(now)) : false;
    }

    bool BlockManager::need_marshalling(common::ArrayHelper<uint64_t>& servers, const BlockCollect* block, const time_t now) const
    {
      bool ret = (NULL != block);
      if (ret)
      {
        get_mutex_(block->id()).rdlock();
        ret = (block->check_marshalling(now));
        if (ret)
          block->get_servers(servers);
        get_mutex_(block->id()).unlock();
        ret = ret && !manager_.get_task_manager().exist_block(block->id());
      }
      return ret;
    }

    bool BlockManager::need_reinstate(const BlockCollect* block, const time_t now) const
    {
      bool ret = (NULL != block && block->is_in_family());
      if (ret)
      {
        RWLock::Lock lock(get_mutex_(block->id()), READ_LOCKER);
        ret = block->check_reinstate(now);
      }
      return ret;
    }

    bool BlockManager::resolve_invalid_copies(common::ArrayHelper<ServerItem>& invalids,
      common::ArrayHelper<ServerItem>& clean_familyinfo, BlockCollect* block, const time_t now)
    {
      bool ret = (NULL != block );
      if (ret)
      {
        RWLock::Lock lock(get_mutex_(block->id()), WRITE_LOCKER);
        ret = block->resolve_invalid_copies(invalids, clean_familyinfo, now);
      }
      return ret;
    }

    int BlockManager::expand_ratio(int32_t& index, const float expand_ratio)
    {
      if (++index >= MAX_BLOCK_CHUNK_NUMS)
        index = 0;
      RWLock::Lock lock(rwmutex_[index], WRITE_LOCKER);
      if (blocks_[index]->need_expand(expand_ratio))
        blocks_[index]->expand_ratio(expand_ratio);
      return TFS_SUCCESS;
    }

    bool BlockManager::has_valid_lease(const BlockCollect* pblock, const time_t now) const
    {
      bool ret = (NULL != pblock);
      if (ret)
      {
        RWLock::Lock lock(get_mutex_(pblock->id()), READ_LOCKER);
        ret = pblock->has_valid_lease(now);
      }
      return ret;
    }

    bool BlockManager::has_valid_lease(const uint64_t block, const time_t now) const
    {
      bool ret = (INVALID_BLOCK_ID != block);
      if (ret)
      {
        BlockCollect* pblock = get(block);
        ret =  has_valid_lease(pblock, now);
      }
      return ret;
    }

    int BlockManager::apply_lease(const uint64_t server, const time_t now, const int32_t step, const bool update,
          const uint64_t block, common::ArrayHelper<ServerItem>& helper, common::ArrayHelper<ServerItem>& clean_familyinfo)
    {
      int32_t ret = (INVALID_BLOCK_ID != block && INVALID_SERVER_ID != server) ? TFS_SUCCESS : EXIT_PARAMETER_ERROR;
      if (TFS_SUCCESS == ret)
      {
        RWLock::Lock lock(get_mutex_(block), WRITE_LOCKER);
        BlockCollect* pblock = get_(block);
        ret = (NULL != pblock) ? TFS_SUCCESS : EXIT_BLOCK_NOT_FOUND;
        if (TFS_SUCCESS == ret)
        {
          ret = pblock->apply_lease(server, now, step, update, helper, clean_familyinfo);
        }
      }
      return ret;
    }

    int BlockManager::apply_lease(const uint64_t server, const time_t now, const int32_t step, const bool update,
          BlockCollect* pblock, common::ArrayHelper<ServerItem>& helper, common::ArrayHelper<ServerItem>& clean_familyinfo)
    {
      int32_t ret = (NULL != pblock && INVALID_SERVER_ID != server) ? TFS_SUCCESS : EXIT_PARAMETER_ERROR;
      if (TFS_SUCCESS == ret)
      {
        RWLock::Lock lock(get_mutex_(pblock->id()), WRITE_LOCKER);
        ret = pblock->apply_lease(server, now, step, update, helper, clean_familyinfo);
      }
      return ret;
    }

    int BlockManager::renew_lease(const uint64_t server, const time_t now, const int32_t step, const bool update,
          const common::BlockInfoV2& info, const uint64_t block, common::ArrayHelper<ServerItem>& helper,
          common::ArrayHelper<ServerItem>& clean_familyinfo)
    {
      int32_t ret = (INVALID_BLOCK_ID != block && INVALID_SERVER_ID != server) ? TFS_SUCCESS : EXIT_PARAMETER_ERROR;
      if (TFS_SUCCESS == ret)
      {
        RWLock::Lock lock(get_mutex_(block), WRITE_LOCKER);
        BlockCollect* pblock = get_(block);
        ret = (NULL != pblock) ? TFS_SUCCESS : EXIT_BLOCK_NOT_FOUND;
        if (TFS_SUCCESS == ret)
        {
          ret = pblock->renew_lease(server, now, step, update, info, helper, clean_familyinfo);
        }
      }
      return ret;
    }

    int BlockManager::renew_lease(const uint64_t server, const time_t now, const int32_t step, const bool update,
          const common::BlockInfoV2& info, BlockCollect* pblock, common::ArrayHelper<ServerItem>& helper,
         common::ArrayHelper<ServerItem>& clean_familyinfo )
    {
      int32_t ret = (NULL != pblock && INVALID_SERVER_ID != server) ? TFS_SUCCESS : EXIT_PARAMETER_ERROR;
      if (TFS_SUCCESS == ret)
      {
        RWLock::Lock lock(get_mutex_(pblock->id()), WRITE_LOCKER);
        ret = pblock->renew_lease(server, now, step, update, info, helper, clean_familyinfo);
      }
      return ret;
    }

    int BlockManager::giveup_lease(const uint64_t server, const time_t now, const common::BlockInfoV2* info, const uint64_t block)
    {
      int32_t ret = (INVALID_BLOCK_ID != block && INVALID_SERVER_ID != server) ? TFS_SUCCESS : EXIT_PARAMETER_ERROR;
      if (TFS_SUCCESS == ret)
      {
        RWLock::Lock lock(get_mutex_(block), WRITE_LOCKER);
        BlockCollect* pblock = get_(block);
        ret = (NULL != pblock) ? TFS_SUCCESS : EXIT_BLOCK_NOT_FOUND;
        if (TFS_SUCCESS == ret)
        {
          ret = pblock->giveup_lease(server, now, info);
        }
      }
      return ret;
    }

    int BlockManager::giveup_lease(const uint64_t server, const time_t now, const common::BlockInfoV2* info, BlockCollect* pblock)
    {
      int32_t ret = (NULL!= pblock && INVALID_SERVER_ID != server) ? TFS_SUCCESS : EXIT_PARAMETER_ERROR;
      if (TFS_SUCCESS == ret)
      {
        RWLock::Lock lock(get_mutex_(pblock->id()), WRITE_LOCKER);
        ret = pblock->giveup_lease(server, now, info);
      }
      return ret;
    }

    void BlockManager::timeout(const time_t now)
    {
      BlockCollect* pblock = NULL;
      const int32_t MAX_QUERY_BLOCK_NUMS = 40960;
      BlockCollect* blocks[MAX_QUERY_BLOCK_NUMS];
      ArrayHelper<BlockCollect*> results(MAX_QUERY_BLOCK_NUMS, blocks);
      bool over = scan(results, last_traverse_block_, MAX_QUERY_BLOCK_NUMS);
      for (int64_t index = 0; index < results.get_array_index(); ++index)
      {
        pblock = *results.at(index);
        assert(NULL != pblock);
        last_traverse_block_ = pblock->id();
        if (pblock->has_lease() && !pblock->has_valid_lease(now))
        {
          manager_.get_server_manager().giveup_block(pblock->get_master(), last_traverse_block_);
        }
      }
      if (over)
        last_traverse_block_ = 0;
    }


    void BlockManager::set_task_expired_time(const uint64_t block, const int64_t now, const int32_t step)
    {
      RWLock::Lock lock(get_mutex_(block), READ_LOCKER);
      BlockCollect* pblock = get_(block);
      if (NULL != pblock)
        pblock->set_task_expired_time(now, step);
    }

    void BlockManager::update_version(common::ArrayHelper<uint64_t>& helper, const uint64_t block, const int32_t version, const int32_t step, const BlockInfoV2& info)
    {
      RWLock::Lock lock(get_mutex_(block), WRITE_LOCKER);
      BlockCollect* pblock = get_(block);
      if (NULL != pblock)
      {
        pblock->set_version(version);
        pblock->update_info(info);
        pblock->update_version(helper, step);
      }
    }

    int32_t BlockManager::get_chunk_(const uint64_t block) const
    {
      return  block % MAX_BLOCK_CHUNK_NUMS;
    }

    int BlockManager::get_servers_(ArrayHelper<uint64_t>& servers, const BlockCollect* block) const
    {
      int32_t ret = (NULL != block && servers.get_array_size() > 0)? TFS_SUCCESS : EXIT_NO_BLOCK;
      if (TFS_SUCCESS == ret)
      {
        servers.clear();
        block->get_servers(servers);
        ret = servers.empty() ? EXIT_NO_DATASERVER : TFS_SUCCESS;
      }
      return ret;
    }
  }/** nameserver **/
}/** tfs **/
