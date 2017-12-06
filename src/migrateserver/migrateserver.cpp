/*
 * (C) 2007-2013 Alibaba Group Holding Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 * Version: $Id: migrateservice.cpp 1000 2013-09-02 09:40:09Z duanfei@taobao.com $
 *
 * Authors:
 *   duanfei<duanfei@taobao.com>
 *      - initial release
 */
#include <Memory.hpp>
#include "common/client_manager.h"
#include "common/func.h"
#include "common/config_item.h"
#include "common/directory_op.h"

#include "ms_define.h"
#include "migrateserver.h"

namespace tfs
{
  namespace migrateserver
  {
    using namespace std;
    using namespace tfs::common;
    using namespace tfs::message;
    MigrateService::MigrateService():
      timeout_thread_(0),
      manager_(NULL)
    {

    }

    MigrateService::~MigrateService()
    {
      tbsys::gDelete(manager_);
    }

    int MigrateService::initialize(int argc, char* argv[])
    {
      UNUSED(argc);
      UNUSED(argv);
      srandom(time(NULL));

      // load config file
      int32_t ret = SYSPARAM_MIGRATESERVER.initialize();
      if (TFS_SUCCESS != ret)
      {
        TBSYS_LOG(ERROR, "load migrateserver parameter failed: %d", ret);
        ret = EXIT_GENERAL_ERROR;
      }
      else
      {
        manager_ = new (std::nothrow)MigrateManager();
        assert(NULL != manager_);
        ret = manager_->initialize();
      }

      if (TFS_SUCCESS == ret)
      {
        timeout_thread_  = new (std::nothrow)TimeoutThreadHelper(*this);
        assert(0 != timeout_thread_);
      }
      TBSYS_LOG(INFO, "initialize migrate server : %s, ret: %d", TFS_SUCCESS == ret ? "successful" : "failed", ret);
      return ret;
    }

    int MigrateService::destroy_service()
    {
      migrateserver::MsRuntimeGlobalInformation& srgi= migrateserver::MsRuntimeGlobalInformation::instance();
      srgi.is_destroy_ = true;
      if (0 != timeout_thread_)
      {
        timeout_thread_->join();
        timeout_thread_ = 0;
      }
      if (NULL != manager_)
      {
        manager_->destroy();
      }
      return TFS_SUCCESS;
    }

    void MigrateService::rotate_(time_t& last_rotate_log_time, time_t now, time_t zonesec)
    {
      if ((now % 86400 >= zonesec)
          && (now % 86400 < zonesec + 300)
          && (last_rotate_log_time < now - 600))
      {
        last_rotate_log_time = now;
        TBSYS_LOGGER.rotateLog(NULL);
      }
    }

    void MigrateService::timeout_()
    {
      tzset();
      const int32_t MAX_SLEEP_TIME_US = 1 * 1000 * 1000;//1s
      time_t zonesec = 86400 + timezone, now = 0, last_rotate_log_time = 0;
      migrateserver::MsRuntimeGlobalInformation& srgi= migrateserver::MsRuntimeGlobalInformation::instance();
      while (!srgi.is_destroyed())
      {
        now = time(NULL);
        //rotate log
        rotate_(last_rotate_log_time, now, zonesec);
        usleep(MAX_SLEEP_TIME_US);
      }
    }

    bool MigrateService::check_response(common::NewClient* client)
    {
      bool all_success = (NULL != client);
      NewClient::RESPONSE_MSG_MAP* sresponse = NULL;
      NewClient::RESPONSE_MSG_MAP* fresponse = NULL;
      if (all_success)
      {
        sresponse = client->get_success_response();
        fresponse = client->get_fail_response();
        all_success = ((NULL != sresponse) && (NULL != fresponse));
      }

      if (all_success)
      {
        all_success = (sresponse->size() == client->get_send_id_sign().size());
        NewClient::RESPONSE_MSG_MAP::iterator iter = sresponse->begin();
        for ( ; all_success && (iter != sresponse->end()); iter++)
        {
          tbnet::Packet* rmsg = iter->second.second;
          all_success = (NULL != rmsg);
          if (all_success)
          {
            all_success = (STATUS_MESSAGE == rmsg->getPCode());
          }
          if (all_success)
          {
            StatusMessage* smsg = dynamic_cast<StatusMessage*>(rmsg);
            all_success = STATUS_MESSAGE_OK == smsg->get_status();
          }
        }
      }
      return all_success;
    }

