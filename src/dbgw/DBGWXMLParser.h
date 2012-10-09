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

  class DBGWExpatXMLParser
  {
  public:
    DBGWExpatXMLParser(const string &fileName);
    virtual ~ DBGWExpatXMLParser();

  public:
    XML_Parser get() const;
    const char *getFileName() const;

  private:
    XML_Parser m_pParser;
    string m_fileName;
  };

  class DBGWExpatXMLProperties
  {
  public:
    DBGWExpatXMLProperties(const DBGWExpatXMLParser &xmlParser,
        const string &nodeName, const XML_Char *szAttr[]);
    virtual ~ DBGWExpatXMLProperties();

    const char *get(const char *szName, bool bRequired);
    const char *getCString(const char *szName, bool bRequired);
    int getInt(const char *szName, bool bRequired);
    bool getBool(const char *szName, bool bRequired);
    void getValidateResult(const char *szName, bool bValidateResult[]);
    DBGWValueType get30ValueType(const char *szName);
    DBGWValueType get20ValueType(const char *szName);
    DBGWValueType get10ValueType(const char *szName);
    CCI_LOG_LEVEL getLogLevel(const char *szName);

  public:
    const char *getNodeName() const;

  private:
    int propertyToInt(const char *szProperty);
    bool propertyToBoolean(const char *szProperty);

  private:
    const DBGWExpatXMLParser &m_xmlParser;
    string m_nodeName;
    const XML_Char **m_pAttr;
  };

  typedef stack<string> DBGWExpatXMLElementStack;

  class DBGWParser
  {
  public:
    DBGWParser(const string &fileName);
    virtual ~ DBGWParser();

    virtual void setRealFileName(const string &realFileName);

  public:
    static void parse(DBGWParser *pParser);

  protected:
    virtual void doOnElementStart(const XML_Char *szName,
        DBGWExpatXMLProperties &properties) = 0;
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
    static void doParse(DBGWParser *pParser);
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
    DBGWExpatXMLElementStack m_elementStack;
    bool m_bCdataSection;
    bool m_bAborted;
  };

  class DBGWConnectorParser: public DBGWParser
  {
  public:
    DBGWConnectorParser(const string &fileName,
        DBGWConnectorSharedPtr pConnector);
    virtual ~ DBGWConnectorParser();

  protected:
    virtual void doOnElementStart(const XML_Char *szName,
        DBGWExpatXMLProperties &properties);
    virtual void doOnElementEnd(const XML_Char *szName);

  private:
    void parseService(DBGWExpatXMLProperties &properties);
    void parseGroup(DBGWExpatXMLProperties &properties);
    void parsePool(DBGWExpatXMLProperties &properties);
    void parseDBInfo(DBGWExpatXMLProperties &properties);
    void parseHost(DBGWExpatXMLProperties &properties);
    void parseAltHost(DBGWExpatXMLProperties &properties);

  private:
    DBGWConnectorSharedPtr m_pConnector;
    DBGWServiceSharedPtr m_pService;
    DBGWGroupSharedPtr m_pGroup;
    DBGWDBInfoHashMap m_dbInfoMap;
    DBGWHostSharedPtr m_pHost;
    size_t m_nPoolSize;
    bool bExistDbInfo;
  };

  class DBGWQueryMapContext
  {
  public:
    DBGWQueryMapContext(const string &fileName,
        DBGWQueryMapperSharedPtr pQueryMapper);
    virtual ~DBGWQueryMapContext();

    void clear();
    void checkAndClearSql();
    void checkAndClearGroup();
    void setFileName(const string &fileName);
    void setGlobalGroupName(const char *szGlobalGroupName);
    void setLocalGroupName(const char *szLocalGroupName);
    void setQueryType(DBGWQueryType::Enum queryType);
    void setSqlName(const char *szSqlName);
    void setParameter(const string &name, int nIndex, DBGWValueType valueType);
    void setParameter(const char *szMode, const char *szName,
        DBGWValueType valueType);
    void setResult(const string &name, size_t nIndex, DBGWValueType valueType,
        int nLength);
    void appendQueryString(const char *szQueryString);
    void addQuery();

  public:
    const string &getGlobalGroupName() const;
    bool isExistQueryString() const;

  private:
    string m_fileName;
    DBGWQueryMapperSharedPtr m_pQueryMapper;
    string m_globalGroupName;
    string m_localGroupName;
    string m_sqlName;
    DBGWQueryType::Enum m_queryType;
    set<int> m_paramIndexList;
    DBGWQueryParameterHashMap m_inQueryParamMap;
    DBGWQueryParameterHashMap m_outQueryParamMap;
    set<int> m_resultIndexList;
    db::MetaDataList m_userDefinedMetaList;
    stringstream m_queryBuffer;
    bool m_bExistQueryInSql;
    bool m_bExistQueryInGroup;
  };

  class DBGW10QueryMapParser : public DBGWParser
  {
  public:
    DBGW10QueryMapParser(const string &fileName,
        DBGWQueryMapContext &parserContext);
    virtual ~DBGW10QueryMapParser();

  protected:
    virtual void doOnElementStart(const XML_Char *szName,
        DBGWExpatXMLProperties &properties);
    virtual void doOnElementEnd(const XML_Char *szName);
    virtual void doOnElementContent(const XML_Char *szData, int nLength);

  private:
    void parseSql(DBGWExpatXMLProperties properties);
    void parseParameter(DBGWExpatXMLProperties properties);
    void parseColumn(DBGWExpatXMLProperties properties);

  private:
    DBGWQueryMapContext &m_parserContext;
  };

  class DBGW20QueryMapParser : public DBGWParser
  {
  public:
    DBGW20QueryMapParser(const string &fileName,
        DBGWQueryMapContext &parserContext);
    virtual ~DBGW20QueryMapParser();

  protected:
    virtual void doOnElementStart(const XML_Char *szName,
        DBGWExpatXMLProperties &properties);
    virtual void doOnElementEnd(const XML_Char *szName);
    virtual void doOnElementContent(const XML_Char *szData, int nLength);
    virtual void doOnCdataEnd();

  private:
    void parseUsingDB(DBGWExpatXMLProperties &properties);
    void parseSql(DBGWExpatXMLProperties &properties);
    void parseParameter(DBGWExpatXMLProperties &properties);

  private:
    DBGWQueryMapContext &m_parserContext;
    DBGWStringList m_moduleIDList;
  };

  class DBGW30QueryMapParser : public DBGWParser
  {
  public:
    DBGW30QueryMapParser(const string &fileName,
        DBGWQueryMapContext &parserContext);
    virtual ~DBGW30QueryMapParser();

  protected:
    virtual void doOnElementStart(const XML_Char *szName,
        DBGWExpatXMLProperties &properties);
    virtual void doOnElementEnd(const XML_Char *szName);
    virtual void doOnElementContent(const XML_Char *szData, int nLength);
    virtual void doOnCdataEnd();

  private:
    void parseSqls(DBGWExpatXMLProperties &properties);
    void parseSql(DBGWExpatXMLProperties &properties);
    void parseParameter(DBGWExpatXMLProperties &properties);
    void parseGroup(DBGWExpatXMLProperties &properties);

  private:
    DBGWQueryMapContext &m_parserContext;
  };

  class DBGWConfigurationParser: public DBGWParser
  {
  public:
    DBGWConfigurationParser(const string &fileName,
        DBGWConnectorSharedPtr pConnector);
    DBGWConfigurationParser(const string &fileName,
        DBGWQueryMapperSharedPtr pQueryMapper);

  protected:
    virtual void doOnElementStart(const XML_Char *szName,
        DBGWExpatXMLProperties &properties);
    virtual void doOnElementEnd(const XML_Char *szName);

  private:
    void parseQueryMap(DBGWExpatXMLProperties &properties);
    void parseLog(DBGWExpatXMLProperties &properties);
    void parseInclude(DBGWExpatXMLProperties &properties);

  private:
    DBGWConnectorSharedPtr m_pConnector;
    DBGWQueryMapperSharedPtr m_pQueryMapper;
  };

  void parseQueryMapper(const string &fileName,
      DBGWQueryMapperSharedPtr pQueryMaper);

}

#endif				/* DBGWXMLPARSER_H_ */
