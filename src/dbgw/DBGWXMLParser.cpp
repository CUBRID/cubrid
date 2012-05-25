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
#include <expat.h>
#include <fstream>
#include <errno.h>
#include <boost/algorithm/string.hpp>
#include "DBGWCommon.h"
#include "DBGWError.h"
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

  static const char *XML_NODE_SERVICE = "service";
  static const char *XML_NODE_SERVICE_PROP_NAMESPACE = "namespace";
  static const char *XML_NODE_SERVICE_PROP_DESCRIPTION = "description";
  static const char *XML_NODE_SERVICE_PROP_VALIDATE_RESULT = "validate-result";

  static const char *XML_NODE_GROUP = "group";
  static const char *XML_NODE_GROUP_PROP_NAME = "name";
  static const char *XML_NODE_GROUP_PROP_DESCRIPTION = "description";
  static const char *XML_NODE_GROUP_PROP_INACTIVATE = "inactivate";
  static const char *XML_NODE_GROUP_PROP_IGNORE_RESULT = "ignore_result";

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

  static const char *XML_NODE_SQLS = "sqls";
  static const char *XML_NODE_SQLS_PROP_GROUP_NAME = "group-name";

  static const char *XML_NODE_SELECT = "select";
  static const char *XML_NODE_INSERT = "insert";
  static const char *XML_NODE_UPDATE = "update";
  static const char *XML_NODE_DELETE = "delete";
  static const char *XML_NODE_SQL_PROP_NAME = "name";

  static const char *XML_NODE_PARAM = "param";
  static const char *XML_NODE_PARAM_PROP_NAME = "name";
  static const char *XML_NODE_PARAM_PROP_TYPE = "type";
  static const char *XML_NODE_PARAM_PROP_MODE = "mode";

  static const char *XML_NODE_CDATA = "cdata";

  static const char *XML_NODE_LOG = "log";
  static const char *XML_NODE_LOG_PROP_LEVEL = "level";
  static const char *XML_NODE_LOG_PROP_PATH = "path";
  static const char *XML_NODE_LOG_PROP_FORCE_FLUSH = "force-flush";

  static const char *XML_NODE_INCLUDE = "include";
  static const char *XML_NODE_INCLUDE_PROP_FILE = "file";

  static const int POOL_DEFAULT_POOL_SIZE = 10;

  DBGWExpatXMLParser::DBGWExpatXMLParser(const string &fileName)
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

  DBGWExpatXMLProperties::DBGWExpatXMLProperties(const string &nodeName,
      const XML_Char *szAttr[]) :
    m_nodeName(nodeName), m_pAttr(szAttr)
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
        NotExistPropertyException e(m_nodeName.c_str(), szName);
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
        NotExistPropertyException e(m_nodeName.c_str(), szName);
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
        NotExistPropertyException e(m_nodeName.c_str(), szName);
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
        NotExistPropertyException e(m_nodeName.c_str(), szName);
        DBGW_LOG_ERROR(e.what());
        throw e;
      }
    else
      {
        return false;
      }
  }

  DBGWValueType DBGWExpatXMLProperties::getValueType(const char *szName)
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
    InvalidValueTypeException e(szType);
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

    InvalidPropertyValueException e(szLogLevel, "OFF|ERROR|WARNING|INFO|DEBUG");
    DBGW_LOG_ERROR(e.what());
    throw e;
  }

  int DBGWExpatXMLProperties::propertyToInt(const char *szProperty)
  {
    try
      {
        return boost::lexical_cast<int>(szProperty);
      }
    catch (boost::bad_lexical_cast &e)
      {
        InvalidPropertyValueException e(szProperty, "NUMERIC");
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
        InvalidPropertyValueException e(szProperty, "TRUE|FALSE");
        DBGW_LOG_ERROR(e.what());
        throw e;
      }
  }

  DBGWParser::DBGWParser(const string &fileName) :
    m_fileName(fileName), m_bCdataSection(false)
  {
  }

  DBGWParser::~DBGWParser()
  {
  }

  const string &DBGWParser::getFileName() const
  {
    return m_fileName;
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

  void DBGWParser::parse(DBGWParser *pParser)
  {
    DBGWExpatXMLParser parser(pParser->m_fileName);

    XML_SetUserData(parser.get(), pParser);
    XML_SetElementHandler(parser.get(), onElementStart, onElementEnd);
    XML_SetCdataSectionHandler(parser.get(), onCdataStart, onCdataEnd);
    XML_SetCharacterDataHandler(parser.get(), onElementContent);
    XML_SetUnknownEncodingHandler(parser.get(), onUnknownEncoding, NULL);

    FILE *fp = fopen(pParser->getFileName().c_str(), "r");
    if (fp == NULL)
      {
        CreateFailParserExeception e(pParser->getFileName().c_str());
        DBGW_LOG_ERROR("%s (%d)", e.what(), errno);
        throw e;
      }

    DBGWInterfaceException exception;
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
            pParser->m_fileName.c_str());
        DBGW_LOG_ERROR(e.what());
        throw e;
      }

    if (exception.getErrorCode() != DBGWErrorCode::NO_ERROR)
      {
        throw exception;
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

  void DBGWParser::onElementStart(void *pParam, const XML_Char *szName,
      const XML_Char *szAttr[])
  {
    DBGWParser *pParser = (DBGWParser *) pParam;
    DBGWExpatXMLProperties properties(szName, szAttr);

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
    DBGWParser(fileName), m_pConnector(pConnector), m_pDBnfoMap(NULL),
    m_nPoolSize(POOL_DEFAULT_POOL_SIZE)
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
        if (m_pDBnfoMap != NULL)
          {
            delete m_pDBnfoMap;
          }
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

    m_pService = DBGWServiceSharedPtr(
        new DBGWService(
            getFileName(),
            properties. get(XML_NODE_SERVICE_PROP_NAMESPACE, true),
            properties. get(XML_NODE_SERVICE_PROP_DESCRIPTION, false),
            properties. getBool(XML_NODE_SERVICE_PROP_VALIDATE_RESULT,
                false)));
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
            properties. getBool(XML_NODE_GROUP_PROP_IGNORE_RESULT,
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

    m_pDBnfoMap = new DBGWDBInfoHashMap();
    (*m_pDBnfoMap)[XML_NODE_DBINFO_PROP_DBNAME] = properties.get(
        XML_NODE_DBINFO_PROP_DBNAME, true);
    (*m_pDBnfoMap)[XML_NODE_DBINFO_PROP_DBUSER] = properties.get(
        XML_NODE_DBINFO_PROP_DBUSER, true);
    (*m_pDBnfoMap)[XML_NODE_DBINFO_PROP_DBPASSWD] = properties.get(
        XML_NODE_DBINFO_PROP_DBPASSWD, true);
  }

  void DBGWConnectorParser::parseHost(DBGWExpatXMLProperties &properties)
  {
    if (getParentElementName() != XML_NODE_GROUP)
      {
        return;
      }

    if (m_pDBnfoMap == NULL)
      {
        NotExistNodeInXmlException e(XML_NODE_DBINFO, getFileName().c_str());
        DBGW_LOG_ERROR(e.what());
        throw e;
      }

    m_pHost = DBGWHostSharedPtr(
        new DBGWHost(properties.get(XML_NODE_HOST_PROP_ADDRESS, true),
            properties.getInt(XML_NODE_HOST_PROP_PORT, true),
            properties.getInt(XML_NODE_HOST_PROP_WEIGHT, true),
            *m_pDBnfoMap));
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

  DBGWQueryMapParser::DBGWQueryMapParser(const string &fileName,
      DBGWQueryMapperSharedPtr pQueryMapper) :
    DBGWParser(fileName), m_pQueryMapper(pQueryMapper),
    m_bExistQueryInSql(false), m_bExistQueryInGroup(false),
    m_nQueryLen(0)
  {
  }

  DBGWQueryMapParser::~DBGWQueryMapParser()
  {
  }

  void DBGWQueryMapParser::doOnElementStart(const XML_Char *szName,
      DBGWExpatXMLProperties &properties)
  {
    if (!strcasecmp(szName, XML_NODE_SQLS))
      {
        parseSqls(properties);
      }
    else if (!strcasecmp(szName, XML_NODE_SELECT))
      {
        m_queryType = DBGWQueryType::SELECT;
        parseSql(properties);
      }
    else if (!strcasecmp(szName, XML_NODE_INSERT) || !strcasecmp(szName,
        XML_NODE_UPDATE) || !strcasecmp(szName, XML_NODE_DELETE))
      {
        m_queryType = DBGWQueryType::DML;
        parseSql(properties);
      }
    else if (!strcasecmp(szName, XML_NODE_PARAM))
      {
        parseParameter(properties);
      }
    else if (!strcasecmp(szName, XML_NODE_GROUP))
      {
        parseGroup(properties);
      }
  }

  void DBGWQueryMapParser::doOnElementEnd(const XML_Char *szName)
  {
    if (!strcasecmp(szName, XML_NODE_SQLS))
      {
        m_globalGroupName = "";
      }
    else if (!strcasecmp(szName, XML_NODE_SELECT) || !strcasecmp(szName,
        XML_NODE_INSERT) || !strcasecmp(szName, XML_NODE_UPDATE)
        || !strcasecmp(szName, XML_NODE_DELETE))
      {
        m_sqlName = "";
        m_inQueryParamMap.clear();
        m_outQueryParamMap.clear();

        if (m_bExistQueryInSql == false)
          {
            NotExistNodeInXmlException e(XML_NODE_CDATA, getFileName().c_str());
            DBGW_LOG_ERROR(e.what());
            throw e;
          }

        m_bExistQueryInSql = false;
      }
    else if (!strcasecmp(szName, XML_NODE_GROUP))
      {
        m_localGroupName = "";

        if (m_bExistQueryInGroup == false)
          {
            NotExistNodeInXmlException e(XML_NODE_CDATA, getFileName().c_str());
            DBGW_LOG_ERROR(e.what());
            throw e;
          }

        m_bExistQueryInGroup = false;
      }
  }

  void DBGWQueryMapParser::doOnElementContent(const XML_Char *szData, int nLength)
  {
    if (isCdataSection() == false)
      {
        return;
      }

    if (getParentElementName() != XML_NODE_SELECT && getParentElementName()
        != XML_NODE_INSERT && getParentElementName() != XML_NODE_UPDATE
        && getParentElementName() != XML_NODE_DELETE
        && getParentElementName() != XML_NODE_GROUP)
      {
        return;
      }

    memcpy(m_szQueryBuffer + m_nQueryLen, szData, nLength);
    m_nQueryLen += nLength;
    m_szQueryBuffer[m_nQueryLen] = '\0';
  }

  void DBGWQueryMapParser::doOnCdataEnd()
  {
    if (m_nQueryLen > 0)
      {
        string groupName = m_localGroupName;
        if (groupName == "")
          {
            groupName = m_globalGroupName;
          }

        if (groupName == "")
          {
            NotExistNodeInXmlException e(XML_NODE_GROUP, getFileName().c_str());
            DBGW_LOG_ERROR(e.what());
            throw e;
          }

        DBGWStringList groupNameList;
        boost::split(groupNameList, groupName, boost::is_any_of(","));
        for (DBGWStringList::iterator it = groupNameList.begin(); it
            != groupNameList.end(); it++)
          {
            boost::trim(*it);
            DBGWQuerySharedPtr p(
                new DBGWQuery(getFileName(), m_szQueryBuffer, m_sqlName,
                    *it, m_queryType, m_inQueryParamMap,
                    m_outQueryParamMap));
            m_pQueryMapper->addQuery(m_sqlName, p);
          }

        m_bExistQueryInSql = true;
        m_bExistQueryInGroup = true;

        m_nQueryLen = 0;
      }
  }

  void DBGWQueryMapParser::parseSqls(DBGWExpatXMLProperties &properties)
  {
    if (!isRootElement())
      {
        return;
      }

    m_globalGroupName = properties.get(XML_NODE_SQLS_PROP_GROUP_NAME, false);
  }

  void DBGWQueryMapParser::parseSql(DBGWExpatXMLProperties &properties)
  {
    if (getParentElementName() != XML_NODE_SQLS)
      {
        return;
      }

    m_sqlName = properties.get(XML_NODE_SQL_PROP_NAME, true);
  }

  void DBGWQueryMapParser::parseParameter(DBGWExpatXMLProperties &properties)
  {
    if (getParentElementName() != XML_NODE_SELECT && getParentElementName()
        != XML_NODE_INSERT && getParentElementName() != XML_NODE_UPDATE
        && getParentElementName() != XML_NODE_DELETE)
      {
        return;
      }

    DBGWQueryParameter stParam;
    stParam.name = properties.get(XML_NODE_PARAM_PROP_NAME, true);
    stParam.type = properties.getValueType(XML_NODE_PARAM_PROP_TYPE);

    const char *szMode = properties.get(XML_NODE_PARAM_PROP_MODE, true);
    if (!strcasecmp(szMode, "in"))
      {
        stParam.nIndex = m_inQueryParamMap.size();
        m_inQueryParamMap[stParam.name] = stParam;
      }
    else if (!strcasecmp(szMode, "out"))
      {
        stParam.nIndex = m_outQueryParamMap.size();
        m_outQueryParamMap[stParam.name] = stParam;
      }
    else
      {
        InvalidPropertyValueException e(szMode, "IN|OUT");
        DBGW_LOG_ERROR(e.what());
        throw e;
      }
  }

  void DBGWQueryMapParser::parseGroup(DBGWExpatXMLProperties &properties)
  {
    if (getParentElementName() != XML_NODE_SELECT && getParentElementName()
        != XML_NODE_INSERT && getParentElementName() != XML_NODE_UPDATE
        && getParentElementName() != XML_NODE_DELETE)
      {
        return;
      }

    m_localGroupName = properties.get(XML_NODE_GROUP_PROP_NAME, true);
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
    if (!strcasecmp(szName, XML_NODE_INCLUDE))
      {
        parseInclude(properties);
      }
  }

  void DBGWConfigurationParser::doOnElementEnd(const XML_Char *szName)
  {
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
    if (getParentElementName() == XML_NODE_CONNECTOR)
      {
        if (m_pConnector != NULL)
          {
            DBGWConnectorParser parser(
                properties.get(XML_NODE_INCLUDE_PROP_FILE, true),
                m_pConnector);
            DBGWParser::parse(&parser);
          }
      }
    else if (getParentElementName() == XML_NODE_QUERYMAP)
      {
        if (m_pQueryMapper != NULL)
          {
            DBGWQueryMapParser parser(
                properties.get(XML_NODE_INCLUDE_PROP_FILE, true),
                m_pQueryMapper);
            DBGWParser::parse(&parser);
          }
      }
  }

}
