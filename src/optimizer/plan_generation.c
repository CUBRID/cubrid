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
 * plan_generation.c - Generate XASL trees from query optimizer plans
 */

#ident "$Id$"

#include <assert.h>

#include "optimizer.h"

#include "config.h"
#include "object_primitive.h"
#include "query_bitset.h"
#include "query_graph.h"
#include "query_planner.h"
#include "parser.h"
#include "parser_support.h"
#include "system_parameter.h"
#include "xasl.h"
#include "xasl_generation.h"
#include "xasl_predicate.hpp"

typedef int (*ELIGIBILITY_FN) (QO_TERM *);

static XASL_NODE *make_scan_proc (QO_ENV * env);
static XASL_NODE *make_mergelist_proc (QO_ENV * env, QO_PLAN * plan, XASL_NODE * left, PT_NODE * left_list,
				       BITSET * left_exprs, PT_NODE * left_elist, XASL_NODE * rght, PT_NODE * rght_list,
				       BITSET * rght_exprs, PT_NODE * rght_elist);
static XASL_NODE *make_fetch_proc (QO_ENV * env, QO_PLAN * plan);
static XASL_NODE *make_buildlist_proc (QO_ENV * env, PT_NODE * namelist);

static XASL_NODE *init_class_scan_proc (QO_ENV * env, XASL_NODE * xasl, QO_PLAN * plan);
static XASL_NODE *init_list_scan_proc (QO_ENV * env, XASL_NODE * xasl, XASL_NODE * list, PT_NODE * namelist,
				       BITSET * predset, int *poslist);

static XASL_NODE *add_access_spec (QO_ENV *, XASL_NODE *, QO_PLAN *);
static XASL_NODE *add_scan_proc (QO_ENV * env, XASL_NODE * xasl, XASL_NODE * scan);
static XASL_NODE *add_fetch_proc (QO_ENV * env, XASL_NODE * xasl, XASL_NODE * proc);
static XASL_NODE *add_uncorrelated (QO_ENV * env, XASL_NODE * xasl, XASL_NODE * sub);
static XASL_NODE *add_subqueries (QO_ENV * env, XASL_NODE * xasl, BITSET *);
static XASL_NODE *add_sort_spec (QO_ENV *, XASL_NODE *, QO_PLAN *, DB_VALUE *, bool);
static XASL_NODE *add_if_predicate (QO_ENV *, XASL_NODE *, PT_NODE *);
static XASL_NODE *add_after_join_predicate (QO_ENV *, XASL_NODE *, PT_NODE *);

static PT_NODE *make_pred_from_bitset (QO_ENV * env, BITSET * predset, ELIGIBILITY_FN safe);
static void make_pred_from_plan (QO_ENV * env, QO_PLAN * plan, PT_NODE ** key_access_pred, PT_NODE ** access_pred,
				 QO_XASL_INDEX_INFO * qo_index_infop, PT_NODE ** hash_pred);
static PT_NODE *make_if_pred_from_plan (QO_ENV * env, QO_PLAN * plan);
static PT_NODE *make_instnum_pred_from_plan (QO_ENV * env, QO_PLAN * plan);
static PT_NODE *make_namelist_from_projected_segs (QO_ENV * env, QO_PLAN * plan);

static XASL_NODE *gen_outer (QO_ENV *, QO_PLAN *, BITSET *, XASL_NODE *, XASL_NODE *, XASL_NODE *);
static XASL_NODE *gen_inner (QO_ENV *, QO_PLAN *, BITSET *, BITSET *, XASL_NODE *, XASL_NODE *);
static XASL_NODE *preserve_info (QO_ENV * env, QO_PLAN * plan, XASL_NODE * xasl);

static int is_normal_access_term (QO_TERM *);
static int is_normal_if_term (QO_TERM *);
static int is_after_join_term (QO_TERM *);
static int is_totally_after_join_term (QO_TERM *);
static int is_follow_if_term (QO_TERM *);
static int is_always_true (QO_TERM *);

static QO_XASL_INDEX_INFO *qo_get_xasl_index_info (QO_ENV * env, QO_PLAN * plan);
static void qo_free_xasl_index_info (QO_ENV * env, QO_XASL_INDEX_INFO * info);

static bool qo_validate_regu_var_for_limit (REGU_VARIABLE * var_p);
static bool qo_get_limit_from_instnum_pred (PARSER_CONTEXT * parser, PRED_EXPR * pred, REGU_PTR_LIST * lower,
					    REGU_PTR_LIST * upper);
static bool qo_get_limit_from_eval_term (PARSER_CONTEXT * parser, PRED_EXPR * pred, REGU_PTR_LIST * lower,
					 REGU_PTR_LIST * upper);

static REGU_PTR_LIST regu_ptr_list_create ();
static void regu_ptr_list_free (REGU_PTR_LIST list);
static REGU_PTR_LIST regu_ptr_list_add_regu (REGU_VARIABLE * var_p, REGU_PTR_LIST list);

static bool qo_check_seg_belongs_to_range_term (QO_PLAN * subplan, QO_ENV * env, int seg_idx);
static int qo_check_plan_index_for_multi_range_opt (PT_NODE * orderby_nodes, PT_NODE * orderby_sort_list,
						    QO_PLAN * plan, bool * is_valid, int *first_col_idx_pos,
						    bool * reverse);
static int qo_check_terms_for_multiple_range_opt (QO_PLAN * plan, int first_sort_col_idx, bool * can_optimize);
static bool qo_check_subqueries_for_multi_range_opt (QO_PLAN * plan, int sort_col_idx_pos);

static int qo_check_subplans_for_multi_range_opt (QO_PLAN * parent, QO_PLAN * plan, QO_PLAN * sortplan, bool * is_valid,
						  bool * seen);
static bool qo_check_subplan_join_cond_for_multi_range_opt (QO_PLAN * parent, QO_PLAN * subplan, QO_PLAN * sort_plan);
static bool qo_check_parent_eq_class_for_multi_range_opt (QO_PLAN * parent, QO_PLAN * subplan, QO_PLAN * sort_plan);
static XASL_NODE *make_sort_limit_proc (QO_ENV * env, QO_PLAN * plan, PT_NODE * namelist, XASL_NODE * xasl);
static PT_NODE *qo_get_orderby_num_upper_bound_node (PARSER_CONTEXT * parser, PT_NODE * orderby_for,
						     bool * is_new_node);
static int qo_get_multi_col_range_segs (QO_ENV * env, QO_PLAN * plan, QO_INDEX_ENTRY * index_entryp,
					BITSET * multi_col_segs, BITSET * multi_col_range_segs, BITSET * index_segs);

/*
 * make_scan_proc () -
 *   return: XASL_NODE *
 *   env(in): The optimizer environment
 */
static XASL_NODE *
make_scan_proc (QO_ENV * env)
{
  return ptqo_to_scan_proc (QO_ENV_PARSER (env), NULL, NULL, NULL, NULL, NULL, NULL, NULL);
}


/*
 * make_fetch_proc () -
 *   return:
 *   env(in):
 *   plan(in):
 */
static XASL_NODE *
make_fetch_proc (QO_ENV * env, QO_PLAN * plan)
{
  XASL_NODE *xasl;
  PT_NODE *access_pred;
  PT_NODE *if_pred;

  make_pred_from_plan (env, plan, NULL, &access_pred, NULL, NULL);
  if_pred = make_if_pred_from_plan (env, plan);

  xasl =
    pt_to_fetch_proc (QO_ENV_PARSER (env), QO_NODE_ENTITY_SPEC (QO_TERM_TAIL (plan->plan_un.follow.path)), access_pred);
  xasl = add_if_predicate (env, xasl, if_pred);

  /* free pointer node list */
  parser_free_tree (QO_ENV_PARSER (env), access_pred);
  parser_free_tree (QO_ENV_PARSER (env), if_pred);

  return xasl;
}

/*
 * make_mergelist_proc () -
 *   return: XASL_NODE *
 *   env(in): The optimizer environment
 *   plan(in): The (sub)plan to generate code for merge
 *   left(in): The XASL node that should build a sorted outer result
 *   left_list(in): The expr, name list used to create the left XASL node
 *   left_exprs(in): The join terms bitset of left expr segs
 *   left_elist(in): The join terms expr list of left expr segs
 *   rght(in): The XASL node that should build a sorted inner result
 *   rght_list(in): The expr, name list used to create the right XASL node
 *   rght_exprs(in): The join terms bitset of right expr segs
 *   rght_elist(in): The join terms expr list of right expr segs
 *
 * Note: Make a MERGELIST_PROC XASL node that will eventually reside
 *	on the merge_list_ptr of some other XASL node.
 *	Thes initializes the left and right procs  things
 *	live in such a specialized environment that there is no need
 *	to initialize anything other than the type and the access
 *	spec; the scan evidently uses the val_list, etc. from the
 *	outer block.
 */
static XASL_NODE *
make_mergelist_proc (QO_ENV * env, QO_PLAN * plan, XASL_NODE * left, PT_NODE * left_list, BITSET * left_exprs,
		     PT_NODE * left_elist, XASL_NODE * rght, PT_NODE * rght_list, BITSET * rght_exprs,
		     PT_NODE * rght_elist)
{
  XASL_NODE *merge = NULL;
  PARSER_CONTEXT *parser = NULL;
  QFILE_LIST_MERGE_INFO *ls_merge;
  PT_NODE *outer_attr, *inner_attr;
  int i, left_epos, rght_epos, cnt, seg_idx, ncols;
  int left_nlen, left_elen, rght_nlen, rght_elen, nlen;
  SORT_LIST *order, *prev_order;
  QO_TERM *term;
  BITSET_ITERATOR bi;
  BITSET term_segs;

  bitset_init (&term_segs, env);

  if (env == NULL || plan == NULL)
    {
      goto exit_on_error;
    }

  parser = QO_ENV_PARSER (env);

  merge = ptqo_to_merge_list_proc (parser, left, rght, plan->plan_un.join.join_type);

  if (merge == NULL || left == NULL || left_list == NULL || rght == NULL || rght_list == NULL)
    {
      goto exit_on_error;
    }

  ls_merge = &merge->proc.mergelist.ls_merge;

  ls_merge->join_type = plan->plan_un.join.join_type;

  ncols = ls_merge->ls_column_cnt = bitset_cardinality (&(plan->plan_un.join.join_terms));
  assert (ncols > 0);

  ls_merge->ls_outer_column = (int *) pt_alloc_packing_buf (ncols * sizeof (int));
  if (ls_merge->ls_outer_column == NULL)
    {
      goto exit_on_error;
    }

  ls_merge->ls_outer_unique = (int *) pt_alloc_packing_buf (ncols * sizeof (int));
  if (ls_merge->ls_outer_unique == NULL)
    {
      goto exit_on_error;
    }

  ls_merge->ls_inner_column = (int *) pt_alloc_packing_buf (ncols * sizeof (int));

  if (ls_merge->ls_inner_column == NULL)
    {
      goto exit_on_error;
    }

  ls_merge->ls_inner_unique = (int *) pt_alloc_packing_buf (ncols * sizeof (int));
  if (ls_merge->ls_inner_unique == NULL)
    {
      goto exit_on_error;
    }

  left->orderby_list = NULL;
  rght->orderby_list = NULL;

  cnt = 0;			/* init */
  left_epos = rght_epos = 0;	/* init */
  for (i = bitset_iterate (&(plan->plan_un.join.join_terms), &bi); i != -1; i = bitset_next_member (&bi))
    {
      term = QO_ENV_TERM (env, i);

      if (ls_merge->join_type == JOIN_INNER && QO_IS_PATH_TERM (term))
	{
	  /* mark merge join spec as single-fetch */
	  ls_merge->single_fetch = QPROC_SINGLE_OUTER;
	}

      if (BITSET_MEMBER (*left_exprs, i) && left_elist != NULL)
	{
	  /* Then we added an "extra" column for the expression to the left_elist.  We want to treat that expression as
	   * the outer expression, but we want to leave it off of the list of segments that are projected out of the
	   * merge. Take it off, but remember it in "outer_attr" so that we can fix up domain info in a little while. */
	  ls_merge->ls_outer_column[cnt] = left_epos++;
	  outer_attr = left_elist;
	  left_elist = left_elist->next;
	}
      else
	{
	  /* Determine which attributes are involved in this predicate. */
	  bitset_assign (&term_segs, &(QO_TERM_SEGS (term)));
	  bitset_intersect (&term_segs, &((plan->plan_un.join.outer)->info->projected_segs));
	  seg_idx = bitset_first_member (&term_segs);
	  if (seg_idx == -1)
	    {
	      goto exit_on_error;
	    }

	  outer_attr = QO_SEG_PT_NODE (QO_ENV_SEG (env, seg_idx));

	  ls_merge->ls_outer_column[cnt] = pt_find_attribute (parser, outer_attr, left_list);
	}
      ls_merge->ls_outer_unique[cnt] = false;	/* currently, unused */

      if (BITSET_MEMBER (*rght_exprs, i) && rght_elist != NULL)
	{
	  /* This situation is exactly analogous to the one above, except that we're concerned with the right (inner)
	   * side this time. */
	  ls_merge->ls_inner_column[cnt] = rght_epos++;
	  inner_attr = rght_elist;
	  rght_elist = rght_elist->next;
	}
      else
	{
	  /* Determine which attributes are involved in this predicate. */
	  bitset_assign (&term_segs, &(QO_TERM_SEGS (term)));
	  bitset_intersect (&term_segs, &((plan->plan_un.join.inner)->info->projected_segs));
	  seg_idx = bitset_first_member (&term_segs);
	  if (seg_idx == -1)
	    {
	      goto exit_on_error;
	    }

	  inner_attr = QO_SEG_PT_NODE (QO_ENV_SEG (env, seg_idx));

	  ls_merge->ls_inner_column[cnt] = pt_find_attribute (parser, inner_attr, rght_list);
	}
      ls_merge->ls_inner_unique[cnt] = false;	/* currently, unused */

      /* set outer list order entry */
      prev_order = NULL;
      for (order = left->orderby_list; order; order = order->next)
	{
	  if (order->pos_descr.pos_no == ls_merge->ls_outer_column[cnt])
	    {
	      /* found order entry */
	      break;
	    }
	  prev_order = order;
	}

      /* not found outer order entry */
      if (order == NULL)
	{
	  order = ptqo_single_orderby (parser);
	  if (order == NULL)
	    {
	      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_FAILED_ASSERTION, 1, "left_order != NULL");
	      goto exit_on_error;
	    }

	  order->s_order = S_ASC;
	  order->pos_descr.pos_no = ls_merge->ls_outer_column[cnt];
	  order->pos_descr.dom = pt_xasl_node_to_domain (parser, outer_attr);
	  if (prev_order == NULL)
	    {
	      left->orderby_list = order;
	    }
	  else
	    {
	      prev_order->next = order;
	    }
	}

      /* set inner list order entry */
      prev_order = NULL;
      for (order = rght->orderby_list; order; order = order->next)
	{
	  if (order->pos_descr.pos_no == ls_merge->ls_inner_column[cnt])
	    {
	      /* found order entry */
	      break;
	    }
	  prev_order = order;
	}

      /* not found inner order entry */
      if (order == NULL)
	{
	  order = ptqo_single_orderby (parser);
	  if (order == NULL)
	    {
	      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_FAILED_ASSERTION, 1, "right_order != NULL");
	      goto exit_on_error;
	    }

	  order->s_order = S_ASC;
	  order->pos_descr.pos_no = ls_merge->ls_inner_column[cnt];
	  order->pos_descr.dom = pt_xasl_node_to_domain (parser, inner_attr);
	  if (prev_order == NULL)
	    {
	      rght->orderby_list = order;
	    }
	  else
	    {
	      prev_order->next = order;
	    }
	}

      cnt++;
    }				/* for (i = ... ) */
  assert (cnt == ncols);

  left_elen = bitset_cardinality (left_exprs);
  left_nlen = pt_length_of_list (left_list) - left_elen;
  rght_elen = bitset_cardinality (rght_exprs);
  rght_nlen = pt_length_of_list (rght_list) - rght_elen;

  nlen = ls_merge->ls_pos_cnt = left_nlen + rght_nlen;
  ls_merge->ls_outer_inner_list = (int *) pt_alloc_packing_buf (nlen * sizeof (int));
  if (ls_merge->ls_outer_inner_list == NULL)
    {
      goto exit_on_error;
    }

  ls_merge->ls_pos_list = (int *) pt_alloc_packing_buf (nlen * sizeof (int));
  if (ls_merge->ls_pos_list == NULL)
    {
      goto exit_on_error;
    }

  /* these could be sorted out arbitrily. This could make it easier to avoid the wrapper buildlist_proc, when no
   * expressions, predicates, subqueries, fetches, or aggregation is involved. For now, we always build the same thing,
   * with simple column concatenation. */

  for (i = 0; i < left_nlen; i++)
    {
      ls_merge->ls_outer_inner_list[i] = QFILE_OUTER_LIST;
      ls_merge->ls_pos_list[i] = i + left_elen;
    }

  for (i = 0; i < nlen - left_nlen; i++)
    {
      ls_merge->ls_outer_inner_list[left_nlen + i] = QFILE_INNER_LIST;
      ls_merge->ls_pos_list[left_nlen + i] = i + rght_elen;
    }

  /* make outer_spec_list, outer_val_list, inner_spec_list, and inner_val_list */
  if (ls_merge->join_type != JOIN_INNER)
    {
      PT_NODE *other_pred;
      int *poslist;

      /* set poslist of outer XASL node */
      poslist = NULL;		/* init */
      if (left_elen > 0)
	{
	  /* proceed to name list and skip out join edge exprs */
	  for (i = 0; i < left_elen; i++)
	    {
	      left_list = left_list->next;
	    }

	  poslist = (int *) malloc (left_nlen * sizeof (int));
	  if (poslist == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, left_nlen * sizeof (int));
	      goto exit_on_error;
	    }

	  for (i = 0; i < left_nlen; i++)
	    {
	      poslist[i] = i + left_elen;
	    }
	}

      /* sets xasl->spec_list and xasl->val_list */
      merge = ptqo_to_list_scan_proc (parser, merge, SCAN_PROC, left, left_list, NULL, poslist);
      /* dealloc */
      if (poslist != NULL)
	{
	  free_and_init (poslist);
	}

      if (merge == NULL)
	{
	  goto exit_on_error;
	}

      merge->proc.mergelist.outer_spec_list = merge->spec_list;
      merge->proc.mergelist.outer_val_list = merge->val_list;

      /* set poslist of inner XASL node */
      poslist = NULL;		/* init */
      if (rght_elen > 0)
	{
	  /* proceed to name list and skip out join edge exprs */
	  for (i = 0; i < rght_elen; i++)
	    {
	      rght_list = rght_list->next;
	    }

	  poslist = (int *) malloc (rght_nlen * sizeof (int));
	  if (poslist == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, rght_nlen * sizeof (int));
	      goto exit_on_error;
	    }

	  for (i = 0; i < rght_nlen; i++)
	    {
	      poslist[i] = i + rght_elen;
	    }
	}

      /* sets xasl->spec_list and xasl->val_list */
      merge = ptqo_to_list_scan_proc (parser, merge, SCAN_PROC, rght, rght_list, NULL, poslist);
      /* dealloc */
      if (poslist)
	{
	  free_and_init (poslist);
	}

      if (merge == NULL)
	{
	  goto exit_on_error;
	}

      merge->proc.mergelist.inner_spec_list = merge->spec_list;
      merge->proc.mergelist.inner_val_list = merge->val_list;

      merge->spec_list = NULL;
      merge->val_list = NULL;

      /* add outer join terms */
      other_pred = make_pred_from_bitset (env, &(plan->plan_un.join.during_join_terms), is_always_true);
      if (other_pred)
	{
	  merge->after_join_pred = pt_to_pred_expr (parser, other_pred);

	  /* free pointer node list */
	  parser_free_tree (parser, other_pred);
	}
    }
  else
    {
      merge->proc.mergelist.outer_spec_list = NULL;
      merge->proc.mergelist.outer_val_list = NULL;
      merge->proc.mergelist.inner_spec_list = NULL;
      merge->proc.mergelist.inner_val_list = NULL;
    }

