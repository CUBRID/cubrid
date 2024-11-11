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

#include "method_connection_cl.hpp"


namespace cubmethod
{
  static std::queue <cubmem::extensible_block> s_data_queue;

  std::queue <cubmem::extensible_block> &mcon_get_data_queue ()
  {
    return s_data_queue;
  }

#if defined (CS_MODE)
  static method_server_conn_info s_conn_info [METHOD_MAX_RECURSION_DEPTH + 1];

  void mcon_set_connection_info (int idx, int rc)
  {
    method_server_conn_info &info = s_conn_info [idx];
    info.rc = rc;
  }

  method_server_conn_info *get_connection_info (int idx)
  {
    if (idx <= METHOD_MAX_RECURSION_DEPTH)
      {
	return &s_conn_info[idx];
      }
    else
      {
	return nullptr;
      }
  }

  int
  mcon_send_queue_data_to_server ()
  {
    int depth = tran_get_libcas_depth () - 1;
    method_server_conn_info *info = get_connection_info (depth);

    assert (info);
    assert (!mcon_get_data_queue().empty());

    cubmem::extensible_block &blk = mcon_get_data_queue().front ();
    int error = net_client_send_data (net_client_get_server_host(), info->rc, blk.get_ptr (), blk.get_size());
    mcon_get_data_queue().pop ();
    return error;
  }

#endif

} // cubmethod
