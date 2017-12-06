/*
 * (C) 2007-2010 Alibaba Group Holding Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 * Version: $Id: parameter.cpp 1000 2011-11-03 02:40:09Z mingyan.zc@taobao.com $
 *
 * Authors:
 *   duolong <duolong@taobao.com>
 *      - initial release
 *
 */
#include "parameter.h"

#include <tbsys.h>
#include "config.h"
#include "config_item.h"
#include "error_msg.h"
#include "func.h"
#include "internal.h"
#include "rts_define.h"
#include "kv_rts_define.h"
namespace
{
  const int PORT_PER_PROCESS = 2;
}
namespace tfs
{
  namespace common
  {
    NameServerParameter NameServerParameter::ns_parameter_;
    FileSystemParameter FileSystemParameter::fs_parameter_;
    DataServerParameter DataServerParameter::ds_parameter_;
    RcServerParameter RcServerParameter::rc_parameter_;
    NameMetaServerParameter NameMetaServerParameter::meta_parameter_;
    RtServerParameter RtServerParameter::rt_parameter_;
    CheckServerParameter CheckServerParameter::cs_parameter_;
    MigrateServerParameter MigrateServerParameter::ms_parameter_;
    KvMetaParameter KvMetaParameter::kv_meta_parameter_;
    KvRtServerParameter KvRtServerParameter::kv_rt_parameter_;
    ExpireServerParameter ExpireServerParameter::expire_server_parameter_;
    ExpireRootServerParameter ExpireRootServerParameter::expire_root_server_parameter_;

    static void set_hour_range(const char *str, int32_t& min, int32_t& max)
    {
      if (NULL != str)
      {
        char *p1, *p2, buffer[64];
        p1 = buffer;
        strncpy(buffer, str, 63);
        p2 = strsep(&p1, "-~ ");
        if (NULL  != p2 && p2[0] != '\0')
          min = atoi(p2);
        if (NULL != p1 && p1[0] != '\0')
          max = atoi(p1);
      }
    }


