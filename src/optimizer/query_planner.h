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
 * query_planner.h - Query plan
 */

#ifndef _QUERY_PLANNER_H_
#define _QUERY_PLANNER_H_

#ident "$Id$"

#if defined (SERVER_MODE)
#error Does not belong to server module
#endif /* SERVER_MODE */

#include "optimizer.h"
#include "query_bitset.h"

// forward definitions
struct xasl_node;
namespace cubxasl
{
  struct analytic_eval_type;
}

#define QO_CPU_WEIGHT   0.0025

typedef enum
{
  QO_PLANTYPE_SCAN,
  QO_PLANTYPE_SORT,
  QO_PLANTYPE_JOIN,
  QO_PLANTYPE_FOLLOW,
  QO_PLANTYPE_WORST
} QO_PLANTYPE;

typedef enum
{
  QO_SCANMETHOD_SEQ_SCAN,
  QO_SCANMETHOD_INDEX_SCAN,
  QO_SCANMETHOD_INDEX_ORDERBY_SCAN,
  QO_SCANMETHOD_INDEX_GROUPBY_SCAN,
  QO_SCANMETHOD_INDEX_SCAN_INSPECT,
  QO_SCANMETHOD_INDEX_SCAN_OPTIMIZED
} QO_SCANMETHOD;

typedef enum
{
  QO_JOINMETHOD_NL_JOIN,
  QO_JOINMETHOD_IDX_JOIN,
  QO_JOINMETHOD_MERGE_JOIN
} QO_JOINMETHOD;

typedef struct qo_plan_vtbl QO_PLAN_VTBL;
struct qo_plan_vtbl
{
  const char *plan_string;
  void (*fprint_fn) (QO_PLAN *, FILE *, int);
  void (*walk_fn) (QO_PLAN *, void (*)(QO_PLAN *, void *), void *, void (*)(QO_PLAN *, void *), void *);
  void (*free_fn) (QO_PLAN *);
  void (*cost_fn) (QO_PLAN *);
  void (*default_cost) (QO_PLAN *);
  void (*info_fn) (QO_PLAN *, FILE *, int);
  const char *info_string;
};

typedef enum
{
  PLAN_COMP_UNK = -2,
  PLAN_COMP_LT = -1,
  PLAN_COMP_EQ = 0,
  PLAN_COMP_GT = 1
} QO_PLAN_COMPARE_RESULT;

typedef enum
{
  PLAN_MULTI_RANGE_OPT_USE = 1,
  PLAN_MULTI_RANGE_OPT_NO = 0,
  PLAN_MULTI_RANGE_OPT_CANNOT_USE = -1,
  PLAN_MULTI_RANGE_OPT_CAN_USE = -2
} QO_PLAN_ULTI_RANGE_OPT_USE;

struct qo_plan
{
  QO_INFO *info;

  int refcount;

  /*
   * A plan is "top-rooted" if it is a top level plan
   */
  bool top_rooted;

  /*
   * A plan is "well-rooted" if it is a scan plan, or if it is a follow
   * plan whose subplan is itself well-rooted.  These are plans that
   * won't require the construction of any temporary files during
   * execution.  The current generation of XASL can't cope with
   * temporary files for some kinds of queries (in particular, those
   * with subqueries in the select list), so we have to be sure to
   * avoid generating plans that require that kind of implementation.
   */
  bool well_rooted;

  /* CPU and IO cost which are fixed against join position(inner or outer) */
  double fixed_cpu_cost, fixed_io_cost;
  /* CPU and IO cost which are variable according to join position */
  double variable_cpu_cost, variable_io_cost;
  BITSET sarged_terms;
  QO_EQCLASS *order;
  PT_NODE *iscan_sort_list;	/* sorting fields */

  /*
   * The set of correlated subqueries that are "covered" by this plan.
   * These are the subqueries that must be reevaluated every time a new
   * candidate row is produced by this plan.
   */
  BITSET subqueries;

  QO_PLANTYPE plan_type;
  QO_PLAN_VTBL *vtbl;

