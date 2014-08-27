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
 * query_graph.c - builds a query graph from a parse tree and
 * 			transforms the tree by unfolding path expressions.
 */

#ident "$Id$"

#include "config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <math.h>
#if !defined(WINDOWS)
#include <values.h>
#endif /* !WINDOWS */

#include "parser.h"
#include "error_code.h"
#include "error_manager.h"
#include "object_representation.h"
#include "optimizer.h"
#include "query_graph.h"
#include "query_planner.h"
#include "schema_manager.h"
#include "statistics.h"
#include "system_parameter.h"
#include "environment_variable.h"
#include "xasl_generation.h"
#include "query_list.h"
#include "db.h"
#include "system_parameter.h"
#include "memory_alloc.h"
#include "environment_variable.h"
#include "util_func.h"

#include "locator_cl.h"
#include "object_domain.h"
#include "network_interface_cl.h"

/* figure out how many bytes a QO_USING_INDEX struct with n entries requires */
#define SIZEOF_USING_INDEX(n) \
    (sizeof(QO_USING_INDEX) + (((n)-1) * sizeof(QO_USING_INDEX_ENTRY)))

/* any number that won't overlap PT_MISC_TYPE */
#define PREDICATE_TERM  -2

#define RANK_DEFAULT       0	/* default              */
#define RANK_NAME          RANK_DEFAULT	/* name  -- use default */
#define RANK_VALUE         RANK_DEFAULT	/* value -- use default */
#define RANK_EXPR_LIGHT    1	/* Group 1              */
#define RANK_EXPR_MEDIUM   2	/* Group 2              */
#define RANK_EXPR_HEAVY    3	/* Group 3              */
#define RANK_EXPR_FUNCTION 4	/* agg function, set    */
#define RANK_QUERY         8	/* subquery             */

/*  log2(sizeof(POINTER)) */
#if __WORDSIZE == 64
/*  log2(8) */
#define LOG2_SIZEOF_POINTER 3
#else
/*  log2(4) */
#define LOG2_SIZEOF_POINTER 2
#endif

/*
 * Figure out how many bytes a QO_INDEX struct with n entries requires.
 */
#define SIZEOF_INDEX(n) \
    (sizeof(QO_INDEX) + (((n)-1)* sizeof(QO_INDEX_ENTRY)))

/*
 * Figure out how many bytes a QO_CLASS_INFO struct with n entries requires.
 */
#define SIZEOF_CLASS_INFO(n) \
    (sizeof(QO_CLASS_INFO) + (((n)-1) * sizeof(QO_CLASS_INFO_ENTRY)))

/*
 * Figure out how many bytes a pkeys[] struct with n entries requires.
 */
#define SIZEOF_ATTR_CUM_STATS_PKEYS(n) \
    ((n) * sizeof(int))

#define NOMINAL_HEAP_SIZE(class)	200	/* pages */
#define NOMINAL_OBJECT_SIZE(class)	 64	/* bytes */

/* Figure out how many bytes a QO_NODE_INDEX struct with n entries requires. */
#define SIZEOF_NODE_INDEX(n) \
    (sizeof(QO_NODE_INDEX) + (((n)-1)* sizeof(QO_NODE_INDEX_ENTRY)))

#define EXCHANGE_BUILDER(type,e0,e1) \
    do { type _tmp = e0; e0 = e1; e1 = _tmp; } while (0)

#define TERMCLASS_EXCHANGE(e0,e1)  EXCHANGE_BUILDER(QO_TERMCLASS,e0,e1)
#define DOUBLE_EXCHANGE(e0,e1)     EXCHANGE_BUILDER(double,e0,e1)
#define PT_NODE_EXCHANGE(e0,e1)    EXCHANGE_BUILDER(PT_NODE *,e0,e1)
#define INT_EXCHANGE(e0,e1)        EXCHANGE_BUILDER(int,e0,e1)
#define SEGMENTPTR_EXCHANGE(e0,e1) EXCHANGE_BUILDER(QO_SEGMENT *,e0,e1)
#define NODEPTR_EXCHANGE(e0,e1)    EXCHANGE_BUILDER(QO_NODE *,e0,e1)
#define EQCLASSPTR_EXCHANGE(e0,e1) EXCHANGE_BUILDER(QO_EQCLASS *,e0,e1)
#define BOOL_EXCHANGE(e0,e1)       EXCHANGE_BUILDER(bool,e0,e1)
#define JOIN_TYPE_EXCHANGE(e0,e1)  EXCHANGE_BUILDER(JOIN_TYPE,e0,e1)
#define FLAG_EXCHANGE(e0,e1)       EXCHANGE_BUILDER(int,e0,e1)

#define BISET_EXCHANGE(s0,s1) \
    do { \
	BITSET tmp; \
	BITSET_MOVE(tmp, s0); \
	BITSET_MOVE(s0, s1); \
	BITSET_MOVE(s1, tmp); \
    } while (0)

#define PUT_FLAG(cond, flag) \
    do { \
	if (cond) { \
	    if (extra_info++) { \
	        fputs(flag, f); \
	    } else { \
		fputs(" (", f); \
		fputs(flag, f); \
	    } \
	} \
    } while (0)
typedef enum
{
  QO_BUILD_ENTITY = 0x01,	/* 0000 0001 */
  QO_BUILD_PATH = 0x02		/* 0000 0010 */
} QO_BUILD_STATUS;

typedef struct walk_info WALK_INFO;
struct walk_info
{
  QO_ENV *env;
  QO_TERM *term;
};

double QO_INFINITY = 0.0;

static QO_PLAN *qo_optimize_helper (QO_ENV * env);

static QO_NODE *qo_add_node (PT_NODE * entity, QO_ENV * env);

static QO_SEGMENT *qo_insert_segment (QO_NODE * head, QO_NODE * tail,
				      PT_NODE * node, QO_ENV * env,
				      const char *expr_str);

static QO_SEGMENT *qo_join_segment (QO_NODE * head, QO_NODE * tail,
				    PT_NODE * name, QO_ENV * env);

static PT_NODE *qo_add_final_segment (PARSER_CONTEXT * parser, PT_NODE * tree,
				      void *arg, int *continue_walk);

static QO_TERM *qo_add_term (PT_NODE * conjunct, int term_type, QO_ENV * env);

static void
qo_add_dep_term (QO_NODE * derived_node, BITSET * depend_nodes,
		 BITSET * depend_segs, QO_ENV * env);

static QO_TERM *qo_add_dummy_join_term (QO_ENV * env, QO_NODE * p_node,
					QO_NODE * on_node);

static void qo_analyze_term (QO_TERM * term, int term_type);

static PT_NODE *set_seg_expr (PARSER_CONTEXT * parser, PT_NODE * tree,
			      void *arg, int *continue_walk);

static void set_seg_node (PT_NODE * attr, QO_ENV * env, BITSET * bitset);

static QO_ENV *qo_env_init (PARSER_CONTEXT * parser, PT_NODE * query);

static bool qo_validate (QO_ENV * env);

static PT_NODE *build_query_graph (PARSER_CONTEXT * parser, PT_NODE * tree,
				   void *arg, int *continue_walk);

static PT_NODE *build_query_graph_post (PARSER_CONTEXT * parser,
					PT_NODE * tree, void *arg,
					int *continue_walk);

static PT_NODE *build_query_graph_function_index (PARSER_CONTEXT * parser,
						  PT_NODE * tree, void *arg,
						  int *continue_walk);

static QO_NODE *build_graph_for_entity (QO_ENV * env, PT_NODE * entity,
					QO_BUILD_STATUS status);

static PT_NODE *graph_size_select (PARSER_CONTEXT * parser, PT_NODE * tree,
				   void *arg, int *continue_walk);

static void graph_size_for_entity (QO_ENV * env, PT_NODE * entity);

static bool is_dependent_table (PT_NODE * entity);

static void get_term_subqueries (QO_ENV * env, QO_TERM * term);

static void get_term_rank (QO_ENV * env, QO_TERM * term);

static PT_NODE *check_subquery_pre (PARSER_CONTEXT * parser, PT_NODE * node,
				    void *arg, int *continue_walk);

static bool is_local_name (QO_ENV * env, PT_NODE * expr);

static void get_local_subqueries (QO_ENV * env, PT_NODE * tree);

static void get_rank (QO_ENV * env);

static PT_NODE *get_referenced_attrs (PT_NODE * entity);

static bool expr_is_mergable (PT_NODE * pt_expr);

static bool qo_is_equi_join_term (QO_TERM * term);

static void add_hint (QO_ENV * env, PT_NODE * tree);

static void add_using_index (QO_ENV * env, PT_NODE * using_index);

static int get_opcode_rank (PT_OP_TYPE opcode);

static int get_expr_fcode_rank (FUNC_TYPE fcode);

static int get_operand_rank (PT_NODE * node);

static int count_classes (PT_NODE * p);
static QO_CLASS_INFO_ENTRY *grok_classes (QO_ENV * env, PT_NODE * dom_set,
					  QO_CLASS_INFO_ENTRY * info);
static int qo_data_compare (DB_DATA * data1, DB_DATA * data2, DB_TYPE type);
static void qo_estimate_statistics (MOP class_mop, CLASS_STATS *);

static void qo_node_free (QO_NODE *);
static void qo_node_dump (QO_NODE *, FILE *);
static void qo_node_add_sarg (QO_NODE *, QO_TERM *);

static void qo_seg_free (QO_SEGMENT *);

static QO_EQCLASS *qo_eqclass_new (QO_ENV *);
static void qo_eqclass_free (QO_EQCLASS *);
static void qo_eqclass_add (QO_EQCLASS *, QO_SEGMENT *);
static void qo_eqclass_dump (QO_EQCLASS *, FILE *);

static void qo_term_free (QO_TERM *);
static void qo_term_dump (QO_TERM *, FILE *);
static void qo_subquery_dump (QO_ENV *, QO_SUBQUERY *, FILE *);
static void qo_subquery_free (QO_SUBQUERY *);

static void qo_partition_init (QO_ENV *, QO_PARTITION *, int);
static void qo_partition_free (QO_PARTITION *);
static void qo_partition_dump (QO_PARTITION *, FILE *);
static void qo_find_index_terms (QO_ENV * env, BITSET * segsp,
				 QO_INDEX_ENTRY * index_entry);
static void qo_find_index_seg_terms (QO_ENV * env,
				     QO_INDEX_ENTRY * index_entry, int idx);
static bool qo_find_index_segs (QO_ENV *, SM_CLASS_CONSTRAINT *, QO_NODE *,
				int *, int, int *, BITSET *);
static bool qo_is_coverage_index (QO_ENV * env, QO_NODE * nodep,
				  QO_INDEX_ENTRY * index_entry);
static void qo_find_node_indexes (QO_ENV *, QO_NODE *);
static int is_equivalent_indexes (QO_INDEX_ENTRY * index1,
				  QO_INDEX_ENTRY * index2);
static int qo_find_matching_index (QO_INDEX_ENTRY * index_entry,
				   QO_INDEX * class_indexes);
static QO_INDEX_ENTRY *is_index_compatible (QO_CLASS_INFO * class_info,
					    int n,
					    QO_INDEX_ENTRY * index_entry);

static void qo_discover_sort_limit_nodes (QO_ENV * env);
static void qo_equivalence (QO_SEGMENT *, QO_SEGMENT *);
static void qo_seg_nodes (QO_ENV *, BITSET *, BITSET *);
static QO_ENV *qo_env_new (PARSER_CONTEXT *, PT_NODE *);
static void qo_discover_partitions (QO_ENV *);
static void qo_discover_indexes (QO_ENV *);
static void qo_assign_eq_classes (QO_ENV *);
static void qo_discover_edges (QO_ENV *);
static void qo_classify_outerjoin_terms (QO_ENV *);
static void qo_term_clear (QO_ENV *, int);
static void qo_seg_clear (QO_ENV *, int);
static void qo_node_clear (QO_ENV *, int);
static void qo_get_index_info (QO_ENV * env, QO_NODE * node);
static void qo_free_index (QO_ENV * env, QO_INDEX *);
static QO_INDEX *qo_alloc_index (QO_ENV * env, int);
static void qo_free_node_index_info (QO_ENV * env,
				     QO_NODE_INDEX * node_indexp);
static void qo_free_attr_info (QO_ENV * env, QO_ATTR_INFO * info);
static QO_ATTR_INFO *qo_get_attr_info (QO_ENV * env, QO_SEGMENT * seg);
static QO_ATTR_INFO *qo_get_attr_info_func_index (QO_ENV * env,
						  QO_SEGMENT * seg,
						  const char *expr_str);
static void qo_free_class_info (QO_ENV * env, QO_CLASS_INFO *);
static QO_CLASS_INFO *qo_get_class_info (QO_ENV * env, QO_NODE * node);
static QO_SEGMENT *qo_eqclass_wrt (QO_EQCLASS *, BITSET *);
static void qo_env_dump (QO_ENV *, FILE *);
static int qo_get_ils_prefix_length (QO_ENV * env, QO_NODE * nodep,
				     QO_INDEX_ENTRY * index_entry);
static bool qo_is_iss_index (QO_ENV * env, QO_NODE * nodep,
			     QO_INDEX_ENTRY * index_entry);
static void qo_discover_sort_limit_join_nodes (QO_ENV * env, QO_NODE * nodep,
					       BITSET * order_nodes,
					       BITSET * dep_nodes);
static bool qo_is_pk_fk_full_join (QO_ENV * env, QO_NODE * fk_node,
				   QO_NODE * pk_node);
/*
 * qo_get_optimization_param () - Return the current value of some (global)
 *				  optimization parameter
 *   return: int
 *   retval(in): pointer to area to receive info
 *   param(in): what parameter to retrieve
 *   ...(in): parameter-specific parameters
 */
void
qo_get_optimization_param (void *retval, QO_PARAM param, ...)
{
  char *buf;
  va_list args;

  va_start (args, param);

  switch (param)
    {
    case QO_PARAM_LEVEL:
      *(int *) retval = prm_get_integer_value (PRM_ID_OPTIMIZATION_LEVEL);
      break;
    case QO_PARAM_COST:
      buf = (char *) retval;
      buf[0] = (char) qo_plan_get_cost_fn (va_arg (args, char *));
      buf[1] = '\0';
      break;
    }

  va_end (args);
}

/*
 * qo_need_skip_execution (void) - check execution level and return skip or not
 *
 *   return: bool
 */
bool
qo_need_skip_execution (void)
{
  int level;
  qo_get_optimization_param (&level, QO_PARAM_LEVEL);

  return level & 0x02;
}

/*
 * qo_set_optimization_param () - Return the old value of some (global)
 *				  optimization param, and set the global
 *				  param to the new value
 *   return: int
 *   retval(in): pointer to area to receive info about old value
 *   param(in): what parameter to retrieve
 *   ...(in): parameter-specific parameters
 */
void
qo_set_optimization_param (void *retval, QO_PARAM param, ...)
{
  va_list args;
  va_start (args, param);

  switch (param)
    {
    case QO_PARAM_LEVEL:
      if (retval)
	*(int *) retval = prm_get_integer_value (PRM_ID_OPTIMIZATION_LEVEL);
      prm_set_integer_value (PRM_ID_OPTIMIZATION_LEVEL, va_arg (args, int));
      break;

    case QO_PARAM_COST:
      {
	const char *plan_name;
	int cost_fn;

	plan_name = va_arg (args, char *);
	cost_fn = va_arg (args, int);
	plan_name = qo_plan_set_cost_fn (plan_name, cost_fn);
	if (retval)
	  *(const char **) retval = plan_name;
	break;
      }
    }

  va_end (args);
}

/*
 * qo_optimize_query () - optimize a single select statement, skip nested
 *			  selects since embedded selects are optimized first
 *   return: void
 *   parser(in): parser environment
 *   tree(in): select tree to optimize
 */
QO_PLAN *
qo_optimize_query (PARSER_CONTEXT * parser, PT_NODE * tree)
{
  QO_ENV *env;
  int level;

  /*
   * Give up right away if the optimizer has been turned off in the
   * user's cubrid.conf file or somewhere else.
   */
  qo_get_optimization_param (&level, QO_PARAM_LEVEL);
  if (!OPTIMIZATION_ENABLED (level))
    {
      return NULL;
    }

  /* if its not a select, we're not interested, also if it is
   * merge we give up.
   */
  if (tree->node_type != PT_SELECT)
    {
      return NULL;
    }

  env = qo_env_init (parser, tree);
  if (env == NULL)
    {
      /* we can't optimize, so bail out */
      return NULL;
    }

  switch (setjmp (env->catch_))
    {
    case 0:
      /*
       * The return here is ok; we'll take care of freeing the env
       * structure later, when qo_plan_discard is called.  In fact, if
       * we free it now, the plan pointer we're about to return will be
       * worthless.
       */
      return qo_optimize_helper (env);
    case 1:
      /*
       * Out of memory during optimization.  malloc() has already done
       * an er_set().
       */
      break;
    case 2:
      /*
       * Failed some optimizer assertion.  QO_ABORT() has already done
       * an er_set().
       */
      break;
    default:
      /*
       * No clue.
       */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED_ASSERTION, 1,
	      "false");
      break;
    }

  /*
   * If we get here, an error of some sort occurred, and we need to
   * tear down everything and get out.
   */
#if defined(CUBRID_DEBUG)
  fprintf (stderr, "*** optimizer aborting ***\n");
#endif /* CUBRID_DEBUG */
  qo_env_free (env);

  return NULL;
}

/*
 * qo_optimize_helper () -
 *   return:
 *   env(in):
 */
static QO_PLAN *
qo_optimize_helper (QO_ENV * env)
{
  PARSER_CONTEXT *parser;
  PT_NODE *tree;
  PT_NODE *spec, *conj, *next;
  QO_ENV *local_env;		/* So we can safely take its address */
  QO_PLAN *plan;
  int level;
  QO_TERM *term;
  QO_NODE *node, *p_node;
  BITSET nodeset;
  int n;

  parser = QO_ENV_PARSER (env);
  tree = QO_ENV_PT_TREE (env);
  local_env = env;

  (void) parser_walk_tree (parser, tree, build_query_graph, &local_env,
			   build_query_graph_post, &local_env);
  (void) parser_walk_tree (parser, tree, build_query_graph_function_index,
			   &local_env, NULL, NULL);
  (void) add_hint (env, tree);
  add_using_index (env, tree->info.query.q.select.using_index);

  /* add dep term */
  {
    BITSET dependencies;
    BITSET antecedents;

    bitset_init (&dependencies, env);
    bitset_init (&antecedents, env);

    for (n = 0; n < env->nnodes; n++)
      {
	node = QO_ENV_NODE (env, n);
	spec = QO_NODE_ENTITY_SPEC (node);

	/*
	 * Set up the dependencies; it's simplest just to assume that a
	 * dependent table depends on everything that precedes it.
	 */
	if (is_dependent_table (spec))
	  {
	    /*
	     * Find all of the segments (i.e., attributes) referenced in
	     * the derived table expression, and then find the set of
	     * nodes that underly those segments.  This node can't be
	     * added to a join plan before all of those nodes have, so we
	     * establish some artificial dependency links that force the
	     * planner to maintain that constraint.
	     */

	    BITSET_CLEAR (dependencies);
	    BITSET_CLEAR (antecedents);

	    qo_expr_segs (env, spec->info.spec.derived_table, &dependencies);

	    if (!bitset_is_empty (&dependencies))
	      {
		qo_seg_nodes (env, &dependencies, &antecedents);
		qo_add_dep_term (node, &antecedents, &dependencies, env);
	      }
	  }
      }				/* for (n = 0 ...) */

    bitset_delset (&dependencies);
    bitset_delset (&antecedents);
  }

  bitset_init (&nodeset, env);

  /* add term in the ON clause */
  for (spec = tree->info.query.q.select.from; spec; spec = spec->next)
    {
      if (spec->node_type == PT_SPEC && spec->info.spec.on_cond)
	{
	  for (conj = spec->info.spec.on_cond; conj; conj = conj->next)
	    {
	      next = conj->next;
	      conj->next = NULL;

	      /* The conjuct could be PT_VALUE(0) if an explicit join
	         condition was derived/transformed to the always-false
	         search condition when type checking, expression evaluation
	         or query rewrite transformation. We should sustained it for
	         correct join plan. It's different from ordinary WHERE search
	         condition. If an always-false search condition was found
	         in WHERE clause, they did not call query optimization and
	         return no result to the user unless the query doesn't have
	         aggregation. */

	      term = qo_add_term (conj, PREDICATE_TERM, env);

	      if (QO_TERM_CLASS (term) == QO_TC_JOIN)
		{
		  QO_ASSERT (env, QO_ON_COND_TERM (term));

		  n = QO_TERM_LOCATION (term);
		  if (QO_NODE_LOCATION (QO_TERM_HEAD (term)) == n - 1
		      && QO_NODE_LOCATION (QO_TERM_TAIL (term)) == n)
		    {
		      bitset_add (&nodeset,
				  QO_NODE_IDX (QO_TERM_TAIL (term)));
		    }
		}

	      conj->next = next;
	    }
	}
    }

  /* add term in the WHERE clause */
  for (conj = tree->info.query.q.select.where; conj; conj = conj->next)
    {
      next = conj->next;
      conj->next = NULL;

      term = qo_add_term (conj, PREDICATE_TERM, env);
      conj->next = next;
    }

  for (n = 1; n < env->nnodes; n++)
    {
      node = QO_ENV_NODE (env, n);

      /* check join-edge for explicit join */
      if (QO_NODE_PT_JOIN_TYPE (node) != PT_JOIN_NONE
	  && QO_NODE_PT_JOIN_TYPE (node) != PT_JOIN_CROSS
	  && !BITSET_MEMBER (nodeset, n))
	{
	  p_node = QO_ENV_NODE (env, n - 1);
	  (void) qo_add_dummy_join_term (env, p_node, node);
	  /* Is it safe to pass node[n-1] as head node?
	     Yes, because the sequence of QO_NODEs corresponds to
	     the sequence of PT_SPEC list */
	}
    }

  /* classify terms for outer join */
  qo_classify_outerjoin_terms (env);

  bitset_delset (&nodeset);

  (void) parser_walk_tree (parser, tree->info.query.q.select.list,
			   qo_add_final_segment, &local_env, pt_continue_walk,
			   NULL);
  (void) parser_walk_tree (parser, tree->info.query.q.select.group_by,
			   qo_add_final_segment, &local_env, pt_continue_walk,
			   NULL);
  (void) parser_walk_tree (parser, tree->info.query.q.select.having,
			   qo_add_final_segment, &local_env, pt_continue_walk,
			   NULL);

  /* it's necessary to find segments for nodes in predicates that are evaluated
   * after joins, in order to be available in intermediary output lists
   */
  if (tree->info.query.q.select.connect_by
      && !tree->info.query.q.select.single_table_opt)
    {
      (void) parser_walk_tree (parser, tree->info.query.q.select.start_with,
			       qo_add_final_segment, &local_env,
			       pt_continue_walk, NULL);
      (void) parser_walk_tree (parser, tree->info.query.q.select.connect_by,
			       qo_add_final_segment, &local_env,
			       pt_continue_walk, NULL);
      (void) parser_walk_tree (parser,
			       tree->info.query.q.select.after_cb_filter,
			       qo_add_final_segment, &local_env,
			       pt_continue_walk, NULL);
    }

  /* finish the rest of the opt structures */
  qo_discover_edges (env);

  /* Don't do these things until *after* qo_discover_edges(); that
   * function may rearrange the QO_TERM structures that were discovered
   * during the earlier phases, and anyone who grabs the idx of one of
   * the terms (or even a pointer to one) will be pointing to the wrong
   * term after they're rearranged.
   */
  qo_assign_eq_classes (env);

  get_local_subqueries (env, tree);
  get_rank (env);
  qo_discover_indexes (env);
  qo_discover_partitions (env);
  qo_discover_sort_limit_nodes (env);
  /* now optimize */

  plan = qo_planner_search (env);

  /* need to set est_card for the select in case it is a subquery */

  /*
   * Print out any needed post-optimization info.  Leave a way to find
   * out about environment info if we aren't able to produce a plan.
   * If this happens in the field at least we'll be able to glean some
   * info.
   */
  qo_get_optimization_param (&level, QO_PARAM_LEVEL);
  if (PLAN_DUMP_ENABLED (level) && DETAILED_DUMP (level) &&
      env->plan_dump_enabled)
    {
      qo_env_dump (env, query_Plan_dump_fp);
    }
  if (plan == NULL)
    {
      qo_env_free (env);
    }

  return plan;
}

/*
 * qo_env_init () - initialize an optimizer environment
 *   return: QO_ENV *
 *   parser(in): parser environment
 *   query(in): A pointer to a PT_NODE structure that describes the query
 *		to be optimized
 */
static QO_ENV *
qo_env_init (PARSER_CONTEXT * parser, PT_NODE * query)
{
  QO_ENV *env;
  int i;
  size_t size;

  if (query == NULL)
    {
      return NULL;
    }

  env = qo_env_new (parser, query);
  if (env == NULL)
    {
      return NULL;
    }

  if (qo_validate (env))
    {
      goto error;
    }

  env->segs = NULL;
  if (env->nsegs > 0)
    {
      size = sizeof (QO_SEGMENT) * env->nsegs;
      env->segs = (QO_SEGMENT *) malloc (size);
      if (env->segs == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, size);
	  goto error;
	}
    }

  env->nodes = NULL;
  if (env->nnodes > 0)
    {
      size = sizeof (QO_NODE) * env->nnodes;
      env->nodes = (QO_NODE *) malloc (size);
      if (env->nodes == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, size);
	  goto error;
	}
    }

  env->terms = NULL;
  if (env->nterms > 0)
    {
      size = sizeof (QO_TERM) * env->nterms;
      env->terms = (QO_TERM *) malloc (size);
      if (env->terms == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, size);
	  goto error;
	}
    }

  env->eqclasses = NULL;
  size = sizeof (QO_EQCLASS) * (MAX (env->nnodes, env->nterms) + env->nsegs);
  if (size > 0)
    {
      env->eqclasses = (QO_EQCLASS *) malloc (size);
      if (env->eqclasses == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, size);
	  goto error;
	}
    }

  env->partitions = NULL;
  if (env->nnodes > 0)
    {
      size = sizeof (QO_PARTITION) * env->nnodes;
      env->partitions = (QO_PARTITION *) malloc (size);
      if (env->partitions == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, size);
	  goto error;
	}
    }

  for (i = 0; i < env->nsegs; ++i)
    {
      qo_seg_clear (env, i);
    }

  for (i = 0; i < env->nnodes; ++i)
    {
      qo_node_clear (env, i);
    }

  for (i = 0; i < env->nterms; ++i)
    {
      qo_term_clear (env, i);
    }

  env->Nnodes = env->nnodes;
  env->Nsegs = env->nsegs;
  env->Nterms = env->nterms;
  env->Neqclasses = MAX (env->nnodes, env->nterms) + env->nsegs;

  env->nnodes = 0;
  env->nsegs = 0;
  env->nterms = 0;
  env->neqclasses = 0;

  QO_INFINITY = infinity ();

  return env;

error:
  qo_env_free (env);
  return NULL;
}

/*
 * qo_validate () -
 *   return: true iff we reject the query, false otherwise
 *   env(in): A pointer to the environment we are working on
 *
 * Note: Determine whether this is a problem that we're willing to
 *	work on.  Right now, this means enforcing the constraints
 *	about maximum set sizes.  We're not very happy with
 *	set-valued attributes, class attributes, or shared attributes
 *      either, but these are temporary problems and are detected
 *      elsewhere.
 */
static bool
qo_validate (QO_ENV * env)
{
#define OPTIMIZATION_LIMIT      64
  PT_NODE *tree, *spec, *conj;

  tree = QO_ENV_PT_TREE (env);

  /* find out how many nodes and segments will be required for the
   * query graph.
   */
  (void) parser_walk_tree (env->parser, tree, graph_size_select, &env,
			   pt_continue_walk, NULL);

  /* count the number of conjuncts in the ON clause */
  for (spec = tree->info.query.q.select.from; spec; spec = spec->next)
    {
      if (spec->node_type == PT_SPEC && spec->info.spec.on_cond)
	{
	  for (conj = spec->info.spec.on_cond; conj; conj = conj->next)
	    {
	      if (conj->node_type != PT_EXPR && conj->node_type != PT_VALUE)
		{		/* for outer join */
		  env->bail_out = 1;
		  return true;
		}
	      env->nterms++;
	    }
	}
    }

  /* count the number of conjuncts in the WHERE clause */
  for (conj = tree->info.query.q.select.where; conj; conj = conj->next)
    {
      if (conj->node_type != PT_EXPR
	  && conj->node_type != PT_VALUE /* is a false conjunct */ )
	{
	  env->bail_out = 1;
	  return true;
	}
      env->nterms++;
    }

  if (env->nnodes > OPTIMIZATION_LIMIT)
    {
      return true;
    }

  return false;

}

/*
 * graph_size_select () - This pre walk function will examine the current
 *			  select and determine the graph size needed for it
 *   return: PT_NODE *
 *   parser(in): parser environment
 *   tree(in): tree to walk
 *   arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
graph_size_select (PARSER_CONTEXT * parser, PT_NODE * tree, void *arg,
		   int *continue_walk)
{
  QO_ENV *env = (QO_ENV *) * (long *) arg;

  *continue_walk = PT_CONTINUE_WALK;

  /*
   * skip nested selects, they've already been counted
   */
  if ((tree->node_type == PT_SELECT) && (tree != env->pt_tree))
    {
      *continue_walk = PT_LIST_WALK;
      return tree;
    }

  /* if its not an entity_spec, we're not interested */
  if (tree->node_type != PT_SPEC)
    {
      return tree;
    }

  (void) graph_size_for_entity (env, tree);

  /* don't visit leaves of the entity, graph_size_for_entity already did */
  *continue_walk = PT_LIST_WALK;

  return tree;

}

/*
 * graph_size_for_entity () - This routine will size the graph for the entity
 *   return: nothing
 *   env(in): optimizer environment
 *   entity(in): entity to build the graph for
 *
 * Note: This routine mimics build_graph_for_entity.  It is IMPORTANT that
 *      they remain in sync or else we might not allocate enough space for
 *      the graph arrays resulting in memory corruption.
 */
static void
graph_size_for_entity (QO_ENV * env, PT_NODE * entity)
{
  PT_NODE *name, *conj, *next_entity;
  MOP cls;
  SM_CLASS_CONSTRAINT *constraints;

  env->nnodes++;

  /* create oid segment for the entity */
  env->nsegs++;

  for (name = get_referenced_attrs (entity); name != NULL; name = name->next)
    {
      env->nsegs++;
    }

  /* check if the constraint is a function index info and add a segment for
   * each function index expression
   */
  if (entity->info.spec.flat_entity_list)
    {
      cls =
	sm_find_class (entity->info.spec.flat_entity_list->info.name.
		       original);
      if (cls)
	{
	  constraints = sm_class_constraints (cls);
	  while (constraints != NULL)
	    {
	      if (constraints->func_index_info)
		{
		  env->nsegs++;
		}
	      constraints = constraints->next;
	    }
	}
    }

  if (is_dependent_table (entity))
    {
      env->nterms += env->nnodes;
    }

  /* recurse and size the graph for path entities */
  for (next_entity = entity->info.spec.path_entities;
       next_entity != NULL; next_entity = next_entity->next)
    {
      (void) graph_size_for_entity (env, next_entity);
    }

  /* create a term for each conjunct in the entity's path_conjuncts */
  for (conj = entity->info.spec.path_conjuncts;
       conj != NULL; conj = conj->next)
    {
      env->nterms++;
    }

  /* reserve space for explicit join dummy term */
  switch (entity->info.spec.join_type)
    {
    case PT_JOIN_INNER:
      /* reserve dummy inner join term */
      env->nterms++;
      /* reserve additional always-false sarg */
      env->nterms++;
      break;
    case PT_JOIN_LEFT_OUTER:
    case PT_JOIN_RIGHT_OUTER:
    case PT_JOIN_FULL_OUTER:
      /* reserve dummy outer join term */
      env->nterms++;
      break;
    default:
      break;
    }

}

/*
 * build_query_graph () - This pre walk function will build the portion of the
 *			  query graph for each entity in the entity_list
 *			  (from list)
 *   return: PT_NODE *
 *   parser(in): parser environment
 *   tree(in): tree to walk
 *   arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
build_query_graph (PARSER_CONTEXT * parser, PT_NODE * tree, void *arg,
		   int *continue_walk)
{
  QO_ENV *env = (QO_ENV *) * (long *) arg;

  *continue_walk = PT_CONTINUE_WALK;

  /*
   * skip nested selects, they've already been done and are
   * constant with respect to the current select statement
   */
  if ((tree->node_type == PT_SELECT) && (tree != env->pt_tree))
    {
      *continue_walk = PT_LIST_WALK;
      return tree;
    }

  /* if its not an entity_spec, we're not interested */
  if (tree->node_type != PT_SPEC)
    {
      return tree;
    }

  (void) build_graph_for_entity (env, tree, QO_BUILD_ENTITY);

  /* don't visit leaves of the entity, build_graph_for_entity already did */
  *continue_walk = PT_LIST_WALK;

  return tree;
}

/*
 * build_query_graph_post () - This post walk function will build the portion
 *			       of the query graph for each path-entity in the
 *			       entity_list (from list)
 *   return:
 *   parser(in): parser environment
 *   tree(in): tree to walk
 *   arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
build_query_graph_post (PARSER_CONTEXT * parser, PT_NODE * tree, void *arg,
			int *continue_walk)
{
  QO_ENV *env = (QO_ENV *) * (long *) arg;

  *continue_walk = PT_CONTINUE_WALK;

  /* if its not an entity_spec, we're not interested */
  if (tree->node_type != PT_SPEC)
    {
      return tree;
    }

  (void) build_graph_for_entity (env, tree, QO_BUILD_PATH);

  return tree;
}

/*
 * build_query_graph_function_index () - This pre walk function will search
 *					 the tree for expressions that match
 *					 expressions used in function indexes.
 *					 For such matched expressions,
 *					 a corresponding segment is added to
 *                                       the query graph.
 *   return: PT_NODE *
 *   parser(in): parser environment
 *   tree(in): tree to walk
 *   arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
build_query_graph_function_index (PARSER_CONTEXT * parser, PT_NODE * tree,
				  void *arg, int *continue_walk)
{
  PT_NODE *entity = NULL;
  UINTPTR spec_id = 0;
  int i, k;
  MOP cls;
  SM_CLASS_CONSTRAINT *constraints;
  QO_SEGMENT *seg;
  QO_NODE *node = NULL;
  QO_SEGMENT *seg_fi;
  const char *seg_name;
  QO_ENV *env = (QO_ENV *) * (long *) arg;

  *continue_walk = PT_CONTINUE_WALK;

  if (pt_is_function_index_expr (parser, tree, false))
    {
      if (!pt_is_join_expr (tree, &spec_id))
	{
	  for (i = 0; i < env->nnodes; i++)
	    {
	      node = QO_ENV_NODE (env, i);
	      entity = QO_NODE_ENTITY_SPEC (node);
	      if (entity->info.spec.id == spec_id)
		{
		  break;	/* found the node */
		}
	    }

	  if (entity != NULL && entity->info.spec.entity_name
	      &&
	      ((cls =
		sm_find_class (entity->info.spec.entity_name->info.
			       name.original)) != NULL))
	    {
	      constraints = sm_class_constraints (cls);
	      k = 0;
	      while (constraints != NULL)
		{
		  if (constraints->func_index_info)
		    {
		      char *expr_str =
			parser_print_function_index_expr (env->parser, tree);

		      if (expr_str != NULL
			  && !intl_identifier_casecmp (expr_str,
						       constraints->
						       func_index_info->
						       expr_str))
			{
			  for (i = 0; i < env->nsegs; i++)
			    {
			      seg_fi = QO_ENV_SEG (env, i);
			      seg_name = QO_SEG_NAME (seg_fi);
			      if ((QO_SEG_FUNC_INDEX (seg_fi) == true)
				  && !intl_identifier_casecmp (seg_name,
							       constraints->
							       func_index_info->
							       expr_str))
				{
				  /* segment already exists */
				  break;
				}
			    }
			  if (i == env->nsegs)
			    {
			      seg = qo_insert_segment (node, NULL, tree, env,
						       expr_str);
			      QO_SEG_FUNC_INDEX (seg) = true;
			      QO_SEG_NAME (seg) =
				strdup
				(constraints->func_index_info->expr_str);
			      if (QO_SEG_NAME (seg) == NULL)
				{
				  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
					  ER_OUT_OF_VIRTUAL_MEMORY,
					  1,
					  strlen
					  (constraints->func_index_info->
					   expr_str) + 1);
				  *continue_walk = PT_STOP_WALK;
				  return tree;
				}
			    }
			}
		    }
		  constraints = constraints->next;
		  k++;
		}
	    }
	}
    }
  return tree;
}

/*
 * build_graph_for_entity () - This routine will create nodes and segments
 *			       based on the parse tree entity
 *   return: QO_NODE *
 *   env(in): optimizer environment
 *   entity(in): entity to build the graph for
 *   status(in):
 *
 * Note: Any changes made to this routine should be reflected in
 *   graph_size_for_entity.  They must remain in sync or else we might not
 *   allocate enough space for the graph arrays resulting in memory
 *   corruption.
 */