    int NameServerParameter::initialize(void)
    {
      discard_max_count_ = 0;
      move_task_expired_time_ = 120;
      compact_task_expired_time_ = 120;
      marshalling_task_expired_time_ = 360;
      reinstate_task_expired_time_ = 240;
      dissolve_task_expired_time_  = 120;
      max_mr_network_bandwith_ratio_ = 60;
      max_rw_network_bandwith_ratio_ = 40;
      compact_family_member_ratio_   = 60;
      resolve_version_conflic_task_expired_time_ = 30;
      max_single_machine_network_bandwith_ = 240;//120MB
      adjust_copies_location_time_lower_   = 6;
      adjust_copies_location_time_upper_   = 12;
      between_ns_and_ds_lease_expire_time_ = 60;//60s
      between_ns_and_ds_lease_safe_time_   = 2;//2s
      between_ns_and_ds_lease_retry_times_ = 3;
      between_ns_and_ds_lease_retry_expire_time_  = 2;//2s
      max_marshalling_num_ = 10;
      marshalling_visit_time_ = 7;
      global_switch_ = ENABLE_VERSION_CHECK | ENABLE_READ_STATSTICS;
      global_switch_ = TBSYS_CONFIG.getInt(CONF_SN_NAMESERVER, CONF_GLOBAL_SWITCH, global_switch_);
      client_keepalive_interval_ = 600;
      verify_index_reserved_space_ratio_ = VERIFY_INDEX_RESERVED_SPACKE_DEFAULT_RATIO;
      check_integrity_interval_days_ = CHECK_INTEGRITY_INTERVAL_DAYS_DEFAULT;
      write_file_check_copies_complete_ = WRITE_FILE_CHECK_COPIES_COMPLETE_FLAG_NO;
      report_block_time_interval_ = TBSYS_CONFIG.getInt(CONF_SN_NAMESERVER, CONF_REPORT_BLOCK_TIME_INTERVAL, 1);
      report_block_time_interval_min_ = TBSYS_CONFIG.getInt(CONF_SN_NAMESERVER, CONF_REPORT_BLOCK_TIME_INTERVAL_MIN, 0);
      max_write_timeout_= TBSYS_CONFIG.getInt(CONF_SN_NAMESERVER, CONF_MAX_WRITE_TIMEOUT, 3);
      max_task_in_machine_nums_ = TBSYS_CONFIG.getInt(CONF_SN_NAMESERVER, CONF_MAX_TASK_IN_MACHINE_NUMS, 14);
      cleanup_write_timeout_threshold_ =
        TBSYS_CONFIG.getInt(CONF_SN_NAMESERVER, CONF_CLEANUP_WRITE_TIMEOUT_THRESHOLD, 40960);
      const char* index = TBSYS_CONFIG.getString(CONF_SN_NAMESERVER, CONF_CLUSTER_ID);
      if (index == NULL
          || strlen(index) < 1
          || !isdigit(index[0]))
      {
        TBSYS_LOG(ERROR, "%s in [%s] is invalid", CONF_CLUSTER_ID, CONF_SN_NAMESERVER);
        return EXIT_SYSTEM_PARAMETER_ERROR;
      }
      cluster_index_ = index[0];

      int32_t block_use_ratio = TBSYS_CONFIG.getInt(CONF_SN_NAMESERVER, CONF_BLOCK_USE_RATIO, 95);
      if (block_use_ratio <= 0)
        block_use_ratio = 95;
      block_use_ratio = std::min(100, block_use_ratio);
      int32_t max_block_size = TBSYS_CONFIG.getInt(CONF_SN_NAMESERVER, CONF_BLOCK_MAX_SIZE);
      if (max_block_size <= 0)
      {
        TBSYS_LOG(ERROR, "%s in [%s] is invalid", CONF_BLOCK_MAX_SIZE, CONF_SN_NAMESERVER);
        return EXIT_SYSTEM_PARAMETER_ERROR;
      }
      // roundup to 1M
      int32_t writeBlockSize = (int32_t)(((double) max_block_size * block_use_ratio) / 100);
      max_block_size_ = (writeBlockSize & 0xFFF00000) + 1024 * 1024;
      max_block_size_ = std::min(max_block_size_, max_block_size);

      max_replication_ = TBSYS_CONFIG.getInt(CONF_SN_NAMESERVER, CONF_MAX_REPLICATION, 2);

      replicate_ratio_ = TBSYS_CONFIG.getInt(CONF_SN_NAMESERVER, CONF_REPLICATE_RATIO, 50);
      if (replicate_ratio_ <= 0)
        replicate_ratio_ = 50;
      replicate_ratio_ = std::min(replicate_ratio_, 100);

      max_write_file_count_ = TBSYS_CONFIG.getInt(CONF_SN_NAMESERVER, CONF_MAX_WRITE_FILECOUNT, 16);
      max_write_file_count_ = std::min(max_write_file_count_, 128);

      max_use_capacity_ratio_ = TBSYS_CONFIG.getInt(CONF_SN_NAMESERVER, CONF_USE_CAPACITY_RATIO, 98);
      max_use_capacity_ratio_ = std::min(max_use_capacity_ratio_, 100);

      TBSYS_LOG(INFO, "load configure::max_block_size_:%u, max_replication_:%u,max_write_file_count_:%u,max_use_capacity_ratio_:%u",
          max_block_size_,max_replication_, max_write_file_count_,max_use_capacity_ratio_);

      heart_interval_ = TBSYS_CONFIG.getInt(CONF_SN_NAMESERVER, CONF_HEART_INTERVAL, 2);
      if (heart_interval_ <= 0)
        heart_interval_ = 2;

      replicate_wait_time_ = TBSYS_CONFIG.getInt(CONF_SN_NAMESERVER, CONF_REPL_WAIT_TIME, 240);
      if (replicate_wait_time_ <= between_ns_and_ds_lease_expire_time_)
        replicate_wait_time_ = between_ns_and_ds_lease_expire_time_ * 2;

      compact_delete_ratio_ = TBSYS_CONFIG.getInt(CONF_SN_NAMESERVER, CONF_COMPACT_DELETE_RATIO, 15);
      if (compact_delete_ratio_ <= 0)
        compact_delete_ratio_ = 15;
      compact_delete_ratio_ = std::min(compact_delete_ratio_, 100);

      compact_update_ratio_ = TBSYS_CONFIG.getInt(CONF_SN_NAMESERVER, CONF_COMPACT_UPDATE_RATIO, 10);
      if (compact_update_ratio_ <= 0)
        compact_update_ratio_ = 10;
      compact_update_ratio_  = std::min(compact_update_ratio_, 100);
      const char* compact_time_str = TBSYS_CONFIG.getString(CONF_SN_NAMESERVER, CONF_COMPACT_HOUR_RANGE, "2~6");
      set_hour_range(compact_time_str, compact_time_lower_, compact_time_upper_);
      compact_task_ratio_ = TBSYS_CONFIG.getInt(CONF_SN_NAMESERVER, CONF_COMPACT_TASK_RATIO, 1);

      object_wait_free_time_ = TBSYS_CONFIG.getInt(CONF_SN_NAMESERVER, CONF_OBJECT_WAIT_FREE_TIME_MS, 300);
      if (object_wait_free_time_ <=  300)
        object_wait_free_time_ = 300;

      object_wait_clear_time_ = TBSYS_CONFIG.getInt(CONF_SN_NAMESERVER, CONF_OBJECT_WAIT_CLEAR_TIME_MS, 180);
      if (object_wait_clear_time_ <=  180)
        object_wait_clear_time_ = 180;

      add_primary_block_count_ = TBSYS_CONFIG.getInt(CONF_SN_NAMESERVER, CONF_ADD_PRIMARY_BLOCK_COUNT, 3);
      if (add_primary_block_count_ <= 0)
        add_primary_block_count_ = 3;
      add_primary_block_count_ = std::min(add_primary_block_count_, max_write_file_count_);

      safe_mode_time_ = TBSYS_CONFIG.getInt(CONF_SN_NAMESERVER, CONF_SAFE_MODE_TIME, 300);
      if (safe_mode_time_ <= 120)
        safe_mode_time_ = 120;

      block_safe_mode_time_ = safe_mode_time_;

      task_expired_time_ = TBSYS_CONFIG.getInt(CONF_SN_NAMESERVER, CONF_TASK_EXPIRED_TIME, 120);
      if (task_expired_time_ <= 0)
        task_expired_time_ = 120;
      if (task_expired_time_ > object_wait_clear_time_)
        task_expired_time_ = object_wait_clear_time_ - 5;

      dump_stat_info_interval_ = TBSYS_CONFIG.getInt(CONF_SN_NAMESERVER, CONF_DUMP_STAT_INFO_INTERVAL, 10000000);
      if (dump_stat_info_interval_ <= 60000000)
        dump_stat_info_interval_ = 60000000;
      const char* percent = TBSYS_CONFIG.getString(CONF_SN_NAMESERVER, CONF_BALANCE_PERCENT,"0.00001");
      balance_percent_ = strtod(percent, NULL);
      group_count_ = TBSYS_CONFIG.getInt(CONF_SN_NAMESERVER, CONF_GROUP_COUNT, 1);
      if (group_count_ < 0)
      {
        TBSYS_LOG(ERROR, "%s in [%s] is invalid, value: %d", CONF_GROUP_COUNT, CONF_SN_NAMESERVER, group_count_);
        return EXIT_SYSTEM_PARAMETER_ERROR;
      }
      group_seq_ = TBSYS_CONFIG.getInt(CONF_SN_NAMESERVER, CONF_GROUP_SEQ, 0);
      if ((group_seq_ < 0)
        || (group_seq_ >= group_count_))
      {
        TBSYS_LOG(ERROR, "%s in [%s] is invalid, value: %d", CONF_GROUP_SEQ, CONF_SN_NAMESERVER, group_seq_);
        return EXIT_SYSTEM_PARAMETER_ERROR;
      }
      report_block_expired_time_ = TBSYS_CONFIG.getInt(CONF_SN_NAMESERVER, CONF_REPORT_BLOCK_EXPIRED_TIME, 30);
      discard_newblk_safe_mode_time_ = TBSYS_CONFIG.getInt(CONF_SN_NAMESERVER, CONF_DISCARD_NEWBLK_SAFE_MODE_TIME, safe_mode_time_ * 2);
      int32_t report_block_thread_nums = TBSYS_CONFIG.getInt(CONF_SN_NAMESERVER, CONF_REPORT_BLOCK_THREAD_COUNT, 4);
      report_block_queue_size_ = TBSYS_CONFIG.getInt(CONF_SN_NAMESERVER, CONF_REPORT_BLOCK_MAX_QUEUE_SIZE, report_block_thread_nums * 2);
      report_block_pending_size_ = TBSYS_CONFIG.getInt(CONF_SN_NAMESERVER, CONF_REPORT_BLOCK_MAX_PENDING_SIZE, 16);
      if (report_block_queue_size_ < report_block_thread_nums * 2)
         report_block_queue_size_ = report_block_thread_nums * 2;
      const char* report_hour_str = TBSYS_CONFIG.getString(CONF_SN_NAMESERVER, CONF_REPORT_BLOCK_HOUR_RANGE, "2~4");

      set_hour_range(report_hour_str, report_block_time_lower_, report_block_time_upper_);

      choose_target_server_random_max_nums_ = TBSYS_CONFIG.getInt(CONF_SN_NAMESERVER,CONF_CHOOSE_TARGET_SERVER_RANDOM_MAX_NUM, 8);

      choose_target_server_retry_max_nums_  = TBSYS_CONFIG.getInt(CONF_SN_NAMESERVER,CONF_CHOOSE_TARGET_SERVER_RETRY_MAX_NUM, 8);

      keepalive_queue_size_ = TBSYS_CONFIG.getInt(CONF_SN_NAMESERVER, CONF_HEART_MAX_QUEUE_SIZE, 1024);

      marshalling_delete_ratio_  = TBSYS_CONFIG.getInt(CONF_SN_NAMESERVER, CONF_MARSHALLING_DELETE_RATIO, 5);

      const char* marshalling_time_str = TBSYS_CONFIG.getString(CONF_SN_NAMESERVER, CONF_MARSHALLING_HOUR_RANGE, "6~9");
      set_hour_range(marshalling_time_str, marshalling_time_lower_, marshalling_time_upper_);

      marshalling_type_ = TBSYS_CONFIG.getInt(CONF_SN_NAMESERVER, CONF_MARSHALLING_TYPE, 1);

      max_data_member_num_ = TBSYS_CONFIG.getInt(CONF_SN_NAMESERVER, CONF_MAX_DATA_MEMBER_NUM, 3);
      max_check_member_num_ = TBSYS_CONFIG.getInt(CONF_SN_NAMESERVER, CONF_MAX_CHECK_MEMBER_NUM, 1);

      max_marshalling_queue_timeout_ = TBSYS_CONFIG.getInt(CONF_SN_NAMESERVER, CONF_MAX_MARSHALLING_QUEUE_TIMEOUT, 3600);

      // used by migrateserver
      migrate_complete_wait_time_ = TBSYS_CONFIG.getInt(CONF_SN_NAMESERVER, CONF_MIGRATE_COMPLETE_WAIT_TIME, 120);

      business_port_count_ = TBSYS_CONFIG.getInt(CONF_SN_NAMESERVER, CONF_BUSINESS_PORT_COUNT, 1);
      if (business_port_count_ <= 0)
      {
        TBSYS_LOG(ERROR, "%s in [%s] is invalid, value: %d", CONF_BUSINESS_PORT_COUNT, CONF_SN_NAMESERVER, business_port_count_);
        return EXIT_SYSTEM_PARAMETER_ERROR;
      }
      heart_port_count_ = TBSYS_CONFIG.getInt(CONF_SN_NAMESERVER, CONF_HEART_PORT_COUNT, 1);
      if (heart_port_count_ <= 0)
      {
        TBSYS_LOG(ERROR, "%s in [%s] is invalid, value: %d", CONF_HEART_PORT_COUNT, CONF_SN_NAMESERVER, heart_port_count_);
        return EXIT_SYSTEM_PARAMETER_ERROR;
      }

      plan_run_flag_ = TBSYS_CONFIG.getInt(CONF_SN_NAMESERVER, CONF_PLAN_RUN_FLAG, -1);

      return TFS_SUCCESS;
    }

