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
 * xasl_regu_alloc - allocate/initialize XASL structures (for XASL generation)
 */

#ifndef _XASL_REGU_ALLOC_HPP_
#define _XASL_REGU_ALLOC_HPP_

#if defined (SERVER_MODE)
#error Does not belong to server module
#endif /* defined (SERVER_MODE) */

#include "dbtype_def.h"       // OID, DB_TYPE
#include "storage_common.h"   // HFID
#include "xasl.h"             // PROC_TYPE, TARGET_TYPE

// forward definitions
struct access_spec_node;
struct arith_list_node;
struct db_value;
struct function_node;
struct indx_info;
struct key_range;
struct qfile_list_id;
class regu_variable_node;
struct regu_variable_list_node;
struct selupd_list;
struct sort_list;
struct tp_domain;
struct update_assignment;
struct upddel_class_info;
struct xasl_node;

namespace cubxasl
{
  struct aggregate_list_node;
  struct analytic_list_node;
  struct pred_expr;
}

template <typename T>
void regu_init (T &t);

void regu_init (db_value &dbval);
void regu_init (regu_variable_node &regu);
void regu_init (regu_variable_list_node &regu_varlist);
void regu_init (cubxasl::pred_expr &pr);
void regu_init (indx_info &ii);
void regu_init (tp_domain &dom);
void regu_init (selupd_list &selupd);
void regu_init (key_range &kr);
void regu_init (OID &oid);
void regu_init (HFID &hfid);
void regu_init (upddel_class_info &upddel);
void regu_init (update_assignment &assign);
void regu_init (arith_list_node &arith);
void regu_init (function_node &fnode);
void regu_init (cubxasl::aggregate_list_node &agg);
void regu_init (cubxasl::analytic_list_node &ana);
void regu_init (xasl_node &node);
void regu_init (sort_list &sl);
void regu_init (qfile_list_id &list_id);
void regu_init (access_spec_node &spec);

template <typename T>
void regu_alloc (T *&ptr);

template <typename T>
void regu_array_alloc (T **ptr, size_t size);

/* for regu_machead_array () */
int *regu_int_array_alloc (int size);
OID *regu_oid_array_alloc (int size);

void regu_dbval_type_init (db_value *ptr, DB_TYPE type);

xasl_node *regu_xasl_node_alloc (PROC_TYPE type);
access_spec_node *regu_spec_alloc (TARGET_TYPE type);

//////////////////////////////////////////////////////////////////////////
// inline/template implementation
//////////////////////////////////////////////////////////////////////////

#include "parser_support.h"

template <typename T>
void
regu_init (T &t)
{
  t = T ();
}

template <typename T>
void
regu_alloc (T *&ptr)
{
  ptr = reinterpret_cast<T *> (pt_alloc_packing_buf ((int) sizeof (T)));
  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return;
    }
  regu_init (*ptr);
}

template <typename T>
void
regu_array_alloc (T **ptr, size_t size)
{
  if (size == 0)
    {
      *ptr = NULL;
      return;
    }
  *ptr = reinterpret_cast<T *> (pt_alloc_packing_buf ((int) (sizeof (T) * size)));
  if (*ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return;
    }
  for (size_t idx = 0; idx < size; idx++)
    {
      regu_init ((*ptr)[idx]);
    }
}

#endif /* _XASL_REGU_ALLOC_HPP_ */
