/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * query_planner.h - Query plan
 * TODO: rename this file to query_planner.h and include query_planer_2.h
 */

#ifndef _QUERY_PLANNER_H_
#define _QUERY_PLANNER_H_

#ident "$Id$"

#include "qo.h"
#include "bitset.h"

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
  QO_SCANMETHOD_INDEX_SCAN
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
  void (*walk_fn) (QO_PLAN *, void (*)(QO_PLAN *, void *), void *,
		   void (*)(QO_PLAN *, void *), void *);
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
      bool equi;
      BITSET kf_terms;
      QO_NODE_INDEX_ENTRY *index;
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
      XASL_NODE *xasl;
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
    } join;

    struct
    {
      QO_PLAN *head;
      QO_TERM *path;
    } follow;

  } plan_un;
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

extern QO_PLAN *qo_seq_scan_new (QO_INFO *, QO_NODE *, BITSET *);
extern QO_PLAN *qo_index_scan_new (QO_INFO *, QO_NODE *,
				   QO_NODE_INDEX_ENTRY *, BITSET *, BITSET *,
				   BITSET *);
extern QO_PLAN *qo_sort_new (QO_PLAN *, QO_EQCLASS *, SORT_TYPE);
extern QO_PLAN *qo_join_new (QO_INFO *, JOIN_TYPE, QO_JOINMETHOD, QO_PLAN *,
			     QO_PLAN *, BITSET *, BITSET *, BITSET *,
			     BITSET *, BITSET *);
extern QO_PLAN *qo_follow_new (QO_INFO *, QO_PLAN *, QO_TERM *, BITSET *,
			       BITSET *);
extern QO_PLAN *qo_cp_new (QO_INFO *, QO_PLAN *, QO_PLAN *, BITSET *,
			   BITSET *);
extern QO_PLAN *qo_worst_new (QO_ENV *);

extern QO_PLAN *qo_plan_malloc (QO_ENV *);
extern void qo_plan_free (QO_PLAN *);
extern QO_PLAN_COMPARE_RESULT qo_plan_cmp (QO_PLAN *, QO_PLAN *);
extern void qo_plan_fprint (QO_PLAN *, FILE *, int, const char *);
extern QO_PLAN *qo_plan_order_by (QO_PLAN *, QO_EQCLASS *);

extern QO_PLAN *qo_plan_finalize (QO_PLAN *);

extern void qo_plan_walk (QO_PLAN *, void (*)(QO_PLAN *, void *), void *,
			  void (*)(QO_PLAN *, void *), void *);

extern void qo_plans_init (QO_ENV * env);
extern void qo_plans_teardown (QO_ENV * env);
extern void qo_plans_stats (FILE *);

extern void qo_nljoin_cost (QO_PLAN *);

extern void qo_plan_add_to_free_list (QO_PLAN *, void *ignore);

#if !defined(WINDOWS)
extern double log2 (double);
#endif

#endif /* _QUERY_PLANNER_H_ */
