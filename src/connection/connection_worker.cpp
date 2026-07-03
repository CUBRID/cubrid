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
 * connection_worker.cpp
 */

#include <atomic>
#include <iostream>
#include <chrono>
#include <array>
#include <thread>
#include <cstdint>
#include <cstdio>
#include <unistd.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <sys/epoll.h>
#include <time.h>

#include "resources.hpp"
#include "porting.h"
#include "tcp.h"
#include "network.h"
#include "network_interface_sr.h"
#include "server_support.h"
#include "connection_pool.hpp"
#include "connection_sr.h"
#include "connection_defs.h"
#include "connection_worker.hpp"
#include "buffer.hpp"
#include "thread_manager.hpp"
#include "error_manager.h"

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

namespace cubconn::connection
{
  REGISTER_CONNECTION (connection_worker, []()
  {
    return prm_get_integer_value (PRM_ID_CSS_MAX_CONNECTION_WORKER);
  });

  worker::worker (pool *pool, std::shared_ptr<coordinator> coord, std::shared_ptr<thread_watcher> watcher,
		  std::size_t core, std::size_t index) :
    m_parent (pool),
    m_coordinator (coord),
    m_watcher (watcher),
    m_core (core),
    m_status (status::READY),
    m_stop (false),
    m_entry (nullptr),
    m_index (index),
    m_has_retry (false)
  {
    std::size_t i;

    m_context.reserve (256);

    /* limiter */
    m_recv_budget = static_cast<size_t> (prm_get_integer_value (PRM_ID_CSS_RECV_BUDGET_PER_CONNECTION));
    m_send_budget = static_cast<size_t> (prm_get_integer_value (PRM_ID_CSS_SEND_BUDGET_PER_CONNECTION));
    m_exhausted.reserve (128);

    /* notifier */
    m_eventfd = eventfd (0, EFD_NONBLOCK | EFD_CLOEXEC);
    m_timerfd = timerfd_create (CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (m_eventfd < 0 || m_timerfd < 0)
      {
	er_log_conn (__FILE__, __LINE__, "connection::worker: failed to create fd. %s\n", strerror (errno));
	assert_release (false);
      }

    if (!this->eventfd_register (m_eventfd) ||
	!this->eventfd_register (m_timerfd))
      {
	er_log_conn (__FILE__, __LINE__, "connection::worker: failed to register fd\n");
	assert_release (false);
      }

    /* timer */
    for (i = 0; i < static_cast<std::size_t> (timer_type::TYPE_COUNT); i++)
      {
	m_timer_handler[i].valid = false;
	m_timer_handler[i].latency = timer_latency::NA;
	m_timer_handler[i].function = nullptr;
	m_timer_handler[i].last_time = this->get_time_ns (CLOCK_MONOTONIC);
      }
    if (!this->eventfd_addtimer (timer_type::HIBERNATE, timer_latency::MEDIUM_LATENCY,
				 std::bind (&worker::hibernate_check, this)))
      {
	er_log_conn (__FILE__, __LINE__, "connection::worker: failed to add timer\n");
	assert_release (false);
      }
    if (!this->eventfd_addtimer (timer_type::STATISTICS, timer_latency::MEDIUM_LATENCY,
				 std::bind (&worker::statistics_metrics_to_coordinator, this)))
      {
	er_log_conn (__FILE__, __LINE__, "connection::worker: failed to add timer\n");
	assert_release (false);
      }
    if (!this->eventfd_addtimer (timer_type::HA, timer_latency::HIGH_LATENCY,
				 std::bind (&worker::ha_close_all_connections, this)))
      {
	er_log_conn (__FILE__, __LINE__, "connection::worker: failed to add timer\n");
	assert_release (false);
      }

    /* request queue */
    for (i = 0; i < static_cast<std::size_t> (queue_type::TYPE_COUNT); i++)
      {
	m_queue_size[i].store (0, std::memory_order_relaxed);
      }

    m_thread = std::thread (&worker::attach, this);
  }

  worker::~worker ()
  {
    if (m_thread.joinable ())
      {
	m_thread.join ();
      }
    ::close (m_eventfd);
    ::close (m_timerfd);

    assert (m_context.size () == 0);
  }

  void worker::enqueue (queue_type type, message &&item)
  {
    assert ((item.conn ? (item.conn->fd != -1) : false) ||
	    (item.ctx ? (item.ctx->m_conn ? item.ctx->m_conn->fd != -1 : false) : false) ||
	    (item.id > 0) ||
	    (item.type == message_type::START || item.type == message_type::SHUTDOWN ||
	     item.type == message_type::HIBERNATE || item.type == message_type::AWAKEN));

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

  bool worker::notify ()
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

  bool worker::enqueue_and_notify (queue_type type, message &&item, std::function<void ()> func,
				   int wait_time)
  {
    /* wait_time is implemented only for  */
    /*	START				  */
    /*	SHUTDOWN_CLIENT			  */
    /*	SEND_PACKET			  */
    /* you must implement a logic to use a waiter whose message type is not in above */

    assert (!wait_time ||
	    (item.type == message_type::START ||
	     item.type == message_type::SHUTDOWN_CLIENT ||
	     item.type == message_type::SEND_PACKET));

    std::shared_ptr<message_blocker> handle;
    std::unique_lock<std::mutex> lock;

    if (wait_time)
      {
	/* acquire the lock to prevent a lost wakeup */
	handle = std::make_shared<message_blocker> ();
	handle->done = false;

	lock = std::unique_lock<std::mutex> (handle->m);
	item.waiter_handle = handle;
      }

    this->enqueue (type, std::move (item));
    if (!this->notify ())
      {
	if (func)
	  {
	    func ();
	  }
	return false;
      }

    if (func)
      {
	func ();
      }

    if (wait_time)
      {
	if (wait_time < 0)
	  {
	    handle->cv.wait (lock, [&] { return handle->done; });
	  }
	else
	  {
	    handle->cv.wait_for (lock, std::chrono::seconds (wait_time), [&] { return handle->done; });
	  }
      }

    return true;
  }

  uint64_t worker::get_time_ns (clockid_t type)
  {
    struct timespec ts;

    if (clock_gettime (type, &ts) == -1)
      {
	er_log_conn (__FILE__, __LINE__, "clock_gettime (CLOCK_MONOTONIC) failed: %s\n", strerror (errno));
	return 0;
      }

    return (uint64_t) ts.tv_sec * 1000000000ULL + ts.tv_nsec;
  }

  void worker::push_task_into_worker_pool (context *ctx)
  {
    /* push new task into worker pool */
    css_push_server_task (*ctx->m_conn);
  }

  void worker::purge_stale_contexts ()
  {
    coordinator::message message;

    message.type = coordinator::message_type::RETURN_TO_POOL;
    message.resource = m_removed_context;

    /* remove from the context list */
    for (context *ctx : m_removed_context)
      {
	if (m_context.erase (ctx) == 0)
	  {
	    er_log_conn (__FILE__, __LINE__, "connection::worker->purge_stale_contexts: context not found\n");
	    continue;
	  }
	m_exhausted.erase (ctx->m_id);
      }
    m_removed_context.clear ();

    m_coordinator->enqueue (std::move (message));
    if (!m_coordinator->notify ())
      {
	assert_release (false);
      }
  }

  void worker::wakeup_blocked_worker (std::shared_ptr<message_blocker> handle)
  {
    if (handle)
      {
	std::lock_guard<std::mutex> lock (handle->m);
	handle->done = true;
	handle->cv.notify_one ();
      }
  }

  bool worker::requires_client_info (context *ctx)
  {
    if (ctx->m_conn->fd == cdc_Gl.conn.fd)
      {
	return false;
      }

    return true;
  }

  bool worker::is_registering_client (context *ctx)
  {
    return ctx->m_conn->has_pending_request () || ctx->m_conn->has_working_task ();
  }

  bool worker::has_remaining_tasks (context *ctx)
  {
    /* handle the entries in the message queue as there may be a queued request to release the memory in ctx */
    this->handle_message_queue_by_index (queue_type::IMMEDIATE);

    if (ctx->m_ignore < ignore_level::IGNORE_ALL && !ctx->m_send.m_transmitter.empty ())
      {
	er_log_conn (ARG_FILE_LINE,
		     "connection::worker->handle_connection_close: retry shutdown (conn %p): send buffer not empty\n", ctx->m_conn);
	return true;
      }

    return false;
  }

  std::pair<int, int> worker::start_connection_close (context *ctx)
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

  void worker::end_connection_close ()
  {
    pthread_mutex_lock (&m_entry->tran_index_lock);

    css_set_thread_info (m_entry, -1, 0, -1, -1);
    m_entry->conn_entry = NULL;
    m_entry->m_status = cubthread::entry::status::TS_RUN;

    pthread_mutex_unlock (&m_entry->tran_index_lock);
  }

  bool worker::retry_connection_close (context *ctx, bool is_retry, std::shared_ptr<message_blocker> handle)
  {
    message request;

    er_log_conn (__FILE__, __LINE__, "connection::worker->handle_connection_close: retry fd = %d, ignore = %d, conn = %p\n",
		 ctx->m_conn ? ctx->m_conn->fd : -1, ctx->m_ignore, ctx->m_conn);

    request.type = message_type::SHUTDOWN_CLIENT;
    request.conn = ctx->m_conn;
    request.ctx = ctx;
    request.id = ctx->m_id;
    request.ignore = ctx->m_ignore;
    request.retry = is_retry;
    request.waiter_handle = handle;

    /* this request must be handled as lazily */
    this->enqueue (queue_type::LAZY, std::move (request));

    /* lazily notified */
    m_has_retry = true;

    return this->eventfd_addtimer (
		   timer_type::QUEUE,
		   timer_latency::LOW_LATENCY,
		   std::bind (&worker::handle_message_queue, this)
	   );
  }

  bool worker::handle_connection_close (context *ctx, bool is_retry, std::shared_ptr<message_blocker> handle)
  {
    std::chrono::time_point<std::chrono::steady_clock> start, end;
    int tran_index, client_id;
    int status;

    if (ctx->m_removed)
      {
	/* wake up the thread blocked until this request is complete */
	this->wakeup_blocked_worker (handle);

	er_log_conn (__FILE__, __LINE__, "connection::worker->handle_connection_close: already removed context. conn = %p\n",
		     ctx->m_conn);
	return true;
      }

    assert_release (ctx->m_conn);

    /* during shutdown, boot_client_register cannot be expected to finish */
    if (m_status != status::TERMINATING && !css_is_shutdowning_server ()
	&& this->requires_client_info (ctx)
	&& ctx->m_conn->get_tran_index () == NULL_TRAN_INDEX
	&& this->is_registering_client (ctx))
      {
	rmutex_lock (m_entry, &ctx->m_conn->rmutex);
	ctx->m_conn->stop_talk = true;
	rmutex_unlock (m_entry, &ctx->m_conn->rmutex);

	er_log_conn (__FILE__, __LINE__,
		     "connection::worker->handle_connection_close: retry for transaction index. conn = %p, fd = %d\n",
		     ctx->m_conn, ctx->m_conn->fd);

	return this->retry_connection_close (ctx, is_retry, handle);
      }

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
	if (this->requires_client_info (ctx))
	  {
	    /* retry was skipped, so boot_client_register is not expected to publish a transaction index. */
	    /* close without retry. */
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

    if (!is_retry)
      {
	/* interrupt and wake up */

	net_server_wakeup_workers (m_entry, tran_index, client_id);
      }

    /* retry until the worker related to the connection is complete */

    if (net_server_active_workers (m_entry, ctx->m_conn, tran_index, client_id) > 0)
      {
	er_log_conn (__FILE__, __LINE__,
		     "connection::worker->handle_connection_close: net_server_active_workers. conn = %p, fd = %d\n", ctx->m_conn,
		     ctx->m_conn->fd);
	goto retry;
      }

    /* check if there is any remaining task */

    if (this->has_remaining_tasks (ctx))
      {
	er_log_conn (__FILE__, __LINE__,
		     "connection::worker->handle_connection_close: has_remaining_tasks. conn = %p, fd = %d\n", ctx->m_conn,
		     ctx->m_conn->fd);
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

    start = std::chrono::steady_clock::now ();

    rmutex_lock (m_entry, &ctx->m_conn->cmutex);

    ctx->m_conn->worker = nullptr;
    ctx->m_conn->context = nullptr;

    rmutex_unlock (m_entry, &ctx->m_conn->cmutex);

    end = std::chrono::steady_clock::now ();
    m_stats.add (statistics::worker::BLOCKED_RMUTEX,
		 std::chrono::duration_cast<std::chrono::microseconds> (end - start).count ());

    ctx->m_send.m_transmitter.clear ();

    /* close the socket */
    css_shutdown_socket (ctx->m_conn->fd);
    ctx->m_conn->fd = INVALID_SOCKET;

    /* wake up the thread blocked until this request is complete */
    this->wakeup_blocked_worker (handle);
    this->wakeup_blocked_worker (ctx->m_send.m_blocker);

    /* any sessions that are nat cleared (e.g. cdc, flashback) should be handled here */
    css_prepare_shutdown_conn (ctx->m_conn);

    /* mark deleted and lazily release this */
    ctx->m_removed = true;
    m_removed_context.push_back (ctx);

    m_stats.sub (statistics::worker::CLIENT_NUM, 1);

    return true;

retry:
    this->end_connection_close ();

    return this->retry_connection_close (ctx, true, handle);
  }

  bool worker::statistics_metrics_to_coordinator ()
  {
    coordinator::message message;

    message.type = coordinator::message_type::STATISTICS;

    message.statistics.cpu_time_ns = get_time_ns (CLOCK_THREAD_CPUTIME_ID);
    message.statistics.time_ns = get_time_ns (CLOCK_MONOTONIC);
    message.statistics.worker.first = m_index;
    message.statistics.worker.second = m_stats;
    message.statistics.contexts.reserve (m_context.size ());
    for (context *ctx : m_context)
      {
	message.statistics.contexts.emplace_back (ctx->m_id, ctx->m_stats);
      }

    /* just enqueue */
    m_coordinator->enqueue (std::move (message));

    return true;
  }

  bool worker::hibernate_check ()
  {
    if (m_status != status::HIBERNATING || !m_context.empty ())
      {
	return true;
      }

    if (!this->eventfd_stoptimer ())
      {
	er_log_conn (__FILE__, __LINE__, "connection::worker->hibernate_check: failed to stop the timer\n");
	assert_release (false);
      }

    assert (m_context.empty ());
    assert (m_exhausted.empty ());

    /* reset counters so resumed workers don't report stale totals */
    m_stats.reset ();

    return true;
  }

  bool worker::ha_close_all_connections ()
  {
    /* HA msut continue to check */
    if (css_ha_server_state () != HA_SERVER_STATE_TO_BE_STANDBY)
      {
	return true;
      }

    /* alive context */
    for (auto &ctx : m_context)
      {
	if (!ctx->m_conn->in_transaction
	    && css_count_transaction_worker_threads (m_entry, ctx->m_conn->get_tran_index (), ctx->m_conn->client_id) == 0)
	  {
	    er_log_conn (__FILE__, __LINE__, "connection::worker->ha_close_all_connections: close fd = %d, conn = %p\n",
			 ctx->m_conn->fd, ctx->m_conn);
	    this->handle_connection_close (ctx);
	  }
      }

    /* the actual release of the context is handled last */
    this->purge_stale_contexts ();

    return true;
  }

  bool worker::eventfd_register (int fd)
  {
    context *ctx;
    css_conn_entry *conn;

    ctx = new context ();
    conn = reinterpret_cast<css_conn_entry *> (new int { fd });
    if (!ctx || !conn)
      {
	er_log_conn (__FILE__, __LINE__, "connection::worker->eventfd_register: failed to allocate memory\n");
	return false;
      }
    ctx->m_conn = conn;

    if (!m_events.add_descriptor (fd, EPOLLET | EPOLLIN, ctx))
      {
	er_log_conn (__FILE__, __LINE__, "connection::worker->eventfd_register: add_descriptor failed\n");

	delete ctx;
	delete conn;
	return false;
      }

    return true;
  }

  bool worker::eventfd_clear (int fd)
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

  bool worker::eventfd_settimer (int fd, uint64_t sec, uint64_t nsec)
  {
    struct itimerspec its;

    memset (&its, 0, sizeof (its));
    its.it_value.tv_sec = sec;
    its.it_value.tv_nsec = nsec;
    its.it_interval.tv_sec = sec;
    its.it_interval.tv_nsec = nsec;

    if (timerfd_settime (fd, 0, &its, NULL) < 0)
      {
	er_log_conn (__FILE__, __LINE__, "connection::worker->eventfd_settimer: %s\n", strerror (errno));
	return false;
      }

    return true;
  }

  bool worker::eventfd_settimer (int fd, timer_latency latency)
  {
    uint64_t sec, nsec;

    sec = static_cast<uint64_t> (latency) / static_cast<uint64_t> (1e9);
    nsec = static_cast<uint64_t> (latency) % static_cast<uint64_t> (1e9);
    if (eventfd_settimer (fd, sec, nsec))
      {
	return true;
      }
    return false;
  }

  bool worker::eventfd_starttimer ()
  {
    timer_latency min;
    std::size_t i;

    min = timer_latency::NA;
    for (i = 0; i < static_cast<std::size_t> (timer_type::TYPE_COUNT); i++)
      {
	if (m_timer_handler[i].valid)
	  {
	    if (min == timer_latency::NA || static_cast<uint64_t> (min) > static_cast<uint64_t> (m_timer_handler[i].latency))
	      {
		min = m_timer_handler[i].latency;
	      }
	  }
      }

    if (min != timer_latency::NA)
      {
	return this->eventfd_settimer (m_timerfd, min);
      }
    return this->eventfd_stoptimer ();
  }

  bool worker::eventfd_stoptimer ()
  {
    return eventfd_settimer (m_timerfd, 0, 0);
  }

  bool worker::eventfd_addtimer (timer_type type, timer_latency latency, std::function<bool ()> handle)
  {
    timer_latency min;
    std::size_t i;

    if (m_timer_handler[static_cast<std::size_t> (type)].valid)
      {
	return true;
      }

    m_timer_handler[static_cast<std::size_t> (type)].valid = true;
    m_timer_handler[static_cast<std::size_t> (type)].latency = latency;
    m_timer_handler[static_cast<std::size_t> (type)].function = handle;
    m_timer_handler[static_cast<std::size_t> (type)].last_time = this->m_timens;

    min = timer_latency::NA;
    for (i = 0; i < static_cast<std::size_t> (timer_type::TYPE_COUNT); i++)
      {
	if (m_timer_handler[i].valid)
	  {
	    if (min == timer_latency::NA || static_cast<uint64_t> (min) > static_cast<uint64_t> (m_timer_handler[i].latency))
	      {
		min = m_timer_handler[i].latency;
	      }
	  }
      }

    assert (min != timer_latency::NA);

    return this->eventfd_settimer (m_timerfd, min);
  }

  bool worker::eventfd_removetimer (timer_type type)
  {
    timer_latency min;
    std::size_t i;

    /* avoid resetting the timerfd unnecessarily */
    if (!m_timer_handler[static_cast<std::size_t> (type)].valid)
      {
	return true;
      }

    m_timer_handler[static_cast<std::size_t> (type)].valid = false;

    min = timer_latency::NA;
    for (i = 0; i < static_cast<std::size_t> (timer_type::TYPE_COUNT); i++)
      {
	if (m_timer_handler[i].valid)
	  {
	    if (min == timer_latency::NA || static_cast<uint64_t> (min) > static_cast<uint64_t> (m_timer_handler[i].latency))
	      {
		min = m_timer_handler[i].latency;
	      }
	  }
      }

    if (min != timer_latency::NA)
      {
	return this->eventfd_settimer (m_timerfd, min);
      }
    return this->eventfd_stoptimer ();
  }

  bool worker::eventfd_handler (bool *eventfds)
  {
    std::size_t i;

    m_has_retry = false;

    /* event fd */
    if (eventfds[0])
      {
	eventfds[0] = false;

	if (!this->eventfd_clear (m_eventfd))
	  {
	    er_log_conn (__FILE__, __LINE__, "connection::worker->eventfd_handler: eventfd_clear failed\n");
	    return false;
	  }

	if (!this->handle_message_queue ())
	  {
	    return false;
	  }

	m_timer_handler[static_cast<std::size_t> (timer_type::QUEUE)].last_time = m_timens;
      }

    /* timer fd */
    if (eventfds[1])
      {
	eventfds[1] = false;

	for (i = 0; i < static_cast<std::size_t> (timer_type::TYPE_COUNT); i++)
	  {
	    if (!m_timer_handler[i].valid)
	      {
		continue;
	      }

	    if (m_timens - m_timer_handler[i].last_time > static_cast<uint64_t> (m_timer_handler[i].latency))
	      {
		if (!m_timer_handler[i].function ())
		  {
		    return false;
		  }

		m_timer_handler[i].last_time = m_timens;
	      }
	  }

	if (!this->eventfd_clear (m_timerfd))
	  {
	    er_log_conn (__FILE__, __LINE__, "connection::worker->eventfd_handler: eventfd_clear failed\n");
	    return false;
	  }

	if (!m_has_retry)
	  {
	    return this->eventfd_removetimer (timer_type::QUEUE);
	  }
      }

    return true;
  }

  bool worker::validate_message_generation (const message &item, context *ctx) const
  {
    return ctx != nullptr && item.ctx == ctx && item.id == ctx->m_id && ctx->m_conn == item.conn;
  }

  bool worker::forward_message_to_successor (queue_type type, message &item, context *ctx)
  {
    worker *successor;

    if (item.conn->worker == this && m_context.find (ctx) != m_context.end ())
      {
	return false;
      }

    successor = item.conn->worker;
    if (successor != nullptr)
      {
	successor->enqueue (type, std::move (item));
	if (!successor->notify ())
	  {
	    assert_release (false);
	  }
      }
    else
      {
	this->wakeup_blocked_worker (item.waiter_handle);
      }

    return true;
  }

  bool worker::handle_message_queue_send_packet (message &item)
  {
    context *ctx;
    css_conn_entry *conn;
    result status;
    int r;

    assert (item.conn);

    r = rmutex_lock (m_entry, &item.conn->cmutex);
    assert (r == NO_ERROR);

    ctx = reinterpret_cast<context *> (item.conn->context);
    if (ctx == nullptr)
      {
	if (item.deleter)
	  {
	    item.deleter ();
	  }

	r = rmutex_unlock (m_entry, &item.conn->cmutex);
	assert (r == NO_ERROR);

#if !defined (NDEBUG)
	er_log_conn (__FILE__, __LINE__,
		     "connection::worker->handle_message_queue_send_packet: message_id = %lld, context is already cleared for conn = %p\n",
		     item.message_id, static_cast<void *> (item.conn));
#endif
	this->wakeup_blocked_worker (item.waiter_handle);

	return true;
      }

    conn = item.conn;
    if (!this->validate_message_generation (item, ctx))
      {
	r = rmutex_unlock (m_entry, &conn->cmutex);
	assert (r == NO_ERROR);

	this->wakeup_blocked_worker (item.waiter_handle);

	return true;
      }
    if (this->forward_message_to_successor (queue_type::IMMEDIATE, item, ctx))
      {
	r = rmutex_unlock (m_entry, &conn->cmutex);
	assert (r == NO_ERROR);

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

	this->wakeup_blocked_worker (item.waiter_handle);

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
#else
	er_log_conn (__FILE__, __LINE__, "fully sent. fd = %d in the worker = %d\n", ctx->m_conn->fd, m_index);
#endif

	r = rmutex_unlock (m_entry, &item.conn->cmutex);
	assert (r == NO_ERROR);

	this->wakeup_blocked_worker (item.waiter_handle);

	return true;
      }

    /* if buffer is full, register the fd to epoll loop and wait to send the others */
    if (!m_events.modify_descriptor (ctx->m_conn->fd, EPOLLET | EPOLLIN | EPOLLOUT | EPOLLRDHUP, ctx))
      {
	r = rmutex_unlock (m_entry, &item.conn->cmutex);
	assert (r == NO_ERROR);

	this->wakeup_blocked_worker (item.waiter_handle);

	er_log_conn (__FILE__, __LINE__, "connection::worker->handle_message_queue_send_packet: modify_descriptor failed\n");
	return false;
      }

    r = rmutex_unlock (m_entry, &item.conn->cmutex);
    assert (r == NO_ERROR);

    ctx->m_send.m_blocker = std::move (item.waiter_handle);

    return true;
  }

  bool worker::handle_message_queue_release_packet (message &item)
  {
    context *ctx;
    css_conn_entry *conn;
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
		     "connection::worker->handle_message_queue_release_packet: context is already cleared for conn = %p\n",
		     static_cast<void *> (item.conn));
	return true;
      }

    conn = item.conn;
    if (!this->validate_message_generation (item, ctx))
      {
	r = rmutex_unlock (m_entry, &conn->cmutex);
	assert (r == NO_ERROR);

	return true;
      }
    if (this->forward_message_to_successor (queue_type::IMMEDIATE, item, ctx))
      {
	r = rmutex_unlock (m_entry, &conn->cmutex);
	assert (r == NO_ERROR);

	return true;
      }

    for (cubbase::span<std::byte> &packet : item.packet)
      {
	ctx->m_recv.m_receiver.release (packet.data ());
	er_log_conn (__FILE__, __LINE__,
		     "connection::worker->handle_message_queue_release_packet: release packet pointer = %p\n", packet.data ());
      }

    r = rmutex_unlock (m_entry, &item.conn->cmutex);
    assert (r == NO_ERROR);

    return true;
  }

  bool worker::handle_message_queue_new_client (message &item)
  {
    context *ctx;

    assert (item.conn && item.conn->fd != -1);

    ctx = item.ctx;
    ctx->m_conn = item.conn;

    rmutex_lock (NULL, &ctx->m_conn->cmutex);
    ctx->m_conn->worker = this;
    ctx->m_conn->context = ctx;

    if (!m_events.add_descriptor (ctx->m_conn->fd, EPOLLET | EPOLLIN | EPOLLRDHUP, ctx))
      {
	ctx->m_conn->worker = nullptr;
	ctx->m_conn->context = nullptr;
	rmutex_unlock (NULL, &ctx->m_conn->cmutex);

	m_removed_context.push_back (ctx);
	er_log_conn (__FILE__, __LINE__, "connection::worker->handle_message_queue_new_client: add_descriptor failed\n");
	return false;
      }
    if (!m_context.insert (ctx).second)
      {
	ctx->m_conn->worker = nullptr;
	ctx->m_conn->context = nullptr;
	rmutex_unlock (NULL, &ctx->m_conn->cmutex);

	m_events.remove_descriptor (ctx->m_conn->fd);

	m_removed_context.push_back (ctx);
	er_log_conn (__FILE__, __LINE__,
		     "connection::worker->handle_message_queue_new_client: context can not be duplicated\n");
	return false;
      }

    rmutex_unlock (NULL, &ctx->m_conn->cmutex);

    ctx->m_stats.set (statistics::context::OPEND_NS, m_timens);
    ctx->m_stats.set (statistics::context::LAST_ACTIVE_NS, m_timens);
    ctx->m_stats.set (statistics::context::LAST_MOVED_NS, 0);
    ctx->m_stats.set (statistics::context::MOVE_COUNT, 0);

    er_log_conn (__FILE__, __LINE__, "add new client that has fd = %d in the worker = %d\n", item.conn->fd, m_index);

    m_stats.add (statistics::worker::CLIENT_NUM, 1);

    /* The condition below theoretically cannot be true, so there is no need to start the timer. */
    /* But if the concurrency queue gets out of sync and the MQ processing order is disrupted,	 */
    /* we must start the timer.									 */
    if (m_status == status::HIBERNATING)
      {
	if (!this->eventfd_starttimer ())
	  {
	    er_log_conn (__FILE__, __LINE__, "connection::worker->handle_message_queue_new_client: failed to start the timer\n");
	    assert_release (false);
	  }
      }

    return true;
  }

  bool worker::handle_message_queue_handoff_client (message &item)
  {
    coordinator::message response;
    message request;
    context *ctx;
    uint64_t id;
    bool transferred = true;

    id = item.id;
    auto iterator = std::find_if (m_context.begin (), m_context.end (), [id] (context *ptr)
    {
      return ptr->m_id == id;
    });

    if (iterator == m_context.end ())
      {
	/* the connection has already been cleaned up */
	transferred = false;
	goto respond;
      }
    ctx = *iterator;

    rmutex_lock (NULL, &ctx->m_conn->cmutex);

    /* handle the entries in the message queue */
    this->handle_message_queue_by_index (queue_type::IMMEDIATE);
    this->handle_message_queue_by_index (queue_type::LAZY);

    if (ctx->m_conn->status != CONN_OPEN || ctx->m_removed)
      {
	/* this connection will be terminated soon */
	rmutex_unlock (NULL, &ctx->m_conn->cmutex);

	transferred = false;
	goto respond;
      }

    ctx->m_conn->worker = item.worker_ptr;
    ctx->m_worker = item.worker_index;

    if (!m_events.remove_descriptor (ctx->m_conn->fd))
      {
	er_log_conn (__FILE__, __LINE__, "connection::worker->handle_message_queue_handoff_client: add_descriptor failed\n");
	assert_release (false);
      }
    if (m_context.erase (ctx) == 0)
      {
	er_log_conn (__FILE__, __LINE__, "connection::worker->handle_message_queue_handoff_client: context not found\n");
	assert_release (false);
      }
    m_exhausted.erase (ctx->m_id);

    /* hand off the context */
    request.type = message_type::TAKEOVER_CLIENT;
    request.ctx = ctx;
    item.worker_ptr->enqueue (cubconn::connection::worker::queue_type::IMMEDIATE, std::move (request));
    if (!item.worker_ptr->notify ())
      {
	assert_release (false);
      }

    rmutex_unlock (NULL, &ctx->m_conn->cmutex);

    ctx->m_stats.add (statistics::context::MOVE_COUNT, 1);
    m_stats.sub (statistics::worker::CLIENT_NUM, 1);

respond:
    /* respond to the coordinator */
    response.type = coordinator::message_type::HANDOFF_REPLY;
    response.transferred = transferred;
    response.from = m_index;
    response.to = item.worker_index;
    response.id = id;
    m_coordinator->enqueue (std::move (response));
    if (!m_coordinator->notify ())
      {
	assert_release (false);
      }
    return true;
  }

  bool worker::handle_message_queue_takeover_client (message &item)
  {
    context *ctx;
    int flags;

    ctx = item.ctx;

    rmutex_lock (NULL, &ctx->m_conn->cmutex);

    assert (ctx->m_conn->worker == this);
    assert (static_cast<std::size_t> (ctx->m_worker) == m_index);

    flags = EPOLLET | EPOLLIN | EPOLLRDHUP;
    if (!ctx->m_send.m_transmitter.empty ())
      {
	flags |= EPOLLOUT;
      }

    if (!m_events.add_descriptor (ctx->m_conn->fd, flags, ctx))
      {
	ctx->m_conn->worker = nullptr;
	ctx->m_conn->context = nullptr;
	rmutex_unlock (NULL, &ctx->m_conn->cmutex);

	m_removed_context.push_back (ctx);
	er_log_conn (__FILE__, __LINE__, "connection::worker->handle_message_queue_new_client: add_descriptor failed\n");
	return false;
      }
    if (!m_context.insert (ctx).second)
      {
	ctx->m_conn->worker = nullptr;
	ctx->m_conn->context = nullptr;
	rmutex_unlock (NULL, &ctx->m_conn->cmutex);

	m_events.remove_descriptor (ctx->m_conn->fd);

	m_removed_context.push_back (ctx);
	er_log_conn (__FILE__, __LINE__,
		     "connection::worker->handle_message_queue_new_client: context can not be duplicated\n");
	return false;
      }

    rmutex_unlock (NULL, &ctx->m_conn->cmutex);

    ctx->m_stats.set (statistics::context::LAST_MOVED_NS, m_timens);

    er_log_conn (__FILE__, __LINE__, "take over the client that has fd = %d in the worker = %d\n", ctx->m_conn->fd,
		 m_index);

    m_stats.add (statistics::worker::CLIENT_NUM, 1);

    /* The condition below theoretically cannot be true, so there is no need to start the timer. */
    /* But if the concurrency queue gets out of sync and the MQ processing order is disrupted,	 */
    /* we must start the timer.									 */
    if (m_status == status::HIBERNATING)
      {
	if (!this->eventfd_starttimer ())
	  {
	    er_log_conn (__FILE__, __LINE__,
			 "connection::worker->handle_message_queue_takeover_client: failed to start the timer\n");
	    assert_release (false);
	  }
      }

    return true;
  }

  bool worker::handle_message_queue_shutdown_client (message &item)
  {
    context *ctx;
    css_conn_entry *conn;
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
		     "connection::worker->handle_message_queue_shutdown_client: context is already cleared for conn = %p\n",
		     static_cast<void *> (item.conn));

	this->wakeup_blocked_worker (item.waiter_handle);
	return true;
      }

    conn = item.conn;
    if (!this->validate_message_generation (item, ctx))
      {
	r = rmutex_unlock (m_entry, &conn->cmutex);
	assert (r == NO_ERROR);

	this->wakeup_blocked_worker (item.waiter_handle);

	return true;
      }
    if (this->forward_message_to_successor (queue_type::LAZY, item, ctx))
      {
	r = rmutex_unlock (m_entry, &conn->cmutex);
	assert (r == NO_ERROR);

	return true;
      }

    ctx->m_ignore = item.ignore;

    r = rmutex_unlock (m_entry, &item.conn->cmutex);
    assert (r == NO_ERROR);

    this->handle_connection_close (ctx, item.retry, item.waiter_handle);

    return true;
  }

