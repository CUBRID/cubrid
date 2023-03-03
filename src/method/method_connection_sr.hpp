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
 * method_connection_sr.hpp
 */

#ifndef _METHOD_CONNECTION_SR_HPP_
#define _METHOD_CONNECTION_SR_HPP_

#ident "$Id$"

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs to server module
#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

#include <functional>

#include "porting.h"
#include "method_connection.hpp"

// thread_entry.hpp
namespace cubthread
{
  class entry;
}

namespace cubmem
{
  class block;
}

namespace cubmethod
{
  using xs_callback_func = std::function <int (cubmem::block &)>;
  using xs_callback_func_with_sock = std::function <int (SOCKET socket, cubmem::block &)>;

  //////////////////////////////////////////////////////////////////////////
  // Interface to communicate with CAS
  //////////////////////////////////////////////////////////////////////////
  int xs_receive (cubthread::entry *thread_p, const xs_callback_func &func);
  int xs_receive (cubthread::entry *thread_p, SOCKET socket, const xs_callback_func_with_sock &func);
  int xs_send (cubthread::entry *thread_p, cubmem::extensible_block &mem);
  template <typename ... Args>
  int method_send_data_to_client (cubthread::entry *thread_p, Args &&... args)
  {
    cubmem::extensible_block b = std::move (mcon_pack_data (std::forward<Args> (args)...));
    return xs_send (thread_p, b);
  }
}

#endif // _METHOD_CONNECTION_SR_HPP_
