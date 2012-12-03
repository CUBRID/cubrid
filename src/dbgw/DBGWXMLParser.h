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

#ifndef DBGWXMLPARSER_H_
#define DBGWXMLPARSER_H_

using namespace std;

namespace dbgw
{

  class _DBGWExpatXMLParser
  {
  public:
    _DBGWExpatXMLParser(const string &fileName);
    virtual ~_DBGWExpatXMLParser();

  public:
    XML_Parser get() const;
    const char *getFileName() const;

  private:
    XML_Parser m_pParser;
    string m_fileName;
  };

  class _DBGWExpatXMLProperties
  {
  public:
    _DBGWExpatXMLProperties(const _DBGWExpatXMLParser &xmlParser,
        const string &nodeName, const XML_Char *szAttr[]);
    virtual ~ _DBGWExpatXMLProperties();

    const char *get(const char *szName, bool bRequired);
    const char *get(const char *szName, const char *szHiddenName,
        bool bRequired);
    const char *getCString(const char *szName, bool bRequired);
    const char *getCString(const char *szName, const char *szHiddenName,
        bool bRequired);
    int getInt(const char *szName, bool bRequired, int nDefault = 0);
    int getInt(const char *szName, const char *szHiddenName,
        bool bRequired, int nDefault = 0);
    long getLong(const char *szName, bool bRequired, int nDefault = 0);
    long getLong(const char *szName, const char *szHiddenName,
        bool bRequired, int nDefault = 0);
    bool getBool(const char *szName, bool bRequired);
    bool getBool(const char *szName, const char *szHiddenName,
        bool bRequired);
    void getValidateResult(const char *szName, const char *szHiddenName,
        bool bValidateResult[]);
    DBGWValueType get30ValueType(const char *szName);
    DBGWValueType get20ValueType(const char *szName);
    DBGWValueType get10ValueType(const char *szName);
    CCI_LOG_LEVEL getLogLevel(const char *szName);
    db::DBGWParameterMode getBindMode(const char *szMode);

  public:
    const char *getNodeName() const;

  private:
    int propertyToInt(const char *szProperty);
    long propertyToLong(const char *szProperty);
    bool propertyToBoolean(const char *szProperty);

  private:
    const _DBGWExpatXMLParser &m_xmlParser;
    string m_nodeName;
    const XML_Char **m_pAttr;
  };

  typedef stack<string> _DBGWExpatXMLElementStack;

  class _DBGWParser
  {
  public:
    _DBGWParser(const string &fileName);
    virtual ~ _DBGWParser();

    virtual void setRealFileName(const string &realFileName);

  public:
    static void parse(_DBGWParser *pParser);

  protected:
    virtual void doOnElementStart(const XML_Char *szName,
        _DBGWExpatXMLProperties &properties) = 0;
    virtual void doOnElementEnd(const XML_Char *szName) = 0;
    virtual void doOnElementContent(const XML_Char *szData, int nLength);
    virtual void doOnCdataStart();
    virtual void doOnCdataEnd();
    void abort();

  protected:
    const string &getFileName() const;
    const string &getRealFileName() const;
    const string &getParentElementName() const;
    bool isRootElement() const;
    bool isCdataSection() const;

  private:
    bool isAborted() const;
    static void doParse(_DBGWParser *pParser);
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
    string m_fileName;
    string m_realFileName;
    _DBGWExpatXMLElementStack m_elementStack;
    bool m_bCdataSection;
    bool m_bAborted;
  };

  class _DBGWConnectorParser: public _DBGWParser
  {
  public:
    _DBGWConnectorParser(const string &fileName,
        _DBGWConnectorSharedPtr pConnector);
    virtual ~ _DBGWConnectorParser();

  protected:
    virtual void doOnElementStart(const XML_Char *szName,
        _DBGWExpatXMLProperties &properties);
    virtual void doOnElementEnd(const XML_Char *szName);

  private:
    void parseService(_DBGWExpatXMLProperties &properties);
    void parseGroup(_DBGWExpatXMLProperties &properties);
    void parsePool(_DBGWExpatXMLProperties &properties);
    void parseDBInfo(_DBGWExpatXMLProperties &properties);
    void parseHost(_DBGWExpatXMLProperties &properties);
    void parseAltHost(_DBGWExpatXMLProperties &properties);

  private:
    _DBGWConnectorSharedPtr m_pConnector;
    _DBGWServiceSharedPtr m_pService;
    _DBGWGroupSharedPtr m_pGroup;
    _DBGWHostSharedPtr m_pHost;
    _DBGWExecutorPoolContext m_context;
    string m_url;
    string m_dbinfo;
  };

  class _DBGWQueryMapContext
  {
  public:
    _DBGWQueryMapContext(const string &fileName,
        _DBGWQueryMapperSharedPtr pQueryMapper);
    virtual ~_DBGWQueryMapContext();

