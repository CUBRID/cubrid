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
#include "dbgw3/system/DBGWPorting.h"
#include "dbgw3/system/Mutex.h"
#include "dbgw3/Logger.h"

namespace dbgw
{

  namespace system
  {

    /* for windows porting UMR problem */
    static const pthread_mutex_t g_mutexForInit = PTHREAD_MUTEX_INITIALIZER;

    class _Mutex::Impl
    {
    public:
      Impl() :
        m_mutex(g_mutexForInit)
      {
        int nStatus = pthread_mutex_init(&m_mutex, NULL);
        if (nStatus != 0)
          {
            MutexOperationFailException e("init", nStatus);
            DBGW_LOG_ERROR(e.what());
            throw e;
          }
      }

      ~Impl()
      {
        int nStatus = pthread_mutex_destroy(&m_mutex);
        if (nStatus == EBUSY)
          {
            nStatus = pthread_mutex_destroy(&m_mutex);
          }

        if (nStatus != 0)
          {
            MutexOperationFailException e("destroy", nStatus);
            DBGW_LOG_ERROR(e.what());
          }
      }

      void lock()
      {
        int nStatus = pthread_mutex_lock(&m_mutex);
        if (nStatus != 0)
          {
            MutexOperationFailException e("lock", nStatus);
            DBGW_LOG_ERROR(e.what());
            throw e;
          }
      }

      void unlock()
      {
        int nStatus = pthread_mutex_unlock(&m_mutex);
        if (nStatus != 0)
          {
            MutexOperationFailException e("unlock", nStatus);
            DBGW_LOG_ERROR(e.what());
            throw e;
          }
      }

      void *get()
      {
        return &m_mutex;
      }

    private:
      pthread_mutex_t m_mutex;
    };

    _Mutex::_Mutex() :
      m_pImpl(new Impl())
    {
    }

    _Mutex::~_Mutex()
    {
      if (m_pImpl != NULL)
        {
          delete m_pImpl;
        }
    }

    void _Mutex::lock()
    {
      m_pImpl->lock();
    }

    void _Mutex::unlock()
    {
      m_pImpl->unlock();
    }

    void *_Mutex::get()
    {
      return m_pImpl->get();
    }

    _MutexAutoLock::_MutexAutoLock(_Mutex *pMutex) :
      m_pMutex(pMutex), m_bNeedUnlocked(true)
    {
      m_pMutex->lock();
    }

    _MutexAutoLock::~_MutexAutoLock()
    {
      unlock();
    }

    void _MutexAutoLock::lock()
    {
      if (m_bNeedUnlocked == false)
        {
          m_bNeedUnlocked = true;

          m_pMutex->lock();
        }
    }

    void _MutexAutoLock::unlock()
    {
      if (m_bNeedUnlocked)
        {
          m_bNeedUnlocked = false;
          m_pMutex->unlock();
        }
    }

  }

}
