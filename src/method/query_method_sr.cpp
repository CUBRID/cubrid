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

/*
 * External definitions for method calls in queries
 */

#include "query_method_sr.hpp"

#include "dbtype_function.h"
#include "thread_compat.hpp"
#include "method_def.hpp"	/* method_sig_list, method_sig_node */
#include "method_invoke_group.hpp"

int xmethod_invoke_fold_constants (THREAD_ENTRY *thread_p, const method_sig_list &sig_list, std::vector<std::reference_wrapper<DB_VALUE>> &args,
				   DB_VALUE &result)
{
  int error_code = NO_ERROR;
  cubmethod::method_invoke_group method_group (thread_p, sig_list);
  method_group.begin ();
  error_code = method_group.prepare (args);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = method_group.execute (args);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  DB_VALUE *res = method_group.get_return_value (0);
  db_value_clone (res, &result);

  method_group.end ();
  return error_code;
}