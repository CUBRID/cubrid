/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

#include <errno.h>
#include <sstream>
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp>
#include "dbgw3/client/Client.h"
#include "dbgw3/system/DBGWPorting.h"
#include "dbgw3/system/File.h"
#include "dbgw3/sql/DatabaseInterface.h"
#include "dbgw3/sql/Statement.h"
#include "dbgw3/sql/PreparedStatement.h"
#include "dbgw3/sql/CallableStatement.h"
#include "dbgw3/sql/ResultSetMetaData.h"
#include "dbgw3/sql/ResultSet.h"
#include "dbgw3/client/XmlParser.h"
#include "dbgw3/client/CharsetConverter.h"
#include "dbgw3/client/Group.h"
#include "dbgw3/client/Connector.h"
#include "dbgw3/client/Executor.h"
#include "dbgw3/client/Service.h"
#include "dbgw3/client/Host.h"
#include "dbgw3/client/Query.h"
#include "dbgw3/client/ClientResultSetImpl.h"
#include "dbgw3/client/StatisticsMonitor.h"
#include "dbgw3/client/AsyncWorkerJob.h"

namespace dbgw
{

  static const int XML_FILE_BUFFER_SIZE = 4096;

  static const char *XN_CONFIGURATION = "configuration";
  static const char *XN_CONFIGURATION_PR_MAX_WAIT_EXIT_TIME_MILLIS =
      "maxWaitExitTimeMillis";

  static const char *XN_JOB_QUEUE= "jobqueue";
  static const char *XN_JOB_QUEUE_MAX_SIZE = "maxSize";

  static const char *XN_CONNECTOR = "connector";

  static const char *XN_QUERYMAP = "querymap";
  static const char *XN_QUERYMAP_PR_VERSION = "version";

  static const char *XN_STATISTICS = "statistics";
  static const char *XN_STATISTICS_LOG_PATH = "logPath";
  static const char *XN_STATISTICS_STATISTICS_TYPE = "type";
  static const char *XN_STATISTICS_MAX_FILE_SIZE_KBYTES = "maxFileSizeKBytes";
  static const char *XN_STATISTICS_MAX_BACKUP_COUNT = "maxBackupCount";
  static const char *XN_STATISTICS_TIME_BETWEEN_STATISTICS_RUNS_MILLIS =
      "timeBetweenStatisticsRunsMillis";

  static const char *XN_SERVICE = "service";
  static const char *XN_SERVICE_PR_NAMESPACE = "namespace";
  static const char *XN_SERVICE_PR_DESCRIPTION = "description";
  static const char *XN_SERVICE_PR_VALIDATE_RESULT = "validateResult";
  static const char *XN_SERVICE_PR_VALIDATE_RESULT_HIDDEN = "validate-result";
  static const char *XN_SERVICE_PR_VALIDATE_RATIO = "validateRatio";
  static const char *XN_SERVICE_PR_VALIDATE_RATIO_HIDDEN = "validate-ratio";

  static const char *XN_GROUP = "group";
  static const char *XN_GROUP_PR_NAME = "name";
  static const char *XN_GROUP_PR_DESCRIPTION = "description";
  static const char *XN_GROUP_PR_INACTIVATE = "inactivate";
  static const char *XN_GROUP_PR_IGNORE_RESULT = "ignoreResult";
  static const char *XN_GROUP_PR_IGNORE_RESULT_HIDDEN = "ignore-result";
  static const char *XN_GROUP_PR_USE_DEFAULT_VALUE_WHEN_FAILED_TO_CAST_PARAM =
      "useDefaultValueWhenFailedToCastParam";
  static const char *XN_GROUP_PR_MAX_PREPARED_STATEMENT_SIZE =
      "maxPrepraredStatementSize";
  static const char *XN_GROUP_PR_CLIENT_CHARSET = "clientCharset";
  static const char *XN_GROUP_PR_DB_CHARSET = "dbCharset";
  static const char *XN_GROUP_PR_DBTYPE = "dbtype";

  static const char *XN_POOL = "pool";
  static const char *XN_POOL_PR_INIT_SIZE = "initialSize";
  static const char *XN_POOL_PR_INIT_SIZE_HIDDEN = "pool-size";
  static const char *XN_POOL_PR_MIN_IDLE = "minIdle";
  static const char *XN_POOL_PR_MAX_IDLE = "maxIdle";
  static const char *XN_POOL_PR_MAX_ACTIVE = "maxActive";
  static const char *XN_POOL_PR_TIME_BETWEEN_EVICTION_RUNS_MILLIS =
      "timeBetweenEvictionRunsMillis";
  static const char *XN_POOL_PR_NUM_TESTS_PER_EVICTIONRUN =
      "numTestsPerEvictionRun";
  static const char *XN_POOL_PR_MIN_EVICTABLE_IDLE_TIMEMILLIS =
      "minEvictableIdleTimeMillis";

  static const char *XN_DBINFO = "dbinfo";
  static const char *XN_DBINFO_PR_DBNAME = "dbname";
  static const char *XN_DBINFO_PR_DBUSER = "dbuser";
  static const char *XN_DBINFO_PR_DBPASSWD = "dbpasswd";

  static const char *XN_HOST = "host";
  static const char *XN_HOST_PR_ADDRESS = "address";
  static const char *XN_HOST_PR_PORT = "port";
  static const char *XN_HOST_PR_URL = "url";
  static const char *XN_HOST_PR_USER = "user";
  static const char *XN_HOST_PR_PASSWORD = "password";
  static const char *XN_HOST_PR_WEIGHT = "weight";

  static const char *XN_ALTHOSTS = "althosts";

  static const char *XN_10_DEFINEDQUERY = "definedquery";

  static const char *XN_10_SQL = "sql";
  static const char *XN_10_SQL_PR_NAME = "name";
  static const char *XN_10_SQL_PR_TYPE = "type";

  static const char *XN_10_QUERY = "query";

  static const char *XN_10_PLACEHOLDER = "placeholder";

  static const char *XN_10_PARAMETER = "parameter";
  static const char *XN_10_PARAMETER_PR_NAME = "name";
  static const char *XN_10_PARAMETER_PR_INDEX = "index";
  static const char *XN_10_PARAMETER_PR_TYPE = "type";
  static const char *XN_10_PARAMETER_PR_MODE = "mode";
  static const char *XN_10_PARAMETER_PR_SIZE = "size";

  static const char *XN_10_RESULT = "result";

  static const char *XN_10_COLUMN = "column";
  static const char *XN_10_COLUMN_PR_NAME = "name";
  static const char *XN_10_COLUMN_PR_INDEX = "index";
  static const char *XN_10_COLUMN_PR_LENGTH = "length";
  static const char *XN_10_COLUMN_PR_TYPE = "type";

  static const char *XN_20_SQLS = "sqls";

  static const char *XN_20_USING_DB = "using-db";
  static const char *XN_20_USING_DB_PR_MODULE_ID = "module-id";
  static const char *XN_20_USING_DB_PR_DEFAULT = "default";

  static const char *XN_20_SELECT = "select";
  static const char *XN_20_INSERT = "insert";
  static const char *XN_20_UPDATE = "update";
  static const char *XN_20_DELETE = "delete";
  static const char *XN_20_PROCEDURE = "procedure";
  static const char *XN_20_SQL_PR_NAME = "name";
  static const char *XN_20_SQL_PR_DB = "db";

  static const char *XN_20_PARAM = "param";
  static const char *XN_20_PARAM_PR_NAME = "name";
  static const char *XN_20_PARAM_PR_MODE = "mode";
  static const char *XN_20_PARAM_PR_TYPE = "type";
  static const char *XN_20_PARAM_PR_SIZE = "size";

  static const char *XN_30_SQLS = "sqls";
  static const char *XN_30_SQLS_PR_GROUP_NAME = "groupName";
  static const char *XN_30_SQLS_PR_GROUP_NAME_HIDDEN = "group-name";

  static const char *XN_30_SELECT = "select";
  static const char *XN_30_INSERT = "insert";
  static const char *XN_30_UPDATE = "update";
  static const char *XN_30_DELETE = "delete";
  static const char *XN_30_PROCEDURE = "procedure";
  static const char *XN_30_SQL_PR_NAME = "name";

  static const char *XN_30_PARAM = "param";
  static const char *XN_30_PARAM_PR_NAME = "name";
  static const char *XN_30_PARAM_PR_TYPE = "type";
  static const char *XN_30_PARAM_PR_MODE = "mode";
  static const char *XN_30_PARAM_PR_SIZE = "size";

  static const char *XN_30_GROUP = "group";
  static const char *XN_30_GROUP_PR_NAME = "name";

  static const char *XN_30_CDATA = "cdata";

  static const char *XN_LOG = "log";
  static const char *XN_LOG_PR_LEVEL = "level";
  static const char *XN_LOG_PR_PATH = "path";
  static const char *XN_LOG_PR_FORCE_FLUSH = "forceFlush";
  static const char *XN_LOG_PR_FORCE_FLUSH_HIDDEN = "force-flush";
  static const char *XN_LOG_PR_POSTFIX = "postfix";

  static const char *XN_INCLUDE = "include";
  static const char *XN_INCLUDE_PR_FILE = "file";
  static const char *XN_INCLUDE_PR_PATH = "path";


  _XmlParser::_XmlParser(const std::string &fileName) :
    m_fileName(fileName), m_realFileName(fileName)
  {
  }