static QO_NODE *
build_graph_for_entity (QO_ENV * env, PT_NODE * entity,
			QO_BUILD_STATUS status)
{
  PARSER_CONTEXT *parser;
  QO_NODE *node = NULL, *next_node;
  PT_NODE *name, *next_entity, *attr, *attr_list;
  QO_SEGMENT *seg;
  parser = QO_ENV_PARSER (env);

  if (!(status & QO_BUILD_ENTITY))
    {
      int i;
      for (i = 0; i < env->nnodes; i++)
	{
	  node = QO_ENV_NODE (env, i);
	  if (QO_NODE_ENTITY_SPEC (node) == entity)
	    {
	      break;		/* found the node */
	    }
	}

      goto build_path;
    }

  node = qo_add_node (entity, env);

  attr_list = get_referenced_attrs (entity);

  /*
   * Find the PT_NAME corresponding to this entity spec (i.e., the
   * PT_NODE that we want backing the oid segment we're about to
   * create), if such exists.
   */

  for (attr = attr_list; attr && !PT_IS_OID_NAME (attr); attr = attr->next)
    {
      ;
    }

  /*
   * 'attr' will be non-null iff the oid "attribute" of the class
   * is explicitly used in some way, e.g., in a comparison or a projection.
   * If it is non-null, it will be created with the rest of the symbols.
   *
   * If it is null, we'll make one unless we're dealing with a derived
   * table.
   */
  if (attr == NULL && entity->info.spec.derived_table == NULL)
    {
      attr = parser_new_node (parser, PT_NAME);
      if (attr == NULL)
	{
	  PT_INTERNAL_ERROR (parser, "allocate new node");
	  return NULL;
	}

      attr->info.name.resolved =
	entity->info.spec.flat_entity_list->info.name.original;
      attr->info.name.original = "";
      attr->info.name.spec_id = entity->info.spec.id;
      attr->info.name.meta_class = PT_OID_ATTR;

      /* create oid segment for the entity */
      seg = qo_insert_segment (node, NULL, attr, env, NULL);
      QO_SEG_SET_VALUED (seg) = false;	/* oid segments aren't set valued */
      QO_SEG_CLASS_ATTR (seg) = false;	/* oid segments aren't class attrs */
      QO_SEG_SHARED_ATTR (seg) = false;	/* oid segments aren't shared attrs */
    }

  /*
   * Create a segment for each symbol in the entities symbol table.
   */
  for (name = attr_list; name != NULL; name = name->next)
    {
      seg = qo_insert_segment (node, NULL, name, env, NULL);

      if ((name->type_enum == PT_TYPE_SET) ||
	  (name->type_enum == PT_TYPE_MULTISET) ||
	  (name->type_enum == PT_TYPE_SEQUENCE))
	{
	  QO_SEG_SET_VALUED (seg) = true;
	}
      else
	{
	  QO_SEG_SET_VALUED (seg) = false;
	}

      if (name->info.name.meta_class == PT_META_ATTR)
	{
	  QO_SEG_CLASS_ATTR (seg) = true;
	}
      else
	{
	  QO_SEG_CLASS_ATTR (seg) = false;
	}

      /* this needs to check a flag Bill is going to add--CHECK!!!!! */
      QO_SEG_SHARED_ATTR (seg) = false;
    }

build_path:

  if (!(status & QO_BUILD_PATH))
    {
      return node;
    }

  /* recurse and build the graph for path entities */
  for (next_entity = entity->info.spec.path_entities;
       next_entity != NULL; next_entity = next_entity->next)
    {
      next_node = build_graph_for_entity (env, next_entity,
					  (QO_BUILD_STATUS) (QO_BUILD_ENTITY |
							     QO_BUILD_PATH));

      /* for each path entity, fix the join segment */
      QO_ASSERT (env, next_node != NULL);

      /* make sure path entity contains the one and only path conjunct */
      QO_ASSERT (env, next_entity->info.spec.path_conjuncts != NULL);
      QO_ASSERT (env, next_entity->info.spec.path_conjuncts->next == NULL);

      (void) qo_join_segment (node, next_node,
			      next_entity->info.spec.path_conjuncts->
			      info.expr.arg1, env);
    }

  /* create a term for the entity's path_conjunct if one exists */
  if (entity->info.spec.path_conjuncts != NULL)
    {
      (void) qo_add_term (entity->info.spec.path_conjuncts,
			  entity->info.spec.meta_class, env);
    }

  return node;
}

/*
 * qo_add_node () - This routine adds a node to the optimizer environment
 *		 for the entity
 *   return: QO_NODE *
 *   entity(in): entity to add node for
 *   env(in): optimizer environment
 */
static QO_NODE *
qo_add_node (PT_NODE * entity, QO_ENV * env)
{
  QO_NODE *node = NULL;
  QO_CLASS_INFO *info;
  int i, n;
  CLASS_STATS *stats;

  QO_ASSERT (env, env->nnodes < env->Nnodes);
  QO_ASSERT (env, entity != NULL);
  QO_ASSERT (env, entity->node_type == PT_SPEC);

  node = QO_ENV_NODE (env, env->nnodes);

  /* fill in node */
  QO_NODE_ENV (node) = env;
  QO_NODE_ENTITY_SPEC (node) = entity;
  QO_NODE_NAME (node) = entity->info.spec.range_var->info.name.original;
  QO_NODE_IDX (node) = env->nnodes;
  QO_NODE_SORT_LIMIT_CANDIDATE (node) = false;

  env->nnodes++;

  /*
   * If derived table there will be no info.  Also if derived table
   * that is correlated to the current scope level, establish
   * dependency links to all nodes that precede it in the scope.  This
   * is overkill, but it's easier than figuring out the exact
   * information, and it's usually the same anyway.
   */
  if (entity->info.spec.derived_table == NULL
      && (info = qo_get_class_info (env, node)) != NULL)
    {
      QO_NODE_INFO (node) = info;
      for (i = 0, n = info->n; i < n; i++)
	{
	  stats = QO_GET_CLASS_STATS (&info->info[i]);
	  QO_ASSERT (env, stats != NULL);
	  if (entity->info.spec.meta_class == PT_META_CLASS)
	    {
	      /* is class OID reference spec
	       * for example: 'class x'
	       *   SELECT class_meth(class x, x.i) FROM x, class x
	       */
	      QO_NODE_NCARD (node) += 1;
	      QO_NODE_TCARD (node) += 1;
	    }
	  else
	    {
	      QO_NODE_NCARD (node) += stats->heap_num_objects;
	      QO_NODE_TCARD (node) += stats->heap_num_pages;
	    }
	}			/* for (i = ... ) */
    }
  else
    {
      QO_NODE_NCARD (node) = 5;	/* just guess */
      QO_NODE_TCARD (node) = 1;	/* just guess */

      /* recalculate derived table size */
      if (entity->info.spec.derived_table)
	{
	  XASL_NODE *xasl;

	  switch (entity->info.spec.derived_table->node_type)
	    {
	    case PT_SELECT:
	    case PT_UNION:
	    case PT_DIFFERENCE:
	    case PT_INTERSECTION:
	      xasl =
		(XASL_NODE *) entity->info.spec.derived_table->info.query.
		xasl;
	      if (xasl)
		{
		  QO_NODE_NCARD (node) = (unsigned long) xasl->cardinality;
		  QO_NODE_TCARD (node) = (unsigned long)
		    ((QO_NODE_NCARD (node) *
		      (double) xasl->projected_size) / (double) IO_PAGESIZE);
		  if (QO_NODE_TCARD (node) == 0)
		    {
		      QO_NODE_TCARD (node) = 1;
		    }
		}
	      break;
	    default:
	      break;
	    }
	}
    }

  n = QO_NODE_IDX (node);
  QO_NODE_SARGABLE (node) = true;

  switch (QO_NODE_PT_JOIN_TYPE (node))
    {
    case PT_JOIN_LEFT_OUTER:
      QO_NODE_SARGABLE (QO_ENV_NODE (env, n - 1)) = false;
      break;

    case PT_JOIN_RIGHT_OUTER:
      QO_NODE_SARGABLE (node) = false;
      break;

/* currently, not used */
/*    case PT_JOIN_FULL_OUTER:
        QO_NODE_SARGABLE(QO_ENV_NODE(env, n - 1)) = false;
        QO_NODE_SARGABLE(node) = false;
        break;
 */
    default:
      break;
    }

  return node;
}

/*
 * lookup_node () - looks up node in the node array, returns NULL if not found
 *   return: Ptr to node in node table. If node is found, entity will be set
 *	     to point to the corresponding entity spec in the parse tree.
 *   attr(in): class to look up
 *   env(in): optimizer environment
 *   entity(in): entity spec for the node
 */
QO_NODE *
lookup_node (PT_NODE * attr, QO_ENV * env, PT_NODE ** entity)
{
  int i;
  bool found = false;
  PT_NODE *aux = attr;

  if (pt_is_function_index_expr (env->parser, attr, false))
    {
      /*
       * The node should be the same for each argument of expression =>
       * once found should be returned
       */
      QO_NODE *node = NULL;
      if (attr->info.expr.op == PT_FUNCTION_HOLDER)
	{
	  PT_NODE *func = attr->info.expr.arg1;
	  for (aux = func->info.function.arg_list; aux != NULL;
	       aux = aux->next)
	    {
	      PT_NODE *save_aux = aux;
	      aux = pt_function_index_skip_expr (aux);
	      if (aux->node_type == PT_NAME)
		{
		  node = lookup_node (aux, env, entity);
		  if (node == NULL)
		    {
		      return NULL;
		    }
		  else
		    {
		      return node;
		    }
		}
	      aux = save_aux;
	    }
	}
      else
	{
	  aux = pt_function_index_skip_expr (attr->info.expr.arg1);
	  if (aux)
	    {
	      if (aux->node_type == PT_NAME)
		{
		  node = lookup_node (aux, env, entity);
		  if (node == NULL)
		    {
		      return NULL;
		    }
		  else
		    {
		      return node;
		    }
		}
	    }
	  aux = pt_function_index_skip_expr (attr->info.expr.arg2);
	  if (aux)
	    {
	      if (aux->node_type == PT_NAME)
		{
		  node = lookup_node (aux, env, entity);
		  if (node == NULL)
		    {
		      return NULL;
		    }
		  else
		    {
		      return node;
		    }
		}
	    }
	  aux = pt_function_index_skip_expr (attr->info.expr.arg3);
	  if (aux)
	    {
	      if (aux->node_type == PT_NAME)
		{
		  node = lookup_node (aux, env, entity);
		  if (node == NULL)
		    {
		      return NULL;
		    }
		  else
		    {
		      return node;
		    }
		}
	    }
	}
      /* node is null at this point */
      return node;
    }

  QO_ASSERT (env, aux->node_type == PT_NAME);

  for (i = 0; (!found) && (i < env->nnodes); /* no increment step */ )
    {
      *entity = QO_NODE_ENTITY_SPEC (QO_ENV_NODE (env, i));
      if ((*entity)->info.spec.id == aux->info.name.spec_id)
	{
	  found = true;
	}
      else
	{
	  i++;
	}
    }

  return ((found) ? QO_ENV_NODE (env, i) : NULL);
}

/*
 * qo_insert_segment () - inserts a segment into the optimizer environment
 *   return: QO_SEGMENT *
 *   head(in): head of the segment
 *   tail(in): tail of the segment
 *   node(in): pt_node that gave rise to this segment
 *   env(in): optimizer environment
 *   expr_str(in): function index expression (if needed - NULL for a normal or
 *		      filter index
 */
static QO_SEGMENT *
qo_insert_segment (QO_NODE * head, QO_NODE * tail, PT_NODE * node,
		   QO_ENV * env, const char *expr_str)
{
  QO_SEGMENT *seg = NULL;

  QO_ASSERT (env, head != NULL);
  QO_ASSERT (env, env->nsegs < env->Nsegs);

  seg = QO_ENV_SEG (env, env->nsegs);

  /* fill in seg */
  QO_SEG_PT_NODE (seg) = node;
  QO_SEG_HEAD (seg) = head;
  QO_SEG_TAIL (seg) = tail;
  QO_SEG_IDX (seg) = env->nsegs;
  /* add dummy name to segment
   * example: dummy attr from view transfrom
   *   select count(*) from v
   *   select count(*) from (select {v}, 1 from t) v (v, 1)
   *
   * here, '1' is dummy attr
   * set empty string to avoid core crash
   */
  if (node)
    {
      QO_SEG_NAME (seg) = node->info.name.original ?
	node->info.name.original :
	pt_append_string (QO_ENV_PARSER (env), NULL, "");
      if (PT_IS_OID_NAME (node))
	{
	  /* this is an oid segment */
	  QO_NODE_OID_SEG (head) = seg;
	  QO_SEG_INFO (seg) = NULL;
	}
      else if (!PT_IS_CLASSOID_NAME (node))
	{
	  /* Ignore CLASSOIDs.  They are generated by updates on the server
	   * and can be treated as any other projected column.  We don't
	   * need to know anything else about this attr since it can not
	   * be used as an index or in any other interesting way.
	   */
	  if (node->node_type == PT_NAME)
	    {
	      QO_SEG_INFO (seg) = qo_get_attr_info (env, seg);
	    }
	  else
	    {
	      QO_SEG_INFO (seg) = qo_get_attr_info_func_index (env, seg,
							       expr_str);
	    }
	}
    }

  bitset_add (&(QO_NODE_SEGS (head)), QO_SEG_IDX (seg));

  env->nsegs++;

  return seg;
}

/*
 * qo_join_segment () - This routine will look for the segment and set its tail
 *		     to the correct node
 *   return: QO_SEGMENT *
 *   head(in): head of join segment
 *   tail(in): tail of join segment
 *   name(in): name of join segment
 *   env(in): optimizer environment
 */
static QO_SEGMENT *
qo_join_segment (QO_NODE * head, QO_NODE * tail, PT_NODE * name, QO_ENV * env)
{
  QO_SEGMENT *seg = NULL;

  seg = lookup_seg (head, name, env);
  QO_ASSERT (env, seg != NULL);

  QO_SEG_TAIL (seg) = tail;	/* may be redundant */

  return seg;
}

/*
 * lookup_seg () -
 *   return: ptr to segment in segment table, or NULL if the segment is not
 *	     in the table
 *   head(in): head of the segment
 *   name(in): name of the segment
 *   env(in): optimizer environment
 */
QO_SEGMENT *
lookup_seg (QO_NODE * head, PT_NODE * name, QO_ENV * env)
{
  int i;
  bool found = false;

  if (pt_is_function_index_expr (env->parser, name, false))
    {
      int k = -1;
      /* we search through the segments that come from a function
       * index. If one of these are matched by the PT_NODE, there
       * is no need to search other segments, since they will never
       * match
       */
      const char *expr_str =
	parser_print_function_index_expr (QO_ENV_PARSER (env), name);

      if (expr_str != NULL)
	{
	  for (i = 0; (!found) && (i < env->nsegs); i++)
	    {
	      if (QO_SEG_FUNC_INDEX (QO_ENV_SEG (env, i)) == false)
		{
		  continue;
		}

	      /* match function index expression against the expression
	       * in the given query
	       */
	      if (!intl_identifier_casecmp (QO_SEG_NAME (QO_ENV_SEG (env, i)),
					    expr_str))
		{
		  found = true;
		  k = i;
		}
	    }
	}

      return ((found) ? QO_ENV_SEG (env, k) : NULL);
    }

  for (i = 0; (!found) && (i < env->nsegs); /* no increment step */ )
    {
      if (QO_SEG_HEAD (QO_ENV_SEG (env, i)) == head
	  && pt_name_equal (QO_ENV_PARSER (env),
			    QO_SEG_PT_NODE (QO_ENV_SEG (env, i)), name))
	{
	  found = true;
	}
      else
	{
	  i++;
	}
    }

  return ((found) ? QO_ENV_SEG (env, i) : NULL);
}

/*
 * qo_add_final_segment () -
 *   return: PT_NODE *
 *   parser(in): parser environment
 *   tree(in): tree to walk
 *   arg(in):
 *   continue_walk(in):
 *
 * Note: This walk "pre" function looks up the segment for each
 *      node in the list.  If the node is a PT_NAME node, it can use it to
 *      find the final segment.  If the node is a dot expression, the final
 *      segment will be the segment associated with the PT_NAME node that is
 *      arg2 of the dot expression.
 */
static PT_NODE *
qo_add_final_segment (PARSER_CONTEXT * parser, PT_NODE * tree, void *arg,
		      int *continue_walk)
{
  QO_ENV *env = (QO_ENV *) * (long *) arg;

  *continue_walk = PT_CONTINUE_WALK;

  if (tree->node_type == PT_NAME)
    {
      (void) set_seg_node (tree, env, &env->final_segs);
      *continue_walk = PT_LIST_WALK;
    }
  else if ((tree->node_type == PT_DOT_))
    {
      (void) set_seg_node (tree->info.dot.arg2, env, &env->final_segs);
      *continue_walk = PT_LIST_WALK;
    }

  return tree;			/* don't alter tree structure */
}

/*
 * qo_add_term () - Creates a new term in the env term table
 *   return: void
 *   conjunct(in): term to add
 *   term_type(in): is the term a path term?
 *   env(in): optimizer environment
 */
static QO_TERM *
qo_add_term (PT_NODE * conjunct, int term_type, QO_ENV * env)
{
  QO_TERM *term;
  QO_NODE *node;

  QO_ASSERT (env, conjunct->next == NULL);

  /* The conjuct could be PT_VALUE(0);
     (1) if an outer join condition was derived/transformed to the
     always-false ON condition when type checking, expression evaluation
     or query rewrite transformation. We should sustained it for correct
     outer join plan. It's different from ordinary WHERE condition.
     (2) Or is an always-false WHERE condition */
  QO_ASSERT (env, conjunct->node_type == PT_EXPR ||
	     conjunct->node_type == PT_VALUE);
  QO_ASSERT (env, env->nterms < env->Nterms);

  term = QO_ENV_TERM (env, env->nterms);

  /* fill in term */
  QO_TERM_CLASS (term) = QO_TC_SARG;	/* assume sarg until proven otherwise */
  QO_TERM_JOIN_TYPE (term) = NO_JOIN;
  QO_TERM_PT_EXPR (term) = conjunct;
  QO_TERM_LOCATION (term) = (conjunct->node_type == PT_EXPR ?
			     conjunct->info.expr.location :
			     conjunct->info.value.location);
  QO_TERM_SELECTIVITY (term) = 0.0;
  QO_TERM_RANK (term) = 0;
  QO_TERM_FLAG (term) = 0;	/* init */
  QO_TERM_IDX (term) = env->nterms;

  env->nterms++;

  if (conjunct->node_type == PT_EXPR)
    {
      (void) qo_analyze_term (term, term_type);
    }
  else
    {
      /* conjunct->node_type == PT_VALUE */
      if (!pt_false_search_condition (QO_ENV_PARSER (env), conjunct))
	{
	  /* is an always-true WHERE condition */
	  QO_TERM_SELECTIVITY (term) = 1.0;
	}

      if (conjunct->info.value.location == 0)
	{
	  /* is an always-false WHERE condition */
	  QO_TERM_CLASS (term) = QO_TC_OTHER;	/* is dummy */
	}
      else
	{
	  /* Assume 'conjunct->info.value.location' is same to QO_NODE idx */
	  node = QO_ENV_NODE (env, conjunct->info.value.location);
	  switch (QO_NODE_PT_JOIN_TYPE (node))
	    {
	    case PT_JOIN_INNER:
	      /* add always-false arg to each X, Y
	       * example: SELECT ... FROM X inner join Y on 0 <> 0;
	       */
	      bitset_add (&(QO_TERM_NODES (term)), QO_NODE_IDX (node) - 1);

	      term = QO_ENV_TERM (env, env->nterms);

	      /* fill in term */
	      QO_TERM_CLASS (term) = QO_TC_SARG;
	      QO_TERM_JOIN_TYPE (term) = NO_JOIN;
	      QO_TERM_PT_EXPR (term) = conjunct;
	      QO_TERM_LOCATION (term) = conjunct->info.value.location;
	      if (!pt_false_search_condition (QO_ENV_PARSER (env), conjunct))
		{
		  /* is an always-true WHERE condition */
		  QO_TERM_SELECTIVITY (term) = 1.0;
		}
	      else
		{
		  QO_TERM_SELECTIVITY (term) = 0.0;
		}
	      QO_TERM_RANK (term) = 0;
	      QO_TERM_FLAG (term) = 0;	/* init */
	      QO_TERM_IDX (term) = env->nterms;

	      env->nterms++;

	      bitset_add (&(QO_TERM_NODES (term)), QO_NODE_IDX (node));
	      break;
	    case PT_JOIN_LEFT_OUTER:
	      bitset_add (&(QO_TERM_NODES (term)), QO_NODE_IDX (node));
	      break;
	    case PT_JOIN_RIGHT_OUTER:
	      bitset_add (&(QO_TERM_NODES (term)), QO_NODE_IDX (node) - 1);
	      break;
	    case PT_JOIN_FULL_OUTER:	/* not used */
	      /* I don't know what is to be done for full outer. */
	      break;
	    default:
	      /* this should not happen */
	      bitset_add (&(QO_TERM_NODES (term)), QO_NODE_IDX (node));
	      break;
	    }
	}			/* else */
    }

  return term;
}

/*
 * qo_add_dep_term () -
 *   return: void
 *   derived_node(in): The node representing the dependent derived table
 *   depend_nodes(in):
 *   depend_segs(in):
 *   env(in): optimizer environment
 *
 * Note: Creates a new QO_TC_DEP_LINK term in the env term table, plus
 *	QO_TC_DEP_JOIN terms as necessary.  QO_TC_DEP_LINK terms are
 *	used only to capture dependency information between a node
 *	representing a dependent derived table and a node on which
 *	that derived table depends.
 */
static void
qo_add_dep_term (QO_NODE * derived_node,
		 BITSET * depend_nodes, BITSET * depend_segs, QO_ENV * env)
{
  QO_TERM *term = NULL;
  BITSET_ITERATOR bi;
  int ni, di;

  QO_ASSERT (env, env->nterms < env->Nterms);

  term = QO_ENV_TERM (env, env->nterms);

  bitset_assign (&(QO_NODE_DEP_SET (derived_node)), depend_nodes);

  /* fill in term */
  QO_TERM_CLASS (term) = QO_TC_DEP_LINK;
  QO_TERM_PT_EXPR (term) = NULL;
  QO_TERM_LOCATION (term) = 0;
  QO_TERM_SELECTIVITY (term) = 1.0;
  QO_TERM_RANK (term) = 0;
  QO_TERM_FLAG (term) = 0;
  QO_TERM_IDX (term) = env->nterms;
  QO_TERM_JOIN_TYPE (term) = JOIN_INNER;
  /*
   * This is misleading if |depend_nodes| > 1, but the planner is the
   * only party relying on this information, and it understands the
   * rules of the game.
   */
  QO_TERM_HEAD (term) = QO_ENV_NODE (env, bitset_first_member (depend_nodes));
  QO_TERM_TAIL (term) = derived_node;

  bitset_assign (&(QO_TERM_NODES (term)), depend_nodes);
  bitset_add (&(QO_TERM_NODES (term)), QO_NODE_IDX (derived_node));
  bitset_assign (&(QO_TERM_SEGS (term)), depend_segs);
  /*
   * Add this term to env->fake_terms so that we're not tempted to sarg
   * it if a mergeable join term between these nodes is also present.
   * This is part of the fix for PR 7314.
   */
  bitset_add (&(env->fake_terms), QO_TERM_IDX (term));

  env->nterms++;

  ni = bitset_iterate (depend_nodes, &bi);
  while ((di = bitset_next_member (&bi)) != -1)
    {
      QO_ASSERT (env, env->nterms < env->Nterms);
      term = QO_ENV_TERM (env, env->nterms);
      QO_TERM_CLASS (term) = QO_TC_DEP_JOIN;
      QO_TERM_PT_EXPR (term) = NULL;
      QO_TERM_SELECTIVITY (term) = 1.0;
      QO_TERM_RANK (term) = 0;
      QO_TERM_FLAG (term) = 0;
      QO_TERM_IDX (term) = env->nterms;
      bitset_add (&(QO_TERM_NODES (term)), ni);
      bitset_add (&(QO_TERM_NODES (term)), di);
      QO_TERM_HEAD (term) = QO_ENV_NODE (env, ni);
      QO_TERM_TAIL (term) = QO_ENV_NODE (env, di);
      QO_TERM_JOIN_TYPE (term) = JOIN_INNER;
      /*
       * Do NOT add these terms to env->fake_terms, because (unlike the
       * DEP_LINK terms) there is no restriction on how they can be
       * implemented (e.g., if there's a mergeable term available to
       * join the two nodes, it's ok to use it).
       */
      env->nterms++;
    }

}

/*
 * qo_add_dummy_join_term () - Make and add dummy join term if there's no explicit
 *			    join term related with given two nodes
 *   return: void
 *   env(in): optimizer environment
 *   p_node(in):
 *   on_node(in):
 */
static QO_TERM *
qo_add_dummy_join_term (QO_ENV * env, QO_NODE * p_node, QO_NODE * on_node)
{
  QO_TERM *term;

  QO_ASSERT (env, env->nterms < env->Nterms);
  QO_ASSERT (env, QO_NODE_IDX (p_node) >= 0);
  QO_ASSERT (env, QO_NODE_IDX (p_node) + 1 == QO_NODE_IDX (on_node));
  QO_ASSERT (env, QO_NODE_LOCATION (on_node) > 0);

  term = QO_ENV_TERM (env, env->nterms);

  /* fill in term */
  QO_TERM_CLASS (term) = QO_TC_DUMMY_JOIN;
  bitset_add (&(QO_TERM_NODES (term)), QO_NODE_IDX (p_node));
  bitset_add (&(QO_TERM_NODES (term)), QO_NODE_IDX (on_node));
  QO_TERM_HEAD (term) = p_node;
  QO_TERM_TAIL (term) = on_node;
  QO_TERM_PT_EXPR (term) = NULL;
  QO_TERM_LOCATION (term) = QO_NODE_LOCATION (on_node);
  QO_TERM_SELECTIVITY (term) = 1.0;
  QO_TERM_RANK (term) = 0;

  switch (QO_NODE_PT_JOIN_TYPE (on_node))
    {
    case PT_JOIN_INNER:
      QO_TERM_JOIN_TYPE (term) = JOIN_INNER;
      break;
    case PT_JOIN_LEFT_OUTER:
      QO_TERM_JOIN_TYPE (term) = JOIN_LEFT;
      break;
    case PT_JOIN_RIGHT_OUTER:
      QO_TERM_JOIN_TYPE (term) = JOIN_RIGHT;
      break;
    case PT_JOIN_FULL_OUTER:	/* not used */
      QO_TERM_JOIN_TYPE (term) = JOIN_OUTER;
      break;
    default:
      /* this should not happen */
      assert (false);
      QO_TERM_JOIN_TYPE (term) = JOIN_INNER;
      break;
    }
  QO_TERM_FLAG (term) = 0;
  QO_TERM_IDX (term) = env->nterms;

  env->nterms++;

  /* record outer join dependecy */
  if (QO_ON_COND_TERM (term))
    {
      QO_ASSERT (env, QO_TERM_LOCATION (term) == QO_NODE_LOCATION (on_node));

      bitset_union (&(QO_NODE_OUTER_DEP_SET (on_node)),
		    &(QO_NODE_OUTER_DEP_SET (p_node)));
      bitset_add (&(QO_NODE_OUTER_DEP_SET (on_node)), QO_NODE_IDX (p_node));
    }

  QO_ASSERT (env, QO_TERM_CAN_USE_INDEX (term) == 0);

  return term;
}

/*
 * qo_analyze_term () - determine the selectivity and class of the given term
 *   return: void
 *   term(in): term to analyze
 *   term_type(in): predicate, path or selector path term
 */
