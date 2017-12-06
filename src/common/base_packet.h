/*
 * (C) 2007-2010 Alibaba Group Holding Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 * Version: $Id: base_packet.h 213 2011-04-22 16:22:51Z duanfei@taobao.com $
 *
 * Authors:
 *   duolong <duolong@taobao.com>
 *      - initial release
 *
 */
#ifndef TFS_COMMON_BASE_PACKET_H_
#define TFS_COMMON_BASE_PACKET_H_

#include <tbsys.h>
#include <tbnet.h>
#include <Memory.hpp>
#include "internal.h"
#include "func.h"
#include "stream.h"
#include "serialization.h"
#include "easy_helper.h"

namespace tfs
{
  namespace common
  {
      //old structure
#pragma pack(4)
    struct TfsPacketNewHeaderV0
    {
      uint32_t flag_;
      int32_t length_;
      int16_t type_;
      int16_t check_;
      int serialize(char*data, const int64_t data_len, int64_t& pos) const
      {
        int32_t iret = NULL != data && data_len - pos >= length() ? TFS_SUCCESS : TFS_ERROR;
        if (TFS_SUCCESS == iret)
        {
          iret = Serialization::set_int32(data, data_len, pos, flag_);
        }
        if (TFS_SUCCESS == iret)
        {
          iret = Serialization::set_int32(data, data_len, pos, length_);
        }
        if (TFS_SUCCESS == iret)
        {
          iret = Serialization::set_int16(data, data_len, pos, type_);
        }
        if (TFS_SUCCESS == iret)
        {
          iret = Serialization::set_int16(data, data_len, pos, check_);
        }
        return iret;
      }

      int deserialize(char*data, const int64_t data_len, int64_t& pos)
      {
        int32_t iret = NULL != data && data_len - pos >= length() ? TFS_SUCCESS : TFS_ERROR;
        if (TFS_SUCCESS == iret)
        {
          iret = Serialization::get_int32(data, data_len, pos, reinterpret_cast<int32_t*>(&flag_));
        }
        if (TFS_SUCCESS == iret)
        {
          iret = Serialization::get_int32(data, data_len, pos, &length_);
        }
        if (TFS_SUCCESS == iret)
        {
          iret = Serialization::get_int16(data, data_len, pos, &type_);
        }
        if (TFS_SUCCESS == iret)
        {
          iret = Serialization::get_int16(data, data_len, pos, &check_);
        }
        return iret;
      }

      int64_t length() const
      {
        return INT_SIZE * 3;
      }
    };

    //new structure
    struct TfsPacketNewHeaderV1
    {
      uint64_t id_;
      uint32_t flag_;
      uint32_t crc_;
      int32_t  length_;
      int16_t  type_;
      int16_t  version_;
      int serialize(char*data, const int64_t data_len, int64_t& pos) const
      {
        int32_t iret = NULL != data && data_len - pos >= length() ? TFS_SUCCESS : TFS_ERROR;
        if (TFS_SUCCESS == iret)
        {
          iret = Serialization::set_int32(data, data_len, pos, flag_);
        }
        if (TFS_SUCCESS == iret)
        {
          iret = Serialization::set_int32(data, data_len, pos, length_);
        }
        if (TFS_SUCCESS == iret)
        {
          iret = Serialization::set_int16(data, data_len, pos, type_);
        }
        if (TFS_SUCCESS == iret)
        {
          iret = Serialization::set_int16(data, data_len, pos, version_);
        }
        if (TFS_SUCCESS == iret)
        {
          iret = Serialization::set_int64(data, data_len, pos, id_);
        }
        if (TFS_SUCCESS == iret)
        {
          iret = Serialization::set_int32(data, data_len, pos, crc_);
        }
        return iret;
      }

