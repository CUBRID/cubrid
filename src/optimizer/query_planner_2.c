/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * planner.c - Search coordinator for query optimization
 * TODO: merge this file to query_planner.c
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>

#include "parser.h"
#include "optimizer.h"
#include "query_planner_2.h"
#include "query_graph_1.h"
#include "query_planner_1.h"
#include "query_graph_2.h"
#include "environment_variable.h"
#include "xasl_generation_2.h"

#define QO_INFO_INDEX(_M_offset, _bitset)  \
    (_M_offset + (unsigned int)(BITPATTERN(_bitset) & planner->node_mask))

typedef enum
{ JOIN_RIGHT_ORDER, JOIN_OPPOSITE_ORDER } JOIN_ORDER_TRY;

static int infos_allocated = 0;
static int infos_deallocated = 0;

static void qo_init_planvec (QO_PLANVEC *);
static void qo_uninit_planvec (QO_PLANVEC *);
static void qo_dump_planvec (QO_PLANVEC *, FILE *, int);
static QO_PLAN_COMPARE_RESULT qo_check_planvec (QO_PLANVEC *, QO_PLAN *);
static QO_PLAN_COMPARE_RESULT qo_cmp_planvec (QO_PLANVEC *, QO_PLAN *);
static QO_PLAN *qo_find_best_plan_on_planvec (QO_PLANVEC *, double);

static QO_INFO *qo_alloc_info (QO_PLANNER *, BITSET *, BITSET *, BITSET *,
			       double);
static void qo_free_info (QO_INFO *);
static void qo_detach_info (QO_INFO *);
static void qo_dump_info (QO_INFO *, FILE *);
static QO_PLAN *qo_find_best_nljoin_inner_plan_on_info (QO_PLAN *, QO_INFO *,
							JOIN_TYPE);
static QO_PLAN *qo_find_best_plan_on_info (QO_INFO *, QO_EQCLASS *, double);
static bool qo_check_new_best_plan_on_info (QO_INFO *, QO_PLAN *);
static int qo_check_plan_on_info (QO_INFO *, QO_PLAN *);
static int qo_examine_correlated_index (QO_INFO *, JOIN_TYPE, QO_INFO *,
					QO_INFO *, BITSET *, BITSET *,
					BITSET *);
static int qo_examine_follow (QO_INFO *, QO_TERM *, QO_INFO *, BITSET *,
			      BITSET *);
static void qo_compute_projected_segs (QO_PLANNER *, BITSET *, BITSET *,
				       BITSET *);
static int qo_compute_projected_size (QO_PLANNER *, BITSET *);


static QO_PLANNER *qo_alloc_planner (QO_ENV *);
static void qo_clean_planner (QO_PLANNER *);
static QO_PLAN *qo_search_planner (QO_PLANNER *);
static void qo_dump_planner_info (QO_PLANNER *, QO_PARTITION *, FILE *);

static QO_PLAN *qo_search_partition (QO_PLANNER *,
				     QO_PARTITION *, QO_EQCLASS *, BITSET *);
static QO_PLAN *qo_combine_partitions (QO_PLANNER *, BITSET *);
static int qo_generate_join_index_scan (QO_INFO *, JOIN_TYPE, QO_PLAN *,
					QO_INFO *, QO_NODE *,
					QO_NODE_INDEX_ENTRY *, BITSET *,
					BITSET *, BITSET *, BITSET *);
static void qo_generate_index_scan (QO_INFO *, QO_NODE *,
				    QO_NODE_INDEX_ENTRY *);

/*
 * qo_init_planvec () -
 *   return:
 *   planvec(in):
 */
static void
qo_init_planvec (QO_PLANVEC * planvec)
{
  int i;

  planvec->overflow = false;
  planvec->nplans = 0;

  for (i = 0; i < NPLANS; ++i)
    planvec->plan[i] = (QO_PLAN *) NULL;
}

/*
 * qo_uninit_planvec () -
 *   return:
 *   planvec(in):
 */
static void
qo_uninit_planvec (QO_PLANVEC * planvec)
{
  int i;

  for (i = 0; i < planvec->nplans; ++i)
    qo_plan_del_ref (planvec->plan[i]);

  planvec->overflow = false;
  planvec->nplans = 0;
}

/*
 * qo_dump_planvec () -
 *   return:
 *   planvec(in):
 *   f(in):
 *   indent(in):
 */
static void
qo_dump_planvec (QO_PLANVEC * planvec, FILE * f, int indent)
{
  int i;
  int positive_indent = indent < 0 ? -indent : indent;

  if (planvec->overflow)
    fputs ("(overflowed) ", f);

  for (i = 0; i < planvec->nplans; ++i)
    {
      qo_plan_fprint (planvec->plan[i], f, indent, NULL);
      fputs ("\n\n", f);
      indent = positive_indent;
    }
}

/*
 * qo_check_planvec () -
 *   return: one of {PLAN_COMP_LT, PLAN_COMP_EQ, PLAN_COMP_GT}
 *   planvec(in):
 *   plan(in):
 */
static QO_PLAN_COMPARE_RESULT
qo_check_planvec (QO_PLANVEC * planvec, QO_PLAN * plan)
{
  /*
   * Check whether the new plan is definitely better than any of the
   * others.  Keep it if it is, or if it is incomparable and we still
   * have room in the planvec.  Return true if we keep the plan, false
   * if not.
   */
  int i;
  int already_retained;
  QO_PLAN_COMPARE_RESULT cmp;
  int num_eq;

  /* init */
  already_retained = 0;
  num_eq = 0;

  for (i = 0; i < planvec->nplans; i++)
    {
      cmp = qo_plan_cmp (planvec->plan[i], plan);

      /* cmp : PLAN_COMP_UNK, PLAN_COMP_LT, PLAN_COMP_EQ, PLAN_COMP_GT */
      if (cmp == PLAN_COMP_GT)
	{
	  /*
	   * The new plan is better than the previous one in the i'th
	   * slot.  Remove the old one, and if we haven't yet retained
	   * the new one, put it in the freshly-available slot.  If we
	   * have already retained the plan, pull the last element down
	   * from the end of the planvec and stuff it in this slot, and
	   * then check this slot all over again.  Don't forget to NULL
	   * out the old last element.
	   */
	  if (already_retained)
	    {
	      planvec->nplans--;
	      qo_plan_del_ref (planvec->plan[i]);
	      planvec->plan[i] =
		(i < planvec->nplans) ? planvec->plan[planvec->nplans] : NULL;
	      planvec->plan[planvec->nplans] = NULL;
	      /*
	       * Back up `i' so that we examine this slot again.
	       */
	      i--;
	    }
	  else
	    {
	      (void) qo_plan_add_ref (plan);
	      qo_plan_del_ref (planvec->plan[i]);
	      planvec->plan[i] = plan;
	    }
	  already_retained = 1;
	}
      else if (cmp == PLAN_COMP_EQ)
	{
	  /* found equal cost plan already found
	   */
	  num_eq++;
	}
      else if (cmp == PLAN_COMP_LT)
	{
	  /*
	   * The new plan is worse than some plan that we already have.
	   * There is no point in checking any others; give up and get
	   * out.
	   */
	  return PLAN_COMP_LT;
	}			/* else { cmp == PLAN_COMP_UNK } */
    }

  /*
   * Ok, we've looked at all of the current plans.  It's possible to
   * get here and still not have retained the new plan if it couldn't
   * be determined whether it was definitely better or worse than any
   * of the plans we're already holding (or if we're not holding any).
   * Try to add it to the vector of plans.
   */
  if (!already_retained && !num_eq)
    {				/* all is PLAN_COMP_UNK */

      if (i < NPLANS)
	{
	  planvec->nplans++;
	  (void) qo_plan_add_ref (plan);
	  qo_plan_del_ref (planvec->plan[i]);
	  planvec->plan[i] = plan;
	  already_retained = 1;
	}
      else
	{
	  int best_vc_pid, best_tc_pid;
	  int worst_vc_pid, worst_tc_pid;
	  double vc, tc, p_vc, p_tc;
	  QO_PLAN *p;

	  /*
	   * We would like to keep this plan, but we're out of slots in
	   * the planvec.  For now, we just throw out one plan with the
	   * highest access cost.
	   */

	  /* STEP 1: found best plan */
	  best_vc_pid = best_tc_pid = -1;	/* init */

	  vc = plan->variable_cpu_cost + plan->variable_io_cost;
	  tc = plan->fixed_cpu_cost + plan->fixed_io_cost + vc;

	  for (i = 0; i < planvec->nplans; i++)
	    {
	      p = planvec->plan[i];
	      p_vc = p->variable_cpu_cost + p->variable_io_cost;
	      p_tc = p->fixed_cpu_cost + p->fixed_io_cost + p_vc;

	      if (p_vc < vc)
		{		/* found best variable cost plan */
		  best_vc_pid = i;
		  vc = p_vc;	/* save best variable cost */
		}

	      if (p_tc < tc)
		{		/* found best total cost plan */
		  best_tc_pid = i;
		  tc = p_tc;	/* save best total cost */
		}
	    }			/* for (i = 0; ...) */

	  /* STEP 2: found worst plan */
	  worst_vc_pid = worst_tc_pid = -1;	/* init */

	  vc = plan->variable_cpu_cost + plan->variable_io_cost;
	  tc = plan->fixed_cpu_cost + plan->fixed_io_cost + vc;

	  for (i = 0; i < planvec->nplans; i++)
	    {
	      p = planvec->plan[i];
	      p_vc = p->variable_cpu_cost + p->variable_io_cost;
	      p_tc = p->fixed_cpu_cost + p->fixed_io_cost + p_vc;

	      if (i != best_vc_pid && i != best_tc_pid)
		{
		  if (p_vc > vc)
		    {		/* found worst variable cost plan */
		      worst_vc_pid = i;
		      vc = p_vc;	/* save worst variable cost */
		    }

		  if (p_tc > tc)
		    {		/* found worst total cost plan */
		      worst_tc_pid = i;
		      tc = p_tc;	/* save worst total cost */
		    }
		}
	    }			/* for (i = 0; ...) */

	  if (worst_tc_pid != -1)
	    {			/* release worst total cost plan */
	      (void) qo_plan_add_ref (plan);
	      qo_plan_del_ref (planvec->plan[worst_tc_pid]);
	      planvec->plan[worst_tc_pid] = plan;
	      already_retained = 1;
	    }
	  else if (worst_vc_pid != -1)
	    {			/* release worst variable cost plan */
	      (void) qo_plan_add_ref (plan);
	      qo_plan_del_ref (planvec->plan[worst_vc_pid]);
	      planvec->plan[worst_vc_pid] = plan;
	      already_retained = 1;
	    }
	  else
	    {
	      /*
	       * The new plan is worse than some plan that we already have.
	       * There is no point in checking any others; give up and get
	       * out.
	       */
	      return PLAN_COMP_LT;
	    }
	}
    }

  if (already_retained)
    {
      return PLAN_COMP_GT;
    }
  else if (num_eq)
    {
      return PLAN_COMP_EQ;
    }

  return cmp;
}

/*
 * qo_cmp_planvec () -
 *   return: one of {PLAN_COMP_UNK, PLAN_COMP_LT, PLAN_COMP_EQ, PLAN_COMP_GT}
 *   planvec(in):
 *   plan(in):
 */
static QO_PLAN_COMPARE_RESULT
qo_cmp_planvec (QO_PLANVEC * planvec, QO_PLAN * plan)
{
  int i;
  QO_PLAN_COMPARE_RESULT cmp;

  cmp = PLAN_COMP_UNK;		/* init */

  for (i = 0; i < planvec->nplans; i++)
    {
      cmp = qo_plan_cmp (planvec->plan[i], plan);
      if (cmp != PLAN_COMP_UNK)
	{
	  return cmp;
	}
    }

  /* at here, all is PLAN_COMP_UNK */

  return cmp;
}

/*
 * qo_find_best_plan_on_planvec () -
 *   return:
 *   planvec(in):
 *   n(in):
 */
static QO_PLAN *
qo_find_best_plan_on_planvec (QO_PLANVEC * planvec, double n)
{
  int i;
  QO_PLAN *best_plan, *plan;
  double fixed, variable, best_cost, cost;

  /* While initializing the cost to QO_INFINITY and starting the loop
     at i = 0 might look equivalent to this, it actually loses if all
     of the elements in the vector have cost QO_INFINITY, because the
     comparison never succeeds and we never make plan non-NULL.  This
     is very bad for those callers above who believe that we're
     returning something useful. */

  best_plan = planvec->plan[0];
  fixed = best_plan->fixed_cpu_cost + best_plan->fixed_io_cost;
  variable = best_plan->variable_cpu_cost + best_plan->variable_io_cost;
  best_cost = fixed + (n * variable);
  for (i = 1; i < planvec->nplans; i++)
    {
      plan = planvec->plan[i];
      fixed = plan->fixed_cpu_cost + plan->fixed_io_cost;
      variable = plan->variable_cpu_cost + plan->variable_io_cost;
      cost = fixed + (n * variable);

      if (cost < best_cost)
	{
	  best_plan = plan;
	  best_cost = cost;
	}
    }

  return best_plan;
}				/* qo_find_best_plan_on_planvec() */


/*
 * An Info node is associated with a combination of join expressions, and
 * holds plans for producing the result described by that combination.
 * At any time, the node holds the best plans we have yet encountered for
 * producing the result in any of the interesting orders (including the
 * best way we know of when order is not a concern).  Of course, for many
 * join combinations, it will be impossible to produce a result in a
 * given order (i.e., when that order is based on an attribute of a
 * relation that doesn't participate in the particular combination).  In
 * these cases we simply record a NULL plan for that order.
 */

/*
 * qo_info_nodes_init () -
 *   return:
 *   env(in):
 */
static void
qo_info_nodes_init (QO_ENV * env)
{
  infos_allocated = 0;
  infos_deallocated = 0;
}

/*
 * qo_alloc_info () -
 *   return:
 *   planner(in):
 *   nodes(in):
 *   terms(in):
 *   eqclasses(in):
 *   cardinality(in):
 */
static QO_INFO *
qo_alloc_info (QO_PLANNER * planner,
	       BITSET * nodes,
	       BITSET * terms, BITSET * eqclasses, double cardinality)
{
  QO_INFO *info;
  int i;
  int EQ;

  info = ALLOCATE (planner->env, QO_INFO);
  i = 0;
  EQ = planner->EQ;

  info->env = planner->env;
  info->planner = planner;

  bitset_init (&(info->nodes), planner->env);
  bitset_init (&(info->terms), planner->env);
  bitset_init (&(info->eqclasses), planner->env);
  bitset_init (&(info->projected_segs), planner->env);

  bitset_assign (&info->nodes, nodes);
  bitset_assign (&info->terms, terms);
  bitset_assign (&info->eqclasses, eqclasses);
  qo_compute_projected_segs (planner, nodes, terms, &info->projected_segs);
  info->projected_size = qo_compute_projected_size (planner,
						    &info->projected_segs);
  info->cardinality = cardinality;

  qo_init_planvec (&info->best_no_order);
  /*
   * Set aside an array for ordered plans.  Each element of the array
   * holds a plan that is ordered according to the corresponding
   * equivalence class.
   *
   * If this NALLOCATE() fails, we'll lose the memory pointed to by
   * info.  I'll take the chance.
   */
  info->planvec = NALLOCATE (info->env, QO_PLANVEC, EQ);
  for (i = 0; i < EQ; ++i)
    {
      qo_init_planvec (&info->planvec[i]);
    }

  info->join_unit = planner->join_unit;	/* init */

  info->detached = false;

  infos_allocated++;

  /* insert into the head of alloced info list
   */
  info->next = planner->info_list;
  planner->info_list = info;

  return info;
}

/*
 * qo_free_info () -
 *   return:
 *   info(in):
 */
static void
qo_free_info (QO_INFO * info)
{
  if (info == NULL)
    return;

  qo_detach_info (info);

  bitset_delset (&(info->nodes));
  bitset_delset (&(info->terms));
  bitset_delset (&(info->eqclasses));
  bitset_delset (&(info->projected_segs));

  DEALLOCATE (info->env, info);

  infos_deallocated++;
}

/*
 * qo_detach_info () -
 *   return:
 *   info(in):
 */
static void
qo_detach_info (QO_INFO * info)
{
  /*
   * If the node hasn't already been detached, detach it now and give
   * up references to all plans that are no longer needed.
   */
  if (!info->detached)
    {
      int i;
      int EQ = info->planner->EQ;

      for (i = 0; i < EQ; ++i)
	{
	  qo_uninit_planvec (&info->planvec[i]);
	}
      DEALLOCATE (info->env, info->planvec);
      info->planvec = NULL;
      info->detached = true;

      qo_uninit_planvec (&info->best_no_order);
    }
}

/*
 * qo_check_new_best_plan_on_info () -
 *   return:
 *   info(in):
 *   plan(in):
 */
