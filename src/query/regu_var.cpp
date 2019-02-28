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
  return map_regu_and_xasl (regu_func, xasl_func, stop);
}

void
regu_variable_node::map_regu_and_xasl (const map_regu_func_type &regu_func, const map_xasl_func_type &xasl_func,
				       bool &stop)
{
  // helper macros to avoid repeating code
#define map_regu_and_check_stop(regu) \
  { (regu)->map_regu_and_xasl (regu_func, xasl_func, stop); if (stop) return; }
#define map_regu_not_null_and_check_stop(regu) \
  if ((regu) != NULL) map_regu_and_check_stop (regu)

  if (xasl != NULL)
    {
      // apply XASL function
      xasl_func (*xasl, stop);
      if (stop)
	{
	  return;
	}
    }

  // apply regu function to me
  regu_func (*this, stop);
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
      // no siblings
      return;
    }

#undef map_regu_not_null_and_check_stop
#undef map_regu_and_check_stop
}
