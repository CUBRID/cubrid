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
#include "thread_compat.hpp"

enum PL_TYPE
{
  PL_TYPE_NONE = 0,
  PL_TYPE_INSTANCE_METHOD,
  PL_TYPE_CLASS_METHOD,
  PL_TYPE_JAVA_SP,
  PL_TYPE_PLCSQL
};

enum PL_ARG_DEFAULT
{
  PL_ARG_DEFAULT_NONE = -1,
  PL_ARG_DEFAULT_NULL = 0
};

namespace cubpl
{
#define PL_TYPE_IS_METHOD(type) \
        ((type) == PL_TYPE_INSTANCE_METHOD || (type) == PL_TYPE_CLASS_METHOD)

  struct pl_arg : public cubpacking::packable_object
  {
    THREAD_ENTRY *owner;

    int arg_size;
    int *arg_mode;  // array of (IN|OUT|IN/OUT)
    int *arg_type;  // array of DB_TYPE

    // Only used in runtime
    int   *arg_default_value_size;
    char **arg_default_value;

    void pack (cubpacking::packer &serializator) const override;
    void unpack (cubpacking::unpacker &deserializator) override;
    size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const override;

    pl_arg ();
    pl_arg (int num_args);
    ~pl_arg () override;

    void set_arg_size (int num_args);
    void clear ();
  };

  union pl_ext
  {
    struct pl_sp_info
    {
      char *target_class_name;
      char *target_method_name;
      OID code_oid; // PL/CSQL
    } sp;
    struct pl_method_info
    {
      char *class_name;
      int *arg_pos;
    } method;
  };

  struct pl_signature : public cubpacking::packable_object
  {
    THREAD_ENTRY *owner;

    int type; // PL_TYPE
    char *name;
    char *auth;
    int result_type; // DB_TYPE

#if defined (CS_MODE)
    bool is_deterministic; // DETERMINISTIC
#endif

    pl_arg arg;
    pl_ext ext;

    void pack (cubpacking::packer &serializator) const override;
    void unpack (cubpacking::unpacker &deserializator) override;
    size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const override;

    bool has_args ();

    pl_signature ();
    ~pl_signature () override;
  };

  struct pl_signature_array : public cubpacking::packable_object
  {
    THREAD_ENTRY *owner;

    int num_sigs;
    pl_signature *sigs;

    void pack (cubpacking::packer &serializator) const override;
    void unpack (cubpacking::unpacker &deserializator) override;
    size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const override;

    pl_signature_array ();
    pl_signature_array (int num);
    ~pl_signature_array () override;
  };
}

using PL_SIGNATURE_TYPE = cubpl::pl_signature;
using PL_SIGNATURE_ARG_TYPE = cubpl::pl_arg;
using PL_SIGNATURE_ARRAY_TYPE = cubpl::pl_signature_array;

#endif // _PL_SIGNATURE_HPP_
