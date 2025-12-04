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
 * coordinator.cpp
 */

#include "connection_pool.hpp"
#include "coordinator.hpp"
#include "connection_sr.h"

#include <unistd.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <sys/epoll.h>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#if 0
#define er_log_conn(...) er_log_debug (__VA_ARGS__)
#else
#define er_log_conn(...)
#endif

namespace cubconn::connection
{
  coordinator::coordinator (pool *pool, std::shared_ptr<thread_watcher> watcher, std::size_t core,
			    std::uint32_t max_worker, std::uint32_t min_worker) :
    m_parent (pool),
    m_watcher (watcher),
    m_core (core),
    m_stop (false),
    m_max_worker (max_worker),
    m_min_worker (min_worker)
  {
    /* notifier */
    m_eventfd = eventfd (0, EFD_NONBLOCK | EFD_CLOEXEC);
    m_timerfd = timerfd_create (CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (m_eventfd < 0 || m_timerfd < 0)
      {
	er_log_conn (__FILE__, __LINE__, "connection::coordinator: failed to create fd. %s\n", strerror (errno));
	assert_release (false);
      }

    if (!this->eventfd_register (m_eventfd) ||
	!this->eventfd_register (m_timerfd))
      {
	er_log_conn (__FILE__, __LINE__, "connection::coordinator: failed to register fd\n");
	assert_release (false);
      }

    if (!this->eventfd_settimer (m_timerfd, 1 /* 1 sec */, 0))
      {
	er_log_conn (__FILE__, __LINE__, "connection::coordinator: failed to eventfd_settimer\n");
	assert_release (false);
      }

    /* request queue */
    m_queue_size.store (0, std::memory_order_relaxed);

    m_thread = std::thread (&coordinator::attach, this);
  }

  coordinator::~coordinator ()
  {
    if (m_thread.joinable ())
      {
	m_thread.join ();
      }
    ::close (m_eventfd);
    ::close (m_timerfd);
  }

  void coordinator::enqueue (message &&item)
  {
    m_queue.push (std::move (item));
    m_queue_size.fetch_add (1, std::memory_order_release);
  }

  bool coordinator::notify ()
  {
    std::uint64_t u;
    ssize_t bytes;

    u = 1;
    while (true)
      {
	bytes = ::write (m_eventfd, &u, sizeof (u));
	if (bytes == sizeof (u))
	  {
	    break;
	  }

	if (bytes == 0 || (bytes > 0 && static_cast<unsigned long> (bytes) < sizeof (u)))
	  {
	    return false;
	  }

	assert (bytes < 0);

	if (errno == EINTR)
	  {
	    continue;
	  }
	if (errno == EAGAIN)
	  {
	    break;
	  }
	return false;
      }

    return true;
  }

  bool coordinator::eventfd_register (int fd)
  {
    if (!m_events.add_descriptor (fd, EPOLLET | EPOLLIN))
      {
	er_log_conn (__FILE__, __LINE__, "connection::coordinator->eventfd_register: add_descriptor failed\n");

	return false;
      }

    return true;
  }

  bool coordinator::eventfd_clear (int fd)
  {
    ssize_t bytes;
    uint64_t u;

    /* read counter */
    while (true)
      {
	bytes = ::read (fd, &u, sizeof (u));
	if (bytes == sizeof (u))
	  {
	    break;
	  }

	if (bytes == 0 || (bytes > 0 && static_cast<unsigned long> (bytes) < sizeof (u)))
	  {
	    return false;
	  }

	assert (bytes < 0);

	if (errno == EINTR)
	  {
	    continue;
	  }
	if (errno == EAGAIN)
	  {
	    break;
	  }
	return false;
      }
    return true;
  }

  bool coordinator::eventfd_settimer (int fd, uint32_t sec, uint64_t nsec)
  {
    struct itimerspec its;

    memset (&its, 0, sizeof (its));
    its.it_value.tv_sec = sec;
    its.it_value.tv_nsec = nsec;
    its.it_interval.tv_sec = sec;
    its.it_interval.tv_nsec = nsec;

    if (timerfd_settime (fd, 0, &its, NULL) < 0)
      {
	er_log_conn (__FILE__, __LINE__, "connection::coordinator->eventfd_settimer: %s\n", strerror (errno));
	return false;
      }

    return true;
  }

  bool coordinator::handle_message_queue_new_client (message &item)
  {
    std::vector<std::unique_ptr<worker>> &workers = m_parent->get_workers ();
    connection::worker::message request;

    request.type = connection::worker::message_type::NEW_CLIENT;
    request.ctx = m_parent->claim_context ();
    request.conn = item.conn;

    static std::size_t counter = 0;
    workers[counter]->enqueue (cubconn::connection::worker::queue_type::IMMEDIATE, std::move (request));
    if (!workers[counter]->notify ())
      {
	assert_release (false);
      }

    counter++;
    if (counter == workers.size ())
      {
	counter = 0;
      }

    return true;
  }

  bool coordinator::handle_message_queue_return_to_pool (message &item)
  {
    for (context *ctx : item.resource)
      {
	/* release the conneciton */
	css_free_conn (ctx->m_conn);
	m_parent->retire_context (ctx);
      }

    return true;
  }

  bool coordinator::handle_message_queue ()
  {
    message request;
    uint64_t size, i;

    i = 0;
    size = m_queue_size.exchange (0, std::memory_order_acquire);
    while (i++ < size && m_queue.try_pop (request))
      {
	switch (request.type)
	  {
	  case message_type::START:
	    break;

	  case message_type::NEW_CLIENT:
	    this->handle_message_queue_new_client (request);
	    break;

	  case message_type::RETURN_TO_POOL:
	    this->handle_message_queue_return_to_pool (request);
	    break;

	  case message_type::SHUTDOWN:
	    m_stop = true;
	    break;

	  default:
	    er_log_conn (__FILE__, __LINE__,
			 "connection::coordinator->handle_message_queue: received unknown event from eventfd\n");
	    assert_release (false);
	    break;
	  }
      }

    return true;
  }

  void coordinator::initialize ()
  {
    /* watch me */
    m_watcher->mtx.lock ();
    m_watcher->active++;
    m_watcher->mtx.unlock ();

    m_parent->lock_resource ();
  }

  void coordinator::finalize ()
  {
    m_parent->release_resource ();

    /* remove the watcher */
    m_watcher->mtx.lock ();
    m_watcher->active--;
    m_watcher->mtx.unlock ();

    m_watcher->cv.notify_one ();
  }

  bool coordinator::run ()
  {
    std::array<epoll_event, 4> events;
    int nfds, i;

    while (!m_stop)
      {
	nfds = m_events.wait (events.data (), events.size (), TIMEOUT_INFINITE);
	if (nfds < 0)
	  {
	    if (errno == EINTR)
	      {
		continue;
	      }
	    er_log_conn (__FILE__, __LINE__, "connection::coordinator->run: m_events->wait failed: %s", strerror (errno));
	    assert_release (false);
	    continue;
	  }

	for (i = 0; i < nfds; i++)
	  {
	    assert (events[i].data.fd > 0);

	    if (events[i].events & EPOLLIN)
	      {
		if (events[i].data.fd == m_eventfd)
		  {
		    this->handle_message_queue ();
		  }
		else if (events[i].data.fd == m_timerfd)
		  {
		  }
	      }
	  }
      }

    return true;
  }

  void coordinator::attach ()
  {
    this->initialize ();
    this->run ();
    this->finalize ();
  }
}