static bool
qo_check_new_best_plan_on_info (QO_INFO * info, QO_PLAN * plan)
{
  /*
   * Check the incoming plan to see if it is the best we have seen
   * (without regard to order) for this info node.  If it is, record it
   * in best_no_order, and then see if it can be used to fabricate
   * better plans for any of the interesting orders.
   *
   * If the plan is a winner, its refcount should be incremented.  If
   * it is not, it should be released.
   *
   * This routine returns 1 is the plan is retained, and 0 if not.
   */
  QO_PLAN_COMPARE_RESULT cmp;
  QO_EQCLASS *order;

  order = plan->order;
  if (order && bitset_is_empty (&(QO_EQCLASS_SEGS (order))))
    {
      /* Then this "equivalence class" is a phony fabricated
       * especially for a complex merge term.
       *
       * skip out
       */
      cmp = PLAN_COMP_LT;
    }
  else
    {
      cmp = qo_check_planvec (&info->best_no_order, plan);

      if (cmp == PLAN_COMP_GT)
	{
	  if (plan->plan_type != QO_PLANTYPE_SORT)
	    {
	      int i, EQ;
	      QO_PLAN *new_plan;
	      QO_PLAN_COMPARE_RESULT new_cmp;

	      EQ = info->planner->EQ;

	      /*
	       * Check to see if any of the ordered solutions can be made
	       * cheaper by sorting this new plan.
	       */
	      for (i = 0; i < EQ; i++)
		{
		  /*
		   * Implementation note: this could probably be done more
		   * cheaply by calculating the sorted cost first (without
		   * building a new Plan), and then building the new Plan if
		   * it's a win.  However, superficial investigation indicates
		   * that this isn't happening as often as I originally
		   * believed, and the change would cause just enough bother to
		   * keep me from pursuing it for now.
		   *
		   */
		  order = &info->planner->eqclass[i];

		  new_plan =
		    qo_plan_order_by (qo_find_best_plan_on_planvec
				      (&info->best_no_order, 1.0), order);
		  if (new_plan)
		    {
		      new_cmp =
			qo_check_planvec (&info->planvec[i], new_plan);
		      if (new_cmp == PLAN_COMP_LT || new_cmp == PLAN_COMP_EQ)
			{
			  qo_plan_release (new_plan);
			}
		    }
		}		/* for (i = 0; ...) */
	    }
	}			/* if (cmp == PLAN_COMP_GT) */
    }

  if (cmp == PLAN_COMP_LT || cmp == PLAN_COMP_EQ)
    {
      qo_plan_release (plan);
      return false;
    }

  return true;
}

/*
 * qo_check_plan_on_info () -
 *   return:
 *   info(in):
 *   plan(in):
 */
static int
qo_check_plan_on_info (QO_INFO * info, QO_PLAN * plan)
{
  /*
   * Try to add a new plan to this node.  There is no guarantee that
   * the plan is actually better than any we have seen so far; it is up
   * to this routine to determine whether it is.
   *
   * Callers of this routine do not adjust the refcount of the incoming
   * plan, so if it is retained its refcount should be bumped up.  On
   * the other hand, if it is not retained, its refcount need not be
   * decremented, but the plan should be released.  Make sure that all
   * paths through the code obey this protocol, or you will lose
   * storage.
   *
   * This routine returns 1 if the plan is retained (i.e., it is an
   * improvement on some previsou plan), and 0 is the plan is not
   * retained.
   */
  QO_INFO *best_info = info->planner->best_info;
  QO_EQCLASS *plan_order = plan->order;
  QO_PLAN_COMPARE_RESULT cmp;
  bool found_new_best;

  found_new_best = false;	/* init */

  /*
   * If the cost of the new Plan already exceeds the cost of the best
   * known solution with the same order, there is no point in
   * remembering the new plan.
   */
  if (best_info)
    {
      cmp = qo_cmp_planvec (plan_order == QO_UNORDERED
			    ? &best_info->best_no_order
			    : &best_info->
			    planvec[QO_EQCLASS_IDX (plan_order)], plan);
      /* cmp : PLAN_COMP_UNK, PLAN_COMP_LT, PLAN_COMP_EQ, PLAN_COMP_GT */
      if (cmp == PLAN_COMP_LT || cmp == PLAN_COMP_EQ)
	{
	  qo_plan_release (plan);
	  return 0;
	}
    }

  /*
   * The only time we will keep an unordered plan is if it is cheaper
   * than any other plan we have seen so far (ordered or unordered).
   * Only ordered plans are kept in the _plan vector.
   */
  if (plan_order == QO_UNORDERED)
    {
      found_new_best = qo_check_new_best_plan_on_info (info, plan);
    }
  else
    {
      /*
       * If we get here, we are dealing with an ordered plan.  Check
       * whether we already have memo-ized a plan for this particular scan
       * order.  If so, see if this new plan is an improvement.
       */

      cmp =
	qo_check_planvec (&info->planvec[QO_EQCLASS_IDX (plan_order)], plan);
      if (cmp == PLAN_COMP_GT)
	{
	  (void) qo_check_new_best_plan_on_info (info, plan);
	  found_new_best = true;
	}
      else
	{
	  qo_plan_release (plan);
	  return 0;
	}
    }

  if (found_new_best != true)
    {
      return 0;
    }

  /* save the last join level; used for cache */
  info->join_unit = info->planner->join_unit;

  return 1;
}

/*
 * qo_find_best_nljoin_inner_plan_on_info () -
 *   return:
 *   outer(in):
 *   info(in):
 *   join_type(in):
 */
static QO_PLAN *
qo_find_best_nljoin_inner_plan_on_info (QO_PLAN * outer, QO_INFO * info,
					JOIN_TYPE join_type)
{
  QO_PLANVEC *pv;
  double best_cost = -1.0, temp_cost;
  QO_PLAN *temp, *best_plan;
  int i;

  best_plan = NULL;		/* init */

  /* alloc temporary nl-join plan */
  temp = qo_plan_malloc (info->env);

  temp->info = info;
  temp->refcount = 0;
  temp->top_rooted = false;
  temp->well_rooted = false;
  temp->iscan_sort_list = NULL;

  temp->plan_un.join.join_type = join_type;	/* set nl-join type */
  temp->plan_un.join.outer = outer;	/* set outer */

  for (i = 0, pv = &info->best_no_order; i < pv->nplans; i++)
    {
      temp->plan_un.join.inner = pv->plan[i];	/* set inner */
      qo_nljoin_cost (temp);
      temp_cost = temp->fixed_cpu_cost +
	temp->fixed_io_cost +
	temp->variable_cpu_cost + temp->variable_io_cost;
      if ((best_cost == -1) || (temp_cost < best_cost))
	{
	  best_cost = temp_cost;
	  best_plan = pv->plan[i];
	}
    }				/* for */

  /* free temp plan */
  temp->plan_un.join.outer = NULL;
  temp->plan_un.join.inner = NULL;

  qo_plan_add_to_free_list (temp, NULL);

  return best_plan;
}

/*
 * qo_find_best_plan_on_info () -
 *   return:
 *   info(in):
 *   order(in):
 *   n(in):
 */
static QO_PLAN *
qo_find_best_plan_on_info (QO_INFO * info, QO_EQCLASS * order, double n)
{
  /*
   * Return the best plan we have for a particular ordering and a
   * particular number of accesses.
   *
   * If the requestor doesn't care about an ordering, or if it does
   * care and the ordering is the same as that of the cheapest plan we
   * have found so far, return that plan.
   *
   * If we've never made a plan for this ordering (it can happen
   * because of the funny orderings that we supply for complex merge
   * terms), make one now by using the best subplan we know and sorting
   * it.
   */
  QO_PLANVEC *pv;

  if (order == QO_UNORDERED)
    {
      pv = &info->best_no_order;
    }
  else
    {
      int order_idx = QO_EQCLASS_IDX (order);
      if (info->planvec[order_idx].nplans == 0)
	{
	  QO_PLAN *planp;

	  planp =
	    qo_sort_new (qo_find_best_plan_on_planvec
			 (&info->best_no_order, n), order, SORT_TEMP);

	  qo_check_planvec (&info->planvec[order_idx], planp);
	}
      pv = &info->planvec[order_idx];
    }

  return qo_find_best_plan_on_planvec (pv, n);
}

/*
 * examine_idx_join () -
 *   return:
 *   info(in):
 *   join_type(in):
 *   outer(in):
 *   inner(in):
 *   afj_terms(in):
 *   sarged_terms(in):
 *   pinned_subqueries(in):
 */
static int
examine_idx_join (QO_INFO * info,
		  JOIN_TYPE join_type,
		  QO_INFO * outer,
		  QO_INFO * inner,
		  BITSET * afj_terms,
		  BITSET * sarged_terms, BITSET * pinned_subqueries)
{
  int n = 0;
  QO_NODE *inner_node;

  /* check for right outer join;
   */
  if (join_type == JOIN_RIGHT)
    {
      if (bitset_cardinality (&(outer->nodes)) != 1)
	{			/* not single class spec */
	  /* inner of correlated index join should be plain class access */
	  goto exit;
	}

      inner_node =
	QO_ENV_NODE (outer->env, bitset_first_member (&(outer->nodes)));
      if (QO_NODE_HINT (inner_node) & PT_HINT_ORDERED)
	{
	  /* join hint: force join left-to-right; skip idx-join
	   * because, these are only support left outer join
	   */
	  goto exit;
	}
    }
  else
    {
      inner_node =
	QO_ENV_NODE (inner->env, bitset_first_member (&(inner->nodes)));
    }

  /* inner is single class spec */
  if (QO_NODE_HINT (inner_node) & (PT_HINT_USE_IDX | PT_HINT_USE_NL))
    {
      /* join hint: force idx-join
       */
    }
  else if (QO_NODE_HINT (inner_node) & PT_HINT_USE_MERGE)
    {
      /* join hint: force merge-join; skip idx-join
       */
      goto exit;
    }

  /* check whether we can build a nested loop join with a
     correlated index scan. That is, is the inner term a scan of a
     single node, and can this join term be used as an index with
     respect to that node? If so, we can build a special kind of plan
     to exploit that. */
  if (join_type == JOIN_RIGHT)
    {
      /* if right outer join, select outer plan from the inner node
         and inner plan from the outer node, and do left outer join */
      n = qo_examine_correlated_index (info,
				       JOIN_LEFT,
				       inner,
				       outer,
				       afj_terms,
				       sarged_terms, pinned_subqueries);
    }
  else
    {
      n = qo_examine_correlated_index (info,
				       join_type,
				       outer,
				       inner,
				       afj_terms,
				       sarged_terms, pinned_subqueries);
    }

exit:

  return n;
}

/*
 * examine_nl_join () -
 *   return:
 *   info(in):
 *   join_type(in):
 *   outer(in):
 *   inner(in):
 *   nl_join_terms(in):
 *   duj_terms(in):
 *   afj_terms(in):
 *   sarged_terms(in):
 *   pinned_subqueries(in):
 */
static int
examine_nl_join (QO_INFO * info,
		 JOIN_TYPE join_type,
		 QO_INFO * outer,
		 QO_INFO * inner,
		 BITSET * nl_join_terms,
		 BITSET * duj_terms,
		 BITSET * afj_terms,
		 BITSET * sarged_terms, BITSET * pinned_subqueries)
{
  int n = 0;
  QO_PLAN *outer_plan, *inner_plan;
  QO_NODE *inner_node;

  if (join_type == JOIN_RIGHT)
    {
      /* converse outer join type
       */
      join_type = JOIN_LEFT;

      if (bitset_intersects (sarged_terms, &(info->env->fake_terms)))
	{
	  goto exit;
	}
      {
	int t;
	QO_TERM *term;
	BITSET_ITERATOR iter;

	for (t = bitset_iterate (nl_join_terms, &iter); t != -1;
	     t = bitset_next_member (&iter))
	  {
	    term = QO_ENV_TERM (info->env, t);
	    if (QO_TERM_CLASS (term) == QO_TC_DEP_LINK)
	      {
		goto exit;
	      }
	  }			/* for (t = ...) */
      }

      if (bitset_cardinality (&(outer->nodes)) == 1)
	{			/* single class spec */
	  inner_node = QO_ENV_NODE (outer->env,
				    bitset_first_member (&(outer->nodes)));
	  if (QO_NODE_HINT (inner_node) & PT_HINT_ORDERED)
	    {
	      /* join hint: force join left-to-right; skip idx-join
	       * because, these are only support left outer join
	       */
	      goto exit;
	    }

	  if (QO_NODE_HINT (inner_node) & PT_HINT_USE_NL)
	    {
	      /* join hint: force nl-join
	       */
	    }
	  else
	    if (QO_NODE_HINT (inner_node) &
		(PT_HINT_USE_IDX | PT_HINT_USE_MERGE))
	    {
	      /* join hint: force idx-join or merge-join; skip nl-join
	       */
	      goto exit;
	    }
	}

      if ((outer_plan =
	   qo_find_best_plan_on_info (inner, QO_UNORDERED, 1.0)) == NULL)
	{
	  goto exit;
	}
      if ((inner_plan =
	   qo_find_best_nljoin_inner_plan_on_info (outer_plan, outer,
						   join_type)) == NULL)
	{
	  goto exit;
	}
    }
  else
    {
      /* At here, inner is single class spec */
      inner_node = QO_ENV_NODE (inner->env,
				bitset_first_member (&(inner->nodes)));
      if (QO_NODE_HINT (inner_node) & PT_HINT_USE_NL)
	{
	  /* join hint: force nl-join
	   */
	}
      else
	if (QO_NODE_HINT (inner_node) & (PT_HINT_USE_IDX | PT_HINT_USE_MERGE))
	{
	  /* join hint: force idx-join or merge-join; skip nl-join
	   */
	  goto exit;
	}

      if ((outer_plan =
	   qo_find_best_plan_on_info (outer, QO_UNORDERED, 1.0)) == NULL)
	{
	  goto exit;
	}
      if ((inner_plan =
	   qo_find_best_nljoin_inner_plan_on_info (outer_plan, inner,
						   join_type)) == NULL)
	{
	  goto exit;
	}
    }

#if 0				/* CHAINS_ONLY */
  /* If CHAINS_ONLY is defined, we want the optimizer constrained to
     produce only left-linear trees of joins, i.e., no inner term can
     itself be a join or a follow. */

  if (inner_plan->plan_type != QO_PLANTYPE_SCAN)
    {
      if (inner_plan->plan_type == QO_PLANTYPE_SORT &&
	  inner_plan->order == QO_UNORDERED)
	{
	  /* inner has temporary list file plan; it's ok */
	  ;
	}
      else
	{
	  goto exit;
	}
    }
#endif /* CHAINS_ONLY */

#if 0				/* JOIN_FOLLOW_RESTRICTION */
  /* Under this restriction, we are not permitted to produce plans that
     have follow nodes sandwiched between joins.  Don't ask why. */

  if (outer_plan->plan_type == QO_PLANTYPE_FOLLOW &&
      QO_PLAN_SUBJOINS (outer_plan))
    {
      goto exit;
    }
  if (inner_plan->plan_type == QO_PLANTYPE_FOLLOW &&
      QO_PLAN_SUBJOINS (inner_plan))
    {
      goto exit;
    }
#endif /* JOIN_FOLLOW_RESTRICTION */

  /* look for the best nested loop solution we can find.  Since
     the subnodes are already keeping track of the lowest-cost plan
     they have seen, we needn't do any search here to find the cheapest
     nested loop join we can produce for this combination. */
  n = qo_check_plan_on_info (info,
			     qo_join_new (info,
					  join_type,
					  QO_JOINMETHOD_NL_JOIN,
					  outer_plan,
					  inner_plan,
					  nl_join_terms,
					  duj_terms,
					  afj_terms,
					  sarged_terms, pinned_subqueries));

exit:

  return n;
}

/*
 * examine_merge_join () -
 *   return:
 *   info(in):
 *   join_type(in):
 *   outer(in):
 *   inner(in):
 *   sm_join_terms(in):
 *   duj_terms(in):
 *   afj_terms(in):
 *   sarged_terms(in):
 *   pinned_subqueries(in):
 */
