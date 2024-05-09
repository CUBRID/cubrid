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
#include "jsp_file.h" /* javasp_read_info() */

#if defined (SERVER_MODE)
#include "server_support.h"
#endif
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubmethod
{
  connection_pool::connection_pool (int pool_size)
    : m_pool_size (pool_size)
    , m_queue ()
    , m_mutex ()
  {
    //
  }

  connection_pool::~connection_pool ()
  {
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

    if (!m_queue.empty ())
      {
	connection *conn = m_queue.front ();
	m_queue.pop ();
	ulock.unlock ();

	// test socket
	if (conn->is_valid() == false)
	  {
	    jsp_disconnect_server (conn->m_socket); // disconnect connecting with ExecuteThread in invalid state
	    conn->m_socket = jsp_connect_server (boot_db_name (), jsp_server_port_from_info ());
	  }

	return conn;
      }

    // new connection
    SOCKET socket = jsp_connect_server (boot_db_name (), jsp_server_port_from_info ());
    return new connection (this, socket);
  }

  void
  connection_pool::retire (connection *&claimed, bool kill)
  {
    std::unique_lock<std::mutex> ulock (m_mutex);
    if (claimed == nullptr)
      {
	return;
      }

    // test connection
    if (kill == false && claimed->is_valid () == true)
      {
	if ((int) m_queue.size () < m_pool_size)
	  {
	    m_queue.push (claimed);
	    return;
	  }
	else
	  {
	    // overflow
	    kill = true;
	  }
      }

    if (kill)
      {
	assert (claimed != nullptr);
	if (claimed)
	  {
	    delete claimed;
	    claimed = nullptr;
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
    m_pool = nullptr;
    jsp_disconnect_server (m_socket);
  }

  bool
  connection::is_valid ()
  {
    return (m_socket != INVALID_SOCKET);
  }

  SOCKET
  connection::get_socket ()
  {
    return m_socket;
  }

  bool
  connection::is_jvm_running ()
  {
    JAVASP_SERVER_INFO info;
    javasp_read_info (boot_db_name (), info);
    if (info.pid == -1)
      {
	return false;
      }
    else
      {
	return true;
      }
  }

} // namespace cubmethod
