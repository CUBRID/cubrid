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

#ifndef THREAD_H_
#define THREAD_H_

namespace dbgw
{

  namespace system
  {

    typedef THREAD_RET_T(THREAD_CALLING_CONVENTION *_ThreadFunction)(void *);

    class _Thread
    {
    public:
      _Thread(_ThreadFunction pFunc, void *pData);
      ~_Thread();

      void start();
      void join();
      void detach();

    private:
      _Thread(const _Thread &);
      _Thread &operator=(const _Thread &);

    private:
      class Impl;
      Impl *m_pImpl;
    };

  }

}

#endif
