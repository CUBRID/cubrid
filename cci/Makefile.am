
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
CCI_DIR = $(SRC)/cci

AM_CPPFLAGS = @SRC_INC@ @SYS_DEFS@ @VERSION_DEFS@

include_HEADERS = \
	$(BROKER_DIR)/cas_error.h \
	$(CCI_DIR)/cas_cci.h

lib_LTLIBRARIES = libcascci.la
libcascci_la_SOURCES = \
	$(CCI_DIR)/cas_cci.c \
	$(CCI_DIR)/cci_util.c \
	$(CCI_DIR)/cci_query_execute.c \
	$(CCI_DIR)/cci_net_buf.c \
	$(CCI_DIR)/cci_network.c \
	$(CCI_DIR)/cci_handle_mng.c \
	$(CCI_DIR)/cci_t_set.c \
	$(CCI_DIR)/cci_t_lob.c \
	$(CCI_DIR)/cci_common.c
libcascci_la_LDFLAGS = -version-number @LT_VERSION@

VERSIONED_FILES = \
	$(CCI_DIR)/cas_cci.c \
	$(CCI_DIR)/cci_network.c
$(VERSIONED_FILES): $(top_srcdir)/BUILD_NUMBER
	touch $@
