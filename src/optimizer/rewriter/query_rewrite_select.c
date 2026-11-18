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
 * query_select.c
 */

#include "parse_tree.h"
#ident "$Id$"

#include <assert.h>
#include "query_rewrite.h"
#include "query_graph.h"
#include "query_planner.h"
#include "xasl.h"
#include "xasl_generation.h"


static PT_NODE *qo_reset_location (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk);
static PT_NODE *qo_get_name_cnt_by_spec (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk);
static PT_NODE *qo_get_name_cnt_keep_unique (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk);
static PT_NODE *qo_check_keeps_distinct (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk);
static PT_TYPE_ENUM qo_get_host_var_bound_type (PARSER_CONTEXT * parser, const PT_NODE * host_var);
static bool qo_eq_keeps_distinct (PARSER_CONTEXT * parser, PT_NODE * col_side, const PT_NODE * other_side);
static PT_NODE *qo_collect_name_with_eq_const (PARSER_CONTEXT * parser, PT_NODE * on_cond, PT_NODE * spec);
static PT_NODE *qo_reduce_outer_joined_tbls (PARSER_CONTEXT * parser, PT_NODE * spec, PT_NODE * query);
static void qo_reduce_joined_tbls_ref_by_fk (PARSER_CONTEXT * parser, PT_NODE * query);
static bool qo_is_exclude_spec (PT_NODE * exclude_spec_point_list, PT_NODE * spec);
static bool qo_check_pk_ref_by_fk_in_parent_spec (PARSER_CONTEXT * parser, PT_NODE * query,
						  QO_REDUCE_REFERENCE_INFO * reduce_reference_info);
static bool qo_check_fks_ref_pk_in_child_spec (PARSER_CONTEXT * parser, PT_NODE * query,
					       QO_REDUCE_REFERENCE_INFO * reduce_reference_info);
static bool qo_check_fk_ref_pk_in_child_spec (PARSER_CONTEXT * parser, PT_NODE * query,
					      QO_REDUCE_REFERENCE_INFO * reduce_reference_info);
static bool qo_check_reduce_predicate_for_parent_spec (PARSER_CONTEXT * parser, PT_NODE * query,
						       QO_REDUCE_REFERENCE_INFO * reduce_reference_info);
static void qo_reduce_predicate_for_parent_spec (PARSER_CONTEXT * parser, PT_NODE * query,
						 QO_REDUCE_REFERENCE_INFO * reduce_reference_info);
static bool qo_groupby_has_key (PT_NODE * group_by, UINTPTR spec_id, SM_CLASS_CONSTRAINT * cons);
static void qo_remove_useless_groupby_columns (PARSER_CONTEXT * parser, PT_NODE * query);
static PT_NODE *qo_check_unmatchable_pre (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk);
static const char *qo_print_without_paren (PARSER_CONTEXT * parser, PT_NODE * node);
static bool qo_is_constant_order_expr (PARSER_CONTEXT * parser, PT_NODE * where, PT_NODE * col);
static int qo_reduce_order_by (PARSER_CONTEXT * parser, PT_NODE * node);
static PT_NODE *qo_rewrite_oid_equality (PARSER_CONTEXT * parser, PT_NODE * node, PT_NODE * pred, int *seqno);
static bool qo_is_semi_join_on_after (PT_NODE * spec, int location);
static PT_NODE *qo_rewrite_outerjoin (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk);
static PT_NODE *qo_get_next_oid_pred (PT_NODE * pred);

/*
 * qo_rewrite_select_queries () - Rewrite select queries
 *   return: bool
 *   parser(in): parser environment
 *   nodep(in/out): &select node
 *   wherep(in/out): &where node
 *   seqno(in): sequence number
 */
bool
qo_rewrite_select_queries (PARSER_CONTEXT * parser, PT_NODE ** nodep, PT_NODE ** wherep, int *seqno)
{
  PT_NODE *pred;
  PT_NODE *spec, *next;
  if ((*nodep)->node_type == PT_SELECT)
    {
      int continue_walk;

      /* rewrite outer join to inner join */
      qo_rewrite_outerjoin (parser, *nodep, NULL, &continue_walk);

      /* rewrite semi join on a unique key to inner join, after the outer join rewrite read its ON */
      qo_rewrite_unique_semi_inner (parser, *nodep);

      /* rewrite explicit inner join to implicit inner join */
      qo_rewrite_innerjoin (parser, *nodep, NULL, &continue_walk);

      pred = qo_get_next_oid_pred (*wherep);
      if (pred)
	{
	  while (pred)
	    {
	      next = pred->next;
	      *nodep = qo_rewrite_oid_equality (parser, *nodep, pred, seqno);
	      assert_release ((*nodep) != NULL);
	      if ((*nodep) == NULL)
		{
		  return false;
		}

	      pred = qo_get_next_oid_pred (next);
	    }			/* while (pred) */

	  /* re-analyze paths for possible optimizations */
	  (*nodep)->info.query.q.select.from =
	    parser_walk_tree (parser, (*nodep)->info.query.q.select.from, qo_analyze_path_join_pre, NULL,
			      qo_analyze_path_join, (*nodep)->info.query.q.select.where);
	}			/* if (pred) */

      /* shrink the GROUP BY first, so that qo_reduce_order_by () judges the ORDER BY against the
       * grouping key that will actually run */
      qo_remove_useless_groupby_columns (parser, (*nodep));

      if (qo_reduce_order_by (parser, (*nodep)) != NO_ERROR)
	{
	  return false;		/* give up */
	}

      PT_NODE *point_list = NULL;
      PT_NODE *point, *tmp_spec;
      spec = (*nodep)->info.query.q.select.from;
      /* predicate push */
      while (spec)
	{
	  if (spec->info.spec.derived_table_type == PT_IS_SUBQUERY
	      || spec->info.spec.derived_table_type == PT_DERIVED_DBLINK_TABLE)
	    {
	      (void) mq_copypush_sargable_terms (parser, (*nodep), spec);
	    }
	  else
	    {
	      point_list = parser_append_previous_node (pt_point (parser, spec), point_list);
	    }
	  spec = spec->next;
	}
      /* reduce unnecessary tables */
      point = point_list;
      while (point)
	{
	  tmp_spec = point;
	  CAST_POINTER_TO_NODE (tmp_spec);
	  if (mq_is_outer_join_spec (parser, tmp_spec) && !PT_SPEC_IS_CTE (tmp_spec))
	    {
	      (*nodep) = qo_reduce_outer_joined_tbls (parser, tmp_spec, (*nodep));
	    }
	  point = point->next;
	}
      if (point_list != NULL)
	{
	  parser_free_tree (parser, point_list);
	}

      qo_reduce_joined_tbls_ref_by_fk (parser, (*nodep));
    }

  return true;
}

/*
 * qo_rewrite_index_hints () - Rewrite index hint list, removing useless hints
 *   return: PT_NODE *
 *   parser(in):
 *   node(in): QUERY node
 *   parent_node(in):
 */
void
qo_rewrite_index_hints (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  PT_NODE *using_index = NULL, *hint_node, *prev_node, *next_node;

  bool is_sorted, is_idx_reversed, is_idx_match_nokl, is_hint_masked;

  PT_NODE *hint_none, *root_node;
  PT_NODE dummy_hint_local, *dummy_hint;

  switch (statement->node_type)
    {
    case PT_SELECT:
      using_index = statement->info.query.q.select.using_index;
      break;
    case PT_UPDATE:
      using_index = statement->info.update.using_index;
      break;
    case PT_DELETE:
      using_index = statement->info.delete_.using_index;
      break;
    default:
      /* USING index clauses are not allowed for other query types */
      assert (false);
      return;
    }

  if (using_index == NULL)
    {
      /* no index hints, nothing to do here */
      return;
    }

  /* Main logic - we can safely assume that pt_check_using_index() has already checked for possible semantic errors or
   * incompatible index hints. */

  /* basic rewrite, for USING INDEX NONE */
  hint_node = using_index;
  prev_node = NULL;
  hint_none = NULL;
  while (hint_node != NULL)
    {
      if (hint_node->etc == (void *) PT_IDX_HINT_NONE)
	{
	  hint_none = hint_node;
	  break;
	}
      prev_node = (prev_node == NULL) ? hint_node : prev_node->next;
      hint_node = hint_node->next;
    }

  if (hint_none != NULL)
    {
      /* keep only the using_index_none hint stored in hint_none */
      /* update links and discard the first part of the hint list */
      if (prev_node != NULL)
	{
	  prev_node->next = NULL;
	  parser_free_tree (parser, using_index);
	  using_index = NULL;
	}
      /* update links and discard the last part of the hint list */
      hint_node = hint_none->next;
      if (hint_node != NULL)
	{
	  parser_free_tree (parser, hint_node);
	  hint_node = NULL;
	}
      /* update links and keep only the USING INDEX NONE node */
      hint_none->next = NULL;
      using_index = hint_none;
      goto exit;
    }

  if (using_index->etc == (void *) PT_IDX_HINT_ALL_EXCEPT)
    {
      /* find all t.none index hints and mark them for later removal */
      /* the first node, when USING INDEX ALL EXCEPT, is a '*', so use this node as a constant list root */
      hint_node = using_index;
      while (hint_node != NULL && (next_node = hint_node->next) != NULL)
	{
	  if (next_node->info.name.original == NULL && next_node->info.name.resolved != NULL
	      && strcmp (next_node->info.name.resolved, "*") != 0)
	    {
	      /* found a t.none identifier; remove it from the list */
	      hint_node->next = next_node->next;
	      next_node->next = NULL;
	      parser_free_node (parser, next_node);
	    }
	  else
	    {
	      hint_node = hint_node->next;
	    }
	}

      /* if only the '*' marker node is left in the list, it means that USING INDEX ALL EXCEPT contains only
       * t.none-like hints, so it is actually an empty hint list */
      if (using_index->next == NULL)
	{
	  parser_free_node (parser, using_index);
	  using_index = NULL;
	  goto exit;
	}

      root_node = prev_node = using_index;
      hint_node = using_index->next;
    }
  else
    {
      /* there is no USING INDEX {NONE|ALL EXCEPT ...} in the query; the dummy node is necessary for faster operation;
       * use local variable dummy_hint */
      dummy_hint = &dummy_hint_local;
      dummy_hint->next = using_index;
      /* just need something else than PT_IDX_HINT_ALL AEXCEPT, so that this node won't be kept later */
      dummy_hint->etc = (void *) PT_IDX_HINT_USE;
      root_node = prev_node = dummy_hint;
      hint_node = using_index;
    }

  /* remove duplicate index hints and sort them; keep the same order for the hints of the same type with keylimit */
  /* order: class_none, ignored, forced, used */
  is_sorted = false;
  while (!is_sorted)
    {
      prev_node = root_node;
      hint_node = prev_node->next;
      is_sorted = true;
      while ((next_node = hint_node->next) != NULL)
	{
	  is_idx_reversed = false;
	  is_idx_match_nokl = false;
	  if (PT_IDX_HINT_ORDER (hint_node) > PT_IDX_HINT_ORDER (next_node))
	    {
	      is_idx_reversed = true;
	    }
	  else if (hint_node->etc == next_node->etc)
	    {
	      /* if hints have the same type, check if they need to be swapped or are identical and one of them needs
	       * to be removed */
	      int res_cmp_tbl_names = -1;
	      /* unless USING INDEX NONE, which is rewritten above, all indexes should have table names already
	       * resolved */
	      assert (hint_node->info.name.resolved != NULL && next_node->info.name.resolved != NULL);

	      /*
	       * Case of comparing names after dot(.).
	       * 1. When comparing owner_name.class_name and class_name.
	       *    class_name used in index_name must be in from. So, only class_name should be compared,
	       *    not owner_name.
	       *    e.g. select c1 from t1 force index (i1) where c1 >= 0 using index i1(+);
	       *      - resolved_name of "force index (i1)"  : "t1"
	       *      - resolved_name of "using index i1(+)" : "dba.t1"
	       */

	      /* compare the tables on which the indexes are defined */
	      res_cmp_tbl_names =
		pt_user_specified_name_compare (hint_node->info.name.resolved, next_node->info.name.resolved);

	      if (res_cmp_tbl_names == 0)
		{
		  /* also compare index names */
		  if (hint_node->info.name.original != NULL && next_node->info.name.original != NULL)
		    {
		      /* index names can be null if t.none */
		      int res_cmp_idx_names;

		      res_cmp_idx_names =
			intl_identifier_casecmp (hint_node->info.name.original, next_node->info.name.original);
		      if (res_cmp_idx_names == 0)
			{
			  is_idx_match_nokl = true;
			}
		      else
			{
			  is_idx_reversed = (res_cmp_idx_names > 0);
			}
		    }
		  else
		    {
		      /* hints are of the same type, name.original is either NULL or not NULL for both hints */
		      assert (hint_node->info.name.original == NULL && next_node->info.name.original == NULL);
		      /* both hints are "same-table.none"; identical */
		      is_idx_match_nokl = true;
		    }
		}
	      else
		{
		  is_idx_reversed = (res_cmp_tbl_names > 0);
		}

	      if (is_idx_match_nokl)
		{
		  /* The same index is used in both hints; examine the keylimit clauses; if search_node does not have
		   * keylimit, the IF below will skip, and search_node will be deleted */
		  if (next_node->info.name.indx_key_limit != NULL)
		    {
		      /* search_node has keylimit */
		      if (hint_node->info.name.indx_key_limit != NULL)
			{
			  /* hint_node has keylimit; no action is performed; we want to preserve the order of index
			   * hints for the same index, with keylimit */
			  is_idx_reversed = false;
			  is_idx_match_nokl = false;
			}
		      else
			{
			  /* special case; need to delete hint_node and keep search_node, because this one has
			   * keylimit; */
			  assert (!is_idx_reversed);
			  is_idx_reversed = true;
			  /* reverse the two nodes so the code below can be reused for this situation */
			}
		    }		/* endif (search_node) */
		}		/* endif (is_idx_match_nokl) */
	    }

	  if (is_idx_reversed)
	    {
	      /* Interchange the two hints */
	      hint_node->next = next_node->next;
	      next_node->next = hint_node;
	      prev_node->next = next_node;
	      is_sorted = false;
	      /* update hint_node and search_node, for possible delete */
	      hint_node = prev_node->next;
	      next_node = hint_node->next;
	    }

	  if (is_idx_match_nokl)
	    {
	      /* remove search_node */
	      hint_node->next = next_node->next;
	      next_node->next = NULL;
	      parser_free_node (parser, next_node);
	      /* node removed, use prev_node and hint_node in next loop */
	      continue;
	    }
	  prev_node = prev_node->next;
	  hint_node = prev_node->next;
	}
    }

  /* Find index hints to remove later. At this point, the only index hints that can be found in using_index are
   * {USE|FORCE|IGNORE} INDEX and USING INDEX {idx|idx(-)|idx(+)|t.none}... Need to ignore duplicate hints, and hints
   * that are masked by applying the hint operation rules. */
  hint_node = root_node->next;
  while (hint_node != NULL)
    {
      next_node = hint_node->next;
      prev_node = hint_node;
      while (next_node != NULL)
	{
	  if (next_node->etc == hint_node->etc)
	    {
	      /* same hint type; duplicates were already removed, skip hint */
	      prev_node = next_node;
	      next_node = next_node->next;
	      continue;
	    }

	  /* Main logic for removing redundant/masked index hints */
	  /* The hint list is now sorted, first by index type, then by table and index name, so the next_node type is
	   * the same as hint_node or lower in importance (class.none > ignore > force > use), so it is not necessary
	   * to check next_index hint type */
	  is_hint_masked = false;

	  if ((hint_node->etc == (void *) PT_IDX_HINT_CLASS_NONE
	       || ((hint_node->etc == (void *) PT_IDX_HINT_IGNORE || hint_node->etc == (void *) PT_IDX_HINT_FORCE)
		   && (intl_identifier_casecmp (hint_node->info.name.original, next_node->info.name.original) == 0)))
	      && (pt_user_specified_name_compare (hint_node->info.name.resolved, next_node->info.name.resolved) == 0))
	    {
	      is_hint_masked = true;
	    }

	  if (is_hint_masked)
	    {
	      /* hint search_node is masked; remove it from the hint list */
	      prev_node->next = next_node->next;
	      next_node->next = NULL;
	      parser_free_node (parser, next_node);
	      next_node = prev_node;
	    }
	  prev_node = next_node;
	  next_node = next_node->next;
	}
      hint_node = hint_node->next;
    }

  /* remove the dummy first node, if any */
  if (root_node->etc != (void *) PT_IDX_HINT_ALL_EXCEPT)
    {
      using_index = root_node->next;
      root_node->next = NULL;
    }
  else
    {
      using_index = root_node;
    }

exit:
  /* Save changes to query node */
  switch (statement->node_type)
    {
    case PT_SELECT:
      statement->info.query.q.select.using_index = using_index;
      break;
    case PT_UPDATE:
      statement->info.update.using_index = using_index;
      break;
    case PT_DELETE:
      statement->info.delete_.using_index = using_index;
      break;
    default:
      break;
    }
}


/*
 * qo_find_best_path_type () -
 *   return: PT_NODE *
 *   spec(in): path entity to test
 *
 * Note: prunes non spec's
 */
static PT_MISC_TYPE
qo_find_best_path_type (PT_NODE * spec)
{
  PT_MISC_TYPE best_path_type = PT_PATH_OUTER;
  PT_MISC_TYPE path_type;

  /* if any is an inner, the result is inner. if all are outer, the result is outer */

  while (spec)
    {
      path_type = spec->info.spec.meta_class;
      if (path_type == PT_PATH_INNER)
	return PT_PATH_INNER;
      if (path_type != PT_PATH_OUTER)
	best_path_type = PT_PATH_OUTER_WEASEL;

      path_type = qo_find_best_path_type (spec->info.spec.path_entities);
      if (path_type == PT_PATH_INNER)
	return PT_PATH_INNER;
      if (path_type != PT_PATH_OUTER)
	best_path_type = PT_PATH_OUTER_WEASEL;

      spec = spec->next;
    }

  return best_path_type;
}

/*
 * qo_move_on_of_explicit_join_to_where () - move on clause of explicit join to where clause
 *   return: void
 *   parser(in): parser environment
 *   fromp(in/out): &from of SELECT, &spec of UPDATE/DELETE
 *   wherep(in/out): &where of SELECT/UPDATE/DELETE
 *
 * NOTE: It moves on clause of explicit join for SELECT/UPDATE/DELETE to where clase for temporary purpose.
 *       qo_rewrite_queries_post will restore them after several optimizations, for instance, range merge/intersection,
 *       auto-parameterization.
 *
 */
void
qo_move_on_of_explicit_join_to_where (PARSER_CONTEXT * parser, PT_NODE ** fromp, PT_NODE ** wherep)
{
  PT_NODE *t_node, *spec;

  t_node = *wherep;
  while (t_node != NULL && t_node->next != NULL)
    {
      t_node = t_node->next;
    }

  for (spec = *fromp; spec != NULL; spec = spec->next)
    {
      if (spec->node_type == PT_SPEC && spec->info.spec.on_cond != NULL)
	{
	  if (t_node == NULL)
	    {
	      t_node = *wherep = spec->info.spec.on_cond;
	    }
	  else
	    {
	      t_node->next = spec->info.spec.on_cond;
	    }

	  spec->info.spec.on_cond = NULL;

	  while (t_node->next != NULL)
	    {
	      t_node = t_node->next;
	    }
	}
    }
}

/*
 * qo_analyze_path_join_pre () -
 *   return: PT_NODE *
 *   parser(in): parser environment
 *   spec(in): path entity to test
 *   arg(in): where clause to test
 *   continue_walk(in):
 *
 * Note : prunes non spec's
 */
PT_NODE *
qo_analyze_path_join_pre (PARSER_CONTEXT * parser, PT_NODE * spec, void *arg, int *continue_walk)
{
  *continue_walk = PT_CONTINUE_WALK;

  if (spec->node_type != PT_SPEC)
    {
      *continue_walk = PT_STOP_WALK;
    }

  return spec;
}

/*
 * qo_analyze_path_join () -
 *   return: PT_NODE *
 *   parser(in): parser environment
 *   path_spec(in): path entity to test
 *   arg(in): where clause to test
 *   continue_walk(in):
 *
 * Note: tests all non-selector path spec's for the type of join
 * 	that can be done.
 * 	if a null path can be guaranteed to produce no row
 *	tags spec as PT_INNER_PATH
 *
 *	if a null path can have no effect on
 *	(does not appear in) the where clause
 *	tags spec as PT_PATH_OUTER
 *
 *	if a null path COULD affect the where clause (appears),
 *	but cannot be guaranteed to have no effect,
 *	tags the spec as PT_PATH_OUTER_WEASEL. This means
 *	no merge, since I can't prove that this is equivalent
 *	to PT_PATH_INNER. This is treated the same as
 *	PT_PATH_OUTER, with apologies for the silly name.
 *
 */
PT_NODE *
qo_analyze_path_join (PARSER_CONTEXT * parser, PT_NODE * path_spec, void *arg, int *continue_walk)
{
  PT_NODE *where = (PT_NODE *) arg;
  PT_MISC_TYPE path_type;
  SPEC_ID_INFO info;

  *continue_walk = PT_CONTINUE_WALK;

  if (path_spec->node_type == PT_SPEC && path_spec->info.spec.path_conjuncts
      && path_spec->info.spec.meta_class != PT_PATH_INNER)
    {
      /* to get here, this must be a 'normal' outer path entity We may be able to optimize this to an inner path if
       * any sub path is an PT_PATH_INNER, so is this one. otherwise, if any sub-path is NOT an PT_PATH_OUTER, the best
       * we can be is a WEASEL :). Since we are a post function, sub-paths are already set. */
      path_type = qo_find_best_path_type (path_spec->info.spec.path_entities);

      path_spec->info.spec.meta_class = path_type;

      if (path_type != PT_PATH_INNER)
	{
	  info.id = path_spec->info.spec.id;
	  info.appears = false;
	  parser_walk_tree (parser, where, qo_get_name_by_spec_id, &info, NULL, NULL);

	  if (info.appears)
	    {
	      if (qo_check_condition_null (parser, path_spec, where))
		{
		  path_spec->info.spec.meta_class = PT_PATH_INNER;
		}
	      else
		{
		  path_spec->info.spec.meta_class = PT_PATH_OUTER_WEASEL;
		}
	    }
	  else
	    {
	      /* best path type already assigned above */
	    }
	}
    }

  return path_spec;
}


/*
 * qo_convert_attref_to_dotexpr_pre () -
 *   return:
 *   parser(in):
 *   spec(in):
 *   arg(in):
 *   continue_walk(in):
 *
 * Note: prunes PT_SPEC
 */
static PT_NODE *
qo_convert_attref_to_dotexpr_pre (PARSER_CONTEXT * parser, PT_NODE * spec, void *arg, int *continue_walk)
{
  TO_DOT_INFO *info = (TO_DOT_INFO *) arg;

  *continue_walk = PT_CONTINUE_WALK;

  if (spec->node_type == PT_SPEC && spec->info.spec.id == info->old_spec->info.spec.id)
    {
      *continue_walk = PT_LIST_WALK;
    }
  return spec;
}

/*
 * qo_convert_attref_to_dotexpr () -
 *   return:
 *   parser(in):
 *   node(in):
 *   arg(in):
 *   continue_walk(in):
 *
 * Note: looks for any attribute reference x.i in
 *     	select x.i, ... from c x, ... where x.i ... and x {=|IN} expr
 *   	and rewrites those into path expressions t.x.i in
 *     	select t.x.i, ... from table({expr}) as t(x), ... where t.x.i ...
 */
