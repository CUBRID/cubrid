
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
API_DIR = $(SRC)/api

INCLUDES = @SRC_INC@ @EXT_INC@
DEFS = @SYS_DEFS@ @VERSION_DEFS@

lib_LIBRARIES = libcas.a
libcas_a_SOURCES = \
	$(BROKER_DIR)/cas.c \
	$(BROKER_DIR)/net_cas.c \
	$(BROKER_DIR)/net_buf.c \
	$(BROKER_DIR)/func.c \
	$(BROKER_DIR)/exec_db.c \
	$(BROKER_DIR)/handle_mng.c \
	$(BROKER_DIR)/cas_util.c \
	$(BROKER_DIR)/str_like.c \
	$(BROKER_DIR)/cas_xa.c \
	$(BROKER_DIR)/schema_info.c \
	$(BROKER_DIR)/cas_log.c \
	$(BROKER_DIR)/as_util.c \
	$(BROKER_DIR)/sql_log2.c \
	$(BROKER_DIR)/file_name.c
libcas_a_CFLAGS = $(AM_CFLAGS) -DLIBCAS_FOR_JSP

lib_LTLIBRARIES = libcascci.la libbrokeradmin.la
libcascci_la_SOURCES = \
	$(CCI_DIR)/cas_cci.c \
	$(CCI_DIR)/cci_util.c \
	$(CCI_DIR)/cci_query_execute.c \
	$(CCI_DIR)/cci_net_buf.c \
	$(CCI_DIR)/cci_network.c \
	$(CCI_DIR)/cci_handle_mng.c \
	$(CCI_DIR)/cci_t_set.c \
	$(API_DIR)/api_util.c \
    $(API_DIR)/api_value_indexer.c \
    $(API_DIR)/api_handle.c \
    $(API_DIR)/cci_stub.c
	
libcascci_la_LDFLAGS = -version-number @LT_VERSION@

libbrokeradmin_la_SOURCES = \
	$(BROKER_DIR)/uc_admin.c \
	$(BROKER_DIR)/admin_pub.c \
	$(BROKER_DIR)/shm.c \
	$(BROKER_DIR)/error.c \
	$(BROKER_DIR)/util.c \
	$(BROKER_DIR)/br_config.c \
	$(BROKER_DIR)/max_heap.c \
	$(BROKER_DIR)/getsize.c \
	$(BROKER_DIR)/get_psinfo.c \
	$(BROKER_DIR)/file_name.c
libbrokeradmin_la_CFLAGS = $(AM_CFLAGS) -D_UC_ADMIN_SO_ -DDIAG_DEVEL
libbrokeradmin_la_LDFLAGS = -version-number @LT_VERSION@
