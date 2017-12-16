/*
 * (C) 2007-2010 Alibaba Group Holding Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 * Version: $Id: transfer.cpp 2312 2013-11-26 08:46:08Z duanfei $
 *
 * Authors:
 *   duanfei <duanfei@taobao.com>
 *      - initial release
 *
 */
#include <sys/types.h>
#include <sys/syscall.h>
#include "message/read_index_message_v2.h"
#include "message/server_status_message.h"
#include "message/block_info_message_v2.h"
#include "message/read_data_message_v2.h"
#include "common/status_message.h"
#include "common/error_msg.h"
#include "common/client_manager.h"
#include "common/new_client.h"
#include "requester/ns_requester.h"
#include "requester/ds_requester.h"
#include "requester/sync_util.h"
#include "nameserver/ns_define.h"
#include "requester/misc_requester.h"

#include "transfer.h"

using namespace std;
using namespace tfs::tools;
using namespace tfs::common;
using namespace tfs::message;
using namespace tfs::clientv2;
using namespace tfs::requester;
namespace tfs
{
  namespace transfer
  {
    // trim left and right whitespace/tab/line break
    static char* trim(char* s)
    {
      if (NULL != s)
      {
        while(*s == ' ' || *s == '\t')
          ++s;
        char* p = s + strlen(s) - 1;
        while(p >= s && (*p == ' ' || *p == '\n'  || *s == '\t'))//fgets line including the trailing '\n'
          --p;
        *(p+1) = '\0';
      }
      return s;
    }

    static void log_info(const char* type, const std::string& name, const int32_t ret, const common::FileInfoV2& left, const common::FileInfoV2& right)
    {
      TBSYS_LOG(INFO, "%s file %s ret %d status %d:%d size %d:%d crc %u:%u modify_time %s:%s create_time %s:%s",
        type, name.c_str(), ret, left.status_, right.status_, left.size_, right.size_, left.crc_, right.crc_,
        Func::time_to_str(left.modify_time_).c_str(), Func::time_to_str(right.modify_time_).c_str(),
        Func::time_to_str(left.create_time_).c_str(), Func::time_to_str(right.create_time_).c_str());
    }

    void SyncByBlockWorker::destroy()
    {
      BaseWorker::destroy();
      TfsClientImplV2::Instance()->destroy();
    }

    int SyncByBlockWorker::process(string& line)
    {
      assert(!line.empty());
      int32_t ret = TFS_SUCCESS;
      uint64_t block = INVALID_BLOCK_ID;
      const int32_t type = manager_.get_type();
      if (TRANSFER_TYPE_TRANSFER == type)
      {
        block= strtoull(line.c_str(), NULL, 10);
        ret = (INVALID_BLOCK_ID != block) ? TFS_SUCCESS : EXIT_BLOCK_ID_INVALID_ERROR;
        if (TFS_SUCCESS == ret && !IS_VERFIFY_BLOCK(block))
        {
          uint64_t server = manager_.choose_target_server();
          ret = (INVALID_SERVER_ID != server) ? TFS_SUCCESS : EXIT_DATASERVER_NOT_FOUND;
          if (TFS_SUCCESS == ret)
          {
            ret = transfer_block_by_raw(block, server);
          }
        }
      }
      else if (TRANSFER_TYPE_SYNC_BLOCK == type)
      {
        block= strtoull(line.c_str(), NULL, 10);
        ret = (INVALID_BLOCK_ID != block) ? TFS_SUCCESS : EXIT_BLOCK_ID_INVALID_ERROR;
        if (TFS_SUCCESS == ret && !IS_VERFIFY_BLOCK(block))
        {
          ret = check_dest_block_copies(block, false);
          if (EXIT_FAMILY_EXISTED == ret)
          {
            ret = TFS_SUCCESS;
          }
          if (TFS_SUCCESS == ret)
          {
            ret = transfer_block_by_file(block);
          }
        }
      }
      else if (TRANSFER_TYPE_SYNC_FILE == type)
      {
        ret = transfer_file(line);
      }
      else if (TRANSFER_TYPE_COMPARE_BLOCK == type)
      {
        block= strtoull(line.c_str(), NULL, 10);
        ret = (INVALID_BLOCK_ID != block) ? TFS_SUCCESS : EXIT_BLOCK_ID_INVALID_ERROR;
        if (TFS_SUCCESS == ret && !IS_VERFIFY_BLOCK(block))
        {
          ret = compare_crc_by_block(block);
        }
      }
      else
      {
        ret = EXIT_PARAMETER_ERROR;
        TBSYS_LOG(WARN, "invalid op type: %d", type);
      }

      return ret;
    }

