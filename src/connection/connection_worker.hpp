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

#include "server_support.h"
#include "epoll.hpp"
#include "MPSCQueue.hpp"
#include "DMRB_SPSC.hpp"

#include <thread>
#include <cstring>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <fcntl.h>

namespace cubconn
{
  class connection_pool;

  class transmitter
  {
  public:
    transmitter ();
    ~transmitter ();
  };

  class connection_worker
  {
    public:
      enum class message_type
      {
	NEW_CLIENT,
	SHUTDOWN
      };

      struct message
      {
	message_type type;

	/* NEW_CLIENT */
	css_conn_entry *conn;
      };

    private:
      enum class state
      {
	Somestate
      };

      struct context
      {
	css_conn_entry *m_conn;

	cubbase::DMRB_SPSC<true> m_sendbuf;

	state m_state { state::Somestate };

	context ();
	~context ();
      };

    public:
      connection_worker (connection_pool *pool, std::size_t index);
      ~connection_worker ();

      /* used for control from other threads */
      void enqueue (const message &item);
      void notify ();

      bool run ();

    private:
      /* thread handle */
      std::thread m_thread;
      bool m_stop;
      /* connection pool */
      connection_pool *m_parent;

      std::size_t m_index;
      cubsocket::epoll m_events;
      int m_eventfd;

      /* this is a multi-producer single-consumer queue, so */
      /* data can be put into the queue from anywhere, but  */
      /* consumption must happen from only one thread.	    */
      cubbase::MPSCQueue<message> m_queue;

      /* --------------------------------------------------------------------------- */
      /* message queue based interface						     */
      /* --------------------------------------------------------------------------- */
      bool handle_message_queue_new_client (message &item);
      bool handle_message_queue ();

      /* --------------------------------------------------------------------------- */
      /* reception								     */
      /* --------------------------------------------------------------------------- */
      bool recv_with_buffer (context *ctx);
      bool handle_reception (context *ctx);

      /* --------------------------------------------------------------------------- */
      /* transmission								     */
      /* --------------------------------------------------------------------------- */
      bool handle_transmission (context *ctx);
  };
}

#endif
