/*
 * (C) 2007-2010 Alibaba Group Holding Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 * Version: $Id: tfs_client_api.cpp 49 2010-11-16 09:58:57Z zongdai@taobao.com $
 *
 * Authors:
 *    daoan<daoan@taobao.com>
 *      - initial release
 *
 */
#include "tfs_rc_client_api_impl.h"

#include "common/func.h"
#include "common/session_util.h"
#include "common/client_manager.h"
#include "tfs_client_impl_v2.h"
#include "tfs_rc_helper.h"
#include "fsname.h"
#include "tfs_kv_meta_client_impl.h"
#include "tfs_cluster_manager.h"

#define RC_CLIENT_VERSION "rc_1.0.0_c++"


namespace
{
  const int INIT_INVALID = 0;
  const int INIT_LOGINED = 1;
  const int CLUSTER_ACCESS_TYPE_READ_ONLY = 1;
  const int CLUSTER_ACCESS_TYPE_READ_WRITE = 2;

}

namespace tfs
{
  namespace clientv2
  {
    using namespace tfs::common;
    using namespace std;

    StatUpdateTask::StatUpdateTask(RcClientImpl& rc_client):rc_client_(rc_client)
    {
    }
    void StatUpdateTask::runTimerTask()
    {
      uint64_t rc_ip = 0;
      KeepAliveInfo ka_info;
      {
        tbsys::CThreadGuard mutex_guard(&rc_client_.mutex_);
        rc_client_.get_ka_info(ka_info);
        rc_ip = rc_client_.active_rc_ip_;
      }
      ka_info.s_stat_.cache_hit_ratio_ = 0;//discard this statistic
      bool update_flag = false;
      BaseInfo new_base_info;
      int ret = RcHelper::keep_alive(rc_ip, ka_info, update_flag, new_base_info);
      if (TFS_SUCCESS == ret)
      {
        TBSYS_LOG(DEBUG, "keep alive ok, update flag: %d", update_flag);
        {
          tbsys::CThreadGuard mutex_guard(&rc_client_.mutex_);
          rc_client_.next_rc_index_ = 0;
        }
        int last_report_interval = 0;
        if (update_flag)
        {
          tbsys::CThreadGuard mutex_guard(&rc_client_.mutex_);
          last_report_interval = rc_client_.base_info_.report_interval_;
          rc_client_.base_info_ = new_base_info;
          rc_client_.calculate_ns_info(new_base_info);
#ifdef WITH_TAIR_CACHE
          std::vector<std::string> ns_cache_info;
          common::Func::split_string(rc_client_.base_info_.ns_cache_info_.c_str(), ';', ns_cache_info);
          if (ns_cache_info.size() == 4)
          {
            TfsClientImplV2::Instance()->set_remote_cache_info(ns_cache_info[0].c_str(),
                ns_cache_info[1].c_str(), ns_cache_info[2].c_str(),
                atoi(ns_cache_info[3].c_str()));
            TfsClientImplV2::Instance()->set_use_remote_cache(rc_client_.base_info_.use_remote_cache_);
          }
          else
          {
            TBSYS_LOG(WARN, "invalid ns_cache_info(size: %zd), remote cache will not initialize", ns_cache_info.size());
            TfsClientImplV2::Instance()->set_use_remote_cache(false);
          }
#endif
          rc_client_.session_base_info_.modify_time_ = rc_client_.base_info_.modify_time_;
        }
        if (update_flag && last_report_interval != new_base_info.report_interval_)
        {
          TBSYS_LOG(DEBUG, "reschedule update stat task :old interval is %d new is %d",
              last_report_interval, new_base_info.report_interval_);

          rc_client_.keepalive_timer_->cancel(rc_client_.stat_update_task_);
          rc_client_.keepalive_timer_->scheduleRepeated(rc_client_.stat_update_task_,
              tbutil::Time::seconds(new_base_info.report_interval_));
        }
      }
      else
      {
        TBSYS_LOG(DEBUG, "keep alive error will roll back");
        uint64_t next_rc_ip;
        tbsys::CThreadGuard mutex_guard(&rc_client_.mutex_);
        next_rc_ip = rc_client_.get_active_rc_ip(rc_client_.next_rc_index_);
        if (0 == next_rc_ip)
        {
          rc_client_.next_rc_index_ = 0;
        }
        else
        {
          rc_client_.active_rc_ip_ = next_rc_ip;
        }
        // roll back stat info;
        rc_client_.stat_ += ka_info.s_stat_;
      }
    }

    RcClientImpl::RcClientImpl()
      :local_addr_(0),
      init_stat_(INIT_INVALID), active_rc_ip_(0), next_rc_index_(0),
      kv_meta_client_(NULL), tfs_cluster_manager_(NULL), app_id_(0), my_fd_(1)
    {
    }

    void RcClientImpl::destroy()// must explict call
    {
      tbsys::CThreadGuard mutex_guard(&mutex_);
      logout();
      if (NULL != tfs_cluster_manager_)
      {
        delete tfs_cluster_manager_;
        tfs_cluster_manager_ = NULL;
      }
      if (NULL != kv_meta_client_)
      {
        delete kv_meta_client_;
        kv_meta_client_ = NULL;
      }
      if (0 != keepalive_timer_)
      {
        keepalive_timer_->cancel(stat_update_task_);
        keepalive_timer_->destroy();
        stat_update_task_ = 0;
        keepalive_timer_ = 0;// auto pointer free
      }
      TfsClientImplV2::Instance()->destroy();
    }

