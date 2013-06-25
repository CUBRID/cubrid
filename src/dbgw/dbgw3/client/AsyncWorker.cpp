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
 *  Foundation, Inc., 51 Franklin Street, Fifth , Boston, MA 02110-1301 USA
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
#include "dbgw3/system/Thread.h"
#include "dbgw3/system/ThreadEx.h"
#include "dbgw3/system/Time.h"
#include "dbgw3/client/Resource.h"
#include "dbgw3/client/StatisticsMonitor.h"
#include "dbgw3/client/Client.h"
#include "dbgw3/client/AsyncWorkerJob.h"
#include "dbgw3/client/AsyncWorker.h"
#include "dbgw3/client/AsyncWaiter.h"
#include "dbgw3/client/Configuration.h"

namespace dbgw
{

  /**
   * ASYNC CALL HANDLE ID = WORKER ID + REQ ID = INT_MAX
   * WORKER ID = (0 ~ 20000) * 100000
   * REQ ID = (0 ~ 99999) * 1
   */
  static const int WORKER_ID_MAX = 20000;

  class _AsyncWorker::Impl
  {
  public:
    Impl(_AsyncWorker *pSelf, _AsyncWorkerPool &workerPool,
        trait<_StatisticsMonitor>::sp pMonitor, int nWorkerId) :
      m_pSelf(pSelf), m_workerPool(workerPool), m_pMonitor(pMonitor),
      m_state(DBGW_WORKTER_STATE_IDLE), m_nWorkerId(nWorkerId), m_nReqId(-1),
      m_statItem("WS")
    {
      m_statItem.addColumn(
          new _StatisticsItemColumn(m_pMonitor,
              DBGW_STAT_COL_TYPE_STATIC, DBGW_STAT_VAL_TYPE_STRING, " ", 1));
      m_statItem.addColumn(
          new _StatisticsItemColumn(m_pMonitor,
              DBGW_STAT_COL_TYPE_STATIC, DBGW_STAT_VAL_TYPE_LONG, "WORKER-ID",
              10));
      m_statItem.addColumn(
          new _StatisticsItemColumn(m_pMonitor,
              DBGW_STAT_COL_TYPE_STATIC, DBGW_STAT_VAL_TYPE_STRING, "STATE",
              10));
      m_statItem.addColumn(
          new _StatisticsItemColumn(m_pMonitor,
              DBGW_STAT_COL_TYPE_STATIC, DBGW_STAT_VAL_TYPE_STRING, "JOB-NAME",
              15));
      m_statItem.addColumn(
          new _StatisticsItemColumn(m_pMonitor,
              DBGW_STAT_COL_TYPE_STATIC, DBGW_STAT_VAL_TYPE_STRING,
              "JOB-SQL-NAME", 20));
      m_statItem.addColumn(
          new _StatisticsItemColumn(m_pMonitor,
              DBGW_STAT_COL_TYPE_STATIC, DBGW_STAT_VAL_TYPE_STRING,
              "JOB-START-TIME", 27));
      m_statItem.addColumn(
          new _StatisticsItemColumn(m_pMonitor,
              DBGW_STAT_COL_TYPE_STATIC, DBGW_STAT_VAL_TYPE_STRING,
              "JOB-TIMEOUT", 27));

      m_statItem[DBGW_WORKER_STAT_COL_PADDING] = "*";
      m_statItem[DBGW_WORKER_STAT_COL_ID] = (int64) m_nWorkerId;
      m_statItem[DBGW_WORKER_STAT_COL_STATE] = "IDLE";
      m_statItem[DBGW_WORKER_STAT_COL_JOB_NAME] = "-";
      m_statItem[DBGW_WORKER_STAT_COL_JOB_SQL_NAME] = "-";
      m_statItem[DBGW_WORKER_STAT_COL_JOB_START_TIME] = "-";
      m_statItem[DBGW_WORKER_STAT_COL_JOB_TIMEOUT] = "-";

      m_statItem[DBGW_WORKER_STAT_COL_ID].setRightAlign();

      std::stringstream workerId;
      workerId << m_nWorkerId;
      m_pMonitor->getWorkerStatGroup()->addItem(workerId.str(),
          &m_statItem);
    }

