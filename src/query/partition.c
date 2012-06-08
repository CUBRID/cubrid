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
 * partition_sr.c - partition pruning on server
 */

#include <assert.h>
#include "partition.h"
#include "heap_file.h"
#include "object_representation.h"
#include "object_representation_sr.h"
#include "object_domain.h"
#include "fetch.h"
#include "dbval.h"

typedef enum match_status
{
  MATCH_OK,			/* no partitions contain usable data */
  MATCH_NOT_FOUND		/* search condition does not refer partition
				   expression */
} MATCH_STATUS;

typedef enum pruning_op
{
  PO_INVALID = 0,
  PO_LT,
  PO_LE,
  PO_GT,
  PO_GE,
  PO_EQ,
  PO_NE,
  PO_IN,
  PO_NOT_IN,
  PO_IS_NULL
} PRUNING_OP;

typedef struct partition_node PARTITION_NODE;
struct partition_node
{
  OR_PARTITION *partition;
  PARTITION_NODE *next;
};

typedef struct pruning_context PRUNING_CONTEXT;
struct pruning_context
{
  THREAD_ENTRY *thread_p;	/* current thread */
  OID root_oid;			/* OID of the root class */
  ACCESS_SPEC_TYPE *spec;	/* class to prune */
  VAL_DESCR *vd;		/* value descriptor */
  DB_PARTITION_TYPE partition_type;	/* hash, range, list */

  OR_PARTITION *partitions;	/* partitions array */
  int count;			/* number of partitions */

  void *fp_cache_context;	/* unpacking info */
  FUNC_PRED *partition_pred;	/* partition predicate */
  int attr_position;		/* attribute position in index key */
  ATTR_ID attr_id;		/* id of the attribute which defines the
				 * partitions */
  int error_code;		/* error encountered during pruning */
};

#define PARTITION_CACHE_NAME "Partitions_Cache"
#define PARTITION_CACHE_SIZE 200

/* partition cache hash table */
static MHT_TABLE *db_Partition_Ht = NULL;

#define PARTITION_IS_CACHE_INITIALIZED() (db_Partition_Ht != NULL)

typedef struct partition_cache_entry PARTITION_CACHE_ENTRY;
struct partition_cache_entry
{
  OID class_oid;		/* root class OID */

  OR_PARTITION *partitions;	/* partitions information */
  int count;			/* number of partitions */

  ATTR_ID attr_id;		/* attribute id of the partitioning key */
};

static int partition_free_cache_entry (const void *key, void *data,
				       void *args);
static int partition_cache_pruning_context (PRUNING_CONTEXT * pinfo);
static bool partition_load_context_from_cache (PRUNING_CONTEXT * pinfo,
					       bool * is_modified);
static int partition_cache_entry_to_pruning_context (PRUNING_CONTEXT * pinfo,
						     PARTITION_CACHE_ENTRY *
						     entry_p);
static PARTITION_CACHE_ENTRY
  * partition_pruning_context_to_cache_entry (PRUNING_CONTEXT * pinfo);
static PRUNING_OP partition_rel_op_to_pruning_op (REL_OP op);
static void partition_init_pruning_context (PRUNING_CONTEXT * pinfo);
static int partition_load_pruning_context (THREAD_ENTRY * thread_p,
					   const OID * class_oid,
					   PRUNING_CONTEXT * pinfo);
static void partition_clear_pruning_context (PRUNING_CONTEXT * pinfo);
static int partition_load_partition_predicate (PRUNING_CONTEXT * pinfo,
					       OR_PARTITION * master);
static int partition_get_position_in_key (PRUNING_CONTEXT * pinfo);
static ATTR_ID partition_get_attribute_id (REGU_VARIABLE * regu_var);
static void partition_set_cache_info_for_expr (REGU_VARIABLE * regu_var,
					       ATTR_ID attr_id,
					       HEAP_CACHE_ATTRINFO * info);
static PARTITION_NODE *partition_match_pred_expr (PRUNING_CONTEXT * pinfo,
						  PARTITION_NODE * partitions,
						  const PRED_EXPR * pr,
						  MATCH_STATUS * status);
static PARTITION_NODE *partition_match_index (PRUNING_CONTEXT * pinfo,
					      PARTITION_NODE * partitions,
					      const KEY_INFO * key,
					      RANGE_TYPE range_type,
					      MATCH_STATUS * status);
static PARTITION_NODE *partition_match_key_range (PRUNING_CONTEXT * pinfo,
						  const PARTITION_NODE *
						  partitions,
						  const KEY_RANGE * range,
						  MATCH_STATUS * status);
static bool partition_do_regu_variables_match (PRUNING_CONTEXT * pinfo,
					       const REGU_VARIABLE * left,
					       const REGU_VARIABLE * right);
static PARTITION_NODE *partition_prune (PRUNING_CONTEXT * pinfo,
					const PARTITION_NODE * partitions,
					const REGU_VARIABLE * arg,
					const PRUNING_OP op,
					MATCH_STATUS * status);
static PARTITION_NODE *partition_prune_db_val (PRUNING_CONTEXT * pinfo,
					       const PARTITION_NODE * parts,
					       const DB_VALUE * val,
					       const PRUNING_OP op,
					       MATCH_STATUS * status);
static int partition_get_value_from_key (PRUNING_CONTEXT * pinfo,
					 const REGU_VARIABLE * key,
					 DB_VALUE * attr_key,
					 bool * is_present);
static int partition_get_value_from_inarith (PRUNING_CONTEXT * pinfo,
					     const REGU_VARIABLE * src,
					     DB_VALUE * value_p,
					     bool * is_present);
static int partition_get_value_from_regu_var (PRUNING_CONTEXT * pinfo,
					      const REGU_VARIABLE * key,
					      DB_VALUE * value_p,
					      bool * is_value);
static PARTITION_NODE *partition_prune_range (PRUNING_CONTEXT * pinfo,
					      const PARTITION_NODE *
					      partitions,
					      const DB_VALUE * val,
					      const PRUNING_OP op,
					      MATCH_STATUS * status);
static PARTITION_NODE *partition_prune_list (PRUNING_CONTEXT * pinfo,
					     const PARTITION_NODE *
					     partitions, const DB_VALUE * val,
					     const PRUNING_OP op,
					     MATCH_STATUS * status);
static PARTITION_NODE *partition_prune_hash (PRUNING_CONTEXT * pinfo,
					     const PARTITION_NODE *
					     partitions, const DB_VALUE * val,
					     const PRUNING_OP op,
					     MATCH_STATUS * status);
static int partition_find_partition_for_record (PRUNING_CONTEXT * pinfo,
						const OID * class_oid,
						RECDES * recdes,
						OID * partition_oid,
						HFID * partition_hfid);
static int partition_prune_heap_scan (PRUNING_CONTEXT * pinfo);

static int partition_prune_index_scan (PRUNING_CONTEXT * pinfo);

/* partition list manipulation functions */
static PARTITION_NODE *partition_new_node (THREAD_ENTRY * thread_p,
					   OR_PARTITION * partition);
static void partition_free_list (THREAD_ENTRY * thread_p,
				 PARTITION_NODE * list);
static PARTITION_NODE *partition_build_list (THREAD_ENTRY * thread_p,
					     OR_PARTITION * partitions,
					     int parts_count);
static PARTITION_NODE *partition_intersect_lists (THREAD_ENTRY *
						  thread_p,
						  PARTITION_NODE * left,
						  PARTITION_NODE * right);
static PARTITION_NODE *partition_merge_lists (THREAD_ENTRY * thread_p,
					      PARTITION_NODE * left,
					      PARTITION_NODE * right);
static PARTITION_NODE *partition_add_node_to_list (THREAD_ENTRY *
						   thread_p,
						   PARTITION_NODE * list,
						   OR_PARTITION * partition);
static int partition_count_elements_in_list (PARTITION_NODE * list);
static int partition_list_to_spec_list (PRUNING_CONTEXT * pinfo,
					PARTITION_NODE * list);

/*
 * partition_free_cache_entry () - free memory allocated for a cache entry
 * return : error code or NO_ERROR
 * key (in)   :
 * data (in)  :
 * args (in)  :
 *
 * Note: Since cache entries are not allocated in the private heaps used
 *  by threads, this function changes the private heap of the calling thread
 *  to the '0' heap (to use malloc/free) before actually freeing the memory
 *  and sets it back after it finishes
 */
static int
partition_free_cache_entry (const void *key, void *data, void *args)
{
  PARTITION_CACHE_ENTRY *entry = (PARTITION_CACHE_ENTRY *) data;
  HL_HEAPID old_heap;

  /* change private heap */
  old_heap = db_change_private_heap (NULL, 0);
  if (entry != NULL)
    {
      if (entry->partitions != NULL)
	{
	  int i = 0;
	  for (i = 0; i < entry->count; i++)
	    {
	      db_seq_free (entry->partitions[i].values);
	    }
	  free_and_init (entry->partitions);
	}
      free_and_init (entry);
    }

  db_change_private_heap (NULL, old_heap);

  return NO_ERROR;
}

/*
 * partition_cache_entry_to_pruning_context () - create a pruning context from
 *						 a cache entry
 * return : error code or NO_ERROR
 * pinfo (in/out) : pruning context
 * entry_p (in)	  : cache entry
 */
static int
partition_cache_entry_to_pruning_context (PRUNING_CONTEXT * pinfo,
					  PARTITION_CACHE_ENTRY * entry_p)
{
  int i;

  assert (pinfo != NULL);
  assert (entry_p != NULL);

  pinfo->count = entry_p->count;
  if (pinfo->count == 0)
    {
      assert (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      pinfo->error_code = ER_FAILED;
      return ER_FAILED;
    }

  pinfo->partitions =
    (OR_PARTITION *) db_private_alloc (pinfo->thread_p,
				       pinfo->count * sizeof (OR_PARTITION));
  if (pinfo->partitions == NULL)
    {
      pinfo->error_code = ER_FAILED;
      goto error_return;
    }

  for (i = 0; i < pinfo->count; i++)
    {
      COPY_OID (&pinfo->partitions[i].db_part_oid,
		&entry_p->partitions[i].db_part_oid);

      COPY_OID (&pinfo->partitions[i].class_oid,
		&entry_p->partitions[i].class_oid);

      HFID_COPY (&pinfo->partitions[i].class_hfid,
		 &entry_p->partitions[i].class_hfid);

      pinfo->partitions[i].partition_type =
	entry_p->partitions[i].partition_type;
      if (entry_p->partitions[i].values != NULL)
	{
	  pinfo->partitions[i].values =
	    db_seq_copy (entry_p->partitions[i].values);
	  if (pinfo->partitions[i].values == NULL)
	    {
	      pinfo->error_code = ER_FAILED;
	      goto error_return;
	    }
	}
    }

  pinfo->attr_id = entry_p->attr_id;

  pinfo->partition_type = pinfo->partitions[0].partition_type;

  return NO_ERROR;

error_return:

  if (pinfo->partitions != NULL)
    {
      int j;
      /* i holds the actual count of allocated partition values */
      for (j = 0; i < i; j++)
	{
	  if (pinfo->partitions[j].values != NULL)
	    {
	      db_seq_free (pinfo->partitions[j].values);
	    }
	}
      db_private_free_and_init (pinfo->thread_p, pinfo->partitions);
      pinfo->count = 0;
    }

  return ER_FAILED;
}

/*
 * partition_pruning_context_to_cache_entry () - create a cache entry from
 *						 a pruning context
 * return : cache entry object
 * pinfo (in) : pruning context
 */
static PARTITION_CACHE_ENTRY *
partition_pruning_context_to_cache_entry (PRUNING_CONTEXT * pinfo)
{
  PARTITION_CACHE_ENTRY *entry_p = NULL;
  int i;
  HL_HEAPID old_heap_id = 0;

  assert (pinfo != NULL);

  entry_p = (PARTITION_CACHE_ENTRY *) malloc (sizeof (PARTITION_CACHE_ENTRY));
  if (entry_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      sizeof (PARTITION_CACHE_ENTRY));
      pinfo->error_code = ER_FAILED;
      goto error_return;
    }
  entry_p->partitions = NULL;
  entry_p->count = 0;

  COPY_OID (&entry_p->class_oid, &pinfo->root_oid);
  entry_p->attr_id = pinfo->attr_id;

  entry_p->count = pinfo->count;

  entry_p->partitions =
    (OR_PARTITION *) malloc (entry_p->count * sizeof (OR_PARTITION));
  if (entry_p->partitions == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      sizeof (PARTITION_CACHE_ENTRY));
      pinfo->error_code = ER_FAILED;
      goto error_return;
    }

  /* Change private heap to 0 to use malloc/free for entry_p allocated values.
   * These values outlast the current thread heap
   */
  old_heap_id = db_change_private_heap (pinfo->thread_p, 0);
  for (i = 0; i < entry_p->count; i++)
    {
      COPY_OID (&entry_p->partitions[i].db_part_oid,
		&pinfo->partitions[i].db_part_oid);

      COPY_OID (&entry_p->partitions[i].class_oid,
		&pinfo->partitions[i].class_oid);

      HFID_COPY (&entry_p->partitions[i].class_hfid,
		 &pinfo->partitions[i].class_hfid);

      entry_p->partitions[i].partition_type =
	pinfo->partitions[i].partition_type;
      if (pinfo->partitions[i].values != NULL)
	{
	  entry_p->partitions[i].values =
	    db_seq_copy (pinfo->partitions[i].values);
	  if (entry_p->partitions[i].values == NULL)
	    {
	      pinfo->error_code = ER_FAILED;
	      goto error_return;
	    }
	}
    }

  /* restore heap id */
  db_change_private_heap (pinfo->thread_p, old_heap_id);

  return entry_p;