  void _XmlParser::setRealFileName(const std::string &realFileName)
  {
    m_realFileName = realFileName;
    doSetRealFileName();
  }

  const std::string &_XmlParser::getFileName() const
  {
    return m_fileName;
  }

  const std::string &_XmlParser::getRealFileName() const
  {
    return m_realFileName;
  }

  void _XmlParser::doSetRealFileName()
  {
  }

  _ExpatXMLParser::_ExpatXMLParser(const std::string &fileName) :
    m_fileName(fileName)
  {
    m_pParser = XML_ParserCreate(NULL);
    if (m_pParser == NULL)
      {
        CreateFailParserExeception e(fileName.c_str());
        DBGW_LOG_ERROR(e.what());
        throw e;
      }
  }

  _ExpatXMLParser::~_ExpatXMLParser()
  {
    if (m_pParser != NULL)
      {
        XML_ParserFree(m_pParser);
      }
  }

  XML_Parser _ExpatXMLParser::get() const
  {
    return m_pParser;
  }

  const char *_ExpatXMLParser::getFileName() const
  {
    return m_fileName.c_str();
  }

  _ExpatXMLProperties::_ExpatXMLProperties(
      const _ExpatXMLParser &xmlParser, const std::string &nodeName,
      const XML_Char *szAttr[]) :
    m_xmlParser(xmlParser), m_nodeName(nodeName), m_pAttr(szAttr)
  {
  }

  _ExpatXMLProperties::~_ExpatXMLProperties()
  {
  }

  std::string _ExpatXMLProperties::get(const char *szName, bool bRequired)
  {
    return get(szName, NULL, bRequired);
  }

  std::string _ExpatXMLProperties::get(const char *szName,
      const char *szHiddenName, bool bRequired)
  {
    std::string prop;

    for (int i = 0; m_pAttr[i] != NULL; i += 2)
      {
        if (szName != NULL && !strcmp(m_pAttr[i], szName))
          {
            return propertyFromEnv(m_pAttr[i + 1]);
          }
        else if (szHiddenName != NULL && !strcmp(m_pAttr[i], szHiddenName))
          {
            return propertyFromEnv(m_pAttr[i + 1]);
          }
      }

    if (bRequired)
      {
        NotExistPropertyException e(m_xmlParser.getFileName(),
            m_nodeName.c_str(), szName);
        DBGW_LOG_ERROR(e.what());
        throw e;
      }
    else
      {
        return "";
      }
  }

  int _ExpatXMLProperties::getInt(const char *szName, bool bRequired,
      int nDefault)
  {
    return getInt(szName, NULL, bRequired, nDefault);
  }

  int _ExpatXMLProperties::getInt(const char *szName,
      const char *szHiddenName, bool bRequired, int nDefault)
  {
    std::string prop;

    for (int i = 0; m_pAttr[i] != NULL; i += 2)
      {
        if (szName != NULL && !strcmp(m_pAttr[i], szName))
          {
            prop = propertyFromEnv(m_pAttr[i + 1]);
            return propertyToInt(prop.c_str());
          }
        else if (szHiddenName != NULL && !strcmp(m_pAttr[i], szHiddenName))
          {
            prop = propertyFromEnv(m_pAttr[i + 1]);
            return propertyToInt(prop.c_str());
          }
      }

    if (bRequired)
      {
        NotExistPropertyException e(m_xmlParser.getFileName(),
            m_nodeName.c_str(), szName);
        DBGW_LOG_ERROR(e.what());
        throw e;
      }
    else
      {
        return nDefault;
      }
  }

  long _ExpatXMLProperties::getLong(const char *szName, bool bRequired,
      unsigned long ulDefault)
  {
    return getLong(szName, NULL, bRequired, ulDefault);
  }

  long _ExpatXMLProperties::getLong(const char *szName,
      const char *szHiddenName, bool bRequired, unsigned long ulDefault)
  {
    std::string prop;

    for (int i = 0; m_pAttr[i] != NULL; i += 2)
      {
        if (szName != NULL && !strcmp(m_pAttr[i], szName))
          {
            prop = propertyFromEnv(m_pAttr[i + 1]);
            return propertyToLong(prop.c_str());
          }
        else if (szHiddenName != NULL && !strcmp(m_pAttr[i], szHiddenName))
          {
            prop = propertyFromEnv(m_pAttr[i + 1]);
            return propertyToLong(prop.c_str());
          }
      }

    if (bRequired)
      {
        NotExistPropertyException e(m_xmlParser.getFileName(),
            m_nodeName.c_str(), szName);
        DBGW_LOG_ERROR(e.what());
        throw e;
      }
    else
      {
        return ulDefault;
      }
  }

  bool _ExpatXMLProperties::getBool(const char *szName, bool bRequired)
  {
    return getBool(szName, NULL, bRequired);
  }

  bool _ExpatXMLProperties::getBool(const char *szName,
      const char *szHiddenName, bool bRequired)
  {
    std::string prop;

    for (int i = 0; m_pAttr[i] != NULL; i += 2)
      {
        if (szName != NULL && !strcmp(m_pAttr[i], szName))
          {
            prop = propertyFromEnv(m_pAttr[i + 1]);
            return propertyToBoolean(prop.c_str());
          }
        else if (szHiddenName != NULL && !strcmp(m_pAttr[i], szHiddenName))
          {
            prop = propertyFromEnv(m_pAttr[i + 1]);
            return propertyToBoolean(prop.c_str());
          }
      }

    if (bRequired)
      {
        NotExistPropertyException e(m_xmlParser.getFileName(),
            m_nodeName.c_str(), szName);
        DBGW_LOG_ERROR(e.what());
        throw e;
      }
    else
      {
        return false;
      }
  }

  void _ExpatXMLProperties::getValidateResult(const char *szName,
      const char *szHiddenName, bool bValidateResult[])
  {
    memset(bValidateResult, 0, sql::DBGW_STMT_TYPE_SIZE);

    std::string validateResult = get(szName, szHiddenName, false);
    if (validateResult == "")
      {
        return;
      }

    std::vector<std::string> queryTypeList;
    boost::split(queryTypeList, validateResult, boost::is_any_of(","));
    for (std::vector<std::string>::iterator it = queryTypeList.begin(); it
        != queryTypeList.end(); it++)
      {
        boost::trim(*it);

        if (!strcasecmp(it->c_str(), "select"))
          {
            bValidateResult[sql::DBGW_STMT_TYPE_SELECT] = true;
          }
        else if (!strcasecmp(it->c_str(), "update"))
          {
            bValidateResult[sql::DBGW_STMT_TYPE_UPDATE] = true;
          }
        else if (!strcasecmp(it->c_str(), "procedure"))
          {
            bValidateResult[sql::DBGW_STMT_TYPE_PROCEDURE] = true;
          }
        else
          {
            InvalidPropertyValueException e(m_xmlParser.getFileName(),
                validateResult.c_str(), "SELECT|PROCEDURE|UPDATE");
            DBGW_LOG_ERROR(e.what());
            throw e;
          }
      }
  }

  ValueType _ExpatXMLProperties::get30ValueType(const char *szName)
  {
    std::string type = get(szName, true);

    if (!strcasecmp(type.c_str(), "string"))
      {
        return DBGW_VAL_TYPE_STRING;
      }
    if (!strcasecmp(type.c_str(), "char"))
      {
        return DBGW_VAL_TYPE_CHAR;
      }
    if (!strcasecmp(type.c_str(), "int"))
      {
        return DBGW_VAL_TYPE_INT;
      }
    if (!strcasecmp(type.c_str(), "long")
        || !strcasecmp(type.c_str(), "int64"))
      {
        return DBGW_VAL_TYPE_LONG;
      }
    if (!strcasecmp(type.c_str(), "float"))
      {
        return DBGW_VAL_TYPE_FLOAT;
      }
    if (!strcasecmp(type.c_str(), "double"))
      {
        return DBGW_VAL_TYPE_DOUBLE;
      }
    if (!strcasecmp(type.c_str(), "date"))
      {
        return DBGW_VAL_TYPE_DATE;
      }
    if (!strcasecmp(type.c_str(), "time"))
      {
        return DBGW_VAL_TYPE_TIME;
      }
    if (!strcasecmp(type.c_str(), "datetime"))
      {
        return DBGW_VAL_TYPE_DATETIME;
      }
    if (!strcasecmp(type.c_str(), "bytes"))
      {
        return DBGW_VAL_TYPE_BYTES;
      }
    if (!strcasecmp(type.c_str(), "clob"))
      {
        return DBGW_VAL_TYPE_CLOB;
      }
    if (!strcasecmp(type.c_str(), "blob"))
      {
        return DBGW_VAL_TYPE_BLOB;
      }
    if (!strcasecmp(type.c_str(), "resultset"))
      {
        return DBGW_VAL_TYPE_RESULTSET;
      }

    InvalidValueTypeException e(m_xmlParser.getFileName(), type.c_str());
    DBGW_LOG_ERROR(e.what());
    throw e;
  }

  ValueType _ExpatXMLProperties::get20ValueType(const char *szName)
  {
    std::string type = get(szName, true);

    if (!strcasecmp(type.c_str(), "string"))
      {
        return DBGW_VAL_TYPE_STRING;
      }
    if (!strcasecmp(type.c_str(), "char"))
      {
        return DBGW_VAL_TYPE_CHAR;
      }
    if (!strcasecmp(type.c_str(), "int"))
      {
        return DBGW_VAL_TYPE_INT;
      }
    if (!strcasecmp(type.c_str(), "long"))
      {
        return DBGW_VAL_TYPE_LONG;
      }
    if (!strcasecmp(type.c_str(), "double"))
      {
        return DBGW_VAL_TYPE_DOUBLE;
      }
    if (!strcasecmp(type.c_str(), "data"))
      {
        return DBGW_VAL_TYPE_DATETIME;
      }

    InvalidValueTypeException e(m_xmlParser.getFileName(), type.c_str());
    DBGW_LOG_ERROR(e.what());
    throw e;
  }