static PT_NODE *
qo_convert_attref_to_dotexpr (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
{
  TO_DOT_INFO *info = (TO_DOT_INFO *) arg;
  PT_NODE *arg1, *arg2, *attr, *rvar;
  PT_NODE *new_spec = info->new_spec;

  if (node->node_type == PT_NAME && node->info.name.spec_id == info->old_spec->info.spec.id)
    {
      attr = new_spec->info.spec.as_attr_list;
      rvar = new_spec->info.spec.range_var;

      switch (node->info.name.meta_class)
	{
	case PT_CLASS:
	  /* must be a data_type entity, so don't change its original name because later xasl domain handling code may
	   * use that name to look up the class. */
	  break;
	case PT_OID_ATTR:
	  /* resolve the name to the new_spec */
	  node->info.name.spec_id = new_spec->info.spec.id;
	  node->info.name.original = attr->info.name.original;
	  node->info.name.resolved = rvar->info.name.original;
	  /* an OID_ATTR becomes a NORMAL attribute reference */
	  if (node->info.name.meta_class == PT_OID_ATTR)
	    node->info.name.meta_class = PT_NORMAL;
	  break;
	case PT_NORMAL:
	  /* we must transform this NAME node into a DOT node in place to preserve its address. (Otherwise, we have to
	   * find all the places that point to it and change them all.) */
	  {
	    arg2 = parser_copy_tree (parser, node);
	    if (arg2)
	      {
		arg2->next = NULL;
	      }
	    arg1 = pt_name (parser, attr->info.name.original);
	    if (arg1)
	      {
		arg1->info.name.resolved = rvar->info.name.original;
		arg1->info.name.spec_id = new_spec->info.spec.id;
		arg1->info.name.meta_class = PT_NORMAL;
		arg1->type_enum = attr->type_enum;
		arg1->data_type = parser_copy_tree (parser, attr->data_type);
	      }

	    int coll_modifier = node->info.name.coll_modifier;
	    short tag_click_counter = node->info.name.tag_click_counter;

	    pt_init_node (node, PT_DOT_);
	    node->info.dot.arg1 = arg1;
	    node->info.dot.arg2 = arg2;
	    node->info.dot.coll_modifier = coll_modifier;
	    node->info.dot.tag_click_counter = tag_click_counter;
	  }
	  break;
	default:
	  break;
	}
    }
  else if (node->node_type == PT_SPEC && node->info.spec.id == info->old_spec->info.spec.id)
    {
      *continue_walk = PT_LIST_WALK;
    }
  return node;
}

/*
 * qo_get_next_oid_pred () -
 *   return:
 *   pred(in): cursor into a subquery's where clause
 *
 * Note:
 *   It requires pred is a cursor into a subquery's where clause that has been
 *   transformed into conjunctive normal form and
 *   effects that returns a pointer to subquery's next CNF-term that can be
 *   rewritten into an oid attribute equality test, if one exists.
 *   returns a NULL pointer otherwise.
 */
static PT_NODE *
qo_get_next_oid_pred (PT_NODE * pred)
{
  while (pred && pred->node_type == PT_EXPR && pred->or_next == NULL)
    {
      if (pred->info.expr.op == PT_EQ || pred->info.expr.op == PT_IS_IN)
	{
	  if (pred->info.expr.arg1 && pred->info.expr.arg1->node_type == PT_NAME
	      && pred->info.expr.arg1->info.name.meta_class == PT_OID_ATTR)
	    {
	      return pred;
	    }
	  if (pred->info.expr.arg2 && pred->info.expr.arg2->node_type == PT_NAME
	      && pred->info.expr.arg2->info.name.meta_class == PT_OID_ATTR)
	    {
	      return pred;
	    }
	}
      pred = pred->next;
    }

  return pred;
}


/*
 * qo_is_oid_const () -
 *   return: Returns true iff the argument looks like a constant for
 *	     the purposes f the oid equality rewrite optimization
 *   node(in):
 */
static int
qo_is_oid_const (PT_NODE * node)
{
  if (node == NULL)
    {
      return 0;
    }

  switch (node->node_type)
    {
    case PT_VALUE:
    case PT_HOST_VAR:
      return 1;

    case PT_NAME:
      /*
       * This *could* look to see if the name is correlated to the same
       * level as the caller, but that's going to require more context
       * to come in...
       */
      return node->info.name.meta_class == PT_PARAMETER;

    case PT_FUNCTION:
      if (node->info.function.function_type != F_SET && node->info.function.function_type != F_MULTISET
	  && node->info.function.function_type != F_SEQUENCE)
	{
	  return 0;
	}
      else
	{
	  /*
	   * The is the case for an expression like
	   *
	   *  {:a, :b, :c}
	   *
	   * Here the the expression '{:a, :b, :c}' comes in as a
	   * sequence function call, with PT_NAMEs 'a', 'b', and 'c' as
	   * its arglist.
	   */
	  PT_NODE *p;

	  for (p = node->info.function.arg_list; p; p = p->next)
	    {
	      if (!qo_is_oid_const (p))
		return 0;
	    }
	  return 1;
	}

    case PT_SELECT:
    case PT_UNION:
    case PT_DIFFERENCE:
    case PT_INTERSECTION:
      return node->info.query.correlation_level != 1;

    default:
      return 0;
    }
}

/*
 * qo_construct_new_set () -
 *   return:
 *   parser(in): parser context
 *   node(in): an OID_ATTR equality/IN predicate
 *
 * Note:
 *   It requires that node is an OID_ATTR predicate (x {=|IN} expr) from
 *        select ... from c x, ... where ... and x {=|IN} expr
 *   and modifies parser heap
 *   and effects that creates, initializes, returns a new set constructor
 *   subtree that can be used for the derived table field of a new PT_SPEC
 *    node representing 'table({expr}) as t(x)' in the rewritten
 *        select ... from table({expr}) as t(x), ... where ...
 */
static PT_NODE *
qo_construct_new_set (PARSER_CONTEXT * parser, PT_NODE * node)
{
  PT_NODE *arg = NULL, *set = NULL;
  /* jabaek: modify SQLM */
  PT_NODE *targ = NULL;

  /* make sure we have reasonable arguments */
  if (!node || node->node_type != PT_EXPR)
    return set;

  /* if control reaches here, then qo_get_next_oid_pred must have succeeded in finding a CNF-term: 'x {=|IN} expr' from
   * a query select ... from c x, ... where ... and x {=|IN} expr Now, copy 'expr' into a derived table:
   * 'table({expr})' which the caller will put into the transformed query select ... from table({expr}) as t(x), ...
   * where ... */
  switch (node->info.expr.op)
    {
    case PT_EQ:
      if (node->info.expr.arg1 && node->info.expr.arg1->node_type == PT_NAME
	  && node->info.expr.arg1->info.name.meta_class == PT_OID_ATTR && qo_is_oid_const (node->info.expr.arg2))
	{
	  arg = parser_copy_tree (parser, node->info.expr.arg2);
	  targ = node->info.expr.arg1;
	}
      else if (node->info.expr.arg2 && node->info.expr.arg2->node_type == PT_NAME
	       && node->info.expr.arg2->info.name.meta_class == PT_OID_ATTR && qo_is_oid_const (node->info.expr.arg1))
	{
	  arg = parser_copy_tree (parser, node->info.expr.arg1);
	  targ = node->info.expr.arg2;
	}
      break;
    case PT_IS_IN:
      if (PT_IS_OID_NAME (node->info.expr.arg1) && PT_IS_FUNCTION (node->info.expr.arg2)
	  && PT_IS_CONST_INPUT_HOSTVAR (node->info.expr.arg2->info.function.arg_list))
	{
	  arg = parser_copy_tree (parser, node->info.expr.arg2->info.function.arg_list);
	  targ = node->info.expr.arg1;
	}
      else if (PT_IS_OID_NAME (node->info.expr.arg2) && PT_IS_FUNCTION (node->info.expr.arg1)
	       && PT_IS_CONST_INPUT_HOSTVAR (node->info.expr.arg1->info.function.arg_list))
	{
	  arg = parser_copy_tree (parser, node->info.expr.arg1->info.function.arg_list);
	  targ = node->info.expr.arg2;
	}
      break;
    default:
      break;
    }

  /* create mset constructor subtree */
  if (arg && (set = parser_new_node (parser, PT_FUNCTION)) != NULL)
    {
      set->info.function.function_type = F_SEQUENCE;
      set->info.function.arg_list = arg;
      set->type_enum = PT_TYPE_SEQUENCE;

      set->data_type = parser_copy_tree_list (parser, arg->data_type);
    }

  return set;
}

/*
 * qo_make_new_derived_tblspec () -
 *   return:
 *   parser(in): parser context
 *   node(in): a PT_SPEC node
 *   pred(in): node's OID_ATTR predicate
 *   seqno(in/out): sequence number for generating unique derived table names
 *
 * Note:
 *   It requires that node is the PT_SPEC node (c x) and
 *   pred is the OID_ATTR predicate (x {=|IN} expr) from
 *        select ... from c x, ... where ... and x {=|IN} expr
 *   and modifies parser heap, node
 *   and effects that creates, initializes, returns a new derived table
 *   type PT_SPEC node representing 'table({expr}) as t(x)' in the rewritten
 *        select ... from table({expr}) as t(x), ... where ...
 */
static PT_NODE *
qo_make_new_derived_tblspec (PARSER_CONTEXT * parser, PT_NODE * node, PT_NODE * pred, int *seqno)
{
  PT_NODE *spec = NULL, *dtbl, *eq = NULL, *rvar;
  UINTPTR spec_id;
  const char *dtblnam, *dattnam;

  dtbl = qo_construct_new_set (parser, pred);
  if (!dtbl)
    {
      return NULL;
    }

  spec = parser_new_node (parser, PT_SPEC);
  if (spec)
    {
      spec_id = (UINTPTR) spec;
      spec->info.spec.id = spec_id;
      spec->info.spec.only_all = PT_ONLY;
      spec->info.spec.derived_table_type = PT_IS_SET_EXPR;
      spec->info.spec.derived_table = dtbl;
      dtblnam = mq_generate_name (parser, "dt", seqno);
      dattnam = mq_generate_name (parser, "da", seqno);
      spec->info.spec.range_var = pt_name (parser, dtblnam);
      if (spec->info.spec.range_var == NULL)
	{
	  goto exit_on_error;
	}
      spec->info.spec.range_var->info.name.spec_id = spec_id;
      spec->info.spec.as_attr_list = pt_name (parser, dattnam);
      if (spec->info.spec.as_attr_list == NULL)
	{
	  goto exit_on_error;
	}
      spec->info.spec.as_attr_list->info.name.spec_id = spec_id;
      spec->info.spec.as_attr_list->info.name.meta_class = PT_NORMAL;
      spec->info.spec.as_attr_list->type_enum = PT_TYPE_OBJECT;
      spec->info.spec.as_attr_list->data_type = parser_copy_tree (parser, dtbl->data_type);
      if (node && node->node_type == PT_SPEC && (rvar = node->info.spec.range_var) != NULL)
	{
	  /* new derived table spec needs path entities */
	  spec->info.spec.path_entities = node;

	  /* we also need to graft a path conjunct to node */
	  node->info.spec.path_conjuncts = eq = parser_new_node (parser, PT_EXPR);
	  if (eq)
	    {
	      eq->type_enum = PT_TYPE_LOGICAL;
	      eq->info.expr.op = PT_EQ;
	      eq->info.expr.arg1 = pt_name (parser, dattnam);
	      if (eq->info.expr.arg1 == NULL)
		{
		  goto exit_on_error;
		}
	      eq->info.expr.arg1->info.name.spec_id = spec_id;
	      eq->info.expr.arg1->info.name.resolved = dtblnam;
	      eq->info.expr.arg1->info.name.meta_class = PT_NORMAL;
	      eq->info.expr.arg1->type_enum = PT_TYPE_OBJECT;
	      eq->info.expr.arg1->data_type = parser_copy_tree (parser, dtbl->data_type);
	      eq->info.expr.arg2 = pt_name (parser, "");
	      if (eq->info.expr.arg2 == NULL)
		{
		  goto exit_on_error;
		}
	      eq->info.expr.arg2->info.name.spec_id = node->info.spec.id;
	      eq->info.expr.arg2->info.name.resolved = rvar->info.name.original;
	      eq->info.expr.arg2->info.name.meta_class = PT_OID_ATTR;
	      eq->info.expr.arg2->type_enum = PT_TYPE_OBJECT;
	      eq->info.expr.arg2->data_type = parser_copy_tree (parser, dtbl->data_type);
	    }
	}
    }
  return spec;

exit_on_error:
  if (eq)
    {
      if (eq->info.expr.arg1)
	{
	  parser_free_node (parser, eq->info.expr.arg1);
	}
      if (eq->info.expr.arg2)
	{
	  parser_free_node (parser, eq->info.expr.arg2);
	}
    }
  if (spec->info.spec.range_var)
    {
      parser_free_node (parser, spec->info.spec.range_var);
    }
  if (spec->info.spec.as_attr_list)
    {
      parser_free_node (parser, spec->info.spec.as_attr_list);
    }
  parser_free_node (parser, spec);
  return NULL;
}

/*
 * qo_rewrite_oid_equality () -
 *   return:
 *   parser(in): parser context
 *   node(in): a subquery
 *   pred(in): subquery's OID_ATTR equality/IN predicate
 *   seqno(in/out): seq number for generating unique derived table/attr names
 *
 * Note:
 *   It requires that node is a subquery of the form
 *       select ... from c x, ... where ... and x {=|IN} expr
 *       pred is x {=|IN} expr
 *   and modifies node
 *   and effects that rewrites node into the form
 *       select ... from table({expr}) as t(x), ... where ...
 */
static PT_NODE *
qo_rewrite_oid_equality (PARSER_CONTEXT * parser, PT_NODE * node, PT_NODE * pred, int *seqno)
{
  PT_NODE *prev, *next, *from, *new_spec, *prev_spec = NULL;
  UINTPTR spec_id = 0;
  int found;

  /* make sure we have reasonable arguments */
  if (pred->node_type != PT_EXPR || pred->type_enum != PT_TYPE_LOGICAL
      || (pred->info.expr.op != PT_EQ && pred->info.expr.op != PT_IS_IN))
    {
      return node;
    }
  else if (pred->info.expr.arg1 && pred->info.expr.arg1->node_type == PT_NAME
	   && pred->info.expr.arg1->info.name.meta_class == PT_OID_ATTR && qo_is_oid_const (pred->info.expr.arg2))
    {
      spec_id = pred->info.expr.arg1->info.name.spec_id;
    }
  else if (pred->info.expr.arg2 && pred->info.expr.arg2->node_type == PT_NAME
	   && pred->info.expr.arg2->info.name.meta_class == PT_OID_ATTR && qo_is_oid_const (pred->info.expr.arg1))
    {
      spec_id = pred->info.expr.arg2->info.name.spec_id;
    }
  else
    {
      return node;		/* bail out without rewriting node */
    }

  /* make sure spec_id resolves to a regular spec in node */
  from = node->info.query.q.select.from;
  if (from && from->node_type == PT_SPEC && from->info.spec.id == spec_id)
    {
      found = 1;
    }
  else
    {
      found = 0;
      prev_spec = from;
      while (from && from->node_type == PT_SPEC)
	{
	  if (from->info.spec.id == spec_id)
	    {
	      found = 1;
	      break;
	    }
	  prev_spec = from;
	  from = from->next;
	}
    }

  if (!found)
    {
      return node;		/* bail out without rewriting node */
    }

  /* There is no advantage to rewriting class OID predicates like select ... from class c x, ... where x = expr so
   * screen those cases out now. */
  if (from->info.spec.meta_class == PT_META_CLASS)
    return node;		/* bail out without rewriting node */

  /* put node's PT_SPEC into a new derived table type PT_SPEC */
  new_spec = qo_make_new_derived_tblspec (parser, from, pred, seqno);
  if (!new_spec)
    return node;		/* bail out without rewriting node */

  /* excise pred from node's where clause */
  if (pred == node->info.query.q.select.where)
    {
      node->info.query.q.select.where = pred->next;
    }
  else
    {
      prev = next = node->info.query.q.select.where;
      while (next)
	{
	  if (next == pred)
	    {
	      prev->next = next->next;
	      break;
	    }
	  prev = next;
	  next = next->next;
	}
    }

  /* replace old PT_SPEC with new_spec in node's from list */
  new_spec->next = from->next;
  from->next = NULL;
  if (from == node->info.query.q.select.from)
    {
      node->info.query.q.select.from = new_spec;
    }
  else if (prev_spec != NULL)
    {
      prev_spec->next = new_spec;
    }

  /* transform attribute references x.i in select x.i, ... from c x, ... where x.i ... and x {=|IN} expr into path
   * expressions t.x.i in select t.x.i, ... from table({expr}) as t(x), ... where t.x.i ... */
  {
    TO_DOT_INFO dinfo;
    dinfo.old_spec = from;
    dinfo.new_spec = new_spec;
    parser_walk_tree (parser, node, qo_convert_attref_to_dotexpr_pre, &dinfo, qo_convert_attref_to_dotexpr, &dinfo);
  }

  node = mq_reset_ids_in_statement (parser, node);
  return node;
}

/*
 * qo_reduce_order_by_for () - move orderby_num() to groupby_num()
 *   return: NO_ERROR if successful, otherwise returns error number
 *   parser(in): parser global context info for reentrancy
 *   node(in): query node has ORDER BY
 *
 * Note:
 *   It modifies parser's heap of PT_NODEs(parser->error_msgs)
 *   and effects that remove order by for clause
 */
static int
qo_reduce_order_by_for (PARSER_CONTEXT * parser, PT_NODE * node)
{
  int error = NO_ERROR;
  PT_NODE *ord_num, *grp_num;

  if (node->node_type != PT_SELECT)
    {
      return error;
    }

  ord_num = NULL;
  grp_num = NULL;

  /* move orderby_num() to groupby_num() */
  if (node->info.query.orderby_for)
    {
      /* generate orderby_num(), groupby_num() */
      if (!(ord_num = parser_new_node (parser, PT_EXPR)) || !(grp_num = parser_new_node (parser, PT_FUNCTION)))
	{
	  if (ord_num)
	    {
	      parser_free_tree (parser, ord_num);
	    }
	  PT_ERRORm (parser, node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OUT_OF_MEMORY);
	  goto exit_on_error;
	}

      ord_num->type_enum = PT_TYPE_BIGINT;
      ord_num->info.expr.op = PT_ORDERBY_NUM;
      PT_EXPR_INFO_SET_FLAG (ord_num, PT_EXPR_INFO_ORDERBYNUM_C);

      grp_num->type_enum = PT_TYPE_BIGINT;
      grp_num->info.function.function_type = PT_GROUPBY_NUM;
      grp_num->info.function.arg_list = NULL;
      grp_num->info.function.all_or_distinct = PT_ALL;

      /* replace orderby_num() to groupby_num() */
      node->info.query.orderby_for = pt_lambda_with_arg (parser, node->info.query.orderby_for, ord_num, grp_num,
							 false /* loc_check: DEFAULT */ ,
							 2 /* type: don't walk into subquery */ ,
							 false /* dont_replace: DEFAULT */ );

      /* Even though node->info.q.query.q.select has no orderby_num so far, it is a safe guard to prevent potential
       * rewrite problem. */
      node->info.query.q.select.list = pt_lambda_with_arg (parser, node->info.query.q.select.list, ord_num, grp_num,
							   false /* loc_check: DEFAULT */ ,
							   2 /* type: don't walk into subquery */ ,
							   false /* dont_replace: DEFAULT */ );

      node->info.query.q.select.having =
	parser_append_node (node->info.query.orderby_for, node->info.query.q.select.having);

      node->info.query.orderby_for = NULL;

      parser_free_tree (parser, ord_num);
      parser_free_tree (parser, grp_num);
    }

exit_on_end:

  return error;

exit_on_error:

  if (error == NO_ERROR)
    {
      /* missing compiler error list */
      error = ER_GENERIC_ERROR;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
    }
  goto exit_on_end;
}

/*
 * qo_is_row_identifying_key () - check whether a constraint picks out a single row, and so
 *				  determines every other column of its table
 *   return: true, if the constraint may be used to drop redundant GROUP BY columns
 *   cons(in): class constraint
 *
 * Note:
 *   A UNIQUE constraint accepts NULL more than once while GROUP BY treats NULLs as equal, so
 *   two rows could share such a key and still differ in the other columns. Every column of a
 *   unique key must therefore be NOT NULL as well. A primary key is NOT NULL by definition.
 *
 *   Only SM_NORMAL_INDEX is trusted: an invisible index or one that is still being built
 *   online does not guarantee uniqueness yet.
 */
bool
qo_is_row_identifying_key (SM_CLASS_CONSTRAINT * cons)
{
  int i;

  if (!SM_IS_CONSTRAINT_UNIQUE_FAMILY (cons->type))
    {
      return false;
    }

  if (cons->attributes == NULL)
    {
      return false;
    }

  if (cons->index_status != SM_NORMAL_INDEX)
    {
      return false;
    }

  if (cons->type == SM_CONSTRAINT_PRIMARY_KEY)
    {
      return true;
    }

  for (i = 0; cons->attributes[i] != NULL; i++)
    {
      if (!(cons->attributes[i]->flags & SM_ATTFLAG_NON_NULL))
	{
	  return false;
	}
    }

  return true;
}

/*
 * qo_groupby_has_key () - check whether the GROUP BY clause groups on every column of a key
 *   return: true, if every column of the key is in the clause
 *   group_by(in): GROUP BY clause, a list of PT_SORT_SPEC
 *   spec_id(in): id of the spec that owns the key
 *   cons(in): class constraint holding the key columns
 */
static bool
qo_groupby_has_key (PT_NODE * group_by, UINTPTR spec_id, SM_CLASS_CONSTRAINT * cons)
{
  PT_NODE *group, *col;
  const char *name;
  int i;

  for (i = 0; cons->attributes[i] != NULL; i++)
    {
      name = cons->attributes[i]->header.name;

      for (group = group_by; group != NULL; group = group->next)
	{
	  col = group->info.sort_spec.expr;

	  if (col->node_type == PT_NAME && col->info.name.spec_id == spec_id
	      && intl_identifier_casecmp (col->info.name.original, name) == 0)
	    {
	      break;
	    }
	}

      if (group == NULL)
	{
	  /* ran out of clause without meeting this key column */
	  return false;
	}
    }

  return true;
}

/*
 * qo_remove_useless_groupby_columns () - remove the GROUP BY columns that are functionally determined
 *			   by a key of the same table
 *   return: void
 *   parser(in): parser global context info for reentrancy
 *   query(in/out): query node has GROUP BY
 *
 * Note:
 *   When every column of a table's primary key, or of one of its NOT NULL unique keys, is
 *   listed in the GROUP BY clause, the other GROUP BY columns of that same table cannot vary
 *   inside a group, so they never split a group. Removing them shrinks the grouping key that
 *   pt_to_buildlist_proc () builds later on, which makes both the sort key and the hash key
 *   narrower. Their values are still produced for the output, because pt_to_aggregate ()
 *   appends every select list name that is missing from the intermediate output list.
 *
 *   e.g. create table t (pk int primary key, name varchar (100), amount int);
 *
 *        select pk, name, sum (amount) from t group by pk, name;
 *          -> select pk, name, sum (amount) from t group by pk;
 *
 *        select pk, name, sum (amount) from t group by name, pk;
 *          -> select pk, name, sum (amount) from t group by pk;
 *
 *   A column goes wherever it is written, so the order in which a sorted GROUP BY hands out its
 *   groups may change. This must therefore run before qo_reduce_order_by (), which drops an
 *   ORDER BY only when the GROUP BY that will actually run still covers it.
 *
 *   When more than one key of the table is covered, the one with the fewest columns wins,
 *   because everything outside the chosen key is removed. A key column written twice keeps
 *   only its first occurrence.
 */
static void
qo_remove_useless_groupby_columns (PARSER_CONTEXT * parser, PT_NODE * query)
{
  PT_NODE *group, *next, *spec, *col, *dup;
  DB_OBJECT *classop;
  SM_CLASS_CONSTRAINT *cons, *best_cons;
  int i, size, best_size;

  if (query->node_type != PT_SELECT)
    {
      return;
    }

  if (query->info.query.q.select.group_by == NULL || query->info.query.q.select.group_by->flag.with_rollup)
    {
      /* ROLLUP builds a partial group for every prefix of the grouping key, so none of its
       * columns may be dropped */
      return;
    }

  for (spec = query->info.query.q.select.from; spec != NULL; spec = spec->next)
    {
      classop = NULL;
      PT_SPEC_GET_DB_OBJECT (spec, classop);
      if (classop == NULL)
	{
	  /* derived tables and CTEs carry no constraint to rely on */
	  continue;
	}

      best_cons = NULL;
      best_size = 0;

      for (cons = sm_class_constraints (classop); cons != NULL; cons = cons->next)
	{
	  if (!qo_is_row_identifying_key (cons)
	      || !qo_groupby_has_key (query->info.query.q.select.group_by, spec->info.spec.id, cons))
	    {
	      continue;
	    }

	  for (size = 0; cons->attributes[size]; size++)
	    {
	      ;
	    }

	  if (best_cons == NULL || size < best_size)
	    {
	      best_cons = cons;
	      best_size = size;
	    }
	}

      if (best_cons == NULL)
	{
	  continue;
	}

      for (group = query->info.query.q.select.group_by; group != NULL; group = next)
	{
	  next = group->next;

	  col = group->info.sort_spec.expr;
	  if (col->node_type != PT_NAME || col->info.name.spec_id != spec->info.spec.id)
	    {
	      continue;
	    }

	  for (i = 0; best_cons->attributes[i]; i++)
	    {
	      if (intl_identifier_casecmp (col->info.name.original, best_cons->attributes[i]->header.name) == 0)
		{
		  break;
		}
	    }

	  if (best_cons->attributes[i] != NULL)
	    {
	      /* a key column stays; a repeat of it further on orders nothing, so only the first one is kept */
	      while ((dup = pt_find_order_value_in_list (parser, col, group->next)) != NULL)
		{
		  group->next = pt_remove_from_list (parser, dup, group->next);
		}

	      next = group->next;
	      continue;
	    }

	  query->info.query.q.select.group_by =
	    pt_remove_from_list (parser, group, query->info.query.q.select.group_by);
	}
    }
}

/*
 * qo_check_unmatchable_pre () - find a node that keeps an expression from being matched by its printed text
 *   return: node
 *   parser(in): parser context
 *   node(in): node to check
 *   arg(out): bool, set to true when such a node is found
 *   continue_walk(in/out): walk control
 */
static PT_NODE *
qo_check_unmatchable_pre (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
{
  bool *found = (bool *) arg;

  if (PT_IS_QUERY (node) || node->node_type == PT_METHOD_CALL)
    {
      *found = true;
    }
  /* a money value prints rounded to two decimals, so different amounts can print the same */
  else if (node->node_type == PT_VALUE && node->type_enum == PT_TYPE_MONETARY)
    {
      *found = true;
    }
  else if (node->node_type == PT_EXPR)
    {
      switch (node->info.expr.op)
	{
	case PT_RAND:
	case PT_RANDOM:
	case PT_DRAND:
	case PT_DRANDOM:
	case PT_SYS_GUID:
	case PT_UUID:
	case PT_CURRENT_VALUE:
	case PT_NEXT_VALUE:
	case PT_INST_NUM:
	case PT_ROWNUM:
	case PT_ORDERBY_NUM:
	case PT_LEVEL:
	case PT_PRIOR:
	case PT_CONNECT_BY_ROOT:
	case PT_SYS_CONNECT_BY_PATH:
	case PT_CONNECT_BY_ISLEAF:
	case PT_CONNECT_BY_ISCYCLE:
	case PT_DEFINE_VARIABLE:
	case PT_EVALUATE_VARIABLE:
	  *found = true;
	  break;
	default:
	  break;
	}
    }

  if (*found)
    {
      *continue_walk = PT_STOP_WALK;
    }

  return node;
}

/*
 * qo_print_without_paren () - print a node ignoring the parentheses of its top-level expression
 *   return: printed string
 *   parser(in): parser context
 *   node(in): node to print
 *
 * Note: view/derived translation copies a select list expression with paren_type set, which only
 *   affects printing, so it must not make otherwise equal expressions differ.
 */
static const char *
qo_print_without_paren (PARSER_CONTEXT * parser, PT_NODE * node)
{
  const char *str;
  int paren_type;

  if (node->node_type != PT_EXPR)
    {
      return parser_print_tree (parser, node);
    }

  paren_type = node->info.expr.paren_type;
  node->info.expr.paren_type = 0;
  str = parser_print_tree (parser, node);
  node->info.expr.paren_type = paren_type;

  return str;
}

/*
 * qo_is_constant_order_expr () - is the ORDER BY expression fixed to a constant by a WHERE term?
 *   return: true if a top-level WHERE conjunct 'col = const' (or 'col range (const =)') exists
 *   parser(in): parser context
 *   where(in): WHERE clause of the query
 *   col(in): select list node an ORDER BY item refers to
 *
 * Note: a plain column is handled by qo_reduce_equality_terms () through PT_NAME_INFO_CONSTANT;
 *   'expr = const' is left in place there to keep function index usage, so it is matched here.
 */
static bool
qo_is_constant_order_expr (PARSER_CONTEXT * parser, PT_NODE * where, PT_NODE * col)
{
  PT_NODE *term, *arg1, *arg2, *other, *val, *col_next;
  const char *col_str, *other_str;
  bool unmatchable = false;

  if (where == NULL || (col->node_type != PT_EXPR && col->node_type != PT_FUNCTION))
    {
      return false;
    }

  col_next = col->next;
  col->next = NULL;
  (void) parser_walk_tree (parser, col, qo_check_unmatchable_pre, &unmatchable, NULL, NULL);
  col->next = col_next;
  if (unmatchable)
    {
      return false;
    }

  col_str = qo_print_without_paren (parser, col);
  if (col_str == NULL)
    {
      return false;
    }

  for (term = where; term != NULL; term = term->next)
    {
      /* a term with location > 0 is an outer join ON condition and does not filter the rows */
      if (term->node_type != PT_EXPR || term->or_next != NULL || term->info.expr.location != 0
	  || PT_EXPR_INFO_IS_FLAGED (term, PT_EXPR_INFO_ANTI_JOIN_ON))
	{
	  continue;
	}

      arg1 = term->info.expr.arg1;
      arg2 = term->info.expr.arg2;
      if (arg1 == NULL || arg2 == NULL)
	{
	  continue;
	}

      if (term->info.expr.op == PT_EQ)
	{
	  if (qo_is_reduceable_const (arg2))
	    {
	      other = arg1;
	      val = arg2;
	    }
	  else if (qo_is_reduceable_const (arg1))
	    {
	      other = arg2;
	      val = arg1;
	    }
	  else
	    {
	      continue;
	    }
	}
      else if (term->info.expr.op == PT_RANGE)
	{
	  if (arg2->node_type != PT_EXPR || arg2->or_next != NULL || arg2->info.expr.op != PT_BETWEEN_EQ_NA
	      || !qo_is_reduceable_const (arg2->info.expr.arg1))
	    {
	      continue;
	    }
	  other = arg1;
	  val = arg2->info.expr.arg1;
	}
      else
	{
	  continue;
	}

      if (!qo_is_exact_compare_const (parser, other, val))
	{
	  continue;
	}

      /* case sensitive and without stripping CAST: a collation cast on the WHERE side must not match */
      other_str = qo_print_without_paren (parser, other);
      if (other_str != NULL && strcmp (col_str, other_str) == 0)
	{
	  return true;
	}
    }

  return false;
}

/*
 * qo_reduce_order_by () -
 *   return: NO_ERROR, if successful, otherwise returns error number
 *   parser(in): parser global context info for reentrancy
 *   node(in): query node has ORDER BY
 *
 * Note:
 *   It modifies parser's heap of PT_NODEs(parser->error_msgs)
 *   and effects that reduce the constant orders
 */
static int
qo_reduce_order_by (PARSER_CONTEXT * parser, PT_NODE * node)
{
  int error = NO_ERROR;
  PT_NODE *order, *order_next, *order_prev, *col, *col2, *col2_next;
  PT_NODE *r, *new_r;
  int i, j;
  int const_order_count, order_move_count;
  bool need_merge_check;
  bool has_orderbynum_with_groupby;
  bool is_const_col;
  bool with_rollup;

  /* do not reduce order by siblings */
  if (node->node_type != PT_SELECT || node->info.query.q.select.connect_by)
    {
      return error;
    }

  /* init */
  const_order_count = order_move_count = 0;
  need_merge_check = false;
  has_orderbynum_with_groupby = false;

  with_rollup = node->info.query.q.select.group_by != NULL && node->info.query.q.select.group_by->flag.with_rollup;

  /* check for merge order by to group by( without DISTINCT, HAVING clause and ROLLUP) */

  if (node->info.query.all_distinct == PT_DISTINCT)
    {
      ;				/* give up */
    }
  else if (with_rollup)
    {
      /* give up: groupby_num () numbers the rows in the GROUP BY output order, a subtotal after its groups and the
       * total last, and its bound is applied before the ORDER BY sorts the rows. Moving the orderby_num () bound of a
       * LIMIT to groupby_num () would keep rows that are not the first rows of the ORDER BY, so the subtotal rows are
       * dropped or shuffled. The semantic check rejects GROUPBY_NUM () with ROLLUP, but not the one this rewrite makes. */
      ;
    }
  else
    {
      if (node->info.query.q.select.group_by && node->info.query.q.select.having == NULL && node->info.query.order_by)
	{
	  bool ordbynum_flag;

	  ordbynum_flag = false;	/* init */

	  /* check for orderby_num() in the select list */
	  (void) parser_walk_tree (parser, node->info.query.q.select.list, pt_check_orderbynum_pre, NULL,
				   pt_check_orderbynum_post, &ordbynum_flag);

	  if (ordbynum_flag)
	    {			/* found orderby_num() in the select list */
	      has_orderbynum_with_groupby = true;	/* give up */
	    }
	  else
	    {
	      need_merge_check = true;	/* mark to checking */
	    }
	}
    }

  /* the first phase, do check the current order by */
  if (need_merge_check)
    {
      if (pt_sort_spec_cover (node->info.query.q.select.group_by, node->info.query.order_by))
	{
	  if (qo_reduce_order_by_for (parser, node) != NO_ERROR)
	    {
	      goto exit_on_error;
	    }

	  if (node->info.query.orderby_for == NULL && !node->info.query.q.select.connect_by)
	    {
	      /* clear unnecessary node info */
	      parser_free_tree (parser, node->info.query.order_by);
	      node->info.query.order_by = NULL;
	    }

	  need_merge_check = false;	/* clear */
	}
    }

  order_prev = NULL;
  for (order = node->info.query.order_by; order; order = order_next)
    {
      order_next = order->next;

      r = order->info.sort_spec.expr;

      /*
       * safe guard: check for integer value. */
      if (r->node_type != PT_VALUE)
	{
	  goto exit_on_error;
	}

      col = node->info.query.q.select.list;
      for (i = 1; i < r->info.value.data_value.i; i++)
	{
	  if (col == NULL)
	    {			/* impossible case */
	      break;
	    }
	  col = col->next;
	}

      /*
       * safe guard: invalid parse tree */
      if (col == NULL)
	{
	  goto exit_on_error;
	}

      col = pt_get_end_path_node (col);
      if (with_rollup)
	{
	  /* a super-aggregate row of ROLLUP outputs NULL for the grouped expression, so it is not constant */
	  is_const_col = false;
	}
      else if (col->node_type == PT_NAME)
	{
	  is_const_col = PT_NAME_INFO_IS_FLAGED (col, PT_NAME_INFO_CONSTANT);
	}
      else
	{
	  is_const_col = qo_is_constant_order_expr (parser, node->info.query.q.select.where, col);
	}

      if (is_const_col)
	{
	  /* remove constant order node */
	  if (order_prev == NULL)
	    {			/* the first */
	      node->info.query.order_by = order->next;	/* re-link */
	    }
	  else
	    {
	      order_prev->next = order->next;	/* re-link */
	    }
	  order->next = NULL;	/* cut-off */
	  parser_free_tree (parser, order);

	  const_order_count++;	/* increase const entry remove count */

	  continue;		/* go ahead */
	}

      if (col->node_type == PT_NAME)
	{
	  /* for non-constant order, change order position to the same left-most col's position */
	  col2 = node->info.query.q.select.list;
	  for (j = 1; j < i; j++)
	    {
	      col2_next = col2->next;	/* save next link */

	      col2 = pt_get_end_path_node (col2);

	      /* change to the same left-most col */
	      if (pt_name_equal (parser, col2, col))
		{
		  new_r = parser_new_node (parser, PT_VALUE);
		  if (new_r == NULL)
		    {
		      error = MSGCAT_SEMANTIC_OUT_OF_MEMORY;
		      PT_ERRORm (parser, col, MSGCAT_SET_PARSER_SEMANTIC, error);
		      goto exit_on_error;
		    }

		  new_r->type_enum = PT_TYPE_INTEGER;
		  new_r->info.value.data_value.i = j;
		  pt_value_to_db (parser, new_r);
		  parser_free_tree (parser, r);
		  order->info.sort_spec.expr = new_r;
		  order->info.sort_spec.pos_descr.pos_no = j;

		  order_move_count++;	/* increase entry move count */

		  break;	/* exit for-loop */
		}

	      col2 = col2_next;	/* restore next link */
	    }
	}

      order_prev = order;	/* go ahead */
    }

  if (order_move_count > 0)
    {
      PT_NODE *match;

      /* now check for duplicate entries.  - If they match on ascending/descending, remove the second.  - If they do
       * not, generate an error. */
      for (order = node->info.query.order_by; order; order = order->next)
	{
	  while ((match = pt_find_order_value_in_list (parser, order->info.sort_spec.expr, order->next)))
	    {
	      if ((order->info.sort_spec.asc_or_desc != match->info.sort_spec.asc_or_desc)
		  || (pt_to_null_ordering (order) != pt_to_null_ordering (match)))
		{
		  error = MSGCAT_SEMANTIC_SORT_DIR_CONFLICT;
		  PT_ERRORmf (parser, match, MSGCAT_SET_PARSER_SEMANTIC, error, pt_short_print (parser, match));
		  goto exit_on_error;
		}
	      else
		{
		  order->next = pt_remove_from_list (parser, match, order->next);
		}
	    }			/* while */
	}			/* for (order = ...) */
    }

  if (const_order_count > 0)
    {				/* is reduced */
      /* the second phase, do check with reduced order by */
      if (need_merge_check)
	{
	  if (pt_sort_spec_cover (node->info.query.q.select.group_by, node->info.query.order_by))
	    {
	      if (qo_reduce_order_by_for (parser, node) != NO_ERROR)
		{
		  goto exit_on_error;
		}

	      if (node->info.query.orderby_for == NULL && !node->info.query.q.select.connect_by)
		{
		  /* clear unnecessary node info */
		  parser_free_tree (parser, node->info.query.order_by);
		  node->info.query.order_by = NULL;
		}

	      need_merge_check = false;	/* clear */
	    }
	}
      else
	{
	  if (node->info.query.order_by == NULL)
	    {
	      /* move orderby_num() to inst_num() */
	      if (node->info.query.orderby_for)
		{
		  PT_NODE *ord_num, *ins_num;

		  ord_num = NULL;
		  ins_num = NULL;

		  /* generate orderby_num(), inst_num() */
		  if (!(ord_num = parser_new_node (parser, PT_EXPR)) || !(ins_num = parser_new_node (parser, PT_EXPR)))
		    {
		      if (ord_num)
			{
			  parser_free_tree (parser, ord_num);
			}
		      PT_ERRORm (parser, node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OUT_OF_MEMORY);
		      goto exit_on_error;
		    }

		  ord_num->type_enum = PT_TYPE_BIGINT;
		  ord_num->info.expr.op = PT_ORDERBY_NUM;
		  PT_EXPR_INFO_SET_FLAG (ord_num, PT_EXPR_INFO_ORDERBYNUM_C);

		  ins_num->type_enum = PT_TYPE_BIGINT;
		  ins_num->info.expr.op = PT_INST_NUM;
		  PT_EXPR_INFO_SET_FLAG (ins_num, PT_EXPR_INFO_INSTNUM_C);

		  /* replace orderby_num() to inst_num() */
		  node->info.query.orderby_for =
		    pt_lambda_with_arg (parser, node->info.query.orderby_for, ord_num, ins_num,
					false /* loc_check: DEFAULT */ ,
					2 /* type: don't walk into subquery */ ,
					false /* dont_replace: DEFAULT */ );

		  node->info.query.q.select.list =
		    pt_lambda_with_arg (parser, node->info.query.q.select.list, ord_num, ins_num,
					false /* loc_check: DEFAULT */ ,
					2 /* type: don't walk into subquery */ ,
					false /* dont_replace: DEFAULT */ );

		  node->info.query.q.select.where =
		    parser_append_node (node->info.query.orderby_for, node->info.query.q.select.where);

		  node->info.query.orderby_for = NULL;

		  parser_free_tree (parser, ord_num);
		  parser_free_tree (parser, ins_num);
		}
	      else if (has_orderbynum_with_groupby == true)
		{
		  PT_NODE *ord_num, *grp_num;

		  ord_num = NULL;
		  grp_num = NULL;

		  /* generate orderby_num(), groupby_num() */
		  if (!(ord_num = parser_new_node (parser, PT_EXPR))
		      || !(grp_num = parser_new_node (parser, PT_FUNCTION)))
		    {
		      if (ord_num)
			{
			  parser_free_tree (parser, ord_num);
			}
		      PT_ERRORm (parser, node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OUT_OF_MEMORY);
		      goto exit_on_error;
		    }

		  ord_num->type_enum = PT_TYPE_BIGINT;
		  ord_num->info.expr.op = PT_ORDERBY_NUM;
		  PT_EXPR_INFO_SET_FLAG (ord_num, PT_EXPR_INFO_ORDERBYNUM_C);

		  grp_num->type_enum = PT_TYPE_BIGINT;
		  grp_num->info.function.function_type = PT_GROUPBY_NUM;
		  grp_num->info.function.arg_list = NULL;
		  grp_num->info.function.all_or_distinct = PT_ALL;

		  /* replace orderby_num() to groupby_num() */
		  node->info.query.q.select.list = pt_lambda_with_arg (parser, node->info.query.q.select.list, ord_num,
								       grp_num, false /* loc_check: DEFAULT */ ,
								       2 /* type: don't walk into subquery */ ,
								       false /* dont_replace: DEFAULT */ );

		  parser_free_tree (parser, ord_num);
		  parser_free_tree (parser, grp_num);
		}
	      else
		{
		  /* for select-list */
		  PT_NODE *ord_num, *ins_num;

		  ord_num = NULL;
		  ins_num = NULL;

		  /* generate orderby_num(), inst_num() */
		  if (!(ord_num = parser_new_node (parser, PT_EXPR)) || !(ins_num = parser_new_node (parser, PT_EXPR)))
		    {
		      if (ord_num)
			{
			  parser_free_tree (parser, ord_num);
			}
		      PT_ERRORm (parser, node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OUT_OF_MEMORY);
		      goto exit_on_error;
		    }

		  ord_num->type_enum = PT_TYPE_BIGINT;
		  ord_num->info.expr.op = PT_ORDERBY_NUM;
		  PT_EXPR_INFO_SET_FLAG (ord_num, PT_EXPR_INFO_ORDERBYNUM_C);

		  ins_num->type_enum = PT_TYPE_BIGINT;
		  ins_num->info.expr.op = PT_INST_NUM;
		  PT_EXPR_INFO_SET_FLAG (ins_num, PT_EXPR_INFO_INSTNUM_C);

		  node->info.query.q.select.list =
		    pt_lambda_with_arg (parser, node->info.query.q.select.list, ord_num, ins_num,
					false /* loc_check: DEFAULT */ ,
					2 /* type: don't walk into subquery */ ,
					false /* dont_replace: DEFAULT */ );

		  parser_free_tree (parser, ord_num);
		  parser_free_tree (parser, ins_num);
		}
	    }
	}
    }

exit_on_end:

  return error;

exit_on_error:

  if (error == NO_ERROR)
    {
      /* missing compiler error list */
      error = ER_GENERIC_ERROR;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
    }
  goto exit_on_end;
}

/*
 * qo_get_name_cnt_by_spec () - looks for a name with a matching id
 *   return: PT_NODE *
 *   parser(in): parser environment
 *   spec(in):
 *   arg(in): info of spec and result
 *   continue_walk(in):
 */
static PT_NODE *
qo_get_name_cnt_by_spec (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
{
  SPEC_CNT_INFO *info = (SPEC_CNT_INFO *) arg;

  if (node->node_type == PT_NAME)
    {
      if (node->info.name.spec_id == info->spec->info.spec.id)
	{
	  info->my_spec_cnt++;
	  info->my_spec_node = node;
	}
      else
	{
	  info->other_spec_cnt++;
	}
    }

  if (info->my_spec_cnt >= 2 || (info->my_spec_cnt == 1 && info->other_spec_cnt >= 1))
    {
      *continue_walk = PT_STOP_WALK;
    }
  return node;
}

/*
 * qo_get_name_cnt_keep_unique () - looks for a name with a matching id
 *   return: PT_NODE *
 *   parser(in): parser environment
 *   spec(in):
 *   arg(in): info of spec and result
 *   continue_walk(in):
 */
static PT_NODE *
qo_get_name_cnt_keep_unique (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
{
  SPEC_CNT_INFO *info = (SPEC_CNT_INFO *) arg;

  *continue_walk = PT_CONTINUE_WALK;

  if (node->node_type == PT_NAME)
    {
      if (node->info.name.spec_id == info->spec->info.spec.id)
	{
	  info->my_spec_cnt++;
	  info->my_spec_node = node;
	}
      else
	{
	  info->other_spec_cnt++;
	}
    }
  else if (node->node_type == PT_EXPR && pt_expr_keep_uniqueness (node))
    {
      /* keep going */
    }
  else if (node->node_type == PT_DATA_TYPE || node->node_type == PT_VALUE || node->node_type == PT_HOST_VAR)
    {
      /* don't walk into node */
      *continue_walk = PT_LIST_WALK;
    }
  else
    {
      /* impossible case */
      info->my_spec_cnt = 2;
      *continue_walk = PT_STOP_WALK;
    }

  if (info->my_spec_cnt >= 2 || (info->my_spec_cnt == 1 && info->other_spec_cnt >= 1))
    {
      *continue_walk = PT_STOP_WALK;
    }
  return node;
}

/*
 * qo_get_name_cnt_by_spec_no_on () - looks for a name with a matching id
 *   return: PT_NODE *
 *   parser(in): parser environment
 *   spec(in):
 *   arg(in): info of spec and result
 *   continue_walk(in):
 */
static PT_NODE *
qo_get_name_cnt_by_spec_no_on (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
{
  SPEC_CNT_INFO *info = (SPEC_CNT_INFO *) arg;
  *continue_walk = PT_CONTINUE_WALK;

  /* ignore on_cond of my spec */
  if ((pt_is_expr_node (node) && node->info.expr.location == info->spec->info.spec.location)
      || (pt_is_value_node (node) && node->info.value.location == info->spec->info.spec.location)
      || (node->node_type == PT_SPEC && node->info.spec.id == info->spec->info.spec.id))
    {
      *continue_walk = PT_LIST_WALK;
      return node;
    }

  if (node->node_type == PT_NAME)
    {
      if (node->info.name.spec_id == info->spec->info.spec.id)
	{
	  info->my_spec_cnt++;
	  info->my_spec_node = node;
	}
      else
	{
	  info->other_spec_cnt++;
	}
    }

  if (info->my_spec_cnt >= 1)
    {
      *continue_walk = PT_STOP_WALK;
    }
  return node;
}

/*
 * qo_check_keeps_distinct () - checks if each expression keeps distinct values distinct
 *   return: PT_NODE *
 *   parser(in): parser environment
 *   node(in):
 *   arg(in/out): bool, set to false when an expression can merge distinct values
 *   continue_walk(in/out):
 */
static PT_NODE *
qo_check_keeps_distinct (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
{
  bool *keeps_distinct = (bool *) arg;

  if (node->node_type == PT_EXPR && !pt_expr_keeps_distinct_values (node))
    {
      *keeps_distinct = false;
      *continue_walk = PT_STOP_WALK;
    }
  return node;
}

/*
 * qo_get_host_var_bound_type () - gets the type a host variable is compared as
 *   return: PT_TYPE_NONE if unknown
 *   parser(in): parser environment
 *   host_var(in):
 */
static PT_TYPE_ENUM
qo_get_host_var_bound_type (PARSER_CONTEXT * parser, const PT_NODE * host_var)
{
  int index = host_var->info.host_var.index;

  /* the value is converted to the expected domain before it is sent */
  if (host_var->expected_domain != NULL)
    {
      return pt_db_to_type_enum (TP_DOMAIN_TYPE (host_var->expected_domain));
    }

  /* a value-typed position is compiled for the type of its value, and a value of another type gets another plan.
   * without a value no plan is made */
  if (host_var->info.host_var.var_type == PT_HOST_IN && parser->host_variables != NULL && index >= 0
      && index < parser->host_var_count && !DB_IS_NULL (&parser->host_variables[index]))
    {
      return pt_db_to_type_enum (DB_VALUE_DOMAIN_TYPE (&parser->host_variables[index]));
    }

  return PT_TYPE_NONE;
}

/*
 * qo_eq_keeps_distinct () - checks if the equality keeps distinct values of the column side distinct
 *   return: bool
 *   parser(in): parser environment
 *   col_side(in): operand with the column of the outer joined spec
 *   other_side(in): the other operand
 *
 * Note: a comparison coerces both operands to a common type without an explicit cast,
 *       e.g. BIGINT = DOUBLE compares as DOUBLE and merges BIGINT values above 2^53.
 */
static bool
qo_eq_keeps_distinct (PARSER_CONTEXT * parser, PT_NODE * col_side, const PT_NODE * other_side)
{
  PT_TYPE_ENUM col_type = col_side->type_enum;
  PT_TYPE_ENUM other_type = other_side->type_enum;
  bool keeps_distinct = true;

  /* expressions between the column and the comparison */
  parser_walk_tree (parser, col_side, qo_check_keeps_distinct, &keeps_distinct, NULL, NULL);
  if (!keeps_distinct)
    {
      return false;
    }

  if (PT_IS_NULL_NODE (other_side))
    {
      return true;
    }

  if (other_side->node_type == PT_HOST_VAR && other_type == PT_TYPE_MAYBE)
    {
      other_type = qo_get_host_var_bound_type (parser, other_side);
      if (PT_IS_CHAR_STRING_TYPE (col_type) && PT_IS_CHAR_STRING_TYPE (other_type))
	{
	  /* a string value takes the collation of the column */
	  return true;
	}
      if ((PT_IS_DISCRETE_NUMBER_TYPE (col_type) || col_type == PT_TYPE_NUMERIC)
	  && (PT_IS_DISCRETE_NUMBER_TYPE (other_type) || other_type == PT_TYPE_NUMERIC))
	{
	  return true;
	}
      return (col_type == other_type
	      || ((col_type == PT_TYPE_SMALLINT || col_type == PT_TYPE_INTEGER) && other_type == PT_TYPE_DOUBLE));
    }

  /* exact numbers compare exactly whatever the precision and scale */
  if ((PT_IS_DISCRETE_NUMBER_TYPE (col_type) || col_type == PT_TYPE_NUMERIC)
      && (PT_IS_DISCRETE_NUMBER_TYPE (other_type) || other_type == PT_TYPE_NUMERIC))
    {
      return true;
    }

  return pt_keeps_distinct_values (col_side, other_side, false);
}

/*
 * qo_collect_name_with_eq_const () - collect name node with equal OP and constant value
 *   return: node_list
 *   parser(in):
 *   on_cond(in):
 *   spec(in):
 *
 * Note:
 * collect name nodes with following conditions
 *                arg1            op             arg2
 *     only have one of my spec   '='    not have my spec node
 * e.g.)      col(my spec)         =     2 (constant)
 *            a.col(my spec)       =     b.col
 *            a.col(my spec)       =     b.col + c.col
 */
static PT_NODE *
qo_collect_name_with_eq_const (PARSER_CONTEXT * parser, PT_NODE * on_cond, PT_NODE * spec)
{
  PT_NODE *pred, *arg1, *arg2;
  PT_NODE *point, *s_name, *point_list = NULL;
  SPEC_CNT_INFO info1, info2;

  info1.spec = spec;
  info2.spec = spec;

  pred = on_cond;
  while (pred != NULL)
    {
      if (pred->or_next != NULL)
	{
	  pred = pred->next;
	  continue;
	}
      if (!PT_IS_EXPR_NODE_WITH_OPERATOR (pred, PT_EQ))
	{
	  pred = pred->next;
	  continue;
	}
      /* check on_cond predicate */
      if (spec->info.spec.location != pred->info.expr.location)
	{
	  pred = pred->next;
	  continue;
	}

      arg1 = pred->info.expr.arg1;
      arg2 = pred->info.expr.arg2;
      /* find name with spec id */
      info1.my_spec_cnt = 0;
      info1.other_spec_cnt = 0;
      info2.my_spec_cnt = 0;
      info2.other_spec_cnt = 0;
      parser_walk_tree (parser, arg1, qo_get_name_cnt_keep_unique, &info1, NULL, NULL);
      parser_walk_tree (parser, arg2, qo_get_name_cnt_keep_unique, &info2, NULL, NULL);

      /* col1(my spec) = const */
      if (info1.my_spec_cnt == 1 && info1.other_spec_cnt == 0)
	{
	  /* const */
	  if (info2.my_spec_cnt == 0 && qo_eq_keeps_distinct (parser, arg1, arg2))
	    {
	      point_list = parser_append_node (pt_point (parser, info1.my_spec_node), point_list);
	    }
	}
      /* const = col(my spec) */
      else if (info2.my_spec_cnt == 1 && info2.other_spec_cnt == 0)
	{
	  /* const */
	  if (info1.my_spec_cnt == 0 && qo_eq_keeps_distinct (parser, arg2, arg1))
	    {
	      point_list = parser_append_node (pt_point (parser, info2.my_spec_node), point_list);
	    }
	}
      pred = pred->next;
    }
  return point_list;
}

/*
 * qo_modify_location () -
 *   return:
 *   parser(in):
 *   node(in):
 *   arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
qo_modify_location (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
{
  RESET_LOCATION_INFO *infop = (RESET_LOCATION_INFO *) arg;

  /* PT_LIST_WALK set on a subquery below is still in force when the walk reaches the next conjunct */
  *continue_walk = PT_CONTINUE_WALK;

  if (PT_IS_QUERY (node))
    {
      *continue_walk = PT_LIST_WALK;
      return node;
    }

  if (node->node_type == PT_EXPR && node->info.expr.location == infop->start)
    {
      node->info.expr.location = infop->end;
    }

  if (node->node_type == PT_NAME && node->info.name.location == infop->start)
    {
      node->info.name.location = infop->end;
    }

  if (node->node_type == PT_VALUE && node->info.value.location == infop->start)
    {
      node->info.value.location = infop->end;
    }

  return node;
}

/*
 * qo_reset_spec_location () - reset location of spec
 *   return: node_list
 *   parser(in):
 *   on_cond(in):
 *   spec(in):
 *
 * Note:
 */
static void
qo_reset_spec_location (PARSER_CONTEXT * parser, PT_NODE * spec, PT_NODE * query)
{
  short curr_loc, after_loc;
  PT_NODE *where;
  RESET_LOCATION_INFO locate_info;

  while (spec != NULL)
    {
      curr_loc = spec->info.spec.location;
      after_loc = curr_loc - 1;

      if (curr_loc <= 0 || after_loc < 0)
	{
	  spec = spec->next;
	  continue;
	}

      /* reset location of spec */
      spec->info.spec.location = after_loc;

      /* reset location of predicate */
      locate_info.start = curr_loc;
      locate_info.end = after_loc;
      where = query->info.query.q.select.where;
      (void) parser_walk_tree (parser, where, qo_modify_location, &locate_info, NULL, NULL);

      spec = spec->next;
    }
}

/*
 * qo_insert_spec_location () - make room at 'spec' for a spec the caller is about to link in front of it: move
 *      the locations of 'spec' and every spec behind it up by one, with the conjuncts stamped with them; the
 *      counterpart of qo_reset_spec_location ()
 *   return:
 *   parser(in):
 *   spec(in): the first spec that moves, i.e. the one the newcomer goes in front of
 *   query(in): the SELECT whose WHERE holds the conjuncts stamped with those locations
 *   on_conds(in): conjuncts stamped the same way but not yet appended to that WHERE
 *
 * Note: the specs behind 'spec' move first.  In list order a location would be carried up twice, once with
 *   its own spec and once more with the next one.
 */
void
qo_insert_spec_location (PARSER_CONTEXT * parser, PT_NODE * spec, PT_NODE * query, PT_NODE * on_conds)
{
  short curr_loc, after_loc;
  PT_NODE *where;
  RESET_LOCATION_INFO locate_info;

  if (spec == NULL)
    {
      return;
    }

  qo_insert_spec_location (parser, spec->next, query, on_conds);

  curr_loc = spec->info.spec.location;
  after_loc = curr_loc + 1;

  if (curr_loc < 0)
    {
      /* never numbered: a derived table the rewriter appended */
      return;
    }

  /* reset location of spec */
  spec->info.spec.location = after_loc;

  /* reset location of predicate */
  locate_info.start = curr_loc;
  locate_info.end = after_loc;
  where = query->info.query.q.select.where;
  (void) parser_walk_tree (parser, where, qo_modify_location, &locate_info, NULL, NULL);
  (void) parser_walk_tree (parser, on_conds, qo_modify_location, &locate_info, NULL, NULL);
}

/*
 * qo_reduce_outer_joined_tbls () - reduce outer joined tables with unique join predicates
 *   return:
 *   parser(in):
 *   spec(in):
 *   query(in):
 *
 * Note:
 * 	examples:
 *  	          select count(*)
 *                  from tbl1 a left outer join tbl2 b on a.pk = b.pk <== outer joined table with unique join columns
 *                ==>
 *                select count(*)
 *                  from tbl1 a
 */
static PT_NODE *
qo_reduce_outer_joined_tbls (PARSER_CONTEXT * parser, PT_NODE * spec, PT_NODE * query)
{
  MOP cls;
  SM_CLASS_CONSTRAINT *consp;
  SM_ATTRIBUTE *attrp;
  PT_NODE *point_list = NULL, *point, *where, *col, *tmp_spec, *prev_spec, *pred, *prev_pred, *next_pred;
  PT_NODE *next_spec;
  SPEC_CNT_INFO info;
  bool all_unique_col_match = false;
  int i;

  /* check left outer join */
  /* TO_DO : for right outer join */
  if (spec == NULL || spec->info.spec.join_type != PT_JOIN_LEFT_OUTER)
    {
      return query;
    }

  /* check query */
  if (!PT_IS_SELECT (query))
    {
      return query;
    }

  /* check Inheritance of spec */
  if (spec->info.spec.only_all == PT_ALL)
    {
      return query;
    }

  /* only a class spec has unique constraints; TABLE (set) and JSON_TABLE specs have no class */
  if (!PT_SPEC_IS_ENTITY (spec))
    {
      return query;
    }

  /* check no_eliminate_join sql hint */
  if (query->info.query.q.select.hint & PT_HINT_NO_ELIMINATE_JOIN)
    {
      return query;
    }

  /* check referenced columns except on_cond */
  info.spec = spec;
  info.my_spec_cnt = 0;
  info.other_spec_cnt = 0;
  parser_walk_tree (parser, query, qo_get_name_cnt_by_spec_no_on, &info, NULL, NULL);
  if (info.my_spec_cnt >= 1)
    {
      /* Except for on_cond, the referenced columns exist. */
      return query;
    }

  where = query->info.query.q.select.where;
  /* get columns with equal op and constant in on_cond */
  point_list = qo_collect_name_with_eq_const (parser, where, spec);
  if (point_list == NULL)
    {
      return query;
    }

  /* get class info */
  cls = sm_find_class (spec->info.spec.flat_entity_list->info.name.original);
  if (cls == NULL)
    {
      goto end;
    }

  /* get index info of spec */
  consp = sm_class_constraints (cls);
  while (consp != NULL)
    {
      if (!SM_IS_CONSTRAINT_UNIQUE_FAMILY (consp->type))
	{
	  consp = consp->next;
	  continue;
	}

      /* check columns on this constraint */
      for (i = 0; consp->attributes[i]; i++)
	{
	  attrp = consp->attributes[i];
	  point = point_list;
	  col = point;
	  CAST_POINTER_TO_NODE (col);
	  while (col != NULL)
	    {
	      if (intl_identifier_casecmp (col->info.name.original, attrp->header.name) == 0)
		{
		  break;
		}
	      point = point->next;
	      col = point;
	      CAST_POINTER_TO_NODE (col);
	    }
	  /* not find */
	  if (col == NULL)
	    {
	      break;
	    }
	}
      /* matche all columns of the unique index */
      if (consp->attributes[i] == NULL)
	{
	  all_unique_col_match = true;
	  break;
	}

      consp = consp->next;
    }

  if (all_unique_col_match)
    {
      /* remove unnecessary table spec */
      /* find previous spec */
      prev_spec = NULL;
      tmp_spec = query->info.query.q.select.from;
      while (tmp_spec && tmp_spec->info.spec.id != spec->info.spec.id)
	{
	  prev_spec = tmp_spec;
	  tmp_spec = tmp_spec->next;
	}
      if (tmp_spec == NULL || prev_spec == NULL)
	{
	  goto end;
	}

      /* cut off unnecessary spec */
      prev_spec->next = next_spec = spec->next;
      spec->next = NULL;

      /* remove on_cond predicate */
      prev_pred = NULL;
      pred = query->info.query.q.select.where;
      while (pred != NULL)
	{
	  next_pred = pred->next;
	  if ((pt_is_expr_node (pred) && pred->info.expr.location == spec->info.spec.location)
	      || (pt_is_value_node (pred) && pred->info.value.location == spec->info.spec.location))
	    {
	      if (prev_pred == NULL)
		{
		  /* first time */
		  query->info.query.q.select.where = next_pred;
		  pred->next = NULL;
		  parser_free_tree (parser, pred);
		}
	      else
		{
		  prev_pred->next = next_pred;
		  pred->next = NULL;
		  parser_free_tree (parser, pred);
		}
	    }
	  else
	    {
	      prev_pred = pred;
	    }
	  pred = next_pred;
	}

      /* reset location */
      qo_reset_spec_location (parser, next_spec, query);

      /* free spec */
      parser_free_tree (parser, spec);
    }

end:
  if (point_list != NULL)
    {
      parser_free_tree (parser, point_list);
    }
  return query;
}

/*
 * qo_reduce_joined_tbls_ref_by_fk () - Removes a table with a primary key from a join
 *                                                        with a table with a foreign key referencing it.
 *   return: void
 *   parser(in): parser context
 *   query(in): query to check
 *
 * Note: A table with a primary key is removed from a join with a table with a foreign key
 *       that references the primary key. There must be no references to the table to be removed
 *       other than join predicates. This is because the relationship between a primary key and a foreign key
 *       replaces a data filter of a join. If a table with a primary key is removed, a predicate for IS NOT NULL
 *       may be added, as it cannot filter on NULL.
 *
 *    e.g. drop if exists child, parent;
 *         create table parent (c1 int primary key, c2 int);
 *         create table child (c1 int, parent_c1 int references parent (c1), c2 int);
 *
 *         select c.* from child c inner join parent p on p.c1 = c.parent_c1 where c.c2 = 1;
 *         select c.* from child c, parent p where c.parent_c1 = p.c1 and c.c2 = 1;
 *           -> select c.* from child c where c.c2 = 1 and c.parent_c1 is not null;
 *
 *         select c.* from child c inner join parent p on p.c1 = c.parent_c1 where p.c2 = 1;
 *         select c.* from child c, parent p where c.parent_c1 = p.c1 and p.c2 = 1;
 *           -> do not change.
 */
static void
qo_reduce_joined_tbls_ref_by_fk (PARSER_CONTEXT * parser, PT_NODE * query)
{
  QO_REDUCE_REFERENCE_INFO reduce_reference_info;
  PT_NODE *curr_pk_spec = NULL, *prev_pk_spec = NULL, *next_pk_spec = NULL;
  PT_NODE *curr_fk_spec = NULL;
  bool has_reduce = false;

  assert (parser != NULL && query != NULL);

  memset (&reduce_reference_info, 0, sizeof (QO_REDUCE_REFERENCE_INFO));

  if (query->node_type != PT_SELECT)
    {
      return;
    }

  if (query->info.query.q.select.hint & PT_HINT_NO_ELIMINATE_JOIN)
    {
      return;
    }

  if (query->info.query.q.select.where == NULL)
    {
      return;
    }

  do
    {
      has_reduce = false;

      for (prev_pk_spec = NULL, curr_pk_spec = query->info.query.q.select.from; curr_pk_spec != NULL;
	   prev_pk_spec = curr_pk_spec, curr_pk_spec = curr_pk_spec->next)
	{
	  if (qo_is_exclude_spec (reduce_reference_info.exclude_pk_spec_point_list, curr_pk_spec))
	    {
	      continue;		/* curr_pk_spec->next */
	    }

	  reduce_reference_info.pk_spec = curr_pk_spec;

	  if (!qo_check_pk_ref_by_fk_in_parent_spec (parser, query, &reduce_reference_info))
	    {
	      if (er_has_error ())
		{
		  goto exit_on_fail_with_cleanup;
		}

	      continue;		/* curr_pk_spec->next */
	    }

	  for (curr_fk_spec = query->info.query.q.select.from; curr_fk_spec != NULL; curr_fk_spec = curr_fk_spec->next)
	    {
	      if (curr_pk_spec == curr_fk_spec)
		{
		  continue;	/* curr_fk_spec->next */
		}

	      if (qo_is_exclude_spec (reduce_reference_info.exclude_fk_spec_point_list, curr_fk_spec))
		{
		  continue;	/* curr_fk_spec->next */
		}

	      reduce_reference_info.fk_spec = curr_fk_spec;

	      if (qo_check_fks_ref_pk_in_child_spec (parser, query, &reduce_reference_info))
		{
		  break;
		}
	      else
		{
		  if (er_has_error ())
		    {
		      goto exit_on_fail_with_cleanup;
		    }

		  continue;	/* curr_fk_spec->next */
		}
	    }

	  if (curr_fk_spec == NULL)
	    {
	      /* not found */
	      continue;		/* curr_pk_spec->next */
	    }

	  /* Do not use next_pk_spec in the for-loop because curr_pk_spec may change. */
	  next_pk_spec = curr_pk_spec->next;

	  /* safe guard */
	  if (prev_pk_spec == NULL
	      && (next_pk_spec->info.spec.join_type == PT_JOIN_LEFT_OUTER
		  || next_pk_spec->info.spec.join_type == PT_JOIN_RIGHT_OUTER
		  || next_pk_spec->info.spec.join_type == PT_JOIN_FULL_OUTER))
	    {
	      continue;		/* give up */
	    }

	  qo_reduce_predicate_for_parent_spec (parser, query, &reduce_reference_info);

	  assert (reduce_reference_info.join_pred_point_list == NULL);
	  assert (reduce_reference_info.parent_pred_point_list == NULL);
	  assert (reduce_reference_info.append_not_null_pred_list == NULL);

	  if (prev_pk_spec != NULL)
	    {
	      prev_pk_spec->next = next_pk_spec;
	    }
	  else
	    {
	      query->info.query.q.select.from = next_pk_spec;

	      next_pk_spec->info.spec.join_type = PT_JOIN_NONE;
	      next_pk_spec->info.spec.natural = false;
	    }

	  /* reset location */
	  qo_reset_spec_location (parser, next_pk_spec, query);

	  parser_free_node (parser, curr_pk_spec);
	  curr_pk_spec = next_pk_spec;

	  has_reduce = true;

	  if (curr_pk_spec == NULL)
	    {
	      /* first again */
	      break;
	    }
	}
    }
  while (has_reduce);

  /* end */

exit_on_fail_with_cleanup:
  if (reduce_reference_info.exclude_pk_spec_point_list != NULL)
    {
      parser_free_tree (parser, reduce_reference_info.exclude_pk_spec_point_list);
      reduce_reference_info.exclude_pk_spec_point_list = NULL;
    }

  if (reduce_reference_info.exclude_fk_spec_point_list != NULL)
    {
      parser_free_tree (parser, reduce_reference_info.exclude_fk_spec_point_list);
      reduce_reference_info.exclude_fk_spec_point_list = NULL;
    }

  if (reduce_reference_info.join_pred_point_list != NULL)
    {
      parser_free_tree (parser, reduce_reference_info.join_pred_point_list);
      reduce_reference_info.join_pred_point_list = NULL;
    }

  if (reduce_reference_info.parent_pred_point_list != NULL)
    {
      parser_free_tree (parser, reduce_reference_info.parent_pred_point_list);
      reduce_reference_info.parent_pred_point_list = NULL;
    }

  if (reduce_reference_info.append_not_null_pred_list != NULL)
    {
      parser_free_tree (parser, reduce_reference_info.append_not_null_pred_list);
      reduce_reference_info.append_not_null_pred_list = NULL;
    }

  return;
}

/*
 * qo_is_exclude_spec () - Whether the given spec exists in the list of specs to exclude.
 *   return: bool
 *   exclude_spec_point_list(in): list of specs to exclude
 *   spec(in): spec to find
 *
 * Note: In the qo_reduce_joined_tbls_ref_by_fk() function,
 *       if the checked spec does not need to be checked again, it is added to the list of specs to exclude.
 */
static bool
qo_is_exclude_spec (PT_NODE * exclude_spec_point_list, PT_NODE * spec)
{
  PT_NODE *exclude_spec_point = NULL, *exclude_spec = NULL;

  for (exclude_spec_point = exclude_spec_point_list; exclude_spec_point != NULL;
       exclude_spec_point = exclude_spec_point->next)
    {
      exclude_spec = exclude_spec_point;
      CAST_POINTER_TO_NODE (exclude_spec);

      if (exclude_spec == spec)
	{
	  return true;
	}
    }

  return false;
}

/*
 * qo_check_pk_ref_by_fk_in_parent_spec () - Whether the given spec has a primary key
 *                                                                    referenced by a foreign key.
 *   return: bool
 *   parser(in): parser context
 *   query(in): query to check
 *   reduce_reference_info(in/out): Information needed to check
 *
 * Note: If the given spec can be removed, set the memory object pointer of the given spec and a constraint pointer
 *       of a primary key to reduce_reference_info. And the predicates of the given spec are added
 *       to the join_pred_point_list.
 * 
 *       In the following cases, add to the exclude_pk_spec_point_list.
 *         1. Access to hierarchical tables.
 *         2. Not an inner join or natural join.
 *         3. CTEs or derived tables.
 *         4. No primary key.
 *         5. No foreign key referencing the primary key.
 *         6. Non-join predicates exist.
 */
static bool
qo_check_pk_ref_by_fk_in_parent_spec (PARSER_CONTEXT * parser, PT_NODE * query,
				      QO_REDUCE_REFERENCE_INFO * reduce_reference_info)
{
  PT_NODE *curr_pk_spec = NULL;
  MOP curr_pk_mop = NULL;
  SM_CLASS_CONSTRAINT *curr_pk_cons = NULL;
  PT_NODE *join_pred_point_list = NULL;
  PT_NODE *parent_pred_point_list = NULL;
  PT_NODE *curr_pred_point = NULL;
  PT_NODE *curr_pred = NULL, *next_pred = NULL;
  int cons_attr_cnt;
  unsigned int cons_attr_flag;
  int i;

  assert (parser != NULL && query != NULL);
  assert (reduce_reference_info != NULL);
  assert (reduce_reference_info->pk_spec != NULL);

  reduce_reference_info->pk_mop = NULL;
  reduce_reference_info->pk_cons = NULL;

  if (reduce_reference_info->join_pred_point_list != NULL)
    {
      parser_free_tree (parser, reduce_reference_info->join_pred_point_list);
      reduce_reference_info->join_pred_point_list = NULL;
    }

  if (reduce_reference_info->parent_pred_point_list != NULL)
    {
      parser_free_tree (parser, reduce_reference_info->parent_pred_point_list);
      reduce_reference_info->parent_pred_point_list = NULL;
    }

  curr_pk_spec = reduce_reference_info->pk_spec;
  assert (PT_NODE_IS_SPEC (curr_pk_spec));

  /* PT_ALL is not supported. */
  if (PT_SPEC_IS_ALL (curr_pk_spec))
    {
      goto exit_on_fail_with_exclude;
    }

  switch (PT_SPEC_JOIN_TYPE (curr_pk_spec))
    {
    case PT_JOIN_NONE:
    case PT_JOIN_INNER:
    case PT_JOIN_NATURAL:
      break;

    case PT_JOIN_CROSS:
    case PT_JOIN_LEFT_OUTER:
    case PT_JOIN_RIGHT_OUTER:
    case PT_JOIN_FULL_OUTER:
    case PT_JOIN_UNION:
    default:
      goto exit_on_fail_with_exclude;
    }

  PT_SPEC_GET_DB_OBJECT (curr_pk_spec, curr_pk_mop);
  if (curr_pk_mop == NULL)
    {
      /* CTEs and derived tables are excluded. */
      goto exit_on_fail_with_exclude;
    }

  for (curr_pk_cons = sm_class_constraints (curr_pk_mop); curr_pk_cons != NULL; curr_pk_cons = curr_pk_cons->next)
    {
      if (curr_pk_cons->type == SM_CONSTRAINT_PRIMARY_KEY)
	{
	  /* found */
	  break;
	}
    }

  if (curr_pk_cons == NULL || curr_pk_cons->fk_info == NULL)
    {
      /* No primary key or no foreign key referencing the primary key. */
      goto exit_on_fail_with_exclude;
    }

  /* There must be no non-join predicates. */
  for (curr_pred = query->info.query.q.select.where; curr_pred != NULL; curr_pred = curr_pred->next)
    {
      SPEC_CNT_INFO info;

      memset (&info, 0, sizeof (SPEC_CNT_INFO));
      info.spec = curr_pk_spec;

      next_pred = curr_pred->next;
      curr_pred->next = NULL;

      parser_walk_tree (parser, curr_pred, qo_get_name_cnt_by_spec, &info, NULL, NULL);

      curr_pred->next = next_pred;

      if (info.my_spec_cnt >= 1)
	{
	  if (curr_pred->or_next == NULL && PT_NODE_IS_EXPR (curr_pred) && PT_EXPR_OP (curr_pred) == PT_EQ)
	    {
	      if (pt_is_attr (PT_EXPR_ARG1 (curr_pred)) && pt_is_attr (PT_EXPR_ARG2 (curr_pred)))
		{
		  join_pred_point_list = parser_append_node (pt_point (parser, curr_pred), join_pred_point_list);
		  continue;
		}

	      /* Some predicates are non-join predicates, but can be reduced by predicate fulfillment.
	       * 
	       *   e.g. drop table if exists child, parent;
	       *        reate table parent (c1 int, c2 int, primary key (c1, c2));
	       *        create table child (c1 int, c2 int);
	       *        alter table child add constraint foreign key (c1, c2) references parent (c1, c2);
	       *
	       *        select c.* from child c, parent p where c.c1 = p.c1 and c.c2 = p.c2 and p.c1 = 1;
	       * 
	       *        -- rewritten query
	       *        select c.* from child c, parent p where c.c1 = p.c1 and c.c2 = p.c2 and p.c1 = 1 and c.c1 = 1;
	       * 
	       *        -- execute query
	       *        select c.* from child c where c.c1 = 1;
	       *
	       * 'p.c1 = 1' is a reference to the parent, but can be reduced if 'c.c1 = 1' exists.
	       * In the rewrite query, since 'c.c1 = 1' is added by predicate fulfillment, eliminate join is possible
	       * even if 'p.c1 = 1' exists.
	       */
	      if ((pt_is_attr (PT_EXPR_ARG1 (curr_pred)) && qo_is_reduceable_const (PT_EXPR_ARG2 (curr_pred))) ||
		  (pt_is_attr (PT_EXPR_ARG2 (curr_pred)) && qo_is_reduceable_const (PT_EXPR_ARG1 (curr_pred))))
		{
		  parent_pred_point_list = parser_append_node (pt_point (parser, curr_pred), parent_pred_point_list);
		  continue;
		}

	      goto exit_on_fail_with_exclude;
	    }
	  else
	    {
	      /* Non-join predicates exist. */
	      goto exit_on_fail_with_exclude;
	    }
	}
    }

  if (join_pred_point_list == NULL)
    {
      /* There are no join predicates. */
      goto exit_on_fail_with_exclude;
    }

  /* We need to check if all the attributes of the constraint are used in predicated. */
  for (i = 0; curr_pk_cons->attributes[i] != NULL; i++);

  /* Set the bits of cons_attr_flag to 1 as many as the number of attributes in the constraint.
   * And if an attribute of the constraint is used in a predicate, it sets the bit of that index to 0.
   * After checking the predicates, if cons_attr_flag is 0, we know that all the attributes of the constraint
   * are used in the predicate. */
  cons_attr_cnt = i;
  cons_attr_flag = (1 << i) - 1;

  /* The columns of join predicates must be in the primary key. */
  for (curr_pred_point = join_pred_point_list; curr_pred_point != NULL; curr_pred_point = curr_pred_point->next)
    {
      SM_ATTRIBUTE *pk_cons_attr = NULL;
      PT_NODE *pk_pred_attr = NULL;
      PT_NODE *arg1 = NULL, *arg2 = NULL;
      int i = 0;

      curr_pred = curr_pred_point;
      CAST_POINTER_TO_NODE (curr_pred);

      assert (PT_NODE_IS_EXPR (curr_pred));
      assert (PT_EXPR_OP (curr_pred) == PT_EQ);
      assert (PT_NODE_IS_NAME (PT_EXPR_ARG1 (curr_pred)));
      assert (PT_NODE_IS_NAME (PT_EXPR_ARG2 (curr_pred)));

      arg1 = PT_EXPR_ARG1 (curr_pred);
      arg2 = PT_EXPR_ARG2 (curr_pred);

      if (PT_SPEC_ID (curr_pk_spec) == PT_NAME_SPEC_ID (arg1))
	{
	  pk_pred_attr = PT_EXPR_ARG1 (curr_pred);
	}
      else if (PT_SPEC_ID (curr_pk_spec) == PT_NAME_SPEC_ID (arg2))
	{
	  pk_pred_attr = PT_EXPR_ARG2 (curr_pred);
	}
      else
	{
	  /* Already checked before. */
	  assert (false);
	  goto exit_on_fail_with_exclude;
	}

      for (i = 0; curr_pk_cons->attributes[i] != NULL; i++)
	{
	  if (intl_identifier_casecmp (curr_pk_cons->attributes[i]->header.name, PT_NAME_ORIGINAL (pk_pred_attr)) == 0)
	    {
	      /* found */
	      pk_cons_attr = curr_pk_cons->attributes[i];

	      /* If an attribute of the constraint is used in a predicate, it sets the bit of that index to 0. */
	      cons_attr_flag &= ~(1 << i);

	      break;		/* curr_pred_point->next */
	    }
	}

      if (pk_cons_attr == NULL)
	{
	  /* not found */
	  goto exit_on_fail_with_cleanup;
	}
    }

  assert (curr_pred_point == NULL);

  /* If cons_attr_flag is non-zero, then all the attributes of the constraint are not used in the predicate. */
  if (cons_attr_flag != 0)
    {
      goto exit_on_fail_with_exclude;
    }

  {
    SPEC_CNT_INFO info;
    PT_NODE *backup_from = NULL;
    PT_NODE *backup_where = NULL;

    /* qo_get_name_cnt_by_spec_no_on does not check PT_EXPR in the select_list.
     * qo_get_name_cnt_by_spec increases my_spec_cnt too much if from exists.
     * So I check both.
     */

    /* STEP 1 */
    memset (&info, 0, sizeof (SPEC_CNT_INFO));
    info.spec = curr_pk_spec;

    backup_from = query->info.query.q.select.from;
    backup_where = query->info.query.q.select.where;
    query->info.query.q.select.from = NULL;
    query->info.query.q.select.where = NULL;

    parser_walk_tree (parser, query, qo_get_name_cnt_by_spec, &info, NULL, NULL);

    query->info.query.q.select.from = backup_from;
    query->info.query.q.select.where = backup_where;

    if (info.my_spec_cnt >= 1)
      {
	goto exit_on_fail_with_exclude;
      }

    /* STEP 2 */
    memset (&info, 0, sizeof (SPEC_CNT_INFO));
    info.spec = curr_pk_spec;

    parser_walk_tree (parser, query->info.query.q.select.from, qo_get_name_cnt_by_spec_no_on, &info, NULL, NULL);

    if (info.my_spec_cnt >= 1)
      {
	goto exit_on_fail_with_exclude;
      }
  }

  reduce_reference_info->pk_mop = curr_pk_mop;
  reduce_reference_info->pk_cons = curr_pk_cons;
  reduce_reference_info->join_pred_point_list = join_pred_point_list;
  reduce_reference_info->parent_pred_point_list = parent_pred_point_list;

  return true;

exit_on_fail_with_exclude:
  reduce_reference_info->exclude_pk_spec_point_list =
    parser_append_node (pt_point (parser, curr_pk_spec), reduce_reference_info->exclude_pk_spec_point_list);
  /* fallthrough */

exit_on_fail_with_cleanup:
  if (join_pred_point_list != NULL)
    {
      parser_free_tree (parser, join_pred_point_list);
      join_pred_point_list = NULL;
    }

  if (parent_pred_point_list != NULL)
    {
      parser_free_tree (parser, parent_pred_point_list);
      parent_pred_point_list = NULL;
    }

  return false;
}

/*
 * qo_check_fks_ref_pk_in_child_spec () - Whether the given spec has a foreign key
 *                                                                  referencing a primary key.
 *   return: bool
 *   parser(in): parser context
 *   query(in): query to check
 *   reduce_reference_info(in/out): Information needed to check
 *
 * Note: For each foreign key of a given spec, it is checked
 *       in the qo_check_fk_ref_pk_in_child_spec() function.
 * 
 *       In the following cases, add to the exclude_fk_spec_point_list.
 *         1. Access to hierarchical tables.
 *         2. Not an inner join or natural join.
 *         3. CTEs or derived tables.
 *         4. No foreign key.
 */
static bool
qo_check_fks_ref_pk_in_child_spec (PARSER_CONTEXT * parser, PT_NODE * query,
				   QO_REDUCE_REFERENCE_INFO * reduce_reference_info)
{
  PT_NODE *curr_fk_spec = NULL;
  MOP curr_fk_mop = NULL;
  SM_CLASS_CONSTRAINT *curr_fk_cons = NULL;

  assert (parser != NULL && query != NULL);
  assert (reduce_reference_info != NULL);
  assert (reduce_reference_info->pk_spec != NULL);
  assert (reduce_reference_info->pk_mop != NULL);
  assert (reduce_reference_info->pk_cons != NULL);
  assert (reduce_reference_info->fk_spec != NULL);
  assert (reduce_reference_info->join_pred_point_list != NULL);

  reduce_reference_info->fk_cons = NULL;

  if (reduce_reference_info->append_not_null_pred_list != NULL)
    {
      parser_free_tree (parser, reduce_reference_info->append_not_null_pred_list);
      reduce_reference_info->append_not_null_pred_list = NULL;
    }

  curr_fk_spec = reduce_reference_info->fk_spec;
  assert (PT_NODE_IS_SPEC (curr_fk_spec));

  /* PT_ALL is not supported. */
  if (PT_SPEC_IS_ALL (curr_fk_spec))
    {
      goto exit_on_fail_with_exclude;
    }

  switch (PT_SPEC_JOIN_TYPE (curr_fk_spec))
    {
    case PT_JOIN_NONE:
    case PT_JOIN_INNER:
    case PT_JOIN_NATURAL:
      break;

    case PT_JOIN_CROSS:
    case PT_JOIN_LEFT_OUTER:
    case PT_JOIN_RIGHT_OUTER:
    case PT_JOIN_FULL_OUTER:
    case PT_JOIN_UNION:
      [[fallthrough]];
    default:
      goto exit_on_fail_with_exclude;
    }

  PT_SPEC_GET_DB_OBJECT (curr_fk_spec, curr_fk_mop);
  if (curr_fk_mop == NULL)
    {
      /* CTEs and derived tables are excluded. */
      goto exit_on_fail_with_exclude;
    }

  for (curr_fk_cons = sm_class_constraints (curr_fk_mop); curr_fk_cons != NULL; curr_fk_cons = curr_fk_cons->next)
    {
      if (curr_fk_cons->type != SM_CONSTRAINT_FOREIGN_KEY)
	{
	  continue;
	}

      reduce_reference_info->fk_cons = curr_fk_cons;

      if (!qo_check_fk_ref_pk_in_child_spec (parser, query, reduce_reference_info))
	{
	  if (er_has_error ())
	    {
	      goto exit_on_fail;
	    }

	  continue;
	}

      if (reduce_reference_info->parent_pred_point_list == NULL)
	{
	  return true;
	}

      if (qo_check_reduce_predicate_for_parent_spec (parser, query, reduce_reference_info))
	{
	  return true;
	}
      else
	{
	  if (er_has_error ())
	    {
	      goto exit_on_fail;
	    }

	  continue;
	}
    }

  assert (curr_fk_cons == NULL);

  if (reduce_reference_info->fk_cons == NULL)
    {
      /* No foreign key */
      goto exit_on_fail_with_exclude;
    }

  goto exit_on_fail;

exit_on_fail_with_exclude:
  reduce_reference_info->exclude_fk_spec_point_list =
    parser_append_node (pt_point (parser, curr_fk_spec), reduce_reference_info->exclude_fk_spec_point_list);

exit_on_fail:
  return false;
}

/*
 * qo_check_fk_ref_pk_in_child_spec () - Whether the given spec has a foreign key
 *                                                                 referencing a primary key.
 *   return: bool
 *   parser(in): parser context
 *   query(in): query to check
 *   reduce_reference_info(in/out): Information needed to check
 *
 * Note: Check whether the primary key of pk_spec in reduce_reference_info is referenced by the foreign key of fk_spec
 *       in reduce_reference_info. And check whether the column used in the join predicates exists in the primary key
 *       and the foreign key. If there is no not null constraint on the column existing in the foreign key,
 *       add a predicate for IS NOT NULL.
 */
static bool
qo_check_fk_ref_pk_in_child_spec (PARSER_CONTEXT * parser, PT_NODE * query,
				  QO_REDUCE_REFERENCE_INFO * reduce_reference_info)
{
  PT_NODE *curr_pk_spec = NULL, *curr_fk_spec = NULL;
  MOP curr_pk_mop = NULL;
  SM_CLASS_CONSTRAINT *curr_pk_cons = NULL, *curr_fk_cons = NULL;
  SM_ATTRIBUTE *pk_cons_attr, *fk_cons_attr;
  PT_NODE *join_pred_point_list = NULL;
  PT_NODE *append_not_null_pred_list = NULL;
  PT_NODE *curr_pred_point = NULL;
  PT_NODE *curr_pred = NULL;
  PT_NODE *pk_pred_attr, *fk_pred_attr;
  PT_NODE *copy_fk_pred_attr, *fk_not_null_pred;
  int cons_attr_cnt;
  unsigned int cons_attr_flag;
  PT_NODE *arg1, *arg2;
  int i = 0;

  assert (parser != NULL && query != NULL);
  assert (reduce_reference_info != NULL);
  assert (reduce_reference_info->pk_spec != NULL);
  assert (reduce_reference_info->pk_mop != NULL);
  assert (reduce_reference_info->pk_cons != NULL);
  assert (reduce_reference_info->fk_spec != NULL);
  assert (reduce_reference_info->fk_cons != NULL);
  assert (reduce_reference_info->join_pred_point_list != NULL);
  assert (reduce_reference_info->append_not_null_pred_list == NULL);

  curr_pk_spec = reduce_reference_info->pk_spec;
  curr_pk_mop = reduce_reference_info->pk_mop;
  curr_pk_cons = reduce_reference_info->pk_cons;
  curr_fk_spec = reduce_reference_info->fk_spec;
  curr_fk_cons = reduce_reference_info->fk_cons;
  join_pred_point_list = reduce_reference_info->join_pred_point_list;

  assert (curr_fk_cons->type == SM_CONSTRAINT_FOREIGN_KEY);

  /* We must check that the oid of the parent table is equal to the fk_info->ref_class_oid
   * of the foreign key constraint.
   * 
   *   e.g. drop table if exists child, parent, super_parent;
   *        create table super_parent (c1 int primary key);
   *        create table parent under super_parent (c2 int);
   *        create table child (c1 int);
   *        alter table child add constraint foreign key (c1) references parent (c1);
   *
   *        -- irreducible
   *        select c.* from child c, super_parent s where c.c1 = s.c1;
   *
   *        -- reducible
   *        select c.* from child c, parent s where c.c1 = s.c1;
   */

  /* WS_OID or WS_REAL_OID, which one? */
  if (!OID_EQ (WS_REAL_OID (curr_pk_mop), &curr_fk_cons->fk_info->ref_class_oid))
    {
      goto exit_on_fail;
    }

  if (!BTID_IS_EQUAL (&(curr_pk_cons->index_btid), &(curr_fk_cons->fk_info->ref_class_pk_btid)))
    {
      goto exit_on_fail;
    }

  /* We need to check if all the attributes of the constraint are used in predicated.
   *
   *   e.g. drop table if exists child, parent;
   *        create table parent (c1 int, c2 int, primary key (c1, c2));
   *        create table child (c1 int, c2 int);
   *        alter table child add constraint foreign key (c1, c2) references parent (c1, c2);
   *
   *        -- irreducible
   *        select c.* from child c, parent p where c.c1 = p.c1;
   *        select c.* from child c, parent p where c.c2 = p.c2;
   *        select c.* from child c, parent p where c.c1 = p.c1 and c.c1 = p.c1;
   *
   *        -- reducible
   *        select c.* from child c, parent p where c.c1 = p.c1 and c.c2 = p.c2;
   */
  for (i = 0; curr_pk_cons->attributes[i] != NULL && curr_fk_cons->attributes[i] != NULL; i++);

  /* Set the bits of cons_attr_flag to 1 as many as the number of attributes in the constraint.
   * And if an attribute of the constraint is used in a predicate, it sets the bit of that index to 0.
   * After checking the predicates, if cons_attr_flag is 0, we know that all the attributes of the constraint
   * are used in the predicate. */
  cons_attr_cnt = i;
  cons_attr_flag = (1 << i) - 1;

  for (curr_pred_point = join_pred_point_list; curr_pred_point != NULL; curr_pred_point = curr_pred_point->next)
    {
      curr_pred = curr_pred_point;
      CAST_POINTER_TO_NODE (curr_pred);

      assert (PT_NODE_IS_EXPR (curr_pred));
      assert (PT_EXPR_OP (curr_pred) == PT_EQ);
      assert (PT_NODE_IS_NAME (PT_EXPR_ARG1 (curr_pred)));
      assert (PT_NODE_IS_NAME (PT_EXPR_ARG2 (curr_pred)));

      arg1 = PT_EXPR_ARG1 (curr_pred);
      arg2 = PT_EXPR_ARG2 (curr_pred);

      if (PT_SPEC_ID (curr_pk_spec) == PT_NAME_SPEC_ID (arg1))
	{
	  pk_pred_attr = PT_EXPR_ARG1 (curr_pred);
	}
      else if (PT_SPEC_ID (curr_pk_spec) == PT_NAME_SPEC_ID (arg2))
	{
	  pk_pred_attr = PT_EXPR_ARG2 (curr_pred);
	}
      else
	{
	  /* Already checked before. */
	  assert (false);
	  goto exit_on_fail_with_cleanup;
	}

      if (PT_SPEC_ID (curr_fk_spec) == PT_NAME_SPEC_ID (arg1))
	{
	  fk_pred_attr = PT_EXPR_ARG1 (curr_pred);
	}
      else if (PT_SPEC_ID (curr_fk_spec) == PT_NAME_SPEC_ID (arg2))
	{
	  fk_pred_attr = PT_EXPR_ARG2 (curr_pred);
	}
      else
	{
	  goto exit_on_fail_with_cleanup;
	}

      assert (pk_pred_attr != NULL);
      assert (fk_pred_attr != NULL);

      for (i = 0; curr_pk_cons->attributes[i] != NULL && curr_fk_cons->attributes[i] != NULL; i++)
	{
	  pk_cons_attr = curr_pk_cons->attributes[i];
	  fk_cons_attr = curr_fk_cons->attributes[i];

	  if (intl_identifier_casecmp (pk_cons_attr->header.name, PT_NAME_ORIGINAL (pk_pred_attr)) == 0)
	    {
	      if (intl_identifier_casecmp (fk_cons_attr->header.name, PT_NAME_ORIGINAL (fk_pred_attr)) == 0)
		{
		  /* If there is no not null constraint on a column of the table with the foreign key,
		   * predicates for IS NOT NULL must be added. */
		  if (!(fk_cons_attr->flags & SM_ATTFLAG_NON_NULL))
		    {
		      copy_fk_pred_attr = parser_copy_tree (parser, fk_pred_attr);
		      fk_not_null_pred = parser_make_expression (parser, PT_IS_NOT_NULL, copy_fk_pred_attr, NULL, NULL);
		      append_not_null_pred_list = parser_append_node (fk_not_null_pred, append_not_null_pred_list);
		    }

		  /* If an attribute of the constraint is used in a predicate, it sets the bit of that index to 0. */
		  cons_attr_flag &= ~(1 << i);

		  break;	/* curr_pred_point->next */
		}
	      else
		{
		  /* It cannot be reduced in fk_child_c1_c2 but can be reduced in fk_child_c2_c1.
		   * 
		   *   e.g. drop if exists child, parent;
		   *        create table parent (c1 int, c2 int, primary key (c1, c2));
		   *        create table child (c1 int, c2 int);
		   *        alter table child add constraint foreign key (c1, c2) references parent (c1, c2);
		   *        alter table child add constraint foreign key (c2, c1) references parent (c1, c2);
		   *
		   *        select c.* from child c, parent p where c.c1 = p.c2 and c.c2 = p.c1;
		   */
		  goto exit_on_fail_with_cleanup;
		}
	    }
	}
    }

  assert (curr_pred_point == NULL);

  /* If cons_attr_flag is non-zero, then all the attributes of the constraint are not used in the predicate. */
  if (cons_attr_flag != 0)
    {
      goto exit_on_fail_with_cleanup;
    }

  reduce_reference_info->append_not_null_pred_list = append_not_null_pred_list;

  return true;

exit_on_fail_with_cleanup:
  if (append_not_null_pred_list != NULL)
    {
      parser_free_tree (parser, append_not_null_pred_list);
      append_not_null_pred_list = NULL;
    }
  /* fallthrough */

exit_on_fail:
  return false;
}

/*
 * qo_check_reduce_predicate_for_parent_spec () - Whether the non-join predicate on the parent is reducible.
 *
 *   return: bool
 *   parser(in): parser context
 *   query(in): query to check
 *   reduce_reference_info(in/out): Information needed to check
 *
 * Note: Checks if there is a predicate on the child equal to the non-join predicate of the parent.
 */
static bool
qo_check_reduce_predicate_for_parent_spec (PARSER_CONTEXT * parser, PT_NODE * query,
					   QO_REDUCE_REFERENCE_INFO * reduce_reference_info)
{
  PT_NODE *fk_spec;
  SM_CLASS_CONSTRAINT *pk_cons, *fk_cons;
  SM_ATTRIBUTE *pk_cons_attr, *fk_cons_attr;
  PT_NODE *join_pred_point_list;
  PT_NODE *parent_pred_point_list;
  PT_NODE *child_pred_point_list;
  PT_NODE *curr_pred, *next_pred;
  PT_NODE *curr_parent_pred_point, *curr_parent_pred;
  PT_NODE *curr_child_pred_point, *curr_child_pred;
  PT_NODE *parent_pred_attr, *parent_pred_const;
  PT_NODE *child_pred_attr, *child_pred_const;
  const char *parent_pred_attr_str, *parent_pred_const_str;
  const char *child_pred_attr_str, *child_pred_const_str;
  PT_NODE *arg1, *arg2;
  int i;

  assert (parser != NULL && query != NULL);
  assert (reduce_reference_info != NULL);
  assert (reduce_reference_info->pk_cons != NULL);
  assert (reduce_reference_info->fk_spec != NULL);
  assert (reduce_reference_info->fk_cons != NULL);
  assert (reduce_reference_info->join_pred_point_list != NULL);
  assert (reduce_reference_info->parent_pred_point_list != NULL);

  pk_cons = reduce_reference_info->pk_cons;
  fk_spec = reduce_reference_info->fk_spec;
  fk_cons = reduce_reference_info->fk_cons;
  join_pred_point_list = reduce_reference_info->join_pred_point_list;
  parent_pred_point_list = reduce_reference_info->parent_pred_point_list;
  child_pred_point_list = NULL;

  /* child_pred_point_list */
  for (curr_pred = query->info.query.q.select.where; curr_pred != NULL; curr_pred = curr_pred->next)
    {
      SPEC_CNT_INFO info;

      if (curr_pred->or_next != NULL)
	{
	  continue;
	}

      memset (&info, 0, sizeof (SPEC_CNT_INFO));
      info.spec = fk_spec;

      next_pred = curr_pred->next;
      curr_pred->next = NULL;

      parser_walk_tree (parser, curr_pred, qo_get_name_cnt_by_spec, &info, NULL, NULL);

      curr_pred->next = next_pred;

      if (info.my_spec_cnt >= 1)
	{
	  if (curr_pred->node_type == PT_EXPR && curr_pred->info.expr.op == PT_EQ)
	    {
	      arg1 = curr_pred->info.expr.arg1;
	      arg2 = curr_pred->info.expr.arg2;

	      if ((pt_is_attr (arg1) && qo_is_reduceable_const (arg2)) ||
		  (pt_is_attr (arg2) && qo_is_reduceable_const (arg1)))
		{
		  child_pred_point_list = parser_append_node (pt_point (parser, curr_pred), child_pred_point_list);
		  continue;
		}
	    }
	}
    }

  /* parent_pred_point_list */
  for (curr_parent_pred_point = parent_pred_point_list; curr_parent_pred_point != NULL;
       curr_parent_pred_point = curr_parent_pred_point->next)
    {
      curr_parent_pred = curr_parent_pred_point;
      CAST_POINTER_TO_NODE (curr_parent_pred);

      assert (curr_parent_pred->node_type == PT_EXPR);

      arg1 = curr_parent_pred->info.expr.arg1;
      arg2 = curr_parent_pred->info.expr.arg2;

      if (pt_is_attr (arg1))
	{
	  parent_pred_attr = arg1;
	  parent_pred_const = arg2;
	}
      else
	{
	  assert (arg2->node_type == PT_NAME);
	  parent_pred_attr = arg2;
	  parent_pred_const = arg1;
	}

      parent_pred_attr_str = parent_pred_attr->info.name.original;

      fk_cons_attr = NULL;
      for (i = 0; pk_cons->attributes[i] != NULL && fk_cons->attributes[i] != NULL; i++)
	{
	  pk_cons_attr = pk_cons->attributes[i];

	  if (intl_identifier_casecmp (pk_cons_attr->header.name, parent_pred_attr_str) == 0)
	    {
	      fk_cons_attr = fk_cons->attributes[i];
	      break;
	    }
	}

      if (fk_cons_attr == NULL)
	{
	  /* not found */
	  goto exit_on_fail_with_cleanup;
	}

      /* child_pred_point_list */
      for (curr_child_pred_point = child_pred_point_list; curr_child_pred_point != NULL;
	   curr_child_pred_point = curr_child_pred_point->next)
	{
	  curr_child_pred = curr_child_pred_point;
	  CAST_POINTER_TO_NODE (curr_child_pred);

	  assert (curr_child_pred->node_type == PT_EXPR);

	  arg1 = curr_child_pred->info.expr.arg1;
	  arg2 = curr_child_pred->info.expr.arg2;

	  if (pt_is_attr (arg1))
	    {
	      child_pred_attr = arg1;
	      child_pred_const = arg2;
	    }
	  else
	    {
	      assert (arg2->node_type == PT_NAME);
	      child_pred_attr = arg2;
	      child_pred_const = arg1;
	    }

	  child_pred_attr_str = child_pred_attr->info.name.original;

	  if (intl_identifier_casecmp (fk_cons_attr->header.name, child_pred_attr_str) == 0)
	    {
	      unsigned int save_custom;

	      save_custom = parser->custom_print;	/* save */
	      parser->custom_print |= PT_CONVERT_RANGE;

	      parent_pred_const_str = parser_print_tree (parser, parent_pred_const);
	      child_pred_const_str = parser_print_tree (parser, child_pred_const);

	      parser->custom_print = save_custom;	/* restore */

	      if (pt_str_compare (parent_pred_const_str, child_pred_const_str, CASE_INSENSITIVE) == 0)
		{
		  break;
		}
	    }
	}

      if (child_pred_point_list == NULL)
	{
	  /* not found */
	  goto exit_on_fail_with_cleanup;
	}
    }

  assert (curr_parent_pred_point == NULL);

  if (child_pred_point_list != NULL)
    {
      parser_free_tree (parser, child_pred_point_list);
      child_pred_point_list = NULL;
    }

  return true;

exit_on_fail_with_cleanup:
  if (child_pred_point_list != NULL)
    {
      parser_free_tree (parser, child_pred_point_list);
      child_pred_point_list = NULL;
    }
  /* fallthrough */

  return false;
}

/*
 * qo_reduce_predicate_for_parent_spec () - The join predicates for pk_spec in reduce_reference_info are removed.
 *   return: bool
 *   parser(in): parser context
 *   query(in): query to check
 *   reduce_reference_info(in/out): Information needed to check
 *
 * Note: Predicates in append_not_null_pred_list of reduce_reference_info are added without duplicates.
 */
static void
qo_reduce_predicate_for_parent_spec (PARSER_CONTEXT * parser, PT_NODE * query,
				     QO_REDUCE_REFERENCE_INFO * reduce_reference_info)
{
  PT_NODE *curr_pred_point = NULL, *prev_pred_point = NULL, *next_pred_point = NULL;
  PT_NODE *curr_pred = NULL, *prev_pred = NULL, *next_pred = NULL, *parent_pred = NULL;
  PT_NODE *curr_append_pred = NULL, *prev_append_pred = NULL, *next_append_pred = NULL;
  PT_NODE *curr_pred_arg = NULL, *curr_append_pred_arg = NULL;

  assert (parser != NULL && query != NULL);
  assert (reduce_reference_info->join_pred_point_list != NULL);

  prev_pred = NULL;
  curr_pred = query->info.query.q.select.where;
  while (curr_pred != NULL)
    {
      for (parent_pred = NULL, prev_pred_point = NULL, curr_pred_point = reduce_reference_info->join_pred_point_list;
	   curr_pred_point != NULL; prev_pred_point = curr_pred_point, curr_pred_point = curr_pred_point->next)
	{
	  parent_pred = curr_pred_point;
	  CAST_POINTER_TO_NODE (parent_pred);

	  if (curr_pred == parent_pred)
	    {
	      /* found */
	      break;
	    }
	}

      next_pred = curr_pred->next;

      if (curr_pred_point == NULL)
	{
	  /* not found */
	  prev_pred = curr_pred;
	  curr_pred = next_pred;
	  continue;
	}

      /* found */
      if (prev_pred != NULL)
	{
	  prev_pred->next = next_pred;
	}
      else
	{
	  query->info.query.q.select.where = next_pred;
	}
      parser_free_node (parser, curr_pred);
      curr_pred = next_pred;

      next_pred_point = curr_pred_point->next;

      if (prev_pred_point != NULL)
	{
	  prev_pred_point->next = next_pred_point;
	}
      else
	{
	  reduce_reference_info->join_pred_point_list = next_pred_point;
	}
      parser_free_node (parser, curr_pred_point);
      curr_pred_point = next_pred_point;
    }

  assert (reduce_reference_info->join_pred_point_list == NULL);

  prev_pred = NULL;
  curr_pred = query->info.query.q.select.where;
  while (curr_pred != NULL && reduce_reference_info->parent_pred_point_list != NULL)
    {
      for (parent_pred = NULL, prev_pred_point = NULL, curr_pred_point =
	   reduce_reference_info->parent_pred_point_list; curr_pred_point != NULL;
	   prev_pred_point = curr_pred_point, curr_pred_point = curr_pred_point->next)
	{
	  parent_pred = curr_pred_point;
	  CAST_POINTER_TO_NODE (parent_pred);

	  if (curr_pred == parent_pred)
	    {
	      /* found */
	      break;
	    }
	}

      next_pred = curr_pred->next;

      if (curr_pred_point == NULL)
	{
	  /* not found */
	  prev_pred = curr_pred;
	  curr_pred = next_pred;
	  continue;
	}

      /* found */
      if (prev_pred != NULL)
	{
	  prev_pred->next = next_pred;
	}
      else
	{
	  query->info.query.q.select.where = next_pred;
	}
      parser_free_node (parser, curr_pred);
      curr_pred = next_pred;

      next_pred_point = curr_pred_point->next;

      if (prev_pred_point != NULL)
	{
	  prev_pred_point->next = next_pred_point;
	}
      else
	{
	  reduce_reference_info->parent_pred_point_list = next_pred_point;
	}
      parser_free_node (parser, curr_pred_point);
      curr_pred_point = next_pred_point;
    }

  assert (reduce_reference_info->parent_pred_point_list == NULL);

  prev_append_pred = NULL;
  curr_append_pred = reduce_reference_info->append_not_null_pred_list;
  while (curr_append_pred != NULL)
    {
      assert (PT_NODE_IS_EXPR (curr_append_pred));
      assert (PT_EXPR_OP (curr_append_pred) == PT_IS_NOT_NULL);
      assert (PT_NODE_IS_NAME (PT_EXPR_ARG1 (curr_append_pred)));

      next_append_pred = curr_append_pred->next;

      for (curr_pred = next_append_pred; curr_pred != NULL; curr_pred = curr_pred->next)
	{
	  curr_pred_arg = PT_EXPR_ARG1 (curr_pred);
	  curr_append_pred_arg = PT_EXPR_ARG1 (curr_append_pred);

	  if (pt_name_equal (parser, curr_pred_arg, curr_append_pred_arg))
	    {
	      /* found */
	      break;
	    }
	}

      if (curr_pred == NULL)
	{
	  /* not found */
	  prev_append_pred = curr_append_pred;
	  curr_append_pred = next_append_pred;
	  continue;
	}

      /* found */
      if (prev_append_pred != NULL)
	{
	  prev_append_pred->next = next_append_pred;
	}
      else
	{
	  reduce_reference_info->append_not_null_pred_list = next_append_pred;
	}
      parser_free_node (parser, curr_append_pred);
      curr_append_pred = next_append_pred;
    }

  prev_append_pred = NULL;
  curr_append_pred = reduce_reference_info->append_not_null_pred_list;
  while (curr_append_pred != NULL)
    {
      for (curr_pred = query->info.query.q.select.where; curr_pred != NULL; curr_pred = curr_pred->next)
	{
	  if (PT_NODE_IS_EXPR (curr_pred) && PT_EXPR_OP (curr_pred) == PT_IS_NOT_NULL
	      && PT_NODE_IS_NAME (PT_EXPR_ARG1 (curr_pred)))
	    {
	      curr_pred_arg = PT_EXPR_ARG1 (curr_pred);
	      curr_append_pred_arg = PT_EXPR_ARG1 (curr_append_pred);

	      if (pt_name_equal (parser, curr_pred_arg, curr_append_pred_arg))
		{
		  /* found */
		  break;
		}
	    }
	}

      next_append_pred = curr_append_pred->next;

      if (curr_pred == NULL)
	{
	  /* not found */
	  prev_append_pred = curr_append_pred;
	  curr_append_pred = next_append_pred;
	  continue;
	}

      /* found */
      if (prev_append_pred != NULL)
	{
	  prev_append_pred->next = next_append_pred;
	}
      else
	{
	  reduce_reference_info->append_not_null_pred_list = next_append_pred;
	}
      parser_free_node (parser, curr_append_pred);
      curr_append_pred = next_append_pred;
    }

  if (reduce_reference_info->append_not_null_pred_list != NULL)
    {
      query->info.query.q.select.where =
	parser_append_node (reduce_reference_info->append_not_null_pred_list, query->info.query.q.select.where);
      reduce_reference_info->append_not_null_pred_list = NULL;
    }

  return;
}

/*
 * qo_is_semi_join_on_after () - is the location that of the ON of a SEMI JOIN after the spec, with no RIGHT OUTER
 *      JOIN connected behind that SEMI JOIN? That ON drops the outer rows it rejects as a WHERE term does, while an
 *      ANTI JOIN keeps them.  A RIGHT OUTER JOIN behind the SEMI JOIN NULL-fills the spec after that ON ran: the
 *      rewrite would turn it INNER and move the ON terms to WHERE, dropping the rows it preserves (CBRD-27783).  A
 *      SEMI JOIN on a unique key is still one here: qo_rewrite_unique_semi_inner () turns it INNER only after
 *      qo_rewrite_outerjoin ()
 *   return: bool
 *   spec(in): the outer joined spec
 *   location(in): location of a WHERE term
 */
static bool
qo_is_semi_join_on_after (PT_NODE * spec, int location)
{
  PT_NODE *ns;

  for (ns = spec->next; ns; ns = ns->next)
    {
      if (ns->info.spec.location == location)
	{
	  break;
	}
    }

  if (ns == NULL || ns->info.spec.join_type != PT_JOIN_SEMI)
    {
      return false;
    }

  /* traverse the specs connected behind the semi join */
  for (ns = ns->next; ns && ns->info.spec.join_type != PT_JOIN_NONE; ns = ns->next)
    {
      if (ns->info.spec.join_type == PT_JOIN_RIGHT_OUTER)
	{
	  return false;
	}
    }

  return true;
}

/*
 * qo_rewrite_outerjoin () - Rewrite outer join to inner join
 *   return: PT_NODE *
 *   parser(in):
 *   node(in): SELECT node
 *   arg(in):
 *   continue_walk(in):
 *
 * Note: do parser_walk_tree() pre function
 */
static PT_NODE *
qo_rewrite_outerjoin (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
{
  PT_NODE *spec, *expr, *ns, *save_next;
  SPEC_ID_INFO info, info_spec;
  RESET_LOCATION_INFO locate_info;
  bool rewrite_again, is_outer_joined;

  if (node->node_type != PT_SELECT)
    {
      return node;
    }

  if (node->info.query.q.select.connect_by)
    {
      /* don't rewrite if the query is hierarchical because conditions in 'where' must be applied after HQ evaluation;
       * HQ uses as input the result of joins */
      return node;
    }

  do
    {
      rewrite_again = false;
      /* traverse spec list */
      for (spec = node->info.query.q.select.from; spec; spec = spec->next)
	{
	  /* check outer join spec.  SEMI/ANTI specs report as outer-joined for the view-merge freeze, but
	     they are NL-inner only and never convert to inner here; including them lets a null-intolerant
	     WHERE predicate on the inner set rewrite_again with no conversion -> infinite loop (CBRD-26872). */
	  is_outer_joined = mq_is_outer_join_spec (parser, spec)
	    && spec->info.spec.join_type != PT_JOIN_SEMI && spec->info.spec.join_type != PT_JOIN_ANTI;
	  if (is_outer_joined)
	    {
	      info.id = info_spec.id = spec->info.spec.id;

	      /* search where list */
	      for (expr = node->info.query.q.select.where; expr; expr = expr->next)
		{
		  if (expr->node_type == PT_EXPR
		      && (expr->info.expr.location == 0 || qo_is_semi_join_on_after (spec, expr->info.expr.location))
		      && expr->info.expr.op != PT_IS_NULL && expr->or_next == NULL && expr->info.expr.op != PT_AND
		      && expr->info.expr.op != PT_OR)
		    {
		      info_spec.appears = false;
		      info.nullable = false;

		      save_next = expr->next;
		      expr->next = NULL;
		      (void) parser_walk_tree (parser, expr, NULL, NULL, qo_check_nullable_expr_with_spec, &info);
		      (void) parser_walk_tree (parser, expr, qo_get_name_by_spec_id, &info_spec, NULL, NULL);
		      expr->next = save_next;

		      /* have found a term which makes outer join to inner */
		      /* there are predicate referenced by spec and all preds are not nullable */
		      if (info_spec.appears && !info.nullable)
			{
			  rewrite_again = true;
			  if (spec->info.spec.join_type == PT_JOIN_LEFT_OUTER)
			    {
			      spec->info.spec.join_type = PT_JOIN_INNER;

			      locate_info.start = spec->info.spec.location;
			      locate_info.end = locate_info.start;
			      (void) parser_walk_tree (parser, node->info.query.q.select.where, qo_reset_location,
						       &locate_info, NULL, NULL);
			    }

			  /* rewrite the following connected right outer join to inner join */
			  for (ns = spec->next;	/* traverse next spec */
			       ns && ns->info.spec.join_type != PT_JOIN_NONE; ns = ns->next)
			    {
			      if (ns->info.spec.join_type == PT_JOIN_RIGHT_OUTER)
				{
				  ns->info.spec.join_type = PT_JOIN_INNER;
				  locate_info.start = ns->info.spec.location;
				  locate_info.end = locate_info.start;
				  (void) parser_walk_tree (parser, node->info.query.q.select.where, qo_reset_location,
							   &locate_info, NULL, NULL);
				}
			    }
			  break;
			}
		    }
		}
	    }

	  if (spec->info.spec.derived_table && spec->info.spec.derived_table_type == PT_IS_SUBQUERY)
	    {
	      /* apply qo_rewrite_outerjoin() to derived table's subquery */
	      (void) parser_walk_tree (parser, spec->info.spec.derived_table, qo_rewrite_outerjoin, NULL, NULL, NULL);
	    }
	}
    }
  while (rewrite_again);

  *continue_walk = PT_LIST_WALK;

  return node;
}

/*
 * qo_reset_location () -
 *   return:
 *   parser(in):
 *   node(in):
 *   arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
qo_reset_location (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
{
  RESET_LOCATION_INFO *infop = (RESET_LOCATION_INFO *) arg;

  if (node->node_type == PT_EXPR && node->info.expr.location >= infop->start && node->info.expr.location <= infop->end)
    {
      node->info.expr.location = 0;
    }

  if (node->node_type == PT_NAME && node->info.name.location >= infop->start && node->info.name.location <= infop->end)
    {
      node->info.name.location = 0;
    }

  if (node->node_type == PT_VALUE && node->info.value.location >= infop->start
      && node->info.value.location <= infop->end)
    {
      node->info.value.location = 0;
    }

  return node;
}

/*
 * qo_rewrite_innerjoin () - Rewrite explicit(ordered) inner join
 *			  to implicit(unordered) inner join
 *   return: PT_NODE *
 *   parser(in):
 *   node(in): SELECT node
 *   arg(in):
 *   continue_walk(in):
 *
 * Note: If join order hint is set, skip and go ahead.
 *   do parser_walk_tree() pre function
 *   Also called from qo_reduce_equality_terms_post (), which needs the location reset. Calling it twice is harmless.
 */
PT_NODE *
qo_rewrite_innerjoin (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
{
  PT_NODE *spec, *spec2;
  RESET_LOCATION_INFO info;	/* spec location reset info */

  if (node->node_type != PT_SELECT)
    {
      return node;
    }

  if (node->info.query.q.select.connect_by)
    {
      /* don't rewrite if the query is hierarchical because conditions in 'where' must be applied after HQ evaluation;
       * HQ uses as input the result of joins */
      return node;
    }

  if (node->info.query.q.select.hint & PT_HINT_ORDERED)
    {
      /* join hint: force join left-to-right. skip and go ahead. */
      return node;
    }

  info.start = 0;
  info.end = 0;
  info.found_outerjoin = false;

  /* traverse spec list to find disconnected spec list */
  for (info.start_spec = spec = node->info.query.q.select.from; spec; spec = spec->next)
    {

      switch (spec->info.spec.join_type)
	{
	case PT_JOIN_LEFT_OUTER:
	case PT_JOIN_RIGHT_OUTER:
	  /* case PT_JOIN_FULL_OUTER: */
	case PT_JOIN_SEMI:
	case PT_JOIN_ANTI:
	  /* semi/anti are reorder barriers. the explicit->implicit rewrite below also resets this segment's
	   * ON-predicate locations via qo_reset_location(), which would strip the semi/anti ON location that
	   * query_graph relies on to bind the join term and set semi_anti_dep_set; so the whole segment is frozen. */
	  info.found_outerjoin = true;
	  break;
	default:
	  break;
	}

      if (spec->info.spec.join_type == PT_JOIN_NONE && info.found_outerjoin == false && info.start < info.end)
	{
	  /* rewrite explicit inner join to implicit inner join */
	  for (spec2 = info.start_spec; spec2 != spec; spec2 = spec2->next)
	    {
	      if (spec2->info.spec.join_type == PT_JOIN_INNER)
		{
		  spec2->info.spec.join_type = PT_JOIN_NONE;
		}
	    }

	  /* reset location of spec list */
	  (void) parser_walk_tree (parser, node->info.query.q.select.where, qo_reset_location, &info, NULL, NULL);

	  /* reset start spec, found_outerjoin */
	  info.start = spec->info.spec.location;
	  info.start_spec = spec;
	  info.found_outerjoin = false;
	}

      info.end = spec->info.spec.location;

      if (spec->info.spec.derived_table && spec->info.spec.derived_table_type == PT_IS_SUBQUERY)
	{
	  /* apply qo_rewrite_innerjoin() to derived table's subquery */
	  (void) parser_walk_tree (parser, spec->info.spec.derived_table, qo_rewrite_innerjoin, NULL, NULL, NULL);
	}
    }

  if (info.found_outerjoin == false && info.start < info.end)
    {
      /* rewrite explicit inner join to implicit inner join */
      for (spec2 = info.start_spec; spec2; spec2 = spec2->next)
	{
	  if (spec2->info.spec.join_type == PT_JOIN_INNER)
	    {
	      spec2->info.spec.join_type = PT_JOIN_NONE;
	    }
	}

      /* reset location of spec list */
      (void) parser_walk_tree (parser, node->info.query.q.select.where, qo_reset_location, &info, NULL, NULL);
    }

  *continue_walk = PT_LIST_WALK;

  return node;
}

/* argument classification for a CONNECT BY predicate side (see pt_split_hash_attrs_for_HQ) */
typedef struct qo_hq_arg_class
{
  bool has_prior;		/* references a PRIOR expression */
  bool has_name;		/* references a column outside of PRIOR */
} QO_HQ_ARG_CLASS;

/*
 * qo_hq_classify_arg_walker () - marks PRIOR references (without descending
 *   into them) and bare column references of a CONNECT BY predicate side
 */
static PT_NODE *
qo_hq_classify_arg_walker (PARSER_CONTEXT * parser, PT_NODE * tree, void *void_arg, int *continue_walk)
{
  QO_HQ_ARG_CLASS *c = (QO_HQ_ARG_CLASS *) void_arg;

  *continue_walk = PT_CONTINUE_WALK;

  if (PT_IS_EXPR_NODE_WITH_OPERATOR (tree, PT_PRIOR) || PT_IS_EXPR_NODE_WITH_OPERATOR (tree, PT_QPRIOR))
    {
      c->has_prior = true;
      /* names under PRIOR are parent-row constants, not scan columns */
      *continue_walk = PT_LIST_WALK;
    }
  else if (PT_IS_NAME_NODE (tree))
    {
      c->has_name = true;
    }

  return tree;
}

/*
 * qo_hq_op_except_prior_walker () - detects hierarchical operators other than
 *   PRIOR (LEVEL, CONNECT_BY_ISLEAF, ...); such terms are evaluated in the
 *   CONNECT BY proc and can use neither an index nor a hash
 */
static PT_NODE *
qo_hq_op_except_prior_walker (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
{
  bool *found = (bool *) arg;

  if (node->node_type == PT_EXPR && PT_CHECK_HQ_OP_EXCEPT_PRIOR (node->info.expr.op))
    {
      *found = true;
      *continue_walk = PT_STOP_WALK;
    }

  return node;
}

/*
 * qo_hq_is_hashable_eq_term () - checks one PT_EQ conjunct of a CONNECT BY
 *   predicate: one side a pure PRIOR expression (probe), the other a pure scan
 *   column expression (build), without other hierarchical operators
 *   return: whether the hash list scan can use the conjunct as a key
 *   build_name(out): the build side when it is a bare column, NULL otherwise
 * Note: mirrors pt_split_hash_attrs_for_HQ / CHECK_HASH_ATTR, where a side
 *       carrying both PRIOR and a column is unhashable and a constant side
 *       gives no parent-child link.
 */
static bool
qo_hq_is_hashable_eq_term (PARSER_CONTEXT * parser, PT_NODE * term, PT_NODE ** build_name)
{
  bool has_hq_op = false;
  QO_HQ_ARG_CLASS c1 = { false, false };
  QO_HQ_ARG_CLASS c2 = { false, false };
  PT_NODE *save_next = term->next;
  PT_NODE *build;

  *build_name = NULL;

  term->next = NULL;
  (void) parser_walk_tree (parser, term, qo_hq_op_except_prior_walker, &has_hq_op, NULL, NULL);
  term->next = save_next;
  if (has_hq_op)
    {
      return false;
    }

  (void) parser_walk_tree (parser, term->info.expr.arg1, qo_hq_classify_arg_walker, &c1, NULL, NULL);
  (void) parser_walk_tree (parser, term->info.expr.arg2, qo_hq_classify_arg_walker, &c2, NULL, NULL);

  if (c1.has_prior && !c1.has_name && c2.has_name && !c2.has_prior)
    {
      build = term->info.expr.arg2;
    }
  else if (c1.has_name && !c1.has_prior && c2.has_prior && !c2.has_name)
    {
      build = term->info.expr.arg1;
    }
  else
    {
      return false;
    }

  if (PT_IS_NAME_NODE (build))
    {
      *build_name = build;
    }

  return true;
}

/*
 * qo_hq_has_hashable_eq_term () - checks the CONNECT BY predicate (still an
 *   AND parse tree at rewrite time) for a hashable equality conjunct
 *   return: whether any conjunct is hashable
 */
static bool
qo_hq_has_hashable_eq_term (PARSER_CONTEXT * parser, PT_NODE * pred)
{
  PT_NODE *build_name;

  /* the predicate is an AND parse tree at rewrite time, but iterate a
   * conjunct (->next) list too in case it was already normalized */
  for (; pred != NULL; pred = pred->next)
    {
      if (PT_IS_EXPR_NODE_WITH_OPERATOR (pred, PT_AND))
	{
	  if (qo_hq_has_hashable_eq_term (parser, pred->info.expr.arg1)
	      || qo_hq_has_hashable_eq_term (parser, pred->info.expr.arg2))
	    {
	      return true;
	    }
	  continue;
	}

      if (PT_IS_EXPR_NODE_WITH_OPERATOR (pred, PT_EQ) && pred->or_next == NULL
	  && qo_hq_is_hashable_eq_term (parser, pred, &build_name))
	{
	  return true;
	}
    }

  return false;
}

/*
 * qo_hq_is_spec_column () - checks whether a node is the given column of the spec
 */
static bool
qo_hq_is_spec_column (PT_NODE * node, PT_NODE * spec, const char *attr_name)
{
  return (PT_IS_NAME_NODE (node) && node->info.name.spec_id == spec->info.spec.id
	  && intl_identifier_casecmp (node->info.name.original, attr_name) == 0);
}

/*
 * qo_hq_eq_term_binds () - checks whether one PT_EQ (or PT_IS_IN) term of a
 *   CONNECT BY predicate fixes the column for every parent row: the column
 *   alone on one side (the left side of IN), no scan column on the other (a
 *   constant, a host variable or a PRIOR expression), so the single-table plan
 *   can use it as an index key
 */
static bool
qo_hq_eq_term_binds (PARSER_CONTEXT * parser, PT_NODE * term, PT_NODE * spec, const char *attr_name)
{
  bool has_hq_op = false;
  QO_HQ_ARG_CLASS c = { false, false };
  PT_NODE *save_next = term->next;
  PT_NODE *other;

  if (qo_hq_is_spec_column (term->info.expr.arg1, spec, attr_name))
    {
      other = term->info.expr.arg2;
    }
  else if (PT_IS_EXPR_NODE_WITH_OPERATOR (term, PT_EQ) && qo_hq_is_spec_column (term->info.expr.arg2, spec, attr_name))
    {
      other = term->info.expr.arg1;
    }
  else
    {
      return false;
    }

  term->next = NULL;
  (void) parser_walk_tree (parser, term, qo_hq_op_except_prior_walker, &has_hq_op, NULL, NULL);
  term->next = save_next;
  if (has_hq_op)
    {
      return false;
    }

  (void) parser_walk_tree (parser, other, qo_hq_classify_arg_walker, &c, NULL, NULL);

  return !c.has_name;
}

/*
 * qo_hq_or_term_binds () - checks whether a conjunct of a CONNECT BY predicate
 *   fixes the column to one of a few values: an IN list or a disjunction whose
 *   every disjunct fixes the column (see qo_hq_eq_term_binds)
 *   return: whether the column is fixed
 */
static bool
qo_hq_or_term_binds (PARSER_CONTEXT * parser, PT_NODE * term, PT_NODE * spec, const char *attr_name)
{
  for (; term != NULL; term = term->or_next)
    {
      if (PT_IS_EXPR_NODE_WITH_OPERATOR (term, PT_OR))
	{
	  if (!qo_hq_or_term_binds (parser, term->info.expr.arg1, spec, attr_name)
	      || !qo_hq_or_term_binds (parser, term->info.expr.arg2, spec, attr_name))
	    {
	      return false;
	    }
	  continue;
	}

      if (!PT_IS_EXPR_NODE_WITH_OPERATOR (term, PT_EQ) && !PT_IS_EXPR_NODE_WITH_OPERATOR (term, PT_IS_IN))
	{
	  return false;
	}
      if (!qo_hq_eq_term_binds (parser, term, spec, attr_name))
	{
	  return false;
	}
    }

  return true;
}

/*
 * qo_hq_column_is_bound () - checks whether a conjunct of the CONNECT BY
 *   predicate fixes the column (see qo_hq_eq_term_binds, qo_hq_or_term_binds)
 *   return: whether the column is fixed
 *   is_link(out): set when a conjunct fixing it is a hashable PRIOR equality
 *                 whose build side is the column
 */
static bool
qo_hq_column_is_bound (PARSER_CONTEXT * parser, PT_NODE * pred, PT_NODE * spec, const char *attr_name, bool * is_link)
{
  PT_NODE *build_name;
  bool bound = false;

  for (; pred != NULL; pred = pred->next)
    {
      if (PT_IS_EXPR_NODE_WITH_OPERATOR (pred, PT_AND))
	{
	  if (qo_hq_column_is_bound (parser, pred->info.expr.arg1, spec, attr_name, is_link))
	    {
	      bound = true;
	    }
	  if (qo_hq_column_is_bound (parser, pred->info.expr.arg2, spec, attr_name, is_link))
	    {
	      bound = true;
	    }
	  continue;
	}

      if (PT_IS_EXPR_NODE_WITH_OPERATOR (pred, PT_EQ) && pred->or_next == NULL
	  && qo_hq_eq_term_binds (parser, pred, spec, attr_name))
	{
	  bound = true;
	  if (qo_hq_is_hashable_eq_term (parser, pred, &build_name) && build_name != NULL)
	    {
	      *is_link = true;
	    }
	}
      else if (qo_hq_or_term_binds (parser, pred, spec, attr_name))
	{
	  /* the key range takes one value per disjunct, but only a single equality can be the PRIOR link */
	  bound = true;
	}
    }

  return bound;
}

/*
 * qo_hq_index_serves_child_lookup () - checks whether a normal index of the
 *   class without a filter predicate can find the children of a parent row by
 *   a key range
 *   return: whether the leading columns of an index are all fixed by the
 *           CONNECT BY predicate and one of them is the build side of a PRIOR
 *           link
 */
static bool
qo_hq_index_serves_child_lookup (PARSER_CONTEXT * parser, PT_NODE * pred, PT_NODE * spec,
				 SM_CLASS_CONSTRAINT * constraints)
{
  SM_CLASS_CONSTRAINT *cons;
  int i;
  bool is_link;

  for (cons = constraints; cons != NULL; cons = cons->next)
    {
      /* a filter index is considered only with USING INDEX, which keeps the single-table plan anyway */
      if (!SM_IS_CONSTRAINT_INDEX_FAMILY (cons->type) || cons->index_status != SM_NORMAL_INDEX
	  || cons->attributes == NULL || cons->filter_predicate != NULL)
	{
	  continue;
	}

      is_link = false;
      for (i = 0; cons->attributes[i] != NULL; i++)
	{
	  /* attributes[] of a function index skip the function key and list its arguments after the key columns */
	  if (cons->func_index_info != NULL && i >= cons->func_index_info->col_id)
	    {
	      break;
	    }
	  if (!qo_hq_column_is_bound (parser, pred, spec, cons->attributes[i]->header.name, &is_link))
	    {
	      break;
	    }
	  if (is_link)
	    {
	      return true;
	    }
	}
    }

  return false;
}

/*
 * qo_hq_cast_walker () - detects a cast that can fail on a value it cannot
 *   convert: any cast written in the query, or a cast the compiler wrapped
 *   from a string to another type (its other wrapped casts only widen)
 */
static PT_NODE *
qo_hq_cast_walker (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
{
  bool *found = (bool *) arg;

  if (PT_IS_QUERY_NODE_TYPE (node->node_type))
    {
      *continue_walk = PT_LIST_WALK;
    }
  else if (PT_IS_EXPR_NODE_WITH_OPERATOR (node, PT_CAST)
	   && (!PT_EXPR_INFO_IS_FLAGED (node, PT_EXPR_INFO_CAST_WRAP)
	       || (node->info.expr.arg1 != NULL && PT_IS_CHAR_STRING_TYPE (node->info.expr.arg1->type_enum)
		   && !PT_IS_CHAR_STRING_TYPE (node->type_enum))))
    {
      *found = true;
      *continue_walk = PT_STOP_WALK;
    }

  return node;
}

/*
 * qo_hq_fixed_numeric_key () - the precision and scale of a fixed NUMERIC key
 *   side
 *   return: false for a float NUMERIC or a side without a data type
 *   arg(in): a NUMERIC key side
 *   precision(out), scale(out): its precision and scale
 */
static bool
qo_hq_fixed_numeric_key (PT_NODE * arg, int *precision, int *scale)
{
  /* PRIOR is typed float NUMERIC, but its values keep the form of its operand */
  if ((PT_IS_EXPR_NODE_WITH_OPERATOR (arg, PT_PRIOR) || PT_IS_EXPR_NODE_WITH_OPERATOR (arg, PT_QPRIOR))
      && arg->info.expr.arg1 != NULL)
    {
      arg = arg->info.expr.arg1;
    }
  if (arg->data_type == NULL)
    {
      return false;
    }
  *precision = arg->data_type->info.data_type.precision;
  *scale = arg->data_type->info.data_type.dec_precision;

  return *precision > 0 && *precision <= DB_MAX_FIXED_NUMERIC_PRECISION;
}

/*
 * qo_hq_is_inexact_numeric_pair () - checks whether the hash list scan hashes
 *   a pair whose common type is NUMERIC through a DOUBLE that can round equal
 *   values of the two sides apart, or does not hash it at all
 *   return: true for such a pair
 *   prior(in): the side of a PT_EQ conjunct with PRIOR
 *   build(in): the other side
 * Note: the PRIOR key is typed float NUMERIC, so a pair with a NUMERIC PRIOR
 *       side hashes as DOUBLE; equal values of one fixed scale convert alike.
 *       A NUMERIC converts its coefficient with one rounding and then divides
 *       it by 10^scale, which rounds again unless the coefficient is below
 *       2^53 (15 digits); a negative scale or a float NUMERIC rounds as well.
 *       An integer PRIOR side and a fixed NUMERIC column hash in their common
 *       NUMERIC, or not at all beyond 38 digits.
 */
static bool
qo_hq_is_inexact_numeric_pair (PT_NODE * prior, PT_NODE * build)
{
  PT_NODE *arg[2] = { prior, build };
  bool fixed[2] = { false, false };
  int precision[2] = { 0, 0 };
  int scale[2] = { 0, 0 };
  int i;

  if (prior->type_enum != PT_TYPE_NUMERIC && build->type_enum != PT_TYPE_NUMERIC)
    {
      return false;
    }

  for (i = 0; i < 2; i++)
    {
      switch (arg[i]->type_enum)
	{
	case PT_TYPE_NUMERIC:
	  fixed[i] = qo_hq_fixed_numeric_key (arg[i], &precision[i], &scale[i]);
	  break;
	case PT_TYPE_SMALLINT:
	  precision[i] = DB_SMALLINT_PRECISION;
	  break;
	case PT_TYPE_INTEGER:
	  precision[i] = DB_INTEGER_PRECISION;
	  break;
	case PT_TYPE_BIGINT:
	  precision[i] = DB_BIGINT_PRECISION;
	  break;
	case PT_TYPE_MAYBE:
	  break;
	default:
	  /* the common type is not NUMERIC */
	  return false;
	}
    }

  if (precision[0] > 0 && prior->type_enum != PT_TYPE_NUMERIC && fixed[1] && PT_IS_NAME_NODE (build))
    {
      return MAX (precision[0], precision[1] - scale[1]) + MAX (scale[1], 0) > DB_MAX_FIXED_NUMERIC_PRECISION;
    }

  if (fixed[0] && fixed[1] && scale[0] == scale[1])
    {
      return false;
    }

  for (i = 0; i < 2; i++)
    {
      if (arg[i]->type_enum == PT_TYPE_NUMERIC && (!fixed[i] || scale[i] < 0 || (scale[i] > 0 && precision[i] > 15)))
	{
	  return true;
	}
    }

  return false;
}

/*
 * qo_hq_is_unsafe_hash_key () - checks whether one PT_EQ conjunct, as a key of
 *   the hash list scan, can give a result other than the single-table plan
 * Note: a JSON key is hashed by its text, so JSON values that compare equal
 *       ('1.0' and '"1"' against '1') can hash apart. The build key is
 *       evaluated on every row, even one no parent reaches, so a cast there
 *       can fail where the single-table plan never evaluates it. A NUMERIC
 *       PRIOR key is hashed as DOUBLE (see qo_hq_is_inexact_numeric_pair).
 */
static bool
qo_hq_is_unsafe_hash_key (PARSER_CONTEXT * parser, PT_NODE * term)
{
  PT_NODE *arg1 = term->info.expr.arg1;
  PT_NODE *arg2 = term->info.expr.arg2;
  QO_HQ_ARG_CLASS c1 = { false, false };
  QO_HQ_ARG_CLASS c2 = { false, false };
  bool found = false;

  if (arg1 == NULL || arg2 == NULL)
    {
      return false;
    }

  if (arg1->type_enum == PT_TYPE_JSON || arg2->type_enum == PT_TYPE_JSON)
    {
      return true;
    }

  (void) parser_walk_tree (parser, arg1, qo_hq_classify_arg_walker, &c1, NULL, NULL);
  (void) parser_walk_tree (parser, arg2, qo_hq_classify_arg_walker, &c2, NULL, NULL);
  if (c1.has_prior == c2.has_prior)
    {
      if (c1.has_prior || c1.has_name == c2.has_name)
	{
	  return false;
	}

      /* a column side paired with a constant is a build key too */
      (void) parser_walk_tree (parser, c1.has_name ? arg1 : arg2, qo_hq_cast_walker, &found, NULL, NULL);

      return found;
    }

  /* TODO: a NUMERIC PRIOR key hashes as DOUBLE; drop the DOUBLE cases once it hashes exactly */
  if (c1.has_prior ? qo_hq_is_inexact_numeric_pair (arg1, arg2) : qo_hq_is_inexact_numeric_pair (arg2, arg1))
    {
      return true;
    }

  (void) parser_walk_tree (parser, c1.has_prior ? arg2 : arg1, qo_hq_cast_walker, &found, NULL, NULL);

  return found;
}

/*
 * qo_hq_has_unsafe_hash_key () - checks the CONNECT BY predicate for an
 *   equality conjunct that is unsafe as a hash key (see qo_hq_is_unsafe_hash_key)
 */
static bool
qo_hq_has_unsafe_hash_key (PARSER_CONTEXT * parser, PT_NODE * pred)
{
  for (; pred != NULL; pred = pred->next)
    {
      if (PT_IS_EXPR_NODE_WITH_OPERATOR (pred, PT_AND))
	{
	  if (qo_hq_has_unsafe_hash_key (parser, pred->info.expr.arg1)
	      || qo_hq_has_unsafe_hash_key (parser, pred->info.expr.arg2))
	    {
	      return true;
	    }
	  continue;
	}

      if (PT_IS_EXPR_NODE_WITH_OPERATOR (pred, PT_EQ) && qo_hq_is_unsafe_hash_key (parser, pred))
	{
	  return true;
	}
    }

  return false;
}

/*
 * qo_hq_prefer_hash_over_single_table () - decides whether a single-table
 *   CONNECT BY should give up the single-table optimization in favor of the
 *   generic path, whose hash list scan finds children in O(N+E) instead of
 *   rescanning the table for every parent row
 *   return: true to use the generic (materialize + hash list scan) path
 * Note: the single-table plan is kept only when the leading columns of an
 *       index are all fixed by equalities of the CONNECT BY predicate (to a
 *       constant, a host variable or a PRIOR expression, or to one of several
 *       such values by IN or OR) and one of them is the bare build-side column
 *       of a hashable equality, since only such an index finds the children of
 *       a parent by a key range: (pid) or (grp, pid) for PRIOR id = pid AND
 *       grp = 7 or grp IN (7, 8). An expression over the column
 *       (PRIOR id = pid + 0) cannot use any index. It is also kept when an
 *       equality conjunct is unsafe as a hash key (see
 *       qo_hq_is_unsafe_hash_key): a JSON side, a build side with a cast
 *       that can fail, or a NUMERIC pair whose DOUBLE hash can round equal
 *       values apart.
 */
static bool
qo_hq_prefer_hash_over_single_table (PARSER_CONTEXT * parser, PT_NODE * node, PT_NODE * spec)
{
  PT_NODE *connect_by = node->info.query.q.select.connect_by;
  SM_CLASS_CONSTRAINT *constraints;

  /* the user asked for the single-table plan shape explicitly */
  if (node->info.query.q.select.hint & (PT_HINT_NO_HASH_LIST_SCAN | PT_HINT_USE_IDX | PT_HINT_USE_IDX_DESC))
    {
      return false;
    }
  if (node->info.query.q.select.using_index != NULL)
    {
      return false;
    }

  if (!qo_hq_has_hashable_eq_term (parser, connect_by))
    {
      return false;
    }

  /* every equality conjunct becomes a hash key */
  if (qo_hq_has_unsafe_hash_key (parser, connect_by))
    {
      return false;
    }

  constraints = sm_class_constraints (spec->info.spec.entity_name->info.name.db_object);
  if (constraints != NULL && qo_hq_index_serves_child_lookup (parser, connect_by, spec, constraints))
    {
      return false;
    }

  return true;
}

/*
 * qo_check_generate_single_tbl_connect_by () - checks a SELECT ... CONNECT BY
 *                                              query for single-table
 *                                              optimizations
 *   return: whether single-table optimization can be performed
 *   parser(in): parser environment
 *   node(in): SELECT ... CONNECT BY query
 * Note: The single-table optimizations (potentially using indexes for table
 *       access in START WITH and CONNECT BY predicates) can be performed if
 *       the query does not involve joins or partitioned tables.
 */
bool
qo_check_generate_single_tbl_connect_by (PARSER_CONTEXT * parser, PT_NODE * node)
{
  int level = 0;
  PT_NODE *name = NULL;
  PT_NODE *spec = NULL;
  PT_NODE *select = NULL;

  assert (node->node_type == PT_SELECT && node->info.query.q.select.connect_by != NULL);

  spec = node->info.query.q.select.from;

  if (node->info.query.q.select.where || spec->next)
    {
      /* joins */
      return false;
    }

  select = node->info.query.q.select.list;

  while (select != NULL)
    {
      if (select->node_type == PT_METHOD_CALL)
	{
	  /* method call can be rewritten as subquery later. */
	  return false;
	}
      select = select->next;
    }

  qo_get_optimization_param (&level, QO_PARAM_LEVEL);
  if (!OPTIMIZATION_ENABLED (level))
    {
      return false;
    }

  assert (spec->next == NULL);
  if (spec->node_type != PT_SPEC)
    {
      assert (false);
      return false;
    }

  if (spec->info.spec.only_all != PT_ONLY)
    {
      /* class hierarchy */
      return false;
    }

  name = spec->info.spec.entity_name;
  if (name == NULL)
    {
      return false;
    }
  assert (name->node_type == PT_NAME);
  if (name == NULL || name->node_type != PT_NAME)
    {
      assert (false);
      return false;
    }

  if (sm_is_partitioned_class (name->info.name.db_object) > 0)
    {
      return false;
    }

  if (qo_hq_prefer_hash_over_single_table (parser, node, spec))
    {
      /* no index can serve the child lookups: the generic path finds children
       * by hash instead of rescanning the table per parent row */
      return false;
    }

  return true;
}

static PT_NODE *
qo_rewrite_nonnull_count (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
{
  PT_NODE *from = (PT_NODE *) arg;
  PT_NODE *count_arg;
  PT_NAME_INFO *name_info;
  MOP cls;
  SM_CLASS_CONSTRAINT *consp, *cons_iter;
  SM_ATTRIBUTE *attrp;
  PT_NODE *spec_node;
  int i;
  const char *class_name;
  bool is_rewrite_to_count_star_available = false;
  if (node->node_type == PT_FUNCTION && node->info.function.function_type == PT_COUNT
      && node->info.function.all_or_distinct == PT_ALL)
    {
      count_arg = node->info.function.arg_list;
      assert (count_arg->next == NULL);
      if (count_arg->node_type == PT_NAME)
	{
	  name_info = &count_arg->info.name;
	  spec_node = pt_find_entity (parser, from, name_info->spec_id);
	  if (spec_node == NULL || spec_node->info.spec.entity_name == NULL
	      || spec_node->info.spec.join_type == PT_JOIN_LEFT_OUTER)
	    {
	      return node;
	    }
	  class_name = spec_node->info.spec.entity_name->info.name.original;
	  if (class_name == NULL)
	    {
	      return node;
	    }
	  cls = sm_find_class (class_name);
	  if (cls == NULL)
	    {
	      return node;
	    }
	  consp = sm_class_constraints (cls);
	  if (consp == NULL)
	    {
	      return node;
	    }
	  for (cons_iter = consp; cons_iter != NULL && !is_rewrite_to_count_star_available; cons_iter = cons_iter->next)
	    {
	      if (SM_IS_CONSTRAINT_NOT_NULL_FAMILY (cons_iter->type))
		{
		  for (i = 0; cons_iter->attributes[i]; i++)
		    {
		      attrp = cons_iter->attributes[i];
		      if (intl_identifier_casecmp (count_arg->info.name.original, attrp->header.name) == 0)
			{
			  is_rewrite_to_count_star_available = true;
			  break;
			}
		    }
		}
	    }
	  for (cons_iter = consp; cons_iter != NULL; cons_iter = cons_iter->next)
	    {
	      if (cons_iter->filter_predicate)
		{
		  is_rewrite_to_count_star_available = false;
		}
	    }
	  if (is_rewrite_to_count_star_available)
	    {
	      node->info.function.function_type = PT_COUNT_STAR;
	      parser_free_tree (parser, node->info.function.arg_list);
	      node->info.function.arg_list = NULL;
	    }
	}
    }
  return node;
}

void
qo_rewrite_nonnull_count_select_list (PARSER_CONTEXT * parser, PT_NODE * select)
{
  bool is_rewrite_to_count_star_available = false;
  PT_NODE *select_list = select->info.query.q.select.list;
  PT_NODE *from = select->info.query.q.select.from;
  PT_NODE *spec;
  SM_ATTRIBUTE *attrp;
  int i;
  int continue_walk = PT_LEAF_WALK;
  for (spec = from; spec; spec = spec->next)
    {
      if (spec->info.spec.join_type == PT_JOIN_RIGHT_OUTER || spec->info.spec.join_type == PT_JOIN_FULL_OUTER
	  || spec->info.spec.join_type == PT_JOIN_CROSS)
	{
	  return;
	}
    }
  parser_walk_tree (parser, select_list, qo_rewrite_nonnull_count, (void *) from, NULL, NULL);
}

/*
 * Group-by placement (CBRD-28183)
 *
 * A SELECT that joins tables and aggregates may group a part of the tables before the join, in a derived table that
 * groups them on the columns the rest of the query reads from them: the join then meets one row per group instead
 * of one per row, and the block re-aggregates the partial results (W. P. Yan and P.-A. Larson, "Eager Aggregation
 * and Lazy Aggregation", VLDB 1995):
 *
 *   select c_custkey, count (o_orderkey)
 *   from customer left outer join orders on c_custkey = o_custkey and o_comment not like '%special%requests%'
 *   group by c_custkey
 *     ->
 *   select c_custkey, nvl (sum (nvl (gbp.c, 0)), 0)
 *   from customer left outer join (select o_custkey, count (o_orderkey) c from orders
 *                                  where o_comment not like '%special%requests%' group by o_custkey) gbp
 *        on c_custkey = gbp.o_custkey
 *   group by c_custkey
 *
 * Whether it pays depends on how many rows the grouping folds and on what the join would have read without it (a
 * selective condition on the other tables keeps an index join cheaper), so the rewriter plans the block as it is and
 * every rewritten form of it with the planner and keeps the cheapest (S. Chaudhuri and K. Shim, "Including Group-By
 * in Query Optimization", VLDB 1994).  The plans are thrown away; XASL generation plans the chosen form again.
 *
 * The tables grouped early (C below) hold every aggregate argument and are joined to the others by equalities of
 * one column on each side only, compared in the same domain and collation, so that the rows of a group meet the
 * same rows of the other tables.  The grouping key is every column of C the block reads outside the aggregates.
 * C is either tables joined in the WHERE, or the one table on the null-supplying side of an outer join; the latter
 * needs the aggregate arguments to be NULL whenever their columns are, which the compensation below relies on:
 *
 *   SUM (x), MIN (x), MAX (x)   -> same function of the partial value
 *   COUNT (x)                   -> NVL (SUM (NVL (partial COUNT (x), 0)), 0)
 *   COUNT (*)                   -> NVL (SUM (NVL (partial COUNT (*), 1)), 0)   (an unmatched row counts once)
 *   AVG (x)                     -> SUM (partial SUM (x)) / SUM (partial COUNT (x)) in DOUBLE
 *
 * each cast back to the type of the aggregate it replaces.  When every table left in the block is fixed by the
 * GROUP BY columns through its keys and the join equalities, a group meets one row and the block drops its GROUP BY.
 *
 * The NO_PUSH_GROUP_BY hint keeps a block as it is.
 */

#define QO_GBP_MAX_SPECS 16	/* tables a block may join for the rewrite to look at it */
#define QO_GBP_EXHAUSTIVE_SPECS 6	/* free tables up to which every set C is planned; past it C grows one table at a
					 * time */
#define QO_GBP_ADDED_GROUPING_GAIN 0.7	/* cost ratio under which a block that keeps its GROUP BY is rewritten */
#define QO_GBP_MIN_GROUP_SIZE 2.0	/* rows a group of the derived table must fold, estimated, when the block keeps its
					 * GROUP BY */

/* hints that name no table and also apply to the derived table's SELECT */
#define QO_GBP_DERIVED_HINTS \
  (PT_HINT_NO_PARALLEL_SCAN | PT_HINT_NO_PARALLEL_HASH_JOIN | PT_HINT_NO_PARALLEL_SUBQUERY | PT_HINT_NO_HASH_AGGREGATE \
   | PT_HINT_NO_HASH_LIST_SCAN | PT_HINT_NO_SORT_LIMIT | PT_HINT_NO_MULTI_RANGE_OPT | PT_HINT_NO_COVERING_IDX \
   | PT_HINT_NO_INDEX_SS | PT_HINT_NO_INDEX_LS | PT_HINT_PARALLEL)

/* hints a block may hold and still be rewritten; one naming tables (ORDERED, LEADING, USE_NL ...) keeps it as it
 * is, since C's tables leave it */
#define QO_GBP_ALLOWED_HINTS (QO_GBP_DERIVED_HINTS | PT_HINT_RECOMPILE | PT_HINT_QUERY_CACHE)

typedef unsigned int QO_GBP_MASK;

typedef struct qo_gbp_env QO_GBP_ENV;
struct qo_gbp_env
{
  PT_NODE *query;
  int nspecs;
  PT_NODE *specs[QO_GBP_MAX_SPECS];
  QO_GBP_MASK at_mask;		/* tables the aggregate arguments read */
  QO_GBP_MASK inner_mask;	/* tables joined in the WHERE that C may take */
  QO_GBP_MASK nullable_mask;	/* null-supplying tables of an outer join that C may take alone */
  QO_GBP_MASK adj[QO_GBP_MAX_SPECS];	/* tables joined to each by an equality in the WHERE */
  QO_GBP_MASK edge[QO_GBP_MAX_SPECS];	/* tables joined to each by an equality in the WHERE or an ON */
  bool args_null_strict;	/* every aggregate argument is NULL when one of its columns is */
  bool has_groupby_num;		/* groupby_num () counts the groups of the block */
};

typedef struct qo_gbp_walk QO_GBP_WALK;
struct qo_gbp_walk
{
  QO_GBP_ENV *env;
  QO_GBP_MASK mask;		/* tables the walked tree reads */
  int naggs;			/* aggregates met */
  bool has_name_outside_agg;	/* a column read outside an aggregate */
  bool bad;			/* the tree cannot be moved or re-evaluated */
};

typedef struct qo_gbp_build QO_GBP_BUILD;
struct qo_gbp_build
{
  QO_GBP_ENV *env;
  QO_GBP_MASK cmask;
  PT_NODE *dquery;		/* the derived table's SELECT */
  PT_NODE *dspec;		/* its spec in the block */
  PT_NODE *needed;		/* the columns of C the block reads, a list of PT_NAME */
  PT_NODE *attrs;		/* the derived table columns that hold them, in the same order */
  int ncols;
  int seq;
  bool remove_group_by;
  bool nullable;		/* C is the null-supplying table of an outer join */
  bool failed;
};

static int qo_gbp_spec_index (QO_GBP_ENV * env, UINTPTR spec_id);
static PT_NODE *qo_gbp_check_expr_pre (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk);
static bool qo_gbp_is_null_strict (PT_NODE * node);
static bool qo_gbp_is_image_equal (PT_NODE * node);
static bool qo_gbp_is_same_domain (PT_NODE * a, PT_NODE * b);
static bool qo_gbp_name_is_grouped (QO_GBP_ENV * env, PT_NODE * name);
static PT_NODE *qo_gbp_check_output_pre (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk);
static bool qo_gbp_check_term (PARSER_CONTEXT * parser, QO_GBP_ENV * env, PT_NODE * term, QO_GBP_MASK * maskp);
static bool qo_gbp_analyze (PARSER_CONTEXT * parser, PT_NODE * query, QO_GBP_ENV * env);
static QO_GBP_MASK qo_gbp_term_mask (PARSER_CONTEXT * parser, QO_GBP_ENV * env, PT_NODE * term);
static PT_NODE *qo_gbp_collect_needed_pre (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk);
static PT_NODE *qo_gbp_needed_columns (PARSER_CONTEXT * parser, QO_GBP_ENV * env, QO_GBP_MASK cmask);
static bool qo_gbp_needed_is_image_equal (PT_NODE * needed);
static bool qo_gbp_needed_has_key (QO_GBP_ENV * env, QO_GBP_MASK cmask, PT_NODE * needed);
static bool qo_gbp_can_remove_group_by (PARSER_CONTEXT * parser, QO_GBP_ENV * env, QO_GBP_MASK cmask, PT_NODE * needed);
static PT_NODE *qo_gbp_add_column (PARSER_CONTEXT * parser, QO_GBP_BUILD * build, PT_NODE * expr, bool group);
static PT_NODE *qo_gbp_make_agg (PARSER_CONTEXT * parser, FUNC_CODE function, PT_NODE * arg);
static PT_NODE *qo_gbp_cast_to (PARSER_CONTEXT * parser, PT_NODE * expr, PT_NODE * like);
static PT_NODE *qo_gbp_replace_agg_post (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk);
static PT_NODE *qo_gbp_replace_name_pre (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk);
static PT_NODE *qo_gbp_transform (PARSER_CONTEXT * parser, QO_GBP_ENV * env, QO_GBP_MASK cmask, bool remove_group_by);
static bool qo_gbp_is_connected (QO_GBP_ENV * env, QO_GBP_MASK cmask);
static bool qo_gbp_is_candidate (QO_GBP_ENV * env, QO_GBP_MASK cmask);
static double qo_gbp_plan_cost (PARSER_CONTEXT * parser, PT_NODE * select, double *in_rows, double *out_rows,
				int *projected_size);
static double qo_gbp_candidate_cost (PARSER_CONTEXT * parser, PT_NODE * query, QO_GBP_MASK cmask);

#define QO_GBP_BIT(i) ((QO_GBP_MASK) 1 << (i))

/*
 * qo_gbp_popcount () - the number of tables in a mask
 */
static int
qo_gbp_popcount (QO_GBP_MASK mask)
{
  int n = 0;

  while (mask)
    {
      mask &= mask - 1;
      n++;
    }
  return n;
}

/*
 * qo_gbp_spec_index () - the position of a spec of the block in its FROM, -1 for another spec
 */
static int
qo_gbp_spec_index (QO_GBP_ENV * env, UINTPTR spec_id)
{
  int i;

  for (i = 0; i < env->nspecs; i++)
    {
      if (env->specs[i]->info.spec.id == spec_id)
	{
	  return i;
	}
    }
  return -1;
}

/*
 * qo_gbp_check_expr_pre () - collect the tables an expression reads, and refuse what it cannot evaluate twice or
 *			      somewhere else: a subquery, a method, a path, a column of another block, and the
 *			      functions whose value depends on the row order or changes per call
 */
static PT_NODE *
qo_gbp_check_expr_pre (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
{
  QO_GBP_WALK *walk = (QO_GBP_WALK *) arg;
  int idx;

  if (walk->bad)
    {
      *continue_walk = PT_STOP_WALK;
      return node;
    }

  switch (node->node_type)
    {
    case PT_SELECT:
    case PT_UNION:
    case PT_DIFFERENCE:
    case PT_INTERSECTION:
    case PT_METHOD_CALL:
    case PT_DOT_:
      walk->bad = true;
      break;

    case PT_FUNCTION:
      if (node->info.function.analytic.is_analytic || node->info.function.function_type == PT_GROUPBY_NUM)
	{
	  walk->bad = true;
	}
      break;

    case PT_NAME:
      if (node->info.name.meta_class != PT_NORMAL)
	{
	  walk->bad = true;
	  break;
	}
      idx = qo_gbp_spec_index (walk->env, node->info.name.spec_id);
      if (idx < 0)
	{
	  walk->bad = true;
	  break;
	}
      walk->mask |= QO_GBP_BIT (idx);
      break;

    case PT_EXPR:
      switch (node->info.expr.op)
	{
	case PT_RAND:
	case PT_RANDOM:
	case PT_DRAND:
	case PT_DRANDOM:
	case PT_SYS_GUID:
	case PT_UUID:
	case PT_CURRENT_VALUE:
	case PT_NEXT_VALUE:
	case PT_INST_NUM:
	case PT_ROWNUM:
	case PT_ORDERBY_NUM:
	case PT_LEVEL:
	case PT_PRIOR:
	case PT_CONNECT_BY_ROOT:
	case PT_SYS_CONNECT_BY_PATH:
	case PT_CONNECT_BY_ISLEAF:
	case PT_CONNECT_BY_ISCYCLE:
	case PT_DEFINE_VARIABLE:
	case PT_EVALUATE_VARIABLE:
	  walk->bad = true;
	  break;
	default:
	  break;
	}
      break;

    default:
      break;
    }

  if (walk->bad)
    {
      *continue_walk = PT_STOP_WALK;
    }

  return node;
}

/*
 * qo_gbp_is_null_strict () - whether an aggregate argument is NULL whenever one of its columns is NULL: a column, or
 *			      numeric arithmetic over at least one such operand (a NULL-extended row of an outer join
 *			      must leave the aggregate as an unmatched row of the derived table does)
 */
static bool
qo_gbp_is_null_strict (PT_NODE * node)
{
  if (node == NULL)
    {
      return false;
    }

  if (node->node_type == PT_NAME)
    {
      return true;
    }

  if (node->node_type != PT_EXPR || !PT_IS_NUMERIC_TYPE (node->type_enum))
    {
      return false;
    }

  switch (node->info.expr.op)
    {
    case PT_UNARY_MINUS:
      return qo_gbp_is_null_strict (node->info.expr.arg1);
    case PT_PLUS:
    case PT_MINUS:
    case PT_TIMES:
    case PT_DIVIDE:
      return qo_gbp_is_null_strict (node->info.expr.arg1) || qo_gbp_is_null_strict (node->info.expr.arg2);
    default:
      return false;
    }
}

/*
 * qo_gbp_is_image_equal () - whether two values of a column that compare equal are the same value, so that it does
 *			      not matter which row of a group gives the group its value: no string collation that
 *			      folds case, accents or trailing spaces, no time zone (PostgreSQL asks the same of an
 *			      eager aggregation key, btequalimage)
 */
static bool
qo_gbp_is_image_equal (PT_NODE * node)
{
  int coll;

  if (PT_IS_NUMERIC_TYPE (node->type_enum) || node->type_enum == PT_TYPE_DATE || node->type_enum == PT_TYPE_TIME
      || node->type_enum == PT_TYPE_TIMESTAMP || node->type_enum == PT_TYPE_DATETIME)
    {
      return true;
    }

  if (!PT_HAS_COLLATION (node->type_enum) || node->data_type == NULL
      || prm_get_bool_value (PRM_ID_IGNORE_TRAILING_SPACE))
    {
      return false;
    }

  coll = node->data_type->info.data_type.collation_id;
  return coll == LANG_COLL_ISO_BINARY || coll == LANG_COLL_UTF8_BINARY || coll == LANG_COLL_EUCKR_BINARY
    || coll == LANG_COLL_BINARY;
}

/*
 * qo_gbp_is_same_domain () - whether the two sides of a join equality compare as GROUP BY compares either of them:
 *			      the same type and, for strings, the same collation, so no cast comes between them
 */
static bool
qo_gbp_is_same_domain (PT_NODE * a, PT_NODE * b)
{
  if (a->type_enum != b->type_enum || !qo_gbp_is_image_equal (a) || !qo_gbp_is_image_equal (b))
    {
      return false;
    }

  if (PT_HAS_COLLATION (a->type_enum))
    {
      return a->data_type->info.data_type.collation_id == b->data_type->info.data_type.collation_id;
    }

  return true;
}

/*
 * qo_gbp_name_is_grouped () - whether a column the block reads outside an aggregate keeps one value per group: it is
 *			       a GROUP BY column, or a key of its table is (see qo_remove_useless_groupby_columns ())
 */
static bool
qo_gbp_name_is_grouped (QO_GBP_ENV * env, PT_NODE * name)
{
  PT_NODE *group_by = env->query->info.query.q.select.group_by, *group, *col, *spec;
  DB_OBJECT *classop = NULL;
  SM_CLASS_CONSTRAINT *cons;
  int idx;

  for (group = group_by; group != NULL; group = group->next)
    {
      col = group->info.sort_spec.expr;
      if (col->node_type == PT_NAME && col->info.name.spec_id == name->info.name.spec_id
	  && intl_identifier_casecmp (col->info.name.original, name->info.name.original) == 0)
	{
	  return true;
	}
    }

  idx = qo_gbp_spec_index (env, name->info.name.spec_id);
  if (idx < 0 || group_by == NULL)
    {
      return false;
    }

  spec = env->specs[idx];
  PT_SPEC_GET_DB_OBJECT (spec, classop);
  if (classop == NULL)
    {
      return false;
    }

  for (cons = sm_class_constraints (classop); cons != NULL; cons = cons->next)
    {
      if (qo_is_row_identifying_key (cons) && qo_groupby_has_key (group_by, spec->info.spec.id, cons))
	{
	  return true;
	}
    }

  return false;
}

/*
 * qo_gbp_check_output_pre () - check an item of the select list, the HAVING or the ORDER BY: the aggregates are
 *				re-aggregated, and the columns outside them must keep one value per group
 */
static PT_NODE *
qo_gbp_check_output_pre (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
{
  QO_GBP_WALK *walk = (QO_GBP_WALK *) arg;
  QO_GBP_WALK arg_walk;
  PT_NODE *agg_arg;
  FUNC_CODE function;

  if (walk->bad)
    {
      *continue_walk = PT_STOP_WALK;
      return node;
    }

  if (node->node_type == PT_FUNCTION && node->info.function.function_type == PT_GROUPBY_NUM)
    {
      /* LIMIT over the groups counts the groups of the block, which keeps its GROUP BY for it */
      walk->env->has_groupby_num = true;
      return node;
    }

  if (pt_is_aggregate_function (parser, node))
    {
      /* the aggregate is replaced as a whole: its argument is not read outside it */
      *continue_walk = PT_LIST_WALK;
      walk->naggs++;

      function = node->info.function.function_type;
      if (node->info.function.all_or_distinct != PT_ALL || node->info.function.order_by != NULL
	  || (function != PT_SUM && function != PT_COUNT && function != PT_COUNT_STAR && function != PT_AVG
	      && function != PT_MIN && function != PT_MAX))
	{
	  walk->bad = true;
	  *continue_walk = PT_STOP_WALK;
	  return node;
	}

      if (function == PT_COUNT_STAR)
	{
	  return node;
	}

      agg_arg = node->info.function.arg_list;
      if (agg_arg == NULL || agg_arg->next != NULL)
	{
	  walk->bad = true;
	  *continue_walk = PT_STOP_WALK;
	  return node;
	}

      arg_walk.env = walk->env;
      arg_walk.mask = 0;
      arg_walk.naggs = 0;
      arg_walk.has_name_outside_agg = false;
      arg_walk.bad = false;
      (void) parser_walk_tree (parser, agg_arg, qo_gbp_check_expr_pre, &arg_walk, NULL, NULL);

      /* an argument over several tables (SUM (a.x * b.y)) has no table to be grouped with */
      if (arg_walk.bad || qo_gbp_popcount (arg_walk.mask) > 1)
	{
	  walk->bad = true;
	  *continue_walk = PT_STOP_WALK;
	  return node;
	}

      /* AVG adds in DOUBLE over a NUMERIC or a floating argument: partial sums of another order would round
       * differently; over integers the sum is exact */
      if (function == PT_AVG && agg_arg->type_enum != PT_TYPE_SMALLINT && agg_arg->type_enum != PT_TYPE_INTEGER
	  && agg_arg->type_enum != PT_TYPE_BIGINT)
	{
	  walk->bad = true;
	  *continue_walk = PT_STOP_WALK;
	  return node;
	}

      /* MIN and MAX of values that compare equal return the first one met: partial results met in another order
       * would return another */
      if ((function == PT_MIN || function == PT_MAX) && !qo_gbp_is_image_equal (agg_arg))
	{
	  walk->bad = true;
	  *continue_walk = PT_STOP_WALK;
	  return node;
	}

      walk->env->at_mask |= arg_walk.mask;
      if (!qo_gbp_is_null_strict (agg_arg))
	{
	  walk->env->args_null_strict = false;
	}
      return node;
    }

  switch (node->node_type)
    {
    case PT_NAME:
      walk->has_name_outside_agg = true;
      if (node->info.name.meta_class != PT_NORMAL || !qo_gbp_name_is_grouped (walk->env, node))
	{
	  walk->bad = true;
	}
      break;

    default:
      {
	/* the same refusals as the expressions moved, for what is left around the aggregates */
	QO_GBP_WALK tmp = *walk;
	int cw = PT_CONTINUE_WALK;

	(void) qo_gbp_check_expr_pre (parser, node, &tmp, &cw);
	if (tmp.bad && node->node_type != PT_NAME)
	  {
	    walk->bad = true;
	  }
      }
      break;
    }

  if (walk->bad)
    {
      *continue_walk = PT_STOP_WALK;
    }

  return node;
}

/*
 * qo_gbp_check_term () - check a conjunct of the WHERE or of an ON: AND only, and a join of two tables is an equality
 *			  of their columns in the same domain
 *   return: false if the block cannot be rewritten
 *   maskp(out): the tables the term reads
 */
static bool
qo_gbp_check_term (PARSER_CONTEXT * parser, QO_GBP_ENV * env, PT_NODE * term, QO_GBP_MASK * maskp)
{
  QO_GBP_WALK walk;
  PT_NODE *arg1, *arg2;

  if (term->or_next != NULL || (term->node_type == PT_EXPR && term->info.expr.op == PT_OR))
    {
      return false;
    }

  walk.env = env;
  walk.mask = 0;
  walk.naggs = 0;
  walk.has_name_outside_agg = false;
  walk.bad = false;
  (void) parser_walk_tree (parser, term, qo_gbp_check_expr_pre, &walk, NULL, NULL);
  if (walk.bad)
    {
      return false;
    }

  *maskp = walk.mask;

  if (qo_gbp_popcount (walk.mask) <= 1)
    {
      return true;
    }

  if (qo_gbp_popcount (walk.mask) > 2 || term->node_type != PT_EXPR || term->info.expr.op != PT_EQ)
    {
      return false;
    }

  arg1 = term->info.expr.arg1;
  arg2 = term->info.expr.arg2;
  if (arg1 == NULL || arg2 == NULL || arg1->node_type != PT_NAME || arg2->node_type != PT_NAME
      || !qo_gbp_is_same_domain (arg1, arg2))
    {
      return false;
    }

  return true;
}

/*
 * qo_gbp_term_mask () - the tables a checked term reads
 */
static QO_GBP_MASK
qo_gbp_term_mask (PARSER_CONTEXT * parser, QO_GBP_ENV * env, PT_NODE * term)
{
  QO_GBP_WALK walk;
  PT_NODE *next = term->next;

  walk.env = env;
  walk.mask = 0;
  walk.naggs = 0;
  walk.has_name_outside_agg = false;
  walk.bad = false;
  term->next = NULL;
  (void) parser_walk_tree (parser, term, qo_gbp_check_expr_pre, &walk, NULL, NULL);
  term->next = next;

  return walk.mask;
}

/*
 * qo_gbp_analyze () - check that a block may take the rewrite and find which tables its derived table may group
 *   return: false if the block is left as it is
 */
static bool
qo_gbp_analyze (PARSER_CONTEXT * parser, PT_NODE * query, QO_GBP_ENV * env)
{
  PT_NODE *spec, *term, *next, *item;
  QO_GBP_WALK walk;
  QO_GBP_MASK mask, where_ref_mask = 0, on_ref_mask = 0, a, b;
  int i, j, k;
  bool has_join_edge;

  memset (env, 0, sizeof (QO_GBP_ENV));
  env->query = query;
  env->args_null_strict = true;

  if (query->node_type != PT_SELECT || query->info.query.q.select.flavor != PT_USER_SELECT)
    {
      return false;
    }

  if ((query->info.query.q.select.hint & ~QO_GBP_ALLOWED_HINTS) != 0 || query->info.query.correlation_level != 0
      || query->info.query.q.select.connect_by != NULL || query->info.query.q.select.start_with != NULL
      || query->info.query.q.select.after_cb_filter != NULL || query->info.query.upd_del_class_cnt > 0
      || query->info.query.scan_op_type != S_SELECT || query->info.query.oids_included != 0
      || query->info.query.flag.is_insert_select || query->info.query.flag.single_tuple
      || query->info.query.is_subquery == PT_IS_CTE_REC_SUBQUERY || query->flag.is_system_generated_stmt
      || query->info.query.q.select.using_index != NULL)
    {
      return false;
    }

  if (!PT_SELECT_INFO_IS_FLAGED (query, PT_SELECT_INFO_HAS_AGG) || pt_has_analytic (parser, query)
      || (query->info.query.q.select.group_by != NULL && query->info.query.q.select.group_by->flag.with_rollup))
    {
      return false;
    }

  /* FROM: tables only, joined in the WHERE or by LEFT / RIGHT OUTER JOIN */
  for (spec = query->info.query.q.select.from; spec != NULL; spec = spec->next)
    {
      if (env->nspecs >= QO_GBP_MAX_SPECS || spec->node_type != PT_SPEC || spec->info.spec.derived_table != NULL
	  || PT_SPEC_IS_CTE (spec) || spec->info.spec.entity_name == NULL || spec->info.spec.only_all == PT_ALL
	  || spec->info.spec.path_entities != NULL || spec->info.spec.meta_class == PT_META_CLASS
	  || spec->info.spec.flat_entity_list == NULL || spec->info.spec.flat_entity_list->next != NULL)
	{
	  return false;
	}

      switch (spec->info.spec.join_type)
	{
	case PT_JOIN_NONE:
	case PT_JOIN_CROSS:
	case PT_JOIN_LEFT_OUTER:
	case PT_JOIN_RIGHT_OUTER:
	  break;
	case PT_JOIN_INNER:
	  if (spec->info.spec.on_cond != NULL)
	    {
	      return false;
	    }
	  break;
	default:
	  return false;
	}

      env->specs[env->nspecs++] = spec;
    }

  if (env->nspecs < 2)
    {
      return false;
    }

  /* select list, HAVING, ORDER BY: one aggregate per item and nothing that is not grouped beside it */
  for (item = query->info.query.q.select.list; item != NULL; item = item->next)
    {
      next = item->next;
      item->next = NULL;
      walk.env = env;
      walk.mask = 0;
      walk.naggs = 0;
      walk.has_name_outside_agg = false;
      walk.bad = false;
      (void) parser_walk_tree (parser, item, qo_gbp_check_output_pre, &walk, NULL, NULL);
      item->next = next;
      if (walk.bad || walk.naggs > 1 || (walk.naggs == 1 && walk.has_name_outside_agg))
	{
	  return false;
	}
    }

  walk.env = env;
  walk.mask = 0;
  walk.naggs = 0;
  walk.has_name_outside_agg = false;
  walk.bad = false;
  (void) parser_walk_tree (parser, query->info.query.q.select.having, qo_gbp_check_output_pre, &walk, NULL, NULL);
  (void) parser_walk_tree (parser, query->info.query.order_by, qo_gbp_check_output_pre, &walk, NULL, NULL);
  if (walk.bad)
    {
      return false;
    }

  for (item = query->info.query.q.select.group_by; item != NULL; item = item->next)
    {
      /* the group takes its value from one of its rows, met in another order once the plan changes */
      if (item->info.sort_spec.expr == NULL || item->info.sort_spec.expr->node_type != PT_NAME
	  || qo_gbp_spec_index (env, item->info.sort_spec.expr->info.name.spec_id) < 0
	  || !qo_gbp_is_image_equal (item->info.sort_spec.expr))
	{
	  return false;
	}
    }

  if (env->at_mask == 0)
    {
      /* only COUNT (*): grouping a table without aggregate argument would need its count multiplied into the others */
      return false;
    }

  /* WHERE */
  for (term = query->info.query.q.select.where; term != NULL; term = term->next)
    {
      next = term->next;
      term->next = NULL;
      if (!qo_gbp_check_term (parser, env, term, &mask))
	{
	  term->next = next;
	  return false;
	}
      term->next = next;

      where_ref_mask |= mask;
      if (qo_gbp_popcount (mask) == 2)
	{
	  i = qo_gbp_spec_index (env, term->info.expr.arg1->info.name.spec_id);
	  j = qo_gbp_spec_index (env, term->info.expr.arg2->info.name.spec_id);
	  env->adj[i] |= QO_GBP_BIT (j);
	  env->adj[j] |= QO_GBP_BIT (i);
	  env->edge[i] |= QO_GBP_BIT (j);
	  env->edge[j] |= QO_GBP_BIT (i);
	}
    }

  /* the tables joined in the WHERE, and the null-supplying tables, that C may take */
  for (k = 0; k < env->nspecs; k++)
    {
      spec = env->specs[k];

      if (spec->info.spec.join_type != PT_JOIN_LEFT_OUTER && spec->info.spec.join_type != PT_JOIN_RIGHT_OUTER)
	{
	  if (spec->info.spec.on_cond == NULL)
	    {
	      env->inner_mask |= QO_GBP_BIT (k);
	    }
	  continue;
	}

      if (spec->info.spec.join_type == PT_JOIN_RIGHT_OUTER && k != 1)
	{
	  /* every table before it is null-supplying: only a single one is handled */
	  return false;
	}

      /* a LEFT OUTER JOIN supplies NULL for its own table, a RIGHT OUTER JOIN for the table before it */
      a = (spec->info.spec.join_type == PT_JOIN_LEFT_OUTER) ? QO_GBP_BIT (k) : QO_GBP_BIT (0);
      has_join_edge = false;

      for (term = spec->info.spec.on_cond; term != NULL; term = term->next)
	{
	  next = term->next;
	  term->next = NULL;
	  if (!qo_gbp_check_term (parser, env, term, &mask))
	    {
	      term->next = next;
	      return false;
	    }
	  term->next = next;

	  b = mask & ~a;
	  on_ref_mask |= b;
	  if ((mask & a) && b)
	    {
	      /* an equality between the null-supplying table and one table of the other side */
	      i = qo_gbp_spec_index (env, term->info.expr.arg1->info.name.spec_id);
	      j = qo_gbp_spec_index (env, term->info.expr.arg2->info.name.spec_id);
	      env->edge[i] |= QO_GBP_BIT (j);
	      env->edge[j] |= QO_GBP_BIT (i);
	      has_join_edge = true;
	    }
	}

      /* a WHERE term over it reads it after the join, NULL-extended */
      if (has_join_edge && !(where_ref_mask & a))
	{
	  env->nullable_mask |= a;
	}
      if (spec->info.spec.join_type == PT_JOIN_RIGHT_OUTER)
	{
	  env->inner_mask &= ~QO_GBP_BIT (0);
	}
    }

  /* a table an ON names besides its own null-supplying table must stay where the ON is */
  env->inner_mask &= ~on_ref_mask;
  env->nullable_mask &= ~on_ref_mask;

  return true;
}

/*
 * qo_gbp_is_connected () - whether the tables of C are joined to each other by equalities in the WHERE
 */
static bool
qo_gbp_is_connected (QO_GBP_ENV * env, QO_GBP_MASK cmask)
{
  QO_GBP_MASK reached, prev;
  int i;

  for (i = 0; i < env->nspecs && !(cmask & QO_GBP_BIT (i)); i++)
    {
      ;
    }
  if (i == env->nspecs)
    {
      return false;
    }

  reached = QO_GBP_BIT (i);
  do
    {
      prev = reached;
      for (i = 0; i < env->nspecs; i++)
	{
	  if (reached & QO_GBP_BIT (i))
	    {
	      reached |= env->adj[i] & cmask;
	    }
	}
    }
  while (reached != prev);

  return reached == cmask;
}

/*
 * qo_gbp_is_candidate () - whether a set of tables may be grouped before the join
 */
static bool
qo_gbp_is_candidate (QO_GBP_ENV * env, QO_GBP_MASK cmask)
{
  QO_GBP_MASK all = (QO_GBP_MASK) ((1ULL << env->nspecs) - 1), other = 0;
  int i;

  if ((cmask & env->at_mask) != env->at_mask || cmask == all || cmask == 0)
    {
      return false;
    }

  if (qo_gbp_popcount (cmask) == 1 && (cmask & env->nullable_mask))
    {
      if (!env->args_null_strict)
	{
	  return false;
	}
    }
  else if ((cmask & env->inner_mask) != cmask || !qo_gbp_is_connected (env, cmask))
    {
      return false;
    }

  /* joined to the rest by an equality, which gives the derived table its grouping key */
  for (i = 0; i < env->nspecs; i++)
    {
      if (cmask & QO_GBP_BIT (i))
	{
	  other |= env->edge[i] & ~cmask;
	}
    }

  return other != 0;
}

/*
 * qo_gbp_collect_needed_pre () - collect the columns of C read outside the aggregates
 */
static PT_NODE *
qo_gbp_collect_needed_pre (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
{
  QO_GBP_BUILD *build = (QO_GBP_BUILD *) arg;
  PT_NODE *col;
  int idx;

  if (pt_is_aggregate_function (parser, node))
    {
      *continue_walk = PT_LIST_WALK;
      return node;
    }

  if (node->node_type != PT_NAME)
    {
      return node;
    }

  idx = qo_gbp_spec_index (build->env, node->info.name.spec_id);
  if (idx < 0 || !(build->cmask & QO_GBP_BIT (idx)))
    {
      return node;
    }

  for (col = build->needed; col != NULL; col = col->next)
    {
      if (col->info.name.spec_id == node->info.name.spec_id
	  && intl_identifier_casecmp (col->info.name.original, node->info.name.original) == 0)
	{
	  return node;
	}
    }

  col = parser_copy_tree (parser, node);
  if (col == NULL)
    {
      build->failed = true;
      return node;
    }
  build->needed = parser_append_node (col, build->needed);

  return node;
}

/*
 * qo_gbp_needed_columns () - the columns of C the block reads outside the aggregates and outside the terms that move
 *			      into the derived table: they become its grouping key
 */
static PT_NODE *
qo_gbp_needed_columns (PARSER_CONTEXT * parser, QO_GBP_ENV * env, QO_GBP_MASK cmask)
{
  QO_GBP_BUILD build;
  PT_NODE *query = env->query, *term, *next;
  int k;

  memset (&build, 0, sizeof (build));
  build.env = env;
  build.cmask = cmask;

#define QO_GBP_COLLECT(tree) \
  (void) parser_walk_tree (parser, (tree), qo_gbp_collect_needed_pre, &build, NULL, NULL)

  /* the group by first, so that the derived table groups in the order the block does */
  QO_GBP_COLLECT (query->info.query.q.select.group_by);
  QO_GBP_COLLECT (query->info.query.q.select.list);
  QO_GBP_COLLECT (query->info.query.q.select.having);
  QO_GBP_COLLECT (query->info.query.order_by);

  for (term = query->info.query.q.select.where; term != NULL; term = term->next)
    {
      if ((qo_gbp_term_mask (parser, env, term) & ~cmask) != 0)
	{
	  next = term->next;
	  term->next = NULL;
	  QO_GBP_COLLECT (term);
	  term->next = next;
	}
    }

  for (k = 0; k < env->nspecs; k++)
    {
      for (term = env->specs[k]->info.spec.on_cond; term != NULL; term = term->next)
	{
	  if ((qo_gbp_term_mask (parser, env, term) & ~cmask) != 0)
	    {
	      next = term->next;
	      term->next = NULL;
	      QO_GBP_COLLECT (term);
	      term->next = next;
	    }
	}
    }

#undef QO_GBP_COLLECT

  if (build.failed)
    {
      parser_free_tree (parser, build.needed);
      return NULL;
    }

  return build.needed;
}

/*
 * qo_gbp_needed_is_image_equal () - whether every grouping column of the derived table is (qo_gbp_is_image_equal ())
 */
static bool
qo_gbp_needed_is_image_equal (PT_NODE * needed)
{
  PT_NODE *col;

  for (col = needed; col != NULL; col = col->next)
    {
      if (!qo_gbp_is_image_equal (col))
	{
	  return false;
	}
    }
  return true;
}

/*
 * qo_gbp_needed_has_key () - whether a single table is grouped on one of its keys: every group would hold one row
 */
static bool
qo_gbp_needed_has_key (QO_GBP_ENV * env, QO_GBP_MASK cmask, PT_NODE * needed)
{
  PT_NODE *spec, *col;
  DB_OBJECT *classop = NULL;
  SM_CLASS_CONSTRAINT *cons;
  int i, k;

  if (qo_gbp_popcount (cmask) != 1)
    {
      return false;
    }

  for (k = 0; !(cmask & QO_GBP_BIT (k)); k++)
    {
      ;
    }
  spec = env->specs[k];
  PT_SPEC_GET_DB_OBJECT (spec, classop);
  if (classop == NULL)
    {
      return false;
    }

  for (cons = sm_class_constraints (classop); cons != NULL; cons = cons->next)
    {
      if (!qo_is_row_identifying_key (cons))
	{
	  continue;
	}

      for (i = 0; cons->attributes[i] != NULL; i++)
	{
	  for (col = needed; col != NULL; col = col->next)
	    {
	      if (intl_identifier_casecmp (col->info.name.original, cons->attributes[i]->header.name) == 0)
		{
		  break;
		}
	    }
	  if (col == NULL)
	    {
	      break;
	    }
	}

      if (cons->attributes[i] == NULL)
	{
	  return true;
	}
    }

  return false;
}

/*
 * qo_gbp_can_remove_group_by () - whether the block, once C is grouped, meets one row per group: every table left is
 *				   fixed by the GROUP BY columns, through a key of it whose columns are fixed, or
 *				   for the derived table through its grouping key, a column being fixed by an
 *				   equality with a fixed one or by a key of its table (C's tables included: their rows
 *				   vary inside a group, but a column fixed by their key does not)
 */
static bool
qo_gbp_can_remove_group_by (PARSER_CONTEXT * parser, QO_GBP_ENV * env, QO_GBP_MASK cmask, PT_NODE * needed)
{
  PT_NODE *query = env->query, *group, *term, *col, *fixed = NULL, *a1, *a2, *spec;
  QO_GBP_MASK determined = 0, all = (QO_GBP_MASK) ((1ULL << env->nspecs) - 1);
  DB_OBJECT *classop;
  SM_CLASS_CONSTRAINT *cons;
  bool changed, ok, dquery_fixed = false;
  int i, k, idx1;

#define QO_GBP_IS_FIXED(col_) \
  (((idx1 = qo_gbp_spec_index (env, (col_)->info.name.spec_id)) >= 0 && (determined & QO_GBP_BIT (idx1))) \
   || pt_find_name (parser, (col_), fixed) != NULL)

  if (query->info.query.q.select.group_by == NULL || query->info.query.q.select.having != NULL
      || query->info.query.order_by == NULL || env->has_groupby_num)
    {
      /* without ORDER BY the GROUP BY gives the output its order */
      return false;
    }

  for (k = 0; k < env->nspecs; k++)
    {
      if (env->specs[k]->info.spec.join_type == PT_JOIN_LEFT_OUTER
	  || env->specs[k]->info.spec.join_type == PT_JOIN_RIGHT_OUTER)
	{
	  return false;
	}
    }

  for (group = query->info.query.q.select.group_by; group != NULL; group = group->next)
    {
      fixed = parser_append_node (parser_copy_tree (parser, group->info.sort_spec.expr), fixed);
    }

  do
    {
      changed = false;

      /* the tables fixed through a key, C through its grouping key */
      for (k = 0; k < env->nspecs; k++)
	{
	  if (determined & QO_GBP_BIT (k))
	    {
	      continue;
	    }

	  spec = env->specs[k];
	  classop = NULL;
	  PT_SPEC_GET_DB_OBJECT (spec, classop);
	  if (classop == NULL)
	    {
	      continue;
	    }

	  for (cons = sm_class_constraints (classop); cons != NULL; cons = cons->next)
	    {
	      if (!qo_is_row_identifying_key (cons))
		{
		  continue;
		}

	      for (i = 0; cons->attributes[i] != NULL; i++)
		{
		  for (col = fixed; col != NULL; col = col->next)
		    {
		      if (col->info.name.spec_id == spec->info.spec.id
			  && intl_identifier_casecmp (col->info.name.original, cons->attributes[i]->header.name) == 0)
			{
			  break;
			}
		    }
		  if (col == NULL)
		    {
		      break;
		    }
		}

	      if (cons->attributes[i] == NULL)
		{
		  determined |= QO_GBP_BIT (k);
		  changed = true;
		  break;
		}
	    }
	}

      /* the derived table meets one row per group once its grouping key is fixed */
      if (!dquery_fixed)
	{
	  ok = true;
	  for (col = needed; col != NULL && ok; col = col->next)
	    {
	      ok = QO_GBP_IS_FIXED (col);
	    }
	  if (ok)
	    {
	      dquery_fixed = true;
	      changed = true;
	    }
	}

      /* a column equal to a fixed one is fixed */
      for (term = query->info.query.q.select.where; term != NULL; term = term->next)
	{
	  if (term->node_type != PT_EXPR || term->info.expr.op != PT_EQ)
	    {
	      continue;
	    }
	  a1 = term->info.expr.arg1;
	  a2 = term->info.expr.arg2;
	  if (a1->node_type != PT_NAME || a2->node_type != PT_NAME)
	    {
	      continue;
	    }
	  if (QO_GBP_IS_FIXED (a1) && !QO_GBP_IS_FIXED (a2))
	    {
	      fixed = parser_append_node (parser_copy_tree (parser, a2), fixed);
	      changed = true;
	    }
	  else if (QO_GBP_IS_FIXED (a2) && !QO_GBP_IS_FIXED (a1))
	    {
	      fixed = parser_append_node (parser_copy_tree (parser, a1), fixed);
	      changed = true;
	    }
	}
    }
  while (changed && !(dquery_fixed && (determined & ~cmask) == (all & ~cmask)));

#undef QO_GBP_IS_FIXED

  parser_free_tree (parser, fixed);

  /* the tables of C may vary inside a group of the derived table: what is fixed is its grouping key */
  return dquery_fixed && (determined & ~cmask) == (all & ~cmask);
}

/*
 * qo_gbp_add_column () - add a column to the derived table's select list, and its GROUP BY for a grouping column
 *   return: a name of the block for the new column
 */
static PT_NODE *
qo_gbp_add_column (PARSER_CONTEXT * parser, QO_GBP_BUILD * build, PT_NODE * expr, bool group)
{
  PT_NODE *attr, *sort, *range = build->dspec->info.spec.range_var;

  if (expr == NULL)
    {
      build->failed = true;
      return NULL;
    }

  build->ncols++;
  build->dquery->info.query.q.select.list = parser_append_node (expr, build->dquery->info.query.q.select.list);

  attr = pt_name (parser, mq_generate_name (parser, "gbp_a", &build->seq));
  if (attr == NULL)
    {
      build->failed = true;
      return NULL;
    }
  attr->info.name.meta_class = PT_NORMAL;
  attr->info.name.resolved = range->info.name.original;
  attr->info.name.spec_id = build->dspec->info.spec.id;
  attr->type_enum = expr->type_enum;
  attr->data_type = parser_copy_tree (parser, expr->data_type);
  PT_NAME_INFO_SET_FLAG (attr, PT_NAME_GENERATED_DERIVED_SPEC);
  build->dspec->info.spec.as_attr_list = parser_append_node (attr, build->dspec->info.spec.as_attr_list);

  if (group)
    {
      sort = parser_new_node (parser, PT_SORT_SPEC);
      if (sort == NULL)
	{
	  build->failed = true;
	  return NULL;
	}
      sort->info.sort_spec.expr = parser_copy_tree (parser, expr);
      sort->info.sort_spec.asc_or_desc = PT_ASC;
      sort->info.sort_spec.pos_descr.pos_no = build->ncols;
      sort->info.sort_spec.pos_descr.dom = pt_xasl_node_to_domain (parser, expr);
      build->dquery->info.query.q.select.group_by =
	parser_append_node (sort, build->dquery->info.query.q.select.group_by);
    }

  return parser_copy_tree (parser, attr);
}

/*
 * qo_gbp_make_agg () - a new aggregate function over an argument, typed
 */
static PT_NODE *
qo_gbp_make_agg (PARSER_CONTEXT * parser, FUNC_CODE function, PT_NODE * arg)
{
  PT_NODE *agg;

  agg = parser_new_node (parser, PT_FUNCTION);
  if (agg == NULL)
    {
      return NULL;
    }
  agg->info.function.function_type = function;
  agg->info.function.all_or_distinct = PT_ALL;
  agg->info.function.arg_list = arg;

  return pt_semantic_type (parser, agg, NULL);
}

/*
 * qo_gbp_cast_to () - cast an expression to the type of the aggregate it replaces, when it differs
 */
static PT_NODE *
qo_gbp_cast_to (PARSER_CONTEXT * parser, PT_NODE * expr, PT_NODE * like)
{
  int prec = 0, scale = 0;

  if (expr == NULL)
    {
      return NULL;
    }

  expr = pt_semantic_type (parser, expr, NULL);
  if (expr == NULL || pt_has_error (parser))
    {
      return NULL;
    }

  if (expr->type_enum == like->type_enum
      && (expr->data_type == NULL ? like->data_type == NULL
	  : (like->data_type != NULL
	     && expr->data_type->info.data_type.precision == like->data_type->info.data_type.precision
	     && expr->data_type->info.data_type.dec_precision == like->data_type->info.data_type.dec_precision)))
    {
      return expr;
    }

  if (like->data_type != NULL)
    {
      prec = like->data_type->info.data_type.precision;
      scale = like->data_type->info.data_type.dec_precision;
    }

  expr = pt_wrap_with_cast_op (parser, expr, like->type_enum, prec, scale, like->data_type);
  if (expr == NULL)
    {
      return NULL;
    }

  return pt_semantic_type (parser, expr, NULL);
}

/*
 * qo_gbp_replace_agg_post () - replace an aggregate of the block by the re-aggregation of the partial one(s) the
 *				derived table computes
 */
static PT_NODE *
qo_gbp_replace_agg_post (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
{
  QO_GBP_BUILD *build = (QO_GBP_BUILD *) arg;
  PT_NODE *agg_arg, *s, *c, *expr = NULL, *next;
  FUNC_CODE function;
  bool flat = build->remove_group_by;

  if (build->failed || !pt_is_aggregate_function (parser, node) || node->info.function.function_type == PT_GROUPBY_NUM)
    {
      return node;
    }

  function = node->info.function.function_type;
  agg_arg = node->info.function.arg_list;

  switch (function)
    {
    case PT_SUM:
    case PT_MIN:
    case PT_MAX:
      s = qo_gbp_add_column (parser, build, qo_gbp_make_agg (parser, function, parser_copy_tree (parser, agg_arg)),
			     false);
      expr = flat ? s : qo_gbp_make_agg (parser, function, s);
      break;

    case PT_COUNT:
    case PT_COUNT_STAR:
      c = qo_gbp_add_column (parser, build,
			     qo_gbp_make_agg (parser, function,
					      function == PT_COUNT ? parser_copy_tree (parser, agg_arg) : NULL), false);
      /* a row of the block without a partner in the derived table counts 0 for COUNT (x) and 1 for COUNT (*), as
       * its NULL-extended row did */
      expr = parser_make_expression (parser, PT_NVL, c, pt_make_integer_value (parser, function == PT_COUNT ? 0 : 1),
				     NULL);
      if (!flat)
	{
	  expr = parser_make_expression (parser, PT_NVL, qo_gbp_make_agg (parser, PT_SUM, expr),
					 pt_make_integer_value (parser, 0), NULL);
	}
      break;

    case PT_AVG:
      s = qo_gbp_add_column (parser, build, qo_gbp_make_agg (parser, PT_SUM, parser_copy_tree (parser, agg_arg)),
			     false);
      c = qo_gbp_add_column (parser, build, qo_gbp_make_agg (parser, PT_COUNT, parser_copy_tree (parser, agg_arg)),
			     false);
      if (!flat)
	{
	  s = qo_gbp_make_agg (parser, PT_SUM, s);
	  c = qo_gbp_make_agg (parser, PT_SUM, c);
	}
      /* AVG divides the sum by the count in DOUBLE */
      s = pt_wrap_with_cast_op (parser, s, PT_TYPE_DOUBLE, 0, 0, NULL);
      c = pt_wrap_with_cast_op (parser, parser_make_expression (parser, PT_NULLIF, c,
								pt_make_integer_value (parser, 0), NULL),
				PT_TYPE_DOUBLE, 0, 0, NULL);
      expr = parser_make_expression (parser, PT_DIVIDE, s, c, NULL);
      break;

    default:
      build->failed = true;
      return node;
    }

  expr = qo_gbp_cast_to (parser, expr, node);
  if (expr == NULL || build->failed)
    {
      build->failed = true;
      return node;
    }

  next = node->next;
  node->next = NULL;
  expr->next = next;
  expr->line_number = node->line_number;
  expr->column_number = node->column_number;
  if (node->alias_print != NULL)
    {
      expr->alias_print = node->alias_print;
    }
  expr->flag.is_hidden_column = node->flag.is_hidden_column;
  parser_free_tree (parser, node);

  return expr;
}

/*
 * qo_gbp_replace_name_pre () - replace a column of C by the derived table column that holds it
 */
static PT_NODE *
qo_gbp_replace_name_pre (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
{
  QO_GBP_BUILD *build = (QO_GBP_BUILD *) arg;
  PT_NODE *col, *attr, *new_name;

  if (build->failed || node->node_type != PT_NAME || node->info.name.spec_id == build->dspec->info.spec.id)
    {
      return node;
    }

  for (col = build->needed, attr = build->attrs; col != NULL && attr != NULL; col = col->next, attr = attr->next)
    {
      if (col->info.name.spec_id == node->info.name.spec_id
	  && intl_identifier_casecmp (col->info.name.original, node->info.name.original) == 0)
	{
	  break;
	}
    }

  if (col == NULL)
    {
      return node;
    }

  new_name = parser_copy_tree (parser, attr);
  if (new_name == NULL)
    {
      build->failed = true;
      return node;
    }
  new_name->next = node->next;
  new_name->line_number = node->line_number;
  new_name->column_number = node->column_number;
  new_name->alias_print = node->alias_print;
  new_name->flag.is_hidden_column = node->flag.is_hidden_column;
  node->next = NULL;
  parser_free_tree (parser, node);

  return new_name;
}

/*
 * qo_gbp_transform () - group the tables of C in a derived table and re-aggregate the block over it
 *   return: the derived table's SELECT, NULL if the block could not be rewritten (it may be partly rewritten then:
 *	     the caller works on a copy, or has checked the same copy before)
 *   env(in): the block, analyzed
 *   cmask(in): the tables to group
 *   remove_group_by(in): the block meets one row per group and drops its GROUP BY
 */
static PT_NODE *
qo_gbp_transform (PARSER_CONTEXT * parser, QO_GBP_ENV * env, QO_GBP_MASK cmask, bool remove_group_by)
{
  QO_GBP_BUILD build;
  PT_NODE *query = env->query, *dquery, *dspec, *spec, *prev, *next, *term, **termp, *col, *attr;
  PT_NODE *first_c = NULL, *nullable_spec = NULL, *moved = NULL, *on_spec = NULL;
  QO_GBP_MASK mask;
  int k, idx = 0, seq = 0;

  memset (&build, 0, sizeof (build));
  build.env = env;
  build.cmask = cmask;
  build.remove_group_by = remove_group_by;
  build.nullable = (cmask & env->nullable_mask) != 0;

  build.needed = qo_gbp_needed_columns (parser, env, cmask);
  if (build.needed == NULL)
    {
      return NULL;
    }

  dquery = parser_new_node (parser, PT_SELECT);
  dspec = parser_new_node (parser, PT_SPEC);
  if (dquery == NULL || dspec == NULL)
    {
      return NULL;
    }
  build.dquery = dquery;
  build.dspec = dspec;

  dquery->info.query.q.select.flavor = PT_USER_SELECT;
  dquery->info.query.is_subquery = PT_IS_SUBQUERY;
  dquery->info.query.all_distinct = PT_ALL;
  dquery->info.query.correlation_level = 0;
  dquery->flag.cannot_prepare = query->flag.cannot_prepare;
  dquery->info.query.q.select.hint = query->info.query.q.select.hint & QO_GBP_DERIVED_HINTS;
  dquery->info.query.q.select.num_parallel_threads = query->info.query.q.select.num_parallel_threads;
  PT_SELECT_INFO_SET_FLAG (dquery, PT_SELECT_INFO_HAS_AGG);

  dspec->info.spec.id = (UINTPTR) dspec;
  dspec->info.spec.derived_table = dquery;
  dspec->info.spec.derived_table_type = PT_IS_SUBQUERY;
  dspec->info.spec.range_var = pt_name (parser, mq_generate_name (parser, "gbp", &seq));
  if (dspec->info.spec.range_var == NULL)
    {
      return NULL;
    }
  dspec->info.spec.range_var->info.name.spec_id = dspec->info.spec.id;

  /* the grouping columns lead the derived table's select list */
  for (col = build.needed; col != NULL; col = col->next)
    {
      attr = qo_gbp_add_column (parser, &build, parser_copy_tree (parser, col), true);
      if (build.failed)
	{
	  return NULL;
	}
      build.attrs = parser_append_node (attr, build.attrs);
    }

  /* the aggregates: partial ones in the derived table, re-aggregated in the block */
  query->info.query.q.select.list =
    parser_walk_tree (parser, query->info.query.q.select.list, NULL, NULL, qo_gbp_replace_agg_post, &build);
  query->info.query.q.select.having =
    parser_walk_tree (parser, query->info.query.q.select.having, NULL, NULL, qo_gbp_replace_agg_post, &build);
  query->info.query.order_by =
    parser_walk_tree (parser, query->info.query.order_by, NULL, NULL, qo_gbp_replace_agg_post, &build);
  if (build.failed)
    {
      return NULL;
    }

  /* the WHERE terms over C alone move into the derived table */
  for (termp = &query->info.query.q.select.where; *termp != NULL;)
    {
      term = *termp;
      mask = qo_gbp_term_mask (parser, env, term);
      if (mask != 0 && (mask & ~cmask) == 0)
	{
	  *termp = term->next;
	  term->next = NULL;
	  moved = parser_append_node (term, moved);
	}
      else
	{
	  termp = &term->next;
	}
    }

  /* so do the terms of an ON over the null-supplying table alone */
  if (build.nullable)
    {
      for (k = 0; k < env->nspecs; k++)
	{
	  if (cmask & QO_GBP_BIT (k))
	    {
	      nullable_spec = env->specs[k];
	    }
	}
      on_spec = (nullable_spec->info.spec.join_type == PT_JOIN_LEFT_OUTER) ? nullable_spec : env->specs[1];
      for (termp = &on_spec->info.spec.on_cond; *termp != NULL;)
	{
	  term = *termp;
	  mask = qo_gbp_term_mask (parser, env, term);
	  if (mask != 0 && (mask & ~cmask) == 0)
	    {
	      *termp = term->next;
	      term->next = NULL;
	      moved = parser_append_node (term, moved);
	    }
	  else
	    {
	      termp = &term->next;
	    }
	}
    }

  for (term = moved; term != NULL; term = term->next)
    {
      if (term->node_type == PT_EXPR)
	{
	  term->info.expr.location = 0;
	}
      else if (term->node_type == PT_VALUE)
	{
	  term->info.value.location = 0;
	}
    }
  dquery->info.query.q.select.where = moved;

  /* the tables of C move into the derived table, which takes the place of the first of them */
  prev = NULL;
  for (spec = query->info.query.q.select.from, k = 0; spec != NULL; spec = next, k++)
    {
      next = spec->next;
      if (!(cmask & QO_GBP_BIT (k)))
	{
	  prev = spec;
	  continue;
	}

      if (first_c == NULL)
	{
	  first_c = spec;
	  dspec->info.spec.location = spec->info.spec.location;
	  if (spec == nullable_spec && spec->info.spec.join_type == PT_JOIN_LEFT_OUTER)
	    {
	      dspec->info.spec.join_type = PT_JOIN_LEFT_OUTER;
	      dspec->info.spec.on_cond = spec->info.spec.on_cond;
	      spec->info.spec.on_cond = NULL;
	    }
	  dspec->next = next;
	  if (prev == NULL)
	    {
	      query->info.query.q.select.from = dspec;
	    }
	  else
	    {
	      prev->next = dspec;
	    }
	  prev = dspec;
	}
      else
	{
	  prev->next = next;
	}

      spec->next = NULL;
      spec->info.spec.join_type = PT_JOIN_NONE;
      spec->info.spec.location = idx++;
      dquery->info.query.q.select.from = parser_append_node (spec, dquery->info.query.q.select.from);
    }

  /* the columns of C left in the block read the derived table */
#define QO_GBP_REPLACE(tree) \
  (tree) = parser_walk_tree (parser, (tree), qo_gbp_replace_name_pre, &build, NULL, NULL)

  QO_GBP_REPLACE (query->info.query.q.select.list);
  QO_GBP_REPLACE (query->info.query.q.select.group_by);
  QO_GBP_REPLACE (query->info.query.q.select.having);
  QO_GBP_REPLACE (query->info.query.order_by);
  QO_GBP_REPLACE (query->info.query.q.select.where);
  for (spec = query->info.query.q.select.from; spec != NULL; spec = spec->next)
    {
      QO_GBP_REPLACE (spec->info.spec.on_cond);
    }

#undef QO_GBP_REPLACE

  if (build.failed)
    {
      return NULL;
    }

  /* the derived table groups on a key of its tables as the block would (it also keeps the group estimate from
   * multiplying the distinct counts of columns the key fixes) */
  qo_remove_useless_groupby_columns (parser, dquery);

  if (remove_group_by)
    {
      parser_free_tree (parser, query->info.query.q.select.group_by);
      query->info.query.q.select.group_by = NULL;
      PT_SELECT_INFO_CLEAR_FLAG (query, PT_SELECT_INFO_HAS_AGG);
    }

  query = mq_regenerate_if_ambiguous (parser, dspec, query, query->info.query.q.select.from);
  (void) query;

  parser_free_tree (parser, build.needed);
  parser_free_tree (parser, build.attrs);

  return dquery;
}

/*
 * qo_gbp_plan_cost () - plan a SELECT the way XASL generation would and return the plan's cost
 *   return: the cost, -1 if there is no plan
 *   in_rows(out): rows the plan reads before grouping
 *   out_rows(out): rows it returns
 *   projected_size(out): bytes per row it returns
 */
static double
qo_gbp_plan_cost (PARSER_CONTEXT * parser, PT_NODE * select, double *in_rows, double *out_rows, int *projected_size)
{
  QO_PLAN *plan;
  double cost;

  plan = qo_optimize_query (parser, select);
  if (plan == NULL)
    {
      return -1.0;
    }

  cost = plan->fixed_cpu_cost + plan->fixed_io_cost + plan->variable_cpu_cost + plan->variable_io_cost;
  if (in_rows != NULL)
    {
      *in_rows = plan->info->cardinality;
    }
  if (out_rows != NULL)
    {
      *out_rows = plan->info->group_rows;
    }
  if (projected_size != NULL)
    {
      *projected_size = plan->info->projected_size;
    }

  qo_plan_discard (plan);

  if (cost >= QO_INFINITY)
    {
      return -1.0;
    }

  return cost;
}

/*
 * qo_gbp_candidate_cost () - the cost of the block with the tables of cmask grouped first: the derived table's plan
 *			      and the block's plan over it, both planned on a copy
 *   return: the cost, -1 if the candidate is dropped
 */
static double
qo_gbp_candidate_cost (PARSER_CONTEXT * parser, PT_NODE * query, QO_GBP_MASK cmask)
{
  QO_GBP_ENV env;
  PT_NODE *copy, *dquery, *needed;
  XASL_NODE fake_xasl;
  double cost = -1.0, dcost, ocost, in_rows = 0.0, out_rows = 0.0;
  int projected_size = 0;
  bool remove_group_by;

  copy = parser_copy_tree (parser, query);
  if (copy == NULL)
    {
      return -1.0;
    }

  if (!qo_gbp_analyze (parser, copy, &env))
    {
      goto end;
    }

  needed = qo_gbp_needed_columns (parser, &env, cmask);
  if (needed == NULL)
    {
      goto end;
    }

  /* a single table grouped on one of its keys folds no row; a grouping key must not merge different values */
  if (!qo_gbp_needed_is_image_equal (needed) || qo_gbp_needed_has_key (&env, cmask, needed))
    {
      parser_free_tree (parser, needed);
      goto end;
    }
  remove_group_by = qo_gbp_can_remove_group_by (parser, &env, cmask, needed);
  parser_free_tree (parser, needed);

  dquery = qo_gbp_transform (parser, &env, cmask, remove_group_by);
  if (dquery == NULL || pt_has_error (parser))
    {
      goto end;
    }

  dcost = qo_gbp_plan_cost (parser, dquery, &in_rows, &out_rows, &projected_size);
  /* a block that keeps its GROUP BY groups twice: the derived table's grouping must fold enough rows to pay for
   * itself, which the costs alone misjudge when it barely folds. One that drops it moves its grouping down. */
  if (dcost < 0.0 || out_rows <= 0.0 || in_rows < out_rows * (remove_group_by ? 1.0 : QO_GBP_MIN_GROUP_SIZE))
    {
      goto end;
    }

  /* the block reads the derived table as XASL generation will size it (qo_add_node ()) */
  memset (&fake_xasl, 0, sizeof (XASL_NODE));
  fake_xasl.cardinality = out_rows;
  fake_xasl.projected_size = projected_size;
  dquery->info.query.xasl = &fake_xasl;
  ocost = qo_gbp_plan_cost (parser, copy, NULL, NULL, NULL);
  dquery->info.query.xasl = NULL;

  if (ocost >= 0.0)
    {
      cost = dcost + ocost;

      /* a block that keeps its GROUP BY aggregates twice: the planner does not price a hash aggregation that runs
       * out of memory and sorts, nor how much the join it saves would have gained from parallel execution, so the
       * added grouping must win clearly. One that drops it only moves its grouping. */
      if (!remove_group_by)
	{
	  cost /= QO_GBP_ADDED_GROUPING_GAIN;
	}
    }

end:
  parser_free_tree (parser, copy);
  return cost;
}

/*
 * qo_rewrite_group_by_placement () - group a part of the tables of a joining, aggregating SELECT before the join when
 *				      the planner finds it cheaper (see the block comment above)
 *   return: void
 *   parser(in):
 *   query(in/out): a SELECT whose rewrites are done (qo_rewrite_queries_post ())
 */
void
qo_rewrite_group_by_placement (PARSER_CONTEXT * parser, PT_NODE * query)
{
  QO_GBP_ENV env;
  QO_GBP_MASK cmask, best_mask = 0, free_mask, sub;
  PT_NODE *copy, *needed, *save_errors;
  double orig_cost, best_cost, cost;
  bool remove_group_by;
  int level, i, nfree, best_i;

  if (query == NULL || query->node_type != PT_SELECT
      || (query->info.query.q.select.hint & PT_HINT_NO_PUSH_GROUP_BY) || pt_has_error (parser))
    {
      return;
    }

  qo_get_optimization_param (&level, QO_PARAM_LEVEL);
  if (!OPTIMIZATION_ENABLED (level))
    {
      return;
    }

  if (!qo_gbp_analyze (parser, query, &env))
    {
      return;
    }

  /* nothing C may take */
  if ((env.at_mask & env.inner_mask) != env.at_mask
      && !(qo_gbp_popcount (env.at_mask) == 1 && (env.at_mask & env.nullable_mask)))
    {
      return;
    }

  /* the planning below must not leave an error behind for the statement */
  save_errors = parser->error_msgs;
  parser->error_msgs = NULL;

  /* the block as it is is planned once a candidate is found */
  orig_cost = -2.0;
  best_cost = 0.0;

#define QO_GBP_TRY(mask) \
  do \
    { \
      if (orig_cost == -2.0 && qo_gbp_is_candidate (&env, (mask))) \
	{ \
	  copy = parser_copy_tree (parser, query); \
	  orig_cost = (copy != NULL) ? qo_gbp_plan_cost (parser, copy, NULL, NULL, NULL) : -1.0; \
	  parser_free_tree (parser, copy); \
	  if (pt_has_error (parser)) \
	    { \
	      orig_cost = -1.0; \
	    } \
	  best_cost = orig_cost; \
	} \
      if (orig_cost >= 0.0 && qo_gbp_is_candidate (&env, (mask))) \
	{ \
	  cost = qo_gbp_candidate_cost (parser, query, (mask)); \
	  if (pt_has_error (parser)) \
	    { \
	      parser_free_tree (parser, parser->error_msgs); \
	      parser->error_msgs = NULL; \
	      cost = -1.0; \
	    } \
	  if (cost >= 0.0 && cost < best_cost) \
	    { \
	      best_cost = cost; \
	      best_mask = (mask); \
	    } \
	} \
    } \
  while (0)

  if (qo_gbp_popcount (env.at_mask) == 1 && (env.at_mask & env.nullable_mask))
    {
      /* the null-supplying table is grouped alone */
      QO_GBP_TRY (env.at_mask);
    }
  else
    {
      free_mask = env.inner_mask & ~env.at_mask;
      nfree = qo_gbp_popcount (free_mask);

      if (nfree <= QO_GBP_EXHAUSTIVE_SPECS)
	{
	  /* every subset of the free tables added to the aggregate tables */
	  sub = 0;
	  do
	    {
	      QO_GBP_TRY (env.at_mask | sub);
	      sub = (sub - free_mask) & free_mask;
	    }
	  while (sub != 0);
	}
      else
	{
	  /* C grows one joined table at a time from the aggregate tables (the linear search of Oracle's cost-based
	   * transformation) */
	  cmask = env.at_mask;
	  QO_GBP_TRY (cmask);
	  while ((cmask & free_mask) != free_mask)
	    {
	      best_i = -1;
	      for (i = 0; i < env.nspecs && best_i < 0; i++)
		{
		  if ((free_mask & ~cmask & QO_GBP_BIT (i)) == 0)
		    {
		      continue;
		    }
		  if (env.adj[i] & cmask)
		    {
		      best_i = i;
		    }
		}
	      if (best_i < 0)
		{
		  break;
		}
	      cmask |= QO_GBP_BIT (best_i);
	      QO_GBP_TRY (cmask);
	    }
	}
    }

#undef QO_GBP_TRY

  if (best_mask == 0)
    {
      goto end;
    }

  /* the winner on the block itself */
  needed = qo_gbp_needed_columns (parser, &env, best_mask);
  if (needed == NULL)
    {
      goto end;
    }
  remove_group_by = qo_gbp_can_remove_group_by (parser, &env, best_mask, needed);
  parser_free_tree (parser, needed);

  if (qo_gbp_transform (parser, &env, best_mask, remove_group_by) == NULL || pt_has_error (parser))
    {
      /* the copy of the same block was rewritten: this cannot fail but on memory */
      assert (false);
      if (!pt_has_error (parser))
	{
	  PT_INTERNAL_ERROR (parser, "group by placement");
	}
      save_errors = parser_append_node (parser->error_msgs, save_errors);
      parser->error_msgs = save_errors;
      return;
    }

end:
  if (parser->error_msgs != NULL)
    {
      parser_free_tree (parser, parser->error_msgs);
    }
  parser->error_msgs = save_errors;
}