error_return:
  if (entry_p != NULL)
    {
      if (entry_p->partitions != NULL)
	{
	  int j = 0;
	  for (j = 0; j < i; j++)
	    {
	      if (entry_p->partitions[j].values != NULL)
		{
		  db_seq_free (entry_p->partitions[j].values);
		}
	    }
	  free_and_init (entry_p->partitions);
	}
      free_and_init (entry_p);
    }
  if (old_heap_id != 0)
    {
      db_change_private_heap (pinfo->thread_p, old_heap_id);
    }

  return NULL;
}

/*
 * partition_cache_pruning_context () - cache a pruning context
 * return : error code or NO_ERROR
 * pinfo (in) : pruning context
 */
static int
partition_cache_pruning_context (PRUNING_CONTEXT * pinfo)
{
  PARTITION_CACHE_ENTRY *entry_p = NULL;
  OID *oid_key = NULL;

  if (!PARTITION_IS_CACHE_INITIALIZED ())
    {
      return NO_ERROR;
    }

  assert (pinfo != NULL);

  /* Check if this class is being modified by this transaction. If this is
   * the case, we can't actually use this cache entry because the next request
   * might have already modified the partitioning schema and we won't know it
   */
  if (log_is_class_being_modified (pinfo->thread_p, &pinfo->root_oid))
    {
      return NO_ERROR;
    }

  entry_p = partition_pruning_context_to_cache_entry (pinfo);
  if (entry_p == NULL)
    {
      return ER_FAILED;
    }

  oid_key = &entry_p->class_oid;
  if (csect_enter (pinfo->thread_p, CSECT_PARTITION_CACHE, INF_WAIT)
      != NO_ERROR)
    {
      return ER_FAILED;
    }

  (void) mht_put (db_Partition_Ht, oid_key, entry_p);

  csect_exit (CSECT_PARTITION_CACHE);

  return NO_ERROR;
}

/*
 * partition_load_context_from_cache () - load pruning info from cache
 * return : true if the pruning context was found in cache, false otherwise
 * pinfo (in/out)      : pruning context
 * is_modfied (in/out) : true if the class schema is being modified
 *
 * Note: If the class schema is being modified by the current transaction, the
 *  cache info is not used since it might not reflect the latest changes.
 *  There is no way that the class schema is being modified by another
 *  transaction than the one which is requesting the current pruning context.
 *  Any schema changes place a X_LOCK on the class and the transaction
 *  performing the operation has exclusive access to it (and implicitly to the
 *  entry in the cache). Because of this, we only have to look for the class
 *  in the modified list of the current transaction. If this changes (classes
 *  are no longer exclusively locked for alter), we have to review this
 *  function.
 */
static bool
partition_load_context_from_cache (PRUNING_CONTEXT * pinfo, bool * is_modfied)
{
  PARTITION_CACHE_ENTRY *entry_p = NULL;

  if (!PARTITION_IS_CACHE_INITIALIZED ())
    {
      return false;
    }

  assert (pinfo != NULL);

  /* Check if this class is being modified by this transaction. If this is
   * the case, we can't actually use this cache entry because the next request
   * might have already modified the partitioning schema and we won't know it
   */
  if (log_is_class_being_modified (pinfo->thread_p, &pinfo->root_oid))
    {
      if (is_modfied != NULL)
	{
	  *is_modfied = true;
	}
      return false;
    }
  if (is_modfied != NULL)
    {
      *is_modfied = false;
    }

  if (csect_enter_as_reader (pinfo->thread_p, CSECT_PARTITION_CACHE, INF_WAIT)
      != NO_ERROR)
    {
      pinfo->error_code = ER_FAILED;
      return false;
    }

  entry_p = (PARTITION_CACHE_ENTRY *) mht_get (db_Partition_Ht,
					       &pinfo->root_oid);
  if (entry_p == NULL)
    {
      csect_exit (CSECT_PARTITION_CACHE);
      return false;
    }
  if (partition_cache_entry_to_pruning_context (pinfo, entry_p) != NO_ERROR)
    {
      csect_exit (CSECT_PARTITION_CACHE);
      return false;
    }

  csect_exit (CSECT_PARTITION_CACHE);
  return true;
}

/*
 * partition_cache_init () - Initialize partition cache area
 *   return: NO_ERROR or error code
 *   thread_p (in) : thread entry
 *
 * Note: Creates and initializes a main memory hash table that will be
 * used by pruning operations. This routine should only be called once during
 * server boot.
 */
int
partition_cache_init (THREAD_ENTRY * thread_p)
{
  int error = NO_ERROR;

  er_log_debug (ARG_FILE_LINE, "creating partition cache\n");

  if (csect_enter (thread_p, CSECT_PARTITION_CACHE, INF_WAIT) != NO_ERROR)
    {
      return ER_FAILED;
    }
  if (PARTITION_IS_CACHE_INITIALIZED ())
    {
      /* nothing to do */
      goto cleanup;
    }

  db_Partition_Ht =
    mht_create (PARTITION_CACHE_NAME, PARTITION_CACHE_SIZE, oid_hash,
		oid_compare_equals);
  if (db_Partition_Ht == NULL)
    {
      error = ER_FAILED;
      goto cleanup;
    }

cleanup:
  csect_exit (CSECT_PARTITION_CACHE);
  return error;
}

/*
 * partition_cache_finalize () - cleanup the partition cache
 *   thread_p (in) : thread entry
 *
 * Note: This function deletes the partition cache.
 *	 This function should be called only during server shutdown
 */
void
partition_cache_finalize (THREAD_ENTRY * thread_p)
{
  er_log_debug (ARG_FILE_LINE, "deleting partition cache\n");

  if (csect_enter (thread_p, CSECT_PARTITION_CACHE, INF_WAIT) != NO_ERROR)
    {
      return;
    }

  if (PARTITION_IS_CACHE_INITIALIZED ())
    {
      (void) mht_map (db_Partition_Ht, partition_free_cache_entry, NULL);
      mht_destroy (db_Partition_Ht);
      db_Partition_Ht = NULL;
    }

  csect_exit (CSECT_PARTITION_CACHE);
}

/*
 * partition_decache_class () - remove a class from the partition hash
 * return : void
 * thread_p (in)  : thread entry
 * class_oid (in) : class OID
 *
 */
void
partition_decache_class (THREAD_ENTRY * thread_p, const OID * class_oid)
{
  int error = NO_ERROR;

  if (!PARTITION_IS_CACHE_INITIALIZED ())
    {
      return;
    }

  if (csect_enter (thread_p, CSECT_PARTITION_CACHE, INF_WAIT) != NO_ERROR)
    {
      return;
    }

  (void) mht_rem (db_Partition_Ht, class_oid, partition_free_cache_entry,
		  NULL);

  csect_exit (CSECT_PARTITION_CACHE);
}

/*
 * partition_rel_op_to_pruning_op () - convert REL_OP to PRUNING_OP
 * return : pruning operator
 * op (in) :
 */
static PRUNING_OP
partition_rel_op_to_pruning_op (REL_OP op)
{
  switch (op)
    {
    case R_EQ:
    case R_EQ_TORDER:
    case R_NULLSAFE_EQ:
      return PO_EQ;
    case R_NE:
      return PO_NE;
    case R_GT:
      return PO_GT;
    case R_GE:
      return PO_GE;
    case R_LT:
      return PO_LT;
    case R_LE:
      return PO_LE;
    case R_EQ_SOME:
      return PO_IN;
    case R_NE_ALL:
      return PO_NOT_IN;
    case R_NULL:
      return PO_IS_NULL;
    case R_EXISTS:
    case R_NE_SOME:
    case R_GT_SOME:
    case R_GE_SOME:
    case R_LT_SOME:
    case R_LE_SOME:
    case R_EQ_ALL:
    case R_GT_ALL:
    case R_GE_ALL:
    case R_LT_ALL:
    case R_LE_ALL:
    case R_SUPERSETEQ:
    case R_SUBSETEQ:
    case R_SUPERSET:
    case R_SUBSET:
      return PO_INVALID;
    case R_LIKE:
    default:
      return PO_INVALID;
    }
  return PO_INVALID;
}

/*
 * partition_new_node () - create a new node
 * return : node or null
 * thread_p (in)  :
 * partition (in) : partition information for the node
 */
static PARTITION_NODE *
partition_new_node (THREAD_ENTRY * thread_p, OR_PARTITION * partition)
{
  PARTITION_NODE *node =
    (PARTITION_NODE *) db_private_alloc (thread_p, sizeof (PARTITION_NODE));

  if (node == NULL)
    {
      return NULL;
    }
  node->partition = partition;
  node->next = NULL;

  return node;
}

/*
 * partition_free_list () - free nodes in a list
 * return : void
 * thread_p (in) :
 * list (in)	 : list to be freed
 */
static void
partition_free_list (THREAD_ENTRY * thread_p, PARTITION_NODE * list)
{
  while (list != NULL)
    {
      PARTITION_NODE *next = list->next;

      db_private_free (thread_p, list);
      list = next;
    }
}

/*
 * partition_build_list () - create a PARTITION_NODE list from an array of
 *			     partitions
 * return : partition list or null
 * thread_p (in)    :
 * partitions (in)  : array of elements to put into the list
 * parts_count (in) : number of elements in partitions array
 *
 *  Note: The code dealing with pruning has to perform merges and
 *  intersections on lists of partitions. In order to speed up these
 *  operations we will work with sorted lists and since each partition info
 *  from a node is a pointer to an element from an array, the easiest way to
 *  accomplish this is to use the address of the node->partitions pointer as
 *  an ordering criteria.
 */
