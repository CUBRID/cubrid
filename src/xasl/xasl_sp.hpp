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
// xasl_sp.hpp - XASL structures used for stored procedures
//

#ifndef _XASL_STORED_PROCEDURE_HPP_
#define _XASL_STORED_PROCEDURE_HPP_

#include "dbtype_def.h"
#include "storage_common.h"
#include "pl_signature.hpp"

// forward definitions
struct regu_variable_list_node;
class regu_variable_node;

namespace cubxasl
{
  struct sp_node
  {
    cubpl::pl_signature *sig;
    regu_variable_list_node *args;
    DB_VALUE *value; // return value
  };
};

// legacy aliases
using SP_TYPE = cubxasl::sp_node;

#endif // _XASL_STORED_PROCEDURE_HPP_
