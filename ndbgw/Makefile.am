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
BASE_DIR = $(SRC)/base
DBGW_DIR = $(SRC)/dbgw/dbgw3
DBGW_AI_DIR = $(DBGW_DIR)/adapter
DBGW_CI_DIR = $(DBGW_DIR)/client
DBGW_SYS_DIR = $(DBGW_DIR)/system
DBGW_SQL_DIR = $(DBGW_DIR)/sql
CCI_DIR = $(SRC)/cci
BROKER_DIR = $(SRC)/broker

AM_CPPFLAGS = @EXT_INC@ @DBGW_DEFS@ @VERSION_DEFS@

if BUILD_DBGW
if BUILD_NCLAVIS
  DBGW_LIB = libdbgw3_nclavis.la
else
	DBGW_LIB =
endif  
endif

if BUILD_DBGW_ORACLE
if BUILD_NCLAVIS
  DBGW_ORACLE_LIB = libdbgw3_nclavis_oracle.la
else
  DBGW_ORACLE_LIB =
endif
endif

if BUILD_DBGW_MYSQL51
if BUILD_NCLAVIS
  DBGW_MYSQL_LIB = libdbgw3_nclavis_mysql.la
else
  DBGW_MYSQL_LIB =
endif
endif

if BUILD_DBGW_MYSQL
if BUILD_NCLAVIS
  DBGW_MYSQL61_LIB = libdbgw3_nclavis_mysql61.la
else
  DBGW_MYSQL61_LIB =
endif
endif

if BUILD_DBGW_NBASE_T
if BUILD_NCLAVIS
  DBGW_NBASE_T_LIB = libdbgw3_nbase_t.la libdbgw3_nclavis_nbase_t.la
else
  DBGW_NBASE_T_LIB = libdbgw3_nbase_t.la
endif
else
	DBGW_NBASE_T_LIB =
endif

if BUILD_DBGW_ALL
if BUILD_NCLAVIS
  DBGW_ALL_LIB = libdbgw3_nclavis_all.la
else
  DBGW_ALL_LIB =
endif
endif

# DBGW Season 3 Library
dbgw3_includedir = $(includedir)
dbgw3_include_HEADERS = \
        $(CCI_DIR)/cci_log.h \
        $(SRC)/dbgw/DBGWClient.h \
        $(SRC)/dbgw/DBGWConnector3.h
dbgw3_base_includedir = $(includedir)/dbgw3
dbgw3_base_include_HEADERS = \
        $(DBGW_DIR)/Common.h \
        $(DBGW_DIR)/Exception.h \
        $(DBGW_DIR)/Lob.h \
        $(DBGW_DIR)/Logger.h \
        $(DBGW_DIR)/Value.h \
        $(DBGW_DIR)/ValueSet.h \
        $(DBGW_DIR)/SynchronizedResource.h
dbgw3_sys_includedir = $(includedir)/dbgw3/system
dbgw3_sys_include_HEADERS = \
        $(DBGW_SYS_DIR)/ThreadEx.h \
        $(DBGW_SYS_DIR)/DBGWPorting.h
dbgw3_sql_includedir = $(includedir)/dbgw3/sql
dbgw3_sql_include_HEADERS = \
        $(DBGW_SQL_DIR)/CallableStatement.h \
        $(DBGW_SQL_DIR)/Connection.h \
        $(DBGW_SQL_DIR)/DatabaseInterface.h \
        $(DBGW_SQL_DIR)/DriverManager.h \
        $(DBGW_SQL_DIR)/PreparedStatement.h \
        $(DBGW_SQL_DIR)/ResultSet.h \
        $(DBGW_SQL_DIR)/ResultSetMetaData.h \
        $(DBGW_SQL_DIR)/Statement.h
dbgw3_ci_includedir = $(includedir)/dbgw3/client
dbgw3_ci_include_HEADERS = \
        $(DBGW_CI_DIR)/Interface.h \
        $(DBGW_CI_DIR)/Mock.h \
        $(DBGW_CI_DIR)/ConfigurationObject.h \
        $(DBGW_CI_DIR)/Configuration.h \
        $(DBGW_CI_DIR)/ClientResultSet.h \
        $(DBGW_CI_DIR)/Resource.h \
        $(DBGW_CI_DIR)/QueryMapper.h \
        $(DBGW_CI_DIR)/Client.h
