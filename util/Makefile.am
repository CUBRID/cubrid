
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

ANTLR = $(top_builddir)/pccts/antlrd/antlr
DLG = $(top_builddir)/pccts/dlg/dlg

INCLUDES = @SRC_INC@ -I$(top_srcdir)/pccts/h @EXT_INC@
DEFS += @SYS_DEFS@ @COMMON_DEFS@ @VERSION_DEFS@ -DZZCOL -DLL_K=1 -DYY_CHAR=char -DPRODUCE_ANSI_CODE -DUCI_TEMPORARY
LIBS += @SYS_LIBS@

BUILT_SOURCES = es.c eserr.c es.dlg estokens.h esmode.h esscan.c \
		lo.c loerr.c lo.dlg lotokens.h lomode.h loscan.c
CLEANFILES = es.c eserr.c es.dlg estokens.h esmode.h esscan.c \
		lo.c loerr.c lo.dlg lotokens.h lomode.h loscan.c

lo.c loerr.c lo.dlg: $(EXECUTABLE_DIR)/lo.g
	$(ANTLR) -ga -k 1 -fe loerr.c -fl lo.dlg -ft lotokens.h -fm lomode.h $<
loscan.c lomode.h: lo.dlg
	$(DLG) -C2 -ga -m lomode.h $< loscan.c

es.c eserr.c es.dlg: $(EXECUTABLE_DIR)/es.g
	$(ANTLR) -ga -k 1 -fe eserr.c -fl es.dlg -ft estokens.h -fm esmode.h $<
esscan.c esmode.h: es.dlg
	$(DLG) -C2 -ga -m esmode.h $< esscan.c

bin_PROGRAMS = \
	csql cub_master cub_server cubrid cub_admin cub_commdb \
	cubrid_esql cubrid_rel loadjava repl_agent repl_server

compat_PROGRAMS = \
	addvoldb backupdb checkdb commdb compactdb copydb \
	createdb deletedb installdb killtran load_noopt loaddb lockdb \
	optimizedb renamedb restoredb spacedb sqlx unloaddb get_disk_compat convert_password

addvoldb_SOURCES = $(EXECUTABLE_DIR)/util_front.c

backupdb_SOURCES = $(EXECUTABLE_DIR)/util_front.c

checkdb_SOURCES = $(EXECUTABLE_DIR)/util_front.c

commdb_SOURCES = $(EXECUTABLE_DIR)/util_front.c

compactdb_SOURCES = $(EXECUTABLE_DIR)/util_front.c

copydb_SOURCES = $(EXECUTABLE_DIR)/util_front.c

createdb_SOURCES = $(EXECUTABLE_DIR)/util_front.c

deletedb_SOURCES = $(EXECUTABLE_DIR)/util_front.c

installdb_SOURCES = $(EXECUTABLE_DIR)/util_front.c

killtran_SOURCES = $(EXECUTABLE_DIR)/util_front.c
get_disk_compat_SOURCES = $(EXECUTABLE_DIR)/get_disk_compat.c
get_disk_compat_LDFLAGS = @SA_LIB@
convert_password_SOURCES = $(EXECUTABLE_DIR)/convert_password.c
convert_password_LDFLAGS = @SA_LIB@

load_noopt_SOURCES = lo.c loerr.c loscan.c \
		$(EXECUTABLE_DIR)/ldr_o.c \
		$(EXECUTABLE_DIR)/loader_object_table.c \
		$(EXECUTABLE_DIR)/ldr_act.c \
		$(EXECUTABLE_DIR)/loader_disk.c \
		$(EXECUTABLE_DIR)/load_object.c \
		$(EXECUTABLE_DIR)/loaddb.c \
		$(EXECUTABLE_DIR)/dbu_misc.c \
		$(EXECUTABLE_DIR)/migdb.c
dist_load_noopt_SOURCES=$(EXECUTABLE_DIR)/lo.g
load_noopt_CFLAGS = $(AM_CFLAGS) -DLDR_OLD_LOADDB
load_noopt_LDFLAGS = @SA_LIB@

loaddb_SOURCES = $(EXECUTABLE_DIR)/util_front.c

lockdb_SOURCES = $(EXECUTABLE_DIR)/util_front.c

optimizedb_SOURCES = $(EXECUTABLE_DIR)/util_front.c

renamedb_SOURCES = $(EXECUTABLE_DIR)/util_front.c

restoredb_SOURCES = $(EXECUTABLE_DIR)/util_front.c

spacedb_SOURCES = $(EXECUTABLE_DIR)/util_front.c

sqlx_SOURCES = $(EXECUTABLE_DIR)/util_front.c

