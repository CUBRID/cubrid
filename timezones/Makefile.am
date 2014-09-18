
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

timezonesdir = @CUBRID_TZ_DIR@

EXTRA_DIST = make_tz_Win32.bat make_tz_x64.bat tzlib_win_x64 tzlib_win_Win32

SUBDIRS = tzdata tzlib

dist_bin_SCRIPTS = make_tz.sh
dist_timezones_DATA = $(top_srcdir)/src/base/tz_list.h
