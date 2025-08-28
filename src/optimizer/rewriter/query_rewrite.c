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
 * query_rewrite.c - Query rewrite optimization
 */

#ident "$Id$"

#include <assert.h>

#include "query_rewrite.h"


/*
 * qo_rewrite_queries () - checks all subqueries for rewrite optimizations
 *   return: PT_NODE *
 *   parser(in): parser environment
 *   node(in): possible query
 *   arg(in):
 *   continue_walk(in):
 *   
 *   Steps:
 *   1. Pre-rewrite: Transform subqueries and hidden columns for better structure.
 *   2. Rewrite: Apply rules to simplify conditions and improve performance.
 *   3. Auto-parameterize: Replace constants with parameter markers for query caching.
 */
static PT_NODE *
qo_rewrite_queries (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
{
  int level, seqno = 0;
  PT_NODE *next, **wherep, **havingp, *dummy;
  PT_NODE *spec, *derived_table;
  PT_NODE **startwithp, **connectbyp, **aftercbfilterp;
  PT_NODE **merge_upd_wherep, **merge_ins_wherep, **merge_del_wherep;
  PT_NODE **orderby_for_p;
  PT_NODE **show_argp;
  PT_NODE *limit, *derived;
  bool call_auto_parameterize = false;

  /* Initialize pointers to prevent segmentation faults. */
  dummy = NULL;
  wherep = havingp = startwithp = connectbyp = aftercbfilterp =
    merge_upd_wherep = merge_ins_wherep = merge_del_wherep = orderby_for_p = show_argp = &dummy;

  /* 1. Pre-rewrite steps. */
  switch (node->node_type)
    {
    case PT_SELECT:
      /* HQ sub-query might be optimized twice in UPDATE statement because UPDATE statement internally creates SELECT
       * statement to get targets to update. We should check whether it was already single-table-optimized. Here is an
       * example: 
       *  CREATE TABLE t(p INT, c INT, x INT);
       *  INSERT INTO t VALUES(1, 11, 0), (1, 12, 0), (2, 21, 0);
       *  UPDATE t SET x=0 WHERE c IN (SELECT c FROM t START WITH p=1 CONNECT BY PRIOR c=p);
       */
      if (node->info.query.q.select.connect_by != NULL
	  && !node->info.query.q.select.after_cb_filter && !node->info.query.q.select.single_table_opt)
	{
	  PT_NODE *join_part = NULL;
	  PT_NODE *after_connectby_filter_part = NULL;

	  /* We need to separate the join predicates before we perform rewriting and optimizations so that they don't
	   * get mixed up with the filtering predicates (to be applied after connect by). */
	  pt_split_join_preds (parser, node->info.query.q.select.where, &join_part, &after_connectby_filter_part);

	  node->info.query.q.select.where = join_part;
	  assert (node->info.query.q.select.after_cb_filter == NULL);
	  node->info.query.q.select.after_cb_filter = after_connectby_filter_part;

	  /* if we have no joins prepare for using heap scans/index scans for start with list and connect by processing */
	  if (qo_check_generate_single_tbl_connect_by (parser, node))
	    {
	      node->info.query.q.select.where = node->info.query.q.select.start_with;
	      node->info.query.q.select.start_with = NULL;
	      node->info.query.q.select.single_table_opt = 1;
	    }
	}

      /* Put all join conditions together with WHERE clause for rewrite optimization. But we can distinguish a join
       * condition from each other and from WHERE clause by location information that were marked at 'pt_bind_names()'.
       * We'll recover the parse tree of join conditions using the location information in shortly. */
      qo_move_on_of_explicit_join_to_where (parser, &node->info.query.q.select.from, &node->info.query.q.select.where);

      wherep = &node->info.query.q.select.where;
      havingp = &node->info.query.q.select.having;
      if (node->info.query.q.select.start_with)
	{
	  startwithp = &node->info.query.q.select.start_with;
	}
      if (node->info.query.q.select.connect_by)
	{
	  connectbyp = &node->info.query.q.select.connect_by;
	}
      if (node->info.query.q.select.after_cb_filter)
	{
	  aftercbfilterp = &node->info.query.q.select.after_cb_filter;
	}
      spec = node->info.query.q.select.from;
      if (spec != NULL && spec->info.spec.derived_table_type == PT_IS_SHOWSTMT
	  && (derived_table = spec->info.spec.derived_table) != NULL && derived_table->node_type == PT_SHOWSTMT)
	{
	  show_argp = &derived_table->info.showstmt.show_args;
	}
      orderby_for_p = &node->info.query.orderby_for;

      qo_rewrite_index_hints (parser, node);


      break;

    case PT_UPDATE:
      qo_move_on_of_explicit_join_to_where (parser, &node->info.update.spec, &node->info.update.search_cond);

      wherep = &node->info.update.search_cond;
      orderby_for_p = &node->info.update.orderby_for;
      qo_rewrite_index_hints (parser, node);
      break;

    case PT_DELETE:
      qo_move_on_of_explicit_join_to_where (parser, &node->info.delete_.spec, &node->info.delete_.search_cond);

      wherep = &node->info.delete_.search_cond;
      qo_rewrite_index_hints (parser, node);
      break;

    case PT_INSERT:
      {
	PT_NODE *const subquery_ptr = pt_get_subquery_of_insert_select (node);

	if (subquery_ptr == NULL || subquery_ptr->node_type != PT_SELECT)
	  {
	    return node;
	  }
	wherep = &subquery_ptr->info.query.q.select.where;
      }
      break;

    case PT_MERGE:
      wherep = &node->info.merge.search_cond;
      merge_upd_wherep = &node->info.merge.update.search_cond;
      merge_ins_wherep = &node->info.merge.insert.search_cond;
      merge_del_wherep = &node->info.merge.update.del_search_cond;
      break;

    case PT_UNION:
    case PT_DIFFERENCE:
    case PT_INTERSECTION:
      node->info.query.q.union_.arg1 = qo_rewrite_hidden_col_as_derived (parser, node->info.query.q.union_.arg1, NULL);
      node->info.query.q.union_.arg2 = qo_rewrite_hidden_col_as_derived (parser, node->info.query.q.union_.arg2, NULL);

      /* If LIMIT clause is specified without ORDER BY clause, we will rewrite the UNION query as derived. For example,
       * (SELECT ...) UNION (SELECT ...) LIMIT 10 will be rewritten to: SELECT * FROM ((SELECT ...) UNION (SELECT ...))
       * T WHERE INST_NUM() <= 10 */
      /* If LIMIT clause is specified without ORDER BY clause, we will rewrite the UNION query as derived. For example,
       * (SELECT ...) UNION (SELECT ...) LIMIT 10 will be rewritten to: SELECT * FROM ((SELECT ...) UNION (SELECT ...))
       * T WHERE INST_NUM() <= 10 */
      if (node->info.query.limit && node->info.query.flag.rewrite_limit)
	{
	  limit = pt_limit_to_numbering_expr (parser, node->info.query.limit, PT_INST_NUM, false);
	  if (limit != NULL)
	    {
	      PT_NODE *limit_node;
	      bool single_tuple_bak;

	      node->info.query.flag.rewrite_limit = 0;

	      /* to move limit clause to derived */
	      limit_node = node->info.query.limit;
	      node->info.query.limit = NULL;

	      /* to move single tuple to derived */
	      single_tuple_bak = node->info.query.flag.single_tuple;
	      node->info.query.flag.single_tuple = false;

	      /* push limit to union */
	      if (node->info.query.order_by == NULL && !qo_check_distinct_union (parser, node)
		  && !qo_check_hint_union (parser, node, PT_HINT_NO_PUSH_PRED))
		{
		  node = qo_push_limit_to_union (parser, node, limit_node);
		}
	      derived = mq_rewrite_query_as_derived (parser, node);
	      if (derived != NULL)
		{
		  PT_NODE_MOVE_NUMBER_OUTERLINK (derived, node);

		  assert (derived->info.query.q.select.where == NULL);
		  derived->info.query.q.select.where = limit;

		  wherep = &derived->info.query.q.select.where;

		  node = derived;
		}
	      node->info.query.flag.single_tuple = single_tuple_bak;
	      node->info.query.limit = limit_node;
	    }
	}

      orderby_for_p = &node->info.query.orderby_for;
      break;

    case PT_EXPR:
      switch (node->info.expr.op)
	{
	case PT_EQ:
	case PT_NE:
	case PT_NULLSAFE_EQ:
	  node->info.expr.arg1 = qo_rewrite_hidden_col_as_derived (parser, node->info.expr.arg1, node);
	  [[fallthrough]];

	  /* keep out hidden column subquery from UPDATE assignment */
	case PT_ASSIGN:
	  /* quantified comparisons */
	case PT_GE_SOME:
	case PT_GT_SOME:
	case PT_LT_SOME:
	case PT_LE_SOME:
	case PT_GE_ALL:
	case PT_GT_ALL:
	case PT_LT_ALL:
	case PT_LE_ALL:
	  /* quantified equality comparisons */
	case PT_EQ_SOME:
	case PT_NE_SOME:
	case PT_EQ_ALL:
	case PT_NE_ALL:
	case PT_IS_IN:
	case PT_IS_NOT_IN:
	  node->info.expr.arg2 = qo_rewrite_hidden_col_as_derived (parser, node->info.expr.arg2, node);
	  break;
	case PT_EXISTS:
	  if (pt_is_query (node->info.expr.arg1))
	    {
	      qo_add_limit_clause (parser, node->info.expr.arg1);
	    }
	  break;
	default:
	  break;
	}
      /* no WHERE clause */
      return node;

    case PT_FUNCTION:
      if (node->info.function.function_type == F_TABLE_SET || node->info.function.function_type == F_TABLE_MULTISET
	  || node->info.function.function_type == F_TABLE_SEQUENCE)
	{
	  node->info.function.arg_list = qo_rewrite_hidden_col_as_derived (parser, node->info.function.arg_list, node);
	}
      /* no WHERE clause */
      return node;

    default:
      /* no WHERE clause */
      return node;
    }

  if (node->node_type == PT_SELECT)
    {
      /* analyze paths for possible optimizations */
      node->info.query.q.select.from =
	parser_walk_tree (parser, node->info.query.q.select.from, qo_analyze_path_join_pre, NULL, qo_analyze_path_join,
			  node->info.query.q.select.where);
      qo_rewrite_nonnull_count_select_list (parser, node);
    }

  qo_get_optimization_param (&level, QO_PARAM_LEVEL);
  /* 2. Optimization */
  if (OPTIMIZATION_ENABLED (level))
    {

      if (node->node_type == PT_SELECT)
	{
	  int continue_walk;
	  int idx = 0;

	  /* rewrite uncorrelated subquery to join query (TODO : correlated) */
	  qo_rewrite_subqueries (parser, node, &idx, &continue_walk);

	}

      /* rewrite optimization on WHERE, HAVING clause */

      if (!*wherep && !*havingp && !*aftercbfilterp && !*startwithp && !*connectbyp && !*merge_upd_wherep
	  && !*merge_ins_wherep && !*merge_del_wherep && !*orderby_for_p && !*show_argp)
	{
	  if (node->node_type != PT_SELECT)
	    {
	      return node;
	    }
	  else
	    {
	      /* check for group by, order by */
	      if (node->info.query.q.select.group_by == NULL && node->info.query.order_by == NULL)
		{
		  return node;
		}		/* else - go ahead */
	    }
	}

      /* convert to CNF and tag taggable terms */
      if (*wherep)
	{
	  *wherep = pt_cnf (parser, *wherep);
	}
      if (*havingp)
	{
	  *havingp = pt_cnf (parser, *havingp);
	}
      if (*startwithp)
	{
	  *startwithp = pt_cnf (parser, *startwithp);
	}
      if (*connectbyp)
	{
	  *connectbyp = pt_cnf (parser, *connectbyp);
	}
      if (*aftercbfilterp)
	{
	  *aftercbfilterp = pt_cnf (parser, *aftercbfilterp);
	}
      if (*merge_upd_wherep)
	{
	  *merge_upd_wherep = pt_cnf (parser, *merge_upd_wherep);
	}
      if (*merge_ins_wherep)
	{
	  *merge_ins_wherep = pt_cnf (parser, *merge_ins_wherep);
	}
      if (*merge_del_wherep)
	{
	  *merge_del_wherep = pt_cnf (parser, *merge_del_wherep);
	}
      if (*orderby_for_p)
	{
	  *orderby_for_p = pt_cnf (parser, *orderby_for_p);
	}

      /* in HAVING clause with GROUP BY, move non-aggregate terms to WHERE clause */
      if (PT_IS_SELECT (node) && node->info.query.q.select.group_by && *havingp)
	{
	  PT_NODE *prev, *cnf, *next;
	  PT_NON_GROUPBY_COL_INFO col_info;
	  PT_AGG_FIND_INFO info;
	  int has_pseudocolumn;
	  bool can_move;

	  col_info.groupby = node->info.query.q.select.group_by;

	  prev = NULL;		/* init */
	  for (cnf = *havingp; cnf; cnf = next)
	    {
	      next = cnf->next;	/* save and cut-off link */
	      cnf->next = NULL;

	      col_info.has_non_groupby_col = false;	/* on the supposition */
	      (void) parser_walk_tree (parser, cnf, pt_has_non_groupby_column_node, &col_info, NULL, NULL);
	      can_move = (col_info.has_non_groupby_col == false);

	      if (can_move)
		{
		  /* init agg info */
		  info.stop_on_subquery = false;
		  info.out_of_context_count = 0;
		  info.base_count = 0;
		  info.select_stack = pt_pointer_stack_push (parser, NULL, node);

		  /* search for aggregate of this select */
		  (void) parser_walk_tree (parser, cnf, pt_find_aggregate_functions_pre, &info,
					   pt_find_aggregate_functions_post, &info);
		  can_move = (info.base_count == 0);

		  /* cleanup */
		  info.select_stack = pt_pointer_stack_pop (parser, info.select_stack, NULL);
		}

	      /* Note: Do not move the cnf node if it contains a pseudo-column! */
	      if (can_move)
		{
		  has_pseudocolumn = 0;
		  (void) parser_walk_tree (parser, cnf, pt_is_pseudocolumn_node, &has_pseudocolumn, NULL, NULL);
		  if (has_pseudocolumn)
		    {
		      can_move = false;
		    }
		}

	      /* Not found aggregate function in cnf node and no ROLLUP clause. So, move it from HAVING clause to WHERE
	       * clause. */
	      if (can_move && !node->info.query.q.select.group_by->flag.with_rollup)
		{
		  /* delete cnf node from HAVING clause */
		  if (!prev)
		    {		/* very the first node */
		      *havingp = next;
		    }
		  else
		    {
		      prev->next = next;
		    }

		  /* add cnf node to WHERE clause */
		  *wherep = parser_append_node (*wherep, cnf);
		}
	      else
		{		/* do nothing and go ahead */
		  cnf->next = next;	/* restore link */
		  prev = cnf;	/* save previous */
		}
	    }
	}

      /* reduce equality terms */
      if (*wherep)
	{
	  if (PT_IS_SELECT (node))
	    {
	      /*
	       * It is correct that qo_reduce_equality_terms() is called in the post order.
	       * for correlated constant value in another subquery
	       * e.g. select .. from (select col1 .. where col1 =1) a,
	       *                     (select col1 from table) b where a.col1 = b.col1
	       *      ==>
	       *      select .. from (select 1 .. where col1 =1) a, <== 1st replace
	       *                     (select col1 from table) b where 1 = b.col1 <== 2nd replace
	       * Applies only to SELECT. In other cases, apply later if necessary.
	       */
	      parser_walk_tree (parser, node, NULL, NULL, qo_reduce_equality_terms_post, NULL);
	    }
	  else
	    {
	      QO_CHECK_AND_REDUCE_EQUALITY_TERMS (parser, node, wherep);
	    }
	}
      if (*havingp)
	{
	  QO_CHECK_AND_REDUCE_EQUALITY_TERMS (parser, node, havingp);
	}

      /* we don't reduce equality terms for startwith and connectby. This optimization for every A after a statement
       * like A = 5, replaced the column with the scalar 5. If the column is in an ORDER BY clause, the sorting may not
       * occur on column A because it's always 5. This behavior is incorrect when running a hierarchical query because
       * there may be a A = 5 in the START WITH part or CONNECT BY part but the ORDER BY on A should sort all elements
       * from all levels, column A being different. */
      if (*aftercbfilterp)
	{
	  QO_CHECK_AND_REDUCE_EQUALITY_TERMS (parser, node, aftercbfilterp);
	}
      if (*merge_upd_wherep)
	{
	  QO_CHECK_AND_REDUCE_EQUALITY_TERMS (parser, node, merge_upd_wherep);
	}
      if (*merge_ins_wherep)
	{
	  QO_CHECK_AND_REDUCE_EQUALITY_TERMS (parser, node, merge_ins_wherep);
	}
      if (*merge_del_wherep)
	{
	  QO_CHECK_AND_REDUCE_EQUALITY_TERMS (parser, node, merge_del_wherep);
	}

      qo_rewrite_terms (parser, wherep);
      qo_rewrite_terms (parser, havingp);
      qo_rewrite_terms (parser, startwithp);
      qo_rewrite_terms (parser, connectbyp);
      qo_rewrite_terms (parser, aftercbfilterp);
      qo_rewrite_terms (parser, merge_upd_wherep);
      qo_rewrite_terms (parser, merge_ins_wherep);
      qo_rewrite_terms (parser, merge_del_wherep);

      /* rewrite select queries */
      if (node->node_type == PT_SELECT)
	{
	  if (!qo_rewrite_select_queries (parser, &node, wherep, &seqno))
	    {
	      return node;	/* if failed give up */
	    }
	}

      /* auto-parameterization is safe when it is done as the last step of rewrite optimization */
      if (!prm_get_bool_value (PRM_ID_HOSTVAR_LATE_BINDING)
	  && prm_get_integer_value (PRM_ID_XASL_CACHE_MAX_ENTRIES) > 0 && node->flag.cannot_prepare == 0
	  && parser->flag.is_parsing_static_sql == 0 && parser->flag.is_skip_auto_parameterize == 0)
	{
	  call_auto_parameterize = true;
	}
    }

  /* 3. Auto-parameterize */
  /* auto-parameterize convert value in expression to host variable (input marker) */
  if (call_auto_parameterize)
    {
      PROCESS_IF_EXISTS (parser, wherep, qo_auto_parameterize);
      PROCESS_IF_EXISTS (parser, havingp, qo_auto_parameterize);
      PROCESS_IF_EXISTS (parser, startwithp, qo_auto_parameterize);
      PROCESS_IF_EXISTS (parser, connectbyp, qo_auto_parameterize);
      PROCESS_IF_EXISTS (parser, aftercbfilterp, qo_auto_parameterize);
      PROCESS_IF_EXISTS (parser, merge_upd_wherep, qo_auto_parameterize);
      PROCESS_IF_EXISTS (parser, merge_ins_wherep, qo_auto_parameterize);
      PROCESS_IF_EXISTS (parser, merge_del_wherep, qo_auto_parameterize);
      PROCESS_IF_EXISTS (parser, orderby_for_p, qo_auto_parameterize);

    }
  else
    {
      if (*wherep && (*wherep)->flag.force_auto_parameterize)
	qo_auto_parameterize (parser, *wherep);
      if (*merge_upd_wherep && (*merge_upd_wherep)->flag.force_auto_parameterize)
	qo_auto_parameterize (parser, *merge_upd_wherep);
    }

  if (node->node_type == PT_UPDATE && call_auto_parameterize)
    {
      qo_auto_parameterize (parser, node->info.update.assignment);
    }

  if (pt_is_const_not_hostvar (*show_argp))
    {
      PT_NODE *p = *show_argp;
      PT_NODE *result_list = NULL;
      PT_NODE *one_rewrited;
      PT_NODE *save;

      while (p)
	{
	  save = p->next;
	  p->next = NULL;
	  one_rewrited = pt_rewrite_to_auto_param (parser, p);
	  p = save;

	  result_list = parser_append_node (one_rewrited, result_list);
	}
      *show_argp = result_list;
    }

  /* auto parameterize for limit clause */
  if (PT_IS_QUERY_NODE_TYPE (node->node_type) || node->node_type == PT_UPDATE || node->node_type == PT_DELETE)
    {
      qo_auto_parameterize_limit_clause (parser, node);

      /* auto parameterize for keylimit clause */
      if (node->node_type == PT_SELECT || node->node_type == PT_UPDATE || node->node_type == PT_DELETE)
	{
	  qo_auto_parameterize_keylimit_clause (parser, node);
	}
    }

  if (node->node_type == PT_SELECT)
    {
      if (node->info.query.is_subquery == PT_IS_SUBQUERY)
	{
	  if (node->info.query.flag.single_tuple == 1)
	    {
	      node = qo_rewrite_hidden_col_as_derived (parser, node, NULL);
	    }
	}
    }

  return node;
}

/*
 * qo_rewrite_queries_post () -
 *   return:
 *   parser(in):
 *   tree(in):
 *   arg(in):
 *   continue_walk(in):
 * NOTE: see qo_move_on_of_explicit_join_to_where
 */
static PT_NODE *
qo_rewrite_queries_post (PARSER_CONTEXT * parser, PT_NODE * tree, void *arg, int *continue_walk)
{
  PT_NODE *node, *prev, *next, *spec;
  PT_NODE **fromp, **wherep;
  short location;

  switch (tree->node_type)
    {
    case PT_SELECT:
      fromp = &tree->info.query.q.select.from;
      wherep = &tree->info.query.q.select.where;
      break;
    case PT_UPDATE:
      fromp = &tree->info.update.spec;
      wherep = &tree->info.update.search_cond;
      break;
    case PT_DELETE:
      fromp = &tree->info.delete_.spec;
      wherep = &tree->info.delete_.search_cond;
      break;
    default:
      fromp = NULL;
      wherep = NULL;
      break;
    }

  if (wherep != NULL)
    {
      assert (fromp != NULL);

      prev = NULL;
      for (node = *wherep; node != NULL; node = next)
	{
	  next = node->next;
	  node->next = NULL;

	  if (node->node_type == PT_EXPR)
	    {
	      location = node->info.expr.location;
	    }
	  else if (node->node_type == PT_VALUE)
	    {
	      location = node->info.value.location;
	    }
	  else
	    {
	      location = -1;
	    }

	  if (location > 0)
	    {
	      for (spec = *fromp; spec && spec->info.spec.location != location; spec = spec->next)
		;		/* nop */

	      if (spec != NULL)
		{
		  if (spec->info.spec.join_type == PT_JOIN_LEFT_OUTER
		      || spec->info.spec.join_type == PT_JOIN_RIGHT_OUTER || spec->info.spec.join_type == PT_JOIN_INNER)
		    {
		      node->next = spec->info.spec.on_cond;
		      spec->info.spec.on_cond = node;

		      if (prev != NULL)
			{
			  prev->next = next;
			}
		      else
			{
			  *wherep = next;
			}
		    }
		  else
		    {		/* already converted to inner join */
		      /* clear on cond location */
		      if (node->node_type == PT_EXPR)
			{
			  node->info.expr.location = 0;
			}
		      else if (node->node_type == PT_VALUE)
			{
			  node->info.value.location = 0;
			}

		      /* Here - at the last stage of query optimize, remove copy-pushed term */
		      if (node->node_type == PT_EXPR && PT_EXPR_INFO_IS_FLAGED (node, PT_EXPR_INFO_COPYPUSH))
			{
			  parser_free_tree (parser, node);

			  if (prev != NULL)
			    {
			      prev->next = next;
			    }
			  else
			    {
			      *wherep = next;
			    }
			}
		      else
			{
			  prev = node;
			  node->next = next;
			}
		    }
		}
	      else
		{
		  /* might be impossible might be outer join error */
		  PT_ERRORf (parser, node, "check outer join syntax at '%s'", pt_short_print (parser, node));

		  prev = node;
		  node->next = next;
		}
	    }
	  else
	    {
	      /* Here - at the last stage of query optimize, remove copy-pushed term */
	      if (node->node_type == PT_EXPR && PT_EXPR_INFO_IS_FLAGED (node, PT_EXPR_INFO_COPYPUSH))
		{
		  parser_free_tree (parser, node);

		  if (prev != NULL)
		    {
		      prev->next = next;
		    }
		  else
		    {
		      *wherep = next;
		    }
		}
	      else
		{
		  prev = node;
		  node->next = next;
		}
	    }
	}
    }

  return tree;
}

/*
 * mq_optimize () - optimize statements by a variety of rewrites
 *   return: void
 *   parser(in): parser environment
 *   statement(in): select tree to optimize
 *
 * Note: rewrite only if optimization is enabled
 */
PT_NODE *
mq_rewrite (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  return parser_walk_tree (parser, statement, qo_rewrite_queries, NULL, qo_rewrite_queries_post, NULL);
}
