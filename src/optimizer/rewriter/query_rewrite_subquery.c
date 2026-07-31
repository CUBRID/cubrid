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
 * query_rewrite_subquery.c - Subquery Rewrite Optimization
 */

#ident "$Id$"

#include <assert.h>
#include "query_rewrite.h"
#include "dbi.h"


/*
 * Subquery unnesting: [NOT] EXISTS / [NOT] IN -> SEMI / ANTI JOIN (CBRD-24043)
 *
 *   SELECT * FROM t1 WHERE EXISTS (SELECT 1 FROM t2 WHERE t2.a = t1.a)
 *     ->  SELECT * FROM t1 SEMI JOIN t2 ON t2.a = t1.a
 *   SELECT * FROM t1 WHERE t1.a IN (SELECT t2.x FROM t2 WHERE t2.b = t1.b)
 *     ->  SELECT * FROM t1 SEMI JOIN t2 ON t2.x = t1.a AND t2.b = t1.b
 *
 * The subquery's single base table moves into the enclosing FROM as a SEMI (positive form) or ANTI
 * (negated form) joined spec and its WHERE becomes that spec's ON; the IN forms synthesize the extra
 * 'lhs = select-list item' equality shown above.
 *
 * Running after name resolution means every PT_NAME is already bound, so moving a spec between FROM lists
 * re-resolves nothing -- but it also means the generated joins bypass pt_check_semi_anti_join (), whose
 * direct-join invariant is therefore reused explicitly below.
 */

/*
 * qo_is_unnestable_subquery () - eligibility gate shared by the [NOT] EXISTS and [NOT] IN paths
 *   return: true only if 'subq' is a plain SELECT over one base table, safe to flatten into a single
 *           semi/anti-joined spec
 *   parser(in):
 *   subq(in): the subquery operand
 *   require_where(in): true for the EXISTS forms, whose only join predicate is the subquery WHERE
 */
static bool
qo_is_unnestable_subquery (PARSER_CONTEXT * parser, PT_NODE * subq, bool require_where)
{
  PT_NODE *inner_spec;

  if (subq == NULL || subq->node_type != PT_SELECT)
    {
      return false;
    }

  /* the spec moves up exactly one level, so a reference further out would leave its scope behind */
  if (subq->info.query.correlation_level > 1)
    {
      return false;
    }

  if (require_where && subq->info.query.q.select.where == NULL)
    {
      return false;
    }

  /* reject anything whose flattening would change cardinality or semantics */
  if (subq->info.query.q.select.group_by != NULL
      || subq->info.query.q.select.having != NULL
      || subq->info.query.q.select.connect_by != NULL
      || subq->info.query.q.select.start_with != NULL
      || subq->info.query.q.select.with_increment != NULL
      || subq->info.query.order_by != NULL
      || subq->info.query.orderby_for != NULL
      || subq->info.query.limit != NULL || pt_has_aggregate (parser, subq) || pt_has_analytic (parser, subq))
    {
      return false;
    }

  /* INST_NUM / ROWNUM is legal in a nested subquery (it restarts per outer row), but once lifted into ON
   * it is re-checked in the outer scope and rejected -- unnesting would turn a working query into an
   * error. The walker stops at query-node scope boundaries, so probe the lifted clauses, not 'subq'
   * itself; a deeper subquery's own ROWNUM moves intact inside its scope and rightly stays invisible. */
  if (pt_has_inst_num (parser, subq->info.query.q.select.where)
      || pt_has_inst_num (parser, subq->info.query.q.select.list))
    {
      return false;
    }

  /* one simple base-table spec: no join, derived table or CTE */
  inner_spec = subq->info.query.q.select.from;
  if (inner_spec == NULL || inner_spec->next != NULL || inner_spec->node_type != PT_SPEC
      || inner_spec->info.spec.entity_name == NULL || inner_spec->info.spec.derived_table != NULL
      || inner_spec->info.spec.cte_name != NULL || inner_spec->info.spec.on_cond != NULL
      || inner_spec->info.spec.join_type != PT_JOIN_NONE)
    {
      return false;
    }

  return true;
}

