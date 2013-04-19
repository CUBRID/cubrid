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

#include "dbgw3/Common.h"
#include "dbgw3/Logger.h"
#include "dbgw3/Value.h"
#include "dbgw3/ValueSet.h"
#include "dbgw3/system/ConditionVariable.h"
#include "dbgw3/system/Mutex.h"
#include "dbgw3/system/Time.h"
#include "dbgw3/client/Client.h"
#include "dbgw3/client/AsyncWorkerJob.h"
#include "dbgw3/client/AsyncWaiter.h"
#include "dbgw3/client/ClientResultSet.h"
#include "dbgw3/client/ExecutorHandler.h"
#include "dbgw3/client/AsyncWorker.h"

namespace dbgw
{

  class _AsyncWorkerJob::Impl
  {
  public:
    Impl(_AsyncWorkerJob *pSelf) :
      m_pSelf(pSelf), m_status(DBGW_ASYNC_JOB_STATUS_IDLE)
    {
    }

    ~Impl()
    {
      if (m_pExecHandler != NULL)
        {
          m_pExecHandler->release(false);
        }
    }

    void execute()
    {
      system::_MutexAutoLock lock(&m_mutex);

      m_status = DBGW_ASYNC_JOB_STATUS_BUSY;

      lock.unlock();

      m_pSelf->doExecute();

      lock.lock();

      if (m_status == DBGW_ASYNC_JOB_STATUS_BUSY)
        {
          m_status = DBGW_ASYNC_JOB_STATUS_DONE;
          notify();
        }
    }

    void cancel()
    {
      system::_MutexAutoLock lock(&m_mutex);

      if (m_status != DBGW_ASYNC_JOB_STATUS_DONE)
        {
          m_status = DBGW_ASYNC_JOB_STATUS_CANCEL;
          notify();
        }
    }

    bool isDone()
    {
      system::_MutexAutoLock lock(&m_mutex);

      return m_status == DBGW_ASYNC_JOB_STATUS_DONE;
    }

    void bindWaiter(trait<_AsyncWaiter>::sp pWaiter)
    {
      m_pWaiter = pWaiter;
    }

    void bindWorker(trait<_AsyncWorker>::sp pWorker)
    {
      m_pWorker = pWorker;
    }

    uint64_t getAbsTimeOutMilSec() const
    {
      return m_pWaiter->getAbsTimeOutMilSec();
    }

    trait<_ExecutorHandler>::sp getExecutorHandler() const
    {
      return m_pExecHandler;
    }

    void setExecutorHandler(trait<_ExecutorHandler>::sp pExecHandler)
    {
      m_pExecHandler = pExecHandler;
    }

  private:
    void notify()
    {
      if (m_status == DBGW_ASYNC_JOB_STATUS_DONE)
        {
          if (m_pExecHandler != NULL)
            {
              m_pWaiter->bindResultSet(m_pExecHandler->getResultSet());
              m_pWaiter->bindResultSetList(m_pExecHandler->getResultSetList());
              m_pWaiter->bindException(m_pExecHandler->getLastException());
            }

          m_pWorker->changeWorkerState(DBGW_WORKTER_STATE_IDLE,
              m_pSelf->shared_from_this());
        }
      else if (m_status == DBGW_ASYNC_JOB_STATUS_CANCEL)
        {
          if (m_pExecHandler != NULL)
            {
              ExecuteTimeoutExecption e(m_pWaiter->getTimeOutMilSec());
              DBGW_LOG_ERROR(e.what());
              m_pWaiter->bindException(e);
            }

          m_pWorker->changeWorkerState(DBGW_WORKTER_STATE_TIMEOUT,
              m_pSelf->shared_from_this());
        }

      m_pWaiter->notify();
    }

  private:
    _AsyncWorkerJob *m_pSelf;
    trait<_AsyncWaiter>::sp m_pWaiter;
    trait<_AsyncWorker>::sp m_pWorker;
    trait<_ExecutorHandler>::sp m_pExecHandler;
    system::_Mutex m_mutex;
    _AsyncWorkerJobStatus m_status;
  };

  _AsyncWorkerJob::_AsyncWorkerJob() :
    m_pImpl(new Impl(this))
  {
  }

  _AsyncWorkerJob::~_AsyncWorkerJob()
  {
    if (m_pImpl != NULL)
      {
        delete m_pImpl;
      }
  }

  void _AsyncWorkerJob::execute()
  {
    m_pImpl->execute();
  }

  void _AsyncWorkerJob::cancel()
  {
    m_pImpl->cancel();
  }

  bool _AsyncWorkerJob::isDone()
  {
    return m_pImpl->isDone();
  }

  void _AsyncWorkerJob::bindWaiter(trait<_AsyncWaiter>::sp pWaiter)
  {
    return m_pImpl->bindWaiter(pWaiter);
  }

  void _AsyncWorkerJob::bindWorker(trait<_AsyncWorker>::sp pWorker)
  {
    return m_pImpl->bindWorker(pWorker);
  }

  const char *_AsyncWorkerJob::getSqlName() const
  {
    return "-";
  }