static PARTITION_NODE *
partition_build_list (THREAD_ENTRY * thread_p, OR_PARTITION * partitions,
		      int parts_count)
{
  PARTITION_NODE *list = NULL;
  int i = 0;

  /* We will insert elements at the beginning of the list but we want the
   * list to be sorted ascending. This is why we parser partitions from end to
   * start
   */
  list = partition_new_node (thread_p, &partitions[parts_count - 1]);
  if (list == NULL)
    {
      goto error_exit;
    }

  for (i = parts_count - 2; i >= 0; i--)
    {
      PARTITION_NODE *node = partition_new_node (thread_p, &partitions[i]);

      if (node == NULL)
	{
	  goto error_exit;
	}
      node->partition = &partitions[i];
      node->next = list;
      list = node;
    }

  return list;

error_exit:
  (void) partition_free_list (thread_p, list);
  return NULL;
}

/*
 * partition_merge_lists () - perform reunion on two lists
 * return : 
 * thread_p (in)  :
 * left (in)	  :
 * right (in)	  :
 *
 *  Note: this function performs reunion on left and right lists
 *  and returns the result as a new list. Nodes not used from left and right
 *  lists are freed during this merge.
 */
static PARTITION_NODE *
partition_merge_lists (THREAD_ENTRY * thread_p, PARTITION_NODE * left,
		       PARTITION_NODE * right)
{
  PARTITION_NODE *merge = NULL, *mnext = NULL;
  if (left == NULL)
    {
      return right;
    }
  if (right == NULL)
    {
      return left;
    }

  if (left->partition > right->partition)
    {
      merge = right;
      right = right->next;
      merge->next = NULL;
    }
  else
    {
      merge = left;
      left = left->next;
      merge->next = NULL;

      if (merge->partition == right->partition)
	{
	  PARTITION_NODE *aux = right;
	  right = right->next;
	  db_private_free (thread_p, aux);
	}
    }

  mnext = merge;
  while (left != NULL && right != NULL)
    {
      if (left->partition > right->partition)
	{
	  /* add right and move to next right */
	  mnext->next = right;
	  right = right->next;
	  mnext = mnext->next;
	  mnext->next = NULL;
	}
      else
	{
	  /* add left and move to the next left */
	  mnext->next = left;
	  left = left->next;
	  mnext = mnext->next;
	  mnext->next = NULL;
	  if (mnext->partition == right->partition)
	    {
	      /* also move right to next and free current right */
	      PARTITION_NODE *aux = right;
	      right = right->next;
	      db_private_free (thread_p, aux);
	    }
	}
    }

  /* add what's left */
  if (left != NULL)
    {
      mnext->next = left;
    }
  else if (right != NULL)
    {
      mnext->next = right;
    }

  return merge;
}

/*
 * partition_intersect_lists () - perform intersection on two lists
 * return : 
 * thread_p (in)  :
 * left (in)	  :
 * right (in)	  :
 *
 *  Note: this function performs intersection between left and right lists
 *  and returns the result as a new list. Nodes not used from left or right
 *  lists are freed during this merge.
 */
static PARTITION_NODE *
partition_intersect_lists (THREAD_ENTRY * thread_p, PARTITION_NODE * left,
			   PARTITION_NODE * right)
{
  PARTITION_NODE *merge = NULL, *mnext = NULL;

  /* We know that left and right lists are sorted and we will use this
   * information to speed up the intersection
   */
  if (left == NULL || right == NULL)
    {
      /* nothing to merge */
      (void) partition_free_list (thread_p, left);
      (void) partition_free_list (thread_p, right);
      return NULL;
    }

  if (left->partition > right->partition)
    {
      /* swap lists, it will make things easier */
      PARTITION_NODE *aux = right;
      right = left;
      left = aux;
    }

  while (left != NULL && right != NULL)
    {
      PARTITION_NODE *lnext, *rnext;

      if (left->partition == right->partition)
	{
	  /* Add left to merge and free right. Then move to the next node
	   * in left and next node in right
	   */
	  if (merge == NULL)
	    {
	      merge = left;
	      left = left->next;
	      merge->next = NULL;
	      mnext = merge;
	    }
	  else
	    {
	      mnext->next = left;
	      left = left->next;
	      mnext->next->next = NULL;
	      mnext = mnext->next;
	    }
	  /* free right, we don't need it anymore */
	  rnext = right->next;
	  db_private_free (thread_p, right);
	  right = rnext;
	  continue;
	}
      else if (left->partition < right->partition)
	{
	  /* left is not in right. Free current left node and move to the next
	     one */
	  lnext = left->next;
	  db_private_free (thread_p, left);
	  left = lnext;
	}
      else if (left->partition > right->partition)
	{
	  /* right is not in left. Free current right node and move to the
	     next one */
	  rnext = right->next;
	  db_private_free (thread_p, right);
	  right = rnext;
	}
    }

  /* at this point, if either left or right are not NULL, we have to free them
     because they don't contain useful information */
  (void) partition_free_list (thread_p, left);
  (void) partition_free_list (thread_p, right);

  return merge;
}

/*
 * partition_add_node_to_list () - add a node to a list
 * return : list or NULL on error
 * thread_p (in)  : thread
 * list (in)	  : list to add the new node to
 * partition (in) : contents of the new node
 *
 *  Note: in case of an error, this function also frees the memory allocated
 *  for the list
 */
static PARTITION_NODE *
partition_add_node_to_list (THREAD_ENTRY * thread_p, PARTITION_NODE * list,
			    OR_PARTITION * partition)
{
  PARTITION_NODE *n = NULL, *node = NULL;

  node = partition_new_node (thread_p, partition);
  if (node == NULL)
    {
      (void) partition_free_list (thread_p, list);
      return NULL;
    }

  if (list == NULL)
    {
      /* no elements in the list, just return node as new head */
      return node;
    }

  if (list->partition >= node->partition)
    {
      /* we have to insert it before the head of the list */
      node->next = list;
      return node;
    }

  /* search for appropriate location into which to insert this node */
  n = list;
  while (n != NULL)
    {
      PARTITION_NODE *next = n->next;
      if ((n->partition <= node->partition)
	  && (next == NULL || node->partition <= next->partition))
	{
	  /* if element =< new_node =< next, we found the place for it */
	  node->next = n->next;
	  n->next = node;
	  break;
	}
      n = n->next;
    }

  return list;
}

/*
 * partition_count_elements_in_list () - count the elements in a list
 * return : number of elements
 * list (in) : list
 */
static int
partition_count_elements_in_list (PARTITION_NODE * list)
{
  int count = 0;

  while (list)
    {
      count++;
      list = list->next;
    }

  return count;
}

/*
 * partition_list_to_spec_list () - convert a list of nodes to an array of
 *				    PARTITION_SPEC_TYPE elements
 * return : error code or NO_ERROR
 * thread_p (in)      :
 * list (in)	      :
 * spec_list (in/out) :
 */
static int
partition_list_to_spec_list (PRUNING_CONTEXT * pinfo, PARTITION_NODE * list)
{
  int cnt = 0, i = 0, error = NO_ERROR;
  PARTITION_NODE *node = NULL;
  PARTITION_SPEC_TYPE *spec = NULL;
  bool is_index = false;
  char *btree_name = NULL;
  OID *master_oid = NULL;
  BTID *master_btid = NULL;

  cnt = partition_count_elements_in_list (list);
  if (cnt == 0)
    {
      /* pruning did not find any partition, just return */
      error = NO_ERROR;
      goto cleanup;
    }

  if (pinfo->spec->access == INDEX)
    {
      /* we have to load information about the index used so we can duplicate
         it for each partition */
      is_index = true;
      master_oid = &ACCESS_SPEC_CLS_OID (pinfo->spec);
      master_btid = &pinfo->spec->indexptr->indx_id.i.btid;
      error =
	heap_get_indexinfo_of_btid (pinfo->thread_p,
				    master_oid, master_btid,
				    NULL, NULL, NULL, NULL, &btree_name);
      if (error != NO_ERROR)
	{
	  goto cleanup;
	}
    }

  spec = (PARTITION_SPEC_TYPE *)
    db_private_alloc (pinfo->thread_p, cnt * sizeof (PARTITION_SPEC_TYPE));
  if (spec == NULL)
    {
      error = ER_FAILED;
      goto cleanup;
    }

  for (i = 0, node = list; i < cnt; i++, node = node->next)
    {
      assert (node != NULL);

      COPY_OID (&spec[i].oid, &node->partition->class_oid);
      HFID_COPY (&spec[i].hfid, &node->partition->class_hfid);
      if (i == cnt - 1)
	{
	  spec[i].next = NULL;
	}
      else
	{
	  spec[i].next = &spec[i + 1];
	}

      if (is_index)
	{
	  error =
	    heap_get_index_with_name (pinfo->thread_p, &spec[i].oid,
				      btree_name, &spec[i].indx_id.i.btid);
	  spec[i].indx_id.type = T_BTID;

	  if (error != NO_ERROR)
	    {
	      goto cleanup;
	    }
	}
    }

cleanup:
  if (btree_name != NULL)
    {
      /* heap_get_indexinfo_of_btid calls strdup on the index name so we have
         to free it with free_and_init */
      free_and_init (btree_name);
    }

  pinfo->spec->curent = NULL;

  if (error != NO_ERROR)
    {
      if (spec != NULL)
	{
	  db_private_free (pinfo->thread_p, spec);
	}
      pinfo->spec->parts = NULL;
    }
  else
    {
      pinfo->spec->parts = spec;
    }

  return error;
}

/*
 * partition_do_regu_variables_match () - check if two regu variables match
 * return : 
 * pinfo (in) :
 * left (in)  :
 * right (in) :
 */
static bool
partition_do_regu_variables_match (PRUNING_CONTEXT * pinfo,
				   const REGU_VARIABLE * left,
				   const REGU_VARIABLE * right)
{
  if (left == NULL || right == NULL)
    {
      return left == right;
    }

  if (left->type != right->type)
    {
      return false;
    }

  switch (left->type)
    {
    case TYPE_DBVAL:
      /* use dbval */
      if (tp_value_compare (&left->value.dbval, &right->value.dbval, 1, 0)
	  != DB_EQ)
	{
	  return false;
	}
      else
	{
	  return true;
	}

    case TYPE_CONSTANT:
      /* use varptr */
      if (tp_value_compare (left->value.dbvalptr, right->value.dbvalptr,
			    1, 1) != DB_EQ)
	{
	  return false;
	}
      else
	{
	  return true;
	}

    case TYPE_POS_VALUE:
      {
	/* use val_pos for host variable references */
	DB_VALUE *val_left =
	  (DB_VALUE *) pinfo->vd->dbval_ptr + left->value.val_pos;
	DB_VALUE *val_right =
	  (DB_VALUE *) pinfo->vd->dbval_ptr + right->value.val_pos;
	if (tp_value_compare (val_left, val_right, 1, 1) != DB_EQ)
	  {
	    return false;
	  }
	else
	  {
	    return true;
	  }
      }

    case TYPE_INARITH:
    case TYPE_OUTARITH:
      /* use arithptr */
      if (left->value.arithptr->opcode != right->value.arithptr->opcode)
	{
	  /* different operations */
	  return false;
	}
      /* check misc_operand for EXTRACT, etc */
      if (left->value.arithptr->misc_operand
	  != left->value.arithptr->misc_operand)
	{
	  return false;
	}
      /* check left operand */
      if (!partition_do_regu_variables_match (pinfo,
					      left->value.arithptr->leftptr,
					      right->value.arithptr->leftptr))
	{
	  return false;
	}
      /* check right operand */
      if (!partition_do_regu_variables_match (pinfo,
					      left->value.arithptr->rightptr,
					      right->value.arithptr->
					      rightptr))
	{
	  return false;
	}

      /* check third operand */
      if (!partition_do_regu_variables_match (pinfo,
					      left->value.arithptr->thirdptr,
					      right->value.arithptr->
					      thirdptr))
	{
	  return false;
	}
      return true;

    case TYPE_ATTR_ID:
      /* use attr_descr */
      return left->value.attr_descr.id == right->value.attr_descr.id;

    default:
      return false;
    }

  return false;
}

