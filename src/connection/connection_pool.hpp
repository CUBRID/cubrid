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

#include <mutex>
#include <cstring>
#include <cstdint>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <fcntl.h>

#include "connection_context.hpp"
#include "connection_worker.hpp"
#include "coordinator.hpp"

namespace cubconn::connection
{
  class pool
  {
    private:
      struct freelist
      {
	/* THIS MUST BE THE FIRST */
	context m_context;

	freelist *m_next;

	freelist (std::size_t capacity) :
	  m_context (capacity),
	  m_next (nullptr)
	{
	}

	~freelist () = default;

	bool prepare ()
	{
	  return m_context.prepare ();
	}
      };

    public:
      pool ();
      ~pool ();

      bool initialize (std::uint32_t max_connections, int max_connection_workers, int min_connection_workers);
      void finalize ();

      void dispatch (css_conn_entry *conn);

      void lock_resource ();
      void release_resource ();

      context *claim_context ();
      void retire_context (context *ctx);

      std::vector<std::unique_ptr<worker>> &get_workers ();

    private:
      /* the members in connection pool can be managed entirely by other threads. */
      /* so you must acquire the mutex to access belows.			  */
      std::mutex m_mutex;
#if !defined (NDEBUG)
      std::atomic<std::thread::id> m_mutex_holder { std::thread::id () };
#endif

      /* components */
      std::vector<std::unique_ptr<worker>> m_workers;
      std::shared_ptr<coordinator> m_coordinator;

      std::shared_ptr<thread_watcher> m_watcher;

      /* base */
      std::uint32_t m_max_connections;

      std::uint32_t m_max_connection_workers;
      std::uint32_t m_min_connection_workers;

      /* freelist */
      struct
      {
	freelist *m_head;
	std::size_t m_max;
	std::size_t m_claim;
      } m_freelist;

      void drain_contexts ();

      void try_to_lock_resource ();

      bool initialize_freelist (std::uint32_t max_connections);
      void finalize_freelist ();

      std::uint32_t initialize_topology (std::uint32_t max_connection_workers);
      void finalize_topology ();

      void initialize_workers (std::uint32_t max_connection_workers, std::uint32_t min_connection_workers);
      void finalize_workers ();

      void initialize_coordinator (std::uint32_t max_connection_workers, std::uint32_t min_connection_workers);
      void start_coordinator ();
      void finalize_coordinator ();
  };
}

#endif