static int
examine_merge_join (QO_INFO * info,
		    JOIN_TYPE join_type,
		    QO_INFO * outer,
		    QO_INFO * inner,
		    BITSET * sm_join_terms,
		    BITSET * duj_terms,
		    BITSET * afj_terms,
		    BITSET * sarged_terms, BITSET * pinned_subqueries)
{
  int n = 0;
  QO_PLAN *outer_plan, *inner_plan;
  QO_NODE *inner_node;
  QO_EQCLASS *order = QO_UNORDERED;
  int t;
  BITSET_ITERATOR iter;
  QO_TERM *term;

  /* If any of the sarged terms are fake terms, we can't implement this
   * join as a merge join, because the timing assumptions required by
   * the fake terms won't be satisfied.  Nested loops are the only
   * joins that will work.
   */
  if (bitset_intersects (sarged_terms, &(info->env->fake_terms)))
    {
      goto exit;
    }

  /* examine ways of producing ordered results.  For each
     ordering, check whether the inner and outer subresults can be
     produced in that order.  If so, check a merge join plan on that
     order. */
  for (t = bitset_iterate (sm_join_terms, &iter); t != -1;
       t = bitset_next_member (&iter))
    {
      term = QO_ENV_TERM (info->env, t);
      order = QO_TERM_EQCLASS (term);
      if (order != QO_UNORDERED)
	{
	  break;
	}
    }				/* for (t = ...) */
  if (order == QO_UNORDERED)
    {
      goto exit;
    }

#ifdef OUTER_MERGE_JOIN_RESTRICTION
  if (IS_OUTER_JOIN_TYPE (join_type))
    {
      int node_idx;

      term = QO_ENV_TERM (info->env, bitset_first_member (sm_join_terms));
      node_idx = (join_type == JOIN_LEFT) ? QO_NODE_IDX (QO_TERM_HEAD (term))
	: QO_NODE_IDX (QO_TERM_TAIL (term));
      for (t = bitset_iterate (duj_terms, &iter); t != -1;
	   t = bitset_next_member (&iter))
	{
	  term = QO_ENV_TERM (info->env, t);
	  if (!BITSET_MEMBER (QO_TERM_NODES (term), node_idx))
	    {
	      goto exit;
	    }
	}			/* for (t = ...) */
    }
#endif /* OUTER_MERGE_JOIN_RESTRICTION */

#if 1				/* RIGHT_OUTER_MERGE_JOIN */
  /* At here, inner is single class spec */
  inner_node =
    QO_ENV_NODE (inner->env, bitset_first_member (&(inner->nodes)));

  if (QO_NODE_HINT (inner_node) & PT_HINT_USE_MERGE)
    {
      /* join hint: force m-join;
       */
    }
  else if (QO_NODE_HINT (inner_node) & (PT_HINT_USE_NL | PT_HINT_USE_IDX))
    {
      /* join hint: force nl-join, idx-join;
       */
      goto exit;
    }

  if ((outer_plan = qo_find_best_plan_on_info (outer, order, 1.0)) == NULL)
    {
      goto exit;
    }
  if ((inner_plan = qo_find_best_plan_on_info (inner, order, 1.0)) == NULL)
    {
      goto exit;
    }
#else /* RIGHT_OUTER_MERGE_JOIN */
  /* if right outer join, select outer plan from the inner node
     and inner plan from the outer node */
  if (join_type == JOIN_RIGHT)
    {
      /* converse outer join type
       */
      join_type = JOIN_LEFT;

      if (bitset_cardinality (&(outer->nodes)) == 1)
	{			/* single class spec */
	  inner_node = QO_ENV_NODE (outer->env,
				    bitset_first_member (&(outer->nodes)));
	  if (QO_NODE_HINT (inner_node) & PT_HINT_ORDERED)
	    {
	      /* join hint: force join left-to-right; fail
	       */
	      goto exit;
	    }


	  if (QO_NODE_HINT (inner_node) & PT_HINT_USE_MERGE)
	    {
	      /* join hint: force m-join;
	       */
	    }
	  else
	    if (QO_NODE_HINT (inner_node) &
		(PT_HINT_USE_NL | PT_HINT_USE_IDX))
	    {
	      /* join hint: force nl-join, idx-join;
	       */
	      goto exit;
	    }

	}

      if ((outer_plan =
	   qo_find_best_plan_on_info (inner, order, 1.0)) == NULL)
	{
	  goto exit;
	}
      if ((inner_plan =
	   qo_find_best_plan_on_info (outer, order, 1.0)) == NULL)
	{
	  goto exit;
	}
    }
  else
    {
      /* At here, inner is single class spec */
      inner_node = QO_ENV_NODE (inner->env,
				bitset_first_member (&(inner->nodes)));

      if (QO_NODE_HINT (inner_node) & PT_HINT_USE_MERGE)
	{
	  /* join hint: force m-join;
	   */
	}
      else if (QO_NODE_HINT (inner_node) & (PT_HINT_USE_NL | PT_HINT_USE_IDX))
	{
	  /* join hint: force nl-join, idx-join;
	   */
	  goto exit;
	}

      if ((outer_plan =
	   qo_find_best_plan_on_info (outer, order, 1.0)) == NULL)
	{
	  goto exit;
	}
      if ((inner_plan =
	   qo_find_best_plan_on_info (inner, order, 1.0)) == NULL)
	{
	  goto exit;
	}
    }
#endif /* RIGHT_OUTER_MERGE_JOIN */

#ifdef CHAINS_ONLY
  /* If CHAINS_ONLY is defined, we want the optimizer constrained to
     produce only left-linear trees of joins, i.e., no inner term can
     itself be a join or a follow. */

  if (inner_plan->plan_type != QO_PLANTYPE_SCAN)
    {
      if (inner_plan->plan_type == QO_PLANTYPE_SORT
	  && inner_plan->order == QO_UNORDERED)
	{
	  /* inner has temporary list file plan; it's ok */
	  ;
	}
      else
	{
	  goto exit;
	}
    }
#endif /* CHAINS_ONLY */

#if 0				/* JOIN_FOLLOW_RESTRICTION */
  /* Under this restriction, we are not permitted to produce plans that
     have follow nodes sandwiched between joins.  Don't ask why. */

  if (outer_plan->plan_type == QO_PLANTYPE_FOLLOW
      && QO_PLAN_SUBJOINS (outer_plan))
    {
      goto exit;
    }
  if (inner_plan->plan_type == QO_PLANTYPE_FOLLOW
      && QO_PLAN_SUBJOINS (inner_plan))
    {
      goto exit;
    }
#endif /* JOIN_FOLLOW_RESTRICTION */

  n = qo_check_plan_on_info (info,
			     qo_join_new (info,
					  join_type,
					  QO_JOINMETHOD_MERGE_JOIN,
					  outer_plan,
					  inner_plan,
					  sm_join_terms,
					  duj_terms,
					  afj_terms,
					  sarged_terms, pinned_subqueries));

exit:

  return n;
}

/*
 * qo_examine_correlated_index () -
 *   return: int
 *   info(in): The info node to be corresponding to the join being investigated
 *   join_type(in):
 *   outer(in): The info node for the outer join operand
 *   inner(in): The info node for the inner join operand
 *   afj_terms(in): The term being used to join the operands
 *   sarged_terms(in): The residual terms to be evaluated at this level of
 *		       the plan tree
 *   pinned_subqueries(in): The subqueries to be pinned to plans at this node
 *
 * Note: Check whether we can build a nested loop join that implements
 *		the join term as a correlated index on the inner term.
 *		Operationally, such a join looks something like
 *
 *			for (every record in the outer term)
 *			{
 *			    parameterize join term with values
 *				from outer record;
 *			    do index scan on inner node;
 *			    for (every record in inner scan)
 *			    {
 *				join outer and inner records;
 *			    }
 *			}
 */
static int
qo_examine_correlated_index (QO_INFO * info,
			     JOIN_TYPE join_type,
			     QO_INFO * outer,
			     QO_INFO * inner,
			     BITSET * afj_terms,
			     BITSET * sarged_terms,
			     BITSET * pinned_subqueries)
{
  QO_NODE *nodep;
  QO_NODE_INDEX *node_indexp;
  QO_NODE_INDEX_ENTRY *ni_entryp;
  QO_INDEX_ENTRY *index_entryp;
  QO_PLAN *outer_plan;
  int i, n = 0;
  BITSET_ITERATOR iter;
  int t;
  QO_TERM *termp;
  int num_only_args;
  BITSET indexable_terms;

  /* outer plan */
  if ((outer_plan =
       qo_find_best_plan_on_info (outer, QO_UNORDERED, 1.0)) == NULL)
    {
      return 0;
    }

#if 0				/* JOIN_FOLLOW_RESTRICTION */
  /* Under this restriction, we are not permitted to produce plans that
     have follow nodes sandwiched between joins.  Don't ask why. */

  if (outer_plan->plan_type == QO_PLANTYPE_FOLLOW
      && QO_PLAN_SUBJOINS (outer_plan))
    {
      return 0;
    }
#endif /* JOIN_FOLLOW_RESTRICTION */

  /* inner node and its indexes */
  nodep = &info->planner->node[bitset_first_member (&(inner->nodes))];
  node_indexp = QO_NODE_INDEXES (nodep);
  if (!node_indexp)
    {
      /* inner dose not have any usable index */
      return 0;
    }

  bitset_init (&indexable_terms, info->env);

  /* We're interested in all of the terms so combine 'join_term' and
     'sarged_terms' together. */
  if (IS_OUTER_JOIN_TYPE (join_type))
    {
      for (t = bitset_iterate (sarged_terms, &iter); t != -1;
	   t = bitset_next_member (&iter))
	{

	  termp = QO_ENV_TERM (QO_NODE_ENV (nodep), t);

	  if (QO_TERM_CLASS (termp) == QO_TC_AFTER_JOIN)
	    {
	      /* exclude after-join term in 'sarged_terms' */
	      continue;
	    }

	  if (QO_TERM_LOCATION (termp) == 0)
	    {			/* WHERE clause */
	      if (QO_TERM_CLASS (termp) == QO_TC_SARG
		  || QO_TERM_CLASS (termp) == QO_TC_DURING_JOIN)
		{
		  /* include sarg term, during join term */
		}
	      else
		{
		  /* exclude after-joinable terms in 'sarged_terms' */
		  continue;
		}
	    }

	  bitset_add (&indexable_terms, t);
	}			/* for (t = ...) */
    }
  else
    {
      bitset_union (&indexable_terms, sarged_terms);
    }

  /* finally, combine inner plan's 'sarg term' together */
  bitset_union (&indexable_terms, &(QO_NODE_SARGS (nodep)));

  num_only_args = 0;		/* init */

  /* Iterate through the indexes attached to this node and look for ones
     which are a subset of the terms that we're interested in. For each
     applicable index, register a plans and compute the cost. */
  for (i = 0; i < QO_NI_N (node_indexp); i++)
    {
      /* pointer to QO_NODE_INDEX_ENTRY structure */
      ni_entryp = QO_NI_ENTRY (node_indexp, i);
      /* pointer to QO_INDEX_ENTRY structure */
      index_entryp = (ni_entryp)->head;

      /* the index has terms which are a subset of the terms that we're
         intersted in */
      if (bitset_intersects (&indexable_terms, &(index_entryp->terms)))
	{

	  if (!bitset_intersects (sarged_terms, &(index_entryp->terms)))
	    {
	      /* there is not join-edge, only inner sargs */
	      num_only_args++;
	      continue;
	    }

	  /* generate join index scan using 'ni_entryp' */
	  n += qo_generate_join_index_scan (info, join_type, outer_plan,
					    inner, nodep, ni_entryp,
					    &indexable_terms,
					    afj_terms,
					    sarged_terms, pinned_subqueries);
	}
    }				/* for (i = 0; i < QO_NI_N(node_indexp); i++) */

  if (QO_NODE_HINT (nodep) & PT_HINT_USE_IDX)
    {
      /* join hint: force idx-join
       */
      if (n == 0 && num_only_args)
	{			/* not found 'idx-join' plan */
	  /* Re-Iterate */
	  for (i = 0; i < QO_NI_N (node_indexp); i++)
	    {
	      /* pointer to QO_NODE_INDEX_ENTRY structure */
	      ni_entryp = QO_NI_ENTRY (node_indexp, i);
	      /* pointer to QO_INDEX_ENTRY structure */
	      index_entryp = (ni_entryp)->head;

	      /* the index has terms which are a subset of the terms that
	         we're intersted in */
	      if (bitset_intersects
		  (&indexable_terms, &(index_entryp->terms)))
		{
		  if (bitset_intersects
		      (sarged_terms, &(index_entryp->terms)))
		    {
		      /* there is join-edge; already examined */
		      continue;
		    }

		  /* generate join index scan using 'ni_entryp' */
		  n += qo_generate_join_index_scan (info, join_type,
						    outer_plan, inner,
						    nodep, ni_entryp,
						    &indexable_terms,
						    afj_terms,
						    sarged_terms,
						    pinned_subqueries);
		}
	    }			/* for (i = 0; i < QO_NI_N(node_indexp); i++) */
	}
    }				/* if (QO_NODE_HINT(nodep) & PT_HINT_USE_IDX) */

  bitset_delset (&indexable_terms);

  return n;
}				/* qo_examine_correlated_index() */


/*
 * qo_examine_follow () -
 *   return:
 *   info(in):
 *   path_term(in):
 *   head_info(in):
 *   sarged_terms(in):
 *   pinned_subqueries(in):
 */
static int
qo_examine_follow (QO_INFO * info,
		   QO_TERM * path_term,
		   QO_INFO * head_info,
		   BITSET * sarged_terms, BITSET * pinned_subqueries)
{
  PT_NODE *entity_spec;
  /*
   * Examine the feasibility of a follow plan implementation for this
   * edge.  Don't build follow plans if the tail of the path is an rdb
   * proxy; these things *have* to be implemented via joins.
   */
  entity_spec = path_term->tail->entity_spec;
  if (entity_spec->info.spec.flat_entity_list == NULL)
    return 0;

  return qo_check_plan_on_info (info,
				qo_follow_new (info,
					       qo_find_best_plan_on_info
					       (head_info, QO_UNORDERED, 1.0),
					       path_term, sarged_terms,
					       pinned_subqueries));

}

/*
 * qo_compute_projected_segs () -
 *   return:
 *   planner(in):
 *   nodes(in):
 *   terms(in):
 *   projected(in):
 */
static void
qo_compute_projected_segs (QO_PLANNER * planner,
			   BITSET * nodes, BITSET * terms, BITSET * projected)
{
  /*
   * Figure out which of the attributes of the nodes joined by the
   * terms in 'terms' need to be projected out of the join in order to
   * satisfy the needs of higher-level plans.  An attribute will need
   * to preserved if it is to be produced as part of the final result
   * or if it is needed to compute some term that isn't included in
   * 'terms'.
   */

  BITSET required;
  int i;
  QO_TERM *term;

  BITSET_CLEAR (*projected);
  bitset_init (&required, planner->env);
  bitset_assign (&required, &(planner->final_segs));

  for (i = 0; i < (signed) planner->T; i++)
    {
      if (!BITSET_MEMBER (*terms, i))
	{
	  term = &planner->term[i];
	  bitset_union (&required, &(QO_TERM_SEGS (term)));
	}

    }				/* for (i = 0; i < planner->T; i++) */

  for (i = 0; i < (signed) planner->N; ++i)
    {
      if (BITSET_MEMBER (*nodes, i))
	bitset_union (projected, &(QO_NODE_SEGS (&planner->node[i])));
    }

  bitset_intersect (projected, &required);
  bitset_delset (&required);
}

/*
 * qo_compute_projected_size () -
 *   return:
 *   planner(in):
 *   segset(in):
 */
static int
qo_compute_projected_size (QO_PLANNER * planner, BITSET * segset)
{
  BITSET_ITERATOR si;
  int i;
  int size;

  /*
   * 8 bytes overhead per record.
   */
  size = 8;

  for (i = bitset_iterate (segset, &si); i != -1;
       i = bitset_next_member (&si))
    {
      /*
       * Four bytes overhead for each field.
       */
      size += qo_seg_width (QO_ENV_SEG (planner->env, i)) + 4;
    }

  return size;
}

/*
 * qo_dump_info () -
 *   return:
 *   info(in):
 *   f(in):
 */
static void
qo_dump_info (QO_INFO * info, FILE * f)
{
  /*
   * Dump the contents of this node for debugging scrutiny.
   */

  int i;

  fputs ("  projected segments: ", f);
  bitset_print (&(info->projected_segs),
		(int (*)(void *, char *, int)) fprintf, f);
  fputs ("\n", f);

  fputs ("  best: ", f);
  if (info->best_no_order.nplans > 0)
    {
      qo_dump_planvec (&info->best_no_order, f, -8);
    }
  else
    {
      fputs ("(empty)\n\n", f);
    }

  if (info->planvec)
    {
      for (i = 0; i < (signed) info->planner->EQ; ++i)
	{
	  if (info->planvec[i].nplans > 0)
	    {
	      char buf[20];

	      sprintf (buf, "[%d]: ", i);
	      fprintf (f, "%8s", buf);
	      qo_dump_planvec (&info->planvec[i], f, -8);
	    }
	}
    }
}

/*
 * qo_info_stats () -
 *   return:
 *   f(in):
 */
void
qo_info_stats (FILE * f)
{
  fprintf (f, "%d/%d info nodes allocated/deallocated\n",
	   infos_allocated, infos_deallocated);
}

/*
 * qo_alloc_planner () -
 *   return:
 *   env(in):
 */
