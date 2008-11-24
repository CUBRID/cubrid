
#
# Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
#
#   This program is free software; you can redistribute it and/or modify 
#   it under the terms of the GNU General Public License as published by 
#   the Free Software Foundation; version 2 of the License. 
#
#  This program is distributed in the hope that it will be useful, 
#  but WITHOUT ANY WARRANTY; without even the implied warranty of 
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the 
#  GNU General Public License for more details. 
#
#  You should have received a copy of the GNU General Public License 
#  along with this program; if not, write to the Free Software 
#  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA 
#


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

INCLUDES = @SRC_INC@ @EXT_INC@
DEFS += @SYS_DEFS@ @SERVER_DEFS@ @COMMON_DEFS@ @VERSION_DEFS@ @OTHER_DEFS@
LIBS += @EXT_LIBS@ @SYS_LIBS@

#BUILT_SOURCES = cnvlex.c
#CLEANFILES = cnvlex.c

#AM_LFLAGS = -8 -L -S$(COMPAT_DIR)/flex.skel -t
#cnvlex.c: $(COMPAT_DIR)/cnvlex.l
#	$(LEX) $(LFLAGS) $(AM_LFLAGS) $< > $@

COMPAT_SOURCES = \
	$(COMPAT_DIR)/cnv.c \
	$(COMPAT_DIR)/cnvlex.c \
	$(COMPAT_DIR)/db_date.c \
	$(COMPAT_DIR)/db_macro.c \
	$(COMPAT_DIR)/db_set.c
THREAD_SOURCES = \
	$(THREAD_DIR)/thread.c \
	$(THREAD_DIR)/critical_section.c
BASE_SOURCES = \
	$(BASE_DIR)/porting.c \
        $(BASE_DIR)/area_alloc.c \
        $(BASE_DIR)/fixed_alloc.c \
        $(BASE_DIR)/memory_alloc.c \
	$(BASE_DIR)/databases_file.c \
	$(BASE_DIR)/encryption.c \
	$(BASE_DIR)/error_manager.c \
	$(BASE_DIR)/language_support.c \
	$(BASE_DIR)/memory_hash.c \
	$(BASE_DIR)/perf_monitor.c \
	$(BASE_DIR)/ini_parser.c \
	$(BASE_DIR)/system_parameter.c \
	$(BASE_DIR)/release_string.c \
	$(BASE_DIR)/stack_dump.c \
	$(BASE_DIR)/message_catalog.c \
	$(BASE_DIR)/object_representation_sr.c \
	$(BASE_DIR)/adjustable_array.c \
	$(BASE_DIR)/chartype.c \
	$(BASE_DIR)/condition_handler.c \
	$(BASE_DIR)/util_func.c \
	$(BASE_DIR)/intl_support.c \
	$(BASE_DIR)/environment_variable.c \
	$(BASE_DIR)/misc_string.c \
	$(BASE_DIR)/variable_string.c \
	$(BASE_DIR)/stringl.c \
	$(BASE_DIR)/getopt_long.c
HEAPLAYER_SOURCES = \
	$(HEAPLAYER_DIR)/customheaps.cpp
CONNECTION_SOURCES = \
	$(CONNECTION_DIR)/connection_sr.c \
	$(CONNECTION_DIR)/connection_list_sr.c \
	$(CONNECTION_DIR)/connection_globals.c \
	$(CONNECTION_DIR)/tcp.c \
	$(CONNECTION_DIR)/server_support.c \
	$(CONNECTION_DIR)/connection_support.c 
COMMUNICATION_SOURCES = \
	$(COMMUNICATION_DIR)/network_interface_sr.c \
	$(COMMUNICATION_DIR)/network_sr.c \
	$(COMMUNICATION_DIR)/network.c \
PARSER_SOURCES = \
	$(PARSER_DIR)/parse_tree.c
