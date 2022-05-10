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

#include "method_connection_pool.hpp"

#include "boot_sr.h"
#include "jsp_sr.h" /* jsp_server_port(), jsp_connect_server() */
#include "jsp_comm.h" /* jsp_disconnect_server (), jsp_ping () */

#if defined (SERVER_MODE)
#include "server_support.h"
#endif

namespace cubmethod
{

#if defined (SERVER_MODE)
  const std::size_t MAX_POOLABLE = css_get_max_workers ();
#else
  const std::size_t MAX_POOLABLE = 15;
#endif

  static connection_pool g_conn_pool (MAX_POOLABLE);

  connection_pool *
  get_connection_pool (void)
  {
    return &g_conn_pool;
  }

  connection_pool::connection_pool (int pool_size)
    : m_pool_size (pool_size), m_mutex ()
  {
    //
  }

  connection_pool::~connection_pool ()
  {
    std::unique_lock<std::mutex> ulock (m_mutex);
    while (!m_queue.empty ())
      {
	connection *conn = m_queue.front ();
	m_queue.pop ();
	delete conn;
      }
  }

  connection *
  connection_pool::claim ()
  {
    std::unique_lock<std::mutex> ulock (m_mutex);
    while (!m_queue.empty ())
      {
	connection *conn = m_queue.front ();
	m_queue.pop ();

	// test socket
	if (conn->is_valid() == false)
	  {
	    jsp_disconnect_server (conn->m_socket); // disconnect connecting with ExecuteThread in invalid state
	    conn->m_socket = jsp_connect_server (boot_db_name (), jsp_server_port ());
	  }

	return conn;
      }

    // new connection
    SOCKET socket = jsp_connect_server (boot_db_name (), jsp_server_port ());
    return new connection (this, socket);
  }

  void
  connection_pool::retire (connection *claimed, bool kill)
  {
    if (claimed == nullptr)
      {
	return;
      }

    std::unique_lock<std::mutex> ulock (m_mutex);

    if (kill)
      {
	delete claimed;
	return;
      }

    // test connection
    if (claimed->is_valid () == true)
      {
	if ((int) m_queue.size () < m_pool_size)
	  {
	    m_queue.push (claimed);
	  }
	else
	  {
	    // overflow
	    delete claimed;
	  }
      }
  }

  int
  connection_pool::max_size () const
  {
    return m_pool_size;
  }

  connection::connection (connection_pool *pool, SOCKET socket)
    : m_pool (pool), m_socket (socket)
  {
    //
  }

  connection::~connection ()
  {
    jsp_disconnect_server (m_socket);
  }

  bool
  connection::is_valid ()
  {
    return (jsp_ping (m_socket) == NO_ERROR);
  }

  SOCKET
  connection::get_socket ()
  {
    return m_socket;
  }

} // namespace cubmethod
