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

#ifndef EXECUTORSTATEMENT_H_
#define EXECUTORSTATEMENT_H_

namespace dbgw
{

  class _BoundQuery;
  class _StatisticsItem;

  typedef std::list<std::string> _ExecutorStatementPoolKeyList;

  class _ExecutorStatement;
  typedef std::pair<_ExecutorStatement *,
          _ExecutorStatementPoolKeyList::iterator> _ExecutorStatementPoolValue;

  typedef boost::unordered_map<std::string, _ExecutorStatementPoolValue,
          boost::hash<std::string>, func::compareString> _ExecutorStatementPoolHashMap;

  class _ExecutorStatement
  {
  public:
    _ExecutorStatement(bool bUseDefaultParameterValue,
        trait<sql::Connection>::sp pConnection, const trait<_BoundQuery>::sp pQuery);
    virtual ~_ExecutorStatement();

    void init(const trait<_BoundQuery>::sp pQuery);
    trait<ClientResultSet>::sp execute(const _Parameter &parameter);
    trait<ClientResultSet>::spvector executeBatch(
        const _ParameterList &parameterList);

  private:
    void bindParameter(const _Parameter &parameter);
    void bindNull(size_t nIndex, ValueType type);
    void bindInt(size_t nIndex, const Value *pValue);
    void bindLong(size_t nIndex, const Value *pValue);
    void bindFloat(size_t nIndex, const Value *pValue);
    void bindDouble(size_t nIndex, const Value *pValue);
    void bindString(size_t nIndex, const Value *pValue);
    void bindTime(size_t nIndex, const Value *pValue);
    void bindDate(size_t nIndex, const Value *pValue);
    void bindDateTime(size_t nIndex, const Value *pValue);
    void bindBytes(size_t nIndex, const Value *pValue);
    void bindClob(size_t nIndex, const Value *pValue);
    void bindBlob(size_t nIndex, const Value *pValue);

  private:
    bool m_bUseDefaultParameterValue;
    trait<sql::PreparedStatement>::sp m_pStatement;
    trait<sql::CallableStatement>::sp m_pCallableStatement;
    trait<_BoundQuery>::sp m_pQuery;
    _Logger m_logger;
    _LogDecorator m_paramLogDecorator;
  };

  class _ExecutorStatementPool
  {
  public:
    _ExecutorStatementPool(trait<_StatisticsItem>::sp pStatItem, size_t nMaxLRUSize);
    virtual ~_ExecutorStatementPool();

    void put(const std::string &fullSqlText, _ExecutorStatement *pStatement);
    _ExecutorStatement *get(const std::string &fullSqlText);
    void clear();

  private:
    void evict();

  private:
    /**
     * head : least recently used
     * tail : most recently used
     */
    size_t m_nMaxLRUSize;
    _ExecutorStatementPoolKeyList m_statementKeyList;
    _ExecutorStatementPoolHashMap m_statementMap;

    trait<_StatisticsItem>::sp m_pStatItem;
  };

}

#endif
