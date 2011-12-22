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
 * plan_generation.c - Generate XASL trees from query optimizer plans
 */

#ident "$Id$"

#include "config.h"

#include <assert.h>

#include "xasl_support.h"

#include "parser.h"
#include "xasl_generation.h"

#include "optimizer.h"
#include "query_graph.h"
#include "query_planner.h"
#include "query_bitset.h"

#define IS_DERIVED_TABLE(node) \
    (QO_NODE_ENTITY_SPEC(node)->info.spec.derived_table)

typedef int (*ELIGIBILITY_FN) (QO_TERM *);

static XASL_NODE *make_scan_proc (QO_ENV * env);
static XASL_NODE *make_mergelist_proc (QO_ENV * env,
				       QO_PLAN * plan,
				       XASL_NODE * left,
				       PT_NODE * left_list,
				       BITSET * left_exprs,
				       PT_NODE * left_elist,
				       XASL_NODE * rght,
				       PT_NODE * rght_list,
				       BITSET * rght_exprs,
				       PT_NODE * rght_elist);
static XASL_NODE *make_fetch_proc (QO_ENV * env, QO_PLAN * plan);
static XASL_NODE *make_buildlist_proc (QO_ENV * env, PT_NODE * namelist);

static XASL_NODE *init_class_scan_proc (QO_ENV * env, XASL_NODE * xasl,
					QO_PLAN * plan);
static XASL_NODE *init_list_scan_proc (QO_ENV * env, XASL_NODE * xasl,
				       XASL_NODE * list, PT_NODE * namelist,
				       BITSET * predset, int *poslist);

static XASL_NODE *add_access_spec (QO_ENV *, XASL_NODE *, QO_PLAN *);
static XASL_NODE *add_scan_proc (QO_ENV * env, XASL_NODE * xasl,
				 XASL_NODE * scan);
static XASL_NODE *add_fetch_proc (QO_ENV * env, XASL_NODE * xasl,
				  XASL_NODE * proc);
static XASL_NODE *add_uncorrelated (QO_ENV * env, XASL_NODE * xasl,
				    XASL_NODE * sub);
static XASL_NODE *add_subqueries (QO_ENV * env, XASL_NODE * xasl, BITSET *);
static XASL_NODE *add_sort_spec (QO_ENV *, XASL_NODE *, QO_PLAN *, bool);
static XASL_NODE *add_if_predicate (QO_ENV *, XASL_NODE *, PT_NODE *);
static XASL_NODE *add_after_join_predicate (QO_ENV *, XASL_NODE *, PT_NODE *);
static XASL_NODE *add_instnum_predicate (QO_ENV *, XASL_NODE *, PT_NODE *);

static PT_NODE *make_pred_from_bitset (QO_ENV * env, BITSET * predset,
				       ELIGIBILITY_FN safe);
static void make_pred_from_plan (QO_ENV * env, QO_PLAN * plan,
				 PT_NODE ** key_access_pred,
				 PT_NODE ** access_pred,
				 QO_XASL_INDEX_INFO * qo_index_infop);
static PT_NODE *make_if_pred_from_plan (QO_ENV * env, QO_PLAN * plan);
static PT_NODE *make_instnum_pred_from_plan (QO_ENV * env, QO_PLAN * plan);
static PT_NODE *make_namelist_from_projected_segs (QO_ENV * env,
						   QO_PLAN * plan);
static SORT_LIST *make_sort_list_after_eqclass (QO_ENV * env, int column_cnt);

static XASL_NODE *gen_outer (QO_ENV *, QO_PLAN *, BITSET *,
			     XASL_NODE *, XASL_NODE *, XASL_NODE *);
static XASL_NODE *gen_inner (QO_ENV *, QO_PLAN *, BITSET *, BITSET *,
			     XASL_NODE *, XASL_NODE *, bool);
static XASL_NODE *preserve_info (QO_ENV * env, QO_PLAN * plan,
				 XASL_NODE * xasl);

static int is_normal_access_term (QO_TERM *);
static int is_norma_if_term (QO_TERM *);
static int is_after_join_term (QO_TERM *);
static int is_totally_after_join_term (QO_TERM *);
static int is_follow_if_term (QO_TERM *);
static int is_always_true (QO_TERM *);

static QO_XASL_INDEX_INFO *qo_get_xasl_index_info (QO_ENV * env,
						   QO_PLAN * plan);
static void qo_free_xasl_index_info (QO_ENV * env, QO_XASL_INDEX_INFO * info);

static bool qo_validate_regu_var_for_limit (REGU_VARIABLE * var_p);
static bool qo_get_limit_from_instnum_pred (PARSER_CONTEXT * parser,
					    PRED_EXPR * pred,
					    REGU_PTR_LIST * lower,
					    REGU_PTR_LIST * upper);
static bool qo_get_limit_from_eval_term (PARSER_CONTEXT * parser,
					 PRED_EXPR * pred,
					 REGU_PTR_LIST * lower,
					 REGU_PTR_LIST * upper);

static REGU_PTR_LIST regu_ptr_list_create ();
static void regu_ptr_list_free (REGU_PTR_LIST list);
static REGU_PTR_LIST regu_ptr_list_add_regu (REGU_VARIABLE * var_p,
					     REGU_PTR_LIST list);


/*
 * make_scan_proc () -
 *   return: XASL_NODE *
 *   env(in): The optimizer environment
 */
