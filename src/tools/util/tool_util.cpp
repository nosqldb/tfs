/*
 * (C) 2007-2010 Alibaba Group Holding Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 * Version: $Id: ds_lib.cpp 413 2011-06-03 00:52:46Z daoan@taobao.com $
 *
 * Authors:
 *   nayan <nayan@taobao.com>
 *
 */

#include "common/client_manager.h"
#include "common/status_message.h"
#include "message/server_status_message.h"
#include "message/block_info_message.h"
#include "message/block_info_message_v2.h"
#include "message/file_info_message.h"
#include "message/read_data_message.h"
#include "message/family_info_message.h"

#include "tool_util.h"

using namespace tfs::common;
using namespace tfs::message;

namespace tfs
{
  namespace tools
  {
    int ToolUtil::get_block_ds_list(const uint64_t server_id, const uint32_t block_id, VUINT64& ds_list, const int32_t flag)
    {
      int ret = TFS_ERROR;
      if (0 == server_id)
      {
        TBSYS_LOG(ERROR, "server is is invalid: %"PRI64_PREFIX"u", server_id);
      }
      else
      {
        GetBlockInfoMessage gbi_message(flag);
        gbi_message.set_block_id(block_id);

        tbnet::Packet* rsp = NULL;
        NewClient* client = NewClientManager::get_instance().create_client();
        ret = send_msg_to_server(server_id, client, &gbi_message, rsp);

        if (rsp != NULL)
        {
          if (rsp->getPCode() == SET_BLOCK_INFO_MESSAGE)
          {
            ds_list = dynamic_cast<SetBlockInfoMessage*>(rsp)->get_block_ds();
          }
          else if (rsp->getPCode() == STATUS_MESSAGE)
          {
            ret = dynamic_cast<StatusMessage*>(rsp)->get_status();
            fprintf(stderr, "get block info fail, error: %s\n,", dynamic_cast<StatusMessage*>(rsp)->get_error());
            ret = dynamic_cast<StatusMessage*>(rsp)->get_status();
          }
        }
        else
        {
          fprintf(stderr, "get NULL response message, ret: %d\n", ret);
        }

        NewClientManager::get_instance().destroy_client(client);
      }

      return ret;
    }

    int ToolUtil::get_block_ds_list_v2(const uint64_t server_id, const uint64_t block_id, VUINT64& ds_list, const int32_t flag)
    {
      int ret = TFS_ERROR;
      if (0 == server_id)
      {
        TBSYS_LOG(ERROR, "server id is invalid: %"PRI64_PREFIX"u", server_id);
      }
      else
      {
        GetBlockInfoMessageV2 gbi_message;
        gbi_message.set_block_id(block_id);
        gbi_message.set_mode(flag);

        tbnet::Packet* rsp = NULL;
        NewClient* client = NewClientManager::get_instance().create_client();
        ret = send_msg_to_server(server_id, client, &gbi_message, rsp);

        if (rsp != NULL)
        {
          if (rsp->getPCode() == GET_BLOCK_INFO_RESP_MESSAGE_V2)
          {
            GetBlockInfoRespMessageV2* msg = dynamic_cast<GetBlockInfoRespMessageV2* >(rsp);
            BlockMeta& block_meta = msg->get_block_meta();
            for (int i = 0; i < block_meta.size_; i++)
            {
              ds_list.push_back(block_meta.ds_[i]);
            }
          }
          else if (rsp->getPCode() == STATUS_MESSAGE)
          {
            fprintf(stderr, "get block info fail, error: %s\n", dynamic_cast<StatusMessage*>(rsp)->get_error());
            ret = dynamic_cast<StatusMessage*>(rsp)->get_status();
          }
        }
        else
        {
          fprintf(stderr, "get NULL response message, ret: %d\n", ret);
        }

        NewClientManager::get_instance().destroy_client(client);
      }

      return ret;
    }

