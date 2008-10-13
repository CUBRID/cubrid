
compatdir = @prefix@/compat

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
DEFS = @SYS_DEFS@ @VERSION_DEFS@ -DASYNC_MODE -D_XOPEN_SOURCE -D_BSD_SOURCE \
	-DDIAG_DEVEL -DCAS_BROKER
AM_LDFLAGS = -lcurses @CS_LIB@ @CCI_LIB@

bin_PROGRAMS = cub_broker cubrid_broker cub_cas \
	broker_monitor broker_changer \
	broker_log_converter broker_log_runner broker_log_top

compat_SCRIPTS = uc

cub_broker_SOURCES = \
	$(BROKER_DIR)/broker.c \
	$(BROKER_DIR)/shm.c \
	$(BROKER_DIR)/error.c \
	$(BROKER_DIR)/util.c \
	$(BROKER_DIR)/getsize.c \
	$(BROKER_DIR)/max_heap.c \
	$(BROKER_DIR)/uw_acl.c \
	$(BROKER_DIR)/send_fd.c \
	$(BROKER_DIR)/file_name.c

broker_monitor_SOURCES = \
	$(BROKER_DIR)/monitor.c \
	$(BROKER_DIR)/br_config.c \
	$(BROKER_DIR)/shm.c \
	$(BROKER_DIR)/error.c \
	$(BROKER_DIR)/util.c \
	$(BROKER_DIR)/getsize.c \
	$(BROKER_DIR)/max_heap.c \
	$(BROKER_DIR)/get_psinfo.c \
	$(BROKER_DIR)/file_name.c

broker_changer_SOURCES = \
	$(BROKER_DIR)/changer.c \
	$(BROKER_DIR)/admin_pub.c \
	$(BROKER_DIR)/shm.c \
	$(BROKER_DIR)/error.c \
	$(BROKER_DIR)/getsize.c \
	$(BROKER_DIR)/br_config.c \
	$(BROKER_DIR)/util.c \
	$(BROKER_DIR)/max_heap.c \
	$(BROKER_DIR)/file_name.c

cubrid_broker_SOURCES = \
	$(BROKER_DIR)/br_config.c \
	$(BROKER_DIR)/admin.c \
	$(BROKER_DIR)/shm.c \
	$(BROKER_DIR)/error.c \
	$(BROKER_DIR)/util.c \
	$(BROKER_DIR)/admin_pub.c \
	$(BROKER_DIR)/getsize.c \
	$(BROKER_DIR)/max_heap.c \
	$(BROKER_DIR)/file_name.c

cub_cas_SOURCES = \
	$(BROKER_DIR)/cas.c \
	$(BROKER_DIR)/net_cas.c \
	$(BROKER_DIR)/net_buf.c \
	$(BROKER_DIR)/func.c \
	$(BROKER_DIR)/exec_db.c \
	$(BROKER_DIR)/handle_mng.c \
	$(BROKER_DIR)/util.c \
	$(BROKER_DIR)/str_like.c \
	$(BROKER_DIR)/recv_fd.c \
	$(BROKER_DIR)/cas_xa.c \
	$(BROKER_DIR)/schema_info.c \
	$(BROKER_DIR)/cas_log.c \
	$(BROKER_DIR)/as_util.c \
	$(BROKER_DIR)/shm.c \
	$(BROKER_DIR)/getsize.c \
	$(BROKER_DIR)/sql_log2.c \
	$(BROKER_DIR)/file_name.c \
	$(BROKER_DIR)/error.c \
	$(BROKER_DIR)/setup.c

broker_log_converter_SOURCES = \
	$(BROKER_DIR)/log_converter.c \
	$(BROKER_DIR)/log_util.c \
	$(BROKER_DIR)/t_string.c

broker_log_runner_SOURCES = \
	$(BROKER_DIR)/cas_runner.c \
	$(BROKER_DIR)/log_util.c \
	$(BROKER_DIR)/t_string.c

broker_log_top_SOURCES = \
	$(BROKER_DIR)/log_top.c \
	$(BROKER_DIR)/query_info.c \
	$(BROKER_DIR)/log_time.c \
	$(BROKER_DIR)/sql_list.c \
	$(BROKER_DIR)/log_util.c \
	$(BROKER_DIR)/t_string.c \
	$(BROKER_DIR)/log_top_tran.c
