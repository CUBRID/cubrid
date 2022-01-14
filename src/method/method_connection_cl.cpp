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
#if defined (CS_MODE)
  static method_server_conn_info g_conn_info [METHOD_MAX_RECURSION_DEPTH + 1];

  int set_connection_info (int idx, int rc)
  {
    method_server_conn_info &info = g_conn_info [idx];
    info.rc = rc;
    return NO_ERROR;
  }

  method_server_conn_info *get_connection_info (int idx)
  {
    if (idx <= METHOD_MAX_RECURSION_DEPTH)
      {
	return &g_conn_info[idx];
      }
    else
      {
	return nullptr;
      }
  }

  int
  method_send_buffer_to_server (cubmem::block &block)
  {
    int depth = tran_get_libcas_depth () - 1;
    method_server_conn_info *info = get_connection_info (depth);

    assert (info);

    if (info)
      {
	int error = net_client_send_data (net_client_get_server_host(), info->rc, block.ptr, block.dim);
	if (error != NO_ERROR)
	  {
	    return ER_FAILED;
	  }
      }
    else
      {
	/* should not happened */
	assert (false);
	return ER_FAILED;
      }

    return NO_ERROR;
  }

#endif

} // cubmethod
