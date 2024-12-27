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
 * pl_connection.hpp
 */

#ifndef _PL_CONNECTION_POOL_HPP_
#define _PL_CONNECTION_POOL_HPP_

#if !defined (SERVER_MODE) && !defined (SA_MODE) && !defined (PL_MODE)
#error Belongs to server module
#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

#include <atomic>
#include <queue>
#include <mutex>
#include <memory>

#include "porting.h" // SOCKET

#include "error_code.h"
#include "mem_block.hpp"
#include "packer.hpp"
#include "pl_file.h" /* pl_read_info() */
#include "network_callback_sr.hpp"

namespace cubthread
{
  class entry;
}

using pl_callback_func = std::function <int ()>;
using pl_callback_func_with_sock = std::function <int (SOCKET socket, cubmem::block &)>;

namespace cubpl
{
  //////////////////////////////////////////////////////////////////////////
  // Declarations
  //////////////////////////////////////////////////////////////////////////
  // forward declaration
  class connection;

  using connection_view = std::unique_ptr<connection, std::function<void (connection *)>>;

  /*********************************************************************
   * connection_pool - declaration
   *********************************************************************/
  class EXPORT_IMPORT connection_pool
  {
    public:
      connection_pool () = delete;
      explicit connection_pool (int pool_size, const std::string &db_name, int pl_port = PL_PORT_DISABLED,
				bool is_for_sys = false);
      ~connection_pool ();

      connection_pool (connection_pool &&other) = delete; // Not MoveConstructible
      connection_pool (const connection_pool &copy) = delete; // Not CopyConstructible

      connection_pool &operator= (connection_pool &&other) = delete; // Not MoveAssignable
      connection_pool &operator= (const connection_pool &copy) = delete; // Not CopyAssignable

      connection_view claim ();
      void retire (connection *claimed);

      void increment_epoch ();

      int get_pool_size () const;
      int get_epoch () const;

      const char *get_db_name () const;
      int get_db_port () const;

      bool is_system_pool () const;

    private:
      explicit connection_pool (int pool_size);
      void create_new_connection (int index);
      connection_view get_connection_view (int index);


      void initialize_pool ();
      void cleanup_pool ();

      std::vector <connection *> m_pool;
      std::atomic<int> m_epoch; // Whenever PL server is restarted, server_manager increments this value

      int m_min_conn_size; // minimum connection size
      int m_inc_conn_size; // increment connection size for lazy initialization

      // for connection
      std::string m_db_name;
      int m_db_port;

      // blocking queue
      std::queue <int> m_queue;
      std::mutex m_mutex;
  };

  /*********************************************************************
   * connection - declaration
   *********************************************************************/
  class EXPORT_IMPORT connection
  {
      friend connection_pool;

    public:
      connection () = delete;
      ~connection ();

      connection (const connection &copy) = delete; // Not CopyConstructible
      connection &operator= (const connection &copy) = delete; // Not CopyAssignable

      connection (connection &&c) = delete;
      connection &operator= (connection &&other) = delete;

      bool is_connected () const;
      bool is_valid () const;
      int get_index () const;

      int send_buffer (const cubmem::block &mem);

      int receive_buffer (cubmem::block &b); // simplified version
      int receive_buffer (cubmem::block &b, const pl_callback_func *interrupt_func, int timeout_ms);

      template <typename ... Args>
      int send_buffer_args (Args &&... args)
      {
	cubmem::block b = pack_data_block (std::forward<Args> (args)...);
	int status = send_buffer (b);
	if (b.is_valid ())
	  {
	    delete [] b.ptr;
	    b.ptr = NULL;
	    b.dim = 0;
	  }
	return status;
      }

      void invalidate ();

    private:
      explicit connection (connection_pool *pool, int index);

      void do_reconnect ();
      int do_handle_network_error (int nbytes);

      SOCKET get_socket () const;

      connection_pool *m_pool;
      int m_index;
      SOCKET m_socket;
      int m_epoch; // see connection_pool::m_epoch
  };
}; // namespace cubpl

using PL_CONNECTION_POOL = cubpl::connection_pool;
using PL_CONNECTION_VIEW = cubpl::connection_view;
using PL_CONNECTION = cubpl::connection;

#endif // _PL_CONNECTION_POOL_HPP_
