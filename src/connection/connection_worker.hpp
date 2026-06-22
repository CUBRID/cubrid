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
 * connection_worker.hpp
 */

#ifndef _CONNECTION_WORKER_HPP_
#define _CONNECTION_WORKER_HPP_

#include <atomic>
#include <thread>
#include <array>
#include <tuple>
#include <functional>
#include <unordered_set>
#include <unordered_map>
#include <condition_variable>
#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/epoll.h>

#include "tbb/concurrent_queue.h"
#include "connection_defs.h"
#include "connection_context.hpp"
#include "connection_statistics.hpp"
#include "receiver.hpp"
#include "transmitter.hpp"
#include "epoll.hpp"

namespace cubconn::connection
{
  class pool;
  class coordinator;

  class worker
  {
    private:
      enum class status
      {
	READY,
	RUNNING,
	HIBERNATING,
	TERMINATING
      };

      enum class timer_type : uint32_t
      {
	NA,
	HIBERNATE,
	STATISTICS,
	QUEUE,
	HA,

	TYPE_COUNT
      };

      enum class timer_latency : uint64_t
      {
	NA = 0, /* off */
	LOW_LATENCY = static_cast<uint64_t> (1 * 1e6), /* 1 msec */
	MEDIUM_LATENCY = static_cast<uint64_t> (1 * 1e9), /* 1 sec */
	HIGH_LATENCY = static_cast<uint64_t> (2 * 1e9) /* 2 sec */
      };

      struct timer_handle
      {
	bool valid;
	timer_latency latency;
	std::function<bool ()> function;
	uint64_t last_time;
      };

      struct exhausted_context
      {
	bool prepared;
	uint32_t events;
	context *ctx;
      };

    public:
      enum class queue_type : uint8_t
      {
	IMMEDIATE,
	LAZY,

	TYPE_COUNT
      };

      enum class message_type
      {
	/* WORKER */

	START,

	HIBERNATE, /* lazy queue */
	AWAKEN, /* lazy queue */

	SHUTDOWN,

	/* CLIENT */

	NEW_CLIENT,
	HANDOFF_CLIENT, /* lazy queue */
	TAKEOVER_CLIENT,
	SHUTDOWN_CLIENT, /* lazy queue */

	SEND_PACKET,
	RELEASE_PACKET,

	TYPE_COUNT
      };

      struct message
      {
	public:
	  message () :
	    id (0),
	    ctx (nullptr),
	    conn (nullptr),
	    worker_ptr (nullptr),
	    worker_index (-1),
	    ignore (ignore_level::DONT_IGNORE),
	    retry (false),
	    waiter_handle (nullptr)
	  {
	  }
	  ~message () = default;

	  message (const message &) = delete;
	  message &operator= (const message &) = delete;

	  message (message &&) noexcept = default;
	  message &operator= (message &&) noexcept = default;

	  message_type type;

	  uint64_t id;
	  context *ctx;
	  css_conn_entry *conn;

	  /* the members below are used to deliver a target data */
	  /* each comment is a message_type using that member */

	  /* SEND_PACKET    */
	  /* RELEASE_PACKET */
	  std::vector<cubbase::span<std::byte>> packet;

	  /* SEND_PACKET    */
	  std::function<void ()> deleter;

	  /* HANDOFF_CLIENT */
	  worker *worker_ptr;
	  int worker_index;

	  /* SHUTDOWN_CLIENT */
	  ignore_level ignore;
	  bool retry;

	  /* waiter handle (implemented only for START, SHUTDOWN_CLIENT) */
	  /* START	     */
	  /* SHUTDOWN_CLIENT */
	  /* SEND_PACKET     */
	  std::shared_ptr<message_blocker> waiter_handle;

	  /* debug purpose */
#if !defined (NDEBUG)
	  uint64_t message_id;
#endif
      };

    public:
      worker (pool *pool, std::shared_ptr<coordinator> coord, std::shared_ptr<thread_watcher> watcher, std::size_t core,
	      std::size_t index);
      ~worker ();

      void initialize ();

      void finalize ();
      void finalize_resources ();

      bool run ();

      void attach ();

      /* used for control from other threads */
      void enqueue (queue_type type, message &&item);
      bool notify ();
      bool enqueue_and_notify (queue_type type, message &&item, std::function<void ()> func = nullptr,
			       int wait_time = 0 /* no wait */);

    private:
      /* connection pool */
      pool *m_parent;
      std::shared_ptr<coordinator> m_coordinator;
      std::shared_ptr<thread_watcher> m_watcher;

      /* thread handle */
      std::thread m_thread;
      std::size_t m_core;
      status m_status;
      bool m_stop;

      cubthread::entry *m_entry;
      std::unordered_set<context *> m_context;

      std::size_t m_index;

