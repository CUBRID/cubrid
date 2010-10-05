
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
CCI_DIR = $(SRC)/cci
EXEC_DIR = $(SRC)/executables
COMPAT_DIR = $(SRC)/compat
BROKER_DIR = $(SRC)/broker
CM_COMMON_DIR = $(SRC)/cm_common

include_HEADERS = \
	$(CCI_DIR)/cas_cci.h \
	$(EXEC_DIR)/cubrid_esql.h \
	$(BROKER_DIR)/cas_error.h \
	$(CM_COMMON_DIR)/cm_stat.h \
	$(CM_COMMON_DIR)/cm_dep.h

install-data-hook:
	test -z "$(DESTDIR)$(includedir)" || mkdir -p "$(DESTDIR)$(includedir)"
	cp $(COMPAT_DIR)/dbi_compat.h $(DESTDIR)$(includedir)/dbi.h
