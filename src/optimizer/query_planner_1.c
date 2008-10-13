/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * plan.c - Plan descriptors
 * TODO: merge this file into query_planner.c
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#if !defined(WINDOWS)
#include <values.h>
#endif /* !WINDOWS */

#include "ustring.h"
#include "system_parameter.h"
#include "query_planner_1.h"
#include "query_graph_1.h"
#include "query_planner_2.h"
#include "query_graph_2.h"
#include "parser.h"
#include "msgexec.h"
#include "intl.h"
#include "common.h"
#include "xasl_generation_2.h"
#include "dbval.h"

#define INDENT_INCR		4
#define INDENT_FMT		"%*c"
#define TITLE_WIDTH		7
#define TITLE_FMT		"%-" __STR(TITLE_WIDTH) "s"
#define INDENTED_TITLE_FMT	INDENT_FMT TITLE_FMT
#define __STR(n)		__VAL(n)
#define __VAL(n)		#n

#define VALID_INNER(plan)	(plan->well_rooted || \
				 (plan->plan_type == QO_PLANTYPE_SORT))

#define FUDGE_FACTOR 0.7
#define ISCAN_OVERHEAD_FACTOR   1.2
#define TEMP_SETUP_COST 5.0
#define NONGROUPED_SCAN_COST 0.1

#define	qo_scan_walk	qo_generic_walk
#define	qo_worst_walk	qo_generic_walk

#define	qo_generic_free	NULL
#define	qo_sort_free	qo_generic_free
#define	qo_follow_free	qo_generic_free
#define	qo_worst_free	qo_generic_free

static int qo_plans_allocated;
static int qo_plans_deallocated;
static int qo_plans_malloced;
static int qo_plans_demalloced;
static int qo_accumulating_plans;
static int qo_next_tmpfile;

static QO_PLAN *qo_plan_free_list;

/* moved to plan.h (used in planner.c)
static QO_PLAN	*qo_plan_malloc	(QO_ENV *);
*/

static QO_PLAN *qo_scan_new (QO_INFO *, QO_NODE *, QO_SCANMETHOD, BITSET *);
static void qo_scan_free (QO_PLAN *);
static void qo_join_free (QO_PLAN *);

static void qo_scan_fprint (QO_PLAN *, FILE *, int);
static void qo_sort_fprint (QO_PLAN *, FILE *, int);
static void qo_join_fprint (QO_PLAN *, FILE *, int);
static void qo_follow_fprint (QO_PLAN *, FILE *, int);
static void qo_worst_fprint (QO_PLAN *, FILE *, int);

static void qo_scan_info (QO_PLAN *, FILE *, int);
static void qo_sort_info (QO_PLAN *, FILE *, int);
static void qo_join_info (QO_PLAN *, FILE *, int);
static void qo_follow_info (QO_PLAN *, FILE *, int);
static void qo_worst_info (QO_PLAN *, FILE *, int);

static void qo_plan_lite_print (QO_PLAN *, FILE *, int);
static void qo_plan_del_ref_func (QO_PLAN * plan, void *ignore);

static void qo_generic_walk (QO_PLAN *, void (*)(QO_PLAN *, void *), void *,
			     void (*)(QO_PLAN *, void *), void *);
static void qo_sort_walk (QO_PLAN *, void (*)(QO_PLAN *, void *), void *,
			  void (*)(QO_PLAN *, void *), void *);
static void qo_join_walk (QO_PLAN *, void (*)(QO_PLAN *, void *), void *,
			  void (*)(QO_PLAN *, void *), void *);
static void qo_follow_walk (QO_PLAN *, void (*)(QO_PLAN *, void *), void *,
			    void (*)(QO_PLAN *, void *), void *);

static void qo_plan_compute_cost (QO_PLAN *);
static void qo_plan_compute_subquery_cost (PT_NODE *, double *, double *);

static void qo_sscan_cost (QO_PLAN *);
static void qo_iscan_cost (QO_PLAN *);
static void qo_sort_cost (QO_PLAN *);
static void qo_mjoin_cost (QO_PLAN *);
static void qo_follow_cost (QO_PLAN *);
static void qo_worst_cost (QO_PLAN *);
static void qo_zero_cost (QO_PLAN *);

static QO_PLAN *qo_top_plan_new (QO_PLAN *);

static double log3 (double);

static QO_PLAN_VTBL qo_seq_scan_plan_vtbl = {
  "sscan",
  qo_scan_fprint,
  qo_scan_walk,
  qo_scan_free,
  qo_sscan_cost,
  qo_sscan_cost,
  qo_scan_info,
  "Sequential scan"
};

static QO_PLAN_VTBL qo_index_scan_plan_vtbl = {
  "iscan",
  qo_scan_fprint,
  qo_scan_walk,
  qo_scan_free,
  qo_iscan_cost,
  qo_iscan_cost,
  qo_scan_info,
  "Index scan"
};

static QO_PLAN_VTBL qo_sort_plan_vtbl = {
  "temp",
  qo_sort_fprint,
  qo_sort_walk,
  qo_sort_free,
  qo_sort_cost,
  qo_sort_cost,
  qo_sort_info,
  "Sort"
};

static QO_PLAN_VTBL qo_nl_join_plan_vtbl = {
  "nl-join",
  qo_join_fprint,
  qo_join_walk,
  qo_join_free,
  qo_nljoin_cost,
  qo_nljoin_cost,
  qo_join_info,
  "Nested-loop join"
};

static QO_PLAN_VTBL qo_idx_join_plan_vtbl = {
  "idx-join",
  qo_join_fprint,
  qo_join_walk,
  qo_join_free,
  qo_nljoin_cost,
  qo_nljoin_cost,
  qo_join_info,
  "Correlated-index join"
};

static QO_PLAN_VTBL qo_merge_join_plan_vtbl = {
  "m-join",
  qo_join_fprint,
  qo_join_walk,
  qo_join_free,
  qo_mjoin_cost,
  qo_mjoin_cost,
  qo_join_info,
  "Merge join"
};

static QO_PLAN_VTBL qo_follow_plan_vtbl = {
  "follow",
  qo_follow_fprint,
  qo_follow_walk,
  qo_follow_free,
  qo_follow_cost,
  qo_follow_cost,
  qo_follow_info,
  "Object fetch"
};

static QO_PLAN_VTBL qo_set_follow_plan_vtbl = {
  "set_follow",
  qo_follow_fprint,
  qo_follow_walk,
  qo_follow_free,
  qo_follow_cost,
  qo_follow_cost,
  qo_follow_info,
  "Set fetch"
};

static QO_PLAN_VTBL qo_worst_plan_vtbl = {
  "worst",
  qo_worst_fprint,
  qo_worst_walk,
  qo_worst_free,
  qo_worst_cost,
  qo_worst_cost,
  qo_worst_info,
  "Bogus"
};

QO_PLAN_VTBL *all_vtbls[] = {
  &qo_seq_scan_plan_vtbl,
  &qo_index_scan_plan_vtbl,
  &qo_sort_plan_vtbl,
  &qo_nl_join_plan_vtbl,
  &qo_idx_join_plan_vtbl,
  &qo_merge_join_plan_vtbl,
  &qo_follow_plan_vtbl,
  &qo_set_follow_plan_vtbl,
  &qo_worst_plan_vtbl
};

/*
 * log3 () -
 *   return:
 *   n(in):
 */
static double
log3 (double n)
{
  static int initialized = 0;
  static double ln3;


  if (!initialized)
    {
      /*
       * I could check ln3 against 0, but I prefer to avoid the
       * floating point loads and comparison.
       */
      ln3 = log (3.0);
      initialized++;
    }

  return log (n) / ln3;
}

/*
 * qo_plan_malloc () -
 *   return:
 *   env(in):
 */
QO_PLAN *
qo_plan_malloc (QO_ENV * env)
{
  QO_PLAN *plan;

  ++qo_plans_allocated;
  if (qo_plan_free_list)
    {
      plan = qo_plan_free_list;
      qo_plan_free_list = plan->plan_un.free.link;
    }
  else
    {
      ++qo_plans_malloced;
      plan = ALLOCATE (env, QO_PLAN);
    }

  bitset_init (&(plan->sarged_terms), env);
  bitset_init (&(plan->subqueries), env);

  return plan;
}


/*
 * qo_term_string () -
 *   return:
 *   term(in):
 */
static const char *
qo_term_string (QO_TERM * term)
{
  static char buf[257];
  char *p;
  BITSET_ITERATOR bi;
  int i;
  QO_ENV *env;
  const char *separator;

  env = QO_TERM_ENV (term);

  switch (QO_TERM_CLASS (term))
    {

    case QO_TC_DEP_LINK:
      sprintf (buf, "table(");
      p = buf + strlen (buf);
      separator = "";
      for (i = bitset_iterate (&(QO_NODE_DEP_SET (QO_TERM_TAIL (term))), &bi);
	   i != -1; i = bitset_next_member (&bi))
	{
	  sprintf (p, "%s%s", separator, QO_NODE_NAME (QO_ENV_NODE (env, i)));
	  p = buf + strlen (buf);
	  separator = ",";
	}
      sprintf (p, ") -> %s", QO_NODE_NAME (QO_TERM_TAIL (term)));
      p = buf;
      break;

    case QO_TC_DEP_JOIN:
      p = buf;
      sprintf (p, "dep-join(%s,%s)",
	       QO_NODE_NAME (QO_TERM_HEAD (term)),
	       QO_NODE_NAME (QO_TERM_TAIL (term)));
      break;

    case QO_TC_DUMMY_JOIN:
      p = buf;
      sprintf (p, "dummy(%s,%s)",
	       QO_NODE_NAME (QO_TERM_HEAD (term)),
	       QO_NODE_NAME (QO_TERM_TAIL (term)));
      break;

    default:
      {
	PARSER_CONTEXT *parser = QO_ENV_PARSER (QO_TERM_ENV (term));
	PT_PRINT_VALUE_FUNC saved_func = parser->print_db_value;
	/* in order to print auto parameterized values */
	parser->print_db_value = pt_print_node_value;
	p = parser_print_tree (parser, QO_TERM_PT_EXPR (term));
	parser->print_db_value = saved_func;
      }
    }

  return p;
}


/*
 * qo_plan_compute_cost () -
 *   return:
 *   plan(in):
 */
static void
qo_plan_compute_cost (QO_PLAN * plan)
{
  QO_ENV *env;
  QO_SUBQUERY *subq;
  PT_NODE *query;
  double temp_cpu_cost, temp_io_cost;
  double subq_cpu_cost, subq_io_cost;
  int i;
  BITSET_ITERATOR iter;

  /* When computing the cost for a WORST_PLAN, we'll get in here
     without a backing info node; just work around it. */
  env = plan->info ? (plan->info)->env : NULL;
  subq_cpu_cost = subq_io_cost = 0.0;

  /* Compute the costs for all of the subqueries. Each of the pinned
     subqueries is intended to be evaluated once for each row produced
     by this plan; the cost of each such evaluation in the fixed cost
     of the subquery plus one trip through the result, i.e.,

     QO_PLAN_FIXED_COST(subplan) + QO_PLAN_ACCESS_COST(subplan)

     The cost info for the subplan has (probably) been squirreled away
     in a QO_SUMMARY structure reachable from the original select node. */

  for (i = bitset_iterate (&(plan->subqueries), &iter);
       i != -1; i = bitset_next_member (&iter))
    {
      subq = env ? &env->subqueries[i] : NULL;
      query = subq ? subq->node : NULL;
      qo_plan_compute_subquery_cost (query, &temp_cpu_cost, &temp_io_cost);
      subq_cpu_cost += temp_cpu_cost;
      subq_io_cost += temp_io_cost;
    }

  /* This computes the specific cost characteristics for each plan. */
  (*(plan->vtbl)->cost_fn) (plan);

  /* Now add in the subquery costs; this cost is incurred for each row
     produced by this plan, so multiply it by the estimated cardinality
     and add it to the access cost. */
  if (plan->info)
    {
      plan->variable_cpu_cost += (plan->info)->cardinality * subq_cpu_cost;
      plan->variable_io_cost += (plan->info)->cardinality * subq_io_cost;
    }
}				/* qo_plan_compute_cost() */

/*
 * qo_plan_compute_subquery_cost () -
 *   return:
 *   subquery(in):
 *   subq_cpu_cost(in):
 *   subq_io_cost(in):
 */
