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

#ifndef _SP_CONSTASNTS_HPP_
#define _SP_CONSTASNTS_HPP_

#define SP_CLASS_NAME           "_db_stored_procedure"
#define SP_ARG_CLASS_NAME       "_db_stored_procedure_args"
#define SP_CODE_CLASS_NAME      "_db_stored_procedure_code"

#define SP_ATTR_UNIQUE_NAME             "unique_name"
#define SP_ATTR_NAME                    "sp_name"
#define SP_ATTR_SP_TYPE                 "sp_type"
#define SP_ATTR_RETURN_TYPE             "return_type"
#define SP_ATTR_ARGS                    "args"
#define SP_ATTR_ARG_COUNT               "arg_count"
#define SP_ATTR_LANG                    "lang"
#define SP_ATTR_PKG                     "pkg_name"
#define SP_ATTR_IS_SYSTEM_GENERATED     "is_system_generated"
#define SP_ATTR_TARGET_CLASS            "target_class"
#define SP_ATTR_TARGET_METHOD           "target_method"
#define SP_ATTR_DIRECTIVE               "directive"
#define SP_ATTR_OWNER                   "owner"
#define SP_ATTR_COMMENT                 "comment"

#define SP_ATTR_SP_OF                   "sp_of"
#define SP_ATTR_ARG_NAME                "arg_name"
#define SP_ATTR_INDEX_OF_NAME           "index_of"
#define SP_ATTR_DATA_TYPE               "data_type"
#define SP_ATTR_MODE                    "mode"
#define SP_ATTR_DEFAULT_VALUE           "default_value"
#define SP_ATTR_IS_OPTIONAL             "is_optional"
#define SP_ATTR_ARG_COMMENT             "comment"

#define SP_ATTR_CLS_NAME                "name"
#define SP_ATTR_TIMESTAMP               "created_time"
#define SP_ATTR_IS_STATIC               "is_static"
#define SP_ATTR_SOURCE_TYPE             "stype"
#define SP_ATTR_SOURCE_CODE             "scode"
#define SP_ATTR_OBJECT_TYPE             "otype"
#define SP_ATTR_OBJECT_CODE             "ocode"

#define SP_MAX_DEFAULT_VALUE_LEN        255

typedef enum
{
  SP_TYPE_PROCEDURE = 1,
  SP_TYPE_FUNCTION
} SP_TYPE_ENUM;

typedef enum
{
  SP_MODE_IN = 1,
  SP_MODE_OUT,
  SP_MODE_INOUT
} SP_MODE_ENUM;

typedef enum
{
  SP_LANG_PLCSQL = 0,
  SP_LANG_JAVA = 1
} SP_LANG_ENUM;

// refactor following

#define METHOD_MAX_RECURSION_DEPTH 15

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
  METHOD_TYPE_JAVA_SP,
  METHOD_TYPE_PLCSQL
};

enum METHOD_AUTH
{
  METHOD_AUTH_OWNER = 0,
  METHOD_AUTH_INVOKER = 1
};

enum METHOD_REQUEST
{
  METHOD_REQUEST_ARG_PREPARE = 0x40,
  METHOD_REQUEST_INVOKE = 0x01,
  METHOD_REQUEST_CALLBACK = 0x08,
  METHOD_REQUEST_END = 0x20,

  METHOD_REQUEST_COMPILE = 0x80,
  METHOD_REQUEST_SQL_SEMANTICS = 0xA0,
  METHOD_REQUEST_GLOBAL_SEMANTICS = 0xA1
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
  METHOD_CALLBACK_GET_SQL_SEMANTICS = 100,
  METHOD_CALLBACK_GET_GLOBAL_SEMANTICS = 101,

  // AUTH
  METHOD_CALLBACK_CHANGE_RIGHTS = 200,

  // CLASS ACCESS
  METHOD_CALLBACK_GET_CODE_ATTR = 201
};

enum METHOD_ARG_MODE
{
  METHOD_ARG_MODE_IN = 1,
  METHOD_ARG_MODE_OUT,
  METHOD_ARG_MODE_INOUT
};

#define METHOD_GROUP_ID uint64_t
#define METHOD_REQ_ID int


#endif // _SP_CONSTASNTS_HPP_