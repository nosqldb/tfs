/*
 * (C) 2007-2010 Alibaba Group Holding Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 * Version: $Id: oplog_sync_manager.h 596 2011-07-21 10:03:24Z daoan@taobao.com $
 *
 * Authors:
 *   duolong <duolong@taobao.com>
 *      - initial release
 *   qushan<qushan@taobao.com>
 *      - modify 2009-03-27
 *   duanfei <duanfei@taobao.com>
 *      - modify 2010-04-23
 *
 */
#ifndef TFS_NAMESERVER_OPERATION_LOG_SYNC_MANAGER_H_
#define TFS_NAMESERVER_OPERATION_LOG_SYNC_MANAGER_H_

#include <deque>
#include <map>
#include <Mutex.h>
#include <Monitor.h>
#include <Timer.h>
#include "common/file_queue.h"
#include "common/file_queue_thread.h"
#include "message/message_factory.h"

#include "oplog.h"
#include "block_id_factory.h"
#include "tair_helper.h"

namespace tfs
{
  namespace nameserver
  {
    class LayoutManager;
    class OpLogSyncManager: public tbnet::IPacketQueueHandler
    {
      friend class FlushOpLogTimerTask;
    public:
      explicit OpLogSyncManager(LayoutManager& mm);
      virtual ~OpLogSyncManager();
      int initialize();
      int wait_for_shut_down();
      int destroy();
      int register_slots(const char* const data, const int64_t length) const;
      void switch_role();
      int log(const uint8_t type, const char* const data, const int64_t length, const time_t now);
      int push(common::BasePacket* msg, int32_t max_queue_size = 0, bool block = false);
      inline common::FileQueueThread* get_file_queue_thread() const { return file_queue_thread_;}
      int replay_helper(const char* const data, const int64_t data_len, int64_t& pos, const time_t now = common::Func::get_monotonic_time());
      int replay_helper_do_msg(const int32_t type, const char* const data, const int64_t data_len, int64_t& pos);
      int replay_helper_do_oplog(const time_t now, const int32_t type, const char* const data, const int64_t data_len, int64_t& pos);

      inline uint64_t generation(const bool verify) { return id_factory_.generation(verify);}
      inline int update(const uint64_t id) { return id_factory_.update(id);}
      uint64_t get_max_block_id() { return id_factory_.get(); }

      int create_family_id(int64_t& family_id);
      int create_family(common::FamilyInfo& family_info);
      int del_family(const int64_t family_id);

      // handle libeasy packet
      int handle(common::BasePacket* packet);

      int update_global_block_id(const uint64_t block_id);
      int query_global_block_id(uint64_t& block_id);

    private:
      DISALLOW_COPY_AND_ASSIGN( OpLogSyncManager);
      virtual bool handlePacketQueue(tbnet::Packet *packet, void *args);
      static int sync_log_func(const void* const data, const int64_t len, const int32_t threadIndex, void *arg);
      int send_log_(const char* const data, const int64_t length);
      int transfer_log_msg_(common::BasePacket* msg);
      int recv_log_(common::BasePacket* msg);
      int replay_all_();
      common::BasePacket* malloc_(const int32_t type);

      int scan_all_family_(const int32_t thseqno, const int32_t chunk, int64_t& start_family_id);
      int scan_all_family_log_();
      int load_family_info_(const int32_t thread_seqno);
      int load_family_log_(const int32_t thread_seqno);
      int load_all_family_info_(const int32_t thread_seqno, bool& load_complete);

      int sync_remote_block_id_();
      int save_remote_block_id_();

      class LoadFamilyInfoThreadHelper: public tbutil::Thread
      {
        public:
          LoadFamilyInfoThreadHelper(OpLogSyncManager& manager, const int32_t thread_seqno):
            manager_(manager), thread_seqno_(thread_seqno), load_complete_(false){start(THREAD_STATCK_SIZE);}
          void set_reload() { load_complete_ = false;}
          bool load_complete() const { return load_complete_;}
          virtual ~LoadFamilyInfoThreadHelper() {}
          void run();
        private:
          OpLogSyncManager& manager_;
          int32_t thread_seqno_;
          bool load_complete_;
          DISALLOW_COPY_AND_ASSIGN(LoadFamilyInfoThreadHelper);
      };
      typedef tbutil::Handle<LoadFamilyInfoThreadHelper> LoadFamilyInfoThreadHelperPtr;

      class SyncBlockIdThreadHelper: public tbutil::Thread
      {
        public:
          SyncBlockIdThreadHelper(OpLogSyncManager& manager):manager_(manager) {start(THREAD_STATCK_SIZE);}
          virtual ~SyncBlockIdThreadHelper() {}
          void run();
        private:
          OpLogSyncManager& manager_;
          DISALLOW_COPY_AND_ASSIGN(SyncBlockIdThreadHelper);
      };
      typedef tbutil::Handle<SyncBlockIdThreadHelper> SyncBlockIdThreadHelperPtr;

    private:
      static const int32_t DEFATUL_TAIR_INDEX = 0;
      LayoutManager& manager_;
      OpLog* oplog_;
      common::FileQueue* file_queue_;
      common::FileQueueThread* file_queue_thread_;
      BlockIdFactory id_factory_;
      tbutil::Mutex mutex_;
      TairHelper* dbhelper_[MAX_LOAD_FAMILY_INFO_THREAD_NUM];
      tbnet::PacketQueueThread work_thread_;
      LoadFamilyInfoThreadHelperPtr load_family_info_thread_[MAX_LOAD_FAMILY_INFO_THREAD_NUM];
      SyncBlockIdThreadHelperPtr sync_block_id_thread_;
    };
  }//end namespace nameserver
}//end namespace tfs
#endif
