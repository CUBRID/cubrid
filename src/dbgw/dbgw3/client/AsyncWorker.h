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

#ifndef ASYNCWORKER_H_
#define ASYNCWORKER_H_

namespace dbgw
{

  enum _AsyncWorkerState
  {
    DBGW_WORKTER_STATE_IDLE = 0,
    DBGW_WORKTER_STATE_BUSY,
    DBGW_WORKTER_STATE_TIMEOUT
  };

  enum _AsyncWorkderStatColumn
  {
    DBGW_WORKER_STAT_COL_PADDING = 0,
    DBGW_WORKER_STAT_COL_ID,
    DBGW_WORKER_STAT_COL_STATE,
    DBGW_WORKER_STAT_COL_JOB_NAME,
    DBGW_WORKER_STAT_COL_JOB_SQL_NAME,
    DBGW_WORKER_STAT_COL_JOB_START_TIME,
    DBGW_WORKER_STAT_COL_JOB_TIMEOUT
  };

  class Configuration;
  class _StatisticsMonitor;
  class _AsyncWaiter;
  class _AsyncWorker;
  class _AsyncWorkerJob;
  class _AsyncWorkerPool;

  class _AsyncWorker : public system::_ThreadEx
  {
  public:
    _AsyncWorker(_AsyncWorkerPool &workerPool, Configuration *pConfiguration,
        int nWorkerId);
    virtual ~_AsyncWorker();

    void delegateJob(trait<_AsyncWorkerJob>::sp pJob,
        unsigned long ulWaitTimeMilSec, unsigned long ulAbsWaitTimeMilSec);
    int delegateJobAsync(trait<_AsyncWorkerJob>::sp pJob,
        ExecAsyncCallBack pCallBack,
        unsigned long ulWaitTimeMilSec = system::INFINITE_TIMEOUT,
        unsigned long ulAbsWaitTimeMilSec = system::INFINITE_TIMEOUT);
    int delegateJobAsync(trait<_AsyncWorkerJob>::sp pJob,
        ExecBatchAsyncCallBack pCallBack,
        unsigned long ulWaitTimeMilSec = system::INFINITE_TIMEOUT,
        unsigned long ulAbsWaitTimeMilSec = system::INFINITE_TIMEOUT);
    void release(bool bIsForceDrop = false);
    void changeWorkerState(_AsyncWorkerState state,
        trait<_AsyncWorkerJob>::sp pJob = trait<_AsyncWorkerJob>::sp());

  public:
    _AsyncWorkerState getState() const;
    _AsyncWorkerState getStateWithoutLock() const;

  private:
    class Impl;
    Impl *m_pImpl;
  };

  class _AsyncWorkerPool
  {
  public:
    _AsyncWorkerPool(Configuration *pConfiguration);
    virtual ~_AsyncWorkerPool();

    trait<_AsyncWorker>::sp getAsyncWorker();
    void returnWorker(trait<_AsyncWorker>::sp pWorker, bool bIsForceDrop);
    void clear();

  private:
    class Impl;
    Impl *m_pImpl;
  };

}

#endif