static XASL_NODE *
make_scan_proc (QO_ENV * env)
{
  return ptqo_to_scan_proc (QO_ENV_PARSER (env), NULL, NULL, NULL, NULL,
			    NULL);
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

  make_pred_from_plan (env, plan, NULL, &access_pred, NULL);
  if_pred = make_if_pred_from_plan (env, plan);

  xasl = pt_to_fetch_proc (QO_ENV_PARSER (env),
			   QO_NODE_ENTITY_SPEC (QO_TERM_TAIL
						(plan->plan_un.follow.path)),
			   access_pred);
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
make_mergelist_proc (QO_ENV * env,
		     QO_PLAN * plan,
		     XASL_NODE * left,
		     PT_NODE * left_list,
		     BITSET * left_exprs,
		     PT_NODE * left_elist,
		     XASL_NODE * rght,
		     PT_NODE * rght_list,
		     BITSET * rght_exprs, PT_NODE * rght_elist)
{
  XASL_NODE *merge = NULL;
  PARSER_CONTEXT *parser = NULL;
  QFILE_LIST_MERGE_INFO *ls_merge;
  PT_NODE *outer_attr, *inner_attr;
  int i, left_epos, rght_epos, cnt, seg_idx, ncols;
  int left_nlen, left_elen, rght_nlen, rght_elen, nlen;
  SORT_LIST *left_order, *rght_order;
  QO_TERM *term;
  QO_EQCLASS *order;
  BITSET_ITERATOR bi;
  BITSET merge_eqclass;
  BITSET merge_terms;
  BITSET term_segs;

  bitset_init (&merge_eqclass, env);
  bitset_init (&merge_terms, env);
  bitset_init (&term_segs, env);

  if (env == NULL || plan == NULL)
    {
      goto exit_on_error;
    }

  parser = QO_ENV_PARSER (env);

  merge =
    ptqo_to_merge_list_proc (parser, left, rght,
			     plan->plan_un.join.join_type);

  if (merge == NULL || left == NULL || left_list == NULL || rght == NULL
      || rght_list == NULL)
    {
      goto exit_on_error;
    }

  ls_merge = &merge->proc.mergelist.ls_merge;

  ls_merge->join_type = plan->plan_un.join.join_type;

  for (i = bitset_iterate (&(plan->plan_un.join.join_terms), &bi);
       i != -1; i = bitset_next_member (&bi))
    {
      term = QO_ENV_TERM (env, i);
      order = QO_TERM_EQCLASS (term);

      if (order != QO_UNORDERED)
	{
	  if (BITSET_MEMBER (merge_eqclass, QO_EQCLASS_IDX (order)))
	    {
	      continue;
	    }
	  bitset_add (&merge_eqclass, QO_EQCLASS_IDX (order));
	}

      bitset_add (&merge_terms, i);
    }

  ncols = ls_merge->ls_column_cnt = bitset_cardinality (&merge_terms);
  ls_merge->ls_outer_column =
    (int *) pt_alloc_packing_buf (ncols * sizeof (int));
  if (ls_merge->ls_outer_column == NULL)
    {
      goto exit_on_error;
    }

  ls_merge->ls_outer_unique =
    (int *) pt_alloc_packing_buf (ncols * sizeof (int));
  if (ls_merge->ls_outer_unique == NULL)
    {
      goto exit_on_error;
    }

  ls_merge->ls_inner_column =
    (int *) pt_alloc_packing_buf (ncols * sizeof (int));

  if (ls_merge->ls_inner_column == NULL)
    {
      goto exit_on_error;
    }

  ls_merge->ls_inner_unique =
    (int *) pt_alloc_packing_buf (ncols * sizeof (int));
  if (ls_merge->ls_inner_unique == NULL)
    {
      goto exit_on_error;
    }

  left_order = left->orderby_list = make_sort_list_after_eqclass (env, ncols);
  rght_order = rght->orderby_list = make_sort_list_after_eqclass (env, ncols);

  cnt = 0;			/* init */
  left_epos = rght_epos = 0;	/* init */
  for (i = bitset_iterate (&merge_terms, &bi);
       i != -1; i = bitset_next_member (&bi))
    {

      term = QO_ENV_TERM (env, i);

      if (ls_merge->join_type == JOIN_INNER && QO_IS_PATH_TERM (term))
	{
	  /* mark merge join spec as single-fetch */
	  ls_merge->single_fetch = QPROC_SINGLE_OUTER;
	}

      if (BITSET_MEMBER (*left_exprs, i) && left_elist != NULL)
	{
	  /* Then we added an "extra" column for the expression to the
	   * left_elist.  We want to treat that expression
	   * as the outer expression, but we want to leave it off of
	   * the list of segments that are projected out of the merge.
	   * Take it off, but remember it in "outer_attr" so that
	   * we can fix up domain info in a little while.
	   */
	  ls_merge->ls_outer_column[cnt] = left_epos++;
	  outer_attr = left_elist;
	  left_elist = left_elist->next;
	}
      else
	{
	  /* Determine which attributes are involved in this predicate.
	   */
	  bitset_assign (&term_segs, &(QO_TERM_SEGS (term)));
	  bitset_intersect (&term_segs,
			    &((plan->plan_un.join.outer)->info->
			      projected_segs));
	  seg_idx = bitset_first_member (&term_segs);
	  if (seg_idx == -1)
	    {
	      goto exit_on_error;
	    }

	  outer_attr = QO_SEG_PT_NODE (QO_ENV_SEG (env, seg_idx));

	  ls_merge->ls_outer_column[cnt] =
	    pt_find_attribute (parser, outer_attr, left_list);
	}
      ls_merge->ls_outer_unique[cnt] = false;	/* currently, unused */

      if (BITSET_MEMBER (*rght_exprs, i) && rght_elist != NULL)
	{
	  /* This situation is exactly analogous to the one above,
	   * except that we're concerned with the right (inner) side
	   * this time.
	   */
	  ls_merge->ls_inner_column[cnt] = rght_epos++;
	  inner_attr = rght_elist;
	  rght_elist = rght_elist->next;
	}
      else
	{
	  /* Determine which attributes are involved in this predicate.
	   */
	  bitset_assign (&term_segs, &(QO_TERM_SEGS (term)));
	  bitset_intersect (&term_segs,
			    &((plan->plan_un.join.inner)->info->
			      projected_segs));
	  seg_idx = bitset_first_member (&term_segs);
	  if (seg_idx == -1)
	    {
	      goto exit_on_error;
	    }

	  inner_attr = QO_SEG_PT_NODE (QO_ENV_SEG (env, seg_idx));

	  ls_merge->ls_inner_column[cnt] =
	    pt_find_attribute (parser, inner_attr, rght_list);
	}

      ls_merge->ls_inner_unique[cnt] = false;	/* currently, unused */

      if (left_order == NULL)
	{
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE,
		  ER_QO_FAILED_ASSERTION, 0);
	  goto exit_on_error;
	}
      left_order->s_order = S_ASC;
      left_order->pos_descr.pos_no = ls_merge->ls_outer_column[cnt];
      left_order->pos_descr.dom = pt_xasl_node_to_domain (parser, outer_attr);
      left_order = left_order->next;

      if (rght_order == NULL)
	{
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE,
		  ER_QO_FAILED_ASSERTION, 0);
	  goto exit_on_error;
	}

      rght_order->s_order = S_ASC;
      rght_order->pos_descr.pos_no = ls_merge->ls_inner_column[cnt];
      rght_order->pos_descr.dom = pt_xasl_node_to_domain (parser, inner_attr);
      rght_order = rght_order->next;

      cnt++;
    }				/* for (i = ... ) */

  left_elen = bitset_cardinality (left_exprs);
  left_nlen = pt_length_of_list (left_list) - left_elen;
  rght_elen = bitset_cardinality (rght_exprs);
  rght_nlen = pt_length_of_list (rght_list) - rght_elen;

  nlen = ls_merge->ls_pos_cnt = left_nlen + rght_nlen;
  ls_merge->ls_outer_inner_list =
    (int *) pt_alloc_packing_buf (nlen * sizeof (int));
  if (ls_merge->ls_outer_inner_list == NULL)
    {
      goto exit_on_error;
    }

  ls_merge->ls_pos_list = (int *) pt_alloc_packing_buf (nlen * sizeof (int));
  if (ls_merge->ls_pos_list == NULL)
    {
      goto exit_on_error;
    }

  /* these could be sorted out arbitrily. This could make it
   * easier to avoid the wrapper buildlist_proc, when no expressions,
   * predicates, subqueries, fetches, or aggregation is involved.
   * For now, we always build the same thing, with simple column
   * concatenation.
   */

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

  /* make outer_spec_list, outer_val_list, inner_spec_list, and
     inner_val_list */
  if (ls_merge->join_type != JOIN_INNER)
    {
      PT_NODE *other_pred;
      int *poslist;

      /* set poslist of outer XASL node
       */
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
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_OUT_OF_VIRTUAL_MEMORY, 1, left_nlen * sizeof (int));
	      goto exit_on_error;
	    }

	  for (i = 0; i < left_nlen; i++)
	    {
	      poslist[i] = i + left_elen;
	    }
	}

      /* sets xasl->spec_list and xasl->val_list */
      merge = ptqo_to_list_scan_proc (parser, merge, SCAN_PROC,
				      left, left_list, NULL, poslist);
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

      /* set poslist of inner XASL node
       */
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
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_OUT_OF_VIRTUAL_MEMORY, 1, rght_nlen * sizeof (int));
	      goto exit_on_error;
	    }

	  for (i = 0; i < rght_nlen; i++)
	    {
	      poslist[i] = i + rght_elen;
	    }
	}

      /* sets xasl->spec_list and xasl->val_list */
      merge = ptqo_to_list_scan_proc (parser, merge, SCAN_PROC,
				      rght, rght_list, NULL, poslist);
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
      other_pred =
	make_pred_from_bitset (env, &(plan->plan_un.join.during_join_terms),
			       is_always_true);
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
  bitset_delset (&merge_terms);
  bitset_delset (&merge_eqclass);

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

  for (i = bitset_iterate (predset, &bi);
       i != -1; i = bitset_next_member (&bi))
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
  PT_NODE *access_pred;
  PT_NODE *if_pred;
  PT_NODE *after_join_pred;
  QO_XASL_INDEX_INFO *info;

  parser = QO_ENV_PARSER (env);

  spec = QO_NODE_ENTITY_SPEC (plan->plan_un.scan.node);

  info = qo_get_xasl_index_info (env, plan);
  make_pred_from_plan (env, plan, &key_pred, &access_pred, info);
  xasl = ptqo_to_scan_proc (parser, xasl, spec, key_pred, access_pred, info);

  /* free pointer node list */
  parser_free_tree (parser, key_pred);
  parser_free_tree (parser, access_pred);

  if (xasl)
    {
      after_join_pred = make_pred_from_bitset (env, &(plan->sarged_terms),
					       is_after_join_term);
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
init_list_scan_proc (QO_ENV * env,
		     XASL_NODE * xasl,
		     XASL_NODE * listfile,
		     PT_NODE * namelist, BITSET * predset, int *poslist)
{
  PT_NODE *access_pred, *if_pred, *after_join_pred, *instnum_pred;

  if (xasl)
    {
      access_pred =
	make_pred_from_bitset (env, predset, is_normal_access_term);
      if_pred = make_pred_from_bitset (env, predset, is_norma_if_term);
      after_join_pred =
	make_pred_from_bitset (env, predset, is_after_join_term);
      instnum_pred =
	make_pred_from_bitset (env, predset, is_totally_after_join_term);

      xasl = ptqo_to_list_scan_proc (QO_ENV_PARSER (env),
				     xasl,
				     SCAN_PROC,
				     listfile,
				     namelist, access_pred, poslist);

      if (env->pt_tree->node_type == PT_SELECT
	  && env->pt_tree->info.query.q.select.connect_by)
	{
	  pt_set_level_node_etc (QO_ENV_PARSER (env),
				 if_pred, &xasl->level_val);
	  pt_set_isleaf_node_etc (QO_ENV_PARSER (env),
				  if_pred, &xasl->isleaf_val);
	  pt_set_iscycle_node_etc (QO_ENV_PARSER (env),
				   if_pred, &xasl->iscycle_val);
	  pt_set_connect_by_operator_node_etc (QO_ENV_PARSER (env),
					       if_pred, xasl);
	  pt_set_qprior_node_etc (QO_ENV_PARSER (env), if_pred, xasl);
	}

      xasl = add_if_predicate (env, xasl, if_pred);
      xasl = add_after_join_predicate (env, xasl, after_join_pred);
      xasl = add_instnum_predicate (env, xasl, instnum_pred);

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

  info = qo_get_xasl_index_info (env, plan);
  make_pred_from_plan (env, plan, &key_pred, &access_pred, info);

  xasl->spec_list = pt_to_spec_list (parser, class_spec,
				     key_pred, access_pred, info, NULL);
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

  if (env->pt_tree->node_type == PT_SELECT
      && env->pt_tree->info.query.q.select.connect_by)
    {
      pt_set_level_node_etc (parser, if_pred, &xasl->level_val);
      pt_set_isleaf_node_etc (parser, if_pred, &xasl->isleaf_val);
      pt_set_iscycle_node_etc (parser, if_pred, &xasl->iscycle_val);
      pt_set_connect_by_operator_node_etc (parser, if_pred, xasl);
      pt_set_qprior_node_etc (parser, if_pred, xasl);
    }

  xasl = add_if_predicate (env, xasl, if_pred);
  xasl = add_instnum_predicate (env, xasl, instnum_pred);

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
      xasl->aptr_list =
	pt_remove_xasl (pt_append_xasl (xasl->aptr_list, sub), xasl);
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
      for (i = bitset_iterate (subqueries, &bi);
	   i != -1; i = bitset_next_member (&bi))
	{
	  subq = &env->subqueries[i];
	  sub_xasl = (XASL_NODE *) subq->node->info.query.xasl;
	  if (sub_xasl)
	    {
	      if (bitset_is_empty (&(subq->nodes)))
		{		/* uncorrelated */
		  xasl->aptr_list =
		    pt_remove_xasl (pt_append_xasl
				    (xasl->aptr_list, sub_xasl), xasl);
		}
	      else
		{		/* correlated */
		  xasl->dptr_list =
		    pt_remove_xasl (pt_append_xasl
				    (xasl->dptr_list, sub_xasl), xasl);
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
add_sort_spec (QO_ENV * env, XASL_NODE * xasl, QO_PLAN * plan,
	       bool instnum_flag)
{
  QO_PLAN *subplan;

  subplan = plan->plan_un.sort.subplan;

  /*
   * xasl->orderby_list for m-join is added in make_mergelist_proc()
   */

  if (instnum_flag)
    {
      if (xasl
	  && subplan->plan_type == QO_PLANTYPE_JOIN
	  && subplan->plan_un.join.join_method == QO_JOINMETHOD_MERGE_JOIN)
	{
	  PT_NODE *instnum_pred;

	  instnum_pred = make_instnum_pred_from_plan (env, plan);
	  xasl = add_instnum_predicate (env, xasl, instnum_pred);
	  /* free pointer node list */
	  parser_free_tree (QO_ENV_PARSER (env), instnum_pred);
	}
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

static XASL_NODE *
add_instnum_predicate (QO_ENV * env, XASL_NODE * xasl, PT_NODE * pred)
{
  PARSER_CONTEXT *parser;
  int flag;

  if (xasl && pred)
    {
      parser = QO_ENV_PARSER (env);

      pt_set_numbering_node_etc (parser, pred, &xasl->instnum_val, NULL);
      flag = 0;
      xasl->instnum_pred = pt_to_pred_expr_with_arg (parser, pred, &flag);
      if (flag & PT_PRED_ARG_INSTNUM_CONTINUE)
	{
	  xasl->instnum_flag = XASL_INSTNUM_FLAG_SCAN_CONTINUE;
	}
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
    return 0;
  if (QO_TERM_CLASS (term) == QO_TC_OTHER ||
      /*QO_TERM_CLASS(term) == QO_TC_DURING_JOIN || */
      /* nl outer join treats during join terms as sarged terms of inner */
      QO_TERM_CLASS (term) == QO_TC_AFTER_JOIN ||
      QO_TERM_CLASS (term) == QO_TC_TOTALLY_AFTER_JOIN)
    {
      return 0;
    }

  return 1;
}

/*
 * is_norma_if_term () -
 *   return:
 *   term(in):
 */
static int
is_norma_if_term (QO_TERM * term)
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
  if (QO_TERM_CLASS (term) == QO_TC_DURING_JOIN /*? */  ||
      QO_TERM_CLASS (term) == QO_TC_AFTER_JOIN ||
      QO_TERM_CLASS (term) == QO_TC_TOTALLY_AFTER_JOIN)
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
  for (i = bitset_iterate (predset, &bi); i != -1;
       i = bitset_next_member (&bi))
    {
      term = QO_ENV_TERM (env, i);

      /* Don't ever let one of our fabricated terms find its way into
       * the predicate; that will cause serious confusion.
       */
      if (QO_IS_FAKE_TERM (term) || !(*safe) (term))
	{
	  continue;
	}

      /* We need to use predicate pointer.
       * modifying WHERE clause structure in place gives us no way
       * to compile the query if the optimizer bails out.
       */
      if ((pt_expr = QO_TERM_PT_EXPR (term)) == NULL)
	{
	  /* is possible ? */
	  goto exit_on_error;
	}
      if ((pointer = pt_point (parser, pt_expr)) == NULL)
	{
	  goto exit_on_error;
	}

      /* set AND predicate evaluation selectivity, rank;
       */
      pointer->info.pointer.sel = QO_TERM_SELECTIVITY (term);
      pointer->info.pointer.rank = QO_TERM_RANK (term);

      /* insert to the AND predicate list by descending order of
       * (selectivity, rank) vector; this order is used at
       * pt_to_pred_expr_with_arg()
       */
      found = false;		/* init */
      prev = NULL;		/* init */
      for (curr = pred_list; curr; curr = curr->next)
	{
	  cmp = curr->info.pointer.sel - pointer->info.pointer.sel;

	  if (cmp == 0)
	    {			/* same selectivity, re-compare rank */
	      cmp = curr->info.pointer.rank - pointer->info.pointer.rank;
	    }

	  if (cmp <= 0)
	    {
	      pointer->next = curr;
	      if (prev == NULL)
		{		/* very the first */
		  pred_list = pointer;
		}
	      else
		{
		  prev->next = pointer;
		}
	      found = true;
	      break;
	    }

	  prev = curr;
	}

      /* append to the predicate list */
      if (found == false)
	{
	  if (prev == NULL)
	    {			/* very the first */
	      pointer->next = pred_list;
	      pred_list = pointer;
	    }
	  else
	    {			/* very the last */
	      prev->next = pointer;
	    }
	}
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
make_pred_from_plan (QO_ENV * env, QO_PLAN * plan,
		     PT_NODE ** key_predp,
		     PT_NODE ** predp, QO_XASL_INDEX_INFO * qo_index_infop)
{
  /* initialize output parameter */
  if (key_predp != NULL)
    {
      *key_predp = NULL;
    }

  if (predp != NULL)
    {
      *predp = NULL;
    }

  if (plan->plan_type == QO_PLANTYPE_FOLLOW)
    {
      /* Don't allow predicates to migrate to fetch_proc access specs;
         the special handling of NULL doesn't look at the access spec,
         so it will miss predicates that are deposited there.  Always
         put these things on the if_pred for now.
         This needs to get fixed.
         >>>> Note the same problem is encountered when emulating follow with
         >>>> joins. The access pred must return a row, even if its null.
         >>>> the rest or the predicate may then be applied. */
      return;
    }

  /* This is safe guard code - DO NOT DELETE ME
   */
  do
    {
      /* exclude key-range terms from key-filter terms */
      bitset_difference (&(plan->plan_un.scan.kf_terms),
			 &(plan->plan_un.scan.terms));

      /* exclude key-range terms from sarged terms */
      bitset_difference (&(plan->sarged_terms), &(plan->plan_un.scan.terms));
      /* exclude key-filter terms from sarged terms */
      bitset_difference (&(plan->sarged_terms),
			 &(plan->plan_un.scan.kf_terms));
    }
  while (0);

  /* if key filter(predicates) is not required */
  if (predp != NULL && (key_predp == NULL || qo_index_infop == NULL))
    {
      *predp = make_pred_from_bitset (env, &(plan->sarged_terms),
				      bitset_has_path (env,
						       &(plan->sarged_terms))
				      ? path_access_term :
				      is_normal_access_term);
      return;
    }

  /* make predicate list for key filter */
  if (key_predp != NULL)
    {
      *key_predp =
	make_pred_from_bitset (env, &(plan->plan_un.scan.kf_terms),
			       is_always_true);
    }

  /* make predicate list for data filter */
  if (predp != NULL)
    {
      *predp = make_pred_from_bitset (env, &(plan->sarged_terms),
				      bitset_has_path (env,
						       &(plan->
							 sarged_terms)) ?
				      path_access_term :
				      is_normal_access_term);
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
      test = bitset_has_path (env, &(plan->sarged_terms))
	? path_if_term : is_norma_if_term;
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
  return make_pred_from_bitset (env, &(plan->sarged_terms),
				is_totally_after_join_term);
}				/* make_instnum_pred_from_plan() */

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

  for (i = bitset_iterate (&((plan->info)->projected_segs), &bi);
       namelistp != NULL && i != -1; i = bitset_next_member (&bi))
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
 * make_sort_list_after_eqclass () -
 *   return: SORT_LIST *
 *   env(in): The optimizer environment
 *   column_cnt(in): ordering column count
 *
 * Note: Find one of the projected segments that belongs to the right
 *    equivalence class and build a SORT_LIST with a POS_DESC for
 *    that attribute.
 */
static SORT_LIST *
make_sort_list_after_eqclass (QO_ENV * env, int column_cnt)
{
  SORT_LIST *list, *single;
  int i;

  list = NULL;			/* init */

  for (i = 0; i < column_cnt; i++)
    {
      single = ptqo_single_orderby (env->parser);
      if (list == NULL)
	{			/* the first time */
	  list = single;
	}
      else if (single != NULL)
	{			/* insert into the head of list */
	  single->next = list;
	  list = single;
	}
    }

  return list;
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
  for (merge = xasl->aptr_list;
       merge && merge->type != MERGELIST_PROC; merge = merge->next)
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
      || xasl->spec_list == NULL
      || xasl->val_list == NULL || xasl->outptr_list == NULL
      /*
       * Make sure the merge_list_info looks plausible.
       */
      || merge->proc.mergelist.outer_xasl == NULL
      || merge->proc.mergelist.inner_xasl == NULL
      || merge->proc.mergelist.ls_merge.ls_column_cnt <= 0)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_QO_FAILED_ASSERTION, 0);
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
	      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE,
		      ER_QO_FAILED_ASSERTION, 0);
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

  for (t = bitset_iterate (&(plan->sarged_terms), &iter);
       t != -1; t = bitset_next_member (&iter))
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
gen_outer (QO_ENV * env, QO_PLAN * plan, BITSET * subqueries,
	   XASL_NODE * inner_scans, XASL_NODE * fetches, XASL_NODE * xasl)
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

      /* outer join could have terms classed as AFTER JOIN TERM;
       * setting after join terms to merged list scan
       */
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
	      xasl = gen_outer (env,
				plan->plan_un.sort.subplan,
				&new_subqueries, inner_scans, fetches, xasl);
	      return xasl;
	    }
	}

      /*
       * If inner_scans is not empty, this plan is really a subplan of
       * some outer join node, and we need to make xasl scan the
       * contents of the temp file intended to be created by this plan.
       * If not, we're really at the "top" of a tree (we haven't gone
       * through a join node yet) and we can simply recurse, tacking on
       * our sort spec after the recursion.
       */
      if (inner_scans)
	{
	  PT_NODE *namelist = NULL;

	  namelist = make_namelist_from_projected_segs (env, plan);
	  listfile = make_buildlist_proc (env, namelist);
	  listfile = gen_outer (env,
				plan->plan_un.sort.subplan,
				&EMPTY_SET, NULL, NULL, listfile);
	  listfile = add_sort_spec (env, listfile, plan, false);
	  xasl = add_uncorrelated (env, xasl, listfile);
	  xasl = init_list_scan_proc (env, xasl, listfile, namelist,
				      &(plan->sarged_terms), NULL);
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
	  xasl = gen_outer (env,
			    plan->plan_un.sort.subplan,
			    &new_subqueries, inner_scans, fetches, xasl);
	  xasl = add_sort_spec (env, xasl, plan, true /*add instnum pred */ );
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
	      xasl = pt_gen_simple_merge_plan (parser,
					       xasl, QO_ENV_PT_TREE (env));
	      break;
	    }
	  else
	    {
	      /* FALL THROUGH */
	    }
	case QO_JOINMETHOD_IDX_JOIN:
	  for (i = bitset_iterate (&(plan->plan_un.join.join_terms), &bi);
	       i != -1; i = bitset_next_member (&bi))
	    {
	      term = QO_ENV_TERM (env, i);
	      if (QO_IS_FAKE_TERM (term))
		{
		  bitset_union (&fake_subqueries,
				&(QO_TERM_SUBQUERIES (term)));
		}
	    }			/* for (i = ... ) */

	  bitset_difference (&new_subqueries, &fake_subqueries);

	  for (i = bitset_iterate (&predset, &bi);
	       i != -1; i = bitset_next_member (&bi))
	    {
	      term = QO_ENV_TERM (env, i);
	      if (is_totally_after_join_term (term))
		{
		  bitset_add (&taj_terms, i);
		}
	    }			/* for (i = ... ) */
	  /* exclude totally after join term and push into inner */
	  bitset_difference (&predset, &taj_terms);

	  /*
	   * In case of outer join, we should not use sarg terms as key filter terms.
	   * If not, a term, which should be applied after single scan, can be applied
	   * during btree_range_search. It means that there can be no records fetched
	   * by single scan due to key filtering, and null records can be returned
	   * by scan_handle_single_scan. It might lead to making a wrong result.
	   */
	  scan = gen_inner (env, inner, &predset, &new_subqueries, 
			    inner_scans, fetches,
			    IS_OUTER_JOIN_TYPE (join_type));
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

	    /* find outer/inner segs from expr and name
	     */
	    bitset_assign (&plan_segs, &((plan->info)->projected_segs));
	    for (i = bitset_iterate (&predset, &bi);
		 i != -1; i = bitset_next_member (&bi))
	      {

		term = QO_ENV_TERM (env, i);

		if (BITSET_MEMBER (plan->plan_un.join.join_terms, i))
		  {		/* is m-join edge */

		    BITSET_CLEAR (temp_segs);	/* init */

		    pt_expr = QO_TERM_PT_EXPR (term);
		    qo_expr_segs (env, pt_left_part (pt_expr), &temp_segs);

		    /* is lhs matching outer ? */
		    if (bitset_intersects
			(&temp_segs, &((outer->info)->projected_segs)))
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
			left_elist =
			  parser_append_node (pt_point (parser, left),
					      left_elist);
			bitset_add (&left_exprs, i);
		      }
		    else
		      {
			bitset_union (&plan_segs, &(QO_TERM_SEGS (term)));
		      }

		    if (pt_is_expr_node (rght) || pt_is_function (rght))
		      {
			/* append to the expr list */
			rght_elist =
			  parser_append_node (pt_point (parser, rght),
					      rght_elist);
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
	      }			/* for (i = ...) */

	    /* build outer segs namelist
	     */
	    bitset_assign (&temp_segs, &((outer->info)->projected_segs));	/* save */

	    bitset_intersect (&((outer->info)->projected_segs), &plan_segs);
	    left_nlist = make_namelist_from_projected_segs (env, outer);
	    left_nlen = pt_length_of_list (left_nlist);	/* only names include */

	    /* make expr, name list */
	    left_list = parser_append_node (left_nlist, left_elist);
	    left_xasl = make_buildlist_proc (env, left_list);
	    left_xasl =
	      gen_outer (env, outer, &EMPTY_SET, NULL, NULL, left_xasl);
	    bitset_assign (&((outer->info)->projected_segs), &temp_segs);	/* restore */

	    /* build inner segs namelist
	     */
	    bitset_assign (&temp_segs, &((inner->info)->projected_segs));	/* save */

	    bitset_intersect (&((inner->info)->projected_segs), &plan_segs);
	    rght_nlist = make_namelist_from_projected_segs (env, inner);
	    rght_nlen = pt_length_of_list (rght_nlist);	/* only names include */

	    /* make expr, name list */
	    rght_list = parser_append_node (rght_nlist, rght_elist);
	    rght_xasl = make_buildlist_proc (env, rght_list);
	    rght_xasl =
	      gen_outer (env, inner, &EMPTY_SET, NULL, NULL, rght_xasl);
	    bitset_assign (&((inner->info)->projected_segs), &temp_segs);	/* restore */

	    merge = make_mergelist_proc (env, plan,
					 left_xasl, left_list,
					 &left_exprs, left_elist,
					 rght_xasl, rght_list,
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
		bitset_difference (&predset,
				   &(plan->plan_un.join.during_join_terms));
	      }

	    /* set referenced segments */
	    bitset_assign (&plan_segs, &((plan->info)->projected_segs));
	    for (i = bitset_iterate (&predset, &bi);
		 i != -1; i = bitset_next_member (&bi))
	      {
		term = QO_ENV_TERM (env, i);
		bitset_union (&plan_segs, &(QO_TERM_SEGS (term)));
	      }

	    /* generate left name list of projected segs */
	    bitset_assign (&temp_segs, &((outer->info)->projected_segs));
	    bitset_intersect (&temp_segs, &plan_segs);
	    for (i = bitset_iterate (&temp_segs, &bi);
		 i != -1; i = bitset_next_member (&bi))
	      {
		seg_nlist =
		  parser_append_node (pt_point (parser,
						QO_SEG_PT_NODE (QO_ENV_SEG
								(env, i))),
				      seg_nlist);
	      }

	    /* generate right name list of projected segs */
	    bitset_assign (&temp_segs, &((inner->info)->projected_segs));
	    bitset_intersect (&temp_segs, &plan_segs);
	    for (i = bitset_iterate (&temp_segs, &bi);
		 i != -1; i = bitset_next_member (&bi))
	      {
		seg_nlist =
		  parser_append_node (pt_point (parser,
						QO_SEG_PT_NODE (QO_ENV_SEG
								(env, i))),
				      seg_nlist);
	      }

	    seg_nlen = pt_length_of_list (seg_nlist);

	    /* set used column position in name list and
	     * filter out unnecessary join edge segs from projected segs
	     */
	    if (seg_nlen > 0 && seg_nlen < left_nlen + rght_nlen)
	      {
		QFILE_LIST_MERGE_INFO *ls_merge;
		int outer_inner, pos = 0, p;
		PT_NODE *attr;

		seg_pos_list = (int *) malloc (seg_nlen * sizeof (int));
		if (seg_pos_list == NULL)
		  {
		    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			    ER_OUT_OF_VIRTUAL_MEMORY, 1,
			    seg_nlen * sizeof (int));
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
			if (ls_merge->ls_outer_inner_list[i] == outer_inner
			    && ls_merge->ls_pos_list[i] == pos)
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
		  }		/* for (attr = seg_nlist; ...) */

		if (pos == -1)
		  {		/* error */
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
	      }			/* if (seg_nlen > 0 ...) */

	    if (xasl)
	      {
		xasl = init_list_scan_proc (env, xasl, merge, seg_nlist,
					    &predset, seg_pos_list);
		xasl = add_fetch_proc (env, xasl, fetches);
		xasl = add_subqueries (env, xasl, &new_subqueries);
	      }

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

	  }

	  /*
	   * This can be removed after we trust ourselves some more.
	   */
	  xasl = check_merge_xasl (env, xasl);
	  break;

	default:
	  break;
	}			/* switch (plan->plan_un.join.join_method) */

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
      xasl =
	gen_outer (env, plan->plan_un.follow.head, &EMPTY_SET, inner_scans,
		   fetch, xasl);
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
 *   do_not_push_sarg_to_kf(in): Do not use sarg as key filter if it is true
 */
static XASL_NODE *
gen_inner (QO_ENV * env, QO_PLAN * plan, BITSET * predset,
	   BITSET * subqueries, XASL_NODE * inner_scans, XASL_NODE * fetches,
	   bool do_not_push_sarg_to_kf)
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
       * For nl-join and idx-join, we push join edge to sarg term of inner scan
       * to filter out unsatisfied records earlier. Especially, if a scan plan
       * uses a covered index and join type is not outer join, we push them to
       * key filter instead of sarg term.
       */
      if (do_not_push_sarg_to_kf == false
	  && plan->plan_un.scan.scan_method == QO_SCANMETHOD_INDEX_SCAN
	  && plan->plan_un.scan.index->head
	  && plan->plan_un.scan.index->head->cover_segments)
	{
	  bitset_union (&(plan->plan_un.scan.kf_terms), predset);
	}
      else
	{
	  bitset_union (&(plan->sarged_terms), predset);
	}
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
      scan = gen_inner (env,
			plan->plan_un.follow.head,
			&EMPTY_SET, &EMPTY_SET, inner_scans, fetch, false);
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
      scan = init_list_scan_proc (env, scan, listfile, namelist, predset,
				  NULL);
      if (namelist)
	parser_free_tree (env->parser, namelist);
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
XASL_NODE *
qo_to_xasl (QO_PLAN * plan, XASL_NODE * xasl)
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
      (void) pt_set_dptr (env->parser,
			  env->pt_tree->info.query.q.select.list,
			  lastxasl, MATCH_ALL);

      xasl = preserve_info (env, plan, xasl);
    }
  else
    {
      xasl = NULL;
    }

  if (xasl == NULL)
    {
      int level;
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_QO_FAILED_ASSERTION, 0);
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
 * qo_subplan_iscan_sort_list () - get after index scan PT_SORT_SPEC list for
 *                                 subplan of a sort plan
 *   return: sort list
 *   plan(in): QO_PLAN
 */
PT_NODE *
qo_subplan_iscan_sort_list (QO_PLAN * plan)
{
  if (!plan || plan->plan_type != QO_PLANTYPE_SORT)
    {
      return NULL;
    }
  if (!plan->plan_un.sort.subplan)
    {
      return NULL;
    }

  return plan->plan_un.sort.subplan->iscan_sort_list;
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
	       || plan->plan_un.sort.sort_type == SORT_ORDERBY))
	  ? false : true);
}

/*
 * qo_plan_skip_groupby () - check the plan info for order by
 *   return: true/false
 *   plan(in): QO_PLAN
 */
bool
qo_plan_skip_groupby (QO_PLAN * plan)
{
  return (plan->plan_type == QO_PLANTYPE_SCAN &&
	  plan->plan_un.scan.index &&
	  plan->plan_un.scan.index->head->groupby_skip) ? true : false;
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
  int nterms, nsegs, nkfterms;
  QO_NODE_INDEX_ENTRY *ni_entryp;
  QO_INDEX_ENTRY *index_entryp;
  QO_XASL_INDEX_INFO *index_infop;
  int t, i, j, pos;
  BITSET_ITERATOR iter;
  QO_TERM *termp;

  /* if no index scan terms, no index scan */
  nterms = bitset_cardinality (&(plan->plan_un.scan.terms));
  nkfterms = bitset_cardinality (&(plan->plan_un.scan.kf_terms));

  /* support also indexes with only sarg terms */
  if (plan && plan->plan_type == QO_PLANTYPE_SCAN &&
      plan->plan_un.scan.scan_method == QO_SCANMETHOD_INDEX_GROUPBY_SCAN)
    {
      /* if group by skip plan do not return */
      ;
    }
  else if (nterms <= 0 && nkfterms <= 0 &&
	   bitset_cardinality (&(plan->sarged_terms)) == 0)
    {
      return NULL;
    }

  if (plan->plan_un.scan.index == NULL)
    {
      return NULL;
    }

  /* pointer to QO_NODE_INDEX_ENTRY structure in QO_PLAN */
  ni_entryp = plan->plan_un.scan.index;
  /* pointer to linked list of index node, 'head' field(QO_INDEX_ENTRY
     strucutre) of QO_NODE_INDEX_ENTRY */
  index_entryp = (ni_entryp)->head;
  /* number of indexed segments */
  nsegs = index_entryp->nsegs;	/* nsegs == nterms ? */

  /* allocate QO_XASL_INDEX_INFO structure */
  index_infop = (QO_XASL_INDEX_INFO *) malloc (sizeof (QO_XASL_INDEX_INFO));
  if (index_infop == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      sizeof (QO_XASL_INDEX_INFO));
      goto error;
    }

  if (index_entryp->is_iss_candidate)
    {
      /* allow space for the first element (NULL actually), for instance
       * in term_exprs */
      nterms++;
    }

  if (nterms == 0)
    {
      index_infop->nterms = 0;
      index_infop->term_exprs = NULL;
      index_infop->ni_entry = ni_entryp;
      return index_infop;
    }

  index_infop->nterms = nterms;
  index_infop->term_exprs = (PT_NODE **) malloc (nterms * sizeof (PT_NODE *));
  if (index_infop->term_exprs == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      nterms * sizeof (PT_NODE *));
      goto error;
    }

  if (index_entryp->is_iss_candidate)
    {
      index_infop->term_exprs[0] = NULL;
    }

  index_infop->ni_entry = ni_entryp;

  /* Make 'term_expr[]' array from the given index terms in order of the
     'seg_idx[]' array of the associated index. */

  /* for all index scan terms */
  for (t = bitset_iterate (&(plan->plan_un.scan.terms), &iter); t != -1;
       t = bitset_next_member (&iter))
    {
      /* pointer to QO_TERM denoted by number 't' */
      termp = QO_ENV_TERM (env, t);

      /* Find the matching segment in the segment index array to determine
         the array position to store the expression. We're using the
         'index_seg[]' array of the term to find its segment index */
      pos = -1;
      for (i = 0; i < termp->can_use_index && pos == -1; i++)
	{
	  for (j = 0; j < nsegs; j++)
	    {
	      if (i >=
		  sizeof (termp->index_seg) / sizeof (termp->index_seg[0]))
		{
		  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE,
			  ER_QO_FAILED_ASSERTION, 0);
		  goto error;
		}

	      if ((index_entryp->seg_idxs[j]) ==
		  QO_SEG_IDX (termp->index_seg[i]))
		{
		  pos = j;
		  break;
		}
	    }
	}

      /* always, pos != -1 and 0 < pos < nsegs */
      if (pos < 0)
	{
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE,
		  ER_QO_FAILED_ASSERTION, 0);
	  goto error;
	}

      /* if the index is Index Skip Scan, the first column should have
       * never been found in a term */
      assert (!index_entryp->is_iss_candidate || pos != 0);

      index_infop->term_exprs[pos] = QO_TERM_PT_EXPR (termp);
    }				/* for (t = bitset_iterate(...); ...) */

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
      /*      DEALLOCATE (env, info->term_exprs); */

      free_and_init (info);
      /*    DEALLOCATE(env, info); */
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
 * qo_xasl_get_btid () - Return a point to the index BTID
 *   return: BTID *
 *   classop(in):
 *   info(in): Pointer to info structure
 */
