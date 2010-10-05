
JDBC_SRC_DIR = cubrid

BIN_DIR = bin
SRC_DIR = src

JDBC_DRIVER_MAJOR_VERSION := $(shell cut -d . -f 1 ../../BUILD_NUMBER)
JDBC_DRIVER_MINOR_VERSION := $(shell cut -d . -f 2 ../../BUILD_NUMBER)
JDBC_DRIVER_PATCH_VERSION := $(shell cut -d . -f 3 ../../BUILD_NUMBER)
JDBC_DRIVER_SERIAL_NUMBER := $(shell cut -d . -f 4 ../../BUILD_NUMBER)
JDBC_BUILD_NUMBER := $(shell cat ../../BUILD_NUMBER)

JDBC_VERSION = \
	-DJDBC_DRIVER_MAJOR_VERSION=$(JDBC_DRIVER_MAJOR_VERSION) \
	-DJDBC_DRIVER_MINOR_VERSION=$(JDBC_DRIVER_MINOR_VERSION) \
	-DJDBC_DRIVER_PATCH_VERSION=$(JDBC_DRIVER_PATCH_VERSION) \
	-DJDBC_DRIVER_SERIAL_NUMBER=$(JDBC_DRIVER_SERIAL_NUMBER)

PJAVA_CFLAGS = $(JDBC_VERSION)

VERSION_FILE = CUBRID-JDBC-$(JDBC_BUILD_NUMBER)
JDBC_DRIVER = JDBC-$(JDBC_BUILD_NUMBER)-cubrid.jar

PJAVA_SRCS = \
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
	src/cubrid/jdbc/jci/UJciException.java

all: $(PJAVA_SRCS) $(JDBC_DRIVER)

clean:
	ant -buildfile ../../jdbc/build.xml -Dbasedir=. -Dversion=$(JDBC_BUILD_NUMBER) clean

$(JDBC_DRIVER):
	ant -buildfile ../../jdbc/build.xml -Dbasedir=. -Dsrc=$(JDBC_SRC_DIR) -Dversion=$(JDBC_BUILD_NUMBER) dist-cubrid

src/$(VERSION_FILE):
	mkdir -p src/cubrid/jdbc/driver
	mkdir -p src/cubrid/jdbc/jci
	touch $@

src/cubrid/jdbc/driver/CUBRIDConnection.java: cubrid/jdbc/driver/CUBRIDConnection.pjava
	cpp -C -P $(PJAVA_CFLAGS) $< $@

src/cubrid/jdbc/driver/CUBRIDDriver.java: cubrid/jdbc/driver/CUBRIDDriver.pjava
	cpp -C -P $(PJAVA_CFLAGS) $< $@

src/cubrid/jdbc/driver/CUBRIDBlob.java: cubrid/jdbc/driver/CUBRIDBlob.pjava
	cpp -C -P $(PJAVA_CFLAGS) $< $@

src/cubrid/jdbc/driver/CUBRIDCallableStatement.java: cubrid/jdbc/driver/CUBRIDCallableStatement.pjava
	cpp -C -P $(PJAVA_CFLAGS) $< $@

src/cubrid/jdbc/driver/CUBRIDClob.java: cubrid/jdbc/driver/CUBRIDClob.pjava
	cpp -C -P $(PJAVA_CFLAGS) $< $@

src/cubrid/jdbc/driver/CUBRIDDatabaseMetaData.java: cubrid/jdbc/driver/CUBRIDDatabaseMetaData.pjava
	cpp -C -P $(PJAVA_CFLAGS) $< $@

src/cubrid/jdbc/driver/CUBRIDDataSource.java: cubrid/jdbc/driver/CUBRIDDataSource.pjava
	cpp -C -P $(PJAVA_CFLAGS) $< $@

src/cubrid/jdbc/driver/CUBRIDPooledConnection.java: cubrid/jdbc/driver/CUBRIDPooledConnection.pjava
	cpp -C -P $(PJAVA_CFLAGS) $< $@

src/cubrid/jdbc/driver/CUBRIDPreparedStatement.java: cubrid/jdbc/driver/CUBRIDPreparedStatement.pjava
	cpp -C -P $(PJAVA_CFLAGS) $< $@

src/cubrid/jdbc/driver/CUBRIDResultSet.java: cubrid/jdbc/driver/CUBRIDResultSet.pjava
	cpp -C -P $(PJAVA_CFLAGS) $< $@

src/cubrid/jdbc/driver/CUBRIDResultSetMetaData.java: cubrid/jdbc/driver/CUBRIDResultSetMetaData.pjava
	cpp -C -P $(PJAVA_CFLAGS) $< $@

src/cubrid/jdbc/driver/CUBRIDResultSetWithoutQuery.java: cubrid/jdbc/driver/CUBRIDResultSetWithoutQuery.pjava
	cpp -C -P $(PJAVA_CFLAGS) $< $@

src/cubrid/jdbc/driver/CUBRIDStatement.java: cubrid/jdbc/driver/CUBRIDStatement.pjava
	cpp -C -P $(PJAVA_CFLAGS) $< $@

src/cubrid/jdbc/jci/UConnection.java: cubrid/jdbc/jci/UConnection.pjava
	cpp -C -P $(PJAVA_CFLAGS) $< $@

src/cubrid/jdbc/jci/UJCIManager.java: cubrid/jdbc/jci/UJCIManager.pjava
	cpp -C -P $(PJAVA_CFLAGS) $< $@

src/cubrid/jdbc/jci/UInputBuffer.java: cubrid/jdbc/jci/UInputBuffer.pjava
	cpp -C -P $(PJAVA_CFLAGS) $< $@

src/cubrid/jdbc/jci/UJciException.java: cubrid/jdbc/jci/UJciException.pjava
	cpp -C -P $(PJAVA_CFLAGS) $< $@


