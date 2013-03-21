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

  enum _AsyncWorkerJobStatus
  {
    DBGW_ASYNC_JOB_STATUS_IDLE = 0,
    DBGW_ASYNC_JOB_STATUS_BUSY,
    DBGW_ASYNC_JOB_STATUS_DONE,
    DBGW_ASYNC_JOB_STATUS_CANCEL
  };

  class _AsyncWorkerJob : public boost::enable_shared_from_this<_AsyncWorkerJob>
  {
  public:
    _AsyncWorkerJob();
    virtual ~_AsyncWorkerJob();

    void execute();
    void cancel();
    bool isDone();
    void bindWaiter(trait<_AsyncWaiter>::sp pWaiter);
    void bindWorker(trait<_AsyncWorker>::sp pWorker);

  public:
    virtual const char *getJobName() const = 0;
    virtual const char *getSqlName() const;
    unsigned long getAbsTimeOutMilSec() const;
    trait<_ExecutorHandler>::sp getExecutorHandler() const;

  protected:
    virtual void doExecute() = 0;
    virtual void setExecutorHandler(trait<_ExecutorHandler>::sp pExecHandler);

  private:
    class Impl;
    Impl *m_pImpl;
  };

  class _GetExecutorHandlerJob : public _AsyncWorkerJob
  {
  public:
    _GetExecutorHandlerJob(trait<_Service>::sp pService,
        trait<_QueryMapper>::sp pQueryMapper);
    virtual ~_GetExecutorHandlerJob() {}

    virtual void doExecute();

  public:
    virtual const char *getJobName() const;

  private:
    trait<_Service>::sp m_pService;
    trait<_QueryMapper>::sp m_pQueryMapper;
  };

  class _ExecutorHandlerJobBase : public _AsyncWorkerJob
  {
  public:
    _ExecutorHandlerJobBase(trait<_ExecutorHandler>::sp pExecHandler);
    virtual ~_ExecutorHandlerJobBase() {}
  };

  class _SetAutoCommitJob : public _ExecutorHandlerJobBase
  {
  public:
    _SetAutoCommitJob(trait<_ExecutorHandler>::sp pExecHandler,
        bool bIsAutoCommit);
    virtual ~_SetAutoCommitJob() {}

    virtual void doExecute();

  public:
    virtual const char *getJobName() const;

  private:
    bool m_bIsAutoCommit;
  };

  class _CommitJob : public _ExecutorHandlerJobBase
  {
  public:
    _CommitJob(trait<_ExecutorHandler>::sp pExecHandler);
    virtual ~_CommitJob() {}

    virtual void doExecute();

  public:
    virtual const char *getJobName() const;
  };

  class _RollbackJob : public _ExecutorHandlerJobBase
  {
  public:
    _RollbackJob(trait<_ExecutorHandler>::sp pExecHandler);
    virtual ~_RollbackJob() {}

    virtual void doExecute();

  public:
    virtual const char *getJobName() const;
  };

  class _ExecuteQueryJob : public _ExecutorHandlerJobBase
  {
  public:
    _ExecuteQueryJob(trait<_ExecutorHandler>::sp pExecHandler,
        const char *szSqlName, const _Parameter *pParameter);
    virtual ~_ExecuteQueryJob() {}

    virtual void doExecute();

  public:
    virtual const char *getJobName() const;
    virtual const char *getSqlName() const;

  private:
    std::string m_sqlName;
    _Parameter m_parameter;
  };

  class _ExecuteQueryBatchJob : public _ExecutorHandlerJobBase
  {
  public:
    _ExecuteQueryBatchJob(trait<_ExecutorHandler>::sp pExecHandler,
        const char *szSqlName, const _ParameterList &parameterList);
    virtual ~_ExecuteQueryBatchJob() {}

    virtual void doExecute();

  public:
    virtual const char *getJobName() const;
    virtual const char *getSqlName() const;

  private:
    std::string m_sqlName;
    const _ParameterList &m_parameterList;
  };

  class _CreateClobJob : public _ExecutorHandlerJobBase
  {
  public:
    _CreateClobJob(trait<_ExecutorHandler>::sp pExecHandler);
    virtual ~_CreateClobJob() {}

    virtual void doExecute();
    trait<Lob>::sp getLob();

  public:
    virtual const char *getJobName() const;

  private:
    trait<Lob>::sp m_pLob;
  };

  class _CreateBlobJob : public _ExecutorHandlerJobBase
  {
  public:
    _CreateBlobJob(trait<_ExecutorHandler>::sp pExecHandler);
    virtual ~_CreateBlobJob() {}

    virtual void doExecute();
    trait<Lob>::sp getLob();

  public:
    virtual const char *getJobName() const;

  private:
    trait<Lob>::sp m_pLob;
  };

}

#endif