  union
  {
    struct
    {
      QO_PLAN *link;
    } free;

    struct
    {
      QO_SCANMETHOD scan_method;	/* SEQ_SCAN, INDEX_SCAN */
      QO_NODE *node;
      BITSET terms;
      BITSET kf_terms;
      bool index_equi;
      bool index_cover;		/* covered index scan flag */
      bool index_iss;		/* index skip scan flag */
      bool index_loose;		/* loose index scan flag */
      QO_NODE_INDEX_ENTRY *index;
      BITSET multi_col_range_segs;	/* range condition segs for multi_col_term */
      BITSET hash_terms;	/* hash_terms for hash list scan */
    } scan;

    /*
     * Sort nodes are now really "build a temp file" nodes; the
     * created temp file may be sorted or unsorted.  If sorted, the
     * `order' field indicates the sorting order; if unsorted, the
     * `order' field is NULL.  The `tmpnum' field is assigned during
     * plan finalization; it is the logical number of the temporary
     * file to be created at runtime.
     */
    struct
    {
      SORT_TYPE sort_type;
#if 0				/* DO NOT DELETE ME - need future work */
      SORT_LIST *sort_list;
#endif
      QO_PLAN *subplan;
      xasl_node *xasl;
    } sort;

    struct
    {
      JOIN_TYPE join_type;	/* JOIN_INNER, _LEFT, _RIGHT, _OUTER */
      QO_JOINMETHOD join_method;	/* NL_JOIN, MERGE_JOIN */
      QO_PLAN *outer;
      QO_PLAN *inner;
      BITSET join_terms;	/* all join edges */
      BITSET during_join_terms;	/* during join terms */
      BITSET other_outer_join_terms;	/* for merge outer join only */
      BITSET after_join_terms;	/* after join terms */
      BITSET hash_terms;	/* hash_terms for hash list scan */
    } join;

    struct
    {
      QO_PLAN *head;
      QO_TERM *path;
    } follow;

  } plan_un;

  QO_PLAN_ULTI_RANGE_OPT_USE multi_range_opt_use;	/* used to determine if this plan uses multi range opt */
  // *INDENT-OFF*
  cubxasl::analytic_eval_type *analytic_eval_list;	/* analytic evaluation list */
  // *INDENT-ON*
  bool has_sort_limit;		/* true if this plan or one if its subplans is a SORT-LIMIT plan */
  bool use_iscan_descending;
};

#define qo_plan_add_ref(p)	((p->refcount)++, (p))
#define qo_plan_del_ref(p)	do {					      \
					QO_PLAN *__p = (p);		      \
					if ((__p) && --(__p->refcount) == 0) \
					    qo_plan_free(__p);		      \
				    } while(0)
#define qo_plan_release(p)	do {					      \
					QO_PLAN *__p = (p);		      \
					if ((__p) && (__p->refcount) == 0) \
					    qo_plan_free(__p);		      \
				    } while(0)

#define NPLANS		4	/* Maximum number of plans to keep in a PlanVec */

typedef struct qo_planvec QO_PLANVEC;
struct qo_planvec
{
  QO_PLAN *plan[NPLANS];
  int nplans;
  bool overflow;
};

struct qo_info
{
  struct qo_info *next;

  /*
   * The environment relative to which all of the following sets, etc.
   * make sense.
   */
  QO_ENV *env;

  /*
   * The Planner instance to which this Info node belongs.  I wish
   * we didn't have to do this, but there are just enough occassions
   * where we need the back pointer that it is easier just to include
   * it and be done with it.  This pointer is NULL if this info node
   * has been "detached" from the planner; this happens when the
   * winning plan is "finalized".
   */
  QO_PLANNER *planner;

  /*
   * The lowest-cost plan without regard to ordering.
   */
  QO_PLANVEC best_no_order;

  /*
   * The set of nodes joined by the plans at this node.
   */
  BITSET nodes;

  /*
   * All of the terms accounted for by the plans in this node and their
   * descendents, i.e., the complement of the terms remaining to be
   * dealt with.
   */
  BITSET terms;

