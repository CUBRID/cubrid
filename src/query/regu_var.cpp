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
 * Regular variable - implementation.
 */

#include "regu_var.hpp"

#include "object_primitive.h"
#include "xasl_predicate.hpp"

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
	    case F_REGEXP_REPLACE:
	    case F_REGEXP_SUBSTR:
	    case F_REGEXP_INSTR:
	    {
	      delete value.funcp->tmp_obj->compiled_regex;
	    }
	    break;
	    default:
	      //any of union member may have been erased
	      assert (0);
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