QUERY_SOURCES = \
	$(QUERY_DIR)/arithmetic.c \
	$(QUERY_DIR)/query_dump.c \
	$(QUERY_DIR)/query_evaluator.c \
	$(QUERY_DIR)/fetch.c \
	$(QUERY_DIR)/list_file.c \
	$(QUERY_DIR)/numeric_opfunc.c \
	$(QUERY_DIR)/query_manager.c \
	$(QUERY_DIR)/scan_manager.c \
	$(QUERY_DIR)/serial.c \
	$(QUERY_DIR)/set_scan.c \
	$(QUERY_DIR)/string_opfunc.c \
	$(QUERY_DIR)/method_scan.c \
	$(QUERY_DIR)/query_executor.c \
	$(QUERY_DIR)/query_opfunc.c \
	$(QUERY_DIR)/stream_to_xasl.c
OBJECT_SOURCES = \
	$(OBJECT_DIR)/object_print.c \
	$(OBJECT_DIR)/set_object.c \
	$(OBJECT_DIR)/object_representation.c \
	$(OBJECT_DIR)/object_primitive.c \
	$(OBJECT_DIR)/transform.c \
	$(OBJECT_DIR)/object_domain.c
JSP_SOURCES = \
	$(JSP_DIR)/jsp_sr.c
TRANSACTION_SOURCES = \
	$(TRANSACTION_DIR)/log_compress.c \
	$(TRANSACTION_DIR)/boot_sr.c \
	$(TRANSACTION_DIR)/locator.c	\
	$(TRANSACTION_DIR)/locator_sr.c \
	$(TRANSACTION_DIR)/lock_table.c \
	$(TRANSACTION_DIR)/lock_manager.c \
	$(TRANSACTION_DIR)/log_manager.c \
	$(TRANSACTION_DIR)/log_2pc.c \
	$(TRANSACTION_DIR)/log_global.c \
	$(TRANSACTION_DIR)/log_page_buffer.c \
	$(TRANSACTION_DIR)/log_recovery.c \
	$(TRANSACTION_DIR)/log_tran_table.c \
	$(TRANSACTION_DIR)/log_comm.c \
	$(TRANSACTION_DIR)/replication.c \
	$(TRANSACTION_DIR)/recovery.c \
	$(TRANSACTION_DIR)/transaction_sr.c \
	$(TRANSACTION_DIR)/wait_for_graph.c
STORAGE_SOURCES = \
	$(STORAGE_DIR)/btree.c \
	$(STORAGE_DIR)/btree_load.c \
	$(STORAGE_DIR)/system_catalog.c \
	$(STORAGE_DIR)/catalog_class.c \
	$(STORAGE_DIR)/disk_manager.c \
	$(STORAGE_DIR)/storage_common.c \
	$(STORAGE_DIR)/extendible_hash.c \
	$(STORAGE_DIR)/file_manager.c \
	$(STORAGE_DIR)/heap_file.c \
	$(STORAGE_DIR)/file_io.c \
	$(STORAGE_DIR)/large_object_directory.c \
	$(STORAGE_DIR)/large_object.c \
	$(STORAGE_DIR)/oid.c \
	$(STORAGE_DIR)/overflow_file.c \
	$(STORAGE_DIR)/page_buffer.c \
	$(STORAGE_DIR)/statistics_sr.c \
	$(STORAGE_DIR)/slotted_page.c \
	$(STORAGE_DIR)/external_sort.c

lib_LTLIBRARIES = libcubrid.la
libcubrid_la_SOURCES = \
	$(COMPAT_SOURCES) \
	$(THREAD_SOURCES) \
	$(BASE_SOURCES) \
	$(HEAPLAYER_SOURCES) \
	$(CONNECTION_SOURCES) \
	$(COMMUNICATION_SOURCES) \
	$(PARSER_SOURCES) \
	$(QUERY_SOURCES) \
	$(OBJECT_SOURCES) \
	$(JSP_SOURCES) \
	$(TRANSACTION_SOURCES) \
	$(STORAGE_SOURCES) \
	$(EXECUTABLE_SOURCES)
libcubrid_la_LDFLAGS = -version-number @LT_VERSION@
libcubrid_la_LIBADD = $(LIBADD_DL)
libcubrid_la_LINK = $(LINK)