    int DataServerParameter::initialize(const std::string& config_file, const std::string& index)
    {
      tbsys::CConfig config;
      int32_t ret = config.load(config_file.c_str());
      if (EXIT_SUCCESS != ret)
      {
        return TFS_ERROR;
      }

      heart_interval_ = config.getInt(CONF_SN_DATASERVER, CONF_HEART_INTERVAL, 5);
      check_interval_ = config.getInt(CONF_SN_DATASERVER, CONF_CHECK_INTERVAL, 2);
      expire_data_file_time_ = config.getInt(CONF_SN_DATASERVER, CONF_EXPIRE_DATAFILE_TIME, 10);
      expire_cloned_block_time_
        = config.getInt(CONF_SN_DATASERVER, CONF_EXPIRE_CLONEDBLOCK_TIME, 300);
      expire_compact_time_ = config.getInt(CONF_SN_DATASERVER, CONF_EXPIRE_COMPACTBLOCK_TIME, 86400);
      replicate_thread_count_ = config.getInt(CONF_SN_DATASERVER, CONF_REPLICATE_THREADCOUNT, 3);
      //default use O_SYNC
      sync_flag_ = config.getInt(CONF_SN_DATASERVER, CONF_WRITE_SYNC_FLAG, 1);
      max_block_size_ = config.getInt(CONF_SN_DATASERVER, CONF_BLOCK_MAX_SIZE);
      dump_vs_interval_ = config.getInt(CONF_SN_DATASERVER, CONF_VISIT_STAT_INTERVAL, -1);

      const char* max_io_time = config.getString(CONF_SN_DATASERVER, CONF_IO_WARN_TIME, "0");
      max_io_warn_time_ = strtoll(max_io_time, NULL, 10);
      if (max_io_warn_time_ < 200000 || max_io_warn_time_ > 2000000)
        max_io_warn_time_ = 1000000;

      tfs_backup_type_ = config.getInt(CONF_SN_DATASERVER, CONF_BACKUP_TYPE, 1);
      local_ns_ip_ = config.getString(CONF_SN_DATASERVER, CONF_IP_ADDR, "");
      if (local_ns_ip_.length() <= 0)
      {
        TBSYS_LOG(ERROR, "can not find %s in [%s]", CONF_IP_ADDR, CONF_SN_DATASERVER);
        return EXIT_SYSTEM_PARAMETER_ERROR;
      }
      local_ns_port_ = config.getInt(CONF_SN_DATASERVER, CONF_PORT);
      ns_addr_list_ = config.getString(CONF_SN_DATASERVER, CONF_IP_ADDR_LIST, "");
      if (ns_addr_list_.length() <= 0)
      {
        TBSYS_LOG(ERROR, "can not find %s in [%s]", CONF_IP_ADDR_LIST, CONF_SN_DATASERVER);
        return EXIT_SYSTEM_PARAMETER_ERROR;
      }
      slave_ns_ip_ = config.getString(CONF_SN_DATASERVER, CONF_SLAVE_NSIP, "");
      /*if (NULL == slave_ns_ip_)
      {
        TBSYS_LOG(ERROR, "can not find %s in [%s]", CONF_SLAVE_NSIP, CONF_SN_DATASERVER);
        return EXIT_SYSTEM_PARAMETER_ERROR;
      }*/
      //config_log_file_ = config.getString(CONF_SN_DATASERVER, CONF_LOG_FILE);
      max_datafile_nums_ = config.getInt(CONF_SN_DATASERVER, CONF_DATA_FILE_NUMS, 100);
      max_crc_error_nums_ = config.getInt(CONF_SN_DATASERVER, CONF_MAX_CRCERROR_NUMS, 4);
      max_eio_error_nums_ = config.getInt(CONF_SN_DATASERVER, CONF_MAX_EIOERROR_NUMS, 6);
      expire_check_block_time_
        = config.getInt(CONF_SN_DATASERVER, CONF_EXPIRE_CHECKBLOCK_TIME, 86400);
      max_cpu_usage_ = config.getInt(CONF_SN_DATASERVER, CONF_MAX_CPU_USAGE, 60);
      dump_stat_info_interval_ = config.getInt(CONF_SN_DATASERVER, CONF_DUMP_STAT_INFO_INTERVAL, 60000000);
      object_dead_max_time_ = config.getInt(CONF_SN_DATASERVER, CONF_OBJECT_DEAD_MAX_TIME, 3600);
      if (object_dead_max_time_ <=  0)
        object_dead_max_time_ = 3600;
      object_clear_max_time_ = config.getInt(CONF_SN_DATASERVER, CONF_OBJECT_CLEAR_MAX_TIME, 300);
      if (object_clear_max_time_ <= 0)
        object_clear_max_time_ = 300;
      max_sync_retry_count_ = config.getInt(CONF_SN_DATASERVER, CONF_MAX_SYNC_RETRY_COUNT, 3);
      max_sync_retry_interval_ = config.getInt(CONF_SN_DATASERVER, CONF_MAX_SYNC_RETRY_INTERVAL, 30);
      sync_fail_retry_interval_ = config.getInt(CONF_SN_DATASERVER, CONF_SYNC_FAIL_RETRY_INTERVAL, 300);
      max_bg_task_queue_size_ = config.getInt(CONF_SN_DATASERVER, CONF_MAX_BG_TASK_QUEUE_SIZE, 5);
      business_port_count_ = config.getInt(CONF_SN_DATASERVER, CONF_BUSINESS_PORT_COUNT, 1);
      heart_port_count_ = config.getInt(CONF_SN_DATASERVER, CONF_HEART_PORT_COUNT, 1);
      const int rack_id_tmp = config.getInt(CONF_SN_DATASERVER, CONF_RACK_ID, -1);
      if (rack_id_tmp < 0)
      {
        TBSYS_LOG(ERROR, "rack_id %d < 0 invalid", rack_id_tmp);
        return EXIT_SYSTEM_PARAMETER_ERROR;
      }
      rack_id_ = static_cast<uint32_t>(rack_id_tmp);

      // example ==> 10.232.36.201:3100:1|10.232.36.202:3100:2|10.232.36.203:3100:1
      const char* cluster_version_str = config.getString(CONF_SN_DATASERVER, CONF_CLUSTER_VERSION_LIST, NULL);
      if (NULL != cluster_version_str)
      {
        std::vector<std::string> clusters;
        Func::split_string(cluster_version_str, '|', clusters);
        std::vector<std::string>::iterator it = clusters.begin();
        for ( ; it != clusters.end(); it++)
        {
          std::vector<std::string> items;
          Func::split_string(it->c_str(), ':', items);
          if (items.size() != 3)
          {
            TBSYS_LOG(ERROR, "cluster_version %s invalid", cluster_version_str);
            return EXIT_SYSTEM_PARAMETER_ERROR;
          }
          else
          {
            uint64_t ns_id = Func::str_to_addr(items[0].c_str(), atoi(items[1].c_str()));
            int32_t version = atoi(items[2].c_str());
            std::map<uint64_t, int32_t>::iterator iter = cluster_version_list_.find(ns_id);
            if (iter == cluster_version_list_.end())
            {
              cluster_version_list_.insert(std::make_pair(ns_id, version));
            }
            else
            {
              TBSYS_LOG(ERROR, "cluster_version %s invalid, ns_addr duplicate", cluster_version_str);
              return EXIT_SYSTEM_PARAMETER_ERROR;
            }
          }
        }

        std::map<uint64_t, int32_t>::iterator iter = cluster_version_list_.begin();
        for ( ; iter != cluster_version_list_.end(); iter++)
        {
          TBSYS_LOG(INFO, "cluster nsip: %s, version: %d",
              tbsys::CNetUtil::addrToString(iter->first).c_str(), iter->second);
        }
      }

      return SYSPARAM_FILESYSPARAM.initialize(index);
    }

