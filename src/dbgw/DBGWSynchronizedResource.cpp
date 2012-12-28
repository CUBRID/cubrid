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
#include "DBGWPorting.h"
#include "DBGWValue.h"
#include "DBGWLogger.h"
#include "DBGWSynchronizedResource.h"

namespace dbgw
{

  _DBGWSynchronizedResourceSubject::_DBGWSynchronizedResourceSubject() :
    m_pIdMutex(system::_MutexFactory::create()),
    m_pResourceMutex(system::_MutexFactory::create())
  {
  }

  _DBGWSynchronizedResourceSubject::~_DBGWSynchronizedResourceSubject()
  {
    unregisterResourceAll();
  }

  void _DBGWSynchronizedResourceSubject::registerResource(
      _DBGWSynchronizedResource *pResource)
  {
    system::_MutexAutoLock resourceLock(m_pResourceMutex);
    system::_MutexAutoLock idLock(m_pIdMutex);

    int nId = -1;
    if (m_idList.empty())
      {
        nId = m_resourceList.size();
        m_resourceList.push_back(pResource);
      }
    else
      {
        nId = m_idList.front();
        m_idList.pop_front();
        m_resourceList[nId] = pResource;
      }

    pResource->linkSubject(nId, this);
  }

  void _DBGWSynchronizedResourceSubject::unregisterResource(int nId)
  {
    system::_MutexAutoLock idLock(m_pIdMutex);

    m_resourceList[nId] = NULL;
    m_idList.push_back(nId);
  }

  void _DBGWSynchronizedResourceSubject::unregisterResourceAll()
  {
    system::_MutexAutoLock resourceLock(m_pResourceMutex);

    for (int i = 0, size = m_resourceList.size(); i < size; i++)
      {
        if (m_resourceList[i] != NULL)
          {
            m_resourceList[i]->unlinkSubject();
            m_resourceList[i] = NULL;

            m_pIdMutex->lock();
            m_idList.push_back(i);
            m_pIdMutex->unlock();
          }
      }
  }

  _DBGWSynchronizedResource::_DBGWSynchronizedResource() :
    m_nId(-1), m_pSubject(NULL)
  {
  }

  _DBGWSynchronizedResource::~_DBGWSynchronizedResource()
  {
  }

  void _DBGWSynchronizedResource::linkSubject(int nId,
      _DBGWSynchronizedResourceSubject *pSubject)
  {
    m_nId = nId;
    m_pSubject = pSubject;
  }

  void _DBGWSynchronizedResource::unlinkSubject()
  {
    doUnlinkResource();
    m_nId = -1;
    m_pSubject = NULL;
  }

  void _DBGWSynchronizedResource::closeResource()
  {
    if (isClosedResource() == false)
      {
        m_pSubject->unregisterResource(m_nId);
        unlinkSubject();
      }
  }

  bool _DBGWSynchronizedResource::isClosedResource() const
  {
    return m_pSubject == NULL;
  }

  void _DBGWSynchronizedResource::doUnlinkResource()
  {
  }

}
