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

#ifndef _SP_CONSTANTS_HPP_
#define _SP_CONSTANTS_HPP_

#define PKG_ATTR_LIST    \
    MAP_LIST_ITEM(UNIQUE_NAME) \
    MAP_LIST_ITEM(PKG_NAME) \
    MAP_LIST_ITEM(FLAGS) \
    MAP_LIST_ITEM(OWNER) \
    MAP_LIST_ITEM(CODE) \
    MAP_LIST_ITEM(PROCEDURES_CNT) \
    MAP_LIST_ITEM(PROCEDURES) \
    MAP_LIST_ITEM(VARIABLES_CNT) \
    MAP_LIST_ITEM(VARIABLES) \
    MAP_LIST_ITEM(EXCEPTIONS_CNT) \
    MAP_LIST_ITEM(EXCEPTIONS) \
    MAP_LIST_ITEM(CURSORS_CNT) \
    MAP_LIST_ITEM(CURSORS) \
    MAP_LIST_ITEM(RECORD_TYPES_CNT) \
    MAP_LIST_ITEM(RECORD_TYPES) \
    MAP_LIST_ITEM(COMMENT) \
    MAP_LIST_ITEM(CREATED_TIME) \
    MAP_LIST_ITEM(UPDATED_TIME)

#define PKG_ATTR_UNIQUE_NAME         "unique_name"
#define PKG_ATTR_PKG_NAME            "pkg_name"
#define PKG_ATTR_FLAGS               "flags"
#define PKG_ATTR_OWNER               "owner"
#define PKG_ATTR_CODE                "code"
#define PKG_ATTR_PROCEDURES_CNT      "procedures_cnt"
#define PKG_ATTR_PROCEDURES          "procedures"
#define PKG_ATTR_VARIABLES_CNT       "variables_cnt"
#define PKG_ATTR_VARIABLES           "variables"
#define PKG_ATTR_EXCEPTIONS_CNT      "exceptions_cnt"
#define PKG_ATTR_EXCEPTIONS          "exceptions"
#define PKG_ATTR_CURSORS_CNT         "cursors_cnt"
#define PKG_ATTR_CURSORS             "cursors"
#define PKG_ATTR_RECORD_TYPES_CNT    "record_types_cnt"
#define PKG_ATTR_RECORD_TYPES        "record_types"
#define PKG_ATTR_COMMENT             "comment"
#define PKG_ATTR_CREATED_TIME        "created_time"
#define PKG_ATTR_UPDATED_TIME        "updated_time"

enum index_pkg_attr
{
#define MAP_LIST_ITEM(item)     INDEX_PKG_ATTR_##item,
  PKG_ATTR_LIST
#undef MAP_LIST_ITEM
  NUM_PKG_ATTR
};

#define PKG_CODE_ATTR_LIST    \
    MAP_LIST_ITEM(PKG_UNIQUE_NAME) \
    MAP_LIST_ITEM(NAME) \
    MAP_LIST_ITEM(COMPILE_ID) \
    MAP_LIST_ITEM(STYPE) \
    MAP_LIST_ITEM(SCODE_SPEC) \
    MAP_LIST_ITEM(SCODE_BODY) \
    MAP_LIST_ITEM(OTYPE) \
    MAP_LIST_ITEM(OCODE)

#define PKG_CODE_ATTR_PKG_UNIQUE_NAME       "pkg_unique_name"
#define PKG_CODE_ATTR_NAME                  "name"
#define PKG_CODE_ATTR_COMPILE_ID            "compile_id"
#define PKG_CODE_ATTR_STYPE                 "stype"
#define PKG_CODE_ATTR_SCODE_SPEC            "scode_spec"
#define PKG_CODE_ATTR_SCODE_BODY            "scode_body"
#define PKG_CODE_ATTR_OTYPE                 "otype"
#define PKG_CODE_ATTR_OCODE                 "ocode"

