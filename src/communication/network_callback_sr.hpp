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

#ifndef _NETWORK_CALLBACK_SR_HPP_
#define _NETWORK_CALLBACK_SR_HPP_

#include <queue>
#include <functional>

#include "error_code.h"
#include "thread_compat.hpp"
#include "mem_block.hpp" /* cubmem::block */
#include "packer.hpp" /* packing_packer */

//////////////////////////////////////////////////////////////////////////
// Packing data
//////////////////////////////////////////////////////////////////////////
template <typename ... Args>
cubmem::extensible_block pack_data (Args &&... args)
{
  packing_packer packer;
  cubmem::extensible_block eb;
  packer.set_buffer_and_pack_all (eb, std::forward<Args> (args)...);
  return eb;
}

template <typename ... Args>
cubmem::block pack_data_block (Args &&... args)
{
  packing_packer packer;
  cubmem::extensible_block eb;
  packer.set_buffer_and_pack_all (eb, std::forward<Args> (args)...);
  cubmem::block b (packer.get_current_size(), eb.release_ptr());
  return b;
}

//////////////////////////////////////////////////////////////////////////
// Send/Receive
//////////////////////////////////////////////////////////////////////////
using xs_callback_func = std::function <int (cubmem::block &)>;

int xs_callback_receive (cubthread::entry *thread_p, const xs_callback_func &func);
int xs_callback_send (cubthread::entry *thread_p, const cubmem::extensible_block &mem);

template <typename ... Args>
int xs_callback_send_args (cubthread::entry *thread_p, Args &&... args)
{
  const cubmem::extensible_block b = std::move (pack_data (std::forward<Args> (args)...));
  return xs_callback_send (thread_p, b);
}

template <typename ... Args>
int xs_callback_send_and_receive (cubthread::entry *thread_p, const xs_callback_func &func, Args &&... args)
{
  int error_code = NO_ERROR;

  error_code = xs_callback_send_args (thread_p, std::forward<Args> (args)...);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return xs_callback_receive (thread_p, func);
}

#endif // _NETWORK_CALLBACK_SR_HPP_
