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
 * query_rewrite_auto_parameterize.c
 */

#ident "$Id$"

#include <assert.h>

#include "query_rewrite.h"

/*
 * qo_auto_parameterize () - Convert value to host variable (input marker)
 *   return:
 *   parser(in):
 *   where(in): pointer to WHERE list
 *
 * Note:
 * 	examples:
 *      WHERE a=10 AND b<20   -->  WHERE a=? AND b<? w/ input host var 10, 20
 *
 */
void
qo_auto_parameterize (PARSER_CONTEXT * parser, PT_NODE * where)
{
  PT_NODE *cnf_node, *dnf_node, *between_and, *range;
  PT_NODE *node_prior;

  /* traverse CNF list */
  for (cnf_node = where; cnf_node; cnf_node = cnf_node->next)
    {

      /* traverse DNF list */
      for (dnf_node = cnf_node; dnf_node; dnf_node = dnf_node->or_next)
	{
	  if (dnf_node->node_type != PT_EXPR)
	    {
	      /* dnf_node is not an expression node */
	      continue;
	    }

	  if (PT_EXPR_INFO_IS_FLAGED (dnf_node, PT_EXPR_INFO_DO_NOT_AUTOPARAM))
	    {
	      /* copy_pull term from select list of derived table do NOT auto_parameterize */
	      /* because the query rewrite step is performed in the XASL generation of DELETE and UPDATE. */
	      /* to_do: remove rewriting aptr in the XASL generation of DEL,UPD (pt_to_delete_xasl) */
	      continue;
	    }

	  node_prior = pt_get_first_arg_ignore_prior (dnf_node);

	  if (!pt_is_attr (node_prior) && !pt_is_function_index_expression (node_prior) && !pt_is_instnum (node_prior)
	      && !pt_is_orderbynum (node_prior))
	    {
	      /* neither LHS is an attribute, inst_num, nor orderby_num */
	      continue;
	    }

	  switch (dnf_node->info.expr.op)
	    {
	    case PT_EQ:
	    case PT_GT:
	    case PT_GE:
	    case PT_LT:
	    case PT_LE:
	    case PT_LIKE:
	    case PT_ASSIGN:
	      if (pt_is_const_not_hostvar (dnf_node->info.expr.arg2) && !PT_IS_NULL_NODE (dnf_node->info.expr.arg2))
		{
		  dnf_node->info.expr.arg2 = pt_rewrite_to_auto_param (parser, dnf_node->info.expr.arg2);
		}
	      break;
	    case PT_BETWEEN:
	      between_and = dnf_node->info.expr.arg2;
	      assert (between_and->node_type == PT_EXPR);
	      if (pt_is_const_not_hostvar (between_and->info.expr.arg1)
		  && !PT_IS_NULL_NODE (between_and->info.expr.arg1))
		{
		  between_and->info.expr.arg1 = pt_rewrite_to_auto_param (parser, between_and->info.expr.arg1);
		}
	      if (pt_is_const_not_hostvar (between_and->info.expr.arg2)
		  && !PT_IS_NULL_NODE (between_and->info.expr.arg2))
		{
		  between_and->info.expr.arg2 = pt_rewrite_to_auto_param (parser, between_and->info.expr.arg2);
		}
	      break;
	    case PT_RANGE:
	      for (range = dnf_node->info.expr.arg2; range; range = range->or_next)
		{
		  if (pt_is_const_not_hostvar (range->info.expr.arg1) && !PT_IS_NULL_NODE (range->info.expr.arg1)
		      && !PT_IS_COLLECTION_TYPE (range->info.expr.arg1->type_enum))
		    {
		      range->info.expr.arg1 = pt_rewrite_to_auto_param (parser, range->info.expr.arg1);
		    }
		  if (pt_is_const_not_hostvar (range->info.expr.arg2) && !PT_IS_NULL_NODE (range->info.expr.arg2)
		      && !PT_IS_COLLECTION_TYPE (range->info.expr.arg2->type_enum))
		    {
		      range->info.expr.arg2 = pt_rewrite_to_auto_param (parser, range->info.expr.arg2);
		    }
		}
	      break;
	    default:
	      /* Is any other expression type possible to be auto-parameterized? */
	      break;
	    }
	}
    }

}

/*
 * qo_find_constant_host_var () - Find the host variable of the 'attr = ?' or 'attr range (? =)' term of name
 *   return: PT_HOST_VAR node, or NULL if not found
 *   parser(in):
 *   terms(in): CNF list
 *   name(in): select list column
 *
 * Note:
 *      '? = attr' is not matched. qo_converse_sarg_terms () in qo_rewrite_terms () turns it into
 *      'attr = ?' before auto-parameterization, so this relies on that order.
 */
