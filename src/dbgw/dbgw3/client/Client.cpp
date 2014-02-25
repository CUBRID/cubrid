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
#include "dbgw3/Exception.h"
#include "dbgw3/Logger.h"
#include "dbgw3/Value.h"
#include "dbgw3/ValueSet.h"
#include "dbgw3/SynchronizedResource.h"
#include "dbgw3/system/DBGWPorting.h"
#include "dbgw3/system/ConditionVariable.h"
#include "dbgw3/system/Mutex.h"
#include "dbgw3/system/ThreadEx.h"
#include "dbgw3/system/Time.h"
#include "dbgw3/sql/DatabaseInterface.h"
#include "dbgw3/client/Interface.h"
#include "dbgw3/client/ConfigurationObject.h"
#include "dbgw3/client/Configuration.h"
#include "dbgw3/client/ClientResultSet.h"
#include "dbgw3/client/Client.h"
#include "dbgw3/client/AsyncWorker.h"
#include "dbgw3/client/AsyncWorkerJob.h"
#include "dbgw3/client/ExecutorHandler.h"
#include "dbgw3/client/Timer.h"
#include "dbgw3/client/Executor.h"
#include "dbgw3/client/Service.h"

namespace dbgw
{

  class Client::Impl
  {
  public:
    Impl(Client *pSelf, Configuration &configuration, const char *szNameSpace) :
      m_pSelf(pSelf), m_configuration(configuration),
      m_bIsClosed(false), m_bValidClient(false), m_bIsAutoCommit(true),
      m_ulWaitTimeMilSec(configuration.getWaitTimeMilSec())
    {
      clearException();

      try
        {
          m_stVersion = m_configuration.getVersion();
          m_pService = m_configuration.getService(m_stVersion, szNameSpace);
          if (m_pService == NULL)
            {
              throw getLastException();
            }

          m_pQueryMapper = m_configuration.getQueryMapper(m_stVersion);
          if (m_pQueryMapper == NULL)
            {
              throw getLastException();
            }

          m_pTimer = m_configuration.getTimer();

          m_pAsyncJobManager = m_configuration.getAsyncWorkerJobManager();
          m_pTimeoutJobManager = m_configuration.getTimeoutWorkerJobManager();

          long lServiceWaitTimeMilSec = m_pService->getWaitTimeMilSec();
          if (lServiceWaitTimeMilSec != system::UNDEFINED_TIMEOUT)
            {
              m_ulWaitTimeMilSec = lServiceWaitTimeMilSec;
            }

          m_bValidClient = true;
        }
      catch (Exception &e)
        {
          setLastException(e);
        }
    }

    ~Impl()
    {
      close();
    }

    void setWaitTimeMilSec(unsigned long ulWaitTimeMilSec)
    {
      m_ulWaitTimeMilSec = ulWaitTimeMilSec;
    }

    bool setForceValidateResult()
    {
      clearException();

      try
        {
          m_pService->setForceValidateResult();
          return true;
        }
      catch (Exception &e)
        {
          setLastException(e);
          return false;
        }
    }

    bool setAutocommit(bool bIsAutoCommit)
    {
      return setAutocommit(bIsAutoCommit, m_ulWaitTimeMilSec);
    }

    bool setAutocommit(bool bIsAutoCommit, unsigned long ulWaitTimeMilSec)
    {
      clearException();

      try
        {
          checkClientIsValid();

          ulWaitTimeMilSec = bindExecutorHandler(ulWaitTimeMilSec);

          if (ulWaitTimeMilSec > system::INFINITE_TIMEOUT)
            {
              trait<_AsyncWorkerJob>::sp pJob(
                  new _SetAutoCommitJob(m_pExecHandler, ulWaitTimeMilSec,
                      bIsAutoCommit));

              delegateJob(pJob);
            }
          else
            {
              m_pExecHandler->setAutoCommit(bIsAutoCommit);
            }

          processError(m_pExecHandler->getLastException());

          m_bIsAutoCommit = bIsAutoCommit;
        }
      catch (Exception &e)
        {
          setLastException(e);
          return false;
        }

      return true;
    }