/*
 * partition_prune_list () - Perform pruning for LIST type partitions
 * return : partition list
 * pinfo (in)	    : pruning context
 * partitions (in)  : list of partitions on which to perform pruning
 * val	      (in)  : the value to which the partition expression is compared
 * op (in)	    : operator to apply
 * status (in/out)  : match status
 */
static PARTITION_NODE *
partition_prune_list (PRUNING_CONTEXT * pinfo,
		      const PARTITION_NODE * partitions,
		      const DB_VALUE * val, const PRUNING_OP op,
		      MATCH_STATUS * status)
{
  PARTITION_NODE *parts = NULL;
  const PARTITION_NODE *part = NULL;
  int size = 0, i = 0;
  DB_SEQ *part_collection = NULL;
  DB_COLLECTION *val_collection = NULL;
  *status = MATCH_NOT_FOUND;

  part = partitions;

  while (part)
    {
      part_collection = part->partition->values;
      size = db_set_size (part_collection);
      if (size < 0)
	{
	  *status = MATCH_NOT_FOUND;
	  goto cleanup;
	}

      switch (op)
	{
	case PO_IN:
	  {
	    DB_VALUE col;
	    bool found = false;
	    /* Perform intersection on part_values and val. If the
	     * result is not empty then this partition should be added
	     * to the pruned list
	     */
	    if (!db_value_type_is_collection (val))
	      {
		*status = MATCH_NOT_FOUND;
		return NULL;
	      }

	    val_collection = DB_GET_COLLECTION (val);
	    for (i = 0; i < size; i++)
	      {
		db_set_get (part_collection, i, &col);
		if (db_set_ismember (val_collection, &col))
		  {
		    pr_clear_value (&col);
		    found = true;
		    break;
		  }
		pr_clear_value (&col);
	      }

	    if (found)
	      {
		/* add this partition */
		parts = partition_add_node_to_list (pinfo->thread_p, parts,
						    part->partition);

	      }
	    *status = MATCH_OK;
	    break;
	  }

	case PO_NOT_IN:
	  {
	    DB_VALUE col;
	    bool found = false;
	    /* Perform intersection on part_values and val. If the
	     * result is empty then this partition should be added
	     * to the pruned list
	     */
	    if (!db_value_type_is_collection (val))
	      {
		*status = MATCH_NOT_FOUND;
		return NULL;
	      }

	    val_collection = DB_GET_COLLECTION (val);
	    for (i = 0; i < size; i++)
	      {
		db_set_get (part_collection, i, &col);
		if (db_set_ismember (val_collection, &col))
		  {
		    pr_clear_value (&col);
		    found = true;
		    break;
		  }
		pr_clear_value (&col);
	      }

	    if (!found)
	      {
		/* add this partition */
		parts = partition_add_node_to_list (pinfo->thread_p, parts,
						    part->partition);

	      }
	    *status = MATCH_OK;
	    break;
	  }

	case PO_IS_NULL:
	  /* Add this partition if part_values contains val. */
	  if (db_set_has_null (part_collection))
	    {
	      /* add this partition */
	      parts = partition_add_node_to_list (pinfo->thread_p, parts,
						  part->partition);
	      /* Since partition values are disjoint sets this is the only
	         possible result */
	      *status = MATCH_OK;
	      return parts;
	    }
	  break;

	case PO_EQ:
	  /* Add this partition if part_values contains val. */
	  if (db_set_ismember (part_collection, (DB_VALUE *) val))
	    {
	      /* add this partition */
	      parts = partition_add_node_to_list (pinfo->thread_p, parts,
						  part->partition);
	      /* Since partition values are disjoint sets this is the only
	         possible result */
	      *status = MATCH_OK;
	      return parts;
	    }
	  break;

	case PO_NE:
	case PO_LT:
	case PO_LE:
	case PO_GT:
	case PO_GE:
	default:
	  *status = MATCH_NOT_FOUND;
	  parts = NULL;
	  break;
	}
      part = part->next;
    }

cleanup:
  return parts;
}

/*
 * partition_prune_hash () - Perform pruning for HASH type partitions
 * return : partition list
 * pinfo (in)	    : pruning context
 * partitions (in)  : list of partitions on which to perform pruning
 * val	      (in)  : the value to which the partition expression is compared
 * op (in)	    : operator to apply
 * status (in/out)  : match status
 */
static PARTITION_NODE *
partition_prune_hash (PRUNING_CONTEXT * pinfo,
		      const PARTITION_NODE * partitions,
		      const DB_VALUE * val_p, const PRUNING_OP op,
		      MATCH_STATUS * status)
{
  PARTITION_NODE *parts = NULL;
  int idx = 0;
  int hash_size = pinfo->count - 1;
  TP_DOMAIN *col_domain = NULL;
  DB_VALUE val;

  *status = MATCH_NOT_FOUND;
  col_domain = pinfo->partition_pred->func_regu->domain;
  switch (op)
    {
    case PO_EQ:
      if (TP_DOMAIN_TYPE (col_domain) != DB_VALUE_TYPE (val_p))
	{
	  /* We have a problem here because type checking might not have 
	   * coerced val to the type of the column. If this is the case, we
	   * have to do it here
	   */
	  if (tp_value_cast (val_p, &val, col_domain, false)
	      == DOMAIN_INCOMPATIBLE)
	    {
	      /* We cannot set an error here because this is not considered
	       * to be an error when scanning regular tables either. We
	       * can consider this predicate to be always false so status to
	       * MATCH_OK to simulate the case when no appropriate partition
	       * was found.
	       */
	      er_clear ();
	      *status = MATCH_OK;
	      return NULL;
	    }
	}
      else
	{
	  pr_clone_value (val_p, &val);
	}

      idx = mht_get_hash_number (hash_size, &val);
      /* Start from 1 because we're using position 0 for the master class */
      parts = partition_add_node_to_list (pinfo->thread_p, parts,
					  &pinfo->partitions[idx + 1]);
      *status = MATCH_OK;
      break;

    case PO_IN:
      {
	DB_COLLECTION *values = NULL;
	int size = 0, i, idx;
	if (!db_value_type_is_collection (val_p))
	  {
	    *status = MATCH_NOT_FOUND;
	    /* This is an error and it should be handled outside of the
	     * pruning environment
	     */
	    return NULL;
	  }
	values = DB_GET_COLLECTION (val_p);
	size = db_set_size (values);
	if (size < 0)
	  {
	    pinfo->error_code = ER_FAILED;
	    *status = MATCH_NOT_FOUND;
	    return NULL;
	  }
	for (i = 0; i < size; i++)
	  {
	    DB_VALUE col;
	    if (db_set_get (values, i, &col) != NO_ERROR)
	      {
		pinfo->error_code = ER_FAILED;
		*status = MATCH_NOT_FOUND;
		return NULL;
	      }
	    if (TP_DOMAIN_TYPE (col_domain) != DB_VALUE_TYPE (&col))
	      {
		/* A failed coercion is not an error in this case,
		 * we should just skip over it
		 */
		if (tp_value_cast (val_p, &val, col_domain, false)
		    == DOMAIN_INCOMPATIBLE)
		  {
		    er_clear ();
		    continue;
		  }
	      }

	    idx = mht_get_hash_number (hash_size, &col);
	    parts = partition_add_node_to_list (pinfo->thread_p, parts,
						&pinfo->partitions[idx + 1]);
	  }
	*status = MATCH_OK;
	break;
      }

    case PO_IS_NULL:
      /* first partition */
      parts = partition_add_node_to_list (pinfo->thread_p, parts,
					  &pinfo->partitions[1]);
      *status = MATCH_OK;
      break;

    default:
      *status = MATCH_NOT_FOUND;
      break;
    }

  return parts;
}

/*
 * partition_prune_range () - Perform pruning for RANGE type partitions
 * return : partition list
 * pinfo (in)	    : pruning context
 * partitions (in)  : list of partitions on which to perform pruning
 * val	      (in)  : the value to which the partition expression is compared
 * op (in)	    : operator to apply
 * status (in/out)  : match status
 */
static PARTITION_NODE *
partition_prune_range (PRUNING_CONTEXT * pinfo,
		       const PARTITION_NODE * partitions,
		       const DB_VALUE * val, const PRUNING_OP op,
		       MATCH_STATUS * status)
{
  int i = 0, error = NO_ERROR;
  PARTITION_NODE *parts = NULL;
  const PARTITION_NODE *part = NULL;
  DB_VALUE min, max;
  int rmin = DB_UNK, rmax = DB_UNK;
  part = partitions;

  DB_MAKE_NULL (&min);
  DB_MAKE_NULL (&max);

  while (part)
    {
      pr_clear_value (&min);
      pr_clear_value (&max);

      error = db_set_get (part->partition->values, 0, &min);
      if (error != NO_ERROR)
	{
	  pinfo->error_code = error;
	  goto cleanup;
	}

      db_set_get (part->partition->values, 1, &max);
      if (error != NO_ERROR)
	{
	  pinfo->error_code = error;
	  goto cleanup;
	}

      if (DB_IS_NULL (&min))
	{
	  /* MINVALUE */
	  rmin = DB_LT;
	}
      else
	{
	  rmin = tp_value_compare (&min, val, 1, 1);
	}

      if (DB_IS_NULL (&max))
	{
	  /* MAXVALUE */
	  rmax = DB_LT;
	}
      else
	{
	  rmax = tp_value_compare (val, &max, 1, 1);
	}

      *status = MATCH_OK;
      switch (op)
	{
	case PO_EQ:
	  /* Filter is part_expr = value. Find the *only* partition for
	   * which min <= value < max */
	  if ((rmin == DB_EQ || rmin == DB_LT) && rmax == DB_LT)
	    {
	      parts =
		partition_add_node_to_list (pinfo->thread_p, parts,
					    part->partition);
	      if (parts == NULL)
		{
		  pinfo->error_code = ER_FAILED;
		  error = NO_ERROR;
		}
	      /* no need to look any further */
	      goto cleanup;
	    }
	  break;

	case PO_LT:
	  /* Filter is part_expr < value. All partitions for which
	     min < value qualify */
	  if (rmin == DB_LT)
	    {
	      parts =
		partition_add_node_to_list (pinfo->thread_p, parts,
					    part->partition);
	      if (parts == NULL)
		{
		  pinfo->error_code = ER_FAILED;
		  error = ER_FAILED;
		  goto cleanup;
		}
	    }
	  break;

	case PO_LE:
	  /* Filter is part_expr <= value. All partitions for which
	     min <= value qualify */
	  if (rmin == DB_EQ)
	    {
	      /* this is the only partition than can qualify */
	      parts =
		partition_add_node_to_list (pinfo->thread_p, parts,
					    part->partition);
	      if (parts == NULL)
		{
		  pinfo->error_code = ER_FAILED;
		  error = ER_FAILED;
		}
	      goto cleanup;
	    }
	  else if (rmin == DB_LT)
	    {
	      parts =
		partition_add_node_to_list (pinfo->thread_p, parts,
					    part->partition);
	      if (parts == NULL)
		{
		  pinfo->error_code = ER_FAILED;
		  error = ER_FAILED;
		  goto cleanup;
		}
	    }
	  break;

	case PO_GT:
	  /* Filter is part_expr > value. All partitions for which 
	     value < max qualify */
	  if (rmax == DB_LT)
	    {
	      parts =
		partition_add_node_to_list (pinfo->thread_p, parts,
					    part->partition);
	      if (parts == NULL)
		{
		  pinfo->error_code = ER_FAILED;
		  error = ER_FAILED;
		  goto cleanup;
		}
	    }
	  break;

	case PO_GE:
	  /* Filter is part_expr > value. All partitions for which
	     value < max qualify */
	  if (rmax == DB_LT)
	    {
	      /* this is the only partition that can qualify */
	      parts =
		partition_add_node_to_list (pinfo->thread_p, parts,
					    part->partition);
	      if (parts == NULL)
		{
		  pinfo->error_code = ER_FAILED;
		  error = ER_FAILED;
		  goto cleanup;
		}
	    }
	  break;

	case PO_IS_NULL:
	  if (DB_IS_NULL (&min))
	    {
	      parts =
		partition_add_node_to_list (pinfo->thread_p, parts,
					    part->partition);
	      if (parts == NULL)
		{
		  pinfo->error_code = ER_FAILED;
		  error = ER_FAILED;
		  goto cleanup;
		}
	      /* no need to look any further */
	      goto cleanup;
	    }
	  break;

	default:
	  *status = MATCH_NOT_FOUND;
	  goto cleanup;
	  break;
	}
      part = part->next;
    }

cleanup:
  pr_clear_value (&min);
  pr_clear_value (&max);

  if (error != NO_ERROR)
    {
      (void) partition_free_list (pinfo->thread_p, parts);
      parts = NULL;
    }

  return parts;
}

