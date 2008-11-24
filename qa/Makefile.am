
#
# Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
#
#   This program is free software; you can redistribute it and/or modify 
#   it under the terms of the GNU General Public License as published by 
#   the Free Software Foundation; version 2 of the License. 
#
#  This program is distributed in the hope that it will be useful, 
#  but WITHOUT ANY WARRANTY; without even the implied warranty of 
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the 
#  GNU General Public License for more details. 
#
#  You should have received a copy of the GNU General Public License 
#  along with this program; if not, write to the Free Software 
#  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA 
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

CESQL = $(top_builddir)/util/cubrid_esql

INCLUDES = @SRC_INC@ @EXT_INC@

DEFS = @SYS_DEFS@ @SA_DEFS@ @COMMON_DEFS@ @VERSION_DEFS@

AM_CFLAGS = -fPIC -DPIC

qatoolsdir = ${prefix}/qatools
qatools_PROGRAMS = populate methods.so nrecurse.so recurse.so

populate_SOURCES = $(QATOOLS_DIR)/comp.c $(QATOOLS_DIR)/populate.c
populate_LDFLAGS = @SA_LIB@ @CAS_LIB@

BUILT_SOURCES = methods.so nrecurse.so recurse.so
DISTCLEANFILES = methods.so nrecurse.so recurse.so nrecurse.c recurse.c

methods_so_SOURCES = $(QATOOLS_DIR)/methods.c
methods_so_LDFLAGS = -shared

nrecurse.so: nrecurse.o
	$(CCLD) -shared $(CFLAGS) -L$(top_builddir)/util/.libs -lcubridesql $< -o $@

nrecurse.o: nrecurse.c
	$(COMPILE) $(AM_CFLAGS) -fPIC -DPIC -c $< -o $@

nrecurse.c: $(QATOOLS_DIR)/nrecurse.ec
	CUBRID=$(top_builddir) $(CESQL) $<

recurse.so: recurse.o
	$(CCLD) -shared $(CFLAGS) -L$(top_builddir)/util/.libs -lcubridesql $< -o $@

recurse.o: recurse.c
	$(COMPILE) $(AM_CFLAGS) -fPIC -DPIC -c $< -o $@

recurse.c: $(QATOOLS_DIR)/recurse.ec
	CUBRID=$(top_builddir) $(CESQL) $<
