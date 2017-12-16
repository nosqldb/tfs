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
 *
 *   Authors:
 *          zongdai(zongdai@taobao.com)
 *
 */
#ifndef TFS_COMMON_RC_DEFINE_H_
#define TFS_COMMON_RC_DEFINE_H_

#include <vector>
#include <map>
#include <string>

#include <stdint.h>
#include "internal.h"

namespace tfs
{
  namespace common
  {
    enum UpdateFlag
    {
      LOGIN_FLAG = 1,
      KA_FLAG,
      LOGOUT_FLAG
    };

    enum ReloadType
    {
      RELOAD_CONFIG = 1,
      RELOAD_RESOURCE
    };

    struct ClusterData
    {
      ClusterData() :
        cluster_stat_(-1),
        access_type_(-1)
      {
      }

      int deserialize(const char* data, const int64_t data_len, int64_t& pos);
      int serialize(char* data, const int64_t data_len, int64_t& pos) const;
      int64_t length() const;
      void dump() const;

      int32_t cluster_stat_;
      int32_t access_type_;
      std::string cluster_id_;
      std::string ns_vip_;
    };

    struct ClusterRackData
    {
      ClusterRackData() :
        need_duplicate_(false)
      {
      }

      int deserialize(const char* data, const int64_t data_len, int64_t& pos);
      int serialize(char* data, const int64_t data_len, int64_t& pos) const;
      int64_t length() const;
      void dump() const;

      bool need_duplicate_;
      std::string dupliate_server_addr_;
      std::vector<ClusterData> cluster_data_;
    };

    struct BaseInfo
    {
      BaseInfo() : report_interval_(0), modify_time_(0), meta_root_server_(0), use_remote_cache_(0)
      {
      }

      int deserialize(const char* data, const int64_t data_len, int64_t& pos);
      int serialize(char* data, const int64_t data_len, int64_t& pos) const;
      int64_t length() const;
      void dump() const;

      std::vector<uint64_t> rc_server_infos_;
      std::vector<ClusterRackData> cluster_infos_;
      int32_t report_interval_;
      int64_t modify_time_;
      int64_t meta_root_server_;
      std::vector<int64_t> kvroot_server_infos_;
      std::string ns_cache_info_;
      std::vector<ClusterData> cluster_infos_for_update_;
      int32_t use_remote_cache_;

      BaseInfo& operator= (const BaseInfo& right)
      {
        rc_server_infos_ = right.rc_server_infos_;
        cluster_infos_ = right.cluster_infos_;
        report_interval_ = right.report_interval_;
        modify_time_ = right.modify_time_;
        meta_root_server_ = right.meta_root_server_;
        kvroot_server_infos_ = right.kvroot_server_infos_;
        ns_cache_info_ = right.ns_cache_info_;
        cluster_infos_for_update_ = right.cluster_infos_for_update_;
        use_remote_cache_ = right.use_remote_cache_;
        return *this;
      }
    };

    struct SessionBaseInfo
    {
      int deserialize(const char* data, const int64_t data_len, int64_t& pos);
      int serialize(char* data, const int64_t data_len, int64_t& pos) const;
      int64_t length() const;
      void dump() const;

      std::string session_id_;
      std::string client_version_;
      int64_t cache_size_;
      int64_t cache_time_;
      int64_t modify_time_;
      bool is_logout_;

      SessionBaseInfo(const std::string session_id) :
        session_id_(session_id), cache_size_(0), cache_time_(0), modify_time_(0), is_logout_(false)
      {
      }
      SessionBaseInfo() : cache_size_(0), cache_time_(0), modify_time_(0), is_logout_(false)
      {
      }

    };

    enum OperType
    {
      OPER_INVALID = 0,
      OPER_READ,
      OPER_WRITE,
      OPER_UNIQUE_WRITE,
      OPER_UNLINK,
      OPER_UNIQUE_UNLINK
    };

    struct AppOperInfo
    {
      int deserialize(const char* data, const int64_t data_len, int64_t& pos);
      int serialize(char* data, const int64_t data_len, int64_t& pos) const;
      static int64_t length()
      {
        return INT_SIZE  + INT64_SIZE * 4;
      }
      void dump() const;

      OperType oper_type_;
      int64_t oper_times_;  //times
      int64_t oper_size_;   //size
      int64_t oper_rt_;     //time
      int64_t oper_succ_;   //succ
      int32_t oper_app_id_;