    bool commit()
    {
      return commit(m_ulWaitTimeMilSec);
    }

    bool commit(unsigned long ulWaitTimeMilSec)
    {
      clearException();

      try
        {
          checkClientIsValid();

          ulWaitTimeMilSec = bindExecutorHandler(ulWaitTimeMilSec);

          if (ulWaitTimeMilSec > system::INFINITE_TIMEOUT)
            {
              trait<_AsyncWorkerJob>::sp pJob(new _CommitJob(m_pExecHandler,
                  ulWaitTimeMilSec));

              delegateJob(pJob);
            }
          else
            {
              m_pExecHandler->commit();
            }

          processError(m_pExecHandler->getLastException());
        }
      catch (Exception &e)
        {
          setLastException(e);
          return false;
        }

      return true;
    }

    bool rollback()
    {
      return rollback(m_ulWaitTimeMilSec);
    }

    bool rollback(unsigned long ulWaitTimeMilSec)
    {
      clearException();

      try
        {
          checkClientIsValid();

          ulWaitTimeMilSec = bindExecutorHandler(ulWaitTimeMilSec);

          if (ulWaitTimeMilSec > system::INFINITE_TIMEOUT)
            {
              trait<_AsyncWorkerJob>::sp pJob(new _RollbackJob(m_pExecHandler,
                  ulWaitTimeMilSec));

              delegateJob(pJob);
            }
          else
            {
              m_pExecHandler->rollback();
            }

          processError(m_pExecHandler->getLastException());
        }
      catch (Exception &e)
        {
          setLastException(e);
          return false;
        }

      return true;
    }

    bool setContainerKey(const char *szKey)
    {
      return setContainerKey(szKey, m_ulWaitTimeMilSec);
    }

    bool setContainerKey(const char *szKey, unsigned long ulWaitTimeMilSec)
    {
      clearException();

      try
        {
          checkClientIsValid();

          bindExecutorHandler(ulWaitTimeMilSec);

          m_pExecHandler->setContainerKey(szKey);

          processError(m_pExecHandler->getLastException());
        }
      catch (Exception &e)
        {
          setLastException(e);
          return false;
        }

      return true;
    }

    trait<ClientResultSet>::sp exec(const char *szSqlName,
        unsigned long ulWaitTimeMilSec)
    {
      return exec(szSqlName, NULL, ulWaitTimeMilSec);
    }

    trait<ClientResultSet>::sp exec(const char *szSqlName,
        const _Parameter *pParameter)
    {
      return exec(szSqlName, pParameter, m_ulWaitTimeMilSec);
    }

    trait<ClientResultSet>::sp exec(const char *szSqlName,
        const _Parameter *pParameter, unsigned long ulWaitTimeMilSec)
    {
      clearException();

      try
        {
          checkClientIsValid();

          ulWaitTimeMilSec = bindExecutorHandler(ulWaitTimeMilSec);

          if (ulWaitTimeMilSec > system::INFINITE_TIMEOUT)
            {
              trait<_AsyncWorkerJob>::sp pJob(
                  new _ExecuteQueryJob(m_pExecHandler, ulWaitTimeMilSec,
                      szSqlName, pParameter));

              delegateJob(pJob);
            }
          else
            {
              m_pExecHandler->execute(szSqlName, pParameter);
            }

          processError(m_pExecHandler->getLastException());
        }
      catch (Exception &e)
        {
          setLastException(e);
          return trait<ClientResultSet>::sp();
        }

      return m_pExecHandler->getResultSet();
    }

    int execAsync(const char *szSqlName, ExecAsyncCallBack pCallBack,
        void *pData)
    {
      return execAsync(szSqlName, NULL, pCallBack, m_ulWaitTimeMilSec, pData);
    }

    int execAsync(const char *szSqlName, ExecAsyncCallBack pCallBack,
        unsigned long ulWaitTimeMilSec, void *pData)
    {
      return execAsync(szSqlName, NULL, pCallBack, ulWaitTimeMilSec, pData);
    }

    int execAsync(const char *szSqlName, const _Parameter *pParameter,
        ExecAsyncCallBack pCallBack, void *pData)
    {
      return execAsync(szSqlName, pParameter, pCallBack, m_ulWaitTimeMilSec,
          pData);
    }

