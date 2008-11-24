
SRC = $(top_srcdir)/src
JDBC_SRC_DIR = $(SRC)/jdbc

DRIVER = jspserver.jar

JDBC_DRIVER_MAJOR_VERSION = ${shell cut -d . -f 1 ${top_srcdir}/BUILD_NUMBER}
JDBC_DRIVER_MINOR_VERSION = ${shell cut -d . -f 2 ${top_srcdir}/BUILD_NUMBER}
JDBC_DRIVER_PATCH_VERSION = ${shell cut -d . -f 3 ${top_srcdir}/BUILD_NUMBER}
JDBC_DRIVER_SERIAL_NUMBER = ${shell cut -d . -f 4 ${top_srcdir}/BUILD_NUMBER}
JDBC_VERSION = \
	-DJDBC_DRIVER_MAJOR_VERSION=$(JDBC_DRIVER_MAJOR_VERSION) \
	-DJDBC_DRIVER_MINOR_VERSION=$(JDBC_DRIVER_MINOR_VERSION) \
	-DJDBC_DRIVER_PATCH_VERSION=$(JDBC_DRIVER_PATCH_VERSION) \
	-DJDBC_DRIVER_SERIAL_NUMBER=$(JDBC_DRIVER_SERIAL_NUMBER) \
	-DSERVER_SIDE_JDBC

javadir = ${prefix}/java
java_DATA = $(DRIVER) ${JDBC_SRC_DIR}/logging.properties

BUILT_SOURCES = \
	CUBRIDConnection.java \
	CUBRIDDriver.java \
	UConnection.java \
	UJCIManager.java \
	$(DRIVER)

CLEANFILES = \
	CUBRIDConnection.java \
	CUBRIDDriver.java \
	UConnection.java \
	UJCIManager.java

CUBRIDConnection.java: $(JDBC_SRC_DIR)/cubrid/jdbc/driver/CUBRIDConnection.pjava
	cpp -C -P $(JDBC_VERSION) $< $@

CUBRIDDriver.java: $(JDBC_SRC_DIR)/cubrid/jdbc/driver/CUBRIDDriver.pjava
	cpp -C -P $(JDBC_VERSION) $< $@

UConnection.java: $(JDBC_SRC_DIR)/cubrid/jdbc/jci/UConnection.pjava
	cpp -C -P $(JDBC_VERSION) $< $@

UJCIManager.java: $(JDBC_SRC_DIR)/cubrid/jdbc/jci/UJCIManager.pjava
	cpp -C -P $(JDBC_VERSION) $< $@

${DRIVER}:
	ant dist -buildfile ${top_srcdir}/java/build.xml -Dbasedir=. -Dversion=@BUILD_NUMBER@ -Dsrc=${JDBC_SRC_DIR}

clean-am: clean-generic clean-libtool mostlyclean-am
	ant clean -buildfile ${top_srcdir}/java/build.xml -Dbasedir=.  