static QO_PLANNER *
qo_alloc_planner (QO_ENV * env)
{
  int i;
  QO_PLANNER *planner;

  planner = ALLOCATE (env, QO_PLANNER);

  env->planner = planner;

  planner->env = env;
  planner->node = env->nodes;
  planner->N = env->nnodes;
  planner->E = env->nedges;
  planner->M = 0;		/* later, set in qo_search_planner() */
  if (planner->N < 32)
    {
      planner->node_mask =
	(unsigned long) ((unsigned int) (1 << planner->N) - 1);
    }
  else
    {
      planner->node_mask = (unsigned long) DB_UINT32_MAX;
    }
  planner->join_unit = 0;
  planner->term = env->terms;
  planner->T = env->nterms;
  planner->segment = env->segs;
  planner->S = env->nsegs;
  planner->eqclass = env->eqclasses;
  planner->EQ = env->neqclasses;
  planner->subqueries = env->subqueries;
  planner->Q = env->nsubqueries;
  planner->P = env->npartitions;
  planner->partition = env->partitions;

  bitset_init (&(planner->final_segs), env);
  bitset_assign (&(planner->final_segs), &(env->final_segs));
  bitset_init (&(planner->all_subqueries), env);
  for (i = 0; i < (signed) planner->Q; ++i)
    bitset_add (&(planner->all_subqueries), i);

  planner->node_info = NULL;
  planner->join_info = NULL;
  planner->best_info = NULL;
  planner->cp_info = NULL;

  planner->info_list = NULL;

  planner->cleanup_needed = true;

  return planner;
}

/*
 * qo_planner_free () -
 *   return:
 *   planner(in):
 */
void
qo_planner_free (QO_PLANNER * planner)
{
  if (planner->cleanup_needed)
    qo_clean_planner (planner);

  qo_plan_del_ref (planner->worst_plan);

  if (planner->info_list)
    {
      QO_INFO *info, *next_info;

      for (info = planner->info_list; info; info = next_info)
	{
	  next_info = info->next;	/* save next link */
	  qo_free_info (info);
	}
    }

  if (planner->node_info)
    {
      DEALLOCATE (planner->env, planner->node_info);
    }

  if (planner->join_info)
    {
      DEALLOCATE (planner->env, planner->join_info);
    }

  if (planner->cp_info)
    {
      DEALLOCATE (planner->env, planner->cp_info);
    }

  DEALLOCATE (planner->env, planner);
}

/*
 * qo_dump_planner_info () -
 *   return:
 *   planner(in):
 *   partition(in):
 *   f(in):
 */
static void
qo_dump_planner_info (QO_PLANNER * planner, QO_PARTITION * partition,
		      FILE * f)
{
  int i, M;
  QO_INFO *info;
  int t;
  BITSET_ITERATOR iter;
  const char *prefix;

  fputs ("\nNode info maps:\n", f);
  for (i = 0; i < (signed) planner->N; i++)
    {
      if (BITSET_MEMBER (QO_PARTITION_NODES (partition), i))
	{
	  info = planner->node_info[i];
	  if (info && !info->detached)
	    {
	      fprintf (f, "node_info[%d]:\n", i);
	      qo_dump_info (info, f);
	    }
	}
    }

  if (!bitset_is_empty (&(QO_PARTITION_EDGES (partition))))
    {
      fputs ("\nJoin info maps:\n", f);
      /* in current implementation, join_info[0..2] does not used
       */
      i = QO_PARTITION_M_OFFSET (partition);
      M = i + QO_JOIN_INFO_SIZE (partition);
      for (i = i + 3; i < M; i++)
	{
	  info = planner->join_info[i];
	  if (info && !info->detached)
	    {
	      fputs ("join_info[", f);
	      prefix = "";	/* init */
	      for (t = bitset_iterate (&(info->nodes), &iter); t != -1;
		   t = bitset_next_member (&iter))
		{
		  fprintf (f, "%s%d",
			   prefix,
			   QO_NODE_IDX (QO_ENV_NODE (planner->env, t)));
		  prefix = ",";
		}
	      fputs ("]:\n", f);
	      qo_dump_info (info, f);
	    }
	}			/* for (i = i + 3; i < M; i++) */
    }
}

/*
 * The heart of the search algorithm; generates all
 * "useful" permutations of the join nodes.  A permutation is
 * useful if every element of the permutation is "connected" to the
 * permutation prefix that precedes it.  An element (i.e., node)
 * is connected to a prefix with join edges.
 *
 * 		         D
 * 		        /
 * 		 A--B--C
 * 		        \
 * 			 E
 *
 * The permutations of the set {A, B, C, D, E} are
 *
 *   (+ordered)ABCDE(u)   BACDE(u)   CABDE      DABCE      EABCD
 *             ABCED(u)   BACED(u)   CABED      DABEC      EABDC
 *             ABDCE      BADCE	 CADBE      DACBE      EACBD
 *             ABDEC      BADEC	 CADEB      DACEB      EACDB
 *             ABECD      BAECD	 CAEBD      DAEBC      EADBC
 *             ABEDC      BAEDC	 CAEDB      DAECB      EADCB
 *             ACBDE      BCADE(u)   CBADE(u)   DBACE      EBACD
 *             ACBED      BCAED(u)   CBAED(u)   DBAEC      EBADC
 *             ACDCE      BCDAE(u)   CBDAE(u)   DBCAE      EBCAD
 *             ACDEC      BCDEA(u)   CBDEA(u)   DBCEA      EBCDA
 *             ACEBD      BCEAD(u)   CBEAD(u)   DBEAC      EBDAC
 *             ACEDB      BCEDA(u)   CBEDA(u)   DBECA      EBDCA
 *             ADBCE      BDACE	 CDABE      DCABE      ECABD
 *             ADBEC      BDAEC	 CDAEB      DCAEB      ECADB
 *             ADCBE      BDCAE	 CDBAE(u)   DCBAE(u)   ECBAD(u)
 *             ADCEB      BDCEA	 CDBEA(u)   DCBEA(u)   ECBDA(u)
 *             ADEBC      BDEAC	 CDEAB      DCEAB      ECDAB
 *             ADECB      BDECA	 CDEBA(u)   DCEBA(u)   ECDBA(u)
 *             AEBCD      BEACD	 CEABD      DEABC      EDABC
 *             AEBDC      BEADC	 CEADB      DEACB      EDACB
 *             AECBD      BECAD	 CEBAD(u)   DEBAC      EDBAC
 *             AECDB      BECDA 	 CEBDA(u)   DEBCA      EDBCA
 *             AEDBC      BEDAC	 CEDAB      DECAB      EDCAB
 *             AEDCB      BEDCA	 CEDBA(u)   DECBA      EDCBA
 *
 * The useful permutations are flagged with a (u).  Conceptually,
 * this function examines the permutations in this order,
 * investigating different methods of performing each of the joins in
 * the permutation, building a cost estimate for each these methods,
 * and picking the plan with the lowest estimate.  In reality, we do
 * things somewhat differently.
 *
 * This function works recursively.  Each level of recursion
 * corresponds to a permutation prefix; the algorithm progresses by
 * choosing a successor element for that prefix, extending the prefix
 * with that element, and then recursively generating all
 * permutations that begin with the extended prefix.  When those
 * possibilities are exhausted, the next successor element is
 * selected and the process repeated, until there are no more
 * successors.
 *
 * The search pruning occurs because we are constantly examining
 * prefixes to see if they are worth pursuing further.  The tests we
 * employ are described later.
 *
 *
 * The parameters are:
 *
 * planner: The current planner.
 *
 * partition: The current partition.
 *
 * tail_node: The node for a subtree of permutation suffixes.
 *  That is, planner_visit_node(planner, partition, tail_node, ...)
 *  produces all permutations (of unconnected free nodes) that begin
 *  with tail_node.
 *
 * visited_nodes: The nodes( with original id)
 *  joined together by the current permutation prefix.
 *
 * visited_rel_nodes: The nodes( with relative id in partition)
 *  joined together by the current permutation prefix.
 *
 * visited_terms: The terms that have been incorporated into the plan
 *  tree so far.  It contains every term that backs an edge
 *  that has been used as a join (or follow) operator, plus
 *  all sargs for nodes in visited_nodes, plus all other terms
 *  that are covered by the current set of joined nodes (i.e.,
 *  terms involving only nodes that are elements of visited_nodes).
 *
 * remaining_terms: The terms haven't yet been incorporated into the plan
 *  tree so far.
 *
 * remaining_subqueries: The correlated subqueries that haven't yet
 *  been pinned to a plan because their dependencies aren't
 *  all satisfied.  We'll pin any for whom this call supplies
 *  the missing link (node).
 */

/*
 * planner_visit_node () -
 *   return:
 *   planner(in):
 *   partition(in):
 *   hint(in):
 *   head_node(in):
 *   tail_node(in):
 *   visited_nodes(in):
 *   visited_rel_nodes(in):
 *   visited_terms(in):
 *   nested_path_nodes(in):
 *   remaining_nodes(in):
 *   remaining_terms(in):
 *   remaining_subqueries(in):
 *   num_path_inner(in):
 */
