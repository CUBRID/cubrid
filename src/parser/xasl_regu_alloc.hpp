/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
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
struct pred_expr_with_context;
struct qfile_list_id;
struct qfile_sorted_list_id;
struct regu_variable_node;
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
void regu_init (pred_expr_with_context &pred);
void regu_init (sort_list &sl);
void regu_init (qfile_list_id &list_id);
void regu_init (qfile_sorted_list_id &list_id);
void regu_init (access_spec_node &spec);

template <typename T>
void regu_alloc (T *&ptr);

template <typename T>
void regu_array_alloc (T **ptr, size_t size);

/* for regu_machead_array () */
inline int *regu_int_array_alloc (int size);
inline OID *regu_oid_array_alloc (int size);

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

int *
regu_int_array_alloc (int size)
{
  int *ret_array = NULL;
  assert (size > 0);
  regu_array_alloc (&ret_array, (size_t) size);
  return ret_array;
}

OID *
regu_oid_array_alloc (int size)
{
  OID *ret_array = NULL;
  assert (size > 0);
  regu_array_alloc (&ret_array, (size_t) size);
  return ret_array;
}

#endif /* _XASL_REGU_ALLOC_HPP_ */
