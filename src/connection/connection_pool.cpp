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
#include <sys/socket.h>
#include <sys/epoll.h>
#include <fcntl.h>

namespace cubconn
{
    connection_pool::connection_pool ()
      {
      }

    connection_pool::~connection_pool ()
      {
      }

    void connection_pool::init (std::uint32_t max_connections)
      {
	std::uint32_t i;
	
	for (i = 0; i < max_connections; i++)
	  {
	    new connection_worker ();
	  }

	m_max_connections = max_connections;
      }

    void connection_pool::run ()
      {
      }

    void connection_pool::dispatch (css_conn_entry *conn)
      {
      }
}