    int SyncByBlockWorker::transfer_block_by_raw(const uint64_t block, const uint64_t server)
    {
      uint64_t sserver = INVALID_SERVER_ID;
      tbnet::DataBuffer sbuf, dbuf;
      common::IndexDataV2 sindex_data, dindex_data;
      int32_t traffic = 1048576; // default 1M
      if (!get_extra_arg().empty())
      {
        traffic = atoi(get_extra_arg().c_str());
      }
      int32_t ret = (INVALID_BLOCK_ID != block && INVALID_SERVER_ID != server) ? TFS_SUCCESS : EXIT_PARAMETER_ERROR;
      if (TFS_SUCCESS == ret)
      {
        std::vector<uint64_t> servers;
        ret = NsRequester::get_block_replicas(Func::get_host_ip(get_src_addr().c_str()), block, servers);
        if (TFS_SUCCESS == ret)
        {
          if (!servers.empty())
            sserver = servers[random() % servers.size()];
          ret = INVALID_SERVER_ID != sserver ? TFS_SUCCESS : EXIT_SERVER_ID_INVALID_ERROR;
        }
        if (TFS_SUCCESS != ret)
        {
          TBSYS_LOG(WARN, "get block %"PRI64_PREFIX"u replica from src cluster %s fail, ret: %d",
              block, get_src_addr().c_str(), ret);
        }
      }
      if (TFS_SUCCESS == ret)
      {
        ret = DsRequester::read_block_index(sserver, block, block, sindex_data);
        if (TFS_SUCCESS != ret)
        {
          TBSYS_LOG(WARN, "read %"PRI64_PREFIX"u index from src cluster %s fail, ret: %d",
              block, get_src_addr().c_str(), ret);
        }
      }
      if (TFS_SUCCESS == ret && !sindex_data.finfos_.empty())
      {
        ret = DsRequester::read_raw_data(sserver, block, traffic, sindex_data.header_.used_offset_, sbuf);
      }
      if (TFS_SUCCESS == ret && !sindex_data.finfos_.empty())
      {
        std::vector<FileInfoV2> nosync_files;//TODO
        ret = DsRequester::recombine_raw_data(sindex_data, sbuf, dindex_data, dbuf, nosync_files);
      }
      if (TFS_SUCCESS == ret && !sindex_data.finfos_.empty() && !dindex_data.finfos_.empty())
      {
        ret = DsRequester::remove_block(block,tbsys::CNetUtil::addrToString(server), false);
        if (EXIT_NO_LOGICBLOCK_ERROR == ret)
          ret = TFS_SUCCESS;
      }
      if (TFS_SUCCESS == ret && !sindex_data.finfos_.empty() && !dindex_data.finfos_.empty())
      {
        ret = check_dest_block_copies(block, true);
      }
      if (TFS_SUCCESS == ret && !sindex_data.finfos_.empty() && !dindex_data.finfos_.empty())
      {
        ret = DsRequester::write_raw_data(dbuf, block, server, traffic);
      }
      if (TFS_SUCCESS == ret && !sindex_data.finfos_.empty() && !dindex_data.finfos_.empty())
      {
        ret = DsRequester::write_raw_index(dindex_data, block, server);
      }
      if (TFS_SUCCESS == ret && !sindex_data.finfos_.empty() && !dindex_data.finfos_.empty())
      {
        ret = check_block_integrity(block, server, dindex_data);
      }

      if (sindex_data.finfos_.empty() || dindex_data.finfos_.empty())
      {
        TBSYS_LOG(INFO, "block %"PRI64_PREFIX"u is empty, will not sync", block);
      }

      return ret;
    }