    RcClientImpl::~RcClientImpl()
    {
    }
    TfsRetType RcClientImpl::initialize(const char* str_rc_ip, const char* app_key, const char* str_app_ip,
        const int32_t cache_times, const int32_t cache_items, const char* dev_name)
    {
      if (str_rc_ip == NULL || app_key == NULL)
      {
        TBSYS_LOG(WARN, "input parameter is invalid. rc_ip: %s, app_key: %s, app_ip: %s",
            str_rc_ip == NULL ? "null":str_rc_ip,
            app_key == NULL ? "null":app_key,
            str_app_ip == NULL ? "null":str_app_ip);
        return TFS_ERROR;
      }
      if (cache_times < 0 || cache_items < 0)
      {
        TBSYS_LOG(WARN, "invalid cache setting. cache_times: %d, cache_items: %d", cache_times, cache_items);
        return TFS_ERROR;
      }
      uint64_t rc_ip = Func::get_host_ip(str_rc_ip);
      uint64_t app_ip = 0;
      if (NULL != str_app_ip)
      {
        app_ip = Func::str_to_addr(str_app_ip, 0);
      }
      return initialize(rc_ip, app_key, app_ip, cache_times, cache_items, dev_name);
    }

    TfsRetType RcClientImpl::initialize(const uint64_t rc_ip, const char* app_key, const uint64_t app_ip,
        const int32_t cache_times, const int32_t cache_items, const char* dev_name)
    {
      int ret = TFS_SUCCESS;
      tbsys::CThreadGuard mutex_guard(&mutex_);
      if (init_stat_ != INIT_LOGINED)
      {
        stat_update_task_ = new StatUpdateTask(*this);
        keepalive_timer_ = new tbutil::Timer();
        kv_meta_client_ = new KvMetaClientImpl();
        tfs_cluster_manager_ = new TfsClusterManager();
        if (TFS_SUCCESS == ret)
        {
          ret = TfsClientImplV2::Instance()->initialize(NULL, cache_times, cache_items);
        }
        TBSYS_LOG(DEBUG, "TfsClientImplV2::Instance()->initialize ret %d", ret);
        if (TFS_SUCCESS == ret)
        {
          if (app_ip != 0)
          {
            local_addr_ = app_ip & 0xffffffff;
          }
          else
          {
            local_addr_ = Func::get_local_addr(dev_name);
          }
          TBSYS_LOG(DEBUG, "local_addr_ = %s", tbsys::CNetUtil::addrToString(local_addr_).c_str());
          ret = login(rc_ip, app_key, local_addr_);
        }
        TBSYS_LOG(DEBUG, "login ret %d", ret);

        if (TFS_SUCCESS == ret)
        {
          session_base_info_.client_version_ = RC_CLIENT_VERSION;
          session_base_info_.cache_size_ = cache_items;
          session_base_info_.cache_time_ = cache_times;
          session_base_info_.modify_time_ = tbsys::CTimeUtil::getTime();
          session_base_info_.is_logout_ = false;

          active_rc_ip_ = rc_ip;
          // TODO: set kv_rs_addr before here
          kv_meta_client_->set_tfs_cluster_manager(tfs_cluster_manager_);
          kv_meta_client_->initialize(kv_rs_addr_);
#ifdef WITH_TAIR_CACHE
          std::vector<std::string> ns_cache_info;
          common::Func::split_string(base_info_.ns_cache_info_.c_str(), ';', ns_cache_info);
          if (ns_cache_info.size() == 4)
          {
            TfsClientImplV2::Instance()->set_remote_cache_info(ns_cache_info[0].c_str(),
                ns_cache_info[1].c_str(), ns_cache_info[2].c_str(),
                atoi(ns_cache_info[3].c_str()));
            TfsClientImplV2::Instance()->set_use_remote_cache(base_info_.use_remote_cache_);
          }
          else
          {
            TBSYS_LOG(WARN, "invalid ns_cache_info(size: %zd), remote cache will not initialize", ns_cache_info.size());
            TfsClientImplV2::Instance()->set_use_remote_cache(false);
          }
#endif
          keepalive_timer_->scheduleRepeated(stat_update_task_,
              tbutil::Time::seconds(base_info_.report_interval_));
        }
      }
      return ret;
    }

    TfsRetType RcClientImpl::logout()
    {
      int ret = TFS_ERROR;
      size_t retry = 0;
      uint64_t rc_ip = 0;
      ret = check_init_stat();
      if (TFS_SUCCESS == ret)
      {
        KeepAliveInfo ka_info;
        get_ka_info(ka_info);
        ka_info.s_base_info_.is_logout_ = true;
        while(0 != (rc_ip = get_active_rc_ip(retry)))
        {
          ret = RcHelper::logout(rc_ip, ka_info);
          if (TFS_SUCCESS == ret)
          {
            break;
          }
        }
      }
      if (TFS_SUCCESS == ret)
      {
        init_stat_ = INIT_INVALID;
      }
      return ret;
    }

#ifdef WITH_TAIR_CACHE
    void RcClientImpl::set_remote_cache_info(const char * remote_cache_info)
    {
      std::vector<std::string> tair_addr;
      common::Func::split_string(remote_cache_info, ';', tair_addr);
      if (tair_addr.size() == 4)
      {
        TfsClientImplV2::Instance()->set_remote_cache_info(tair_addr[0].c_str(),
            tair_addr[1].c_str(), tair_addr[2].c_str(),
            atoi(tair_addr[3].c_str()));
        TfsClientImplV2::Instance()->set_use_remote_cache(true);
      }
    }
#endif

    void RcClientImpl::set_log_level(const char* level)
    {
      TBSYS_LOGGER.setLogLevel(level);
    }

    void RcClientImpl::set_log_file(const char* log_file)
    {
      TBSYS_LOGGER.setFileName(log_file);
    }

    void RcClientImpl::set_wait_timeout(const int64_t timeout_ms)
    {
      TfsClientImplV2::Instance()->set_wait_timeout(timeout_ms);
    }

