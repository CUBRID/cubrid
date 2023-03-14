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
 * method_connection_java.hpp: Interface to communicate with Java SP Server
 */

#ifndef _METHOD_CONNECTION_JAVA_HPP_
#define _METHOD_CONNECTION_JAVA_HPP_

#include "porting.h"

#include "method_connection.hpp"

#include <functional>

#include "jsp_comm.h"
#include "object_representation.h" /* OR_ */

namespace cubmethod
{
  using mcon_callback_func = std::function <int ()>;
  using mcon_callback_func_with_sock = std::function <int (SOCKET socket, cubmem::block &)>;

  //////////////////////////////////////////////////////////////////////////
  // Interface to communicate with Java SP Server
  //////////////////////////////////////////////////////////////////////////
  EXPORT_IMPORT int mcon_send_buffer_to_java (SOCKET socket, cubmem::block &blk);

  template <typename ... Args>
  int mcon_send_data_to_java (SOCKET socket, Args &&... args)
  {
    cubmem::block b = mcon_pack_data_block (std::forward<Args> (args)...);
    int status = mcon_send_buffer_to_java (socket, b);
    free (b.ptr);
    return status;
  }

  EXPORT_IMPORT int mcon_read_data_from_java (const SOCKET socket, cubmem::extensible_block &b);

  EXPORT_IMPORT int mcon_read_data_from_java (const SOCKET socket, cubmem::block &b);
  EXPORT_IMPORT int mcon_read_data_from_java (const SOCKET socket, cubmem::block &b, const mcon_callback_func &interrupt_func);
}

#endif // _METHOD_CONNECTION_JAVA_HPP_
