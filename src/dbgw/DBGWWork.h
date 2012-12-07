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
 *  aint64 with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

#ifndef DBGWWORK_H_
#define DBGWWORK_H_

#include "DBGWWorkFwd.h"
#include "DBGWConfigurationFwd.h"

namespace dbgw
{

  class _DBGWWorkerJob
  {
  public:
    _DBGWWorkerJob();
    virtual ~_DBGWWorkerJob();

    virtual _DBGWWorkerJobResultSharedPtr execute() = 0;
  };

  class _DBGWWorkerJobBindClientProxy : public _DBGWWorkerJob
  {
  public:
    _DBGWWorkerJobBindClientProxy(_DBGWService *pService,
        _DBGWQueryMapper *pQueryMapper);
    virtual ~_DBGWWorkerJobBindClientProxy();

    virtual _DBGWWorkerJobResultSharedPtr execute();

  private:
    _DBGWService *m_pService;
    _DBGWQueryMapper *m_pQueryMapper;
  };

  class _DBGWWorkerJobExecute : public _DBGWWorkerJob
  {
  public:
    _DBGWWorkerJobExecute(_DBGWClientProxySharedPtr pClientProxy,
        const char *szSqlName, const _DBGWParameter *pParameter);
    virtual ~_DBGWWorkerJobExecute();

    virtual _DBGWWorkerJobResultSharedPtr execute();

  private:
    _DBGWClientProxySharedPtr m_pClientProxy;
    string m_sqlName;
    _DBGWParameter m_parameter;
  };

  class _DBGWWorkerJobExecuteBatch : public _DBGWWorkerJob
  {
  public:
    _DBGWWorkerJobExecuteBatch(_DBGWClientProxySharedPtr pClientProxy,
        const char *szSqlName, const _DBGWParameterList &parameterList);
    virtual ~_DBGWWorkerJobExecuteBatch();

    virtual _DBGWWorkerJobResultSharedPtr execute();

  private:
    _DBGWClientProxySharedPtr m_pClientProxy;
    string m_sqlName;
    _DBGWParameterList m_parameterList;
  };

  class _DBGWWorkerJobSetAutoCommit: public _DBGWWorkerJob
  {
  public:
    _DBGWWorkerJobSetAutoCommit(_DBGWClientProxySharedPtr pClientProxy,
        bool bAutoCommit);
    virtual ~_DBGWWorkerJobSetAutoCommit();

    virtual _DBGWWorkerJobResultSharedPtr execute();

  private:
    _DBGWClientProxySharedPtr m_pClientProxy;
    bool m_bAutoCommit;
  };

  class _DBGWWorkerJobCommit: public _DBGWWorkerJob
  {
  public:
    _DBGWWorkerJobCommit(_DBGWClientProxySharedPtr pClientProxy);
    virtual ~_DBGWWorkerJobCommit();

    virtual _DBGWWorkerJobResultSharedPtr execute();

  private:
    _DBGWClientProxySharedPtr m_pClientProxy;
  };

  class _DBGWWorkerJobRollback: public _DBGWWorkerJob
  {
  public:
    _DBGWWorkerJobRollback(_DBGWClientProxySharedPtr pClientProxy);
    virtual ~_DBGWWorkerJobRollback();

    virtual _DBGWWorkerJobResultSharedPtr execute();

  private:
    _DBGWClientProxySharedPtr m_pClientProxy;
  };

  class _DBGWWorkerJobResult
  {
  public:
    _DBGWWorkerJobResult(DBGWException e);
    _DBGWWorkerJobResult(_DBGWClientProxySharedPtr pClientProxy, DBGWException e);
    _DBGWWorkerJobResult(DBGWClientResultSetSharedPtr pResult, DBGWException e);
    _DBGWWorkerJobResult(DBGWClientBatchResultSetSharedPtr pBatchResult,
        DBGWException e);
    virtual ~_DBGWWorkerJobResult();

  public:
    _DBGWClientProxySharedPtr getClientProxy() const;
    DBGWClientResultSetSharedPtr getResult() const;
    DBGWClientBatchResultSetSharedPtr getBatchResult() const;
    DBGWException getException() const;

  private:
    _DBGWClientProxySharedPtr m_pClientProxy;
    DBGWClientResultSetSharedPtr m_pResult;
    DBGWClientBatchResultSetSharedPtr m_pBatchResult;
    DBGWException m_exception;
  };

  class _DBGWWorker : public system::_ThreadData
  {
  public:
    _DBGWWorker(_DBGWWorkerPool &workerPool);
    virtual ~_DBGWWorker();

    void start();
    void stop(unsigned long ulMaxWaitExitTimeMilSec);
    void bindClientProxy(_DBGWService *pService, _DBGWQueryMapper *pQueryMapper,
        unsigned long ulWaitTimeMilSec = system::INFINITE_TIMEOUT);
    void releaseClientProxy();
    void forceReleaseClientProxy();
    void returnToPool(bool bIsForceDrop = false);

    void setAutoCommit(bool bAutoCommit,
        unsigned long ulWaitTimeMilSec = system::INFINITE_TIMEOUT);
    void commit(unsigned long ulWaitTimeMilSec = system::INFINITE_TIMEOUT);
    void rollback(unsigned long ulWaitTimeMilSec = system::INFINITE_TIMEOUT);
    DBGWClientResultSetSharedPtr execute(const char *szSqlName,
        const _DBGWParameter *pParameter,
        unsigned long ulWaitTimeMilSec = system::INFINITE_TIMEOUT);
    DBGWClientBatchResultSetSharedPtr executeBatch(
        const char *szSqlName, const _DBGWParameterList &parameterList,
        unsigned long ulWaitTimeMilSec = system::INFINITE_TIMEOUT);

  public:
    bool isBusy() const;

  private:
    _DBGWWorkerJobResultSharedPtr delegateJob(_DBGWWorkerJobSharedPtr pJob,
        unsigned long lWaitTimeMilSec);
    _DBGWWorkerJobSharedPtr waitAndGetJob();
    void notifyEndOfJob(_DBGWWorkerJobResultSharedPtr pJobResult);

  private:
    static void run(const system::_Thread *pThread,
        system::_ThreadDataSharedPtr pThreadData);

  private:
    _DBGWWorkerPool &m_workerPool;
    _DBGWClientProxySharedPtr m_pClientProxy;
    system::_ThreadSharedPtr m_pThread;
    system::_MutexSharedPtr m_pMutex;
    system::_ConditionVariableSharedPtr m_pCond;
    _DBGWWorkerJobSharedPtr m_pJob;
    _DBGWWorkerJobResultSharedPtr m_pJobResult;
  };

  class _DBGWWorkerPool
  {
  public:
    _DBGWWorkerPool(DBGWConfiguration *pConfiguration);
    virtual ~_DBGWWorkerPool();

    _DBGWWorkerSharedPtr getWorker();
    void returnWorker(_DBGWWorkerSharedPtr pWorkerThread);

  private:
    DBGWConfiguration *m_pConfiguration;
    _DBGWWorkerList m_workerList;
    system::_MutexSharedPtr m_pMutex;
  };

}

#endif
