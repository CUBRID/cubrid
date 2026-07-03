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
 * Value fetch routines
 */

#ifndef _FETCH_H_
#define _FETCH_H_

#ident "$Id$"

#include "storage_common.h"
#include "oid.h"
#include "query_evaluator.h"
#include "query_list.h"
#include "regu_var.hpp"		/* REGU_VARIABLE definition + flags for the inline fetch_peek_dbval () */
#include "query_executor.h"	/* val_descr definition for the inline fetch_peek_dbval () TYPE_POS_VALUE path */

// forward definitions
struct regu_variable_list_node;

extern int fetch_peek_dbval_slow (THREAD_ENTRY * thread_p, regu_variable_node * regu_var, val_descr * vd,
				  OID * class_oid, OID * obj_oid, QFILE_TUPLE tpl, DB_VALUE ** peek_dbval);

/*
 * fetch_peek_dbval () - returns a POINTER to an existing db_value
 *   Inline fast-path for the dominant per-row cases: a regu_var previously confirmed simple
 *   (REGU_VARIABLE_FAST_PEEK set by fetch_peek_dbval_slow () on the first fetch). Returns the value
 *   pointer directly - no call frame, no type switch, no domain dereference:
 *     TYPE_DBVAL     -> the embedded constant db_value;
 *     TYPE_CONSTANT  -> the value-pointer slot, only when there is no linked subquery to execute;
 *     TYPE_POS_VALUE -> the value-list slot at the fixed position (live slot; value changes per row);
 *     TYPE_*ATTR_ID  -> the cached attribute value pointer (instance/shared/class; re-checked != NULL
 *                       so a later cache reset is handled safely, same as the slow path).
 *   Everything else (incl. the first fetch that sets the flag, collation/variable-domain, subqueries)
 *   falls back to the full path.
 */
inline int
fetch_peek_dbval (THREAD_ENTRY * thread_p, regu_variable_node * regu_var, val_descr * vd, OID * class_oid,
		  OID * obj_oid, QFILE_TUPLE tpl, DB_VALUE ** peek_dbval)
{
  if (REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_FAST_PEEK))
    {
      switch (regu_var->type)
	{
	case TYPE_DBVAL:
	  *peek_dbval = &regu_var->value.dbval;
	  return NO_ERROR;
	case TYPE_CONSTANT:
	  /* flag is set only when there is no linked subquery (regu_var->xasl == NULL) to execute, so this
	   * is just the stable value-pointer slot maintained by the owner (e.g. a correlated column ref). */
	  *peek_dbval = regu_var->value.dbvalptr;
	  return NO_ERROR;
	case TYPE_POS_VALUE:
	  *peek_dbval = (DB_VALUE *) vd->dbval_ptr + regu_var->value.val_pos;
	  return NO_ERROR;
	case TYPE_ATTR_ID:
	case TYPE_SHARED_ATTR_ID:
	case TYPE_CLASS_ATTR_ID:
	  if (regu_var->value.attr_descr.cache_dbvalp != NULL)
	    {
	      *peek_dbval = regu_var->value.attr_descr.cache_dbvalp;
	      return NO_ERROR;
	    }
	  break;
	default:
	  break;
	}
    }
  return fetch_peek_dbval_slow (thread_p, regu_var, vd, class_oid, obj_oid, tpl, peek_dbval);
}

extern int fetch_copy_dbval (THREAD_ENTRY * thread_p, regu_variable_node * regu_var, val_descr * vd, OID * class_oid,
			     OID * obj_oid, QFILE_TUPLE tpl, DB_VALUE * dbval);
extern int fetch_val_list (THREAD_ENTRY * thread_p, regu_variable_list_node * regu_list, val_descr * vd,
			   OID * class_oid, OID * obj_oid, QFILE_TUPLE tpl, int peek);
extern void fetch_init_val_list (regu_variable_list_node * regu_list);

extern void fetch_force_not_const_recursive (regu_variable_node & reguvar);

extern DB_VALUE *fetch_peek_leftmost_numeric_regu (THREAD_ENTRY * thread_p, regu_variable_node * regu_var,
						   val_descr * vd);
extern int fetch_and_coerce_key_limit_lower (THREAD_ENTRY * thread_p, regu_variable_node * key_limit_l, val_descr * vd,
					     DB_VALUE * out_val);

#endif /* _FETCH_H_ */