    int RcClientImpl::open(const char* file_name, const char* suffix, const RcClient::RC_MODE mode)
    {
      int fd = -1;
      int ret = check_init_stat();
      if (TFS_SUCCESS == ret)
      {
        if (NULL == file_name && RcClient::READ == mode)
        {
          ret = EXIT_PARAMETER_ERROR;
        }
        if (NULL != file_name && *file_name == 'L')
        {
          TBSYS_LOG(WARN, "not support largr tfs file");
          ret = EXIT_PARAMETER_ERROR;
        }
      }

      if (TFS_SUCCESS == ret)
      {
        int flag = -1;
        //check mode
        if (RcClient::CREATE == mode)
        {
          flag = common::T_WRITE;
        }
        else if (RcClient::READ == mode)
        {
          flag = common::T_READ | common::T_STAT;
        }
        else if(RcClient::READ_FORCE == mode)
        {
          flag = common::T_READ | common::T_STAT | common::T_FORCE;
        }

        ret = flag != -1 ? TFS_SUCCESS : TFS_ERROR;
        if (TFS_SUCCESS != ret)
        {
          TBSYS_LOG(ERROR, "mode %d not support", mode);
        }
        else
        {
          if (have_permission(file_name, mode))
          {
            fdInfo fd_info(file_name, suffix, flag);
            fd = gen_fdinfo(fd_info);
          }
          else
          {
            TBSYS_LOG(WARN, "no permission to do this");
          }
        }
      }
      return fd;
    }

    TfsRetType RcClientImpl::close(const int fd, char* tfs_name_buf, const int32_t buff_len)
    {
      int ret = check_init_stat();
      if (TFS_SUCCESS == ret)
      {
        fdInfo fd_info;
        remove_fdinfo(fd, fd_info);
        if (fd_info.raw_tfs_fd_ >= 0)
        {
          ret = TfsClientImplV2::Instance()->close(fd_info.raw_tfs_fd_, tfs_name_buf, buff_len);
        }
      }
      return ret;
    }

    int64_t RcClientImpl::real_read(const int fd, const int raw_tfs_fd, void* buf, const int64_t count,
        fdInfo& fd_info, TfsFileStat* tfs_stat_buf)
    {
      int64_t read_count = -1;
      if (raw_tfs_fd >= 0)
      {
        int64_t start_time = tbsys::CTimeUtil::getTime();
        read_count = TfsClientImplV2::Instance()->pread(raw_tfs_fd, buf, count, fd_info.offset_);
        if (NULL != tfs_stat_buf)
        {
          int ret = TfsClientImplV2::Instance()->fstat(raw_tfs_fd, tfs_stat_buf);
          if (TFS_SUCCESS != ret)
          {
            TBSYS_LOG(WARN, "stat error, ret: %d", ret);
            read_count = ret;
          }
        }
        if (read_count > 0)
        {
          fd_info.offset_ += read_count;
          // should use rc's fd, not raw_tfs_fd
          if (TFS_SUCCESS != update_fdinfo_offset(fd, fd_info.offset_))
          {
            TBSYS_LOG(WARN, "update_fdinfo_offset error ");
          }
        }
        int64_t response_time = tbsys::CTimeUtil::getTime() - start_time;
        add_stat_info(OPER_READ, read_count, response_time, read_count >= 0);
      }
      return read_count;
    }

    int64_t RcClientImpl::read_ex(const int fd, void* buf, const int64_t count, TfsFileStat* tfs_stat_buf )
    {
      int64_t read_count = -1;
      int ret = check_init_stat();
      if (TFS_SUCCESS == ret)
      {
        fdInfo fd_info;
        ret = get_fdinfo(fd, fd_info);
        if (TFS_SUCCESS == ret)
        {
          if (fd_info.raw_tfs_fd_ >= 0)
          {
            read_count = real_read(fd, fd_info.raw_tfs_fd_, buf, count, fd_info, tfs_stat_buf);
          }
          else if (INVALID_RAW_TFS_FD == fd_info.raw_tfs_fd_)
          {
            //not open yet,
            int ns_get_index = 0;
            string ns_addr;
            int raw_tfs_fd = -1;
            //RcClient::RC_MODE mode = RcClient::READ;
            const char* file_name = NULL;
            const char* suffix = NULL;
            if (!fd_info.name_.empty())
            {
              file_name = fd_info.name_.c_str();
            }
            if (!fd_info.suffix_.empty())
            {
              suffix = fd_info.suffix_.c_str();
            }
            do
            {
              ns_addr = tfs_cluster_manager_->get_read_ns_addr(file_name, ns_get_index++);
              if (ns_addr.empty())
              {
                break;
              }
              raw_tfs_fd = open(ns_addr.c_str(), file_name, suffix,
                  fd_info.flag_);
              read_count = real_read(fd, raw_tfs_fd, buf, count, fd_info, tfs_stat_buf);
              if (read_count < 0)
              {
                TBSYS_LOG(WARN, "read file from ns %s error ret is %"PRI64_PREFIX"d",
                    ns_addr.c_str(), read_count);
                if (raw_tfs_fd >= 0)
                {
                  TfsClientImplV2::Instance()->close(raw_tfs_fd);
                }
                raw_tfs_fd = -1;
              }
              else
              {
                break;
              }
            }while (raw_tfs_fd < 0);
            if (raw_tfs_fd >= 0)
            {
              if (TFS_SUCCESS != update_fdinfo_rawfd(fd, raw_tfs_fd))
              {
                TfsClientImplV2::Instance()->close(raw_tfs_fd);
              }
            }
          }
          else
          {
            TBSYS_LOG(ERROR, "name meta file not support read or readv2");
          }
        }
      }
      return read_count;
    }
    int64_t RcClientImpl::read(const int fd, void* buf, const int64_t count)
    {
      return read_ex(fd, buf, count, NULL);
    }

    int64_t RcClientImpl::readv2(const int fd, void* buf, const int64_t count, TfsFileStat* tfs_stat_buf)
    {
      return read_ex(fd, buf, count, tfs_stat_buf);
    }

