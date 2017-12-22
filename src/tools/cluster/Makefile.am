AM_CPPFLAGS=-I$(top_srcdir)/src\
	    -I$(TBLIB_ROOT)/include/tbsys\
	    -I$(TBLIB_ROOT)/include/tbnet\
			-I$(EASY_ROOT)/include/easy\
      $(READLINE_INCLUDE)\
		  -I$(TREST_ROOT)/include

AM_LDFLAGS=-lrt -lpthread -ldl $(READLINE_LIB) -ljson -lcurl -lssl -lcrypto

bin_PROGRAMS = sync_by_blk sync_by_file  transfer split_item_tool
LDADD = $(top_builddir)/src/tools/util/libtfstoolsutil.a\
  $(top_builddir)/src/requester/libtfssyncutil.a\
  $(top_builddir)/src/clientv2/.libs/libtfsclientv2.a\
  $(top_builddir)/src/requester/libtfsrequester.a\
	$(top_builddir)/src/message/libtfsmessage.a\
	$(top_builddir)/src/common/libtfscommon.a\
	$(TAIR_CACHE_LDFLAGS)\
	$(TBLIB_ROOT)/lib/libtbnet.a\
	$(TBLIB_ROOT)/lib/libtbsys.a\
	$(EASY_ROOT)/lib64/libeasy.a

sync_by_blk_SOURCES = sync_by_blk_v2.cpp sync_file_base_v2.cpp sync_file_base_v2.h
sync_by_file_SOURCES = sync_by_file_v2.cpp sync_file_base_v2.cpp sync_file_base_v2.h
transfer_SOURCES=transfer.cpp transfer.h

split_item_tool_SOURCES=split_item_tool.cpp

#block_toolv2_SOURCES=block_tool_v2.cpp

#can't compile transfer_from_filesystem.cpp as lack of libtfs_cpp_restful_client.a
#transfer_from_filesystem_SOURCES = transfer_from_filesystem.cpp