#define PKG_CODE_ATTR_PKG_UNIQUE_NAME_LEN   (255)
#define PKG_CODE_ATTR_NAME_LEN              (255)
#define PKG_CODE_ATTR_COMPILE_ID_LEN        (39)    // 19 (max long len) * 2 + 1 (delimiter)

enum index_pkg_code_attr
{
#define MAP_LIST_ITEM(item)     INDEX_PKG_CODE_ATTR_##item,
  PKG_CODE_ATTR_LIST
#undef MAP_LIST_ITEM
  NUM_PKG_CODE_ATTR
};

#define PKG_VAR_ATTR_LIST    \
    MAP_LIST_ITEM(PKG_UNIQUE_NAME) \
    MAP_LIST_ITEM(NAME) \
    MAP_LIST_ITEM(DATA_TYPE) \
    MAP_LIST_ITEM(PREC) \
    MAP_LIST_ITEM(SCALE) \
    MAP_LIST_ITEM(FLAGS) \
    MAP_LIST_ITEM(COMMENT)

#define PKG_VAR_ATTR_PKG_UNIQUE_NAME                "pkg_unique_name"
#define PKG_VAR_ATTR_NAME                           "name"
#define PKG_VAR_ATTR_DATA_TYPE                      "data_type"
#define PKG_VAR_ATTR_PREC                           "prec"
#define PKG_VAR_ATTR_SCALE                          "scale"
#define PKG_VAR_ATTR_FLAGS                          "flags"
#define PKG_VAR_ATTR_COMMENT                        "comment"

enum index_pkg_var_attr
{
#define MAP_LIST_ITEM(item)     INDEX_PKG_VAR_ATTR_##item,
  PKG_VAR_ATTR_LIST
#undef MAP_LIST_ITEM
  NUM_PKG_VAR_ATTR
};

#define PKG_EXCEPTION_ATTR_LIST    \
    MAP_LIST_ITEM(PKG_UNIQUE_NAME) \
    MAP_LIST_ITEM(NAME) \
    MAP_LIST_ITEM(COMMENT)

#define PKG_EXCEPTION_ATTR_PKG_UNIQUE_NAME          "pkg_unique_name"
#define PKG_EXCEPTION_ATTR_NAME                     "name"
#define PKG_EXCEPTION_ATTR_COMMENT                  "comment"

enum index_pkg_exception_attr
{
#define MAP_LIST_ITEM(item)     INDEX_PKG_EXCEPTION_ATTR_##item,
  PKG_EXCEPTION_ATTR_LIST
#undef MAP_LIST_ITEM
  NUM_PKG_EXCEPTION_ATTR
};

#define PKG_CURSOR_ATTR_LIST    \
    MAP_LIST_ITEM(PKG_UNIQUE_NAME) \
    MAP_LIST_ITEM(NAME) \
    MAP_LIST_ITEM(RECORD_TYPE) \
    MAP_LIST_ITEM(PARAMETERS) \
    MAP_LIST_ITEM(COMMENT)

#define PKG_CURSOR_ATTR_PKG_UNIQUE_NAME             "pkg_unique_name"
#define PKG_CURSOR_ATTR_NAME                        "name"
#define PKG_CURSOR_ATTR_RECORD_TYPE                 "record_type"
#define PKG_CURSOR_ATTR_PARAMETERS                  "parameters"
#define PKG_CURSOR_ATTR_COMMENT                     "comment"

enum index_pkg_cursor_attr
{
#define MAP_LIST_ITEM(item)     INDEX_PKG_CURSOR_ATTR_##item,
  PKG_CURSOR_ATTR_LIST
#undef MAP_LIST_ITEM
  NUM_PKG_CURSOR_ATTR
};

#define PKG_RECORD_TYPE_ATTR_LIST    \
    MAP_LIST_ITEM(PKG_UNIQUE_NAME) \
    MAP_LIST_ITEM(NAME) \
    MAP_LIST_ITEM(FIELDS) \
    MAP_LIST_ITEM(COMMENT)

