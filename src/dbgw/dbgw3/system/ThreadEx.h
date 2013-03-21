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

#ifndef THREADEX_H_
#define THREADEX_H_

namespace dbgw
{

  namespace system
  {

    class _ThreadEx;
    typedef void (*_ThreadFunctionEx)(const _ThreadEx *);

    enum _ThreadStatus
    {
      THREAD_STATUS_INIT = 0,
      THREAD_STATUS_STOP,
      THREAD_STATUS_DETACH,
      THREAD_STATUS_RUNNING
    };

    enum _ThreadOperation
    {
      THREAD_OP_NONE,
      THREAD_OP_START,
      THREAD_OP_STOP,
      THREAD_OP_DETACH
    };

    class _ThreadEx : public boost::enable_shared_from_this<_ThreadEx>
    {
    public:
      static unsigned long MIN_SLEEP_TIME_MILSEC();

    public:
      _ThreadEx(_ThreadFunctionEx pFunc);
      virtual ~_ThreadEx();

      void start();
      void join();
      void timedJoin(unsigned long nWaitTimeMilSec);
      void detach();

    public:
      _ThreadStatus getStatus();
      bool isRunning() const;
      bool sleep(unsigned long ulSleepMilSec) const;

    public:
      /* dont't call this method directly */
      void execute();

    private:
      class Impl;
      Impl *m_pImpl;
    };

  }

}

#endif