static void
qo_analyze_term (QO_TERM * term, int term_type)
{
  QO_ENV *env;
  PARSER_CONTEXT *parser;
  int merge_applies, lhs_indexable, rhs_indexable;
  PT_NODE *pt_expr, *lhs_expr, *rhs_expr;
  QO_NODE *head_node = NULL, *tail_node = NULL;
  QO_SEGMENT *head_seg, *tail_seg;
  BITSET lhs_segs, rhs_segs, lhs_nodes, rhs_nodes;
  BITSET_ITERATOR iter;
  PT_OP_TYPE op_type = PT_AND;
  int i, n, t;

  QO_ASSERT (env, QO_TERM_LOCATION (term) >= 0);

  env = QO_TERM_ENV (term);
  parser = QO_ENV_PARSER (env);
  pt_expr = QO_TERM_PT_EXPR (term);
  merge_applies = 1;		/* until proven otherwise */
  lhs_indexable = rhs_indexable = 0;	/* until proven as indexable */
  lhs_expr = rhs_expr = NULL;

  bitset_init (&lhs_segs, env);
  bitset_init (&rhs_segs, env);
  bitset_init (&lhs_nodes, env);
  bitset_init (&rhs_nodes, env);

  if (pt_expr->node_type != PT_EXPR)
    {
      goto wrapup;
    }

  /* only interesting in one predicate term; if 'term' has 'or_next', it was
     derived from OR term */
  if (pt_expr->or_next == NULL)
    {
      QO_TERM_SET_FLAG (term, QO_TERM_SINGLE_PRED);

      op_type = pt_expr->info.expr.op;
      switch (op_type)
	{

	  /* operators classified as lhs- and rhs-indexable */
	case PT_EQ:
	  QO_TERM_SET_FLAG (term, QO_TERM_EQUAL_OP);
	case PT_LT:
	case PT_LE:
	case PT_GT:
	case PT_GE:
	  /* temporary guess; RHS could be a indexable segment */
	  rhs_indexable = 1;
	  /* no break; fall through */

	  /* operators classified as rhs-indexable */
	case PT_BETWEEN:
	case PT_RANGE:
	  if (op_type == PT_RANGE)
	    {
	      assert (pt_expr->info.expr.arg2 != NULL);

	      if (pt_expr->info.expr.arg2)
		{
		  PT_NODE *between_and;

		  between_and = pt_expr->info.expr.arg2;
		  if (between_and->or_next)
		    {
		      /* is RANGE (r1, r2, ...) */
		      QO_TERM_SET_FLAG (term, QO_TERM_RANGELIST);
		    }

		  for (; between_and; between_and = between_and->or_next)
		    {
		      if (between_and->info.expr.op != PT_BETWEEN_EQ_NA)
			{
			  break;
			}
		    }

		  if (between_and == NULL)
		    {
		      /* All ranges are EQ */
		      QO_TERM_SET_FLAG (term, QO_TERM_EQUAL_OP);
		    }
		}
	    }
	case PT_IS_IN:
	case PT_EQ_SOME:
	  /* temporary guess; LHS could be a indexable segment */
	  lhs_indexable = 1;
	  /* no break; fall through */

	  /* operators classified as not-indexable */
	case PT_NOT_BETWEEN:
	case PT_IS_NOT_IN:
	case PT_GE_SOME:
	case PT_GT_SOME:
	case PT_LT_SOME:
	case PT_LE_SOME:
	case PT_EQ_ALL:
	case PT_GE_ALL:
	case PT_GT_ALL:
	case PT_LT_ALL:
	case PT_LE_ALL:
	case PT_NE:
	case PT_SETEQ:
	case PT_SETNEQ:
	case PT_SUPERSETEQ:
	case PT_SUPERSET:
	case PT_SUBSET:
	case PT_SUBSETEQ:
	case PT_NE_SOME:
	case PT_NE_ALL:
	case PT_LIKE:
	case PT_NOT_LIKE:
	case PT_RLIKE:
	case PT_NOT_RLIKE:
	case PT_RLIKE_BINARY:
	case PT_NOT_RLIKE_BINARY:
	case PT_NULLSAFE_EQ:
	  /* RHS of the expression */
	  rhs_expr = pt_expr->info.expr.arg2;
	  /* get segments from RHS of the expression */
	  qo_expr_segs (env, rhs_expr, &rhs_segs);
	  /* no break; fall through */

	case PT_IS_NULL:
	case PT_IS_NOT_NULL:
	case PT_EXISTS:
	case PT_IS:
	case PT_IS_NOT:
	  /* LHS of the expression */
	  lhs_expr = pt_expr->info.expr.arg1;
	  /* get segments from LHS of the expression */
	  qo_expr_segs (env, lhs_expr, &lhs_segs);
	  /* now break switch statement */
	  break;

	case PT_OR:
	case PT_NOT:
	case PT_XOR:
	  /* get segments from the expression itself */
	  qo_expr_segs (env, pt_expr, &lhs_segs);
	  break;

	  /* the other operators that can not be used as term; error case */
	default:
	  /* stop processing */
	  QO_ABORT (env);

	}			/* switch (op_type) */
    }
  else
    {				/* if (pt_expr->or_next == NULL) */
      /* term that consist of more than one predicates; do same as PT_OR */

      qo_expr_segs (env, pt_expr, &lhs_segs);

    }				/* if (pt_expr->or_next == NULL) */

  /* get nodes from segments */
  qo_seg_nodes (env, &lhs_segs, &lhs_nodes);
  qo_seg_nodes (env, &rhs_segs, &rhs_nodes);

  /* do LHS and RHS of the term belong to the different node? */
  if (!bitset_intersects (&lhs_nodes, &rhs_nodes))
    {
      i = 0;			/* idx of term->index_seg[] array; it shall be 0 or 1 */

      /* There terms look like they might be candidates for implementation
         via indexes. Make sure that they really are candidates.
         IMPORTANT: this is not the final say, since we don't know at this
         point whether indexes actually exist or not. We won't know that
         until a later phase (qo_discover_indexes()). Right now we're just
         determining whether these terms qualify structurally. */

      /* examine if LHS is indexable or not? */

      /* is LHS a type of name(attribute) of local database */
      if (lhs_indexable)
	{
	  if (is_local_name (env, lhs_expr))
	    {
	      if (lhs_expr->type_enum == PT_TYPE_ENUMERATION)
		{
		  /* lhs is indexable only if this is an equality
		     comparison */
		  op_type = pt_expr->info.expr.op;
		  switch (op_type)
		    {
		    case PT_EQ:
		    case PT_IS_IN:
		    case PT_EQ_SOME:
		    case PT_NULLSAFE_EQ:
		      break;
		    case PT_RANGE:
		      if (!QO_TERM_IS_FLAGED (term, QO_TERM_EQUAL_OP))
			{
			  lhs_indexable = 0;
			}
		      break;
		    default:
		      lhs_indexable = 0;
		      break;
		    }
		}
	      else
		{
		  lhs_indexable = 1;
		}
	    }
	  else
	    {
	      lhs_indexable = 0;
	    }
	}
      if (lhs_indexable
	  && (pt_is_function_index_expr (parser, lhs_expr, false)
	      || (lhs_expr && lhs_expr->info.expr.op == PT_PRIOR
		  && pt_is_function_index_expr (parser,
						lhs_expr->info.expr.arg1,
						false))))
	{
	  /* we should be dealing with a function indexable expression,
	   * so we must check if a segment has been associated with it
	   */
	  n = bitset_first_member (&lhs_segs);
	  if ((n == -1) || (QO_SEG_FUNC_INDEX (QO_ENV_SEG (env, n)) == false))
	    {
	      lhs_indexable = 0;
	    }
	}
      if (lhs_indexable)
	{

	  if (op_type == PT_IS_IN || op_type == PT_EQ_SOME)
	    {
	      /* We have to be careful with this case because
	         "i IN (SELECT ...)"
	         has a special meaning: in this case the select is treated as
	         UNBOX_AS_TABLE instead of the usual UNBOX_AS_VALUE, and we
	         can't use an index even if we want to (because of an XASL
	         deficiency).Because pt_is_pseudo_const() wants to believe that
	         subqueries are pseudo-constants, we have to check for that
	         condition outside of pt_is_pseudo_const(). */
	      switch (rhs_expr->node_type)
		{
		case PT_SELECT:
		case PT_UNION:
		case PT_DIFFERENCE:
		case PT_INTERSECTION:
		  lhs_indexable = 0;
		  break;
		case PT_NAME:
		  if (rhs_expr->info.name.meta_class != PT_PARAMETER
		      && pt_is_set_type (rhs_expr))
		    {
		      lhs_indexable = 0;
		    }
		  break;
		case PT_DOT_:
		  if (pt_is_set_type (rhs_expr))
		    {
		      lhs_indexable = 0;
		    }
		  break;
		case PT_VALUE:
		  if (op_type == PT_EQ_SOME &&
		      rhs_expr->info.value.db_value_is_initialized &&
		      db_value_type_is_collection (&
						   (rhs_expr->info.value.
						    db_value)))
		    {
		      /* if we have the = some{} operator check its size */
		      DB_COLLECTION *db_collectionp =
			db_get_collection (&(rhs_expr->info.value.db_value));

		      if (db_col_size (db_collectionp) == 0)
			{
			  lhs_indexable = 0;
			}
		    }
		  lhs_indexable &= pt_is_pseudo_const (rhs_expr);
		  break;
		default:
		  lhs_indexable &= pt_is_pseudo_const (rhs_expr);
		}
	    }
	  else
	    {
	      /* is LHS attribute and is RHS constant value ? */
	      lhs_indexable &= pt_is_pseudo_const (rhs_expr);
	    }
	}

      /* check LHS and RHS for collations that invalidate the term's use in a
       * key range/filter; if one of the expr sides contains such a collation
       * then the whole term is not indexable */
      if (lhs_indexable || rhs_indexable)
	{
	  int has_nis_coll = 0;

	  (void) parser_walk_tree (parser, lhs_expr,
				   pt_has_non_idx_sarg_coll_pre,
				   &has_nis_coll, NULL, NULL);
	  (void) parser_walk_tree (parser, rhs_expr,
				   pt_has_non_idx_sarg_coll_pre,
				   &has_nis_coll, NULL, NULL);

	  if (has_nis_coll)
	    {
	      QO_TERM_SET_FLAG (term, QO_TERM_NON_IDX_SARG_COLL);
	      lhs_indexable = 0;
	      rhs_indexable = 0;
	    }
	}

      if (lhs_indexable)
	{
	  n = bitset_first_member (&lhs_segs);
	  if (n != -1)
	    {
	      /* record in the term that it has indexable segment as LHS */
	      term->index_seg[i++] = QO_ENV_SEG (env, n);
	    }
	}

      /* examine if LHS is indexable or not? */
      if (rhs_indexable)
	{
	  if (is_local_name (env, rhs_expr) && pt_is_pseudo_const (lhs_expr))
	    {
	      /* is RHS attribute and is LHS constant value ? */
	      if (rhs_expr->type_enum == PT_TYPE_ENUMERATION)
		{
		  /* lhs is indexable only if this is an equality
		     comparison */
		  op_type = pt_expr->info.expr.op;
		  switch (op_type)
		    {
		    case PT_EQ:
		    case PT_IS_IN:
		    case PT_EQ_SOME:
		    case PT_NULLSAFE_EQ:
		      break;
		    case PT_RANGE:
		      if (!QO_TERM_IS_FLAGED (term, QO_TERM_EQUAL_OP))
			{
			  rhs_indexable = 0;
			}
		      break;
		    default:
		      rhs_indexable = 0;
		      break;
		    }
		}
	      else
		{
		  rhs_indexable = 1;
		}
	    }
	  else
	    {
	      rhs_indexable = 0;
	    }
	}
      if (rhs_indexable
	  && (pt_is_function_index_expr (term->env->parser, rhs_expr, false)
	      || (rhs_expr && rhs_expr->info.expr.op == PT_PRIOR
		  && pt_is_function_index_expr (term->env->parser,
						rhs_expr->info.expr.arg1,
						false))))
	{
	  /* we should be dealing with a function indexable expression,
	   * so we must check if a segment has been associated with it
	   */
	  n = bitset_first_member (&rhs_segs);
	  if ((n == -1) || (QO_SEG_FUNC_INDEX (QO_ENV_SEG (env, n)) == false))
	    {
	      rhs_indexable = 0;
	    }
	}
      if (rhs_indexable)
	{
	  if (!lhs_indexable)
	    {
	      op_type = pt_converse_op (op_type);
	      if (op_type != 0)
		{
		  /* converse 'const op attr' to 'attr op const' */
		  PT_NODE *tmp;

		  tmp = pt_expr->info.expr.arg2;
		  pt_expr->info.expr.arg2 = pt_expr->info.expr.arg1;
		  pt_expr->info.expr.arg1 = tmp;
		  pt_expr->info.expr.op = op_type;
		}
	      else
		{
		  /* must be impossible error. check pt_converse_op() */
		  QO_ABORT (env);
		}
	    }

	  n = bitset_first_member (&rhs_segs);
	  if (n != -1)
	    {
	      /* record in the term that it has indexable segment as RHS */
	      term->index_seg[i++] = QO_ENV_SEG (env, n);
	    }
	}			/* if (rhs_indexable) */


      QO_TERM_CAN_USE_INDEX (term) = i;	/* cardinality of term->index_seg[] array */

    }
  else
    {				/* if (!bitset_intersects(&lhs_nodes, &rhs_nodes)) */

      merge_applies = 0;
      QO_TERM_CAN_USE_INDEX (term) = 0;

    }				/* if (!bitset_intersects(&lhs_nodes, &rhs_nodes)) */


  /* fill in segment and node information of QO_TERM structure */
  bitset_assign (&(QO_TERM_SEGS (term)), &lhs_segs);
  bitset_union (&(QO_TERM_SEGS (term)), &rhs_segs);
  bitset_assign (&(QO_TERM_NODES (term)), &lhs_nodes);
  bitset_union (&(QO_TERM_NODES (term)), &rhs_nodes);

  /* number of nodes with which this term associated */
  n = bitset_cardinality (&(QO_TERM_NODES (term)));
  /* determine the class of the term */
  if (term_type != PREDICATE_TERM)
    {
      QO_ASSERT (env, n >= 2);

      QO_TERM_CLASS (term) = QO_TC_PATH;

      if (n == 2)
	{
	  /* i.e., it's a path term...
	     In this case, it's imperative that we get the head and tail
	     nodes and segs right. Fortunately, in this particular case
	     we can rely on the compiler to produce the term in a
	     consistent way, with the head on the lhs and the tail on
	     the rhs. */
	  head_node = QO_ENV_NODE (env, bitset_first_member (&lhs_nodes));
	  tail_node = QO_ENV_NODE (env, bitset_first_member (&rhs_nodes));

	  QO_ASSERT (env, QO_NODE_IDX (head_node) < QO_NODE_IDX (tail_node));
	}
    }
  else if (n == 0)
    {
      bool inst_num = false;

      (void) parser_walk_tree (parser, pt_expr, pt_check_instnum_pre,
			       NULL, pt_check_instnum_post, &inst_num);
      QO_TERM_CLASS (term) =
	inst_num ? QO_TC_TOTALLY_AFTER_JOIN : QO_TC_OTHER;
    }
  else if (n == 1)
    {
      QO_TERM_CLASS (term) = QO_TC_SARG;

      /* QO_NODE to which this sarg term belongs */
      head_node =
	QO_ENV_NODE (env, bitset_iterate (&(QO_TERM_NODES (term)), &iter));
    }
  else if (n == 2)
    {
      QO_TERM_CLASS (term) = QO_TC_JOIN;

      /* Although it may be tempting to say that the head node is the first
         member of the lhs_nodes and the tail node is the first member of the
         rhs_nodes, that's not always true. For example, a term like
         "x.a + y.b < 100"
         can get in here, and then *both* head and tail are in lhs_nodes.
         If you get down into the code guarded by 'merge_applies' you can
         safely make more stringent assumptions, but not before then. */

      head_node =
	QO_ENV_NODE (env, bitset_iterate (&(QO_TERM_NODES (term)), &iter));
      tail_node = QO_ENV_NODE (env, bitset_next_member (&iter));

      if (QO_NODE_IDX (head_node) > QO_NODE_IDX (tail_node))
	{
	  QO_NODE *swap_node = NULL;

	  swap_node = head_node;
	  head_node = tail_node;
	  tail_node = swap_node;
	}
      QO_ASSERT (env, QO_NODE_IDX (head_node) < QO_NODE_IDX (tail_node));
      QO_ASSERT (env, QO_NODE_IDX (tail_node) > 0);
    }
  else
    {				/* n >= 3 */
      QO_TERM_CLASS (term) = QO_TC_OTHER;
    }

  if (n == 2)
    {
      QO_ASSERT (env, QO_TERM_CLASS (term) == QO_TC_PATH
		 || QO_TERM_CLASS (term) == QO_TC_JOIN);

      /* This is a pretty weak test; it only looks for equality
         comparisons. */
      merge_applies &= expr_is_mergable (pt_expr);

      /* check for dependent edge. do not join with it */
      if (BITSET_MEMBER (QO_NODE_DEP_SET (head_node), QO_NODE_IDX (tail_node))
	  || BITSET_MEMBER (QO_NODE_DEP_SET (tail_node),
			    QO_NODE_IDX (head_node)))
	{
	  QO_TERM_CLASS (term) = QO_TC_OTHER;

	  merge_applies = 0;
	}

      /* And there had better be something on both sides of the comparison
         too. You don't want to be misled by something like
         "x.a + y.b = 100"
         because that's definitely not mergeable right now. Perhaps if we
         rewrote it like
         "x.a = 100 - y.b"
         but that seems to be stretching things a little bit. */
      merge_applies &= (!bitset_is_empty (&lhs_segs)
			&& !bitset_is_empty (&rhs_segs));

      if (merge_applies || QO_TERM_CLASS (term) == QO_TC_PATH)
	{
	  head_seg =
	    QO_ENV_SEG (env, bitset_iterate (&(QO_TERM_SEGS (term)), &iter));
	  for (t = bitset_iterate (&(QO_TERM_SEGS (term)), &iter); t != -1;
	       t = bitset_next_member (&iter))
	    {
	      tail_seg = QO_ENV_SEG (env, t);
	      if (QO_NODE_IDX (QO_SEG_HEAD (tail_seg))
		  != QO_NODE_IDX (QO_SEG_HEAD (head_seg)))
		{
		  break;	/* found tail */
		}
	    }

	  /* Now make sure that the head and tail segs correspond to the
	     proper nodes. */
	  if (QO_SEG_HEAD (head_seg) != head_node)
	    {
	      QO_SEGMENT *swap_seg = NULL;

	      swap_seg = head_seg;
	      head_seg = tail_seg;
	      tail_seg = swap_seg;
	    }

	  QO_ASSERT (env, QO_SEG_HEAD (head_seg) == head_node);
	  QO_ASSERT (env, QO_SEG_HEAD (tail_seg) == tail_node);

	  /* These are really only interesting for path terms, but it doesn't
	     hurt to set them for others too. */
	  QO_TERM_SEG (term) = head_seg;
	  QO_TERM_OID_SEG (term) = tail_seg;

	  /* The term might be a merge term (i.e., it uses '=' as the
	     operator), but the expressions might not be simple attribute
	     references, and we mustn't try to establish equivalence classes
	     in that case. */
	  if (qo_is_equi_join_term (term))
	    {
	      qo_equivalence (head_seg, tail_seg);
	      QO_TERM_NOMINAL_SEG (term) = head_seg;
	    }
	}

      /* always true transitive equi-join term is not suitable as m-join edge.
       */
      if (PT_EXPR_INFO_IS_FLAGED (pt_expr, PT_EXPR_INFO_TRANSITIVE))
	{
	  merge_applies = 0;
	}

      if (merge_applies)
	{
	  QO_TERM_SET_FLAG (term, QO_TERM_MERGEABLE_EDGE);
	}

      /* Now make sure that the two (node) ends of the join get cached in the
         term structure. */
      QO_TERM_HEAD (term) = head_node;
      QO_TERM_TAIL (term) = tail_node;

      QO_ASSERT (env,
		 QO_NODE_IDX (QO_TERM_HEAD (term)) <
		 QO_NODE_IDX (QO_TERM_TAIL (term)));
    }

  if (n == 1)
    {
      QO_ASSERT (env, QO_TERM_CLASS (term) == QO_TC_SARG);
    }

  /* classify TC_JOIN term for outer join and determine its join type */
  if (QO_TERM_CLASS (term) == QO_TC_JOIN)
    {
      QO_ASSERT (env, QO_NODE_IDX (head_node) < QO_NODE_IDX (tail_node));

      /* inner join until proven otherwise */
      QO_TERM_JOIN_TYPE (term) = JOIN_INNER;

      /* check iff explicit join term */
      if (QO_ON_COND_TERM (term))
	{
	  QO_NODE *on_node;

	  on_node = QO_ENV_NODE (env, QO_TERM_LOCATION (term));
	  QO_ASSERT (env, on_node != NULL);
	  QO_ASSERT (env, QO_NODE_IDX (on_node) > 0);
	  QO_ASSERT (env, QO_NODE_LOCATION (on_node) > 0);

	  QO_ASSERT (env,
		     QO_TERM_LOCATION (term) == QO_NODE_LOCATION (on_node));

	  if (QO_NODE_IDX (tail_node) == QO_NODE_IDX (on_node))
	    {
	      QO_ASSERT (env,
			 QO_NODE_LOCATION (head_node) <
			 QO_NODE_LOCATION (tail_node));
	      QO_ASSERT (env, QO_NODE_LOCATION (tail_node) > 0);

	      if (QO_NODE_PT_JOIN_TYPE (on_node) == PT_JOIN_LEFT_OUTER)
		{
		  QO_TERM_JOIN_TYPE (term) = JOIN_LEFT;
		}
	      else if (QO_NODE_PT_JOIN_TYPE (on_node) == PT_JOIN_RIGHT_OUTER)
		{
		  QO_TERM_JOIN_TYPE (term) = JOIN_RIGHT;
		}
	      else if (QO_NODE_PT_JOIN_TYPE (on_node) == PT_JOIN_FULL_OUTER)
		{		/* not used */
		  QO_TERM_JOIN_TYPE (term) = JOIN_OUTER;
		}

	      /* record explicit join dependecy */
	      bitset_union (&(QO_NODE_OUTER_DEP_SET (on_node)),
			    &(QO_NODE_OUTER_DEP_SET (head_node)));
	      bitset_add (&(QO_NODE_OUTER_DEP_SET (on_node)),
			  QO_NODE_IDX (head_node));
	    }
	  else
	    {
	      /* is not valid explicit join term */
	      QO_TERM_CLASS (term) = QO_TC_OTHER;

	      QO_TERM_JOIN_TYPE (term) = NO_JOIN;

	      /* keep out from m-join edge */
	      QO_TERM_CLEAR_FLAG (term, QO_TERM_MERGEABLE_EDGE);
	    }
	}
      else
	{
	  QO_NODE *node;

	  for (i = 0; i < env->nnodes; i++)
	    {
	      node = QO_ENV_NODE (env, i);

	      if (QO_NODE_IDX (node) >= QO_NODE_IDX (head_node)
		  && QO_NODE_IDX (node) < QO_NODE_IDX (tail_node))
		{
		  if (QO_NODE_PT_JOIN_TYPE (node) == PT_JOIN_LEFT_OUTER
		      || QO_NODE_PT_JOIN_TYPE (node) == PT_JOIN_RIGHT_OUTER
		      || QO_NODE_PT_JOIN_TYPE (node) == PT_JOIN_FULL_OUTER)
		    {
		      /* record explicit join dependecy */
		      bitset_union (&(QO_NODE_OUTER_DEP_SET (tail_node)),
				    &(QO_NODE_OUTER_DEP_SET (node)));
		      bitset_add (&(QO_NODE_OUTER_DEP_SET (tail_node)),
				  QO_NODE_IDX (node));
		    }
		}
	    }
	}			/* else */
    }

wrapup:

  /* A negative selectivity means that the cardinality of the result depends
     only on the cardinality of the head, not on the product of the
     cardinalities of the head and the tail as in the usual case. */
  switch (term_type)
    {
    case PT_PATH_INNER:
      QO_TERM_JOIN_TYPE (term) = JOIN_INNER;
      if (QO_NODE_NCARD (QO_TERM_TAIL (term)) == 0)
	{
	  QO_TERM_SELECTIVITY (term) = 0.0;
	}
      else
	{
	  QO_TERM_SELECTIVITY (term) =
	    1.0 / QO_NODE_NCARD (QO_TERM_TAIL (term));
	}
      break;

    case PT_PATH_OUTER:
      {
	QO_TERM *t_term;
	QO_NODE *t_node;

	/* Traverse previously generated terms */
	for (t = 0; t < env->nterms - 1; t++)
	  {

	    t_term = QO_ENV_TERM (env, t);

	    if (QO_TERM_CLASS (t_term) == QO_TC_PATH
		&& QO_TERM_JOIN_TYPE (t_term) == JOIN_LEFT)
	      {
		if (head_node == NULL)
		  {
		    break;
		  }

		if ((QO_NODE_IDX (QO_TERM_HEAD (t_term)) ==
		     QO_NODE_IDX (head_node))
		    || (QO_NODE_IDX (QO_TERM_TAIL (t_term)) ==
			QO_NODE_IDX (head_node)))
		  {
		    /* found previously generated head_nodes's path-term
		     */

		    /* get tail node */
		    t_node = QO_TERM_TAIL (t_term);

		    /* apply ordered dependency to the tail node */
		    bitset_union (&(QO_NODE_OUTER_DEP_SET (tail_node)),
				  &(QO_NODE_OUTER_DEP_SET (t_node)));
		    bitset_add (&(QO_NODE_OUTER_DEP_SET (tail_node)),
				QO_NODE_IDX (t_node));
		  }
	      }
	  }
      }
      /* FALL THROUGH */
    case PT_PATH_OUTER_WEASEL:
      /* These can't be implemented with index scans regardless because an
         index scan won't properly implement the left-outer semantics of the
         path... */
      QO_TERM_JOIN_TYPE (term) = JOIN_LEFT;
      QO_TERM_SELECTIVITY (term) = -1.0;
      QO_TERM_CAN_USE_INDEX (term) = 0;
      break;

    case PREDICATE_TERM:
      QO_TERM_SELECTIVITY (term) = qo_expr_selectivity (env, pt_expr);
      break;

    default:
      QO_TERM_JOIN_TYPE (term) = JOIN_INNER;
      QO_TERM_SELECTIVITY (term) = -1.0;
      break;
    }				/* switch (term_type) */

  bitset_delset (&lhs_segs);
  bitset_delset (&rhs_segs);
  bitset_delset (&lhs_nodes);
  bitset_delset (&rhs_nodes);
}

/*
 * qo_expr_segs () -  Returns a bitset encoding all of the join graph segments
 *		      used in the pt_expr
 *   return: BITSET
 *   env(in):
 *   pt_expr(in): pointer to a conjunct
 *   result(out): BITSET of join segments (OUTPUT PARAMETER)
 */
void
qo_expr_segs (QO_ENV * env, PT_NODE * pt_expr, BITSET * result)
{
  PT_NODE *next;

  if (pt_expr == NULL)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_FAILED_ASSERTION, 1,
	      "pt_expr != NULL");
      return;
    }

  /* remember the next link and then break it */
  next = pt_expr->next;
  pt_expr->next = NULL;

  /* use env to get the bitset to the walk functions */
  QO_ENV_TMP_BITSET (env) = result;

  (void) parser_walk_tree (env->parser, pt_expr, set_seg_expr, &env,
			   pt_continue_walk, NULL);

  /* recover the next link */
  pt_expr->next = next;

  /* reset the temp pointer so we don't have a dangler */
  QO_ENV_TMP_BITSET (env) = NULL;
}

/*
 * set_seg_expr () -
 *   return: PT_NODE *
 *   parser(in): parser environment
 *   tree(in): tree to walk
 *   arg(in):
 *   continue_walk(in):
 *
 * Note: This walk "pre" function will set a bit in the bitset for
 *      each segment associated with the PT_NAME node.
 */
static PT_NODE *
set_seg_expr (PARSER_CONTEXT * parser, PT_NODE * tree, void *arg,
	      int *continue_walk)
{
  QO_ENV *env = (QO_ENV *) * (long *) arg;

  *continue_walk = PT_CONTINUE_WALK;

  /*
   * Make sure we check all subqueries for embedded references.  This
   * stuff really ought to all be done in one pass.
   */
  switch (tree->node_type)
    {
    case PT_SPEC:
      (void) parser_walk_tree (parser, tree->info.spec.derived_table,
			       set_seg_expr, arg, pt_continue_walk, NULL);
      *continue_walk = PT_LIST_WALK;
      break;

    case PT_NAME:
      (void) set_seg_node (tree, env, QO_ENV_TMP_BITSET (env));
      *continue_walk = PT_LIST_WALK;
      break;

    case PT_DOT_:
      (void) set_seg_node (tree->info.dot.arg2, env, QO_ENV_TMP_BITSET (env));
      *continue_walk = PT_LIST_WALK;
      break;

    case PT_EXPR:
      if (tree->info.expr.op == PT_SYS_CONNECT_BY_PATH
	  || tree->info.expr.op == PT_CONNECT_BY_ROOT
	  || tree->info.expr.op == PT_PRIOR)
	{
	  *continue_walk = PT_STOP_WALK;
	}
      if (pt_is_function_index_expr (parser, tree, false))
	{
	  int count_bits = bitset_cardinality (QO_ENV_TMP_BITSET (env));
	  (void) set_seg_node (tree, env, QO_ENV_TMP_BITSET (env));
	  if (bitset_cardinality (QO_ENV_TMP_BITSET (env)) - count_bits > 0)
	    {
	      *continue_walk = PT_STOP_WALK;
	    }
	}

      break;

    default:
      break;
    }

  return tree;			/* don't alter tree structure */
}

/*
 * set_seg_node () -
 *   return: nothing
 *   attr(in): attribute to set the seg for
 *   env(in): optimizer environment
 *   bitset(in): bitset in which to set the bit for the segment
 */
static void
set_seg_node (PT_NODE * attr, QO_ENV * env, BITSET * bitset)
{
  QO_NODE *node;
  QO_SEGMENT *seg;
  PT_NODE *entity;

  node = lookup_node (attr, env, &entity);

  /* node will be null if this attr resolves to an enclosing scope */
  if (node != NULL && (seg = lookup_seg (node, attr, env)) != NULL)
    {
      /*
       * lookup_seg() really shouldn't ever fail here, but it used to
       * for shared variables, and it doesn't really hurt anyone just
       * to ignore failures here.
       */
      bitset_add (bitset, QO_SEG_IDX (seg));
    }

}

/*
 * expr_is_mergable () - Test if the pt_expr is an equi-join conjunct
 *   return: bool
 *   pt_expr(in):
 */
static bool
expr_is_mergable (PT_NODE * pt_expr)
{
  if (pt_expr->or_next == NULL)
    {				/* keep out OR conjunct */
      if (!pt_is_query (pt_expr->info.expr.arg1)
	  && !pt_is_query (pt_expr->info.expr.arg2))
	{
	  if (pt_expr->info.expr.op == PT_EQ)
	    {
	      return true;
	    }
	  else if (pt_expr->info.expr.op == PT_RANGE)
	    {
	      PT_NODE *between_and;

	      between_and = pt_expr->info.expr.arg2;
	      if (between_and->or_next == NULL	/* has only one range */
		  && between_and->info.expr.op == PT_BETWEEN_EQ_NA)
		{
		  return true;
		}
	    }
	}
    }

  return false;
}

/*
 * qo_is_equi_join_term () - Test if the term is an equi-join conjunct whose
 *			  left and right sides are simple attribute references
 *   return: bool
 *   term(in):
 */
static bool
qo_is_equi_join_term (QO_TERM * term)
{
  PT_NODE *pt_expr;
  PT_NODE *rhs_expr;

  if (QO_TERM_IS_FLAGED (term, QO_TERM_SINGLE_PRED)
      && QO_TERM_IS_FLAGED (term, QO_TERM_EQUAL_OP))
    {
      if (QO_TERM_IS_FLAGED (term, QO_TERM_RANGELIST))
	{
	  /* keep out OR conjunct */
	  return false;
	}

      pt_expr = QO_TERM_PT_EXPR (term);
      if (pt_is_attr (pt_expr->info.expr.arg1))
	{
	  rhs_expr = pt_expr->info.expr.arg2;
	  if (pt_expr->info.expr.op == PT_RANGE)
	    {
	      assert (rhs_expr->info.expr.op == PT_BETWEEN_EQ_NA);
	      assert (rhs_expr->or_next == NULL);
	      /* has only one range */
	      rhs_expr = rhs_expr->info.expr.arg1;
	    }

	  return pt_is_attr (rhs_expr);
	}
    }

  return false;
}

/*
 * is_dependent_table () - Returns true iff the tree represents a dependent
 *			   derived table for this query
 *   return: bool
 *   entity(in): entity spec for a from list entry
 */
static bool
is_dependent_table (PT_NODE * entity)
{
  if (entity->info.spec.derived_table)
    {
      /* this test is too pessimistic.  The argument must depend
       * on a previous entity spec in the from list.
       * >>>> fix me some day <<<<
       */
      if (entity->info.spec.derived_table_type == PT_IS_SET_EXPR ||	/* is cselect derived table of method */
	  entity->info.spec.derived_table_type == PT_IS_CSELECT
	  || entity->info.spec.derived_table->info.query.correlation_level ==
	  1)
	{
	  return true;
	}
    }

  return false;
}

/*
 * get_term_subqueries () - walks the expression to see whether it contains any
 *			    correlated subqueries.  If so, it records the
 *			    identity of the containing term in the subquery
 *			    structure
 *   return:
 *   env(in): optimizer environment
 *   term(in):
 */
static void
get_term_subqueries (QO_ENV * env, QO_TERM * term)
{
  PT_NODE *pt_expr, *next;
  WALK_INFO info;

  if (QO_IS_FAKE_TERM (term))
    {
      /*
       * This is a pseudo-term introduced to keep track of derived
       * table dependencies.  If the dependent derived table is based
       * on a subquery, we need to find that subquery and record it in
       * the pseudo-term.
       */
      pt_expr = QO_NODE_ENTITY_SPEC (QO_TERM_TAIL (term));
    }
  else
    {
      /*
       * This is a normal term, and we need to find all of the
       * correlated subqueries contained within it.
       */
      pt_expr = QO_TERM_PT_EXPR (term);
    }

  /*
   * This should only happen for dependent derived tables, either those
   * based on a set or when checking out QO_TC_DEP_JOIN terms
   * introduced for ddt's that depend on more than one thing.
   *
   */
  if (pt_expr == NULL)
    {
      return;
    }

  next = pt_expr->next;
  pt_expr->next = NULL;

  info.env = env;
  info.term = term;

  (void) parser_walk_tree (QO_ENV_PARSER (env), pt_expr, check_subquery_pre,
			   &info, pt_continue_walk, NULL);

  pt_expr->next = next;

}

/*
 * get_opcode_rank () -
 *   return:
 *   opcode(in):
 */
static int
get_opcode_rank (PT_OP_TYPE opcode)
{
  switch (opcode)
    {
      /* Group 1 -- light */
    case PT_COERCIBILITY:
      /* is always folded to constant : should not reach this code */
      assert (false);
    case PT_CHARSET:
    case PT_COLLATION:
    case PT_AND:
    case PT_OR:
    case PT_XOR:
    case PT_NOT:
    case PT_ASSIGN:

    case PT_IS_IN:
    case PT_IS_NOT_IN:
    case PT_BETWEEN:
    case PT_NOT_BETWEEN:

    case PT_EQ:
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

    case PT_NE:
    case PT_GE:
    case PT_GT:
    case PT_LT:
    case PT_LE:

    case PT_GT_INF:
    case PT_LT_INF:

    case PT_BIT_NOT:
    case PT_BIT_AND:
    case PT_BIT_OR:
    case PT_BIT_XOR:
    case PT_BITSHIFT_LEFT:
    case PT_BITSHIFT_RIGHT:
    case PT_DIV:
    case PT_MOD:

    case PT_NULLSAFE_EQ:

    case PT_PLUS:
    case PT_MINUS:
    case PT_TIMES:
    case PT_DIVIDE:
    case PT_UNARY_MINUS:

    case PT_EXISTS:

    case PT_BETWEEN_AND:
    case PT_BETWEEN_GE_LE:
    case PT_BETWEEN_GE_LT:
    case PT_BETWEEN_GT_LE:
    case PT_BETWEEN_GT_LT:

    case PT_BETWEEN_EQ_NA:
    case PT_BETWEEN_INF_LE:
    case PT_BETWEEN_INF_LT:
    case PT_BETWEEN_GE_INF:
    case PT_BETWEEN_GT_INF:

    case PT_RANGE:

    case PT_SYS_DATE:
    case PT_SYS_TIME:
    case PT_SYS_TIMESTAMP:
    case PT_SYS_DATETIME:
    case PT_UTC_TIME:
    case PT_UTC_DATE:

    case PT_CURRENT_USER:
    case PT_LOCAL_TRANSACTION_ID:
    case PT_CURRENT_VALUE:
    case PT_NEXT_VALUE:

    case PT_INST_NUM:
    case PT_ROWNUM:
    case PT_ORDERBY_NUM:

    case PT_MODULUS:
    case PT_RAND:
    case PT_DRAND:
    case PT_RANDOM:
    case PT_DRANDOM:

    case PT_FLOOR:
    case PT_CEIL:
    case PT_SIGN:
    case PT_POWER:
    case PT_ROUND:
    case PT_LOG:
    case PT_EXP:
    case PT_SQRT:
    case PT_ABS:
    case PT_CHR:

    case PT_LEVEL:
    case PT_CONNECT_BY_ISLEAF:
    case PT_CONNECT_BY_ISCYCLE:

    case PT_IS_NULL:
    case PT_IS_NOT_NULL:
    case PT_IS:
    case PT_IS_NOT:

    case PT_ACOS:
    case PT_ASIN:
    case PT_ATAN:
    case PT_ATAN2:
    case PT_SIN:
    case PT_COS:
    case PT_TAN:
    case PT_COT:
    case PT_DEGREES:
    case PT_RADIANS:
    case PT_PI:
    case PT_LN:
    case PT_LOG2:
    case PT_LOG10:

    case PT_DATEF:
    case PT_TIMEF:
    case PT_TIME_FORMAT:
    case PT_TIMESTAMP:
    case PT_YEARF:
    case PT_MONTHF:
    case PT_DAYF:
    case PT_DAYOFMONTH:
    case PT_HOURF:
    case PT_MINUTEF:
    case PT_SECONDF:
    case PT_UNIX_TIMESTAMP:
    case PT_FROM_UNIXTIME:
    case PT_QUARTERF:
    case PT_WEEKDAY:
    case PT_DAYOFWEEK:
    case PT_DAYOFYEAR:
    case PT_TODAYS:
    case PT_FROMDAYS:
    case PT_TIMETOSEC:
    case PT_SECTOTIME:
    case PT_MAKEDATE:
    case PT_MAKETIME:
    case PT_ADDTIME:
    case PT_WEEKF:

    case PT_SCHEMA:
    case PT_DATABASE:
    case PT_VERSION:
    case PT_USER:
    case PT_ROW_COUNT:
    case PT_LAST_INSERT_ID:
    case PT_DEFAULTF:
    case PT_LIST_DBS:
    case PT_OID_OF_DUPLICATE_KEY:
    case PT_TYPEOF:
    case PT_EVALUATE_VARIABLE:
    case PT_DEFINE_VARIABLE:
    case PT_BIN:
    case PT_INET_ATON:
    case PT_INET_NTOA:
      return RANK_EXPR_LIGHT;

      /* Group 2 -- medium */
    case PT_REPEAT:
    case PT_SPACE:
    case PT_SETEQ:
    case PT_SETNEQ:
    case PT_SUPERSETEQ:
    case PT_SUPERSET:
    case PT_SUBSET:
    case PT_SUBSETEQ:

    case PT_POSITION:
    case PT_FINDINSET:
    case PT_SUBSTRING:
    case PT_SUBSTRING_INDEX:
    case PT_OCTET_LENGTH:
    case PT_BIT_LENGTH:

    case PT_CHAR_LENGTH:
    case PT_LOWER:
    case PT_UPPER:
    case PT_HEX:
    case PT_ASCII:
    case PT_CONV:
    case PT_TRIM:

    case PT_LIKE_LOWER_BOUND:
    case PT_LIKE_UPPER_BOUND:
    case PT_LTRIM:
    case PT_RTRIM:
    case PT_LPAD:
    case PT_RPAD:
    case PT_REPLACE:
    case PT_TRANSLATE:

    case PT_STRCAT:
    case PT_TO_CHAR:
    case PT_TO_DATE:
    case PT_TO_NUMBER:
    case PT_TO_TIME:
    case PT_TO_TIMESTAMP:
    case PT_TO_DATETIME:

    case PT_TRUNC:
    case PT_INSTR:
    case PT_LEAST:
    case PT_GREATEST:
    case PT_ADD_MONTHS:
    case PT_LAST_DAY:
    case PT_MONTHS_BETWEEN:

    case PT_CASE:
    case PT_NULLIF:
    case PT_COALESCE:
    case PT_NVL:
    case PT_NVL2:
    case PT_DECODE:

    case PT_EXTRACT:
    case PT_LIKE_ESCAPE:
    case PT_CAST:

    case PT_PATH_EXPR_SET:

    case PT_IF:
    case PT_IFNULL:
    case PT_ISNULL:

    case PT_CONCAT:
    case PT_CONCAT_WS:
    case PT_FIELD:
    case PT_LEFT:
    case PT_RIGHT:
    case PT_LOCATE:
    case PT_MID:
    case PT_STRCMP:
    case PT_REVERSE:

    case PT_BIT_COUNT:
    case PT_ADDDATE:
    case PT_DATE_ADD:
    case PT_SUBDATE:
    case PT_DATE_SUB:
    case PT_FORMAT:
    case PT_DATE_FORMAT:
    case PT_STR_TO_DATE:
    case PT_DATEDIFF:
    case PT_TIMEDIFF:
    case PT_TO_ENUMERATION_VALUE:
    case PT_INDEX_PREFIX:
      return RANK_EXPR_MEDIUM;

      /* Group 3 -- heavy */
    case PT_LIKE:
    case PT_NOT_LIKE:
    case PT_RLIKE:
    case PT_NOT_RLIKE:
    case PT_RLIKE_BINARY:
    case PT_NOT_RLIKE_BINARY:

    case PT_MD5:
    case PT_AES_ENCRYPT:
    case PT_AES_DECRYPT:
    case PT_SHA_ONE:
    case PT_SHA_TWO:
    case PT_ENCRYPT:
    case PT_DECRYPT:
    case PT_INDEX_CARDINALITY:
    case PT_TO_BASE64:
    case PT_FROM_BASE64:
    case PT_SYS_GUID:
    case PT_SLEEP:

      return RANK_EXPR_HEAVY;
      /* special case operator */
    case PT_FUNCTION_HOLDER:
      /* should be solved at PT_EXPR */
      assert (false);
      return RANK_EXPR_MEDIUM;
      break;
    default:
      return RANK_EXPR_MEDIUM;
    }
}

/*
 * get_expr_fcode_rank () -
 *   return:
 *   fcode(in): function code
 *   Only the functions embedded in an expression (with PT_FUNCTION_HOLDER)
 *   should be added here.
 */
static int
get_expr_fcode_rank (FUNC_TYPE fcode)
{
  switch (fcode)
    {
    case F_ELT:
      return RANK_EXPR_LIGHT;
    case F_INSERT_SUBSTRING:
      return RANK_EXPR_MEDIUM;
    default:
      /* each function must fill its rank */
      assert (false);
      return RANK_EXPR_FUNCTION;
    }
}

/*
 * get_operand_rank () -
 *   return:
 *   node(in):
 */
static int
get_operand_rank (PT_NODE * node)
{
  int rank = RANK_DEFAULT;

  if (node)
    {
      switch (node->node_type)
	{
	case PT_NAME:
	  rank = RANK_NAME;
	  break;

	case PT_VALUE:
	  rank = RANK_VALUE;
	  break;

	case PT_EXPR:
	  if (node->info.expr.op == PT_FUNCTION_HOLDER)
	    {
	      PT_NODE *function = node->info.expr.arg1;

	      assert (function != NULL);
	      if (function == NULL)
		{
		  rank = RANK_EXPR_MEDIUM;
		}
	      else
		{
		  rank =
		    get_expr_fcode_rank (function->info.
					 function.function_type);
		}
	    }
	  else
	    {
	      rank = get_opcode_rank (node->info.expr.op);
	    }
	  break;

	case PT_FUNCTION:
	  rank = RANK_EXPR_FUNCTION;
	  break;

	default:
	  break;
	}
    }

  return rank;
}

/*
 * get_term_rank () - walks the expression to see whether it contains any
 *		      rankable things. If so, it records the rank of the
 *		      containing term
 *   return:
 *   env(in): optimizer environment
 *   term(in): term to get
 */
static void
get_term_rank (QO_ENV * env, QO_TERM * term)
{
  PT_NODE *pt_expr;

  QO_TERM_RANK (term) =
    bitset_cardinality (&(QO_TERM_SUBQUERIES (term))) * RANK_QUERY;

  if (QO_IS_FAKE_TERM (term))
    {
      pt_expr = NULL;		/* do nothing */
    }
  else
    {
      pt_expr = QO_TERM_PT_EXPR (term);
    }

  if (pt_expr == NULL)
    {
      return;
    }

  /* At here, do not traverse OR list */
  switch (pt_expr->node_type)
    {
    case PT_EXPR:
      QO_TERM_RANK (term) += get_opcode_rank (pt_expr->info.expr.op);
      if (pt_expr->info.expr.arg1)
	QO_TERM_RANK (term) += get_operand_rank (pt_expr->info.expr.arg1);
      if (pt_expr->info.expr.arg2)
	QO_TERM_RANK (term) += get_operand_rank (pt_expr->info.expr.arg2);
      if (pt_expr->info.expr.arg3)
	QO_TERM_RANK (term) += get_operand_rank (pt_expr->info.expr.arg3);
      break;

    default:
      break;
    }
}

/*
 * check_subquery_pre () - Pre routine to add to some bitset all correlated
 *			   subqueries found in an expression
 *   return: PT_NODE *
 *   parser(in): parser environmnet
 *   node(in): node to check
 *   arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
check_subquery_pre (PARSER_CONTEXT * parser, PT_NODE * node, void *arg,
		    int *continue_walk)
{
  WALK_INFO *info = (WALK_INFO *) arg;

  /*
   * Be sure to reenable walking for list tails.
   */
  *continue_walk = PT_CONTINUE_WALK;

  if (node->node_type == PT_SELECT
      || node->node_type == PT_UNION
      || node->node_type == PT_DIFFERENCE
      || node->node_type == PT_INTERSECTION)
    {
      *continue_walk = PT_LIST_WALK;	/* NEVER need to look inside queries */

      if (node->info.query.correlation_level == 1)
	{
	  /*
	   * Find out the index of this subquery, and record that index
	   * in the enclosing term's subquery bitset.  This is lame,
	   * but I can't think of a better way to do it.  When we
	   * originally grabbed all of the subqueries we had no idea
	   * what expression they were in, so we have to discover it
	   * after the fact.  Oh well, this doesn't happen often
	   * anyway.
	   */
	  int i, N;
	  QO_ENV *env;

	  env = info->env;
	  for (i = 0, N = env->nsubqueries; i < N; i++)
	    {
	      if (node == env->subqueries[i].node)
		{
		  bitset_add (&(env->subqueries[i].terms),
			      QO_TERM_IDX (info->term));
		  bitset_add (&(QO_TERM_SUBQUERIES (info->term)), i);
		  break;
		}
	    }
	}
    }

  return node;			/* leave node unchanged */

}

/*
 * is_local_name () -
 *   return: 1 iff the expression is a name correlated to the current query
 *   env(in): Optimizer environment
 *   expr(in): The parse tree for the expression to examine
 */