      int deserialize(const char*data, const int64_t data_len, int64_t& pos)
      {
        int32_t iret = NULL != data && data_len - pos >= length() ? TFS_SUCCESS : TFS_ERROR;
        if (TFS_SUCCESS == iret)
        {
          iret = Serialization::get_int32(data, data_len, pos, reinterpret_cast<int32_t*>(&flag_));
        }
        if (TFS_SUCCESS == iret)
        {
          iret = Serialization::get_int32(data, data_len, pos, &length_);
        }
        if (TFS_SUCCESS == iret)
        {
          iret = Serialization::get_int16(data, data_len, pos, &type_);
        }
        if (TFS_SUCCESS == iret)
        {
          iret = Serialization::get_int16(data, data_len, pos, &version_);
        }
        /*if (TFS_SUCCESS == iret)
        {
          iret = Serialization::get_int64(data, data_len, pos, reinterpret_cast<int64_t*>(&id_));
        }
        if (TFS_SUCCESS == iret)
        {
          iret = Serialization::get_int32(data, data_len, pos, reinterpret_cast<int32_t*>(&crc_));
        }*/
        return iret;
      }

      int64_t length() const
      {
        return INT_SIZE * 4 + INT64_SIZE;
      }
    };
  #pragma pack()

    enum DirectionStatus
    {
      DIRECTION_RECEIVE = 1,
      DIRECTION_SEND = 2,
      DIRECTION_MASTER_SLAVE_NS = 4
    };

    enum HeartMessageStatus
    {
      HEART_MESSAGE_OK = 0,
      HEART_NEED_SEND_BLOCK_INFO = 1,
      HEART_EXP_BLOCK_ID = 2,
      HEART_MESSAGE_FAILED = 3,
      HEART_REPORT_BLOCK_SERVER_OBJECT_NOT_FOUND = 4,
      HEART_REPORT_UPDATE_RELATION_ERROR = 5
    };

    enum ReportBlockMessageStatus
    {
      REPORT_BLOCK_OK = 0,
      REPORT_BLOCK_SERVER_OBJECT_NOT_FOUND = 1,
      REPORT_BLOCK_UPDATE_RELATION_ERROR   = 2,
      REPORT_BLOCK_OTHER_ERROR = 3
    };

    enum TfsPacketVersion
    {
      TFS_PACKET_VERSION_V0 = 0,
      TFS_PACKET_VERSION_V1 = 1,
      TFS_PACKET_VERSION_V2 = 2
    };

