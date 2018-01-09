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


#include <malloc.h>
#include "meta_info_helper.h"
using namespace std;
namespace tfs
{
  using namespace common;
  namespace kvmetaserver
  {
    const int TFS_INFO_BUFF_SIZE = 128;
    const int VALUE_BUFF_SIZE = 1024*1024;
    const int KV_VALUE_BUFF_SIZE = 1024*1024;
    const int32_t KEY_BUFF_SIZE = 64 + 512 + 8 + 8 + 8; // bucket_name(64) object_name(512) offset(8) version(8) delimiter(8)
    const int32_t SCAN_LIMIT_FOR_OVERLAP = 3;
    const int32_t SCAN_LIMIT = 20;
    const int32_t MESS_LIMIT = 10;
    const int64_t INT64_INFI = 0x7FFFFFFFFFFFFFFF;
    const int32_t GET_BUCKET_KV_SCAN_MAX_NUM = 20;//every time take 0.08s
    const int32_t OTHER_ROLE = -1;
    const char APPID_UID_BUCKET_NAME_DELIMITER = '^';
    enum
    {
      MODE_REQ_LIMIT = 1,
      MODE_KV_LIMIT = 2,
    };
    enum
    {
      CMD_RANGE_ALL = 1,
      CMD_RANGE_VALUE_ONLY,
      CMD_RANGE_KEY_ONLY,
    };
    MetaInfoHelper::MetaInfoHelper()
    {
      kv_engine_helper_ = NULL;
      meta_info_name_area_ = 0;
    }

    MetaInfoHelper::~MetaInfoHelper()
    {
      kv_engine_helper_ = NULL;
    }

    int MetaInfoHelper::init(common::KvEngineHelper* kv_engine_helper)
    {
      int ret = TFS_SUCCESS;
      kv_engine_helper_ = kv_engine_helper;

      //TODO change later
      meta_info_name_area_ = SYSPARAM_KVMETA.object_area_;
      if (meta_info_name_area_ <= 0 )
      {
        TBSYS_LOG(ERROR, "area error %d", meta_info_name_area_);
        ret = TFS_ERROR;
      }
      return ret;
    }

    int MetaInfoHelper::serialize_key(const std::string &bucket_name, const std::string &file_name,
                                      const int64_t offset, KvKey *key, char *key_buff, const int32_t buff_size, int32_t key_type)
    {
      int ret = (bucket_name.size() > 0 && file_name.size() > 0 && offset >= 0
                 && key != NULL &&  key_buff != NULL ) ? TFS_SUCCESS : TFS_ERROR;
      int64_t pos = 0;
      if(TFS_SUCCESS == ret)
      {
        //bucket
        int64_t bucket_name_size = static_cast<int64_t>(bucket_name.size());
        if(pos + bucket_name_size < buff_size)
        {
          memcpy(key_buff + pos, bucket_name.data(), bucket_name.size());
          pos += bucket_name_size;
        }
        else
        {
          ret = TFS_ERROR;
        }
        //DELIMITER
        if (TFS_SUCCESS == ret && (pos + 1) < buff_size)
        {
          key_buff[pos++] = KvKey::DELIMITER;
        }
        else
        {
          ret = TFS_ERROR;
        }
        //filename
        int64_t file_name_size = static_cast<int64_t>(file_name.size());
        if(pos + file_name_size < buff_size)
        {
          memcpy(key_buff + pos, file_name.data(), file_name.size());
          pos += file_name_size;
        }
        else
        {
          ret = TFS_ERROR;
        }
        //DELIMITER
        if (TFS_SUCCESS == ret && (pos + 1) < buff_size)
        {
          key_buff[pos++] = KvKey::DELIMITER;
        }
        else
        {
          ret = TFS_ERROR;
        }
        //version
        int64_t version = 0;
        if (TFS_SUCCESS == ret && (pos + 8) < buff_size)
        {
          ret = Serialization::int64_to_char(key_buff + pos, buff_size, version);
          pos = pos + 8;
        }
        //DELIMITER
        if (TFS_SUCCESS == ret && (pos + 1) < buff_size)
        {
          key_buff[pos++] = KvKey::DELIMITER;
        }
        else
        {
          ret = TFS_ERROR;
        }
        //offset
        if (TFS_SUCCESS == ret && (pos + 8) < buff_size)
        {
          ret = Serialization::int64_to_char(key_buff + pos, buff_size, offset);
          pos = pos + 8;
        }
      }

      if (TFS_SUCCESS == ret)
      {
        key->key_ = key_buff;
        key->key_size_ = pos;
        key->key_type_ = key_type;
      }
      return ret;
    }

    int MetaInfoHelper::serialize_key_ex(const std::string &file_name, const int64_t offset, KvKey *key,
        char *key_buff, const int32_t buff_size, int32_t key_type)
    {
      TBSYS_LOG(DEBUG, "part offset: %" PRI64_PREFIX "d", offset);

      int ret = (file_name.size() > 0 && offset >= 0
                 && key != NULL &&  key_buff != NULL ) ? TFS_SUCCESS : TFS_ERROR;
      int64_t pos = 0;
      if (TFS_SUCCESS == ret)
      {
        //filename
        int64_t file_name_size = static_cast<int64_t>(file_name.size());
        if(pos + file_name_size < buff_size)
        {
          memcpy(key_buff + pos, file_name.data(), file_name.size());
          pos += file_name_size;
        }
        else
        {
          ret = TFS_ERROR;
        }
        //DELIMITER
        if (TFS_SUCCESS == ret && (pos + 1) < buff_size)
        {
          key_buff[pos++] = KvKey::DELIMITER;
        }
        else
        {
          ret = TFS_ERROR;
        }
        //version
        int64_t version = 0;
        if (TFS_SUCCESS == ret && (pos + 8) < buff_size)
        {
          ret = Serialization::int64_to_char(key_buff + pos, buff_size, version);
          pos = pos + 8;
        }
        //DELIMITER
        if (TFS_SUCCESS == ret && (pos + 1) < buff_size)
        {
          key_buff[pos++] = KvKey::DELIMITER;
        }
        else
        {
          ret = TFS_ERROR;
        }
        //offset
        if (TFS_SUCCESS == ret && (pos + 8) < buff_size)
        {
          ret = Serialization::int64_to_char(key_buff + pos, buff_size, offset);
          pos = pos + 8;
        }
      }

      if (TFS_SUCCESS == ret)
      {
        key->key_ = key_buff;
        key->key_size_ = pos;
        key->key_type_ = key_type;
      }
      return ret;
    }


    /*----------------------------object part-----------------------------*/
    int MetaInfoHelper::head_object(const string &bucket_name,
        const string &file_name, const common::UserInfo &user_info,
        ObjectInfo *object_info_zero)
    {
      int ret = (bucket_name.size() > 0 && file_name.size() > 0 &&
          NULL != object_info_zero) ? TFS_SUCCESS : TFS_ERROR;

      if (TFS_SUCCESS == ret)
      {
        ret = check_bucket_acl(bucket_name, user_info, READ);
      }
      if (TFS_SUCCESS == ret)
      {
        ret = get_object_part(bucket_name, file_name, 0, object_info_zero, NULL);
        object_info_zero->dump();
      }

      return ret;
    }

    int MetaInfoHelper::put_object_ex(const string &bucket_name, const string &file_name,
        const int64_t offset, const ObjectInfo &object_info, const int64_t lock_version)
    {
      //op key
      int ret = TFS_SUCCESS;
      char *key_buff = (char*)malloc(KEY_BUFF_SIZE);
      char *value_buff = (char*)malloc(VALUE_BUFF_SIZE);

      if(NULL == key_buff || NULL == value_buff)
      {
        ret = TFS_ERROR;
      }
      KvKey key;
      if (TFS_SUCCESS == ret)
      {
        ret = serialize_key(bucket_name, file_name, offset, &key, key_buff, KEY_BUFF_SIZE, KvKey::KEY_TYPE_OBJECT);
      }

      //op value
      int64_t pos = 0;
      if (TFS_SUCCESS == ret)
      {
        ret = object_info.serialize(value_buff, VALUE_BUFF_SIZE, pos);
      }

      KvMemValue kv_value;
      if (TFS_SUCCESS == ret)
      {
        kv_value.set_data(value_buff, pos);
        ret = kv_engine_helper_->put_key(meta_info_name_area_, key, kv_value, lock_version);
      }

      if (NULL != value_buff)
      {
        free(value_buff);
        value_buff = NULL;
      }

      if (NULL != key_buff)
      {
        free(key_buff);
        key_buff = NULL;
      }

      return ret;
    }

    int MetaInfoHelper::put_object_part(const string &bucket_name, const string &file_name,
        const ObjectInfo &object_info)
    {
      int ret = TFS_SUCCESS;
      ObjectInfo object_info_part;
      int64_t offset = 0;
      for (size_t i = 0; i < object_info.v_tfs_file_info_.size(); i++)
      {
        offset = object_info.v_tfs_file_info_[i].offset_;
        // skip object info zero
        if (0 == offset)
        {
          continue;
        }

        object_info_part.v_tfs_file_info_.clear();
        object_info_part.v_tfs_file_info_.push_back(object_info.v_tfs_file_info_[i]);

        TBSYS_LOG(DEBUG, "will put object part, bucekt_name: %s, object_name: %s",
            bucket_name.c_str(), file_name.c_str());
        object_info_part.dump();
        ret = put_object_ex(bucket_name, file_name, offset, object_info_part, 0);

        if (TFS_SUCCESS != ret)
        {
          TBSYS_LOG(ERROR, "put bucket_name: %s, file_name: %s, offset: %" PRI64_PREFIX "d part fail",
              bucket_name.c_str(), file_name.c_str(), offset);
          break;
        }
      }

      return ret;
    }