static PT_NODE *
qo_find_constant_host_var (PARSER_CONTEXT * parser, PT_NODE * terms, PT_NODE * name)
{
  PT_NODE *term, *value;

  for (term = terms; term; term = term->next)
    {
      if (term->node_type != PT_EXPR || term->or_next != NULL || !pt_is_attr (term->info.expr.arg1)
	  || !pt_name_equal (parser, pt_get_end_path_node (term->info.expr.arg1), name))
	{
	  continue;
	}

      value = NULL;
      if (term->info.expr.op == PT_EQ)
	{
	  value = term->info.expr.arg2;
	}
      else if (term->info.expr.op == PT_RANGE && term->info.expr.arg2 != NULL
	       && term->info.expr.arg2->or_next == NULL && term->info.expr.arg2->info.expr.op == PT_BETWEEN_EQ_NA)
	{
	  value = term->info.expr.arg2->info.expr.arg1;
	}

      if (PT_IS_INPUT_HOSTVAR (value))
	{
	  return value;
	}
    }

  return NULL;
}

/*
 * qo_auto_parameterize_constant_select_list () - Replace the literal constant values of the select list
 *						  with the host variables of the auto-parameterized terms
 *   return:
 *   parser(in):
 *   select(in): SELECT node
 *
 * Note:
 *      qo_reduce_equality_terms () keeps a copy of the reduced constant in the constant_value of
 *      the select list column, and qo_auto_parameterize () later turns only the constant of the
 *      remaining 'attr = const' term into a host variable. When the query is rewritten again, as
 *      the SELECT built for UPDATE and DELETE in the XASL generation, the copy is pulled up into
 *      the outer query. If it stays a literal, the literal is baked into an XASL whose cache key
 *      has the host variable, and every execution that hits the cache uses the first literal.
 *      The term is in WHERE, or in HAVING when it is not moved to WHERE (WITH ROLLUP).
 *
 * 	examples:
 *      SELECT a ... WHERE a=10  -->  SELECT a ... WHERE a=?:0, constant_value of a: 10 --> ?:0
 */
void
qo_auto_parameterize_constant_select_list (PARSER_CONTEXT * parser, PT_NODE * select)
{
  PT_NODE *col, *name, *host_var;

  if (select == NULL || select->node_type != PT_SELECT)
    {
      return;
    }

  for (col = select->info.query.q.select.list; col; col = col->next)
    {
      name = pt_get_end_path_node (col);
      if (name == NULL || name->node_type != PT_NAME || !PT_NAME_INFO_IS_FLAGED (name, PT_NAME_INFO_CONSTANT)
	  || name->info.name.constant_value == NULL || PT_IS_HOSTVAR (name->info.name.constant_value))
	{
	  continue;
	}

      host_var = qo_find_constant_host_var (parser, select->info.query.q.select.where, name);
      if (host_var == NULL)
	{
	  host_var = qo_find_constant_host_var (parser, select->info.query.q.select.having, name);
	}

      if (host_var != NULL)
	{
	  parser_free_tree (parser, name->info.name.constant_value);
	  name->info.name.constant_value = parser_copy_tree (parser, host_var);
	}
    }
}