    int execAsync(const char *szSqlName, const _Parameter *pParameter,
        ExecAsyncCallBack pCallBack, unsigned long ulWaitTimeMilSec, void *pData)
    {
      clearException();

      try
        {
          checkClientIsValid();

          trait<_AsyncWorkerJob>::sp pJob(
              new _ExecuteQueryJob(m_pService, m_pQueryMapper, ulWaitTimeMilSec,
                  szSqlName, pParameter));

          return delegateJobAsync(pJob, pCallBack, pData);
        }
      catch (Exception &e)
        {
          setLastException(e);
          return -1;
        }
    }

    trait<ClientResultSet>::spvector execBatch(const char *szSqlName,
        const _ParameterList &parameterList)
    {
      return execBatch(szSqlName, parameterList, m_ulWaitTimeMilSec);
    }

    trait<ClientResultSet>::spvector execBatch(const char *szSqlName,
        const _ParameterList &parameterList, unsigned long ulWaitTimeMilSec)
    {
      clearException();

      try
        {
          checkClientIsValid();

          ulWaitTimeMilSec = bindExecutorHandler(ulWaitTimeMilSec);

          if (ulWaitTimeMilSec > system::INFINITE_TIMEOUT)
            {
              trait<_AsyncWorkerJob>::sp pJob(
                  new _ExecuteQueryBatchJob(m_pExecHandler, ulWaitTimeMilSec,
                      szSqlName, parameterList));

              delegateJob(pJob);
            }
          else
            {
              m_pExecHandler->executeBatch(szSqlName, parameterList);
            }

          processError(m_pExecHandler->getLastException());
        }
      catch (Exception &e)
        {
          setLastException(e);
          if (m_pExecHandler == NULL)
            {
              return trait<ClientResultSet>::spvector();
            }
        }

      return m_pExecHandler->getResultSetList();
    }

    int execBatchAsync(const char *szSqlName,
        const _ParameterList &parameterList,
        ExecBatchAsyncCallBack pCallBack, void *pData)
    {
      return execBatchAsync(szSqlName, parameterList, pCallBack,
          m_ulWaitTimeMilSec, pData);
    }

    int execBatchAsync(const char *szSqlName,
        const _ParameterList &parameterList,
        ExecBatchAsyncCallBack pCallBack, unsigned long ulWaitTimeMilSec,
        void *pData)
    {
      clearException();

      try
        {
          checkClientIsValid();

          trait<_AsyncWorkerJob>::sp pJob(
              new _ExecuteQueryBatchJob(m_pService, m_pQueryMapper, ulWaitTimeMilSec,
                  szSqlName, parameterList));

          return delegateJobAsync(pJob, pCallBack, pData);
        }
      catch (Exception &e)
        {
          setLastException(e);
          return -1;
        }
    }

    bool close()
    {
      clearException();

      Exception exception;
      try
        {
          if (m_bIsClosed)
            {
              return true;
            }

          m_bIsClosed = true;

          releaseExecutorHandler(false);

          m_pService.reset();
          m_pTimer.reset();
          m_pAsyncJobManager.reset();
          m_pTimeoutJobManager.reset();
          m_pExecHandler.reset();
        }
      catch (Exception &e)
        {
          exception = e;
        }

      if (m_configuration.closeVersion(m_stVersion) == false)
        {
          exception = getLastException();
        }

      if (exception.getErrorCode() != DBGW_ER_NO_ERROR)
        {
          setLastException(exception);
          return false;
        }

      return true;
    }

    trait<Lob>::sp createClob()
    {
      return createClob(m_ulWaitTimeMilSec);
    }

    trait<Lob>::sp createClob(unsigned long ulWaitTimeMilSec)
    {
      clearException();

