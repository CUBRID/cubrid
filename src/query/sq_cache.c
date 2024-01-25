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
 * Correlated Scalar Subquery Result Cache.
 */

#ident "$Id$"

#include <stdio.h>
#include <string.h>

#include "xasl.h"
#include "xasl_predicate.hpp"
#include "dbtype.h"


#include "sq_cache.h"

typedef struct _sq_cache
{
  MHT_TABLE *hashtable;
  bool enabled;
} sq_cache;

sq_cache sq_cache_by_thread[100];

typedef struct _sq_key
{
  xasl_node *xasl_addr;
  VAL_LIST *key_list;
  VAL_LIST *pred_list;
  VAL_LIST *range_list;
} sq_key;

typedef struct _sq_val
{
  DB_VALUE *dbval;
} sq_val;

static int sq_hm_entries = 10;
static int sq_key_max_term = 3;

static unsigned int sq_hash_func (const void *key, unsigned int ht_size);
static int sq_cmp_func (const void *key1, const void *key2);
static sq_key *sq_make_key (xasl_node * xasl);
static void sq_free_key (sq_key * key);
static void sq_copy_val_list (VAL_LIST * val_list_p, VAL_LIST ** new_val_list, bool alloc);
static sq_val *sq_make_val (xasl_node * xasl, DB_VALUE * result);
static void sq_unpack_val (sq_val * val, xasl_node * xasl, DB_VALUE ** retp);
static int sq_add_term_to_list (PRED_EXPR * src, VAL_LIST * dest);
static int sq_add_val_to_list (DB_VALUE * dbv, VAL_LIST * list);
static void sq_free_val (sq_val * val);

int
sq_cache_initialize (THREAD_ENTRY * thread_p)
{
  sq_cache *p = &sq_cache_by_thread[thread_p->index % 100];
  assert (!p->enabled);
  p->hashtable = mht_create ("sq_cache", sq_hm_entries, sq_hash_func, sq_cmp_func);
  if (!p->hashtable)
    {
      return ER_FAILED;
    }
  p->enabled = true;
  return NO_ERROR;
}

static unsigned int
sq_hash_func (const void *key, unsigned int ht_size)
{
  sq_key *pk, k;
  QPROC_DB_VALUE_LIST p;
  int i;
  DB_VALUE t;
  unsigned int h = 0;
  pk = (sq_key *) key;
  k = *pk;
  memset (&t, 0, sizeof (DB_VALUE));

  h = mht_ptrhash ((void *) k.xasl_addr, ht_size);

  for (p = k.key_list->valp; p; p = p->next)
    {
      h += mht_valhash (p->val, ht_size);
    }
  for (p = k.pred_list->valp; p; p = p->next)
    {
      h += mht_valhash (p->val, ht_size);
    }
  for (p = k.range_list->valp; p; p = p->next)
    {
      h += mht_valhash (p->val, ht_size);
    }

  return h % ht_size;
}

static int
sq_cmp_func (const void *key1, const void *key2)
{
  sq_key *k1, *k2;
  QPROC_DB_VALUE_LIST p1, p2;
  k1 = (sq_key *) key1;
  k2 = (sq_key *) key2;
  if (!mht_compare_ptrs_are_equal (k1->xasl_addr, k2->xasl_addr))
    {
      return 0;
    }
  if (k1->key_list->val_cnt != k2->key_list->val_cnt)
    {
      return 0;
    }
  if (k1->pred_list->val_cnt != k2->pred_list->val_cnt)
    {
      return 0;
    }
  if (k1->range_list->val_cnt != k2->range_list->val_cnt)
    {
      return 0;
    }

  p1 = k1->key_list->valp;
  p2 = k2->key_list->valp;
  for (; p1 && p2; p1 = p1->next)
    {
      if (!p1->val || !p2->val)
	{
	  return 0;
	}
      if (!mht_compare_dbvalues_are_equal (p1->val, p2->val))
	{
	  return 0;
	}
      p2 = p2->next;
    }

  p1 = k1->pred_list->valp;
  p2 = k2->pred_list->valp;
  for (; p1 && p2; p1 = p1->next)
    {
      if (!p1->val || !p2->val)
	{
	  return 0;
	}
      if (!mht_compare_dbvalues_are_equal (p1->val, p2->val))
	{
	  return 0;
	}
      p2 = p2->next;
    }

  p1 = k1->range_list->valp;
  p2 = k2->range_list->valp;
  for (; p1 && p2; p1 = p1->next)
    {
      if (!p1->val || !p2->val)
	{
	  return 0;
	}
      if (!mht_compare_dbvalues_are_equal (p1->val, p2->val))
	{
	  return 0;
	}
      p2 = p2->next;
    }


  return 1;
}

