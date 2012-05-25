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

  private:
    XML_Parser m_pParser;
  };

  class DBGWExpatXMLProperties
  {
  public:
    DBGWExpatXMLProperties(const string &nodeName, const XML_Char *szAttr[]);
    virtual ~ DBGWExpatXMLProperties();

    const char *get(const char *szName, bool bRequired);
    const char *getCString(const char *szName, bool bRequired);
    int getInt(const char *szName, bool bRequired);
    bool getBool(const char *szName, bool bRequired);
    DBGWValueType getValueType(const char *szName);
    CCI_LOG_LEVEL getLogLevel(const char *szName);

  private:
    int propertyToInt(const char *szProperty);
    bool propertyToBoolean(const char *szProperty);

  private:
    string m_nodeName;
    const XML_Char **m_pAttr;
  };

  typedef stack<string> DBGWExpatXMLElementStack;

  class DBGWParser
  {
  public:
    DBGWParser(const string &fileName);
    virtual ~ DBGWParser();

  public:
    static void parse(DBGWParser *pParser);

  protected:
    virtual void doOnElementStart(const XML_Char *szName,
        DBGWExpatXMLProperties &properties) = 0;
    virtual void doOnElementEnd(const XML_Char *szName) = 0;
    virtual void doOnElementContent(const XML_Char *szData, int nLength);
    virtual void doOnCdataStart();
    virtual void doOnCdataEnd();

  protected:
    const string &getFileName() const;
    const string &getParentElementName() const;
    bool isRootElement() const;
    bool isCdataSection() const;

  private:
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
    DBGWExpatXMLElementStack m_elementStack;
    bool m_bCdataSection;
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
    DBGWDBInfoHashMap *m_pDBnfoMap;
    DBGWHostSharedPtr m_pHost;
    size_t m_nPoolSize;
  };

  static const int MAX_QUERY_LEN = 4096;

  class DBGWQueryMapParser: public DBGWParser
  {
  public:
    DBGWQueryMapParser(const string &fileName,
        DBGWQueryMapperSharedPtr pQueryMapper);
    virtual ~ DBGWQueryMapParser();

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
    DBGWQueryMapperSharedPtr m_pQueryMapper;
    string m_sqlName;
    string m_globalGroupName;
    string m_localGroupName;
    DBGWQueryType::Enum m_queryType;
    DBGWQueryParameterHashMap m_inQueryParamMap;
    DBGWQueryParameterHashMap m_outQueryParamMap;
    bool m_bExistQueryInSql;
    bool m_bExistQueryInGroup;
    char m_szQueryBuffer[MAX_QUERY_LEN];
    int m_nQueryLen;
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
    void parseLog(DBGWExpatXMLProperties &properties);
    void parseInclude(DBGWExpatXMLProperties &properties);

  private:
    DBGWConnectorSharedPtr m_pConnector;
    DBGWQueryMapperSharedPtr m_pQueryMapper;
  };

}

#endif				/* DBGWXMLPARSER_H_ */
