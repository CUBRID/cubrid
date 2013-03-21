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

namespace dbgw
{

  namespace system
  {

    class _ConditionVariable::Impl
    {
    public:
      Impl()
      {
        int nStatus = pthread_cond_init(&m_cond_t, NULL);
        if (nStatus != 0)
          {
            CondVarOperationFailException e("init", nStatus);
            DBGW_LOG_ERROR(e.what());
            throw e;
          }
      }

      ~Impl()
      {
        int nStatus = pthread_cond_destroy(&m_cond_t);
        if (nStatus == EBUSY)
          {
            notifyAll();
            nStatus = pthread_cond_destroy(&m_cond_t);
          }

        if (nStatus != 0)
          {
            CondVarOperationFailException e("destroy", nStatus);
            DBGW_LOG_ERROR(e.what());
          }
      }

      void notify()
      {
        int nStatus = pthread_cond_signal(&m_cond_t);
        if (nStatus != 0)
          {
            CondVarOperationFailException e("signal", nStatus);
            DBGW_LOG_ERROR(e.what());
            throw e;
          }
      }

      void notifyAll()
      {
        int nStatus = pthread_cond_broadcast(&m_cond_t);
        if (nStatus != 0)
          {
            CondVarOperationFailException e("broadcast", nStatus);
            DBGW_LOG_ERROR(e.what());
            throw e;
          }
      }

      void wait(_Mutex *pMutex)
      {
        int nStatus = pthread_cond_wait(&m_cond_t,
            (pthread_mutex_t *) pMutex->get());
        if (nStatus != 0)
          {
            CondVarOperationFailException e("wait", nStatus);
            DBGW_LOG_ERROR(e.what());
            throw e;
          }
      }

      void timedWait(_Mutex *pMutex,
          unsigned long lWaitTimeMilSec)
      {
        struct timeval tp;
        struct timespec ts;

        gettimeofday(&tp, NULL);

        long int abstime_usec = tp.tv_usec + lWaitTimeMilSec * 1000;

        ts.tv_sec = tp.tv_sec + abstime_usec / 1000000;
        ts.tv_nsec = (abstime_usec % 1000000) * 1000;

        int nStatus = pthread_cond_timedwait(&m_cond_t,
            (pthread_mutex_t *) pMutex->get(), &ts);
        if (nStatus != 0)
          {
            CondVarOperationFailException e("timed wait", nStatus);
            if (nStatus != ETIMEDOUT)
              {
                DBGW_LOG_ERROR(e.what());
              }
            throw e;
          }
      }

    private:
      pthread_cond_t m_cond_t;
    };

    _ConditionVariable::_ConditionVariable() :
      m_pImpl(new Impl())
    {
    }

    _ConditionVariable::~_ConditionVariable()
    {
      if (m_pImpl != NULL)
        {
          delete m_pImpl;
        }
    }

    void _ConditionVariable::notify()
    {
      m_pImpl->notify();
    }

    void _ConditionVariable::notifyAll()
    {
      m_pImpl->notifyAll();
    }

    void _ConditionVariable::wait(_Mutex *pMutex)
    {
      m_pImpl->wait(pMutex);
    }

    void _ConditionVariable::timedWait(_Mutex *pMutex,
        unsigned long ulWaitTimeMilSec)
    {
      m_pImpl->timedWait(pMutex, ulWaitTimeMilSec);
    }

  }

}