    enum MessageType
    {
      STATUS_MESSAGE = 1,
      GET_BLOCK_INFO_MESSAGE = 2,
      SET_BLOCK_INFO_MESSAGE = 3,
      CARRY_BLOCK_MESSAGE = 4,
      SET_DATASERVER_MESSAGE = 5,
      UPDATE_BLOCK_INFO_MESSAGE = 6,
      READ_DATA_MESSAGE = 7,
      RESP_READ_DATA_MESSAGE = 8,
      WRITE_DATA_MESSAGE = 9,
      CLOSE_FILE_MESSAGE = 10,
      UNLINK_FILE_MESSAGE = 11,
      REPLICATE_BLOCK_MESSAGE = 12,
      COMPACT_BLOCK_MESSAGE = 13,
      GET_SERVER_STATUS_MESSAGE = 14,
      SHOW_SERVER_INFORMATION_MESSAGE = 15,
      SUSPECT_DATASERVER_MESSAGE = 16,
      FILE_INFO_MESSAGE = 17,
      RESP_FILE_INFO_MESSAGE = 18,
      RENAME_FILE_MESSAGE = 19,
      CLIENT_CMD_MESSAGE = 20,
      CREATE_FILENAME_MESSAGE = 21,
      RESP_CREATE_FILENAME_MESSAGE = 22,
      ROLLBACK_MESSAGE = 23,
      RESP_HEART_MESSAGE = 24,
      RESET_BLOCK_VERSION_MESSAGE = 25,
      BLOCK_FILE_INFO_MESSAGE = 26,
      LEGACY_UNIQUE_FILE_MESSAGE = 27,
      LEGACY_RETRY_COMMAND_MESSAGE = 28,
      NEW_BLOCK_MESSAGE = 29,
      REMOVE_BLOCK_MESSAGE = 30,
      LIST_BLOCK_MESSAGE = 31,
      RESP_LIST_BLOCK_MESSAGE = 32,
      BLOCK_WRITE_COMPLETE_MESSAGE = 33,
      BLOCK_RAW_META_MESSAGE = 34,
      WRITE_RAW_DATA_MESSAGE = 35,
      WRITE_INFO_BATCH_MESSAGE = 36,
      BLOCK_COMPACT_COMPLETE_MESSAGE = 37,
      READ_DATA_MESSAGE_V2 = 38,
      RESP_READ_DATA_MESSAGE_V2 = 39,
      LIST_BITMAP_MESSAGE =40,
      RESP_LIST_BITMAP_MESSAGE = 41,
      RELOAD_CONFIG_MESSAGE = 42,
      SERVER_META_INFO_MESSAGE = 43,
      RESP_SERVER_META_INFO_MESSAGE = 44,
      READ_RAW_DATA_MESSAGE = 45,
      RESP_READ_RAW_DATA_MESSAGE = 46,
      REPLICATE_INFO_MESSAGE = 47,
      ACCESS_STAT_INFO_MESSAGE = 48,
      READ_SCALE_IMAGE_MESSAGE = 49,
      OPLOG_SYNC_MESSAGE = 50,
      OPLOG_SYNC_RESPONSE_MESSAGE = 51,
      MASTER_AND_SLAVE_HEART_MESSAGE = 52,
      MASTER_AND_SLAVE_HEART_RESPONSE_MESSAGE = 53,
      HEARTBEAT_AND_NS_HEART_MESSAGE = 54,
      //OWNER_CHECK_MESSAGE = 55,
      GET_BLOCK_LIST_MESSAGE = 56,
      CRC_ERROR_MESSAGE = 57,
      ADMIN_CMD_MESSAGE = 58,
      BATCH_GET_BLOCK_INFO_MESSAGE = 59,
      BATCH_SET_BLOCK_INFO_MESSAGE = 60,
      REMOVE_BLOCK_RESPONSE_MESSAGE = 61,
      READ_DATA_MESSAGE_V3 = 62,
      RESP_READ_DATA_MESSAGE_V3 = 63,
      DUMP_PLAN_MESSAGE = 64,
      DUMP_PLAN_RESPONSE_MESSAGE = 65,
      REQ_RC_LOGIN_MESSAGE = 66,
      RSP_RC_LOGIN_MESSAGE = 67,
      REQ_RC_KEEPALIVE_MESSAGE = 68,
      RSP_RC_KEEPALIVE_MESSAGE = 69,
      REQ_RC_LOGOUT_MESSAGE = 70,
      REQ_RC_RELOAD_MESSAGE = 71,
      GET_DATASERVER_INFORMATION_MESSAGE = 72,
      GET_DATASERVER_INFORMATION_RESPONSE_MESSAGE = 73,
      FILEPATH_ACTION_MESSAGE = 74,
      WRITE_FILEPATH_MESSAGE = 75,
      READ_FILEPATH_MESSAGE = 76,
      RESP_READ_FILEPATH_MESSAGE = 77,
      LS_FILEPATH_MESSAGE = 78,
      RESP_LS_FILEPATH_MESSAGE = 79,
      REQ_RT_UPDATE_TABLE_MESSAGE = 80,
      RSP_RT_UPDATE_TABLE_MESSAGE = 81,
      REQ_RT_MS_KEEPALIVE_MESSAGE = 82,
      RSP_RT_MS_KEEPALIVE_MESSAGE = 83,
      REQ_RT_GET_TABLE_MESSAGE = 84,
      RSP_RT_GET_TABLE_MESSAGE = 85,
      REQ_RT_RS_KEEPALIVE_MESSAGE = 86,
      RSP_RT_RS_KEEPALIVE_MESSAGE = 87,
      REQ_CALL_DS_REPORT_BLOCK_MESSAGE = 88,
      REQ_REPORT_BLOCKS_TO_NS_MESSAGE  = 89,
      RSP_REPORT_BLOCKS_TO_NS_MESSAGE  = 90,
      REQ_EC_MARSHALLING_MESSAGE = 91,
      REQ_EC_MARSHALLING_COMMIT_MESSAGE = 92,
      REQ_EC_REINSTATE_MESSAGE = 93,
      REQ_EC_REINSTATE_COMMIT_MESSAGE = 94,
      REQ_EC_DISSOLVE_MESSAGE = 95,
      REQ_EC_DISSOLVE_COMMIT_MESSAGE = 96,
      READ_RAW_INDEX_MESSAGE = 97,
      RSP_READ_RAW_INDEX_MESSAGE = 98,
      WRITE_RAW_INDEX_MESSAGE = 99,
      DS_COMPACT_BLOCK_MESSAGE = 100,
      DS_REPLICATE_BLOCK_MESSAGE = 101,
      RESP_DS_REPLICATE_BLOCK_MESSAGE = 102,
      RESP_DS_COMPACT_BLOCK_MESSAGE = 103,
      RSP_WRITE_DATA_MESSAGE = 104,
      RSP_UNLINK_FILE_MESSAGE = 105,
      REQ_RESOLVE_BLOCK_VERSION_CONFLICT_MESSAGE = 106,
      RSP_RESOLVE_BLOCK_VERSION_CONFLICT_MESSAGE = 107,
      REQ_GET_FAMILY_INFO_MESSAGE = 108,
      RSP_GET_FAMILY_INFO_MESSAGE = 109,
      DEGRADE_READ_DATA_MESSAGE = 110,
      REQ_CHECK_BLOCK_MESSAGE = 200,
      RSP_CHECK_BLOCK_MESSAGE = 201,
      GET_BLOCK_INFO_MESSAGE_V2 = 202,
      GET_BLOCK_INFO_RESP_MESSAGE_V2 = 203,
      BATCH_GET_BLOCK_INFO_MESSAGE_V2 = 204,
      BATCH_GET_BLOCK_INFO_RESP_MESSAGE_V2 = 205,
      WRITE_FILE_MESSAGE_V2 = 206,
      WRITE_FILE_RESP_MESSAGE_V2 = 207,
      SLAVE_DS_RESP_MESSAGE = 208,
      CLOSE_FILE_MESSAGE_V2 = 209,
      UPDATE_BLOCK_INFO_MESSAGE_V2 = 210,
      REPAIR_BLOCK_MESSAGE_V2 = 211,
      STAT_FILE_MESSAGE_V2 = 212,
      STAT_FILE_RESP_MESSAGE_V2 = 213,
      READ_FILE_MESSAGE_V2 = 214,
      READ_FILE_RESP_MESSAGE_V2 = 215,
      UNLINK_FILE_MESSAGE_V2 = 216,
      NEW_BLOCK_MESSAGE_V2 = 217,
      REMOVE_BLOCK_MESSAGE_V2 = 218,
      READ_RAWDATA_MESSAGE_V2 = 219,
      READ_RAWDATA_RESP_MESSAGE_V2 = 220,
      WRITE_RAWDATA_MESSAGE_V2 = 221,
      READ_INDEX_MESSAGE_V2 = 222,
      READ_INDEX_RESP_MESSAGE_V2 = 223,
      WRITE_INDEX_MESSAGE_V2 = 224,
      QUERY_EC_META_MESSAGE = 225,
      QUERY_EC_META_RESP_MESSAGE = 226,
      COMMIT_EC_META_MESSAGE = 227,
      BLOCK_FILE_INFO_MESSAGE_V2 = 228,
      REPORT_CHECK_BLOCK_MESSAGE = 229,
      REPORT_CHECK_BLOCK_RESPONSE_MESSAGE = 230,
      GET_BLOCK_STATISTIC_VISIT_INFO_MESSAGE = 231,
      DS_APPLY_LEASE_MESSAGE = 232,
      DS_APPLY_LEASE_RESPONSE_MESSAGE = 233,
      DS_RENEW_LEASE_MESSAGE = 234,
      DS_RENEW_LEASE_RESPONSE_MESSAGE = 235,
      DS_GIVEUP_LEASE_MESSAGE= 236,
      DS_GIVEUP_LEASE_RESPONSE_MESSAGE = 237,
      DS_APPLY_BLOCK_MESSAGE = 238,
      DS_APPLY_BLOCK_RESPONSE_MESSAGE = 239,
      DS_APPLY_BLOCK_FOR_UPDATE_MESSAGE = 240,
      DS_APPLY_BLOCK_FOR_UPDATE_RESPONSE_MESSAGE = 241,
      DS_GIVEUP_BLOCK_MESSAGE = 242,
      DS_GIVEUP_BLOCK_RESPONSE_MESSAGE = 243,
      NS_REQ_RESOLVE_BLOCK_VERSION_CONFLICT_MESSAGE = 244,
      NS_RSP_RESOLVE_BLOCK_VERSION_CONFLICT_MESSAGE = 245,
      GET_ALL_BLOCKS_HEADER_MESSAGE = 246,
      GET_ALL_BLOCKS_HEADER_RESP_MESSAGE = 247,
      REQ_SYNC_FILE_ENTRY_MESSAGE   = 248,
      RSP_SYNC_FILE_ENTRY_MESSAGE   = 249,
      DS_STAT_INFO_MESSAGE = 250,
//      RESP_DS_STAT_INFO_MESSAGE = 251,
      CLIENT_NS_KEEPALIVE_MESSAGE = 252,
      CLIENT_NS_KEEPALIVE_RESPONSE_MESSAGE = 253,
      NS_CLEAR_FAMILYINFO_MESSAGE = 254,
      DS_RENEW_BLOCK_MESSAGE = 255,
      DS_RENEW_BLOCK_RESPONSE_MESSAGE = 256,

