/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

/*
 * packable_object.hpp
 */

#ifndef _PACKABLE_OBJECT_HPP_
#define _PACKABLE_OBJECT_HPP_

#ident "$Id$"

#include "packer.hpp"
#include <map>

namespace cubpacking
{

  class packable_object
  {
    public:
      virtual ~packable_object () {};
      virtual void pack (packer *serializator) const = 0;
      virtual void unpack (unpacker *deserializator) = 0;

      virtual bool is_equal (const packable_object *other)
      {
	return true;
      }

      /* used at packing to get info on how much memory to reserve */
      virtual size_t get_packed_size (packer *serializator) const = 0;
  };

} /* namespace cubpacking */

#endif /* _PACKABLE_OBJECT_HPP_ */