dbgw3_ai_includedir = $(includedir)/dbgw3/adapter
dbgw3_ai_include_HEADERS = \
        $(DBGW_AI_DIR)/Adapter.h
       
dbgw3_base_SOURCES = \
        $(BASE_DIR)/porting.c \
        $(CCI_DIR)/cci_log.cpp \
        $(DBGW_DIR)/Exception.cpp \
        $(DBGW_DIR)/Logger.cpp \
        $(DBGW_DIR)/SynchronizedResource.cpp \
        $(DBGW_DIR)/Value.cpp \
        $(DBGW_DIR)/ValueSet.cpp \
        $(DBGW_SYS_DIR)/ConditionVariable.cpp \
        $(DBGW_SYS_DIR)/File.cpp \
        $(DBGW_SYS_DIR)/Mutex.cpp \
        $(DBGW_SYS_DIR)/Thread.cpp \
        $(DBGW_SYS_DIR)/ThreadEx.cpp \
        $(DBGW_SYS_DIR)/Time.cpp \
        $(DBGW_SYS_DIR)/DBGWPorting.cpp \
        $(DBGW_AI_DIR)/Adapter.cpp \
        $(DBGW_CI_DIR)/AsyncWaiter.cpp \
        $(DBGW_CI_DIR)/AsyncWorker.cpp \
        $(DBGW_CI_DIR)/AsyncWorkerJob.cpp \
        $(DBGW_CI_DIR)/Client.cpp \
        $(DBGW_CI_DIR)/ClientResultSetImpl.cpp \
        $(DBGW_CI_DIR)/CharsetConverter.cpp \
        $(DBGW_CI_DIR)/Configuration.cpp \
        $(DBGW_CI_DIR)/ConfigurationObject.cpp \
        $(DBGW_CI_DIR)/Connector.cpp \
        $(DBGW_CI_DIR)/Executor.cpp \
        $(DBGW_CI_DIR)/ExecutorHandler.cpp \
        $(DBGW_CI_DIR)/ExecutorStatement.cpp \
        $(DBGW_CI_DIR)/Group.cpp \
        $(DBGW_CI_DIR)/Host.cpp \
        $(DBGW_CI_DIR)/Mock.cpp \
        $(DBGW_CI_DIR)/Query.cpp \
        $(DBGW_CI_DIR)/QueryMapper.cpp \
        $(DBGW_CI_DIR)/Resource.cpp \
        $(DBGW_CI_DIR)/Service.cpp \
        $(DBGW_CI_DIR)/StatisticsMonitor.cpp \
        $(DBGW_CI_DIR)/Timer.cpp \
        $(DBGW_CI_DIR)/XmlParser.cpp \
        $(DBGW_SQL_DIR)/CallableStatement.cpp \
        $(DBGW_SQL_DIR)/Connection.cpp \
        $(DBGW_SQL_DIR)/DriverManager.cpp \
        $(DBGW_SQL_DIR)/PreparedStatement.cpp \
        $(DBGW_SQL_DIR)/ResultSet.cpp \
        $(DBGW_SQL_DIR)/Statement.cpp
dbgw3_SOURCES = \
				$(dbgw3_base_SOURCES) \
        $(DBGW_SQL_DIR)/cubrid/CUBRIDCallableStatement.cpp \
        $(DBGW_SQL_DIR)/cubrid/CUBRIDConnection.cpp \
        $(DBGW_SQL_DIR)/cubrid/CUBRIDException.cpp \
        $(DBGW_SQL_DIR)/cubrid/CUBRIDPreparedStatement.cpp \
        $(DBGW_SQL_DIR)/cubrid/CUBRIDResultSet.cpp \
        $(DBGW_SQL_DIR)/cubrid/CUBRIDResultSetMetaData.cpp \
        $(DBGW_SQL_DIR)/cubrid/CUBRIDStatementBase.cpp \
        $(DBGW_SQL_DIR)/cubrid/CUBRIDMock.cpp