      REQ_KVMETA_GET_OBJECT_MESSAGE = 300,
      RSP_KVMETA_GET_OBJECT_MESSAGE = 301,
      REQ_KVMETA_PUT_OBJECT_MESSAGE = 302,
      REQ_KVMETA_DEL_OBJECT_MESSAGE = 303,
      RSP_KVMETA_DEL_OBJECT_MESSAGE = 304,
      REQ_KVMETA_HEAD_OBJECT_MESSAGE = 305,
      RSP_KVMETA_HEAD_OBJECT_MESSAGE = 306,

      REQ_KVMETA_PUT_BUCKET_MESSAGE = 307,
      REQ_KVMETA_GET_BUCKET_MESSAGE = 308,
      RSP_KVMETA_GET_BUCKET_MESSAGE = 309,
      REQ_KVMETA_DEL_BUCKET_MESSAGE = 310,
      REQ_KVMETA_HEAD_BUCKET_MESSAGE = 311,
      RSP_KVMETA_HEAD_BUCKET_MESSAGE = 312,

      REQ_KVMETA_GET_SERVICE_MESSAGE = 313,
      RSP_KVMETA_GET_SERVICE_MESSAGE = 314,

      REQ_KVMETA_PUT_BUCKET_ACL_MESSAGE = 319,
      REQ_KVMETA_GET_BUCKET_ACL_MESSAGE = 320,
      RSP_KVMETA_GET_BUCKET_ACL_MESSAGE = 321,