    void clear();
    void checkAndClearSql();
    void checkAndClearGroup();
    void setFileName(const string &fileName);
    void setGlobalGroupName(const char *szGlobalGroupName);
    void setLocalGroupName(const char *szLocalGroupName);
    void setStatementType(db::DBGWStatementType statementType);
    void setSqlName(const char *szSqlName);
    void setParameter(const string &name, int nIndex, DBGWValueType valueType,
        db::DBGWParameterMode mode);
    void setParameter(const char *szMode, const char *szName,
        DBGWValueType valueType, db::DBGWParameterMode mode);
    void setResult(const string &name, size_t nIndex, DBGWValueType valueType,
        int nLength);
    void appendQueryString(const char *szQueryString);
    void addQuery();

  public:
    const string &getGlobalGroupName() const;
    bool isExistQueryString() const;

  private:
    string m_fileName;
    _DBGWQueryMapperSharedPtr m_pQueryMapper;
    string m_globalGroupName;
    string m_localGroupName;
    string m_sqlName;
    db::DBGWStatementType m_statementType;
    set<int> m_paramIndexList;
    _DBGWQueryParameterList m_queryParamList;
    set<int> m_resultIndexList;
    stringstream m_queryBuffer;
    _DBGWClientResultSetMetaDataRawList m_userDefinedMetaList;
    bool m_bExistQueryInSql;
    bool m_bExistQueryInGroup;
  };

  class _DBGW10QueryMapParser : public _DBGWParser
  {
  public:
    _DBGW10QueryMapParser(const string &fileName,
        _DBGWQueryMapContext &parserContext);
    virtual ~_DBGW10QueryMapParser();

  protected:
    virtual void doOnElementStart(const XML_Char *szName,
        _DBGWExpatXMLProperties &properties);
    virtual void doOnElementEnd(const XML_Char *szName);
    virtual void doOnElementContent(const XML_Char *szData, int nLength);

  private:
    void parseSql(_DBGWExpatXMLProperties properties);
    void parseParameter(_DBGWExpatXMLProperties properties);
    void parseColumn(_DBGWExpatXMLProperties properties);

  private:
    _DBGWQueryMapContext &m_parserContext;
  };

  class _DBGW20QueryMapParser : public _DBGWParser
  {
  public:
    _DBGW20QueryMapParser(const string &fileName,
        _DBGWQueryMapContext &parserContext);
    virtual ~_DBGW20QueryMapParser();

  protected:
    virtual void doOnElementStart(const XML_Char *szName,
        _DBGWExpatXMLProperties &properties);
    virtual void doOnElementEnd(const XML_Char *szName);
    virtual void doOnElementContent(const XML_Char *szData, int nLength);
    virtual void doOnCdataEnd();

  private:
    void parseUsingDB(_DBGWExpatXMLProperties &properties);
    void parseSql(_DBGWExpatXMLProperties &properties);
    void parseParameter(_DBGWExpatXMLProperties &properties);

  private:
    _DBGWQueryMapContext &m_parserContext;
    DBGWStringList m_moduleIDList;
  };

  class _DBGW30QueryMapParser : public _DBGWParser
  {
  public:
    _DBGW30QueryMapParser(const string &fileName,
        _DBGWQueryMapContext &parserContext);
    virtual ~_DBGW30QueryMapParser();

  protected:
    virtual void doOnElementStart(const XML_Char *szName,
        _DBGWExpatXMLProperties &properties);
    virtual void doOnElementEnd(const XML_Char *szName);
    virtual void doOnElementContent(const XML_Char *szData, int nLength);
    virtual void doOnCdataEnd();

  private:
    void parseSqls(_DBGWExpatXMLProperties &properties);
    void parseSql(_DBGWExpatXMLProperties &properties);
    void parseParameter(_DBGWExpatXMLProperties &properties);
    void parseGroup(_DBGWExpatXMLProperties &properties);

  private:
    _DBGWQueryMapContext &m_parserContext;
  };

  class _DBGWConfigurationParser: public _DBGWParser
  {
  public:
    _DBGWConfigurationParser(const string &fileName,
        _DBGWConnectorSharedPtr pConnector);
    _DBGWConfigurationParser(const string &fileName,
        _DBGWQueryMapperSharedPtr pQueryMapper);

  protected:
    virtual void doOnElementStart(const XML_Char *szName,
        _DBGWExpatXMLProperties &properties);
    virtual void doOnElementEnd(const XML_Char *szName);

  private:
    void parseQueryMap(_DBGWExpatXMLProperties &properties);
    void parseLog(_DBGWExpatXMLProperties &properties);
    void parseInclude(_DBGWExpatXMLProperties &properties);

  private:
    _DBGWConnectorSharedPtr m_pConnector;
    _DBGWQueryMapperSharedPtr m_pQueryMapper;
  };

  void parseQueryMapper(const string &fileName,
      _DBGWQueryMapperSharedPtr pQueryMaper);

}

#endif				/* DBGWXMLPARSER_H_ */