/*
 * partition_prune_db_val () - prune partitions using the given DB_VALUE
 * return : pruned partitions or NULL
 * pinfo (in)	   : pruning context
 * partitions (in) : list of partitions to prune
 * val (in)	   : value to use for pruning
 * op (in)	   : operation to be applied for value
 * status (in/out) : pruning status
 */
static PARTITION_NODE *
partition_prune_db_val (PRUNING_CONTEXT * pinfo,
			const PARTITION_NODE * partitions,
			const DB_VALUE * val, const PRUNING_OP op,
			MATCH_STATUS * status)
{
  PARTITION_NODE *pruned = NULL;

  switch (pinfo->partition_type)
    {
    case DB_PARTITION_HASH:
      pruned = partition_prune_hash (pinfo, partitions, val, op, status);
      break;
    case DB_PARTITION_RANGE:
      pruned = partition_prune_range (pinfo, partitions, val, op, status);
      break;
    case DB_PARTITION_LIST:
      pruned = partition_prune_list (pinfo, partitions, val, op, status);
      break;
    default:
      *status = MATCH_NOT_FOUND;
      break;
    }

  return pruned;
}

/*
 * partition_prune () - perform pruning on the specified partitions list
 * return : partition list
 * pinfo (in)	    : pruning context
 * partitions (in)  : list of partitions on which to perform pruning
 * val	      (in)  : the value to which the partition expression is compared
 * op (in)	    : operator to apply
 * status (in/out)  : match status
 */
static PARTITION_NODE *
partition_prune (PRUNING_CONTEXT * pinfo, const PARTITION_NODE * partitions,
		 const REGU_VARIABLE * arg, const PRUNING_OP op,
		 MATCH_STATUS * status)
{
  PARTITION_NODE *pruned = NULL;
  DB_VALUE val;
  bool is_value = false;

  if (arg == NULL && op != PO_IS_NULL)
    {
      *status = MATCH_NOT_FOUND;
      return NULL;
    }

  if (op == PO_IS_NULL)
    {
      DB_MAKE_NULL (&val);
      is_value = true;
    }
  else if (partition_get_value_from_regu_var (pinfo, arg, &val, &is_value)
	   != NO_ERROR)
    {
      /* pruning failed */
      pinfo->error_code = ER_FAILED;
      return NULL;
    }

  if (!is_value)
    {
      /* cannot perform pruning */
      *status = MATCH_NOT_FOUND;
      return NULL;
    }

  pruned = partition_prune_db_val (pinfo, partitions, &val, op, status);

  pr_clear_value (&val);
  return pruned;
}

/*
 * partition_get_value_from_regu_var () - get a DB_VALUE from a REGU_VARIABLE
 * return : error code or NO_ERROR
 * pinfo (in)	    : pruning context
 * regu (in)	    : regu variable
 * value_p (in/out) : holder for the value of regu
 * is_value (in/out): true if the conversion was successful 
 */
static int
partition_get_value_from_regu_var (PRUNING_CONTEXT * pinfo,
				   const REGU_VARIABLE * regu,
				   DB_VALUE * value_p, bool * is_value)
{
  /* we cannot use fetch_peek_dbval here because we're not inside a scan
   * yet.
   */
  if (regu == NULL)
    {
      assert (false);
      goto error;
    }

  switch (regu->type)
    {
    case TYPE_DBVAL:
      if (pr_clone_value (&regu->value.dbval, value_p) != NO_ERROR)
	{
	  goto error;
	}
      *is_value = true;
      break;

    case TYPE_POS_VALUE:
      {
	DB_VALUE *arg_val =
	  (DB_VALUE *) pinfo->vd->dbval_ptr + regu->value.val_pos;
	if (pr_clone_value (arg_val, value_p) != NO_ERROR)
	  {
	    goto error;
	  }
	*is_value = true;
	break;
      }

    case TYPE_FUNC:
      {
	if (regu->value.funcp->ftype != F_MIDXKEY)
	  {
	    *is_value = false;
	    DB_MAKE_NULL (value_p);
	    return NO_ERROR;
	  }
	if (partition_get_value_from_key (pinfo, regu, value_p, is_value) !=
	    NO_ERROR)
	  {
	    goto error;
	  }
	break;
      }

    case TYPE_INARITH:
      /* We can't evaluate the general form of TYPE_INARITH but we can
       * handle "pseudo constants" (SYS_DATE, SYS_TIME, etc) and the CAST
       * operator applied to constants. Eventually, it would be great if we
       * could evaluate all pseudo constant here (like cast ('1' as int) + 1)
       */
      if (partition_get_value_from_inarith (pinfo, regu, value_p, is_value) !=
	  NO_ERROR)
	{
	  goto error;
	}
      break;

    default:
      DB_MAKE_NULL (value_p);
      *is_value = false;
      return NO_ERROR;
    }
  return NO_ERROR;

error:
  DB_MAKE_NULL (value_p);
  *is_value = false;
  return ER_FAILED;
}


/*
 * partition_get_value_from_key () - get a value from an index key
 * return : error code or NO_ERROR
 * pinfo (in)		: pruning context
 * key (in)		: index key
 * attr_key (in/out)	: the requested value
 * is_present (in/out)	: set to true if the value was successfully fetched
 */
static int
partition_get_value_from_key (PRUNING_CONTEXT * pinfo,
			      const REGU_VARIABLE * key, DB_VALUE * attr_key,
			      bool * is_present)
{
  int error = NO_ERROR;
  assert (attr_key != NULL);

  switch (key->type)
    {
    case TYPE_DBVAL:
      pr_clone_value (&key->value.dbval, attr_key);
      *is_present = true;
      break;

    case TYPE_POS_VALUE:
      {
	/* use val_pos for host variable references */
	DB_VALUE *val =
	  (DB_VALUE *) pinfo->vd->dbval_ptr + key->value.val_pos;
	error = pr_clone_value (val, attr_key);
	*is_present = true;
	break;
      }

    case TYPE_FUNC:
      {
	/* this is a F_MIDXKEY function and the value we're interested
	   in is in the operands */
	REGU_VARIABLE_LIST regu_list = NULL;
	int i;
	if (key->value.funcp->ftype != F_MIDXKEY)
	  {
	    assert (false);
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	    return ER_FAILED;
	  }
	/* loop through arguments of the function and get the MIDX_KEY */
	regu_list = key->value.funcp->operand;
	for (i = 0; i < pinfo->attr_position; i++)
	  {
	    if (regu_list == NULL)
	      {
		/* partition key not referenced in range */
		break;
	      }
	    regu_list = regu_list->next;
	  }

	if (regu_list == NULL)
	  {
	    DB_MAKE_NULL (attr_key);
	    error = NO_ERROR;
	    *is_present = false;
	  }
	else
	  {
	    error = partition_get_value_from_key (pinfo, &regu_list->value,
						  attr_key, is_present);
	  }
	break;
      }

    default:
      assert (false);
      DB_MAKE_NULL (attr_key);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      error = ER_FAILED;
      *is_present = false;
      break;
    }

  return error;
}

/*
 * partition_get_value_from_key () - get a value from a reguvariable of type
 *				     INARITH
 * return : error code or NO_ERROR
 * pinfo (in)		: pruning context
 * src (in)		: source reguvariable
 * value_p (in/out)	: the requested value
 * is_value (in/out)	: set to true if the value was successfully fetched
 *
 */
static int
partition_get_value_from_inarith (PRUNING_CONTEXT * pinfo,
				  const REGU_VARIABLE * src,
				  DB_VALUE * value_p, bool * is_value)
{
  int error = NO_ERROR;
  DB_VALUE *val_backup = NULL, *peek_val = NULL;
  ARITH_TYPE *arithptr = NULL;

  assert_release (src != NULL && value_p != NULL);
  assert_release (src->type == TYPE_INARITH);

  DB_MAKE_NULL (value_p);
  arithptr = src->value.arithptr;
  switch (arithptr->opcode)
    {
    case T_SYS_DATE:
    case T_SYS_TIME:
    case T_SYS_TIMESTAMP:
    case T_UTC_TIME:
    case T_UTC_DATE:
      /* backup arithptr value */
      val_backup = arithptr->value;
      arithptr->value = value_p;
      error =
	fetch_peek_dbval (pinfo->thread_p, (REGU_VARIABLE *) src, pinfo->vd,
			  NULL, NULL, NULL, &peek_val);
      if (error != NO_ERROR)
	{
	  *is_value = false;
	  DB_MAKE_NULL (value_p);
	}
      else
	{
	  *is_value = true;
	}
      /* restore arithptr */
      arithptr->value = val_backup;
      break;

    case T_CAST:
    case T_CAST_NOFAIL:
      {
	break;
      }

    default:
      DB_MAKE_NULL (value_p);
      *is_value = false;
      break;
    }

  return error;
}

/*
 * partition_match_pred_expr () - get partitions matching a predicate
 *				  expression
 * return : partition list
 * pinfo (in)	    : pruning context
 * partitions (in)  : partitions
 * pr (in)	    : predicate expression
 * status (in/out)  : matching status
 */
