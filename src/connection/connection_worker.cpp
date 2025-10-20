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

#include "log_manager.h"
#include "hardware_topology.hpp"
#include "network.h"
#include "network_interface_sr.h"
#include "server_support.h"
#include "connection_sr.h"
#include "connection_defs.h"
#include "connection_worker.hpp"
#include "buffer.hpp"
#include "error_manager.h"

#include <atomic>
#include <iostream>
#include <chrono>
#include <array>
#include <thread>
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

#if !defined (NDEBUG)
std::atomic<uint64_t> message_counter (0);
#endif

#define NEXT_STATE(ctx, sel, x) do { \
    er_log_conn (__FILE__, __LINE__, "fd = %d, set state = %d\n", ctx->m_conn ? ctx->m_conn->fd : -1, state::x); \
    (ctx->sel.m_state = state::x); \
} while (0)

namespace cubconn
{
  connection_worker::context::context (std::size_t capacity, connection_stats *stats) :
    m_conn (nullptr),
    m_ignore (ignore_level::DONT_IGNORE),
    m_removed (false),
    m_recv
  {
    .m_state = state::HEADER,
    .m_receiver = receiver (capacity, stats),
    .m_header = { nullptr, 0 },
    .m_request_id = -1,
    .m_command = false
  },
  m_send
  {
    .m_transmitter = transmitter (stats)
  }
  {
  }

  connection_worker::context::context () :
    m_conn (nullptr),
    m_recv
  {
    .m_state = state::HEADER,
    .m_receiver = receiver (),
    .m_header = { nullptr, 0 },
    .m_request_id = -1,
    .m_command = false
  },
  m_send
  {
    .m_transmitter = transmitter ()
  }
  {
  }

  connection_worker::context::~context ()
  {
  }

  connection_worker::connection_worker (connection_pool *pool, std::size_t core, std::size_t index) :
    m_parent (pool),
    m_core (core),
    m_stop (false),
    m_entry (nullptr),
    m_index (index)
  {
    std::size_t i;

    /* notifier */
    m_eventfd = eventfd (0, EFD_NONBLOCK | EFD_CLOEXEC);
    m_timerfd = timerfd_create (CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (m_eventfd < 0 || m_timerfd < 0)
      {
	er_log_conn (__FILE__, __LINE__, "connection_worker: failed to create fd. %s\n", strerror (errno));
	assert_release (false);
      }

    if (!this->register_eventfd (m_eventfd) ||
	!this->register_eventfd (m_timerfd))
      {
	er_log_conn (__FILE__, __LINE__, "connection_worker: failed to register fd\n");
	assert_release (false);
      }

    /* request queue */
    for (i = 0; i < static_cast<std::size_t> (queue_type::TYPE_COUNT); i++)
      {
	m_queue_size[i].store (0, std::memory_order_relaxed);
      }

    m_thread = std::thread (&connection_worker::attach, this);
  }

  connection_worker::~connection_worker ()
  {
    if (m_thread.joinable ())
      {
	m_thread.join ();
      }
    ::close (m_eventfd);

    assert (m_context.size () == 0);
  }

  void connection_worker::enqueue (queue_type type, message &&item)
  {
    assert ((item.conn ? (item.conn->fd != -1) : false) || item.type == message_type::SHUTDOWN);

#if !defined (NDEBUG)
    item.message_id = message_counter++;
#endif

    m_queue[static_cast<std::size_t> (type)].push (std::move (item));
    m_queue_size[static_cast<std::size_t> (type)].fetch_add (1, std::memory_order_release);

#if !defined (NDEBUG)
    er_log_conn (__FILE__, __LINE__,
		 "enqueued message_id = %lld, request_type = %d to the worker index = %d, queue_type = %d\n", item.message_id,
		 item.type, m_index, type);
#endif
  }

  bool connection_worker::notify ()
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

    er_log_conn (__FILE__, __LINE__, "requested to wake up the worker index = %d\n", m_index);
    return true;
  }

  void connection_worker::stats ()
  {
    int i;

    std::cout << "connection worker: " << m_index << std::endl;
    for (i = 0; i < stats::STATS_COUNT; i++)
      {
	std::cout << "  " << stats_name[i] << ": " << m_stats.get (static_cast<enum stats> (i)) << std::endl;
      }
    std::cout << "-------------- clients --------------" << std::endl;
    for (auto &ctx : m_context)
      {
	std::cout << "  fd: " << ctx->m_conn->fd << std::endl;
	/* whenever whenever whenever ... */
	std::cout << std::endl;
      }
    std::cout << std::endl;
    std::cout << std::endl;
  }

  bool connection_worker::register_eventfd (int fd)
  {
    context *ctx;
    css_conn_entry *conn;

    ctx = new context ();
    conn = reinterpret_cast<css_conn_entry *> (new int { fd });
    if (!ctx || !conn)
      {
	er_log_conn (__FILE__, __LINE__, "connection_worker->register_eventfd: failed to allocate memory\n");
	return false;
      }
    ctx->m_conn = conn;

    if (!m_events.add_descriptor (m_eventfd, EPOLLET | EPOLLIN, ctx))
      {
	er_log_conn (__FILE__, __LINE__, "connection_worker->register_eventfd: add_descriptor failed\n");

	delete ctx;
	delete conn;
	return false;
      }

    return true;
  }