      REQ_KV_RT_MS_KEEPALIVE_MESSAGE = 350,
      RSP_KV_RT_MS_KEEPALIVE_MESSAGE = 351,
      REQ_KV_RT_GET_TABLE_MESSAGE = 352,
      RSP_KV_RT_GET_TABLE_MESSAGE = 353,

      /* 360:MULTIPART */
      /* 370:AUTHORIZE */
      REQ_EXPIRE_CLEAN_TASK_MESSAGE = 380,
      REQ_RT_FINISH_TASK_MESSAGE = 381,
      REQ_KVMETA_SET_LIFE_CYCLE_MESSAGE = 382,
      REQ_KVMETA_GET_LIFE_CYCLE_MESSAGE = 383,
      RSP_KVMETA_GET_LIFE_CYCLE_MESSAGE = 384,
      REQ_KVMETA_RM_LIFE_CYCLE_MESSAGE = 385,

      REQ_RT_ES_KEEPALIVE_MESSAGE = 390,
      RSP_RT_ES_KEEPALIVE_MESSAGE = 391,
      REQ_QUERY_TASK_MESSAGE = 392,
      RSP_QUERY_TASK_MESSAGE = 393,

      REQ_RC_REQ_STAT_MESSAGE = 394,
      RSP_RC_REQ_STAT_MESSAGE = 395,

      REQ_RC_KEEPALIVE_BY_MONITOR_KEY_CLIENT = 396,
      RSP_RC_KEEPALIVE_BY_MONITOR_KEY_CLIENT = 397,

      REQ_KVMETA_PUT_OBJECT_USER_METADATA_MESSAGE = 400,
      REQ_KVMETA_DEL_OBJECT_USER_METADATA_MESSAGE = 403,

      LOCAL_PACKET = 500
    };