static PARTITION_NODE *
partition_match_pred_expr (PRUNING_CONTEXT * pinfo,
			   PARTITION_NODE * partitions, const PRED_EXPR * pr,
			   MATCH_STATUS * status)
{
  PARTITION_NODE *parts = NULL;
  MATCH_STATUS lstatus, rstatus;
  REGU_VARIABLE *part_expr = pinfo->partition_pred->func_regu;

  if (pr == NULL || status == NULL)
    {
      assert (false);
      return NULL;
    }

  switch (pr->type)
    {
    case T_PRED:
      {
	/* T_PRED contains conjunctions and disjunctions of PRED_EXPR.
	 * Get partitions matching the left PRED_EXPR and partitions matching
	 * the right PRED_EXPR and merge or intersect the lists */
	PARTITION_NODE *lleft = NULL, *lright = NULL;
	lleft =
	  partition_match_pred_expr (pinfo, partitions, pr->pe.pred.lhs,
				     &lstatus);
	lright =
	  partition_match_pred_expr (pinfo, partitions, pr->pe.pred.rhs,
				     &rstatus);
	if (pr->pe.pred.bool_op == B_AND)
	  {
	    /* do intersection between left and right */
	    if (lstatus == MATCH_NOT_FOUND)
	      {
		/* pr->pe.pred.lhs does not refer part_expr so return right */
		parts = lright;
		*status = rstatus;
	      }
	    else if (rstatus == MATCH_NOT_FOUND)
	      {
		/* pr->pe.pred.rhs does not refer part_expr so return right */
		parts = lleft;
		*status = lstatus;
	      }
	    else
	      {
		*status = MATCH_OK;
		parts =
		  partition_intersect_lists (pinfo->thread_p, lleft, lright);
		lleft = NULL;
		lright = NULL;
	      }
	  }
	else if (pr->pe.pred.bool_op == B_OR)
	  {
	    if (lstatus == MATCH_NOT_FOUND || rstatus == MATCH_NOT_FOUND)
	      {
		*status = MATCH_NOT_FOUND;
		(void) partition_free_list (pinfo->thread_p, lleft);
		(void) partition_free_list (pinfo->thread_p, lright);
	      }
	    else
	      {
		*status = MATCH_OK;
		parts =
		  partition_merge_lists (pinfo->thread_p, lleft, lright);
		lleft = NULL;
		lright = NULL;
	      }
	  }
	break;
      }

    case T_EVAL_TERM:
      switch (pr->pe.eval_term.et_type)
	{
	case T_COMP_EVAL_TERM:
	  {
	    /* see if part_expr matches right or left */
	    REGU_VARIABLE *left = pr->pe.eval_term.et.et_comp.lhs;
	    REGU_VARIABLE *right = pr->pe.eval_term.et.et_comp.rhs;
	    PRUNING_OP op =
	      partition_rel_op_to_pruning_op (pr->pe.eval_term.et.et_comp.
					      rel_op);
	    *status = MATCH_NOT_FOUND;
	    if (partition_do_regu_variables_match (pinfo, left, part_expr))
	      {
		parts =
		  partition_prune (pinfo, partitions, right, op, status);
	      }
	    else
	      if (partition_do_regu_variables_match (pinfo, right, part_expr))
	      {
		parts = partition_prune (pinfo, partitions, left, op, status);
	      }
	    break;
	  }

	case T_ALSM_EVAL_TERM:
	  {
	    REGU_VARIABLE *regu = pr->pe.eval_term.et.et_alsm.elem;
	    REGU_VARIABLE *list = pr->pe.eval_term.et.et_alsm.elemset;
	    PRUNING_OP op =
	      partition_rel_op_to_pruning_op (pr->pe.eval_term.et.et_alsm.
					      rel_op);
	    /* adjust rel_op based on the QL_FLAG of the alsm eval node */
	    if (pr->pe.eval_term.et.et_alsm.eq_flag == F_SOME)
	      {
		if (op == PO_EQ)
		  {
		    op = PO_IN;
		  }
		else if (op == PO_NE)
		  {
		    op = PO_NOT_IN;
		  }
	      }
	    if (partition_do_regu_variables_match (pinfo, regu, part_expr))
	      {
		parts = partition_prune (pinfo, partitions, list, op, status);
	      }
	  }
	  break;

	case T_LIKE_EVAL_TERM:
	case T_RLIKE_EVAL_TERM:
	  /* Don't know how to work with LIKE/RLIKE expressions yet. There are
	   * some cases in which we can deduce a range from the like pattern.
	   */
	  *status = MATCH_NOT_FOUND;
	  break;
	}
      break;

    case T_NOT_TERM:
      *status = MATCH_NOT_FOUND;
      break;
    }

  return parts;
}

/*
 * partition_match_key_range () - perform pruning using a key_range
 * return : pruned list
 * pinfo (in)	   : pruning context
 * partitions (in) : partitions to prune
 * key_range (in)  : key range
 * status (in/out) : pruning status
 */
static PARTITION_NODE *
partition_match_key_range (PRUNING_CONTEXT * pinfo,
			   const PARTITION_NODE * partitions,
			   const KEY_RANGE * key_range, MATCH_STATUS * status)
{
  PARTITION_NODE *pruned = NULL, *left = NULL, *right = NULL;
  PRUNING_OP lop, rop;
  MATCH_STATUS lstatus, rstatus;

  switch (key_range->range)
    {
    case NA_NA:
      /* v1 and v2 are N/A, so that no range is defined */
      *status = MATCH_NOT_FOUND;
      pruned = NULL;
      break;

    case GE_LE:
      /* v1 <= key <= v2 */
      lop = PO_GE;
      rop = PO_LE;
      break;

    case GE_LT:
      /* v1 <= key < v2 */
      lop = PO_GE;
      rop = PO_LT;
      break;

    case GT_LE:
      /* v1 < key <= v2 */
      lop = PO_GT;
      rop = PO_LE;
      break;

    case GT_LT:
      /* v1 < key < v2 */
      lop = PO_GT;
      rop = PO_LT;
      break;

    case GE_INF:
      /* v1 <= key (<= the end) */
      lop = PO_GE;
      rop = PO_INVALID;
      break;

    case GT_INF:
      /* v1 < key (<= the end) */
      lop = PO_GT;
      rop = PO_INVALID;
      break;

    case INF_LE:
      /* (the beginning <=) key <= v2 */
      lop = PO_INVALID;
      rop = PO_LE;
      break;

    case INF_LT:
      /* (the beginning <=) key < v2 */
      lop = PO_INVALID;
      rop = PO_LT;
      break;

    case INF_INF:
      /* the beginning <= key <= the end */
      lop = PO_INVALID;
      rop = PO_INVALID;
      break;

    case EQ_NA:
      /* key = v1, v2 is N/A */
      lop = PO_EQ;
      rop = PO_INVALID;
      break;

    case LE_GE:
    case LE_GT:
    case LT_GE:
    case LT_GT:
    case NEQ_NA:
      lop = PO_INVALID;
      rop = PO_INVALID;
      break;
    }

  /* prune left */
  if (lop == PO_INVALID)
    {
      lstatus = MATCH_NOT_FOUND;
      left = NULL;
    }
  else
    {
      left =
	partition_prune (pinfo, partitions, key_range->key1, lop, &lstatus);
      if (pinfo->error_code != NO_ERROR)
	{
	  assert (left == NULL);
	  return NULL;
	}
    }

  /* prune right */
  if (rop == PO_INVALID)
    {
      rstatus = MATCH_NOT_FOUND;
      right = NULL;
    }
  else
    {
      right =
	partition_prune (pinfo, partitions, key_range->key2, rop, &rstatus);
      if (pinfo->error_code != NO_ERROR)
	{
	  assert (right == NULL);
	  (void) partition_free_list (pinfo->thread_p, left);
	  return NULL;
	}
    }

  if (lstatus == MATCH_NOT_FOUND)
    {
      *status = rstatus;
      return right;
    }
  if (rstatus == MATCH_NOT_FOUND)
    {
      *status = lstatus;
      return left;
    }

  *status = MATCH_OK;
  pruned = partition_intersect_lists (pinfo->thread_p, left, right);

  return pruned;
}

/*
 * partition_match_index_key () - get the list of partitions that fit into the
 *				  index key
 * return : partition list
 * pinfo (in)	    : pruning context
 * partitions (in)  : partitions to search
 * key (in)	    : index key info
 * range_type (in)  : range type
 * status (in/out)  : match status
 *
 */
static PARTITION_NODE *
partition_match_index (PRUNING_CONTEXT * pinfo, PARTITION_NODE * partitions,
		       const KEY_INFO * key, RANGE_TYPE range_type,
		       MATCH_STATUS * status)
{
  int error = NO_ERROR, i;
  int ptype = partitions->partition->partition_type;
  PARTITION_NODE *pruned = NULL;

  if (pinfo->partition_pred->func_regu->type != TYPE_ATTR_ID)
    {
      *status = MATCH_NOT_FOUND;
      return NULL;
    }

  /* We do not care which range_type this index scan is supposed to 
   * perform. Each key range produces a list of partitions and we will
   * merge those lists to get the full list of partitions that contains
   * information for our search
   */
  for (i = 0; i < key->key_cnt; i++)
    {
      PARTITION_NODE *key_parts =
	partition_match_key_range (pinfo, partitions, &key->key_ranges[i],
				   status);
      if (*status == MATCH_NOT_FOUND)
	{
	  /* For key ranges we have to find a match for all ranges.
	   * If we get a MATCH_NOT_FOUND then we have to assume that all
	   * partitions have to be scanned for the result
	   */
	  (void) partition_free_list (pinfo->thread_p, pruned);
	  pruned = NULL;
	  break;
	}
      pruned = partition_merge_lists (pinfo->thread_p, pruned, key_parts);
      key_parts = NULL;
    }

  return pruned;
}

/*
 * partition_init_pruning_context () - initialize pruning context
 * return : void
 * pinfo (in/out)  : pruning context
 */
static void
partition_init_pruning_context (PRUNING_CONTEXT * pinfo)
{
  if (pinfo == NULL)
    {
      assert (false);
      return;
    }

  pinfo->thread_p = NULL;
  pinfo->partitions = NULL;
  pinfo->spec = NULL;
  pinfo->vd = NULL;
  pinfo->count = 0;
  pinfo->fp_cache_context = NULL;
  pinfo->partition_pred = NULL;
  pinfo->attr_position = -1;
  pinfo->error_code = NO_ERROR;
  OID_SET_NULL (&pinfo->root_oid);
}

/*
 * partition_load_pruning_context () - load pruning context
 * return : error code or NO_ERROR
 * thread_p (in)  :
 * spec (in)	  : access specification for which to perform pruning
 * vd (in)	  : value descriptor
 * pinfo (in)	  : pruning context
 */
static int
partition_load_pruning_context (THREAD_ENTRY * thread_p,
				const OID * class_oid,
				PRUNING_CONTEXT * pinfo)
{
  int error = NO_ERROR;
  PARTITION_NODE *parts = NULL;
  OR_PARTITION *master = NULL;
  MATCH_STATUS status = MATCH_NOT_FOUND;
  bool is_modified = false;

  if (pinfo == NULL)
    {
      assert (false);
      return ER_FAILED;
    }

  (void) partition_init_pruning_context (pinfo);

  pinfo->thread_p = thread_p;
  COPY_OID (&pinfo->root_oid, class_oid);

  /* try to get info from the cache first */
  if (!partition_load_context_from_cache (pinfo, &is_modified))
    {
      if (pinfo->error_code != NO_ERROR)
	{
	  return pinfo->error_code;
	}
    }
  else
    {
      master = &pinfo->partitions[0];
      /* load the partition predicate which is not deserialized in the
       * cache
       */
      error = partition_load_partition_predicate (pinfo, master);
      if (error != NO_ERROR)
	{
	  /* cleanup and return error */
	  db_private_free (thread_p, pinfo->partitions);
	  return error;
	}
      return NO_ERROR;
    }

  error =
    heap_get_class_partitions (pinfo->thread_p, class_oid, &pinfo->partitions,
			       &pinfo->count);
  if (error != NO_ERROR)
    {
      return error;
    }
  if (pinfo->partitions == NULL)
    {
      /* Class is not partitioned, just return */
      return NO_ERROR;
    }

  master = &pinfo->partitions[0];
  error = partition_load_partition_predicate (pinfo, master);
  if (error != NO_ERROR)
    {
      /* cleanup and return error */
      db_private_free (thread_p, pinfo->partitions);
      return error;
    }

  pinfo->partition_type = master->partition_type;

  pinfo->attr_id =
    partition_get_attribute_id (pinfo->partition_pred->func_regu);

  if (!is_modified)
    {
      /* cache the loaded info */
      partition_cache_pruning_context (pinfo);
    }
  return NO_ERROR;
}

