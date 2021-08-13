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

//
// method_def.hpp - define structures used by method feature
//

#ifndef _METHOD_DEF_H_
#define _METHOD_DEF_H_

#include <string>

#include "packer.hpp"

typedef enum
{
  METHOD_SUCCESS = 1,
  METHOD_EOF,
  METHOD_ERROR
} METHOD_CALL_STATUS;

enum METHOD_TYPE
{
  METHOD_TYPE_NONE = 0,
  METHOD_TYPE_INSTANCE_METHOD,
  METHOD_TYPE_CLASS_METHOD,
  METHOD_TYPE_JAVA_SP
};

enum METHOD_CALLBACK_CODE
{
  METHOD_CALLBACK_ARG_PREPARE,
  METHOD_CALLBACK_INVOKE,
  METHOD_CALLBACK_END,

  /* TODO at CBRD-23961 */
  METHOD_CALLBACK_DB_PARAMETER,
  METHOD_CALLBACK_NESTED_QUERY_PREPARE,
  METHOD_CALLBACK_NESTED_QUERY_EXECUTE,
  METHOD_CALLBACK_NESTED_QUERY_END
};

typedef struct method_arg_info METHOD_ARG_INFO;
struct method_arg_info
{
  int *arg_mode; /* IN, OUT, INOUT */
  int *arg_type; /* DB_TYPE */
  int result_type; /* DB_TYPE */

  method_arg_info () = default;
};

typedef struct method_sig_node METHOD_SIG;
struct method_sig_node
{
  /* method signature */
  METHOD_SIG *next;
  char *method_name;		/* method name */
  METHOD_TYPE method_type;	/* instance or class method */
  int num_method_args;		/* number of arguments */
  int *method_arg_pos;		/* arg position in list file */

  union
  {
    char *class_name;		/* class name for the class method */
    METHOD_ARG_INFO arg_info;  /* argument info for javasp's server-side calling */
  };

  void pack (cubpacking::packer &serializator) const;
  void unpack (cubpacking::unpacker &deserializator);
  size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset = 0) const;

  void freemem ();

  method_sig_node ();
};

struct method_sig_list
{
  /* signature for methods */
  METHOD_SIG *method_sig;	/* one method signature */
  int num_methods;		/* number of signatures */

  void pack (cubpacking::packer &serializator) const;
  void unpack (cubpacking::unpacker &deserializator);
  size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset = 0) const;

  void freemem ();

  method_sig_list () = default;
};
typedef struct method_sig_list METHOD_SIG_LIST;

#endif // _METHOD_DEF_H_
