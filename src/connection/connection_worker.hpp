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
#include <cstring>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <fcntl.h>

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

      enum class ignore_level : uint8_t
      {
	DONT_IGNORE = 0,
	IGNORE_PENDING,
	IGNORE_ALL
      };

      struct message
      {
	public:
	  message () = default;
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
      cubsocket::epoll m_events;
      int m_eventfd;

      /* this is a multi-producer single-consumer queue, so */
      /* data can be put into the queue from anywhere, but  */
      /* consumption must happen from only one thread.	    */
      tbb::concurrent_queue<message> m_queue[static_cast<std::size_t> (queue_type::TYPE_COUNT)];
      /* use a counter to ensure that the handler only processes	*/
      /* requests currently in the queue. this is essential to prevent	*/
      /* starvation.							*/
      std::atomic<uint64_t> m_queue_size[static_cast<std::size_t> (queue_type::TYPE_COUNT)];
      bool m_notified;

      /* stats */
      connection_stats m_stats;

      void push_task_into_worker_pool (context *ctx);

      bool handle_connection_error (context *ctx);

      /* --------------------------------------------------------------------------- */
      /* message queue based interface						     */
      /* --------------------------------------------------------------------------- */
      bool clear_event ();

      bool handle_message_queue_send_packet (message &item);
      bool handle_message_queue_release_packet (message &item);

      bool handle_message_queue_new_client (message &item);
      bool handle_message_queue_shutdown_client (message &item);

      bool handle_message_queue_by_index (queue_type type);
      bool handle_message_queue ();

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
