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

#ifndef _TEST_PACKING_HPP_
#define _TEST_PACKING_HPP_

#include "packable_object.hpp"
#include <vector>

class packing_buffer;

namespace test_packing
{

int test_packing1 (void);

int test_packing_buffer1 (void);

class buffer_manager : public pinner
{
private:
  std::vector<packing_buffer*> buffers;
public:
  void allocate_bufer (packing_buffer *&buf, const size_t &amount);

  void free_storage();

};

class po1 : public packable_object
{
public:
  int i1;
  short sh1;
  DB_BIGINT b1;
  int int_a[5];
  std::vector<int> int_v;
  DB_VALUE values[10];
  char small_str[256];
  std::string large_str;

public:

  int pack (packer *serializator);
  int unpack (packer *serializator);

  bool is_equal (const packable_object *other);
  
  size_t get_packed_size (packer *serializator);

  void generate_obj (void);
};



}

#endif /* _TEST_PACKING_HPP_ */
