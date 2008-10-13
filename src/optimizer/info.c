/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * info.c - Miscellaneous interface functions that I wish existed
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>

#include "optimizer.h"
#include "query_graph_2.h"
#include "query_graph_1.h"

#include "parser.h"
#include "schema_manager_3.h"
#include "locator_cl.h"
#include "object_domain.h"
#include "memory_manager_2.h"

/*
 * Figure out how many bytes a QO_INDEX struct with n entries requires.
 */
#define SIZEOF_INDEX(n) \
    (sizeof(QO_INDEX) + (((n)-1)* sizeof(QO_INDEX_ENTRY)))

/*
 * Malloc a QO_INDEX struct with n entries.
 */
#define ALLOC_INDEX(env, n) \
    (QO_INDEX *)malloc(SIZEOF_INDEX(n))

/*
 * Figure out how many bytes a QO_CLASS_INFO struct with n entries requires.
 */
#define SIZEOF_CLASS_INFO(n) \
    (sizeof(QO_CLASS_INFO) + (((n)-1) * sizeof(QO_CLASS_INFO_ENTRY)))

/*
 * Malloc a QO_CLASS_INFO struct with n entries.
 */
#define ALLOC_CLASS_INFO(env, n) \
    (QO_CLASS_INFO *)malloc(SIZEOF_CLASS_INFO(n))

/*
 * Malloc a QO_ATTR_INFO struct with n entries.
 */
#define ALLOC_ATTR_INFO(env) \
    (QO_ATTR_INFO *)malloc(sizeof(QO_ATTR_INFO))

/*
 * Figure out how many bytes a pkeys[] struct with n entries requires.
 */
#define SIZEOF_ATTR_CUM_STATS_PKEYS(n) \
    ((n) * sizeof(int))

/*
 * Malloc a pkeys[] struct with n entries.
 */
#define ALLOC_ATTR_CUM_STATS_PKEYS(env, n) \
    (int *)malloc(SIZEOF_ATTR_CUM_STATS_PKEYS(n))

#define NOMINAL_HEAP_SIZE(class)	200	/* pages */
#define NOMINAL_OBJECT_SIZE(class)	 64	/* bytes */

static int count_classes (PT_NODE * p);
static QO_CLASS_INFO_ENTRY *grok_classes (QO_ENV * env, PT_NODE * dom_set,
					  QO_CLASS_INFO_ENTRY * info);
static int qo_data_compare (DB_DATA * data1, DB_DATA * data2, DB_TYPE type);
static void qo_estimate_statistics (MOP class_mop, CLASS_STATS *);

/*
 * qo_alloc_index () - Allocate a QO_INDEX structure with room for <n>
 *		       QO_INDEX_ENTRY elements.  The fields are initialized
 *   return: QO_CLASS_INFO *
 *   env(in): The current optimizer environment
 *   n(in): The node whose class info we want
 */
QO_INDEX *
qo_alloc_index (QO_ENV * env, int n)
{
  int i;
  QO_INDEX *indexp;
  QO_INDEX_ENTRY *entryp;

  indexp = ALLOC_INDEX (env, n);
  indexp->n = 0;
  indexp->max = n;

  for (i = 0; i < n; i++)
    {
      entryp = QO_INDEX_INDEX (indexp, i);

      entryp->next = NULL;
      entryp->type = SM_CONSTRAINT_INDEX;
      entryp->class_ = NULL;
      BTID_SET_NULL (&(entryp->btid));
      entryp->name = NULL;
      entryp->col_num = 0;
      entryp->stats = NULL;
      entryp->bt_stats_idx = -1;
      entryp->nsegs = 0;
      entryp->seg_idxs = NULL;
      entryp->seg_equal_terms = NULL;
      entryp->seg_other_terms = NULL;
      bitset_init (&(entryp->terms), env);
    }

  return indexp;
}				/* qo_alloc_index() */