    int64_t RcClientImpl::write(const int fd, const void* buf, const int64_t count)
    {
      int64_t write_count = -1;
      int ret = check_init_stat();
      if (TFS_SUCCESS == ret)
      {
        fdInfo fd_info;
        ret = get_fdinfo(fd, fd_info);
        if (TFS_SUCCESS == ret)
        {
          int64_t start_time = tbsys::CTimeUtil::getTime();
          if (fd_info.raw_tfs_fd_ >= 0)
          {
            write_count = TfsClientImplV2::Instance()->write(fd_info.raw_tfs_fd_, buf, count);
          }
          else if (INVALID_RAW_TFS_FD == fd_info.raw_tfs_fd_)
          {
            int ns_get_index = 0;
            string ns_addr;
            int raw_tfs_fd = -1;
            //RcClient::RC_MODE mode = RcClient::READ;
            const char* file_name = NULL;
            const char* suffix = NULL;
            if (!fd_info.name_.empty())
            {
              file_name = fd_info.name_.c_str();
            }
            if (!fd_info.suffix_.empty())
            {
              suffix = fd_info.suffix_.c_str();
            }
            do
            {
              ns_addr = tfs_cluster_manager_->get_write_ns_addr(ns_get_index++);
              if (ns_addr.empty())
              {
                break;
              }
              raw_tfs_fd = open(ns_addr.c_str(), file_name, suffix, fd_info.flag_);
              write_count = TfsClientImplV2::Instance()->write(raw_tfs_fd, buf, count);
              if (write_count < 0)
              {
                TBSYS_LOG(WARN, "write file to ns %s error ret is %"PRI64_PREFIX"d",
                    ns_addr.c_str(), write_count);
                if (raw_tfs_fd >= 0)
                {
                  TfsClientImplV2::Instance()->close(raw_tfs_fd);
                }
                raw_tfs_fd = -1;
              }
              else
              {
                break;
              }
            }while (raw_tfs_fd < 0);
            if (raw_tfs_fd >= 0)
            {
              if (TFS_SUCCESS != update_fdinfo_rawfd(fd, raw_tfs_fd))
              {
                TfsClientImplV2::Instance()->close(raw_tfs_fd);
                write_count = -1;
              }
            }
          }
          else
          {
            TBSYS_LOG(ERROR, "name meta file not support write");
          }
          int64_t response_time = tbsys::CTimeUtil::getTime() - start_time;
          add_stat_info(OPER_WRITE, write_count, response_time, write_count >= 0);
        }
      }
      return write_count;
    }

    int64_t RcClientImpl::lseek(const int fd, const int64_t offset, const int whence)
    {
      int64_t ret_offset = -1;
      fdInfo fd_info;
      int ret = get_fdinfo(fd, fd_info);
      if (TFS_SUCCESS == ret)
      {
        if (fd_info.raw_tfs_fd_ >= 0 || INVALID_RAW_TFS_FD == fd_info.raw_tfs_fd_)
        {
          switch (whence)
          {
            case T_SEEK_SET:
              if (offset < 0)
              {
                TBSYS_LOG(ERROR, "wrong offset seek_set, %"PRI64_PREFIX"d", offset);
                ret_offset = EXIT_PARAMETER_ERROR;
              }
              else
              {
                fd_info.offset_ = offset;
                ret_offset = fd_info.offset_;
              }
              break;
            case T_SEEK_CUR:
              if (fd_info.offset_ + offset < 0)
              {
                TBSYS_LOG(ERROR, "wrong offset seek_cur, %"PRI64_PREFIX"d", offset);
                ret_offset = EXIT_PARAMETER_ERROR;
              }
              else
              {
                fd_info.offset_ += offset;
                ret_offset = fd_info.offset_;
              }
              break;
            default:
              TBSYS_LOG(ERROR, "unknown seek flag: %d", whence);
              break;

          }
          if (ret_offset >= 0)
          {
            if (TFS_SUCCESS != update_fdinfo_offset(fd, ret_offset))
            {
              TBSYS_LOG(WARN, "update_fdinfo_offset error ");
              ret_offset = -1;
            }
          }
        }
        else
        {
          TBSYS_LOG(ERROR, "name meta file not support lseek");
        }
      }
      return ret_offset;
    }

    TfsRetType RcClientImpl::fstat(const int fd, common::TfsFileStat* buf, const common::TfsStatType fmode)
    {
      fdInfo fd_info;
      int ret = get_fdinfo(fd, fd_info);
      if (TFS_SUCCESS == ret)
      {
        if (fd_info.raw_tfs_fd_ >= 0)
        {
          TfsClientImplV2::Instance()->set_option_flag(fd_info.raw_tfs_fd_, fmode);
          ret = TfsClientImplV2::Instance()->fstat(fd_info.raw_tfs_fd_, buf);
        }
        else if (INVALID_RAW_TFS_FD == fd_info.raw_tfs_fd_)
        {
          int ns_get_index = 0;
          string ns_addr;
          int raw_tfs_fd = -1;
          //RcClient::RC_MODE mode = RcClient::READ;
          const char* file_name = NULL;
          const char* suffix = NULL;
          if (!fd_info.name_.empty())
          {
            file_name = fd_info.name_.c_str();
          }
          if (!fd_info.suffix_.empty())
          {
            suffix = fd_info.suffix_.c_str();
          }
          do
          {
            ns_addr = tfs_cluster_manager_->get_read_ns_addr(file_name, ns_get_index++);
            if (ns_addr.empty())
            {
              break;
            }
            raw_tfs_fd = open(ns_addr.c_str(), file_name, suffix, fd_info.flag_);
            TfsClientImplV2::Instance()->set_option_flag(raw_tfs_fd, fmode);
            ret = TfsClientImplV2::Instance()->fstat(raw_tfs_fd, buf);
            if (TFS_SUCCESS != ret)
            {
              TBSYS_LOG(WARN, "fstat file from ns %s error ret is %d",
                  ns_addr.c_str(), ret);
              if (raw_tfs_fd >= 0)
              {
                TfsClientImplV2::Instance()->close(raw_tfs_fd);
              }
              raw_tfs_fd = -1;
            }
            else
            {
              break;
            }
          }while (raw_tfs_fd < 0);
          if (raw_tfs_fd >= 0)
          {
            if (TFS_SUCCESS != update_fdinfo_rawfd(fd, raw_tfs_fd))
            {
              TfsClientImplV2::Instance()->close(raw_tfs_fd);
            }
          }
        }
        else
        {
          TBSYS_LOG(ERROR, "name meta file not support fstat");
        }
      }
      return ret;
    }

