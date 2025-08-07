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

#include <thread>
#include <cstring>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <fcntl.h>

namespace cubconn
{
  class connection_pool;

  class connection_worker
  {
    private:
      enum class state
      {
	/* handshake with master */
	SendInHandshake,
	RecvInHandshake,

	SwitchToUnixSocket,

	/* request from master */
	RecvRequestType,

	RecvNewClient,

	/* send to clients */
	SendReplyToClient
      };

      struct context
      {
	css_conn_entry *m_conn;

	state m_state { state::SendInHandshake };
	bool m_has_error;

	context ();
	~context ();

	void reset ();
	bool has_data_to_send ();

	template <typename... Spans>
	void push_for_send (const cubbase::span<std::byte> &first, const Spans &... rest)
	{
	  m_sendbuf.push_for_send (std::forward<const cubbase::span<std::byte>> (first), std::forward<Spans> (rest)...);
	}

	template <typename T>
	T *allocate ()
	{
	  return m_sendbuf.allocate<T> ();
	}
      };

    public:
      connection_worker (connection_pool *pool, std::size_t index);
      ~connection_worker ();

      /* used for control from other threads */
      void enqueue ();
      void notify ();

      void run ();

    private:
      /* thread handle */
      std::thread m_thread;
      /* connection pool */
      connection_pool *m_parent;

      std::size_t m_index;
      cubsocket::epoll m_events;
      int m_eventfd;

      /* this is a multi-producer single-consumer queue, so */
      /* data can be put into the queue from anywhere, but  */
      /* consumption must happen from only one thread.	    */
      cubbase::MPSCQueue<int> m_queue;
  };
}

#endif