static void
qo_plan_compute_subquery_cost (PT_NODE * subquery,
			       double *subq_cpu_cost, double *subq_io_cost)
{
  QO_SUMMARY *summary;
  double arg1_cpu_cost, arg1_io_cost, arg2_cpu_cost, arg2_io_cost;

  *subq_cpu_cost = *subq_io_cost = 0.0;	/* init */

  if (subquery == NULL)
    {
      /* This case is selected when a subquery wasn't
         optimized for some reason.  just take a guess.
         ---> NEED MORE CONSIDERATION */
      *subq_cpu_cost = 5.0;
      *subq_io_cost = 5.0;
      return;
    }				/* if (subquery == NULL) */

  switch (subquery->node_type)
    {
    case PT_SELECT:
      summary = (QO_SUMMARY *) subquery->info.query.q.select.qo_summary;
      if (summary)
	{
	  *subq_cpu_cost +=
	    summary->fixed_cpu_cost + summary->variable_cpu_cost;
	  *subq_io_cost += summary->fixed_io_cost + summary->variable_io_cost;
	}
      else
	{
	  /* it may be unknown error. just take a guess.
	     ---> NEED MORE CONSIDERATION */
	  *subq_cpu_cost = 5.0;
	  *subq_io_cost = 5.0;
	}

      /* Here, GROUP BY and ORDER BY cost must be considered.
         ---> NEED MORE CONSIDERATION */
      /* ---> under construction <--- */
      break;

    case PT_UNION:
    case PT_INTERSECTION:
    case PT_DIFFERENCE:
      qo_plan_compute_subquery_cost (subquery->info.query.q.union_.arg1,
				     &arg1_cpu_cost, &arg1_io_cost);
      qo_plan_compute_subquery_cost (subquery->info.query.q.union_.arg2,
				     &arg2_cpu_cost, &arg2_io_cost);

      *subq_cpu_cost = arg1_cpu_cost + arg2_cpu_cost;
      *subq_io_cost = arg1_io_cost + arg2_io_cost;

      /* later, sort cost and result-set scan cost must be considered.
         ---> NEED MORE CONSIDERATION */
      /* ---> under construction <--- */
      break;

    default:
      /* it is unknown case. just take a guess.
         ---> NEED MORE CONSIDERATION */
      *subq_cpu_cost = 5.0;
      *subq_io_cost = 5.0;
      break;
    }				/* switch */

  return;
}				/* qo_plan_compute_subquery_cost() */

/*
 * qo_plan_compute_iscan_sort_list () -
 *   return:
 *   root(in):
 */
static void
qo_plan_compute_iscan_sort_list (QO_PLAN * root)
{
  QO_PLAN *plan;
  QO_ENV *env;
  PARSER_CONTEXT *parser;
  PT_NODE *tree, *sort_list, *sort, *col, *node;
  QO_NODE_INDEX_ENTRY *ni_entryp;
  QO_INDEX_ENTRY *index_entryp;
  int nterms, seg_idx, i, j;
  QO_SEGMENT *seg;
  PT_MISC_TYPE asc_or_desc;
  QFILE_TUPLE_VALUE_POSITION pos_descr;
  TP_DOMAIN *key_type;

  /* find sortable plan */
  plan = root;
  while (plan->plan_type == QO_PLANTYPE_FOLLOW ||
	 (plan->plan_type == QO_PLANTYPE_JOIN &&
	  (plan->plan_un.join.join_method == QO_JOINMETHOD_NL_JOIN ||
	   plan->plan_un.join.join_method == QO_JOINMETHOD_IDX_JOIN)))
    {
      plan =
	(plan->plan_type == QO_PLANTYPE_FOLLOW) ? plan->plan_un.follow.head
	: plan->plan_un.join.outer;
    }

  /* check for plan type */
  if (plan->plan_type != QO_PLANTYPE_SCAN)
    {
      return;			/* nop */
    }

  /* exclude class hierarchy scan */
  if (QO_NODE_INFO (plan->plan_un.scan.node) == NULL ||	/* may be impossible */
      QO_NODE_INFO_N (plan->plan_un.scan.node) > 1)
    {
      return;			/* nop */
    }

  /* check for index scan plan */
  if (plan == NULL ||
      plan->plan_type != QO_PLANTYPE_SCAN ||
      plan->plan_un.scan.scan_method != QO_SCANMETHOD_INDEX_SCAN ||
      (env = (plan->info)->env) == NULL ||
      (parser = QO_ENV_PARSER (env)) == NULL ||
      (tree = QO_ENV_PT_TREE (env)) == NULL)
    {
      return;			/* nop */
    }

  /* if no index scan terms, no index scan */
  nterms = bitset_cardinality (&(plan->plan_un.scan.terms));
  if (nterms <= 0)
    {
      return;			/* nop */
    }

  /* pointer to QO_NODE_INDEX_ENTRY structure in QO_PLAN */
  ni_entryp = plan->plan_un.scan.index;
  /* pointer to linked list of index node, 'head' field(QO_INDEX_ENTRY
     strucutre) of QO_NODE_INDEX_ENTRY */
  index_entryp = (ni_entryp)->head;

  asc_or_desc = SM_IS_CONSTRAINT_REVERSE_INDEX_FAMILY (index_entryp->type)
    ? PT_DESC : PT_ASC;

  key_type = NULL;		/* init */
  if (asc_or_desc != PT_DESC)
    {				/* is not reverse index */
      ATTR_STATS *attr_stats;
      int idx;

      attr_stats = index_entryp->stats;
      idx = index_entryp->bt_stats_idx;
      if (attr_stats && idx >= 0 && idx < attr_stats->n_btstats)
	{
	  key_type = attr_stats->bt_stats[idx].key_type;
	  if (key_type && key_type->type->id == DB_TYPE_MIDXKEY)
	    {
	      /* get the column key-type of multi-column index */
	      key_type = key_type->setdomain;
	    }
	}

      i = plan->plan_un.scan.equi ? nterms : nterms - 1;
      /* get the first non-equal range key domain */
      for (j = 0; j < i && key_type; j++)
	{
	  key_type = key_type->next;
	}

      if (key_type == NULL)
	{			/* invalid case */
	  return;		/* nop */
	}
    }

  sort_list = NULL;		/* init */

  for (i = plan->plan_un.scan.equi ? nterms : nterms - 1;
       i < index_entryp->nsegs; i++)
    {
      seg_idx = (index_entryp->seg_idxs[i]);
      if (seg_idx == -1)
	{			/* not exist in query */
	  break;		/* give up */
	}

      seg = QO_ENV_SEG (env, seg_idx);
      node = QO_SEG_PT_NODE (seg);
      if (node->node_type == PT_DOT_)
	{
	  /* FIXME :: we do not handle path-expr here */
	  break;		/* give up */
	}

      pos_descr = pt_to_pos_descr (parser, node, tree);
      if (pos_descr.pos_no <= 0)
	{			/* not found i-th key element */
	  break;		/* give up */
	}

      /* check for constant col's order node
       */
      col = tree->info.query.q.select.list;
      for (j = 1; j < pos_descr.pos_no && col; j++)
	{
	  col = col->next;
	}
      if (col == NULL)
	{			/* impossible case */
	  break;		/* give up */
	}

      while (col->node_type == PT_DOT_)
	{
	  col = col->info.dot.arg2;
	}

      if (col->node_type == PT_NAME &&
	  PT_NAME_INFO_IS_FLAGED (col, PT_NAME_INFO_CONSTANT))
	{
	  continue;		/* skip out constant order */
	}

      /* set sort info */
      sort = parser_new_node (parser, PT_SORT_SPEC);
      if (sort == NULL)
	{
	  PT_ERRORm (parser, node, MSGCAT_SET_PARSER_SEMANTIC,
		     MSGCAT_SEMANTIC_OUT_OF_MEMORY);
	  break;		/* give up */
	}

      if (key_type)
	{
	  asc_or_desc = (key_type->is_desc) ? PT_DESC : PT_ASC;
	  key_type = key_type->next;
	}

      sort->info.sort_spec.expr = pt_point (parser, col);
      sort->info.sort_spec.pos_descr = pos_descr;
      sort->info.sort_spec.asc_or_desc = asc_or_desc;

      sort_list = parser_append_node (sort, sort_list);
    }				/* for (i = plan->plan_un.scan.equi ? nterms : nterms - 1; ... */

  root->iscan_sort_list = sort_list;

  return;
}

/*
 * qo_top_plan_new () -
 *   return:
 *   plan(in):
 */
static QO_PLAN *
qo_top_plan_new (QO_PLAN * plan)
{
  QO_ENV *env;
  PT_NODE *tree, *group_by, *order_by, *orderby_for;
  PT_MISC_TYPE all_distinct;

  if (plan->top_rooted)
    {
      return plan;		/* is already top-level plan - OK */
    }

  if (plan == NULL || plan->info == NULL ||	/* worst plan */
      (env = (plan->info)->env) == NULL
      || bitset_cardinality (&((plan->info)->nodes)) < env->Nnodes
      || /* sub-plan */ (tree = QO_ENV_PT_TREE (env)) == NULL)
    {
      return plan;		/* do nothing */
    }

  QO_ASSERT (env, tree->node_type == PT_SELECT);

  plan->top_rooted = true;	/* mark as top-level plan */

  if (pt_is_single_tuple (QO_ENV_PARSER (env), tree))
    {				/* one tuple plan */
      return plan;		/* do nothing */
    }

  all_distinct = tree->info.query.all_distinct;
  group_by = tree->info.query.q.select.group_by;
  order_by = tree->info.query.order_by;
  orderby_for = tree->info.query.orderby_for;

  if (group_by || (all_distinct == PT_DISTINCT || order_by))
    {

      bool groupby_skip, orderby_skip;
      bool found_instnum;
      int t;
      BITSET_ITERATOR iter;
      QO_TERM *term;

      groupby_skip = orderby_skip = false;	/* init */

      for (t = bitset_iterate (&(plan->sarged_terms), &iter);
	   t != -1; t = bitset_next_member (&iter))
	{
	  term = QO_ENV_TERM (env, t);
	  if (QO_TERM_CLASS (term) == QO_TC_TOTALLY_AFTER_JOIN)
	    {
	      break;		/* found inst_num() */
	    }
	}			/* for (t = ...) */
      found_instnum = (t == -1) ? false : true;

      (void) qo_plan_compute_iscan_sort_list (plan);

      /* GROUP BY */
      if (group_by)
	{
#if 0				/* DO NOT DELETE ME - need future work */
	  if (plan->iscan_sort_list)
	    {
	      if (found_instnum && found_grpynum)
		{
		  ;		/* give up */
		}
	      else
		{
		  groupby_skip =
		    pt_sort_spec_cover (plan->iscan_sort_list, group_by);
		}
	    }
#endif

	  if (groupby_skip)
	    {
	      ;			/* DO NOT DELETE ME - need future work */
	    }
	  else
	    {
	      plan = qo_sort_new (plan, QO_UNORDERED, SORT_GROUPBY);
	    }
	}

      /* DISTINCT, ORDER BY */
      if (all_distinct == PT_DISTINCT || order_by)
	{
	  if (plan->iscan_sort_list)
	    {			/* need to check */
	      if (all_distinct == PT_DISTINCT)
		{
		  ;		/* give up */
		}
	      else
		{		/* non distinct */
		  if (group_by)
		    {
		      /* we already removed covered ORDER BY
		       * in reduce_order_by(). so is not covered ordering
		       */
		      ;		/* give up; DO NOT DELETE ME - need future work */
		    }
		  else
		    {		/* non group_by */
		      if (found_instnum && orderby_for)
			{
			  /* at here, we can not merge orderby_num pred with
			   * inst_num pred
			   */
			  ;	/* give up; DO NOT DELETE ME - need future work */
			}
		      else
			{
			  orderby_skip =
			    pt_sort_spec_cover (plan->iscan_sort_list,
						order_by);
			}
		    }
		}		/* else */
	    }			/* if (plan->iscan_sort_list) */

	  if (orderby_skip)
	    {
	      if (orderby_for)
		{		/* apply inst_num filter */
		  ;		/* DO NOT DELETE ME - need future work */
		}
	    }
	  else
	    {
	      plan = qo_sort_new (plan, QO_UNORDERED,
				  all_distinct == PT_DISTINCT ? SORT_DISTINCT
				  : SORT_ORDERBY);
	    }
	}
    }

  return plan;
}

/*
 * qo_generic_walk () -
 *   return:
 *   plan(in):
 *   child_fn(in):
 *   child_data(in):
 *   parent_fn(in):
 *   parent_data(in):
 */
static void
qo_generic_walk (QO_PLAN * plan,
		 void (*child_fn) (QO_PLAN *, void *),
		 void *child_data, void (*parent_fn) (QO_PLAN *, void *),
		 void *parent_data)
{
  if (parent_fn)
    (*parent_fn) (plan, parent_data);
}

/*
 * qo_plan_print_sort_spec_helper () -
 *   return:
 *   list(in):
 *   f(in):
 *   howfar(in):
 */
static void
qo_plan_print_sort_spec_helper (PT_NODE * list, FILE * f, int howfar)
{
  const char *prefix;

  if (list == NULL)
    {
      return;
    }

  fprintf (f, "\n" INDENTED_TITLE_FMT, (int) howfar, ' ', "sort:  ");

  prefix = "";
  for (; list; list = list->next)
    {
      if (list->info.sort_spec.pos_descr.pos_no < 1)
	{			/* useless from here */
	  break;
	}
      fputs (prefix, f);
      fprintf (f, "%d %s",
	       list->info.sort_spec.pos_descr.pos_no,
	       list->info.sort_spec.asc_or_desc == PT_ASC ? "asc" : "desc");
      prefix = ", ";
    }
}

/*
 * qo_plan_print_sort_spec () -
 *   return:
 *   plan(in):
 *   f(in):
 *   howfar(in):
 */