    int SyncByBlockWorker::check_block_integrity(const uint64_t block, const uint64_t server, const common::IndexDataV2& dindex_data)
    {
      IndexDataV2 index_data;
      int32_t ret = (INVALID_BLOCK_ID != block && INVALID_SERVER_ID != server) ? TFS_SUCCESS : EXIT_PARAMETER_ERROR;
      if (TFS_SUCCESS == ret)
      {
        ret = DsRequester::read_block_index(server, block, block, index_data);
        if (TFS_SUCCESS != ret)
        {
          TBSYS_LOG(WARN, "read %"PRI64_PREFIX"u index from %s failed, ret: %d", block, tbsys::CNetUtil::addrToString(server).c_str(), ret);
        }
      }
      if (TFS_SUCCESS == ret)
      {
        ret = index_data.finfos_.size() == dindex_data.finfos_.size() ? TFS_SUCCESS : EXIT_FILE_COUNT_CONFLICT_ERROR;
        if (TFS_SUCCESS != ret)
        {
          TBSYS_LOG(WARN, "check %"PRI64_PREFIX"u integrity failed, ret: %d, file count conflict: %zd <> %zd, serever: %s",
              block, ret, index_data.finfos_.size(), dindex_data.finfos_.size(), tbsys::CNetUtil::addrToString(server).c_str());
        }
      }

      if (TFS_SUCCESS == ret)
      {
        FileInfoV2 left, right;
        std::vector<FileInfoV2>::const_iterator iter = dindex_data.finfos_.begin();
        for (; iter != dindex_data.finfos_.end() && TFS_SUCCESS == ret; ++iter)
        {
          left = (*iter);
          FSName name(dindex_data.header_.info_.block_id_, left.id_);
          assert(NULL != name.get_name());
          std::string real_name(name.get_name());
          ret = check_file_integrity(real_name, left, right);
          if (TFS_SUCCESS != ret)
          {
            log_info("TRANSFER_BLOCK_BY_RAW", real_name, ret, left, right);
          }
        }
      }
      return ret;
    }

    int SyncByBlockWorker::check_file_integrity(const std::string& filename, const common::FileInfoV2& left, common::FileInfoV2& right)
    {
      int32_t ret = SyncUtil::read_file_real_crc_v2(get_dest_addr().c_str(), filename, right, true);
      if (TFS_SUCCESS == ret)
      {
        ret = left.crc_ != right.crc_ ? EXIT_CHECK_CRC_ERROR : TFS_SUCCESS;
      }
      else
      {
        TBSYS_LOG(WARN, "read %s crc from %s error, ret: %d", filename.c_str(), get_dest_addr().c_str(), ret);
      }
      return ret;
    }

    int SyncByBlockWorker::check_dest_block_copies(const uint64_t block, const bool force_remove)
    {
      int32_t ret = (INVALID_BLOCK_ID != block) ? TFS_SUCCESS : EXIT_PARAMETER_ERROR;
      if (TFS_SUCCESS == ret)
      {
        BlockMeta meta;
        memset(&meta, 0, sizeof(meta));
        ret = NsRequester::get_block_replicas(Func::get_host_ip(get_dest_addr().c_str()), block, meta);
        if (TFS_SUCCESS == ret)
        {
          ret = (INVALID_FAMILY_ID != meta.family_info_.family_id_) ? EXIT_FAMILY_EXISTED : TFS_SUCCESS;
          if (TFS_SUCCESS != ret)
          {
            TBSYS_LOG(WARN, "block: %"PRI64_PREFIX"u exists in dest cluster. family id: %"PRI64_PREFIX"u", block, meta.family_info_.family_id_);
          }
          else
          {
            if (force_remove)
            {
              ret = NsRequester::remove_block(block, get_dest_addr(), tfs::nameserver::HANDLE_DELETE_BLOCK_FLAG_ONLY_RELATION);
              TBSYS_LOG(WARN, "remove block: %"PRI64_PREFIX"u from %s %s", block, get_dest_addr().c_str(), TFS_SUCCESS == ret ? "successful" : "failed");
              for (int32_t index = 0; index < meta.size_; ++index)
              {
                uint64_t server = meta.ds_[index];
                ret = DsRequester::remove_block(block,tbsys::CNetUtil::addrToString(server), false);
                if (EXIT_NO_LOGICBLOCK_ERROR == ret)
                  ret = TFS_SUCCESS;
                TBSYS_LOG(WARN, "remove block: %"PRI64_PREFIX"u from %s %s", block, tbsys::CNetUtil::addrToString(server).c_str(), TFS_SUCCESS == ret ? "successful" : "failed");
              }
            }
          }
        }
        else
        {
          if (EXIT_NO_DATASERVER == ret)
          {
            TBSYS_LOG(WARN, "remove block: %"PRI64_PREFIX"u from %s", block, get_dest_addr().c_str());
            ret = NsRequester::remove_block(block, get_dest_addr(), tfs::nameserver::HANDLE_DELETE_BLOCK_FLAG_ONLY_RELATION);
          }
          if (EXIT_BLOCK_NOT_FOUND == ret)
          {
            ret = TFS_SUCCESS;
          }
        }
      }
      return ret;
    }