  uint64_t _AsyncWorkerJob::getAbsTimeOutMilSec() const
  {
    return m_pImpl->getAbsTimeOutMilSec();
  }

  trait<_ExecutorHandler>::sp _AsyncWorkerJob::getExecutorHandler() const
  {
    return m_pImpl->getExecutorHandler();
  }

  void _AsyncWorkerJob::setExecutorHandler(
      trait<_ExecutorHandler>::sp pExecHandler)
  {
    m_pImpl->setExecutorHandler(pExecHandler);
  }

  _GetExecutorHandlerJob::_GetExecutorHandlerJob(trait<_Service>::sp pService,
      trait<_QueryMapper>::sp pQueryMapper) :
    m_pService(pService), m_pQueryMapper(pQueryMapper)
  {
  }

  void _GetExecutorHandlerJob::doExecute()
  {
    trait<_ExecutorHandler>::sp p(
        new _ExecutorHandler(m_pService, m_pQueryMapper));
    setExecutorHandler(p);
  }

  const char *_GetExecutorHandlerJob::getJobName() const
  {
    return "CONNECT";
  }

  _ExecutorHandlerJobBase::_ExecutorHandlerJobBase(
      trait<_ExecutorHandler>::sp pExecHandler)
  {
    setExecutorHandler(pExecHandler);
  }

  _SetAutoCommitJob::_SetAutoCommitJob(trait<_ExecutorHandler>::sp pExecHandler,
      bool bIsAutoCommit) :
    _ExecutorHandlerJobBase(pExecHandler),
    m_bIsAutoCommit(bIsAutoCommit)
  {
  }

  void _SetAutoCommitJob::doExecute()
  {
    getExecutorHandler()->setAutoCommit(m_bIsAutoCommit);
  }

  const char *_SetAutoCommitJob::getJobName() const
  {
    return "SET AUTOCOMMIT";
  }

  _CommitJob::_CommitJob(trait<_ExecutorHandler>::sp pExecHandler) :
    _ExecutorHandlerJobBase(pExecHandler)
  {
  }

  void _CommitJob::doExecute()
  {
    getExecutorHandler()->commit();
  }

  const char *_CommitJob::getJobName() const
  {
    return "COMMIT";
  }

  _RollbackJob::_RollbackJob(trait<_ExecutorHandler>::sp pExecHandler) :
    _ExecutorHandlerJobBase(pExecHandler)
  {
  }

  void _RollbackJob::doExecute()
  {
    getExecutorHandler()->rollback();
  }

  const char *_RollbackJob::getJobName() const
  {
    return "ROLLBACK";
  }

  _ExecuteQueryJob::_ExecuteQueryJob(trait<_ExecutorHandler>::sp pExecHandler,
      const char *szSqlName, const _Parameter *pParameter) :
    _ExecutorHandlerJobBase(pExecHandler), m_sqlName(szSqlName)
  {
    if (pParameter != NULL)
      {
        m_parameter = *pParameter;
      }
  }

  void _ExecuteQueryJob::doExecute()
  {
    getExecutorHandler()->execute(m_sqlName, &m_parameter);
  }

  const char *_ExecuteQueryJob::getJobName() const
  {
    return "EXECUTE";
  }

  const char *_ExecuteQueryJob::getSqlName() const
  {
    return m_sqlName.c_str();
  }

  _ExecuteQueryBatchJob::_ExecuteQueryBatchJob(
      trait<_ExecutorHandler>::sp pExecHandler, const char *szSqlName,
      const _ParameterList &parameterList) :
    _ExecutorHandlerJobBase(pExecHandler), m_sqlName(szSqlName),
    m_parameterList(parameterList)
  {
  }

  void _ExecuteQueryBatchJob::doExecute()
  {
    getExecutorHandler()->executeBatch(m_sqlName, m_parameterList);
  }

  const char *_ExecuteQueryBatchJob::getJobName() const
  {
    return "EXECUTE-BATCH";
  }

  const char *_ExecuteQueryBatchJob::getSqlName() const
  {
    return m_sqlName.c_str();
  }

  _CreateClobJob::_CreateClobJob(trait<_ExecutorHandler>::sp pExecHandler) :
    _ExecutorHandlerJobBase(pExecHandler)
  {
  }

  void _CreateClobJob::doExecute()
  {
    m_pLob = getExecutorHandler()->createClob();
  }

  trait<Lob>::sp _CreateClobJob::getLob()
  {
    return m_pLob;
  }

  const char *_CreateClobJob::getJobName() const
  {
    return "CREATE CLOB";
  }

  _CreateBlobJob::_CreateBlobJob(trait<_ExecutorHandler>::sp pExecHandler) :
    _ExecutorHandlerJobBase(pExecHandler)
  {
  }

  void _CreateBlobJob::doExecute()
  {
    m_pLob = getExecutorHandler()->createBlob();
  }

  trait<Lob>::sp _CreateBlobJob::getLob()
  {
    return m_pLob;
  }

  const char *_CreateBlobJob::getJobName() const
  {
    return "CREATE BLOB";
  }

}
