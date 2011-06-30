
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

DRIVER = jspserver.jar

javadir = @CUBRID_JAVADIR@
java_DATA = $(DRIVER) ${JDBC_SRC_DIR}/logging.properties

BUILT_SOURCES = $(DRIVER)

CLEANFILES = $(DRIVER)

${DRIVER}:
	@ANT@ dist -buildfile ${top_srcdir}/java/build.xml -Dbasedir=. -Dversion=@BUILD_NUMBER@ -Dsrc=${JDBC_SRC_DIR}

clean-local: clean-generic clean-libtool mostlyclean-am
	@ANT@ clean -buildfile ${top_srcdir}/java/build.xml -Dbasedir=.

