
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

ANTLR = $(top_builddir)/pccts/antlrd/antlr
DLG = $(top_builddir)/pccts/dlg/dlg

INCLUDES = @SRC_INC@ -I$(top_srcdir)/pccts/h @EXT_INC@
DEFS += @SYS_DEFS@ @CS_DEFS@ @COMMON_DEFS@ @VERSION_DEFS@ @OTHER_DEFS@ -DLL_K=2 -DZZCOL
LIBS += @CAS_LIB@ @EXT_LIBS@ @SYS_LIBS@

BUILT_SOURCES = sqlx_grammar.c sqerr.c sq.dlg sqtokens.h sqmode.h sqscan.c \
	ld.c lderr.c ld.dlg ldtokens.h ldmode.h ldscan.c
#BUILT_SOURCES = cnvlex.c
CLEANFILES = sqlx_grammar.c sqerr.c sq.dlg sqtokens.h sqmode.h sqscan.c \
	ld.c lderr.c ld.dlg ldtokens.h ldmode.h ldscan.c
#CLEANFILES = cnvlex.c

sqlx_grammar.c sqerr.c sq.dlg: $(PARSER_DIR)/sqlx_grammar.g
	$(ANTLR) -ga -k 2 -fe sqerr.c -fl sq.dlg -ft sqtokens.h -fm sqmode.h $<
sqscan.c sqmode.h: sq.dlg
	$(DLG) -C2 -ga -m sqmode.h $< sqscan.c

ld.c lderr.c ld.dlg: $(EXECUTABLE_DIR)/ld.g
	$(ANTLR) -ga -k 1 -fe lderr.c -fl ld.dlg -ft ldtokens.h -fm ldmode.h $<
ldscan.c ldmode.h: ld.dlg
	$(DLG) -C2 -ga -m ldmode.h $< ldscan.c

#AM_LFLAGS = -8 -L -S$(COMPAT_DIR)/flex.skel -t
#cnvlex.c: $(COMPAT_DIR)/cnvlex.l
#	$(LEX) $(LFLAGS) $(AM_LFLAGS) $< > $@

API_SOURCES = \
	$(SRC)/api/db_collection.c \
	$(SRC)/api/db_object.c \
	$(SRC)/api/api_util.c \
	$(SRC)/api/api_value_indexer.c \
	$(SRC)/api/db_value_table.c \
	$(SRC)/api/api_handle.c \
	$(SRC)/api/db_stub.c \
	$(SRC)/api/cubrid_api.c
EXECUTABLE_SOURCES = \
	$(EXECUTABLE_DIR)/sqlx.c \
	$(EXECUTABLE_DIR)/sqlx_session.c \
	$(EXECUTABLE_DIR)/sqlx_result.c \
	$(EXECUTABLE_DIR)/iqutil.c \
	$(EXECUTABLE_DIR)/sqlx_result_format.c \
	$(EXECUTABLE_DIR)/util_cs.c \
	$(EXECUTABLE_DIR)/util_common.c \
	$(EXECUTABLE_DIR)/loaddb.c \
	$(EXECUTABLE_DIR)/unloaddb.c \
	$(EXECUTABLE_DIR)/load_object.c \
	$(EXECUTABLE_DIR)/extract_object.c \
	$(EXECUTABLE_DIR)/extract_schema.c \
	$(EXECUTABLE_DIR)/file_hash.c \
	$(EXECUTABLE_DIR)/loader.c \
	$(EXECUTABLE_DIR)/loader_object_table.c \
	ldscan.c ld.c lderr.c
COMPAT_SOURCES = \
	$(COMPAT_DIR)/cnv.c \
	$(COMPAT_DIR)/cnvlex.c \
	$(COMPAT_DIR)/db_admin.c \
	$(COMPAT_DIR)/db_class.c \
	$(COMPAT_DIR)/db_date.c \
	$(COMPAT_DIR)/db_info.c	\
	$(COMPAT_DIR)/db_macro.c \
	$(COMPAT_DIR)/db_obj.c \
	$(COMPAT_DIR)/db_old.c \
	$(COMPAT_DIR)/db_query.c \
	$(COMPAT_DIR)/db_set.c \
	$(COMPAT_DIR)/db_temp.c	\
	$(COMPAT_DIR)/db_vdb.c \
	$(COMPAT_DIR)/db_virt.c
