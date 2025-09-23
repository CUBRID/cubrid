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
 * connection_pool.cpp
 */

#include "hardware_topology.hpp"
#include "connection_pool.hpp"
#include "connection_worker.hpp"

#include <csignal>
#include <cstdint>
#include <unistd.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/epoll.h>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubconn
{
  connection_pool::connection_pool () :
    m_max_connections (-1),
    m_counter (0)
  {
  }

  connection_pool::~connection_pool ()
  {
  }

  void connection_pool::initialize (std::uint32_t max_connections, int connection_threads)
  {
    std::vector<int> *cores;
    std::uint32_t i;

    /* signal */
    (void) os_set_signal_handler (SIGPIPE, SIG_IGN);
    (void) os_set_signal_handler (SIGFPE, SIG_IGN);

    /* topology setting */
    cubbase::topology.load_cpu (connection_threads);
    cubbase::topology.map_nic_to_core ();
    cores = &cubbase::topology.get_cores ();

    /* TODO: need to consider dynamic increses */
    m_workers.reserve (cores->size () + 1);

    i = 0;
    for (int core : *cores)
      {
	m_workers.emplace_back (std::make_unique<connection_worker> (this, core, i++));
      }

    m_max_connections = max_connections;
  }

  void connection_pool::finalize ()
  {
    connection_worker::message request;

    request.type = connection_worker::message_type::SHUTDOWN;
    for (auto &worker : m_workers)
      {
	worker->enqueue (request);
	if (!worker->notify ())
	  {
	    assert_release (false);
	  }
      }
    m_max_connections = -1;
    m_workers.clear ();
  }

  void connection_pool::dispatch (css_conn_entry *conn)
  {
    /* TODO: in this function, we can take some kind of strategies how	      */
    /* the connection pool distributes the connections to connection workers. */
    /* but now, just uses round robin					      */
    connection_worker::message request;

    request.type = connection_worker::message_type::NEW_CLIENT;
    request.conn = conn;
    m_workers[m_counter]->enqueue (request);
    if (!m_workers[m_counter]->notify ())
      {
	assert_release (false);
      }

    m_counter++;
    if (m_counter == m_workers.size ())
      {
	m_counter = 0;
      }
  }

  void connection_pool::stats ()
  {
    for (auto &conn : m_workers)
      {
	conn->stats ();
      }
  }
}
