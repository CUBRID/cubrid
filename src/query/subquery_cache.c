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

#include "subquery_cache.h"

/**************************************************************************************/

/* Static functions for sq_cache hash table. */

static SQ_VAL *sq_make_val (THREAD_ENTRY * thread_p, REGU_VARIABLE * val);
static void sq_free_val (THREAD_ENTRY * thread_p, SQ_VAL * val);
static void sq_unpack_val (SQ_VAL * val, REGU_VARIABLE * retp);

static unsigned int sq_hash_func (const void *key, unsigned int ht_size);
static int sq_cmp_func (const void *key1, const void *key2);
static int sq_rem_func (const void *key, void *data, void *args);

/**************************************************************************************/

/*
 * sq_make_key () - Creates a key for the scalar subquery cache.
 *   return: Pointer to a newly allocated SQ_KEY structure, or NULL if no constant predicate is present.
 *   xasl(in): The XASL node of the scalar subquery.
 *
 * This function generates a key for caching the results of a scalar subquery. It checks the provided XASL node
 * for predicates (where_key, where_pred, where_range) and creates a DB_VALUE array to represent the key.
 */
SQ_KEY *
sq_make_key (THREAD_ENTRY * thread_p, XASL_NODE * xasl)
{
  SQ_KEY *keyp;
  int i, cnt = 0;

  keyp = (SQ_KEY *) db_private_alloc (thread_p, sizeof (SQ_KEY));
  if (keyp == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, sizeof (SQ_KEY));
      return NULL;
    }
  keyp->n_elements = SQ_CACHE_KEY_STRUCT (xasl)->n_elements;
  keyp->dbv_array = (DB_VALUE **) db_private_alloc (thread_p, keyp->n_elements * sizeof (DB_VALUE *));
  if (keyp->dbv_array == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, keyp->n_elements * sizeof (DB_VALUE *));
      return NULL;
    }
  for (i = 0; i < keyp->n_elements; i++)
    {
      keyp->dbv_array[i] = db_value_copy (SQ_CACHE_KEY_STRUCT (xasl)->dbv_array[i]);
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
  ret = (SQ_VAL *) db_private_alloc (thread_p, sizeof (SQ_VAL));

  ret->type = val->type;

  switch (ret->type)
    {
    case TYPE_CONSTANT:
      ret->val.dbvalptr = db_value_copy (val->value.dbvalptr);
      break;

    case TYPE_LIST_ID:
      /* should be implemented later. */
      break;

    default:
      /* Never happens. */
      break;
    }

  return ret;
}

/*
 * sq_free_key () - Frees the memory allocated for a SQ_KEY structure.
 *   key(in): The SQ_KEY structure to be freed.
 *
 * This function releases the memory allocated for the DB_VAUE array within the SQ_KEY structure and then
 * frees the SQ_KEY structure itself.
 */
void
sq_free_key (THREAD_ENTRY * thread_p, SQ_KEY * key)
{
  int i;
  for (i = 0; i < key->n_elements; i++)
    {
      pr_free_ext_value (key->dbv_array[i]);
    }
  db_private_free_and_init (thread_p, key->dbv_array);
  db_private_free_and_init (thread_p, key);
}

/*
 * sq_free_val () - Frees the memory allocated for a SQ_VAL structure.
 *   v(in): The SQ_VAL structure to be freed.
 *
 * Depending on the type of the value in the SQ_VAL structure (e.g., TYPE_CONSTANT, TYPE_LIST_ID),
 * this function frees the associated resources and then the SQ_VAL structure itself.
 */
void
sq_free_val (THREAD_ENTRY * thread_p, SQ_VAL * v)
{
  switch (v->type)
    {
    case TYPE_CONSTANT:
      pr_free_ext_value (v->val.dbvalptr);
      break;

    case TYPE_LIST_ID:
      /* should be implemented later.. */
      break;

    default:
      /* Never happens */
      break;
    }
  db_private_free_and_init (thread_p, v);
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
  switch (v->type)
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
	  retp->value.srlist_id->list_id = qfile_clone_list_id (v->val.srlist_id->list_id, true);
	}
      break;

    default:
      /* Never happens */
      break;
    }
}