/*
 * qo_free_index () - Free the QO_INDEX structure and all elements contained
 *		      within it
 *   return: nothing
 *   env(in): The current optimizer environment
 *   indexp(in): A pointer to a previously-allocated index vector
 */
void
qo_free_index (QO_ENV * env, QO_INDEX * indexp)
{
  int i, j;
  QO_INDEX_ENTRY *entryp;

  if (!indexp)
    return;

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
	  DEALLOCATE (env, entryp->seg_equal_terms);
	  DEALLOCATE (env, entryp->seg_other_terms);
	  DEALLOCATE (env, entryp->seg_idxs);
	}
    }

  DEALLOCATE (env, indexp);
}				/* qo_free_index() */

/*
 * qo_get_class_info () -
 *   return: QO_CLASS_INFO *
 *   env(in): The current optimizer environment
 *   node(in): The node whose class info we want
 */
QO_CLASS_INFO *
qo_get_class_info (QO_ENV * env, QO_NODE * node)
{
  PT_NODE *dom_set;
  int n;
  QO_CLASS_INFO *info;
  QO_CLASS_INFO_ENTRY *end;
  int i;

  dom_set = QO_NODE_ENTITY_SPEC (node)->info.spec.flat_entity_list;
  n = count_classes (dom_set);
  info = ALLOC_CLASS_INFO (env, n);

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

}				/* qo_get_class_info */

/*
 * qo_free_class_info () - Free the vector and all interally-allocated
 *			   structures
 *   return: nothing
 *   env(in): The current optimizer environment
 *   info(in): A pointer to a previously-allocated info vector
 */
void
qo_free_class_info (QO_ENV * env, QO_CLASS_INFO * info)
{
  int i;

  if (info == NULL)
    return;

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
	DEALLOCATE (env, info->info[i].stats);
      info->info[i].smclass = NULL;
    }
  DEALLOCATE (env, info);

}				/* qo_free_class_info */

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
      n += 1;
    }

  return n;

}				/* count_classes */

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

      /*
       * If there are no statistics for some reason, OR if the class
       * info says the heap_size is 0 but the class has a heap
       * associated with it, then manufacture some bogus (but large)
       * statistics.  This will keep us from doing stupid things (e.g.,
       * nested loops) with temporary classes of the sort we find in
       * SQL/M (or other classes that have had instances created but
       * for which the statistics haven't been updated).
       *
       * This will get confused if someone has a class that has had all
       * of its instances deleted, because the class will still have
       * heap file pages associated with it.  This ought to be a pretty
       * uncommon occurrence, though, and we can't do any better until
       * the statistics manager grows a brain.
       */
      smclass = info->smclass;
      if (smclass->stats == NULL)
	{
	  info->stats = ALLOCATE (env, CLASS_STATS);
	  info->self_allocated = 1;
	  info->stats->n_attrs = 0;
	  info->stats->attr_stats = NULL;
	  qo_estimate_statistics (info->mop, info->stats);
	}
      else if (smclass->stats->heap_size == 0)
	{
	  /*
	   * Be careful here: if this is a proxy for some ldb, it will
	   * look like an empty class, when in reality it may be
	   * something huge.  Make sure that we use the big estimate in
	   * that case.
	   *
	   * If it's not a proxy and it seems to have a heap associated
	   * with it, assume that the statistics manager is confused.
	   */
	  if (!info->normal_class
	      || (((hfid = sm_get_heap (info->mop)) && !HFID_IS_NULL (hfid))))
	    {
	      qo_estimate_statistics (info->mop, smclass->stats);
	    }
	}

      info++;
    }

  return info;

}				/* grok_classes */

/*
 * qo_get_attr_info () - Find the ATTR_STATS information about each actual
 *			 attribute that underlies this segment
 *   return: QO_ATTR_INFO *
 *   env(in): The current optimizer environment
 *   seg(in): A (pointer to) a join graph segment
 */