static void
qo_plan_print_sort_spec (QO_PLAN * plan, FILE * f, int howfar)
{
  if (plan->top_rooted != true)
    {				/* check for top level plan */
      return;
    }

  qo_plan_print_sort_spec_helper (plan->iscan_sort_list, f, howfar);

  if (plan->plan_type == QO_PLANTYPE_SORT)
    {
      QO_ENV *env;
      PT_NODE *tree;

      if ((env = (plan->info)->env) == NULL ||
	  (tree = QO_ENV_PT_TREE (env)) == NULL)
	{
	  return;		/* give up */
	}

      if (plan->plan_un.sort.sort_type == SORT_GROUPBY
	  && tree->node_type == PT_SELECT)
	{
	  qo_plan_print_sort_spec_helper (tree->info.query.q.select.group_by,
					  f, howfar);
	}

      if ((plan->plan_un.sort.sort_type == SORT_DISTINCT ||
	   plan->plan_un.sort.sort_type == SORT_ORDERBY)
	  && PT_IS_QUERY (tree))
	{
	  qo_plan_print_sort_spec_helper (tree->info.query.order_by,
					  f, howfar);
	}
    }
}

/*
 * qo_plan_print_costs () -
 *   return:
 *   plan(in):
 *   f(in):
 *   howfar(in):
 */
static void
qo_plan_print_costs (QO_PLAN * plan, FILE * f, int howfar)
{
  double fixed = plan->fixed_cpu_cost + plan->fixed_io_cost;
  double variable = plan->variable_cpu_cost + plan->variable_io_cost;
  fprintf (f,
	   "\n" INDENTED_TITLE_FMT
	   "fixed %.0f(%.1f/%.1f) var %.0f(%.1f/%.1f) card %.0f",
	   (int) howfar, ' ', "cost:",
	   fixed,
	   plan->fixed_cpu_cost,
	   plan->fixed_io_cost,
	   variable,
	   plan->variable_cpu_cost,
	   plan->variable_io_cost, (plan->info)->cardinality);
}


/*
 * qo_plan_print_projected_segs () -
 *   return:
 *   plan(in):
 *   f(in):
 *   howfar(in):
 */
static void
qo_plan_print_projected_segs (QO_PLAN * plan, FILE * f, int howfar)
{
  int sx;
  const char *prefix = "";
  BITSET_ITERATOR si;

  if (!((plan->info)->env->dump_enable))
    return;

  fprintf (f, "\n" INDENTED_TITLE_FMT, (int) howfar, ' ', "segs:");
  for (sx = bitset_iterate (&((plan->info)->projected_segs), &si);
       sx != -1; sx = bitset_next_member (&si))
    {
      fputs (prefix, f);
      qo_seg_fprint (&(plan->info)->env->segs[sx], f);
      prefix = ", ";
    }
}


/*
 * qo_plan_print_sarged_terms () -
 *   return:
 *   plan(in):
 *   f(in):
 *   howfar(in):
 */
static void
qo_plan_print_sarged_terms (QO_PLAN * plan, FILE * f, int howfar)
{
  if (!bitset_is_empty (&(plan->sarged_terms)))
    {
      fprintf (f, "\n" INDENTED_TITLE_FMT, (int) howfar, ' ', "sargs:");
      qo_termset_fprint ((plan->info)->env, &plan->sarged_terms, f);
    }
}

/*
 * qo_plan_print_outer_join_terms () -
 *   return:
 *   plan(in):
 *   f(in):
 *   howfar(in):
 */
static void
qo_plan_print_outer_join_terms (QO_PLAN * plan, FILE * f, int howfar)
{
  if (!bitset_is_empty (&(plan->plan_un.join.during_join_terms)))
    {
      fprintf (f, "\n" INDENTED_TITLE_FMT, (int) howfar, ' ', "during:");
      qo_termset_fprint ((plan->info)->env,
			 &(plan->plan_un.join.during_join_terms), f);
    }
  if (!bitset_is_empty (&(plan->plan_un.join.after_join_terms)))
    {
      fprintf (f, "\n" INDENTED_TITLE_FMT, (int) howfar, ' ', "after:");
      qo_termset_fprint ((plan->info)->env,
			 &(plan->plan_un.join.after_join_terms), f);
    }
}				/* qo_plan_print_outer_join_terms() */


/*
 * qo_plan_print_subqueries () -
 *   return:
 *   plan(in):
 *   f(in):
 *   howfar(in):
 */
static void
qo_plan_print_subqueries (QO_PLAN * plan, FILE * f, int howfar)
{
  if (!bitset_is_empty (&(plan->subqueries)))
    {
      fprintf (f, "\n" INDENTED_TITLE_FMT, (int) howfar, ' ', "subqs: ");
      bitset_print (&(plan->subqueries),
		    (int (*)(void *, char *, int)) fprintf, f);
    }
}

/*
 * qo_scan_new () -
 *   return:
 *   info(in):
 *   node(in):
 *   scan_method(in):
 *   pinned_subqueries(in):
 */
static QO_PLAN *
qo_scan_new (QO_INFO * info, QO_NODE * node, QO_SCANMETHOD scan_method,
	     BITSET * pinned_subqueries)
{
  QO_PLAN *plan;
  QO_ENV *env = info->env;
  BITSET_ITERATOR iter;
  int t;
  QO_TERM *term;
  PT_NODE *pt_expr;

  plan = qo_plan_malloc (info->env);

  plan->info = info;
  plan->refcount = 0;
  plan->top_rooted = false;
  plan->well_rooted = true;
  plan->iscan_sort_list = NULL;
  plan->plan_type = QO_PLANTYPE_SCAN;
  plan->order = QO_UNORDERED;

  plan->plan_un.scan.scan_method = scan_method;
  plan->plan_un.scan.node = node;

  for (t = bitset_iterate (&(QO_NODE_SARGS (node)), &iter);
       t != -1; t = bitset_next_member (&iter))
    {
      term = QO_ENV_TERM (env, t);
      pt_expr = QO_TERM_PT_EXPR (term);

      /* check for non-null RANGE sarg term only used for index scan
       * only used for the first segment of the index key
       */
      if (pt_expr &&
	  PT_EXPR_INFO_IS_FLAGED (pt_expr, PT_EXPR_INFO_FULL_RANGE))
	{
	  continue;		/* skip and go ahead */
	}

      bitset_add (&(plan->sarged_terms), QO_TERM_IDX (term));
    }
  bitset_assign (&(plan->subqueries), pinned_subqueries);
  bitset_init (&(plan->plan_un.scan.terms), info->env);
  bitset_init (&(plan->plan_un.scan.kf_terms), info->env);
  plan->plan_un.scan.index = NULL;

  return plan;
}


/*
 * qo_scan_free () -
 *   return:
*   plan(in):
 */
static void
qo_scan_free (QO_PLAN * plan)
{
  bitset_delset (&(plan->plan_un.scan.terms));
  bitset_delset (&(plan->plan_un.scan.kf_terms));
}


/*
 * qo_seq_scan_new () -
 *   return:
 *   info(in):
 *   node(in):
 *   pinned_subqueries(in):
 */
QO_PLAN *
qo_seq_scan_new (QO_INFO * info, QO_NODE * node, BITSET * pinned_subqueries)
{
  QO_PLAN *plan;

  plan = qo_scan_new (info, node, QO_SCANMETHOD_SEQ_SCAN, pinned_subqueries);

  plan->vtbl = &qo_seq_scan_plan_vtbl;
  plan->plan_un.scan.index = NULL;

  qo_plan_compute_cost (plan);

  plan = qo_top_plan_new (plan);

  return plan;
}


/*
 * qo_sscan_cost () -
 *   return:
 *   planp(in):
 */
static void
qo_sscan_cost (QO_PLAN * planp)
{
  QO_NODE *nodep;

  nodep = planp->plan_un.scan.node;
  planp->fixed_cpu_cost = 0.0;
  planp->fixed_io_cost = 0.0;
  if (QO_NODE_NCARD (nodep) == 0)
    {
      planp->variable_cpu_cost = 1.0 * (double) QO_CPU_WEIGHT;
    }
  else
    {
      planp->variable_cpu_cost = (double) QO_NODE_NCARD (nodep) *
	(double) QO_CPU_WEIGHT;
    }
  planp->variable_io_cost = (double) QO_NODE_TCARD (nodep);
}				/* qo_sscan_cost() */

/*
 * qo_index_scan_new () -
 *   return:
 *   info(in):
 *   node(in):
 *   ni_entry(in):
 *   range_terms(in):
 *   kf_terms(in):
 *   pinned_subqueries(in):
 */
QO_PLAN *
qo_index_scan_new (QO_INFO * info, QO_NODE * node,
		   QO_NODE_INDEX_ENTRY * ni_entry, BITSET * range_terms,
		   BITSET * kf_terms, BITSET * pinned_subqueries)
{
  QO_PLAN *plan;
  BITSET_ITERATOR iter;
  int t;
  QO_ENV *env = info->env;
  QO_INDEX_ENTRY *index_entryp;
  QO_TERM *term;
  PT_NODE *pt_expr;
  BITSET index_segs;
  BITSET term_segs;

  bitset_init (&index_segs, env);
  bitset_init (&term_segs, env);

  plan =
    qo_scan_new (info, node, QO_SCANMETHOD_INDEX_SCAN, pinned_subqueries);
  /*
   * This is, in essence, the selectivity of the index.  We
   * really need to do a better job of figuring out the cost of
   * an indexed scan.
   */
  plan->vtbl = &qo_index_scan_plan_vtbl;
  plan->plan_un.scan.index = ni_entry;

  bitset_assign (&(plan->plan_un.scan.terms), range_terms);	/* set key-range terms */
  plan->plan_un.scan.equi = true;	/* init */
  for (t = bitset_iterate (range_terms, &iter);
       t != -1; t = bitset_next_member (&iter))
    {
      term = QO_ENV_TERM (env, t);
      if (!QO_TERM_IS_FLAGED (term, QO_TERM_EQUAL_OP))
	{
	  plan->plan_un.scan.equi = false;
	  if (bitset_cardinality (&(plan->plan_un.scan.terms)) > 1)
	    {			/* not the first term */
	      pt_expr = QO_TERM_PT_EXPR (term);

	      /* check for non-null RANGE sarg term only used for index scan
	       * only used for the first segment of the index key
	       */
	      if (pt_expr &&
		  PT_EXPR_INFO_IS_FLAGED (pt_expr, PT_EXPR_INFO_FULL_RANGE))
		{
		  bitset_remove (&(plan->plan_un.scan.terms),
				 QO_TERM_IDX (term));
		}
	    }
	  break;
	}
    }				/* for (t = ... ) */

  /* remove key-range terms from sarged terms */
  bitset_difference (&(plan->sarged_terms), range_terms);

  /* all segments consisting in key columns */
  index_entryp = (ni_entry)->head;
  for (t = 0; t < index_entryp->nsegs; t++)
    {
      if ((index_entryp->seg_idxs[t]) != -1)
	bitset_add (&index_segs, (index_entryp->seg_idxs[t]));
    }

  /* for each sarged terms */
  for (t = bitset_iterate (kf_terms, &iter);
       t != -1; t = bitset_next_member (&iter))
    {
      term = QO_ENV_TERM (env, t);

      if (!bitset_is_empty (&(QO_TERM_SUBQUERIES (term))))
	{
	  continue;		/* term contains correlated subquery */
	}

      pt_expr = QO_TERM_PT_EXPR (term);

      /* check for non-null RANGE sarg term only used for index scan
       * only used for the first segment of the index key
       */
      if (pt_expr &&
	  PT_EXPR_INFO_IS_FLAGED (pt_expr, PT_EXPR_INFO_FULL_RANGE))
	{
	  continue;		/* skip out unnecessary term */
	}

      bitset_assign (&term_segs, &(QO_TERM_SEGS (term)));
      /* only include the node's segments for the correlated-index-join
       *
       * for example:
       * SELECT...
       * FROM x, y - x:outer, y:inner, do correlated-index-join on y(j)
       * WHERE x.i > 0
       *   and x.j = y.j -- range_term
       *   and y.j <> 5  -- key_filter: segs is (y.j)
       *   and x.k > y.j -- key_filter: segs is not (x.k, y.j) but (y.j)
       */
      bitset_intersect (&term_segs, &(QO_NODE_SEGS (node)));

      /* if the term is consisted by only the node's segments which
       * appear in scan terms, it will be key-filter.
       * otherwise will be data filter
       */
      if (!bitset_is_empty (&term_segs))
	{
	  if (bitset_subset (&index_segs, &term_segs))
	    {
	      bitset_add (&(plan->plan_un.scan.kf_terms), t);
	    }
	}
    }				/* for (t = ... ) */

  /* exclude key filter terms from sargs terms */
  bitset_difference (&(plan->sarged_terms), &(plan->plan_un.scan.kf_terms));

  bitset_delset (&term_segs);
  bitset_delset (&index_segs);

  qo_plan_compute_cost (plan);

  plan = qo_top_plan_new (plan);

  return plan;
}

/*
 * qo_iscan_cost () -
 *   return:
 *   planp(in):
 */
