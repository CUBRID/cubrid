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
#include "DBGWClient.h"

namespace dbgw
{

  _DBGWWorkerJob::_DBGWWorkerJob()
  {
  }

  _DBGWWorkerJob::~_DBGWWorkerJob()
  {
  }

  _DBGWWorkerJobBindClientProxy::_DBGWWorkerJobBindClientProxy(
      _DBGWService *pService, _DBGWQueryMapper *pQueryMapper) :
    m_pService(pService), m_pQueryMapper(pQueryMapper)
  {
  }

  _DBGWWorkerJobBindClientProxy::~_DBGWWorkerJobBindClientProxy()
  {
  }

  _DBGWWorkerJobResultSharedPtr _DBGWWorkerJobBindClientProxy::execute()
  {
    _DBGWClientProxySharedPtr pClientProxy;
    DBGWException exception;
    try
      {
        pClientProxy = _DBGWClientProxySharedPtr(new _DBGWClientProxy(
            m_pService, m_pQueryMapper));
      }
    catch (DBGWException &e)
      {
        exception = e;
      }

    _DBGWWorkerJobResultSharedPtr pJobResult(
        new _DBGWWorkerJobResult(pClientProxy, exception));
    return pJobResult;
  }

  _DBGWWorkerJobExecute::_DBGWWorkerJobExecute(
      _DBGWClientProxySharedPtr pClientProxy, const char *szSqlName,
      const _DBGWParameter *pParameter) :
    m_pClientProxy(pClientProxy), m_sqlName(szSqlName)
  {
    if (pParameter != NULL)
      {
        m_parameter = *pParameter;
      }
  }

  _DBGWWorkerJobExecute::~_DBGWWorkerJobExecute()
  {
  }

  _DBGWWorkerJobResultSharedPtr _DBGWWorkerJobExecute::execute()
  {
    DBGWClientResultSetSharedPtr pResult;
    DBGWException exception;

    try
      {
        m_pClientProxy->execute(m_sqlName, m_parameter);
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        exception = e;
      }

    if (exception.getErrorCode() == DBGW_ER_NO_ERROR
        || exception.getErrorCode() == DBGW_ER_CLIENT_VALIDATE_FAIL
        || exception.getErrorCode() == DBGW_ER_CLIENT_VALIDATE_TYPE_FAIL
        || exception.getErrorCode() == DBGW_ER_CLIENT_VALIDATE_VALUE_FAIL)
      {
        pResult = m_pClientProxy->getReusltSet();
      }

    _DBGWWorkerJobResultSharedPtr pJobResult(
        new _DBGWWorkerJobResult(pResult, exception));
    return pJobResult;
  }

  _DBGWWorkerJobExecuteBatch::_DBGWWorkerJobExecuteBatch(
      _DBGWClientProxySharedPtr pClientProxy, const char *szSqlName,
      const _DBGWParameterList &parameterList) :
    m_pClientProxy(pClientProxy), m_sqlName(szSqlName),
    m_parameterList(parameterList)
  {
  }

  _DBGWWorkerJobExecuteBatch::~_DBGWWorkerJobExecuteBatch()
  {
  }

  _DBGWWorkerJobResultSharedPtr _DBGWWorkerJobExecuteBatch::execute()
  {
    DBGWClientBatchResultSetSharedPtr pResult;
    DBGWException exception;

    try
      {
        m_pClientProxy->executeBatch(m_sqlName, m_parameterList);
      }
    catch (DBGWException &e)
      {
        setLastException(e);
        exception = e;
      }

    if (exception.getErrorCode() == DBGW_ER_NO_ERROR
        || exception.getErrorCode() == DBGW_ER_CLIENT_VALIDATE_FAIL
        || exception.getErrorCode() == DBGW_ER_CLIENT_VALIDATE_TYPE_FAIL
        || exception.getErrorCode() == DBGW_ER_CLIENT_VALIDATE_VALUE_FAIL)
      {
        pResult = m_pClientProxy->getBatchReusltSet();
      }

    _DBGWWorkerJobResultSharedPtr pJobResult(
        new _DBGWWorkerJobResult(pResult, exception));
    return pJobResult;
  }