void
qo_auto_parameterize_limit_clause (PARSER_CONTEXT * parser, PT_NODE * node)
{
  PT_NODE *limit_offsetp, *limit_row_countp;
  PT_NODE *new_limit_offsetp, *new_limit_row_countp;

  if (node == NULL)
    {
      return;
    }

  if (parser->flag.is_parsing_static_sql == 1 || parser->flag.is_skip_auto_parameterize == 1)
    {
      return;
    }

  limit_offsetp = NULL;
  limit_row_countp = NULL;

  switch (node->node_type)
    {
    case PT_UNION:
    case PT_DIFFERENCE:
    case PT_INTERSECTION:
    case PT_SELECT:
      if (node->info.query.limit == NULL)
	{
	  return;
	}

      if (node->info.query.limit->next != NULL)
	{
	  limit_offsetp = node->info.query.limit;
	  limit_row_countp = node->info.query.limit->next;
	  limit_offsetp->next = NULL;	/* cut */
	}
      else
	{
	  limit_offsetp = NULL;
	  limit_row_countp = node->info.query.limit;
	}
      break;

    case PT_UPDATE:
      if (node->info.update.limit == NULL)
	{
	  return;
	}

      if (node->info.update.limit->next != NULL)
	{
	  limit_offsetp = node->info.update.limit;
	  limit_row_countp = node->info.update.limit->next;
	  limit_offsetp->next = NULL;	/* cut */
	}
      else
	{
	  limit_offsetp = NULL;
	  limit_row_countp = node->info.update.limit;
	}
      break;

    case PT_DELETE:
      if (node->info.delete_.limit == NULL)
	{
	  return;
	}

      if (node->info.delete_.limit->next != NULL)
	{
	  limit_offsetp = node->info.delete_.limit;
	  limit_row_countp = node->info.delete_.limit->next;
	  limit_offsetp->next = NULL;	/* cut */
	}
      else
	{
	  limit_offsetp = NULL;
	  limit_row_countp = node->info.delete_.limit;
	}
      break;

    default:
      return;
    }

  new_limit_offsetp = limit_offsetp;
  if (limit_offsetp != NULL && !PT_IS_NULL_NODE (limit_offsetp))
    {
      if (pt_is_const_not_hostvar (limit_offsetp))
	{
	  new_limit_offsetp = pt_rewrite_to_auto_param (parser, limit_offsetp);
	}
#if 0
      else if (PT_IS_EXPR_NODE (limit_offsetp))
	{
	  /* We may optimize to auto parameterize expressions in limit clause. However, I don't think it is practical.
	   * Full constant expressions, e.g, (0+2) is folded as constant and eventually parameterized as a hostvar.
	   * Expressions which include a const would be mixed use of a constant and a hostvar, e.g, (0+?).
	   * If you really want to optimize this case too, you can add a function to parameterize an expression node.
	   */
	}
#endif
    }

  new_limit_row_countp = limit_row_countp;
  if (limit_row_countp != NULL && !PT_IS_NULL_NODE (limit_row_countp))
    {
      if (pt_is_const_not_hostvar (limit_row_countp))
	{
	  new_limit_row_countp = pt_rewrite_to_auto_param (parser, limit_row_countp);
	}
#if 0
      else if (PT_IS_EXPR_NODE (limit_row_countp))
	{
	  /* We may optimize to auto parameterize expressions in limit clause. However, I don't think it is practical.
	   * Full constant expressions, e.g, (0+2) is folded as constant and eventually parameterized as a hostvar.
	   * Expressions which include a const would be mixed use of a constant and a hostvar, e.g, (0+?).
	   * If you really want to optimize this case too, you can add a function to parameterize an expression node.
	   */
	}
#endif
    }

  switch (node->node_type)
    {
    case PT_UPDATE:
      if (limit_offsetp != NULL)
	{
	  node->info.update.limit = new_limit_offsetp;
	  node->info.update.limit->next = new_limit_row_countp;
	}
      else
	{
	  node->info.update.limit = new_limit_row_countp;
	  node->info.update.limit->next = NULL;
	}
      break;
    case PT_DELETE:
      if (limit_offsetp != NULL)
	{
	  node->info.delete_.limit = new_limit_offsetp;
	  node->info.delete_.limit->next = new_limit_row_countp;
	}
      else
	{
	  node->info.delete_.limit = new_limit_row_countp;
	  node->info.delete_.limit->next = NULL;
	}
      break;
    default:
      if (limit_offsetp != NULL)
	{
	  node->info.query.limit = new_limit_offsetp;
	  node->info.query.limit->next = new_limit_row_countp;
	}
      else
	{
	  node->info.query.limit = new_limit_row_countp;
	  node->info.query.limit->next = NULL;
	}
      break;
    }
}

void
qo_auto_parameterize_keylimit_clause (PARSER_CONTEXT * parser, PT_NODE * node)
{
  PT_NODE *using_index = NULL;
  PT_NODE *key_limit_lower_boundp, *key_limit_upper_boundp;

  if (node == NULL)
    {
      return;
    }

  switch (node->node_type)
    {
    case PT_SELECT:
      using_index = node->info.query.q.select.using_index;
      break;

    case PT_UPDATE:
      using_index = node->info.update.using_index;
      break;

    case PT_DELETE:
      using_index = node->info.delete_.using_index;
      break;

    default:
      return;
    }

  while (using_index != NULL)
    {
      /* it may include keylimit clause */

      key_limit_lower_boundp = key_limit_upper_boundp = NULL;

      if (using_index->info.name.indx_key_limit != NULL)
	{
	  key_limit_upper_boundp = using_index->info.name.indx_key_limit;
	  key_limit_lower_boundp = using_index->info.name.indx_key_limit->next;

	  using_index->info.name.indx_key_limit->next = NULL;
	}

      if (key_limit_upper_boundp != NULL)
	{
	  if (pt_is_const_not_hostvar (key_limit_upper_boundp) && !PT_IS_NULL_NODE (key_limit_upper_boundp))
	    {
	      using_index->info.name.indx_key_limit = pt_rewrite_to_auto_param (parser, key_limit_upper_boundp);
	    }
	  else
	    {
	      using_index->info.name.indx_key_limit = key_limit_upper_boundp;
	    }
	}

      if (key_limit_lower_boundp != NULL)
	{
	  if (pt_is_const_not_hostvar (key_limit_lower_boundp) && !PT_IS_NULL_NODE (key_limit_lower_boundp))
	    {
	      using_index->info.name.indx_key_limit->next = pt_rewrite_to_auto_param (parser, key_limit_lower_boundp);
	    }
	  else
	    {
	      using_index->info.name.indx_key_limit->next = key_limit_lower_boundp;
	    }
	}

      using_index = using_index->next;
    }
}
