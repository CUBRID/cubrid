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
 * subquery_cache.c - Correlated Scalar Subquery Result Cache.
 */


#ident "$Id$"

#include <stdio.h>
#include <string.h>

#include "xasl.h"
#include "dbtype.h"
#include "query_executor.h"
#include "xasl_predicate.hpp"
#include "regu_var.hpp"
#include "object_representation.h"
#include "system_parameter.h"
#include "memory_alloc.h"

#include "list_file.h"
#include "db_set_function.h"

#include "subquery_cache.h"

/**************************************************************************************/

/* Static functions for sq_cache hash table. */

static SQ_VAL *sq_make_val (THREAD_ENTRY * thread_p, REGU_VARIABLE * val);
static void sq_free_val (SQ_VAL * val);
static void sq_unpack_val (SQ_VAL * val, REGU_VARIABLE * retp);

static unsigned int sq_hash_func (const void *key, unsigned int ht_size);
static int sq_cmp_func (const void *key1, const void *key2);
static int sq_rem_func (const void *key, void *data, void *args);

static int sq_walk_xasl_check_not_caching (THREAD_ENTRY * thread_p, XASL_NODE * xasl);
//static int sq_walk_xasl_and_add_val_to_set (THREAD_ENTRY * thread_p, void *p, int type, SQ_KEY * pred_set);

/**************************************************************************************/

/*
 * sq_make_key () - Creates a key for the scalar subquery cache.
 *   return: Pointer to a newly allocated SQ_KEY structure, or NULL if no constant predicate is present.
 *   xasl(in): The XASL node of the scalar subquery.
 *
 * This function generates a key for caching the results of a scalar subquery. It checks the provided XASL node
 * for predicates (where_key, where_pred, where_range) and creates a set (pred_set) to represent the key.
 * If no predicates are found or the set cannot be populated, NULL is returned. Otherwise, a pointer to the
 * newly created SQ_KEY structure is returned.
 */
SQ_KEY *
sq_make_key (THREAD_ENTRY * thread_p, XASL_NODE * xasl)
{
  SQ_KEY *keyp;
  int i, cnt = 0;
/*
  keyp = (SQ_KEY *) db_private_alloc (NULL, sizeof (SQ_KEY));
  keyp->pred_set = db_value_create ();
  db_make_set (keyp->pred_set, db_set_create_basic (NULL, NULL));

  cnt = sq_walk_xasl_and_add_val_to_set (thread_p, (void *) xasl, SQ_TYPE_XASL, keyp);

  if (cnt == 0)
    {
      sq_free_key (keyp);
      return NULL;
    }*/
  keyp = (SQ_KEY *) db_private_alloc (NULL, sizeof (SQ_KEY));
  keyp->n_elements = xasl->sq_cache->n_elements;
  keyp->dbv_array = (DB_VALUE **) db_private_alloc (NULL, keyp->n_elements * sizeof (DB_VALUE *));
  for (i = 0; i < keyp->n_elements; i++)
    {
      keyp->dbv_array[i] = db_value_copy (xasl->sq_cache->sq_key_struct[i]);
    }

  return keyp;
}

/*
 * sq_make_val () - Creates a value structure for the scalar subquery cache.
 *   return: Pointer to a newly created SQ_VAL structure.
 *   val(in): The REGU_VARIABLE for which to create the SQ_VAL structure.
 *
 * Allocates and initializes a new SQ_VAL structure based on the given REGU_VARIABLE. The function handles
 * different types of REGU_VARIABLE (e.g., TYPE_CONSTANT, TYPE_LIST_ID) appropriately by copying or cloning
 * the necessary data. It returns a pointer to the newly allocated and initialized SQ_VAL structure.
 */
SQ_VAL *
sq_make_val (THREAD_ENTRY * thread_p, REGU_VARIABLE * val)
{
  SQ_VAL *ret;
  ret = (SQ_VAL *) db_private_alloc (NULL, sizeof (SQ_VAL));

  ret->t = val->type;

  switch (ret->t)
    {
    case TYPE_CONSTANT:
      ret->val.dbvalptr = db_value_copy (val->value.dbvalptr);
      break;

    case TYPE_LIST_ID:
      /* should be implemented later. */
      break;

    default:
      assert (0);
      break;
    }

  return ret;
}

/*
 * sq_free_key () - Frees the memory allocated for a SQ_KEY structure.
 *   key(in): The SQ_KEY structure to be freed.
 *
 * This function releases the memory allocated for the pred_set within the SQ_KEY structure and then
 * frees the SQ_KEY structure itself.
 */