    TfsRetType RcClientImpl::unlink(const char* file_name, const char* suffix, const common::TfsUnlinkType action)
    {
      int ret = check_init_stat();
      if (TFS_SUCCESS == ret)
      {
        int ns_get_index = 0;
        string ns_addr;
        do
        {
          ns_addr = tfs_cluster_manager_->get_unlink_ns_addr(file_name, ns_get_index++);
          if (ns_addr.empty())
          {
            ret = TFS_ERROR;
            break;
          }
          ret = unlink(ns_addr.c_str(), file_name, suffix, action);
        } while(TFS_SUCCESS != ret);
      }
      return ret;
    }

    TfsRetType RcClientImpl::unlink(const char* ns_addr, const char* file_name,
        const char* suffix, const common::TfsUnlinkType action)
    {
      int ret = TFS_SUCCESS;
      int64_t start_time = tbsys::CTimeUtil::getTime();
      int64_t data_size = 0;
      ret = TfsClientImplV2::Instance()->unlink(data_size, file_name, suffix,
          action, ns_addr);
      int64_t response_time = tbsys::CTimeUtil::getTime() - start_time;
      switch (action)
      {
        case DELETE:
          break;
        case UNDELETE:
          data_size = 0 - data_size;
          break;
        default:
          data_size = 0;
          break;
      }
      add_stat_info(OPER_UNLINK, data_size, response_time, ret == TFS_SUCCESS);
      return ret;
    }

    int64_t RcClientImpl::save_file(const char* local_file, char* tfs_name_buf, const int32_t buf_len, const char* suffix)
    {
      int ret = check_init_stat();
      int64_t saved_size = -1;
      if (TFS_SUCCESS == ret)
      {
        FSName fs(tfs_name_buf);
        bool is_update = fs.get_block_id() > 0 ? true : false;
        int ns_get_index = 0;
        string ns_addr;
        do
        {
          if (is_update)
          {
            // TODO: should use TfsClientImplV2::save_file_update,
            // here not support update for rc client
            ns_addr = tfs_cluster_manager_->get_unlink_ns_addr(tfs_name_buf, ns_get_index++);
          }
          else
          {
            ns_addr = tfs_cluster_manager_->get_write_ns_addr(ns_get_index++);
          }
          if (ns_addr.empty())
          {
            break;
          }
          saved_size = save_file(ns_addr.c_str(), local_file, tfs_name_buf, buf_len, suffix);
        } while(saved_size < 0);
      }
      return saved_size;
    }

    int64_t RcClientImpl::save_buf(const char* source_data, const int32_t data_len,
        char* tfs_name_buf, const int32_t buff_len, const char* suffix)
    {
      int ret = check_init_stat();
      int64_t saved_size = -1;
      if (TFS_SUCCESS == ret)
      {
        FSName fs(tfs_name_buf);
        bool is_update = fs.get_block_id() > 0 ? true : false;
        int ns_get_index = 0;
        string ns_addr;
        do
        {
          if (is_update)
          {
            ns_addr = tfs_cluster_manager_->get_unlink_ns_addr(tfs_name_buf, ns_get_index++);
          }
          else
          {
            ns_addr = tfs_cluster_manager_->get_write_ns_addr(ns_get_index++);
          }
          if (ns_addr.empty())
          {
            break;
          }
          saved_size = save_buf(ns_addr.c_str(), source_data, data_len,
              tfs_name_buf, buff_len, suffix);
        } while(saved_size < 0);
      }
      return saved_size;
    }

    int RcClientImpl::fetch_file(const char* local_file,
        const char* file_name, const char* suffix)
    {
      int ret = check_init_stat();
      if (TFS_SUCCESS == ret)
      {
        int ns_get_index = 0;
        string ns_addr;
        do
        {
          ns_addr = tfs_cluster_manager_->get_read_ns_addr(file_name, ns_get_index++);
          if (ns_addr.empty())
          {
            ret = TFS_ERROR;
            break;
          }
          ret = fetch_file(ns_addr.c_str(), local_file, file_name, suffix);
        } while(ret != TFS_SUCCESS);
      }
      return ret;
    }

    int64_t RcClientImpl::save_file(const char* ns_addr, const char* local_file, char* ret_tfs_name_buf,
        const int32_t buf_len, const char* suffix)
    {
      int flag = T_DEFAULT;
      int64_t saved_size = -1;
      int64_t start_time = tbsys::CTimeUtil::getTime();
      saved_size = TfsClientImplV2::Instance()->save_file(ret_tfs_name_buf, buf_len, local_file,
          flag, suffix, ns_addr);
      int64_t response_time = tbsys::CTimeUtil::getTime() - start_time;
      add_stat_info(OPER_WRITE, saved_size, response_time, saved_size >= 0);
      return saved_size;
    }

    int RcClientImpl::fetch_buf(int64_t& ret_count, char* buf, const int64_t count,
        const char* file_name, const char* suffix)
    {
      int ret = check_init_stat();
      if (TFS_SUCCESS == ret)
      {
        int ns_get_index = 0;
        string ns_addr;
        do
        {
          ns_addr = tfs_cluster_manager_->get_read_ns_addr(file_name, ns_get_index++);
          if (ns_addr.empty())
          {
            ret = TFS_ERROR;
            break;
          }
          ret = fetch_buf(ns_addr.c_str(), ret_count, buf, count, file_name, suffix);
        } while(ret != TFS_SUCCESS);
      }
      return ret;
    }

