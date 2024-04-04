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

#include "list_file.h"
#include "db_set_function.h"

#include "subquery_cache.h"

typedef union _sq_regu_value
{
  /* fields used by both XASL interpreter and regulator */
  DB_VALUE *dbvalptr;		/* for constant values */
  QFILE_SORTED_LIST_ID *srlist_id;	/* sorted list identifier for subquery results */
} sq_regu_value;

typedef struct _sq_key
{
  DB_VALUE *pred_set;
} sq_key;

typedef struct _sq_val
{
  sq_regu_value val;
  REGU_DATATYPE t;
} sq_val;

/**************************************************************************************/

/* Static functions for sq_cache hash table. */

static sq_key *sq_make_key (xasl_node * xasl);
static sq_val *sq_make_val (REGU_VARIABLE * val);
static void sq_free_key (sq_key * key);
static void sq_free_val (sq_val * val);
static void sq_unpack_val (sq_val * val, REGU_VARIABLE * retp);

static unsigned int sq_hash_func (const void *key, unsigned int ht_size);
static int sq_cmp_func (const void *key1, const void *key2);
static int sq_rem_func (const void *key, void *data, void *args);

static int sq_walk_xasl_check_not_caching (xasl_node * xasl);
static int sq_walk_xasl_and_add_val_to_set (void *p, int type, sq_key * pred_set);

/**************************************************************************************/

/*
 * sq_make_key () - Creates a key for the scalar subquery cache.
 *   return: Pointer to a newly allocated sq_key structure, or NULL if no constant predicate is present.
 *   xasl(in): The XASL node of the scalar subquery.
 *
 * This function generates a key for caching the results of a scalar subquery. It checks the provided XASL node
 * for predicates (where_key, where_pred, where_range) and creates a set (pred_set) to represent the key.
 * If no predicates are found or the set cannot be populated, NULL is returned. Otherwise, a pointer to the
 * newly created sq_key structure is returned.
 */
sq_key *
sq_make_key (xasl_node * xasl)
{
  sq_key *keyp;
  int cnt = 0;
  ACCESS_SPEC_TYPE *p;

  p = xasl->spec_list;

  if (p && !p->where_key && !p->where_pred && !p->where_range)
    {
      /* if there's no predicate in xasl, not caching */
      return NULL;
    }

  keyp = (sq_key *) malloc (sizeof (sq_key));
  keyp->pred_set = db_value_create ();
  db_make_set (keyp->pred_set, db_set_create_basic (NULL, NULL));

  cnt = sq_walk_xasl_and_add_val_to_set ((void *) xasl, SQ_TYPE_XASL, keyp);

  if (cnt == 0)
    {
      sq_free_key (keyp);
      return NULL;
    }

  return keyp;
}

/*
 * sq_make_val () - Creates a value structure for the scalar subquery cache.
 *   return: Pointer to a newly created sq_val structure.
 *   val(in): The REGU_VARIABLE for which to create the sq_val structure.
 *
 * Allocates and initializes a new sq_val structure based on the given REGU_VARIABLE. The function handles
 * different types of REGU_VARIABLE (e.g., TYPE_CONSTANT, TYPE_LIST_ID) appropriately by copying or cloning
 * the necessary data. It returns a pointer to the newly allocated and initialized sq_val structure.
 */
sq_val *
sq_make_val (REGU_VARIABLE * val)
{
  sq_val *ret;
  ret = (sq_val *) malloc (sizeof (sq_val));

  ret->t = val->type;

  switch (ret->t)
    {
    case TYPE_CONSTANT:
      ret->val.dbvalptr = db_value_copy (val->value.dbvalptr);
      break;

    case TYPE_LIST_ID:
      ret->val.srlist_id = (QFILE_SORTED_LIST_ID *) malloc (sizeof (QFILE_SORTED_LIST_ID));
      ret->val.srlist_id->list_id = qfile_clone_list_id (val->value.srlist_id->list_id, true);
      ret->val.srlist_id->sorted = val->value.srlist_id->sorted;
      break;

    default:
      assert (0);
      break;
    }

  return ret;
}

/*
 * sq_free_key () - Frees the memory allocated for a sq_key structure.
 *   key(in): The sq_key structure to be freed.
 *
 * This function releases the memory allocated for the pred_set within the sq_key structure and then
 * frees the sq_key structure itself.
 */
