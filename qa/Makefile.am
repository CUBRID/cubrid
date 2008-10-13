
SRC = $(top_srcdir)/src
COMPAT_DIR = $(SRC)/compat
THREAD_DIR = $(SRC)/thread
BASE_DIR = $(SRC)/base
HEAPLAYER_DIR = $(SRC)/heaplayers
CONNECTION_DIR = $(SRC)/connection
COMMUNICATION_DIR = $(SRC)/communication
PARSER_DIR = $(SRC)/parser
OPTIMIZER_DIR = $(SRC)/optimizer
QUERY_DIR = $(SRC)/query
OBJECT_DIR = $(SRC)/object
JSP_DIR = $(SRC)/jsp
TRANSACTION_DIR = $(SRC)/transaction
STORAGE_DIR = $(SRC)/storage
EXECUTABLE_DIR = $(SRC)/executables
BROKER_DIR = $(SRC)/broker
CCI_DIR = $(SRC)/cci
DBMT_DIR = $(SRC)/dbmt
QATOOLS_DIR = $(SRC)/qatools

CESQL = $(top_builddir)/util/cubrid_esql

INCLUDES = @SRC_INC@ @EXT_INC@

DEFS = @SYS_DEFS@ @SA_DEFS@ @COMMON_DEFS@ @VERSION_DEFS@

AM_CFLAGS = -fPIC -DPIC

qatoolsdir = ${prefix}/qatools
qatools_PROGRAMS = populate methods.so nrecurse.so recurse.so

populate_SOURCES = $(QATOOLS_DIR)/comp.c $(QATOOLS_DIR)/populate.c
populate_LDFLAGS = @SA_LIB@ @CAS_LIB@

BUILT_SOURCES = methods.so nrecurse.so recurse.so
DISTCLEANFILES = methods.so nrecurse.so recurse.so nrecurse.c recurse.c

methods_so_SOURCES = $(QATOOLS_DIR)/methods.c
methods_so_LDFLAGS = -shared

nrecurse.so: nrecurse.o
	$(CCLD) -shared $(CFLAGS) -L$(top_builddir)/util/.libs -lcubridesql $< -o $@

nrecurse.o: nrecurse.c
	$(COMPILE) $(AM_CFLAGS) -fPIC -DPIC -c $< -o $@

nrecurse.c: $(QATOOLS_DIR)/nrecurse.ec
	CUBRID=$(top_builddir) $(CESQL) $<

recurse.so: recurse.o
	$(CCLD) -shared $(CFLAGS) -L$(top_builddir)/util/.libs -lcubridesql $< -o $@

recurse.o: recurse.c
	$(COMPILE) $(AM_CFLAGS) -fPIC -DPIC -c $< -o $@

recurse.c: $(QATOOLS_DIR)/recurse.ec
	CUBRID=$(top_builddir) $(CESQL) $<