void
sq_free_key (SQ_KEY * key)
{
  int i;
  /*
     db_value_clear (key->pred_set);
     pr_free_ext_value (key->pred_set);
   */
  for (i = 0; i < key->n_elements; i++)
    {
      pr_free_ext_value (key->dbv_array[i]);
    }
  db_private_free_and_init (NULL, key->dbv_array);
  db_private_free_and_init (NULL, key);
}

/*
 * sq_free_val () - Frees the memory allocated for a SQ_VAL structure.
 *   v(in): The SQ_VAL structure to be freed.
 *
 * Depending on the type of the value in the SQ_VAL structure (e.g., TYPE_CONSTANT, TYPE_LIST_ID),
 * this function frees the associated resources and then the SQ_VAL structure itself.
 */
void
sq_free_val (SQ_VAL * v)
{
  switch (v->t)
    {
    case TYPE_CONSTANT:
      pr_free_ext_value (v->val.dbvalptr);
      break;

    case TYPE_LIST_ID:
      /* should be implemented later.. */
      break;

    default:
      assert (0);
      break;
    }
  db_private_free_and_init (NULL, v);
}

/*
 * sq_unpack_val () - Unpacks the value from a SQ_VAL structure into a REGU_VARIABLE.
 *   v(in): The SQ_VAL structure containing the value to be unpacked.
 *   retp(out): The REGU_VARIABLE to store the unpacked value.
 *
 * Based on the type of the value in the SQ_VAL structure, this function unpacks the value and stores
 * it in the provided REGU_VARIABLE. The function handles different types appropriately, such as copying
 * DB_VALUE or cloning a LIST_ID.
 */
void
sq_unpack_val (SQ_VAL * v, REGU_VARIABLE * retp)
{
  switch (v->t)
    {
    case TYPE_CONSTANT:
      if (retp->value.dbvalptr)
	{
	  pr_clear_value (retp->value.dbvalptr);
	  db_value_clone (v->val.dbvalptr, retp->value.dbvalptr);
	}
      else
	{
	  retp->value.dbvalptr = db_value_copy (v->val.dbvalptr);
	}
      break;

    case TYPE_LIST_ID:

      if (retp->value.srlist_id)
	{
	  qfile_copy_list_id (retp->value.srlist_id->list_id, v->val.srlist_id->list_id, true);
	  retp->value.srlist_id->sorted = v->val.srlist_id->sorted;
	}
      else
	{
	  assert (0);
	  retp->value.srlist_id->list_id = qfile_clone_list_id (v->val.srlist_id->list_id, true);
	}
      break;

    default:
      break;
    }
}

/*
 * sq_hash_func () - Hash function for the scalar subquery cache keys.
 *   return: The hash value.
 *   key(in): The key to be hashed.
 *   ht_size(in): The size of the hash table.
 *   
 * Generates a hash value for the given key by hashing the elements of the pred_set within the SQ_KEY structure.
 * The hash value is then modulated by the size of the hash table to ensure it falls within valid bounds.
 */
unsigned int
sq_hash_func (const void *key, unsigned int ht_size)
{
  SQ_KEY *k = (SQ_KEY *) key;
  int i;
  unsigned int h = 0;

  for (i = 0; i < k->n_elements; i++)
    {
      h ^= mht_valhash (k->dbv_array[i], ht_size);
    }
  return h % ht_size;
}

/*
 * sq_cmp_func () - Comparison function for scalar subquery cache keys.
 *   return: 1 if the keys are equal, 0 otherwise.
 *   key1(in): The first key to compare.
 *   key2(in): The second key to compare.
 *
 * Compares two SQ_KEY structures to determine if they are equal. The comparison is based on the elements
 * of the pred_set within each key. The function returns 1 if the keys are considered equal, otherwise 0.
 */
int
sq_cmp_func (const void *key1, const void *key2)
{
  SQ_KEY *k1, *k2;
  int i, sz1, sz2;
  k1 = (SQ_KEY *) key1;
  k2 = (SQ_KEY *) key2;
  sz1 = k1->n_elements;
  sz2 = k2->n_elements;

  if (sz1 != sz2)
    {
      return 0;
    }

  for (i = 0; i < sz1; i++)
    {
      if (!mht_compare_dbvalues_are_equal (k1->dbv_array[i], k2->dbv_array[i]))
	{

	  return 0;
	}
    }
  return 1;

}