  bool worker::handle_message_queue_start (message &item)
  {
    assert (m_status == status::READY || m_status == status::RUNNING);

    m_status = status::RUNNING;
    this->wakeup_blocked_worker (item.waiter_handle);

    return true;
  }

  bool worker::handle_message_queue_hibernate (message &item)
  {
    m_status = status::HIBERNATING;

    return true;
  }

  bool worker::handle_message_queue_awaken (message &item)
  {
    if (!this->eventfd_starttimer ())
      {
	er_log_conn (__FILE__, __LINE__, "connection::worker->handle_message_queue_awaken: failed to start the timer\n");
	assert_release (false);
      }

    m_status = status::RUNNING;

    return true;
  }

  bool worker::handle_message_queue_shutdown (message &item)
  {
    m_status = status::TERMINATING;
    m_stop = true;

    return true;
  }

  bool worker::handle_message_queue_by_index (queue_type type)
  {
    static constexpr std::array<
    std::pair<bool (worker::*) (message &), statistics::worker>, static_cast<std::size_t> (message_type::TYPE_COUNT)
    > handler =
    {
      {
	/* START	   */ { &worker::handle_message_queue_start,		statistics::worker::NA },
	/* HIBERNATE	   */ { &worker::handle_message_queue_hibernate,	statistics::worker::NA },
	/* AWAKEN	   */ { &worker::handle_message_queue_awaken,		statistics::worker::NA },
	/* SHUTDOWN	   */ { &worker::handle_message_queue_shutdown,		statistics::worker::NA },
	/* NEW_CLIENT	   */ { &worker::handle_message_queue_new_client,	statistics::worker::MQ_NEW_CLIENT },
	/* HANDOFF_CLIENT  */ { &worker::handle_message_queue_handoff_client,	statistics::worker::MQ_HANDOFF_CLIENT },
	/* TAKEOVER_CLIENT */ { &worker::handle_message_queue_takeover_client,	statistics::worker::MQ_TAKEOVER_CLIENT },
	/* SHUTDOWN_CLIENT */ { &worker::handle_message_queue_shutdown_client,	statistics::worker::MQ_SHUTDOWN_CLIENT },
	/* SEND_PACKET	   */ { &worker::handle_message_queue_send_packet,	statistics::worker::MQ_SEND_PACKET },
	/* RELEASE_PACKET  */ { &worker::handle_message_queue_release_packet,	statistics::worker::MQ_RELEASE_PACKET }
      }
    };
    message request;
    uint64_t size, i;

    static_assert (static_cast<int> (message_type::START) == 0, "message_type must start at 0");
    static_assert (static_cast<int> (message_type::TYPE_COUNT) == handler.size (), "handler table size must match");
    static_assert (static_cast<int> (message_type::TYPE_COUNT) == 10, "this must be modified");

    i = 0;
    size = m_queue_size[static_cast<std::size_t> (type)].exchange (0, std::memory_order_acquire);
    m_stats.add (statistics::worker::MQ_REQUESTED, size);

    while (i++ < size && m_queue[static_cast<std::size_t> (type)].try_pop (request))
      {
#if !defined (NDEBUG)
	er_log_conn (__FILE__, __LINE__,
		     "recevied message_id = %lld, request_type = %d from message queue in the worker = %d\n", request.message_id,
		     request.type, m_index);
#endif
	if (! (message_type::START <= request.type && message_type::TYPE_COUNT > request.type))
	  {
	    er_log_conn (__FILE__, __LINE__,
			 "connection::worker->handle_message_queue: received unknown event from eventfd in the worker = %d\n", m_index);
	    assert_release (false);
	    continue;
	  }

	if (! (this->*handler[static_cast <std::size_t> (request.type)].first) (request))
	  {
	    return false;
	  }
	if (handler[static_cast <std::size_t> (request.type)].second != statistics::worker::NA)
	  {
	    m_stats.add (handler[static_cast <std::size_t> (request.type)].second, 1);
	  }
	m_stats.add (statistics::worker::MQ_COMPLETED, 1);
      }

    return true;
  }

