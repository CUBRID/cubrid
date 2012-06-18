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
DBGW_DIR = $(SRC)/dbgw
CCI_DIR = $(SRC)/cci
BROKER_DIR = $(SRC)/broker

AM_CPPFLAGS = @EXT_INC@ @DBGW_DEFS@ @VERSION_DEFS@

# DBGW Season 3 Library
dbgw3_includedir = $(includedir)
dbgw3_include_HEADERS = \
		$(CCI_DIR)/cci_log.h \
        $(DBGW_DIR)/DBGWMock.h \
        $(DBGW_DIR)/DBGWCommon.h \
        $(DBGW_DIR)/DBGWError.h \
        $(DBGW_DIR)/DBGWValue.h \
        $(DBGW_DIR)/DBGWDataBaseInterface.h \
        $(DBGW_DIR)/DBGWCUBRIDInterface.h \
        $(DBGW_DIR)/DBGWXMLParser.h \
        $(DBGW_DIR)/DBGWQuery.h \
        $(DBGW_DIR)/DBGWLogger.h \
        $(DBGW_DIR)/DBGWConfiguration.h \
        $(DBGW_DIR)/DBGWClient.h      
dbgw3_SOURCES = \
        $(DBGW_DIR)/DBGWMock.cpp \
        $(DBGW_DIR)/DBGWCommon.cpp \
        $(DBGW_DIR)/DBGWError.cpp \
        $(DBGW_DIR)/DBGWValue.cpp \
        $(DBGW_DIR)/DBGWDataBaseInterface.cpp \
        $(DBGW_DIR)/DBGWCUBRIDInterface.cpp \
        $(DBGW_DIR)/DBGWXMLParser.cpp \
        $(DBGW_DIR)/DBGWQuery.cpp \
        $(DBGW_DIR)/DBGWLogger.cpp \
        $(DBGW_DIR)/DBGWConfiguration.cpp \
        $(DBGW_DIR)/DBGWClient.cpp
lib_LTLIBRARIES = libdbgw3.la
libdbgw3_la_SOURCES = \
        $(dbgw3_SOURCES)
libdbgw3_la_CPPFLAGS = \
        $(AM_CPPFLAGS) -I$(CCI_DIR) -I$(BROKER_DIR)
libdbgw3_la_LDFLAGS = -version-number @LT_VERSION@		
libdbgw3_la_LIBADD = @EXPAT_LIBS@
libdbgw3_la_LINK = $(CXXLINK)

# DBGW Season 3 Query Tester
bin_PROGRAMS = \
		dbgw3_query_tester
dbgw3_query_tester_includedir = $(includedir)
dbgw3_query_tester_include_HEADERS = \
		$(DBGW_DIR)/DBGWQueryTester.h	
dbgw3_query_tester_SOURCES = \
        $(DBGW_DIR)/DBGWMock.cpp \
        $(DBGW_DIR)/DBGWCommon.cpp \
        $(DBGW_DIR)/DBGWError.cpp \
        $(DBGW_DIR)/DBGWValue.cpp \
        $(DBGW_DIR)/DBGWDataBaseInterface.cpp \
        $(DBGW_DIR)/DBGWCUBRIDInterface.cpp \
        $(DBGW_DIR)/DBGWXMLParser.cpp \
        $(DBGW_DIR)/DBGWQuery.cpp \
        $(DBGW_DIR)/DBGWLogger.cpp \
        $(DBGW_DIR)/DBGWConfiguration.cpp \
        $(DBGW_DIR)/DBGWClient.cpp \
		$(DBGW_DIR)/DBGWQueryTester.cpp		
dbgw3_query_tester_CPPFLAGS = \
		$(AM_CPPFLAGS) -I$(CCI_DIR) -I$(BROKER_DIR)		
dbgw3_query_tester_LDADD = @EXPAT_LIBS@ @CCI_LIB@

VERSIONED_FILES = \
        $(BASE_DIR)/release_string.c \
        $(DBGW_DIR)/DBGWClient.cpp \
        $(DBGW_DIR)/DBGWCUBRIDInterface.cpp \
        $(DBGW_DIR)/DBGWConfiguration.cpp
$(VERSIONED_FILES): $(top_srcdir)/BUILD_NUMBER
		touch $@