    int64_t RcClientImpl::save_buf(const char* ns_addr, const char* source_data, const int32_t data_len,
        char* ret_tfs_name_buf, const int32_t buf_len, const char* suffix)
    {
      int64_t saved_size = -1;
      int64_t start_time = tbsys::CTimeUtil::getTime();
      saved_size = TfsClientImplV2::Instance()->save_buf(ret_tfs_name_buf, buf_len, source_data, data_len,
          T_DEFAULT, suffix, ns_addr);
      int64_t response_time = tbsys::CTimeUtil::getTime() - start_time;
      add_stat_info(OPER_WRITE, saved_size, response_time, saved_size >= 0);
      return saved_size;
    }

    int RcClientImpl::fetch_file(const char* ns_addr, const char* local_file,
        const char* file_name, const char* suffix)
    {
      int ret = TFS_SUCCESS;
      int64_t start_time = tbsys::CTimeUtil::getTime();
      ret = TfsClientImplV2::Instance()->fetch_file(local_file,
          file_name, suffix, common::READ_DATA_OPTION_FLAG_NORMAL, ns_addr);
      int64_t response_time = tbsys::CTimeUtil::getTime() - start_time;
      int64_t file_size = 0;
      //TODO get file_size
      add_stat_info(OPER_READ, file_size, response_time, TFS_SUCCESS == ret);
      return ret;
    }

    int RcClientImpl::fetch_buf(const char* ns_addr, int64_t& ret_count, char* buf, const int64_t count,
        const char* file_name, const char* suffix)
    {
      int ret = TFS_SUCCESS;
      int64_t start_time = tbsys::CTimeUtil::getTime();
      ret = TfsClientImplV2::Instance()->fetch_buf(ret_count, buf, count,
          file_name, suffix, ns_addr);
      int64_t response_time = tbsys::CTimeUtil::getTime() - start_time;
      add_stat_info(OPER_READ, ret_count, response_time, TFS_SUCCESS == ret);
      return ret;
    }

    TfsRetType RcClientImpl::login(const uint64_t rc_ip, const char* app_key, const uint64_t app_ip)
    {
      int ret = TFS_SUCCESS;
      if (TFS_SUCCESS == (ret = RcHelper::login(rc_ip, app_key, app_ip,
              session_base_info_.session_id_, base_info_)))
      {
        TBSYS_LOG(DEBUG, "base_info_.ns_cache_info_ = %s", base_info_.ns_cache_info_.c_str());
        calculate_ns_info(base_info_);
        int32_t app_id = 0;
        int64_t session_ip = 0;
        common::SessionUtil::parse_session_id(session_base_info_.session_id_, app_id, session_ip);
        app_id_ = app_id;
        init_stat_ = INIT_LOGINED;
      }
      return ret;
    }

    TfsRetType RcClientImpl::check_init_stat(const bool check_app_id) const
    {
      int ret = TFS_SUCCESS;
      if (init_stat_ != INIT_LOGINED)
      {
        TBSYS_LOG(ERROR, "not inited");
        ret = TFS_ERROR;
      }
      if (check_app_id)
      {
        if (app_id_ <= 0)
        {
          TBSYS_LOG(ERROR, "app_id error");
          ret = TFS_ERROR;
        }
      }
      return ret;
    }
    uint64_t RcClientImpl::get_active_rc_ip(size_t& retry_index) const
    {
      uint64_t active_rc_ip = 0;
      if (retry_index <= base_info_.rc_server_infos_.size())
      {
        if (0 == retry_index)
        {
          active_rc_ip = active_rc_ip_;
        }
        else
        {
          active_rc_ip = base_info_.rc_server_infos_[retry_index - 1];
        }
        retry_index++;
      }
      return active_rc_ip;
    }
    void RcClientImpl::get_ka_info(common::KeepAliveInfo& kainfo)
    {
      kainfo.last_report_time_ = tbsys::CTimeUtil::getTime();
      kainfo.s_base_info_ = session_base_info_;
      kainfo.s_stat_.app_oper_info_.swap(stat_.app_oper_info_);
    }

    void RcClientImpl::add_stat_info(const OperType& oper_type, const int64_t size,
        const int64_t response_time, const bool is_success)
    {
      AppOperInfo appinfo;
      appinfo.oper_type_ = oper_type;
      appinfo.oper_times_ = 1;
      appinfo.oper_rt_ =  response_time;
      if (is_success)
      {
        appinfo.oper_size_ = size;
        appinfo.oper_succ_= 1;
      }
      tbsys::CThreadGuard mutex_guard(&mutex_);
      stat_.app_oper_info_[oper_type] += appinfo;
    }

    int RcClientImpl::open(const char* ns_addr, const char* file_name, const char* suffix,
        const int flag)
    {
      int ret = NULL == ns_addr ? -1 : 0;
      if (0 == ret)
      {
        ret = TfsClientImplV2::Instance()->open(file_name, suffix, ns_addr, flag);
      }
      return ret;
    }