  bool worker::handle_message_queue ()
  {
    std::size_t i;

    for (i = 0; i < static_cast<std::size_t> (queue_type::TYPE_COUNT); i++)
      {
	if (!this->handle_message_queue_by_index (static_cast<queue_type> (i)))
	  {
	    er_log_conn (__FILE__, __LINE__, "connection::worker->handle_message_queue: handle_message_queue_by_index failed\n");
	    return false;
	  }
      }

    /* the actual release of the context is handled last */
    this->purge_stale_contexts ();

    return true;
  }

  void worker::handle_hangup_or_error (context *ctx, bool err)
  {
    socklen_t length;
    int error;

    if (err)
      {
	error = 0;
	length = sizeof (error);
	if (getsockopt (ctx->m_conn->fd, SOL_SOCKET, SO_ERROR, &error, &length) == 0)
	  {
	    er_log_conn (__FILE__, __LINE__, "connection::worker->handle_hangup_or_error: socket error (EPOLLERR) on fd %d: %s",
			 ctx->m_conn->fd,
			 strerror (error));
	  }
	else
	  {
	    er_log_conn (__FILE__, __LINE__,
			 "connection::worker->handle_hangup_or_error: socket error (EPOLLERR) on fd %d, but getsockopt failed.",
			 ctx->m_conn->fd);
	  }
      }
    else
      {
	er_log_conn (__FILE__, __LINE__,
		     "connection::worker->handle_hangup_or_error: connection closed by peer (HUP/RDHUP) on fd %d.",
		     ctx->m_conn->fd);
      }

    /* ctx will be forcibly removed */
    ctx->m_ignore = ignore_level::IGNORE_ALL;

    this->handle_connection_close (ctx);
  }