BTID *
qo_xasl_get_btid (MOP classop, QO_XASL_INDEX_INFO * info)
{
  BTID *btid = NULL;
  int i, n_classes;
  QO_INDEX_ENTRY *index;

  n_classes = (info->ni_entry)->n;

  for (i = 0, index = (info->ni_entry)->head;
       ((i < n_classes) && (btid == NULL)); i++, index = index->next)
    {
      if (classop == (index->class_)->mop)
	btid = &(index->constraints->index);
    }

  return btid;
}

/*
 * qo_xasl_get_multi_col () -
 *   return: Return true if the specified index is multi-column index
 *   class_mop(in): Pointer to class MOP to which the index belongs
 *   infop(in):
 */
bool
qo_xasl_get_multi_col (MOP class_mop, QO_XASL_INDEX_INFO * infop)
{
  int i, n_classes;
  QO_INDEX_ENTRY *index_entryp;

  n_classes = (infop->ni_entry)->n;
  for (i = 0, index_entryp = (infop->ni_entry)->head;
       i < n_classes && index_entryp; i++, index_entryp = index_entryp->next)
    {
      if (class_mop == (index_entryp->class_)->mop)
	{
	  return QO_ENTRY_MULTI_COL (index_entryp);
	}
    }

  /* cannot find the index entry with class MOP, is it possible? */
  return false;
}

