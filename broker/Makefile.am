
#
# Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
#
#   This program is free software; you can redistribute it and/or modify 
#   it under the terms of the GNU General Public License as published by 
#   the Free Software Foundation; either version 2 of the License, or 
#   (at your option) any later version. 
#
#  This program is distributed in the hope that it will be useful, 
#  but WITHOUT ANY WARRANTY; without even the implied warranty of 
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the 
#  GNU General Public License for more details. 
#
#  You should have received a copy of the GNU General Public License 
#  along with this program; if not, write to the Free Software 
#  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
#

compatdir=@CUBRID_COMPATDIR@

SRC = $(top_srcdir)/src
COMPAT_DIR = $(SRC)/compat
THREAD_DIR = $(SRC)/thread
BASE_DIR = $(SRC)/base
HEAPLAYER_DIR = $(SRC)/heaplayers
CONNECTION_DIR = $(SRC)/connection
COMMUNICATION_DIR = $(SRC)/communication
PARSER_DIR = $(SRC)/parser
OPTIMIZER_DIR = $(SRC)/optimizer
QUERY_DIR = $(SRC)/query
OBJECT_DIR = $(SRC)/object
JSP_DIR = $(SRC)/jsp
TRANSACTION_DIR = $(SRC)/transaction
STORAGE_DIR = $(SRC)/storage
EXECUTABLE_DIR = $(SRC)/executables
BROKER_DIR = $(SRC)/broker
CCI_DIR = $(SRC)/cci
DBMT_DIR = $(SRC)/dbmt
QATOOLS_DIR = $(SRC)/qatools

AM_CPPFLAGS = @SRC_INC@ @EXT_INC@ \
	@SYS_DEFS@ @VERSION_DEFS@ -DASYNC_MODE -D_XOPEN_SOURCE -D_BSD_SOURCE \
	-DDIAG_DEVEL -DCAS_BROKER @DIR_DEFS@

BROKER_CAS_LIBS = @CS_LIB@
LOG_RUNNER_LIBS = @CS_LIB@ @CCI_LIB@

include_HEADERS = $(BROKER_DIR)/cas_error.h

bin_PROGRAMS = cub_broker cubrid_broker cub_cas \
	broker_monitor broker_changer \
	broker_log_converter broker_log_runner broker_log_top

dist_compat_SCRIPTS = uc

cub_broker_SOURCES = \
	$(BROKER_DIR)/broker.c \
	$(BROKER_DIR)/broker_shm.c \
	$(BROKER_DIR)/broker_error.c \
	$(BROKER_DIR)/broker_util.c \
	$(BROKER_DIR)/broker_process_size.c \
	$(BROKER_DIR)/broker_max_heap.c \
	$(BROKER_DIR)/broker_access_list.c \
	$(BROKER_DIR)/broker_send_fd.c \
	$(BROKER_DIR)/broker_filename.c
cub_broker_LDADD = $(BROKER_CAS_LIBS)

broker_monitor_SOURCES = \
	$(BROKER_DIR)/broker_monitor.c \
	$(BROKER_DIR)/broker_config.c \
	$(BROKER_DIR)/broker_shm.c \
	$(BROKER_DIR)/broker_error.c \
	$(BROKER_DIR)/broker_util.c \
	$(BROKER_DIR)/broker_process_size.c \
	$(BROKER_DIR)/broker_max_heap.c \
	$(BROKER_DIR)/broker_process_info.c \
	$(BROKER_DIR)/broker_filename.c
broker_monitor_LDADD = $(BROKER_CAS_LIBS)

broker_changer_SOURCES = \
	$(BROKER_DIR)/broker_changer.c \
	$(BROKER_DIR)/broker_admin_pub.c \
	$(BROKER_DIR)/broker_shm.c \
	$(BROKER_DIR)/broker_error.c \
	$(BROKER_DIR)/broker_process_size.c \
	$(BROKER_DIR)/broker_config.c \
	$(BROKER_DIR)/broker_util.c \
	$(BROKER_DIR)/broker_max_heap.c \
	$(BROKER_DIR)/broker_filename.c
broker_changer_LDADD = $(BROKER_CAS_LIBS)