      trait<Lob>::sp pLob;
      try
        {
          checkClientIsValid();

          ulWaitTimeMilSec = bindExecutorHandler(ulWaitTimeMilSec);

          if (ulWaitTimeMilSec > system::INFINITE_TIMEOUT)
            {
              trait<_AsyncWorkerJob>::sp pJob(
                  new _CreateClobJob(m_pExecHandler, ulWaitTimeMilSec));

              trait<_AsyncWorkerJobResult>::sp pJobResult = delegateJob(pJob);

              pLob = pJobResult->getLob();
            }
          else
            {
              pLob = m_pExecHandler->createClob();
            }

          m_pExecHandler->getLastException();
        }
      catch (Exception &e)
        {
          setLastException(e);
          return trait<Lob>::sp();
        }

      return pLob;
    }

    trait<Lob>::sp createBlob()
    {
      return createBlob(m_ulWaitTimeMilSec);
    }

    trait<Lob>::sp createBlob(unsigned long ulWaitTimeMilSec)
    {
      clearException();

      trait<Lob>::sp pLob;
      try
        {
          checkClientIsValid();

          ulWaitTimeMilSec = bindExecutorHandler(ulWaitTimeMilSec);

          if (ulWaitTimeMilSec > system::INFINITE_TIMEOUT)
            {
              trait<_AsyncWorkerJob>::sp pJob(
                  new _CreateBlobJob(m_pExecHandler, ulWaitTimeMilSec));

              trait<_AsyncWorkerJobResult>::sp pJobResult = delegateJob(pJob);

              pLob = pJobResult->getLob();
            }
          else
            {
              pLob = m_pExecHandler->createBlob();
            }

          m_pExecHandler->getLastException();
        }
      catch (Exception &e)
        {
          setLastException(e);
          return trait<Lob>::sp();
        }

      return pLob;
    }

    bool isClosed() const
    {
      return m_bIsClosed;
    }

    bool isAutocommit() const
    {
      return m_bIsAutoCommit;
    }

    const _QueryMapper *getQueryMapper() const
    {
      return m_pQueryMapper.get();
    }

    unsigned long getWaitTimeMilSec() const
    {
      return m_ulWaitTimeMilSec;
    }

  private:
    void checkClientIsValid()
    {
      if (m_bValidClient == false)
        {
          InvalidClientException e;
          DBGW_LOG_ERROR(e.what());
          throw e;
        }
    }

    unsigned long bindExecutorHandler(unsigned long ulWaitTimeMilSec)
    {
      unsigned long ulRemainWaitTimeMilSec = ulWaitTimeMilSec;
      struct timeval now;
      if (ulRemainWaitTimeMilSec > system::INFINITE_TIMEOUT)
        {
          gettimeofday(&now, NULL);
        }

      if (m_pExecHandler == NULL)
        {
          if (ulRemainWaitTimeMilSec > system::INFINITE_TIMEOUT)
            {
              trait<_AsyncWorkerJob>::sp pJob(
                  new _GetExecutorHandlerJob(m_pService, m_pQueryMapper,
                      ulRemainWaitTimeMilSec));

              trait<_AsyncWorkerJobResult>::sp pJobResult = delegateJob(pJob);

              m_pExecHandler = pJobResult->getExecutorHandler();
              if (m_pExecHandler == NULL)
                {
                  throw pJobResult->getException();
                }
            }
          else
            {
              trait<_ExecutorHandler>::sp pExecHandler(
                  new _ExecutorHandler(m_pService, m_pQueryMapper));
              m_pExecHandler = pExecHandler;
            }

          if (ulRemainWaitTimeMilSec > system::INFINITE_TIMEOUT)
            {
              ulRemainWaitTimeMilSec -= system::getdifftimeofday(now);
              if (ulRemainWaitTimeMilSec <= 0)
                {
                  ExecuteTimeoutExecption e(ulRemainWaitTimeMilSec);
                  DBGW_LOG_ERROR(e.what());
                  throw e;
                }
            }
        }

      return ulRemainWaitTimeMilSec;
    }

  private:
    trait<_AsyncWorkerJobResult>::sp delegateJob(trait<_AsyncWorkerJob>::sp pJob)
    {
      if (pJob->getTimeOutMilSec() > system::INFINITE_TIMEOUT)
        {
          _TimerEvent *pEvent = new _TimerEvent(pJob);
          m_pTimer->addEvent(pEvent);
        }