dbgw3_oracle_SOURCES = \
				$(dbgw3_base_SOURCES) \
        $(DBGW_SQL_DIR)/oracle/OracleCallableStatement.cpp \
        $(DBGW_SQL_DIR)/oracle/OracleConnection.cpp \
        $(DBGW_SQL_DIR)/oracle/OracleException.cpp \
        $(DBGW_SQL_DIR)/oracle/OracleHandle.cpp \
        $(DBGW_SQL_DIR)/oracle/OracleLob.cpp \
        $(DBGW_SQL_DIR)/oracle/OracleParameterMetaData.cpp \
        $(DBGW_SQL_DIR)/oracle/OraclePreparedStatement.cpp \
        $(DBGW_SQL_DIR)/oracle/OracleStatementBase.cpp \
        $(DBGW_SQL_DIR)/oracle/OracleResultSet.cpp \
        $(DBGW_SQL_DIR)/oracle/OracleResultSetMetaData.cpp \
        $(DBGW_SQL_DIR)/oracle/OracleValue.cpp \
        $(DBGW_SQL_DIR)/oracle/OracleMock.cpp
dbgw3_mysql_SOURCES = \
				$(dbgw3_base_SOURCES) \
        $(DBGW_SQL_DIR)/mysql/MySQLConnection.cpp \
        $(DBGW_SQL_DIR)/mysql/MySQLException.cpp \
        $(DBGW_SQL_DIR)/mysql/MySQLPreparedStatement.cpp \
        $(DBGW_SQL_DIR)/mysql/MySQLResultSet.cpp \
        $(DBGW_SQL_DIR)/mysql/MySQLResultSetMetaData.cpp \
        $(DBGW_SQL_DIR)/mysql/MySQLStatementBase.cpp \
        $(DBGW_SQL_DIR)/mysql/MySQLValue.cpp \
        $(DBGW_SQL_DIR)/mysql/MySQLMock.cpp
dbgw3_nbase_t_SOURCES = \
        $(dbgw3_base_SOURCES) \
        $(DBGW_SQL_DIR)/nbase_t/NBaseTException.cpp \
        $(DBGW_SQL_DIR)/nbase_t/NBaseTConnection.cpp \
        $(DBGW_SQL_DIR)/nbase_t/NBaseTPreparedStatement.cpp \
        $(DBGW_SQL_DIR)/nbase_t/NBaseTExecutor.cpp \
        $(DBGW_SQL_DIR)/nbase_t/NBaseTResultSetMetaData.cpp \
        $(DBGW_SQL_DIR)/nbase_t/NBaseTResultSet.cpp