/*
 * qo_operand_is_non_null () - true iff 'operand' is a base-table column the schema guarantees is never
 *      NULL (NOT NULL or PRIMARY KEY). Anything unprovable -- an expression, a view, a class hierarchy,
 *      an outer-scope reference -- is reported nullable. Licenses the NOT IN -> ANTI JOIN rewrite only.
 *   return: bool
 *   operand(in): one side of the synthesized join equality
 *   spec_list(in): the FROM list the operand's spec_id must resolve in
 */
static bool
qo_operand_is_non_null (PT_NODE * operand, PT_NODE * spec_list)
{
  PT_NODE *spec, *flat;
  DB_ATTRIBUTE *attr;

  if (operand == NULL || operand->node_type != PT_NAME || operand->info.name.spec_id == 0
      || operand->info.name.original == NULL)
    {
      return false;
    }

  for (spec = spec_list; spec != NULL; spec = spec->next)
    {
      if (spec->info.spec.id == operand->info.name.spec_id)
	{
	  break;
	}
    }
  if (spec == NULL || spec->info.spec.entity_name == NULL || spec->info.spec.derived_table != NULL
      || spec->info.spec.cte_name != NULL)
    {
      return false;
    }

  /* one resolved class only: a hierarchy would need every subclass checked, a view has no constraints */
  flat = spec->info.spec.flat_entity_list;
  if (flat == NULL || flat->next != NULL || flat->info.name.db_object == NULL
      || db_is_class (flat->info.name.db_object) <= 0)
    {
      return false;
    }

  attr = db_get_attribute (flat->info.name.db_object, operand->info.name.original);

  return (attr != NULL && (db_attribute_is_non_null (attr) || db_attribute_is_primary_key (attr)));
}

/*
 * qo_unnest_one_conjunct () - try to unnest one top-level WHERE conjunct into a SEMI / ANTI joined spec
 *   return: true iff 'cnf_node' was unnested, in which case it is unlinked from the WHERE and freed
 *   parser(in):
 *   node(in/out): the enclosing PT_SELECT, with a non-empty FROM
 *   cnf_node(in): the conjunct to examine
 *   prev(in), next(in): its CNF-list neighbours, used to unlink it on success
 *
 * Note: every rejection returns false without touching the tree, so a case we cannot handle simply keeps
 *   its nested subquery -- a missed optimization, never a wrong answer.
 */