/*
 * qo_xasl_get_key_limit () - get the index key limits
 *   return: Key limit PT_NODE
 *   class_mop(in): Pointer to class MOP to which the index belongs
 *   infop(in): Pointer to the index info structure
 */
PT_NODE *
qo_xasl_get_key_limit (MOP class_mop, QO_XASL_INDEX_INFO * infop)
{
  int i, n_classes;
  QO_INDEX_ENTRY *index_entryp;
  PT_NODE *key_limit = NULL;

  n_classes = (infop->ni_entry)->n;
  for (i = 0, index_entryp = (infop->ni_entry)->head;
       i < n_classes && index_entryp; i++, index_entryp = index_entryp->next)
    {
      if (class_mop == (index_entryp->class_)->mop)
	{
	  key_limit = index_entryp->key_limit;
	  break;
	}
    }

  return key_limit;
}


/*
 * qo_xasl_get_coverage () - get the index coverage state
 *   return: index coverage state
 *   class_mop(in): Pointer to class MOP to which the index belongs
 *   infop(in): Pointer to the index info structure
 */
bool
qo_xasl_get_coverage (MOP class_mop, QO_XASL_INDEX_INFO * infop)
{
  int i, n_classes;
  QO_INDEX_ENTRY *index_entryp;

  n_classes = (infop->ni_entry)->n;
  for (i = 0, index_entryp = (infop->ni_entry)->head;
       i < n_classes && index_entryp; i++, index_entryp = index_entryp->next)
    {
      if (class_mop == (index_entryp->class_)->mop)
	{
	  return index_entryp->cover_segments;
	}
    }

  return false;
}