static bool
is_local_name (QO_ENV * env, PT_NODE * expr)
{
  UINTPTR spec = 0;

  if (expr == NULL)
    {
      return false;
    }
  else if (expr->node_type == PT_NAME)
    {
      spec = expr->info.name.spec_id;
    }
  else if (expr->node_type == PT_DOT_)
    {
      spec = expr->info.dot.arg2->info.name.spec_id;
    }
  else if (expr->node_type == PT_EXPR && expr->info.expr.op == PT_PRIOR)
    {
      return is_local_name (env, expr->info.expr.arg1);
    }
  else if (pt_is_function_index_expr (env->parser, expr, false))
    {
      if (expr->info.expr.op == PT_FUNCTION_HOLDER)
	{
	  PT_NODE *arg = NULL;
	  PT_NODE *func = expr->info.expr.arg1;
	  for (arg = func->info.function.arg_list; arg != NULL;
	       arg = arg->next)
	    {
	      PT_NODE *save_arg = arg;
	      arg = pt_function_index_skip_expr (arg);
	      if (arg->node_type == PT_NAME)
		{
		  if (is_local_name (env, arg) == false)
		    {
		      return false;
		    }
		}
	      arg = save_arg;
	    }
	}
      else
	{
	  PT_NODE *arg = NULL;
	  if (expr->info.expr.arg1)
	    {
	      arg = pt_function_index_skip_expr (expr->info.expr.arg1);
	      if (arg->node_type == PT_NAME)
		{
		  if (is_local_name (env, arg) == false)
		    {
		      return false;
		    }
		}
	    }
	  if (expr->info.expr.arg2)
	    {
	      arg = pt_function_index_skip_expr (expr->info.expr.arg2);
	      if (arg->node_type == PT_NAME)
		{
		  if (is_local_name (env, arg) == false)
		    {
		      return false;
		    }
		}
	    }
	  if (expr->info.expr.arg3)
	    {
	      arg = pt_function_index_skip_expr (expr->info.expr.arg3);
	      if (arg->node_type == PT_NAME)
		{
		  if (is_local_name (env, arg) == false)
		    {
		      return false;
		    }
		}
	    }
	}
      return true;
    }
  else
    {
      return false;
    }

  return (pt_find_entity (env->parser,
			  env->pt_tree->info.query.q.select.from,
			  spec) != NULL) ? true : false;
}

/*
 * pt_is_pseudo_const () -
 *   return: true if the expression can serve as a pseudo-constant
 *	     during predicate evaluation.  Used primarily to help
 *	     determine whether a predicate can be implemented
 *	     with an index scan
 *   env(in): The optimizer environment
 *   expr(in): The parse tree for the expression to examine
 */
bool
pt_is_pseudo_const (PT_NODE * expr)
{
  if (expr == NULL)
    {
      return false;
    }

  switch (expr->node_type)
    {
    case PT_VALUE:
    case PT_HOST_VAR:
      return true;

    case PT_SELECT:
    case PT_UNION:
    case PT_DIFFERENCE:
    case PT_INTERSECTION:
      return (expr->info.query.correlation_level != 1) ? true : false;

    case PT_NAME:
      /*
       * It is up to the calling context to ensure that the name is
       * actually a pseudo constant, either because it is a correlated
       * outer reference, or because it can otherwise be guaranteed to
       * be evaluated by the time it is referenced.
       */
      return true;

    case PT_DOT_:
      /*
       * It would be nice if we could use expressions that are
       * guaranteed to be independent of the attribute, but the current
       * XASL implementation can't guarantee that such expressions have
       * been evaluated by the time that we need them, so we have to
       * play it safe here and not use them.
       */
      return true;

    case PT_EXPR:
      switch (expr->info.expr.op)
	{
	case PT_FUNCTION_HOLDER:
	  return pt_is_pseudo_const (expr->info.expr.arg1);
	case PT_PLUS:
	case PT_STRCAT:
	case PT_MINUS:
	case PT_TIMES:
	case PT_DIVIDE:
	case PT_BIT_AND:
	case PT_BIT_OR:
	case PT_BIT_XOR:
	case PT_BITSHIFT_LEFT:
	case PT_BITSHIFT_RIGHT:
	case PT_DIV:
	case PT_MOD:
	case PT_LEFT:
	case PT_RIGHT:
	case PT_REPEAT:
	case PT_STRCMP:
	  return (pt_is_pseudo_const (expr->info.expr.arg1)
		  && pt_is_pseudo_const (expr->info.
					 expr.arg2)) ? true : false;
	case PT_UNARY_MINUS:
	case PT_BIT_NOT:
	case PT_BIT_COUNT:
	case PT_QPRIOR:
	case PT_PRIOR:
	  return pt_is_pseudo_const (expr->info.expr.arg1);
	case PT_BETWEEN_AND:
	case PT_BETWEEN_GE_LE:
	case PT_BETWEEN_GE_LT:
	case PT_BETWEEN_GT_LE:
	case PT_BETWEEN_GT_LT:
	  return (pt_is_pseudo_const (expr->info.expr.arg1)
		  && pt_is_pseudo_const (expr->info.
					 expr.arg2)) ? true : false;
	case PT_BETWEEN_EQ_NA:
	case PT_BETWEEN_INF_LE:
	case PT_BETWEEN_INF_LT:
	case PT_BETWEEN_GE_INF:
	case PT_BETWEEN_GT_INF:
	  return pt_is_pseudo_const (expr->info.expr.arg1);
	case PT_MODULUS:
	  return (pt_is_pseudo_const (expr->info.expr.arg1)
		  && pt_is_pseudo_const (expr->info.
					 expr.arg2)) ? true : false;
	case PT_SCHEMA:
	case PT_DATABASE:
	case PT_VERSION:
	case PT_PI:
	case PT_USER:
	case PT_LAST_INSERT_ID:
	case PT_ROW_COUNT:
	case PT_DEFAULTF:
	case PT_LIST_DBS:
	case PT_OID_OF_DUPLICATE_KEY:
	  return true;
	case PT_FLOOR:
	case PT_CEIL:
	case PT_SIGN:
	case PT_ABS:
	case PT_CHR:
	case PT_EXP:
	case PT_SQRT:
	case PT_ACOS:
	case PT_ASIN:
	case PT_ATAN:
	case PT_SIN:
	case PT_COS:
	case PT_TAN:
	case PT_COT:
	case PT_DEGREES:
	case PT_RADIANS:
	case PT_LN:
	case PT_LOG2:
	case PT_LOG10:
	case PT_DATEF:
	case PT_TIMEF:
	  return pt_is_pseudo_const (expr->info.expr.arg1);
	case PT_POWER:
	case PT_ROUND:
	case PT_TRUNC:
	case PT_LOG:
	  return (pt_is_pseudo_const (expr->info.expr.arg1)
		  && pt_is_pseudo_const (expr->info.
					 expr.arg2)) ? true : false;
	case PT_INSTR:
	case PT_CONV:
	  return (pt_is_pseudo_const (expr->info.expr.arg1)
		  && pt_is_pseudo_const (expr->info.expr.arg2)
		  && pt_is_pseudo_const (expr->info.
					 expr.arg3)) ? true : false;
	case PT_POSITION:
	case PT_FINDINSET:
	  return (pt_is_pseudo_const (expr->info.expr.arg1)
		  && pt_is_pseudo_const (expr->info.
					 expr.arg2)) ? true : false;
	case PT_SUBSTRING:
	case PT_SUBSTRING_INDEX:
	case PT_LOCATE:
	  return (pt_is_pseudo_const (expr->info.expr.arg1)
		  && pt_is_pseudo_const (expr->info.expr.arg2)
		  && (expr->info.expr.arg3 ?
		      pt_is_pseudo_const (expr->info.
					  expr.arg3) : true)) ? true : false;
	case PT_CHAR_LENGTH:
	case PT_OCTET_LENGTH:
	case PT_BIT_LENGTH:
	case PT_LOWER:
	case PT_UPPER:
	case PT_HEX:
	case PT_ASCII:
	case PT_REVERSE:
	case PT_SPACE:
	case PT_MD5:
	case PT_SHA_ONE:
	case PT_TO_BASE64:
	case PT_FROM_BASE64:
	case PT_BIN:
	  return pt_is_pseudo_const (expr->info.expr.arg1);
	case PT_TRIM:
	case PT_LTRIM:
	case PT_RTRIM:
	case PT_LIKE_LOWER_BOUND:
	case PT_LIKE_UPPER_BOUND:
	case PT_FROM_UNIXTIME:
	case PT_AES_ENCRYPT:
	case PT_AES_DECRYPT:
	case PT_SHA_TWO:
	  return (pt_is_pseudo_const (expr->info.expr.arg1)
		  && (expr->info.expr.arg2 ?
		      pt_is_pseudo_const (expr->info.
					  expr.arg2) : true)) ? true : false;

	case PT_LPAD:
	case PT_RPAD:
	case PT_REPLACE:
	case PT_TRANSLATE:
	case PT_INDEX_PREFIX:
	  return (pt_is_pseudo_const (expr->info.expr.arg1)
		  && pt_is_pseudo_const (expr->info.expr.arg2)
		  && (expr->info.expr.arg3 ?
		      pt_is_pseudo_const (expr->info.
					  expr.arg3) : true)) ? true : false;
	case PT_ADD_MONTHS:
	  return (pt_is_pseudo_const (expr->info.expr.arg1)
		  && pt_is_pseudo_const (expr->info.
					 expr.arg2)) ? true : false;
	case PT_LAST_DAY:
	case PT_UNIX_TIMESTAMP:
	  if (expr->info.expr.arg1)
	    {
	      return pt_is_pseudo_const (expr->info.expr.arg1);
	    }
	  else
	    {
	      return true;
	    }
	case PT_YEARF:
	case PT_MONTHF:
	case PT_DAYF:
	case PT_DAYOFMONTH:
	case PT_HOURF:
	case PT_MINUTEF:
	case PT_SECONDF:
	case PT_QUARTERF:
	case PT_WEEKDAY:
	case PT_DAYOFWEEK:
	case PT_DAYOFYEAR:
	case PT_TODAYS:
	case PT_FROMDAYS:
	case PT_TIMETOSEC:
	case PT_SECTOTIME:
	case PT_TO_ENUMERATION_VALUE:
	  return pt_is_pseudo_const (expr->info.expr.arg1);
	case PT_TIMESTAMP:
	  return (pt_is_pseudo_const (expr->info.expr.arg1)
		  && pt_is_pseudo_const (expr->info.
					 expr.arg2)) ? true : false;
	case PT_MONTHS_BETWEEN:
	case PT_TIME_FORMAT:
	case PT_FORMAT:
	case PT_ATAN2:
	case PT_ADDDATE:
	case PT_DATE_ADD:	/* 2 args because the 3rd is constant (unit) */
	case PT_SUBDATE:
	case PT_DATE_SUB:
	case PT_DATE_FORMAT:
	case PT_STR_TO_DATE:
	case PT_DATEDIFF:
	case PT_TIMEDIFF:
	case PT_MAKEDATE:
	case PT_ADDTIME:
	case PT_WEEKF:
	  return (pt_is_pseudo_const (expr->info.expr.arg1)
		  && pt_is_pseudo_const (expr->info.
					 expr.arg2)) ? true : false;
	case PT_SYS_DATE:
	case PT_SYS_TIME:
	case PT_SYS_TIMESTAMP:
	case PT_SYS_DATETIME:
	case PT_UTC_TIME:
	case PT_UTC_DATE:
	case PT_LOCAL_TRANSACTION_ID:
	case PT_CURRENT_USER:
	case PT_EVALUATE_VARIABLE:
	  return true;
	case PT_TO_CHAR:
	case PT_TO_DATE:
	case PT_TO_TIME:
	case PT_TO_TIMESTAMP:
	case PT_TO_DATETIME:
	case PT_TO_NUMBER:
	  return (pt_is_pseudo_const (expr->info.expr.arg1)
		  && (expr->info.expr.arg2 ?
		      pt_is_pseudo_const (expr->info.
					  expr.arg2) : true)) ? true : false;
	case PT_CURRENT_VALUE:
	case PT_NEXT_VALUE:
	  return true;
	case PT_CAST:
	  return pt_is_pseudo_const (expr->info.expr.arg1);
	case PT_CASE:
	case PT_DECODE:
	  return (pt_is_pseudo_const (expr->info.expr.arg1)
		  && pt_is_pseudo_const (expr->info.
					 expr.arg2)) ? true : false;
	case PT_NULLIF:
	case PT_COALESCE:
	case PT_NVL:
	  return (pt_is_pseudo_const (expr->info.expr.arg1)
		  && pt_is_pseudo_const (expr->info.
					 expr.arg2)) ? true : false;
	case PT_IF:
	  return (pt_is_pseudo_const (expr->info.expr.arg2)
		  && pt_is_pseudo_const (expr->info.
					 expr.arg3)) ? true : false;
	case PT_IFNULL:
	  return (pt_is_pseudo_const (expr->info.expr.arg1)
		  && pt_is_pseudo_const (expr->info.
					 expr.arg2)) ? true : false;

	case PT_ISNULL:
	  return pt_is_pseudo_const (expr->info.expr.arg1);
	case PT_CONCAT:
	  return (pt_is_pseudo_const (expr->info.expr.arg1)
		  && (expr->info.expr.arg2 ?
		      pt_is_pseudo_const (expr->info.
					  expr.arg2) : true)) ? true : false;
	case PT_CONCAT_WS:
	case PT_FIELD:
	  return (pt_is_pseudo_const (expr->info.expr.arg1)
		  && (expr->info.expr.arg2 ?
		      pt_is_pseudo_const (expr->info.expr.arg2) : true)
		  && pt_is_pseudo_const (expr->info.
					 expr.arg3)) ? true : false;
	case PT_MID:
	case PT_NVL2:
	case PT_MAKETIME:
	  return (pt_is_pseudo_const (expr->info.expr.arg1)
		  && pt_is_pseudo_const (expr->info.expr.arg2)
		  && pt_is_pseudo_const (expr->info.
					 expr.arg3)) ? true : false;
	case PT_EXTRACT:
	  return pt_is_pseudo_const (expr->info.expr.arg1);
	case PT_LEAST:
	case PT_GREATEST:
	  return (pt_is_pseudo_const (expr->info.expr.arg1)
		  && pt_is_pseudo_const (expr->info.
					 expr.arg2)) ? true : false;
	case PT_COERCIBILITY:
	  /* is always folded to constant : should not reach this code */
	  assert (false);
	case PT_COLLATION:
	case PT_CHARSET:
	case PT_INET_ATON:
	case PT_INET_NTOA:
	  return pt_is_pseudo_const (expr->info.expr.arg1);
	default:
	  return false;
	}

    case PT_FUNCTION:
      {
	/*
	 * The is the case we encounter for predicates like
	 *
	 *      x in (a,b,c)
	 *
	 * Here the the expression '(a,b,c)' comes in as a multiset
	 * function call, with PT_NAMEs 'a', 'b', and 'c' as its arglist.
	 */
	PT_NODE *p;

	if (expr->info.function.function_type != F_SET
	    && expr->info.function.function_type != F_MULTISET
	    && expr->info.function.function_type != F_SEQUENCE)
	  {
	    return false;
	  }
	for (p = expr->info.function.arg_list; p; p = p->next)
	  {
	    if (!pt_is_pseudo_const (p))
	      {
		return false;
	      }
	  }
	return true;
      }

    default:
      return false;
    }
}

/*
 * add_local_subquery () - This routine adds an entry to the optimizer
 *			   environment for the subquery
 *   return: nothing
 *   env(in): Optimizer environment
 *   node(in): The parse tree for the subquery being added
 */
static void
add_local_subquery (QO_ENV * env, PT_NODE * node)
{
  int i, n;
  QO_SUBQUERY *tmp;

  n = env->nsubqueries++;

  /*
   * Be careful here: the previously allocated QO_SUBQUERY terms
   * contain bitsets that may have self-relative internal pointers, and
   * those pointers have to be maintained in the new array.  The proper
   * way to make sure that they are consistent is to use the bitset_assign()
   * macro, not just to do the bitcopy that memcpy() will do.
   */
  tmp = NULL;
  if ((n + 1) > 0)
    {
      tmp = (QO_SUBQUERY *) malloc (sizeof (QO_SUBQUERY) * (n + 1));
      if (tmp == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, sizeof (QO_SUBQUERY) * (n + 1));
	  return;
	}
    }
  else
    {
      return;
    }

  memcpy (tmp, env->subqueries, n * sizeof (QO_SUBQUERY));
  for (i = 0; i < n; i++)
    {
      QO_SUBQUERY *subq;
      subq = &env->subqueries[i];
      BITSET_MOVE (tmp[i].segs, subq->segs);
      BITSET_MOVE (tmp[i].nodes, subq->nodes);
      BITSET_MOVE (tmp[i].terms, subq->terms);
    }
  if (env->subqueries)
    {
      free_and_init (env->subqueries);
    }
  env->subqueries = tmp;

  tmp = &env->subqueries[n];
  tmp->node = node;
  bitset_init (&tmp->segs, env);
  bitset_init (&tmp->nodes, env);
  bitset_init (&tmp->terms, env);
  qo_expr_segs (env, node, &tmp->segs);
  qo_seg_nodes (env, &tmp->segs, &tmp->nodes);
  tmp->idx = n;
}


/*
 * get_local_subqueries_pre () - Builds vector of locally correlated
 *				 (level 1) queries
 *   return:
 *   parser(in):
 *   node(in):
 *   arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
get_local_subqueries_pre (PARSER_CONTEXT * parser, PT_NODE * node, void *arg,
			  int *continue_walk)
{
  QO_ENV *env;
  BITSET segs;

  *continue_walk = PT_CONTINUE_WALK;

  switch (node->node_type)
    {
    case PT_SELECT:
    case PT_UNION:
    case PT_DIFFERENCE:
    case PT_INTERSECTION:
      /* check for correlated subquery except for SELECT list */
      if (node->info.query.correlation_level == 1)
	{
	  env = (QO_ENV *) arg;
	  bitset_init (&segs, env);
	  qo_expr_segs (env, node, &segs);
	  if (bitset_is_empty (&segs))
	    {
	      /* reduce_equality_terms() can change a correlated subquery to
	       * uncorrelated one */
	      node->info.query.correlation_level = 0;
	    }
	  bitset_delset (&segs);
	}
      *continue_walk = PT_LIST_WALK;
      break;

    default:
      break;
    }

  return node;
}

/*
 * get_local_subqueries_post () - Builds vector of locally correlated
 *				  (level 1) queries
 *   return:
 *   parser(in):
 *   node(in):
 *   arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
get_local_subqueries_post (PARSER_CONTEXT * parser, PT_NODE * node, void *arg,
			   int *continue_walk)
{
  QO_ENV *env = (QO_ENV *) arg;

  *continue_walk = PT_CONTINUE_WALK;

  switch (node->node_type)
    {
    case PT_SELECT:
    case PT_UNION:
    case PT_DIFFERENCE:
    case PT_INTERSECTION:
      if (node->info.query.correlation_level == 1)
	{
	  add_local_subquery (env, node);
	}
      break;

    default:
      break;
    }

  return node;
}

/*
 * get_local_subqueries () -
 *   return: non-zero if something went wrong
 *   env(in):
 *   node(in):
 *
 * Note:
 *	Gather the correlated level == 1 subqueries.
 *	EXCLUDE nested queries.
 *	INCLUDING the node being passed in.
 */
static void
get_local_subqueries (QO_ENV * env, PT_NODE * node)
{
  PARSER_CONTEXT *parser;
  PT_NODE *tree;
  PT_NODE *select_list_ptr;
  PT_NODE *next_ptr;
  int i;

  parser = QO_ENV_PARSER (env);
  tree = QO_ENV_PT_TREE (env);

  next_ptr = tree->next;
  tree->next = NULL;
  select_list_ptr = tree->info.query.q.select.list;
  tree->info.query.q.select.list = NULL;

  parser_walk_leaves (parser,
		      tree,
		      get_local_subqueries_pre, env,
		      get_local_subqueries_post, env);

  /* restore next list pointer */
  tree->next = next_ptr;
  tree->info.query.q.select.list = select_list_ptr;

  /*
   * Now that all of the subqueries have been discovered, make
   * *another* pass and associate each with its enclosing QO_TERM, if
   * any.
   */
  for (i = 0; i < env->nterms; i++)
    {
      get_term_subqueries (env, QO_ENV_TERM (env, i));
    }

  QO_ASSERT (env, env->subqueries != NULL || env->nsubqueries == 0);

}


/*
 * get_rank () - Gather term's rank
 *   return:
 *   env(in):
 */
static void
get_rank (QO_ENV * env)
{
  int i;

  for (i = 0; i < env->nterms; i++)
    {
      get_term_rank (env, QO_ENV_TERM (env, i));
    }
}

/*
 * get_referenced_attrs () - Returns the list of this entity's attributes that
 *			     are referenced in this query
 *   return:
 *   entity(in):
 */
static PT_NODE *
get_referenced_attrs (PT_NODE * entity)
{
  return (entity->info.spec.derived_table
	  ? entity->info.spec.as_attr_list
	  : entity->info.spec.referenced_attrs);
}

/*
 * add_hint_args () - attach hint informations to QO_NODEs
 *   return:
 *   env(in):
 *   arg_list(in):
 *   hint(in):
 */
static void
add_hint_args (QO_ENV * env, PT_NODE * arg_list, PT_HINT_ENUM hint)
{
  PT_NODE *arg, *entity_spec;
  QO_NODE *node;
  int i;

  if (arg_list)
    {
      /* iterate over all nodes */
      for (i = 0; i < env->nnodes; i++)
	{
	  node = QO_ENV_NODE (env, i);
	  entity_spec = QO_NODE_ENTITY_SPEC (node);
	  /* check for spec list */
	  for (arg = arg_list; arg; arg = arg->next)
	    {
	      /* found match */
	      if (entity_spec->info.spec.id == arg->info.name.spec_id)
		{
		  QO_NODE_HINT (node) =
		    (PT_HINT_ENUM) (QO_NODE_HINT (node) | hint);
		  break;
		}
	    }
	}
    }
  else
    {				/* FULLY HINTED */
      /* iterate over all nodes */
      for (i = 0; i < env->nnodes; i++)
	{
	  node = QO_ENV_NODE (env, i);
	  QO_NODE_HINT (node) = (PT_HINT_ENUM) (QO_NODE_HINT (node) | hint);
	}
    }

}

/*
 * add_hint () -
 *   return:
 *   env(in):
 *   tree(in):
 */
static void
add_hint (QO_ENV * env, PT_NODE * tree)
{
  PT_HINT_ENUM hint;
  int i, j, k;
  QO_NODE *node, *p_node;
  PT_NODE *arg, *p_arg, *spec, *p_spec;
  int last_ordered_idx = 0;

  hint = tree->info.query.q.select.hint;

  if (hint & PT_HINT_ORDERED)
    {
      if (tree->info.query.q.select.ordered)
	{
	  /* find last ordered node */
	  for (arg = tree->info.query.q.select.ordered;
	       arg->next; arg = arg->next)
	    {
	      ;			/* nop */
	    }
	  for (i = 0; i < env->nnodes; i++)
	    {
	      node = QO_ENV_NODE (env, i);
	      spec = QO_NODE_ENTITY_SPEC (node);
	      if (spec->info.spec.id == arg->info.name.spec_id)
		{
		  last_ordered_idx = QO_NODE_IDX (node);
		  break;
		}
	    }

	  /* iterate over all nodes */
	  for (i = 0; i < env->nnodes; i++)
	    {
	      node = QO_ENV_NODE (env, i);
	      spec = QO_NODE_ENTITY_SPEC (node);
	      /* check for arg list */
	      p_arg = NULL;
	      for (arg = tree->info.query.q.select.ordered, j = 0;
		   arg; arg = arg->next, j++)
		{
		  if (spec->info.spec.id == arg->info.name.spec_id)
		    {
		      if (p_arg)
			{	/* skip out the first ordered spec */
			  /* find prev node */
			  for (k = 0; k < env->nnodes; k++)
			    {
			      p_node = QO_ENV_NODE (env, k);
			      p_spec = QO_NODE_ENTITY_SPEC (p_node);
			      if (p_spec->info.spec.id ==
				  p_arg->info.name.spec_id)
				{
				  bitset_assign (&
						 (QO_NODE_OUTER_DEP_SET
						  (node)),
						 &(QO_NODE_OUTER_DEP_SET
						   (p_node)));
				  bitset_add (&(QO_NODE_OUTER_DEP_SET (node)),
					      QO_NODE_IDX (p_node));
				  break;
				}
			    }
			}

#if 1				/* TEMPORARY CODE: DO NOT REMOVE ME !!! */
		      QO_NODE_HINT (node) =
			(PT_HINT_ENUM) (QO_NODE_HINT (node) |
					PT_HINT_ORDERED);
#endif
		      break;	/* exit loop for arg traverse */
		    }

		  p_arg = arg;	/* save previous arg */
		}

	      /* not found in arg list */
	      if (!arg)
		{
		  bitset_add (&(QO_NODE_OUTER_DEP_SET (node)),
			      last_ordered_idx);
		}

	    }			/* for (i = ... ) */

	}
      else
	{			/* FULLY HINTED */
	  /* iterate over all nodes */
	  p_node = NULL;
	  for (i = 0; i < env->nnodes; i++)
	    {
	      node = QO_ENV_NODE (env, i);
	      if (p_node)
		{		/* skip out the first ordered node */
		  bitset_assign (&(QO_NODE_OUTER_DEP_SET (node)),
				 &(QO_NODE_OUTER_DEP_SET (p_node)));
		  bitset_add (&(QO_NODE_OUTER_DEP_SET (node)),
			      QO_NODE_IDX (p_node));
		}
#if 1				/* TEMPORARY CODE: DO NOT REMOVE ME !!! */
	      QO_NODE_HINT (node) =
		(PT_HINT_ENUM) (QO_NODE_HINT (node) | PT_HINT_ORDERED);
#endif

	      p_node = node;	/* save previous node */
	    }			/* for (i = ... ) */
	}
    }

  if (hint & PT_HINT_USE_NL)
    {
      add_hint_args (env, tree->info.query.q.select.use_nl, PT_HINT_USE_NL);
    }

  if (hint & PT_HINT_USE_IDX)
    {
      add_hint_args (env, tree->info.query.q.select.use_idx, PT_HINT_USE_IDX);
    }
  if (hint & PT_HINT_INDEX_SS)
    {
      add_hint_args (env, tree->info.query.q.select.index_ss,
		     PT_HINT_INDEX_SS);
    }
  if (hint & PT_HINT_INDEX_LS)
    {
      add_hint_args (env, tree->info.query.q.select.index_ls,
		     PT_HINT_INDEX_LS);
    }


  if (hint & PT_HINT_USE_MERGE)
    {
      add_hint_args (env, tree->info.query.q.select.use_merge,
		     PT_HINT_USE_MERGE);
    }
}

/*
 * add_using_index () - attach index names specified in USING INDEX clause
 *			to QO_NODEs
 *   return:
 *   env(in):
 *   using_index(in):
 */
static void
add_using_index (QO_ENV * env, PT_NODE * using_index)
{
  int i, j, n;
  QO_NODE *nodep;
  QO_USING_INDEX *uip;
  PT_NODE *indexp, *indexp_nokl;
  bool is_none, is_ignored;
  PT_NODE **idx_ignore_list = NULL;
  int idx_ignore_list_capacity = 0;
  int idx_ignore_list_size = 0;

  if (!using_index)
    {
      /* no USING INDEX clause in the query;
         all QO_NODE_USING_INDEX(node) will contain NULL */
      goto cleanup;
    }

  /* allocate memory for index ignore list; by default, the capacity of the
     list is the number of index hints in the USING INDEX clause */
  idx_ignore_list_capacity = 0;
  for (indexp = using_index; indexp; indexp = indexp->next)
    {
      idx_ignore_list_capacity++;
    }

  idx_ignore_list = (PT_NODE **) malloc (idx_ignore_list_capacity *
					 sizeof (PT_NODE *));
  if (idx_ignore_list == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, idx_ignore_list_capacity * sizeof (PT_NODE *));
      goto cleanup;
    }

  /* if an index occurs more than once and at least one occurrence has a
     keylimit, we should ignore the occurrences without keylimit; we now
     build an ignore list containing indexes which should not be attached to
     the QO_NODE */
  idx_ignore_list_size = 0;
  for (indexp_nokl = using_index; indexp_nokl;
       indexp_nokl = indexp_nokl->next)
    {
      if (indexp_nokl->info.name.original
	  && indexp_nokl->info.name.resolved
	  && !indexp_nokl->info.name.indx_key_limit)
	{
	  /* it's a normal index without a keylimit; search for same index
	     with keylimit */
	  for (indexp = using_index; indexp; indexp = indexp->next)
	    {
	      if (indexp->info.name.original
		  && indexp->info.name.resolved
		  && indexp->info.name.indx_key_limit
		  && indexp->etc == indexp_nokl->etc
		  && !intl_identifier_casecmp (indexp->info.name.original,
					       indexp_nokl->info.name.
					       original)
		  && !intl_identifier_casecmp (indexp->info.name.resolved,
					       indexp_nokl->info.name.
					       resolved))
		{
		  /* same index found, with keylimit; add to ignore list */
		  idx_ignore_list[idx_ignore_list_size++] = indexp_nokl;
		  break;
		}
	    }			/* for (indexp ...) */
	}
    }				/* for (indexp_nokl ...) */

  /* for each node */
  for (i = 0; i < env->nnodes; i++)
    {
      nodep = QO_ENV_NODE (env, i);
      is_none = false;

      /* count number of indexes for this node */
      n = 0;
      for (indexp = using_index; indexp; indexp = indexp->next)
	{
	  /* check for USING INDEX NONE or USING INDEX class.NONE cases */
	  if (indexp->info.name.original == NULL
	      && indexp->info.name.resolved == NULL)
	    {
	      n = 0;
	      is_none = true;
	      break;		/* USING INDEX NONE case */
	    }
	  if (indexp->info.name.original == NULL
	      && !intl_identifier_casecmp (QO_NODE_NAME (nodep),
					   indexp->info.name.resolved)
	      && indexp->etc == (void *) PT_IDX_HINT_CLASS_NONE)
	    {
	      n = 0;		/* USING INDEX class_name.NONE,... case */
	      is_none = true;
	      break;
	    }

	  /* check if index is in ignore list (pointer comparison) */
	  is_ignored = false;
	  for (j = 0; j < idx_ignore_list_size; j++)
	    {
	      if (indexp == idx_ignore_list[j])
		{
		  is_ignored = true;
		  break;
		}
	    }
	  if (is_ignored)
	    {
	      continue;
	    }

	  /* check index type and count it accordingly */
	  if (indexp->info.name.original == NULL
	      && indexp->info.name.resolved[0] == '*')
	    {
	      n++;		/* USING INDEX ALL EXCEPT case */
	    }
	  if (indexp->info.name.original
	      && !intl_identifier_casecmp (QO_NODE_NAME (nodep),
					   indexp->info.name.resolved))
	    {
	      n++;
	    }
	}
      /* if n == 0, it means that either no indexes in USING INDEX clause for
         this node or USING INDEX NONE case */

      if (n == 0 && !is_none)
	{
	  /* no index for this node in USING INDEX clause, however give it
	     a chance to be assigned an index later */
	  QO_NODE_USING_INDEX (nodep) = NULL;
	  continue;
	}

      /* allocate QO_USING_INDEX structure */
      if (n == 0)
	{
	  uip = (QO_USING_INDEX *) malloc (SIZEOF_USING_INDEX (1));
	}
      else
	{
	  uip = (QO_USING_INDEX *) malloc (SIZEOF_USING_INDEX (n));
	}
      QO_NODE_USING_INDEX (nodep) = uip;

      if (uip == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, SIZEOF_USING_INDEX (n ? n : 1));
	  goto cleanup;
	}

      QO_UI_N (uip) = n;

      /* USING INDEX NONE, or USING INDEX class_name.NONE,... case */
      if (is_none)
	{
	  continue;
	}

      /* attach indexes to QO_NODE */
      n = 0;
      for (indexp = using_index; indexp; indexp = indexp->next)
	{
	  /* check for USING INDEX NONE case */
	  if (indexp->info.name.original == NULL
	      && indexp->info.name.resolved == NULL)
	    {
	      break;		/* USING INDEX NONE case */
	    }

	  /* check if index is in ignore list (pointer comparison) */
	  is_ignored = false;
	  for (j = 0; j < idx_ignore_list_size; j++)
	    {
	      if (indexp == idx_ignore_list[j])
		{
		  is_ignored = true;
		  break;
		}
	    }
	  if (is_ignored)
	    {
	      continue;
	    }

	  /* attach index if necessary */
	  if (indexp->info.name.original == NULL
	      && indexp->info.name.resolved[0] == '*')
	    {
	      /* USING INDEX ALL EXCEPT case */
	      QO_UI_INDEX (uip, n) = indexp->info.name.resolved;
	      QO_UI_FORCE (uip, n++) = (int) (UINT64) (indexp->etc);
	    }
	  if (indexp->info.name.original
	      && !intl_identifier_casecmp (QO_NODE_NAME (nodep),
					   indexp->info.name.resolved))
	    {
	      QO_UI_INDEX (uip, n) = indexp->info.name.original;
	      QO_UI_KEYLIMIT (uip, n) = indexp->info.name.indx_key_limit;
	      QO_UI_FORCE (uip, n++) = (int) (UINT64) (indexp->etc);
	    }
	}
    }

cleanup:
  /* free memory of index ignore list */
  if (idx_ignore_list != NULL)
    {
      free (idx_ignore_list);
    }
}

/*
 * qo_alloc_index () - Allocate a QO_INDEX structure with room for <n>
 *		       QO_INDEX_ENTRY elements.  The fields are initialized
 *   return: QO_CLASS_INFO *
 *   env(in): The current optimizer environment
 *   n(in): The node whose class info we want
 */
static QO_INDEX *
qo_alloc_index (QO_ENV * env, int n)
{
  int i;
  QO_INDEX *indexp;
  QO_INDEX_ENTRY *entryp;

  indexp = (QO_INDEX *) malloc (SIZEOF_INDEX (n));
  if (indexp == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      SIZEOF_INDEX (n));
      return NULL;
    }

  indexp->n = 0;
  indexp->max = n;

  for (i = 0; i < n; i++)
    {
      entryp = QO_INDEX_INDEX (indexp, i);

      entryp->next = NULL;
      entryp->class_ = NULL;
      entryp->col_num = 0;
      entryp->key_type = NULL;
      entryp->nsegs = 0;
      entryp->seg_idxs = NULL;
      entryp->rangelist_seg_idx = -1;
      entryp->seg_equal_terms = NULL;
      entryp->seg_other_terms = NULL;
      bitset_init (&(entryp->terms), env);
      entryp->all_unique_index_columns_are_equi_terms = false;
      entryp->cover_segments = false;
      entryp->is_iss_candidate = false;
      entryp->first_sort_column = -1;
      entryp->orderby_skip = false;
      entryp->groupby_skip = false;
      entryp->use_descending = false;
      entryp->statistics_attribute_name = NULL;
      entryp->key_limit = NULL;
      entryp->constraints = NULL;
      entryp->ils_prefix_len = 0;
    }

  return indexp;
}

/*
 * qo_free_index () - Free the QO_INDEX structure and all elements contained
 *		      within it
 *   return: nothing
 *   env(in): The current optimizer environment
 *   indexp(in): A pointer to a previously-allocated index vector
 */
static void
qo_free_index (QO_ENV * env, QO_INDEX * indexp)
{
  int i, j;
  QO_INDEX_ENTRY *entryp;

  if (!indexp)
    {
      return;
    }

  for (i = 0; i < indexp->max; i++)
    {
      entryp = QO_INDEX_INDEX (indexp, i);
      bitset_delset (&(entryp->terms));
      for (j = 0; j < entryp->nsegs; j++)
	{
	  bitset_delset (&(entryp->seg_equal_terms[j]));
	  bitset_delset (&(entryp->seg_other_terms[j]));
	}
      if (entryp->nsegs)
	{
	  if (entryp->seg_equal_terms)
	    {
	      free_and_init (entryp->seg_equal_terms);
	    }
	  if (env, entryp->seg_other_terms)
	    {
	      free_and_init (entryp->seg_other_terms);
	    }
	  if (entryp->seg_idxs)
	    {
	      free_and_init (entryp->seg_idxs);
	    }

	  if (entryp->statistics_attribute_name)
	    {
	      free_and_init (entryp->statistics_attribute_name);
	    }
	}
      entryp->constraints = NULL;
    }

  if (indexp)
    {
      free_and_init (indexp);
    }
}

/*
 * qo_get_class_info () -
 *   return: QO_CLASS_INFO *
 *   env(in): The current optimizer environment
 *   node(in): The node whose class info we want
 */
static QO_CLASS_INFO *
qo_get_class_info (QO_ENV * env, QO_NODE * node)
{
  PT_NODE *dom_set;
  int n;
  QO_CLASS_INFO *info;
  QO_CLASS_INFO_ENTRY *end;
  int i;

  dom_set = QO_NODE_ENTITY_SPEC (node)->info.spec.flat_entity_list;
  n = count_classes (dom_set);
  info = (QO_CLASS_INFO *) malloc (SIZEOF_CLASS_INFO (n));
  if (info == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      SIZEOF_CLASS_INFO (n));
      return NULL;
    }

  for (i = 0; i < n; ++i)
    {
      info->info[i].name = NULL;
      info->info[i].mop = NULL;
      info->info[i].smclass = NULL;
      info->info[i].stats = NULL;
      info->info[i].self_allocated = 0;
      OID_SET_NULL (&info->info[i].oid);
      info->info[i].index = NULL;
    }

  info->n = n;
  end = grok_classes (env, dom_set, &info->info[0]);

  QO_ASSERT (env, end == &info->info[n]);

  return info;

}

/*
 * qo_free_class_info () - Free the vector and all interally-allocated
 *			   structures
 *   return: nothing
 *   env(in): The current optimizer environment
 *   info(in): A pointer to a previously-allocated info vector
 */
