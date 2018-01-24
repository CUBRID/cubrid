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
 * thread_daemon - interface for daemon threads
 */

#ifndef _THREAD_DAEMON_HPP_
#define _THREAD_DAEMON_HPP_

#include "thread_looper.hpp"
#include "thread_waiter.hpp"

#include <thread>

// cubthread::daemon
//
//  description
//    defines a daemon thread using a looper and a task
//    task is executed in a loop; wait times are defined by looper
//
//  how to use
//    // define your task
//    class custom_task : public task
//    {
//      void execute () override { ... }
//    }
//
//    // declare a looper
//    cubthread::looper loop_pattern;   // by default sleep until wakeup
//    cubthread::daemon my_daemon (loop_pattern, new custom_task ());    // daemon starts, executes task and sleeps
//
//    std::chrono::sleep_for (std::chrono::seconds (1));
//    my_daemon.wakeup ();    // daemon executes task again
//    std::chrono::sleep_for (std::chrono::seconds (1));
//
//    // when daemon is destroyed, its execution is stopped and thread is joined
//
namespace cubthread
{

// forward definition
  class task;

  class daemon
  {
    public:
      daemon (const looper &loop_pattern, task *exec);
      ~daemon();

      void wakeup (void);     // wakeup daemon thread
      void stop (void);       // stop daemon thread from looping and join it
                              // note: this must not be called concurrently

    private:

      static void loop (daemon *daemon_arg, task *exec);    // daemon thread loop function

      void pause (void);                                    // pause between tasks

      waiter m_waiter;        // thread waiter
      looper m_looper;        // thread looper
      std::thread m_thread;   // the actual daemon thread
  };

} // namespace cubthread

#endif // _THREAD_DAEMON_HPP_
