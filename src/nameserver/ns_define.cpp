/*
 * (C) 2007-2010 Alibaba Group Holding Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 * Version: $Id: ns_define.h 344 2011-05-26 01:17:38Z duanfei@taobao.com $
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
#include "common/define.h"
#include "common/atomic.h"
#include "ns_define.h"
#include "common/error_msg.h"
#include "common/parameter.h"
#include "server_collect.h"

using namespace tfs::common;

namespace tfs
{
  namespace nameserver
  {
    bool in_hour_range(const int64_t now, int32_t& min, int32_t& max)
    {
      struct tm lt;
      localtime_r(&now, &lt);
      if (min > max)
        std::swap(min, max);
      return (lt.tm_hour >= min && lt.tm_hour < max);
    }

    bool in_min_range(const int64_t now, const int32_t hour, const int32_t min)
    {
      struct tm lt;
      localtime_r(&now, &lt);
      return (lt.tm_hour == hour && lt.tm_min < min);
    }

    void NsGlobalStatisticsInfo::dump(int32_t level, const char* file , const int32_t line , const char* function , const pthread_t thid) const
    {
      TBSYS_LOGGER.logMessage(level, file, line, function, thid,
          "use_capacity: %" PRI64_PREFIX "d, total_capacity: %" PRI64_PREFIX "d, total_block_count: %" PRI64_PREFIX "d, total_load: %" PRI64_PREFIX "d",
          use_capacity_, total_capacity_, total_block_count_, total_load_);
    }

    NsRuntimeGlobalInformation::NsRuntimeGlobalInformation()
    {
      initialize();
    }

    void NsRuntimeGlobalInformation::initialize()
    {
      memset(this, 0, sizeof(*this));
      owner_role_ = NS_ROLE_SLAVE;
      peer_role_ = NS_ROLE_SLAVE;
      destroy_flag_ = false;
      owner_status_ = NS_STATUS_NONE;
      peer_status_ = NS_STATUS_NONE;
    }

    void NsRuntimeGlobalInformation::destroy()
    {
      destroy_flag_ = true;
      //memset(this, 0, sizeof(*this));
      //owner_role_ = NS_ROLE_SLAVE;
      //peer_role_ = NS_ROLE_SLAVE;
      //owner_status_ = NS_STATUS_NONE;
      //peer_status_ = NS_STATUS_NONE;
    }

    uint64_t NsRuntimeGlobalInformation::choose_report_block_ipport_addr(const uint64_t server) const
    {
      return heart_ip_ports_.empty() ? INVALID_SERVER_ID : heart_ip_ports_[server % heart_ip_ports_.size()];
    }

    bool NsRuntimeGlobalInformation::is_destroyed() const
    {
      return destroy_flag_;
    }

    int8_t NsRuntimeGlobalInformation::get_role() const
    {
      return owner_role_;
    }

    bool NsRuntimeGlobalInformation::is_master() const
    {
      return owner_role_ == NS_ROLE_MASTER;
    }

    bool NsRuntimeGlobalInformation::peer_is_master() const
    {
      return peer_role_ == NS_ROLE_MASTER;
    }

    bool NsRuntimeGlobalInformation::own_is_initialize_complete() const
    {
      return owner_status_ == NS_STATUS_INITIALIZED;
    }

    void NsRuntimeGlobalInformation::update_peer_info(const uint64_t server, const int8_t role, const int8_t status)
    {
      peer_ip_port_ = server;
      peer_role_ = role;
      peer_status_ = status;
    }

    void NsRuntimeGlobalInformation::switch_role(const bool startup, const int64_t now)
    {
      lease_id_ = common::INVALID_LEASE_ID;
      lease_expired_time_ = 0;
      load_family_info_complete_ = false;
      if (startup)//startup
      {
        startup_time_ = now;
        peer_role_ = NS_ROLE_SLAVE;
        apply_block_safe_mode_time_  = now;
        switch_time_  = now + common::SYSPARAM_NAMESERVER.safe_mode_time_;
        discard_newblk_safe_mode_time_ =  now + common::SYSPARAM_NAMESERVER.discard_newblk_safe_mode_time_;
        owner_role_ = common::Func::is_local_addr(vip_) == true ? NS_ROLE_MASTER : NS_ROLE_SLAVE;
        TBSYS_LOG(INFO, "i %s the master server", owner_role_ == NS_ROLE_MASTER ? "am" : "am not");
      }
      else//switch
      {
        owner_role_ = owner_role_ == NS_ROLE_MASTER ? NS_ROLE_SLAVE : NS_ROLE_MASTER;
        peer_role_ = owner_role_ == NS_ROLE_MASTER ? NS_ROLE_SLAVE : NS_ROLE_MASTER;
        apply_block_safe_mode_time_ = now + common::SYSPARAM_NAMESERVER.between_ns_and_ds_lease_expire_time_;
        if (now - common::SYSPARAM_NAMESERVER.safe_mode_time_ < startup_time_)
          discard_newblk_safe_mode_time_ =  now + common::SYSPARAM_NAMESERVER.discard_newblk_safe_mode_time_;
        TBSYS_LOG(INFO, "i %s the master server", owner_role_ == NS_ROLE_MASTER ? "am" : "am not");
      }
    }

    bool NsRuntimeGlobalInformation::in_safe_mode_time(const int64_t now) const
    {
      return  now < switch_time_;
    }

    bool NsRuntimeGlobalInformation::in_discard_newblk_safe_mode_time(const int64_t now) const
    {
      return now < discard_newblk_safe_mode_time_;
    }

    bool NsRuntimeGlobalInformation::in_apply_block_safe_mode_time(const int64_t now) const
    {
      return now < apply_block_safe_mode_time_;
    }

    bool NsRuntimeGlobalInformation::in_report_block_time(const int64_t now) const
    {
      bool report_time = in_min_range(time(NULL), SYSPARAM_NAMESERVER.report_block_time_lower_, (SYSPARAM_NAMESERVER.safe_mode_time_ + 59) / 60);
      return ((report_time) || ((switch_time_ +  SYSPARAM_NAMESERVER.safe_mode_time_) > now));
    }

    int NsRuntimeGlobalInformation::keepalive(int64_t& lease_id, const uint64_t server,
         const int8_t role, const int8_t status, const int8_t type, const time_t now)
    {
      static uint64_t lease_id_factory = 1;
      int32_t ret = owner_role_ == NS_ROLE_MASTER ? common::TFS_SUCCESS : common::EXIT_ROLE_ERROR;
      if (common::TFS_SUCCESS == ret)
      {
        peer_status_  = static_cast<NsStatus>(status);
        peer_role_ = static_cast<NsRole>(role);
        if (NS_KEEPALIVE_TYPE_LOGIN == type)
        {
          peer_ip_port_ = server;
          lease_id = lease_id_ = common::atomic_inc(&lease_id_factory);
          renew(now, common::SYSPARAM_NAMESERVER.heart_interval_);
        }
        else if (NS_KEEPALIVE_TYPE_RENEW == type)
        {
          ret = common::INVALID_LEASE_ID == lease_id_ ? common::EXIT_LEASE_EXPIRED : common::TFS_SUCCESS;
          if (common::TFS_SUCCESS == ret)
          {
            ret = lease_id_ == lease_id ? common::TFS_SUCCESS : common::EXIT_RENEW_LEASE_ERROR;
            if (common::TFS_SUCCESS == ret)
            {
              renew(now, common::SYSPARAM_NAMESERVER.heart_interval_);
            }
          }
        }
        else if (NS_KEEPALIVE_TYPE_LOGOUT == type)
        {
          logout();
        }
      }
      TBSYS_LOG(DEBUG, "peer_role: %d, peer_status: %d,status: %d, lease_id: %ld, %ld, type: %d, time: %ld,now: %ld",
        peer_role_, peer_status_, status, lease_id, lease_id_, type, lease_expired_time_, now);
      return ret;
    }

    bool NsRuntimeGlobalInformation::has_valid_lease(const time_t now) const
    {
      TBSYS_LOG(DEBUG, "Now: %ld, lease id: %ld, lease_expired_time: %ld", now, lease_id_, lease_expired_time_);
      return (lease_id_ != common::INVALID_LEASE_ID && lease_expired_time_ > now);
    }

    bool NsRuntimeGlobalInformation::logout()
    {
      lease_id_ = common::INVALID_LEASE_ID;
      peer_status_ = NS_STATUS_UNINITIALIZE;
      lease_expired_time_ = 0;
      return true;
    }

    bool NsRuntimeGlobalInformation::renew(const int32_t step, const time_t now)
    {
      lease_expired_time_  = now + step;
      return true;
    }

    bool NsRuntimeGlobalInformation::renew(const int64_t lease_id, const int32_t step, const time_t now)
    {
      lease_id_ = lease_id;
      lease_expired_time_ = now + step;
      return true;
    }

    void NsRuntimeGlobalInformation::dump(const int32_t level, const char* file, const int32_t line,
            const char* function, const pthread_t thid, const char* format, ...)
    {
        char msgstr[256] = {'\0'};/** include '\0'*/
        va_list ap;
        va_start(ap, format);
        vsnprintf(msgstr, 256, NULL == format ? "" : format, ap);
        va_end(ap);
        TBSYS_LOGGER.logMessage(level, file, line, function, thid, "%s, owner_ip_port: %s, other_side_ip_port: %s,switch_time: %s, vip: %s \
          destroy: %s, owner_role: %s, other_side_role: %s, owner_status: %s, other_side_status: %s, leaes_id: %" PRI64_PREFIX "d, lease_expired_time: %" PRI64_PREFIX "d",
          msgstr, tbsys::CNetUtil::addrToString(owner_ip_port_).c_str(), tbsys::CNetUtil::addrToString(peer_ip_port_).c_str(),
          common::Func::time_to_str(switch_time_).c_str(), tbsys::CNetUtil::addrToString(vip_).c_str(),
          destroy_flag_ ? "yes" : "no", owner_role_
          == NS_ROLE_MASTER ? "master" : owner_role_ == NS_ROLE_SLAVE ? "slave" : "unknow", peer_role_
          == NS_ROLE_MASTER ? "master" : peer_role_ == NS_ROLE_SLAVE ? "slave" : "unknow",
          owner_status_ == NS_STATUS_UNINITIALIZE ? "uninitialize"
          : owner_status_ == NS_STATUS_INITIALIZED ? "initialize" : "unknow",
          peer_status_ == NS_STATUS_UNINITIALIZE ? "uninitialize"
          : peer_status_ == NS_STATUS_INITIALIZED ? "initialize" : "unknow",
          lease_id_, lease_expired_time_);
    }

    NsRuntimeGlobalInformation& NsRuntimeGlobalInformation::instance()
    {
      return instance_;
    }

    void print_int64(const common::ArrayHelper<uint64_t>& servers, std::string& result)
    {
      for (int64_t i = 0; i < servers.get_array_index(); ++i)
      {
        result += "/";
        result += tbsys::CNetUtil::addrToString(*servers.at(i));
      }
    }

    void print_int64(const common::ArrayHelper<ServerItem> &servers, std::stringstream& result)
    {
      for (int64_t i = 0; i < servers.get_array_index(); ++i)
      {
        ServerItem* iter = servers.at(i);
        result << "/" << tbsys::CNetUtil::addrToString(iter->server_) << " : " << iter->family_id_ << " : " << iter->version_;
      }
    }

    void print_int64(const std::vector<uint64_t>& servers, std::string& result)
    {
      std::vector<uint64_t>::const_iterator iter = servers.begin();
      for (; iter != servers.end(); ++iter)
      {
        result += "/";
        result += tbsys::CNetUtil::addrToString((*iter));
      }
    }

    void print_int64(const std::vector<ServerItem>& servers, std::stringstream& result)
    {
      std::vector<ServerItem>::const_iterator iter = servers.begin();
      for (; iter != servers.end(); ++iter)
      {
        result << "/" << tbsys::CNetUtil::addrToString(iter->server_) << " : " << iter->family_id_ << " : " << iter->version_;
      }
    }

    void print_lease(const common::ArrayHelper<common::BlockLease>& helper, std::stringstream& result)
    {
      if (helper.get_array_index() > 0)
        result << " size: " << helper.get_array_index() << " ";
      for (int64_t index = 0; index < helper.get_array_index(); ++index)
      {
        BlockLease* lease = helper.at(index);
        lease->dump(result);
      }
    }

    double calc_capacity_percentage(const uint64_t capacity, const uint64_t total_capacity)
    {
      double ret = PERCENTAGE_MIN;
      uint64_t unit = capacity > GB ? GB : MB;
      uint64_t tmp_capacity = capacity / unit;
      uint64_t tmp_total_capacity = total_capacity / unit;
      if (0 == tmp_total_capacity)
        ret = PERCENTAGE_MAX;
      if ((tmp_capacity != 0)
          && (tmp_total_capacity != 0))
      {
        ret = (double)tmp_capacity / (double)tmp_total_capacity;
      }
      return ret;
    }

    bool is_equal_group(const uint64_t id)
    {
      return (static_cast<int64_t>(id % common::SYSPARAM_NAMESERVER.group_count_)
          == common::SYSPARAM_NAMESERVER.group_seq_);
    }
  }/** nameserver **/
}/** tfs **/
