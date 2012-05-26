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
#include "DBGWCommon.h"
#include "DBGWError.h"
#include "DBGWValue.h"
#include "DBGWLogger.h"
#include "DBGWQuery.h"
#include "DBGWDataBaseInterface.h"
#include "DBGWConfiguration.h"
#include "DBGWClient.h"

namespace dbgw
{

  Mutex::Mutex()
  {
    if (pthread_mutex_init(&m_stMutex, NULL) != 0)
      {
        MutexInitFailException e;
        DBGW_LOG_ERROR(e.what());
        throw e;
      }
  }

  Mutex::~Mutex()
  {
    pthread_mutex_destroy(&m_stMutex);
  }

  void Mutex::lock()
  {
    pthread_mutex_lock(&m_stMutex);
  }

  void Mutex::unlock()
  {
    pthread_mutex_unlock(&m_stMutex);
  }

  MutexLock::MutexLock(Mutex *pMutex) :
    m_pMutex(pMutex), m_bUnlocked(false)
  {
    m_pMutex->lock();
  }

  MutexLock::~MutexLock()
  {
    unlock();
  }

  void MutexLock::unlock()
  {
    if (m_bUnlocked == false)
      {
        m_bUnlocked = true;
        m_pMutex->unlock();
      }
  }

}
