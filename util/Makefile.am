
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


compatdir = @CUBRID_COMPATDIR@

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

FLEX = flex

EXTRA_DIST = start_cubrid stop_cubrid

AM_CPPFLAGS = @SRC_INC@ @EXT_INC@ \
	@SYS_DEFS@ @COMMON_DEFS@ @VERSION_DEFS@ \
	-DPRODUCE_ANSI_CODE -DUCI_TEMPORARY
#AM_LDADD = @SYS_LIBS@
LIBS += @SYS_LIBS@


$(EXECUTABLE_DIR)/esql_grammar.c : $(EXECUTABLE_DIR)/esql_grammar.y
	$(BISON) --name-prefix=esql_yy -d -r all -o $@ $< 

$(EXECUTABLE_DIR)/esql_lexer.c : $(EXECUTABLE_DIR)/esql_lexer.l
	$(FLEX) --prefix=esql_yy -o $@ $<

$(EXECUTABLE_DIR)/loader_old_grammar.c : $(EXECUTABLE_DIR)/loader_old_grammar.y
	$(BISON) --name-prefix=loader_yy -d -r all -o $@ $<

$(EXECUTABLE_DIR)/loader_old_lexer.c : $(EXECUTABLE_DIR)/loader_old_lexer.l
	$(FLEX) --prefix=loader_yy -o $@ $<


include_HEADERS = $(EXECUTABLE_DIR)/cubrid_esql.h

bin_PROGRAMS = \
	csql cub_master cub_server cubrid cub_admin cub_commdb \
	cubrid_esql cubrid_rel loadjava migrate_90beta_to_91

compat_PROGRAMS = \
	addvoldb backupdb checkdb commdb compactdb copydb \
	createdb deletedb installdb killtran load_noopt loaddb lockdb \
	optimizedb renamedb restoredb spacedb sqlx unloaddb get_disk_compat \
	convert_password

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
get_disk_compat_LDADD = @SA_LIB@

convert_password_SOURCES = $(EXECUTABLE_DIR)/convert_password.c
convert_password_LDADD = @SA_LIB@

migrate_90beta_to_91_SOURCES = $(EXECUTABLE_DIR)/migrate.c
migrate_90beta_to_91_LDADD = @SA_LIB@

load_noopt_SOURCES = $(EXECUTABLE_DIR)/loader_old_grammar.c \
		$(EXECUTABLE_DIR)/loader_old_lexer.c \
		$(EXECUTABLE_DIR)/loader_old.c \
		$(EXECUTABLE_DIR)/loader_object_table.c \
		$(EXECUTABLE_DIR)/loader_action.c \
		$(EXECUTABLE_DIR)/loader_disk.c \
		$(EXECUTABLE_DIR)/load_object.c \
		$(EXECUTABLE_DIR)/loaddb.c \
		$(EXECUTABLE_DIR)/util_support.c \
		$(EXECUTABLE_DIR)/loader_noopt.c
load_noopt_CFLAGS = $(AM_CFLAGS) -DLDR_OLD_LOADDB
load_noopt_LDADD = @SA_LIB@

loaddb_SOURCES = $(EXECUTABLE_DIR)/util_front.c

lockdb_SOURCES = $(EXECUTABLE_DIR)/util_front.c

optimizedb_SOURCES = $(EXECUTABLE_DIR)/util_front.c

renamedb_SOURCES = $(EXECUTABLE_DIR)/util_front.c

restoredb_SOURCES = $(EXECUTABLE_DIR)/util_front.c

spacedb_SOURCES = $(EXECUTABLE_DIR)/util_front.c

sqlx_SOURCES = $(EXECUTABLE_DIR)/util_front.c

unloaddb_SOURCES = $(EXECUTABLE_DIR)/util_front.c

compat_SCRIPTS = start_cubrid stop_cubrid

install-exec-hook:
	test -z "$(DESTDIR)$(compatdir)" || mkdir -p "$(DESTDIR)$(compatdir)"
	cd $(DESTDIR)$(compatdir) && $(LN_S) -f start_cubrid start_server
	cd $(DESTDIR)$(compatdir) && $(LN_S) -f stop_cubrid stop_server

lib_LTLIBRARIES = libcubridesql.la
libcubridesql_la_SOURCES = $(EXECUTABLE_DIR)/esql_gadget.c $(EXECUTABLE_DIR)/esql_cli.c
libcubridesql_la_CFLAGS = $(AM_CFLAGS) -DPRODUCE_ANSI_CODE -DUCI_TEMPORARY 
libcubridesql_la_LDFLAGS = -version-number @LT_VERSION@

cub_server_SOURCES = $(EXECUTABLE_DIR)/server.c
cub_server_LDADD = @CUBRID_LIB@

cub_commdb_SOURCES = \
	$(EXECUTABLE_DIR)/commdb.c \
	$(EXECUTABLE_DIR)/util_support.c \
	$(EXECUTABLE_DIR)/master_util.c
cub_commdb_LDADD = @CS_LIB@

cub_master_SOURCES = \
	$(EXECUTABLE_DIR)/master.c \
	$(EXECUTABLE_DIR)/master_request.c \
	$(EXECUTABLE_DIR)/master_util.c \
	$(EXECUTABLE_DIR)/master_heartbeat.c
cub_master_LDADD = @CS_LIB@

cubrid_rel_SOURCES = \
	$(EXECUTABLE_DIR)/cubrid_version.c
cubrid_rel_LDADD = @SA_LIB@

loadjava_SOURCES = \
	$(EXECUTABLE_DIR)/loadjava.c
loadjava_LDADD = @SA_LIB@

cubrid_SOURCES = \
	$(EXECUTABLE_DIR)/util_service.c \
	$(EXECUTABLE_DIR)/util_support.c
cubrid_LDADD = @SA_LIB@

cub_admin_SOURCES = \
	$(EXECUTABLE_DIR)/util_admin.c \
	$(EXECUTABLE_DIR)/util_support.c \
	$(BASE_DIR)/getopt_long.c


csql_SOURCES = $(EXECUTABLE_DIR)/csql_launcher.c \
			   $(EXECUTABLE_DIR)/util_support.c \
			   $(BASE_DIR)/porting.c \
			   $(BASE_DIR)/getopt_long.c

cubrid_esql_SOURCES = \
	$(EXECUTABLE_DIR)/esql_symbol_table.c \
	$(EXECUTABLE_DIR)/esql_declare.c \
	$(EXECUTABLE_DIR)/esql_cursor.c	\
	$(EXECUTABLE_DIR)/esql_misc.c \
	$(EXECUTABLE_DIR)/esql_whenever.c \
	$(EXECUTABLE_DIR)/esql_hash.c \
	$(EXECUTABLE_DIR)/esql_translate.c \
	$(EXECUTABLE_DIR)/esql_host_variable.c \
	$(EXECUTABLE_DIR)/esql_grammar.c \
	$(EXECUTABLE_DIR)/esql_lexer.c 
cubrid_esql_CFLAGS = $(AM_CFLAGS) -DPRODUCE_ANSI_CODE -DUCI_TEMPORARY 
cubrid_esql_LDADD = @CS_LIB@

noinst_PROGRAMS = gencat
gencat_SOURCES = \
	$(BASE_DIR)/porting.c \
	$(EXECUTABLE_DIR)/gencat.c