    bool MetaInfoHelper::check_put_object_part(ObjectInfo &object_info, const int64_t offset, const bool is_append)
    {
      bool need_put_part = false;

      if (object_info.v_tfs_file_info_.size() > 1
          || 0 != offset)
      {
        need_put_part = true;
      }
      if (need_put_part)
      {
        if (is_append)
        {
          // update offsets
          int64_t tmp_offset = offset;
          for (size_t i = 0; i < object_info.v_tfs_file_info_.size(); i++)
          {
            object_info.v_tfs_file_info_[i].offset_ = tmp_offset;
            tmp_offset += object_info.v_tfs_file_info_[i].file_size_;
          }
        }

      }

      return need_put_part;
    }

    int MetaInfoHelper::put_object_zero(const string &bucket_name,
        const string &file_name, ObjectInfo *object_info_zero,
        int64_t *offset, const int64_t length, int64_t version,
        const bool is_append)
    {
      int ret = TFS_SUCCESS;
      int32_t retry = KvDefine::VERSION_ERROR_RETRY_COUNT;
      bool is_old_data = false;
      //-5 means transfer data from mysql
      if (-5 == object_info_zero->meta_info_.max_tfs_file_size_)
      {
        is_old_data = true;
        object_info_zero->meta_info_.max_tfs_file_size_ = 2048;
      }
      do
      {
        if (!is_old_data)
        {
          object_info_zero->meta_info_.modify_time_ = static_cast<int64_t>(time(NULL));
        }

        TBSYS_LOG(DEBUG, "will put object info zero, bucket: %s, object: %s",
            bucket_name.c_str(), file_name.c_str());
        object_info_zero->dump();
        ret = put_object_ex(bucket_name, file_name, 0, *object_info_zero, version);
        if (EXIT_KV_RETURN_VERSION_ERROR == ret)
        {
          TBSYS_LOG(INFO, "put object zero version conflict, bucket: %s, object: %s",
              bucket_name.c_str(), file_name.c_str());
          ObjectInfo curr_object_info_zero;
          int tmp_ret = get_object_part(bucket_name, file_name, 0, &curr_object_info_zero, &version);

          if (TFS_SUCCESS != tmp_ret)
          {
            TBSYS_LOG(WARN, "get object zero fail, ret: %d, bucket: %s, object: %s",
                tmp_ret, bucket_name.c_str(), file_name.c_str());
            break;
          }
          else
          {
            if (is_append)
            {
              // assumption failed
              if (curr_object_info_zero.meta_info_.big_file_size_ > 0)
              {
                object_info_zero->v_tfs_file_info_ = curr_object_info_zero.v_tfs_file_info_;
              }
              // get real offset
              *offset = curr_object_info_zero.meta_info_.big_file_size_;
              object_info_zero->meta_info_ = curr_object_info_zero.meta_info_;
              object_info_zero->meta_info_.big_file_size_ += length;
            }
            else
            {
              if (0 != *offset)
              {
                object_info_zero->v_tfs_file_info_ = curr_object_info_zero.v_tfs_file_info_;
                object_info_zero->meta_info_ = curr_object_info_zero.meta_info_;
              }
              else
              {
                if (!curr_object_info_zero.v_tfs_file_info_.empty()
                    && !object_info_zero->v_tfs_file_info_.empty())
                {
                  TBSYS_LOG(WARN, "object info zero conflict found");
                }
              }

              int64_t curr_file_size = curr_object_info_zero.meta_info_.big_file_size_;
              object_info_zero->meta_info_.big_file_size_ =
                curr_file_size > (*offset + length) ? curr_file_size : (*offset + length);
            }
            if (!object_info_zero->has_user_metadata_)
            {
              object_info_zero->has_user_metadata_ = curr_object_info_zero.has_user_metadata_;
              object_info_zero->user_metadata_ = curr_object_info_zero.user_metadata_;
            }
          }
        }
      }while (retry-- && EXIT_KV_RETURN_VERSION_ERROR == ret);

      return ret;
    }

    void MetaInfoHelper::check_put_object_zero(ObjectInfo &object_info, ObjectInfo &object_info_zero, const UserInfo &user_info, int64_t &offset, int64_t &length, bool &is_append)
    {
      //ObjectInfo object_info_zero;
      //int64_t offset = 0;
      if (object_info.v_tfs_file_info_.size() > 0)
      {
        offset = object_info.v_tfs_file_info_.front().offset_;
      }

      is_append = (-1 == offset);
      // assume this is object info zero
      if (is_append)
      {
        offset = 0;
        if (!object_info.v_tfs_file_info_.empty())
        {
          object_info.v_tfs_file_info_[0].offset_ = 0;
        }
      }

      /*trick for transfer data*/
      if (-5 == object_info.meta_info_.max_tfs_file_size_)
      {
        object_info_zero.meta_info_.modify_time_ = object_info.meta_info_.modify_time_;
        object_info_zero.meta_info_.max_tfs_file_size_ = -5;
      }

      length = 0;
      for (size_t i = 0; i < object_info.v_tfs_file_info_.size(); i++)
      {
        length += object_info.v_tfs_file_info_[i].file_size_;
      }

      //TBSYS_LOG(DEBUG, "will put object, bucekt: %s, object: %s, "
      //"offset: %" PRI64_PREFIX "d, length: %" PRI64_PREFIX "d",
      //bucket_name.c_str(), file_name.c_str(), offset, length);

      object_info_zero.has_meta_info_ = true;
      object_info_zero.meta_info_.big_file_size_ = offset + length;
      if (0 == offset)
      {
        object_info_zero.meta_info_.owner_id_ = user_info.owner_id_;
        // identify old data
        if (-5 == object_info.meta_info_.max_tfs_file_size_)
        {
          object_info_zero.meta_info_.create_time_ = object_info.meta_info_.create_time_;
        }
        else
        {
          object_info_zero.meta_info_.create_time_ = static_cast<int64_t>(time(NULL));
        }

        object_info_zero.has_user_metadata_ = object_info.has_user_metadata_;
        object_info_zero.user_metadata_ = object_info.user_metadata_;

        if (!object_info.v_tfs_file_info_.empty())
        {
          object_info_zero.v_tfs_file_info_.push_back(object_info.v_tfs_file_info_.front());
        }
      }

      return;
    }

    int MetaInfoHelper::check_object_overlap(const std::string &bucket_name,
        const std::string &file_name, const int64_t offset,
        const int64_t length)
    {
      int ret = (!bucket_name.empty() && !file_name.empty()) ? TFS_SUCCESS : TFS_ERROR;
      common::ObjectInfo object_info_zero;
      TBSYS_LOG(DEBUG, "will check coverage");
      //op key
      char start_key_buff[KEY_BUFF_SIZE];
      char end_key_buff[KEY_BUFF_SIZE];

      KvKey start_key;
      KvKey end_key;
      int64_t start_offset = offset;
      int64_t end_offset = offset + length - 1;
      if (TFS_SUCCESS == ret)
      {
        ret = serialize_key(bucket_name, file_name, start_offset,
            &start_key, start_key_buff, KEY_BUFF_SIZE, KvKey::KEY_TYPE_OBJECT);
      }
      if (TFS_SUCCESS == ret)
      {
        ret = serialize_key(bucket_name, file_name, end_offset,
            &end_key, end_key_buff, KEY_BUFF_SIZE, KvKey::KEY_TYPE_OBJECT);
      }

      //op value

      int32_t i;
      int32_t scan_offset = 0;
      short scan_type = CMD_RANGE_VALUE_ONLY;//only scan value
      vector<KvValue*> kv_value_keys;
      vector<KvValue*> kv_value_values;
      common::ObjectInfo object_info;
      int32_t valid_result = 0;
      int32_t result_size = 0;

      ret = kv_engine_helper_->scan_keys(meta_info_name_area_, start_key, end_key, SCAN_LIMIT_FOR_OVERLAP, scan_offset,
          &kv_value_keys, &kv_value_values, &result_size, scan_type);

      TBSYS_LOG(DEBUG, "scan overlap frag result_size is: %d", result_size);
      if (EXIT_KV_RETURN_DATA_NOT_EXIST == ret || TFS_SUCCESS == ret)
      {
        if (0 == result_size)
        {
          /* if offset == 0 no need find pre */
          if (start_offset > 0)
          {
            //we should find pre record
            ret = scan_pre_record(bucket_name, file_name, start_key, &object_info, valid_result);
            /* this ret always return TFS_SUCCESS */
            if (valid_result > 0)
            {
              if (object_info.v_tfs_file_info_[0].offset_ + object_info.v_tfs_file_info_[0].file_size_ > offset)
              {
                ret = EXIT_OBJECT_OVERLAP;
                TBSYS_LOG(DEBUG, "pre frag overlap ret is: %d", ret);
              }
              else
              {
                ret = TFS_SUCCESS;
              }
            }
          }
          else
          {
            ret = TFS_SUCCESS;
          }
        }
        else if (1 == result_size) /* need judge kong file */
        {
          common::ObjectInfo tmp_object_info;
          //value get
          int64_t pos = 0;
          tmp_object_info.deserialize(kv_value_values[0]->get_data(),
              kv_value_values[0]->get_size(), pos);
          /* is head frag && data is kong*/
          if (tmp_object_info.has_meta_info_ && tmp_object_info.v_tfs_file_info_.size() == 0)
          {
            ret = TFS_SUCCESS;
          }
          else
          {
            ret = EXIT_OBJECT_OVERLAP;
            TBSYS_LOG(DEBUG, "may be first frag overlap ret is: %d", ret);
          }
        }
        else /* has frag > 1 */
        {
          ret = EXIT_OBJECT_OVERLAP;
          TBSYS_LOG(DEBUG, "overlap ret is: %d", ret);
        }
      }

      for(i = 0; i < result_size; ++i)//free kv
      {
        kv_value_values[i]->free();
        kv_value_keys[i]->free();
      }
      kv_value_values.clear();
      kv_value_keys.clear();

      return ret;
    }

