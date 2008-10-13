/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * query_graph.c - Query graph
 * TODO: rename this file to query_gaph.c
 * TODO: include query_graph_2.c and remove it
 *
 * Note: This module builds a query graph from a parse tree.
 *       It also transforms the tree by unfolding path expressions.
 */

#ident "$Id$"

#include "config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>
#if !defined(WINDOWS)
#include <values.h>
#endif /* !WINDOWS */

#include "error_manager.h"
#include "object_representation.h"
#include "optimizer.h"
#include "parser.h"
#include "xasl_generation_2.h"
#include "parser.h"
#include "qp_list.h"
#include "query_graph_2.h"
#include "db.h"
#include "schema_manager_3.h"
#include "query_graph_1.h"
#include "system_parameter.h"
#include "query_planner_1.h"
#include "memory_manager_2.h"
#include "environment_variable.h"
#include "util_func.h"
#include "qo.h"
#include "parser.h"
#include "query_planner_2.h"

/* figure out how many bytes a QO_USING_INDEX struct with n entries requires */
#define SIZEOF_USING_INDEX(n) \
    (sizeof(QO_USING_INDEX) + (((n)-1) * (sizeof(char *) + sizeof(int))))

#define ALLOC_USING_INDEX(env, n) \
    (QO_USING_INDEX *)malloc(SIZEOF_USING_INDEX(n))

/* any number that won't overlap PT_MISC_TYPE */
#define PREDICATE_TERM  -2


/* used by build_graph_for_entity() */
#define IS_OID(name_spec) \
    (((name_spec)->node_type == PT_NAME) \
     && PT_IS_OID_NAME(name_spec))

#define RANK_DEFAULT       0	/* default              */
#define RANK_NAME          RANK_DEFAULT	/* name  -- use default */
#define RANK_VALUE         RANK_DEFAULT	/* value -- use default */
#define RANK_EXPR_LIGHT    1	/* Group 1              */
#define RANK_EXPR_MEDIUM   2	/* Group 2              */
#define RANK_EXPR_HEAVY    3	/* Group 3              */
#define RANK_EXPR_FUNCTION 4	/* agg function, set    */
#define RANK_QUERY         8	/* subquery             */

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

static QO_PLAN *qo_optimize_helper (QO_ENV * env);

static QO_NODE *qo_add_node (PT_NODE * entity, QO_ENV * env);

static QO_SEGMENT *qo_insert_segment (QO_NODE * head, QO_NODE * tail,
				      PT_NODE * node, QO_ENV * env);

static QO_SEGMENT *qo_join_segment (QO_NODE * head, QO_NODE * tail,
				    PT_NODE * name, QO_ENV * env);

static PT_NODE *qo_add_final_segment (PARSER_CONTEXT * parser, PT_NODE * tree,
				      void *arg, int *continue_walk);

static QO_TERM *qo_add_term (PT_NODE * conjunct, int term_type, QO_ENV * env);

static void
qo_add_dep_term (QO_NODE * derived_node, BITSET * depend_nodes,
		 BITSET * depend_segs, QO_ENV * env);

static QO_TERM *qo_add_dummy_join_term (QO_ENV * env, QO_NODE * head_node,
					QO_NODE * tail_node);

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

static bool is_pseudo_const (QO_ENV * env, PT_NODE * expr);

static void get_local_subqueries (QO_ENV * env, PT_NODE * tree);

static void get_rank (QO_ENV * env);

static PT_NODE *get_referenced_attrs (PT_NODE * entity);

static bool expr_is_mergable (PT_NODE * pt_expr);

static bool expr_is_equi_join (PT_NODE * pt_expr);

static void add_hint (QO_ENV * env, PT_NODE * tree);

static void add_using_index (QO_ENV * env, PT_NODE * using_index);

static int get_opcode_rank (PT_OP_TYPE opcode);

