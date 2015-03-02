
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
COMPAT_DIR = $(SRC)/compat
CCI_DIR = $(SRC)/cci
API_DIR = $(SRC)/api

AM_CPPFLAGS = -I. -I$(srcdir) -I$(top_builddir) -I@SRC_INC@ @EXT_INC@ @JAVA_INC@ \
	@SYS_DEFS@ @SA_DEFS@ @COMMON_DEFS@ @VERSION_DEFS@

noinst_HEADERS = system.h

.PHONY: er_code_check stmt_type_check isolation_type_check

BUILT_SOURCES = er_in_error_code_h.out er_in_dbi_compat_h.out er_code_check
er_in_error_code_h.out: $(BASE_DIR)/error_code.h
	@echo "Extracting error codes from $<"
	@$(CPP) $(AM_CPPFLAGS) -dD $< | $(SED) -n 's/#define \(ER_.*\)/\1/p' | sort -rnk2 > $@
er_in_dbi_compat_h.out: $(COMPAT_DIR)/dbi_compat.h
	@echo "Extracting error codes from $<"
	@$(CPP) $(AM_CPPFLAGS) -dD $< | $(SED) -n 's/#define \(ER_.*\)/\1/p' | sort -rnk2 > $@
er_code_check: er_in_error_code_h.out er_in_dbi_compat_h.out
	@echo "Checking error codes in $(BASE_DIR)/error_code.h and $(COMPAT_DIR)/dbi_compat.h"
	$(DIFF) -U0 er_in_error_code_h.out er_in_dbi_compat_h.out || (echo "Please check error codes!"; exit 1)

BUILT_SOURCES += stmt_types_in_cas_cci_h.out stmt_types_in_dbi_compat_h.out stmt_type_check
stmt_types_in_cas_cci_h.out: $(CCI_DIR)/cas_cci.h
	@echo "Extracting enum T_CCI_CUBRID_STMT from $<"
	@$(CPP) $(AM_CPPFLAGS) -dD $(AM_CPPFLAGS) -dD $< \
	  | $(SED) -n '/typedef enum/{:r;/;/!{N;s/[[:blank:]]//g;s/=\n/=/;s/\n\n/\n/;br}; /T_CCI_CUBRID_STMT/{s/\/\*[^\*\/]*\*\///g;/{/,/}/{:a;$!N;/}/!{$!ba};s/.*{\([^)]*\)}.*/\1/p}}}' > $@
stmt_types_in_dbi_compat_h.out: $(COMPAT_DIR)/dbi_compat.h
	@echo "Extracting enum CUBRID_STMT_TYPE from $<"
	@$(CPP) $(AM_CPPFLAGS) -dD $(AM_CPPFLAGS) -dD $< \
	  | $(SED) -n '/typedef enum/{:r;/;/!{N;s/[[:blank:]]//g;s/=\n/=/;s/\n\n/\n/;br}; /CUBRID_STMT_TYPE/{s/\/\*[^\*\/]*\*\///g;/{/,/}/{:a;$!N;/}/!{$!ba};s/.*{\([^)]*\)}.*/\1/p}}}' > $@
stmt_type_check: stmt_types_in_cas_cci_h.out stmt_types_in_dbi_compat_h.out
	@echo "Checking statement type codes in $(CCI_DIR)/cas_cci.h and $(COMPAT_DIR)/dbi_compat.h"
	$(DIFF) -U0 stmt_types_in_cas_cci_h.out stmt_types_in_dbi_compat_h.out || (echo "Please check statement codes!"; exit 1)

BUILT_SOURCES += isolation_types_in_dbi_compat_h.out isolation_types_in_cubrid_api_h.out isolation_type_check
isolation_types_in_dbi_compat_h.out: $(COMPAT_DIR)/dbi_compat.h
	@echo "Extracting enum DB_TRAN_ISOLATION from $<"
	@$(CPP) $(AM_CPPFLAGS) -dD $(AM_CPPFLAGS) -dD $< \
	  | $(SED) -n '/typedef enum/{:r;/;/!{N;s/[[:blank:]]//g;s/=\n/=/;s/\n\n/\n/;br}; /DB_TRAN_ISOLATION/{s/\/\*[^\*\/]*\*\///g;/{/,/}/{:a;$!N;/}/!{$!ba};s/.*{\([^)]*\)}.*/\1/p}}}' > $@
isolation_types_in_cubrid_api_h.out: $(API_DIR)/cubrid_api.h
	@echo "Extracting enum DB_TRAN_ISOLATION from $<"
	@$(CPP) $(AM_CPPFLAGS) -dD $(AM_CPPFLAGS) -dD $< \
	  | $(SED) -n '/typedef enum/{:r;/;/!{N;s/[[:blank:]]//g;s/=\n/=/;s/\n\n/\n/;br}; /DB_TRAN_ISOLATION/{s/\/\*[^\*\/]*\*\///g;/{/,/}/{:a;$!N;/}/!{$!ba};s/.*{\([^)]*\)}.*/\1/p}}}' > $@
isolation_type_check: isolation_types_in_dbi_compat_h.out isolation_types_in_cubrid_api_h.out
	@echo "Checking transaction isolation type codes in $(COMPAT_DIR)/dbi_compat.h and $(API_DIR)/cubrid_api.h"
	$(DIFF) -U0 isolation_types_in_dbi_compat_h.out isolation_types_in_cubrid_api_h.out || (echo "Please check isolation codes!"; exit 1)

CLEANFILES = $(BUILT_SOURCES)
