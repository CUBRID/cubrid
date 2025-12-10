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

/*
 * px_query_checker.hpp - module that checks whether parallel subqueries are possible.
 */

#ifndef _PX_QUERY_CHECKER_HPP_
#define _PX_QUERY_CHECKER_HPP_
#include "xasl.h"

extern "C" int check_parallel_subquery_possible (XASL_NODE *xasl);

#endif /*_PX_QUERY_CHECKER_HPP_ */
