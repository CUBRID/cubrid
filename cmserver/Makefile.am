
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
DBMT_DIR = $(SRC)/cmserver

INCLUDES = @SRC_INC@
DEFS += @SYS_DEFS@ @VERSION_DEFS@ @COMMON_DEFS@
LIBS += @SYS_LIBS@

bin_PROGRAMS = cub_js cub_job cub_auto

cub_js_SOURCES = \
	$(DBMT_DIR)/cm_job_server.c \
	$(DBMT_DIR)/cm_text_encryption.c \
	$(DBMT_DIR)/cm_config.c \
	$(DBMT_DIR)/cm_server_util.c \
	$(DBMT_DIR)/cm_command_execute.c \
	$(DBMT_DIR)/cm_connect_info.c \
	$(DBMT_DIR)/cm_user.c

cub_js_LDFLAGS= @CMDEP_LIB@

cub_job_SOURCES = \
	$(DBMT_DIR)/cm_server_util.c \
	$(DBMT_DIR)/cm_job_slave.c \
	$(DBMT_DIR)/cm_text_encryption.c \
	$(DBMT_DIR)/cm_config.c \
	$(DBMT_DIR)/cm_job_task.c \
	$(DBMT_DIR)/cm_command_execute.c \
	$(DBMT_DIR)/cm_connect_info.c \
	$(DBMT_DIR)/cm_user.c \
	$(DBMT_DIR)/cm_diag_util.c

cub_job_CFLAGS = $(AM_CFLAGS) -DFSERVER_SLAVE
cub_job_LDFLAGS = @CS_LIB@ @CMSTAT_LIB@ @CMDEP_LIB@

cub_auto_SOURCES = \
	$(DBMT_DIR)/cm_auto_server.c \
	$(DBMT_DIR)/cm_server_util.c \
	$(DBMT_DIR)/cm_text_encryption.c \
	$(DBMT_DIR)/cm_server_stat.c \
	$(DBMT_DIR)/cm_autojob.c \
	$(DBMT_DIR)/cm_auto_task.c \
	$(DBMT_DIR)/cm_config.c \
	$(DBMT_DIR)/cm_command_execute.c \
	$(DBMT_DIR)/cm_connect_info.c \
	$(DBMT_DIR)/cm_user.c \
	$(DBMT_DIR)/cm_diag_util.c
cub_auto_LDFLAGS = @CS_LIB@ @CMSTAT_LIB@ @CMDEP_LIB@
