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
 * method_connection.hpp
 */

#ifndef _METHOD_CONNECTION_HPP_
#define _METHOD_CONNECTION_HPP_

#ident "$Id$"

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs to server module
#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

#include <functional>

#include "porting.h"

#include "mem_block.hpp" /* cubmem::block */
#include "packer.hpp" /* packing_packer */

// thread_entry.hpp
namespace cubthread
{
  class entry;
}

namespace cubmethod
{
  #if defined (SERVER_MODE) 
    //////////////////////////////////////////////////////////////////////////
    // Interface to communicate with CAS
    //////////////////////////////////////////////////////////////////////////
    using xs_callback_func = std::function <int (cubmem::block &)>;
    using xs_callback_func_with_sock = std::function <int (SOCKET socket, cubmem::block &)>;

    int xs_send (cubthread::entry *thread_p, cubmem::block &mem);
    int xs_receive (cubthread::entry *thread_p, const xs_callback_func &func);
    int xs_receive (cubthread::entry *thread_p, SOCKET socket, const xs_callback_func_with_sock &func);

    template <typename ... Args>
    int method_send_data_to_client (cubthread::entry *thread_p, Args &&... args)
    {
      packing_packer packer;
      cubmem::extensible_block eb;
      packer.set_buffer_and_pack_all (eb, std::forward<Args> (args)...);
      cubmem::block b (packer.get_current_size (), eb.get_ptr ());
      return xs_send (thread_p, b);
    }
  #endif

  //////////////////////////////////////////////////////////////////////////
  // Interface to communicate with Java SP Server
  //////////////////////////////////////////////////////////////////////////
  int method_send_buffer_to_java (SOCKET socket, cubmem::block &blk);

  template <typename ... Args>
  int method_send_data_to_java (SOCKET socket, Args &&... args)
  {
    packing_packer packer;
    cubmem::extensible_block eb;
    packer.set_buffer_and_pack_all (eb, std::forward<Args> (args)...);
    cubmem::block b (packer.get_current_size (), eb.get_ptr ());
    return method_send_buffer_to_java (socket, b);
  }
}

#endif // _METHOD_CONNECTION_HPP_
