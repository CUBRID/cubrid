/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * planner.h - Query optimization search coordinator
 * TODO: merge this file to query_planner.h
 */

#ifndef _PLANNER_H_
#define _PLANNER_H_

#ident "$Id$"

#include "qo.h"
#include "bitset.h"

#define NPLANS		4	/* Maximum number of plans to keep in a PlanVec */

typedef struct qo_planvec QO_PLANVEC;
struct qo_planvec
{
  bool overflow;
  int nplans;
  QO_PLAN *plan[NPLANS];

};


struct qo_info
{
  struct qo_info *next;

  /*
   * An Info node holds the best plans that we have generated for a
   * particular combination of joins.  It holds one plan for every
   * possible join ordering that could be produced for the combination
   * (even if some of those orderings are not directly required by
   * upper joins, it may turn out to be cheaper to use one of them and
   * then sort), and also keeps track of the best possible (lowest
   * cost) plan without regard to ordering.  This plan may in fact be
   * unordered, or it may be one of the other plans that is already
   * being remembered.
   */

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
  int projected_size;
  double cardinality;

  /*
   * One plan for each equivalence class, in each case the best we have
   * seen so far.  This vector is NULL after a node is detached.
   */
  QO_PLANVEC *planvec;

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
  unsigned int N;

  /*
   * The join terms (e.g., employee.dno = dept.dno); there are T of
   * them, E of which are actual edges in the join graph.  node_mask is
   * a bit mask used to mask out non-node terms from node bitsets.
   * M is simply 2^N, and is the size of the join_info vector that will 
   * be allocated.
   */
  QO_TERM *term;
  unsigned int E, M, T;
  unsigned long node_mask;

  /*
   * The last join level.
   */
  int join_unit;

  /*
   * The path segments involved in the various join terms, and the
   * equivalence classes implied by those joins (e.g., if we have join
   * terms c1 = c2 and c2 = c3, (c1,c2,c3) is an equivalence class for
   * the purposes of determining sort orderings).
   */
  QO_SEGMENT *segment;
  unsigned int S;

  QO_EQCLASS *eqclass;
  unsigned int EQ;

  /*
   * The partitions (strong components) of the join graph.
   */
  QO_PARTITION *partition;
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

  /*
   * The memo arrays that record the current best solutions for
   * different (partial) join combinations.  node_info is indexed by
   * node number; node_info[i] holds information about the costs of
   * the various scans available on node[i].  edge_info is indexed
   * by bitset; edge_info[i] holds information on the best ways seen so
   * far of combining all of the edges represented by the set bits
   * of i.  (That is, if bit j of i is set, then edge[j] is an element
   * of the combination described by edge_info[i].)  best_info is just
   * a shortcut way of getting at the information for the entire
   * collection.
   */
  QO_INFO **node_info;
  QO_INFO **join_info;
  QO_INFO **cp_info;
  QO_INFO *best_info;

  QO_PLAN *worst_plan;
  QO_INFO *worst_info;

  /* alloced info list
   */
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
extern void qo_info_stats (FILE *);

#endif /* _PLANNER_H_ */
