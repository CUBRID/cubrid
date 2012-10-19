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

#ifndef DBGWQUERY_H_
#define DBGWQUERY_H_

namespace dbgw
{

  class DBGWQuery;
  typedef shared_ptr<DBGWQuery> DBGWQuerySharedPtr;

  enum DBGWQueryMapperVersion
  {
    DBGW_QUERY_MAP_VER_UNKNOWN,
    DBGW_QUERY_MAP_VER_10,
    DBGW_QUERY_MAP_VER_20,
    DBGW_QUERY_MAP_VER_30
  };

  enum DBGWQueryType
  {
    DBGW_QUERY_TYPE_UNDEFINED = -1,
    DBGW_QUERY_TYPE_SELECT = 0,
    DBGW_QUERY_TYPE_PROCEDURE,
    DBGW_QUERY_TYPE_UPDATE,
    DBGW_QUERY_TYPE_SIZE
  };

  extern string makeImplicitParamName(int nIndex);
  extern bool isImplicitParamName(const char *szName);
  extern DBGWQueryType getQueryType(const char *szQueryType);

  enum DBGWBindMode
  {
    DBGW_BIND_MODE_IN = 0,
    DBGW_BIND_MODE_OUT,
    DBGW_BIND_MODE_INOUT
  };

  namespace db
  {
    typedef vector<struct MetaData> MetaDataList;
  }

  struct DBGWQueryParameter
  {
    string name;
    DBGWValueType type;
    size_t index;
    int firstPlaceHolderIndex;
    DBGWBindMode mode;
  };

  struct DBGWPlaceHolder
  {
    string name;
    size_t index;
    size_t queryParamIndex;
  };

  typedef vector<DBGWQueryType> DBGWQueryTypeList;
  typedef vector<DBGWQueryParameter> DBGWQueryParameterList;
  typedef vector<DBGWPlaceHolder> DBGWPlaceHolderList;

  class DBGWQuery;

  class DBGWBoundQuery
  {
  public:
    DBGWBoundQuery(const DBGWBoundQuery &query);
    virtual ~ DBGWBoundQuery();

  public:
    const char *getSQL() const;
    const char *getSqlName() const;
    const char *getGroupName() const;
    string getSqlKey() const;
    DBGWQueryType getType() const;
    int getBindNum() const;
    const DBGWQueryParameter &getQueryParamByPlaceHolderIndex(int nBindIndex) const;
    const DBGWQueryParameterList &getQueryParamList() const;
    const db::MetaDataList &getUserDefinedMetaList() const;
    bool isExistOutBindParam() const;

  private:
    DBGWBoundQuery(const char *szSql, const char *szGroupName,
        const DBGWQuery &query);

  private:
    string m_sql;
    string m_groupName;
    string m_sqlKey;
    const DBGWQuery &m_query;

  private:
    friend class DBGWQuery;
  };

  typedef shared_ptr<DBGWBoundQuery> DBGWBoundQuerySharedPtr;

  class DBGWQuery
  {
  public:
    DBGWQuery(DBGWQueryMapperVersion version, const string &fileName,
        const string &query, const string &sqlName, const string &groupName,
        DBGWQueryType queryType,
        const DBGWQueryParameterList &queryParamList,
        const db::MetaDataList &userDefinedMetaList);
    virtual ~ DBGWQuery();

  public:
    DBGWBoundQuerySharedPtr getDBGWBoundQuery(const char *szGroupName,
        const DBGWValueSet *pValueSet) const;
    const char *getFileName() const;
    const char *getSqlName() const;
    const char *getGroupName() const;
    DBGWQueryType getType() const;
    int getBindNum() const;
    const DBGWQueryParameter &getQueryParamByPlaceHolderIndex(size_t nBindIndex) const;
    const char *getQuery() const;
    const db::MetaDataList &getUserDefinedMetaList() const;
    const DBGWQueryParameterList &getQueryParamList() const;
    bool isExistOutBindParam() const;

  private:
    void addQueryPart(char cToken, const char *szStart, const char *szEnd);

  private:
    class DBGWQueryPart;
    typedef shared_ptr<DBGWQueryPart> DBGWQueryPartSharedPtr;
    typedef vector<DBGWQueryPartSharedPtr> DBGWQueryPartList;

  private:
    DBGWLogger m_logger;
    DBGWQueryPartList m_queryPartList;
    string m_fileName;
    string m_query;
    string m_sqlName;
    string m_groupName;
    DBGWQueryType m_queryType;
    DBGWQueryParameterList m_queryParamList;
    DBGWPlaceHolderList m_placeHolderList;
    db::MetaDataList m_userDefinedMetaList;
    bool m_bExistOutBindParam;

  private:
    class DBGWQueryPart
    {
    public:
      virtual ~ DBGWQueryPart()
      {
      }

      virtual string toString(const DBGWValueSet *pValueSet) const = 0;
    };

    class DBGWSQLQueryPart: public DBGWQueryPart
    {
    public:
      DBGWSQLQueryPart(string sql);
      virtual ~ DBGWSQLQueryPart();

      virtual string toString(const DBGWValueSet *pValueSet) const;

    private:
      string m_sql;
    };

    class DBGWReplaceQueryPart: public DBGWQueryPart
    {
    public:
      DBGWReplaceQueryPart(const DBGWLogger &logger, const string &name);
      virtual ~ DBGWReplaceQueryPart();

      virtual string toString(const DBGWValueSet *pValueSet) const;

    private:
      const DBGWLogger &m_logger;
      string m_name;

    };
  };

}

#endif /* DBGWQUERY_H_ */