  _DBGWWorkerJobSetAutoCommit::_DBGWWorkerJobSetAutoCommit(
      _DBGWClientProxySharedPtr pClientProxy, bool bAutoCommit) :
    m_pClientProxy(pClientProxy), m_bAutoCommit(bAutoCommit)
  {
  }

  _DBGWWorkerJobSetAutoCommit::~_DBGWWorkerJobSetAutoCommit()
  {
  }

  _DBGWWorkerJobResultSharedPtr _DBGWWorkerJobSetAutoCommit::execute()
  {
    DBGWException exception;

    try
      {
        m_pClientProxy->setAutocommit(m_bAutoCommit);
      }
    catch (DBGWException &e)
      {
        exception = e;
      }

    _DBGWWorkerJobResultSharedPtr pJobResult(new _DBGWWorkerJobResult(exception));
    return pJobResult;
  }

  _DBGWWorkerJobCommit::_DBGWWorkerJobCommit(_DBGWClientProxySharedPtr pClientProxy) :
    m_pClientProxy(pClientProxy)
  {
  }

  _DBGWWorkerJobCommit::~_DBGWWorkerJobCommit()
  {
  }

  _DBGWWorkerJobResultSharedPtr _DBGWWorkerJobCommit::execute()
  {
    DBGWException exception;

    try
      {
        m_pClientProxy->commit();
      }
    catch (DBGWException &e)
      {
        exception = e;
      }

    _DBGWWorkerJobResultSharedPtr pJobResult(new _DBGWWorkerJobResult(exception));
    return pJobResult;
  }

  _DBGWWorkerJobRollback::_DBGWWorkerJobRollback(_DBGWClientProxySharedPtr pClientProxy) :
    m_pClientProxy(pClientProxy)
  {
  }

  _DBGWWorkerJobRollback::~_DBGWWorkerJobRollback()
  {
  }

  _DBGWWorkerJobResultSharedPtr _DBGWWorkerJobRollback::execute()
  {
    DBGWException exception;

    try
      {
        m_pClientProxy->rollback();
      }
    catch (DBGWException &e)
      {
        exception = e;
      }

    _DBGWWorkerJobResultSharedPtr pJobResult(new _DBGWWorkerJobResult(exception));
    return pJobResult;
  }

  _DBGWWorkerJobResult::_DBGWWorkerJobResult(DBGWException e) :
    m_exception(e)
  {
  }

  _DBGWWorkerJobResult::_DBGWWorkerJobResult(_DBGWClientProxySharedPtr pClientProxy,
      DBGWException e) :
    m_pClientProxy(pClientProxy), m_exception(e)
  {
  }

  _DBGWWorkerJobResult::_DBGWWorkerJobResult(DBGWClientResultSetSharedPtr pResult,
      DBGWException e) :
    m_pResult(pResult), m_exception(e)
  {
  }

  _DBGWWorkerJobResult::_DBGWWorkerJobResult(
      DBGWClientBatchResultSetSharedPtr pBatchResult, DBGWException e) :
    m_pBatchResult(pBatchResult), m_exception(e)
  {
  }

  _DBGWWorkerJobResult::~_DBGWWorkerJobResult()
  {
  }

  _DBGWClientProxySharedPtr _DBGWWorkerJobResult::getClientProxy() const
  {
    return m_pClientProxy;
  }

  DBGWClientResultSetSharedPtr _DBGWWorkerJobResult::getResult() const
  {
    return m_pResult;
  }

  DBGWClientBatchResultSetSharedPtr _DBGWWorkerJobResult::getBatchResult() const
  {
    return m_pBatchResult;
  }

