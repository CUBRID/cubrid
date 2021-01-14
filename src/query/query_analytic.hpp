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
// query_analytic - interface for analytic query execution
//

#ifndef _QUERY_ANALYTIC_HPP_
#define _QUERY_ANALYTIC_HPP_

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Wrong module
#endif // not server and not SA

#include "system.h"               // QUERY_ID

// forward definitions
struct val_descr;

namespace cubthread
{
  class entry;
}

namespace cubxasl
{
  struct analytic_list_node;
} // namespace cubxasl

int qdata_initialize_analytic_func (cubthread::entry *thread_p, cubxasl::analytic_list_node *func_p, QUERY_ID query_id);
int qdata_evaluate_analytic_func (cubthread::entry *thread_p, cubxasl::analytic_list_node *func_p, val_descr *vd);
int qdata_finalize_analytic_func (cubthread::entry *thread_p, cubxasl::analytic_list_node *func_p, bool is_same_group);

#endif // _QUERY_ANALYTIC_HPP_
