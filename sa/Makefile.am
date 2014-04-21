
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
SESSION_DIR = $(SRC)/session
SYSTEMTAP_DIR = $(top_builddir)/contrib/systemtap

FLEX = flex

AM_CPPFLAGS = @SRC_INC@ -I$(HEAPLAYER_DIR)/util @EXT_INC@ @JAVA_INC@ \
	@SYS_DEFS@ @SA_DEFS@ @COMMON_DEFS@ @VERSION_DEFS@ @DIR_DEFS@
if ENABLE_SYSTEMTAP
   AM_CPPFLAGS += -I$(SYSTEMTAP_DIR)
endif

#BUILT_SOURCES = cnvlex.c
#CLEANFILES = cnvlex.c

$(PARSER_DIR)/csql_grammar.c $(PARSER_DIR)/csql_grammar.h : $(PARSER_DIR)/csql_grammar.y
	$(BISON) --name-prefix=csql_yy -d -r all -o $@ $<

$(PARSER_DIR)/csql_lexer.c : $(PARSER_DIR)/csql_lexer.l
	$(FLEX) --never-interactive --prefix=csql_yy -o $@ $<

$(EXECUTABLE_DIR)/loader_grammar.c : $(EXECUTABLE_DIR)/loader_grammar.y
	$(BISON) --name-prefix=loader_yy -d -r all -o $@ $<

$(EXECUTABLE_DIR)/loader_lexer.c : $(EXECUTABLE_DIR)/loader_lexer.l
	$(FLEX) --never-interactive --prefix=loader_yy -o $@ $<

#AM_LFLAGS = -8 -L -S$(COMPAT_DIR)/flex.skel -t
#cnvlex.c: $(COMPAT_DIR)/cnvlex.l
#	$(LEX) $(LFLAGS) $(AM_LFLAGS) $< > $@

#API_SOURCES = \
#	$(SRC)/api/db_collection.c \
#	$(SRC)/api/db_object.c \
#	$(SRC)/api/api_util.c \
#	$(SRC)/api/api_value_indexer.c \
#	$(SRC)/api/db_value_table.c \
#	$(SRC)/api/api_handle.c \
#	$(SRC)/api/db_stub.c \
#	$(SRC)/api/cubrid_api.c

EXECUTABLE_SOURCES = \
	$(EXECUTABLE_DIR)/csql.c \
	$(EXECUTABLE_DIR)/csql_session.c \
	$(EXECUTABLE_DIR)/csql_result.c \
	$(EXECUTABLE_DIR)/csql_support.c \
	$(EXECUTABLE_DIR)/csql_result_format.c \
	$(EXECUTABLE_DIR)/util_sa.c \
	$(EXECUTABLE_DIR)/util_cs.c \
	$(EXECUTABLE_DIR)/util_common.c \
	$(EXECUTABLE_DIR)/compactdb.c \
	$(EXECUTABLE_DIR)/loaddb.c \
	$(EXECUTABLE_DIR)/unloaddb.c \
	$(EXECUTABLE_DIR)/load_object.c \
	$(EXECUTABLE_DIR)/unload_object.c \
	$(EXECUTABLE_DIR)/unload_schema.c \
	$(EXECUTABLE_DIR)/file_hash.c \
	$(EXECUTABLE_DIR)/loader.c \
	$(EXECUTABLE_DIR)/loader_object_table.c \
	$(EXECUTABLE_DIR)/loader_grammar.c \
	$(EXECUTABLE_DIR)/loader_lexer.c 

COMPAT_SOURCES = \
	$(COMPAT_DIR)/cnv.c \
	$(COMPAT_DIR)/cnvlex.c \
	$(COMPAT_DIR)/db_admin.c \
	$(COMPAT_DIR)/db_class.c \
	$(COMPAT_DIR)/db_date.c \
	$(COMPAT_DIR)/db_info.c	\
	$(COMPAT_DIR)/db_elo.c	\
	$(COMPAT_DIR)/db_macro.c \
	$(COMPAT_DIR)/db_obj.c \
	$(COMPAT_DIR)/db_old.c \
	$(COMPAT_DIR)/db_query.c \
	$(COMPAT_DIR)/db_set.c \
	$(COMPAT_DIR)/db_temp.c	\
	$(COMPAT_DIR)/db_vdb.c \
	$(COMPAT_DIR)/db_virt.c

