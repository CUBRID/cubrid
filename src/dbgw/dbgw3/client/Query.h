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

#ifndef QUERY_H_
#define QUERY_H_

namespace dbgw
{

  enum _QueryStatColumn
  {
    DBGW_QUERY_STAT_COL_PADDING = 0,
    DBGW_QUERY_STAT_COL_GROUPNAME,
    DBGW_QUERY_STAT_COL_SQLNAME,
    DBGW_QUERY_STAT_COL_TOTAL_CNT,
    DBGW_QUERY_STAT_COL_SUCC_CNT,
    DBGW_QUERY_STAT_COL_FAIL_CNT,
    DBGW_QUERY_STAT_COL_AVG_TIME,
    DBGW_QUERY_STAT_COL_MAX_TIME
  };

  class _Query;
  class _ValueSet;
  class _StatisticsItemColumn;
  class ClientResultSetMetaData;

  class _StatisticsMonitor;

  struct _QueryParameter
  {
    std::string name;
    ValueType type;
    size_t index;
    int firstPlaceHolderIndex;
    sql::ParameterMode mode;
    int size;
  };

  struct _PlaceHolder
  {
    std::string name;
    size_t index;
    size_t queryParamIndex;
  };

  std::string makeImplicitParamName(int nIndex);
  bool isImplicitParamName(const char *szName);
  sql::StatementType getQueryType(const char *szQueryType);

  class _BoundQuery
  {
  public:
    _BoundQuery(const _BoundQuery &query);
    virtual ~ _BoundQuery();

    void bindCharsetConverter(_CharsetConverter *pConverter);

  public:
    std::string getSQL() const;
    const char *getSqlName() const;
    const char *getGroupName() const;
    std::string getSqlKey() const;
    sql::StatementType getType() const;
    int getBindNum() const;
    const _QueryParameter &getQueryParam(size_t nIndex) const;
    const _QueryParameter &getQueryParamByPlaceHolderIndex(
        size_t nBindIndex) const;
    const trait<_QueryParameter>::vector &getQueryParamList() const;
    const trait<ClientResultSetMetaData>::sp getUserDefinedResultSetMetaData() const;
    bool isExistOutBindParam() const;
    _StatisticsItemColumn &getStatColumn(_QueryStatColumn column) const;

  private:
    _BoundQuery(const char *szSql, const char *szGroupName,
        const _Query &query);

  private:
    std::string m_sql;
    std::string m_groupName;
    std::string m_sqlKey;
    const _Query &m_query;
    _CharsetConverter *m_pConverter;

  private:
    friend class _Query;
  };

  class _Query
  {
  public:
    _Query(trait<_StatisticsMonitor>::sp pMonitor, QueryMapperVersion version,
        const std::string &fileName, const std::string &query,
        const std::string &sqlName, const std::string &groupName,
        sql::StatementType statementType,
        const trait<_QueryParameter>::vector &queryParamList,
        const trait<ClientResultSetMetaData>::sp pUserDefinedResultSetMetaData);
    virtual ~ _Query();

  public:
    void setDbType(sql::DataBaseType dbType);
    void parseQuery();
    trait<_BoundQuery>::sp getBoundQuery(const char *szGroupName,
        const _Parameter &valueSet) const;
    const char *getFileName() const;
    const char *getSqlName() const;
    const char *getGroupName() const;
    sql::StatementType getType() const;
    int getBindNum() const;
    const _QueryParameter &getQueryParam(size_t nIndex) const;
    const _QueryParameter &getQueryParamByPlaceHolderIndex(
        size_t nBindIndex) const;
    const char *getQuery() const;
    const trait<ClientResultSetMetaData>::sp getUserDefinedResultSetMetaData() const;
    const trait<_QueryParameter>::vector &getQueryParamList() const;
    bool isExistOutBindParam() const;
    _StatisticsItemColumn &getStatColumn(_QueryStatColumn column) const;

  private:
    void addQueryPart(char cToken, const char *szStart, const char *szEnd);

  private:
    _Query(const _Query &);
    _Query &operator=(const _Query &);

  private:
    class Impl;
    Impl *m_pImpl;
  };

}

#endif
