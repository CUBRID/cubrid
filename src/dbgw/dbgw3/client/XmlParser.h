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

#ifndef XMLPARSER_H_
#define XMLPARSER_H_

#if defined(WINDOWS) || defined(_WIN32) || defined(_WIN64)
#include <expat/expat.h>
#else /* WINDOWS */
#include <expat.h>
#endif /* !WINDOWS */

namespace dbgw
{

  class _Connector;
  class _Service;
  class _Host;
  class _Group;
  class _QueryMapper;
  class _Configuration;

  class _XmlParser
  {
  public:
    _XmlParser(const std::string &fileName);
    virtual ~_XmlParser() {}

    virtual void parse() = 0;

  public:
    void setRealFileName(const std::string &realFileName);

  public:
    const std::string &getFileName() const;
    const std::string &getRealFileName() const;

  protected:
    virtual void doSetRealFileName();

  private:
    std::string m_fileName;
    std::string m_realFileName;
  };

  class _ExpatXMLParser
  {
  public:
    _ExpatXMLParser(const std::string &fileName);
    virtual ~_ExpatXMLParser();

  public:
    XML_Parser get() const;
    const char *getFileName() const;

  private:
    XML_Parser m_pParser;
    std::string m_fileName;
  };

  class _ExpatXMLProperties
  {
  public:
    _ExpatXMLProperties(const _ExpatXMLParser &xmlParser,
        const std::string &nodeName, const XML_Char *szAttr[]);
    virtual ~ _ExpatXMLProperties();

    const char *get(const char *szName, bool bRequired);
    const char *get(const char *szName, const char *szHiddenName,
        bool bRequired);
    const char *getCString(const char *szName, bool bRequired);
    const char *getCString(const char *szName, const char *szHiddenName,
        bool bRequired);
    int getInt(const char *szName, bool bRequired, int nDefault = 0);
    int getInt(const char *szName, const char *szHiddenName,
        bool bRequired, int nDefault = 0);
    long getLong(const char *szName, bool bRequired,
        unsigned long ulDefault = 0l);
    long getLong(const char *szName, const char *szHiddenName,
        bool bRequired, unsigned long ulDefault = 0l);
    bool getBool(const char *szName, bool bRequired);
    bool getBool(const char *szName, const char *szHiddenName,
        bool bRequired);
    void getValidateResult(const char *szName, const char *szHiddenName,
        bool bValidateResult[]);
    ValueType get30ValueType(const char *szName);
    ValueType get20ValueType(const char *szName);
    ValueType get10ValueType(const char *szName);
    CCI_LOG_LEVEL getLogLevel(const char *szName);
    CCI_LOG_POSTFIX getLogPostfix(const char *szName);
    sql::ParameterMode getBindMode(const char *szMode);

  public:
    const char *getNodeName() const;

  private:
    int propertyToInt(const char *szProperty);
    long propertyToLong(const char *szProperty);
    bool propertyToBoolean(const char *szProperty);

  private:
    const _ExpatXMLParser &m_xmlParser;
    std::string m_nodeName;
    const XML_Char **m_pAttr;
  };

  class _BaseXmlParser : public _XmlParser
  {
  public:
    _BaseXmlParser(const std::string &fileName);
    virtual ~ _BaseXmlParser();

  public:
    virtual void parse();

  protected:
    virtual void doOnElementStart(const XML_Char *szName,
        _ExpatXMLProperties &properties) = 0;
    virtual void doOnElementEnd(const XML_Char *szName) = 0;
    virtual void doOnElementContent(const XML_Char *szData, int nLength);
    virtual void doOnCdataStart();
    virtual void doOnCdataEnd();
    void abort();

  protected:
    const std::string &getParentElementName() const;
    bool isRootElement() const;
    bool isCdataSection() const;

  private:
    bool isAborted() const;
    static void onElementStart(void *pParam, const XML_Char *szName,
        const XML_Char *szAttr[]);
    static void onElementEnd(void *pParam, const XML_Char *szName);
    static void onElementContent(void *pParam, const XML_Char *szData,
        int nLength);
    static void onCdataStart(void *pParam);
    static void onCdataEnd(void *pParam);
    static int onUnknownEncoding(void *pParam, const XML_Char *szName,
        XML_Encoding *pInfo);
    static int convertCharacterSet(void *pParam, const char *szStr);

  private:
    std::stack<std::string> m_elementStack;
    bool m_bCdataSection;
    bool m_bAborted;
  };

  class _ConnectorParser: public _BaseXmlParser
  {
  public:
    _ConnectorParser(const std::string &fileName,
        _Connector *pConnector);
    virtual ~ _ConnectorParser();

  protected:
    virtual void doOnElementStart(const XML_Char *szName,
        _ExpatXMLProperties &properties);
    virtual void doOnElementEnd(const XML_Char *szName);

  private:
    _ConnectorParser(const _ConnectorParser &);
    _ConnectorParser &operator=(const _ConnectorParser &);

  private:
    class Impl;
    Impl *m_pImpl;
  };