    int MetaInfoHelper::put_object(const std::string &bucket_name,
        const std::string &file_name,
        ObjectInfo &object_info, const UserInfo &user_info)
    {
      int64_t offset = 0;
      int64_t version = KvDefine::MAX_VERSION;
      bool is_append = 0;
      int64_t length = 0;
      ObjectInfo object_info_zero;

      TBSYS_LOG(DEBUG, "put object:%s to bucket:%s, user_id:%" PRI64_PREFIX "d",
          file_name.c_str(), bucket_name.c_str(), user_info.owner_id_);

      int ret = (bucket_name.size() > 0 && file_name.size() > 0) ? TFS_SUCCESS : TFS_ERROR;

      if (TFS_SUCCESS == ret)
      {
        ret = check_bucket_acl(bucket_name, user_info, WRITE);
      }

      /* check object zero info */
      if (TFS_SUCCESS == ret)
      {
        check_put_object_zero(object_info, object_info_zero, user_info, offset, length, is_append);
      }

      /* Ban overlap */
      if (TFS_SUCCESS == ret && !is_append)
      {
        ret = check_object_overlap(bucket_name, file_name, offset, length);
      }

      if (TFS_SUCCESS == ret)
      {
        ret = put_object_zero(bucket_name, file_name, &object_info_zero,
            &offset, length, version, is_append);

        if (TFS_SUCCESS == ret)
        {
          bool need_put_part = check_put_object_part(object_info, offset, is_append);

          if (need_put_part)
          {
            ret = put_object_part(bucket_name, file_name, object_info);
            TBSYS_LOG(DEBUG, "put object part ret: %d, bucekt_name: %s, object_name: %s, "
                "offset: %" PRI64_PREFIX "d, length: %" PRI64_PREFIX "d",
                ret, bucket_name.c_str(), file_name.c_str(), offset, length);
          }
        }
        else
        {
          TBSYS_LOG(ERROR, "put object zero failed, ret: %d, bucekt: %s, object: %s, "
              "offset: %" PRI64_PREFIX "d, length: %" PRI64_PREFIX "d",
              ret, bucket_name.c_str(), file_name.c_str(), offset, length);
        }
      }

      return ret;
    }

    int MetaInfoHelper::put_object_user_metadata(const std::string &bucket_name,
        const std::string &file_name, const common::UserInfo &user_info,
        const common::UserMetadata &user_metadata)
    {
      int32_t ret = (bucket_name.size() > 0 && file_name.size() > 0) ? TFS_SUCCESS : TFS_ERROR;

      if (user_metadata.is_exceed_metadata_size())
      {
        ret = EXIT_EXCEED_USER_METADATA_MAX_SIZE;
      }

      if (TFS_SUCCESS == ret)
      {
        BucketMetaInfo bucket_meta_info;
        ret = head_bucket_ex(bucket_name, &bucket_meta_info);

        //check acl of bucket
        if (TFS_SUCCESS == ret)
        {
          ret = check_bucket_acl(bucket_meta_info.bucket_acl_map_, user_info.owner_id_, WRITE_ACP);
        }
      }

      if (TFS_SUCCESS == ret)
      {
        int64_t lock_version = 0;
        int32_t retry = KvDefine::VERSION_ERROR_RETRY_COUNT;
        ObjectInfo object_info_zero;

        do {
          ret = get_object_part(bucket_name, file_name, 0, &object_info_zero, &lock_version);
          if (TFS_SUCCESS != ret)
          {
            TBSYS_LOG(WARN, "get object zero fail, ret: %d, bucket: %s, object: %s",
                ret, bucket_name.c_str(), file_name.c_str());
            break;
          }
          object_info_zero.dump();

          object_info_zero.has_user_metadata_ = true;
          object_info_zero.set_user_metadata(user_metadata);

          ret = put_object_ex(bucket_name, file_name, 0, object_info_zero, lock_version);

          if (EXIT_KV_RETURN_VERSION_ERROR == ret)
          {
            TBSYS_LOG(INFO, "put object zero version conflict, bucket: %s, object: %s",
                bucket_name.c_str(), file_name.c_str());
          }
          else if (TFS_SUCCESS != ret)
          {
            TBSYS_LOG(WARN, "put object zero fail, ret: %d, bucket: %s, object: %s",
                ret, bucket_name.c_str(), file_name.c_str());
          }
        }while (retry-- && EXIT_KV_RETURN_VERSION_ERROR == ret);
      }

      return ret;
    }

    int MetaInfoHelper::get_object_part(const std::string &bucket_name,
        const std::string &file_name,
        const int64_t offset,
        common::ObjectInfo *object_info,
        int64_t *lock_version)
    {
      int ret = (bucket_name.size() > 0 && file_name.size() > 0
          && offset >= 0 && NULL != object_info) ? TFS_SUCCESS : TFS_ERROR;
      //op key
      char *key_buff = NULL;
      key_buff = (char*) malloc(KEY_BUFF_SIZE);
      if (NULL == key_buff)
      {
        ret = TFS_ERROR;
      }
      KvKey key;
      if (TFS_SUCCESS == ret)
      {
        ret = serialize_key(bucket_name, file_name, offset, &key, key_buff, KEY_BUFF_SIZE, KvKey::KEY_TYPE_OBJECT);
      }

      //op value
      KvValue *kv_value = NULL;
      if (TFS_SUCCESS == ret)
      {
        ret = kv_engine_helper_->get_key(meta_info_name_area_, key, &kv_value, lock_version);
        if (EXIT_KV_RETURN_DATA_NOT_EXIST == ret)
        {
          ret = EXIT_OBJECT_NOT_EXIST;
        }
      }
      int64_t pos = 0;
      if (TFS_SUCCESS == ret)
      {
        ret = object_info->deserialize(kv_value->get_data(), kv_value->get_size(), pos);
      }

      if (NULL != kv_value)
      {
        kv_value->free();
      }

      if (NULL != key_buff)
      {
        free(key_buff);
        key_buff = NULL;
      }

      return ret;
    }

    int MetaInfoHelper::scan_pre_record(const std::string &bucket_name,
        const std::string& file_name, const KvKey& start_key,
        common::ObjectInfo *object_info, int32_t& valid_result)
    {
      int ret = TFS_SUCCESS;
      //we should find pre record
      vector<KvValue*> kv_value_keys;
      vector<KvValue*> kv_value_values;
      int32_t result_size = 0;
      KvKey rend_key;
      char *rend_key_buff = NULL;
      rend_key_buff = (char*) malloc(KEY_BUFF_SIZE);
      assert(NULL != rend_key_buff);

      ret = serialize_key(bucket_name, file_name, 0,
          &rend_key, rend_key_buff, KEY_BUFF_SIZE, KvKey::KEY_TYPE_OBJECT);
      if (TFS_SUCCESS == ret)
      {
        ret = kv_engine_helper_->scan_keys(meta_info_name_area_,
            start_key, rend_key, -1, true,
            &kv_value_keys, &kv_value_values, &result_size, CMD_RANGE_VALUE_ONLY);
      }
      free(rend_key_buff);
      if (EXIT_KV_RETURN_DATA_NOT_EXIST == ret ||TFS_SUCCESS == ret )
      {
        if (TFS_SUCCESS == ret)
        {
          for(int i = result_size -1 ; i >= 0; i--)
          {
            common::ObjectInfo tmp_object_info;
            //value get
            int64_t pos = 0;
            tmp_object_info.deserialize(kv_value_values[i]->get_data(),
                kv_value_values[i]->get_size(), pos);
            if (tmp_object_info.v_tfs_file_info_.size() > 0)
            {
              object_info->v_tfs_file_info_.push_back(tmp_object_info.v_tfs_file_info_[0]);
              valid_result++;
            }
            kv_value_values[i]->free();
            kv_value_keys[i]->free();
          }
        }
        ret = TFS_SUCCESS;

      }
      else
      {
        TBSYS_LOG(ERROR, "metainfo exist but data not exist");
        ret = TFS_SUCCESS;
      }
      return ret;

    }

