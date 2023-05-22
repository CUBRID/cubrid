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

#ifndef _METHOD_STRUCT_VALUE_HPP_
#define _METHOD_STRUCT_VALUE_HPP_

#include "packer.hpp"
#include "packable_object.hpp"

namespace cubmethod
{
  /*
   * cubmethod::dbvalue_java
   *
   * description
   *    javasp specialized packing for DB_VALUE
   *
   * how to use
   *    - packing
   *        DB_VALUE db_val;
   *        dbvalue_java sp_wrapper;
   *        sp_val.value = (DB_VALUE *) &db_val;
   *        sp_val.pack (serializator); // serializator is cubpacking::packer
   *
   *    - unpacking
   *        DB_VALUE db_val;
   *        dbvalue_java sp_wrapper;
   *        sp_val.value = &db_val[i];
   *        sp_val.unpack (deserializator); // deserializator is cubpacking::unpacker
   *
   * note
   *    - packing/unpacking shoudld should sync with CUBRIDPacker.java and CUBRIDUnpacker.java
   *
   */
  struct dbvalue_java : public cubpacking::packable_object
  {
    dbvalue_java ();

    void pack (cubpacking::packer &serializator) const override;
    void unpack (cubpacking::unpacker &deserializator) override;
    size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const override;

    void pack_value_internal (cubpacking::packer &serializator, DB_VALUE &v) const;
    void unpack_value_interanl (cubpacking::unpacker &deserializator, DB_VALUE *v);
    size_t get_packed_value_size_internal (cubpacking::packer &serializator, std::size_t start_offset, DB_VALUE &v) const;

    DB_VALUE *value;
  };
} // namespace cubmethod

#endif // _METHOD_STRUCT_VALUE_HPP_
