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
 * query_planner.c - Plan descriptors
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#if !defined(WINDOWS)
#include <values.h>
#endif /* !WINDOWS */

#include "parser.h"
#include "optimizer.h"
#include "query_planner.h"
#include "query_graph.h"
#include "environment_variable.h"
#include "misc_string.h"
#include "system_parameter.h"
#include "parser.h"
#include "parser_message.h"
#include "intl_support.h"
#include "storage_common.h"
#include "xasl_generation.h"
#include "schema_manager.h"

/* this must be the last header file included!!! */
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

#define QO_INFO_INDEX(_M_offset, _bitset)  \
    (_M_offset + (unsigned int)(BITPATTERN(_bitset) & planner->node_mask))

typedef enum
{ JOIN_RIGHT_ORDER, JOIN_OPPOSITE_ORDER } JOIN_ORDER_TRY;

typedef int (*QO_WALK_FUNCTION) (QO_PLAN *, void *);

static int infos_allocated = 0;
static int infos_deallocated = 0;

static int qo_plans_allocated;
static int qo_plans_deallocated;
static int qo_plans_malloced;
static int qo_plans_demalloced;
static int qo_accumulating_plans;
static int qo_next_tmpfile;

static QO_PLAN *qo_plan_free_list;

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

static void qo_init_planvec (QO_PLANVEC *);
static void qo_uninit_planvec (QO_PLANVEC *);
static QO_PLAN_COMPARE_RESULT qo_check_planvec (QO_PLANVEC *, QO_PLAN *);
static QO_PLAN_COMPARE_RESULT qo_cmp_planvec (QO_PLANVEC *, QO_PLAN *);
static QO_PLAN *qo_find_best_plan_on_planvec (QO_PLANVEC *, double);

static QO_INFO *qo_alloc_info (QO_PLANNER *, BITSET *, BITSET *, BITSET *,
			       double);
static void qo_free_info (QO_INFO *);
static void qo_detach_info (QO_INFO *);
#if defined (CUBRID_DEBUG)
static void qo_dump_planvec (QO_PLANVEC *, FILE *, int);
static void qo_dump_info (QO_INFO *, FILE *);
static void qo_dump_planner_info (QO_PLANNER *, QO_PARTITION *, FILE *);
#endif
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

static QO_PLAN *qo_search_partition (QO_PLANNER *,
				     QO_PARTITION *, QO_EQCLASS *, BITSET *);
static QO_PLAN *qo_combine_partitions (QO_PLANNER *, BITSET *);
static int qo_generate_join_index_scan (QO_INFO *, JOIN_TYPE, QO_PLAN *,
					QO_INFO *, QO_NODE *,
					QO_NODE_INDEX_ENTRY *, BITSET *,
					BITSET *, BITSET *, BITSET *);
static void qo_generate_seq_scan (QO_INFO *, QO_NODE *);
static int qo_generate_index_scan (QO_INFO *, QO_NODE *,
				   QO_NODE_INDEX_ENTRY *);

static void qo_plan_add_to_free_list (QO_PLAN *, void *ignore);
static void qo_nljoin_cost (QO_PLAN *);
static void qo_plans_teardown (QO_ENV * env);
static void qo_plans_init (QO_ENV * env);
static void qo_plan_walk (QO_PLAN *, void (*)(QO_PLAN *, void *), void *,
			  void (*)(QO_PLAN *, void *), void *);
static QO_PLAN *qo_plan_finalize (QO_PLAN *);
static QO_PLAN *qo_plan_order_by (QO_PLAN *, QO_EQCLASS *);
static void qo_plan_fprint (QO_PLAN *, FILE *, int, const char *);
static QO_PLAN_COMPARE_RESULT qo_plan_cmp (QO_PLAN *, QO_PLAN *);
static QO_PLAN_COMPARE_RESULT qo_cover_index_plans_cmp (QO_PLAN *, QO_PLAN *);
static QO_PLAN_COMPARE_RESULT qo_order_by_skip_plans_cmp (QO_PLAN *,
							  QO_PLAN *);
static QO_PLAN_COMPARE_RESULT qo_group_by_skip_plans_cmp (QO_PLAN *,
							  QO_PLAN *);
static void qo_plan_free (QO_PLAN *);
static QO_PLAN *qo_plan_malloc (QO_ENV *);
static QO_PLAN *qo_worst_new (QO_ENV *);
static QO_PLAN *qo_cp_new (QO_INFO *, QO_PLAN *, QO_PLAN *, BITSET *,
			   BITSET *);
static QO_PLAN *qo_follow_new (QO_INFO *, QO_PLAN *, QO_TERM *, BITSET *,
			       BITSET *);
static QO_PLAN *qo_join_new (QO_INFO *, JOIN_TYPE, QO_JOINMETHOD, QO_PLAN *,
			     QO_PLAN *, BITSET *, BITSET *, BITSET *,
			     BITSET *, BITSET *);
static QO_PLAN *qo_sort_new (QO_PLAN *, QO_EQCLASS *, SORT_TYPE);
static QO_PLAN *qo_index_scan_new (QO_INFO *, QO_NODE *,
				   QO_NODE_INDEX_ENTRY *, BITSET *, BITSET *,
				   BITSET *);
static QO_PLAN *qo_seq_scan_new (QO_INFO *, QO_NODE *, BITSET *);
static int qo_has_is_not_null_term (QO_NODE * node);

static void qo_generate_index_scan_from_orderby (QO_INFO * info,
						 QO_NODE * node,
						 QO_NODE_INDEX_ENTRY *
						 in_entry);
static void qo_generate_index_scan_from_groupby (QO_INFO * info,
						 QO_NODE * node,
						 QO_NODE_INDEX_ENTRY *
						 in_entry);
static QO_PLAN *qo_index_scan_order_by_new (QO_INFO *, QO_NODE *,
					    QO_NODE_INDEX_ENTRY *, BITSET *,
					    BITSET *, BITSET *);
static QO_PLAN *qo_index_scan_group_by_new (QO_INFO *, QO_NODE *,
					    QO_NODE_INDEX_ENTRY *, BITSET *,
					    BITSET *, BITSET *);
static int qo_validate_index_for_orderby (QO_ENV * env,
					  QO_NODE_INDEX_ENTRY * ni_entryp);
static int qo_validate_index_for_groupby (QO_ENV * env,
					  QO_NODE_INDEX_ENTRY * ni_entryp);
static PT_NODE *search_isnull_key_expr_orderby (PARSER_CONTEXT * parser,
						PT_NODE * tree, void *arg,
						int *continue_walk);
static PT_NODE *search_isnull_key_expr_groupby (PARSER_CONTEXT * parser,
						PT_NODE * tree, void *arg,
						int *continue_walk);
static bool qo_check_orderby_skip_descending (QO_PLAN * plan);
static bool qo_check_groupby_skip_descending (QO_PLAN * plan, PT_NODE * list);
static void qo_plan_compute_iscan_group_sort_list (QO_PLAN * root,
						   PT_NODE ** out_list,
						   bool * is_index_w_prefix);

static int qo_walk_plan_tree (QO_PLAN * plan, QO_WALK_FUNCTION f, void *arg);
static int qo_set_use_desc (QO_PLAN * plan, void *arg);
static int qo_set_orderby_skip (QO_PLAN * plan, void *arg);
static int qo_validate_indexes_for_orderby (QO_PLAN * plan, void *arg);

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

#define DEFAULT_NULL_SELECTIVITY (double) 0.01
#define DEFAULT_EXISTS_SELECTIVITY (double) 0.1
#define DEFAULT_SELECTIVITY (double) 0.1
#define DEFAULT_EQUAL_SELECTIVITY (double) 0.001
#define DEFAULT_EQUIJOIN_SELECTIVITY (double) 0.001
#define DEFAULT_COMP_SELECTIVITY (double) 0.1
#define DEFAULT_BETWEEN_SELECTIVITY (double) 0.01
#define DEFAULT_IN_SELECTIVITY (double) 0.01
#define DEFAULT_RANGE_SELECTIVITY (double) 0.1

/*
 * WARNING: Be sure to update qo_integral_type[] whenever there are
 * changes to the set of DB_TYPE values.
 */
unsigned char qo_type_qualifiers[] = {
  0,				/* DB_TYPE_NULL         */
  _INT + _NUM,			/* DB_TYPE_INTEGER      */
  _NUM,				/* DB_TYPE_FLOAT        */
  _NUM,				/* DB_TYPE_DOUBLE       */
  0,				/* DB_TYPE_STRING       */
  0,				/* DB_TYPE_OBJECT       */
  0,				/* DB_TYPE_SET          */
  0,				/* DB_TYPE_MULTI_SET    */
  0,				/* DB_TYPE_SEQUENCE     */
  0,				/* DB_TYPE_ELO          */
  _INT + _NUM,			/* DB_TYPE_TIME         */
  _INT + _NUM,			/* DB_TYPE_UTIME        */
  _INT + _NUM,			/* DB_TYPE_DATE         */
  _NUM,				/* DB_TYPE_MONETARY     */
  0,				/* DB_TYPE_VARIABLE     */
  0,				/* DB_TYPE_SUB          */
  0,				/* DB_TYPE_POINTER      */
  0,				/* DB_TYPE_ERROR        */
  _INT,				/* DB_TYPE_SHORT        */
  0,				/* DB_TYPE_VOBJ         */
  0,				/* DB_TYPE_OID          */
  0,				/* DB_TYPE_DB_VALUE     */
  0,				/* DB_TYPE_NUMERIC      *//* FIXME */
  0,				/* DB_TYPE_BIT          */
  0,				/* DB_TYPE_VARBIT       */
  0,				/* DB_TYPE_CHAR         */
  0,				/* DB_TYPE_NCHAR        */
  0,				/* DB_TYPE_VARNCHAR     */
  0,				/* DB_TYPE_RESULTSET    */
  0,				/* DB_TYPE_MIDXKEY      */
  0,				/* DB_TYPE_TABLE        */
  _INT + _NUM,			/* DB_TYPE_BIGINT       */
  _INT + _NUM,			/* DB_TYPE_DATETIME     */
  0,				/* DB_TYPE_BLOB         */
  0				/* DB_TYPE_CLOB         */
};

/* Structural equivalence classes for expressions */

typedef enum PRED_CLASS
{
  PC_ATTR,
  PC_CONST,
  PC_HOST_VAR,
  PC_SUBQUERY,
  PC_SET,
  PC_OTHER
} PRED_CLASS;

static double
qo_or_selectivity (QO_ENV * env, double lhs_sel, double rhs_sel);

static double
qo_and_selectivity (QO_ENV * env, double lhs_sel, double rhs_sel);

static double qo_not_selectivity (QO_ENV * env, double sel);

static double qo_equal_selectivity (QO_ENV * env, PT_NODE * pt_expr);

static double qo_comp_selectivity (QO_ENV * env, PT_NODE * pt_expr);

static double qo_between_selectivity (QO_ENV * env, PT_NODE * pt_expr);

static double qo_range_selectivity (QO_ENV * env, PT_NODE * pt_expr);

static double qo_all_some_in_selectivity (QO_ENV * env, PT_NODE * pt_expr);

static PRED_CLASS qo_classify (PT_NODE * attr);

static bool qo_is_arithmetic_type (PT_NODE * attr);

static double get_const_value (QO_ENV * env, PT_NODE * attr);

static int qo_index_cardinality (QO_ENV * env, PT_NODE * attr);

static int
qo_get_range (QO_ENV * env, PT_NODE * attr, double *low_value,
	      double *high_value);

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
static QO_PLAN *
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
      plan = (QO_PLAN *) malloc (sizeof (QO_PLAN));
      if (plan == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, sizeof (QO_PLAN));
	  return NULL;
	}
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
qo_plan_compute_iscan_sort_list (QO_PLAN * root, bool * is_index_w_prefix)
{
  QO_PLAN *plan;
  QO_ENV *env;
  PARSER_CONTEXT *parser;
  PT_NODE *tree, *sort_list, *sort, *col, *node, *expr;
  QO_NODE_INDEX_ENTRY *ni_entryp;
  QO_INDEX_ENTRY *index_entryp;
  int nterms, equi_nterms, seg_idx, i, j;
  QO_SEGMENT *seg;
  PT_MISC_TYPE asc_or_desc;
  QFILE_TUPLE_VALUE_POSITION pos_descr;
  TP_DOMAIN *key_type;
  BITSET *terms;
  BITSET_ITERATOR bi;
  bool is_const_eq_term;

  *is_index_w_prefix = false;

  /* find sortable plan */
  plan = root;
  while (plan->plan_type == QO_PLANTYPE_FOLLOW
	 || (plan->plan_type == QO_PLANTYPE_JOIN
	     && (plan->plan_un.join.join_method == QO_JOINMETHOD_NL_JOIN
		 || (plan->plan_un.join.join_method ==
		     QO_JOINMETHOD_IDX_JOIN))))
    {
      plan = ((plan->plan_type == QO_PLANTYPE_FOLLOW)
	      ? plan->plan_un.follow.head : plan->plan_un.join.outer);
    }

  /* check for plan type */
  if (plan == NULL || plan->plan_type != QO_PLANTYPE_SCAN)
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
  if (plan->plan_type != QO_PLANTYPE_SCAN
      || (plan->plan_un.scan.scan_method != QO_SCANMETHOD_INDEX_SCAN
	  && plan->plan_un.scan.scan_method !=
	  QO_SCANMETHOD_INDEX_ORDERBY_SCAN
	  && plan->plan_un.scan.scan_method !=
	  QO_SCANMETHOD_INDEX_GROUPBY_SCAN)
      || (env = (plan->info)->env) == NULL
      || (parser = QO_ENV_PARSER (env)) == NULL
      || (tree = QO_ENV_PT_TREE (env)) == NULL)
    {
      return;			/* nop */
    }

  /* if no index scan terms, no index scan */
  nterms = bitset_cardinality (&(plan->plan_un.scan.terms));

  if (nterms <= 0 &&
      plan->plan_un.scan.scan_method != QO_SCANMETHOD_INDEX_ORDERBY_SCAN &&
      plan->plan_un.scan.scan_method != QO_SCANMETHOD_INDEX_GROUPBY_SCAN)
    {
      return;			/* nop */
    }

  /* pointer to QO_NODE_INDEX_ENTRY structure in QO_PLAN */
  ni_entryp = plan->plan_un.scan.index;
  /* pointer to linked list of index node, 'head' field(QO_INDEX_ENTRY
     strucutre) of QO_NODE_INDEX_ENTRY */
  index_entryp = (ni_entryp)->head;

  /* check if this is an index with prefix */
  *is_index_w_prefix = qo_is_prefix_index (index_entryp);

  asc_or_desc =
    (SM_IS_CONSTRAINT_REVERSE_INDEX_FAMILY (index_entryp->constraints->type) ?
     PT_DESC : PT_ASC);

  equi_nterms = plan->plan_un.scan.equi ? nterms : nterms - 1;
  if (index_entryp->rangelist_seg_idx != -1)
    {
      equi_nterms = MIN (equi_nterms, index_entryp->rangelist_seg_idx);
    }

  /* we must have the first index column appear as the first sort column, so
   * we pretend the number of equi columns is zero, to force it to match
   * the sort list and the index columns one-for-one. */
  if (index_entryp->is_iss_candidate)
    {
      equi_nterms = 0;
    }

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

      /* get the first non-equal range key domain */
      for (j = 0; j < equi_nterms && key_type; j++)
	{
	  key_type = key_type->next;
	}

      if (key_type == NULL)
	{			/* invalid case */
	  return;		/* nop */
	}
    }

  sort_list = NULL;		/* init */

  for (i = equi_nterms; i < index_entryp->nsegs; i++)
    {
      if (key_type)
	{
	  asc_or_desc = (key_type->is_desc) ? PT_DESC : PT_ASC;
	  key_type = key_type->next;
	}

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

      /* skip segment of const eq term */
      terms = &(QO_SEG_INDEX_TERMS (seg));
      is_const_eq_term = false;
      for (j = bitset_iterate (terms, &bi); j != -1;
	   j = bitset_next_member (&bi))
	{
	  expr = QO_TERM_PT_EXPR (QO_ENV_TERM (env, j));
	  if (PT_IS_EXPR_NODE_WITH_OPERATOR (expr, PT_EQ) &&
	      (PT_IS_CONST (expr->info.expr.arg1) ||
	       PT_IS_CONST (expr->info.expr.arg2)))
	    {
	      is_const_eq_term = true;
	    }
	}
      if (is_const_eq_term)
	{
	  continue;
	}

      pt_to_pos_descr (parser, &pos_descr, node, tree, NULL);
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

      col = pt_get_end_path_node (col);

      if (col->node_type == PT_NAME
	  && PT_NAME_INFO_IS_FLAGED (col, PT_NAME_INFO_CONSTANT))
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

      sort->info.sort_spec.expr = pt_point (parser, col);
      sort->info.sort_spec.pos_descr = pos_descr;
      sort->info.sort_spec.asc_or_desc = asc_or_desc;

      sort_list = parser_append_node (sort, sort_list);
    }

  root->iscan_sort_list = sort_list;

  return;
}

/*
 * qo_walk_plan_tree () - applies a callback to every plan in a plan tree
 *                        and stops on errors
 *   return:  the error of the first callback function that returns something
 *            other than NO_ERROR, or NO_ERROR.
 *   plan(in): the root of the plan tree to walk
 *   f(in): functor (callback) to apply to each non-null plan
 *   arg(in/out): argument to be used liberally by the callback
 */
static int
qo_walk_plan_tree (QO_PLAN * plan, QO_WALK_FUNCTION f, void *arg)
{
  int ret = NO_ERROR;

  if (!plan)
    {
      return NO_ERROR;
    }

  switch (plan->plan_type)
    {
    case QO_PLANTYPE_SCAN:
      return f (plan, arg);

    case QO_PLANTYPE_FOLLOW:
      return qo_walk_plan_tree (plan->plan_un.follow.head, f, arg);

    case QO_PLANTYPE_SORT:
      return qo_walk_plan_tree (plan->plan_un.sort.subplan, f, arg);

    case QO_PLANTYPE_JOIN:
      ret = qo_walk_plan_tree (plan->plan_un.join.outer, f, arg);
      if (ret != NO_ERROR)
	{
	  return ret;
	}

      return qo_walk_plan_tree (plan->plan_un.join.inner, f, arg);

    default:
      return ER_FAILED;
    }
}

/*
 * qo_set_use_desc () - sets certain plans' use_descending index flag to the
 *                      boolean value given in *arg.
 * 
 * return: NO_ERROR
 * plan(in):
 * arg(in): will be cast to bool* and its values used to set
 *          the use_descending property
 * note: the function only cares about index scans and skips other plan types
 */
static int
qo_set_use_desc (QO_PLAN * plan, void *arg)
{
  if (plan->plan_type == QO_PLANTYPE_SCAN &&
      (plan->plan_un.scan.scan_method == QO_SCANMETHOD_INDEX_ORDERBY_SCAN ||
       plan->plan_un.scan.scan_method == QO_SCANMETHOD_INDEX_SCAN))
    {
      bool yn = *((bool *) arg);
      plan->plan_un.scan.index->head->use_descending = yn;
    }

  return NO_ERROR;
}

/*
 * qo_set_orderby_skip () - sets certain plans' orderby_skip index flag to the
 *                          boolean value given in *arg.
 * 
 * return: NO_ERROR
 * plan(in):
 * arg(in): will be cast to bool* and its values used to set
 *          the orderby_skip property
 * note: the function only cares about index scans and skips other plan types
 */
static int
qo_set_orderby_skip (QO_PLAN * plan, void *arg)
{
  if (plan->plan_type == QO_PLANTYPE_SCAN &&
      (plan->plan_un.scan.scan_method == QO_SCANMETHOD_INDEX_ORDERBY_SCAN ||
       plan->plan_un.scan.scan_method == QO_SCANMETHOD_INDEX_SCAN))
    {
      bool yn = *((bool *) arg);
      plan->plan_un.scan.index->head->orderby_skip = yn;
    }

  return NO_ERROR;
}

/*
 * qo_validate_indexes_for_orderby () - wrapper function for
 *					qo_validate_index_for_orderby
 *                                      used with qo_walk_plan_tree.
 * return: NO_ERROR or ER_FAILED if the wrapped function returns false
 * plan(in):
 * arg(in): not used, must be NULL
 */
