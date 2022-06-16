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
 * method_connection_pool.hpp
 */

#ifndef _METHOD_CONNECTION_POOL_HPP_
#define _METHOD_CONNECTION_POOL_HPP_

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs to server module
#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

#include <queue>
#include <mutex>

#include "porting.h" // SOCKET

namespace cubmethod
{
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

    private:
      int m_pool_size;
      std::queue <connection *> m_queue;
      std::mutex m_mutex;
  };

  class connection
  {

      friend connection_pool;

    public:
      connection () = delete;
      ~connection ();

      connection (const connection &copy) = delete; // Not CopyConstructible
      connection &operator= (const connection &copy) = delete; // Not CopyAssignable

      connection (connection &&c) = default;
      connection &operator= (connection &&other) = delete;

      bool is_valid ();
      SOCKET get_socket ();
      bool is_jvm_running ();

    private:
      explicit connection (connection_pool *pool, SOCKET socket);

      connection_pool *m_pool;
      SOCKET m_socket;
  };
} // namespace cubmethod

#endif // _METHOD_CONNECTION_POOL_HPP_