    std::string DataServerParameter::get_real_file_name(const std::string& src_file,
        const std::string& index, const std::string& suffix)
    {
      return src_file + "_" + index + "." + suffix;
    }

    int DataServerParameter::get_real_ds_port(const int ds_port, const std::string& index)
    {
      return ds_port + ((atoi(index.c_str()))- 1);
    }

    int FileSystemParameter::initialize(const std::string& index)
    {
      if (TBSYS_CONFIG.getString(CONF_SN_DATASERVER, CONF_MOUNT_POINT_NAME) == NULL || strlen(TBSYS_CONFIG.getString(
              CONF_SN_DATASERVER, CONF_MOUNT_POINT_NAME)) >= static_cast<uint32_t> (MAX_DEV_NAME_LEN))
      {
        TBSYS_LOG(ERROR, "can not find %s in [%s]", CONF_MOUNT_POINT_NAME, CONF_SN_DATASERVER);
        return EXIT_SYSTEM_PARAMETER_ERROR;
      }

      mount_name_ = TBSYS_CONFIG.getString(CONF_SN_DATASERVER, CONF_MOUNT_POINT_NAME);
      mount_name_ = get_real_mount_name(mount_name_, index);

      const char* tmp_max_size = TBSYS_CONFIG.getString(CONF_SN_DATASERVER, CONF_MOUNT_MAX_USESIZE);
      if (tmp_max_size == NULL)
      {
        TBSYS_LOG(ERROR, "can not find %s in [%s]", CONF_MOUNT_MAX_USESIZE, CONF_SN_DATASERVER);
        return EXIT_SYSTEM_PARAMETER_ERROR;
      }

      max_mount_size_ = strtoull(tmp_max_size, NULL, 10);
      // use system disk for index 0
      if (index == "0")
      {
        const char* tmp_extra_size = TBSYS_CONFIG.getString(CONF_SN_DATASERVER, CONF_EXTRA_MOUNT_MAX_USESIZE);
        if (tmp_extra_size == NULL)
        {
          TBSYS_LOG(ERROR, "can not find %s in [%s]", CONF_EXTRA_MOUNT_MAX_USESIZE, CONF_SN_DATASERVER);
          return EXIT_SYSTEM_PARAMETER_ERROR;
        }
        max_mount_size_ = strtoull(tmp_extra_size, NULL, 10);
      }

      base_fs_type_ = TBSYS_CONFIG.getInt(CONF_SN_DATASERVER, CONF_BASE_FS_TYPE);
      super_block_reserve_offset_ = TBSYS_CONFIG.getInt(CONF_SN_DATASERVER, CONF_SUPERBLOCK_START, 0);
      avg_segment_size_ = TBSYS_CONFIG.getInt(CONF_SN_DATASERVER, CONF_AVG_SEGMENT_SIZE);
      main_block_size_ = TBSYS_CONFIG.getInt(CONF_SN_DATASERVER, CONF_MAINBLOCK_SIZE);
      extend_block_size_ = TBSYS_CONFIG.getInt(CONF_SN_DATASERVER, CONF_EXTBLOCK_SIZE);

      const char* tmp_ratio = TBSYS_CONFIG.getString(CONF_SN_DATASERVER, CONF_BLOCKTYPE_RATIO);
      if (tmp_ratio == NULL)
      {
        TBSYS_LOG(ERROR, "can not find %s in [%s]", CONF_BLOCKTYPE_RATIO, CONF_SN_DATASERVER);
        return EXIT_SYSTEM_PARAMETER_ERROR;
      }

      block_type_ratio_ = strtof(tmp_ratio, NULL);
      if (block_type_ratio_ == 0)
      {
        TBSYS_LOG(ERROR, "%s error :%s", CONF_BLOCKTYPE_RATIO, tmp_ratio);
        return EXIT_SYSTEM_PARAMETER_ERROR;
      }

      file_system_version_ = TBSYS_CONFIG.getInt(CONF_SN_DATASERVER, CONF_BLOCK_VERSION, 1);

      const char* tmp_hash_ratio = TBSYS_CONFIG.getString(CONF_SN_DATASERVER, CONF_HASH_SLOT_RATIO);
      if (tmp_hash_ratio == NULL)
      {
        TBSYS_LOG(ERROR, "can not find %s in [%s]", CONF_HASH_SLOT_RATIO, CONF_SN_DATASERVER);
        return EXIT_SYSTEM_PARAMETER_ERROR;
      }

      hash_slot_ratio_ = strtof(tmp_hash_ratio, NULL);
      if (hash_slot_ratio_ == 0)
      {
        TBSYS_LOG(ERROR, "%s error :%s", CONF_HASH_SLOT_RATIO, tmp_hash_ratio);
        return EXIT_SYSTEM_PARAMETER_ERROR;
      }

      max_init_index_element_nums_ = TBSYS_CONFIG.getInt(CONF_SN_DATASERVER, CONF_MAX_INIT_INDEX_ELEMENT_NUMS, 2048);
      max_extend_index_element_nums_ = TBSYS_CONFIG.getInt(CONF_SN_DATASERVER, CONF_MAX_EXTEND_INDEX_ELEMENT_NUMS, 136);

      return TFS_SUCCESS;
    }
    std::string FileSystemParameter::get_real_mount_name(const std::string& mount_name, const std::string& index)
    {
      return mount_name + index;
    }

