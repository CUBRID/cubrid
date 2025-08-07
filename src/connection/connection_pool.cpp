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

#include "connection_pool.hpp"
#include "connection_worker.hpp"

#include <cstdint>
#include <unistd.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/epoll.h>

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

  void connection_pool::initialize (unsigned int worker_count, std::uint32_t max_connections)
  {
    std::uint32_t i;

    /* TODO: consider dynamic increses */
    m_workers.reserve (worker_count + 1);

    for (i = 0; i < worker_count; i++)
      {
	m_workers.emplace_back (std::make_unique<connection_worker> (this, i));
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
    m_workers[m_counter]->notify ();

    m_counter++;
    if (m_counter == m_workers.size ())
      {
	m_counter = 0;
      }
  }
}
