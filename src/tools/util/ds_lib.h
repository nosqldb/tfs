/*
 * (C) 2007-2010 Alibaba Group Holding Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 * Version: $Id: ds_lib.h 413 2011-06-03 00:52:46Z daoan@taobao.com $
 *
 * Authors:
 *   jihe
 *      - initial release
 *   chuyu <chuyu@taobao.com>
 *      - modify 2010-03-20
 *   zhuhui <zhuhui_a.pt@taobao.com>
 *      - modify 2010-04-23
 *
 */
#ifndef TFS_TOOLS_UTIL_DS_LIB_H_
#define TFS_TOOLS_UTIL_DS_LIB_H_

#include "common/internal.h"

namespace tfs
{
  namespace tools
  {
    struct BlockVisit
    {
      BlockVisit(uint64_t block_id, int64_t total_visit_count, int64_t last_access_time) :
        block_id_(block_id), total_visit_count_(total_visit_count), last_access_time_(last_access_time)
      {}
      bool operator < (const BlockVisit& b) const
      {
        if (total_visit_count_ != b.total_visit_count_)
          return total_visit_count_ < b.total_visit_count_;// asc
        else
          return last_access_time_ < b.last_access_time_;// asc
      }
      uint64_t block_id_;
      int64_t total_visit_count_;
      int64_t last_access_time_;
    };

    class DsLib
    {
    public:
      DsLib()
      {
      }

      ~DsLib()
      {
      }

      static int get_server_status(common::DsTask& ds_task);
      static int get_ping_status(common::DsTask& ds_task);
      static int new_block(common::DsTask& ds_task);
      static int remove_block(common::DsTask& ds_task);
      static int list_block(common::DsTask& list_block_task);
      static int get_block_info(common::DsTask& list_block_task);
      static int reset_block_version(common::DsTask& list_block_task);
      static int create_file_id(common::DsTask& ds_task);
      static int list_file(common::DsTask& ds_task);
      static int check_file_info(common::DsTask& ds_task);
      static int read_file_data(common::DsTask& ds_task);
      static int verify_file_data(common::DsTask& ds_task);
      static int write_file_data(common::DsTask& ds_task);
      static int write_file_data_v2(common::DsTask& ds_task);
      static int unlink_file(common::DsTask& ds_task);
      static int unlink_file_v2(common::DsTask& ds_task, uint64_t& lease_id, bool prepare);
      static int rename_file(common::DsTask& ds_task);
      static int read_file_info(common::DsTask& ds_task);
      static int read_file_info_v2(common::DsTask& ds_task);
      static int list_bitmap(common::DsTask& ds_task);
      static int send_crc_error(common::DsTask& ds_task);
      static int get_blocks_index_header(common::DsTask& ds_task, std::vector<common::IndexHeaderV2>& blocks_header);

    private:
      static void print_file_info(const char* name, common::FileInfo& file_info);
      static void print_file_info_v2(const char* name, common::FileInfoV2& file_info);
      static int write_data(const uint64_t server_id, const uint32_t block_id, const char* data, const int32_t length,
                            const int32_t offset, const uint64_t file_id, const uint64_t file_num);
      static int write_data_v2(const uint64_t server_id, const uint64_t block_id, const char* data, const int32_t length,
                                  const int32_t offset, uint64_t& file_id, uint64_t& lease_id);
      static int create_file_num(const uint64_t server_id, const uint32_t block_id, const uint64_t file_id, uint64_t& new_file_id,
                                 int64_t& file_num);
      static int close_data(const uint64_t server_id, const uint32_t block_id, const uint32_t crc, const uint64_t file_id,
                            const uint64_t file_num);
      static int close_data_v2(const uint64_t server_id, const uint64_t block_id, const uint32_t crc, const uint64_t file_id, uint64_t lease_id);
    };
  }
}
#endif