static int get_operand_rank (PT_NODE * node);

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
      *(int *) retval = PRM_OPTIMIZATION_LEVEL;
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
	*(int *) retval = PRM_OPTIMIZATION_LEVEL;
      PRM_OPTIMIZATION_LEVEL = va_arg (args, int);
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
    return NULL;

  /* if its not a select, we're not interested, also if it is
   * merge we give up.
   */
  if (tree->node_type != PT_SELECT)
    return NULL;

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
      er_set (ER_ERROR_SEVERITY, __FILE__, __LINE__,
	      ER_QO_FAILED_ASSERTION, 0);
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
		  n = QO_TERM_LOCATION (term);
		  if (QO_NODE_LOCATION (QO_TERM_HEAD (term)) == n - 1 &&
		      QO_NODE_LOCATION (QO_TERM_TAIL (term)) == n)
		    {
		      bitset_add (&nodeset,
				  QO_NODE_IDX (QO_TERM_TAIL (term)));
		    }
		}

	      conj->next = next;
	    }			/* for */
	}
    }				/* for */

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
      spec = QO_NODE_ENTITY_SPEC (node);
      /* check join-edge for explicit join */
      if (spec->node_type == PT_SPEC &&
	  (spec->info.spec.join_type == PT_JOIN_INNER ||
	   spec->info.spec.join_type == PT_JOIN_LEFT_OUTER ||
	   spec->info.spec.join_type == PT_JOIN_RIGHT_OUTER) &&
	  !BITSET_MEMBER (nodeset, n))
	{
	  p_node = QO_ENV_NODE (env, n - 1);
	  (void) qo_add_dummy_join_term (env, p_node, node);
	  /* Is it safe to pass node[n-1] as head node?
	     Yes, because the sequence of QO_NODEs corresponds to
	     the sequence of PT_SPEC list */
	}
    }

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

  /* finish the rest of the opt structures */
  qo_discover_edges (env);

  /*
   * Don't do these things until *after* qo_discover_edges(); that
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
  if (PLAN_DUMP_ENABLED (level) && DETAILED_DUMP (level))
    qo_env_dump (env, query_plan_dump_fp);
  if (plan == NULL)
    qo_env_free (env);

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

  if (query == NULL)
    return NULL;

  env = qo_env_new (parser, query);
  if (env == NULL)
    return NULL;

  if (qo_validate (env))
    goto error;

  /*
   * Some of the following vectors may all be larger than we actually
   * need, but it's easier to simply allocate the maximum possible size
   * now and maybe waste some than it is to a) determine (at this
   * point) the proper size or b) use an extensible vector (since that
   * requires another level of indirection so that pointers to elements
   * of the vector don't change).
   * Also, qo_validate sets the number of nodes and segments that we
   * will need in env->nsegs, env->nterms and env->nnodes. Use this
   * info to allocate then reset env->nsegs, env->nterms and env->nnodes.
   */
  env->segs = NALLOCATE (env, QO_SEGMENT, env->nsegs);
  env->nodes = NALLOCATE (env, QO_NODE, env->nnodes);
  env->terms = NALLOCATE (env, QO_TERM, env->nterms);
  env->eqclasses = NALLOCATE (env, QO_EQCLASS,
			      MAX (env->nnodes, env->nterms) + env->nsegs);
  env->partitions = NALLOCATE (env, QO_PARTITION, env->nnodes);

  if (env->segs == NULL
      || env->nodes == NULL
      || (env->nterms && env->terms == NULL)
      || env->eqclasses == NULL || env->partitions == NULL)
    goto error;

  for (i = 0; i < env->nsegs; ++i)
    qo_seg_clear (env, i);
  for (i = 0; i < env->nnodes; ++i)
    qo_node_clear (env, i);
  for (i = 0; i < env->nterms; ++i)
    qo_term_clear (env, i);

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
	    }			/* for */
	}
    }				/* for */

  /* count the number of conjuncts in the WHERE clause */
  for (conj = tree->info.query.q.select.where; conj; conj = conj->next)
    {
      if (conj->node_type != PT_EXPR &&
	  conj->node_type != PT_VALUE /* is a false conjunct */ )
	{
	  env->bail_out = 1;
	  return true;
	}
      env->nterms++;
    }				/* for */

  if (env->nnodes > OPTIMIZATION_LIMIT)
    return true;

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

  env->nnodes++;

  /* create oid segment for the entity */
  env->nsegs++;

  for (name = get_referenced_attrs (entity); name != NULL; name = name->next)
    {

      env->nsegs++;
    }

  /*
   * If this is a dependent derived table, set aside enough terms to
   * allow for the dependency edges that will be added to the graph:
   * one for the actual dependency, plus (nnodes-1) for the
   * QO_TC_DEP_JOIN edges we may have to fabricate.  We may not need
   * that many, but we won't need more, at least as long as the plan is
   * to connect the nodes with a star interconnection.  If that
   * assumption changes, we may need to bump this number.  See
   * qo_add_dep_term() for more info.
   */
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

}				/* build_query_graph_post */

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
  QO_NODE *node, *next_node;
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

  for (attr = attr_list; attr && !IS_OID (attr); attr = attr->next)
    ;

  /*
   * 'attr' will be non-null iff the oid "attribute" of the class
   * is explicitly used in some way, e.g., in a comparison or a projection.
   * If it is non-null, it will be created with the rest of the symbols.
   *
   * If it is null, we'll make one unless we're dealing with a derived
   * table.
   */
  if (!attr && !entity->info.spec.derived_table)
    {
      attr = parser_new_node (parser, PT_NAME);
      attr->info.name.resolved =
	entity->info.spec.flat_entity_list->info.name.original;
      attr->info.name.original = "";
      attr->info.name.spec_id = entity->info.spec.id;
      attr->info.name.meta_class = PT_OID_ATTR;

      /* create oid segment for the entity */
      seg = qo_insert_segment (node, NULL, attr, env);
      QO_SEG_SET_VALUED (seg) = false;	/* oid segments aren't set valued */
      QO_SEG_CLASS_ATTR (seg) = false;	/* oid segments aren't class attrs */
      QO_SEG_SHARED_ATTR (seg) = false;	/* oid segments aren't shared attrs */
    }

  /*
   * Create a segment for each symbol in the entities symbol table.
   */
  for (name = attr_list; name != NULL; name = name->next)
    {

      seg = qo_insert_segment (node, NULL, name, env);

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

      (void) qo_join_segment (node, next_node, next_entity->info.spec.
			      path_conjuncts->info.expr.arg1, env);

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

  QO_ASSERT (env, env->nnodes < env->Nnodes);

  node = QO_ENV_NODE (env, env->nnodes);

  /* fill in node */
  QO_NODE_ENV (node) = env;
  QO_NODE_ENTITY_SPEC (node) = entity;
  QO_NODE_NAME (node) = entity->info.spec.range_var->info.name.original;
  QO_NODE_IDX (node) = env->nnodes;
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
	  QO_ASSERT (env, QO_GET_CLASS_STATS(&info->info[i]) != NULL);
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
	      QO_NODE_NCARD (node) += QO_GET_CLASS_STATS(&info->info[i])->num_objects;
	      QO_NODE_TCARD (node) += QO_GET_CLASS_STATS(&info->info[i])->heap_size;
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
		  QO_NODE_NCARD (node) = xasl->cardinality;
		  QO_NODE_TCARD (node) = (QO_NODE_NCARD (node) *
					  (double) xasl->projected_size)
		    / (double) IO_PAGESIZE;
		  if (QO_NODE_TCARD (node) == 0)
		    QO_NODE_TCARD (node) = 1;
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
      {
	bool found_prev_not_sargable;
	QO_NODE *prev;

	found_prev_not_sargable = false;	/* init */
	for (i = n - 1; i > 0 && !found_prev_not_sargable; i--)
	  {
	    prev = QO_ENV_NODE (env, i);
	    /* directly outer-join connected */
	    if (QO_NODE_PT_JOIN_TYPE (prev) == PT_JOIN_LEFT_OUTER
		|| QO_NODE_PT_JOIN_TYPE (prev) == PT_JOIN_RIGHT_OUTER
		|| QO_NODE_PT_JOIN_TYPE (prev) == PT_JOIN_FULL_OUTER)
	      {
		if (!QO_NODE_SARGABLE (QO_ENV_NODE (env, i - 1)))
		  {
		    found_prev_not_sargable = true;
		  }
	      }
	    else
	      {			/* not directly outer-join connected */
		break;		/* give up */
	      }
	  }			/* for (i = n - 1; ...) */

	if (!found_prev_not_sargable)
	  {
	    QO_NODE_SARGABLE (QO_ENV_NODE (env, n - 1)) = false;
	  }
      }
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

  QO_ASSERT (env, attr->node_type == PT_NAME);

  for (i = 0; (!found) && (i < env->nnodes); /* no increment step */ )
    {
      *entity = QO_NODE_ENTITY_SPEC (QO_ENV_NODE (env, i));
      if ((*entity)->info.spec.id == attr->info.name.spec_id)
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
 */
static QO_SEGMENT *
qo_insert_segment (QO_NODE * head, QO_NODE * tail, PT_NODE * node,
		   QO_ENV * env)
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
  QO_SEG_NAME (seg) = node->info.name.original ?
    node->info.name.original :
    pt_append_string (QO_ENV_PARSER (env), NULL, "");

  if (IS_OID (node))
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
      QO_SEG_INFO (seg) = qo_get_attr_info (env, seg);
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

  for (i = 0; (!found) && (i < env->nsegs); /* no increment step */ )
    {
      if ((QO_SEG_HEAD (QO_ENV_SEG (env, i)) == head) &&
	  pt_name_equal (QO_ENV_PARSER (env),
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
	      QO_TERM_SELECTIVITY (term) = 0.0;
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
	    case PT_JOIN_FULL_OUTER:
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

  /*
   * Now add QO_TC_DEP_JOIN edges to connect all of the dependency
   * nodes.  These are necessary in order to let the planner know that
   * these nodes are in fact related even though there is no explicit
   * search condition term that relates them.
   *
   * This implementation will just build a star interconnection among
   * the nodes;  we probably ought to build a crossbar, but that's
   * harder and probably not all that much more useful.  Besides, the
   * two are equivalent for the case of N == 2, and dependent derived
   * tables with N > 2 seem to be extrememly rare.
   *
   * If this changes so that we're no longer building a star, be sure
   * to change the code in graph_size_for_entity() that sizes the
   * number of terms we'll need.
   */
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
 *   head_node(in):
 *   tail_node(in):
 */
static QO_TERM *
qo_add_dummy_join_term (QO_ENV * env, QO_NODE * head_node,
			QO_NODE * tail_node)
{
  QO_TERM *term;

  QO_ASSERT (env, env->nterms < env->Nterms);

  term = QO_ENV_TERM (env, env->nterms);

  /* fill in term */
  QO_TERM_CLASS (term) = QO_TC_DUMMY_JOIN;
  bitset_add (&(QO_TERM_NODES (term)), QO_NODE_IDX (head_node));
  bitset_add (&(QO_TERM_NODES (term)), QO_NODE_IDX (tail_node));
  QO_TERM_HEAD (term) = head_node;
  QO_TERM_TAIL (term) = tail_node;
  QO_TERM_PT_EXPR (term) = NULL;
  QO_TERM_LOCATION (term) = QO_NODE_LOCATION (tail_node);
  QO_TERM_SELECTIVITY (term) = 1.0;
  QO_TERM_RANK (term) = 0;

  switch (QO_NODE_PT_JOIN_TYPE (tail_node))
    {
    case PT_JOIN_LEFT_OUTER:
      QO_TERM_JOIN_TYPE (term) = JOIN_LEFT;
      break;
    case PT_JOIN_RIGHT_OUTER:
      QO_TERM_JOIN_TYPE (term) = JOIN_RIGHT;
      break;
    case PT_JOIN_FULL_OUTER:
      QO_TERM_JOIN_TYPE (term) = JOIN_OUTER;
      break;
    default:
      /* this should not happen */
      QO_TERM_JOIN_TYPE (term) = JOIN_INNER;
      break;
    }
  QO_TERM_FLAG (term) = 0;
  QO_TERM_IDX (term) = env->nterms;

  env->nterms++;

  /* record outer join dependecy */
  if (QO_OUTER_JOIN_TERM (term))
    {
      QO_NODE *p_node;

      p_node = QO_ENV_NODE (env, QO_NODE_IDX (tail_node) - 1);
      bitset_union (&(QO_NODE_OUTER_DEP_SET (tail_node)),
		    &(QO_NODE_OUTER_DEP_SET (p_node)));
      bitset_add (&(QO_NODE_OUTER_DEP_SET (tail_node)), QO_NODE_IDX (p_node));
    }

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
  QO_NODE *head_node, *tail_node, *on_node;
  QO_SEGMENT *head_seg, *tail_seg;
  BITSET lhs_segs, rhs_segs, lhs_nodes, rhs_nodes;
  BITSET_ITERATOR iter;
  PT_OP_TYPE op_type;
  int i, n, location;
  bool is_outer_on_cond;

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
    goto wrapup;

  /* only intersting in one predicate term; if 'term' has 'or_next', it was
     derived from OR term */
  if (pt_expr->or_next == NULL)
    {

      switch ((op_type = pt_expr->info.expr.op))
	{

	  /* operaotrs classified as lhs- and rhs-indexable */
	case PT_EQ:
	  QO_TERM_SET_FLAG (term, QO_TERM_EQUAL_OP);
	case PT_LT:
	case PT_LE:
	case PT_GT:
	case PT_GE:
	  /* temporary guess; RHS could be a indexable segment */
	  rhs_indexable = 1;
	  /* no break; fall through */

	  /* operaotrs classified as rhs-indexable */
	case PT_BETWEEN:
	case PT_RANGE:
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
	  /* RHS of the expression */
	  rhs_expr = pt_expr->info.expr.arg2;
	  /* get segments from RHS of the expression */
	  qo_expr_segs (env, rhs_expr, &rhs_segs);
	  /* no break; fall through */

	case PT_IS_NULL:
	case PT_IS_NOT_NULL:
	case PT_EXISTS:
	  /* LHS of the expression */
	  lhs_expr = pt_expr->info.expr.arg1;
	  /* get segments from LHS of the expression */
	  qo_expr_segs (env, lhs_expr, &lhs_segs);
	  /* now break switch statment */
	  break;

	case PT_OR:
	case PT_NOT:
	  /* get segments from the expression itself */
	  qo_expr_segs (env, pt_expr, &lhs_segs);
	  break;

	  /* the other operators that can not be used as term; error case */
	default:
	  /* stop processing */
	  QO_ABORT (env);

	}			/* switch ((op_type = pt_expr->info.expr.op)) */

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
      lhs_indexable &= (lhs_indexable && is_local_name (env, lhs_expr));
      if (lhs_indexable)
	{

	  if (op_type == PT_IS_IN || op_type == PT_EQ_SOME)
	    {
	      /* We have to be careful with this case because
	         "i IN (SELECT ...)"
	         has a special meaning: in this case the select is treated as
	         UNBOX_AS_TABLE instead of the usual UNBOX_AS_VALUE, and we
	         can't use an index even if we want to (because of an XASL
	         deficiency). Because is_pseudo_const() wants to believe that
	         subqueries are pseudo-constants, we have to check for that
	         condition outside of is_pseudo_const(). */
	      switch (rhs_expr->node_type)
		{
		case PT_SELECT:
		case PT_UNION:
		case PT_DIFFERENCE:
		case PT_INTERSECTION:
		  lhs_indexable = 0;
		  break;
		case PT_NAME:
		  if (rhs_expr->info.name.meta_class != PT_PARAMETER &&
		      pt_is_set_type (rhs_expr))
		    lhs_indexable = 0;
		  break;
		case PT_DOT_:
		  if (pt_is_set_type (rhs_expr))
		    lhs_indexable = 0;
		  break;
		default:
		  lhs_indexable &= is_pseudo_const (env, rhs_expr);
		}
	    }
	  else
	    {
	      /* is LHS attribute and is RHS constant value ? */
	      lhs_indexable &= is_pseudo_const (env, rhs_expr);
	    }			/* else */

	}			/* if (lhs_indexable) */
      if (lhs_indexable)
	{
	  n = bitset_first_member (&lhs_segs);
	  if (n != -1)
	    /* record in the term that it has indexable segment as LHS */
	    term->index_seg[i++] = QO_ENV_SEG (env, n);
	}			/* if (lhs_indexable) */


      /* examine if LHS is indexable or not? */


      /* is RHS attribute and is LHS constant value ? */
      rhs_indexable &= (rhs_indexable && is_local_name (env, rhs_expr) &&
			is_pseudo_const (env, lhs_expr));
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
	    /* record in the term that it has indexable segment as RHS */
	    term->index_seg[i++] = QO_ENV_SEG (env, n);
	}			/* if (rhs_indexable) */


      /* This will be non-zero iff this term looks like it could be used as
         an index access predicate one or two ways. (Things like "X.a = Y.b"
         could be used in either direction if there are indexes on both "X.a"
         and "Y.b".)
         This code used to check for compatible lhs and rhs domains, but that
         check has been removed for two reasons:
         1) We know, by virtue of the fact that we have made it through
         semantic analysis to this part of code generation, that a
         reasonable coercion exists so that the left and right sides are
         comparable;
         2) The index scan code has been modified to actually do a coercion
         on the incoming key if necessary.
         This means, for example, that you can supply an integer key to an
         index of floats and still get the right answer. Unfortunately, XASL
         evaluation is still sufficiently broken that we can't use an
         arbitrary expression as a key. Maybe some day... */
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

  /* location of this term */
  location = QO_TERM_LOCATION (term);
  QO_ASSERT (env, location >= 0);

  is_outer_on_cond = false;	/* init */
  if (location > 0)
    {
      on_node = QO_ENV_NODE (env, location);
      if (QO_NODE_PT_JOIN_TYPE (on_node) == PT_JOIN_LEFT_OUTER
	  || QO_NODE_PT_JOIN_TYPE (on_node) == PT_JOIN_RIGHT_OUTER
	  || QO_NODE_PT_JOIN_TYPE (on_node) == PT_JOIN_FULL_OUTER)
	{
	  is_outer_on_cond = true;
	}
    }

  /* number of nodes with which this term associated */
  n = bitset_cardinality (&(QO_TERM_NODES (term)));
  /* determine the class of the term */
  if (n == 0)
    {
      if (QO_TERM_LOCATION (term) > 0)
	{
	  QO_TERM_CLASS (term) = QO_TC_DURING_JOIN;
	}
      else
	{
	  bool inst_num = false;
	  (void) parser_walk_tree (parser, pt_expr, pt_check_instnum_pre,
				   NULL, pt_check_instnum_post, &inst_num);
	  QO_TERM_CLASS (term) =
	    inst_num ? QO_TC_TOTALLY_AFTER_JOIN : QO_TC_OTHER;
	}
    }
  else
    {
      if (term_type != PREDICATE_TERM)
	{
	  QO_TERM_CLASS (term) = QO_TC_PATH;
	}
      else if (n == 1)
	{
	  QO_TERM_CLASS (term) = QO_TC_SARG;
	}
      else
	{			/* n >= 2 */
	  if (location == 0)
	    {			/* in WHERE condition */
	      QO_TERM_CLASS (term) = QO_TC_OTHER;	/* init */
	      if (n == 2)
		{
		  QO_TERM_CLASS (term) = QO_TC_JOIN;	/* init */
		}
	    }
	  else
	    {			/* in ON condition */
	      on_node = QO_ENV_NODE (env, location);
	      switch (QO_NODE_PT_JOIN_TYPE (on_node))
		{
		case PT_JOIN_LEFT_OUTER:
		case PT_JOIN_RIGHT_OUTER:
		  /*  case PT_JOIN_FULL_OUTER: *//* not used */
		  QO_TERM_CLASS (term) = QO_TC_DURING_JOIN;	/* init */
		  break;

		default:
		  QO_TERM_CLASS (term) = QO_TC_OTHER;	/* init */
		  break;
		}

	      if (n == 2)
		{
		  head_node =
		    QO_ENV_NODE (env,
				 bitset_iterate (&(QO_TERM_NODES (term)),
						 &iter));
		  tail_node = QO_ENV_NODE (env, bitset_next_member (&iter));

		  if ((QO_NODE_IDX (head_node) == QO_NODE_IDX (on_node) - 1 &&
		       QO_NODE_IDX (tail_node) == QO_NODE_IDX (on_node))
		      || (QO_NODE_IDX (head_node) == QO_NODE_IDX (on_node) &&
			  QO_NODE_IDX (tail_node) ==
			  QO_NODE_IDX (on_node) - 1))
		    {
		      QO_TERM_CLASS (term) = QO_TC_JOIN;
		    }
		}

	    }			/* in ON condition */
	}			/* n >= 2 */
    }

  /* outer join cond shall not be others except QO_TC_SARG and QO_TC_JOIN */
  if (is_outer_on_cond
      && QO_TERM_CLASS (term) != QO_TC_SARG
      && QO_TERM_CLASS (term) != QO_TC_JOIN
      && QO_TERM_CLASS (term) != QO_TC_DURING_JOIN)
    QO_ABORT (env);

  /* re-classify QO_TC_SARG term for outer join */
  if (n == 1)
    {				/* QO_TERM_CLASS(term) == QO_TC_SARG */
      /* QO_NODE to which this sarg term belongs */
      head_node =
	QO_ENV_NODE (env, bitset_first_member (&(QO_TERM_NODES (term))));

      if (location > 0)
	{
	  if (is_outer_on_cond)
	    {
	      /* this term appears in outer join condition of FROM clause */

	      /* sarg term which exists in the following different ON cond;
	       * change it to QO_TC_DURING_JOIN
	       * example: change 'X.i = 1' to during-join term
	       *     SELECT ...
	       *     FROM   X right join Y on ... rigint join Z on X.i = 1
	       *
	       * if left outer join and the sarg term belongs to left ; or
	       * if right outer join and the sarg term belongs to right ;
	       * case no.2 of term class in 'outer join TM document'
	       * in other words, if the sarg term belongs to null padding table,
	       * change it to QO_TC_DURING_JOIN
	       */
	      if (!QO_NODE_SARGABLE (head_node))
		{
		  QO_TERM_CLASS (term) = QO_TC_DURING_JOIN;
		}
	      else if (QO_NODE_LOCATION (head_node) < location)
		{
		  PT_NODE *entity;

		  entity = QO_NODE_ENTITY_SPEC (head_node);
		  if (entity->node_type == PT_SPEC
		      && entity->info.spec.on_cond)
		    {
		      /* example: change 'Y.i = 2' to during-join term
		       *  SELECT ...
		       *  FROM X right join Y on ... rigint join Z on Y.i = 2
		       */
		      QO_TERM_CLASS (term) = QO_TC_DURING_JOIN;
		    }
		  else if (QO_NODE_LOCATION (head_node) + 1 < location)
		    {
		      /* example: change 'X.i = 1' to during-join term
		       *  SELECT ...
		       *  FROM X right join Y on ... rigint join Z on X.i = 1
		       */
		      QO_TERM_CLASS (term) = QO_TC_DURING_JOIN;
		    }
		}
	    }			/* if (is_outer_on_cond) */
	}
      else
	{			/* if (location > 0) */
	  int join_idx, node_idx;

	  /* this term appears in search condition of WHERE clause */

	  /* case no.4 of term class in 'outer join TM document'
	   *
	   * for example:
	   *   SELECT *
	   *   FROM X left outer join Y, ..., P right outer join Q
	   *   WHERE X = ? -- case 4.1: X is not sargable
	   *     AND Y = ? -- case 4.2: join type is 'left outer'
	   *     AND P = ? -- case 4.3: next join type is 'right outer'
	   *     AND Q = ? -- case 4.4: Q is not sargable
	   */

	  /* set the start node of outer join - init */
	  join_idx = -1;

	  node_idx = QO_NODE_IDX (head_node);
	  /* if the sarg term belongs to null padding table; */
	  if (QO_NODE_PT_JOIN_TYPE (head_node) == PT_JOIN_LEFT_OUTER)
	    {
	      join_idx = node_idx;	/* case 4.2 */
	    }
	  else
	    {
	      /* NEED MORE OPTIMIZATION for furture */
	      for (node_idx += 1; node_idx < env->nnodes; node_idx++)
		{
		  if (QO_NODE_PT_JOIN_TYPE (QO_ENV_NODE (env, node_idx)) ==
		      PT_JOIN_RIGHT_OUTER)
		    {
		      join_idx = node_idx;	/* case 4.3 */
		      break;
		    }
		}
	    }

	  /* check for the next right outer join;
	     case no.4 of term class in 'outer join TM document' */
	  if (join_idx != -1)
	    {
	      QO_TERM_CLASS (term) = QO_TC_AFTER_JOIN;
	    }
	}			/* if (location > 0) */
    }				/* if (n == 1) */


  /* There doesn't seem to be any useful distinction between QO_TC_JOIN and
     QO_TC_OTHER if the planner does the right thing: consider every term
     once its "valence" falls to 1.

     THIS REALLY NEEDS TO GET FIXED!

     If we don't fix it, we lose on queries like
     "SELECT x.a, y.b, z.c FROM x, y, z
     WHERE x.a = y.b + z.c AND y.d = k0 AND z.e = k1"

     If we have indexes on x.a, y.d, and z.e, we can do that with three index
     scans if we're behaving properly. If not, we have to do a full scan on
     x. */

  if (n == 2)
    {
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

      /* This is a pretty weak test; it only looks for equality
         comparisons. */
      merge_applies &= expr_is_mergable (pt_expr);

      /* You can't use merge to implement an = predicate on a table and one
         of its dependencies (or two dependencies of the same table, for that
         matter); merge needs both inputs to exist in their entirety, and
         dependent tables can't exist without an accompanying scan of their
         antecedent(s).
         The following test is probably too strong, since it rules out a
         merge in the following case:
         "SELECT *
         FROM x, y, TABLE(x.r) AS r(i), TABLE(y.s) AS s(i)
         WHERE r.i = s.i"
         Unfortunately, we don't have transitive dependency information
         around, so it's hard for us to distinguish that case from
         "SELECT *
         FROM x, TABLE(x.r) AS r(s), TABLE(s.t) AS t(i), TABLE(x.w) AS w(i)
         WHERE t.i = w.i"
         where t.i and w.i are ultimately dependent on the same base class
         and therefore cannot be joined with a merge.
         Right now I'd rather be safe. If you really want a merge in the
         first case, you can probably make it happen by changing
         "SELECT *
         FROM (SELECT r.i FROM x, TABLE(x.r) AS r(i)) AS r(i),
         (SELECT s.i FROM y, TABLE(y.s) AS s(i)) AS s(i)
         WHERE r.i = s.i"
       */
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

      if (merge_applies || term_type != PREDICATE_TERM)
	{
	  head_seg =
	    QO_ENV_SEG (env, bitset_iterate (&(QO_TERM_SEGS (term)), &iter));
	  tail_seg = QO_ENV_SEG (env, bitset_next_member (&iter));

	  if (term_type != PREDICATE_TERM)
	    {
	      /* i.e., it's a path term...
	         In this case, it's imperative that we get the head and tail
	         nodes and segs right. Fortunately, in this particular case
	         we can rely on the compiler to produce the term in a
	         consistent way, with the head on the lhs and the tail on
	         the rhs. */
	      head_node = QO_ENV_NODE (env, bitset_first_member (&lhs_nodes));
	      tail_node = QO_ENV_NODE (env, bitset_first_member (&rhs_nodes));
	    }

	  /* Now make sure that the head and tail segs correspond to the
	     proper nodes. */
	  if (QO_SEG_HEAD (head_seg) != head_node)
	    {
	      QO_SEGMENT *tmp;
	      tmp = head_seg;
	      head_seg = tail_seg;
	      tail_seg = tmp;
	      QO_ASSERT (env, QO_SEG_HEAD (head_seg) == head_node);
	      QO_ASSERT (env, QO_SEG_HEAD (tail_seg) == tail_node);
	    }

	  /* These are really only interesting for path terms, but it doesn't
	     hurt to set them for others too. */
	  QO_TERM_SEG (term) = head_seg;
	  QO_TERM_OID_SEG (term) = tail_seg;

	  /* The term might be a merge term (i.e., it uses '=' as the
	     operator), but the expressions might not be simple attribute
	     references, and we mustn't try to establish equivalence classes
	     in that case. */
	  if (expr_is_equi_join (pt_expr))
	    {
	      qo_equivalence (head_seg, tail_seg);
	      QO_TERM_NOMINAL_SEG (term) = head_seg;
	    }

	  /* always true transitive equi-join term is not suitable as
	   * m-join edge.
	   */
	  if (PT_EXPR_INFO_IS_FLAGED (pt_expr, PT_EXPR_INFO_TRANSITIVE))
	    {
	      merge_applies = 0;
	    }
	}			/* if (merge_applies) */

      if (merge_applies)
	{
	  QO_TERM_SET_FLAG (term, QO_TERM_MERGEABLE_EDGE);
	}

      /* Now make sure that the two (node) ends of the join get cached in the
         term structure. */
      QO_TERM_HEAD (term) = head_node;
      QO_TERM_TAIL (term) = tail_node;

    }

  /* re-classify TC_JOIN term for outer join and determine its join type */
  if (QO_TERM_CLASS (term) == QO_TC_JOIN)
    {
      /* head and tail QO_NODE to which this join term belongs;
         always 'head_node' precedents to 'tail_node' and
         tail has outer join spec */
      head_node = QO_TERM_HEAD (term);
      tail_node = QO_TERM_TAIL (term);

      /* inner join until proven otherwise */
      QO_TERM_JOIN_TYPE (term) = JOIN_INNER;

      if (location > 0)
	{
	  if (QO_NODE_IDX (tail_node) > 0)
	    {
	      QO_NODE *p_node;

	      p_node = QO_ENV_NODE (env, QO_NODE_IDX (tail_node) - 1);

	      /* if explicit inner join */
	      if (QO_NODE_PT_JOIN_TYPE (tail_node) == PT_JOIN_INNER)
		{
		  QO_TERM_JOIN_TYPE (term) = JOIN_INNER;
		  /* record explicit inner join dependecy */
		  bitset_union (&(QO_NODE_OUTER_DEP_SET (tail_node)),
				&(QO_NODE_OUTER_DEP_SET (p_node)));
		  bitset_add (&(QO_NODE_OUTER_DEP_SET (tail_node)),
			      QO_NODE_IDX (p_node));
		}
	      /* if left outer join;
	         case no.7 of term class in 'outer join TM document' */
	      if (QO_NODE_PT_JOIN_TYPE (tail_node) == PT_JOIN_LEFT_OUTER)
		{
		  QO_TERM_JOIN_TYPE (term) = JOIN_LEFT;
		  /* record outer join dependecy */
		  bitset_union (&(QO_NODE_OUTER_DEP_SET (tail_node)),
				&(QO_NODE_OUTER_DEP_SET (p_node)));
		  bitset_add (&(QO_NODE_OUTER_DEP_SET (tail_node)),
			      QO_NODE_IDX (p_node));
		}
	      /* if right outer join;
	         case no.8 of term class in 'outer join TM document' */
	      if (QO_NODE_PT_JOIN_TYPE (tail_node) == PT_JOIN_RIGHT_OUTER)
		{
		  QO_TERM_JOIN_TYPE (term) = JOIN_RIGHT;
		  /* record outer join dependecy */
		  bitset_union (&(QO_NODE_OUTER_DEP_SET (tail_node)),
				&(QO_NODE_OUTER_DEP_SET (p_node)));
		  bitset_add (&(QO_NODE_OUTER_DEP_SET (tail_node)),
			      QO_NODE_IDX (p_node));
		}
	    }			/* if (QO_NODE_IDX(tail_node) > 0) */

	  /* check for during join term
	   * for example:
	   *   SELECT ...
	   *   FROM X left outer join Y on ... left outer join Z on X.i = Y.i
	   */
	  if (IS_OUTER_JOIN_TYPE (QO_TERM_JOIN_TYPE (term)) &&
	      (QO_TERM_LOCATION (term) > QO_NODE_LOCATION (tail_node)))
	    {
	      QO_TERM_CLASS (term) = QO_TC_DURING_JOIN;
	    }
	}
      else
	{			/* if (location > 0) */
	  int join_idx, node_idx;

	  /* if explicit join;
	     case no.9 of term class in 'outer join TM document' */

	  /* set the start node of outer join - init */
	  join_idx = -1;

	  node_idx = QO_NODE_IDX (head_node);
	  /* if the sarg term belongs to null padding table; */
	  if (QO_NODE_PT_JOIN_TYPE (tail_node) == PT_JOIN_LEFT_OUTER)
	    {
	      join_idx = node_idx;	/* case 4.2 */
	    }
	  else
	    {
	      /* NEED MORE OPTIMIZATION for furture */
	      node_idx =
		MIN (QO_NODE_IDX (head_node), QO_NODE_IDX (tail_node));
	      for (; node_idx < env->nnodes; node_idx++)
		{
		  if (QO_NODE_PT_JOIN_TYPE (QO_ENV_NODE (env, node_idx)) ==
		      PT_JOIN_RIGHT_OUTER)
		    {
		      join_idx = node_idx;	/* case 4.3 */
		      break;
		    }
		}
	    }

	  /* check for the next right outer join;
	     case no.9 of term class in 'outer join TM document' */
	  if (join_idx != -1)
	    {
	      QO_TERM_CLASS (term) = QO_TC_AFTER_JOIN;
	      QO_TERM_JOIN_TYPE (term) = NO_JOIN;

	      /* keep out from m-join edge */
	      QO_TERM_CLEAR_FLAG (term, QO_TERM_MERGEABLE_EDGE);
	    }
	}			/* if (location > 0) */
    }				/* if (QO_TERM_CLASS(term) == QO_TC_JOIN) */

wrapup:

  /* A negative selectivity means that the cardinality of the result depends
     only on the cardinality of the head, not on the product of the
     cardinalities of the head and the tail as in the usual case. */
  switch (term_type)
    {
    case PT_PATH_INNER:
      QO_TERM_JOIN_TYPE (term) = JOIN_INNER;
      if (QO_NODE_NCARD (QO_TERM_TAIL (term)) == 0)
	QO_TERM_SELECTIVITY (term) = 0.0;
      else
	QO_TERM_SELECTIVITY (term) =
	  1.0 / QO_NODE_NCARD (QO_TERM_TAIL (term));
      break;

    case PT_PATH_OUTER:
      {
	int t;
	QO_TERM *t_term;
	QO_NODE *t_node;

	/* Traverse previously generated terms */
	for (t = 0; t < env->nterms - 1; t++)
	  {

	    t_term = QO_ENV_TERM (env, t);

	    if (QO_TERM_CLASS (t_term) == QO_TC_PATH &&
		QO_TERM_JOIN_TYPE (t_term) == JOIN_LEFT)
	      {
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
	  }			/* for (t = ...) */
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

  /* set flag
   * TEMPORARY CODE (DO NOT REMOVE ME) */
  if (pt_expr->or_next == NULL)
    {
      QO_TERM_SET_FLAG (term, QO_TERM_SINGLE_PRED);
    }

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

  QO_ASSERT (env, attr->node_type == PT_NAME);

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
      if (!pt_is_query (pt_expr->info.expr.arg1) &&
	  !pt_is_query (pt_expr->info.expr.arg2))
	{
	  if (pt_expr->info.expr.op == PT_EQ)
	    {
	      return true;

	    }
	  else if (pt_expr->info.expr.op == PT_RANGE)
	    {
	      PT_NODE *between_and;

	      between_and = pt_expr->info.expr.arg2;
	      if (between_and->or_next == NULL &&	/* has only one range */
		  between_and->info.expr.op == PT_BETWEEN_EQ_NA)
		{
		  return true;
		}
	    }			/* else if */
	}
    }

  return false;
}

/*
 * expr_is_equi_join () - Test if the pt_expr is an equi-join conjunct whose
 *			  left and right sides are simple attribute references
 *   return: bool
 *   pt_expr(in):
 */
static bool
expr_is_equi_join (PT_NODE * pt_expr)
{
  if (pt_expr->or_next == NULL)
    {				/* keep out OR conjunct */
      if (pt_is_attr (pt_expr->info.expr.arg1))
	{
	  if (pt_expr->info.expr.op == PT_EQ)
	    {
	      return pt_is_attr (pt_expr->info.expr.arg2);

	    }
	  else if (pt_expr->info.expr.op == PT_RANGE)
	    {
	      PT_NODE *between_and;

	      between_and = pt_expr->info.expr.arg2;
	      if (between_and->or_next == NULL &&	/* has only one range */
		  between_and->info.expr.op == PT_BETWEEN_EQ_NA)
		{
		  return pt_is_attr (between_and->info.expr.arg1);
		}
	    }			/* else if */
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
  return (entity->info.spec.derived_table && (
					       /* this test is too pessimistic. The argument must depend
					        * on a previous entity spec in the from list.
					        * >>>> fix me some day <<<<
					        */
					       entity->info.spec.
					       derived_table_type ==
					       PT_IS_SET_EXPR ||
					       /* is cselect derived table of method */
					       entity->info.spec.
					       derived_table_type ==
					       PT_IS_CSELECT
					       || entity->info.spec.
					       derived_table->info.query.
					       correlation_level == 1));

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
    return;

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
    case PT_AND:
    case PT_OR:
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

    case PT_IS_NULL:
    case PT_IS_NOT_NULL:
      return RANK_EXPR_LIGHT;

      /* Group 2 -- medium */
    case PT_SETEQ:
    case PT_SETNEQ:
    case PT_SUPERSETEQ:
    case PT_SUPERSET:
    case PT_SUBSET:
    case PT_SUBSETEQ:

    case PT_POSITION:
    case PT_SUBSTRING:
    case PT_OCTET_LENGTH:
    case PT_BIT_LENGTH:

    case PT_CHAR_LENGTH:
    case PT_LOWER:
    case PT_UPPER:
    case PT_TRIM:

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
      return RANK_EXPR_MEDIUM;

      /* Group 3 -- heavy */
    case PT_LIKE:
    case PT_NOT_LIKE:

    case PT_ENCRYPT:
    case PT_DECRYPT:
      return RANK_EXPR_HEAVY;

    default:
      return RANK_EXPR_MEDIUM;
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
	  rank = get_opcode_rank (node->info.expr.op);
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
    return;

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
    return 0;
  else if (expr->node_type == PT_NAME)
    spec = expr->info.name.spec_id;
  else if (expr->node_type == PT_DOT_)
    spec = expr->info.dot.arg2->info.name.spec_id;
  else
    return false;

  return (pt_find_entity (env->parser,
			  env->pt_tree->info.query.q.select.from,
			  spec) != NULL);

}

/*
 * is_pseudo_const () -
 *   return: 1 iff the expression can server as a pseudo-constant
 *	     during predicate evaluation.  Used primarily to help
 *	     determine whether a predicate can be implemented
 *	     with an index scan
 *   env(in): The optimizer environment
 *   expr(in): The parse tree for the expression to examine
 */
static bool
is_pseudo_const (QO_ENV * env, PT_NODE * expr)
{
  if (expr == NULL)
    return false;
  switch (expr->node_type)
    {
    case PT_VALUE:
    case PT_HOST_VAR:
      return true;

    case PT_SELECT:
    case PT_UNION:
    case PT_DIFFERENCE:
    case PT_INTERSECTION:
      return expr->info.query.correlation_level != 1;

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
	case PT_PLUS:
	case PT_STRCAT:
	case PT_MINUS:
	case PT_TIMES:
	case PT_DIVIDE:
	  return (is_pseudo_const (env, expr->info.expr.arg1) &&
		  is_pseudo_const (env, expr->info.expr.arg2));
	case PT_UNARY_MINUS:
	  return is_pseudo_const (env, expr->info.expr.arg1);
	case PT_BETWEEN_AND:
	case PT_BETWEEN_GE_LE:
	case PT_BETWEEN_GE_LT:
	case PT_BETWEEN_GT_LE:
	case PT_BETWEEN_GT_LT:
	  return (is_pseudo_const (env, expr->info.expr.arg1) &&
		  is_pseudo_const (env, expr->info.expr.arg2));
	case PT_BETWEEN_EQ_NA:
	case PT_BETWEEN_INF_LE:
	case PT_BETWEEN_INF_LT:
	case PT_BETWEEN_GE_INF:
	case PT_BETWEEN_GT_INF:
	  return is_pseudo_const (env, expr->info.expr.arg1);
	case PT_MODULUS:
	  return (is_pseudo_const (env, expr->info.expr.arg1) &&
		  is_pseudo_const (env, expr->info.expr.arg2));
	case PT_RAND:
	case PT_DRAND:
	  return 1;
	case PT_FLOOR:
	case PT_CEIL:
	case PT_SIGN:
	case PT_ABS:
	case PT_CHR:
	case PT_EXP:
	case PT_SQRT:
	  return is_pseudo_const (env, expr->info.expr.arg1);
	case PT_POWER:
	case PT_ROUND:
	case PT_TRUNC:
	case PT_LOG:
	  return (is_pseudo_const (env, expr->info.expr.arg1) &&
		  is_pseudo_const (env, expr->info.expr.arg2));
	case PT_INSTR:
	  return (is_pseudo_const (env, expr->info.expr.arg1) &&
		  is_pseudo_const (env, expr->info.expr.arg2) &&
		  is_pseudo_const (env, expr->info.expr.arg3));
	case PT_POSITION:
	  return (is_pseudo_const (env, expr->info.expr.arg1) &&
		  is_pseudo_const (env, expr->info.expr.arg2));
	case PT_SUBSTRING:
	  return (is_pseudo_const (env, expr->info.expr.arg1) &&
		  is_pseudo_const (env, expr->info.expr.arg2) &&
		  (expr->info.expr.arg3 ?
		   is_pseudo_const (env, expr->info.expr.arg3) : 1));
	case PT_CHAR_LENGTH:
	case PT_OCTET_LENGTH:
	case PT_BIT_LENGTH:
	case PT_LOWER:
	case PT_UPPER:
	  return is_pseudo_const (env, expr->info.expr.arg1);
	case PT_TRIM:
	case PT_LTRIM:
	case PT_RTRIM:
	  return (is_pseudo_const (env, expr->info.expr.arg1) &&
		  (expr->info.expr.arg2 ?
		   is_pseudo_const (env, expr->info.expr.arg2) : 1));

	case PT_LPAD:
	case PT_RPAD:
	case PT_REPLACE:
	case PT_TRANSLATE:
	  return (is_pseudo_const (env, expr->info.expr.arg1) &&
		  is_pseudo_const (env, expr->info.expr.arg2) &&
		  (expr->info.expr.arg3 ?
		   is_pseudo_const (env, expr->info.expr.arg3) : 1));
	case PT_ADD_MONTHS:
	  return (is_pseudo_const (env, expr->info.expr.arg1) &&
		  is_pseudo_const (env, expr->info.expr.arg2));
	case PT_LAST_DAY:
	  return is_pseudo_const (env, expr->info.expr.arg1);
	case PT_MONTHS_BETWEEN:
	  return (is_pseudo_const (env, expr->info.expr.arg1) &&
		  is_pseudo_const (env, expr->info.expr.arg2));
	case PT_SYS_DATE:
	case PT_SYS_TIME:
	case PT_SYS_TIMESTAMP:
	case PT_LOCAL_TRANSACTION_ID:
	case PT_CURRENT_USER:
	  return 1;
	case PT_TO_CHAR:
	case PT_TO_DATE:
	case PT_TO_TIME:
	case PT_TO_TIMESTAMP:
	case PT_TO_NUMBER:
	  return (is_pseudo_const (env, expr->info.expr.arg1) &&
		  expr->info.expr.arg2 ?
		  is_pseudo_const (env, expr->info.expr.arg2) : 1);
	case PT_CURRENT_VALUE:
	case PT_NEXT_VALUE:
	  return 1;
	case PT_CAST:
	  return is_pseudo_const (env, expr->info.expr.arg1);
	case PT_CASE:
	case PT_DECODE:
	  return (is_pseudo_const (env, expr->info.expr.arg1) &&
		  is_pseudo_const (env, expr->info.expr.arg2));
	case PT_NULLIF:
	case PT_COALESCE:
	case PT_NVL:
	  return (is_pseudo_const (env, expr->info.expr.arg1) &&
		  is_pseudo_const (env, expr->info.expr.arg2));
	case PT_NVL2:
	  return (is_pseudo_const (env, expr->info.expr.arg1) &&
		  is_pseudo_const (env, expr->info.expr.arg2) &&
		  is_pseudo_const (env, expr->info.expr.arg3));
	case PT_EXTRACT:
	  return is_pseudo_const (env, expr->info.expr.arg1);
	case PT_LEAST:
	case PT_GREATEST:
	  return (is_pseudo_const (env, expr->info.expr.arg1) &&
		  is_pseudo_const (env, expr->info.expr.arg2));
	default:
	  return 0;
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
	if (expr->info.function.function_type != F_SET &&
	    expr->info.function.function_type != F_MULTISET &&
	    expr->info.function.function_type != F_SEQUENCE)
	  return false;
	for (p = expr->info.function.arg_list; p; p = p->next)
	  if (!is_pseudo_const (env, p))
	    return false;
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
  tmp = NALLOCATE (env, QO_SUBQUERY, n + 1);
  memcpy (tmp, env->subqueries, n * sizeof (QO_SUBQUERY));
  for (i = 0; i < n; i++)
    {
      QO_SUBQUERY *subq;
      subq = &env->subqueries[i];
      BITSET_MOVE (tmp[i].segs, subq->segs);
      BITSET_MOVE (tmp[i].nodes, subq->nodes);
      BITSET_MOVE (tmp[i].terms, subq->terms);
    }
  DEALLOCATE (env, env->subqueries);
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

  /*
   * Set the next list pointer to NULL so that we don't walk the next
   * pointer list.  Restore after the walk.
   *
   * Same for the select list; we don't want to grab any subqueries
   * from the select list here because they will be taken care of in
   * the call to pt_set_dptr() in qo_to_xasl().  All of the segments
   * needed to evaluate those subqueries will be properly projected out
   * of any subplans because they will look like final segments.  We
   * also know that we don't want to evaluate those subqueries until
   * the very end (i.e., on the dptr list of the topmost XASL node), so
   * there's no point searching for some other spot in the XASL tree at
   * which to pin them.  This also has the advantage of not getting
   * screwed up by merge joins, which tend to make the decision about
   * where to pin subqueries more complicated.
   */
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
  int last_ordered_idx;

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

  if (hint & PT_HINT_W)
    {				/* not used */
    }
  if (hint & PT_HINT_X)
    {				/* not used */
    }
  if (hint & PT_HINT_Y)
    {				/* not used */
    }

  if (hint & PT_HINT_USE_NL)
    {
      add_hint_args (env, tree->info.query.q.select.use_nl, PT_HINT_USE_NL);
    }

  if (hint & PT_HINT_USE_IDX)
    {
      add_hint_args (env, tree->info.query.q.select.use_idx, PT_HINT_USE_IDX);
    }

  if (hint & PT_HINT_USE_MERGE)
    {
      add_hint_args (env, tree->info.query.q.select.use_merge,
		     PT_HINT_USE_MERGE);
    }

  if (hint & PT_HINT_USE_HASH)
    {				/* not used */
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
  int i, n;
  QO_NODE *nodep;
  QO_USING_INDEX *uip;
  PT_NODE *indexp;

  if (!using_index)
    /* no USING INDEX clause in the query;
       all QO_NODE_USING_INDEX(node) will contain NULL */
    return;

  /* for each node */
  for (i = 0; i < env->nnodes; i++)
    {
      nodep = QO_ENV_NODE (env, i);

      /* count number of indexes for this node */
      n = 0;
      for (indexp = using_index; indexp; indexp = indexp->next)
	{
	  if (indexp->info.name.original == NULL &&
	      indexp->info.name.resolved == NULL)
	    break;		/* USING INDEX NONE case */
	  if (indexp->info.name.original == NULL &&
	      indexp->info.name.resolved[0] == '*')
	    n++;		/* USING INDEX ALL EXCEPT case */
	  if (indexp->info.name.original &&
	      !intl_mbs_casecmp (QO_NODE_NAME (nodep),
				 indexp->info.name.resolved))
	    n++;
	}
      /* if n == 0, it means that either no indexes in USING INDEX clause for
         this node or USING INDEX NONE case */

      /* allocate QO_USING_INDEX structure */
      uip = QO_NODE_USING_INDEX (nodep) =
	(n == 0) ? ALLOC_USING_INDEX (env, 1) : ALLOC_USING_INDEX (env, n);
      QO_UI_N (uip) = n;
      /* attach indexes to QO_NODE */
      n = 0;
      for (indexp = using_index; indexp; indexp = indexp->next)
	{
	  if (indexp->info.name.original == NULL &&
	      indexp->info.name.resolved == NULL)
	    break;		/* USING INDEX NONE case */
	  if (indexp->info.name.original == NULL &&
	      indexp->info.name.resolved[0] == '*')
	    {
	      /* USING INDEX ALL EXCEPT case */
	      QO_UI_INDEX (uip, n) = indexp->info.name.resolved;
	      QO_UI_FORCE (uip, n++) = (int) (indexp->etc);
	    }
	  if (indexp->info.name.original &&
	      !intl_mbs_casecmp (QO_NODE_NAME (nodep),
				 indexp->info.name.resolved))
	    {
	      QO_UI_INDEX (uip, n) = indexp->info.name.original;
	      QO_UI_FORCE (uip, n++) = (int) (indexp->etc);
	    }
	}

    }				/* for (i = 0; i < env->nnodes; i++) */
}