BASE_SOURCES = \
	$(BASE_DIR)/dynamic_array.c \
	$(BASE_DIR)/porting.c \
	$(BASE_DIR)/area_alloc.c \
	$(BASE_DIR)/fixed_alloc.c \
	$(BASE_DIR)/memory_alloc.c \
	$(BASE_DIR)/databases_file.c \
	$(BASE_DIR)/encryption.c \
	$(BASE_DIR)/sha1.c \
	$(BASE_DIR)/md5.c \
	$(BASE_DIR)/base64.c \
	$(BASE_DIR)/error_manager.c \
	$(BASE_DIR)/language_support.c \
	$(BASE_DIR)/locale_support.c \
	$(BASE_DIR)/xml_parser.c \
	$(BASE_DIR)/uca_support.c \
	$(BASE_DIR)/unicode_support.c \
	$(BASE_DIR)/memory_hash.c \
	$(BASE_DIR)/perf_monitor.c \
	$(BASE_DIR)/ini_parser.c \
	$(BASE_DIR)/system_parameter.c \
	$(BASE_DIR)/release_string.c \
	$(BASE_DIR)/stack_dump.c \
	$(BASE_DIR)/message_catalog.c \
	$(BASE_DIR)/dynamic_load.c \
	$(BASE_DIR)/object_representation_sr.c \
	$(BASE_DIR)/ieeefp.h \
	$(BASE_DIR)/mprec.h \
	$(BASE_DIR)/mprec.c \
	$(BASE_DIR)/dtoa.c \
	$(BASE_DIR)/adjustable_array.c \
	$(BASE_DIR)/chartype.c \
	$(BASE_DIR)/condition_handler.c \
	$(BASE_DIR)/util_func.c \
	$(BASE_DIR)/intl_support.c \
	$(BASE_DIR)/environment_variable.c \
	$(BASE_DIR)/misc_string.c \
	$(BASE_DIR)/variable_string.c \
	$(BASE_DIR)/getopt_long.c \
	$(BASE_DIR)/binaryheap.c \
	$(BASE_DIR)/tsc_timer.c \
	$(BASE_DIR)/perf.c

HEAPLAYER_SOURCES = \
	$(HEAPLAYER_DIR)/customheaps.cpp \
	$(HEAPLAYER_DIR)/lea_heap.c

CONNECTION_SOURCES = \
	$(CONNECTION_DIR)/connection_less.c \
	$(CONNECTION_DIR)/connection_cl.c \
	$(CONNECTION_DIR)/connection_globals.c \
	$(CONNECTION_DIR)/connection_list_cl.c \
	$(CONNECTION_DIR)/tcp.c \
	$(CONNECTION_DIR)/client_support.c \
	$(CONNECTION_DIR)/connection_support.c

COMMUNICATION_SOURCES = \
	$(COMMUNICATION_DIR)/network_interface_cl.c

PARSER_SOURCES = \
	$(PARSER_DIR)/csql_grammar.c \
	$(PARSER_DIR)/csql_lexer.c \
	$(PARSER_DIR)/parser_support.c \
	$(PARSER_DIR)/show_meta.c \
	$(PARSER_DIR)/double_byte_support.c \
	$(PARSER_DIR)/method_transform.c \
	$(PARSER_DIR)/cnf.c \
	$(PARSER_DIR)/compile.c \
	$(PARSER_DIR)/parse_dbi.c \
	$(PARSER_DIR)/parse_evaluate.c \
	$(PARSER_DIR)/keyword.c \
	$(PARSER_DIR)/parse_tree_cl.c \
	$(PARSER_DIR)/parse_tree.c \
	$(PARSER_DIR)/query_result.c \
	$(PARSER_DIR)/name_resolution.c \
	$(PARSER_DIR)/semantic_check.c \
	$(PARSER_DIR)/type_checking.c \
	$(PARSER_DIR)/xasl_generation.c \
	$(PARSER_DIR)/scanner_support.c \
	$(PARSER_DIR)/view_transform.c

OPTIMIZER_SOURCES = \
	$(OPTIMIZER_DIR)/query_bitset.c \
	$(OPTIMIZER_DIR)/query_graph.c \
	$(OPTIMIZER_DIR)/query_planner.c \
	$(OPTIMIZER_DIR)/plan_generation.c \
	$(OPTIMIZER_DIR)/query_rewrite.c

QUERY_SOURCES = \
	$(QUERY_DIR)/execute_schema.c \
	$(QUERY_DIR)/execute_statement.c \
	$(QUERY_DIR)/arithmetic.c \
	$(QUERY_DIR)/cursor.c \
	$(QUERY_DIR)/query_dump.c \
	$(QUERY_DIR)/query_evaluator.c \
	$(QUERY_DIR)/fetch.c \
	$(QUERY_DIR)/list_file.c \
	$(QUERY_DIR)/numeric_opfunc.c \
	$(QUERY_DIR)/query.c \
	$(QUERY_DIR)/query_manager.c \
	$(QUERY_DIR)/scan_manager.c \
	$(QUERY_DIR)/serial.c \
	$(QUERY_DIR)/set_scan.c \
	$(QUERY_DIR)/string_opfunc.c \
	$(QUERY_DIR)/method_scan.c \
	$(QUERY_DIR)/query_executor.c \
	$(QUERY_DIR)/show_scan.c \
	$(QUERY_DIR)/partition.c \
	$(QUERY_DIR)/query_opfunc.c \
	$(QUERY_DIR)/xasl_to_stream.c \
	$(QUERY_DIR)/stream_to_xasl.c \
	$(QUERY_DIR)/crypt_opfunc.c