static int
qo_validate_indexes_for_orderby (QO_PLAN * plan, void *arg)
{
  if (plan->plan_type == QO_PLANTYPE_SCAN &&
      plan->plan_un.scan.scan_method == QO_SCANMETHOD_INDEX_ORDERBY_SCAN)
    {
      if (!qo_validate_index_for_orderby (plan->info->env,
					  plan->plan_un.scan.index))
	{
	  return ER_FAILED;
	}
    }

  return NO_ERROR;
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

  if (plan == NULL || plan->top_rooted)
    {
      return plan;		/* is already top-level plan - OK */
    }

  if (plan->info == NULL ||	/* worst plan */
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

      bool groupby_skip, orderby_skip, is_index_w_prefix;
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

      (void) qo_plan_compute_iscan_sort_list (plan, &is_index_w_prefix);

      /* GROUP BY */
      /* if we have rollup, we do not skip the group by */
      if (group_by && !group_by->with_rollup)
	{
	  PT_NODE *group_sort_list = NULL;

	  qo_plan_compute_iscan_group_sort_list (plan,
						 &group_sort_list,
						 &is_index_w_prefix);

	  if (group_sort_list)
	    {
	      if (found_instnum /* && found_grpynum */ )
		{
		  ;		/* give up */
		}
	      else
		{
		  groupby_skip =
		    pt_sort_spec_cover_groupby (plan->info->env->parser,
						group_sort_list, group_by,
						tree);

		  /* if index plan and can't skip group by, we search
		   * that maybe a descending scan can be used.
		   */
		  if (plan->plan_type == QO_PLANTYPE_SCAN &&
		      (plan->plan_un.scan.scan_method ==
		       QO_SCANMETHOD_INDEX_GROUPBY_SCAN
		       ||
		       plan->plan_un.scan.scan_method ==
		       QO_SCANMETHOD_INDEX_SCAN) && !groupby_skip)
		    {
		      groupby_skip = qo_check_groupby_skip_descending (plan,
								       group_sort_list);

		      if (groupby_skip)
			{
			  plan->plan_un.scan.
			    index->head->use_descending = true;
			}
		    }
		}
	    }

	  if (groupby_skip)
	    {
	      /* if the plan is index_groupby, we validate the plan
	       */
	      if (plan->plan_type == QO_PLANTYPE_SCAN
		  && plan->plan_un.scan.scan_method ==
		  QO_SCANMETHOD_INDEX_GROUPBY_SCAN)
		{
		  if (!qo_validate_index_for_groupby
		      (plan->info->env, plan->plan_un.scan.index))
		    {
		      /* drop the plan if it wasn't validated */
		      qo_worst_cost (plan);
		      return plan;
		    }
		}
	      /* if all goes well, we have an indexed plan with group by
	       * skip!
	       */
	      if (plan->plan_type == QO_PLANTYPE_SCAN &&
		  plan->plan_un.scan.index)
		{
		  plan->plan_un.scan.index->head->groupby_skip = true;
		}
	    }
	  else
	    {
	      /* if the order by is not skipped we drop the plan because it
	       * didn't helped us
	       */
	      if (plan->plan_type == QO_PLANTYPE_SCAN
		  && plan->plan_un.scan.scan_method ==
		  QO_SCANMETHOD_INDEX_GROUPBY_SCAN)
		{
		  qo_worst_cost (plan);
		  return plan;
		}

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
		      else if (!is_index_w_prefix &&
			       !tree->info.query.q.select.connect_by)
			{
			  orderby_skip =
			    pt_sort_spec_cover (plan->iscan_sort_list,
						order_by);

			  /* try using a reverse scan */
			  if (!orderby_skip)
			    {
			      orderby_skip =
				qo_check_orderby_skip_descending (plan);

			      if (orderby_skip)
				{
				  /* set the use_descending flag */
				  bool yn = true;
				  qo_walk_plan_tree (plan, qo_set_use_desc,
						     &yn);
				}
			    }
			}
		    }
		}		/* else */
	    }			/* if (plan->iscan_sort_list) */

	  if (orderby_skip)
	    {
	      if (plan->plan_type == QO_PLANTYPE_SCAN
		  && plan->plan_un.scan.scan_method ==
		  QO_SCANMETHOD_INDEX_GROUPBY_SCAN)
		{
		  /* group by skipping plan and we have order by skip -> drop
		   */
		  qo_worst_cost (plan);
		  return plan;
		}

	      if (orderby_for)
		{		/* apply inst_num filter */
		  ;		/* DO NOT DELETE ME - need future work */
		}

	      /* validate the index orderby plan or subplans
	       */
	      if (qo_walk_plan_tree
		  (plan, qo_validate_indexes_for_orderby, NULL) != NO_ERROR)
		{
		  /* drop the plan if it wasn't validated */
		  qo_worst_cost (plan);
		  return plan;
		}

	      /* if all goes well, we have an indexed plan with order by
	       * skip: set the flag to all suitable subplans
	       */
	      {
		bool yn = true;
		qo_walk_plan_tree (plan, qo_set_orderby_skip, &yn);
	      }
	    }
	  else
	    {
	      /* if the order by is not skipped we drop the plan because it
	       * didn't helped us
	       */
	      if (plan->plan_type == QO_PLANTYPE_SCAN
		  && plan->plan_un.scan.scan_method ==
		  QO_SCANMETHOD_INDEX_ORDERBY_SCAN)
		{
		  qo_worst_cost (plan);
		  return plan;
		}

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
      bitset_print (&(plan->subqueries), f);
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
  if (plan == NULL)
    {
      return NULL;
    }

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
      if (pt_expr
	  && PT_EXPR_INFO_IS_FLAGED (pt_expr, PT_EXPR_INFO_FULL_RANGE))
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
static QO_PLAN *
qo_seq_scan_new (QO_INFO * info, QO_NODE * node, BITSET * pinned_subqueries)
{
  QO_PLAN *plan;

  plan = qo_scan_new (info, node, QO_SCANMETHOD_SEQ_SCAN, pinned_subqueries);
  if (plan == NULL)
    {
      return NULL;
    }

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
static QO_PLAN *
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
  if (plan == NULL)
    {
      return NULL;
    }

  /*
   * This is, in essence, the selectivity of the index.  We
   * really need to do a better job of figuring out the cost of
   * an indexed scan.
   */
  plan->vtbl = &qo_index_scan_plan_vtbl;
  plan->plan_un.scan.index = ni_entry;

  bitset_assign (&(plan->plan_un.scan.terms), range_terms);	/* set key-range terms */
  for (t = bitset_iterate (range_terms, &iter);
       t != -1; t = bitset_next_member (&iter))
    {
      term = QO_ENV_TERM (env, t);
      if (!QO_TERM_IS_FLAGED (term, QO_TERM_EQUAL_OP))
	{
	  if (bitset_cardinality (&(plan->plan_un.scan.terms)) > 1)
	    {			/* is not the first term */
	      pt_expr = QO_TERM_PT_EXPR (term);

	      /* check for non-null RANGE sarg term only used for index scan
	       * only used for the first segment of the index key
	       */
	      if (pt_expr
		  && PT_EXPR_INFO_IS_FLAGED (pt_expr,
					     PT_EXPR_INFO_FULL_RANGE))
		{
		  bitset_remove (&(plan->plan_un.scan.terms),
				 QO_TERM_IDX (term));
		  continue;	/* go ahead */
		}
	    }
	  break;
	}
    }

  if (t == -1)
    {
      plan->plan_un.scan.equi = true;	/* is all equi-cond key-range terms */
    }
  else
    {
      plan->plan_un.scan.equi = false;
    }

  /* remove key-range terms from sarged terms */
  bitset_difference (&(plan->sarged_terms), range_terms);

  /* all segments consisting in key columns */
  index_entryp = (ni_entry)->head;
  for (t = 0; t < index_entryp->nsegs; t++)
    {
      if ((index_entryp->seg_idxs[t]) != -1)
	{
	  bitset_add (&index_segs, (index_entryp->seg_idxs[t]));
	}
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
      if (pt_expr
	  && PT_EXPR_INFO_IS_FLAGED (pt_expr, PT_EXPR_INFO_FULL_RANGE))
	{
	  continue;		/* skip out unnecessary term */
	}

      bitset_assign (&term_segs, &(QO_TERM_SEGS (term)));
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
  n = index_entryp->col_num;

  if (SM_IS_CONSTRAINT_UNIQUE_FAMILY (index_entryp->constraints->type)
      && n == index_entryp->nsegs)
    {
      assert (n > 0);

      for (i = 0; i < n; i++)
	{
	  if (bitset_is_empty (&index_entryp->seg_equal_terms[i]))
	    {
	      break;
	    }
	}

      if (i == n)
	{
	  /* When the index is a unique family and all of index columns
	   * are specified in the equal conditions,
	   * the cardinality of the scan will 0 or 1.
	   * In this case we will make the scan cost to zero,
	   * thus to force the optimizer to select this scan.
	   */
	  qo_zero_cost (planp);
	  return;
	}
    }

  pkeys_num = MIN (n, cum_statsp->key_size);
  if (bitset_is_empty (&(planp->plan_un.scan.terms)))
    {
      bool is_null_sel = false;
      sel = 0.1;

      assert (!index_entryp->is_iss_candidate);

      /* set selectivity limit */
      if (pkeys_num > 0 && cum_statsp->pkeys[0] > 0)
	{
	  sel_limit = 1.0 / (double) cum_statsp->pkeys[0];
	}
      else
	{
	  /* can not use btree partial-key statistics */
	  if (cum_statsp->keys > 0)
	    {
	      sel_limit = 1.0 / (double) cum_statsp->keys;
	    }
	  else
	    {
	      if (QO_NODE_NCARD (nodep) == 0)
		{		/* empty class */
		  sel = 0.0;
		  is_null_sel = true;
		}
	      else
		{
		  sel_limit = 1.0 / (double) QO_NODE_NCARD (nodep);
		}
	    }
	}

      /* check lower bound */
      if (is_null_sel == false)
	{
	  sel = MAX (sel, sel_limit);
	}
    }
  else
    {
      i = 0;

      /* for index skip scan, we should pre-compute the first column's
       * selectivity and check that we have relevant statistics. */
      if (index_entryp->is_iss_candidate)
	{
	  /* we can't do proper index skip scan analysis without 
	   * relevant statistics */
	  if (pkeys_num <= 0 || cum_statsp->pkeys[0] <= 0)
	    {
	      qo_worst_cost (planp);
	      return;
	    }

	  i = 1;
	  n--;
	}

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

      if (index_entryp->is_iss_candidate)
	{
	  sel = sel * (double) cum_statsp->pkeys[0];
	}
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

  /* Index Skip Scan adds to the index IO cost the K extra BTREE searches
   * it does to fetch the next value for the following BTRangeScan */
  if (index_entryp->is_iss_candidate)
    {
      /* The btree is scanned an additional K times */
      index_IO += cum_statsp->pkeys[0] * ((ni_entryp)->n * height);

      /* K leaves are additionally read */
      index_IO += cum_statsp->pkeys[0];
    }

  /* IO cost to fetch objects */
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
  bool natural_desc_index = false;

  fprintf (f, "\n" INDENTED_TITLE_FMT, (int) howfar, ' ', "class:");
  qo_node_fprint (plan->plan_un.scan.node, f);

  if (plan->plan_un.scan.scan_method == QO_SCANMETHOD_INDEX_SCAN ||
      plan->plan_un.scan.scan_method == QO_SCANMETHOD_INDEX_ORDERBY_SCAN ||
      plan->plan_un.scan.scan_method == QO_SCANMETHOD_INDEX_GROUPBY_SCAN)
    {
      fprintf (f, "\n" INDENTED_TITLE_FMT, (int) howfar, ' ', "index: ");
      fprintf (f, "%s ", plan->plan_un.scan.index->head->constraints->name);

      /* print key limit */
      if (plan->plan_un.scan.index->head->key_limit)
	{
	  PT_NODE *key_limit = plan->plan_un.scan.index->head->key_limit;
	  PT_NODE *saved_next = key_limit->next;
	  PARSER_CONTEXT *parser = QO_ENV_PARSER (plan->info->env);
	  PT_PRINT_VALUE_FUNC saved_func = parser->print_db_value;
	  parser->print_db_value = pt_print_node_value;
	  if (saved_next)
	    {
	      saved_next->next = key_limit;
	      key_limit->next = NULL;
	    }
	  fprintf (f, "keylimit %s ",
		   parser_print_tree_list (parser, saved_next ?
					   saved_next : key_limit));
	  parser->print_db_value = saved_func;
	  if (saved_next)
	    {
	      key_limit->next = saved_next;
	      saved_next->next = NULL;
	    }
	}

      qo_termset_fprint ((plan->info)->env, &plan->plan_un.scan.terms, f);

      /* print index covering */
      if (plan->plan_un.scan.index
	  && plan->plan_un.scan.index->head->cover_segments
	  && qo_is_prefix_index (plan->plan_un.scan.index->head) == false)
	{
	  if (bitset_cardinality (&(plan->plan_un.scan.terms)) > 0)
	    {
	      fprintf (f, " ");
	    }
	  fprintf (f, "(covers)");
	}

      if (plan->plan_un.scan.index
	  && plan->plan_un.scan.index->head->is_iss_candidate)
	{
	  fprintf (f, " (index skip scan)");
	}

      if (plan->plan_un.scan.index
	  && plan->plan_un.scan.index->head->use_descending)
	{
	  fprintf (f, " (desc_index)");
	  natural_desc_index = true;
	}

      if (!natural_desc_index &&
	  (QO_ENV_PT_TREE (plan->info->env)->info.query.q.select.hint &
	   PT_HINT_USE_IDX_DESC))
	{
	  fprintf (f, " (desc_index forced)");
	}

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
      for (i = 0, n = QO_NODE_INFO_N (node); i < n; i++)
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

  if (plan->plan_un.scan.scan_method == QO_SCANMETHOD_INDEX_SCAN ||
      plan->plan_un.scan.scan_method == QO_SCANMETHOD_INDEX_ORDERBY_SCAN)
    {
      BITSET_ITERATOR bi;
      QO_ENV *env;
      int i;
      const char *separator;
      bool natural_desc_index = false;

      env = (plan->info)->env;
      separator = ", ";

      fprintf (f, "%s%s", separator,
	       plan->plan_un.scan.index->head->constraints->name);

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

      /* print index covering */
      if (plan->plan_un.scan.index
	  && plan->plan_un.scan.index->head->cover_segments
	  && qo_is_prefix_index (plan->plan_un.scan.index->head) == false)
	{
	  fprintf (f, " (covers)");
	}

      if (plan->plan_un.scan.index
	  && plan->plan_un.scan.index->head->is_iss_candidate)
	{
	  fprintf (f, " (index skip scan)");
	}

      if (plan->plan_un.scan.index
	  && plan->plan_un.scan.index->head->use_descending)
	{
	  fprintf (f, " (desc_index)");
	  natural_desc_index = true;
	}

      if (!natural_desc_index &&
	  (QO_ENV_PT_TREE (plan->info->env)->info.query.q.select.hint &
	   PT_HINT_USE_IDX_DESC))
	{
	  fprintf (f, " (desc_index forced)");
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
static QO_PLAN *
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
	  if (subplan->top_rooted
	      && subplan->plan_un.sort.sort_type != SORT_TEMP)
	    {
	      ;			/* skip and go ahead */
	    }
	  else
	    {
	      break;		/* is not top-level sort plan */
	    }
	}

      /* check for dummy sort plan */
      if (order == QO_UNORDERED && subplan != NULL
	  && subplan->plan_type == QO_PLANTYPE_SORT)
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

  if (subplan == NULL)
    {
      return NULL;
    }

  plan = qo_plan_malloc ((subplan->info)->env);
  if (plan == NULL)
    {
      return NULL;
    }

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

  if (subplanp->plan_type == QO_PLANTYPE_SORT
      && planp->plan_un.sort.sort_type == SORT_TEMP)
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
static QO_PLAN *
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
  if (plan == NULL)
    {
      return NULL;
    }

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
      if (join_method == QO_JOINMETHOD_NL_JOIN)
	{
	  plan->vtbl = &qo_nl_join_plan_vtbl;
	}
      else
	{
	  plan->vtbl = &qo_idx_join_plan_vtbl;
	}
      plan->order = QO_UNORDERED;

      /* These checks are necessary because of restrictions in the
         current XASL implementation of nested loop joins.
         Never put anything on the inner plan that isn't file-based
         (i.e., a scan of either a heap file or a list file). */
      if (!VALID_INNER (inner))
	{
	  inner = qo_sort_new (inner, inner->order, SORT_TEMP);
	}
      else if (IS_OUTER_JOIN_TYPE (join_type))
	{
	  /* for outer join,
	   * if inner plan is a scan of classes in hierarchy */
	  if (inner->plan_type == QO_PLANTYPE_SCAN
	      && QO_NODE_INFO (inner->plan_un.scan.node)
	      && QO_NODE_INFO_N (inner->plan_un.scan.node) > 1)
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
  spec = QO_NODE_ENTITY_SPEC (node);
  if (spec
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
static void
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

  if (inner->plan_type == QO_PLANTYPE_SCAN
      && inner->plan_un.scan.scan_method == QO_SCANMETHOD_INDEX_SCAN
      && inner->plan_un.scan.equi == true)
    {
      /* correlated index equi-join */
      inner_cpu_cost += inner->variable_cpu_cost;
      if (outer->plan_type == QO_PLANTYPE_SCAN
	  && outer->plan_un.scan.scan_method == QO_SCANMETHOD_SEQ_SCAN
	  && PRM_MAX_OUTER_CARD_OF_IDXJOIN != 0
	  && guessed_result_cardinality > PRM_MAX_OUTER_CARD_OF_IDXJOIN)
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

      if (outer->plan_type == QO_PLANTYPE_SCAN
	  && outer->plan_un.scan.scan_method == QO_SCANMETHOD_SEQ_SCAN)
	{
	  if (inner->plan_un.scan.scan_method == QO_SCANMETHOD_SEQ_SCAN)
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
		      io_cost = (inner->variable_io_cost +
				 MIN (inner_io_cost, pages2 * 2));
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
	{
	  /* outer join leads nongrouped scan overhead */
	  inner_cpu_cost +=
	    ((outer->info)->cardinality * pages * NONGROUPED_SCAN_COST);
	}
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
static QO_PLAN *
qo_follow_new (QO_INFO * info,
	       QO_PLAN * head_plan,
	       QO_TERM * path_term,
	       BITSET * sarged_terms, BITSET * pinned_subqueries)
{
  QO_PLAN *plan;

  plan = qo_plan_malloc (info->env);
  if (plan == NULL)
    {
      return NULL;
    }

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
static QO_PLAN *
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
static QO_PLAN *
qo_worst_new (QO_ENV * env)
{
  QO_PLAN *plan;

  plan = qo_plan_malloc (env);
  if (plan == NULL)
    {
      return NULL;
    }

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
static QO_PLAN *
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
 * qo_plan_cmp_prefer_covering_index () -
 *   return: one of {PLAN_COMP_UNK, PLAN_COMP_LT, PLAN_COMP_GT}
 *   scan_plan_p(in):
 *   sort_plan_p(in):
 */
static QO_PLAN_COMPARE_RESULT
qo_plan_cmp_prefer_covering_index (QO_PLAN * scan_plan_p,
				   QO_PLAN * sort_plan_p)
{
  QO_PLAN *sort_subplan_p;

  assert (scan_plan_p->plan_type == QO_PLANTYPE_SCAN
	  && sort_plan_p->plan_type == QO_PLANTYPE_SORT);

  sort_subplan_p = sort_plan_p->plan_un.sort.subplan;
  if (sort_subplan_p->plan_type == QO_PLANTYPE_SCAN
      && sort_subplan_p->plan_un.scan.scan_method == QO_SCANMETHOD_INDEX_SCAN
      && sort_subplan_p->plan_un.scan.index->head->cover_segments)
    {
      /* if the sort plan contains a index plan with segment covering,
       * prefer it
       */
      if ((scan_plan_p->plan_un.scan.scan_method == QO_SCANMETHOD_INDEX_SCAN
	   || scan_plan_p->plan_un.scan.scan_method ==
	   QO_SCANMETHOD_INDEX_ORDERBY_SCAN
	   || scan_plan_p->plan_un.scan.scan_method ==
	   QO_SCANMETHOD_INDEX_GROUPBY_SCAN)
	  && scan_plan_p->plan_un.scan.index->head->cover_segments)
	{
	  if (scan_plan_p->plan_un.scan.index->head ==
	      sort_subplan_p->plan_un.scan.index->head)
	    {
	      return PLAN_COMP_LT;
	    }
	}
      else
	{
	  return PLAN_COMP_GT;
	}
    }

  return PLAN_COMP_UNK;
}

/*
 * qo_plan_cmp () -
 *   return: one of {PLAN_COMP_UNK, PLAN_COMP_LT, PLAN_COMP_EQ, PLAN_COMP_GT}
 *   a(in):
 *   b(in):
 */
static QO_PLAN_COMPARE_RESULT
qo_plan_cmp (QO_PLAN * a, QO_PLAN * b)
{

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
  QO_PLAN_COMPARE_RESULT temp_res;

  /* skip out top-level sort plan */
  if (a->top_rooted && b->top_rooted)
    {
      /* skip out the same sort plan */
      while (a->plan_type == QO_PLANTYPE_SORT
	     && b->plan_type == QO_PLANTYPE_SORT
	     && a->plan_un.sort.sort_type == b->plan_un.sort.sort_type)
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

  /* a order by skip plan is always preferred to a sort plan */
  if (a->plan_type == QO_PLANTYPE_SCAN && b->plan_type == QO_PLANTYPE_SORT)
    {
      temp_res = qo_plan_cmp_prefer_covering_index (a, b);
      if (temp_res == PLAN_COMP_LT || temp_res == PLAN_COMP_GT)
	{
	  return temp_res;
	}

      if (a->plan_un.scan.index && a->plan_un.scan.index->head->groupby_skip)
	{
	  return PLAN_COMP_LT;
	}
      if (a->plan_un.scan.index && a->plan_un.scan.index->head->orderby_skip)
	{
	  return PLAN_COMP_LT;
	}
    }

  if (b->plan_type == QO_PLANTYPE_SCAN && a->plan_type == QO_PLANTYPE_SORT)
    {
      temp_res = qo_plan_cmp_prefer_covering_index (b, a);

      /* Since we swapped its position, we have to negate the comp result */
      if (temp_res == PLAN_COMP_LT)
	{
	  return PLAN_COMP_GT;
	}
      else if (temp_res == PLAN_COMP_GT)
	{
	  return PLAN_COMP_LT;
	}

      if (b->plan_un.scan.index && b->plan_un.scan.index->head->groupby_skip)
	{
	  return PLAN_COMP_GT;
	}
      if (b->plan_un.scan.index && b->plan_un.scan.index->head->orderby_skip)
	{
	  return PLAN_COMP_GT;
	}
    }

  if (a->plan_type == QO_PLANTYPE_SCAN && b->plan_type == QO_PLANTYPE_SCAN)
    {
      if ((a->plan_un.scan.scan_method == QO_SCANMETHOD_INDEX_SCAN ||
	   a->plan_un.scan.scan_method == QO_SCANMETHOD_INDEX_ORDERBY_SCAN ||
	   a->plan_un.scan.scan_method == QO_SCANMETHOD_INDEX_GROUPBY_SCAN) &&
	  a->plan_un.scan.index->head->cover_segments &&
	  b->plan_un.scan.scan_method == QO_SCANMETHOD_SEQ_SCAN)
	{
	  return PLAN_COMP_LT;
	}

      if ((b->plan_un.scan.scan_method == QO_SCANMETHOD_INDEX_SCAN ||
	   b->plan_un.scan.scan_method == QO_SCANMETHOD_INDEX_ORDERBY_SCAN ||
	   b->plan_un.scan.scan_method == QO_SCANMETHOD_INDEX_GROUPBY_SCAN) &&
	  b->plan_un.scan.index->head->cover_segments &&
	  a->plan_un.scan.scan_method == QO_SCANMETHOD_SEQ_SCAN)
	{
	  return PLAN_COMP_GT;
	}

      /* a plan does order by skip, the other does group by skip - prefer the
       * group by skipping because it's done in the final step
       */
      if (a->plan_un.scan.scan_method != QO_SCANMETHOD_SEQ_SCAN &&
	  b->plan_un.scan.scan_method != QO_SCANMETHOD_SEQ_SCAN)
	{
	  if (a->plan_un.scan.index->head->orderby_skip &&
	      b->plan_un.scan.index->head->groupby_skip)
	    {
	      return PLAN_COMP_LT;
	    }
	  else if (a->plan_un.scan.index->head->groupby_skip &&
		   b->plan_un.scan.index->head->orderby_skip)
	    {
	      return PLAN_COMP_GT;
	    }
	}
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
  if ((a->plan_un.scan.scan_method != QO_SCANMETHOD_INDEX_SCAN &&
       a->plan_un.scan.scan_method != QO_SCANMETHOD_INDEX_ORDERBY_SCAN &&
       a->plan_un.scan.scan_method != QO_SCANMETHOD_INDEX_GROUPBY_SCAN)
      ||
      (b->plan_un.scan.scan_method != QO_SCANMETHOD_INDEX_SCAN &&
       b->plan_un.scan.scan_method != QO_SCANMETHOD_INDEX_ORDERBY_SCAN &&
       b->plan_un.scan.scan_method != QO_SCANMETHOD_INDEX_GROUPBY_SCAN))
    {
      goto cost_cmp;		/* give up */
    }

  /* check index coverage */
  temp_res = qo_cover_index_plans_cmp (a, b);
  if (temp_res == PLAN_COMP_LT || temp_res == PLAN_COMP_GT)
    {
      return temp_res;
    }

  /* check if one of the plans skips the order by, and if so, prefer it */
  {
    QO_PLAN_COMPARE_RESULT temp_res;
    temp_res = qo_order_by_skip_plans_cmp (a, b);
    if (temp_res == PLAN_COMP_LT || temp_res == PLAN_COMP_GT)
      {
	return temp_res;
      }
  }

  /* check if one of the plans skips the group by, and if so, prefer it */
  {
    QO_PLAN_COMPARE_RESULT temp_res;
    temp_res = qo_group_by_skip_plans_cmp (a, b);
    if (temp_res == PLAN_COMP_LT || temp_res == PLAN_COMP_GT)
      {
	return temp_res;
      }
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
	/* take the one which has more key filters */
	if (at_kf > bt_kf)
	  {
	    return PLAN_COMP_LT;
	  }
	else if (at_kf < bt_kf)
	  {
	    return PLAN_COMP_GT;
	  }

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
	if (a_last > 0)
	  {
	    if (bitset_cardinality (&(a->plan_un.scan.terms)) > 0)
	      {
		term =
		  QO_ENV_TERM ((a->info)->env,
			       bitset_first_member (&
						    (a->plan_un.scan.terms)));
		a_keys = (int) ceil (1.0 / QO_TERM_SELECTIVITY (term));
	      }
	    else
	      {
		a_keys = (int) ceil (1.0 / DEFAULT_SELECTIVITY);
	      }

	    a_keys = MIN (a_cum->pkeys[0], a_keys);
	  }
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
	if (b_last > 0)
	  {
	    if (bitset_cardinality (&(b->plan_un.scan.terms)) > 0)
	      {
		term =
		  QO_ENV_TERM ((b->info)->env,
			       bitset_first_member (&
						    (b->plan_un.scan.terms)));
		b_keys = (int) ceil (1.0 / QO_TERM_SELECTIVITY (term));
	      }
	    else
	      {
		b_keys = (int) ceil (1.0 / DEFAULT_SELECTIVITY);
	      }

	    b_keys = MIN (b_cum->pkeys[0], b_keys);
	  }
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

    if (af == bf && aa == ba)
      {
	if (a_ent->cover_segments && b_ent->cover_segments)
	  {
	    if (a_ent->col_num > b_ent->col_num)
	      {
		return PLAN_COMP_GT;
	      }
	    else if (a_ent->col_num < b_ent->col_num)
	      {
		return PLAN_COMP_LT;
	      }
	  }

	/* if both plans skip order by and same costs, take the larger one */
	if (a_ent->orderby_skip && b_ent->orderby_skip)
	  {
	    if (a_ent->col_num > b_ent->col_num)
	      {
		return PLAN_COMP_LT;
	      }
	    else if (a_ent->col_num < b_ent->col_num)
	      {
		/* if the new plan has more columns, prefer it */
		return PLAN_COMP_GT;
	      }
	  }

	/* if both plans skip group by and same costs, take the larger one */
	if (a_ent->groupby_skip && b_ent->groupby_skip)
	  {
	    if (a_ent->col_num > b_ent->col_num)
	      {
		return PLAN_COMP_LT;
	      }
	    else if (a_ent->col_num < b_ent->col_num)
	      {
		/* if the new plan has more columns, prefer it */
		return PLAN_COMP_GT;
	      }
	  }
      }

  }

cost_cmp:

  if (a == b || (af == bf && aa == ba))
    {
      return PLAN_COMP_EQ;
    }
  if (af <= bf && aa <= ba)
    {
      return PLAN_COMP_LT;
    }
  if (bf <= af && ba <= aa)
    {
      return PLAN_COMP_GT;
    }

  return PLAN_COMP_UNK;
#endif /* OLD_CODE */
}

/*
 * qo_cover_index_plans_cmp () - compare 2 index scan plans by coverage
 *   return:  one of {PLAN_COMP_UNK, PLAN_COMP_LT, PLAN_COMP_EQ, PLAN_COMP_GT}
 *   a(in):
 *   b(in):
 */
static QO_PLAN_COMPARE_RESULT
qo_cover_index_plans_cmp (QO_PLAN * a, QO_PLAN * b)
{
  QO_INDEX_ENTRY *a_ent, *b_ent;

  if (a == NULL || b == NULL ||
      (a->plan_un.scan.scan_method != QO_SCANMETHOD_INDEX_SCAN &&
       a->plan_un.scan.scan_method != QO_SCANMETHOD_INDEX_ORDERBY_SCAN)
      ||
      (b->plan_un.scan.scan_method != QO_SCANMETHOD_INDEX_SCAN &&
       b->plan_un.scan.scan_method != QO_SCANMETHOD_INDEX_ORDERBY_SCAN))
    {
      return PLAN_COMP_UNK;
    }

  a_ent = a->plan_un.scan.index->head;
  b_ent = b->plan_un.scan.index->head;

  if (a_ent == NULL || b_ent == NULL)
    {
      return PLAN_COMP_UNK;
    }

  if (a_ent->cover_segments)
    {
      if (b_ent->cover_segments)
	{
	  if (bitset_cardinality (&(a->plan_un.scan.terms)) > 0)
	    {
	      if (bitset_cardinality ((&b->plan_un.scan.terms)) == 0)
		{
		  return PLAN_COMP_LT;
		}
	    }
	  else
	    {
	      if (bitset_cardinality (&(b->plan_un.scan.terms)) > 0)
		{
		  return PLAN_COMP_GT;
		}
	    }
	}
      else
	{
	  return PLAN_COMP_LT;
	}
    }
  else
    {
      if (b_ent->cover_segments)
	{
	  return PLAN_COMP_GT;
	}
    }

  return PLAN_COMP_EQ;
}

/*
 * qo_plan_fprint () -
 *   return:
 *   plan(in):
 *   f(in):
 *   howfar(in):
 *   title(in):
 */
static void
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
static QO_PLAN *
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

#if defined (CUBRID_DEBUG)
      if (dump_enable)
	qo_print_stats (stdout);
#endif
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
static void
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
static void
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
      free_and_init (plan);
    }
  ++qo_plans_deallocated;
}

/*
 * qo_plan_free () -
 *   return:
 *   plan(in):
 */
static void
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
static void
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
static void
qo_plans_teardown (QO_ENV * env)
{
  while (qo_plan_free_list)
    {
      QO_PLAN *plan = qo_plan_free_list;

      qo_plan_free_list = plan->plan_un.free.link;
      if (plan)
	{
	  free_and_init (plan);
	}
      ++qo_plans_demalloced;
    }
  qo_accumulating_plans = false;
}

#if defined (CUBRID_DEBUG)
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
#endif

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
      plan_string = DB_PULL_STRING (plan);
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
      cost_string = DB_PULL_STRING (cost);
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
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      DB_MAKE_ERROR (result, ER_GENERIC_ERROR);
    }
}

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

#if defined (CUBRID_DEBUG)
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
#endif

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

  if (plan == NULL)
    {
      /* There is no new plan to be compared. */
      return PLAN_COMP_LT;
    }

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

  info = (QO_INFO *) malloc (sizeof (QO_INFO));
  if (info == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      sizeof (QO_INFO));
      return NULL;
    }

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
   * If this malloc() fails, we'll lose the memory pointed to by
   * info.  I'll take the chance.
   */
  info->planvec = NULL;
  if (EQ > 0)
    {
      info->planvec = (QO_PLANVEC *) malloc (sizeof (QO_PLANVEC) * EQ);
      if (info->planvec == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (QO_PLANVEC) * EQ);
	  free_and_init (info);
	  return NULL;
	}
    }

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

  free_and_init (info);

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
      free_and_init (info->planvec);
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
  QO_INFO *best_info;
  QO_EQCLASS *plan_order;
  QO_PLAN_COMPARE_RESULT cmp;
  bool found_new_best;

  if (info == NULL || plan == NULL)
    {
      return 0;
    }

  /* init */
  found_new_best = false;
  best_info = info->planner->best_info;
  plan_order = plan->order;

  /* if the plan is of type QO_SCANMETHOD_INDEX_ORDERBY_SCAN but it doesn't
   * skip the orderby, we release the plan.
   */
  if (plan->plan_type == QO_PLANTYPE_SCAN &&
      plan->plan_un.scan.scan_method == QO_SCANMETHOD_INDEX_ORDERBY_SCAN &&
      !plan->plan_un.scan.index->head->orderby_skip)
    {
      qo_plan_release (plan);
      return 0;
    }

  /* if the plan is of type QO_SCANMETHOD_INDEX_GRUOPBY_SCAN but it doesn't
   * skip the groupby, we release the plan.
   */
  if (plan->plan_type == QO_PLANTYPE_SCAN &&
      plan->plan_un.scan.scan_method == QO_SCANMETHOD_INDEX_GROUPBY_SCAN &&
      !plan->plan_un.scan.index->head->groupby_skip)
    {
      qo_plan_release (plan);
      return 0;
    }

  /*
   * If the cost of the new Plan already exceeds the cost of the best
   * known solution with the same order, there is no point in
   * remembering the new plan.
   */
  if (best_info)
    {
      cmp = qo_cmp_planvec (plan_order == QO_UNORDERED
			    ? &best_info->best_no_order
			    :
			    &best_info->planvec[QO_EQCLASS_IDX (plan_order)],
			    plan);
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
  if (temp == NULL)
    {
      return NULL;
    }

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

      outer_plan = qo_find_best_plan_on_info (inner, QO_UNORDERED, 1.0);
      if (outer_plan == NULL)
	{
	  goto exit;
	}
      inner_plan =
	qo_find_best_nljoin_inner_plan_on_info (outer_plan, outer, join_type);
      if (inner_plan == NULL)
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

      outer_plan = qo_find_best_plan_on_info (outer, QO_UNORDERED, 1.0);
      if (outer_plan == NULL)
	{
	  goto exit;
	}
      inner_plan =
	qo_find_best_nljoin_inner_plan_on_info (outer_plan, inner, join_type);
      if (inner_plan == NULL)
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
      /* inner does not have any usable index */
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
}

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
    {
      return 0;
    }

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

#if defined (CUBRID_DEBUG)
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
  bitset_print (&(info->projected_segs), f);
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
#endif

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

  planner = (QO_PLANNER *) malloc (sizeof (QO_PLANNER));
  if (planner == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (QO_PLANNER));
      return NULL;
    }

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
      free_and_init (planner->node_info);
    }

  if (planner->join_info)
    {
      free_and_init (planner->join_info);
    }

  if (planner->cp_info)
    {
      free_and_init (planner->cp_info);
    }

  free_and_init (planner);
}

#if defined (CUBRID_DEBUG)
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
#endif

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
  PT_NODE *pt_expr;
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
			  if (QO_TERM_CLASS (term) == QO_TC_PATH
			      && BITSET_MEMBER (*nested_path_nodes,
						QO_NODE_IDX (QO_TERM_HEAD
							     (term))))
			    {
			      found_idx = QO_NODE_IDX (QO_TERM_TAIL (term));
			      /* found nested path term */
			      if (!BITSET_MEMBER (*nested_path_nodes,
						  found_idx))
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
			  if (QO_TERM_CLASS (term) == QO_TC_PATH
			      && BITSET_MEMBER (*nested_path_nodes,
						QO_NODE_IDX (QO_TERM_TAIL
							     (term))))
			    {
			      found_idx = QO_NODE_IDX (QO_TERM_HEAD (term));
			      /* found nested path term */
			      if (!BITSET_MEMBER (*nested_path_nodes,
						  found_idx))
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
	planner->join_info[QO_INFO_INDEX
			   (QO_PARTITION_M_OFFSET (partition),
			    *visited_rel_nodes)];
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
    planner->join_info[QO_INFO_INDEX
		       (QO_PARTITION_M_OFFSET (partition),
			*visited_rel_nodes)];

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

	pt_expr = QO_TERM_PT_EXPR (term);

	/* check for non-null RANGE sarg term only used for index scan
	 * only used for the first segment of the index key
	 */
	if (pt_expr
	    && PT_EXPR_INFO_IS_FLAGED (pt_expr, PT_EXPR_INFO_FULL_RANGE))
	  {
	    continue;		/* skip and go ahead */
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

	    /* check for always true dummy join term */
	    if (QO_TERM_CLASS (term) == QO_TC_DUMMY_JOIN)
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

	      default:
		QO_ASSERT (planner->env, UNEXPECTED_CASE);
		break;
	      }
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

      double selectivity, cardinality;
      BITSET eqclasses;

      bitset_init (&eqclasses, planner->env);


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
	      if (QO_IS_PATH_TERM (term)
		  && QO_TERM_JOIN_TYPE (term) != JOIN_INNER)
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
	    }
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
	planner->join_info[QO_INFO_INDEX
			   (QO_PARTITION_M_OFFSET (partition),
			    *visited_rel_nodes)] =
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
	       * the node.
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
	      /* already apply inner cost: apply only outer cost */
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
	   * the node.
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
  if (planner == NULL)
    {
      return NULL;
    }

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
  QO_PLAN *inner_plan;
  int i, t, last_t, j, n, seg;
  bool found_rangelist;
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

  found_rangelist = false;
  for (i = 0; i < index_entryp->nsegs; i++)
    {
      seg = index_entryp->seg_idxs[i];
      if (seg == -1)
	{
	  break;
	}
      n = 0;
      last_t = -1;
      for (t = bitset_iterate (indexable_terms, &iter); t != -1;
	   t = bitset_next_member (&iter))
	{
	  termp = QO_ENV_TERM (env, t);

	  /* check for always true dummy join term */
	  if (QO_TERM_CLASS (termp) == QO_TC_DUMMY_JOIN)
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
		      if (QO_TERM_IS_FLAGED (termp, QO_TERM_RANGELIST))
			{
			  if (found_rangelist == true)
			    {
			      break;	/* already found. give up */
			    }

			  /* is the first time */
			  found_rangelist = true;
			}

		      bitset_add (&range_terms, t);
		      n++;
		    }

		  break;
		}
	    }

	  /* found EQ term. exit term-iteration loop */
	  if (n)
	    {
	      break;
	    }
	}

      /* not found EQ term. exit seg-iteration loop */
      if (n == 0)
	{
	  /* found term. add last non-EQ term */
	  if (last_t != -1)
	    {
	      if (found_rangelist == true)
		{
		  termp = QO_ENV_TERM (env, last_t);
		  if (QO_TERM_IS_FLAGED (termp, QO_TERM_RANGELIST))
		    {
		      break;	/* give up */
		    }
		}
	      bitset_add (&range_terms, last_t);
	    }
	  break;
	}
    }

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
						     inner_plan,
						     &empty_terms,
						     &empty_terms,
						     afj_terms,
						     &remaining_terms,
						     pinned_subqueries));
    }

  bitset_delset (&remaining_terms);
  bitset_delset (&empty_terms);
  bitset_delset (&kf_terms);
  bitset_delset (&range_terms);

  return n;
}

