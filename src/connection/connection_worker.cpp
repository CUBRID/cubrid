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
#include <sys/epoll.h>

namespace cubconn
{
  connection_worker::context::context () :
    m_conn (nullptr)
  {
  }

  connection_worker::context::~context ()
  {
  }

  connection_worker::connection_worker (connection_pool *pool, std::size_t index) :
    m_stop (false),
    m_parent (),
    m_index (index)
  {
    context *ctx;

    m_eventfd = eventfd (0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (m_eventfd == -1)
      {
	_er_log_debug (__FILE__, __LINE__, "connection_worker: failed to create eventfd\n");
	assert_release (false);
      }
    ctx = new context;
    if (!ctx)
      {
	_er_log_debug (__FILE__, __LINE__, "connection_worker: failed to allocate memory\n");
	assert_release (false);
      }
    ctx->m_conn = reinterpret_cast<css_conn_entry *> (new int { m_eventfd });
    if (!ctx->m_conn)
      {
	_er_log_debug (__FILE__, __LINE__, "connection_worker: failed to allocate memory\n");
	assert_release (false);
      }
    if (!m_events.add_descriptor (m_eventfd, EPOLLET | EPOLLIN, ctx))
      {
	_er_log_debug (__FILE__, __LINE__, "connection_worker: add_descriptor failed\n");
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

  void connection_worker::enqueue (const message &item)
  {
    m_queue.enqueue (item);

    _er_log_debug (__FILE__, __LINE__, "enqueued request_type = %d to the worker index = %d\n", item.type, m_index);
  }

  void connection_worker::notify ()
  {
    std::uint64_t u;

    u = 1;
    ::write (m_eventfd, &u, sizeof (u));

    _er_log_debug (__FILE__, __LINE__, "reqeusted to wake up the worker index = %d\n", m_index);
  }

  bool connection_worker::handle_mq_new_client (message &item)
  {
    context *ctx;

    ctx = new context;
    if (!ctx)
      {
	_er_log_debug (__FILE__, __LINE__, "connection_worker->handle_mq_new_client: failed to allocate memory\n");
	return false;
      }
    ctx->m_conn = item.conn;
    if (!m_events.add_descriptor (ctx->m_conn->fd, EPOLLET | EPOLLIN, ctx))
      {
	_er_log_debug (__FILE__, __LINE__, "connection_worker->handle_mq_new_client: add_descriptor failed\n");
	return false;
      }

    _er_log_debug (__FILE__, __LINE__, "add new client that has fd = %d in the worker = %d\n", item.conn->fd, m_index);
    return true;
  }

  bool connection_worker::handle_message_queue ()
  {
    std::optional<message> request;
    uint64_t tmp;

    assert (!m_queue.empty ());

    ::read (m_eventfd, &tmp, 8);

    do
      {
	request = m_queue.dequeue ();
	if (!request)
	  {
	    break;
	  }

	_er_log_debug (__FILE__, __LINE__, "recevied request_type = %d from message queue in the worker = %d\n", request->type,
		       m_index);
	switch (request->type)
	  {
	  case message_type::NEW_CLIENT:
	    if (!this->handle_mq_new_client (*request))
	      {
		return false;
	      }
	    break;

	  case message_type::SHUTDOWN:
	    m_stop = true;
	    break;

	  default:
	    _er_log_debug (__FILE__, __LINE__,
			   "master_connector->handle_message_queue: received unknown event from eventfd in the worker = %d\n", m_index);
	    assert_release (false);
	    break;
	  }
      }
    while (true);

    return true;
  }

  bool connection_worker::handle_reception (context *ctx)
  {
    return true;
  }

  bool connection_worker::handle_transmission (context *ctx)
  {
    return true;
  }

  bool connection_worker::run ()
  {
    std::array<epoll_event, 128> events;
    context *ctx;
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
	    _er_log_debug (__FILE__, __LINE__, "master_connector->execute: m_events->wait failed: %s", strerror (errno));
	    assert_release (false);
	  }

	if (nfds == 0)
	  {
	    /* TODO: maybe heartbeat ? */
	  }

	assert (nfds > 0);

	for (i = 0; i < nfds; i++)
	  {
	    assert (events[i].data.ptr);

	    ctx = reinterpret_cast<context *> (events[i].data.ptr);
	    if (events[i].events & EPOLLIN)
	      {
		if (ctx->m_conn->fd == m_eventfd)
		  {
		    if (!this->handle_message_queue ())
		      {
			_er_log_debug (__FILE__, __LINE__, "connection_worker->run: handle_message_queue failed");
			return false;
		      }
		    continue;
		  }

		if (!this->handle_reception (ctx))
		  {
		    _er_log_debug (__FILE__, __LINE__, "connection_worker->run: handle_reception failed");
		    return false;
		  }
	      }
	    if (events[i].events & EPOLLOUT)
	      {
		if (!this->handle_transmission (ctx))
		  {
		    _er_log_debug (__FILE__, __LINE__, "connection_worker->run: handle_transmission failed");
		    return false;
		  }
	      }
	    if (events[i].events & (EPOLLHUP | EPOLLERR))
	      {
		_er_log_debug (__FILE__, __LINE__, "connection_worker->run: connection closed: %s", strerror (errno));
		return false;
	      }
	  }
      }

    return true;
  }
}