  ValueType _ExpatXMLProperties::get10ValueType(const char *szName)
  {
    std::string type = get(szName, true);

    if (!strcasecmp(type.c_str(), "string"))
      {
        return DBGW_VAL_TYPE_STRING;
      }
    if (!strcasecmp(type.c_str(), "char"))
      {
        return DBGW_VAL_TYPE_CHAR;
      }
    if (!strcasecmp(type.c_str(), "int"))
      {
        return DBGW_VAL_TYPE_INT;
      }
    if (!strcasecmp(type.c_str(), "int64"))
      {
        return DBGW_VAL_TYPE_LONG;
      }
    if (!strcasecmp(type.c_str(), "float"))
      {
        return DBGW_VAL_TYPE_FLOAT;
      }
    if (!strcasecmp(type.c_str(), "double"))
      {
        return DBGW_VAL_TYPE_DOUBLE;
      }

    InvalidValueTypeException e(m_xmlParser.getFileName(), type.c_str());
    DBGW_LOG_ERROR(e.what());
    throw e;
  }

  CCI_LOG_LEVEL _ExpatXMLProperties::getLogLevel(const char *szName)
  {
    std::string logLevel = get(szName, false);

    if (!strcasecmp(logLevel.c_str(), "off"))
      {
        return CCI_LOG_LEVEL_OFF;
      }
    if (!strcasecmp(logLevel.c_str(), "error"))
      {
        return CCI_LOG_LEVEL_ERROR;
      }
    if (!strcasecmp(logLevel.c_str(), "warning"))
      {
        return CCI_LOG_LEVEL_WARN;
      }
    if (!strcasecmp(logLevel.c_str(), "info") || logLevel == "")
      {
        return CCI_LOG_LEVEL_INFO;
      }
    if (!strcasecmp(logLevel.c_str(), "debug"))
      {
        return CCI_LOG_LEVEL_DEBUG;
      }

    InvalidPropertyValueException e(m_xmlParser.getFileName(), logLevel.c_str(),
        "OFF|ERROR|WARNING|INFO|DEBUG");
    DBGW_LOG_ERROR(e.what());
    throw e;
  }

  CCI_LOG_POSTFIX _ExpatXMLProperties::getLogPostfix(const char *szName)
  {
    std::string logPostfix = get(szName, false);

    if (!strcasecmp(logPostfix.c_str(), "none") || logPostfix == "")
      {
        return CCI_LOG_POSTFIX_NONE;
      }
    if (!strcasecmp(logPostfix.c_str(), "date"))
      {
        return CCI_LOG_POSTFIX_DATE;
      }

    InvalidPropertyValueException e(m_xmlParser.getFileName(), logPostfix.c_str(),
        "NONE|PID|DATE");
    DBGW_LOG_ERROR(e.what());
    throw e;
  }

  sql::ParameterMode _ExpatXMLProperties::getBindMode(const char *szName)
  {
    std::string mode = get(szName, false);

    if (!strcasecmp(mode.c_str(), "in") || mode == "")
      {
        return sql::DBGW_PARAM_MODE_IN;
      }
    else if (!strcasecmp(mode.c_str(), "out"))
      {
        return sql::DBGW_PARAM_MODE_OUT;
      }
    else if (!strcasecmp(mode.c_str(), "inout"))
      {
        return sql::DBGW_PARAM_MODE_INOUT;
      }
    else
      {
        InvalidPropertyValueException e(m_xmlParser.getFileName(), mode.c_str(),
            "IN|OUT|INOUT");
        DBGW_LOG_ERROR(e.what());
        throw e;
      }
  }

  sql::DataBaseType _ExpatXMLProperties::getDataBaseType(const char *szName)
  {
    std::string dbType = get(szName, false);

    if (dbType == "")
      {
#if defined(DBGW_ALL)
        return sql::DBGW_DB_TYPE_CUBRID;
#elif defined(DBGW_ORACLE)
        return sql::DBGW_DB_TYPE_ORACLE;
#elif defined(DBGW_MYSQL)
        return sql::DBGW_DB_TYPE_MYSQL;
#else
        return sql::DBGW_DB_TYPE_CUBRID;
#endif
      }
    else if (!strcasecmp(dbType.c_str(), "cubrid"))
      {
        return sql::DBGW_DB_TYPE_CUBRID;
      }
    else if (!strcasecmp(dbType.c_str(), "mysql"))
      {
        return sql::DBGW_DB_TYPE_MYSQL;
      }
    else if (!strcasecmp(dbType.c_str(), "oracle"))
      {
        return sql::DBGW_DB_TYPE_ORACLE;
      }
    else
      {
        InvalidPropertyValueException e(m_xmlParser.getFileName(), dbType.c_str(),
            "CUBRID|MYSQL|ORACLE");
        DBGW_LOG_ERROR(e.what());
        throw e;
      }
  }

  const char *_ExpatXMLProperties::getNodeName() const
  {
    return m_nodeName.c_str();
  }

  std::string _ExpatXMLProperties::propertyFromEnv(const char *szValue)
  {
    if (szValue == NULL || szValue == "")
      {
        return "";
      }

    std::stringstream prop;
    const char *p = szValue, *s = NULL;
    char *szKeyBuffer = new char[strlen(szValue) + 1];

    /* ex) abcd${efg}hij */
    while (*p != '\0')
      {
        if (*p == '$' && *(p + 1) == '{')
          {
            /* ex) abcd${efg}hij */
            /*           s       */
            /*         p         */
            s = p + 2;
            p += 2;
          }
        else if (s != NULL && *p == '}')
          {
            /* ex) abcd${efg}hij */
            /*           s       */
            /*              p    */

            if (p - s > 0)
              {
                memcpy(szKeyBuffer, s, p - s);
                szKeyBuffer[p - s] = '\0';
                prop << getenv(szKeyBuffer);
              }

            p += 1;
            s = NULL;
          }
        else
          {
            if (s == NULL)
              {
                prop << *p;
              }
            p += 1;
          }
      }

    if (szKeyBuffer != NULL)
      {
        delete[] szKeyBuffer;
      }

    return prop.str();
  }

  int _ExpatXMLProperties::propertyToInt(const char *szProperty)
  {
    try
      {
        return boost::lexical_cast<int>(szProperty);
      }
    catch (boost::bad_lexical_cast &)
      {
        InvalidPropertyValueException e(m_xmlParser.getFileName(), szProperty,
            "NUMERIC");
        DBGW_LOG_ERROR(e.what());
        throw e;
      }
  }

  long _ExpatXMLProperties::propertyToLong(const char *szProperty)
  {
    try
      {
        return boost::lexical_cast<long>(szProperty);
      }
    catch (boost::bad_lexical_cast &)
      {
        InvalidPropertyValueException e(m_xmlParser.getFileName(), szProperty,
            "NUMERIC");
        DBGW_LOG_ERROR(e.what());
        throw e;
      }
  }

  bool _ExpatXMLProperties::propertyToBoolean(const char *szProperty)
  {
    if (szProperty == NULL || !strcasecmp(szProperty, "false"))
      {
        return false;
      }
    else if (!strcasecmp(szProperty, "true"))
      {
        return true;
      }
    else
      {
        InvalidPropertyValueException e(m_xmlParser.getFileName(), szProperty,
            "TRUE|FALSE");
        DBGW_LOG_ERROR(e.what());
        throw e;
      }
  }

  _BaseXmlParser::_BaseXmlParser(const std::string &fileName) :
    _XmlParser(fileName), m_bCdataSection(false),
    m_bAborted(false)
  {
  }

  _BaseXmlParser::~_BaseXmlParser()
  {
  }

  const std::string &_BaseXmlParser::getParentElementName() const
  {
    return m_elementStack.top();
  }

  bool _BaseXmlParser::isRootElement() const
  {
    return m_elementStack.empty();
  }

  bool _BaseXmlParser::isCdataSection() const
  {
    return m_bCdataSection;
  }

  bool _BaseXmlParser::isAborted() const
  {
    return m_bAborted;
  }

