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

#include "network_callback_cl.hpp"

#include "network_interface_cl.h" /* net_client_send_data */
#include "method_callback.hpp"

static unsigned int xs_conn_info [METHOD_MAX_RECURSION_DEPTH + 1];

std::queue <cubmem::extensible_block> &
xs_get_data_queue ()
{
  return cubmethod::get_callback_handler()->get_data_queue ();
}

#if defined (CS_MODE)
void
xs_set_conn_info (int idx, unsigned int rc)
{
  xs_conn_info [idx] = rc;
}

unsigned int
xs_get_conn_info (int idx)
{
  return xs_conn_info [idx];
}

int
xs_queue_send ()
{
  int error = NO_ERROR;
  int depth = tran_get_libcas_depth () - 1;
  int rc = xs_get_conn_info (depth);

  if (!xs_get_data_queue().empty())
    {
      cubmem::extensible_block &blk = xs_get_data_queue().front ();
      error = net_client_send_data (net_client_get_server_host(), rc, blk.get_ptr (), blk.get_size());
      xs_get_data_queue().pop ();
    }

  return error;
}
#endif