/*
 * sq_hash_func () - Hash function for the scalar subquery cache keys.
 *   return: The hash value.
 *   key(in): The key to be hashed.
 *   ht_size(in): The size of the hash table.
 *   
 * Generates a hash value for the given key by hashing the elements of the DB_VALUE array within the SQ_KEY structure.
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
 * of the DB_VALUE array within each key. The function returns 1 if the keys are considered equal, otherwise 0.
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
  assert (sz1 == sz2);

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
  THREAD_ENTRY *thread_p = (THREAD_ENTRY *) args;
  sq_free_key (thread_p, (SQ_KEY *) key);
  sq_free_val (thread_p, (SQ_VAL *) data);
  return NO_ERROR;
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
sq_cache_initialize (XASL_NODE * xasl)
{
  UINT64 max_subquery_cache_size = (UINT64) prm_get_bigint_value (PRM_ID_MAX_SUBQUERY_CACHE_SIZE);
  int sq_hm_entries = (int) max_subquery_cache_size / SQ_CACHE_EXPECTED_ENTRY_SIZE;	// default 4096 (4K)
  int actual_entries;

  SQ_CACHE_HT (xasl) = mht_create ("sq_cache", sq_hm_entries, sq_hash_func, sq_cmp_func);
  if (!SQ_CACHE_HT (xasl))
    {
      return ER_FAILED;
    }
  SQ_CACHE_ENABLED (xasl) = true;
  SQ_CACHE_SIZE_MAX (xasl) = max_subquery_cache_size;
  SQ_CACHE_SIZE (xasl) += DB_SIZEOF (SQ_CACHE);
  SQ_CACHE_SIZE (xasl) += DB_SIZEOF (MHT_TABLE);
  actual_entries = mht_calculate_htsize ((unsigned int) sq_hm_entries);
  SQ_CACHE_SIZE (xasl) += (DB_SIZEOF (HENTRY) * MAX (2, actual_entries / 2 + 1));
  SQ_CACHE_SIZE (xasl) += (DB_SIZEOF (HENTRY_PTR) * actual_entries);
  SQ_CACHE_SIZE (xasl) += sizeof (SQ_KEY);
  SQ_CACHE_SIZE (xasl) += DB_SIZEOF (DB_VALUE *) * SQ_CACHE_KEY_STRUCT (xasl)->n_elements;

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
  int i;

  val = sq_make_val (thread_p, regu_var);

  if (!SQ_CACHE_HT (xasl))
    {
      if (sq_cache_initialize (xasl) == ER_FAILED)
	{
	  XASL_CLEAR_FLAG (xasl, XASL_USES_SQ_CACHE);
	  return ER_FAILED;
	}
    }

  new_entry_size += DB_SIZEOF (HENTRY);

  for (i = 0; i < key->n_elements; i++)
    {
      new_entry_size += (UINT64) or_db_value_size (key->dbv_array[i]);
    }
  new_entry_size += sizeof (SQ_KEY);
  new_entry_size += DB_SIZEOF (DB_VALUE *) * key->n_elements;

  switch (val->type)
    {
    case TYPE_CONSTANT:
      new_entry_size += (UINT64) or_db_value_size (val->val.dbvalptr) + sizeof (SQ_VAL);
      break;

    default:
      break;
    }

  if (SQ_CACHE_SIZE_MAX (xasl) < SQ_CACHE_SIZE (xasl) + new_entry_size)
    {
      SQ_CACHE_ENABLED (xasl) = false;
      sq_free_val (thread_p, val);
      return ER_FAILED;
    }

  ret = mht_put_if_not_exists (SQ_CACHE_HT (xasl), key, val);

  if (!ret || ret != val)
    {
      sq_free_val (thread_p, val);
      return ER_FAILED;
    }
  SQ_CACHE_SIZE (xasl) += new_entry_size;
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

  if (SQ_CACHE_HT (xasl))
    {
      /* This conditional check acts as a mechanism to prevent the cache from being 
         overwhelmed by unsuccessful lookups. If the cache miss count exceeds a predefined 
         maximum, it evaluates the hit-to-miss ratio to decide whether continuing caching 
         is beneficial. This approach optimizes cache usage and performance by dynamically 
         adapting to the effectiveness of the cache. */
      UINT64 sq_cache_miss_max = SQ_CACHE_SIZE_MAX (xasl) / SQ_CACHE_EXPECTED_ENTRY_SIZE;
      if (SQ_CACHE_MISS (xasl) >= (int) sq_cache_miss_max)
	{
	  if (SQ_CACHE_HIT (xasl) / SQ_CACHE_MISS (xasl) < SQ_CACHE_MIN_HIT_RATIO)
	    {
	      SQ_CACHE_ENABLED (xasl) = false;
	      return false;
	    }
	}
    }

  if (!SQ_CACHE_HT (xasl))
    {
      if (sq_cache_initialize (xasl) == ER_FAILED)
	{
	  XASL_CLEAR_FLAG (xasl, XASL_USES_SQ_CACHE);
	  return false;
	}
      SQ_CACHE_MISS (xasl)++;
      return false;
    }

  ret = (SQ_VAL *) mht_get (SQ_CACHE_HT (xasl), key);
  if (ret == NULL)
    {
      SQ_CACHE_MISS (xasl)++;
      return false;
    }

  sq_unpack_val (ret, regu_var);

  SQ_CACHE_HIT (xasl)++;
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
sq_cache_destroy (THREAD_ENTRY * thread_p, SQ_CACHE * sq_cache)
{
  if (sq_cache)
    {
      if (sq_cache->ht)
	{
	  er_log_debug (ARG_FILE_LINE,
			"destroy sq_cache  %p\ncache info : \n\thit : %10d\n\tmiss: %10d\n\tsize: %10lu Bytes\n",
			sq_cache, sq_cache->stats.hit, sq_cache->stats.miss, sq_cache->size);
	  mht_clear (sq_cache->ht, sq_rem_func, (void *) thread_p);
	  mht_destroy (sq_cache->ht);
	  sq_cache->ht = NULL;
	}
      sq_cache->size_max = 0;
      sq_cache->size = 0;
      sq_cache->enabled = false;
      sq_cache->stats.hit = 0;
      sq_cache->stats.miss = 0;
    }
}
