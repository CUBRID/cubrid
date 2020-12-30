/*
 * Copyright 2008 Search Solution Corporation
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
 * packable_object.hpp
 */

#ifndef _PACKABLE_OBJECT_HPP_
#define _PACKABLE_OBJECT_HPP_

#ident "$Id$"

#include "packer.hpp"

namespace cubpacking
{

  class packable_object
  {
    public:
      virtual ~packable_object () {};

      /* used at packing to get info on how much memory to reserve */
      virtual size_t get_packed_size (packer &serializator, std::size_t start_offset = 0) const = 0;
      virtual void pack (packer &serializator) const = 0;
      virtual void unpack (unpacker &deserializator) = 0;

      virtual bool is_equal (const packable_object *other)
      {
	return true;
      }
  };

} /* namespace cubpacking */

#endif /* _PACKABLE_OBJECT_HPP_ */
