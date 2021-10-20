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

#include "method_struct_lob_info.hpp"

namespace cubmethod
{
  void
  lob_info::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_int ((int) db_type);
    if (lob_handle != NULL)
      {
	serializator.pack_bigint (lob_handle->size);
	serializator.pack_c_string (lob_handle->locator, strlen (lob_handle->locator) + 1);
      }
  }

  void
  lob_info::unpack (cubpacking::unpacker &deserializator)
  {
    lob_handle = NULL;
    // TODO
  }

  size_t
  lob_info::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    size_t size = serializator.get_packed_int_size (start_offset); // db_type
    if (lob_handle != NULL)
      {
	size += serializator.get_packed_bigint_size (size); // lob_size
	size += serializator.get_packed_c_string_size (lob_handle->locator, strlen (lob_handle->locator) + 1, size); // lob_size
      }
    return size;
  }
}