exit_on_end:

  bitset_delset (&term_segs);

  return merge;

exit_on_error:

  merge = NULL;
  goto exit_on_end;
}

/*
 * make_buildlist_proc () -
 *   return: XASL_NODE *
 *   env(in): The optimizer environment
 *   namelist(in): The list of names to use as input to and output from this
 *		   node
 */
static XASL_NODE *
make_buildlist_proc (QO_ENV * env, PT_NODE * namelist)
{
  return pt_skeleton_buildlist_proc (QO_ENV_PARSER (env), namelist);
}

/*
 * bitset_has_path () -
 *   return: int 1 iff path term is present, 0 otherwise
 *   env(in): The optimizer environment
 *   predset(in): The bitset of predicates to turn into a predicate tree
 */
static int
bitset_has_path (QO_ENV * env, BITSET * predset)
{
  BITSET_ITERATOR bi;
  int i;

  for (i = bitset_iterate (predset, &bi); i != -1; i = bitset_next_member (&bi))
    {
      QO_TERM *term;

      term = QO_ENV_TERM (env, i);
      if (QO_IS_PATH_TERM (term))
	{
	  return 1;
	}
    }

  return 0;
}

/*
 * mark_access_as_outer_join () - mark aan xasl proc's access spec
 *				  as left outer join
 *   return:
 *   parser(in): The parser environment
 *   xasl(in): The already allocated node to be initialized
 */
static void
mark_access_as_outer_join (PARSER_CONTEXT * parser, XASL_NODE * xasl)
{
  ACCESS_SPEC_TYPE *access;

  for (access = xasl->spec_list; access; access = access->next)
    {
      access->single_fetch = QPROC_NO_SINGLE_OUTER;
    }
}

/*
 * init_class_scan_proc () -
 *   return: XASL_NODE *
 *   env(in): The optimizer environment
 *   xasl(in): The already allocated node to be initialized
 *   plan(in): The plan from which to initialize the scan proc
 *
 * Note: Take a BUILDwhatever skeleton and flesh it out as a scan
 *	gadget.  Don't mess with any other fields than you absolutely
 *	must:  they may have already been initialized by other
 *	routines.
 */
static XASL_NODE *
init_class_scan_proc (QO_ENV * env, XASL_NODE * xasl, QO_PLAN * plan)
{
  PARSER_CONTEXT *parser;
  PT_NODE *spec;
  PT_NODE *key_pred;
  PT_NODE *access_pred, *hash_pred;
  PT_NODE *if_pred;
  PT_NODE *after_join_pred;
  QO_XASL_INDEX_INFO *info;

  parser = QO_ENV_PARSER (env);

  spec = QO_NODE_ENTITY_SPEC (plan->plan_un.scan.node);

  info = qo_get_xasl_index_info (env, plan);
  make_pred_from_plan (env, plan, &key_pred, &access_pred, info, &hash_pred);
  xasl = ptqo_to_scan_proc (parser, plan, xasl, spec, key_pred, access_pred, info, hash_pred);

  /* free pointer node list */
  parser_free_tree (parser, key_pred);
  parser_free_tree (parser, access_pred);

  if (xasl)
    {
      after_join_pred = make_pred_from_bitset (env, &(plan->sarged_terms), is_after_join_term);
      if_pred = make_if_pred_from_plan (env, plan);

      xasl = add_after_join_predicate (env, xasl, after_join_pred);
      xasl = add_if_predicate (env, xasl, if_pred);

      /* free pointer node list */
      parser_free_tree (parser, after_join_pred);
      parser_free_tree (parser, if_pred);
    }

  if (info)
    {
      qo_free_xasl_index_info (env, info);
    }

  return xasl;
}

/*
 * init_list_scan_proc () -
 *   return: XASL_NODE *
 *   env(in): The optimizer environment
 *   xasl(in): The already allocated node to be initialized
 *   listfile(in): The buildlist proc node for the list file to be scanned
 *   namelist(in): The list of names (columns) to be retrieved from the file
 *   predset(in): A bitset of predicates to be added to the access spec
 *   poslist(in):
 *
 * Note: Take a BUILDwhatever skeleton and flesh it out as a scan
 *	gadget.  Don't mess with any other fields than you absolutely
 *	must:  they may have already been initialized by other
 *	routines.
 */
static XASL_NODE *
init_list_scan_proc (QO_ENV * env, XASL_NODE * xasl, XASL_NODE * listfile, PT_NODE * namelist, BITSET * predset,
		     int *poslist)
{
  PT_NODE *access_pred, *if_pred, *after_join_pred, *instnum_pred;

  if (xasl)
    {
      access_pred = make_pred_from_bitset (env, predset, is_normal_access_term);
      if_pred = make_pred_from_bitset (env, predset, is_normal_if_term);
      after_join_pred = make_pred_from_bitset (env, predset, is_after_join_term);
      instnum_pred = make_pred_from_bitset (env, predset, is_totally_after_join_term);

      xasl = ptqo_to_list_scan_proc (QO_ENV_PARSER (env), xasl, SCAN_PROC, listfile, namelist, access_pred, poslist);

      if (env->pt_tree->node_type == PT_SELECT && env->pt_tree->info.query.q.select.connect_by)
	{
	  pt_set_level_node_etc (QO_ENV_PARSER (env), if_pred, &xasl->level_val);
	  pt_set_isleaf_node_etc (QO_ENV_PARSER (env), if_pred, &xasl->isleaf_val);
	  pt_set_iscycle_node_etc (QO_ENV_PARSER (env), if_pred, &xasl->iscycle_val);
	  pt_set_connect_by_operator_node_etc (QO_ENV_PARSER (env), if_pred, xasl);
	  pt_set_qprior_node_etc (QO_ENV_PARSER (env), if_pred, xasl);
	}

      xasl = add_if_predicate (env, xasl, if_pred);
      xasl = add_after_join_predicate (env, xasl, after_join_pred);
      xasl = pt_to_instnum_pred (QO_ENV_PARSER (env), xasl, instnum_pred);

      /* free pointer node list */
      parser_free_tree (QO_ENV_PARSER (env), access_pred);
      parser_free_tree (QO_ENV_PARSER (env), if_pred);
      parser_free_tree (QO_ENV_PARSER (env), after_join_pred);
      parser_free_tree (QO_ENV_PARSER (env), instnum_pred);
    }

  return xasl;
}

/*
 * add_access_spec () -
 *   return: XASL_NODE *
 *   env(in): The optimizer environment
 *   xasl(in): The XASL block to twiddle
 *   plan(in):
 */
static XASL_NODE *
add_access_spec (QO_ENV * env, XASL_NODE * xasl, QO_PLAN * plan)
{
  PARSER_CONTEXT *parser;
  PT_NODE *class_spec;
  PT_NODE *key_pred = NULL;
  PT_NODE *access_pred = NULL;
  PT_NODE *if_pred = NULL;
  PT_NODE *instnum_pred = NULL;
  QO_XASL_INDEX_INFO *info = NULL;

  if (!xasl)
    {				/* may be invalid argument */
      return xasl;
    }

  assert (plan->plan_type == QO_PLANTYPE_SCAN);

  parser = QO_ENV_PARSER (env);

  class_spec = QO_NODE_ENTITY_SPEC (plan->plan_un.scan.node);

  /* set the type for XASL generation */
  if (PT_IS_VALUE_QUERY (env->pt_tree))
    {
      PT_SET_VALUE_QUERY (class_spec);
    }

  info = qo_get_xasl_index_info (env, plan);
  make_pred_from_plan (env, plan, &key_pred, &access_pred, info, NULL);

  xasl->spec_list = pt_to_spec_list (parser, class_spec, key_pred, access_pred, plan, info, NULL, NULL);
  if (xasl->spec_list == NULL)
    {
      goto exit_on_error;
    }

  xasl->val_list = pt_to_val_list (parser, class_spec->info.spec.id);
  if (xasl->val_list == NULL)
    {
      goto exit_on_error;
    }

  if_pred = make_if_pred_from_plan (env, plan);
  instnum_pred = make_instnum_pred_from_plan (env, plan);

  if (env->pt_tree->node_type == PT_SELECT && env->pt_tree->info.query.q.select.connect_by)
    {
      pt_set_level_node_etc (parser, if_pred, &xasl->level_val);
      pt_set_isleaf_node_etc (parser, if_pred, &xasl->isleaf_val);
      pt_set_iscycle_node_etc (parser, if_pred, &xasl->iscycle_val);
      pt_set_connect_by_operator_node_etc (parser, if_pred, xasl);
      pt_set_qprior_node_etc (parser, if_pred, xasl);
    }

  xasl = add_if_predicate (env, xasl, if_pred);
  xasl = pt_to_instnum_pred (QO_ENV_PARSER (env), xasl, instnum_pred);

success:

  /* free pointer node list */
  parser_free_tree (parser, key_pred);
  parser_free_tree (parser, access_pred);
  parser_free_tree (parser, if_pred);
  parser_free_tree (parser, instnum_pred);

  qo_free_xasl_index_info (env, info);

  return xasl;

exit_on_error:

  xasl = NULL;
  goto success;
}

/*
 * add_scan_proc () - Add the scan proc to the end of xasl's scan_ptr list
 *   return: XASL_NODE *
 *   env(in): The optimizer environment
 *   xasl(in): The XASL block to receive the scan block
 *   scan(in): The scanproc to be added
 */
static XASL_NODE *
add_scan_proc (QO_ENV * env, XASL_NODE * xasl, XASL_NODE * scan)
{
  XASL_NODE *xp;

  if (xasl)
    {
      for (xp = xasl; xp->scan_ptr; xp = xp->scan_ptr)
	;
      xp->scan_ptr = scan;
    }
  else
    xasl = NULL;

  return xasl;
}

/*
 * add_fetch_proc () - Create a fetch proc and add it to the *head*
 *			of the list of fetch procs in xasl->fptr
 *   return: XASL_NODE *
 *   env(in): The optimizer environment
 *   xasl(in): The XASL block to receive the fetch block
 *   procs(in): The fetch proc to be added
 */
static XASL_NODE *
add_fetch_proc (QO_ENV * env, XASL_NODE * xasl, XASL_NODE * procs)
{
  XASL_NODE *f;

  if (xasl)
    {
      /*
       * The idea here is that we want these fetches to run *every
       * time* a new candidate row is produced by xasl, which means
       * they should go at the end of this proc's fptr_list.
       */
      for (f = xasl; f->fptr_list; f = f->fptr_list)
	;
      f->fptr_list = procs;
    }
  else
    {
      xasl = NULL;
    }

  return xasl;
}

/*
 * add_uncorrelated () - Add the scan proc to the *head* of the list of
 *			 scanprocs in xasl->scan_ptr
 *   return: XASL_NODE *
 *   env(in): The optimizer environment
 *   xasl(in): The XASL block to receive the scan block
 *   sub(in): The XASL thing to be added to xasl's list of uncorrelated
 *	      "subqueries"
 */
static XASL_NODE *
add_uncorrelated (QO_ENV * env, XASL_NODE * xasl, XASL_NODE * sub)
{

  if (xasl && sub)
    {
      xasl->aptr_list = pt_remove_xasl (pt_append_xasl (xasl->aptr_list, sub), xasl);
    }
  else
    {
      xasl = NULL;
    }

  return xasl;
}

/*
 * add_subqueries () - Add the xasl trees for the subqueries to the xasl node
 *   return: XASL_NODE *
 *   env(in): The optimizer environment
 *   xasl(in): The XASL block to receive the scan block
 *   subqueries(in): A bitset representing the correlated subqueries that
 *		     should be tacked onto xasl
 *
 * Note: Because of the way the outer driver controls
 *	things, we never have to worry about subqueries that nested
 *	deeper than one, so there is no ordering that needs to be
 *	maintained here; we can just put these guys on the d-list in
 *	any convenient order.
 */
static XASL_NODE *
add_subqueries (QO_ENV * env, XASL_NODE * xasl, BITSET * subqueries)
{
  BITSET_ITERATOR bi;
  int i;
  XASL_NODE *sub_xasl;
  QO_SUBQUERY *subq;

  if (xasl)
    {
      for (i = bitset_iterate (subqueries, &bi); i != -1; i = bitset_next_member (&bi))
	{
	  subq = &env->subqueries[i];
	  sub_xasl = (XASL_NODE *) subq->node->info.query.xasl;
	  if (sub_xasl)
	    {
	      if (bitset_is_empty (&(subq->nodes)))
		{		/* uncorrelated */
		  xasl->aptr_list = pt_remove_xasl (pt_append_xasl (xasl->aptr_list, sub_xasl), xasl);
		}
	      else
		{		/* correlated */
		  xasl->dptr_list = pt_remove_xasl (pt_append_xasl (xasl->dptr_list, sub_xasl), xasl);
		}
	    }
	}
    }

  return xasl;
}

/*
 * add_sort_spec () -
 *   return: XASL_NODE *
 *   env(in): The optimizer environment
 *   xasl(in): The XASL node that should build a sorted result
 *   plan(in): The plan that needs sorting
 *   instnum_flag(in): instnum indicator
 */
static XASL_NODE *
add_sort_spec (QO_ENV * env, XASL_NODE * xasl, QO_PLAN * plan, DB_VALUE * ordby_val, bool instnum_flag)
{
  QO_PLAN *subplan;

  subplan = plan->plan_un.sort.subplan;

  /*
   * xasl->orderby_list for m-join is added in make_mergelist_proc()
   */

  if (instnum_flag)
    {
      if (xasl && subplan->plan_type == QO_PLANTYPE_JOIN
	  && subplan->plan_un.join.join_method == QO_JOINMETHOD_MERGE_JOIN)
	{
	  PT_NODE *instnum_pred;

	  instnum_pred = make_instnum_pred_from_plan (env, plan);
	  xasl = pt_to_instnum_pred (QO_ENV_PARSER (env), xasl, instnum_pred);
	  /* free pointer node list */
	  parser_free_tree (QO_ENV_PARSER (env), instnum_pred);
	}
    }

  if (xasl && plan->plan_un.sort.sort_type == SORT_LIMIT)
    {
      /* setup ORDER BY list here */
      int ordbynum_flag;
      QO_LIMIT_INFO *limit_infop;
      PARSER_CONTEXT *parser = QO_ENV_PARSER (env);
      PT_NODE *query = QO_ENV_PT_TREE (env);
      PT_NODE *upper_bound = NULL, *save_next = NULL;
      bool free_upper_bound = false;

      xasl->orderby_list = pt_to_orderby (parser, query->info.query.order_by, query);
      XASL_CLEAR_FLAG (xasl, XASL_SKIP_ORDERBY_LIST);

      xasl->orderby_limit = NULL;
      /* A SORT-LIMIT plan can only handle the upper limit of the orderby_num predicate. This is because the
       * orderby_num pred will be applied twice: once for the SORT-LIMIT plan and once for the top plan. If the lower
       * bound is evaluated twice, some tuples are lost. */
      upper_bound = query->info.query.orderby_for;
      upper_bound = qo_get_orderby_num_upper_bound_node (parser, upper_bound, &free_upper_bound);
      if (upper_bound == NULL)
	{
	  /* Must have an upper limit if we're considering a SORT-LIMIT plan. */
	  return NULL;
	}
      save_next = upper_bound->next;
      upper_bound->next = NULL;
      ordbynum_flag = 0;
      xasl->ordbynum_pred = pt_to_pred_expr_with_arg (parser, upper_bound, &ordbynum_flag);
      upper_bound->next = save_next;
      if (free_upper_bound)
	{
	  parser_free_tree (parser, upper_bound);
	}

      if (ordbynum_flag & PT_PRED_ARG_ORDBYNUM_CONTINUE)
	{
	  xasl->ordbynum_flag = XASL_ORDBYNUM_FLAG_SCAN_CONTINUE;
	}
      limit_infop = qo_get_key_limit_from_ordbynum (parser, plan, xasl, false);
      if (limit_infop)
	{
	  xasl->orderby_limit = limit_infop->upper;
	  db_private_free (NULL, limit_infop);
	}
      xasl->ordbynum_val = ordby_val;
    }

  return xasl;
}

/*
 * add_if_predicate () - Tack the predicate onto the XASL node's if_pred list
 *   return: XASL_NODE *
 *   env(in): The optimizer environment
 *   xasl(in): The XASL block to which we should add the predicate
 *   pred(in): The pt predicate to tacked on to xasl
 */
static XASL_NODE *
add_if_predicate (QO_ENV * env, XASL_NODE * xasl, PT_NODE * pred)
{
  PARSER_CONTEXT *parser;

  if (xasl && pred)
    {
      parser = QO_ENV_PARSER (env);
      xasl->if_pred = pt_to_pred_expr (parser, pred);
    }

  return xasl;
}

/*
 * add_after_join_predicate () -
 *   return:
 *   env(in):
 *   xasl(in):
 *   pred(in):
 */
static XASL_NODE *
add_after_join_predicate (QO_ENV * env, XASL_NODE * xasl, PT_NODE * pred)
{
  PARSER_CONTEXT *parser;

  if (xasl && pred)
    {
      parser = QO_ENV_PARSER (env);
      xasl->after_join_pred = pt_to_pred_expr (parser, pred);
    }

  return xasl;
}

/*
 * path_access_term () -
 *   return:
 *   term(in):
 */
static int
path_access_term (QO_TERM * term)
{
  return QO_IS_PATH_TERM (term);
}

/*
 * path_if_term () -
 *   return:
 *   term(in):
 */
static int
path_if_term (QO_TERM * term)
{
  return !QO_IS_PATH_TERM (term) && !is_totally_after_join_term (term);
}

/*
 * is_normal_access_term () -
 *   return:
 *   term(in):
 */
static int
is_normal_access_term (QO_TERM * term)
{
  if (!bitset_is_empty (&(QO_TERM_SUBQUERIES (term))))
    {
      return 0;
    }

  if (QO_TERM_CLASS (term) == QO_TC_OTHER
      /* || QO_TERM_CLASS(term) == QO_TC_DURING_JOIN || */
      /* nl outer join treats during join terms as sarged terms of inner */
      || QO_TERM_CLASS (term) == QO_TC_AFTER_JOIN || QO_TERM_CLASS (term) == QO_TC_TOTALLY_AFTER_JOIN)
    {
      return 0;
    }

  return 1;
}

/*
 * is_normal_if_term () -
 *   return:
 *   term(in):
 */
static int
is_normal_if_term (QO_TERM * term)
{
  if (!bitset_is_empty (&(QO_TERM_SUBQUERIES (term))))
    {
      return 1;
    }
  if (QO_TERM_CLASS (term) == QO_TC_OTHER)
    {
      return 1;
    }

  return 0;
}

/*
 * is_after_join_term () -
 *   return:
 *   term(in):
 */
static int
is_after_join_term (QO_TERM * term)
{
  if (!bitset_is_empty (&(QO_TERM_SUBQUERIES (term))))
    {
      return 0;
    }
  if (QO_TERM_CLASS (term) == QO_TC_AFTER_JOIN)
    {
      return 1;
    }

  return 0;
}

/*
 * is_totally_after_join_term () -
 *   return:
 *   term(in):
 */
static int
is_totally_after_join_term (QO_TERM * term)
{
  if (!bitset_is_empty (&(QO_TERM_SUBQUERIES (term))))
    {
      return 0;
    }
  if (QO_TERM_CLASS (term) == QO_TC_TOTALLY_AFTER_JOIN)
    {
      return 1;
    }

  return 0;
}

/*
 * is_follow_if_term () -
 *   return:
 *   term(in):
 */
