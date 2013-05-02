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

#ifndef RESOURCE_H_
#define RESOURCE_H_

namespace dbgw
{

  class _Resource;
  typedef boost::unordered_map<int, trait<_Resource>::sp,
          boost::hash<int>, func::compareInt> _ResourceHashMap;

  class _Resource
  {
  public:
    _Resource();
    virtual ~_Resource();

    void modifyRefCount(int nDelta);
    int getRefCount();

  private:
    int m_nRefCount;
  };

  class _VersionedResource
  {
  public:
    static int INVALID_VERSION();

  public:
    _VersionedResource();
    virtual ~_VersionedResource();

    int getVersion();
    void closeVersion(int nVersion);
    void putResource(trait<_Resource>::sp pResource);
    trait<_Resource>::sp getNewResource();
    trait<_Resource>::sp getResource(int nVersion);
    void clear();

  public:
    size_t size() const;

  private:
    _VersionedResource(const _VersionedResource &);
    _VersionedResource &operator=(const _VersionedResource &);

  private:
    class Impl;
    Impl *m_pImpl;
  };

}

#endif