dbgw3_all_SOURCES = \
				$(dbgw3_base_SOURCES) \
        $(DBGW_SQL_DIR)/oracle/OracleCallableStatement.cpp \
        $(DBGW_SQL_DIR)/oracle/OracleConnection.cpp \
        $(DBGW_SQL_DIR)/oracle/OracleException.cpp \
        $(DBGW_SQL_DIR)/oracle/OracleHandle.cpp \
        $(DBGW_SQL_DIR)/oracle/OracleLob.cpp \
        $(DBGW_SQL_DIR)/oracle/OracleParameterMetaData.cpp \
        $(DBGW_SQL_DIR)/oracle/OraclePreparedStatement.cpp \
        $(DBGW_SQL_DIR)/oracle/OracleStatementBase.cpp \
        $(DBGW_SQL_DIR)/oracle/OracleResultSet.cpp \
        $(DBGW_SQL_DIR)/oracle/OracleResultSetMetaData.cpp \
        $(DBGW_SQL_DIR)/oracle/OracleValue.cpp \
        $(DBGW_SQL_DIR)/oracle/OracleMock.cpp \
        $(DBGW_SQL_DIR)/mysql/MySQLConnection.cpp \
        $(DBGW_SQL_DIR)/mysql/MySQLException.cpp \
        $(DBGW_SQL_DIR)/mysql/MySQLPreparedStatement.cpp \
        $(DBGW_SQL_DIR)/mysql/MySQLResultSet.cpp \
        $(DBGW_SQL_DIR)/mysql/MySQLResultSetMetaData.cpp \
        $(DBGW_SQL_DIR)/mysql/MySQLStatementBase.cpp \
        $(DBGW_SQL_DIR)/mysql/MySQLValue.cpp \
        $(DBGW_SQL_DIR)/mysql/MySQLMock.cpp \
        $(DBGW_SQL_DIR)/cubrid/CUBRIDCallableStatement.cpp \
        $(DBGW_SQL_DIR)/cubrid/CUBRIDConnection.cpp \
        $(DBGW_SQL_DIR)/cubrid/CUBRIDException.cpp \
        $(DBGW_SQL_DIR)/cubrid/CUBRIDPreparedStatement.cpp \
        $(DBGW_SQL_DIR)/cubrid/CUBRIDResultSet.cpp \
        $(DBGW_SQL_DIR)/cubrid/CUBRIDResultSetMetaData.cpp \
        $(DBGW_SQL_DIR)/cubrid/CUBRIDStatementBase.cpp \
        $(DBGW_SQL_DIR)/cubrid/CUBRIDMock.cpp
lib_LTLIBRARIES = $(DBGW_LIB) $(DBGW_ORACLE_LIB) $(DBGW_MYSQL_LIB) $(DBGW_MYSQL61_LIB) $(DBGW_NBASE_T_LIB) $(DBGW_ALL_LIB)
libdbgw3_nclavis_la_SOURCES = \
        $(dbgw3_SOURCES) \
        $(DBGW_DIR)/NClavisClient.cpp \
        $(DBGW_DIR)/JsonParser.cpp
libdbgw3_nclavis_la_CPPFLAGS = \
        $(AM_CPPFLAGS) @SRC_INC@ -I$(CCI_DIR) -I$(BROKER_DIR) -DUSE_NCLAVIS
libdbgw3_nclavis_la_LDFLAGS = -version-number @LT_VERSION@
libdbgw3_nclavis_la_LIBADD = @EXPAT_LIBS@ @JANSSON_LIBS@ -lrt -lcurl
libdbgw3_nclavis_la_LINK = $(CXXLINK)

libdbgw3_nclavis_oracle_la_SOURCES = \
        $(dbgw3_oracle_SOURCES) \
        $(DBGW_DIR)/NClavisClient.cpp \
        $(DBGW_DIR)/JsonParser.cpp
libdbgw3_nclavis_oracle_la_CPPFLAGS = \
        $(AM_CPPFLAGS) @SRC_INC@ -I$(CCI_DIR) -I$(BROKER_DIR) @ORACLE_INC_PATH@ -DDBGW_ORACLE -DUSE_NCLAVIS
libdbgw3_nclavis_oracle_la_LDFLAGS = -version-number @LT_VERSION@ @ORACLE_LIB_PATH@
libdbgw3_nclavis_oracle_la_LIBADD = @EXPAT_LIBS@ @JANSSON_LIBS@ -lclntsh -lnnz11 -lrt -lcurl
libdbgw3_nclavis_oracle_la_LINK = $(CXXLINK)

libdbgw3_nclavis_mysql_la_SOURCES = \
        $(dbgw3_mysql_SOURCES) \
        $(DBGW_DIR)/NClavisClient.cpp \
        $(DBGW_DIR)/JsonParser.cpp
libdbgw3_nclavis_mysql_la_CPPFLAGS = \
        $(AM_CPPFLAGS) @SRC_INC@ -I$(CCI_DIR) -I$(BROKER_DIR) @MYSQL_INC_PATH@ -DDBGW_MYSQL -DUSE_NCLAVIS