static int
sq_add_val_to_list (DB_VALUE * dbv, VAL_LIST * list)
{
  int cnt = 0;
  QPROC_DB_VALUE_LIST p;
  if (!list->valp)
    {
      list->valp = (QPROC_DB_VALUE_LIST) malloc (sizeof (qproc_db_value_list));
      list->valp->next = NULL;
      p = list->valp;
    }
  else
    {
      for (p = list->valp; p->next; p = p->next)
	{
	  ;			/* find last node in list */
	}
      p->next = (QPROC_DB_VALUE_LIST) malloc (sizeof (qproc_db_value_list));
      p = p->next;
    }

  p->dom = 0;
  p->val = db_value_copy (dbv);
  p->next = NULL;
  list->val_cnt++;
  cnt++;
  return cnt;
}

static int
sq_regu_var_handler (regu_variable_node * p, VAL_LIST * dest)
{
  int cnt = 0;
  if (!p)
    {
      return 0;
    }
  if (p->type == TYPE_CONSTANT)
    {
      cnt += sq_add_val_to_list (p->value.dbvalptr, dest);
    }
  else if (p->type == TYPE_DBVAL)
    {
      cnt += sq_add_val_to_list (&p->value.dbval, dest);
    }
  else if (p->type == TYPE_INARITH)
    {
      cnt += sq_regu_var_handler (p->value.arithptr->leftptr, dest);
      cnt += sq_regu_var_handler (p->value.arithptr->rightptr, dest);
    }
  return cnt;
}

static int
sq_add_term_to_list (PRED_EXPR * src, VAL_LIST * dest)
{
  int cnt = 0;
  if (!src)
    {
      return 0;
    }

  if (src->type == T_PRED)
    {
      cnt += sq_add_term_to_list (src->pe.m_pred.lhs, dest);
      cnt += sq_add_term_to_list (src->pe.m_pred.rhs, dest);
      return cnt;
    }

  if (src->type == T_EVAL_TERM)
    {
      if (src->pe.m_eval_term.et_type == T_COMP_EVAL_TERM)
	{
	  COMP_EVAL_TERM t = src->pe.m_eval_term.et.et_comp;
	  cnt += sq_regu_var_handler (t.lhs, dest);
	  cnt += sq_regu_var_handler (t.rhs, dest);
	}


    }
  return cnt;

}