BASE_SOURCES = \
	$(BASE_DIR)/porting.c \
	$(BASE_DIR)/memory_manager_1.c \
	$(BASE_DIR)/memory_manager_2.c \
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
	$(BASE_DIR)/dynamic_load.c \
	$(BASE_DIR)/adj_ar.c \
	$(BASE_DIR)/chartype.c \
	$(BASE_DIR)/co.c \
	$(BASE_DIR)/util_func.c \
	$(BASE_DIR)/intl.c \
	$(BASE_DIR)/environment_variable.c \
	$(BASE_DIR)/ustring.c \
	$(BASE_DIR)/vstr.c \
	$(BASE_DIR)/stringl.c \
	$(BASE_DIR)/getopt_long.c
HEAPLAYER_SOURCES = \
	$(HEAPLAYER_DIR)/customheaps.cpp
CONNECTION_SOURCES = \
	$(CONNECTION_DIR)/anchor.c \
	$(CONNECTION_DIR)/connless.c \
	$(CONNECTION_DIR)/general.c \
	$(CONNECTION_DIR)/globals.c \
	$(CONNECTION_DIR)/oob.c \
	$(CONNECTION_DIR)/queue.c \
	$(CONNECTION_DIR)/tcp.c \
	$(CONNECTION_DIR)/top.c \
	$(CONNECTION_DIR)/net.c
COMMUNICATION_SOURCES = \
	$(COMMUNICATION_DIR)/network_interface_sky.c \
	$(COMMUNICATION_DIR)/network_sky.c \
	$(COMMUNICATION_DIR)/network.c \
PARSER_SOURCES = \
	$(PARSER_DIR)/qp_mem.c \
	$(PARSER_DIR)/qp_util.c \
	$(PARSER_DIR)/double_byte_support.c \
	$(PARSER_DIR)/method_transform.c \
	$(PARSER_DIR)/cnf.c \
	$(PARSER_DIR)/compile.c \
	$(PARSER_DIR)/dbi.c \
	$(PARSER_DIR)/evaluate.c \
	$(PARSER_DIR)/keyword.c \
	$(PARSER_DIR)/parse_tree_1.c \
	$(PARSER_DIR)/parse_tree_3.c \
	$(PARSER_DIR)/query_result.c \
	$(PARSER_DIR)/name_resolution.c \
	$(PARSER_DIR)/semantic_check.c \
	$(PARSER_DIR)/type_checking.c \
	$(PARSER_DIR)/support_functinos.c \
	$(PARSER_DIR)/parse_tree.c \
	$(PARSER_DIR)/xasl_generation_1.c \
	$(PARSER_DIR)/xasl_generation_2.c \
	$(PARSER_DIR)/xasl_generation_3.c \
	sqerr.c \
	sqlx_grammar.c \
	sqscan.c \
	$(PARSER_DIR)/sqs.c \
	$(PARSER_DIR)/view_transform_1.c \
	$(PARSER_DIR)/view_transform_2.c
OPTIMIZER_SOURCES = \
	$(OPTIMIZER_DIR)/bitset.c \
	$(OPTIMIZER_DIR)/query_graph_1.c \
	$(OPTIMIZER_DIR)/query_graph_2.c \
	$(OPTIMIZER_DIR)/info.c \
	$(OPTIMIZER_DIR)/query_planner.c \
	$(OPTIMIZER_DIR)/query_planner_1.c \
	$(OPTIMIZER_DIR)/query_planner_2.c \
	$(OPTIMIZER_DIR)/plan_generation.c \
	$(OPTIMIZER_DIR)/query_rewrite.c