static int
is_follow_if_term (QO_TERM * term)
{
  if (QO_TERM_CLASS (term) == QO_TC_DURING_JOIN	/* ? */
      || QO_TERM_CLASS (term) == QO_TC_AFTER_JOIN || QO_TERM_CLASS (term) == QO_TC_TOTALLY_AFTER_JOIN)
    {
      return 0;
    }

  return 1;
}

/*
 * is_always_true () -
 *   return:
 *   term(in):
 */
static int
is_always_true (QO_TERM * term)
{
  return true;
}

/*
 * make_pred_from_bitset () -
 *   return: PT_NODE *
 *   env(in): The optimizer environment
 *   predset(in): The bitset of predicates to turn into a predicate tree
 *   safe(in): A function to test whether a particular term should be
 *	       put on a predicate
 *
 * Note: make pred_info * style predicates from a bitset of conjuncts.
 *    use only those conjuncts that can be put on an access pred.
 */
static PT_NODE *
make_pred_from_bitset (QO_ENV * env, BITSET * predset, ELIGIBILITY_FN safe)
{
  PARSER_CONTEXT *parser;
  PT_NODE *pred_list, *pointer, *prev, *curr;
  BITSET_ITERATOR bi;
  int i;
  QO_TERM *term;
  bool found;
  PT_NODE *pt_expr;
  double cmp;

  parser = QO_ENV_PARSER (env);

  pred_list = NULL;		/* init */
  for (i = bitset_iterate (predset, &bi); i != -1; i = bitset_next_member (&bi))
    {
      term = QO_ENV_TERM (env, i);

      /* Don't ever let one of our fabricated terms find its way into the predicate; that will cause serious confusion. */
      if (QO_IS_FAKE_TERM (term) || !(*safe) (term))
	{
	  continue;
	}

      /* We need to use predicate pointer. modifying WHERE clause structure in place gives us no way to compile the
       * query if the optimizer bails out. */
      pt_expr = QO_TERM_PT_EXPR (term);
      if (pt_expr == NULL)
	{
	  /* is possible ? */
	  goto exit_on_error;
	}
      pointer = pt_point (parser, pt_expr);
      if (pointer == NULL)
	{
	  goto exit_on_error;
	}

      /* insert to the AND predicate list; this order is used at pt_to_pred_expr_with_arg() */
      pointer->next = pred_list;
      pred_list = pointer;
    }

  return pred_list;

exit_on_error:

  if (pred_list)
    {
      parser_free_tree (parser, pred_list);
    }

  return NULL;
}

/*
 * make_pred_from_plan () -
 *   return:
 *   env(in): The optimizer environment
 *   plan(in): Query plan
 *   key_predp(in): Index information of query plan.
 *		    Predicate tree to be used as key filter
 *   predp(in): Predicate tree to be used as data filter
 *   qo_index_infop(in):
 *
 * Note: Make a PT_NODE * style predicate from a bitset of conjuncts.
 *     Splits sargs into key filter predicates and data filter predicates.
 */
static void
make_pred_from_plan (QO_ENV * env, QO_PLAN * plan, PT_NODE ** key_predp, PT_NODE ** predp,
		     QO_XASL_INDEX_INFO * qo_index_infop, PT_NODE ** hash_predp)
{
  QO_INDEX_ENTRY *index_entryp = NULL;

  /* initialize output parameter */
  if (key_predp != NULL)
    {
      *key_predp = NULL;
    }
  if (predp != NULL)
    {
      *predp = NULL;
    }
  if (hash_predp != NULL)
    {
      *hash_predp = NULL;
    }

  if (plan->plan_type == QO_PLANTYPE_FOLLOW)
    {
      /* Don't allow predicates to migrate to fetch_proc access specs; the special handling of NULL doesn't look at the
       * access spec, so it will miss predicates that are deposited there.  Always put these things on the if_pred for
       * now. This needs to get fixed. >>>> Note the same problem is encountered when emulating follow with >>>> joins.
       * The access pred must return a row, even if its null. >>>> the rest or the predicate may then be applied. */
      return;
    }

  /* This is safe guard code - DO NOT DELETE ME */
  do
    {
      /* exclude key-range terms from key-filter terms */
      bitset_difference (&(plan->plan_un.scan.kf_terms), &(plan->plan_un.scan.terms));

      /* exclude key-range terms from sarged terms */
      bitset_difference (&(plan->sarged_terms), &(plan->plan_un.scan.terms));
      /* exclude key-filter terms from sarged terms */
      bitset_difference (&(plan->sarged_terms), &(plan->plan_un.scan.kf_terms));
    }
  while (0);

  /* make predicate list for hash key */
  if (hash_predp != NULL)
    {
      *hash_predp = make_pred_from_bitset (env, &(plan->plan_un.scan.hash_terms), is_always_true);
    }
  /* if key filter(predicates) is not required */
  if (predp != NULL && (key_predp == NULL || qo_index_infop == NULL))
    {
      *predp = (make_pred_from_bitset (env, &(plan->sarged_terms), bitset_has_path (env, &(plan->sarged_terms))
				       ? path_access_term : is_normal_access_term));
      return;
    }

  /* make predicate list for key filter */
  if (key_predp != NULL)
    {
      if (qo_index_infop && qo_index_infop->need_copy_multi_range_term != -1 && qo_index_infop->need_copy_to_sarg_term)
	{
	  bitset_add (&(plan->sarged_terms), qo_index_infop->need_copy_multi_range_term);
	}
      else if (qo_index_infop->need_copy_multi_range_term != -1)
	{
	  index_entryp = qo_index_infop->ni_entry->head;
	  if (index_entryp && index_entryp->constraints && index_entryp->constraints->func_index_info
	      && index_entryp->cover_segments == false)
	    {
	      /* if predicate has function index column then do not permit key-filter. so force-copy to sarg */
	      bitset_add (&(plan->sarged_terms), qo_index_infop->need_copy_multi_range_term);
	    }
	  else
	    {
	      /*force-copy multi col range pred to key filter */
	      bitset_add (&(plan->plan_un.scan.kf_terms), qo_index_infop->need_copy_multi_range_term);
	    }
	}
      *key_predp = make_pred_from_bitset (env, &(plan->plan_un.scan.kf_terms), is_always_true);
    }

  /* make predicate list for data filter */
  if (predp != NULL)
    {
      *predp = (make_pred_from_bitset (env, &(plan->sarged_terms), bitset_has_path (env, &(plan->sarged_terms))
				       ? path_access_term : is_normal_access_term));
    }
}

/*
 * make_if_pred_from_plan () -
 *   return:
 *   env(in):
 *   plan(in):
 */
static PT_NODE *
make_if_pred_from_plan (QO_ENV * env, QO_PLAN * plan)
{
  ELIGIBILITY_FN test;

  if (plan->plan_type == QO_PLANTYPE_FOLLOW)
    {
      /*
       * Put all predicates on the if_pred right now, because the "dead
       * end" handling for NULLs won't look at predicates on the access
       * spec.
       *
       * This needs to get fixed.
       */
      test = is_follow_if_term;
    }
  else
    {
      test = bitset_has_path (env, &(plan->sarged_terms)) ? path_if_term : is_normal_if_term;
    }

  return make_pred_from_bitset (env, &(plan->sarged_terms), test);
}

/*
 * make_instnum_pred_from_plan () -
 *   return:
 *   env(in):
 *   plan(in):
 */
static PT_NODE *
make_instnum_pred_from_plan (QO_ENV * env, QO_PLAN * plan)
{
  /* is it enough? */
  return make_pred_from_bitset (env, &(plan->sarged_terms), is_totally_after_join_term);
}

/*
 * make_namelist_from_projected_segs () -
 *   return: PT_NODE *
 *   env(in): The optimizer environment
 *   plan(in): he plan whose projected segments need to be put into a name list
 *
 * Note: Take a bitset of segment indexes and produce a name list
 *	suitable for creating the outptr_list member of a buildlist
 *	proc.  This is used by the creators of temporary list files:
 *	merge joins and sorts.
 *
 *	In the interests of sanity, the elements in the list appear
 *	in the same order as the indexes in the scan of the bitset.
 */
static PT_NODE *
make_namelist_from_projected_segs (QO_ENV * env, QO_PLAN * plan)
{
  PARSER_CONTEXT *parser;
  PT_NODE *namelist;
  PT_NODE **namelistp;
  BITSET_ITERATOR bi;
  int i;

  parser = QO_ENV_PARSER (env);
  namelist = NULL;
  namelistp = &namelist;

  for (i = bitset_iterate (&((plan->info)->projected_segs), &bi); namelistp != NULL && i != -1;
       i = bitset_next_member (&bi))
    {
      QO_SEGMENT *seg;
      PT_NODE *name;

      seg = QO_ENV_SEG (env, i);
      name = pt_point (parser, QO_SEG_PT_NODE (seg));

      *namelistp = name;
      namelistp = &name->next;
    }

  return namelist;
}

/*
 * check_merge_xasl () -
 *   return:
 *   env(in):
 *   xasl(in):
 */
static XASL_NODE *
check_merge_xasl (QO_ENV * env, XASL_NODE * xasl)
{
  XASL_NODE *merge;
  int i, ncols;

  /*
   * NULL is actually a semi-common case; it can arise under timeout
   * conditions, etc.
   */
  if (xasl == NULL)
    {
      return NULL;
    }

  /*
   * The mergelist proc isn't necessarily the first thing on the
   * aptr_list; some other procs may have found their way in front of
   * it, and that's not incorrect.  Search until we find a mergelist
   * proc; is there any way to have more than one?
   */
  for (merge = xasl->aptr_list; merge && merge->type != MERGELIST_PROC; merge = merge->next)
    ;

  if (merge == NULL
      /*
       * Make sure there are two things on the aptr list.
       */
      || merge->type != MERGELIST_PROC || merge->aptr_list == NULL	/* left */
      || merge->aptr_list->next == NULL	/* right */
      /*
       * Make sure both buildlist gadgets look well-formed.
       */
      || xasl->spec_list == NULL || xasl->val_list == NULL || xasl->outptr_list == NULL
      /*
       * Make sure the merge_list_info looks plausible.
       */
      || merge->proc.mergelist.outer_xasl == NULL || merge->proc.mergelist.inner_xasl == NULL
      || merge->proc.mergelist.ls_merge.ls_column_cnt <= 0)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_FAILED_ASSERTION, 1, "false");
      xasl = NULL;
    }

  if (merge != NULL)
    {
      ncols = merge->proc.mergelist.ls_merge.ls_column_cnt;
      for (i = 0; i < ncols; i++)
	{
	  if (merge->proc.mergelist.ls_merge.ls_outer_column[i] < 0
	      || merge->proc.mergelist.ls_merge.ls_inner_column[i] < 0)
	    {
	      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_FAILED_ASSERTION, 1, "false");
	      xasl = NULL;
	      break;
	    }
	}
    }

  return xasl;
}

/*
 * make_outer_instnum () -
 *   return:
 *   env(in):
 *   outer(in):
 *   plan(in):
 */
static void
make_outer_instnum (QO_ENV * env, QO_PLAN * outer, QO_PLAN * plan)
{
  int t;
  BITSET_ITERATOR iter;
  QO_TERM *termp;

  for (t = bitset_iterate (&(plan->sarged_terms), &iter); t != -1; t = bitset_next_member (&iter))
    {
      termp = QO_ENV_TERM (env, t);

      if (is_totally_after_join_term (termp))
	{
	  bitset_add (&(outer->sarged_terms), t);
	}
    }
}

/*
 * gen_outer () -
 *   return: XASL_NODE *
 *   env(in): The optimizer environment
 *   plan(in): The (sub)plan to generate code for
 *   subqueries(in): The set of subqueries that need to be reevaluated every
 *		     time a new row is produced by plan
 *   inner_scans(in): A list of scan
 *   fetches(in): A list of fetch procs that should be executed every time plan
 *		  produces a new row
 *   xasl(in): The xasl node that is receiving the various procs we generate
 */
