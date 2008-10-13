/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * env.c - Query environment scaffolding
 * TODO: merge this file to query_graph_2.c
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <math.h>
#if !defined(WINDOWS)
#include <values.h>
#endif /* !WINDOWS */

#include "error_code.h"

#include "optimizer.h"
#include "query_graph_1.h"
#include "query_planner_1.h"
#include "query_planner_2.h"
#include "query_graph_2.h"

#include "schema_manager_3.h"
#include "statistics.h"
#include "system_parameter.h"
#include "parser.h"
#include "environment_variable.h"
#include "parser.h"
#include "xasl_generation_2.h"

#include "memory_manager_2.h"

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

double QO_INFINITY = 0.0;

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
				 BITSET * termsp);
static void qo_find_index_seg_terms (QO_ENV * env, int seg_idx,
				     BITSET * seg_equal_termsp,
				     BITSET * seg_other_termsp);
static bool qo_find_index_segs (QO_ENV *,
				SM_CLASS_CONSTRAINT *, QO_NODE *,
				int *, int, int *, BITSET *);
static void qo_find_node_indexes (QO_ENV *, QO_NODE *);
static int is_equivalent_indexes (QO_INDEX_ENTRY * index1,
				  QO_INDEX_ENTRY * index2);
static int qo_find_matching_index (QO_INDEX_ENTRY * index_entry,
				   QO_INDEX * class_indexes);
static QO_INDEX_ENTRY *is_index_compatible (QO_CLASS_INFO * class_info,
					    int n,
					    QO_INDEX_ENTRY * index_entry);


/*
 * qo_env_new () -
 *   return:
 *   parser(in):
 *   query(in):
 */
