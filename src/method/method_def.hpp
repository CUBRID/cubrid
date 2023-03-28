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
#include "packable_object.hpp"

#define METHOD_MAX_RECURSION_DEPTH 15

using METHOD_GROUP_ID = std::uint64_t;
using METHOD_REQ_ID = int;

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

enum METHOD_REQUEST
{
  METHOD_REQUEST_ARG_PREPARE = 0x40,
  METHOD_REQUEST_INVOKE = 0x01,
  METHOD_REQUEST_CALLBACK = 0x08,
  METHOD_REQUEST_END = 0x20,

  METHOD_REQUEST_COMPILE = 0x80,
  METHOD_REQUEST_SQL_SEMANTICS = 0xA0
};

enum METHOD_RESPONSE
{
  METHOD_RESPONSE_SUCCESS,
  METHOD_RESPONSE_ERROR
};

enum METHOD_CALLBACK_RESPONSE
{
  METHOD_CALLBACK_END_TRANSACTION = 1,
  METHOD_CALLBACK_QUERY_PREPARE = 2,
  METHOD_CALLBACK_QUERY_EXECUTE = 3,
  METHOD_CALLBACK_GET_DB_PARAMETER = 4,

  METHOD_CALLBACK_CURSOR = 7,
  METHOD_CALLBACK_FETCH = 8,
  METHOD_CALLBACK_GET_SCHEMA_INFO = 9,

  METHOD_CALLBACK_OID_GET = 10,
  METHOD_CALLBACK_OID_PUT = 11,
  METHOD_CALLBACK_OID_CMD = 17,
  METHOD_CALLBACK_COLLECTION = 18,

  // METHOD_CALLBACK_GET_DB_VERSION = 15,

  METHOD_CALLBACK_NEXT_RESULT = 19,

  METHOD_CALLBACK_EXECUTE_BATCH = 20,
  METHOD_CALLBACK_EXECUTE_ARRAY = 21,

  METHOD_CALLBACK_CURSOR_UPDATE = 22,

  METHOD_CALLBACK_MAKE_OUT_RS = 33,
  METHOD_CALLBACK_GET_GENERATED_KEYS = 34,

  METHOD_CALLBACK_LOB_NEW = 35,
  METHOD_CALLBACK_LOB_WRITE = 36,
  METHOD_CALLBACK_LOB_READ = 37,

  METHOD_CALLBACK_CURSOR_CLOSE = 42,

  // COMPILE
  METHOD_CALLBACK_GET_SQL_SEMNATICS = 100,
  METHOD_CALLBACK_GET_GLOBAL_SEMANTICS = 101,
};

enum METHOD_ARG_MODE
{
  METHOD_ARG_MODE_IN = 1,
  METHOD_ARG_MODE_OUT,
  METHOD_ARG_MODE_INOUT
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

  method_sig_node &operator= (const method_sig_node &rhs);

  method_sig_node ();
  method_sig_node (method_sig_node &&); // move constructor
  method_sig_node (const method_sig_node &obj); // copy constructor
  ~method_sig_node ();
};

struct method_sig_list : public cubpacking::packable_object
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
