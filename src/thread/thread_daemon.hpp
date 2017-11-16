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

/*
 *
 */

#ifndef _THREAD_DAEMON_HPP_
#define _THREAD_DAEMON_HPP_

#include "thread_looper.hpp"
#include "thread_waiter.hpp"

#include <thread>

namespace cubthread
{

// forward definition
  class task;

  class daemon
  {
    public:
      daemon (const looper &loop_pattern, task *exec);
      ~daemon();

      void wakeup (void);
      void stop (void);

    private:

      static void loop (daemon *daemon_arg, task *exec);

      void pause (void);

      waiter m_waiter;
      looper m_looper;
      std::thread m_thread;
  };



} // namespace cubthread

#endif // _THREAD_DAEMON_HPP_
