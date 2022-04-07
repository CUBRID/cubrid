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

#include "packer.hpp"
#include "packable_object.hpp"

namespace cubmethod
{
  enum METHOD_CALLBACK_ERROR_CODE
  {
    METHOD_CALLBACK_ER_INTERNAL = -10001,
    METHOD_CALLBACK_ER_NO_MORE_MEMORY = -10002,
    METHOD_CALLBACK_ER_COMMUNICATION = -10003,
    METHOD_CALLBACK_ER_ARGS = -10004,
    METHOD_CALLBACK_ER_TRAN_TYPE = -10005,
    METHOD_CALLBACK_ER_SRV_HANDLE = -10006,
    METHOD_CALLBACK_ER_NUM_BIND = -10007,
    METHOD_CALLBACK_ER_UNKNOWN_U_TYPE = -10008,
    METHOD_CALLBACK_ER_TYPE_CONVERSION = -10010,
    METHOD_CALLBACK_ER_PARAM_NAME = -10011,
    METHOD_CALLBACK_ER_NO_MORE_DATA = -10012,
    METHOD_CALLBACK_ER_OBJECT = -10013,
    METHOD_CALLBACK_ER_OPEN_FILE = -10014,
    METHOD_CALLBACK_ER_SCHEMA_TYPE = -10015,
    METHOD_CALLBACK_ER_NOT_COLLECTION = -10020,
    METHOD_CALLBACK_ER_COLLECTION_DOMAIN = -10021,
    METHOD_CALLBACK_ER_NO_MORE_RESULT_SET = -10022,
    METHOD_CALLBACK_ER_INVALID_CALL_STMT = -10023,
    METHOD_CALLBACK_ER_STMT_POOLING = -10024,
    METHOD_CALLBACK_ER_MAX_PREPARED_STMT_COUNT_EXCEEDED = -10026,
    METHOD_CALLBACK_ER_NOT_IMPLEMENTED = -10100,
  };

  class error_context : public cubpacking::packable_object
  {
    public:
      error_context ();
      bool has_error ();
      void clear ();
      int get_error ();
      std::string get_error_msg ();

      void set_error (int number, const char *msg, const char *file, int line);

      void pack (cubpacking::packer &serializator) const override;
      void unpack (cubpacking::unpacker &deserializator) override;
      size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const override;

    private:
      int err_id;
      std::string err_string;
      std::string err_file;
      int err_line;
  };
}

#endif