  void connection_worker::push_task_into_worker_pool (context *ctx)
  {
    /* push new task into worker pool */
    css_push_server_task (*ctx->m_conn);
  }

  bool connection_worker::is_wait_required (context *ctx)
  {
    if (ctx->m_conn->fd == cdc_Gl.conn.fd)
      {
	return false;
      }

    return true;
  }

  bool connection_worker::has_remaining_tasks (context *ctx)
  {
    /* handle the entries in the message queue as there may be a queued request to release the memory in ctx */
    this->handle_message_queue_by_index (queue_type::IMMEDIATE);

    if (ctx->m_ignore < ignore_level::IGNORE_ALL && !ctx->m_send.m_transmitter.empty ())
      {
	er_log_conn (ARG_FILE_LINE,
		     "connection_worker->handle_connection_close: retry shutdown (conn %p): send buffer not empty\n", ctx->m_conn);
	return true;
      }

    return false;
  }

  std::pair<int, int> connection_worker::start_connection_close (context *ctx)
  {
    css_conn_entry *conn;
    int tran_index, client_id;

    conn = ctx->m_conn;
    tran_index = conn->get_tran_index ();
    if (tran_index == NULL_TRAN_INDEX)
      {
	/* the connected client does not yet finished boot_client_register */
	/* retry */
	return { -1, -1 };
      }
    client_id = conn->client_id;
    m_entry->conn_entry = ctx->m_conn;
    css_set_thread_info (m_entry, client_id, 0, tran_index, NET_SERVER_SHUTDOWN);

    return { tran_index, client_id };
  }

  void connection_worker::end_connection_close ()
  {
    pthread_mutex_lock (&m_entry->tran_index_lock);

    css_set_thread_info (m_entry, -1, 0, -1, -1);
    m_entry->conn_entry = NULL;
    m_entry->m_status = cubthread::entry::status::TS_RUN;

    pthread_mutex_unlock (&m_entry->tran_index_lock);
  }

  bool connection_worker::handle_connection_close (context *ctx)
  {
    std::chrono::time_point<std::chrono::steady_clock> start, end;
    message request;
    int tran_index, client_id;
    int status;

    if (ctx->m_removed)
      {
	er_log_conn (__FILE__, __LINE__, "connection_worker->handle_connection_close: already removed context\n");
	return true;
      }

    assert_release (ctx->m_conn);

    /* change status */

    rmutex_lock (m_entry, &ctx->m_conn->rmutex);
    if (ctx->m_conn->status == CONN_OPEN)
      {
	ctx->m_conn->status = CONN_CLOSING;
      }
    status = ctx->m_conn->status;
    rmutex_unlock (m_entry, &ctx->m_conn->rmutex);

    /* get tran index and client id */

    /* the thread_p is only accessible from this connection thread (myself), but  */
    /* the entry may be accessed through unknown path because it is in the thread */
    /* manager's entry list. */
    pthread_mutex_lock (&m_entry->tran_index_lock);

    std::tie (tran_index, client_id) = this->start_connection_close (ctx);
    if (tran_index < 0 && client_id < 0)
      {
	if (this->is_wait_required (ctx))
	  {
	    er_log_conn (__FILE__, __LINE__, "connection_worker->handle_connection_close: wait for transaction index\n");

	    /* the connected client does not yet finished boot_client_register */
	    /* this case is unusual */
	    /* DO NOT RETRY. retrying may result in duplicate shutdown client requests */
	    thread_sleep (50);

	    tran_index = ctx->m_conn->get_tran_index ();
	    client_id = ctx->m_conn->client_id;
	  }
      }

    pthread_mutex_unlock (&m_entry->tran_index_lock);

    /* stop the sessions associated with conn */

    css_end_server_request (ctx->m_conn);
    /* avoid infinite waiting with xtran_wait_server_active_trans() */
    m_entry->m_status = cubthread::entry::status::TS_CHECK;
    if (ctx->m_conn->session_p != NULL)
      {
	ssession_stop_attached_threads (m_entry, ctx->m_conn->session_p);
      }

    /* interrupt and wake up */

    net_server_wakeup_workers (m_entry, tran_index, client_id);

    /* retry until the worker related to the connection is complete */

    if (net_server_active_workers (m_entry, ctx->m_conn, tran_index, client_id) > 0)
      {
	er_log_conn (__FILE__, __LINE__, "connection_worker->handle_connection_close: net_server_active_workers\n");
	goto retry;
      }

    /* check if there is any remaining task */

    if (this->has_remaining_tasks (ctx))
      {
	er_log_conn (__FILE__, __LINE__, "connection_worker->handle_connection_close: has_remaining_tasks\n");
	goto retry;
      }

    /* this context has no remaining tasks */

    _er_log_debug (ARG_FILE_LINE,
		   "handle_connection_close: conn %p { fd %d status %d transaction_id %d db_error %d stop_talk %d stop_phase %d }\n",
		   ctx->m_conn, ctx->m_conn->fd, status, tran_index, ctx->m_conn->db_error, ctx->m_conn->stop_talk,
		   ctx->m_conn->stop_phase);

    /* remove and close */

    m_events.remove_descriptor (ctx->m_conn->fd);
    if (tran_index != NULL_TRAN_INDEX)
      {
	net_server_conn_down (m_entry, tran_index);
      }

    this->end_connection_close ();

    /* clear resource */

    ctx->m_send.m_transmitter.clear ();

    start = std::chrono::steady_clock::now ();

    rmutex_lock (m_entry, &ctx->m_conn->cmutex);

    ctx->m_conn->worker = nullptr;
    ctx->m_conn->context = nullptr;

    rmutex_unlock (m_entry, &ctx->m_conn->cmutex);

    end = std::chrono::steady_clock::now ();
    m_stats.add (stats::BLOCKED_RMUTEX, std::chrono::duration_cast<std::chrono::microseconds> (end - start).count ());

    /* release the conneciton */
    css_free_conn (ctx->m_conn);
    /* mark deleted and lazily release this */
    ctx->m_removed = true;
    m_removed_context.push_back (ctx);

    m_stats.sub (stats::NET_CLIENTS, 1);

    return true;

retry:
    er_log_conn (__FILE__, __LINE__, "connection_worker->handle_connection_close: retry fd = %d, ignore = %d, conn = %p\n",
		 ctx->m_conn ? ctx->m_conn->fd : -1, ctx->m_ignore, ctx->m_conn);

    this->end_connection_close ();

    request.type = message_type::SHUTDOWN_CLIENT;
    request.conn = ctx->m_conn;
    request.ignore = ctx->m_ignore;

    /* this request must be handled as razily */
    this->enqueue (queue_type::LAZY, std::move (request));
    /* rezily notified */
    m_notified = true;

    return true;
  }

