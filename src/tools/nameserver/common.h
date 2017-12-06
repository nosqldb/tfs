/*
 * (C) 2007-2010 Alibaba Group Holding Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 * Version: $Id: common.h 445 2011-06-08 09:27:48Z nayan@taobao.com $
 *
 * Authors:
 *   chuyu <chuyu@taobao.com>
 *      - initial release
 *
 */
#ifndef TFS_TOOLS_COMMON_H_
#define TFS_TOOLS_COMMON_H_

#include <vector>
#include <tbnet.h>
#include <Handle.h>
#include "common/client_manager.h"
#include "message/message_factory.h"
#include "common/config_item.h"
#include "common/internal.h"
#include "nameserver/ns_define.h"

namespace tfs
{
  namespace tools
  {
    static const int32_t MAX_READ_NUM = 0x400;
    static const int32_t MAX_COUNT = 10;

    enum ComType
    {
      SERVER_TYPE = 1,
      BLOCK_TYPE = 2,
      MACHINE_TYPE = 4,
      BLOCK_DISTRIBUTION_TYPE = 8,
      RACK_BLOCK_TYPE = 16,
      FAMILY_TYPE = 32,
      CHECK_BLOCK_TYPE = 64,
    };

    enum ComCmd
    {
      CMD_NOP = 0,
      CMD_UNKNOWN,
      CMD_NUM = 2,
      CMD_BLOCK_INFO,
      CMD_BLOCK_ID,//CMD_BLOCK_ID, CMD_FAMILY_ID
      CMD_BLOCK_STATUS,
      CMD_BLOCK_FULL,
      CMD_SERVER_LIST,
      CMD_SERVER_INFO,
      CMD_SERVER_ID,
      CMD_BLOCK_LIST,
      CMD_BLOCK_WRITABLE,
      CMD_BLOCK_MASTER,
      CMD_ALL,
      CMD_PART,
      CMD_FOR_MONITOR,
      CMD_NEED_FAMILY,
      CMD_COUNT,
      CMD_INTERVAL,
      CMD_REDIRECT,
      CMD_IP_ID,
      CMD_IP_MASK_ID,
      CMD_IP_GROUP_ID
    };
    enum SubCmdBlockType
    {
      BLOCK_TYPE_BLOCK_INFO = 1,
      BLOCK_TYPE_BLOCK_ID = 2,
      BLOCK_TYPE_SERVER_LIST = 4,
      BLOCK_TYPE_BLOCK_STATUS = 8,
      BLOCK_TYPE_BLOCK_FULL = 16,
    };
    enum SubServerType
    {
      SERVER_TYPE_SERVER_INFO = 1,
      SERVER_TYPE_BLOCK_LIST = 2,
      SERVER_TYPE_BLOCK_WRITABLE = 4,
      SERVER_TYPE_BLOCK_MASTER = 8
    };
    enum SubCmdMachine
    {
      MACHINE_TYPE_ALL = 1,
      MACHINE_TYPE_PART = 2,
      MACHINE_TYPE_FOR_MONITOR = 4
    };
    enum SubCmdBlockDistribution
    {
      BLOCK_IP_DISTRIBUTION_TYPE = 1,
      BLOCK_RACK_DISTRIBUTION_TYPE = 2
    };

    enum SubCmdRackBlockType//RACK_BLOCK_TYPE
    {
      RACK_BLOCK_TYPE_RACK_LIST = 1,
      RACK_BLOCK_TYPE_BLOCK_LIST = 2
    };

    enum CmpBlockType
    {
      BLOCK_CMP_ALL_INFO = 1,
      BLOCK_CMP_PART_INFO = 2,
      BLOCK_CMP_SERVER = 4
    };
    enum PushType
    {
      PUSH_MORE = 0,
      PUSH_LESS
    };