static void
qo_iscan_cost (QO_PLAN * planp)
{
  QO_NODE *nodep;
  QO_NODE_INDEX_ENTRY *ni_entryp;
  QO_ATTR_CUM_STATS *cum_statsp;
  QO_INDEX_ENTRY *index_entryp;
  double sel, sel_limit, objects, height, leaves, opages;
  double object_IO, index_IO;
  QO_TERM *termp;
  BITSET_ITERATOR iter;
  int i, t, n, pkeys_num;

  nodep = planp->plan_un.scan.node;
  ni_entryp = planp->plan_un.scan.index;
  index_entryp = (ni_entryp)->head;
  cum_statsp = &(ni_entryp)->cum_stats;

  if (index_entryp->force < 0)
    {
      qo_worst_cost (planp);
      return;
    }
  else if (index_entryp->force > 0)
    {
      qo_zero_cost (planp);
      return;
    }

  /* selectivity of the index terms */
  sel = 1.0;

  i = 0;
  n = index_entryp->col_num;

  pkeys_num = MIN (n, cum_statsp->key_size);
  for (t = bitset_iterate (&(planp->plan_un.scan.terms), &iter);
       t != -1; t = bitset_next_member (&iter))
    {
      termp = QO_ENV_TERM (QO_NODE_ENV (nodep), t);

      if (i == 0)
	{			/* the first key-range term of the index scan */
	  sel *= QO_TERM_SELECTIVITY (termp);
	}
      else
	{			/* apply heuristic factor */
	  if (QO_TERM_SELECTIVITY (termp) < 0.1)
	    sel *= QO_TERM_SELECTIVITY (termp) * pow ((double) n, 2);
	  else
	    sel *= QO_TERM_SELECTIVITY (termp);
	}

      /* check upper bound */
      sel = MIN (sel, 1.0);

      /* set selectivity limit */
      if (i < pkeys_num && cum_statsp->pkeys[i] > 0)
	{
	  sel_limit = 1.0 / (double) cum_statsp->pkeys[i];
	}
      else
	{			/* can not use btree partial-key statistics */
	  if (cum_statsp->keys > 0)
	    {
	      sel_limit = 1.0 / (double) cum_statsp->keys;
	    }
	  else
	    {
	      if (QO_NODE_NCARD (nodep) == 0)
		{		/* empty class */
		  sel = 0.0;
		  break;	/* immediately exit loop */
		}
	      sel_limit = 1.0 / (double) QO_NODE_NCARD (nodep);
	    }
	}

      /* check lower bound */
      sel = MAX (sel, sel_limit);

      i++;
      n--;
    }

  /* number of objects to be selected */
  objects = sel * (double) QO_NODE_NCARD (nodep);
  /* height of the B+tree */
  height = (double) cum_statsp->height - 1.0;
  if (height < 0.0)
    height = 0.0;
  /* number of leaf pages to be accessed */
  leaves = ceil (sel * (double) cum_statsp->leafs);
  /* total number of pages occupied by objects */
  opages = (double) QO_NODE_TCARD (nodep);
  /* I/O cost to access B+tree index */
  index_IO = ((ni_entryp)->n * height) + leaves;
  /* IO cost to fetch objects */
  /* By heuristic, we defined a probability function of IO cost by
     the selectivity as followings.

     fomular: probability = lean (selectivity - x-bias) + y-bias
     objectIO = #pages * probability
     if selectivity < 0.3, then below 30% of pages will be accessed
     if 0.3 <= selectivity < 0.8, then over 60% and below 100% of pages
     will be accessed
     otherwise, 100% of pages will be accessed

     100% |. . . . . . . . . . . . . * * * * *
     |                        * .       .
     |                     *    .       .
     |                  *       .       .
     |               *          .       .
     |            *             .       .
     60% +. . . .  *                .       .
     |                          .       .
     |                          .       .
     |                          .       .
     |                          .       .
     30% +. . . . .                 .       .
     |       * .                .       .
     |     *   .                .       .
     |   *     .                .       .
     | *       .                .       .
     +---------+----------------+-------+
     0.3              0.8     1.0
   */
  if (sel < 0.3)
    {
      /* p = 1.0 (sel - 0.0) + 0.0 */
      object_IO = opages * sel;
      /* 0.0 <= sel < 0.3; 0 <= object_IO < opages * 0.3 */
    }
  else if (sel < 0.8)
    {
      /* p = ((1.0 - 0.6) / (0.8 - 0.3)) (sel - 0.3) + 0.6
         = 0.8 sel + 0.36 */
      object_IO = opages * (0.8 * sel + 0.36);
      /* 0.3 <= selectivity < 0.8; opages * 0.6 <= object_IO < opages * 1.0 */
    }
  else
    {
      /* p = 0.0 (sel - 0.0) + 1.0 = 1.0 */
      object_IO = opages;
      /* 0.8 <= sel <= 1.0; object_IO = opages */
    }

  if (object_IO < 1.0)
    {
      /* at least one page */
      object_IO = 1.0;
    }
  else if ((double) PRM_PB_NBUFFERS - index_IO < object_IO)
    {
      /* If there are more target pages than buffer pages, then we'll do
         more I/O's, but probably not one per object. In this situation,
         we consider only object IO because the index pages will be
         accessed very frequently so that they will be resident in
         the buffer pool.

         Let
         T = # of object pages
         P = # of buffer pages
         N = # of objects (expected)
         Then, if we assume that objects are distributed randomly across the
         pages, the probability that any particular object resides on a page
         already in the buffer is P/T, so the probability it will cause a
         "miss" is (1 - (P/T)), and the expected number of I/O's is
         N * (1 - (P/T)). */
      object_IO =
	objects *
	(1.0 - (((double) PRM_PB_NBUFFERS - index_IO) / (double) opages));
    }

  if (sel < 1.0)
    {				/* is not Full-Range sel */
      object_IO = ceil (FUDGE_FACTOR * object_IO);
    }
  object_IO = MAX (1.0, object_IO);

  /* index scan requires more CPU cost than sequential scan */

  planp->fixed_cpu_cost = 0.0;
  planp->fixed_io_cost = index_IO;
  planp->variable_cpu_cost =
    objects * (double) QO_CPU_WEIGHT *ISCAN_OVERHEAD_FACTOR;
  planp->variable_io_cost = object_IO;

  /* one page heap file; reconfig iscan cost */
  if (QO_NODE_TCARD (nodep) <= 1)
    {
      if (QO_NODE_NCARD (nodep) > 1)
	{			/* index scan is worth */
	  planp->fixed_io_cost = 0.0;
	}
    }
}				/* qo_iscan_cost() */


static void
qo_scan_fprint (QO_PLAN * plan, FILE * f, int howfar)
{
  fprintf (f, "\n" INDENTED_TITLE_FMT, (int) howfar, ' ', "class:");
  qo_node_fprint (plan->plan_un.scan.node, f);

  if (plan->plan_un.scan.scan_method == QO_SCANMETHOD_INDEX_SCAN)
    {
      fprintf (f, "\n" INDENTED_TITLE_FMT, (int) howfar, ' ', "index: ");
      fprintf (f, "%s ", plan->plan_un.scan.index->head->name);

      qo_termset_fprint ((plan->info)->env, &plan->plan_un.scan.terms, f);

      if (!bitset_is_empty (&(plan->plan_un.scan.kf_terms)))
	{
	  fprintf (f, "\n" INDENTED_TITLE_FMT, (int) howfar, ' ', "filtr: ");
	  qo_termset_fprint ((plan->info)->env,
			     &(plan->plan_un.scan.kf_terms), f);
	}
    }
}

/*
 * qo_scan_info () -
 *   return:
 *   plan(in):
 *   f(in):
 *   howfar(in):
 */
static void
qo_scan_info (QO_PLAN * plan, FILE * f, int howfar)
{
  QO_NODE *node = plan->plan_un.scan.node;
  int i, n = 1;
  const char *name;

  fprintf (f, "\n%*c%s(", (int) howfar, ' ', (plan->vtbl)->info_string);
  if (QO_NODE_INFO (node))
    {
      for (i = 0, n = QO_NODE_INFO (node)->n; i < n; i++)
	{
	  name = QO_NODE_INFO (node)->info[i].name;
	  fprintf (f, "%s ", (name ? name : "(anon)"));
	}
    }
  name = QO_NODE_NAME (node);
  if (n == 1)
    fprintf (f, "%s", (name ? name : "(unknown)"));
  else
    fprintf (f, "as %s", (name ? name : "(unknown)"));

  if (plan->plan_un.scan.scan_method == QO_SCANMETHOD_INDEX_SCAN)
    {
      BITSET_ITERATOR bi;
      QO_ENV *env;
      int i;
      const char *separator;

      env = (plan->info)->env;
      separator = ", ";

      fprintf (f, "%s%s", separator, plan->plan_un.scan.index->head->name);

      for (i = bitset_iterate (&(plan->plan_un.scan.terms), &bi);
	   i != -1; i = bitset_next_member (&bi))
	{
	  fprintf (f, "%s%s", separator,
		   qo_term_string (QO_ENV_TERM (env, i)));
	  separator = " and ";
	}
      if (bitset_cardinality (&(plan->plan_un.scan.kf_terms)) > 0)
	{
	  separator = ", [";
	  for (i = bitset_iterate (&(plan->plan_un.scan.kf_terms), &bi);
	       i != -1; i = bitset_next_member (&bi))
	    {
	      fprintf (f, "%s%s",
		       separator, qo_term_string (QO_ENV_TERM (env, i)));
	      separator = " and ";
	    }
	  fprintf (f, "]");
	}
    }

  fprintf (f, ")");
}


/*
 * qo_sort_new () -
 *   return:
 *   root(in):
 *   order(in):
 *   sort_type(in):
 */
QO_PLAN *
qo_sort_new (QO_PLAN * root, QO_EQCLASS * order, SORT_TYPE sort_type)
{
  QO_PLAN *subplan, *plan;

  subplan = root;

  if (sort_type == SORT_TEMP)
    {				/* is not top-level plan */
      /* skip out top-level sort plan */
      for (;
	   subplan && subplan->plan_type == QO_PLANTYPE_SORT;
	   subplan = subplan->plan_un.sort.subplan)
	{
	  if (subplan->top_rooted &&
	      subplan->plan_un.sort.sort_type != SORT_TEMP)
	    {
	      ;			/* skip and go ahead */
	    }
	  else
	    {
	      break;		/* is not top-level sort plan */
	    }
	}

      /* check for dummy sort plan */
      if (order == QO_UNORDERED && subplan->plan_type == QO_PLANTYPE_SORT)
	{
	  return qo_plan_add_ref (root);
	}

      /* skip out empty sort plan */
      for (;
	   subplan && subplan->plan_type == QO_PLANTYPE_SORT;
	   subplan = subplan->plan_un.sort.subplan)
	{
	  if (!bitset_is_empty (&(subplan->sarged_terms)))
	    {
	      break;
	    }
	}
    }

  plan = qo_plan_malloc ((subplan->info)->env);

  plan->info = subplan->info;
  plan->refcount = 0;
  plan->top_rooted = subplan->top_rooted;
  plan->well_rooted = false;
  plan->iscan_sort_list = NULL;
  plan->order = order;
  plan->plan_type = QO_PLANTYPE_SORT;
  plan->vtbl = &qo_sort_plan_vtbl;

  plan->plan_un.sort.sort_type = sort_type;
  plan->plan_un.sort.subplan = qo_plan_add_ref (subplan);
  plan->plan_un.sort.xasl = NULL;	/* To be determined later */

  qo_plan_compute_cost (plan);

  plan = qo_top_plan_new (plan);

  return plan;
}

/*
 * qo_sort_walk () -
 *   return:
 *   plan(in):
 *   child_fn(in):
 *   child_data(in):
 *   parent_fn(in):
 *   parent_data(in):
 */
static void
qo_sort_walk (QO_PLAN * plan,
	      void (*child_fn) (QO_PLAN *, void *),
	      void *child_data, void (*parent_fn) (QO_PLAN *, void *),
	      void *parent_data)
{
  if (child_fn)
    (*child_fn) (plan->plan_un.sort.subplan, child_data);
  if (parent_fn)
    (*parent_fn) (plan, parent_data);
}

static void
qo_sort_fprint (QO_PLAN * plan, FILE * f, int howfar)
{
  switch (plan->plan_un.sort.sort_type)
    {
    case SORT_TEMP:
      fprintf (f, "\n" INDENTED_TITLE_FMT, (int) howfar, ' ', "order:");
      qo_eqclass_fprint_wrt (plan->order, &(plan->info->nodes), f);
      break;

    case SORT_GROUPBY:
      fprintf (f, "(group by)");
      break;

    case SORT_ORDERBY:
      fprintf (f, "(order by)");
      break;

    case SORT_DISTINCT:
      fprintf (f, "(distinct)");
      break;

    default:
      break;
    }

  qo_plan_fprint (plan->plan_un.sort.subplan, f, howfar, "subplan: ");
}

/*
 * qo_sort_info () -
 *   return:
 *   plan(in):
 *   f(in):
 *   howfar(in):
 */
static void
qo_sort_info (QO_PLAN * plan, FILE * f, int howfar)
{

  switch (plan->plan_un.sort.sort_type)
    {
    case SORT_TEMP:
      if (plan->order != QO_UNORDERED)
	{
#if 0
	  /*
	   * Don't bother printing these out; they're almost always
	   * superfluous from the standpoint of a naive user trying to
	   * figure out what's going on.
	   */
	  fprintf (f, "\n%*c%s(", (int) howfar, ' ',
		   (plan->vtbl)->info_string);
	  qo_eqclass_fprint_wrt (plan->order, &(plan->info->nodes), f);
	  fprintf (f, ")");
#endif
	}
      break;

    case SORT_GROUPBY:
      fprintf (f, "\n%*c%s(%s)", (int) howfar, ' ',
	       (plan->vtbl)->info_string, "group by");
      howfar += INDENT_INCR;
      break;

    case SORT_ORDERBY:
      fprintf (f, "\n%*c%s(%s)", (int) howfar, ' ',
	       (plan->vtbl)->info_string, "order by");
      howfar += INDENT_INCR;
      break;

    case SORT_DISTINCT:
      fprintf (f, "\n%*c%s(%s)", (int) howfar, ' ',
	       (plan->vtbl)->info_string, "distinct");
      howfar += INDENT_INCR;
      break;

    default:
      break;
    }

  qo_plan_lite_print (plan->plan_un.sort.subplan, f, howfar);
}