QO_ATTR_INFO *
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
  bool is_unique_index;
  SM_CLASS_CONSTRAINT *constraints, *consp;

  /* actual attribute name of the given segment */
  name = QO_SEG_NAME (seg);
  /* QO_NODE of the given segment */
  nodep = QO_SEG_HEAD (seg);

  if (QO_NODE_INFO (nodep) == NULL ||
      !(QO_NODE_INFO (nodep)->info[0].normal_class))
    /* if there's no class information or the class is not normal class */
    return NULL;

  /* number of class information entries */
  n = QO_NODE_INFO (nodep)->n;
  QO_ASSERT (env, n > 0);

  /* pointer to QO_CLASS_INFO_ENTRY[] array of the node */
  class_info_entryp = &QO_NODE_INFO (nodep)->info[0];

  /* allocate QO_ATTR_INFO within the current optimizer environment */
  attr_infop = ALLOC_ATTR_INFO (env);
  if (!attr_infop)
    {
      /* already, error has been set */
      return NULL;
    }

  /* initialize QO_ATTR_CUM_STATS structure of QO_ATTR_INFO */
  cum_statsp = &attr_infop->cum_stats;
  cum_statsp->type = sm_att_type_id (class_info_entryp->mop, name);
  cum_statsp->valid_limits = false;
  OR_PUT_INT (&cum_statsp->min_value, 0);
  OR_PUT_INT (&cum_statsp->max_value, 0);
  cum_statsp->is_indexed = true;
  cum_statsp->leafs = cum_statsp->pages = cum_statsp->height =
    cum_statsp->keys = cum_statsp->oids = cum_statsp->nulls =
    cum_statsp->ukeys = 0;
  cum_statsp->key_type = NULL;
  cum_statsp->key_size = 0;
  cum_statsp->pkeys = NULL;

  /* set the statistics from the class information(QO_CLASS_INFO_ENTRY) */
  for (i = 0; i < n; class_info_entryp++, i++)
    {

      attr_id = sm_att_id (class_info_entryp->mop, name);

      /* pointer to ATTR_STATS of CLASS_STATS of QO_CLASS_INFO_ENTRY */
      attr_statsp = QO_GET_CLASS_STATS (class_info_entryp)->attr_stats;
      if (!attr_statsp)
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
      n_attrs = QO_GET_CLASS_STATS (class_info_entryp)->n_attrs;
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

      /* if the atrribute is numeric type so its min/max values are
         meaningful, keep the min/max existing values */
      if (DB_NUMERIC_TYPE (attr_statsp->type))
	{

	  if (!cum_statsp->valid_limits)
	    {

	      /* first time */
	      cum_statsp->type = attr_statsp->type;
	      cum_statsp->valid_limits = true;
	      /* assign values, bitwise-copy of DB_DATA structure */
	      cum_statsp->min_value = attr_statsp->min_value;
	      cum_statsp->max_value = attr_statsp->max_value;

	    }
	  else
	    {			/* if (!cum_statsp->valid_limits) */

	      /* compare with previsous values */
	      if (qo_data_compare (&attr_statsp->min_value,
				   &cum_statsp->min_value,
				   cum_statsp->type) < 0)
		cum_statsp->min_value = attr_statsp->min_value;
	      if (qo_data_compare (&attr_statsp->max_value,
				   &cum_statsp->max_value,
				   cum_statsp->type) > 0)
		cum_statsp->max_value = attr_statsp->max_value;
	      /* 'qo_data_compare()' is a simplized function that works
	         with DB_DATA instead of DB_VALUE. However, this way
	         would be enough to get minimum/maximum existing value,
	         because the values are meaningful only when their types
	         are numeric and we are considering compatible indexes
	         under class hierarchy. */

	    }			/* if (!cum_statsp->valid_limits) */

	}			/* if (DB_NUMERIC_TYPE(attr_statsp->type)) */


      if (attr_statsp->n_btstats <= 0 || !attr_statsp->bt_stats)
	{
	  /* the attribute dose not have any index */
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
	  is_unique_index = false;	/* init */

	  constraints = sm_class_constraints (class_info_entryp->mop);
	  for (consp = constraints; consp; consp = consp->next)
	    {
	      /* found index */
	      if (SM_IS_CONSTRAINT_INDEX_FAMILY (consp->type) &&
		  BTID_IS_EQUAL (&bt_statsp->btid, &consp->index))
		{
		  if (SM_IS_CONSTRAINT_UNIQUE_FAMILY (consp->type))
		    {
		      is_unique_index = true;
		    }
		  break;
		}
	    }			/* for ( ... ) */

	  if (is_unique_index)
	    {
	      /* is class hierarchy index: set unique index statistics */
	      cum_statsp->leafs = bt_statsp->leafs;
	      cum_statsp->pages = bt_statsp->pages;
	      cum_statsp->oids = bt_statsp->oids;
	      cum_statsp->nulls = bt_statsp->nulls;
	      cum_statsp->height = bt_statsp->height;
	      cum_statsp->keys = bt_statsp->keys;
	      cum_statsp->ukeys = bt_statsp->ukeys;
	      cum_statsp->key_type = bt_statsp->key_type;
	      cum_statsp->key_size = bt_statsp->key_size;
	      /* alloc pkeys[] within the current optimizer environment */
	      if (cum_statsp->pkeys)
		DEALLOCATE (env, cum_statsp->pkeys);	/* free alloced */
	      cum_statsp->pkeys =
		ALLOC_ATTR_CUM_STATS_PKEYS (env, cum_statsp->key_size);
	      if (!cum_statsp->pkeys)
		{
		  /* already, error has been set */
		  qo_free_attr_info (env, attr_infop);
		  return NULL;
		}
	      for (j = 0; j < cum_statsp->key_size; j++)
		cum_statsp->pkeys[j] = bt_statsp->pkeys[j];

	      /* immediately return the allocated QO_ATTR_INFO */
	      return attr_infop;
	    }
	  else
	    {
	      /* keep cumulative totals of index statistics */
	      cum_statsp->leafs += bt_statsp->leafs;
	      cum_statsp->pages += bt_statsp->pages;
	      cum_statsp->oids += bt_statsp->oids;
	      cum_statsp->nulls += bt_statsp->nulls;
	      /* Assume that the key distributions overlap here, so that the
	         number of distinct keys in all of the attributes equal to the
	         maximum number of distinct keys in any one of the attributes.
	         This is probably not far from the truth; it is almost
	         certainly a better guess than assuming that all key ranges
	         are distinct. */
	      cum_statsp->height =
		MAX (cum_statsp->height, bt_statsp->height);
	      if (cum_statsp->key_size == 0 ||	/* the first found */
		  cum_statsp->keys < bt_statsp->keys)
		{
		  cum_statsp->keys = bt_statsp->keys;
		  cum_statsp->ukeys = bt_statsp->ukeys;
		  cum_statsp->key_type = bt_statsp->key_type;
		  cum_statsp->key_size = bt_statsp->key_size;
		  /* alloc pkeys[] within the current optimizer environment */
		  if (cum_statsp->pkeys)
		    DEALLOCATE (env, cum_statsp->pkeys);	/* free alloced */
		  cum_statsp->pkeys =
		    ALLOC_ATTR_CUM_STATS_PKEYS (env, cum_statsp->key_size);
		  if (!cum_statsp->pkeys)
		    {
		      /* already, error has been set */
		      qo_free_attr_info (env, attr_infop);
		      return NULL;
		    }
		  for (j = 0; j < cum_statsp->key_size; j++)
		    cum_statsp->pkeys[j] = bt_statsp->pkeys[j];
		}
	    }
	}
      else
	{
	  /* dynamic classes spec, etc
	     for example: select ... from (x, y, z) p
	     select ... from x p
	   */

	  /* keep cumulative totals of index statistics */
	  cum_statsp->leafs += bt_statsp->leafs;
	  cum_statsp->pages += bt_statsp->pages;
	  cum_statsp->oids += bt_statsp->oids;
	  cum_statsp->nulls += bt_statsp->nulls;
	  /* Assume that the key distributions overlap here, so that the
	     number of distinct keys in all of the attributes equal to the
	     maximum number of distinct keys in any one of the attributes.
	     This is probably not far from the truth; it is almost certainly
	     a better guess than assuming that all key ranges are distinct. */
	  cum_statsp->height = MAX (cum_statsp->height, bt_statsp->height);
	  if (cum_statsp->key_size == 0 ||	/* the first found */
	      cum_statsp->keys < bt_statsp->keys)
	    {
	      cum_statsp->keys = bt_statsp->keys;
	      cum_statsp->ukeys = bt_statsp->ukeys;
	      cum_statsp->key_type = bt_statsp->key_type;
	      cum_statsp->key_size = bt_statsp->key_size;
	      /* alloc pkeys[] within the current optimizer environment */
	      if (cum_statsp->pkeys)
		DEALLOCATE (env, cum_statsp->pkeys);	/* free alloced */
	      cum_statsp->pkeys =
		ALLOC_ATTR_CUM_STATS_PKEYS (env, cum_statsp->key_size);
	      if (!cum_statsp->pkeys)
		{
		  /* already, error has been set */
		  qo_free_attr_info (env, attr_infop);
		  return NULL;
		}
	      for (j = 0; j < cum_statsp->key_size; j++)
		cum_statsp->pkeys[j] = bt_statsp->pkeys[j];
	    }
	}

    }				/* for (i = 0; i < n; ...) */

  /* return the allocated QO_ATTR_INFO */
  return attr_infop;
}				/* qo_get_attr_info() */

