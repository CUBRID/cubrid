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

#include "method_invoke.hpp"

#include <algorithm>

#include "method_struct_invoke.hpp"
#include "method_invoke_group.hpp"
#include "packer.hpp"
#include "method_connection_sr.hpp"

#if defined (SERVER_MODE)
#include "method_struct_query.hpp"
#else
#include "query_method.hpp"
#endif
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubmethod
{
  method_invoke_builtin::method_invoke_builtin (method_invoke_group *group, method_sig_node *method_sig)
    : method_invoke (group, method_sig)
  {
    //
  }

  int method_invoke_builtin::invoke (cubthread::entry *thread_p, std::vector<std::reference_wrapper<DB_VALUE>> &arg_base)
  {
    int error = NO_ERROR;
    cubmethod::header header (METHOD_REQUEST_INVOKE /* default */, m_group->get_id());
    cubmethod::invoke_builtin arg (m_method_sig);
    error = method_send_data_to_client (thread_p, header, arg);
    return error;
  }

  int
  method_invoke_builtin::get_return (cubthread::entry *thread_p, std::vector<std::reference_wrapper<DB_VALUE>> &arg_base,
				     DB_VALUE &result)
  {
    int error = NO_ERROR;
    db_value_clear (&result);

    auto get_method_result = [&] (cubmem::block & b)
    {
      int e = NO_ERROR;
      packing_unpacker unpacker (b);
      int status;
      unpacker.unpack_int (status);
      if (status == METHOD_SUCCESS)
	{
	  unpacker.unpack_db_value (result);
	}
      else
	{
	  unpacker.unpack_int (e);	/* er_errid */
	}
      return e;
    };

    error = xs_receive (thread_p, get_method_result);
    return error;
  }
} // namespace cubmethod
