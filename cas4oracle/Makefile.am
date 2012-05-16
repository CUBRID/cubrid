
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

if BUILD_CAS_FOR_ORACLE
  CAS4O = cub_cas_oracle
else
  CAS4O =
endif

bin_PROGRAMS = $(CAS4O)

cub_cas_oracle_SOURCES = \
	$(BROKER_DIR)/cas.c \
	$(BROKER_DIR)/cas_network.c \
	$(BROKER_DIR)/cas_net_buf.c \
	$(BROKER_DIR)/cas_function.c \
	$(BROKER_DIR)/cas_oracle_execute.c \
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
	$(BROKER_DIR)/cas_util.c \
	$(BROKER_DIR)/cas_dbms_util.c \
	$(BROKER_DIR)/cas_error.c \
	$(BROKER_DIR)/cas_error_log.c \
	$(BASE_DIR)/environment_variable.c \
	$(BASE_DIR)/stringl.c
cub_cas_oracle_CPPFLAGS = @SRC_INC@ @EXT_INC@ @ORACLE_INC_PATH@\
	@SYS_DEFS@ @VERSION_DEFS@ -DASYNC_MODE -D_XOPEN_SOURCE -D_BSD_SOURCE \
	-DDIAG_DEVEL -DCAS_BROKER @DIR_DEFS@ -DCAS_FOR_ORACLE
cub_cas_oracle_LDFLAGS = @ORACLE_LIB_PATH@ -lclntsh -lnnz11 -lrt

VERSIONED_FILES = \
	$(BROKER_DIR)/cas_oracle_execute.c

$(VERSIONED_FILES): $(top_srcdir)/BUILD_NUMBER
	touch $@
