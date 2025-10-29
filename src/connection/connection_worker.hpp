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
 * connection_worker.hpp
 */

#ifndef _CONNECTION_WORKER_HPP_
#define _CONNECTION_WORKER_HPP_

#include "connection_defs.h"
#include "connection_stats.hpp"
#include "receiver.hpp"
#include "transmitter.hpp"
#include "epoll.hpp"
#include "tbb/concurrent_queue.h"

#include <atomic>
#include <thread>
#include <unordered_set>
#include <condition_variable>
#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/epoll.h>

namespace cubconn
{
  class connection_pool;

  class connection_worker
  {
    public:
      enum class queue_type : uint8_t
      {
	IMMEDIATE,
	LAZY,

	TYPE_COUNT
      };

      enum class message_type
      {
	NEW_CLIENT,
	SHUTDOWN_CLIENT, /* lazy queue */

	SEND_PACKET,
	RELEASE_PACKET,

	SHUTDOWN
      };

      enum class notification_type : uint8_t
      {
	QUEUE = 0x1,
	HA = 0x2
      };

      enum class timer_latency : uint32_t
      {
	NA = 0, /* off */
	LOW_LATENCY = static_cast<uint32_t> (1 * 1e6), /* 1 msec */
	MEDIUM_LATENCY = static_cast<uint32_t> (2 * 1e9) /* 2 sec, default */
      };

      enum class ignore_level : uint8_t
      {
	DONT_IGNORE = 0,
	IGNORE_ALL
      };

      struct message_blocker
      {
	std::mutex m;
	std::condition_variable cv;
	bool done;
      };

      struct message
      {
	public:
	  message () :
	    conn (nullptr),
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

	  css_conn_entry *conn;

	  /* send packet/release packet */
	  std::vector<cubbase::span<std::byte>> packet;
	  /* send packet */
	  std::function<void ()> deleter;
	  /* shutdown client */
	  ignore_level ignore;
	  bool retry;

	  /* waiter handle */
	  std::shared_ptr<message_blocker> waiter_handle;

	  /* debug purpose */
#if !defined (NDEBUG)
	  uint64_t message_id;
#endif
      };

    private:
      enum class state
      {
	HEADER,
	DATA,
	ERROR
      };

      struct context
      {
	css_conn_entry *m_conn;

	/* ignore guards (ERR/HUP) */
	ignore_level m_ignore;
	bool m_removed;

	/* --------------------------------------------------------------------------- */
	/* reception								       */
	/* --------------------------------------------------------------------------- */
	struct
	{
	  state m_state;
	  receiver m_receiver;

	  cubbase::span<std::byte> m_header;
	  int m_request_id;

	  /* if received command packet, task will be pushed into worker pool */
	  /* when data packet is completely received. */
	  bool m_command;
	} m_recv;

	/* --------------------------------------------------------------------------- */
	/* transmission								       */
	/* --------------------------------------------------------------------------- */
	struct
	{
	  transmitter m_transmitter;
	} m_send;

	context (std::size_t capacity, connection_stats *stats);
	context ();
	~context ();
      };

    public:
      connection_worker (connection_pool *pool, std::size_t core, std::size_t index);
      ~connection_worker ();

      void initialize ();
      void finalize ();
      bool run ();

      void attach ();

      /* used for control from other threads */
      void enqueue (queue_type type, message &&item);
      bool notify ();
      bool enqueue_and_notify (queue_type type, message &&item, bool need_wait = false);

      /* statistics */
      void stats ();

    private:
      /* connection pool */
      connection_pool *m_parent;

      /* thread handle */
      std::thread m_thread;
      std::size_t m_core;
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
      timer_latency m_timer_latency;
      /* purpose of timer notification */
      uint32_t m_notification;

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

      /* stats */
      connection_stats m_stats;

      void push_task_into_worker_pool (context *ctx);

      /* --------------------------------------------------------------------------- */
      /* close connection							     */
      /* --------------------------------------------------------------------------- */
      bool is_wait_required (context *ctx);
      bool has_remaining_tasks (context *ctx);

      std::pair<int, int> start_connection_close (context *ctx);
      void end_connection_close ();

      bool handle_connection_close (context *ctx, bool retry = false, std::shared_ptr<message_blocker> handle = nullptr);

      /* --------------------------------------------------------------------------- */
      /* HA									     */
      /* --------------------------------------------------------------------------- */
      void ha_close_all_connections ();

      /* --------------------------------------------------------------------------- */
      /* event fd								     */
      /* --------------------------------------------------------------------------- */
      bool eventfd_register (int fd);
      bool eventfd_clear (int fd);

      bool eventfd_settimer (int fd, uint32_t sec, uint64_t nsec);
      bool eventfd_settimer (int fd, timer_latency latency);

      bool eventfd_handler (bool *eventfds);

      /* --------------------------------------------------------------------------- */
      /* message queue based interface						     */
      /* --------------------------------------------------------------------------- */
      bool handle_message_queue_send_packet (message &item);
      bool handle_message_queue_release_packet (message &item);

      bool handle_message_queue_new_client (message &item);
      bool handle_message_queue_shutdown_client (message &item);

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
      result handle_reception (context *ctx);

      /* --------------------------------------------------------------------------- */
      /* transmission								     */
      /* --------------------------------------------------------------------------- */
      result handle_transmission (context *ctx);
  };
}

#endif