    int MetaInfoHelper::get_object(const std::string &bucket_name,
        const std::string &file_name, const int64_t offset,
        const int64_t length, const common::UserInfo &user_info,
        common::ObjectInfo *object_info, bool* still_have)
    {
      int ret = (bucket_name.size() > 0 && file_name.size() > 0 && length > 0
          && offset >= 0 && object_info != NULL && still_have != NULL) ? TFS_SUCCESS : TFS_ERROR;

      TBSYS_LOG(DEBUG, "get object:%s from bucket:%s, offset:%" PRI64_PREFIX "d"
          "length:%" PRI64_PREFIX "d, user_id:%" PRI64_PREFIX "d",
          file_name.c_str(), bucket_name.c_str(), offset, length, user_info.owner_id_);

      if (TFS_SUCCESS == ret)
      {
        ret = check_bucket_acl(bucket_name, user_info, READ);
      }
      common::ObjectInfo object_info_zero;
      if (TFS_SUCCESS == ret)
      {
        int64_t version = 0;
        int64_t offset_zero = 0;
        ret = get_object_part(bucket_name, file_name, offset_zero, &object_info_zero, &version);
        if (EXIT_OBJECT_NOT_EXIST == ret)
        {
          TBSYS_LOG(ERROR, "object %s %s not exist", bucket_name.c_str(), file_name.c_str());
        }
      }
      if (TFS_SUCCESS == ret)
      {
        *object_info = object_info_zero;
        *still_have = false;

        if (offset > object_info_zero.meta_info_.big_file_size_)
        {
          TBSYS_LOG(ERROR, "object %s %s req offset is out of big_file_size_", bucket_name.c_str(), file_name.c_str());
          ret = EXIT_READ_OFFSET_ERROR;
        }
      }
      //if offset == big_file_size_ return TFS_SUCCESS
      if (TFS_SUCCESS == ret && offset < object_info_zero.meta_info_.big_file_size_)
      {
        bool is_big_file = false;

        if (object_info_zero.v_tfs_file_info_.size() > 0)
        {
          if (object_info_zero.meta_info_.big_file_size_ == object_info_zero.v_tfs_file_info_[0].file_size_)
          {
            is_big_file = false;
          }
          else
          {
            is_big_file = true;
          }
        }
        else
        {
          is_big_file = true;
        }
        if (is_big_file)//big file
        {
          TBSYS_LOG(DEBUG, "is big_file");
          //op key
          char *start_key_buff = NULL;
          if (TFS_SUCCESS == ret)
          {
            start_key_buff = (char*) malloc(KEY_BUFF_SIZE);
            if (NULL == start_key_buff)
            {
              ret = TFS_ERROR;
            }
          }
          char *end_key_buff = NULL;
          if (ret == TFS_SUCCESS)
          {
            end_key_buff = (char*) malloc(KEY_BUFF_SIZE);
            if (NULL == end_key_buff)
            {
              ret = TFS_ERROR;
            }
          }
          KvKey start_key;
          KvKey end_key;
          int64_t start_offset = offset;
          int64_t end_offset = offset + length - 1;
          if (TFS_SUCCESS == ret)
          {
            ret = serialize_key(bucket_name, file_name, start_offset,
                &start_key, start_key_buff, KEY_BUFF_SIZE, KvKey::KEY_TYPE_OBJECT);
          }
          if (TFS_SUCCESS == ret)
          {
            ret = serialize_key(bucket_name, file_name, end_offset,
                &end_key, end_key_buff, KEY_BUFF_SIZE, KvKey::KEY_TYPE_OBJECT);
          }

          //op value

          int32_t i;
          int32_t scan_offset = 0;
          bool go_on = true;
          short scan_type = CMD_RANGE_VALUE_ONLY;//only scan value
          vector<KvValue*> kv_value_keys;
          vector<KvValue*> kv_value_values;
          object_info->v_tfs_file_info_.clear();
          int32_t valid_result = 0;

          while (go_on)
          {
            int32_t result_size = 0;
            int64_t last_offset = 0;
            ret = kv_engine_helper_->scan_keys(meta_info_name_area_, start_key, end_key, SCAN_LIMIT, scan_offset,
                &kv_value_keys, &kv_value_values, &result_size, scan_type);
            if (EXIT_KV_RETURN_DATA_NOT_EXIST == ret)
            {
              //we should find pre record
              ret = scan_pre_record(bucket_name, file_name, start_key, object_info, valid_result);
            }
            for(i = 0; i < result_size; ++i)
            {
              common::ObjectInfo tmp_object_info;
              //value get
              int64_t pos = 0;
              tmp_object_info.deserialize(kv_value_values[i]->get_data(),
                  kv_value_values[i]->get_size(), pos);
              if (tmp_object_info.v_tfs_file_info_.size() > 0)
              {
                last_offset = tmp_object_info.v_tfs_file_info_[0].offset_;
                if (0 == i )
                {
                  if (tmp_object_info.v_tfs_file_info_[0].offset_ > offset)
                  {
                    //we should find pre record
                    ret = scan_pre_record(bucket_name, file_name, start_key, object_info, valid_result);
                  }//end deal pre record
                }
                if (tmp_object_info.v_tfs_file_info_[0].offset_ + tmp_object_info.v_tfs_file_info_[0].file_size_ <= offset)
                {//invalid frag
                  continue;
                }
                // now vector max == 1
                object_info->v_tfs_file_info_.push_back(tmp_object_info.v_tfs_file_info_[0]);
                valid_result++;
                if (valid_result >= MESS_LIMIT)
                {
                  break;
                }
              }
            }
            TBSYS_LOG(DEBUG, "this time result_size is: %d", result_size);

            if ((result_size == SCAN_LIMIT && valid_result < MESS_LIMIT) || EXIT_KV_RETURN_HAS_MORE_DATA == ret)
            {
              scan_offset = 1;
              ret = serialize_key(bucket_name, file_name, last_offset,
                  &start_key, start_key_buff, KEY_BUFF_SIZE, KvKey::KEY_TYPE_OBJECT);
            }
            else
            {
              go_on = false;
            }

            for(i = 0; i < result_size; ++i)//free kv
            {
              kv_value_values[i]->free();
              kv_value_keys[i]->free();
            }
            kv_value_values.clear();
            kv_value_keys.clear();
          }//end while

          if (NULL != start_key_buff)
          {
            free(start_key_buff);
            start_key_buff = NULL;
          }
          if (NULL != end_key_buff)
          {
            free(end_key_buff);
            end_key_buff = NULL;
          }
          int32_t vec_tfs_size = static_cast<int32_t>(object_info->v_tfs_file_info_.size());

          if (MESS_LIMIT == vec_tfs_size)
          {
            *still_have = true;
          }
        }//end big file
      }//end success
      return ret;
    }

    int MetaInfoHelper::del_object(const std::string& bucket_name,
        const std::string& file_name, const common::UserInfo &user_info,
        common::ObjectInfo *object_info, bool* still_have)
    {
      int ret = (bucket_name.size() > 0 && file_name.size() > 0) ? TFS_SUCCESS : TFS_ERROR;
      *still_have = false;

      TBSYS_LOG(DEBUG, "del object:%s from bucket:%s, user_id:%" PRI64_PREFIX "d",
          file_name.c_str(), bucket_name.c_str(), user_info.owner_id_);

      if (TFS_SUCCESS == ret)
      {
        ret = check_bucket_acl(bucket_name, user_info, WRITE);
      }
      //op key
      char *start_key_buff = NULL;
      if (TFS_SUCCESS == ret)
      {
        start_key_buff = (char*) malloc(KEY_BUFF_SIZE);
        if (NULL == start_key_buff)
        {
          ret = TFS_ERROR;
        }
      }
      char *end_key_buff = NULL;
      if (TFS_SUCCESS == ret)
      {
        end_key_buff = (char*) malloc(KEY_BUFF_SIZE);
        if (NULL == end_key_buff)
        {
          ret = TFS_ERROR;
        }
      }
      KvKey start_key;
      KvKey end_key;
      int64_t start_offset = 0;
      int64_t end_offset = INT64_INFI;
      if (TFS_SUCCESS == ret)
      {
        ret = serialize_key(bucket_name, file_name, start_offset,
              &start_key, start_key_buff, KEY_BUFF_SIZE, KvKey::KEY_TYPE_OBJECT);
      }
      if (TFS_SUCCESS == ret)
      {
        ret = serialize_key(bucket_name, file_name, end_offset,
              &end_key, end_key_buff, KEY_BUFF_SIZE, KvKey::KEY_TYPE_OBJECT);
      }

      int32_t limit = MESS_LIMIT;
      int32_t i;
      int32_t scan_offset = 0;
      short scan_type = CMD_RANGE_ALL;
      vector<KvValue*> kv_value_keys;
      vector<KvValue*> kv_value_values;
      object_info->v_tfs_file_info_.clear();
      vector<KvKey> vec_keys;

      int32_t result_size = 0;
      if (TFS_SUCCESS == ret)
      {
        ret = kv_engine_helper_->scan_keys(meta_info_name_area_, start_key, end_key, limit + 1, scan_offset,
          &kv_value_keys, &kv_value_values, &result_size, scan_type);
        TBSYS_LOG(DEBUG, "del object, bucekt_name: %s, object_name: %s, "
            "scan ret: %d, limit: %d, result size: %d",
            bucket_name.c_str(), file_name.c_str(), ret, limit + 1, result_size);
        if (EXIT_KV_RETURN_DATA_NOT_EXIST == ret && result_size == 0)
        {
          ret = EXIT_OBJECT_NOT_EXIST;
        }
        if (result_size == limit + 1)
        {
          result_size -= 1;
          *still_have = true;
        }
        if(TFS_SUCCESS == ret || EXIT_KV_RETURN_HAS_MORE_DATA == ret)
        {
          if (EXIT_KV_RETURN_HAS_MORE_DATA == ret)
          {
            ret = TFS_SUCCESS;
            *still_have = true;
          }
          for(i = 0; i < result_size; ++i)
          {
            //key get
            KvKey tmp_key;
            tmp_key.key_ = kv_value_keys[i]->get_data();
            tmp_key.key_size_ = kv_value_keys[i]->get_size();
            tmp_key.key_type_ = KvKey::KEY_TYPE_OBJECT;
            vec_keys.push_back(tmp_key);

            //value get
            common::ObjectInfo tmp_object_info;
            int64_t pos = 0;
            if(TFS_SUCCESS == ret)
            {
              ret = tmp_object_info.deserialize(kv_value_values[i]->get_data(),
                                     kv_value_values[i]->get_size(), pos);
            }
            if(TFS_SUCCESS == ret)
            {
              //j now max == 1
              for (size_t j = 0; j < tmp_object_info.v_tfs_file_info_.size(); j++)
              {
                TBSYS_LOG(DEBUG, "del tfs file info:");
                tmp_object_info.v_tfs_file_info_[j].dump();
                object_info->v_tfs_file_info_.push_back(tmp_object_info.v_tfs_file_info_[j]);
              }
            }
          }
        }

        //del from kv
        if(TFS_SUCCESS == ret && result_size > 0)
        {
           ret = kv_engine_helper_->delete_keys(meta_info_name_area_, vec_keys);
        }
        for(i = 0; i < result_size; ++i)//free kv
        {
          kv_value_keys[i]->free();
          kv_value_values[i]->free();
        }
        kv_value_keys.clear();
        kv_value_values.clear();
      }

      if(NULL != start_key_buff)
      {
        free(start_key_buff);
        start_key_buff = NULL;
      }
      if(NULL != end_key_buff)
      {
        free(end_key_buff);
        end_key_buff = NULL;
      }
      return ret;
    }

