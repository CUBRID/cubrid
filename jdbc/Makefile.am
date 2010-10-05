
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

if BUILD_CAS_FOR_MYSQL
  DRIVER_MYSQL = JDBC-@BUILD_NUMBER@-cubrid-mysql.jar
else
  DRIVER_MYSQL =
endif

if BUILD_CAS_FOR_ORACLE
  DRIVER_ORACLE = JDBC-@BUILD_NUMBER@-cubrid-oracle.jar
else
  DRIVER_ORACLE =
endif

JDBC_DRIVER_MAJOR_VERSION = `cut -d . -f 1 ${top_srcdir}/BUILD_NUMBER`
JDBC_DRIVER_MINOR_VERSION = `cut -d . -f 2 ${top_srcdir}/BUILD_NUMBER`
JDBC_DRIVER_PATCH_VERSION = `cut -d . -f 3 ${top_srcdir}/BUILD_NUMBER`
JDBC_DRIVER_SERIAL_NUMBER = `cut -d . -f 4 ${top_srcdir}/BUILD_NUMBER`
JDBC_VERSION = \
	-DJDBC_DRIVER_MAJOR_VERSION=$(JDBC_DRIVER_MAJOR_VERSION) \
	-DJDBC_DRIVER_MINOR_VERSION=$(JDBC_DRIVER_MINOR_VERSION) \
	-DJDBC_DRIVER_PATCH_VERSION=$(JDBC_DRIVER_PATCH_VERSION) \
	-DJDBC_DRIVER_SERIAL_NUMBER=$(JDBC_DRIVER_SERIAL_NUMBER)
PJAVA_CFLAGS = $(JDBC_VERSION) @JDK6_DEFINE@

VERSION_FILE = CUBRID-JDBC-@BUILD_NUMBER@

jdbcdir = @CUBRID_JDBCDIR@
jdbc_DATA = $(DRIVER_CUBRID) $(DRIVER_MYSQL) $(DRIVER_ORACLE)

BUILT_SOURCES = \
	src/$(VERSION_FILE) \
	src/cubrid/jdbc/driver/CUBRIDConnection.java \
	src/cubrid/jdbc/driver/CUBRIDDriver.java \
	src/cubrid/jdbc/driver/CUBRIDBlob.java \
	src/cubrid/jdbc/driver/CUBRIDCallableStatement.java \
	src/cubrid/jdbc/driver/CUBRIDClob.java \
	src/cubrid/jdbc/driver/CUBRIDDatabaseMetaData.java \
	src/cubrid/jdbc/driver/CUBRIDDataSource.java \
	src/cubrid/jdbc/driver/CUBRIDPooledConnection.java \
	src/cubrid/jdbc/driver/CUBRIDPreparedStatement.java \
	src/cubrid/jdbc/driver/CUBRIDResultSet.java \
	src/cubrid/jdbc/driver/CUBRIDResultSetMetaData.java \
	src/cubrid/jdbc/driver/CUBRIDResultSetWithoutQuery.java \
	src/cubrid/jdbc/driver/CUBRIDStatement.java \
	src/cubrid/jdbc/jci/UConnection.java \
	src/cubrid/jdbc/jci/UJCIManager.java \
	src/cubrid/jdbc/jci/UInputBuffer.java \
	src/cubrid/jdbc/jci/UJciException.java \
	$(DRIVER_CUBRID) $(DRIVER_MYSQL) $(DRIVER_ORACLE)

CLEANFILES = \
	src/$(VERSION_FILE) \
	src/cubrid/jdbc/driver/CUBRIDConnection.java \
	src/cubrid/jdbc/driver/CUBRIDDriver.java \
	src/cubrid/jdbc/driver/CUBRIDBlob.java \
	src/cubrid/jdbc/driver/CUBRIDCallableStatement.java \
	src/cubrid/jdbc/driver/CUBRIDClob.java \
	src/cubrid/jdbc/driver/CUBRIDDatabaseMetaData.java \
	src/cubrid/jdbc/driver/CUBRIDDataSource.java \
	src/cubrid/jdbc/driver/CUBRIDPooledConnection.java \
	src/cubrid/jdbc/driver/CUBRIDPreparedStatement.java \
	src/cubrid/jdbc/driver/CUBRIDResultSet.java \
	src/cubrid/jdbc/driver/CUBRIDResultSetMetaData.java \
	src/cubrid/jdbc/driver/CUBRIDResultSetWithoutQuery.java \
	src/cubrid/jdbc/driver/CUBRIDStatement.java \
	src/cubrid/jdbc/jci/UConnection.java \
	src/cubrid/jdbc/jci/UJCIManager.java \
	src/cubrid/jdbc/jci/UInputBuffer.java \
	src/cubrid/jdbc/jci/UJciException.java \
	$(DRIVER_CUBRID) $(DRIVER_MYSQL) $(DRIVER_ORACLE)

src/$(VERSION_FILE):
	mkdir -p src/cubrid/jdbc/driver
	mkdir -p src/cubrid/jdbc/jci
	touch $@

src/cubrid/jdbc/driver/CUBRIDConnection.java: $(JDBC_SRC_DIR)/cubrid/jdbc/driver/CUBRIDConnection.pjava
	cpp -C -P $(PJAVA_CFLAGS) $< $@

src/cubrid/jdbc/driver/CUBRIDDriver.java: $(JDBC_SRC_DIR)/cubrid/jdbc/driver/CUBRIDDriver.pjava
	cpp -C -P $(PJAVA_CFLAGS) $< $@