/*
 * qo_generate_seq_scan () - Generates sequential scan plan
 *   return: nothing
 *   infop(in): pointer to QO_INFO (environment info node which holds plans)
 *   nodep(in): pointer to QO_NODE (node in the join graph)
 */
static void
qo_generate_seq_scan (QO_INFO * infop, QO_NODE * nodep)
{
  int n;
  QO_PLAN *planp;
  bool plan_created = false;

  planp = qo_seq_scan_new (infop, nodep, &QO_NODE_SUBQUERIES (nodep));

  n = qo_check_plan_on_info (infop, planp);
  if (n)
    {
      plan_created = true;
    }
}

/*
 * qo_generate_index_scan () - With index information, generates index scan plan
 *   return: num of index scan plans
 *   infop(in): pointer to QO_INFO (environment info node which holds plans)
 *   nodep(in): pointer to QO_NODE (node in the join graph)
 *   ni_entryp(in): pointer to QO_NODE_INDEX_ENTRY (node index entry)
 */
static int
qo_generate_index_scan (QO_INFO * infop, QO_NODE * nodep,
			QO_NODE_INDEX_ENTRY * ni_entryp)
{
  QO_INDEX_ENTRY *index_entryp;
  BITSET_ITERATOR iter;
  QO_TERM *term;
  PT_NODE *pt_expr;
  int i, t, nsegs, n, normal_index_plan_n = 0;
  QO_PLAN *planp;
  BITSET range_terms;
  BITSET kf_terms;
  BITSET seg_other_terms;
  bool plan_created = false;
  bool no_kf_terms = false;
  int start_column = 0;

  bitset_init (&range_terms, infop->env);
  bitset_init (&kf_terms, infop->env);
  bitset_init (&seg_other_terms, infop->env);

  /* pointer to QO_INDEX_ENTRY structure */
  index_entryp = (ni_entryp)->head;
  start_column = index_entryp->is_iss_candidate ? 1 : 0;

  if (index_entryp->constraints->func_index_info)
    {
      no_kf_terms = true;
    }
  if (QO_ENTRY_MULTI_COL (index_entryp))
    {
      /* the section below counts the total number of segments including
       * all equal ones + the next non-equal found. These will be the range
       * segments. For example: SELECT ... WHERE a = 1 and b = 2 and c > 10
       * chooses all a, b, c for range because the first two are with =
       */
      for (nsegs = start_column; nsegs < index_entryp->nsegs; nsegs++)
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

      if (nsegs == 0)
	{
	  if (bitset_cardinality (&(index_entryp->key_filter_terms)) &&
	      index_entryp->cover_segments)
	    {
	      bitset_assign (&kf_terms, &(index_entryp->key_filter_terms));
	      planp = qo_index_scan_new (infop, nodep, ni_entryp,
					 &range_terms,
					 &kf_terms,
					 &QO_NODE_SUBQUERIES (nodep));

	      n = qo_check_plan_on_info (infop, planp);
	      if (n)
		{
		  if (start_column == 0)
		    {
		      normal_index_plan_n++;
		    }
		}
	    }

	  goto end;
	}

      for (i = start_column; i < nsegs - 1; i++)
	{
	  bitset_add (&range_terms,
		      bitset_first_member (&
					   (index_entryp->seg_equal_terms
					    [i])));
	}

      /* for each terms associated with the last segment */
      for (t =
	   bitset_iterate (&(index_entryp->seg_equal_terms[nsegs - 1]),
			   &iter); t != -1; t = bitset_next_member (&iter))
	{
	  bitset_add (&range_terms, t);
	  bitset_assign (&kf_terms, &(QO_NODE_SARGS (nodep)));
	  bitset_difference (&kf_terms, &range_terms);
	  if (no_kf_terms)
	    {
	      BITSET_CLEAR (kf_terms);
	    }
	  /* generate index scan plan */
	  planp = qo_index_scan_new (infop, nodep, ni_entryp,
				     &range_terms,
				     &kf_terms, &QO_NODE_SUBQUERIES (nodep));

	  n = qo_check_plan_on_info (infop, planp);
	  if (n)
	    {
	      if (start_column == 0)
		{
		  normal_index_plan_n++;
		}
	    }

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

      bitset_assign (&seg_other_terms,
		     &(index_entryp->seg_other_terms[nsegs - 1]));
      for (t = bitset_iterate (&seg_other_terms, &iter);
	   t != -1; t = bitset_next_member (&iter))
	{
	  term = QO_ENV_TERM (infop->env, t);
	  if (bitset_cardinality (&(index_entryp->seg_other_terms[nsegs - 1]))
	      > 1)
	    {
	      pt_expr = QO_TERM_PT_EXPR (term);

	      /* check for non-null RANGE sarg term only used for index scan
	       * only used for the first segment of the index key
	       */
	      if (pt_expr
		  && PT_EXPR_INFO_IS_FLAGED (pt_expr,
					     PT_EXPR_INFO_FULL_RANGE))
		{
		  continue;	/* skip */
		}
	    }

	  bitset_add (&range_terms, t);
	  bitset_assign (&kf_terms, &(QO_NODE_SARGS (nodep)));
	  bitset_difference (&kf_terms, &range_terms);
	  if (no_kf_terms)
	    {
	      BITSET_CLEAR (kf_terms);
	    }
	  /* generate index scan plan */
	  planp = qo_index_scan_new (infop, nodep, ni_entryp,
				     &range_terms,
				     &kf_terms, &QO_NODE_SUBQUERIES (nodep));

	  n = qo_check_plan_on_info (infop, planp);
	  if (n)
	    {
	      if (start_column == 0)
		{
		  normal_index_plan_n++;
		}
	    }

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
         'find_node_indexes()' */
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
	      if (no_kf_terms)
		{
		  BITSET_CLEAR (kf_terms);
		}
	      /* generate index scan plan */
	      planp = qo_index_scan_new (infop, nodep, ni_entryp,
					 &range_terms,
					 &kf_terms,
					 &QO_NODE_SUBQUERIES (nodep));

	      n = qo_check_plan_on_info (infop, planp);
	      if (n)
		{
		  plan_created = true;
		  if (start_column == 0)
		    {
		      normal_index_plan_n++;
		    }
		}

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

	  bitset_assign (&seg_other_terms,
			 &(index_entryp->seg_other_terms[i]));
	  for (t = bitset_iterate (&seg_other_terms, &iter);
	       t != -1; t = bitset_next_member (&iter))
	    {
	      term = QO_ENV_TERM (infop->env, t);
	      if (bitset_cardinality (&(index_entryp->seg_other_terms[i])) >
		  1)
		{
		  pt_expr = QO_TERM_PT_EXPR (term);

		  /* check for non-null RANGE sarg term only used for index scan
		   * only used for the first segment of the index key
		   */
		  if (pt_expr
		      && PT_EXPR_INFO_IS_FLAGED (pt_expr,
						 PT_EXPR_INFO_FULL_RANGE))
		    {
		      continue;	/* skip */
		    }
		}

	      bitset_add (&range_terms, t);
	      bitset_assign (&kf_terms, &(QO_NODE_SARGS (nodep)));
	      bitset_difference (&kf_terms, &range_terms);
	      if (no_kf_terms)
		{
		  BITSET_CLEAR (kf_terms);
		}
	      /* generate index scan plan */
	      planp = qo_index_scan_new (infop, nodep, ni_entryp,
					 &range_terms,
					 &kf_terms,
					 &QO_NODE_SUBQUERIES (nodep));

	      n = qo_check_plan_on_info (infop, planp);
	      if (n)
		{
		  plan_created = true;
		  if (start_column == 0)
		    {
		      normal_index_plan_n++;
		    }
		}

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

      /* we have only key filter terms and single column index. Use index
       * only if covering, to be sure that we have all segments correct.
       */
      if (!plan_created && index_entryp->cover_segments)
	{
	  /* section added to support key filter terms for order by skipping
	   * also when we do not have covering.
	   * For example (select * from T where col is not null order by col)
	   */
	  if (bitset_cardinality (&(index_entryp->key_filter_terms)))
	    {
	      bitset_assign (&kf_terms, &(index_entryp->key_filter_terms));

	      planp = qo_index_scan_new (infop, nodep, ni_entryp,
					 &range_terms,
					 &kf_terms,
					 &QO_NODE_SUBQUERIES (nodep));

	      n = qo_check_plan_on_info (infop, planp);
	      if (n)
		{
		  plan_created = true;
		  if (start_column == 0)
		    {
		      normal_index_plan_n++;
		    }
		}
	    }
	}
    }				/* if (QO_ENTRY_MULTI_COL(index_entryp)) */

end:
  bitset_delset (&seg_other_terms);
  bitset_delset (&kf_terms);
  bitset_delset (&range_terms);

  return normal_index_plan_n;
}

/*
 * qo_has_is_not_null_term () - Check if whether a given node has sarg term
 *                              with not null operation
 *   return: 1 if it is found, otherwise 0
 *   node(in): pointer to QO_NODE
 */
static int
qo_has_is_not_null_term (QO_NODE * node)
{
  QO_ENV *env;
  QO_TERM *term;
  PT_NODE *expr;
  int i;
  bool found;


  assert (node != NULL && node->env != NULL);
  if (node == NULL || node->env == NULL)
    {
      return 0;
    }

  env = QO_NODE_ENV (node);
  for (i = 0; i < env->nterms; i++)
    {
      term = QO_ENV_TERM (env, i);

      /* term should belong to the given node */
      if (!bitset_intersects (&(QO_TERM_SEGS (term)), &(QO_NODE_SEGS (node))))
	{
	  continue;
	}

      expr = QO_TERM_PT_EXPR (term);
      if (!PT_IS_EXPR_NODE (expr))
	{
	  continue;
	}

      found = false;
      while (expr)
	{
	  if (expr->info.expr.op == PT_IS_NOT_NULL)
	    {
	      found = true;
	    }
	  else if (expr->info.expr.op == PT_IS_NULL)
	    {
	      found = false;
	      break;
	    }

	  expr = expr->or_next;
	}

      /* return if one of sarg term has not null operation */
      if (found)
	{
	  return 1;
	}
    }
  return 0;
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
  QO_PLAN *plan, *index_plan;
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
  int have_range_terms = 0;
  int normal_index_plan_n;

  bitset_init (&nodes, planner->env);
  bitset_init (&subqueries, planner->env);
  bitset_init (&remaining_subqueries, planner->env);

  planner->worst_plan = qo_worst_new (planner->env);
  if (planner->worst_plan == NULL)
    {
      plan = NULL;
      goto end;
    }

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
	  planner->join_info = (QO_INFO **) malloc (join_info_bytes);
	  if (planner->join_info == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_OUT_OF_VIRTUAL_MEMORY, 1, join_info_bytes);
	      plan = NULL;
	      goto end;
	    }
	}
      else
	{
	  plan = NULL;
	  goto end;
	}

      memset (planner->join_info, 0, join_info_bytes);
    }

  bitset_assign (&remaining_subqueries, &(planner->all_subqueries));

  /*
   * Add appropriate scan plans for each node.
   */
  planner->node_info = NULL;
  if (planner->N > 0)
    {
      size_t size = sizeof (QO_INFO *) * planner->N;

      planner->node_info = (QO_INFO **) malloc (size);
      if (planner->node_info == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_OUT_OF_VIRTUAL_MEMORY, 1, size);
	  plan = NULL;
	  goto end;
	}
    }

  for (i = 0; i < (signed) planner->N; ++i)
    {
      node = &planner->node[i];
      BITSET_CLEAR (nodes);
      bitset_add (&nodes, i);
      planner->node_info[i] =
	qo_alloc_info (planner,
		       &nodes,
		       &QO_NODE_SARGS (node),
		       &QO_NODE_EQCLASSES (node),
		       QO_NODE_SELECTIVITY (node) *
		       (double) QO_NODE_NCARD (node));

      if (planner->node_info[i] == NULL)
	{
	  plan = NULL;
	  goto end;
	}

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
    }

  /*
   * Check all of the terms to determine which are eligible to serve as
   * index scans.
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
      normal_index_plan_n = 0;

      if (node_index != NULL)
	{
	  bitset_init (&seg_terms, planner->env);

	  for (j = 0; j < QO_NI_N (node_index); j++)
	    {
	      /* If the index is a candidate for index skip scan, then it will
	       * not have any terms for seg_eqal or seg_other[0], so we should
	       * skip that first column from initial checks. Set the start column
	       * to 1.
	       */
	      int start_column;

	      ni_entry = QO_NI_ENTRY (node_index, j);
	      index_entry = (ni_entry)->head;
	      start_column = index_entry->is_iss_candidate ? 1 : 0;

	      /* seg_terms will contain all the indexable terms that refer
	       * segments from this node; stops at the first one that has
	       * no equals or other terms */
	      BITSET_CLEAR (seg_terms);
	      for (k = start_column; k < index_entry->nsegs; k++)
		{
		  if (bitset_is_empty (&(index_entry->seg_equal_terms[k]))
		      && bitset_is_empty (&(index_entry->seg_other_terms[k])))
		    {
		      break;
		    }
		  bitset_union (&seg_terms,
				&(index_entry->seg_equal_terms[k]));
		  bitset_union (&seg_terms,
				&(index_entry->seg_other_terms[k]));
		}

	      index_plan = NULL;
	      have_range_terms = bitset_intersects (&(QO_NODE_SARGS (node)),
						    &seg_terms);