  DBGWException _DBGWWorkerJobResult::getException() const
  {
    return m_exception;
  }

  _DBGWWorker::_DBGWWorker(_DBGWWorkerPool &workerPool) :
    m_workerPool(workerPool),
    m_pThread(system::_ThreadFactory::create(run)),
    m_pMutex(system::_MutexFactory::create()),
    m_pCond(system::_ConditionVariableFactory::create())
  {
  }

  _DBGWWorker::~_DBGWWorker()
  {
  }

  void _DBGWWorker::start()
  {
    m_pThread->start(shared_from_this());
  }

  void _DBGWWorker::stop(unsigned long ulMaxWaitExitTimeMilSec)
  {
    m_pThread->timedJoin(ulMaxWaitExitTimeMilSec);
  }

  void _DBGWWorker::bindClientProxy(_DBGWService *pService,
      _DBGWQueryMapper *pQueryMapper, unsigned long ulWaitTimeMilSec)
  {
    if (m_pClientProxy != NULL)
      {
        return;
      }

    _DBGWWorkerJobSharedPtr pJob(new _DBGWWorkerJobBindClientProxy(pService,
        pQueryMapper));

    _DBGWWorkerJobResultSharedPtr pJobResult;
    if (ulWaitTimeMilSec > system::INFINITE_TIMEOUT)
      {
        pJobResult = delegateJob(pJob, ulWaitTimeMilSec);
        if (pJobResult->getClientProxy() == NULL)
          {
            throw pJobResult->getException();
          }
      }
    else
      {
        pJobResult = pJob->execute();
      }

    m_pClientProxy = pJobResult->getClientProxy();
    if (m_pClientProxy == NULL)
      {
        throw pJobResult->getException();
      }
  }

  void _DBGWWorker::releaseClientProxy()
  {
    system::_MutexAutoLock lock(m_pMutex);

    if (m_pClientProxy != NULL)
      {
        if (m_pJob == NULL)
          {
            /**
             * the worker thread is idle.
             * so we can reuse client proxy.
             */
            m_pClientProxy->releaseExecutor();
          }

        m_pClientProxy.reset();
      }
  }

  void _DBGWWorker::forceReleaseClientProxy()
  {
    if (m_pClientProxy != NULL)
      {
        m_pClientProxy->forceReleaseExecutor();
        m_pClientProxy.reset();
      }
  }

  void _DBGWWorker::returnToPool(bool bIsForceDrop)
  {
    if (bIsForceDrop == false)
      {
        m_workerPool.returnWorker(
            boost::shared_static_cast<_DBGWWorker>(shared_from_this()));
      }
  }

  void _DBGWWorker::setAutoCommit(bool bAutoCommit,
      unsigned long ulWaitTimeMilSec)
  {
    _DBGWWorkerJobSharedPtr pJob(new _DBGWWorkerJobSetAutoCommit(m_pClientProxy,
        bAutoCommit));

    _DBGWWorkerJobResultSharedPtr pJobResult;
    if (ulWaitTimeMilSec > system::INFINITE_TIMEOUT)
      {
        pJobResult = delegateJob(pJob, ulWaitTimeMilSec);
      }
    else
      {
        pJobResult = pJob->execute();
      }

    DBGWException e = pJobResult->getException();
    if (e.getErrorCode() != DBGW_ER_NO_ERROR)
      {
        throw e;
      }
  }

  void _DBGWWorker::commit(unsigned long ulWaitTimeMilSec)
  {
    _DBGWWorkerJobSharedPtr pJob(new _DBGWWorkerJobCommit(m_pClientProxy));

    _DBGWWorkerJobResultSharedPtr pJobResult;
    if (ulWaitTimeMilSec > system::INFINITE_TIMEOUT)
      {
        pJobResult = delegateJob(pJob, ulWaitTimeMilSec);
      }
    else
      {
        pJobResult = pJob->execute();
      }

    DBGWException e = pJobResult->getException();
    if (e.getErrorCode() != DBGW_ER_NO_ERROR)
      {
        throw e;
      }
  }