static bool
qo_unnest_one_conjunct (PARSER_CONTEXT * parser, PT_NODE * node, PT_NODE * cnf_node, PT_NODE * prev, PT_NODE * next)
{
  PT_NODE *pred_expr, *subq, *inner_spec, *on_cond, *on_cnf, *spec;
  PT_NODE *new_on_pred = NULL;
  bool is_anti = false, is_in_form = false, has_direct_join = false;
  short loc;

  assert (node->info.query.q.select.from != NULL);

  /* a plain conjunct only: an OR-ed alternative can hold with the subquery false, which no join expresses */
  if (cnf_node->or_next != NULL || cnf_node->node_type != PT_EXPR)
    {
      return false;
    }

  /* an explicit NOT flips the sense of whatever it wraps */
  pred_expr = cnf_node;
  if (pred_expr->info.expr.op == PT_NOT)
    {
      pred_expr = pred_expr->info.expr.arg1;
      if (pred_expr == NULL || pred_expr->node_type != PT_EXPR)
	{
	  return false;
	}
      is_anti = true;
    }

  switch (pred_expr->info.expr.op)
    {
    case PT_EXISTS:		/* EXISTS -> SEMI, NOT EXISTS -> ANTI */
      break;
    case PT_IS_IN:		/* IN / = SOME -> SEMI */
    case PT_EQ_SOME:
      is_in_form = true;
      break;
    case PT_IS_NOT_IN:		/* NOT IN / <> ALL -> ANTI; a wrapping NOT folds it back to SEMI */
    case PT_NE_ALL:
      is_in_form = true;
      is_anti = !is_anti;
      break;
    default:
      return false;
    }

  /* EXISTS holds its subquery in arg1, the IN forms in arg2 */
  subq = is_in_form ? pred_expr->info.expr.arg2 : pred_expr->info.expr.arg1;
  if (!qo_is_unnestable_subquery (parser, subq, !is_in_form))
    {
      return false;
    }

  /* uncorrelated IN / = SOME belongs to qo_rewrite_subqueries (), which ran just above and turns it into a
   * DISTINCT derived-table join -- one plan shape per form. The ANTI forms have no such path. */
  if (is_in_form && !is_anti && subq->info.query.correlation_level != 1)
    {
      return false;
    }

  inner_spec = subq->info.query.q.select.from;
  on_cond = subq->info.query.q.select.where;

  if (is_in_form)
    {
      /* the IN forms carry no join predicate of their own, so synthesize 'lhs = select-list item'.
       * v1 is single column: '(a, b) IN (SELECT x, y ...)' would need one equality per pair. */
      PT_NODE *lhs = pred_expr->info.expr.arg1;
      PT_NODE *item = subq->info.query.q.select.list;

      if (lhs == NULL || lhs->next != NULL || PT_IS_COLLECTION_TYPE (lhs->type_enum)
	  || item == NULL || item->next != NULL || item->flag.is_hidden_column
	  || PT_IS_COLLECTION_TYPE (item->type_enum))
	{
	  return false;
	}

      /* NOT IN / <> ALL matches an ANTI JOIN only when neither side can be NULL: a NULL leaves the predicate
       * UNKNOWN and drops the row, while the ANTI JOIN reads that comparison as a non-match and emits it */
      if (is_anti
	  && !(qo_operand_is_non_null (lhs, node->info.query.q.select.from)
	       && qo_operand_is_non_null (item, inner_spec)))
	{
	  return false;
	}

      new_on_pred = parser_new_node (parser, PT_EXPR);
      if (new_on_pred == NULL)
	{
	  PT_INTERNAL_ERROR (parser, "allocate new node");
	  return false;
	}
      new_on_pred->info.expr.op = PT_EQ;
      new_on_pred->info.expr.arg1 = lhs;	/* still owned by pred_expr until the commit below */
      new_on_pred->info.expr.arg2 = item;	/* likewise by the subquery select list */
      new_on_pred->next = on_cond;
      on_cond = new_on_pred;
    }

  /* Require a direct inner<->single-outer join conjunct: the only shape the optimizer's semi/anti freeze
   * models. Reusing the parse-time invariant keeps us from building a join a hand-written one would be
   * rejected for. on_cond is a CNF list and the invariant takes one conjunct, so accept iff any qualifies. */
  for (on_cnf = on_cond; on_cnf != NULL && !has_direct_join; on_cnf = on_cnf->next)
    {
      has_direct_join = pt_semi_anti_has_direct_join_conjunct (parser, on_cnf, inner_spec->info.spec.id,
							       node->info.query.q.select.from);
    }
  if (!has_direct_join)
    {
      if (new_on_pred != NULL)
	{
	  /* discard the synthesized equality; its operands still belong to the untouched tree */
	  new_on_pred->next = NULL;
	  new_on_pred->info.expr.arg1 = NULL;
	  new_on_pred->info.expr.arg2 = NULL;
	  parser_free_tree (parser, new_on_pred);
	}
      return false;
    }

  /* ---- commit ---- */

  /* detach everything we keep, before the subquery shell is freed at the end */
  subq->info.query.q.select.from = NULL;
  subq->info.query.q.select.where = NULL;
  if (new_on_pred != NULL)
    {
      pred_expr->info.expr.arg1 = NULL;
      subq->info.query.q.select.list = NULL;
    }

  inner_spec->info.spec.join_type = (is_anti ? PT_JOIN_ANTI : PT_JOIN_SEMI);
  inner_spec->info.spec.on_cond = on_cond;
  inner_spec->info.spec.natural = false;

  /* Append at the FROM tail. Count the position rather than read the last spec's location: a derived spec
   * appended earlier in this pass by qo_rewrite_subqueries () still carries the unset -1, and
   * qo_analyze_term () indexes the node array by location, so it must equal the FROM position. */
  loc = 0;
  for (spec = node->info.query.q.select.from; spec->next != NULL; spec = spec->next)
    {
      loc++;
    }
  spec->next = inner_spec;
  inner_spec->info.spec.location = (short) (loc + 1);

  /* stamp the moved ON as pt_bind_names () would, with the same helpers: this spec's location on every term,
   * plus (ANTI only) PT_EXPR_INFO_ANTI_JOIN_ON so qo_reduce_equality_terms () keeps them as join predicates */
  (void) parser_walk_tree (parser, on_cond, pt_mark_location, &inner_spec->info.spec.location, NULL, NULL);
  if (is_anti)
    {
      (void) parser_walk_tree (parser, on_cond, pt_mark_anti_join_on, NULL, NULL, NULL);
    }

  /* unlink the conjunct and free its now empty shell */
  if (prev == NULL)
    {
      node->info.query.q.select.where = next;
    }
  else
    {
      prev->next = next;
    }
  cnf_node->next = NULL;
  parser_free_tree (parser, cnf_node);

  return true;
}