static sq_key *
sq_make_key (xasl_node * xasl)
{
  sq_key *keyp;
  ACCESS_SPEC_TYPE *p;
  int cnt;

  p = xasl->spec_list;

  if (p && !p->where_key && !p->where_pred && !p->where_range)
    {
      /* this should be modified later, no conditions -> not caching? */
      return NULL;
    }

  keyp = (sq_key *) malloc (sizeof (sq_key));
  memset (keyp, 0, sizeof (sq_key));
  keyp->xasl_addr = xasl;

  keyp->key_list = (VAL_LIST *) malloc (sizeof (VAL_LIST));
  keyp->key_list->val_cnt = 0;
  keyp->key_list->valp = NULL;

  keyp->pred_list = (VAL_LIST *) malloc (sizeof (VAL_LIST));
  keyp->pred_list->val_cnt = 0;
  keyp->pred_list->valp = NULL;

  keyp->range_list = (VAL_LIST *) malloc (sizeof (VAL_LIST));
  keyp->range_list->val_cnt = 0;
  keyp->range_list->valp = NULL;


  for (p = xasl->spec_list; p; p = p->next)
    {
      /* key */
      cnt += sq_add_term_to_list (p->where_key, keyp->key_list);
      /* pred */
      cnt += sq_add_term_to_list (p->where_pred, keyp->pred_list);
      /* range */
      cnt += sq_add_term_to_list (p->where_range, keyp->range_list);
    }
  if (xasl->scan_ptr)
    {
      for (p = xasl->scan_ptr->spec_list; p; p = p->next)
	{
	  /* key */
	  cnt += sq_add_term_to_list (p->where_key, keyp->key_list);
	  /* pred */
	  cnt += sq_add_term_to_list (p->where_pred, keyp->pred_list);
	  /* range */
	  cnt += sq_add_term_to_list (p->where_range, keyp->range_list);
	}
    }

  if (cnt == 0)
    {
      sq_free_key (keyp);
      return NULL;
    }

  return keyp;
}

static void
sq_free_key (sq_key * keyp)
{
  QPROC_DB_VALUE_LIST p, tmp;
  if (keyp->key_list->val_cnt > 0)
    {
      p = keyp->key_list->valp;
      while (p != NULL)
	{
	  tmp = p;
	  p = p->next;

	  pr_free_ext_value (tmp->val);
	  free (tmp);
	}
    }

  if (keyp->pred_list->val_cnt > 0)
    {
      p = keyp->pred_list->valp;
      while (p != NULL)
	{
	  tmp = p;
	  p = p->next;

	  pr_free_ext_value (tmp->val);
	  free (tmp);
	}
    }

  if (keyp->range_list->val_cnt > 0)
    {
      p = keyp->range_list->valp;
      while (p != NULL)
	{
	  tmp = p;
	  p = p->next;

	  pr_free_ext_value (tmp->val);
	  free (tmp);
	}
    }
  free (keyp->key_list);
  free (keyp->pred_list);
  free (keyp->range_list);

  free (keyp);
}


static void
sq_copy_val_list (VAL_LIST * val_list_p, VAL_LIST ** new_val_list, bool alloc)
{
  QPROC_DB_VALUE_LIST dblist1, dblist2;

  if (!alloc)
    {
      QPROC_DB_VALUE_LIST p, tmp;
      p = (*new_val_list)->valp;
      dblist2 = (*new_val_list)->valp;
      (*new_val_list)->val_cnt = 0;
      for (dblist1 = val_list_p->valp; dblist1; dblist1 = dblist1->next)
	{
	  if ((*new_val_list)->valp != dblist2)
	    {
	      if (!dblist2->next)
		{
		  dblist2->next = (QPROC_DB_VALUE_LIST) malloc (sizeof (qproc_db_value_list));
		  dblist2 = dblist2->next;
		  dblist2->next = 0;
		  dblist2->val = 0;
		}
	      else
		{
		  dblist2 = dblist2->next;
		}

	    }

	  if (dblist2->val)
	    {
	      db_value_clone (dblist1->val, dblist2->val);
	    }
	  else
	    {
	      dblist2->val = db_value_copy (dblist1->val);
	    }


	  dblist2->dom = 0;
	  (*new_val_list)->val_cnt++;
	}
    }
  else
    {
      *new_val_list = (VAL_LIST *) malloc (sizeof (VAL_LIST));
      dblist2 = NULL;

      (*new_val_list)->val_cnt = 0;

      for (dblist1 = val_list_p->valp; dblist1; dblist1 = dblist1->next)
	{
	  if (!dblist2)
	    {
	      (*new_val_list)->valp = (QPROC_DB_VALUE_LIST) malloc (sizeof (qproc_db_value_list));
	      (*new_val_list)->valp->next = 0;
	      dblist2 = (*new_val_list)->valp;
	    }
	  else
	    {
	      dblist2->next = (QPROC_DB_VALUE_LIST) malloc (sizeof (qproc_db_value_list));
	      dblist2 = dblist2->next;
	      dblist2->next = 0;
	    }
	  dblist2->val = db_value_copy (dblist1->val);
	  dblist2->dom = 0;
	  (*new_val_list)->val_cnt++;
	}
    }
  return;
}

