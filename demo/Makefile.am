
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


demodir = @CUBRID_DEMODIR@
dist_demo_DATA = demodb_objects demodb_schema
dist_demo_SCRIPTS =  make_cubrid_demo.sh

databasedir = ${localstatedir}
database_DATA = # EMPTY

if DISABLE_CUBRIDENV
install-data-hook :
	@SED@ -i \
	  -e 's|-z "$$CUBRID"|! -d @CUBRID_DEMODIR@|g' \
	  -e 's|$$CUBRID|@CUBRID_DEMODIR@|g' $(DESTDIR)$(demodir)/$(demo_SCRIPTS)
endif
