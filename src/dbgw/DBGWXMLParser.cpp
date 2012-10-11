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
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp>
#if defined(WINDOWS)
#include <expat/expat.h>
#else /* WINDOWS */
#include <expat.h>
#endif /* !WINDOWS */
#include <fstream>
#include <errno.h>
#include "DBGWCommon.h"
#include "DBGWError.h"
#include "DBGWPorting.h"
#include "DBGWLogger.h"
#include "DBGWValue.h"
#include "DBGWQuery.h"
#include "DBGWDataBaseInterface.h"
#include "DBGWConfiguration.h"
#include "DBGWXMLParser.h"

namespace dbgw
{

  static const int XML_FILE_BUFFER_SIZE = 4096;

  static const char *XML_NODE_CONFIGURATION = "configuration";
  static const char *XML_NODE_CONNECTOR = "connector";
  static const char *XML_NODE_QUERYMAP = "querymap";
  static const char *XML_NODE_QUERYMAP_PROP_VERSION = "version";

  static const char *XML_NODE_SERVICE = "service";
  static const char *XML_NODE_SERVICE_PROP_NAMESPACE = "namespace";
  static const char *XML_NODE_SERVICE_PROP_DESCRIPTION = "description";
  static const char *XML_NODE_SERVICE_PROP_VALIDATE_RESULT = "validate-result";
  static const char *XML_NODE_SERVICE_PROP_VALIDATE_RATIO = "validate-ratio";

  static const char *XML_NODE_GROUP = "group";
  static const char *XML_NODE_GROUP_PROP_NAME = "name";
  static const char *XML_NODE_GROUP_PROP_DESCRIPTION = "description";
  static const char *XML_NODE_GROUP_PROP_INACTIVATE = "inactivate";
  static const char *XML_NODE_GROUP_PROP_IGNORE_RESULT = "ignore-result";

  static const char *XML_NODE_POOL = "pool";
  static const char *XML_NODE_POOL_PROP_SIZE = "pool-size";

  static const char *XML_NODE_DBINFO = "dbinfo";
  static const char *XML_NODE_DBINFO_PROP_DBNAME = "dbname";
  static const char *XML_NODE_DBINFO_PROP_DBUSER = "dbuser";
  static const char *XML_NODE_DBINFO_PROP_DBPASSWD = "dbpasswd";

  static const char *XML_NODE_HOST = "host";
  static const char *XML_NODE_HOST_PROP_ADDRESS = "address";
  static const char *XML_NODE_HOST_PROP_PORT = "port";
  static const char *XML_NODE_HOST_PROP_WEIGHT = "weight";

  static const char *XML_NODE_ALTHOSTS = "althosts";

  static const char *XML_NODE_10_DEFINEDQUERY = "definedquery";

  static const char *XML_NODE_10_SQL = "sql";
  static const char *XML_NODE_10_SQL_PROP_NAME = "name";
  static const char *XML_NODE_10_SQL_PROP_TYPE = "type";

  static const char *XML_NODE_10_QUERY = "query";

  static const char *XML_NODE_10_PLACEHOLDER = "placeholder";

  static const char *XML_NODE_10_PARAMETER = "parameter";
  static const char *XML_NODE_10_PARAMETER_PROP_NAME = "name";
  static const char *XML_NODE_10_PARAMETER_PROP_INDEX = "index";
  static const char *XML_NODE_10_PARAMETER_PROP_TYPE = "type";
  static const char *XML_NODE_10_PARAMETER_PROP_MODE = "mode";

  static const char *XML_NODE_10_RESULT = "result";

  static const char *XML_NODE_10_COLUMN = "column";
  static const char *XML_NODE_10_COLUMN_PROP_NAME = "name";
  static const char *XML_NODE_10_COLUMN_PROP_INDEX = "index";
  static const char *XML_NODE_10_COLUMN_PROP_LENGTH = "length";
  static const char *XML_NODE_10_COLUMN_PROP_TYPE = "type";

  static const char *XML_NODE_20_SQLS = "sqls";

  static const char *XML_NODE_20_USING_DB = "using-db";
  static const char *XML_NODE_20_USING_DB_PROP_MODULE_ID = "module-id";
  static const char *XML_NODE_20_USING_DB_PROP_DEFAULT = "default";

  static const char *XML_NODE_20_SELECT = "select";
  static const char *XML_NODE_20_INSERT = "insert";
  static const char *XML_NODE_20_UPDATE = "update";
  static const char *XML_NODE_20_DELETE = "delete";
  static const char *XML_NODE_20_PROCEDURE = "procedure";
  static const char *XML_NODE_20_SQL_PROP_NAME = "name";
  static const char *XML_NODE_20_SQL_PROP_DB = "db";

  static const char *XML_NODE_20_PARAM = "param";
  static const char *XML_NODE_20_PARAM_PROP_NAME = "name";
  static const char *XML_NODE_20_PARAM_PROP_MODE = "mode";
  static const char *XML_NODE_20_PARAM_PROP_TYPE = "type";

  static const char *XML_NODE_30_SQLS = "sqls";
  static const char *XML_NODE_30_SQLS_PROP_GROUP_NAME = "group-name";

  static const char *XML_NODE_30_SELECT = "select";
  static const char *XML_NODE_30_INSERT = "insert";
  static const char *XML_NODE_30_UPDATE = "update";
  static const char *XML_NODE_30_DELETE = "delete";
  static const char *XML_NODE_30_PROCEDURE = "procedure";
  static const char *XML_NODE_30_SQL_PROP_NAME = "name";

  static const char *XML_NODE_30_PARAM = "param";
  static const char *XML_NODE_30_PARAM_PROP_NAME = "name";
  static const char *XML_NODE_30_PARAM_PROP_TYPE = "type";
  static const char *XML_NODE_30_PARAM_PROP_MODE = "mode";