  void _BaseXmlParser::parse()
  {
    _ExpatXMLParser parser(getRealFileName());

    XML_SetUserData(parser.get(), this);
    XML_SetElementHandler(parser.get(), onElementStart, onElementEnd);
    XML_SetCdataSectionHandler(parser.get(), onCdataStart, onCdataEnd);
    XML_SetCharacterDataHandler(parser.get(), onElementContent);
    XML_SetUnknownEncodingHandler(parser.get(), onUnknownEncoding, NULL);

    FILE *fp = fopen(getRealFileName().c_str(), "r");
    if (fp == NULL)
      {
        CreateFailParserExeception e(getRealFileName().c_str());
        DBGW_LOGF_ERROR("%s (%d)", e.what(), errno);
        throw e;
      }

    Exception exception;
    char buffer[XML_FILE_BUFFER_SIZE];
    enum XML_Status status = XML_STATUS_OK;
    enum XML_Error errCode;
    int nReadLen;

    do
      {
        nReadLen = fread(buffer, 1, XML_FILE_BUFFER_SIZE, fp);
        try
          {
            status = XML_Parse(parser.get(), buffer, nReadLen, 0);
          }
        catch (Exception &e)
          {
            exception = e;
            break;
          }

        if (isAborted())
          {
            break;
          }
      }
    while (nReadLen > 0 && status != XML_STATUS_ERROR);

    fclose(fp);

    if (exception.getErrorCode() != DBGW_ER_NO_ERROR)
      {
        throw exception;
      }
    else if (status == XML_STATUS_ERROR)
      {
        errCode = XML_GetErrorCode(parser.get());
        const char *szErrMsg = XML_ErrorString(errCode);
        if (errCode == XML_ERROR_NONE || szErrMsg == NULL)
          {
            FailedToParseXmlException e(getRealFileName().c_str());
            DBGW_LOG_ERROR(e.what());
            throw e;
          }
        else
          {
            InvalidXMLSyntaxException e(szErrMsg,
                getRealFileName().c_str(),
                XML_GetCurrentLineNumber(parser.get()),
                XML_GetCurrentColumnNumber(parser.get()));
            DBGW_LOG_ERROR(e.what());
            throw e;
          }
      }
  }

  void _BaseXmlParser::doOnElementContent(const XML_Char *szData, int nLength)
  {
  }

  void _BaseXmlParser::doOnCdataStart()
  {
  }

  void _BaseXmlParser::doOnCdataEnd()
  {
  }

  void _BaseXmlParser::abort()
  {
    m_bAborted = true;
  }

  void _BaseXmlParser::onElementStart(void *pParam, const XML_Char *szName,
      const XML_Char *szAttr[])
  {
    _BaseXmlParser *pParser = (_BaseXmlParser *) pParam;
    _ExpatXMLProperties properties(pParser->getFileName(), szName, szAttr);

    pParser->doOnElementStart(szName, properties);

    std::string name = szName;
    boost::algorithm::to_lower(name);
    pParser->m_elementStack.push(name);
  }

  void _BaseXmlParser::onElementEnd(void *pParam, const XML_Char *szName)
  {
    _BaseXmlParser *pParser = (_BaseXmlParser *) pParam;

    pParser->m_elementStack.pop();

    pParser->doOnElementEnd(szName);
  }

  void _BaseXmlParser::onElementContent(void *pParam, const XML_Char *szData,
      int nLength)
  {
    _BaseXmlParser *pParser = (_BaseXmlParser *) pParam;

    pParser->doOnElementContent(szData, nLength);
  }

  void _BaseXmlParser::onCdataStart(void *pParam)
  {
    _BaseXmlParser *pParser = (_BaseXmlParser *) pParam;

    pParser->m_bCdataSection = true;
    pParser->doOnCdataStart();
  }

  void _BaseXmlParser::onCdataEnd(void *pParam)
  {
    _BaseXmlParser *pParser = (_BaseXmlParser *) pParam;

    pParser->doOnCdataEnd();
    pParser->m_bCdataSection = false;
  }

  int _BaseXmlParser::onUnknownEncoding(void *pParam, const XML_Char *szName,
      XML_Encoding *pInfo)
  {
    pInfo->data = NULL;
    pInfo->convert = convertCharacterSet;
    pInfo->release = NULL;

    int i = 0;
    for (; i < 0x80; ++i)
      {
        pInfo->map[i] = i;
      }
    for (; i <= 0xFF; ++i)
      {
        pInfo->map[i] = -2;
      }
    return 1;
  }

  int _BaseXmlParser::convertCharacterSet(void *pParam, const char *szStr)
  {
    return *szStr;
  }

  class _ConnectorParser::Impl
  {
  public:
    Impl(_ConnectorParser *pSelf, const std::string &fileName,
        _Connector *pConnector) :
      m_pSelf(pSelf), m_pConnector(pConnector)
    {
    }

    ~Impl()
    {
    }

    void doOnElementStart(const XML_Char *szName,
        _ExpatXMLProperties &properties)
    {
      if (!strcasecmp(szName, XN_SERVICE))
        {
          parseService(properties);
        }
      else if (!strcasecmp(szName, XN_GROUP))
        {
          parseGroup(properties);
        }
      else if (!strcasecmp(szName, XN_POOL))
        {
          parsePool(properties);
        }
      else if (!strcasecmp(szName, XN_DBINFO))
        {
          parseDBInfo(properties);
        }
      else if (!strcasecmp(szName, XN_HOST))
        {
          parseHost(properties);
        }
      else if (!strcasecmp(szName, XN_ALTHOSTS))
        {
          parseAltHost(properties);
        }
    }

