/*
 * (C) 2007-2010 Alibaba Group Holding Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 * Authors:
 *   duanfei<duanfei@taobao.com>
 *      - initial release
 *
 */
#ifndef TFS_MESSAGE_ERASURE_CODE_MESSAGE_H_
#define TFS_MESSAGE_ERASURE_CODE_MESSAGE_H_

#include "base_task_message.h"

namespace tfs
{
  namespace message
  {
    class ECMarshallingMessage : virtual public BaseTaskMessage
    {
      public:
        ECMarshallingMessage();
        virtual ~ECMarshallingMessage();
        virtual int serialize(common::Stream& output) const;
        virtual int deserialize(common::Stream& input);
        virtual int deserialize(const char* data, const int64_t data_len, int64_t& pos);
        virtual int64_t length() const;
        virtual void dump(void) const;
        inline int64_t get_family_id() const { return family_id_;}
        inline int32_t get_family_aid_info() const { return family_aid_info_;}
        inline common::FamilyMemberInfo* get_family_member_info() const { return family_members_;}
        inline void set_family_id(const int64_t family_id) { family_id_= family_id;}
        int set_family_member_info(const common::FamilyMemberInfo* members, const int32_t family_aid_info);
      protected:
        DISALLOW_COPY_AND_ASSIGN(ECMarshallingMessage);
        common::FamilyMemberInfo* family_members_;
        int64_t family_id_;
        int32_t family_aid_info_;
    };

    class ECMarshallingCommitMessage : virtual public ECMarshallingMessage
    {
      public:
      ECMarshallingCommitMessage(const bool forward = false);
      virtual ~ECMarshallingCommitMessage();
      virtual int serialize(common::Stream& output) const;
      virtual int deserialize(common::Stream& input);
      virtual int deserialize(const char* data, const int64_t data_len, int64_t& pos);
      virtual int64_t length() const;
      virtual void dump(void) const;
      inline int8_t get_status() const { return status_;}
      inline void set_status(const int8_t status) { status_ = status;}
      inline bool get_forward() const { return forward_;}
      private:
      DISALLOW_COPY_AND_ASSIGN(ECMarshallingCommitMessage);
      int8_t status_;
      bool forward_;
    };

    class ECReinstateMessage : virtual public ECMarshallingMessage
    {
      public:
        ECReinstateMessage();
        virtual ~ECReinstateMessage();
      private:
        DISALLOW_COPY_AND_ASSIGN(ECReinstateMessage);
    };

    class ECReinstateCommitMessage: virtual public ECMarshallingCommitMessage
    {
      public:
        ECReinstateCommitMessage(const bool forward = false);
        virtual ~ECReinstateCommitMessage();
        virtual int serialize(common::Stream& output) const;
        virtual int deserialize(common::Stream& input);
        virtual int deserialize(const char* data, const int64_t data_len, int64_t& pos);
        virtual int64_t length() const;
        int32_t get_reinstate_num() const;
        common::BlockInfoV2* get_reinstate_block_info();
        int set_reinstate_block_info(common::BlockInfoV2* block_infos, const int32_t reinstate_num);
      private:
        common::BlockInfoV2 block_infos_[common::MAX_MARSHALLING_NUM];
        int32_t reinstate_num_;
        DISALLOW_COPY_AND_ASSIGN(ECReinstateCommitMessage);
    };

    class ECDissolveMessage : virtual public ECMarshallingMessage
    {
      public:
        ECDissolveMessage();
        virtual ~ECDissolveMessage();
      private:
        DISALLOW_COPY_AND_ASSIGN(ECDissolveMessage);
    };

    class ECDissolveCommitMessage : virtual public ECMarshallingCommitMessage
    {
      public:
        ECDissolveCommitMessage(const bool forward = false);
        virtual ~ECDissolveCommitMessage();
      private:
        DISALLOW_COPY_AND_ASSIGN(ECDissolveCommitMessage);
    };
  }
}
#endif /* TFS_MESSAGE_ERASURE_CODE_MESSAGE_H_ */