cubrid_broker_SOURCES = \
	$(BROKER_DIR)/broker_config.c \
	$(BROKER_DIR)/broker_admin.c \
	$(BROKER_DIR)/broker_shm.c \
	$(BROKER_DIR)/broker_error.c \
	$(BROKER_DIR)/broker_util.c \
	$(BROKER_DIR)/broker_admin_pub.c \
	$(BROKER_DIR)/broker_process_size.c \
	$(BROKER_DIR)/broker_max_heap.c \
	$(BROKER_DIR)/broker_filename.c
cubrid_broker_LDADD = $(BROKER_CAS_LIBS)

cub_cas_SOURCES = \
	$(BROKER_DIR)/cas.c \
	$(BROKER_DIR)/cas_network.c \
	$(BROKER_DIR)/cas_net_buf.c \
	$(BROKER_DIR)/cas_function.c \
	$(BROKER_DIR)/cas_execute.c \
	$(BROKER_DIR)/cas_handle.c \
	$(BROKER_DIR)/broker_util.c \
	$(BROKER_DIR)/cas_str_like.c \
	$(BROKER_DIR)/broker_recv_fd.c \
	$(BROKER_DIR)/cas_xa.c \
	$(BROKER_DIR)/cas_schema_info.c \
	$(BROKER_DIR)/cas_log.c \
	$(BROKER_DIR)/broker_shm.c \
	$(BROKER_DIR)/broker_process_size.c \
	$(BROKER_DIR)/cas_sql_log2.c \
	$(BROKER_DIR)/broker_filename.c \
	$(BROKER_DIR)/broker_error.c \
	$(BROKER_DIR)/cas_error.c
cub_cas_CPPFLAGS = $(AM_CPPFLAGS) -DCAS_CUBRID
cub_cas_LDADD = $(BROKER_CAS_LIBS)

broker_log_converter_SOURCES = \
	$(BROKER_DIR)/broker_log_converter.c \
	$(BROKER_DIR)/broker_log_util.c \
	$(BROKER_DIR)/log_top_string.c 
broker_log_converter_CPPFLAGS = $(AM_CPPFLAGS) -DBROKER_LOG_CONVERTER
broker_log_converter_LDADD = $(BROKER_CAS_LIBS)

broker_log_runner_SOURCES = \
	$(BROKER_DIR)/cas_runner.c \
	$(BROKER_DIR)/broker_log_util.c \
	$(BROKER_DIR)/log_top_string.c
broker_log_runner_CPPFLAGS = $(AM_CPPFLAGS) -DBROKER_LOG_RUNNER
broker_log_runner_LDADD = $(LOG_RUNNER_LIBS)

broker_log_top_SOURCES = \
	$(BROKER_DIR)/broker_log_top.c \
	$(BROKER_DIR)/cas_query_info.c \
	$(BROKER_DIR)/broker_log_time.c \
	$(BROKER_DIR)/broker_log_sql_list.c \
	$(BROKER_DIR)/broker_log_util.c \
	$(BROKER_DIR)/log_top_string.c \
	$(BROKER_DIR)/broker_log_top_tran.c
broker_log_top_CPPFLAGS = $(AM_CPPFLAGS) -DBROKER_LOG_TOP
broker_log_top_LDADD = $(BROKER_CAS_LIBS)

lib_LTLIBRARIES = libbrokeradmin.la
libbrokeradmin_la_SOURCES = \
	$(BROKER_DIR)/broker_admin_so.c \
	$(BROKER_DIR)/broker_admin_pub.c \
	$(BROKER_DIR)/broker_shm.c \
	$(BROKER_DIR)/broker_error.c \
	$(BROKER_DIR)/broker_util.c \
	$(BROKER_DIR)/broker_config.c \
	$(BROKER_DIR)/broker_max_heap.c \
	$(BROKER_DIR)/broker_process_size.c \
	$(BROKER_DIR)/broker_process_info.c \
	$(BROKER_DIR)/broker_filename.c
libbrokeradmin_la_CPPFLAGS = $(AM_CPPFLAGS) -D_UC_ADMIN_SO_ -DDIAG_DEVEL
libbrokeradmin_la_LDFLAGS = -version-number @LT_VERSION@
libbrokeradmin_la_LIBADD = @CS_LIB@

VERSIONED_FILES = \
	$(BROKER_DIR)/broker_admin.c \
	$(BROKER_DIR)/broker_changer.c \
	$(BROKER_DIR)/broker_monitor.c \
	$(BROKER_DIR)/broker_admin_so.c

$(VERSIONED_FILES): $(top_srcdir)/BUILD_NUMBER
	touch $@
