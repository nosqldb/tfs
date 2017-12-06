/*
 * (C) 2007-2012 Alibaba Group Holding Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 * Version: $Id
 *
 * Authors:
 *   duanfei <duanfei@taobao.com>
 *      - initial release
 *
 */
#include "family_collect.h"

using namespace tfs::common;
namespace tfs
{
  namespace nameserver
  {
    FamilyCollect::FamilyCollect(const int64_t family_id):
      BaseObject<LayoutManager>(0),
      members_(NULL),
      family_id_(family_id),
      family_aid_info_(0),
      in_reinstate_or_dissolve_queue_(FAMILY_IN_REINSTATE_OR_DISSOLVE_QUEUE_NO)
    {
      //for query
    }

    FamilyCollect::FamilyCollect(const int64_t family_id, const int32_t family_aid_info, const time_t now):
      BaseObject<LayoutManager>(now),
      members_(NULL),
      family_id_(family_id),
      family_aid_info_(family_aid_info),
      in_reinstate_or_dissolve_queue_(FAMILY_IN_REINSTATE_OR_DISSOLVE_QUEUE_NO)
    {
      const int32_t MEMBER_NUM = get_data_member_num() + get_check_member_num();
      members_ = new (std::nothrow)std::pair<uint64_t, int32_t>[MEMBER_NUM];
      memset(members_, 0, (sizeof(std::pair<uint64_t, int32_t>) * MEMBER_NUM));
      assert(NULL != members_);
    }

    FamilyCollect::~FamilyCollect()
    {
      tbsys::gDeleteA(members_);
    }

    int FamilyCollect::add(const uint64_t block, const int32_t version)
    {
      bool complete = false;
      int32_t ret = INVALID_BLOCK_ID != block && version >= 0 ? TFS_SUCCESS : EXIT_PARAMETER_ERROR;
      if (TFS_SUCCESS == ret)
      {
        const int32_t MEMBER_NUM = get_data_member_num() + get_check_member_num();
        for (int32_t i = 0; i < MEMBER_NUM && !complete; ++i)
        {
          complete = INVALID_BLOCK_ID == members_[i].first;
          if (complete)
          {
            members_[i].first = block;
            members_[i].second= version;
          }
        }
      }
      return complete ? TFS_SUCCESS : TFS_SUCCESS == ret ? EXIT_OUT_OF_RANGE : ret;
    }

    int FamilyCollect::add(const common::ArrayHelper<std::pair<uint64_t, int32_t> >& members)
    {
      const int32_t MEMBER_NUM = get_data_member_num() + get_check_member_num();
      int32_t ret = (members.get_array_index() > MEMBER_NUM || members.get_array_index() <= 0)
             ? EXIT_PARAMETER_ERROR : TFS_SUCCESS;
      if (TFS_SUCCESS == ret)
      {
        for (int64_t index = 0; index < members.get_array_index(); ++index)
        {
          members_[index] = *members.at(index);
        }
      }
      return ret;
    }

    int FamilyCollect::update(const uint64_t block, const int32_t version)
    {
      bool complete = false;
      int32_t ret = INVALID_BLOCK_ID != block && version > 0 ? TFS_SUCCESS : EXIT_PARAMETER_ERROR;
      if (TFS_SUCCESS == ret)
      {
        const int32_t MEMBER_NUM = get_data_member_num() + get_check_member_num();
        for (int32_t i = 0; i < MEMBER_NUM && !complete; ++i)
        {
          complete = block == members_[i].first;
          if (complete)
            members_[i].second = version;
        }
      }
      return complete ? TFS_SUCCESS : TFS_SUCCESS == ret ? EXIT_NO_BLOCK : ret;
    }

    bool FamilyCollect::exist(const uint64_t block) const
    {
      bool ret = INVALID_BLOCK_ID != block;
      if (ret)
      {
        ret = false;
        const int32_t MEMBER_NUM = get_data_member_num() + get_check_member_num();
        for (int32_t i = 0; i < MEMBER_NUM && !ret; ++i)
        {
          ret = members_[i].first == block;
        }
      }
      return ret;
    }

    bool FamilyCollect::exist(int32_t& current_version, const uint64_t block, const int32_t version) const
    {
      bool ret = INVALID_BLOCK_ID != block;
      if (ret)
      {
        ret = false;
        const int32_t MEMBER_NUM = get_data_member_num() + get_check_member_num();
        for (int32_t i = 0; i < MEMBER_NUM && !ret; ++i)
        {
          ret = members_[i].first == block && version >= members_[i].second;
          if (ret)
            current_version = members_[i].second;
        }
      }
      return ret;
    }

    void FamilyCollect::get_members(ArrayHelper<std::pair<uint64_t, int32_t> >& members) const
    {
      const int32_t MEMBER_NUM = get_data_member_num() + get_check_member_num();
      for (int32_t i = 0; i < MEMBER_NUM; ++i)
      {
        members.push_back((members_[i]));
      }
    }

    int FamilyCollect::scan(SSMScanParameter& param) const
    {
      param.data_.writeInt64(family_id_);
      param.data_.writeInt32(family_aid_info_);
      const int32_t MEMBER_NUM = get_data_member_num() + get_check_member_num();
      for (int32_t i = 0; i < MEMBER_NUM ; ++i)
      {
        param.data_.writeInt64(members_[i].first);//block_id
        param.data_.writeInt32(members_[i].second);//version
      }
      return TFS_SUCCESS;
    }

    void FamilyCollect::dump(int32_t level, const char* file, const int32_t line, const char* function, const pthread_t thid) const
    {
      if (level >= TBSYS_LOGGER._level)
      {
        std::ostringstream str;
        str <<"family_id: " << family_id_ <<",data_member_num: " << get_data_member_num() << ",check_member_num: "<<
          get_check_member_num() << ",code_type:" << get_code_type() << ",master_index: " << get_master_index();
        int32_t i = 0;
        str << ", data_members: ";
        for (i = 0; i < get_data_member_num(); ++i)
        {
          str <<members_[i].first <<":" << members_[i].second<< ",";
        }
        str << ", check_members: ";
        const int32_t MEMBER_NUM = get_data_member_num() + get_check_member_num();
        for (; i < MEMBER_NUM; ++i)
        {
          str <<members_[i].first <<":" << members_[i].second<< ",";
        }
        TBSYS_LOGGER.logMessage(level, file, line, function, thid,"%s", str.str().c_str());
      }
    }

    bool FamilyCollect::check_need_reinstate(const time_t now) const
    {
      return now > get();
    }

    bool FamilyCollect::check_need_dissolve(const time_t now) const
    {
      return now > get();
    }
  }/** end namespace nameserver **/
}/** end namespace tfs **/
