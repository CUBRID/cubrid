
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
DBMT_DIR = $(SRC)/cmserver
QATOOLS_DIR = $(SRC)/qatools

INCLUDES = @SRC_INC@
DEFS += @SYS_DEFS@ @VERSION_DEFS@ @COMMON_DEFS@ -DDIAG_DEVEL -DSPECIAL_BUILD_STRING=\"\"
LIBS += @SYS_LIBS@

bin_PROGRAMS = cub_js cub_job cub_jobsa cub_auto cub_sainfo

cub_js_SOURCES = \
	$(DBMT_DIR)/cm_job_server.c \
	$(DBMT_DIR)/cm_dstring.c \
	$(DBMT_DIR)/cm_text_encryption.c \
	$(DBMT_DIR)/cm_nameval.c \
	$(DBMT_DIR)/cm_config.c \
	$(DBMT_DIR)/cm_server_util.c \
	$(DBMT_DIR)/cm_command_execute.c \
	$(DBMT_DIR)/cm_user.c

cub_job_SOURCES = \
	$(DBMT_DIR)/cm_server_util.c \
	$(DBMT_DIR)/cm_job_slave.c \
	$(DBMT_DIR)/cm_nameval.c \
	$(DBMT_DIR)/cm_text_encryption.c \
	$(DBMT_DIR)/cm_dstring.c \
	$(DBMT_DIR)/cm_config.c \
	$(DBMT_DIR)/cm_job_task.c \
	$(DBMT_DIR)/cm_command_execute.c \
	$(DBMT_DIR)/cm_user.c \
	$(DBMT_DIR)/cm_broker_admin.c \
	$(DBMT_DIR)/cm_diag_util.c
cub_job_CFLAGS = $(AM_CFLAGS) -DFSERVER_SLAVE
cub_job_LDFLAGS = @CS_LIB@

cub_auto_SOURCES = \
	$(DBMT_DIR)/cm_auto_server.c \
	$(DBMT_DIR)/cm_server_util.c \
	$(DBMT_DIR)/cm_dstring.c \
	$(DBMT_DIR)/cm_nameval.c \
	$(DBMT_DIR)/cm_text_encryption.c \
	$(DBMT_DIR)/cm_server_stat.c \
	$(DBMT_DIR)/cm_autojob.c \
	$(DBMT_DIR)/cm_auto_task.c \
	$(DBMT_DIR)/cm_config.c \
	$(DBMT_DIR)/cm_command_execute.c \
	$(DBMT_DIR)/cm_user.c \
	$(DBMT_DIR)/cm_broker_admin.c \
	$(DBMT_DIR)/cm_diag_util.c
cub_auto_LDFLAGS = @CS_LIB@

cub_jobsa_SOURCES = $(DBMT_DIR)/cm_class_info_sa.c
cub_jobsa_LDFLAGS = @SA_LIB@

cub_sainfo_SOURCES = $(DBMT_DIR)/cm_trigger_info_sa.c
cub_sainfo_LDFLAGS = @SA_LIB@