static XASL_NODE *
gen_outer (QO_ENV * env, QO_PLAN * plan, BITSET * subqueries, XASL_NODE * inner_scans, XASL_NODE * fetches,
	   XASL_NODE * xasl)
{
  PARSER_CONTEXT *parser;
  XASL_NODE *scan, *listfile, *merge, *fetch;
  QO_PLAN *outer, *inner;
  JOIN_TYPE join_type = NO_JOIN;
  QO_TERM *term;
  int i;
  BITSET_ITERATOR bi;
  BITSET new_subqueries;
  BITSET fake_subqueries;
  BITSET predset;
  BITSET taj_terms;

  if (env == NULL)
    {
      return NULL;
    }
  parser = QO_ENV_PARSER (env);

  if (parser == NULL || plan == NULL || xasl == NULL)
    {
      return NULL;
    }

  bitset_init (&new_subqueries, env);
  bitset_init (&fake_subqueries, env);
  bitset_init (&predset, env);
  bitset_init (&taj_terms, env);

  /* set subqueries */
  bitset_assign (&new_subqueries, subqueries);
  bitset_union (&new_subqueries, &(plan->subqueries));

  /* set predicates */
  bitset_assign (&predset, &(plan->sarged_terms));

  switch (plan->plan_type)
    {
    case QO_PLANTYPE_JOIN:
      join_type = plan->plan_un.join.join_type;

      /*
       * The join terms may be EMPTY if this "join" is actually a
       * cartesian product, or if it has been implemented as an
       * index scan on the inner term (in which case it has already
       * been placed in the inner plan as the index term).
       */
      bitset_union (&predset, &(plan->plan_un.join.join_terms));

      /* outer join could have terms classed as AFTER JOIN TERM; setting after join terms to merged list scan */
      if (IS_OUTER_JOIN_TYPE (join_type))
	{
	  bitset_union (&predset, &(plan->plan_un.join.during_join_terms));
	  bitset_union (&predset, &(plan->plan_un.join.after_join_terms));
	}
      break;

    case QO_PLANTYPE_FOLLOW:
      /* include follow edge */
      bitset_add (&predset, QO_TERM_IDX (plan->plan_un.follow.path));
      break;

    default:
      break;
    }

  /*
   * Because this routine tail-calls itself in several common cases, we
   * could implement those tail calls with a loop back to the beginning
   * of the code.  However, because these calls won't get very deep in
   * practice (~10 deep), I've left the code as is in the interest of
   * clarity.
   */

  switch (plan->plan_type)
    {
    case QO_PLANTYPE_SCAN:
      /*
       * This case only needs to attach the access spec to the incoming
       * XASL node.  The remainder of the interesting initialization
       * (e.g., the val list) of that XASL node is expected to be
       * performed by the caller.
       */
      xasl = add_access_spec (env, xasl, plan);
      xasl = add_scan_proc (env, xasl, inner_scans);
      xasl = add_fetch_proc (env, xasl, fetches);
      xasl = add_subqueries (env, xasl, &new_subqueries);
      break;

    case QO_PLANTYPE_SORT:
      /*
       * check for top level plan
       */
      if (plan->top_rooted)
	{
	  if (plan->plan_un.sort.sort_type == SORT_TEMP)
	    {
	      ;			/* nop */
	    }
	  else
	    {
	      /* SORT-LIMIT plans should never be top rooted */
	      assert (plan->plan_un.sort.sort_type != SORT_LIMIT);
	      xasl = gen_outer (env, plan->plan_un.sort.subplan, &new_subqueries, inner_scans, fetches, xasl);
	      return xasl;
	    }
	}

      /*
       * If inner_scans is not empty, this plan is really a subplan of
       * some outer join node, and we need to make xasl scan the
       * contents of the temp file intended to be created by this plan.
       * If not, we're really at the "top" of a tree (we haven't gone
       * through a join node yet) and we can simply recurse, tacking on
       * our sort spec after the recursion. The exception to this rule is
       * the SORT-LIMIT plan which must always be working on a temp file.
       */
      if (inner_scans != NULL || plan->plan_un.sort.sort_type == SORT_LIMIT)
	{
	  PT_NODE *namelist = NULL;

	  namelist = make_namelist_from_projected_segs (env, plan);
	  if (plan->plan_un.sort.sort_type == SORT_LIMIT)
	    {
	      listfile = make_sort_limit_proc (env, plan, namelist, xasl);
	    }
	  else
	    {
	      listfile = make_buildlist_proc (env, namelist);
	      listfile = gen_outer (env, plan->plan_un.sort.subplan, &EMPTY_SET, NULL, NULL, listfile);
	      listfile = add_sort_spec (env, listfile, plan, xasl->ordbynum_val, false);
	    }

	  xasl = add_uncorrelated (env, xasl, listfile);
	  xasl = init_list_scan_proc (env, xasl, listfile, namelist, &(plan->sarged_terms), NULL);
	  if (namelist)
	    {
	      parser_free_tree (parser, namelist);
	    }
	  xasl = add_scan_proc (env, xasl, inner_scans);
	  xasl = add_fetch_proc (env, xasl, fetches);
	  xasl = add_subqueries (env, xasl, &new_subqueries);
	}
      else
	{
	  xasl = gen_outer (env, plan->plan_un.sort.subplan, &new_subqueries, inner_scans, fetches, xasl);
	  xasl = add_sort_spec (env, xasl, plan, NULL, true /* add instnum pred */ );
	}
      break;

    case QO_PLANTYPE_JOIN:

      outer = plan->plan_un.join.outer;
      inner = plan->plan_un.join.inner;

      switch (plan->plan_un.join.join_method)
	{
	case QO_JOINMETHOD_NL_JOIN:
	  /* check for cselect of method */
	  if (join_type == JOIN_CSELECT)
	    {
	      xasl = pt_gen_simple_merge_plan (parser, QO_ENV_PT_TREE (env), plan, xasl);
	      break;
	    }
	  /* FALLTHRU */
	case QO_JOINMETHOD_IDX_JOIN:
	  for (i = bitset_iterate (&(plan->plan_un.join.join_terms), &bi); i != -1; i = bitset_next_member (&bi))
	    {
	      term = QO_ENV_TERM (env, i);
	      if (QO_IS_FAKE_TERM (term))
		{
		  bitset_union (&fake_subqueries, &(QO_TERM_SUBQUERIES (term)));
		}
	    }

	  bitset_difference (&new_subqueries, &fake_subqueries);

	  for (i = bitset_iterate (&predset, &bi); i != -1; i = bitset_next_member (&bi))
	    {
	      term = QO_ENV_TERM (env, i);
	      if (is_totally_after_join_term (term))
		{
		  bitset_add (&taj_terms, i);
		}
	      else if (is_normal_access_term (term))
		{
		  /* Check if join term can be pushed to key filter instead of sargable terms. The index used for inner
		   * index scan must include all term segments that belong to inner node */
		  if (qo_is_index_covering_scan (inner) || qo_plan_multi_range_opt (inner))
		    {
		      /* Coverage indexes and indexes using multi range optimization are certified to include segments
		       * from inner node */
		      bitset_add (&(inner->plan_un.scan.kf_terms), i);
		      bitset_difference (&predset, &(inner->plan_un.scan.kf_terms));
		    }
		  else if (qo_is_iscan (plan))
		    {
		      /* check that index covers all segments */
		      BITSET term_segs, index_segs;
		      QO_INDEX_ENTRY *idx_entryp = NULL;
		      int j;

		      /* create bitset including index segments */
		      bitset_init (&index_segs, env);
		      idx_entryp = inner->plan_un.scan.index->head;
		      for (j = 0; j < idx_entryp->nsegs; j++)
			{
			  bitset_add (&index_segs, idx_entryp->seg_idxs[j]);
			}

		      /* create bitset including term segments that belong to inner node */
		      bitset_init (&term_segs, env);
		      bitset_union (&term_segs, &term->segments);
		      bitset_intersect (&term_segs, &(QO_NODE_SEGS (plan->plan_un.scan.node)));

		      /* check that term_segs is covered by index_segs */
		      bitset_difference (&term_segs, &index_segs);
		      if (bitset_is_empty (&term_segs))
			{
			  /* safe to add term to key filter terms */
			  bitset_add (&(inner->plan_un.scan.kf_terms), i);
			  bitset_difference (&predset, &(inner->plan_un.scan.kf_terms));
			}
		    }
		}
	    }
	  /* exclude totally after join term and push into inner */
	  bitset_difference (&predset, &taj_terms);

	  /* copy hash join term to inner for hash list scan */
	  if (qo_is_seq_scan (inner) && !bitset_is_empty (&(plan->plan_un.join.hash_terms)))
	    {
	      bitset_assign (&(inner->plan_un.scan.hash_terms), &(plan->plan_un.join.hash_terms));
	    }

	  /*
	   * In case of outer join, we should not use sarg terms as key filter terms.
	   * If not, a term, which should be applied after single scan, can be applied
	   * during btree_range_search. It means that there can be no records fetched
	   * by single scan due to key filtering, and null records can be returned
	   * by scan_handle_single_scan. It might lead to making a wrong result.
	   */
	  scan = gen_inner (env, inner, &predset, &new_subqueries, inner_scans, fetches);
	  if (scan)
	    {
	      if (IS_OUTER_JOIN_TYPE (join_type))
		{
		  mark_access_as_outer_join (parser, scan);
		}
	    }
	  bitset_assign (&new_subqueries, &fake_subqueries);
	  make_outer_instnum (env, outer, plan);
	  xasl = gen_outer (env, outer, &new_subqueries, scan, NULL, xasl);
	  break;

	case QO_JOINMETHOD_MERGE_JOIN:
	  /*
	   * The optimizer isn't supposed to produce plans in which a
	   * merge join isn't "shielded" by a sort (temp file) plan,
	   * precisely because XASL has a difficult time coping with
	   * that.  Because of that, inner_scans should ALWAYS be NULL
	   * here.
	   */
	  assert (inner_scans == NULL);
	  if (inner_scans)
	    {
	      xasl = NULL;
	      break;
	    }

	  /*
	   * In this case, we have to hold on to the accumulated
	   * predicates and subqueries, and tack them on to the scan
	   * proc that eventually reads the result of the join.  The
	   * subplans for the two join components should start with
	   * clean slates.
	   */
	  {

	    XASL_NODE *left_xasl, *rght_xasl;
	    PT_NODE *left_elist, *left_nlist, *left_list, *left;
	    PT_NODE *rght_elist, *rght_nlist, *rght_list, *rght;
	    PT_NODE *seg_nlist, *pt_expr;
	    int left_nlen, rght_nlen, seg_nlen;
	    int *seg_pos_list;
	    BITSET plan_segs, temp_segs, left_exprs, rght_exprs;

	    bitset_init (&plan_segs, env);
	    bitset_init (&temp_segs, env);
	    bitset_init (&left_exprs, env);
	    bitset_init (&rght_exprs, env);

	    /* init */
	    left_nlist = rght_nlist = left_list = NULL;
	    left_elist = rght_elist = rght_list = NULL;

	    seg_nlist = NULL;
	    seg_pos_list = NULL;

	    /* find outer/inner segs from expr and name */
	    bitset_assign (&plan_segs, &((plan->info)->projected_segs));
	    for (i = bitset_iterate (&predset, &bi); i != -1; i = bitset_next_member (&bi))
	      {

		term = QO_ENV_TERM (env, i);

		if (BITSET_MEMBER (plan->plan_un.join.join_terms, i))
		  {		/* is m-join edge */

		    BITSET_CLEAR (temp_segs);	/* init */

		    pt_expr = QO_TERM_PT_EXPR (term);
		    qo_expr_segs (env, pt_left_part (pt_expr), &temp_segs);

		    /* is lhs matching outer ? */
		    if (bitset_intersects (&temp_segs, &((outer->info)->projected_segs)))
		      {
			left = pt_left_part (pt_expr);
			rght = pt_right_part (pt_expr);
			if (pt_expr->info.expr.op == PT_RANGE && rght != NULL)
			  {
			    rght = rght->info.expr.arg1;
			  }
		      }
		    else
		      {
			rght = pt_left_part (pt_expr);
			left = pt_right_part (pt_expr);
			if (pt_expr->info.expr.op == PT_RANGE && left != NULL)
			  {
			    left = left->info.expr.arg1;
			  }
		      }

		    if (pt_is_expr_node (left) || pt_is_function (left))
		      {
			/* append to the expr list */
			left_elist = parser_append_node (pt_point (parser, left), left_elist);
			bitset_add (&left_exprs, i);
		      }
		    else
		      {
			bitset_union (&plan_segs, &(QO_TERM_SEGS (term)));
		      }

		    if (pt_is_expr_node (rght) || pt_is_function (rght))
		      {
			/* append to the expr list */
			rght_elist = parser_append_node (pt_point (parser, rght), rght_elist);
			bitset_add (&rght_exprs, i);
		      }
		    else
		      {
			bitset_union (&plan_segs, &(QO_TERM_SEGS (term)));
		      }
		  }
		else
		  {
		    bitset_union (&plan_segs, &(QO_TERM_SEGS (term)));
		  }
	      }

	    /* build outer segs namelist */
	    bitset_assign (&temp_segs, &((outer->info)->projected_segs));	/* save */

	    bitset_intersect (&((outer->info)->projected_segs), &plan_segs);
	    left_nlist = make_namelist_from_projected_segs (env, outer);
	    left_nlen = pt_length_of_list (left_nlist);	/* only names include */

	    /* make expr, name list */
	    left_list = parser_append_node (left_nlist, left_elist);
	    left_xasl = make_buildlist_proc (env, left_list);
	    left_xasl = gen_outer (env, outer, &EMPTY_SET, NULL, NULL, left_xasl);
	    bitset_assign (&((outer->info)->projected_segs), &temp_segs);	/* restore */

	    /* build inner segs namelist */
	    bitset_assign (&temp_segs, &((inner->info)->projected_segs));	/* save */

	    bitset_intersect (&((inner->info)->projected_segs), &plan_segs);
	    rght_nlist = make_namelist_from_projected_segs (env, inner);
	    rght_nlen = pt_length_of_list (rght_nlist);	/* only names include */

	    /* make expr, name list */
	    rght_list = parser_append_node (rght_nlist, rght_elist);
	    rght_xasl = make_buildlist_proc (env, rght_list);
	    rght_xasl = gen_outer (env, inner, &EMPTY_SET, NULL, NULL, rght_xasl);
	    bitset_assign (&((inner->info)->projected_segs), &temp_segs);	/* restore */

	    merge =
	      make_mergelist_proc (env, plan, left_xasl, left_list, &left_exprs, left_elist, rght_xasl, rght_list,
				   &rght_exprs, rght_elist);
	    if (merge == NULL)
	      {
		xasl = NULL;	/* cause error */

		if (left_list)
		  parser_free_tree (parser, left_list);
		if (rght_list)
		  parser_free_tree (parser, rght_list);

		if (seg_nlist)
		  parser_free_tree (parser, seg_nlist);

		if (seg_pos_list)
		  free_and_init (seg_pos_list);

		bitset_delset (&plan_segs);
		bitset_delset (&temp_segs);
		bitset_delset (&left_exprs);
		bitset_delset (&rght_exprs);
		break;
	      }

	    xasl = add_uncorrelated (env, xasl, merge);

	    /* filter out already applied terms */
	    bitset_difference (&predset, &(plan->plan_un.join.join_terms));
	    if (IS_OUTER_JOIN_TYPE (join_type))
	      {
		bitset_difference (&predset, &(plan->plan_un.join.during_join_terms));
	      }

	    /* set referenced segments */
	    bitset_assign (&plan_segs, &((plan->info)->projected_segs));
	    for (i = bitset_iterate (&predset, &bi); i != -1; i = bitset_next_member (&bi))
	      {
		term = QO_ENV_TERM (env, i);
		bitset_union (&plan_segs, &(QO_TERM_SEGS (term)));
	      }

	    /* generate left name list of projected segs */
	    bitset_assign (&temp_segs, &((outer->info)->projected_segs));
	    bitset_intersect (&temp_segs, &plan_segs);
	    for (i = bitset_iterate (&temp_segs, &bi); i != -1; i = bitset_next_member (&bi))
	      {
		seg_nlist = parser_append_node (pt_point (parser, QO_SEG_PT_NODE (QO_ENV_SEG (env, i))), seg_nlist);
	      }

	    /* generate right name list of projected segs */
	    bitset_assign (&temp_segs, &((inner->info)->projected_segs));
	    bitset_intersect (&temp_segs, &plan_segs);
	    for (i = bitset_iterate (&temp_segs, &bi); i != -1; i = bitset_next_member (&bi))
	      {
		seg_nlist = parser_append_node (pt_point (parser, QO_SEG_PT_NODE (QO_ENV_SEG (env, i))), seg_nlist);
	      }

	    seg_nlen = pt_length_of_list (seg_nlist);

	    /* set used column position in name list and filter out unnecessary join edge segs from projected segs */
	    if (seg_nlen > 0 && seg_nlen < left_nlen + rght_nlen)
	      {
		QFILE_LIST_MERGE_INFO *ls_merge;
		int outer_inner, pos = 0, p;
		PT_NODE *attr;

		seg_pos_list = (int *) malloc (seg_nlen * sizeof (int));
		if (seg_pos_list == NULL)
		  {
		    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, seg_nlen * sizeof (int));
		    xasl = NULL;	/* cause error */

		    if (left_list)
		      {
			parser_free_tree (parser, left_list);
		      }
		    if (rght_list)
		      {
			parser_free_tree (parser, rght_list);
		      }

		    if (seg_nlist)
		      {
			parser_free_tree (parser, seg_nlist);
		      }

		    if (seg_pos_list)
		      {
			free_and_init (seg_pos_list);
		      }

		    bitset_delset (&plan_segs);
		    bitset_delset (&temp_segs);
		    bitset_delset (&left_exprs);
		    bitset_delset (&rght_exprs);
		    break;
		  }

		ls_merge = &merge->proc.mergelist.ls_merge;

		p = 0;		/* init */
		for (attr = seg_nlist; attr; attr = attr->next)
		  {

		    pos = pt_find_attribute (parser, attr, left_list);
		    if (pos == -1)
		      {		/* not found in left */
			pos = pt_find_attribute (parser, attr, rght_list);
			if (pos == -1)
			  {	/* not found in right */
			    break;	/* cause error */
			  }
			outer_inner = QFILE_INNER_LIST;
		      }
		    else
		      {
			outer_inner = QFILE_OUTER_LIST;
		      }

		    for (i = 0; i < ls_merge->ls_pos_cnt; i++)
		      {
			if (ls_merge->ls_outer_inner_list[i] == outer_inner && ls_merge->ls_pos_list[i] == pos)
			  {	/* found */
			    seg_pos_list[p] = i;	/* save position */
			    break;
			  }
		      }

		    if (i >= ls_merge->ls_pos_cnt)
		      {		/* error */
			pos = -1;
			break;	/* cause error */
		      }

		    p++;	/* increase */
		  }

		if (pos == -1)
		  {		/* error */
		    xasl = NULL;	/* cause error */

		    if (left_list)
		      {
			parser_free_tree (parser, left_list);
		      }
		    if (rght_list)
		      {
			parser_free_tree (parser, rght_list);
		      }

		    if (seg_nlist)
		      {
			parser_free_tree (parser, seg_nlist);
		      }

		    if (seg_pos_list)
		      {
			free_and_init (seg_pos_list);
		      }

		    bitset_delset (&plan_segs);
		    bitset_delset (&temp_segs);
		    bitset_delset (&left_exprs);
		    bitset_delset (&rght_exprs);
		    break;
		  }
	      }

	    if (xasl)
	      {
		xasl = init_list_scan_proc (env, xasl, merge, seg_nlist, &predset, seg_pos_list);
		xasl = add_fetch_proc (env, xasl, fetches);
		xasl = add_subqueries (env, xasl, &new_subqueries);
	      }

	    if (left_list)
	      {
		parser_free_tree (parser, left_list);
	      }
	    if (rght_list)
	      {
		parser_free_tree (parser, rght_list);
	      }

	    if (seg_nlist)
	      {
		parser_free_tree (parser, seg_nlist);
	      }

	    if (seg_pos_list)
	      {
		free_and_init (seg_pos_list);
	      }

	    bitset_delset (&plan_segs);
	    bitset_delset (&temp_segs);
	    bitset_delset (&left_exprs);
	    bitset_delset (&rght_exprs);

	  }

	  /*
	   * This can be removed after we trust ourselves some more.
	   */
	  xasl = check_merge_xasl (env, xasl);
	  break;

	default:
	  break;
	}

      break;

    case QO_PLANTYPE_FOLLOW:
      /*
       * Add the fetch proc to the head of the list of fetch procs
       * before recursing.  This means that it will be later in the
       * list than fetch procs that are added during the recursion,
       * which means that those fetches will happen earlier at runtime,
       * which is exactly what we want.  Pass it on down to the inner
       * call, since we don't know exactly where we want to stick it
       * yet.
       */
      fetch = make_fetch_proc (env, plan);
      fetch = add_fetch_proc (env, fetch, fetches);
      fetch = add_subqueries (env, fetch, &new_subqueries);
      make_outer_instnum (env, plan->plan_un.follow.head, plan);
      xasl = gen_outer (env, plan->plan_un.follow.head, &EMPTY_SET, inner_scans, fetch, xasl);
      break;

    case QO_PLANTYPE_WORST:
      xasl = NULL;
      break;
    }

  bitset_delset (&taj_terms);
  bitset_delset (&predset);
  bitset_delset (&fake_subqueries);
  bitset_delset (&new_subqueries);

  return xasl;
}

/*
 * gen_inner () -
 *   return: XASL_NODE *
 *   env(in): The optimizer environment
 *   plan(in): The (sub)plan to generate code for
 *   predset(in): The predicates being pushed down from above
 *   subqueries(in): The subqueries inherited from enclosing plans
 *   inner_scans(in): A list of inner scan procs to be put on this scan's
 *		      scan_ptr list
 *   fetches(in): A list of fetch procs to be run every time plan produces
 *		  a new row
 */
static XASL_NODE *
gen_inner (QO_ENV * env, QO_PLAN * plan, BITSET * predset, BITSET * subqueries, XASL_NODE * inner_scans,
	   XASL_NODE * fetches)
{
  XASL_NODE *scan, *listfile, *fetch;
  PT_NODE *namelist;
  BITSET new_subqueries;

  /*
   * All of the rationale about ordering, etc. presented in the
   * comments in gen_outer also applies here.
   */

  scan = NULL;
  bitset_init (&new_subqueries, env);
  bitset_assign (&new_subqueries, subqueries);
  bitset_union (&new_subqueries, &(plan->subqueries));

  switch (plan->plan_type)
    {
    case QO_PLANTYPE_SCAN:
      /*
       * For nl-join and idx-join, we push join edge to sarg term of
       * inner scan to filter out unsatisfied records earlier.
       */
      bitset_union (&(plan->sarged_terms), predset);

      scan = init_class_scan_proc (env, scan, plan);
      scan = add_scan_proc (env, scan, inner_scans);
      scan = add_fetch_proc (env, scan, fetches);
      scan = add_subqueries (env, scan, &new_subqueries);
      break;

    case QO_PLANTYPE_FOLLOW:
#if 1
      /*
       * We have to take care of any sargs that have been passed down
       * from above.  Go ahead and destructively union them into this
       * plan's sarg set: no one will ever look at the plan again
       * anyway.
       */
      bitset_union (&(plan->sarged_terms), predset);
      fetch = make_fetch_proc (env, plan);
      fetch = add_fetch_proc (env, fetch, fetches);
      fetch = add_subqueries (env, fetch, &new_subqueries);
      /*
       * Now proceed on with inner generation, passing the augmented
       * list of fetch procs.
       */
      scan = gen_inner (env, plan->plan_un.follow.head, &EMPTY_SET, &EMPTY_SET, inner_scans, fetch);
      break;
#else
      /* Fall through */
#endif

    case QO_PLANTYPE_JOIN:
      /*
       * These aren't supposed to show up, but if they do just take the
       * conservative approach of treating them like a sort and
       * whacking their results into a temporary file, and then scan
       * that file.
       */
    case QO_PLANTYPE_SORT:
      /* check for sort type */
      QO_ASSERT (env, plan->plan_un.sort.sort_type == SORT_TEMP);

      namelist = make_namelist_from_projected_segs (env, plan);
      listfile = make_buildlist_proc (env, namelist);
      listfile = gen_outer (env, plan, &EMPTY_SET, NULL, NULL, listfile);
      scan = make_scan_proc (env);
      scan = init_list_scan_proc (env, scan, listfile, namelist, predset, NULL);
      if (namelist)
	{
	  parser_free_tree (env->parser, namelist);
	}
      scan = add_scan_proc (env, scan, inner_scans);
      scan = add_fetch_proc (env, scan, fetches);
      scan = add_subqueries (env, scan, &new_subqueries);
      scan = add_uncorrelated (env, scan, listfile);
      break;

    case QO_PLANTYPE_WORST:
      /*
       * This case should never arise.
       */
      scan = NULL;
      break;
    }

  bitset_delset (&new_subqueries);

  return scan;
}

/*
 * preserve_info () -
 *   return: XASL_NODE *
 *   env(in): The optimizer environment
 *   plan(in): The plan from which the xasl tree was generated
 *   xasl(in): The generated xasl tree
 */
static XASL_NODE *
preserve_info (QO_ENV * env, QO_PLAN * plan, XASL_NODE * xasl)
{
  QO_SUMMARY *summary;
  PARSER_CONTEXT *parser;
  PT_NODE *select;

  if (xasl != NULL)
    {
      parser = QO_ENV_PARSER (env);
      select = QO_ENV_PT_TREE (env);
      summary = (QO_SUMMARY *) parser_alloc (parser, sizeof (QO_SUMMARY));
      if (summary)
	{
	  summary->fixed_cpu_cost = plan->fixed_cpu_cost;
	  summary->fixed_io_cost = plan->fixed_io_cost;
	  summary->variable_cpu_cost = plan->variable_cpu_cost;
	  summary->variable_io_cost = plan->variable_io_cost;
	  summary->cardinality = (plan->info)->cardinality;
	  summary->xasl = xasl;
	  select->info.query.q.select.qo_summary = summary;
	}
      else
	{
	  xasl = NULL;
	}

      /* save info for derived table size estimation */
      if (plan != NULL && xasl != NULL)
	{
	  xasl->projected_size = (plan->info)->projected_size;
	  xasl->cardinality = (plan->info)->cardinality;
	}
    }

  return xasl;
}

/*
 * qo_to_xasl () -
 *   return: XASL_NODE *
 *   plan(in): The (already optimized) select statement to generate code for
 *   xasl(in): The XASL block for the root of the plan
 *
 * Note: Create an XASL tree from the QO_PLAN tree associated with
 *	'select'.  In essence, this takes the entity specs from the
 *	from part of 'select' and produces a collection of access
 *	specs that will do the right thing.  It also distributes the
 *	predicates in the where part across those access specs.  The
 *	caller shouldn't have to do anything for the from part or the
 *	where part, but it must still take care of all of the other
 *	grunge, such as setting up the code for the select list
 *	expressions, etc.
 */