#if 1
	      /* Currently we do not consider the following optimization. */
	      if (have_range_terms)
#else
	      /* We can generate a plan using index scan if terms are covered,
	       * even though no range term is found. In this case, however,
	       * one of the following conditions should be satisfied.
	       *  1) the index has at least one attribute which has not null constraint
	       *  2) one of sarg term belonging to the node has not null operation
	       * If not, we cannot do index full scan, because we cannot guarantee
	       * that result of index scan is same with sequential scan's.
	       */
	      if (have_range_terms ||
		  (bitset_cardinality (&(index_entry->key_filter_terms)) > 0
		   && index_entry->cover_segments
		   && (sm_has_non_null_attribute (index_entry->
						  constraints->
						  attributes)
		       || qo_has_is_not_null_term (node))))
#endif
		{
		  normal_index_plan_n =
		    qo_generate_index_scan (info, node, ni_entry);
		}

	      /* if the index didn't normally skipped the order by, we try
	       * the new plan, maybe this will be better.
	       * DO NOT generate a order by index if there is no order by!
	       */
	      if (!ni_entry->head->orderby_skip &&
		  QO_ENV_PT_TREE (info->env) &&
		  QO_ENV_PT_TREE (info->env)->info.query.order_by &&
		  !QO_ENV_PT_TREE (info->env)->info.query.q.select.connect_by
		  && !qo_is_prefix_index (ni_entry->head))
		{
		  qo_generate_index_scan_from_orderby (info, node, ni_entry);
		}

	      /* if the index didn't normally skipped the group by, we try
	       * the new plan, maybe this will be better.
	       * DO NOT generate a group by index if there is no group by!
	       */
	      if (!ni_entry->head->groupby_skip &&
		  QO_ENV_PT_TREE (info->env) &&
		  QO_ENV_PT_TREE (info->env)->info.query.q.select.group_by &&
		  !qo_is_prefix_index (ni_entry->head))
		{
		  qo_generate_index_scan_from_groupby (info, node, ni_entry);
		}
	    }

	  bitset_delset (&seg_terms);
	}

      /*
       * Create a sequential scan plan for each node.
       */
      if (normal_index_plan_n > 0 && QO_NODE_TCARD (node) <= 1)
	{
	  /* Already generate some index scans for one-page heap.
	   * Skip sequential scan plan for the node.
	   */
	  ;			/* nop */
	}
      else
	{
	  qo_generate_seq_scan (info, node);
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

	  if (plan->plan_type == QO_PLANTYPE_SCAN
	      && plan->plan_un.scan.scan_method == QO_SCANMETHOD_SEQ_SCAN)
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
      size_t size = sizeof (QO_INFO *) * planner->P;

      planner->cp_info = (QO_INFO **) malloc (size);
      if (planner->cp_info == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_OUT_OF_VIRTUAL_MEMORY, 1, size);
	  plan = NULL;
	  goto end;
	}

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

  /* if we have use_desc_idx hint and order by or group by, do some checking */
  if (plan)
    {
      bool has_hint;
      PT_HINT_ENUM *hint;
      PT_NODE *node = NULL;

      hint = &(QO_ENV_PT_TREE (planner->env)->info.query.q.select.hint);
      has_hint = (*hint & PT_HINT_USE_IDX_DESC) > 0;

      /* check direction of the first order by column. */
      node = QO_ENV_PT_TREE (planner->env)->info.query.order_by;
      if (node != NULL)
	{
	  if (QO_ENV_PT_TREE (planner->env)->info.query.q.select.connect_by)
	    {
	      ;
	    }
	  /* if we have order by and the hint, we allow the hint only if we
	   * have order by descending on first column. Otherwise we clear it
	   */
	  else if (has_hint && node->info.sort_spec.asc_or_desc == PT_ASC)
	    {
	      *hint &= ~PT_HINT_USE_IDX_DESC;
	    }
	}

      /* check direction of the first order by column. */
      node = QO_ENV_PT_TREE (planner->env)->info.query.q.select.group_by;
      if (node != NULL)
	{
	  if (node->with_rollup);
	  /* if we have group by and the hint, we allow the hint only if we
	   * have group by descending on first column. Otherwise we clear it
	   */
	  else if (has_hint && node->info.sort_spec.asc_or_desc == PT_ASC)
	    {
	      *hint &= ~PT_HINT_USE_IDX_DESC;
	    }
	}
    }

  if (plan && plan->plan_type == QO_PLANTYPE_SCAN &&
      plan->plan_un.scan.scan_method != QO_SCANMETHOD_SEQ_SCAN)
    {
      if (plan->plan_un.scan.index && plan->plan_un.scan.index->head)
	{
	  if (plan->plan_un.scan.index->head->use_descending)
	    {
	      /* We no longer need to set the USE_DESC_IDX hint if the planner
	       * wants a descending index, because the requirement is copied
	       * to each scan_ptr's index info at XASL generation.
	       *
	       * plan->info->env->pt_tree->info.query.q.select.hint |=
	       *   PT_HINT_USE_IDX_DESC;
	       */
	    }
	  else if (plan->plan_un.scan.index->head->orderby_skip)
	    {
	      plan->info->env->pt_tree->info.query.q.select.hint &=
		~PT_HINT_USE_IDX_DESC;
	    }
	}
    }

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

		      if (i_plan->plan_type == QO_PLANTYPE_SCAN
			  && (i_plan->plan_un.scan.scan_method ==
			      QO_SCANMETHOD_INDEX_SCAN))
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

		      if (j_plan->plan_type == QO_PLANTYPE_SCAN
			  && (j_plan->plan_un.scan.scan_method ==
			      QO_SCANMETHOD_INDEX_SCAN))
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

		      if (BITSET_MEMBER (QO_TERM_NODES (term),
					 QO_NODE_IDX (j_node))
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
				}
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
		    }

		  if (found_edge)
		    {
		      /* delete j_plan from first_node */
		      bitset_remove (&first_nodes, j);
		    }
		}
	    }

	  if (j == -1)
	    {			/* add new plan */
	      bitset_add (&first_nodes, i);
	    }
	}

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
	    planner->join_info[QO_INFO_INDEX
			       (QO_PARTITION_M_OFFSET (partition),
				visited_rel_nodes)];
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

#if defined (CUBRID_DEBUG)
  if (planner->env->dump_enable)
    {
      qo_dump_planner_info (planner, partition, stdout);
    }
#endif

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

  for (i = 0; i < (signed) planner->P; ++i)
    {
      (QO_PARTITION_PLAN (&planner->partition[i]))->refcount--;
    }

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
	  if (!bitset_is_empty (&(QO_TERM_NODES (&planner->term[t])))
	      && !BITSET_MEMBER (terms, t)
	      && bitset_subset (&nodes, &(QO_TERM_NODES (&planner->term[t])))
	      && (QO_TERM_CLASS (&planner->term[t]) !=
		  QO_TC_TOTALLY_AFTER_JOIN))
	    {
	      bitset_add (&sarged_terms, t);
	    }
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

/*
 * qo_expr_selectivity () -
 *   return: double
 *   env(in): optimizer environment
 *   pt_expr(in): expression to evaluate
 */
