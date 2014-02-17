
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
BASE_DIR = $(SRC)/base
TOOLS_DIR = $(SRC)/tools

AM_CPPFLAGS = @SRC_INC@ @EXT_INC@ @SYS_DEFS@ @VERSION_DEFS@
CCI_APPLIER_LIBS = @CCI_LIB@

bin_PROGRAMS = cci_applier

cci_applier_SOURCES = \
	$(TOOLS_DIR)/cci_applier.c
cci_applier_LDADD = $(CCI_APPLIER_LIBS) -lnsl

VERSIONED_FILES = \
	$(TOOLS_DIR)/cci_applier.c

$(VERSIONED_FILES): $(top_srcdir)/BUILD_NUMBER
	touch $@	      