    void doOnElementEnd(const XML_Char *szName)
    {
      if (!strcasecmp(szName, XN_SERVICE))
        {
          if (m_pService->empty())
            {
              NotExistNodeInXmlException e(XN_GROUP, m_pSelf->getFileName().c_str());
              DBGW_LOG_ERROR(e.what());
              throw e;
            }
          m_pService->initGroup(m_context);
          m_pService->start();

          m_pService = trait<_Service>::sp();
          m_context = _ExecutorPoolContext();
        }
      else if (!strcasecmp(szName, XN_GROUP))
        {
          if (m_pGroup->empty())
            {
              NotExistNodeInXmlException e(XN_HOST, m_pSelf->getFileName().c_str());
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          m_pGroup = trait<_Group>::sp();
          m_url = "";
          m_dbinfo = "";
        }
      else if (!strcasecmp(szName, XN_HOST))
        {
          m_pHost = trait<_Host>::sp();
        }
    }

  private:
    void parseService(_ExpatXMLProperties &properties)
    {
      if (!m_pSelf->isRootElement())
        {
          return;
        }

      bool bValidateResult[sql::DBGW_STMT_TYPE_SIZE];
      properties.getValidateResult(XN_SERVICE_PR_VALIDATE_RESULT,
          XN_SERVICE_PR_VALIDATE_RESULT_HIDDEN, bValidateResult);

      m_pService = trait<_Service>::sp(
          new _Service(*m_pConnector, m_pSelf->getFileName(),
              properties.get(XN_SERVICE_PR_NAMESPACE, true),
              properties.get(XN_SERVICE_PR_DESCRIPTION, false), bValidateResult,
              properties.getInt(XN_SERVICE_PR_VALIDATE_RATIO,
                  XN_SERVICE_PR_VALIDATE_RATIO_HIDDEN, false, 0)));

      m_pConnector->addService(m_pService);
    }

    void parseGroup(_ExpatXMLProperties &properties)
    {
      if (m_pSelf->getParentElementName() != XN_SERVICE)
        {
          return;
        }

      CodePage dbCodePage = DBGW_IDENTITY;
      CodePage clientCodePage = DBGW_IDENTITY;

      std::string dbCharset =
          properties.get(XN_GROUP_PR_DB_CHARSET, false);
      if (dbCharset != "")
        {
          dbCodePage = stringToCodepage(dbCharset);
        }

      std::string clientCharset =
          properties.get(XN_GROUP_PR_CLIENT_CHARSET, false);
      if (clientCharset != "")
        {
          clientCodePage = stringToCodepage(clientCharset);
        }

      if ((clientCharset != "" && dbCharset == "")
          || (clientCharset == "" && dbCharset != ""))
        {
          dbgw::NotExistCharsetException e;
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      sql::DataBaseType dbType = properties.getDataBaseType(XN_GROUP_PR_DBTYPE);
#if !defined(DBGW_ALL)
#if defined(DBGW_MYSQL)
      if (dbType != sql::DBGW_DB_TYPE_MYSQL)
        {
          InvalidDbTypeException e(getDbTypeString(dbType));
          DBGW_LOG_ERROR(e.what());
          throw e;
        }
#elif defined(DBGW_ORACLE)
      if (dbType != sql::DBGW_DB_TYPE_ORACLE)
        {
          InvalidDbTypeException e(getDbTypeString(dbType));
          DBGW_LOG_ERROR(e.what());
          throw e;
        }
#else
      if (dbType != sql::DBGW_DB_TYPE_CUBRID)
        {
          InvalidDbTypeException e(getDbTypeString(dbType));
          DBGW_LOG_ERROR(e.what());
          throw e;
        }
#endif /* DBGW_ORACLE */
#endif /* DBGW_ALL */

      m_pGroup = trait<_Group>::sp(
          new _Group(m_pService.get(), m_pSelf->getFileName(),
              properties.get(XN_GROUP_PR_NAME, true),
              properties.get(XN_GROUP_PR_DESCRIPTION, false),
              properties.getBool(XN_GROUP_PR_INACTIVATE, false),
              properties.getBool(XN_GROUP_PR_IGNORE_RESULT,
                  XN_GROUP_PR_IGNORE_RESULT_HIDDEN, false),
              properties.getBool(
                  XN_GROUP_PR_USE_DEFAULT_VALUE_WHEN_FAILED_TO_CAST_PARAM,
                  false),
              properties.getInt(XN_GROUP_PR_MAX_PREPARED_STATEMENT_SIZE, false,
                  _Executor::DEFAULT_MAX_PREPARED_STATEMENT_SIZE()), dbCodePage,
              clientCodePage, dbType));
      m_pService->addGroup(m_pGroup);
    }

    void parsePool(_ExpatXMLProperties &properties)
    {
      if (m_pSelf->getParentElementName() != XN_SERVICE)
        {
          return;
        }

      m_context.initialSize = properties.getInt(XN_POOL_PR_INIT_SIZE,
          XN_POOL_PR_INIT_SIZE_HIDDEN, false,
          _ExecutorPoolContext::DEFAULT_INITIAL_SIZE());
      m_context.minIdle = properties.getInt(XN_POOL_PR_MIN_IDLE, false,
          _ExecutorPoolContext::DEFAULT_MIN_IDLE());
      m_context.maxIdle = properties.getInt(XN_POOL_PR_MAX_IDLE, false,
          _ExecutorPoolContext::DEFAULT_MAX_IDLE());
      m_context.maxActive = properties.getInt(XN_POOL_PR_MAX_ACTIVE, false,
          _ExecutorPoolContext::DEFAULT_MAX_ACTIVE());
      m_context.timeBetweenEvictionRunsMillis = properties.getLong(
          XN_POOL_PR_TIME_BETWEEN_EVICTION_RUNS_MILLIS, false,
          _ExecutorPoolContext::DEFAULT_TIME_BETWEEN_EVICTION_RUNS_MILLIS());
      m_context.numTestsPerEvictionRun = properties.getInt(
          XN_POOL_PR_NUM_TESTS_PER_EVICTIONRUN, false,
          _ExecutorPoolContext::DEFAULT_NUM_TESTS_PER_EVICTIONRUN());
      m_context.minEvictableIdleTimeMillis = properties.getLong(
          XN_POOL_PR_MIN_EVICTABLE_IDLE_TIMEMILLIS, false,
          _ExecutorPoolContext::DEFAULT_MIN_EVICTABLE_IDLE_TIMEMILLIS());
    }

    void parseDBInfo(_ExpatXMLProperties &properties)
    {
      if (m_pSelf->getParentElementName() != XN_GROUP)
        {
          return;
        }

      m_dbinfo = properties.get(XN_DBINFO_PR_DBNAME, true);
      m_dbinfo += ":";
      m_dbinfo += properties.get(XN_DBINFO_PR_DBUSER, true);
      m_dbinfo += ":";
      m_dbinfo += properties.get(XN_DBINFO_PR_DBPASSWD, true);
      m_dbinfo += ":";
    }

    void parseHost(_ExpatXMLProperties &properties)
    {
      if (m_pSelf->getParentElementName() != XN_GROUP)
        {
          return;
        }

      m_url = properties.get(XN_HOST_PR_URL, false);
      if (m_url == "")
        {
          if (m_dbinfo == "")
            {
              NotExistNodeInXmlException e(XN_DBINFO,
                  m_pSelf->getFileName().c_str());
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          m_url = "cci:CUBRID:";
          m_url += properties.get(XN_HOST_PR_ADDRESS, true);
          m_url += ":";
          m_url += properties.get(XN_HOST_PR_PORT, true);
          m_url += ":";

          m_url = m_url + m_dbinfo;
        }

      std::string user = properties.get(XN_HOST_PR_USER, false);
      std::string pasword = properties.get(XN_HOST_PR_PASSWORD, false);

      int nWeight = properties.getInt(XN_HOST_PR_WEIGHT, true, 1);
      if (nWeight <= 0)
        {
          InvalidHostWeightException e;
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      m_pHost = trait<_Host>::sp(
          new _Host(m_url, user, pasword, nWeight));
      m_pGroup->addHost(m_pHost);
    }

    void parseAltHost(_ExpatXMLProperties &properties)
    {
      if (m_pSelf->getParentElementName() != XN_HOST)
        {
          return;
        }

      if (m_dbinfo == "")
        {
          /**
           * we don't need to use althost because the url is given by user.
           */
          return;
        }

      m_pHost->setAltHost(properties.get(XN_HOST_PR_ADDRESS, true),
          properties.get(XN_HOST_PR_PORT, true));
    }

  private:
    _ConnectorParser *m_pSelf;
    _Connector *m_pConnector;
    trait<_Service>::sp m_pService;
    trait<_Group>::sp m_pGroup;
    trait<_Host>::sp m_pHost;
    _ExecutorPoolContext m_context;
    std::string m_url;
    std::string m_dbinfo;
  };

  _ConnectorParser::_ConnectorParser(const std::string &fileName,
      _Connector *pConnector) :
    _BaseXmlParser(fileName), m_pImpl(new Impl(this, fileName, pConnector))
  {
  }

  _ConnectorParser::~_ConnectorParser()
  {
    if (m_pImpl != NULL)
      {
        delete m_pImpl;
      }
  }

  void _ConnectorParser::doOnElementStart(const XML_Char *szName,
      _ExpatXMLProperties &properties)
  {
    m_pImpl->doOnElementStart(szName, properties);
  }

  void _ConnectorParser::doOnElementEnd(const XML_Char *szName)
  {
    m_pImpl->doOnElementEnd(szName);
  }

  class _QueryMapContext::Impl
  {
  public:
    Impl(const std::string &fileName, _QueryMapper *pQueryMapper) :
      m_fileName(fileName), m_pQueryMapper(pQueryMapper), m_globalGroupName(""),
      m_localGroupName(""), m_sqlName(""),
      m_statementType(sql::DBGW_STMT_TYPE_UNDEFINED), m_bExistQueryInSql(false),
      m_bExistQueryInGroup(false)
    {
    }

    ~Impl()
    {
    }

    void clear()
    {
      m_globalGroupName = "";
      m_localGroupName = "";
      m_sqlName = "";
      m_statementType = sql::DBGW_STMT_TYPE_UNDEFINED;
      m_paramIndexList.clear();
      m_queryParamList.clear();
      m_queryBuffer.str("");
      m_queryBuffer.str("");
      m_resultIndexList.clear();
      m_userDefinedMetaList.clear();
      m_bExistQueryInSql = false;
      m_bExistQueryInGroup = false;
    }

    void checkAndClearSql()
    {
      if (m_bExistQueryInSql == false)
        {
          NotExistNodeInXmlException e(XN_30_CDATA, m_fileName.c_str());
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      m_localGroupName = "";
      m_sqlName = "";
      m_statementType = sql::DBGW_STMT_TYPE_UNDEFINED;
      m_paramIndexList.clear();
      m_queryParamList.clear();
      m_queryBuffer.str("");
      m_queryBuffer.str("");
      m_resultIndexList.clear();
      m_userDefinedMetaList.clear();
      m_bExistQueryInSql = false;
    }

    void checkAndClearGroup()
    {
      if (m_bExistQueryInGroup == false)
        {
          NotExistNodeInXmlException e(XN_30_CDATA, m_fileName.c_str());
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      m_localGroupName = "";
      m_queryBuffer.str("");
      m_queryBuffer.str("");
      m_bExistQueryInGroup = false;
    }

    void setFileName(const std::string &fileName)
    {
      m_fileName = fileName;
    }

    void setGlobalGroupName(const std::string &globalGroupName)
    {
      m_globalGroupName = globalGroupName;
    }

    void setLocalGroupName(const std::string &localGroupName)
    {
      m_localGroupName = localGroupName;
    }

    void setStatementType(sql::StatementType statementType)
    {
      m_statementType = statementType;
    }

    void setSqlName(const std::string &sqlName)
    {
      m_sqlName = sqlName;
    }

    void setParameter(const std::string &name, int nIndex,
        ValueType valueType, sql::ParameterMode mode, int nSize)
    {
      _QueryParameter stParam;
      if (name == "")
        {
          stParam.name = makeImplicitParamName(nIndex - 1);
        }
      else
        {
          stParam.name = name;
        }

      if (nIndex < 1)
        {
          InvalidParamIndexException e(m_fileName.c_str(), m_sqlName.c_str(),
              nIndex);
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      if (m_paramIndexList.insert(nIndex - 1).second == false)
        {
          DuplicateParamIndexException e(m_fileName.c_str(), m_sqlName.c_str(),
              nIndex);
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      stParam.type = valueType;
      stParam.mode = mode;
      stParam.index = nIndex - 1;
      stParam.firstPlaceHolderIndex = -1;
      stParam.size = nSize;
      m_queryParamList.push_back(stParam);
    }

    void setParameter(const std::string &name, ValueType valueType,
        sql::ParameterMode mode, int nSize)
    {
      if (isImplicitParamName(name.c_str()))
        {
          throw getLastException();
        }

      _QueryParameter stParam;
      stParam.name = name;
      stParam.type = valueType;
      stParam.mode = mode;
      stParam.index = m_queryParamList.size();
      stParam.firstPlaceHolderIndex = -1;
      stParam.size = nSize;
      m_queryParamList.push_back(stParam);
    }

    void setResult(const std::string &name, size_t nIndex,
        ValueType valueType, int nLength)
    {
      if (nIndex < 1)
        {
          InvalidResultIndexException e(m_fileName.c_str(), m_sqlName.c_str(),
              nIndex);
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      if (m_resultIndexList.insert(nIndex - 1).second == false)
        {
          DuplicateResultIndexException e(m_fileName.c_str(), m_sqlName.c_str(),
              nIndex);
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      _ClientResultSetMetaDataRaw metaData(name.c_str(), valueType);

      if (m_userDefinedMetaList.size() < nIndex)
        {
          m_userDefinedMetaList.resize(nIndex);
        }

      m_userDefinedMetaList[nIndex - 1] = metaData;
    }

    void appendQueryString(const char *szQueryString)
    {
      m_queryBuffer << szQueryString;
    }

    void addQuery()
    {
      std::string groupName = m_localGroupName;
      if (groupName == "")
        {
          groupName = m_globalGroupName;
        }

      if (groupName == "")
        {
          NotExistNodeInXmlException e(XN_30_GROUP, m_fileName.c_str());
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      std::string queryString = m_queryBuffer.str().substr(0,
          m_queryBuffer.tellp());

      if (m_pQueryMapper->getVersion() == DBGW_QUERY_MAP_VER_10
          && m_statementType == sql::DBGW_STMT_TYPE_UNDEFINED)
        {
          /**
           * in dbgw 1.0, we have to get query type to parse query std::string.
           */
          m_statementType = getQueryType(queryString.c_str());
        }

      trait<ClientResultSetMetaData>::sp pUserDefinedResultSetMetaData;
      if (m_userDefinedMetaList.size() > 0)
        {
          pUserDefinedResultSetMetaData = trait<ClientResultSetMetaData>::sp(
              new ClientResultSetMetaDataImpl(m_userDefinedMetaList));
        }

      std::vector<std::string> groupNameList;
      boost::split(groupNameList, groupName, boost::is_any_of(","));
      for (std::vector<std::string>::iterator it = groupNameList.begin(); it
          != groupNameList.end(); it++)
        {
          boost::trim(*it);
          trait<_Query>::sp p(new _Query(m_pQueryMapper->getMonitor(),
              m_pQueryMapper->getVersion(), m_fileName, queryString, m_sqlName,
              *it, m_statementType, m_queryParamList,
              pUserDefinedResultSetMetaData));
          m_pQueryMapper->addQuery(m_sqlName, p);
        }

      m_bExistQueryInGroup = true;
      m_bExistQueryInSql = true;
    }

    const std::string &getGlobalGroupName() const
    {
      return m_globalGroupName;
    }

    bool isExistQueryString() const
    {
      return !(std::stringstream::traits_type::eq_int_type(
          m_queryBuffer.rdbuf()->sgetc(), std::stringstream::traits_type::eof()));
    }

  private:
    std::string m_fileName;
    _QueryMapper *m_pQueryMapper;
    std::string m_globalGroupName;
    std::string m_localGroupName;
    std::string m_sqlName;
    sql::StatementType m_statementType;
    std::set<int> m_paramIndexList;
    trait<_QueryParameter>::vector m_queryParamList;
    std::set<int> m_resultIndexList;
    std::stringstream m_queryBuffer;
    trait<_ClientResultSetMetaDataRaw>::vector m_userDefinedMetaList;
    bool m_bExistQueryInSql;
    bool m_bExistQueryInGroup;
  };

  _QueryMapContext::_QueryMapContext(const std::string &fileName,
      _QueryMapper *pQueryMapper) :
    m_pImpl(new Impl(fileName, pQueryMapper))
  {
  }

  _QueryMapContext::~_QueryMapContext()
  {
    if (m_pImpl != NULL)
      {
        delete m_pImpl;
      }
  }

  void _QueryMapContext::clear()
  {
    m_pImpl->clear();
  }

  void _QueryMapContext::checkAndClearSql()
  {
    m_pImpl->checkAndClearSql();
  }

  void _QueryMapContext::checkAndClearGroup()
  {
    m_pImpl->checkAndClearGroup();
  }

  void _QueryMapContext::setFileName(const std::string &fileName)
  {
    m_pImpl->setFileName(fileName);
  }

  void _QueryMapContext::setGlobalGroupName(const std::string &globalGroupName)
  {
    m_pImpl->setGlobalGroupName(globalGroupName);
  }

  void _QueryMapContext::setLocalGroupName(const std::string &localGroupName)
  {
    m_pImpl->setLocalGroupName(localGroupName);
  }

  void _QueryMapContext::setStatementType(sql::StatementType statementType)
  {
    m_pImpl->setStatementType(statementType);
  }

  void _QueryMapContext::setSqlName(const std::string &sqlName)
  {
    m_pImpl->setSqlName(sqlName);
  }

  void _QueryMapContext::setParameter(const std::string &name, int nIndex,
      ValueType valueType, sql::ParameterMode mode, int nSize)
  {
    m_pImpl->setParameter(name, nIndex, valueType, mode, nSize);
  }

  void _QueryMapContext::setParameter(const std::string &name,
      ValueType valueType, sql::ParameterMode mode, int nSize)
  {
    m_pImpl->setParameter(name, valueType, mode, nSize);
  }

  void _QueryMapContext::setResult(const std::string &name, size_t nIndex,
      ValueType valueType, int nLength)
  {
    m_pImpl->setResult(name, nIndex, valueType, nLength);
  }

  void _QueryMapContext::appendQueryString(const char *szQueryString)
  {
    m_pImpl->appendQueryString(szQueryString);
  }

  void _QueryMapContext::addQuery()
  {
    m_pImpl->addQuery();
  }

  const std::string &_QueryMapContext::getGlobalGroupName() const
  {
    return m_pImpl->getGlobalGroupName();
  }

  bool _QueryMapContext::isExistQueryString() const
  {
    return m_pImpl->isExistQueryString();
  }

  _10QueryMapParser::_10QueryMapParser(const std::string &fileName,
      _QueryMapContext &parserContext) :
    _BaseXmlParser(fileName), m_parserContext(parserContext)
  {
  }

  _10QueryMapParser::~_10QueryMapParser()
  {
  }

  void _10QueryMapParser::doOnElementStart(const XML_Char *szName,
      _ExpatXMLProperties &properties)
  {
    if (!strcasecmp(szName, XN_10_SQL))
      {
        parseSql(properties);
      }
    else if (!strcasecmp(szName, XN_10_PARAMETER))
      {
        parseParameter(properties);
      }
    else if (!strcasecmp(szName, XN_10_COLUMN))
      {
        parseColumn(properties);
      }
  }

  void _10QueryMapParser::doOnElementEnd(const XML_Char *szName)
  {
    if (!strcasecmp(szName, XN_10_DEFINEDQUERY))
      {
        m_parserContext.clear();
      }
    else if (!strcasecmp(szName, XN_10_SQL))
      {
        if (m_parserContext.isExistQueryString())
          {
            m_parserContext.addQuery();
          }

        m_parserContext.checkAndClearSql();
      }
  }

  void _10QueryMapParser::doOnElementContent(const XML_Char *szData, int nLength)
  {
    if (getParentElementName() != XN_10_QUERY)
      {
        return;
      }

    std::string data(szData);
    m_parserContext.appendQueryString(data.substr(0, nLength).c_str());
  }

  void _10QueryMapParser::doSetRealFileName()
  {
    m_parserContext.setFileName(getRealFileName());
  }

  void _10QueryMapParser::parseSql(_ExpatXMLProperties properties)
  {
    if (getParentElementName() != XN_10_DEFINEDQUERY)
      {
        return;
      }

    m_parserContext.setLocalGroupName("__FIRST__");
    m_parserContext.setSqlName(properties.get(XN_10_SQL_PR_NAME, true));

    std::string type = properties.get(XN_10_SQL_PR_TYPE, false);
    if (type != "")
      {
        if (!strcasecmp(type.c_str(), "selcet"))
          {
            m_parserContext.setStatementType(sql::DBGW_STMT_TYPE_SELECT);
          }
        else if (!strcasecmp(type.c_str(), "update")
            || !strcasecmp(type.c_str(), "delete")
            || !strcasecmp(type.c_str(), "insert"))
          {
            m_parserContext.setStatementType(sql::DBGW_STMT_TYPE_UPDATE);
          }
        else if (!strcasecmp(type.c_str(), "procedure"))
          {
            m_parserContext.setStatementType(sql::DBGW_STMT_TYPE_PROCEDURE);
          }
      }
  }

  void _10QueryMapParser::parseParameter(_ExpatXMLProperties properties)
  {
    if (getParentElementName() != XN_10_PLACEHOLDER)
      {
        return;
      }

    m_parserContext.setParameter(
        properties.get(XN_10_PARAMETER_PR_NAME, false),
        properties.getInt(XN_10_PARAMETER_PR_INDEX, true),
        properties.get10ValueType(XN_10_PARAMETER_PR_TYPE),
        properties.getBindMode(XN_10_PARAMETER_PR_MODE),
        properties.getInt(XN_10_PARAMETER_PR_SIZE, false));
  }

  void _10QueryMapParser::parseColumn(_ExpatXMLProperties properties)
  {
    if (getParentElementName() != XN_10_RESULT)
      {
        return;
      }

    m_parserContext.setResult(
        properties.get(XN_10_COLUMN_PR_NAME, true),
        properties.getInt(XN_10_COLUMN_PR_INDEX, true),
        properties.get10ValueType(XN_10_COLUMN_PR_TYPE),
        properties.getInt(XN_10_COLUMN_PR_LENGTH, false));
  }

  _20QueryMapParser::_20QueryMapParser(const std::string &fileName,
      _QueryMapContext &parserContext) :
    _BaseXmlParser(fileName), m_parserContext(parserContext)
  {
  }

  _20QueryMapParser::~_20QueryMapParser()
  {
  }

  void _20QueryMapParser::doOnElementStart(const XML_Char *szName,
      _ExpatXMLProperties &properties)
  {
    if (!strcasecmp(szName, XN_20_USING_DB))
      {
        parseUsingDB(properties);
      }
    else if (!strcasecmp(szName, XN_20_SELECT))
      {
        m_parserContext.setStatementType(sql::DBGW_STMT_TYPE_SELECT);
        parseSql(properties);
      }
    else if (!strcasecmp(szName, XN_20_PROCEDURE))
      {
        m_parserContext.setStatementType(sql::DBGW_STMT_TYPE_PROCEDURE);
        parseSql(properties);
      }
    else if (!strcasecmp(szName, XN_20_INSERT)
        || !strcasecmp(szName, XN_20_UPDATE)
        || !strcasecmp(szName, XN_20_DELETE))
      {
        m_parserContext.setStatementType(sql::DBGW_STMT_TYPE_UPDATE);
        parseSql(properties);
      }
    else if (!strcasecmp(szName, XN_20_PARAM))
      {
        parseParameter(properties);
      }
  }

  void _20QueryMapParser::doOnElementEnd(const XML_Char *szName)
  {
    if (!strcasecmp(szName, XN_20_SQLS))
      {
        m_parserContext.clear();
      }
    else if (!strcasecmp(szName, XN_20_SELECT)
        || !strcasecmp(szName, XN_20_INSERT)
        || !strcasecmp(szName, XN_20_UPDATE)
        || !strcasecmp(szName, XN_20_DELETE)
        || !strcasecmp(szName, XN_20_PROCEDURE))
      {
        m_parserContext.checkAndClearSql();
      }
  }

  void _20QueryMapParser::doOnElementContent(const XML_Char *szData, int nLength)
  {
    if (isCdataSection() == false)
      {
        return;
      }

    if (getParentElementName() != XN_20_SELECT
        && getParentElementName() != XN_20_INSERT
        && getParentElementName() != XN_20_UPDATE
        && getParentElementName() != XN_20_DELETE
        && getParentElementName() != XN_20_PROCEDURE)
      {
        return;
      }

    std::string data(szData);
    m_parserContext.appendQueryString(data.substr(0, nLength).c_str());
  }

  void _20QueryMapParser::doOnCdataEnd()
  {
    if (m_parserContext.isExistQueryString())
      {
        m_parserContext.addQuery();
      }
  }

  void _20QueryMapParser::doSetRealFileName()
  {
    m_parserContext.setFileName(getRealFileName());
  }

  void _20QueryMapParser::parseUsingDB(_ExpatXMLProperties &properties)
  {
    if (getParentElementName() != XN_20_SQLS)
      {
        return;
      }

    std::string moduleID = properties.get(XN_20_USING_DB_PR_MODULE_ID, true);
    if (properties.getBool(XN_20_USING_DB_PR_DEFAULT, false)
        || m_parserContext.getGlobalGroupName() == "")
      {
        m_parserContext.setGlobalGroupName(moduleID.c_str());
      }

    m_moduleIDList.push_back(moduleID);
  }

  void _20QueryMapParser::parseSql(_ExpatXMLProperties &properties)
  {
    if (getParentElementName() != XN_20_SQLS)
      {
        return;
      }

    m_parserContext.setSqlName(properties.get(XN_20_SQL_PR_NAME, true));
    std::string moduleID = properties.get(XN_20_SQL_PR_DB, false);
    if (moduleID != "")
      {
        m_parserContext.setLocalGroupName(moduleID);
      }
  }

  void _20QueryMapParser::parseParameter(_ExpatXMLProperties &properties)
  {
    if (getParentElementName() != XN_20_SELECT
        && getParentElementName() != XN_20_INSERT
        && getParentElementName() != XN_20_UPDATE
        && getParentElementName() != XN_20_DELETE
        && getParentElementName() != XN_20_PROCEDURE)
      {
        return;
      }

    m_parserContext.setParameter(
        properties.get(XN_20_PARAM_PR_NAME, true),
        properties.get20ValueType(XN_20_PARAM_PR_TYPE),
        properties.getBindMode(XN_20_PARAM_PR_MODE),
        properties.getInt(XN_20_PARAM_PR_SIZE, false));
  }

  _30QueryMapParser::_30QueryMapParser(const std::string &fileName,
      _QueryMapContext &parserContext) :
    _BaseXmlParser(fileName), m_parserContext(parserContext)
  {
  }

  _30QueryMapParser::~_30QueryMapParser()
  {
  }

  void _30QueryMapParser::doOnElementStart(const XML_Char *szName,
      _ExpatXMLProperties &properties)
  {
    if (!strcasecmp(szName, XN_30_SQLS))
      {
        parseSqls(properties);
      }
    else if (!strcasecmp(szName, XN_30_SELECT))
      {
        m_parserContext.setStatementType(sql::DBGW_STMT_TYPE_SELECT);
        parseSql(properties);
      }
    else if (!strcasecmp(szName, XN_30_PROCEDURE))
      {
        m_parserContext.setStatementType(sql::DBGW_STMT_TYPE_PROCEDURE);
        parseSql(properties);
      }
    else if (!strcasecmp(szName, XN_30_INSERT)
        || !strcasecmp(szName, XN_30_UPDATE)
        || !strcasecmp(szName, XN_30_DELETE))
      {
        m_parserContext.setStatementType(sql::DBGW_STMT_TYPE_UPDATE);
        parseSql(properties);
      }
    else if (!strcasecmp(szName, XN_30_PARAM))
      {
        parseParameter(properties);
      }
    else if (!strcasecmp(szName, XN_GROUP))
      {
        parseGroup(properties);
      }
  }

  void _30QueryMapParser::doOnElementEnd(const XML_Char *szName)
  {
    if (!strcasecmp(szName, XN_30_SQLS))
      {
        m_parserContext.clear();
      }
    else if (!strcasecmp(szName, XN_30_SELECT)
        || !strcasecmp(szName, XN_30_INSERT)
        || !strcasecmp(szName, XN_30_UPDATE)
        || !strcasecmp(szName, XN_30_DELETE)
        || !strcasecmp(szName, XN_30_PROCEDURE))
      {
        m_parserContext.checkAndClearSql();
      }
    else if (!strcasecmp(szName, XN_30_GROUP))
      {
        m_parserContext.checkAndClearGroup();
      }
  }

  void _30QueryMapParser::doOnElementContent(const XML_Char *szData, int nLength)
  {
    if (isCdataSection() == false)
      {
        return;
      }

    if (getParentElementName() != XN_30_SELECT
        && getParentElementName() != XN_30_INSERT
        && getParentElementName() != XN_30_UPDATE
        && getParentElementName() != XN_30_DELETE
        && getParentElementName() != XN_30_PROCEDURE
        && getParentElementName() != XN_30_GROUP)
      {
        return;
      }

    std::string data(szData);
    m_parserContext.appendQueryString(data.substr(0, nLength).c_str());
  }

  void _30QueryMapParser::doOnCdataEnd()
  {
    if (m_parserContext.isExistQueryString())
      {
        m_parserContext.addQuery();
      }
  }

  void _30QueryMapParser::doSetRealFileName()
  {
    m_parserContext.setFileName(getRealFileName());
  }

  void _30QueryMapParser::parseSqls(_ExpatXMLProperties &properties)
  {
    if (!isRootElement())
      {
        return;
      }

    m_parserContext.setGlobalGroupName(
        properties.get(XN_30_SQLS_PR_GROUP_NAME,
            XN_30_SQLS_PR_GROUP_NAME_HIDDEN, false));
  }

  void _30QueryMapParser::parseSql(_ExpatXMLProperties &properties)
  {
    if (getParentElementName() != XN_30_SQLS)
      {
        return;
      }

    m_parserContext.setSqlName(properties.get(XN_30_SQL_PR_NAME, true));
  }

  void _30QueryMapParser::parseParameter(_ExpatXMLProperties &properties)
  {
    if (getParentElementName() != XN_30_SELECT
        && getParentElementName() != XN_30_INSERT
        && getParentElementName() != XN_30_UPDATE
        && getParentElementName() != XN_30_DELETE
        && getParentElementName() != XN_30_PROCEDURE)
      {
        return;
      }

    m_parserContext.setParameter(
        properties.get(XN_30_PARAM_PR_NAME, true),
        properties.get30ValueType(XN_30_PARAM_PR_TYPE),
        properties.getBindMode(XN_30_PARAM_PR_MODE),
        properties.getInt(XN_30_PARAM_PR_SIZE, false));
  }

  void _30QueryMapParser::parseGroup(_ExpatXMLProperties &properties)
  {
    if (getParentElementName() != XN_30_SELECT
        && getParentElementName() != XN_30_INSERT
        && getParentElementName() != XN_30_UPDATE
        && getParentElementName() != XN_30_DELETE
        && getParentElementName() != XN_30_PROCEDURE)
      {
        return;
      }

    m_parserContext.setLocalGroupName(
        properties.get(XN_30_GROUP_PR_NAME, true));
  }

  _ConfigurationParser::_ConfigurationParser(
      Configuration &configuraion, const std::string &fileName,
      _Connector *pConnector) :
    _BaseXmlParser(fileName), m_configuration(configuraion),
    m_pConnector(pConnector), m_pQueryMapper(NULL),
    m_ulMaxWaitExitTimeMilSec(
        Configuration::DEFAULT_MAX_WAIT_EXIT_TIME_MILSEC())
  {
  }

  _ConfigurationParser::_ConfigurationParser(
      Configuration &configuraion, const std::string &fileName,
      _QueryMapper *pQueryMapper) :
    _BaseXmlParser(fileName), m_configuration(configuraion),
    m_pConnector(NULL), m_pQueryMapper(pQueryMapper),
    m_ulMaxWaitExitTimeMilSec(
        Configuration::DEFAULT_MAX_WAIT_EXIT_TIME_MILSEC())
  {
  }

  _ConfigurationParser::_ConfigurationParser(
      Configuration &configuraion, const std::string &fileName,
      _Connector *pConnector,
      _QueryMapper *pQueryMapper) :
    _BaseXmlParser(fileName), m_configuration(configuraion),
    m_pConnector(pConnector), m_pQueryMapper(pQueryMapper),
    m_ulMaxWaitExitTimeMilSec(
        Configuration::DEFAULT_MAX_WAIT_EXIT_TIME_MILSEC())
  {
  }

  void _ConfigurationParser::doOnElementStart(const XML_Char *szName,
      _ExpatXMLProperties &properties)
  {
    if (!strcasecmp(szName, XN_CONFIGURATION))
      {
        parseConfiguration(properties);
      }
    else if (!strcasecmp(szName, XN_LOG))
      {
        parseLog(properties);
      }
    else if (!strcasecmp(szName, XN_JOB_QUEUE))
      {
        parseWorker(properties);
      }
    else if (!strcasecmp(szName, XN_QUERYMAP))
      {
        parseQueryMap(properties);
      }
    else if (!strcasecmp(szName, XN_INCLUDE))
      {
        parseInclude(properties);
      }
    else if (!strcasecmp(szName, XN_STATISTICS))
      {
        parseStatistics(properties);
      }
  }

  void _ConfigurationParser::doOnElementEnd(const XML_Char *szName)
  {
  }

  void _ConfigurationParser::parseConfiguration(
      _ExpatXMLProperties &properties)
  {
    m_ulMaxWaitExitTimeMilSec = properties.getLong(
        XN_CONFIGURATION_PR_MAX_WAIT_EXIT_TIME_MILLIS, false,
        Configuration::DEFAULT_MAX_WAIT_EXIT_TIME_MILSEC());

    m_configuration.setMaxWaitExitTimeMilSec(m_ulMaxWaitExitTimeMilSec);
  }

  void _ConfigurationParser::parseQueryMap(_ExpatXMLProperties &properties)
  {
    if (getParentElementName() != XN_CONFIGURATION || m_pQueryMapper == NULL)
      {
        return;
      }

    std::string version = properties.get(XN_QUERYMAP_PR_VERSION, false);
    if (version == "1.0")
      {
        m_pQueryMapper->setVersion(DBGW_QUERY_MAP_VER_10);
      }
    else if (version == "2.0")
      {
        m_pQueryMapper->setVersion(DBGW_QUERY_MAP_VER_20);
      }
    else if (version == "3.0")
      {
        m_pQueryMapper->setVersion(DBGW_QUERY_MAP_VER_30);
      }
    else
      {
        m_pQueryMapper->setVersion(DBGW_QUERY_MAP_VER_UNKNOWN);
      }
  }

  void _ConfigurationParser::parseLog(_ExpatXMLProperties &properties)
  {
    if (getParentElementName() != XN_CONFIGURATION)
      {
        return;
      }

    _Logger::setLogPath(properties.get(XN_LOG_PR_PATH, false));
    _Logger::setLogLevel(properties.getLogLevel(XN_LOG_PR_LEVEL));
    _Logger::setForceFlush(
        properties.getBool(XN_LOG_PR_FORCE_FLUSH,
            XN_LOG_PR_FORCE_FLUSH_HIDDEN, false));
    _Logger::setDefaultPostfix(properties.getLogPostfix(XN_LOG_PR_POSTFIX));
  }

  void _ConfigurationParser::parseWorker(_ExpatXMLProperties &properties)
  {
    if (getParentElementName() != XN_CONFIGURATION)
      {
        return;
      }

    m_configuration.setJobQueueMaxSize(
        properties.getInt(XN_JOB_QUEUE_MAX_SIZE, false,
            (int) _AsyncWorkerJobManager::DEFAULT_MAX_SIZE()));
  }

  void _ConfigurationParser::parseInclude(_ExpatXMLProperties &properties)
  {
    if (getParentElementName() != XN_CONNECTOR
        && getParentElementName() != XN_QUERYMAP)
      {
        return;
      }

    if (getParentElementName() == XN_CONNECTOR && m_pConnector == NULL)
      {
        return;
      }

    if (getParentElementName() == XN_QUERYMAP && m_pQueryMapper == NULL)
      {
        return;
      }

    std::string file = properties.get(XN_INCLUDE_PR_FILE, false);
    std::string path = properties.get(XN_INCLUDE_PR_PATH, false);
    std::string fileName;
    if (file != "")
      {
        system::_Directory director(file);
        if (director.isDirectory())
          {
            InvalidPropertyValueException e(getRealFileName().c_str(),
                file.c_str(), "file name");
            DBGW_LOG_ERROR(e.what());
            throw e;
          }
        fileName = file;
      }
    else if (path != "")
      {
        system::_Directory director(path);
        if (!director.isDirectory())
          {
            InvalidPropertyValueException e(getRealFileName().c_str(),
                path.c_str(), "file path");
            DBGW_LOG_ERROR(e.what());
            throw e;
          }
        fileName = path;
      }
    else
      {
        NotExistPropertyException e(getRealFileName().c_str(),
            properties.getNodeName(), "file or path");
        DBGW_LOG_ERROR(e.what());
        throw e;
      }

    if (getParentElementName() == XN_CONNECTOR)
      {
        if (m_pConnector != NULL)
          {
            _ConnectorParser parser(fileName, m_pConnector);
            parseXml(&parser);
          }
      }
    else if (getParentElementName() == XN_QUERYMAP)
      {
        if (m_pQueryMapper == NULL)
          {
            return;
          }

        parseQueryMapper(fileName, m_pQueryMapper);
      }
  }

  void _ConfigurationParser::parseStatistics(_ExpatXMLProperties &properties)
  {
    if (getParentElementName() != XN_CONFIGURATION)
      {
        return;
      }

    std::string logPath = properties.get(XN_STATISTICS_LOG_PATH, false);
    if (logPath == "")
      {
        logPath = _StatisticsMonitor::DEFAULT_LOG_PATH();
      }

    int nStatType = 0;
    std::string statType = properties.get(XN_STATISTICS_STATISTICS_TYPE, false);
    if (statType == "")
      {
        nStatType |= _StatisticsMonitor::DBGW_STAT_TYPE_ALL();
      }
    else
      {
        std::vector<std::string> statTypeList;
        boost::split(statTypeList, statType, boost::is_any_of(","));
        std::vector<std::string>::iterator it = statTypeList.begin();
        for (; it != statTypeList.end(); it++)
          {
            boost::trim(*it);
            if (!strcasecmp("query", it->c_str()))
              {
                nStatType |= _StatisticsMonitor::DBGW_STAT_TYPE_QUERY();
              }
            else if (!strcasecmp("connection", it->c_str()))
              {
                nStatType |= _StatisticsMonitor::DBGW_STAT_TYPE_CONNECTION();
              }
            else if (!strcasecmp("worker", it->c_str()))
              {
                nStatType |= _StatisticsMonitor::DBGW_STAT_TYPE_WORKER();
              }
            else if (!strcasecmp("statement", it->c_str()))
              {
                nStatType |= _StatisticsMonitor::DBGW_STAT_TYPE_STATEMENT();
              }
          }
      }

    unsigned long ulLogIntervalMilSec = properties.getLong(
        XN_STATISTICS_TIME_BETWEEN_STATISTICS_RUNS_MILLIS, false,
        _StatisticsMonitor::DEFAULT_LOG_INTERVAL_MILSEC());

    int nMaxFileSizeKBytes = properties.getInt(
        XN_STATISTICS_MAX_FILE_SIZE_KBYTES, false,
        _StatisticsMonitor::DEFAULT_MAX_FILE_SIZE_KBYTES());

    int nMaxBackupCount = properties.getInt(
        XN_STATISTICS_MAX_BACKUP_COUNT, false,
        _StatisticsMonitor::DEFAULT_MAX_BACKUP_COUNT());

    m_configuration.getMonitor()->clear();
    m_configuration.getMonitor()->init(logPath.c_str(),
        nStatType, ulLogIntervalMilSec, nMaxFileSizeKBytes, nMaxBackupCount);
  }

  void parseXml(_XmlParser *pParser)
  {
    std::string path = pParser->getFileName().c_str();
    std::vector<std::string> fileNameList;
    system::_Directory directory(path);
    if (directory.isDirectory())
      {
        directory.getFullPathList(fileNameList);
      }
    else
      {
        fileNameList.push_back(path);
      }

    for (std::vector<std::string>::iterator it = fileNameList.begin();
        it != fileNameList.end(); ++it)
      {
        if (system::getFileExtension(*it) != "xml")
          {
            continue;
          }

        pParser->setRealFileName(*it);
        pParser->parse();
      }
  }

  void parseQueryMapper(const std::string &fileName,
      _QueryMapper *pQueryMaper)
  {
    _QueryMapContext context(fileName, pQueryMaper);

    _XmlParser *pParser = NULL;
    if (pQueryMaper->getVersion() == DBGW_QUERY_MAP_VER_10)
      {
        pParser = new _10QueryMapParser(fileName, context);
      }
    else if (pQueryMaper->getVersion() == DBGW_QUERY_MAP_VER_20)
      {
        pParser = new _20QueryMapParser(fileName, context);
      }
    else
      {
        pParser = new _30QueryMapParser(fileName, context);
      }

    try
      {
        parseXml(pParser);
        delete pParser;
      }
    catch (...)
      {
        delete pParser;
        throw;
      }
  }

}
