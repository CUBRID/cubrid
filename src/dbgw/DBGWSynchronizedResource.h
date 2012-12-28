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

#ifndef DBGWSYNCHRONIZEDRESOURCE_H_
#define DBGWSYNCHRONIZEDRESOURCE_H_

namespace dbgw
{

  class _DBGWSynchronizedResource;
  typedef vector<_DBGWSynchronizedResource *>
  _DBGWSynchronizedResourceList;
  typedef list<int> _DBGWSynchronizedResourceIdList;

  class _DBGWSynchronizedResourceSubject
  {
  public:
    _DBGWSynchronizedResourceSubject();
    virtual ~_DBGWSynchronizedResourceSubject();

    void registerResource(_DBGWSynchronizedResource *pResource);
    void unregisterResource(int nId);
    void unregisterResourceAll();

  private:
    system::_MutexSharedPtr m_pIdMutex;
    _DBGWSynchronizedResourceIdList m_idList;
    system::_MutexSharedPtr m_pResourceMutex;
    _DBGWSynchronizedResourceList m_resourceList;
  };

  class _DBGWSynchronizedResource
  {
  public:
    _DBGWSynchronizedResource();
    virtual ~_DBGWSynchronizedResource();

    void closeResource();

    /**
     * this method is called by _DBGWSynchronizedResourceSubject.
     * don't call this method directly.
     */
    void linkSubject(int nId, _DBGWSynchronizedResourceSubject *pSubject);
    void unlinkSubject();

  public:
    bool isClosedResource() const;

  protected:
    virtual void doUnlinkResource();

  private:
    int m_nId;
    _DBGWSynchronizedResourceSubject *m_pSubject;
  };

}

#endif
