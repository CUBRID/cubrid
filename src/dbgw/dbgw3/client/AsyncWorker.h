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
    DBGW_WORKER_STATE_IDLE = 0,
    DBGW_WORKER_STATE_READY,
    DBGW_WORKER_STATE_BUSY,
    DBGW_WORKER_STATE_TIMEOUT
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
    _AsyncWorker(Configuration *pConfiguration,
        int nWorkerId);
    virtual ~_AsyncWorker();

    void delegateJob(trait<_AsyncWorkerJob>::sp pJob);
    void cancelJob();
    void release(bool bIsForceDrop = false);
    void changeWorkerState(_AsyncWorkerState state,
        trait<_AsyncWorkerJob>::sp pJob = trait<_AsyncWorkerJob>::sp());

  public:
    _AsyncWorkerState getState() const;
    _AsyncWorkerState getStateWithoutLock() const;

  private:
    _AsyncWorker(const _AsyncWorker &);
    _AsyncWorker &operator=(const _AsyncWorker &);

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
    void clear();

  private:
    _AsyncWorkerPool(const _AsyncWorkerPool &);
    _AsyncWorkerPool &operator=(const _AsyncWorkerPool &);

  private:
    class Impl;
    Impl *m_pImpl;
  };

}

#endif