    void RcClientImpl::calculate_ns_info(const common::BaseInfo& base_info)
    {
      tfs_cluster_manager_->clear();
      std::vector<ClusterRackData>::const_iterator it = base_info.cluster_infos_.begin();
      std::vector<ClusterData>::const_iterator cluster_data_it;
      for (; it != base_info.cluster_infos_.end(); it++)
      {
        //every cluster rack
        bool can_write = false;
        cluster_data_it = it->cluster_data_.begin();
        for (; cluster_data_it != it->cluster_data_.end(); cluster_data_it++)
        {
          //every cluster
          assert(0 != cluster_data_it->cluster_stat_);
          //rc server should not give the cluster which stat is 0
          assert(0 != cluster_data_it->access_type_);
          int32_t cluster_id = 0;
          bool is_master = false;
          parse_cluster_id(cluster_data_it->cluster_id_, cluster_id, is_master);

          if (CLUSTER_ACCESS_TYPE_READ_WRITE == cluster_data_it->access_type_)
          {
            can_write = true;
            tfs_cluster_manager_->add_ns_into_write_ns(cluster_data_it->ns_vip_);
          }
          tfs_cluster_manager_->add_ns_into_read_ns(cluster_data_it->ns_vip_, cluster_id);
        }
      }
      cluster_data_it = base_info_.cluster_infos_for_update_.begin();
      // get updatable ns list
      for (; cluster_data_it != base_info_.cluster_infos_for_update_.end(); cluster_data_it++)
      {
        int32_t cluster_id = 0;
        bool is_master = false;
        parse_cluster_id(cluster_data_it->cluster_id_, cluster_id, is_master);

        tfs_cluster_manager_->add_ns_into_unlink_ns(cluster_data_it->ns_vip_, cluster_id, is_master);
      }

      tfs_cluster_manager_->dump();
      return;
    }

    void RcClientImpl::parse_cluster_id(const std::string& cluster_id_str, int32_t& id, bool& is_master)
    {
      //cluster_id_str will be like 'T1M'  'T1B'
      id = 0;
      is_master = false;
      if (cluster_id_str.length() < 3)
      {
        TBSYS_LOG(ERROR, "cluster_id_str error %s", cluster_id_str.c_str());
      }
      else
      {
        id = cluster_id_str[1] - '0';
        is_master = (cluster_id_str[2] == 'M' || cluster_id_str[2] == 'm');
      }
    }

    bool RcClientImpl::have_permission(const char* file_name, const RcClient::RC_MODE mode)
    {
      FSName fsname(file_name);
      int32_t cluster_id = fsname.get_cluster_id();
      uint32_t block_id = fsname.get_block_id();

      return have_permission(cluster_id, block_id, mode);
    }

    bool RcClientImpl::have_permission(const int32_t cluster_id, const uint32_t block_id,
        const RcClient::RC_MODE mode)
    {
      string ns_addr;

      switch (mode)
      {
        case RcClient::READ:
        case RcClient::READ_FORCE:
          ns_addr = tfs_cluster_manager_->get_read_ns_addr_ex(cluster_id, 0);
          break;
        case RcClient::CREATE:
        case RcClient::WRITE:
          if (0 == block_id)
          {
            ns_addr = tfs_cluster_manager_->get_write_ns_addr(0);
          }
          else
          {
            ns_addr = tfs_cluster_manager_->get_unlink_ns_addr_ex(cluster_id, block_id, 0);
          }
      }
      return !ns_addr.empty(); //if we can not find a ns, we have no permission
    }

    int RcClientImpl::gen_fdinfo(const fdInfo& fdinfo)
    {
      int gen_fd = -1;
      map<int, fdInfo>::iterator it;
      tbsys::CThreadGuard mutex_guard(&fd_info_mutex_);
      if (static_cast<int32_t>(fd_infos_.size()) >= MAX_OPEN_FD_COUNT - 1)
      {
        TBSYS_LOG(ERROR, "too much open files");
      }
      else
      {
        while(1)
        {
          if (MAX_FILE_FD == my_fd_)
          {
            my_fd_ = 1;
          }
          gen_fd = my_fd_++;
          it = fd_infos_.find(gen_fd);
          if (it == fd_infos_.end())
          {
            fd_infos_.insert(make_pair(gen_fd, fdinfo));
            break;
          }
        }
      }
      return gen_fd;
    }

    TfsRetType RcClientImpl::remove_fdinfo(const int fd, fdInfo& fdinfo)
    {
      TfsRetType ret = TFS_ERROR;
      map<int, fdInfo>::iterator it;
      tbsys::CThreadGuard mutex_guard(&fd_info_mutex_);
      it = fd_infos_.find(fd);
      if (it != fd_infos_.end())
      {
        fdinfo = it->second;
        fd_infos_.erase(it);
        ret = TFS_SUCCESS;
      }
      return ret;
    }

    TfsRetType RcClientImpl::get_fdinfo(const int fd, fdInfo& fdinfo) const
    {
      TfsRetType ret = TFS_ERROR;
      map<int, fdInfo>::const_iterator it;
      tbsys::CThreadGuard mutex_guard(&fd_info_mutex_);
      it = fd_infos_.find(fd);
      if (it != fd_infos_.end())
      {
        fdinfo = it->second;
        ret = TFS_SUCCESS;
      }
      return ret;
    }

    TfsRetType RcClientImpl::update_fdinfo_offset(const int fd, const int64_t offset)
    {
      TfsRetType ret = TFS_ERROR;
      map<int, fdInfo>::iterator it;
      tbsys::CThreadGuard mutex_guard(&fd_info_mutex_);
      it = fd_infos_.find(fd);
      if (it != fd_infos_.end() && offset >= 0)
      {
        it->second.offset_ = offset;
        ret = TFS_SUCCESS;
      }
      return ret;
    }

    TfsRetType RcClientImpl::update_fdinfo_rawfd(const int fd, const int raw_fd)
    {
      TfsRetType ret = TFS_ERROR;
      map<int, fdInfo>::iterator it;
      tbsys::CThreadGuard mutex_guard(&fd_info_mutex_);
      it = fd_infos_.find(fd);
      if (it != fd_infos_.end() && INVALID_RAW_TFS_FD == it->second.raw_tfs_fd_)
      {
        it->second.raw_tfs_fd_= raw_fd;
        ret = TFS_SUCCESS;
      }
      return ret;
    }


