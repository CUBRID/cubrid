/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

/*
 * connection_worker.cpp
 */

#include "connection_worker.hpp"
#include "error_manager.h"

#include <array>
#include <thread>
#include <unistd.h>
#include <sys/eventfd.h>

namespace cubconn
{
  connection_worker::connection_worker (connection_pool *pool, std::size_t index) :
    m_parent (),
    m_index (index)
  {
    m_eventfd = eventfd (0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (m_eventfd == -1)
      {
	assert_release (false);
      }

    m_thread = std::thread (&connection_worker::run, this);
  }

  connection_worker::~connection_worker ()
  {
    if (m_thread.joinable ())
      {
	m_thread.join ();
      }
    ::close (m_eventfd);
  }

  void connection_worker::enqueue ()
  {
  }

  void connection_worker::notify ()
  {
    std::uint64_t u;

    u = 1;
    ::write (m_eventfd, &u, sizeof (u));
  }

  void connection_worker::run ()
  {
    std::array<epoll_event, 32> events;

    sleep (1);
    _er_log_debug (__FILE__, __LINE__, "connectionr_worker->run: %d\n", m_index);
  }
}
