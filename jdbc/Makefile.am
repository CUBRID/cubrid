
SRC = $(top_srcdir)/src
JDBC_SRC_DIR = $(SRC)/jdbc

DRIVER = JDBC-@BUILD_NUMBER@.jar

JDBC_DRIVER_MAJOR_VERSION = ${shell cut -d . -f 1 ${top_srcdir}/BUILD_NUMBER}
JDBC_DRIVER_MINOR_VERSION = ${shell cut -d . -f 2 ${top_srcdir}/BUILD_NUMBER}
JDBC_DRIVER_PATCH_VERSION = ${shell cut -d . -f 3 ${top_srcdir}/BUILD_NUMBER}
JDBC_DRIVER_SERIAL_NUMBER = ${shell cut -d . -f 4 ${top_srcdir}/BUILD_NUMBER}
JDBC_VERSION = \
	-DJDBC_DRIVER_MAJOR_VERSION=$(JDBC_DRIVER_MAJOR_VERSION) \
	-DJDBC_DRIVER_MINOR_VERSION=$(JDBC_DRIVER_MINOR_VERSION) \
	-DJDBC_DRIVER_PATCH_VERSION=$(JDBC_DRIVER_PATCH_VERSION) \
	-DJDBC_DRIVER_SERIAL_NUMBER=$(JDBC_DRIVER_SERIAL_NUMBER)

VERSION_FILE = CUBRID-JDBC-$(JDBC_DRIVER_MAJOR_VERSION).$(JDBC_DRIVER_MINOR_VERSION).$(JDBC_DRIVER_PATCH_VERSION).$(JDBC_DRIVER_SERIAL_NUMBER)

jdbcdir = ${prefix}/jdbc
jdbc_DATA = $(DRIVER)

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
	UJCIManager.java \
	$(DRIVER)


CUBRIDConnection.java: $(JDBC_SRC_DIR)/cubrid/jdbc/driver/CUBRIDConnection.pjava
	cpp -C -P $(JDBC_VERSION) $< $@

CUBRIDDriver.java: $(JDBC_SRC_DIR)/cubrid/jdbc/driver/CUBRIDDriver.pjava
	cpp -C -P $(JDBC_VERSION) $< $@

CUBRIDBlob.java: $(JDBC_SRC_DIR)/cubrid/jdbc/driver/CUBRIDBlob.pjava
	cpp -C -P $(JDBC_VERSION) $< $@

CUBRIDCallableStatement.java: $(JDBC_SRC_DIR)/cubrid/jdbc/driver/CUBRIDCallableStatement.pjava
	cpp -C -P $(JDBC_VERSION) $< $@

CUBRIDClob.java: $(JDBC_SRC_DIR)/cubrid/jdbc/driver/CUBRIDClob.pjava
	cpp -C -P $(JDBC_VERSION) $< $@

CUBRIDDatabaseMetaData.java: $(JDBC_SRC_DIR)/cubrid/jdbc/driver/CUBRIDDatabaseMetaData.pjava
	cpp -C -P $(JDBC_VERSION) $< $@

CUBRIDDataSource.java: $(JDBC_SRC_DIR)/cubrid/jdbc/driver/CUBRIDDataSource.pjava
	cpp -C -P $(JDBC_VERSION) $< $@

CUBRIDPooledConnection.java: $(JDBC_SRC_DIR)/cubrid/jdbc/driver/CUBRIDPooledConnection.pjava
	cpp -C -P $(JDBC_VERSION) $< $@

CUBRIDPreparedStatement.java: $(JDBC_SRC_DIR)/cubrid/jdbc/driver/CUBRIDPreparedStatement.pjava
	cpp -C -P $(JDBC_VERSION) $< $@

CUBRIDResultSet.java: $(JDBC_SRC_DIR)/cubrid/jdbc/driver/CUBRIDResultSet.pjava
	cpp -C -P $(JDBC_VERSION) $< $@

CUBRIDResultSetMetaData.java: $(JDBC_SRC_DIR)/cubrid/jdbc/driver/CUBRIDResultSetMetaData.pjava
	cpp -C -P $(JDBC_VERSION) $< $@

CUBRIDResultSetWithoutQuery.java: $(JDBC_SRC_DIR)/cubrid/jdbc/driver/CUBRIDResultSetWithoutQuery.pjava
	cpp -C -P $(JDBC_VERSION) $< $@

CUBRIDStatement.java: $(JDBC_SRC_DIR)/cubrid/jdbc/driver/CUBRIDStatement.pjava
	cpp -C -P $(JDBC_VERSION) $< $@

UConnection.java: $(JDBC_SRC_DIR)/cubrid/jdbc/jci/UConnection.pjava
	cpp -C -P $(JDBC_VERSION) $< $@

UJCIManager.java: $(JDBC_SRC_DIR)/cubrid/jdbc/jci/UJCIManager.pjava
	cpp -C -P $(JDBC_VERSION) $< $@

${DRIVER}:
	test -d class || mkdir class
	touch class/${VERSION_FILE}
	ant dist -buildfile ${top_srcdir}/jdbc/build.xml -Dbasedir=. -Dversion=@BUILD_NUMBER@ -Dsrc=${JDBC_SRC_DIR}/cubrid

install-exec-hook:
	test -z "$(jdbcdir)" || mkdir -p "$(jdbcdir)"
	cd $(jdbcdir) && $(LN_S) -f $(DRIVER) cubrid_jdbc.jar

clean-am: clean-generic clean-libtool mostlyclean-am
	ant clean -buildfile ${top_srcdir}/jdbc/build.xml -Dbasedir=.