      // for kv meta
      void RcClientImpl::set_kv_rs_addr(const char *rs_addr)
      {
        strncpy(kv_rs_addr_, rs_addr, 128);
      }

      TfsRetType RcClientImpl::put_bucket(const char *bucket_name, const UserInfo &user_info)
      {
        TfsRetType ret = check_init_stat();
        if (TFS_SUCCESS == ret)
        {
          ret = kv_meta_client_->put_bucket(bucket_name, user_info);
        }
        return ret;
      }

      TfsRetType RcClientImpl::put_bucket_acl(const char *bucket_name,
          const UserInfo &user_info, const CANNED_ACL acl)
      {
        TfsRetType ret = check_init_stat();
        if (TFS_SUCCESS == ret)
        {
          ret = kv_meta_client_->put_bucket_acl(bucket_name, user_info, acl);
        }
        return ret;
      }

      TfsRetType RcClientImpl::get_bucket(const char *bucket_name, const char *prefix,
          const char *start_key, const char delimiter, const int32_t limit,
          vector<ObjectMetaInfo> *v_object_meta_info,
          vector<string> *v_object_name,
          set<string> *s_common_prefix,
          int8_t *is_truncated, const UserInfo &user_info)
      {
        TfsRetType ret = check_init_stat();
        if (TFS_SUCCESS == ret)
        {
          ret = kv_meta_client_->get_bucket(bucket_name, prefix, start_key,
              delimiter, limit, v_object_meta_info, v_object_name,
              s_common_prefix, is_truncated, user_info);
        }
        return ret;
      }

      TfsRetType RcClientImpl::del_bucket(const char *bucket_name,
          const UserInfo &user_info)
      {
        TfsRetType ret = check_init_stat();
        if (TFS_SUCCESS == ret)
        {
          ret = kv_meta_client_->del_bucket(bucket_name, user_info);
        }
        return ret;
      }

      TfsRetType RcClientImpl::head_bucket(const char *bucket_name,
          BucketMetaInfo *bucket_meta_info, const UserInfo &user_info)
      {
        TfsRetType ret = check_init_stat();
        if (TFS_SUCCESS == ret)
        {
          ret = kv_meta_client_->head_bucket(bucket_name, bucket_meta_info, user_info);
        }
        return ret;
      }

      TfsRetType RcClientImpl::put_object(const char *bucket_name, const char *object_name,
          const char* local_file, const common::UserInfo &user_info)
      {
        TfsRetType ret = check_init_stat();
        if (TFS_SUCCESS == ret)
        {
          ret = kv_meta_client_->put_object(bucket_name, object_name,
              local_file, user_info);
        }
        return ret;
      }

      int64_t RcClientImpl::pwrite_object(const char *bucket_name, const char *object_name,
          const void *buf, const int64_t offset, const int64_t length,
          const UserInfo &user_info)
      {
        TfsRetType ret = check_init_stat();
        if (TFS_SUCCESS == ret)
        {
          ret = kv_meta_client_->pwrite_object(bucket_name, object_name,
              buf, offset, length, user_info);
        }
        return ret;
      }

      int64_t RcClientImpl::pread_object(const char *bucket_name, const char *object_name,
          void *buf, const int64_t offset, const int64_t length,
          ObjectMetaInfo *object_meta_info, UserMetadata *user_metadata,
          const UserInfo &user_info)
      {
        TfsRetType ret = check_init_stat();
        if (TFS_SUCCESS == ret)
        {
          ret = kv_meta_client_->pread_object(bucket_name, object_name,
              buf, offset, length, object_meta_info, user_metadata,
              user_info);
        }
        return ret;
      }

      TfsRetType RcClientImpl::get_object(const char *bucket_name, const char *object_name,
          const char* local_file, const UserInfo &user_info)
      {
        TfsRetType ret = check_init_stat();
        if (TFS_SUCCESS == ret)
        {
          ObjectMetaInfo object_meta_info;
          UserMetadata user_metadata;
          ret = kv_meta_client_->get_object(bucket_name, object_name,
              local_file, &object_meta_info, &user_metadata, user_info);
        }
        return ret;
      }

      TfsRetType RcClientImpl::del_object(const char *bucket_name, const char *object_name,
          const UserInfo &user_info)
      {
        TfsRetType ret = check_init_stat();
        if (TFS_SUCCESS == ret)
        {
          ret = kv_meta_client_->del_object(bucket_name, object_name, user_info);
        }
        return ret;
      }

      TfsRetType RcClientImpl::head_object(const char *bucket_name, const char *object_name,
          ObjectInfo *object_info, const UserInfo &user_info)
      {
        TfsRetType ret = check_init_stat();
        if (TFS_SUCCESS == ret)
        {
          ret = kv_meta_client_->head_object(bucket_name, object_name,
              object_info, user_info);
        }
        return ret;
      }

      TfsRetType RcClientImpl::set_life_cycle(const int32_t file_type, const char *file_name,
                                              const int32_t invalid_time_s, const char *app_key)
      {
        TfsRetType ret = check_init_stat();
        if (TFS_SUCCESS == ret)
        {
          ret = kv_meta_client_->set_life_cycle(file_type, file_name, invalid_time_s, app_key);
        }
        return ret;
      }

      TfsRetType RcClientImpl::get_life_cycle(const int32_t file_type, const char *file_name,
                                              int32_t* invalid_time_s)
      {
        TfsRetType ret = check_init_stat();
        if (TFS_SUCCESS == ret)
        {
          ret = kv_meta_client_->get_life_cycle(file_type, file_name, invalid_time_s);
        }
        return ret;
      }

      TfsRetType RcClientImpl::rm_life_cycle(const int32_t file_type, const char *file_name)
      {
        TfsRetType ret = check_init_stat();
        if (TFS_SUCCESS == ret)
        {
          ret = kv_meta_client_->rm_life_cycle(file_type, file_name);
        }
        return ret;
      }
      /* ==================kv end==================== */
    }
}