    // copy from DsLib::get_block_info
    int ToolUtil::get_block_info(const uint64_t ds_id, const uint64_t block_id, BlockInfoV2& block_info)
    {
      GetBlockInfoMessageV2 req_gbi_msg;
      req_gbi_msg.set_block_id(block_id);

      NewClient* client = NewClientManager::get_instance().create_client();
      tbnet::Packet* ret_msg = NULL;
      int ret = send_msg_to_server(ds_id, client, &req_gbi_msg, ret_msg);
      if (TFS_SUCCESS == ret)
      {
        if (UPDATE_BLOCK_INFO_MESSAGE_V2 == ret_msg->getPCode())
        {
          UpdateBlockInfoMessageV2 *req_ubi_msg = dynamic_cast<UpdateBlockInfoMessageV2*> (ret_msg);
          block_info = req_ubi_msg->get_block_info();
        }
        else if (STATUS_MESSAGE == ret_msg->getPCode())
        {
          StatusMessage* s_msg = dynamic_cast<StatusMessage*> (ret_msg);
          ret = s_msg->get_status();
          fprintf(stderr, "get block info from Data Server failure, %s\n", s_msg->get_error());
        }
        else
        {
          ret = EXIT_UNKNOWN_MSGTYPE;
        }
      }
      else
      {
        fprintf(stderr, "send message to Data Server failure\n");
      }
      NewClientManager::get_instance().destroy_client(client);
      return ret;
    }

    int ToolUtil::read_file_infos_v2(const uint64_t ns_id, const uint64_t block_id, std::vector<FileInfoV2>& finofs)
    {
      int ret = TFS_SUCCESS;
      VUINT64 ds_list;
      ret = get_block_ds_list_v2(ns_id, block_id, ds_list);
      if(TFS_SUCCESS == ret)
      {
        if(ds_list.size() > 0)
        {
          int index = random() % ds_list.size();
          uint64_t ds_server = ds_list.at(index);
          ret = list_file_v2(ds_server, block_id, block_id, finofs);
          if(TFS_SUCCESS != ret)
          {
             TBSYS_LOG(WARN, "get blockid: %"PRI64_PREFIX"u files list fail, ds_id:%s, ret:%d", block_id, tbsys::CNetUtil::addrToString(ds_server).c_str(), ret);
          }
        }
        else
        {
          ret = EXIT_TFS_ERROR;//有编组的block丢失,虽然可以继续退化读，但是效率太低，还是不要读了
          TBSYS_LOG(WARN, "unknown error, get block ds list success, but ds list is empty, only can degrade read, blockid: %"PRI64_PREFIX"u, ns_addr:%s", block_id, tbsys::CNetUtil::addrToString(ns_id).c_str());
        }
      }
      else
      {
        TBSYS_LOG(WARN, "get blockid: %"PRI64_PREFIX"u ds list fail, ns_addr:%s, ret:%d", block_id, tbsys::CNetUtil::addrToString(ns_id).c_str(), ret);
      }
      return ret;
    }

    int ToolUtil::read_file_info(const uint64_t server_id, const uint64_t block_id, const uint64_t file_id, const int32_t flag, FileInfo& info)
    {
      int ret = TFS_SUCCESS;
      if ((INVALID_SERVER_ID != server_id) &&
          (INVALID_BLOCK_ID != block_id) &&
          (INVALID_FILE_ID != file_id))
      {
        FileInfoMessage fi_msg;
        fi_msg.set_block_id(block_id);
        fi_msg.set_file_id(file_id);
        fi_msg.set_mode(flag);

        tbnet::Packet* rsp = NULL;
        NewClient* client = NewClientManager::get_instance().create_client();
        if (NULL != client)
        {
          ret = send_msg_to_server(server_id, client, &fi_msg, rsp);
          if (TFS_SUCCESS == ret)
          {
            if (rsp->getPCode() == RESP_FILE_INFO_MESSAGE)
            {
              RespFileInfoMessage* resp_msg = dynamic_cast<RespFileInfoMessage*>(rsp);
              info = *(resp_msg->get_file_info());
            }
            else if (rsp->getPCode() == STATUS_MESSAGE)
            {
              StatusMessage* sm = dynamic_cast<StatusMessage*>(rsp);
              TBSYS_LOG(WARN, "read file info fail. error: %s, ret: %d", sm->get_error(), sm->get_status());
            }
            else
            {
              ret = EXIT_UNKNOWN_MSGTYPE;
              TBSYS_LOG(WARN, "unknown message type, pcode: %d", rsp->getPCode());
            }
          }

          NewClientManager::get_instance().destroy_client(client);
        }
        else
        {
          ret = EXIT_CLIENT_MANAGER_CREATE_CLIENT_ERROR;
          TBSYS_LOG(WARN, "create new client error");
        }
      }
      else
      {
        ret = EXIT_PARAMETER_ERROR;
        TBSYS_LOG(WARN, "invalid parameter");
      }

      return ret;
    }

