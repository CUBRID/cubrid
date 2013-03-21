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
#include "dbgw3/client/AsyncWaiter.h"
#include "dbgw3/client/ClientResultSet.h"

namespace dbgw
{

  class _AsyncWaiter::Impl
  {
  public:
    Impl(unsigned long ulTimeOutMilSec, unsigned long ulAbsTimeOutMilSec) :
      m_ulTimeOutMilSec(ulTimeOutMilSec),
      m_ulAbsTimeOutMilSec(ulAbsTimeOutMilSec), m_nHandleId(-1),
      m_pCallBack(NULL), m_pBatchCallBack(NULL)
    {
    }

    Impl(unsigned long ulTimeOutMilSec, unsigned long ulAbsTimeOutMilSec,
        int nHandleId, ExecAsyncCallBack pCallBack) :
      m_ulTimeOutMilSec(ulTimeOutMilSec),
      m_ulAbsTimeOutMilSec(ulAbsTimeOutMilSec), m_nHandleId(nHandleId),
      m_pCallBack(pCallBack), m_pBatchCallBack(NULL)
    {
    }

    Impl(unsigned long ulTimeOutMilSec, unsigned long ulAbsTimeOutMilSec,
        int nHandleId, ExecBatchAsyncCallBack pBatchCallBack) :
      m_ulTimeOutMilSec(ulTimeOutMilSec),
      m_ulAbsTimeOutMilSec(ulAbsTimeOutMilSec), m_nHandleId(nHandleId),
      m_pCallBack(NULL), m_pBatchCallBack(pBatchCallBack)
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

      m_cond.notify();

      if (m_pCallBack != NULL)
        {
          (*m_pCallBack)(m_nHandleId, m_pResultSet, m_exception);
        }
      else if (m_pBatchCallBack != NULL)
        {
          (*m_pBatchCallBack)(m_nHandleId, m_resultSetList, m_exception);
        }
    }

    void bindHandleId(int nHandleId)
    {
      m_nHandleId = nHandleId;
    }

    void bindResultSet(trait<ClientResultSet>::sp pResultSet)
    {
      m_pResultSet = pResultSet;
    }

    void bindResultSetList(
        const trait<ClientResultSet>::spvector &resultSetList)
    {
      m_resultSetList = resultSetList;
    }

    void bindException(const Exception &e)
    {
      m_exception = e;
    }

  public:
    unsigned long getTimeOutMilSec() const
    {
      return m_ulTimeOutMilSec;
    }

    unsigned long getAbsTimeOutMilSec() const
    {
      return m_ulAbsTimeOutMilSec;
    }

  private:
    system::_Mutex m_mutex;
    system::_ConditionVariable m_cond;
    unsigned long m_ulTimeOutMilSec;
    unsigned long m_ulAbsTimeOutMilSec;
    int m_nHandleId;
    ExecAsyncCallBack m_pCallBack;
    ExecBatchAsyncCallBack m_pBatchCallBack;
    trait<ClientResultSet>::sp m_pResultSet;
    trait<ClientResultSet>::spvector m_resultSetList;
    Exception m_exception;
  };

  _AsyncWaiter::_AsyncWaiter(unsigned long ulTimeOutMilSec,
      unsigned long ulAbsTimeOutMilSec) :
    m_pImpl(new Impl(ulTimeOutMilSec, ulAbsTimeOutMilSec))
  {
  }

  _AsyncWaiter::_AsyncWaiter(unsigned long ulTimeOutMilSec,
      unsigned long ulAbsTimeOutMilSec, int nHandleId,
      ExecAsyncCallBack pCallBack) :
    m_pImpl(new Impl(ulTimeOutMilSec, ulAbsTimeOutMilSec, nHandleId, pCallBack))
  {
  }

  _AsyncWaiter::_AsyncWaiter(unsigned long ulTimeOutMilSec,
      unsigned long ulAbsTimeOutMilSec, int nHandleId,
      ExecBatchAsyncCallBack pBatchCallBack) :
    m_pImpl(new Impl(ulTimeOutMilSec, ulAbsTimeOutMilSec, nHandleId,
        pBatchCallBack))
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

  void _AsyncWaiter::bindResultSet(trait<ClientResultSet>::sp pResultSet)
  {
    m_pImpl->bindResultSet(pResultSet);
  }

  void _AsyncWaiter::bindResultSetList(
      const trait<ClientResultSet>::spvector &resultSetList)
  {
    m_pImpl->bindResultSetList(resultSetList);
  }

  void _AsyncWaiter::bindException(const Exception &e)
  {
    m_pImpl->bindException(e);
  }

  unsigned long _AsyncWaiter::getTimeOutMilSec() const
  {
    return m_pImpl->getTimeOutMilSec();
  }

  unsigned long _AsyncWaiter::getAbsTimeOutMilSec() const
  {
    return m_pImpl->getAbsTimeOutMilSec();
  }

}