    int MetaInfoHelper::del_object_user_metadata(const std::string& bucket_name,
        const std::string& file_name, const common::UserInfo &user_info)
    {
      int32_t ret = (bucket_name.size() > 0 && file_name.size() > 0) ? TFS_SUCCESS : TFS_ERROR;

      if (TFS_SUCCESS == ret)
      {
        BucketMetaInfo bucket_meta_info;
        ret = head_bucket_ex(bucket_name, &bucket_meta_info);

        //check acl of bucket
        if (TFS_SUCCESS == ret)
        {
          ret = check_bucket_acl(bucket_meta_info.bucket_acl_map_, user_info.owner_id_, WRITE_ACP);
        }
      }

      if (TFS_SUCCESS == ret)
      {
        int64_t lock_version = 0;
        ObjectInfo object_info_zero;
        int32_t retry = KvDefine::VERSION_ERROR_RETRY_COUNT;

        do
        {
          ret = get_object_part(bucket_name, file_name, 0, &object_info_zero, &lock_version);
          if (TFS_SUCCESS != ret)
          {
            TBSYS_LOG(WARN, "get object zero fail, ret: %d, bucket: %s, object: %s",
                ret, bucket_name.c_str(), file_name.c_str());
            break;
          }

          UserMetadata& exist_user_metadata = object_info_zero.get_mutable_user_metadata();
          if(exist_user_metadata.get_metadata().size() > 0)
          {
            exist_user_metadata.clear_metadata();

            ret = put_object_ex(bucket_name, file_name, 0, object_info_zero, lock_version);
            if (EXIT_KV_RETURN_VERSION_ERROR == ret)
            {
              TBSYS_LOG(INFO, "put object zero version conflict, bucket: %s, object: %s",
                  bucket_name.c_str(), file_name.c_str());
            }
            else if (TFS_SUCCESS != ret)
            {
              TBSYS_LOG(WARN, "put object zero fail, ret: %d, bucket: %s, object: %s",
                  ret, bucket_name.c_str(), file_name.c_str());
            }
          }
        } while (retry-- && EXIT_KV_RETURN_VERSION_ERROR == ret);
      }

      return ret;
    }

    /*----------------------------bucket part-----------------------------*/

    int MetaInfoHelper::get_common_prefix(const char *key, const string& prefix,
        const char delimiter, bool *prefix_flag, bool *common_flag, int *common_end_pos)
    {
      int ret = TFS_SUCCESS;
      if (NULL == prefix_flag || NULL == common_flag || NULL == common_end_pos)
      {
        ret = TFS_ERROR;
      }

      if (TFS_SUCCESS == ret)
      {
        *prefix_flag = false;
        *common_flag = false;
        *common_end_pos = -1;

        int start_pos = 0;
        if (!prefix.empty())
        {
          if (strncmp(key, prefix.c_str(), prefix.length()) == 0)
          {
            *prefix_flag = true;
            start_pos = prefix.length();
          }
        }
        else
        {
          *prefix_flag = true;
        }

        if (*prefix_flag && KvDefine::DEFAULT_CHAR != delimiter)
        {
          for (size_t j = start_pos; j < strlen(key); j++)
          {
            if (*(key+j) == delimiter)
            {
              *common_flag = true;
              *common_end_pos = j;
              break;
            }
          }
        }
      }

      return ret;
    }

    int MetaInfoHelper::get_range(const KvKey &pkey, const string &start_key,
        const int32_t offset, const int32_t limit, vector<KvValue*> *kv_value_keys,
        vector<KvValue*> *kv_value_values, int32_t *result_size)
    {
      int ret = TFS_SUCCESS;

      short scan_type = CMD_RANGE_ALL;//scan key and value
      KvKey start_obj_key;
      KvKey end_obj_key;

      string skey(pkey.key_);
      string ekey(pkey.key_);
      skey += KvKey::DELIMITER;
      ekey += (KvKey::DELIMITER+1);
      if (!start_key.empty())
      {
        skey += start_key;
      }
      start_obj_key.key_ = skey.c_str();
      start_obj_key.key_size_ = skey.length();
      start_obj_key.key_type_ = KvKey::KEY_TYPE_OBJECT;

      end_obj_key.key_ = ekey.c_str();
      end_obj_key.key_size_ = ekey.length();
      start_obj_key.key_type_ = KvKey::KEY_TYPE_OBJECT;

      ret = kv_engine_helper_->scan_keys(meta_info_name_area_, start_obj_key, end_obj_key, limit, offset, kv_value_keys, kv_value_values, result_size, scan_type);
      if (EXIT_KV_RETURN_DATA_NOT_EXIST == ret)
      {
        ret = TFS_SUCCESS;
      }

      return ret;
    }

    int MetaInfoHelper::deserialize_key(const char *key, const int32_t key_size, string *bucket_name, string *object_name,
        int64_t *offset, int64_t *version)
    {
      int ret = (key != NULL && key_size > 0 && bucket_name != NULL && object_name != NULL &&
          version != NULL && offset != NULL) ? TFS_SUCCESS : TFS_ERROR;

      if (TFS_SUCCESS == ret)
      {
        char *pos = const_cast<char*>(key);
        do
        {
          if (KvKey::DELIMITER == *pos)
          {
            break;
          }
          pos++;
        } while(pos - key < key_size);
        int64_t bucket_name_size = pos - key;
        bucket_name->assign(key, bucket_name_size);

        pos++;

        do
        {
          if (KvKey::DELIMITER == *pos)
          {
            break;
          }
          pos++;
        } while(pos - key < key_size);

        int64_t object_name_size = pos - key - bucket_name_size - 1;
        object_name->assign(key + bucket_name_size + 1, object_name_size);

        pos++;

        if (TFS_SUCCESS == ret && (pos + 8) <= key + key_size)
        {
          ret = Serialization::char_to_int64(pos, key + key_size - pos, *version);
          pos = pos + 8;
        }

        pos++;

        if (TFS_SUCCESS == ret && (pos + 8) <= key + key_size)
        {
          ret = Serialization::char_to_int64(pos, key + key_size - pos, *offset);
          pos = pos + 8;
        }
      }


      return ret;
    }

    int MetaInfoHelper::group_objects(const string &object_name, const string &v,
        const string &prefix, const char delimiter,
        vector<ObjectMetaInfo> *v_object_meta_info, vector<string> *v_object_name, set<string> *s_common_prefix)
    {
      int ret = TFS_SUCCESS;
      int common_pos = -1;

      bool prefix_flag = false;
      bool common_flag = false;

      ret = get_common_prefix(object_name.c_str(), prefix, delimiter, &prefix_flag, &common_flag, &common_pos);

      if (TFS_SUCCESS == ret)
      {
        if (common_flag)
        {
          string common_prefix(object_name.substr(0, common_pos+1));
          s_common_prefix->insert(common_prefix);
        }
        else if (prefix_flag)
        {
          ObjectInfo object_info;
          int64_t pos = 0;
          ret = object_info.deserialize(v.c_str(), v.length(), pos);
          if (TFS_SUCCESS == ret)
          {
            v_object_meta_info->push_back(object_info.meta_info_);
            v_object_name->push_back(object_name);
          }
        }
      }

      return ret;
    }

    int MetaInfoHelper::put_bucket_list(const int64_t owner_id, const set<string> &s_bucket_list,
        const int64_t version)
    {
      //first pos put special char to differ from other keys
      int32_t buff_size = 1 + INT64_SIZE;
      char key_buff[buff_size];
      int64_t pos = 0;
      key_buff[pos++] = KvKey::PREFIX;
      int ret = Serialization::int64_to_char(key_buff + pos, buff_size, owner_id);
      pos = pos + INT64_SIZE;

      KvKey key;
      key.key_ = key_buff;
      key.key_size_ = pos;
      key.key_type_ = KvKey::KEY_TYPE_BUCKET;

      char *kv_value_bucket_name_buff = NULL;
      int64_t kv_value_buff_size = Serialization::get_sstring_length(s_bucket_list);
      kv_value_bucket_name_buff = (char*) malloc(kv_value_buff_size);

      if (NULL == kv_value_bucket_name_buff)
      {
        ret = TFS_ERROR;
      }

      if (TFS_SUCCESS == ret)
      {
        pos = 0;
        ret = Serialization::set_sstring(kv_value_bucket_name_buff, kv_value_buff_size, pos, s_bucket_list);
      }

      KvMemValue value;
      if (TFS_SUCCESS == ret)
      {
        value.set_data(kv_value_bucket_name_buff, pos);
        ret = kv_engine_helper_->put_key(meta_info_name_area_, key, value, version);
      }

      if (NULL != kv_value_bucket_name_buff)
      {
        free(kv_value_bucket_name_buff);
        kv_value_bucket_name_buff = NULL;
      }

      return ret;
    }