QO_ENV *
qo_env_new (PARSER_CONTEXT * parser, PT_NODE * query)
{
  QO_ENV *env;

  env = (QO_ENV *) malloc (sizeof (QO_ENV));
  if (env == NULL)
    return NULL;

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
  env->dump_enable = PRM_QO_DUMP;
  bitset_init (&(env->fake_terms), env);

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

}				/* qo_malloc */
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
  er_set (ER_WARNING_SEVERITY, file, line, ER_QO_FAILED_ASSERTION, 0);
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
	    qo_seg_free (QO_ENV_SEG (env, i));
	  DEALLOCATE (env, env->segs);
	}

      if (env->nodes)
	{
	  for (i = 0; i < env->Nnodes; ++i)
	    qo_node_free (QO_ENV_NODE (env, i));
	  DEALLOCATE (env, env->nodes);
	}

      if (env->eqclasses)
	{
	  for (i = 0; i < env->neqclasses; ++i)
	    qo_eqclass_free (QO_ENV_EQCLASS (env, i));
	  DEALLOCATE (env, env->eqclasses);
	}

      if (env->terms)
	{
	  for (i = 0; i < env->Nterms; ++i)
	    qo_term_free (QO_ENV_TERM (env, i));
	  DEALLOCATE (env, env->terms);
	}

      if (env->partitions)
	{
	  for (i = 0; i < env->npartitions; ++i)
	    qo_partition_free (QO_ENV_PARTITION (env, i));
	  DEALLOCATE (env, env->partitions);
	}

      if (env->subqueries)
	{
	  for (i = 0; i < env->nsubqueries; ++i)
	    qo_subquery_free (&env->subqueries[i]);
	  DEALLOCATE (env, env->subqueries);
	}

      bitset_delset (&(env->final_segs));
      bitset_delset (&(env->fake_terms));

      if (env->planner)
	qo_planner_free (env->planner);

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
   * We need to swap the contents of t0 and t1.  This is a little
   * trickier than it sounds, for two reasons:
   *
   *   1. Each term has an element (idx) that identifies that terms
   *      ordinal position in the array of terms; when we swap the
   *      terms we mustn't swap the idx values.
   *
   *   2. Each term has several internal bitsets, and these bitsets
   *      (may) contain self-relative pointers.  Blindly copying the
   *      bits over will destroy the necessary relationships; instead,
   *      the bitsets have to be copied using the EXCHANGE macro.
   */

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
void
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

  /*
   * This loop rearranges the terms so that all of the terms that give
   * rise to edges are contiguous in the lower part of the terms
   * vector.  This is allows the term indexes to coincide with the edge
   * indexes, which is particularly convenient in the planner (since it
   * needs to keep sets of terms and edges concurrently).
   *
   * LOOP INVARIANTS:
   *  For all m < i, term[m] is an edge.
   *  For all m >= n, term[m] is not an edge.
   */
  i = 0;
  n = env->nterms;

  while (i < n)
    {
      term = QO_ENV_TERM (env, i);
      if (QO_IS_EDGE_TERM (term))
	{
	  ++env->nedges;
	  ++i;
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
	    qo_exchange (term1, term2);
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
	    qo_exchange (term1, term2);
	}
    }

  /*
   * The terms vector is now partitioned into two contiguous sections:
   * the lower section (the interval [0,i)) contains terms that qualify
   * as (and produce) join graph edges, while the upper section (the
   * interval [i,nterms)) contains terms that don't qualify as edges.
   * These latter terms need to be examined to see if they qualify as
   * sargs for any of the join graph nodes.
   *
   * IMPORTANT: since this routine may change the idx values of terms,
   * it is important that no one initialize bitsets of term indexes
   * until the sorting is finished.
   */
  for (n = env->nterms; i < n; ++i)
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
  for (i = 0, n = env->nedges; i < n; ++i)
    {
      edge = QO_ENV_TERM (env, i);
      QO_ASSERT (env, QO_TERM_HEAD (edge) != NULL
		 && QO_TERM_TAIL (edge) != NULL);

      /*
       * If any join term is a non-inner join, make sure that all other
       * join terms for the same set of nodes is also a non-inner join.
       * This code probably isn't strong enough for the general case,
       * but right now it will only be confronted with a choice between
       * JOIN_INNER and JOIN_LEFT cases, and in those cases all terms
       * should be coerced to JOIN_LEFT.  I have no idea how this
       * should change if it ever becomes the case that one could have
       * to choose between (for example) JOIN_INNER, JOIN_LEFT, and
       * JOIN_RIGHT terms.
       *
       * This is part of the solution: where "is null"
       * predicates were interacting badly with path expressions.
       *
       * This is also a quadratic loop, but since the number of terms
       * that we'll ever look at should be pretty small (e.g., usually
       * less than 10) that probably doesn't matter.  If it starts to
       * be a problem some more clever solution will be necessary.
       */
      if (QO_TERM_JOIN_TYPE (edge) != JOIN_INNER &&
	  QO_TERM_CLASS (edge) != QO_TC_JOIN)
	{
	  for (j = 0; j < n; j++)
	    {
	      edge2 = QO_ENV_TERM (env, j);
	      if (i != j
		  && bitset_is_equivalent (&(QO_TERM_NODES (edge)),
					   &(QO_TERM_NODES (edge2))))
		QO_TERM_JOIN_TYPE (edge2) = QO_TERM_JOIN_TYPE (edge);
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
 * qo_find_index_terms () - Find the terms which contain the passed segments
 *   return:
 *   env(in): The environment used
 *   segsp(in): Passed BITSET of interested segments
 *   termsp(in): Returned BITSET of terms which contain the segments
 */
static void
qo_find_index_terms (QO_ENV * env, BITSET * segsp, BITSET * termsp)
{
  int t;
  QO_TERM *qo_termp;

  BITSET_CLEAR (*termsp);

  /* traverse all terms */
  for (t = 0; t < env->nterms; t++)
    {
      /* get the pointer to QO_TERM structure */
      qo_termp = QO_ENV_TERM (env, t);

      /* Fake terms (e.g., dependency links) won't have pt_expr's associated
         with them. They can't be implemented as indexed sargs, either,
         so don't worry about them here. */
      if (!QO_TERM_PT_EXPR (qo_termp))
	continue;
      /* 'graph.c:analyze_term()' function verifies that all indexable
         terms are expression so that they have 'pt_expr' filed of type
         PT_EXPR. */

      /* if the segments that give rise to the term are in the given segment
         set */
      if (bitset_intersects (&(QO_TERM_SEGS (qo_termp)), segsp))
	/* collect this term */
	bitset_add (termsp, t);

    }				/* for (t = 0; t < env->nterms; t++) */

}				/* qo_find_index_terms */

/*
 * qo_find_index_seg_terms () - Find the terms which contain the passed segment.
 *                               Only indexable and SARG terms are included
 *   return:
 *   env(in): The environment used
 *   seg_idx(in): Passed idx of an interested segment
 *   seg_equal_termsp(in): Returned BITSET of equal erms which contain
 *			   the segments and its class is TC_SARG
 *   seg_other_termsp(in):
 */
static void
qo_find_index_seg_terms (QO_ENV * env, int seg_idx,
			 BITSET * seg_equal_termsp, BITSET * seg_other_termsp)
{
  int t;
  QO_TERM *qo_termp;

  BITSET_CLEAR (*seg_equal_termsp);
  BITSET_CLEAR (*seg_other_termsp);

  /* traverse all terms */
  for (t = 0; t < env->nterms; t++)
    {
      /* get the pointer to QO_TERM structure */
      qo_termp = QO_ENV_TERM (env, t);

      /* ignore this term if it is not marked as indexable by
         'graph.c:analyze_term()' */
      if (!qo_termp->can_use_index)
	continue;

      /* Fake terms (e.g., dependency links) won't have pt_expr's associated
         with them. They can't be implemented as indexed sargs, either,
         so don't worry about them here. */
      if (!QO_TERM_PT_EXPR (qo_termp))
	continue;
      /* 'graph.c:analyze_term()' function verifies that all indexable
         terms are expression so that they have 'pt_expr' filed of type
         PT_EXPR. */

      /* if the term is sarg and the given segment is involed in the
         expression that gives rise to the term */
      if ((QO_TERM_CLASS (qo_termp) == QO_TC_SARG) &&
	  (BITSET_MEMBER (QO_TERM_SEGS (qo_termp), seg_idx)))
	{
	  /* collect this term */
	  if (QO_TERM_IS_FLAGED (qo_termp, QO_TERM_EQUAL_OP))
	    {
	      bitset_add (seg_equal_termsp, t);
	    }
	  else
	    {
	      bitset_add (seg_other_termsp, t);
	    }
	}

    }				/* for (t = 0; t < env->nterms; t++) */

}				/* qo_find_index_seg_terms() */

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
    return false;

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
      if ((QO_INDEX_INDEX (class_indexes, i)->next == NULL) &&
	  is_equivalent_indexes (index_entry,
				 QO_INDEX_INDEX (class_indexes, i)))
	break;
    }

  /*
   *  If a match is found, return its index, otherwise return -1
   */
  if (i < class_indexes->n)
    return i;
  else
    return -1;
}

/*
 * is_index_compatible () -
 *   return: int (True/False)
 *   class_info(in): Class info structure
 *   n(in): Index into class info structure.  This determines the level
 *          in the class heirarchy that we're currently concerned with
 *   index_entry(in): Index entry to match against
 *
 * Note:
 *     This is a recursive function which is used to verify that a
 *     given index entry is compatible accross the class heirarchy.
 *     An index entry is compatible if there exists an index definition
 *     on the same sequence of attributes at each level in the class
 *     heirarchy.  If the index entry is compatible, the entry will be
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
    return NULL;

  class_entry = &(class_info->info[n]);
  class_indexes = class_entry->index;

  i = qo_find_matching_index (index_entry, class_indexes);
  if (i < 0)
    return NULL;

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
	return NULL;
      else
	return index;
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

  /* working set; indexed segments */
  bitset_init (&working, env);
  bitset_assign (&working, &(QO_NODE_SEGS (nodep)));

  /* for each attribute of this constraint */
  for (i = 0; consp->attributes[i] && *nseg_idxp < seg_idx_num; i++)
    {

      attrp = consp->attributes[i];

      matched = false;
      /* for each indexed segments of this node, compare the name of the
         segment with the one of the attribute */
      for (iseg = bitset_iterate (&working, &iter);
	   iseg != -1; iseg = bitset_next_member (&iter))
	{

	  segp = QO_ENV_SEG (env, iseg);

	  if (!intl_mbs_casecmp (QO_SEG_NAME (segp), attrp->header.name))
	    {

	      bitset_add (segs, iseg);	/* add the segment to the index segment set */
	      bitset_remove (&working, iseg);	/* remove the segment from the working set */
	      seg_idx[i] = iseg;	/* remember the order of the index segments */
	      (*nseg_idxp)++;	/* number of index segments, 'seg_idx[]' */
	      /* If we're handling with a multi-column index, then only
	         equality expressions are allowed except for the last
	         matching segment. */
	      matched = true;
	      break;
	    }			/* if (!intl_mbs_casecmp...) */

	}			/* for (iseg = bitset_iterate(&working, &iter); ...) */

      if (!matched)
	{
	  seg_idx[i] = -1;	/* not found matched segment */
	  (*nseg_idxp)++;	/* number of index segments, 'seg_idx[]' */
	}			/* if (!matched) */

    }				/* for (i = 0; consp->attributes[i]; i++) */

  bitset_delset (&working);

  return (seg_idx[0] != -1) ? true : false;
  /* this index is feasible to use if at least the first attribute of index
     is specified(matched) */
}				/* qo_find_index_segs() */

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
  bool found;
  BITSET index_segs, index_terms;

  /* information of classes underlying this node */
  class_infop = QO_NODE_INFO (nodep);

  if (class_infop->n <= 0)
    return;			/* no classes, nothing to do process */


  /* for each class in the hierarachy, search the class constraint cache
     looking for applicable indexes(UNIQUE and INDEX constraint) */
  for (i = 0; i < class_infop->n; i++)
    {

      /* class information entry */
      class_entryp = &(class_infop->info[i]);
      /* get constraints of the class */
      constraints = sm_class_constraints (class_entryp->mop);

      /* count the number of INDEX and UNIQUE constraints contatined in this
         class */
      n = 0;
      for (consp = constraints; consp; consp = consp->next)
	{
	  if (SM_IS_CONSTRAINT_INDEX_FAMILY (consp->type))
	    n++;
	}
      /* allocate room for the constraint indexes */
      /* we don't have apriori knowledge about which constraints will be
         applied, so allocate room for all of them */
      /* qo_alloc_index(env, n) will allocate QO_INDEX structure and
         QO_INDEX_ENTRY structure array */
      indexp = class_entryp->index = qo_alloc_index (env, n);

      indexp->n = 0;

      /* for each constraint of the class */
      for (consp = constraints; consp; consp = consp->next)
	{

	  if (!SM_IS_CONSTRAINT_INDEX_FAMILY (consp->type))
	    continue;		/* neither INDEX nor UNIQUE constraint, skip */

	  uip = QO_NODE_USING_INDEX (nodep);
	  j = -1;
	  if (uip)
	    {
	      if (QO_UI_N (uip) == 0)
		/* USING INDEX NONE case */
		continue;
	      /* search USING INDEX list */
	      found = false;
	      for (j = 0; j < QO_UI_N (uip); j++)
		{
		  if (!intl_mbs_casecmp (consp->name, QO_UI_INDEX (uip, j)))
		    {
		      found = true;
		      break;
		    }
		}
	      if (QO_UI_FORCE (uip, 0) == -2)
		{
		  /* USING INDEX ALL EXCEPT case */
		  if (found)
		    /* this constraint(index) is specified in
		       USING INDEX ALL EXCEPT clause; do not use it */
		    continue;
		  j = -1;
		}
	      else
		{		/* QO_UI_FORCE(uip, j) could be either -1, 0, or 1 */
		  /* normal USING INDEX case */
		  if (!found)
		    /* this constraint(index) is not specified in
		       USING INDEX clause; do not use it */
		    continue;
		}
	    }

	  bitset_init (&index_segs, env);
	  bitset_init (&index_terms, env);
	  nseg_idx = 0;

	  /* count the number of columns on this constraint */
	  for (col_num = 0; consp->attributes[col_num]; col_num++)
	    ;
	  if (col_num <= NELEMENTS)
	    {
	      seg_idx = seg_idx_arr;
	    }
	  else
	    {
	      /* allocate seg_idx */
	      if ((seg_idx = (int *) malloc (sizeof (int) * col_num)) == NULL)
		{
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
	     which are found and applicable to this index(constraint) as
	     search key in the order of the index key attribute. For example,
	     if the index consists of attributes 'b' and 'a', and the given
	     segments of the node are 'a(1)', 'b(2)' and 'c(3)', then the
	     result of 'seg_idx[]' will be '{ 2, 1, -1 }'. The value -1 in
	     'seg_idx[] array means that no segment is specified. */
	  if (found == true)
	    {
	      /* if applicable index was found, add it to the node */

	      /* fill in QO_INDEX_ENTRY structure */
	      index_entryp = QO_INDEX_INDEX (indexp, indexp->n);
	      index_entryp->seg_idxs = (int *) NALLOCATE (env, int, nseg_idx);
	      index_entryp->seg_equal_terms =
		(BITSET *) NALLOCATE (env, BITSET, nseg_idx);
	      index_entryp->seg_other_terms =
		(BITSET *) NALLOCATE (env, BITSET, nseg_idx);
	      index_entryp->type = consp->type;
	      index_entryp->class_ = class_entryp;
	      index_entryp->btid = consp->index;
	      /* j == -1 iff no USING INDEX or USING INDEX ALL EXCEPT */
	      index_entryp->force = (j == -1) ? 0 : QO_UI_FORCE (uip, j);
	      index_entryp->name = consp->name;
	      index_entryp->col_num = col_num;
	      index_entryp->stats = NULL;
	      index_entryp->bt_stats_idx = -1;

	      index_entryp->nsegs = nseg_idx;
	      /* assign seg_idx[] and seg_terms[] */
	      for (j = 0; j < nseg_idx; j++)
		{
		  bitset_init (&(index_entryp->seg_equal_terms[j]), env);
		  bitset_init (&(index_entryp->seg_other_terms[j]), env);
		  if ((index_entryp->seg_idxs[j] = seg_idx[j]) != -1)
		    {
		      qo_find_index_seg_terms (env, seg_idx[j],
					       &(index_entryp->
						 seg_equal_terms[j]),
					       &(index_entryp->
						 seg_other_terms[j]));
		    }
		}		/* for (j = 0; j < nseg_idx; j++) */
	      qo_find_index_terms (env, &index_segs, &(index_entryp->terms));

	      (indexp->n)++;

	    }			/* if (found) */

	  bitset_delset (&(index_segs));
	  bitset_delset (&(index_terms));
	  if (seg_idx != seg_idx_arr)
	    free_and_init (seg_idx);

	}			/* for (consp = constraintp; consp; consp = consp->next) */

    }				/* for (i = 0; i < class_infop->n; i++) */
  /* class_infop->n >= 1 */


  /* find and mark indexes which are compatible across class heirarchy */

  n = 0;
  indexp = class_infop->info[n].index;

  /* allocate room for the compatible heirarchical indexex */
  /* We'll go ahead and allocate room for each index in the top level
     class. This is the worst case situation and it simplifies the code
     a bit. */
  /* Malloc a QO_INDEX struct with n entries. */
  node_indexp =
    QO_NODE_INDEXES (nodep) =
    (QO_NODE_INDEX *) malloc (SIZEOF_NODE_INDEX (indexp->n));

  QO_NI_N (node_indexp) = 0;

  /* if we don`t have any indexes to process, we're through
     if there is only one, then make sure that the head pointer points to it
     if there are more than one, we also need to construct a linked list
     of compatible indexes by recursively searching down the heirarchy */
  for (i = 0; i < indexp->n; i++)
    {
      index_entryp = QO_INDEX_INDEX (indexp, i);
      /* get compatible(equivalent) index of the next class
         'index_entryp->next' points to it */
      index_entryp->next =
	is_index_compatible (class_infop, n + 1, index_entryp);

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

}				/* qo_find_node_indexes() */

/*
 * qo_discover_indexes () -
 *   return: nothing
 *   env(in): The environment to be updated
 *
 * Note: Study each term to finish determination of whether it can use
 *	an index.  analyze_term() already determined whether each
 *	term qualifies structurally, and qo_get_class_info() has
 *	determined all of the indexes that are available, so all we
 *	have to do here is combine those two pieces of information.
 */
void
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
	     that apply to indexed segments */
	  qo_find_node_indexes (env, nodep);
	  /* collect statistic infomation on discovered indexes */
	  qo_get_index_info (env, nodep);

	}
      else
	{
	  /* If the 'info' of node is NULL, then this is probably a derived
	     table. Without the info, we don't have class informatino to
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
         assigned at 'analyze_term()' */
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

}				/* qo_discover_indexes() */

/*
 * qo_discover_partitions () -
 *   return:
 *   env(in):
 */
void
qo_discover_partitions (QO_ENV * env)
{
  /*
   * Partition the join graph into connected components.  Each
   * component will be "solved" by a separate planner, and the
   * subresults will be combined by cartesian product plans.
   *
   * This is, again, a standard union-find algorithm, this time using
   * locally allocated auxiliary data structures to hold the necessary
   * information.
   *
   * The buddy array actually encodes a forest which, when finished,
   * will have one tree for each graph partition.
   */
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

  buddy = NALLOCATE (env, int, (2 * N));
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
      while (buddy[hi] != -1)
	hi = buddy[hi];

      /*
       * Now buddy up all of the other nodes encompassed by this term.
       */
      while ((ti = bitset_next_member (&bi)) != -1)
	{
	  /*
	   * Run to the top of the tree in which node[ti] resides.
	   */
	  while (buddy[ti] != -1)
	    ti = buddy[ti];
	  /*
	   * Join the two trees together.
	   */
	  if (hi != ti)
	    buddy[hi] = ti;
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
	    r = buddy[r];
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
	      (&(QO_PARTITION_NODES (part)), &(QO_TERM_NODES (edge))))
	    {
	      bitset_add (&(QO_PARTITION_EDGES (part)), e);
	    }
	}
      /* alloc size check
       * 2: for signed max int. 2**30 is positive, 2**31 is negative
       * 2: for sizeof(QO_INFO *)
       */
      QO_ASSERT (env,
		 bitset_cardinality (&(QO_PARTITION_NODES (part))) <=
		 _WORDSIZE - 2 - 2);

      /* set the starting point the join_info vector that
       * correspond to each partition.
       */
      if (p > 0)
	{
	  QO_PARTITION_M_OFFSET (part) = M_offset;
	}
      join_info_size = QO_JOIN_INFO_SIZE (part);
      QO_ASSERT (env,
		 INT_MAX - M_offset * sizeof (QO_INFO *) >=
		 join_info_size * sizeof (QO_INFO *));
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

  DEALLOCATE (env, buddy);
}