src/cubrid/jdbc/driver/CUBRIDBlob.java: $(JDBC_SRC_DIR)/cubrid/jdbc/driver/CUBRIDBlob.pjava
	cpp -C -P $(PJAVA_CFLAGS) $< $@

src/cubrid/jdbc/driver/CUBRIDCallableStatement.java: $(JDBC_SRC_DIR)/cubrid/jdbc/driver/CUBRIDCallableStatement.pjava
	cpp -C -P $(PJAVA_CFLAGS) $< $@

src/cubrid/jdbc/driver/CUBRIDClob.java: $(JDBC_SRC_DIR)/cubrid/jdbc/driver/CUBRIDClob.pjava
	cpp -C -P $(PJAVA_CFLAGS) $< $@

src/cubrid/jdbc/driver/CUBRIDDatabaseMetaData.java: $(JDBC_SRC_DIR)/cubrid/jdbc/driver/CUBRIDDatabaseMetaData.pjava
	cpp -C -P $(PJAVA_CFLAGS) $< $@

src/cubrid/jdbc/driver/CUBRIDDataSource.java: $(JDBC_SRC_DIR)/cubrid/jdbc/driver/CUBRIDDataSource.pjava
	cpp -C -P $(PJAVA_CFLAGS) $< $@

src/cubrid/jdbc/driver/CUBRIDPooledConnection.java: $(JDBC_SRC_DIR)/cubrid/jdbc/driver/CUBRIDPooledConnection.pjava
	cpp -C -P $(PJAVA_CFLAGS) $< $@

src/cubrid/jdbc/driver/CUBRIDPreparedStatement.java: $(JDBC_SRC_DIR)/cubrid/jdbc/driver/CUBRIDPreparedStatement.pjava
	cpp -C -P $(PJAVA_CFLAGS) $< $@

src/cubrid/jdbc/driver/CUBRIDResultSet.java: $(JDBC_SRC_DIR)/cubrid/jdbc/driver/CUBRIDResultSet.pjava
	cpp -C -P $(PJAVA_CFLAGS) $< $@

src/cubrid/jdbc/driver/CUBRIDResultSetMetaData.java: $(JDBC_SRC_DIR)/cubrid/jdbc/driver/CUBRIDResultSetMetaData.pjava
	cpp -C -P $(PJAVA_CFLAGS) $< $@

src/cubrid/jdbc/driver/CUBRIDResultSetWithoutQuery.java: $(JDBC_SRC_DIR)/cubrid/jdbc/driver/CUBRIDResultSetWithoutQuery.pjava
	cpp -C -P $(PJAVA_CFLAGS) $< $@

src/cubrid/jdbc/driver/CUBRIDStatement.java: $(JDBC_SRC_DIR)/cubrid/jdbc/driver/CUBRIDStatement.pjava
	cpp -C -P $(PJAVA_CFLAGS) $< $@

src/cubrid/jdbc/jci/UConnection.java: $(JDBC_SRC_DIR)/cubrid/jdbc/jci/UConnection.pjava
	cpp -C -P $(PJAVA_CFLAGS) $< $@

src/cubrid/jdbc/jci/UJCIManager.java: $(JDBC_SRC_DIR)/cubrid/jdbc/jci/UJCIManager.pjava
	cpp -C -P $(PJAVA_CFLAGS) $< $@

src/cubrid/jdbc/jci/UInputBuffer.java: $(JDBC_SRC_DIR)/cubrid/jdbc/jci/UInputBuffer.pjava
	cpp -C -P $(PJAVA_CFLAGS) $< $@

src/cubrid/jdbc/jci/UJciException.java: $(JDBC_SRC_DIR)/cubrid/jdbc/jci/UJciException.pjava
	cpp -C -P $(PJAVA_CFLAGS) $< $@

$(DRIVER_CUBRID):
	JAVA_HOME=@JAVA_HOME@ @ANT@ dist-cubrid -buildfile ${top_srcdir}/jdbc/build.xml -Dbasedir=. -Dversion=@BUILD_NUMBER@ -Dsrc=${JDBC_SRC_DIR}/cubrid

$(DRIVER_MYSQL): $(DRIVER_CUBRID)
	JAVA_HOME=@JAVA_HOME@ @ANT@ dist-mysql -buildfile ${top_srcdir}/jdbc/build.xml -Dbasedir=. -Dversion=@BUILD_NUMBER@ -Dsrc=${JDBC_SRC_DIR}/cubrid

$(DRIVER_ORACLE): $(DRIVER_CUBRID)
	JAVA_HOME=@JAVA_HOME@ @ANT@ dist-oracle -buildfile ${top_srcdir}/jdbc/build.xml -Dbasedir=. -Dversion=@BUILD_NUMBER@ -Dsrc=${JDBC_SRC_DIR}/cubrid

install-data-hook:
	test -z "$(DESTDIR)$(jdbcdir)" || mkdir -p "$(DESTDIR)$(jdbcdir)"
	cd $(DESTDIR)$(jdbcdir) && $(LN_S) -f $(DRIVER_CUBRID) cubrid_jdbc.jar

clean-am: clean-generic clean-libtool mostlyclean-am
	JAVA_HOME=@JAVA_HOME@ @ANT@ clean -buildfile ${top_srcdir}/jdbc/build.xml -Dbasedir=.

