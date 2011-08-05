
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
CM_COMMON_DIR = $(SRC)/cm_common

AM_CPPFLAGS = @SRC_INC@ \
	@SYS_DEFS@ @VERSION_DEFS@ @COMMON_DEFS@ @DIR_DEFS@
#AM_LIBADD = @SYS_LIBS@

include_HEADERS = \
	$(CM_COMMON_DIR)/cm_stat.h \
	$(CM_COMMON_DIR)/cm_dep.h

bin_PROGRAMS = cub_jobsa cub_sainfo 

lib_LTLIBRARIES = libcmstat.la libcmdep.la
libcmstat_la_SOURCES = \
	$(CM_COMMON_DIR)/cm_mem_cpu_stat.c \
	$(CM_COMMON_DIR)/cm_broker_admin.c \
	$(CM_COMMON_DIR)/cm_errmsg.c   \
	$(CM_COMMON_DIR)/cm_utils.c	\
	$(CM_COMMON_DIR)/cm_broker_jni.c
libcmstat_la_LDFLAGS = -version-number @LT_VERSION@
libcmstat_la_LIBADD = @BROKER_ADMIN_LIB@

libcmdep_la_SOURCES = \
	$(CM_COMMON_DIR)/cm_dep_tasks.c   \
	$(CM_COMMON_DIR)/cm_dstring.c   \
	$(CM_COMMON_DIR)/cm_nameval.c   \
	$(CM_COMMON_DIR)/cm_utils.c
libcmdep_la_LDFLAGS = -version-number @LT_VERSION@
libcmdep_la_LIBADD = @CS_LIB@

cub_jobsa_SOURCES = $(CM_COMMON_DIR)/cm_class_info_sa.c
cub_jobsa_LDADD = @SA_LIB@

cub_sainfo_SOURCES = $(CM_COMMON_DIR)/cm_trigger_info_sa.c
cub_sainfo_LDADD = @SA_LIB@