static void
planner_visit_node (QO_PLANNER * planner,
		    QO_PARTITION * partition,
		    PT_HINT_ENUM hint,
		    QO_NODE * head_node,
		    QO_NODE * tail_node,
		    BITSET * visited_nodes,
		    BITSET * visited_rel_nodes,
		    BITSET * visited_terms,
		    BITSET * nested_path_nodes,
		    BITSET * remaining_nodes,
		    BITSET * remaining_terms,
		    BITSET * remaining_subqueries, int num_path_inner)
{
  JOIN_TYPE join_type = NO_JOIN;
  QO_TERM *follow_term = NULL;
  int idx_join_cnt = 0;		/* number of idx-join edges */
  QO_TERM *term;
  QO_NODE *node;
  QO_INFO *head_info = (QO_INFO *) NULL;
  QO_INFO *tail_info = (QO_INFO *) NULL;
  QO_INFO *new_info = (QO_INFO *) NULL;
  int i, j;
  BITSET_ITERATOR bi, bj;
  BITSET nl_join_terms;		/* nested-loop join terms */
  BITSET sm_join_terms;		/* sort merge join terms */
  BITSET duj_terms;		/* during join terms */
  BITSET afj_terms;		/* after join terms */
  BITSET sarged_terms;
  BITSET info_terms;
  BITSET pinned_subqueries;

  bitset_init (&nl_join_terms, planner->env);
  bitset_init (&sm_join_terms, planner->env);
  bitset_init (&duj_terms, planner->env);
  bitset_init (&afj_terms, planner->env);
  bitset_init (&sarged_terms, planner->env);
  bitset_init (&info_terms, planner->env);
  bitset_init (&pinned_subqueries, planner->env);

  /* If we have visited two or more nodes, then a node that isn't
   * covered by that prefix is now connected to prefix, and we need to
   * examine ways of joining that uncovered node with the subresult
   * implied by the prefix.  If we have visited only one node (i.e.,
   * this is the top-level call to planner_visit_node(), and the current
   * prefix has only one node), then we are simply joining two base nodes.
   *
   * The cleverness here is that we take advantage of commutativity to
   * use the best plan for the particular combination of joins in the
   * current prefix, regardless of the specific order indicated by that
   * prefix.  The join_info array is used to cache solutions, and for
   * each combination of joins, it remembers the best plans available
   * for the combination for each interesting order.  So, even if the
   * current prefix is, for example, (A B C D), we may use a plan
   * whose order is (D ((AB) C)) if it is known to be cheaper.  All of
   * this is invisible to planner_visit_node(); the magic is accomplished by
   * qo_examine_follow(), examine_idx_join(), examine_nl_join(),
   * examine_merge_join() and the Info nodes.  However,
   * by generating all permutations in planner_visit_node(), we guarantee
   * that all possible (profitable) orderings are considered.
   *
   * Another advantage of this ploy is that non-productive branches
   * will be pruned quickly.  For example, if the plan with order
   * ((AB) C) is cheaper than the one with order ((BC) A), then we will
   * cease to examine the permutations with a (B C A) prefix, since we know
   * that we can't possibly generate a cheaper plan than one we've
   * already generated.
   */

  if (head_node == NULL)
    {
      goto wrapup;		/* unknown error */
    }

  if (num_path_inner)
    {				/* check for path connected nodes */
      if (bitset_is_empty (nested_path_nodes))
	{			/* not yet assign path connected nodes */
	  int found_num, found_idx;

	  for (i = bitset_iterate (remaining_terms, &bi); i != -1;
	       i = bitset_next_member (&bi))
	    {
	      term = QO_ENV_TERM (planner->env, i);
	      if (QO_TERM_CLASS (term) == QO_TC_PATH
		  && QO_NODE_IDX (QO_TERM_HEAD (term)) ==
		  QO_NODE_IDX (head_node)
		  && QO_NODE_IDX (QO_TERM_TAIL (term)) ==
		  QO_NODE_IDX (tail_node))
		{
		  bitset_add (nested_path_nodes,
			      QO_NODE_IDX (QO_TERM_TAIL (term)));
		  /* Traverse tail link */
		  do
		    {
		      found_num = 0;	/* init */
		      for (j = bitset_iterate (remaining_terms, &bj); j != -1;
			   j = bitset_next_member (&bj))
			{
			  term = QO_ENV_TERM (planner->env, j);
			  if (QO_TERM_CLASS (term) == QO_TC_PATH &&
			      BITSET_MEMBER (*nested_path_nodes,
					     QO_NODE_IDX (QO_TERM_HEAD
							  (term))))
			    {
			      found_idx = QO_NODE_IDX (QO_TERM_TAIL (term));
			      /* found nested path term */
			      if (!BITSET_MEMBER
				  (*nested_path_nodes, found_idx))
				{
				  bitset_add (nested_path_nodes, found_idx);
				  found_num++;
				}
			    }
			}
		    }
		  while (found_num);

		  /* Traverse head link reversely */
		  do
		    {
		      found_num = 0;	/* init */
		      for (j = bitset_iterate (remaining_terms, &bj); j != -1;
			   j = bitset_next_member (&bj))
			{
			  term = QO_ENV_TERM (planner->env, j);
			  if (QO_TERM_CLASS (term) == QO_TC_PATH &&
			      BITSET_MEMBER (*nested_path_nodes,
					     QO_NODE_IDX (QO_TERM_TAIL
							  (term))))
			    {
			      found_idx = QO_NODE_IDX (QO_TERM_HEAD (term));
			      /* found nested path term */
			      if (!BITSET_MEMBER
				  (*nested_path_nodes, found_idx))
				{
				  bitset_add (nested_path_nodes, found_idx);
				  found_num++;
				}
			    }
			}
		    }
		  while (found_num);

		  /* exclude already joined nodes */
		  bitset_difference (nested_path_nodes, visited_nodes);
		  /* remove tail_node from path connected nodes */
		  bitset_remove (nested_path_nodes, QO_NODE_IDX (tail_node));

		  break;	/* exit for-loop */
		}
	    }			/* for (i = ...) */
	}
      else
	{			/* already assign path connected nodes */
	  if (BITSET_MEMBER (*nested_path_nodes, QO_NODE_IDX (tail_node)))
	    {
	      /* remove tail_node from path connected nodes */
	      bitset_remove (nested_path_nodes, QO_NODE_IDX (tail_node));
	    }
	  else
	    {
	      goto wrapup;
	    }
	}
    }

  /*
   * STEP 1: set head_info, tail_info, visited_nodes, visited_rel_nodes
   */

  /* head_info points to the current prefix
   */
  if (bitset_cardinality (visited_nodes) == 1)
    {
      /* current prefix has only one node */
      head_info = planner->node_info[QO_NODE_IDX (head_node)];
    }
  else
    {
      /* current prefix has two or more nodes */
      head_info =
	planner->
	join_info[QO_INFO_INDEX
		  (QO_PARTITION_M_OFFSET (partition), *visited_rel_nodes)];
      /* currently, do not permit cross join plan. for future work,
       *                  NEED MORE CONSIDERAION
       */
      if (head_info == NULL)
	{
	  goto wrapup;
	}
    }

  /* tail_info points to the node for the single class being added to the
   * prefix
   */
  tail_info = planner->node_info[QO_NODE_IDX (tail_node)];

  /* connect tail_node to the prefix
   */
  bitset_add (visited_nodes, QO_NODE_IDX (tail_node));
  bitset_add (visited_rel_nodes, QO_NODE_REL_IDX (tail_node));
  bitset_remove (remaining_nodes, QO_NODE_IDX (tail_node));

  new_info =
    planner->
    join_info[QO_INFO_INDEX
	      (QO_PARTITION_M_OFFSET (partition), *visited_rel_nodes)];

  /* check for already examined join_info */
  if (new_info && new_info->join_unit < planner->join_unit)
    {
      /* at here, not yet visited at this join level; use cache
       */

      if (new_info->best_no_order.nplans == 0)
	{
	  goto wrapup;		/* give up */
	}

      /* STEP 2: set terms for join_info */
      /* set info terms */
      bitset_assign (&info_terms, &(new_info->terms));
      bitset_difference (&info_terms, visited_terms);

      /* extract visited info terms
       */
      bitset_union (visited_terms, &info_terms);
      bitset_difference (remaining_terms, &info_terms);

      /* STEP 3: set pinned_subqueries */
      {
	QO_SUBQUERY *subq;

	for (i = bitset_iterate (remaining_subqueries, &bi); i != -1;
	     i = bitset_next_member (&bi))
	  {
	    subq = &planner->subqueries[i];
	    if (bitset_subset (visited_nodes, &(subq->nodes))
		&& bitset_subset (visited_terms, &(subq->terms)))
	      {
		bitset_add (&pinned_subqueries, i);
	      }
	  }			/* for (i = ...) */

	/* extract pinned subqueries
	 */
	bitset_difference (remaining_subqueries, &pinned_subqueries);
      }

      goto go_ahead_subvisit;
    }

  /* extract terms of the tail_info subplan.
   * this is necessary to ensure that we are aware of any terms that
   * have been sarged by the subplans
   */
  bitset_union (&info_terms, &(tail_info->terms));

  /* extract visited info terms
   */
  bitset_union (visited_terms, &info_terms);
  bitset_difference (remaining_terms, &info_terms);

  /* STEP 2: set specific terms for follow and join */

  /* in given partition, collect terms connected to tail_info
   */
  {
    int retry_cnt, edge_cnt, path_cnt;
    bool found_edge;
    PT_NODE *pt_expr;

    retry_cnt = 0;		/* init */

  retry_join_edge:

    edge_cnt = path_cnt = 0;	/* init */

    for (i = bitset_iterate (remaining_terms, &bi); i != -1;
	 i = bitset_next_member (&bi))
      {

	term = QO_ENV_TERM (planner->env, i);

	if (!bitset_subset (visited_nodes, &(QO_TERM_NODES (term))))
	  {
	    continue;
	  }

	found_edge = false;	/* init */

	if (BITSET_MEMBER (QO_TERM_NODES (term), QO_NODE_IDX (tail_node)))
	  {
	    if (QO_TERM_CLASS (term) == QO_TC_PATH)
	      {
		if (retry_cnt == 0)
		  {		/* is the first stage */
		    /* need to check the direction; head -> tail */
		    if (QO_NODE_IDX (QO_TERM_TAIL (term)) ==
			QO_NODE_IDX (tail_node))
		      {
			found_edge = true;
		      }
		    else
		      {
			/* save path for the retry stage */
			path_cnt++;
		      }
		  }
		else
		  {
		    /* at retry stage; there is only path edge
		     * so, need not to check the direction */
		    found_edge = true;
		  }
	      }
	    else if (QO_IS_EDGE_TERM (term))
	      {
		found_edge = true;
	      }
	  }

	if (found_edge == true)
	  {
	    /* found edge
	     */
	    edge_cnt++;

	    /* set join type */
	    if (join_type == NO_JOIN)
	      {			/* the first time */
		join_type = QO_TERM_JOIN_TYPE (term);
	      }
	    else
	      {			/* already assigned */
		if (IS_OUTER_JOIN_TYPE (join_type))
		  {
		    /* outer join type must be the same */
		    if (IS_OUTER_JOIN_TYPE (QO_TERM_JOIN_TYPE (term)))
		      {
			QO_ASSERT (planner->env,
				   join_type == QO_TERM_JOIN_TYPE (term));
		      }
		  }
		else
		  {
		    if (IS_OUTER_JOIN_TYPE (QO_TERM_JOIN_TYPE (term)))
		      {
			/* replace to the outer join type */
			join_type = QO_TERM_JOIN_TYPE (term);
		      }
		  }
	      }

	    pt_expr = QO_TERM_PT_EXPR (term);

	    /* check for always true transitive join term */
	    if (pt_expr &&
		PT_EXPR_INFO_IS_FLAGED (pt_expr, PT_EXPR_INFO_TRANSITIVE))
	      {
		/* check for idx-join */
		if (QO_TERM_CAN_USE_INDEX (term))
		  {
		    idx_join_cnt++;
		  }

		bitset_add (&info_terms, i);	/* add to info term */

		continue;	/* skip out from join terms and sarged terms */
	      }

	    switch (QO_TERM_CLASS (term))
	      {
	      case QO_TC_PATH:
		if (follow_term == NULL)
		  {		/* get the first PATH term idx */
		    follow_term = term;
		    /* for path-term, if join type is not outer join,
		     * we can use idx-join, nl-join
		     */
		    if (QO_TERM_JOIN_TYPE (follow_term) == JOIN_INNER)
		      {
			/* check for idx-join */
			if (QO_TERM_CAN_USE_INDEX (term))
			  {
			    idx_join_cnt++;
			  }
			bitset_add (&nl_join_terms, i);
		      }
		    /* check for m-join */
		    if (QO_TERM_IS_FLAGED (term, QO_TERM_MERGEABLE_EDGE))
		      {
			bitset_add (&sm_join_terms, i);
		      }
		  }
		else
		  {		/* found another PATH term */
		    /* unknown error */
		    QO_ASSERT (planner->env, UNEXPECTED_CASE);
		  }
		break;

	      case QO_TC_JOIN:
		/* check for idx-join */
		if (QO_TERM_CAN_USE_INDEX (term))
		  {
		    idx_join_cnt++;
		  }
		bitset_add (&nl_join_terms, i);
		/* check for m-join */
		if (QO_TERM_IS_FLAGED (term, QO_TERM_MERGEABLE_EDGE))
		  {
		    bitset_add (&sm_join_terms, i);
		  }
		else
		  {		/* non-eq edge */
		    if (IS_OUTER_JOIN_TYPE (join_type)
			&& QO_TERM_LOCATION (term) > 0)
		      {		/* ON clause */
			bitset_add (&duj_terms, i);	/* need for m-join */
		      }
		  }
		break;

	      case QO_TC_DEP_LINK:
	      case QO_TC_DEP_JOIN:
		bitset_add (&nl_join_terms, i);
		break;

	      case QO_TC_DUMMY_JOIN:
		/* check for idx-join */
		if (QO_TERM_CAN_USE_INDEX (term))
		  {
		    idx_join_cnt++;
		  }
		/* exclude from nl_join_terms, sm_join_terms
		 */
		break;

	      default:
		QO_ASSERT (planner->env, UNEXPECTED_CASE);
		break;
	      }			/* switch (QO_TERM_CLASS(term)) */
	  }
	else
	  {
	    /* does not edge
	     */

	    if (QO_TERM_CLASS (term) == QO_TC_DURING_JOIN)
	      {
		/* check location for outer join */
		for (j = bitset_iterate (visited_nodes, &bj); j != -1;
		     j = bitset_next_member (&bj))
		  {
		    node = QO_ENV_NODE (planner->env, j);
		    if (QO_NODE_LOCATION (node) == QO_TERM_LOCATION (term))
		      {
			bitset_add (&duj_terms, i);
			/* check for joinable edge */
			if (bitset_cardinality (&(QO_TERM_NODES (term))) == 2
			    && BITSET_MEMBER (QO_TERM_NODES (term),
					      QO_NODE_IDX (tail_node)))
			  {
			    /* check for idx-join */
			    if (QO_TERM_CAN_USE_INDEX (term))
			      {
				idx_join_cnt++;
			      }
			  }
			break;
		      }
		  }		/* for (j = ...) */

		if (j == -1)
		  {		/* out of location */
		    continue;
		  }
	      }
	    else if (QO_TERM_CLASS (term) == QO_TC_AFTER_JOIN)
	      {
		/* If visited_nodes is the same as partition's nodes,
		 * then we have successfully generated one of the graph
		 * permutations(i.e., we have considered every one of the
		 * nodes). only include after-join term for this plan.
		 */
		if (!bitset_is_equivalent (visited_nodes,
					   &(QO_PARTITION_NODES (partition))))
		  {
		    continue;
		  }
		bitset_add (&afj_terms, i);
	      }
	    else
	      {
		if (IS_OUTER_JOIN_TYPE (join_type)
		    && QO_TERM_LOCATION (term) > 0)
		  {		/* ON clause */
		    bitset_add (&duj_terms, i);
		  }
	      }
	  }

	bitset_add (&info_terms, i);	/* add to info term */

	/* skip always true dummy join term and do not evaluate */
	if (QO_TERM_CLASS (term) != QO_TC_DUMMY_JOIN)
	  {
	    bitset_add (&sarged_terms, i);	/* add to sarged term */
	  }

      }				/* for (i = ...) */

    /* currently, do not permit cross join plan. for future work,
     *                  NEED MORE CONSIDERAION
     */
    if (edge_cnt == 0)
      {
	if (retry_cnt == 0)
	  {			/* is the first stage */
	    if (path_cnt > 0)
	      {
		/* there is only path edge and the direction is reversed */
		retry_cnt++;
		goto retry_join_edge;
	      }
	  }
	goto wrapup;
      }
  }

  /* extract visited info terms
   */
  bitset_union (visited_terms, &info_terms);
  bitset_difference (remaining_terms, &info_terms);

  /* STEP 3: set pinned_subqueries */

  /* Find out if we can pin any of the remaining subqueries.  A subquery is
   * eligible to be pinned here if all of the nodes on which it depends are
   * covered here.  However, it mustn't be pinned here if it is part of a
   * term that hasn't been pinned yet.  Doing so risks improperly pushing
   * a subquery plan down through a merge join during XASL generation,
   * which results in an incorrect plan (the subquery has to be evaluated
   * during the merge, rather than during the scan that feeds the merge).
   */
  {
    QO_SUBQUERY *subq;

    for (i = bitset_iterate (remaining_subqueries, &bi); i != -1;
	 i = bitset_next_member (&bi))
      {
	subq = &planner->subqueries[i];
	if (bitset_subset (visited_nodes, &(subq->nodes))
	    && bitset_subset (visited_terms, &(subq->terms)))
	  {
	    bitset_add (&pinned_subqueries, i);
	  }
      }				/* for (i = ...) */

    /* extract pinned subqueries
     */
    bitset_difference (remaining_subqueries, &pinned_subqueries);
  }

  /* STEP 4: set joined info */

  if (new_info == NULL)
    {
      /* If this is the first time we have seen this particular
       * combination of nodes (regardless of their ordering), allocate
       * a new info node for the combination.
       *
       * Notice that we compute several pieces of information for this
       * new node (e.g., cardinality, projected segments) only once, when
       * we first construct this new node.  Because this information
       * must always be constant for this particular combination of
       * nodes, regardless of the plan used to combine them, we keep a
       * common copy of the information in the info node.
       *
       * One thing that *won't* be constant is the set of terms that
       * are sarged by a plan: that set depends on the "path" by which
       * we arrived at that plan, i.e., the subplans that go into that
       * plan.
       */

      double selectivity, cardinality;
      BITSET eqclasses;

      bitset_init (&eqclasses, planner->env);

      /* Let the immediate termset of a plan be the terms that are
       * sarged by that plan and, in the case of a join or a follow
       * plan, the edge that describes the join.  Let the full termset
       * of a plan be the union of the plan's immediate termset and the
       * full termsets of any subplans.
       *
       * Although the immediate termsets of each of the plans at an
       * info node may differ, the full termsets must all be the same
       * (since, by construction, all of the plans at the same info node
       * join together the same join graph nodes).  This means that we
       * can use any one of them to compute the selectivity of the full
       * termset and, consequently, the estimated cardinality.  For
       * this purpose we use the first plan we encounter, and cache the
       * constant information in the info node to be shared with other
       * plans at the same node.
       *
       * By construction, there can be at most one path term in the set
       * of terms under construction here (since every path term leads
       * to a distinct join graph node).  If there is one path term
       * here, we know that the cardinality of the result will be a
       * function only of the cardinality of the node at the head of
       * the term (or nodes, in the case where the path term is leading
       * out of an already-joined group of nodes).  Watch for that case
       * and exploit it: it can identified by either an explicit
       * QO_IS_PATH_TERM test or by a negative selectivity for the
       * term.
       */

      selectivity = 1.0;	/* init */

      cardinality = head_info->cardinality * tail_info->cardinality;
      if (IS_OUTER_JOIN_TYPE (join_type))
	{
	  /* set lower bound of outer join result */
	  if (join_type == JOIN_RIGHT)
	    {
	      cardinality = MAX (cardinality, tail_info->cardinality);
	    }
	  else
	    {
	      cardinality = MAX (cardinality, head_info->cardinality);
	    }
	}

      if (cardinality != 0)
	{			/* not empty */
	  cardinality = MAX (1.0, cardinality);
	  for (i = bitset_iterate (&sarged_terms, &bi); i != -1;
	       i = bitset_next_member (&bi))
	    {
	      term = &planner->term[i];
	      if (QO_IS_PATH_TERM (term) &&
		  QO_TERM_JOIN_TYPE (term) != JOIN_INNER)
		{
		  /* single-fetch */
		  cardinality = head_info->cardinality;
		  if (cardinality != 0)
		    {		/* not empty */
		      cardinality = MAX (1.0, cardinality);
		    }
		}
	      else
		{
		  selectivity *= QO_TERM_SELECTIVITY (term);
		  selectivity = MAX (1.0 / cardinality, selectivity);
		}
	    }			/* for (i = ...) */
	  cardinality *= selectivity;
	  cardinality = MAX (1.0, cardinality);

	  if (IS_OUTER_JOIN_TYPE (join_type) && bitset_is_empty (&afj_terms))
	    {
	      /* set lower bound of outer join result */
	      if (join_type == JOIN_RIGHT)
		{
		  cardinality = MAX (cardinality, tail_info->cardinality);
		}
	      else
		{
		  cardinality = MAX (cardinality, head_info->cardinality);
		}
	    }
	}

      bitset_assign (&eqclasses, &(head_info->eqclasses));
      bitset_union (&eqclasses, &(tail_info->eqclasses));

      new_info =
	planner->
	join_info[QO_INFO_INDEX
		  (QO_PARTITION_M_OFFSET (partition), *visited_rel_nodes)] =
	qo_alloc_info (planner, visited_nodes, visited_terms, &eqclasses,
		       cardinality);

      bitset_delset (&eqclasses);
    }

  /* STEP 5: do EXAMINE follow, join */

  {
    int kept = 0;

    /* for path-term, if join order is correct, we can use follow.
     */
    if (follow_term
	&& QO_NODE_IDX (QO_TERM_TAIL (follow_term)) ==
	QO_NODE_IDX (tail_node))
      {
	/* STEP 5-1: examine follow
	 */
	kept += qo_examine_follow (new_info,
				   follow_term,
				   head_info, &sarged_terms,
				   &pinned_subqueries);
      }

    if (follow_term
	&& join_type != JOIN_INNER
	&& QO_NODE_IDX (QO_TERM_TAIL (follow_term)) !=
	QO_NODE_IDX (tail_node))
      {
	/* if there is a path-term whose outer join order is not correct,
	 * we can not use idx-join, nl-join, m-join
	 */
	;
      }
    else
      {
#if 1				/* CORRELATED_INDEX */
	/* STEP 5-2: examine idx-join
	 */
	if (idx_join_cnt)
	  {
	    kept += examine_idx_join (new_info,
				      join_type,
				      head_info,
				      tail_info,
				      &afj_terms,
				      &sarged_terms, &pinned_subqueries);
	  }
#endif /* CORRELATED_INDEX */

	/* STEP 5-3: examine nl-join
	 */
	kept += examine_nl_join (new_info,
				 join_type,
				 head_info,
				 tail_info,
				 &nl_join_terms,
				 &duj_terms,
				 &afj_terms,
				 &sarged_terms, &pinned_subqueries);

#if 1				/* MERGE_JOINS */
	/* STEP 5-4: examine merge-join
	 */
	if (!bitset_is_empty (&sm_join_terms))
	  {
	    kept += examine_merge_join (new_info,
					join_type,
					head_info,
					tail_info,
					&sm_join_terms,
					&duj_terms,
					&afj_terms,
					&sarged_terms, &pinned_subqueries);
	  }
#endif /* MERGE_JOINS */
      }

    /* At this point, kept indicates the number of worthwhile plans
     * generated by examine_joins (i.e., plans that where cheaper than
     * some previous equivalent plan).  If that number is 0, then there
     * is no point in continuing this particular branch of permutations:
     * we've already generated all of the suffixes once before, and with
     * a better prefix to boot.  There is no possibility of finding a
     * better plan with this prefix.
     */
    if (!kept)
      {
	goto wrapup;
      }
  }

  /* STEP 7: go on sub permutations */

go_ahead_subvisit:

  /* If visited_nodes' cardinality is the same as join_unit,
   * then we have successfully generated one of the graph permutations
   * (i.e., we have considered every one of the nodes).
   * If not, we need to try to recursively generate suffixes.
   */
  if (bitset_cardinality (visited_nodes) >= planner->join_unit)
    {
      /* If this is the info node that corresponds to the final plan
       * (i.e., every node in the partition is covered by the plans at
       * this node), *AND* we have something to put in it, then record
       * that fact in the planner.  This permits more aggressive pruning,
       * since we can immediately discard any plan (or subplan) that is
       * no better than the best known plan for the entire partition.
       */
      if (!planner->best_info)
	{
	  planner->best_info = new_info;
	}
    }
  else
    {
      for (i = bitset_iterate (remaining_nodes, &bi); i != -1;
	   i = bitset_next_member (&bi))
	{
	  node = QO_ENV_NODE (planner->env, i);

	  /* node dependency check;
	   */
	  if (!bitset_subset (visited_nodes, &(QO_NODE_DEP_SET (node))))
	    {
	      /* node represents dependent tables, so there is no way
	       * this combination can work in isolation.  Give up so we can
	       * try some other combinations.
	       */
	      continue;
	    }
	  if (!bitset_subset (visited_nodes, &(QO_NODE_OUTER_DEP_SET (node))))
	    {
	      /* All previous nodes participating in outer join spec
	       * should be joined before. QO_NODE_OUTER_DEP_SET()
	       * represents all previous nodes which are dependents on
	       * the node.  See graph.c:analyze_term()
	       */
	      continue;
	    }

	  /* now, set node as next tail node, do recursion
	   */
	  (void) planner_visit_node (planner, partition, hint, tail_node,	/* next head node */
				     node,	/* next tail node */
				     visited_nodes,
				     visited_rel_nodes,
				     visited_terms,
				     nested_path_nodes,
				     remaining_nodes,
				     remaining_terms,
				     remaining_subqueries, num_path_inner);

	  /* join hint: force join left-to-right */
	  if (hint & PT_HINT_ORDERED)
	    {
	      break;
	    }
	}			/* for (i = ...) */
    }				/* else */

wrapup:

  /* recover to original
   */

  bitset_remove (visited_nodes, QO_NODE_IDX (tail_node));
  bitset_remove (visited_rel_nodes, QO_NODE_REL_IDX (tail_node));
  bitset_add (remaining_nodes, QO_NODE_IDX (tail_node));

  bitset_difference (visited_terms, &info_terms);
  bitset_union (remaining_terms, &info_terms);

  bitset_union (remaining_subqueries, &pinned_subqueries);

  /* free alloced
   */
  bitset_delset (&nl_join_terms);
  bitset_delset (&sm_join_terms);
  bitset_delset (&duj_terms);
  bitset_delset (&afj_terms);
  bitset_delset (&sarged_terms);
  bitset_delset (&info_terms);
  bitset_delset (&pinned_subqueries);
}