    ~Impl()
    {
      std::stringstream workerId;
      workerId << m_nWorkerId;
      m_pMonitor->getWorkerStatGroup()->removeItem(workerId.str());
    }

    void delegateJob(trait<_AsyncWorkerJob>::sp pJob)
    {
      system::_MutexAutoLock lock(&m_mutex);

      pJob->bindWorker(
          boost::dynamic_pointer_cast<_AsyncWorker>(m_pSelf->shared_from_this()));

      changeWorkerStateWithOutLock(DBGW_WORKTER_STATE_BUSY, pJob);

      m_pJob = pJob;
      m_cond.notify();
    }

    void release(bool bIsForceDrop)
    {
      system::_MutexAutoLock lock(&m_mutex);

      m_workerPool.returnWorker(
          boost::dynamic_pointer_cast<_AsyncWorker>(m_pSelf->shared_from_this()),
          bIsForceDrop);
    }

    static void run(const system::_ThreadEx *pThread)
    {
      if (pThread == NULL)
        {
          FailedToCreateThreadException e("async worker");
          DBGW_LOG_ERROR(e.what());
          return;
        }

      _AsyncWorker::Impl *pWorkerImpl = ((_AsyncWorker *) pThread)->m_pImpl;
      trait<_AsyncWorkerJob>::sp pJob;
      while ((pJob = pWorkerImpl->waitAndGetJob()) != NULL)
        {
          pJob->execute();

          pJob.reset();
        }
    }

    _AsyncWorkerState getState()
    {
      system::_MutexAutoLock lock(&m_mutex);

      return m_state;
    }

    _AsyncWorkerState getStateWithoutLock()
    {
      return m_state;
    }

    void changeWorkerState(_AsyncWorkerState state,
        trait<_AsyncWorkerJob>::sp pJob = trait<_AsyncWorkerJob>::sp())
    {
      system::_MutexAutoLock lock(&m_mutex);

      changeWorkerStateWithOutLock(state, pJob);
    }

    void changeWorkerStateWithOutLock(_AsyncWorkerState state,
        trait<_AsyncWorkerJob>::sp pJob = trait<_AsyncWorkerJob>::sp())
    {
      m_state = state;

      if (m_state == DBGW_WORKTER_STATE_IDLE)
        {
          m_statItem[DBGW_WORKER_STAT_COL_STATE] = "IDLE";
          m_statItem[DBGW_WORKER_STAT_COL_JOB_NAME] = "-";
          m_statItem[DBGW_WORKER_STAT_COL_JOB_SQL_NAME] = "-";
          m_statItem[DBGW_WORKER_STAT_COL_JOB_START_TIME] = "-";
          m_statItem[DBGW_WORKER_STAT_COL_JOB_TIMEOUT] = "-";
        }
      else if (m_state == DBGW_WORKTER_STATE_BUSY)
        {
          m_statItem[DBGW_WORKER_STAT_COL_STATE] = "BUSY";
          m_statItem[DBGW_WORKER_STAT_COL_JOB_NAME] = pJob->getJobName();
          m_statItem[DBGW_WORKER_STAT_COL_JOB_SQL_NAME] = pJob->getSqlName();
          m_statItem[DBGW_WORKER_STAT_COL_JOB_START_TIME] =
              system::getTimeStrFromMilSec(system::getCurrTimeMilSec()).c_str();

          uint64_t ulTimeOutMilSec = pJob->getAbsTimeOutMilSec();
          if (ulTimeOutMilSec == 0)
            {
              m_statItem[DBGW_WORKER_STAT_COL_JOB_TIMEOUT] = "-";
            }
          else
            {
              m_statItem[DBGW_WORKER_STAT_COL_JOB_TIMEOUT] =
                  system::getTimeStrFromMilSec(ulTimeOutMilSec).c_str();
            }
        }
      else if (m_state == DBGW_WORKTER_STATE_TIMEOUT)
        {
          m_statItem[DBGW_WORKER_STAT_COL_STATE] = "TIMEOUT";
          m_statItem[DBGW_WORKER_STAT_COL_JOB_NAME] = pJob->getJobName();
          m_statItem[DBGW_WORKER_STAT_COL_JOB_SQL_NAME] = pJob->getSqlName();
          m_statItem[DBGW_WORKER_STAT_COL_JOB_START_TIME] =
              system::getTimeStrFromMilSec(system::getCurrTimeMilSec()).c_str();

          uint64_t ulTimeOutMilSec = pJob->getAbsTimeOutMilSec();
          if (ulTimeOutMilSec == 0)
            {
              m_statItem[DBGW_WORKER_STAT_COL_JOB_TIMEOUT] = "-";
            }
          else
            {
              m_statItem[DBGW_WORKER_STAT_COL_JOB_TIMEOUT] =
                  system::getTimeStrFromMilSec(ulTimeOutMilSec).c_str();
            }

          m_statItem.removeAfterWriteItem();
        }
    }