/*
 * qo_free_attr_info () - Free the vector and any internally allocated
 *			  structures
 *   return: nothing
 *   env(in): The current optimizer environment
 *   info(in): A pointer to a previously allocated info vector
 */
void
qo_free_attr_info (QO_ENV * env, QO_ATTR_INFO * info)
{
  QO_ATTR_CUM_STATS *cum_statsp;

  if (info)
    {
      cum_statsp = &info->cum_stats;
      if (cum_statsp->pkeys)
	{
	  DEALLOCATE (env, cum_statsp->pkeys);
	}
      DEALLOCATE (env, info);
    }

}				/* qo_free_attr_info */

/*
 * qo_get_index_info () - Get index statistical information
 *   return:
 *   env(in): The current optimizer environment
 *   node(in): A join graph node
 */
void
qo_get_index_info (QO_ENV * env, QO_NODE * node)
{
  QO_NODE_INDEX *node_indexp;
  QO_NODE_INDEX_ENTRY *ni_entryp;
  QO_INDEX_ENTRY *index_entryp;
  QO_ATTR_CUM_STATS *cum_statsp;
  QO_SEGMENT *segp;
  QO_NODE *seg_node;
  QO_CLASS_INFO_ENTRY *class_info_entryp;
  const char *name;
  int attr_id, n_attrs;
  ATTR_STATS *attr_statsp;
  BTREE_STATS *bt_statsp;
  int i, j, k;
  bool is_unique_index;

  /* pointer to QO_NODE_INDEX structure of QO_NODE */
  node_indexp = QO_NODE_INDEXES (node);

  /* for each index list(linked list of QO_INDEX_ENTRY) rooted at the node
     (all elements of QO_NODE_INDEX_ENTRY[] array) */
  for (i = 0, ni_entryp = QO_NI_ENTRY (node_indexp, 0);
       i < QO_NI_N (node_indexp); i++, ni_entryp++)
    {

      /* initialize QO_ATTR_CUM_STATS structure of QO_NODE_INDEX_ENTRY */
      cum_statsp = &(ni_entryp)->cum_stats;
      /* cum_statsp->type = (index_entryp->stats)->type; */
      cum_statsp->valid_limits = false;
      OR_PUT_INT (&cum_statsp->min_value, 0);
      OR_PUT_INT (&cum_statsp->max_value, 0);
      cum_statsp->is_indexed = true;
      cum_statsp->leafs = cum_statsp->pages = cum_statsp->height =
	cum_statsp->keys = cum_statsp->oids = cum_statsp->nulls =
	cum_statsp->ukeys = 0;
      cum_statsp->key_type = NULL;
      cum_statsp->key_size = 0;
      cum_statsp->pkeys = NULL;

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
	     denoting the attribute that contains the index statisitcs that
	     we want to get. */
	  segp = QO_ENV_SEG (env, (index_entryp->seg_idxs[0]));

	  /* QO_NODE of the given segment */
	  seg_node = QO_SEG_HEAD (segp);

	  /* actual attribute name of the given segment */
	  name = QO_SEG_NAME (segp);

	  /* pointer to QO_CLASS_INFO_ENTRY[] array of the node */
	  class_info_entryp = &QO_NODE_INFO (seg_node)->info[j];

	  attr_id = sm_att_id (class_info_entryp->mop, name);

	  /* pointer to ATTR_STATS of CLASS_STATS of QO_CLASS_INFO_ENTRY */
	  attr_statsp = QO_GET_CLASS_STATS (class_info_entryp)->attr_stats;

	  /* search the attribute from the class information */
	  n_attrs = QO_GET_CLASS_STATS (class_info_entryp)->n_attrs;
	  for (k = 0; k < n_attrs; k++, attr_statsp++)
	    {
	      if (attr_statsp->id == attr_id)
		{
		  break;
		}
	    }
	  if (k >= n_attrs)	/* not found */
	    {
	      attr_statsp = NULL;
	    }
	  index_entryp->stats = attr_statsp;
	  index_entryp->bt_stats_idx = -1;

	  if (!attr_statsp)
	    {
	      /* absence of the attribute statistics? */
	      continue;
	    }

	  /* if the attribute is numeric type so its min/max values are
	     meaningful, keep the min/max existing values */
	  if (DB_NUMERIC_TYPE (attr_statsp->type))
	    {

	      if (!cum_statsp->valid_limits)
		{

		  /* first time */
		  cum_statsp->type = attr_statsp->type;
		  cum_statsp->valid_limits = true;
		  /* assign values, bitwise-copy of DB_DATA structure */
		  cum_statsp->min_value = attr_statsp->min_value;
		  cum_statsp->max_value = attr_statsp->max_value;

		}
	      else
		{		/* if (!cum_statsp->valid_limits) */

		  /* compare with previsous values */
		  if (qo_data_compare (&attr_statsp->min_value,
				       &cum_statsp->min_value,
				       cum_statsp->type) < 0)
		    cum_statsp->min_value = attr_statsp->min_value;
		  if (qo_data_compare (&attr_statsp->max_value,
				       &cum_statsp->max_value,
				       cum_statsp->type) > 0)
		    cum_statsp->max_value = attr_statsp->max_value;
		  /* 'qo_data_compare()' is a simplized function that works
		     with DB_DATA instead of DB_VALUE. However, this way
		     would be enough to get minimum/maximum existing value,
		     because the values are meaningful only when their types
		     are numeric and we are considering compatible indexes
		     under class hierarchy. */

		}		/* if (!cum_statsp->valid_limits) */

	    }			/* if (DB_NUMERIC_TYPE(attr_statsp->type)) */


	  /* find the index that we are interesting within BTREE_STATS[] array */
	  for (k = 0, bt_statsp = attr_statsp->bt_stats; k <
	       attr_statsp->n_btstats; k++, bt_statsp++)
	    {

	      if (BTID_IS_EQUAL (&bt_statsp->btid, &(index_entryp->btid)))
		{
		  index_entryp->bt_stats_idx = k;
		  break;
		}

	    }			/* for (k = 0, ...) */
	  if (k == attr_statsp->n_btstats)
	    /* cannot find index in this attribute. what happens? */
	    continue;

	  if (QO_NODE_ENTITY_SPEC (node)->info.spec.only_all == PT_ALL)
	    {
	      /* class hierarchy spec
	         for example: select ... from all p */

	      /* check index uniqueness */
	      is_unique_index = false;	/* init */
	      if (SM_IS_CONSTRAINT_UNIQUE_FAMILY (index_entryp->type))
		{
		  is_unique_index = true;
		}

	      if (is_unique_index)
		{
		  /* is class hierarchy index: set unique index statistics */
		  cum_statsp->leafs = bt_statsp->leafs;
		  cum_statsp->pages = bt_statsp->pages;
		  cum_statsp->oids = bt_statsp->oids;
		  cum_statsp->nulls = bt_statsp->nulls;
		  cum_statsp->height = bt_statsp->height;
		  cum_statsp->keys = bt_statsp->keys;
		  cum_statsp->ukeys = bt_statsp->ukeys;
		  cum_statsp->key_type = bt_statsp->key_type;
		  cum_statsp->key_size = bt_statsp->key_size;
		  /* alloc pkeys[] within the current optimizer environment */
		  if (cum_statsp->pkeys)
		    DEALLOCATE (env, cum_statsp->pkeys);	/* free alloced */
		  cum_statsp->pkeys =
		    ALLOC_ATTR_CUM_STATS_PKEYS (env, cum_statsp->key_size);
		  if (!cum_statsp->pkeys)
		    {
		      /* already, error has been set */
		      return;	/* give up */
		    }
		  for (k = 0; k < cum_statsp->key_size; k++)
		    cum_statsp->pkeys[k] = bt_statsp->pkeys[k];

		  /* immediately finish getting index statistics */
		  return;
		}
	      else
		{
		  /* keep cumulative totals of index statistics */
		  cum_statsp->leafs += bt_statsp->leafs;
		  cum_statsp->pages += bt_statsp->pages;
		  cum_statsp->oids += bt_statsp->oids;
		  cum_statsp->nulls += bt_statsp->nulls;
		  /* Assume that the key distributions overlap here, so that
		     the number of distinct keys in all of the attributes
		     equal to the maximum number of distinct keys in any one
		     of the attributes. This is probably not far from the
		     truth; it is almost certainly a better guess than
		     assuming that all key ranges are distinct. */
		  cum_statsp->height =
		    MAX (cum_statsp->height, bt_statsp->height);
		  if (cum_statsp->key_size == 0 ||	/* the first found */
		      cum_statsp->keys < bt_statsp->keys)
		    {
		      cum_statsp->keys = bt_statsp->keys;
		      cum_statsp->ukeys = bt_statsp->ukeys;
		      cum_statsp->key_type = bt_statsp->key_type;
		      cum_statsp->key_size = bt_statsp->key_size;
		      /* alloc pkeys[] within the current optimizer environment */
		      if (cum_statsp->pkeys)
			DEALLOCATE (env, cum_statsp->pkeys);	/* free alloced */
		      cum_statsp->pkeys =
			ALLOC_ATTR_CUM_STATS_PKEYS (env,
						    cum_statsp->key_size);
		      if (!cum_statsp->pkeys)
			{
			  /* already, error has been set */
			  return;	/* give up */
			}
		      for (k = 0; k < cum_statsp->key_size; k++)
			cum_statsp->pkeys[k] = bt_statsp->pkeys[k];
		    }
		}
	    }
	  else
	    {
	      /* dynamic classes spec, etc
	         for example: select ... from (x, y, z) p
	         select ... from x p
	       */

	      /* keep cumulative totals of index statistics */
	      cum_statsp->leafs += bt_statsp->leafs;
	      cum_statsp->pages += bt_statsp->pages;
	      cum_statsp->oids += bt_statsp->oids;
	      cum_statsp->nulls += bt_statsp->nulls;
	      /* Assume that the key distributions overlap here, so that the
	         number of distinct keys in all of the attributes equal to the
	         maximum number of distinct keys in any one of the attributes.
	         This is probably not far from the truth; it is almost
	         certainly a better guess than assuming that all key ranges
	         are distinct. */
	      cum_statsp->height =
		MAX (cum_statsp->height, bt_statsp->height);
	      if (cum_statsp->key_size == 0 ||	/* the first found */
		  cum_statsp->keys < bt_statsp->keys)
		{
		  cum_statsp->keys = bt_statsp->keys;
		  cum_statsp->ukeys = bt_statsp->ukeys;
		  cum_statsp->key_type = bt_statsp->key_type;
		  cum_statsp->key_size = bt_statsp->key_size;
		  /* alloc pkeys[] within the current optimizer environment */
		  if (cum_statsp->pkeys)
		    DEALLOCATE (env, cum_statsp->pkeys);	/* free alloced */
		  cum_statsp->pkeys =
		    ALLOC_ATTR_CUM_STATS_PKEYS (env, cum_statsp->key_size);
		  if (!cum_statsp->pkeys)
		    {
		      /* already, error has been set */
		      return;	/* give up */
		    }
		  for (k = 0; k < cum_statsp->key_size; k++)
		    cum_statsp->pkeys[k] = bt_statsp->pkeys[k];
		}
	    }
	}			/* for (j = 0, ... ) */

    }				/* for (i = 0, ...) */

}				/* qo_get_index_info() */

