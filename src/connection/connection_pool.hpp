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
 * connection_pool.hpp
 */

#ifndef _CONNECTION_POOL_HPP_
#define _CONNECTION_POOL_HPP_

#include "connection_context.hpp"
#include "connection_worker.hpp"

#include <cstring>
#include <cstdint>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <fcntl.h>

namespace cubconn::connection
{
  class pool
  {
    private:
      struct freelist
      {
	/* this must be the first */
	context m_context;
	freelist *m_next;

	freelist (std::size_t capacity) :
	  m_context (capacity),
	  m_next (nullptr)
	{
	}

	~freelist () = default;
      };

    public:
      pool ();
      ~pool ();

      void initialize (std::uint32_t max_connections, int connection_threads);
      void finalize ();

      void dispatch (css_conn_entry *conn);

      void stats ();

    private:
      std::uint32_t m_max_connections;
      std::vector<std::unique_ptr<worker>> m_workers;
      std::shared_ptr<thread_watcher> m_watcher;

      struct
      {
	freelist *m_head;
	std::size_t m_max;
	std::size_t m_claim;
      } m_freelist;

      std::size_t m_counter;

      void initialize_freelist (std::uint32_t max_connections);
      void finalize_freelist ();
      context *claim_context ();
      void retire_context (context *ctx);
  };
}

#endif