  /*
   * The equivalence classes represented by all of the attributes
   * (segments) joined together in this node.
   */
  BITSET eqclasses;

  /*
   * 'projected_segs' is the set of segments (attributes) that need to
   * be projected from this plan produced by this node in order to
   * satisfy the needs of upper level plans.  'projected_size' is the
   * number of bytes per record required by 'projected_segs'.
   * 'cardinality' is the estimated cardinality of the results produced
   * by plans at this node.
   */
  BITSET projected_segs;
  double cardinality;

  /*
   * One plan for each equivalence class, in each case the best we have
   * seen so far.  This vector is NULL after a node is detached.
   */
  QO_PLANVEC *planvec;

  int projected_size;

  /*
   * The last join level.
   */
  int join_unit;

  /*
   * `detached' is true iff the node has been detached; we can no
   * longer just use the value of `plans' as the indicator because
   * dependent derived tables can give rise to join graphs that couple
   * nodes together without creating an equivalence class, and thus
   * without the need to populate the `plans' vector.
   */
  int detached;
};

struct qo_planner
{
  /*
   * The struct that encapsulates the information involved in searching
   * for an optimal query plan.
   */

  /*
   * The environment that supplies the various nodes, edges, segments, etc.
   */
  QO_ENV *env;

  /*
   * The relations being considered in this join; there are N of them.
   */
  QO_NODE *node;

  /*
   * The join terms (e.g., employee.dno = dept.dno); there are T of
   * them, E of which are actual edges in the join graph.  node_mask is
   * a bit mask used to mask out non-node terms from node bitsets.
   * M is simply 2^N, and is the size of the join_info vector that will
   * be allocated.
   */
  QO_TERM *term;
  unsigned int N;
  unsigned int E, M, T;
  unsigned long node_mask;

  /*
   * The path segments involved in the various join terms, and the
   * equivalence classes implied by those joins (e.g., if we have join
   * terms c1 = c2 and c2 = c3, (c1,c2,c3) is an equivalence class for
   * the purposes of determining sort orderings).
   */
  QO_SEGMENT *segment;

  QO_EQCLASS *eqclass;

  /*
   * The partitions (strong components) of the join graph.
   */
  QO_PARTITION *partition;

  /*
   * The last join level.
   */
  int join_unit;
  unsigned int S;
  unsigned int EQ;
  unsigned int P;

  /*
   * The (level-1 correlated) subqueries used in this query.
   */
  QO_SUBQUERY *subqueries;
  BITSET all_subqueries;
  unsigned int Q;

  /*
   * The final set of segments to be projected out of the top-level
   * plan produced by this planner.
   */
  BITSET final_segs;


  QO_INFO **node_info;
  QO_INFO **join_info;
  QO_INFO **cp_info;
  QO_INFO *best_info;

  QO_PLAN *worst_plan;
  QO_INFO *worst_info;

  /* alloced info list */
  QO_INFO *info_list;

  /*
   * true iff qo_planner_cleanup() needs to be called before freeing
   * this planner.  This is needed to help clean up after aborts, when
   * control flow takes an unexpected longjmp.
   */
  int cleanup_needed;
};

extern QO_PLAN *qo_planner_search (QO_ENV *);
extern void qo_planner_free (QO_PLANNER *);
extern void qo_plans_stats (FILE *);
extern void qo_info_stats (FILE *);

extern bool qo_is_seq_scan (QO_PLAN *);
extern bool qo_is_iscan (QO_PLAN *);
extern bool qo_is_iscan_from_groupby (QO_PLAN *);
extern bool qo_is_iscan_from_orderby (QO_PLAN *);
extern bool qo_is_interesting_order_scan (QO_PLAN *);
extern bool qo_is_all_unique_index_columns_are_equi_terms (QO_PLAN * plan);
extern bool qo_has_sort_limit_subplan (QO_PLAN * plan);
#endif /* _QUERY_PLANNER_H_ */