/*
 * qo_sort_cost () -
 *   return:
 *   planp(in):
 */
static void
qo_sort_cost (QO_PLAN * planp)
{
  QO_PLAN *subplanp;

  subplanp = planp->plan_un.sort.subplan;

  if (subplanp->plan_type == QO_PLANTYPE_SORT &&
      planp->plan_un.sort.sort_type == SORT_TEMP)
    {
      /* This plan won't actually incur any runtime cost because it
         won't actually exist (its sort spec will supercede the sort
         spec of the subplan).  We can't just clobber the sort spec on
         the lower plan because it might be shared by others. */
      planp->fixed_cpu_cost = subplanp->fixed_cpu_cost;
      planp->fixed_io_cost = subplanp->fixed_io_cost;
      planp->variable_cpu_cost = subplanp->variable_cpu_cost;
      planp->variable_io_cost = subplanp->variable_io_cost;
    }
  else
    {
      QO_EQCLASS *order;
      double objects, pages, result_size;

      order = planp->order;
      objects = (subplanp->info)->cardinality;
      result_size = objects * (double) (subplanp->info)->projected_size;
      pages = result_size / (double) IO_PAGESIZE;
      if (pages < 1.0)
	pages = 1.0;

      /* The cost (in io's) of just setting up a list file.  This is
         mostly to discourage the optimizer from choosing merge join
         for joins of little classes. */
      planp->fixed_cpu_cost = subplanp->fixed_cpu_cost +
	subplanp->variable_cpu_cost + TEMP_SETUP_COST;
      planp->fixed_io_cost = subplanp->fixed_io_cost +
	subplanp->variable_io_cost;
      planp->variable_cpu_cost = objects * (double) QO_CPU_WEIGHT;
      planp->variable_io_cost = pages;

      if (order != QO_UNORDERED && order != subplanp->order)
	{
	  double sort_io, tcard;

	  sort_io = 0.0;	/* init */

	  if (objects > 1.0)
	    {
	      if (pages < (double) PRM_SR_NBUFFERS)
		{
		  /* We can sort the result in memory without any
		     additional io costs. Assume cpu costs are n*log(n) in
		     number of recors. */
		  sort_io = (double) QO_CPU_WEIGHT *objects * log2 (objects);
		}
	      else
		{
		  /* There are too many records to permit an in-memory
		     sort, so io costs will be increased.  Assume that the
		     io costs increase by the number of pages required to
		     hold the intermediate result.  CPU costs increase as
		     above.
		     Model courtesy of Ender. */
		  sort_io = pages * log3 (pages / 4.0);

		  /* guess: apply IO caching for big size sort list.
		     Disk IO cost cannot be greater than the 10% number
		     of the requested IO pages */
		  if (subplanp->plan_type == QO_PLANTYPE_SCAN)
		    {
		      tcard =
			(double) QO_NODE_TCARD (subplanp->plan_un.scan.node);
		      tcard *= 0.1;
		      if (pages >= tcard)
			{	/* big size sort list */
			  sort_io *= 0.1;
			}
		    }
		}
	    }

	  planp->fixed_io_cost += sort_io;
	}
    }
}				/* qo_sort_cost() */

/*
 * qo_join_new () -
 *   return:
 *   info(in):
 *   join_type(in):
 *   join_method(in):
 *   outer(in):
 *   inner(in):
 *   join_terms(in):
 *   duj_terms(in):
 *   afj_terms(in):
 *   sarged_terms(in):
 *   pinned_subqueries(in):
 */
QO_PLAN *
qo_join_new (QO_INFO * info,
	     JOIN_TYPE join_type,
	     QO_JOINMETHOD join_method,
	     QO_PLAN * outer,
	     QO_PLAN * inner,
	     BITSET * join_terms,
	     BITSET * duj_terms,
	     BITSET * afj_terms,
	     BITSET * sarged_terms, BITSET * pinned_subqueries)
{
  QO_PLAN *plan;
  QO_NODE *node;
  PT_NODE *spec;
  BITSET sarg_out_terms;

  bitset_init (&sarg_out_terms, info->env);

  plan = qo_plan_malloc (info->env);

  QO_ASSERT (info->env, outer != NULL);
  QO_ASSERT (info->env, inner != NULL);

  plan->info = info;
  plan->refcount = 0;
  plan->top_rooted = false;
  plan->well_rooted = false;
  plan->iscan_sort_list = NULL;
  plan->plan_type = QO_PLANTYPE_JOIN;

  switch (join_method)
    {

    case QO_JOINMETHOD_NL_JOIN:
    case QO_JOINMETHOD_IDX_JOIN:
      plan->vtbl =
	(join_method == QO_JOINMETHOD_NL_JOIN) ? &qo_nl_join_plan_vtbl :
	&qo_idx_join_plan_vtbl;
      plan->order = QO_UNORDERED;
      /* These checks are necessary because of restrictions in the
         current XASL implementation of nested loop joins.
         Never put anything on the inner plan that isn't file-based
         (i.e., a scan of either a heap file or a list file). */
      if (!VALID_INNER (inner))
	inner = qo_sort_new (inner, inner->order, SORT_TEMP);
      else if (IS_OUTER_JOIN_TYPE (join_type))
	{
	  /* for outer join,
	   * if inner plan is a scan of classes in hierarchy */
	  if (inner->plan_type == QO_PLANTYPE_SCAN &&
	      QO_NODE_INFO (inner->plan_un.scan.node) &&
	      QO_NODE_INFO_N (inner->plan_un.scan.node) > 1)
	    {
	      inner = qo_sort_new (inner, inner->order, SORT_TEMP);
	    }
	}

      break;

    case QO_JOINMETHOD_MERGE_JOIN:

      plan->vtbl = &qo_merge_join_plan_vtbl;
#if 0
      /* Don't do this anymore; it relies on symmetry, which definitely
         doesn't apply anymore with the advent of outer joins. */

      /* Arrange to always put the smallest cardinality on the outer
         term; this may lead to some savings given the current merge
         join implementation. */
      if ((inner->info)->cardinality < (outer->info)->cardinality)
	{
	  QO_PLAN *tmp;
	  tmp = inner;
	  inner = outer;
	  outer = tmp;
	}
#endif

      /* The merge join result has the same nominal order as the two
         subjoins that feed it.  However, if it happens that none of
         the segments in that order are to be projected from the
         result, the result is effectively *unordered*.  Check for that
         condition here. */
      plan->order
	= bitset_intersects (&(QO_EQCLASS_SEGS (outer->order)),
			     &((plan->info)->projected_segs))
	? outer->order : QO_UNORDERED;

      /* The current implementation of merge joins always produces a
         list file
         These two checks are necessary because of restrictions in the
         current XASL implementation of merge joins. */
      if (outer->plan_type != QO_PLANTYPE_SORT)
	outer = qo_sort_new (outer, outer->order, SORT_TEMP);
      if (inner->plan_type != QO_PLANTYPE_SORT)
	inner = qo_sort_new (inner, inner->order, SORT_TEMP);

      break;
    }				/* switch (join_method) */

  node =
    QO_ENV_NODE (info->env, bitset_first_member (&((inner->info)->nodes)));

  /* check for cselect of method */
  if ((spec = QO_NODE_ENTITY_SPEC (node))
      && spec->info.spec.flat_entity_list == NULL
      && spec->info.spec.derived_table_type == PT_IS_CSELECT)
    {
      /* mark as cselect join */
      plan->plan_un.join.join_type = JOIN_CSELECT;
    }
  else
    {
      plan->plan_un.join.join_type = join_type;
    }
  plan->plan_un.join.join_method = join_method;
  plan->plan_un.join.outer = qo_plan_add_ref (outer);
  plan->plan_un.join.inner = qo_plan_add_ref (inner);

  bitset_init (&(plan->plan_un.join.join_terms), info->env);
  bitset_init (&(plan->plan_un.join.during_join_terms), info->env);
  bitset_init (&(plan->plan_un.join.after_join_terms), info->env);

  /* set join terms */
  bitset_assign (&(plan->plan_un.join.join_terms), join_terms);
  /* add to out terms */
  bitset_union (&sarg_out_terms, &(plan->plan_un.join.join_terms));

  if (IS_OUTER_JOIN_TYPE (join_type))
    {
      /* set during join terms */
      bitset_assign (&(plan->plan_un.join.during_join_terms), duj_terms);
      bitset_difference (&(plan->plan_un.join.during_join_terms),
			 &sarg_out_terms);
      /* add to out terms */
      bitset_union (&sarg_out_terms, &(plan->plan_un.join.during_join_terms));

      /* set after join terms */
      bitset_assign (&(plan->plan_un.join.after_join_terms), afj_terms);
      bitset_difference (&(plan->plan_un.join.after_join_terms),
			 &sarg_out_terms);
      /* add to out terms */
      bitset_union (&sarg_out_terms, &(plan->plan_un.join.after_join_terms));
    }

  /* set plan's sarged terms */
  bitset_assign (&(plan->sarged_terms), sarged_terms);
  bitset_difference (&(plan->sarged_terms), &sarg_out_terms);

  /* Make sure that the pinned subqueries and the sargs are placed on
     the same node:  by now the pinned subqueries are very likely
     pinned here precisely because they're used by these sargs.
     Separating them (so that they get evaluated in some different
     order) will yield incorrect results. */
  bitset_assign (&(plan->subqueries), pinned_subqueries);

  qo_plan_compute_cost (plan);

#if 1				/* MERGE_ALWAYS_MAKES_LISTFILE */
  /* This is necessary to get the proper cost model for merge joins,
     which always build their result into a listfile right now.  At the
     moment the cost model for a merge plan just models the cost of
     producing the result tuples, but not storing them into a listfile.
     We could push the cost into the merge plan itself, I suppose, but
     a rational implementation wouldn't impose this cost, and so I have
     hope that one day we'll be able to eliminate it. */
  if (join_method == QO_JOINMETHOD_MERGE_JOIN)
    plan = qo_sort_new (plan, plan->order, SORT_TEMP);
#endif /* MERGE_ALWAYS_MAKES_LISTFILE */

  bitset_delset (&sarg_out_terms);

  plan = qo_top_plan_new (plan);

  return plan;
}				/* qo_join_new() */

/*
 * qo_join_free () -
 *   return:
 *   plan(in):
 */
static void
qo_join_free (QO_PLAN * plan)
{
  bitset_delset (&(plan->plan_un.join.join_terms));
  bitset_delset (&(plan->plan_un.join.during_join_terms));
  bitset_delset (&(plan->plan_un.join.after_join_terms));
}				/* qo_join_free() */

/*
 * qo_join_walk () -
 *   return:
 *   plan(in):
 *   child_fn(in):
 *   child_data(in):
 *   parent_fn(in):
 *   parent_data(in):
 */
static void
qo_join_walk (QO_PLAN * plan,
	      void (*child_fn) (QO_PLAN *, void *),
	      void *child_data, void (*parent_fn) (QO_PLAN *, void *),
	      void *parent_data)
{
  if (child_fn)
    {
      (*child_fn) (plan->plan_un.join.outer, child_data);
      (*child_fn) (plan->plan_un.join.inner, child_data);
    }
  if (parent_fn)
    (*parent_fn) (plan, parent_data);
}

/*
 * qo_join_fprint () -
 *   return:
 *   plan(in):
 *   f(in):
 *   howfar(in):
 */
static void
qo_join_fprint (QO_PLAN * plan, FILE * f, int howfar)
{
  switch (plan->plan_un.join.join_type)
    {
    case JOIN_INNER:
      if (!bitset_is_empty (&(plan->plan_un.join.join_terms)))
	fputs (" (inner join)", f);
      else
	{
	  if (plan->plan_un.join.join_method == QO_JOINMETHOD_IDX_JOIN)
	    {
	      fputs (" (inner join)", f);
	    }
	  else
	    {
	      fputs (" (cross join)", f);
	    }
	}
      break;
    case JOIN_LEFT:
      fputs (" (left outer join)", f);
      break;
    case JOIN_RIGHT:
      fputs (" (right outer join)", f);
      break;
    case JOIN_OUTER:
      fputs (" (full outer join)", f);
      break;
    case JOIN_CSELECT:
      fputs (" (cselect join)", f);
      break;
    case NO_JOIN:
    default:
      fputs (" (unknown join type)", f);
      break;
    }
  if (!bitset_is_empty (&(plan->plan_un.join.join_terms)))
    {
      fprintf (f, "\n" INDENTED_TITLE_FMT, (int) howfar, ' ', "edge:");
      qo_termset_fprint ((plan->info)->env, &(plan->plan_un.join.join_terms),
			 f);
    }
  qo_plan_fprint (plan->plan_un.join.outer, f, howfar, "outer: ");
  qo_plan_fprint (plan->plan_un.join.inner, f, howfar, "inner: ");
  qo_plan_print_outer_join_terms (plan, f, howfar);
}