/*
 * sq_rem_func () - Function to remove an entry from the scalar subquery cache.
 *   return: NO_ERROR on success.
 *   key(in): The key of the entry to remove.
 *   data(in): The data associated with the key.
 *   args(in): Additional arguments (unused).
 *
 * This function is called when an entry is removed from the scalar subquery cache. It frees the resources
 * allocated for the key and the data (SQ_VAL structure) using sq_free_key and sq_free_val functions.
 */
int
sq_rem_func (const void *key, void *data, void *args)
{
  sq_free_key ((SQ_KEY *) key);
  sq_free_val ((SQ_VAL *) data);
  return NO_ERROR;
}

/*
 * sq_walk_xasl_check_not_caching () - Checks if a XASL should not be cached.
 *   return: True if the XASL should not be cached, False otherwise.
 *   xasl(in): The XASL node to check.
 *
 * Recursively checks a XASL node and its children to determine if any conditions exist that would prevent
 * caching its results. Conditions include the presence of if_pred, after_join_pred, or dptr_list in the XASL node.
 */
int
sq_walk_xasl_check_not_caching (THREAD_ENTRY * thread_p, XASL_NODE * xasl)
{
  XASL_NODE *scan_ptr, *aptr;
  int ret;
  SQ_KEY *key;

  if (!xasl)
    {
      return true;
    }
  else if (!xasl->spec_list)
    {
      return true;
    }
  else if (!xasl->spec_list->where_key && !xasl->spec_list->where_pred && !xasl->spec_list->where_range)
    {
      return true;
    }

  if (xasl->if_pred || xasl->after_join_pred || xasl->dptr_list)
    {
      return true;
    }

  key = sq_make_key (thread_p, xasl);

  if (key == NULL)
    {
      return true;
    }

  sq_free_key (key);

  ret = 0;
  if (xasl->scan_ptr)
    {
      for (scan_ptr = xasl->scan_ptr; scan_ptr != NULL; scan_ptr = scan_ptr->next)
	{
	  ret |= sq_walk_xasl_check_not_caching (thread_p, scan_ptr);
	}
    }
  if (xasl->aptr_list)
    {
      for (aptr = xasl->aptr_list; aptr != NULL; aptr = aptr->next)
	{
	  ret |= sq_walk_xasl_check_not_caching (thread_p, aptr);
	}
    }

  return ret;
}

/*
 * sq_cache_initialize () - Initializes the cache for a given XASL node.
 *   return: NO_ERROR if successful, ER_FAILED otherwise.
 *   xasl(in/out): The XASL node for which the cache is being initialized.
 *
 * This function creates a hash table for caching the results of the XASL node. It sets up initial values for cache hit and miss
 * counters and marks the cache as initialized. The function returns NO_ERROR upon successful initialization, or ER_FAILED if the
 * hash table could not be created.
 */
int
sq_cache_initialize (THREAD_ENTRY * thread_p, XASL_NODE * xasl)
{
  UINT64 max_subquery_cache_size = (UINT64) prm_get_bigint_value (PRM_ID_MAX_SUBQUERY_CACHE_SIZE);
  int sq_hm_entries = (int) max_subquery_cache_size / 2048;	// default 1024

  xasl->sq_cache->ht = mht_create ("sq_cache", sq_hm_entries, sq_hash_func, sq_cmp_func);
  if (!xasl->sq_cache->ht)
    {
      return ER_FAILED;
    }

  xasl->sq_cache->size_max = max_subquery_cache_size;
  return NO_ERROR;
}

/*
 * sq_put () - Puts a value into the cache for a given XASL node.
 *   return: NO_ERROR if the value is successfully cached, ER_FAILED otherwise.
 *   xasl(in): The XASL node for which the value is being cached.
 *   regu_var(in): The regu variable containing the value to be cached.
 *
 * This function attempts to cache the result of a regu variable associated with a given XASL node. It generates a key based on
 * the XASL node's structure and creates a cache entry if such a key does not already exist in the cache. The function returns
 * NO_ERROR if the value is successfully cached, or ER_FAILED if the key could not be generated or the value could not be added
 * to the cache.
 */
