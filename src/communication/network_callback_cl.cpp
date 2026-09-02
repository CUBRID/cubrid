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

// +1 (one more slot): method_error need this when ER_SP_TOO_MANY_NESTED_CALL occurs in method_dispatch
static unsigned int xs_method_eid [METHOD_MAX_RECURSION_DEPTH + 1];

std::queue <cubmem::extensible_block> &
xs_get_data_queue ()
{
  return cubmethod::get_callback_handler()->get_data_queue ();
}

#if defined (CS_MODE)

bool
xs_is_in_method_rids (unsigned short rid)
{
  for (int i = 0; i <= METHOD_MAX_RECURSION_DEPTH ; i++)
    {
      unsigned int method_eid = xs_method_eid[i];
      if (method_eid)
	{
	  unsigned short method_rid = CSS_RID_FROM_EID (method_eid);
	  if (rid == method_rid)
	    {
	      return true;
	    }
	}
    }

  return false;
}
void
xs_set_method_eid (int idx, unsigned int eid)
{
  assert (idx <= METHOD_MAX_RECURSION_DEPTH);
  xs_method_eid [idx] = eid;
}

unsigned int
xs_get_method_eid (int idx)
{
  assert (idx <= METHOD_MAX_RECURSION_DEPTH);
  return xs_method_eid [idx];
}

int
xs_queue_send ()
{
  int error = NO_ERROR;
  int idx = tran_get_libcas_depth () - 1;
  assert (idx <= METHOD_MAX_RECURSION_DEPTH);
  int eid = xs_get_method_eid (idx);

  if (!xs_get_data_queue().empty())
    {
      cubmem::extensible_block &blk = xs_get_data_queue().front ();
      error = net_client_send_data (eid, blk.get_ptr (), blk.get_size());
      xs_get_data_queue().pop ();
    }

  return error;
}
#endif