unloaddb_SOURCES = $(EXECUTABLE_DIR)/util_front.c

bin_SCRIPTS = \
	repl_make_distdb repl_make_slavedb repl_make_group \
	repl_make_snapshot repl_check_sync repl_safe_page \
	repl_change_param repl_change_master

compat_SCRIPTS = start_cubrid stop_cubrid

install-exec-hook:
	test -z "$(compatdir)" || mkdir -p "$(compatdir)"
	cd $(compatdir) && $(LN_S) -f ../bin/cub_server$(EXEEXT) server$(EXEEXT)
	cd $(compatdir) && $(LN_S) -f start_cubrid start_server
	cd $(compatdir) && $(LN_S) -f stop_cubrid stop_server

lib_LTLIBRARIES = libcubridesql.la
libcubridesql_la_SOURCES = $(EXECUTABLE_DIR)/esql_gadget.c $(EXECUTABLE_DIR)/unici.c
libcubridesql_la_CFLAGS = $(AM_CFLAGS) -DPRODUCE_ANSI_CODE -DUCI_TEMPORARY -DYY_CHAR=char
libcubridesql_la_LDFLAGS = -version-number @LT_VERSION@

cub_server_SOURCES = $(EXECUTABLE_DIR)/server.c
cub_server_LDFLAGS = @CUBRID_LIB@

cub_commdb_SOURCES = \
	$(EXECUTABLE_DIR)/commdb.c \
	$(EXECUTABLE_DIR)/dbu_misc.c \
	$(EXECUTABLE_DIR)/master_util.c
cub_commdb_LDFLAGS = @CS_LIB@

cub_master_SOURCES = \
	$(EXECUTABLE_DIR)/master.c \
	$(EXECUTABLE_DIR)/master_request.c \
	$(EXECUTABLE_DIR)/daemon.c \
	$(EXECUTABLE_DIR)/master_util.c
cub_master_LDFLAGS = @CS_LIB@

repl_server_SOURCES = \
	$(EXECUTABLE_DIR)/repl_server.c \
	$(EXECUTABLE_DIR)/repl_svr_sock.c \
	$(EXECUTABLE_DIR)/repl_svr_tp.c \
	$(EXECUTABLE_DIR)/repl_comm.c
repl_server_LDFLAGS = @SA_LIB@

repl_agent_SOURCES = \
	$(EXECUTABLE_DIR)/repl_agent.c \
	$(EXECUTABLE_DIR)/repl_ag_sock.c \
	$(EXECUTABLE_DIR)/repl_ag_tp.c \
	$(EXECUTABLE_DIR)/repl_comm.c
repl_agent_LDFLAGS = @CS_LIB@

cubrid_rel_SOURCES = \
	$(EXECUTABLE_DIR)/cosmos_version.c
cubrid_rel_LDFLAGS = @SA_LIB@

loadjava_SOURCES = \
	$(EXECUTABLE_DIR)/loadjava.c
loadjava_LDFLAGS = @SA_LIB@

cubrid_SOURCES = \
	$(EXECUTABLE_DIR)/util_service.c \
	$(EXECUTABLE_DIR)/dbu_misc.c
cubrid_LDFLAGS = @SA_LIB@

cub_admin_SOURCES = \
	$(EXECUTABLE_DIR)/util_admin.c \
	$(EXECUTABLE_DIR)/dbu_misc.c

csql_SOURCES = $(EXECUTABLE_DIR)/sqlx_launcher.c $(EXECUTABLE_DIR)/dbu_misc.c

cubrid_esql_SOURCES = \
	$(EXECUTABLE_DIR)/esql_symbol_table.c \
	$(EXECUTABLE_DIR)/esql_declare.c \
	$(EXECUTABLE_DIR)/esql_cursor.c	\
	$(EXECUTABLE_DIR)/esql_misc.c \
	$(EXECUTABLE_DIR)/whenever.c \
	$(EXECUTABLE_DIR)/esql_hash.c \
	$(EXECUTABLE_DIR)/esql_translate.c \
	$(EXECUTABLE_DIR)/esql_host_variable.c \
	es.c esscan.c eserr.c
dist_cubrid_esql_SOURCES = $(EXECUTABLE_DIR)/es.g
cubrid_esql_CFLAGS = $(AM_CFLAGS) -DPRODUCE_ANSI_CODE -DUCI_TEMPORARY -DYY_CHAR=char
cubrid_esql_LDFLAGS = @CS_LIB@

noinst_PROGRAMS = gencat
gencat_SOURCES = $(EXECUTABLE_DIR)/gencat.c
