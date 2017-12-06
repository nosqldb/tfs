/*
 * (C) 2007-2010 Alibaba Group Holding Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 * Version: $Id: kvrootserver.cpp  $
 *
 * Authors:
 *   qixiao <qixiao.zs@alibaba-inc.com>
 *      - initial release
 */

#include "exp_root_server.h"

#include <Service.h>
#include <Memory.hpp>
#include <iterator>
#include "common/error_msg.h"
#include "common/config_item.h"
#include "common/parameter.h"
#include "common/local_packet.h"
#include "common/directory_op.h"
#include "common/status_message.h"
#include "common/client_manager.h"
#include "message/kv_rts_message.h"
#include "common/mysql_cluster/mysql_engine_helper.h"

using namespace tfs::common;
using namespace tfs::message;

namespace tfs
{
  namespace exprootserver
  {
    ExpRootServer::ExpRootServer():
      rt_es_heartbeat_handler_(*this),
      manager_(handle_task_helper_),
      handle_task_helper_(manager_)
    {

    }

    ExpRootServer::~ExpRootServer()
    {

    }

    int ExpRootServer::initialize(int /*argc*/, char* /*argv*/[])
    {
      int32_t iret =  SYSPARAM_EXPIREROOTSERVER.initialize(config_file_.c_str());
      if (TFS_SUCCESS != iret)
      {
        TBSYS_LOG(ERROR, "%s", "initialize exprootserver parameter error, must be exit");
        iret = EXIT_GENERAL_ERROR;
      }

      //initialize expserver manager
      if (TFS_SUCCESS == iret)
      {
				iret = manager_.initialize();
				if (TFS_SUCCESS != iret)
        {
          TBSYS_LOG(ERROR, "exp server manager initialize error, iret: %d, must be exit", iret);
        }
      }

      //initialize expserver ==> exprootserver heartbeat
      if (TFS_SUCCESS == iret)
      {
        int32_t heart_thread_count = TBSYS_CONFIG.getInt(CONF_SN_EXPIREROOTSERVER, CONF_HEART_THREAD_COUNT, 1);
        rt_es_heartbeat_workers_.setThreadParameter(heart_thread_count, &rt_es_heartbeat_handler_, this);
        rt_es_heartbeat_workers_.start();
      }

      if (TFS_SUCCESS == iret)
      {
        iret = handle_task_helper_.init();
        if (TFS_SUCCESS != iret)
        {
          TBSYS_LOG(ERROR, "query task helper initial fail: %d", iret);
        }
      }

      return iret;
    }

    int ExpRootServer::destroy_service()
    {
      rt_es_heartbeat_workers_.stop();
      rt_es_heartbeat_workers_.wait();
      manager_.destroy();
      handle_task_helper_.destroy();
      return TFS_SUCCESS;
    }

    /** handle single packet */
    tbnet::IPacketHandler::HPRetCode ExpRootServer::handlePacket(tbnet::Connection *connection, tbnet::Packet *packet)
    {
      tbnet::IPacketHandler::HPRetCode hret = tbnet::IPacketHandler::FREE_CHANNEL;
      bool bret = NULL != connection && NULL != packet;
      if (bret)
      {
        TBSYS_LOG(DEBUG, "receive pcode : %d", packet->getPCode());
        if (!packet->isRegularPacket())
        {
          bret = false;
          TBSYS_LOG(WARN, "control packet, pcode: %d", dynamic_cast<tbnet::ControlPacket*>(packet)->getCommand());
        }
        if (bret)
        {
          BasePacket* bpacket = dynamic_cast<BasePacket*>(packet);
          bpacket->set_connection(connection);
          bpacket->setExpireTime(MAX_RESPONSE_TIME);
          bpacket->set_direction(static_cast<DirectionStatus>(bpacket->get_direction()|DIRECTION_RECEIVE));

          if (bpacket->is_enable_dump())
          {
            bpacket->dump();
          }
          int32_t pcode = bpacket->getPCode();
          int32_t iret = common::TFS_SUCCESS;

          if (common::TFS_SUCCESS == iret)
          {
            hret = tbnet::IPacketHandler::KEEP_CHANNEL;
            switch (pcode)
            {
            case REQ_RT_ES_KEEPALIVE_MESSAGE:
              rt_es_heartbeat_workers_.push(bpacket, 0/* no limit */, false/* no block */);
              break;
            default:
              if (!main_workers_.push(bpacket, work_queue_size_))
              {
                bpacket->reply_error_packet(TBSYS_LOG_LEVEL(ERROR),STATUS_MESSAGE_ERROR, "%s, task message beyond max queue size, discard", get_ip_addr());
                bpacket->free();
              }
              break;
            }
          }
          else
          {
            bpacket->free();
            TBSYS_LOG(WARN, "the msg: %d will be ignored", pcode);
          }
        }
      }
      return hret;
    }

