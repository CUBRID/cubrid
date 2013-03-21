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

#include <algorithm>
#include "dbgw3/Common.h"
#include "dbgw3/Exception.h"
#include "dbgw3/Logger.h"
#include "dbgw3/Value.h"
#include "dbgw3/ValueSet.h"
#include "dbgw3/system/Mutex.h"
#include "dbgw3/system/ThreadEx.h"
#include "dbgw3/system/ConditionVariable.h"
#include "dbgw3/system/Time.h"
#include "dbgw3/client/Timer.h"
#include "dbgw3/client/Client.h"
#include "dbgw3/client/AsyncWorkerJob.h"

namespace dbgw
{

  _TimerEvent::_TimerEvent(unsigned long ulAbsTimeOutMilSec,
      _AsyncWorkerJobSharedPtr pJob) :
    m_ulAbsTimeOutMilSec(ulAbsTimeOutMilSec), m_pJob(pJob), m_bIsDone(false)
  {
  }

  void _TimerEvent::wakeup()
  {
    m_pJob->cancel();
  }

  bool _TimerEvent::isDone()
  {
    return m_pJob->isDone();
  }

  bool _TimerEvent::needWakeUp(unsigned long ulCurrTimeMilSec) const
  {
    return ulCurrTimeMilSec >= m_ulAbsTimeOutMilSec;
  }

  class _Timer::Impl
  {
  public:
    Impl(_Timer *pSelf) :
      m_pSelf(pSelf)
    {
    }

    ~Impl()
    {
      _TimerEventList::iterator it = m_eventList.begin();
      for (; it != m_eventList.end(); it++)
        {
          if (*it != NULL)
            {
              delete(*it);
            }
        }
    }

    bool waitEvent()
    {
      system::_MutexAutoLock lock(&m_mutex);

      bool bIsRunning = false;
      while ((bIsRunning = m_pSelf->isRunning()) && m_eventList.empty())
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

      return bIsRunning;
    }

    void wakeupEvent()
    {
      unsigned long ulCurrTimeMilSec = 0;
      _TimerEvent *pEvent = NULL;

      while (true)
        {
          m_mutex.lock();

          if (m_eventList.empty())
            {
              m_mutex.unlock();
              break;
            }

          ulCurrTimeMilSec = system::getCurrTimeMilSec();
          pEvent = m_eventList.front();

          if (pEvent->isDone())
            {
              std::pop_heap(m_eventList.begin(), m_eventList.end());
              m_eventList.pop_back();
              m_mutex.unlock();
              delete pEvent;
            }
          else if (pEvent->needWakeUp(ulCurrTimeMilSec))
            {
              pEvent->wakeup();
              std::pop_heap(m_eventList.begin(), m_eventList.end());
              m_eventList.pop_back();
              m_mutex.unlock();
              delete pEvent;
            }
          else
            {
              m_mutex.unlock();

              if (m_pSelf->sleep(100) == false)
                {
                  break;
                }
            }
        }
    }

    void addEvent(_TimerEvent *pJob)
    {
      system::_MutexAutoLock lock(&m_mutex);

      m_eventList.push_back(pJob);
      std::push_heap(m_eventList.begin(), m_eventList.end());

      m_cond.notify();
    }

    static void run(const system::_ThreadEx *pThread)
    {
      if (pThread == NULL)
        {
          FailedToCreateThreadException e("timer");
          DBGW_LOG_ERROR(e.what());
          return;
        }

      _Timer::Impl *pTimerImpl = ((_Timer *) pThread)->m_pImpl;

      while (pTimerImpl->waitEvent())
        {
          pTimerImpl->wakeupEvent();
        }
    }

  private:
    _Timer *m_pSelf;
    system::_ConditionVariable m_cond;
    system::_Mutex m_mutex;
    _TimerEventList m_eventList;
  };

  _Timer::_Timer() :
    system::_ThreadEx(Impl::run), m_pImpl(new Impl(this))
  {
  }

  _Timer::~_Timer()
  {
    if (m_pImpl != NULL)
      {
        delete m_pImpl;
      }
  }

  void _Timer::addEvent(_TimerEvent *pJob)
  {
    m_pImpl->addEvent(pJob);
  }

}
