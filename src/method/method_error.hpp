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
// method_error.hpp
//

#ifndef _METHOD_ERROR_HPP_
#define _METHOD_ERROR_HPP_

#include <string>

namespace cubmethod
{
  typedef enum
  {
    METHOD_CALLBACK_ER_DBMS = -10000,
    METHOD_CALLBACK_ER_INTERNAL = -10001,
    METHOD_CALLBACK_ER_NO_MORE_MEMORY = -10002,
    METHOD_CALLBACK_ER_COMMUNICATION = -10003,
    METHOD_CALLBACK_ER_ARGS = -10004,
    METHOD_CALLBACK_ER_TRAN_TYPE = -10005,
    METHOD_CALLBACK_ER_SRV_HANDLE = -10006,
    METHOD_CALLBACK_ER_NUM_BIND = -10007,
    METHOD_CALLBACK_ER_UNKNOWN_U_TYPE = -10008,
    METHOD_CALLBACK_ER_DB_VALUE = -10009,
    METHOD_CALLBACK_ER_TYPE_CONVERSION = -10010,
    METHOD_CALLBACK_ER_PARAM_NAME = -10011,
    METHOD_CALLBACK_ER_NO_MORE_DATA = -10012,
    METHOD_CALLBACK_ER_OBJECT = -10013,
    METHOD_CALLBACK_ER_OPEN_FILE = -10014,
    METHOD_CALLBACK_ER_SCHEMA_TYPE = -10015,
    METHOD_CALLBACK_ER_VERSION = -10016,
    METHOD_CALLBACK_ER_FREE_SERVER = -10017,
    METHOD_CALLBACK_ER_NOT_AUTHORIZED_CLIENT = -10018,
    METHOD_CALLBACK_ER_QUERY_CANCEL = -10019,
    METHOD_CALLBACK_ER_NOT_COLLECTION = -10020,
    METHOD_CALLBACK_ER_COLLECTION_DOMAIN = -10021,
    METHOD_CALLBACK_ER_NO_MORE_RESULT_SET = -10022,
    METHOD_CALLBACK_ER_INVALID_CALL_STMT = -10023,
    METHOD_CALLBACK_ER_STMT_POOLING = -10024,
    METHOD_CALLBACK_ER_DBSERVER_DISCONNECTED = -10025,
    METHOD_CALLBACK_ER_MAX_PREPARED_STMT_COUNT_EXCEEDED = -10026,
    METHOD_CALLBACK_ER_HOLDABLE_NOT_ALLOWED = -10027,
    METHOD_CALLBACK_ER_NOT_IMPLEMENTED = -10100,
    METHOD_CALLBACK_ER_MAX_CLIENT_EXCEEDED = -10101,
    METHOD_CALLBACK_ER_INVALID_CURSOR_POS = -10102,
    METHOD_CALLBACK_ER_SSL_TYPE_NOT_ALLOWED = -10103,
    METHOD_CALLBACK_ER_IS = -10200,
  } METHOD_CALLBACK_ERROR_CODE;

  class error_context
  {
    public:
      int get_error ();
      int set_error (int err_number, int err_indicator, std::string file, int line);
      int set_error_with_msg (int err_number, int err_indicator, std::string err_msg, std::string err_file, int line,
			      bool force);

    private:
      int err_indicator;
      int err_number;
      std::string err_string;
      std::string err_file;
      int err_line;
  };
}

#endif