/*
 * planner_nodeset_join_cost () -
 *   return:
 *   planner(in):
 *   nodeset(in):
 */
static double
planner_nodeset_join_cost (QO_PLANNER * planner, BITSET * nodeset)
{
  int i;
  BITSET_ITERATOR bi;
  QO_NODE *node;
  QO_INFO *info;
  QO_PLAN *plan, *subplan;
  double total_cost, objects, result_size, pages;

  total_cost = 0.0;		/* init */

  for (i = bitset_iterate (nodeset, &bi);
       i != -1; i = bitset_next_member (&bi))
    {

      node = QO_ENV_NODE (planner->env, i);
      info = planner->node_info[QO_NODE_IDX (node)];

      plan = qo_find_best_plan_on_info (info, QO_UNORDERED, 1.0);

      if (plan == NULL)
	{			/* something wrong */
	  continue;		/* give up */
	}

      objects = (plan->info)->cardinality;
      result_size = objects * (double) (plan->info)->projected_size;
      pages = result_size / (double) IO_PAGESIZE;
      pages = MAX (1.0, pages);

      /* apply join cost; add to the total cost */
      total_cost += pages;

      if (QO_NODE_HINT (node) & (PT_HINT_USE_IDX | PT_HINT_USE_NL))
	{
	  /* join hint: force idx-join, nl-join;
	   */
	}
      else if (QO_NODE_HINT (node) & PT_HINT_USE_MERGE)
	{			/* force m-join */
	  if (plan->plan_type == QO_PLANTYPE_SORT)
	    {
	      subplan = plan->plan_un.sort.subplan;
	    }
	  else
	    {
	      subplan = plan;
	    }

	  objects = (subplan->info)->cardinality;
	  result_size = objects * (double) (subplan->info)->projected_size;
	  pages = result_size / (double) IO_PAGESIZE;
	  pages = MAX (1.0, pages);

	  /* apply merge cost; add to the total cost */
	  if (plan->plan_type == QO_PLANTYPE_SORT)
	    {
	      /* alread apply inner cost: apply only outer cost */
	      total_cost += pages;
	    }
	  else
	    {
	      /* do guessing: apply outer, inner cost */
	      total_cost += pages * 2.0;
	    }
	}

    }				/* for (i = ...) */

  return total_cost;
}

/*
 * planner_permutate () -
 *   return:
 *   planner(in):
 *   partition(in):
 *   hint(in):
 *   prev_head_node(in):
 *   visited_nodes(in):
 *   visited_rel_nodes(in):
 *   visited_terms(in):
 *   first_nodes(in):
 *   nested_path_nodes(in):
 *   remaining_nodes(in):
 *   remaining_terms(in):
 *   remaining_subqueries(in):
 *   num_path_inner(in):
 *   node_idxp(in):
 */
static void
planner_permutate (QO_PLANNER * planner,
		   QO_PARTITION * partition,
		   PT_HINT_ENUM hint,
		   QO_NODE * prev_head_node,
		   BITSET * visited_nodes,
		   BITSET * visited_rel_nodes,
		   BITSET * visited_terms,
		   BITSET * first_nodes,
		   BITSET * nested_path_nodes,
		   BITSET * remaining_nodes,
		   BITSET * remaining_terms,
		   BITSET * remaining_subqueries,
		   int num_path_inner, int *node_idxp)
{
  int i, j;
  BITSET_ITERATOR bi, bj;
  QO_INFO *head_info, *best_info;
  QO_NODE *head_node, *tail_node;
  QO_PLAN *best_plan;
  double best_cost, prev_best_cost;
  BITSET rest_nodes;

  bitset_init (&rest_nodes, planner->env);

  planner->best_info = NULL;	/* init */

  prev_best_cost = -1.0;	/* init */

  /* Now perform the actual search.  Entries in join_info will
   * gradually be filled and refined within the calls to examine_xxx_join().
   * When we finish, planner->best_info will hold information about
   * the best ways discovered to perform the entire join.
   */
  for (i = bitset_iterate (remaining_nodes, &bi); i != -1;
       i = bitset_next_member (&bi))
    {

      head_node = QO_ENV_NODE (planner->env, i);

      /* head node dependency check;
       */
      if (!bitset_subset (visited_nodes, &(QO_NODE_DEP_SET (head_node))))
	{
	  /* head node represents dependent tables, so there is no way
	   * this combination can work in isolation.  Give up so we
	   * can try some other combinations.
	   */
	  continue;
	}
      if (!bitset_subset
	  (visited_nodes, &(QO_NODE_OUTER_DEP_SET (head_node))))
	{
	  /* All previous nodes participating in outer join spec
	   * should be joined before. QO_NODE_OUTER_DEP_SET()
	   * represents all previous nodes which are dependents on
	   * the node.  See graph.c:analyze_term()
	   */
	  continue;
	}

      /* the first join node check */
      if (!bitset_is_empty (first_nodes))
	{
	  if (!bitset_intersects (visited_nodes, first_nodes))
	    {			/* not include */
	      if (!BITSET_MEMBER (*first_nodes, QO_NODE_IDX (head_node)))
		{
		  continue;
		}
	    }
	}

      if (bitset_is_empty (visited_nodes))
	{			/* not found outermost nodes */

	  head_info = planner->node_info[QO_NODE_IDX (head_node)];

	  /* init */
	  bitset_add (visited_nodes, QO_NODE_IDX (head_node));
	  bitset_add (visited_rel_nodes, QO_NODE_REL_IDX (head_node));
	  bitset_remove (remaining_nodes, QO_NODE_IDX (head_node));

	  bitset_union (visited_terms, &(head_info->terms));
	  bitset_difference (remaining_terms, &(head_info->terms));

	  for (j = bitset_iterate (remaining_nodes, &bj); j != -1;
	       j = bitset_next_member (&bj))
	    {

	      tail_node = QO_ENV_NODE (planner->env, j);

	      /* tail node dependency check;
	       */
	      if (!bitset_subset
		  (visited_nodes, &(QO_NODE_DEP_SET (tail_node))))
		{
		  continue;
		}
	      if (!bitset_subset
		  (visited_nodes, &(QO_NODE_OUTER_DEP_SET (tail_node))))
		{
		  continue;
		}

	      BITSET_CLEAR (*nested_path_nodes);

	      (void) planner_visit_node (planner,
					 partition,
					 hint,
					 head_node,
					 tail_node,
					 visited_nodes,
					 visited_rel_nodes,
					 visited_terms,
					 nested_path_nodes,
					 remaining_nodes,
					 remaining_terms,
					 remaining_subqueries,
					 num_path_inner);

	      /* join hint: force join left-to-right */
	      if (hint & PT_HINT_ORDERED)
		{
		  break;
		}

	    }			/* for (j = ...) */

	  /* recover to original
	   */
	  BITSET_CLEAR (*visited_nodes);
	  BITSET_CLEAR (*visited_rel_nodes);
	  bitset_add (remaining_nodes, QO_NODE_IDX (head_node));

	  bitset_difference (visited_terms, &(head_info->terms));
	  bitset_union (remaining_terms, &(head_info->terms));

	}
      else
	{			/* found some outermost nodes */

	  BITSET_CLEAR (*nested_path_nodes);

	  (void) planner_visit_node (planner, partition, hint, prev_head_node, head_node,	/* next tail node */
				     visited_nodes,
				     visited_rel_nodes,
				     visited_terms,
				     nested_path_nodes,
				     remaining_nodes,
				     remaining_terms,
				     remaining_subqueries, num_path_inner);
	}

      if (node_idxp)
	{			/* is partial node visit */
	  best_info = planner->best_info;
	  if (best_info == NULL)
	    {			/* not found best plan */
	      continue;		/* skip and go ahead */
	    }

	  best_plan =
	    qo_find_best_plan_on_info (best_info, QO_UNORDERED, 1.0);

	  if (best_plan == NULL)
	    {			/* unknown error */
	      break;		/* give up */
	    }

	  /* set best plan's cost */
	  best_cost = best_plan->fixed_cpu_cost +
	    best_plan->fixed_io_cost +
	    best_plan->variable_cpu_cost + best_plan->variable_io_cost;

	  /* apply rest nodes's cost */
	  bitset_assign (&rest_nodes, remaining_nodes);
	  bitset_difference (&rest_nodes, &(best_info->nodes));
	  best_cost += planner_nodeset_join_cost (planner, &rest_nodes);

	  if (prev_best_cost == -1.0	/* the first time */
	      || best_cost < prev_best_cost)
	    {
	      *node_idxp = QO_NODE_IDX (head_node);
	      prev_best_cost = best_cost;	/* found new best */
	    }

	  planner->best_info = NULL;	/* clear */
	}

      /* join hint: force join left-to-right */
      if (hint & PT_HINT_ORDERED)
	{
	  break;
	}

    }				/* for (i = ...) */

  if (node_idxp)
    {				/* is partial node visit */
      planner->best_info = NULL;	/* clear */
    }

  bitset_delset (&rest_nodes);

  return;
}

/*
 * qo_planner_search () -
 *   return:
 *   env(in):
 */
QO_PLAN *
qo_planner_search (QO_ENV * env)
{
  QO_PLANNER *planner;
  QO_PLAN *plan;

  planner = NULL;
  plan = NULL;

  planner = qo_alloc_planner (env);
  qo_info_nodes_init (env);
  qo_plans_init (env);
  plan = qo_search_planner (planner);
  qo_clean_planner (planner);

  return plan;
}

/*
 * qo_generate_join_index_scan () -
 *   return:
 *   infop(in):
 *   join_type(in):
 *   outer_plan(in):
 *   inner(in):
 *   nodep(in):
 *   ni_entryp(in):
 *   indexable_terms(in):
 *   afj_terms(in):
 *   sarged_terms(in):
 *   pinned_subqueries(in):
 */
static int
qo_generate_join_index_scan (QO_INFO * infop,
			     JOIN_TYPE join_type,
			     QO_PLAN * outer_plan,
			     QO_INFO * inner,
			     QO_NODE * nodep,
			     QO_NODE_INDEX_ENTRY * ni_entryp,
			     BITSET * indexable_terms,
			     BITSET * afj_terms,
			     BITSET * sarged_terms,
			     BITSET * pinned_subqueries)
{
  QO_ENV *env;
  QO_INDEX_ENTRY *index_entryp;
  BITSET_ITERATOR iter;
  QO_TERM *termp;
  PT_NODE *pt_expr;
  QO_PLAN *inner_plan;
  int i, t, last_t, j, n, seg;
  BITSET range_terms;
  BITSET kf_terms;
  BITSET empty_terms;
  BITSET remaining_terms;

  env = infop->env;

  bitset_init (&range_terms, env);
  bitset_init (&kf_terms, env);
  bitset_init (&empty_terms, env);
  bitset_init (&remaining_terms, env);

  bitset_assign (&kf_terms, indexable_terms);
  bitset_assign (&remaining_terms, sarged_terms);

  /* pointer to QO_INDEX_ENTRY structure */
  index_entryp = (ni_entryp)->head;

  for (i = 0; i < index_entryp->nsegs; i++)
    {
      if ((seg = (index_entryp->seg_idxs[i])) == -1)
	break;
      n = 0;
      last_t = -1;
      for (t = bitset_iterate (indexable_terms, &iter); t != -1;
	   t = bitset_next_member (&iter))
	{

	  termp = QO_ENV_TERM (env, t);
	  pt_expr = QO_TERM_PT_EXPR (termp);

	  /* check for always true transitive join term */
	  if (pt_expr &&
	      PT_EXPR_INFO_IS_FLAGED (pt_expr, PT_EXPR_INFO_TRANSITIVE))
	    {
	      /* skip out from all terms */
	      bitset_remove (&kf_terms, t);
	      bitset_remove (&remaining_terms, t);
	      continue;		/* do not add to range_terms */
	    }
	  for (j = 0; j < termp->can_use_index; j++)
	    {
	      /* found term */
	      if (QO_SEG_IDX (termp->index_seg[j]) == seg)
		{
		  /* save last found term */
		  last_t = t;
		  /* found EQ term */
		  if (QO_TERM_IS_FLAGED (termp, QO_TERM_EQUAL_OP))
		    {
		      bitset_add (&range_terms, t);
		      n++;
		    }
		  break;
		}
	    }			/* for (j = 0; j < termp->can_use_index; j++) */

	  /* found EQ term. exit term-iteration loop */
	  if (n)
	    {
	      break;
	    }
	}			/* for (t = ...) */

      /* not found EQ term. exit seg-iteration loop */
      if (n == 0)
	{
	  /* found term. add last non-EQ term */
	  if (last_t != -1)
	    {
	      bitset_add (&range_terms, last_t);
	    }
	  break;
	}
    }				/* for (i = 0; i < index_entryp->nsegs; i++) */

  n = 0;
  if (!bitset_is_empty (&range_terms))
    {
      /* exclude key-range terms from filter checking terms */
      bitset_difference (&kf_terms, &range_terms);
      inner_plan = qo_index_scan_new (inner, nodep, ni_entryp,
				      &range_terms,
				      &kf_terms, &QO_NODE_SUBQUERIES (nodep));
      /* now, key-flter is assigned;
         exclude key-range, key-filter terms from remaining terms */
      bitset_difference (&remaining_terms, &range_terms);
      bitset_difference (&remaining_terms,
			 &(inner_plan->plan_un.scan.kf_terms));

      n = qo_check_plan_on_info (infop, qo_join_new (infop,
						     join_type,
						     QO_JOINMETHOD_IDX_JOIN,
						     outer_plan,
						     inner_plan, &empty_terms
						     /* join_terms */ ,
						     &empty_terms
						     /* duj_terms */ ,
						     afj_terms,
						     &remaining_terms,
						     pinned_subqueries));
    }

  bitset_delset (&remaining_terms);
  bitset_delset (&empty_terms);
  bitset_delset (&kf_terms);
  bitset_delset (&range_terms);

  return n;
}				/* qo_generate_join_index_scan() */

/*
 * qo_generate_index_scan () - With index information, generates index scan plan
 *   return: nothing
 *   infop(in): pointer to QO_INFO (environment info node which holds plans)
 *   nodep(in): pointer to QO_NODE (node in the join graph)
 *   ni_entryp(in): pointer to QO_NODE_INDEX_ENTRY (node index entry)
 */
