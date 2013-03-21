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
// for header
#include "dbgw3/system/Mutex.h"
// for source
#include "dbgw3/Exception.h"
#include "dbgw3/Logger.h"
#include "dbgw3/system/Mutex.h"
#include "dbgw3/client/Resource.h"

namespace dbgw
{

  _Resource::_Resource() :
    m_nRefCount(0)
  {
  }

  _Resource::~_Resource()
  {
  }

  void _Resource::modifyRefCount(int nDelta)
  {
    m_nRefCount += nDelta;
  }

  int _Resource::getRefCount()
  {
    return m_nRefCount;
  }

  int _VersionedResource::INVALID_VERSION()
  {
    return -1;
  }

  class _VersionedResource::Impl
  {
  public:
    Impl() :
      m_nVersion(INVALID_VERSION())
    {
    }

    ~Impl()
    {
      clear();
    }

    int getVersion()
    {
      system::_MutexAutoLock lock(&m_mutex);

      if (m_nVersion > INVALID_VERSION())
        {
          m_pResource->modifyRefCount(1);
        }

      return m_nVersion;
    }

    void closeVersion(int nVersion)
    {
      if (nVersion <= INVALID_VERSION())
        {
          return;
        }

      system::_MutexAutoLock lock(&m_mutex);

      trait<_Resource>::sp pResource = getResourceWithUnlock(nVersion);
      pResource->modifyRefCount(-1);

      _ResourceHashMap::iterator it = m_resourceMap.begin();
      while (it != m_resourceMap.end())
        {
          if (it->second->getRefCount() <= 0)
            {
              m_resourceMap.erase(it++);
            }
          else
            {
              ++it;
            }
        }
    }

    void putResource(trait<_Resource>::sp pResource)
    {
      system::_MutexAutoLock lock(&m_mutex);

      if (m_pResource != NULL && m_nVersion > INVALID_VERSION()
          && m_pResource->getRefCount() > 0)
        {
          m_resourceMap[m_nVersion] = m_pResource;
        }

      m_nVersion = (m_nVersion == INT_MAX) ? 0 : m_nVersion + 1;
      m_pResource = pResource;
    }

    trait<_Resource>::sp getNewResource()
    {
      system::_MutexAutoLock lock(&m_mutex);

      if (m_nVersion <= INVALID_VERSION())
        {
          return trait<_Resource>::sp();
        }

      return m_pResource;
    }

    trait<_Resource>::sp getResource(int nVersion)
    {
      system::_MutexAutoLock lock(&m_mutex);

      return getResourceWithUnlock(nVersion);
    }

    void clear()
    {
      system::_MutexAutoLock lock(&m_mutex);

      m_pResource.reset();
      m_resourceMap.clear();
    }

    trait<_Resource>::sp getResourceWithUnlock(int nVersion)
    {
      if (nVersion <= INVALID_VERSION())
        {
          NotYetLoadedException e;
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      if (nVersion == m_nVersion)
        {
          return m_pResource;
        }

      _ResourceHashMap::iterator it = m_resourceMap.find(nVersion);
      if (it == m_resourceMap.end())
        {
          NotExistVersionException e(nVersion);
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      return it->second;
    }

    size_t size() const
    {
      return m_resourceMap.size();
    }

  private:
    system::_Mutex m_mutex;
    int m_nVersion;
    trait<_Resource>::sp m_pResource;
    _ResourceHashMap m_resourceMap;

  };

  _VersionedResource::_VersionedResource() :
    m_pImpl(new Impl())
  {
  }

  _VersionedResource::~_VersionedResource()
  {
    if (m_pImpl != NULL)
      {
        delete m_pImpl;
      }
  }

  int _VersionedResource::getVersion()
  {
    return m_pImpl->getVersion();
  }

  void _VersionedResource::closeVersion(int nVersion)
  {
    return m_pImpl->closeVersion(nVersion);
  }

  void _VersionedResource::putResource(trait<_Resource>::sp pResource)
  {
    m_pImpl->putResource(pResource);
  }

  trait<_Resource>::sp _VersionedResource::getNewResource()
  {
    return m_pImpl->getNewResource();
  }

  trait<_Resource>::sp _VersionedResource::getResource(int nVersion)
  {
    return m_pImpl->getResource(nVersion);
  }

  void _VersionedResource::clear()
  {
    return m_pImpl->clear();
  }

  size_t _VersionedResource::size() const
  {
    return m_pImpl->size();
  }

}