  void _DBGWWorker::rollback(unsigned long ulWaitTimeMilSec)
  {
    _DBGWWorkerJobSharedPtr pJob(new _DBGWWorkerJobRollback(m_pClientProxy));

    _DBGWWorkerJobResultSharedPtr pJobResult;
    if (ulWaitTimeMilSec > system::INFINITE_TIMEOUT)
      {
        pJobResult = delegateJob(pJob, ulWaitTimeMilSec);
      }
    else
      {
        pJobResult = pJob->execute();
      }

    DBGWException e = pJobResult->getException();
    if (e.getErrorCode() != DBGW_ER_NO_ERROR)
      {
        throw e;
      }
  }

  DBGWClientResultSetSharedPtr _DBGWWorker::execute(const char *szSqlName,
      const _DBGWParameter *pParameter, unsigned long ulWaitTimeMilSec)
  {
    _DBGWWorkerJobSharedPtr pJob(new _DBGWWorkerJobExecute(m_pClientProxy,
        szSqlName, pParameter));

    _DBGWWorkerJobResultSharedPtr pJobResult;
    if (ulWaitTimeMilSec > system::INFINITE_TIMEOUT)
      {
        pJobResult = delegateJob(pJob, ulWaitTimeMilSec);
      }
    else
      {
        pJobResult = pJob->execute();
      }

    DBGWClientResultSetSharedPtr pClientResult = pJobResult->getResult();
    if (pClientResult == NULL)
      {
        throw pJobResult->getException();
      }
    else
      {
        return pClientResult;
      }
  }

  DBGWClientBatchResultSetSharedPtr _DBGWWorker::executeBatch(const char *szSqlName,
      const _DBGWParameterList &parameterList, unsigned long ulWaitTimeMilSec)
  {
    _DBGWWorkerJobSharedPtr pJob(new _DBGWWorkerJobExecuteBatch(m_pClientProxy,
        szSqlName, parameterList));

    _DBGWWorkerJobResultSharedPtr pJobResult;
    if (ulWaitTimeMilSec > system::INFINITE_TIMEOUT)
      {
        pJobResult = delegateJob(pJob, ulWaitTimeMilSec);
      }
    else
      {
        pJobResult = pJob->execute();
      }

    DBGWClientBatchResultSetSharedPtr pClientResult = pJobResult->getBatchResult();
    if (pClientResult == NULL)
      {
        throw pJobResult->getException();
      }
    else
      {
        return pClientResult;
      }
  }

  bool _DBGWWorker::isBusy() const
  {
    system::_MutexAutoLock lock(m_pMutex);
    return m_pJob != NULL;
  }

  _DBGWWorkerJobResultSharedPtr _DBGWWorker::delegateJob(
      _DBGWWorkerJobSharedPtr pJob, unsigned long lWaitTimeMilSec)
  {
    system::_MutexAutoLock lock(m_pMutex);
    m_pJob = pJob;
    m_pCond->notify();

    if (lWaitTimeMilSec > system::INFINITE_TIMEOUT)
      {
        try
          {
            m_pCond->timedWait(m_pMutex, lWaitTimeMilSec);
          }
        catch (CondVarOperationFailException &)
          {
            m_pJob.reset();
            m_pJobResult.reset();
            m_pThread->detach();

            /**
             * In order to detach executor thread,
             * we have to change timed wait exception to timeout exception.
             */
            ExecuteTimeoutExecption e(lWaitTimeMilSec);
            DBGW_LOG_DEBUG(e.what());
            throw e;
          }
      }
    else
      {
        m_pCond->wait(m_pMutex);
      }

    _DBGWWorkerJobResultSharedPtr pJobResult = m_pJobResult;
    m_pJobResult.reset();
    return pJobResult;
  }