/*
 * qo_join_info () -
 *   return:
 *   plan(in):
 *   f(in):
 *   howfar(in):
 */
static void
qo_join_info (QO_PLAN * plan, FILE * f, int howfar)
{
  if (!bitset_is_empty (&(plan->plan_un.join.join_terms)))
    {
      QO_ENV *env;
      const char *separator;
      int i;
      BITSET_ITERATOR bi;

      env = (plan->info)->env;
      separator = "";

      fprintf (f, "\n%*c%s(", (int) howfar, ' ', (plan->vtbl)->info_string);
      for (i = bitset_iterate (&(plan->plan_un.join.join_terms), &bi);
	   i != -1; i = bitset_next_member (&bi))
	{
	  fprintf (f, "%s%s", separator,
		   qo_term_string (QO_ENV_TERM (env, i)));
	  separator = " and ";
	}
      fprintf (f, ")");
    }
  else
    fprintf (f, "\n%*cNested loops", (int) howfar, ' ');
  if (plan->plan_un.join.join_type == JOIN_LEFT)
    fprintf (f, ": left outer");
  else if (plan->plan_un.join.join_type == JOIN_RIGHT)
    fprintf (f, ": right outer");

  qo_plan_lite_print (plan->plan_un.join.outer, f, howfar + INDENT_INCR);
  qo_plan_lite_print (plan->plan_un.join.inner, f, howfar + INDENT_INCR);
}


/*
 * qo_nljoin_cost () -
 *   return:
 *   planp(in):
 */
void
qo_nljoin_cost (QO_PLAN * planp)
{
  QO_PLAN *inner, *outer;
  double inner_io_cost, inner_cpu_cost, outer_io_cost, outer_cpu_cost;
  double pages, guessed_result_cardinality;
  double pages2, io_cost, diff_cost;

  inner = planp->plan_un.join.inner;
  outer = planp->plan_un.join.outer;

  /* CPU and IO costs which are fixed againt join */
  planp->fixed_cpu_cost = outer->fixed_cpu_cost + inner->fixed_cpu_cost;
  planp->fixed_io_cost = outer->fixed_io_cost + inner->fixed_io_cost;

  /* inner side CPU cost of nested-loop block join */
  guessed_result_cardinality = (outer->info)->cardinality;
  inner_cpu_cost = guessed_result_cardinality * (double) QO_CPU_WEIGHT;
  /* join cost */

  if (inner->plan_type == QO_PLANTYPE_SCAN &&
      inner->plan_un.scan.scan_method == QO_SCANMETHOD_INDEX_SCAN &&
      inner->plan_un.scan.equi == true)
    {
      /* correlated index equi-join */
      inner_cpu_cost += inner->variable_cpu_cost;
      if (outer->plan_type == QO_PLANTYPE_SCAN && outer->plan_un.scan.scan_method == QO_SCANMETHOD_SEQ_SCAN && PRM_MAX_OUTER_CARD_OF_IDXJOIN != 0 &&	/* is set */
	  guessed_result_cardinality > PRM_MAX_OUTER_CARD_OF_IDXJOIN)
	{
	  planp->variable_cpu_cost = QO_INFINITY;
	  planp->variable_io_cost = QO_INFINITY;
	  return;
	}
    }
  else
    {
      /* neither correlated index join nor equi-join */
      inner_cpu_cost += MAX (1.0, (outer->info)->cardinality) *
	inner->variable_cpu_cost;
    }

  /* inner side IO cost of nested-loop block join */
  inner_io_cost = outer->variable_io_cost * inner->variable_io_cost;	/* assume IO as # blocks */
  if (inner->plan_type == QO_PLANTYPE_SCAN)
    {
      pages = QO_NODE_TCARD (inner->plan_un.scan.node);
      if (inner_io_cost > pages * 2)
	{
	  /* inner IO cost cannot be greater than two times of the number of
	     pages of the class because buffering */
	  inner_io_cost = pages * 2;

	  /* for iscan of inner, reconfig inner_io_cost */
	  inner_io_cost -= inner->fixed_io_cost;
	  inner_io_cost = MAX (0.0, inner_io_cost);

	}

      if (outer->plan_type == QO_PLANTYPE_SCAN &&
	  outer->plan_un.scan.scan_method == QO_SCANMETHOD_SEQ_SCAN)
	{
	  if (			/* inner->plan_type == QO_PLANTYPE_SCAN && *//* here OK */
	       inner->plan_un.scan.scan_method == QO_SCANMETHOD_SEQ_SCAN)
	    {
	      if ((outer->info)->cardinality == (inner->info)->cardinality)
		{
		  pages2 = QO_NODE_TCARD (outer->plan_un.scan.node);
		  /* exclude too many heavy-sequential scan
		   *     nl-join - sscan (small)
		   *             + sscan (big)
		   */
		  if (pages > pages2)
		    {
		      io_cost = inner->variable_io_cost +
			MIN (inner_io_cost, pages2 * 2);
		      diff_cost =
			io_cost - (outer->variable_io_cost + inner_io_cost);
		      if (diff_cost > 0)
			{
			  inner_io_cost += diff_cost + 0.1;
			}
		    }
		}
	    }
	}

      if (planp->plan_un.join.join_type != JOIN_INNER)
	/* outer join leads nongrouped scan overhead */
	inner_cpu_cost +=
	  (outer->info)->cardinality * pages * NONGROUPED_SCAN_COST;
    }
  if (inner->plan_type == QO_PLANTYPE_SORT)
    {
      /* (inner->plan_un.sort.subplan)->info == inner->info */
      pages = ((inner->info)->cardinality * (inner->info)->projected_size)
	/ IO_PAGESIZE;
      if (pages < 1)
	pages = 1;

      pages2 = pages * 2;
      if (inner_io_cost > pages2)
	{
	  diff_cost = inner_io_cost - pages2;

	  /* inner IO cost cannot be greater than two times of the number
	     of pages of the list file */
	  inner_io_cost = pages2;

	  /* The cost (in io's) of just handling a list file.  This is
	     mostly to discourage the optimizer from choosing nl-join with
	     temp inner for joins of little classes. */
	  io_cost = inner->fixed_io_cost * 0.1;
	  diff_cost = MIN (io_cost, diff_cost);
	  planp->fixed_io_cost += diff_cost + 0.1;
	}

      inner_cpu_cost +=
	(outer->info)->cardinality * pages * NONGROUPED_SCAN_COST;

    }

  /* outer side CPU cost of nested-loop block join */
  outer_cpu_cost = outer->variable_cpu_cost;
  /* outer side IO cost of nested-loop block join */
  outer_io_cost = outer->variable_io_cost;

  /* CPU and IO costs which are variable according to the join plan */
  planp->variable_cpu_cost = inner_cpu_cost + outer_cpu_cost;
  planp->variable_io_cost = inner_io_cost + outer_io_cost;

  {
    QO_ENV *env;
    int i;
    QO_SUBQUERY *subq;
    PT_NODE *query;
    double temp_cpu_cost, temp_io_cost;
    double subq_cpu_cost, subq_io_cost;
    BITSET_ITERATOR iter;

    /* Compute the costs for all of the subqueries. Each of the pinned
       subqueries is intended to be evaluated once for each row produced
       by this plan; the cost of each such evaluation in the fixed cost
       of the subquery plus one trip through the result, i.e.,

       QO_PLAN_FIXED_COST(subplan) + QO_PLAN_ACCESS_COST(subplan)

       The cost info for the subplan has (probably) been squirreled away
       in a QO_SUMMARY structure reachable from the original select node. */

    /* When computing the cost for a WORST_PLAN, we'll get in here
       without a backing info node; just work around it. */
    env = inner->info ? (inner->info)->env : NULL;
    subq_cpu_cost = subq_io_cost = 0.0;	/* init */

    for (i = bitset_iterate (&(inner->subqueries), &iter);
	 i != -1; i = bitset_next_member (&iter))
      {
	subq = env ? &env->subqueries[i] : NULL;
	query = subq ? subq->node : NULL;
	qo_plan_compute_subquery_cost (query, &temp_cpu_cost, &temp_io_cost);
	subq_cpu_cost += temp_cpu_cost;
	subq_io_cost += temp_io_cost;
      }

    planp->variable_cpu_cost +=
      MAX (0.0, guessed_result_cardinality - 1.0) * subq_cpu_cost;
    planp->variable_io_cost += MAX (0.0, outer->variable_io_cost - 1.0) * subq_io_cost;	/* assume IO as # blocks */
  }

}				/* qo_nljoin_cost() */


/*
 * qo_mjoin_cost () -
 *   return:
 *   planp(in):
 */
static void
qo_mjoin_cost (QO_PLAN * planp)
{
  QO_PLAN *inner;
  QO_PLAN *outer;

  inner = planp->plan_un.join.inner;
  outer = planp->plan_un.join.outer;
  /* CPU and IO costs which are fixed againt join */
  planp->fixed_cpu_cost = outer->fixed_cpu_cost + inner->fixed_cpu_cost;
  planp->fixed_io_cost = outer->fixed_io_cost + inner->fixed_io_cost;
  /* CPU and IO costs which are variable according to the join plan */
  planp->variable_cpu_cost = outer->variable_cpu_cost +
    inner->variable_cpu_cost;
  planp->variable_cpu_cost += ((outer->info)->cardinality / 2) *
    ((inner->info)->cardinality / 2) * (double) QO_CPU_WEIGHT;
  /* merge cost */
  planp->variable_io_cost = outer->variable_io_cost + inner->variable_io_cost;
}				/* qo_mjoin_cost() */

/*
 * qo_follow_new () -
 *   return:
 *   info(in):
 *   head_plan(in):
 *   path_term(in):
 *   sarged_terms(in):
 *   pinned_subqueries(in):
 */
QO_PLAN *
qo_follow_new (QO_INFO * info,
	       QO_PLAN * head_plan,
	       QO_TERM * path_term,
	       BITSET * sarged_terms, BITSET * pinned_subqueries)
{
  QO_PLAN *plan;

  plan = qo_plan_malloc (info->env);

  QO_ASSERT (info->env, head_plan != NULL);

  plan->info = info;
  plan->refcount = 0;
  plan->top_rooted = false;
  plan->well_rooted = head_plan->well_rooted;
  plan->iscan_sort_list = NULL;
  plan->plan_type = QO_PLANTYPE_FOLLOW;
  plan->vtbl = &qo_follow_plan_vtbl;
  plan->order = QO_UNORDERED;

  plan->plan_un.follow.head = qo_plan_add_ref (head_plan);
  plan->plan_un.follow.path = path_term;

  bitset_assign (&(plan->sarged_terms), sarged_terms);
  bitset_remove (&(plan->sarged_terms), QO_TERM_IDX (path_term));

  bitset_assign (&(plan->subqueries), pinned_subqueries);

  /*
   * Follow plans must be sure to account for the sarged terms for the
   * tail node.  Ordinarily, those terms would have been taken care of
   * by a scan plan of some sort, but since a follow plan essentially
   * performs a directed scan of the tail node class, it needs to keep
   * track of those sargs in the same way that the other scans do.
   *
   * The same precautions apply for the correlated subqueries pinned to
   * this plan.
   */
  bitset_union (&(plan->sarged_terms),
		&(QO_NODE_SARGS (QO_TERM_TAIL (path_term))));
  bitset_union (&(plan->subqueries),
		&(QO_NODE_SUBQUERIES (QO_TERM_TAIL (path_term))));

  qo_plan_compute_cost (plan);

  plan = qo_top_plan_new (plan);

  return plan;
}

/*
 * qo_follow_walk () -
 *   return:
 *   plan(in):
 *   child_fn(in):
 *   child_data(in):
 *   parent_fn(in):
 *   parent_data(in):
 */
static void
qo_follow_walk (QO_PLAN * plan,
		void (*child_fn) (QO_PLAN *, void *),
		void *child_data, void (*parent_fn) (QO_PLAN *, void *),
		void *parent_data)
{
  if (child_fn)
    (*child_fn) (plan->plan_un.follow.head, child_data);
  if (parent_fn)
    (*parent_fn) (plan, parent_data);
}

/*
 * qo_follow_fprint () -
 *   return:
 *   plan(in):
 *   f(in):
 *   howfar(in):
 */
static void
qo_follow_fprint (QO_PLAN * plan, FILE * f, int howfar)
{
  fprintf (f, "\n" INDENTED_TITLE_FMT, (int) howfar, ' ', "edge:");
  qo_term_fprint (plan->plan_un.follow.path, f);
  qo_plan_fprint (plan->plan_un.follow.head, f, howfar, "head: ");
}

/*
 * qo_follow_info () -
 *   return:
 *   plan(in):
 *   f(in):
 *   howfar(in):
 */
static void
qo_follow_info (QO_PLAN * plan, FILE * f, int howfar)
{
  fprintf (f, "\n%*c%s(%s)", (int) howfar, ' ',
	   (plan->vtbl)->info_string,
	   qo_term_string (plan->plan_un.follow.path));
  qo_plan_lite_print (plan->plan_un.follow.head, f, howfar + INDENT_INCR);
}

/*
 * qo_follow_cost () -
 *   return:
 *   planp(in):
 */