  private:
    trait<_AsyncWorkerJob>::sp waitAndGetJob()
    {
      system::_MutexAutoLock lock(&m_mutex);

      bool bIsRunning = false;
      while ((bIsRunning = m_pSelf->isRunning()) && m_pJob == NULL)
        {
          try
            {
              m_cond.timedWait(&m_mutex, 100);
            }
          catch (CondVarOperationFailException &)
            {
              /**
               * ignore timeout exception
               */
            }
        }

      lock.unlock();

      if (bIsRunning)
        {
          trait<_AsyncWorkerJob>::sp pJob = m_pJob;
          m_pJob.reset();
          changeWorkerStateWithOutLock(DBGW_WORKTER_STATE_BUSY, pJob);
          return pJob;
        }
      else
        {
          return trait<_AsyncWorkerJob>::sp();
        }
    }

  private:
    _AsyncWorker *m_pSelf;
    _AsyncWorkerPool &m_workerPool;
    trait<_StatisticsMonitor>::sp m_pMonitor;
    system::_Mutex m_mutex;
    system::_ConditionVariable m_cond;
    trait<_AsyncWorkerJob>::sp m_pJob;
    _AsyncWorkerState m_state;
    int m_nWorkerId;
    int m_nReqId;

    _StatisticsItem m_statItem;
  };

  _AsyncWorker::_AsyncWorker(_AsyncWorkerPool &workerPool,
      Configuration *pConfiguration, int nWorkerId) :
    system::_ThreadEx(Impl::run),
    m_pImpl(new Impl(this, workerPool, pConfiguration->getMonitor(), nWorkerId))
  {
  }

  _AsyncWorker::~_AsyncWorker()
  {
    if (m_pImpl != NULL)
      {
        delete m_pImpl;
      }
  }

  void _AsyncWorker::delegateJob(trait<_AsyncWorkerJob>::sp pJob)
  {
    m_pImpl->delegateJob(pJob);
  }

  void _AsyncWorker::release(bool bIsForceDrop)
  {
    m_pImpl->release(bIsForceDrop);
  }

  void _AsyncWorker::changeWorkerState(_AsyncWorkerState state,
      trait<_AsyncWorkerJob>::sp pJob)
  {
    m_pImpl->changeWorkerState(state, pJob);
  }

  _AsyncWorkerState _AsyncWorker::getState() const
  {
    return m_pImpl->getState();
  }

  _AsyncWorkerState _AsyncWorker::getStateWithoutLock() const
  {
    return m_pImpl->getStateWithoutLock();
  }

  class _AsyncWorkerPool::Impl
  {
  public:
    Impl(_AsyncWorkerPool *pSelf, Configuration *pConfiguration) :
      m_pSelf(pSelf), m_pConfiguration(pConfiguration), m_nWorkerId(-1)
    {
    }

    ~Impl()
    {
      clear();
    }