    int MetaInfoHelper::get_bucket_list(const int64_t owner_id, set<string> *s_bucket_list, int64_t *version)
    {
      //first pos put prefix char to differ from other keys
      int32_t buff_size = 1 + INT64_SIZE;
      char key_buff[buff_size];
      int64_t pos = 0;
      key_buff[pos++] = KvKey::PREFIX;
      int ret = Serialization::int64_to_char(key_buff + pos, buff_size, owner_id);
      pos = pos + INT64_SIZE;

      KvKey key;
      key.key_ = key_buff;
      key.key_size_ = pos;
      key.key_type_ = KvKey::KEY_TYPE_BUCKET;

      KvValue *value = NULL;
      if (TFS_SUCCESS == ret)
      {
        ret = kv_engine_helper_->get_key(meta_info_name_area_, key, &value, version);
      }
      if (ret == EXIT_KV_RETURN_DATA_NOT_EXIST)
      {
        TBSYS_LOG(DEBUG, "owner: %" PRI64_PREFIX "d has not create any bucket", owner_id);
        ret = EXIT_NO_BUCKETS;
      }

      if (TFS_SUCCESS == ret)
      {
        pos = 0;
        ret = Serialization::get_sstring(value->get_data(), value->get_size(), pos, *s_bucket_list);
      }

      if (NULL != value)
      {
        value->free();
      }

      dump_bucket_list(owner_id, *s_bucket_list);
      return ret;
    }

    int MetaInfoHelper::list_buckets(common::BucketsResult *buckets_result,
        const common::UserInfo &user_info)
    {
      int ret = NULL == buckets_result ? TFS_ERROR : TFS_SUCCESS;

      TBSYS_LOG(DEBUG, "list buckets of user_id:%" PRI64_PREFIX "d",
          user_info.owner_id_);

      if (TFS_SUCCESS == ret)
      {
        buckets_result->owner_id_ = user_info.owner_id_;
      }

      set<string> s_bucket_list;
      if (TFS_SUCCESS == ret)
      {
        ret = get_bucket_list(user_info.owner_id_, &s_bucket_list, NULL);
      }

      if (TFS_SUCCESS == ret)
      {
        buckets_result->bucket_info_map_.clear();
        set<string>::iterator iter = s_bucket_list.begin();
        for (; TFS_SUCCESS == ret && iter != s_bucket_list.end(); iter++)
        {
          BucketMetaInfo bucket_meta_info;
          ret = head_bucket_ex(*iter, &bucket_meta_info);

          if (TFS_SUCCESS == ret)
          {
            if (user_info.owner_id_ == bucket_meta_info.owner_id_)
            {
              buckets_result->bucket_info_map_.insert(make_pair(*iter, bucket_meta_info));
            }
            else
            {
              TBSYS_LOG(ERROR, "bucket: %s has conflict owner_id: %" PRI64_PREFIX "d <=> %" PRI64_PREFIX "d",
                  (*iter).c_str(), bucket_meta_info.owner_id_, user_info.owner_id_);
              continue;
            }
          }
          else
          {
            TBSYS_LOG(ERROR, "head bucket: %s fail, ret: %d", (*iter).c_str(), ret);
            ret = TFS_SUCCESS;
            // TODO: maybe bucket is not exist any more, need update bucket list?
            //if (EXIT_BUCKET_NOT_EXIST == ret)
            //{
            //  need_update = true;
            //}
            continue;
          }
        }
      }
      else if (EXIT_NO_BUCKETS == ret)
      {
        TBSYS_LOG(INFO, "owner: %" PRI64_PREFIX "d has not create any buckets", user_info.owner_id_);
        ret = TFS_SUCCESS;
      }

      return ret;
    }

    int MetaInfoHelper::list_objects(const KvKey& pkey, const std::string& prefix,
        const std::string& start_key, const char delimiter, int32_t *limit,
        std::vector<common::ObjectMetaInfo>* v_object_meta_info, common::VSTRING* v_object_name,
        std::set<std::string>* s_common_prefix, int8_t* is_truncated)
    {
      int ret = TFS_SUCCESS;

      if (NULL == v_object_meta_info ||
          NULL == v_object_name ||
          NULL == s_common_prefix ||
          NULL == is_truncated ||
          NULL == limit)
      {
        ret = TFS_ERROR;
      }

      if (*limit > KvDefine::MAX_LIMIT or *limit < 0)
      {
        TBSYS_LOG(WARN, "limit: %d will be cutoff", *limit);
        *limit = KvDefine::MAX_LIMIT;
      }

      if (TFS_SUCCESS == ret)
      {
        v_object_meta_info->clear();
        v_object_name->clear();
        s_common_prefix->clear();

        int32_t limit_size = *limit;
        *is_truncated = 0;

        vector<KvValue*> kv_value_keys;
        vector<KvValue*> kv_value_values;

        bool first_loop = true;
        string temp_start_key(start_key);

        if (start_key.compare(prefix) < 0)
        {
          temp_start_key = prefix;
          //never handle start_key
          first_loop = false;
        }
        /* if is dir need skip the lastone at lasttime */
        if (start_key.length() > 1 && start_key[start_key.length() - 1] == delimiter)
        {
          const char next_delimiter = delimiter + 1;
          temp_start_key = start_key.substr(0, start_key.length() - 1) + next_delimiter;
          first_loop = false;
        }
        bool loop = true;
        int32_t has_scan_times = 0;
        do
        {
          int32_t res_size = -1;
          int32_t actual_size = static_cast<int32_t>(v_object_name->size()) +
            static_cast<int32_t>(s_common_prefix->size());

          limit_size = *limit - actual_size;

          //start_key need to be excluded from a result except for using prefix as start_key.
          int32_t extra = first_loop ? 2 : 1;
          ret = get_range(pkey, temp_start_key, 0, limit_size + extra,
              &kv_value_keys, &kv_value_values, &res_size);
          // error
          if (TFS_SUCCESS != ret && EXIT_KV_RETURN_HAS_MORE_DATA != ret)
          {
            TBSYS_LOG(ERROR, "get range fail, ret: %d", ret);
            break;
          }

          TBSYS_LOG(DEBUG, "get range once, res_size: %d, limit_size: %d", res_size, limit_size);

          has_scan_times++;
          if (res_size == 0)
          {
            break;
          }
          else if (res_size < limit_size + extra && EXIT_KV_RETURN_HAS_MORE_DATA != ret)
          {
            loop = false;
          }
          else if (has_scan_times > GET_BUCKET_KV_SCAN_MAX_NUM - 1)
          {
            loop = false;
            *is_truncated = 1;
          }

          string object_name;
          string bucket_name;
          int64_t offset = -1;
          int64_t version = -1;

          for (int i = 0; i < res_size; i++)
          {
            string k(kv_value_keys[i]->get_data(), kv_value_keys[i]->get_size());
            string v(kv_value_values[i]->get_data(), kv_value_values[i]->get_size());

            ret = deserialize_key(k.c_str(), k.length(), &bucket_name, &object_name, &offset, &version);
            if (TFS_SUCCESS != ret)
            {
              TBSYS_LOG(ERROR, "deserialize from %s fail", k.c_str());
            }
            else if (offset == 0)
            {
              if (!first_loop)
              {
                ret = group_objects(object_name, v, prefix, delimiter,
                    v_object_meta_info, v_object_name, s_common_prefix);
              }
              //If it is first_loop, we need to skip the object which equals start_key.
              else if (object_name.compare(start_key) != 0)
              {
                ret = group_objects(object_name, v, prefix, delimiter,
                    v_object_meta_info, v_object_name, s_common_prefix);
              }

              if (TFS_SUCCESS != ret)
              {
                TBSYS_LOG(ERROR, "group objects fail, ret: %d", ret);
              }
            }

            if (TFS_SUCCESS != ret)
            {
              loop = false;
              break;
            }

            if (static_cast<int32_t>(s_common_prefix->size()) +
                static_cast<int32_t>(v_object_name->size()) >= *limit)
            {
              loop = false;
              *is_truncated = 1;
              break;
            }

            if (!prefix.empty() && object_name.compare(prefix) > 0 && object_name.find(prefix) != 0)
            {
              TBSYS_LOG(DEBUG, "object after %s can't match", object_name.c_str());
              loop = false;
              break;
            }
          }

          if (loop)
          {
            int32_t found = -1;
            string new_object_name;
            const char next_delimiter = delimiter + 1;
            KvKey key;
            char key_buff[KEY_BUFF_SIZE];

            if (KvDefine::DEFAULT_CHAR != delimiter)
            {
              if (prefix.empty())
              {
                found = object_name.find(delimiter);
              }
              else
              {
                found = object_name.find(delimiter, prefix.size());
              }
              if (found != -1)
              {
                new_object_name = object_name.substr(0, found);
                object_name = new_object_name + next_delimiter;
              }
            }

            offset = INT64_MAX;
            ret = serialize_key_ex(object_name, offset, &key, key_buff, KEY_BUFF_SIZE, KvKey::KEY_TYPE_OBJECT);
            if (TFS_SUCCESS == ret)
            {
              temp_start_key.assign(key.key_, key.key_size_);
            }
            else
            {
              TBSYS_LOG(INFO, "serialize sub key error");
              loop = false;
            }
          }

          //delete for kv
          for (int i = 0; i < res_size; ++i)
          {
            kv_value_keys[i]->free();
            kv_value_values[i]->free();
          }
          kv_value_keys.clear();
          kv_value_values.clear();
          first_loop = false;
        } while (loop);// end of while
      }// end of if
      return ret;
    }// end of func