    int RcServerParameter::initialize(void)
    {
      db_info_ = TBSYS_CONFIG.getString(CONF_SN_RCSERVER, CONF_RC_DB_INFO, "");
      db_user_ = TBSYS_CONFIG.getString(CONF_SN_RCSERVER, CONF_RC_DB_USER, "");
      db_pwd_ = TBSYS_CONFIG.getString(CONF_SN_RCSERVER, CONF_RC_DB_PWD, "");
      std::string ops_db_info = TBSYS_CONFIG.getString(CONF_SN_RCSERVER, CONF_RC_OPS_DB_INFO, "");
      Func::split_string(ops_db_info.c_str(), ';', ops_db_info_);

      monitor_interval_ = TBSYS_CONFIG.getInt(CONF_SN_RCSERVER, CONF_RC_MONITOR_INTERVAL, 60);
      stat_interval_ = TBSYS_CONFIG.getInt(CONF_SN_RCSERVER, CONF_RC_STAT_INTERVAL, 120);
      update_interval_ = TBSYS_CONFIG.getInt(CONF_SN_RCSERVER, CONF_RC_UPDATE_INTERVAL, 30);
      count_interval_ = TBSYS_CONFIG.getInt(CONF_SN_RCSERVER, CONF_RC_COUNT_INTERVAL, 10);
      monitor_key_interval_ = TBSYS_CONFIG.getInt(CONF_SN_RCSERVER, CONF_RC_MONITOR_KEY_INTERVAL, 600);
      return TFS_SUCCESS;
    }

    int NameMetaServerParameter::initialize(void)
    {
      int ret = TFS_SUCCESS;
      max_pool_size_ = TBSYS_CONFIG.getInt(CONF_SN_NAMEMETASERVER, CONF_MAX_SPOOL_SIZE, 10);
      max_cache_size_ = TBSYS_CONFIG.getInt(CONF_SN_NAMEMETASERVER, CONF_MAX_CACHE_SIZE, 1024);
      max_mutex_size_ = TBSYS_CONFIG.getInt(CONF_SN_NAMEMETASERVER, CONF_MAX_MUTEX_SIZE, 16);
      free_list_count_ = TBSYS_CONFIG.getInt(CONF_SN_NAMEMETASERVER, CONF_FREE_LIST_COUNT, 256);
      max_sub_files_count_ = TBSYS_CONFIG.getInt(CONF_SN_NAMEMETASERVER, CONF_MAX_SUB_FILES_COUNT, 1000);
      max_sub_dirs_count_ = TBSYS_CONFIG.getInt(CONF_SN_NAMEMETASERVER, CONF_MAX_SUB_DIRS_COUNT, 100);
      max_sub_dirs_deep_ = TBSYS_CONFIG.getInt(CONF_SN_NAMEMETASERVER, CONF_MAX_SUB_DIRS_DEEP, 10);
      const char* gc_ratio = TBSYS_CONFIG.getString(CONF_SN_NAMEMETASERVER, CONF_GC_RATIO, "0.1");
      gc_ratio_ = strtod(gc_ratio, NULL);
      if (gc_ratio_ <= 0 || gc_ratio_ >= 1.0)
      {
        TBSYS_LOG(ERROR, "gc ration error %f set it to 0.1", gc_ratio_);
        gc_ratio_ = 0.1;
      }
      std::string db_infos = TBSYS_CONFIG.getString(CONF_SN_NAMEMETASERVER, CONF_META_DB_INFOS, "");
      std::vector<std::string> fields;
      Func::split_string(db_infos.c_str(), ';', fields);
      TBSYS_LOG(DEBUG, "fields.size = %zd", fields.size());
      for (size_t i = 0; i < fields.size(); i++)
      {
        std::vector<std::string> items;
        Func::split_string(fields[i].c_str(), ',', items);
        TBSYS_LOG(DEBUG, "items.size = %zd", items.size());
        DbInfo tmp_db_info;
        if (items.size() >= 3)
        {
          tmp_db_info.conn_str_ = items[0];
          tmp_db_info.user_ = items[1];
          tmp_db_info.passwd_ = items[2];
          tmp_db_info.hash_value_ = db_infos_.size();
          db_infos_.push_back(tmp_db_info);
        }
      }
      if (db_infos_.size() == 0)
      {
        TBSYS_LOG(ERROR, "can not find dbinfos");
        ret = TFS_ERROR;
      }
      if (TFS_SUCCESS == ret)
      {
        std::string ips = TBSYS_CONFIG.getString(CONF_SN_NAMEMETASERVER, CONF_IP_ADDR, "");
        std::vector<std::string> items;
        Func::split_string(ips.c_str(), ':', items);
        if (items.size() != 2U)
        {
          TBSYS_LOG(ERROR, "%s is invalid", ips.c_str());
          ret = TFS_ERROR;
        }
        else
        {
          int32_t port = atoi(items[1].c_str());
          if (port <= 1024 || port >= 65535)
          {
            TBSYS_LOG(ERROR, "%s is invalid", ips.c_str());
            ret = TFS_ERROR;
          }
          else
          {
            rs_ip_port_ = tbsys::CNetUtil::strToAddr(items[0].c_str(), atoi(items[1].c_str()));
          }
          TBSYS_LOG(INFO, "root server ip addr: %s", ips.c_str());
        }
      }
      return ret;
    }