static void
qo_follow_cost (QO_PLAN * planp)
{
  QO_PLAN *head;
  QO_NODE *tail;
  double cardinality, target_pages, fetch_ios;

  head = planp->plan_un.follow.head;
  cardinality = (planp->info)->cardinality;
  tail = QO_TERM_TAIL (planp->plan_un.follow.path);
  target_pages = (double) QO_NODE_TCARD (tail);

  if (cardinality < target_pages)
    {
      /* If we expect to fetch fewer objects than there are pages in
         the target class, just assume that each fetch will touch a new
         page. */
      fetch_ios = cardinality;
    }
  else if (PRM_PB_NBUFFERS >= target_pages)
    {
      /* We have more pointers to follow than pages in the target, but
         fewer target pages than buffer pages.  Assume that the page
         buffering will limit the number of of page fetches to the
         number of target pages. */
      fetch_ios = target_pages;
    }
  else
    {
      /* If there are more target pages than buffer pages, then we'll
         do more io's, but probably not one per object.  Let

         T = # of object pages
         P = # of buffer pages
         N = # of objects (expected)

         Then, if we assume that objects are distributed randomly
         across the pages, the probability that any particular object
         resides on a page already in the buffer is P/T, so the
         probability it will cause a "miss" is (1 - (P/T)), and the
         expected number of io's is N * (1 - (P/T)). */
      fetch_ios = cardinality *
	(1.0 - ((double) PRM_PB_NBUFFERS) / target_pages);
    }

  planp->fixed_cpu_cost = head->fixed_cpu_cost;
  planp->fixed_io_cost = head->fixed_io_cost;
  planp->variable_cpu_cost = head->variable_cpu_cost +
    (cardinality * (double) QO_CPU_WEIGHT);
  planp->variable_io_cost = head->variable_io_cost + fetch_ios;
}				/* qo_follow_cost() */


/*
 * qo_cp_new () -
 *   return:
 *   info(in):
 *   outer(in):
 *   inner(in):
 *   sarged_terms(in):
 *   pinned_subqueries(in):
 */
QO_PLAN *
qo_cp_new (QO_INFO * info, QO_PLAN * outer, QO_PLAN * inner,
	   BITSET * sarged_terms, BITSET * pinned_subqueries)
{
  QO_PLAN *plan;
  BITSET empty_terms;

  bitset_init (&empty_terms, info->env);

  plan = qo_join_new (info, JOIN_INNER /* default */ ,
		      QO_JOINMETHOD_NL_JOIN,
		      outer, inner, &empty_terms /* join_terms */ ,
		      &empty_terms /* duj_terms */ ,
		      &empty_terms /* afj_terms */ ,
		      sarged_terms, pinned_subqueries);

  bitset_delset (&empty_terms);

  return plan;
}


/*
 * qo_worst_new () -
 *   return:
 *   env(in):
 */
QO_PLAN *
qo_worst_new (QO_ENV * env)
{
  QO_PLAN *plan;

  plan = qo_plan_malloc (env);

  plan->info = NULL;
  plan->refcount = 0;
  plan->top_rooted = true;
  plan->well_rooted = false;
  plan->iscan_sort_list = NULL;
  plan->order = QO_UNORDERED;
  plan->plan_type = QO_PLANTYPE_WORST;
  plan->vtbl = &qo_worst_plan_vtbl;

  qo_plan_compute_cost (plan);

  return plan;
}

/*
 * qo_worst_fprint () -
 *   return:
 *   plan(in):
 *   f(in):
 *   howfar(in):
 */
static void
qo_worst_fprint (QO_PLAN * plan, FILE * f, int howfar)
{
}

/*
 * qo_worst_info () -
 *   return:
 *   plan(in):
 *   f(in):
 *   howfar(in):
 */
static void
qo_worst_info (QO_PLAN * plan, FILE * f, int howfar)
{
  fprintf (f, "\n%*c%s", (int) howfar, ' ', (plan->vtbl)->info_string);
}

/*
 * qo_worst_cost () -
 *   return:
 *   planp(in):
 */
static void
qo_worst_cost (QO_PLAN * planp)
{
  planp->fixed_cpu_cost = QO_INFINITY;
  planp->fixed_io_cost = QO_INFINITY;
  planp->variable_cpu_cost = QO_INFINITY;
  planp->variable_io_cost = QO_INFINITY;
}				/* qo_worst_cost() */

/*
 * qo_zero_cost () -
 *   return:
 *   planp(in):
 */
static void
qo_zero_cost (QO_PLAN * planp)
{
  planp->fixed_cpu_cost = 0.0;
  planp->fixed_io_cost = 0.0;
  planp->variable_cpu_cost = 0.0;
  planp->variable_io_cost = 0.0;
}				/* qo_zero_cost() */


/*
 * qo_plan_order_by () -
 *   return:
 *   plan(in):
 *   order(in):
 */
QO_PLAN *
qo_plan_order_by (QO_PLAN * plan, QO_EQCLASS * order)
{
  if (plan == NULL || order == QO_UNORDERED || plan->order == order)
    return plan;
  else if (BITSET_MEMBER ((plan->info)->eqclasses, QO_EQCLASS_IDX (order)))
    return qo_sort_new (plan, order, SORT_TEMP);
  else
    return (QO_PLAN *) NULL;
}

/*
 * qo_plan_cmp () -
 *   return: one of {PLAN_COMP_UNK, PLAN_COMP_LT, PLAN_COMP_EQ, PLAN_COMP_GT}
 *   a(in):
 *   b(in):
 */
QO_PLAN_COMPARE_RESULT
qo_plan_cmp (QO_PLAN * a, QO_PLAN * b)
{
  /*
   * Make sure that qo_plan_cmp() is commutative, especially when two
   * plans differ in only one of their cost components.  In particular,
   * if we have
   *
   *  C(p1) = (k, m)
   *  C(p2) = (k, n)
   *
   * or
   *
   *  C(p1) = (m, k)
   *  C(p2) = (n, k)
   *
   * where m != n (which happens fairly frequently in this
   * application), we want to make sure that qo_plan_cmp(p1, p2) ==
   * qo_plan_cmp(p2, p1).  If we're not careful about the tests, it won't
   * be.
   */

#ifdef OLD_CODE
  if (QO_PLAN_FIXED_COST (a) <= QO_PLAN_FIXED_COST (b))
    {
      return QO_PLAN_ACCESS_COST (a) <= QO_PLAN_ACCESS_COST (b) ? a : b;
    }
  else
    {
      return QO_PLAN_ACCESS_COST (b) <= QO_PLAN_ACCESS_COST (a) ? b : a;
    }
#else /* OLD_CODE */
  double af, aa, bf, ba;
  QO_NODE *a_node, *b_node;

  /* skip out top-level sort plan */
  if (a->top_rooted && b->top_rooted)
    {
      /* skip out the same sort plan */
      while (a->plan_type == QO_PLANTYPE_SORT &&
	     b->plan_type == QO_PLANTYPE_SORT &&
	     a->plan_un.sort.sort_type == b->plan_un.sort.sort_type)
	{
	  a = a->plan_un.sort.subplan;
	  b = b->plan_un.sort.subplan;
	}
    }
  else
    {
      if (a->top_rooted)
	{
	  while (a->plan_type == QO_PLANTYPE_SORT)
	    {
	      if (a->plan_un.sort.sort_type == SORT_TEMP)
		{
		  break;	/* is not top-level plan */
		}
	      a = a->plan_un.sort.subplan;
	    }
	}
      if (b->top_rooted)
	{
	  while (b->plan_type == QO_PLANTYPE_SORT)
	    {
	      if (b->plan_un.sort.sort_type == SORT_TEMP)
		{
		  break;	/* is top-level plan */
		}
	      b = b->plan_un.sort.subplan;
	    }
	}
    }

  af = a->fixed_cpu_cost + a->fixed_io_cost;
  aa = a->variable_cpu_cost + a->variable_io_cost;
  bf = b->fixed_cpu_cost + b->fixed_io_cost;
  ba = b->variable_cpu_cost + b->variable_io_cost;

  if (a->plan_type != QO_PLANTYPE_SCAN && a->plan_type != QO_PLANTYPE_SORT)
    {
      goto cost_cmp;		/* give up */
    }

  if (b->plan_type != QO_PLANTYPE_SCAN && b->plan_type != QO_PLANTYPE_SORT)
    {
      goto cost_cmp;		/* give up */
    }

  if (a == b)
    {
      return PLAN_COMP_EQ;
    }

  if (a->plan_type != QO_PLANTYPE_SCAN || b->plan_type != QO_PLANTYPE_SCAN)
    {				/* impossible case */
      goto cost_cmp;		/* give up */
    }

  a_node = a->plan_un.scan.node;
  b_node = b->plan_un.scan.node;

  /* check for empty spec */
  if (QO_NODE_NCARD (a_node) == 0 && QO_NODE_TCARD (a_node) == 0)
    {
      if (QO_NODE_NCARD (b_node) == 0 && QO_NODE_TCARD (b_node) == 0)
	{
	  goto cost_cmp;	/* give up */
	}
      return PLAN_COMP_LT;
    }
  else if (QO_NODE_NCARD (b_node) == 0 && QO_NODE_TCARD (b_node) == 0)
    {
      return PLAN_COMP_GT;
    }

  if (QO_NODE_IDX (a_node) != QO_NODE_IDX (b_node))
    {
      goto cost_cmp;		/* give up */
    }

  /* check for both index scan of the same spec */
  if (a->plan_un.scan.scan_method != QO_SCANMETHOD_INDEX_SCAN ||
      b->plan_un.scan.scan_method != QO_SCANMETHOD_INDEX_SCAN)
    {
      goto cost_cmp;		/* give up */
    }

  /* iscan vs iscan index rule comparison */

  {
    int at, bt;			/* num iscan range terms */
    int at_kf, bt_kf;		/* num iscan filter terms */
    QO_NODE_INDEX_ENTRY *a_ni, *b_ni;
    QO_INDEX_ENTRY *a_ent, *b_ent;
    QO_ATTR_CUM_STATS *a_cum, *b_cum;
    int i;
    int a_last, b_last;		/* the last partial-key indicator */
    int a_keys, b_keys;		/* num keys */
    int a_pages, b_pages;	/* num access index pages */
    int a_leafs, b_leafs;	/* num access index leaf pages */
    QO_TERM *term;

    /* index entry of a spec */
    a_ni = a->plan_un.scan.index;
    a_ent = (a_ni)->head;
    a_cum = &(a_ni)->cum_stats;
    for (i = 0; i < a_cum->key_size; i++)
      {
	if (a_cum->pkeys[i] <= 0)
	  {
	    break;
	  }
      }
    a_last = i;
    /* index range terms */
    at = bitset_cardinality (&(a->plan_un.scan.terms));
    /* index filter terms */
    at_kf = bitset_cardinality (&(a->plan_un.scan.kf_terms));

    /* set the last equal range term */
    if (!(a->plan_un.scan.equi))
      {
	at--;
      }

    /* index entry of b spec */
    b_ni = b->plan_un.scan.index;
    b_ent = (b_ni)->head;
    b_cum = &(b_ni)->cum_stats;
    for (i = 0; i < b_cum->key_size; i++)
      {
	if (b_cum->pkeys[i] <= 0)
	  {
	    break;
	  }
      }
    b_last = i;
    /* index range terms */
    bt = bitset_cardinality (&(b->plan_un.scan.terms));
    /* index filter terms */
    bt_kf = bitset_cardinality (&(b->plan_un.scan.kf_terms));

    /* set the last equal range term */
    if (!(b->plan_un.scan.equi))
      {
	bt--;
      }

    /* STEP 1: take the smaller search condition */

    /* check for same index pointer */
    if (a_ent == b_ent)
      {
	/* check for search condition */
	if (at == bt && at_kf == bt_kf)
	  {
	    ;			/* go ahead */
	  }
	else if (at >= bt && at_kf >= bt_kf)
	  {
	    return PLAN_COMP_LT;
	  }
	else if (at <= bt && at_kf <= bt_kf)
	  {
	    return PLAN_COMP_GT;
	  }
      }

    /* check for same search condition */
    if (bitset_is_equivalent
	(&(a->plan_un.scan.terms), &(b->plan_un.scan.terms)))
      {
	/* take the smaller index */
	if (a_cum->pages < b_cum->pages)
	  {
	    return PLAN_COMP_LT;
	  }
	else if (a_cum->pages > b_cum->pages)
	  {
	    return PLAN_COMP_GT;
	  }
      }

    /* STEP 2: take the smaller access pages */

    if (a->variable_io_cost != b->variable_io_cost)
      {
	goto cost_cmp;		/* give up */
      }

    /* btree partial-key stats */
    if (at == a_ent->col_num)
      {
	a_keys = a_cum->keys;
      }
    else if (at > 0 && at < a_last)
      {
	a_keys = a_cum->pkeys[at - 1];
      }
    else
      {				/* at == 0 */
	a_keys = 1;		/* init as full range */
	term =
	  QO_ENV_TERM ((a->info)->env,
		       bitset_first_member (&(a->plan_un.scan.terms)));
	a_keys = (int) ceil (1.0 / QO_TERM_SELECTIVITY (term));
	a_keys = MIN (a_cum->pkeys[0], a_keys);
      }

    if (a_cum->leafs <= a_keys)
      {
	a_leafs = 1;
      }
    else
      {
	a_leafs = (int) ceil ((double) a_cum->leafs / a_keys);
      }

    /* btree access pages */
    a_pages = a_leafs + a_cum->height - 1;

    /* btree partial-key stats */
    if (bt == b_ent->col_num)
      {
	b_keys = b_cum->keys;
      }
    else if (bt > 0 && bt < b_last)
      {
	b_keys = b_cum->pkeys[bt - 1];
      }
    else
      {				/* bt == 0 */
	b_keys = 1;		/* init as full range */
	term =
	  QO_ENV_TERM ((b->info)->env,
		       bitset_first_member (&(b->plan_un.scan.terms)));
	b_keys = (int) ceil (1.0 / QO_TERM_SELECTIVITY (term));
	b_keys = MIN (b_cum->pkeys[0], b_keys);
      }

    if (b_cum->leafs <= b_keys)
      {
	b_leafs = 1;
      }
    else
      {
	b_leafs = (int) ceil ((double) b_cum->leafs / b_keys);
      }

    /* btree access pages */
    b_pages = b_leafs + b_cum->height - 1;

    if (a_pages < b_pages)
      {
	return PLAN_COMP_LT;
      }
    else if (a_pages > b_pages)
      {
	return PLAN_COMP_GT;
      }

    /* STEP 3: take the smaller index */
    if (a_cum->pages > a_cum->height && b_cum->pages > b_cum->height)
      {
	/* each index is big enough */
	if (a_cum->pages < b_cum->pages)
	  {
	    return PLAN_COMP_LT;
	  }
	else if (a_cum->pages > b_cum->pages)
	  {
	    return PLAN_COMP_GT;
	  }
      }

    /* STEP 4: take the smaller key range */
    if (a_keys > b_keys)
      {
	return PLAN_COMP_LT;
      }
    else if (a_keys < b_keys)
      {
	return PLAN_COMP_GT;
      }
  }

cost_cmp:

  if (a == b || (af == bf && aa == ba))
    return PLAN_COMP_EQ;
  if (af <= bf && aa <= ba)
    return PLAN_COMP_LT;
  if (bf <= af && ba <= aa)
    return PLAN_COMP_GT;
  return PLAN_COMP_UNK;
#endif /* OLD_CODE */
}


