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
regu_variable_node::map_tree (const map_func &func)
{
  bool stop = false;
  map_tree (func, stop);
}

void
regu_variable_node::map_tree (const map_func &func, bool &stop)
{
  // helper macros to avoid repeating code
#define map_tree_and_check_stop(regu) \
  { (regu)->map_tree (func, stop); if (stop) return; }
#define map_tree_not_null_and_check_stop(regu) \
  if ((regu) != NULL) map_tree_and_check_stop (regu)

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
      map_tree_not_null_and_check_stop (value.arithptr->leftptr);
      map_tree_not_null_and_check_stop (value.arithptr->rightptr);
      break;

    case TYPE_FUNC:
      if (value.funcp == NULL)
	{
	  assert (false);
	  return;
	}
      for (regu_variable_list_node *operand = value.funcp->operand; operand != NULL; operand = operand->next)
	{
	  map_tree_and_check_stop (&operand->value);
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
	  map_tree_not_null_and_check_stop (item->value);
	}
      break;

    case TYPE_REGU_VAR_LIST:
      for (regu_variable_list_node *node = value.regu_var_list; node != NULL; node = node->next)
	{
	  map_tree_and_check_stop (&node->value);
	}
      break;

    default:
      // nothing
      return;
    }

#undef map_tree_not_null_and_check_stop
#undef map_tree_and_check_stop
}
