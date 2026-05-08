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
 * DMRBMemoryPool.hpp
 */

#ifndef _DMRB_MEMORY_POOL_HPP_
#define _DMRB_MEMORY_POOL_HPP_

#ident "$Id$"

#include "DMRB.hpp"

#include <map>
#include <cstring>
#include <cstddef>

namespace cubbase
{
  class DMRBMemoryPool : public DMRB<false>
  {
    public:
      DMRBMemoryPool (std::size_t capacity);
      DMRBMemoryPool ();
      ~DMRBMemoryPool ();

      void reset ();

      cubbase::span<std::byte> buffer ();
      void restore (cubbase::span<std::byte> &span);
      bool is_in (cubbase::span<std::byte> &span);
      bool is_in (std::byte *ptr);

      cubbase::span<std::byte> reserve (std::size_t length);
      void commit (std::size_t length);
      void consume (std::size_t length);
      cubbase::span<const std::byte> peek () const;

    private:
      std::map<std::uint64_t, std::uint64_t> m_free;
  };
}

#endif
