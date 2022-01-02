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

#include "network_interface_cl.h"
#include "method_def.hpp"
#include "mem_block.hpp"
#include "packer.hpp"

#if defined (SERVER_MODE)
#error does not belong to server
#endif // SERVER_MODE

struct method_server_conn_info
{
  unsigned int rc;
  char *host;
  char *server_name;
};

namespace cubmethod
{
#if defined (CS_MODE)
  //////////////////////////////////////////////////////////////////////////
  // Interface to communicate with DB Server
  //////////////////////////////////////////////////////////////////////////
  template<typename ... Args>
  int method_send_data_to_server (method_server_conn_info &info, Args &&... args)
  {
    packing_packer packer;
    cubmem::extensible_block eb;

    packer.set_buffer_and_pack_all (eb, std::forward<Args> (args)...);

    int error = net_client_send_data (info.host, info.rc, eb.get_ptr (), packer.get_current_size ());
    if (error != NO_ERROR)
      {
	return ER_FAILED;
      }

    return NO_ERROR;
  }
#else
  template<typename ... Args>
  int method_send_data_to_server (method_server_conn_info &info, Args &&... args)
  {
    return NO_ERROR;
  }
#endif
} // cubmethod

#endif // _METHOD_CONNECTION_CL_HPP_