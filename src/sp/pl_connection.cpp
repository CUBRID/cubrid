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

#include "pl_connection.hpp"

#include <algorithm> /* std::count_if */
#include <memory> /* std::unique_ptr */

#include "boot_sr.h"
#include "pl_sr.h" /* pl_server_port(), pl_connect_server() */
#include "pl_comm.h" /* pl_disconnect_server (), pl_ping () */
#include "object_representation.h" /* OR_ */

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubpl
{
  //////////////////////////////////////////////////////////////////////////
  // Definitions
  //////////////////////////////////////////////////////////////////////////

  /*********************************************************************
   * connection_pool - definition
   *********************************************************************/

  constexpr int SYSTEM_REVISION = -1;
  constexpr int INITIAL_REVISION = 0;

  connection_pool::connection_pool (int pool_size)
    : m_pool (pool_size, nullptr)
    , m_epoch (INITIAL_REVISION)
    , m_queue ()
    , m_mutex ()
  {
    assert (pool_size > 0);
    // TODO: make it configurable
    m_min_conn_size = 3;
    m_inc_conn_size = 5;

    initialize_pool ();
  }

  connection_pool::connection_pool (int pool_size, const std::string &db_name, int pl_port, bool is_for_sys)
    : connection_pool (pool_size)
  {
    m_db_name = db_name;
    m_db_port = pl_port;
    if (is_for_sys)
      {
	m_epoch = SYSTEM_REVISION;
      }
  }

  connection_pool::~connection_pool ()
  {
    cleanup_pool ();
  }

  connection_view
  connection_pool::claim ()
  {
    if (!is_system_pool ())
      {
	if (pl_server_wait_for_ready () != NO_ERROR)
	  {
	    return nullptr;
	  }
      }
    if (m_db_port == PL_PORT_DISABLED)
      {
	m_db_port = pl_server_port_from_info ();
      }

    std::lock_guard<std::mutex> lock (m_mutex);

    // Check if a connection is available in the queue
    if (!m_queue.empty())
      {
	int index = m_queue.front();
	m_queue.pop();

	if (m_pool[index] == nullptr)
	  {
	    create_new_connection (index);
	  }

	return get_connection_view (index);
      }

    // If no connections are available, find a slot for a new connection
    for (size_t i = 0; i < m_pool.size(); ++i)
      {
	if (m_pool[i] == nullptr)
	  {
	    create_new_connection (i);
	    return get_connection_view (i);
	  }
      }

    return nullptr;
  }

  void
  connection_pool::increment_epoch ()
  {
    if (!is_system_pool ())
      {
	m_epoch++;
      }
  }

  int
  connection_pool::get_pool_size () const
  {
    return m_pool.size ();
  }

  int
  connection_pool::get_epoch () const
  {
    return m_epoch;
  }

  const char *
  connection_pool::get_db_name () const
  {
    return m_db_name.c_str();
  }

  int
  connection_pool::get_db_port () const
  {
    return m_db_port;
  }

  void
  connection_pool::set_db_port (int port)
  {
    m_db_port = port;
  }

  bool
  connection_pool::is_system_pool () const
  {
    return (m_epoch.load (std::memory_order::memory_order_relaxed) == SYSTEM_REVISION);
  }

  // private
  void
  connection_pool::retire (connection *conn)
  {
    std::lock_guard<std::mutex> lock (m_mutex);

    m_queue.push (conn->get_index ());
  }

  void
  connection_pool::initialize_pool()
  {
    for (int i = 0; i < (int) m_pool.size (); ++i)
      {
	m_pool[i] = nullptr;
      }

    for (int i = 0; i < m_min_conn_size && i < (int) m_pool.size (); ++i)
      {
	m_queue.push (i); // Pre-fill the queue with indices
      }
  }

  void
  connection_pool::cleanup_pool()
  {
    std::lock_guard<std::mutex> lock (m_mutex);

    for (int i = 0; i < (int) m_pool.size (); ++i)
      {
	if (m_pool[i])
	  {
	    delete m_pool[i];
	    m_pool[i] = nullptr;
	  }
      }

    m_pool.clear();
    while (!m_queue.empty())
      {
	m_queue.pop();
      }
  }

  void
  connection_pool::create_new_connection (int index)
  {
    connection *conn = new connection (this, index);
    m_pool[index] = conn;
  }

  connection_view
  connection_pool::get_connection_view (int index)
  {
    connection *conn = m_pool[index];
    return connection_view (conn, [this] (connection *c)
    {
      if (c)
	{
	  this->retire (c); // Automatically return connection to the pool
	}
    });
  }

  /*********************************************************************
   * connection - definition
   *********************************************************************/
  connection::connection (connection_pool *pool, int index)
    : m_pool (pool)
    , m_index (index)
    , m_socket (INVALID_SOCKET)
    , m_epoch (pool->get_epoch ())
  {
    //
    do_reconnect ();
  }

  connection::~connection ()
  {
    if (m_socket != INVALID_SOCKET)
      {
	pl_disconnect_server (m_socket);
      }
  }

  bool
  connection::is_connected () const
  {
    return m_socket != INVALID_SOCKET;
  }

  bool
  connection::is_valid () const
  {
    return (m_socket != INVALID_SOCKET) && (m_pool->get_epoch () == m_epoch || m_pool->get_epoch () == SYSTEM_REVISION);
  }

  int
  connection::get_index () const
  {
    return m_index;
  }

  SOCKET
  connection::get_socket () const
  {
    return m_socket;
  }

  int
  connection::send_buffer (const cubmem::block &blk)
  {
    if (!is_valid ())
      {
	do_reconnect ();
      }

    OR_ALIGNED_BUF (OR_INT_SIZE) a_request;
    char *request = OR_ALIGNED_BUF_START (a_request);

    int request_size = static_cast<int> (blk.dim);
    or_pack_int (request, request_size);

    int nbytes = pl_writen (m_socket, request, OR_INT_SIZE);
    if (nbytes != OR_INT_SIZE)
      {
	return do_handle_network_error (nbytes);
      }

    nbytes = pl_writen (m_socket, blk.ptr, blk.dim);
    if (nbytes != static_cast<int> (blk.dim))
      {
	return do_handle_network_error (nbytes);
      }

    return NO_ERROR;
  }

  int
  connection::receive_buffer (cubmem::block &b)
  {
    return receive_buffer (b, nullptr, -1);
  }

  int
  connection::receive_buffer (cubmem::block &b, const pl_callback_func *interrupt_func, int timeout_ms)
  {
    if (!is_valid ())
      {
	return do_handle_network_error (-1);
      }

    int res_size = 0;
    int nbytes;
    int elapsed = 0;

    /* read data length */
    while (true)
      {
	nbytes = pl_readn_with_timeout (m_socket, (char *)&res_size, OR_INT_SIZE, timeout_ms);
	if (nbytes < 0)
	  {
	    if (errno == ETIMEDOUT)
	      {
		if (interrupt_func && (*interrupt_func)() != NO_ERROR)
		  {
		    return er_errid ();
		  }
		continue;
	      }
	    return do_handle_network_error (-1);
	  }
	if (nbytes != sizeof (int))
	  {
	    return do_handle_network_error (nbytes);
	  }
	else
	  {
	    break;
	  }
      }

    res_size = ntohl (res_size);

    // To avoid invalid res_size is returned by PL server, and to prevent memory exhaustion of cub_server
    constexpr int MAX_BUFFER_SIZE = 10 * 1024 * 1024; // 10MB max size
    if (res_size > MAX_BUFFER_SIZE || res_size < 0)
      {
	return do_handle_network_error (nbytes);
      }

    if (res_size == 0)
      {
	// No payload to read
	return NO_ERROR;
      }

    // Step 3: Extend block to fit the incoming data
    cubmem::extensible_block ext_blk;
    ext_blk.extend_to (res_size);

    // Step 4: Read the actual data with optional interrupt handling
    int total_read = 0;
    while (total_read < res_size)
      {
	nbytes = pl_readn_with_timeout (m_socket, ext_blk.get_ptr() + total_read, res_size - total_read, timeout_ms);
	if (errno == ETIMEDOUT && interrupt_func && (*interrupt_func)() != NO_ERROR)
	  {
	    return er_errid ();
	  }
	if (nbytes < 0)
	  {
	    return do_handle_network_error (nbytes);
	  }

	total_read += nbytes;
      }

    // Step 5: Move the data into the block
    cubmem::block blk (res_size, ext_blk.release_ptr());
    b = std::move (blk);

    return NO_ERROR; // Successfully received the buffer
  }

  void
  connection::invalidate ()
  {
    pl_disconnect_server (m_socket);
  }

  // private
  void
  connection::do_reconnect ()
  {
    if (m_socket != INVALID_SOCKET)
      {
	pl_disconnect_server (m_socket);
      }

    m_socket = pl_connect_server (m_pool->get_db_name (), m_pool->get_db_port ());
    if (m_socket != INVALID_SOCKET)
      {
	m_epoch = m_pool->get_epoch ();
      }
  }

  int
  connection::do_handle_network_error (int nbytes)
  {
    (void) invalidate ();

    if (m_pool->is_system_pool ())
      {
	// Do not set error message for system pool
	// To avoid noise in the error log
	return ER_SP_NETWORK_ERROR;
      }
    else
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NETWORK_ERROR, 1, nbytes);
	return er_errid ();
      }
  }

} // namespace cubpl
