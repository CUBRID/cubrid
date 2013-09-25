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
#include "dbgw3/system/DBGWPorting.h"
#include "dbgw3/sql/DatabaseInterface.h"
#include "dbgw3/client/Client.h"
#include "dbgw3/client/AsyncWorkerJob.h"
#include "dbgw3/client/AsyncWaiter.h"
#include "dbgw3/client/ClientResultSet.h"
#include "dbgw3/client/ExecutorHandler.h"
#include "dbgw3/client/AsyncWorker.h"
#include "dbgw3/client/Connector.h"
#include "dbgw3/client/Query.h"
#include "dbgw3/client/QueryMapper.h"

namespace dbgw
{

  _AsyncWorkerJobResult::_AsyncWorkerJobResult(const Exception &exception) :
    m_exception(exception)
  {
  }

  _AsyncWorkerJobResult::_AsyncWorkerJobResult(
      trait<_ExecutorHandler>::sp pExecHandler, const Exception &exception) :
    m_pExecHandler(pExecHandler), m_exception(exception)
  {
  }

  _AsyncWorkerJobResult::_AsyncWorkerJobResult(
      trait<ClientResultSet>::sp pResultSet, const Exception &exception) :
    m_pResultSet(pResultSet), m_exception(exception)
  {
  }

  _AsyncWorkerJobResult::_AsyncWorkerJobResult(
      trait<ClientResultSet>::spvector resultSetList, const Exception &exception) :
    m_resultSetList(resultSetList), m_exception(exception)
  {
  }

  _AsyncWorkerJobResult::_AsyncWorkerJobResult(trait<Lob>::sp pLob,
      const Exception &exception) :
    m_pLob(pLob), m_exception(exception)
  {
  }

  _AsyncWorkerJobResult::~_AsyncWorkerJobResult()
  {
  }

  trait<_ExecutorHandler>::sp _AsyncWorkerJobResult::getExecutorHandler()
  {
    return m_pExecHandler;
  }

  trait<ClientResultSet>::sp _AsyncWorkerJobResult::getResultSet()
  {
    return m_pResultSet;
  }

  trait<ClientResultSet>::spvector _AsyncWorkerJobResult::getResultSetList()
  {
    return m_resultSetList;
  }

  trait<Lob>::sp _AsyncWorkerJobResult::getLob()
  {
    return m_pLob;
  }

  const Exception &_AsyncWorkerJobResult::getException()
  {
    return m_exception;
  }

  class _AsyncWorkerJob::Impl
  {
  public:
    Impl(_AsyncWorkerJob *pSelf, trait<_Service>::sp pService,
        trait<_QueryMapper>::sp pQueryMapper, unsigned long ulTimeOutMilSec) :
      m_pSelf(pSelf), m_pService(pService), m_pQueryMapper(pQueryMapper),
      m_status(DBGW_ASYNC_JOB_STATUS_IDLE), m_ulTimeOutMilSec(ulTimeOutMilSec),
      m_ulAbsTimeOutMilSec(0), needReleaseExecutor(false)
    {
      if (ulTimeOutMilSec > system::INFINITE_TIMEOUT)
        {
          m_ulAbsTimeOutMilSec = system::getCurrTimeMilSec() + ulTimeOutMilSec;
        }
    }

    Impl(_AsyncWorkerJob *pSelf, trait<_ExecutorHandler>::sp pExecHandler,
        unsigned long ulTimeOutMilSec) :
      m_pSelf(pSelf), m_pExecHandler(pExecHandler),
      m_status(DBGW_ASYNC_JOB_STATUS_IDLE), m_ulTimeOutMilSec(ulTimeOutMilSec),
      m_ulAbsTimeOutMilSec(0), needReleaseExecutor(false)
    {
      if (ulTimeOutMilSec > system::INFINITE_TIMEOUT)
        {
          m_ulAbsTimeOutMilSec = system::getCurrTimeMilSec() + ulTimeOutMilSec;
        }
    }