    int SyncByBlockWorker::transfer_block_by_file(const uint64_t block)
    {
      int32_t ret = TFS_SUCCESS;
      common::IndexDataV2 sindex_data, dindex_data;
      int32_t sret = TFS_SUCCESS, dret = TFS_SUCCESS;
      sret = MiscRequester::read_block_index(Func::get_host_ip(get_src_addr().c_str()), block, block, sindex_data);
      if (TFS_SUCCESS != sret)
      {
        TBSYS_LOG(WARN, "read block %"PRI64_PREFIX"u index from src cluster %s fail, ret: %d",
            block, get_src_addr().c_str(), ret);
      }
      else
      {
        dret = MiscRequester::read_block_index(Func::get_host_ip(get_dest_addr().c_str()), block, block, dindex_data);
        dret = (TFS_SUCCESS == dret || EXIT_BLOCK_NOT_FOUND == dret) ? TFS_SUCCESS : dret;
        if (TFS_SUCCESS != dret)
        {
          TBSYS_LOG(WARN, "read block %"PRI64_PREFIX"u index from dest cluster %s fail, ret: %d",
              block, get_dest_addr().c_str(), ret);
        }
        else
        {
          const bool force = manager_.get_force();
          FileInfoV2 left, right;
          std::set<FileInfoV2, CompareFileInfoV2ByFileId> dfinfos, sfinfos;
          sfinfos.insert(sindex_data.finfos_.begin(), sindex_data.finfos_.end());
          dfinfos.insert(dindex_data.finfos_.begin(), dindex_data.finfos_.end());
          std::set<FileInfoV2, CompareFileInfoV2ByFileId>::const_iterator iter = sfinfos.begin();
          for (; iter != sfinfos.end() && TFS_SUCCESS == ret; ++iter)
          {
            left = (*iter);
            memset(&right, 0, sizeof(right));
            std::set<FileInfoV2, CompareFileInfoV2ByFileId>::const_iterator it = dfinfos.find(left);
            if (dfinfos.end() != it)
            {
              right  = *it;
              dfinfos.erase(it);
            }

            int32_t retry = 2;
            FSName name(block, left.id_);
            std::string filename(name.get_name());
            do
            {
              ret = SyncUtil::cmp_and_sync_file(get_src_addr(), get_dest_addr(), filename, get_timestamp(), force, left, right, false);
              if (EXIT_SYNC_FILE_NOTHING == ret)
                ret = TFS_SUCCESS;
            }
            while (TFS_SUCCESS != ret && retry-- > 0);
            if (TFS_SUCCESS != ret)
            {
              log_info("TRANSFER_BLOCK_BY_FILE_FAIL", filename, ret, left, right);
            }
          }

          TBSYS_LOG(INFO, "sync block: %"PRI64_PREFIX"u %s successful",block, TFS_SUCCESS == ret ? "all" : "part");

          memset(&left, 0, sizeof(left));
          for (iter = dfinfos.begin(); iter != dfinfos.end(); ++iter)
          {
            FSName name(block, (*iter).id_);
            std::string filename(name.get_name());
            log_info("TRANSFER_BLOCK_BY_FILE_LESS", filename, TFS_SUCCESS, left, (*iter));
          }
        }
      }
      return (TFS_SUCCESS == sret && TFS_SUCCESS == dret) ? ret : TFS_SUCCESS != sret ? sret : dret;
    }

    int SyncByBlockWorker::transfer_file(const std::string& line)
    {
      char name[256] = {'\0'};  // filename may has suffix
      strncpy(name, line.c_str(), 256);
      char* tmp= tfs::transfer::trim(name);
      int32_t ret = (NULL != tmp) ? TFS_SUCCESS : EXIT_INVALID_FILE_NAME;
      if (TFS_SUCCESS != ret)
      {
        TBSYS_LOG(WARN, "%s is invalid file name, ret: %d", line.c_str(), ret);
      }
      else
      {
        std::string filename(tmp);
        const bool force  = manager_.get_force();
        FileInfoV2 left, right;
        ret = SyncUtil::cmp_and_sync_file(get_src_addr(), get_dest_addr(), filename, get_timestamp(), force, left, right);
        if (EXIT_SYNC_FILE_NOTHING == ret)
          ret = TFS_SUCCESS;
        if (TFS_SUCCESS != ret)
        {
          log_info("TRANSFER_FILE_FAIL", filename, ret, left, right);
        }
      }
      return ret;
    }

