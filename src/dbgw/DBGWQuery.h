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

#include "DBGWClientFwd.h"

namespace dbgw
{

  class _DBGWQuery;
  typedef shared_ptr<_DBGWQuery> _DBGWQuerySharedPtr;

  enum DBGWQueryMapperVersion
  {
    DBGW_QUERY_MAP_VER_UNKNOWN,
    DBGW_QUERY_MAP_VER_10,
    DBGW_QUERY_MAP_VER_20,
    DBGW_QUERY_MAP_VER_30
  };

  extern string makeImplicitParamName(int nIndex);
  extern bool isImplicitParamName(const char *szName);
  extern db::DBGWStatementType getQueryType(const char *szQueryType);

  struct _DBGWQueryParameter
  {
    string name;
    DBGWValueType type;
    size_t index;
    int firstPlaceHolderIndex;
    db::DBGWParameterMode mode;
  };

  struct _DBGWPlaceHolder
  {
    string name;
    size_t index;
    size_t queryParamIndex;
  };

  typedef vector<db::DBGWStatementType> _DBGWQueryTypeList;
  typedef vector<_DBGWQueryParameter> _DBGWQueryParameterList;
  typedef vector<_DBGWPlaceHolder> _DBGWPlaceHolderList;

  class _DBGWBoundQuery
  {
  public:
    _DBGWBoundQuery(const _DBGWBoundQuery &query);
    virtual ~ _DBGWBoundQuery();

  public:
    const char *getSQL() const;
    const char *getSqlName() const;
    const char *getGroupName() const;
    string getSqlKey() const;
    db::DBGWStatementType getType() const;
    int getBindNum() const;
    const _DBGWQueryParameter &getQueryParam(size_t nIndex) const;
    const _DBGWQueryParameter &getQueryParamByPlaceHolderIndex(
        size_t nBindIndex) const;
    const _DBGWQueryParameterList &getQueryParamList() const;
    const db::DBGWResultSetMetaDataSharedPtr getUserDefinedResultSetMetaData() const;
    bool isExistOutBindParam() const;

  private:
    _DBGWBoundQuery(const char *szSql, const char *szGroupName,
        const _DBGWQuery &query);

  private:
    string m_sql;
    string m_groupName;
    string m_sqlKey;
    const _DBGWQuery &m_query;

  private:
    friend class _DBGWQuery;
  };

  typedef shared_ptr<_DBGWBoundQuery> _DBGWBoundQuerySharedPtr;

  class _DBGWQuery
  {
  public:
    _DBGWQuery(DBGWQueryMapperVersion version, const string &fileName,
        const string &query, const string &sqlName, const string &groupName,
        db::DBGWStatementType statementType,
        const _DBGWQueryParameterList &queryParamList,
        const db::DBGWResultSetMetaDataSharedPtr pUserDefinedResultSetMetaData);
    virtual ~ _DBGWQuery();

  public:
    _DBGWBoundQuerySharedPtr getDBGWBoundQuery(const char *szGroupName,
        const _DBGWValueSet *pValueSet) const;
    const char *getFileName() const;
    const char *getSqlName() const;
    const char *getGroupName() const;
    db::DBGWStatementType getType() const;
    int getBindNum() const;
    const _DBGWQueryParameter &getQueryParam(size_t nIndex) const;
    const _DBGWQueryParameter &getQueryParamByPlaceHolderIndex(
        size_t nBindIndex) const;
    const char *getQuery() const;
    const db::DBGWResultSetMetaDataSharedPtr getUserDefinedResultSetMetaData() const;
    const _DBGWQueryParameterList &getQueryParamList() const;
    bool isExistOutBindParam() const;

  private:
    void addQueryPart(char cToken, const char *szStart, const char *szEnd);

  private:
    class _DBGWQueryPart;
    typedef shared_ptr<_DBGWQueryPart> _DBGWQueryPartSharedPtr;
    typedef vector<_DBGWQueryPartSharedPtr> _DBGWQueryPartList;

  private:
    _DBGWLogger m_logger;
    _DBGWQueryPartList m_queryPartList;
    string m_fileName;
    string m_query;
    string m_sqlName;
    string m_groupName;
    db::DBGWStatementType m_statementType;
    _DBGWQueryParameterList m_queryParamList;
    _DBGWPlaceHolderList m_placeHolderList;
    /**
     * in dbgw 1.0,
     * user can specify result set meta data.
     * so we have to save this to make result set.
     */
    db::DBGWResultSetMetaDataSharedPtr m_pUserDefinedResultSetMetaData;
    bool m_bExistOutBindParam;

  private:
    class _DBGWQueryPart
    {
    public:
      virtual ~ _DBGWQueryPart()
      {
      }

      virtual string toString(const _DBGWValueSet *pValueSet) const = 0;
    };

    class _DBGWSQLQueryPart: public _DBGWQueryPart
    {
    public:
      _DBGWSQLQueryPart(string sql);
      virtual ~ _DBGWSQLQueryPart();

      virtual string toString(const _DBGWValueSet *pValueSet) const;

    private:
      string m_sql;
    };

    class _DBGWReplaceQueryPart: public _DBGWQueryPart
    {
    public:
      _DBGWReplaceQueryPart(const _DBGWLogger &logger, const string &name);
      virtual ~ _DBGWReplaceQueryPart();

      virtual string toString(const _DBGWValueSet *pValueSet) const;

    private:
      const _DBGWLogger &m_logger;
      string m_name;

    };
  };

}

#endif /* DBGWQUERY_H_ */