      AppOperInfo() : oper_type_(OPER_INVALID), oper_times_(0),
      oper_size_(0), oper_rt_(0), oper_succ_(0),oper_app_id_(0)
      {
      }

      AppOperInfo& operator +=(const AppOperInfo& right)
      {
        //assert(oper_type_ == right.oper_type_);
        oper_times_ += right.oper_times_;
        oper_size_ += right.oper_size_;
        oper_rt_ += right.oper_rt_;
        oper_succ_+= right.oper_succ_;
        return *this;
      }
    };

    struct SessionStat
    {
      int deserialize(const char* data, const int64_t data_len, int64_t& pos);
      int serialize(char* data, const int64_t data_len, int64_t& pos) const;
      int64_t length() const;
      void dump() const;

      std::map<OperType, AppOperInfo> app_oper_info_;
      int64_t cache_hit_ratio_;

      SessionStat() : cache_hit_ratio_(100)
      {
        app_oper_info_.clear();
      }

      SessionStat& operator +=(const SessionStat& right)
      {
        std::map<OperType, AppOperInfo>::iterator lit;
        std::map<OperType, AppOperInfo>::const_iterator rit = right.app_oper_info_.begin();
        for ( ; rit != right.app_oper_info_.end(); ++rit)
        {
          lit = app_oper_info_.find(rit->first);
          if (lit == app_oper_info_.end()) //not found
          {
            app_oper_info_.insert(std::pair<OperType, AppOperInfo>(rit->first, rit->second));
          }
          else //found
          {
            lit->second += rit->second;
          }
        }
        return *this;
      }

    };

    struct KeepAliveInfo
    {
      KeepAliveInfo(const std::string& session_id) : s_base_info_(session_id), last_report_time_(time(NULL))
      {
      }
      KeepAliveInfo() : last_report_time_(time(NULL))
      {
      }

      int deserialize(const char* data, const int64_t data_len, int64_t& pos);
      int serialize(char* data, const int64_t data_len, int64_t& pos) const;
      int64_t length() const;
      void dump() const;

      KeepAliveInfo& operator +=(const KeepAliveInfo& right)
      {
        s_stat_ += right.s_stat_;
        if (last_report_time_ < right.last_report_time_)
        {
          s_base_info_ = right.s_base_info_;
          s_stat_.cache_hit_ratio_ = right.s_stat_.cache_hit_ratio_;
          last_report_time_ = right.last_report_time_;
        }
        return *this;
      }

      SessionBaseInfo s_base_info_;
      SessionStat s_stat_;
      int64_t last_report_time_;
    };

    struct MonitorKeyAidKey
    {
      std::string service_type_;
      std::string app_version_;
      bool operator < (const MonitorKeyAidKey& key) const
      {
        return ( 0 == app_version_.compare(key.app_version_) && 0 == service_type_.compare(key.service_type_));
      }
    };

    struct MonitorKeyInfo
    {
      std::string key_;
      int32_t key_flag_;
      int32_t key_repeat_report_;
      int32_t key_internal_time_report_;
      int32_t key_threshold_report_;
      int64_t last_report_time_;

      int deserialize(const char* data, const int64_t data_len, int64_t& pos);
      int serialize(char* data, const int64_t data_len, int64_t& pos) const;
      int64_t length() const;
    };


    typedef std::map<std::string, KeepAliveInfo> SessionCollectMap;
    typedef SessionCollectMap::const_iterator SessionCollectMapConstIter;
    typedef SessionCollectMap::iterator SessionCollectMapIter;

    typedef std::map<int32_t, SessionCollectMap> AppSessionMap;
    typedef AppSessionMap::const_iterator AppSessionMapConstIter;
    typedef AppSessionMap::iterator AppSessionMapIter;

    typedef std::multimap<uint64_t, SessionStat> SessionStatMap;
    typedef SessionStatMap::const_iterator SessionStatMapConstIter;
    typedef SessionStatMap::iterator SessionStatMapIter;

    typedef std::multimap<OperType, AppOperInfo> AppOperInfoMap;
    typedef AppOperInfoMap ::const_iterator AppOperInfoMapConstIter;
    typedef AppOperInfoMap ::iterator AppOperInfoMapIter;

    typedef std::map<MonitorKeyAidKey, std::vector<MonitorKeyInfo> > MonitorKeyMap;
    typedef MonitorKeyMap::const_iterator CONST_MONITOR_KEY_MAP_ITER;
    typedef MonitorKeyMap::iterator MONITOR_KEY_MAP_ITER;
  }
}
#endif //TFS_RC_DEFINE_H_
