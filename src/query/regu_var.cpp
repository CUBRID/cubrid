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
 * Regular variable - implementation.
 */

#include "regu_var.hpp"

#include "memory_alloc.h"
#include "object_primitive.h"
#include "xasl_predicate.hpp"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

using namespace cubxasl;    // it should belong to cubxasl namespace

void
regu_variable_node::map_regu (const map_regu_func_type &func)
{
  bool stop = false;
  map_regu (func, stop);
}

void
regu_variable_node::map_regu (const map_regu_func_type &func, bool &stop)
{
  // helper macros to avoid repeating code
#define map_regu_and_check_stop(regu) \
  { (regu)->map_regu (func, stop); if (stop) return; }
#define map_regu_not_null_and_check_stop(regu) \
  if ((regu) != NULL) map_regu_and_check_stop (regu)

  // apply function to me
  func (*this, stop);
  if (stop)
    {
      return;
    }
  switch (type)
    {
    case TYPE_INARITH:
    case TYPE_OUTARITH:
      if (value.arithptr == NULL)
	{
	  assert (false);
	  return;
	}
      map_regu_not_null_and_check_stop (value.arithptr->leftptr);
      map_regu_not_null_and_check_stop (value.arithptr->rightptr);
      break;

    case TYPE_FUNC:
      if (value.funcp == NULL)
	{
	  assert (false);
	  return;
	}
      for (regu_variable_list_node *operand = value.funcp->operand; operand != NULL; operand = operand->next)
	{
	  map_regu_and_check_stop (&operand->value);
	}
      break;

    case TYPE_REGUVAL_LIST:
      if (value.reguval_list == NULL)
	{
	  assert (false);
	  return;
	}
      for (regu_value_item *item = value.reguval_list->regu_list; item != NULL; item = item->next)
	{
	  map_regu_not_null_and_check_stop (item->value);
	}
      break;

    case TYPE_REGU_VAR_LIST:
      for (regu_variable_list_node *node = value.regu_var_list; node != NULL; node = node->next)
	{
	  map_regu_and_check_stop (&node->value);
	}
      break;

    default:
      // nothing
      return;
    }

#undef map_regu_not_null_and_check_stop
#undef map_regu_and_check_stop
}

void
regu_variable_node::map_regu_and_xasl (const map_regu_func_type &regu_func, const map_xasl_func_type &xasl_func)
{
  bool stop;
  // wrap regu_func and xasl_func calls into a single map_regu_func_type that can be used by map_regu
  auto cnv_funcs_to_regu_func = [&] (regu_variable_node &regu, bool &stop)
  {
    if (regu.xasl != NULL)
      {
	xasl_func (*regu.xasl, stop);
	if (stop)
	  {
	    return;
	  }
      }
    regu_func (regu, stop);
  };
  return map_regu (cnv_funcs_to_regu_func, stop);
}

void
regu_variable_node::clear_xasl_local ()
{
  switch (type)
    {
    case TYPE_INARITH:
    case TYPE_OUTARITH:
      assert (value.arithptr != NULL);
      if (value.arithptr->rand_seed != NULL)
	{
	  free_and_init (value.arithptr->rand_seed);
	}
      if (value.arithptr->pred != NULL)
	{
	  value.arithptr->pred->clear_xasl ();
	}
      break;

    case TYPE_FUNC:
      assert (value.funcp != NULL);
      pr_clear_value (value.funcp->value);

      if (value.funcp->tmp_obj != NULL)
	{
	  switch (value.funcp->ftype)
	    {
	    case F_REGEXP_COUNT:
	    case F_REGEXP_INSTR:
	    case F_REGEXP_LIKE:
	    case F_REGEXP_REPLACE:
	    case F_REGEXP_SUBSTR:
	    {
	      delete value.funcp->tmp_obj->compiled_regex;
	      value.funcp->tmp_obj->compiled_regex = NULL;
	    }
	    break;
	    default:
	      //any of union member may have been erased
	      assert (false);
	      break;
	    }

	  delete value.funcp->tmp_obj;
	  value.funcp->tmp_obj = NULL;
	}

      break;

    case TYPE_DBVAL:
      pr_clear_value (&value.dbval);
      break;

    default:
      break;
    }

  pr_clear_value (vfetch_to);
}

void
regu_variable_node::clear_xasl ()
{
  // todo - call map_regu_and_xasl; it requires similar XASL clear_xasl functionality
  auto map_func = [] (regu_variable_node & regu, bool & stop)
  {
    regu.clear_xasl_local ();
  };
  map_regu (map_func);
}