      try
        {
          return m_pTimeoutJobManager->delegateJob(pJob);
        }
      catch (Exception &e)
        {
          if (e.getErrorCode() == DBGW_ER_CLIENT_EXEC_TIMEOUT)
            {
              releaseExecutorHandler(true);
            }

          throw;
        }
    }

    int delegateJobAsync(trait<_AsyncWorkerJob>::sp pJob,
        ExecAsyncCallBack pCallBack, void *pData)
    {
      if (pJob->getTimeOutMilSec() > system::INFINITE_TIMEOUT)
        {
          _TimerEvent *pEvent = new _TimerEvent(pJob);
          m_pTimer->addEvent(pEvent);
        }

      return m_pAsyncJobManager->delegateJobAsync(pJob, pCallBack, pData);
    }

    int delegateJobAsync(trait<_AsyncWorkerJob>::sp pJob,
        ExecBatchAsyncCallBack pCallBack, void *pData)
    {
      if (pJob->getTimeOutMilSec() > system::INFINITE_TIMEOUT)
        {
          _TimerEvent *pEvent = new _TimerEvent(pJob);
          m_pTimer->addEvent(pEvent);
        }

      return m_pAsyncJobManager->delegateJobAsync(pJob, pCallBack, pData);
    }

    void processError(const Exception &e)
    {
      if (e.getErrorCode() == DBGW_ER_NO_ERROR)
        {
          clearException();
          return;
        }

      setLastException(e);

      if (e.getErrorCode() != DBGW_ER_CLIENT_VALIDATE_FAIL
          && e.getErrorCode() != DBGW_ER_CLIENT_VALIDATE_TYPE_FAIL
          && e.getErrorCode() != DBGW_ER_CLIENT_VALIDATE_VALUE_FAIL)
        {
          throw e;
        }
    }

    void releaseExecutorHandler(bool bNeedForceDrop)
    {
      if (m_pExecHandler != NULL)
        {
          m_pExecHandler->release(bNeedForceDrop);
        }

      m_pExecHandler.reset();
    }

  private:
    Client *m_pSelf;
    Configuration &m_configuration;
    _ConfigurationVersion m_stVersion;
    trait<_Service>::sp m_pService;
    trait<_QueryMapper>::sp m_pQueryMapper;
    trait<_Timer>::sp m_pTimer;
    trait<_AsyncWorkerJobManager>::sp m_pAsyncJobManager;
    trait<_TimeoutWorkerJobManager>::sp m_pTimeoutJobManager;
    trait<_ExecutorHandler>::sp m_pExecHandler;
    bool m_bIsClosed;
    bool m_bValidClient;
    bool m_bIsAutoCommit;
    unsigned long m_ulWaitTimeMilSec;
  };

  Client::Client(Configuration &configuration, const char *szNameSpace) :
    m_pImpl(new Impl(this, configuration, szNameSpace))
  {
    configuration.registerResource(this);
  }

  Client::~ Client()
  {
    closeResource();

    if (m_pImpl != NULL)
      {
        delete m_pImpl;
      }
  }

  void Client::setWaitTimeMilSec(unsigned long ulWaitTimeMilSec)
  {
    m_pImpl->setWaitTimeMilSec(ulWaitTimeMilSec);
  }

  bool Client::setForceValidateResult()
  {
    return m_pImpl->setForceValidateResult();
  }

  bool Client::setAutocommit(bool bIsAutocommit)
  {
    return m_pImpl->setAutocommit(bIsAutocommit);
  }

  bool Client::setAutocommit(bool bIsAutocommit, unsigned long ulWaitTimeMilSec)
  {
    return m_pImpl->setAutocommit(bIsAutocommit, ulWaitTimeMilSec);
  }

  bool Client::commit()
  {
    return m_pImpl->commit();
  }

  bool Client::commit(unsigned long ulWaitTimeMilSec)
  {
    return m_pImpl->commit(ulWaitTimeMilSec);
  }

  bool Client::rollback()
  {
    return m_pImpl->rollback();
  }

  bool Client::rollback(unsigned long ulWaitTimeMilSec)
  {
    return m_pImpl->rollback(ulWaitTimeMilSec);
  }