xasl_node *
qo_to_xasl (QO_PLAN * plan, xasl_node * xasl)
{
  QO_ENV *env;
  XASL_NODE *lastxasl;

  if (plan && xasl && (env = (plan->info)->env))
    {
      xasl = gen_outer (env, plan, &EMPTY_SET, NULL, NULL, xasl);

      lastxasl = xasl;
      while (lastxasl)
	{
	  /*
	   * Don't consider only scan pointers here; it's quite
	   * possible that the correlated subqueries might depend on
	   * values retrieved by a fetch proc that lives on an fptr.
	   */
	  if (lastxasl->scan_ptr)
	    {
	      lastxasl = lastxasl->scan_ptr;
	    }
	  else if (lastxasl->fptr_list)
	    {
	      lastxasl = lastxasl->fptr_list;
	    }
	  else
	    {
	      break;
	    }
	}
      (void) pt_set_dptr (env->parser, env->pt_tree->info.query.q.select.list, lastxasl, MATCH_ALL);

      xasl = preserve_info (env, plan, xasl);
    }
  else
    {
      xasl = NULL;
    }

  if (xasl == NULL)
    {
      int level;

      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_FAILED_ASSERTION, 1, "xasl != NULL");

      qo_get_optimization_param (&level, QO_PARAM_LEVEL);
      if (PLAN_DUMP_ENABLED (level))
	{
	  fprintf (stderr, "*** XASL generation failed ***\n");
	}
#if defined(CUBRID_DEBUG)
      else
	{
	  fprintf (stderr, "*** XASL generation failed ***\n");
	  fprintf (stderr, "*** %s ***\n", er_msg ());
	}
#endif /* CUBRID_DEBUG */
    }

  return xasl;
}

/*
 * qo_plan_iscan_sort_list () - get after index scan PT_SORT_SPEC list
 *   return: SORT_LIST *
 *   plan(in): QO_PLAN
 */
PT_NODE *
qo_plan_iscan_sort_list (QO_PLAN * plan)
{
  return plan->iscan_sort_list;
}

/*
 * qo_plan_skip_orderby () - check the plan info for order by
 *   return: true/false
 *   plan(in): QO_PLAN
 */
bool
qo_plan_skip_orderby (QO_PLAN * plan)
{
  return ((plan->plan_type == QO_PLANTYPE_SORT
	   && (plan->plan_un.sort.sort_type == SORT_DISTINCT
	       || plan->plan_un.sort.sort_type == SORT_ORDERBY)) ? false : true);
}

/*
 * qo_plan_skip_groupby () - check the plan info for order by
 *   return: true/false
 *   plan(in): QO_PLAN
 */
bool
qo_plan_skip_groupby (QO_PLAN * plan)
{
  return (plan->plan_type == QO_PLANTYPE_SCAN && plan->plan_un.scan.index
	  && plan->plan_un.scan.index->head->groupby_skip) ? true : false;
}

/*
 * qo_is_index_covering_scan () - check the plan info for covering index scan
 *   return: true/false
 *   plan(in): QO_PLAN
 */
bool
qo_is_index_covering_scan (QO_PLAN * plan)
{
  assert (plan != NULL);
  assert (plan->info != NULL);
  assert (plan->info->env != NULL);

  if (qo_is_interesting_order_scan (plan))
    {
      if (plan->plan_un.scan.index_cover == true)
	{
	  assert (plan->plan_un.scan.index->head);
	  assert (plan->plan_un.scan.index->head->cover_segments);

	  assert (!qo_is_prefix_index (plan->plan_un.scan.index->head));

	  return true;
	}
    }

  return false;
}

/*
 * qo_is_index_iss_scan () - check the plan info for index skip scan
 *   return: true/false
 *   plan(in): QO_PLAN
 */
bool
qo_is_index_iss_scan (QO_PLAN * plan)
{
  assert (plan != NULL);
  assert (plan->info != NULL);
  assert (plan->info->env != NULL);

  if (qo_is_interesting_order_scan (plan))
    {
      if (plan->plan_un.scan.index_iss == true)
	{
	  assert (plan->plan_un.scan.index->head);
	  assert (plan->plan_un.scan.index->head->is_iss_candidate);

	  assert (QO_ENTRY_MULTI_COL (plan->plan_un.scan.index->head));
	  assert (plan->plan_un.scan.index_loose == false);
	  assert (plan->multi_range_opt_use != PLAN_MULTI_RANGE_OPT_USE);
	  assert (!qo_is_filter_index (plan->plan_un.scan.index->head));

	  return true;
	}
    }

  return false;
}

/*
 * qo_is_index_loose_scan () - check the plan info for loose index scan
 *   return: true/false
 *   plan(in): QO_PLAN
 */
bool
qo_is_index_loose_scan (QO_PLAN * plan)
{
  assert (plan != NULL);
  assert (plan->info != NULL);
  assert (plan->info->env != NULL);

  if (qo_is_iscan (plan))
    {
      if (plan->plan_un.scan.index_loose == true)
	{
	  assert (plan->plan_un.scan.index->head);
	  assert (plan->plan_un.scan.index->head->ils_prefix_len > 0);

	  assert (QO_ENTRY_MULTI_COL (plan->plan_un.scan.index->head));
	  assert (plan->plan_un.scan.index_cover == true);

	  assert (!qo_is_prefix_index (plan->plan_un.scan.index->head));
	  assert (plan->plan_un.scan.index_iss == false);
	  assert (plan->multi_range_opt_use != PLAN_MULTI_RANGE_OPT_USE);

	  return true;
	}
    }

  return false;
}

/*
 * qo_is_index_mro_scan () - check the plan info for multi range opt scan
 *   return: true/false
 *   plan(in): QO_PLAN
 */
bool
qo_is_index_mro_scan (QO_PLAN * plan)
{
  assert (plan != NULL);
  assert (plan->info != NULL);
  assert (plan->info->env != NULL);

  if (qo_is_interesting_order_scan (plan))
    {
      if (plan->multi_range_opt_use == PLAN_MULTI_RANGE_OPT_USE)
	{
	  assert (plan->plan_un.scan.index->head);

	  assert (QO_ENTRY_MULTI_COL (plan->plan_un.scan.index->head));
	  assert (plan->plan_un.scan.index_iss == false);
	  assert (plan->plan_un.scan.index_loose == false);
	  assert (!qo_is_filter_index (plan->plan_un.scan.index->head));

	  return true;
	}
    }

  return false;
}

/*
 * qo_plan_multi_range_opt () - check the plan info for multi range opt
 *   return: true/false
 *   plan(in): QO_PLAN
 */
bool
qo_plan_multi_range_opt (QO_PLAN * plan)
{
  assert (plan != NULL);
  assert (plan->info != NULL);
  assert (plan->info->env != NULL);

  if (plan != NULL && plan->multi_range_opt_use == PLAN_MULTI_RANGE_OPT_USE)
    {
#if !defined(NDEBUG)
      if (qo_is_interesting_order_scan (plan))
	{
	  assert (plan->plan_un.scan.index->head);

	  assert (QO_ENTRY_MULTI_COL (plan->plan_un.scan.index->head));
	  assert (plan->plan_un.scan.index_iss == false);
	  assert (plan->plan_un.scan.index_loose == false);
	  assert (!qo_is_filter_index (plan->plan_un.scan.index->head));
	}
#endif

      return true;
    }

  return false;
}

/******************************************************************************
 *  qo_xasl support functions
 *****************************************************************************/

/*
 * qo_get_xasl_index_info () -
 *   return: QO_XASL_INDEX_INFO structure which contains index information
 *	     needed for XASL generation
 *   env(in): The environment
 *   plan(in): The plan from which to initialize the scan proc
 *
 * Note: The term expression array <term_exprs> is returned in index
 *     definition order. i.e. For multi-column indexes, you can create
 *     a sequence key from the expression array in the order that they
 *     are returned.
 */
static QO_XASL_INDEX_INFO *
qo_get_xasl_index_info (QO_ENV * env, QO_PLAN * plan)
{
  int nterms, nsegs, nkfterms, multi_term_num;;
  QO_NODE_INDEX_ENTRY *ni_entryp;
  QO_INDEX_ENTRY *index_entryp;
  QO_XASL_INDEX_INFO *index_infop;
  int t, i, j, pos;
  BITSET_ITERATOR iter;
  QO_TERM *termp;
  BITSET multi_col_segs, multi_col_range_segs, index_segs;

  if (!qo_is_interesting_order_scan (plan))
    {
      return NULL;		/* give up */
    }

  assert (plan->plan_un.scan.index != NULL);

  bitset_init (&multi_col_segs, env);
  bitset_init (&multi_col_range_segs, env);
  bitset_init (&index_segs, env);

  /* if no index scan terms, no index scan */
  nterms = bitset_cardinality (&(plan->plan_un.scan.terms));
  nkfterms = bitset_cardinality (&(plan->plan_un.scan.kf_terms));

  /* pointer to QO_NODE_INDEX_ENTRY structure in QO_PLAN */
  ni_entryp = plan->plan_un.scan.index;
  /* pointer to linked list of index node, 'head' field(QO_INDEX_ENTRY structure) of QO_NODE_INDEX_ENTRY */
  index_entryp = (ni_entryp)->head;
  /* number of indexed segments */
  nsegs = index_entryp->nsegs;	/* nsegs == nterms ? */

  /* support also full range indexes */
  if (nterms <= 0 && nkfterms <= 0 && bitset_cardinality (&(plan->sarged_terms)) == 0)
    {
      if (qo_is_filter_index (index_entryp) || qo_is_index_loose_scan (plan) || qo_is_iscan_from_groupby (plan)
	  || qo_is_iscan_from_orderby (plan)
	  || PT_SPEC_SPECIAL_INDEX_SCAN (QO_NODE_ENTITY_SPEC (plan->plan_un.scan.node)))
	{
	  /* Do not return if: 1. filtered index. 2. skip group by or skip order by 3. loose scan. 4. scan for b-tree
	   * node info or key info. */
	  ;
	}
      else
	{			/* is invalid case */
	  assert (false);
	  return NULL;		/* give up */
	}
    }

  /* allocate QO_XASL_INDEX_INFO structure */
  index_infop = (QO_XASL_INDEX_INFO *) malloc (sizeof (QO_XASL_INDEX_INFO));
  if (index_infop == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (QO_XASL_INDEX_INFO));
      goto error;
    }

  if (qo_is_index_iss_scan (plan))
    {
      assert (index_entryp->is_iss_candidate);

      /* allow space for the first element (NULL actually), for instance in term_exprs */
      nterms++;
    }

  /* check multi column index term */
  multi_term_num =
    qo_get_multi_col_range_segs (env, plan, index_entryp, &multi_col_segs, &multi_col_range_segs, &index_segs);
  if (multi_term_num != -1)
    {
      /* case of term having multiple columns */
      termp = QO_ENV_TERM (env, multi_term_num);
      if (!bitset_subset (&index_segs, &multi_col_segs))
	{
	  /* need to add sarg term (data filter) */
	  index_infop->need_copy_multi_range_term = multi_term_num;
	  index_infop->need_copy_to_sarg_term = true;
	}
      else if (!bitset_subset (&multi_col_range_segs, &multi_col_segs)
	       || QO_TERM_IS_FLAGED (termp, QO_TERM_MULTI_COLL_CONST))
	{
	  /* need to add key filter term (index key filter) */
	  index_infop->need_copy_multi_range_term = multi_term_num;
	  index_infop->need_copy_to_sarg_term = false;
	}
      else
	{
	  /* don't need to force-copy any filter */
	  index_infop->need_copy_multi_range_term = -1;
	  index_infop->need_copy_to_sarg_term = false;
	}
      /* add multi column term's segs ex) index(a,b,c), (a,b) in .. and c = 1 : nterms = 2 + 2 -1 */
      nterms = nterms + bitset_cardinality (&multi_col_range_segs) - 1;
    }
  else
    {
      index_infop->need_copy_multi_range_term = -1;
      index_infop->need_copy_to_sarg_term = false;
    }

  if (nterms == 0)
    {
      index_infop->nterms = 0;
      index_infop->term_exprs = NULL;
      index_infop->multi_col_pos = NULL;
      index_infop->ni_entry = ni_entryp;
      return index_infop;
    }

  index_infop->nterms = nterms;
  index_infop->term_exprs = (PT_NODE **) malloc (nterms * sizeof (PT_NODE *));
  index_infop->multi_col_pos = (int *) malloc (nterms * sizeof (int));
  if (index_infop->term_exprs == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, nterms * sizeof (PT_NODE *));
      goto error;
    }

  if (qo_is_index_iss_scan (plan))
    {
      assert (index_entryp->is_iss_candidate);

      index_infop->term_exprs[0] = NULL;
      index_infop->multi_col_pos[0] = -1;
    }

  index_infop->ni_entry = ni_entryp;

  /* Make 'term_expr[]' array from the given index terms in order of the 'seg_idx[]' array of the associated index. */

  /* for all index scan terms */
  for (t = bitset_iterate (&(plan->plan_un.scan.terms), &iter); t != -1; t = bitset_next_member (&iter))
    {
      /* pointer to QO_TERM denoted by number 't' */
      termp = QO_ENV_TERM (env, t);

      if (!QO_TERM_IS_FLAGED (termp, QO_TERM_MULTI_COLL_PRED))
	{
	  /* Find the matching segment in the segment index array to determine the array position to store the expression.
	   * We're using the 'index_seg[]' array of the term to find its segment index */
	  pos = -1;
	  for (i = 0; i < termp->can_use_index && pos == -1; i++)
	    {
	      for (j = 0; j < nsegs; j++)
		{
		  if (i >= (int) (sizeof (termp->index_seg) / sizeof (termp->index_seg[0])))
		    {
		      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_FAILED_ASSERTION, 1, "false");
		      goto error;
		    }

		  if ((index_entryp->seg_idxs[j]) == QO_SEG_IDX (termp->index_seg[i]))
		    {
		      pos = j;
		      break;
		    }
		}
	    }

	  /* always, pos != -1 and 0 <= pos < nterms */
	  assert (pos >= 0 && pos < nterms);
	  if (pos < 0 || pos >= nterms)
	    {
	      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_FAILED_ASSERTION, 1, "pos >= 0 and pos < nterms");
	      goto error;
	    }

	  /* if the index is Index Skip Scan, the first column should have never been found in a term */
	  assert (!qo_is_index_iss_scan (plan) || pos != 0);
	  index_infop->term_exprs[pos] = QO_TERM_PT_EXPR (termp);
	  index_infop->multi_col_pos[pos] = -1;
	}
      else
	{
	  /* case of multi column term */
	  /* not need can_use_index's iteration because multi col term having other node's segments isn't indexable */
	  /* ex) (a.col1,a.col2) in ((a.col1,b.col2)) is not indexable */
	  for (i = 0; i < termp->multi_col_cnt; i++)
	    {
	      pos = -1;
	      for (j = 0; j < nsegs; j++)
		{
		  if ((index_entryp->seg_idxs[j]) == (termp->multi_col_segs[i]))
		    {
		      pos = j;
		      break;
		    }
		}
	      /* if the index is Index Skip Scan, the first column should have never been found in a term */
	      assert (!qo_is_index_iss_scan (plan) || pos != 0);

	      if (pos != -1 && BITSET_MEMBER (multi_col_range_segs, index_entryp->seg_idxs[pos]))
		{
		  /* always, pos != -1 and 0 <= pos < nterms */
		  assert (pos >= 0 && pos < nterms);
		  if (pos < 0 || pos >= nterms)
		    {
		      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_FAILED_ASSERTION, 1, "pos >= 0 and pos < nterms");
		      goto error;
		    }

		  index_infop->term_exprs[pos] = QO_TERM_PT_EXPR (termp);
		  index_infop->multi_col_pos[pos] = i;
		}
	    }
	}
    }

  bitset_delset (&multi_col_segs);
  bitset_delset (&multi_col_range_segs);
  bitset_delset (&index_segs);
  /* return QO_XASL_INDEX_INFO */
  return index_infop;

error:
  /* malloc error */
  qo_free_xasl_index_info (env, index_infop);
  return NULL;
}

/*
 * qo_free_xasl_index_info () -
 *   return: void
 *   env(in): The environment
 *   info(in): Information structure (QO_XASL_INDEX_INFO)
 *
 * Note: Free the memory occupied by the QO_XASL_INDEX_INFO
 */
static void
qo_free_xasl_index_info (QO_ENV * env, QO_XASL_INDEX_INFO * info)
{
  if (info)
    {
      if (info->term_exprs)
	{
	  free_and_init (info->term_exprs);
	}
      /* DEALLOCATE (env, info->term_exprs); */
      if (info->multi_col_pos)
	{
	  free_and_init (info->multi_col_pos);
	}
      /* DEALLOCATE (env, info->multi_col_pos); */
      free_and_init (info);
      /* DEALLOCATE(env, info); */
    }
}

/*
 * qo_xasl_get_num_terms () - Return the number of terms in the array
 *   return: int
 *   info(in): Pointer to info structure
 */
int
qo_xasl_get_num_terms (QO_XASL_INDEX_INFO * info)
{
  return info->nterms;
}

/*
 * qo_xasl_get_terms () - Return a point to the NULL terminated list
 *			  of TERM expressions
 *   return: PT_NODE **
 *   info(in): Pointer to info structure
 */
PT_NODE **
qo_xasl_get_terms (QO_XASL_INDEX_INFO * info)
{
  return info->term_exprs;
}

/*
 * qo_add_hq_iterations_access_spec () - adds hierarchical query iterations
 *	access spec on single table
 *   return:
 *   plan(in):
 *   xasl(in):
 */
xasl_node *
qo_add_hq_iterations_access_spec (QO_PLAN * plan, xasl_node * xasl)
{
  PARSER_CONTEXT *parser;
  QO_ENV *env;
  PT_NODE *class_spec;
  PT_NODE *key_pred = NULL;
  PT_NODE *access_pred = NULL;
  PT_NODE *if_pred = NULL;
  QO_XASL_INDEX_INFO *index_info;

  if (!plan)
    {
      return NULL;
    }

  if (plan->plan_type == QO_PLANTYPE_SORT)
    {
      QO_PLAN *subplan = plan->plan_un.sort.subplan;
      while (subplan && subplan->plan_type == QO_PLANTYPE_SORT)
	{
	  subplan = subplan->plan_un.sort.subplan;
	}
      if (subplan && subplan->plan_type == QO_PLANTYPE_SCAN)
	{
	  plan = subplan;
	}
      else
	{
	  return NULL;
	}
    }
  else if (plan->plan_type != QO_PLANTYPE_SCAN)
    {
      return NULL;
    }

  class_spec = plan->plan_un.scan.node->entity_spec;
  env = plan->info->env;

  parser = QO_ENV_PARSER (env);

  index_info = qo_get_xasl_index_info (env, plan);
  make_pred_from_plan (env, plan, &key_pred, &access_pred, index_info, NULL);

  xasl->spec_list = pt_to_spec_list (parser, class_spec, key_pred, access_pred, plan, index_info, NULL, NULL);

  if_pred = make_if_pred_from_plan (env, plan);
  if (if_pred)
    {
      xasl->if_pred = pt_to_pred_expr (parser, if_pred);
    }

  /* free pointer node list */
  parser_free_tree (parser, key_pred);
  parser_free_tree (parser, access_pred);
  parser_free_tree (parser, if_pred);

  qo_free_xasl_index_info (env, index_info);

  if (xasl->spec_list == NULL)
    {
      return NULL;
    }

  return xasl;
}

/*
 * regu_ptr_list_create () - creates and initializes a REGU_PTR_LIST - a linked
 *                           list of POINTERS to REGU VARIBLES
 *   return: a new node, or NULL on error
 */
static REGU_PTR_LIST
regu_ptr_list_create ()
{
  REGU_PTR_LIST p;

  p = (REGU_PTR_LIST) db_private_alloc (NULL, sizeof (struct regu_ptr_list_node));
  if (!p)
    {
      return NULL;
    }

  p->next = NULL;
  p->var_p = NULL;

  return p;
}

/*
 * regu_ptr_list_free () - iterates over a linked list of regu var pointers
 *                         and frees each node (the containing node, NOT the
 *                         actual REGU_VARIABLE).
 *   list(in): the REGU_PTR_LIST. It can be NULL.
 *   return:
 */
static void
regu_ptr_list_free (REGU_PTR_LIST list)
{
  REGU_PTR_LIST next;

  while (list)
    {
      next = list->next;
      db_private_free (NULL, list);
      list = next;
    }
}