    int MigrateService::callback(common::NewClient* client)
    {
      int32_t ret = NULL != client ? TFS_SUCCESS : TFS_ERROR;
      if (TFS_SUCCESS == ret)
      {
        tbnet::Packet* packet = client->get_source_msg();
        ret = (NULL != packet)? TFS_SUCCESS: TFS_ERROR;
        if (TFS_SUCCESS == ret)
        {
          //bool all_success = check_response(client);
          //int32_t pcode = packet->getPCode();
          //common::BasePacket* bpacket= dynamic_cast<BasePacket*>(packet);
        }
      }
      return ret;
    }

    tbnet::IPacketHandler::HPRetCode MigrateService::handlePacket(tbnet::Connection *connection, tbnet::Packet *packet)
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
          migrateserver::MsRuntimeGlobalInformation& srgi= migrateserver::MsRuntimeGlobalInformation::instance();
          if (!srgi.is_destroyed())
          {
            bret = push(bpacket, false);
            if (bret)
              hret = tbnet::IPacketHandler::KEEP_CHANNEL;
            else
            {
              bpacket->reply_error_packet(TBSYS_LOG_LEVEL(ERROR),STATUS_MESSAGE_ERROR, "%s, task message beyond max queue size, discard", get_ip_addr());
              bpacket->free();
            }
          }
          else
          {
            bpacket->reply_error_packet(TBSYS_LOG_LEVEL(WARN), STATUS_MESSAGE_ACCESS_DENIED,
                "you client %s access been denied. msgtype: %d", tbsys::CNetUtil::addrToString(
                  connection->getPeerId()).c_str(), packet->getPCode());
            // packet denied, must free
            bpacket->free();
          }
        }
      }
      return hret;
    }

    bool MigrateService::handlePacketQueue(tbnet::Packet* packet, void* args)
    {
      bool bret = BaseService::handlePacketQueue(packet, args);
      if (bret)
      {
        int32_t pcode = packet->getPCode();
        int32_t ret = LOCAL_PACKET == pcode ? TFS_ERROR : TFS_SUCCESS;
        if (TFS_SUCCESS == ret)
        {
          switch (pcode)
          {
            default:
              ret = EXIT_UNKNOWN_MSGTYPE;
              break;
          }
          if (common::TFS_SUCCESS != ret)
          {
            common::BasePacket* msg = dynamic_cast<common::BasePacket*>(packet);
            msg->reply_error_packet(TBSYS_LOG_LEVEL(ERROR), ret, "execute message failed, pcode: %d", pcode);
          }
        }
      }
      return bret;
    }

    int MigrateService::handle(common::BasePacket* packet)
    {
      assert(NULL != packet);
      int ret = TFS_SUCCESS;
      int32_t pcode = packet->getPCode();
      if (TFS_SUCCESS == ret)
      {
        switch (pcode)
        {
          default:
            ret = EXIT_UNKNOWN_MSGTYPE;
            break;
        }
      }

      if (common::TFS_SUCCESS != ret)
      {
        common::BasePacket* msg = dynamic_cast<common::BasePacket*>(packet);
        msg->reply_error_packet(TBSYS_LOG_LEVEL(ERROR), ret, "execute message failed, pcode: %d", pcode);
      }

      return EASY_OK;
    }

    void MigrateService::TimeoutThreadHelper::run()
    {
      service_.timeout_();
    }

    int ms_async_callback(common::NewClient* client)
    {
      MigrateService* service = dynamic_cast<MigrateService*>(BaseMain::instance());
      int32_t ret = NULL != service ? TFS_SUCCESS : TFS_ERROR;
      if (TFS_SUCCESS == ret)
      {
        ret = service->callback(client);
      }
      return ret;
    }
  }/** end namespace migrateserver **/
}/** end namespace tfs **/