/*
 * qo_assign_eq_classes () -
 *   return:
 *   env(in):
 */
void
qo_assign_eq_classes (QO_ENV * env)
{
  int i;
  QO_EQCLASS **eq_map;
  BITSET segs;

  bitset_init (&segs, env);
  eq_map = NALLOCATE (env, QO_EQCLASS *, env->nsegs);
  for (i = 0; i < env->nsegs; ++i)
    eq_map[i] = NULL;

  /*
   * Find out all of the segments that are used in predicate terms.
   * These are the only segments that we are really interested in
   * grouping into equivalence classes.  All others are just
   * "tag-along" segments that correspond to attributes that are part
   * of the final result but aren't really "necessary" in determining
   * it.
   *
   * If the term doesn't have a "nominal seg" associated with it, then
   * it's not a term that has generated an equivalence class, and we
   * don't want to add any segments that it contains.
   */
  for (i = 0; i < env->nedges; i++)
    {
      QO_TERM *term;

      term = QO_ENV_TERM (env, i);
      if (QO_TERM_NOMINAL_SEG (term))
	bitset_union (&segs, &(QO_TERM_SEGS (term)));
    }

  /*
   * Now examine each segment and see if it should be assigned to an
   * equivalence class.
   */
  for (i = 0; i < env->nsegs; ++i)
    {
      if (!BITSET_MEMBER (segs, i))
	continue;

      if (eq_map[i] == NULL)
	{
	  QO_SEGMENT *root, *seg;
	  seg = QO_ENV_SEG (env, i);

	  /*
	   * Find the root of the tree in which this segment resides.
	   */
	  for (root = seg; QO_SEG_EQ_ROOT (root);
	       root = QO_SEG_EQ_ROOT (root))
	    ;
	  /*
	   * Assign a new EqClass to that root if one hasn't already
	   * been assigned.
	   */
	  if (eq_map[QO_SEG_IDX (root)] == NULL)
	    qo_eqclass_add ((eq_map[QO_SEG_IDX (root)] =
			     qo_eqclass_new (env)), root);
	  /*
	   * Now add the original segment to the same equivalence
	   * class.
	   */
	  if (root != seg)
	    qo_eqclass_add (eq_map[QO_SEG_IDX (root)], seg);
	  eq_map[i] = eq_map[QO_SEG_IDX (root)];
	}
    }

  bitset_delset (&segs);
  DEALLOCATE (env, eq_map);

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
	  /*
	   * This term must have been some sort of complex merge
	   * candidate, such as "a+b = c+d", so assign it an eqclass of
	   * its own.  This will need to change if we ever succeed in
	   * separating the notion of "attribute equivalence class"
	   * from "sort order".
	   *
	   * This eq_class will be a little funny, in that it won't
	   * have any segments associated with it.  That's ok, all we
	   * really need is something with a unique identity here.
	   * eqclass_to_orderby (in qo_xasl.c) is the only routine that
	   * should need to know anything about this, and it will deal
	   * with it appropriately.
	   *
	   * Keep a backpointer to the term so that it's easier for
	   * eqclass_to_orderby to do its job.
	   */
	  QO_TERM_EQCLASS (term) = qo_eqclass_new (env);
	  QO_EQCLASS_TERM (QO_TERM_EQCLASS (term)) = term;
	}
      else
	{
	  QO_TERM_EQCLASS (term) = NULL;
	}
    }
}