int
sq_put (THREAD_ENTRY * thread_p, SQ_KEY * key, XASL_NODE * xasl, REGU_VARIABLE * regu_var)
{
  SQ_VAL *val;
  const void *ret;
  UINT64 new_entry_size = 0;

  val = sq_make_val (thread_p, regu_var);

  if (!xasl->sq_cache->ht)
    {
      sq_cache_initialize (thread_p, xasl);
    }

  new_entry_size += (UINT64) (key->n_elements) * or_db_value_size (key->dbv_array[0]) + sizeof (SQ_KEY);

  switch (val->t)
    {
    case TYPE_CONSTANT:
      new_entry_size += (UINT64) or_db_value_size (val->val.dbvalptr) + sizeof (SQ_VAL);
      break;

    default:
      break;
    }

  if (xasl->sq_cache->size_max < xasl->sq_cache->size + new_entry_size)
    {
      XASL_CLEAR_FLAG (xasl, XASL_SQ_CACHE);
      sq_free_val (val);
      return ER_FAILED;
    }

  ret = mht_put_if_not_exists (xasl->sq_cache->ht, key, val);

  if (!ret || ret != val)
    {
      sq_free_val (val);
      return ER_FAILED;
    }
  xasl->sq_cache->size += new_entry_size;
  return NO_ERROR;
}

/*
 * sq_get () - Retrieves a value from the cache for a given XASL node.
 *   return: True if a cached value is found and retrieved, False otherwise.
 *   xasl(in): The XASL node for which a cached value is being retrieved.
 *   regu_var(in/out): The regu variable where the retrieved value will be stored.
 *
 * This function attempts to retrieve a value from the cache for a given XASL node. It generates a key based on the XASL node's
 * structure and looks up the cache for a matching value. If a cached value is found, it is unpacked into the specified regu
 * variable, and the function returns True. Otherwise, the function updates cache miss counters and returns False.
 */
bool
sq_get (THREAD_ENTRY * thread_p, SQ_KEY * key, XASL_NODE * xasl, REGU_VARIABLE * regu_var)
{
  SQ_VAL *ret;

  if (XASL_IS_FLAGED (xasl, XASL_SQ_CACHE) && xasl->sq_cache->ht)
    {
      /* This conditional check acts as a mechanism to prevent the cache from being 
         overwhelmed by unsuccessful lookups. If the cache miss count exceeds a predefined 
         maximum, it evaluates the hit-to-miss ratio to decide whether continuing caching 
         is beneficial. This approach optimizes cache usage and performance by dynamically 
         adapting to the effectiveness of the cache. */
      UINT64 sq_cache_miss_max = xasl->sq_cache->size_max / 2048;
      if (xasl->sq_cache->stats.miss >= (int) sq_cache_miss_max)
	{
	  if (xasl->sq_cache->stats.hit / xasl->sq_cache->stats.miss < SQ_CACHE_MIN_HIT_RATIO)
	    {
	      XASL_CLEAR_FLAG (xasl, XASL_SQ_CACHE);
	      return false;
	    }
	}
    }

  if (!xasl->sq_cache->ht)
    {
      sq_cache_initialize (thread_p, xasl);
      xasl->sq_cache->stats.miss++;
      return false;
    }

  ret = (SQ_VAL *) mht_get (xasl->sq_cache->ht, key);
  if (ret == NULL)
    {
      xasl->sq_cache->stats.miss++;
      return false;
    }

  sq_unpack_val (ret, regu_var);

  xasl->sq_cache->stats.hit++;
  return true;
}

/*
 * sq_cache_destroy () - Destroys the cache for a given XASL node.
 *   xasl(in): The XASL node for which the cache is being destroyed.
 *
 * This function destroys the cache associated with a given XASL node. It clears all cache entries and then destroys the hash
 * table itself. It also resets cache-related flags and counters for the XASL node. This function is called when a XASL node is
 * no longer needed or before it is deallocated.
 */
void
sq_cache_destroy (XASL_NODE * xasl)
{
  int i;
  if (xasl->sq_cache)
    {
      for (i = 0; i < xasl->sq_cache->n_elements; i++)
	{
	  pr_clear_value (xasl->sq_cache->sq_key_struct[i]);
	}
      xasl->sq_cache->n_elements = 0;
      if (xasl->sq_cache->ht)
	{
	  er_log_debug (ARG_FILE_LINE,
			"destroy sq_cache at xasl %p\ncache info : \n\thit : %10d\n\tmiss: %10d\n\tsize: %10lu Bytes\n",
			xasl, xasl->sq_cache->stats.hit, xasl->sq_cache->stats.miss, xasl->sq_cache->size);
	  mht_clear (xasl->sq_cache->ht, sq_rem_func, NULL);
	  mht_destroy (xasl->sq_cache->ht);
	  xasl->sq_cache->ht = NULL;
	}
    }
  XASL_CLEAR_FLAG (xasl, XASL_SQ_CACHE);
}
