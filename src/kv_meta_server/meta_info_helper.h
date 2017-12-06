/*
 * (C) 2007-2010 Alibaba Group Holding Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 * Version: $Id: meta_server_service.h 49 2011-08-08 09:58:57Z nayan@taobao.com $
 *
 * Authors:
 *   daoan <daoan@taobao.com>
 *      - initial release
 *
 */

#ifndef TFS_KVMETASERVER_META_INFO_HELPER_H_
#define TFS_KVMETASERVER_META_INFO_HELPER_H_

#include "common/parameter.h"
#include "common/base_service.h"
#include "common/status_message.h"
#include "common/kv_meta_define.h"
#include "common/kvengine_helper.h"
#include "message/message_factory.h"

namespace tfs
{
  namespace kvmetaserver
  {
    class MetaInfoHelper
    {
      public:
        MetaInfoHelper();
        virtual ~MetaInfoHelper();
        int init(common::KvEngineHelper*);
        int put_meta(const std::string& bucket_name, const std::string& file_name,
            /*const int64_t offset,*/ const common::TfsFileInfo& tfs_file_info
            /* const taglist , versioning*/
            );
        int get_meta(const std::string& bucket_name, const std::string& file_name,
            /*const int64_t offset,*/ /*const get_tag_list*/ common::TfsFileInfo* tfs_file_info
            /*taglist* */);
        //int scan_metas();
        //int delete_metas();

      /*----------------------------get service-----------------------------*/
        int list_buckets(common::BucketsResult *buckets_result, const common::UserInfo &user_info);

      /*----------------------------object part-----------------------------*/

        int head_object(const std::string &bucket_name,
                        const std::string &file_name,
                        const common::UserInfo &user_info,
                        common::ObjectInfo *object_info_zero);

        int put_object(const std::string& bucket_name,
                       const std::string& file_name,
                       common::ObjectInfo &object_info,
                       const common::UserInfo &user_info);

        void check_put_object_zero(common::ObjectInfo &object_info,
                     common::ObjectInfo &object_info_zero,
                     const common::UserInfo &user_info,
                     int64_t &offset, int64_t &length,
                     bool &is_append);

        bool check_put_object_part(common::ObjectInfo &object_info,
                     const int64_t offset, const bool is_append);

        int put_object_user_metadata(const std::string& bucket_name,
                       const std::string& file_name,
                       const common::UserInfo &user_info,
                       const common::UserMetadata &user_metadata);

        int get_object(const std::string& bucket_name,
                       const std::string& file_name,
                       const int64_t offset, const int64_t length,
                       const common::UserInfo &user_info,
                       common::ObjectInfo *object_info, bool *still_have);

        int del_object(const std::string& bucket_name,
                       const std::string& file_name,
                       const common::UserInfo &user_info,
                       common::ObjectInfo *object_info, bool* still_have);

        int del_object_user_metadata(const std::string& bucket_name,
                      const std::string& file_name,
                      const common::UserInfo &user_info);

        /*----------------------------bucket part-----------------------------*/

        int head_bucket(const std::string& bucket_name, const common::UserInfo &user_info,
                        common::BucketMetaInfo *bucket_meta_info);

        static int get_common_prefix(const char *key, const std::string &prefix, const char delimiter,
            bool *prefix_flag, bool *common_flag, int *common_end_pos);

        int put_bucket(const std::string& bucket_name, common::BucketMetaInfo& bucket_meta_info,
                       const common::UserInfo &user_info, const common::CANNED_ACL acl);

        int get_bucket(const std::string& bucket_name, const std::string& prefix,
            const std::string& start_key, const char delimiter, int32_t *limit,
            const common::UserInfo &user_info,
            std::vector<common::ObjectMetaInfo>* v_object_meta_info, common::VSTRING* v_object_name,
            std::set<std::string>* s_common_prefix, int8_t* is_truncated);

        int del_bucket(const std::string& bucket_name, const common::UserInfo &user_info);

        int put_bucket_acl(const std::string &bucket_name,
            const common::MAP_INT64_INT &bucket_acl_map, const common::UserInfo &user_info);

