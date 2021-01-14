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

#ifndef _TEST_PACKING_HPP_
#define _TEST_PACKING_HPP_

#include "packable_object.hpp"
#include "pinnable_buffer.hpp"
#include <vector>

namespace test_packing
{

  int test_packing1 (void);

  int test_packing_buffer1 (void);

  int test_packing_all (void);

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
      enum rgb
      {
	RED, GREEN, BLUE,
	MAX
      };

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
      rgb color;

    public:

      void pack (cubpacking::packer &serializator) const;
      void unpack (cubpacking::unpacker &deserializator);

      bool is_equal (const packable_object *other);

      size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset = 0) const override;

      void generate_obj (void);
  };

}

#endif /* _TEST_PACKING_HPP_ */