/*
 * qo_add_hq_iterations_access_spec () - adds hierarchical query iterations
 *	access spec on single table
 *   return:
 *   plan(in):
 *   xasl(in):
 */
XASL_NODE *
qo_add_hq_iterations_access_spec (QO_PLAN * plan, XASL_NODE * xasl)
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
  make_pred_from_plan (env, plan, &key_pred, &access_pred, index_info);

  xasl->spec_list =
    pt_to_spec_list (parser, class_spec, key_pred, access_pred, index_info,
		     NULL);

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

  p = (REGU_PTR_LIST) db_private_alloc (NULL,
					sizeof (struct regu_ptr_list_node));
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

  node =
    (REGU_PTR_LIST) db_private_alloc (NULL,
				      sizeof (struct regu_ptr_list_node));
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

      return (qo_validate_regu_var_for_limit (aptr->leftptr)
	      && qo_validate_regu_var_for_limit (aptr->rightptr)
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
qo_get_limit_from_eval_term (PARSER_CONTEXT * parser, PRED_EXPR * pred,
			     REGU_PTR_LIST * lower, REGU_PTR_LIST * upper)
{
  REGU_VARIABLE *lhs, *rhs;
  REL_OP op;
  PT_NODE *node_one = NULL;
  TP_DOMAIN *dom_bigint = tp_domain_resolve_default (DB_TYPE_BIGINT);
  REGU_VARIABLE *regu_one, *regu_low;

  if (pred == NULL || pred->type != T_EVAL_TERM ||
      pred->pe.eval_term.et_type != T_COMP_EVAL_TERM)
    {
      return false;
    }

  lhs = pred->pe.eval_term.et.et_comp.lhs;
  rhs = pred->pe.eval_term.et.et_comp.rhs;
  op = pred->pe.eval_term.et.et_comp.rel_op;

  if (!lhs || !rhs)
    {
      return false;
    }
  if (op != R_LE && op != R_LT && op != R_GE && op != R_GT && op != R_EQ)
    {
      return false;
    }

  /* the TYPE_CONSTANT regu variable must be instnum, otherwise it would not
   * be accepted by the parser*/

  /* switch the ops to transform into instnum rel_op value/hostvar */
  if (rhs->type == TYPE_CONSTANT)
    {
      rhs = pred->pe.eval_term.et.et_comp.lhs;
      lhs = pred->pe.eval_term.et.et_comp.rhs;
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

  /* Bring every accepted relation to a form similar to
   * lower < rownum <= upper.
   */
  switch (op)
    {
    case R_EQ:
      /* decrement node value for lower, but remember current value
       * for upper
       */
      node_one = pt_make_integer_value (parser, 1);
      if (!node_one)
	{
	  return false;
	}

      if (!(regu_one = pt_to_regu_variable (parser, node_one, UNBOX_AS_VALUE))
	  || !(regu_low =
	       pt_make_regu_arith (rhs, regu_one, NULL, T_SUB, dom_bigint)))
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
	  || !(regu_low =
	       pt_make_regu_arith (rhs, regu_one, NULL, T_SUB, dom_bigint)))
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
	  || !(regu_low =
	       pt_make_regu_arith (rhs, regu_one, NULL, T_SUB, dom_bigint)))
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
qo_get_limit_from_instnum_pred (PARSER_CONTEXT * parser, PRED_EXPR * pred,
				REGU_PTR_LIST * lower, REGU_PTR_LIST * upper)
{
  if (pred == NULL)
    {
      return false;
    }

  if (pred->type == T_PRED && pred->pe.pred.bool_op == B_AND)
    {
      return (qo_get_limit_from_instnum_pred (parser, pred->pe.pred.lhs,
					      lower, upper)
	      && qo_get_limit_from_instnum_pred (parser, pred->pe.pred.rhs,
						 lower, upper));
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
qo_get_key_limit_from_instnum (PARSER_CONTEXT * parser, QO_PLAN * plan,
			       XASL_NODE * xasl)
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
      if (plan->plan_un.scan.scan_method != QO_SCANMETHOD_INDEX_SCAN
	  && (plan->plan_un.scan.scan_method !=
	      QO_SCANMETHOD_INDEX_ORDERBY_SCAN)
	  && (plan->plan_un.scan.scan_method !=
	      QO_SCANMETHOD_INDEX_GROUPBY_SCAN))
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
  if (!qo_get_limit_from_instnum_pred (parser, xasl->instnum_pred, &lower,
				       &upper))
    {
      return NULL;
    }
  /* not having upper limit is not helpful */
  if (upper == NULL)
    {
      regu_ptr_list_free (lower);
      return NULL;
    }

  limit_infop = (QO_LIMIT_INFO *) db_private_alloc (NULL,
						    sizeof (QO_LIMIT_INFO));
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
      limit_infop->upper = pt_make_regu_arith (limit_infop->upper,
					       ptr->var_p, NULL, T_LEAST,
					       dom_bigint);
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
	  limit_infop->lower =
	    pt_make_regu_arith (limit_infop->lower, ptr->var_p, NULL,
				T_GREATEST, dom_bigint);
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
 */
QO_LIMIT_INFO *
qo_get_key_limit_from_ordbynum (PARSER_CONTEXT * parser, QO_PLAN * plan,
				XASL_NODE * xasl)
{
  REGU_PTR_LIST lower = NULL, upper = NULL, ptr = NULL;
  QO_LIMIT_INFO *limit_infop;
  TP_DOMAIN *dom_bigint = tp_domain_resolve_default (DB_TYPE_BIGINT);

  if (xasl == NULL || xasl->ordbynum_pred == NULL)
    {
      return NULL;
    }

  /* get lower and upper limits */
  if (!qo_get_limit_from_instnum_pred (parser, xasl->ordbynum_pred, &lower,
				       &upper))
    {
      return NULL;
    }
  /* having a lower limit, or not having upper limit is not helpful */
  if (upper == NULL || lower != NULL)
    {
      regu_ptr_list_free (lower);
      regu_ptr_list_free (upper);
      return NULL;
    }

  limit_infop =
    (QO_LIMIT_INFO *) db_private_alloc (NULL, sizeof (QO_LIMIT_INFO));
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
      limit_infop->upper =
	pt_make_regu_arith (limit_infop->upper, ptr->var_p, NULL, T_LEAST,
			    dom_bigint);
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
 * qo_check_plan_for_multiple_ranges_limit_opt () - check the plan to find out
 *                                                  if multiple ranges keylimit
 *                                                  optimization can be used
 *   return:
 *   parser(in):
 *   plan(in):
 *   sort_node(in):
 *   can_optimize(out):
 *
 *   Note: Find the sort column position in the index, and check that all
 *         columns that come before the sort column (on the left side of the
 *         sort column) in the index are either in an equality term, or in
 *         a key list term. Only one column should be in a key list term.
 *
 */
int
qo_check_plan_for_multiple_ranges_limit_opt (PARSER_CONTEXT * parser,
					     QO_PLAN * subplan,
					     PT_NODE * sort_node,
					     int *can_optimize)
{
  int t, i, j, pos;
  BITSET_ITERATOR iter;
  QO_TERM *termp;
  QO_ENV *env;
  QO_INDEX_ENTRY *index_entryp;
  PT_NODE *seg_node;
  int *used_cols, idx_col;
  int kl_terms = 0;

  *can_optimize = false;

  if (!subplan || !subplan->info || !subplan->plan_un.scan.index ||
      subplan->plan_type != QO_PLANTYPE_SCAN ||
      (subplan->plan_un.scan.scan_method != QO_SCANMETHOD_INDEX_SCAN &&
       subplan->plan_un.scan.scan_method != QO_SCANMETHOD_INDEX_ORDERBY_SCAN
       && subplan->plan_un.scan.scan_method !=
       QO_SCANMETHOD_INDEX_GROUPBY_SCAN))
    {
      return NO_ERROR;
    }

  env = subplan->info->env;
  if (!env)
    {
      return NO_ERROR;
    }

  index_entryp = subplan->plan_un.scan.index->head;
  if (!index_entryp)
    {
      return NO_ERROR;
    }

  CAST_POINTER_TO_NODE (sort_node);
  if (sort_node->node_type != PT_NAME)
    {
      return NO_ERROR;
    }

  idx_col = -1;

  /* find the position of the sort column in the index */
  for (i = 0; i < index_entryp->nsegs; i++)
    {
      seg_node = QO_SEG_PT_NODE (QO_ENV_SEG (env, index_entryp->seg_idxs[i]));
      CAST_POINTER_TO_NODE (seg_node);
      if (seg_node->node_type == PT_NAME &&
	  pt_name_equal (parser, sort_node, seg_node))
	{
	  idx_col = i;
	  break;
	}
    }
  if (idx_col < 1)
    {
      return NO_ERROR;
    }

  /* index columns that are used in terms */
  used_cols = (int *) malloc (idx_col * sizeof (int));
  if (!used_cols)
    {
      return ER_FAILED;
    }
  for (i = 0; i < idx_col; i++)
    {
      used_cols[i] = 0;
    }

  /* check all index scan terms */
  for (t = bitset_iterate (&(subplan->plan_un.scan.terms), &iter); t != -1;
       t = bitset_next_member (&iter))
    {
      termp = QO_ENV_TERM (env, t);

      pos = -1;
      for (i = 0; i < termp->can_use_index && i < 2 && pos == -1; i++)
	{
	  for (j = 0; j < index_entryp->nsegs; j++)
	    {
	      if ((index_entryp->seg_idxs[j]) ==
		  QO_SEG_IDX (termp->index_seg[i]))
		{
		  pos = j;
		  break;
		}
	    }
	}
      if (pos == -1)
	{
	  free (used_cols);
	  return NO_ERROR;
	}

      if (pos < idx_col)
	{
	  used_cols[pos]++;
	  /* only helpful if term is equality or key list */
	  switch (QO_TERM_PT_EXPR (termp)->info.expr.op)
	    {
	    case PT_EQ:
	      break;
	    case PT_IS_IN:
	    case PT_EQ_SOME:
	    case PT_RANGE:
	      kl_terms++;
	      break;
	    default:
	      free (used_cols);
	      return NO_ERROR;
	    }
	}
    }				/* for (t = bitset_iterate(...); ...) */

  /* check all key filter terms */
  for (t = bitset_iterate (&(subplan->plan_un.scan.kf_terms), &iter); t != -1;
       t = bitset_next_member (&iter))
    {
      termp = QO_ENV_TERM (env, t);

      pos = -1;
      for (i = 0; i < termp->can_use_index && i < 2 && pos == -1; i++)
	{
	  for (j = 0; j < index_entryp->nsegs; j++)
	    {
	      if ((index_entryp->seg_idxs[j]) ==
		  QO_SEG_IDX (termp->index_seg[i]))
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
	  free (used_cols);
	  return NO_ERROR;
	}

      if (pos < idx_col)
	{
	  /* for key filter terms we are only interested if it is an eq term */
	  if (QO_TERM_PT_EXPR (termp)->info.expr.op == PT_EQ)
	    {
	      used_cols[pos]++;
	    }
	}
    }				/* for (t = bitset_iterate(...); ...) */

  /* check key list terms */
  if (kl_terms > 1)
    {
      free (used_cols);
      return NO_ERROR;
    }
  /* check used columns */
  for (i = 0; i < idx_col; i++)
    {
      if (used_cols[i] == 0)
	{
	  free (used_cols);
	  return NO_ERROR;
	}
    }

  *can_optimize = true;

  free (used_cols);
  return NO_ERROR;
}