/*
 * qo_env_dump () -
 *   return:
 *   env(in):
 *   f(in):
 */
void
qo_env_dump (QO_ENV * env, FILE * f)
{
  if (f == NULL)
    f = stdout;

  if (env->nsegs)
    {
      int i;

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
      int i;
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
      int i;
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
      int i;
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
      int i;
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
      int i;
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
      int i;
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
void
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
    qo_free_node_index_info (QO_NODE_ENV (node), QO_NODE_INDEXES (node));
  DEALLOCATE (env, QO_NODE_USING_INDEX (node));
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
    QO_NODE_SELECTIVITY (node) = sel_limit;
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
    fprintf (f, "%s", QO_NODE_NAME (node));
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
      n = QO_NODE_INFO (node)->n;
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
    fprintf (f, "%s", (name ? name : "(unknown)"));
  else
    fprintf (f, "as %s", (name ? name : "(unknown)"));

  if (entity->info.spec.range_var->alias_print)
    {
      fprintf (f, "(%s)", entity->info.spec.range_var->alias_print);
    }

  fprintf (f, "(%lu/%lu)", QO_NODE_NCARD (node), QO_NODE_TCARD (node));
  if (!bitset_is_empty (&(QO_NODE_SARGS (node))))
    {
      fputs (" (sargs ", f);
      bitset_print (&(QO_NODE_SARGS (node)),
		    (int (*)(void *, char *, int)) fprintf, f);
      fputs (")", f);
    }
}

/*
 * qo_seg_clear () -
 *   return:
 *   env(in):
 *   idx(in):
 */
void
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
    qo_free_attr_info (QO_SEG_ENV (seg), QO_SEG_INFO (seg));
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
size_t
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
      return sizeof (long);
    }

  size = tp_domain_disk_size (domain);
  switch (domain->type->id)
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
  return MAX ((int) sizeof (long), size);
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
void
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
  QO_TERM_EQCLASS (term) = NULL;
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
 * qo_equivalence () -
 *   return:
 *   sega(in):
 *   segb(in):
 */