#define PKG_RECORD_TYPE_ATTR_PKG_UNIQUE_NAME        "pkg_unique_name"
#define PKG_RECORD_TYPE_ATTR_NAME                   "name"
#define PKG_RECORD_TYPE_ATTR_FIELDS                 "fields"
#define PKG_RECORD_TYPE_ATTR_COMMENT                "comment"

enum index_pkg_record_type_attr
{
#define MAP_LIST_ITEM(item)     INDEX_PKG_RECORD_TYPE_ATTR_##item,
  PKG_RECORD_TYPE_ATTR_LIST
#undef MAP_LIST_ITEM
  NUM_PKG_RECORD_TYPE_ATTR
};

#define SP_ATTR_LIST    \
    MAP_LIST_ITEM(UNIQUE_NAME) \
    MAP_LIST_ITEM(SP_NAME) \
    MAP_LIST_ITEM(SP_TYPE) \
    MAP_LIST_ITEM(RETURN_TYPE) \
    MAP_LIST_ITEM(ARG_COUNT) \
    MAP_LIST_ITEM(ARGS) \
    MAP_LIST_ITEM(LANG) \
    MAP_LIST_ITEM(PKG_NAME) \
    MAP_LIST_ITEM(IS_SYSTEM_GENERATED) \
    MAP_LIST_ITEM(DIRECTIVE) \
    MAP_LIST_ITEM(TARGET_CLASS) \
    MAP_LIST_ITEM(TARGET_METHOD) \
    MAP_LIST_ITEM(OWNER) \
    MAP_LIST_ITEM(SQL_DATA_ACCESS) \
    MAP_LIST_ITEM(COMMENT) \
    MAP_LIST_ITEM(CREATED_TIME) \
    MAP_LIST_ITEM(UPDATED_TIME)

#define SP_ATTR_UNIQUE_NAME             "unique_name"
#define SP_ATTR_SP_NAME                 "sp_name"
#define SP_ATTR_SP_TYPE                 "sp_type"
#define SP_ATTR_RETURN_TYPE             "return_type"
#define SP_ATTR_ARG_COUNT               "arg_count"
#define SP_ATTR_ARGS                    "args"
#define SP_ATTR_LANG                    "lang"
#define SP_ATTR_PKG_NAME                "pkg_name"
#define SP_ATTR_IS_SYSTEM_GENERATED     "is_system_generated"
#define SP_ATTR_DIRECTIVE               "directive"
#define SP_ATTR_TARGET_CLASS            "target_class"
#define SP_ATTR_TARGET_METHOD           "target_method"
#define SP_ATTR_OWNER                   "owner"
#define SP_ATTR_SQL_DATA_ACCESS         "sql_data_access"
#define SP_ATTR_COMMENT                 "comment"
#define SP_ATTR_CREATED_TIME            "created_time"
#define SP_ATTR_UPDATED_TIME            "updated_time"

#define SP_ATTR_UNIQUE_NAME_LEN         (255)
#define SP_ATTR_SP_NAME_LEN             (255)
#define SP_ATTR_PKG_NAME_LEN            (1024)
#define SP_ATTR_TARGET_CLASS_LEN        (255)
#define SP_ATTR_TARGET_METHOD_LEN       (4096)
#define SP_ATTR_COMMENT_LEN             (1024)


enum index_sp_attr
{
#define MAP_LIST_ITEM(item)     INDEX_SP_ATTR_##item,
  SP_ATTR_LIST
#undef MAP_LIST_ITEM
  NUM_SP_ATTR
};

#define SP_ARG_ATTR_LIST    \
    MAP_LIST_ITEM(SP_OF) \
    MAP_LIST_ITEM(INDEX_OF) \
    MAP_LIST_ITEM(IS_SYSTEM_GENERATED) \
    MAP_LIST_ITEM(ARG_NAME) \
    MAP_LIST_ITEM(DATA_TYPE) \
    MAP_LIST_ITEM(MODE) \
    MAP_LIST_ITEM(DEFAULT_VALUE) \
    MAP_LIST_ITEM(IS_OPTIONAL) \
    MAP_LIST_ITEM(COMMENT)