static void
qo_generate_index_scan (QO_INFO * infop, QO_NODE * nodep,
			QO_NODE_INDEX_ENTRY * ni_entryp)
{
  QO_INDEX_ENTRY *index_entryp;
  BITSET_ITERATOR iter;
  int i, t, nsegs, n;
  QO_PLAN *planp;
  BITSET range_terms;
  BITSET kf_terms;

  bitset_init (&range_terms, infop->env);
  bitset_init (&kf_terms, infop->env);

  /* pointer to QO_INDEX_ENTRY structure */
  index_entryp = (ni_entryp)->head;

  /* Generate index scan plan for this index. With single-column index,
     a plan will be generated for each terms that recorded in 'seg_terms'
     field of QO_INDEX_ENTRY structure. For example, "WHERE a>10 and a<20"
     will have two index scan plan, one with "a>10" and the other with
     "a<20". If we're dealing with multi-column index, which has more
     than or equal to one segs in 'seg_idx[]' array (nsegs>=1) or in
     'segs' field of QO_INDEX_ENTRY structure, we will generate a single
     plan with all terms recorded in all 'seg_terms[]' array. There will
     be only one member in the each 'seg_terms[]' array because only '='
     expression is allowed as to be indexed term in the case of
     multi-column index. This code should be changed when the '=' limitation
     of multi-column index is eliminated. */

  if (QO_ENTRY_MULTI_COL (index_entryp))
    {

      for (nsegs = 0; nsegs < index_entryp->nsegs; nsegs++)
	{
	  if (bitset_is_empty (&(index_entryp->seg_equal_terms[nsegs])))
	    {
	      if (!bitset_is_empty (&(index_entryp->seg_other_terms[nsegs])))
		{
		  /* include this term */
		  nsegs++;
		}

	      break;
	    }
	}			/* for (nsegs = 0; nsegs < index_entryp->nsegs; nsegs++) */

      for (i = 0; i < nsegs - 1; i++)
	{
	  bitset_add (&range_terms,
		      bitset_first_member (&
					   (index_entryp->
					    seg_equal_terms[i])));
	}

      /* for each terms associated with the last segment */
      for (t =
	   bitset_iterate (&(index_entryp->seg_equal_terms[nsegs - 1]),
			   &iter); t != -1; t = bitset_next_member (&iter))
	{
	  bitset_add (&range_terms, t);
	  bitset_assign (&kf_terms, &(QO_NODE_SARGS (nodep)));
	  bitset_difference (&kf_terms, &range_terms);
	  /* generate index scan plan */
	  planp = qo_index_scan_new (infop, nodep, ni_entryp,
				     &range_terms,
				     &kf_terms, &QO_NODE_SUBQUERIES (nodep));

	  n = qo_check_plan_on_info (infop, planp);

/* the following bug fix cause Performance Regression. so, temporary disable it
 * DO NOT DELETE ME
 */
#if 0
	  /*if (envvar_get("ENABLE_TEMP_JOIN")) */
	  {
	    /* temporary list file plan */
	    if (n != 0 && bitset_is_empty (&(QO_NODE_DEP_SET (nodep))))
	      {
		if (planp->top_rooted != true)
		  {
		    (void) qo_check_plan_on_info (infop,
						  qo_sort_new (planp,
							       QO_UNORDERED,
							       SORT_TEMP));
		  }
	      }
	  }
#endif

	  /* is it safe to ignore the result of qo_check_plan_on_info()? */
	  bitset_remove (&range_terms, t);
	}			/* for (t = ... ) */

      for (t =
	   bitset_iterate (&(index_entryp->seg_other_terms[nsegs - 1]),
			   &iter); t != -1; t = bitset_next_member (&iter))
	{
	  bitset_add (&range_terms, t);
	  bitset_assign (&kf_terms, &(QO_NODE_SARGS (nodep)));
	  bitset_difference (&kf_terms, &range_terms);
	  /* generate index scan plan */
	  planp = qo_index_scan_new (infop, nodep, ni_entryp,
				     &range_terms,
				     &kf_terms, &QO_NODE_SUBQUERIES (nodep));

	  n = qo_check_plan_on_info (infop, planp);

/* the following bug fix cause Performance Regression. so, temporary disable it
 * DO NOT DELETE ME
 */
#if 0
	  /*if (envvar_get("ENABLE_TEMP_JOIN")) */
	  {
	    /* temporary list file plan */
	    if (n != 0 && bitset_is_empty (&(QO_NODE_DEP_SET (nodep))))
	      {
		if (planp->top_rooted != true)
		  {
		    (void) qo_check_plan_on_info (infop,
						  qo_sort_new (planp,
							       QO_UNORDERED,
							       SORT_TEMP));
		  }
	      }
	  }
#endif

	  /* is it safe to ignore the result of qo_check_plan_on_info()? */
	  bitset_remove (&range_terms, t);
	}			/* for (t = ... ) */

    }
  else
    {				/* if (QO_ENTRY_MULTI_COL(index_entryp)) */

      /* for all segments covered by this index and found in
         'env.c:find_node_indexes()' */
      for (i = 0; i < index_entryp->nsegs; i++)
	{

	  /* for each terms associated with the segment */
	  for (t = bitset_iterate (&(index_entryp->seg_equal_terms[i]),
				   &iter);
	       t != -1; t = bitset_next_member (&iter))
	    {
	      bitset_add (&range_terms, t);
	      bitset_assign (&kf_terms, &(QO_NODE_SARGS (nodep)));
	      bitset_difference (&kf_terms, &range_terms);
	      /* generate index scan plan */
	      planp = qo_index_scan_new (infop, nodep, ni_entryp,
					 &range_terms,
					 &kf_terms,
					 &QO_NODE_SUBQUERIES (nodep));

	      n = qo_check_plan_on_info (infop, planp);

	      /*if (envvar_get("ENABLE_TEMP_JOIN")) */
	      {
		/* temporary list file plan */
		if (n != 0 && bitset_is_empty (&(QO_NODE_DEP_SET (nodep))))
		  {
		    if (planp->top_rooted != true)
		      {
			(void) qo_check_plan_on_info (infop,
						      qo_sort_new (planp,
								   QO_UNORDERED,
								   SORT_TEMP));
		      }
		  }
	      }

	      /* is it safe to ignore the result of qo_check_plan_on_info()? */
	      bitset_remove (&range_terms, t);
	    }			/* for (t = ... ) */

	  for (t = bitset_iterate (&(index_entryp->seg_other_terms[i]),
				   &iter);
	       t != -1; t = bitset_next_member (&iter))
	    {
	      bitset_add (&range_terms, t);
	      bitset_assign (&kf_terms, &(QO_NODE_SARGS (nodep)));
	      bitset_difference (&kf_terms, &range_terms);
	      /* generate index scan plan */
	      planp = qo_index_scan_new (infop, nodep, ni_entryp,
					 &range_terms,
					 &kf_terms,
					 &QO_NODE_SUBQUERIES (nodep));

	      n = qo_check_plan_on_info (infop, planp);

	      /*if (envvar_get("ENABLE_TEMP_JOIN")) */
	      {
		/* temporary list file plan */
		if (n != 0 && bitset_is_empty (&(QO_NODE_DEP_SET (nodep))))
		  {
		    if (planp->top_rooted != true)
		      {
			(void) qo_check_plan_on_info (infop,
						      qo_sort_new (planp,
								   QO_UNORDERED,
								   SORT_TEMP));
		      }
		  }
	      }

	      /* is it safe to ignore the result of qo_check_plan_on_info()? */
	      bitset_remove (&range_terms, t);
	    }			/* for (t = ... ) */

	}			/* for (i = 0; i < index_entryp->nsegs; i++) */

    }				/* if (QO_ENTRY_MULTI_COL(index_entryp)) */

  bitset_delset (&kf_terms);
  bitset_delset (&range_terms);
}


/*
 * qo_search_planner () -
 *   return:
 *   planner(in):
 */
static QO_PLAN *
qo_search_planner (QO_PLANNER * planner)
{
  int i, j, k;
  bool broken;
  QO_PLAN *plan;
  QO_NODE *node;
  QO_INFO *info;
  BITSET_ITERATOR si;
  int subq_idx;
  QO_SUBQUERY *subq;
  QO_PLANVEC *pv;
  QO_NODE_INDEX *node_index;
  QO_NODE_INDEX_ENTRY *ni_entry;
  QO_INDEX_ENTRY *index_entry;
  BITSET seg_terms;
  BITSET nodes, subqueries, remaining_subqueries;
  int join_info_bytes;

  bitset_init (&nodes, planner->env);
  bitset_init (&subqueries, planner->env);
  bitset_init (&remaining_subqueries, planner->env);

  planner->worst_plan = qo_worst_new (planner->env);
  planner->worst_info = qo_alloc_info (planner,
				       &nodes, &nodes, &nodes, QO_INFINITY);
  (planner->worst_plan)->info = planner->worst_info;
  (void) qo_plan_add_ref (planner->worst_plan);

  /*
   * At this point, N (and node), S (and seg), E (and edge), and
   * EQ (and eqclass) have been initialized; we now need to set up the
   * various info vectors.
   *
   * For the time being, we assume that N is never "too large", and we
   * go ahead and allocate the full join_info vector of M elements.
   */
  if (planner->N > 1)
    {
      planner->M =
	QO_PARTITION_M_OFFSET (&planner->partition[planner->P - 1]) +
	QO_JOIN_INFO_SIZE (&planner->partition[planner->P - 1]);

      join_info_bytes = planner->M * sizeof (QO_INFO *);

      if (join_info_bytes > 0)
	{
	  planner->join_info =
	    (QO_INFO **) BALLOCATE (planner->env, join_info_bytes);
	}
      else
	{
	  /* overflow. N is too large */
	  planner->join_info = NULL;
	}

      if (planner->join_info == NULL)
	{
	  /* memory allocation fail */
	  plan = NULL;
	  goto end;
	}

      memset (planner->join_info, 0, join_info_bytes);
    }

  bitset_assign (&remaining_subqueries, &(planner->all_subqueries));

  /*
   * Add appropriate scan plans for each node.
   */
  planner->node_info = NALLOCATE (planner->env, QO_INFO *, planner->N);
  for (i = 0; i < (signed) planner->N; ++i)
    {
      node = &planner->node[i];
      BITSET_CLEAR (nodes);
      bitset_add (&nodes, i);
      planner->node_info[i] = info =
	qo_alloc_info (planner,
		       &nodes,
		       &QO_NODE_SARGS (node),
		       &QO_NODE_EQCLASSES (node),
		       QO_NODE_SELECTIVITY (node) *
		       (double) QO_NODE_NCARD (node));

      /*
       * Identify all of the subqueries that can be pinned on this
       * node.  Remember, a subquery can be pinned here only if it
       * isn't used in some term that isn't also pinned here, i.e.,
       * only if it isn't used in any term, or if the term it is used
       * in is pinned here as a sarg.
       *
       * This code is nearly the same as that in planner_visit() and
       * really shouldn't be duplicated here.  This is further evidence
       * that the distinction between single-node plans and multi-node
       * plans (i.e., node_info and join_info) isn't working well.
       *
       */
      BITSET_CLEAR (subqueries);
      for (subq_idx = bitset_iterate (&remaining_subqueries, &si);
	   subq_idx != -1; subq_idx = bitset_next_member (&si))
	{
	  subq = &planner->subqueries[subq_idx];
	  if (bitset_is_empty (&subq->nodes) ||	/* uncorrelated */
	      (bitset_subset (&nodes, &(subq->nodes)) &&	/* correlated */
	       bitset_subset (&(QO_NODE_SARGS (node)), &(subq->terms))))
	    {
	      bitset_add (&subqueries, subq_idx);
	      bitset_remove (&remaining_subqueries, subq_idx);
	    }
	}
      bitset_assign (&(QO_NODE_SUBQUERIES (node)), &subqueries);

      /*
       * Start with a sequential scan plan for each node.
       */
      (void) qo_check_plan_on_info (info,
				    qo_seq_scan_new (info, node,
						     &subqueries));
    }

  /*
   * Check all of the terms to determine which are eligible to serve as
   * index scans, try them to see if they are cheaper than any of the
   * segment scans we just created.
   */
  for (i = 0; i < (signed) planner->N; i++)
    {
      node = &planner->node[i];
      info = planner->node_info[QO_NODE_IDX (node)];

      node_index = QO_NODE_INDEXES (node);

      /*
       *  It is possible that this node will not have indexes.  This would
       *  happen (for instance) if the node represented a derived table.
       *  There is no purpose looking for index scans for a node without
       *  indexes so skip the search in this case.
       */
      if (node_index != NULL)
	{
	  bitset_init (&seg_terms, planner->env);

	  for (j = 0; j < QO_NI_N (node_index); j++)
	    {
	      ni_entry = QO_NI_ENTRY (node_index, j);
	      index_entry = (ni_entry)->head;
	      BITSET_CLEAR (seg_terms);
	      for (k = 0; k < index_entry->nsegs; k++)
		{
		  if (bitset_is_empty (&(index_entry->seg_equal_terms[k])) &&
		      bitset_is_empty (&(index_entry->seg_other_terms[k])))
		    {
		      break;
		    }
		  bitset_union (&seg_terms,
				&(index_entry->seg_equal_terms[k]));
		  bitset_union (&seg_terms,
				&(index_entry->seg_other_terms[k]));
		}
	      if (bitset_intersects (&(QO_NODE_SARGS (node)), &seg_terms))
		{
		  qo_generate_index_scan (info, node, ni_entry);
		}
	    }

	  bitset_delset (&seg_terms);
	}

      pv = &info->best_no_order;
      for (j = 0; j < pv->nplans; j++)
	{
	  plan = pv->plan[j];
	  if (plan->plan_type == QO_PLANTYPE_SORT)
	    {
	      break;		/* found temp list plan */
	    }
	}

      if (j >= pv->nplans)
	{			/* not found temp list plan */
	  plan = qo_find_best_plan_on_info (info, QO_UNORDERED, 1.0);

	  if (plan->plan_type == QO_PLANTYPE_SCAN &&
	      plan->plan_un.scan.scan_method == QO_SCANMETHOD_SEQ_SCAN)
	    {
	      if (plan->top_rooted != true)
		{
		  /*if (envvar_get("ENABLE_TEMP_JOIN")) */
		  {
		    /* temporary list file plan */
		    if (bitset_is_empty (&(QO_NODE_DEP_SET (node))))
		      (void) qo_check_plan_on_info (info,
						    qo_sort_new (plan,
								 QO_UNORDERED,
								 SORT_TEMP));
		  }
		}
	    }
	}

    }

  /*
   * Now remaining_subqueries should contain only entries that depend
   * on more than one class.
   */

  if (planner->P > 1)
    {
      planner->cp_info = NALLOCATE (planner->env, QO_INFO *, planner->P);
      for (i = 0; i < (signed) planner->P; i++)
	{
	  planner->cp_info[i] = NULL;
	}
    }

  broken = false;
  for (i = 0; i < (signed) planner->P; ++i)
    {
      /*
       * If any partition fails, give up.  We'll have to build an
       * unoptimized plan elsewhere.
       */
      if (qo_search_partition (planner,
			       &planner->partition[i],
			       QO_UNORDERED, &remaining_subqueries) == NULL)
	{
	  int j;
	  for (j = 0; j < i; ++j)
	    {
	      qo_plan_del_ref (planner->partition[j].plan);
	    }
	  broken = true;
	  break;
	}
    }
  plan =
    broken ? NULL : qo_combine_partitions (planner, &remaining_subqueries);

end:
  bitset_delset (&nodes);
  bitset_delset (&subqueries);
  bitset_delset (&remaining_subqueries);

  return plan;
}


/*
 * qo_clean_planner () -
 *   return:
 *   planner(in):
 */
static void
qo_clean_planner (QO_PLANNER * planner)
{
  /*
   * This cleans up everything that isn't needed for the surviving
   * plan.  In particular, it will give back all excess QO_PLAN
   * structures that we have allocated during the search.  All
   * detachable QO_INFO should already have been detached, so we don't
   * worry about them here.
   */
  planner->cleanup_needed = false;
  bitset_delset (&(planner->all_subqueries));
  bitset_delset (&(planner->final_segs));
  qo_plans_teardown (planner->env);
}

/* Tables considered at a time during a join
 * -------------------------------------------
 * Tables joined | Tables considered at a time
 * --------------+----------------------------
 *  4..25        | 4
 * 26..37        | 3
 * 38..          | 2
 * -------------------------------------------
 * Refer Sybase Ataptive Server
 */

/*
 * qo_search_partition_join () -
 *   return:
 *   planner(in):
 *   partition(in):
 *   remaining_subqueries(in):
 */