    int RtServerParameter::initialize(void)
    {
      int32_t iret = TFS_SUCCESS;
      mts_rts_lease_expired_time_ =
        TBSYS_CONFIG.getInt(CONF_SN_ROOTSERVER, CONF_MTS_RTS_LEASE_EXPIRED_TIME,
          RTS_MS_LEASE_EXPIRED_TIME_DEFAULT);
      if (mts_rts_lease_expired_time_ <= 0)
      {
        TBSYS_LOG(ERROR, "mts_rts_lease_expired_time: %d is invalid", mts_rts_lease_expired_time_);
        iret = TFS_ERROR;
      }
      if (TFS_SUCCESS == iret)
      {
        mts_rts_renew_lease_interval_
        = TBSYS_CONFIG.getInt(CONF_SN_ROOTSERVER, CONF_MTS_RTS_LEASE_INTERVAL,
            RTS_MS_RENEW_LEASE_INTERVAL_TIME_DEFAULT);
        if (mts_rts_renew_lease_interval_ > mts_rts_lease_expired_time_ / 2 )
        {
          TBSYS_LOG(ERROR, "mts_rts_lease_expired_interval: %d is invalid, less than: %d",
            mts_rts_renew_lease_interval_, mts_rts_lease_expired_time_ / 2 + 1);
          iret = TFS_ERROR;
        }
        if (TFS_SUCCESS == iret)
        {
          safe_mode_time_ = TBSYS_CONFIG.getInt(CONF_SN_ROOTSERVER, CONF_SAFE_MODE_TIME, 60);
        }
      }
      if (TFS_SUCCESS == iret)
      {
        rts_rts_lease_expired_time_ =
          TBSYS_CONFIG.getInt(CONF_SN_ROOTSERVER, CONF_RTS_RTS_LEASE_EXPIRED_TIME,
            RTS_RS_LEASE_EXPIRED_TIME_DEFAULT);
        if (rts_rts_lease_expired_time_ <= 0)
        {
          TBSYS_LOG(ERROR, "rts_rts_lease_expired_time: %d is invalid", mts_rts_lease_expired_time_);
          iret = TFS_ERROR;
        }
        if (TFS_SUCCESS == iret)
        {
          rts_rts_renew_lease_interval_
            = TBSYS_CONFIG.getInt(CONF_SN_ROOTSERVER, CONF_RTS_RTS_LEASE_INTERVAL,
                RTS_RS_RENEW_LEASE_INTERVAL_TIME_DEFAULT);
          if (rts_rts_renew_lease_interval_ > rts_rts_lease_expired_time_ / 2 )
          {
            TBSYS_LOG(ERROR, "rts_rts_lease_expired_interval: %d is invalid, less than: %d",
                rts_rts_renew_lease_interval_, rts_rts_lease_expired_time_ / 2 + 1);
            iret = TFS_ERROR;
          }
        }
      }
      return iret;
    }

    int CheckServerParameter::initialize(const std::string& config_file)
    {
      tbsys::CConfig config;
      int32_t ret = config.load(config_file.c_str());
      if (EXIT_SUCCESS != ret)
      {
        TBSYS_LOG(ERROR, "load config file erro.");
        ret = TFS_ERROR;
      }
      else
      {
        check_span_ = config.getInt(CONF_SN_CHECKSERVER, CONF_CHECK_SPAN, 86400); // recent day
        if (0 == check_span_)
          check_span_ = INT_MAX;
        check_interval_ = config.getInt(CONF_SN_CHECKSERVER, CONF_CHECK_INTERVAL, 86400);  // check every day
        thread_count_ = config.getInt(CONF_SN_CHECKSERVER, CONF_THREAD_COUNT, 8);
        cluster_id_ = config.getInt(CONF_SN_CHECKSERVER, CONF_CLUSTER_ID, 1);
        check_retry_turns_ = config.getInt(CONF_SN_CHECKSERVER, CONF_CHECK_RETRY_TURN, 3);
        block_check_interval_ = config.getInt(CONF_SN_CHECKSERVER, CONF_BLOCK_CHECK_INTERVAL, 0);
        block_check_cost_ = config.getInt(CONF_SN_CHECKSERVER, CONF_BLOCK_CHECK_COST, 200);
        check_flag_ = config.getInt(CONF_SN_CHECKSERVER, CONF_CHECK_FLAG, 0);
        check_reserve_time_ = config.getInt(CONF_SN_CHECKSERVER, CONF_CHECK_RESERVE_TIME, 180);
        force_check_all_ = config.getInt(CONF_SN_CHECKSERVER, CONF_FORCE_CHECK_ALL, 0);
        group_seq_ = config.getInt(CONF_SN_CHECKSERVER, CONF_GROUP_SEQ, 0);
        group_count_ = config.getInt(CONF_SN_CHECKSERVER, CONF_GROUP_COUNT, 1);
      }

      // start_time like 01:30
      if (TFS_SUCCESS == ret)
      {
        start_time_hour_ = -1;
        start_time_min_ = -1;
        const char* start_time = config.getString(CONF_SN_CHECKSERVER, CONF_START_TIME, NULL);
        if (NULL != start_time)
        {
          std::vector<std::string> time_parts;
          common::Func::split_string(start_time, ':', time_parts);
          if (2 == time_parts.size())
          {
            start_time_hour_ = atoi(time_parts[0].c_str());
            start_time_min_ = atoi(time_parts[1].c_str());
            if (start_time_hour_ >= 24 || start_time_min_ >= 59)
            {
              TBSYS_LOG(ERROR, "start time config invalid");
              ret = TFS_ERROR;
            }
          }
          else
          {
            TBSYS_LOG(ERROR, "start time config invalid");
            ret = TFS_ERROR;
          }
        }
      }

      if (TFS_SUCCESS == ret)
      {
        const char* self_ip = config.getString(CONF_SN_PUBLIC, CONF_IP_ADDR);
        int32_t self_port = config.getInt(CONF_SN_PUBLIC, CONF_PORT);
        if ((NULL != self_ip) && (self_port > 0))
        {
          self_id_ = Func::str_to_addr(self_ip, self_port);
        }
        else
        {
          TBSYS_LOG(ERROR, "ip_addr or port config item not found");
          ret = TFS_ERROR;
        }
      }

      if (TFS_SUCCESS == ret)
      {
        const char* ns_ip  = config.getString(CONF_SN_CHECKSERVER, CONF_NS_IP, NULL);
        if (NULL != ns_ip)
        {
          std::vector<std::string> ns_ip_parts;
          common::Func::split_string(ns_ip, ':', ns_ip_parts);
          if (2 == ns_ip_parts.size())
          {
            ns_id_ = Func::str_to_addr(ns_ip_parts[0].c_str(), atoi(ns_ip_parts[1].c_str()));
          }
          else
          {
            ret = TFS_ERROR;
            TBSYS_LOG(ERROR, "ns_ip config invalid. must be ip:port.");
          }
        }
        else
        {
          TBSYS_LOG(ERROR, "ns_ip config item not found.");
          ret = TFS_ERROR;
        }
      }

      if (TFS_SUCCESS == ret)
      {
        const char* ns_ip  = config.getString(CONF_SN_CHECKSERVER, CONF_PEER_NS_IP, NULL);
        if (NULL != ns_ip)
        {
          std::vector<std::string> ns_ip_parts;
          common::Func::split_string(ns_ip, ':', ns_ip_parts);
          if (2 == ns_ip_parts.size())
          {
            peer_ns_id_ = Func::str_to_addr(ns_ip_parts[0].c_str(), atoi(ns_ip_parts[1].c_str()));
          }
          else
          {
            ret = TFS_ERROR;
            TBSYS_LOG(ERROR, "peer_ns_ip config invalid. must be ip:port.");
          }
        }
        else
        {
          TBSYS_LOG(ERROR, "peer_ns_ip config item not found.");
          ret = TFS_ERROR;
        }
      }

      return ret;
    }