void
sq_free_key (sq_key * key)
{
  db_value_clear (key->pred_set);
  pr_free_ext_value (key->pred_set);

  free (key);
}

/*
 * sq_free_val () - Frees the memory allocated for a sq_val structure.
 *   v(in): The sq_val structure to be freed.
 *
 * Depending on the type of the value in the sq_val structure (e.g., TYPE_CONSTANT, TYPE_LIST_ID),
 * this function frees the associated resources and then the sq_val structure itself.
 */
void
sq_free_val (sq_val * v)
{
  switch (v->t)
    {
    case TYPE_CONSTANT:
      pr_free_ext_value (v->val.dbvalptr);
      break;

    case TYPE_LIST_ID:
      QFILE_FREE_AND_INIT_LIST_ID (v->val.srlist_id->list_id);
      free (v->val.srlist_id);
      break;

    default:
      assert (0);
      break;
    }
  free (v);
}

/*
 * sq_unpack_val () - Unpacks the value from a sq_val structure into a REGU_VARIABLE.
 *   v(in): The sq_val structure containing the value to be unpacked.
 *   retp(out): The REGU_VARIABLE to store the unpacked value.
 *
 * Based on the type of the value in the sq_val structure, this function unpacks the value and stores
 * it in the provided REGU_VARIABLE. The function handles different types appropriately, such as copying
 * DB_VALUE or cloning a LIST_ID.
 */
void
sq_unpack_val (sq_val * v, REGU_VARIABLE * retp)
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
 * Generates a hash value for the given key by hashing the elements of the pred_set within the sq_key structure.
 * The hash value is then modulated by the size of the hash table to ensure it falls within valid bounds.
 */
unsigned int
sq_hash_func (const void *key, unsigned int ht_size)
{
  sq_key *k = (sq_key *) key;
  int i;
  unsigned int h = 0;
  DB_SET *set = db_get_set (k->pred_set);
  DB_VALUE v;

  for (i = 0; i < db_set_size (set); i++)
    {
      db_set_get (set, i, &v);
      h += mht_valhash (&v, ht_size);
      pr_clear_value (&v);
    }
  return h % ht_size;
}

/*
 * sq_cmp_func () - Comparison function for scalar subquery cache keys.
 *   return: 1 if the keys are equal, 0 otherwise.
 *   key1(in): The first key to compare.
 *   key2(in): The second key to compare.
 *
 * Compares two sq_key structures to determine if they are equal. The comparison is based on the elements
 * of the pred_set within each key. The function returns 1 if the keys are considered equal, otherwise 0.
 */
