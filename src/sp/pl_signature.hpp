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

//
// pl_signature.hpp
//

#ifndef _PL_SIGNATURE_HPP_
#define _PL_SIGNATURE_HPP_

#include "packable_object.hpp"

namespace cubpl
{
  struct pl_arg : public cubpacking::packable_object
  {
    int arg_size;
    int *arg_mode;  // IN OUT IN/OUT
    int *arg_type;  // DB_TYPE array
    int   *arg_default_value_size;
    char **arg_default_value;

    void pack (cubpacking::packer &serializator) const override;
    void unpack (cubpacking::unpacker &deserializator) override;
    size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const override;

    pl_arg ();
    ~pl_arg () override;
  };

  struct pl_signature : public cubpacking::packable_object
  {
    char *name;
    char *auth;
    int pl_type; // Java SP or PL/CSQL
    int result_type; // DB_TYPE

    pl_arg *arg;

    void pack (cubpacking::packer &serializator) const override;
    void unpack (cubpacking::unpacker &deserializator) override;
    size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const override;

    pl_signature ();
    ~pl_signature () override;
  };

  struct pl_signature_array : public cubpacking::packable_object
  {
    int num_sigs;
    pl_signature* sigs;

    void pack (cubpacking::packer &serializator) const override;
    void unpack (cubpacking::unpacker &deserializator) override;
    size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const override;

    pl_signature_array ();
    ~pl_signature_array () override;
  };

#if 0
  struct method_sig : public pl_signature
  {
    int *method_arg_pos;		/* arg position in list file */
    char *class_name;		        /* class name for the class method */

    void pack (cubpacking::packer &serializator) const override;
    void unpack (cubpacking::unpacker &deserializator) override;
    size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const override;

    method ();
    ~method () override;
  };
#endif
}

#endif // _PL_SIGNATURE_HPP_