  _DBGWWorkerJobSharedPtr _DBGWWorker::waitAndGetJob()
  {
    system::_MutexAutoLock lock(m_pMutex);

    bool bRunning = false;
    while ((bRunning = m_pThread->isRunning()) && m_pJob == NULL)
      {
        try
          {
            m_pCond->timedWait(m_pMutex, 100);
          }
        catch (CondVarOperationFailException &)
          {
            /**
             * ignore timeout exception
             */
          }
      }

    if (bRunning)
      {
        return m_pJob;
      }
    else
      {
        return _DBGWWorkerJobSharedPtr();
      }
  }

  void _DBGWWorker::notifyEndOfJob(_DBGWWorkerJobResultSharedPtr pJobResult)
  {
    system::_MutexAutoLock lock(m_pMutex);
    m_pJobResult.reset();
    m_pJob.reset();
    if (m_pThread->isRunning())
      {
        m_pJobResult = pJobResult;
      }
    m_pCond->notify();
  }

  void _DBGWWorker::run(const system::_Thread *pThread,
      system::_ThreadDataSharedPtr pData)
  {
    if (pThread == NULL || pData == NULL)
      {
        FailedToCreateThreadException e("worker");
        DBGW_LOG_ERROR(e.what());
        return;
      }

    _DBGWWorker *pWorker = (_DBGWWorker *) pData.get();
    _DBGWWorkerJobSharedPtr pJob;
    _DBGWWorkerJobResultSharedPtr pJobResult;

    while ((pJob = pWorker->waitAndGetJob()) != NULL)
      {
        try
          {
            pJobResult = pJob->execute();
          }
        catch (DBGWException &e)
          {
            pJobResult = _DBGWWorkerJobResultSharedPtr(new _DBGWWorkerJobResult(e));
          }

        pWorker->notifyEndOfJob(pJobResult);
        pJobResult.reset();
        pJob.reset();
      }
  }

  _DBGWWorkerPool::_DBGWWorkerPool(DBGWConfiguration *pConfiguration) :
    m_pConfiguration(pConfiguration), m_pMutex(system::_MutexFactory::create())
  {
  }

  _DBGWWorkerPool::~_DBGWWorkerPool()
  {
    system::_MutexAutoLock lock(m_pMutex);

    unsigned long ulMaxWaitExitTimeMilSec =
        DBGWConfiguration::DEFAULT_MAX_WAIT_EXIT_TIME_MILSEC();

    if (m_pConfiguration != NULL)
      {
        ulMaxWaitExitTimeMilSec = m_pConfiguration->getMaxWaitExitTimeMilSec();
      }

    _DBGWWorkerList::iterator it = m_workerList.begin();
    for (; it != m_workerList.end(); it++)
      {
        try
          {
            (*it)->stop(ulMaxWaitExitTimeMilSec);
          }
        catch (CondVarOperationFailException &)
          {
            /**
             * ignore timeout exception
             */
          }
      }

    m_workerList.clear();
  }

  _DBGWWorkerSharedPtr _DBGWWorkerPool::getWorker()
  {
    _DBGWWorkerSharedPtr pWorker;
    do
      {
        m_pMutex->lock();
        if (m_workerList.empty())
          {
            m_pMutex->unlock();
            break;
          }

        pWorker = m_workerList.front();
        m_workerList.pop_front();
        m_pMutex->unlock();
      }
    while (pWorker == NULL);

    if (pWorker == NULL)
      {
        pWorker = _DBGWWorkerSharedPtr(new _DBGWWorker(*this));
        if (pWorker != NULL)
          {
            pWorker->start();
          }
      }

    return pWorker;
  }

  void _DBGWWorkerPool::returnWorker(_DBGWWorkerSharedPtr pWorker)
  {
    if (pWorker == NULL || pWorker->isBusy())
      {
        return;
      }

    system::_MutexAutoLock lock(m_pMutex);
    m_workerList.push_back(pWorker);
  }

}