    int ToolUtil::read_file_data(const uint64_t server_id, const uint64_t block_id, const uint64_t file_id,
        const int32_t length, const int32_t offset, const int32_t flag, char* data, int32_t& read_len)
    {
      int ret = TFS_SUCCESS;
      if ((INVALID_SERVER_ID != server_id) &&
          (INVALID_BLOCK_ID != block_id) &&
          (INVALID_FILE_ID != file_id) &&
          (length > 0) &&
          (offset >= 0) &&
          (NULL != data))
      {
        ReadDataMessage rd_msg;
        rd_msg.set_block_id(block_id);
        rd_msg.set_file_id(file_id);
        rd_msg.set_length(length);
        rd_msg.set_offset(offset);
        rd_msg.set_flag(flag);

        tbnet::Packet* rsp = NULL;
        NewClient* client = NewClientManager::get_instance().create_client();
        if (NULL != client)
        {
          ret = send_msg_to_server(server_id, client, &rd_msg, rsp);
          if (TFS_SUCCESS == ret)
          {
            if (rsp->getPCode() == RESP_READ_DATA_MESSAGE)
            {
              RespReadDataMessage* resp_msg = dynamic_cast<RespReadDataMessage*>(rsp);
              read_len = resp_msg->get_length();
              if (read_len < 0)
              {
                ret = read_len;
              }
              else
              {
                memcpy(data, resp_msg->get_data(), read_len);
              }
            }
            else if (rsp->getPCode() == STATUS_MESSAGE)
            {
              StatusMessage* sm = dynamic_cast<StatusMessage*>(rsp);
              TBSYS_LOG(WARN, "read file data fail. error: %s, ret: %d", sm->get_error(), sm->get_status());
            }
            else
            {
              ret = EXIT_UNKNOWN_MSGTYPE;
              TBSYS_LOG(WARN, "unknown message type, pcode: %d", rsp->getPCode());
            }
          }

          NewClientManager::get_instance().destroy_client(client);
        }
        else
        {
          ret = EXIT_CLIENT_MANAGER_CREATE_CLIENT_ERROR;
          TBSYS_LOG(WARN, "create new client error");
        }
      }
      else
      {
        ret = EXIT_PARAMETER_ERROR;
        TBSYS_LOG(WARN, "invalid parameter");
      }

      return ret;
    }

    int ToolUtil::list_file(const uint64_t server_id, const uint64_t block_id,
         std::vector<FileInfo>& finfos)
    {
      int ret = TFS_SUCCESS;
      if ((INVALID_SERVER_ID == server_id) ||
          (INVALID_BLOCK_ID == block_id))
      {
        ret = EXIT_PARAMETER_ERROR;
        TBSYS_LOG(WARN, "invalid parameter");
      }

      if (TFS_SUCCESS == ret)
      {
        NewClient* client = NewClientManager::get_instance().create_client();
        if (NULL != client)
        {
          GetServerStatusMessage req_gss_msg;
          req_gss_msg.set_status_type(GSS_BLOCK_FILE_INFO);
          req_gss_msg.set_return_row(block_id);

          tbnet::Packet* ret_msg = NULL;
          ret = send_msg_to_server(server_id, client, &req_gss_msg, ret_msg);
          if (TFS_SUCCESS == ret)
          {
            if (BLOCK_FILE_INFO_MESSAGE == ret_msg->getPCode())
            {
              BlockFileInfoMessage* bmsg = dynamic_cast<BlockFileInfoMessage*>(ret_msg);
              finfos = bmsg->get_fileinfo_list();
            }
            else if (STATUS_MESSAGE == ret_msg->getPCode())
            {
              StatusMessage* smsg = dynamic_cast<StatusMessage*> (ret_msg);
              TBSYS_LOG(WARN, "list file. error: %s, ret: %d",
                  smsg->get_error(), smsg->get_status());
              ret = smsg->get_status();
            }
            else
            {
              TBSYS_LOG(WARN, "unknown message type, pcode: %d", ret_msg->getPCode());
              ret = EXIT_UNKNOWN_MSGTYPE;
            }
          }
        }
        else
        {
          TBSYS_LOG(WARN, "create new client error");
          ret = EXIT_CLIENT_MANAGER_CREATE_CLIENT_ERROR;
        }
      }

      return ret;
    }

