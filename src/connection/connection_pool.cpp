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

#include <numeric>
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

#include "resources.hpp"
#include "thread_manager.hpp"
#include "connection_pool.hpp"
#include "connection_worker.hpp"
#include "server_support.h"
#include "system_parameter.h"
#include "error_manager.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubconn::connection
{
  pool::pool () :
    m_max_connections (-1),
    m_max_connection_workers (-1),
    m_min_connection_workers (-1),
    m_freelist { nullptr, 0, 0 }
  {
    m_watcher = std::make_shared<thread_watcher> ();
    m_watcher->active = 0;
  }

  pool::~pool ()
  {
  }

  bool pool::initialize (std::uint32_t max_connections, int max_connection_workers, int min_connection_workers)
  {
    (void) os_set_signal_handler (SIGPIPE, SIG_IGN);
    (void) os_set_signal_handler (SIGFPE, SIG_IGN);

    max_connection_workers = this->initialize_topology (max_connection_workers);
    if (min_connection_workers > max_connection_workers)
      {
	min_connection_workers = max_connection_workers;
      }

    this->lock_resource ();

    if (!this->initialize_freelist (max_connections))
      {
	this->release_resource ();

	return false;
      }
    this->initialize_coordinator (max_connection_workers, min_connection_workers);
    this->initialize_workers (max_connection_workers, min_connection_workers);

    this->release_resource ();

    /* request to start coordinating */
    this->start_coordinator ();

    m_max_connections = max_connections;
    m_max_connection_workers = max_connection_workers;
    m_min_connection_workers = min_connection_workers;

    return true;
  }

  void pool::finalize ()
  {
    this->finalize_workers ();
    this->finalize_coordinator ();

    /* acquire the lock or kill itself */
    this->try_to_lock_resource ();

    /* drain all resources from the coordinator and workers */
    this->drain_contexts ();

    m_workers.clear ();
    this->finalize_freelist ();

    this->release_resource ();

    this->finalize_topology ();

    m_max_connections = -1;
    m_max_connection_workers = -1;
    m_min_connection_workers = -1;
  }

  void pool::dispatch (css_conn_entry *conn)
  {
    coordinator::message request;

    request.type = coordinator::message_type::NEW_CLIENT;
    request.conn = conn;
    m_coordinator->enqueue (std::move (request));
    if (!m_coordinator->notify ())
      {
	assert_release (false);
      }
  }

  void pool::lock_resource ()
  {
    m_mutex.lock ();

#if !defined (NDEBUG)
    m_mutex_holder = std::this_thread::get_id ();
#endif
  }

  void pool::release_resource ()
  {
#if !defined (NDEBUG)
    m_mutex_holder = std::thread::id ();
#endif

    m_mutex.unlock ();
  }

  context *pool::claim_context ()
  {
    freelist *head;

    assert (m_mutex_holder == std::this_thread::get_id ());

    head = m_freelist.m_head;
    if (head)
      {
	m_freelist.m_head = m_freelist.m_head->m_next;
      }
    else
      {
	head = new freelist (32 * 1024);
	if (!head->prepare ())
	  {
	    delete head;

	    return nullptr;
	  }
      }
    m_freelist.m_claim++;

    return &head->m_context;
  }

  void pool::retire_context (context *ctx)
  {
    freelist *head;

    assert (m_mutex_holder == std::this_thread::get_id ());

    head = reinterpret_cast<freelist *> (ctx);
    head->m_context.reset ();
    if (m_freelist.m_claim > m_freelist.m_max)
      {
	delete head;
      }
    else
      {
	head->m_next = m_freelist.m_head;
	m_freelist.m_head = head;
      }
    m_freelist.m_claim--;
  }

  std::vector<std::unique_ptr<worker>> &pool::get_workers ()
  {
    assert (m_mutex_holder == std::this_thread::get_id ());

    return m_workers;
  }

  void pool::drain_contexts ()
  {
    if (m_coordinator)
      {
	m_coordinator->finalize_resources ();
      }

    if (!m_workers.empty ())
      {
	for (std::unique_ptr<worker> &worker : m_workers)
	  {
	    worker->finalize_resources ();
	  }
      }
  }

  void pool::try_to_lock_resource ()
  {
    int i;

    for (i = 0; i < 1000; i++)
      {
	if (m_mutex.try_lock ())
	  {
	    break;
	  }

	thread_sleep (10); /* 10 ms */
      }

    /* timeout */
    if (i == 1000)
      {
	er_log_debug (ARG_FILE_LINE, "could not stop coordinator");
	_exit (0);
      }

#if !defined (NDEBUG)
    m_mutex_holder = std::this_thread::get_id ();
#endif
  }

  bool pool::initialize_freelist (std::uint32_t max_connections)
  {
    freelist *node;
    std::size_t i;

    assert (m_mutex_holder == std::this_thread::get_id ());

    m_freelist.m_head = nullptr;
    m_freelist.m_claim = 0;
    m_freelist.m_max = static_cast<std::size_t> (static_cast<float> (max_connections) * /* margin */ 1.1);
    for (i = 0; i < m_freelist.m_max; i++)
      {
	node = new freelist (32 * 1024);
	if (!node->prepare ())
	  {
	    delete node;

	    return false;
	  }
	node->m_next = m_freelist.m_head;
	m_freelist.m_head = node;
      }

    return true;
  }

  void pool::finalize_freelist ()
  {
    freelist *head;

    assert (m_mutex_holder == std::this_thread::get_id ());
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

  std::uint32_t pool::initialize_topology (std::uint32_t max_connection_workers)
  {
    const auto &ctx = os::resources::cpu::effective ();

    if (ctx.adjusted_effective && !ctx.adjusted_effective->empty ())
      {
	std::vector<std::size_t> cores (
		ctx.adjusted_effective->begin (),
		std::next (ctx.adjusted_effective->begin (),
			   std::min (ctx.adjusted_effective->size (), static_cast<std::size_t> (max_connection_workers)))
	);
	os::resources::net::map_nic_to_index (cores);
      }
    return std::min (ctx.adjusted_max, static_cast<std::size_t> (max_connection_workers));
  }

  void pool::finalize_topology ()
  {
  }

  void pool::initialize_workers (std::uint32_t max_connection_workers, std::uint32_t min_connection_workers)
  {
    std::vector<std::size_t> cores;
    std::uint32_t i;
    const auto &ctx = os::resources::cpu::effective ();

    assert (m_mutex_holder == std::this_thread::get_id ());

    m_workers.reserve (max_connection_workers);

    if (ctx.adjusted_effective)
      {
	cores = *ctx.adjusted_effective;
      }
    else
      {
	std::vector<std::size_t> vec (ctx.adjusted_max);
	std::iota (vec.begin (), vec.end (), 0);
	cores = vec;
      }

    assert (cores.size () >= max_connection_workers);

    for (i = 0; i < max_connection_workers; i++)
      {
	m_workers.emplace_back (std::make_unique<worker> (this, m_coordinator, m_watcher, cores[i], i));
      }

    /* pre-warm the connection worker and its queue to avoid a race condition. */
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

    if (m_workers.empty ())
      {
	/* not initialized */
	return;
      }

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
    compelete = m_watcher->cv.wait_for (lock, wait_for, [this] { return m_watcher->active == 1; /* coordinator */ });
    lock.unlock ();
    if (!compelete)
      {
	er_log_debug (ARG_FILE_LINE, "could not stop all active connection workers");
	_exit (0);
      }
  }

  void pool::initialize_coordinator (std::uint32_t max_connection_workers, std::uint32_t min_connection_workers)
  {
    std::size_t core;
    const auto &ctx = os::resources::cpu::effective ();

    if (ctx.adjusted_effective)
      {
	core = (*ctx.adjusted_effective)[0];
      }
    else
      {
	core = 0;
      }

    m_coordinator = std::make_shared<coordinator> (
			    this,
			    m_watcher,
			    core,
			    max_connection_workers,
			    min_connection_workers
		    );
  }

  void pool::start_coordinator ()
  {
    coordinator::message request;

    request.type = coordinator::message_type::START;
    m_coordinator->enqueue (std::move (request));
    if (!m_coordinator->notify ())
      {
	assert_release (false);
      }
  }

  void pool::finalize_coordinator ()
  {
    std::chrono::system_clock::time_point deadline, now;
    std::chrono::microseconds wait_for (0);
    coordinator::message request;
    struct timeval *timeout;
    bool compelete;

    if (!m_coordinator)
      {
	/* not initialized */
	return;
      }

    request.type = coordinator::message_type::SHUTDOWN;
    m_coordinator->enqueue (std::move (request));
    if (!m_coordinator->notify ())
      {
	assert_release (false);
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
	er_log_debug (ARG_FILE_LINE, "could not stop coordinator");
	_exit (0);
      }
  }
}