/*
 * qo_plan_fprint () -
 *   return:
 *   plan(in):
 *   f(in):
 *   howfar(in):
 *   title(in):
 */
void
qo_plan_fprint (QO_PLAN * plan, FILE * f, int howfar, const char *title)
{
  if (howfar < 0)
    {
      howfar = -howfar;
    }
  else
    {
      fputs ("\n", f);
      if (howfar)
	fprintf (f, INDENT_FMT, (int) howfar, ' ');
    }

  if (title)
    fprintf (f, TITLE_FMT, title);

  fputs ((plan->vtbl)->plan_string, f);

  {
    int title_len;

    title_len = title ? strlen (title) : 0;
    howfar += (title_len + INDENT_INCR);
  }

  (*((plan->vtbl)->fprint_fn)) (plan, f, howfar);

  qo_plan_print_projected_segs (plan, f, howfar);
  qo_plan_print_sarged_terms (plan, f, howfar);
  qo_plan_print_subqueries (plan, f, howfar);
  qo_plan_print_sort_spec (plan, f, howfar);
  qo_plan_print_costs (plan, f, howfar);
}

/*
 * qo_plan_lite_print () -
 *   return:
 *   plan(in):
 *   f(in):
 *   howfar(in):
 */
static void
qo_plan_lite_print (QO_PLAN * plan, FILE * f, int howfar)
{
  (*((plan->vtbl)->info_fn)) (plan, f, howfar);
}


/*
 * qo_plan_finalize () -
 *   return:
 *   plan(in):
 */
QO_PLAN *
qo_plan_finalize (QO_PLAN * plan)
{
  return qo_plan_add_ref (plan);
}


/*
 * qo_plan_discard () -
 *   return:
 *   plan(in):
 */
void
qo_plan_discard (QO_PLAN * plan)
{
  if (plan)
    {
      QO_ENV *env;
      bool dump_enable;

      /*
       * Be sure to capture dump_enable *before* we free the env
       * structure!
       */
      env = (plan->info)->env;
      dump_enable = env->dump_enable;

      qo_plan_del_ref (plan);
      qo_env_free (env);

      if (dump_enable)
	qo_print_stats (stdout);
    }
}


/*
 * qo_plan_walk () -
 *   return:
 *   plan(in):
 *   child_fn(in):
 *   child_data(in):
 *   parent_fn(in):
 *   parent_data(in):
 */
void
qo_plan_walk (QO_PLAN * plan,
	      void (*child_fn) (QO_PLAN *, void *),
	      void *child_data, void (*parent_fn) (QO_PLAN *, void *),
	      void *parent_data)
{
  (*(plan->vtbl)->walk_fn) (plan,
			    child_fn, child_data, parent_fn, parent_data);
}

/*
 * static () -
 *   return:
 *   qo_plan_del_ref_func(in):
 *   plan(in):
 */
static void
qo_plan_del_ref_func (QO_PLAN * plan, void *ignore)
{
  qo_plan_del_ref (plan);	/* use the macro */
}

/*
 * qo_plan_add_to_free_list () -
 *   return:
 *   plan(in):
 */
void
qo_plan_add_to_free_list (QO_PLAN * plan, void *ignore)
{
  bitset_delset (&(plan->sarged_terms));
  bitset_delset (&(plan->subqueries));
  if (plan->iscan_sort_list)
    {
      parser_free_tree (QO_ENV_PARSER ((plan->info)->env),
			plan->iscan_sort_list);
    }

  if (qo_accumulating_plans)
    {
      memset ((void *) plan, 0, sizeof (*plan));
      plan->plan_un.free.link = qo_plan_free_list;
      qo_plan_free_list = plan;
    }
  else
    {
      ++qo_plans_demalloced;
      DEALLOCATE ((plan->info)->env, plan);
    }
  ++qo_plans_deallocated;
}

/*
 * qo_plan_free () -
 *   return:
 *   plan(in):
 */
void
qo_plan_free (QO_PLAN * plan)
{
  if (plan->refcount != 0)
    {
#if defined(CUBRID_DEBUG)
      fprintf (stderr, "*** optimizer problem: plan refcount = %d ***\n",
	       plan->refcount);
#endif /* CUBRID_DEBUG */
    }
  else
    {
      if ((plan->vtbl)->free_fn)
	{
	  (*(plan->vtbl)->free_fn) (plan);
	}

      qo_plan_walk (plan, qo_plan_del_ref_func, NULL,
		    qo_plan_add_to_free_list, NULL);
    }
}

/*
 * qo_plans_init () -
 *   return:
 *   env(in):
 */
void
qo_plans_init (QO_ENV * env)
{
  qo_plan_free_list = NULL;
  qo_plans_allocated = 0;
  qo_plans_deallocated = 0;
  qo_plans_malloced = 0;
  qo_plans_demalloced = 0;
  qo_accumulating_plans = false /* true */ ;
  qo_next_tmpfile = 0;
}

/*
 * qo_plans_teardown () -
 *   return:
 *   env(in):
 */
void
qo_plans_teardown (QO_ENV * env)
{
  while (qo_plan_free_list)
    {
      QO_PLAN *plan = qo_plan_free_list;
      qo_plan_free_list = plan->plan_un.free.link;
      DEALLOCATE (env, plan);
      ++qo_plans_demalloced;
    }
  qo_accumulating_plans = false;
}

/*
 * qo_plans_stats () -
 *   return:
 *   f(in):
 */
void
qo_plans_stats (FILE * f)
{
  fprintf (f, "%d/%d plans allocated/deallocated\n",
	   qo_plans_allocated, qo_plans_deallocated);
  fprintf (f, "%d/%d plans malloced/demalloced\n",
	   qo_plans_malloced, qo_plans_demalloced);
}

/*
 * qo_plan_dump () - Print a representation of the plan on the indicated
 *		     stream
 *   return: nothing
 *   plan(in): A pointer to a query optimizer plan
 *   output(in): The stream to dump the plan to
 */
void
qo_plan_dump (QO_PLAN * plan, FILE * output)
{
  int level;

  if (output == NULL)
    output = stdout;

  if (plan == NULL)
    {
      fputs ("\nNo optimized plan!\n", output);
      return;
    }

  qo_get_optimization_param (&level, QO_PARAM_LEVEL);
  if (DETAILED_DUMP (level))
    qo_plan_fprint (plan, output, 0, NULL);
  else if (SIMPLE_DUMP (level))
    qo_plan_lite_print (plan, output, 0);

  fputs ("\n", output);

}				/* qo_plan_dump */

/******************************************************************************
 * qo_plan_get_cost_fn							      *
 *									      *
 * arguments:								      *
 *	plan_name: The name of a particular plan type.			      *
 *									      *
 * returns/side-effects: nothing					      *
 *									      *
 * description: Retrieve the current cost function for the named plan.	      *
 *****************************************************************************/

/*  for usqlx.exe on the PC Client. */

int
qo_plan_get_cost_fn (const char *plan_name)
{
  int n = DIM (all_vtbls);
  int i = 0;
  int cost = 'u';

  for (i = 0; i < n; i++)
    {
      if (intl_mbs_ncasecmp (plan_name, all_vtbls[i]->plan_string,
			     strlen (all_vtbls[i]->plan_string)) == 0)
	{
	  if (all_vtbls[i]->cost_fn == &qo_zero_cost)
	    cost = '0';
	  else if (all_vtbls[i]->cost_fn == &qo_worst_cost)
	    cost = 'i';
	  else
	    cost = 'd';
	  break;
	}
    }

  return cost;

}				/* qo_plan_get_cost_fn */

/*
 * qo_plan_set_cost_fn () - Changes the optimizer cost function for all plans
 *			    of the kind
 *   return: nothing
 *   plan_name(in): The name of a particular plan type
 *   fn(in): The new setting for the optimizer cost unction for the plan
 *
 * Note: Useful for testing different optimizer strategies and for forcing
 *	particular kinds of plans during testing
 */
const char *
qo_plan_set_cost_fn (const char *plan_name, int fn)
{
  int n = DIM (all_vtbls);
  int i = 0;

  for (i = 0; i < n; i++)
    {
      if (intl_mbs_ncasecmp (plan_name, all_vtbls[i]->plan_string,
			     strlen (all_vtbls[i]->plan_string)) == 0)
	{
	  switch (fn)
	    {
	    case 0:
	    case '0':
	    case 'b':		/* best */
	    case 'B':		/* BEST */
	    case 'z':		/* zero */
	    case 'Z':		/* ZERO */
	      all_vtbls[i]->cost_fn = &qo_zero_cost;
	      break;

	    case 1:
	    case '1':
	    case 'i':		/* infinite */
	    case 'I':		/* INFINITE */
	    case 'w':		/* worst */
	    case 'W':		/* WORST */
	      all_vtbls[i]->cost_fn = &qo_worst_cost;
	      break;

	    default:
	      all_vtbls[i]->cost_fn = all_vtbls[i]->default_cost;
	      break;
	    }
	  return all_vtbls[i]->plan_string;
	}
    }

  return NULL;

}				/* qo_plan_set_cost_fn */

/*
 * qo_set_cost () - SQL/X method interface to qo_set_cost_fn()
 *   return: nothing
 *   target(in): The target of the method; we don't care
 *   result(in): The result returned by the method; we don't care
 *   plan(in): The plan type to get jacked
 *   cost(in): The new cost for that plan type
 *
 * Note: This should get registered in the schema as
 *
 *		alter class foo
 *			add class method opt_set_cost(string, string)
 *			function qo_set_cost;
 *
 *	No libraries or other files are required, since this will
 *	always be linked in to the base executable.  Once linked, you
 *	should be able to do things like
 *
 *		call opt_set_cost("iscan", "0") on class foo
 *
 *	from SQL/X.
 */
void
qo_set_cost (DB_OBJECT * target, DB_VALUE * result, DB_VALUE * plan,
	     DB_VALUE * cost)
{
  const char *plan_string;
  const char *cost_string;

  switch (DB_VALUE_TYPE (plan))
    {
    case DB_TYPE_STRING:
    case DB_TYPE_CHAR:
    case DB_TYPE_NCHAR:
      plan_string = DB_GET_STRING (plan);
      break;
    default:
      plan_string = "unknown";
      break;
    }

  switch (DB_VALUE_TYPE (cost))
    {
    case DB_TYPE_STRING:
    case DB_TYPE_CHAR:
    case DB_TYPE_NCHAR:
      cost_string = DB_GET_STRING (cost);
      break;
    default:
      cost_string = "d";
      break;
    }

  /*
   * This relies on the fact that qo_plan_set_cost_fn is returning a
   * CONST string.  That way we don't need to dup it, and therefore we
   * won't leak it when the return value is discarded.
   */
  if ((plan_string =
       qo_plan_set_cost_fn (plan_string, cost_string[0])) != NULL)
    {
      DB_MAKE_STRING (result, plan_string);
    }
  else
    {
      er_set (ER_ERROR_SEVERITY, __FILE__, __LINE__, ER_GENERIC_ERROR, 0);
      DB_MAKE_ERROR (result, ER_GENERIC_ERROR);
    }
}