/*
 * partition_clear_pruning_context () - free memory allocated for pruning
 *					context
 * return : void
 * pinfo (in) : pruning context
 */
static void
partition_clear_pruning_context (PRUNING_CONTEXT * pinfo)
{
  if (pinfo == NULL)
    {
      assert (false);
      return;
    }

  if (pinfo->partitions != NULL)
    {
      int i;

      for (i = 0; i < pinfo->count; i++)
	{
	  if (pinfo->partitions[i].values != NULL)
	    {
	      db_seq_free (pinfo->partitions[i].values);
	    }
	}
      db_private_free (pinfo->thread_p, pinfo->partitions);
    }

  if (pinfo->partition_pred != NULL
      && pinfo->partition_pred->func_regu != NULL)
    {
      (void) qexec_clear_partition_expression (pinfo->thread_p,
					       pinfo->partition_pred->
					       func_regu);
    }
  if (pinfo->fp_cache_context != NULL)
    {
      db_private_free (pinfo->thread_p, pinfo->fp_cache_context);
    }
}

/*
 * partition_load_partition_predicate () - load partition predicate
 * return : 
 * pinfo (in)	: pruning context
 * master (in)	: master partition information
 */
static int
partition_load_partition_predicate (PRUNING_CONTEXT * pinfo,
				    OR_PARTITION * master)
{
  int error = NO_ERROR, stream_len = 0;
  DB_VALUE val;
  char *expr_stream = NULL;

  if (db_set_size (master->values) < 3)
    {
      /* internal storage error */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return ER_FAILED;
    }

  /* get expr stream */
  error = db_set_get (master->values, 2, &val);
  if (error != NO_ERROR)
    {
      return ER_FAILED;
    }

  assert (DB_VALUE_TYPE (&val) == DB_TYPE_CHAR);
  expr_stream = DB_PULL_CHAR (&val, &stream_len);

  /* unpack partition expression */
  error =
    stx_map_stream_to_func_pred (pinfo->thread_p, &pinfo->partition_pred,
				 expr_stream, stream_len,
				 &pinfo->fp_cache_context);
  pr_clear_value (&val);
  return error;
}

/*
 * partition_set_cache_info_for_expr () - set attribute info cache for the
 *					  partition expression
 * return : true if attribute cache info was set, false otherwise
 * var (in/out) : partition expression
 * attr_id (in) : partition attribute id
 * attr_info (in) : attribute cache info
 */
static void
partition_set_cache_info_for_expr (REGU_VARIABLE * var, ATTR_ID attr_id,
				   HEAP_CACHE_ATTRINFO * attr_info)
{
  assert (attr_info != NULL);

  if (var == NULL)
    {
      /* nothing to do */
      return;
    }

  if (attr_id == NULL_ATTRID)
    {
      /* nothing to do */
      return;
    }

  switch (var->type)
    {
    case TYPE_ATTR_ID:
      if (var->value.attr_descr.id == attr_id)
	{
	  var->value.attr_descr.cache_attrinfo = attr_info;
	}
      break;

    case TYPE_INARITH:
    case TYPE_OUTARITH:
      (void) partition_set_cache_info_for_expr (var->value.arithptr->leftptr,
						attr_id, attr_info);
      (void) partition_set_cache_info_for_expr (var->value.arithptr->rightptr,
						attr_id, attr_info);
      (void) partition_set_cache_info_for_expr (var->value.arithptr->thirdptr,
						attr_id, attr_info);
      break;

    case TYPE_AGGREGATE:
      (void) partition_set_cache_info_for_expr (&var->value.aggptr->operand,
						attr_id, attr_info);
      break;

    case TYPE_FUNC:
      {
	REGU_VARIABLE_LIST op = var->value.funcp->operand;
	while (op != NULL)
	  {
	    (void) partition_set_cache_info_for_expr (&op->value, attr_id,
						      attr_info);
	    op = op->next;
	  }
	break;
      }

    default:
      break;
    }
}

/*
 * partition_get_attribute_id () - get the id of the attribute of the
 *				   partition expression
 * return : attribute id
 * var (in) : partition expression
 */
static ATTR_ID
partition_get_attribute_id (REGU_VARIABLE * var)
{
  ATTR_ID attr_id = NULL_ATTRID;

  assert (var != NULL);
  switch (var->type)
    {
    case TYPE_ATTR_ID:
      return var->value.attr_descr.id;

    case TYPE_INARITH:
    case TYPE_OUTARITH:
      if (var->value.arithptr->leftptr != NULL)
	{
	  attr_id = partition_get_attribute_id (var->value.arithptr->leftptr);
	  if (attr_id != NULL_ATTRID)
	    {
	      return attr_id;
	    }
	}

      if (var->value.arithptr->rightptr != NULL)
	{
	  attr_id =
	    partition_get_attribute_id (var->value.arithptr->rightptr);
	  if (attr_id != NULL_ATTRID)
	    {
	      return attr_id;
	    }
	}

      if (var->value.arithptr->thirdptr != NULL)
	{
	  attr_id =
	    partition_get_attribute_id (var->value.arithptr->thirdptr);
	  if (attr_id != NULL_ATTRID)
	    {
	      return attr_id;
	    }
	}
      return attr_id;

    case TYPE_AGGREGATE:
      return partition_get_attribute_id (&var->value.aggptr->operand);

    case TYPE_FUNC:
      {
	REGU_VARIABLE_LIST op = var->value.funcp->operand;
	while (op != NULL)
	  {
	    attr_id = partition_get_attribute_id (&op->value);
	    if (attr_id != NULL_ATTRID)
	      {
		return attr_id;
	      }
	    op = op->next;
	  }
	return attr_id;
      }

    default:
      return NULL_ATTRID;
    }

  return attr_id;
}

/*
 * partition_get_position_in_key () - get the position of the partition column
 *				      in a multicolumn key
 * return : error code or NO_ERROR
 * pinfo (in) : pruning context
 */
static int
partition_get_position_in_key (PRUNING_CONTEXT * pinfo)
{
  int i = 0;
  int error = NO_ERROR;
  ATTR_ID part_attr_id = -1;
  ATTR_ID *keys = NULL;
  int key_count = 0;
  OID *class_oid;
  BTID *btid;

  if (pinfo->spec->access != INDEX)
    {
      pinfo->attr_position = -1;
      return NO_ERROR;
    }

  if (pinfo->partition_pred->func_regu->type != TYPE_ATTR_ID)
    {
      /* In the case of index keys, we will only apply pruning if the
       * partition expression is actually an attribute. This is because we
       * will not have expressions in the index key, only attributes
       * (except for function and filter indexes which are not handled yet)
       */
      pinfo->attr_position = -1;
      return NO_ERROR;
    }

  assert (pinfo->spec->indexptr->indx_id.type == T_BTID);

  class_oid = &ACCESS_SPEC_CLS_OID (pinfo->spec);
  btid = &pinfo->spec->indexptr->indx_id.i.btid;
  part_attr_id = pinfo->partition_pred->func_regu->value.attr_descr.id;

  error = heap_get_indexinfo_of_btid (pinfo->thread_p, class_oid, btid, NULL,
				      &key_count, &keys, NULL, NULL);
  if (error != NO_ERROR)
    {
      return error;
    }

  for (i = 0; i < key_count; i++)
    {
      if (part_attr_id == keys[i])
	{
	  /* found the attribute */
	  pinfo->attr_position = i;
	  break;
	}
    }

  if (keys != NULL)
    {
      db_private_free (pinfo->thread_p, keys);
    }

  return NO_ERROR;
}

/*
 * partition_prune_heap_scan () - prune a access spec for heap scan
 * return : error code or NO_ERROR
 * pinfo (in) : pruning context
 */
static int
partition_prune_heap_scan (PRUNING_CONTEXT * pinfo)
{
  int error = NO_ERROR;
  PARTITION_NODE *parts = NULL, *pruned = NULL;
  MATCH_STATUS status = MATCH_NOT_FOUND;

  assert (pinfo != NULL);
  assert (pinfo->partitions != NULL);

  parts = partition_build_list (pinfo->thread_p, pinfo->partitions + 1,
				pinfo->count - 1);
  if (parts == NULL)
    {
      error = ER_FAILED;
      goto cleanup;
    }

  if (pinfo->spec->where_pred == NULL)
    {
      pruned = NULL;
      status = MATCH_NOT_FOUND;
    }
  else
    {
      pruned =
	partition_match_pred_expr (pinfo, parts, pinfo->spec->where_pred,
				   &status);
      if (pinfo->error_code != NO_ERROR)
	{
	  error = pinfo->error_code;
	  goto cleanup;
	}
    }

  if (status != MATCH_NOT_FOUND)
    {
      partition_list_to_spec_list (pinfo, pruned);
    }
  else
    {
      /* consider all partitions */
      partition_list_to_spec_list (pinfo, parts);
    }

cleanup:
  if (parts != NULL)
    {
      (void) partition_free_list (pinfo->thread_p, parts);
    }
  if (pruned != NULL)
    {
      (void) partition_free_list (pinfo->thread_p, pruned);
    }
  return error;
}

/*
 * partition_prune_index_scan () - perform partition pruning on an index scan
 * return : error code or NO_ERROR
 * pinfo (in) : pruning context
 */
static int
partition_prune_index_scan (PRUNING_CONTEXT * pinfo)
{
  int error = NO_ERROR;
  PARTITION_NODE *parts = NULL;
  MATCH_STATUS status = MATCH_NOT_FOUND;

  assert (pinfo != NULL);
  assert (pinfo->partitions != NULL);

  parts = partition_build_list (pinfo->thread_p, pinfo->partitions + 1,
				pinfo->count - 1);
  if (parts == NULL)
    {
      error = ER_FAILED;
      goto cleanup;
    }

  if (pinfo->spec->where_pred != NULL)
    {
      PARTITION_NODE *pruned = NULL;

      pruned =
	partition_match_pred_expr (pinfo, parts, pinfo->spec->where_pred,
				   &status);
      if (status == MATCH_NOT_FOUND)
	{
	  assert (pruned == NULL);
	}
      else
	{
	  (void) partition_free_list (pinfo->thread_p, parts);
	  parts = pruned;
	}
    }

  if (pinfo->spec->where_key != NULL)
    {
      PARTITION_NODE *pruned = NULL;

      pruned =
	partition_match_pred_expr (pinfo, parts, pinfo->spec->where_key,
				   &status);
      if (status == MATCH_NOT_FOUND)
	{
	  assert (pruned == NULL);
	}
      else
	{
	  (void) partition_free_list (pinfo->thread_p, parts);
	  parts = pruned;
	}
    }

  if (pinfo->attr_position != -1)
    {
      PARTITION_NODE *pruned = NULL;

      pruned =
	partition_match_index (pinfo, parts,
			       &pinfo->spec->indexptr->key_info,
			       pinfo->spec->indexptr->range_type, &status);
      if (status == MATCH_NOT_FOUND)
	{
	  assert (pruned == NULL);
	}
      else
	{
	  (void) partition_free_list (pinfo->thread_p, parts);
	  parts = pruned;
	}
    }

  partition_list_to_spec_list (pinfo, parts);

cleanup:
  if (parts != NULL)
    {
      (void) partition_free_list (pinfo->thread_p, parts);
    }

  return error;
}