        int put_bucket_acl(const std::string &bucket_name, const common::CANNED_ACL acl,
            const common::UserInfo &user_info);

        int get_bucket_acl(const std::string &bucket_name,
            common::MAP_INT64_INT *bucket_acl_map, const common::UserInfo &user_info, int64_t &owner_id);

      public:
        int put_bucket_ex(const std::string &bucket_name, const common::BucketMetaInfo &bucket_meta_info,
            const int64_t lock_version = 0);
        int put_object_ex(const std::string &bucket_name, const std::string &file_name,
            const int64_t offset, const common::ObjectInfo &object_info, const int64_t lock_version = 0);

        int get_object_part(const std::string &bucket_name,
                           const std::string &file_name,
                           const int64_t offset,
                           common::ObjectInfo *object_info,
                           int64_t *version);

        int deserialize_key(const char *key, const int32_t key_size, std::string *bucket_name, std::string *object_name,
            int64_t *offset, int64_t *version);
        int serialize_key(const std::string &bucket_name,
                        const std::string &file_name, const int64_t offset,
                        common::KvKey *key, char *key_buff, const int32_t buff_size, int32_t key_type);
        int serialize_key_ex(const std::string &file_name, const int64_t offset,
                        common::KvKey *key, char *key_buff, const int32_t buff_size, int32_t key_type);

      protected:
        int get_bucket_list(const int64_t owner_id, std::set<std::string> *s_bucket_list, int64_t *version);
        int put_bucket_list(const int64_t owner_id, const std::set<std::string> &s_bucket_list,
            const int64_t version);
        void dump_bucket_list(const int64_t owner_id, const std::set<std::string> &s_bucket_list);
        void dump_acl_map(const common::MAP_INT64_INT acl_map);

        int group_objects(const std::string &object_name, const std::string &v, const std::string &prefix,
            const char delimiter, std::vector<common::ObjectMetaInfo> *v_object_meta_info,
            std::vector<std::string> *v_object_name, std::set<std::string> *s_common_prefix);

        int put_object_part(const std::string &bucket_name, const std::string &file_name,
            const common::ObjectInfo &object_info);

        int put_object_zero(const std::string &bucket_name, const std::string &file_name,
            common::ObjectInfo *object_info_zero, int64_t *offset,
            const int64_t length, int64_t version, bool is_append);

        int get_range(const common::KvKey &pkey, const std::string &start_key,
            int32_t offset, const int32_t limit, std::vector<common::KvValue*> *kv_value_keys,
            std::vector<common::KvValue*> *kv_value_values, int32_t *result_size);

        int list_objects(const common::KvKey &pkey, const std::string &prefix,
            const std::string &start_key, const char delimiter, int32_t *limit,
            std::vector<common::ObjectMetaInfo> *v_object_meta_info, common::VSTRING *v_object_name,
            std::set<std::string> *s_common_prefix, int8_t *is_truncated);

        int scan_pre_record(const std::string &bucket_name,
        const std::string& file_name, const common::KvKey& start_key,
        common::ObjectInfo *object_info, int32_t& valid_result);

        int do_canned_acl(const common::CANNED_ACL acl, common::MAP_INT64_INT &bucket_acl_map,
                          const int64_t owner_id);

        bool is_appid_uid_bucket_name(const std::string& bucket_name);
        int check_bucket_acl(const common::MAP_INT64_INT &bucket_acl_map, int64_t owner_id, common::PERMISSION per);
        int check_bucket_acl(const std::string& bucket_name, const common::UserInfo &user_info, common::PERMISSION per);

        int head_bucket_ex(const std::string& bucket_name, common::BucketMetaInfo *bucket_meta_info);

        int check_object_overlap(const std::string &bucket_name,
            const std::string &file_name, const int64_t offset, const int64_t length);

      protected:
        common::KvEngineHelper* kv_engine_helper_;
        int32_t meta_info_name_area_;
      private:
        DISALLOW_COPY_AND_ASSIGN(MetaInfoHelper);
    };
  }
}

#endif