    int ToolUtil::list_file_v2(const uint64_t server_id, const uint64_t block_id,
        const uint64_t attach_block_id, std::vector<FileInfoV2>& finfos)
    {
      int ret = TFS_SUCCESS;
      if ((INVALID_SERVER_ID == server_id) ||
          (INVALID_BLOCK_ID == block_id) ||
          (INVALID_BLOCK_ID == attach_block_id))
      {
        ret = EXIT_PARAMETER_ERROR;
        TBSYS_LOG(WARN, "invalid parameter");
      }

      if (TFS_SUCCESS == ret)
      {
        NewClient* client = NewClientManager::get_instance().create_client();
        if (NULL != client)
        {
          GetServerStatusMessage req_gss_msg;
          req_gss_msg.set_status_type(GSS_BLOCK_FILE_INFO_V2);
          req_gss_msg.set_return_row(block_id);
          req_gss_msg.set_from_row(attach_block_id);

          tbnet::Packet* ret_msg = NULL;
          ret = send_msg_to_server(server_id, client, &req_gss_msg, ret_msg);
          if (TFS_SUCCESS == ret)
          {
            if (BLOCK_FILE_INFO_MESSAGE_V2 == ret_msg->getPCode())
            {
              BlockFileInfoMessageV2* bmsg = dynamic_cast<BlockFileInfoMessageV2*>(ret_msg);
              finfos = *(bmsg->get_fileinfo_list());
            }
            else if (STATUS_MESSAGE == ret_msg->getPCode())
            {
              StatusMessage* smsg = dynamic_cast<StatusMessage*> (ret_msg);
              TBSYS_LOG(WARN, "list file. error: %s, ret: %d",
                  smsg->get_error(), smsg->get_status());
              ret = smsg->get_status();
            }
            else
            {
              TBSYS_LOG(WARN, "unknown message type, pcode: %d", ret_msg->getPCode());
              ret = EXIT_UNKNOWN_MSGTYPE;
            }
          }
        }
        else
        {
          TBSYS_LOG(WARN, "create new client error");
          ret = EXIT_CLIENT_MANAGER_CREATE_CLIENT_ERROR;
        }
      }

      return ret;
    }

    int ToolUtil::get_all_blocks_meta(const uint64_t ns_id, const VUINT64& blocks, std::vector<BlockMeta>& blocks_meta, const bool need_check_block)
    {
      int ret = TFS_SUCCESS;
      BatchGetBlockInfoMessageV2 bgbi_message;
      uint64_t* pblocks = bgbi_message.get_block_ids();
      bgbi_message.set_mode(T_READ);
      bgbi_message.set_flag(F_FAMILY_INFO);//需要获取到family info

      int32_t batch_index = 0;
      uint32_t index = 0;
      while (true)
      {
        while (index < blocks.size() && batch_index < MAX_BATCH_SIZE)
        {
          uint64_t block_id = blocks.at(index++);
          if (!IS_VERFIFY_BLOCK(block_id) || need_check_block)
          {
            pblocks[batch_index++] = block_id;
          }
          else
          {
            TBSYS_LOG(DEBUG, "skip verify block, blockid: %"PRI64_PREFIX"u,", block_id);
          }
        }

        if (0 == batch_index)
        {
          break;
        }
        bgbi_message.set_size(batch_index);
        batch_index = 0;

        tbnet::Packet* rsp = NULL;
        NewClient* client = NewClientManager::get_instance().create_client();
        ret = send_msg_to_server(ns_id, client, &bgbi_message, rsp);
        if (TFS_SUCCESS == ret)
        {
          if (rsp->getPCode() == BATCH_GET_BLOCK_INFO_RESP_MESSAGE_V2)
          {
            BatchGetBlockInfoRespMessageV2* bgbi_resp = dynamic_cast<BatchGetBlockInfoRespMessageV2*>(rsp);
            BlockMeta* pblocks_meta = bgbi_resp->get_block_metas();
            for (int32_t i = 0; i < bgbi_resp->get_size(); ++i)
            {
              blocks_meta.push_back(pblocks_meta[i]);// 如果block不存在或者ds_list为空则pblocks_meta[i].size为0
            }
          }
          else if (rsp->getPCode() == STATUS_MESSAGE)
          {
            StatusMessage* sm = dynamic_cast<StatusMessage*>(rsp);
            ret = sm->get_status();
            TBSYS_LOG(WARN, "batch get block info fail, error msg: %s, ret: %d", sm->get_error(), ret);
          }
          else
          {
            ret = EXIT_UNKNOWN_MSGTYPE;
            TBSYS_LOG(WARN, "batch get block info fail, unknown msg, pcode: %d", rsp->getPCode());
          }
        }
        else
        {
          TBSYS_LOG(WARN, "batch get block info fail, ret: %d", ret);
        }

        if (TFS_SUCCESS != ret || index >= blocks.size())
        {
          break;
        }
      }

      return ret;
    }

