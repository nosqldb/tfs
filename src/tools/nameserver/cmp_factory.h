/*
 * (C) 2007-2010 Alibaba Group Holding Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 * Version: $Id: func.cpp 400 2011-06-02 07:26:40Z duanfei@taobao.com $
 *
 * Authors:
 *   chuyu <chuyu@taobao.com>
 *      - initial release
 *
 */
#ifndef TFS_TOOLS_CMPFACTORY_H_
#define TFS_TOOLS_CMPFACTORY_H_

#include <stdio.h>
#include <map>
#include <bitset>
#include "common.h"

namespace tfs
{
  namespace tools
  {
    static const int32_t MAX_BITS_SIZE = 81920;
    template<class V> void cmp_disorder_container(const V& container_a, const V& container_b, std::bitset<MAX_BITS_SIZE>& flag_a, std::bitset<MAX_BITS_SIZE>& flag_b);
    template<class V> void cmp_order_container(const V& container_a, const V& container_b, std::bitset<MAX_BITS_SIZE>& flag_a, std::bitset<MAX_BITS_SIZE>& flag_b);
    template<class V> void print_container(V& container, std::bitset<MAX_BITS_SIZE>& flag, bool reverse, bool is_servercmp);
    class ServerCmp : public ServerBase
    {
      public:
        ServerCmp();
        virtual ~ServerCmp();
        uint64_t get_id() const
        {
          return server_stat_.id_;
        }
        int cmp(ServerCmp& server_b, const int8_t type);
        void dump(ServerCmp& server_b, const int8_t type);
      protected:
        void dump(const int8_t type, bool is_master);
        std::bitset<MAX_BITS_SIZE> flag_;
    };
    class BlockCmp : public BlockBase
    {
      public:
        BlockCmp();
        virtual ~BlockCmp();
        uint64_t get_id() const
        {
          return info_.block_id_;
        }
        int cmp(BlockCmp& b, const int8_t type);
        void dump(BlockCmp& block_b, const int8_t type);
      protected:
        void dump(const int8_t type);
        std::bitset<MAX_BITS_SIZE> flag_;
    };
    class StatCmp
    {
      public:
        StatCmp();
        ~StatCmp();
        template<class T> void push_back(T i, PushType p_type, bool is_servercmp)
        {
          switch (p_type) {
            case PUSH_MORE :
              !is_servercmp ? more_block_.push_back(i) : more_server_.push_back(i);
              break;
            case PUSH_LESS :
              !is_servercmp ? less_block_.push_back(i) : less_server_.push_back(i);
              break;
            default:
              break;
          }
        }

        void print_stat(const int8_t type);
        int64_t diff_count_;
        int64_t total_count_;
      private:
        std::vector<uint64_t> more_server_;
        std::vector<uint64_t> less_server_;
        std::vector<uint64_t> more_block_;
        std::vector<uint64_t> less_block_;
    };
  }
}
#endif