/*
 * qo_rewrite_exists_semi_anti () - unnest [NOT] EXISTS / [NOT] IN conjuncts in a SELECT's WHERE into
 *   SEMI / ANTI JOINs (see the block comment above)
 *   return: none
 *   parser(in):
 *   node(in/out): a PT_SELECT node
 */
void
qo_rewrite_exists_semi_anti (PARSER_CONTEXT * parser, PT_NODE * node)
{
  PT_NODE *prev, *cnf_node, *next;

  if (node == NULL || node->node_type != PT_SELECT)
    {
      return;
    }

  /* SEMI/ANTI JOIN cannot appear in a hierarchical query, and the unnest needs a FROM tail to append to */
  if (node->info.query.q.select.connect_by != NULL || node->info.query.q.select.from == NULL)
    {
      return;
    }

  prev = NULL;
  for (cnf_node = node->info.query.q.select.where; cnf_node != NULL; cnf_node = next)
    {
      next = cnf_node->next;
      if (!qo_unnest_one_conjunct (parser, node, cnf_node, prev, next))
	{
	  prev = cnf_node;	/* kept, so it is the predecessor of whatever comes next */
	}
    }
}


/*
 * qo_rewrite_subqueries () - Rewrite uncorrelated subquery to join query
 *   return: PT_NODE *
 *   parser(in):
 *   node(in): SELECT node
 *   arg(in):
 *   continue_walk(in):
 *
 * Note: do parser_walk_tree() pre function
 */
