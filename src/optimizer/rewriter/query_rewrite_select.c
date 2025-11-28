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


static PT_NODE *qo_reset_location (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk);
static PT_NODE *qo_get_name_cnt_by_spec (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk);
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
static int qo_reduce_order_by (PARSER_CONTEXT * parser, PT_NODE * node);
static PT_NODE *qo_rewrite_oid_equality (PARSER_CONTEXT * parser, PT_NODE * node, PT_NODE * pred, int *seqno);
static PT_NODE *qo_rewrite_innerjoin (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk);
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

  /* do not reduce order by siblings */
  if (node->node_type != PT_SELECT || node->info.query.q.select.connect_by)
    {
      return error;
    }

  /* init */
  const_order_count = order_move_count = 0;
  need_merge_check = false;
  has_orderbynum_with_groupby = false;

  /* check for merge order by to group by( without DISTINCT and HAVING clause) */

  if (node->info.query.all_distinct == PT_DISTINCT)
    {
      ;				/* give up */
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

	  if (node->info.query.orderby_for == NULL && !node->info.query.q.select.connect_by
	      && !node->info.query.q.select.group_by->flag.with_rollup)
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
      if (col->node_type == PT_NAME)
	{
	  if (PT_NAME_INFO_IS_FLAGED (col, PT_NAME_INFO_CONSTANT))
	    {
	      /* remove constant order node */
	      if (order_prev == NULL)
		{		/* the first */
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
      parser_walk_tree (parser, arg1, qo_get_name_cnt_by_spec, &info1, NULL, NULL);
      parser_walk_tree (parser, arg2, qo_get_name_cnt_by_spec, &info2, NULL, NULL);

      /* col1(my spec) = const */
      if (info1.my_spec_cnt == 1 && info1.other_spec_cnt == 0)
	{
	  /* const */
	  if (info2.my_spec_cnt == 0)
	    {
	      point_list = parser_append_node (pt_point (parser, info1.my_spec_node), point_list);
	    }
	}
      /* const = col(my spec) */
      else if (info2.my_spec_cnt == 1 && info2.other_spec_cnt == 0)
	{
	  /* const */
	  if (info1.my_spec_cnt == 0)
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
	  /* check outer join spec. */
	  is_outer_joined = mq_is_outer_join_spec (parser, spec);
	  if (is_outer_joined)
	    {
	      info.id = info_spec.id = spec->info.spec.id;

	      /* search where list */
	      for (expr = node->info.query.q.select.where; expr; expr = expr->next)
		{
		  if (expr->node_type == PT_EXPR && expr->info.expr.location == 0 && expr->info.expr.op != PT_IS_NULL
		      && expr->or_next == NULL && expr->info.expr.op != PT_AND && expr->info.expr.op != PT_OR)
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
 */
static PT_NODE *
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
