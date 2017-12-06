/*
 * (C) 2007-2010 Alibaba Group Holding Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 * Authors:
 *   zhuhui <zhuhui_a.pt@taobao.com>
 *      - initial release
 *   linqing <linqing.zyd@taobao.com>
 *      - modify 2013-07-12
 *
 */
#ifndef TFS_CLIENTV2_TFSSESSION_POOL_H_
#define TFS_CLIENTV2_TFSSESSION_POOL_H_

#include <map>
#include <tbsys.h>
#include <Mutex.h>
#include "Timer.h"
#include "tfs_session.h"

namespace tfs
{
  namespace clientv2
  {
    typedef std::map<uint64_t, int32_t> VersionMap;

    class TfsSessionPool
    {
      typedef std::map<std::string, TfsSession*> SESSION_MAP;
      public:
        TfsSessionPool(tbutil::TimerPtr timer, const VersionMap* version_map = NULL);
        virtual ~TfsSessionPool();

        TfsSession* get(const char* ns_addr,
            const int64_t cache_time = ClientConfig::cache_time_,
            const int64_t cache_items = ClientConfig::cache_items_);
        void put(TfsSession* session);

      private:
        DISALLOW_COPY_AND_ASSIGN( TfsSessionPool);
        tbutil::Mutex mutex_;
        SESSION_MAP pool_;
        tbutil::TimerPtr timer_;
        VersionMap version_map_;
    };
  }
}

#endif /* TFSSESSION_POOL_H_ */