/*
 * qo_free_node_index_info () - Free the vector and any internally allocated
 *				structures
 *   return: nothing
 *   env(in): The current optimizer environment
 *   node_indexp(in): A pointer to QO_NODE_INDEX structure of QO_NODE
 */
void
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
	      DEALLOCATE (env, cum_statsp->pkeys);
	    }
	}

      DEALLOCATE (env, node_indexp);
    }
}				/* qo_free_node_index_info */

/*
 * qo_data_compare () -
 *   return: 1, 0, -1
 *   data1(in):
 *   data2(in):
 *   type(in):
 *
 * Note: This is a simplized function that works with DB_DATA
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
      result = (data1->sh < data2->sh) ?
	-1 : ((data1->sh > data2->sh) ? 1 : 0);
      break;
    case DB_TYPE_FLOAT:
      result = (data1->f < data2->f) ? -1 : ((data1->f > data2->f) ? 1 : 0);
      break;
    case DB_TYPE_DOUBLE:
      result = (data1->d < data2->d) ? -1 : ((data1->d > data2->d) ? 1 : 0);
      break;
    case DB_TYPE_DATE:
      result = (data1->date < data2->date) ?
	-1 : ((data1->date > data2->date) ? 1 : 0);
      break;
    case DB_TYPE_TIME:
      result = (data1->time < data2->time) ?
	-1 : ((data1->time > data2->time) ? 1 : 0);
      break;
    case DB_TYPE_UTIME:
      result = (data1->utime < data2->utime) ?
	-1 : ((data1->utime > data2->utime) ? 1 : 0);
      break;
    case DB_TYPE_MONETARY:
      result = (data1->money.amount < data2->money.amount) ?
	-1 : ((data1->money.amount > data2->money.amount) ? 1 : 0);
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

  statblock->heap_size = NOMINAL_HEAP_SIZE (class_mop);
  statblock->num_objects =
    (statblock->heap_size * DB_PAGESIZE) / NOMINAL_OBJECT_SIZE (class_mop);

}				/* qo_estimate_statistics */