void
qo_equivalence (QO_SEGMENT * sega, QO_SEGMENT * segb)
{
  /*
   * This is part of a basic union-find algorithm as discussed in, for
   * example, Sedgewick's "Algorithms".  During the discovery phase,
   * equivalence classes are represented by trees of Segment instances,
   * with links (the eq_root fields) going toward the roots of the
   * trees.  When we discover a pair of segments that are to belong to
   * the same equivalence class, we merge the two trees in which they
   * reside.  Merging is accomplished by chasing links until we arrive at
   * the roots of the two subtrees (identified as such because their
   * eq_root links are NULL).  If the roots are the same, the
   * columns are already in the same equivalence class, and no further
   * action is necessary.  If the roots differ, we merge the trees by
   * assigning by linking one of the trees to the other through its
   * eq_root link.
   *
   * After all columns have been examined, we make a second pass
   * (assign_eq_classes()) and set aside actual EqClass instances
   * for each of the distinct equivalence class trees that has been
   * built.
   */

  while (QO_SEG_EQ_ROOT (sega))
    sega = QO_SEG_EQ_ROOT (sega);
  while (QO_SEG_EQ_ROOT (segb))
    segb = QO_SEG_EQ_ROOT (segb);

  if (sega != segb)
    QO_SEG_EQ_ROOT (sega) = segb;
}

