
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
QATOOLS_DIR = $(SRC)/qatools
API_DIR = $(SRC)/api

INCLUDES = @SRC_INC@ @EXT_INC@
#DEFS = @SYS_DEFS@ @VERSION_DEFS@ @DIR_DEFS@ -DCCI_DEBUG -DCAS_FOR_DBMS
DEFS = @SYS_DEFS@ @VERSION_DEFS@ @DIR_DEFS@ -DCCI_DEBUG

lib_LIBRARIES = libcas.a
libcas_a_SOURCES = \
	$(BROKER_DIR)/cas.c \
	$(BROKER_DIR)/cas_network.c \
	$(BROKER_DIR)/cas_net_buf.c \
	$(BROKER_DIR)/cas_function.c \
	$(BROKER_DIR)/cas_execute.c \
	$(BROKER_DIR)/cas_handle.c \
	$(BROKER_DIR)/cas_util.c \
	$(BROKER_DIR)/cas_str_like.c \
	$(BROKER_DIR)/cas_xa.c \
	$(BROKER_DIR)/cas_schema_info.c \
	$(BROKER_DIR)/cas_log.c \
	$(BROKER_DIR)/cas_sql_log2.c \
	$(BROKER_DIR)/broker_filename.c \
	$(BROKER_DIR)/broker_util.c
libcas_a_CFLAGS = $(AM_CFLAGS) -DLIBCAS_FOR_JSP -fPIC

lib_LTLIBRARIES = libcascci.la
libcascci_la_SOURCES = \
	$(CCI_DIR)/cas_cci.c \
	$(CCI_DIR)/cci_util.c \
	$(CCI_DIR)/cci_query_execute.c \
	$(CCI_DIR)/cci_net_buf.c \
	$(CCI_DIR)/cci_network.c \
	$(CCI_DIR)/cci_handle_mng.c \
	$(CCI_DIR)/cci_t_set.c
#	$(API_DIR)/api_util.c \
#	$(API_DIR)/api_value_indexer.c \
#	$(API_DIR)/api_handle.c \
#	$(API_DIR)/cci_stub.c
	
libcascci_la_LDFLAGS = -version-number @LT_VERSION@

