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

#ident "$Id$"

#ifndef _PACKABLE_OBJECT_HPP_
#define _PACKABLE_OBJECT_HPP_

#include <map>
#include "packer.hpp"

namespace cubpacking
{

class packable_object
{
protected:
  size_t m_packed_size;
public:
  virtual int pack (packer *serializator) = 0;
  virtual int unpack (packer *serializator) = 0;

  virtual bool is_equal (const packable_object *other) = 0;
  
  /* used at packing to get info on how much memory to reserve */
  virtual size_t get_packed_size (packer *serializator) = 0;
};

class self_creating_object
{
public:
  virtual self_creating_object *create (void) = 0;

  virtual ~self_creating_object() {};

  virtual int get_create_id (void) =  0;
};
/*
 * this is used as factory for packable_object
 * at unpacking, we have to instanciate a specific type of 'packable_object' from a sequence
 * of bytes (usualy first integer which encodes the type)
 */
class object_builder
{
protected:
  std::map <int, self_creating_object*> m_object_patterns;
public:
  ~object_builder ()
    {
      for(std::map<int, self_creating_object*>::iterator it = m_object_patterns.begin(); it != m_object_patterns.end(); it++)
        {
          delete (it->second);
        }
    };

  void add_pattern_object (self_creating_object *obj) { m_object_patterns[obj->get_create_id ()] = obj; };

  virtual self_creating_object *create_object (packer *serializator)
    {
      int obj_type;
      serializator->peek_unpack_int (&obj_type);

      return m_object_patterns[obj_type]->create ();
    };
};

} /* namespace cubpacking */

#endif /* _PACKABLE_OBJECT_HPP_ */