/*
 * qo_eqclass_wrt () -
 *   return:
 *   eqclass(in):
 *   nodeset(in):
 */
QO_SEGMENT *
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
    fputs ("UNORDERED", f);
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
    qo_seg_fprint (qo_eqclass_wrt (eqclass, nodeset), f);
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
	    fprintf (f, " () -> ");
	  else
	    fprintf (f, " %s -> ", QO_SEG_NAME (QO_TERM_SEG (term)));
	  qo_node_fprint (QO_TERM_TAIL (term), f);
	  break;

	case QO_TC_DEP_LINK:
	  fprintf (f, "table(");
	  bitset_print (&(QO_NODE_DEP_SET (QO_TERM_TAIL (term))),
			(int (*)(void *, char *, int)) fprintf, f);
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
  QO_TERMCLASS tc;

  switch (tc = QO_TERM_CLASS (term))
    {
    case QO_TC_PATH:
      qo_node_fprint (QO_TERM_HEAD (term), f);
      if (!QO_TERM_SEG (term) || !QO_SEG_NAME (QO_TERM_SEG (term)))
	fprintf (f, " () -> ");
      else
	fprintf (f, " %s -> ", QO_SEG_NAME (QO_TERM_SEG (term)));
      qo_node_fprint (QO_TERM_TAIL (term), f);
      break;

    case QO_TC_DEP_LINK:
      fprintf (f, "table(");
      bitset_print (&(QO_NODE_DEP_SET (QO_TERM_TAIL (term))),
		    (int (*)(void *, char *, int)) fprintf, f);
      fprintf (f, ") -> ");
      qo_node_fprint (QO_TERM_TAIL (term), f);
      break;

    case QO_TC_DEP_JOIN:
      qo_node_fprint (QO_TERM_HEAD (term), f);
      fprintf (f, " <dj> ");
      qo_node_fprint (QO_TERM_TAIL (term), f);
      break;

    case QO_TC_DUMMY_JOIN:
      if (QO_TERM_PT_EXPR (term))
	{			/* may be transitive dummy join term */
	  fprintf (f, "%s",
		   parser_print_tree (QO_ENV_PARSER (QO_TERM_ENV (term)),
				      QO_TERM_PT_EXPR (term)));
	}
      else
	{
	  qo_node_fprint (QO_TERM_HEAD (term), f);
	  fprintf (f, ", ");
	  qo_node_fprint (QO_TERM_TAIL (term), f);
	}
      break;

    default:
      {
	PARSER_CONTEXT *parser = QO_ENV_PARSER (QO_TERM_ENV (term));
	PT_PRINT_VALUE_FUNC saved_func = parser->print_db_value;
	/* in order to print auto parameterized values */
	parser->print_db_value = pt_print_node_value;
	fprintf (f, "%s", parser_print_tree (parser, QO_TERM_PT_EXPR (term)));
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
	PT_NODE *conj;

	conj = QO_TERM_PT_EXPR (term);
	if (conj &&
	    conj->node_type == PT_VALUE && conj->info.value.location == 0)
	  {
	    /* is an always-false WHERE condition */
	    fprintf (f, " (always-false term)");
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
    fputs (" (mergeable)", f);

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

    case JOIN_OUTER:
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
	fputs (separator, f);
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
  bitset_print (&(QO_PARTITION_NODES (part)),
		(int (*)(void *, char *, int)) fprintf, f);
  fputs (") (edges ", f);
  bitset_print (&(QO_PARTITION_EDGES (part)),
		(int (*)(void *, char *, int)) fprintf, f);
  fputs (") (dependencies ", f);
  bitset_print (&(QO_PARTITION_DEPENDENCIES (part)),
		(int (*)(void *, char *, int)) fprintf, f);
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
  set_stats (f);
}

/*
 * qo_seg_nodes () - Return a bitset of node ids produced from the heads
 *		     of all of the segments in segset
 *   return:
 *   env(in): The environment in which these segment and node ids make sense
 *   segset(in): A bitset of segment ids
 *   result(out): A bitset of node ids (OUTPUT PARAMETER)
 */
void
qo_seg_nodes (QO_ENV * env, BITSET * segset, BITSET * result)
{
  BITSET_ITERATOR si;
  int i;

  BITSET_CLEAR (*result);
  for (i = bitset_iterate (segset, &si); i != -1;
       i = bitset_next_member (&si))
    bitset_add (result, QO_NODE_IDX (QO_SEG_HEAD (QO_ENV_SEG (env, i))));

}
