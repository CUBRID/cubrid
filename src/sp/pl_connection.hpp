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

#include "porting.h" // SOCKET

#include "error_code.h"
#include "mem_block.hpp"

namespace cubthread
{
  class entry;
}

namespace cubpl
{
  using xs_callback_func = std::function <int (cubmem::block &)>;
  using xs_callback_func_with_sock = std::function <int (SOCKET socket, cubmem::block &)>;

  // forward declaration
  class connection;

  class connection_pool
  {
    public:
      connection_pool () = delete;
      explicit connection_pool (int pool_size);
      ~connection_pool ();

      connection_pool (connection_pool &&other) = delete; // Not MoveConstructible
      connection_pool (const connection_pool &copy) = delete; // Not CopyConstructible

      connection_pool &operator= (connection_pool &&other) = delete; // Not MoveAssignable
      connection_pool &operator= (const connection_pool &copy) = delete; // Not CopyAssignable

      connection *claim ();
      void retire (connection *&claimed, bool kill);

      int max_size () const;

      int get_serial_val ()
      {
	return m_serial_val.load ();
      }

    private:
      int m_pool_size;

      // blocking queue
      std::queue <connection *> m_queue;
      std::mutex m_mutex;

      std::atomic<int> m_serial_val; // if PL server is restarted, this value is incremented
  };

  class connection
  {
      friend connection_pool;

    public:
      connection () = delete;
      ~connection ();

      connection (const connection &copy) = delete; // Not CopyConstructible
      connection &operator= (const connection &copy) = delete; // Not CopyAssignable

      connection (connection &&c) = delete;
      connection &operator= (connection &&other) = delete;

      bool is_valid ();

      SOCKET get_socket ();

    private:
      explicit connection (connection_pool *pool, SOCKET socket, int serial_val);

      connection_pool *m_pool;
      SOCKET m_socket;
      int m_serial_val; // see connection_pool::m_serial_val
  };

  //////////////////////////////////////////////////////////////////////////
  // Interface to communicate with PL server
  //////////////////////////////////////////////////////////////////////////
  


  //////////////////////////////////////////////////////////////////////////
  // Interface to communicate with CAS
  //////////////////////////////////////////////////////////////////////////
  int xs_receive (cubthread::entry *thread_p, const xs_callback_func &func);
  int xs_receive (cubthread::entry *thread_p, SOCKET socket, const xs_callback_func_with_sock &func);
  int xs_send (cubthread::entry *thread_p, const cubmem::extensible_block &mem);

  template <typename ... Args>
  int xs_send_args (cubthread::entry *thread_p, Args &&... args)
  {
    const cubmem::extensible_block b = std::move (mcon_pack_data (std::forward<Args> (args)...));
    return xs_send (thread_p, b);
  }

  template <typename ... Args>
  int xs_send_and_receive (cubthread::entry *thread_p, const xs_callback_func &func, Args &&... args)
  {
    int error_code = NO_ERROR;

    error_code = xs_send_args (thread_p, std::forward<Args> (args)...);
    if (error_code != NO_ERROR)
      {
	return error_code;
      }

    return xs_receive (thread_p, func);
  }
}; // namespace cubpl

using PL_CONNECTION_POOL = cubpl::connection_pool;
using PL_CONNECTION = cubpl::connection;

#endif // _PL_CONNECTION_POOL_HPP_