  class _QueryMapContext
  {
  public:
    _QueryMapContext(const std::string &fileName,
        _QueryMapper *pQueryMapper);
    virtual ~_QueryMapContext();

    void clear();
    void checkAndClearSql();
    void checkAndClearGroup();
    void setFileName(const std::string &fileName);
    void setGlobalGroupName(const char *szGlobalGroupName);
    void setLocalGroupName(const char *szLocalGroupName);
    void setStatementType(sql::StatementType statementType);
    void setSqlName(const char *szSqlName);
    void setParameter(const std::string &name, int nIndex, ValueType valueType,
        sql::ParameterMode mode, int nSize);
    void setParameter(const char *szMode, const char *szName,
        ValueType valueType, sql::ParameterMode mode, int nSize);
    void setResult(const std::string &name, size_t nIndex, ValueType valueType,
        int nLength);
    void appendQueryString(const char *szQueryString);
    void addQuery();

  public:
    const std::string &getGlobalGroupName() const;
    bool isExistQueryString() const;

  private:
    _QueryMapContext(const _QueryMapContext &);
    _QueryMapContext &operator=(const _QueryMapContext &);

  private:
    class Impl;
    Impl *m_pImpl;
  };

  class _10QueryMapParser : public _BaseXmlParser
  {
  public:
    _10QueryMapParser(const std::string &fileName,
        _QueryMapContext &parserContext);
    virtual ~_10QueryMapParser();

  protected:
    virtual void doOnElementStart(const XML_Char *szName,
        _ExpatXMLProperties &properties);
    virtual void doOnElementEnd(const XML_Char *szName);
    virtual void doOnElementContent(const XML_Char *szData, int nLength);
    virtual void doSetRealFileName();

  private:
    void parseSql(_ExpatXMLProperties properties);
    void parseParameter(_ExpatXMLProperties properties);
    void parseColumn(_ExpatXMLProperties properties);

  private:
    _QueryMapContext &m_parserContext;
  };

  class _20QueryMapParser : public _BaseXmlParser
  {
  public:
    _20QueryMapParser(const std::string &fileName,
        _QueryMapContext &parserContext);
    virtual ~_20QueryMapParser();

  protected:
    virtual void doOnElementStart(const XML_Char *szName,
        _ExpatXMLProperties &properties);
    virtual void doOnElementEnd(const XML_Char *szName);
    virtual void doOnElementContent(const XML_Char *szData, int nLength);
    virtual void doOnCdataEnd();
    virtual void doSetRealFileName();

  private:
    void parseUsingDB(_ExpatXMLProperties &properties);
    void parseSql(_ExpatXMLProperties &properties);
    void parseParameter(_ExpatXMLProperties &properties);

  private:
    _QueryMapContext &m_parserContext;
    std::vector<std::string> m_moduleIDList;
  };

  class _30QueryMapParser : public _BaseXmlParser
  {
  public:
    _30QueryMapParser(const std::string &fileName,
        _QueryMapContext &parserContext);
    virtual ~_30QueryMapParser();

  protected:
    virtual void doOnElementStart(const XML_Char *szName,
        _ExpatXMLProperties &properties);
    virtual void doOnElementEnd(const XML_Char *szName);
    virtual void doOnElementContent(const XML_Char *szData, int nLength);
    virtual void doOnCdataEnd();
    virtual void doSetRealFileName();

  private:
    void parseSqls(_ExpatXMLProperties &properties);
    void parseSql(_ExpatXMLProperties &properties);
    void parseParameter(_ExpatXMLProperties &properties);
    void parseGroup(_ExpatXMLProperties &properties);

  private:
    _QueryMapContext &m_parserContext;
  };

  class _ConfigurationParser: public _BaseXmlParser
  {
  public:
    /**
     * parse connector.xml
     */
    _ConfigurationParser(Configuration &configuraion,
        const std::string &fileName, _Connector *pConnector);
    /**
     * parse querymap.xml
     */
    _ConfigurationParser(Configuration &configuraion,
        const std::string &fileName, _QueryMapper *pQueryMapper);
    /**
     * parse connector.xml and querymap.xml
     */
    _ConfigurationParser(Configuration &configuraion,
        const std::string &fileName, _Connector *pConnector,
        _QueryMapper *pQueryMapper);

  protected:
    virtual void doOnElementStart(const XML_Char *szName,
        _ExpatXMLProperties &properties);
    virtual void doOnElementEnd(const XML_Char *szName);

  private:
    void parseConfiguration(_ExpatXMLProperties &properties);
    void parseQueryMap(_ExpatXMLProperties &properties);
    void parseLog(_ExpatXMLProperties &properties);
    void parseWorker(_ExpatXMLProperties &properties);
    void parseInclude(_ExpatXMLProperties &properties);
    void parseStatistics(_ExpatXMLProperties &properties);

  private:
    Configuration &m_configuration;
    _Connector *m_pConnector;
    _QueryMapper *m_pQueryMapper;
    unsigned long m_ulMaxWaitExitTimeMilSec;
  };

  void parseXml(_XmlParser *pParser);
  void parseQueryMapper(const std::string &fileName,
      _QueryMapper *pQueryMaper);

}

#endif