  bool connection_worker::clear_event ()
  {
    ssize_t bytes;
    uint64_t u;

    /* read counter */
    while (true)
      {
	bytes = ::read (m_eventfd, &u, sizeof (u));
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

  bool connection_worker::handle_message_queue_send_packet (message &item)
  {
    context *ctx;
    result status;
    int r;

    assert (item.conn);

    r = rmutex_lock (m_entry, &item.conn->cmutex);
    assert (r == NO_ERROR);

    ctx = reinterpret_cast<context *> (item.conn->context);
    if (ctx == nullptr)
      {
	r = rmutex_unlock (m_entry, &item.conn->cmutex);
	assert (r == NO_ERROR);

	if (item.deleter)
	  {
	    item.deleter ();
	  }

#if !defined (NDEBUG)
	er_log_conn (__FILE__, __LINE__,
		     "connection_worker->handle_message_queue_send_packet: message_id = %lld, context is already cleared for conn = %p\n",
		     item.message_id, static_cast<void *> (item.conn));
#endif

	return true;
      }

#if !defined (NDEBUG)
    er_log_conn (__FILE__, __LINE__, "new packet to send. message_id = %lld, fd = %d in the worker = %d\n", item.message_id,
		 item.conn->fd, m_index);
#endif

    for (auto &packet : item.packet)
      {
	ctx->m_send.m_transmitter.push_for_send ({ packet.data (), packet.size () });
      }
    ctx->m_send.m_transmitter.stamp ();
    ctx->m_send.m_transmitter.push_for_deleter (std::move (item.deleter));

    /* first, try to send the packets */
    status = ctx->m_send.m_transmitter.fill (ctx->m_conn->fd);
    if (status == result::PeerReset || status == result::Error)
      {
	r = rmutex_unlock (m_entry, &item.conn->cmutex);
	assert (r == NO_ERROR);

	/* ctx will be forcibly removed */
	ctx->m_ignore = ignore_level::IGNORE_ALL;

	/* this connection will be removed by main loop */
	return true;
      }

    assert (status == result::Ok || status == result::Pending);

    if (status == result::Ok)
      {
	ctx->m_send.m_transmitter.clear ();
#if !defined (NDEBUG)
	er_log_conn (__FILE__, __LINE__, "fully sent. message_id = %lld, fd = %d in the worker = %d\n", item.message_id,
		     ctx->m_conn->fd, m_index);
#endif

	r = rmutex_unlock (m_entry, &item.conn->cmutex);
	assert (r == NO_ERROR);

	return true;
      }

    /* if buffer is full, register the fd to epoll loop and wait to send the others */
    if (!m_events.modify_descriptor (ctx->m_conn->fd, EPOLLET | EPOLLIN | EPOLLOUT | EPOLLRDHUP, ctx))
      {
	r = rmutex_unlock (m_entry, &item.conn->cmutex);
	assert (r == NO_ERROR);

	er_log_conn (__FILE__, __LINE__, "connection_worker->handle_message_queue_send_packet: modify_descriptor failed\n");
	return false;
      }

    r = rmutex_unlock (m_entry, &item.conn->cmutex);
    assert (r == NO_ERROR);

    return true;
  }

  bool connection_worker::handle_message_queue_release_packet (message &item)
  {
    context *ctx;
    int r;

    assert (item.conn);
    assert (item.packet.size () > 0);

    r = rmutex_lock (m_entry, &item.conn->cmutex);
    assert (r == NO_ERROR);

    ctx = reinterpret_cast<context *> (item.conn->context);
    if (ctx == nullptr)
      {
	r = rmutex_unlock (m_entry, &item.conn->cmutex);
	assert (r == NO_ERROR);

	er_log_conn (__FILE__, __LINE__,
		     "connection_worker->handle_message_queue_release_packet: context is already cleared for conn = %p\n",
		     static_cast<void *> (item.conn));
	return true;
      }

    for (cubbase::span<std::byte> &packet : item.packet)
      {
	ctx->m_recv.m_receiver.release (packet.data ());
	er_log_conn (__FILE__, __LINE__,
		     "connection_worker->handle_message_queue_release_packet: release packet pointer = %p\n", packet.data ());
      }

    r = rmutex_unlock (m_entry, &item.conn->cmutex);
    assert (r == NO_ERROR);

    return true;
  }

  bool connection_worker::handle_message_queue_new_client (message &item)
  {
    context *ctx;

    ctx = new context (32 * 1024, &m_stats);
    if (!ctx)
      {
	er_log_conn (__FILE__, __LINE__, "connection_worker->handle_message_queue_new_client: failed to allocate memory\n");
	return false;
      }
    ctx->m_conn = item.conn;
    /* there is no need to hold the mutex now */
    ctx->m_conn->worker = this;
    ctx->m_conn->context = ctx;
    if (!m_events.add_descriptor (ctx->m_conn->fd, EPOLLET | EPOLLIN | EPOLLRDHUP, ctx))
      {
	ctx->m_conn->worker = nullptr;
	ctx->m_conn->context = nullptr;
	delete ctx;
	er_log_conn (__FILE__, __LINE__, "connection_worker->handle_message_queue_new_client: add_descriptor failed\n");
	return false;
      }
    if (!m_context.insert (ctx).second)
      {
	m_events.remove_descriptor (ctx->m_conn->fd);
	ctx->m_conn->worker = nullptr;
	ctx->m_conn->context = nullptr;
	delete ctx;
	er_log_conn (__FILE__, __LINE__,
		     "connection_worker->handle_message_queue_new_client: context can not be duplicated\n");
	return false;
      }
    er_log_conn (__FILE__, __LINE__, "add new client that has fd = %d in the worker = %d\n", item.conn->fd, m_index);
    return true;
  }

  bool connection_worker::handle_message_queue_shutdown_client (message &item)
  {
    context *ctx;
    int r;

    assert (item.conn);

    r = rmutex_lock (m_entry, &item.conn->cmutex);
    assert (r == NO_ERROR);

    ctx = reinterpret_cast<context *> (item.conn->context);
    if (ctx == nullptr)
      {
	r = rmutex_unlock (m_entry, &item.conn->cmutex);
	assert (r == NO_ERROR);

	er_log_conn (__FILE__, __LINE__,
		     "connection_worker->handle_message_queue_shutdown_client: context is already cleared for conn = %p\n",
		     static_cast<void *> (item.conn));
	return true;
      }

    ctx->m_ignore = item.ignore;

    r = rmutex_unlock (m_entry, &item.conn->cmutex);
    assert (r == NO_ERROR);

    this->handle_connection_close (ctx);

    return true;
  }

  bool connection_worker::handle_message_queue_by_index (queue_type type)
  {
    message request;
    uint64_t size, i;

    i = 0;
    size = m_queue_size[static_cast<std::size_t> (type)].exchange (0, std::memory_order_acquire);
    while (i++ < size && m_queue[static_cast<std::size_t> (type)].try_pop (request))
      {
#if !defined (NDEBUG)
	er_log_conn (__FILE__, __LINE__,
		     "recevied message_id = %lld, request_type = %d from message queue in the worker = %d\n", request.message_id,
		     request.type, m_index);
#endif

	switch (request.type)
	  {
	  case message_type::NEW_CLIENT:
	    m_stats.add (stats::MQ_NEW_CLIENT, 1);
	    if (!this->handle_message_queue_new_client (request))
	      {
		return false;
	      }
	    m_stats.add (stats::NET_CLIENTS, 1);
	    break;

	  case message_type::SHUTDOWN_CLIENT:
	    m_stats.add (stats::MQ_SHUTDOWN_CLIENT, 1);
	    if (!this->handle_message_queue_shutdown_client (request))
	      {
		return false;
	      }
	    break;

	  case message_type::SEND_PACKET:
	    m_stats.add (stats::MQ_SEND_PACKET, 1);
	    if (!this->handle_message_queue_send_packet (request))
	      {
		return false;
	      }
	    break;

	  case message_type::RELEASE_PACKET:
	    m_stats.add (stats::MQ_RELEASE_PACKET, 1);
	    if (!this->handle_message_queue_release_packet (request))
	      {
		return false;
	      }
	    break;

	  case message_type::SHUTDOWN:
	    m_stop = true;
	    break;

	  default:
	    er_log_conn (__FILE__, __LINE__,
			 "master_connector->handle_message_queue: received unknown event from eventfd in the worker = %d\n", m_index);
	    assert_release (false);
	    break;
	  }
      }

    return true;
  }

  bool connection_worker::handle_message_queue ()
  {
    std::size_t i;

    if (!this->clear_event ())
      {
	return false;
      }

    m_stats.add (stats::MQ_REQUESTED, 1);

    for (i = 0; i < static_cast<std::size_t> (queue_type::TYPE_COUNT); i++)
      {
	if (!this->handle_message_queue_by_index (static_cast<queue_type> (i)))
	  {
	    return false;
	  }
      }

    /* the actual release of the context is handled last */
    for (auto &ctx : m_removed_context)
      {
	if (m_context.erase (ctx) == 0)
	  {
	    er_log_conn (__FILE__, __LINE__, "connection_worker->handle_message_queue: context not found\n");
	    continue;
	  }
	delete ctx;
      }
    m_removed_context.clear ();

    return true;
  }

  void connection_worker::handle_hangup_or_error (context *ctx, bool err)
  {
    socklen_t length;
    int error;

    if (err)
      {
	error = 0;
	length = sizeof (error);
	if (getsockopt (ctx->m_conn->fd, SOL_SOCKET, SO_ERROR, &error, &length) == 0)
	  {
	    er_log_conn (__FILE__, __LINE__, "connection_worker->handle_hangup_or_error: socket error (EPOLLERR) on fd %d: %s",
			 ctx->m_conn->fd,
			 strerror (error));
	  }
	else
	  {
	    er_log_conn (__FILE__, __LINE__,
			 "connection_worker->handle_hangup_or_error: socket error (EPOLLERR) on fd %d, but getsockopt failed.",
			 ctx->m_conn->fd);
	  }
      }
    else
      {
	er_log_conn (__FILE__, __LINE__,
		     "connection_worker->handle_hangup_or_error: connection closed by peer (HUP/RDHUP) on fd %d.",
		     ctx->m_conn->fd);
      }

    /* ctx will be forcibly removed */
    ctx->m_ignore = ignore_level::IGNORE_ALL;

    this->handle_connection_close (ctx);
  }

  result connection_worker::handle_error_packet (context *ctx, cubbase::span<std::byte> &packet)
  {
    css_conn_entry *conn;
    css_error_code error;
    NET_HEADER *header;
    int size;

    assert (ctx->m_recv.m_header.size () == sizeof (NET_HEADER));

    conn = ctx->m_conn;
    header = reinterpret_cast<NET_HEADER *> (ctx->m_recv.m_header.data ());

    size = ntohl (header->buffer_size);
    if (packet.size () != static_cast<std::size_t> (size) && packet.size () != ((static_cast<std::size_t> (size) + 7) & ~7))
      {
	er_log_conn (__FILE__, __LINE__,
		     "connection_worker->handle_error_packet: the expected size by header and packet size is different\n");
	return result::Skewed;
      }

    if (!css_is_request_aborted (conn, ctx->m_recv.m_request_id))
      {
	error = css_add_queue_entry (conn, &conn->error_queue, ctx->m_recv.m_request_id,
				     reinterpret_cast<char *> (packet.data ()),
				     packet.size (), NO_ERRORS, conn->get_tran_index (), conn->invalidate_snapshot, conn->db_error);
	if (error != NO_ERRORS)
	  {
	    ctx->m_recv.m_receiver.release (packet.data ());
	    return result::Error;
	  }
      }
    else
      {
	ctx->m_recv.m_receiver.release (packet.data ());
      }
    ctx->m_recv.m_command = false;
    NEXT_STATE (ctx, m_recv, HEADER);
    return result::Ok;
  }

  result connection_worker::handle_data_packet (context *ctx, cubbase::span<std::byte> &packet)
  {
    THREAD_ENTRY *waiter_thread;
    css_wait_queue_entry *waiter;
    css_conn_entry *conn;
    css_error_code error;
    NET_HEADER *header;
    int size;

    assert (ctx->m_recv.m_header.size () == sizeof (NET_HEADER));

    conn = ctx->m_conn;
    header = reinterpret_cast<NET_HEADER *> (ctx->m_recv.m_header.data ());

    size = ntohl (header->buffer_size);
    if (packet.size () != static_cast<std::size_t> (size) && packet.size () != ((static_cast<std::size_t> (size) + 7) & ~7))
      {
	er_log_conn (__FILE__, __LINE__,
		     "connection_worker->handle_data_packet: the expected size by header and packet size is different\n");
	return result::Skewed;
      }

    /* check if there is thread waiting for data */
    waiter_thread = NULL;
    waiter = css_find_and_remove_wait_queue_entry (&conn->data_wait_queue, ctx->m_recv.m_request_id);
    if (waiter != NULL)
      {
	waiter_thread = waiter->thrd_entry;
	waiter_thread->next_wait_thrd = NULL;
      }

    if (!css_is_request_aborted (conn, ctx->m_recv.m_request_id))
      {
	if (waiter)
	  {
	    *waiter->buffer = reinterpret_cast<char *> (packet.data ());
	    *waiter->size = packet.size ();
	    *waiter->rc = NO_ERRORS;
	    waiter->thrd_entry = NULL;
	    css_free_wait_queue_entry (conn, waiter);
	  }
	else
	  {
	    /* if waiter not exists, add to data queue */
	    error = css_add_queue_entry (conn, &conn->data_queue, ctx->m_recv.m_request_id,
					 reinterpret_cast<char *> (packet.data ()),
					 packet.size (), NO_ERRORS, conn->get_tran_index (), conn->invalidate_snapshot, conn->db_error);
	    if (error != NO_ERRORS)
	      {
		ctx->m_recv.m_receiver.release (packet.data ());
		return result::Error;
	      }
	  }
      }
    else
      {
	if (waiter)
	  {
	    *waiter->buffer = NULL;
	    *waiter->size = 0;
	    *waiter->rc = SERVER_ABORTED;
	  }
      }

    if (waiter_thread)
      {
	thread_lock_entry (waiter_thread);

	assert (waiter_thread->resume_status == THREAD_CSS_QUEUE_SUSPENDED
		|| waiter_thread->resume_status == THREAD_CSECT_WRITER_SUSPENDED);
	assert (waiter_thread->next_wait_thrd == NULL);

	/* When the resume_status is THREAD_CSS_QUEUE_SUSPENDED, it means the data waiting thread is still waiting on the
	 * data queue. Otherwise, in case of THREAD_CSECT_WRITER_SUSPENDED, it means that the thread was timed out, is
	 * trying to clear its queue buffer (see clear_wait_queue_entry_and_free_buffer function), and waiting for its
	 * conn->csect. We don't need to wakeup the thread for this case. We may send useless signal for it, but it may
	 * bring other anomalies: the thread may sleep on another resources which we don't know at this moment. */
	if (waiter_thread->resume_status == THREAD_CSS_QUEUE_SUSPENDED)
	  {
	    thread_wakeup_already_had_mutex (waiter_thread, THREAD_CSS_QUEUE_RESUMED);
	  }

	thread_unlock_entry (waiter_thread);
      }

    if (ctx->m_recv.m_command)
      {
	this->push_task_into_worker_pool (ctx);
	ctx->m_recv.m_command = false;
      }

    NEXT_STATE (ctx, m_recv, HEADER);
    return result::Ok;
  }

  result connection_worker::handle_command_header_packet (context *ctx)
  {
    css_conn_entry *conn;
    NET_HEADER *header;
    css_error_code error;

    if (css_is_request_aborted (ctx->m_conn, ctx->m_recv.m_request_id))
      {
	ctx->m_recv.m_receiver.release (ctx->m_recv.m_header.data ());
	return result::Aborted;
      }

    assert (ctx->m_recv.m_header.size () == sizeof (NET_HEADER));

    conn = ctx->m_conn;
    header = reinterpret_cast<NET_HEADER *> (ctx->m_recv.m_header.data ());

    error = css_add_queue_entry (conn, &conn->request_queue, ctx->m_recv.m_request_id,
				 reinterpret_cast<char *> (ctx->m_recv.m_header.data ()), ctx->m_recv.m_header.size (), NO_ERRORS,
				 conn->get_tran_index (), conn->invalidate_snapshot, conn->db_error);
    if (error != NO_ERRORS)
      {
	ctx->m_recv.m_receiver.release (ctx->m_recv.m_header.data ());
	return result::Error;
      }

    if (ntohl (header->buffer_size) > 0)
      {
	/* data packet will be received belongs to this command */
	ctx->m_recv.m_command = true;
      }
    else
      {
	/* there is a request without no data following.		    */
	/* e.g. NET_SERVER_LOG_CHECKPOINT, NET_SERVER_TM_SERVER_ABORT.  */
	this->push_task_into_worker_pool (ctx);
      }

    return result::Ok;
  }

  result connection_worker::handle_header_packet (context *ctx, cubbase::span<std::byte> &packet)
  {
    css_conn_entry *conn;
    NET_HEADER *header;
    unsigned short flags;
    result status;

    assert (ctx->m_conn);

    if (packet.size () != sizeof (NET_HEADER))
      {
	/* 1. the state was wrong or				      */
	/* 2. the incoming packet was wrong			      */
	/* in this case, we must reset the context and drain all data */
	/* from the socket to recover this state machine and handle   */
	/* the next request properly.				      */
	er_log_conn (__FILE__, __LINE__,
		     "connection_worker->handle_header_packet: the expected size, sizeof (NET_HEADER) and packet size is different\n");
	return result::Skewed;
      }

    ctx->m_recv.m_header = packet;

    conn = ctx->m_conn;
    header = reinterpret_cast<NET_HEADER *> (ctx->m_recv.m_header.data ());

    ctx->m_recv.m_request_id = ntohl (header->request_id);

    if (conn->stop_talk)
      {
	return result::ClosedConnection;
      }

    conn->set_tran_index (ntohl (header->transaction_id));
    conn->db_error = (int) ntohl (header->db_error);
    flags = ntohs (header->flags);
    conn->invalidate_snapshot = flags & NET_HEADER_FLAG_INVALIDATE_SNAPSHOT ? 1 : 0;
    conn->in_method = flags & NET_HEADER_FLAG_METHOD_MODE ? true : false;

    status = result::Ok;
    switch (ntohl (header->type))
      {
      case COMMAND_TYPE:
	/* no more packets are requested */
	status = this->handle_command_header_packet (ctx);
	break;

      case DATA_TYPE:
	ctx->m_recv.m_receiver.release (packet.data ());
	NEXT_STATE (ctx, m_recv, DATA);
	break;

      case ABORT_TYPE:
	/* no more packets are requested */
	ctx->m_recv.m_receiver.release (packet.data ());
	ctx->m_recv.m_command = false;
	css_process_abort_packet (ctx->m_conn, ctx->m_recv.m_request_id);
	break;

      case CLOSE_TYPE:
	ctx->m_recv.m_receiver.release (packet.data ());
	/* no more packets are requested */
	status = result::ClosedConnection;
	break;

      case ERROR_TYPE:
	ctx->m_recv.m_receiver.release (packet.data ());
	NEXT_STATE (ctx, m_recv, ERROR);
	break;

      default:
	er_log_conn (ARG_FILE_LINE,
		     "connection_worker->handle_header_packet: unknown state - will be reset by skew handler\n");
	status = result::Skewed;
	break;
      }

    return status;
  }

  result connection_worker::handle_packet (context *ctx, cubbase::span<std::byte> &packet)
  {
    result status;

    switch (ctx->m_recv.m_state)
      {
      case state::HEADER:
	status = this->handle_header_packet (ctx, packet);
	break;

      case state::DATA:
	status = this->handle_data_packet (ctx, packet);
	break;

      case state::ERROR:
	status = this->handle_error_packet (ctx, packet);
	break;

      default:
	status = result::Error;
	er_log_conn (ARG_FILE_LINE, "connection_worker->handle_packet: unknown state\n");
	assert_release (false);
	break;
      }

    return status;
  }

  result connection_worker::handle_reception (context *ctx)
  {
    std::chrono::time_point<std::chrono::steady_clock> start, end;
    std::vector<cubbase::span<std::byte>> *packets;
    result status;
    int mtx;

    rmutex_lock (m_entry, &ctx->m_conn->rmutex);
    if (ctx->m_conn->status != CONN_OPEN || ctx->m_conn->stop_talk == true)
      {
	rmutex_unlock (m_entry, &ctx->m_conn->rmutex);
	this->handle_connection_close (ctx);
	return result::ClosedConnection;
      }
    rmutex_unlock (m_entry, &ctx->m_conn->rmutex);

    status = ctx->m_recv.m_receiver.drain (ctx->m_conn->fd);
    if (status == result::PeerReset || status == result::Error)
      {
	er_log_conn (__FILE__, __LINE__, "connection_worker->handle_reception: status = %d\n", status);
	ctx->m_send.m_transmitter.empty ();
	this->handle_connection_close (ctx);
	return status;
      }

    assert (status == result::Ok || status == result::Pending);

    if (status != result::Ok)
      {
	return result::Ok;
      }

    m_stats.add (stats::NET_PACKET_COUNT, ctx->m_recv.m_receiver.get_result ()->size ());

    start = std::chrono::steady_clock::now ();

    /* hold m_conn */
    mtx = rmutex_lock (m_entry, &ctx->m_conn->rmutex);
    if (mtx != NO_ERROR)
      {
	return result::Error;
      }

    end = std::chrono::steady_clock::now ();
    m_stats.add (stats::BLOCKED_RMUTEX, std::chrono::duration_cast<std::chrono::microseconds> (end - start).count ());

    /* received at least one packet */
    packets = ctx->m_recv.m_receiver.get_result ();
    for (auto &packet : *packets)
      {
	status = this->handle_packet (ctx, packet);

	if (status == result::Skewed)
	  {
	    /* the packet must be ignored */
	    ctx->m_recv.m_receiver.release (packet.data ());
	  }
	else if (status == result::ClosedConnection)
	  {
	    er_log_conn (__FILE__, __LINE__, "connection_worker->handle_reception: requested to close the connection\n");
	    mtx = rmutex_unlock (m_entry, &ctx->m_conn->rmutex);
	    if (mtx != NO_ERROR)
	      {
		return result::Error;
	      }
	    this->handle_connection_close (ctx);
	    return status;
	  }
      }

    /* release m_conn */
    mtx = rmutex_unlock (m_entry, &ctx->m_conn->rmutex);
    if (mtx != NO_ERROR)
      {
	return result::Error;
      }

    packets->clear ();

    return result::Ok;
  }

  result connection_worker::handle_transmission (context *ctx)
  {
    result status;

    status = ctx->m_send.m_transmitter.fill (ctx->m_conn->fd);
    if (status == result::PeerReset || status == result::Error)
      {
	er_log_conn (__FILE__, __LINE__, "connection_worker->handle_transmission: status = %d\n", status);

	/* ctx will be forcibly removed */
	ctx->m_ignore = ignore_level::IGNORE_ALL;

	this->handle_connection_close (ctx);
	return status;
      }

    assert (status == result::Ok || status == result::Pending);

    if (status == result::Ok)
      {
	rmutex_lock (m_entry, &ctx->m_conn->rmutex);
	if (ctx->m_conn->status == CONN_CLOSING)
	  {
	    rmutex_unlock (m_entry, &ctx->m_conn->rmutex);
	    /* this transmission is the last handling on this connection */
	    this->handle_connection_close (ctx);
	    er_log_conn (__FILE__, __LINE__,
			 "connection_worker->handle_transmission: this transmission is the last handling on this connection: closed\n");
	    return result::ClosedConnection;
	  }
	rmutex_unlock (m_entry, &ctx->m_conn->rmutex);

	if (!m_events.modify_descriptor (ctx->m_conn->fd, EPOLLET | EPOLLIN | EPOLLRDHUP, ctx))
	  {
	    er_log_conn (__FILE__, __LINE__, "connection_worker->handle_transmission: modify_descriptor failed\n");

	    /* ctx will be forcibly removed */
	    ctx->m_ignore = ignore_level::IGNORE_ALL;

	    this->handle_connection_close (ctx);
	    return result::Error;
	  }
	ctx->m_send.m_transmitter.clear ();
	er_log_conn (__FILE__, __LINE__, "fully sent. fd = %d in the worker = %d\n", ctx->m_conn->fd, m_index);
      }
    return status;
  }

  void connection_worker::initialize ()
  {
    /* pin myself */
    cubbase::topology.pin_core (m_core);

    /* entry */
    m_entry = cubthread::get_manager ()->claim_entry ();
    if (m_entry == nullptr)
      {
	er_log_conn (__FILE__, __LINE__, "connection_worker->initialize: claim_entry failed\n");
	assert_release (false);
      }
    m_entry->register_id ();
    m_entry->index = m_index;
    m_entry->type = TT_SERVER;
    m_entry->tran_index = -1;
    m_entry->m_status = cubthread::entry::status::TS_RUN;
    m_entry->shutdown = false;

    m_entry->get_error_context ().register_thread_local ();

    m_context.clear ();
    m_removed_context.clear ();
  }

  void connection_worker::finalize ()
  {
    std::vector<context *> contexts (m_context.begin (), m_context.end ());

    /* alive context */
    for (auto &ctx : contexts)
      {
	ctx->m_ignore = ignore_level::IGNORE_ALL;
	this->handle_connection_close (ctx);
      }

    while (!m_context.empty ())
      {
	this->handle_message_queue_by_index (queue_type::LAZY);

	/* removed context */
	for (auto &ctx : m_removed_context)
	  {
	    if (m_context.erase (ctx) == 0)
	      {
		er_log_conn (__FILE__, __LINE__, "connection_worker->handle_message_queue: context not found\n");
		continue;
	      }
	    delete ctx;
	  }
	m_removed_context.clear ();

	/* 1 ms */
	thread_sleep (1);
      }
    m_context.clear ();

    m_entry->unregister_id ();
    cubthread::get_manager ()->retire_entry (*m_entry);
  }

  bool connection_worker::run ()
  {
    std::array<epoll_event, 512> events;
    result status;
    context *ctx;
    int nfds, i;

    m_notified = false;
    while (!m_stop)
      {
	nfds = m_events.wait (events.data (), events.size (), m_notified ? 50 /* 50 msec */ : TIMEOUT_INFINITE);
	if (nfds < 0)
	  {
	    if (errno == EINTR)
	      {
		continue;
	      }
	    er_log_conn (__FILE__, __LINE__, "master_connector->execute: m_events->wait failed: %s", strerror (errno));
	    assert_release (false);
	  }

	for (i = 0; i < nfds; i++)
	  {
	    assert (events[i].data.ptr);

	    ctx = reinterpret_cast<context *> (events[i].data.ptr);
	    if ((events[i].events & (EPOLLHUP | EPOLLRDHUP | EPOLLERR)) && ctx->m_conn->fd != m_eventfd)
	      {
		this->handle_hangup_or_error (ctx, events[i].events & EPOLLERR);
		continue;
	      }
	    if (events[i].events & EPOLLIN)
	      {
		if (ctx->m_conn->fd == m_eventfd)
		  {
		    m_notified = true;
		    continue;
		  }
		status = this->handle_reception (ctx);
		if (status == result::ClosedConnection || status == result::PeerReset)
		  {
		    continue;
		  }
		if (status == result::Error)
		  {
		    er_log_conn (__FILE__, __LINE__, "connection_worker->run: handle_reception failed");
		    return false;
		  }
	      }
	    if (events[i].events & EPOLLOUT)
	      {
		status = this->handle_transmission (ctx);
		if (status == result::ClosedConnection || status == result::PeerReset)
		  {
		    continue;
		  }
		if (status == result::Error)
		  {
		    er_log_conn (__FILE__, __LINE__, "connection_worker->run: handle_transmission failed");
		    return false;
		  }
	      }
	  }

	/* lazy handling */
	if (m_notified)
	  {
	    m_notified = false;
	    if (!this->handle_message_queue ())
	      {
		er_log_conn (__FILE__, __LINE__, "connection_worker->run: handle_message_queue failed");
		return false;
	      }
	  }
      }

    return true;
  }

  void connection_worker::attach ()
  {
    this->initialize ();
    this->run ();
    this->finalize ();
  }
}
