
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
JDBC_SRC_DIR = $(SRC)/jdbc

DRIVER_CUBRID = JDBC-@BUILD_NUMBER@-cubrid.jar

VERSION_FILE = CUBRID-JDBC-@BUILD_NUMBER@

EXTRA_DIST = build.xml

jdbcdir = @CUBRID_JDBCDIR@
jdbc_DATA = $(DRIVER_CUBRID)

BUILT_SOURCES = src/$(VERSION_FILE) \
	$(DRIVER_CUBRID)

CLEANFILES = src/$(VERSION_FILE) \
	$(DRIVER_CUBRID)

src/$(VERSION_FILE):
	mkdir -p src
	touch $@

$(DRIVER_CUBRID):
	@ANT@ dist-cubrid -buildfile ${top_srcdir}/jdbc/build.xml -Dbasedir=. -Dversion=@BUILD_NUMBER@ -Dsrc=${JDBC_SRC_DIR}

install-data-hook:
	test -z "$(DESTDIR)$(jdbcdir)" || mkdir -p "$(DESTDIR)$(jdbcdir)"
	cd $(DESTDIR)$(jdbcdir) && $(LN_S) -f $(DRIVER_CUBRID) cubrid_jdbc.jar

clean-local: clean-generic clean-libtool mostlyclean-am
	@ANT@ clean -buildfile ${top_srcdir}/jdbc/build.xml -Dbasedir=.

