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

#ifndef EXECUTORHANDLER_H_
#define EXECUTORHANDLER_H_

namespace dbgw
{

  enum _ExecutionType
  {
    DBGW_EXEC_TYPE_NORMAL,
    DBGW_EXEC_TYPE_ARRAY,
    DBGW_EXEC_TYPE_BATCH,
  };

  class _Lob;
  class _Service;
  class _QueryMapper;
  class _BoundQuery;

  class _ExecutorHandler
  {
  public:
    _ExecutorHandler(trait<_Service>::sp pService,
        trait<_QueryMapper>::sp pQueryMapper);
    virtual ~_ExecutorHandler();

    void release(bool bIsForceDrop = false);

    void setAutoCommit(bool bAutoCommit);
    void commit();
    void rollback();
    void execute(const std::string &sqlName, const _Parameter *pParameter);
    void executeBatch(const std::string &sqlName,
        const _ParameterList &parameterList);
    trait<Lob>::sp createClob();
    trait<Lob>::sp createBlob();
    const Exception &getLastException();

  public:
    trait<ClientResultSet>::sp getResultSet() const;
    trait<ClientResultSet>::spvector getResultSetList() const;

  private:
    _ExecutorHandler(const _ExecutorHandler &);
    _ExecutorHandler &operator=(const _ExecutorHandler &);

  private:
    class Impl;
    Impl *m_pImpl;
  };

}

#endif
