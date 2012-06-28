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

  namespace DBGWQueryType
  {

    enum Enum
    {
      UNDEFINED = -1, SELECT = 0, PROCEDURE, DML, SIZE
    };

  }

  struct DBGWQueryParameter
  {
    string name;
    DBGWValueType type;
    int nIndex;
  };

  typedef hash_map<string, DBGWQueryParameter, hash<string> ,
          dbgwStringCompareFunc> DBGWQueryParameterHashMap;

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
    DBGWQueryType::Enum getType() const;
    int getBindNum() const;
    DBGWQueryParameter getBindParam(int nIndex) const;

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
    DBGWQuery(const string &fileName, const string &query,
        const string &sqlName, const string &groupName,
        DBGWQueryType::Enum queryType,
        const DBGWQueryParameterHashMap &inQueryParamMap,
        const DBGWQueryParameterHashMap &outQueryParamMap);
    virtual ~ DBGWQuery();

  public:
    DBGWBoundQuerySharedPtr getDBGWBoundQuery(const char *szGroupName,
        const DBGWValueSet *pValueSet) const;
    const char *getFileName() const;
    const char *getSqlName() const;
    const char *getGroupName() const;
    DBGWQueryType::Enum getType() const;
    int getBindNum() const;
    DBGWQueryParameter getBindParam(size_t nIndex) const;
    const char *getQuery() const;

  private:
    bool addQueryPart(char cToken, int nStart, int nEnd, bool bForce);

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
    DBGWQueryType::Enum m_queryType;
    DBGWQueryParameterHashMap m_inQueryParamMap;
    DBGWQueryParameterHashMap m_outQueryParamMap;
    DBGWStringList m_bindParamNameList;

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
