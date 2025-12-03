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

#include <cmath>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
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
    m_max_connection_threads (-1),
    m_min_connection_threads (-1)
  {
    m_watcher = std::make_shared<thread_watcher> ();
    m_watcher->active = 0;
  }

  pool::~pool ()
  {
  }

  void pool::initialize (std::uint32_t max_connections, int max_connection_threads, int min_connection_threads)
  {
    (void) os_set_signal_handler (SIGPIPE, SIG_IGN);
    (void) os_set_signal_handler (SIGFPE, SIG_IGN);

    this->initialize_freelist (max_connections);
    this->initialize_topology (max_connection_threads);
    this->initialize_workers (max_connection_threads, min_connection_threads);
    this->initialize_coordinator ();

    m_max_connections = max_connections;
    m_max_connection_threads = max_connection_threads;
    m_min_connection_threads = min_connection_threads;
  }

  void pool::finalize ()
  {
    this->finalize_coordinator ();
    this->finalize_workers ();
    this->finalize_topology ();
    this->finalize_freelist ();

    m_max_connections = -1;
    m_max_connection_threads = -1;
    m_min_connection_threads = -1;
  }

  void pool::dispatch (css_conn_entry *conn)
  {
    /* TODO: in this function, we can take some kind of strategies how	      */
    /* the connection pool distributes the connections to connection workers. */
    /* but now, just uses round robin					      */
    worker::message request;

    request.type = worker::message_type::NEW_CLIENT;
    request.ctx = this->claim_context ();
    /* TODO: null guard */
    request.conn = conn;
    m_workers[0]->enqueue (worker::queue_type::IMMEDIATE, std::move (request));
    if (!m_workers[0]->notify ())
      {
	assert_release (false);
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
    freelist *head;
    std::size_t i;

    m_freelist.m_head = nullptr;
    m_freelist.m_claim = 0;
    m_freelist.m_max = static_cast<std::size_t> (static_cast<float> (max_connections) * /* margin */ 1.1);
    for (i = 0; i < m_freelist.m_max; i++)
      {
	head = m_freelist.m_head;
	m_freelist.m_head = new freelist (32 * 1024);
	m_freelist.m_head->m_next = head;
      }
  }

  void pool::finalize_freelist ()
  {
    freelist *head;

    assert (m_freelist.m_claim == 0);

    while (m_freelist.m_head)
      {
	head = m_freelist.m_head;
	m_freelist.m_head = m_freelist.m_head->m_next;
	delete head;
      }

    m_freelist.m_max = 0;
    m_freelist.m_claim = 0;
  }

  void pool::initialize_topology (std::uint32_t max_connection_threads)
  {
    cubbase::topology.load_cpu (max_connection_threads);
    cubbase::topology.map_nic_to_core ();
  }

  void pool::finalize_topology ()
  {
  }

  void pool::initialize_workers (std::uint32_t max_connection_threads, std::uint32_t min_connection_threads)
  {
    std::vector<int> *cores;
    std::uint32_t i;

    m_workers.reserve (max_connection_threads + 1);

    cores = &cubbase::topology.get_cores ();

    assert (cores->size () == max_connection_threads);

    for (i = 0; i < max_connection_threads; i++)
      {
	m_workers.emplace_back (std::make_unique<worker> (this, m_watcher, (*cores)[i], i));
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
  }

  void pool::finalize_workers ()
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
    lock.unlock ();
    if (!compelete)
      {
	er_log_debug (ARG_FILE_LINE, "could not stop all active connection workers");
	_exit (0);
      }

    m_workers.clear ();
  }

  void pool::initialize_coordinator ()
  {
    coordinator::message request;
    std::vector<int> *cores;

    cores = &cubbase::topology.get_cores ();
    m_coordinator = std::make_unique<coordinator> (this, m_watcher, (*cores)[0]);

    request.type = coordinator::message_type::START;
    m_coordinator->enqueue (std::move (request));
    if (!m_coordinator->notify ())
      {
	assert_release (false);
      }
  }

  void pool::finalize_coordinator ()
  {
    coordinator::message request;

    request.type = coordinator::message_type::SHUTDOWN;
    m_coordinator->enqueue (std::move (request));
    if (!m_coordinator->notify ())
      {
	assert_release (false);
      }
  }

  context *pool::claim_context ()
  {
    freelist *head;

    head = m_freelist.m_head;
    if (head)
      {
	m_freelist.m_head = m_freelist.m_head->m_next;
      }
    else
      {
	head = new freelist (32 * 1024);
      }
    m_freelist.m_claim++;

    return &head->m_context;
  }

  void pool::retire_context (context *ctx)
  {
    freelist *head;

    head = reinterpret_cast<freelist *> (ctx);
    if (m_freelist.m_claim > m_freelist.m_max)
      {
	delete head;
      }
    else
      {
	head->m_context.reset ();
	head->m_next = m_freelist.m_head;
	m_freelist.m_head = head;
      }
    m_freelist.m_claim--;
  }
}
