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

#ifndef ASYNCWORKERJOB_H_
#define ASYNCWORKERJOB_H_

namespace dbgw
{

  class _AsyncWaiter;
  class _Service;
  class _QueryMapper;
  class _ExecutorHandler;
  class _AsyncWorkerJob;
  class _Parameter;
  class _AsyncWorkerPool;

  enum _AsyncWorkerJobStatus
  {
    DBGW_ASYNC_JOB_STATUS_IDLE = 0,
    DBGW_ASYNC_JOB_STATUS_BUSY,
    DBGW_ASYNC_JOB_STATUS_DONE,
    DBGW_ASYNC_JOB_STATUS_CANCEL
  };

  class _AsyncWorkerJobResult
  {
  public:
    _AsyncWorkerJobResult(const Exception &exception);
    _AsyncWorkerJobResult(trait<_ExecutorHandler>::sp pExecHandler,
        const Exception &exception);
    _AsyncWorkerJobResult(trait<ClientResultSet>::sp pResultSet,
        const Exception &exception);
    _AsyncWorkerJobResult(trait<ClientResultSet>::spvector resultSetList,
        const Exception &exception);
    _AsyncWorkerJobResult(trait<Lob>::sp pLob, const Exception &exception);
    virtual ~_AsyncWorkerJobResult();

    trait<_ExecutorHandler>::sp getExecutorHandler();
    trait<ClientResultSet>::sp getResultSet();
    trait<ClientResultSet>::spvector getResultSetList();
    trait<Lob>::sp getLob();
    const Exception &getException();

  private:
    trait<_ExecutorHandler>::sp m_pExecHandler;
    trait<ClientResultSet>::sp m_pResultSet;
    trait<ClientResultSet>::spvector m_resultSetList;
    trait<Lob>::sp m_pLob;
    Exception m_exception;
  };

  class _AsyncWorkerJob : public boost::enable_shared_from_this<_AsyncWorkerJob>
  {
  public:
    _AsyncWorkerJob(trait<_Service>::sp pService,
        trait<_QueryMapper>::sp pQueryMapper, unsigned long ulTimeOutMilSec);
    _AsyncWorkerJob(trait<_ExecutorHandler>::sp pExecHandler,
        unsigned long ulTimeOutMilSec);
    virtual ~_AsyncWorkerJob();

    void execute();
    void cancel();
    bool isDone();
    void bindWaiter(trait<_AsyncWaiter>::sp pWaiter);
    void bindWorker(trait<_AsyncWorker>::sp pWorker);
    void makeExecutorHandler();
    bool bindExecutorHandler();

  public:
    virtual const char *getJobName() const = 0;
    virtual const char *getSqlName() const;
    unsigned long getTimeOutMilSec() const;
    unsigned long long int getAbsTimeOutMilSec() const;

  protected:
    virtual trait<_AsyncWorkerJobResult>::sp doExecute() = 0;
    trait<_ExecutorHandler>::sp getExecutorHandler() const;

  private:
    _AsyncWorkerJob(const _AsyncWorkerJob &);
    _AsyncWorkerJob &operator=(const _AsyncWorkerJob &);

  private:
    class Impl;
    Impl *m_pImpl;
  };

  class _GetExecutorHandlerJob : public _AsyncWorkerJob
  {
  public:
    _GetExecutorHandlerJob(trait<_Service>::sp pService,
        trait<_QueryMapper>::sp pQueryMapper, unsigned long ulTimeOutMilSec);
    virtual ~_GetExecutorHandlerJob() {}

    virtual trait<_AsyncWorkerJobResult>::sp doExecute();

  public:
    virtual const char *getJobName() const;

  private:
    trait<_Service>::sp m_pService;
    trait<_QueryMapper>::sp m_pQueryMapper;
  };

  class _SetAutoCommitJob : public _AsyncWorkerJob
  {
  public:
    _SetAutoCommitJob(trait<_ExecutorHandler>::sp pExecHandler,
        unsigned long ulTimeOutMilSec, bool bIsAutoCommit);
    virtual ~_SetAutoCommitJob() {}

    virtual trait<_AsyncWorkerJobResult>::sp doExecute();

  public:
    virtual const char *getJobName() const;

  private:
    bool m_bIsAutoCommit;
  };

  class _CommitJob : public _AsyncWorkerJob
  {
  public:
    _CommitJob(trait<_ExecutorHandler>::sp pExecHandler,
        unsigned long ulTimeOutMilSec);
    virtual ~_CommitJob() {}

    virtual trait<_AsyncWorkerJobResult>::sp doExecute();

