
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
BROKER_DIR = $(SRC)/broker

AM_CPPFLAGS = @SRC_INC@ @EXT_INC@ \
	@SYS_DEFS@ @VERSION_DEFS@ @DIR_DEFS@

noinst_LTLIBRARIES = libcas.la
libcas_la_SOURCES = \
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
	$(BROKER_DIR)/broker_util.c \
	$(BROKER_DIR)/cas_error.c 
libcas_la_CFLAGS = $(AM_CFLAGS) -fPIC
libcas_la_CPPFLAGS = $(AM_CPPFLAGS) -DLIBCAS_FOR_JSP
libcas_la_LDFLAGS = -static

VERSIONED_FILES = \
	$(BROKER_DIR)/cas.c

$(VERSIONED_FILES): $(top_srcdir)/BUILD_NUMBER
	touch $@
