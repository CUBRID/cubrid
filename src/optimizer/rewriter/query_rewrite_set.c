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
 * query_rewrite_set.c
 */


#ident "$Id$"

#include <assert.h>

#include "query_rewrite.h"


/*
 * qo_push_limit_to_union () - push limit on union query
 *   return: PT_NODE *
 *   parser(in): parser environment
 *   node(in): possible query
 *   arg(in):
 *   continue_walk(in):
 */
PT_NODE *
qo_push_limit_to_union (PARSER_CONTEXT * parser, PT_NODE * node, PT_NODE * limit)
{
  PT_NODE *save_next, *add_limit;

  switch (node->node_type)
    {
    case PT_SELECT:
      if (!pt_has_inst_or_orderby_num_in_where (parser, node) && node->info.query.limit == NULL)
	{
	  /* case of limit 10,10 */
	  if (limit->next)
	    {
	      /* change 'limit 10,10' to 'limit 10 + 10' */
	      /* generate 'limit 10 + 10' */
	      if (!(add_limit = parser_new_node (parser, PT_EXPR)))
		{
		  PT_ERRORm (parser, add_limit, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OUT_OF_MEMORY);
		  return NULL;
		}
	      /* cut off limit->next */
	      save_next = limit->next;
	      limit->next = NULL;

	      add_limit->type_enum = limit->type_enum;
	      add_limit->info.expr.op = PT_PLUS;
	      add_limit->info.expr.arg1 = parser_copy_tree (parser, save_next);
	      add_limit->info.expr.arg2 = parser_copy_tree (parser, limit);
	      limit->next = save_next;

	      node->info.query.limit = add_limit;
	    }
	  else
	    {
	      node->info.query.limit = parser_copy_tree (parser, limit);
	    }

	  node->info.query.flag.rewrite_limit = 1;
	  return node;
	}
      break;

    case PT_UNION:
      qo_push_limit_to_union (parser, node->info.query.q.union_.arg1, limit);
      qo_push_limit_to_union (parser, node->info.query.q.union_.arg2, limit);
      break;

    default:
      break;
    }
  return node;
}

/*
 * qo_check_distinct_union () - check having distinct in union clause
 *   return: PT_NODE *
 *   parser(in): parser environment
 *   node(in): possible query
 *   arg(in):
 *   continue_walk(in):
 */
bool
qo_check_distinct_union (PARSER_CONTEXT * parser, PT_NODE * node)
{
  bool result = false;

  switch (node->node_type)
    {
    case PT_UNION:
      if (node->info.query.all_distinct == PT_DISTINCT)
	{
	  return true;
	}

      result |= qo_check_distinct_union (parser, node->info.query.q.union_.arg1);
      result |= qo_check_distinct_union (parser, node->info.query.q.union_.arg2);
      break;

    default:
      break;
    }
  return result;
}

/*
 * qo_check_hint_union () - check having distinct in union clause
 *   return: PT_NODE *
 *   parser(in): parser environment
 *   node(in): possible query
 *   arg(in):
 *   continue_walk(in):
 */
bool
qo_check_hint_union (PARSER_CONTEXT * parser, PT_NODE * node, PT_HINT_ENUM hint)
{
  bool result = false;

  switch (node->node_type)
    {
    case PT_SELECT:
      if (node->info.query.q.select.hint & hint)
	{
	  return true;
	}
      break;
    case PT_UNION:
      result |= qo_check_hint_union (parser, node->info.query.q.union_.arg1, hint);
      result |= qo_check_hint_union (parser, node->info.query.q.union_.arg2, hint);
      break;

    default:
      break;
    }
  return result;
}