libdbgw3_nclavis_mysql_la_LDFLAGS = -version-number @LT_VERSION@ @MYSQL_LIB_PATH@
libdbgw3_nclavis_mysql_la_LIBADD = @EXPAT_LIBS@ @JANSSON_LIBS@ -lmysqlclient -lrt -lcurl
libdbgw3_nclavis_mysql_la_LINK = $(CXXLINK)

libdbgw3_nclavis_mysql61_la_SOURCES = \
        $(dbgw3_mysql_SOURCES) \
        $(DBGW_DIR)/NClavisClient.cpp \
        $(DBGW_DIR)/JsonParser.cpp
libdbgw3_nclavis_mysql61_la_CPPFLAGS = \
        $(AM_CPPFLAGS) @SRC_INC@ -I$(CCI_DIR) -I$(BROKER_DIR) @MYSQL_INC_PATH@ -DDBGW_MYSQL -DUSE_NCLAVIS
libdbgw3_nclavis_mysql61_la_LDFLAGS = -version-number @LT_VERSION@ @MYSQL_LIB_PATH@
libdbgw3_nclavis_mysql61_la_LIBADD = @EXPAT_LIBS@ @JANSSON_LIBS@ -lmysqlclient -lrt -lcurl
libdbgw3_nclavis_mysql61_la_LINK = $(CXXLINK)

libdbgw3_nbase_t_la_SOURCES = \
        $(dbgw3_nbase_t_SOURCES)
libdbgw3_nbase_t_la_CPPFLAGS = \
        $(AM_CPPFLAGS) @SRC_INC@ -I$(CCI_DIR) -I$(BROKER_DIR) @NBASE_T_INC_PATH@ -DDBGW_NBASE_T
libdbgw3_nbase_t_la_LDFLAGS = -version-number @LT_VERSION@ @NBASE_T_LIB_PATH@ -lz
libdbgw3_nbase_t_la_LIBADD = @EXPAT_LIBS@ @JANSSON_LIBS@ -lnbaseclient
libdbgw3_nbase_t_la_LINK = $(CXXLINK)

libdbgw3_nclavis_nbase_t_la_SOURCES = \
        $(dbgw3_nbase_t_SOURCES) \
        $(DBGW_DIR)/NClavisClient.cpp \
        $(DBGW_DIR)/JsonParser.cpp
libdbgw3_nclavis_nbase_t_la_CPPFLAGS = \
        $(AM_CPPFLAGS) @SRC_INC@ -I$(CCI_DIR) -I$(BROKER_DIR) @NBASE_T_INC_PATH@ -DDBGW_NBASE_T -DUSE_NCLAVIS
libdbgw3_nclavis_nbase_t_la_LDFLAGS = -version-number @LT_VERSION@ @NBASE_T_LIB_PATH@ -lz
libdbgw3_nclavis_nbase_t_la_LIBADD = @EXPAT_LIBS@ @JANSSON_LIBS@ -lnbaseclient -lcurl
libdbgw3_nclavis_nbase_t_la_LINK = $(CXXLINK)

libdbgw3_nclavis_all_la_SOURCES = \
        $(dbgw3_all_SOURCES) \
        $(DBGW_DIR)/NClavisClient.cpp \
        $(DBGW_DIR)/JsonParser.cpp
libdbgw3_nclavis_all_la_CPPFLAGS = \
        $(AM_CPPFLAGS) @SRC_INC@ -I$(CCI_DIR) -I$(BROKER_DIR) @MYSQL_INC_PATH@ -DDBGW_ALL \
        @ORACLE_INC_PATH@ -DUSE_NCLAVIS
libdbgw3_nclavis_all_la_LDFLAGS = -version-number @LT_VERSION@ @MYSQL_LIB_PATH@ @ORACLE_LIB_PATH@
libdbgw3_nclavis_all_la_LIBADD = @EXPAT_LIBS@ @JANSSON_LIBS@ -lclntsh -lnnz11 -lmysqlclient -lrt -lcurl
libdbgw3_nclavis_all_la_LINK = $(CXXLINK)

VERSIONED_FILES = \
        $(BASE_DIR)/release_string.c
$(VERSIONED_FILES): $(top_srcdir)/BUILD_NUMBER
		touch $@
