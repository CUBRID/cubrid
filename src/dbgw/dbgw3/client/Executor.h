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

#ifndef EXECUTOR_H_
#define EXECUTOR_H_

namespace dbgw
{

  class _Group;
  class _BoundQuery;
  class _ExecutorStatement;

  namespace sql
  {
    class Connection;
  }

  class _Executor : public _ConfigurationObject,
    public boost::enable_shared_from_this<_Executor>
  {
  public:
    static int DEFAULT_MAX_PREPARED_STATEMENT_SIZE();

  public:
    _Executor(_Group &group, trait<sql::Connection>::sp pConnection,
        int nMaxPreparedStatementSize);
    virtual ~_Executor();

    void init(bool bAutoCommit, sql::TransactionIsolarion isolation);
    void close();
    bool isValid() const;
    bool isEvictable(unsigned long ulMinEvictableIdleTimeMillis);

    trait<ClientResultSet>::sp execute(trait<_BoundQuery>::sp pQuery,
        _Parameter &parameter);
    trait<ClientResultSet>::spvector executeBatch(trait<_BoundQuery>::sp pQuery,
        _ParameterList &parameterList);
    void setAutoCommit(bool bAutoCommit);
    void commit();
    void rollback();
    void cancel();
    trait<Lob>::sp createClob();
    trait<Lob>::sp createBlob();
    void returnToPool(bool bIsForceDrop = false);

  public:
    const char *getGroupName() const;
    bool isIgnoreResult() const;

  private:
    _ExecutorStatement *getExecutorProxy(const trait<_BoundQuery>::sp pQuery);

  private:
    class Impl;
    Impl *m_pImpl;
  };

}

#endif
