/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * query_graph.h - Query graph
 * TODO: rename this file to query_graph.h
 * TODO: include query_graph_1.h and remove it
 */

#ifndef _QUERY_GRAPH_H_
#define _QUERY_GRAPH_H_

#ident "$Id$"

#include "work_space.h"
#include "statistics.h"

#include "qo.h"
#include "bitset.h"

typedef struct qo_class_info_entry QO_CLASS_INFO_ENTRY;

struct qo_class_info_entry
{
  /*
   * The name of the class.
   */
  const char *name;

  /*
   * The actual oid of the class.
   */
  OID oid;

  /*
   * The mop pointer to the memory-resident descriptor for the class
   * object.  It is important that we keep this alive while we still
   * need statistics; it our guarantee that the garbage collector won't
   * try to remove the class object from memory and thereby render our
   * pointer to the statistics structure useless.
   */
  MOP mop;

  /*
   * The class which contains a statistics structure. This structure will be
   * automatically reclaimed, so there is no need to explicitly free it
   * when the QO_CLASS_INFO structure is freed.
   *
   * UNLESS we allocate it ourselves in qo_estimate_statistics(); use
   * the self_allocated flag to remember this tidbit.
   */
  SM_CLASS *smclass;

  /* The statistics which is allocated when it can not get from server */
  CLASS_STATS *stats;
  int self_allocated;

  /* is this a normal-resident sqlx class ? */
  int normal_class;

  QO_INDEX *index;
};


struct qo_class_info
{
  int n;
  QO_CLASS_INFO_ENTRY info[1];

};


struct qo_attr_info
{
  /* cumulative stats for all attributes under this umbrella */
  QO_ATTR_CUM_STATS cum_stats;
};

struct qo_index_entry
{
  /* Indexes which belong to the same class are grouped as arrays
     in the QO_INDEX structure.  This is useful when discovering
     indexes.  When using indexes, is is more convenient to group
     compatible indexes accross the class heirarchy.  The next pointer
     will pointer to the next compatible index in the class heirarchy.

     Another way of thinking about this is that indexes are grouped
     horizontally using arrays in QO_INDEX and grouped vertically
     using pointers.

     The class pointer, points back to the class which this index
     is a member of. */

  /* next compatible index under the class hierarchy */
  struct qo_index_entry *next;

  /* type of index; either SM_CONSTRAINT_INDEX or SM_CONSTRAINT_UNIQUE */
  SM_CONSTRAINT_TYPE type;
  /* information of the class to which the index belongs */
  QO_CLASS_INFO_ENTRY *class_;
  /* B-tree ID of the index */
  BTID btid;
  int force;
  /* index name */
  const char *name;
  /* number of columns of the index */
  int col_num;
  /* statistics of the index; statistcs of the first indexed attribute */
  ATTR_STATS *stats;
  /* B-tree statistics of the index; ATTR_STATS.BTREE_STATS[idx] */
  int bt_stats_idx;


  /* 'seg_idxs[]' is an array of segment idx's while 'nsegs' contains
     the number of valid entries in the array.  We use 'seg_idxs[]'
     because BITSETs can only represent membership but cannot represent
     ordering.
     For multi-column indexes, both membership and ordering are
     important, and so we need this array.  We'll go ahead and
     the BITSET arround because it's a very convinient form for
     some operations.

     'seg_terms[]' is an array of term sets.  Each element of the array
     is a set of SARG terms associated with a particular segment.  The
     order of the sets will follow the 'seg_idxs[]' array.
     (i.e. seg_terms[0] will be the set of terms associated with
     with segment seg_idxs[0]) */

  /* number of entries of seg_idxs[] and seg_terms[] array */
  int nsegs;
  /* array of segment idx's constrained by the index */
  int *seg_idxs;
  /* array of equal operator term sets */
  BITSET *seg_equal_terms;
  /* array of non-equal operator term sets */
  BITSET *seg_other_terms;
  /* terms constrained by the index */
  BITSET terms;
};				/* struct qo_index_entry */

#define QO_ENTRY_MULTI_COL(entry)       ((entry)->col_num > 1 ? true : false)

struct qo_index
{
  /*
   * Array of index structures.  Each structure represents an possibly
   * useful index which is a member of this class.  If a compatible index
   * exists at each level of the class heirarchy, then they are
   * considered useful.
   *
   * 'n' is the valid number of entries (from 0 to n-1), 'max' is the
   * extent of the allocated entries.  The distinction is necessary
   * because we allocate a fixed number (in qo_get_index()) before we
   * really know how many we need, and we need to know to clean up that
   * many in qo_free_index().
   */
  int n;
  int max;
  struct qo_index_entry index[1];
};

#define QO_INDEX_INDEX(ind, n)		(&(ind)->index[(n)])

/*
 * Get current class statistics.
 */ 
#define QO_GET_CLASS_STATS(entryp) \
       ((entryp)->self_allocated ? (entryp)->stats : (entryp)->smclass->stats)

extern QO_CLASS_INFO *qo_get_class_info (QO_ENV * env, QO_NODE * node);
extern void qo_free_class_info (QO_ENV * env, QO_CLASS_INFO *);
extern QO_ATTR_INFO *qo_get_attr_info (QO_ENV * env, QO_SEGMENT * seg);
extern void qo_free_attr_info (QO_ENV * env, QO_ATTR_INFO * info);
extern void qo_free_node_index_info (QO_ENV * env,
				     QO_NODE_INDEX * node_indexp);
extern QO_INDEX *qo_alloc_index (QO_ENV * env, int);
extern void qo_free_index (QO_ENV * env, QO_INDEX *);
extern void qo_get_index_info (QO_ENV * env, QO_NODE * node);

#endif /* _QUERY_GRAPH_H_ */