/*
 * regu_ptr_list_add_regu () - adds a pointer to a regu variable to the list,
 *                             initializing the list if required.
 *
 * regu(in): REGU_VAR ptr to add to the head of the list
 * list(in): the initial list. It can be NULL - in this case it will be initialised.
 * return: the list with the added element, or NULL on error.
 */
static REGU_PTR_LIST
regu_ptr_list_add_regu (REGU_VARIABLE * var_p, REGU_PTR_LIST list)
{
  REGU_PTR_LIST node;

  if (!var_p)
    {
      return list;
    }

  node = (REGU_PTR_LIST) db_private_alloc (NULL, sizeof (struct regu_ptr_list_node));
  if (!node)
    {
      regu_ptr_list_free (list);
      return NULL;
    }

  node->next = list;
  node->var_p = var_p;

  return node;
}

static bool
qo_validate_regu_var_for_limit (REGU_VARIABLE * var_p)
{
  if (var_p == NULL)
    {
      return true;
    }

  if (var_p->type == TYPE_DBVAL)
    {
      return true;
    }
  else if (var_p->type == TYPE_POS_VALUE)
    {
      return true;
    }
  else if (var_p->type == TYPE_INARITH && var_p->value.arithptr)
    {
      struct arith_list_node *aptr = var_p->value.arithptr;

      return (qo_validate_regu_var_for_limit (aptr->leftptr) && qo_validate_regu_var_for_limit (aptr->rightptr)
	      && qo_validate_regu_var_for_limit (aptr->thirdptr));
    }

  return false;
}

/*
 * qo_get_limit_from_eval_term () - get lower and upper limits from an
 *                                  eval term involving instnum
 *   return:   true on success.
 *   parser(in):
 *   pred(in): the predicate expression.
 *   lower(out): lower limit node
 *   upper(out): upper limit node
 *
 *   Note: handles terms of the form:
 *         instnum rel_op value/hostvar
 *         value/hostvar rel_op instnum
 */
static bool
qo_get_limit_from_eval_term (PARSER_CONTEXT * parser, PRED_EXPR * pred, REGU_PTR_LIST * lower, REGU_PTR_LIST * upper)
{
  REGU_VARIABLE *lhs, *rhs;
  REL_OP op;
  PT_NODE *node_one = NULL;
  TP_DOMAIN *dom_bigint = tp_domain_resolve_default (DB_TYPE_BIGINT);
  REGU_VARIABLE *regu_one, *regu_low;

  if (pred == NULL || pred->type != T_EVAL_TERM || pred->pe.m_eval_term.et_type != T_COMP_EVAL_TERM)
    {
      return false;
    }

  lhs = pred->pe.m_eval_term.et.et_comp.lhs;
  rhs = pred->pe.m_eval_term.et.et_comp.rhs;
  op = pred->pe.m_eval_term.et.et_comp.rel_op;

  if (!lhs || !rhs)
    {
      return false;
    }
  if (op != R_LE && op != R_LT && op != R_GE && op != R_GT && op != R_EQ)
    {
      return false;
    }

  /* the TYPE_CONSTANT regu variable must be instnum, otherwise it would not be accepted by the parser */

  /* switch the ops to transform into instnum rel_op value/hostvar */
  if (rhs->type == TYPE_CONSTANT)
    {
      rhs = pred->pe.m_eval_term.et.et_comp.lhs;
      lhs = pred->pe.m_eval_term.et.et_comp.rhs;
      switch (op)
	{
	case R_LE:
	  op = R_GE;
	  break;
	case R_LT:
	  op = R_GT;
	  break;
	case R_GE:
	  op = R_LE;
	  break;
	case R_GT:
	  op = R_LT;
	  break;
	default:
	  break;
	}
    }

  if (lhs->type != TYPE_CONSTANT || !qo_validate_regu_var_for_limit (rhs))
    {
      return false;
    }

  /* Bring every accepted relation to a form similar to lower < rownum <= upper. */
  switch (op)
    {
    case R_EQ:
      /* decrement node value for lower, but remember current value for upper */
      node_one = pt_make_integer_value (parser, 1);
      if (!node_one)
	{
	  return false;
	}

      if (!(regu_one = pt_to_regu_variable (parser, node_one, UNBOX_AS_VALUE))
	  || !(regu_low = pt_make_regu_arith (rhs, regu_one, NULL, T_SUB, dom_bigint)))
	{
	  parser_free_node (parser, node_one);
	  return false;
	}
      regu_low->domain = dom_bigint;

      *lower = regu_ptr_list_add_regu (regu_low, *lower);
      *upper = regu_ptr_list_add_regu (rhs, *upper);
      break;

    case R_LE:
      *upper = regu_ptr_list_add_regu (rhs, *upper);
      break;

    case R_LT:
      /* decrement node value */
      node_one = pt_make_integer_value (parser, 1);
      if (!node_one)
	{
	  return false;
	}

      if (!(regu_one = pt_to_regu_variable (parser, node_one, UNBOX_AS_VALUE))
	  || !(regu_low = pt_make_regu_arith (rhs, regu_one, NULL, T_SUB, dom_bigint)))
	{
	  parser_free_node (parser, node_one);
	  return false;
	}
      regu_low->domain = dom_bigint;

      *upper = regu_ptr_list_add_regu (regu_low, *upper);
      break;

    case R_GE:
      /* decrement node value for lower */
      node_one = pt_make_integer_value (parser, 1);
      if (!node_one)
	{
	  return false;
	}

      if (!(regu_one = pt_to_regu_variable (parser, node_one, UNBOX_AS_VALUE))
	  || !(regu_low = pt_make_regu_arith (rhs, regu_one, NULL, T_SUB, dom_bigint)))
	{
	  parser_free_node (parser, node_one);
	  return false;
	}
      regu_low->domain = dom_bigint;

      *lower = regu_ptr_list_add_regu (regu_low, *lower);
      break;

    case R_GT:
      /* leave node value as it is */
      *lower = regu_ptr_list_add_regu (rhs, *lower);
      break;

    default:
      break;
    }

  if (node_one)
    {
      parser_free_node (parser, node_one);
    }

  return true;
}

/*
 * qo_get_limit_from_instnum_pred () - get lower and upper limits from an
 *                                     instnum predicate
 *   return: true if successful
 *   parser(in):
 *   pred(in): the predicate expression
 *   lower(out): lower limit node
 *   upper(out): upper limit node
 */
static bool
qo_get_limit_from_instnum_pred (PARSER_CONTEXT * parser, PRED_EXPR * pred, REGU_PTR_LIST * lower, REGU_PTR_LIST * upper)
{
  if (pred == NULL)
    {
      return false;
    }

  if (pred->type == T_PRED && pred->pe.m_pred.bool_op == B_AND)
    {
      return (qo_get_limit_from_instnum_pred (parser, pred->pe.m_pred.lhs, lower, upper)
	      && qo_get_limit_from_instnum_pred (parser, pred->pe.m_pred.rhs, lower, upper));
    }

  if (pred->type == T_EVAL_TERM)
    {
      return qo_get_limit_from_eval_term (parser, pred, lower, upper);
    }

  return false;
}

/*
 * qo_get_key_limit_from_instnum () - creates a keylimit node from an
 *                                    instnum predicate, if possible.
 *   return:     a new node, or NULL if a keylimit node cannot be
 *               initialized (not necessarily an error)
 *   parser(in): the parser context
 *   plan (in):  the query plan
 *   xasl (in):  the full XASL node
 */
QO_LIMIT_INFO *
qo_get_key_limit_from_instnum (PARSER_CONTEXT * parser, QO_PLAN * plan, xasl_node * xasl)
{
  REGU_PTR_LIST lower = NULL, upper = NULL, ptr = NULL;
  QO_LIMIT_INFO *limit_infop = NULL;
  TP_DOMAIN *dom_bigint = tp_domain_resolve_default (DB_TYPE_BIGINT);

  if (xasl == NULL || xasl->instnum_pred == NULL || plan == NULL)
    {
      return NULL;
    }

  switch (plan->plan_type)
    {
    case QO_PLANTYPE_SCAN:
      if (!qo_is_interesting_order_scan (plan))
	{
	  return NULL;
	}
      break;

    case QO_PLANTYPE_JOIN:
      /* only allow inner joins */
      if (plan->plan_un.join.join_type != JOIN_INNER)
	{
	  return NULL;
	}
      break;

    default:
      return NULL;
    }

  /* get lower and upper limits */
  if (!qo_get_limit_from_instnum_pred (parser, xasl->instnum_pred, &lower, &upper))
    {
      return NULL;
    }
  /* not having upper limit is not helpful */
  if (upper == NULL)
    {
      regu_ptr_list_free (lower);
      return NULL;
    }

  limit_infop = (QO_LIMIT_INFO *) db_private_alloc (NULL, sizeof (QO_LIMIT_INFO));
  if (limit_infop == NULL)
    {
      regu_ptr_list_free (lower);
      regu_ptr_list_free (upper);
      return NULL;
    }

  limit_infop->lower = limit_infop->upper = NULL;


  /* upper limit */
  limit_infop->upper = upper->var_p;
  ptr = upper->next;
  while (ptr)
    {
      limit_infop->upper = pt_make_regu_arith (limit_infop->upper, ptr->var_p, NULL, T_LEAST, dom_bigint);
      if (!limit_infop->upper)
	{
	  regu_ptr_list_free (upper);
	  regu_ptr_list_free (lower);
	  db_private_free (NULL, limit_infop);
	  return NULL;
	}

      limit_infop->upper->domain = dom_bigint;
      ptr = ptr->next;
    }
  regu_ptr_list_free (upper);

  if (lower)
    {
      limit_infop->lower = lower->var_p;
      ptr = lower->next;
      while (ptr)
	{
	  limit_infop->lower = pt_make_regu_arith (limit_infop->lower, ptr->var_p, NULL, T_GREATEST, dom_bigint);
	  if (!limit_infop->lower)
	    {
	      regu_ptr_list_free (lower);
	      db_private_free (NULL, limit_infop);
	      return NULL;
	    }

	  limit_infop->lower->domain = dom_bigint;
	  ptr = ptr->next;
	}
      regu_ptr_list_free (lower);
    }

  return limit_infop;
}

/*
 * qo_get_key_limit_from_ordbynum () - creates a keylimit node from an
 *                                     orderby_num predicate, if possible.
 *   return:     a new node, or NULL if a keylimit node cannot be
 *               initialized (not necessarily an error)
 *   parser(in): the parser context
 *   plan (in):  the query plan
 *   xasl (in):  the full XASL node
 *   ignore_lower (in): generate key limit even if ordbynum has a lower limit
 */
QO_LIMIT_INFO *
qo_get_key_limit_from_ordbynum (PARSER_CONTEXT * parser, QO_PLAN * plan, xasl_node * xasl, bool ignore_lower)
{
  REGU_PTR_LIST lower = NULL, upper = NULL, ptr = NULL;
  QO_LIMIT_INFO *limit_infop;
  TP_DOMAIN *dom_bigint = tp_domain_resolve_default (DB_TYPE_BIGINT);

  if (xasl == NULL || xasl->ordbynum_pred == NULL)
    {
      return NULL;
    }

  /* get lower and upper limits */
  if (!qo_get_limit_from_instnum_pred (parser, xasl->ordbynum_pred, &lower, &upper))
    {
      return NULL;
    }
  /* having a lower limit, or not having upper limit is not helpful */
  if (upper == NULL || (lower != NULL && !ignore_lower))
    {
      regu_ptr_list_free (lower);
      regu_ptr_list_free (upper);
      return NULL;
    }

  limit_infop = (QO_LIMIT_INFO *) db_private_alloc (NULL, sizeof (QO_LIMIT_INFO));
  if (!limit_infop)
    {
      regu_ptr_list_free (lower);
      regu_ptr_list_free (upper);
      return NULL;
    }

  limit_infop->lower = limit_infop->upper = NULL;

  /* upper limit */
  limit_infop->upper = upper->var_p;
  ptr = upper->next;
  while (ptr)
    {
      limit_infop->upper = pt_make_regu_arith (limit_infop->upper, ptr->var_p, NULL, T_LEAST, dom_bigint);
      if (!limit_infop->upper)
	{
	  regu_ptr_list_free (upper);
	  regu_ptr_list_free (lower);
	  db_private_free (NULL, limit_infop);
	  return NULL;
	}

      limit_infop->upper->domain = dom_bigint;
      ptr = ptr->next;
    }

  regu_ptr_list_free (upper);
  regu_ptr_list_free (lower);

  return limit_infop;
}

/*
 * qo_check_iscan_for_multi_range_opt () - check that current index scan can
 *					   use multi range key-limit
 *					   optimization
 *
 * return	    : true/false
 * plan (in)	    : index scan plan
 *
 * Note: The optimization requires a series of conditions to be met:
 *	 For single table case:
 *	 - valid order by for condition
 *	    -> the upper limit has to be less than multi_range_opt_limit
 *	       system parameter
 *	    -> the expression should look like: LIMIT n,
						ORDERBY_NUM </<= n,
 *						n > ORDERBY_NUM
 *	       or AND operator on ORDERBY_NUM valid expressions.
 *	    -> lower limit is not allowed
 *	 - index scan with no data filter
 *	 - order by columns should occupy consecutive positions in index and
 *	   the ordering should match all columns (or all should be reversed)
 *	 - index access keys have multiple key ranges, but only one range
 *	   column
 *	 - The generic case that uses multi range optimization is the
 *	   following:
 *	    SELECT ... FROM table
 *		WHERE col_1 = ? AND col_2 = ? AND ...
 *		    AND col_(j) IN (?,?,...)
 *		    AND col_(j+1) = ? AND ... AND col_(p-1) = ?
 *		ORDER BY col_(p) [ASC/DESC] [, col_(p2) [ASC/DESC], ...]
 *		FOR ordbynum_pred / LIMIT n
 */
bool
qo_check_iscan_for_multi_range_opt (QO_PLAN * plan)
{
  QO_ENV *env = NULL;
  bool can_optimize = 0;
  PT_NODE *col = NULL, *query = NULL, *select_list = NULL;
  int error = NO_ERROR;
  bool multi_range_optimize = false;
  int first_col_idx_pos = -1, i = 0;
  PT_NODE *orderby_nodes = NULL, *point = NULL, *name = NULL;
  PARSER_CONTEXT *parser = NULL;
  bool reverse = false;
  PT_NODE *order_by = NULL;
  PT_MISC_TYPE all_distinct;


  if (plan == NULL)
    {
      return false;
    }

  if (!qo_is_iscan (plan))
    {
      return false;
    }

  if (QO_NODE_IS_CLASS_HIERARCHY (plan->plan_un.scan.node))
    {
      /* for now, multi range optimization can only work on one index, therefore class hierarchies are not accepted */
      return false;
    }

  assert (plan->info->env && plan->info->env->parser);
  env = plan->info->env;
  parser = env->parser;

  query = QO_ENV_PT_TREE (env);
  if (!PT_IS_SELECT (query))
    {
      return false;
    }
  if ((query->info.query.q.select.hint & PT_HINT_NO_MULTI_RANGE_OPT) != 0)
    {
      /* NO_MULTI_RANGE_OPT was hinted */
      return false;
    }
  if (pt_has_aggregate (parser, query))
    {
      // CBRD-22696
      //
      // MRO XASL is flawed when query has aggregate. MRO depends on order by list, which is generated based on
      // query's select list. Then it uses pointers from XASL outptr_list, which is normally also generated based
      // on query's select list.
      //
      // In case of group by and/or aggregates, XASL outptr_list is used as input list for group by/aggregate. As a
      // consequence, MRO is broken; sometimes it will fallback to normal index scan (because pointers do not match),
      // but sometimes a safe-guard is hit (when order by position number is not found in outptr_list).
      //
      // until a proper fix is found, MRO is disabled for aggregate queries.
      return false;
    }
  all_distinct = query->info.query.all_distinct;
  order_by = query->info.query.order_by;

  if (order_by == NULL || all_distinct == PT_DISTINCT)
    {
      return false;
    }

  if (query->info.query.orderby_for == NULL)
    {
      return false;
    }

  select_list = pt_get_select_list (parser, query);
  assert (select_list != NULL);

  /* create a list of pointers to the names referenced in order by */
  for (col = order_by; col != NULL; col = col->next)
    {
      i = col->info.sort_spec.pos_descr.pos_no;
      if (i <= 0)
	{
	  goto exit;
	}
      name = select_list;
      while (--i > 0)
	{
	  name = name->next;
	  if (name == NULL)
	    {
	      goto exit;
	    }
	}
      if (!PT_IS_NAME_NODE (name))
	{
	  goto exit;
	}
      point = pt_point (parser, name);
      orderby_nodes = parser_append_node (point, orderby_nodes);
    }

  /* verify that the index used for scan contains all order by columns in the right order and with the right ordering
   * (or reversed ordering) */
  error =
    qo_check_plan_index_for_multi_range_opt (orderby_nodes, order_by, plan, &can_optimize, &first_col_idx_pos,
					     &reverse);
  if (error != NO_ERROR || !can_optimize)
    {
      goto exit;
    }

  /* check scan terms and key filter terms to verify that multi range optimization is applicable */
  error = qo_check_terms_for_multiple_range_opt (plan, first_col_idx_pos, &can_optimize);
  if (error != NO_ERROR || !can_optimize)
    {
      goto exit;
    }

  /* make sure that correlated subqueries may not affect the results obtained with multiple range optimization */
  can_optimize = qo_check_subqueries_for_multi_range_opt (plan, first_col_idx_pos);
  if (!can_optimize)
    {
      goto exit;
    }

  /* check a valid range */
  if (!pt_check_ordby_num_for_multi_range_opt (parser, query, &env->multi_range_opt_candidate, NULL))
    {
      goto exit;
    }

  /* all conditions were met, so multi range optimization can be applied */
  multi_range_optimize = true;

  plan->plan_un.scan.index->head->use_descending = reverse;
  plan->plan_un.scan.index->head->first_sort_column = first_col_idx_pos;
  plan->use_iscan_descending = reverse;

exit:
  if (orderby_nodes != NULL)
    {
      parser_free_tree (parser, orderby_nodes);
    }
  return multi_range_optimize;
}

/*
 * qo_check_plan_index_for_multi_range_opt () - check if the index of index
 *						scan plan can use multi range
 *						key-limit optimization
 *
 * return		   : error code
 * orderby_nodes (in)	   : list of pointer to the names of order by columns
 * orderby_sort_list (in)  : list of PT_SORT_SPEC for the order by columns
 * plan (in)		   : current plan to check
 * is_valid (out)	   : true/false
 * first_col_idx_pos (out) : position in index for the first sort column
 * reverse (out)	   : true if the index has to be reversed in order to
 *			     use multiple range optimization, false otherwise
 *
 * NOTE: In order to be compatible with multi range optimization, the index of
 *	 index scan plan must meet the next conditions:
 *	 - index should cover all order by columns and their positions in
 *	   index should be consecutive (in the same order as in the order by
 *	   clause).
 *	 - column ordering should either match in both order by clause and
 *	   index, or should all be reversed.
 */