    // StatusMessage status value
    enum StatusMessageStatus
    {
      STATUS_MESSAGE_OK = 0,
      STATUS_MESSAGE_ERROR,
      STATUS_NEED_SEND_BLOCK_INFO,
      STATUS_MESSAGE_PING,
      STATUS_MESSAGE_REMOVE,
      STATUS_MESSAGE_BLOCK_FULL,
      STATUS_MESSAGE_ACCESS_DENIED
    };

		static const uint32_t TFS_PACKET_FLAG_V0 = 0x4d534654;//TFSM
		static const uint32_t TFS_PACKET_FLAG_V1 = 0x4e534654;//TFSN(V1)
		static const int32_t TFS_PACKET_HEADER_V0_SIZE = sizeof(TfsPacketNewHeaderV0);
		static const int32_t TFS_PACKET_HEADER_V1_SIZE = sizeof(TfsPacketNewHeaderV1);
		static const int32_t TFS_PACKET_HEADER_DIFF_SIZE = TFS_PACKET_HEADER_V1_SIZE - TFS_PACKET_HEADER_V0_SIZE;

    class BasePacket: public tbnet::Packet
    {
    public:
      typedef BasePacket* (*create_packet_handler)(int32_t);
      typedef std::map<int32_t, create_packet_handler> CREATE_PACKET_MAP;
      typedef CREATE_PACKET_MAP::iterator CREATE_PACKET_MAP_ITER;
    public:
      BasePacket();
      virtual ~BasePacket();
      virtual bool copy(BasePacket* src, const int32_t version, const bool deserialize);
      virtual bool encode(tbnet::DataBuffer* output);
      virtual bool decode(tbnet::DataBuffer* input, tbnet::PacketHeader* header);

      virtual int serialize(Stream& output) const = 0;
      virtual int deserialize(Stream& input) = 0;
      virtual int deserialize(const char* data, const int64_t data_len, int64_t& pos);
      virtual int64_t length() const = 0;
      int64_t get_data_length() const { return stream_.get_data_length();}
      virtual int reply(BasePacket* packet);
      int reply_error_packet(const int32_t level, const char* file, const int32_t line,
               const char* function, pthread_t thid, const int32_t error_code, const char* fmt, ...);
      virtual void dump() const {}

      inline bool is_enable_dump() const { return dump_flag_;}
      inline void enable_dump() { dump_flag_ = true;}
      inline void disable_dump() { dump_flag_ = false;}
      //inline void set_auto_free(const bool auto_free = true) { auto_free_ = auto_free;}
      //inline bool get_auto_free() const { return auto_free_;}
      //virtual void free() { if (auto_free_) { delete this;} }
      virtual void free() { delete this;}
      inline void set_connection(tbnet::Connection* connection) { connection_ = connection;}
      inline tbnet::Connection* get_connection() const { return connection_;}
      inline void set_direction(const DirectionStatus direction) { direction_ = direction; }
      inline DirectionStatus get_direction() const { return direction_;}
      inline void set_version(const int32_t version) { version_ = version;}
      inline int32_t get_version() const { return version_;}
      inline void set_crc(const uint32_t crc) { crc_ = crc;}
      inline uint32_t get_crc() const { return crc_;}
      inline void set_id(const uint64_t id) { id_ = id;}
      inline uint64_t get_id() const { return id_;}
      //inline Stream& get_stream() {return stream_;}

      static bool parse_special_ds(std::vector<uint64_t>& value, int32_t& version, uint32_t& lease);

      // libeasy
      void set_request(easy_request_t* request)
      {
        request_ = request;
      }
      easy_request_t* get_request()
      {
        return request_;
      }
      uint64_t getPeerId()
      {
        uint64_t id = 0;
        if (NULL != request_)
        {
          id = EasyHelper::convert_addr(request_->ms->c->addr);
        }
        return id;
      }

      public:
        Stream stream_;

  #ifdef TFS_GTEST
    public:
  #else
    protected:
  #endif
      // Stream stream_;
      tbnet::Connection* connection_;
      uint64_t id_;
      uint32_t crc_;
      DirectionStatus direction_;
      int32_t version_;
      static const int16_t MAX_ERROR_MSG_LENGTH = 511; /** not include '\0'*/
      //bool auto_free_;
      bool dump_flag_;
      easy_request_t* request_;
    };
  } /** common **/
}/** tfs **/
#endif //TFS_COMMON_BASE_PACKET_H_