static sq_val *
sq_make_val (xasl_node * xasl, DB_VALUE * result)
{
  sq_val *val;
  val = (sq_val *) malloc (sizeof (sq_val));
  val->dbval = db_value_copy (result);

  return val;
}

static void
sq_unpack_val (sq_val * val, xasl_node * xasl, DB_VALUE ** retp)
{


  if (*retp)
    {
      pr_clear_value (*retp);
      db_value_clone (val->dbval, *retp);
    }
  else
    {
      *retp = db_value_copy (val->dbval);
    }

  return;
}

static void
sq_free_val (sq_val * val)
{
  QPROC_DB_VALUE_LIST p, tmp;
  pr_free_ext_value (val->dbval);
  /*
     if (val->single_tuple->val_cnt > 0)
     {
     p = val->single_tuple->valp;
     while (p != NULL)
     {
     tmp = p;
     p = p->next;

     pr_free_ext_value (tmp->val);
     free (tmp);
     }
     }
     free (val->single_tuple); */
  free (val);

}

int
sq_put (THREAD_ENTRY * thread_p, xasl_node * xasl, DB_VALUE * result)
{
  sq_cache *sq_cache = &sq_cache_by_thread[thread_p->index % 100];
  const void *ret;
  if (sq_cache->enabled == false)
    {
      sq_cache_initialize (thread_p);
    }


  sq_key *key = sq_make_key (xasl);

  if (key == NULL)
    {
      return ER_FAILED;
    }

  sq_val *val = sq_make_val (xasl, result);

  ret = mht_put_if_not_exists (sq_cache->hashtable, key, val);

  if (!ret || ret != val)
    {
      sq_free_key (key);
      sq_free_val (val);
      return ER_FAILED;
    }

  return NO_ERROR;
}

bool
sq_get (THREAD_ENTRY * thread_p, xasl_node * xasl, DB_VALUE ** retp)
{
  sq_cache *sq_cache = &sq_cache_by_thread[thread_p->index % 100];
  sq_key *key;
  sq_val *ret;

  if (sq_cache->enabled == false)
    {
      sq_cache_initialize (thread_p);
    }

  key = sq_make_key (xasl);

  if (key == NULL)
    {
      return false;
    }

  ret = (sq_val *) mht_get (sq_cache->hashtable, key);

  if (ret == NULL)
    {
      sq_free_key (key);
      return false;
    }

  sq_unpack_val (ret, xasl, retp);

  sq_free_key (key);

  return true;
}

static int
sq_rem_func (const void *key, void *data, void *args)
{
  sq_free_key ((sq_key *) key);
  sq_free_val ((sq_val *) data);

  return NO_ERROR;
}

void
sq_cache_drop_all (THREAD_ENTRY * thread_p)
{
  sq_cache *sq_cache = &sq_cache_by_thread[thread_p->index % 100];
  if (sq_cache->hashtable != NULL)
    {
      mht_clear (sq_cache->hashtable, sq_rem_func, NULL);
    }
}

void
sq_cache_destroy (THREAD_ENTRY * thread_p)
{
  sq_cache *sq_cache = &sq_cache_by_thread[thread_p->index % 100];
  if (sq_cache && sq_cache->enabled && sq_cache->hashtable)
    {
      sq_cache_drop_all (thread_p);
      mht_destroy (sq_cache->hashtable);
      sq_cache->enabled = false;
    }
}
