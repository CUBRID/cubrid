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
#include "pinnable_buffer.hpp"
#include <vector>

namespace test_packing
{

  int test_packing1 (void);

  int test_packing_buffer1 (void);

  class buffer_manager : public cubbase::pinner
  {
    private:
      std::vector<cubmem::pinnable_buffer *> buffers;
    public:
      void allocate_bufer (cubmem::pinnable_buffer *&buf, const size_t &amount);

      void free_storage();

  };

  class po1 : public cubpacking::packable_object
  {
    public:
      int i1;
      short sh1;
      std::int64_t b1;
      int int_a[5];
      std::vector<int> int_v;
      DB_VALUE values[10];
      char small_str[256];
      std::string large_str;
      std::string str1;
      char str2[300];

    public:

      void pack (cubpacking::packer *serializator) const;
      void unpack (cubpacking::unpacker *deserializator);

      bool is_equal (const packable_object *other);

      size_t get_packed_size (cubpacking::packer *serializator) const;

      void generate_obj (void);
  };

}

#endif /* _TEST_PACKING_HPP_ */
