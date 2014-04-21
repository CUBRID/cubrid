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
#include "dbgw3/system/Thread.h"

namespace dbgw
{

  namespace system
  {

    class _Thread::Impl
    {
    public:
      Impl(_ThreadFunction pFunc, void *pData) :
        m_pFunc(pFunc), m_pData(pData)
      {
        memset(&m_thread_t, 0, sizeof(pthread_t));
      }

      ~Impl()
      {
      }

      void start()
      {
        int nStatus = pthread_create(&m_thread_t, NULL, m_pFunc, m_pData);
        if (nStatus != 0)
          {
            ThreadOperationFailException e("create", nStatus);
            DBGW_LOG_ERROR(e.what());
            throw e;
          }
      }

      void join()
      {
        int nStatus = pthread_join(m_thread_t, NULL);
        if (nStatus != 0)
          {
            ThreadOperationFailException e("join", nStatus);
            DBGW_LOG_ERROR(e.what());
            throw e;
          }
      }

      void detach()
      {
        int nStatus = pthread_detach(m_thread_t);
        if (nStatus != 0)
          {
            ThreadOperationFailException e("detach", nStatus);
            DBGW_LOG_ERROR(e.what());
            throw e;
          }
      }

    private:
      pthread_t m_thread_t;
      _ThreadFunction m_pFunc;
      void *m_pData;
    };

    _Thread::_Thread(_ThreadFunction pFunc, void *pData) :
      m_pImpl(new Impl(pFunc, pData))
    {
    }

    _Thread::~_Thread()
    {
      if (m_pImpl != NULL)
        {
          delete m_pImpl;
        }
    }

    void _Thread::start()
    {
      m_pImpl->start();
    }

    void _Thread::join()
    {
      m_pImpl->join();
    }

    void _Thread::detach()
    {
      m_pImpl->detach();
    }

  }

}
