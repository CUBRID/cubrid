
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

JDBC_DRIVER_MAJOR_VERSION = `cut -d . -f 1 ${top_srcdir}/BUILD_NUMBER`
JDBC_DRIVER_MINOR_VERSION = `cut -d . -f 2 ${top_srcdir}/BUILD_NUMBER`
JDBC_DRIVER_PATCH_VERSION = `cut -d . -f 3 ${top_srcdir}/BUILD_NUMBER`
JDBC_DRIVER_SERIAL_NUMBER = `cut -d . -f 4 ${top_srcdir}/BUILD_NUMBER`
JDBC_VERSION = \
	-DJDBC_DRIVER_MAJOR_VERSION=$(JDBC_DRIVER_MAJOR_VERSION) \
	-DJDBC_DRIVER_MINOR_VERSION=$(JDBC_DRIVER_MINOR_VERSION) \
	-DJDBC_DRIVER_PATCH_VERSION=$(JDBC_DRIVER_PATCH_VERSION) \
	-DJDBC_DRIVER_SERIAL_NUMBER=$(JDBC_DRIVER_SERIAL_NUMBER) \
	-DSERVER_SIDE_JDBC
PJAVA_CFLAGS = $(JDBC_VERSION) @JDK6_DEFINE@

javadir = @CUBRID_JAVADIR@
java_DATA = $(DRIVER) ${JDBC_SRC_DIR}/logging.properties

BUILT_SOURCES = \
	CUBRIDConnection.java \
	CUBRIDDriver.java \
	CUBRIDBlob.java \
	CUBRIDCallableStatement.java \
	CUBRIDClob.java \
	CUBRIDDatabaseMetaData.java \
	CUBRIDDataSource.java \
	CUBRIDPooledConnection.java \
	CUBRIDPreparedStatement.java \
	CUBRIDResultSet.java \
	CUBRIDResultSetMetaData.java \
	CUBRIDResultSetWithoutQuery.java \
	CUBRIDStatement.java \
	UConnection.java \
	UJCIManager.java \
	$(DRIVER)

CLEANFILES = \
	CUBRIDConnection.java \
	CUBRIDDriver.java \
	CUBRIDBlob.java \
	CUBRIDCallableStatement.java \
	CUBRIDClob.java \
	CUBRIDDatabaseMetaData.java \
	CUBRIDDataSource.java \
	CUBRIDPooledConnection.java \
	CUBRIDPreparedStatement.java \
	CUBRIDResultSet.java \
	CUBRIDResultSetMetaData.java \
	CUBRIDResultSetWithoutQuery.java \
	CUBRIDStatement.java \
	UConnection.java \
	UJCIManager.java

CUBRIDConnection.java: $(JDBC_SRC_DIR)/cubrid/jdbc/driver/CUBRIDConnection.pjava
	cpp -C -P $(PJAVA_CFLAGS) $< $@

CUBRIDDriver.java: $(JDBC_SRC_DIR)/cubrid/jdbc/driver/CUBRIDDriver.pjava
	cpp -C -P $(PJAVA_CFLAGS) $< $@

CUBRIDBlob.java: $(JDBC_SRC_DIR)/cubrid/jdbc/driver/CUBRIDBlob.pjava
	cpp -C -P $(PJAVA_CFLAGS) $< $@

CUBRIDCallableStatement.java: $(JDBC_SRC_DIR)/cubrid/jdbc/driver/CUBRIDCallableStatement.pjava
	cpp -C -P $(PJAVA_CFLAGS) $< $@

CUBRIDClob.java: $(JDBC_SRC_DIR)/cubrid/jdbc/driver/CUBRIDClob.pjava
	cpp -C -P $(PJAVA_CFLAGS) $< $@

CUBRIDDatabaseMetaData.java: $(JDBC_SRC_DIR)/cubrid/jdbc/driver/CUBRIDDatabaseMetaData.pjava
	cpp -C -P $(PJAVA_CFLAGS) $< $@

CUBRIDDataSource.java: $(JDBC_SRC_DIR)/cubrid/jdbc/driver/CUBRIDDataSource.pjava
	cpp -C -P $(PJAVA_CFLAGS) $< $@

CUBRIDPooledConnection.java: $(JDBC_SRC_DIR)/cubrid/jdbc/driver/CUBRIDPooledConnection.pjava
	cpp -C -P $(PJAVA_CFLAGS) $< $@

CUBRIDPreparedStatement.java: $(JDBC_SRC_DIR)/cubrid/jdbc/driver/CUBRIDPreparedStatement.pjava
	cpp -C -P $(PJAVA_CFLAGS) $< $@

CUBRIDResultSet.java: $(JDBC_SRC_DIR)/cubrid/jdbc/driver/CUBRIDResultSet.pjava
	cpp -C -P $(PJAVA_CFLAGS) $< $@

CUBRIDResultSetMetaData.java: $(JDBC_SRC_DIR)/cubrid/jdbc/driver/CUBRIDResultSetMetaData.pjava
	cpp -C -P $(PJAVA_CFLAGS) $< $@

CUBRIDResultSetWithoutQuery.java: $(JDBC_SRC_DIR)/cubrid/jdbc/driver/CUBRIDResultSetWithoutQuery.pjava
	cpp -C -P $(PJAVA_CFLAGS) $< $@

CUBRIDStatement.java: $(JDBC_SRC_DIR)/cubrid/jdbc/driver/CUBRIDStatement.pjava
	cpp -C -P $(PJAVA_CFLAGS) $< $@

UConnection.java: $(JDBC_SRC_DIR)/cubrid/jdbc/jci/UConnection.pjava
	cpp -C -P $(PJAVA_CFLAGS) $< $@

UJCIManager.java: $(JDBC_SRC_DIR)/cubrid/jdbc/jci/UJCIManager.pjava
	cpp -C -P $(PJAVA_CFLAGS) $< $@

${DRIVER}:
	JAVA_HOME=@JAVA_HOME@ @ANT@ dist -buildfile ${top_srcdir}/java/build.xml -Dbasedir=. -Dversion=@BUILD_NUMBER@ -Dsrc=${JDBC_SRC_DIR}

clean-am: clean-generic clean-libtool mostlyclean-am
	JAVA_HOME=@JAVA_HOME@ @ANT@ clean -buildfile ${top_srcdir}/java/build.xml -Dbasedir=.  