PT_NODE *
qo_rewrite_subqueries (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
{
  PT_NODE *cnf_node, *arg1, *arg2, *select_list, *arg2_list;
  PT_OP_TYPE op_type;
  PT_NODE *new_spec, *new_attr, *new_func;
  int *idx = (int *) arg;
  bool do_rewrite;
  PT_NODE *save_next, *arg1_next, *new_attr_next, *tmp, *arg2_next;
  PT_OP_TYPE saved_op_type;

  if (node->node_type != PT_SELECT)
    {
      return node;
    }

  /* traverse CNF list */
  for (cnf_node = node->info.query.q.select.where; cnf_node; cnf_node = cnf_node->next)
    {

      if (cnf_node->or_next != NULL)
	{
	  continue;
	}

      if (cnf_node->node_type != PT_EXPR)
	{
	  continue;
	}

      op_type = cnf_node->info.expr.op;
      arg1 = cnf_node->info.expr.arg1;
      arg2 = cnf_node->info.expr.arg2;

      if (arg1 && arg2
	  && (op_type == PT_EQ || op_type == PT_IS_IN || op_type == PT_EQ_SOME || op_type == PT_GT_SOME
	      || op_type == PT_GE_SOME || op_type == PT_LT_SOME || op_type == PT_LE_SOME))
	{
	  /* go ahead */
	}
      else
	{
	  continue;
	}

      select_list = pt_get_select_list (parser, arg2);
      if ((op_type == PT_EQ || op_type == PT_IS_IN || op_type == PT_EQ_SOME) && select_list
	  && PT_IS_COLLECTION_TYPE (arg1->type_enum) && PT_IS_FUNCTION (arg1)
	  && PT_IS_COLLECTION_TYPE (arg2->type_enum) && (PT_IS_FUNCTION (select_list) || PT_IS_CONST (select_list)))
	{
	  /* collection case : (col1,col2) [in or =] (select col1,col2 ...) */
	  arg1 = arg1->info.function.arg_list;
	  if (PT_IS_FUNCTION (select_list))
	    {
	      arg2_list = select_list->info.function.arg_list;
	    }
	  else
	    {
	      arg2_list = select_list->info.value.data_value.set;
	    }
	}
      else if (op_type == PT_EQ)
	{
	  /* one column subquery is not rewrited to join with derived table. ex) col1 = (select col1 ... ) */
	  continue;
	}
      else
	{
	  arg2_list = arg2;
	}

      do_rewrite = false;
      select_list = NULL;

      /* should be 'attr op uncorr-subquery', and select list of the subquery should be indexable-column */
      for (arg1_next = arg1, arg2_next = arg2_list; arg1_next && arg2_next;
	   arg1_next = arg1_next->next, arg2_next = arg2_next->next)
	{
	  if (tp_valid_indextype (pt_type_enum_to_db (arg1_next->type_enum))
	      && (pt_is_attr (arg1_next) || pt_is_function_index_expression (arg1_next)))
	    {
	      if (tp_valid_indextype (pt_type_enum_to_db (arg2_next->type_enum)) && !pt_has_analytic (parser, arg2))
		{
		  select_list = pt_get_select_list (parser, arg2);
		  if (select_list != NULL && arg2->info.query.correlation_level == 0)
		    {
		      assert (pt_length_of_select_list (select_list, EXCLUDE_HIDDEN_COLUMNS) == 1);

		      /* match 'indexable-attr op indexable-uncorr-subquery' */
		      do_rewrite = true;
		    }
		  else
		    {
		      do_rewrite = false;
		      break;
		    }
		}
	      else
		{
		  do_rewrite = false;
		  break;
		}
	    }
	  else
	    {
	      do_rewrite = false;
	      break;
	    }
	}

      if (do_rewrite)
	{
	  /* rewrite subquery to join with derived table */
	  switch (op_type)
	    {
	    case PT_EQ:	/* arg1 = set_func_elements */
	    case PT_IS_IN:	/* arg1 = set_func_elements, attr */
	    case PT_EQ_SOME:	/* arg1 = attr */
	      if (PT_IS_COLLECTION_TYPE (arg2->type_enum) && select_list
		  && (PT_IS_FUNCTION (select_list) || PT_IS_CONST (select_list)))
		{
		  /* if arg2 is collection type then select_list is rewrited to multi col */
		  pt_select_list_to_one_col (parser, arg2, false);
		}

	      /* make new derived spec and append it to FROM */
	      if (mq_make_derived_spec (parser, node, arg2, idx, &new_spec, &new_attr) == NULL)
		{
		  return NULL;
		}

	      /* convert to 'attr op attr' */
	      cnf_node->info.expr.arg1 = arg1;
	      arg1 = arg1->next;
	      cnf_node->info.expr.arg1->next = NULL;

	      cnf_node->info.expr.arg2 = new_attr;
	      saved_op_type = cnf_node->info.expr.op;
	      cnf_node->info.expr.op = PT_EQ;

	      if (new_attr != NULL)
		{
		  new_attr = new_attr->next;
		  cnf_node->info.expr.arg2->next = NULL;
		}

	      /* save, cut-off link */
	      save_next = cnf_node->next;
	      cnf_node->next = NULL;

	      /* create the following 'attr op attr' */
	      for (tmp = NULL; arg1 && new_attr; arg1 = arg1_next, new_attr = new_attr_next)
		{
		  tmp = parser_new_node (parser, PT_EXPR);
		  if (tmp == NULL)
		    {
		      PT_INTERNAL_ERROR (parser, "allocate new node");
		      return NULL;
		    }

		  /* save, cut-off link */
		  arg1_next = arg1->next;
		  arg1->next = NULL;
		  new_attr_next = new_attr->next;
		  new_attr->next = NULL;

		  tmp->info.expr.arg1 = arg1;
		  tmp->info.expr.arg2 = new_attr;
		  tmp->info.expr.op = PT_EQ;

		  cnf_node = parser_append_node (tmp, cnf_node);
		}

	      if (tmp)
		{		/* move to the last cnf */
		  cnf_node = tmp;
		}
	      cnf_node->next = save_next;	/* restore link */

	      /* apply qo_rewrite_subqueries() to derived table's subquery */
	      (void) parser_walk_tree (parser, new_spec->info.spec.derived_table, qo_rewrite_subqueries, idx, NULL,
				       NULL);
	      break;

	    case PT_GT_SOME:	/* arg1 = attr */
	    case PT_GE_SOME:	/* arg1 = attr */
	    case PT_LT_SOME:	/* arg1 = attr */
	    case PT_LE_SOME:	/* arg1 = attr */
	      if (arg2->node_type == PT_UNION || arg2->node_type == PT_INTERSECTION || arg2->node_type == PT_DIFFERENCE
		  || pt_has_aggregate (parser, arg2) || arg2->info.query.orderby_for)
		{
		  PT_NODE *rewritten = NULL;

		  /* if it is composite query, rewrite to simple query */
		  rewritten = mq_rewrite_query_as_derived (parser, arg2);
		  if (rewritten == NULL)
		    {
		      return NULL;
		    }
		  else
		    {
		      /* fix list */
		      PT_NODE_MOVE_NUMBER_OUTERLINK (rewritten, arg2);
		      arg2 = rewritten;
		    }

		  /* set as uncorrelated subquery */
		  arg2->info.query.q.select.flavor = PT_USER_SELECT;
		  arg2->info.query.is_subquery = PT_IS_SUBQUERY;
		  arg2->info.query.correlation_level = 0;

		  /* free old composite query */
		  parser_free_tree (parser, cnf_node->info.expr.arg2);
		  cnf_node->info.expr.arg2 = arg2;
		}

	      /* make new derived spec and append it to FROM */
	      if (mq_make_derived_spec (parser, node, arg2, idx, &new_spec, &new_attr) == NULL)
		{
		  return NULL;
		}

	      /* apply qo_rewrite_subqueries() to derived table's subquery */
	      (void) parser_walk_tree (parser, new_spec->info.spec.derived_table, qo_rewrite_subqueries, idx, NULL,
				       NULL);

	      select_list = pt_get_select_list (parser, arg2);
	      if (select_list == NULL)
		{
		  return NULL;
		}

	      /* convert select list of subquery to MIN()/MAX() */
	      new_func = parser_new_node (parser, PT_FUNCTION);
	      if (new_func == NULL)
		{
		  PT_INTERNAL_ERROR (parser, "allocate new node");
		  return NULL;
		}

	      new_func->info.function.function_type =
		((op_type == PT_GT_SOME || op_type == PT_GE_SOME) ? PT_MIN : PT_MAX);
	      new_func->info.function.all_or_distinct = PT_ALL;
	      new_func->info.function.arg_list = select_list;
	      new_func->type_enum = select_list->type_enum;
	      new_func->data_type = parser_copy_tree (parser, select_list->data_type);
	      arg2->info.query.q.select.list = new_func;
	      /* mark as agg select */
	      PT_SELECT_INFO_SET_FLAG (arg2, PT_SELECT_INFO_HAS_AGG);

	      /* convert to 'attr > new_attr' */
	      cnf_node->info.expr.arg2 = new_attr;
	      if (op_type == PT_GT_SOME)
		{
		  cnf_node->info.expr.op = PT_GT;
		}
	      else if (op_type == PT_GE_SOME)
		{
		  cnf_node->info.expr.op = PT_GE;
		}
	      else if (op_type == PT_LT_SOME)
		{
		  cnf_node->info.expr.op = PT_LT;
		}
	      else
		{
		  cnf_node->info.expr.op = PT_LE;
		}
	      break;

	    default:
	      break;
	    }
	}
    }				/* for (cnf_node = ...) */

  *continue_walk = PT_LIST_WALK;

  return node;
}

/*
 * qo_rewrite_hidden_col_as_derived () - Rewrite subquery with ORDER BY
 *				      hidden column as derived one
 *   return: PT_NODE *
 *   parser(in):
 *   node(in): QUERY node
 *   parent_node(in):
 *
 * Note: Keep out hidden column from derived select list
 */
PT_NODE *
qo_rewrite_hidden_col_as_derived (PARSER_CONTEXT * parser, PT_NODE * node, PT_NODE * parent_node)
{
  PT_NODE *t_node, *next, *derived;

  switch (node->node_type)
    {
    case PT_SELECT:
      if (node->info.query.order_by)
	{
	  bool remove_order_by = true;	/* guessing */

	  /* check parent context */
	  if (parent_node)
	    {
	      switch (parent_node->node_type)
		{
		case PT_FUNCTION:
		  switch (parent_node->info.function.function_type)
		    {
		    case F_TABLE_SEQUENCE:
		      remove_order_by = false;
		      break;
		    default:
		      break;
		    }
		  break;
		default:
		  break;
		}
	    }
	  else
	    {
	      remove_order_by = false;
	    }

	  /* check node context */
	  if (remove_order_by == true)
	    {
	      if (node->info.query.orderby_for)
		{
		  remove_order_by = false;
		}
	    }

	  if (remove_order_by == true)
	    {
	      for (t_node = node->info.query.q.select.list; t_node; t_node = t_node->next)
		{
		  if (t_node->node_type == PT_EXPR && t_node->info.expr.op == PT_ORDERBY_NUM)
		    {
		      remove_order_by = false;
		      break;
		    }
		}
	    }

	  /* remove unnecessary ORDER BY clause */
	  if (remove_order_by == true && !node->info.query.q.select.connect_by)
	    {
	      parser_free_tree (parser, node->info.query.order_by);
	      node->info.query.order_by = NULL;

	      for (t_node = node->info.query.q.select.list; t_node && t_node->next; t_node = next)
		{
		  next = t_node->next;
		  if (next->flag.is_hidden_column)
		    {
		      parser_free_tree (parser, next);
		      t_node->next = NULL;
		      break;
		    }
		}
	    }
	  else
	    {
	      /* Check whether we can rewrite query as derived. */
	      bool skip_query_rewrite_as_derived = false;
	      if (node->info.query.is_subquery == PT_IS_SUBQUERY && node->info.query.order_by != NULL)
		{
		  /* If all nodes in select list are hidden columns, we do not rewrite the query as derived
		   * since we want to avoid null select list. This will avoid the crash for queries like:
		   * set @a = 1; SELECT  (SELECT @a := @a + 1 FROM db_root ORDER BY @a + 1)
		   */
		  skip_query_rewrite_as_derived = true;
		  for (t_node = node->info.query.q.select.list; t_node; t_node = t_node->next)
		    {
		      if (!t_node->flag.is_hidden_column)
			{
			  skip_query_rewrite_as_derived = false;
			}
		    }
		}

	      if (!skip_query_rewrite_as_derived)
		{
		  for (t_node = node->info.query.q.select.list; t_node; t_node = t_node->next)
		    {
		      if (t_node->flag.is_hidden_column)
			{
			  /* make derived query */
			  derived = mq_rewrite_query_as_derived (parser, node);
			  if (derived == NULL)
			    {
			      break;
			    }

			  PT_NODE_MOVE_NUMBER_OUTERLINK (derived, node);
			  derived->info.query.q.select.flavor = node->info.query.q.select.flavor;
			  derived->info.query.is_subquery = node->info.query.is_subquery;
			  derived->type_enum = node->type_enum;

			  /* free old composite query */
			  parser_free_tree (parser, node);
			  node = derived;
			  break;
			}
		    }
		}
	    }			/* else */
	}
      break;

    case PT_UNION:
    case PT_DIFFERENCE:
    case PT_INTERSECTION:
      node->info.query.q.union_.arg1 = qo_rewrite_hidden_col_as_derived (parser, node->info.query.q.union_.arg1, NULL);
      node->info.query.q.union_.arg2 = qo_rewrite_hidden_col_as_derived (parser, node->info.query.q.union_.arg2, NULL);
      break;
    default:
      return node;
    }

  return node;
}

/*
 * qo_add_keylimit_clause () - Add limit clause to subquery exists
 *   return: void
 *   parser(in):
 *   node(in): QUERY node
 */
void
qo_add_limit_clause (PARSER_CONTEXT * parser, PT_NODE * node)
{
  bool has_instnum = false, has_orderbynum = false, has_groupbynum = false;
  if (PT_IS_SELECT (node))
    {
      (void) parser_walk_tree (parser, node->info.query.q.select.where, pt_check_instnum_pre, NULL,
			       pt_check_instnum_post, &has_instnum);
      (void) parser_walk_tree (parser, node->info.query.orderby_for, pt_check_orderbynum_pre, NULL,
			       pt_check_orderbynum_post, &has_orderbynum);
      (void) parser_walk_tree (parser, node->info.query.q.select.having, pt_check_groupbynum_pre, NULL,
			       pt_check_groupbynum_post, &has_groupbynum);
    }
  if (node->info.query.limit != NULL || has_instnum || has_orderbynum || has_groupbynum)
    {
      return;			/* give up */
    }

  PT_NODE *ins_num = parser_new_node (parser, PT_VALUE);
  ins_num->type_enum = PT_TYPE_INTEGER;
  ins_num->info.value.data_value.i = 1;

  node->info.query.limit = ins_num;
  node->info.query.limit->next = NULL;
  node->info.query.flag.rewrite_limit = 1;
}
