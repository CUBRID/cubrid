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
 * query_planner.c - Plan descriptors
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>
#if !defined(WINDOWS)
#include <values.h>
#endif /* !WINDOWS */
#include "jansson.h"

#include "parser.h"
#include "object_primitive.h"
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
#include "xasl_analytic.hpp"
#include "xasl_generation.h"
#include "schema_manager.h"
#include "network_interface_cl.h"
#include "dbtype.h"
#include "regu_var.hpp"

#define INDENT_INCR		4
#define INDENT_FMT		"%*c"
#define TITLE_WIDTH		7
#define TITLE_FMT		"%-" __STR(TITLE_WIDTH) "s"
#define INDENTED_TITLE_FMT	INDENT_FMT TITLE_FMT
#define __STR(n)		__VAL(n)
#define __VAL(n)		#n
#define SORT_SPEC_FMT(spec) \
  "%d %s %s", (spec)->pos_descr.pos_no + 1, \
  ((spec)->s_order == S_ASC ? "asc" : "desc"), \
  ((spec)->s_nulls == S_NULLS_FIRST ? "nulls first" : "nulls last")

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

#define QO_IS_LIMIT_NODE(env, node) \
  (BITSET_MEMBER (QO_ENV_SORT_LIMIT_NODES ((env)), QO_NODE_IDX ((node))))

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

static QO_PLAN *qo_scan_new (QO_INFO *, QO_NODE *, QO_SCANMETHOD);
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

void qo_plan_lite_print (QO_PLAN *, FILE *, int);
static void qo_plan_del_ref_func (QO_PLAN * plan, void *ignore);

static void qo_generic_walk (QO_PLAN *, void (*)(QO_PLAN *, void *), void *, void (*)(QO_PLAN *, void *), void *);
static void qo_plan_print_sort_spec_helper (PT_NODE *, bool, FILE *, int);
static void qo_plan_print_sort_spec (QO_PLAN *, FILE *, int);
static void qo_plan_print_costs (QO_PLAN *, FILE *, int);
static void qo_plan_print_projected_segs (QO_PLAN *, FILE *, int);
static void qo_plan_print_sarged_terms (QO_PLAN *, FILE *, int);
static void qo_plan_print_outer_join_terms (QO_PLAN *, FILE *, int);
static void qo_plan_print_subqueries (QO_PLAN *, FILE *, int);
static void qo_plan_print_analytic_eval (QO_PLAN *, FILE *, int);
static void qo_sort_walk (QO_PLAN *, void (*)(QO_PLAN *, void *), void *, void (*)(QO_PLAN *, void *), void *);
static void qo_join_walk (QO_PLAN *, void (*)(QO_PLAN *, void *), void *, void (*)(QO_PLAN *, void *), void *);
static void qo_follow_walk (QO_PLAN *, void (*)(QO_PLAN *, void *), void *, void (*)(QO_PLAN *, void *), void *);

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

static void qo_info_nodes_init (QO_ENV *);
static QO_INFO *qo_alloc_info (QO_PLANNER *, BITSET *, BITSET *, BITSET *, double);
static void qo_free_info (QO_INFO *);
static void qo_detach_info (QO_INFO *);
static void qo_dump_planvec (QO_PLANVEC *, FILE *, int);
static void qo_dump_info (QO_INFO *, FILE *);
static void qo_dump_planner_info (QO_PLANNER *, QO_PARTITION *, FILE *);

static void planner_visit_node (QO_PLANNER *, QO_PARTITION *, PT_HINT_ENUM, QO_NODE *, QO_NODE *, BITSET *, BITSET *,
				BITSET *, BITSET *, BITSET *, BITSET *, BITSET *, int);
static double planner_nodeset_join_cost (QO_PLANNER *, BITSET *);
static void planner_permutate (QO_PLANNER *, QO_PARTITION *, PT_HINT_ENUM, QO_NODE *, BITSET *, BITSET *, BITSET *,
			       BITSET *, BITSET *, BITSET *, BITSET *, BITSET *, int, int *);

static QO_PLAN *qo_find_best_nljoin_inner_plan_on_info (QO_PLAN *, QO_INFO *, JOIN_TYPE, int);
static QO_PLAN *qo_find_best_plan_on_info (QO_INFO *, QO_EQCLASS *, double);
static bool qo_check_new_best_plan_on_info (QO_INFO *, QO_PLAN *);
static int qo_check_plan_on_info (QO_INFO *, QO_PLAN *);
static int qo_examine_idx_join (QO_INFO *, JOIN_TYPE, QO_INFO *, QO_INFO *, BITSET *, BITSET *, BITSET *);
static int qo_examine_nl_join (QO_INFO *, JOIN_TYPE, QO_INFO *, QO_INFO *, BITSET *, BITSET *, BITSET *, BITSET *,
			       BITSET *, int, BITSET *);
static int qo_examine_merge_join (QO_INFO *, JOIN_TYPE, QO_INFO *, QO_INFO *, BITSET *, BITSET *, BITSET *, BITSET *,
				  BITSET *);
static int qo_examine_correlated_index (QO_INFO *, JOIN_TYPE, QO_INFO *, QO_INFO *, BITSET *, BITSET *, BITSET *);
static int qo_examine_follow (QO_INFO *, QO_TERM *, QO_INFO *, BITSET *, BITSET *);
static void qo_compute_projected_segs (QO_PLANNER *, BITSET *, BITSET *, BITSET *);
static int qo_compute_projected_size (QO_PLANNER *, BITSET *);


static QO_PLANNER *qo_alloc_planner (QO_ENV *);
static void qo_clean_planner (QO_PLANNER *);
static QO_INFO *qo_search_partition_join (QO_PLANNER *, QO_PARTITION *, BITSET *);
static QO_PLAN *qo_search_partition (QO_PLANNER *, QO_PARTITION *, QO_EQCLASS *, BITSET *);
static QO_PLAN *qo_search_planner (QO_PLANNER *);
static void sort_partitions (QO_PLANNER *);
static QO_PLAN *qo_combine_partitions (QO_PLANNER *, BITSET *);
static int qo_generate_join_index_scan (QO_INFO *, JOIN_TYPE, QO_PLAN *, QO_INFO *, QO_NODE *, QO_NODE_INDEX_ENTRY *,
					BITSET *, BITSET *, BITSET *, BITSET *);
static void qo_generate_seq_scan (QO_INFO *, QO_NODE *);
static int qo_generate_index_scan (QO_INFO *, QO_NODE *, QO_NODE_INDEX_ENTRY *, int);
static int qo_generate_loose_index_scan (QO_INFO *, QO_NODE *, QO_NODE_INDEX_ENTRY *);
static int qo_generate_sort_limit_plan (QO_ENV *, QO_INFO *, QO_PLAN *);
static void qo_plan_add_to_free_list (QO_PLAN *, void *ignore);
static void qo_nljoin_cost (QO_PLAN *);
static void qo_plans_teardown (QO_ENV * env);
static void qo_plans_init (QO_ENV * env);
static void qo_plan_walk (QO_PLAN *, void (*)(QO_PLAN *, void *), void *, void (*)(QO_PLAN *, void *), void *);
static QO_PLAN *qo_plan_finalize (QO_PLAN *);
static QO_PLAN *qo_plan_order_by (QO_PLAN *, QO_EQCLASS *);
static QO_PLAN_COMPARE_RESULT qo_plan_cmp_prefer_covering_index (QO_PLAN *, QO_PLAN *);
static void qo_plan_fprint (QO_PLAN *, FILE *, int, const char *);
static QO_PLAN_COMPARE_RESULT qo_plan_cmp (QO_PLAN *, QO_PLAN *);
static QO_PLAN_COMPARE_RESULT qo_plan_iscan_terms_cmp (QO_PLAN * a, QO_PLAN * b);
static QO_PLAN_COMPARE_RESULT qo_index_covering_plans_cmp (QO_PLAN *, QO_PLAN *);
static QO_PLAN_COMPARE_RESULT qo_order_by_skip_plans_cmp (QO_PLAN *, QO_PLAN *);
static QO_PLAN_COMPARE_RESULT qo_group_by_skip_plans_cmp (QO_PLAN *, QO_PLAN *);
static QO_PLAN_COMPARE_RESULT qo_multi_range_opt_plans_cmp (QO_PLAN *, QO_PLAN *);
static void qo_plan_free (QO_PLAN *);
static QO_PLAN *qo_plan_malloc (QO_ENV *);
static const char *qo_term_string (QO_TERM *);
static QO_PLAN *qo_worst_new (QO_ENV *);
static QO_PLAN *qo_cp_new (QO_INFO *, QO_PLAN *, QO_PLAN *, BITSET *, BITSET *);
static QO_PLAN *qo_follow_new (QO_INFO *, QO_PLAN *, QO_TERM *, BITSET *, BITSET *);
static QO_PLAN *qo_join_new (QO_INFO *, JOIN_TYPE, QO_JOINMETHOD, QO_PLAN *, QO_PLAN *, BITSET *, BITSET *, BITSET *,
			     BITSET *, BITSET *, BITSET *);
static QO_PLAN *qo_sort_new (QO_PLAN *, QO_EQCLASS *, SORT_TYPE);
static QO_PLAN *qo_seq_scan_new (QO_INFO *, QO_NODE *);
static QO_PLAN *qo_index_scan_new (QO_INFO *, QO_NODE *, QO_NODE_INDEX_ENTRY *, QO_SCANMETHOD, BITSET *, BITSET *);
static int qo_has_is_not_null_term (QO_NODE * node);

static bool qo_validate_index_term_notnull (QO_ENV * env, QO_INDEX_ENTRY * index_entryp);
static bool qo_validate_index_attr_notnull (QO_ENV * env, QO_INDEX_ENTRY * index_entryp, PT_NODE * col);
static int qo_validate_index_for_orderby (QO_ENV * env, QO_NODE_INDEX_ENTRY * ni_entryp);
static int qo_validate_index_for_groupby (QO_ENV * env, QO_NODE_INDEX_ENTRY * ni_entryp);
static PT_NODE *qo_search_isnull_key_expr (PARSER_CONTEXT * parser, PT_NODE * tree, void *arg, int *continue_walk);
static bool qo_check_orderby_skip_descending (QO_PLAN * plan);
static bool qo_check_groupby_skip_descending (QO_PLAN * plan, PT_NODE * list);
static PT_NODE *qo_plan_compute_iscan_sort_list (QO_PLAN * root, PT_NODE * group_by, bool * is_index_w_prefix);

static int qo_walk_plan_tree (QO_PLAN * plan, QO_WALK_FUNCTION f, void *arg);
static void qo_set_use_desc (QO_PLAN * plan);
static int qo_set_orderby_skip (QO_PLAN * plan, void *arg);
static int qo_validate_indexes_for_orderby (QO_PLAN * plan, void *arg);
static int qo_unset_multi_range_optimization (QO_PLAN * plan, void *arg);
static bool qo_plan_is_orderby_skip_candidate (QO_PLAN * plan);
static bool qo_is_sort_limit (QO_PLAN * plan);

static json_t *qo_plan_scan_print_json (QO_PLAN * plan);
static json_t *qo_plan_sort_print_json (QO_PLAN * plan);
static json_t *qo_plan_join_print_json (QO_PLAN * plan);
static json_t *qo_plan_follow_print_json (QO_PLAN * plan);
static json_t *qo_plan_print_json (QO_PLAN * plan);

static void qo_plan_scan_print_text (FILE * fp, QO_PLAN * plan, int indent);
static void qo_plan_sort_print_text (FILE * fp, QO_PLAN * plan, int indent);
static void qo_plan_join_print_text (FILE * fp, QO_PLAN * plan, int indent);
static void qo_plan_follow_print_text (FILE * fp, QO_PLAN * plan, int indent);
static void qo_plan_print_text (FILE * fp, QO_PLAN * plan, int indent);

static bool qo_index_has_bit_attr (QO_INDEX_ENTRY * index_entryp);

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

/* Structural equivalence classes for expressions */

typedef enum PRED_CLASS
{
  PC_ATTR,
  PC_CONST,
  PC_HOST_VAR,
  PC_SUBQUERY,
  PC_SET,
  PC_OTHER,
  PC_MULTI_ATTR
} PRED_CLASS;

static double qo_or_selectivity (QO_ENV * env, double lhs_sel, double rhs_sel);

static double qo_and_selectivity (QO_ENV * env, double lhs_sel, double rhs_sel);

static double qo_not_selectivity (QO_ENV * env, double sel);

static double qo_equal_selectivity (QO_ENV * env, PT_NODE * pt_expr);

static double qo_comp_selectivity (QO_ENV * env, PT_NODE * pt_expr);

static double qo_between_selectivity (QO_ENV * env, PT_NODE * pt_expr);

static double qo_range_selectivity (QO_ENV * env, PT_NODE * pt_expr);

static double qo_all_some_in_selectivity (QO_ENV * env, PT_NODE * pt_expr);

static PRED_CLASS qo_classify (PT_NODE * attr);

static int qo_index_cardinality (QO_ENV * env, PT_NODE * attr);

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
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (QO_PLAN));
	  return NULL;
	}
    }

  bitset_init (&(plan->sarged_terms), env);
  bitset_init (&(plan->subqueries), env);

  plan->has_sort_limit = false;
  plan->use_iscan_descending = false;

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
  PT_NODE *conj, *saved_next = NULL;

  env = QO_TERM_ENV (term);

  conj = QO_TERM_PT_EXPR (term);
  if (conj)
    {
      saved_next = conj->next;
      conj->next = NULL;
    }

  switch (QO_TERM_CLASS (term))
    {
    case QO_TC_DEP_LINK:
      sprintf (buf, "table(");
      p = buf + strlen (buf);
      separator = "";
      for (i = bitset_iterate (&(QO_NODE_DEP_SET (QO_TERM_TAIL (term))), &bi); i != -1; i = bitset_next_member (&bi))
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
      sprintf (p, "dep-join(%s,%s)", QO_NODE_NAME (QO_TERM_HEAD (term)), QO_NODE_NAME (QO_TERM_TAIL (term)));
      break;

    case QO_TC_DUMMY_JOIN:
      p = buf;
      sprintf (p, "dummy(%s,%s)", QO_NODE_NAME (QO_TERM_HEAD (term)), QO_NODE_NAME (QO_TERM_TAIL (term)));
      break;

    default:
      assert_release (conj != NULL);
      if (conj)
	{
	  PARSER_CONTEXT *parser = QO_ENV_PARSER (QO_TERM_ENV (term));
	  PT_PRINT_VALUE_FUNC saved_func = parser->print_db_value;
	  unsigned int save_custom = parser->custom_print;

	  parser->custom_print |= PT_CONVERT_RANGE;
	  parser->print_db_value = NULL;

	  p = parser_print_tree (parser, conj);

	  parser->custom_print = save_custom;
	  parser->print_db_value = saved_func;
	}
      else
	{
	  p = buf;
	  buf[0] = '\0';
	}
    }

  /* restore link */
  if (conj)
    {
      conj->next = saved_next;
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

  /* When computing the cost for a WORST_PLAN, we'll get in here without a backing info node; just work around it. */
  env = plan->info ? (plan->info)->env : NULL;
  subq_cpu_cost = subq_io_cost = 0.0;

  /* Compute the costs for all of the subqueries. Each of the pinned subqueries is intended to be evaluated once for
   * each row produced by this plan; the cost of each such evaluation in the fixed cost of the subquery plus one trip
   * through the result, i.e.,
   *
   * QO_PLAN_FIXED_COST(subplan) + QO_PLAN_ACCESS_COST(subplan)
   *
   * The cost info for the subplan has (probably) been squirreled away in a QO_SUMMARY structure reachable from the
   * original select node.
   */

  for (i = bitset_iterate (&(plan->subqueries), &iter); i != -1; i = bitset_next_member (&iter))
    {
      subq = env ? &env->subqueries[i] : NULL;
      query = subq ? subq->node : NULL;
      qo_plan_compute_subquery_cost (query, &temp_cpu_cost, &temp_io_cost);
      subq_cpu_cost += temp_cpu_cost;
      subq_io_cost += temp_io_cost;
    }

  /* This computes the specific cost characteristics for each plan. */
  (*(plan->vtbl)->cost_fn) (plan);

  /* Now add in the subquery costs; this cost is incurred for each row produced by this plan, so multiply it by the
   * estimated cardinality and add it to the access cost.
   */
  if (plan->info)
    {
      plan->variable_cpu_cost += (plan->info)->cardinality * subq_cpu_cost;
      plan->variable_io_cost += (plan->info)->cardinality * subq_io_cost;
    }
}

/*
 * qo_plan_compute_subquery_cost () -
 *   return:
 *   subquery(in):
 *   subq_cpu_cost(in):
 *   subq_io_cost(in):
 */