double
qo_expr_selectivity (QO_ENV * env, PT_NODE * pt_expr)
{
  double lhs_selectivity, rhs_selectivity, selectivity =
    0.0, total_selectivity;
  PT_NODE *node;

  QO_ASSERT (env, pt_expr != NULL && pt_expr->node_type == PT_EXPR);

  total_selectivity = 0.0;

  /* traverse OR list */
  for (node = pt_expr; node; node = node->or_next)
    {

      switch (node->info.expr.op)
	{

	case PT_OR:
	  lhs_selectivity = qo_expr_selectivity (env, node->info.expr.arg1);
	  rhs_selectivity = qo_expr_selectivity (env, node->info.expr.arg2);
	  selectivity = qo_or_selectivity (env, lhs_selectivity,
					   rhs_selectivity);
	  break;

	case PT_AND:
	  lhs_selectivity = qo_expr_selectivity (env, node->info.expr.arg1);
	  rhs_selectivity = qo_expr_selectivity (env, node->info.expr.arg2);
	  selectivity = qo_and_selectivity (env, lhs_selectivity,
					    rhs_selectivity);
	  break;

	case PT_NOT:
	  lhs_selectivity = qo_expr_selectivity (env, node->info.expr.arg1);
	  selectivity = qo_not_selectivity (env, lhs_selectivity);
	  break;

	case PT_EQ:
	  selectivity = qo_equal_selectivity (env, node);
	  break;

	case PT_NE:
	  lhs_selectivity = qo_equal_selectivity (env, node);
	  selectivity = qo_not_selectivity (env, lhs_selectivity);
	  break;

	case PT_NULLSAFE_EQ:
	  selectivity = qo_equal_selectivity (env, node);
	  break;

	case PT_GE:
	case PT_GT:
	case PT_LT:
	case PT_LE:
	  selectivity = qo_comp_selectivity (env, node);
	  break;

	case PT_BETWEEN:
	  selectivity = qo_between_selectivity (env, node);
	  break;

	case PT_NOT_BETWEEN:
	  lhs_selectivity = qo_between_selectivity (env, node);
	  selectivity = qo_not_selectivity (env, lhs_selectivity);
	  break;

	case PT_RANGE:
	  selectivity = qo_range_selectivity (env, node);
	  break;

	case PT_LIKE_ESCAPE:
	case PT_LIKE:
	  selectivity = (double) PRM_LIKE_TERM_SELECTIVITY;
	  break;
	case PT_SETNEQ:
	case PT_SETEQ:
	case PT_SUPERSETEQ:
	case PT_SUPERSET:
	case PT_SUBSET:
	case PT_SUBSETEQ:
	case PT_IS:
	case PT_XOR:
	  selectivity = DEFAULT_SELECTIVITY;
	  break;

	case PT_NOT_LIKE:
	case PT_IS_NOT:
	  selectivity = qo_not_selectivity (env, DEFAULT_SELECTIVITY);
	  break;

	case PT_EQ_SOME:
	case PT_NE_SOME:
	case PT_GE_SOME:
	case PT_GT_SOME:
	case PT_LT_SOME:
	case PT_LE_SOME:
	case PT_EQ_ALL:
	case PT_NE_ALL:
	case PT_GE_ALL:
	case PT_GT_ALL:
	case PT_LT_ALL:
	case PT_LE_ALL:
	case PT_IS_IN:
	  selectivity = qo_all_some_in_selectivity (env, node);
	  break;

	case PT_IS_NOT_IN:
	  lhs_selectivity = qo_all_some_in_selectivity (env, node);
	  selectivity = qo_not_selectivity (env, lhs_selectivity);
	  break;

	case PT_IS_NULL:
	  selectivity = DEFAULT_NULL_SELECTIVITY;	/* make a guess */
	  break;

	case PT_IS_NOT_NULL:
	  selectivity = qo_not_selectivity (env, DEFAULT_NULL_SELECTIVITY);
	  break;

	case PT_EXISTS:
	  selectivity = DEFAULT_EXISTS_SELECTIVITY;	/* make a guess */
	  break;

	default:
	  break;
	}

      total_selectivity = qo_or_selectivity (env, total_selectivity,
					     selectivity);
      total_selectivity = MAX (total_selectivity, 0.0);
      total_selectivity = MIN (total_selectivity, 1.0);
    }

  return total_selectivity;
}

/*
 * qo_or_selectivity () - Calculate the selectivity of an OR expression
 *                        from the selectivities of the lhs and rhs
 *   return: double
 *   env(in):
 *   lhs_sel(in):
 *   rhs_sel(in):
 */
static double
qo_or_selectivity (QO_ENV * env, double lhs_sel, double rhs_sel)
{
  double result;
  QO_ASSERT (env, lhs_sel >= 0.0 && lhs_sel <= 1.0);
  QO_ASSERT (env, rhs_sel >= 0.0 && rhs_sel <= 1.0);
  result = lhs_sel + rhs_sel - (lhs_sel * rhs_sel);
  return result;
}

/*
 * qo_and_selectivity () -
 *   return:
 *   env(in):
 *   lhs_sel(in):
 *   rhs_sel(in):
 */
static double
qo_and_selectivity (QO_ENV * env, double lhs_sel, double rhs_sel)
{
  double result;
  QO_ASSERT (env, lhs_sel >= 0.0 && lhs_sel <= 1.0);
  QO_ASSERT (env, rhs_sel >= 0.0 && rhs_sel <= 1.0);
  result = lhs_sel * rhs_sel;
  return result;
}

/*
 * qo_not_selectivity () - Calculate the selectivity of a not expresssion
 *   return: double
 *   env(in):
 *   sel(in):
 */
static double
qo_not_selectivity (QO_ENV * env, double sel)
{
  QO_ASSERT (env, sel >= 0.0 && sel <= 1.0);
  return 1.0 - sel;
}

/*
 * qo_equal_selectivity () - Compute the selectivity of an equality predicate
 *   return: double
 *   env(in):
 *   pt_expr(in):
 *
 * Note: This uses the System R algorithm
 */
static double
qo_equal_selectivity (QO_ENV * env, PT_NODE * pt_expr)
{
  PT_NODE *lhs, *rhs;
  PRED_CLASS pc_lhs, pc_rhs;
  int lhs_icard, rhs_icard, icard;
  double selectivity,
    lhs_high_value, lhs_low_value, rhs_high_value, rhs_low_value, const_val;
  int rc1, rc2;

  lhs = pt_expr->info.expr.arg1;
  rhs = pt_expr->info.expr.arg2;

  /* the class of lhs and rhs */
  pc_lhs = qo_classify (lhs);
  pc_rhs = qo_classify (rhs);

  selectivity = DEFAULT_EQUAL_SELECTIVITY;

  switch (pc_lhs)
    {
    case PC_ATTR:

      switch (pc_rhs)
	{
	case PC_ATTR:
	  /* attr = attr */

	  /* check for indexes on either of the attributes */
	  lhs_icard = qo_index_cardinality (env, lhs);
	  rhs_icard = qo_index_cardinality (env, rhs);

	  if ((icard = MAX (lhs_icard, rhs_icard)) != 0)
	    selectivity = (1.0 / icard);
	  else
	    selectivity = DEFAULT_EQUIJOIN_SELECTIVITY;

	  /* special case */
	  if (qo_is_arithmetic_type (lhs) && qo_is_arithmetic_type (rhs))
	    {
	      /* get the range and data value coerced into doubles */
	      rc1 = qo_get_range (env, lhs, &lhs_low_value, &lhs_high_value);
	      rc2 = qo_get_range (env, rhs, &rhs_low_value, &rhs_high_value);
	      if (rc1 || rc2)
		{
		  if ((lhs_icard == 0 && rhs_low_value == rhs_high_value) ||
		      (rhs_icard == 0 && lhs_low_value == lhs_high_value))
		    selectivity = 1.0;
		  if (lhs_low_value > rhs_high_value ||
		      rhs_low_value > lhs_high_value)
		    selectivity = 0.0;
		}
	    }

	  break;

	case PC_CONST:
	case PC_HOST_VAR:
	case PC_SUBQUERY:
	case PC_SET:
	case PC_OTHER:
	  /* attr = const */

	  /* check for index on the attribute.  NOTE: For an equality
	     predicate, we treat subqueries as constants. */
	  lhs_icard = qo_index_cardinality (env, lhs);
	  if (lhs_icard != 0)
	    selectivity = (1.0 / lhs_icard);
	  else
	    selectivity = DEFAULT_EQUAL_SELECTIVITY;

	  /* special case */
	  if (pc_rhs == PC_CONST && qo_is_arithmetic_type (lhs))
	    {
	      /* get the range and data value coerced into doubles */
	      rc1 = qo_get_range (env, lhs, &lhs_low_value, &lhs_high_value);
	      if (rc1)
		{
		  /* get the constant values */
		  const_val = get_const_value (env, rhs);
		  if (const_val == lhs_low_value
		      && lhs_low_value == lhs_high_value)
		    {
		      selectivity = 1.0;
		    }
		  else if (const_val < lhs_low_value
			   || const_val > lhs_high_value)
		    {
		      selectivity = 0.0;
		    }
		  else if (lhs->type_enum == PT_TYPE_INTEGER
			   && lhs_icard == 0)
		    {		/* still default-selectivity */
		      double diff;

		      diff = lhs_high_value - lhs_low_value + 1.0;
		      if ((int) diff > 0)
			selectivity = 1.0 / diff;
		    }
		}
	    }

	  break;
	}

      break;

    case PC_CONST:
    case PC_HOST_VAR:
    case PC_SUBQUERY:
    case PC_SET:
    case PC_OTHER:

      switch (pc_rhs)
	{
	case PC_ATTR:
	  /* const = attr */

	  /* check for index on the attribute.  NOTE: For an equality
	     predicate, we treat subqueries as constants. */
	  rhs_icard = qo_index_cardinality (env, rhs);
	  if (rhs_icard != 0)
	    selectivity = (1.0 / rhs_icard);
	  else
	    selectivity = DEFAULT_EQUAL_SELECTIVITY;

	  /* special case */
	  if (pc_lhs == PC_CONST && qo_is_arithmetic_type (rhs))
	    {
	      /* get the range and data value coerced into doubles */
	      rc2 = qo_get_range (env, rhs, &rhs_low_value, &rhs_high_value);
	      if (rc2)
		{
		  /* get the constant values */
		  const_val = get_const_value (env, lhs);
		  if (const_val == rhs_low_value
		      && rhs_low_value == rhs_high_value)
		    {
		      selectivity = 1.0;
		    }
		  else if (const_val < rhs_low_value
			   || const_val > rhs_high_value)
		    {
		      selectivity = 0.0;
		    }
		  else if (rhs->type_enum == PT_TYPE_INTEGER
			   && rhs_icard == 0)
		    {		/* still default-selectivity */
		      double diff;

		      diff = rhs_high_value - rhs_low_value + 1.0;
		      if ((int) diff > 0)
			selectivity = 1.0 / diff;
		    }
		}
	    }

	  break;

	case PC_CONST:
	case PC_HOST_VAR:
	case PC_SUBQUERY:
	case PC_SET:
	case PC_OTHER:
	  /* const = const */

	  selectivity = DEFAULT_EQUAL_SELECTIVITY;

	  break;
	}

      break;
    }

  return selectivity;
}

/*
 * qo_comp_selectivity () - Compute the selectivity of a comparison predicate.
 *   return: double
 *   env(in): Pointer to an environment structure
 *   pt_expr(in): comparison expression
 *
 * Note: This uses the System R algorithm
 */
static double
qo_comp_selectivity (QO_ENV * env, PT_NODE * pt_expr)
{
  PRED_CLASS pc_lhs, pc_rhs;
  double low_value = 0.0, high_value = 0.0, const_value = 0.0;
  double comp_sel;
  int rc;

  /* determine the class of each side of the comparison */
  pc_lhs = qo_classify (pt_expr->info.expr.arg1);
  pc_rhs = qo_classify (pt_expr->info.expr.arg2);

  /* The only interesting cases are when one side is an attribute and the
   * other is a constant.
   */
  if ((pc_lhs == PC_ATTR && pc_rhs == PC_CONST) ||
      (pc_rhs == PC_ATTR && pc_lhs == PC_CONST))
    {

      /* bail out if the datatype is not arithmetic */
      if ((pc_lhs == PC_ATTR
	   && !qo_is_arithmetic_type (pt_expr->info.expr.arg2))
	  || (pc_rhs == PC_ATTR
	      && !qo_is_arithmetic_type (pt_expr->info.expr.arg1)))
	return DEFAULT_COMP_SELECTIVITY;

      /* get high and low values for the class of the attribute. */
      if (pc_lhs == PC_ATTR)
	rc = qo_get_range (env, pt_expr->info.expr.arg1,
			   &low_value, &high_value);
      else
	rc = qo_get_range (env, pt_expr->info.expr.arg2,
			   &low_value, &high_value);

      if (!rc)
	{
	  /* bail out if fails to get range */
	  return DEFAULT_COMP_SELECTIVITY;
	}

      /* bail out if they are equal by chance */
      if (low_value == high_value)
	return DEFAULT_COMP_SELECTIVITY;

      /* get the constant value */
      if (pc_lhs == PC_CONST)
	const_value = get_const_value (env, pt_expr->info.expr.arg1);
      else
	const_value = get_const_value (env, pt_expr->info.expr.arg2);

      /* finally, interpolate selectivity, based on the operator
       * NOTE: if the interpolation yields a negative result, the user
       * has asked for a value outside the range and the selectivity is 0.0
       */
      switch (pt_expr->info.expr.op)
	{
	case PT_GE:
	  comp_sel = (high_value - const_value + 1.0) /
	    (high_value - low_value + 1.0);
	  break;
	case PT_GT:
	  comp_sel = (high_value - const_value) /
	    (high_value - low_value + 1.0);
	  break;
	case PT_LT:
	  comp_sel = (const_value - low_value) /
	    (high_value - low_value + 1.0);
	  break;
	case PT_LE:
	  comp_sel = (const_value - low_value + 1.0) /
	    (high_value - low_value + 1.0);
	  break;
	default:
	  /* can't get here, but so the compiler doesn't whine... */
	  return DEFAULT_COMP_SELECTIVITY;
	}
      return comp_sel < 0.0 ? 0.0 : comp_sel > 1.0 ? 1.0 : comp_sel;
    }

  return DEFAULT_COMP_SELECTIVITY;
}

/*
 * qo_between_selectivity () - Compute the selectivity of a between predicate
 *   return: double
 *   env(in): Pointer to an environment structure
 *   pt_expr(in): between expression
 *
 * Note: This uses the System R algorithm
 */
static double
qo_between_selectivity (QO_ENV * env, PT_NODE * pt_expr)
{
  PRED_CLASS pc1, pc2, pc3;
  double low_value = 0.0, high_value = 0.0, lhs_const_val =
    0.0, rhs_const_val = 0.0;
  PT_NODE *and_node = pt_expr->info.expr.arg2;
  int rc;

  QO_ASSERT (env, and_node->node_type == PT_EXPR);
  QO_ASSERT (env, pt_is_between_range_op (and_node->info.expr.op));

  /* determine the classes of the operands */
  pc1 = qo_classify (pt_expr->info.expr.arg1);
  pc2 = qo_classify (and_node->info.expr.arg1);
  pc3 = qo_classify (and_node->info.expr.arg2);

  /* The only interesting case is: attr BETWEEN const1 AND const2 */
  if (pc1 == PC_ATTR && pc2 == PC_CONST && pc3 == PC_CONST)
    {

      /* bail out if the datatypes are not arithmetic */
      if (!qo_is_arithmetic_type (and_node->info.expr.arg1) ||
	  !qo_is_arithmetic_type (and_node->info.expr.arg2))
	return DEFAULT_BETWEEN_SELECTIVITY;

      /* get the range and data value coerced into doubles */
      rc =
	qo_get_range (env, pt_expr->info.expr.arg1, &low_value, &high_value);

      if (!rc)
	{
	  /* bail out if fails to get range */
	  return DEFAULT_BETWEEN_SELECTIVITY;
	}

      /* bail out if they are equal by chance */
      if (low_value == high_value)
	return DEFAULT_BETWEEN_SELECTIVITY;

      /* get the constant values */
      lhs_const_val = get_const_value (env, and_node->info.expr.arg1);
      rhs_const_val = get_const_value (env, and_node->info.expr.arg2);

      /* choose the class's bounds if it restricts the range */
      if (rhs_const_val > high_value)
	rhs_const_val = high_value;
      if (lhs_const_val < low_value)
	lhs_const_val = low_value;

      /* Check if the range is trivially empty */
      if (lhs_const_val > rhs_const_val)
	return 0.0;

      /* finally, calculate the selectivity */
      return (rhs_const_val - lhs_const_val + 1.0) /
	(high_value - low_value + 1.0);
    }

  return DEFAULT_BETWEEN_SELECTIVITY;
}

/*
 * qo_range_selectivity () -
 *   return:
 *   env(in):
 *   pt_expr(in):
 */
static double
qo_range_selectivity (QO_ENV * env, PT_NODE * pt_expr)
{
  PT_NODE *lhs, *arg1, *arg2;
  PRED_CLASS pc1, pc2;
  double total_selectivity, selectivity,
    high_value1, low_value1, high_value2, low_value2, const1, const2;
  int lhs_icard, rhs_icard, icard;
  PT_NODE *range_node;
  PT_OP_TYPE op_type;
  int rc1, rc2;

  lhs = pt_expr->info.expr.arg1;

  /* the only interesting case is 'attr RANGE {...}' */
  if (qo_classify (lhs) != PC_ATTR)
    return DEFAULT_RANGE_SELECTIVITY;

  /* check for non-null RANGE sarg term only used for index scan;
   * 'attr RANGE ( Min ge_inf )'
   */
  if (PT_EXPR_INFO_IS_FLAGED (pt_expr, PT_EXPR_INFO_FULL_RANGE))
    {
      return 1.0;
    }

  /* get index cardinality */
  lhs_icard = qo_index_cardinality (env, lhs);

  total_selectivity = 0.0;

  for (range_node = pt_expr->info.expr.arg2; range_node;
       range_node = range_node->or_next)
    {
      QO_ASSERT (env, range_node->node_type == PT_EXPR);
      QO_ASSERT (env,
		 pt_is_between_range_op (op_type = range_node->info.expr.op));

      arg1 = range_node->info.expr.arg1;
      arg2 = range_node->info.expr.arg2;

      pc1 = qo_classify (arg1);

      if (op_type == PT_BETWEEN_GE_LE || op_type == PT_BETWEEN_GE_LT
	  || op_type == PT_BETWEEN_GT_LE || op_type == PT_BETWEEN_GT_LT)
	{
	  selectivity = DEFAULT_BETWEEN_SELECTIVITY;

	  pc2 = qo_classify (arg2);

	  if (pc1 == PC_CONST && pc2 == PC_CONST
	      /* bail out if the datatypes are not arithmetic */
	      && qo_is_arithmetic_type (arg1) && qo_is_arithmetic_type (arg2))
	    {

	      /* get the range and data value coerced into doubles */
	      rc1 = qo_get_range (env, lhs, &low_value1, &high_value1);

	      /* get the constant values */
	      const1 = get_const_value (env, arg1);
	      const2 = get_const_value (env, arg2);

	      if (const1 == const2)
		{
		  /* same as equal selectivity */
		  if (lhs_icard != 0)
		    {
		      selectivity = (1.0 / lhs_icard);
		    }
		  else
		    {
		      selectivity = DEFAULT_EQUAL_SELECTIVITY;
		    }

		  /* special case */
		  if (rc1)
		    {
		      if (low_value1 == high_value1)
			{
			  selectivity = 1.0;
			}
		      if (const1 < low_value1 || const1 > high_value1)
			{
			  selectivity = 0.0;
			}
		    }
		}
	      else if (const1 < const2)
		{
		  /* same as between selectivity */
		  /* choose the class's bounds if it restricts the range */
		  if (rc1)
		    {
		      if (const1 < low_value1)
			{
			  const1 = low_value1;
			}
		      if (const2 > high_value1)
			{
			  const2 = high_value1;
			}

		      switch (op_type)
			{
			case PT_BETWEEN_GE_LE:
			  selectivity = ((const2 - const1 + 1.0) /
					 (high_value1 - low_value1 + 1.0));
			  break;
			case PT_BETWEEN_GE_LT:
			case PT_BETWEEN_GT_LE:
			  selectivity = ((const2 - const1) /
					 (high_value1 - low_value1 + 1.0));
			  break;
			case PT_BETWEEN_GT_LT:
			  selectivity = ((const2 - const1 - 1.0) /
					 (high_value1 - low_value1 + 1.0));
			  break;
			default:
			  break;
			}
		    }
		}
	      else
		{
		  /* low value > high value; trivially empty */
		  selectivity = 0.0;
		}
	    }
	}
      else if (op_type == PT_BETWEEN_EQ_NA)
	{
	  /* PT_BETWEEN_EQ_NA have only one argument */

	  selectivity = DEFAULT_EQUAL_SELECTIVITY;

	  if (pc1 == PC_ATTR)
	    {
	      /* attr1 range (attr2 = ) */
	      rhs_icard = qo_index_cardinality (env, arg1);

	      if ((icard = MAX (lhs_icard, rhs_icard)) != 0)
		{
		  selectivity = (1.0 / icard);
		}
	      else
		{
		  selectivity = DEFAULT_EQUIJOIN_SELECTIVITY;
		}

	      /* special case */
	      if (qo_is_arithmetic_type (lhs) && qo_is_arithmetic_type (arg1))
		{
		  /* get the range and data value coerced into doubles */
		  rc1 = qo_get_range (env, lhs, &low_value1, &high_value1);
		  rc2 = qo_get_range (env, arg1, &low_value2, &high_value2);
		  if (rc1 || rc2)
		    {
		      if ((lhs_icard == 0 && low_value1 == high_value1)
			  || (rhs_icard == 0 && low_value2 == high_value2))
			{
			  selectivity = 1.0;
			}
		      if (low_value1 > high_value2
			  || low_value2 > high_value1)
			{
			  selectivity = 0.0;
			}
		    }
		}
	    }
	  else
	    {
	      /* attr1 range (const2 = ) */
	      if (lhs_icard != 0)
		{
		  selectivity = (1.0 / lhs_icard);
		}
	      else
		{
		  selectivity = DEFAULT_EQUAL_SELECTIVITY;
		}

	      /* special case */
	      if (pc1 == PC_CONST && qo_is_arithmetic_type (lhs))
		{
		  if (qo_is_arithmetic_type (arg1))
		    {
		      /* get the range and data value coerced into doubles */
		      rc1 = qo_get_range (env, lhs, &low_value1,
					  &high_value1);
		      if (rc1)
			{
			  /* get the constant values */
			  const1 = get_const_value (env, arg1);
			  if (const1 == low_value1
			      && low_value1 == high_value1)
			    {
			      selectivity = 1.0;
			    }
			  if (const1 < low_value1 || const1 > high_value1)
			    {
			      selectivity = 0.0;
			    }
			}
		    }
		  else
		    {
		      /* evaluate on different data type
		       * ex) SELECT ... FROM ... WHERE i in (1, 'x');
		       *     ---> now, evaluate i on 'x'
		       */
		      selectivity = 0.0;
		    }
		}
	    }
	}
      else
	{
	  /* PT_BETWEEN_INF_LE, PT_BETWEEN_INF_LT, PT_BETWEEN_GE_INF, and
	     PT_BETWEEN_GT_INF have only one argument */

	  selectivity = DEFAULT_COMP_SELECTIVITY;

	  /* in the case of
	     'attr RANGE {INF_LE(INF_LT, GE_INF, GT_INF) const, ...}' */
	  if (pc1 == PC_CONST
	      /* bail out if the datatype is not arithmetic */
	      && qo_is_arithmetic_type (arg1))
	    {
	      /* get the range and data value coerced into doubles */
	      rc1 = qo_get_range (env, lhs, &low_value1, &high_value1);
	      if (rc1)
		{
		  /* get the constant value */
		  const1 = get_const_value (env, arg1);

		  switch (op_type)
		    {
		    case PT_BETWEEN_GE_INF:
		      selectivity = ((high_value1 - const1 + 1.0) /
				     (high_value1 - low_value1 + 1.0));
		      break;
		    case PT_BETWEEN_GT_INF:
		      selectivity = ((high_value1 - const1) /
				     (high_value1 - low_value1 + 1.0));
		      break;
		    case PT_BETWEEN_INF_LE:
		      selectivity = ((const1 - low_value1 + 1.0) /
				     (high_value1 - low_value1 + 1.0));
		      break;
		    case PT_BETWEEN_INF_LT:
		      selectivity = ((const1 - low_value1) /
				     (high_value1 - low_value1 + 1.0));
		      break;
		    default:
		      break;
		    }
		}
	    }
	}

      selectivity = MAX (selectivity, 0.0);
      selectivity = MIN (selectivity, 1.0);

      total_selectivity = qo_or_selectivity (env, total_selectivity,
					     selectivity);
      total_selectivity = MAX (total_selectivity, 0.0);
      total_selectivity = MIN (total_selectivity, 1.0);
    }

  return total_selectivity;
}