  static const char *XML_NODE_30_GROUP = "group";
  static const char *XML_NODE_30_GROUP_PROP_NAME = "name";

  static const char *XML_NODE_30_CDATA = "cdata";

  static const char *XML_NODE_LOG = "log";
  static const char *XML_NODE_LOG_PROP_LEVEL = "level";
  static const char *XML_NODE_LOG_PROP_PATH = "path";
  static const char *XML_NODE_LOG_PROP_FORCE_FLUSH = "force-flush";

  static const char *XML_NODE_INCLUDE = "include";
  static const char *XML_NODE_INCLUDE_PROP_FILE = "file";
  static const char *XML_NODE_INCLUDE_PROP_PATH = "path";

  static const int POOL_DEFAULT_POOL_SIZE = 10;

  DBGWExpatXMLParser::DBGWExpatXMLParser(const string &fileName) :
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

  DBGWExpatXMLParser::~DBGWExpatXMLParser()
  {
    if (m_pParser != NULL)
      {
        XML_ParserFree(m_pParser);
      }
  }

  XML_Parser DBGWExpatXMLParser::get() const
  {
    return m_pParser;
  }

  const char *DBGWExpatXMLParser::getFileName() const
  {
    return m_fileName.c_str();
  }

  DBGWExpatXMLProperties::DBGWExpatXMLProperties(
      const DBGWExpatXMLParser &xmlParser, const string &nodeName,
      const XML_Char *szAttr[]) :
    m_xmlParser(xmlParser), m_nodeName(nodeName), m_pAttr(szAttr)
  {
  }

  DBGWExpatXMLProperties::~DBGWExpatXMLProperties()
  {
  }

