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

#ifndef _NETWORK_CALLBACK_CL_HPP_
#define _NETWORK_CALLBACK_CL_HPP_

#include <queue>
#include <functional>

#include "thread_compat.hpp"
#include "mem_block.hpp" /* cubmem::block */
#include "packer.hpp" /* packing_packer */

#if defined (CS_MODE)
void xs_set_conn_info (int idx, unsigned int rc);
unsigned int xs_get_conn_info (int idx);
int xs_queue_send ();
#endif

std::queue <cubmem::extensible_block> &xs_get_data_queue ();

template <typename ... Args>
int xs_pack_and_queue (Args &&... args)
{
  packing_packer packer;
  cubmem::extensible_block eb;
  packer.set_buffer_and_pack_all (eb, std::forward<Args> (args)...);
  eb.extend_to (packer.get_current_size ()); // ensure eb.get_size () == packer.get_current_size ()

  xs_get_data_queue().push (std::move (eb));
  return NO_ERROR;
}

template <typename ... Args>
int xs_send_queue (Args &&... args)
{
  xs_pack_and_queue (std::forward<Args> (args)...);
#if defined (CS_MODE)
  xs_queue_send ();
#endif
  return NO_ERROR;
}

#endif // _NETWORK_CALLBACK_CL_HPP_