    trait<_AsyncWorker>::sp getAsyncWorker()
    {
      trait<_AsyncWorker>::sp pWorker;
      do
        {
          m_mutex.lock();

          rearrangeWorkerList();

          if (m_idleWorkerList.empty())
            {
              m_mutex.unlock();
              break;
            }

          pWorker = m_idleWorkerList.front();
          m_idleWorkerList.pop_front();
          m_mutex.unlock();
        }
      while (pWorker == NULL);

      if (pWorker == NULL)
        {
          m_mutex.lock();
          if (++m_nWorkerId > WORKER_ID_MAX)
            {
              m_nWorkerId = 0;
            }
          m_mutex.unlock();

          pWorker = trait<_AsyncWorker>::sp(
              new _AsyncWorker(*m_pSelf, m_pConfiguration, m_nWorkerId));
          if (pWorker != NULL)
            {
              pWorker->start();
            }
        }

      return pWorker;
    }

    void returnWorker(trait<_AsyncWorker>::sp pWorker, bool bIsForceDrop)
    {
      if (pWorker == NULL)
        {
          return;
        }

      if (bIsForceDrop)
        {
          pWorker->detach();
        }
      else
        {
          system::_MutexAutoLock lock(&m_mutex);

          if (pWorker->getStateWithoutLock() == DBGW_WORKTER_STATE_BUSY)
            {
              m_busyWorkerList.push_back(pWorker);
            }
          else
            {
              m_idleWorkerList.push_back(pWorker);
            }
        }
    }

    void clear()
    {
      system::_MutexAutoLock lock(&m_mutex);

      unsigned long ulMaxWaitExitTimeMilSec =
          m_pConfiguration->getMaxWaitExitTimeMilSec();

      trait<_AsyncWorker>::splist::iterator it = m_idleWorkerList.begin();
      for (; it != m_idleWorkerList.end(); it++)
        {
          try
            {
              (*it)->timedJoin(ulMaxWaitExitTimeMilSec);
            }
          catch (CondVarOperationFailException &)
            {
              /**
               * ignore timeout exception
               */
            }
        }

      m_idleWorkerList.clear();

      it = m_busyWorkerList.begin();
      for (; it != m_busyWorkerList.end(); it++)
        {
          try
            {
              (*it)->timedJoin(ulMaxWaitExitTimeMilSec);
            }
          catch (CondVarOperationFailException &)
            {
              /**
               * ignore timeout exception
               */
            }
        }

      m_busyWorkerList.clear();
    }

    void rearrangeWorkerList()
    {
      if (m_idleWorkerList.empty() == false || m_busyWorkerList.empty())
        {
          return;
        }

      trait<_AsyncWorker>::splist::iterator it = m_busyWorkerList.begin();
      while (it != m_busyWorkerList.end())
        {
          if ((*it)->getState() == DBGW_WORKTER_STATE_IDLE)
            {
              m_idleWorkerList.push_back(*it);
              m_busyWorkerList.erase(it++);
            }
          else
            {
              ++it;
            }
        }
    }

  private:
    _AsyncWorkerPool *m_pSelf;
    Configuration *m_pConfiguration;
    trait<_AsyncWorker>::splist m_idleWorkerList;
    trait<_AsyncWorker>::splist m_busyWorkerList;
    system::_Mutex m_mutex;
    int m_nWorkerId;
  };

  _AsyncWorkerPool::_AsyncWorkerPool(Configuration *pConfiguration) :
    m_pImpl(new Impl(this, pConfiguration))
  {
  }

  _AsyncWorkerPool::~_AsyncWorkerPool()
  {
    if (m_pImpl != NULL)
      {
        delete m_pImpl;
      }
  }

  trait<_AsyncWorker>::sp _AsyncWorkerPool::getAsyncWorker()
  {
    return m_pImpl->getAsyncWorker();
  }

  void _AsyncWorkerPool::returnWorker(trait<_AsyncWorker>::sp pWorker,
      bool bIsForceDrop)
  {
    m_pImpl->returnWorker(pWorker, bIsForceDrop);
  }

  void _AsyncWorkerPool::clear()
  {
    m_pImpl->clear();
  }

}
