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
#include "dbgw3/SynchronizedResource.h"
#include "dbgw3/system/Mutex.h"

namespace dbgw
{

  class _SynchronizedResourceSubject::Impl
  {
  public:
    Impl(_SynchronizedResourceSubject *pSelf) :
      m_pSelf(pSelf)
    {
    }

    ~Impl()
    {
      unregisterResourceAll();
    }

    void registerResource(_SynchronizedResource *pResource)
    {
      system::_MutexAutoLock lock(&m_mutex);

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

      pResource->linkSubject(nId, m_pSelf);
    }

    void unregisterResource(int nId)
    {
      system::_MutexAutoLock lock(&m_mutex);

      m_resourceList[nId] = NULL;
      m_idList.push_back(nId);
    }

    void unregisterResourceAll()
    {
      system::_MutexAutoLock lock(&m_mutex);

      for (int i = 0, size = m_resourceList.size(); i < size; i++)
        {
          if (m_resourceList[i] != NULL)
            {
              m_resourceList[i]->unlinkSubject();
              m_resourceList[i] = NULL;
              m_idList.push_back(i);
            }
        }
    }

  private:
    _SynchronizedResourceSubject *m_pSelf;
    system::_Mutex m_mutex;
    trait<int>::list m_idList;
    trait<_SynchronizedResource *>::vector m_resourceList;
  };

  _SynchronizedResourceSubject::_SynchronizedResourceSubject() :
    m_pImpl(new Impl(this))
  {
  }

  _SynchronizedResourceSubject::~_SynchronizedResourceSubject()
  {
    if (m_pImpl != NULL)
      {
        delete m_pImpl;
      }
  }

  void _SynchronizedResourceSubject::registerResource(
      _SynchronizedResource *pResource)
  {
    m_pImpl->registerResource(pResource);
  }

  void _SynchronizedResourceSubject::unregisterResource(int nId)
  {
    m_pImpl->unregisterResource(nId);
  }

  void _SynchronizedResourceSubject::unregisterResourceAll()
  {
    m_pImpl->unregisterResourceAll();
  }

  _SynchronizedResource::_SynchronizedResource() :
    m_nId(-1), m_pSubject(NULL)
  {
  }

  _SynchronizedResource::~_SynchronizedResource()
  {
  }

  void _SynchronizedResource::linkSubject(int nId,
      _SynchronizedResourceSubject *pSubject)
  {
    m_nId = nId;
    m_pSubject = pSubject;
  }

  void _SynchronizedResource::unlinkSubject()
  {
    doUnlinkResource();
    m_nId = -1;
    m_pSubject = NULL;
  }

  void _SynchronizedResource::closeResource()
  {
    if (isClosedResource() == false)
      {
        m_pSubject->unregisterResource(m_nId);
        unlinkSubject();
      }
  }

  bool _SynchronizedResource::isClosedResource() const
  {
    return m_pSubject == NULL;
  }

  void _SynchronizedResource::doUnlinkResource()
  {
  }

}