QUERY_SOURCES = \
	$(QUERY_DIR)/execute_schema_1.c \
	$(QUERY_DIR)/execute_schema_2.c \
	$(QUERY_DIR)/execute_schema_4.c	\
	$(QUERY_DIR)/execute_schema_6.c \
	$(QUERY_DIR)/execute_schema_8.c \
    $(QUERY_DIR)/execute_schema_9.c \
	$(QUERY_DIR)/execute_statement_2.c \
	$(QUERY_DIR)/execute_statement_3.c \
	$(QUERY_DIR)/execute_statement_4.c \
	$(QUERY_DIR)/execute_statement_5.c \
	$(QUERY_DIR)/execute_statement_7.c \
	$(QUERY_DIR)/execute_statement_8.c \
	$(QUERY_DIR)/execute_statement_9.c \
	$(QUERY_DIR)/execute_statement_10.c \
	$(QUERY_DIR)/execute_statement_11.c \
	$(QUERY_DIR)/execute_statement_12.c \
	$(QUERY_DIR)/execute_statement_13.c \
	$(QUERY_DIR)/execute_statement_14.c \
	$(QUERY_DIR)/execute_statement_15.c \
	$(QUERY_DIR)/arithmetic.c \
	$(QUERY_DIR)/cursor.c \
	$(QUERY_DIR)/query_dump.c \
	$(QUERY_DIR)/query_method.c \
	$(QUERY_DIR)/numeric.c \
	$(QUERY_DIR)/query.c \
	$(QUERY_DIR)/qp_regu.c \
	$(QUERY_DIR)/string.c \
	$(QUERY_DIR)/qp_xmcl.c \
	$(QUERY_DIR)/xvarcl.c
OBJECT_SOURCES = \
	$(OBJECT_DIR)/classdef.c \
	$(OBJECT_DIR)/glo_class.c \
	$(OBJECT_DIR)/elo_holder.c \
	$(OBJECT_DIR)/elo_holder2.c \
	$(OBJECT_DIR)/elo_recovery.c \
	$(OBJECT_DIR)/memory_manager_3.c \
	$(OBJECT_DIR)/memory_manager_4.c \
	$(OBJECT_DIR)/memory_manager_5.c \
	$(OBJECT_DIR)/authenticate.c \
	$(OBJECT_DIR)/elo_class.c \
	$(OBJECT_DIR)/fbo_class.c \
	$(OBJECT_DIR)/object_print_1.c \
	$(OBJECT_DIR)/object_accessor.c \
	$(OBJECT_DIR)/object_template.c \
	$(OBJECT_DIR)/set_object_1.c \
	$(OBJECT_DIR)/trigger_manager.c \
	$(OBJECT_DIR)/virtual_object_1.c \
	$(OBJECT_DIR)/class_object.c \
	$(OBJECT_DIR)/schema_manager_1.c \
	$(OBJECT_DIR)/schema_manager_2.c \
	$(OBJECT_DIR)/schema_manager_3.c \
	$(OBJECT_DIR)/schema_manager_4.c \
	$(OBJECT_DIR)/schema_template.c \
	$(OBJECT_DIR)/object_representation.c \
	$(OBJECT_DIR)/object_primitive.c \
	$(OBJECT_DIR)/primch.c \
	$(OBJECT_DIR)/quick_fit.c \
	$(OBJECT_DIR)/transform_sky.c \
	$(OBJECT_DIR)/transform.c \
	$(OBJECT_DIR)/object_domain.c \
	$(OBJECT_DIR)/wslist.c \
	$(OBJECT_DIR)/work_space.c
JSP_SOURCES = \
	$(JSP_DIR)/jsp_sky.c
TRANSACTION_SOURCES = \
	$(TRANSACTION_DIR)/log_compress.c \
	$(TRANSACTION_DIR)/boot_cl.c \
	$(TRANSACTION_DIR)/locator_bt.c	\
	$(TRANSACTION_DIR)/locator_cl.c \
	$(TRANSACTION_DIR)/lock_bt.c \
	$(TRANSACTION_DIR)/logcp.c \
	$(TRANSACTION_DIR)/recovery_cl.c \
	$(TRANSACTION_DIR)/transaction_cl.c
STORAGE_SOURCES = \
	$(STORAGE_DIR)/common.c \
	$(STORAGE_DIR)/oid.c \
	$(STORAGE_DIR)/statistics_cl.c \
	$(STORAGE_DIR)/value_convert.c

lib_LTLIBRARIES = libcubridcs.la
libcubridcs_la_SOURCES = \
	$(API_SOURCES) \
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
	$(STORAGE_SOURCES)
dist_libcubridcs_la_SOURCES = $(PARSER_DIR)/sqlx_grammar.g $(EXECUTABLE_DIR)/ld.g
libcubridcs_la_LDFLAGS = -version-number @LT_VERSION@
libcubridcs_la_LIBADD = $(LIBADD_DL)
libcubridcs_la_LINK = $(LINK)