    int MetaInfoHelper::head_bucket_ex(const std::string &bucket_name,
        common::BucketMetaInfo *bucket_meta_info)
    {
      int ret = TFS_SUCCESS;

      KvKey key;
      key.key_ = bucket_name.c_str();
      key.key_size_ = bucket_name.length();
      key.key_type_ = KvKey::KEY_TYPE_BUCKET;

      KvValue *value = NULL;
      int64_t version = 0;
      if (TFS_SUCCESS == ret)
      {
        ret = kv_engine_helper_->get_key(meta_info_name_area_, key, &value, &version);
      }
      if (ret == EXIT_KV_RETURN_DATA_NOT_EXIST)
      {
        ret = EXIT_BUCKET_NOT_EXIST;
      }

      if (TFS_SUCCESS == ret)
      {
        int64_t pos = 0;
        ret = bucket_meta_info->deserialize(value->get_data(), value->get_size(), pos);
      }

      if (NULL != value)
      {
        value->free();
      }

      return ret;
    }

    int MetaInfoHelper::head_bucket(const std::string &bucket_name, const common::UserInfo &user_info,
                                    common::BucketMetaInfo *bucket_meta_info)
    {
      int ret = TFS_SUCCESS;

      TBSYS_LOG(DEBUG, "head bucket:%s, user_id:%" PRI64_PREFIX "d",
          bucket_name.c_str(), user_info.owner_id_);

      if (TFS_SUCCESS == ret)
      {
        ret = head_bucket_ex(bucket_name, bucket_meta_info);
      }
      if (TFS_SUCCESS == ret)
      {
        if (!is_appid_uid_bucket_name(bucket_name))
        {
          ret = check_bucket_acl(bucket_meta_info->bucket_acl_map_, user_info.owner_id_, READ);
        }
      }
      return ret;
    }

    int MetaInfoHelper::put_bucket_ex(const string &bucket_name, const BucketMetaInfo &bucket_meta_info,
        int64_t lock_version)
    {
      int ret = TFS_SUCCESS;

      KvKey key;
      key.key_ = bucket_name.c_str();
      key.key_size_ = bucket_name.length();
      key.key_type_ = KvKey::KEY_TYPE_BUCKET;

      char *kv_value_bucket_info_buff = NULL;
      kv_value_bucket_info_buff = (char*) malloc(KV_VALUE_BUFF_SIZE);
      if (NULL == kv_value_bucket_info_buff)
      {
        ret = TFS_ERROR;
      }

      int64_t pos = 0;
      if (TFS_SUCCESS == ret)
      {
        ret = bucket_meta_info.serialize(kv_value_bucket_info_buff, KV_VALUE_BUFF_SIZE, pos);
      }

      KvMemValue value;
      if (TFS_SUCCESS == ret)
      {
        value.set_data(kv_value_bucket_info_buff, pos);
      }

      if (TFS_SUCCESS == ret)
      {
        ret = kv_engine_helper_->put_key(meta_info_name_area_, key, value, lock_version);
      }

      if (NULL != kv_value_bucket_info_buff)
      {
        free(kv_value_bucket_info_buff);
        kv_value_bucket_info_buff = NULL;
      }
      return ret;
    }

    int MetaInfoHelper::put_bucket(const std::string& bucket_name, common::BucketMetaInfo& bucket_meta_info,
        const common::UserInfo &user_info, const common::CANNED_ACL acl)
    {
      int64_t now_time = static_cast<int64_t>(time(NULL));
      bucket_meta_info.set_create_time(now_time);
      int ret = TFS_SUCCESS;

      TBSYS_LOG(DEBUG, "put bucket:%s, user_id:%" PRI64_PREFIX "d, acl:%d",
          bucket_name.c_str(), user_info.owner_id_, acl);

      if (TFS_SUCCESS == ret)
      {
        BucketMetaInfo tmp_bucket_meta_info;
        ret = head_bucket_ex(bucket_name, &tmp_bucket_meta_info);
        if (TFS_SUCCESS == ret)
        {
          TBSYS_LOG(INFO, "bucket: %s has existed", bucket_name.c_str());
          ret = EXIT_BUCKET_EXIST;
        }
        else if (EXIT_BUCKET_NOT_EXIST == ret)
        {
          ret = TFS_SUCCESS;
        }
      }

      set<string> s_bucket_list;
      int64_t version = 0;
      if (!is_appid_uid_bucket_name(bucket_name))
      {
        // update bucket list
        if (TFS_SUCCESS == ret)
        {
          ret = get_bucket_list(user_info.owner_id_, &s_bucket_list, &version);
          if (TFS_SUCCESS == ret && static_cast<int32_t>(s_bucket_list.size()) >= KvDefine::MAX_BUCKETS_COUNT)
          {
            ret = EXIT_OVER_MAX_BUCKETS_COUNT;
            TBSYS_LOG(ERROR, "owner: %" PRI64_PREFIX "d has %zu buckets, over %d",
                user_info.owner_id_, s_bucket_list.size(), KvDefine::MAX_BUCKETS_COUNT);
          }
          else if (EXIT_NO_BUCKETS == ret)
          {
            ret = TFS_SUCCESS;
          }
        }
      }

      if (TFS_SUCCESS == ret)
      {
        //default PRIVATE:
        bucket_meta_info.owner_id_ = user_info.owner_id_;
        ret = do_canned_acl(acl, bucket_meta_info.bucket_acl_map_, bucket_meta_info.owner_id_);
        int64_t ver = KvDefine::MAX_VERSION;
        ret = put_bucket_ex(bucket_name, bucket_meta_info, ver);
      }

      if (!is_appid_uid_bucket_name(bucket_name))
      {
        if (TFS_SUCCESS == ret)
        {
          s_bucket_list.insert(bucket_name);
          if (0 == version)
          {
            version = KvDefine::MAX_VERSION;
          }
          int32_t retry = KvDefine::VERSION_ERROR_RETRY_COUNT;
          do
          {
            ret = put_bucket_list(user_info.owner_id_, s_bucket_list, version);
            if (EXIT_KV_RETURN_VERSION_ERROR == ret)
            {
              int iret = TFS_ERROR;
              s_bucket_list.clear();
              iret = get_bucket_list(user_info.owner_id_, &s_bucket_list, &version);
              if (TFS_SUCCESS == iret || EXIT_NO_BUCKETS == iret)
              {
                if (TFS_SUCCESS == iret && static_cast<int32_t>(s_bucket_list.size()) >= KvDefine::MAX_BUCKETS_COUNT)
                {
                  ret = EXIT_OVER_MAX_BUCKETS_COUNT;
                  TBSYS_LOG(ERROR, "owner: %" PRI64_PREFIX "d has %zu buckets, over %d",
                      user_info.owner_id_, s_bucket_list.size(), KvDefine::MAX_BUCKETS_COUNT);
                  break;
                }
                else
                {
                  s_bucket_list.insert(bucket_name);
                }
              }
              else
              {
                TBSYS_LOG(ERROR, "get owner: %" PRI64_PREFIX "d's bucket list failed, ret: %d",
                    user_info.owner_id_, iret);
                break;
              }
            }
          } while (EXIT_KV_RETURN_VERSION_ERROR == ret && retry--);
          // retry still failed, roll back
          if (TFS_SUCCESS != ret)
          {
            del_bucket(bucket_name, user_info);
          }
        }
      }

      return ret;
    }

    int MetaInfoHelper::get_bucket(const std::string& bucket_name, const std::string& prefix,
        const std::string& start_key, const char delimiter, int32_t *limit, const common::UserInfo &user_info,
        vector<ObjectMetaInfo>* v_object_meta_info, VSTRING* v_object_name, set<string>* s_common_prefix,
        int8_t* is_truncated)
    {
      int ret = TFS_SUCCESS;

      TBSYS_LOG(DEBUG, "get bucket:%s, prefix:%s, start_key:%s, delimiter:%c, user_id:%" PRI64_PREFIX "d",
          bucket_name.c_str(), prefix.c_str(), start_key.c_str(), delimiter, user_info.owner_id_);

      ret = check_bucket_acl(bucket_name, user_info, READ);

      KvKey pkey;
      pkey.key_ = bucket_name.c_str();
      pkey.key_size_ = bucket_name.length();
      pkey.key_type_ = KvKey::KEY_TYPE_BUCKET;

      TBSYS_LOG(DEBUG, "get bucket: %s, prefix: %s, start_key: %s, delimiter: %c",
          bucket_name.c_str(), prefix.c_str(), start_key.c_str(), delimiter);
      /*
      if (TFS_SUCCESS == ret)
      {
        BucketMetaInfo bucket_meta_info;
        ret = head_bucket(bucket_name, &bucket_meta_info);
        TBSYS_LOG(INFO, "head bucket: %s, ret: %d", bucket_name.c_str(), ret);
      }
      */
      if (TFS_SUCCESS == ret)
      {
        ret = list_objects(pkey, prefix, start_key, delimiter, limit,
            v_object_meta_info, v_object_name, s_common_prefix, is_truncated);
      }

      return ret;
    }