/*
 * partition_prune_spec () - perform pruning on an access spec.
 * return : error code or NO_ERROR
 * thread_p (in)    :
 * access_spec (in) : access spec to prune
 */
int
partition_prune_spec (THREAD_ENTRY * thread_p, VAL_DESCR * vd,
		      ACCESS_SPEC_TYPE * spec)
{
  int error = NO_ERROR;
  PRUNING_CONTEXT pinfo;

  if (spec == NULL)
    {
      assert (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return ER_FAILED;
    }

  if (spec->type != TARGET_CLASS && spec->type != TARGET_CLASS_ATTR)
    {
      /* nothing to prune */
      return NO_ERROR;
    }

  (void) partition_init_pruning_context (&pinfo);

  spec->curent = NULL;
  spec->parts = NULL;

  error =
    partition_load_pruning_context (thread_p, &ACCESS_SPEC_CLS_OID (spec),
				    &pinfo);
  if (error != NO_ERROR)
    {
      return error;
    }

  if (pinfo.partitions == NULL)
    {
      /* no partitions */
      spec->pruned = true;
      return NO_ERROR;
    }

  pinfo.spec = spec;
  pinfo.vd = vd;
  partition_get_position_in_key (&pinfo);

  if (spec->access == SEQUENTIAL)
    {

      error = partition_prune_heap_scan (&pinfo);
    }
  else
    {
      error = partition_prune_index_scan (&pinfo);
    }

  (void) partition_clear_pruning_context (&pinfo);
  spec->pruned = true;

  return error;
}

/*
 * partition_find_partition_for_record () - find the partition in which a
 *					    record should be placed
 * return : error code or NO_ERROR
 * pinfo (in)	  : pruning context
 * class_oid (in) : OID of the root class
 * recdes (in)	  : record descriptor
 * partition_oid (in/out) : OID of the partition in which the record fits
 * partition_hfid (in/out): HFID of the partition in which the record fits
 */
static int
partition_find_partition_for_record (PRUNING_CONTEXT * pinfo,
				     const OID * class_oid, RECDES * recdes,
				     OID * partition_oid,
				     HFID * partition_hfid)
{
  PARTITION_NODE *parts = NULL, *pruned = NULL;
  HEAP_CACHE_ATTRINFO attr_info;
  bool clear_attrinfo = false, clear_dbvalues = false;
  DB_VALUE *result = NULL;
  MATCH_STATUS status = MATCH_NOT_FOUND;
  int error = NO_ERROR;
  PRUNING_OP op = PO_EQ;

  assert (partition_oid != NULL);
  assert (partition_hfid != NULL);

  parts =
    partition_build_list (pinfo->thread_p, pinfo->partitions + 1,
			  pinfo->count - 1);
  if (parts == NULL)
    {
      error = ER_FAILED;
      goto cleanup;
    }

  error =
    heap_attrinfo_start (pinfo->thread_p, class_oid, 1, &pinfo->attr_id,
			 &attr_info);
  if (error != NO_ERROR)
    {
      goto cleanup;
    }

  clear_attrinfo = true;

  error =
    heap_attrinfo_read_dbvalues (pinfo->thread_p, &attr_info.inst_oid,
				 recdes, &attr_info);
  if (error != NO_ERROR)
    {
      goto cleanup;
    }
  clear_dbvalues = true;

  (void) partition_set_cache_info_for_expr (pinfo->partition_pred->func_regu,
					    pinfo->attr_id, &attr_info);

  error =
    fetch_peek_dbval (pinfo->thread_p, pinfo->partition_pred->func_regu, NULL,
		      (OID *) class_oid, &attr_info.inst_oid, NULL, &result);
  if (error != NO_ERROR)
    {
      goto cleanup;
    }

  assert (result != NULL);

  if (db_value_is_null (result))
    {
      /* use IS_NULL comparison when pruning NULL DB_VALUEs */
      op = PO_IS_NULL;
    }

  pruned = partition_prune_db_val (pinfo, parts, result, op, &status);

  if (pruned == NULL)
    {
      /* At this stage we should absolutely have a result. If we don't then
       * something went wrong (either some internal error (e.g. allocation
       * failed) or the inserted value cannot be placed in any partition
       */
      if (pinfo->error_code == NO_ERROR)
	{
	  /* no appropriate partition found */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_PARTITION_NOT_EXIST,
		  0);
	  error = ER_PARTITION_NOT_EXIST;
	}
      else
	{
	  /* This is an internal *error (allocation, etc). Error was set by
	   * the calls above, just set *error code */
	  error = pinfo->error_code;
	}
      goto cleanup;
    }

  if (status != MATCH_OK)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_PARTITION_NOT_EXIST, 0);
      error = ER_PARTITION_NOT_EXIST;
      goto cleanup;
    }

  if (pruned->next != NULL)
    {
      /* this should never happen, a tuple cannot belong to more than one
       * partition
       */
      assert (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_PARTITION_NOT_EXIST, 0);
      error = ER_PARTITION_NOT_EXIST;
      goto cleanup;
    }

  COPY_OID (partition_oid, &pruned->partition->class_oid);
  HFID_COPY (partition_hfid, &pruned->partition->class_hfid);

  if (!OID_EQ (class_oid, partition_oid))
    {
      /* Update representation id of the record to that of the pruned
       * partition. For any other operation than pruning, the new
       * representation id should be obtained by constructing a new 
       * HEAP_ATTRIBUTE_INFO structure for the new class, copying values
       * from this record to that structure and then transforming it to disk.
       * Since we're working with partitioned tables, we can guarantee that,
       * except for the actual representation id bits, the new record will be
       * exactly the same. Because of this, we can take a shortcut here and
       * only update the bits from the representation id
       */
      REPR_ID newrep_id = NULL_REPRID, oldrep_id = NULL_REPRID;
      newrep_id = heap_get_class_repr_id (pinfo->thread_p, partition_oid);
      if (newrep_id <= 0)
	{
	  error = ER_FAILED;
	  goto cleanup;
	}
      oldrep_id = or_rep_id (recdes);
      if (newrep_id != oldrep_id)
	{
	  /* set newrep_id */
	  error = or_set_rep_id (recdes, newrep_id);
	}
    }

cleanup:
  if (clear_dbvalues)
    {
      heap_attrinfo_clear_dbvalues (&attr_info);
    }
  if (clear_attrinfo)
    {
      heap_attrinfo_end (pinfo->thread_p, &attr_info);
    }
  if (parts != NULL)
    {
      (void) partition_free_list (pinfo->thread_p, parts);
    }
  if (pruned != NULL)
    {
      (void) partition_free_list (pinfo->thread_p, pruned);
    }
  return error;
}

/*
 * partition_prune_insert () - perform pruning for insert
 * return : error code or NO_ERROR
 * thread_p (in)  : thread entry
 * class_oid (in) : OID of the root class
 * recdes (in)	  : Record describing the new object
 * scan_cache (in): Heap scan cache
 * pruned_class_oid (in/out) : partition to insert into
 * pruned_hfid (in/out)	     : HFID of the partition
 */
int
partition_prune_insert (THREAD_ENTRY * thread_p, const OID * class_oid,
			RECDES * recdes, HEAP_SCANCACHE * scan_cache,
			OID * pruned_class_oid, HFID * pruned_hfid)
{
  PRUNING_CONTEXT pinfo;
  int error = NO_ERROR;

  assert (pruned_class_oid != NULL);
  assert (pruned_hfid != NULL);

  (void) partition_init_pruning_context (&pinfo);

  error = partition_load_pruning_context (thread_p, class_oid, &pinfo);
  if (error != NO_ERROR)
    {
      return error;
    }

  if (pinfo.partitions == NULL)
    {
      /* no partitions, cleanup and exit */
      goto cleanup;
    }

  error =
    partition_find_partition_for_record (&pinfo, class_oid, recdes,
					 pruned_class_oid, pruned_hfid);
  if (error != NO_ERROR)
    {
      goto cleanup;
    }

cleanup:
  (void) partition_clear_pruning_context (&pinfo);
  return error;
}

/*
 * partition_prune_update () - perform pruning on update statements
 * return : error code or NO_ERROR
 * thread_p (in)  : thread entry
 * class_oid (in) : OID of the root class
 * recdes (in)	  : Record describing the new object
 * pruned_class_oid (in/out) : partition to insert into
 * pruned_hfid (in/out)	     : HFID of the partition
 */
int
partition_prune_update (THREAD_ENTRY * thread_p, const OID * class_oid,
			RECDES * recdes, OID * pruned_class_oid,
			HFID * pruned_hfid)
{
  PRUNING_CONTEXT pinfo;
  int error = NO_ERROR;
  int super_count = 0;
  OID *super_class = NULL;

  if (OID_IS_ROOTOID (class_oid))
    {
      /* nothing to do here */
      COPY_OID (pruned_class_oid, class_oid);
      return NO_ERROR;
    }

  (void) partition_init_pruning_context (&pinfo);
  /* Due to the nature of the way in which UPDATE statements are executed,
   * class_oid either points to the root partitioned class or to the actual
   * partition in which the updated record resides. Since it is more probable
   * for class_oid to be a partition, not the root, we check this first
   */
  error =
    heap_get_class_supers (thread_p, class_oid, &super_class, &super_count);
  if (error != NO_ERROR)
    {
      return error;
    }

  if (super_count > 1)
    {
      /* A partition may only have one superclass so this is not a
       * partition */
      if (super_class != NULL)
	{
	  free_and_init (super_class);
	}
      return NO_ERROR;
    }

  if (super_count != 1)
    {
      /* class_oid has no superclasses which means that it is not a partition
       * of a partitioned class. However, class_oid might still point to a
       * partitioned class so we're trying this bellow
       */
      error = partition_load_pruning_context (thread_p, class_oid, &pinfo);
    }
  else
    {
      error = partition_load_pruning_context (thread_p, super_class, &pinfo);
    }

  if (error != NO_ERROR || pinfo.partitions == NULL)
    {
      /* Error while initializing pruning context or there are no partitions,
         cleanup and exit */
      goto cleanup;
    }

  error =
    partition_find_partition_for_record (&pinfo, class_oid, recdes,
					 pruned_class_oid, pruned_hfid);
  if (error != NO_ERROR)
    {
      goto cleanup;
    }

cleanup:
  if (super_class != NULL)
    {
      free_and_init (super_class);
    }

  (void) partition_clear_pruning_context (&pinfo);

  return error;
}

/*
 * partition_get_partitions () - get partitions of a partitioned class
 * return : error code or NO_ERROR
 * thread_p (in)      : thread entry
 * root_oid (in)      : root class OID
 * partitions (in/out): partitions
 * parts_count(in/out): partitions count
 */
int
partition_get_partitions (THREAD_ENTRY * thread_p, const OID * root_oid,
			  OR_PARTITION ** partitions, int *parts_count)
{
  int error = NO_ERROR;
  PRUNING_CONTEXT pinfo;
  bool is_modified = false;

  if (root_oid == NULL || partitions == NULL || parts_count == NULL)
    {
      assert (false);
      return ER_FAILED;
    }

  (void) partition_init_pruning_context (&pinfo);

  COPY_OID (&pinfo.root_oid, root_oid);
  pinfo.thread_p = thread_p;

  if (partition_load_context_from_cache (&pinfo, &is_modified))
    {
      *partitions = pinfo.partitions;
      *parts_count = pinfo.count;
      return NO_ERROR;
    }
  else if (pinfo.error_code != NO_ERROR)
    {
      return error;
    }

  /* not cached, load it */
  error =
    heap_get_class_partitions (thread_p, root_oid, partitions, parts_count);

  return error;
}