OBJECT_SOURCES = \
	$(OBJECT_DIR)/elo.c \
	$(OBJECT_DIR)/authenticate.c \
	$(OBJECT_DIR)/object_print.c \
	$(OBJECT_DIR)/object_accessor.c \
	$(OBJECT_DIR)/object_template.c \
	$(OBJECT_DIR)/set_object.c \
	$(OBJECT_DIR)/trigger_manager.c \
	$(OBJECT_DIR)/virtual_object.c \
	$(OBJECT_DIR)/class_object.c \
	$(OBJECT_DIR)/schema_manager.c \
	$(OBJECT_DIR)/schema_template.c \
	$(OBJECT_DIR)/object_representation.c \
	$(OBJECT_DIR)/object_primitive.c \
	$(OBJECT_DIR)/quick_fit.c \
	$(OBJECT_DIR)/transform_cl.c \
	$(OBJECT_DIR)/transform.c \
	$(OBJECT_DIR)/object_domain.c \
	$(OBJECT_DIR)/work_space.c

JSP_SOURCES = \
	$(JSP_DIR)/jsp_cl.c \
	$(JSP_DIR)/jsp_sr.c

TRANSACTION_SOURCES = \
	$(TRANSACTION_DIR)/log_compress.c \
	$(TRANSACTION_DIR)/boot_cl.c \
	$(TRANSACTION_DIR)/boot_sr.c \
	$(TRANSACTION_DIR)/locator.c \
	$(TRANSACTION_DIR)/locator_cl.c \
	$(TRANSACTION_DIR)/locator_sr.c \
	$(TRANSACTION_DIR)/lock_manager.c \
	$(TRANSACTION_DIR)/lock_table.c \
	$(TRANSACTION_DIR)/log_2pc.c \
	$(TRANSACTION_DIR)/log_comm.c \
	$(TRANSACTION_DIR)/log_global.c \
	$(TRANSACTION_DIR)/log_manager.c \
	$(TRANSACTION_DIR)/log_page_buffer.c \
	$(TRANSACTION_DIR)/log_recovery.c \
	$(TRANSACTION_DIR)/log_tran_table.c \
	$(TRANSACTION_DIR)/replication.c \
	$(TRANSACTION_DIR)/recovery_cl.c \
	$(TRANSACTION_DIR)/recovery.c \
	$(TRANSACTION_DIR)/transaction_cl.c \
	$(TRANSACTION_DIR)/transaction_sr.c \
	$(TRANSACTION_DIR)/wait_for_graph.c \
	$(TRANSACTION_DIR)/log_writer.c

STORAGE_SOURCES = \
	$(STORAGE_DIR)/btree.c \
	$(STORAGE_DIR)/btree_load.c \
	$(STORAGE_DIR)/system_catalog.c \
	$(STORAGE_DIR)/catalog_class.c \
	$(STORAGE_DIR)/compactdb_sr.c \
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
	$(STORAGE_DIR)/statistics_cl.c \
	$(STORAGE_DIR)/statistics_sr.c \
	$(STORAGE_DIR)/slotted_page.c \
	$(STORAGE_DIR)/external_sort.c \
	$(STORAGE_DIR)/es_common.c \
	$(STORAGE_DIR)/es.c \
	$(STORAGE_DIR)/es_posix.c \
	$(STORAGE_DIR)/es_owfs.c

SESSION_SOURCES = \
	$(SESSION_DIR)/session.c \
	$(SESSION_DIR)/session_sr.c

lib_LTLIBRARIES = libcubridsa.la
libcubridsa_la_SOURCES = \
	$(EXECUTABLE_SOURCES) \
	$(COMPAT_SOURCES) \
	$(BASE_SOURCES) \
	$(HEAPLAYER_SOURCES) \
	$(CONNECTION_SOURCES) \
	$(COMMUNICATION_SOURCES) \
	$(PARSER_SOURCES) \
	$(OPTIMIZER_SOURCES) \
	$(QUERY_SOURCES) \
	$(OBJECT_SOURCES) \
	$(JSP_SOURCES) \
	$(TRANSACTION_SOURCES) \
	$(STORAGE_SOURCES) \
	$(ES_SOURCES) \
	$(SESSION_SOURCES)
#	$(API_SOURCES)
libcubridsa_la_LDFLAGS = -version-number @LT_VERSION@
libcubridsa_la_LIBADD = @CAS_LIB@ @EXT_LIBS@ @SYS_LIBS@ $(LIBADD_DL)
libcubridsa_la_DEPENDENCIES = @CAS_LIB@ @EXT_LIBS@
libcubridsa_la_LINK = $(LINK)

VERSIONED_FILES = \
	$(BASE_DIR)/release_string.c

$(VERSIONED_FILES): $(top_srcdir)/BUILD_NUMBER
	touch $@