    int SyncByBlockWorker::compare_crc_by_block(const uint64_t block)
    {
      int32_t ret = TFS_SUCCESS, retry = 2;
      common::IndexDataV2 sindex_data, dindex_data;
      int32_t sret = TFS_SUCCESS, dret = TFS_SUCCESS;
      sret = MiscRequester::read_block_index(Func::get_host_ip(get_src_addr().c_str()), block, block, sindex_data);
      if (TFS_SUCCESS == sret)
      {
        dret = MiscRequester::read_block_index(Func::get_host_ip(get_dest_addr().c_str()), block, block, dindex_data);
        dret = (TFS_SUCCESS == dret || EXIT_BLOCK_NOT_FOUND == dret || EXIT_NO_DATASERVER == dret) ? TFS_SUCCESS : dret;
        if (TFS_SUCCESS == dret)
        {
          FileInfoV2 right, left;
          std::set<FileInfoV2, CompareFileInfoV2ByFileId> dfinfos, sfinfos;
          sfinfos.insert(sindex_data.finfos_.begin(), sindex_data.finfos_.end());
          dfinfos.insert(dindex_data.finfos_.begin(), dindex_data.finfos_.end());
          std::set<FileInfoV2, CompareFileInfoV2ByFileId>::const_iterator iter = sfinfos.begin();
          for (; iter != sfinfos.end() && TFS_SUCCESS == ret; ++iter)
          {
            retry = 2;
            left = (*iter);
            FSName name(block, left.id_);
            std::string filename(name.get_name());
            memset(&right, 0, sizeof(right));
            std::set<FileInfoV2, CompareFileInfoV2ByFileId>::const_iterator it = dfinfos.find(left);
            if (dfinfos.end() != it)
            {
              right = (*it);
              dfinfos.erase(it);
              do
              {
                ret = compare_crc_by_finfo(filename, left, right);
              }
              while (TFS_SUCCESS != ret && retry-- > 0);
              if (TFS_SUCCESS != ret)
              {
                log_info("COMPARE_CRC_FAIL", filename, ret, left, right);
              }
            }
            else
            {
              log_info("COMPARE_CRC_MORE", filename, TFS_SUCCESS, left, right);
            }
          }

          memset(&left, 0, sizeof(left));
          memset(&right, 0, sizeof(right));
          TBSYS_LOG(INFO, "compare block: %"PRI64_PREFIX"u %s successful",block, TFS_SUCCESS == ret ? "all" : "part");
          for (iter = dfinfos.begin(); iter != dfinfos.end(); ++iter)
          {
            right = (*iter);
            FSName name(block, right.id_);
            std::string filename(name.get_name());
            log_info("COMPARE_CRC_LESS", filename, TFS_SUCCESS, left, right);
          }
        }
      }
      return (TFS_SUCCESS == sret && TFS_SUCCESS == dret) ? ret : TFS_SUCCESS != sret ? sret : dret;
    }

    int SyncByBlockWorker::compare_crc_by_finfo(const std::string& name, const common::FileInfoV2& left, const common::FileInfoV2& right)
    {
      int32_t ret = TFS_SUCCESS;
      if (left.modify_time_ <= get_timestamp()
          && 0 == left.status_ & FILE_STATUS_DELETE)
      {
        if (left.status_ == right.status_
          && left.crc_ == right.crc_
          && left.size_ == right.size_)
        {
          if (manager_.get_force())
          {
            FileInfoV2 tleft, tright;
            int32_t lret = SyncUtil::read_file_real_crc_v2(get_src_addr(), name, tleft, true);
            int32_t rret = SyncUtil::read_file_real_crc_v2(get_dest_addr(), name, tright, true);
            ret = (TFS_SUCCESS == lret && TFS_SUCCESS == rret) ? TFS_SUCCESS :
              TFS_SUCCESS == lret ? rret : lret;
            if (TFS_SUCCESS == ret)
            {
              if (tleft.crc_ != tright.crc_)
              {
                log_info("COMPARE_CRC_DIFF", name, EXIT_CHECK_CRC_ERROR, tleft, tright);
              }
            }
            else
            {
              log_info("COMPARE_CRC_FAIL", name, ret, tleft, tright);
            }
          }
        }
        else
        {
          log_info("COMPARE_CRC_DIFF", name, ret, left, right);
        }
      }
      else
      {
        log_info("COMPARE_CRC_IGNORE", name, TFS_SUCCESS, left, right);
      }
      return ret;
    }