    /** handle packet*/
    bool ExpRootServer::handlePacketQueue(tbnet::Packet *packet, void *args)
    {
      bool bret = BaseService::handlePacketQueue(packet, args);
      if (bret)
      {
        int32_t pcode = packet->getPCode();
        int32_t iret = LOCAL_PACKET == pcode ? TFS_ERROR : common::TFS_SUCCESS;
        if (TFS_SUCCESS == iret)
        {
          TBSYS_LOG(DEBUG, "PCODE: %d", pcode);
          common::BasePacket* msg = dynamic_cast<common::BasePacket*>(packet);
          switch (pcode)
          {
            case REQ_RT_FINISH_TASK_MESSAGE:
              iret = handle_finish_task(dynamic_cast<ReqFinishTaskFromEsMessage*>(msg));
              break;
            case REQ_QUERY_TASK_MESSAGE:
              iret = query_task(dynamic_cast<ReqQueryTaskMessage*>(msg));
              break;
            default:
              iret = EXIT_UNKNOWN_MSGTYPE;
              TBSYS_LOG(ERROR, "unknown msg type: %d", pcode);
              break;
          }
          if (common::TFS_SUCCESS != iret)
          {
            msg->reply_error_packet(TBSYS_LOG_LEVEL(ERROR), iret, "execute message failed, pcode: %d", pcode);
          }
        }
      }
      return bret;
    }

		bool ExpRootServer::KeepAliveIPacketQueueHandlerHelper::handlePacketQueue(tbnet::Packet *packet, void *args)
    {
      UNUSED(args);
      bool bret = packet != NULL;
      if (bret)
      {
        //if return TFS_SUCCESS, packet had been delete in this func
        //if handlePacketQueue return true, tbnet will delete this packet
        assert(packet->getPCode() == REQ_RT_ES_KEEPALIVE_MESSAGE);
        manager_.rt_es_keepalive(dynamic_cast<BasePacket*>(packet));
      }
      return bret;
    }

    // libeasy handle packet
    int ExpRootServer::handle(common::BasePacket* packet)
    {
      assert(NULL != packet);
      int ret = TFS_SUCCESS;
      int32_t pcode = packet->getPCode();
      switch (pcode)
      {
        case REQ_RT_ES_KEEPALIVE_MESSAGE:
          ret = rt_es_keepalive(dynamic_cast<BasePacket*>(packet));
          break;
        case REQ_RT_FINISH_TASK_MESSAGE:
          ret = handle_finish_task(dynamic_cast<ReqFinishTaskFromEsMessage*>(packet));
          break;
        case REQ_QUERY_TASK_MESSAGE:
          ret = query_task(dynamic_cast<ReqQueryTaskMessage*>(packet));
          break;
        default:
          ret = EXIT_UNKNOWN_MSGTYPE;
          TBSYS_LOG(ERROR, "unknown msg type: %d", pcode);
          break;
      }

      if (common::TFS_SUCCESS != ret)
      {
        packet->reply_error_packet(TBSYS_LOG_LEVEL(ERROR), ret, "execute message failed, pcode: %d", pcode);
      }

      return EASY_OK;
    }

    int ExpRootServer::rt_es_keepalive(common::BasePacket* packet)
    {
      int32_t iret = NULL != packet? TFS_SUCCESS : TFS_ERROR;
      if (TFS_SUCCESS == iret)
      {
        ReqRtsEsHeartMessage* msg = dynamic_cast<ReqRtsEsHeartMessage*>(packet);
        common::ExpServerBaseInformation& base_info = msg->get_mutable_es();
        iret = manager_.keepalive(base_info);

        RspRtsEsHeartMessage* reply_msg = new(std::nothrow) RspRtsEsHeartMessage();
        assert(NULL != reply_msg);
        int32_t tmp = SYSPARAM_EXPIREROOTSERVER.es_rts_heart_interval_;
        //reply_msg->set_time(SYSPARAM_KVRTSERVER.es_rts_heart_interval_);
        TBSYS_LOG(DEBUG, "es_rts_heart_interval_: %d is ", SYSPARAM_EXPIREROOTSERVER.es_rts_heart_interval_);
        reply_msg->set_time(tmp);
        iret = packet->reply(reply_msg);
      }
      return iret;
    }

    int32_t ExpRootServer::handle_finish_task(ReqFinishTaskFromEsMessage *msg)
    {
      int32_t iret = NULL != msg ? TFS_SUCCESS : TFS_ERROR;
      if (TFS_SUCCESS == iret)
      {
        uint64_t es_id = msg->get_es_id();
        const ExpireTaskInfo task = msg->get_task();

        iret = handle_task_helper_.handle_finish_task(es_id, task);
        if (TFS_SUCCESS == iret)
        {
          iret = msg->reply(new StatusMessage(STATUS_MESSAGE_OK));
        }
        else
        {
          iret = msg->reply_error_packet(TBSYS_LOG_LEVEL(INFO), iret, "handle finish task fail");
        }
      }

      return iret;
    }

    int32_t ExpRootServer::query_task(ReqQueryTaskMessage *msg)
    {
      int32_t iret = NULL != msg ? TFS_SUCCESS : TFS_ERROR;
      if (TFS_SUCCESS == iret)
      {
        RspQueryTaskMessage* rsp = new(std::nothrow) RspQueryTaskMessage();
        assert(NULL != rsp);
        uint64_t es_id = msg->get_es_id();
        std::vector<common::ServerExpireTask>* p_running_tasks = rsp->get_mutable_running_tasks_info();

        iret = handle_task_helper_.query_task(es_id, p_running_tasks);
        if (TFS_SUCCESS == iret)
        {
          iret = msg->reply(rsp);
        }
        else
        {
          iret = msg->reply_error_packet(TBSYS_LOG_LEVEL(INFO), iret, "query task fail");
          tbsys::gDelete(rsp);
        }
      }
      return iret;
    }
  } /** exprootserver **/
}/** tfs **/