  public:
    virtual const char *getJobName() const;
  };

  class _RollbackJob : public _AsyncWorkerJob
  {
  public:
    _RollbackJob(trait<_ExecutorHandler>::sp pExecHandler,
        unsigned long ulTimeOutMilSec);
    virtual ~_RollbackJob() {}

    virtual trait<_AsyncWorkerJobResult>::sp doExecute();

  public:
    virtual const char *getJobName() const;
  };

  class _ExecuteQueryJob : public _AsyncWorkerJob
  {
  public:
    _ExecuteQueryJob(trait<_Service>::sp pService,
        trait<_QueryMapper>::sp pQueryMapper, unsigned long ulTimeOutMilSec,
        const char *szSqlName, const _Parameter *pParameter);
    _ExecuteQueryJob(trait<_ExecutorHandler>::sp pExecHandler,
        unsigned long ulTimeOutMilSec, const char *szSqlName,
        const _Parameter *pParameter);
    virtual ~_ExecuteQueryJob() {}

    virtual trait<_AsyncWorkerJobResult>::sp doExecute();

  public:
    virtual const char *getJobName() const;
    virtual const char *getSqlName() const;

  private:
    std::string m_sqlName;
    _Parameter m_parameter;
  };

  class _ExecuteQueryBatchJob : public _AsyncWorkerJob
  {
  public:
    _ExecuteQueryBatchJob(trait<_Service>::sp pService,
        trait<_QueryMapper>::sp pQueryMapper, unsigned long ulTimeOutMilSec,
        const char *szSqlName, const _ParameterList &parameterList);
    _ExecuteQueryBatchJob(trait<_ExecutorHandler>::sp pExecHandler,
        unsigned long ulTimeOutMilSec, const char *szSqlName,
        const _ParameterList &parameterList);
    virtual ~_ExecuteQueryBatchJob() {}

    virtual trait<_AsyncWorkerJobResult>::sp doExecute();

  public:
    virtual const char *getJobName() const;
    virtual const char *getSqlName() const;

  private:
    std::string m_sqlName;
    const _ParameterList &m_parameterList;
  };

  class _CreateClobJob : public _AsyncWorkerJob
  {
  public:
    _CreateClobJob(trait<_ExecutorHandler>::sp pExecHandler,
        unsigned long ulTimeOutMilSec);
    virtual ~_CreateClobJob() {}

    virtual trait<_AsyncWorkerJobResult>::sp doExecute();

  public:
    virtual const char *getJobName() const;
  };

  class _CreateBlobJob : public _AsyncWorkerJob
  {
  public:
    _CreateBlobJob(trait<_ExecutorHandler>::sp pExecHandler,
        unsigned long ulTimeOutMilSec);
    virtual ~_CreateBlobJob() {}

    virtual trait<_AsyncWorkerJobResult>::sp doExecute();

  public:
    virtual const char *getJobName() const;
  };

  class _WorkerJobManager : public system::_ThreadEx
  {
  public:
    static size_t DEFAULT_MAX_SIZE();

  public:
    _WorkerJobManager(_AsyncWorkerPool *pWorkerPool,
        system::_ThreadFunctionEx pFunc);
    virtual ~_WorkerJobManager();

    trait<_AsyncWorkerJobResult>::sp delegateJob(trait<_AsyncWorkerJob>::sp pJob);
    int delegateJobAsync(trait<_AsyncWorkerJob>::sp pJob,
        ExecAsyncCallBack pCallBack, void *pData);
    int delegateJobAsync(trait<_AsyncWorkerJob>::sp pJob,
        ExecBatchAsyncCallBack pCallBack, void *pData);
    void setMaxSize(size_t nMaxSize);

  protected:
    trait<_AsyncWorkerJob>::sp waitAndGetJob();
    trait<_AsyncWorkerJob>::sp getJob();
    trait<_AsyncWorker>::sp getWorker();
    void removeJob();

  private:
    class Impl;
    Impl *m_pImpl;
  };

  class _AsyncWorkerJobManager : public _WorkerJobManager
  {
  public:
    _AsyncWorkerJobManager(_AsyncWorkerPool *pWorkerPool);
    virtual ~_AsyncWorkerJobManager();

    static void run(const system::_ThreadEx *pThread);
  };

  class _TimeoutWorkerJobManager : public _WorkerJobManager
  {
  public:
    _TimeoutWorkerJobManager(_AsyncWorkerPool *pWorkerPool);
    virtual ~_TimeoutWorkerJobManager();

    static void run(const system::_ThreadEx *pThread);
  };

}

#endif
