/*
 *
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
 * connection_pool.cpp
 */

#include "hardware_topology.hpp"
#include "connection_pool.hpp"
#include "connection_worker.hpp"
#include "server_support.h"
#include "system_parameter.h"
#include "error_manager.h"

#include <csignal>
#include <cstdint>
#include <chrono>
#include <unistd.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <utility>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubconn::connection
{
  pool::pool () :
    m_max_connections (-1),
    m_counter (0)
  {
    m_watcher = std::make_shared<thread_watcher> ();
    m_watcher->active = 0;
  }

  pool::~pool ()
  {
  }

  void pool::initialize (std::uint32_t max_connections, int connection_threads)
  {
    std::vector<int> *cores;
    std::uint32_t i;

    /* signal */
    (void) os_set_signal_handler (SIGPIPE, SIG_IGN);
    (void) os_set_signal_handler (SIGFPE, SIG_IGN);

    /* topology setting */
    cubbase::topology.load_cpu (connection_threads);
    cubbase::topology.map_nic_to_core ();
    cores = &cubbase::topology.get_cores ();

    /* TODO: need to consider dynamic increase */
    m_workers.reserve (cores->size () + 1);

    i = 0;
    for (int core : *cores)
      {
	m_workers.emplace_back (std::make_unique<worker> (this, m_watcher, core, i++));
      }

    /* pre-warm the connection thread and its queue to avoid a race condition. */
    for (std::unique_ptr<worker> &worker : m_workers)
      {
	for (i = 0; i < static_cast<std::size_t> (worker::queue_type::TYPE_COUNT); i++)
	  {
	    worker::message request;

	    request.type = worker::message_type::START;
	    if (!worker->enqueue_and_notify (static_cast<worker::queue_type> (i), std::move (request), nullptr,
					     -1 /* infinite */))
	      {
		assert_release (false);
	      }
	  }
      }

    m_max_connections = max_connections;
    printf ("max_connections: %d\n", max_connections);
  }

  void pool::finalize ()
  {
    std::chrono::system_clock::time_point deadline, now;
    std::chrono::microseconds wait_for (0);
    struct timeval *timeout;
    bool compelete;

    for (auto &worker : m_workers)
      {
	worker::message request;
	request.type = worker::message_type::SHUTDOWN;
	worker->enqueue (worker::queue_type::IMMEDIATE, std::move (request));
	if (!worker->notify ())
	  {
	    assert_release (false);
	  }
      }

    /* shutdown timeout */
    timeout = css_get_shutdown_timeout ();
    deadline = std::chrono::system_clock::time_point (
		       std::chrono::seconds (timeout->tv_sec) +
		       std::chrono::microseconds (timeout->tv_usec));
    now = std::chrono::system_clock::now ();
    if (deadline > now)
      {
	wait_for = std::chrono::duration_cast<std::chrono::microseconds> (deadline - now);
      }

    std::unique_lock<std::mutex> lock (m_watcher->mtx);
    compelete = m_watcher->cv.wait_for (lock, wait_for, [this] { return m_watcher->active == 0; });
    lock.unlock();
    if (!compelete)
      {
	er_log_debug (ARG_FILE_LINE, "could not stop all active connection workers");
	_exit (0);
      }

    m_max_connections = -1;
    m_workers.clear ();
  }

  void pool::dispatch (css_conn_entry *conn)
  {
    /* TODO: in this function, we can take some kind of strategies how	      */
    /* the connection pool distributes the connections to connection workers. */
    /* but now, just uses round robin					      */
    worker::message request;

    request.type = worker::message_type::NEW_CLIENT;
    request.conn = conn;
    m_workers[m_counter]->enqueue (worker::queue_type::IMMEDIATE, std::move (request));
    if (!m_workers[m_counter]->notify ())
      {
	assert_release (false);
      }

    m_counter++;
    if (m_counter == m_workers.size ())
      {
	m_counter = 0;
      }
  }

  void pool::stats ()
  {
    printf ("\033[2J\033[H");
    for (auto &conn : m_workers)
      {
	conn->stats ();
      }
  }

  void pool::initialize_freelist (std::uint32_t max_connections)
  {
    std::uint32_t i;

    for (i = 0; i < max_connections; i++)
      {
      }
  }
}