      /* eventfds */
      cubsocket::epoll m_events;
      /* event based */
      int m_eventfd;
      /* timer based */
      int m_timerfd;
      uint64_t m_timens;
      /* index is a type of timer handle block */
      std::array<timer_handle, static_cast<std::size_t> (timer_type::TYPE_COUNT)> m_timer_handler;

      bool m_has_retry;

      /* this is a multi-producer single-consumer queue, so */
      /* data can be put into the queue from anywhere, but  */
      /* consumption must happen from only one thread.	    */
      tbb::concurrent_queue<message> m_queue[static_cast<std::size_t> (queue_type::TYPE_COUNT)];
      /* use a counter to ensure that the handler only processes	*/
      /* requests currently in the queue. this is essential to prevent	*/
      /* starvation.							*/
      std::atomic<uint64_t> m_queue_size[static_cast<std::size_t> (queue_type::TYPE_COUNT)];

      std::vector<context *> m_removed_context;

      /* limiter */
      size_t m_recv_budget;
      size_t m_send_budget;
      std::unordered_map<uint64_t, exhausted_context> m_exhausted;

      /* statistics */
      statistics::metrics<statistics::worker> m_stats;

      /* --------------------------------------------------------------------------- */
      /* utility								     */
      /* --------------------------------------------------------------------------- */
      uint64_t get_time_ns (clockid_t type);

      void push_task_into_worker_pool (context *ctx);
      void purge_stale_contexts ();
      void wakeup_blocked_worker (std::shared_ptr<message_blocker> handle);

      /* --------------------------------------------------------------------------- */
      /* close connection							     */
      /* --------------------------------------------------------------------------- */
      bool is_wait_required (context *ctx);
      bool has_remaining_tasks (context *ctx);

      std::pair<int, int> start_connection_close (context *ctx);
      void end_connection_close ();

      bool handle_connection_close (context *ctx, bool retry = false, std::shared_ptr<message_blocker> handle = nullptr);

      /* --------------------------------------------------------------------------- */
      /* statistics and hibernation						     */
      /* --------------------------------------------------------------------------- */
      bool statistics_metrics_to_coordinator ();

      bool hibernate_check ();

      /* --------------------------------------------------------------------------- */
      /* HA									     */
      /* --------------------------------------------------------------------------- */
      bool ha_close_all_connections ();

      /* --------------------------------------------------------------------------- */
      /* event fd								     */
      /* --------------------------------------------------------------------------- */
      bool eventfd_register (int fd);
      bool eventfd_clear (int fd);

      bool eventfd_settimer (int fd, uint64_t sec, uint64_t nsec);
      bool eventfd_settimer (int fd, timer_latency latency);

      bool eventfd_starttimer ();
      bool eventfd_stoptimer ();
      bool eventfd_addtimer (timer_type type, timer_latency latency, std::function<bool ()> handle);
      bool eventfd_removetimer (timer_type type);

      bool eventfd_handler (bool *eventfds);

      /* --------------------------------------------------------------------------- */
      /* message queue based interface						     */
      /* --------------------------------------------------------------------------- */
      bool validate_message_generation (const message &item, context *ctx) const;
      bool forward_message_to_successor (queue_type type, message &item, context *ctx);

      bool handle_message_queue_send_packet (message &item);
      bool handle_message_queue_release_packet (message &item);

      bool handle_message_queue_new_client (message &item);
      bool handle_message_queue_handoff_client (message &item);
      bool handle_message_queue_takeover_client (message &item);
      bool handle_message_queue_shutdown_client (message &item);

      bool handle_message_queue_start (message &item);
      bool handle_message_queue_hibernate (message &item);
      bool handle_message_queue_awaken (message &item);
      bool handle_message_queue_shutdown (message &item);

      bool handle_message_queue_by_index (queue_type type);
      bool handle_message_queue ();

      /* --------------------------------------------------------------------------- */
      /* error									     */
      /* --------------------------------------------------------------------------- */
      void handle_hangup_or_error (context *ctx, bool err);

      /* --------------------------------------------------------------------------- */
      /* reception								     */
      /* --------------------------------------------------------------------------- */
      /* error */
      result handle_error_packet (context *ctx, cubbase::span<std::byte> &packet);

      /* data */
      result handle_data_packet (context *ctx, cubbase::span<std::byte> &packet);

      /* header */
      result handle_command_header_packet (context *ctx);
      result handle_header_packet (context *ctx, cubbase::span<std::byte> &packet);

      /* reception */
      result handle_packet (context *ctx, cubbase::span<std::byte> &packet);
      result handle_reception (context *ctx, bool in_exhausted);

      /* --------------------------------------------------------------------------- */
      /* transmission								     */
      /* --------------------------------------------------------------------------- */
      result handle_transmission (context *ctx, bool in_exhausted);

      /* --------------------------------------------------------------------------- */
      /* exhausted								     */
      /* --------------------------------------------------------------------------- */
      void handle_exhausted_add_context (context *ctx, uint32_t event);
      bool handle_exhausted ();
  };
}

#endif