static void
qo_free_class_info (QO_ENV * env, QO_CLASS_INFO * info)
{
  int i;

  if (info == NULL)
    {
      return;
    }

  /*
   * The CLASS_STATS structures that are pointed to by the various
   * members of info[] will be automatically freed by the garbage
   * collector.  Make sure that we null out our mop pointer so that the
   * garbage collector doesn't mistakenly believe that the class object
   * is still in use.
   */
  for (i = 0; i < info->n; ++i)
    {
      qo_free_index (env, info->info[i].index);
      info->info[i].name = NULL;
      info->info[i].mop = NULL;
      if (info->info[i].self_allocated)
	{
	  free_and_init (info->info[i].stats);
	}
      info->info[i].smclass = NULL;
    }
  if (info)
    {
      free_and_init (info);
    }
}

/*
 * count_classes () - Count the number of object-based classes in the domain set
 *   return: int
 *   p(in):
 */
static int
count_classes (PT_NODE * p)
{
  int n;

  for (n = 0; p; p = p->next)
    {
      n++;
    }

  return n;
}

/*
 * grok_classes () -
 *   return: QO_CLASS_INFO_ENTRY *
 *   env(in): The current optimizer environment
 *   p(in): The flat list of entity_specs
 *   info(in): The next info slot to be initialized
 *
 * Note: Populate the info array by traversing the given flat list.
 *	info is assumed to point to a vector of QO_CLASS_INFO_ENTRY
 *	structures that is long enough to accept entries for all
 *	remaining object-based classes.  This should be the case if
 *	the length of the array was determined using count_classes()
 *	above.
 */
static QO_CLASS_INFO_ENTRY *
grok_classes (QO_ENV * env, PT_NODE * p, QO_CLASS_INFO_ENTRY * info)
{
  HFID *hfid;
  SM_CLASS *smclass;

  for (; p; p = p->next)
    {
      info->mop = p->info.name.db_object;
      info->normal_class = db_is_class (info->mop);
      if (info->mop)
	{
	  info->oid = *WS_OID (info->mop);
	  info->name = sm_class_name (info->mop);
	  info->smclass = sm_get_class_with_statistics (info->mop);
	}
      else
	{
	  PARSER_CONTEXT *parser = env->parser;
	  PT_INTERNAL_ERROR (parser, "info");
	  return info;
	}

      smclass = info->smclass;
      if (smclass == NULL)
	{
	  PARSER_CONTEXT *parser = env->parser;
	  PT_INTERNAL_ERROR (parser, "info");
	  return info;
	}

      if (smclass->stats == NULL)
	{
	  info->stats = (CLASS_STATS *) malloc (sizeof (CLASS_STATS));
	  if (info->stats == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (CLASS_STATS));
	      return NULL;
	    }

	  info->self_allocated = 1;
	  info->stats->n_attrs = 0;
	  info->stats->attr_stats = NULL;
	  qo_estimate_statistics (info->mop, info->stats);
	}
      else if (smclass->stats->heap_num_pages == 0)
	{
	  if (!info->normal_class
	      || (((hfid = sm_get_heap (info->mop)) && !HFID_IS_NULL (hfid))))
	    {
	      qo_estimate_statistics (info->mop, smclass->stats);
	    }
	}

      info++;
    }

  return info;
}

/*
 * qo_get_attr_info_func_index () - Find the statistics information about
 *			        the function index that underlies this segment
 *   return: QO_ATTR_INFO *
 *   env(in): The current optimizer environment
 *   seg(in): A (pointer to) a join graph segment
 *   expr_str(in):
 */
static QO_ATTR_INFO *
qo_get_attr_info_func_index (QO_ENV * env, QO_SEGMENT * seg,
			     const char *expr_str)
{
  QO_NODE *nodep;
  QO_CLASS_INFO_ENTRY *class_info_entryp;
  SM_CLASS_CONSTRAINT *consp;
  QO_ATTR_INFO *attr_infop = NULL;
  QO_ATTR_CUM_STATS *cum_statsp;
  BTREE_STATS *bstatsp = NULL;
  ATTR_STATS *attr_statsp = NULL;
  int n, i, j;
  int attr_id;
  int n_attrs;
  CLASS_STATS *stats;

  nodep = QO_SEG_HEAD (seg);

  if (QO_NODE_INFO (nodep) == NULL ||
      !(QO_NODE_INFO (nodep)->info[0].normal_class))
    {
      /* if there's no class information or the class is not normal class */
      return NULL;
    }

  /* number of class information entries */
  n = QO_NODE_INFO_N (nodep);
  QO_ASSERT (env, n > 0);

  /* pointer to QO_CLASS_INFO_ENTRY[] array of the node */
  class_info_entryp = &QO_NODE_INFO (nodep)->info[0];

  /* allocate QO_ATTR_INFO within the current optimizer environment */
  attr_infop = (QO_ATTR_INFO *) malloc (sizeof (QO_ATTR_INFO));
  if (attr_infop == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      sizeof (QO_ATTR_INFO));
      return NULL;
    }

  cum_statsp = &attr_infop->cum_stats;
  cum_statsp->type = pt_type_enum_to_db (QO_SEG_PT_NODE (seg)->type_enum);
  cum_statsp->valid_limits = false;
  cum_statsp->is_indexed = true;
  cum_statsp->leafs = cum_statsp->pages = cum_statsp->height = 0;
  cum_statsp->keys = 0;
  cum_statsp->key_type = NULL;
  cum_statsp->pkeys_size = 0;
  cum_statsp->pkeys = NULL;

  /* set the statistics from the class information(QO_CLASS_INFO_ENTRY) */
  for (i = 0; i < n; class_info_entryp++, i++)
    {
      stats = QO_GET_CLASS_STATS (class_info_entryp);
      QO_ASSERT (env, stats != NULL);
      if (stats->attr_stats == NULL)
	{
	  /* the attribute statistics of the class were not set */
	  cum_statsp->is_indexed = false;
	  continue;
	  /* We'll consider the segment to be indexed only if all of the
	     attributes it represents are indexed. The current optimization
	     strategy makes it inconvenient to try to construct "mixed"
	     (segment and index) scans of a node that represents more than
	     one node. */
	}

      for (consp = class_info_entryp->smclass->constraints; consp;
	   consp = consp->next)
	{
	  /* search the attribute from the class information */
	  attr_statsp = stats->attr_stats;
	  n_attrs = stats->n_attrs;

	  if (consp->func_index_info && consp->func_index_info->col_id == 0
	      && !intl_identifier_casecmp (expr_str,
					   consp->func_index_info->expr_str))
	    {
	      attr_id = consp->attributes[0]->id;

	      for (j = 0; j < n_attrs; j++, attr_statsp++)
		{
		  if (attr_statsp->id == attr_id)
		    {
		      break;
		    }
		}
	      if (j == n_attrs)
		{
		  /* attribute not found, what happens to the class attribute? */
		  cum_statsp->is_indexed = false;
		  continue;
		}

	      bstatsp = attr_statsp->bt_stats;
	      for (j = 0; j < attr_statsp->n_btstats; j++, bstatsp++)
		{
		  if (BTID_IS_EQUAL (&bstatsp->btid, &consp->index_btid) &&
		      bstatsp->has_function == 1)
		    {
		      break;
		    }
		}

	      if (cum_statsp->valid_limits == false)
		{
		  /* first time */
		  cum_statsp->valid_limits = true;
		}

	      cum_statsp->leafs += bstatsp->leafs;
	      cum_statsp->pages += bstatsp->pages;
	      cum_statsp->height = MAX (cum_statsp->height, bstatsp->height);

	      if (cum_statsp->pkeys_size == 0 ||	/* the first found */
		  cum_statsp->keys < bstatsp->keys)
		{
		  cum_statsp->keys = bstatsp->keys;
		  cum_statsp->key_type = bstatsp->key_type;
		  cum_statsp->pkeys_size = bstatsp->pkeys_size;
		  /* alloc pkeys[] within the current optimizer environment */
		  if (cum_statsp->pkeys)
		    {
		      free_and_init (cum_statsp->pkeys);
		    }
		  cum_statsp->pkeys = (int *)
		    malloc (SIZEOF_ATTR_CUM_STATS_PKEYS
			    (cum_statsp->pkeys_size));
		  if (cum_statsp->pkeys == NULL)
		    {
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			      ER_OUT_OF_VIRTUAL_MEMORY, 1,
			      SIZEOF_ATTR_CUM_STATS_PKEYS
			      (cum_statsp->pkeys_size));
		      qo_free_attr_info (env, attr_infop);
		      return NULL;
		    }

		  assert (cum_statsp->pkeys_size <= BTREE_STATS_PKEYS_NUM);
		  for (i = 0; i < cum_statsp->pkeys_size; i++)
		    {
		      cum_statsp->pkeys[i] = bstatsp->pkeys[i];
		    }
		}
	    }
	}
    }

  return attr_infop;
}

/*
 * qo_get_attr_info () - Find the ATTR_STATS information about each actual
 *			 attribute that underlies this segment
 *   return: QO_ATTR_INFO *
 *   env(in): The current optimizer environment
 *   seg(in): A (pointer to) a join graph segment
 */
static QO_ATTR_INFO *
qo_get_attr_info (QO_ENV * env, QO_SEGMENT * seg)
{
  QO_NODE *nodep;
  QO_CLASS_INFO_ENTRY *class_info_entryp;
  QO_ATTR_INFO *attr_infop;
  int attr_id;
  QO_ATTR_CUM_STATS *cum_statsp;
  ATTR_STATS *attr_statsp;
  BTREE_STATS *bt_statsp;
  int n_attrs;
  const char *name;
  int n, i, j;
  int n_func_indexes;
  SM_CLASS_CONSTRAINT *consp;
  CLASS_STATS *stats;
  bool is_reserved_name = false;

  if ((QO_SEG_PT_NODE (seg))->info.name.meta_class == PT_RESERVED)
    {
      is_reserved_name = true;
    }

  /* actual attribute name of the given segment */
  name = QO_SEG_NAME (seg);
  /* QO_NODE of the given segment */
  nodep = QO_SEG_HEAD (seg);

  if (QO_NODE_INFO (nodep) == NULL ||
      !(QO_NODE_INFO (nodep)->info[0].normal_class))
    {
      /* if there's no class information or the class is not normal class */
      return NULL;
    }

  /* number of class information entries */
  n = QO_NODE_INFO_N (nodep);
  QO_ASSERT (env, n > 0);

  /* pointer to QO_CLASS_INFO_ENTRY[] array of the node */
  class_info_entryp = &QO_NODE_INFO (nodep)->info[0];

  /* allocate QO_ATTR_INFO within the current optimizer environment */
  attr_infop = (QO_ATTR_INFO *) malloc (sizeof (QO_ATTR_INFO));
  if (attr_infop == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      sizeof (QO_ATTR_INFO));
      return NULL;
    }

  /* initialize QO_ATTR_CUM_STATS structure of QO_ATTR_INFO */
  cum_statsp = &attr_infop->cum_stats;
  if (is_reserved_name)
    {
      cum_statsp->type =
	pt_Reserved_name_table[(QO_SEG_PT_NODE (seg))->info.name.reserved_id].
	type;
      cum_statsp->valid_limits = false;
      cum_statsp->is_indexed = true;
      cum_statsp->leafs = cum_statsp->pages = cum_statsp->height = 0;
      cum_statsp->keys = 0;
      cum_statsp->key_type = NULL;
      cum_statsp->pkeys_size = 0;
      cum_statsp->pkeys = NULL;

      return attr_infop;
    }

  /* not a reserved name */
  cum_statsp->type = sm_att_type_id (class_info_entryp->mop, name);
  cum_statsp->valid_limits = false;
  cum_statsp->is_indexed = true;
  cum_statsp->leafs = cum_statsp->pages = cum_statsp->height = 0;
  cum_statsp->keys = 0;
  cum_statsp->key_type = NULL;
  cum_statsp->pkeys_size = 0;
  cum_statsp->pkeys = NULL;

  /* set the statistics from the class information(QO_CLASS_INFO_ENTRY) */
  for (i = 0; i < n; class_info_entryp++, i++)
    {
      attr_id = sm_att_id (class_info_entryp->mop, name);

      /* pointer to ATTR_STATS of CLASS_STATS of QO_CLASS_INFO_ENTRY */
      stats = QO_GET_CLASS_STATS (class_info_entryp);
      QO_ASSERT (env, stats != NULL);
      if (stats->attr_stats == NULL)
	{
	  /* the attribute statistics of the class were not set */
	  cum_statsp->is_indexed = false;
	  continue;
	  /* We'll consider the segment to be indexed only if all of the
	     attributes it represents are indexed. The current optimization
	     strategy makes it inconvenient to try to construct "mixed"
	     (segment and index) scans of a node that represents more than
	     one node. */
	}

      /* The stats vector isn't kept in id order because of the effects
         of schema updates (attribute deletion, most notably). We need
         to search it to find the stats record we're interested in.
         Worse, there doesn't even need to be an entry for this particular
         attribute in the vector. If we're dealing with a class that was
         created after the last statistics update, it won't have any
         information associated with it, or if we're dealing with certain
         kinds of attributes they simply won't be recorded. In these cases
         we just make the best guess we can. */

      /* search the attribute from the class information */
      attr_statsp = stats->attr_stats;
      n_attrs = stats->n_attrs;
      for (j = 0; j < n_attrs; j++, attr_statsp++)
	{
	  if (attr_statsp->id == attr_id)
	    {
	      break;
	    }
	}
      if (j == n_attrs)
	{
	  /* attribute not found, what happens to the class attribute? */
	  cum_statsp->is_indexed = false;
	  continue;
	}

      if (cum_statsp->valid_limits == false)
	{
	  /* first time */
	  cum_statsp->type = attr_statsp->type;
	  cum_statsp->valid_limits = true;
	}

      n_func_indexes = 0;
      for (j = 0; j < attr_statsp->n_btstats; j++)
	{
	  if (attr_statsp->bt_stats[j].has_function == 1)
	    {
	      n_func_indexes++;
	    }
	}

      if (attr_statsp->n_btstats - n_func_indexes <= 0
	  || !attr_statsp->bt_stats)
	{
	  /* the attribute does not have any index */
	  cum_statsp->is_indexed = false;
	  continue;
	  /* We'll consider the segment to be indexed only if all of the
	     attributes it represents are indexed. The current optimization
	     strategy makes it inconvenient to try to construct "mixed"
	     (segment and index) scans of a node that represents more than
	     one node. */
	}

      /* Because we cannot know which index will be selected for this
         attribute when there're more than one indexes on this attribute,
         use the statistics of the MIN keys index. */
      bt_statsp = &attr_statsp->bt_stats[0];
      for (j = 1; j < attr_statsp->n_btstats; j++)
	{
	  if (bt_statsp->keys > attr_statsp->bt_stats[j].keys)
	    {
	      bt_statsp = &attr_statsp->bt_stats[j];
	    }
	}

      if (QO_NODE_ENTITY_SPEC (nodep)->info.spec.only_all == PT_ALL)
	{
	  /* class hierarchy spec
	     for example: select ... from all p */

	  /* check index uniqueness */
	  for (consp = sm_class_constraints (class_info_entryp->mop);
	       consp; consp = consp->next)
	    {
	      if (SM_IS_CONSTRAINT_UNIQUE_FAMILY (consp->type)
		  && BTID_IS_EQUAL (&bt_statsp->btid, &consp->index_btid))
		{
		  break;
		}
	    }

	  if (consp)		/* is unique index */
	    {
	      /* is class hierarchy index: set unique index statistics */
	      cum_statsp->leafs = bt_statsp->leafs;
	      cum_statsp->pages = bt_statsp->pages;
	      cum_statsp->height = bt_statsp->height;
	      cum_statsp->keys = bt_statsp->keys;
	      cum_statsp->key_type = bt_statsp->key_type;
	      cum_statsp->pkeys_size = bt_statsp->pkeys_size;
	      /* alloc pkeys[] within the current optimizer environment */
	      if (cum_statsp->pkeys != NULL)
		{
		  free_and_init (cum_statsp->pkeys);	/* free alloced */
		}
	      cum_statsp->pkeys =
		(int *)
		malloc (SIZEOF_ATTR_CUM_STATS_PKEYS (cum_statsp->pkeys_size));
	      if (cum_statsp->pkeys == NULL)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_OUT_OF_VIRTUAL_MEMORY, 1,
			  SIZEOF_ATTR_CUM_STATS_PKEYS
			  (cum_statsp->pkeys_size));
		  qo_free_attr_info (env, attr_infop);
		  return NULL;
		}

	      assert (cum_statsp->pkeys_size <= BTREE_STATS_PKEYS_NUM);
	      for (j = 0; j < cum_statsp->pkeys_size; j++)
		{
		  cum_statsp->pkeys[j] = bt_statsp->pkeys[j];
		}

	      /* immediately return the allocated QO_ATTR_INFO */
	      return attr_infop;
	    }
	}

      /* keep cumulative totals of index statistics */
      cum_statsp->leafs += bt_statsp->leafs;
      cum_statsp->pages += bt_statsp->pages;
      /* Assume that the key distributions overlap here, so that the
         number of distinct keys in all of the attributes equal to the
         maximum number of distinct keys in any one of the attributes.
         This is probably not far from the truth; it is almost certainly
         a better guess than assuming that all key ranges are distinct. */
      cum_statsp->height = MAX (cum_statsp->height, bt_statsp->height);
      if (cum_statsp->pkeys_size == 0 ||	/* the first found */
	  cum_statsp->keys < bt_statsp->keys)
	{
	  cum_statsp->keys = bt_statsp->keys;
	  cum_statsp->key_type = bt_statsp->key_type;
	  cum_statsp->pkeys_size = bt_statsp->pkeys_size;
	  /* alloc pkeys[] within the current optimizer environment */
	  if (cum_statsp->pkeys)
	    {
	      free_and_init (cum_statsp->pkeys);
	    }
	  cum_statsp->pkeys =
	    (int *)
	    malloc (SIZEOF_ATTR_CUM_STATS_PKEYS (cum_statsp->pkeys_size));
	  if (cum_statsp->pkeys == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_OUT_OF_VIRTUAL_MEMORY, 1,
		      SIZEOF_ATTR_CUM_STATS_PKEYS (cum_statsp->pkeys_size));
	      qo_free_attr_info (env, attr_infop);
	      return NULL;
	    }

	  assert (cum_statsp->pkeys_size <= BTREE_STATS_PKEYS_NUM);
	  for (j = 0; j < cum_statsp->pkeys_size; j++)
	    {
	      cum_statsp->pkeys[j] = bt_statsp->pkeys[j];
	    }
	}

    }				/* for (i = 0; i < n; ...) */

  /* return the allocated QO_ATTR_INFO */
  return attr_infop;
}

/*
 * qo_free_attr_info () - Free the vector and any internally allocated
 *			  structures
 *   return: nothing
 *   env(in): The current optimizer environment
 *   info(in): A pointer to a previously allocated info vector
 */
static void
qo_free_attr_info (QO_ENV * env, QO_ATTR_INFO * info)
{
  QO_ATTR_CUM_STATS *cum_statsp;

  if (info)
    {
      cum_statsp = &info->cum_stats;
      if (cum_statsp->pkeys)
	{
	  free_and_init (cum_statsp->pkeys);
	}
      free_and_init (info);
    }
}

/*
 * qo_get_index_info () - Get index statistical information
 *   return:
 *   env(in): The current optimizer environment
 *   node(in): A join graph node
 */
static void
qo_get_index_info (QO_ENV * env, QO_NODE * node)
{
  QO_NODE_INDEX *node_indexp;
  QO_NODE_INDEX_ENTRY *ni_entryp;
  QO_INDEX_ENTRY *index_entryp;
  QO_ATTR_CUM_STATS *cum_statsp;
  QO_SEGMENT *segp;
  QO_NODE *seg_node;
  QO_CLASS_INFO_ENTRY *class_info_entryp = NULL;
  const char *name;
  int attr_id, n_attrs;
  ATTR_STATS *attr_statsp;
  BTREE_STATS *bt_statsp;
  int i, j, k;
  CLASS_STATS *stats;

  /* pointer to QO_NODE_INDEX structure of QO_NODE */
  node_indexp = QO_NODE_INDEXES (node);

  /* for each index list(linked list of QO_INDEX_ENTRY) rooted at the node
     (all elements of QO_NODE_INDEX_ENTRY[] array) */
  for (i = 0, ni_entryp = QO_NI_ENTRY (node_indexp, 0);
       i < QO_NI_N (node_indexp); i++, ni_entryp++)
    {
      cum_statsp = &(ni_entryp)->cum_stats;
      cum_statsp->is_indexed = true;

      /* The linked list of QO_INDEX_ENTRY was built by 'qo_find_node_index()'
         function. It is the list of compatible indexes under class
         hierarchy. */
      /* for each index entry(QO_INDEX_ENTRY) on the list, acquire
         the statistics and cumulate them */
      for (j = 0, index_entryp = (ni_entryp)->head;
	   index_entryp != NULL; j++, index_entryp = index_entryp->next)
	{
	  /* The index information is associated with the first attribute of
	     index keys in the case of multi-column index and 'seg_idx[]'
	     array of QO_INDEX_ENTRY structure was built by
	     'qo_find_index_seg_and_term()' function to keep the order of
	     index key attributes. So, 'seg_idx[0]' is the right segment
	     denoting the attribute that contains the index statistics that
	     we want to get. If seg_idx[0] is null (-1), then the name of the
	     first attribute is taken from index_entryp->statistics_attribute_name
	   */
	  segp = NULL;
	  for (k = 0; k < index_entryp->nsegs; k++)
	    {
	      if (index_entryp->seg_idxs[k] != -1)
		{
		  segp = QO_ENV_SEG (env, (index_entryp->seg_idxs[k]));

		  if (segp != NULL)
		    {
		      break;
		    }
		}
	    }

	  if (segp == NULL)
	    {
	      index_entryp->key_type = NULL;
	      continue;
	    }

	  /* QO_NODE of the given segment */
	  seg_node = QO_SEG_HEAD (segp);

	  if (k == 0)
	    {
	      /* actual attribute name of the given segment */
	      name = QO_SEG_NAME (segp);
	    }
	  else
	    {
	      /* actual attribute name of the given segment */
	      name = index_entryp->statistics_attribute_name;
	    }

	  /* pointer to QO_CLASS_INFO_ENTRY[] array of the node */
	  class_info_entryp = &QO_NODE_INFO (seg_node)->info[j];

	  /* pointer to ATTR_STATS of CLASS_STATS of QO_CLASS_INFO_ENTRY */
	  stats = QO_GET_CLASS_STATS (class_info_entryp);
	  QO_ASSERT (env, stats != NULL);

	  /* search the attribute from the class information */
	  attr_statsp = stats->attr_stats;
	  n_attrs = stats->n_attrs;

	  if (!index_entryp->is_func_index)
	    {
	      attr_id = sm_att_id (class_info_entryp->mop, name);
	    }
	  else
	    {
	      /* function index with the function expression as the first
	       * attribute
	       */
	      attr_id = index_entryp->constraints->attributes[0]->id;
	    }

	  for (k = 0; k < n_attrs; k++, attr_statsp++)
	    {
	      if (attr_statsp->id == attr_id)
		{
		  break;
		}
	    }

	  index_entryp->key_type = NULL;
	  if (k >= n_attrs)	/* not found */
	    {
	      attr_statsp = NULL;
	      continue;
	    }

	  if (cum_statsp->valid_limits == false)
	    {
	      /* first time */
	      cum_statsp->type = attr_statsp->type;
	      cum_statsp->valid_limits = true;
	    }

	  /* find the index that we are interesting within BTREE_STATS[] array */
	  bt_statsp = attr_statsp->bt_stats;
	  for (k = 0; k < attr_statsp->n_btstats; k++, bt_statsp++)
	    {
	      if (BTID_IS_EQUAL (&bt_statsp->btid,
				 &(index_entryp->constraints->index_btid)))
		{
		  if (!index_entryp->is_func_index
		      || bt_statsp->has_function == 1)
		    {
		      index_entryp->key_type = bt_statsp->key_type;
		      break;
		    }
		}
	    }			/* for (k = 0, ...) */

	  if (k == attr_statsp->n_btstats)
	    {
	      /* cannot find index in this attribute. what happens? */
	      continue;
	    }

	  if (QO_NODE_ENTITY_SPEC (node)->info.spec.only_all == PT_ALL)
	    {
	      /* class hierarchy spec
	         for example: select ... from all p */

	      /* check index uniqueness */
	      if (SM_IS_CONSTRAINT_UNIQUE_FAMILY
		  (index_entryp->constraints->type))
		{
		  /* is class hierarchy index: set unique index statistics */
		  cum_statsp->leafs = bt_statsp->leafs;
		  cum_statsp->pages = bt_statsp->pages;
		  cum_statsp->height = bt_statsp->height;
		  cum_statsp->keys = bt_statsp->keys;
		  cum_statsp->key_type = bt_statsp->key_type;
		  cum_statsp->pkeys_size = bt_statsp->pkeys_size;
		  /* alloc pkeys[] within the current optimizer environment */
		  if (cum_statsp->pkeys)
		    {
		      free_and_init (cum_statsp->pkeys);
		    }
		  cum_statsp->pkeys =
		    (int *)
		    malloc (SIZEOF_ATTR_CUM_STATS_PKEYS
			    (cum_statsp->pkeys_size));
		  if (cum_statsp->pkeys == NULL)
		    {
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			      ER_OUT_OF_VIRTUAL_MEMORY, 1,
			      SIZEOF_ATTR_CUM_STATS_PKEYS
			      (cum_statsp->pkeys_size));
		      return;	/* give up */
		    }

		  assert (cum_statsp->pkeys_size <= BTREE_STATS_PKEYS_NUM);
		  for (k = 0; k < cum_statsp->pkeys_size; k++)
		    {
		      cum_statsp->pkeys[k] = bt_statsp->pkeys[k];
		    }

		  /* immediately finish getting index statistics */
		  break;	/* for (j = 0, ... ) */
		}
	    }

	  /* keep cumulative totals of index statistics */
	  cum_statsp->leafs += bt_statsp->leafs;
	  cum_statsp->pages += bt_statsp->pages;
	  /* Assume that the key distributions overlap here, so that the
	     number of distinct keys in all of the attributes equal to the
	     maximum number of distinct keys in any one of the attributes.
	     This is probably not far from the truth; it is almost
	     certainly a better guess than assuming that all key ranges
	     are distinct. */
	  cum_statsp->height = MAX (cum_statsp->height, bt_statsp->height);
	  if (cum_statsp->pkeys_size == 0 ||	/* the first found */
	      cum_statsp->keys < bt_statsp->keys)
	    {
	      cum_statsp->keys = bt_statsp->keys;
	      cum_statsp->key_type = bt_statsp->key_type;
	      cum_statsp->pkeys_size = bt_statsp->pkeys_size;
	      /* alloc pkeys[] within the current optimizer environment */
	      if (cum_statsp->pkeys)
		{
		  free_and_init (cum_statsp->pkeys);
		}
	      cum_statsp->pkeys =
		(int *)
		malloc (SIZEOF_ATTR_CUM_STATS_PKEYS (cum_statsp->pkeys_size));
	      if (cum_statsp->pkeys == NULL)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_OUT_OF_VIRTUAL_MEMORY, 1,
			  SIZEOF_ATTR_CUM_STATS_PKEYS
			  (cum_statsp->pkeys_size));
		  return;	/* give up */
		}

	      assert (cum_statsp->pkeys_size <= BTREE_STATS_PKEYS_NUM);
	      for (k = 0; k < cum_statsp->pkeys_size; k++)
		{
		  cum_statsp->pkeys[k] = bt_statsp->pkeys[k];
		}
	    }
	}			/* for (j = 0, ... ) */
    }				/* for (i = 0, ...) */
}

/*
 * qo_free_node_index_info () - Free the vector and any internally allocated
 *				structures
 *   return: nothing
 *   env(in): The current optimizer environment
 *   node_indexp(in): A pointer to QO_NODE_INDEX structure of QO_NODE
 */
static void
qo_free_node_index_info (QO_ENV * env, QO_NODE_INDEX * node_indexp)
{
  QO_NODE_INDEX_ENTRY *ni_entryp;
  QO_ATTR_CUM_STATS *cum_statsp;
  int i;

  if (node_indexp)
    {
      /* for each index list(linked list of QO_INDEX_ENTRY) rooted at the node
         (all elements of QO_NODE_INDEX_ENTRY[] array) */
      for (i = 0, ni_entryp = QO_NI_ENTRY (node_indexp, 0);
	   i < QO_NI_N (node_indexp); i++, ni_entryp++)
	{
	  cum_statsp = &(ni_entryp)->cum_stats;
	  if (cum_statsp->pkeys)
	    {
	      free_and_init (cum_statsp->pkeys);
	    }
	}

      free_and_init (node_indexp);
    }
}

/*
 * qo_data_compare () -
 *   return: 1, 0, -1
 *   data1(in):
 *   data2(in):
 *   type(in):
 *
 * Note: This is a simplified function that works with DB_DATA
 *      instead of DB_VALUE, which is the same function of 'qst_data_compare()'.
 */
static int
qo_data_compare (DB_DATA * data1, DB_DATA * data2, DB_TYPE type)
{
  int result;

  switch (type)
    {
    case DB_TYPE_INTEGER:
      result = (data1->i < data2->i) ? -1 : ((data1->i > data2->i) ? 1 : 0);
      break;
    case DB_TYPE_SHORT:
      result = ((data1->sh < data2->sh) ?
		-1 : ((data1->sh > data2->sh) ? 1 : 0));
      break;
    case DB_TYPE_BIGINT:
      result = ((data1->bigint < data2->bigint) ?
		-1 : ((data1->bigint > data2->bigint) ? 1 : 0));
      break;
    case DB_TYPE_FLOAT:
      result = (data1->f < data2->f) ? -1 : ((data1->f > data2->f) ? 1 : 0);
      break;
    case DB_TYPE_DOUBLE:
      result = (data1->d < data2->d) ? -1 : ((data1->d > data2->d) ? 1 : 0);
      break;
    case DB_TYPE_DATE:
      result = ((data1->date < data2->date) ?
		-1 : ((data1->date > data2->date) ? 1 : 0));
      break;
    case DB_TYPE_TIME:
      result = ((data1->time < data2->time) ?
		-1 : ((data1->time > data2->time) ? 1 : 0));
      break;
    case DB_TYPE_UTIME:
      result = ((data1->utime < data2->utime) ?
		-1 : ((data1->utime > data2->utime) ? 1 : 0));
      break;
    case DB_TYPE_DATETIME:
      if (data1->datetime.date < data2->datetime.date)
	{
	  result = -1;
	}
      else if (data1->datetime.date > data2->datetime.date)
	{
	  result = 1;
	}
      else if (data1->datetime.time < data2->datetime.time)
	{
	  result = -1;
	}
      else if (data1->datetime.time > data2->datetime.time)
	{
	  result = 1;
	}
      else
	{
	  result = 0;
	}
      break;
    case DB_TYPE_MONETARY:
      result = ((data1->money.amount < data2->money.amount) ?
		-1 : ((data1->money.amount > data2->money.amount) ? 1 : 0));
      break;
    default:
      /* not numeric type */
      result = 0;
      break;
    }

  return result;
}

/*
 * qo_estimate_statistics () - Make a wild-ass guess at the appropriate
 *			       statistics for this class.  The statistics
 *			       manager doesn't know anything about this class,
 *			       so we're on our own.
 *   return: nothing
 *   class_mop(in): The mop of the class whose statistics need to be
                    fabricated
 *   statblock(in): The CLASS_STATS structure to be populated
 */
static void
qo_estimate_statistics (MOP class_mop, CLASS_STATS * statblock)
{
  /*
   * It would be nice if we could the get the actual number of pages
   * allocated for the class; at least then we could make some sort of
   * realistic guess at the upper bound of the number of objects (we
   * can already figure out the "average" size of an object).
   *
   * Really, the statistics manager ought to be doing this on its own.
   */

  statblock->heap_num_pages = NOMINAL_HEAP_SIZE (class_mop);
  statblock->heap_num_objects =
    (statblock->heap_num_pages * DB_PAGESIZE) /
    NOMINAL_OBJECT_SIZE (class_mop);

}

/*
 * qo_env_new () -
 *   return:
 *   parser(in):
 *   query(in):
 */
static QO_ENV *
qo_env_new (PARSER_CONTEXT * parser, PT_NODE * query)
{
  QO_ENV *env;
  PT_NODE *spec;

  env = (QO_ENV *) malloc (sizeof (QO_ENV));
  if (env == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      sizeof (QO_ENV));
      return NULL;
    }

  env->parser = parser;
  env->pt_tree = query;
  env->nsegs = 0;
  env->nnodes = 0;
  env->nedges = 0;
  env->neqclasses = 0;
  env->nterms = 0;
  env->nsubqueries = 0;
  env->npartitions = 0;
  env->final_plan = NULL;
  env->segs = NULL;
  env->nodes = NULL;
  env->eqclasses = NULL;
  env->terms = NULL;
  env->subqueries = NULL;
  env->partitions = NULL;
  bitset_init (&(env->final_segs), env);
  env->tmp_bitset = NULL;
  env->bail_out = 0;
  env->planner = NULL;
  env->dump_enable = prm_get_bool_value (PRM_ID_QO_DUMP);
  bitset_init (&(env->fake_terms), env);
  bitset_init (&QO_ENV_SORT_LIMIT_NODES (env), env);
  DB_MAKE_NULL (&QO_ENV_LIMIT_VALUE (env));

  assert (query->node_type == PT_SELECT);
  if (PT_SELECT_INFO_IS_FLAGED (query, PT_SELECT_INFO_COLS_SCHEMA)
      || PT_SELECT_INFO_IS_FLAGED (query, PT_SELECT_FULL_INFO_COLS_SCHEMA)
      || ((spec = query->info.query.q.select.from) != NULL
	  && spec->info.spec.derived_table_type == PT_IS_SHOWSTMT))
    {
      env->plan_dump_enabled = false;
    }
  else
    {
      env->plan_dump_enabled = true;
    }
  env->multi_range_opt_candidate = false;

  return env;
}

#if 0
/*
 * qo_malloc () - Try to allocate the requested number of bytes.  If that
 *                fails, throw to some enclosing unwind-protect handler
 *   return: void *
 *   env(in): The optimizer environment from which the request is issued
 *   size(in): The number of bytes requested
 *   file(in): The file from which qo_malloc() was called
 *   line(in): The line number of from which qo_malloc() was called
 */
void *
qo_malloc (QO_ENV * env, unsigned size, const char *file, int line)
{
  void *p;

  p = malloc (size);
  if (p == NULL)
    {
      longjmp (env->catch, 1);
    }
  return p;

}
#endif

/*
 * qo_abort () -
 *   return:
 *   env(in):
 *   file(in):
 *   line(in):
 */
void
qo_abort (QO_ENV * env, const char *file, int line)
{
  er_set (ER_WARNING_SEVERITY, file, line, ER_FAILED_ASSERTION, 1, "false");
  longjmp (env->catch_, 2);
}

/*
 * qo_env_free () -
 *   return:
 *   env(in):
 */
void
qo_env_free (QO_ENV * env)
{
  if (env)
    {
      int i;

      /*
       * Be sure to use Nnodes, Nterms, and Nsegs as the loop limits in
       * the code below, because those are the sizes of the allocated
       * arrays; nnodes, nterms, and nsegs are the extents of those
       * arrays that were actually used, but the entries past those
       * extents need to be cleaned up too.
       */

      if (env->segs)
	{
	  for (i = 0; i < env->Nsegs; ++i)
	    {
	      qo_seg_free (QO_ENV_SEG (env, i));
	    }
	  free_and_init (env->segs);
	}

      if (env->nodes)
	{
	  for (i = 0; i < env->Nnodes; ++i)
	    {
	      qo_node_free (QO_ENV_NODE (env, i));
	    }
	  free_and_init (env->nodes);
	}

      if (env->eqclasses)
	{
	  for (i = 0; i < env->neqclasses; ++i)
	    {
	      qo_eqclass_free (QO_ENV_EQCLASS (env, i));
	    }
	  free_and_init (env->eqclasses);
	}

      if (env->terms)
	{
	  for (i = 0; i < env->Nterms; ++i)
	    {
	      qo_term_free (QO_ENV_TERM (env, i));
	    }
	  free_and_init (env->terms);
	}

      if (env->partitions)
	{
	  for (i = 0; i < env->npartitions; ++i)
	    {
	      qo_partition_free (QO_ENV_PARTITION (env, i));
	    }
	  free_and_init (env->partitions);
	}

      if (env->subqueries)
	{
	  for (i = 0; i < env->nsubqueries; ++i)
	    {
	      qo_subquery_free (&env->subqueries[i]);
	    }
	  free_and_init (env->subqueries);
	}

      bitset_delset (&(env->final_segs));
      bitset_delset (&(env->fake_terms));
      bitset_delset (&QO_ENV_SORT_LIMIT_NODES (env));
      pr_clear_value (&QO_ENV_LIMIT_VALUE (env));

      if (env->planner)
	{
	  qo_planner_free (env->planner);
	}

      free_and_init (env);
    }
}

/*
 * qo_exchange () -
 *   return:
 *   t0(in):
 *   t1(in):
 */