    int ToolUtil::get_family_info(const int64_t family_id, const uint64_t ns_id,
        std::vector<std::pair<uint64_t,uint64_t> >& family_members, int32_t& family_aid_info)
    {
      int ret = TFS_SUCCESS;
      std::pair<uint64_t,uint64_t>* members = NULL;
      family_members.clear();

      // get family info
      GetFamilyInfoMessage gfi_message;
      gfi_message.set_family_id(family_id);
      gfi_message.set_mode(T_READ);
      tbnet::Packet* rsp = NULL;
      NewClient* client = NewClientManager::get_instance().create_client();
      ret = send_msg_to_server(ns_id, client, &gfi_message, rsp);
      if (TFS_SUCCESS == ret)
      {
        if (rsp->getPCode() == RSP_GET_FAMILY_INFO_MESSAGE)
        {
          GetFamilyInfoResponseMessage* gfi_resp = dynamic_cast<GetFamilyInfoResponseMessage*>(rsp);
          family_aid_info = gfi_resp->get_family_aid_info();
          members = gfi_resp->get_members();
          for (int i = 0; i < MAX_MARSHALLING_NUM; ++i)
          {
            if (members[i].first != INVALID_BLOCK_ID)
            {
              family_members.push_back(members[i]);
            }
          }
        }
        else if (rsp->getPCode() == STATUS_MESSAGE)
        {
          StatusMessage* sm = dynamic_cast<StatusMessage*>(rsp);
          ret = sm->get_status();
          TBSYS_LOG(WARN, "get family info fail, familyid: %"PRI64_PREFIX"d, error msg: %s, ret: %d",
              family_id, sm->get_error(), ret);
        }
        else
        {
          ret = EXIT_UNKNOWN_MSGTYPE;
          TBSYS_LOG(WARN, "get family info fail, familyid: %"PRI64_PREFIX"d, unknown msg, pcode: %d",
              family_id, rsp->getPCode());
        }
      }
      else
      {
        TBSYS_LOG(WARN, "send GetFamilyInfoMessage fail, familyid: %"PRI64_PREFIX"d, ret: %d", family_id, ret);
      }
      NewClientManager::get_instance().destroy_client(client);

      return ret;
    }

    int ToolUtil::show_help(const STR_FUNC_MAP& cmd_map)
    {
      fprintf(stdout, "\nsupported command:");
      for (STR_FUNC_MAP_CONST_ITER it = cmd_map.begin(); it != cmd_map.end(); it++)
      {
        fprintf(stdout, "\n%-40s %s", it->second.syntax_, it->second.info_);
      }
      fprintf(stdout, "\n\n");

      return TFS_SUCCESS;
    }

    void ToolUtil::print_info(const int status, const char* fmt, ...)
    {
      va_list args;
      va_start(args, fmt);
      FILE* out = NULL;
      const char* str = NULL;

      if (TFS_SUCCESS == status)
      {
        out = stdout;
        str = "success";
      }
      else
      {
        out = stderr;
        str = "fail";
      }
      fprintf(out, fmt, va_arg(args, const char*));
      fprintf(out, " %s.\n", str);
      va_end(args);
    }

  }
}