    int MigrateServerParameter::initialize(void)
    {
      const char* ipaddr = TBSYS_CONFIG.getString(CONF_SN_MIGRATESERVER, CONF_IP_ADDR, "");
      const int32_t port = TBSYS_CONFIG.getInt(CONF_SN_MIGRATESERVER, CONF_PORT, 0);
      int32_t ret = (NULL != ipaddr && port > 1024 && port < 65535) ? TFS_SUCCESS : EXIT_SYSTEM_PARAMETER_ERROR;
      if (TFS_SUCCESS != ret)
      {
        TBSYS_LOG(ERROR, "migrateserver not set (nameserver vip) ipaddr: %s or port: %d, must be exit", NULL == ipaddr ? "null" : ipaddr, port);
      }
      if (TFS_SUCCESS == ret)
      {
        ds_base_port_ = TBSYS_CONFIG.getInt(CONF_SN_MIGRATESERVER, CONF_DS_BASE_PORT, 3200);
        max_full_ds_count_ = TBSYS_CONFIG.getInt(CONF_SN_MIGRATESERVER, CONF_MAX_FULL_DS_COUNT, 12);
        ret = (port > 1024 && port < 65535 && max_full_ds_count_ > 0) ? TFS_SUCCESS : EXIT_SYSTEM_PARAMETER_ERROR;
        if (TFS_SUCCESS != ret)
        {
          TBSYS_LOG(ERROR, "migrateserver not ds base port: %d or max full ds count: %d invalid, must be exit", ds_base_port_, max_full_ds_count_);
        }
      }
      if (TFS_SUCCESS == ret)
      {
        ns_vip_port_ = tbsys::CNetUtil::strToAddr(ipaddr, port);

        const char* percent = TBSYS_CONFIG.getString(CONF_SN_MIGRATESERVER, CONF_BALANCE_PERCENT, "0.05");
        balance_percent_ = strtod(percent, NULL);
        const char* penalty = TBSYS_CONFIG.getString(CONF_SN_MIGRATESERVER, CONF_PENALTY_PERCENT, "0.8");
        penalty_percent_ = strtod(penalty, NULL);

        update_statistic_interval_ = TBSYS_CONFIG.getInt(CONF_SN_MIGRATESERVER, CONF_UPDATE_STATISTIC_INTERVAL, 3600);
        const int32_t TWO_MONTH = 2 * 31 * 86400;
        hot_time_range_ = TBSYS_CONFIG.getInt(CONF_SN_MIGRATESERVER, CONF_HOT_TIME_RANGE, TWO_MONTH);

        const char* str_full_disk_access_ratio = TBSYS_CONFIG.getString(CONF_SN_MIGRATESERVER, CONF_FULL_DISK_ACCESS_RATIO, "");
        const char* str_system_disk_access_ratio = TBSYS_CONFIG.getString(CONF_SN_MIGRATESERVER, CONF_SYSTEM_DISK_ACCESS_RATIO, "");
        std::vector<std::string> ratios[2];
        Func::split_string(str_full_disk_access_ratio, ':', ratios[0]);
        Func::split_string(str_system_disk_access_ratio, ':', ratios[1]);
        ret = (5U == ratios[0].size() && 5U == ratios[1].size()) ? TFS_SUCCESS : EXIT_SYSTEM_PARAMETER_ERROR;
        if (TFS_SUCCESS == ret)
        {
          AccessRatio* ar[2];
          ar[0] = &full_disk_access_ratio_;
          ar[1] = &system_disk_access_ratio_;
          for (int32_t i = 0; i < 2; ++i)
          {
            ar[i]->last_access_time_ratio = atoi(ratios[i][0].c_str());
            ar[i]->read_ratio = atoi(ratios[i][1].c_str());
            ar[i]->write_ratio = atoi(ratios[i][2].c_str());
            ar[i]->update_ratio = atoi(ratios[i][3].c_str());
            ar[i]->unlink_ratio = atoi(ratios[i][4].c_str());
          }
        }
        need_migrate_back_ = TBSYS_CONFIG.getInt(CONF_SN_MIGRATESERVER, CONF_NEED_MIGRATE_BACK, 0);
      }
      return ret;
    }

    int KvMetaParameter::initialize(const std::string& config_file)
    {
      tbsys::CConfig config;
      int32_t ret = config.load(config_file.c_str());
      if (EXIT_SUCCESS != ret)
      {
        TBSYS_LOG(ERROR, "load config file erro.");
        return TFS_ERROR;
      }

      conn_str_ = config.getString(CONF_SN_KVMETA, CONF_KV_DB_CONN, "");
      user_name_ = config.getString(CONF_SN_KVMETA, CONF_KV_DB_USER, "");
      pass_wd_ = config.getString(CONF_SN_KVMETA, CONF_KV_DB_PASS, "");
      pool_size_ = config.getInt(CONF_SN_KVMETA, CONF_KV_DB_POOL_SIZE, 20);
      object_area_ = config.getInt(CONF_SN_KVMETA, CONF_OBJECT_AREA, -1);
      lifecycle_area_ = config.getInt(CONF_SN_KVMETA, CONF_LIFECYCLE_AREA, -1);
      dump_stat_info_interval_ = config.getInt(CONF_SN_KVMETA, CONF_STAT_INFO_INTERVAL, 60000000);

      if (TFS_SUCCESS == ret)
      {
        std::string ips1 = TBSYS_CONFIG.getString(CONF_SN_KVMETA, CONF_KV_ROOT_IPPORT, "");
        std::vector<std::string> items1;
        Func::split_string(ips1.c_str(), ':', items1);
        if (items1.size() != 2U)
        {
          TBSYS_LOG(ERROR, "%s is invalid", ips1.c_str());
          ret = TFS_ERROR;
        }
        else
        {
          int32_t port1 = atoi(items1[1].c_str());
          if (port1 <= 1024 || port1 >= 65535)
          {
            TBSYS_LOG(ERROR, "%s is invalid", ips1.c_str());
            ret = TFS_ERROR;
          }
          else
          {
            rs_ip_port_ = tbsys::CNetUtil::strToAddr(items1[0].c_str(), atoi(items1[1].c_str()));
          }
          TBSYS_LOG(INFO, "kv root server ip addr: %s", ips1.c_str());
        }
      }
      if (TFS_SUCCESS == ret)
      {
        std::string ips2 = TBSYS_CONFIG.getString(CONF_SN_PUBLIC, CONF_IP_ADDR, "");
        std::string ports2 = TBSYS_CONFIG.getString(CONF_SN_PUBLIC, CONF_PORT, "");

        int32_t port2 = atoi(ports2.c_str());
        if (port2 <= 1024 || port2 >= 65535)
        {
          TBSYS_LOG(ERROR, "%s is invalid", ports2.c_str());
          ret = TFS_ERROR;
        }
        else
        {
          ms_ip_port_ = tbsys::CNetUtil::strToAddr(ips2.c_str(), port2);
        }
        TBSYS_LOG(INFO, "kv meta server ip addr: %s:%d", ips2.c_str(), port2);
      }
      return ret;
    }