static void
qo_exchange (QO_TERM * t0, QO_TERM * t1)
{

  /*
   * 'env' attribute is the same in both, don't bother with it.
   */
  TERMCLASS_EXCHANGE (t0->term_class, t1->term_class);
  BISET_EXCHANGE (t0->nodes, t1->nodes);
  BISET_EXCHANGE (t0->segments, t1->segments);
  DOUBLE_EXCHANGE (t0->selectivity, t1->selectivity);
  INT_EXCHANGE (t0->rank, t1->rank);
  PT_NODE_EXCHANGE (t0->pt_expr, t1->pt_expr);
  INT_EXCHANGE (t0->location, t1->location);
  BISET_EXCHANGE (t0->subqueries, t1->subqueries);
  JOIN_TYPE_EXCHANGE (t0->join_type, t1->join_type);
  INT_EXCHANGE (t0->can_use_index, t1->can_use_index);
  SEGMENTPTR_EXCHANGE (t0->index_seg[0], t1->index_seg[0]);
  SEGMENTPTR_EXCHANGE (t0->index_seg[1], t1->index_seg[1]);
  SEGMENTPTR_EXCHANGE (t0->seg, t1->seg);
  SEGMENTPTR_EXCHANGE (t0->oid_seg, t1->oid_seg);
  NODEPTR_EXCHANGE (t0->head, t1->head);
  NODEPTR_EXCHANGE (t0->tail, t1->tail);
  EQCLASSPTR_EXCHANGE (t0->eqclass, t1->eqclass);
  SEGMENTPTR_EXCHANGE (t0->nominal_seg, t1->nominal_seg);
  FLAG_EXCHANGE (t0->flag, t1->flag);
  /*
   * DON'T exchange the 'idx' values!
   */
}

/*
 * qo_discover_edges () -
 *   return:
 *   env(in):
 */
static void
qo_discover_edges (QO_ENV * env)
{
  int i, j, n;
  QO_TERM *term, *edge, *edge2;
  QO_NODE *node;
  PT_NODE *pt_expr;
  int t;
  BITSET_ITERATOR iter;
  BITSET direct_nodes;
  int t1, t2;
  QO_TERM *term1, *term2;

  bitset_init (&direct_nodes, env);

  i = 0;
  n = env->nterms;

  while (i < n)
    {
      term = QO_ENV_TERM (env, i);
      if (QO_IS_EDGE_TERM (term))
	{
	  env->nedges++;
	  i++;
	}
      else
	{
	  if (i < --n)
	    {
	      /*
	       * Exchange the terms at the two boundaries.  This moves
	       * a known non-edge up to just below the section of other
	       * non-edge terms, and moves a term of unknown "edgeness"
	       * down to just above the section of known edges.  Leave
	       * the bottom boundary alone, but move the upper boundary
	       * down one notch.
	       */
	      qo_exchange (QO_ENV_TERM (env, i), QO_ENV_TERM (env, n));
	    }
	}
    }

  /* sort join-term on selectivity as descending order */
  for (t1 = 0; t1 < i - 1; t1++)
    {
      term1 = QO_ENV_TERM (env, t1);
      for (t2 = t1 + 1; t2 < i; t2++)
	{
	  term2 = QO_ENV_TERM (env, t2);
	  if (QO_TERM_SELECTIVITY (term1) < QO_TERM_SELECTIVITY (term2))
	    {
	      qo_exchange (term1, term2);
	    }
	}
    }
  /* sort sarg-term on selectivity as descending order */
  for (t1 = i; t1 < env->nterms - 1; t1++)
    {
      term1 = QO_ENV_TERM (env, t1);
      for (t2 = t1 + 1; t2 < env->nterms; t2++)
	{
	  term2 = QO_ENV_TERM (env, t2);
	  if (QO_TERM_SELECTIVITY (term1) < QO_TERM_SELECTIVITY (term2))
	    {
	      qo_exchange (term1, term2);
	    }
	}
    }

  for (n = env->nterms; i < n; i++)
    {
      term = QO_ENV_TERM (env, i);
      if (QO_TERM_CLASS (term) == QO_TC_SARG)
	{
	  QO_ASSERT (env, bitset_cardinality (&(QO_TERM_NODES (term))) == 1);
	  qo_node_add_sarg (QO_ENV_NODE (env,
					 bitset_first_member (&(QO_TERM_NODES
								(term)))),
			    term);
	}
    }

  /*
   * Check some invariants.  If something has gone wrong during the
   * discovery phase to violate these invariants, it will mean certain
   * death for later phases, so we need to discover it now while it's
   * convenient.
   */
  for (i = 0, n = env->nedges; i < n; i++)
    {
      edge = QO_ENV_TERM (env, i);
      QO_ASSERT (env, QO_TERM_HEAD (edge) != NULL);
      QO_ASSERT (env, QO_TERM_TAIL (edge) != NULL);

      if (QO_TERM_JOIN_TYPE (edge) != JOIN_INNER
	  && QO_TERM_CLASS (edge) != QO_TC_JOIN)
	{
	  for (j = 0; j < n; j++)
	    {
	      edge2 = QO_ENV_TERM (env, j);
	      if (i != j
		  && bitset_is_equivalent (&(QO_TERM_NODES (edge)),
					   &(QO_TERM_NODES (edge2))))
		{
		  QO_TERM_JOIN_TYPE (edge2) = QO_TERM_JOIN_TYPE (edge);
		}
	    }
	}

      pt_expr = QO_TERM_PT_EXPR (edge);

      /* check for always true transitive join term */
      if (pt_expr
	  && PT_EXPR_INFO_IS_FLAGED (pt_expr, PT_EXPR_INFO_TRANSITIVE))
	{
	  BITSET_CLEAR (direct_nodes);

	  for (j = 0; j < n; j++)
	    {
	      edge2 = QO_ENV_TERM (env, j);
	      if (bitset_intersects
		  (&(QO_TERM_NODES (edge2)), &(QO_TERM_NODES (edge))))
		{
		  bitset_union (&direct_nodes, &(QO_TERM_NODES (edge2)));
		}
	    }			/* for (j = 0; ...) */

	  /* check for direct connected nodes */
	  for (t = bitset_iterate (&direct_nodes, &iter); t != -1;
	       t = bitset_next_member (&iter))
	    {
	      node = QO_ENV_NODE (env, t);
	      if (!QO_NODE_SARGABLE (node))
		{
		  break;	/* give up */
		}
	    }

	  /* found dummy join edge. it is used for planning only */
	  if (t == -1)
	    {
	      QO_TERM_CLASS (edge) = QO_TC_DUMMY_JOIN;

	      /* keep out from m-join edge */
	      QO_TERM_CLEAR_FLAG (edge, QO_TERM_MERGEABLE_EDGE);
	    }
	}
    }

  bitset_delset (&direct_nodes);
}

/*
 * qo_classify_outerjoin_terms () -
 *   return:
 *   env(in):
 *
 * Note:
 * Term Classify Matrix
 * --+-----+---------------------+----------+---------------+------------------
 * NO|Major|Minor                |nidx_self |dep_set        | Classify
 * --+-----+---------------------+----------+---------------+------------------
 * O1|ON   |TC_sarg(on_conn)     |!Right    |!Outer(ex R_on)|TC_sarg(ow TC_dj) 
 * O2|ON   |TC_join              |term_tail=|=on_node       |TC_join(ow O3)
 * O3|ON   |TC_other(n>0,on_conn)|-         |!Outer(ex R_on)|TC_other(ow TC_dj)
 * O4|ON   |TC_other(n==0)       |-         |-              |TC_dj
 * W1|WHERE|TC_sarg              |!Left     |!Right         |TC_sarg(ow TC_aj)
 * W2|WHERE|TC_join              |!Outer    |!Right         |TC_join(ow TC_aj)
 * W3|WHERE|TC_other(n>0)        |!Outer    |!Right         |TC_other(ow TC_aj)
 * W4|WHERE|TC_other(n==0)       |-         |-              |TC_aj
 * --+-----+---------------------+----------+---------------+------------------
 */
static void
qo_classify_outerjoin_terms (QO_ENV * env)
{
  bool is_null_padded, found_left, found_right;
  int n, i, t;
  BITSET_ITERATOR iter;
  QO_NODE *node, *on_node;
  QO_TERM *term;
  int nidx_self;		/* node index of term */
  BITSET dep_set, prev_dep_set;

  for (i = 0; i < env->nterms; i++)
    {
      term = QO_ENV_TERM (env, i);
      QO_ASSERT (env, QO_TERM_LOCATION (term) >= 0);

      if (QO_OUTER_JOIN_TERM (term))
	{
	  break;
	}
    }

  if (i >= env->nterms)
    {
      return;			/* not found outer join term; do nothing */
    }

  bitset_init (&dep_set, env);
  bitset_init (&prev_dep_set, env);

  for (i = 0; i < env->nterms; i++)
    {
      term = QO_ENV_TERM (env, i);
      QO_ASSERT (env, QO_TERM_LOCATION (term) >= 0);

      on_node = QO_ENV_NODE (env, QO_TERM_LOCATION (term));
      QO_ASSERT (env, on_node != NULL);

      if (QO_ON_COND_TERM (term))
	{
	  QO_ASSERT (env, QO_NODE_IDX (on_node) > 0);
	  QO_ASSERT (env, QO_NODE_LOCATION (on_node) > 0);
	  QO_ASSERT (env, QO_NODE_PT_JOIN_TYPE (on_node) != PT_JOIN_NONE);

	  /* is explicit join ON cond */
	  QO_ASSERT (env,
		     QO_TERM_LOCATION (term) == QO_NODE_LOCATION (on_node));

	  if (QO_NODE_PT_JOIN_TYPE (on_node) == PT_JOIN_INNER)
	    {
	      continue;		/* is inner join; no need to classify */
	    }

	  /* is explicit outer-joined ON cond */
	  QO_ASSERT (env, QO_NODE_PT_JOIN_TYPE (on_node) == PT_JOIN_LEFT_OUTER
		     || QO_NODE_PT_JOIN_TYPE (on_node) == PT_JOIN_RIGHT_OUTER
		     || QO_NODE_PT_JOIN_TYPE (on_node) == PT_JOIN_FULL_OUTER);
	}
      else
	{
	  QO_ASSERT (env, QO_NODE_IDX (on_node) == 0);
	  QO_ASSERT (env, QO_NODE_LOCATION (on_node) == 0);
	  QO_ASSERT (env, QO_NODE_PT_JOIN_TYPE (on_node) == PT_JOIN_NONE);
	}

      nidx_self = -1;		/* init */
      for (t = bitset_iterate (&(QO_TERM_NODES (term)), &iter); t != -1;
	   t = bitset_next_member (&iter))
	{
	  node = QO_ENV_NODE (env, t);

	  nidx_self = MAX (nidx_self, QO_NODE_IDX (node));
	}
      QO_ASSERT (env, nidx_self < env->nnodes);

      /* nidx_self is -1 iff term nodes is empty */

      /* STEP 1: check nidx_self
       */
      if (QO_TERM_CLASS (term) == QO_TC_SARG)
	{
	  QO_ASSERT (env, nidx_self >= 0);

	  node = QO_ENV_NODE (env, nidx_self);

	  if (QO_ON_COND_TERM (term))
	    {
	      if (QO_NODE_PT_JOIN_TYPE (node) == PT_JOIN_RIGHT_OUTER
		  || QO_NODE_PT_JOIN_TYPE (node) == PT_JOIN_FULL_OUTER)
		{
		  QO_TERM_CLASS (term) = QO_TC_DURING_JOIN;
		}
	    }
	  else
	    {
	      if (QO_NODE_PT_JOIN_TYPE (node) == PT_JOIN_LEFT_OUTER
		  || QO_NODE_PT_JOIN_TYPE (node) == PT_JOIN_FULL_OUTER)
		{
		  QO_TERM_CLASS (term) = QO_TC_AFTER_JOIN;
		}
	    }
	}
      else if (QO_TERM_CLASS (term) == QO_TC_JOIN)
	{
	  QO_ASSERT (env, nidx_self >= 0);

	  node = QO_ENV_NODE (env, nidx_self);

	  if (QO_ON_COND_TERM (term))
	    {
	      /* is already checked at qo_analyze_term () */
	      QO_ASSERT (env,
			 QO_NODE_IDX (QO_TERM_TAIL (term)) ==
			 QO_NODE_IDX (on_node));
	      QO_ASSERT (env,
			 QO_NODE_IDX (QO_TERM_HEAD (term)) <
			 QO_NODE_IDX (QO_TERM_TAIL (term)));
	    }
	  else
	    {
	      if (QO_NODE_PT_JOIN_TYPE (node) == PT_JOIN_LEFT_OUTER
		  || QO_NODE_PT_JOIN_TYPE (node) == PT_JOIN_RIGHT_OUTER
		  || QO_NODE_PT_JOIN_TYPE (node) == PT_JOIN_FULL_OUTER)
		{
		  QO_TERM_CLASS (term) = QO_TC_AFTER_JOIN;

		  QO_TERM_JOIN_TYPE (term) = NO_JOIN;

		  /* keep out from m-join edge */
		  QO_TERM_CLEAR_FLAG (term, QO_TERM_MERGEABLE_EDGE);
		}
	    }
	}
      else if (QO_TERM_CLASS (term) == QO_TC_OTHER)
	{
	  if (nidx_self >= 0)
	    {
	      node = QO_ENV_NODE (env, nidx_self);

	      if (QO_ON_COND_TERM (term))
		{
		  ;		/* no restriction on nidx_self; go ahead */
		}
	      else
		{
		  if (QO_NODE_PT_JOIN_TYPE (node) == PT_JOIN_LEFT_OUTER
		      || QO_NODE_PT_JOIN_TYPE (node) == PT_JOIN_RIGHT_OUTER
		      || QO_NODE_PT_JOIN_TYPE (node) == PT_JOIN_FULL_OUTER)
		    {
		      QO_TERM_CLASS (term) = QO_TC_AFTER_JOIN;
		    }
		}
	    }
	  else
	    {
	      if (QO_ON_COND_TERM (term))
		{
		  QO_TERM_CLASS (term) = QO_TC_DURING_JOIN;
		}
	      else
		{
		  QO_TERM_CLASS (term) = QO_TC_AFTER_JOIN;
		}
	    }
	}

      if (!(QO_TERM_CLASS (term) == QO_TC_SARG
	    || QO_TERM_CLASS (term) == QO_TC_JOIN
	    || QO_TERM_CLASS (term) == QO_TC_OTHER))
	{
	  continue;		/* no need to classify */
	}

      /* traverse outer-dep nodeset */

      QO_ASSERT (env, nidx_self >= 0);

      is_null_padded = false;	/* init */

      BITSET_CLEAR (dep_set);

      bitset_add (&dep_set, nidx_self);

      do
	{
	  bitset_assign (&prev_dep_set, &dep_set);

	  for (n = 0; n < env->nnodes; n++)
	    {
	      node = QO_ENV_NODE (env, n);

	      if (bitset_intersects (&dep_set,
				     &(QO_NODE_OUTER_DEP_SET (node))))
		{
		  bitset_add (&dep_set, QO_NODE_IDX (node));
		}
	    }
	}
      while (!bitset_is_equivalent (&prev_dep_set, &dep_set));

      /* STEP 2: check iff term nodes are connected with ON cond
       */
      if (QO_ON_COND_TERM (term))
	{
	  if (!BITSET_MEMBER (dep_set, QO_NODE_IDX (on_node)))
	    {
	      is_null_padded = true;
	    }
	}

      /* STEP 3: check outer-dep nodeset join type
       */
      bitset_remove (&dep_set, nidx_self);	/* remove me */
      if (QO_ON_COND_TERM (term))
	{
	  QO_ASSERT (env,
		     QO_TERM_LOCATION (term) == QO_NODE_LOCATION (on_node));

	  if (QO_NODE_PT_JOIN_TYPE (on_node) == PT_JOIN_RIGHT_OUTER)
	    {
	      bitset_remove (&dep_set, QO_NODE_IDX (on_node));	/* remove ON */
	    }
	}

      for (t = bitset_iterate (&dep_set, &iter);
	   t != -1 && !is_null_padded; t = bitset_next_member (&iter))
	{
	  node = QO_ENV_NODE (env, t);

	  if (QO_ON_COND_TERM (term))
	    {
	      if (QO_NODE_LOCATION (node) > QO_NODE_LOCATION (on_node))
		{
		  continue;	/* out of ON cond scope */
		}

	      if (QO_NODE_PT_JOIN_TYPE (node) == PT_JOIN_LEFT_OUTER)
		{
		  is_null_padded = true;
		}
	    }

	  if (QO_NODE_PT_JOIN_TYPE (node) == PT_JOIN_RIGHT_OUTER
	      || QO_NODE_PT_JOIN_TYPE (node) == PT_JOIN_FULL_OUTER)
	    {
	      is_null_padded = true;
	    }
	}

      if (!is_null_padded)
	{
	  continue;		/* go ahead */
	}

      /* at here, found non-sargable node in outer-dep nodeset */

      if (QO_ON_COND_TERM (term))
	{
	  QO_TERM_CLASS (term) = QO_TC_DURING_JOIN;
	}
      else
	{
	  QO_TERM_CLASS (term) = QO_TC_AFTER_JOIN;
	}

      if (QO_TERM_CLASS (term) != QO_TC_JOIN)
	{
	  QO_TERM_JOIN_TYPE (term) = NO_JOIN;

	  /* keep out from m-join edge */
	  QO_TERM_CLEAR_FLAG (term, QO_TERM_MERGEABLE_EDGE);
	}

    }				/* for (i = 0; ...) */

  bitset_delset (&prev_dep_set);
  bitset_delset (&dep_set);
}

/*
 * qo_find_index_terms () - Find the terms which contain the passed segments
 *			    and terms which contain just the passed segments
 *   return:
 *   env(in): The environment used
 *   segsp(in): Passed BITSET of interested segments
 *   index_entry(in):
 */
static void
qo_find_index_terms (QO_ENV * env, BITSET * segsp,
		     QO_INDEX_ENTRY * index_entry)
{
  int t;
  QO_TERM *qo_termp;

  assert (index_entry != NULL);

  BITSET_CLEAR (index_entry->terms);

  /* traverse all terms */
  for (t = 0; t < env->nterms; t++)
    {
      /* get the pointer to QO_TERM structure */
      qo_termp = QO_ENV_TERM (env, t);

      /* Fake terms (e.g., dependency links) won't have pt_expr's associated
         with them. They can't be implemented as indexed sargs, either,
         so don't worry about them here. */
      if (!QO_TERM_PT_EXPR (qo_termp))
	{
	  continue;
	}
      /* 'qo_analyze_term()' function verifies that all indexable
         terms are expression so that they have 'pt_expr' field of type
         PT_EXPR. */

      /* if the segments that give rise to the term are in the given segment
         set */
      if (bitset_intersects (&(QO_TERM_SEGS (qo_termp)), segsp))
	{
	  /* collect this term */
	  bitset_add (&(index_entry->terms), t);
	}
    }				/* for (t = 0; t < env->nterms; t++) */

}

/*
 * qo_find_index_seg_terms () - Find the terms which contain the passed segment.
 *                               Only indexable and SARG terms are included
 *   return:
 *   env(in): The environment used
 *   index_entry(in/out): Index entry
 *   idx(in): Passed idx of an interested segment
 */
static void
qo_find_index_seg_terms (QO_ENV * env, QO_INDEX_ENTRY * index_entry, int idx)
{
  int t;
  QO_TERM *qo_termp;

  /* traverse all terms */
  for (t = 0; t < env->nterms; t++)
    {
      /* get the pointer to QO_TERM structure */
      qo_termp = QO_ENV_TERM (env, t);

      /* ignore this term if it is not marked as indexable by
         'qo_analyze_term()' */
      if (!qo_termp->can_use_index)
	{
	  continue;
	}

      /* Fake terms (e.g., dependency links) won't have pt_expr's associated
         with them. They can't be implemented as indexed sargs, either,
         so don't worry about them here. */
      if (!QO_TERM_PT_EXPR (qo_termp))
	{
	  continue;
	}
      /* 'qo_analyze_term()' function verifies that all indexable
         terms are expression so that they have 'pt_expr' field of type
         PT_EXPR. */

      /* if the term is sarg and the given segment is involed in the
         expression that gives rise to the term */
      if (QO_TERM_CLASS (qo_termp) == QO_TC_SARG
	  && BITSET_MEMBER (QO_TERM_SEGS (qo_termp),
			    index_entry->seg_idxs[idx]))
	{
	  /* check for range list term; RANGE (r1, r2, ...) */
	  if (QO_TERM_IS_FLAGED (qo_termp, QO_TERM_RANGELIST))
	    {
	      if (index_entry->rangelist_seg_idx != -1)
		{
		  continue;	/* already found. give up */
		}

	      /* is the first time */
	      index_entry->rangelist_seg_idx = idx;
	    }

	  /* collect this term */
	  if (QO_TERM_IS_FLAGED (qo_termp, QO_TERM_EQUAL_OP))
	    {
	      bitset_add (&(index_entry->seg_equal_terms[idx]), t);
	    }
	  else
	    {
	      bitset_add (&(index_entry->seg_other_terms[idx]), t);
	    }
	}

    }				/* for (t = 0; ... */

}

/*
 * is_equivalent_indexes () - Compare the two index entries
 *   return: True/False
 *   index1(in): First index entry
 *   index2(in): Second index entry
 *
 * Note: Return true if they are equivalent
 *     and false otherwise.  In order to be equivalent, the index entries
 *     must contain the same segments specified in the same order
 */
static int
is_equivalent_indexes (QO_INDEX_ENTRY * index1, QO_INDEX_ENTRY * index2)
{
  int i, equivalent;

  /*
   *  If the number of segments is different, then the indexes can't
   *  be equivalent (cheap test).
   */
  if (index1->nsegs != index2->nsegs)
    {
      return false;
    }

  /*
   * Now compare the two indexes element by element
   */
  equivalent = true;
  for (i = 0; i < index1->nsegs; i++)
    {
      if ((index1->seg_idxs[i]) != (index2->seg_idxs[i]))
	{
	  equivalent = false;
	  break;
	}
    }

  return equivalent;
}

/*
 * qo_find_matching_index () -
 *   return: int (index of matching index entry, or -1)
 *   index_entry(in): Index entry to match
 *   class_indexes(in): Array of index entries to search
 *
 * Note:
 *     Given a index entry, search the index array looking for a match.
 *     The array index of the matching entry is returned (if found).
 *     A -1 is returned if a matching entry is not found.
 *
 *     Indexes which are already a part of a heirarchical compatible index
 *     list are not considered (these are identifialbe since their next
 *     pointer is non-NULL).
 */
static int
qo_find_matching_index (QO_INDEX_ENTRY * index_entry,
			QO_INDEX * class_indexes)
{
  int i;

  for (i = 0; i < class_indexes->n; i++)
    {
      /*
       *  A matching index is found if the index node is not already a member
       *  of a heirarchical compatible index list (i.e. next pointer is NULL)
       *  and if it matches the passes <index_entry>.
       */
      if (QO_INDEX_INDEX (class_indexes, i)->next == NULL
	  && is_equivalent_indexes (index_entry,
				    QO_INDEX_INDEX (class_indexes, i)))
	{
	  break;
	}
    }

  /*
   *  If a match is found, return its index, otherwise return -1
   */
  if (i < class_indexes->n)
    {
      return i;
    }
  else
    {
      return -1;
    }
}

/*
 * is_index_compatible () -
 *   return: int (True/False)
 *   class_info(in): Class info structure
 *   n(in): Index into class info structure.  This determines the level
 *          in the class hierarchy that we're currently concerned with
 *   index_entry(in): Index entry to match against
 *
 * Note:
 *     This is a recursive function which is used to verify that a
 *     given index entry is compatible across the class hierarchy.
 *     An index entry is compatible if there exists an index definition
 *     on the same sequence of attributes at each level in the class
 *     hierarchy.  If the index entry is compatible, the entry will be
 *     marked as such throughout the hierarchy.
 */
static QO_INDEX_ENTRY *
is_index_compatible (QO_CLASS_INFO * class_info, int n,
		     QO_INDEX_ENTRY * index_entry)
{
  QO_CLASS_INFO_ENTRY *class_entry;
  QO_INDEX *class_indexes;
  QO_INDEX_ENTRY *index;
  int i;

  if (n >= class_info->n)
    {
      return NULL;
    }

  class_entry = &(class_info->info[n]);
  class_indexes = class_entry->index;

  i = qo_find_matching_index (index_entry, class_indexes);
  if (i < 0)
    {
      return NULL;
    }

  index = QO_INDEX_INDEX (class_indexes, i);
  if (n == (class_info->n - 1))
    {
      index->next = NULL;
      return index;
    }
  else
    {
      index->next = is_index_compatible (class_info, n + 1, index);
      if (index->next == NULL)
	{
	  return NULL;
	}
      else
	{
	  return index;
	}
    }

/*  return NULL;*/
}

/*
 * qo_find_index_segs () -
 *   return:
 *   env(in):
 *   consp(in):
 *   nodep(in):
 *   seg_idx(in):
 *   seg_idx_num(in):
 *   nseg_idxp(in):
 *   segs(in):
 */
static bool
qo_find_index_segs (QO_ENV * env,
		    SM_CLASS_CONSTRAINT * consp, QO_NODE * nodep,
		    int *seg_idx, int seg_idx_num, int *nseg_idxp,
		    BITSET * segs)
{
  QO_SEGMENT *segp;
  SM_ATTRIBUTE *attrp;
  BITSET working;
  BITSET_ITERATOR iter;
  int i, iseg;
  bool matched;
  int count_matched_index_attributes = 0;
  int k = 0;

  /* working set; indexed segments */
  bitset_init (&working, env);
  bitset_assign (&working, &(QO_NODE_SEGS (nodep)));

  /* for each attribute of this constraint */
  for (i = 0; *nseg_idxp < seg_idx_num; i++)
    {

      if (consp->func_index_info && i == consp->func_index_info->col_id)
	{
	  matched = false;
	  for (iseg = bitset_iterate (&working, &iter);
	       iseg != -1; iseg = bitset_next_member (&iter))
	    {
	      segp = QO_ENV_SEG (env, iseg);
	      if (QO_SEG_FUNC_INDEX (segp) == true
		  && !intl_identifier_casecmp (QO_SEG_NAME (segp),
					       consp->func_index_info->
					       expr_str))
		{
		  bitset_add (segs, iseg);	/* add the segment to the index segment set */
		  bitset_remove (&working, iseg);	/* remove the segment from the working set */
		  seg_idx[*nseg_idxp] = iseg;	/* remember the order of the index segments */
		  (*nseg_idxp)++;	/* number of index segments, 'seg_idx[]' */
		  /* If we're handling with a multi-column index, then only
		     equality expressions are allowed except for the last
		     matching segment. */
		  bitset_delset (&working);
		  matched = true;
		  count_matched_index_attributes++;
		  break;
		}
	    }
	  if (!matched)
	    {
	      seg_idx[*nseg_idxp] = -1;	/* not found matched segment */
	      (*nseg_idxp)++;	/* number of index segments, 'seg_idx[]' */
	    }			/* if (!matched) */
	}

      if (*nseg_idxp == seg_idx_num)
	{
	  break;
	}
      attrp = consp->attributes[i];

      matched = false;
      /* for each indexed segments of this node, compare the name of the
         segment with the one of the attribute */
      for (iseg = bitset_iterate (&working, &iter);
	   iseg != -1; iseg = bitset_next_member (&iter))
	{

	  segp = QO_ENV_SEG (env, iseg);

	  if (!intl_identifier_casecmp
	      (QO_SEG_NAME (segp), attrp->header.name))
	    {

	      bitset_add (segs, iseg);	/* add the segment to the index segment set */
	      bitset_remove (&working, iseg);	/* remove the segment from the working set */
	      seg_idx[*nseg_idxp] = iseg;	/* remember the order of the index segments */
	      (*nseg_idxp)++;	/* number of index segments, 'seg_idx[]' */
	      /* If we're handling with a multi-column index, then only
	         equality expressions are allowed except for the last
	         matching segment. */
	      matched = true;
	      count_matched_index_attributes++;
	      break;
	    }			/* if (!intl_identifier_casecmp...) */

	}			/* for (iseg = bitset_iterate(&working, &iter); ...) */

      if (!matched)
	{
	  seg_idx[*nseg_idxp] = -1;	/* not found matched segment */
	  (*nseg_idxp)++;	/* number of index segments, 'seg_idx[]' */
	}			/* if (!matched) */

    }				/* for (i = 0; consp->attributes[i]; i++) */

  bitset_delset (&working);

  return count_matched_index_attributes > 0;
  /* this index is feasible to use if at least one attribute of index
     is specified(matched) */
}

/*
 * qo_is_coverage_index () - check if the index cover all query segments
 *   return: bool
 *   env(in): The environment
 *   nodep(in): The node
 *   index_entry(in): The index entry
 */
static bool
qo_is_coverage_index (QO_ENV * env, QO_NODE * nodep,
		      QO_INDEX_ENTRY * index_entry)
{
  int i, j, seg_idx;
  QO_SEGMENT *seg, *fi_seg = NULL;
  bool found;
  QO_CLASS_INFO *class_infop = NULL;
  QO_NODE *seg_nodep = NULL;
  QO_TERM *qo_termp;
  PT_NODE *pt_node;

  if (env == NULL || nodep == NULL || index_entry == NULL)
    {
      return false;
    }

  /*
   * If NO_COVERING_IDX hint is given, we do not generate a plan for
   * covering index scan.
   */
  QO_ASSERT (env, QO_ENV_PT_TREE (env)->node_type == PT_SELECT);
  if (QO_ENV_PT_TREE (env)->node_type == PT_SELECT
      && (QO_ENV_PT_TREE (env)->info.query.q.select.hint
	  & PT_HINT_NO_COVERING_IDX))
    {
      return false;
    }

  for (i = 0; i < index_entry->nsegs; i++)
    {
      seg_idx = (index_entry->seg_idxs[i]);
      if (seg_idx == -1)
	{
	  continue;
	}

      /* We do not use covering index if there is a path expression */
      seg = QO_ENV_SEG (env, seg_idx);
      pt_node = QO_SEG_PT_NODE (seg);
      if (pt_node->node_type == PT_DOT_
	  || (pt_node->node_type == PT_NAME
	      && pt_node->info.name.resolved == NULL))
	{
	  return false;
	}
    }

  for (i = 0; i < env->nsegs; i++)
    {
      seg = QO_ENV_SEG (env, i);

      if (seg == NULL)
	{
	  continue;
	}

      if (QO_SEG_IS_OID_SEG (seg))
	{
	  found = false;
	  for (j = 0; j < env->nterms; j++)
	    {
	      qo_termp = QO_ENV_TERM (env, j);
	      if (BITSET_MEMBER (QO_TERM_SEGS (qo_termp), i))
		{
		  found = true;
		  break;
		}
	    }

	  if (found == false)
	    {
	      continue;
	    }
	}

      /* the segment should belong to the given node */
      seg_nodep = QO_SEG_HEAD (seg);
      if (seg_nodep == NULL || seg_nodep != nodep)
	{
	  continue;
	}

      class_infop = QO_NODE_INFO (seg_nodep);
      if (class_infop == NULL || !(class_infop->info[0].normal_class))
	{
	  return false;
	}
      QO_ASSERT (env, class_infop->n > 0);

      found = false;
      for (j = 0; j < class_infop->n; j++)
	{
	  if (class_infop->info[j].mop == index_entry->class_->mop)
	    {
	      found = true;
	      break;
	    }
	}
      if (!found)
	{
	  continue;
	}

      found = false;
      for (j = 0; j < index_entry->col_num; j++)
	{
	  if (index_entry->seg_idxs[j] == QO_SEG_IDX (seg))
	    {
	      /* if the segment created in respect to the function index info
	       * is covered, we do not use index covering */
	      if (QO_SEG_FUNC_INDEX (seg))
		{
		  return false;
		}
	      found = true;
	      break;
	    }
	}
      if (!found)
	{
	  return false;
	}
    }

  return true;
}

/*
 * qo_get_ils_prefix_length () - get prefix length of loose scan
 *   returns: prefix length or -1 if loose scan not possible
 *   env(in): environment
 *   nodep(in): graph node
 *   index_entry(in): index structure
 */
static int
qo_get_ils_prefix_length (QO_ENV * env, QO_NODE * nodep,
			  QO_INDEX_ENTRY * index_entry)
{
  PT_NODE *tree;
  int prefix_len = 0, i;

  /* check for nulls */
  if (env == NULL || nodep == NULL || index_entry == NULL)
    {
      return 0;
    }

  /* loose scan has no point on single column index */
  if (!QO_ENTRY_MULTI_COL (index_entry))
    {
      return 0;
    }
  assert (index_entry->nsegs > 1);

  tree = env->pt_tree;
  QO_ASSERT (env, tree != NULL);


  if (tree->node_type != PT_SELECT)
    {
      return 0;			/* not applicable */
    }

  /* check hint */
  if (tree->info.query.q.select.hint & PT_HINT_NO_INDEX_LS)
    {
      return 0;			/* disable loose index scan */
    }
  else if ((tree->info.query.q.select.hint & PT_HINT_INDEX_LS)
	   && (QO_NODE_HINT (nodep) & PT_HINT_INDEX_LS))
    {				/* enable loose index scan */
      if (tree->info.query.q.select.hint & PT_HINT_NO_INDEX_SS
	  || !(tree->info.query.q.select.hint & PT_HINT_INDEX_SS)
	  || !(QO_NODE_HINT (nodep) & PT_HINT_INDEX_SS))
	{			/* skip scan is disabled */
	  ;			/* go ahead */
	}
      else
	{			/* skip scan is enabled */
	  return 0;
	}
    }
  else
    {
      return 0;			/* no hint */
    }

  if (PT_SELECT_INFO_IS_FLAGED (tree, PT_SELECT_INFO_DISABLE_LOOSE_SCAN))
    {
      return 0;			/* not applicable */
    }

  if (index_entry->cover_segments
      && (tree->info.query.all_distinct == PT_DISTINCT
	  || PT_SELECT_INFO_IS_FLAGED (tree, PT_SELECT_INFO_HAS_AGG)))
    {
      /* this is a select, index is covering all segments and it's either a
       * DISTINCT query or GROUP BY query with DISTINCT functions
       */

      /* see if only a prefix of the index is used */
      for (i = index_entry->nsegs - 1; i >= 0; i--)
	{
	  if (index_entry->seg_idxs[i] != -1)
	    {
	      prefix_len = i + 1;
	      break;
	    }
	}
    }
  else
    {
      /* not applicable */
      return 0;
    }

  /* no need to continue if no prefix detected */
  if (prefix_len == -1 || prefix_len == index_entry->col_num)
    {
      return 0;
    }

  if (!pt_is_single_tuple (env->parser, env->pt_tree))
    {
      /* if not a single tuple query, then we either have a GROUP BY clause or
       * we don't have any kind of aggregation; in these cases, pure NULL keys
       * qualify iff no terms are used */
      if (bitset_cardinality (&index_entry->terms) <= 0)
	{
	  /* no terms specified, so NULL keys can't be skipped; disable ILS */
	  return 0;
	}
    }

  /* all done */
  return prefix_len;
}

/*
 * qo_is_iss_index () - check if we can use the Index Skip Scan optimization
 *   return: bool
 *   env(in): The environment
 *   nodep(in): The node
 *   index_entry(in): The index entry
 *
 *   Notes: The Index Skip Scan optimization applies when there is no term
 *          involving the first index column, but there are other terms that
 *          refer to the second, third etc columns, and the first column has
 *          few distinct values: in this case, multiple index scans (one for
 *          each value of the first column) can be faster than an index scan.
 */
static bool
qo_is_iss_index (QO_ENV * env, QO_NODE * nodep, QO_INDEX_ENTRY * index_entry)
{
  int i;
  PT_NODE *tree;
  bool first_col_present = false, second_col_present = false;

  if (env == NULL || nodep == NULL || index_entry == NULL)
    {
      return false;
    }

  /* Index skip scan (ISS) candidates:
   *  - have no range or key filter terms for the first column of the index;
   *  - DO have range or key filter terms for at least the second column of
   *    the index (maybe even for further columns, but we are only interested
   *    in the second column right now);
   *  - obviously are multi-column indexes
   *  - not a filter index
   *  - not with HQ
   */

  /* ISS has no meaning on single column indexes */
  if (!QO_ENTRY_MULTI_COL (index_entry))
    {
      return false;
    }
  assert (index_entry->nsegs > 1);

  tree = env->pt_tree;
  QO_ASSERT (env, tree != NULL);

  if (tree->node_type != PT_SELECT)
    {
      return false;
    }

  /* CONNECT BY messes with the terms, so just refuse any index skip scan
   * in this case */
  if (tree->info.query.q.select.connect_by)
    {
      return false;
    }

  /* check hint */
  if (tree->info.query.q.select.hint & PT_HINT_NO_INDEX_SS
      || !(tree->info.query.q.select.hint & PT_HINT_INDEX_SS)
      || !(QO_NODE_HINT (nodep) & PT_HINT_INDEX_SS))
    {
      return false;
    }

  assert (index_entry->constraints != NULL);

  /* do not allow filter ISS with filter index */
  if (index_entry->constraints->filter_predicate != NULL)
    {
      return false;
    }

  /* do not allow filter ISS with function index */
  for (i = 0; i < index_entry->nsegs; i++)
    {
      if ((index_entry->seg_idxs[i] != -1)
	  && (QO_SEG_FUNC_INDEX (QO_ENV_SEG (env, index_entry->seg_idxs[i]))))
	{
	  return false;
	}
    }

  /* First segment index should be missing */
  first_col_present = false;
  if (index_entry->seg_idxs[0] != -1)
    {
      /* it's not enough to have a reference to a segment in seg_idxs[], we
       * must make sure there is an indexable term that uses it: this means
       * either an equal term, or an "other" term that is real (i.e. it is
       * not a full range scan term "invented" by pt_check_orderby to help
       * with generating index covering.
       */

      if (bitset_cardinality (&(index_entry->seg_equal_terms[0])) > 0
	  || bitset_cardinality (&(index_entry->seg_other_terms[0])) > 0)
	{
	  first_col_present = true;
	}
    }

  if (first_col_present)
    {
      return false;
    }

  second_col_present = false;
  if (index_entry->seg_idxs[1] != -1)
    {
      /* it's not enough to have a reference to a segment in seg_idxs[], we
       * must make sure there is an indexable term that uses it: this means
       * either an equal term, or an "other" term that is real (i.e. it is
       * not a full range scan term "invented" by pt_check_orderby to help
       * with generating index covering.
       */

      if (bitset_cardinality (&(index_entry->seg_equal_terms[1])) > 0
	  || bitset_cardinality (&(index_entry->seg_other_terms[1])) > 0)
	{
	  second_col_present = true;
	}
    }

  if (!second_col_present)
    {
      return false;
    }

  /* The first col is missing, and the second col is present and has terms that
   * can be used in a range search.
   * Go ahead and approve the index as a candidate
   * for index skip scanning. We still have a long way ahead of us (use sta-
   * tistics to decide whether index skip scan is the best approach) but we've
   * made the first step.
   */
  return true;
}