  const char *DBGWExpatXMLProperties::get(const char *szName, bool bRequired)
  {
    for (int i = 0; m_pAttr[i] != NULL; i += 2)
      {
        if (!strcmp(m_pAttr[i], szName))
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

  const char *DBGWExpatXMLProperties::getCString(const char *szName, bool bRequired)
  {
    for (int i = 0; m_pAttr[i] != NULL; i += 2)
      {
        if (!strcmp(m_pAttr[i], szName))
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

  int DBGWExpatXMLProperties::getInt(const char *szName, bool bRequired)
  {
    for (int i = 0; m_pAttr[i] != NULL; i += 2)
      {
        if (!strcmp(m_pAttr[i], szName))
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
        return 0;
      }
  }

  bool DBGWExpatXMLProperties::getBool(const char *szName, bool bRequired)
  {
    for (int i = 0; m_pAttr[i] != NULL; i += 2)
      {
        if (!strcmp(m_pAttr[i], szName))
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

  void DBGWExpatXMLProperties::getValidateResult(const char *szName,
      bool bValidateResult[])
  {
    memset(bValidateResult, 0, sizeof(bValidateResult));

    const char *szValidateResult = getCString(szName, false);
    if (szValidateResult == NULL)
      {
        return;
      }

    DBGWStringList queryTypeList;
    boost::split(queryTypeList, szValidateResult, boost::is_any_of(","));
    for (DBGWStringList::iterator it = queryTypeList.begin(); it
        != queryTypeList.end(); it++)
      {
        boost::trim(*it);
        if (!strcasecmp(it->c_str(), "select"))
          {
            bValidateResult[DBGW_QUERY_TYPE_SELECT] = true;
          }
        else if (!strcasecmp(it->c_str(), "update"))
          {
            bValidateResult[DBGW_QUERY_TYPE_UPDATE] = true;
          }
        else if (!strcasecmp(it->c_str(), "procedure"))
          {
            bValidateResult[DBGW_QUERY_TYPE_PROCEDURE] = true;
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

  DBGWValueType DBGWExpatXMLProperties::get30ValueType(const char *szName)
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
#ifdef ENABLE_LOB
    if (!strcasecmp(szType, "clob"))
      {
        return DBGW_VAL_TYPE_CLOB;
      }
#endif

    InvalidValueTypeException e(m_xmlParser.getFileName(), szType);
    DBGW_LOG_ERROR(e.what());
    throw e;
  }

  DBGWValueType DBGWExpatXMLProperties::get20ValueType(const char *szName)
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

  DBGWValueType DBGWExpatXMLProperties::get10ValueType(const char *szName)
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

  CCI_LOG_LEVEL DBGWExpatXMLProperties::getLogLevel(const char *szName)
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

  DBGWBindMode DBGWExpatXMLProperties::getBindMode(const char *szName)
  {
    const char *szMode = getCString(szName, false);

    if (szMode == NULL || !strcasecmp(szMode, "in"))
      {
        return DBGW_BIND_MODE_IN;
      }
    else if (!strcasecmp(szMode, "out"))
      {
        return DBGW_BIND_MODE_OUT;
      }
    else if (!strcasecmp(szMode, "inout"))
      {
        return DBGW_BIND_MODE_INOUT;
      }
    else
      {
        InvalidPropertyValueException e(m_xmlParser.getFileName(), szMode,
            "IN|OUT|INOUT");
        DBGW_LOG_ERROR(e.what());
        throw e;
      }
  }

  const char *DBGWExpatXMLProperties::getNodeName() const
  {
    return m_nodeName.c_str();
  }

  int DBGWExpatXMLProperties::propertyToInt(const char *szProperty)
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

  bool DBGWExpatXMLProperties::propertyToBoolean(const char *szProperty)
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

  DBGWParser::DBGWParser(const string &fileName) :
    m_fileName(fileName), m_realFileName(fileName), m_bCdataSection(false),
    m_bAborted(false)
  {
  }

  DBGWParser::~DBGWParser()
  {
  }

  void DBGWParser::setRealFileName(const string &realFileName)
  {
    m_realFileName = realFileName;
  }

  const string &DBGWParser::getFileName() const
  {
    return m_fileName;
  }

  const string &DBGWParser::getRealFileName() const
  {
    return m_realFileName;
  }

  const string &DBGWParser::getParentElementName() const
  {
    return m_elementStack.top();
  }

  bool DBGWParser::isRootElement() const
  {
    return m_elementStack.empty();
  }

  bool DBGWParser::isCdataSection() const
  {
    return m_bCdataSection;
  }

  bool DBGWParser::isAborted() const
  {
    return m_bAborted;
  }

  void DBGWParser::parse(DBGWParser *pParser)
  {
    const char *szPath = pParser->getFileName().c_str();
    DBGWStringList fileNameList;
    system::DirectorySharedPtr pDir = system::DirectoryFactory::create(szPath);
    if (pDir->isDirectory())
      {
        pDir->getFileFullPathList(fileNameList);
      }
    else
      {
        fileNameList.push_back(szPath);
      }

    for (DBGWStringList::iterator it = fileNameList.begin();
        it != fileNameList.end(); ++it)
      {
        if (system::getFileExtension(*it) != "xml")
          {
            continue;
          }

        pParser->setRealFileName(*it);
        doParse(pParser);
      }
  }

  void DBGWParser::doOnElementContent(const XML_Char *szData, int nLength)
  {
  }

  void DBGWParser::doOnCdataStart()
  {
  }

  void DBGWParser::doOnCdataEnd()
  {
  }

  void DBGWParser::abort()
  {
    m_bAborted = true;
  }

  void DBGWParser::doParse(DBGWParser *pParser)
  {
    DBGWExpatXMLParser parser(pParser->getRealFileName());

    XML_SetUserData(parser.get(), pParser);
    XML_SetElementHandler(parser.get(), onElementStart, onElementEnd);
    XML_SetCdataSectionHandler(parser.get(), onCdataStart, onCdataEnd);
    XML_SetCharacterDataHandler(parser.get(), onElementContent);
    XML_SetUnknownEncodingHandler(parser.get(), onUnknownEncoding, NULL);

    FILE *fp = fopen(pParser->getRealFileName().c_str(), "r");
    if (fp == NULL)
      {
        CreateFailParserExeception e(pParser->getRealFileName().c_str());
        DBGW_LOGF_ERROR("%s (%d)", e.what(), errno);
        throw e;
      }

    DBGWException exception;
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

            if (pParser->isAborted())
              {
                break;
              }
          }
        while (nReadLen > 0 && status != XML_STATUS_ERROR);
      }
    catch (DBGWException &e)
      {
        exception = e;
      }

    fclose(fp);

    if (status == XML_STATUS_ERROR)
      {
        errCode = XML_GetErrorCode(parser.get());
        InvalidXMLSyntaxException e(XML_ErrorString(errCode),
            pParser->getRealFileName().c_str(),
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

  void DBGWParser::onElementStart(void *pParam, const XML_Char *szName,
      const XML_Char *szAttr[])
  {
    DBGWParser *pParser = (DBGWParser *) pParam;
    DBGWExpatXMLProperties properties(pParser->getFileName(), szName, szAttr);

    pParser->doOnElementStart(szName, properties);

    string name = szName;
    boost::algorithm::to_lower(name);
    pParser->m_elementStack.push(name);
  }

  void DBGWParser::onElementEnd(void *pParam, const XML_Char *szName)
  {
    DBGWParser *pParser = (DBGWParser *) pParam;

    pParser->m_elementStack.pop();

    pParser->doOnElementEnd(szName);
  }

  void DBGWParser::onElementContent(void *pParam, const XML_Char *szData,
      int nLength)
  {
    DBGWParser *pParser = (DBGWParser *) pParam;

    pParser->doOnElementContent(szData, nLength);
  }

  void DBGWParser::onCdataStart(void *pParam)
  {
    DBGWParser *pParser = (DBGWParser *) pParam;

    pParser->m_bCdataSection = true;
    pParser->doOnCdataStart();
  }

  void DBGWParser::onCdataEnd(void *pParam)
  {
    DBGWParser *pParser = (DBGWParser *) pParam;

    pParser->doOnCdataEnd();
    pParser->m_bCdataSection = false;
  }

  int DBGWParser::onUnknownEncoding(void *pParam, const XML_Char *szName,
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

  int DBGWParser::convertCharacterSet(void *pParam, const char *szStr)
  {
    return *szStr;
  }

  DBGWConnectorParser::DBGWConnectorParser(const string &fileName,
      DBGWConnectorSharedPtr pConnector) :
    DBGWParser(fileName), m_pConnector(pConnector),
    m_nPoolSize(POOL_DEFAULT_POOL_SIZE), bExistDbInfo(false)
  {
  }

  DBGWConnectorParser::~DBGWConnectorParser()
  {
  }

  void DBGWConnectorParser::doOnElementStart(const XML_Char *szName,
      DBGWExpatXMLProperties &properties)
  {
    if (!strcasecmp(szName, XML_NODE_SERVICE))
      {
        parseService(properties);
      }
    else if (!strcasecmp(szName, XML_NODE_GROUP))
      {
        parseGroup(properties);
      }
    else if (!strcasecmp(szName, XML_NODE_POOL))
      {
        parsePool(properties);
      }
    else if (!strcasecmp(szName, XML_NODE_DBINFO))
      {
        parseDBInfo(properties);
      }
    else if (!strcasecmp(szName, XML_NODE_HOST))
      {
        parseHost(properties);
      }
    else if (!strcasecmp(szName, XML_NODE_ALTHOSTS))
      {
        parseAltHost(properties);
      }
  }

  void DBGWConnectorParser::doOnElementEnd(const XML_Char *szName)
  {
    if (!strcasecmp(szName, XML_NODE_SERVICE))
      {
        if (m_pService->empty())
          {
            NotExistNodeInXmlException e(XML_NODE_GROUP, getFileName().c_str());
            DBGW_LOG_ERROR(e.what());
            throw e;
          }
        m_pService->initPool(m_nPoolSize);

        m_pService = DBGWServiceSharedPtr();
        m_nPoolSize = POOL_DEFAULT_POOL_SIZE;
      }
    else if (!strcasecmp(szName, XML_NODE_GROUP))
      {
        if (m_pGroup->empty())
          {
            NotExistNodeInXmlException e(XML_NODE_HOST, getFileName().c_str());
            DBGW_LOG_ERROR(e.what());
            throw e;
          }

        m_pGroup = DBGWGroupSharedPtr();
        m_dbInfoMap.clear();
        bExistDbInfo = false;
      }
    else if (!strcasecmp(szName, XML_NODE_HOST))
      {
        m_pHost = DBGWHostSharedPtr();
      }
  }

  void DBGWConnectorParser::parseService(DBGWExpatXMLProperties &properties)
  {
    if (!isRootElement())
      {
        return;
      }

    bool bValidateResult[DBGW_QUERY_TYPE_SIZE];
    properties.getValidateResult(XML_NODE_SERVICE_PROP_VALIDATE_RESULT,
        bValidateResult);

    m_pService = DBGWServiceSharedPtr(
        new DBGWService(
            getFileName(),
            properties.get(XML_NODE_SERVICE_PROP_NAMESPACE, true),
            properties.get(XML_NODE_SERVICE_PROP_DESCRIPTION, false),
            bValidateResult,
            properties.getInt(XML_NODE_SERVICE_PROP_VALIDATE_RATIO, false)));
    m_pConnector->addService(m_pService);
  }

  void DBGWConnectorParser::parseGroup(DBGWExpatXMLProperties &properties)
  {
    if (getParentElementName() != XML_NODE_SERVICE)
      {
        return;
      }

    m_pGroup = DBGWGroupSharedPtr(
        new DBGWGroup(
            getFileName(),
            properties.get(XML_NODE_GROUP_PROP_NAME, true),
            properties.get(XML_NODE_GROUP_PROP_DESCRIPTION, false),
            properties.getBool(XML_NODE_GROUP_PROP_INACTIVATE, false),
            properties.getBool(XML_NODE_GROUP_PROP_IGNORE_RESULT,
                false)));
    m_pService->addGroup(m_pGroup);
  }

  void DBGWConnectorParser::parsePool(DBGWExpatXMLProperties &properties)
  {
    if (getParentElementName() != XML_NODE_SERVICE)
      {
        return;
      }

    m_nPoolSize = properties.getInt(XML_NODE_POOL_PROP_SIZE, true);
  }

  void DBGWConnectorParser::parseDBInfo(DBGWExpatXMLProperties &properties)
  {
    if (getParentElementName() != XML_NODE_GROUP)
      {
        return;
      }

    bExistDbInfo = true;
    m_dbInfoMap.clear();
    m_dbInfoMap[XML_NODE_DBINFO_PROP_DBNAME] = properties.get(
        XML_NODE_DBINFO_PROP_DBNAME, true);
    m_dbInfoMap[XML_NODE_DBINFO_PROP_DBUSER] = properties.get(
        XML_NODE_DBINFO_PROP_DBUSER, true);
    m_dbInfoMap[XML_NODE_DBINFO_PROP_DBPASSWD] = properties.get(
        XML_NODE_DBINFO_PROP_DBPASSWD, true);
  }

  void DBGWConnectorParser::parseHost(DBGWExpatXMLProperties &properties)
  {
    if (getParentElementName() != XML_NODE_GROUP)
      {
        return;
      }

    if (bExistDbInfo == false)
      {
        NotExistNodeInXmlException e(XML_NODE_DBINFO, getFileName().c_str());
        DBGW_LOG_ERROR(e.what());
        throw e;
      }

    m_pHost = DBGWHostSharedPtr(
        new DBGWHost(properties.get(XML_NODE_HOST_PROP_ADDRESS, true),
            properties.getInt(XML_NODE_HOST_PROP_PORT, true),
            properties.getInt(XML_NODE_HOST_PROP_WEIGHT, true),
            m_dbInfoMap));
    m_pGroup->addHost(m_pHost);
  }

  void DBGWConnectorParser::parseAltHost(DBGWExpatXMLProperties &properties)
  {
    if (getParentElementName() != XML_NODE_HOST)
      {
        return;
      }

    m_pHost->setAltHost(properties.get(XML_NODE_HOST_PROP_ADDRESS, true),
        properties.get(XML_NODE_HOST_PROP_PORT, true));
  }

  DBGWQueryMapContext::DBGWQueryMapContext(const string &fileName,
      DBGWQueryMapperSharedPtr pQueryMapper) :
    m_fileName(fileName), m_pQueryMapper(pQueryMapper), m_globalGroupName(""),
    m_localGroupName(""), m_sqlName(""), m_queryType(DBGW_QUERY_TYPE_UNDEFINED),
    m_bExistQueryInSql(false), m_bExistQueryInGroup(false)
  {
  }

  DBGWQueryMapContext::~DBGWQueryMapContext()
  {
  }

  void DBGWQueryMapContext::clear()
  {
    m_globalGroupName = "";
    m_localGroupName = "";
    m_sqlName = "";
    m_queryType = DBGW_QUERY_TYPE_UNDEFINED;
    m_paramIndexList.clear();
    m_queryParamList.clear();
    m_queryBuffer.seekg(ios_base::beg);
    m_queryBuffer.seekp(ios_base::beg);
    m_resultIndexList.clear();
    m_userDefinedMetaList.clear();
    m_bExistQueryInSql = false;
    m_bExistQueryInGroup = false;
  }

  void DBGWQueryMapContext::checkAndClearSql()
  {
    if (m_bExistQueryInSql == false)
      {
        NotExistNodeInXmlException e(XML_NODE_30_CDATA, m_fileName.c_str());
        DBGW_LOG_ERROR(e.what());
        throw e;
      }

    m_localGroupName = "";
    m_sqlName = "";
    m_queryType = DBGW_QUERY_TYPE_UNDEFINED;
    m_paramIndexList.clear();
    m_queryParamList.clear();
    m_queryBuffer.seekg(ios_base::beg);
    m_queryBuffer.seekp(ios_base::beg);
    m_resultIndexList.clear();
    m_userDefinedMetaList.clear();
    m_bExistQueryInSql = false;
  }

  void DBGWQueryMapContext::checkAndClearGroup()
  {
    if (m_bExistQueryInGroup == false)
      {
        NotExistNodeInXmlException e(XML_NODE_30_CDATA, m_fileName.c_str());
        DBGW_LOG_ERROR(e.what());
        throw e;
      }

    m_localGroupName = "";
    m_queryBuffer.seekg(ios_base::beg);
    m_queryBuffer.seekp(ios_base::beg);
    m_bExistQueryInGroup = false;
  }

  void DBGWQueryMapContext::setFileName(const string &fileName)
  {
    m_fileName = fileName;
  }

  void DBGWQueryMapContext::setGlobalGroupName(const char *szGlobalGroupName)
  {
    m_globalGroupName = szGlobalGroupName;
  }

  void DBGWQueryMapContext::setLocalGroupName(const char *szLocalGroupName)
  {
    m_localGroupName = szLocalGroupName;
  }

  void DBGWQueryMapContext::setQueryType(DBGWQueryType queryType)
  {
    m_queryType = queryType;
  }

  void DBGWQueryMapContext::setSqlName(const char *szSqlName)
  {
    m_sqlName = szSqlName;
  }

  void DBGWQueryMapContext::setParameter(const string &name, int nIndex,
      DBGWValueType valueType, DBGWBindMode mode)
  {
    DBGWQueryParameter stParam;
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
    m_queryParamList.push_back(stParam);
  }

  void DBGWQueryMapContext::setParameter(const char *szMode, const char *szName,
      DBGWValueType valueType, DBGWBindMode mode)
  {
    if (isImplicitParamName(szName))
      {
        throw getLastException();
      }

    DBGWQueryParameter stParam;
    stParam.name = szName;
    stParam.type = valueType;
    stParam.mode = mode;
    stParam.index = m_queryParamList.size();
    stParam.firstPlaceHolderIndex = -1;
    m_queryParamList.push_back(stParam);
  }

  void DBGWQueryMapContext::setResult(const string &name, size_t nIndex,
      DBGWValueType valueType, int nLength)
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

    db::MetaData md;
    md.unused = false;
    md.name = name;
    md.colNo = nIndex;
    md.orgType = DBGW_VAL_TYPE_UNDEFINED;
    md.type = valueType;
    md.length = nLength;

    if (m_userDefinedMetaList.size() < nIndex)
      {
        m_userDefinedMetaList.resize(nIndex);
      }

    m_userDefinedMetaList[nIndex - 1] = md;
  }

  void DBGWQueryMapContext::appendQueryString(const char *szQueryString)
  {
    m_queryBuffer << szQueryString;
  }

  void DBGWQueryMapContext::addQuery()
  {
    string groupName = m_localGroupName;
    if (groupName == "")
      {
        groupName = m_globalGroupName;
      }

    if (groupName == "")
      {
        NotExistNodeInXmlException e(XML_NODE_30_GROUP, m_fileName.c_str());
        DBGW_LOG_ERROR(e.what());
        throw e;
      }

    string queryString = m_queryBuffer.str().substr(0, m_queryBuffer.tellp());

    if (m_pQueryMapper->getVersion() == DBGW_QUERY_MAP_VER_10
        && m_queryType == DBGW_QUERY_TYPE_UNDEFINED)
      {
        /**
         * in dbgw 1.0, we have to get query type to parse query string.
         */
        m_queryType = getQueryType(queryString.c_str());
      }

    DBGWStringList groupNameList;
    boost::split(groupNameList, groupName, boost::is_any_of(","));
    for (DBGWStringList::iterator it = groupNameList.begin(); it
        != groupNameList.end(); it++)
      {
        boost::trim(*it);
        DBGWQuerySharedPtr p(
            new DBGWQuery(m_pQueryMapper->getVersion(), m_fileName, queryString,
                m_sqlName, *it, m_queryType, m_queryParamList,
                m_userDefinedMetaList));
        m_pQueryMapper->addQuery(m_sqlName, p);
      }

    m_bExistQueryInGroup = true;
    m_bExistQueryInSql = true;
  }

  const string &DBGWQueryMapContext::getGlobalGroupName() const
  {
    return m_globalGroupName;
  }

  bool DBGWQueryMapContext::isExistQueryString() const
  {
    return !(stringstream::traits_type::eq_int_type(
        m_queryBuffer.rdbuf()->sgetc(), stringstream::traits_type::eof()));
  }

  DBGW10QueryMapParser::DBGW10QueryMapParser(const string &fileName,
      DBGWQueryMapContext &parserContext) :
    DBGWParser(fileName), m_parserContext(parserContext)
  {
  }

  DBGW10QueryMapParser::~DBGW10QueryMapParser()
  {
  }

  void DBGW10QueryMapParser::doOnElementStart(const XML_Char *szName,
      DBGWExpatXMLProperties &properties)
  {
    if (!strcasecmp(szName, XML_NODE_10_SQL))
      {
        parseSql(properties);
      }
    else if (!strcasecmp(szName, XML_NODE_10_PARAMETER))
      {
        parseParameter(properties);
      }
    else if (!strcasecmp(szName, XML_NODE_10_COLUMN))
      {
        parseColumn(properties);
      }
  }

  void DBGW10QueryMapParser::doOnElementEnd(const XML_Char *szName)
  {
    if (!strcasecmp(szName, XML_NODE_10_DEFINEDQUERY))
      {
        m_parserContext.clear();
      }
    else if (!strcasecmp(szName, XML_NODE_10_SQL))
      {
        if (m_parserContext.isExistQueryString())
          {
            m_parserContext.addQuery();
          }

        m_parserContext.checkAndClearSql();
      }
  }

  void DBGW10QueryMapParser::doOnElementContent(const XML_Char *szData, int nLength)
  {
    if (getParentElementName() != XML_NODE_10_QUERY)
      {
        return;
      }

    string data(szData);
    m_parserContext.appendQueryString(data.substr(0, nLength).c_str());
  }

  void DBGW10QueryMapParser::parseSql(DBGWExpatXMLProperties properties)
  {
    if (getParentElementName() != XML_NODE_10_DEFINEDQUERY)
      {
        return;
      }

    m_parserContext.setLocalGroupName("__FIRST__");
    m_parserContext.setSqlName(properties.get(XML_NODE_10_SQL_PROP_NAME, true));

    const char *pType = properties.getCString(XML_NODE_10_SQL_PROP_TYPE, false);
    if (pType != NULL)
      {
        if (!strcasecmp(pType, "selcet"))
          {
            m_parserContext.setQueryType(DBGW_QUERY_TYPE_SELECT);
          }
        else if (!strcasecmp(pType, "update") && !strcasecmp(pType, "delete")
            && !strcasecmp(pType, "insert"))
          {
            m_parserContext.setQueryType(DBGW_QUERY_TYPE_UPDATE);
          }
        else if (!strcasecmp(pType, "procedure"))
          {
            m_parserContext.setQueryType(DBGW_QUERY_TYPE_PROCEDURE);
          }
      }
  }

  void DBGW10QueryMapParser::parseParameter(DBGWExpatXMLProperties properties)
  {
    if (getParentElementName() != XML_NODE_10_PLACEHOLDER)
      {
        return;
      }

    m_parserContext.setParameter(
        properties.get(XML_NODE_10_PARAMETER_PROP_NAME, false),
        properties.getInt(XML_NODE_10_PARAMETER_PROP_INDEX, true),
        properties.get10ValueType(XML_NODE_10_PARAMETER_PROP_TYPE),
        properties.getBindMode(XML_NODE_10_PARAMETER_PROP_MODE));
  }

  void DBGW10QueryMapParser::parseColumn(DBGWExpatXMLProperties properties)
  {
    if (getParentElementName() != XML_NODE_10_RESULT)
      {
        return;
      }

    m_parserContext.setResult(
        properties.get(XML_NODE_10_COLUMN_PROP_NAME, true),
        properties.getInt(XML_NODE_10_COLUMN_PROP_INDEX, true),
        properties.get10ValueType(XML_NODE_10_COLUMN_PROP_TYPE),
        properties.getInt(XML_NODE_10_COLUMN_PROP_LENGTH, false));
  }

  DBGW20QueryMapParser::DBGW20QueryMapParser(const string &fileName,
      DBGWQueryMapContext &parserContext) :
    DBGWParser(fileName), m_parserContext(parserContext)
  {
  }

  DBGW20QueryMapParser::~DBGW20QueryMapParser()
  {
  }

  void DBGW20QueryMapParser::doOnElementStart(const XML_Char *szName,
      DBGWExpatXMLProperties &properties)
  {
    if (!strcasecmp(szName, XML_NODE_20_USING_DB))
      {
        parseUsingDB(properties);
      }
    else if (!strcasecmp(szName, XML_NODE_20_SELECT))
      {
        m_parserContext.setQueryType(DBGW_QUERY_TYPE_SELECT);
        parseSql(properties);
      }
    else if (!strcasecmp(szName, XML_NODE_20_PROCEDURE))
      {
        m_parserContext.setQueryType(DBGW_QUERY_TYPE_PROCEDURE);
        parseSql(properties);
      }
    else if (!strcasecmp(szName, XML_NODE_20_INSERT)
        || !strcasecmp(szName, XML_NODE_20_UPDATE)
        || !strcasecmp(szName, XML_NODE_20_DELETE))
      {
        m_parserContext.setQueryType(DBGW_QUERY_TYPE_UPDATE);
        parseSql(properties);
      }
    else if (!strcasecmp(szName, XML_NODE_20_PARAM))
      {
        parseParameter(properties);
      }
  }

  void DBGW20QueryMapParser::doOnElementEnd(const XML_Char *szName)
  {
    if (!strcasecmp(szName, XML_NODE_20_SQLS))
      {
        m_parserContext.clear();
      }
    else if (!strcasecmp(szName, XML_NODE_20_SELECT)
        || !strcasecmp(szName, XML_NODE_20_INSERT)
        || !strcasecmp(szName, XML_NODE_20_UPDATE)
        || !strcasecmp(szName, XML_NODE_20_DELETE)
        || !strcasecmp(szName, XML_NODE_20_PROCEDURE))
      {
        m_parserContext.checkAndClearSql();
      }
  }

  void DBGW20QueryMapParser::doOnElementContent(const XML_Char *szData, int nLength)
  {
    if (isCdataSection() == false)
      {
        return;
      }

    if (getParentElementName() != XML_NODE_20_SELECT
        && getParentElementName() != XML_NODE_20_INSERT
        && getParentElementName() != XML_NODE_20_UPDATE
        && getParentElementName() != XML_NODE_20_DELETE
        && getParentElementName() != XML_NODE_20_PROCEDURE)
      {
        return;
      }

    string data(szData);
    m_parserContext.appendQueryString(data.substr(0, nLength).c_str());
  }

  void DBGW20QueryMapParser::doOnCdataEnd()
  {
    if (m_parserContext.isExistQueryString())
      {
        m_parserContext.addQuery();
      }
  }

  void DBGW20QueryMapParser::parseUsingDB(DBGWExpatXMLProperties &properties)
  {
    if (getParentElementName() != XML_NODE_20_SQLS)
      {
        return;
      }

    string moduleID = properties.get(XML_NODE_20_USING_DB_PROP_MODULE_ID, true);
    if (properties.getBool(XML_NODE_20_USING_DB_PROP_DEFAULT, false)
        || m_parserContext.getGlobalGroupName() == "")
      {
        m_parserContext.setGlobalGroupName(moduleID.c_str());
      }

    m_moduleIDList.push_back(moduleID);
  }

  void DBGW20QueryMapParser::parseSql(DBGWExpatXMLProperties &properties)
  {
    if (getParentElementName() != XML_NODE_20_SQLS)
      {
        return;
      }

    m_parserContext.setSqlName(properties.get(XML_NODE_20_SQL_PROP_NAME, true));
    const char *szModuleID = properties.getCString(XML_NODE_20_SQL_PROP_DB, false);
    if (szModuleID != NULL)
      {
        m_parserContext.setLocalGroupName(szModuleID);
      }
  }

  void DBGW20QueryMapParser::parseParameter(DBGWExpatXMLProperties &properties)
  {
    if (getParentElementName() != XML_NODE_20_SELECT
        && getParentElementName() != XML_NODE_20_INSERT
        && getParentElementName() != XML_NODE_20_UPDATE
        && getParentElementName() != XML_NODE_20_DELETE
        && getParentElementName() != XML_NODE_20_PROCEDURE)
      {
        return;
      }

    m_parserContext.setParameter(
        properties.getCString(XML_NODE_20_PARAM_PROP_MODE, false),
        properties.get(XML_NODE_20_PARAM_PROP_NAME, true),
        properties.get20ValueType(XML_NODE_20_PARAM_PROP_TYPE),
        properties.getBindMode(XML_NODE_20_PARAM_PROP_MODE));
  }

  DBGW30QueryMapParser::DBGW30QueryMapParser(const string &fileName,
      DBGWQueryMapContext &parserContext) :
    DBGWParser(fileName), m_parserContext(parserContext)
  {
  }

  DBGW30QueryMapParser::~DBGW30QueryMapParser()
  {
  }

  void DBGW30QueryMapParser::doOnElementStart(const XML_Char *szName,
      DBGWExpatXMLProperties &properties)
  {
    if (!strcasecmp(szName, XML_NODE_30_SQLS))
      {
        parseSqls(properties);
      }
    else if (!strcasecmp(szName, XML_NODE_30_SELECT))
      {
        m_parserContext.setQueryType(DBGW_QUERY_TYPE_SELECT);
        parseSql(properties);
      }
    else if (!strcasecmp(szName, XML_NODE_30_PROCEDURE))
      {
        m_parserContext.setQueryType(DBGW_QUERY_TYPE_PROCEDURE);
        parseSql(properties);
      }
    else if (!strcasecmp(szName, XML_NODE_30_INSERT)
        || !strcasecmp(szName, XML_NODE_30_UPDATE)
        || !strcasecmp(szName, XML_NODE_30_DELETE))
      {
        m_parserContext.setQueryType(DBGW_QUERY_TYPE_UPDATE);
        parseSql(properties);
      }
    else if (!strcasecmp(szName, XML_NODE_30_PARAM))
      {
        parseParameter(properties);
      }
    else if (!strcasecmp(szName, XML_NODE_GROUP))
      {
        parseGroup(properties);
      }
  }

  void DBGW30QueryMapParser::doOnElementEnd(const XML_Char *szName)
  {
    if (!strcasecmp(szName, XML_NODE_30_SQLS))
      {
        m_parserContext.clear();
      }
    else if (!strcasecmp(szName, XML_NODE_30_SELECT)
        || !strcasecmp(szName, XML_NODE_30_INSERT)
        || !strcasecmp(szName, XML_NODE_30_UPDATE)
        || !strcasecmp(szName, XML_NODE_30_DELETE)
        || !strcasecmp(szName, XML_NODE_30_PROCEDURE))
      {
        m_parserContext.checkAndClearSql();
      }
    else if (!strcasecmp(szName, XML_NODE_30_GROUP))
      {
        m_parserContext.checkAndClearGroup();
      }
  }

  void DBGW30QueryMapParser::doOnElementContent(const XML_Char *szData, int nLength)
  {
    if (isCdataSection() == false)
      {
        return;
      }

    if (getParentElementName() != XML_NODE_30_SELECT
        && getParentElementName() != XML_NODE_30_INSERT
        && getParentElementName() != XML_NODE_30_UPDATE
        && getParentElementName() != XML_NODE_30_DELETE
        && getParentElementName() != XML_NODE_30_PROCEDURE
        && getParentElementName() != XML_NODE_30_GROUP)
      {
        return;
      }

    string data(szData);
    m_parserContext.appendQueryString(data.substr(0, nLength).c_str());
  }

  void DBGW30QueryMapParser::doOnCdataEnd()
  {
    if (m_parserContext.isExistQueryString())
      {
        m_parserContext.addQuery();
      }
  }

  void DBGW30QueryMapParser::parseSqls(DBGWExpatXMLProperties &properties)
  {
    if (!isRootElement())
      {
        return;
      }

    m_parserContext.setGlobalGroupName(
        properties.get(XML_NODE_30_SQLS_PROP_GROUP_NAME, false));
  }

  void DBGW30QueryMapParser::parseSql(DBGWExpatXMLProperties &properties)
  {
    if (getParentElementName() != XML_NODE_30_SQLS)
      {
        return;
      }

    m_parserContext.setSqlName(properties.get(XML_NODE_30_SQL_PROP_NAME, true));
  }

  void DBGW30QueryMapParser::parseParameter(DBGWExpatXMLProperties &properties)
  {
    if (getParentElementName() != XML_NODE_30_SELECT
        && getParentElementName() != XML_NODE_30_INSERT
        && getParentElementName() != XML_NODE_30_UPDATE
        && getParentElementName() != XML_NODE_30_DELETE
        && getParentElementName() != XML_NODE_30_PROCEDURE)
      {
        return;
      }

    m_parserContext.setParameter(properties.get(XML_NODE_30_PARAM_PROP_MODE, true),
        properties.get(XML_NODE_30_PARAM_PROP_NAME, true),
        properties.get30ValueType(XML_NODE_30_PARAM_PROP_TYPE),
        properties.getBindMode(XML_NODE_30_PARAM_PROP_MODE));
  }

  void DBGW30QueryMapParser::parseGroup(DBGWExpatXMLProperties &properties)
  {
    if (getParentElementName() != XML_NODE_30_SELECT
        && getParentElementName() != XML_NODE_30_INSERT
        && getParentElementName() != XML_NODE_30_UPDATE
        && getParentElementName() != XML_NODE_30_DELETE
        && getParentElementName() != XML_NODE_30_PROCEDURE)
      {
        return;
      }

    m_parserContext.setLocalGroupName(
        properties.get(XML_NODE_30_GROUP_PROP_NAME, true));
  }

  DBGWConfigurationParser::DBGWConfigurationParser(const string &fileName,
      DBGWConnectorSharedPtr pConnector) :
    DBGWParser(fileName), m_pConnector(pConnector)
  {
  }

  DBGWConfigurationParser::DBGWConfigurationParser(const string &fileName,
      DBGWQueryMapperSharedPtr pQueryMapper) :
    DBGWParser(fileName), m_pQueryMapper(pQueryMapper)
  {
  }

  void DBGWConfigurationParser::doOnElementStart(const XML_Char *szName,
      DBGWExpatXMLProperties &properties)
  {
    if (!strcasecmp(szName, XML_NODE_LOG))
      {
        parseLog(properties);
      }
    else if (!strcasecmp(szName, XML_NODE_QUERYMAP))
      {
        parseQueryMap(properties);
      }
    else if (!strcasecmp(szName, XML_NODE_INCLUDE))
      {
        parseInclude(properties);
      }
  }

  void DBGWConfigurationParser::doOnElementEnd(const XML_Char *szName)
  {
  }

  void DBGWConfigurationParser::parseQueryMap(DBGWExpatXMLProperties &properties)
  {
    if (getParentElementName() != XML_NODE_CONFIGURATION || m_pQueryMapper == NULL)
      {
        return;
      }

    string version = properties.get(XML_NODE_QUERYMAP_PROP_VERSION, false);
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

  void DBGWConfigurationParser::parseLog(DBGWExpatXMLProperties &properties)
  {
    if (getParentElementName() != XML_NODE_CONFIGURATION)
      {
        return;
      }

    DBGWLogger::setLogPath(properties.getCString(XML_NODE_LOG_PROP_PATH, false));
    DBGWLogger::setLogLevel(properties.getLogLevel(XML_NODE_LOG_PROP_LEVEL));
    DBGWLogger::setForceFlush(
        properties.getBool(XML_NODE_LOG_PROP_FORCE_FLUSH, false));
  }

  void DBGWConfigurationParser::parseInclude(DBGWExpatXMLProperties &properties)
  {
    if (getParentElementName() != XML_NODE_CONNECTOR
        && getParentElementName() != XML_NODE_QUERYMAP)
      {
        return;
      }

    if (getParentElementName() == XML_NODE_CONNECTOR && m_pConnector == NULL)
      {
        return;
      }

    if (getParentElementName() == XML_NODE_QUERYMAP && m_pQueryMapper == NULL)
      {
        return;
      }

    const char *szFile = properties.getCString(XML_NODE_INCLUDE_PROP_FILE, false);
    const char *szPath = properties.getCString(XML_NODE_INCLUDE_PROP_PATH, false);
    string fileName;
    if (szFile != NULL)
      {
        system::DirectorySharedPtr pDir = system::DirectoryFactory::create(
            szFile);
        if (pDir->isDirectory())
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
        system::DirectorySharedPtr pDir = system::DirectoryFactory::create(
            szPath);
        if (!pDir->isDirectory())
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

    if (getParentElementName() == XML_NODE_CONNECTOR)
      {
        if (m_pConnector != NULL)
          {
            DBGWConnectorParser parser(fileName, m_pConnector);
            DBGWParser::parse(&parser);
          }
      }
    else if (getParentElementName() == XML_NODE_QUERYMAP)
      {
        if (m_pQueryMapper == NULL)
          {
            return;
          }

        parseQueryMapper(fileName, m_pQueryMapper);
      }
  }

  void parseQueryMapper(const string &fileName,
      DBGWQueryMapperSharedPtr pQueryMaper)
  {
    DBGWQueryMapContext context(fileName, pQueryMaper);

    if (pQueryMaper->getVersion() == DBGW_QUERY_MAP_VER_10)
      {
        DBGW10QueryMapParser parser(fileName, context);
        DBGWParser::parse(&parser);
      }
    else if (pQueryMaper->getVersion() == DBGW_QUERY_MAP_VER_20)
      {
        DBGW20QueryMapParser parser(fileName, context);
        DBGWParser::parse(&parser);
      }
    else
      {
        DBGW30QueryMapParser parser(fileName, context);
        DBGWParser::parse(&parser);
      }
  }

}