/*
 * qo_all_some_in_selectivity () - Compute the selectivity of an in predicate
 *   return: double
 *   env(in): Pointer to an environment structure
 *   pt_expr(in): in expression
 *
 * Note: This uses the System R algorithm
 */
static double
qo_all_some_in_selectivity (QO_ENV * env, PT_NODE * pt_expr)
{
  PRED_CLASS pc_lhs, pc_rhs;
  int list_card = 0, icard;
  double equal_selectivity, in_selectivity;

  /* determine the class of each side of the range */
  pc_lhs = qo_classify (pt_expr->info.expr.arg1);
  pc_rhs = qo_classify (pt_expr->info.expr.arg2);

  /* The only interesting cases are: attr IN set or attr IN subquery */
  if (pc_lhs == PC_ATTR && (pc_rhs == PC_SET || pc_rhs == PC_SUBQUERY))
    {
      /* check for index on the attribute.  */
      icard = qo_index_cardinality (env, pt_expr->info.expr.arg1);

      if (icard != 0)
	equal_selectivity = (1.0 / icard);
      else
	equal_selectivity = DEFAULT_EQUAL_SELECTIVITY;

      /* determine cardinality of set or subquery */
      if (pc_rhs == PC_SET)
	{
	  list_card =
	    pt_length_of_list (pt_expr->info.expr.arg2->info.value.
			       data_value.set);
	}
      if (pc_rhs == PC_SUBQUERY)
	{
#if 0
/*right now we don't have the hook for the cardinality of subqueries, just use
 * a large number so that the selectivity will end up being capped at 0.5
 */
	  list_card = pt_expr->info.select.est_card;
#else
	  list_card = 1000;
#endif /* 0 */

	}

      /* compute selectivity--cap at 0.5 */
      in_selectivity = list_card * equal_selectivity;
      return in_selectivity > 0.5 ? 0.5 : in_selectivity;
    }

  return DEFAULT_IN_SELECTIVITY;
}

/*
 * qo_classify () - Determine which predicate class the node belongs in
 *   return: PRED_CLASS
 *   attr(in): pt node to classify
 */
static PRED_CLASS
qo_classify (PT_NODE * attr)
{
  switch (attr->node_type)
    {
    case PT_NAME:
    case PT_DOT_:
      return PC_ATTR;

    case PT_VALUE:
      if ((attr->type_enum == PT_TYPE_SET) ||
	  (attr->type_enum == PT_TYPE_MULTISET) ||
	  (attr->type_enum == PT_TYPE_SEQUENCE))
	return PC_SET;
      else if (attr->type_enum == PT_TYPE_NULL)
	return PC_OTHER;
      else
	return PC_CONST;

    case PT_HOST_VAR:
      return PC_HOST_VAR;

    case PT_SELECT:
    case PT_UNION:
    case PT_INTERSECTION:
    case PT_DIFFERENCE:
      return PC_SUBQUERY;

    default:
      return PC_OTHER;
    }
}

/*
 * qo_is_arithmetic_type () - Check whether the attribute has an arithmetic type
 *   return: bool
 *   attr(in): pt node to check
 */
static bool
qo_is_arithmetic_type (PT_NODE * attr)
{
  /*
   * This should probably be extended to look at host variables and do
   * the right thing.  If that happens, we also need to extend
   * get_const_value() to look inside the host variables as well.
   */
  switch (attr->node_type)
    {
    case PT_VALUE:
    case PT_NAME:
      return (attr->type_enum == PT_TYPE_INTEGER
	      || attr->type_enum == PT_TYPE_BIGINT
	      || attr->type_enum == PT_TYPE_FLOAT
	      || attr->type_enum == PT_TYPE_DOUBLE
	      || attr->type_enum == PT_TYPE_SMALLINT
	      || attr->type_enum == PT_TYPE_DATE
	      || attr->type_enum == PT_TYPE_TIME
	      || attr->type_enum == PT_TYPE_TIMESTAMP
	      || attr->type_enum == PT_TYPE_DATETIME
	      || attr->type_enum == PT_TYPE_MONETARY);

    default:
      break;
    }

  return false;
}

/*
 * get_const_value () - Get the value from the pt value node and coerce
 *			to double
 *   return:
 *   env(in):
 *   val(in): pt value node to get the constant value from
 *
 * Note: assumes an arithmetic type which will be guaranteed if
 *	qo_is_arithmetic_type() above is used before this call
 */
static double
get_const_value (QO_ENV * env, PT_NODE * val)
{
  QO_ASSERT (env, val->node_type == PT_VALUE);

  switch (val->type_enum)
    {
    case PT_TYPE_INTEGER:
      return (double) val->info.value.data_value.i;

    case PT_TYPE_BIGINT:
      return (double) val->info.value.data_value.bigint;

    case PT_TYPE_FLOAT:
      return (double) val->info.value.data_value.f;

    case PT_TYPE_DOUBLE:
      return (double) val->info.value.data_value.d;

    case PT_TYPE_TIME:
      return (double) val->info.value.data_value.time;

    case PT_TYPE_TIMESTAMP:
      return (double) val->info.value.data_value.utime;

    case PT_TYPE_DATETIME:
      {
	DB_BIGINT tmp_bi;
	tmp_bi = val->info.value.data_value.datetime.date;
	tmp_bi <<= 32;
	tmp_bi += val->info.value.data_value.datetime.time;
	return (double) tmp_bi;
      }

    case PT_TYPE_DATE:
      return (double) val->info.value.data_value.date;

    case PT_TYPE_MONETARY:
      return (double) val->info.value.data_value.money.amount;

    case PT_TYPE_SMALLINT:
      return (double) val->info.value.data_value.i;

    case PT_TYPE_NUMERIC:
      return (double) atof (val->info.value.text);

    default:
      QO_ASSERT (env, UNEXPECTED_CASE);
      return 0.0;
    }
}

/*
 * qo_index_cardinality () - Determine if the attribute has an index
 *   return: cardinality of the index if the index exists, otherwise return 0
 *   env(in): optimizer environment
 *   attr(in): pt node for the attribute for which we want the index cardinality
 */
static int
qo_index_cardinality (QO_ENV * env, PT_NODE * attr)
{
  PT_NODE *dummy;
  QO_NODE *nodep;
  QO_SEGMENT *segp;
  QO_ATTR_INFO *info;

  if (attr->node_type == PT_DOT_)
    {
      attr = attr->info.dot.arg2;
    }

  QO_ASSERT (env, attr->node_type == PT_NAME);

  nodep = lookup_node (attr, env, &dummy);
  if (nodep == NULL)
    {
      return 0;
    }

  segp = lookup_seg (nodep, attr, env);
  if (segp == NULL)
    {
      return 0;
    }

  info = QO_SEG_INFO (segp);
  if (info == NULL)
    {
      return 0;
    }

  if (info->cum_stats.is_indexed != true)
    {
      return 0;
    }

  QO_ASSERT (env, info->cum_stats.key_size > 0
	     && info->cum_stats.pkeys != NULL);

  /* return number of the first partial-key of the index on the attribute
     shown in the expression */
  return info->cum_stats.pkeys[0];
}

/*
 * qo_get_range () - Look up in the stats of the segment info the range of the
 *		  attribute
 *   return: 1 if success, otherwise 0
 *   env(in): optimizer environment
 *   attr(in): pt node to get the range for
 *   low_value(in): return variable for the low value of the range
 *   high_value(in): return variable for the high_value
 */
static int
qo_get_range (QO_ENV * env, PT_NODE * attr, double *low_value,
	      double *high_value)
{
  PT_NODE *dummy;
  QO_NODE *nodep;
  QO_SEGMENT *segp;
  QO_ATTR_CUM_STATS *cum_statsp;
  int rc = 0;

  if (attr->node_type == PT_DOT_)
    attr = attr->info.dot.arg2;

  QO_ASSERT (env, attr->node_type == PT_NAME);

  *low_value = *high_value = 0.0;

  if ((nodep = lookup_node (attr, env, &dummy)) == NULL)
    return rc;
  if ((segp = lookup_seg (nodep, attr, env)) == NULL)
    return rc;
  if (QO_SEG_INFO (segp) == NULL)
    return rc;
  if (!QO_SEG_INFO (segp)->cum_stats.valid_limits)
    return rc;

  cum_statsp = &(QO_SEG_INFO (segp)->cum_stats);
  switch (cum_statsp->type)
    {
    case DB_TYPE_INTEGER:
      *low_value = (double) cum_statsp->min_value.i;
      *high_value = (double) cum_statsp->max_value.i;
      rc = 1;
      break;
    case DB_TYPE_BIGINT:
      *low_value = (double) cum_statsp->min_value.bigint;
      *high_value = (double) cum_statsp->max_value.bigint;
      rc = 1;
      break;
    case DB_TYPE_FLOAT:
      *low_value = (double) cum_statsp->min_value.f;
      *high_value = (double) cum_statsp->max_value.f;
      rc = 1;
      break;
    case DB_TYPE_DOUBLE:
      *low_value = (double) cum_statsp->min_value.d;
      *high_value = (double) cum_statsp->max_value.d;
      rc = 1;
      break;
    case DB_TYPE_TIME:
      *low_value = (double) cum_statsp->min_value.time;
      *high_value = (double) cum_statsp->max_value.time;
      rc = 1;
      break;
    case DB_TYPE_UTIME:
      *low_value = (double) cum_statsp->min_value.utime;
      *high_value = (double) cum_statsp->max_value.utime;
      rc = 1;
      break;
    case DB_TYPE_DATETIME:
      {
	DB_BIGINT bi;

	bi = cum_statsp->min_value.datetime.date;
	bi = (bi * MILLISECONDS_OF_ONE_DAY
	      + cum_statsp->min_value.datetime.time);
	*low_value = (double) bi;

	bi = cum_statsp->max_value.datetime.date;
	bi = (bi * MILLISECONDS_OF_ONE_DAY
	      + cum_statsp->max_value.datetime.time);
	*high_value = (double) bi;
	rc = 1;
      }
      break;
    case DB_TYPE_DATE:
      *low_value = (double) cum_statsp->min_value.date;
      *high_value = (double) cum_statsp->max_value.date;
      rc = 1;
      break;
    case DB_TYPE_MONETARY:
      *low_value = (double) cum_statsp->min_value.money.amount;
      *high_value = (double) cum_statsp->max_value.money.amount;
      rc = 1;
      break;
    case DB_TYPE_SHORT:
      *low_value = (double) cum_statsp->min_value.sh;
      *high_value = (double) cum_statsp->max_value.sh;
      rc = 1;
      break;
    default:
      QO_ASSERT (env, UNEXPECTED_CASE);
      *low_value = *high_value = 0.0;
      break;
    }

  return rc;
}

/*
 * qo_index_scan_order_by_new () -
 *   return:
 *   info(in):
 *   node(in):
 *   ni_entry(in):
 *   range_terms(in):
 *   kf_terms(in):
 *   pinned_subqueries(in):
 */
static QO_PLAN *
qo_index_scan_order_by_new (QO_INFO * info, QO_NODE * node,
			    QO_NODE_INDEX_ENTRY * ni_entry,
			    BITSET * range_terms, BITSET * kf_terms,
			    BITSET * pinned_subqueries)
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

  plan = qo_scan_new (info, node, QO_SCANMETHOD_INDEX_ORDERBY_SCAN,
		      pinned_subqueries);
  if (plan == NULL)
    {
      return NULL;
    }

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
	      if (pt_expr
		  && PT_EXPR_INFO_IS_FLAGED (pt_expr,
					     PT_EXPR_INFO_FULL_RANGE))
		{
		  bitset_remove (&(plan->plan_un.scan.terms),
				 QO_TERM_IDX (term));
		}
	    }
	  break;
	}
    }

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
      if (pt_expr
	  && PT_EXPR_INFO_IS_FLAGED (pt_expr, PT_EXPR_INFO_FULL_RANGE))
	{
	  continue;		/* skip out unnecessary term */
	}

      bitset_assign (&term_segs, &(QO_TERM_SEGS (term)));
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

#if 0
  /* not yet used, please DO NOT DELETE ! */
  /* if the terms from key filter have the PT_IS_NOT_NULL op
   * we cut him from the plan because we will have a full index
   * scan
   */
  for (t = bitset_iterate (&(plan->plan_un.scan.kf_terms), &iter);
       t != -1; t = bitset_next_member (&iter))
    {
      QO_TERM *term = QO_ENV_TERM (info->env, t);
      PT_NODE *node = term->pt_expr;

      /* treat only the simplest case -> pt_is_not_null(name) */
      if (node->node_type == PT_EXPR &&
	  node->info.expr.op == PT_IS_NOT_NULL &&
	  node->info.expr.arg1->node_type == PT_NAME &&
	  index_entryp->seg_idxs[0] != -1 && node->or_next == NULL)
	{
	  /* check it's the same column as the first in the index */
	  const char *node_name = node->info.expr.arg1->info.name.original;
	  const char *index_key_name = info->env->
	    segs[index_entryp->seg_idxs[0]].name;

	  if (!intl_identifier_casecmp (node_name, index_key_name))
	    {
	      bitset_remove (&plan->plan_un.scan.kf_terms, t);
	    }
	}
    }
#endif

  bitset_delset (&term_segs);
  bitset_delset (&index_segs);

  qo_plan_compute_cost (plan);

  plan = qo_top_plan_new (plan);

  return plan;
}

/*
 * qo_generate_index_scan_from_orderby () - returns a plan which uses the
 *	supplied index, based upon the order by columns
 *   infop(in): pointer to QO_INFO (environment info node which holds plans)
 *   nodep(in): pointer to QO_NODE (node in the join graph)
 *   ni_entryp(in): pointer to QO_NODE_INDEX_ENTRY (node index entry)
 */
void
qo_generate_index_scan_from_orderby (QO_INFO * infop, QO_NODE * nodep,
				     QO_NODE_INDEX_ENTRY * ni_entryp)
{
  QO_INDEX_ENTRY *index_entryp;
  BITSET_ITERATOR iter;
  int i, t, nsegs, n;
  QO_PLAN *planp;
  BITSET range_terms;
  BITSET kf_terms;
  int start_column = 0;

  bitset_init (&range_terms, infop->env);
  bitset_init (&kf_terms, infop->env);

  /* pointer to QO_INDEX_ENTRY structure */
  index_entryp = (ni_entryp)->head;

  /* if virtual class, prevent the use of an index of the base class */
  if (nodep && nodep->class_name && index_entryp && index_entryp->class_ &&
      index_entryp->class_->name &&
      intl_identifier_casecmp (nodep->class_name, index_entryp->class_->name))
    {
      goto end;
    }

  if (QO_ENTRY_MULTI_COL (index_entryp))
    {
      bool plan_created = false;
      start_column = index_entryp->is_iss_candidate ? 1 : 0;

      /* the section below counts the total number of segments including
       * all equal ones + the next non-equal found. These will be the range
       * segments. For example: SELECT ... WHERE a = 1 and b = 2 and c > 10
       * chooses all a, b, c for range because the first two are with =
       */
      for (nsegs = start_column; nsegs < index_entryp->nsegs; nsegs++)
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

      if (nsegs == 0)
	{
	  /* this type of plan does not need the index to be covered if there
	   * are only kf terms
	   */
	  if (bitset_cardinality (&(index_entryp->key_filter_terms)))
	    {
	      bitset_assign (&kf_terms, &(index_entryp->key_filter_terms));
	      planp = qo_index_scan_order_by_new (infop, nodep, ni_entryp,
						  &range_terms,
						  &kf_terms,
						  &QO_NODE_SUBQUERIES
						  (nodep));

	      n = qo_check_plan_on_info (infop, planp);
	      if (n)
		{
		  plan_created = true;
		}
	    }

	  /* Case #1: we have only sarg terms:
	   * index on b,c only, select * from T where a < b + c order by b, c)
	   * -> use full scan index with sarg (a < b + c)
	   */
	  if (!plan_created)
	    {
	      planp = qo_index_scan_order_by_new (infop, nodep, ni_entryp,
						  &range_terms,
						  &kf_terms,
						  &QO_NODE_SUBQUERIES
						  (nodep));

	      n = qo_check_plan_on_info (infop, planp);
	      if (n)
		{
		  plan_created = true;
		}
	    }

	  goto end;
	}

      for (i = start_column; i < nsegs - 1; i++)
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
	  planp = qo_index_scan_order_by_new (infop, nodep, ni_entryp,
					      &range_terms,
					      &kf_terms,
					      &QO_NODE_SUBQUERIES (nodep));

	  n = qo_check_plan_on_info (infop, planp);

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
	  planp = qo_index_scan_order_by_new (infop, nodep, ni_entryp,
					      &range_terms,
					      &kf_terms,
					      &QO_NODE_SUBQUERIES (nodep));

	  n = qo_check_plan_on_info (infop, planp);

	  /* is it safe to ignore the result of qo_check_plan_on_info()? */
	  bitset_remove (&range_terms, t);
	}			/* for (t = ... ) */

    }
  else
    {				/* if (QO_ENTRY_MULTI_COL(index_entryp)) */
      bool plan_created = false;

      /* for all segments covered by this index and found in
         'find_node_indexes()' */
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
	      planp = qo_index_scan_order_by_new (infop, nodep, ni_entryp,
						  &range_terms,
						  &kf_terms,
						  &QO_NODE_SUBQUERIES
						  (nodep));

	      n = qo_check_plan_on_info (infop, planp);
	      if (n)
		{
		  plan_created = true;
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
	      planp = qo_index_scan_order_by_new (infop, nodep, ni_entryp,
						  &range_terms,
						  &kf_terms,
						  &QO_NODE_SUBQUERIES
						  (nodep));

	      n = qo_check_plan_on_info (infop, planp);
	      if (n)
		{
		  plan_created = true;
		}

	      /* is it safe to ignore the result of qo_check_plan_on_info()? */
	      bitset_remove (&range_terms, t);
	    }			/* for (t = ... ) */

	}			/* for (i = 0; i < index_entryp->nsegs; i++) */

      /* Case #1: we reach this if we have only key filter terms and single
       * column index
       */
      if (!plan_created)
	{
	  /* section added to support key filter terms for order by skipping
	   * also when we do not have covering.
	   * For example (select * from T where col is not null order by col)
	   */
	  if (bitset_cardinality (&(index_entryp->key_filter_terms)))
	    {
	      bitset_assign (&kf_terms, &(index_entryp->key_filter_terms));

	      planp = qo_index_scan_order_by_new (infop, nodep, ni_entryp,
						  &range_terms,
						  &kf_terms,
						  &QO_NODE_SUBQUERIES
						  (nodep));

	      n = qo_check_plan_on_info (infop, planp);

	      if (n)
		{
		  plan_created = true;
		}
	    }
	}

      /* Case #2: we have only sarg terms:
       * index_treeid, select * from T where treeid < id order by treeid)
       * -> use full scan index with sarg (treeid < id)
       */
      if (!plan_created && bitset_cardinality (&(index_entryp->terms)) > 0)
	{
	  planp = qo_index_scan_order_by_new (infop, nodep, ni_entryp,
					      &range_terms,
					      &kf_terms,
					      &QO_NODE_SUBQUERIES (nodep));

	  n = qo_check_plan_on_info (infop, planp);
	  if (n)
	    {
	      plan_created = true;
	    }
	}
    }				/* if (QO_ENTRY_MULTI_COL(index_entryp)) */