  bool Client::setContainerKey(const char *szKey)
  {
    return m_pImpl->setContainerKey(szKey);
  }

  trait<ClientResultSet>::sp Client::exec(const char *szSqlName,
      unsigned long ulWaitTimeMilSec)
  {
    return m_pImpl->exec(szSqlName, ulWaitTimeMilSec);
  }

  trait<ClientResultSet>::sp Client::exec(const char *szSqlName,
      const _Parameter *pParameter)
  {
    return m_pImpl->exec(szSqlName, pParameter);
  }

  trait<ClientResultSet>::sp Client::exec(const char *szSqlName,
      const _Parameter *pParameter, unsigned long ulWaitTimeMilSec)
  {
    return m_pImpl->exec(szSqlName, pParameter, ulWaitTimeMilSec);
  }

  int Client::execAsync(const char *szSqlName, ExecAsyncCallBack pCallBack,
      void *pData)
  {
    return m_pImpl->execAsync(szSqlName, pCallBack, pData);
  }

  int Client::execAsync(const char *szSqlName, ExecAsyncCallBack pCallBack,
      unsigned long ulWaitTimeMilSec, void *pData)
  {
    return m_pImpl->execAsync(szSqlName, pCallBack, ulWaitTimeMilSec, pData);
  }

  int Client::execAsync(const char *szSqlName, const _Parameter *pParameter,
      ExecAsyncCallBack pCallBack, void *pData)
  {
    return m_pImpl->execAsync(szSqlName, pParameter, pCallBack, pData);
  }

  int Client::execAsync(const char *szSqlName, const _Parameter *pParameter,
      ExecAsyncCallBack pCallBack, unsigned long ulWaitTimeMilSec,
      void *pData)
  {
    return m_pImpl->execAsync(szSqlName, pParameter, pCallBack,
        ulWaitTimeMilSec, pData);
  }

  trait<ClientResultSet>::spvector Client::execBatch(const char *szSqlName,
      const _ParameterList &parameterList)
  {
    return m_pImpl->execBatch(szSqlName, parameterList);
  }

  trait<ClientResultSet>::spvector Client::execBatch(const char *szSqlName,
      const _ParameterList &parameterList,
      unsigned long ulWaitTimeMilSec)
  {
    return m_pImpl->execBatch(szSqlName, parameterList, ulWaitTimeMilSec);
  }

  int Client::execBatchAsync(const char *szSqlName,
      const _ParameterList &parameterList,
      ExecBatchAsyncCallBack pCallBack, void *pData)
  {
    return m_pImpl->execBatchAsync(szSqlName, parameterList, pCallBack, pData);
  }

  int Client::execBatchAsync(const char *szSqlName,
      const _ParameterList &parameterList,
      ExecBatchAsyncCallBack pCallBack, unsigned long ulWaitTimeMilSec,
      void *pData)
  {
    return m_pImpl->execBatchAsync(szSqlName, parameterList, pCallBack,
        ulWaitTimeMilSec, pData);
  }

  bool Client::close()
  {
    return m_pImpl->close();
  }

  trait<Lob>::sp Client::createClob()
  {
    return m_pImpl->createClob();
  }

  trait<Lob>::sp Client::createClob(unsigned long ulWaitTimeMilSec)
  {
    return m_pImpl->createClob(ulWaitTimeMilSec);
  }

  trait<Lob>::sp Client::createBlob()
  {
    return m_pImpl->createBlob();
  }

  trait<Lob>::sp Client::createBlob(unsigned long ulWaitTimeMilSec)
  {
    return m_pImpl->createBlob(ulWaitTimeMilSec);
  }

  bool Client::isClosed() const
  {
    return m_pImpl->isClosed();
  }

  bool Client::isAutocommit() const
  {
    return m_pImpl->isAutocommit();
  }

  const _QueryMapper *Client::getQueryMapper() const
  {
    return m_pImpl->getQueryMapper();
  }

  unsigned long Client::getWaitTimeMilSec() const
  {
    return m_pImpl->getWaitTimeMilSec();
  }

  void Client::doUnlinkResource()
  {
    m_pImpl->close();
  }

}