static QO_INFO *
qo_search_partition_join (QO_PLANNER * planner,
			  QO_PARTITION * partition,
			  BITSET * remaining_subqueries)
{
  int i, nodes_cnt, node_idx;
  PT_NODE *tree;
  PT_HINT_ENUM hint;
  QO_TERM *term;
  QO_NODE *node;
  int num_path_inner;
  QO_INFO *visited_info;
  BITSET visited_nodes;
  BITSET visited_rel_nodes;
  BITSET visited_terms;
  BITSET first_nodes;
  BITSET nested_path_nodes;
  BITSET remaining_nodes;
  BITSET remaining_terms;

  bitset_init (&visited_nodes, planner->env);
  bitset_init (&visited_rel_nodes, planner->env);
  bitset_init (&visited_terms, planner->env);
  bitset_init (&first_nodes, planner->env);
  bitset_init (&nested_path_nodes, planner->env);
  bitset_init (&remaining_nodes, planner->env);
  bitset_init (&remaining_terms, planner->env);

  /* include useful nodes */
  bitset_assign (&remaining_nodes, &(QO_PARTITION_NODES (partition)));
  nodes_cnt = bitset_cardinality (&remaining_nodes);

  num_path_inner = 0;		/* init */

  /* include useful terms */
  for (i = 0; i < (signed) planner->T; i++)
    {
      term = &planner->term[i];
      if (QO_TERM_CLASS (term) == QO_TC_TOTALLY_AFTER_JOIN)
	{
	  continue;		/* skip and go ahead */
	}

      if (bitset_subset (&remaining_nodes, &(QO_TERM_NODES (term))))
	{
	  bitset_add (&remaining_terms, i);
	  if (QO_TERM_CLASS (term) == QO_TC_PATH)
	    {
	      num_path_inner++;	/* path-expr sargs */
	    }
	}

    }				/* for (i = ...) */

  /* set hint info */
  tree = QO_ENV_PT_TREE (planner->env);
  hint = tree->info.query.q.select.hint;

  /* set #tables consider at a time */
  if (num_path_inner)
    {				/* inner join type path term exist; WHERE x.y.z = ? */
      planner->join_unit = nodes_cnt;	/* give up */
    }
  else
    {
      planner->join_unit = (hint & PT_HINT_ORDERED) ? nodes_cnt :
	(nodes_cnt <= 25) ? MIN (4, nodes_cnt) : (nodes_cnt <= 37) ? 3 : 2;
    }

  if (num_path_inner || hint & PT_HINT_ORDERED)
    {
      ;				/* skip and go ahead */
    }
  else
    {
      int j, k;
      QO_INFO *i_info, *j_info;
      QO_PLAN *i_plan, *j_plan;
      QO_NODE *i_node, *j_node;
      PT_NODE *entity;
      bool found_edge;
      BITSET_ITERATOR bi, bj, bk;
      QO_PLAN_COMPARE_RESULT cmp;
      BITSET derived_nodes;

      bitset_init (&derived_nodes, planner->env);

      for (i = bitset_iterate (&remaining_nodes, &bi); i != -1;
	   i = bitset_next_member (&bi))
	{
	  i_node = QO_ENV_NODE (planner->env, i);
	  /* node dependency check; emptyness check
	   */
	  if (!bitset_is_empty (&(QO_NODE_DEP_SET (i_node))))
	    {
	      continue;
	    }
	  if (!bitset_is_empty (&(QO_NODE_OUTER_DEP_SET (i_node))))
	    {
	      continue;
	    }

	  entity = QO_NODE_ENTITY_SPEC (i_node);
	  /* do not check plan for inline view */
	  if (entity->info.spec.derived_table)
	    {			/* inline view */
	      bitset_add (&derived_nodes, i);
	      continue;		/* OK */
	    }

	  /* current prefix has only one node */
	  i_info = planner->node_info[QO_NODE_IDX (i_node)];

	  if (bitset_is_empty (&first_nodes))
	    {			/* the first time */
	      bitset_add (&first_nodes, i);
	      continue;		/* OK */
	    }

	  i_plan = qo_find_best_plan_on_info (i_info, QO_UNORDERED, 1.0);

	  for (j = bitset_iterate (&first_nodes, &bj); j != -1;
	       j = bitset_next_member (&bj))
	    {
	      j_node = QO_ENV_NODE (planner->env, j);
	      /* current prefix has only one node */
	      j_info = planner->node_info[QO_NODE_IDX (j_node)];

	      j_plan = qo_find_best_plan_on_info (j_info, QO_UNORDERED, 1.0);

	      cmp = qo_plan_cmp (j_plan, i_plan);

	      if (cmp == PLAN_COMP_LT)
		{		/* plan is worse */
		  if (QO_NODE_TCARD (j_node) <= 1)
		    {		/* one page heap file */
		      ;		/* j_plan is always winner */
		    }
		  else
		    {
		      /* check for info cardinality */
		      if (i_info->cardinality < j_info->cardinality + 1.0)
			{
			  continue;	/* do not skip out smaller card */
			}

		      if (i_plan->plan_type == QO_PLANTYPE_SCAN &&
			  i_plan->plan_un.scan.scan_method ==
			  QO_SCANMETHOD_INDEX_SCAN)
			{
			  continue;	/* do not skip out index scan plan */
			}
		    }
		  break;	/* do not add i_plan to the first_nodes */
		}
	      else if (cmp == PLAN_COMP_GT)
		{		/* found new first */
		  if (QO_NODE_TCARD (i_node) <= 1)
		    {		/* one page heap file */
		      ;		/* i_plan is always winner */
		    }
		  else
		    {
		      /* check for info cardinality */
		      if (j_info->cardinality < i_info->cardinality + 1.0)
			{
			  continue;	/* do not skip out smaller card */
			}

		      if (j_plan->plan_type == QO_PLANTYPE_SCAN &&
			  j_plan->plan_un.scan.scan_method ==
			  QO_SCANMETHOD_INDEX_SCAN)
			{
			  continue;	/* do not skip out index scan plan */
			}
		    }

		  /* check for join-connectivity of i_node to j_node
		   */
		  found_edge = false;	/* init */
		  for (k = bitset_iterate (&remaining_terms, &bk); k != -1;
		       k = bitset_next_member (&bk))
		    {

		      term = QO_ENV_TERM (planner->env, k);

		      if (BITSET_MEMBER
			  (QO_TERM_NODES (term), QO_NODE_IDX (j_node))
			  && BITSET_MEMBER (QO_TERM_NODES (term),
					    QO_NODE_IDX (i_node)))
			{
			  if (QO_TERM_CLASS (term) == QO_TC_PATH)
			    {
			      /* need to check the direction; head -> tail */
			      if (QO_NODE_IDX (QO_TERM_TAIL (term)) ==
				  QO_NODE_IDX (j_node))
				{
				  found_edge = true;
				}	/* else { not edge } */
			    }
			  else if (QO_IS_EDGE_TERM (term))
			    {
			      found_edge = true;
			    }

			  if (found_edge)
			    {
			      break;
			    }
			}
		    }		/* for (k = ...) */

		  if (found_edge)
		    {
		      /* delete j_plan from first_node */
		      bitset_remove (&first_nodes, j);
		    }
		}
	    }			/* for (j = ...) */

	  if (j == -1)
	    {			/* add new plan */
	      bitset_add (&first_nodes, i);
	    }

	}			/* for (i = ...) */

      /* finally, add derived nodes to the first nodes */
      bitset_union (&first_nodes, &derived_nodes);

      bitset_delset (&derived_nodes);
    }

  /* STEP 1: do join search with visited nodes */

  node = NULL;			/* init */

  while (1)
    {
      node_idx = -1;		/* init */
      (void) planner_permutate (planner, partition, hint, node,	/* previous head node */
				&visited_nodes, &visited_rel_nodes,
				&visited_terms, &first_nodes,
				&nested_path_nodes, &remaining_nodes,
				&remaining_terms, remaining_subqueries,
				num_path_inner,
				(planner->join_unit < nodes_cnt) ? &node_idx
				/* partial join search */
				: NULL /*   total join search */ );
      if (planner->best_info)
	{			/* OK */
	  break;		/* found best total join plan */
	}

      if (planner->join_unit >= nodes_cnt)
	{
	  /* something wrong for total join search */
	  break;		/* give up */
	}
      else
	{
	  if (node_idx == -1)
	    {
	      /* something wrong for partial join search;
	       * rollback and retry total join search */
	      bitset_union (&remaining_nodes, &visited_nodes);
	      bitset_union (&remaining_terms, &visited_terms);

	      BITSET_CLEAR (first_nodes);
	      BITSET_CLEAR (nested_path_nodes);
	      BITSET_CLEAR (visited_nodes);
	      BITSET_CLEAR (visited_rel_nodes);
	      BITSET_CLEAR (visited_terms);

	      /* set #tables consider at a time */
	      planner->join_unit = nodes_cnt;

	      /* STEP 2: do total join search without visited nodes */

	      continue;
	    }
	}

      /* at here, still do partial join search
       */

      /* extract the outermost nodes at this join level */
      node = QO_ENV_NODE (planner->env, node_idx);
      bitset_add (&visited_nodes, node_idx);
      bitset_add (&visited_rel_nodes, QO_NODE_REL_IDX (node));
      bitset_remove (&remaining_nodes, node_idx);

      /* extract already used terms at this join level */
      if (bitset_cardinality (&visited_nodes) == 1)
	{
	  /* current prefix has only one node */
	  visited_info = planner->node_info[node_idx];
	}
      else
	{
	  /* current prefix has two or more nodes */
	  visited_info =
	    planner->
	    join_info[QO_INFO_INDEX
		      (QO_PARTITION_M_OFFSET (partition), visited_rel_nodes)];
	}

      if (visited_info == NULL)
	{			/* something wrong */
	  break;		/* give up */
	}

      bitset_assign (&visited_terms, &(visited_info->terms));
      bitset_difference (&remaining_terms, &(visited_info->terms));

      planner->join_unit++;	/* increase join unit level */

    }				/* while (1) */

  bitset_delset (&visited_rel_nodes);
  bitset_delset (&visited_nodes);
  bitset_delset (&visited_terms);
  bitset_delset (&first_nodes);
  bitset_delset (&nested_path_nodes);
  bitset_delset (&remaining_nodes);
  bitset_delset (&remaining_terms);

  return planner->best_info;
}

/*
 * qo_search_partition () -
 *   return:
 *   planner(in):
 *   partition(in):
 *   order(in):
 *   remaining_subqueries(in):
 */
static QO_PLAN *
qo_search_partition (QO_PLANNER * planner,
		     QO_PARTITION * partition,
		     QO_EQCLASS * order, BITSET * remaining_subqueries)
{
  int i, nodes_cnt;

  nodes_cnt = bitset_cardinality (&(QO_PARTITION_NODES (partition)));

  /* nodes are multi if there is a join to be done. If not,
   * this is just a degenerate search to determine which of the indexes
   * (if available) to use for the (single) class involved in the query.
   */
  if (nodes_cnt > 1)
    {
      planner->best_info = qo_search_partition_join (planner,
						     partition,
						     remaining_subqueries);
    }
  else
    {
      QO_NODE *node;

      i = bitset_first_member (&(QO_PARTITION_NODES (partition)));
      node = QO_ENV_NODE (planner->env, i);
      planner->best_info = planner->node_info[QO_NODE_IDX (node)];
    }

  if (planner->env->dump_enable)
    {
      qo_dump_planner_info (planner, partition, stdout);
    }

  QO_PARTITION_PLAN (partition) =
    planner->best_info
    ?
    qo_plan_finalize (qo_find_best_plan_on_info
		      (planner->best_info, order, 1.0)) : NULL;

  /* Now clean up after ourselves.  Free all of the plans that aren't
   * part of the winner for this partition, but retain the nodes:
   * they contain information that the winning plan requires.
   */

  if (nodes_cnt > 1)
    {
      QO_INFO *info;

      for (info = planner->info_list; info; info = info->next)
	{
	  if (bitset_subset
	      (&(QO_PARTITION_NODES (partition)), &(info->nodes)))
	    {
	      qo_detach_info (info);
	    }
	}
    }
  else
    {				/* single class */
      for (i = 0; i < (signed) planner->N; i++)
	{
	  if (BITSET_MEMBER (QO_PARTITION_NODES (partition), i))
	    {
	      qo_detach_info (planner->node_info[i]);
	    }
	}
    }

  return QO_PARTITION_PLAN (partition);
}

/*
 * sort_partitions () -
 *   return:
 *   planner(in):
 */
static void
sort_partitions (QO_PLANNER * planner)
{
  /*
   * Order the partitions according to the dependency information in
   * them:  if a partition depends on some other partition, it must be
   * the inner term of a cp plan whose outer term contains that other
   * partition.
   *
   * Just do a bubble sort here; you never have more than a handful of
   * partitions, and even if you do, the query is going to be a monster
   * whose execution time will swamp anything you do here.
   *
   * This scrambles the idx values of the partitions, but we shouldn't
   * be relying on them anymore anyway.
   */
  int i, j;
  QO_PARTITION *i_part, *j_part;
  QO_PARTITION tmp;

  for (i = 1; i < (signed) planner->P; ++i)
    {
      i_part = &planner->partition[i];
      for (j = 0; j < i; ++j)
	{
	  j_part = &planner->partition[j];
	  /*
	   * If the higher partition (i_part) supplies something that
	   * the lower partition (j_part) needs, swap them.
	   */
	  if (bitset_intersects (&(QO_PARTITION_NODES (i_part)),
				 &(QO_PARTITION_DEPENDENCIES (j_part))))
	    {
	      tmp = *i_part;
	      *i_part = *j_part;
	      *j_part = tmp;
	    }
	}
    }

}

/*
 * qo_combine_partitions () -
 *   return:
 *   planner(in):
 *   reamining_subqueries(in):
 */
static QO_PLAN *
qo_combine_partitions (QO_PLANNER * planner, BITSET * reamining_subqueries)
{
  QO_PARTITION *partition = planner->partition;
  QO_PLAN *plan, *t_plan;
  BITSET nodes;
  BITSET terms;
  BITSET eqclasses;
  BITSET sarged_terms;
  BITSET subqueries;
  BITSET_ITERATOR bi;
  int i, t, s;
  double cardinality;
  QO_PLAN *next_plan;

  bitset_init (&nodes, planner->env);
  bitset_init (&terms, planner->env);
  bitset_init (&eqclasses, planner->env);
  bitset_init (&sarged_terms, planner->env);
  bitset_init (&subqueries, planner->env);

  /*
   * Order the partitions by dependency information.  We could probably
   * undertake a more sophisticated search here that takes the
   * remaining sargable terms into account, but this code is probably
   * hardly ever exercised anyway, and this query is already known to
   * be a loser, so don't worry about it.
   */
  sort_partitions (planner);

  /*
   * For each partition, the plan that was retained was finalized
   * (i.e., it's refcount was bumped up by one) to make sure that it
   * wouldn't get collected as we cleaned up all of the info nodes.
   * Now, however, we're going to put all of those plans into cp nodes,
   * which will also bump the refcounts.  We need to undo the
   * finalization of the incoming plans here so that the ref counts are
   * right when we finish.  If we don't we won't ever reclaim the plan
   * nodes.
   *
   * Don't use qo_plan_del_ref(); that will collect the node.
   */
  for (i = 0; i < (signed) planner->P; ++i)
    (QO_PARTITION_PLAN (&planner->partition[i]))->refcount--;

  /*
   * DON'T initialize these until after the sorting is done.
   */
  plan = QO_PARTITION_PLAN (partition);
  cardinality = (plan->info)->cardinality;

  bitset_assign (&nodes, &((plan->info)->nodes));
  bitset_assign (&terms, &((plan->info)->terms));
  bitset_assign (&eqclasses, &((plan->info)->eqclasses));

  for (++partition, i = 1; i < (signed) planner->P; ++partition, ++i)
    {
      next_plan = QO_PARTITION_PLAN (partition);

      bitset_union (&nodes, &((next_plan->info)->nodes));
      bitset_union (&terms, &((next_plan->info)->terms));
      bitset_union (&eqclasses, &((next_plan->info)->eqclasses));
      cardinality *= (next_plan->info)->cardinality;

      planner->cp_info[i] = qo_alloc_info (planner,
					   &nodes,
					   &terms, &eqclasses, cardinality);

      for (t = planner->E; t < (signed) planner->T; ++t)
	{
	  if (!bitset_is_empty (&(QO_TERM_NODES (&planner->term[t]))) &&
	      !BITSET_MEMBER (terms, t) &&
	      bitset_subset (&nodes, &(QO_TERM_NODES (&planner->term[t]))) &&
	      (QO_TERM_CLASS (&planner->term[t]) != QO_TC_TOTALLY_AFTER_JOIN))
	    bitset_add (&sarged_terms, t);
	}

      BITSET_CLEAR (subqueries);
      for (s = bitset_iterate (reamining_subqueries, &bi);
	   s != -1; s = bitset_next_member (&bi))
	{
	  QO_SUBQUERY *subq = &planner->subqueries[s];
	  if (bitset_subset (&nodes, &(subq->nodes))
	      && bitset_subset (&sarged_terms, &(subq->terms)))
	    {
	      bitset_add (&subqueries, s);
	      bitset_remove (reamining_subqueries, s);
	    }
	}

      plan = qo_cp_new (planner->cp_info[i], plan, next_plan,
			&sarged_terms, &subqueries);
      qo_detach_info (planner->cp_info[i]);
      BITSET_CLEAR (sarged_terms);
    }

  /*
   * Now finalize the topmost node of the tree.
   */
  qo_plan_finalize (plan);

  /*
   * Now make sure that we account for any terms that appear not to
   * belong to *any* partition; these will be completely
   * "self-contained" terms such as
   *
   *  exists (select name from person)
   *
   *  or inst_num() < 10
   *
   * Admittedly, these are usually degenerate occurrences, but they do
   * sometimes crop up.  We will simply attach them to the top level
   * plan node for now.
   */
  for (i = planner->E; i < (signed) planner->T; ++i)
    {
      if (bitset_is_empty (&(QO_TERM_NODES (&planner->term[i]))))
	bitset_add (&sarged_terms, i);
    }

  /* skip empty sort plan */
  for (t_plan = plan;
       t_plan && t_plan->plan_type == QO_PLANTYPE_SORT;
       t_plan = t_plan->plan_un.sort.subplan)
    {
      if (!bitset_is_empty (&(t_plan->sarged_terms)))
	{
	  break;
	}
    }

  if (t_plan)
    {
      bitset_union (&(t_plan->sarged_terms), &sarged_terms);
    }
  else
    {
      /* invalid plan structure. occur error */
      qo_plan_discard (plan);
      plan = NULL;
    }

  bitset_delset (&nodes);
  bitset_delset (&terms);
  bitset_delset (&eqclasses);
  bitset_delset (&sarged_terms);
  bitset_delset (&subqueries);

  return plan;
}
