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

#ifndef SYNCHRONIZEDRESOURCE_H_
#define SYNCHRONIZEDRESOURCE_H_

namespace dbgw
{

  class _SynchronizedResource;

  class _SynchronizedResourceSubject
  {
  public:
    _SynchronizedResourceSubject();
    virtual ~_SynchronizedResourceSubject();

    void registerResource(_SynchronizedResource *pResource);
    void unregisterResource(int nId);
    void unregisterResourceAll();

  private:
    _SynchronizedResourceSubject(const _SynchronizedResourceSubject &);
    _SynchronizedResourceSubject &operator=(const _SynchronizedResourceSubject &);

  private:
    class Impl;
    Impl *m_pImpl;
  };

  class _SynchronizedResource
  {
  public:
    _SynchronizedResource();
    virtual ~_SynchronizedResource();

    void closeResource();

    /**
     * this method is called by _DBGWSynchronizedResourceSubject.
     * don't call this method directly.
     */
    void linkSubject(int nId, _SynchronizedResourceSubject *pSubject);
    void unlinkSubject();

  public:
    bool isClosedResource() const;

  protected:
    virtual void doUnlinkResource();

  private:
    int m_nId;
    _SynchronizedResourceSubject *m_pSubject;
  };

}

#endif
