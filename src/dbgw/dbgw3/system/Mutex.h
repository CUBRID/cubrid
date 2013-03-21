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

#ifndef MUTEX_H_
#define MUTEX_H_

namespace dbgw
{

  namespace system
  {

    class _Mutex
    {
    public:
      _Mutex();
      virtual ~_Mutex();

      void lock();
      void unlock();
      void *get();

    private:
      _Mutex(const _Mutex &);
      void operator=(const _Mutex &);

      class Impl;
      Impl *m_pImpl;
    };

    class _MutexAutoLock
    {
    public:
      explicit _MutexAutoLock(_Mutex *pMutex);
      ~_MutexAutoLock();
      void lock();
      void unlock();

    private:
      _Mutex *m_pMutex;
      bool m_bNeedUnlocked;

      _MutexAutoLock(const _MutexAutoLock &);
      void operator=(const _MutexAutoLock &);
    };

  }

}

#endif
