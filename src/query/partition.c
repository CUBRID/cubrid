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
 * partition_sr.c - partition pruning on server
 */

#include <assert.h>
#include "partition_sr.h"

#include "dbtype.h"
#include "fetch.h"
#include "heap_file.h"
#include "object_primitive.h"
#include "object_representation.h"
#include "query_aggregate.hpp"
#include "query_executor.h"
#include "query_opfunc.h"
#include "stream_to_xasl.h"
#include "xasl.h"
#include "xasl_predicate.hpp"
#include "xasl_unpack_info.hpp"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

typedef enum match_status
{
  MATCH_OK,			/* no partitions contain usable data */
  MATCH_NOT_FOUND		/* search condition does not refer partition expression */
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

#define NELEMENTS 1024		/* maximum number of partitions */
#define _WORDSIZE 8 * sizeof(int)

#define MAX_ELEMENTS 1024
#define BITSET_WORD_SIZE sizeof (unsigned int)
#define BITS_IN_WORD (8 * BITSET_WORD_SIZE)
#define BITSET_WORD_COUNT (MAX_ELEMENTS / BITS_IN_WORD)
#define BITSET_LENGTH(s) ((((s)->count - 1) / BITS_IN_WORD)+1)

typedef struct pruning_bitset PRUNING_BITSET;
struct pruning_bitset
{
  unsigned int set[BITSET_WORD_COUNT];
  int count;
};

typedef struct pruning_bitset_iterator PRUNING_BITSET_ITERATOR;
struct pruning_bitset_iterator
{
  const PRUNING_BITSET *set;
  int next;
};

#define PARTITIONS_COUNT(pinfo) (((pinfo) == NULL) ? 0 : (pinfo)->count - 1)

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

/* PRUNING_BITSET operations */
static void pruningset_init (PRUNING_BITSET *, int);
static void pruningset_set_all (PRUNING_BITSET *);
static void pruningset_copy (PRUNING_BITSET *, const PRUNING_BITSET *);
static void pruningset_add (PRUNING_BITSET *, int);
static void pruningset_remove (PRUNING_BITSET *, int);
static void pruningset_intersect (PRUNING_BITSET *, const PRUNING_BITSET *);
static void pruningset_union (PRUNING_BITSET *, const PRUNING_BITSET *);
static bool pruningset_is_set (const PRUNING_BITSET *, int);
static int pruningset_popcount (const PRUNING_BITSET *);
static void pruningset_iterator_init (const PRUNING_BITSET *, PRUNING_BITSET_ITERATOR *);
static int pruningset_iterator_next (PRUNING_BITSET_ITERATOR *);
static int pruningset_to_spec_list (PRUNING_CONTEXT * pinfo, const PRUNING_BITSET * pruned);

/* pruning operations */
static int partition_free_cache_entry_kv (const void *key, void *data, void *args);
static int partition_free_cache_entry (PARTITION_CACHE_ENTRY * entry);
static int partition_cache_pruning_context (PRUNING_CONTEXT * pinfo, bool * already_exists);
static bool partition_load_context_from_cache (PRUNING_CONTEXT * pinfo, bool * is_modified);
static int partition_cache_entry_to_pruning_context (PRUNING_CONTEXT * pinfo, PARTITION_CACHE_ENTRY * entry_p);
static PARTITION_CACHE_ENTRY *partition_pruning_context_to_cache_entry (PRUNING_CONTEXT * pinfo);
static PRUNING_OP partition_rel_op_to_pruning_op (REL_OP op);
static int partition_load_partition_predicate (PRUNING_CONTEXT * pinfo, OR_PARTITION * master);
static void partition_free_partition_predicate (PRUNING_CONTEXT * pinfo);
static void partition_set_specified_partition (PRUNING_CONTEXT * pinfo, const OID * partition_oid);
static int partition_get_position_in_key (PRUNING_CONTEXT * pinfo, BTID * btid);
static ATTR_ID partition_get_attribute_id (REGU_VARIABLE * regu_var);
static void partition_set_cache_info_for_expr (REGU_VARIABLE * regu_var, ATTR_ID attr_id, HEAP_CACHE_ATTRINFO * info);
static MATCH_STATUS partition_match_pred_expr (PRUNING_CONTEXT * pinfo, const PRED_EXPR * pr, PRUNING_BITSET * pruned);
static MATCH_STATUS partition_match_index_key (PRUNING_CONTEXT * pinfo, const KEY_INFO * key, RANGE_TYPE range_type,
					       PRUNING_BITSET * pruned);
static MATCH_STATUS partition_match_key_range (PRUNING_CONTEXT * pinfo, const KEY_RANGE * range,
					       PRUNING_BITSET * pruned);
static bool partition_do_regu_variables_match (PRUNING_CONTEXT * pinfo, const REGU_VARIABLE * left,
					       const REGU_VARIABLE * right);
static MATCH_STATUS partition_prune (PRUNING_CONTEXT * pinfo, const REGU_VARIABLE * arg, const PRUNING_OP op,
				     PRUNING_BITSET * pruned);
static MATCH_STATUS partition_prune_db_val (PRUNING_CONTEXT * pinfo, const DB_VALUE * val, const PRUNING_OP op,
					    PRUNING_BITSET * pruned);
static int partition_get_value_from_key (PRUNING_CONTEXT * pinfo, const REGU_VARIABLE * key, DB_VALUE * attr_key,
					 bool * is_present);
static int partition_get_value_from_inarith (PRUNING_CONTEXT * pinfo, const REGU_VARIABLE * src, DB_VALUE * value_p,
					     bool * is_present);
static int partition_get_value_from_regu_var (PRUNING_CONTEXT * pinfo, const REGU_VARIABLE * key, DB_VALUE * value_p,
					      bool * is_value);
static MATCH_STATUS partition_prune_range (PRUNING_CONTEXT * pinfo, const DB_VALUE * val, const PRUNING_OP op,
					   PRUNING_BITSET * pruned);
static MATCH_STATUS partition_prune_list (PRUNING_CONTEXT * pinfo, const DB_VALUE * val, const PRUNING_OP op,
					  PRUNING_BITSET * pruned);
static MATCH_STATUS partition_prune_hash (PRUNING_CONTEXT * pinfo, const DB_VALUE * val, const PRUNING_OP op,
					  PRUNING_BITSET * pruned);
static int partition_find_partition_for_record (PRUNING_CONTEXT * pinfo, const OID * class_oid, RECDES * recdes,
						OID * partition_oid, HFID * partition_hfid);
static int partition_prune_heap_scan (PRUNING_CONTEXT * pinfo);

static int partition_prune_index_scan (PRUNING_CONTEXT * pinfo);

static int partition_find_inherited_btid (THREAD_ENTRY * thread_p, OID * src_class, OID * dest_class, BTID * src_btid,
					  BTID * dest_btid);
static int partition_attrinfo_get_key (THREAD_ENTRY * thread_p, PRUNING_CONTEXT * pcontext, DB_VALUE * curr_key,
				       OID * class_oid, BTID * btid, DB_VALUE * partition_key);

/* misc pruning functions */
static bool partition_decrement_value (DB_VALUE * val);


/* PRUNING_BITSET manipulation functions */

/*
 * pruningset_init () - initialize a PRUNING_BITSET object
 * return : void
 * s (in/out) : PRUNING_BITSET object
 * count (in) : number of elements
 */
static void
pruningset_init (PRUNING_BITSET * s, int count)
{
  s->count = count;
  memset (s->set, 0, BITSET_WORD_COUNT * BITSET_WORD_SIZE);
}

/*
 * pruningset_set_all () - set all bits in in a PRUNING_BITSET
 * return : void
 * set (in/out) : PRUNING_BITSET object
 */
static void
pruningset_set_all (PRUNING_BITSET * set)
{
  memset (set->set, (~0), BITSET_WORD_COUNT * BITSET_WORD_SIZE);
}

/*
 * pruningset_copy () - copy a PRUNING_BITSET
 * return : void
 * dest (in/out) : destination
 * src (in)	 : source
 */
static void
pruningset_copy (PRUNING_BITSET * dest, const PRUNING_BITSET * src)
{
  unsigned int i;

  pruningset_init (dest, src->count);

  for (i = 0; i < BITSET_LENGTH (dest); i++)
    {
      dest->set[i] = src->set[i];
    }
}

/*
 * pruningset_add () - add an element
 * return : void
 * s (in/out) : PRUNING_BITSET object
 * i (in)     : element to add
 */
static void
pruningset_add (PRUNING_BITSET * s, int i)
{
  s->set[i / BITS_IN_WORD] |= (1 << (i % BITS_IN_WORD));
}

/*
 * pruningset_remove () - remove an element
 * return : void
 * s (in/out) : PRUNING_BITSET object
 * i (in)     : element to remove
 */
static void
pruningset_remove (PRUNING_BITSET * s, int i)
{
  s->set[i / BITS_IN_WORD] &= ~(1 << (i % BITS_IN_WORD));
}

/*
 * pruningset_intersect () - perform intersection on two PRUNING_BITSET
 *			     objects
 * return : void
 * left (in/out) :
 * right (in)	 :
 */
static void
pruningset_intersect (PRUNING_BITSET * left, const PRUNING_BITSET * right)
{
  unsigned int i;

  for (i = 0; i < BITSET_LENGTH (left); i++)
    {
      left->set[i] &= right->set[i];
    }
}

/*
 * pruningset_union () - perform intersection on two PRUNING_BITSET objects
 * return : void
 * left (in/out) :
 * right (in)	 :
 */
static void
pruningset_union (PRUNING_BITSET * left, const PRUNING_BITSET * right)
{
  unsigned int max, i;

  max = BITSET_LENGTH (left);
  if (BITSET_LENGTH (left) > BITSET_LENGTH (right))
    {
      max = BITSET_LENGTH (right);
    }

  for (i = 0; i < max; i++)
    {
      left->set[i] |= right->set[i];
    }
}

/*
 * pruningset_is_set () - test if an element is set
 * return : true if the element is set
 * s (in) : PRUNING_BITSET object
 * idx (in) : index to test
 */
static bool
pruningset_is_set (const PRUNING_BITSET * s, int idx)
{
  return ((s->set[idx / BITS_IN_WORD] & (1 << (idx % BITS_IN_WORD))) != 0);
}

/*
 * pruningset_popcount () - return the number of elements which are set in a
 *			    PRUNING_BITSET object
 * return : number of elements set
 * s (in) : PRUNING_BITSET object
 */
static int
pruningset_popcount (const PRUNING_BITSET * s)
{
  /* we expect to have only a few bits set, so the Brian Kernighan algorithm should be the fastest. */
  int count = 0;
  unsigned i, v;

  for (i = 0; i < BITSET_LENGTH (s); i++)
    {
      v = s->set[i];
      for (; v != 0; count++)
	{
	  v &= v - 1;
	}
    }

  if (count > s->count)
    {
      return s->count;
    }

  return count;
}

/*
 * pruningset_iterator_init () - initialize an iterator
 * return : void
 * set (in) :
 * i (in/out) :
 */
static void
pruningset_iterator_init (const PRUNING_BITSET * set, PRUNING_BITSET_ITERATOR * i)
{
  i->set = set;
  i->next = 0;
}

/*
 * pruningset_iterator_next () - advance the iterator
 * return      : next element
 * it (in/out) : iterator
 */
static int
pruningset_iterator_next (PRUNING_BITSET_ITERATOR * it)
{
  unsigned int i, j;
  unsigned int word;
  int pos = 0;

  if (it->next >= it->set->count)
    {
      it->next = -1;
      return -1;
    }

  for (i = it->next / BITS_IN_WORD; i < BITSET_LENGTH (it->set); i++)
    {
      if (it->set->set[i] == 0)
	{
	  continue;
	}

      word = it->set->set[i];
      pos = it->next % BITS_IN_WORD;

      for (j = pos; j < BITS_IN_WORD; j++)
	{
	  if ((word & (1 << j)) != 0)
	    {
	      it->next = i * BITS_IN_WORD + j + 1;
	      return (i * BITS_IN_WORD + j);
	    }
	}

      it->next = (i + 1) * BITS_IN_WORD;
    }

  it->next = -1;

  return -1;
}

/*
 * partition_free_cache_entry_kv () - A callback function to free memory
 *                                    allocated for a cache entry key and value.
 * return : error code or NO_ERROR
 * key (in)   :
 * data (in)  :
 * args (in)  :
 *
 */
static int
partition_free_cache_entry_kv (const void *key, void *data, void *args)
{
  return partition_free_cache_entry ((PARTITION_CACHE_ENTRY *) data);
}

/*
 * partition_free_cache_entry () - free memory allocated for a cache entry
 * return : error code or NO_ERROR
 * entry (in)  :
 *
 * Note: Since cache entries are not allocated in the private heaps used
 *  by threads, this function changes the private heap of the calling thread
 *  to the '0' heap (to use malloc/free) before actually freeing the memory
 *  and sets it back after it finishes
 */
static int
partition_free_cache_entry (PARTITION_CACHE_ENTRY * entry)
{
  HL_HEAPID old_heap;

  /* change private heap */
  old_heap = db_change_private_heap (NULL, 0);

  if (entry != NULL)
    {
      if (entry->partitions != NULL)
	{
	  int i;

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
 *
 * Note: The cached information is not copied into the curent context, it is
 * just referenced. This is because the cached information for partitions
 * represents only schema information and this information cannot be changed
 * unless the changer has exclusive access to this class. In this case,
 * other callers do not have access to this area.
 */
static int
partition_cache_entry_to_pruning_context (PRUNING_CONTEXT * pinfo, PARTITION_CACHE_ENTRY * entry_p)
{
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

  pinfo->partitions = entry_p->partitions;

  pinfo->attr_id = entry_p->attr_id;

  pinfo->partition_type = (DB_PARTITION_TYPE) pinfo->partitions[0].partition_type;

  return NO_ERROR;
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
  int i = 0;
  HL_HEAPID old_heap_id = 0;

  assert (pinfo != NULL);

  entry_p = (PARTITION_CACHE_ENTRY *) malloc (sizeof (PARTITION_CACHE_ENTRY));
  if (entry_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (PARTITION_CACHE_ENTRY));
      pinfo->error_code = ER_FAILED;
      goto error_return;
    }
  entry_p->partitions = NULL;
  entry_p->count = 0;

  COPY_OID (&entry_p->class_oid, &pinfo->root_oid);
  entry_p->attr_id = pinfo->attr_id;

  entry_p->count = pinfo->count;

  entry_p->partitions = (OR_PARTITION *) malloc (entry_p->count * sizeof (OR_PARTITION));
  if (entry_p->partitions == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      entry_p->count * sizeof (PARTITION_CACHE_ENTRY));
      pinfo->error_code = ER_FAILED;
      goto error_return;
    }

  /* Change private heap to 0 to use malloc/free for entry_p allocated values. These values outlast the current thread
   * heap */
  old_heap_id = db_change_private_heap (pinfo->thread_p, 0);
  for (i = 0; i < entry_p->count; i++)
    {
      COPY_OID (&entry_p->partitions[i].class_oid, &pinfo->partitions[i].class_oid);

      HFID_COPY (&entry_p->partitions[i].class_hfid, &pinfo->partitions[i].class_hfid);

      entry_p->partitions[i].partition_type = pinfo->partitions[i].partition_type;

      entry_p->partitions[i].rep_id = pinfo->partitions[i].rep_id;

      if (pinfo->partitions[i].values != NULL)
	{
	  entry_p->partitions[i].values = db_seq_copy (pinfo->partitions[i].values);
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
	  int j;

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
 * exists (out) : return true if put into cache success,
 *                return false if the key already exists in hash map.
 */
static int
partition_cache_pruning_context (PRUNING_CONTEXT * pinfo, bool * already_exists)
{
  PARTITION_CACHE_ENTRY *entry_p = NULL;
  OID *oid_key = NULL;
  const void *val;

  if (!PARTITION_IS_CACHE_INITIALIZED ())
    {
      return NO_ERROR;
    }

  assert (pinfo != NULL);

  /* Check if this class is being modified by this transaction. If this is the case, we can't actually use this cache
   * entry because the next request might have already modified the partitioning schema and we won't know it */
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
  if (csect_enter (pinfo->thread_p, CSECT_PARTITION_CACHE, INF_WAIT) != NO_ERROR)
    {
      return ER_FAILED;
    }

  val = mht_put_if_not_exists (db_Partition_Ht, oid_key, entry_p);
  if (val == NULL)
    {
      csect_exit (pinfo->thread_p, CSECT_PARTITION_CACHE);
      return ER_FAILED;
    }

  if (val != entry_p)
    {
      partition_free_cache_entry (entry_p);
      *already_exists = true;
    }
  else
    {
      *already_exists = false;
    }

  csect_exit (pinfo->thread_p, CSECT_PARTITION_CACHE);

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

  /* Check if this class is being modified by this transaction. If this is the case, we can't actually use this cache
   * entry because the next request might have already modified the partitioning schema and we won't know it */
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

  if (csect_enter_as_reader (pinfo->thread_p, CSECT_PARTITION_CACHE, INF_WAIT) != NO_ERROR)
    {
      pinfo->error_code = ER_FAILED;
      return false;
    }

  entry_p = (PARTITION_CACHE_ENTRY *) mht_get (db_Partition_Ht, &pinfo->root_oid);
  if (entry_p == NULL)
    {
      csect_exit (pinfo->thread_p, CSECT_PARTITION_CACHE);
      return false;
    }

  if (partition_cache_entry_to_pruning_context (pinfo, entry_p) != NO_ERROR)
    {
      csect_exit (pinfo->thread_p, CSECT_PARTITION_CACHE);
      return false;
    }

  csect_exit (pinfo->thread_p, CSECT_PARTITION_CACHE);

  pinfo->is_from_cache = true;

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

  db_Partition_Ht = mht_create (PARTITION_CACHE_NAME, PARTITION_CACHE_SIZE, oid_hash, oid_compare_equals);
  if (db_Partition_Ht == NULL)
    {
      error = ER_FAILED;
      goto cleanup;
    }

cleanup:
  csect_exit (thread_p, CSECT_PARTITION_CACHE);

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
      (void) mht_map (db_Partition_Ht, partition_free_cache_entry_kv, NULL);
      mht_destroy (db_Partition_Ht);
      db_Partition_Ht = NULL;
    }

  csect_exit (thread_p, CSECT_PARTITION_CACHE);
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
  if (!PARTITION_IS_CACHE_INITIALIZED ())
    {
      return;
    }

  if (csect_enter (thread_p, CSECT_PARTITION_CACHE, INF_WAIT) != NO_ERROR)
    {
      return;
    }

  (void) mht_rem (db_Partition_Ht, class_oid, partition_free_cache_entry_kv, NULL);

  csect_exit (thread_p, CSECT_PARTITION_CACHE);
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
 * pruningset_to_spec_list () - convert a pruningset to an array of
 *				PARTITION_SPEC_TYPE elements
 * return : error code or NO_ERROR
 * pinfo (in)  : pruning context
 * pruned (in) : pruned partitions
 */
static int
pruningset_to_spec_list (PRUNING_CONTEXT * pinfo, const PRUNING_BITSET * pruned)
{
  int cnt = 0, i = 0, pos = 0, error = NO_ERROR;
  PARTITION_SPEC_TYPE *spec = NULL;
  bool is_index = false;
  char *btree_name = NULL;
  OID *master_oid = NULL;
  BTID *master_btid = NULL;
  PRUNING_BITSET_ITERATOR it;

  cnt = pruningset_popcount (pruned);
  if (cnt == 0)
    {
      /* pruning did not find any partition, just return */
      error = NO_ERROR;
      goto cleanup;
    }

  if (pinfo->spec->access == ACCESS_METHOD_INDEX || pinfo->spec->access == ACCESS_METHOD_INDEX_KEY_INFO)
    {
      /* we have to load information about the index used so we can duplicate it for each partition */
      is_index = true;
      master_oid = &ACCESS_SPEC_CLS_OID (pinfo->spec);
      master_btid = &pinfo->spec->indexptr->btid;
      error =
	heap_get_indexinfo_of_btid (pinfo->thread_p, master_oid, master_btid, NULL, NULL, NULL, NULL, &btree_name,
				    NULL);
      if (error != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto cleanup;
	}
    }

  spec = (PARTITION_SPEC_TYPE *) db_private_alloc (pinfo->thread_p, cnt * sizeof (PARTITION_SPEC_TYPE));
  if (spec == NULL)
    {
      assert (false);
      error = ER_FAILED;
      goto cleanup;
    }

  pruningset_iterator_init (pruned, &it);

  pos = 0;
  for (i = 0, pos = pruningset_iterator_next (&it); i < cnt && pos >= 0; i++, pos = pruningset_iterator_next (&it))
    {
      COPY_OID (&spec[i].oid, &pinfo->partitions[pos + 1].class_oid);
      HFID_COPY (&spec[i].hfid, &pinfo->partitions[pos + 1].class_hfid);

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
	  error = heap_get_index_with_name (pinfo->thread_p, &spec[i].oid, btree_name, &spec[i].btid);

	  if (error != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      goto cleanup;
	    }
	}
    }

cleanup:
  if (btree_name != NULL)
    {
      /* heap_get_indexinfo_of_btid calls strdup on the index name so we have to free it with free_and_init */
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
partition_do_regu_variables_match (PRUNING_CONTEXT * pinfo, const REGU_VARIABLE * left, const REGU_VARIABLE * right)
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
      if (tp_value_compare (&left->value.dbval, &right->value.dbval, 1, 0) != DB_EQ)
	{
	  return false;
	}
      else
	{
	  return true;
	}

    case TYPE_CONSTANT:
      /* use varptr */
      if (tp_value_compare (left->value.dbvalptr, right->value.dbvalptr, 1, 1) != DB_EQ)
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
	DB_VALUE *val_left, *val_right;

	val_left = (DB_VALUE *) pinfo->vd->dbval_ptr + left->value.val_pos;
	val_right = (DB_VALUE *) pinfo->vd->dbval_ptr + right->value.val_pos;

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
      if (left->value.arithptr->misc_operand != right->value.arithptr->misc_operand)
	{
	  return false;
	}

      /* check left operand */
      if (!partition_do_regu_variables_match (pinfo, left->value.arithptr->leftptr, right->value.arithptr->leftptr))
	{
	  return false;
	}

      /* check right operand */
      if (!partition_do_regu_variables_match (pinfo, left->value.arithptr->rightptr, right->value.arithptr->rightptr))
	{
	  return false;
	}

      /* check third operand */
      if (!partition_do_regu_variables_match (pinfo, left->value.arithptr->thirdptr, right->value.arithptr->thirdptr))
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
 * return : match status
 * pinfo (in)	  : pruning context
 * val (in)	  : the value to which the partition expression is compared
 * op (in)	  : operator to apply
 * pruned (in/out): pruned partitions
 */
static MATCH_STATUS
partition_prune_list (PRUNING_CONTEXT * pinfo, const DB_VALUE * val, const PRUNING_OP op, PRUNING_BITSET * pruned)
{
  OR_PARTITION *part;
  int size = 0, i = 0, j;
  DB_SEQ *part_collection = NULL;
  DB_COLLECTION *val_collection = NULL;
  MATCH_STATUS status = MATCH_NOT_FOUND;

  for (i = 0; i < PARTITIONS_COUNT (pinfo); i++)
    {
      part = &pinfo->partitions[i + 1];
      part_collection = part->values;

      size = db_set_size (part_collection);
      if (size < 0)
	{
	  return MATCH_NOT_FOUND;
	}

      switch (op)
	{
	case PO_IN:
	  {
	    DB_VALUE col;
	    bool found = false;

	    /* Perform intersection on part_values and val. If the result is not empty then this partition should be
	     * added to the pruned list */
	    if (!db_value_type_is_collection (val))
	      {
		return MATCH_NOT_FOUND;
	      }

	    val_collection = db_get_set (val);
	    for (j = 0; j < size; j++)
	      {
		db_set_get (part_collection, j, &col);
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
		pruningset_add (pruned, i);
	      }

	    status = MATCH_OK;
	    break;
	  }

	case PO_NOT_IN:
	  {
	    DB_VALUE col;
	    bool found = false;

	    /* Perform intersection on part_values and val. If the result is empty then this partition should be added
	     * to the pruned list */
	    if (!db_value_type_is_collection (val))
	      {
		status = MATCH_NOT_FOUND;
	      }

	    val_collection = db_get_set (val);
	    for (j = 0; j < size; j++)
	      {
		db_set_get (part_collection, j, &col);
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
		pruningset_add (pruned, i);
	      }

	    status = MATCH_OK;
	    break;
	  }

	case PO_IS_NULL:
	  /* Add this partition if part_values contains val. */
	  if (db_set_has_null (part_collection))
	    {
	      /* add this partition */
	      pruningset_add (pruned, i);
	      /* Since partition values are disjoint sets this is the only possible result */
	      return MATCH_OK;
	    }
	  break;

	case PO_EQ:
	  /* Add this partition if part_values contains val. */
	  if (db_set_ismember (part_collection, (DB_VALUE *) val))
	    {
	      /* add this partition */
	      pruningset_add (pruned, i);
	      /* Since partition values are disjoint sets this is the only possible result */
	      return MATCH_OK;
	    }
	  break;

	case PO_NE:
	case PO_LT:
	case PO_LE:
	case PO_GT:
	case PO_GE:
	default:
	  return status = MATCH_NOT_FOUND;
	}
    }

  return status;
}

/*
 * partition_prune_hash () - Perform pruning for HASH type partitions
 * return : match status
 * pinfo (in)	  : pruning context
 * val (in)	  : the value to which the partition expression is compared
 * op (in)	  : operator to apply
 * pruned (in/out): pruned partitions
 */
static MATCH_STATUS
partition_prune_hash (PRUNING_CONTEXT * pinfo, const DB_VALUE * val_p, const PRUNING_OP op, PRUNING_BITSET * pruned)
{
  int idx = 0;
  int hash_size = PARTITIONS_COUNT (pinfo);
  TP_DOMAIN *col_domain = NULL;
  DB_VALUE val;
  MATCH_STATUS status = MATCH_NOT_FOUND;

  db_make_null (&val);

  col_domain = pinfo->partition_pred->func_regu->domain;
  switch (op)
    {
    case PO_EQ:
      if (TP_DOMAIN_TYPE (col_domain) != DB_VALUE_TYPE (val_p))
	{
	  /* We have a problem here because type checking might not have coerced val to the type of the column. If this
	   * is the case, we have to do it here */
	  if (tp_value_cast (val_p, &val, col_domain, false) == DOMAIN_INCOMPATIBLE)
	    {
	      /* We cannot set an error here because this is not considered to be an error when scanning regular
	       * tables. We can consider this predicate to be always false so status to MATCH_OK to simulate the case
	       * when no appropriate partition was found. */
	      er_clear ();
	      status = MATCH_OK;
	    }
	}
      else
	{
	  pr_clone_value (val_p, &val);
	}

      idx = mht_get_hash_number (hash_size, &val);

      /* Start from 1 because we're using position 0 for the master class */
      pruningset_add (pruned, idx);

      status = MATCH_OK;
      break;

    case PO_IN:
      {
	DB_COLLECTION *values = NULL;
	int size = 0, i, idx;

	if (!db_value_type_is_collection (val_p))
	  {
	    /* This is an error and it should be handled outside of the pruning environment */
	    status = MATCH_NOT_FOUND;
	    goto cleanup;
	  }

	values = db_get_set (val_p);
	size = db_set_size (values);
	if (size < 0)
	  {
	    pinfo->error_code = ER_FAILED;
	    status = MATCH_NOT_FOUND;
	    goto cleanup;
	  }

	for (i = 0; i < size; i++)
	  {
	    DB_VALUE col;

	    if (db_set_get (values, i, &col) != NO_ERROR)
	      {
		pinfo->error_code = ER_FAILED;
		status = MATCH_NOT_FOUND;
		goto cleanup;
	      }

	    if (TP_DOMAIN_TYPE (col_domain) != DB_VALUE_TYPE (&col))
	      {
		/* A failed coercion is not an error in this case, we should just skip over it */
		if (tp_value_cast (val_p, &val, col_domain, false) == DOMAIN_INCOMPATIBLE)
		  {
		    pr_clear_value (&col);
		    er_clear ();
		    continue;
		  }
	      }

	    idx = mht_get_hash_number (hash_size, &col);
	    pruningset_add (pruned, idx);

	    pr_clear_value (&col);
	  }

	status = MATCH_OK;
	break;
      }

    case PO_IS_NULL:
      /* first partition */
      pruningset_add (pruned, 0);
      status = MATCH_OK;
      break;

    default:
      status = MATCH_NOT_FOUND;
      break;
    }

cleanup:
  pr_clear_value (&val);

  return status;
}

/*
 * partition_prune_range () - Perform pruning for RANGE type partitions
 * return : match status
 * pinfo (in)	   : pruning context
 * val(in)	   : the value to which the partition expression is compared
 * op (in)	   : operator to apply
 * pruned (in/out) : pruned partitions
 */
static MATCH_STATUS
partition_prune_range (PRUNING_CONTEXT * pinfo, const DB_VALUE * val, const PRUNING_OP op, PRUNING_BITSET * pruned)
{
  int i = 0, error = NO_ERROR;
  int added = 0;
  OR_PARTITION *part;
  DB_VALUE min, max;
  int rmin = DB_UNK, rmax = DB_UNK;
  MATCH_STATUS status;

  db_make_null (&min);
  db_make_null (&max);

  for (i = 0; i < PARTITIONS_COUNT (pinfo); i++)
    {
      part = &pinfo->partitions[i + 1];

      pr_clear_value (&min);
      pr_clear_value (&max);

      error = db_set_get (part->values, 0, &min);
      if (error != NO_ERROR)
	{
	  pinfo->error_code = error;
	  status = MATCH_NOT_FOUND;
	  goto cleanup;
	}

      error = db_set_get (part->values, 1, &max);
      if (error != NO_ERROR)
	{
	  pinfo->error_code = error;
	  status = MATCH_NOT_FOUND;
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
	  if (op == PO_GT)
	    {
	      /* Partitioning interval is stored as [min, max). Try to convert it to [min, max--] in order to handle
	       * some limit cases like val > max-- which should not match any partition */
	      (void) partition_decrement_value (&max);
	    }
	  rmax = tp_value_compare (val, &max, 1, 1);
	}

      status = MATCH_OK;
      switch (op)
	{
	case PO_EQ:
	  /* Filter is part_expr = value. Find the *only* partition for which min <= value < max */
	  if ((rmin == DB_EQ || rmin == DB_LT) && rmax == DB_LT)
	    {
	      pruningset_add (pruned, i);
	      ++added;
	      /* no need to look any further */
	      goto cleanup;
	    }
	  break;

	case PO_LT:
	  /* Filter is part_expr < value. All partitions for which min < value qualify */
	  if (rmin == DB_LT)
	    {
	      pruningset_add (pruned, i);
	      ++added;
	    }
	  break;

	case PO_LE:
	  /* Filter is part_expr <= value. All partitions for which min <= value qualify */
	  if (rmin == DB_EQ)
	    {
	      /* this is the only partition than can qualify */
	      pruningset_add (pruned, i);
	      ++added;
	      goto cleanup;
	    }
	  else if (rmin == DB_LT)
	    {
	      pruningset_add (pruned, i);
	      ++added;
	    }
	  break;

	case PO_GT:
	  /* Filter is part_expr > value. All partitions for which value < max qualify */
	  if (rmax == DB_LT)
	    {
	      pruningset_add (pruned, i);
	      ++added;
	    }
	  break;

	case PO_GE:
	  /* Filter is part_expr > value. All partitions for which value < max qualify */
	  if (rmax == DB_LT)
	    {
	      pruningset_add (pruned, i);
	      ++added;
	    }
	  break;

	case PO_IS_NULL:
	  if (DB_IS_NULL (&min))
	    {
	      pruningset_add (pruned, i);
	      ++added;
	      /* no need to look any further */
	      goto cleanup;
	    }
	  break;

	default:
	  status = MATCH_NOT_FOUND;
	  goto cleanup;
	}
    }

cleanup:
  pr_clear_value (&min);
  pr_clear_value (&max);

  if (status == MATCH_OK && added == 0)
    {
      status = MATCH_NOT_FOUND;
    }

  return status;
}

/*
 * partition_prune_db_val () - prune partitions using the given DB_VALUE
 * return : match status
 * pinfo (in)	   : pruning context
 * val (in)	   : value to use for pruning
 * op (in)	   : operation to be applied for value
 * pruned (in/out) : pruned partitions
 */
static MATCH_STATUS
partition_prune_db_val (PRUNING_CONTEXT * pinfo, const DB_VALUE * val, const PRUNING_OP op, PRUNING_BITSET * pruned)
{
  switch (pinfo->partition_type)
    {
    case DB_PARTITION_HASH:
      return partition_prune_hash (pinfo, val, op, pruned);

    case DB_PARTITION_RANGE:
      return partition_prune_range (pinfo, val, op, pruned);

    case DB_PARTITION_LIST:
      return partition_prune_list (pinfo, val, op, pruned);

    default:
      return MATCH_NOT_FOUND;
    }

  return MATCH_NOT_FOUND;
}

/*
 * partition_prune () - perform pruning on the specified partitions list
 * return : match status
 * pinfo (in)	  : pruning context
 * val (in)	  : the value to which the partition expression is compared
 * op (in)	  : operator to apply
 * pruned (in/out): pruned partitions
 */
static MATCH_STATUS
partition_prune (PRUNING_CONTEXT * pinfo, const REGU_VARIABLE * arg, const PRUNING_OP op, PRUNING_BITSET * pruned)
{
  MATCH_STATUS status = MATCH_NOT_FOUND;
  DB_VALUE val;
  bool is_value = false;

  if (arg == NULL && op != PO_IS_NULL)
    {
      return MATCH_NOT_FOUND;
    }

  if (op == PO_IS_NULL)
    {
      db_make_null (&val);
      is_value = true;
    }
  else if (partition_get_value_from_regu_var (pinfo, arg, &val, &is_value) != NO_ERROR)
    {
      /* pruning failed */
      pinfo->error_code = ER_FAILED;
      return MATCH_NOT_FOUND;
    }

  if (!is_value)
    {
      /* cannot perform pruning */
      return MATCH_NOT_FOUND;
    }

  status = partition_prune_db_val (pinfo, &val, op, pruned);

  pr_clear_value (&val);

  return status;
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
partition_get_value_from_regu_var (PRUNING_CONTEXT * pinfo, const REGU_VARIABLE * regu, DB_VALUE * value_p,
				   bool * is_value)
{
  /* we cannot use fetch_peek_dbval here because we're not inside a scan yet. */
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
	DB_VALUE *arg_val = (DB_VALUE *) pinfo->vd->dbval_ptr + regu->value.val_pos;
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
	    db_make_null (value_p);
	    return NO_ERROR;
	  }
	if (partition_get_value_from_key (pinfo, regu, value_p, is_value) != NO_ERROR)
	  {
	    goto error;
	  }
	break;
      }

    case TYPE_INARITH:
      /* We can't evaluate the general form of TYPE_INARITH but we can handle "pseudo constants" (SYS_DATE, SYS_TIME,
       * etc) and the CAST operator applied to constants. Eventually, it would be great if we could evaluate all pseudo
       * constant here (like cast ('1' as int) + 1) */
      if (partition_get_value_from_inarith (pinfo, regu, value_p, is_value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    default:
      db_make_null (value_p);
      *is_value = false;
      return NO_ERROR;
    }

  return NO_ERROR;

error:
  db_make_null (value_p);
  *is_value = false;

  return ER_FAILED;
}

/*
 * partition_is_reguvar_const () - test if a regu_variable is a constant
 * return : true if constant, false otherwise
 * regu_var (in) :
 */
static bool
partition_is_reguvar_const (const REGU_VARIABLE * regu_var)
{
  if (regu_var == NULL)
    {
      return false;
    }
  switch (regu_var->type)
    {
    case TYPE_DBVAL:
    case TYPE_POS_VALUE:
    case TYPE_REGUVAL_LIST:
      return true;
    case TYPE_INARITH:
    case TYPE_OUTARITH:
      {
	ARITH_TYPE *arithptr = regu_var->value.arithptr;
	if (arithptr->leftptr != NULL && !partition_is_reguvar_const (arithptr->leftptr))
	  {
	    return false;
	  }
	if (arithptr->rightptr != NULL && !partition_is_reguvar_const (arithptr->rightptr))
	  {
	    return false;
	  }

	if (arithptr->thirdptr != NULL && !partition_is_reguvar_const (arithptr->thirdptr))
	  {
	    return false;
	  }
	/* either all arguments are constants of this is an expression with no arguments */
	return true;
      }
    default:
      return false;
    }
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
partition_get_value_from_key (PRUNING_CONTEXT * pinfo, const REGU_VARIABLE * key, DB_VALUE * attr_key,
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
	DB_VALUE *val = (DB_VALUE *) pinfo->vd->dbval_ptr + key->value.val_pos;
	error = pr_clone_value (val, attr_key);

	*is_present = true;
	break;
      }

    case TYPE_FUNC:
      {
	/* this is a F_MIDXKEY function and the value we're interested in is in the operands */
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
	    db_make_null (attr_key);
	    error = NO_ERROR;

	    *is_present = false;
	  }
	else
	  {
	    error = partition_get_value_from_key (pinfo, &regu_list->value, attr_key, is_present);
	  }
	break;
      }

    case TYPE_INARITH:
      error = partition_get_value_from_regu_var (pinfo, key, attr_key, is_present);
      break;

    case TYPE_CONSTANT:
      /* TYPE_CONSTANT comes from an index join. Since we haven't actually scanned anything yet, this value is not set
       * so we cannot use it here */
      db_make_null (attr_key);
      error = NO_ERROR;
      *is_present = false;
      break;

    default:
      assert (false);

      db_make_null (attr_key);

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
partition_get_value_from_inarith (PRUNING_CONTEXT * pinfo, const REGU_VARIABLE * src, DB_VALUE * value_p,
				  bool * is_value)
{
  int error = NO_ERROR;
  DB_VALUE *peek_val = NULL;

  assert_release (src != NULL);
  assert_release (value_p != NULL);
  assert_release (src->type == TYPE_INARITH);

  *is_value = false;
  db_make_null (value_p);

  if (!partition_is_reguvar_const (src))
    {
      return NO_ERROR;
    }

  error = fetch_peek_dbval (pinfo->thread_p, (REGU_VARIABLE *) src, pinfo->vd, NULL, NULL, NULL, &peek_val);
  if (error != NO_ERROR)
    {
      return error;
    }

  /* peek_val will be cleared when scanning is performed on this REGU_VAR */
  error = pr_clone_value (peek_val, value_p);
  if (error != NO_ERROR)
    {
      return error;
    }

  *is_value = true;

  return NO_ERROR;
}

/*
 * partition_match_pred_expr () - get partitions matching a predicate
 *				  expression
 * return : match status
 * pinfo (in)	  : pruning context
 * pr (in)	  : predicate expression
 * pruned (in/out): pruned partitions
 */
static MATCH_STATUS
partition_match_pred_expr (PRUNING_CONTEXT * pinfo, const PRED_EXPR * pr, PRUNING_BITSET * pruned)
{
  MATCH_STATUS status = MATCH_NOT_FOUND;
  REGU_VARIABLE *part_expr = pinfo->partition_pred->func_regu;

  if (pr == NULL)
    {
      assert (false);
      return MATCH_NOT_FOUND;
    }

  switch (pr->type)
    {
    case T_PRED:
      {
	/* T_PRED contains conjunctions and disjunctions of PRED_EXPR. Get partitions matching the left PRED_EXPR and
	 * partitions matching the right PRED_EXPR and merge or intersect the lists */
	PRUNING_BITSET left_set, right_set;
	MATCH_STATUS lstatus, rstatus;
	BOOL_OP op = pr->pe.m_pred.bool_op;

	if (op != B_AND && op != B_OR)
	  {
	    /* only know how to handle AND/OR predicates */
	    status = MATCH_NOT_FOUND;
	    break;
	  }

	pruningset_init (&left_set, PARTITIONS_COUNT (pinfo));
	pruningset_init (&right_set, PARTITIONS_COUNT (pinfo));

	lstatus = partition_match_pred_expr (pinfo, pr->pe.m_pred.lhs, &left_set);
	rstatus = partition_match_pred_expr (pinfo, pr->pe.m_pred.rhs, &right_set);

	if (op == B_AND)
	  {
	    /* do intersection between left and right */
	    if (lstatus == MATCH_NOT_FOUND)
	      {
		/* pr->pe.m_pred.lhs does not refer part_expr so return right */
		pruningset_copy (pruned, &right_set);
		status = rstatus;
	      }
	    else if (rstatus == MATCH_NOT_FOUND)
	      {
		/* pr->pe.m_pred.rhs does not refer part_expr so return right */
		pruningset_copy (pruned, &left_set);
		status = lstatus;
	      }
	    else
	      {
		status = MATCH_OK;
		pruningset_intersect (&left_set, &right_set);
		pruningset_copy (pruned, &left_set);
	      }
	  }
	else
	  {
	    /* this is the OR operator so union the two sets */
	    if (lstatus == MATCH_NOT_FOUND || rstatus == MATCH_NOT_FOUND)
	      {
		status = MATCH_NOT_FOUND;
	      }
	    else
	      {
		pruningset_union (&left_set, &right_set);
		pruningset_copy (pruned, &left_set);

		status = MATCH_OK;
	      }
	  }
	break;
      }

    case T_EVAL_TERM:
      switch (pr->pe.m_eval_term.et_type)
	{
	case T_COMP_EVAL_TERM:
	  {
	    /* see if part_expr matches right or left */
	    REGU_VARIABLE *left, *right;
	    PRUNING_OP op;

	    left = pr->pe.m_eval_term.et.et_comp.lhs;
	    right = pr->pe.m_eval_term.et.et_comp.rhs;
	    op = partition_rel_op_to_pruning_op (pr->pe.m_eval_term.et.et_comp.rel_op);

	    status = MATCH_NOT_FOUND;
	    if (partition_do_regu_variables_match (pinfo, left, part_expr))
	      {
		status = partition_prune (pinfo, right, op, pruned);
	      }
	    else if (partition_do_regu_variables_match (pinfo, right, part_expr))
	      {
		status = partition_prune (pinfo, left, op, pruned);
	      }
	    break;
	  }

	case T_ALSM_EVAL_TERM:
	  {
	    REGU_VARIABLE *regu, *list;
	    PRUNING_OP op;

	    regu = pr->pe.m_eval_term.et.et_alsm.elem;
	    list = pr->pe.m_eval_term.et.et_alsm.elemset;
	    op = partition_rel_op_to_pruning_op (pr->pe.m_eval_term.et.et_alsm.rel_op);
	    /* adjust rel_op based on the QL_FLAG of the alsm eval node */
	    if (pr->pe.m_eval_term.et.et_alsm.eq_flag == F_SOME)
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
		status = partition_prune (pinfo, list, op, pruned);
	      }
	  }
	  break;

	case T_LIKE_EVAL_TERM:
	case T_RLIKE_EVAL_TERM:
	  /* Don't know how to work with LIKE/RLIKE expressions yet. There are some cases in which we can deduce a
	   * range from the like pattern. */
	  status = MATCH_NOT_FOUND;
	  break;
	}
      break;

    case T_NOT_TERM:
      status = MATCH_NOT_FOUND;
      break;
    }

  return status;
}

/*
 * partition_match_key_range () - perform pruning using a key_range
 * return : pruned list
 * pinfo (in)	   : pruning context
 * partitions (in) : partitions to prune
 * key_range (in)  : key range
 * status (in/out) : pruning status
 */
static MATCH_STATUS
partition_match_key_range (PRUNING_CONTEXT * pinfo, const KEY_RANGE * key_range, PRUNING_BITSET * pruned)
{
  PRUNING_OP lop = PO_INVALID, rop = PO_INVALID;
  MATCH_STATUS lstatus, rstatus;
  PRUNING_BITSET left, right;

  switch (key_range->range)
    {
    case NA_NA:
      /* v1 and v2 are N/A, so that no range is defined */
      return MATCH_NOT_FOUND;

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

  pruningset_init (&left, PARTITIONS_COUNT (pinfo));
  pruningset_init (&right, PARTITIONS_COUNT (pinfo));

  /* prune left */
  if (lop == PO_INVALID)
    {
      lstatus = MATCH_NOT_FOUND;
    }
  else
    {
      lstatus = partition_prune (pinfo, key_range->key1, lop, &left);
      if (pinfo->error_code != NO_ERROR)
	{
	  return MATCH_NOT_FOUND;
	}
    }

  /* prune right */
  if (rop == PO_INVALID)
    {
      rstatus = MATCH_NOT_FOUND;
    }
  else
    {
      rstatus = partition_prune (pinfo, key_range->key2, rop, &right);
      if (pinfo->error_code != NO_ERROR)
	{
	  return MATCH_NOT_FOUND;
	}
    }

  if (lstatus == MATCH_NOT_FOUND)
    {
      pruningset_copy (pruned, &right);
      return rstatus;
    }

  if (rstatus == MATCH_NOT_FOUND)
    {
      pruningset_copy (pruned, &left);
      return lstatus;
    }

  pruningset_copy (pruned, &left);
  pruningset_intersect (pruned, &right);

  return MATCH_OK;
}

/*
 * partition_match_index_key () - get the list of partitions that fit into the
 *				  index key
 * return : match status
 * pinfo (in)	      : pruning context
 * key (in)	      : index key info
 * range_type (in)    : range type
 * partitions (in/out): pruned partitions
 */
static MATCH_STATUS
partition_match_index_key (PRUNING_CONTEXT * pinfo, const KEY_INFO * key, RANGE_TYPE range_type,
			   PRUNING_BITSET * pruned)
{
  int i;
  PRUNING_BITSET key_pruned;
  MATCH_STATUS status;

  if (pinfo->partition_pred->func_regu->type != TYPE_ATTR_ID)
    {
      return MATCH_NOT_FOUND;
    }

  status = MATCH_OK;

  /* We do not care which range_type this index scan is supposed to perform. Each key range produces a list of
   * partitions and we will merge those lists to get the full list of partitions that contains information for our
   * search */
  for (i = 0; i < key->key_cnt; i++)
    {
      pruningset_init (&key_pruned, PARTITIONS_COUNT (pinfo));

      status = partition_match_key_range (pinfo, &key->key_ranges[i], &key_pruned);
      if (status == MATCH_NOT_FOUND)
	{
	  /* For key ranges we have to find a match for all ranges. If we get a MATCH_NOT_FOUND then we have to assume
	   * that all partitions have to be scanned for the result */
	  pruningset_set_all (pruned);
	  break;
	}

      pruningset_union (pruned, &key_pruned);
    }

  return status;
}

/*
 * partition_init_pruning_context () - initialize pruning context
 * return : void
 * pinfo (in/out)  : pruning context
 */
void
partition_init_pruning_context (PRUNING_CONTEXT * pinfo)
{
  if (pinfo == NULL)
    {
      assert (false);
      return;
    }

  OID_SET_NULL (&pinfo->root_oid);
  pinfo->thread_p = NULL;
  pinfo->partitions = NULL;
  pinfo->selected_partition = NULL;
  pinfo->spec = NULL;
  pinfo->vd = NULL;
  pinfo->count = 0;
  pinfo->fp_cache_context = NULL;
  pinfo->partition_pred = NULL;
  pinfo->attr_position = -1;
  pinfo->error_code = NO_ERROR;
  pinfo->scan_cache_list = NULL;
  pinfo->pruning_type = DB_PARTITIONED_CLASS;
  pinfo->is_attr_info_inited = false;
  pinfo->is_from_cache = false;
}

/*
 * partition_find_root_class_oid () - Find the OID of the root partitioned
 *				      class
 * return : error code or NO_ERROR
 * thread_p (in)      : thread entry
 * class_oid (in)     : either the OID of the partitioned class or the OID of
 *			one of the partitions
 * super_oid (in/out) : OID of the partitioned class
 */
int
partition_find_root_class_oid (THREAD_ENTRY * thread_p, const OID * class_oid, OID * super_oid)
{
  int error = NO_ERROR, super_count = 0;
  OID *super_classes = NULL;

  error = heap_get_class_supers (thread_p, class_oid, &super_classes, &super_count);
  if (error != NO_ERROR)
    {
      ASSERT_ERROR ();
      if (super_classes != NULL)
	{
	  free_and_init (super_classes);
	}

      return error;
    }

  if (super_count > 1)
    {
      OID_SET_NULL (super_oid);
    }
  else if (super_count != 1)
    {
      /* class_oid has no superclasses which means that it is not a partition of a partitioned class. However,
       * class_oid might still point to a partitioned class so we're trying this bellow */
      COPY_OID (super_oid, class_oid);
    }
  else if (super_count == 1)
    {
      COPY_OID (super_oid, super_classes);
    }

  if (super_classes != NULL)
    {
      free_and_init (super_classes);
    }

  return NO_ERROR;
}

/*
 * partition_load_pruning_context () - load pruning context
 * return : error code or NO_ERROR
 * thread_p (in)    :
 * class_oid (in)   : oid of the class for which the context should be loaded
 * pruning_type(in) : DB_CLASS_PARTITION_TYPE specifying if this class is a
 *		      partition or the actual partitioned class
 * pinfo (in/out)   : pruning context
 */
int
partition_load_pruning_context (THREAD_ENTRY * thread_p, const OID * class_oid, int pruning_type,
				PRUNING_CONTEXT * pinfo)
{
  int error = NO_ERROR;
  OR_PARTITION *master = NULL;
  bool is_modified = false;
  bool already_exists = false;

  if (pinfo == NULL)
    {
      assert (false);
      return ER_FAILED;
    }
  assert_release (pruning_type == DB_PARTITIONED_CLASS || pruning_type == DB_PARTITION_CLASS);

  (void) partition_init_pruning_context (pinfo);

  pinfo->pruning_type = pruning_type;
  pinfo->thread_p = thread_p;
  if (pruning_type == DB_PARTITIONED_CLASS)
    {
      COPY_OID (&pinfo->root_oid, class_oid);
    }
  else
    {
      error = partition_find_root_class_oid (thread_p, class_oid, &pinfo->root_oid);
      if (error != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto error_return;
	}
    }

reload_from_cache:

  /* try to get info from the cache first */
  if (partition_load_context_from_cache (pinfo, &is_modified))
    {
      master = &pinfo->partitions[0];
      pinfo->root_repr_id = master->rep_id;
      /* load the partition predicate which is not deserialized in the cache */
      error = partition_load_partition_predicate (pinfo, master);
      if (error != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto error_return;
	}
      if (pruning_type == DB_PARTITION_CLASS)
	{
	  partition_set_specified_partition (pinfo, class_oid);
	}
      return NO_ERROR;
    }

  if (pinfo->error_code != NO_ERROR)
    {
      error = pinfo->error_code;
      ASSERT_ERROR ();
      goto error_return;
    }

  error = heap_get_class_partitions (pinfo->thread_p, &pinfo->root_oid, &pinfo->partitions, &pinfo->count);
  if (error != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto error_return;
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
      ASSERT_ERROR ();
      goto error_return;
    }

  pinfo->partition_type = (DB_PARTITION_TYPE) master->partition_type;
  pinfo->root_repr_id = master->rep_id;

  pinfo->attr_id = partition_get_attribute_id (pinfo->partition_pred->func_regu);
  pinfo->is_from_cache = false;

  if (!is_modified)
    {
      /* Cache the loaded info. If the call below is successful, pinfo will be returned holding the cached information */
      partition_cache_pruning_context (pinfo, &already_exists);

      /* Multiple thread can reach here synchronously. In this case redo the action of load from cache. */
      if (already_exists)
	{
	  heap_clear_partition_info (pinfo->thread_p, pinfo->partitions, pinfo->count);
	  pinfo->partitions = NULL;
	  pinfo->count = 0;

	  partition_free_partition_predicate (pinfo);

	  goto reload_from_cache;
	}
    }

  if (pruning_type == DB_PARTITION_CLASS)
    {
      partition_set_specified_partition (pinfo, class_oid);
    }

  return NO_ERROR;

error_return:
  if (pinfo != NULL)
    {
      partition_clear_pruning_context (pinfo);

      pinfo->error_code = error;
    }

  return error;
}

/*
 * partition_clear_pruning_context () - free memory allocated for pruning
 *					context
 * return : void
 * pinfo (in) : pruning context
 */
void
partition_clear_pruning_context (PRUNING_CONTEXT * pinfo)
{
  SCANCACHE_LIST *list, *next;

  if (pinfo == NULL)
    {
      assert (false);
      return;
    }

  if (!pinfo->is_from_cache)
    {
      if (pinfo->partitions != NULL)
	{
	  heap_clear_partition_info (pinfo->thread_p, pinfo->partitions, pinfo->count);
	}
    }

  pinfo->partitions = NULL;
  pinfo->selected_partition = NULL;
  pinfo->count = 0;

  partition_free_partition_predicate (pinfo);

  if (pinfo->is_attr_info_inited)
    {
      heap_attrinfo_end (pinfo->thread_p, &(pinfo->attr_info));
      pinfo->is_attr_info_inited = false;
    }

  list = pinfo->scan_cache_list;
  while (list != NULL)
    {
      next = list->next;
      if (list->scan_cache.is_scan_cache_started)
	{
	  heap_scancache_end (pinfo->thread_p, &list->scan_cache.scan_cache);
	}
      if (list->scan_cache.func_index_pred != NULL)
	{
	  heap_free_func_pred_unpack_info (pinfo->thread_p, list->scan_cache.n_indexes,
					   list->scan_cache.func_index_pred, NULL);
	}
      db_private_free (pinfo->thread_p, list);
      list = next;
    }

  pinfo->scan_cache_list = NULL;
}

/*
 * partition_load_partition_predicate () - load partition predicate
 * return :
 * pinfo (in)	: pruning context
 * master (in)	: master partition information
 */
static int
partition_load_partition_predicate (PRUNING_CONTEXT * pinfo, OR_PARTITION * master)
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
      ASSERT_ERROR ();
      return error;
    }

  assert (DB_VALUE_TYPE (&val) == DB_TYPE_CHAR);
  // use const_cast since of a limitation of or_unpack_* functions which do not accept const
  expr_stream = CONST_CAST (char *, db_get_string (&val));
  stream_len = db_get_string_size (&val);

  /* unpack partition expression */
  error = stx_map_stream_to_func_pred (pinfo->thread_p, &pinfo->partition_pred, expr_stream, stream_len,
				       &pinfo->fp_cache_context);
  if (error != NO_ERROR)
    {
      ASSERT_ERROR ();
    }
  pr_clear_value (&val);

  return error;
}

/*
 * partition_free_partition_predicate () - free partition predicate
 * return :
 * pinfo (in)	: pruning context
 */
static void
partition_free_partition_predicate (PRUNING_CONTEXT * pinfo)
{
  if (pinfo->partition_pred != NULL && pinfo->partition_pred->func_regu != NULL)
    {
      (void) qexec_clear_partition_expression (pinfo->thread_p, pinfo->partition_pred->func_regu);
      pinfo->partition_pred = NULL;
    }

  if (pinfo->fp_cache_context != NULL)
    {
      free_xasl_unpack_info (pinfo->thread_p, pinfo->fp_cache_context);
    }
}

/*
 * partition_set_specified_partition () - find OR_PARTITION object for
 *					  specified partition and set it
 *					  in the pruning context
 * return : void
 * pinfo (in)	      : pruning context
 * partition_oid (in) : partition oid
 */
static void
partition_set_specified_partition (PRUNING_CONTEXT * pinfo, const OID * partition_oid)
{
  int i = 0;
  for (i = 0; i < PARTITIONS_COUNT (pinfo); i++)
    {
      if (OID_EQ (partition_oid, &(pinfo->partitions[i + 1].class_oid)))
	{
	  pinfo->selected_partition = &pinfo->partitions[i + 1];
	  break;
	}
    }
  assert_release (pinfo->selected_partition != NULL);
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
partition_set_cache_info_for_expr (REGU_VARIABLE * var, ATTR_ID attr_id, HEAP_CACHE_ATTRINFO * attr_info)
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
      (void) partition_set_cache_info_for_expr (var->value.arithptr->leftptr, attr_id, attr_info);
      (void) partition_set_cache_info_for_expr (var->value.arithptr->rightptr, attr_id, attr_info);
      (void) partition_set_cache_info_for_expr (var->value.arithptr->thirdptr, attr_id, attr_info);
      break;

    case TYPE_FUNC:
      {
	REGU_VARIABLE_LIST op = var->value.funcp->operand;

	while (op != NULL)
	  {
	    (void) partition_set_cache_info_for_expr (&op->value, attr_id, attr_info);
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
	  attr_id = partition_get_attribute_id (var->value.arithptr->rightptr);
	  if (attr_id != NULL_ATTRID)
	    {
	      return attr_id;
	    }
	}

      if (var->value.arithptr->thirdptr != NULL)
	{
	  attr_id = partition_get_attribute_id (var->value.arithptr->thirdptr);
	  if (attr_id != NULL_ATTRID)
	    {
	      return attr_id;
	    }
	}

      return attr_id;

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
 * btid (in) : btid for which to get the position
 */
static int
partition_get_position_in_key (PRUNING_CONTEXT * pinfo, BTID * btid)
{
  int i = 0;
  int error = NO_ERROR;
  ATTR_ID part_attr_id = -1;
  ATTR_ID *keys = NULL;
  int key_count = 0;

  pinfo->attr_position = -1;

  if (pinfo->partition_pred->func_regu->type != TYPE_ATTR_ID)
    {
      /* In the case of index keys, we will only apply pruning if the partition expression is actually an attribute.
       * This is because we will not have expressions in the index key, only attributes (except for function and filter
       * indexes which are not handled yet) */
      pinfo->attr_position = -1;
      return NO_ERROR;
    }

  part_attr_id = pinfo->partition_pred->func_regu->value.attr_descr.id;

  error =
    heap_get_indexinfo_of_btid (pinfo->thread_p, &pinfo->root_oid, btid, NULL, &key_count, &keys, NULL, NULL, NULL);
  if (error != NO_ERROR)
    {
      ASSERT_ERROR ();
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
  PRUNING_BITSET pruned;
  MATCH_STATUS status = MATCH_NOT_FOUND;

  assert (pinfo != NULL);
  assert (pinfo->partitions != NULL);

  pruningset_init (&pruned, PARTITIONS_COUNT (pinfo));

  if (pinfo->spec->where_pred == NULL)
    {
      status = MATCH_NOT_FOUND;
    }
  else
    {
      status = partition_match_pred_expr (pinfo, pinfo->spec->where_pred, &pruned);
      if (pinfo->error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return pinfo->error_code;
	}
    }

  if (status != MATCH_NOT_FOUND)
    {
      error = pruningset_to_spec_list (pinfo, &pruned);
      if (error != NO_ERROR)
	{
	  ASSERT_ERROR ();
	}
    }
  else
    {
      /* consider all partitions */
      pruningset_set_all (&pruned);
      error = pruningset_to_spec_list (pinfo, &pruned);
      if (error != NO_ERROR)
	{
	  ASSERT_ERROR ();
	}
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
  PRUNING_BITSET pruned;
  MATCH_STATUS status = MATCH_NOT_FOUND;

  assert (pinfo != NULL);
  assert (pinfo->partitions != NULL);
  assert (pinfo->spec != NULL);
  assert (pinfo->spec->indexptr != NULL);

  pruningset_init (&pruned, PARTITIONS_COUNT (pinfo));
  if (pinfo->spec->where_pred != NULL)
    {
      status = partition_match_pred_expr (pinfo, pinfo->spec->where_pred, &pruned);
    }

  if (pinfo->spec->where_key != NULL)
    {
      status = partition_match_pred_expr (pinfo, pinfo->spec->where_key, &pruned);
    }

  if (pinfo->attr_position != -1)
    {
      if (pinfo->spec->indexptr->use_iss && pinfo->attr_position == 0)
	{
	  /* The first position is missing in ISS and we're dealing with a virtual predicate key = NULL. In this case,
	   * all partitions qualify for the search */
	  pruningset_set_all (&pruned);
	  status = MATCH_OK;
	}
      else if (pinfo->spec->indexptr->func_idx_col_id != -1)
	{
	  /* We are dealing with a function index, so all partitions qualify for the search. */
	  pruningset_set_all (&pruned);
	  status = MATCH_OK;
	}
      else
	{
	  status =
	    partition_match_index_key (pinfo, &pinfo->spec->indexptr->key_info, pinfo->spec->indexptr->range_type,
				       &pruned);
	}
    }
  if (status == MATCH_NOT_FOUND)
    {
      if (pinfo->error_code != NO_ERROR)
	{
	  return pinfo->error_code;
	}
      else
	{
	  pruningset_set_all (&pruned);
	  error = pruningset_to_spec_list (pinfo, &pruned);
	  if (error != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	    }
	}
    }
  else
    {
      error = pruningset_to_spec_list (pinfo, &pruned);
      if (error != NO_ERROR)
	{
	  ASSERT_ERROR ();
	}
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
partition_prune_spec (THREAD_ENTRY * thread_p, val_descr * vd, access_spec_node * spec)
{
  int error = NO_ERROR;
  PRUNING_CONTEXT pinfo;

  if (spec == NULL)
    {
      assert (false);

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return ER_FAILED;
    }
  if (spec->pruning_type != DB_PARTITIONED_CLASS)
    {
      /* nothing to prune */
      spec->pruned = true;
      return NO_ERROR;
    }
  if (spec->type != TARGET_CLASS && spec->type != TARGET_CLASS_ATTR)
    {
      /* nothing to prune */
      spec->pruned = true;
      return NO_ERROR;
    }

  (void) partition_init_pruning_context (&pinfo);

  spec->curent = NULL;
  spec->parts = NULL;

  error = partition_load_pruning_context (thread_p, &ACCESS_SPEC_CLS_OID (spec), spec->pruning_type, &pinfo);
  if (error != NO_ERROR)
    {
      ASSERT_ERROR ();
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

  if (spec->access == ACCESS_METHOD_SEQUENTIAL || spec->access == ACCESS_METHOD_SEQUENTIAL_RECORD_INFO
      || spec->access == ACCESS_METHOD_SEQUENTIAL_PAGE_SCAN || spec->access == ACCESS_METHOD_SEQUENTIAL_SAMPLING_SCAN)
    {
      error = partition_prune_heap_scan (&pinfo);
      if (error != NO_ERROR)
	{
	  ASSERT_ERROR ();
	}
    }
  else
    {
      if (spec->indexptr == NULL)
	{
	  assert (false);

	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  partition_clear_pruning_context (&pinfo);
	  return ER_FAILED;
	}

      if (pinfo.partition_pred->func_regu->type != TYPE_ATTR_ID)
	{
	  /* In the case of index keys, we will only apply pruning if the partition expression is actually an
	   * attribute. This is because we will not have expressions in the index key, only attributes (except for
	   * function and filter indexes which are not handled yet) */
	  pinfo.attr_position = -1;
	}
      else
	{
	  BTID *btid = &spec->indexptr->btid;
	  error = partition_get_position_in_key (&pinfo, btid);
	  if (error != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      partition_clear_pruning_context (&pinfo);
	      return error;
	    }
	}
      error = partition_prune_index_scan (&pinfo);
      if (error != NO_ERROR)
	{
	  ASSERT_ERROR ();
	}
    }

  partition_clear_pruning_context (&pinfo);

  if (error == NO_ERROR)
    {
      spec->pruned = true;
    }

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
partition_find_partition_for_record (PRUNING_CONTEXT * pinfo, const OID * class_oid, RECDES * recdes,
				     OID * partition_oid, HFID * partition_hfid)
{
  PRUNING_BITSET pruned;
  PRUNING_BITSET_ITERATOR it;
  bool clear_dbvalues = false;
  DB_VALUE *result = NULL;
  MATCH_STATUS status = MATCH_NOT_FOUND;
  int error = NO_ERROR, count = 0, pos;
  PRUNING_OP op = PO_EQ;
  REPR_ID repr_id = NULL_REPRID;

  assert (partition_oid != NULL);
  assert (partition_hfid != NULL);

  pruningset_init (&pruned, PARTITIONS_COUNT (pinfo));

  if (pinfo->is_attr_info_inited == false)
    {
      error = heap_attrinfo_start (pinfo->thread_p, &pinfo->root_oid, 1, &pinfo->attr_id, &pinfo->attr_info);
      if (error != NO_ERROR)
	{
	  goto cleanup;
	}

      partition_set_cache_info_for_expr (pinfo->partition_pred->func_regu, pinfo->attr_id, &pinfo->attr_info);
      pinfo->is_attr_info_inited = true;
    }

  /* set root representation id to the recdes so that we can read the value as belonging to the partitioned table */
  repr_id = or_rep_id (recdes);
  or_set_rep_id (recdes, pinfo->root_repr_id);

  error = heap_attrinfo_read_dbvalues (pinfo->thread_p, &pinfo->attr_info.inst_oid, recdes, &pinfo->attr_info);

  or_set_rep_id (recdes, repr_id);
  if (error != NO_ERROR)
    {
      goto cleanup;
    }
  clear_dbvalues = true;

  error =
    fetch_peek_dbval (pinfo->thread_p, pinfo->partition_pred->func_regu, NULL, (OID *) class_oid,
		      &pinfo->attr_info.inst_oid, NULL, &result);
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

  status = partition_prune_db_val (pinfo, result, op, &pruned);
  count = pruningset_popcount (&pruned);
  if (status != MATCH_OK)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_PARTITION_NOT_EXIST, 0);
      error = ER_PARTITION_NOT_EXIST;
      goto cleanup;
    }

  if (count != 1)
    {
      /* At this stage we should absolutely have a result. If we don't then something went wrong (either some internal
       * error (e.g. allocation failed) or the inserted value cannot be placed in any partition */
      if (pinfo->error_code == NO_ERROR)
	{
	  /* no appropriate partition found */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_PARTITION_NOT_EXIST, 0);
	  error = ER_PARTITION_NOT_EXIST;
	}
      else
	{
	  /* This is an internal *error (allocation, etc). Error was set by the calls above, just set *error code */
	  error = pinfo->error_code;
	}
      goto cleanup;
    }

  pruningset_iterator_init (&pruned, &it);

  pos = pruningset_iterator_next (&it);
  assert_release (pos >= 0);

  COPY_OID (partition_oid, &pinfo->partitions[pos + 1].class_oid);
  HFID_COPY (partition_hfid, &pinfo->partitions[pos + 1].class_hfid);

  if (!OID_EQ (class_oid, partition_oid))
    {
      /* Update representation id of the record to that of the pruned partition. For any other operation than pruning,
       * the new representation id should be obtained by constructing a new HEAP_ATTRIBUTE_INFO structure for the new
       * class, copying values from this record to that structure and then transforming it to disk. Since we're working
       * with partitioned tables, we can guarantee that, except for the actual representation id bits, the new record
       * will be exactly the same. Because of this, we can take a shortcut here and only update the bits from the
       * representation id */

      repr_id = pinfo->partitions[pos + 1].rep_id;
      error = or_set_rep_id (recdes, repr_id);
    }

cleanup:
  if (clear_dbvalues)
    {
      heap_attrinfo_clear_dbvalues (&pinfo->attr_info);
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
 * pcontext (in)  : pruning context
 * pruning_type (in) : pruning type
 * pruned_class_oid (in/out) : partition to insert into
 * pruned_hfid (in/out)	     : HFID of the partition
 * superclass_oid (in/out)   : OID of the partitioned class
 *
 * Note: The pruning context argument may be null, in which case, this
 * function loads the pruning context internally. If the pcontext argument
 * is not null, this function uses that context to perform internal
 * operations.
 * If the INSERT operation is repetitive (e.g: for INSERT...SELECT), the
 * caller should initialize a PRUNING_CONTEXT object (by calling
 * partition_init_pruning_context) and pass it to this function for each
 * insert operation in the query.
 */
int
partition_prune_insert (THREAD_ENTRY * thread_p, const OID * class_oid, RECDES * recdes, HEAP_SCANCACHE * scan_cache,
			PRUNING_CONTEXT * pcontext, int pruning_type, OID * pruned_class_oid, HFID * pruned_hfid,
			OID * superclass_oid)
{
  PRUNING_CONTEXT pinfo;
  bool keep_pruning_context = false;
  int error = NO_ERROR;

  assert (pruned_class_oid != NULL);
  assert (pruned_hfid != NULL);

  if (superclass_oid != NULL)
    {
      OID_SET_NULL (superclass_oid);
    }

  if (pruning_type == DB_NOT_PARTITIONED_CLASS)
    {
      assert (false);
      return NO_ERROR;
    }

  if (pcontext == NULL)
    {
      /* set it to point to pinfo so that we use the same variable */
      pcontext = &pinfo;
      keep_pruning_context = false;

      (void) partition_init_pruning_context (pcontext);
      error = partition_load_pruning_context (thread_p, class_oid, pruning_type, pcontext);
      if (error != NO_ERROR)
	{
	  return error;
	}
    }
  else
    {
      keep_pruning_context = true;

      if (pcontext->partitions == NULL)
	{
	  error = partition_load_pruning_context (thread_p, class_oid, pruning_type, pcontext);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }
	}
    }

  if (pcontext->partitions == NULL)
    {
      /* no partitions, cleanup and exit */
      goto cleanup;
    }

  error = partition_find_partition_for_record (pcontext, class_oid, recdes, pruned_class_oid, pruned_hfid);
  if (error != NO_ERROR)
    {
      goto cleanup;
    }

  if (pruning_type == DB_PARTITION_CLASS)
    {
      if (!OID_EQ (pruned_class_oid, &pcontext->selected_partition->class_oid))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INVALID_DATA_FOR_PARTITION, 0);
	  error = ER_INVALID_DATA_FOR_PARTITION;
	  goto cleanup;
	}
    }
  if (superclass_oid != NULL)
    {
      COPY_OID (superclass_oid, &pcontext->root_oid);
    }

cleanup:
  if (keep_pruning_context && error == NO_ERROR)
    {
      return NO_ERROR;
    }

  (void) partition_clear_pruning_context (pcontext);

  return error;
}

/*
 * partition_prune_update () - perform pruning on update statements
 * return : error code or NO_ERROR
 * thread_p (in)  : thread entry
 * class_oid (in) : OID of the root class
 * recdes (in)	  : Record describing the new object
 * pcontext (in)  : pruning context
 * pruning_type (in) : pruning type
 * pruned_class_oid (in/out) : partition to insert into
 * pruned_hfid (in/out)	     : HFID of the partition
 * superclass_oid (in/out)   : OID of the partitioned class
 *
 * Note: The pruning context argument may be null, in which case, this
 * function loads the pruning context internally. If the pcontext argument
 * is not null, this function uses that context to perform internal
 * operations.
 * If the UPDATE operation is repetitive (e.g: for server side UPDATE), the
 * caller should initialize a PRUNING_CONTEXT object (by calling
 * partition_init_pruning_context) and pass it to this function.
 */
int
partition_prune_update (THREAD_ENTRY * thread_p, const OID * class_oid, RECDES * recdes, PRUNING_CONTEXT * pcontext,
			int pruning_type, OID * pruned_class_oid, HFID * pruned_hfid, OID * superclass_oid)
{
  PRUNING_CONTEXT pinfo;
  int error = NO_ERROR;
  OID super_class;
  bool keep_pruning_context = false;

  if (OID_IS_ROOTOID (class_oid))
    {
      /* nothing to do here */
      COPY_OID (pruned_class_oid, class_oid);
      return NO_ERROR;
    }

  if (pruning_type == DB_NOT_PARTITIONED_CLASS)
    {
      assert (false);
      return NO_ERROR;
    }

  OID_SET_NULL (&super_class);
  if (superclass_oid != NULL)
    {
      OID_SET_NULL (superclass_oid);
    }

  if (pcontext == NULL)
    {
      /* set it to point to pinfo so that we use the same variable */
      pcontext = &pinfo;

      keep_pruning_context = false;
      if (pruning_type == DB_PARTITION_CLASS)
	{
	  error = partition_load_pruning_context (thread_p, class_oid, pruning_type, pcontext);
	}
      else
	{
	  /* UPDATE operation is always performed on an instance of a partition (since the top class holds no data).
	   * Even if pruning_type is DB_PARTITIONED_CLASS, class_oid will still hold the OID of the partition. Find the
	   * OID of the root class before loading pruning context. The function which loads the pruning context will
	   * get confused if we tell it that class_oid holds the partitioned class. */
	  (void) partition_init_pruning_context (pcontext);

	  error = partition_find_root_class_oid (thread_p, class_oid, &super_class);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }

	  if (OID_ISNULL (&super_class))
	    {
	      /* not a partitioned class */
	      return NO_ERROR;
	    }

	  error = partition_load_pruning_context (thread_p, &super_class, pruning_type, pcontext);
	}
    }
  else
    {
      keep_pruning_context = true;

      if (pcontext->partitions == NULL)
	{
	  /* this context should have been loaded before */
	  assert (false);

	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  error = ER_FAILED;
	}
    }


  if (error != NO_ERROR || pcontext->partitions == NULL)
    {
      /* Error while initializing pruning context or there are no partitions, cleanup and exit */
      goto cleanup;
    }

  error = partition_find_partition_for_record (pcontext, class_oid, recdes, pruned_class_oid, pruned_hfid);
  if (error != NO_ERROR)
    {
      goto cleanup;
    }

  if (pcontext->pruning_type == DB_PARTITION_CLASS)
    {
      if (!OID_EQ (pruned_class_oid, &pcontext->selected_partition->class_oid))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INVALID_DATA_FOR_PARTITION, 0);
	  error = ER_INVALID_DATA_FOR_PARTITION;
	  goto cleanup;
	}
    }

  if (superclass_oid != NULL)
    {
      COPY_OID (superclass_oid, &pcontext->root_oid);
    }

cleanup:
  if (keep_pruning_context && error == NO_ERROR)
    {
      return NO_ERROR;
    }

  (void) partition_clear_pruning_context (pcontext);
  pcontext = NULL;

  return error;
}

/*
 * partition_get_scancache () - get scan_cache for a partition
 * return : cached object or NULL
 * pcontext (in)      : pruning context
 * partition_oid (in) : partition
 */
PRUNING_SCAN_CACHE *
partition_get_scancache (PRUNING_CONTEXT * pcontext, const OID * partition_oid)
{
  SCANCACHE_LIST *node = NULL;

  if (partition_oid == NULL || pcontext == NULL)
    {
      assert_release (partition_oid != NULL);
      assert_release (pcontext == NULL);
      return NULL;
    }

  node = pcontext->scan_cache_list;
  while (node != NULL)
    {
      if (OID_EQ (&node->scan_cache.scan_cache.node.class_oid, partition_oid))
	{
	  return &node->scan_cache;
	}
      node = node->next;
    }

  return NULL;
}

/*
 * partition_new_scancache () - create a new scan_cache object
 * return : scan_cache entry or NULL
 * pcontext (in) : pruning context
 */
PRUNING_SCAN_CACHE *
partition_new_scancache (PRUNING_CONTEXT * pcontext)
{
  SCANCACHE_LIST *node = NULL;

  node = (SCANCACHE_LIST *) db_private_alloc (pcontext->thread_p, sizeof (SCANCACHE_LIST));
  if (node == NULL)
    {
      return NULL;
    }

  node->scan_cache.is_scan_cache_started = false;
  node->scan_cache.n_indexes = 0;
  node->scan_cache.func_index_pred = NULL;

  /* add it at the beginning */
  node->next = pcontext->scan_cache_list;
  pcontext->scan_cache_list = node;

  return &node->scan_cache;
}

/*
 * partition_get_partition_oids () - get OIDs of partition classes
 * return : error code or NO_ERROR
 * thread_p (in) :
 * class_oid (in)	   : partitioned class OID
 * partition_oids (in/out) : partition OIDs
 * count (in/out)	   : number of partitions
 */
int
partition_get_partition_oids (THREAD_ENTRY * thread_p, const OID * class_oid, OID ** partition_oids, int *count)
{
  int error = NO_ERROR;
  PRUNING_CONTEXT context;
  int i;
  OID *oids = NULL;
  OR_CLASSREP *classrepr = NULL;
  int classrepr_cacheindex = -1;
  bool clear_pcontext = false;

  assert_release (class_oid != NULL);
  assert_release (partition_oids != NULL);
  assert_release (count != NULL);

  /* get class representation to find partition information */
  classrepr = heap_classrepr_get (thread_p, class_oid, NULL, NULL_REPRID, &classrepr_cacheindex);
  if (classrepr == NULL)
    {
      goto cleanup;
    }

  if (classrepr->has_partition_info > 0)
    {
      partition_init_pruning_context (&context);
      clear_pcontext = true;

      error = partition_load_pruning_context (thread_p, class_oid, DB_PARTITIONED_CLASS, &context);
      if (error != NO_ERROR)
	{
	  goto cleanup;
	}

      if (context.count == 0)
	{
	  *count = 0;
	  *partition_oids = NULL;
	  goto cleanup;
	}
    }
  else
    {
      *count = 0;
      *partition_oids = NULL;
      goto cleanup;
    }
  oids = (OID *) db_private_alloc (thread_p, PARTITIONS_COUNT (&context) * sizeof (OID));
  if (oids == NULL)
    {
      error = ER_FAILED;
      goto cleanup;
    }
  for (i = 0; i < PARTITIONS_COUNT (&context); i++)
    {
      COPY_OID (&oids[i], &context.partitions[i + 1].class_oid);
    }
  *count = PARTITIONS_COUNT (&context);
  *partition_oids = oids;

cleanup:
  if (clear_pcontext == true)
    {
      partition_clear_pruning_context (&context);
    }
  if (classrepr != NULL)
    {
      heap_classrepr_free_and_init (classrepr, &classrepr_cacheindex);
    }
  if (error != NO_ERROR)
    {
      if (oids != NULL)
	{
	  db_private_free (thread_p, oids);
	}
      *partition_oids = NULL;
      *count = 0;
    }
  return error;
}

/*
 * partition_decrement_value () - decrement a DB_VALUE
 * return : true if value was decremented, false otherwise
 * val (in) : value to decrement
 */
static bool
partition_decrement_value (DB_VALUE * val)
{
  if (DB_IS_NULL (val))
    {
      return false;
    }

  switch (DB_VALUE_TYPE (val))
    {
    case DB_TYPE_INTEGER:
      val->data.i--;
      return true;

    case DB_TYPE_BIGINT:
      val->data.bigint--;
      return true;

    case DB_TYPE_SHORT:
      val->data.i--;
      return true;

    case DB_TYPE_TIME:
      val->data.time--;
      return true;

    case DB_TYPE_TIMESTAMP:
    case DB_TYPE_TIMESTAMPLTZ:
      val->data.utime--;
      return true;

    case DB_TYPE_TIMESTAMPTZ:
      val->data.timestamptz.timestamp--;
      return true;

    case DB_TYPE_DATETIME:
    case DB_TYPE_DATETIMELTZ:
      if (val->data.datetime.time == 0)
	{
	  val->data.datetime.date--;
	  val->data.datetime.time = MILLISECONDS_OF_ONE_DAY - 1;
	}
      else
	{
	  val->data.datetime.time--;
	}
      return true;

    case DB_TYPE_DATETIMETZ:
      if (val->data.datetimetz.datetime.time == 0)
	{
	  val->data.datetimetz.datetime.date--;
	  val->data.datetimetz.datetime.time = MILLISECONDS_OF_ONE_DAY - 1;
	}
      else
	{
	  val->data.datetimetz.datetime.time--;
	}
      return true;

    case DB_TYPE_DATE:
      if (val->data.date == 0)
	{
	  return false;
	}
      val->data.date--;
      return true;

    default:
      return false;
    }

  return false;
}

/*
 * partition_prune_unique_btid () - prune an UNIQUE BTID key search
 * return : error code or NO_ERROR
 * pcontext (in) : pruning context
 * key (in)	 : search key
 * class_oid (in/out) : class OID
 * class_hfid (in/out): class HFID
 * btid (in/out) :  class BTID
 *
 * Note: this function search for the partition which could contain the key
 * value and places the corresponding partition oid and btid in class_oid and
 * btid arguments
 */
int
partition_prune_unique_btid (PRUNING_CONTEXT * pcontext, DB_VALUE * key, OID * class_oid, HFID * class_hfid,
			     BTID * btid)
{
  int error = NO_ERROR, pos = 0;
  OID partition_oid;
  HFID partition_hfid;
  BTID partition_btid;

  error = partition_prune_partition_index (pcontext, key, class_oid, btid, &pos);

  if (error != NO_ERROR)
    {
      return error;
    }

  COPY_OID (&partition_oid, &pcontext->partitions[pos + 1].class_oid);
  HFID_COPY (&partition_hfid, &pcontext->partitions[pos + 1].class_hfid);

  error =
    partition_find_inherited_btid (pcontext->thread_p, &pcontext->root_oid, &partition_oid, btid, &partition_btid);
  if (error != NO_ERROR)
    {
      return error;
    }

  COPY_OID (class_oid, &partition_oid);
  HFID_COPY (class_hfid, &partition_hfid);
  BTID_COPY (btid, &partition_btid);

  return error;
}

/*
 * partition_find_inherited_btid () - find the inherited BTID for a class
 * return : error code or NO_ERROR
 * thread_p(in)	    : thread entry
 * src_class (in)   : class which owns src_btid
 * dest_class (in)  : class in which to search matching BTID
 * src_btid (in)    : BTID to search for
 * dest_btid (in/out) : matching BTID
 */
static int
partition_find_inherited_btid (THREAD_ENTRY * thread_p, OID * src_class, OID * dest_class, BTID * src_btid,
			       BTID * dest_btid)
{
  char *btree_name = NULL;
  int error = NO_ERROR;
  error = heap_get_indexinfo_of_btid (thread_p, src_class, src_btid, NULL, NULL, NULL, NULL, &btree_name, NULL);
  if (error != NO_ERROR)
    {
      goto cleanup;
    }
  error = heap_get_index_with_name (thread_p, dest_class, btree_name, dest_btid);
  if (error != NO_ERROR)
    {
      goto cleanup;
    }

  if (BTID_IS_NULL (dest_btid))
    {
      assert (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BTREE_INVALID_INDEX_ID, 3, src_btid->vfid.fileid,
	      src_btid->vfid.volid, src_btid->root_pageid);
      error = ER_BTREE_INVALID_INDEX_ID;
    }

cleanup:
  if (btree_name != NULL)
    {
      /* heap_get_indexinfo_of_btid calls strdup on the index name so we have to free it with free_and_init */
      free_and_init (btree_name);
    }
  return error;
}

/*
 * partition_load_aggregate_helper () - setup the members of an aggregate
 *					helper
 * return : error code or NO_ERROR
 * pcontext (in)     : pruning context
 * spec (in)	     : spec on which aggregates will be evaluated
 * pruned_count (in) : number of pruned partitions
 * root_btid (in)    : BTID of the index in the partitioned class
 * helper (in/out)   : aggregate helper
 */
int
partition_load_aggregate_helper (PRUNING_CONTEXT * pcontext, access_spec_node * spec, int pruned_count,
				 BTID * root_btid, HIERARCHY_AGGREGATE_HELPER * helper)
{
  int error = NO_ERROR, i = 0;
  char *btree_name = NULL;
  BTREE_TYPE btree_type;
  PARTITION_SPEC_TYPE *part = NULL;

  assert_release (helper != NULL);

  helper->btids = NULL;
  helper->hfids = NULL;
  helper->count = 0;

  if (spec->pruning_type != DB_PARTITIONED_CLASS || !spec->pruned)
    {
      return NO_ERROR;
    }

  /* setup pruned HFIDs */
  helper->hfids = (HFID *) db_private_alloc (pcontext->thread_p, pruned_count * sizeof (HFID));
  if (helper->hfids == NULL)
    {
      error = ER_OUT_OF_VIRTUAL_MEMORY;
      goto cleanup;
    }

  for (i = 0, part = spec->parts; part != NULL; i++, part = part->next)
    {
      HFID_COPY (&helper->hfids[i], &part->hfid);
    }

  assert (i == pruned_count);
  helper->count = pruned_count;

  if (BTID_IS_NULL (root_btid))
    {
      /* no BTID specified */
      return NO_ERROR;
    }

  error =
    heap_get_indexinfo_of_btid (pcontext->thread_p, &pcontext->root_oid, root_btid, &btree_type, NULL, NULL, NULL,
				&btree_name, NULL);
  if (error != NO_ERROR)
    {
      goto cleanup;
    }

  /* get local BTIDs for pruned partitions */
  helper->btids = (BTID *) db_private_alloc (pcontext->thread_p, pruned_count * sizeof (BTID));
  if (helper->btids == NULL)
    {
      error = ER_OUT_OF_VIRTUAL_MEMORY;
      goto cleanup;
    }

  for (i = 0, part = spec->parts; part != NULL; i++, part = part->next)
    {
      error = heap_get_index_with_name (pcontext->thread_p, &part->oid, btree_name, &helper->btids[i]);
      if (error != NO_ERROR)
	{
	  goto cleanup;
	}
    }

cleanup:
  if (error != NO_ERROR)
    {
      if (helper->btids != NULL)
	{
	  db_private_free_and_init (pcontext->thread_p, helper->btids);
	}
      if (helper->hfids != NULL)
	{
	  db_private_free_and_init (pcontext->thread_p, helper->hfids);
	}
      helper->count = 0;
    }

  if (btree_name != NULL)
    {
      free_and_init (btree_name);
    }
  return error;
}

#if 0
/*
 * partition_is_global_index () - check if an index is global for a partitioned
 *				class
 * return : error code or NO_ERROR
 * thread_p (in) :
 * class_oid (in)	   : partitioned class OID
 * contextp (in)	   : pruning context, NULL if it is unknown
 * btid (in)		   : btree ID of the index
 * btree_typep (in)	   : btree type of the index, NULL if it is unknown
 * is_global_index(out)	   :
 */
int
partition_is_global_index (THREAD_ENTRY * thread_p, PRUNING_CONTEXT * contextp, OID * class_oid, BTID * btid,
			   BTREE_TYPE * btree_typep, int *is_global_index)
{
  PRUNING_CONTEXT context;
  BTREE_TYPE btree_type;
  int error = NO_ERROR;

  assert (class_oid != NULL);
  assert (btid != NULL);

  *is_global_index = 0;

  if (contextp == NULL)
    {
      /* PRUNING_CONTEXT is unknown */
      contextp = &context;

      partition_init_pruning_context (contextp);

      error = partition_load_pruning_context (thread_p, class_oid, DB_PARTITIONED_CLASS, contextp);
      if (error != NO_ERROR)
	{
	  goto cleanup;
	}
    }

  if (contextp->count == 0)
    {
      goto cleanup;
    }

  if (btree_typep == NULL)
    {
      /* btree_type is unknown */
      btree_typep = &btree_type;

      error = heap_get_indexinfo_of_btid (thread_p, class_oid, btid, btree_typep, NULL, NULL, NULL, NULL, NULL);
      if (error != NO_ERROR)
	{
	  goto cleanup;
	}
    }

  if (*btree_typep == BTREE_PRIMARY_KEY)
    {
      *is_global_index = 1;
      goto cleanup;
    }

  if (btree_is_unique_type (*btree_typep))
    {
      error = partition_get_position_in_key (contextp, btid);
      if (error != NO_ERROR)
	{
	  goto cleanup;
	}

      if (contextp->attr_position == -1)
	{
	  *is_global_index = 1;
	  goto cleanup;
	}
    }

cleanup:
  if (contextp == &context)
    {
      partition_clear_pruning_context (contextp);
    }

  return error;
}
#endif

/*
 * partition_attrinfo_get_key () - retrieves the appropiate partitioning key
 *			    from a given index key, by which pruning will be
 *			    performed.
 * return : error code or NO_ERROR
 * thread_p (in) :
 * pcontext (in)	   : pruning context, NULL if it is unknown
 * curr_key (in)	   : index key to be searched
 * class_oid (in)	   : partitioned class OID
 * btid (in)		   : btree ID of the index
 * partition_key(out)	   : value holding the extracted partition key
 */
static int
partition_attrinfo_get_key (THREAD_ENTRY * thread_p, PRUNING_CONTEXT * pcontext, DB_VALUE * curr_key, OID * class_oid,
			    BTID * btid, DB_VALUE * partition_key)
{
  PRUNING_CONTEXT context;
  int error = NO_ERROR;
  ATTR_ID *btree_attr_ids = NULL;
  int btree_num_attr = 0;

  if (pcontext == NULL)
    {
      /* PRUNING_CONTEXT is unknown */
      pcontext = &context;

      partition_init_pruning_context (pcontext);

      error = partition_load_pruning_context (thread_p, class_oid, DB_PARTITIONED_CLASS, pcontext);
      if (error != NO_ERROR)
	{
	  goto cleanup;
	}
    }

  /* read partition attribute information */
  if (pcontext->is_attr_info_inited == false)
    {
      error = heap_attrinfo_start (thread_p, &pcontext->root_oid, 1, &pcontext->attr_id, &pcontext->attr_info);
      if (error != NO_ERROR)
	{
	  goto cleanup;
	}

      partition_set_cache_info_for_expr (pcontext->partition_pred->func_regu, pcontext->attr_id, &pcontext->attr_info);
      pcontext->is_attr_info_inited = true;
    }

  /* read btree information */
  error =
    heap_get_indexinfo_of_btid (thread_p, class_oid, btid, NULL, &btree_num_attr, &btree_attr_ids, NULL, NULL, NULL);
  if (error != NO_ERROR)
    {
      goto cleanup;
    }

  if (DB_VALUE_TYPE (curr_key) == DB_TYPE_MIDXKEY)
    {
      curr_key->data.midxkey.domain = btree_read_key_type (thread_p, btid);
      if (curr_key->data.midxkey.domain == NULL)
	{
	  error = ER_FAILED;
	  goto cleanup;
	}
    }
  error = btree_attrinfo_read_dbvalues (thread_p, curr_key, btree_attr_ids, btree_num_attr, &pcontext->attr_info, -1);
  if (error != NO_ERROR)
    {
      goto cleanup;
    }

  /* just use the corresponding db_value from attribute information */
  if (pcontext->attr_info.values[0].state == HEAP_UNINIT_ATTRVALUE)
    {
      error = ER_FAILED;
      goto cleanup;
    }
  error = pr_clone_value (&pcontext->attr_info.values[0].dbvalue, partition_key);
  if (error != NO_ERROR)
    {
      goto cleanup;
    }

cleanup:
  if (pcontext == &context)
    {
      partition_clear_pruning_context (pcontext);
    }
  if (btree_attr_ids)
    {
      db_private_free_and_init (thread_p, btree_attr_ids);
    }

  return error;
}

/*
 *  partition_prune_partition_index ():     - Gets the index of the partition where the key resides.
 *
 *  return :                              - NO_ERROR or error code.
 *
 *  pcontext (in)                         - Context of the partitions.
 *  key(in)                               - The key to be located.
 *  class_oid(in/out)                     - The correct OID of the object that contains the key.
 *  btid(in/out)                          - The correct BTID of the tree that contains the key.
 *  position(out)                         - The number of the partition that holds the key.
 *
 *  NOTE:
 *            - At the entry of the function, btid and class_oid should refer to the btid and oid, respectively,
 *              of the root table on which the partitions are created. This ensures the correctness of the btid,
 *              and class_oid on function exit.
 */
int
partition_prune_partition_index (PRUNING_CONTEXT * pcontext, DB_VALUE * key, OID * class_oid, BTID * btid,
				 int *position)
{
  int error = NO_ERROR;
  int pos = 0;
  DB_VALUE partition_key;
  PRUNING_BITSET pruned;
  PRUNING_BITSET_ITERATOR it;
  MATCH_STATUS status = MATCH_NOT_FOUND;

  if (pcontext == NULL)
    {
      assert_release (pcontext != NULL);
      return ER_FAILED;
    }

  if (pcontext->pruning_type == DB_PARTITION_CLASS)
    {
      /* btid is the BTID of the index corresponding to the partition. Find the BTID of the root class and use that one */
      error = partition_find_inherited_btid (pcontext->thread_p, class_oid, &pcontext->root_oid, btid, btid);

      if (error != NO_ERROR)
	{
	  return error;
	}
    }

  error = partition_attrinfo_get_key (pcontext->thread_p, pcontext, key, &pcontext->root_oid, btid, &partition_key);
  if (error != NO_ERROR)
    {
      return error;
    }

  pruningset_init (&pruned, PARTITIONS_COUNT (pcontext));
  status = partition_prune_db_val (pcontext, &partition_key, PO_EQ, &pruned);
  pr_clear_value (&partition_key);

  if (status == MATCH_NOT_FOUND)
    {
      /* This can happen only if there's no partition that can hold the key value (for example, if this is called by ON
       * DUPLICATE KEY UPDATE but the value that is being inserted will throw an error anyway) */
      return NO_ERROR;
    }
  else if (pruningset_popcount (&pruned) != 1)
    {
      /* a key value should always return at most one partition */
      assert (false);
      OID_SET_NULL (class_oid);
      return NO_ERROR;
    }

  pruningset_iterator_init (&pruned, &it);
  pos = pruningset_iterator_next (&it);

  *position = pos;
  return error;
}