    int MetaInfoHelper::del_bucket(const string& bucket_name, const UserInfo &user_info)
    {
      int ret = TFS_SUCCESS;
      KvKey pkey;
      pkey.key_ = bucket_name.c_str();
      pkey.key_size_ = bucket_name.length();
      pkey.key_type_ = KvKey::KEY_TYPE_BUCKET;

      TBSYS_LOG(DEBUG, "delete bucket:%s, user_id:%" PRI64_PREFIX "d",
          bucket_name.c_str(), user_info.owner_id_);

      ret = check_bucket_acl(bucket_name, user_info, WRITE);

      if (TFS_SUCCESS == ret)
      {
        int32_t limit = KvDefine::MAX_LIMIT;
        int32_t res_size = -1;
        vector<KvValue*> kv_value_keys;
        vector<KvValue*> kv_value_values;

        ret = get_range(pkey, "", 0, limit, &kv_value_keys, &kv_value_values, &res_size);
        if (res_size == 0 && TFS_SUCCESS == ret)
        {
          TBSYS_LOG(DEBUG, "bucket: %s is empty, will be deleted", bucket_name.c_str());
        }
        else
        {
          TBSYS_LOG(ERROR, "delete bucket: %s failed! bucket is not empty", bucket_name.c_str());
          ret = EXIT_DELETE_DIR_WITH_FILE_ERROR;
        }

        if (TFS_SUCCESS == ret)
        {
          ret = kv_engine_helper_->delete_key(meta_info_name_area_, pkey);
        }

        if (TFS_SUCCESS == ret)
        {
          int32_t retry = KvDefine::VERSION_ERROR_RETRY_COUNT;
          do
          {
            set<string> s_tmp_bucket_list;
            int64_t version;
            ret = get_bucket_list(user_info.owner_id_, &s_tmp_bucket_list, &version);
            if (TFS_SUCCESS == ret)
            {
              if (s_tmp_bucket_list.find(bucket_name) == s_tmp_bucket_list.end())
              {
                TBSYS_LOG(WARN, "owner: %" PRI64_PREFIX "d does not own this bucket: %s", user_info.owner_id_, bucket_name.c_str());
              }
              else
              {
                s_tmp_bucket_list.erase(bucket_name);
                ret = put_bucket_list(user_info.owner_id_, s_tmp_bucket_list, version);
              }
            }
          } while (EXIT_KV_RETURN_VERSION_ERROR == ret && retry--);

          if (TFS_SUCCESS != ret && EXIT_NO_BUCKETS != ret)
          {
            TBSYS_LOG(ERROR, "update owner: %" PRI64_PREFIX "d's bucket list failed, ret: %d", user_info.owner_id_, ret);
          }
        }

        //delete for kv
        for (int i = 0; i < res_size; ++i)
        {
          kv_value_keys[i]->free();
          kv_value_values[i]->free();
        }
        kv_value_keys.clear();
        kv_value_values.clear();
      }
      return ret;
    }

    void MetaInfoHelper::dump_bucket_list(const int64_t owner_id, const set<string> &s_bucket_list)
    {
      TBSYS_LOG(DEBUG, "will dump owner: %" PRI64_PREFIX "d's bucket list", owner_id);
      set<string>::iterator iter = s_bucket_list.begin();
      for (; iter != s_bucket_list.end(); iter++)
      {
        TBSYS_LOG(DEBUG, "bucket: %s", iter->c_str());
      }
    }

    void MetaInfoHelper::dump_acl_map(const MAP_INT64_INT acl_map)
    {
      TBSYS_LOG(DEBUG, "will dump acl_map");
      MAP_INT64_INT_ITER iter = acl_map.begin();
      for (; iter != acl_map.end(); iter++)
      {
        TBSYS_LOG(DEBUG, "user_id: %" PRI64_PREFIX "d, permission: %d", iter->first, iter->second);
      }
    }

    int MetaInfoHelper::check_bucket_acl(const MAP_INT64_INT &bucket_acl_map,
        const int64_t user_id, const PERMISSION per)
    {
      bool has_permission = true;
      int ret = TFS_SUCCESS;

      dump_acl_map(bucket_acl_map);

      // for admin
      if (KvDefine::ADMIN_ID == user_id)
      {
        has_permission = true;
      }
      else
      {
        MAP_INT64_INT_ITER iter = bucket_acl_map.find(user_id);
        if (iter != bucket_acl_map.end())
        {
          TBSYS_LOG(DEBUG, "user_id: %" PRI64_PREFIX "d is bucket owner, permission: %d", user_id, iter->second);
          if (!(iter->second & per))
          {
            has_permission = false;
          }
        }
        else
        {
          // -1 means others not include owner
          iter = bucket_acl_map.find(OTHER_ROLE);
          if (iter != bucket_acl_map.end() && (iter->second & per))
          {
            has_permission = true;
          }
          else
          {
            has_permission = false;
          }
        }
      }

      if (!has_permission)
      {
        ret = EXIT_BUCKET_PERMISSION_DENY;
        TBSYS_LOG(ERROR, "user_id: %" PRI64_PREFIX "d have no %d permission", user_id, per);
      }

      return ret;
    }

    int MetaInfoHelper::do_canned_acl(const common::CANNED_ACL acl, common::MAP_INT64_INT &bucket_acl_map,
          const int64_t owner_id)
    {
        int ret = TFS_SUCCESS;
        switch (acl)
        {
          case PRIVATE:
            bucket_acl_map.insert(make_pair(owner_id, FULL_CONTROL));
            break;
          case PUBLIC_READ:
            bucket_acl_map.insert(make_pair(owner_id, FULL_CONTROL));
            bucket_acl_map.insert(make_pair(OTHER_ROLE, READ));
            break;
          case PUBLIC_READ_WRITE:
            break;
          case AUTHENTICATED_READ:
            break;
          case BUCKET_OWNER_READ:
            break;
          case BUCKET_OWNER_FULL_CONTROL:
            break;
          case LOG_DELIVERY_WRITE:
            break;
          default:
          ret = TFS_ERROR;
          TBSYS_LOG(ERROR, "unexpected error occur, canned_acl: %d", acl);
          break;
        }
        return ret;
    }

    //about bucket acl
    int MetaInfoHelper::put_bucket_acl(const string& bucket_name,
        const MAP_INT64_INT &bucket_acl_map, const UserInfo &user_info)
    {
      int ret = TFS_SUCCESS;

      BucketMetaInfo bucket_meta_info;
      int64_t version = 0;
      if (TFS_SUCCESS == ret)
      {
        ret = head_bucket_ex(bucket_name, &bucket_meta_info);
      }

      //check acl of bucket
      if (TFS_SUCCESS == ret)
      {
        ret = check_bucket_acl(bucket_meta_info.bucket_acl_map_, user_info.owner_id_, WRITE_ACP);
      }

      if (TFS_SUCCESS == ret)
      {
        bucket_meta_info.bucket_acl_map_.clear();
        bucket_meta_info.bucket_acl_map_ = bucket_acl_map;
        ret = put_bucket_ex(bucket_name, bucket_meta_info, version);
      }

      return ret;
    }

    int MetaInfoHelper::put_bucket_acl(const string& bucket_name,
        const CANNED_ACL acl, const UserInfo &user_info)
    {
      int ret = TFS_SUCCESS;

      TBSYS_LOG(DEBUG, "put bucket acl for bucket:%s, user_id:%" PRI64_PREFIX "d, acl:%d",
          bucket_name.c_str(), user_info.owner_id_, acl);

      BucketMetaInfo bucket_meta_info;
      int64_t version = 0;
      if (TFS_SUCCESS == ret)
      {
        ret = head_bucket_ex(bucket_name, &bucket_meta_info);
      }

      //check acl of bucket
      if (TFS_SUCCESS == ret)
      {
        ret = check_bucket_acl(bucket_meta_info.bucket_acl_map_, user_info.owner_id_, WRITE_ACP);
      }

      if (TFS_SUCCESS == ret)
      {
        MAP_INT64_INT bucket_acl_map;
        ret = do_canned_acl(acl, bucket_acl_map, bucket_meta_info.owner_id_);

        if (TFS_SUCCESS == ret)
        {
          bucket_meta_info.bucket_acl_map_.clear();
          bucket_meta_info.bucket_acl_map_ = bucket_acl_map;
          ret = put_bucket_ex(bucket_name, bucket_meta_info, version);
        }
      }

      return ret;
    }

    int MetaInfoHelper::get_bucket_acl(const string& bucket_name,
        common::MAP_INT64_INT *bucket_acl_map, const UserInfo &user_info, int64_t &owner_id)
    {
      int ret = TFS_SUCCESS;
      BucketMetaInfo bucket_meta_info;

      TBSYS_LOG(DEBUG, "get bucket acl for bucket:%s, user_id:%" PRI64_PREFIX "d",
          bucket_name.c_str(), user_info.owner_id_);

      if (TFS_SUCCESS == ret)
      {
        ret = head_bucket_ex(bucket_name, &bucket_meta_info);
      }

      //check acl of bucket
      if (TFS_SUCCESS == ret)
      {
        ret = check_bucket_acl(bucket_meta_info.bucket_acl_map_, user_info.owner_id_, READ_ACP);
      }

      if (TFS_SUCCESS == ret)
      {
        *bucket_acl_map = bucket_meta_info.bucket_acl_map_;
        owner_id = bucket_meta_info.owner_id_;
      }

      return ret;
    }

    bool MetaInfoHelper::is_appid_uid_bucket_name(const string& bucket_name)
    {
      return string::npos != bucket_name.find(APPID_UID_BUCKET_NAME_DELIMITER);
    }

    int MetaInfoHelper::check_bucket_acl(const string& bucket_name,
        const UserInfo &user_info, common::PERMISSION per)
    {
      int ret = TFS_SUCCESS;
      if (!is_appid_uid_bucket_name(bucket_name))
      {
        common::BucketMetaInfo bucket_meta_info;
        if (TFS_SUCCESS == ret)
        {
          ret = head_bucket_ex(bucket_name, &bucket_meta_info);
        }

        //check acl of bucket
        if (TFS_SUCCESS == ret)
        {
          ret = check_bucket_acl(bucket_meta_info.bucket_acl_map_, user_info.owner_id_, per);
        }
      }
      return ret;
    }
  }// end for kvmetaserver
}// end for tfs