/*
 * qo_find_node_indexes () -
 *   return:
 *   env(in): The environment to be updated
 *   nodep(in): The node to be updated
 *
 * Note: Scan the class constraints associated with the node.  If
 *              a match is found between a class constraint and the
 *              segments, then add an QO_INDEX to the node.  A match
 *              occurs when the class constraint attribute are a subset
 *              of the segments.  We currently consider SM_CONSTRAINT_INDEX
 *              and SM_CONSTRAINT_UNIQUE constraint types.
 */
static void
qo_find_node_indexes (QO_ENV * env, QO_NODE * nodep)
{
  int i, j, n, col_num;
  QO_CLASS_INFO *class_infop;
  QO_CLASS_INFO_ENTRY *class_entryp;
  QO_USING_INDEX *uip;
  QO_INDEX *indexp;
  QO_INDEX_ENTRY *index_entryp;
  QO_NODE_INDEX *node_indexp;
  QO_NODE_INDEX_ENTRY *ni_entryp;
  SM_CLASS_CONSTRAINT *constraints, *consp;
  int *seg_idx, seg_idx_arr[NELEMENTS], nseg_idx;
  bool found, is_hint_use, is_hint_ignore, is_hint_force, is_hint_all_except;
  BITSET index_segs, index_terms;
  bool special_index_scan = false;

  /* information of classes underlying this node */
  class_infop = QO_NODE_INFO (nodep);

  if (class_infop->n <= 0)
    {
      return;			/* no classes, nothing to do process */
    }

  if (PT_SPEC_SPECIAL_INDEX_SCAN (QO_NODE_ENTITY_SPEC (nodep)))
    {
      if (QO_NODE_USING_INDEX (nodep) == NULL)
	{
	  assert (0);
	  return;
	}
      special_index_scan = true;
    }

  /* for each class in the hierarchy, search the class constraint cache
     looking for applicable indexes(UNIQUE and INDEX constraint) */
  for (i = 0; i < class_infop->n; i++)
    {

      /* class information entry */
      class_entryp = &(class_infop->info[i]);
      /* get constraints of the class */
      constraints = sm_class_constraints (class_entryp->mop);

      /* count the number of INDEX and UNIQUE constraints contained in this
         class */
      n = 0;
      for (consp = constraints; consp; consp = consp->next)
	{
	  if (SM_IS_CONSTRAINT_INDEX_FAMILY (consp->type))
	    {
	      if (consp->filter_predicate != NULL
		  && QO_NODE_USING_INDEX (nodep) == NULL)
		{
		  continue;
		}
	      n++;
	    }
	}
      /* allocate room for the constraint indexes */
      /* we don't have apriori knowledge about which constraints will be
         applied, so allocate room for all of them */
      /* qo_alloc_index(env, n) will allocate QO_INDEX structure and
         QO_INDEX_ENTRY structure array */
      indexp = class_entryp->index = qo_alloc_index (env, n);
      if (indexp == NULL)
	{
	  return;
	}

      indexp->n = 0;

      /* for each constraint of the class */
      for (consp = constraints; consp; consp = consp->next)
	{

	  if (!SM_IS_CONSTRAINT_INDEX_FAMILY (consp->type))
	    {
	      continue;		/* neither INDEX nor UNIQUE constraint, skip */
	    }

	  if (consp->filter_predicate != NULL
	      && QO_NODE_USING_INDEX (nodep) == NULL)
	    {
	      continue;
	    }

	  uip = QO_NODE_USING_INDEX (nodep);
	  j = -1;
	  if (uip)
	    {
	      if (QO_UI_N (uip) == 0)
		{
		  /* USING INDEX NONE case, skip */
		  continue;
		}

	      /* search USING INDEX list */
	      found = false;
	      is_hint_use = is_hint_force = is_hint_ignore = false;
	      is_hint_all_except = false;

	      /* gather information */
	      for (j = 0; j < QO_UI_N (uip); j++)
		{
		  switch (QO_UI_FORCE (uip, j))
		    {
		    case PT_IDX_HINT_USE:
		      is_hint_use = true;
		      break;
		    case PT_IDX_HINT_FORCE:
		      is_hint_force = true;
		      break;
		    case PT_IDX_HINT_IGNORE:
		      is_hint_ignore = true;
		      break;
		    case PT_IDX_HINT_ALL_EXCEPT:
		      is_hint_all_except = true;
		      break;
		    }
		}

	      /* search for index in using_index clause */
	      for (j = 0; j < QO_UI_N (uip); j++)
		{
		  if (!intl_identifier_casecmp
		      (consp->name, QO_UI_INDEX (uip, j)))
		    {
		      found = true;
		      break;
		    }
		}

	      if (QO_UI_FORCE (uip, 0) == PT_IDX_HINT_ALL_EXCEPT)
		{
		  /* USING INDEX ALL EXCEPT case */
		  if (found)
		    {
		      /* this constraint(index) is specified in
		         USING INDEX ALL EXCEPT clause; do not use it */
		      continue;
		    }
		  if (consp->filter_predicate != NULL)
		    {
		      /* don't use filter indexes unless specified */
		      continue;
		    }
		  j = -1;
		}
	      else if (is_hint_force || is_hint_use)
		{
		  /* if any indexes are forced or used, use them */
		  if (!found || QO_UI_FORCE (uip, j) == PT_IDX_HINT_IGNORE)
		    {
		      continue;
		    }
		}
	      else
		{
		  /* no indexes are used or forced, only ignored */
		  if (found)
		    {
		      /* found as ignored; don't use */
		      continue;
		    }
		  if (consp->filter_predicate != NULL)
		    {
		      /* don't use filter indexes unless specified */
		      continue;
		    }
		  j = -1;
		}
	    }

	  bitset_init (&index_segs, env);
	  bitset_init (&index_terms, env);
	  nseg_idx = 0;

	  /* count the number of columns on this constraint */
	  for (col_num = 0; consp->attributes[col_num]; col_num++)
	    {
	      ;
	    }
	  if (consp->func_index_info)
	    {
	      col_num = consp->func_index_info->attr_index_start + 1;
	    }

	  if (col_num <= NELEMENTS)
	    {
	      seg_idx = seg_idx_arr;
	    }
	  else
	    {
	      /* allocate seg_idx */
	      seg_idx = (int *) malloc (sizeof (int) * col_num);
	      if (seg_idx == NULL)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_OUT_OF_VIRTUAL_MEMORY, 1,
			  sizeof (int) * col_num);
		  /* cannot allocate seg_idx, use seg_idx_arr instead. */
		  seg_idx = seg_idx_arr;
		  col_num = NELEMENTS;
		}
	    }

	  /* find indexed segments into 'seg_idx[]' */
	  found = qo_find_index_segs (env, consp, nodep,
				      seg_idx, col_num, &nseg_idx,
				      &index_segs);
	  /* 'seg_idx[nseg_idx]' array contains index no.(idx) of the segments
	   * which are found and applicable to this index(constraint) as
	   * search key in the order of the index key attribute. For example,
	   * if the index consists of attributes 'b' and 'a', and the given
	   * segments of the node are 'a(1)', 'b(2)' and 'c(3)', then the
	   * result of 'seg_idx[]' will be '{ 2, 1, -1 }'. The value -1 in
	   * 'seg_idx[] array means that no segment is specified.
	   */
	  /* If key information is required, no index segments will be found,
	   * but index scan has to be forced.
	   */
	  if (found == true || special_index_scan == true)
	    {
	      /* if applicable index was found, add it to the node */

	      /* fill in QO_INDEX_ENTRY structure */
	      index_entryp = QO_INDEX_INDEX (indexp, indexp->n);
	      index_entryp->nsegs = nseg_idx;
	      index_entryp->seg_idxs = NULL;
	      index_entryp->rangelist_seg_idx = -1;
	      index_entryp->seg_equal_terms = NULL;
	      index_entryp->seg_other_terms = NULL;
	      if (index_entryp->nsegs > 0)
		{
		  size_t size;

		  size = sizeof (int) * index_entryp->nsegs;
		  index_entryp->seg_idxs = (int *) malloc (size);
		  if (index_entryp->seg_idxs == NULL)
		    {
		      if (seg_idx != seg_idx_arr)
			{
			  free_and_init (seg_idx);
			}
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			      ER_OUT_OF_VIRTUAL_MEMORY, 1, size);
		      return;
		    }

		  size = sizeof (BITSET) * index_entryp->nsegs;
		  index_entryp->seg_equal_terms = (BITSET *) malloc (size);
		  if (index_entryp->seg_equal_terms == NULL)
		    {
		      free_and_init (index_entryp->seg_idxs);
		      if (seg_idx != seg_idx_arr)
			{
			  free_and_init (seg_idx);
			}
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			      ER_OUT_OF_VIRTUAL_MEMORY, 1, size);
		      return;
		    }
		  index_entryp->seg_other_terms = (BITSET *) malloc (size);
		  if (index_entryp->seg_other_terms == NULL)
		    {
		      free_and_init (index_entryp->seg_equal_terms);
		      free_and_init (index_entryp->seg_idxs);
		      if (seg_idx != seg_idx_arr)
			{
			  free_and_init (seg_idx);
			}
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			      ER_OUT_OF_VIRTUAL_MEMORY, 1, size);
		      return;
		    }
		}
	      index_entryp->class_ = class_entryp;
	      /* j == -1 iff no USING INDEX or USING INDEX ALL EXCEPT */
	      index_entryp->force = (j == -1) ? 0 : QO_UI_FORCE (uip, j);
	      index_entryp->col_num = col_num;
	      index_entryp->key_type = NULL;
	      index_entryp->constraints = consp;

	      /* set key limits */
	      index_entryp->key_limit =
		(j == -1) ? NULL : QO_UI_KEYLIMIT (uip, j);

	      /* assign seg_idx[] and seg_terms[] */
	      for (j = 0; j < index_entryp->nsegs; j++)
		{
		  bitset_init (&(index_entryp->seg_equal_terms[j]), env);
		  bitset_init (&(index_entryp->seg_other_terms[j]), env);
		  index_entryp->seg_idxs[j] = seg_idx[j];
		  if (index_entryp->seg_idxs[j] != -1)
		    {
		      qo_find_index_seg_terms (env, index_entryp, j);
		    }
		}
	      qo_find_index_terms (env, &index_segs, index_entryp);

	      index_entryp->cover_segments =
		qo_is_coverage_index (env, nodep, index_entryp);

	      index_entryp->is_iss_candidate =
		qo_is_iss_index (env, nodep, index_entryp);

	      /* disable loose scan if skip scan is possible */
	      if (index_entryp->is_iss_candidate == true)
		{
		  index_entryp->ils_prefix_len = 0;
		}
	      else
		{
		  index_entryp->ils_prefix_len =
		    qo_get_ils_prefix_length (env, nodep, index_entryp);
		}

	      index_entryp->statistics_attribute_name = NULL;
	      index_entryp->is_func_index = false;

	      if (index_entryp->col_num > 0)
		{
		  const char *temp_name = NULL;

		  if (consp->func_index_info
		      && consp->func_index_info->col_id == 0)
		    {
		      index_entryp->is_func_index = true;
		    }
		  if (!index_entryp->is_func_index &&
		      consp->attributes && consp->attributes[0])
		    {
		      temp_name = consp->attributes[0]->header.name;
		      if (temp_name)
			{
			  int len = strlen (temp_name) + 1;
			  index_entryp->statistics_attribute_name =
			    (char *) malloc (sizeof (char) * len);
			  if (index_entryp->statistics_attribute_name == NULL)
			    {
			      if (seg_idx != seg_idx_arr)
				{
				  free_and_init (seg_idx);
				}
			      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
				      ER_OUT_OF_VIRTUAL_MEMORY, 1,
				      sizeof (char) * len);
			      return;
			    }
			  strcpy (index_entryp->statistics_attribute_name,
				  temp_name);
			}
		    }
		}

	      (indexp->n)++;

	    }

	  bitset_delset (&(index_segs));
	  bitset_delset (&(index_terms));
	  if (seg_idx != seg_idx_arr)
	    {
	      free_and_init (seg_idx);
	    }

	}			/* for (consp = constraintp; consp; consp = consp->next) */

    }				/* for (i = 0; i < class_infop->n; i++) */
  /* class_infop->n >= 1 */

  /* find and mark indexes which are compatible across class hierarchy */

  indexp = class_infop->info[0].index;

  /* allocate room for the compatible heirarchical indexex */
  /* We'll go ahead and allocate room for each index in the top level
     class. This is the worst case situation and it simplifies the code
     a bit. */
  /* Malloc and Init a QO_INDEX struct with n entries. */
  node_indexp =
    QO_NODE_INDEXES (nodep) =
    (QO_NODE_INDEX *) malloc (SIZEOF_NODE_INDEX (indexp->n));

  if (node_indexp == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      SIZEOF_NODE_INDEX (indexp->n));
      return;
    }

  memset (node_indexp, 0, SIZEOF_NODE_INDEX (indexp->n));

  QO_NI_N (node_indexp) = 0;

  /* if we don`t have any indexes to process, we're through
     if there is only one, then make sure that the head pointer points to it
     if there are more than one, we also need to construct a linked list
     of compatible indexes by recursively searching down the hierarchy */
  for (i = 0; i < indexp->n; i++)
    {
      index_entryp = QO_INDEX_INDEX (indexp, i);
      /* get compatible(equivalent) index of the next class
         'index_entryp->next' points to it */
      index_entryp->next = is_index_compatible (class_infop, 1, index_entryp);

      if ((index_entryp->next != NULL) || (class_infop->n == 1))
	{
	  /* fill in QO_NODE_INDEX_ENTRY structure */
	  ni_entryp = QO_NI_ENTRY (node_indexp, QO_NI_N (node_indexp));
	  /* number of classes on the list */
	  (ni_entryp)->n = class_infop->n;
	  /* link QO_INDEX_ENTRY struture to QO_NODE_INDEX_ENTRY strucure */
	  (ni_entryp)->head = index_entryp;
	  QO_NI_N (node_indexp)++;
	}
    }				/* for (i = 0; i < indexp->n; i++) */

}

/*
 * qo_discover_indexes () -
 *   return: nothing
 *   env(in): The environment to be updated
 *
 * Note: Study each term to finish determination of whether it can use
 *	an index.  qo_analyze_term() already determined whether each
 *	term qualifies structurally, and qo_get_class_info() has
 *	determined all of the indexes that are available, so all we
 *	have to do here is combine those two pieces of information.
 */
static void
qo_discover_indexes (QO_ENV * env)
{
  int i, j, k, b, n, s;
  bool found;
  BITSET_ITERATOR bi;
  QO_NODE_INDEX *node_indexp;
  QO_NODE_INDEX_ENTRY *ni_entryp;
  QO_INDEX_ENTRY *index_entryp;
  QO_TERM *termp;
  QO_NODE *nodep;
  QO_SEGMENT *segp;

  /* iterate over all nodes and find indexes for each node */
  for (i = 0; i < env->nnodes; i++)
    {

      nodep = QO_ENV_NODE (env, i);
      if (nodep->info)
	{
	  /* find indexed segments that belong to this node and get indexes
	   * that apply to indexed segments.
	   * Note that a scan for record information or page informations
	   * should follow, there is no need to check for index (a sequential
	   * scan is needed).
	   */
	  if (!PT_IS_SPEC_FLAG_SET (QO_NODE_ENTITY_SPEC (nodep),
				    (PT_SPEC_FLAG_RECORD_INFO_SCAN
				     | PT_SPEC_FLAG_PAGE_INFO_SCAN)))
	    {
	      qo_find_node_indexes (env, nodep);
	      /* collect statistic information on discovered indexes */
	      qo_get_index_info (env, nodep);
	    }
	  else
	    {
	      QO_NODE_INDEXES (nodep) = NULL;
	    }
	}
      else
	{
	  /* If the 'info' of node is NULL, then this is probably a derived
	     table. Without the info, we don't have class information to
	     work with so we really can't do much so just skip the node. */
	  QO_NODE_INDEXES (nodep) = NULL;	/* this node will not use a index */
	}

    }				/* for (n = 0; n < env->nnodes; n++) */

  /* for each terms, look indexed segements and filter out the segments
     which don't actually contain any indexes */
  for (i = 0; i < env->nterms; i++)
    {

      termp = QO_ENV_TERM (env, i);

      /* before, 'index_seg[]' has all possible indexed segments, that is
         assigned at 'qo_analyze_term()' */
      /* for all 'term.index_seg[]', examine if it really has index or not */
      k = 0;
      for (j = 0; j < termp->can_use_index; j++)
	{

	  segp = termp->index_seg[j];

	  found = false;	/* init */
	  /* for each nodes, do traverse */
	  for (b = bitset_iterate (&(QO_TERM_NODES (termp)), &bi);
	       b != -1 && !found; b = bitset_next_member (&bi))
	    {

	      nodep = QO_ENV_NODE (env, b);

	      /* pointer to QO_NODE_INDEX structure of QO_NODE */
	      node_indexp = QO_NODE_INDEXES (nodep);
	      if (node_indexp == NULL)
		{
		  /* node has not any index
		   * skip and go ahead
		   */
		  continue;
		}

	      /* for each index list rooted at the node */
	      for (n = 0, ni_entryp = QO_NI_ENTRY (node_indexp, 0);
		   n < QO_NI_N (node_indexp) && !found; n++, ni_entryp++)
		{

		  index_entryp = (ni_entryp)->head;

		  /* for each segments constrained by the index */
		  for (s = 0; s < index_entryp->nsegs && !found; s++)
		    {
		      if (QO_SEG_IDX (segp) == (index_entryp->seg_idxs[s]))
			{
			  /* found specified seg
			   * stop traverse
			   */
			  found = true;

			  /* record term at segment structure */
			  bitset_add (&(QO_SEG_INDEX_TERMS (segp)),
				      QO_TERM_IDX (termp));
			  /* indexed segment in 'index_seg[]' array */
			  termp->index_seg[k++] = termp->index_seg[j];
			}
		    }		/* for (s = 0 ... ) */

		}		/* for (n = 0 ... ) */

	    }			/* for (b = ... ) */

	}			/* for (j = 0 ... ) */

      termp->can_use_index = k;	/* dimension of 'index_seg[]' */
      /* clear unused, discarded 'index_seg[]' entries */
      while (k < j)
	{
	  termp->index_seg[k++] = NULL;
	}

    }				/* for (i = 0; i < env->nterms; i++) */

}

/*
 * qo_discover_partitions () -
 *   return:
 *   env(in):
 */
static void
qo_discover_partitions (QO_ENV * env)
{
  int N = env->nnodes;		/* The number of nodes in the join graph */
  int E = env->nedges;		/* The number of edges (in the strict sense) */
  int P = 0;			/* The number of partitions */
  int e, n, p;

  int *buddy;			/* buddy[i] is the index of another node in the same partition as i */
  int *partition;		/* partition[i] is the index of the partition to which node i belongs */
  BITSET_ITERATOR bi;
  int hi, ti, r;
  QO_TERM *term;
  QO_PARTITION *part;
  int M_offset, join_info_size;
  int rel_idx;

  buddy = NULL;
  if (N > 0)
    {
      buddy = (int *) malloc (sizeof (int) * (2 * N));
      if (buddy == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, sizeof (int) * (2 * N));
	  return;
	}
    }
  else
    {
      return;
    }

  partition = buddy + N;

  /*
   * This code assumes that there will be no ALLOCATE failures; if
   * there are, the buddy array will be lost.
   */

  for (n = 0; n < N; ++n)
    {
      buddy[n] = -1;
      partition[n] = -1;
    }

  for (e = 0; e < E; ++e)
    {
      term = QO_ENV_TERM (env, e);

      /*
       * Identify one of the nodes in this term, and run to the top of
       * the tree in which it resides.
       */
      hi = bitset_iterate (&(QO_TERM_NODES (term)), &bi);
      while (hi != -1 && buddy[hi] != -1)
	{
	  hi = buddy[hi];
	}

      /*
       * Now buddy up all of the other nodes encompassed by this term.
       */
      while ((ti = bitset_next_member (&bi)) != -1)
	{
	  /*
	   * Run to the top of the tree in which node[ti] resides.
	   */
	  while (buddy[ti] != -1)
	    {
	      ti = buddy[ti];
	    }
	  /*
	   * Join the two trees together.
	   */
	  if (hi != ti)
	    {
	      buddy[hi] = ti;
	    }
	}
    }

  /*
   * Now assign the actual partitions.
   */
  for (n = 0; n < N; ++n)
    {
      if (partition[n] == -1)
	{
	  r = n;
	  /*
	   * Find the root of the tree to which node[n] belongs.
	   */
	  while (buddy[r] != -1)
	    {
	      r = buddy[r];
	    }
	  /*
	   * If a partition hasn't already been assigned for this tree,
	   * assign one now.
	   */
	  if (partition[r] == -1)
	    {
	      QO_PARTITION *part = QO_ENV_PARTITION (env, P);
	      QO_NODE *node = QO_ENV_NODE (env, r);
	      qo_partition_init (env, part, P);
	      bitset_add (&(QO_PARTITION_NODES (part)), r);
	      bitset_union (&(QO_PARTITION_DEPENDENCIES (part)),
			    &(QO_NODE_DEP_SET (node)));
	      QO_NODE_PARTITION (node) = part;
	      partition[r] = P;
	      P++;
	    }
	  /*
	   * Now add node[n] to that partition.
	   */
	  if (n != r)
	    {
	      QO_PARTITION *part = QO_ENV_PARTITION (env, partition[r]);
	      QO_NODE *node = QO_ENV_NODE (env, n);
	      partition[n] = partition[r];
	      bitset_add (&(QO_PARTITION_NODES (part)), n);
	      bitset_union (&(QO_PARTITION_DEPENDENCIES (part)),
			    &(QO_NODE_DEP_SET (node)));
	      QO_NODE_PARTITION (node) = part;
	    }
	}
    }

  /*
   * Now go build the edge sets that correspond to each partition,
   * i.e., the set of edges that connect the nodes in each partition.
   */
  M_offset = 0;			/* init */
  for (p = 0; p < P; ++p)
    {
      part = QO_ENV_PARTITION (env, p);

      for (e = 0; e < E; ++e)
	{
	  QO_TERM *edge = QO_ENV_TERM (env, e);

	  if (bitset_subset
	      (&(QO_PARTITION_NODES (part)), &(QO_TERM_NODES (edge)))
	      && !bitset_is_empty (&(QO_TERM_NODES (edge))))
	    {
	      bitset_add (&(QO_PARTITION_EDGES (part)), e);
	    }
	}
      /* alloc size check
       * 2: for signed max int. 2**30 is positive, 2**31 is negative
       * LOG2_SIZEOF_POINTER: log2(sizeof(QO_INFO *))
       */
      if (bitset_cardinality (&(QO_PARTITION_NODES (part)))
	  > _WORDSIZE - 2 - LOG2_SIZEOF_POINTER)
	{
	  if (buddy)
	    {
	      free_and_init (buddy);
	    }
	  QO_ABORT (env);
	}

      /* set the starting point the join_info vector that
       * correspond to each partition.
       */
      if (p > 0)
	{
	  QO_PARTITION_M_OFFSET (part) = M_offset;
	}
      join_info_size = QO_JOIN_INFO_SIZE (part);
      if (INT_MAX - M_offset * sizeof (QO_INFO *)
	  < join_info_size * sizeof (QO_INFO *))
	{
	  if (buddy)
	    {
	      free_and_init (buddy);
	    }
	  QO_ABORT (env);
	}
      M_offset += join_info_size;

      /* set the relative id of nodes in the partition
       */
      rel_idx = 0;		/* init */
      for (hi = bitset_iterate (&(QO_PARTITION_NODES (part)), &bi); hi != -1;
	   hi = bitset_next_member (&bi))
	{
	  QO_NODE_REL_IDX (QO_ENV_NODE (env, hi)) = rel_idx;
	  rel_idx++;
	}
    }

  env->npartitions = P;

  if (buddy)
    {
      free_and_init (buddy);
    }
}

/*
 * qo_assign_eq_classes () -
 *   return:
 *   env(in):
 */
static void
qo_assign_eq_classes (QO_ENV * env)
{
  int i;
  QO_EQCLASS **eq_map;
  BITSET segs;

  bitset_init (&segs, env);

  eq_map = NULL;
  if (env->nsegs > 0)
    {
      eq_map = (QO_EQCLASS **) malloc (sizeof (QO_EQCLASS *) * env->nsegs);
      if (eq_map == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, sizeof (QO_EQCLASS *) * env->nsegs);
	  return;
	}
    }

  for (i = 0; i < env->nsegs; ++i)
    {
      eq_map[i] = NULL;
    }

  for (i = 0; i < env->nedges; i++)
    {
      QO_TERM *term;

      term = QO_ENV_TERM (env, i);
      if (QO_TERM_NOMINAL_SEG (term))
	{
	  bitset_union (&segs, &(QO_TERM_SEGS (term)));
	}
    }

  /*
   * Now examine each segment and see if it should be assigned to an
   * equivalence class.
   */
  for (i = 0; i < env->nsegs; ++i)
    {
      if (!BITSET_MEMBER (segs, i))
	{
	  continue;
	}

      if (eq_map[i] == NULL)
	{
	  QO_SEGMENT *root, *seg;
	  seg = QO_ENV_SEG (env, i);

	  /*
	   * Find the root of the tree in which this segment resides.
	   */
	  for (root = seg; QO_SEG_EQ_ROOT (root);
	       root = QO_SEG_EQ_ROOT (root))
	    {
	      ;
	    }
	  /*
	   * Assign a new EqClass to that root if one hasn't already
	   * been assigned.
	   */
	  if (eq_map[QO_SEG_IDX (root)] == NULL)
	    {
	      qo_eqclass_add ((eq_map[QO_SEG_IDX (root)] =
			       qo_eqclass_new (env)), root);
	    }
	  /*
	   * Now add the original segment to the same equivalence
	   * class.
	   */
	  if (root != seg)
	    {
	      qo_eqclass_add (eq_map[QO_SEG_IDX (root)], seg);
	    }
	  eq_map[i] = eq_map[QO_SEG_IDX (root)];
	}
    }

  bitset_delset (&segs);
  if (eq_map)
    {
      free_and_init (eq_map);
    }

  /*
   * Now squirrel away the eqclass info for each term so that we don't
   * have to keep recomputing it when searching the plan space.  Note
   * that this not really meaningful unless all of the segments in the
   * term are in the same equivalence class as the first one.  However,
   * since we're only supposed to use this information when examining
   * join or path terms, and since that condition holds for those
   * terms, this should be ok.
   */
  for (i = 0; i < env->nedges; i++)
    {
      QO_TERM *term = QO_ENV_TERM (env, i);
      QO_SEGMENT *seg = QO_TERM_NOMINAL_SEG (term);

      if (seg)
	{
	  QO_TERM_EQCLASS (term) = QO_SEG_EQCLASS (seg);
	}
      else if (QO_TERM_IS_FLAGED (term, QO_TERM_MERGEABLE_EDGE))
	{
	  QO_TERM_EQCLASS (term) = qo_eqclass_new (env);
	  QO_EQCLASS_TERM (QO_TERM_EQCLASS (term)) = term;
	}
      else
	{
	  QO_TERM_EQCLASS (term) = QO_UNORDERED;
	}
    }
}

/*
 * qo_env_dump () -
 *   return:
 *   env(in):
 *   f(in):
 */
static void
qo_env_dump (QO_ENV * env, FILE * f)
{
  int i;

  if (f == NULL)
    {
      f = stdout;
    }

  if (env->nsegs)
    {
      fprintf (f, "Join graph segments (f indicates final):\n");
      for (i = 0; i < env->nsegs; ++i)
	{
	  QO_SEGMENT *seg = QO_ENV_SEG (env, i);
	  int extra_info = 0;

	  fprintf (f, "seg[%d]: ", i);
	  qo_seg_fprint (seg, f);

	  PUT_FLAG (BITSET_MEMBER (env->final_segs, i), "f");
	  /*
	   * Put extra flags here.
	   */
	  fputs (extra_info ? ")\n" : "\n", f);
	}
    }

  if (env->nnodes)
    {
      fprintf (f, "Join graph nodes:\n");
      for (i = 0; i < env->nnodes; ++i)
	{
	  QO_NODE *node = QO_ENV_NODE (env, i);
	  fprintf (f, "node[%d]: ", i);
	  qo_node_dump (node, f);
	  fputs ("\n", f);
	}
    }

  if (env->neqclasses)
    {
      fprintf (f, "Join graph equivalence classes:\n");
      for (i = 0; i < env->neqclasses; ++i)
	{
	  fprintf (f, "eqclass[%d]: ", i);
	  qo_eqclass_dump (QO_ENV_EQCLASS (env, i), f);
	  fputs ("\n", f);
	}
    }

  /*
   * Notice that we blow off printing the edge structures themselves,
   * and just print the term that gives rise to the edge.  Also notice
   * the way edges and terms are separated: we don't reset the counter
   * for non-edge terms.
   */
  if (env->nedges)
    {
      fputs ("Join graph edges:\n", f);
      for (i = 0; i < env->nedges; ++i)
	{
	  fprintf (f, "term[%d]: ", i);
	  qo_term_dump (QO_ENV_TERM (env, i), f);
	  fputs ("\n", f);
	}
    }

  if (env->nterms - env->nedges)
    {
      fputs ("Join graph terms:\n", f);
      for (i = env->nedges; i < env->nterms; ++i)
	{
	  fprintf (f, "term[%d]: ", i);
	  qo_term_dump (QO_ENV_TERM (env, i), f);
	  fputs ("\n", f);
	}
    }

  if (env->nsubqueries)
    {
      fputs ("Join graph subqueries:\n", f);
      for (i = 0; i < env->nsubqueries; ++i)
	{
	  fprintf (f, "subquery[%d]: ", i);
	  qo_subquery_dump (env, &env->subqueries[i], f);
	  fputs ("\n", f);
	}
    }

  if (env->npartitions > 1)
    {
      fputs ("Join graph partitions:\n", f);
      for (i = 0; i < env->npartitions; ++i)
	{
	  fprintf (f, "partition[%d]: ", i);
	  qo_partition_dump (QO_ENV_PARTITION (env, i), f);
	  fputs ("\n", f);
	}
    }

  fflush (f);
}

/*
 * qo_node_clear () -
 *   return:
 *   env(in):
 *   idx(in):
 */
static void
qo_node_clear (QO_ENV * env, int idx)
{
  QO_NODE *node = QO_ENV_NODE (env, idx);

  QO_NODE_ENV (node) = env;
  QO_NODE_ENTITY_SPEC (node) = NULL;
  QO_NODE_PARTITION (node) = NULL;
  QO_NODE_OID_SEG (node) = NULL;
  QO_NODE_SELECTIVITY (node) = 1.0;
  QO_NODE_IDX (node) = idx;
  QO_NODE_INFO (node) = NULL;
  QO_NODE_NCARD (node) = 0;
  QO_NODE_TCARD (node) = 0;
  QO_NODE_NAME (node) = NULL;
  QO_NODE_INDEXES (node) = NULL;
  QO_NODE_USING_INDEX (node) = NULL;

  bitset_init (&(QO_NODE_EQCLASSES (node)), env);
  bitset_init (&(QO_NODE_SARGS (node)), env);
  bitset_init (&(QO_NODE_DEP_SET (node)), env);
  bitset_init (&(QO_NODE_SUBQUERIES (node)), env);
  bitset_init (&(QO_NODE_SEGS (node)), env);
  bitset_init (&(QO_NODE_OUTER_DEP_SET (node)), env);

  QO_NODE_HINT (node) = PT_HINT_NONE;
}

/*
 * qo_node_free () -
 *   return:
 *   node(in):
 */
static void
qo_node_free (QO_NODE * node)
{
  bitset_delset (&(QO_NODE_EQCLASSES (node)));
  bitset_delset (&(QO_NODE_SARGS (node)));
  bitset_delset (&(QO_NODE_DEP_SET (node)));
  bitset_delset (&(QO_NODE_SEGS (node)));
  bitset_delset (&(QO_NODE_SUBQUERIES (node)));
  bitset_delset (&(QO_NODE_OUTER_DEP_SET (node)));
  qo_free_class_info (QO_NODE_ENV (node), QO_NODE_INFO (node));
  if (QO_NODE_INDEXES (node))
    {
      qo_free_node_index_info (QO_NODE_ENV (node), QO_NODE_INDEXES (node));
    }
  if (QO_NODE_USING_INDEX (node))
    {
      free_and_init (QO_NODE_USING_INDEX (node));
    }
}

/*
 * qo_node_add_sarg () -
 *   return:
 *   node(in):
 *   sarg(in):
 */
static void
qo_node_add_sarg (QO_NODE * node, QO_TERM * sarg)
{
  double sel_limit;

  bitset_add (&(QO_NODE_SARGS (node)), QO_TERM_IDX (sarg));
  QO_NODE_SELECTIVITY (node) *= QO_TERM_SELECTIVITY (sarg);
  sel_limit =
    (QO_NODE_NCARD (node) == 0) ? 0 : (1.0 / (double) QO_NODE_NCARD (node));
  if (QO_NODE_SELECTIVITY (node) < sel_limit)
    {
      QO_NODE_SELECTIVITY (node) = sel_limit;
    }
}

/*
 * qo_node_fprint () -
 *   return:
 *   node(in):
 *   f(in):
 */
void
qo_node_fprint (QO_NODE * node, FILE * f)
{
  if (QO_NODE_NAME (node))
    {
      fprintf (f, "%s", QO_NODE_NAME (node));
    }
  fprintf (f, " node[%d]", QO_NODE_IDX (node));
}

/*
 * qo_node_dump () -
 *   return:
 *   node(in):
 *   f(in):
 */
