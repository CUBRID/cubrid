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

#include <errno.h>
#include "dbgw3/Common.h"
#include "dbgw3/Exception.h"
#include "dbgw3/Logger.h"
#include "dbgw3/system/DBGWPorting.h"
#include "dbgw3/system/Mutex.h"
#include "dbgw3/system/ConditionVariable.h"
#include "dbgw3/system/Thread.h"
#include "dbgw3/system/ThreadEx.h"

namespace dbgw
{

  namespace system
  {

    THREAD_RET_T THREAD_CALLING_CONVENTION _ThreadExRun(void *pData)
    {
      if (pData == NULL)
        {
          pthread_exit(NULL);
        }

      _ThreadEx *pThreadEx = (_ThreadEx *) pData;

      pThreadEx->execute();

      pthread_exit(NULL);

#if defined(WINDOWS) || defined(_WIN32) || defined(_WIN64)
      return 0;
#endif
    }

    class _ThreadEx::Impl
    {
    public:
      Impl(_ThreadEx *pSelf,_ThreadFunctionEx pFunc) :
        m_pSelf(pSelf), m_status(THREAD_STATUS_INIT), m_bIsRunning(true),
        m_pFunc(pFunc), m_thread(_ThreadExRun, pSelf)
      {
      }

      ~Impl()
      {
      }

      void start()
      {
        _MutexAutoLock lock(&m_mutex);

        if (m_status != THREAD_STATUS_INIT)
          {
            return;
          }

        m_thread.start();

        changeThreadStatus(THREAD_OP_START);
      }

      void join()
      {
        _MutexAutoLock lock(&m_mutex);

        m_bIsRunning = false;

        switch (m_status)
          {
          case THREAD_STATUS_RUNNING:
            m_cond.wait(&m_mutex);
            /* no break */
          case THREAD_STATUS_STOP:
            m_thread.join();
            /* no break */
          case THREAD_STATUS_INIT:
            changeThreadStatus(THREAD_OP_STOP);
            break;
          case THREAD_STATUS_DETACH:
          default:
            break;
          }
      }

      void timedJoin(unsigned long nWaitTimeMilSec)
      {
        _MutexAutoLock lock(&m_mutex);

        m_bIsRunning = false;

        try
          {
            switch (m_status)
              {
              case THREAD_STATUS_RUNNING:
                if (m_cond.timedWait(&m_mutex, nWaitTimeMilSec) != 0)
                  {
                    m_thread.detach();
                    changeThreadStatus(THREAD_OP_DETACH);
                    return;
                  }
                /* no break */
              case THREAD_STATUS_STOP:
                m_thread.join();
                /* no break */
              case THREAD_STATUS_INIT:
                changeThreadStatus(THREAD_OP_STOP);
                break;
              case THREAD_STATUS_DETACH:
              default:
                break;
              }
          }
        catch (Exception &)
          {
            m_thread.detach();
            changeThreadStatus(THREAD_OP_DETACH);
          }
      }

      void detach()
      {
        _MutexAutoLock lock(&m_mutex);

        m_bIsRunning = false;

        switch (m_status)
          {
          case THREAD_STATUS_RUNNING:
            m_thread.detach();
            changeThreadStatus(THREAD_OP_DETACH);
            break;
          case THREAD_STATUS_STOP:
            m_thread.join();
            /* no break */
          case THREAD_STATUS_INIT:
            changeThreadStatus(THREAD_OP_STOP);
            break;
          case THREAD_STATUS_DETACH:
          default:
            break;
          }
      }

      _ThreadStatus getStatus()
      {
        _MutexAutoLock lock(&m_mutex);

        return m_status;
      }

      bool isRunning()
      {
        _MutexAutoLock lock(&m_mutex);

        return m_bIsRunning;
      }

      bool sleep(unsigned long ulSleepMilSec)
      {
        unsigned long ulMinSleepTimeMilSec = MIN_SLEEP_TIME_MILSEC();

        bool bIsRunning = false;
        while (true)
          {
            bIsRunning = isRunning();

            if (bIsRunning == false || ulSleepMilSec <= 0)
              {
                return bIsRunning;
              }

            if (ulSleepMilSec > ulMinSleepTimeMilSec)
              {
                SLEEP_MILISEC(0, ulMinSleepTimeMilSec);
                ulSleepMilSec -= ulMinSleepTimeMilSec;
              }
            else
              {
                SLEEP_MILISEC(0, ulSleepMilSec);
                ulSleepMilSec = 0;
              }
          }
      }

      void execute()
      {
        trait<_ThreadEx>::sp pThread = m_pSelf->shared_from_this();

        if (m_pFunc != NULL)
          {
            try
              {
                (*m_pFunc)(m_pSelf);
              }
            catch (...)
              {
                /* ignore all internal exception */
              }
          }

        _MutexAutoLock lock(&m_mutex);

        if (m_status == THREAD_STATUS_RUNNING)
          {
            m_cond.notifyAll();

            changeThreadStatus(THREAD_OP_STOP);
          }
      }

      void changeThreadStatus(_ThreadOperation op)
      {
        if (op == THREAD_OP_START)
          {
            m_status = THREAD_STATUS_RUNNING;
          }
        else if (op == THREAD_OP_STOP)
          {
            m_status = THREAD_STATUS_STOP;
          }
        else if (op == THREAD_OP_DETACH)
          {
            m_status = THREAD_STATUS_DETACH;
          }
      }

    private:
      _ThreadEx *m_pSelf;
      _ThreadStatus m_status;
      bool m_bIsRunning;
      _ThreadFunctionEx m_pFunc;
      _ConditionVariable m_cond;
      _Mutex m_mutex;
      _Thread m_thread;
    };

    unsigned long _ThreadEx::MIN_SLEEP_TIME_MILSEC()
    {
      return 1000;
    }

    _ThreadEx::_ThreadEx(_ThreadFunctionEx pFunc) :
      m_pImpl(new Impl(this, pFunc))
    {
    }

    _ThreadEx::~_ThreadEx()
    {
      if (m_pImpl != NULL)
        {
          delete m_pImpl;
        }
    }

    void _ThreadEx::start()
    {
      m_pImpl->start();
    }

    void _ThreadEx::join()
    {
      m_pImpl->join();
    }

    void _ThreadEx::timedJoin(unsigned long nWaitTimeMilSec)
    {
      m_pImpl->timedJoin(nWaitTimeMilSec);
    }

    void _ThreadEx::detach()
    {
      m_pImpl->detach();
    }

    _ThreadStatus _ThreadEx::getStatus()
    {
      return m_pImpl->getStatus();
    }

    bool _ThreadEx::isRunning() const
    {
      return m_pImpl->isRunning();
    }

    bool _ThreadEx::sleep(unsigned long ulSleepMilSec) const
    {
      return m_pImpl->sleep(ulSleepMilSec);
    }

    void _ThreadEx::execute()
    {
      m_pImpl->execute();
    }

  }

}
