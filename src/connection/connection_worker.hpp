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
#include "server_support.h"
#include "receiver.hpp"
#include "transmitter.hpp"
#include "epoll.hpp"
#include "tbb/concurrent_queue.h"

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
      enum class message_type
      {
	NEW_CLIENT,
	SHUTDOWN,

	SEND_PACKET,
	RELEASE_PACKET
      };

      struct message
      {
	message_type type;

	css_conn_entry *conn;

	/* send packet/release packet */
	std::vector<cubbase::span<std::byte>> packet;
	/* send packet */
	std::function<void ()> deleter;
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

	context (std::size_t capacity);
	~context ();
      };

    public:
      connection_worker (connection_pool *pool, std::size_t core, std::size_t index);
      ~connection_worker ();

      /* used for control from other threads */
      void enqueue (const message &item);
      bool notify ();

      void initialize ();
      void finalize ();
      bool run ();

      void attach ();

    private:
      /* thread handle */
      std::thread m_thread;
      std::size_t m_core;
      bool m_stop;
      /* connection pool */
      connection_pool *m_parent;

      std::size_t m_index;
      cubsocket::epoll m_events;
      int m_eventfd;

      /* this is a multi-producer single-consumer queue, so */
      /* data can be put into the queue from anywhere, but  */
      /* consumption must happen from only one thread.	    */
      tbb::concurrent_queue<message> m_queue;

      cubthread::entry *m_entry;
      std::unordered_set<context *> m_context;

      void push_task_into_worker_pool (context *ctx);

      bool handle_connection_error (context *ctx);
      result handle_unexpected_packet (context *ctx);

      /* --------------------------------------------------------------------------- */
      /* message queue based interface						     */
      /* --------------------------------------------------------------------------- */
      bool clear_event ();

      bool handle_message_queue_send_packet (message &item);
      bool handle_message_queue_release_packet (message &item);

      bool handle_message_queue_new_client (message &item);
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