static int
qo_check_plan_index_for_multi_range_opt (PT_NODE * orderby_nodes, PT_NODE * orderby_sort_list, QO_PLAN * plan,
					 bool * is_valid, int *first_col_idx_pos, bool * reverse)
{
  int i = 0, seg_idx = -1;
  QO_INDEX_ENTRY *index_entryp = NULL;
  QO_ENV *env = NULL;
  PT_NODE *orderby_node = NULL;
  PT_NODE *orderby_sort_column = NULL;
  PT_NODE *n = NULL, *save_next = NULL;
  TP_DOMAIN *key_type = NULL;

  assert (plan != NULL && orderby_nodes != NULL && is_valid != NULL && first_col_idx_pos != NULL && reverse != NULL);

  *is_valid = false;
  *reverse = false;
  *first_col_idx_pos = -1;

  if (!qo_is_iscan (plan))
    {
      return NO_ERROR;
    }
  if (plan->plan_un.scan.index == NULL || plan->plan_un.scan.index->head == NULL)
    {
      return NO_ERROR;
    }
  if (plan->info->env == NULL)
    {
      return NO_ERROR;
    }

  env = plan->info->env;
  index_entryp = plan->plan_un.scan.index->head;

  key_type = index_entryp->key_type;
  if (key_type == NULL || (TP_DOMAIN_TYPE (key_type) != DB_TYPE_MIDXKEY))
    {
      return NO_ERROR;
    }
  key_type = key_type->setdomain;

  /* look for the first order by column */
  orderby_node = orderby_nodes;
  CAST_POINTER_TO_NODE (orderby_node);
  assert (orderby_node->node_type == PT_NAME);

  orderby_sort_column = orderby_sort_list;
  assert (orderby_sort_column->node_type == PT_SORT_SPEC);

  for (i = 0; i < index_entryp->nsegs; i++, key_type = key_type->next)
    {
      if (!key_type)
	{
	  return NO_ERROR;
	}
      seg_idx = index_entryp->seg_idxs[i];
      if (seg_idx < 0)
	{
	  continue;
	}
      n = QO_SEG_PT_NODE (QO_ENV_SEG (env, seg_idx));
      CAST_POINTER_TO_NODE (n);
      if (n && n->node_type == PT_NAME && pt_name_equal (env->parser, orderby_node, n))
	{
	  if (i == 0)
	    {
	      /* MRO cannot apply */
	      return NO_ERROR;
	    }
	  if (key_type->is_desc != (orderby_sort_column->info.sort_spec.asc_or_desc == PT_DESC))
	    {
	      /* order in index does not match order in order by clause, but reversed index may work */
	      *reverse = true;
	    }
	  break;
	}
    }
  if (i == index_entryp->nsegs)
    {
      /* order by node was not found */
      return NO_ERROR;
    }

  if (index_entryp->first_sort_column != -1)
    {
      assert (index_entryp->first_sort_column == i);
    }

  *first_col_idx_pos = i;
  /* order by node was found, check that all nodes occupy consecutive positions in index */
  for (orderby_node = orderby_nodes->next, i = i + 1, key_type = key_type->next, orderby_sort_column =
       orderby_sort_list->next; orderby_node != NULL && orderby_sort_column != NULL && i < index_entryp->nsegs;
       i++, orderby_sort_column = orderby_sort_column->next, key_type = key_type->next)
    {
      if (key_type == NULL)
	{
	  return NO_ERROR;
	}
      seg_idx = index_entryp->seg_idxs[i];
      if (seg_idx < 0)
	{
	  return NO_ERROR;
	}

      save_next = orderby_node->next;
      CAST_POINTER_TO_NODE (orderby_node);
      n = QO_SEG_PT_NODE (QO_ENV_SEG (env, seg_idx));
      CAST_POINTER_TO_NODE (n);
      if (n == NULL || n->node_type != PT_NAME || !pt_name_equal (env->parser, orderby_node, n))
	{
	  /* order by columns do not match the columns in index */
	  return NO_ERROR;
	}
      if ((*reverse ? !key_type->is_desc : key_type->is_desc) !=
	  (orderby_sort_column->info.sort_spec.asc_or_desc == PT_DESC))
	{
	  /* normally, key_type->is_desc must match sort order == PT_DESC, if reversed, !key_type->is_desc must match
	   * instead. */
	  return NO_ERROR;
	}
      orderby_node = save_next;
    }
  if (orderby_node != NULL)
    {
      /* there are order by columns left */
      return NO_ERROR;
    }
  /* all segments in index matched columns in order by list */
  *is_valid = true;

  return NO_ERROR;
}

/*
 * qo_check_plan_for_multiple_ranges_limit_opt () - check the plan to find out
 *                                                  if multiple ranges key
 *						    limit optimization can be
 *						    used
 *
 * return	     : error_code
 * parser(in)	     : parser context
 * plan(in)       : plan to check
 * idx_col(in)       : first sort column position in index
 * can_optimize(out) : true/false if optimization is allowed/not allowed
 *
 *   Note: Check that all columns that come before the first sort column (on
 *	   the left side of the sort column) in the index are either in an
 *	   equality term, or in a key list term. Only one column should be in
 *	   a key list term.
 *	   Also check all terms in the environment to see if there is any
 *	   data filter
 */
static int
qo_check_terms_for_multiple_range_opt (QO_PLAN * plan, int first_sort_col_idx, bool * can_optimize)
{
  int t, i, j, pos, s, seg_idx;
  BITSET_ITERATOR iter_t, iter_s;
  QO_TERM *termp = NULL;
  QO_ENV *env = NULL;
  QO_INDEX_ENTRY *index_entryp = NULL;
  QO_NODE *node_of_plan = NULL;
  int *used_cols = NULL;
  int kl_terms = 0;

  assert (can_optimize != NULL);

  *can_optimize = false;

  if (plan == NULL || plan->info == NULL || plan->plan_un.scan.index == NULL || !qo_is_interesting_order_scan (plan))
    {
      return NO_ERROR;
    }

  env = plan->info->env;
  if (env == NULL)
    {
      return NO_ERROR;
    }

  index_entryp = plan->plan_un.scan.index->head;
  if (index_entryp == NULL)
    {
      return NO_ERROR;
    }

  node_of_plan = plan->plan_un.scan.node;
  if (node_of_plan == NULL)
    {
      return NO_ERROR;
    }

  /* index columns that are used in terms */
  used_cols = (int *) malloc (first_sort_col_idx * sizeof (int));
  if (!used_cols)
    {
      return ER_FAILED;
    }
  for (i = 0; i < first_sort_col_idx; i++)
    {
      used_cols[i] = 0;
    }

  /* check all index scan terms */
  for (t = bitset_iterate (&(plan->plan_un.scan.terms), &iter_t); t != -1; t = bitset_next_member (&iter_t))
    {
      termp = QO_ENV_TERM (env, t);
      assert (!QO_TERM_IS_FLAGED (termp, QO_TERM_NON_IDX_SARG_COLL));

      pos = -1;
      for (i = 0; i < termp->can_use_index && i < 2 && pos == -1; i++)
	{
	  for (j = 0; j < index_entryp->nsegs; j++)
	    {
	      if ((index_entryp->seg_idxs[j] == QO_SEG_IDX (termp->index_seg[i])))
		{
		  pos = j;
		  break;
		}
	    }
	}
      if (pos == -1)
	{
	  free_and_init (used_cols);
	  return NO_ERROR;
	}

      if (pos < first_sort_col_idx)
	{
	  used_cols[pos]++;
	  /* only helpful if term is equality or key list */
	  switch (QO_TERM_PT_EXPR (termp)->info.expr.op)
	    {
	    case PT_EQ:
	      break;
	    case PT_IS_IN:
	    case PT_EQ_SOME:
	      kl_terms++;
	      break;
	    case PT_RANGE:
	      {
		PT_NODE *between_and;

		between_and = QO_TERM_PT_EXPR (termp)->info.expr.arg2;
		if (PT_IS_EXPR_NODE (between_and) && between_and->info.expr.op == PT_BETWEEN_EQ_NA)
		  {
		    kl_terms++;
		  }
		else
		  {
		    free_and_init (used_cols);
		    return NO_ERROR;
		  }
	      }
	      break;
	    default:
	      free_and_init (used_cols);
	      return NO_ERROR;
	    }
	}
    }

  /* check key list terms */
  if (kl_terms > 1)
    {
      free_and_init (used_cols);
      return NO_ERROR;
    }

  /* check all key filter terms */
  for (t = bitset_iterate (&(plan->plan_un.scan.kf_terms), &iter_t); t != -1; t = bitset_next_member (&iter_t))
    {
      termp = QO_ENV_TERM (env, t);
      assert (!QO_TERM_IS_FLAGED (termp, QO_TERM_NON_IDX_SARG_COLL));

      pos = -1;
      for (i = 0; i < termp->can_use_index && i < 2 && pos == -1; i++)
	{
	  for (j = 0; j < index_entryp->nsegs; j++)
	    {
	      if ((index_entryp->seg_idxs[j] == QO_SEG_IDX (termp->index_seg[i])))
		{
		  pos = j;
		  break;
		}
	    }
	}
      if (pos == -1)
	{
	  if (termp->can_use_index == 0)
	    {
	      continue;
	    }
	  free_and_init (used_cols);
	  return NO_ERROR;
	}

      if (pos < first_sort_col_idx)
	{
	  /* for key filter terms we are only interested if it is an eq term */
	  if (QO_TERM_PT_EXPR (termp)->info.expr.op == PT_EQ)
	    {
	      used_cols[pos]++;
	    }
	}
    }

  /* check used columns */
  for (i = 0; i < first_sort_col_idx; i++)
    {
      if (used_cols[i] == 0)
	{
	  free_and_init (used_cols);
	  return NO_ERROR;
	}
    }
  free_and_init (used_cols);

  /* check all segments in all terms in environment for data filter */
  for (t = 0; t < env->nterms; t++)
    {
      termp = QO_ENV_TERM (env, t);
      if (QO_TERM_IS_FLAGED (termp, QO_TERM_NON_IDX_SARG_COLL))
	{
	  return NO_ERROR;
	}

      for (s = bitset_iterate (&(termp->segments), &iter_s); s != -1; s = bitset_next_member (&iter_s))
	{
	  bool found = false;
	  if (QO_SEG_HEAD (QO_ENV_SEG (env, s)) != node_of_plan)
	    {
	      continue;
	    }
	  seg_idx = s;

	  for (i = 0; i < index_entryp->nsegs; i++)
	    {
	      if (seg_idx == index_entryp->seg_idxs[i])
		{
		  found = true;
		  break;
		}
	    }
	  if (!found)
	    {
	      /* data filter */
	      return NO_ERROR;
	    }
	}
    }

  *can_optimize = true;
  return NO_ERROR;
}

/*
 * qo_check_subqueries_for_multi_range_opt () - check that there are not
 *						subqueries that may invalidate
 *						multiple range optimization
 *
 * return		 : false if invalidated, true otherwise
 * plan (in)		 : the plan that refers to the table that has the
 *			   order by columns
 * sort_col_idx_pos (in) : position in index for the first sort column
 *
 * NOTE:  If there are terms containing correlated subqueries, and if they
 *	  refer to the node of the sort plan, then the affected segments must
 *	  appear in index before the first sort column (and the segment must
 *	  not belong to the range term).
 */
static bool
qo_check_subqueries_for_multi_range_opt (QO_PLAN * plan, int sort_col_idx_pos)
{
  QO_ENV *env = NULL;
  QO_SUBQUERY *subq = NULL;
  int i, s, t, seg_idx, i_seg_idx, ts;
  QO_NODE *node_of_plan = NULL;
  BITSET_ITERATOR iter_t, iter_ts;
  QO_TERM *term = NULL;

  assert (plan != NULL && plan->info->env != NULL && plan->plan_type == QO_PLANTYPE_SCAN
	  && plan->plan_un.scan.index != NULL);

  env = plan->info->env;
  node_of_plan = plan->plan_un.scan.node;

  /* for each sub-query */
  for (s = 0; s < env->nsubqueries; s++)
    {
      subq = QO_ENV_SUBQUERY (env, s);

      /* for each term this sub-query belongs to */
      for (t = bitset_iterate (&(subq->terms), &iter_t); t != -1; t = bitset_next_member (&iter_t))
	{
	  term = QO_ENV_TERM (env, t);

	  for (ts = bitset_iterate (&(term->segments), &iter_ts); ts != -1; ts = bitset_next_member (&iter_ts))
	    {
	      bool found = false;
	      if (QO_SEG_HEAD (QO_ENV_SEG (env, t)) != node_of_plan)
		{
		  continue;
		}
	      seg_idx = ts;
	      /* try to find the segment in index */
	      for (i = 0; i < sort_col_idx_pos; i++)
		{
		  i_seg_idx = plan->plan_un.scan.index->head->seg_idxs[i];
		  if (i_seg_idx == seg_idx)
		    {
		      if (qo_check_seg_belongs_to_range_term (plan, env, seg_idx))
			{
			  return false;
			}
		      break;
		    }
		}
	      if (!found)
		{
		  /* the segment was not found before the first sort column */
		  return false;
		}
	    }
	}
    }
  return true;
}

/*
 * qo_check_seg_belongs_to_range_term () - checks the segment if it is a range
 *					   term
 *
 * return	: true or false
 * subplan (in) : the subplan possibly containing the RANGE expression
 * env (in)	: optimizer environment
 * seg_idx (in) : index of the segment that needs checking
 *
 * NOTE:  Returns true if the specified subplan contains a term that
 *        references the given segment in a RANGE expression
 *        (t.i in (1,2,3) would be an example).
 *        Used in keylimit for multiple key ranges in joins optimization.
 *	  Scan terms, key filter terms and also sarged terms must all be
 *	  checked to cover all cases.
 */
static bool
qo_check_seg_belongs_to_range_term (QO_PLAN * subplan, QO_ENV * env, int seg_idx)
{
  int t, u;
  BITSET_ITERATOR iter, iter_s;

  assert (subplan->plan_type == QO_PLANTYPE_SCAN);
  if (subplan->plan_type != QO_PLANTYPE_SCAN)
    {
      return false;
    }

  for (t = bitset_iterate (&(subplan->plan_un.scan.terms), &iter); t != -1; t = bitset_next_member (&iter))
    {
      QO_TERM *termp = QO_ENV_TERM (env, t);
      BITSET *segs = &(QO_TERM_SEGS (termp));
      if (!segs)
	{
	  continue;
	}
      for (u = bitset_iterate (segs, &iter_s); u != -1; u = bitset_next_member (&iter_s))
	{
	  if (u == seg_idx)
	    {
	      PT_NODE *node = QO_TERM_PT_EXPR (termp);
	      if (!node)
		{
		  continue;
		}

	      switch (node->info.expr.op)
		{
		case PT_IS_IN:
		case PT_EQ_SOME:
		case PT_RANGE:
		  return true;
		default:
		  continue;
		}
	    }
	}
    }
  for (t = bitset_iterate (&(subplan->plan_un.scan.kf_terms), &iter); t != -1; t = bitset_next_member (&iter))
    {
      QO_TERM *termp = QO_ENV_TERM (env, t);
      BITSET *segs = &(QO_TERM_SEGS (termp));
      if (!segs)
	{
	  continue;
	}
      for (u = bitset_iterate (segs, &iter_s); u != -1; u = bitset_next_member (&iter_s))
	{
	  if (u == seg_idx)
	    {
	      PT_NODE *node = QO_TERM_PT_EXPR (termp);
	      if (!node)
		{
		  continue;
		}

	      switch (node->info.expr.op)
		{
		case PT_IS_IN:
		case PT_EQ_SOME:
		case PT_RANGE:
		  return true;
		default:
		  continue;
		}
	    }
	}
    }
  for (t = bitset_iterate (&(subplan->sarged_terms), &iter); t != -1; t = bitset_next_member (&iter))
    {
      QO_TERM *termp = QO_ENV_TERM (env, t);
      BITSET *segs = &(QO_TERM_SEGS (termp));
      if (!segs)
	{
	  continue;
	}
      for (u = bitset_iterate (segs, &iter_s); u != -1; u = bitset_next_member (&iter_s))
	{
	  if (u == seg_idx)
	    {
	      PT_NODE *node = QO_TERM_PT_EXPR (termp);
	      if (!node)
		{
		  continue;
		}

	      switch (node->info.expr.op)
		{
		case PT_IS_IN:
		case PT_EQ_SOME:
		case PT_RANGE:
		  return true;
		default:
		  continue;
		}
	    }
	}
    }
  return false;
}

/*
 * qo_check_join_for_multi_range_opt () - check if join plan can make use of
 *					  multi range key-limit optimization
 *
 * return    : true/false
 * plan (in) : join plan
 *
 * NOTE:  The current join plan has to meet a series of conditions in order
 *	  to use the multi range optimization:
 *	  - Has at least an index scan subplan that can make use of multi
 *	  range optimization (as if there would be no joins)
 *	  - The sort plan (that uses multi range optimization) edges:
 *	    - Segments used to join other "outer-more" scans must also belong
 *	      to index (no data filter).
 *	    - Segments use to join other "inner-more" scans must belong to
 *	      index (no data filter), they must be positioned before the first
 *	      sorting column, and they cannot be in a range term (in order
 *	      to avoid filtering the results obtained after top n sorting).
 */
bool
qo_check_join_for_multi_range_opt (QO_PLAN * plan)
{
  QO_PLAN *sort_plan = NULL;
  int error = NO_ERROR;
  bool can_optimize = true;

  /* verify that this is a valid join for multi range optimization */
  if (plan == NULL || plan->plan_type != QO_PLANTYPE_JOIN || plan->plan_un.join.join_type != JOIN_INNER
      || plan->plan_un.join.join_method == QO_JOINMETHOD_MERGE_JOIN)
    {
      return false;
    }

  assert (plan->info->env && plan->info->env->pt_tree);
  if (!PT_IS_SELECT (plan->info->env->pt_tree))
    {
      return false;
    }
  if (((QO_ENV_PT_TREE (plan->info->env))->info.query.q.select.hint & PT_HINT_NO_MULTI_RANGE_OPT) != 0)
    {
      /* NO_MULTI_RANGE_OPT was hinted */
      return false;
    }

  /* first must find an index scan subplan that can apply multi range optimization */
  error = qo_find_subplan_using_multi_range_opt (plan, &sort_plan, NULL);
  if (error != NO_ERROR || sort_plan == NULL)
    {
      /* error finding subplan or no subplan was found */
      return false;
    }

  /* check all join conditions */
  error = qo_check_subplans_for_multi_range_opt (NULL, plan, sort_plan, &can_optimize, NULL);
  if (error != NO_ERROR || !can_optimize)
    {
      return false;
    }

  /* all conditions are met, multi range optimization may be used */
  return true;
}

/*
 * qo_check_subplans_for_multi_range_opt () - verify that join conditions do
 *					      not invalidate the multi range
 *					      optimization
 *
 * return		 : error code
 * parent (in)		 : join node that contains sub-plans
 * plan (in)		 : current plan to verify
 * sortplan (in)	 : the plan that refers to the order by table
 * is_valid (out)	 : is_valid is true if optimization can be applied
 *			   otherwise it is set on false
 * sort_col_idx_pos (in) : position in index for the first sort column
 * seen (in/out)	 : flag to remember that the sort plan was passed.
 *			   all scan plans that are met after this flag was set
 *			   are potential suspect scans ("to the right" of the
 *			   order by table
 *
 * NOTE: 1. *seen should be false when the function is called for root plan or
 *	    it should be left as NULL.
 *	 2. checks all sub-plans at the right of sort plan in the join chain.
 *	    the sub-plans in the left can invalidate the optimization only if
 *	    the join term acts as data filter, which was already checked at a
 *	    previous step (see qo_check_terms_for_multiple_range_opt).
 *	 Check the comment on qo_check_join_for_multi_range_opt for more
 *	 details.
 */