    enum TpMode
    {
      SUB_OP = -1,
      ADD_OP = 1
    };
    struct CmdInfo
    {
      public:
        CmdInfo() :
          cmd_(0), has_value_(false)
        {}
        CmdInfo(int32_t cmd, bool has_value) :
          cmd_(cmd), has_value_(has_value)
        {}
        ~CmdInfo(){}

        int32_t cmd_;
        bool has_value_;
    };
    struct ParamInfo
    {
      ParamInfo() :
        type_(CMD_NOP), num_(MAX_READ_NUM), count_(1), interval_(2), filename_(""),
        need_family_(false), id_(0), server_ip_port_(""), rack_ip_mask_("")
      {}
      ParamInfo(const int8_t type) :
        type_(type), num_(MAX_READ_NUM), count_(1), interval_(2), filename_(""),
        need_family_(false), id_(0), server_ip_port_(""), rack_ip_mask_("")
      {}
      ~ParamInfo(){}
      int8_t type_;
      uint64_t num_;
      uint64_t count_;
      uint64_t interval_;
      std::string filename_;
      bool need_family_;
      union
      {
        uint64_t block_id_;
        int64_t family_id_;
        int64_t id_;
      };
      std::string server_ip_port_;
      std::string rack_ip_mask_;
    };

    class ServerBase
    {
      public:
        ServerBase();
        virtual ~ServerBase();
        int serialize(tbnet::DataBuffer& output, int32_t& length);
        int deserialize(tbnet::DataBuffer& input, const int32_t length, int32_t& offset, const int8_t type = 0);
        int fetch_family_set();
        void dump() const;

#ifdef TFS_NS_DEBUG
        int64_t total_elect_num_;
#endif
        common::ServerStat server_stat_;
        std::set<uint64_t> hold_;
        std::set<uint64_t> writable_;
        std::set<uint64_t> master_;
        std::set<int64_t> family_set_;
    };

    struct ServerInfo
    {
      uint64_t server_id_;
      int64_t  family_id_;
      int32_t  version_;
      operator uint64_t() const {return server_id_;}
      ServerInfo operator=(const ServerInfo& a)
      {
        server_id_ = a.server_id_;
        return *this;
      }
      bool operator==(const ServerInfo& b) const
      {
        return server_id_ == b.server_id_;
      }
      bool operator<<(std::ostream& os) const
      {
        return os << server_id_;
      }
    };

    class BlockBase
    {
      public:
        common::BlockInfoV2 info_;
        std::vector<ServerInfo> server_list_;
        int64_t expire_time_;
        int64_t last_leave_time_;
        int8_t create_flag_;
        int8_t in_replicate_queue_;
        int8_t has_lease_;
        int8_t choose_master_;

        BlockBase();
        virtual ~BlockBase();
        bool operator<(const BlockBase& b) const
        {
          return info_.block_id_ < b.info_.block_id_;
        }

        int32_t deserialize(tbnet::DataBuffer& input, const int32_t length, int32_t& offset, const int8_t type);
        void dump() const;
    };

    static inline uint64_t get_addr(const std::string& ns_ip_port)
    {
      std::string::size_type pos = ns_ip_port.find_first_of(":");
      if (pos == std::string::npos)
      {
        return common::TFS_ERROR;
      }
      std::string tmp = ns_ip_port.substr(0, pos);
      return common::Func::str_to_addr(tmp.c_str(), atoi(ns_ip_port.substr(pos + 1).c_str()));
    }
    static inline int get_addr(const std::string& ns_ip_port, int64_t& ip, int64_t& port)
    {
      int ret = common::TFS_ERROR;
      std::string::size_type pos = ns_ip_port.find_first_of(":");
      if (pos != std::string::npos)
      {
        ip = tbsys::CNetUtil::getAddr(ns_ip_port.substr(0, pos).c_str());
        port = atoi(ns_ip_port.substr(pos + 1).c_str());
        if ((ip > 0) && (port > 0))
        {
          ret = common::TFS_SUCCESS;
        }
      }
      return ret;
    }
  }
}

#endif
