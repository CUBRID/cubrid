
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

tzlibdir = @CUBRID_TZ_DIR@/tzlib

dist_tzlib_DATA = build_tz.sh $(top_srcdir)/src/base/timezone_lib_common.h

BUILT_SOURCES = timezones.c

timezones.c:
	CUBRID=../.. LD_LIBRARY_PATH=../../sa/.libs ../../util/cub_admin gen_tz -i $(top_srcdir)/timezones/tzdata -g new

lib_LTLIBRARIES = libcubrid_timezones.la
libcubrid_timezones_la_SOURCES = timezones.c 
libcubrid_timezones_la_CPPFLAGS = -I$(top_srcdir)/src/base -fPIC
libcubrid_timezones_la_LDFLAGS = -shared -version-number @LT_VERSION@

CLEANFILES = $(BUILT_SOURCES)