    int KvRtServerParameter::initialize(void)
    {
      int32_t iret = TFS_SUCCESS;
      kv_rts_check_lease_interval_ =
        TBSYS_CONFIG.getInt(CONF_SN_KVROOTSERVER, CONF_KV_MTS_RTS_LEASE_CHECK_TIME, 1);
      if (kv_rts_check_lease_interval_ <= 0)
      {
        TBSYS_LOG(ERROR, "kv_rts_check_lease_interval_: %d is invalid", kv_rts_check_lease_interval_);
        iret = TFS_ERROR;
      }
      kv_mts_rts_lease_expired_time_ =
        TBSYS_CONFIG.getInt(CONF_SN_KVROOTSERVER, CONF_KV_MTS_RTS_LEASE_EXPIRED_TIME, 4);
      if (kv_mts_rts_lease_expired_time_ <= 0)
      {
        TBSYS_LOG(ERROR, "kv_mts_rts_lease_expired_time: %d is invalid", kv_mts_rts_lease_expired_time_);
        iret = TFS_ERROR;
      }
      if (TFS_SUCCESS == iret)
      {
        kv_mts_rts_heart_interval_
        = TBSYS_CONFIG.getInt(CONF_SN_KVROOTSERVER, CONF_KV_MTS_RTS_HEART_INTERVAL, 2);
        if (kv_mts_rts_heart_interval_ > kv_mts_rts_lease_expired_time_ / 2 )
        {
          TBSYS_LOG(ERROR, "mts_rts_lease_expired_interval: %d is invalid, less than: %d",
            kv_mts_rts_heart_interval_, kv_mts_rts_lease_expired_time_ / 2 + 1);
          iret = TFS_ERROR;
        }
        if (TFS_SUCCESS == iret)
        {
          safe_mode_time_ = TBSYS_CONFIG.getInt(CONF_SN_KVROOTSERVER, CONF_SAFE_MODE_TIME, 60);
        }
      }
      return iret;
    }

    int ExpireServerParameter::initialize(const std::string& config_file)
    {
      tbsys::CConfig config;
      int32_t ret = config.load(config_file.c_str());
      if (EXIT_SUCCESS != ret)
      {
        TBSYS_LOG(ERROR, "load config file erro.");
        return TFS_ERROR;
      }
      conn_str_ = config.getString(CONF_SN_EXPIRESERVER, CONF_KV_DB_CONN, "");
      user_name_ = config.getString(CONF_SN_EXPIRESERVER, CONF_KV_DB_USER, "");
      pass_wd_ = config.getString(CONF_SN_EXPIRESERVER, CONF_KV_DB_PASS, "");
      pool_size_ = config.getInt(CONF_SN_EXPIRESERVER, CONF_KV_DB_POOL_SIZE, 20);
      lifecycle_area_ = config.getInt(CONF_SN_EXPIRESERVER, CONF_LIFECYCLE_AREA, -1);

      re_clean_days_ = config.getInt(CONF_SN_EXPIRESERVER, CONF_EXPIRE_RE_CLEAN_DAYS, 1);
      nginx_root_ = config.getString(CONF_SN_EXPIRESERVER, CONF_ES_NGINX_ROOT, "");
      es_appkey_ = config.getString(CONF_SN_EXPIRESERVER, CONF_ES_APPKEY, "");
      log_level_ = config.getString(CONF_SN_PUBLIC, CONF_LOG_LEVEL, "debug");
      if (TFS_SUCCESS == ret)
      {
        std::string ips1 = TBSYS_CONFIG.getString(CONF_SN_EXPIRESERVER, CONF_EXPIRE_ROOT_SERVER_IPPORT, "");
        std::vector<std::string> items1;
        Func::split_string(ips1.c_str(), ':', items1);
        if (items1.size() != 2U)
        {
          TBSYS_LOG(ERROR, "%s is invalid", ips1.c_str());
          ret = TFS_ERROR;
        }
        else
        {
          int32_t port1 = atoi(items1[1].c_str());
          if (port1 <= 1024 || port1 >= 65535)
          {
            TBSYS_LOG(ERROR, "%s is invalid", ips1.c_str());
            ret = TFS_ERROR;
          }
          else
          {
            ers_ip_port_ = tbsys::CNetUtil::strToAddr(items1[0].c_str(), atoi(items1[1].c_str()));
          }
          TBSYS_LOG(INFO, "expire root server ip addr: %s", ips1.c_str());
        }
      }

      if (TFS_SUCCESS == ret)
      {
        std::string ips2 = TBSYS_CONFIG.getString(CONF_SN_PUBLIC, CONF_IP_ADDR, "");
        std::string ports2 = TBSYS_CONFIG.getString(CONF_SN_PUBLIC, CONF_PORT, "");

        int32_t port2 = atoi(ports2.c_str());
        if (port2 <= 1024 || port2 >= 65535)
        {
          TBSYS_LOG(ERROR, "%s is invalid", ports2.c_str());
          ret = TFS_ERROR;
        }
        else
        {
          es_ip_port_ = tbsys::CNetUtil::strToAddr(ips2.c_str(), port2);
        }
        TBSYS_LOG(INFO, "expire server ip addr: %s:%d", ips2.c_str(), port2);
      }
      return ret;
    }

    int ExpireRootServerParameter::initialize(const std::string& config_file)
    {
      tbsys::CConfig config;
      int32_t ret = config.load(config_file.c_str());
      if (EXIT_SUCCESS != ret)
      {
        TBSYS_LOG(ERROR, "load config file erro.");
        return TFS_ERROR;
      }

      conn_str_ = config.getString(CONF_SN_EXPIREROOTSERVER, CONF_KV_DB_CONN, "");
      user_name_ = config.getString(CONF_SN_EXPIREROOTSERVER, CONF_KV_DB_USER, "");
      pass_wd_ = config.getString(CONF_SN_EXPIREROOTSERVER, CONF_KV_DB_PASS, "");
      lifecycle_area_ = config.getInt(CONF_SN_EXPIREROOTSERVER, CONF_LIFECYCLE_AREA, -1);
      task_period_ = config.getInt(CONF_SN_EXPIREROOTSERVER, CONF_TASK_PERIOD, 0);
      note_interval_ = config.getInt(CONF_SN_EXPIREROOTSERVER, CONF_NOTE_INTERVAL, 0);


      es_rts_check_lease_interval_ =
        TBSYS_CONFIG.getInt(CONF_SN_EXPIREROOTSERVER, CONF_ES_RTS_LEASE_CHECK_TIME, 1);
      if (es_rts_check_lease_interval_ <= 0)
      {
        TBSYS_LOG(ERROR, "es_rts_check_lease_interval_: %d is invalid", es_rts_check_lease_interval_);
        ret = TFS_ERROR;
      }

      es_rts_lease_expired_time_ =
        TBSYS_CONFIG.getInt(CONF_SN_EXPIREROOTSERVER, CONF_ES_RTS_LEASE_EXPIRED_TIME, 4);
      if (es_rts_lease_expired_time_ <= 0)
      {
        TBSYS_LOG(ERROR, "es_rts_lease_expired_time: %d is invalid", es_rts_lease_expired_time_);
        ret = TFS_ERROR;
      }

      if (TFS_SUCCESS == ret)
      {
        es_rts_heart_interval_
        = TBSYS_CONFIG.getInt(CONF_SN_EXPIREROOTSERVER, CONF_ES_RTS_HEART_INTERVAL, 2);
        if (es_rts_heart_interval_ > es_rts_lease_expired_time_ / 2 )
        {
          TBSYS_LOG(ERROR, "es_rts_lease_expired_interval: %d is invalid, less than: %d",
            es_rts_heart_interval_, es_rts_lease_expired_time_ / 2 + 1);
          ret = TFS_ERROR;
        }

        if (TFS_SUCCESS == ret)
        {
          safe_mode_time_ = TBSYS_CONFIG.getInt(CONF_SN_EXPIREROOTSERVER, CONF_SAFE_MODE_TIME, 60);
        }
      }

      return ret;
    }

  }/** common **/
}/** tfs **/

