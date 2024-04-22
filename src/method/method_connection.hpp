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
  template <typename ... Args>
  cubmem::extensible_block mcon_pack_data (Args &&... args)
  {
    packing_packer packer;
    cubmem::extensible_block eb;
    packer.set_buffer_and_pack_all (eb, std::forward<Args> (args)...);
    return eb;
  }

  template <typename ... Args>
  cubmem::block mcon_pack_data_block (Args &&... args)
  {
    packing_packer packer;
    cubmem::extensible_block eb;
    packer.set_buffer_and_pack_all (eb, std::forward<Args> (args)...);
    cubmem::block b (packer.get_current_size(), eb.release_ptr());
    return b;
  }
}

#endif // _METHOD_CONNECTION_HPP_