int
sq_cmp_func (const void *key1, const void *key2)
{
  sq_key *k1, *k2;
  int i, sz1, sz2;
  DB_SET *set1, *set2;
  DB_VALUE v1, v2;

  k1 = (sq_key *) key1;
  k2 = (sq_key *) key2;
  set1 = db_get_set (k1->pred_set);
  set2 = db_get_set (k2->pred_set);
  sz1 = db_set_size (set1);
  sz2 = db_set_size (set2);

  if (sz1 != sz2)
    {
      return 0;
    }

  for (i = 0; i < sz1; i++)
    {
      db_set_get (set1, i, &v1);
      db_set_get (set2, i, &v2);
      if (!mht_compare_dbvalues_are_equal (&v1, &v2))
	{
	  pr_clear_value (&v1);
	  pr_clear_value (&v2);
	  return 0;
	}
      pr_clear_value (&v1);
      pr_clear_value (&v2);
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
 * allocated for the key and the data (sq_val structure) using sq_free_key and sq_free_val functions.
 */
int
sq_rem_func (const void *key, void *data, void *args)
{
  sq_free_key ((sq_key *) key);
  sq_free_val ((sq_val *) data);
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
sq_walk_xasl_check_not_caching (xasl_node * xasl)
{
  xasl_node *scan_ptr, *aptr;
  int ret;
  sq_key *key;

  if (xasl->if_pred || xasl->after_join_pred || xasl->dptr_list)
    {
      return true;
    }

  key = sq_make_key (xasl);

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
	  ret |= sq_walk_xasl_check_not_caching (scan_ptr);
	}
    }
  if (xasl->aptr_list)
    {
      for (aptr = xasl->aptr_list; aptr != NULL; aptr = aptr->next)
	{
	  ret |= sq_walk_xasl_check_not_caching (aptr);
	}
    }

  return ret;
}

/*
 * sq_walk_xasl_and_add_val_to_set () - Recursively walks through a XASL tree and adds values to a DB_VALUE set.
 *   return: The count of values added to the set.
 *   p(in): Pointer to the current component (XASL node, predicate expression, or regu variable, or DB_VALUE) being processed.
 *   type(in): The type of the component being processed, indicating whether it's a XASL node, predicate expression, regu variable, or a DB_VALUE.
 *   pred_set(in/out): The DB_VALUE set to which values are being added.
 *
 * This function recursively processes a XASL tree, including its access spec list, predicate expressions, and regu variables.
 * For each node or expression that contains a constant value or a DB value, that value is added to the specified DB_VALUE set.
 * The function uses the type parameter to determine the appropriate processing method for the current component.
 * The count of values added to the set is returned.
 */
int
sq_walk_xasl_and_add_val_to_set (void *p, int type, sq_key * key)
{
  int cnt = 0;

  if (!p)
    {
      return 0;
    }

  switch (type)
    {
    case SQ_TYPE_XASL:
      if (1)
	{
	  xasl_node *xasl = (xasl_node *) p;
	  xasl_node *scan_ptr, *aptr;
	  ACCESS_SPEC_TYPE *p;

	  p = xasl->spec_list;
	  for (p = xasl->spec_list; p; p = p->next)
	    {
	      /* key */
	      cnt += sq_walk_xasl_and_add_val_to_set (p->where_key, SQ_TYPE_PRED, key);
	      /* pred */
	      cnt += sq_walk_xasl_and_add_val_to_set (p->where_pred, SQ_TYPE_PRED, key);
	      /* range */
	      cnt += sq_walk_xasl_and_add_val_to_set (p->where_range, SQ_TYPE_PRED, key);
	    }

	  if (xasl->scan_ptr)
	    {
	      for (scan_ptr = xasl->scan_ptr; scan_ptr != NULL; scan_ptr = scan_ptr->next)
		{
		  cnt += sq_walk_xasl_and_add_val_to_set (scan_ptr, SQ_TYPE_XASL, key);
		}
	    }

	  if (xasl->aptr_list)
	    {
	      for (aptr = xasl->aptr_list; aptr != NULL; aptr = aptr->next)
		{
		  cnt += sq_walk_xasl_and_add_val_to_set (aptr, SQ_TYPE_XASL, key);
		}
	    }

	}
      break;

    case SQ_TYPE_PRED:
      if (1)
	{
	  PRED_EXPR *src = (PRED_EXPR *) p;
	  if (src->type == T_PRED)
	    {
	      cnt += sq_walk_xasl_and_add_val_to_set (src->pe.m_pred.lhs, SQ_TYPE_PRED, key);
	      cnt += sq_walk_xasl_and_add_val_to_set (src->pe.m_pred.rhs, SQ_TYPE_PRED, key);
	    }
	  else if (src->type == T_EVAL_TERM)
	    {
	      COMP_EVAL_TERM t = src->pe.m_eval_term.et.et_comp;
	      cnt += sq_walk_xasl_and_add_val_to_set (t.lhs, SQ_TYPE_REGU_VAR, key);
	      cnt += sq_walk_xasl_and_add_val_to_set (t.rhs, SQ_TYPE_REGU_VAR, key);
	    }
	}
      break;

    case SQ_TYPE_REGU_VAR:
      if (1)
	{
	  REGU_VARIABLE *src = (REGU_VARIABLE *) p;
	  if (src->type == TYPE_CONSTANT)
	    {
	      cnt += sq_walk_xasl_and_add_val_to_set (src->value.dbvalptr, SQ_TYPE_DBVAL, key);
	    }
	  if (src->type == TYPE_DBVAL)
	    {
	      cnt += sq_walk_xasl_and_add_val_to_set (&src->value.dbval, SQ_TYPE_DBVAL, key);
	    }
	  else if (src->type == TYPE_INARITH)
	    {
	      cnt += sq_walk_xasl_and_add_val_to_set (src->value.arithptr->leftptr, SQ_TYPE_REGU_VAR, key);
	      cnt += sq_walk_xasl_and_add_val_to_set (src->value.arithptr->rightptr, SQ_TYPE_REGU_VAR, key);
	    }
	}

      break;

    case SQ_TYPE_DBVAL:
      if (1)
	{
	  db_set_add (db_get_set (key->pred_set), (DB_VALUE *) p);
	  cnt++;
	}

      break;

    default:
      assert (0);
      break;
    }

  return cnt;
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
sq_cache_initialize (xasl_node * xasl)
{
  size_t max_subquery_cache_size = (size_t) prm_get_bigint_value (PRM_ID_MAX_SUBQUERY_CACHE_SIZE);
  int sq_hm_entries = (int) max_subquery_cache_size / 2048;	// default 1024
  xasl->sq_cache_ht = mht_create ("sq_cache", sq_hm_entries, sq_hash_func, sq_cmp_func);
  if (!xasl->sq_cache_ht)
    {
      return ER_FAILED;
    }
  xasl->sq_cache_hit = (size_t) 0;
  xasl->sq_cache_miss = (size_t) 0;
  xasl->sq_cache_size = (size_t) 0;
  xasl->sq_cache_size_max = max_subquery_cache_size;
  xasl->sq_cache_miss_max = max_subquery_cache_size / 2048;	// default 1024
  xasl->sq_cache_flag |= SQ_CACHE_INITIALIZED_FLAG;
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
sq_put (xasl_node * xasl, REGU_VARIABLE * regu_var)
{
  sq_key *key;
  sq_val *val;
  const void *ret;
  size_t new_entry_size = 0;

  key = sq_make_key (xasl);

  if (key == NULL)
    {
      regu_var->xasl->sq_cache_flag |= SQ_CACHE_NOT_CACHING_FLAG;
      return ER_FAILED;
    }

  val = sq_make_val (regu_var);

  if (!(xasl->sq_cache_flag & SQ_CACHE_INITIALIZED_FLAG))
    {
      sq_cache_initialize (xasl);
    }

  new_entry_size += (size_t) or_db_value_size (key->pred_set) + sizeof (sq_key);

  switch (val->t)
    {
    case TYPE_CONSTANT:
      new_entry_size += (size_t) or_db_value_size (val->val.dbvalptr) + sizeof (sq_val);
      break;

    default:
      break;
    }

  if (xasl->sq_cache_size_max < xasl->sq_cache_size + new_entry_size)
    {
      regu_var->xasl->sq_cache_flag |= SQ_CACHE_NOT_CACHING_FLAG;
      sq_free_key (key);
      sq_free_val (val);
      return ER_FAILED;
    }

  ret = mht_put_if_not_exists (xasl->sq_cache_ht, key, val);

  xasl->sq_cache_size += new_entry_size;

  if (!ret || ret != val)
    {
      sq_free_key (key);
      sq_free_val (val);
      return ER_FAILED;
    }
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
sq_get (xasl_node * xasl, REGU_VARIABLE * regu_var)
{
  sq_key *key;
  sq_val *ret;

  if (xasl->sq_cache_flag & SQ_CACHE_INITIALIZED_FLAG)
    {
      /* This conditional check acts as a mechanism to prevent the cache from being 
         overwhelmed by unsuccessful lookups. If the cache miss count exceeds a predefined 
         maximum, it evaluates the hit-to-miss ratio to decide whether continuing caching 
         is beneficial. This approach optimizes cache usage and performance by dynamically 
         adapting to the effectiveness of the cache. */
      if (xasl->sq_cache_miss >= xasl->sq_cache_miss_max)
	{
	  if (xasl->sq_cache_hit / xasl->sq_cache_miss < SQ_CACHE_MIN_HIT_RATIO)
	    {
	      regu_var->xasl->sq_cache_flag |= SQ_CACHE_NOT_CACHING_FLAG;
	      return false;
	    }
	}
    }

  key = sq_make_key (xasl);
  if (key == NULL)
    {
      regu_var->xasl->sq_cache_flag |= SQ_CACHE_NOT_CACHING_FLAG;
      return false;
    }
  if (!(xasl->sq_cache_flag & SQ_CACHE_INITIALIZED_FLAG))
    {
      sq_cache_initialize (xasl);
    }

  ret = (sq_val *) mht_get (xasl->sq_cache_ht, key);
  if (ret == NULL)
    {
      xasl->sq_cache_miss++;
      sq_free_key (key);
      return false;
    }

  sq_unpack_val (ret, regu_var);
  sq_free_key (key);

  xasl->sq_cache_hit++;
  return true;
}

/*
 * sq_cache_drop_all () - Clears all cache entries for a given XASL node.
 *   xasl(in): The XASL node for which all cache entries will be cleared.
 *
 * This function clears all cache entries associated with a given XASL node. It does so by iterating over the hash table and
 * freeing all keys and values. This function is typically called when resetting or destroying the cache for a XASL node.
 */
void
sq_cache_drop_all (xasl_node * xasl)
{
  if (xasl->sq_cache_flag & SQ_CACHE_INITIALIZED_FLAG)
    {
      if (xasl->sq_cache_ht != NULL)
	{
	  mht_clear (xasl->sq_cache_ht, sq_rem_func, NULL);
	}
    }
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
sq_cache_destroy (xasl_node * xasl)
{
  if (xasl->sq_cache_flag & SQ_CACHE_INITIALIZED_FLAG)
    {
      er_log_debug (ARG_FILE_LINE,
		    "destroy sq_cache at xasl %p\ncache info : \n\thit : %10lu\n\tmiss: %10lu\n\tsize: %10lu Bytes\n",
		    xasl, xasl->sq_cache_hit, xasl->sq_cache_miss, xasl->sq_cache_size);
      sq_cache_drop_all (xasl);
      mht_destroy (xasl->sq_cache_ht);
    }
  xasl->sq_cache_flag = 0;
  xasl->sq_cache_ht = NULL;
}

/*
 * execute_regu_variable_xasl_with_sq_cache () - Executes a regu variable XASL with support for scalar subquery result caching.
 *   return: False if execution should proceed after caching logic, True otherwise.
 *   thread_p(in): Thread context.
 *   regu_var(in): The regu variable to be executed.
 *   vd(in): Value descriptor for parameter values.
 *
 * This function attempts to execute a regu variable's XASL with caching logic for scalar subquery results. It checks if the
 * XASL node associated with the regu variable is eligible for caching and either retrieves the cached result or executes the
 * XASL and caches the result if appropriate. This function aims to improve performance by avoiding redundant executions of
 * scalar subqueries.
 */
int
execute_regu_variable_xasl_with_sq_cache (THREAD_ENTRY * thread_p, REGU_VARIABLE * regu_var, void *vd)
{
  if (regu_var->xasl && !(regu_var->xasl->sq_cache_flag & SQ_CACHE_NOT_CACHING_FLAG)
      && (regu_var->xasl->status == XASL_CLEARED || regu_var->xasl->status == XASL_INITIALIZED))
    {

      if (regu_var->xasl->sq_cache_flag & SQ_CACHE_ENABLED_FLAG)
	{
	  if (sq_get (regu_var->xasl, regu_var))
	    {
	      regu_var->xasl->status = XASL_SUCCESS;
	      return true;
	    }
	  EXECUTE_REGU_VARIABLE_XASL (thread_p, regu_var, (val_descr *) vd);
	  if (CHECK_REGU_VARIABLE_XASL_STATUS (regu_var) != XASL_SUCCESS)
	    {
	      return false;
	    }
	  sq_put (regu_var->xasl, regu_var);

	  return false;
	}

      else if (regu_var->xasl->sq_cache_flag == 0)
	{
	  regu_var->xasl->sq_cache_flag |= SQ_CACHE_ENABLED_FLAG;
	  if (!(regu_var->xasl->sq_cache_flag & SQ_CACHE_NOT_CACHING_CHECKED_FLAG))
	    {
	      if (sq_walk_xasl_check_not_caching (regu_var->xasl))
		{
		  regu_var->xasl->sq_cache_flag |= SQ_CACHE_NOT_CACHING_FLAG;
		}
	      regu_var->xasl->sq_cache_flag |= SQ_CACHE_NOT_CACHING_CHECKED_FLAG;
	    }
	}
    }

  EXECUTE_REGU_VARIABLE_XASL (thread_p, regu_var, (val_descr *) vd);

  return false;
}