  result worker::handle_error_packet (context *ctx, cubbase::span<std::byte> &packet)
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
		     "connection::worker->handle_error_packet: the expected size by header and packet size is different\n");
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

  result worker::handle_data_packet (context *ctx, cubbase::span<std::byte> &packet)
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
		     "connection::worker->handle_data_packet: the expected size by header and packet size is different\n");
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

  result worker::handle_command_header_packet (context *ctx)
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

  result worker::handle_header_packet (context *ctx, cubbase::span<std::byte> &packet)
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
		     "connection::worker->handle_header_packet: the expected size, sizeof (NET_HEADER) and packet size is different\n");
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
		     "connection::worker->handle_header_packet: unknown state - will be reset by skew handler\n");
	status = result::Skewed;
	break;
      }

    return status;
  }

  result worker::handle_packet (context *ctx, cubbase::span<std::byte> &packet)
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
	er_log_conn (ARG_FILE_LINE, "connection::worker->handle_packet: unknown state\n");
	assert_release (false);
	break;
      }

    return status;
  }

  result worker::handle_reception (context *ctx, bool in_exhausted)
  {
    std::chrono::time_point<std::chrono::steady_clock> start, end;
    std::vector<cubbase::span<std::byte>> *packets;
    result status, io_status;
    int mtx;

    rmutex_lock (m_entry, &ctx->m_conn->rmutex);
    if (ctx->m_conn->status != CONN_OPEN || ctx->m_conn->stop_talk == true)
      {
	rmutex_unlock (m_entry, &ctx->m_conn->rmutex);
	this->handle_connection_close (ctx);
	return result::ClosedConnection;
      }
    rmutex_unlock (m_entry, &ctx->m_conn->rmutex);

    io_status = ctx->m_recv.m_receiver.drain (ctx->m_conn->fd, m_recv_budget);
    if (io_status == result::PeerReset || io_status == result::Error)
      {
	er_log_conn (__FILE__, __LINE__, "connection::worker->handle_reception: status = %d\n", io_status);
	ctx->m_send.m_transmitter.empty ();
	this->handle_connection_close (ctx);
	return io_status;
      }

    assert (io_status == result::Pending || io_status == result::BudgetExhausted);

    if (!in_exhausted && io_status == result::BudgetExhausted)
      {
	handle_exhausted_add_context (ctx, EPOLLIN);
      }

    if (ctx->m_recv.m_receiver.get_result ()->empty ())
      {
	return io_status;
      }

    m_stats.add (statistics::worker::PACKET_COUNT, ctx->m_recv.m_receiver.get_result ()->size ());

    start = std::chrono::steady_clock::now ();

    /* hold m_conn */
    mtx = rmutex_lock (m_entry, &ctx->m_conn->rmutex);
    if (mtx != NO_ERROR)
      {
	return result::Error;
      }

    end = std::chrono::steady_clock::now ();
    m_stats.add (statistics::worker::BLOCKED_RMUTEX,
		 std::chrono::duration_cast<std::chrono::microseconds> (end - start).count ());

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
	    er_log_conn (__FILE__, __LINE__, "connection::worker->handle_reception: requested to close the connection\n");
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

    return io_status;
  }

  result worker::handle_transmission (context *ctx, bool in_exhausted)
  {
    result status;

    status = ctx->m_send.m_transmitter.fill (ctx->m_conn->fd, m_send_budget);
    if (status == result::PeerReset || status == result::Error)
      {
	er_log_conn (__FILE__, __LINE__, "connection::worker->handle_transmission: status = %d\n", status);

	/* ctx will be forcibly removed */
	ctx->m_ignore = ignore_level::IGNORE_ALL;

	this->wakeup_blocked_worker (ctx->m_send.m_blocker);
	this->handle_connection_close (ctx);
	return status;
      }

    assert (status == result::Ok || status == result::Pending || status == result::BudgetExhausted);

    if (status == result::Ok)
      {
	this->wakeup_blocked_worker (ctx->m_send.m_blocker);

	rmutex_lock (m_entry, &ctx->m_conn->rmutex);
	if (ctx->m_conn->status == CONN_CLOSING)
	  {
	    rmutex_unlock (m_entry, &ctx->m_conn->rmutex);
	    /* this transmission is the last handling on this connection */
	    this->handle_connection_close (ctx);
	    er_log_conn (__FILE__, __LINE__,
			 "connection::worker->handle_transmission: this transmission is the last handling on this connection: closed\n");
	    return result::ClosedConnection;
	  }
	rmutex_unlock (m_entry, &ctx->m_conn->rmutex);

	if (!m_events.modify_descriptor (ctx->m_conn->fd, EPOLLET | EPOLLIN | EPOLLRDHUP, ctx))
	  {
	    er_log_conn (__FILE__, __LINE__, "connection::worker->handle_transmission: modify_descriptor failed\n");

	    /* ctx will be forcibly removed */
	    ctx->m_ignore = ignore_level::IGNORE_ALL;

	    this->handle_connection_close (ctx);
	    return result::Error;
	  }
	ctx->m_send.m_transmitter.clear ();
	er_log_conn (__FILE__, __LINE__, "fully sent. fd = %d in the worker = %d\n", ctx->m_conn->fd, m_index);
      }
    else if (!in_exhausted && status == result::BudgetExhausted)
      {
	handle_exhausted_add_context (ctx, EPOLLOUT);
      }
    return status;
  }

  void worker::handle_exhausted_add_context (context *ctx, uint32_t event)
  {
    if (m_exhausted.find (ctx->m_id) == m_exhausted.end ())
      {
	m_exhausted[ctx->m_id].prepared = false;
	m_exhausted[ctx->m_id].events = event;
	m_exhausted[ctx->m_id].ctx = ctx;
      }
    else
      {
	assert (m_exhausted[ctx->m_id].ctx == ctx);
	m_exhausted[ctx->m_id].events |= event;
      }
    er_log_conn (__FILE__, __LINE__,
		 "connection::worker->handle_exhausted_add_context: add new context into exhausted list: fd = %d\n", ctx->m_conn->fd);
  }

  bool worker::handle_exhausted ()
  {
    result status;
    context *ctx;

    for (auto it = m_exhausted.begin (); it != m_exhausted.end (); )
      {
	if (!it->second.prepared)
	  {
	    it->second.prepared = true;
	    it++;
	    continue;
	  }

	ctx = it->second.ctx;
	ctx->m_stats.set (statistics::context::LAST_ACTIVE_NS, m_timens);

	if (it->second.events & EPOLLIN)
	  {
	    er_log_conn (__FILE__, __LINE__,
			 "connection::worker->handle_exhausted: try to receive from fd = %d\n", ctx->m_conn->fd);

	    status = this->handle_reception (ctx, true);
	    if (status == result::ClosedConnection || status == result::PeerReset)
	      {
		it = m_exhausted.erase (it);
		continue;
	      }
	    if (status == result::Error)
	      {
		er_log_conn (__FILE__, __LINE__, "connection::worker->handle_exhausted: handle_reception failed");
		return false;
	      }
	    if (status == result::Pending)
	      {
		er_log_conn (__FILE__, __LINE__,
			     "connection::worker->handle_exhausted: complete the reception: fd = %d\n", ctx->m_conn->fd);

		assert (it->second.events & EPOLLIN);

		it->second.events &= ~EPOLLIN;
		if (!it->second.events)
		  {
		    it = m_exhausted.erase (it);
		    er_log_conn (__FILE__, __LINE__,
				 "connection::worker->handle_exhausted: remove context from exhausted list: fd = %d\n", ctx->m_conn->fd);
		    continue;
		  }
	      }
	  }
	if (it->second.events & EPOLLOUT)
	  {
	    er_log_conn (__FILE__, __LINE__,
			 "connection::worker->handle_exhausted: try to send to fd = %d\n", ctx->m_conn->fd);

	    status = this->handle_transmission (ctx, true);
	    if (status == result::ClosedConnection || status == result::PeerReset)
	      {
		it = m_exhausted.erase (it);
		continue;
	      }
	    if (status == result::Error)
	      {
		er_log_conn (__FILE__, __LINE__, "connection::worker->handle_exhausted: handle_transmission failed");
		return false;
	      }
	    if (status == result::Ok || status == result::Pending)
	      {
		er_log_conn (__FILE__, __LINE__,
			     "connection::worker->handle_exhausted: complete the transmission: fd = %d\n", ctx->m_conn->fd);

		assert (it->second.events & EPOLLOUT);

		it->second.events &= ~EPOLLOUT;
		if (!it->second.events)
		  {
		    it = m_exhausted.erase (it);
		    er_log_conn (__FILE__, __LINE__,
				 "connection::worker->handle_exhausted: remove context from exhausted list: fd = %d\n", ctx->m_conn->fd);
		    continue;
		  }
	      }
	  }
	it++;
      }

    return true;
  }

  void worker::initialize ()
  {
    /* watch me */
    m_watcher->mtx.lock ();
    m_watcher->active++;
    m_watcher->mtx.unlock ();

    /* set name */
    pthread_setname_np (pthread_self (), "connections");

    /* pin myself */
    os::resources::cpu::setaffinity (m_core);

    /* entry */
    m_entry = cubthread::get_manager ()->claim_entry ();
    if (m_entry == nullptr)
      {
	er_log_conn (__FILE__, __LINE__, "connection::worker->initialize: claim_entry failed\n");
	assert_release (false);
      }
    m_entry->register_id ();
    m_entry->type = TT_SERVER;
    m_entry->tran_index = -1;
    m_entry->m_status = cubthread::entry::status::TS_RUN;
    m_entry->shutdown = false;

    m_entry->get_error_context ().register_thread_local ();

    m_context.clear ();
    m_removed_context.clear ();
  }

  void worker::finalize ()
  {
    /* alive context */
    for (auto &ctx : m_context)
      {
	ctx->m_ignore = ignore_level::IGNORE_ALL;
	this->handle_connection_close (ctx);
      }

    while (!m_context.empty ())
      {
	this->handle_message_queue_by_index (queue_type::LAZY);

	/* the actual release of the context is handled last */
	this->purge_stale_contexts ();

	/* 1 ms */
	thread_sleep (1);
      }
    m_context.clear ();

    m_entry->unregister_id ();
    cubthread::get_manager ()->retire_entry (*m_entry);

    /* remove the watcher */
    m_watcher->mtx.lock ();
    m_watcher->active--;
    m_watcher->mtx.unlock ();

    m_watcher->cv.notify_one ();
  }

  void worker::finalize_resources ()
  {
    message request;
    std::size_t i;

    for (i = 0; i < static_cast<std::size_t> (queue_type::TYPE_COUNT); i++)
      {
	while (m_queue[i].try_pop (request))
	  {
	    switch (request.type)
	      {
	      case message_type::SEND_PACKET:
		if (request.deleter)
		  {
		    request.deleter ();
		  }
		[[fallthrough]];

	      case message_type::SHUTDOWN_CLIENT:
		this->wakeup_blocked_worker (request.waiter_handle);
		break;

	      case message_type::NEW_CLIENT:
		css_free_conn (request.conn);
		m_parent->retire_context (request.ctx);
		break;

	      case message_type::TAKEOVER_CLIENT:
		css_free_conn (request.ctx->m_conn);
		m_parent->retire_context (request.ctx);
		break;

	      case message_type::START:
	      case message_type::HIBERNATE:
	      case message_type::AWAKEN:
	      case message_type::SHUTDOWN:
	      case message_type::HANDOFF_CLIENT:
	      case message_type::RELEASE_PACKET:
	      case message_type::TYPE_COUNT:
		break;
	      }
	  }
      }

    for (context *ctx : m_removed_context)
      {
	css_free_conn (ctx->m_conn);
	m_parent->retire_context (ctx);
      }
    m_removed_context.clear ();
  }

  bool worker::run ()
  {
    std::array<epoll_event, 512> events;
    bool eventfds[2] = { false, false };
    result status;
    context *ctx;
    int nfds, i;

    while (!m_stop)
      {
	nfds = m_events.wait (events.data (), events.size (), m_exhausted.empty () ? TIMEOUT_INFINITE : TIMEOUT_NOWAIT);
	if (nfds < 0)
	  {
	    if (errno == EINTR)
	      {
		continue;
	      }
	    er_log_conn (__FILE__, __LINE__, "master::connector->execute: m_events->wait failed: %s", strerror (errno));
	    assert_release (false);
	    continue;
	  }

	/* criterion time to use during this loop */
	m_timens = this->get_time_ns (CLOCK_MONOTONIC);

	for (i = 0; i < nfds; i++)
	  {
	    assert (events[i].data.ptr);

	    ctx = reinterpret_cast<context *> (events[i].data.ptr);
	    if ((events[i].events & (EPOLLHUP | EPOLLRDHUP | EPOLLERR)) &&
		ctx->m_conn->fd != m_eventfd &&
		ctx->m_conn->fd != m_timerfd)
	      {
		this->handle_hangup_or_error (ctx, events[i].events & EPOLLERR);
		continue;
	      }
	    ctx->m_stats.set (statistics::context::LAST_ACTIVE_NS, m_timens);
	    if (events[i].events & EPOLLIN)
	      {
		if (ctx->m_conn->fd == m_eventfd)
		  {
		    eventfds[0] = true;
		    continue;
		  }
		else if (ctx->m_conn->fd == m_timerfd)
		  {
		    eventfds[1] = true;
		    continue;
		  }
		status = this->handle_reception (ctx, false);
		if (status == result::ClosedConnection || status == result::PeerReset)
		  {
		    continue;
		  }
		if (status == result::Error)
		  {
		    er_log_conn (__FILE__, __LINE__, "connection::worker->run: handle_reception failed");
		    return false;
		  }
	      }
	    if (events[i].events & EPOLLOUT)
	      {
		status = this->handle_transmission (ctx, false);
		if (status == result::ClosedConnection || status == result::PeerReset)
		  {
		    continue;
		  }
		if (status == result::Error)
		  {
		    er_log_conn (__FILE__, __LINE__, "connection::worker->run: handle_transmission failed");
		    return false;
		  }
	      }
	  }

	/* exhausted */
	if (m_exhausted.size () > 0)
	  {
	    if (!this->handle_exhausted ())
	      {
		er_log_conn (__FILE__, __LINE__, "connection::worker->run: handle_exhausted failed");
		return false;
	      }
	  }

	/* lazy handling */
	if (eventfds[0] || eventfds[1])
	  {
	    if (!this->eventfd_handler (eventfds))
	      {
		er_log_conn (__FILE__, __LINE__, "connection::worker->run: eventfd_handler failed");
		return false;
	      }
	  }
      }

    return true;
  }

  void worker::attach ()
  {
    this->initialize ();
    this->run ();
    this->finalize ();
  }
}
