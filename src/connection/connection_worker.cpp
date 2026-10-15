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

  /* how long a waiting sender goes without re-checking its connection and the interrupt flag */
  static constexpr std::chrono::milliseconds SEND_QUEUE_ROOM_WAIT_TURN (100);

  /* why a sender stopped waiting; anything but ROOM_AVAILABLE is logged as the reason its message was dropped */
  enum class room_wait_result
  {
    ROOM_AVAILABLE,
    NOT_PERMITTED,
    DISABLED,
    NOT_INTERRUPTIBLE,
    TIMED_OUT,
    INTERRUPTED,
    RMUTEX_HELD,
    NO_THREAD_ENTRY,
    CONNECTION_GONE
  };

  static const char *
  room_wait_result_name (room_wait_result r)
  {
    /* no default, so -Wswitch flags a new value that has no reason string */
    switch (r)
      {
      case room_wait_result::ROOM_AVAILABLE:
	return "room is available";
      case room_wait_result::NOT_PERMITTED:
	return "only the callback channel may wait";
      case room_wait_result::DISABLED:
	return "waiting is off (send_queue_room_wait_msecs = 0)";
      case room_wait_result::NOT_INTERRUPTIBLE:
	return "an interrupt cannot reach this sender, so only the timeout could end the wait";
      case room_wait_result::TIMED_OUT:
	return "no room within send_queue_room_wait_msecs";
      case room_wait_result::INTERRUPTED:
	return "the query was interrupted";
      case room_wait_result::RMUTEX_HELD:
	return "the sender holds conn->rmutex and may not wait";
      case room_wait_result::NO_THREAD_ENTRY:
	return "the sender has no thread entry, so waiting cannot be shown to be safe";
      case room_wait_result::CONNECTION_GONE:
	return "the connection was closed, taken over or handed to another client";
      }

    assert (false);
    return "unknown";
  }

  /* waits for one more iovec of room; holds conn->cmutex except while asleep, which is all the drain needs */
  static room_wait_result
  wait_for_send_queue_room (cubthread::entry *thread_p, css_conn_entry *conn, context *ctx,
			    worker *&owner)
  {
    int budget_msecs = prm_get_integer_value (PRM_ID_CSS_SEND_QUEUE_ROOM_WAIT_MSECS);
    bool continue_checking = true;
    uint64_t ctx_id;
    int r;

    if (budget_msecs <= 0)
      {
	return room_wait_result::DISABLED;
      }

    if (thread_p == NULL)
      {
	return room_wait_result::NO_THREAD_ENTRY;
      }

    /* the event loop also takes rmutex, so waiting with it held can stall the drain this waits for */
    if (conn->rmutex.owner == thread_p->get_id ())
      {
	return room_wait_result::RMUTEX_HELD;
      }

    /* on a live connection only a cancel or a query timeout ends the wait early, and neither reaches a transaction that
     * is not active */
    if (!logtb_is_current_active (thread_p))
      {
	return room_wait_result::NOT_INTERRUPTIBLE;
      }

    /* conn and ctx are recycled together through LIFO free lists, so m_id tells whether ctx still serves this client */
    ctx_id = ctx->m_id;

    std::chrono::steady_clock::time_point deadline =
	    std::chrono::steady_clock::now () + std::chrono::milliseconds (budget_msecs);

    for (;;)
      {
	std::chrono::steady_clock::time_point now;
	std::chrono::milliseconds remaining;
	std::chrono::milliseconds turn;

	/* Checked before every sleep, so a sender arriving after teardown's wake does not sleep a turn on a closing
	 * connection. A handoff only changes the owner, so adopt it. conn->context != ctx comes before any read of ctx,
	 * which may have been retired while this thread slept. */
	owner = conn->worker;
	if (owner == nullptr || conn->context != ctx || ctx->m_id != ctx_id || ctx->m_conn != conn
	    || IS_INVALID_SOCKET (conn->fd) || conn->status != CONN_OPEN
	    || ctx->m_ignore != ignore_level::DONT_IGNORE)
	  {
	    return room_wait_result::CONNECTION_GONE;
	  }

	if (!logtb_is_current_active (thread_p))
	  {
	    return room_wait_result::NOT_INTERRUPTIBLE;
	  }

	/* A cancel does not close the connection, so it is only visible here. */
	if (logtb_is_interrupted (thread_p, false, &continue_checking))
	  {
	    return room_wait_result::INTERRUPTED;
	  }

	if (ctx->m_send.m_transmitter.prepare_append (1))
	  {
	    return room_wait_result::ROOM_AVAILABLE;
	  }

	now = std::chrono::steady_clock::now ();
	if (now >= deadline)
	  {
	    return room_wait_result::TIMED_OUT;
	  }

	remaining = std::chrono::duration_cast<std::chrono::milliseconds> (deadline - now);
	turn = (remaining < SEND_QUEUE_ROOM_WAIT_TURN) ? remaining : SEND_QUEUE_ROOM_WAIT_TURN;

	std::shared_ptr<message_blocker> waiter = ctx->m_send.m_room;

	/* A woken signal is not ours to wait on -- every waker moves it out of the context. */
	if (waiter == nullptr || waiter->done)
	  {
	    waiter = std::make_shared<message_blocker> ();
	    waiter->done = false;
	    ctx->m_send.m_room = waiter;
	  }

	r = rmutex_unlock (NULL, &conn->cmutex);
	assert (r == NO_ERROR);

	{
	  std::unique_lock<std::mutex> lock (waiter->m);
	  waiter->cv.wait_for (lock, turn, [&waiter] { return waiter->done; });
	}

	r = rmutex_lock (NULL, &conn->cmutex);
	assert (r == NO_ERROR);
      }
  }

  unsigned int
  worker::send_packet (css_conn_entry *conn, const cubbase::span<std::byte> *packet, std::size_t packet_count,
		       const bool *retain_packet, std::function<void ()> &&deleter, int wait_time,
		       bool may_wait_for_room)
  {
    std::array<uint32_t, MAX_DIRECT_PACKET_COUNT> record_size;
    std::array<struct iovec, MAX_DIRECT_PACKET_COUNT * 2> wire;
    std::array<bool, MAX_DIRECT_PACKET_COUNT * 2> wire_owned {};
    std::array<cubbase::span<std::byte>, MAX_DIRECT_PACKET_COUNT * 2> pending;
    std::array<std::byte *, MAX_DIRECT_PACKET_COUNT * 2> allocated {};
    std::shared_ptr<message_blocker> failed_waiter;
    std::shared_ptr<message_blocker> failed_room;
    std::shared_ptr<message_blocker> waiter;
    struct msghdr msg = {};
    worker *owner;
    context *ctx;
    std::size_t allocated_count = 0;
    std::size_t pending_count = 0;
    std::size_t wire_count = 0;
    std::size_t total_size = 0;
    std::size_t sent_size = 0;
    std::size_t pending_size = 0;
    ssize_t bytes = -1;
    bool retain_deleter = false;
    int send_errno = 0;
    int r;
    auto release_allocated = [&allocated, &allocated_count] ()
    {
      for (std::size_t allocation = 0; allocation < allocated_count; allocation++)
	{
	  delete [] allocated[allocation];
	}
      allocated_count = 0;
    };

    assert (conn != nullptr);
    assert (packet != nullptr);
    assert (packet_count > 0 && packet_count <= MAX_DIRECT_PACKET_COUNT);

    for (std::size_t index = 0; index < packet_count; index++)
      {
	assert (packet[index].size () <= static_cast<std::size_t> (INT_MAX));
	record_size[index] = htonl (static_cast<uint32_t> (packet[index].size ()));
	wire[wire_count++] = { &record_size[index], sizeof (record_size[index]) };
	total_size += sizeof (record_size[index]);
	if (packet[index].size () > 0)
	  {
	    wire[wire_count] = { packet[index].data (), packet[index].size () };
	    wire_owned[wire_count++] = retain_packet != nullptr && retain_packet[index] && deleter;
	    total_size += packet[index].size ();
	  }
      }

    r = rmutex_lock (NULL, &conn->cmutex);
    assert (r == NO_ERROR);

    owner = conn->worker;
    ctx = reinterpret_cast<context *> (conn->context);
    if (owner == nullptr || ctx == nullptr || IS_INVALID_SOCKET (conn->fd))
      {
	r = rmutex_unlock (NULL, &conn->cmutex);
	assert (r == NO_ERROR);
	if (deleter)
	  {
	    deleter ();
	  }
	return NO_ERROR;
      }

    if (ctx->m_send.m_transmitter.empty ())
      {
	msg.msg_iov = wire.data ();
	msg.msg_iovlen = wire_count;
	do
	  {
	    bytes = ::sendmsg (conn->fd, &msg, MSG_NOSIGNAL | MSG_DONTWAIT);
	  }
	while (bytes < 0 && errno == EINTR);

	if (bytes > 0)
	  {
	    sent_size = static_cast<std::size_t> (bytes);
	    ctx->m_guarded_stats.bytes_out_total += sent_size;
	  }
	else if (bytes < 0)
	  {
	    send_errno = errno;
	  }
      }
    else
      {
	/* Preserve the bytes already queued for this connection. */
	send_errno = EAGAIN;
      }

    if (sent_size == total_size)
      {
	r = rmutex_unlock (NULL, &conn->cmutex);
	assert (r == NO_ERROR);
	if (deleter)
	  {
	    deleter ();
	  }
	return NO_ERROR;
      }

    assert (sent_size < total_size);

    if (bytes == 0 || (bytes < 0 && send_errno != EAGAIN
#if defined (EWOULDBLOCK) && EWOULDBLOCK != EAGAIN
		       && send_errno != EWOULDBLOCK
#endif
		      ))
      {
	r = rmutex_unlock (NULL, &conn->cmutex);
	assert (r == NO_ERROR);
	if (deleter)
	  {
	    deleter ();
	  }
	css_request_shutdown_conn (conn, static_cast<uint8_t> (ignore_level::IGNORE_ALL), false, 0);
	return ERROR_ON_WRITE;
      }

    {
      std::size_t skip = sent_size;

      for (std::size_t index = 0; index < wire_count; index++)
	{
	  std::size_t available = wire[index].iov_len;
	  std::byte *source = static_cast<std::byte *> (wire[index].iov_base);
	  std::byte *copy;

	  if (skip >= available)
	    {
	      skip -= available;
	      continue;
	    }
	  source += skip;
	  available -= skip;
	  skip = 0;
	  pending_size += available;

	  if (wire_owned[index])
	    {
	      pending[pending_count++] = { source, available };
	      retain_deleter = true;
	      continue;
	    }

	  copy = new std::byte[available];
	  if (copy == nullptr)
	    {
	      release_allocated ();
	      r = rmutex_unlock (NULL, &conn->cmutex);
	      assert (r == NO_ERROR);
	      if (deleter)
		{
		  deleter ();
		}
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, available);
	      css_request_shutdown_conn (conn, static_cast<uint8_t> (ignore_level::IGNORE_ALL), false, 0);
	      return CANT_ALLOC_BUFFER;
	    }
	  std::memcpy (copy, source, available);
	  allocated[allocated_count++] = copy;
	  pending[pending_count++] = { copy, available };
	}
      assert (skip == 0);
      assert (pending_size == total_size - sent_size);
    }

    if (!ctx->m_send.m_transmitter.prepare_append (pending_count))
      {
	std::byte *coalesced;

	if (!ctx->m_send.m_transmitter.prepare_append (1))
	  {
	    room_wait_result waited = may_wait_for_room
				      ? wait_for_send_queue_room (thread_get_thread_entry_info (), conn, ctx, owner)
				      : room_wait_result::NOT_PERMITTED;

	    if (waited != room_wait_result::ROOM_AVAILABLE)
	      {
		release_allocated ();
		r = rmutex_unlock (NULL, &conn->cmutex);
		assert (r == NO_ERROR);
		if (deleter)
		  {
		    deleter ();
		  }

		/* unconditional: it is the only record of why a send failed */
		_er_log_debug (ARG_FILE_LINE, "send queue full for connection %d: %s\n", conn->idx,
			       room_wait_result_name (waited));

		if (waited == room_wait_result::CONNECTION_GONE)
		  {
		    /* like the entry check: the connection is closing or already reused, so conn is not ours to close */
		    return NO_ERROR;
		  }

		/* a dropped message must cost the connection, or its client waits for it forever */
		css_request_shutdown_conn (conn, static_cast<uint8_t> (ignore_level::IGNORE_ALL), false, 0);
		return INTERNAL_CSS_ERROR;
	      }
	  }

	release_allocated ();
	pending_count = 0;
	retain_deleter = false;

	coalesced = new std::byte[pending_size];
	if (coalesced == nullptr)
	  {
	    r = rmutex_unlock (NULL, &conn->cmutex);
	    assert (r == NO_ERROR);
	    if (deleter)
	      {
		deleter ();
	      }
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, pending_size);
	    css_request_shutdown_conn (conn, static_cast<uint8_t> (ignore_level::IGNORE_ALL), false, 0);
	    return CANT_ALLOC_BUFFER;
	  }

	std::size_t skip = sent_size;
	std::size_t copied = 0;
	for (std::size_t index = 0; index < wire_count; index++)
	  {
	    std::size_t available = wire[index].iov_len;
	    std::byte *source = static_cast<std::byte *> (wire[index].iov_base);

	    if (skip >= available)
	      {
		skip -= available;
		continue;
	      }
	    source += skip;
	    available -= skip;
	    skip = 0;
	    std::memcpy (coalesced + copied, source, available);
	    copied += available;
	  }
	assert (skip == 0 && copied == pending_size);
	allocated[allocated_count++] = coalesced;
	pending[pending_count++] = { coalesced, pending_size };
      }

    for (std::size_t index = 0; index < pending_count; index++)
      {
	ctx->m_send.m_transmitter.push (pending[index]);
      }
    for (std::size_t index = 0; index < allocated_count; index++)
      {
	std::byte *copy = allocated[index];
	ctx->m_send.m_transmitter.push_for_deleter ([copy] ()
	{
	  delete [] copy;
	});
      }
    if (retain_deleter)
      {
	ctx->m_send.m_transmitter.push_for_deleter (std::move (deleter));
      }
    ctx->m_send.m_transmitter.stamp ();

    /* A context between handoff and takeover is not registered yet. Takeover
     * observes the queued transmitter and registers EPOLLOUT. */
    if (ctx->m_epoll_registered
	&& !owner->m_events.modify_descriptor (conn->fd, EPOLLET | EPOLLIN | EPOLLOUT | EPOLLRDHUP, ctx))
      {
	ctx->m_send.m_transmitter.clear ();
	failed_waiter = std::move (ctx->m_send.m_blocker);
	failed_room = std::move (ctx->m_send.m_room);
	r = rmutex_unlock (NULL, &conn->cmutex);
	assert (r == NO_ERROR);
	if (!retain_deleter && deleter)
	  {
	    deleter ();
	  }
	owner->wakeup_blocked_worker (failed_waiter);
	owner->wakeup_blocked_worker (failed_room);
	css_request_shutdown_conn (conn, static_cast<uint8_t> (ignore_level::IGNORE_ALL), false, 0);
	return INTERNAL_CSS_ERROR;
      }

    if (wait_time != 0)
      {
	waiter = ctx->m_send.m_blocker;
	if (waiter == nullptr)
	  {
	    waiter = std::make_shared<message_blocker> ();
	    waiter->done = false;
	    ctx->m_send.m_blocker = waiter;
	  }
      }

    r = rmutex_unlock (NULL, &conn->cmutex);
    assert (r == NO_ERROR);

    if (!retain_deleter && deleter)
      {
	deleter ();
      }

    if (waiter != nullptr)
      {
	std::unique_lock<std::mutex> lock (waiter->m);
	if (wait_time < 0)
	  {
	    waiter->cv.wait (lock, [&waiter] { return waiter->done; });
	  }
	else
	  {
	    waiter->cv.wait_for (lock, std::chrono::seconds (wait_time), [&waiter] { return waiter->done; });
	  }
      }

    return NO_ERROR;
  }

  /* Receive the next request of this connection on the calling thread.
   *   1. Claim the socket; give up when it is busy or the connection is closing.
   *   2. Wait up to window_ms for the next request.
   *   3. Receive what came in, or leave an error to the worker.
   *   4. Give the socket back. m_eager_running is lowered now,
   *      or in eager_finish () once the caller has run what was received.
   * When count_out > 0, the caller runs that many requests, then calls eager_finish (ctx_out).
   * Returns whether the caller may wait again. */
  bool worker::eager_poll_and_receive (css_conn_entry &conn, cubthread::entry *entry, int window_ms, int &count_out,
				       context *&ctx_out)
  {
    worker *owner;
    context *ctx;
    struct pollfd po;
    SOCKET fd;
    result status;
    int n;
    bool more_data, hand_back;

    count_out = 0;
    ctx_out = nullptr;

    /* STEP 1. Claim the socket; give up when it is busy or the connection is closing. */
    ctx = eager_claim (conn, entry, owner, fd);
    if (ctx == nullptr)
      {
	return false;
      }

    /* STEP 2. Wait up to window_ms for the next request. */
    po.fd = fd;
    po.events = POLLIN;
    po.revents = 0;
    n = poll (&po, 1, window_ms);

    /* STEP 3. Receive what came in, or leave an error to the worker. */
    status = result::Ok;
    more_data = false;
    hand_back = false;

    if (n > 0 && (po.revents & POLLIN) != 0)
      {
	/* owner may have changed since the claim; eager_drain () needs no worker state */
	status = owner->eager_drain (ctx, entry, count_out, more_data);
	hand_back = (status != result::Ok) || more_data;
      }
    else if (n > 0 || (n < 0 && errno != EINTR))
      {
	/* POLLERR, POLLHUP, POLLNVAL or a failed poll: leave the close to the worker */
	status = result::Error;
	hand_back = true;
      }

    /* STEP 4. Give the socket back. m_eager_running is lowered now,
     * or in eager_finish () once the caller has run what was received. */
    hand_back = eager_release (conn, entry, ctx, fd, hand_back);
    if (count_out == 0)
      {
	ctx->m_recv.m_eager_running.fetch_sub (1, std::memory_order_release);
      }
    else
      {
	ctx_out = ctx;
      }

    return !hand_back && status == result::Ok && count_out > 0;
  }

  /* lower m_eager_running; ctx is passed in because a failed takeover clears conn.context */
  void worker::eager_finish (context *ctx)
  {
    ctx->m_recv.m_eager_running.fetch_sub (1, std::memory_order_release);
  }

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
    m_notified (false),
    m_has_retry (false),
    m_closing_drain (false)
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

    /* release the dummy contexts registered for m_eventfd and m_timerfd.
     * they are not tracked in m_context, so nothing else releases them.
     */
    for (int i = 0; i < DIM (m_eventfd_contexts); i++)
      {
	if (m_eventfd_contexts[i])
	  {
	    (void) m_events.remove_descriptor ((i == 0) ? m_eventfd : m_timerfd);
	    /* m_conn is not a real connection entry. see eventfd_register (). */
	    if (m_eventfd_contexts[i]->m_conn)
	      {
		delete reinterpret_cast<int *> (m_eventfd_contexts[i]->m_conn);
	      }
	    delete m_eventfd_contexts[i];
	  }
      }

    if (m_eventfd != -1)
      {
	::close (m_eventfd);
      }
    if (m_timerfd != -1)
      {
	::close (m_timerfd);
      }

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

    if (m_notified.exchange (true, std::memory_order_acq_rel))
      {
	return true;
      }

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
	    m_notified.store (false, std::memory_order_release);
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
	m_notified.store (false, std::memory_order_release);
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
    /* you must implement a logic to use a waiter whose message type is not in above */

    assert (!wait_time ||
	    (item.type == message_type::START ||
	     item.type == message_type::SHUTDOWN_CLIENT));

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
    cubthread::task_submission_options options;

    if (ctx->m_recv.m_eager_draining)
      {
	if ((ctx->m_recv.m_command_flags & NET_HEADER_FLAG_METHOD_MODE) == 0)
	  {
	    /* the eager receiver runs this request itself */
	    ctx->m_conn->add_pending_request ();
	    ctx->m_recv.m_eager_counted++;
	    ctx->m_recv.m_command_flags = 0;
	    return;
	  }

	/* a method-mode packet needs the pool's admission, and the rest of this drain follows it
	 * or the two would pop from conn->request_queue against each other */
	ctx->m_recv.m_eager_draining = false;
      }

    if ((ctx->m_recv.m_command_flags & NET_HEADER_FLAG_METHOD_MODE) != 0
	&& ctx->m_conn->has_outstanding_method_callback ())
      {
	options.admission = cubthread::task_admission::blocking_continuation;
      }

    /* push new task into worker pool */
    css_push_server_task (*ctx->m_conn, options);
    ctx->m_recv.m_command_flags = 0;
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
	/* blocker may be shared by every sender blocked on the same transmitter */
	handle->cv.notify_all ();
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

  bool worker::must_defer_close_for_eager_receiver (context *ctx)
  {
    bool busy;
    int r;

    if (prm_get_integer_value (PRM_ID_CSS_EAGER_RECEIVE_WINDOW_IN_MSECS) <= 0)
      {
	return false;
      }
    if (m_status != status::TERMINATING && !css_is_shutdowning_server ())
      {
	/* the eager receiver carries no transaction index; it runs inside a working task */
	return ctx->m_conn->has_working_task ();
      }

    /* During shutdown queued tasks retire without end_working_task (), so the close waits on m_eager_running.
     * eager_claim () checks CONN_OPEN under this cmutex, so the count cannot be raised after the read.
     * m_recv_busy is not read: a close from handle_reception () would wait on this worker's own claim. */
    r = rmutex_lock (m_entry, &ctx->m_conn->cmutex);
    assert (r == NO_ERROR);
    busy = ctx->m_recv.m_eager_running.load (std::memory_order_acquire) > 0;
    r = rmutex_unlock (m_entry, &ctx->m_conn->cmutex);
    assert (r == NO_ERROR);

    return busy;
  }

  bool worker::has_remaining_tasks (context *ctx)
  {
    bool has_pending_transmission, save_closing_drain;
    int r;

    /* handle the entries in the message queue as there may be a queued request to release the memory in ctx */
    save_closing_drain = m_closing_drain;	/* save */
    m_closing_drain = true;
    this->handle_message_queue_by_index (queue_type::IMMEDIATE);
    m_closing_drain = save_closing_drain;	/* restore */

    r = rmutex_lock (m_entry, &ctx->m_conn->cmutex);
    assert (r == NO_ERROR);
    has_pending_transmission = (ctx->m_ignore < ignore_level::IGNORE_ALL && !ctx->m_send.m_transmitter.empty ());
    r = rmutex_unlock (m_entry, &ctx->m_conn->cmutex);
    assert (r == NO_ERROR);

    if (has_pending_transmission)
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
    std::shared_ptr<message_blocker> transmission_blocker;
    std::shared_ptr<message_blocker> room;
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
	/* Interrupt the load session here (before draining workers below), but defer
	 * its destruction until the workers have drained; see the destroy call in the
	 * "remove and close" section. */
	ssession_interrupt_attached_threads (m_entry, ctx->m_conn->session_p);
      }

    if (!is_retry)
      {
	/* interrupt and wake up */

	net_server_wakeup_workers (m_entry, tran_index, client_id);
      }

    /* A room waiter is one of the workers counted below, so the clear further down would come too late to wake it.
     * CONN_CLOSING is already set, so a sender is either woken here or sees it before it sleeps. */
    rmutex_lock (m_entry, &ctx->m_conn->cmutex);
    room = std::move (ctx->m_send.m_room);
    rmutex_unlock (m_entry, &ctx->m_conn->cmutex);
    this->wakeup_blocked_worker (room);

    /* retry until the worker related to the connection is complete */

    if (net_server_active_workers (m_entry, ctx->m_conn, tran_index, client_id) > 0)
      {
	er_log_conn (__FILE__, __LINE__,
		     "connection::worker->handle_connection_close: net_server_active_workers. conn = %p, fd = %d\n", ctx->m_conn,
		     ctx->m_conn->fd);
	goto retry;
      }

    /* an eager receiver may still use this context; closing now would retire it under that thread */
    if (this->must_defer_close_for_eager_receiver (ctx))
      {
	er_log_conn (__FILE__, __LINE__,
		     "connection::worker->handle_connection_close: eager receiver holds conn = %p, fd = %d\n", ctx->m_conn,
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

    /* a context closed while has_remaining_tasks () pumped the queue must not be retired twice */
    if (ctx->m_removed)
      {
	this->wakeup_blocked_worker (handle);
	er_log_conn (__FILE__, __LINE__,
		     "connection::worker->handle_connection_close: closed while draining the queue. conn = %p\n", ctx->m_conn);
	return true;
      }

    /* this context has no remaining tasks */

    _er_log_debug (ARG_FILE_LINE,
		   "handle_connection_close: conn %p { fd %d status %d transaction_id %d db_error %d stop_talk %d stop_phase %d }\n",
		   ctx->m_conn, ctx->m_conn->fd, status, tran_index, ctx->m_conn->db_error, ctx->m_conn->stop_talk,
		   ctx->m_conn->stop_phase);

    /* remove and close */

    /* Connection workers have drained; it is now safe to destroy the load session
     * that was interrupted at the start of the close. */
    if (ctx->m_conn->session_p != NULL)
      {
	ssession_destroy_load_session (m_entry, ctx->m_conn->session_p);
      }

    m_events.remove_descriptor (ctx->m_conn->fd);
    if (tran_index != NULL_TRAN_INDEX)
      {
	net_server_conn_down (m_entry, tran_index);
      }

    this->end_connection_close ();

    start = std::chrono::steady_clock::now ();

    rmutex_lock (m_entry, &ctx->m_conn->cmutex);

    ctx->m_epoll_registered = false;
    ctx->m_send.m_transmitter.clear ();
    transmission_blocker = std::move (ctx->m_send.m_blocker);
    ctx->m_conn->worker = nullptr;
    ctx->m_conn->context = nullptr;

    /* Serialize descriptor invalidation with direct send and control paths. */
    css_shutdown_socket (ctx->m_conn->fd);
    ctx->m_conn->fd = INVALID_SOCKET;

    rmutex_unlock (m_entry, &ctx->m_conn->cmutex);

    end = std::chrono::steady_clock::now ();
    m_stats.add (statistics::worker::BLOCKED_RMUTEX,
		 std::chrono::duration_cast<std::chrono::microseconds> (end - start).count ());

    /* wake up the thread blocked until this request is complete */
    this->wakeup_blocked_worker (handle);
    this->wakeup_blocked_worker (transmission_blocker);

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
    int r;

    message.type = coordinator::message_type::STATISTICS;

    message.statistics.cpu_time_ns = get_time_ns (CLOCK_THREAD_CPUTIME_ID);
    message.statistics.time_ns = get_time_ns (CLOCK_MONOTONIC);
    message.statistics.worker.first = m_index;
    message.statistics.worker.second = m_stats;
    message.statistics.contexts.reserve (m_context.size ());
    for (context *ctx : m_context)
      {
	/* stats */
	uint64_t bytes_out_total;

	r = rmutex_lock (m_entry, &ctx->m_conn->cmutex);
	assert (r == NO_ERROR);

	bytes_out_total = ctx->m_guarded_stats.bytes_out_total;
	ctx->m_guarded_stats.bytes_out_total = 0;

	r = rmutex_unlock (m_entry, &ctx->m_conn->cmutex);
	assert (r == NO_ERROR);

	/* integrate direct send stats to owned stats */
	ctx->m_stats.add (statistics::context::BYTES_OUT_TOTAL, bytes_out_total);

	/* store */
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
    if (!ctx)
      {
	er_log_conn (__FILE__, __LINE__, "connection::worker->eventfd_register: failed to allocate memory\n");
	return false;
      }

    /* the eventfd has no connection entry. keep the descriptor itself in m_conn so that
     * ctx->m_conn->fd reads back the eventfd (fd is the first member of css_conn_entry).
     */
    conn = reinterpret_cast<css_conn_entry *> (new int { fd });
    if (!conn)
      {
	er_log_conn (__FILE__, __LINE__, "connection::worker->eventfd_register: failed to allocate memory\n");
	delete ctx;
	return false;
      }
    ctx->m_conn = conn;

    if (!m_events.add_descriptor (fd, EPOLLET | EPOLLIN, ctx))
      {
	er_log_conn (__FILE__, __LINE__, "connection::worker->eventfd_register: add_descriptor failed\n");

	delete reinterpret_cast<int *> (conn);
	delete ctx;
	return false;
      }

    /* epoll holds ctx as its user data. release it in the destructor. */
    m_eventfd_contexts[ (fd == m_eventfd) ? 0 : 1] = ctx;

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

	/* RMW closes the StoreLoad window */
	m_notified.exchange (false, std::memory_order_acq_rel);
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

  bool worker::handle_message_queue_release_packet (message &item)
  {
    context *ctx;
    css_conn_entry *conn;
    int r;

    assert (item.conn);
    assert (item.packet);

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

    ctx->m_recv.m_receiver.release (item.packet);
    er_log_conn (__FILE__, __LINE__,
		 "connection::worker->handle_message_queue_release_packet: release packet pointer = %p\n", item.packet);

    r = rmutex_unlock (m_entry, &item.conn->cmutex);
    assert (r == NO_ERROR);

    return true;
  }

  /* Receive again from a socket an eager receiver handed back.
   *   1. Look up the context under cmutex; discard the message when the connection has none.
   *   2. Discard a message meant for an older context, or forward it to the connection's new worker.
   *   3. Put the message back when nested; the next queue pass serves it outside both cases.
   *   4. Receive from the socket, unless the eager receiver holds it again. */
  bool worker::handle_message_queue_recv_recheck (message &item)
  {
    context *ctx;
    css_conn_entry *conn;
    result status;
    int r;
    bool held_by_outer_handler;

    assert (item.conn);

    /* STEP 1. Look up the context under cmutex; discard the message when the connection has none. */
    r = rmutex_lock (m_entry, &item.conn->cmutex);
    assert (r == NO_ERROR);

    ctx = reinterpret_cast<context *> (item.conn->context);
    if (ctx == nullptr)
      {
	r = rmutex_unlock (m_entry, &item.conn->cmutex);
	assert (r == NO_ERROR);
	return true;
      }

    /* STEP 2. Discard a message meant for an older context, or forward it to the connection's new worker. */
    conn = item.conn;
    if (!this->validate_message_generation (item, ctx)
	|| this->forward_message_to_successor (queue_type::IMMEDIATE, item, ctx))
      {
	r = rmutex_unlock (m_entry, &conn->cmutex);
	assert (r == NO_ERROR);
	return true;
      }

    /* STEP 3. Put the message back when nested; the next queue pass serves it outside both cases.
     * Under cmutex (handle_message_queue_handoff_client ()), handle_reception () would take rmutex
     * while a transaction thread holding rmutex waits for cmutex in css_request_release_packet ().
     * Under a close (has_remaining_tasks ()), a nested close would clear the m_entry client info
     * the outer close needs to release its transaction. */
    held_by_outer_handler = (conn->cmutex.lock_cnt > 1) || m_closing_drain;
    r = rmutex_unlock (m_entry, &conn->cmutex);
    assert (r == NO_ERROR);
    if (held_by_outer_handler)
      {
	if (!this->enqueue_and_notify (queue_type::IMMEDIATE, std::move (item)))
	  {
	    assert_release (false);
	  }
	return true;
      }

    /* STEP 4. Receive from the socket, unless the eager receiver holds it again.
     * Not from_edge: this message is the handover itself. */
    status = this->receive_if_free (ctx, false, false);
    if (status == result::Error)
      {
	er_log_conn (__FILE__, __LINE__, "connection::worker->handle_message_queue_recv_recheck: handle_reception failed\n");
	return false;
      }
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
	ctx->m_epoll_registered = false;
	rmutex_unlock (NULL, &ctx->m_conn->cmutex);

	m_removed_context.push_back (ctx);
	er_log_conn (__FILE__, __LINE__, "connection::worker->handle_message_queue_new_client: add_descriptor failed\n");
	return false;
      }
    ctx->m_epoll_registered = true;
    if (!m_context.insert (ctx).second)
      {
	ctx->m_conn->worker = nullptr;
	ctx->m_conn->context = nullptr;
	ctx->m_epoll_registered = false;
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
    ctx->m_epoll_registered = false;
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
	ctx->m_epoll_registered = false;
	rmutex_unlock (NULL, &ctx->m_conn->cmutex);

	m_removed_context.push_back (ctx);
	er_log_conn (__FILE__, __LINE__, "connection::worker->handle_message_queue_new_client: add_descriptor failed\n");
	return false;
      }
    ctx->m_epoll_registered = true;
    if (!m_context.insert (ctx).second)
      {
	ctx->m_conn->worker = nullptr;
	ctx->m_conn->context = nullptr;
	ctx->m_epoll_registered = false;
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
	/* RELEASE_PACKET  */ { &worker::handle_message_queue_release_packet,	statistics::worker::MQ_RELEASE_PACKET },
	/* RECV_RECHECK    */ { &worker::handle_message_queue_recv_recheck,	statistics::worker::MQ_RECV_RECHECK }
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

    conn = ctx->m_conn;
    header = &ctx->m_recv.m_header;

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
    ctx->m_recv.m_command_flags = 0;
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

    conn = ctx->m_conn;
    header = &ctx->m_recv.m_header;

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

  result worker::handle_command_header_packet (context *ctx, cubbase::span<std::byte> &packet)
  {
    css_conn_entry *conn;
    NET_HEADER *header;
    css_error_code error;

    if (css_is_request_aborted (ctx->m_conn, ctx->m_recv.m_request_id))
      {
	ctx->m_recv.m_command_flags = 0;
	ctx->m_recv.m_receiver.release (packet.data ());
	return result::Aborted;
      }

    assert (packet.size () == sizeof (NET_HEADER));

    conn = ctx->m_conn;
    header = reinterpret_cast<NET_HEADER *> (packet.data ());

    error = css_add_queue_entry (conn, &conn->request_queue, ctx->m_recv.m_request_id,
				 reinterpret_cast<char *> (packet.data ()), packet.size (), NO_ERRORS,
				 conn->get_tran_index (), conn->invalidate_snapshot, conn->db_error);
    if (error != NO_ERRORS)
      {
	ctx->m_recv.m_command_flags = 0;
	ctx->m_recv.m_receiver.release (packet.data ());
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

    std::memcpy (&ctx->m_recv.m_header, packet.data (), sizeof (NET_HEADER));

    conn = ctx->m_conn;
    header = &ctx->m_recv.m_header;

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
	ctx->m_recv.m_command_flags = flags;
	status = this->handle_command_header_packet (ctx, packet);
	break;

      case DATA_TYPE:
	ctx->m_recv.m_receiver.release (packet.data ());
	NEXT_STATE (ctx, m_recv, DATA);
	break;

      case ABORT_TYPE:
	/* no more packets are requested */
	ctx->m_recv.m_receiver.release (packet.data ());
	ctx->m_recv.m_command = false;
	ctx->m_recv.m_command_flags = 0;
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
    std::shared_ptr<message_blocker> transmission_blocker;
    std::shared_ptr<message_blocker> room;
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
	rmutex_lock (m_entry, &ctx->m_conn->cmutex);
	ctx->m_send.m_transmitter.clear ();
	transmission_blocker = std::move (ctx->m_send.m_blocker);
	room = std::move (ctx->m_send.m_room);
	rmutex_unlock (m_entry, &ctx->m_conn->cmutex);
	this->wakeup_blocked_worker (transmission_blocker);
	this->wakeup_blocked_worker (room);
	this->handle_connection_close (ctx);
	return io_status == result::PeerReset ? result::PeerReset : result::ClosedConnection;
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

  /* Take this connection's socket for the worker.
   *   1. Skip the claim with the window 0; no transaction thread takes the socket then.
   *   2. Under cmutex, set m_recv_busy when the socket is free.
   *   3. When it is busy and from_edge is set, record the dropped edge in m_missed_edge
   *      so the eager receiver rechecks on release.
   * Returns whether the worker may receive. */
  bool worker::claim_reading (context *ctx, bool from_edge)
  {
    bool claimed;
    int r;

    /* STEP 1. Skip the claim with the window 0; no transaction thread takes the socket then. */
    if (prm_get_integer_value (PRM_ID_CSS_EAGER_RECEIVE_WINDOW_IN_MSECS) <= 0)
      {
	return true;
      }

    /* STEP 2. Under cmutex, set m_recv_busy when the socket is free. */
    r = rmutex_lock (m_entry, &ctx->m_conn->cmutex);
    assert (r == NO_ERROR);
    claimed = !ctx->m_recv.m_recv_busy.load (std::memory_order_acquire);
    if (claimed)
      {
	ctx->m_recv.m_recv_busy.store (true, std::memory_order_relaxed);
      }
    else if (from_edge)
      {
	/* STEP 3. When it is busy and from_edge is set, record the dropped edge in m_missed_edge
	 * so the eager receiver rechecks on release. */
	ctx->m_recv.m_missed_edge = true;
      }
    r = rmutex_unlock (m_entry, &ctx->m_conn->cmutex);
    assert (r == NO_ERROR);

    return claimed;
  }

  void worker::release_reading (context *ctx)
  {
    ctx->m_recv.m_recv_busy.store (false, std::memory_order_release);
  }

  /* Pending when the eager receiver holds the socket; it rechecks on release */
  result worker::receive_if_free (context *ctx, bool in_exhausted, bool from_edge)
  {
    result status;

    if (!this->claim_reading (ctx, from_edge))
      {
	return result::Pending;
      }
    status = this->handle_reception (ctx, in_exhausted);
    this->release_reading (ctx);

    return status;
  }

  /* a granted claim raises m_eager_running; the close relies on the CONN_OPEN check under cmutex */
  context *worker::eager_claim (css_conn_entry &conn, cubthread::entry *entry, worker *&owner_out, SOCKET &fd_out)
  {
    context *ctx;
    int r;

    if (rmutex_lock (entry, &conn.cmutex) != NO_ERROR)
      {
	return nullptr;
      }
    owner_out = conn.worker;
    ctx = reinterpret_cast<context *> (conn.context);
    if (owner_out == nullptr || ctx == nullptr || ctx->m_recv.m_recv_busy.load (std::memory_order_acquire)
	|| conn.status != CONN_OPEN || conn.stop_talk || css_is_shutdowning_server ())
      {
	ctx = nullptr;
      }
    else
      {
	ctx->m_recv.m_recv_busy.store (true, std::memory_order_relaxed);
	ctx->m_recv.m_eager_running.fetch_add (1, std::memory_order_relaxed);
	ctx->m_recv.m_missed_edge = false;
	fd_out = conn.fd;
      }
    r = rmutex_unlock (entry, &conn.cmutex);
    assert (r == NO_ERROR);

    return ctx;
  }

  /* Run the reception steps of handle_reception () on the eager receiver.
   *   1. Read the socket up to the receive budget.
   *   2. Recheck under rmutex that the connection is still open.
   *   3. Handle the packets; complete requests are counted instead of pushed as tasks.
   *   4. Return the count, or push the counted requests to the pool when a method-mode packet ended counting.
   * Under the constraint at the reception declarations, m_stats and LAST_ACTIVE_NS are left to the worker.
   * m_recv_budget is the only worker member read, and every worker has the same value. */
  result worker::eager_drain (context *ctx, cubthread::entry *entry, int &count_out, bool &more_data_out)
  {
    std::vector<cubbase::span<std::byte>> *packets;
    result status, io_status;
    int r;

    count_out = 0;
    more_data_out = false;

    /* STEP 1. Read the socket up to the receive budget.
     * The fd is read without cmutex: must_defer_close_for_eager_receiver () defers the close
     * while this thread holds the socket. */
    io_status = ctx->m_recv.m_receiver.drain (ctx->m_conn->fd, m_recv_budget);
    if (io_status == result::PeerReset || io_status == result::Error)
      {
	er_log_conn (__FILE__, __LINE__, "connection::worker->eager_drain: status = %d\n", io_status);
	return result::Error;
      }

    assert (io_status == result::Pending || io_status == result::BudgetExhausted);

    /* the budget stopped the drain: hand the rest to the worker instead of polling again */
    more_data_out = (io_status == result::BudgetExhausted);

    if (ctx->m_recv.m_receiver.get_result ()->empty ())
      {
	return result::Ok;
      }

    /* STEP 2. Recheck under rmutex that the connection is still open. */
    if (rmutex_lock (entry, &ctx->m_conn->rmutex) != NO_ERROR)
      {
	return result::Error;
      }
    if (ctx->m_conn->status != CONN_OPEN || ctx->m_conn->stop_talk == true)
      {
	r = rmutex_unlock (entry, &ctx->m_conn->rmutex);
	assert (r == NO_ERROR);
	return result::Error;
      }

    /* STEP 3. Handle the packets; complete requests are counted instead of pushed as tasks. */
    ctx->m_recv.m_eager_draining = true;
    ctx->m_recv.m_eager_counted = 0;

    packets = ctx->m_recv.m_receiver.get_result ();
    for (auto &packet : *packets)
      {
	status = this->handle_packet (ctx, packet);

	if (status == result::Skewed)
	  {
	    ctx->m_recv.m_receiver.release (packet.data ());
	  }
	else if (status == result::ClosedConnection || status == result::Error)
	  {
	    er_log_conn (__FILE__, __LINE__, "connection::worker->eager_drain: handle_packet status = %d\n", status);
	    this->eager_abort (ctx);
	    r = rmutex_unlock (entry, &ctx->m_conn->rmutex);
	    assert (r == NO_ERROR);
	    return status;
	  }
      }

    /* STEP 4. Return the count, or push the counted requests to the pool when a method-mode packet ended counting. */
    if (!ctx->m_recv.m_eager_draining)
      {
	this->eager_flush_counted_to_pool (ctx);
      }
    count_out = ctx->m_recv.m_eager_counted;
    ctx->m_recv.m_eager_counted = 0;
    ctx->m_recv.m_eager_draining = false;

    r = rmutex_unlock (entry, &ctx->m_conn->rmutex);
    assert (r == NO_ERROR);

    packets->clear ();

    return result::Ok;
  }

  /* give back each counted request's add_pending_request (); caller holds rmutex */
  void worker::eager_abort (context *ctx)
  {
    for (int i = 0; i < ctx->m_recv.m_eager_counted; i++)
      {
	ctx->m_conn->start_request ();
      }
    ctx->m_recv.m_eager_draining = false;
    ctx->m_recv.m_eager_counted = 0;
  }

  /* give back each counted add_pending_request () before css_push_server_task () takes its own;
   * caller holds rmutex */
  void worker::eager_flush_counted_to_pool (context *ctx)
  {
    for (int i = 0; i < ctx->m_recv.m_eager_counted; i++)
      {
	ctx->m_conn->start_request ();
	css_push_server_task (*ctx->m_conn, cubthread::task_submission_options ());
      }
    ctx->m_recv.m_eager_counted = 0;
  }

  /* Give the socket taken by eager_claim () back.
   *   1. Clear m_recv_busy under cmutex; when the lock fails, clear it anyway and stop waiting.
   *   2. When the worker dropped an edge meanwhile, hand the socket back if data is still there.
   *   3. When handing back, send the worker a RECV_RECHECK.
   * Returns true when the caller must stop waiting. */
  bool worker::eager_release (css_conn_entry &conn, cubthread::entry *entry, context *ctx, SOCKET fd, bool hand_back)
  {
    struct pollfd po;
    int r;

    /* STEP 1. Clear m_recv_busy under cmutex; when the lock fails, clear it anyway and stop waiting. */
    if (rmutex_lock (entry, &conn.cmutex) != NO_ERROR)
      {
	/* a dropped edge cannot be handed back without the lock; the worker's next wake recovers it */
	ctx->m_recv.m_recv_busy.store (false, std::memory_order_release);
	return true;
      }
    ctx->m_recv.m_recv_busy.store (false, std::memory_order_release);

    /* STEP 2. When the worker dropped an edge meanwhile, hand the socket back if data is still there. */
    if (!hand_back && ctx->m_recv.m_missed_edge)
      {
	/* the dropped edge is usually data this thread already drained */
	po.fd = fd;
	po.events = POLLIN;
	po.revents = 0;
	hand_back = (poll (&po, 1, 0) != 0);
      }

    /* STEP 3. When handing back, send the worker a RECV_RECHECK. */
    if (hand_back && conn.worker != nullptr && conn.context == ctx)
      {
	message request;

	request.type = message_type::RECV_RECHECK;
	request.conn = &conn;
	request.ctx = ctx;
	request.id = ctx->m_id;
	if (!conn.worker->enqueue_and_notify (queue_type::IMMEDIATE, std::move (request)))
	  {
	    assert_release (false);
	  }
      }

    ctx->m_recv.m_missed_edge = false;
    r = rmutex_unlock (entry, &conn.cmutex);
    assert (r == NO_ERROR);

    return hand_back;
  }

  result worker::handle_transmission (context *ctx, bool in_exhausted)
  {
    std::shared_ptr<message_blocker> blocker;
    std::shared_ptr<message_blocker> room;
    result status;
    int r;

    r = rmutex_lock (m_entry, &ctx->m_conn->cmutex);
    assert (r == NO_ERROR);

    if (ctx->m_conn->worker != this || ctx->m_conn->context != ctx)
      {
	r = rmutex_unlock (m_entry, &ctx->m_conn->cmutex);
	assert (r == NO_ERROR);
	return result::Pending;
      }

    status = ctx->m_send.m_transmitter.fill (ctx->m_conn->fd, m_send_budget);
    if (status == result::PeerReset || status == result::Error)
      {
	er_log_conn (__FILE__, __LINE__, "connection::worker->handle_transmission: status = %d\n", status);

	/* ctx will be forcibly removed */
	ctx->m_ignore = ignore_level::IGNORE_ALL;
	blocker = std::move (ctx->m_send.m_blocker);
	room = std::move (ctx->m_send.m_room);

	r = rmutex_unlock (m_entry, &ctx->m_conn->cmutex);
	assert (r == NO_ERROR);

	this->wakeup_blocked_worker (blocker);
	this->wakeup_blocked_worker (room);
	this->handle_connection_close (ctx);
	return status == result::PeerReset ? result::PeerReset : result::ClosedConnection;
      }

    assert (status == result::Ok || status == result::Pending || status == result::BudgetExhausted);

    /* m_room is registered in the same cmutex hold as the prepare_append that failed, so nullptr means nobody waits and
     * the drain skips prepare_append. Wake only on a free slot: a drain inside the first iovec frees none. */
    if (ctx->m_send.m_room != nullptr && ctx->m_send.m_transmitter.prepare_append (1))
      {
	room = std::move (ctx->m_send.m_room);
      }

    if (status == result::Ok)
      {
	ctx->m_send.m_transmitter.clear ();
	blocker = std::move (ctx->m_send.m_blocker);

	if (!m_events.modify_descriptor (ctx->m_conn->fd, EPOLLET | EPOLLIN | EPOLLRDHUP, ctx))
	  {
	    er_log_conn (__FILE__, __LINE__, "connection::worker->handle_transmission: modify_descriptor failed\n");

	    ctx->m_ignore = ignore_level::IGNORE_ALL;
	    r = rmutex_unlock (m_entry, &ctx->m_conn->cmutex);
	    assert (r == NO_ERROR);

	    this->wakeup_blocked_worker (blocker);
	    this->wakeup_blocked_worker (room);
	    this->handle_connection_close (ctx);
	    return result::ClosedConnection;
	  }

	r = rmutex_unlock (m_entry, &ctx->m_conn->cmutex);
	assert (r == NO_ERROR);

	this->wakeup_blocked_worker (blocker);
	this->wakeup_blocked_worker (std::move (room));

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

	er_log_conn (__FILE__, __LINE__, "fully sent. fd = %d in the worker = %d\n", ctx->m_conn->fd, m_index);
      }
    else
      {
	r = rmutex_unlock (m_entry, &ctx->m_conn->cmutex);
	assert (r == NO_ERROR);
	if (!in_exhausted && status == result::BudgetExhausted)
	  {
	    handle_exhausted_add_context (ctx, EPOLLOUT);
	  }
      }
    this->wakeup_blocked_worker (room);

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

	    status = this->receive_if_free (ctx, true, true);
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

    if (prm_get_bool_value (PRM_ID_HARDWARE_AFFINITY))
      {
	/* pin the current thread to the core */
	os::resources::cpu::setaffinity (m_core);
      }

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
	      case message_type::SHUTDOWN_CLIENT:
		this->wakeup_blocked_worker (request.waiter_handle);
		break;

	      case message_type::NEW_CLIENT:
		css_free_conn (request.conn);
		m_parent->retire_context (request.ctx);
		break;

	      case message_type::TAKEOVER_CLIENT:
		/* sender may be blocked on a transmission queued while this context was in handoff */
		this->wakeup_blocked_worker (std::move (request.ctx->m_send.m_blocker));
		css_free_conn (request.ctx->m_conn);
		m_parent->retire_context (request.ctx);
		break;

	      case message_type::START:
	      case message_type::HIBERNATE:
	      case message_type::AWAKEN:
	      case message_type::SHUTDOWN:
	      case message_type::HANDOFF_CLIENT:
	      case message_type::RELEASE_PACKET:
	      case message_type::RECV_RECHECK:
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
		status = this->receive_if_free (ctx, false, true);
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

	/* A connection closed on the socket is otherwise released only on the next message queue pass, which may
	 * never come while the worker is idle; until then the entry stays in the active conn list with its session id
	 * and the session never expires. Release it here, after this round no longer refers to the context. */
	if (!m_removed_context.empty ())
	  {
	    this->purge_stale_contexts ();
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