static int
qo_check_subplans_for_multi_range_opt (QO_PLAN * parent, QO_PLAN * plan, QO_PLAN * sortplan, bool * is_valid,
				       bool * seen)
{
  int error = NO_ERROR;
  bool dummy = false;

  if (seen == NULL)
    {
      seen = &dummy;
    }

  if (plan->plan_type == QO_PLANTYPE_SCAN)
    {
      if (*seen)
	{
	  if (parent == NULL)
	    {
	      *is_valid = false;
	      goto exit;
	    }
	  *is_valid = qo_check_subplan_join_cond_for_multi_range_opt (parent, plan, sortplan);
	  if (*is_valid == true)
	    {
	      *is_valid = qo_check_parent_eq_class_for_multi_range_opt (parent, plan, sortplan);
	    }
	  return NO_ERROR;
	}
      if (plan == sortplan)
	{
	  *seen = true;
	}
      *is_valid = true;
      return NO_ERROR;
    }
  else if (plan->plan_type == QO_PLANTYPE_JOIN)
    {
      if (qo_plan_multi_range_opt (plan))
	{
	  /* plan->multi_range_opt_use == PLAN_MULTI_RANGE_OPT_USE */
	  /* already checked and the plan can use multi range opt */
	  *is_valid = true;
	  /* sort plan is somewhere in the subtree of this plan */
	  *seen = true;
	  return NO_ERROR;
	}
      else if (plan->multi_range_opt_use == PLAN_MULTI_RANGE_OPT_CANNOT_USE)
	{
	  /* already checked and the plan cannot use multi range opt */
	  *is_valid = false;
	  return NO_ERROR;
	}
      /* this must be the first time current plan is checked for multi range optimization */
      error = qo_check_subplans_for_multi_range_opt (plan, plan->plan_un.join.outer, sortplan, is_valid, seen);
      if (error != NO_ERROR || !*is_valid)
	{
	  /* mark the plan for future checks */
	  plan->multi_range_opt_use = PLAN_MULTI_RANGE_OPT_CANNOT_USE;
	  return error;
	}
      error = qo_check_subplans_for_multi_range_opt (plan, plan->plan_un.join.inner, sortplan, is_valid, seen);
      if (error != NO_ERROR || !*is_valid)
	{
	  /* mark the plan for future checks */
	  plan->multi_range_opt_use = PLAN_MULTI_RANGE_OPT_CANNOT_USE;
	  return error;
	}
      return NO_ERROR;
    }

  /* a case we have not foreseen? Be conservative. */
  *is_valid = false;

exit:
  return error;
}

/*
 * qo_check_subplan_join_cond_for_multi_range_opt () - validate a given
 *						       subplan for multi range
 *						       key-limit optimization.
 *
 * return         : true if valid, false otherwise
 * parent (in)    : join plan that contains current subplan
 * subplan (in)   : the subplan that is verified at current step
 * sort_plan (in) : the subplan that refers to the table that has the order by
 *		    columns
 * is_outer (in)  : The position of sort plan relative to sub-plan in the join
 *		    chain. If true, sort plan must be position to the left in
 *		    the chain (is "outer-more"), otherwise it must be to the
 *		    right ("inner-more").
 *
 * NOTE:  The function checks the join conditions between sub-plan and
 *	  sort-plan. It is supposed that sort-plan is outer and sub-plan is
 *	  inner in this join.
 */
static bool
qo_check_subplan_join_cond_for_multi_range_opt (QO_PLAN * parent, QO_PLAN * subplan, QO_PLAN * sort_plan)
{
  QO_ENV *env = NULL;
  QO_NODE *node_of_sort_table = NULL, *node_of_subplan = NULL;
  BITSET_ITERATOR iter_t, iter_n, iter_segs;
  QO_TERM *jt = NULL;
  QO_NODE *jn = NULL;
  int t, n, seg_idx, k, k_seg_idx;
  QO_NODE *seg_node = NULL;
  bool is_jterm_relevant;

  assert (parent != NULL && parent->plan_type == QO_PLANTYPE_JOIN);
  assert (sort_plan != NULL && sort_plan->plan_un.scan.node != NULL && sort_plan->plan_un.scan.node->info != NULL);
  if (sort_plan->plan_un.scan.node->info->n <= 0 || sort_plan->plan_un.scan.index == NULL
      || sort_plan->plan_un.scan.index->n <= 0)
    {
      return false;
    }

  env = sort_plan->info->env;
  node_of_sort_table = sort_plan->plan_un.scan.node;
  node_of_subplan = subplan->plan_un.scan.node;

  assert (node_of_sort_table != NULL && node_of_subplan != NULL);

  /*
   * Scan all the parent's join terms: jt.
   *   If jt is a valid join-term (is a join between sub-plan and sort plan),
   *   the segment that belong to the sort plan must be positioned in index
   *   before the first sort column and it must not be in a range term.
   */
  for (t = bitset_iterate (&(parent->plan_un.join.join_terms), &iter_t); t != -1; t = bitset_next_member (&iter_t))
    {
      jt = QO_ENV_TERM (env, t);
      assert (jt != NULL);

      is_jterm_relevant = false;
      for (n = bitset_iterate (&(jt->nodes), &iter_n); n != -1; n = bitset_next_member (&iter_n))
	{
	  jn = QO_ENV_NODE (env, n);

	  assert (jn != NULL);

	  if (jn == node_of_subplan)
	    {
	      is_jterm_relevant = true;
	      break;
	    }
	}

      if (!is_jterm_relevant)
	{
	  continue;
	}

      for (n = bitset_iterate (&(jt->nodes), &iter_n); n != -1; n = bitset_next_member (&iter_n))
	{
	  jn = QO_ENV_NODE (env, n);

	  assert (jn != NULL);

	  if (jn != node_of_sort_table)
	    {
	      continue;
	    }

	  /* there is a join term t that references the nodes used in sub-plan and sort plan. */
	  for (seg_idx = bitset_iterate (&(jt->segments), &iter_segs); seg_idx != -1;
	       seg_idx = bitset_next_member (&iter_segs))
	    {
	      bool found = false;
	      seg_node = QO_SEG_HEAD (QO_ENV_SEG (env, seg_idx));
	      if (seg_node != node_of_sort_table)
		{
		  continue;
		}
	      /* seg node refer to the order by table */
	      for (k = 0; k < sort_plan->plan_un.scan.index->head->first_sort_column; k++)
		{
		  k_seg_idx = sort_plan->plan_un.scan.index->head->seg_idxs[k];
		  if (k_seg_idx == seg_idx)
		    {
		      /* seg_idx was found before the first sort column */
		      if (qo_check_seg_belongs_to_range_term (sort_plan, env, seg_idx))
			{
			  return false;
			}
		      found = true;
		      break;
		    }
		}
	      if (!found)
		{
		  /* seg_idx was not found before the first sort column */
		  return false;
		}
	    }
	}
    }

  return true;
}

/*
 * qo_check_parent_eq_class_for_multi_range_opt () - validate a given subplan
 *						     for multi range key-limit
 *						     optimization.
 *
 * return	  : true if valid, false otherwise
 * parent (in)    : join plan that contains current subplan
 * subplan (in)   : the subplan that is verified at current step
 * sort_plan (in) : the subplan that refers to the table that has the order by
 *		    columns
 *
 * NOTE: Same as qo_check_subplan_join_cond_for_multi_range_opt, except that
 *	 it checks the EQCLASSES.
 */
static bool
qo_check_parent_eq_class_for_multi_range_opt (QO_PLAN * parent, QO_PLAN * subplan, QO_PLAN * sort_plan)
{
  QO_ENV *env = NULL;
  QO_NODE *node_of_sort_table = NULL, *node_of_crt_table = NULL, *node = NULL;
  int eq_idx, t, k, seg_idx, k_seg_idx;
  QO_EQCLASS *eq = NULL;
  bool is_eqclass_relevant = false;
  BITSET_ITERATOR iter_t;

  assert (parent != NULL && parent->plan_type == QO_PLANTYPE_JOIN);
  assert (subplan != NULL && subplan->plan_type == QO_PLANTYPE_SCAN);
  assert (sort_plan != NULL && sort_plan->plan_un.scan.node != NULL && sort_plan->plan_un.scan.node->info != NULL);
  if (sort_plan->plan_un.scan.node->info->n <= 0 || sort_plan->plan_un.scan.index == NULL
      || sort_plan->plan_un.scan.index->n <= 0)
    {
      return false;
    }

  env = parent->info->env;
  node_of_sort_table = sort_plan->plan_un.scan.node;
  node_of_crt_table = subplan->plan_un.scan.node;

  for (eq_idx = 0; eq_idx < env->neqclasses; eq_idx++)
    {
      eq = QO_ENV_EQCLASS (env, eq_idx);
      is_eqclass_relevant = false;

      for (t = bitset_iterate (&QO_EQCLASS_SEGS (eq), &iter_t); t != -1; t = bitset_next_member (&iter_t))
	{
	  node = QO_SEG_HEAD (QO_ENV_SEG (env, t));
	  if (node == node_of_crt_table)
	    {
	      is_eqclass_relevant = true;
	      break;
	    }
	}

      if (!is_eqclass_relevant)
	{
	  continue;
	}

      /* here: we have an equivalence class that has a segment belonging to our current class. Must see if it also has
       * segments belonging to the sort table: iterate again over it. */
      for (t = bitset_iterate (&QO_EQCLASS_SEGS (eq), &iter_t); t != -1; t = bitset_next_member (&iter_t))
	{
	  bool found = false;
	  node = QO_SEG_HEAD (QO_ENV_SEG (env, t));
	  if (node != node_of_sort_table)
	    {
	      continue;
	    }

	  seg_idx = t;
	  /* try to find the segment in the index that is used when scanning the order by table - take that info from
	   * the subplan (sortplan) rather than from the node info, because the node info lists all the indexes, not
	   * only those that are used at this particular scan. */
	  for (k = 0; k < sort_plan->plan_un.scan.index->head->first_sort_column; k++)
	    {
	      k_seg_idx = sort_plan->plan_un.scan.index->head->seg_idxs[k];
	      if (k_seg_idx == seg_idx)
		{
		  /* we found the segment before the first sort column */
		  if (qo_check_seg_belongs_to_range_term (sort_plan, env, seg_idx))
		    {
		      return false;
		    }
		  found = true;
		  break;
		}
	    }
	  if (!found)
	    {
	      /* seg_idx was not found before the first sort column */
	      return false;
	    }
	}
    }

  return true;
}

/*
 * qo_find_subplan_using_multi_range_opt () - finds an index scan plan that
 *					      may use multi range key-limit
 *					      optimization.
 *
 * return	  : error code
 * plan (in)	  : current node in plan tree
 * result (out)   : plan with multi range optimization
 * join_idx (out) : the position of the optimized plan in join chain
 *
 * NOTE : Leave result or join_idx NULL if they are not what you are looking
 *	  for.
 */
int
qo_find_subplan_using_multi_range_opt (QO_PLAN * plan, QO_PLAN ** result, int *join_idx)
{
  int error = NO_ERROR;

  if (result != NULL)
    {
      *result = NULL;
    }

  if (plan == NULL)
    {
      return NO_ERROR;
    }

  if (plan->plan_type == QO_PLANTYPE_JOIN && plan->plan_un.join.join_type == JOIN_INNER)
    {
      if (join_idx != NULL)
	{
	  *join_idx++;
	}
      error = qo_find_subplan_using_multi_range_opt (plan->plan_un.join.outer, result, join_idx);
      if (error != NO_ERROR || (result != NULL && *result != NULL))
	{
	  return NO_ERROR;
	}
      if (join_idx != NULL)
	{
	  *join_idx++;
	}
      return qo_find_subplan_using_multi_range_opt (plan->plan_un.join.inner, result, join_idx);
    }
  else if (qo_is_interesting_order_scan (plan))
    {
      if (qo_plan_multi_range_opt (plan))
	{
	  if (result != NULL)
	    {
	      *result = plan;
	    }
	}
      return NO_ERROR;
    }
  return NO_ERROR;
}

/*
 * make_sort_limit_proc () - make sort limit xasl node
 * return : xasl proc on success, NULL on error
 * env (in) : optimizer environment
 * plan (in) : query plan
 * namelist (in) : list of segments referenced by nodes in the plan
 * xasl (in) : top xasl
 */
static XASL_NODE *
make_sort_limit_proc (QO_ENV * env, QO_PLAN * plan, PT_NODE * namelist, XASL_NODE * xasl)
{
  PARSER_CONTEXT *parser;
  XASL_NODE *listfile = NULL;
  PT_NODE *new_order_by = NULL, *node_list = NULL;
  PT_NODE *order_by, *statement;

  parser = QO_ENV_PARSER (env);
  statement = QO_ENV_PT_TREE (env);
  order_by = statement->info.query.order_by;

  if (xasl->ordbynum_val == NULL)
    {
      /* If orderbynum_val is NULL, we're probably somewhere in a subplan and orderbynum_val is set for the upper XASL
       * level. Try to find the ORDERBY_NUM node and use the node->etc pointer which is set to the orderby_num val */
      if (statement->info.query.orderby_for == NULL)
	{
	  /* we should not create a sort_limit proc without an orderby_for predicate. */
	  assert_release (false);
	  listfile = NULL;
	  goto cleanup;
	}

      parser_walk_tree (parser, statement->info.query.orderby_for, pt_get_numbering_node_etc, &xasl->ordbynum_val, NULL,
			NULL);
      if (xasl->ordbynum_val == NULL)
	{
	  assert_release (false);
	  listfile = NULL;
	  goto cleanup;
	}
    }
  /* make o copy of the namelist to extend it with expressions from the ORDER BY clause. The extended list will be used
   * to generate the internal listfile scan but will not be used for the actual XASL node. */
  node_list = parser_copy_tree_list (parser, namelist);
  if (node_list == NULL)
    {
      listfile = NULL;
      goto cleanup;
    }

  /* set new SORT_SPEC list based on the position of items in the node_list */
  new_order_by = pt_set_orderby_for_sort_limit_plan (parser, statement, node_list);
  if (new_order_by == NULL)
    {
      listfile = NULL;
      goto cleanup;
    }

  statement->info.query.order_by = new_order_by;

  listfile = make_buildlist_proc (env, node_list);
  listfile = gen_outer (env, plan->plan_un.sort.subplan, &EMPTY_SET, NULL, NULL, listfile);
  listfile = add_sort_spec (env, listfile, plan, xasl->ordbynum_val, false);

cleanup:
  if (node_list != NULL)
    {
      parser_free_tree (parser, node_list);
    }
  if (new_order_by != NULL)
    {
      parser_free_tree (parser, new_order_by);
    }

  statement->info.query.order_by = order_by;

  return listfile;
}

/*
 * qo_get_orderby_num_upper_bound_node () - get the node which represents the
 *					    upper bound predicate of an
 *					    orderby_num predicate
 * return : node or NULL
 * parser (in)		: parser context
 * orderby_for (in)	: orderby_for predicate list
 * is_new_node (in/out) : if a new node was created, free_node is set to true
 *			  and caller must free the returned node
 *
 * Note: A NULL return indicates that this function either found no upper
 * bound or that it found several predicates which specify an upper bound
 * for the ORDERBY_NUM predicate.
 */
static PT_NODE *
qo_get_orderby_num_upper_bound_node (PARSER_CONTEXT * parser, PT_NODE * orderby_for, bool * is_new_node)
{
  PT_NODE *left = NULL, *right = NULL;
  PT_NODE *save_next;
  PT_OP_TYPE op;
  bool free_left = false, free_right = false;
  *is_new_node = false;

  if (orderby_for == NULL || !PT_IS_EXPR_NODE (orderby_for) || orderby_for->or_next != NULL)
    {
      /* orderby_for must be an expression containing only AND predicates */
      assert (false);
      return NULL;
    }

  /* Ranges for ORDERBY_NUM predicates have already been merged (see qo_reduce_order_by). If the code below finds more
   * than one upper bound, this is an error. */
  if (orderby_for->next != NULL)
    {
      save_next = orderby_for->next;
      orderby_for->next = NULL;

      right = save_next;
      left = orderby_for;

      left = qo_get_orderby_num_upper_bound_node (parser, left, &free_left);
      right = qo_get_orderby_num_upper_bound_node (parser, right, &free_right);

      orderby_for->next = save_next;

      if (left != NULL)
	{
	  if (right != NULL)
	    {
	      /* There should be exactly one upper bound */
	      if (free_left)
		{
		  parser_free_tree (parser, left);
		}
	      if (free_right)
		{
		  parser_free_tree (parser, right);
		}
	      return NULL;
	    }
	  *is_new_node = free_left;
	  return left;
	}
      else
	{
	  /* If right is NULL, the orderby_num pred is invalid and we messed something up somewhere. If it is not NULL,
	   * this is the node we are looking for. */
	  *is_new_node = free_right;
	  return right;
	}
    }

  op = orderby_for->info.expr.op;
  /* look for orderby_num < argument */
  if (PT_IS_EXPR_NODE (orderby_for->info.expr.arg1) && orderby_for->info.expr.arg1->info.expr.op == PT_ORDERBY_NUM)
    {
      left = orderby_for->info.expr.arg1;
      right = orderby_for->info.expr.arg2;
    }
  else
    {
      left = orderby_for->info.expr.arg2;
      right = orderby_for->info.expr.arg1;
      if (!PT_IS_EXPR_NODE (left) || left->info.expr.op != PT_ORDERBY_NUM)
	{
	  /* could not find ORDERBY_NUM argument */
	  return NULL;
	}

      /* Verify operator. If LE, LT then reverse it. */
      switch (op)
	{
	case PT_LE:
	  op = PT_GE;
	  break;
	case PT_LT:
	  op = PT_GT;
	  break;
	case PT_GE:
	  op = PT_LE;
	  break;
	case PT_GT:
	  op = PT_LT;
	  break;
	default:
	  break;
	}
    }

  if (op == PT_LE || op == PT_LT)
    {
      return orderby_for;
    }

  if (op == PT_BETWEEN)
    {
      /* construct new predicate for ORDERBY_NUM from BETWEEN expr. */
      PT_NODE *new_node;

      if (!PT_IS_EXPR_NODE (right) || right->info.expr.op != PT_BETWEEN_AND)
	{
	  return NULL;
	}

      new_node = parser_new_node (parser, PT_EXPR);
      if (new_node == NULL)
	{
	  return NULL;
	}
      new_node->info.expr.op = PT_LE;

      new_node->info.expr.arg1 = parser_copy_tree (parser, left);
      if (new_node->info.expr.arg1 == NULL)
	{
	  parser_free_tree (parser, new_node);
	  return NULL;
	}

      new_node->info.expr.arg2 = parser_copy_tree (parser, right->info.expr.arg2);
      if (new_node->info.expr.arg2 == NULL)
	{
	  parser_free_tree (parser, new_node);
	  return NULL;
	}

      *is_new_node = true;
      return new_node;
    }

  /* Any other comparison operator is unusable */
  return NULL;
}

/*
 * qo_get_multi_col_range_segs () -
 *   return:
 *   env(in): The optimizer environment
 *   plan(in): Query plan
 *   qo_index_infop(in):
 *   multi_col_segs(out): (a,b) in ... a,b's segment number bit
 *   multi_col_range_segs(out): range segments in multiple column term
 *
 * Note: return multiple column term's number
 *       output are multi column term's segments and range key filter segments and index col segments
 */
static int
qo_get_multi_col_range_segs (QO_ENV * env, QO_PLAN * plan, QO_INDEX_ENTRY * index_entryp,
			     BITSET * multi_col_segs, BITSET * multi_col_range_segs, BITSET * index_segs)
{
  BITSET_ITERATOR iter;
  QO_TERM *termp = NULL;
  int multi_term = -1;

  /* find term having multiple columns ex) (col1,col2) in ... */
  for (multi_term = bitset_iterate (&(plan->plan_un.scan.terms), &iter); multi_term != -1;
       multi_term = bitset_next_member (&iter))
    {
      termp = QO_ENV_TERM (env, multi_term);
      if (QO_TERM_IS_FLAGED (termp, QO_TERM_MULTI_COLL_PRED))
	{
	  bitset_assign (multi_col_segs, &(QO_TERM_SEGS (termp)));
	  break;
	}
    }

  if (index_entryp)
    {
      bitset_assign (index_segs, &(index_entryp->index_segs));
    }
  bitset_assign (multi_col_range_segs, &(plan->plan_un.scan.multi_col_range_segs));

  return multi_term;
}
