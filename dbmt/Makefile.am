
SUBDIRS = conf

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

INCLUDES = @SRC_INC@
DEFS += @SYS_DEFS@ @VERSION_DEFS@ @COMMON_DEFS@ -DDIAG_DEVEL -DSPECIAL_BUILD_STRING=\"\"
LIBS += @SYS_LIBS@

bin_PROGRAMS = cub_js cub_job cub_jobsa cub_auto cub_sainfo

cub_js_SOURCES = \
	$(DBMT_DIR)/fserver.c \
	$(DBMT_DIR)/dstring.c \
	$(DBMT_DIR)/tea.c \
	$(DBMT_DIR)/nameval.c \
	$(DBMT_DIR)/dbmt_config.c \
	$(DBMT_DIR)/server_util.c \
	$(DBMT_DIR)/cmd_exec.c \
	$(DBMT_DIR)/dbmt_user.c

cub_job_SOURCES = \
	$(DBMT_DIR)/server_util.c \
	$(DBMT_DIR)/fserver_slave.c \
	$(DBMT_DIR)/nameval.c \
	$(DBMT_DIR)/tea.c \
	$(DBMT_DIR)/dstring.c \
	$(DBMT_DIR)/dbmt_config.c \
	$(DBMT_DIR)/fserver_task.c \
	$(DBMT_DIR)/cmd_exec.c \
	$(DBMT_DIR)/dbmt_user.c \
	$(DBMT_DIR)/unicas_admin.c \
	$(DBMT_DIR)/diag_util.c
cub_job_CFLAGS = $(AM_CFLAGS) -DFSERVER_SLAVE
cub_job_LDFLAGS = @CS_LIB@

cub_auto_SOURCES = \
	$(DBMT_DIR)/pserver.c \
	$(DBMT_DIR)/server_util.c \
	$(DBMT_DIR)/dstring.c \
	$(DBMT_DIR)/nameval.c \
	$(DBMT_DIR)/tea.c \
	$(DBMT_DIR)/server_stat.c \
	$(DBMT_DIR)/autojob.c \
	$(DBMT_DIR)/pserver_task.c \
	$(DBMT_DIR)/dbmt_config.c \
	$(DBMT_DIR)/cmd_exec.c \
	$(DBMT_DIR)/dbmt_user.c \
	$(DBMT_DIR)/unicas_admin.c \
	$(DBMT_DIR)/diag_util.c
cub_auto_LDFLAGS = @CS_LIB@

cub_jobsa_SOURCES = $(DBMT_DIR)/class_info_sa.c
cub_jobsa_LDFLAGS = @SA_LIB@

cub_sainfo_SOURCES = $(DBMT_DIR)/trigger_info_sa.c
cub_sainfo_LDFLAGS = @SA_LIB@