end:
  bitset_delset (&kf_terms);
  bitset_delset (&range_terms);
}

/*
 * qo_validate_index_for_orderby () - checks for isnull(key) or not null flag
 *  env(in): pointer to the optimizer environment
 *  ni_entryp(in): pointer to QO_NODE_INDEX_ENTRY (node index entry)
 *  return: 1 if the index can be used, 0 elseware
 */
int
qo_validate_index_for_orderby (QO_ENV * env, QO_NODE_INDEX_ENTRY * ni_entryp)
{
  int have_orderby_index = 0;
  PT_NODE *orderby_col = NULL;
  PT_NODE *node = NULL;
  int i, pos;
  QO_CLASS_INFO_ENTRY *index_class = ni_entryp->head->class_;
  void *env_seg[2];

  /* key_term_status is -1 if no term with key, 0 if isnull or is not null
   * terms with key and 1 if other term with key
   */
  int old_bail_out, key_term_status;
  bool key_notnull = false;
  QO_SEGMENT *segm = NULL;

  have_orderby_index = 0;
  if (!QO_ENV_PT_TREE (env) || !QO_ENV_PT_TREE (env)->info.query.order_by)
    {
      goto end;
    }

  /* do a check on the first column - it should be present in the where clause
   * with a non-null predicate (id > 3 or isnull(id) = false, etc, some
   * statement that ensures us the key cannot have null value) or if no
   * predicate in where clause, the attribute should have the not_null flag
   */

  /* check if exists a simple expression with PT_IS_NOT_NULL on the first key
   * this should not contain OR operator and the PT_IS_NOT_NULL should contain
   * the column directly as parameter (PT_NAME)
   */
  for (node = QO_ENV_PT_TREE (env)->info.query.q.select.where; node;
       node = node->next)
    {
      if (node->or_next)
	{
	  /* cancel the flag */
	  key_notnull = false;
	  break;
	}
      if (node->node_type == PT_EXPR && node->info.expr.op == PT_IS_NOT_NULL
	  && node->info.expr.arg1->node_type == PT_NAME)
	{
	  /* check it's the same column as the first in the index */
	  const char *node_name = pt_get_name (node->info.expr.arg1);
	  const char *index_key_name =
	    env->segs[ni_entryp->head->seg_idxs[0]].name;

	  if (!intl_identifier_casecmp (node_name, index_key_name))
	    {
	      /* we have found a term with no OR and with IS_NOT_NULL on our
	       * key. The plan is ready for order by skip!
	       */
	      key_notnull = true;
	      break;
	    }
	}
    }
  if (key_notnull)
    {
      goto final;
    }

  pos =
    QO_ENV_PT_TREE (env)->info.query.order_by->info.sort_spec.pos_descr.
    pos_no;
  node = QO_ENV_PT_TREE (env)->info.query.q.select.list;

  while (pos > 1 && node)
    {
      node = node->next;
      pos--;
    }
  if (!node)
    {
      goto end;
    }

  for (i = 0; i < env->nsegs; i++)
    {
      if (!intl_identifier_casecmp
	  (QO_ENV_SEG (env, i)->name, pt_get_name (node)))
	{
	  segm = QO_ENV_SEG (env, i);
	  break;
	}
    }
  if (!segm)
    {
      goto end;
    }

  /* we now search in the class columns for the index key */
  for (i = 0; i < index_class->smclass->att_count; i++)
    {
      SM_ATTRIBUTE *attr = &index_class->smclass->attributes[i];

      if (attr && !intl_identifier_casecmp (segm->name, attr->header.name))
	{
	  key_notnull = (attr->flags & SM_ATTFLAG_NON_NULL) != 0;
	  break;
	}			/* end if attr->order == pos */
    }				/* end for */

  if (i == index_class->smclass->att_count)
    {
      /* column wasn't found - this should not happen! */
      goto end;
    }
  if (key_notnull)
    {
      goto final;
    }

  /* now search for not terms with the key */

  /* save old value of bail_out */
  old_bail_out = env->bail_out;
  env->bail_out = -1;		/* no term found value */

  /* check for isnull terms with the key */
  env_seg[0] = (void *) env;
  env_seg[1] = (void *) segm;
  parser_walk_tree (env->parser,
		    QO_ENV_PT_TREE (env)->info.query.q.select.where,
		    search_isnull_key_expr_orderby, env_seg, NULL, NULL);

  /* restore old value and keep walk_tree result in key_term_status */
  key_term_status = env->bail_out;
  env->bail_out = old_bail_out;

  /* if there is no isnull on the key, check that the key appears in some term
   * and if so, make sure that that term doesn't have a OR
   */
  if (key_term_status == 1)
    {
      BITSET expr_segments, key_segment;

      /* key segment bitset */
      bitset_init (&key_segment, env);
      bitset_add (&key_segment, QO_SEG_IDX (segm));

      /* key found in a term */
      for (i = 0; i < env->nterms; i++)
	{
	  QO_TERM *term = QO_ENV_TERM (env, i);

	  if (term && term->pt_expr)
	    {
	      bitset_init (&expr_segments, env);
	      qo_expr_segs (env, term->pt_expr, &expr_segments);

	      if (bitset_intersects (&expr_segments, &key_segment))
		{
		  if (term->pt_expr->or_next)
		    {
		      goto end;
		    }
		}
	    }
	}
    }

  /* Now we have the information we need: if the key column can be null
   * and if there is a PT_IS_NULL or PT_IS_NOT_NULL expression with this
   * key column involved and also if we have other terms with the key.
   * We must decide if there can be NULLs in the results and if so,
   * drop this index.
   *
   * 1. If the key cannot have null values, we have a winner.
   * 2. Otherwise, if we found a term isnull/isnotnull(key) we drop it
   *    (because we cannot evaluate if this yields true or false so we skip all,
   *    for safety)
   * 3. If we have a term with other operator except isnull/isnotnull and
   *    does not have an OR following we have a winner again! (because we
   *    cannot have a null value).
   */
final:
  if (key_notnull || key_term_status == 1)
    {
      return 1;
    }
end:
  return 0;
}

/*
 * search_isnull_key_expr_orderby () -
 *   return: PT_NODE *
 *   parser(in): parser environment
 *   tree(in): tree to walk
 *   arg(in):
 *   continue_walk(in):
 *
 * Note: for env->bail_out values, check key_term_status in
 *	  qo_validate_index_for_orderby
 */
static PT_NODE *
search_isnull_key_expr_orderby (PARSER_CONTEXT * parser,
				PT_NODE * tree, void *arg, int *continue_walk)
{
  BITSET expr_segments, key_segment;
  QO_ENV *env;
  QO_SEGMENT *segm;
  void **env_seg = (void **) arg;

  env = (QO_ENV *) env_seg[0];
  segm = (QO_SEGMENT *) env_seg[1];

  *continue_walk = PT_CONTINUE_WALK;

  /* key segment bitset */
  bitset_init (&key_segment, env);
  bitset_add (&key_segment, QO_SEG_IDX (segm));

  bitset_init (&expr_segments, env);
  if (tree->node_type == PT_EXPR)
    {
      /* get all segments in this expression */
      qo_expr_segs (env, tree, &expr_segments);

      /* now check if the key segment is in there */
      if (bitset_intersects (&expr_segments, &key_segment))
	{
	  /* this expr contains the key segment */
	  if (tree->info.expr.op == PT_IS_NULL ||
	      tree->info.expr.op == PT_IS_NOT_NULL ||
	      tree->info.expr.op == PT_IFNULL ||
	      tree->info.expr.op == PT_NULLSAFE_EQ)
	    {
	      /* 0 all the way, suppress other terms found */
	      env->bail_out = 0;
	      *continue_walk = PT_STOP_WALK;

	      return tree;
	    }
	  else if (env->bail_out == -1)
	    {
	      /* set as 1 only if we haven't found any isnull terms */
	      env->bail_out = 1;
	    }
	}
    }

  return tree;
}

/*
 * qo_group_by_skip_plans_cmp () - compare 2 index scan plans by group by skip
 *   return:  one of {PLAN_COMP_UNK, PLAN_COMP_LT, PLAN_COMP_EQ, PLAN_COMP_GT}
 *   a(in):
 *   b(in):
 */
static QO_PLAN_COMPARE_RESULT
qo_group_by_skip_plans_cmp (QO_PLAN * a, QO_PLAN * b)
{
  QO_INDEX_ENTRY *a_ent, *b_ent;

  if (a == NULL || b == NULL ||
      (a->plan_un.scan.scan_method != QO_SCANMETHOD_INDEX_SCAN &&
       a->plan_un.scan.scan_method != QO_SCANMETHOD_INDEX_GROUPBY_SCAN &&
       a->plan_un.scan.scan_method != QO_SCANMETHOD_INDEX_ORDERBY_SCAN)
      ||
      (b->plan_un.scan.scan_method != QO_SCANMETHOD_INDEX_SCAN &&
       b->plan_un.scan.scan_method != QO_SCANMETHOD_INDEX_GROUPBY_SCAN &&
       b->plan_un.scan.scan_method != QO_SCANMETHOD_INDEX_ORDERBY_SCAN))
    {
      return PLAN_COMP_UNK;
    }

  a_ent = a->plan_un.scan.index->head;
  b_ent = b->plan_un.scan.index->head;

  if (a_ent == NULL || b_ent == NULL)
    {
      return PLAN_COMP_UNK;
    }

  if (a_ent->groupby_skip)
    {
      if (b_ent->groupby_skip)
	{
	  if (bitset_cardinality (&(a->plan_un.scan.terms)) > 0)
	    {
	      if (bitset_cardinality ((&b->plan_un.scan.terms)) == 0)
		{
		  return PLAN_COMP_LT;
		}
	    }
	  else
	    {
	      if (bitset_cardinality (&(b->plan_un.scan.terms)) > 0)
		{
		  return PLAN_COMP_GT;
		}
	    }
	  /* both plans have the same number of range terms
	   * we will check now the key filter terms
	   */
	  if (bitset_cardinality (&(a->plan_un.scan.kf_terms)) > 0)
	    {
	      if (bitset_cardinality ((&b->plan_un.scan.kf_terms)) == 0)
		{
		  return PLAN_COMP_LT;
		}
	    }
	  else
	    {
	      if (bitset_cardinality (&(b->plan_un.scan.kf_terms)) > 0)
		{
		  return PLAN_COMP_GT;
		}
	    }
	}
      else
	{
	  return PLAN_COMP_LT;
	}
    }
  else
    {
      if (b_ent->groupby_skip)
	{
	  return PLAN_COMP_GT;
	}
    }

  return PLAN_COMP_EQ;
}

/*
 * qo_order_by_skip_plans_cmp () - compare 2 index scan plans by order by skip
 *   return:  one of {PLAN_COMP_UNK, PLAN_COMP_LT, PLAN_COMP_EQ, PLAN_COMP_GT}
 *   a(in):
 *   b(in):
 */
static QO_PLAN_COMPARE_RESULT
qo_order_by_skip_plans_cmp (QO_PLAN * a, QO_PLAN * b)
{
  QO_INDEX_ENTRY *a_ent, *b_ent;

  if (a == NULL || b == NULL ||
      (a->plan_un.scan.scan_method != QO_SCANMETHOD_INDEX_SCAN &&
       a->plan_un.scan.scan_method != QO_SCANMETHOD_INDEX_GROUPBY_SCAN &&
       a->plan_un.scan.scan_method != QO_SCANMETHOD_INDEX_ORDERBY_SCAN)
      ||
      (b->plan_un.scan.scan_method != QO_SCANMETHOD_INDEX_SCAN &&
       b->plan_un.scan.scan_method != QO_SCANMETHOD_INDEX_GROUPBY_SCAN &&
       b->plan_un.scan.scan_method != QO_SCANMETHOD_INDEX_ORDERBY_SCAN))
    {
      return PLAN_COMP_UNK;
    }

  a_ent = a->plan_un.scan.index->head;
  b_ent = b->plan_un.scan.index->head;

  if (a_ent == NULL || b_ent == NULL)
    {
      return PLAN_COMP_UNK;
    }

  if (a_ent->orderby_skip)
    {
      if (b_ent->orderby_skip)
	{
	  if (bitset_cardinality (&(a->plan_un.scan.terms)) > 0)
	    {
	      if (bitset_cardinality ((&b->plan_un.scan.terms)) == 0)
		{
		  return PLAN_COMP_LT;
		}
	    }
	  else
	    {
	      if (bitset_cardinality (&(b->plan_un.scan.terms)) > 0)
		{
		  return PLAN_COMP_GT;
		}
	    }
	  /* both plans have the same number of range terms
	   * we will check now the key filter terms
	   */
	  if (bitset_cardinality (&(a->plan_un.scan.kf_terms)) > 0)
	    {
	      if (bitset_cardinality ((&b->plan_un.scan.kf_terms)) == 0)
		{
		  return PLAN_COMP_LT;
		}
	    }
	  else
	    {
	      if (bitset_cardinality (&(b->plan_un.scan.kf_terms)) > 0)
		{
		  return PLAN_COMP_GT;
		}
	    }
	}
      else
	{
	  return PLAN_COMP_LT;
	}
    }
  else
    {
      if (b_ent->orderby_skip)
	{
	  return PLAN_COMP_GT;
	}
    }

  return PLAN_COMP_EQ;
}

/*
 * qo_check_orderby_skip_descending - checks whether an index plan will
 *	    skip order by if its columns changes their direction.
 *   return:  true or false
 *   plan (in): input index plan to be analyzed
 */
static bool
qo_check_orderby_skip_descending (QO_PLAN * plan)
{
  bool orderby_skip = false;
  QO_ENV *env;
  PT_NODE *tree, *trav, *order_by;

  env = NULL;
  tree = order_by = NULL;

  if (plan == NULL)
    {
      goto end;
    }
  if (plan->info)
    {
      env = plan->info->env;
    }

  if (env == NULL)
    {
      goto end;
    }

  tree = QO_ENV_PT_TREE (env);

  if (tree == NULL)
    {
      goto end;
    }

  order_by = tree->info.query.order_by;

  for (trav = plan->iscan_sort_list; trav; trav = trav->next)
    {
      /* change PT_ASC to PT_DESC and vice-versa */
      trav->info.sort_spec.asc_or_desc = PT_ASC + PT_DESC -
	trav->info.sort_spec.asc_or_desc;
    }

  /* test again the order by skip */
  orderby_skip = pt_sort_spec_cover (plan->iscan_sort_list, order_by);

  /* change back directions */
  for (trav = plan->iscan_sort_list; trav; trav = trav->next)
    {
      /* change PT_ASC to PT_DESC and vice-versa */
      trav->info.sort_spec.asc_or_desc = PT_ASC + PT_DESC -
	trav->info.sort_spec.asc_or_desc;
    }

end:
  return orderby_skip;
}

/*
 * qo_generate_index_scan_from_groupby () - returns a plan which uses the
 *	supplied index, based upon the group by columns
 *   infop(in): pointer to QO_INFO (environment info node which holds plans)
 *   nodep(in): pointer to QO_NODE (node in the join graph)
 *   ni_entryp(in): pointer to QO_NODE_INDEX_ENTRY (node index entry)
 */
void
qo_generate_index_scan_from_groupby (QO_INFO * infop, QO_NODE * nodep,
				     QO_NODE_INDEX_ENTRY * ni_entryp)
{
  QO_INDEX_ENTRY *index_entryp;
  BITSET_ITERATOR iter;
  int i, t, nsegs, n;
  QO_PLAN *planp;
  BITSET range_terms;
  BITSET kf_terms;
  int start_column;

  bitset_init (&range_terms, infop->env);
  bitset_init (&kf_terms, infop->env);

  /* pointer to QO_INDEX_ENTRY structure */
  index_entryp = (ni_entryp)->head;

  /* if virtual class, prevent the use of an index of the base class */
  if (nodep && nodep->class_name && index_entryp && index_entryp->class_ &&
      index_entryp->class_->name &&
      intl_identifier_casecmp (nodep->class_name, index_entryp->class_->name))
    {
      goto end;
    }

  if (QO_ENTRY_MULTI_COL (index_entryp))
    {
      bool plan_created = false;
      start_column = index_entryp->is_iss_candidate ? 1 : 0;

      /* the section below counts the total number of segments including
       * all equal ones + the next non-equal found. These will be the range
       * segments. For example: SELECT ... WHERE a = 1 and b = 2 and c > 10
       * chooses all a, b, c for range because the first two are with =
       */
      for (nsegs = start_column; nsegs < index_entryp->nsegs; nsegs++)
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

      if (nsegs == 0)
	{
	  /* this type of plan does not need the index to be covered if there
	   * are only kf terms
	   */
	  if (bitset_cardinality (&(index_entryp->key_filter_terms)))
	    {
	      bitset_assign (&kf_terms, &(index_entryp->key_filter_terms));
	      planp = qo_index_scan_group_by_new (infop, nodep, ni_entryp,
						  &range_terms,
						  &kf_terms,
						  &QO_NODE_SUBQUERIES
						  (nodep));

	      n = qo_check_plan_on_info (infop, planp);
	      if (n)
		{
		  plan_created = true;
		}
	    }

	  /* Case #1: we have only sarg terms:
	   * index on b,c only, select * from T where a < b + c group by b, c)
	   * -> use full scan index with sarg (a < b + c)
	   */
	  if (!plan_created)
	    {
	      planp = qo_index_scan_group_by_new (infop, nodep, ni_entryp,
						  &range_terms,
						  &kf_terms,
						  &QO_NODE_SUBQUERIES
						  (nodep));

	      n = qo_check_plan_on_info (infop, planp);
	      if (n)
		{
		  plan_created = true;
		}
	    }

	  goto end;
	}

      for (i = start_column; i < nsegs - 1; i++)
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
	  planp = qo_index_scan_group_by_new (infop, nodep, ni_entryp,
					      &range_terms,
					      &kf_terms,
					      &QO_NODE_SUBQUERIES (nodep));

	  n = qo_check_plan_on_info (infop, planp);

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
	  planp = qo_index_scan_group_by_new (infop, nodep, ni_entryp,
					      &range_terms,
					      &kf_terms,
					      &QO_NODE_SUBQUERIES (nodep));

	  n = qo_check_plan_on_info (infop, planp);

	  /* is it safe to ignore the result of qo_check_plan_on_info()? */
	  bitset_remove (&range_terms, t);
	}			/* for (t = ... ) */

    }
  else
    {				/* if (QO_ENTRY_MULTI_COL(index_entryp)) */
      bool plan_created = false;

      /* for all segments covered by this index and found in
         'find_node_indexes()' */
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
	      planp = qo_index_scan_group_by_new (infop, nodep, ni_entryp,
						  &range_terms,
						  &kf_terms,
						  &QO_NODE_SUBQUERIES
						  (nodep));

	      n = qo_check_plan_on_info (infop, planp);
	      if (n)
		{
		  plan_created = true;
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
	      planp = qo_index_scan_group_by_new (infop, nodep, ni_entryp,
						  &range_terms,
						  &kf_terms,
						  &QO_NODE_SUBQUERIES
						  (nodep));

	      n = qo_check_plan_on_info (infop, planp);
	      if (n)
		{
		  plan_created = true;
		}

	      /* is it safe to ignore the result of qo_check_plan_on_info()? */
	      bitset_remove (&range_terms, t);
	    }			/* for (t = ... ) */

	}			/* for (i = 0; i < index_entryp->nsegs; i++) */

      /* Case #1: we reach this if we have only key filter terms and single
       * column index
       */
      if (!plan_created)
	{
	  /* section added to support key filter terms for group by skipping
	   * also when we do not have covering.
	   * For example (select * from T where col is not null group by col)
	   */
	  if (bitset_cardinality (&(index_entryp->key_filter_terms)))
	    {
	      bitset_assign (&kf_terms, &(index_entryp->key_filter_terms));

	      planp = qo_index_scan_group_by_new (infop, nodep, ni_entryp,
						  &range_terms,
						  &kf_terms,
						  &QO_NODE_SUBQUERIES
						  (nodep));

	      n = qo_check_plan_on_info (infop, planp);

	      if (n)
		{
		  plan_created = true;
		}
	    }
	}

      /* Case #2: we have only sarg terms:
       * index_treeid, select * from T where treeid < id group by treeid)
       * -> use full scan index with sarg (treeid < id)
       */
      if (!plan_created && bitset_cardinality (&(index_entryp->terms)) > 0)
	{
	  planp = qo_index_scan_group_by_new (infop, nodep, ni_entryp,
					      &range_terms,
					      &kf_terms,
					      &QO_NODE_SUBQUERIES (nodep));

	  n = qo_check_plan_on_info (infop, planp);
	  if (n)
	    {
	      plan_created = true;
	    }
	}
    }				/* if (QO_ENTRY_MULTI_COL(index_entryp)) */

