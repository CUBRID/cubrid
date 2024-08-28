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

#include "method_error.hpp"

#include "error_manager.h"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubmethod
{
  void handle_method_error (int err_id, const std::string &err_msg)
  {
    switch (err_id)
      {
      /* no arg */
      case ER_INTERRUPTED:
      case ER_SP_TOO_MANY_NESTED_CALL:
      case ER_NET_SERVER_SHUTDOWN:
      case ER_SP_NOT_RUNNING_JVM:
      case ER_SES_SESSION_EXPIRED:
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err_id, 0);
	break;

      /* 1 arg */
      case ER_SP_CANNOT_CONNECT_JVM:
      case ER_SP_NETWORK_ERROR:
      case ER_OUT_OF_VIRTUAL_MEMORY:
      case ER_SP_EXECUTE_ERROR:
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err_id, 1, err_msg.c_str ());
	break;
      default:
	/* do nothing */
	break;
      }
  }

  error_context::error_context ()
  {
    clear ();
  }

  bool
  error_context::has_error ()
  {
    return (err_id != 0);
  }

  void
  error_context::clear ()
  {
    err_id = 0;
    err_string.clear ();
    err_file.clear ();
    err_line = 0;
  }

  int
  error_context::get_error ()
  {
    return err_id;
  }

  std::string
  error_context::get_error_msg ()
  {
    return err_string;
  }

  void
  error_context::set_error (int number, const char *msg, const char *file, int line)
  {
    err_id = number;
    err_string.assign (msg ? msg : "");
    err_file.assign (file ? file : "");
    err_line = line;
  }

#define ERROR_CONTEXT_ARGS() \
  err_id, err_string, err_file, err_line

  void
  error_context::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_all (ERROR_CONTEXT_ARGS ());
  }

  void
  error_context::unpack (cubpacking::unpacker &deserializator)
  {
    deserializator.unpack_all (ERROR_CONTEXT_ARGS ());
  }

  size_t
  error_context::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    return serializator.get_all_packed_size_starting_offset (start_offset, ERROR_CONTEXT_ARGS ());
  }
} // namespace cubmethod