    ~Impl()
    {
    }

    void execute()
    {
      system::_MutexAutoLock lock(&m_mutex);

      m_status = DBGW_ASYNC_JOB_STATUS_BUSY;

      lock.unlock();

      trait<_AsyncWorkerJobResult>::sp pJobResult = m_pSelf->doExecute();

      lock.lock();

      if (m_status == DBGW_ASYNC_JOB_STATUS_BUSY)
        {
          m_status = DBGW_ASYNC_JOB_STATUS_DONE;

          notify(pJobResult);
        }
    }

    void cancel()
    {
      system::_MutexAutoLock lock(&m_mutex);

      if (m_status == DBGW_ASYNC_JOB_STATUS_BUSY)
        {
          m_status = DBGW_ASYNC_JOB_STATUS_CANCEL;

          ExecuteTimeoutExecption e(m_pWaiter->getTimeOutMilSec());
          DBGW_LOG_ERROR(e.what());

          trait<_AsyncWorkerJobResult>::sp pJobResult(
              new _AsyncWorkerJobResult(e));

          notify(pJobResult);
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

    unsigned long getTimeOutMilSec() const
    {
      return m_ulTimeOutMilSec;
    }

    unsigned long long int getAbsTimeOutMilSec() const
    {
      return m_ulAbsTimeOutMilSec;
    }

    void makeExecutorHandler()
    {
      m_pExecHandler = trait<_ExecutorHandler>::sp(
          new _ExecutorHandler(m_pService, m_pQueryMapper));
    }

    bool bindExecutorHandler()
    {
      if (m_pExecHandler != NULL)
        {
          return true;
        }
      else
        {
          try
            {
              makeExecutorHandler();

              /* async execution must be executed autotran. */
              m_pExecHandler->setAutoCommit(true);

              needReleaseExecutor = true;
            }
          catch (Exception &e)
            {
              if (e.getErrorCode() == DBGW_ER_CLIENT_CREATE_MAX_CONNECTION)
                {
                  return false;
                }
              else
                {
                  throw;
                }
            }

          return true;
        }
    }

    trait<_ExecutorHandler>::sp getExecutorHandler() const
    {
      return m_pExecHandler;
    }

  private:
    void notify(trait<_AsyncWorkerJobResult>::sp pJobResult)
    {
      if (m_status == DBGW_ASYNC_JOB_STATUS_DONE)
        {
          if (m_pWorker != NULL)
            {
              m_pWorker->changeWorkerState(DBGW_WORKTER_STATE_IDLE,
                  m_pSelf->shared_from_this());
              m_pWorker->release(false);
            }

          if (m_pExecHandler != NULL && needReleaseExecutor)
            {
              m_pExecHandler->release(false);
            }
        }
      else if (m_status == DBGW_ASYNC_JOB_STATUS_CANCEL)
        {
          if (m_pWorker != NULL)
            {
              m_pWorker->changeWorkerState(DBGW_WORKTER_STATE_TIMEOUT,
                  m_pSelf->shared_from_this());
              m_pWorker->release(true);
            }

          if (m_pExecHandler != NULL && needReleaseExecutor)
            {
              m_pExecHandler->release(true);
            }
        }

      m_pWaiter->bindJobResult(pJobResult);

      m_pWaiter->notify();
      m_pExecHandler.reset();
    }

  private:
    _AsyncWorkerJob *m_pSelf;
    trait<_AsyncWaiter>::sp m_pWaiter;
    trait<_AsyncWorker>::sp m_pWorker;
    trait<_ExecutorHandler>::sp m_pExecHandler;
    trait<_Service>::sp m_pService;
    trait<_QueryMapper>::sp m_pQueryMapper;
    system::_Mutex m_mutex;
    Exception m_exception;
    _AsyncWorkerJobStatus m_status;
    unsigned long m_ulTimeOutMilSec;
    unsigned long long int m_ulAbsTimeOutMilSec;
    bool needReleaseExecutor;
  };

  _AsyncWorkerJob::_AsyncWorkerJob(trait<_Service>::sp pService,
      trait<_QueryMapper>::sp pQueryMapper, unsigned long ulTimeOutMilSec) :
    m_pImpl(new Impl(this, pService, pQueryMapper, ulTimeOutMilSec))
  {
  }

  _AsyncWorkerJob::_AsyncWorkerJob(trait<_ExecutorHandler>::sp pExecHandler,
      unsigned long ulTimeOutMilSec) :
    m_pImpl(new Impl(this, pExecHandler, ulTimeOutMilSec))
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

  void _AsyncWorkerJob::makeExecutorHandler()
  {
    m_pImpl->makeExecutorHandler();
  }

  bool _AsyncWorkerJob::bindExecutorHandler()
  {
    return m_pImpl->bindExecutorHandler();
  }

  const char *_AsyncWorkerJob::getSqlName() const
  {
    return "-";
  }

  unsigned long _AsyncWorkerJob::getTimeOutMilSec() const
  {
    return m_pImpl->getTimeOutMilSec();
  }

  unsigned long long int _AsyncWorkerJob::getAbsTimeOutMilSec() const
  {
    return m_pImpl->getAbsTimeOutMilSec();
  }

  trait<_ExecutorHandler>::sp _AsyncWorkerJob::getExecutorHandler() const
  {
    return m_pImpl->getExecutorHandler();
  }

  _GetExecutorHandlerJob::_GetExecutorHandlerJob(trait<_Service>::sp pService,
      trait<_QueryMapper>::sp pQueryMapper, unsigned long ulTimeOutMilSec) :
    _AsyncWorkerJob(pService, pQueryMapper, ulTimeOutMilSec)
  {
  }

  trait<_AsyncWorkerJobResult>::sp _GetExecutorHandlerJob::doExecute()
  {
    Exception exception;
    trait<_ExecutorHandler>::sp pExecHandler;

    try
      {
        makeExecutorHandler();
        pExecHandler = getExecutorHandler();
      }
    catch (Exception &e)
      {
        exception = e;
      }

    trait<_AsyncWorkerJobResult>::sp pJobResult(
        new _AsyncWorkerJobResult(pExecHandler, exception));
    return pJobResult;
  }

  const char *_GetExecutorHandlerJob::getJobName() const
  {
    return "CONNECT";
  }

  _SetAutoCommitJob::_SetAutoCommitJob(trait<_ExecutorHandler>::sp pExecHandler,
      unsigned long ulTimeOutMilSec, bool bIsAutoCommit) :
    _AsyncWorkerJob(pExecHandler, ulTimeOutMilSec),
    m_bIsAutoCommit(bIsAutoCommit)
  {
  }

  trait<_AsyncWorkerJobResult>::sp _SetAutoCommitJob::doExecute()
  {
    Exception exception;
    trait<_ExecutorHandler>::sp pExecHandler = getExecutorHandler();

    pExecHandler->setAutoCommit(m_bIsAutoCommit);
    exception = pExecHandler->getLastException();

    trait<_AsyncWorkerJobResult>::sp pJobResult(
        new _AsyncWorkerJobResult(exception));
    return pJobResult;
  }

  const char *_SetAutoCommitJob::getJobName() const
  {
    return "SET AUTOCOMMIT";
  }

  _CommitJob::_CommitJob(trait<_ExecutorHandler>::sp pExecHandler,
      unsigned long ulTimeOutMilSec) :
    _AsyncWorkerJob(pExecHandler, ulTimeOutMilSec)
  {
  }

  trait<_AsyncWorkerJobResult>::sp _CommitJob::doExecute()
  {
    Exception exception;
    trait<_ExecutorHandler>::sp pExecHandler = getExecutorHandler();

    pExecHandler->commit();
    exception = pExecHandler->getLastException();

    trait<_AsyncWorkerJobResult>::sp pJobResult(
        new _AsyncWorkerJobResult(exception));
    return pJobResult;
  }

  const char *_CommitJob::getJobName() const
  {
    return "COMMIT";
  }

  _RollbackJob::_RollbackJob(trait<_ExecutorHandler>::sp pExecHandler,
      unsigned long ulTimeOutMilSec) :
    _AsyncWorkerJob(pExecHandler, ulTimeOutMilSec)
  {
  }

  trait<_AsyncWorkerJobResult>::sp _RollbackJob::doExecute()
  {
    Exception exception;
    trait<_ExecutorHandler>::sp pExecHandler = getExecutorHandler();

    pExecHandler->rollback();
    exception = pExecHandler->getLastException();

    trait<_AsyncWorkerJobResult>::sp pJobResult(
        new _AsyncWorkerJobResult(exception));
    return pJobResult;
  }

  const char *_RollbackJob::getJobName() const
  {
    return "ROLLBACK";
  }

  _ExecuteQueryJob::_ExecuteQueryJob(trait<_Service>::sp pService,
      trait<_QueryMapper>::sp pQueryMapper, unsigned long ulTimeOutMilSec,
      const char *szSqlName, const _Parameter *pParameter) :
    _AsyncWorkerJob(pService, pQueryMapper, ulTimeOutMilSec), m_sqlName(szSqlName)
  {
    if (pParameter != NULL)
      {
        m_parameter = *pParameter;
      }
  }

  _ExecuteQueryJob::_ExecuteQueryJob(trait<_ExecutorHandler>::sp pExecHandler,
      unsigned long ulTimeOutMilSec, const char *szSqlName,
      const _Parameter *pParameter) :
    _AsyncWorkerJob(pExecHandler, ulTimeOutMilSec), m_sqlName(szSqlName)
  {
    if (pParameter != NULL)
      {
        m_parameter = *pParameter;
      }
  }

  trait<_AsyncWorkerJobResult>::sp _ExecuteQueryJob::doExecute()
  {
    Exception exception;
    trait<ClientResultSet>::sp pResultSet;
    trait<_ExecutorHandler>::sp pExecHandler = getExecutorHandler();

    pExecHandler->execute(m_sqlName, &m_parameter);

    pResultSet = pExecHandler->getResultSet();
    exception = pExecHandler->getLastException();

    trait<_AsyncWorkerJobResult>::sp pJobResult(
        new _AsyncWorkerJobResult(pResultSet, exception));
    return pJobResult;
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
      trait<_ExecutorHandler>::sp pExecHandler, unsigned long ulTimeOutMilSec,
      const char *szSqlName, const _ParameterList &parameterList) :
    _AsyncWorkerJob(pExecHandler, ulTimeOutMilSec), m_sqlName(szSqlName),
    m_parameterList(parameterList)
  {
  }

  trait<_AsyncWorkerJobResult>::sp _ExecuteQueryBatchJob::doExecute()
  {
    Exception exception;
    trait<ClientResultSet>::spvector resultSetList;
    trait<_ExecutorHandler>::sp pExecHandler = getExecutorHandler();

    pExecHandler->executeBatch(m_sqlName, m_parameterList);

    resultSetList = pExecHandler->getResultSetList();
    exception = pExecHandler->getLastException();

    trait<_AsyncWorkerJobResult>::sp pJobResult(
        new _AsyncWorkerJobResult(resultSetList, exception));
    return pJobResult;
  }

  const char *_ExecuteQueryBatchJob::getJobName() const
  {
    return "EXECUTE-BATCH";
  }

  const char *_ExecuteQueryBatchJob::getSqlName() const
  {
    return m_sqlName.c_str();
  }

  _CreateClobJob::_CreateClobJob(trait<_ExecutorHandler>::sp pExecHandler,
      unsigned long ulTimeOutMilSec) :
    _AsyncWorkerJob(pExecHandler, ulTimeOutMilSec)
  {
  }

  trait<_AsyncWorkerJobResult>::sp _CreateClobJob::doExecute()
  {
    Exception exception;
    trait<Lob>::sp pLob;
    trait<_ExecutorHandler>::sp pExecHandler = getExecutorHandler();

    pLob = pExecHandler->createClob();
    exception = pExecHandler->getLastException();

    trait<_AsyncWorkerJobResult>::sp pJobResult(
        new _AsyncWorkerJobResult(pLob, exception));
    return pJobResult;
  }

  const char *_CreateClobJob::getJobName() const
  {
    return "CREATE CLOB";
  }

  _CreateBlobJob::_CreateBlobJob(trait<_ExecutorHandler>::sp pExecHandler,
      unsigned long ulTimeOutMilSec) :
    _AsyncWorkerJob(pExecHandler, ulTimeOutMilSec)
  {
  }

  trait<_AsyncWorkerJobResult>::sp _CreateBlobJob::doExecute()
  {
    Exception exception;
    trait<Lob>::sp pLob;
    trait<_ExecutorHandler>::sp pExecHandler = getExecutorHandler();

    pLob = pExecHandler->createBlob();
    exception = pExecHandler->getLastException();

    trait<_AsyncWorkerJobResult>::sp pJobResult(
        new _AsyncWorkerJobResult(pLob, exception));
    return pJobResult;
  }

  const char *_CreateBlobJob::getJobName() const
  {
    return "CREATE BLOB";
  }

  class _WorkerJobManager::Impl
  {
  public:
    Impl(_WorkerJobManager *pSelf, _AsyncWorkerPool *pWorkerPool) :
      m_pSelf(pSelf), m_nMaxSize(_WorkerJobManager::DEFAULT_MAX_SIZE()),
      m_nCallBackHandleID(0), m_pWorkerPool(pWorkerPool)
    {
    }

    ~Impl()
    {
    }

    trait<_AsyncWorkerJobResult>::sp delegateJob(trait<_AsyncWorkerJob>::sp pJob)
    {
      system::_MutexAutoLock lock(&m_mutex);

      if (m_nMaxSize < m_jobList.size())
        {
          ExecuteAsyncTempUnavailableException e;
          DBGW_LOG_ERROR(e.what());
          throw e;
        }
      else
        {
          trait<_AsyncWaiter>::sp pWaiter(
              new _AsyncWaiter(pJob->getTimeOutMilSec()));
          pJob->bindWaiter(pWaiter);

          m_jobList.push_back(pJob);
          DBGW_LOGF_INFO("<%s> timeout job is waiting worker. (curr : %d, max : %d)",
              pJob->getJobName(), m_jobList.size(), m_nMaxSize);

          m_cond.notify();
          lock.unlock();

          pWaiter->wait();

          if (pJob->isDone() == false)
            {
              ExecuteTimeoutExecption e(pJob->getTimeOutMilSec());
              DBGW_LOG_ERROR(e.what());
              throw e;
            }

          return pWaiter->getJobResult();
        }
    }

    int delegateJobAsync(trait<_AsyncWorkerJob>::sp pJob,
        ExecAsyncCallBack pCallBack, void *pData)
    {
      system::_MutexAutoLock lock(&m_mutex);

      if (m_nMaxSize < m_jobList.size())
        {
          ExecuteAsyncTempUnavailableException e;
          DBGW_LOG_ERROR(e.what());
          throw e;
        }
      else
        {
          if (++m_nCallBackHandleID > UINT_MAX)
            {
              m_nCallBackHandleID = 0;
            }

          trait<_AsyncWaiter>::sp pWaiter(
              new _AsyncWaiter(pJob->getTimeOutMilSec(), m_nCallBackHandleID,
                  pCallBack, pData));
          pJob->bindWaiter(pWaiter);

          m_jobList.push_back(pJob);
          DBGW_LOGF_INFO("<%s> async job is waiting worker. (curr : %d, max : %d)",
              pJob->getJobName(), m_jobList.size(), m_nMaxSize);

          m_cond.notify();
          return m_nCallBackHandleID;
        }
    }

    int delegateJobAsync(trait<_AsyncWorkerJob>::sp pJob,
        ExecBatchAsyncCallBack pCallBack, void *pData)
    {
      system::_MutexAutoLock lock(&m_mutex);

      if (m_nMaxSize < m_jobList.size())
        {
          ExecuteAsyncTempUnavailableException e;
          DBGW_LOG_ERROR(e.what());
          throw e;
        }
      else
        {
          if (++m_nCallBackHandleID > UINT_MAX)
            {
              m_nCallBackHandleID = 0;
            }

          trait<_AsyncWaiter>::sp pWaiter(
              new _AsyncWaiter(pJob->getTimeOutMilSec(), m_nCallBackHandleID,
                  pCallBack, pData));
          pJob->bindWaiter(pWaiter);

          m_jobList.push_back(pJob);
          DBGW_LOGF_INFO("<%s> async job is waiting worker. (curr : %d, max : %d)",
              pJob->getJobName(), m_jobList.size(), m_nMaxSize);

          m_cond.notify();
          return m_nCallBackHandleID;
        }
    }

    void setMaxSize(size_t nMaxSize)
    {
      m_nMaxSize = nMaxSize;
    }

    trait<_AsyncWorkerJob>::sp waitAndGetJob()
    {
      system::_MutexAutoLock lock(&m_mutex);

      bool bIsRunning = false;
      while ((bIsRunning = m_pSelf->isRunning()) && m_jobList.empty())
        {
          m_cond.timedWait(&m_mutex, 100);
        }

      lock.unlock();

      if (bIsRunning)
        {
          return m_jobList.front();
        }
      else
        {
          return trait<_AsyncWorkerJob>::sp();
        }
    }

    trait<_AsyncWorkerJob>::sp getJob()
    {
      system::_MutexAutoLock lock(&m_mutex);

      if (m_pSelf->isRunning() == false || m_jobList.empty())
        {
          return trait<_AsyncWorkerJob>::sp();
        }
      else
        {
          return m_jobList.front();
        }
    }

    trait<_AsyncWorker>::sp getWorker()
    {
      return m_pWorkerPool->getAsyncWorker();
    }

    void removeJob()
    {
      system::_MutexAutoLock lock(&m_mutex);

      m_jobList.pop_front();
    }

  private:
    _WorkerJobManager *m_pSelf;
    system::_Mutex m_mutex;
    system::_ConditionVariable m_cond;
    trait<_AsyncWorkerJob>::splist m_jobList;
    size_t m_nMaxSize;
    size_t m_nCallBackHandleID;
    _AsyncWorkerPool *m_pWorkerPool;
  };

  size_t _WorkerJobManager::DEFAULT_MAX_SIZE()
  {
    return 1024;
  }

  _WorkerJobManager::_WorkerJobManager(_AsyncWorkerPool *pWorkerPool,
      system::_ThreadFunctionEx pFunc) :
    system::_ThreadEx(pFunc), m_pImpl(new Impl(this, pWorkerPool))
  {
  }

  _WorkerJobManager::~_WorkerJobManager()
  {
    if (m_pImpl != NULL)
      {
        delete m_pImpl;
      }
  }

  trait<_AsyncWorkerJobResult>::sp _WorkerJobManager::delegateJob(
      trait<_AsyncWorkerJob>::sp pJob)
  {
    return m_pImpl->delegateJob(pJob);
  }

  int _WorkerJobManager::delegateJobAsync(trait<_AsyncWorkerJob>::sp pJob,
      ExecAsyncCallBack pCallBack, void *pData)
  {
    return m_pImpl->delegateJobAsync(pJob, pCallBack, pData);
  }

  int _WorkerJobManager::delegateJobAsync(trait<_AsyncWorkerJob>::sp pJob,
      ExecBatchAsyncCallBack pCallBack, void *pData)
  {
    return m_pImpl->delegateJobAsync(pJob, pCallBack, pData);
  }

  void _WorkerJobManager::setMaxSize(size_t nMaxSize)
  {
    m_pImpl->setMaxSize(nMaxSize);
  }

  trait<_AsyncWorkerJob>::sp _WorkerJobManager::waitAndGetJob()
  {
    return m_pImpl->waitAndGetJob();
  }

  trait<_AsyncWorkerJob>::sp _WorkerJobManager::getJob()
  {
    return m_pImpl->getJob();
  }

  trait<_AsyncWorker>::sp _WorkerJobManager::getWorker()
  {
    return m_pImpl->getWorker();
  }

  void _WorkerJobManager::removeJob()
  {
    m_pImpl->removeJob();
  }

  _AsyncWorkerJobManager::_AsyncWorkerJobManager(_AsyncWorkerPool *pWorkerPool) :
    _WorkerJobManager(pWorkerPool, _AsyncWorkerJobManager::run)
  {
  }

  _AsyncWorkerJobManager::~_AsyncWorkerJobManager()
  {
  }

  void _AsyncWorkerJobManager::run(const system::_ThreadEx *pThread)
  {
    if (pThread == NULL)
      {
        FailedToCreateThreadException e("job manager");
        DBGW_LOG_ERROR(e.what());
        return;
      }

    _AsyncWorkerJobManager *pJobManager = (_AsyncWorkerJobManager *) pThread;
    trait<_AsyncWorkerJob>::sp pJob;
    trait<_AsyncWorker>::sp pWorker;

    while ((pJob = pJobManager->waitAndGetJob()) != NULL)
      {
        do
          {
            if (pJob->isDone() == false)
              {
                try
                  {
                    pWorker = pJobManager->getWorker();

                    if (pJob->bindExecutorHandler())
                      {
                        pWorker->delegateJob(pJob);
                      }
                    else
                      {
                        pWorker->release(false);
                        SLEEP_MILISEC(0, 10);
                        continue;
                      }
                  }
                catch (Exception &e)
                  {
                    DBGW_LOG_ERROR(e.what());
                    pJob->cancel();
                  }
              }

            pJobManager->removeJob();
            pJob.reset();
            pWorker.reset();
          }
        while ((pJob = pJobManager->getJob()) != NULL);
      }
  }


  _TimeoutWorkerJobManager::_TimeoutWorkerJobManager(
      _AsyncWorkerPool *pWorkerPool) :
    _WorkerJobManager(pWorkerPool, _TimeoutWorkerJobManager::run)
  {
  }

  _TimeoutWorkerJobManager::~_TimeoutWorkerJobManager()
  {
  }

  void _TimeoutWorkerJobManager::run(const system::_ThreadEx *pThread)
  {
    if (pThread == NULL)
      {
        FailedToCreateThreadException e("job manager");
        DBGW_LOG_ERROR(e.what());
        return;
      }

    _TimeoutWorkerJobManager *pJobManager =
        (_TimeoutWorkerJobManager *) pThread;
    trait<_AsyncWorkerJob>::sp pJob;
    trait<_AsyncWorker>::sp pWorker;

    while ((pJob = pJobManager->waitAndGetJob()) != NULL)
      {
        do
          {
            if (pJob->isDone() == false)
              {
                try
                  {
                    pWorker = pJobManager->getWorker();
                    pWorker->delegateJob(pJob);
                  }
                catch (Exception &e)
                  {
                    DBGW_LOG_ERROR(e.what());
                    pJob->cancel();
                  }
              }

            pJobManager->removeJob();
            pJob.reset();
            pWorker.reset();
          }
        while ((pJob = pJobManager->getJob()) != NULL);
      }
  }

}
