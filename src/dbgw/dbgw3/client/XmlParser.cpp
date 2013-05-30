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
  static const char *XML_NODE_GROUP_PROP_CLIENT_CHARSET = "clientCharset";
  static const char *XML_NODE_GROUP_PROP_DB_CHARSET = "dbCharset";

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
  }

  const std::string &_XmlParser::getFileName() const
  {
    return m_fileName;
  }

  const std::string &_XmlParser::getRealFileName() const
  {
    return m_realFileName;
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

  const char *_ExpatXMLProperties::get(const char *szName, bool bRequired)
  {
    return get(szName, NULL, bRequired);
  }

  const char *_ExpatXMLProperties::get(const char *szName,
      const char *szHiddenName, bool bRequired)
  {
    for (int i = 0; m_pAttr[i] != NULL; i += 2)
      {
        if (szName != NULL && !strcmp(m_pAttr[i], szName))
          {
            return m_pAttr[i + 1];
          }
        else if (szHiddenName != NULL && !strcmp(m_pAttr[i], szHiddenName))
          {
            return m_pAttr[i + 1];
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

  const char *_ExpatXMLProperties::getCString(const char *szName,
      bool bRequired)
  {
    return getCString(szName, NULL, bRequired);
  }

  const char *_ExpatXMLProperties::getCString(const char *szName,
      const char *szHiddenName, bool bRequired)
  {
    for (int i = 0; m_pAttr[i] != NULL; i += 2)
      {
        if (szName != NULL && !strcmp(m_pAttr[i], szName))
          {
            return m_pAttr[i + 1];
          }
        else if (szHiddenName != NULL && !strcmp(m_pAttr[i], szHiddenName))
          {
            return m_pAttr[i + 1];
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
        return NULL;
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
    for (int i = 0; m_pAttr[i] != NULL; i += 2)
      {
        if (szName != NULL && !strcmp(m_pAttr[i], szName))
          {
            return propertyToInt(m_pAttr[i + 1]);
          }
        else if (szHiddenName != NULL && !strcmp(m_pAttr[i], szHiddenName))
          {
            return propertyToInt(m_pAttr[i + 1]);
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
    for (int i = 0; m_pAttr[i] != NULL; i += 2)
      {
        if (szName != NULL && !strcmp(m_pAttr[i], szName))
          {
            return propertyToLong(m_pAttr[i + 1]);
          }
        else if (szHiddenName != NULL && !strcmp(m_pAttr[i], szHiddenName))
          {
            return propertyToLong(m_pAttr[i + 1]);
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
    for (int i = 0; m_pAttr[i] != NULL; i += 2)
      {
        if (szName != NULL && !strcmp(m_pAttr[i], szName))
          {
            return propertyToBoolean(m_pAttr[i + 1]);
          }
        else if (szHiddenName != NULL && !strcmp(m_pAttr[i], szHiddenName))
          {
            return propertyToBoolean(m_pAttr[i + 1]);
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

    const char *szValidateResult = getCString(szName, szHiddenName, false);
    if (szValidateResult == NULL)
      {
        return;
      }

    std::vector<std::string> queryTypeList;
    boost::split(queryTypeList, szValidateResult, boost::is_any_of(","));
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
                szValidateResult, "SELECT|PROCEDURE|UPDATE");
            DBGW_LOG_ERROR(e.what());
            throw e;
          }
      }
  }

  ValueType _ExpatXMLProperties::get30ValueType(const char *szName)
  {
    const char *szType = get(szName, true);

    if (!strcasecmp(szType, "string"))
      {
        return DBGW_VAL_TYPE_STRING;
      }
    if (!strcasecmp(szType, "char"))
      {
        return DBGW_VAL_TYPE_CHAR;
      }
    if (!strcasecmp(szType, "int"))
      {
        return DBGW_VAL_TYPE_INT;
      }
    if (!strcasecmp(szType, "long") || !strcasecmp(szType, "int64"))
      {
        return DBGW_VAL_TYPE_LONG;
      }
    if (!strcasecmp(szType, "float"))
      {
        return DBGW_VAL_TYPE_FLOAT;
      }
    if (!strcasecmp(szType, "double"))
      {
        return DBGW_VAL_TYPE_DOUBLE;
      }
    if (!strcasecmp(szType, "date"))
      {
        return DBGW_VAL_TYPE_DATE;
      }
    if (!strcasecmp(szType, "time"))
      {
        return DBGW_VAL_TYPE_TIME;
      }
    if (!strcasecmp(szType, "datetime"))
      {
        return DBGW_VAL_TYPE_DATETIME;
      }
    if (!strcasecmp(szType, "bytes"))
      {
        return DBGW_VAL_TYPE_BYTES;
      }
    if (!strcasecmp(szType, "clob"))
      {
        return DBGW_VAL_TYPE_CLOB;
      }
    if (!strcasecmp(szType, "blob"))
      {
        return DBGW_VAL_TYPE_BLOB;
      }

    InvalidValueTypeException e(m_xmlParser.getFileName(), szType);
    DBGW_LOG_ERROR(e.what());
    throw e;
  }

  ValueType _ExpatXMLProperties::get20ValueType(const char *szName)
  {
    const char *szType = get(szName, true);

    if (!strcasecmp(szType, "string"))
      {
        return DBGW_VAL_TYPE_STRING;
      }
    if (!strcasecmp(szType, "char"))
      {
        return DBGW_VAL_TYPE_CHAR;
      }
    if (!strcasecmp(szType, "int"))
      {
        return DBGW_VAL_TYPE_INT;
      }
    if (!strcasecmp(szType, "long"))
      {
        return DBGW_VAL_TYPE_LONG;
      }
    if (!strcasecmp(szType, "double"))
      {
        return DBGW_VAL_TYPE_DOUBLE;
      }
    if (!strcasecmp(szType, "data"))
      {
        return DBGW_VAL_TYPE_DATETIME;
      }

    InvalidValueTypeException e(m_xmlParser.getFileName(), szType);
    DBGW_LOG_ERROR(e.what());
    throw e;
  }

  ValueType _ExpatXMLProperties::get10ValueType(const char *szName)
  {
    const char *szType = get(szName, true);

    if (!strcasecmp(szType, "string"))
      {
        return DBGW_VAL_TYPE_STRING;
      }
    if (!strcasecmp(szType, "char"))
      {
        return DBGW_VAL_TYPE_CHAR;
      }
    if (!strcasecmp(szType, "int"))
      {
        return DBGW_VAL_TYPE_INT;
      }
    if (!strcasecmp(szType, "int64"))
      {
        return DBGW_VAL_TYPE_LONG;
      }
    if (!strcasecmp(szType, "float"))
      {
        return DBGW_VAL_TYPE_FLOAT;
      }
    if (!strcasecmp(szType, "double"))
      {
        return DBGW_VAL_TYPE_DOUBLE;
      }

    InvalidValueTypeException e(m_xmlParser.getFileName(), szType);
    DBGW_LOG_ERROR(e.what());
    throw e;
  }

  CCI_LOG_LEVEL _ExpatXMLProperties::getLogLevel(const char *szName)
  {
    const char *szLogLevel = get(szName, false);

    if (!strcasecmp(szLogLevel, "off"))
      {
        return CCI_LOG_LEVEL_OFF;
      }
    if (!strcasecmp(szLogLevel, "error"))
      {
        return CCI_LOG_LEVEL_ERROR;
      }
    if (!strcasecmp(szLogLevel, "warning"))
      {
        return CCI_LOG_LEVEL_WARN;
      }
    if (!strcasecmp(szLogLevel, "info") || !strcmp(szLogLevel, ""))
      {
        return CCI_LOG_LEVEL_INFO;
      }
    if (!strcasecmp(szLogLevel, "debug"))
      {
        return CCI_LOG_LEVEL_DEBUG;
      }

    InvalidPropertyValueException e(m_xmlParser.getFileName(), szLogLevel,
        "OFF|ERROR|WARNING|INFO|DEBUG");
    DBGW_LOG_ERROR(e.what());
    throw e;
  }

  CCI_LOG_POSTFIX _ExpatXMLProperties::getLogPostfix(const char *szName)
  {
    const char *szLogPostfix = get(szName, false);

    if (!strcasecmp(szLogPostfix, "none") || !strcmp(szLogPostfix, ""))
      {
        return CCI_LOG_POSTFIX_NONE;
      }
    if (!strcasecmp(szLogPostfix, "date"))
      {
        return CCI_LOG_POSTFIX_DATE;
      }

    InvalidPropertyValueException e(m_xmlParser.getFileName(), szLogPostfix,
        "NONE|PID|DATE");
    DBGW_LOG_ERROR(e.what());
    throw e;
  }

  sql::ParameterMode _ExpatXMLProperties::getBindMode(const char *szName)
  {
    const char *szMode = getCString(szName, false);

    if (szMode == NULL || !strcasecmp(szMode, "in"))
      {
        return sql::DBGW_PARAM_MODE_IN;
      }
    else if (!strcasecmp(szMode, "out"))
      {
        return sql::DBGW_PARAM_MODE_OUT;
      }
    else if (!strcasecmp(szMode, "inout"))
      {
        return sql::DBGW_PARAM_MODE_INOUT;
      }
    else
      {
        InvalidPropertyValueException e(m_xmlParser.getFileName(), szMode,
            "IN|OUT|INOUT");
        DBGW_LOG_ERROR(e.what());
        throw e;
      }
  }

  const char *_ExpatXMLProperties::getNodeName() const
  {
    return m_nodeName.c_str();
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
    try
      {
        do
          {
            nReadLen = fread(buffer, 1, XML_FILE_BUFFER_SIZE, fp);
            status = XML_Parse(parser.get(), buffer, nReadLen, 0);

            if (isAborted())
              {
                break;
              }
          }
        while (nReadLen > 0 && status != XML_STATUS_ERROR);
      }
    catch (Exception &e)
      {
        exception = e;
      }

    fclose(fp);

    if (status == XML_STATUS_ERROR)
      {
        errCode = XML_GetErrorCode(parser.get());
        InvalidXMLSyntaxException e(XML_ErrorString(errCode),
            getRealFileName().c_str(),
            XML_GetCurrentLineNumber(parser.get()),
            XML_GetCurrentColumnNumber(parser.get()));
        DBGW_LOG_ERROR(e.what());
        throw e;
      }

    if (exception.getErrorCode() != DBGW_ER_NO_ERROR)
      {
        throw exception;
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

      const char *szDbCharset =
          properties.getCString(XML_NODE_GROUP_PROP_DB_CHARSET, false);
      if (szDbCharset != NULL)
        {
          dbCodePage = stringToCodepage(szDbCharset);
        }

      const char *szClientCharset =
          properties.getCString(XML_NODE_GROUP_PROP_CLIENT_CHARSET, false);
      if (szClientCharset != NULL)
        {
          clientCodePage = stringToCodepage(szClientCharset);
        }

      if ((szClientCharset != NULL && szDbCharset == NULL)
          || (szClientCharset == NULL && szDbCharset != NULL))
        {
          dbgw::NotExistCharsetException e;
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

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
              clientCodePage));
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

      const char *szUser = properties.get(XN_HOST_PR_USER, false);
      const char *szPasword = properties.get(XN_HOST_PR_PASSWORD, false);

      int nWeight = properties.getInt(XN_HOST_PR_WEIGHT, true, 1);
      if (nWeight <= 0)
        {
          InvalidHostWeightException e;
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      m_pHost = trait<_Host>::sp(
          new _Host(m_url.c_str(), szUser, szPasword, nWeight));
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

    void setGlobalGroupName(const char *szGlobalGroupName)
    {
      m_globalGroupName = szGlobalGroupName;
    }

    void setLocalGroupName(const char *szLocalGroupName)
    {
      m_localGroupName = szLocalGroupName;
    }

    void setStatementType(sql::StatementType statementType)
    {
      m_statementType = statementType;
    }

    void setSqlName(const char *szSqlName)
    {
      m_sqlName = szSqlName;
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

    void setParameter(const char *szMode, const char *szName,
        ValueType valueType, sql::ParameterMode mode, int nSize)
    {
      if (isImplicitParamName(szName))
        {
          throw getLastException();
        }

      _QueryParameter stParam;
      stParam.name = szName;
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

  void _QueryMapContext::setGlobalGroupName(const char *szGlobalGroupName)
  {
    m_pImpl->setGlobalGroupName(szGlobalGroupName);
  }

  void _QueryMapContext::setLocalGroupName(const char *szLocalGroupName)
  {
    m_pImpl->setLocalGroupName(szLocalGroupName);
  }

  void _QueryMapContext::setStatementType(sql::StatementType statementType)
  {
    m_pImpl->setStatementType(statementType);
  }

  void _QueryMapContext::setSqlName(const char *szSqlName)
  {
    m_pImpl->setSqlName(szSqlName);
  }

  void _QueryMapContext::setParameter(const std::string &name, int nIndex,
      ValueType valueType, sql::ParameterMode mode, int nSize)
  {
    m_pImpl->setParameter(name, nIndex, valueType, mode, nSize);
  }

  void _QueryMapContext::setParameter(const char *szMode, const char *szName,
      ValueType valueType, sql::ParameterMode mode, int nSize)
  {
    m_pImpl->setParameter(szMode, szName, valueType, mode, nSize);
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

  void _10QueryMapParser::parseSql(_ExpatXMLProperties properties)
  {
    if (getParentElementName() != XN_10_DEFINEDQUERY)
      {
        return;
      }

    m_parserContext.setLocalGroupName("__FIRST__");
    m_parserContext.setSqlName(properties.get(XN_10_SQL_PR_NAME, true));

    const char *pType = properties.getCString(XN_10_SQL_PR_TYPE, false);
    if (pType != NULL)
      {
        if (!strcasecmp(pType, "selcet"))
          {
            m_parserContext.setStatementType(sql::DBGW_STMT_TYPE_SELECT);
          }
        else if (!strcasecmp(pType, "update") && !strcasecmp(pType, "delete")
            && !strcasecmp(pType, "insert"))
          {
            m_parserContext.setStatementType(sql::DBGW_STMT_TYPE_UPDATE);
          }
        else if (!strcasecmp(pType, "procedure"))
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
    const char *szModuleID = properties.getCString(XN_20_SQL_PR_DB, false);
    if (szModuleID != NULL)
      {
        m_parserContext.setLocalGroupName(szModuleID);
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
        properties.getCString(XN_20_PARAM_PR_MODE, false),
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

    m_parserContext.setParameter(properties.get(XN_30_PARAM_PR_MODE, true),
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

    _Logger::setLogPath(properties.getCString(XN_LOG_PR_PATH, false));
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

    const char *szFile = properties.getCString(XN_INCLUDE_PR_FILE, false);
    const char *szPath = properties.getCString(XN_INCLUDE_PR_PATH, false);
    std::string fileName;
    if (szFile != NULL)
      {
        system::_Directory director(szFile);
        if (director.isDirectory())
          {
            InvalidPropertyValueException e(getRealFileName().c_str(), szFile,
                "file name");
            DBGW_LOG_ERROR(e.what());
            throw e;
          }
        fileName = szFile;
      }
    else if (szPath != NULL)
      {
        system::_Directory director(szPath);
        if (!director.isDirectory())
          {
            InvalidPropertyValueException e(getRealFileName().c_str(), szPath,
                "file path");
            DBGW_LOG_ERROR(e.what());
            throw e;
          }
        fileName = szPath;
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

    unsigned long nMaxFileSizeKBytes = properties.getInt(
        XN_STATISTICS_MAX_FILE_SIZE_KBYTES, false,
        _StatisticsMonitor::DEFAULT_MAX_FILE_SIZE_KBYTES());

    unsigned long nMaxBackupCount = properties.getInt(
        XN_STATISTICS_MAX_BACKUP_COUNT, false,
        _StatisticsMonitor::DEFAULT_MAX_BACKUP_COUNT());

    m_configuration.getMonitor()->clear();
    m_configuration.getMonitor()->init(logPath.c_str(),
        nStatType, ulLogIntervalMilSec, nMaxFileSizeKBytes, nMaxBackupCount);
  }

  void parseXml(_XmlParser *pParser)
  {
    const char *szPath = pParser->getFileName().c_str();
    std::vector<std::string> fileNameList;
    system::_Directory directory(szPath);
    if (directory.isDirectory())
      {
        directory.getFullPathList(fileNameList);
      }
    else
      {
        fileNameList.push_back(szPath);
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