    int SyncByBlockManger::begin()
    {
      srandom((unsigned)time(NULL));
      int32_t ret = TfsClientImplV2::Instance()->initialize();
      if (TFS_SUCCESS != ret)
      {
        TBSYS_LOG(WARN, "TfsClientImplV2 initialize failed, ret:%d", ret);
      }
      if (TFS_SUCCESS == ret)
      {
        if (0 == access(get_dest_addr_path().c_str(), R_OK))
        {
          FILE* fp = fopen(get_dest_addr_path().c_str(), "r");
          ret = (NULL == fp) ? EXIT_OPEN_FILE_ERROR : TFS_SUCCESS;
          if (TFS_SUCCESS == ret)
          {
            char line[256];
            while (NULL != fgets(line, 256, fp))
            {
              int32_t len = strlen(line);
              while (line[len-1] == '\n' || line[len-1] == ' ' || line[len-1] == '\t') len--;
              line[len] = '\0';
              if (len == 0) // ignore empty line
              {
                continue;
              }
              uint64_t server = Func::get_host_ip(line);
              assert(INVALID_SERVER_ID != server);
              dest_server_addr_.push_back(server);
            }
            fclose(fp);
            std::sort(dest_server_addr_.begin(), dest_server_addr_.end());
            std::unique(dest_server_addr_.begin(), dest_server_addr_.end());
          }
        }
      }
      return ret;
    }

    void SyncByBlockManger::end()
    {
      TfsClientImplV2::Instance()->destroy();
    }

    BaseWorker* SyncByBlockManger::create_worker()
    {
      return new SyncByBlockWorker(*this);
    }

    uint64_t SyncByBlockManger::choose_target_server()
    {
      uint64_t server = INVALID_SERVER_ID;
      if (!dest_server_addr_.empty())
      {
        if (index_ < 0 || index_ >= static_cast<int32_t>(dest_server_addr_.size()))
          index_ = 0;
        server = dest_server_addr_[index_++];
      }
      return server;
    }

    void SyncByBlockManger::usage(const char* app_name)
    {
      char *options =
        "-s           source server ip:port\n"
        "-d           dest server ip:port\n"
        "-f           input file path\n"
        "-k           dest server addr path\n"
        "-c           retry count when process fail, default 2\n"
        "-m           timestamp eg: 20130610, optional, default next day 0'clock\n"
        "-i           sleep interval (ms), optional, default 0\n"
        "-e           force flag, need strong consistency(crc), optional\n"
        "-u           type [0, 1, 2, 3]\n"
        "             -- 0: transfer block, will remove block from dest first!!!\n"
        "             -- 1: sync block by file\n"
        "             -- 2: sync file\n"
        "             -- 3: compare block \n"
        "-x           traffic threshold per thread, default 1024(kB/s)\n"
        "-t           thread count, optional, defaul 1\n"
        "-l           log level, optional, default info\n"
        "-p           output directory, optional, default ./logs\n"
        "-n           deamon, default false\n"
        "-v           print version information\n"
        "-h           print help information\n"
        "signal       SIGUSR1 inc sleep interval 1000ms\n"
        "             SIGUSR2 dec sleep interval 1000ms\n"
        "examples: daily t1m ==> t1b\n"
        "-- sync_by_blk ==> transfer -s 10.232.31.17:3100 -d 10.232.128.33:3100 -u 1 -f block_list -t 3 -p log_dir\n"
        "-- sync_by_file ==> transfer -s 10.232.31.17:3100 -d 10.232.128.33:3100 -u 2 -f file_list -t 3 -p log_dir\n"
        "-- transfer_by_raw ==> transfer -s 10.232.31.17:3100 -d 10.232.128.33:3100 -u 0 -k server_list -f block_list -t 3 -p log_dir\n";
      fprintf(stderr, "%s usage:\n%s", app_name, options);
      exit(-1);
    }

  } /** end namespace transfer **/
}/** end namespace tfs **/

int main(int argc, char* argv[])
{
  tfs::transfer::SyncByBlockManger work_manager;
  return work_manager.main(argc, argv);
}