#define SP_ARG_ATTR_SP_OF                   "sp_of"
#define SP_ARG_ATTR_INDEX_OF                "index_of"
#define SP_ARG_ATTR_IS_SYSTEM_GENERATED     "is_system_generated"
#define SP_ARG_ATTR_ARG_NAME                "arg_name"
#define SP_ARG_ATTR_DATA_TYPE               "data_type"
#define SP_ARG_ATTR_MODE                    "mode"
#define SP_ARG_ATTR_DEFAULT_VALUE           "default_value"
#define SP_ARG_ATTR_IS_OPTIONAL             "is_optional"
#define SP_ARG_ATTR_COMMENT                 "comment"

enum index_sp_arg_attr
{
#define MAP_LIST_ITEM(item)     INDEX_SP_ARG_ATTR_##item,
  SP_ARG_ATTR_LIST
#undef MAP_LIST_ITEM
  NUM_SP_ARG_ATTR
};

#define SP_CODE_ATTR_LIST    \
    MAP_LIST_ITEM(NAME) \
    MAP_LIST_ITEM(COMPILE_ID) \
    MAP_LIST_ITEM(CREATED_TIME) \
    MAP_LIST_ITEM(OWNER) \
    MAP_LIST_ITEM(IS_STATIC) \
    MAP_LIST_ITEM(IS_SYSTEM_GENERATED) \
    MAP_LIST_ITEM(STYPE) \
    MAP_LIST_ITEM(SCODE) \
    MAP_LIST_ITEM(OTYPE) \
    MAP_LIST_ITEM(OCODE)

#define SP_CODE_ATTR_NAME                   "name"
#define SP_CODE_ATTR_COMPILE_ID             "compile_id"
#define SP_CODE_ATTR_CREATED_TIME           "created_time"
#define SP_CODE_ATTR_OWNER                  "owner"
#define SP_CODE_ATTR_IS_STATIC              "is_static"
#define SP_CODE_ATTR_IS_SYSTEM_GENERATED    "is_system_generated"
#define SP_CODE_ATTR_STYPE                  "stype"
#define SP_CODE_ATTR_SCODE                  "scode"
#define SP_CODE_ATTR_OTYPE                  "otype"
#define SP_CODE_ATTR_OCODE                  "ocode"

#define SP_CODE_ATTR_COMPILE_ID_LEN              (39)    // 19 (max long len) * 2 + 1 (delimiter)

enum index_sp_code_attr
{
#define MAP_LIST_ITEM(item)     INDEX_SP_CODE_ATTR_##item,
  SP_CODE_ATTR_LIST
#undef MAP_LIST_ITEM
  NUM_SP_CODE_ATTR
};


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

typedef enum
{
  SP_SQL_TYPE_UNKNOWN = -1,
  SP_SQL_TYPE_NO_SQL,
  SP_SQL_TYPE_CONTAINS_SQL,
  SP_SQL_TYPE_READS_SQL_DATA,
  SP_SQL_TYPE_MODIFIES_SQL_DATA
} SP_SQL_DATA_ACCESS_TYPE;

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
  METHOD_REQUEST_ERROR = 0x04,
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

  METHOD_CALLBACK_SET_PL_SESSION_PARAM = 50,

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

enum PKG_FLAGS
{
  PKG_FLAGS_SYSTEM_GENERATED = (1 << 0)
};

enum PKG_VAR_FLAGS
{
  PKG_VAR_CONSTANT = (1 << 0),
  PKG_VAR_NOT_NULL = (1 << 1)
};

#define METHOD_GROUP_ID uint64_t
#define METHOD_REQ_ID int

#endif // _SP_CONSTANTS_HPP_
