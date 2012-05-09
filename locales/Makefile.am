
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

localesdir = @CUBRID_SHLOCALESDIR@

SUBDIRS = data loclib

locales_DATA = make_locale.sh
dist_locales_DATA = make_locale.sh

install-data-hook:
	cp $(top_srcdir)/locales/make_locale.sh $(prefix)/bin/make_locale.sh
	chmod 755 $(prefix)/bin/make_locale.sh
	rm -f $(prefix)/locales/make_locale.sh