static void
qo_node_dump (QO_NODE * node, FILE * f)
{
  int i, n = 1;
  const char *name;
  PT_NODE *entity;

  entity = QO_NODE_ENTITY_SPEC (node);

  if (QO_NODE_INFO (node))
    {
      n = QO_NODE_INFO_N (node);
      if (n > 1)
	{
	  fprintf (f, "(");	/* left paren */
	}
      for (i = 0; i < n; i++)
	{
	  name = QO_NODE_INFO (node)->info[i].name;
	  /* check for class OID reference spec
	   * for example: 'class x'
	   *   SELECT class_meth(class x, x.i) FROM x, class x
	   */
	  if (i == 0)
	    {			/* the first entity */
	      fprintf (f,
		       (entity->info.spec.meta_class == PT_META_CLASS
			? "class %s" : "%s"), (name ? name : "(anon)"));
	    }
	  else
	    {
	      fprintf (f,
		       (entity->info.spec.meta_class == PT_META_CLASS
			? ", class %s" : ", %s"), (name ? name : "(anon)"));
	    }
	}
      fprintf (f, "%s ", (n > 1 ? ")" : ""));	/* right paren */
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

  if (entity->info.spec.range_var->alias_print)
    {
      fprintf (f, "(%s)", entity->info.spec.range_var->alias_print);
    }

  fprintf (f, "(%lu/%lu)", QO_NODE_NCARD (node), QO_NODE_TCARD (node));
  if (!bitset_is_empty (&(QO_NODE_SARGS (node))))
    {
      fputs (" (sargs ", f);
      bitset_print (&(QO_NODE_SARGS (node)), f);
      fputs (")", f);
    }
  if (!bitset_is_empty (&(QO_NODE_OUTER_DEP_SET (node))))
    {
      fputs (" (outer-dep-set ", f);
      bitset_print (&(QO_NODE_OUTER_DEP_SET (node)), f);
      fputs (")", f);
    }

  fprintf (f, " (loc %d)", entity->info.spec.location);
}

/*
 * qo_seg_clear () -
 *   return:
 *   env(in):
 *   idx(in):
 */
static void
qo_seg_clear (QO_ENV * env, int idx)
{
  QO_SEGMENT *seg = QO_ENV_SEG (env, idx);

  QO_SEG_ENV (seg) = env;
  QO_SEG_HEAD (seg) = NULL;
  QO_SEG_TAIL (seg) = NULL;
  QO_SEG_EQ_ROOT (seg) = NULL;
  QO_SEG_EQCLASS (seg) = NULL;
  QO_SEG_NAME (seg) = NULL;
  QO_SEG_INFO (seg) = NULL;
  QO_SEG_SET_VALUED (seg) = false;
  QO_SEG_CLASS_ATTR (seg) = false;
  QO_SEG_SHARED_ATTR (seg) = false;
  QO_SEG_IDX (seg) = idx;
  QO_SEG_FUNC_INDEX (seg) = false;
  bitset_init (&(QO_SEG_INDEX_TERMS (seg)), env);
}

/*
 * qo_seg_free () -
 *   return:
 *   seg(in):
 */
static void
qo_seg_free (QO_SEGMENT * seg)
{
  if (QO_SEG_INFO (seg) != NULL)
    {
      qo_free_attr_info (QO_SEG_ENV (seg), QO_SEG_INFO (seg));
      if (QO_SEG_FUNC_INDEX (seg) == true)
	{
	  if (QO_SEG_NAME (seg))
	    {
	      free_and_init (QO_SEG_NAME (seg));
	    }
	}
    }
  bitset_delset (&(QO_SEG_INDEX_TERMS (seg)));
}

/*
 * qo_seg_width () -
 *   return: size_t
 *   seg(in): A pointer to a QO_SEGMENT
 *
 * Note: Return the estimated width (in size_t units) of the indicated
 *	attribute.  This estimate will be required to estimate the
 *	size of intermediate results should they need to be
 *	materialized for e.g. sorting.
 */
int
qo_seg_width (QO_SEGMENT * seg)
{
  /*
   * This needs to consult the schema manager (or somebody) to
   * determine the type of the underlying attribute.  For set-valued
   * attributes, this is truly an estimate, since the size of the
   * attribute in any result tuple will be the product of the
   * cardinality of that particular set and the size of the underlying
   * element type.
   */
  int size;
  DB_DOMAIN *domain;

  domain = pt_node_to_db_domain (QO_ENV_PARSER (QO_SEG_ENV (seg)),
				 QO_SEG_PT_NODE (seg), NULL);
  if (domain)
    {
      domain = tp_domain_cache (domain);
    }
  else
    {
      /* guessing */
      return sizeof (int);
    }

  size = tp_domain_disk_size (domain);
  switch (TP_DOMAIN_TYPE (domain))
    {
    case DB_TYPE_VARBIT:
    case DB_TYPE_VARCHAR:
    case DB_TYPE_VARNCHAR:
      /* do guessing for variable character type */
      size = size * (2 / 3);
      break;
    default:
      break;
    }
  return MAX ((int) sizeof (int), size);
  /* for backward compatibility, at least sizeof(long) */
}

/*
 * qo_seg_fprint () -
 *   return:
 *   seg(in):
 *   f(in):
 */
void
qo_seg_fprint (QO_SEGMENT * seg, FILE * f)
{
  fprintf (f, "%s[%d]", QO_SEG_NAME (seg), QO_NODE_IDX (QO_SEG_HEAD (seg)));
}

/*
 * qo_eqclass_new () -
 *   return:
 *   env(in):
 */
static QO_EQCLASS *
qo_eqclass_new (QO_ENV * env)
{
  QO_EQCLASS *eqclass;

  QO_ASSERT (env, env->neqclasses < env->Neqclasses);
  eqclass = QO_ENV_EQCLASS (env, env->neqclasses);

  QO_EQCLASS_ENV (eqclass) = env;
  QO_EQCLASS_IDX (eqclass) = env->neqclasses;
  QO_EQCLASS_TERM (eqclass) = NULL;

  bitset_init (&(QO_EQCLASS_SEGS (eqclass)), env);

  env->neqclasses++;

  return eqclass;
}

/*
 * qo_eqclass_free () -
 *   return:
 *   eqclass(in):
 */
static void
qo_eqclass_free (QO_EQCLASS * eqclass)
{
  bitset_delset (&(QO_EQCLASS_SEGS (eqclass)));
}

/*
 * qo_eqclass_add () -
 *   return:
 *   eqclass(in):
 *   seg(in):
 */
static void
qo_eqclass_add (QO_EQCLASS * eqclass, QO_SEGMENT * seg)
{
  bitset_add (&(QO_EQCLASS_SEGS (eqclass)), QO_SEG_IDX (seg));
  bitset_add (&(QO_NODE_EQCLASSES (QO_SEG_HEAD (seg))),
	      QO_EQCLASS_IDX (eqclass));
  QO_SEG_EQCLASS (seg) = eqclass;
}

/*
 * qo_eqclass_dump () -
 *   return:
 *   eqclass(in):
 *   f(in):
 */
static void
qo_eqclass_dump (QO_EQCLASS * eqclass, FILE * f)
{
  const char *prefix = "";
  int member;
  QO_ENV *env = QO_EQCLASS_ENV (eqclass);
  BITSET_ITERATOR bi;

  if (QO_EQCLASS_TERM (eqclass))
    {
      qo_term_fprint (QO_EQCLASS_TERM (eqclass), f);
    }
  else
    {
      for (member = bitset_iterate (&(QO_EQCLASS_SEGS (eqclass)), &bi);
	   member != -1; member = bitset_next_member (&bi))
	{
	  fputs (prefix, f);
	  qo_seg_fprint (QO_ENV_SEG (env, member), f);
	  prefix = " ";
	}
    }
}

/*
 * qo_term_clear () -
 *   return:
 *   env(in):
 *   idx(in):
 */
static void
qo_term_clear (QO_ENV * env, int idx)
{
  QO_TERM *term = QO_ENV_TERM (env, idx);

  QO_TERM_ENV (term) = env;
  QO_TERM_CLASS (term) = QO_TC_OTHER;
  QO_TERM_SELECTIVITY (term) = 1.0;
  QO_TERM_RANK (term) = 0;
  QO_TERM_PT_EXPR (term) = NULL;
  QO_TERM_LOCATION (term) = 0;
  QO_TERM_SEG (term) = NULL;
  QO_TERM_OID_SEG (term) = NULL;
  QO_TERM_HEAD (term) = NULL;
  QO_TERM_TAIL (term) = NULL;
  QO_TERM_EQCLASS (term) = QO_UNORDERED;
  QO_TERM_NOMINAL_SEG (term) = NULL;
  QO_TERM_IDX (term) = idx;

  QO_TERM_CAN_USE_INDEX (term) = 0;
  QO_TERM_INDEX_SEG (term, 0) = NULL;
  QO_TERM_INDEX_SEG (term, 1) = NULL;
  QO_TERM_JOIN_TYPE (term) = NO_JOIN;

  bitset_init (&(QO_TERM_NODES (term)), env);
  bitset_init (&(QO_TERM_SEGS (term)), env);
  bitset_init (&(QO_TERM_SUBQUERIES (term)), env);

  QO_TERM_FLAG (term) = 0;
}

/*
 * qo_discover_sort_limit_nodes () - discover the subset of nodes on which
 *				     a SORT_LIMIT plan can be applied.
 * return   : void
 * env (in) : env
 *
 * Note: This function discovers a subset of nodes on which a SORT_LIMIT plan
 *  can be applied without altering the result of the query.
 */
static void
qo_discover_sort_limit_nodes (QO_ENV * env)
{
  PT_NODE *query, *orderby, *sort_col, *select_list, *col, *save_next;
  int i, pos_spec, limit_max_count;
  QO_NODE *node;
  BITSET order_nodes, dep_nodes, expr_segs, tmp_bitset;
  BITSET_ITERATOR bi;

  bitset_init (&order_nodes, env);
  bitset_init (&QO_ENV_SORT_LIMIT_NODES (env), env);

  query = QO_ENV_PT_TREE (env);
  if (!PT_IS_SELECT (query))
    {
      goto abandon_stop_limit;
    }
  if (query->info.query.all_distinct != PT_ALL
      || query->info.query.q.select.group_by != NULL
      || query->info.query.q.select.connect_by != NULL)
    {
      goto abandon_stop_limit;
    }
  if (query->info.query.q.select.hint & PT_HINT_NO_SORT_LIMIT)
    {
      goto abandon_stop_limit;
    }

  if (pt_get_query_limit_value (QO_ENV_PARSER (env), QO_ENV_PT_TREE (env),
				&QO_ENV_LIMIT_VALUE (env)) != NO_ERROR)
    {
      /* unusable limit */
      goto abandon_stop_limit;
    }

  if (env->npartitions > 1)
    {
      /* not applicable when dealing with more than one partition */
      goto abandon_stop_limit;
    }

  if (bitset_cardinality (&QO_PARTITION_NODES (QO_ENV_PARTITION (env, 0)))
      <= 1)
    {
      /* No need to apply this optimization on a single node */
      goto abandon_stop_limit;
    }

  orderby = query->info.query.order_by;
  if (orderby == NULL)
    {
      goto abandon_stop_limit;
    }

  env->use_sort_limit = QO_SL_INVALID;

  /* Verify that we don't have terms qualified as after join. These terms will
   * be evaluated after the SORT-LIMIT plan and might invalidate tuples the
   * plan returned.
   */
  for (i = 0; i < env->nterms; i++)
    {
      if (QO_TERM_CLASS (&env->terms[i]) == QO_TC_AFTER_JOIN
	  || QO_TERM_CLASS (&env->terms[i]) == QO_TC_OTHER)
	{
	  goto abandon_stop_limit;
	}
    }

  /* Start by assuming that evaluation of the limit clause depends on all
   * nodes in the query. Since we only have one partition, we can get the
   * bitset of nodes from there.
   */
  bitset_union (&env->sort_limit_nodes,
		&QO_PARTITION_NODES (QO_ENV_PARTITION (env, 0)));

  select_list = pt_get_select_list (QO_ENV_PARSER (env), query);
  assert_release (select_list != NULL);

  /* Only consider ORDER BY expression which is evaluable during a scan.
   * This means any expression except analytic and aggregate functions.
   */
  for (sort_col = orderby; sort_col != NULL; sort_col = sort_col->next)
    {
      if (sort_col->node_type != PT_SORT_SPEC)
	{
	  goto abandon_stop_limit;
	}

      bitset_init (&expr_segs, env);
      bitset_init (&tmp_bitset, env);

      pos_spec = sort_col->info.sort_spec.pos_descr.pos_no;

      /* sort_col is a position specifier in select list. Have to walk the
       * select list to find the actual node */
      for (i = 1, col = select_list; col != NULL && i != pos_spec;
	   col = col->next, i++);

      if (col == NULL)
	{
	  assert_release (col != NULL);
	  goto abandon_stop_limit;
	}

      save_next = col->next;
      col->next = NULL;
      if (pt_has_analytic (QO_ENV_PARSER (env), col)
	  || pt_has_aggregate (QO_ENV_PARSER (env), col))
	{
	  /* abandon search because these expressions cannot be evaluated
	   * during SORT_LIMIT evaluation
	   */
	  col->next = save_next;
	  goto abandon_stop_limit;
	}

      /* get segments from col */
      qo_expr_segs (env, col, &expr_segs);
      /* get nodes for segments */
      qo_seg_nodes (env, &expr_segs, &tmp_bitset);

      /* accumulate nodes to order_nodes */
      bitset_union (&order_nodes, &tmp_bitset);

      bitset_delset (&expr_segs);
      bitset_delset (&tmp_bitset);

      col->next = save_next;
    }

  for (i = bitset_iterate (&order_nodes, &bi); i != -1;
       i = bitset_next_member (&bi))
    {
      bitset_init (&dep_nodes, env);
      node = QO_ENV_NODE (env, i);

      /* For each orderby node, gather nodes which are SORT_LIMIT independent
       * on this node and remove them from sort_limit_nodes.
       */
      qo_discover_sort_limit_join_nodes (env, node, &order_nodes, &dep_nodes);
      bitset_difference (&env->sort_limit_nodes, &dep_nodes);

      bitset_delset (&dep_nodes);
    }

  if (bitset_cardinality (&env->sort_limit_nodes) == env->Nnodes)
    {
      /* There is no subset of nodes on which we can apply SORT_LIMIT so
       * abandon this optimization
       */
      goto abandon_stop_limit;
    }

  bitset_delset (&order_nodes);

  /* In order to create a SORT-LIMIT plan, the query must have a valid limit.
   * All other conditions for creating the plan have been met. */
  if (DB_IS_NULL (&QO_ENV_LIMIT_VALUE (env)))
    {
      /* Cannot make a decision at this point. Go ahead with query compilation
       * as if this optimization does not apply. The query will be recompiled
       * once a valid limit is supplied.
       */
      goto sort_limit_possible;
    }

  limit_max_count = prm_get_integer_value (PRM_ID_SORT_LIMIT_MAX_COUNT);
  if (limit_max_count == 0)
    {
      /* SORT-LIMIT plans are disabled */
      goto abandon_stop_limit;
    }

  if ((DB_BIGINT) limit_max_count < DB_GET_BIGINT (&QO_ENV_LIMIT_VALUE (env)))
    {
      /* Limit too large to apply this optimization. Mark it as candidate but
       * do not generate SORT-LIMIT plans at this time.
       */
      goto sort_limit_possible;
    }

  if (bitset_cardinality (&env->sort_limit_nodes) == 1)
    {
      /* Mark this node as a sort stop candidate. We will generate a
       * SORT-LIMIT plan over this node.
       */
      int n = bitset_first_member (&order_nodes);
      node = QO_ENV_NODE (env, n);
      QO_NODE_SORT_LIMIT_CANDIDATE (node) = true;
    }

  env->use_sort_limit = QO_SL_USE;
  return;

sort_limit_possible:
  env->use_sort_limit = QO_SL_POSSIBLE;
  bitset_delset (&QO_ENV_SORT_LIMIT_NODES (env));
  return;

abandon_stop_limit:
  bitset_delset (&order_nodes);
  bitset_delset (&QO_ENV_SORT_LIMIT_NODES (env));
  env->use_sort_limit = QO_SL_INVALID;
}

/*
 * qo_equivalence () -
 *   return:
 *   sega(in):
 *   segb(in):
 */
static void
qo_equivalence (QO_SEGMENT * sega, QO_SEGMENT * segb)
{

  while (QO_SEG_EQ_ROOT (sega))
    {
      sega = QO_SEG_EQ_ROOT (sega);
    }
  while (QO_SEG_EQ_ROOT (segb))
    {
      segb = QO_SEG_EQ_ROOT (segb);
    }

  if (sega != segb)
    {
      QO_SEG_EQ_ROOT (sega) = segb;
    }
}

/*
 * qo_eqclass_wrt () -
 *   return:
 *   eqclass(in):
 *   nodeset(in):
 */
static QO_SEGMENT *
qo_eqclass_wrt (QO_EQCLASS * eqclass, BITSET * nodeset)
{
  int member;
  BITSET_ITERATOR si;
  QO_SEGMENT *result = NULL;

  for (member = bitset_iterate (&(QO_EQCLASS_SEGS (eqclass)), &si);
       member != -1; member = bitset_next_member (&si))
    {
      QO_SEGMENT *seg = QO_ENV_SEG (QO_EQCLASS_ENV (eqclass), member);
      if (BITSET_MEMBER (*nodeset, QO_NODE_IDX (QO_SEG_HEAD (seg))))
	{
	  result = seg;
	  break;
	}
    }

  QO_ASSERT (eqclass->env, result != NULL);
  return result;
}

/*
 * qo_eqclass_fprint_wrt () -
 *   return:
 *   eqclass(in):
 *   nodeset(in):
 *   f(in):
 */
void
qo_eqclass_fprint_wrt (QO_EQCLASS * eqclass, BITSET * nodeset, FILE * f)
{
  if (eqclass == QO_UNORDERED)
    {
      fputs ("UNORDERED", f);
    }
  else if (bitset_is_empty (&(QO_EQCLASS_SEGS (eqclass))))
    {
      /*
       * This is a phony eqclass created for a complex merge join.
       * Just fabricate some text that will let us know where it came
       * from...
       */
      fprintf (f, "phony (term[%d])",
	       QO_TERM_IDX (QO_EQCLASS_TERM (eqclass)));
    }
  else
    {
      qo_seg_fprint (qo_eqclass_wrt (eqclass, nodeset), f);
    }
}

/*
 * qo_term_free () -
 *   return:
 *   term(in):
 */
static void
qo_term_free (QO_TERM * term)
{
  /*
   * Free the expr alloced by this term
   */
  if (QO_TERM_IS_FLAGED (term, QO_TERM_COPY_PT_EXPR))
    {
      parser_free_tree (QO_ENV_PARSER (QO_TERM_ENV (term)),
			QO_TERM_PT_EXPR (term));
    }
  bitset_delset (&(QO_TERM_NODES (term)));
  bitset_delset (&(QO_TERM_SEGS (term)));
  bitset_delset (&(QO_TERM_SUBQUERIES (term)));
}

/*
 * qo_term_fprint () -
 *   return:
 *   term(in):
 *   f(in):
 */
void
qo_term_fprint (QO_TERM * term, FILE * f)
{
  QO_TERMCLASS tc;

  if (term)
    {
      switch (tc = QO_TERM_CLASS (term))
	{
	case QO_TC_PATH:
	  qo_node_fprint (QO_TERM_HEAD (term), f);
	  if (!QO_TERM_SEG (term) || !QO_SEG_NAME (QO_TERM_SEG (term)))
	    {
	      fprintf (f, " () -> ");
	    }
	  else
	    {
	      fprintf (f, " %s -> ", QO_SEG_NAME (QO_TERM_SEG (term)));
	    }
	  qo_node_fprint (QO_TERM_TAIL (term), f);
	  break;

	case QO_TC_DEP_LINK:
	  fprintf (f, "table(");
	  bitset_print (&(QO_NODE_DEP_SET (QO_TERM_TAIL (term))), f);
	  fprintf (f, ") -> ");
	  qo_node_fprint (QO_TERM_TAIL (term), f);
	  break;

	case QO_TC_DEP_JOIN:
	  qo_node_fprint (QO_TERM_HEAD (term), f);
	  fprintf (f, " <dj> ");
	  qo_node_fprint (QO_TERM_TAIL (term), f);
	  break;

	default:
	  fprintf (f, "term[%d]", QO_TERM_IDX (term));
	  break;
	}
    }
  else
    {
      fprintf (f, "none");
    }
}

/*
 * qo_termset_fprint () -
 *   return:
 *   env(in):
 *   terms(in):
 *   f(in):
 */
void
qo_termset_fprint (QO_ENV * env, BITSET * terms, FILE * f)
{
  int tx;
  BITSET_ITERATOR si;
  const char *prefix = "";

  for (tx = bitset_iterate (terms, &si);
       tx != -1; tx = bitset_next_member (&si))
    {
      fputs (prefix, f);
      qo_term_fprint (QO_ENV_TERM (env, tx), f);
      prefix = " AND ";
    }
}

/*
 * qo_term_dump () -
 *   return:
 *   term(in):
 *   f(in):
 */
static void
qo_term_dump (QO_TERM * term, FILE * f)
{
  PT_NODE *conj, *saved_next = NULL;
  QO_TERMCLASS tc;

  conj = QO_TERM_PT_EXPR (term);
  if (conj)
    {
      saved_next = conj->next;
      conj->next = NULL;
    }

  tc = QO_TERM_CLASS (term);
  switch (tc)
    {
    case QO_TC_PATH:
      qo_node_fprint (QO_TERM_HEAD (term), f);
      if (!QO_TERM_SEG (term) || !QO_SEG_NAME (QO_TERM_SEG (term)))
	{
	  fprintf (f, " () -> ");
	}
      else
	{
	  fprintf (f, " %s -> ", QO_SEG_NAME (QO_TERM_SEG (term)));
	}
      qo_node_fprint (QO_TERM_TAIL (term), f);
      break;

    case QO_TC_DEP_LINK:
      fprintf (f, "table(");
      bitset_print (&(QO_NODE_DEP_SET (QO_TERM_TAIL (term))), f);
      fprintf (f, ") -> ");
      qo_node_fprint (QO_TERM_TAIL (term), f);
      break;

    case QO_TC_DEP_JOIN:
      qo_node_fprint (QO_TERM_HEAD (term), f);
      fprintf (f, " <dj> ");
      qo_node_fprint (QO_TERM_TAIL (term), f);
      break;

    case QO_TC_DUMMY_JOIN:
      if (conj)
	{			/* may be transitive dummy join term */
	  fprintf (f, "%s",
		   parser_print_tree (QO_ENV_PARSER (QO_TERM_ENV (term)),
				      conj));
	}
      else
	{
	  qo_node_fprint (QO_TERM_HEAD (term), f);
	  fprintf (f, ", ");
	  qo_node_fprint (QO_TERM_TAIL (term), f);
	}
      break;

    default:
      assert_release (conj != NULL);
      if (conj)
	{
	  PARSER_CONTEXT *parser = QO_ENV_PARSER (QO_TERM_ENV (term));
	  PT_PRINT_VALUE_FUNC saved_func = parser->print_db_value;

	  /* in order to print auto parameterized values */
	  parser->print_db_value = pt_print_node_value;
	  fprintf (f, "%s", parser_print_tree (parser, conj));
	  parser->print_db_value = saved_func;
	}
      break;
    }
  fprintf (f, " (sel %g)", QO_TERM_SELECTIVITY (term));

  if (QO_TERM_RANK (term) > 1)
    {
      fprintf (f, " (rank %d)", QO_TERM_RANK (term));
    }

  switch (QO_TERM_CLASS (term))
    {
    case QO_TC_PATH:
      fprintf (f, " (path term)");
      break;
    case QO_TC_JOIN:
      fprintf (f, " (join term)");
      break;
    case QO_TC_SARG:
      fprintf (f, " (sarg term)");
      break;
    case QO_TC_OTHER:
      {
	if (conj
	    && conj->node_type == PT_VALUE && conj->info.value.location == 0)
	  {
	    /* is an always-false or always-true WHERE condition */
	    fprintf (f, " (dummy sarg term)");
	  }
	else
	  {
	    fprintf (f, " (other term)");
	  }
      }
      break;
    case QO_TC_DEP_LINK:
      fprintf (f, " (dep term)");
      break;
    case QO_TC_DEP_JOIN:
      fprintf (f, " (dep-join term)");
      break;
    case QO_TC_DURING_JOIN:
      fprintf (f, " (during join term)");
      break;
    case QO_TC_AFTER_JOIN:
      fprintf (f, " (after join term)");
      break;
    case QO_TC_TOTALLY_AFTER_JOIN:
      fprintf (f, " (instnum term)");
      break;
    case QO_TC_DUMMY_JOIN:
      fprintf (f, " (dummy join term)");
      break;
    default:
      break;
    }

  if (QO_TERM_IS_FLAGED (term, QO_TERM_MERGEABLE_EDGE))
    {
      fputs (" (mergeable)", f);
    }

  switch (QO_TERM_JOIN_TYPE (term))
    {
    case NO_JOIN:
      fputs (" (not-join eligible)", f);
      break;

    case JOIN_INNER:
      fputs (" (inner-join)", f);
      break;

    case JOIN_LEFT:
      fputs (" (left-join)", f);
      break;

    case JOIN_RIGHT:
      fputs (" (right-join)", f);
      break;

    case JOIN_OUTER:		/* not used */
      fputs (" (outer-join)", f);
      break;

    default:
      break;
    }

  if (QO_TERM_CAN_USE_INDEX (term))
    {
      int i;
      fputs (" (indexable", f);
      for (i = 0; i < QO_TERM_CAN_USE_INDEX (term); i++)
	{
	  fputs (" ", f);
	  qo_seg_fprint (QO_TERM_INDEX_SEG (term, i), f);
	}
      fputs (")", f);
    }

  fprintf (f, " (loc %d)", QO_TERM_LOCATION (term));

  /* restore link */
  if (conj)
    {
      conj->next = saved_next;
    }
}

/*
 * qo_subquery_dump () -
 *   return:
 *   env(in):
 *   subq(in):
 *   f(in):
 */
static void
qo_subquery_dump (QO_ENV * env, QO_SUBQUERY * subq, FILE * f)
{
  int i;
  BITSET_ITERATOR bi;
  const char *separator;

  fprintf (f, "%p", (void *) subq->node);

  separator = NULL;
  fputs (" {", f);
  for (i = bitset_iterate (&(subq->segs), &bi);
       i != -1; i = bitset_next_member (&bi))
    {
      if (separator)
	{
	  fputs (separator, f);
	}
      qo_seg_fprint (QO_ENV_SEG (env, i), f);
      separator = " ";
    }
  fputs ("}", f);

  separator = "";
  fputs (" {", f);
  for (i = bitset_iterate (&(subq->nodes), &bi);
       i != -1; i = bitset_next_member (&bi))
    {
      fprintf (f, "%snode[%d]", separator, i);
      separator = " ";
    }
  fputs ("}", f);

  fputs (" (from term(s)", f);
  for (i = bitset_iterate (&(subq->terms), &bi);
       i != -1; i = bitset_next_member (&bi))
    {
      fprintf (f, " %d", i);
    }
  fputs (")", f);
}

/*
 * qo_subquery_free () -
 *   return:
 *   subq(in):
 */
static void
qo_subquery_free (QO_SUBQUERY * subq)
{
  bitset_delset (&(subq->segs));
  bitset_delset (&(subq->nodes));
  bitset_delset (&(subq->terms));
}

/*
 * qo_partition_init () -
 *   return:
 *   env(in):
 *   part(in):
 *   n(in):
 */
static void
qo_partition_init (QO_ENV * env, QO_PARTITION * part, int n)
{
  bitset_init (&(QO_PARTITION_NODES (part)), env);
  bitset_init (&(QO_PARTITION_EDGES (part)), env);
  bitset_init (&(QO_PARTITION_DEPENDENCIES (part)), env);
  QO_PARTITION_M_OFFSET (part) = 0;
  QO_PARTITION_PLAN (part) = NULL;
  QO_PARTITION_IDX (part) = n;
}

/*
 * qo_partition_free () -
 *   return:
 *   part(in):
 */
static void
qo_partition_free (QO_PARTITION * part)
{
  bitset_delset (&(QO_PARTITION_NODES (part)));
  bitset_delset (&(QO_PARTITION_EDGES (part)));
  bitset_delset (&(QO_PARTITION_DEPENDENCIES (part)));

#if 0
  /*
   * Do *not* free the plan here; it already had its ref count bumped
   * down during combine_partitions(), and it will be (has been)
   * collected by the call to qo_plan_discard() that freed the
   * top-level plan.  What we have here is a dangling pointer.
   */
  qo_plan_del_ref (QO_PARTITION_PLAN (part));
#else
  QO_PARTITION_PLAN (part) = NULL;
#endif
}


/*
 * qo_partition_dump () -
 *   return:
 *   part(in):
 *   f(in):
 */
static void
qo_partition_dump (QO_PARTITION * part, FILE * f)
{
  fputs ("(nodes ", f);
  bitset_print (&(QO_PARTITION_NODES (part)), f);
  fputs (") (edges ", f);
  bitset_print (&(QO_PARTITION_EDGES (part)), f);
  fputs (") (dependencies ", f);
  bitset_print (&(QO_PARTITION_DEPENDENCIES (part)), f);
  fputs (")", f);
}

/*
 * qo_print_stats () -
 *   return:
 *   f(in):
 */
void
qo_print_stats (FILE * f)
{
  fputs ("\n", f);
  qo_info_stats (f);
  qo_plans_stats (f);
#if defined (CUBRID_DEBUG)
  set_stats (f);
#endif
}

/*
 * qo_seg_nodes () - Return a bitset of node ids produced from the heads
 *		     of all of the segments in segset
 *   return:
 *   env(in): The environment in which these segment and node ids make sense
 *   segset(in): A bitset of segment ids
 *   result(out): A bitset of node ids (OUTPUT PARAMETER)
 */
static void
qo_seg_nodes (QO_ENV * env, BITSET * segset, BITSET * result)
{
  BITSET_ITERATOR si;
  int i;

  BITSET_CLEAR (*result);
  for (i = bitset_iterate (segset, &si); i != -1;
       i = bitset_next_member (&si))
    {
      bitset_add (result, QO_NODE_IDX (QO_SEG_HEAD (QO_ENV_SEG (env, i))));
    }
}

/*
 * qo_is_prefix_index () - Find out if this is a prefix index
 *   return:
 *   ent(in): index entry
 */
bool
qo_is_prefix_index (QO_INDEX_ENTRY * ent)
{
  if (ent && ent->class_ && ent->class_->smclass)
    {
      SM_CLASS_CONSTRAINT *cons;

      cons = classobj_find_class_index (ent->class_->smclass,
					ent->constraints->name);
      if (cons)
	{
	  if (cons->attrs_prefix_length && cons->attrs_prefix_length[0] != -1)
	    {
	      return true;
	    }
	}
    }

  return false;
}

/*
 * qo_is_filter_index () - Find out if this is a filter index
 *   return: true/false
 *   ent(in): index entry
 */
bool
qo_is_filter_index (QO_INDEX_ENTRY * ent)
{
  if (ent && ent->constraints)
    {
      if (ent->constraints->filter_predicate && ent->force > 0)
	{
	  return true;
	}
    }

  return false;
}

/*
 * qo_check_coll_optimization () - Checks for attributes with collation in
 *				   index and fills the optimization structure
 *
 *   return:
 *   ent(in): index entry
 *   collation_opt(out):
 *
 *  Note : this only checks for index covering optimization.
 *	   If at least one attribute of index does not support index covering,
 *	   this option will be disabled for the entire index.
 */
void
qo_check_coll_optimization (QO_INDEX_ENTRY * ent, COLL_OPT * collation_opt)
{
  bool is_prefix_index = false;

  assert (collation_opt != NULL);

  collation_opt->allow_index_opt = true;

  if (ent && ent->class_ && ent->class_->smclass)
    {
      SM_CLASS_CONSTRAINT *cons;
      SM_ATTRIBUTE **attr;
      cons =
	classobj_find_class_index (ent->class_->smclass,
				   ent->constraints->name);
      if (cons == NULL || cons->attributes == NULL)
	{
	  return;
	}

      for (attr = cons->attributes; *attr != NULL; attr++)
	{
	  if ((*attr)->domain != NULL
	      && TP_TYPE_HAS_COLLATION (TP_DOMAIN_TYPE ((*attr)->domain)))
	    {
	      LANG_COLLATION *lang_coll =
		lang_get_collation (TP_DOMAIN_COLLATION ((*attr)->domain));

	      assert (lang_coll != NULL);

	      if (!(lang_coll->options.allow_index_opt))
		{
		  collation_opt->allow_index_opt = false;
		  return;
		}
	    }
	}
    }
}

/*
 * qo_discover_sort_limit_join_nodes () - discover nodes which are joined to
 *					  this node and do not need to be
 *					  taken into consideration when
 *					  evaluating sort and limit operators
 * return : void
 * env (in)	      : environment
 * node (in)	      : node to process
 * order_nodes (in)   : nodes on which the query will be ordered
 * dep_nodes (in/out) : nodes which are dependent on this one for sort and
 *			limit evaluation
 *
 * Note: A node joined to this node is independent of the sort and limit
 *  operator evaluation if we are not ordering results based on it and
 *  one of the following conditions is true:
 *  1. Is left outer joined to to node
 *  2. Is inner joined to this node through a primary key->foreign key
 *     relationship (see qo_is_pk_fk_full_join)
 */
static void
qo_discover_sort_limit_join_nodes (QO_ENV * env, QO_NODE * node,
				   BITSET * order_nodes, BITSET * dep_nodes)
{
  BITSET join_nodes;
  BITSET_ITERATOR bi;
  QO_TERM *term;
  int i;

  bitset_init (dep_nodes, env);
  bitset_init (&join_nodes, env);

  for (i = 0; i < env->nedges; i++)
    {
      term = QO_ENV_TERM (env, i);
      if (BITSET_MEMBER (QO_TERM_NODES (term), QO_NODE_IDX (node)))
	{
	  if (QO_TERM_CLASS (term) == QO_TC_JOIN
	      && ((QO_TERM_JOIN_TYPE (term) == JOIN_LEFT
		   && QO_TERM_HEAD (term) == node)
		  || (QO_TERM_JOIN_TYPE (term) == JOIN_RIGHT
		      && QO_TERM_TAIL (term) == node)))
	    {
	      /* Other nodes in this term are outer joined with our node. We
	       * can safely add them to dep_nodes if we're not ordering on
	       * them
	       */
	      bitset_union (dep_nodes, &QO_TERM_NODES (term));

	      /* remove nodes on which we're ordering */
	      bitset_difference (dep_nodes, order_nodes);
	    }
	  else if (QO_TERM_CLASS (term) == QO_TC_PATH)
	    {
	      /* path edges are always independent */
	      bitset_difference (dep_nodes, &QO_TERM_NODES (term));
	    }
	  else
	    {
	      bitset_union (&join_nodes, &QO_TERM_NODES (term));
	    }
	}
    }

  /* remove nodes on which we're ordering from the list */
  bitset_difference (&join_nodes, order_nodes);

  /* process inner joins */
  for (i = bitset_iterate (&join_nodes, &bi); i != -1;
       i = bitset_next_member (&bi))
    {
      if (qo_is_pk_fk_full_join (env, node, QO_ENV_NODE (env, i)))
	{
	  bitset_add (dep_nodes, i);
	}
    }
}

/*
 * qo_is_pk_fk_full_join () - verify if the join between two nodes can be
 *			      fully expressed through a foreign key->primary
 *			      key join
 * return : true if there is a pk->fk relationship, false otherwise
 * env (in) :
 * fk_node (in) : node which should have a foreign key
 * pk_node (in) : node which should have a primary key
 *
 * Note: Full PK->FK relationships guarantee that any tuple from the FK node
 *  generates a single tuple in the PK node. This relationship allows us to
 *  apply some optimizations (like SORT-LIMIT). This relationship exists if
 *  the following conditions are met:
 *  1. PK node has only EQ predicates.
 *  2. All terms from PK node are join edges
 *  3. The segments involved in terms are a prefix of the primary key index
 *  4. There is a foreign key in the fk_node which references the pk_node's
 *     primary key and the join uses the same prefix of the foreign key as
 *     that the of the primary key.
 */
static bool
qo_is_pk_fk_full_join (QO_ENV * env, QO_NODE * fk_node, QO_NODE * pk_node)
{
  int i, j;
  QO_NODE_INDEX *node_indexp;
  QO_INDEX_ENTRY *pk_idx = NULL, *fk_idx = NULL;
  QO_TERM *term;
  QO_SEGMENT *fk_seg, *pk_seg;

  node_indexp = QO_NODE_INDEXES (pk_node);
  if (node_indexp == NULL)
    {
      return false;
    }

  pk_idx = NULL;
  for (i = 0; i < QO_NI_N (node_indexp); i++)
    {
      pk_idx = QO_NI_ENTRY (node_indexp, i)->head;
      if (pk_idx->constraints->type == SM_CONSTRAINT_PRIMARY_KEY)
	{
	  break;
	}
      pk_idx = NULL;
    }

  if (pk_idx == NULL)
    {
      /* node either does not have a primary key or it is not referenced in
       * any way in this statement
       */
      return false;
    }

  /* find the foreign key on fk_node which references pk_node */
  node_indexp = QO_NODE_INDEXES (fk_node);
  if (node_indexp == NULL)
    {
      return false;
    }

  fk_idx = NULL;
  for (i = 0; i < QO_NI_N (node_indexp); i++)
    {
      fk_idx = QO_NI_ENTRY (node_indexp, i)->head;
      if (fk_idx->constraints->type != SM_CONSTRAINT_FOREIGN_KEY)
	{
	  fk_idx = NULL;
	  continue;
	}
      if (BTID_IS_EQUAL (&fk_idx->constraints->fk_info->ref_class_pk_btid,
			 &pk_idx->constraints->index_btid))
	{
	  /* These are the droids we're looking for */
	  break;
	}
      fk_idx = NULL;
    }

  if (fk_idx == NULL)
    {
      /* No matching foreign key */
      return false;
    }

  /* Make sure we don't have gaps in the primary key columns */
  for (i = 0; i < pk_idx->nsegs; i++)
    {
      if (pk_idx->seg_idxs[i] == -1)
	{
	  if (i == 0)
	    {
	      /* first col must be present */
	      return false;
	    }
	  /* this has to be the last one */
	  for (j = i + 1; j < pk_idx->nsegs; j++)
	    {
	      if (pk_idx->seg_idxs[j] != -1)
		{
		  return false;
		}
	    }
	  break;
	}
    }

  if (pk_idx->nsegs > fk_idx->nsegs)
    {
      /* The number of segments from primary key should be less than those
       * referenced in the foreign key. We can have more segments referenced
       * from the foreign key because we don't care about other terms from
       * the fk node */
      return false;
    }

  /* Verify that all terms from pk_node reference terms from fk_node.
   * If we find a term which only references pk_node, we can abandon the
   * search. */
  for (i = 0; i < env->nterms; i++)
    {
      term = QO_ENV_TERM (env, i);

      if (QO_TERM_CLASS (term) == QO_TC_DUMMY_JOIN)
	{
	  /* skip always true dummy join terms */
	  continue;
	}

      if (!BITSET_MEMBER (QO_TERM_NODES (term), QO_NODE_IDX (pk_node)))
	{
	  continue;
	}

      if (!BITSET_MEMBER (pk_idx->terms, QO_TERM_IDX (term)))
	{
	  /* Term does not use pk_idx. This means that there is a predicate
	   * on pk_node outside of the primary key
	   */
	  return false;
	}

      if (QO_TERM_JOIN_TYPE (term) != JOIN_INNER)
	{
	  /* we're only interested in inner joins */
	  return false;
	}

      if (!BITSET_MEMBER (QO_TERM_NODES (term), QO_NODE_IDX (fk_node)))
	{
	  /* found a term belonging to pk_node which does not reference the
	   * fk_node. This means pk_node is not full joined with fk_node
	   */
	  return false;
	}

      if (QO_TERM_CLASS (term) != QO_TC_JOIN
	  || !QO_TERM_IS_FLAGED (term, QO_TERM_EQUAL_OP))
	{
	  /* Not a join term, bail out */
	  return false;
	}

      /* Iterate through segments and make sure we're joining same columns.
       * E.g.: For PK(Pa, Pb) and FK (Fa, Fb), make sure the terms are not
       * Pa = Fb or Pb = Fa
       */
      if (term->can_use_index != 2)
	{
	  return false;
	}
      if (QO_SEG_HEAD (QO_TERM_INDEX_SEG (term, 0)) == fk_node)
	{
	  fk_seg = QO_TERM_INDEX_SEG (term, 0);
	  pk_seg = QO_TERM_INDEX_SEG (term, 1);
	}
      else
	{
	  pk_seg = QO_TERM_INDEX_SEG (term, 0);
	  fk_seg = QO_TERM_INDEX_SEG (term, 1);
	}

      /* make sure pk_seg and fk_seg reference the same position in the
       * two indexes
       */
      for (j = 0; j < pk_idx->nsegs; j++)
	{
	  if (pk_idx->seg_idxs[j] == QO_SEG_IDX (pk_seg))
	    {
	      assert (j < fk_idx->nsegs);
	      if (fk_idx->seg_idxs[j] != QO_SEG_IDX (fk_seg))
		{
		  /* not joining the same column */
		  return false;
		}
	      break;
	    }
	}
    }

  return true;
}
