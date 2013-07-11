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
#include "dbgw3/system/Mutex.h"
#include "dbgw3/system/ConditionVariable.h"
#include "dbgw3/client/Client.h"
#include "dbgw3/client/AsyncWorkerJob.h"
#include "dbgw3/client/AsyncWaiter.h"
#include "dbgw3/client/ClientResultSet.h"

namespace dbgw
{

  class _AsyncWaiter::Impl
  {
  public:
    Impl(unsigned long ulTimeOutMilSec) :
      m_ulTimeOutMilSec(ulTimeOutMilSec), m_nHandleId(-1),
      m_pCallBack(NULL), m_pBatchCallBack(NULL), m_pData(NULL)
    {
    }

    Impl(unsigned long ulTimeOutMilSec, int nHandleId,
        ExecAsyncCallBack pCallBack, void *pData) :
      m_ulTimeOutMilSec(ulTimeOutMilSec), m_nHandleId(nHandleId),
      m_pCallBack(pCallBack), m_pBatchCallBack(NULL), m_pData(pData)
    {
    }

    Impl(unsigned long ulTimeOutMilSec, int nHandleId,
        ExecBatchAsyncCallBack pBatchCallBack, void *pData) :
      m_ulTimeOutMilSec(ulTimeOutMilSec), m_nHandleId(nHandleId),
      m_pCallBack(NULL), m_pBatchCallBack(pBatchCallBack), m_pData(pData)
    {
    }

    ~Impl() {}

    void wait()
    {
      system::_MutexAutoLock lock(&m_mutex);

      m_cond.wait(&m_mutex);
    }

    void notify()
    {
      system::_MutexAutoLock lock(&m_mutex);

      try
        {
          m_cond.notify();
        }
      catch (CondVarOperationFailException &)
        {
          /**
           * ignore condition variable fail exception.
           */
        }

      if (m_pCallBack != NULL)
        {
          (*m_pCallBack)(m_nHandleId, m_pJobResult->getResultSet(),
              m_pJobResult->getException(), m_pData);
        }
      else if (m_pBatchCallBack != NULL)
        {
          (*m_pBatchCallBack)(m_nHandleId, m_pJobResult->getResultSetList(),
              m_pJobResult->getException(), m_pData);
        }
    }

    void bindJobResult(trait<_AsyncWorkerJobResult>::sp pJobResult)
    {
      m_pJobResult = pJobResult;
    }

    trait<_AsyncWorkerJobResult>::sp getJobResult()
    {
      trait<_AsyncWorkerJobResult>::sp pJobResult = m_pJobResult;
      m_pJobResult.reset();
      return pJobResult;
    }

  public:
    unsigned long getTimeOutMilSec() const
    {
      return m_ulTimeOutMilSec;
    }

  private:
    system::_Mutex m_mutex;
    system::_ConditionVariable m_cond;
    unsigned long m_ulTimeOutMilSec;
    int m_nHandleId;
    ExecAsyncCallBack m_pCallBack;
    ExecBatchAsyncCallBack m_pBatchCallBack;
    void *m_pData;
    trait<_AsyncWorkerJobResult>::sp m_pJobResult;
  };

  _AsyncWaiter::_AsyncWaiter(unsigned long ulTimeOutMilSec) :
    m_pImpl(new Impl(ulTimeOutMilSec))
  {
  }

  _AsyncWaiter::_AsyncWaiter(unsigned long ulTimeOutMilSec, int nHandleId,
      ExecAsyncCallBack pCallBack, void *pData) :
    m_pImpl(new Impl(ulTimeOutMilSec, nHandleId, pCallBack, pData))
  {
  }

  _AsyncWaiter::_AsyncWaiter(unsigned long ulTimeOutMilSec, int nHandleId,
      ExecBatchAsyncCallBack pBatchCallBack, void *pData) :
    m_pImpl(new Impl(ulTimeOutMilSec, nHandleId,
        pBatchCallBack, pData))
  {
  }

  _AsyncWaiter::~_AsyncWaiter()
  {
    if (m_pImpl != NULL)
      {
        delete m_pImpl;
      }
  }

  void _AsyncWaiter::wait()
  {
    m_pImpl->wait();
  }

  void _AsyncWaiter::notify()
  {
    m_pImpl->notify();
  }

  void _AsyncWaiter::bindJobResult(trait<_AsyncWorkerJobResult>::sp pJobResult)
  {
    m_pImpl->bindJobResult(pJobResult);
  }

  trait<_AsyncWorkerJobResult>::sp _AsyncWaiter::getJobResult()
  {
    return m_pImpl->getJobResult();
  }

  unsigned long _AsyncWaiter::getTimeOutMilSec() const
  {
    return m_pImpl->getTimeOutMilSec();
  }

}
