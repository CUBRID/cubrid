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
 * method_connection_cl.hpp
 */

#ifndef _METHOD_CONNECTION_CL_HPP_
#define _METHOD_CONNECTION_CL_HPP_

#if defined (SERVER_MODE)
#error does not belong to server
#endif // SERVER_MODE

#include <queue>

#include "network_interface_cl.h"
#include "method_def.hpp"
#include "mem_block.hpp"
#include "packer.hpp"
#include "transaction_cl.h"

struct method_server_conn_info
{
  unsigned int rc;
};

namespace cubmethod
{
  //////////////////////////////////////////////////////////////////////////
  // Data Queue
  //////////////////////////////////////////////////////////////////////////

  std::queue <cubmem::extensible_block> &mcon_get_data_queue ();

  template <typename ... Args>
  int mcon_pack_and_queue (Args &&... args)
  {
    packing_packer packer;
    cubmem::extensible_block eb;
    packer.set_buffer_and_pack_all (eb, std::forward<Args> (args)...);
    eb.extend_to (packer.get_current_size ()); // ensure eb.get_size () == packer.get_current_size ()

    mcon_get_data_queue ().push (std::move (eb));
    return NO_ERROR;
  }

#if defined (CS_MODE)
  //////////////////////////////////////////////////////////////////////////
  // Interface to communicate with DB Server
  //////////////////////////////////////////////////////////////////////////

  void mcon_set_connection_info (int idx, int rc);
  method_server_conn_info *get_connection_info (int idx);
  int mcon_send_queue_data_to_server ();

  template<typename ... Args>
  int mcon_send_data_to_server (Args &&... args)
  {
    mcon_pack_and_queue (std::forward<Args> (args)...);
    mcon_send_queue_data_to_server ();
    return NO_ERROR;
  }
#else
  template<typename ... Args>
  int mcon_send_data_to_server (Args &&... args)
  {
    mcon_pack_and_queue (std::forward<Args> (args)...);
    return NO_ERROR;
  }
#endif
} // cubmethod

#endif // _METHOD_CONNECTION_CL_HPP_
