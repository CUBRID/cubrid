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

#include "hardware_topology.hpp"
#include "network.h"
#include "network_interface_sr.h"
#include "connection_sr.h"
#include "connection_defs.h"
#include "connection_worker.hpp"
#include "buffer.hpp"
#include "error_manager.h"

#include <array>
#include <thread>
#include <unistd.h>
#include <sys/eventfd.h>
#include <sys/epoll.h>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#ifdef _er_log_debug
#undef _er_log_debug
#endif
#define _er_log_debug(x, ...) do { } while (0)

#define NEXT_STATE(ctx, sel, x) do { \
    er_log_debug (__FILE__, __LINE__, "fd = %d, set state = %d\n", ctx->m_conn ? ctx->m_conn->fd : -1, state::x); \
    (ctx->sel.m_state = state::x); \
} while (0)

namespace cubconn
{
  connection_worker::context::context (std::size_t capacity) :
    m_recv
  {
    .m_state = state::HEADER,
    .m_receiver = receiver (capacity),
    .m_header = { nullptr, 0 },
    .m_request_id = -1,
    .m_command = false
  }
  {
  }

  connection_worker::context::~context ()
  {
  }

  connection_worker::connection_worker (connection_pool *pool, std::size_t core, std::size_t index) :
    m_core (core),
    m_stop (false),
    m_parent (pool),
    m_index (index),
    m_entry (nullptr)
  {
    context *ctx;

    m_eventfd = eventfd (0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (m_eventfd == -1)
      {
	_er_log_debug (__FILE__, __LINE__, "connection_worker: failed to create eventfd\n");
	assert_release (false);
      }
    ctx = new context (4 * 1024);
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
	delete ctx->m_conn;
	assert_release (false);
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

  void connection_worker::enqueue (const message &item)
  {
    m_queue.push (std::move (item));

    _er_log_debug (__FILE__, __LINE__, "enqueued request_type = %d to the worker index = %d\n", item.type, m_index);
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

    _er_log_debug (__FILE__, __LINE__, "requested to wake up the worker index = %d\n", m_index);
    return true;
  }

  void connection_worker::push_task_into_worker_pool (context *ctx)
  {
    /* push new task into worker pool */
    css_push_server_task (*ctx->m_conn);
  }

  bool connection_worker::handle_connection_error (context *ctx)
  {
    if (!m_events.remove_descriptor (ctx->m_conn->fd))
      {
	_er_log_debug (__FILE__, __LINE__, "connection_worker->remove_connection: remove_descriptor failed\n");
	return false;
      }

    ctx->m_send.m_transmitter.clear ();

    /* net_server_conn_down */
    /* TODO: this function makes this thread blocked. epoll threads must not be blocked */
    m_entry->conn_entry = ctx->m_conn;
    pthread_mutex_lock (&m_entry->tran_index_lock);
    css_Connection_error_handler (m_entry, ctx->m_conn);
    m_entry->conn_entry = NULL;

    if (m_context.erase (ctx) == 0)
      {
	_er_log_debug (__FILE__, __LINE__, "connection_worker->remove_connection: context not found\n");
	return false;
      }
    delete ctx;

    return true;
  }

  result connection_worker::handle_unexpected_packet (context *ctx)
  {
    char buffer[4096];
    ssize_t bytes;

    _er_log_debug (__FILE__, __LINE__, "connection_worker->handle_unexpected_packet: fd = %d\n", ctx->m_conn->fd);

    ctx->m_recv.m_state = state::HEADER;
    ctx->m_recv.m_receiver.reset ();
    ctx->m_recv.m_command = false;
    while (true)
      {
	bytes = ::recv (ctx->m_conn->fd, buffer, sizeof (buffer), 0);
	if (bytes > 0)
	  {
	    continue;
	  }

	if (__builtin_expect (bytes == 0, 0))
	  {
	    return result::PeerReset;
	  }

	switch (errno)
	  {
	  case EINTR:
	    continue;
	  case EAGAIN:
#if defined (EWOULDBLOCK) && EWOULDBLOCK != EAGAIN
	  case EWOULDBLOCK:
#endif
	    /* pending (= successfully drained all) */
	    return result::Ok;
	  case EPIPE:
	  case ECONNRESET:
	    return result::PeerReset;
	  default:
	    return result::Error;
	  }
      }

    return result::Error;
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

    assert (item.conn && item.conn->context);

    ctx = reinterpret_cast<context *> (item.conn->context);

    for (auto &packet : item.packet)
      {
	ctx->m_send.m_transmitter.push_for_send ({ packet.data (), packet.size () });
      }
    ctx->m_send.m_transmitter.stamp ();
    ctx->m_send.m_transmitter.push_for_deleter (std::move (item.deleter));

    if (!m_events.modify_descriptor (ctx->m_conn->fd, EPOLLET | EPOLLIN | EPOLLOUT | EPOLLRDHUP, ctx))
      {
	_er_log_debug (__FILE__, __LINE__, "connection_worker->handle_message_queue_send_packet: modify_descriptor failed\n");
	return false;
      }
    _er_log_debug (__FILE__, __LINE__, "new packet to send. fd = %d in the worker = %d\n", item.conn->fd, m_index);

    return true;
  }

  bool connection_worker::handle_message_queue_release_packet (message &item)
  {
    context *ctx;

    assert (item.conn && item.conn->context);
    assert (item.packet.size () > 0);

    ctx = reinterpret_cast<context *> (item.conn->context);
    for (auto &packet : item.packet)
      {
	ctx->m_recv.m_receiver.release (packet);
	_er_log_debug (__FILE__, __LINE__,
		       "connection_worker->handle_message_queue_release_packet: release packet pointer = %p\n", packet.data ());
      }

    return true;
  }

  bool connection_worker::handle_message_queue_new_client (message &item)
  {
    context *ctx;

    ctx = new context (32 * 1024);
    if (!ctx)
      {
	_er_log_debug (__FILE__, __LINE__, "connection_worker->handle_message_queue_new_client: failed to allocate memory\n");
	return false;
      }
    ctx->m_conn = item.conn;
    ctx->m_conn->worker = this;
    ctx->m_conn->context = ctx;
    if (!m_events.add_descriptor (ctx->m_conn->fd, EPOLLET | EPOLLIN | EPOLLRDHUP, ctx))
      {
	delete ctx;
	_er_log_debug (__FILE__, __LINE__, "connection_worker->handle_message_queue_new_client: add_descriptor failed\n");
	return false;
      }
    if (!m_context.insert (ctx).second)
      {
	delete ctx;
	_er_log_debug (__FILE__, __LINE__,
		       "connection_worker->handle_message_queue_new_client: context can not be duplicated\n");
	return false;
      }
    _er_log_debug (__FILE__, __LINE__, "add new client that has fd = %d in the worker = %d\n", item.conn->fd, m_index);
    return true;
  }

  bool connection_worker::handle_message_queue ()
  {
    message request;

    if (!this->clear_event ())
      {
	return false;
      }

    while (m_queue.try_pop (request))
      {
	_er_log_debug (__FILE__, __LINE__, "recevied request_type = %d from message queue in the worker = %d\n", request.type,
		       m_index);

	switch (request.type)
	  {
	  case message_type::NEW_CLIENT:
	    if (!this->handle_message_queue_new_client (request))
	      {
		return false;
	      }
	    break;

	  case message_type::SHUTDOWN:
	    m_stop = true;
	    break;

	  case message_type::SEND_PACKET:
	    if (!this->handle_message_queue_send_packet (request))
	      {
		return false;
	      }
	    break;

	  case message_type::RELEASE_PACKET:
	    if (!this->handle_message_queue_release_packet (request))
	      {
		return false;
	      }
	    break;

	  default:
	    _er_log_debug (__FILE__, __LINE__,
			   "master_connector->handle_message_queue: received unknown event from eventfd in the worker = %d\n", m_index);
	    assert_release (false);
	    break;
	  }
      }

    return true;
  }

  result connection_worker::handle_error_packet (context *ctx, cubbase::span<std::byte> &packet)
  {
    css_conn_entry *conn;
    css_error_code error;
    NET_HEADER *header;
    int size, aligned;

    assert (ctx->m_recv.m_header.size () == sizeof (NET_HEADER));

    conn = ctx->m_conn;
    header = reinterpret_cast<NET_HEADER *> (ctx->m_recv.m_header.data ());

    size = ntohl (header->buffer_size);
    if (packet.size () != static_cast<std::size_t> (size) && packet.size () != ((static_cast<std::size_t> (size) + 7) & ~7))
      {
	_er_log_debug (__FILE__, __LINE__,
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
	    ctx->m_recv.m_receiver.release (packet);
	    return result::Error;
	  }
      }
    else
      {
	ctx->m_recv.m_receiver.release (packet);
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
	_er_log_debug (__FILE__, __LINE__,
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
		ctx->m_recv.m_receiver.release (packet);
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
	ctx->m_recv.m_receiver.release (ctx->m_recv.m_header);
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
	ctx->m_recv.m_receiver.release (ctx->m_recv.m_header);
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
	/* 1. the state was wrong or				    */
	/* 2. the incoming packet was wrong			    */
	/* in this case, we must reset the context and drain all data */
	/* from the socket to recover this state machine and handle   */
	/* the next request properly.				    */
	_er_log_debug (__FILE__, __LINE__,
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
	ctx->m_recv.m_receiver.release (packet);
	NEXT_STATE (ctx, m_recv, DATA);
	break;

      case ABORT_TYPE:
	/* no more packets are requested */
	ctx->m_recv.m_receiver.release (packet);
	ctx->m_recv.m_command = false;
	css_process_abort_packet (ctx->m_conn, ctx->m_recv.m_request_id);
	break;

      case CLOSE_TYPE:
	ctx->m_recv.m_receiver.release (packet);
	/* no more packets are requested */
	status = result::ClosedConnection;
	break;

      case ERROR_TYPE:
	ctx->m_recv.m_receiver.release (packet);
	NEXT_STATE (ctx, m_recv, ERROR);
	break;

      default:
	ctx->m_recv.m_receiver.release (packet);
	_er_log_debug (ARG_FILE_LINE,
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
	_er_log_debug (ARG_FILE_LINE, "connection_worker->handle_packet: unknown state\n");
	assert_release (false);
	break;
      }

    return status;
  }

  result connection_worker::handle_reception (context *ctx)
  {
    std::vector<cubbase::span<std::byte>> *packets;
    result status;
    int mtx;

    if (ctx->m_conn->status != CONN_OPEN || ctx->m_conn->stop_talk == true)
      {
	handle_connection_error (ctx);
	return result::ClosedConnection;
      }

    status = ctx->m_recv.m_receiver.drain (ctx->m_conn->fd);
    if (status == result::PeerReset || status == result::Error)
      {
	_er_log_debug (__FILE__, __LINE__, "connection_worker->handle_reception: status = %d\n", status);
	handle_connection_error (ctx);
	return status;
      }

    assert (status == result::Ok || status == result::Pending);

    if (status != result::Ok)
      {
	return result::Ok;
      }

    /* hold m_conn */
    mtx = rmutex_lock (m_entry, &ctx->m_conn->rmutex);
    if (mtx != NO_ERROR)
      {
	return result::Error;
      }

    /* received at least one packet */
    packets = ctx->m_recv.m_receiver.get_result ();
    for (auto &packet : *packets)
      {
	status = this->handle_packet (ctx, packet);

	if (status == result::Skewed)
	  {
	    /* drain all and reset the context */
	    status = this->handle_unexpected_packet (ctx);
	    if (status == result::PeerReset || status == result::Error)
	      {
		_er_log_debug (__FILE__, __LINE__, "connection_worker->handle_reception: reset by peer\n");
		mtx = rmutex_unlock (m_entry, &ctx->m_conn->rmutex);
		if (mtx != NO_ERROR)
		  {
		    return result::Error;
		  }
		handle_connection_error (ctx);
		return status;
	      }
	  }
	else if (status == result::ClosedConnection)
	  {
	    _er_log_debug (__FILE__, __LINE__, "connection_worker->handle_reception: requested to close the connection\n");
	    mtx = rmutex_unlock (m_entry, &ctx->m_conn->rmutex);
	    if (mtx != NO_ERROR)
	      {
		return result::Error;
	      }
	    handle_connection_error (ctx);
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

    if (ctx->m_conn->status != CONN_OPEN || ctx->m_conn->stop_talk == true)
      {
	handle_connection_error (ctx);
	return result::ClosedConnection;
      }

    status = ctx->m_send.m_transmitter.fill (ctx->m_conn->fd);
    if (status == result::PeerReset || status == result::Error)
      {
	_er_log_debug (__FILE__, __LINE__, "connection_worker->handle_transmission: status = %d\n", status);
	handle_connection_error (ctx);
	return status;
      }

    assert (status == result::Ok || status == result::Pending || status == result::Partial);

    if (status == result::Ok)
      {
	if (!m_events.modify_descriptor (ctx->m_conn->fd, EPOLLET | EPOLLIN | EPOLLRDHUP, ctx))
	  {
	    _er_log_debug (__FILE__, __LINE__, "connection_worker->handle_transmission: modify_descriptor failed\n");
	    handle_connection_error (ctx);
	    return result::Error;
	  }
	ctx->m_send.m_transmitter.clear ();
	_er_log_debug (__FILE__, __LINE__, "fully sent. fd = %d in the worker = %d\n", ctx->m_conn->fd, m_index);
      }
    return status;
  }

  void connection_worker::initialize ()
  {
    /* pin myself */
    cubbase::topology.pin_core (m_core);

    /* entry */
    m_entry = cubthread::get_manager ()->claim_entry ();
    m_entry->register_id ();
    m_entry->index = m_index;
    m_entry->type = TT_SERVER;
    m_entry->m_status = cubthread::entry::status::TS_RUN;
    m_entry->shutdown = false;

    m_entry->get_error_context ().register_thread_local ();
  }

  void connection_worker::finalize ()
  {
    for (auto &ctx : m_context)
      {
	if (!m_events.remove_descriptor (ctx->m_conn->fd))
	  {
	    _er_log_debug (__FILE__, __LINE__, "connection_worker->finalize: remove_descriptor failed\n");
	    assert_release (false);
	  }

	ctx->m_send.m_transmitter.clear ();

	m_entry->conn_entry = ctx->m_conn;
	/* net_server_conn_down */
	pthread_mutex_lock (&m_entry->tran_index_lock);
	css_Connection_error_handler (m_entry, ctx->m_conn);
	m_entry->conn_entry = NULL;

	delete ctx;
      }
    m_context.clear ();

    m_entry->unregister_id ();
    cubthread::get_manager ()->retire_entry (*m_entry);
  }

  bool connection_worker::run ()
  {
    std::array<epoll_event, 128> events;
    result status;
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

	assert (nfds > 0);

	for (i = 0; i < nfds; i++)
	  {
	    assert (events[i].data.ptr);

	    ctx = reinterpret_cast<context *> (events[i].data.ptr);
	    if ((events[i].events & EPOLLERR) && ctx->m_conn->fd != m_eventfd)
	      {
		_er_log_debug (__FILE__, __LINE__, "connection_worker->run: connection closed: EPOLLERR");
		handle_connection_error (ctx);
		continue;
	      }
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
		status = this->handle_reception (ctx);
		if (status == result::ClosedConnection || status == result::PeerReset)
		  {
		    continue;
		  }
		if (status == result::Error)
		  {
		    _er_log_debug (__FILE__, __LINE__, "connection_worker->run: handle_reception failed");
		    return false;
		  }
	      }
	    if ((events[i].events & (EPOLLHUP | EPOLLRDHUP)) && ctx->m_conn->fd != m_eventfd)
	      {
		_er_log_debug (__FILE__, __LINE__, "connection_worker->run: connection closed: %s", strerror (errno));
		handle_connection_error (ctx);
		continue;
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
		    _er_log_debug (__FILE__, __LINE__, "connection_worker->run: handle_transmission failed");
		    return false;
		  }
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