end:
  bitset_delset (&kf_terms);
  bitset_delset (&range_terms);
}

/*
 * qo_index_scan_group_by_new () -
 *   return:
 *   info(in):
 *   node(in):
 *   ni_entry(in):
 *   range_terms(in):
 *   kf_terms(in):
 *   pinned_subqueries(in):
 */
static QO_PLAN *
qo_index_scan_group_by_new (QO_INFO * info, QO_NODE * node,
			    QO_NODE_INDEX_ENTRY * ni_entry,
			    BITSET * range_terms, BITSET * kf_terms,
			    BITSET * pinned_subqueries)
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

  plan = qo_scan_new (info, node, QO_SCANMETHOD_INDEX_GROUPBY_SCAN,
		      pinned_subqueries);
  if (plan == NULL)
    {
      return NULL;
    }

  /*
   * This is, in essence, the selectivity of the index.  We
   * really need to do a better job of figuring out the cost of
   * an indexed scan.
   */
  plan->vtbl = &qo_index_scan_plan_vtbl;
  plan->plan_un.scan.index = ni_entry;

  /* set key-range terms */
  bitset_assign (&(plan->plan_un.scan.terms), range_terms);
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
	      if (pt_expr
		  && PT_EXPR_INFO_IS_FLAGED (pt_expr,
					     PT_EXPR_INFO_FULL_RANGE))
		{
		  bitset_remove (&(plan->plan_un.scan.terms),
				 QO_TERM_IDX (term));
		}
	    }
	  break;
	}
    }

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
      if (pt_expr
	  && PT_EXPR_INFO_IS_FLAGED (pt_expr, PT_EXPR_INFO_FULL_RANGE))
	{
	  continue;		/* skip out unnecessary term */
	}

      bitset_assign (&term_segs, &(QO_TERM_SEGS (term)));
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

#if 0
  /* not yet used, please DO NOT DELETE ! */
  /* if the terms from key filter have the PT_IS_NOT_NULL op
   * we cut him from the plan because we will have a full index
   * scan
   */
  for (t = bitset_iterate (&(plan->plan_un.scan.kf_terms), &iter);
       t != -1; t = bitset_next_member (&iter))
    {
      QO_TERM *term = QO_ENV_TERM (info->env, t);
      PT_NODE *node = term->pt_expr;

      /* treat only the simplest case -> pt_is_not_null(name) */
      if (node->node_type == PT_EXPR &&
	  node->info.expr.op == PT_IS_NOT_NULL &&
	  node->info.expr.arg1->node_type == PT_NAME &&
	  index_entryp->seg_idxs[0] != -1 && node->or_next == NULL)
	{
	  /* check it's the same column as the first in the index */
	  const char *node_name = node->info.expr.arg1->info.name.original;
	  const char *index_key_name = info->env->
	    segs[index_entryp->seg_idxs[0]].name;

	  if (!intl_identifier_casecmp (node_name, index_key_name))
	    {
	      bitset_remove (&plan->plan_un.scan.kf_terms, t);
	    }
	}
    }
#endif

  bitset_delset (&term_segs);
  bitset_delset (&index_segs);

  qo_plan_compute_cost (plan);

  plan = qo_top_plan_new (plan);

  return plan;
}

/*
 * qo_validate_index_for_groupby () - checks for isnull(key) or not null flag
 *  env(in): pointer to the optimizer environment
 *  ni_entryp(in): pointer to QO_NODE_INDEX_ENTRY (node index entry)
 *  return: 1 if the index can be used, 0 elseware
 */
int
qo_validate_index_for_groupby (QO_ENV * env, QO_NODE_INDEX_ENTRY * ni_entryp)
{
  int have_groupby_index = 0;
  PT_NODE *groupby_col = NULL;
  PT_NODE *node = NULL;
  int i;
  QO_CLASS_INFO_ENTRY *index_class = ni_entryp->head->class_;
  void *env_seg[2];

  /* key_term_status is -1 if no term with key, 0 if isnull or is not null
   * terms with key and 1 if other term with key
   */
  int old_bail_out, key_term_status;
  bool key_notnull = false;
  const char *first_col_name = NULL;
  PT_NODE *groupby_expr = NULL;
  QO_SEGMENT *segm = NULL;

  have_groupby_index = 0;
  if (!QO_ENV_PT_TREE (env) ||
      !QO_ENV_PT_TREE (env)->info.query.q.select.group_by)
    {
      goto end;
    }

  /* do a check on the first column - it should be present in the where clause
   * with a non-null predicate (id > 3 or isnull(id) = false, etc, some
   * statement that ensures us the key cannot have null value) or if no
   * predicate in where clause, the attribute should have the not_null flag
   */

  /* check if exists a simple expression with PT_IS_NOT_NULL on the first key
   * this should not contain OR operator and the PT_IS_NOT_NULL should contain
   * the column directly as parameter (PT_NAME)
   */
  for (node = QO_ENV_PT_TREE (env)->info.query.q.select.where; node;
       node = node->next)
    {
      if (node->or_next)
	{
	  /* cancel the flag */
	  key_notnull = false;
	  break;
	}
      if (node->node_type == PT_EXPR && node->info.expr.op == PT_IS_NOT_NULL
	  && node->info.expr.arg1->node_type == PT_NAME)
	{
	  /* check it's the same column as the first in the index */
	  const char *node_name = pt_get_name (node->info.expr.arg1);
	  const char *index_key_name =
	    env->segs[ni_entryp->head->seg_idxs[0]].name;

	  if (!intl_identifier_casecmp (node_name, index_key_name))
	    {
	      /* we have found a term with no OR and with IS_NOT_NULL on our
	       * key. The plan is ready for group by skip!
	       */
	      key_notnull = true;
	      break;
	    }
	}
    }
  if (key_notnull)
    {
      goto final;
    }

  /* get the name of the first column in the group by list */
  groupby_expr =
    QO_ENV_PT_TREE (env)->info.query.q.select.group_by->info.sort_spec.expr;
  first_col_name = pt_get_name (groupby_expr);
  if (!first_col_name)
    {
      goto end;
    }

  /* we now search in the class columns for the index key */
  for (i = 0; i < index_class->smclass->att_count; i++)
    {
      SM_ATTRIBUTE *attr = &index_class->smclass->attributes[i];

      if (first_col_name && attr &&
	  !intl_identifier_casecmp (first_col_name, attr->header.name))
	{
	  key_notnull = (attr->flags & SM_ATTFLAG_NON_NULL) != 0;
	  break;
	}
    }				/* end for */

  if (i == index_class->smclass->att_count)
    {
      /* column wasn't found - this should not happen! */
      goto end;
    }
  if (key_notnull)
    {
      goto final;
    }

  for (i = 0; i < env->nsegs; i++)
    {
      if (!intl_identifier_casecmp
	  (QO_ENV_SEG (env, i)->name, first_col_name))
	{
	  segm = QO_ENV_SEG (env, i);
	  break;
	}
    }
  if (!segm)
    {
      goto end;
    }

  /* now search for not terms with the key */

  /* save old value of bail_out */
  old_bail_out = env->bail_out;
  env->bail_out = -1;		/* no term found value */

  env_seg[0] = (void *) env;
  env_seg[1] = (void *) segm;
  /* check for isnull terms with the key */
  parser_walk_tree (env->parser,
		    QO_ENV_PT_TREE (env)->info.query.q.select.where,
		    search_isnull_key_expr_groupby, env_seg, NULL, NULL);

  /* restore old value and keep walk_tree result in key_term_status */
  key_term_status = env->bail_out;
  env->bail_out = old_bail_out;

  /* if there is no isnull on the key, check that the key appears in some term
   * and if so, make sure that that term doesn't have a OR
   */
  if (key_term_status == 1)
    {
      BITSET expr_segments, key_segment;

      /* key segment bitset */
      bitset_init (&key_segment, env);
      bitset_add (&key_segment, QO_SEG_IDX (segm));

      /* key found in a term */
      for (i = 0; i < env->nterms; i++)
	{
	  QO_TERM *term = QO_ENV_TERM (env, i);

	  if (term && term->pt_expr)
	    {
	      bitset_init (&expr_segments, env);
	      qo_expr_segs (env, term->pt_expr, &expr_segments);

	      if (bitset_intersects (&expr_segments, &key_segment))
		{
		  if (term->pt_expr->or_next)
		    {
		      goto end;
		    }
		}
	    }
	}
    }

  /* Now we have the information we need: if the key column can be null
   * and if there is a PT_IS_NULL or PT_IS_NOT_NULL expression with this
   * key column involved and also if we have other terms with the key.
   * We must decide if there can be NULLs in the results and if so,
   * drop this index.
   *
   * 1. If the key cannot have null values, we have a winner.
   * 2. Otherwise, if we found a term isnull/isnotnull(key) we drop it
   *    (because we cannot evaluate if this yields true or false so we skip all,
   *    for safety)
   * 3. If we have a term with other operator except isnull/isnotnull and
   *    does not have an OR following we have a winner again! (because we
   *    cannot have a null value).
   */
final:
  if (key_notnull || key_term_status == 1)
    {
      return 1;
    }
end:
  return 0;
}

/*
 * qo_check_groupby_skip_descending - checks whether an index plan will
 *	    skip group by if its columns changes their direction.
 *   return:  true or false
 *   plan (in): input index plan to be analyzed
 */
static bool
qo_check_groupby_skip_descending (QO_PLAN * plan, PT_NODE * list)
{
  bool groupby_skip = false;
  QO_ENV *env;
  PT_NODE *tree, *trav, *group_by;

  env = NULL;
  tree = group_by = NULL;

  if (plan == NULL)
    {
      goto end;
    }
  if (plan->info)
    {
      env = plan->info->env;
    }

  if (env == NULL)
    {
      goto end;
    }

  tree = QO_ENV_PT_TREE (env);

  if (tree == NULL)
    {
      goto end;
    }

  group_by = tree->info.query.q.select.group_by;

  for (trav = list; trav; trav = trav->next)
    {
      /* change PT_ASC to PT_DESC and vice-versa */
      trav->info.sort_spec.asc_or_desc = PT_ASC + PT_DESC -
	trav->info.sort_spec.asc_or_desc;
    }

  /* test again the group by skip */
  groupby_skip = pt_sort_spec_cover_groupby (env->parser, list, group_by,
					     tree);

  /* change back directions */
  for (trav = list; trav; trav = trav->next)
    {
      /* change PT_ASC to PT_DESC and vice-versa */
      trav->info.sort_spec.asc_or_desc = PT_ASC + PT_DESC -
	trav->info.sort_spec.asc_or_desc;
    }

end:
  return groupby_skip;
}

/*
 * search_isnull_key_expr_groupby () -
 *   return: PT_NODE *
 *   parser(in): parser environment
 *   tree(in): tree to walk
 *   arg(in):
 *   continue_walk(in):
 *
 * Note: for env->bail_out values, check key_term_status in
 *	  qo_validate_index_for_groupby
 */
static PT_NODE *
search_isnull_key_expr_groupby (PARSER_CONTEXT * parser,
				PT_NODE * tree, void *arg, int *continue_walk)
{
  BITSET expr_segments, key_segment;
  QO_ENV *env;
  QO_SEGMENT *segm;
  void **env_seg = (void **) arg;

  env = (QO_ENV *) env_seg[0];
  segm = (QO_SEGMENT *) env_seg[1];

  *continue_walk = PT_CONTINUE_WALK;

  /* key segment bitset */
  bitset_init (&key_segment, env);
  bitset_add (&key_segment, QO_SEG_IDX (segm));

  bitset_init (&expr_segments, env);
  if (tree->node_type == PT_EXPR)
    {
      /* get all segments in this expression */
      qo_expr_segs (env, tree, &expr_segments);

      /* now check if the key segment is in there */
      if (bitset_intersects (&expr_segments, &key_segment))
	{
	  /* this expr contains the key segment */
	  if (tree->info.expr.op == PT_IS_NULL ||
	      tree->info.expr.op == PT_IS_NOT_NULL ||
	      tree->info.expr.op == PT_IFNULL ||
	      tree->info.expr.op == PT_NULLSAFE_EQ)
	    {
	      /* 0 all the way, suppress other terms found */
	      env->bail_out = 0;
	      *continue_walk = PT_STOP_WALK;

	      return tree;
	    }
	  else if (env->bail_out == -1)
	    {
	      /* set as 1 only if we haven't found any isnull terms */
	      env->bail_out = 1;
	    }
	}
    }

  return tree;
}

/*
 * qo_plan_compute_iscan_group_sort_list () -
 *   return:
 *   root(in):
 *   out_list(out): sort_list for group by node
 *
 */
static void
qo_plan_compute_iscan_group_sort_list (QO_PLAN * root, PT_NODE ** out_list,
				       bool * is_index_w_prefix)
{
  QO_PLAN *plan;
  QO_ENV *env;
  PARSER_CONTEXT *parser;
  PT_NODE *tree, *sort_list, *sort, *col, *node, *expr;
  QO_NODE_INDEX_ENTRY *ni_entryp;
  QO_INDEX_ENTRY *index_entryp;
  int nterms, equi_nterms, seg_idx, i, j;
  QO_SEGMENT *seg;
  PT_MISC_TYPE asc_or_desc;
  QFILE_TUPLE_VALUE_POSITION pos_descr;
  TP_DOMAIN *key_type;
  BITSET *terms;
  BITSET_ITERATOR bi;
  bool is_const_eq_term;

  *is_index_w_prefix = false;

  /* find sortable plan */
  plan = root;
  while (plan->plan_type == QO_PLANTYPE_FOLLOW
	 || (plan->plan_type == QO_PLANTYPE_JOIN
	     && (plan->plan_un.join.join_method == QO_JOINMETHOD_NL_JOIN
		 || (plan->plan_un.join.join_method ==
		     QO_JOINMETHOD_IDX_JOIN))))
    {
      plan = ((plan->plan_type == QO_PLANTYPE_FOLLOW)
	      ? plan->plan_un.follow.head : plan->plan_un.join.outer);
    }

  /* check for plan type */
  if (plan == NULL || plan->plan_type != QO_PLANTYPE_SCAN)
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
  if (plan->plan_type != QO_PLANTYPE_SCAN
      || (plan->plan_un.scan.scan_method != QO_SCANMETHOD_INDEX_SCAN
	  && plan->plan_un.scan.scan_method !=
	  QO_SCANMETHOD_INDEX_ORDERBY_SCAN
	  && plan->plan_un.scan.scan_method !=
	  QO_SCANMETHOD_INDEX_GROUPBY_SCAN)
      || (env = (plan->info)->env) == NULL
      || (parser = QO_ENV_PARSER (env)) == NULL
      || (tree = QO_ENV_PT_TREE (env)) == NULL)
    {
      return;			/* nop */
    }

  /* if no index scan terms, no index scan */
  nterms = bitset_cardinality (&(plan->plan_un.scan.terms));

  if (nterms <= 0 &&
      plan->plan_un.scan.scan_method != QO_SCANMETHOD_INDEX_ORDERBY_SCAN &&
      plan->plan_un.scan.scan_method != QO_SCANMETHOD_INDEX_GROUPBY_SCAN)
    {
      return;			/* nop */
    }

  /* pointer to QO_NODE_INDEX_ENTRY structure in QO_PLAN */
  ni_entryp = plan->plan_un.scan.index;
  /* pointer to linked list of index node, 'head' field(QO_INDEX_ENTRY
     strucutre) of QO_NODE_INDEX_ENTRY */
  index_entryp = (ni_entryp)->head;

  /* check if this is an index with prefix */
  *is_index_w_prefix = qo_is_prefix_index (index_entryp);

  asc_or_desc =
    (SM_IS_CONSTRAINT_REVERSE_INDEX_FAMILY (index_entryp->constraints->type)
     ? PT_DESC : PT_ASC);

  equi_nterms = plan->plan_un.scan.equi ? nterms : nterms - 1;
  if (index_entryp->rangelist_seg_idx != -1)
    {
      equi_nterms = MIN (equi_nterms, index_entryp->rangelist_seg_idx);
    }

  /* we must have the first index column appear as the first sort column, so
   * we pretend the number of equi columns is zero, to force it to match
   * the sort list and the index columns one-for-one. */
  if (index_entryp->is_iss_candidate)
    {
      equi_nterms = 0;
    }

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

      /* get the first non-equal range key domain */
      for (j = 0; j < equi_nterms && key_type; j++)
	{
	  key_type = key_type->next;
	}

      if (key_type == NULL)
	{			/* invalid case */
	  return;		/* nop */
	}
    }

  sort_list = NULL;		/* init */

  for (i = equi_nterms; i < index_entryp->nsegs; i++)
    {
      if (key_type)
	{
	  asc_or_desc = (key_type->is_desc) ? PT_DESC : PT_ASC;
	  key_type = key_type->next;
	}

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

      /* skip segment of const eq term */
      terms = &(QO_SEG_INDEX_TERMS (seg));
      is_const_eq_term = false;
      for (j = bitset_iterate (terms, &bi); j != -1;
	   j = bitset_next_member (&bi))
	{
	  expr = QO_TERM_PT_EXPR (QO_ENV_TERM (env, j));
	  if (PT_IS_EXPR_NODE_WITH_OPERATOR (expr, PT_EQ) &&
	      (PT_IS_CONST (expr->info.expr.arg1) ||
	       PT_IS_CONST (expr->info.expr.arg2)))
	    {
	      is_const_eq_term = true;
	    }
	}
      if (is_const_eq_term)
	{
	  continue;
	}

      /* check for constant col's order node
       */
      pt_to_pos_descr (parser, &pos_descr, node, tree, NULL);
      if (pos_descr.pos_no > 0)
	{
	  col = tree->info.query.q.select.list;
	  for (j = 1; j < pos_descr.pos_no && col; j++)
	    {
	      col = col->next;
	    }
	  if (col != NULL)
	    {
	      col = pt_get_end_path_node (col);

	      if (col->node_type == PT_NAME
		  && PT_NAME_INFO_IS_FLAGED (col, PT_NAME_INFO_CONSTANT))
		{
		  continue;	/* skip out constant order */
		}
	    }
	}

      pt_to_pos_descr_groupby (parser, &pos_descr, node, tree);
      if (pos_descr.pos_no <= 0)
	{			/* not found i-th key element */
	  break;		/* give up */
	}

      col = tree->info.query.q.select.group_by;
      for (j = 1; j < pos_descr.pos_no && col; j++)
	{
	  col = col->next;
	}
      while (col != NULL && col->node_type == PT_SORT_SPEC)
	{
	  col = col->info.sort_spec.expr;
	}
      col = pt_get_end_path_node (col);

      if (col == NULL)
	{			/* impossible case */
	  break;		/* give up */
	}

      /* set sort info */
      sort = parser_new_node (parser, PT_SORT_SPEC);
      if (sort == NULL)
	{
	  PT_ERRORm (parser, node, MSGCAT_SET_PARSER_SEMANTIC,
		     MSGCAT_SEMANTIC_OUT_OF_MEMORY);
	  break;		/* give up */
	}

      sort->info.sort_spec.expr = pt_point (parser, col);
      sort->info.sort_spec.pos_descr = pos_descr;
      sort->info.sort_spec.asc_or_desc = asc_or_desc;

      sort_list = parser_append_node (sort, sort_list);
    }

  *out_list = sort_list;

  return;
}