static void
qo_plan_compute_subquery_cost (PT_NODE * subquery, double *subq_cpu_cost, double *subq_io_cost)
{
  QO_SUMMARY *summary;
  double arg1_cpu_cost, arg1_io_cost, arg2_cpu_cost, arg2_io_cost;

  *subq_cpu_cost = *subq_io_cost = 0.0;	/* init */

  if (subquery == NULL)
    {
      /* This case is selected when a subquery wasn't optimized for some reason.
       * just take a guess. ---> NEED MORE CONSIDERATION
       */
      *subq_cpu_cost = 5.0;
      *subq_io_cost = 5.0;
      return;
    }

  switch (subquery->node_type)
    {
    case PT_SELECT:
      summary = (QO_SUMMARY *) subquery->info.query.q.select.qo_summary;
      if (summary)
	{
	  *subq_cpu_cost += summary->fixed_cpu_cost + summary->variable_cpu_cost;
	  *subq_io_cost += summary->fixed_io_cost + summary->variable_io_cost;
	}
      else
	{
	  /* it may be unknown error. just take a guess. ---> NEED MORE CONSIDERATION */
	  *subq_cpu_cost = 5.0;
	  *subq_io_cost = 5.0;
	}

      /* Here, GROUP BY and ORDER BY cost must be considered. ---> NEED MORE CONSIDERATION */
      /* ---> under construction <--- */
      break;

    case PT_UNION:
    case PT_INTERSECTION:
    case PT_DIFFERENCE:
      qo_plan_compute_subquery_cost (subquery->info.query.q.union_.arg1, &arg1_cpu_cost, &arg1_io_cost);
      qo_plan_compute_subquery_cost (subquery->info.query.q.union_.arg2, &arg2_cpu_cost, &arg2_io_cost);

      *subq_cpu_cost = arg1_cpu_cost + arg2_cpu_cost;
      *subq_io_cost = arg1_io_cost + arg2_io_cost;

      /* later, sort cost and result-set scan cost must be considered. ---> NEED MORE CONSIDERATION */
      /* ---> under construction <--- */
      break;

    default:
      /* it is unknown case. just take a guess. ---> NEED MORE CONSIDERATION */
      *subq_cpu_cost = 5.0;
      *subq_io_cost = 5.0;
      break;
    }

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
 * qo_set_use_desc () - sets certain plans' use_descending index flag
 *
 * return: NO_ERROR
 * plan(in):
 *
 * note: the function only cares about index scans and skips other plan types
 */
static void
qo_set_use_desc (QO_PLAN * plan)
{
  switch (plan->plan_type)
    {
    case QO_PLANTYPE_SCAN:
      if (((qo_is_iscan (plan) || qo_is_iscan_from_groupby (plan))
	   && plan->plan_un.scan.index->head->groupby_skip == true)
	  || ((qo_is_iscan (plan) || qo_is_iscan_from_orderby (plan))
	      && plan->plan_un.scan.index->head->orderby_skip == true))
	{
	  plan->plan_un.scan.index->head->use_descending = true;
	}
      break;

    case QO_PLANTYPE_FOLLOW:
      qo_set_use_desc (plan->plan_un.follow.head);
      break;

    case QO_PLANTYPE_JOIN:
      qo_set_use_desc (plan->plan_un.join.outer);
      break;

    default:
      break;
    }
}

/*
 * qo_unset_multi_range_optimization () - set all multi_range_opt flags
 *					  on all indexes to false
 *
 * return    : NO_ERROR
 * plan (in) : current plan
 * arg (in)  : not used
 */
static int
qo_unset_multi_range_optimization (QO_PLAN * plan, void *arg)
{
  if (plan->multi_range_opt_use == PLAN_MULTI_RANGE_OPT_NO)
    {
      /* nothing to do */
      return NO_ERROR;
    }

  /* set multi_range_opt to false */
  plan->multi_range_opt_use = PLAN_MULTI_RANGE_OPT_NO;

  if (qo_is_index_mro_scan (plan))
    {
      QO_INDEX_ENTRY *index_entryp;

      index_entryp = plan->plan_un.scan.index->head;

      /* multi_range_opt may have set the descending order on index */
      if (index_entryp->use_descending)
	{
	  /* if descending order is hinted or if skip order by / skip group by are true, leave the index as descending */
	  if (((plan->info->env->pt_tree->info.query.q.select.hint & PT_HINT_USE_IDX_DESC) == 0)
	      && !index_entryp->groupby_skip && !index_entryp->orderby_skip)
	    {
	      /* set use_descending to false */
	      index_entryp->use_descending = false;
	    }
	}
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
  if (qo_is_iscan (plan) || qo_is_iscan_from_orderby (plan))
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
  if (qo_is_iscan_from_orderby (plan))
    {
      if (!qo_validate_index_for_orderby (plan->info->env, plan->plan_un.scan.index))
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
  PARSER_CONTEXT *parser;

  if (plan == NULL || plan->top_rooted)
    {
      return plan;		/* is already top-level plan - OK */
    }

  if (plan->info == NULL	/* worst plan */
      || (env = (plan->info)->env) == NULL || bitset_cardinality (&((plan->info)->nodes)) < env->Nnodes
      || /* sub-plan */ (tree = QO_ENV_PT_TREE (env)) == NULL
      || (parser = QO_ENV_PARSER (env)) == NULL)
    {
      return plan;		/* do nothing */
    }

  QO_ASSERT (env, tree->node_type == PT_SELECT);

  plan->top_rooted = true;	/* mark as top-level plan */

  if (pt_is_single_tuple (QO_ENV_PARSER (env), tree))
    {				/* one tuple plan */
      return plan;		/* do nothing */
    }

  if (qo_plan_multi_range_opt (plan))
    {
      /* already found out that multi range optimization can be applied on current plan, skip any other checks */
      return plan;
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

      for (t = bitset_iterate (&(plan->sarged_terms), &iter); t != -1; t = bitset_next_member (&iter))
	{
	  term = QO_ENV_TERM (env, t);
	  if (QO_TERM_CLASS (term) == QO_TC_TOTALLY_AFTER_JOIN)
	    {
	      break;		/* found inst_num() */
	    }
	}			/* for (t = ...) */
      found_instnum = (t == -1) ? false : true;

      plan->iscan_sort_list = qo_plan_compute_iscan_sort_list (plan, NULL, &is_index_w_prefix);

      /* GROUP BY */
      /* if we have rollup, we do not skip the group by */
      if (group_by && !group_by->flag.with_rollup)
	{
	  PT_NODE *group_sort_list = NULL;

	  group_sort_list = qo_plan_compute_iscan_sort_list (plan, group_by, &is_index_w_prefix);

	  if (group_sort_list)
	    {
	      if (found_instnum /* && found_grpynum */ )
		{
		  ;		/* give up */
		}
	      else
		{
		  groupby_skip = pt_sort_spec_cover_groupby (parser, group_sort_list, group_by, tree);

		  /* if index plan and can't skip group by, we search that maybe a descending scan can be used. */
		  if (qo_is_interesting_order_scan (plan) && !groupby_skip)
		    {
		      groupby_skip = qo_check_groupby_skip_descending (plan, group_sort_list);

		      if (groupby_skip)
			{
			  plan->use_iscan_descending = true;
			}
		    }
		}

	      parser_free_node (parser, group_sort_list);
	    }

	  if (groupby_skip)
	    {
	      /* if the plan is index_groupby, we validate the plan */
	      if (qo_is_iscan_from_groupby (plan) || qo_is_iscan_from_orderby (plan))
		{
		  if (!qo_validate_index_for_groupby (plan->info->env, plan->plan_un.scan.index))
		    {
		      /* drop the plan if it wasn't validated */
		      qo_worst_cost (plan);
		      return plan;
		    }
		}
	      /* if all goes well, we have an indexed plan with group by skip! */
	      if (plan->plan_type == QO_PLANTYPE_SCAN && plan->plan_un.scan.index)
		{
		  plan->plan_un.scan.index->head->groupby_skip = true;
		}
	    }
	  else
	    {
	      /* if the order by is not skipped we drop the plan because it didn't helped us */
	      if (qo_is_iscan_from_groupby (plan) || qo_is_iscan_from_orderby (plan))
		{
		  qo_worst_cost (plan);
		  return plan;
		}

	      plan = qo_sort_new (plan, QO_UNORDERED, SORT_GROUPBY);
	      assert (plan->iscan_sort_list == NULL);
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
		      /* we already removed covered ORDER BY in reduce_order_by(). so is not covered ordering */
		      ;		/* give up; DO NOT DELETE ME - need future work */
		    }
		  else
		    {		/* non group_by */
		      if (found_instnum && orderby_for)
			{
			  /* at here, we can not merge orderby_num pred with inst_num pred */
			  ;	/* give up; DO NOT DELETE ME - need future work */
			}
		      else if (!is_index_w_prefix && !tree->info.query.q.select.connect_by
			       && !pt_has_analytic (parser, tree))
			{
			  orderby_skip = pt_sort_spec_cover (plan->iscan_sort_list, order_by);

			  /* try using a reverse scan */
			  if (!orderby_skip)
			    {
			      orderby_skip = qo_check_orderby_skip_descending (plan);

			      if (orderby_skip)
				{
				  assert (qo_is_interesting_order_scan (plan) || plan->plan_type == QO_PLANTYPE_JOIN);
				  plan->use_iscan_descending = true;
				}
			    }
			}
		    }
		}
	    }

	  if (orderby_skip)
	    {
	      if (qo_is_iscan_from_groupby (plan))
		{
		  /* group by skipping plan and we have order by skip -> drop */
		  qo_worst_cost (plan);
		  return plan;
		}

	      if (orderby_for)
		{		/* apply inst_num filter */
		  ;		/* DO NOT DELETE ME - need future work */
		}

	      /* validate the index orderby plan or subplans */
	      if (qo_walk_plan_tree (plan, qo_validate_indexes_for_orderby, NULL) != NO_ERROR)
		{
		  /* drop the plan if it wasn't validated */
		  qo_worst_cost (plan);
		  return plan;
		}

	      /* if all goes well, we have an indexed plan with order by skip: set the flag to all suitable subplans */
	      {
		bool yn = true;
		qo_walk_plan_tree (plan, qo_set_orderby_skip, &yn);
	      }
	    }
	  else
	    {
	      /* if the order by is not skipped we drop the plan because it didn't helped us */
	      if (qo_is_iscan_from_orderby (plan))
		{
		  qo_worst_cost (plan);
		  return plan;
		}

	      plan = qo_sort_new (plan, QO_UNORDERED, all_distinct == PT_DISTINCT ? SORT_DISTINCT : SORT_ORDERBY);
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
qo_generic_walk (QO_PLAN * plan, void (*child_fn) (QO_PLAN *, void *), void *child_data,
		 void (*parent_fn) (QO_PLAN *, void *), void *parent_data)
{
  if (parent_fn)
    {
      (*parent_fn) (plan, parent_data);
    }
}

/*
 * qo_plan_print_sort_spec_helper () -
 *   return:
 *   list(in):
 *   f(in):
 *   howfar(in):
 */
static void
qo_plan_print_sort_spec_helper (PT_NODE * list, bool is_iscan_asc, FILE * f, int howfar)
{
  const char *prefix;
  bool is_sort_spec_asc = true;

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

      if (list->info.sort_spec.asc_or_desc == PT_ASC)
	{
	  is_sort_spec_asc = true;
	}
      else
	{
	  is_sort_spec_asc = false;
	}

      fprintf (f, "%d %s", list->info.sort_spec.pos_descr.pos_no, (is_sort_spec_asc == is_iscan_asc) ? "asc" : "desc");

      if (TP_TYPE_HAS_COLLATION (TP_DOMAIN_TYPE (list->info.sort_spec.pos_descr.dom))
	  && TP_DOMAIN_COLLATION (list->info.sort_spec.pos_descr.dom) != LANG_SYS_COLLATION
	  && TP_DOMAIN_COLLATION_FLAG (list->info.sort_spec.pos_descr.dom) != TP_DOMAIN_COLL_LEAVE)
	{
	  fprintf (f, " collate %s",
		   lang_get_collation_name (TP_DOMAIN_COLLATION (list->info.sort_spec.pos_descr.dom)));
	}
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
  bool is_iscan_asc = true;

  if (plan->top_rooted != true)
    {				/* check for top level plan */
      return;
    }

  is_iscan_asc = plan->use_iscan_descending ? false : true;

  qo_plan_print_sort_spec_helper (plan->iscan_sort_list, is_iscan_asc, f, howfar);

  if (plan->plan_type == QO_PLANTYPE_SORT)
    {
      QO_ENV *env;
      PT_NODE *tree;

      env = (plan->info)->env;
      if (env == NULL)
	{
	  assert (false);
	  return;		/* give up */
	}
      tree = QO_ENV_PT_TREE (env);
      if (tree == NULL)
	{
	  assert (false);
	  return;		/* give up */
	}

      if (plan->plan_un.sort.sort_type == SORT_GROUPBY && tree->node_type == PT_SELECT)
	{
	  qo_plan_print_sort_spec_helper (tree->info.query.q.select.group_by, true, f, howfar);
	}

      if ((plan->plan_un.sort.sort_type == SORT_DISTINCT || plan->plan_un.sort.sort_type == SORT_ORDERBY)
	  && PT_IS_QUERY (tree))
	{
	  qo_plan_print_sort_spec_helper (tree->info.query.order_by, true, f, howfar);
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

  fprintf (f, "\n" INDENTED_TITLE_FMT "%.0f card %.0f", (int) howfar, ' ', "cost:", fixed + variable,
	   (plan->info)->cardinality);
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
    {
      return;
    }

  fprintf (f, "\n" INDENTED_TITLE_FMT, (int) howfar, ' ', "segs:");
  for (sx = bitset_iterate (&((plan->info)->projected_segs), &si); sx != -1; sx = bitset_next_member (&si))
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
      qo_termset_fprint ((plan->info)->env, &(plan->plan_un.join.during_join_terms), f);
    }
  if (!bitset_is_empty (&(plan->plan_un.join.after_join_terms)))
    {
      fprintf (f, "\n" INDENTED_TITLE_FMT, (int) howfar, ' ', "after:");
      qo_termset_fprint ((plan->info)->env, &(plan->plan_un.join.after_join_terms), f);
    }
}


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
 * qo_plan_print_analytic_eval () - print evaluation order of analytic
 *				    functions
 *   return:
 *   plan(in):
 *   f(in):
 *   howfar(in):
 */
static void
qo_plan_print_analytic_eval (QO_PLAN * plan, FILE * f, int howfar)
{
  ANALYTIC_EVAL_TYPE *eval;
  ANALYTIC_TYPE *func;
  SORT_LIST *sort;
  int i, j, k;
  char buf[32];

  if (plan->analytic_eval_list != NULL)
    {
      fprintf (f, "\n\nAnalytic functions:");

      /* list functions */
      for (i = 0, k = 0, eval = plan->analytic_eval_list; eval != NULL; eval = eval->next, k++)
	{
	  /* run info */
	  sprintf (buf, "run[%d]: ", k);
	  fprintf (f, "\n" INDENTED_TITLE_FMT, (int) howfar, ' ', buf);
	  fprintf (f, "sort with key (");

	  /* eval sort list */
	  for (sort = eval->sort_list; sort != NULL; sort = sort->next)
	    {
	      fprintf (f, SORT_SPEC_FMT (sort));
	      if (sort->next != NULL)
		{
		  fputs (", ", f);
		}
	    }
	  fputs (")", f);

	  for (func = eval->head; func != NULL; func = func->next, i++)
	    {
	      /* func info */
	      fprintf (f, "\n" INDENTED_TITLE_FMT, (int) howfar, ' ', "");
	      fprintf (f, "func[%d]: ", i);
	      fputs (fcode_get_lowercase_name (func->function), f);

	      /* func partition by */
	      fputs (" partition by (", f);
	      for (sort = eval->sort_list, j = func->sort_prefix_size; sort != NULL && j > 0; sort = sort->next, j--)
		{
		  fprintf (f, SORT_SPEC_FMT (sort));
		  if (sort->next != NULL && j != 1)
		    {
		      fputs (", ", f);
		    }
		}

	      /* func order by */
	      fputs (") order by (", f);
	      for (j = func->sort_list_size - func->sort_prefix_size; sort != NULL && j > 0; sort = sort->next, j--)
		{
		  fprintf (f, SORT_SPEC_FMT (sort));
		  if (sort->next != NULL && j != 1)
		    {
		      fputs (", ", f);
		    }
		}
	      fputs (")", f);
	    }
	}
    }
}

/*
 * qo_scan_new () -
 *   return:
 *   info(in):
 *   node(in):
 *   scan_method(in):
 */
static QO_PLAN *
qo_scan_new (QO_INFO * info, QO_NODE * node, QO_SCANMETHOD scan_method)
{
  QO_PLAN *plan;

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
  plan->analytic_eval_list = NULL;
  plan->plan_type = QO_PLANTYPE_SCAN;
  plan->order = QO_UNORDERED;

  plan->plan_un.scan.scan_method = scan_method;
  plan->plan_un.scan.node = node;

  bitset_assign (&(plan->sarged_terms), &(QO_NODE_SARGS (node)));

  bitset_assign (&(plan->subqueries), &(QO_NODE_SUBQUERIES (node)));
  bitset_init (&(plan->plan_un.scan.terms), info->env);
  bitset_init (&(plan->plan_un.scan.kf_terms), info->env);
  bitset_init (&(plan->plan_un.scan.hash_terms), info->env);
  plan->plan_un.scan.index_equi = false;
  plan->plan_un.scan.index_cover = false;
  plan->plan_un.scan.index_iss = false;
  plan->plan_un.scan.index_loose = false;
  plan->plan_un.scan.index = NULL;

  plan->multi_range_opt_use = PLAN_MULTI_RANGE_OPT_NO;
  bitset_init (&(plan->plan_un.scan.multi_col_range_segs), info->env);

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
  bitset_delset (&(plan->plan_un.scan.hash_terms));
  bitset_delset (&(plan->plan_un.scan.multi_col_range_segs));
}


/*
 * qo_seq_scan_new () -
 *   return:
 *   info(in):
 *   node(in):
 */
static QO_PLAN *
qo_seq_scan_new (QO_INFO * info, QO_NODE * node)
{
  QO_PLAN *plan;

  plan = qo_scan_new (info, node, QO_SCANMETHOD_SEQ_SCAN);
  if (plan == NULL)
    {
      return NULL;
    }

  plan->vtbl = &qo_seq_scan_plan_vtbl;

  assert (bitset_is_empty (&(plan->plan_un.scan.terms)));
  assert (bitset_is_empty (&(plan->plan_un.scan.kf_terms)));
  assert (plan->plan_un.scan.index_equi == false);
  assert (plan->plan_un.scan.index_cover == false);
  assert (plan->plan_un.scan.index_iss == false);
  assert (plan->plan_un.scan.index_loose == false);
  assert (plan->plan_un.scan.index == NULL);

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
      planp->variable_cpu_cost = (double) QO_NODE_NCARD (nodep) * (double) QO_CPU_WEIGHT;
    }
  planp->variable_io_cost = (double) QO_NODE_TCARD (nodep);
}

/*
 * qo_index_has_bit_attr () - temporary function
 *    determines if index has any bit/varbit attributes
 *    return: true/false
 *    index_entyp(in):
 */
static bool
qo_index_has_bit_attr (QO_INDEX_ENTRY * index_entryp)
{
  TP_DOMAIN *domain;
  int col_num = index_entryp->col_num;
  int j;

  for (j = 0; j < col_num; j++)
    {
      domain = index_entryp->constraints->attributes[j]->domain;
      if (TP_DOMAIN_TYPE (domain) == DB_TYPE_BIT || TP_DOMAIN_TYPE (domain) == DB_TYPE_VARBIT)
	{
	  return true;
	}
    }

  return false;
}

/*
 * qo_index_scan_new () -
 *   return:
 *   info(in):
 *   node(in):
 *   ni_entry(in):
 *   scan_method(in):
 *   range_terms(in):
 *   indexable_terms(in):
 */
static QO_PLAN *
qo_index_scan_new (QO_INFO * info, QO_NODE * node, QO_NODE_INDEX_ENTRY * ni_entry, QO_SCANMETHOD scan_method,
		   BITSET * range_terms, BITSET * indexable_terms)
{
  QO_PLAN *plan = NULL;
  BITSET_ITERATOR iter;
  int t = -1;
  QO_ENV *env = info->env;
  QO_INDEX_ENTRY *index_entryp = NULL;
  QO_TERM *term = NULL;
  BITSET index_segs;
  BITSET term_segs;
  BITSET remaining_terms;
  int first_seg;
  bool first_col_present = false;

  assert (ni_entry != NULL);
  assert (ni_entry->head != NULL);

  assert (scan_method == QO_SCANMETHOD_INDEX_SCAN || scan_method == QO_SCANMETHOD_INDEX_ORDERBY_SCAN
	  || scan_method == QO_SCANMETHOD_INDEX_GROUPBY_SCAN || scan_method == QO_SCANMETHOD_INDEX_SCAN_INSPECT);

  assert (scan_method != QO_SCANMETHOD_INDEX_SCAN || !(ni_entry->head->force < 0));
  assert (scan_method == QO_SCANMETHOD_INDEX_SCAN_INSPECT || range_terms != NULL);

  plan = qo_scan_new (info, node, scan_method);
  if (plan == NULL)
    {
      return NULL;
    }

  bitset_init (&index_segs, env);
  bitset_init (&term_segs, env);
  bitset_init (&remaining_terms, env);

  if (range_terms != NULL)
    {
      /* remove key-range terms from sarged terms */
      bitset_difference (&(plan->sarged_terms), range_terms);
    }

  /* remove key-range terms from remaining terms */
  if (indexable_terms != NULL)
    {
      bitset_assign (&remaining_terms, indexable_terms);
      bitset_difference (&remaining_terms, range_terms);
    }
  bitset_union (&remaining_terms, &(plan->sarged_terms));

  /*
   * This is, in essence, the selectivity of the index.  We
   * really need to do a better job of figuring out the cost of
   * an indexed scan.
   */
  plan->vtbl = &qo_index_scan_plan_vtbl;
  plan->plan_un.scan.index = ni_entry;

  index_entryp = (plan->plan_un.scan.index)->head;
  first_seg = index_entryp->seg_idxs[0];

  if (range_terms != NULL)
    {
      /* set key-range terms */
      bitset_assign (&(plan->plan_un.scan.terms), range_terms);
      bitset_assign (&(plan->plan_un.scan.multi_col_range_segs), &(index_entryp->multi_col_range_segs));
      for (t = bitset_iterate (range_terms, &iter); t != -1; t = bitset_next_member (&iter))
	{
	  term = QO_ENV_TERM (env, t);

	  if (first_seg != -1 && BITSET_MEMBER (QO_TERM_SEGS (term), first_seg))
	    {
	      first_col_present = true;
	    }

	  if (!QO_TERM_IS_FLAGED (term, QO_TERM_EQUAL_OP))
	    {
	      break;
	    }
	}
    }

  if (!bitset_is_empty (&(plan->plan_un.scan.terms)) && t == -1)
    {
      /* is all equi-cond key-range terms */
      plan->plan_un.scan.index_equi = true;
    }
  else
    {
      plan->plan_un.scan.index_equi = false;
    }

  if (index_entryp->constraints->func_index_info && index_entryp->cover_segments == false)
    {
      /* do not permit key-filter */
      assert (bitset_is_empty (&(plan->plan_un.scan.kf_terms)));
    }
  else
    {
      /* all segments consisting in key columns */
      for (t = 0; t < index_entryp->nsegs; t++)
	{
	  if ((index_entryp->seg_idxs[t]) != -1)
	    {
	      bitset_add (&index_segs, (index_entryp->seg_idxs[t]));
	    }
	}

      for (t = bitset_iterate (&remaining_terms, &iter); t != -1; t = bitset_next_member (&iter))
	{
	  term = QO_ENV_TERM (env, t);

	  if (QO_TERM_IS_FLAGED (term, QO_TERM_NON_IDX_SARG_COLL))
	    {
	      /* term contains a collation that prevents us from using this term as a key range/filter */
	      continue;
	    }

	  if (!bitset_is_empty (&(QO_TERM_SUBQUERIES (term))))
	    {
	      continue;		/* term contains correlated subquery */
	    }

	  /* check for no key-range index scan */
	  if (bitset_is_empty (&(plan->plan_un.scan.terms)))
	    {
	      if (qo_is_filter_index (index_entryp) || qo_is_iscan_from_orderby (plan)
		  || qo_is_iscan_from_groupby (plan))
		{
		  /* filter index has a pre-defined key-range. ordery/groupby scan already checked nullable terms */
		  ;		/* go ahead */
		}
	      else
		{
		  /* do not permit non-indexable term as key-filter */
		  if (!term->can_use_index)
		    {
		      continue;
		    }
		}
	    }

	  bitset_assign (&term_segs, &(QO_TERM_SEGS (term)));
	  bitset_intersect (&term_segs, &(QO_NODE_SEGS (node)));

	  /* if the term is consisted by only the node's segments which appear in scan terms, it will be key-filter.
	   * otherwise will be data filter
	   */
	  if (!bitset_is_empty (&term_segs))
	    {
	      if (bitset_subset (&index_segs, &term_segs))
		{
		  bitset_add (&(plan->plan_un.scan.kf_terms), t);
		}
	    }
	}

      /* exclude key filter terms from sargs terms */
      bitset_difference (&(plan->sarged_terms), &(plan->plan_un.scan.kf_terms));
      bitset_difference (&remaining_terms, &(plan->plan_un.scan.kf_terms));
    }

  /* check for index cover scan */
  plan->plan_un.scan.index_cover = false;	/* init */
  if (index_entryp->cover_segments)
    {
      /* do not consider prefix index */
      if (qo_is_prefix_index (index_entryp) == false)
	{
	  for (t = bitset_iterate (&remaining_terms, &iter); t != -1; t = bitset_next_member (&iter))
	    {
	      term = QO_ENV_TERM (env, t);

	      if (!bitset_is_empty (&(QO_TERM_SUBQUERIES (term))))
		{
		  /* term contains correlated subquery */
		  continue;
		}

	      break;		/* found data-filter */
	    }

	  if (t == -1)
	    {
	      /* not found data-filter; mark as covering index scan */
	      plan->plan_un.scan.index_cover = true;
	    }
	}
    }

  assert (!bitset_intersects (&(plan->plan_un.scan.terms), &(plan->plan_un.scan.kf_terms)));

  assert (!bitset_intersects (&(plan->plan_un.scan.terms), &(plan->sarged_terms)));
  assert (!bitset_intersects (&(plan->plan_un.scan.kf_terms), &(plan->sarged_terms)));

  assert (!bitset_intersects (&(plan->plan_un.scan.terms), &remaining_terms));
  assert (!bitset_intersects (&(plan->plan_un.scan.kf_terms), &remaining_terms));

  bitset_delset (&remaining_terms);
  bitset_delset (&term_segs);
  bitset_delset (&index_segs);

  /* check for index skip scan */
  plan->plan_un.scan.index_iss = false;	/* init */
  if (index_entryp->is_iss_candidate)
    {
      assert (!bitset_is_empty (&(plan->plan_un.scan.terms)));
      assert (index_entryp->ils_prefix_len == 0);
      assert (!qo_is_filter_index (index_entryp));

      if (first_col_present == false)
	{
	  plan->plan_un.scan.index_iss = true;
	}
    }

  /* check for loose index scan */
  plan->plan_un.scan.index_loose = false;	/* init */
  if (index_entryp->ils_prefix_len > 0)
    {
      assert (plan->plan_un.scan.index_iss == false);

      /* do not consider prefix index */
      if (qo_is_prefix_index (index_entryp) == false)
	{
	  if (scan_method == QO_SCANMETHOD_INDEX_SCAN && qo_is_index_covering_scan (plan)
	      && !qo_index_has_bit_attr (index_entryp))
	    {
	      /* covering index, no key-range, no data-filter; mark as loose index scan */
	      plan->plan_un.scan.index_loose = true;
	    }

	  /* keep out not good index scan */
	  if (!qo_is_index_loose_scan (plan))
	    {
	      /* check for no key-range, no key-filter index scan */
	      if (qo_is_iscan (plan) && bitset_is_empty (&(plan->plan_un.scan.terms))
		  && bitset_is_empty (&(plan->plan_un.scan.kf_terms)))
		{
		  assert (!qo_is_iscan_from_groupby (plan));
		  assert (!qo_is_iscan_from_orderby (plan));

		  /* is not good index scan */
		  qo_plan_release (plan);
		  return NULL;
		}
	    }
	}
    }

  /* check for no key-range, no key-filter index scan */
  if (qo_is_iscan (plan) && bitset_is_empty (&(plan->plan_un.scan.terms))
      && bitset_is_empty (&(plan->plan_un.scan.kf_terms)) && scan_method != QO_SCANMETHOD_INDEX_SCAN_INSPECT)
    {
      assert (!qo_is_iscan_from_groupby (plan));
      assert (!qo_is_iscan_from_orderby (plan));

      /* check for filter-index, loose index scan */
      if (qo_is_filter_index (index_entryp) || qo_is_index_loose_scan (plan))
	{
	  /* filter index has a pre-defined key-range. */
	  assert (bitset_is_empty (&(plan->plan_un.scan.terms)));

	  ;			/* go ahead */
	}
      else
	{
	  assert (false);
	  qo_plan_release (plan);
	  return NULL;
	}
    }

  if (qo_check_iscan_for_multi_range_opt (plan))
    {
      bool dummy;

      plan->multi_range_opt_use = PLAN_MULTI_RANGE_OPT_USE;
      plan->iscan_sort_list = qo_plan_compute_iscan_sort_list (plan, NULL, &dummy);
    }

  assert (plan->plan_un.scan.index != NULL);

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
  bool is_null_sel;
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
      assert (false);
      qo_worst_cost (planp);
      return;
    }
  else if (index_entryp->force > 0)
    {
      qo_zero_cost (planp);
      return;
    }

  n = index_entryp->col_num;
  if (SM_IS_CONSTRAINT_UNIQUE_FAMILY (index_entryp->constraints->type) && n == index_entryp->nsegs)
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
	  /* When the index is a unique family and all of index columns are specified in the equal conditions, the
	   * cardinality of the scan will 0 or 1. In this case we will make the scan cost to zero, thus to force the
	   * optimizer to select this scan.
	   */
	  qo_zero_cost (planp);

	  index_entryp->all_unique_index_columns_are_equi_terms = true;
	  return;
	}
    }

  /* selectivity of the index terms */
  sel = 1.0;
  is_null_sel = false;

  pkeys_num = MIN (n, cum_statsp->pkeys_size);
  assert (pkeys_num <= BTREE_STATS_PKEYS_NUM);

  if (bitset_is_empty (&(planp->plan_un.scan.terms)))
    {
      assert (!qo_is_index_iss_scan (planp));
    }

  sel_limit = 0.0;		/* init */

  /* set selectivity limit */
  if (pkeys_num > 0 && cum_statsp->pkeys[0] > 1)
    {
      sel_limit = 1.0 / (double) cum_statsp->pkeys[0];
    }
  else
    {
      /* can not use btree partial-key statistics */
      if (cum_statsp->keys > 1)
	{
	  sel_limit = 1.0 / (double) cum_statsp->keys;
	}
      else
	{
	  if (QO_NODE_NCARD (nodep) == 0)
	    {			/* empty class */
	      sel = 0.0;
	      is_null_sel = true;
	    }
	  else if (QO_NODE_NCARD (nodep) > 1)
	    {
	      sel_limit = 1.0 / (double) QO_NODE_NCARD (nodep);
	    }
	}
    }

  assert (sel_limit < 1.0);

  /* check lower bound */
  if (is_null_sel == false)
    {
      sel = MAX (sel, sel_limit);
    }

  assert ((is_null_sel == false && sel == 1.0) || (is_null_sel == true && sel == 0.0));

  if (!is_null_sel)
    {
      assert (QO_NODE_NCARD (nodep) > 0);
      i = 0;

      for (t = bitset_iterate (&(planp->plan_un.scan.terms), &iter); t != -1; t = bitset_next_member (&iter))
	{
	  termp = QO_ENV_TERM (QO_NODE_ENV (nodep), t);
	  sel *= QO_TERM_SELECTIVITY (termp);

	  /* each term can have multi index column. e.g.) (a,b) in .. */
	  for (int j = 0; j < index_entryp->col_num; j++)
	    {
	      if (BITSET_MEMBER (QO_TERM_SEGS (termp), index_entryp->seg_idxs[j]))
		{
		  i++;
		}
	    }
	}
      /* check upper bound */
      sel = MIN (sel, 1.0);

      sel_limit = 0.0;		/* init */

      /* set selectivity limit */
      if (i < pkeys_num && cum_statsp->pkeys[i] > 1)
	{
	  sel_limit = 1.0 / (double) cum_statsp->pkeys[i];
	}
      else
	{			/* can not use btree partial-key statistics */
	  if (cum_statsp->keys > 1)
	    {
	      sel_limit = 1.0 / (double) cum_statsp->keys;
	    }
	  else
	    {
	      if (QO_NODE_NCARD (nodep) > 1)
		{
		  sel_limit = 1.0 / (double) QO_NODE_NCARD (nodep);
		}
	    }
	}

      assert (sel_limit < 1.0);

      /* check lower bound */
      sel = MAX (sel, sel_limit);
    }

  assert ((is_null_sel == false) || (is_null_sel == true && sel == 0.0));

  /* number of objects to be selected */
  objects = sel * (double) QO_NODE_NCARD (nodep);
  /* height of the B+tree */
  height = (double) cum_statsp->height - 1;
  if (height < 0)
    {
      height = 0;
    }
  /* number of leaf pages to be accessed */
  leaves = ceil (sel * (double) cum_statsp->leafs);
  /* total number of pages occupied by objects */
  opages = (double) QO_NODE_TCARD (nodep);
  /* I/O cost to access B+tree index */
  index_IO = ((ni_entryp)->n * height) + leaves;

  /* Index Skip Scan adds to the index IO cost the K extra BTREE searches it does to fetch the next value for the
   * following BTRangeScan
   */
  if (qo_is_index_iss_scan (planp))
    {
      if (pkeys_num > 0)
	{
	  assert (cum_statsp->pkeys != NULL);
	  assert (cum_statsp->pkeys_size != 0);

	  /* The btree is scanned an additional K times */
	  index_IO += cum_statsp->pkeys[0] * ((ni_entryp)->n * height);

	  /* K leaves are additionally read */
	  index_IO += cum_statsp->pkeys[0];
	}
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
      /* p = ((1.0 - 0.6) / (0.8 - 0.3)) (sel - 0.3) + 0.6 = 0.8 sel + 0.36 */
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
  else if ((double) prm_get_integer_value (PRM_ID_PB_NBUFFERS) - index_IO < object_IO)
    {
      object_IO =
	objects * (1.0 - (((double) prm_get_integer_value (PRM_ID_PB_NBUFFERS) - index_IO) / (double) opages));
    }

  if (sel < 1.0)
    {				/* is not Full-Range sel */
      object_IO = ceil (FUDGE_FACTOR * object_IO);
    }
  object_IO = MAX (1.0, object_IO);

  /* index scan requires more CPU cost than sequential scan */

  planp->fixed_cpu_cost = 0.0;
  planp->fixed_io_cost = index_IO;
  planp->variable_cpu_cost = objects * (double) QO_CPU_WEIGHT *ISCAN_OVERHEAD_FACTOR;
  planp->variable_io_cost = object_IO;

  /* one page heap file; reconfig iscan cost */
  if (QO_NODE_TCARD (nodep) <= 1)
    {
      if (QO_NODE_NCARD (nodep) > 1)
	{			/* index scan is worth */
	  planp->fixed_io_cost = 0.0;
	}
    }
}


static void
qo_scan_fprint (QO_PLAN * plan, FILE * f, int howfar)
{
  bool natural_desc_index = false;

  if (plan->plan_un.scan.node->entity_spec->info.spec.cte_pointer)
    {
      PT_NODE *spec = plan->plan_un.scan.node->entity_spec;
      if (spec->info.spec.cte_pointer->info.pointer.node->info.cte.recursive_part)
	{
	  fprintf (f, "\n" INDENTED_TITLE_FMT, (int) howfar, ' ', "recursive CTE: ");
	}
      else
	{
	  fprintf (f, "\n" INDENTED_TITLE_FMT, (int) howfar, ' ', "simple CTE:");
	}
    }
  else
    {
      fprintf (f, "\n" INDENTED_TITLE_FMT, (int) howfar, ' ', "class:");
    }

  qo_node_fprint (plan->plan_un.scan.node, f);

  if (qo_is_interesting_order_scan (plan))
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
	  fprintf (f, "keylimit %s ", parser_print_tree_list (parser, saved_next ? saved_next : key_limit));
	  parser->print_db_value = saved_func;
	  if (saved_next)
	    {
	      key_limit->next = saved_next;
	      saved_next->next = NULL;
	    }
	}

      qo_termset_fprint ((plan->info)->env, &plan->plan_un.scan.terms, f);

      /* print index covering */
      if (qo_is_index_covering_scan (plan))
	{
	  if (bitset_cardinality (&(plan->plan_un.scan.terms)) > 0)
	    {
	      fprintf (f, " ");
	    }
	  fprintf (f, "(covers)");
	}

      if (qo_is_index_iss_scan (plan))
	{
	  fprintf (f, " (index skip scan)");
	}

      if (qo_is_index_loose_scan (plan))
	{
	  fprintf (f, " (loose index scan on prefix %d)", plan->plan_un.scan.index->head->ils_prefix_len);
	}

      if (qo_plan_multi_range_opt (plan))
	{
	  fprintf (f, " (multi_range_opt)");
	}

      if (plan->plan_un.scan.index && plan->plan_un.scan.index->head->use_descending)
	{
	  fprintf (f, " (desc_index)");
	  natural_desc_index = true;
	}

      if (!natural_desc_index && (QO_ENV_PT_TREE (plan->info->env)->info.query.q.select.hint & PT_HINT_USE_IDX_DESC))
	{
	  fprintf (f, " (desc_index forced)");
	}

      if (!bitset_is_empty (&(plan->plan_un.scan.kf_terms)))
	{
	  fprintf (f, "\n" INDENTED_TITLE_FMT, (int) howfar, ' ', "filtr: ");
	  qo_termset_fprint ((plan->info)->env, &(plan->plan_un.scan.kf_terms), f);
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
    {
      fprintf (f, "%s", (name ? name : "(unknown)"));
    }
  else
    {
      fprintf (f, "as %s", (name ? name : "(unknown)"));
    }

  if (qo_is_iscan (plan) || qo_is_iscan_from_orderby (plan))
    {
      BITSET_ITERATOR bi;
      QO_ENV *env;
      int i;
      const char *separator;
      bool natural_desc_index = false;

      env = (plan->info)->env;
      separator = ", ";

      fprintf (f, "%s%s", separator, plan->plan_un.scan.index->head->constraints->name);

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
	  fprintf (f, "(keylimit %s) ", parser_print_tree_list (parser, saved_next ? saved_next : key_limit));
	  parser->print_db_value = saved_func;
	  if (saved_next)
	    {
	      key_limit->next = saved_next;
	      saved_next->next = NULL;
	    }
	}

      for (i = bitset_iterate (&(plan->plan_un.scan.terms), &bi); i != -1; i = bitset_next_member (&bi))
	{
	  fprintf (f, "%s%s", separator, qo_term_string (QO_ENV_TERM (env, i)));
	  separator = " and ";
	}
      if (bitset_cardinality (&(plan->plan_un.scan.kf_terms)) > 0)
	{
	  separator = ", [";
	  for (i = bitset_iterate (&(plan->plan_un.scan.kf_terms), &bi); i != -1; i = bitset_next_member (&bi))
	    {
	      fprintf (f, "%s%s", separator, qo_term_string (QO_ENV_TERM (env, i)));
	      separator = " and ";
	    }
	  fprintf (f, "]");
	}

      /* print index covering */
      if (qo_is_index_covering_scan (plan))
	{
	  fprintf (f, " (covers)");
	}

      if (qo_is_index_iss_scan (plan))
	{
	  fprintf (f, " (index skip scan)");
	}

      if (qo_is_index_loose_scan (plan))
	{
	  fprintf (f, " (loose index scan on prefix %d)", plan->plan_un.scan.index->head->ils_prefix_len);
	}

      if (qo_plan_multi_range_opt (plan))
	{
	  fprintf (f, " (multi_range_opt)");
	}

      if (plan->plan_un.scan.index && plan->plan_un.scan.index->head->use_descending)
	{
	  fprintf (f, " (desc_index)");
	  natural_desc_index = true;
	}

      if (!natural_desc_index && (QO_ENV_PT_TREE (plan->info->env)->info.query.q.select.hint & PT_HINT_USE_IDX_DESC))
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
      for (; subplan && subplan->plan_type == QO_PLANTYPE_SORT && subplan->plan_un.sort.sort_type != SORT_LIMIT;
	   subplan = subplan->plan_un.sort.subplan)
	{
	  if (subplan->top_rooted && subplan->plan_un.sort.sort_type != SORT_TEMP)
	    {
	      ;			/* skip and go ahead */
	    }
	  else
	    {
	      break;		/* is not top-level sort plan */
	    }
	}

      /* check for dummy sort plan */
      if (order == QO_UNORDERED && subplan != NULL && subplan->plan_type == QO_PLANTYPE_SORT)
	{
	  return qo_plan_add_ref (root);
	}

      /* skip out empty sort plan */
      for (; subplan && subplan->plan_type == QO_PLANTYPE_SORT && subplan->plan_un.sort.sort_type != SORT_LIMIT;
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
  plan->analytic_eval_list = NULL;
  plan->order = order;
  plan->plan_type = QO_PLANTYPE_SORT;
  plan->vtbl = &qo_sort_plan_vtbl;

  plan->plan_un.sort.sort_type = sort_type;
  plan->plan_un.sort.subplan = qo_plan_add_ref (subplan);
  plan->plan_un.sort.xasl = NULL;	/* To be determined later */

  plan->multi_range_opt_use = PLAN_MULTI_RANGE_OPT_NO;
  plan->has_sort_limit = (sort_type == SORT_LIMIT);

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
qo_sort_walk (QO_PLAN * plan, void (*child_fn) (QO_PLAN *, void *), void *child_data,
	      void (*parent_fn) (QO_PLAN *, void *), void *parent_data)
{
  if (child_fn)
    {
      (*child_fn) (plan->plan_un.sort.subplan, child_data);
    }
  if (parent_fn)
    {
      (*parent_fn) (plan, parent_data);
    }
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

    case SORT_LIMIT:
      fprintf (f, "(sort limit)");
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
	  fprintf (f, "\n%*c%s(", (int) howfar, ' ', (plan->vtbl)->info_string);
	  qo_eqclass_fprint_wrt (plan->order, &(plan->info->nodes), f);
	  fprintf (f, ")");
#endif
	}
      break;
    case SORT_LIMIT:
      fprintf (f, "\n%*c%s(%s)", (int) howfar, ' ', (plan->vtbl)->info_string, "sort limit");
      howfar += INDENT_INCR;
      break;
    case SORT_GROUPBY:
      fprintf (f, "\n%*c%s(%s)", (int) howfar, ' ', (plan->vtbl)->info_string, "group by");
      howfar += INDENT_INCR;
      break;

    case SORT_ORDERBY:
      fprintf (f, "\n%*c%s(%s)", (int) howfar, ' ', (plan->vtbl)->info_string, "order by");
      howfar += INDENT_INCR;
      break;

    case SORT_DISTINCT:
      fprintf (f, "\n%*c%s(%s)", (int) howfar, ' ', (plan->vtbl)->info_string, "distinct");
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

  /* for worst cost */
  if (subplanp->fixed_cpu_cost == QO_INFINITY || subplanp->fixed_io_cost == QO_INFINITY
      || subplanp->variable_cpu_cost == QO_INFINITY || subplanp->variable_io_cost == QO_INFINITY)
    {
      qo_worst_cost (planp);
      return;
    }

  if (subplanp->plan_type == QO_PLANTYPE_SORT && planp->plan_un.sort.sort_type == SORT_TEMP)
    {
      /* This plan won't actually incur any runtime cost because it won't actually exist (its sort spec will supersede
       * the sort spec of the subplan).  We can't just clobber the sort spec on the lower plan because it might be
       * shared by others.
       */
      planp->fixed_cpu_cost = subplanp->fixed_cpu_cost;
      planp->fixed_io_cost = subplanp->fixed_io_cost;
      planp->variable_cpu_cost = subplanp->variable_cpu_cost;
      planp->variable_io_cost = subplanp->variable_io_cost;
    }
  else if (planp->plan_un.sort.sort_type == SORT_LIMIT)
    {
      if (subplanp->plan_type == QO_PLANTYPE_SORT)
	{
	  /* No sense in having a STOP plan above a SORT plan */
	  qo_worst_cost (planp);
	}

      /* SORT-LIMIT plan has the same cost as the subplan (since actually sorting items in memory is not a big
       * drawback. Costs improvements will be applied when we consider joining this plan with other plans
       */
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
	{
	  pages = 1.0;
	}

      /* The cost (in io's) of just setting up a list file.  This is mostly to discourage the optimizer from choosing
       * merge join for joins of little classes.
       */
      planp->fixed_cpu_cost = subplanp->fixed_cpu_cost + subplanp->variable_cpu_cost + TEMP_SETUP_COST;
      planp->fixed_io_cost = subplanp->fixed_io_cost + subplanp->variable_io_cost;
      planp->variable_cpu_cost = objects * (double) QO_CPU_WEIGHT;
      planp->variable_io_cost = pages;

      if (order != QO_UNORDERED && order != subplanp->order)
	{
	  double sort_io, tcard;

	  sort_io = 0.0;	/* init */

	  if (objects > 1.0)
	    {
	      if (pages < (double) prm_get_integer_value (PRM_ID_SR_NBUFFERS))
		{
		  /* We can sort the result in memory without any additional io costs. Assume cpu costs are n*log(n) in
		   * number of recors.
		   */
		  sort_io = (double) QO_CPU_WEIGHT *objects * log2 (objects);
		}
	      else
		{
		  /* There are too many records to permit an in-memory sort, so io costs will be increased.  Assume
		   * that the io costs increase by the number of pages required to hold the intermediate result.  CPU
		   * costs increase as above. Model courtesy of Ender.
		   */
		  sort_io = pages * log3 (pages / 4.0);

		  /* guess: apply IO caching for big size sort list. Disk IO cost cannot be greater than the 10% number
		   * of the requested IO pages
		   */
		  if (subplanp->plan_type == QO_PLANTYPE_SCAN)
		    {
		      tcard = (double) QO_NODE_TCARD (subplanp->plan_un.scan.node);
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
}

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
qo_join_new (QO_INFO * info, JOIN_TYPE join_type, QO_JOINMETHOD join_method, QO_PLAN * outer, QO_PLAN * inner,
	     BITSET * join_terms, BITSET * duj_terms, BITSET * afj_terms, BITSET * sarged_terms,
	     BITSET * pinned_subqueries, BITSET * hash_terms)
{
  QO_PLAN *plan = NULL;
  QO_NODE *node = NULL;
  PT_NODE *spec = NULL;
  BITSET sarg_out_terms;

  bitset_init (&sarg_out_terms, info->env);

  if (inner->has_sort_limit && join_method != QO_JOINMETHOD_MERGE_JOIN)
    {
      /* SORT-LIMIT plans are allowed on inner nodes only for merge joins */
      return NULL;
    }

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
  plan->analytic_eval_list = NULL;
  plan->plan_type = QO_PLANTYPE_JOIN;
  plan->multi_range_opt_use = PLAN_MULTI_RANGE_OPT_NO;
  plan->has_sort_limit = (outer->has_sort_limit || inner->has_sort_limit);

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

      /* These checks are necessary because of restrictions in the current XASL implementation of nested loop joins.
       * Never put anything on the inner plan that isn't file-based (i.e., a scan of either a heap file or a list
       * file).
       */
      if (!VALID_INNER (inner))
	{
	  inner = qo_sort_new (inner, inner->order, SORT_TEMP);
	}
      else if (IS_OUTER_JOIN_TYPE (join_type))
	{
	  /* for outer join, if inner plan is a scan of classes in hierarchy */
	  if (inner->plan_type == QO_PLANTYPE_SCAN && QO_NODE_IS_CLASS_HIERARCHY (inner->plan_un.scan.node))
	    {
	      inner = qo_sort_new (inner, inner->order, SORT_TEMP);
	    }
	}

      break;

    case QO_JOINMETHOD_MERGE_JOIN:

      plan->vtbl = &qo_merge_join_plan_vtbl;
#if 0
      /* Don't do this anymore; it relies on symmetry, which definitely doesn't apply anymore with the advent of outer
       * joins.
       */

      /* Arrange to always put the smallest cardinality on the outer term; this may lead to some savings given the
       * current merge join implementation.
       */
      if ((inner->info)->cardinality < (outer->info)->cardinality)
	{
	  QO_PLAN *tmp;
	  tmp = inner;
	  inner = outer;
	  outer = tmp;
	}
#endif

      /* The merge join result has the same nominal order as the two subjoins that feed it.  However, if it happens
       * that none of the segments in that order are to be projected from the result, the result is effectively
       * *unordered*.  Check for that condition here.
       */
      plan->order =
	bitset_intersects (&(QO_EQCLASS_SEGS (outer->order)),
			   &((plan->info)->projected_segs)) ? outer->order : QO_UNORDERED;

      /* The current implementation of merge joins always produces a list file These two checks are necessary because
       * of restrictions in the current XASL implementation of merge joins.
       */
      if (outer->plan_type != QO_PLANTYPE_SORT)
	{
	  outer = qo_sort_new (outer, outer->order, SORT_TEMP);
	}
      if (inner->plan_type != QO_PLANTYPE_SORT)
	{
	  inner = qo_sort_new (inner, inner->order, SORT_TEMP);
	}

      break;
    }

  assert (inner != NULL && outer != NULL);
  if (inner == NULL || outer == NULL)
    {
      return NULL;
    }

  node = QO_ENV_NODE (info->env, bitset_first_member (&((inner->info)->nodes)));

  assert (node != NULL);
  if (node == NULL)
    {
      return NULL;
    }

  /* check for cselect of method */
  spec = QO_NODE_ENTITY_SPEC (node);
  if (spec && spec->info.spec.flat_entity_list == NULL && spec->info.spec.derived_table_type == PT_IS_CSELECT)
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
  bitset_init (&(plan->plan_un.join.hash_terms), info->env);

  /* set join terms */
  bitset_assign (&(plan->plan_un.join.join_terms), join_terms);
  /* set hash terms */
  bitset_assign (&(plan->plan_un.join.hash_terms), hash_terms);
  /* add to out terms */
  bitset_union (&sarg_out_terms, &(plan->plan_un.join.join_terms));

  if (IS_OUTER_JOIN_TYPE (join_type))
    {
      /* set during join terms */
      bitset_assign (&(plan->plan_un.join.during_join_terms), duj_terms);
      bitset_difference (&(plan->plan_un.join.during_join_terms), &sarg_out_terms);
      /* add to out terms */
      bitset_union (&sarg_out_terms, &(plan->plan_un.join.during_join_terms));

      /* set after join terms */
      bitset_assign (&(plan->plan_un.join.after_join_terms), afj_terms);
      bitset_difference (&(plan->plan_un.join.after_join_terms), &sarg_out_terms);
      /* add to out terms */
      bitset_union (&sarg_out_terms, &(plan->plan_un.join.after_join_terms));
    }

  /* set plan's sarged terms */
  bitset_assign (&(plan->sarged_terms), sarged_terms);
  bitset_difference (&(plan->sarged_terms), &sarg_out_terms);

  /* Make sure that the pinned subqueries and the sargs are placed on the same node: by now the pinned subqueries are
   * very likely pinned here precisely because they're used by these sargs. Separating them (so that they get evaluated
   * in some different order) will yield incorrect results.
   */
  bitset_assign (&(plan->subqueries), pinned_subqueries);

  if (qo_check_join_for_multi_range_opt (plan))
    {
      bool dummy;

      plan->multi_range_opt_use = PLAN_MULTI_RANGE_OPT_USE;
      plan->iscan_sort_list = qo_plan_compute_iscan_sort_list (plan, NULL, &dummy);
    }

  qo_plan_compute_cost (plan);

  if (QO_ENV_USE_SORT_LIMIT (info->env) && !plan->has_sort_limit
      && bitset_is_equivalent (&info->env->sort_limit_nodes, &info->nodes))
    {
      /* Consider creating a SORT_LIMIT plan over this plan only if it cannot skip order by. Since we know that we
       * already have all ORDER BY nodes in this plan, we can verify orderby_skip at this point
       */
      if (!qo_plan_is_orderby_skip_candidate (plan))
	{
	  plan = qo_sort_new (plan, QO_UNORDERED, SORT_LIMIT);
	  if (plan == NULL)
	    {
	      return NULL;
	    }
	}
    }

#if 1				/* MERGE_ALWAYS_MAKES_LISTFILE */
  /* This is necessary to get the proper cost model for merge joins, which always build their result into a listfile
   * right now.  At the moment the cost model for a merge plan just models the cost of producing the result tuples, but
   * not storing them into a listfile. We could push the cost into the merge plan itself, I suppose, but a rational
   * implementation wouldn't impose this cost, and so I have hope that one day we'll be able to eliminate it.
   */
  if (join_method == QO_JOINMETHOD_MERGE_JOIN)
    {
      plan = qo_sort_new (plan, plan->order, SORT_TEMP);
    }
#endif /* MERGE_ALWAYS_MAKES_LISTFILE */

  bitset_delset (&sarg_out_terms);

  plan = qo_top_plan_new (plan);

  return plan;
}

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
}

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
qo_join_walk (QO_PLAN * plan, void (*child_fn) (QO_PLAN *, void *), void *child_data,
	      void (*parent_fn) (QO_PLAN *, void *), void *parent_data)
{
  if (child_fn)
    {
      (*child_fn) (plan->plan_un.join.outer, child_data);
      (*child_fn) (plan->plan_un.join.inner, child_data);
    }
  if (parent_fn)
    {
      (*parent_fn) (plan, parent_data);
    }
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
	{
	  fputs (" (inner join)", f);
	}
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
    case JOIN_OUTER:		/* not used */
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
      qo_termset_fprint ((plan->info)->env, &(plan->plan_un.join.join_terms), f);
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
      for (i = bitset_iterate (&(plan->plan_un.join.join_terms), &bi); i != -1; i = bitset_next_member (&bi))
	{
	  fprintf (f, "%s%s", separator, qo_term_string (QO_ENV_TERM (env, i)));
	  separator = " and ";
	}
      fprintf (f, ")");
    }
  else
    {
      fprintf (f, "\n%*cNested loops", (int) howfar, ' ');
    }

  if (plan->plan_un.join.join_type == JOIN_LEFT)
    {
      fprintf (f, ": left outer");
    }
  else if (plan->plan_un.join.join_type == JOIN_RIGHT)
    {
      fprintf (f, ": right outer");
    }

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

  /* for worst cost */
  if (inner->fixed_cpu_cost == QO_INFINITY || inner->fixed_io_cost == QO_INFINITY
      || inner->variable_cpu_cost == QO_INFINITY || inner->variable_io_cost == QO_INFINITY)
    {
      qo_worst_cost (planp);
      return;
    }

  outer = planp->plan_un.join.outer;

  /* for worst cost */
  if (outer->fixed_cpu_cost == QO_INFINITY || outer->fixed_io_cost == QO_INFINITY
      || outer->variable_cpu_cost == QO_INFINITY || outer->variable_io_cost == QO_INFINITY)
    {
      qo_worst_cost (planp);
      return;
    }

  /* CPU and IO costs which are fixed againt join */
  planp->fixed_cpu_cost = outer->fixed_cpu_cost + inner->fixed_cpu_cost;
  planp->fixed_io_cost = outer->fixed_io_cost + inner->fixed_io_cost;

  /* inner side CPU cost of nested-loop block join */
  if (outer->plan_type == QO_PLANTYPE_SORT && outer->plan_un.sort.sort_type == SORT_LIMIT)
    {
      /* cardinality of a SORT_LIMIT plan is given by the value of the query limit */
      guessed_result_cardinality = (double) db_get_bigint (&QO_ENV_LIMIT_VALUE (outer->info->env));
    }
  else
    {
      guessed_result_cardinality = (outer->info)->cardinality;
    }
  inner_cpu_cost = guessed_result_cardinality * (double) QO_CPU_WEIGHT;
  /* join cost */

  if (qo_is_iscan (inner) && inner->plan_un.scan.index_equi == true)
    {
      /* correlated index equi-join */
      inner_cpu_cost += inner->variable_cpu_cost;
      if (qo_is_seq_scan (outer) && prm_get_integer_value (PRM_ID_MAX_OUTER_CARD_OF_IDXJOIN) != 0
	  && guessed_result_cardinality > prm_get_integer_value (PRM_ID_MAX_OUTER_CARD_OF_IDXJOIN))
	{
	  planp->variable_cpu_cost = QO_INFINITY;
	  planp->variable_io_cost = QO_INFINITY;
	  return;
	}
    }
  else
    {
      /* neither correlated index join nor equi-join */
      inner_cpu_cost += MAX (1.0, (outer->info)->cardinality) * inner->variable_cpu_cost;
    }

  /* inner side IO cost of nested-loop block join */
  inner_io_cost = outer->variable_io_cost * inner->variable_io_cost;	/* assume IO as # blocks */
  if (inner->plan_type == QO_PLANTYPE_SCAN)
    {
      pages = QO_NODE_TCARD (inner->plan_un.scan.node);
      if (inner_io_cost > pages * 2)
	{
	  /* inner IO cost cannot be greater than two times of the number of pages of the class because buffering */
	  inner_io_cost = pages * 2;

	  /* for iscan of inner, reconfig inner_io_cost */
	  inner_io_cost -= inner->fixed_io_cost;
	  inner_io_cost = MAX (0.0, inner_io_cost);

	}

      if (qo_is_seq_scan (outer) && qo_is_seq_scan (inner))
	{
	  if ((outer->info)->cardinality == (inner->info)->cardinality)
	    {
	      pages2 = QO_NODE_TCARD (outer->plan_un.scan.node);
	      /* exclude too many heavy-sequential scan nl-join - sscan (small) + sscan (big) */
	      if (pages > pages2)
		{
		  io_cost = (inner->variable_io_cost + MIN (inner_io_cost, pages2 * 2));
		  diff_cost = io_cost - (outer->variable_io_cost + inner_io_cost);
		  if (diff_cost > 0)
		    {
		      inner_io_cost += diff_cost + 0.1;
		    }
		}
	    }
	}

      if (planp->plan_un.join.join_type != JOIN_INNER)
	{
	  /* outer join leads nongrouped scan overhead */
	  inner_cpu_cost += ((outer->info)->cardinality * pages * NONGROUPED_SCAN_COST);
	}
    }

  if (inner->plan_type == QO_PLANTYPE_SORT)
    {
      /* (inner->plan_un.sort.subplan)->info == inner->info */
      pages = ((inner->info)->cardinality * (inner->info)->projected_size) / IO_PAGESIZE;
      if (pages < 1)
	pages = 1;

      pages2 = pages * 2;
      if (inner_io_cost > pages2)
	{
	  diff_cost = inner_io_cost - pages2;

	  /* inner IO cost cannot be greater than two times of the number of pages of the list file */
	  inner_io_cost = pages2;

	  /* The cost (in io's) of just handling a list file.  This is mostly to discourage the optimizer from choosing
	   * nl-join with temp inner for joins of little classes.
	   */
	  io_cost = inner->fixed_io_cost * 0.1;
	  diff_cost = MIN (io_cost, diff_cost);
	  planp->fixed_io_cost += diff_cost + 0.1;
	}

      inner_cpu_cost += (outer->info)->cardinality * pages * NONGROUPED_SCAN_COST;

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

    /* Compute the costs for all of the subqueries. Each of the pinned subqueries is intended to be evaluated once for
     * each row produced by this plan; the cost of each such evaluation in the fixed cost of the subquery plus one trip
     * through the result, i.e.,
     *
     * QO_PLAN_FIXED_COST(subplan) + QO_PLAN_ACCESS_COST(subplan)
     *
     * The cost info for the subplan has (probably) been squirreled away in a QO_SUMMARY structure reachable from the
     * original select node.
     */

    /* When computing the cost for a WORST_PLAN, we'll get in here without a backing info node; just work around it. */
    env = inner->info ? (inner->info)->env : NULL;
    subq_cpu_cost = subq_io_cost = 0.0;	/* init */

    for (i = bitset_iterate (&(inner->subqueries), &iter); i != -1; i = bitset_next_member (&iter))
      {
	subq = env ? &env->subqueries[i] : NULL;
	query = subq ? subq->node : NULL;
	qo_plan_compute_subquery_cost (query, &temp_cpu_cost, &temp_io_cost);
	subq_cpu_cost += temp_cpu_cost;
	subq_io_cost += temp_io_cost;
      }

    planp->variable_cpu_cost += MAX (0.0, guessed_result_cardinality - 1.0) * subq_cpu_cost;
    planp->variable_io_cost += MAX (0.0, outer->variable_io_cost - 1.0) * subq_io_cost;	/* assume IO as # blocks */
  }
}

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
  QO_ENV *env;
  double outer_cardinality = 0.0, inner_cardinality = 0.0;

  inner = planp->plan_un.join.inner;

  /* for worst cost */
  if (inner->fixed_cpu_cost == QO_INFINITY || inner->fixed_io_cost == QO_INFINITY
      || inner->variable_cpu_cost == QO_INFINITY || inner->variable_io_cost == QO_INFINITY)
    {
      qo_worst_cost (planp);
      return;
    }

  outer = planp->plan_un.join.outer;

  /* for worst cost */
  if (outer->fixed_cpu_cost == QO_INFINITY || outer->fixed_io_cost == QO_INFINITY
      || outer->variable_cpu_cost == QO_INFINITY || outer->variable_io_cost == QO_INFINITY)
    {
      qo_worst_cost (planp);
      return;
    }

  env = outer->info->env;
  if (outer->has_sort_limit)
    {
      outer_cardinality = (double) db_get_bigint (&QO_ENV_LIMIT_VALUE (env));
    }
  else
    {
      outer_cardinality = outer->info->cardinality;
    }

  if (inner->has_sort_limit)
    {
      inner_cardinality = (double) db_get_bigint (&QO_ENV_LIMIT_VALUE (env));
    }
  else
    {
      inner_cardinality = inner->info->cardinality;
    }

  /* CPU and IO costs which are fixed against join */
  planp->fixed_cpu_cost = outer->fixed_cpu_cost + inner->fixed_cpu_cost;
  planp->fixed_io_cost = outer->fixed_io_cost + inner->fixed_io_cost;
  /* CPU and IO costs which are variable according to the join plan */
  planp->variable_cpu_cost = outer->variable_cpu_cost + inner->variable_cpu_cost;
  planp->variable_cpu_cost += (outer_cardinality / 2) * (inner_cardinality / 2) * (double) QO_CPU_WEIGHT;
  /* merge cost */
  planp->variable_io_cost = outer->variable_io_cost + inner->variable_io_cost;
}

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
qo_follow_new (QO_INFO * info, QO_PLAN * head_plan, QO_TERM * path_term, BITSET * sarged_terms,
	       BITSET * pinned_subqueries)
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
  plan->analytic_eval_list = NULL;
  plan->plan_type = QO_PLANTYPE_FOLLOW;
  plan->vtbl = &qo_follow_plan_vtbl;
  plan->order = QO_UNORDERED;

  plan->plan_un.follow.head = qo_plan_add_ref (head_plan);
  plan->plan_un.follow.path = path_term;

  plan->multi_range_opt_use = PLAN_MULTI_RANGE_OPT_NO;

  bitset_assign (&(plan->sarged_terms), sarged_terms);
  bitset_remove (&(plan->sarged_terms), QO_TERM_IDX (path_term));

  bitset_assign (&(plan->subqueries), pinned_subqueries);

  bitset_union (&(plan->sarged_terms), &(QO_NODE_SARGS (QO_TERM_TAIL (path_term))));
  bitset_union (&(plan->subqueries), &(QO_NODE_SUBQUERIES (QO_TERM_TAIL (path_term))));

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
qo_follow_walk (QO_PLAN * plan, void (*child_fn) (QO_PLAN *, void *), void *child_data,
		void (*parent_fn) (QO_PLAN *, void *), void *parent_data)
{
  if (child_fn)
    {
      (*child_fn) (plan->plan_un.follow.head, child_data);
    }
  if (parent_fn)
    {
      (*parent_fn) (plan, parent_data);
    }
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
  fprintf (f, "\n%*c%s(%s)", (int) howfar, ' ', (plan->vtbl)->info_string, qo_term_string (plan->plan_un.follow.path));
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

  /* for worst cost */
  if (head->fixed_cpu_cost == QO_INFINITY || head->fixed_io_cost == QO_INFINITY
      || head->variable_cpu_cost == QO_INFINITY || head->variable_io_cost == QO_INFINITY)
    {
      qo_worst_cost (planp);
      return;
    }

  cardinality = (planp->info)->cardinality;
  tail = QO_TERM_TAIL (planp->plan_un.follow.path);
  target_pages = (double) QO_NODE_TCARD (tail);

  if (cardinality < target_pages)
    {
      /* If we expect to fetch fewer objects than there are pages in the target class, just assume that each fetch will
       * touch a new page.
       */
      fetch_ios = cardinality;
    }
  else if (prm_get_integer_value (PRM_ID_PB_NBUFFERS) >= target_pages)
    {
      /* We have more pointers to follow than pages in the target, but fewer target pages than buffer pages.  Assume
       * that the page buffering will limit the number of of page fetches to the number of target pages.
       */
      fetch_ios = target_pages;
    }
  else
    {
      fetch_ios = cardinality * (1.0 - ((double) prm_get_integer_value (PRM_ID_PB_NBUFFERS)) / target_pages);
    }

  planp->fixed_cpu_cost = head->fixed_cpu_cost;
  planp->fixed_io_cost = head->fixed_io_cost;
  planp->variable_cpu_cost = head->variable_cpu_cost + (cardinality * (double) QO_CPU_WEIGHT);
  planp->variable_io_cost = head->variable_io_cost + fetch_ios;
}


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
qo_cp_new (QO_INFO * info, QO_PLAN * outer, QO_PLAN * inner, BITSET * sarged_terms, BITSET * pinned_subqueries)
{
  QO_PLAN *plan;
  BITSET empty_terms;

  bitset_init (&empty_terms, info->env);

  plan = qo_join_new (info, JOIN_INNER /* default */ ,
		      QO_JOINMETHOD_NL_JOIN, outer, inner, &empty_terms /* join_terms */ ,
		      &empty_terms /* duj_terms */ ,
		      &empty_terms /* afj_terms */ ,
		      sarged_terms, pinned_subqueries, &empty_terms /* hash_terms */ );

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
  plan->analytic_eval_list = NULL;
  plan->order = QO_UNORDERED;
  plan->plan_type = QO_PLANTYPE_WORST;
  plan->vtbl = &qo_worst_plan_vtbl;

  plan->multi_range_opt_use = PLAN_MULTI_RANGE_OPT_NO;

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
  planp->use_iscan_descending = false;
}


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
}


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
    {
      return plan;
    }
  else if (BITSET_MEMBER ((plan->info)->eqclasses, QO_EQCLASS_IDX (order)))
    {
      return qo_sort_new (plan, order, SORT_TEMP);
    }
  else
    {
      return (QO_PLAN *) NULL;
    }
}

/*
 * qo_plan_cmp_prefer_covering_index () - TODO
 *   return: one of {PLAN_COMP_UNK, PLAN_COMP_LT, PLAN_COMP_GT}
 *   scan_plan_p(in):
 *   sort_plan_p(in):
 */
static QO_PLAN_COMPARE_RESULT
qo_plan_cmp_prefer_covering_index (QO_PLAN * scan_plan_p, QO_PLAN * sort_plan_p)
{
  QO_PLAN *sort_subplan_p;

  assert (scan_plan_p->plan_type == QO_PLANTYPE_SCAN);
  assert (sort_plan_p->plan_type == QO_PLANTYPE_SORT);

  sort_subplan_p = sort_plan_p->plan_un.sort.subplan;

  if (!qo_is_interesting_order_scan (scan_plan_p) || !qo_is_interesting_order_scan (sort_subplan_p))
    {
      return PLAN_COMP_UNK;
    }

  if (qo_is_index_iss_scan (scan_plan_p) || qo_is_index_loose_scan (scan_plan_p))
    {
      return PLAN_COMP_UNK;
    }

  if (qo_is_index_iss_scan (sort_subplan_p) || qo_is_index_loose_scan (sort_subplan_p))
    {
      return PLAN_COMP_UNK;
    }

  if (qo_is_index_covering_scan (sort_subplan_p))
    {
      /* if the sort plan contains a index plan with segment covering, prefer it */
      if (qo_is_index_covering_scan (scan_plan_p))
	{
	  if (scan_plan_p->plan_un.scan.index->head == sort_subplan_p->plan_un.scan.index->head)
	    {
	      return PLAN_COMP_LT;
	    }
	}
      else
	{
	  if (!bitset_is_empty (&(sort_subplan_p->plan_un.scan.terms)))
	    {
	      /* prefer covering index scan with key-range */
	      return PLAN_COMP_GT;
	    }
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
#if 1				/* TODO - do not delete me */
#define QO_PLAN_CMP_CHECK_COST(a, b)
#else
#define QO_PLAN_CMP_CHECK_COST(a, b) assert ((a) < ((b)*10));
#endif

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

  af = a->fixed_cpu_cost + a->fixed_io_cost;
  aa = a->variable_cpu_cost + a->variable_io_cost;
  bf = b->fixed_cpu_cost + b->fixed_io_cost;
  ba = b->variable_cpu_cost + b->variable_io_cost;

  if (qo_is_sort_limit (a))
    {
      if (qo_is_sort_limit (b))
	{
	  /* compare subplans */
	  a = a->plan_un.sort.subplan;
	  b = b->plan_un.sort.subplan;
	}
      else if (a->plan_un.sort.subplan == b)
	{
	  /* a is a SORT-LIMIT plan over b */
	  QO_PLAN_CMP_CHECK_COST (af + aa, bf + ba);
	  return PLAN_COMP_LT;
	}
    }
  else if (qo_is_sort_limit (b) && a == b->plan_un.sort.subplan)
    {
      QO_PLAN_CMP_CHECK_COST (bf + ba, af + aa);
      return PLAN_COMP_GT;
    }

  /* skip out top-level sort plan */
  if (a->top_rooted && b->top_rooted)
    {
      /* skip out the same sort plan */
      while (a->plan_type == QO_PLANTYPE_SORT && b->plan_type == QO_PLANTYPE_SORT
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

  if (a == b)
    {
      QO_PLAN_CMP_CHECK_COST (af + aa, bf + ba);
      QO_PLAN_CMP_CHECK_COST (bf + ba, af + aa);
      return PLAN_COMP_EQ;
    }

  if ((a->plan_type != QO_PLANTYPE_SCAN && a->plan_type != QO_PLANTYPE_SORT)
      || (b->plan_type != QO_PLANTYPE_SCAN && b->plan_type != QO_PLANTYPE_SORT))
    {
      /* there may be joins with multi range optimizations */
      temp_res = qo_multi_range_opt_plans_cmp (a, b);
      if (temp_res == PLAN_COMP_LT)
	{
	  QO_PLAN_CMP_CHECK_COST (af + aa, bf + ba);
	  return PLAN_COMP_LT;
	}
      else if (temp_res == PLAN_COMP_GT)
	{
	  QO_PLAN_CMP_CHECK_COST (bf + ba, af + aa);
	  return PLAN_COMP_GT;
	}
      else
	{
	  goto cost_cmp;	/* give up */
	}
    }

  /* a order by skip plan is always preferred to a sort plan */
  if (a->plan_type == QO_PLANTYPE_SCAN && b->plan_type == QO_PLANTYPE_SORT)
    {
      /* prefer scan if it is multi range opt */
      if (qo_is_index_mro_scan (a))
	{
	  QO_PLAN_CMP_CHECK_COST (af + aa, bf + ba);
	  return PLAN_COMP_LT;
	}

      temp_res = qo_plan_cmp_prefer_covering_index (a, b);
      if (temp_res == PLAN_COMP_LT)
	{
	  QO_PLAN_CMP_CHECK_COST (af + aa, bf + ba);
	  return PLAN_COMP_LT;
	}
      else if (temp_res == PLAN_COMP_GT)
	{
	  QO_PLAN_CMP_CHECK_COST (bf + ba, af + aa);
	  return PLAN_COMP_GT;
	}

      if (!qo_is_index_iss_scan (a) && !qo_is_index_loose_scan (a))
	{
	  if (a->plan_un.scan.index && a->plan_un.scan.index->head->groupby_skip)
	    {
	      QO_PLAN_CMP_CHECK_COST (af + aa, bf + ba);
	      return PLAN_COMP_LT;
	    }
	  if (a->plan_un.scan.index && a->plan_un.scan.index->head->orderby_skip)
	    {
	      QO_PLAN_CMP_CHECK_COST (af + aa, bf + ba);
	      return PLAN_COMP_LT;
	    }
	}
    }

  if (b->plan_type == QO_PLANTYPE_SCAN && a->plan_type == QO_PLANTYPE_SORT)
    {
      /* prefer scan if it is multi range opt */
      if (qo_is_index_mro_scan (b))
	{
	  QO_PLAN_CMP_CHECK_COST (bf + ba, af + aa);
	  return PLAN_COMP_GT;
	}

      temp_res = qo_plan_cmp_prefer_covering_index (b, a);

      /* Since we swapped its position, we have to negate the comp result */
      if (temp_res == PLAN_COMP_LT)
	{
	  QO_PLAN_CMP_CHECK_COST (bf + ba, af + aa);
	  return PLAN_COMP_GT;
	}
      else if (temp_res == PLAN_COMP_GT)
	{
	  QO_PLAN_CMP_CHECK_COST (af + aa, bf + ba);
	  return PLAN_COMP_LT;
	}

      if (!qo_is_index_iss_scan (b) && !qo_is_index_loose_scan (b))
	{
	  if (b->plan_un.scan.index && b->plan_un.scan.index->head->groupby_skip)
	    {
	      QO_PLAN_CMP_CHECK_COST (bf + ba, af + aa);
	      return PLAN_COMP_GT;
	    }
	  if (b->plan_un.scan.index && b->plan_un.scan.index->head->orderby_skip)
	    {
	      QO_PLAN_CMP_CHECK_COST (bf + ba, af + aa);
	      return PLAN_COMP_GT;
	    }
	}
    }

  if (a->plan_type == QO_PLANTYPE_SCAN && b->plan_type == QO_PLANTYPE_SCAN)
    {
      /* check if it is an unique index and all columns are equi */
      if (qo_is_all_unique_index_columns_are_equi_terms (a) && !qo_is_all_unique_index_columns_are_equi_terms (b))
	{
	  QO_PLAN_CMP_CHECK_COST (af + aa, bf + ba);
	  return PLAN_COMP_LT;
	}
      if (!qo_is_all_unique_index_columns_are_equi_terms (a) && qo_is_all_unique_index_columns_are_equi_terms (b))
	{
	  QO_PLAN_CMP_CHECK_COST (bf + ba, af + aa);
	  return PLAN_COMP_GT;
	}

      /* check multi range optimization */
      if (qo_is_index_mro_scan (a) && !qo_is_index_mro_scan (b))
	{
	  QO_PLAN_CMP_CHECK_COST (af + aa, bf + ba);
	  return PLAN_COMP_LT;
	}
      if (!qo_is_index_mro_scan (a) && qo_is_index_mro_scan (b))
	{
	  QO_PLAN_CMP_CHECK_COST (bf + ba, af + aa);
	  return PLAN_COMP_GT;
	}

      /* check covering index scan */
      if (qo_is_index_covering_scan (a) && qo_is_seq_scan (b))
	{
	  QO_PLAN_CMP_CHECK_COST (af + aa, bf + ba);
	  return PLAN_COMP_LT;
	}
      if (qo_is_index_covering_scan (b) && qo_is_seq_scan (a))
	{
	  QO_PLAN_CMP_CHECK_COST (bf + ba, af + aa);
	  return PLAN_COMP_GT;
	}

      /* a plan does order by skip, the other does group by skip - prefer the group by skipping because it's done in
       * the final step
       */
      if (qo_is_interesting_order_scan (a) && qo_is_interesting_order_scan (b))
	{
	  if (!qo_is_index_iss_scan (a) && !qo_is_index_loose_scan (a) && !qo_is_index_iss_scan (b)
	      && !qo_is_index_loose_scan (b))
	    {
	      if (a->plan_un.scan.index->head->orderby_skip && b->plan_un.scan.index->head->groupby_skip)
		{
		  QO_PLAN_CMP_CHECK_COST (af + aa, bf + ba);
		  return PLAN_COMP_LT;
		}
	      else if (a->plan_un.scan.index->head->groupby_skip && b->plan_un.scan.index->head->orderby_skip)
		{
		  QO_PLAN_CMP_CHECK_COST (bf + ba, af + aa);
		  return PLAN_COMP_GT;
		}
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

      QO_PLAN_CMP_CHECK_COST (af + aa, bf + ba);
      return PLAN_COMP_LT;
    }
  else if (QO_NODE_NCARD (b_node) == 0 && QO_NODE_TCARD (b_node) == 0)
    {
      QO_PLAN_CMP_CHECK_COST (bf + ba, af + aa);
      return PLAN_COMP_GT;
    }

  if (QO_NODE_IDX (a_node) != QO_NODE_IDX (b_node))
    {
      goto cost_cmp;		/* give up */
    }

  /* check for both index scan of the same spec */
  if (!qo_is_interesting_order_scan (a) || !qo_is_interesting_order_scan (b))
    {
      goto cost_cmp;		/* give up */
    }

  /* check multi range optimization */
  temp_res = qo_multi_range_opt_plans_cmp (a, b);
  if (temp_res == PLAN_COMP_LT)
    {
      QO_PLAN_CMP_CHECK_COST (af + aa, bf + ba);
      return PLAN_COMP_LT;
    }
  else if (temp_res == PLAN_COMP_GT)
    {
      QO_PLAN_CMP_CHECK_COST (bf + ba, af + aa);
      return PLAN_COMP_GT;
    }

  /* check index coverage */
  temp_res = qo_index_covering_plans_cmp (a, b);
  if (temp_res == PLAN_COMP_LT)
    {
      QO_PLAN_CMP_CHECK_COST (af + aa, bf + ba);
      return PLAN_COMP_LT;
    }
  else if (temp_res == PLAN_COMP_GT)
    {
      QO_PLAN_CMP_CHECK_COST (bf + ba, af + aa);
      return PLAN_COMP_GT;
    }

  /* check if one of the plans skips the order by, and if so, prefer it */
  temp_res = qo_order_by_skip_plans_cmp (a, b);
  if (temp_res == PLAN_COMP_LT)
    {
      QO_PLAN_CMP_CHECK_COST (af + aa, bf + ba);
      return PLAN_COMP_LT;
    }
  else if (temp_res == PLAN_COMP_GT)
    {
      QO_PLAN_CMP_CHECK_COST (bf + ba, af + aa);
      return PLAN_COMP_GT;
    }

  /* check if one of the plans skips the group by, and if so, prefer it */
  temp_res = qo_group_by_skip_plans_cmp (a, b);
  if (temp_res == PLAN_COMP_LT)
    {
      QO_PLAN_CMP_CHECK_COST (af + aa, bf + ba);
      return PLAN_COMP_LT;
    }
  else if (temp_res == PLAN_COMP_GT)
    {
      QO_PLAN_CMP_CHECK_COST (bf + ba, af + aa);
      return PLAN_COMP_GT;
    }

  /* iscan vs iscan index rule comparison */

  {
    QO_NODE_INDEX_ENTRY *a_ni, *b_ni;
    QO_INDEX_ENTRY *a_ent, *b_ent;
    QO_ATTR_CUM_STATS *a_cum, *b_cum;
    int a_range, b_range;	/* num iscan range terms */
    int a_filter, b_filter;	/* num iscan filter terms */
    int a_last, b_last;		/* the last partial-key indicator */
    int a_keys, b_keys;		/* num keys */
    int a_pages, b_pages;	/* num access index pages */
    int a_leafs, b_leafs;	/* num access index leaf pages */
    int i;
    QO_TERM *term;

    /* index entry of spec 'a' */
    a_ni = a->plan_un.scan.index;
    a_ent = (a_ni)->head;
    a_cum = &(a_ni)->cum_stats;

    assert (a_cum->pkeys_size <= BTREE_STATS_PKEYS_NUM);
    for (i = 0; i < a_cum->pkeys_size; i++)
      {
	if (a_cum->pkeys[i] <= 0)
	  {
	    break;
	  }
      }
    a_last = i;

    /* index range terms */
    a_range = bitset_cardinality (&(a->plan_un.scan.terms));
    if (a_range > 0 && !(a->plan_un.scan.index_equi))
      {
	a_range--;		/* set the last equal range term */
      }

    /* index filter terms */
    a_filter = bitset_cardinality (&(a->plan_un.scan.kf_terms));

    /* index entry of spec 'b' */
    b_ni = b->plan_un.scan.index;
    b_ent = (b_ni)->head;
    b_cum = &(b_ni)->cum_stats;

    assert (b_cum->pkeys_size <= BTREE_STATS_PKEYS_NUM);
    for (i = 0; i < b_cum->pkeys_size; i++)
      {
	if (b_cum->pkeys[i] <= 0)
	  {
	    break;
	  }
      }
    b_last = i;

    /* index range terms */
    b_range = bitset_cardinality (&(b->plan_un.scan.terms));
    if (b_range > 0 && !(b->plan_un.scan.index_equi))
      {
	b_range--;		/* set the last equal range term */
      }

    /* index filter terms */
    b_filter = bitset_cardinality (&(b->plan_un.scan.kf_terms));

    /* STEP 1: take the smaller search condition */

    /* check for same index pointer */
    if (a_ent == b_ent)
      {
	/* check for search condition */
	if (a_range == b_range && a_filter == b_filter)
	  {
	    ;			/* go ahead */
	  }
	else if (a_range >= b_range && a_filter >= b_filter)
	  {
	    QO_PLAN_CMP_CHECK_COST (af + aa, bf + ba);
	    return PLAN_COMP_LT;
	  }
	else if (a_range <= b_range && a_filter <= b_filter)
	  {
	    QO_PLAN_CMP_CHECK_COST (bf + ba, af + aa);
	    return PLAN_COMP_GT;
	  }
      }

    /* STEP 2: check by index terms */

    temp_res = qo_plan_iscan_terms_cmp (a, b);
    if (temp_res == PLAN_COMP_LT)
      {
	QO_PLAN_CMP_CHECK_COST (af + aa, bf + ba);
	return PLAN_COMP_LT;
      }
    else if (temp_res == PLAN_COMP_GT)
      {
	QO_PLAN_CMP_CHECK_COST (bf + ba, af + aa);
	return PLAN_COMP_GT;
      }

    /* STEP 3: take the smaller access pages */

    if (a->variable_io_cost != b->variable_io_cost)
      {
	goto cost_cmp;		/* give up */
      }

    /* btree partial-key stats */
    if (a_range == a_ent->col_num)
      {
	a_keys = a_cum->keys;
      }
    else if (a_range > 0 && a_range < a_last)
      {
	a_keys = a_cum->pkeys[a_range - 1];
      }
    else
      {				/* a_range == 0 */
	a_keys = 1;		/* init as full range */
	if (a_last > 0)
	  {
	    if (bitset_cardinality (&(a->plan_un.scan.terms)) > 0)
	      {
		term = QO_ENV_TERM ((a->info)->env, bitset_first_member (&(a->plan_un.scan.terms)));
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
    else if (a_keys == 0)
      {
	a_leafs = 0;
      }
    else
      {
	a_leafs = (int) ceil ((double) a_cum->leafs / a_keys);
      }

    /* btree access pages */
    a_pages = a_leafs + a_cum->height - 1;

    /* btree partial-key stats */
    if (b_range == b_ent->col_num)
      {
	b_keys = b_cum->keys;
      }
    else if (b_range > 0 && b_range < b_last)
      {
	b_keys = b_cum->pkeys[b_range - 1];
      }
    else
      {				/* b_range == 0 */
	b_keys = 1;		/* init as full range */
	if (b_last > 0)
	  {
	    if (bitset_cardinality (&(b->plan_un.scan.terms)) > 0)
	      {
		term = QO_ENV_TERM ((b->info)->env, bitset_first_member (&(b->plan_un.scan.terms)));
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
    else if (b_keys == 0)
      {
	b_leafs = 0;
      }
    else
      {
	b_leafs = (int) ceil ((double) b_cum->leafs / b_keys);
      }

    /* btree access pages */
    b_pages = b_leafs + b_cum->height - 1;

    if (a_pages < b_pages)
      {
	QO_PLAN_CMP_CHECK_COST (af + aa, bf + ba);
	return PLAN_COMP_LT;
      }
    else if (a_pages > b_pages)
      {
	QO_PLAN_CMP_CHECK_COST (bf + ba, af + aa);
	return PLAN_COMP_GT;
      }

    /* STEP 4: take the smaller index */
    if (a_cum->pages > a_cum->height && b_cum->pages > b_cum->height)
      {
	/* each index is big enough */
	if (a_cum->pages < b_cum->pages)
	  {
	    QO_PLAN_CMP_CHECK_COST (af + aa, bf + ba);
	    return PLAN_COMP_LT;
	  }
	else if (a_cum->pages > b_cum->pages)
	  {
	    QO_PLAN_CMP_CHECK_COST (bf + ba, af + aa);
	    return PLAN_COMP_GT;
	  }
      }

    /* STEP 5: take the smaller key range */
    if (a_keys > b_keys)
      {
	QO_PLAN_CMP_CHECK_COST (af + aa, bf + ba);
	return PLAN_COMP_LT;
      }
    else if (a_keys < b_keys)
      {
	QO_PLAN_CMP_CHECK_COST (bf + ba, af + aa);
	return PLAN_COMP_GT;
      }

    if (af == bf && aa == ba)
      {
	if (a->plan_un.scan.index_equi == b->plan_un.scan.index_equi && qo_is_index_covering_scan (a)
	    && qo_is_index_covering_scan (b))
	  {
	    if (a_ent->col_num > b_ent->col_num)
	      {
		QO_PLAN_CMP_CHECK_COST (bf + ba, af + aa);
		return PLAN_COMP_GT;
	      }
	    else if (a_ent->col_num < b_ent->col_num)
	      {
		QO_PLAN_CMP_CHECK_COST (af + aa, bf + ba);
		return PLAN_COMP_LT;
	      }
	  }

	if (qo_is_index_mro_scan (a) && qo_is_index_mro_scan (b))
	  {
	    if (a_ent->col_num > b_ent->col_num)
	      {
		QO_PLAN_CMP_CHECK_COST (bf + ba, af + aa);
		return PLAN_COMP_GT;
	      }
	    else if (a_ent->col_num < b_ent->col_num)
	      {
		QO_PLAN_CMP_CHECK_COST (af + aa, bf + ba);
		return PLAN_COMP_LT;
	      }
	  }

	/* if both plans skip order by and same costs, take the larger one */
	if (!qo_is_index_iss_scan (a) && !qo_is_index_loose_scan (a) && !qo_is_index_iss_scan (b)
	    && !qo_is_index_loose_scan (b))
	  {
	    if (a_ent->orderby_skip && b_ent->orderby_skip)
	      {
		if (a_ent->col_num > b_ent->col_num)
		  {
		    QO_PLAN_CMP_CHECK_COST (af + aa, bf + ba);
		    return PLAN_COMP_LT;
		  }
		else if (a_ent->col_num < b_ent->col_num)
		  {
		    /* if the new plan has more columns, prefer it */
		    QO_PLAN_CMP_CHECK_COST (bf + ba, af + aa);
		    return PLAN_COMP_GT;
		  }
	      }
	  }

	/* if both plans skip group by and same costs, take the larger one */
	if (a_ent->groupby_skip && b_ent->groupby_skip)
	  {
	    if (a_ent->col_num > b_ent->col_num)
	      {
		QO_PLAN_CMP_CHECK_COST (af + aa, bf + ba);
		return PLAN_COMP_LT;
	      }
	    else if (a_ent->col_num < b_ent->col_num)
	      {
		/* if the new plan has more columns, prefer it */
		QO_PLAN_CMP_CHECK_COST (bf + ba, af + aa);
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
      QO_PLAN_CMP_CHECK_COST (af + aa, bf + ba);
      return PLAN_COMP_LT;
    }
  if (bf <= af && ba <= aa)
    {
      QO_PLAN_CMP_CHECK_COST (bf + ba, af + aa);
      return PLAN_COMP_GT;
    }

  return PLAN_COMP_UNK;
#endif /* OLD_CODE */
}

/*
 * qo_multi_range_opt_plans_cmp () - compare two plans in regard with multi
 *				     range optimizations
 *
 * return : compare result
 * a (in) : first plan
 * b (in) : second plan
 */
static QO_PLAN_COMPARE_RESULT
qo_multi_range_opt_plans_cmp (QO_PLAN * a, QO_PLAN * b)
{
  QO_PLAN_COMPARE_RESULT temp_res;

  /* if no plan uses multi range optimization, nothing to do here */
  if (!qo_plan_multi_range_opt (a) && !qo_plan_multi_range_opt (b))
    {
      return PLAN_COMP_UNK;
    }

  /* check if only one plan uses multi range optimization */
  if (qo_plan_multi_range_opt (a) && !qo_plan_multi_range_opt (b))
    {
      return PLAN_COMP_LT;
    }
  if (!qo_plan_multi_range_opt (a) && qo_plan_multi_range_opt (b))
    {
      return PLAN_COMP_GT;
    }

  /* at here, both plans use multi range optimization */

  if (a->plan_type == QO_PLANTYPE_JOIN && b->plan_type == QO_PLANTYPE_JOIN)
    {
      /* choose the plan where the optimized index scan is "outer-most" */
      int a_mro_join_idx = -1, b_mro_join_idx = -1;

      if (qo_find_subplan_using_multi_range_opt (a, NULL, &a_mro_join_idx) != NO_ERROR
	  || qo_find_subplan_using_multi_range_opt (b, NULL, &b_mro_join_idx) != NO_ERROR)
	{
	  assert (0);
	  return PLAN_COMP_UNK;
	}
      if (a_mro_join_idx < b_mro_join_idx)
	{
	  return PLAN_COMP_LT;
	}
      else if (a_mro_join_idx > b_mro_join_idx)
	{
	  return PLAN_COMP_GT;
	}
      else
	{
	  return PLAN_COMP_EQ;
	}
    }

  if (a->plan_type == QO_PLANTYPE_JOIN || b->plan_type == QO_PLANTYPE_JOIN)
    {
      /* one plan is join, the other is not join */
      return PLAN_COMP_UNK;
    }

  /* both plans must be optimized index scans */
  assert (qo_is_index_mro_scan (a));
  assert (qo_is_index_mro_scan (b));

  assert (bitset_cardinality (&(a->plan_un.scan.terms)) > 0);
  assert (bitset_cardinality (&(b->plan_un.scan.terms)) > 0);

  /* choose the plan that also covers all segments */
  temp_res = qo_index_covering_plans_cmp (a, b);
  if (temp_res == PLAN_COMP_LT || temp_res == PLAN_COMP_GT)
    {
      return temp_res;
    }

  return qo_plan_iscan_terms_cmp (a, b);
}

/*
 * qo_index_covering_plans_cmp () - compare 2 index scan plans by coverage
 *   return:  one of {PLAN_COMP_UNK, PLAN_COMP_LT, PLAN_COMP_EQ, PLAN_COMP_GT}
 *   a(in):
 *   b(in):
 */
static QO_PLAN_COMPARE_RESULT
qo_index_covering_plans_cmp (QO_PLAN * a, QO_PLAN * b)
{
  int a_range, b_range;		/* num iscan range terms */

  if (!qo_is_interesting_order_scan (a) || !qo_is_interesting_order_scan (b))
    {
      return PLAN_COMP_UNK;
    }

  assert (a->plan_un.scan.index->head != NULL);
  assert (b->plan_un.scan.index->head != NULL);

  if (qo_is_index_iss_scan (a) || qo_is_index_loose_scan (a))
    {
      return PLAN_COMP_UNK;
    }

  if (qo_is_index_iss_scan (b) || qo_is_index_loose_scan (b))
    {
      return PLAN_COMP_UNK;
    }

  a_range = bitset_cardinality (&(a->plan_un.scan.terms));
  b_range = bitset_cardinality (&(b->plan_un.scan.terms));

  assert (a_range >= 0);
  assert (b_range >= 0);

  if (qo_is_index_covering_scan (a))
    {
      if (qo_is_index_covering_scan (b))
	{
	  return qo_plan_iscan_terms_cmp (a, b);
	}
      else
	{
	  if (a_range >= b_range)
	    {
	      /* prefer covering index scan with key-range */
	      return PLAN_COMP_LT;
	    }
	}
    }
  else if (qo_is_index_covering_scan (b))
    {
      if (b_range >= a_range)
	{
	  /* prefer covering index scan with key-range */
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
	{
	  fprintf (f, INDENT_FMT, (int) howfar, ' ');
	}
    }

  if (title)
    {
      fprintf (f, TITLE_FMT, title);
    }

  fputs ((plan->vtbl)->plan_string, f);

  {
    int title_len;

    title_len = title ? (int) strlen (title) : 0;
    howfar += (title_len + INDENT_INCR);
  }

  (*((plan->vtbl)->fprint_fn)) (plan, f, howfar);

  qo_plan_print_projected_segs (plan, f, howfar);
  qo_plan_print_sarged_terms (plan, f, howfar);
  qo_plan_print_subqueries (plan, f, howfar);
  qo_plan_print_sort_spec (plan, f, howfar);
  qo_plan_print_costs (plan, f, howfar);
  qo_plan_print_analytic_eval (plan, f, howfar);
}

/*
 * qo_plan_lite_print () -
 *   return:
 *   plan(in):
 *   f(in):
 *   howfar(in):
 */
void
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

      if (dump_enable)
	{
	  qo_print_stats (stdout);
	}
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
qo_plan_walk (QO_PLAN * plan, void (*child_fn) (QO_PLAN *, void *), void *child_data,
	      void (*parent_fn) (QO_PLAN *, void *), void *parent_data)
{
  (*(plan->vtbl)->walk_fn) (plan, child_fn, child_data, parent_fn, parent_data);
}

/*
 * qo_plan_del_ref_func () -
 *   return:
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
      parser_free_tree (QO_ENV_PARSER ((plan->info)->env), plan->iscan_sort_list);
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
      fprintf (stderr, "*** optimizer problem: plan refcount = %d ***\n", plan->refcount);
#endif /* CUBRID_DEBUG */
    }
  else
    {
      if ((plan->vtbl)->free_fn)
	{
	  (*(plan->vtbl)->free_fn) (plan);
	}

      qo_plan_walk (plan, qo_plan_del_ref_func, NULL, qo_plan_add_to_free_list, NULL);
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

/*
 * qo_plans_stats () -
 *   return:
 *   f(in):
 */
void
qo_plans_stats (FILE * f)
{
  fprintf (f, "%d/%d plans allocated/deallocated\n", qo_plans_allocated, qo_plans_deallocated);
  fprintf (f, "%d/%d plans malloced/demalloced\n", qo_plans_malloced, qo_plans_demalloced);
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
    {
      output = stdout;
    }

  if (plan == NULL)
    {
      fputs ("\nNo optimized plan!\n", output);
      return;
    }

  qo_get_optimization_param (&level, QO_PARAM_LEVEL);
  if (DETAILED_DUMP (level))
    {
      qo_plan_fprint (plan, output, 0, NULL);
    }
  else if (SIMPLE_DUMP (level))
    {
      qo_plan_lite_print (plan, output, 0);
    }

  fputs ("\n", output);
}

/*
 * qo_plan_get_cost_fn
 *
 * arguments:
 *	plan_name: The name of a particular plan type.
 *
 * returns/side-effects: nothing
 *
 * description: Retrieve the current cost function for the named plan.
 */

int
qo_plan_get_cost_fn (const char *plan_name)
{
  int n = DIM (all_vtbls);
  int i = 0;
  int cost = 'u';

  for (i = 0; i < n; i++)
    {
      if (intl_mbs_ncasecmp (plan_name, all_vtbls[i]->plan_string, strlen (all_vtbls[i]->plan_string)) == 0)
	{
	  if (all_vtbls[i]->cost_fn == &qo_zero_cost)
	    {
	      cost = '0';
	    }
	  else if (all_vtbls[i]->cost_fn == &qo_worst_cost)
	    {
	      cost = 'i';
	    }
	  else
	    {
	      cost = 'd';
	    }
	  break;
	}
    }

  return cost;
}

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
      if (intl_mbs_ncasecmp (plan_name, all_vtbls[i]->plan_string, strlen (all_vtbls[i]->plan_string)) == 0)
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
 * qo_set_cost () - csql method interface to qo_set_cost_fn()
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
 *	from csql
 */
void
qo_set_cost (DB_OBJECT * target, DB_VALUE * result, DB_VALUE * plan, DB_VALUE * cost)
{
  const char *plan_string;
  const char *cost_string;

  switch (DB_VALUE_TYPE (plan))
    {
    case DB_TYPE_STRING:
    case DB_TYPE_CHAR:
    case DB_TYPE_NCHAR:
      plan_string = db_get_string (plan);
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
      cost_string = db_get_string (cost);
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
  plan_string = qo_plan_set_cost_fn (plan_string, cost_string[0]);
  if (plan_string != NULL)
    {
      db_make_string (result, plan_string);
    }
  else
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      db_make_error (result, ER_GENERIC_ERROR);
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
    {
      planvec->plan[i] = (QO_PLAN *) NULL;
    }
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
    {
      qo_plan_del_ref (planvec->plan[i]);
    }

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
    {
      fputs ("(overflowed) ", f);
    }

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
	      planvec->plan[i] = (i < planvec->nplans) ? planvec->plan[planvec->nplans] : NULL;
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
	  /* found equal cost plan already found */
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
	}
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
	    }

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
	    }

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

  /* While initializing the cost to QO_INFINITY and starting the loop at i = 0 might look equivalent to this, it
   * actually loses if all of the elements in the vector have cost QO_INFINITY, because the comparison never succeeds
   * and we never make plan non-NULL.  This is very bad for those callers above who believe that we're returning
   * something useful.
   */

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
}

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
qo_alloc_info (QO_PLANNER * planner, BITSET * nodes, BITSET * terms, BITSET * eqclasses, double cardinality)
{
  QO_INFO *info;
  int i;
  int EQ;

  info = (QO_INFO *) malloc (sizeof (QO_INFO));
  if (info == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (QO_INFO));
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
  info->projected_size = qo_compute_projected_size (planner, &info->projected_segs);
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
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (QO_PLANVEC) * EQ);
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

  /* insert into the head of alloced info list */
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
    {
      return;
    }

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
      /* Then this "equivalence class" is a phony fabricated especially for a complex merge term. skip out */
      cmp = PLAN_COMP_LT;
    }
  else
    {
      cmp = qo_check_planvec (&info->best_no_order, plan);

      if (cmp == PLAN_COMP_GT)
	{
	  if (plan->plan_type != QO_PLANTYPE_SORT || plan->plan_un.sort.sort_type != SORT_LIMIT)
	    {
	      int i, EQ;
	      QO_PLAN *new_plan, *sort_plan, *best_plan;
	      QO_ENV *env;
	      QO_PLAN_COMPARE_RESULT new_cmp;

	      env = info->env;

	      EQ = info->planner->EQ;
	      best_plan = qo_find_best_plan_on_planvec (&info->best_no_order, 1.0);
	      if (QO_ENV_USE_SORT_LIMIT (env) == QO_SL_USE && !best_plan->has_sort_limit
		  && bitset_is_equivalent (&QO_ENV_SORT_LIMIT_NODES (env), &info->nodes)
		  && !qo_plan_is_orderby_skip_candidate (best_plan))
		{
		  /* generate a SORT_LIMIT plan over this plan */
		  sort_plan = qo_sort_new (best_plan, QO_UNORDERED, SORT_LIMIT);
		  if (sort_plan != NULL)
		    {
		      if (qo_check_plan_on_info (info, sort_plan) > 0)
			{
			  best_plan = sort_plan;
			}
		    }
		}
	      /*
	       * Check to see if any of the ordered solutions can be made
	       * cheaper by sorting this new plan.
	       */
	      for (i = 0; i < EQ; i++)
		{
		  order = &info->planner->eqclass[i];

		  new_plan = qo_plan_order_by (best_plan, order);
		  if (new_plan)
		    {
		      new_cmp = qo_check_planvec (&info->planvec[i], new_plan);
		      if (new_cmp == PLAN_COMP_LT || new_cmp == PLAN_COMP_EQ)
			{
			  qo_plan_release (new_plan);
			}
		    }
		}
	    }
	}
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

  /* if the plan is of type QO_SCANMETHOD_INDEX_ORDERBY_SCAN but it doesn't skip the orderby, we release the plan. */
  if (qo_is_iscan_from_orderby (plan) && !plan->plan_un.scan.index->head->orderby_skip)
    {
      qo_plan_release (plan);
      return 0;
    }

  /* if the plan is of type QO_SCANMETHOD_INDEX_GRUOPBY_SCAN but it doesn't skip the groupby, we release the plan. */
  if (qo_is_iscan_from_groupby (plan) && !plan->plan_un.scan.index->head->groupby_skip)
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
      cmp =
	qo_cmp_planvec (plan_order ==
			QO_UNORDERED ? &best_info->best_no_order : &best_info->planvec[QO_EQCLASS_IDX (plan_order)],
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

      cmp = qo_check_planvec (&info->planvec[QO_EQCLASS_IDX (plan_order)], plan);
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
 *   idx_join_plan_n(in):
 */
static QO_PLAN *
qo_find_best_nljoin_inner_plan_on_info (QO_PLAN * outer, QO_INFO * info, JOIN_TYPE join_type, int idx_join_plan_n)
{
  QO_PLANVEC *pv;
  QO_PLAN *temp, *best_plan, *inner;
  double temp_cost, best_cost;
  int i;

  /* init */
  best_plan = NULL;
  best_cost = 0;

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
  temp->analytic_eval_list = NULL;

  temp->plan_un.join.join_type = join_type;	/* set nl-join type */
  temp->plan_un.join.outer = outer;	/* set outer */

  temp->multi_range_opt_use = PLAN_MULTI_RANGE_OPT_NO;

  for (i = 0, pv = &info->best_no_order; i < pv->nplans; i++)
    {
      inner = pv->plan[i];	/* set inner */

      /* if already found idx-join, then exclude sequential inner */
      if (idx_join_plan_n > 0)
	{
	  if (qo_is_seq_scan (inner) || inner->has_sort_limit)
	    {
	      continue;
	    }
	}

      temp->plan_un.join.inner = inner;
      qo_nljoin_cost (temp);
      temp_cost = temp->fixed_cpu_cost + temp->fixed_io_cost + temp->variable_cpu_cost + temp->variable_io_cost;
      if (best_plan == NULL || temp_cost < best_cost)
	{
	  /* save new best inner */
	  best_cost = temp_cost;
	  best_plan = inner;
	}
    }

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

	  planp = qo_sort_new (qo_find_best_plan_on_planvec (&info->best_no_order, n), order, SORT_TEMP);

	  qo_check_planvec (&info->planvec[order_idx], planp);
	}
      pv = &info->planvec[order_idx];
    }

  return qo_find_best_plan_on_planvec (pv, n);
}

/*
 * qo_examine_idx_join () -
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
qo_examine_idx_join (QO_INFO * info, JOIN_TYPE join_type, QO_INFO * outer, QO_INFO * inner, BITSET * afj_terms,
		     BITSET * sarged_terms, BITSET * pinned_subqueries)
{
  int n = 0;
  QO_NODE *inner_node;

  /* check for right outer join; */
  if (join_type == JOIN_RIGHT)
    {
      if (bitset_cardinality (&(outer->nodes)) != 1)
	{			/* not single class spec */
	  /* inner of correlated index join should be plain class access */
	  goto exit;
	}

      inner_node = QO_ENV_NODE (outer->env, bitset_first_member (&(outer->nodes)));
      if (QO_NODE_HINT (inner_node) & PT_HINT_ORDERED)
	{
	  /* join hint: force join left-to-right; skip idx-join because, these are only support left outer join */
	  goto exit;
	}
    }
  else
    {
      inner_node = QO_ENV_NODE (inner->env, bitset_first_member (&(inner->nodes)));
    }

  /* inner is single class spec */
  if (QO_NODE_HINT (inner_node) & (PT_HINT_USE_IDX | PT_HINT_USE_NL))
    {
      /* join hint: force idx-join */
    }
  else if (QO_NODE_HINT (inner_node) & PT_HINT_USE_MERGE)
    {
      /* join hint: force merge-join; skip idx-join */
      goto exit;
    }

  /* check whether we can build a nested loop join with a correlated index scan. That is, is the inner term a scan of a
   * single node, and can this join term be used as an index with respect to that node? If so, we can build a special
   * kind of plan to exploit that.
   */
  if (join_type == JOIN_RIGHT)
    {
      /* if right outer join, select outer plan from the inner node and inner plan from the outer node, and do left
       * outer join
       */
      n = qo_examine_correlated_index (info, JOIN_LEFT, inner, outer, afj_terms, sarged_terms, pinned_subqueries);
    }
  else
    {
      n = qo_examine_correlated_index (info, join_type, outer, inner, afj_terms, sarged_terms, pinned_subqueries);
    }

exit:

  return n;
}

/*
 * qo_examine_nl_join () -
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
 *   idx_join_plan_n(in):
 */
static int
qo_examine_nl_join (QO_INFO * info, JOIN_TYPE join_type, QO_INFO * outer, QO_INFO * inner, BITSET * nl_join_terms,
		    BITSET * duj_terms, BITSET * afj_terms, BITSET * sarged_terms, BITSET * pinned_subqueries,
		    int idx_join_plan_n, BITSET * hash_terms)
{
  int n = 0;
  QO_PLAN *outer_plan, *inner_plan;
  QO_NODE *inner_node;

  if (join_type == JOIN_RIGHT)
    {
      /* converse outer join type */
      join_type = JOIN_LEFT;

      if (bitset_intersects (sarged_terms, &(info->env->fake_terms)))
	{
	  goto exit;
	}

      {
	int t;
	QO_TERM *term;
	BITSET_ITERATOR iter;

	for (t = bitset_iterate (nl_join_terms, &iter); t != -1; t = bitset_next_member (&iter))
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
	  inner_node = QO_ENV_NODE (outer->env, bitset_first_member (&(outer->nodes)));
	  if (QO_NODE_HINT (inner_node) & PT_HINT_ORDERED)
	    {
	      /* join hint: force join left-to-right; skip idx-join because, these are only support left outer join */
	      goto exit;
	    }

	  if (QO_NODE_HINT (inner_node) & PT_HINT_USE_NL)
	    {
	      /* join hint: force nl-join */
	    }
	  else if (QO_NODE_HINT (inner_node) & (PT_HINT_USE_IDX | PT_HINT_USE_MERGE))
	    {
	      /* join hint: force idx-join or merge-join; skip nl-join */
	      goto exit;
	    }
	}

      outer_plan = qo_find_best_plan_on_info (inner, QO_UNORDERED, 1.0);
      if (outer_plan == NULL)
	{
	  goto exit;
	}
      inner_plan = qo_find_best_nljoin_inner_plan_on_info (outer_plan, outer, join_type, idx_join_plan_n);
      if (inner_plan == NULL)
	{
	  goto exit;
	}
    }
  else
    {
      /* At here, inner is single class spec */
      inner_node = QO_ENV_NODE (inner->env, bitset_first_member (&(inner->nodes)));
      if (QO_NODE_HINT (inner_node) & PT_HINT_USE_NL)
	{
	  /* join hint: force nl-join */
	}
      else if (QO_NODE_HINT (inner_node) & (PT_HINT_USE_IDX | PT_HINT_USE_MERGE))
	{
	  /* join hint: force idx-join or merge-join; skip nl-join */
	  goto exit;
	}

      outer_plan = qo_find_best_plan_on_info (outer, QO_UNORDERED, 1.0);
      if (outer_plan == NULL)
	{
	  goto exit;
	}
      inner_plan = qo_find_best_nljoin_inner_plan_on_info (outer_plan, inner, join_type, idx_join_plan_n);
      if (inner_plan == NULL)
	{
	  goto exit;
	}
    }

#if 0				/* CHAINS_ONLY */
  /* If CHAINS_ONLY is defined, we want the optimizer constrained to produce only left-linear trees of joins, i.e., no
   * inner term can itself be a join or a follow.
   */

  if (inner_plan->plan_type != QO_PLANTYPE_SCAN)
    {
      if (inner_plan->plan_type == QO_PLANTYPE_SORT && inner_plan->order == QO_UNORDERED)
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
  /* Under this restriction, we are not permitted to produce plans that have follow nodes sandwiched between joins.
   * Don't ask why.
   */

  if (outer_plan->plan_type == QO_PLANTYPE_FOLLOW && QO_PLAN_SUBJOINS (outer_plan))
    {
      goto exit;
    }
  if (inner_plan->plan_type == QO_PLANTYPE_FOLLOW && QO_PLAN_SUBJOINS (inner_plan))
    {
      goto exit;
    }
#endif /* JOIN_FOLLOW_RESTRICTION */

  /* look for the best nested loop solution we can find.  Since the subnodes are already keeping track of the
   * lowest-cost plan they have seen, we needn't do any search here to find the cheapest nested loop join we can
   * produce for this combination.
   */
  n =
    qo_check_plan_on_info (info,
			   qo_join_new (info, join_type, QO_JOINMETHOD_NL_JOIN, outer_plan, inner_plan, nl_join_terms,
					duj_terms, afj_terms, sarged_terms, pinned_subqueries, hash_terms));

exit:

  return n;
}

/*
 * qo_examine_merge_join () -
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
qo_examine_merge_join (QO_INFO * info, JOIN_TYPE join_type, QO_INFO * outer, QO_INFO * inner, BITSET * sm_join_terms,
		       BITSET * duj_terms, BITSET * afj_terms, BITSET * sarged_terms, BITSET * pinned_subqueries)
{
  int n = 0;
  QO_PLAN *outer_plan, *inner_plan;
  QO_NODE *inner_node;
  QO_EQCLASS *order = QO_UNORDERED;
  int t;
  BITSET_ITERATOR iter;
  QO_TERM *term;
  BITSET empty_terms;
  bitset_init (&empty_terms, info->env);

  /* If any of the sarged terms are fake terms, we can't implement this join as a merge join, because the timing
   * assumptions required by the fake terms won't be satisfied.  Nested loops are the only joins that will work.
   */
  if (bitset_intersects (sarged_terms, &(info->env->fake_terms)))
    {
      goto exit;
    }

  /* examine ways of producing ordered results.  For each ordering, check whether the inner and outer subresults can be
   * produced in that order.  If so, check a merge join plan on that order.
   */
  for (t = bitset_iterate (sm_join_terms, &iter); t != -1; t = bitset_next_member (&iter))
    {
      term = QO_ENV_TERM (info->env, t);
      order = QO_TERM_EQCLASS (term);
      if (order != QO_UNORDERED)
	{
	  break;
	}
    }

  if (order == QO_UNORDERED)
    {
      goto exit;
    }

#ifdef OUTER_MERGE_JOIN_RESTRICTION
  if (IS_OUTER_JOIN_TYPE (join_type))
    {
      int node_idx;

      term = QO_ENV_TERM (info->env, bitset_first_member (sm_join_terms));
      node_idx = (join_type == JOIN_LEFT) ? QO_NODE_IDX (QO_TERM_HEAD (term)) : QO_NODE_IDX (QO_TERM_TAIL (term));
      for (t = bitset_iterate (duj_terms, &iter); t != -1; t = bitset_next_member (&iter))
	{
	  term = QO_ENV_TERM (info->env, t);
	  if (!BITSET_MEMBER (QO_TERM_NODES (term), node_idx))
	    {
	      goto exit;
	    }
	}
    }
#endif /* OUTER_MERGE_JOIN_RESTRICTION */

  /* At here, inner is single class spec */
  inner_node = QO_ENV_NODE (inner->env, bitset_first_member (&(inner->nodes)));

  if (QO_NODE_HINT (inner_node) & PT_HINT_USE_MERGE)
    {
      /* join hint: force m-join; */
    }
  else if (QO_NODE_HINT (inner_node) & (PT_HINT_USE_NL | PT_HINT_USE_IDX))
    {
      /* join hint: force nl-join, idx-join; */
      goto exit;
    }
  else if (!prm_get_bool_value (PRM_ID_OPTIMIZER_ENABLE_MERGE_JOIN))
    {
      /* optimizer prm: keep out m-join; */
      goto exit;
    }

  outer_plan = qo_find_best_plan_on_info (outer, order, 1.0);
  if (outer_plan == NULL)
    {
      goto exit;
    }

  inner_plan = qo_find_best_plan_on_info (inner, order, 1.0);
  if (inner_plan == NULL)
    {
      goto exit;
    }

#ifdef CHAINS_ONLY
  /* If CHAINS_ONLY is defined, we want the optimizer constrained to produce only left-linear trees of joins, i.e., no
   * inner term can itself be a join or a follow.
   */

  if (inner_plan->plan_type != QO_PLANTYPE_SCAN)
    {
      if (inner_plan->plan_type == QO_PLANTYPE_SORT && inner_plan->order == QO_UNORDERED)
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
  /* Under this restriction, we are not permitted to produce plans that have follow nodes sandwiched between joins.
   * Don't ask why.
   */

  if (outer_plan->plan_type == QO_PLANTYPE_FOLLOW && QO_PLAN_SUBJOINS (outer_plan))
    {
      goto exit;
    }
  if (inner_plan->plan_type == QO_PLANTYPE_FOLLOW && QO_PLAN_SUBJOINS (inner_plan))
    {
      goto exit;
    }
#endif /* JOIN_FOLLOW_RESTRICTION */

  n =
    qo_check_plan_on_info (info,
			   qo_join_new (info, join_type, QO_JOINMETHOD_MERGE_JOIN, outer_plan, inner_plan,
					sm_join_terms, duj_terms, afj_terms, sarged_terms, pinned_subqueries,
					&empty_terms));

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
qo_examine_correlated_index (QO_INFO * info, JOIN_TYPE join_type, QO_INFO * outer, QO_INFO * inner, BITSET * afj_terms,
			     BITSET * sarged_terms, BITSET * pinned_subqueries)
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
  outer_plan = qo_find_best_plan_on_info (outer, QO_UNORDERED, 1.0);
  if (outer_plan == NULL)
    {
      return 0;
    }

#if 0				/* JOIN_FOLLOW_RESTRICTION */
  /* Under this restriction, we are not permitted to produce plans that have follow nodes sandwiched between joins.
   * Don't ask why.
   */

  if (outer_plan->plan_type == QO_PLANTYPE_FOLLOW && QO_PLAN_SUBJOINS (outer_plan))
    {
      return 0;
    }
#endif /* JOIN_FOLLOW_RESTRICTION */

  /* inner node and its indexes */
  nodep = &info->planner->node[bitset_first_member (&(inner->nodes))];
  node_indexp = QO_NODE_INDEXES (nodep);
  if (node_indexp == NULL)
    {
      /* inner does not have any usable index */
      return 0;
    }

  bitset_init (&indexable_terms, info->env);

  /* We're interested in all of the terms so combine 'join_term' and 'sarged_terms' together. */
  if (IS_OUTER_JOIN_TYPE (join_type))
    {
      for (t = bitset_iterate (sarged_terms, &iter); t != -1; t = bitset_next_member (&iter))
	{

	  termp = QO_ENV_TERM (QO_NODE_ENV (nodep), t);

	  if (QO_TERM_CLASS (termp) == QO_TC_AFTER_JOIN)
	    {
	      /* exclude after-join term in 'sarged_terms' */
	      continue;
	    }

	  bitset_add (&indexable_terms, t);
	}
    }
  else
    {
      bitset_union (&indexable_terms, sarged_terms);
    }

  /* finally, combine inner plan's 'sarg term' together */
  bitset_union (&indexable_terms, &(QO_NODE_SARGS (nodep)));

  num_only_args = 0;		/* init */

  /* Iterate through the indexes attached to this node and look for ones which are a subset of the terms that we're
   * interested in. For each applicable index, register a plans and compute the cost.
   */
  for (i = 0; i < QO_NI_N (node_indexp); i++)
    {
      /* pointer to QO_NODE_INDEX_ENTRY structure */
      ni_entryp = QO_NI_ENTRY (node_indexp, i);
      /* pointer to QO_INDEX_ENTRY structure */
      index_entryp = (ni_entryp)->head;
      if (index_entryp->force < 0)
	{
	  continue;		/* is disabled index; skip and go ahead */
	}

      /* the index has terms which are a subset of the terms that we're interested in */
      if (bitset_intersects (&indexable_terms, &(index_entryp->terms)))
	{

	  if (!bitset_intersects (sarged_terms, &(index_entryp->terms)))
	    {
	      /* there is not join-edge, only inner sargs */
	      num_only_args++;
	      continue;
	    }

	  /* generate join index scan using 'ni_entryp' */
	  n +=
	    qo_generate_join_index_scan (info, join_type, outer_plan, inner, nodep, ni_entryp, &indexable_terms,
					 afj_terms, sarged_terms, pinned_subqueries);
	}
    }

  if (QO_NODE_HINT (nodep) & PT_HINT_USE_IDX)
    {
      /* join hint: force idx-join */
      if (n == 0 && num_only_args)
	{			/* not found 'idx-join' plan */
	  /* Re-Iterate */
	  for (i = 0; i < QO_NI_N (node_indexp); i++)
	    {
	      /* pointer to QO_NODE_INDEX_ENTRY structure */
	      ni_entryp = QO_NI_ENTRY (node_indexp, i);
	      /* pointer to QO_INDEX_ENTRY structure */
	      index_entryp = (ni_entryp)->head;
	      if (index_entryp->force < 0)
		{
		  continue;	/* is disabled index; skip and go ahead */
		}

	      /* the index has terms which are a subset of the terms that we're intersted in */
	      if (bitset_intersects (&indexable_terms, &(index_entryp->terms)))
		{
		  if (bitset_intersects (sarged_terms, &(index_entryp->terms)))
		    {
		      /* there is join-edge; already examined */
		      continue;
		    }

		  /* generate join index scan using 'ni_entryp' */
		  n +=
		    qo_generate_join_index_scan (info, join_type, outer_plan, inner, nodep, ni_entryp, &indexable_terms,
						 afj_terms, sarged_terms, pinned_subqueries);
		}
	    }
	}
    }

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
qo_examine_follow (QO_INFO * info, QO_TERM * path_term, QO_INFO * head_info, BITSET * sarged_terms,
		   BITSET * pinned_subqueries)
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
				qo_follow_new (info, qo_find_best_plan_on_info (head_info, QO_UNORDERED, 1.0),
					       path_term, sarged_terms, pinned_subqueries));

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
qo_compute_projected_segs (QO_PLANNER * planner, BITSET * nodes, BITSET * terms, BITSET * projected)
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
    }

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

  for (i = bitset_iterate (segset, &si); i != -1; i = bitset_next_member (&si))
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
  fprintf (f, "%d/%d info nodes allocated/deallocated\n", infos_allocated, infos_deallocated);
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

  planner = (QO_PLANNER *) malloc (sizeof (QO_PLANNER));
  if (planner == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (QO_PLANNER));
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
      planner->node_mask = (unsigned long) ((unsigned int) (1 << planner->N) - 1);
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

/*
 * qo_dump_planner_info () -
 *   return:
 *   planner(in):
 *   partition(in):
 *   f(in):
 */
static void
qo_dump_planner_info (QO_PLANNER * planner, QO_PARTITION * partition, FILE * f)
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
      /* in current implementation, join_info[0..2] does not used */
      i = QO_PARTITION_M_OFFSET (partition);
      M = i + QO_JOIN_INFO_SIZE (partition);
      for (i = i + 3; i < M; i++)
	{
	  info = planner->join_info[i];
	  if (info && !info->detached)
	    {
	      fputs ("join_info[", f);
	      prefix = "";	/* init */
	      for (t = bitset_iterate (&(info->nodes), &iter); t != -1; t = bitset_next_member (&iter))
		{
		  fprintf (f, "%s%d", prefix, QO_NODE_IDX (QO_ENV_NODE (planner->env, t)));
		  prefix = ",";
		}
	      fputs ("]:\n", f);
	      qo_dump_info (info, f);
	    }
	}
    }
}

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
planner_visit_node (QO_PLANNER * planner, QO_PARTITION * partition, PT_HINT_ENUM hint, QO_NODE * head_node,
		    QO_NODE * tail_node, BITSET * visited_nodes, BITSET * visited_rel_nodes, BITSET * visited_terms,
		    BITSET * nested_path_nodes, BITSET * remaining_nodes, BITSET * remaining_terms,
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
  bool check_afj_terms = false;
  bool is_dummy_term = false;
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

	  for (i = bitset_iterate (remaining_terms, &bi); i != -1; i = bitset_next_member (&bi))
	    {
	      term = QO_ENV_TERM (planner->env, i);
	      if (QO_TERM_CLASS (term) == QO_TC_PATH && QO_NODE_IDX (QO_TERM_HEAD (term)) == QO_NODE_IDX (head_node)
		  && QO_NODE_IDX (QO_TERM_TAIL (term)) == QO_NODE_IDX (tail_node))
		{
		  bitset_add (nested_path_nodes, QO_NODE_IDX (QO_TERM_TAIL (term)));
		  /* Traverse tail link */
		  do
		    {
		      found_num = 0;	/* init */
		      for (j = bitset_iterate (remaining_terms, &bj); j != -1; j = bitset_next_member (&bj))
			{
			  term = QO_ENV_TERM (planner->env, j);
			  if (QO_TERM_CLASS (term) == QO_TC_PATH
			      && BITSET_MEMBER (*nested_path_nodes, QO_NODE_IDX (QO_TERM_HEAD (term))))
			    {
			      found_idx = QO_NODE_IDX (QO_TERM_TAIL (term));
			      /* found nested path term */
			      if (!BITSET_MEMBER (*nested_path_nodes, found_idx))
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
		      for (j = bitset_iterate (remaining_terms, &bj); j != -1; j = bitset_next_member (&bj))
			{
			  term = QO_ENV_TERM (planner->env, j);
			  if (QO_TERM_CLASS (term) == QO_TC_PATH
			      && BITSET_MEMBER (*nested_path_nodes, QO_NODE_IDX (QO_TERM_TAIL (term))))
			    {
			      found_idx = QO_NODE_IDX (QO_TERM_HEAD (term));
			      /* found nested path term */
			      if (!BITSET_MEMBER (*nested_path_nodes, found_idx))
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
	    }
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

  /* head_info points to the current prefix */
  if (bitset_cardinality (visited_nodes) == 1)
    {
      /* current prefix has only one node */
      head_info = planner->node_info[QO_NODE_IDX (head_node)];
    }
  else
    {
      /* current prefix has two or more nodes */
      head_info = planner->join_info[QO_INFO_INDEX (QO_PARTITION_M_OFFSET (partition), *visited_rel_nodes)];
      /* currently, do not permit cross join plan. for future work, NEED MORE CONSIDERAION */
      if (head_info == NULL)
	{
	  goto wrapup;
	}
    }

  /* tail_info points to the node for the single class being added to the prefix */
  tail_info = planner->node_info[QO_NODE_IDX (tail_node)];

  /* connect tail_node to the prefix */
  bitset_add (visited_nodes, QO_NODE_IDX (tail_node));
  bitset_add (visited_rel_nodes, QO_NODE_REL_IDX (tail_node));
  bitset_remove (remaining_nodes, QO_NODE_IDX (tail_node));

  new_info = planner->join_info[QO_INFO_INDEX (QO_PARTITION_M_OFFSET (partition), *visited_rel_nodes)];

  /* check for already examined join_info */
  if (new_info && new_info->join_unit < planner->join_unit)
    {
      /* at here, not yet visited at this join level; use cache */

      if (new_info->best_no_order.nplans == 0)
	{
	  goto wrapup;		/* give up */
	}

      /* STEP 2: set terms for join_info */
      /* set info terms */
      bitset_assign (&info_terms, &(new_info->terms));
      bitset_difference (&info_terms, visited_terms);

      /* extract visited info terms */
      bitset_union (visited_terms, &info_terms);
      bitset_difference (remaining_terms, &info_terms);

      /* STEP 3: set pinned_subqueries */
      {
	QO_SUBQUERY *subq;

	for (i = bitset_iterate (remaining_subqueries, &bi); i != -1; i = bitset_next_member (&bi))
	  {
	    subq = &planner->subqueries[i];
	    if (bitset_subset (visited_nodes, &(subq->nodes)) && bitset_subset (visited_terms, &(subq->terms)))
	      {
		bitset_add (&pinned_subqueries, i);
	      }
	  }

	/* extract pinned subqueries */
	bitset_difference (remaining_subqueries, &pinned_subqueries);
      }

      goto go_ahead_subvisit;
    }

  /* extract terms of the tail_info subplan. this is necessary to ensure that we are aware of any terms that have been
   * sarged by the subplans */
  bitset_union (&info_terms, &(tail_info->terms));

  /* extract visited info terms */
  bitset_union (visited_terms, &info_terms);
  bitset_difference (remaining_terms, &info_terms);

  /* STEP 2: set specific terms for follow and join */

  /* in given partition, collect terms connected to tail_info */
  {
    int retry_cnt, edge_cnt, path_cnt;
    bool found_edge;

    retry_cnt = 0;		/* init */

  retry_join_edge:

    edge_cnt = path_cnt = 0;	/* init */

    for (i = bitset_iterate (remaining_terms, &bi); i != -1; i = bitset_next_member (&bi))
      {

	term = QO_ENV_TERM (planner->env, i);

	/* check term nodes */
	if (!bitset_subset (visited_nodes, &(QO_TERM_NODES (term))))
	  {
	    continue;
	  }

	/* check location for outer join */
	if (QO_TERM_CLASS (term) == QO_TC_DURING_JOIN)
	  {
	    QO_ASSERT (planner->env, QO_ON_COND_TERM (term));

	    for (j = bitset_iterate (visited_nodes, &bj); j != -1; j = bitset_next_member (&bj))
	      {
		node = QO_ENV_NODE (planner->env, j);
		if (QO_NODE_LOCATION (node) == QO_TERM_LOCATION (term))
		  {
		    break;
		  }
	      }

	    if (j == -1)
	      {			/* out of location */
		continue;
	      }
	  }

	found_edge = false;	/* init */

	if (BITSET_MEMBER (QO_TERM_NODES (term), QO_NODE_IDX (tail_node)))
	  {
	    if (QO_TERM_CLASS (term) == QO_TC_PATH)
	      {
		if (retry_cnt == 0)
		  {		/* is the first stage */
		    /* need to check the direction; head -> tail */
		    if (QO_NODE_IDX (QO_TERM_TAIL (term)) == QO_NODE_IDX (tail_node))
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
		    /* at retry stage; there is only path edge so, need not to check the direction */
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
	    /* found edge */
	    edge_cnt++;

	    /* set join type */
	    if (join_type == NO_JOIN || is_dummy_term)
	      {
		/* the first time except dummy term */
		join_type = QO_TERM_JOIN_TYPE (term);
		is_dummy_term = QO_TERM_CLASS (term) == QO_TC_DUMMY_JOIN ? true : false;
	      }
	    else if (QO_TERM_CLASS (term) == QO_TC_DUMMY_JOIN)
	      {
		/* The dummy join term is excluded from the outer join check. */
	      }
	    else
	      {			/* already assigned */
		if (IS_OUTER_JOIN_TYPE (join_type))
		  {
		    /* outer join type must be the same */
		    if (IS_OUTER_JOIN_TYPE (QO_TERM_JOIN_TYPE (term)))
		      {
			QO_ASSERT (planner->env, join_type == QO_TERM_JOIN_TYPE (term));
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

	    switch (QO_TERM_CLASS (term))
	      {
	      case QO_TC_DUMMY_JOIN:	/* is always true dummy join term */
		/* check for idx-join */
		if (QO_TERM_CAN_USE_INDEX (term))
		  {
		    idx_join_cnt++;
		  }
		break;

	      case QO_TC_PATH:
		if (follow_term == NULL)
		  {		/* get the first PATH term idx */
		    follow_term = term;
		    /* for path-term, if join type is not outer join, we can use idx-join, nl-join */
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
		    if (IS_OUTER_JOIN_TYPE (join_type) && QO_ON_COND_TERM (term))
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
	    /* does not edge */

	    if (QO_TERM_CLASS (term) == QO_TC_DURING_JOIN)
	      {
		bitset_add (&duj_terms, i);
	      }
	    else if (QO_TERM_CLASS (term) == QO_TC_AFTER_JOIN)
	      {
		check_afj_terms = true;

		/* If visited_nodes is the same as partition's nodes, then we have successfully generated one of the
		 * graph permutations(i.e., we have considered every one of the nodes). only include after-join term
		 * for this plan.
		 */
		if (!bitset_is_equivalent (visited_nodes, &(QO_PARTITION_NODES (partition))))
		  {
		    continue;
		  }
		bitset_add (&afj_terms, i);
	      }
	    else if (QO_TERM_CLASS (term) == QO_TC_OTHER)
	      {
		if (IS_OUTER_JOIN_TYPE (join_type) && QO_ON_COND_TERM (term))
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
      }

    /* currently, do not permit cross join plan. for future work, NEED MORE CONSIDERAION */
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

#if 1				/* TO NOT DELETE ME - very special case for Object fetch plan */
  /* re-check for after join term; is depence to Object fetch plan */
  if (check_afj_terms && bitset_is_empty (&afj_terms))
    {
      BITSET path_nodes;

      bitset_init (&path_nodes, planner->env);

      for (i = bitset_iterate (remaining_terms, &bi); i != -1; i = bitset_next_member (&bi))
	{
	  term = QO_ENV_TERM (planner->env, i);

	  if (QO_TERM_CLASS (term) == QO_TC_PATH)
	    {
	      bitset_add (&path_nodes, QO_NODE_IDX (QO_TERM_TAIL (term)));
	    }
	}

      /* there is only path joined nodes. So, should apply after join terms at here. */
      if (bitset_subset (&path_nodes, remaining_nodes))
	{
	  for (i = bitset_iterate (remaining_terms, &bi); i != -1; i = bitset_next_member (&bi))
	    {
	      term = QO_ENV_TERM (planner->env, i);

	      if (QO_TERM_CLASS (term) == QO_TC_AFTER_JOIN)
		{
		  bitset_add (&afj_terms, i);
		  bitset_add (&info_terms, i);	/* add to info term */
		  bitset_add (&sarged_terms, i);	/* add to sarged term */
		}
	    }
	}

      bitset_delset (&path_nodes);
    }
#endif

  /* extract visited info terms */
  bitset_union (visited_terms, &info_terms);
  bitset_difference (remaining_terms, &info_terms);

  /* STEP 3: set pinned_subqueries */

  /* Find out if we can pin any of the remaining subqueries.  A subquery is eligible to be pinned here if all of the
   * nodes on which it depends are covered here.  However, it mustn't be pinned here if it is part of a term that
   * hasn't been pinned yet.  Doing so risks improperly pushing a subquery plan down through a merge join during XASL
   * generation, which results in an incorrect plan (the subquery has to be evaluated during the merge, rather than
   * during the scan that feeds the merge).
   */
  {
    QO_SUBQUERY *subq;

    for (i = bitset_iterate (remaining_subqueries, &bi); i != -1; i = bitset_next_member (&bi))
      {
	subq = &planner->subqueries[i];
	if (bitset_subset (visited_nodes, &(subq->nodes)) && bitset_subset (visited_terms, &(subq->terms)))
	  {
	    bitset_add (&pinned_subqueries, i);
	  }
      }

    /* extract pinned subqueries */
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
	  for (i = bitset_iterate (&sarged_terms, &bi); i != -1; i = bitset_next_member (&bi))
	    {
	      term = &planner->term[i];
	      if (QO_IS_PATH_TERM (term) && QO_TERM_JOIN_TYPE (term) != JOIN_INNER)
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

      new_info = planner->join_info[QO_INFO_INDEX (QO_PARTITION_M_OFFSET (partition), *visited_rel_nodes)] =
	qo_alloc_info (planner, visited_nodes, visited_terms, &eqclasses, cardinality);

      bitset_delset (&eqclasses);
    }

  /* STEP 5: do EXAMINE follow, join */

  {
    int kept = 0;
    int idx_join_plan_n = 0;

    /* for path-term, if join order is correct, we can use follow. */
    if (follow_term && (QO_NODE_IDX (QO_TERM_TAIL (follow_term)) == QO_NODE_IDX (tail_node)))
      {
	/* STEP 5-1: examine follow */
	kept += qo_examine_follow (new_info, follow_term, head_info, &sarged_terms, &pinned_subqueries);
      }

    if (follow_term && join_type != JOIN_INNER && QO_NODE_IDX (QO_TERM_TAIL (follow_term)) != QO_NODE_IDX (tail_node))
      {
	/* if there is a path-term whose outer join order is not correct, we can not use idx-join, nl-join, m-join */
	;
      }
    else
      {
#if 1				/* CORRELATED_INDEX */
	/* STEP 5-2: examine idx-join */
	if (idx_join_cnt)
	  {
	    idx_join_plan_n =
	      qo_examine_idx_join (new_info, join_type, head_info, tail_info, &afj_terms, &sarged_terms,
				   &pinned_subqueries);
	    kept += idx_join_plan_n;
	  }
#endif /* CORRELATED_INDEX */

	/* STEP 5-3: examine nl-join */
	/* sm_join_terms is a mergeable term for SM join. In hash list scan, mergeable term is used as hash term. */
	/* The mergeable term and the hash term have the same characteristics. */
	/* If the characteristics for mergeable terms are changed, the logic for hash terms should be separated. */
	/* mergeable term : equi-term, symmetrical term, e.g. TBL1.a = TBL2.a, function(TAB1.a) = function(TAB2.a) */
	kept +=
	  qo_examine_nl_join (new_info, join_type, head_info, tail_info, &nl_join_terms, &duj_terms, &afj_terms,
			      &sarged_terms, &pinned_subqueries, idx_join_plan_n, &sm_join_terms);

#if 1				/* MERGE_JOINS */
	/* STEP 5-4: examine merge-join */
	if (!bitset_is_empty (&sm_join_terms))
	  {
	    kept +=
	      qo_examine_merge_join (new_info, join_type, head_info, tail_info, &sm_join_terms, &duj_terms, &afj_terms,
				     &sarged_terms, &pinned_subqueries);
	  }
#endif /* MERGE_JOINS */
      }

    /* At this point, kept indicates the number of worthwhile plans generated by examine_joins (i.e., plans that where
     * cheaper than some previous equivalent plan).  If that number is 0, then there is no point in continuing this
     * particular branch of permutations: we've already generated all of the suffixes once before, and with a better
     * prefix to boot.  There is no possibility of finding a better plan with this prefix.
     */
    if (!kept)
      {
	goto wrapup;
      }
  }

  /* STEP 7: go on sub permutations */

go_ahead_subvisit:

  /* If visited_nodes' cardinality is the same as join_unit, then we have successfully generated one of the graph
   * permutations (i.e., we have considered every one of the nodes). If not, we need to try to recursively generate
   * suffixes.
   */
  if (bitset_cardinality (visited_nodes) >= planner->join_unit)
    {
      /* If this is the info node that corresponds to the final plan (i.e., every node in the partition is covered by
       * the plans at this node), *AND* we have something to put in it, then record that fact in the planner.  This
       * permits more aggressive pruning, since we can immediately discard any plan (or subplan) that is no better than
       * the best known plan for the entire partition.
       */
      if (!planner->best_info)
	{
	  planner->best_info = new_info;
	}
    }
  else
    {
      for (i = bitset_iterate (remaining_nodes, &bi); i != -1; i = bitset_next_member (&bi))
	{
	  node = QO_ENV_NODE (planner->env, i);

	  /* node dependency check; */
	  if (!bitset_subset (visited_nodes, &(QO_NODE_DEP_SET (node))))
	    {
	      /* node represents dependent tables, so there is no way this combination can work in isolation.  Give up
	       * so we can try some other combinations.
	       */
	      continue;
	    }
	  if (!bitset_subset (visited_nodes, &(QO_NODE_OUTER_DEP_SET (node))))
	    {
	      /* All previous nodes participating in outer join spec should be joined before. QO_NODE_OUTER_DEP_SET()
	       * represents all previous nodes which are dependents on the node.
	       */
	      continue;
	    }

	  /* now, set node as next tail node, do recursion */
	  (void) planner_visit_node (planner, partition, hint, tail_node,	/* next head node */
				     node,	/* next tail node */
				     visited_nodes, visited_rel_nodes, visited_terms, nested_path_nodes,
				     remaining_nodes, remaining_terms, remaining_subqueries, num_path_inner);

	  /* join hint: force join left-to-right */
	  if (hint & PT_HINT_ORDERED)
	    {
	      break;
	    }
	}
    }

wrapup:

  /* recover to original */

  bitset_remove (visited_nodes, QO_NODE_IDX (tail_node));
  bitset_remove (visited_rel_nodes, QO_NODE_REL_IDX (tail_node));
  bitset_add (remaining_nodes, QO_NODE_IDX (tail_node));

  bitset_difference (visited_terms, &info_terms);
  bitset_union (remaining_terms, &info_terms);

  bitset_union (remaining_subqueries, &pinned_subqueries);

  /* free alloced */
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

  for (i = bitset_iterate (nodeset, &bi); i != -1; i = bitset_next_member (&bi))
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
	  /* join hint: force idx-join, nl-join; */
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
    }

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
planner_permutate (QO_PLANNER * planner, QO_PARTITION * partition, PT_HINT_ENUM hint, QO_NODE * prev_head_node,
		   BITSET * visited_nodes, BITSET * visited_rel_nodes, BITSET * visited_terms, BITSET * first_nodes,
		   BITSET * nested_path_nodes, BITSET * remaining_nodes, BITSET * remaining_terms,
		   BITSET * remaining_subqueries, int num_path_inner, int *node_idxp)
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

  /* Now perform the actual search.  Entries in join_info will gradually be filled and refined within the calls to
   * examine_xxx_join(). When we finish, planner->best_info will hold information about the best ways discovered to
   * perform the entire join.
   */
  for (i = bitset_iterate (remaining_nodes, &bi); i != -1; i = bitset_next_member (&bi))
    {

      head_node = QO_ENV_NODE (planner->env, i);

      /* head node dependency check; */
      if (!bitset_subset (visited_nodes, &(QO_NODE_DEP_SET (head_node))))
	{
	  /* head node represents dependent tables, so there is no way this combination can work in isolation.  Give up
	   * so we can try some other combinations.
	   */
	  continue;
	}
      if (!bitset_subset (visited_nodes, &(QO_NODE_OUTER_DEP_SET (head_node))))
	{
	  /* All previous nodes participating in outer join spec should be joined before. QO_NODE_OUTER_DEP_SET()
	   * represents all previous nodes which are dependents on the node.
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

	  for (j = bitset_iterate (remaining_nodes, &bj); j != -1; j = bitset_next_member (&bj))
	    {

	      tail_node = QO_ENV_NODE (planner->env, j);

	      /* tail node dependency check; */
	      if (!bitset_subset (visited_nodes, &(QO_NODE_DEP_SET (tail_node))))
		{
		  continue;
		}
	      if (!bitset_subset (visited_nodes, &(QO_NODE_OUTER_DEP_SET (tail_node))))
		{
		  continue;
		}

	      BITSET_CLEAR (*nested_path_nodes);

	      (void) planner_visit_node (planner, partition, hint, head_node, tail_node, visited_nodes,
					 visited_rel_nodes, visited_terms, nested_path_nodes, remaining_nodes,
					 remaining_terms, remaining_subqueries, num_path_inner);

	      /* join hint: force join left-to-right */
	      if (hint & PT_HINT_ORDERED)
		{
		  break;
		}
	    }

	  /* recover to original */
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
				     visited_nodes, visited_rel_nodes, visited_terms, nested_path_nodes,
				     remaining_nodes, remaining_terms, remaining_subqueries, num_path_inner);
	}

      if (node_idxp)
	{			/* is partial node visit */
	  best_info = planner->best_info;
	  if (best_info == NULL)
	    {			/* not found best plan */
	      continue;		/* skip and go ahead */
	    }

	  best_plan = qo_find_best_plan_on_info (best_info, QO_UNORDERED, 1.0);

	  if (best_plan == NULL)
	    {			/* unknown error */
	      break;		/* give up */
	    }

	  /* set best plan's cost */
	  best_cost =
	    best_plan->fixed_cpu_cost + best_plan->fixed_io_cost + best_plan->variable_cpu_cost +
	    best_plan->variable_io_cost;

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
    }

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
qo_generate_join_index_scan (QO_INFO * infop, JOIN_TYPE join_type, QO_PLAN * outer_plan, QO_INFO * inner,
			     QO_NODE * nodep, QO_NODE_INDEX_ENTRY * ni_entryp, BITSET * indexable_terms,
			     BITSET * afj_terms, BITSET * sarged_terms, BITSET * pinned_subqueries)
{
  QO_ENV *env;
  QO_INDEX_ENTRY *index_entryp;
  BITSET_ITERATOR iter;
  QO_TERM *termp;
  QO_PLAN *inner_plan;
  int i, t, last_t, j, n, seg, rangelist_term_idx;
  bool found_rangelist;
  BITSET range_terms;
  BITSET empty_terms;
  BITSET remaining_terms;

  if (nodep != NULL && QO_NODE_IS_CLASS_HIERARCHY (nodep))
    {
      /* Class hierarchies are split into scan blocks which cannot be used for index joins. However, if the class
       * hierarchy is a partitioning hierarchy, we can use an index join for inner joins
       */
      if (!QO_NODE_IS_CLASS_PARTITIONED (nodep))
	{
	  return 0;
	}
      else if (join_type != JOIN_INNER)
	{
	  return 0;
	}
    }
  env = infop->env;

  bitset_init (&range_terms, env);
  bitset_init (&empty_terms, env);
  bitset_init (&remaining_terms, env);

  bitset_assign (&remaining_terms, sarged_terms);

  /* pointer to QO_INDEX_ENTRY structure */
  index_entryp = (ni_entryp)->head;
  if (index_entryp->force < 0)
    {
      assert (false);
      return 0;
    }

  found_rangelist = false;
  rangelist_term_idx = -1;
  for (i = 0; i < index_entryp->nsegs; i++)
    {
      seg = index_entryp->seg_idxs[i];
      if (seg == -1)
	{
	  break;
	}
      n = 0;
      last_t = -1;
      for (t = bitset_iterate (indexable_terms, &iter); t != -1; t = bitset_next_member (&iter))
	{
	  termp = QO_ENV_TERM (env, t);

	  /* check for always true dummy join term */
	  if (QO_TERM_CLASS (termp) == QO_TC_DUMMY_JOIN)
	    {
	      /* skip out from all terms */
	      bitset_remove (&remaining_terms, t);
	      continue;		/* do not add to range_terms */
	    }

	  if (QO_TERM_IS_FLAGED (termp, QO_TERM_MULTI_COLL_PRED))
	    {
	      /* case of multi column term ex) (a,b) in ... */
	      if (found_rangelist == true && QO_TERM_IDX (termp) != rangelist_term_idx)
		{
		  break;	/* already found. give up */
		}
	      for (j = 0; j < termp->multi_col_cnt; j++)
		{
		  if (QO_TERM_IS_FLAGED (termp, QO_TERM_RANGELIST))
		    {
		      found_rangelist = true;
		      rangelist_term_idx = QO_TERM_IDX (termp);
		    }
		  /* found term */
		  if (termp->multi_col_segs[j] == seg && BITSET_MEMBER (index_entryp->seg_equal_terms[i], t))
		    /* multi col term is only indexable when term's class is TC_SARG. so can use seg_equal_terms */
		    {
		      /* save last found term */
		      last_t = t;
		      /* found EQ term */
		      if (QO_TERM_IS_FLAGED (termp, QO_TERM_EQUAL_OP))
			{
			  bitset_add (&range_terms, t);
			  bitset_add (&(index_entryp->multi_col_range_segs), seg);
			  n++;
			}
		    }
		}
	    }
	  else
	    {
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
			      rangelist_term_idx = QO_TERM_IDX (termp);
			    }

			  bitset_add (&range_terms, t);
			  n++;
			}

		      break;
		    }
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
      inner_plan = qo_index_scan_new (inner, nodep, ni_entryp, QO_SCANMETHOD_INDEX_SCAN, &range_terms, indexable_terms);

      if (inner_plan)
	{
	  /* now, key-filter is assigned; exclude key-range, key-filter terms from remaining terms */
	  bitset_difference (&remaining_terms, &range_terms);
	  bitset_difference (&remaining_terms, &(inner_plan->plan_un.scan.kf_terms));

	  n =
	    qo_check_plan_on_info (infop,
				   qo_join_new (infop, join_type, QO_JOINMETHOD_IDX_JOIN, outer_plan, inner_plan,
						&empty_terms, &empty_terms, afj_terms, &remaining_terms,
						pinned_subqueries, &empty_terms));
	}
    }

  bitset_delset (&remaining_terms);
  bitset_delset (&empty_terms);
  bitset_delset (&range_terms);

  return n;
}

/*
 * qo_is_seq_scan ()
 *   return: true/false
 *   plan(in):
 */
bool
qo_is_seq_scan (QO_PLAN * plan)
{
  if (plan && plan->plan_type == QO_PLANTYPE_SCAN && plan->plan_un.scan.scan_method == QO_SCANMETHOD_SEQ_SCAN)
    {
      return true;
    }

  return false;
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

  planp = qo_seq_scan_new (infop, nodep);

  n = qo_check_plan_on_info (infop, planp);
  if (n)
    {
      plan_created = true;
    }
}

/*
 * qo_is_iscan ()
 *   return: true/false
 *   plan(in):
 */
bool
qo_is_iscan (QO_PLAN * plan)
{
  if (plan && plan->plan_type == QO_PLANTYPE_SCAN
      && (plan->plan_un.scan.scan_method == QO_SCANMETHOD_INDEX_SCAN
	  || plan->plan_un.scan.scan_method == QO_SCANMETHOD_INDEX_SCAN_INSPECT))
    {
      return true;
    }

  return false;
}

/*
 * qo_generate_index_scan () - With index information, generates index scan plan
 *   return: num of index scan plans
 *   infop(in): pointer to QO_INFO (environment info node which holds plans)
 *   nodep(in): pointer to QO_NODE (node in the join graph)
 *   ni_entryp(in): pointer to QO_NODE_INDEX_ENTRY (node index entry)
 *   nsegs(in):
 */
static int
qo_generate_index_scan (QO_INFO * infop, QO_NODE * nodep, QO_NODE_INDEX_ENTRY * ni_entryp, int nsegs)
{
  QO_INDEX_ENTRY *index_entryp;
  BITSET_ITERATOR iter;
  int i, t, n, normal_index_plan_n = 0;
  QO_PLAN *planp;
  BITSET range_terms;
  BITSET seg_other_terms;
  int start_column = 0;

  bitset_init (&range_terms, infop->env);
  bitset_init (&seg_other_terms, infop->env);

  /* pointer to QO_INDEX_ENTRY structure */
  index_entryp = (ni_entryp)->head;
  if (index_entryp->force < 0)
    {
      assert (false);
      return 0;
    }

  if (QO_ENTRY_MULTI_COL (index_entryp))
    {
      assert (nsegs >= 1);
      ;				/* nop */
    }
  else
    {
      assert (nsegs == 1);
      assert (index_entryp->is_iss_candidate == 0);
      assert (!(index_entryp->ils_prefix_len > 0));
    }

  start_column = index_entryp->is_iss_candidate ? 1 : 0;

  for (i = start_column; i < nsegs - 1; i++)
    {
      t = bitset_first_member (&(index_entryp->seg_equal_terms[i]));
      bitset_add (&range_terms, t);

      /* add multi_col_range_segs */
      if (QO_TERM_IS_FLAGED (QO_ENV_TERM (infop->env, t), QO_TERM_MULTI_COLL_PRED))
	{
	  bitset_add (&(index_entryp->multi_col_range_segs), index_entryp->seg_idxs[i]);
	}
    }

  /* for each terms associated with the last segment */
  t = bitset_iterate (&(index_entryp->seg_equal_terms[nsegs - 1]), &iter);
  for (; t != -1; t = bitset_next_member (&iter))
    {
      bitset_add (&range_terms, t);
      /* add multi_col_range_segs */
      if (QO_TERM_IS_FLAGED (QO_ENV_TERM (infop->env, t), QO_TERM_MULTI_COLL_PRED))
	{
	  bitset_add (&(index_entryp->multi_col_range_segs), index_entryp->seg_idxs[nsegs - 1]);
	}

      /* generate index scan plan */
      planp = qo_index_scan_new (infop, nodep, ni_entryp, QO_SCANMETHOD_INDEX_SCAN, &range_terms, NULL);

      n = qo_check_plan_on_info (infop, planp);
      if (n)
	{
	  normal_index_plan_n++;	/* include index skip scan */
	}

      /* is it safe to ignore the result of qo_check_plan_on_info()? */
      bitset_remove (&range_terms, t);
      if (QO_TERM_IS_FLAGED (QO_ENV_TERM (infop->env, t), QO_TERM_MULTI_COLL_PRED))
	{
	  bitset_remove (&(index_entryp->multi_col_range_segs), index_entryp->seg_idxs[nsegs - 1]);
	}
    }

  bitset_assign (&seg_other_terms, &(index_entryp->seg_other_terms[nsegs - 1]));
  for (t = bitset_iterate (&seg_other_terms, &iter); t != -1; t = bitset_next_member (&iter))
    {
      bitset_add (&range_terms, t);

      /* generate index scan plan */
      planp = qo_index_scan_new (infop, nodep, ni_entryp, QO_SCANMETHOD_INDEX_SCAN, &range_terms, NULL);

      n = qo_check_plan_on_info (infop, planp);
      if (n)
	{
	  normal_index_plan_n++;	/* include index skip scan */
	}

      /* is it safe to ignore the result of qo_check_plan_on_info()? */
      bitset_remove (&range_terms, t);
    }

  bitset_delset (&seg_other_terms);
  bitset_delset (&range_terms);

  return normal_index_plan_n;
}

/*
 * qo_generate_loose_index_scan () -
 *   return: num of index loosed scan plans
 *   infop(in): pointer to QO_INFO (environment info node which holds plans)
 *   nodep(in): pointer to QO_NODE (node in the join graph)
 *   ni_entryp(in): pointer to QO_NODE_INDEX_ENTRY (node index entry)
 */
static int
qo_generate_loose_index_scan (QO_INFO * infop, QO_NODE * nodep, QO_NODE_INDEX_ENTRY * ni_entryp)
{
  QO_INDEX_ENTRY *index_entryp;
  int n = 0;
  QO_PLAN *planp;
  BITSET range_terms;

  bitset_init (&range_terms, infop->env);

  /* pointer to QO_INDEX_ENTRY structure */
  index_entryp = (ni_entryp)->head;
  if (index_entryp->force < 0)
    {
      assert (false);
      return 0;
    }

  assert (bitset_is_empty (&(index_entryp->seg_equal_terms[0])));
  assert (index_entryp->ils_prefix_len > 0);
  assert (QO_ENTRY_MULTI_COL (index_entryp));
  assert (index_entryp->cover_segments == true);
  assert (index_entryp->is_iss_candidate == false);

  assert (bitset_is_empty (&range_terms));

  planp = qo_index_scan_new (infop, nodep, ni_entryp, QO_SCANMETHOD_INDEX_SCAN, &range_terms, NULL);

  n = qo_check_plan_on_info (infop, planp);

  bitset_delset (&range_terms);

  return n;
}

/*
 * qo_generate_sort_limit_plan () - generate SORT_LIMIT plans
 * return : number of plans generated
 * env (in)	:
 * infop (in)	: info for the plan
 * subplan (in) : subplan over which to generate the SORT_LIMIT plan
 */
static int
qo_generate_sort_limit_plan (QO_ENV * env, QO_INFO * infop, QO_PLAN * subplan)
{
  int n;
  QO_PLAN *plan;

  if (subplan->order != QO_UNORDERED)
    {
      /* Do not put a SORT_LIMIT plan over an ordered plan because we have to keep the ordered principle. At best, we
       * can place a SORT_LIMIT plan directly under an ordered one.
       */
      return 0;
    }

  plan = qo_sort_new (subplan, QO_UNORDERED, SORT_LIMIT);
  if (plan == NULL)
    {
      return 0;
    }
  n = qo_check_plan_on_info (infop, plan);
  return n;
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
  int i, j, nsegs;
  bool broken;
  QO_PLAN *plan;
  QO_NODE *node;
  QO_INFO *info;
  BITSET_ITERATOR si;
  int subq_idx;
  QO_SUBQUERY *subq;
  QO_NODE_INDEX *node_index;
  QO_NODE_INDEX_ENTRY *ni_entry;
  QO_INDEX_ENTRY *index_entry;
  BITSET seg_terms;
  BITSET nodes, subqueries, remaining_subqueries;
  int join_info_bytes;
  int normal_index_plan_n, n;
  int start_column = 0;
  PT_NODE *tree = NULL;
  bool special_index_scan = false;

  bitset_init (&nodes, planner->env);
  bitset_init (&subqueries, planner->env);
  bitset_init (&remaining_subqueries, planner->env);

  planner->worst_plan = qo_worst_new (planner->env);
  if (planner->worst_plan == NULL)
    {
      plan = NULL;
      goto end;
    }

  planner->worst_info = qo_alloc_info (planner, &nodes, &nodes, &nodes, QO_INFINITY);
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
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) join_info_bytes);
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
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, size);
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
	qo_alloc_info (planner, &nodes, &QO_NODE_SARGS (node), &QO_NODE_EQCLASSES (node),
		       QO_NODE_SELECTIVITY (node) * (double) QO_NODE_NCARD (node));

      if (planner->node_info[i] == NULL)
	{
	  plan = NULL;
	  goto end;
	}

      BITSET_CLEAR (subqueries);
      for (subq_idx = bitset_iterate (&remaining_subqueries, &si); subq_idx != -1; subq_idx = bitset_next_member (&si))
	{
	  subq = &planner->subqueries[subq_idx];
	  if (bitset_is_empty (&subq->nodes)	/* uncorrelated */
	      || (bitset_subset (&nodes, &(subq->nodes))	/* correlated */
		  && bitset_subset (&(QO_NODE_SARGS (node)), &(subq->terms))))
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

      /* Set special_index_scan to true if spec if flagged as: 1. Scan for b-tree key info. 2. Scan for b-tree node
       * info. These are special cases which need index scan forced.
       */
      special_index_scan = PT_SPEC_SPECIAL_INDEX_SCAN (QO_NODE_ENTITY_SPEC (node));
      if (special_index_scan)
	{
	  /* Make sure there is only one index entry */
	  assert (node_index != NULL && QO_NI_N (node_index) == 1);
	  ni_entry = QO_NI_ENTRY (node_index, 0);
	  n =
	    qo_check_plan_on_info (info,
				   qo_index_scan_new (info, node, ni_entry, QO_SCANMETHOD_INDEX_SCAN_INSPECT, NULL,
						      NULL));
	  assert (n == 1);
	  continue;
	}

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
	      ni_entry = QO_NI_ENTRY (node_index, j);
	      index_entry = (ni_entry)->head;
	      if (index_entry->force < 0)
		{
		  continue;	/* is disabled index; skip and go ahead */
		}

	      /* If the index is a candidate for index skip scan, then it will not have any terms for seg_equal or
	       * seg_other[0], so we should skip that first column from initial checks. Set the start column to 1.
	       */
	      start_column = index_entry->is_iss_candidate ? 1 : 0;

	      /* seg_terms will contain all the indexable terms that refer segments from this node; stops at the first
	       * one that has no equals or other terms
	       */
	      BITSET_CLEAR (seg_terms);
	      for (nsegs = start_column; nsegs < index_entry->nsegs; nsegs++)
		{
		  bitset_union (&seg_terms, &(index_entry->seg_equal_terms[nsegs]));
		  bitset_union (&seg_terms, &(index_entry->seg_other_terms[nsegs]));

		  if (bitset_is_empty (&(index_entry->seg_equal_terms[nsegs])))
		    {
		      if (!bitset_is_empty (&(index_entry->seg_other_terms[nsegs])))
			{
			  nsegs++;	/* include this term */
			}
		      break;
		    }
		}

	      bitset_intersect (&seg_terms, &(QO_NODE_SARGS (node)));

	      n = 0;		/* init */

	      if (!bitset_is_empty (&seg_terms))
		{
		  assert (nsegs > 0);

		  n = qo_generate_index_scan (info, node, ni_entry, nsegs);
		  normal_index_plan_n += n;
		}
	      else if (index_entry->constraints->filter_predicate && index_entry->force > 0)
		{
		  assert (bitset_is_empty (&seg_terms));

		  /* Currently, CUBRID does not allow null values in index. The filter index expression must contain at
		   * least one term different than "is null". Otherwise, the index will be empty. Having at least one
		   * term different than "is null" in a filter index expression, the user knows from beginning that
		   * null values can't appear when scan filter index.
		   */

		  n =
		    qo_check_plan_on_info (info,
					   qo_index_scan_new (info, node, ni_entry, QO_SCANMETHOD_INDEX_SCAN,
							      &seg_terms, NULL));
		  normal_index_plan_n += n;
		}
	      else if (index_entry->ils_prefix_len > 0)
		{
		  assert (bitset_is_empty (&seg_terms));

		  n = qo_generate_loose_index_scan (info, node, ni_entry);
		  normal_index_plan_n += n;
		}
	      else
		{
		  assert (bitset_is_empty (&seg_terms));

		  /* if the index didn't normally skipped the order by, we try the new plan, maybe this will be better.
		   * DO NOT generate a order by index if there is no order by! Skip generating index from order by if
		   * multi_range_opt is true (multi range optimized plan is already better)
		   */
		  tree = QO_ENV_PT_TREE (info->env);
		  if (tree == NULL)
		    {
		      assert (false);	/* is invalid case */
		      continue;	/* nop */
		    }

		  if (tree->info.query.q.select.connect_by != NULL || qo_is_prefix_index (index_entry))
		    {
		      continue;	/* nop; go ahead */
		    }

		  /* if the index didn't normally skipped the group/order by, we try the new plan, maybe this will be
		   * better. DO NOT generate if there is no group/order by!
		   */
		  if (!n && !index_entry->groupby_skip && tree->info.query.q.select.group_by
		      && qo_validate_index_for_groupby (info->env, ni_entry))
		    {
		      n =
			qo_check_plan_on_info (info,
					       qo_index_scan_new (info, node, ni_entry,
								  QO_SCANMETHOD_INDEX_GROUPBY_SCAN, &seg_terms, NULL));
		    }

		  if (!n && !index_entry->orderby_skip && tree->info.query.order_by
		      && qo_validate_index_for_orderby (info->env, ni_entry))
		    {
		      n =
			qo_check_plan_on_info (info,
					       qo_index_scan_new (info, node, ni_entry,
								  QO_SCANMETHOD_INDEX_ORDERBY_SCAN, &seg_terms, NULL));
		    }
		}
	    }

	  bitset_delset (&seg_terms);
	}

      /*
       * Create a sequential scan plan for each node.
       */
      if (normal_index_plan_n > 0)
	{
	  /* Already generate some index scans. Skip sequential scan plan for the node. */
	  ;			/* nop */
	}
      else
	{
	  qo_generate_seq_scan (info, node);
	}

      if (QO_ENV_USE_SORT_LIMIT (planner->env) && QO_NODE_SORT_LIMIT_CANDIDATE (node))
	{
	  /* generate a stop plan over the current best plan of the */
	  QO_PLAN *best_plan;
	  best_plan = qo_find_best_plan_on_info (info, QO_UNORDERED, 1.0);
	  if (best_plan->plan_type == QO_PLANTYPE_SCAN && !qo_plan_multi_range_opt (best_plan)
	      && !qo_is_iscan_from_orderby (best_plan) && !qo_is_iscan_from_groupby (best_plan))
	    {
	      qo_generate_sort_limit_plan (planner->env, info, best_plan);
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
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, size);
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
      if (qo_search_partition (planner, &planner->partition[i], QO_UNORDERED, &remaining_subqueries) == NULL)
	{
	  for (j = 0; j < i; ++j)
	    {
	      qo_plan_del_ref (planner->partition[j].plan);
	    }
	  broken = true;
	  break;
	}
    }
  plan = broken ? NULL : qo_combine_partitions (planner, &remaining_subqueries);

  /* if we have use_desc_idx hint and order by or group by, do some checking */
  if (plan)
    {
      bool has_hint;
      PT_HINT_ENUM *hint;
      PT_NODE *node = NULL;

      if (plan->use_iscan_descending == true && qo_plan_multi_range_opt (plan) == false)
	{
	  qo_set_use_desc (plan);
	}

      tree = QO_ENV_PT_TREE (planner->env);
      assert (tree != NULL);

      hint = &(tree->info.query.q.select.hint);
      has_hint = (*hint & PT_HINT_USE_IDX_DESC) > 0;

      /* check direction of the first order by column. */
      node = tree->info.query.order_by;
      if (node != NULL)
	{
	  if (tree->info.query.q.select.connect_by)
	    {
	      ;
	    }
	  /* if we have order by and the hint, we allow the hint only if we have order by descending on first column.
	   * Otherwise we clear it */
	  else if (has_hint && node->info.sort_spec.asc_or_desc == PT_ASC)
	    {
	      *hint = (PT_HINT_ENUM) (*hint & ~PT_HINT_USE_IDX_DESC);
	    }
	}

      /* check direction of the first order by column. */
      node = tree->info.query.q.select.group_by;
      if (node != NULL)
	{
	  if (node->flag.with_rollup);
	  /* if we have group by and the hint, we allow the hint only if we have group by descending on first column.
	   * Otherwise we clear it */
	  else if (has_hint && node->info.sort_spec.asc_or_desc == PT_ASC)
	    {
	      *hint = (PT_HINT_ENUM) (*hint & ~PT_HINT_USE_IDX_DESC);
	    }
	}
    }

  /* some indexes may be marked with multi range optimization (as candidates) However, if the chosen top plan is not
   * marked as using multi range optimization it means that the optimization has been invalidated, or maybe another
   * plan was chosen. Make sure to un-mark indexes in this case
   */
  if (plan != NULL && !qo_plan_multi_range_opt (plan))
    {
      qo_walk_plan_tree (plan, qo_unset_multi_range_optimization, NULL);
      if (plan->info->env != NULL && plan->info->env->multi_range_opt_candidate)
	{
	  plan->multi_range_opt_use = PLAN_MULTI_RANGE_OPT_CAN_USE;
	}
    }

  if (plan != NULL && qo_is_interesting_order_scan (plan))
    {
      if (plan->plan_un.scan.index && plan->plan_un.scan.index->head)
	{
	  if (plan->plan_un.scan.index->head->use_descending)
	    {
	      /* We no longer need to set the USE_DESC_IDX hint if the planner wants a descending index, because the
	       * requirement is copied to each scan_ptr's index info at XASL generation.
	       * plan->info->env->pt_tree->info.query.q.select.hint |= PT_HINT_USE_IDX_DESC;
	       */
	    }
	  else if (plan->plan_un.scan.index->head->orderby_skip || qo_is_index_mro_scan (plan))
	    {
	      if (plan->info->env != NULL)
		{
		  plan->info->env->pt_tree->info.query.q.select.hint =
		    (PT_HINT_ENUM) (plan->info->env->pt_tree->info.query.q.select.hint & ~PT_HINT_USE_IDX_DESC);
		}
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
qo_search_partition_join (QO_PLANNER * planner, QO_PARTITION * partition, BITSET * remaining_subqueries)
{
  QO_ENV *env;
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

  env = planner->env;
  bitset_init (&visited_nodes, env);
  bitset_init (&visited_rel_nodes, env);
  bitset_init (&visited_terms, env);
  bitset_init (&first_nodes, env);
  bitset_init (&nested_path_nodes, env);
  bitset_init (&remaining_nodes, env);
  bitset_init (&remaining_terms, env);

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
  tree = QO_ENV_PT_TREE (env);
  hint = tree->info.query.q.select.hint;

  /* set #tables consider at a time */
  if (num_path_inner || (hint & PT_HINT_ORDERED))
    {
      /* inner join type path term exist; WHERE x.y.z = ? or there is a SQL hint ORDERED */
      planner->join_unit = nodes_cnt;	/* give up */
    }
  else
    {
      planner->join_unit = (nodes_cnt <= 25) ? MIN (4, nodes_cnt) : (nodes_cnt <= 37) ? 3 : 2;
    }

  if (num_path_inner || (hint & PT_HINT_ORDERED))
    {
      ;				/* skip and go ahead */
    }
  else
    {
      int r, f, t;
      QO_NODE *r_node, *f_node;
      QO_INFO *r_info, *f_info;
      QO_PLAN *r_plan, *f_plan;
      PT_NODE *entity;
      bool found_f_edge, found_other_edge;
      BITSET_ITERATOR bi, bj, bt;
      QO_PLAN_COMPARE_RESULT cmp;
      BITSET derived_nodes;
      BITSET idx_inner_nodes;

      bitset_init (&derived_nodes, env);
      bitset_init (&idx_inner_nodes, env);

      for (r = bitset_iterate (&remaining_nodes, &bi); r != -1; r = bitset_next_member (&bi))
	{
	  r_node = QO_ENV_NODE (env, r);
	  /* node dependency check; emptyness check */
	  if (!bitset_is_empty (&(QO_NODE_DEP_SET (r_node))))
	    {
	      continue;
	    }
	  if (!bitset_is_empty (&(QO_NODE_OUTER_DEP_SET (r_node))))
	    {
	      continue;
	    }

	  entity = QO_NODE_ENTITY_SPEC (r_node);
	  /* do not check node for inline view */
	  if (entity->info.spec.derived_table)
	    {			/* inline view */
	      bitset_add (&derived_nodes, r);
	      continue;		/* OK */
	    }

	  if (bitset_is_empty (&first_nodes))
	    {			/* the first time */
	      bitset_add (&first_nodes, r);
	      continue;		/* OK */
	    }

	  /* current prefix has only one node */
	  r_info = planner->node_info[QO_NODE_IDX (r_node)];

	  r_plan = qo_find_best_plan_on_info (r_info, QO_UNORDERED, 1.0);

	  if (qo_plan_multi_range_opt (r_plan))
	    {
	      /* skip removing the other edge from first plan */
	      bitset_add (&first_nodes, r);
	      continue;
	    }

	  BITSET_CLEAR (idx_inner_nodes);

	  for (f = bitset_iterate (&first_nodes, &bj); f != -1; f = bitset_next_member (&bj))
	    {
	      f_node = QO_ENV_NODE (env, f);
	      if ((QO_IS_LIMIT_NODE (env, r_node) && !QO_IS_LIMIT_NODE (env, f_node))
		  || (!QO_IS_LIMIT_NODE (env, r_node) && QO_IS_LIMIT_NODE (env, f_node)))
		{
		  /* Also keep the best plan from limit nodes, otherwise we might not get a chance to create a
		   * SORT-LIMIT plan
		   */
		  continue;
		}

	      /* current prefix has only one node */
	      f_info = planner->node_info[QO_NODE_IDX (f_node)];

	      f_plan = qo_find_best_plan_on_info (f_info, QO_UNORDERED, 1.0);

	      if (qo_plan_multi_range_opt (f_plan))
		{
		  /* allow adding the other edge to the first_nodes */
		  continue;
		}

	      cmp = qo_plan_cmp (f_plan, r_plan);

	      if (cmp == PLAN_COMP_LT)
		{		/* r_plan is worse */
		  if (QO_NODE_TCARD (f_node) <= 1)
		    {		/* one page heap file */
		      ;		/* f_node is always winner */
		    }
		  else
		    {
		      /* check for info cardinality */
		      if (r_info->cardinality < f_info->cardinality + 1.0)
			{
			  continue;	/* do not skip out smaller card */
			}

		      if (qo_is_iscan (r_plan))
			{
			  continue;	/* do not skip out index scan plan */
			}
		    }

		  /* do not add r_node to the first_nodes */
		  break;
		}
	      else if (cmp == PLAN_COMP_GT)
		{		/* found new first */
		  if (QO_NODE_TCARD (r_node) <= 1)
		    {		/* one page heap file */
		      ;		/* r_node is always winner */
		    }
		  else
		    {
		      /* check for info cardinality */
		      if (f_info->cardinality < r_info->cardinality + 1.0)
			{
			  continue;	/* do not skip out smaller card */
			}

		      if (qo_is_iscan (f_plan))
			{
			  continue;	/* do not skip out index scan plan */
			}
		    }

		  /* check for join-connectivity of f_node to r_node */
		  found_f_edge = found_other_edge = false;	/* init */
		  for (t = bitset_iterate (&remaining_terms, &bt); t != -1 && !found_other_edge;
		       t = bitset_next_member (&bt))
		    {
		      term = QO_ENV_TERM (env, t);

		      if (!QO_IS_EDGE_TERM (term))
			{
			  continue;
			}

		      if (!BITSET_MEMBER (QO_TERM_NODES (term), QO_NODE_IDX (f_node)))
			{
			  continue;
			}

		      /* check for f_node's edges */
		      if (BITSET_MEMBER (QO_TERM_NODES (term), QO_NODE_IDX (r_node)))
			{
			  /* edge between f_node and r_node */

			  for (i = 0; i < QO_TERM_CAN_USE_INDEX (term) && !found_f_edge; i++)
			    {
			      if (QO_NODE_IDX (QO_SEG_HEAD (QO_TERM_INDEX_SEG (term, i))) == QO_NODE_IDX (f_node))
				{
				  found_f_edge = true;	/* indexable edge */
				}
			    }
			}
		      else
			{
			  /* edge between f_node and other_node */
			  found_other_edge = true;
			}
		    }

		  if (found_f_edge && !found_other_edge)
		    {
		      bitset_add (&idx_inner_nodes, f);
		    }
		}
	    }

	  /* exclude idx-join's inner nodes from the first_nodes */
	  bitset_difference (&first_nodes, &idx_inner_nodes);

	  if (f == -1)
	    {			/* add new plan */
	      bitset_add (&first_nodes, r);
	    }
	}

      /* finally, add derived nodes to the first nodes */
      bitset_union (&first_nodes, &derived_nodes);

      bitset_delset (&idx_inner_nodes);
      bitset_delset (&derived_nodes);
    }

  /* STEP 1: do join search with visited nodes */

  node = NULL;			/* init */

  while (1)
    {
      node_idx = -1;		/* init */
      (void) planner_permutate (planner, partition, hint, node,	/* previous head node */
				&visited_nodes, &visited_rel_nodes, &visited_terms, &first_nodes, &nested_path_nodes,
				&remaining_nodes, &remaining_terms, remaining_subqueries, num_path_inner,
				(planner->join_unit < nodes_cnt) ? &node_idx
				/* partial join search */
				: NULL /* total join search */ );
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
	      /* something wrong for partial join search; rollback and retry total join search */
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

      /* at here, still do partial join search */

      /* extract the outermost nodes at this join level */
      node = QO_ENV_NODE (env, node_idx);
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
	  visited_info = planner->join_info[QO_INFO_INDEX (QO_PARTITION_M_OFFSET (partition), visited_rel_nodes)];
	}

      if (visited_info == NULL)
	{			/* something wrong */
	  break;		/* give up */
	}

      bitset_assign (&visited_terms, &(visited_info->terms));
      bitset_difference (&remaining_terms, &(visited_info->terms));

      planner->join_unit++;	/* increase join unit level */

    }

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
qo_search_partition (QO_PLANNER * planner, QO_PARTITION * partition, QO_EQCLASS * order, BITSET * remaining_subqueries)
{
  int i, nodes_cnt;

  nodes_cnt = bitset_cardinality (&(QO_PARTITION_NODES (partition)));

  /* nodes are multi if there is a join to be done. If not, this is just a degenerate search to determine which of the
   * indexes (if available) to use for the (single) class involved in the query.
   */
  if (nodes_cnt > 1)
    {
      planner->best_info = qo_search_partition_join (planner, partition, remaining_subqueries);
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

  if (planner->best_info)
    {
      QO_PARTITION_PLAN (partition) = qo_plan_finalize (qo_find_best_plan_on_info (planner->best_info, order, 1.0));
    }
  else
    {
      QO_PARTITION_PLAN (partition) = NULL;
    }

  /* Now clean up after ourselves.  Free all of the plans that aren't part of the winner for this partition, but retain
   * the nodes: they contain information that the winning plan requires.
   */

  if (nodes_cnt > 1)
    {
      QO_INFO *info;

      for (info = planner->info_list; info; info = info->next)
	{
	  if (bitset_subset (&(QO_PARTITION_NODES (partition)), &(info->nodes)))
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
	  if (bitset_intersects (&(QO_PARTITION_NODES (i_part)), &(QO_PARTITION_DEPENDENCIES (j_part))))
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

      planner->cp_info[i] = qo_alloc_info (planner, &nodes, &terms, &eqclasses, cardinality);

      for (t = planner->E; t < (signed) planner->T; ++t)
	{
	  if (!bitset_is_empty (&(QO_TERM_NODES (&planner->term[t]))) && !BITSET_MEMBER (terms, t)
	      && bitset_subset (&nodes, &(QO_TERM_NODES (&planner->term[t])))
	      && (QO_TERM_CLASS (&planner->term[t]) != QO_TC_TOTALLY_AFTER_JOIN))
	    {
	      bitset_add (&sarged_terms, t);
	    }
	}

      BITSET_CLEAR (subqueries);
      for (s = bitset_iterate (reamining_subqueries, &bi); s != -1; s = bitset_next_member (&bi))
	{
	  QO_SUBQUERY *subq = &planner->subqueries[s];
	  if (bitset_subset (&nodes, &(subq->nodes)) && bitset_subset (&sarged_terms, &(subq->terms)))
	    {
	      bitset_add (&subqueries, s);
	      bitset_remove (reamining_subqueries, s);
	    }
	}

      plan = qo_cp_new (planner->cp_info[i], plan, next_plan, &sarged_terms, &subqueries);
      qo_detach_info (planner->cp_info[i]);
      BITSET_CLEAR (sarged_terms);
    }

  /*
   * Now finalize the topmost node of the tree.
   */
  if (plan != NULL)
    {
      qo_plan_finalize (plan);
    }

  for (i = planner->E; i < (signed) planner->T; ++i)
    {
      if (bitset_is_empty (&(QO_TERM_NODES (&planner->term[i]))))
	{
	  bitset_add (&sarged_terms, i);
	}
    }

  /* skip empty sort plan */
  for (t_plan = plan; t_plan && t_plan->plan_type == QO_PLANTYPE_SORT; t_plan = t_plan->plan_un.sort.subplan)
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
  else if (plan != NULL)
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
  double lhs_selectivity, rhs_selectivity, selectivity, total_selectivity;
  PT_NODE *node;

  QO_ASSERT (env, pt_expr != NULL);
  QO_ASSERT (env, pt_expr->node_type == PT_EXPR);

  selectivity = 0.0;
  total_selectivity = 0.0;

  /* traverse OR list */
  for (node = pt_expr; node; node = node->or_next)
    {
      switch (node->info.expr.op)
	{
	case PT_OR:
	  lhs_selectivity = qo_expr_selectivity (env, node->info.expr.arg1);
	  rhs_selectivity = qo_expr_selectivity (env, node->info.expr.arg2);
	  selectivity = qo_or_selectivity (env, lhs_selectivity, rhs_selectivity);
	  break;

	case PT_AND:
	  lhs_selectivity = qo_expr_selectivity (env, node->info.expr.arg1);
	  rhs_selectivity = qo_expr_selectivity (env, node->info.expr.arg2);
	  selectivity = qo_and_selectivity (env, lhs_selectivity, rhs_selectivity);
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
	  selectivity = (double) prm_get_float_value (PRM_ID_LIKE_TERM_SELECTIVITY);
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

      total_selectivity = qo_or_selectivity (env, total_selectivity, selectivity);
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

  QO_ASSERT (env, lhs_sel >= 0.0);
  QO_ASSERT (env, lhs_sel <= 1.0);
  QO_ASSERT (env, rhs_sel >= 0.0);
  QO_ASSERT (env, rhs_sel <= 1.0);

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

  QO_ASSERT (env, lhs_sel >= 0.0);
  QO_ASSERT (env, lhs_sel <= 1.0);
  QO_ASSERT (env, rhs_sel >= 0.0);
  QO_ASSERT (env, rhs_sel <= 1.0);

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
  QO_ASSERT (env, sel >= 0.0);
  QO_ASSERT (env, sel <= 1.0);

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
  PT_NODE *lhs, *rhs, *multi_attr;
  PRED_CLASS pc_lhs, pc_rhs;
  int lhs_icard, rhs_icard, icard;
  double selectivity;

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

	  icard = MAX (lhs_icard, rhs_icard);
	  if (icard != 0)
	    {
	      selectivity = (1.0 / icard);
	    }
	  else
	    {
	      selectivity = DEFAULT_EQUIJOIN_SELECTIVITY;
	    }

	  break;

	case PC_CONST:
	case PC_HOST_VAR:
	case PC_SUBQUERY:
	case PC_SET:
	case PC_OTHER:
	  /* attr = const */

	  /* check for index on the attribute.  NOTE: For an equality predicate, we treat subqueries as constants. */
	  lhs_icard = qo_index_cardinality (env, lhs);
	  if (lhs_icard != 0)
	    {
	      selectivity = (1.0 / lhs_icard);
	    }
	  else
	    {
	      selectivity = DEFAULT_EQUAL_SELECTIVITY;
	    }

	  break;

	case PC_MULTI_ATTR:
	  /* attr = (attr,attr) syntactic impossible case */
	  selectivity = DEFAULT_EQUAL_SELECTIVITY;
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

	  /* check for index on the attribute.  NOTE: For an equality predicate, we treat subqueries as constants. */
	  rhs_icard = qo_index_cardinality (env, rhs);
	  if (rhs_icard != 0)
	    {
	      selectivity = (1.0 / rhs_icard);
	    }
	  else
	    {
	      selectivity = DEFAULT_EQUAL_SELECTIVITY;
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

	case PC_MULTI_ATTR:
	  /* const = (attr,attr) */
	  multi_attr = rhs->info.function.arg_list;
	  rhs_icard = 0;
	  for ( /* none */ ; multi_attr; multi_attr = multi_attr->next)
	    {
	      /* get index cardinality */
	      icard = qo_index_cardinality (env, multi_attr);
	      if (icard <= 0)
		{
		  /* the only interesting case is PT_BETWEEN_EQ_NA */
		  icard = 1 / DEFAULT_EQUAL_SELECTIVITY;
		}
	      if (rhs_icard == 0)
		{
		  /* first time */
		  rhs_icard = icard;
		}
	      else
		{
		  rhs_icard *= icard;
		}
	    }
	  if (rhs_icard != 0)
	    {
	      selectivity = (1.0 / rhs_icard);
	    }
	  else
	    {
	      selectivity = DEFAULT_EQUAL_SELECTIVITY;
	    }
	  break;
	}
      break;

    case PC_MULTI_ATTR:
      switch (pc_rhs)
	{
	case PC_ATTR:
	  /* (attr,attr) = attr  syntactic impossible case */
	  selectivity = DEFAULT_EQUAL_SELECTIVITY;
	  break;

	case PC_CONST:
	case PC_HOST_VAR:
	case PC_SUBQUERY:
	case PC_SET:
	case PC_OTHER:
	  /* (attr,attr) = const */

	  multi_attr = lhs->info.function.arg_list;
	  lhs_icard = 0;
	  for ( /* none */ ; multi_attr; multi_attr = multi_attr->next)
	    {
	      /* get index cardinality */
	      icard = qo_index_cardinality (env, multi_attr);
	      if (icard <= 0)
		{
		  /* the only interesting case is PT_BETWEEN_EQ_NA */
		  icard = 1 / DEFAULT_EQUAL_SELECTIVITY;
		}
	      if (lhs_icard == 0)
		{
		  /* first time */
		  lhs_icard = icard;
		}
	      else
		{
		  lhs_icard *= icard;
		}
	    }
	  if (lhs_icard != 0)
	    {
	      selectivity = (1.0 / lhs_icard);
	    }
	  else
	    {
	      selectivity = DEFAULT_EQUAL_SELECTIVITY;
	    }
	  break;

	case PC_MULTI_ATTR:
	  /* (attr,attr) = (attr,attr) */
	  multi_attr = lhs->info.function.arg_list;
	  lhs_icard = 0;
	  for ( /* none */ ; multi_attr; multi_attr = multi_attr->next)
	    {
	      /* get index cardinality */
	      icard = qo_index_cardinality (env, multi_attr);
	      if (icard <= 0)
		{
		  /* the only interesting case is PT_BETWEEN_EQ_NA */
		  icard = 1 / DEFAULT_EQUAL_SELECTIVITY;
		}
	      if (lhs_icard == 0)
		{
		  /* first time */
		  lhs_icard = icard;
		}
	      else
		{
		  lhs_icard *= icard;
		}
	    }

	  multi_attr = rhs->info.function.arg_list;
	  rhs_icard = 0;
	  for ( /* none */ ; multi_attr; multi_attr = multi_attr->next)
	    {
	      /* get index cardinality */
	      icard = qo_index_cardinality (env, multi_attr);
	      if (icard <= 0)
		{
		  /* the only interesting case is PT_BETWEEN_EQ_NA */
		  icard = 1 / DEFAULT_EQUAL_SELECTIVITY;
		}
	      if (rhs_icard == 0)
		{
		  /* first time */
		  rhs_icard = icard;
		}
	      else
		{
		  rhs_icard *= icard;
		}
	    }

	  icard = MAX (lhs_icard, rhs_icard);
	  if (icard != 0)
	    {
	      selectivity = (1.0 / icard);
	    }
	  else
	    {
	      selectivity = DEFAULT_EQUIJOIN_SELECTIVITY;
	    }
	  break;
	}

      break;
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
  PT_NODE *and_node;

  and_node = pt_expr->info.expr.arg2;

  QO_ASSERT (env, and_node->node_type == PT_EXPR);
  QO_ASSERT (env, pt_is_between_range_op (and_node->info.expr.op));

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
  double total_selectivity, selectivity;
  int lhs_icard = 0, rhs_icard = 0, icard = 0;
  PT_NODE *range_node;
  PT_OP_TYPE op_type;

  lhs = pt_expr->info.expr.arg1;

  pc2 = qo_classify (lhs);

  /* the only interesting case is 'attr RANGE {=1,=2}' or '(attr,attr) RANGE {={..},..}' */
  if (pc2 == PC_MULTI_ATTR)
    {
      lhs = lhs->info.function.arg_list;
      lhs_icard = 0;
      for ( /* none */ ; lhs; lhs = lhs->next)
	{
	  /* get index cardinality */
	  icard = qo_index_cardinality (env, lhs);
	  if (icard <= 0)
	    {
	      /* the only interesting case is PT_BETWEEN_EQ_NA */
	      icard = 1 / DEFAULT_EQUAL_SELECTIVITY;
	    }
	  if (lhs_icard == 0)
	    {
	      /* first time */
	      lhs_icard = icard;
	    }
	  else
	    {
	      lhs_icard *= icard;
	    }
	}
    }
  else if (pc2 == PC_ATTR)
    {
      /* get index cardinality */
      lhs_icard = qo_index_cardinality (env, lhs);
    }
  else
    {
      return DEFAULT_RANGE_SELECTIVITY;
    }
#if 1				/* unused anymore - DO NOT DELETE ME */
  QO_ASSERT (env, !PT_EXPR_INFO_IS_FLAGED (pt_expr, PT_EXPR_INFO_FULL_RANGE));
#endif

  total_selectivity = 0.0;

  for (range_node = pt_expr->info.expr.arg2; range_node; range_node = range_node->or_next)
    {
      QO_ASSERT (env, range_node->node_type == PT_EXPR);

      op_type = range_node->info.expr.op;
      QO_ASSERT (env, pt_is_between_range_op (op_type));

      arg1 = range_node->info.expr.arg1;
      arg2 = range_node->info.expr.arg2;

      pc1 = qo_classify (arg1);

      if (op_type == PT_BETWEEN_GE_LE || op_type == PT_BETWEEN_GE_LT || op_type == PT_BETWEEN_GT_LE
	  || op_type == PT_BETWEEN_GT_LT)
	{
	  selectivity = DEFAULT_BETWEEN_SELECTIVITY;
	}
      else if (op_type == PT_BETWEEN_EQ_NA)
	{
	  /* PT_BETWEEN_EQ_NA have only one argument */

	  selectivity = DEFAULT_EQUAL_SELECTIVITY;

	  if (pc1 == PC_ATTR)
	    {
	      /* attr1 range (attr2 = ) */
	      rhs_icard = qo_index_cardinality (env, arg1);

	      icard = MAX (lhs_icard, rhs_icard);
	      if (icard != 0)
		{
		  selectivity = (1.0 / icard);
		}
	      else
		{
		  selectivity = DEFAULT_EQUIJOIN_SELECTIVITY;
		}
	    }
	  else
	    {
	      /* attr1 range (const = ) */
	      if (lhs_icard != 0)
		{
		  selectivity = (1.0 / lhs_icard);
		}
	      else
		{
		  selectivity = DEFAULT_EQUAL_SELECTIVITY;
		}
	    }
	}
      else
	{
	  /* PT_BETWEEN_INF_LE, PT_BETWEEN_INF_LT, PT_BETWEEN_GE_INF, and PT_BETWEEN_GT_INF have only one argument */

	  selectivity = DEFAULT_COMP_SELECTIVITY;
	}

      selectivity = MAX (selectivity, 0.0);
      selectivity = MIN (selectivity, 1.0);

      total_selectivity = qo_or_selectivity (env, total_selectivity, selectivity);
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
  double list_card = 0, icard;
  PT_NODE *lhs;
  double equal_selectivity, in_selectivity, selectivity;

  /* determine the class of each side of the range */
  pc_lhs = qo_classify (pt_expr->info.expr.arg1);
  pc_rhs = qo_classify (pt_expr->info.expr.arg2);

  /* The only interesting cases are: attr IN set or (attr,attr) IN set or attr IN subquery */
  if ((pc_lhs == PC_MULTI_ATTR || pc_lhs == PC_ATTR) && (pc_rhs == PC_SET || pc_rhs == PC_SUBQUERY))
    {
      if (pc_lhs == PC_MULTI_ATTR)
	{
	  lhs = pt_expr->info.expr.arg1->info.function.arg_list;
	  equal_selectivity = 1;
	  for ( /* none */ ; lhs; lhs = lhs->next)
	    {
	      /* get index cardinality */
	      icard = qo_index_cardinality (env, lhs);
	      if (icard != 0)
		{
		  selectivity = (1.0 / icard);
		}
	      else
		{
		  selectivity = DEFAULT_EQUAL_SELECTIVITY;
		}
	      equal_selectivity *= selectivity;
	    }
	}
      else if (pc_lhs == PC_ATTR)
	{
	  /* check for index on the attribute.  */
	  icard = qo_index_cardinality (env, pt_expr->info.expr.arg1);

	  if (icard != 0)
	    {
	      equal_selectivity = (1.0 / icard);
	    }
	  else
	    {
	      equal_selectivity = DEFAULT_EQUAL_SELECTIVITY;
	    }
	}
      /* determine cardinality of set or subquery */
      if (pc_rhs == PC_SET)
	{
	  if (pt_is_function (pt_expr->info.expr.arg2))
	    {
	      list_card = pt_length_of_list (pt_expr->info.expr.arg2->info.function.arg_list);
	    }
	  else
	    {
	      list_card = pt_length_of_list (pt_expr->info.expr.arg2->info.value.data_value.set);
	    }
	}
      else if (pc_rhs == PC_SUBQUERY)
	{
	  if (pt_expr->info.expr.arg2->info.query.xasl)
	    {
	      list_card = ((XASL_NODE *) pt_expr->info.expr.arg2->info.query.xasl)->cardinality;
	    }
	  else
	    {
	      /* legacy default list_card is 1000. Maybe it won't come in here */
	      list_card = 1000;
	    }
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
      if (PT_IS_SET_TYPE (attr))
	{
	  return PC_SET;
	}
      else if (attr->type_enum == PT_TYPE_NULL)
	{
	  return PC_OTHER;
	}
      else
	{
	  return PC_CONST;
	}

    case PT_HOST_VAR:
      return PC_HOST_VAR;

    case PT_SELECT:
    case PT_UNION:
    case PT_INTERSECTION:
    case PT_DIFFERENCE:
      return PC_SUBQUERY;

    case PT_FUNCTION:
      /* (attr,attr) or (?,?) */
      if (PT_IS_SET_TYPE (attr))
	{
	  PT_NODE *func_arg;
	  func_arg = attr->info.function.arg_list;
	  for ( /* none */ ; func_arg; func_arg = func_arg->next)
	    {
	      if (func_arg->node_type == PT_NAME)
		{
		  /* none */
		}
	      else if (func_arg->node_type == PT_HOST_VAR)
		{
		  return PC_SET;
		}
	      else
		{
		  return PC_OTHER;
		}
	    }
	  return PC_MULTI_ATTR;
	}
      else
	{
	  return PC_OTHER;
	}

    default:
      return PC_OTHER;
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

  if (attr->info.name.meta_class == PT_RESERVED)
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

  QO_ASSERT (env, info->cum_stats.pkeys_size > 0);
  QO_ASSERT (env, info->cum_stats.pkeys_size <= BTREE_STATS_PKEYS_NUM);
  QO_ASSERT (env, info->cum_stats.pkeys != NULL);

  /* return number of the first partial-key of the index on the attribute shown in the expression */
  return info->cum_stats.pkeys[0];
}

/*
 * qo_is_all_unique_index_columns_are_equi_terms () -
 *   check if the current plan uses and
 *   index scan with all_unique_index_columns_are_equi_terms
 *
 * return    : true/false
 * plan (in) : plan to verify
 */
bool
qo_is_all_unique_index_columns_are_equi_terms (QO_PLAN * plan)
{
  if (qo_is_iscan (plan) && plan->plan_un.scan.index && plan->plan_un.scan.index->head
      && (plan->plan_un.scan.index->head->all_unique_index_columns_are_equi_terms))
    {
      return true;
    }
  return false;
}

/*
 * qo_is_iscan_from_orderby ()
 *   return: true/false
 *   plan(in):
 */
bool
qo_is_iscan_from_orderby (QO_PLAN * plan)
{
  if (plan && plan->plan_type == QO_PLANTYPE_SCAN && plan->plan_un.scan.scan_method == QO_SCANMETHOD_INDEX_ORDERBY_SCAN)
    {
      return true;
    }

  return false;
}

/*
 * qo_validate_index_term_notnull ()
 *   return: true/false
 */
static bool
qo_validate_index_term_notnull (QO_ENV * env, QO_INDEX_ENTRY * index_entryp)
{
  bool term_notnull = false;	/* init */
  PT_NODE *node;
  const char *node_name;
  QO_CLASS_INFO_ENTRY *index_class;
  int t;
  QO_TERM *termp;
  int iseg;
  QO_SEGMENT *segp;

  assert (env != NULL);
  assert (index_entryp != NULL);
  assert (index_entryp->class_ != NULL);

  index_class = index_entryp->class_;

  /* do a check on the first column - it should be present in the where clause check if exists a simple expression
   * with PT_IS_NOT_NULL on the first key this should not contain OR operator and the PT_IS_NOT_NULL should contain the
   * column directly as parameter (PT_NAME)
   */
  for (t = 0; t < env->nterms && !term_notnull; t++)
    {
      /* get the pointer to QO_TERM structure */
      termp = QO_ENV_TERM (env, t);
      assert (termp != NULL);
      if (QO_ON_COND_TERM (termp))
	{
	  continue;
	}

      node = QO_TERM_PT_EXPR (termp);
      if (node == NULL)
	{
	  continue;
	}

      if (node && node->or_next)
	{
	  continue;
	}

      if (node->node_type == PT_EXPR && node->info.expr.op == PT_IS_NOT_NULL
	  && node->info.expr.arg1->node_type == PT_NAME)
	{
	  iseg = index_entryp->seg_idxs[0];
	  if (iseg != -1 && BITSET_MEMBER (QO_TERM_SEGS (termp), iseg))
	    {
	      /* check it's the same column as the first in the index */
	      node_name = pt_get_name (node->info.expr.arg1);
	      segp = QO_ENV_SEG (env, iseg);
	      assert (segp != NULL);
	      if (!intl_identifier_casecmp (node_name, QO_SEG_NAME (segp)))
		{
		  /* we have found a term with no OR and with IS_NOT_NULL on our key. The plan is ready for group by
		   * skip!
		   */
		  term_notnull = true;
		  break;
		}
	    }
	}
    }

  return term_notnull;
}

/*
 * qo_validate_index_attr_notnull ()
 *   return: true/false
 */
static bool
qo_validate_index_attr_notnull (QO_ENV * env, QO_INDEX_ENTRY * index_entryp, PT_NODE * col)
{
  bool attr_notnull = false;	/* init */
  QO_NODE *node;
  PT_NODE *dummy;
  int i;
  QO_CLASS_INFO_ENTRY *index_class;
  QO_SEGMENT *segp = NULL;
  SM_ATTRIBUTE *attr;
  void *env_seg[2];

  /* key_term_status is -1 if no term with key, 0 if isnull or is not null terms with key and 1 if other term with key */
  int old_bail_out, key_term_status;

  assert (env != NULL);
  assert (index_entryp != NULL);
  assert (index_entryp->class_ != NULL);
  assert (index_entryp->class_->smclass != NULL);
  assert (col != NULL);

  index_class = index_entryp->class_;

  if (col->node_type != PT_NAME)
    {
      return false;		/* give up */
    }

  node = lookup_node (col, env, &dummy);
  if (node == NULL)
    {
      return false;
    }

  segp = lookup_seg (node, col, env);
  if (segp == NULL)
    {				/* is invalid case */
      assert (false);
      return false;
    }

  for (i = 0; i < index_entryp->col_num; i++)
    {
      if (index_entryp->seg_idxs[i] == QO_SEG_IDX (segp))
	{
	  break;		/* found */
	}
    }
  if (i >= index_entryp->col_num)
    {
      /* col is not included in this index */
      return false;
    }

  assert (segp != NULL);
#if !defined(NDEBUG)
  {
    const char *col_name = pt_get_name (col);

    assert (!intl_identifier_casecmp (QO_SEG_NAME (segp), col_name));
  }
#endif

  /* we now search in the class columns for the index key */
  for (i = 0; i < index_class->smclass->att_count; i++)
    {
      attr = &index_class->smclass->attributes[i];
      if (attr && !intl_identifier_casecmp (QO_SEG_NAME (segp), attr->header.name))
	{
	  if (attr->flags & SM_ATTFLAG_NON_NULL)
	    {
	      attr_notnull = true;
	    }
	  else
	    {
	      attr_notnull = false;
	    }

	  break;
	}
    }
  if (i >= index_class->smclass->att_count)
    {
      /* column wasn't found - this should not happen! */
      assert (false);
      return false;
    }

  /* now search for not terms with the key */
  if (attr_notnull != true)
    {
      /* save old value of bail_out */
      old_bail_out = env->bail_out;
      env->bail_out = -1;	/* no term found value */

      /* check for isnull terms with the key */
      env_seg[0] = (void *) env;
      env_seg[1] = (void *) segp;
      parser_walk_tree (env->parser, QO_ENV_PT_TREE (env)->info.query.q.select.where, qo_search_isnull_key_expr,
			env_seg, NULL, NULL);

      /* restore old value and keep walk_tree result in key_term_status */
      key_term_status = env->bail_out;
      env->bail_out = old_bail_out;

      /* if there is no isnull on the key, check that the key appears in some term and if so, make sure that that term
       * doesn't have a OR
       */
      if (key_term_status == 1)
	{
	  BITSET expr_segments, key_segment;
	  QO_TERM *termp;
	  PT_NODE *pt_expr;

	  bitset_init (&expr_segments, env);
	  bitset_init (&key_segment, env);

	  /* key segment bitset */
	  bitset_add (&key_segment, QO_SEG_IDX (segp));

	  /* key found in a term */
	  for (i = 0; i < env->nterms; i++)
	    {
	      termp = QO_ENV_TERM (env, i);
	      assert (termp != NULL);

	      pt_expr = QO_TERM_PT_EXPR (termp);
	      if (pt_expr == NULL)
		{
		  continue;
		}

	      if (pt_expr->or_next)
		{
		  BITSET_CLEAR (expr_segments);

		  qo_expr_segs (env, pt_expr, &expr_segments);
		  if (bitset_intersects (&expr_segments, &key_segment))
		    {
		      break;	/* give up */
		    }
		}
	    }

	  if (i >= env->nterms)
	    {
	      attr_notnull = true;	/* OK */
	    }

	  bitset_delset (&key_segment);
	  bitset_delset (&expr_segments);
	}
    }

  return attr_notnull;
}

/*
 * qo_validate_index_for_orderby () - checks for isnull(key) or not null flag
 *  env(in): pointer to the optimizer environment
 *  ni_entryp(in): pointer to QO_NODE_INDEX_ENTRY (node index entry)
 *  return: 1 if the index can be used, 0 elseware
 */
static int
qo_validate_index_for_orderby (QO_ENV * env, QO_NODE_INDEX_ENTRY * ni_entryp)
{
  bool key_notnull = false;	/* init */
  QO_INDEX_ENTRY *index_entryp;
  QO_CLASS_INFO_ENTRY *index_class;

  int pos;
  PT_NODE *node = NULL;

  assert (ni_entryp != NULL);
  assert (ni_entryp->head != NULL);
  assert (ni_entryp->head->class_ != NULL);

  index_entryp = ni_entryp->head;
  index_class = index_entryp->class_;

  if (!QO_ENV_PT_TREE (env) || !QO_ENV_PT_TREE (env)->info.query.order_by)
    {
      goto end;
    }

  key_notnull = qo_validate_index_term_notnull (env, index_entryp);
  if (key_notnull)
    {
      goto final_;
    }

  pos = QO_ENV_PT_TREE (env)->info.query.order_by->info.sort_spec.pos_descr.pos_no;
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

  if (node->node_type == PT_EXPR && node->info.expr.op == PT_CAST)
    {
      node = node->info.expr.arg1;
      if (!node)
	{
	  goto end;
	}
    }

  node = pt_get_end_path_node (node);

  assert (key_notnull == false);

  key_notnull = qo_validate_index_attr_notnull (env, index_entryp, node);
  if (key_notnull)
    {
      goto final_;
    }

  /* Now we have the information we need: if the key column can be null and if there is a PT_IS_NULL or PT_IS_NOT_NULL
   * expression with this key column involved and also if we have other terms with the key. We must decide if there can
   * be NULLs in the results and if so, drop this index. 1. If the key cannot have null values, we have a winner. 2.
   * Otherwise, if we found a term isnull/isnotnull(key) we drop it (because we cannot evaluate if this yields true or
   * false so we skip all, for safety) 3. If we have a term with other operator except isnull/isnotnull and does not
   * have an OR following we have a winner again! (because we cannot have a null value).
   */
final_:
  if (key_notnull)
    {
      return 1;
    }
end:
  return 0;
}

/*
 * qo_search_isnull_key_expr () -
 *   return: PT_NODE *
 *   parser(in): parser environment
 *   tree(in): tree to walk
 *   arg(in):
 *   continue_walk(in):
 *
 * Note: for env->bail_out values, check key_term_status in
 *	  qo_validate_index_for_groupby, qo_validate_index_for_orderby
 */
static PT_NODE *
qo_search_isnull_key_expr (PARSER_CONTEXT * parser, PT_NODE * tree, void *arg, int *continue_walk)
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
	  if (tree->info.expr.op == PT_IS_NULL || tree->info.expr.op == PT_IS_NOT_NULL
	      || tree->info.expr.op == PT_IFNULL || tree->info.expr.op == PT_NULLSAFE_EQ)
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
 * qo_plan_iscan_terms_cmp () - compare 2 index scan plans with terms
 *   return:  one of {PLAN_COMP_UNK, PLAN_COMP_LT, PLAN_COMP_EQ, PLAN_COMP_GT}
 *   a(in):
 *   b(in):
 */
static QO_PLAN_COMPARE_RESULT
qo_plan_iscan_terms_cmp (QO_PLAN * a, QO_PLAN * b)
{
  QO_NODE_INDEX_ENTRY *a_ni, *b_ni;
  QO_INDEX_ENTRY *a_ent, *b_ent;
  QO_ATTR_CUM_STATS *a_cum, *b_cum;
  int a_range, b_range;		/* num iscan range terms */
  int a_filter, b_filter;	/* num iscan filter terms */

  if (!qo_is_interesting_order_scan (a) || !qo_is_interesting_order_scan (b))
    {
      assert_release (qo_is_interesting_order_scan (a));
      assert_release (qo_is_interesting_order_scan (b));

      return PLAN_COMP_UNK;
    }

  /* index entry of spec 'a' */
  a_ni = a->plan_un.scan.index;
  a_ent = (a_ni)->head;
  a_cum = &(a_ni)->cum_stats;
  assert (a_cum != NULL);

  /* index range terms */
  a_range = bitset_cardinality (&(a->plan_un.scan.terms));
  if (a_range > 0 && !(a->plan_un.scan.index_equi))
    {
      a_range--;		/* set the last equal range term */
    }

  /* index filter terms */
  a_filter = bitset_cardinality (&(a->plan_un.scan.kf_terms));

  /* index entry of spec 'b' */
  b_ni = b->plan_un.scan.index;
  b_ent = (b_ni)->head;
  b_cum = &(b_ni)->cum_stats;
  assert (b_cum != NULL);

  /* index range terms */
  b_range = bitset_cardinality (&(b->plan_un.scan.terms));
  if (b_range > 0 && !(b->plan_un.scan.index_equi))
    {
      b_range--;		/* set the last equal range term */
    }

  /* index filter terms */
  b_filter = bitset_cardinality (&(b->plan_un.scan.kf_terms));

  assert (a_range >= 0);
  assert (b_range >= 0);

  /* STEP 1: check by terms containment */

  if (bitset_is_equivalent (&(a->plan_un.scan.terms), &(b->plan_un.scan.terms)))
    {
      /* both plans have the same range terms we will check now the key filter terms */
      if (a_filter > b_filter)
	{
	  return PLAN_COMP_LT;
	}
      else if (a_filter < b_filter)
	{
	  return PLAN_COMP_GT;
	}

      /* both have the same range terms and same number of filters */
      if (a_cum && b_cum)
	{
	  /* take the smaller index pages */
	  if (a_cum->pages < b_cum->pages)
	    {
	      return PLAN_COMP_LT;
	    }
	  else if (a_cum->pages > b_cum->pages)
	    {
	      return PLAN_COMP_GT;
	    }

	  assert (a_cum->pkeys_size <= BTREE_STATS_PKEYS_NUM);
	  assert (b_cum->pkeys_size <= BTREE_STATS_PKEYS_NUM);

	  /* take the smaller index pkeys_size */
	  if (a_cum->pkeys_size < b_cum->pkeys_size)
	    {
	      return PLAN_COMP_LT;
	    }
	  else if (a_cum->pkeys_size > b_cum->pkeys_size)
	    {
	      return PLAN_COMP_GT;
	    }
	}

      /* both have the same number of index pages and pkeys_size */
      return PLAN_COMP_EQ;
    }
  else if (a_range > 0 && bitset_subset (&(a->plan_un.scan.terms), &(b->plan_un.scan.terms)))
    {
      return PLAN_COMP_LT;
    }
  else if (b_range > 0 && bitset_subset (&(b->plan_un.scan.terms), &(a->plan_un.scan.terms)))
    {
      return PLAN_COMP_GT;
    }

  /* STEP 2: check by term cardinality */

  if (a->plan_un.scan.index_equi == b->plan_un.scan.index_equi)
    {
      if (a_range > b_range)
	{
	  return PLAN_COMP_LT;
	}
      else if (a_range < b_range)
	{
	  return PLAN_COMP_GT;
	}

      assert (a_range == b_range);

      /* both plans have the same number of range terms we will check now the key filter terms */
      if (a_filter > b_filter)
	{
	  return PLAN_COMP_LT;
	}
      else if (a_filter < b_filter)
	{
	  return PLAN_COMP_GT;
	}

      /* both have the same number of range terms and same number of filters */
      return PLAN_COMP_EQ;
    }

  return PLAN_COMP_EQ;		/* is equal with terms not cost */
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

  if (!qo_is_interesting_order_scan (a) || !qo_is_interesting_order_scan (b))
    {
      return PLAN_COMP_UNK;
    }

  a_ent = a->plan_un.scan.index->head;
  b_ent = b->plan_un.scan.index->head;

  if (a_ent == NULL || b_ent == NULL)
    {
      assert (false);
      return PLAN_COMP_UNK;
    }

  if (qo_is_index_iss_scan (a) || qo_is_index_loose_scan (a))
    {
      return PLAN_COMP_UNK;
    }

  if (qo_is_index_iss_scan (b) || qo_is_index_loose_scan (b))
    {
      return PLAN_COMP_UNK;
    }

  if (a_ent->groupby_skip)
    {
      if (b_ent->groupby_skip)
	{
	  return qo_plan_iscan_terms_cmp (a, b);
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

  if (!qo_is_interesting_order_scan (a) || !qo_is_interesting_order_scan (b))
    {
      return PLAN_COMP_UNK;
    }

  a_ent = a->plan_un.scan.index->head;
  b_ent = b->plan_un.scan.index->head;

  if (a_ent == NULL || b_ent == NULL)
    {
      assert (false);
      return PLAN_COMP_UNK;
    }

  if (qo_is_index_iss_scan (a) || qo_is_index_loose_scan (a))
    {
      return PLAN_COMP_UNK;
    }

  if (qo_is_index_iss_scan (b) || qo_is_index_loose_scan (b))
    {
      return PLAN_COMP_UNK;
    }

  if (a_ent->orderby_skip)
    {
      if (b_ent->orderby_skip)
	{
	  return qo_plan_iscan_terms_cmp (a, b);
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
      return false;
    }
  if (plan->info)
    {
      env = plan->info->env;
    }

  if (env == NULL)
    {
      return false;
    }

  tree = QO_ENV_PT_TREE (env);

  if (tree == NULL)
    {
      return false;
    }

  if (tree->info.query.q.select.hint & PT_HINT_NO_IDX_DESC)
    {
      return false;
    }

  order_by = tree->info.query.order_by;

  for (trav = plan->iscan_sort_list; trav; trav = trav->next)
    {
      /* change PT_ASC to PT_DESC and vice-versa */
      trav->info.sort_spec.asc_or_desc = (PT_MISC_TYPE) (PT_ASC + PT_DESC - trav->info.sort_spec.asc_or_desc);
    }

  /* test again the order by skip */
  orderby_skip = pt_sort_spec_cover (plan->iscan_sort_list, order_by);

  /* change back directions */
  for (trav = plan->iscan_sort_list; trav; trav = trav->next)
    {
      /* change PT_ASC to PT_DESC and vice-versa */
      trav->info.sort_spec.asc_or_desc = (PT_MISC_TYPE) (PT_ASC + PT_DESC - trav->info.sort_spec.asc_or_desc);
    }

  return orderby_skip;
}

/*
 * qo_is_iscan_from_groupby ()
 *   return: true/false
 *   plan(in):
 */
bool
qo_is_iscan_from_groupby (QO_PLAN * plan)
{
  if (plan && plan->plan_type == QO_PLANTYPE_SCAN && plan->plan_un.scan.scan_method == QO_SCANMETHOD_INDEX_GROUPBY_SCAN)
    {
      return true;
    }

  return false;
}

/*
 * qo_validate_index_for_groupby () - checks for isnull(key) or not null flag
 *  env(in): pointer to the optimizer environment
 *  ni_entryp(in): pointer to QO_NODE_INDEX_ENTRY (node index entry)
 *  return: 1 if the index can be used, 0 elseware
 */
static int
qo_validate_index_for_groupby (QO_ENV * env, QO_NODE_INDEX_ENTRY * ni_entryp)
{
  bool key_notnull = false;	/* init */
  QO_INDEX_ENTRY *index_entryp;
  QO_CLASS_INFO_ENTRY *index_class;

  PT_NODE *groupby_expr = NULL;

  assert (ni_entryp != NULL);
  assert (ni_entryp->head != NULL);
  assert (ni_entryp->head->class_ != NULL);

  index_entryp = ni_entryp->head;
  index_class = index_entryp->class_;

  if (!QO_ENV_PT_TREE (env) || !QO_ENV_PT_TREE (env)->info.query.q.select.group_by)
    {
      goto end;
    }

  key_notnull = qo_validate_index_term_notnull (env, index_entryp);
  if (key_notnull)
    {
      goto final;
    }

  /* get the name of the first column in the group by list */
  groupby_expr = QO_ENV_PT_TREE (env)->info.query.q.select.group_by->info.sort_spec.expr;

  assert (key_notnull == false);

  key_notnull = qo_validate_index_attr_notnull (env, index_entryp, groupby_expr);
  if (key_notnull)
    {
      goto final;
    }

  /* Now we have the information we need: if the key column can be null and if there is a PT_IS_NULL or PT_IS_NOT_NULL
   * expression with this key column involved and also if we have other terms with the key. We must decide if there can
   * be NULLs in the results and if so, drop this index. 1. If the key cannot have null values, we have a winner. 2.
   * Otherwise, if we found a term isnull/isnotnull(key) we drop it (because we cannot evaluate if this yields true or
   * false so we skip all, for safety) 3. If we have a term with other operator except isnull/isnotnull and does not
   * have an OR following we have a winner again! (because we cannot have a null value).
   */
final:
  if (key_notnull)
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
      return false;
    }

  if (plan->info)
    {
      env = plan->info->env;
    }

  if (env == NULL)
    {
      return false;
    }

  tree = QO_ENV_PT_TREE (env);

  if (tree == NULL)
    {
      return false;
    }

  if (tree->info.query.q.select.hint & PT_HINT_NO_IDX_DESC)
    {
      return false;
    }

  group_by = tree->info.query.q.select.group_by;

  for (trav = list; trav; trav = trav->next)
    {
      /* change PT_ASC to PT_DESC and vice-versa */
      trav->info.sort_spec.asc_or_desc = (PT_MISC_TYPE) (PT_ASC + PT_DESC - trav->info.sort_spec.asc_or_desc);
    }

  /* test again the group by skip */
  groupby_skip = pt_sort_spec_cover_groupby (env->parser, list, group_by, tree);

  /* change back directions */
  for (trav = list; trav; trav = trav->next)
    {
      /* change PT_ASC to PT_DESC and vice-versa */
      trav->info.sort_spec.asc_or_desc = (PT_MISC_TYPE) (PT_ASC + PT_DESC - trav->info.sort_spec.asc_or_desc);
    }

  return groupby_skip;
}

/*
 * qo_plan_compute_iscan_sort_list () -
 *   return: sort_list
 *   root(in):
 *   group_by(in):
 *   is_index_w_prefix(out):
 *
 */
static PT_NODE *
qo_plan_compute_iscan_sort_list (QO_PLAN * root, PT_NODE * group_by, bool * is_index_w_prefix)
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
  TP_DOMAIN *key_type, *col_type;
  BITSET *terms;
  BITSET_ITERATOR bi;
  bool is_const_eq_term;

  sort_list = NULL;		/* init */
  col = NULL;
  *is_index_w_prefix = false;

  /* find sortable plan */
  plan = root;
  while (plan && plan->plan_type != QO_PLANTYPE_SCAN)
    {
      switch (plan->plan_type)
	{
	case QO_PLANTYPE_FOLLOW:
	  plan = plan->plan_un.follow.head;
	  break;

	case QO_PLANTYPE_JOIN:
	  if (plan->plan_un.join.join_method == QO_JOINMETHOD_NL_JOIN
	      || plan->plan_un.join.join_method == QO_JOINMETHOD_IDX_JOIN)
	    {
	      plan = plan->plan_un.join.outer;
	    }
	  else
	    {
	      /* QO_JOINMETHOD_MERGE_JOIN */
	      plan = NULL;
	    }
	  break;

	case QO_PLANTYPE_SORT:
	  if (plan->plan_un.sort.sort_type == SORT_LIMIT)
	    {
	      plan = plan->plan_un.sort.subplan;
	    }
	  else
	    {
	      plan = NULL;
	    }
	  break;

	default:
	  plan = NULL;
	  break;
	}
    }

  /* check for plan type */
  if (plan == NULL || plan->plan_type != QO_PLANTYPE_SCAN)
    {
      goto exit_on_end;		/* nop */
    }
  else if (QO_NODE_INFO (plan->plan_un.scan.node) == NULL)
    {
      /* if there's no class information or the class is not normal class */
      goto exit_on_end;		/* nop */
    }
  else if (QO_NODE_IS_CLASS_HIERARCHY (plan->plan_un.scan.node))
    {
      /* exclude class hierarchy scan */
      goto exit_on_end;		/* nop */
    }

  /* check for index scan plan */
  if (!qo_is_interesting_order_scan (plan) || (env = (plan->info)->env) == NULL
      || (parser = QO_ENV_PARSER (env)) == NULL || (tree = QO_ENV_PT_TREE (env)) == NULL)
    {
      goto exit_on_end;		/* nop */
    }

  /* pointer to QO_NODE_INDEX_ENTRY structure in QO_PLAN */
  ni_entryp = plan->plan_un.scan.index;
  /* pointer to linked list of index node, 'head' field(QO_INDEX_ENTRY strucutre) of QO_NODE_INDEX_ENTRY */
  index_entryp = (ni_entryp)->head;

  nterms = bitset_cardinality (&(plan->plan_un.scan.terms));
  if (nterms > 0)
    {
      equi_nterms = plan->plan_un.scan.index_equi ? nterms : nterms - 1;
    }
  else
    {
      equi_nterms = 0;
    }
  assert (equi_nterms >= 0);

  if (index_entryp->rangelist_seg_idx != -1)
    {
      equi_nterms = MIN (equi_nterms, index_entryp->rangelist_seg_idx);
    }

  /* we must have the first index column appear as the first sort column, so we pretend the number of index_equi
   * columns is zero, to force it to match the sort list and the index columns one-for-one.
   */
  if (qo_is_index_iss_scan (plan) || index_entryp->constraints->func_index_info != NULL)
    {
      equi_nterms = 0;
    }
  assert (equi_nterms >= 0);
  assert (equi_nterms <= index_entryp->nsegs);

  if (equi_nterms >= index_entryp->nsegs)
    {
      /* is all constant col's order node */
      goto exit_on_end;		/* nop */
    }

  /* check if this is an index with prefix */
  *is_index_w_prefix = qo_is_prefix_index (index_entryp);

  asc_or_desc = (SM_IS_CONSTRAINT_REVERSE_INDEX_FAMILY (index_entryp->constraints->type) ? PT_DESC : PT_ASC);

  key_type = index_entryp->key_type;
  if (key_type == NULL)
    {				/* is invalid case */
      assert (false);
      goto exit_on_end;		/* nop */
    }

  if (asc_or_desc == PT_DESC || index_entryp->constraints->func_index_info != NULL)
    {
      col_type = NULL;		/* nop; do not care asc_or_desc anymore */
    }
  else
    {
      if (TP_DOMAIN_TYPE (key_type) == DB_TYPE_MIDXKEY)
	{
	  assert (QO_ENTRY_MULTI_COL (index_entryp));

	  col_type = key_type->setdomain;
	  assert (col_type != NULL);

	  /* get the first non-equal range key domain */
	  for (j = 0; j < equi_nterms && col_type; j++)
	    {
	      col_type = col_type->next;
	    }
	}
      else
	{
	  col_type = key_type;

	  assert (col_type != NULL);
	  assert (col_type->next == NULL);
	  assert (equi_nterms <= 1);

	  /* get the first non-equal range key domain */
	  if (equi_nterms > 0)
	    {
	      col_type = NULL;	/* give up */
	    }
	}

      assert (col_type != NULL || equi_nterms > 0);
    }

  for (i = equi_nterms; i < index_entryp->nsegs; i++)
    {
      if (index_entryp->ils_prefix_len > 0 && i >= index_entryp->ils_prefix_len)
	{
	  /* sort list should contain only prefix when using loose index scan */
	  break;
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

      if (index_entryp->constraints->func_index_info != NULL)
	{
	  if (QO_SEG_FUNC_INDEX (seg) == true)
	    {
	      asc_or_desc = index_entryp->constraints->func_index_info->fi_domain->is_desc ? PT_DESC : PT_ASC;
	    }
	}
      else
	{
	  if (col_type)
	    {
	      asc_or_desc = (col_type->is_desc) ? PT_DESC : PT_ASC;
	      col_type = col_type->next;
	    }

	  /* skip segment of const eq term */
	  terms = &(QO_SEG_INDEX_TERMS (seg));
	  is_const_eq_term = false;
	  for (j = bitset_iterate (terms, &bi); j != -1; j = bitset_next_member (&bi))
	    {
	      expr = QO_TERM_PT_EXPR (QO_ENV_TERM (env, j));
	      if (PT_IS_EXPR_NODE_WITH_OPERATOR (expr, PT_EQ)
		  && (PT_IS_CONST (expr->info.expr.arg1) || PT_IS_CONST (expr->info.expr.arg2)))
		{
		  is_const_eq_term = true;
		}
	    }
	  if (is_const_eq_term)
	    {
	      continue;
	    }
	}

      /* is for order_by skip */

      /* check for constant col's order node */
      pt_to_pos_descr (parser, &pos_descr, node, tree, NULL);
      if (pos_descr.pos_no > 0)
	{
	  col = tree->info.query.q.select.list;
	  for (j = 1; j < pos_descr.pos_no && col; j++)
	    {
	      col = col->next;
	    }

	  if (col)
	    {
	      col = pt_get_end_path_node (col);
	      if (col && col->node_type == PT_NAME && PT_NAME_INFO_IS_FLAGED (col, PT_NAME_INFO_CONSTANT))
		{
		  continue;	/* skip out constant order */
		}
	    }
	}

      /* is for group_by skip */
      if (group_by != NULL)
	{
	  assert (!group_by->flag.with_rollup);

	  /* check for constant col's group node */
	  pt_to_pos_descr_groupby (parser, &pos_descr, node, tree);
	  if (pos_descr.pos_no > 0)
	    {
	      assert (group_by == tree->info.query.q.select.group_by);
	      for (col = group_by, j = 1; j < pos_descr.pos_no && col; j++)
		{
		  col = col->next;
		}

	      while (col && col->node_type == PT_SORT_SPEC)
		{
		  col = col->info.sort_spec.expr;
		}

	      if (col)
		{
		  col = pt_get_end_path_node (col);
		  if (col && col->node_type == PT_NAME && PT_NAME_INFO_IS_FLAGED (col, PT_NAME_INFO_CONSTANT))
		    {
		      continue;	/* skip out constant order */
		    }
		}
	    }
	}

      if (pos_descr.pos_no <= 0 || col == NULL)
	{			/* not found i-th key element */
	  break;		/* give up */
	}

      /* set sort info */
      sort = parser_new_node (parser, PT_SORT_SPEC);
      if (sort == NULL)
	{
	  PT_ERRORm (parser, node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OUT_OF_MEMORY);
	  break;		/* give up */
	}

      sort->info.sort_spec.expr = pt_point (parser, col);
      sort->info.sort_spec.pos_descr = pos_descr;
      sort->info.sort_spec.asc_or_desc = asc_or_desc;

      sort_list = parser_append_node (sort, sort_list);
    }

exit_on_end:

  return sort_list;
}

/*
 * qo_is_interesting_order_scan ()
 *   return: true/false
 *   plan(in):
 */
bool
qo_is_interesting_order_scan (QO_PLAN * plan)
{
  if (qo_is_iscan (plan) || qo_is_iscan_from_groupby (plan) || qo_is_iscan_from_orderby (plan))
    {
      return true;
    }

  return false;
}

/*
 * qo_plan_is_orderby_skip_candidate () - verify if a plan is a candidate for
 *					  orderby skip
 * return : true/false
 * plan (in) : plan to verify
 */
static bool
qo_plan_is_orderby_skip_candidate (QO_PLAN * plan)
{
  PARSER_CONTEXT *parser;
  PT_NODE *order_by, *statement;
  QO_ENV *env;
  bool is_prefix = false, is_orderby_skip = false;

  if (plan == NULL || plan->info == NULL)
    {
      assert (false);
      return false;
    }

  env = plan->info->env;
  parser = QO_ENV_PARSER (env);
  statement = QO_ENV_PT_TREE (env);
  order_by = statement->info.query.order_by;

  plan->iscan_sort_list = qo_plan_compute_iscan_sort_list (plan, NULL, &is_prefix);

  if (plan->iscan_sort_list == NULL || is_prefix)
    {
      is_orderby_skip = false;
      goto cleanup;
    }

  is_orderby_skip = pt_sort_spec_cover (plan->iscan_sort_list, order_by);
  if (!is_orderby_skip)
    {
      /* verify descending */
      is_orderby_skip = qo_check_orderby_skip_descending (plan);
    }

cleanup:
  if (plan->iscan_sort_list)
    {
      parser_free_tree (parser, plan->iscan_sort_list);
      plan->iscan_sort_list = NULL;
    }
  return is_orderby_skip;
}

/*
 * qo_is_sort_limit () - verify if plan is a SORT-LIMIT plan
 * return : true/false
 * plan (in) :
 */
static bool
qo_is_sort_limit (QO_PLAN * plan)
{
  return (plan != NULL && plan->plan_type == QO_PLANTYPE_SORT && plan->plan_un.sort.sort_type == SORT_LIMIT);
}

/*
 * qo_has_sort_limit_subplan () - verify if a plan has a SORT-LIMIT subplan
 * return : true if plan has SORT-LIMIT subplan, false otherwise
 * plan (in) : plan to verify
 */
bool
qo_has_sort_limit_subplan (QO_PLAN * plan)
{
  if (plan == NULL)
    {
      return false;
    }

  switch (plan->plan_type)
    {
    case QO_PLANTYPE_SCAN:
      return false;

    case QO_PLANTYPE_SORT:
      if (plan->plan_un.sort.sort_type == SORT_LIMIT)
	{
	  return true;
	}
      return qo_has_sort_limit_subplan (plan->plan_un.sort.subplan);

    case QO_PLANTYPE_JOIN:
      return (qo_has_sort_limit_subplan (plan->plan_un.join.outer)
	      || qo_has_sort_limit_subplan (plan->plan_un.join.inner));

    case QO_PLANTYPE_FOLLOW:
    case QO_PLANTYPE_WORST:
      return false;
    }

  return false;
}

/*
 * plan dump for query profile
 */

/*
 * qo_plan_scan_print_json ()
 *   return:
 *   plan(in):
 */
static json_t *
qo_plan_scan_print_json (QO_PLAN * plan)
{
  BITSET_ITERATOR bi;
  QO_ENV *env;
  bool natural_desc_index = false;
  json_t *scan, *range, *filter;
  const char *scan_string = "";
  const char *class_name;
  int i;

  scan = json_object ();

  class_name = QO_NODE_NAME (plan->plan_un.scan.node);
  if (class_name == NULL)
    {
      class_name = "unknown";
    }

  json_object_set_new (scan, "table", json_string (class_name));

  switch (plan->plan_un.scan.scan_method)
    {
    case QO_SCANMETHOD_SEQ_SCAN:
      scan_string = "TABLE SCAN";
      break;

    case QO_SCANMETHOD_INDEX_SCAN:
    case QO_SCANMETHOD_INDEX_ORDERBY_SCAN:
    case QO_SCANMETHOD_INDEX_GROUPBY_SCAN:
    case QO_SCANMETHOD_INDEX_SCAN_INSPECT:
      scan_string = "INDEX SCAN";
      json_object_set_new (scan, "index", json_string (plan->plan_un.scan.index->head->constraints->name));

      env = (plan->info)->env;
      range = json_array ();

      for (i = bitset_iterate (&(plan->plan_un.scan.terms), &bi); i != -1; i = bitset_next_member (&bi))
	{
	  json_array_append_new (range, json_string (qo_term_string (QO_ENV_TERM (env, i))));
	}

      json_object_set_new (scan, "key range", range);

      if (bitset_cardinality (&(plan->plan_un.scan.kf_terms)) > 0)
	{
	  filter = json_array ();
	  for (i = bitset_iterate (&(plan->plan_un.scan.kf_terms), &bi); i != -1; i = bitset_next_member (&bi))
	    {
	      json_array_append_new (filter, json_string (qo_term_string (QO_ENV_TERM (env, i))));
	    }

	  json_object_set_new (scan, "key filter", filter);
	}

      if (qo_is_index_covering_scan (plan))
	{
	  json_object_set_new (scan, "covered", json_true ());
	}

      if (plan->plan_un.scan.index && plan->plan_un.scan.index->head->use_descending)
	{
	  json_object_set_new (scan, "desc_index", json_true ());
	  natural_desc_index = true;
	}

      if (!natural_desc_index && (QO_ENV_PT_TREE (plan->info->env)->info.query.q.select.hint & PT_HINT_USE_IDX_DESC))
	{
	  json_object_set_new (scan, "desc_index forced", json_true ());
	}

      if (qo_is_index_loose_scan (plan))
	{
	  json_object_set_new (scan, "loose", json_true ());
	}

      break;
    }

  return json_pack ("{s:o}", scan_string, scan);
}

/*
 * qo_plan_sort_print_json ()
 *   return:
 *   plan(in):
 */
static json_t *
qo_plan_sort_print_json (QO_PLAN * plan)
{
  json_t *sort, *subplan = NULL;
  const char *type;

  switch (plan->plan_un.sort.sort_type)
    {
    case SORT_TEMP:
      type = "SORT (temp)";
      break;

    case SORT_GROUPBY:
      type = "SORT (group by)";
      break;

    case SORT_ORDERBY:
      type = "SORT (order by)";
      break;

    case SORT_DISTINCT:
      type = "SORT (distinct)";
      break;

    case SORT_LIMIT:
      type = "SORT (limit)";
      break;

    default:
      assert (false);
      type = "";
      break;
    }

  sort = json_object ();

  if (plan->plan_un.sort.subplan)
    {
      subplan = qo_plan_print_json (plan->plan_un.sort.subplan);
      json_object_set_new (sort, type, subplan);
    }
  else
    {
      json_object_set_new (sort, type, json_string (""));
    }

  return sort;
}

/*
 * qo_plan_join_print_json ()
 *   return:
 *   plan(in):
 */
static json_t *
qo_plan_join_print_json (QO_PLAN * plan)
{
  json_t *join, *outer, *inner;
  const char *type, *method = "";
  char buf[32];

  switch (plan->plan_un.join.join_method)
    {
    case QO_JOINMETHOD_NL_JOIN:
    case QO_JOINMETHOD_IDX_JOIN:
      method = "NESTED LOOPS";
      break;

    case QO_JOINMETHOD_MERGE_JOIN:
      method = "MERGE JOIN";
      break;
    }

  switch (plan->plan_un.join.join_type)
    {
    case JOIN_INNER:
      if (!bitset_is_empty (&(plan->plan_un.join.join_terms)))
	{
	  type = "inner join";
	}
      else
	{
	  if (plan->plan_un.join.join_method == QO_JOINMETHOD_IDX_JOIN)
	    {
	      type = "inner join";
	    }
	  else
	    {
	      type = "cross join";
	    }
	}
      break;
    case JOIN_LEFT:
      type = "left outer join";
      break;
    case JOIN_RIGHT:
      type = "right outer join";
      break;
    case JOIN_OUTER:		/* not used */
      type = "full outer join";
      break;
    case JOIN_CSELECT:
      type = "cselect";
      break;
    case NO_JOIN:
    default:
      type = "unknown";
      break;
    }

  outer = qo_plan_print_json (plan->plan_un.join.outer);
  inner = qo_plan_print_json (plan->plan_un.join.inner);

  sprintf (buf, "%s (%s)", method, type);

  join = json_pack ("{s:[o,o]}", buf, outer, inner);

  return join;
}

/*
 * qo_plan_follow_print_json ()
 *   return:
 *   plan(in):
 */
static json_t *
qo_plan_follow_print_json (QO_PLAN * plan)
{
  json_t *head, *follow;

  head = qo_plan_print_json (plan->plan_un.follow.head);

  follow = json_object ();
  json_object_set_new (follow, "edge", json_string (qo_term_string (plan->plan_un.follow.path)));
  json_object_set_new (follow, "head", head);

  return json_pack ("{s:o}", "FOLLOW", follow);
}

/*
 * qo_plan_print_json ()
 *   return:
 *   plan(in):
 */
static json_t *
qo_plan_print_json (QO_PLAN * plan)
{
  json_t *json = NULL;

  switch (plan->plan_type)
    {
    case QO_PLANTYPE_SCAN:
      json = qo_plan_scan_print_json (plan);
      break;

    case QO_PLANTYPE_SORT:
      json = qo_plan_sort_print_json (plan);
      break;

    case QO_PLANTYPE_JOIN:
      json = qo_plan_join_print_json (plan);
      break;

    case QO_PLANTYPE_FOLLOW:
      json = qo_plan_follow_print_json (plan);
      break;

    default:
      break;
    }

  return json;
}

/*
 * qo_top_plan_print_json ()
 *   return:
 *   parser(in):
 *   xasl(in):
 *   select(in):
 *   plan(in):
 */
void
qo_top_plan_print_json (PARSER_CONTEXT * parser, xasl_node * xasl, PT_NODE * select, QO_PLAN * plan)
{
  json_t *json;
  unsigned int save_custom;

  assert (parser != NULL && xasl != NULL && plan != NULL && select != NULL);

  if (parser->num_plan_trace >= MAX_NUM_PLAN_TRACE)
    {
      return;
    }

  json = qo_plan_print_json (plan);

  if (select->info.query.order_by)
    {
      if (xasl && xasl->spec_list && xasl->spec_list->indexptr && xasl->spec_list->indexptr->orderby_skip)
	{
	  json_object_set_new (json, "skip order by", json_true ());
	}
    }

  if (select->info.query.q.select.group_by)
    {
      if (xasl && xasl->spec_list && xasl->spec_list->indexptr && xasl->spec_list->indexptr->groupby_skip)
	{
	  json_object_set_new (json, "group by nosort", json_true ());
	}
    }

  save_custom = parser->custom_print;
  parser->custom_print |= PT_CONVERT_RANGE;

  json_object_set_new (json, "rewritten query", json_string (parser_print_tree (parser, select)));

  parser->custom_print = save_custom;

  parser->plan_trace[parser->num_plan_trace].format = QUERY_TRACE_JSON;
  parser->plan_trace[parser->num_plan_trace].trace.json_plan = json;
  parser->num_plan_trace++;

  return;
}

/*
 * qo_plan_scan_print_text ()
 *   return:
 *   fp(in):
 *   plan(in):
 *   indent(in):
 */
static void
qo_plan_scan_print_text (FILE * fp, QO_PLAN * plan, int indent)
{
  BITSET_ITERATOR bi;
  QO_ENV *env;
  bool natural_desc_index = false;
  const char *class_name;
  int i;

  indent += 2;
  fprintf (fp, "%*c", indent, ' ');

  class_name = QO_NODE_NAME (plan->plan_un.scan.node);
  if (class_name == NULL)
    {
      class_name = "unknown";
    }

  switch (plan->plan_un.scan.scan_method)
    {
    case QO_SCANMETHOD_SEQ_SCAN:
      fprintf (fp, "TABLE SCAN (%s)", class_name);
      break;

    case QO_SCANMETHOD_INDEX_SCAN:
    case QO_SCANMETHOD_INDEX_ORDERBY_SCAN:
    case QO_SCANMETHOD_INDEX_GROUPBY_SCAN:
    case QO_SCANMETHOD_INDEX_SCAN_INSPECT:
      fprintf (fp, "INDEX SCAN (%s.%s)", class_name, plan->plan_un.scan.index->head->constraints->name);

      env = (plan->info)->env;
      fprintf (fp, " (");

      for (i = bitset_iterate (&(plan->plan_un.scan.terms), &bi); i != -1; i = bitset_next_member (&bi))
	{
	  fprintf (fp, "key range: %s", qo_term_string (QO_ENV_TERM (env, i)));
	}

      if (bitset_cardinality (&(plan->plan_un.scan.kf_terms)) > 0)
	{
	  for (i = bitset_iterate (&(plan->plan_un.scan.kf_terms), &bi); i != -1; i = bitset_next_member (&bi))
	    {
	      fprintf (fp, ", key filter: %s", qo_term_string (QO_ENV_TERM (env, i)));
	    }
	}

      if (qo_is_index_covering_scan (plan))
	{
	  fprintf (fp, ", covered: true");
	}

      if (plan->plan_un.scan.index && plan->plan_un.scan.index->head->use_descending)
	{
	  fprintf (fp, ", desc_index: true");
	  natural_desc_index = true;
	}

      if (!natural_desc_index && (QO_ENV_PT_TREE (plan->info->env)->info.query.q.select.hint & PT_HINT_USE_IDX_DESC))
	{
	  fprintf (fp, ", desc_index forced: true");
	}

      if (qo_is_index_loose_scan (plan))
	{
	  fprintf (fp, ", loose: true");
	}

      fprintf (fp, ")");
      break;
    }

  fprintf (fp, "\n");
}

/*
 * qo_plan_sort_print_text ()
 *   return:
 *   fp(in):
 *   plan(in):
 *   indent(in):
 */
static void
qo_plan_sort_print_text (FILE * fp, QO_PLAN * plan, int indent)
{
  const char *type;

  indent += 2;

  switch (plan->plan_un.sort.sort_type)
    {
    case SORT_TEMP:
      type = "SORT (temp)";
      break;

    case SORT_GROUPBY:
      type = "SORT (group by)";
      break;

    case SORT_ORDERBY:
      type = "SORT (order by)";
      break;

    case SORT_DISTINCT:
      type = "SORT (distinct)";
      break;

    case SORT_LIMIT:
      type = "SORT (limit)";
      break;

    default:
      assert (false);
      type = "";
      break;
    }

  fprintf (fp, "%*c%s\n", indent, ' ', type);

  if (plan->plan_un.sort.subplan)
    {
      qo_plan_print_text (fp, plan->plan_un.sort.subplan, indent);
    }
}

/*
 * qo_plan_join_print_text ()
 *   return:
 *   fp(in):
 *   plan(in):
 *   indent(in):
 */
static void
qo_plan_join_print_text (FILE * fp, QO_PLAN * plan, int indent)
{
  const char *type, *method = "";

  indent += 2;

  switch (plan->plan_un.join.join_method)
    {
    case QO_JOINMETHOD_NL_JOIN:
    case QO_JOINMETHOD_IDX_JOIN:
      method = "NESTED LOOPS";
      break;

    case QO_JOINMETHOD_MERGE_JOIN:
      method = "MERGE JOIN";
      break;
    }

  switch (plan->plan_un.join.join_type)
    {
    case JOIN_INNER:
      if (!bitset_is_empty (&(plan->plan_un.join.join_terms)))
	{
	  type = "inner join";
	}
      else
	{
	  if (plan->plan_un.join.join_method == QO_JOINMETHOD_IDX_JOIN)
	    {
	      type = "inner join";
	    }
	  else
	    {
	      type = "cross join";
	    }
	}
      break;
    case JOIN_LEFT:
      type = "left outer join";
      break;
    case JOIN_RIGHT:
      type = "right outer join";
      break;
    case JOIN_OUTER:		/* not used */
      type = "full outer join";
      break;
    case JOIN_CSELECT:
      type = "cselect";
      break;
    case NO_JOIN:
    default:
      type = "unknown";
      break;
    }

  fprintf (fp, "%*c%s (%s)\n", indent, ' ', method, type);
  qo_plan_print_text (fp, plan->plan_un.join.outer, indent);
  qo_plan_print_text (fp, plan->plan_un.join.inner, indent);
}

/*
 * qo_plan_follow_print_text ()
 *   return:
 *   fp(in):
 *   plan(in):
 *   indent(in):
 */
static void
qo_plan_follow_print_text (FILE * fp, QO_PLAN * plan, int indent)
{
  indent += 2;

  fprintf (fp, "%*cFOLLOW (edge: %s)\n", indent, ' ', qo_term_string (plan->plan_un.follow.path));

  qo_plan_print_text (fp, plan->plan_un.follow.head, indent);
}

/*
 * qo_plan_print_text ()
 *   return:
 *   fp(in):
 *   plan(in):
 *   indent(in):
 */
static void
qo_plan_print_text (FILE * fp, QO_PLAN * plan, int indent)
{
  switch (plan->plan_type)
    {
    case QO_PLANTYPE_SCAN:
      qo_plan_scan_print_text (fp, plan, indent);
      break;

    case QO_PLANTYPE_SORT:
      qo_plan_sort_print_text (fp, plan, indent);
      break;

    case QO_PLANTYPE_JOIN:
      qo_plan_join_print_text (fp, plan, indent);
      break;

    case QO_PLANTYPE_FOLLOW:
      qo_plan_follow_print_text (fp, plan, indent);
      break;

    default:
      break;
    }
}

/*
 * qo_top_plan_print_text ()
 *   return:
 *   parser(in):
 *   xasl(in):
 *   select(in):
 *   plan(in):
 */
void
qo_top_plan_print_text (PARSER_CONTEXT * parser, xasl_node * xasl, PT_NODE * select, QO_PLAN * plan)
{
  size_t sizeloc;
  char *ptr, *sql;
  FILE *fp;
  int indent;
  unsigned int save_custom;

  assert (parser != NULL && xasl != NULL && plan != NULL && select != NULL);

  if (parser->num_plan_trace >= MAX_NUM_PLAN_TRACE)
    {
      return;
    }

  fp = port_open_memstream (&ptr, &sizeloc);
  if (fp == NULL)
    {
      return;
    }

  indent = 0;
  qo_plan_print_text (fp, plan, indent);

  indent += 2;

  if (select->info.query.order_by)
    {
      if (xasl && xasl->spec_list && xasl->spec_list->indexptr && xasl->spec_list->indexptr->orderby_skip)
	{
	  fprintf (fp, "%*cskip order by: true\n", indent, ' ');
	}
    }

  if (select->info.query.q.select.group_by)
    {
      if (xasl && xasl->spec_list && xasl->spec_list->indexptr && xasl->spec_list->indexptr->groupby_skip)
	{
	  fprintf (fp, "%*cgroup by nosort: true\n", indent, ' ');
	}
    }

  save_custom = parser->custom_print;
  parser->custom_print |= PT_CONVERT_RANGE;
  sql = parser_print_tree (parser, select);
  parser->custom_print = save_custom;

  if (sql != NULL)
    {
      fprintf (fp, "\n%*crewritten query: %s\n", indent, ' ', sql);
    }

  port_close_memstream (fp, &ptr, &sizeloc);

  if (ptr != NULL)
    {
      parser->plan_trace[parser->num_plan_trace].format = QUERY_TRACE_TEXT;
      parser->plan_trace[parser->num_plan_trace].trace.text_plan = ptr;
      parser->num_plan_trace++;
    }

  return;
}
