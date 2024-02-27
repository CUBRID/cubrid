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
#include "dbtype.h"
#include "query_executor.h"
#include "xasl_predicate.hpp"
#include "regu_var.hpp"

#include "heap_attrinfo.h"
#include "object_domain.h"
#include "query_list.h"
#include "string_opfunc.h"
#include "object_primitive.h"
#include "db_function.hpp"

#include "list_file.h"
#include "db_set_function.h"

#include "sq_cache.h"

#define SQ_CACHE_MISS_MAX 1000
#define SQ_CACHE_MIN_HIT_RATIO 9

#define SQ_TYPE_PRED 0
#define SQ_TYPE_REGU_VAR 1
#define SQ_TYPE_DBVAL 2

#define SQ_CACHE_DISABLED 0
#define SQ_CACHE_ENABLED 1
#define SQ_CACHE_NOT_INITIALIZED 3

typedef union _sq_regu_value
{
  /* fields used by both XASL interpreter and regulator */
  //DB_VALUE dbval;             /* for DB_VALUE values */
  DB_VALUE *dbvalptr;		/* for constant values */
  //ARITH_TYPE *arithptr;               /* arithmetic expression */
  //  cubxasl::aggregate_list_node * aggptr;    /* aggregate expression */
  //ATTR_DESCR attr_descr;      /* attribute information */
  //QFILE_TUPLE_VALUE_POSITION pos_descr;       /* list file columns */
  QFILE_SORTED_LIST_ID *srlist_id;	/* sorted list identifier for subquery results */
  //int val_pos;                        /* host variable references */
  //struct function_node *funcp;        /* function */
  //REGU_VALUE_LIST *reguval_list;      /* for "values" query */
  //REGU_VARIABLE_LIST regu_var_list;   /* for CUME_DIST and PERCENT_RANK */
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

static int sq_hm_entries = 10;
static int sq_key_max_term = 3;


/**************************************************************************************/

static sq_key *sq_make_key (xasl_node * xasl);
static sq_val *sq_make_val (REGU_VARIABLE * val);
static void sq_free_key (sq_key * key);
static void sq_free_val (sq_val * val);
static void sq_unpack_val (sq_val * val, REGU_VARIABLE * retp);

static unsigned int sq_hash_func (const void *key, unsigned int ht_size);
static int sq_cmp_func (const void *key1, const void *key2);
static int sq_rem_func (const void *key, void *data, void *args);

static int sq_walk_xasl_and_add_val_to_set (void *p, int type, DB_VALUE * pred_set);

/**************************************************************************************/

sq_key *
sq_make_key (xasl_node * xasl)
{
  sq_key *keyp;
  int cnt = 0;
  ACCESS_SPEC_TYPE *p;

  p = xasl->spec_list;

  if (p && !p->where_key && !p->where_pred && !p->where_range)
    {
      /* this should be modified later, no conditions -> not caching? */
      return NULL;
    }

  keyp = (sq_key *) malloc (sizeof (sq_key));
  keyp->pred_set = db_value_create ();
  db_make_set (keyp->pred_set, db_set_create_basic (NULL, NULL));

  for (p = xasl->spec_list; p; p = p->next)
    {
      /* key */
      cnt += sq_walk_xasl_and_add_val_to_set (p->where_key, SQ_TYPE_PRED, keyp->pred_set);
      /* pred */
      cnt += sq_walk_xasl_and_add_val_to_set (p->where_pred, SQ_TYPE_PRED, keyp->pred_set);
      /* range */
      cnt += sq_walk_xasl_and_add_val_to_set (p->where_range, SQ_TYPE_PRED, keyp->pred_set);
    }
  if (xasl->scan_ptr)
    {
      for (p = xasl->scan_ptr->spec_list; p; p = p->next)
	{
	  /* key */
	  cnt += sq_walk_xasl_and_add_val_to_set (p->where_key, SQ_TYPE_PRED, keyp->pred_set);
	  /* pred */
	  cnt += sq_walk_xasl_and_add_val_to_set (p->where_pred, SQ_TYPE_PRED, keyp->pred_set);
	  /* range */
	  cnt += sq_walk_xasl_and_add_val_to_set (p->where_range, SQ_TYPE_PRED, keyp->pred_set);
	}
    }

  if (xasl->if_pred)
    {
      cnt += sq_walk_xasl_and_add_val_to_set (xasl->if_pred, SQ_TYPE_PRED, keyp->pred_set);
    }

  if (cnt == 0)
    {
      sq_free_key (keyp);
      return NULL;
    }

  return keyp;
}

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
      ret->val.dbvalptr->domain.general_info.is_null = 0;
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

void
sq_free_key (sq_key * key)
{
  db_value_clear (key->pred_set);
  pr_free_ext_value (key->pred_set);

  free (key);
}

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

int
sq_rem_func (const void *key, void *data, void *args)
{
  sq_free_key ((sq_key *) key);
  sq_free_val ((sq_val *) data);
  return NO_ERROR;
}

int
sq_walk_xasl_and_add_val_to_set (void *p, int type, DB_VALUE * pred_set)
{
  int cnt = 0;

  if (!p)
    {
      return 0;
    }

  switch (type)
    {
    case SQ_TYPE_PRED:
      if (1)
	{
	  PRED_EXPR *src = (PRED_EXPR *) p;
	  if (src->type == T_PRED)
	    {
	      cnt += sq_walk_xasl_and_add_val_to_set (src->pe.m_pred.lhs, SQ_TYPE_PRED, pred_set);
	      cnt += sq_walk_xasl_and_add_val_to_set (src->pe.m_pred.rhs, SQ_TYPE_PRED, pred_set);
	    }
	  else if (src->type == T_EVAL_TERM)
	    {
	      COMP_EVAL_TERM t = src->pe.m_eval_term.et.et_comp;
	      cnt += sq_walk_xasl_and_add_val_to_set (t.lhs, SQ_TYPE_REGU_VAR, pred_set);
	      cnt += sq_walk_xasl_and_add_val_to_set (t.rhs, SQ_TYPE_REGU_VAR, pred_set);
	    }
	}
      break;

    case SQ_TYPE_REGU_VAR:
      if (1)
	{
	  REGU_VARIABLE *src = (REGU_VARIABLE *) p;
	  if (src->type == TYPE_CONSTANT)
	    {
	      cnt += sq_walk_xasl_and_add_val_to_set (src->value.dbvalptr, SQ_TYPE_DBVAL, pred_set);
	    }
	  if (src->type == TYPE_DBVAL)
	    {
	      cnt += sq_walk_xasl_and_add_val_to_set (&src->value.dbval, SQ_TYPE_DBVAL, pred_set);
	    }
	  else if (src->type == TYPE_INARITH)
	    {
	      cnt += sq_walk_xasl_and_add_val_to_set (src->value.arithptr->leftptr, SQ_TYPE_REGU_VAR, pred_set);
	      cnt += sq_walk_xasl_and_add_val_to_set (src->value.arithptr->rightptr, SQ_TYPE_REGU_VAR, pred_set);
	    }
	}

      break;

    case SQ_TYPE_DBVAL:
      if (1)
	{
	  db_set_add (db_get_set (pred_set), (DB_VALUE *) p);
	  cnt++;
	}

      break;

    default:
      assert (0);
      break;
    }

  return cnt;
}


int
sq_cache_initialize (xasl_node * xasl)
{
  xasl->sq_cache_ht = mht_create ("sq_cache", sq_hm_entries, sq_hash_func, sq_cmp_func);
  if (!xasl->sq_cache_ht)
    {
      return ER_FAILED;
    }
  xasl->sq_cache_hit = (DB_BIGINT) 0;
  xasl->sq_cache_miss = (DB_BIGINT) 0;
  xasl->sq_cache_enabled = SQ_CACHE_ENABLED;
  return NO_ERROR;
}

int
sq_put (xasl_node * xasl, REGU_VARIABLE * regu_var)
{
  sq_key *key;
  sq_val *val;
  const void *ret;

  key = sq_make_key (xasl);

  if (key == NULL)
    {
      return ER_FAILED;
    }

  val = sq_make_val (regu_var);

  if (xasl->sq_cache_enabled == SQ_CACHE_NOT_INITIALIZED)
    {
      sq_cache_initialize (xasl);
    }

  ret = mht_put_if_not_exists (xasl->sq_cache_ht, key, val);
  if (!ret || ret != val)
    {
      sq_free_key (key);
      sq_free_val (val);
      return ER_FAILED;
    }
  return NO_ERROR;

}

bool
sq_get (xasl_node * xasl, REGU_VARIABLE * regu_var)
{
  sq_key *key;
  sq_val *ret;

  if (xasl->sq_cache_enabled != SQ_CACHE_ENABLED)
    {
      return false;
    }

  if (xasl->sq_cache_miss >= SQ_CACHE_MISS_MAX)
    {
      if (xasl->sq_cache_hit / xasl->sq_cache_miss < SQ_CACHE_MIN_HIT_RATIO)
	{
	  return false;
	}
    }

  key = sq_make_key (xasl);
  if (key == NULL)
    {
      return false;
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

void
sq_cache_drop_all (xasl_node * xasl)
{
  if (xasl->sq_cache_enabled == SQ_CACHE_ENABLED)
    {
      if (xasl->sq_cache_ht != NULL)
	{
	  mht_clear (xasl->sq_cache_ht, sq_rem_func, NULL);
	}
    }
}

void
sq_cache_destroy (xasl_node * xasl)
{
  if (xasl->sq_cache_enabled == SQ_CACHE_ENABLED)
    {
      er_log_debug (ARG_FILE_LINE, "destroy sq_cache at xasl %p\ncache info : \n\thit : %10ld\n\tmiss: %10ld\n", xasl,
		    xasl->sq_cache_hit, xasl->sq_cache_miss);
      sq_cache_drop_all (xasl);
      mht_destroy (xasl->sq_cache_ht);
    }
  xasl->sq_cache_enabled = SQ_CACHE_DISABLED;
  xasl->sq_cache_ht = NULL;
}

int
execute_regu_variable_xasl_with_sq_cache (THREAD_ENTRY * thread_p, REGU_VARIABLE * regu_var, val_descr * vd)
{
  if (regu_var->xasl && !regu_var->xasl->aptr_list && !regu_var->xasl->dptr_list
      && (regu_var->xasl->status == XASL_CLEARED || regu_var->xasl->status == XASL_INITIALIZED))
    {

      if (regu_var->xasl->sq_cache_enabled == SQ_CACHE_ENABLED
	  || regu_var->xasl->sq_cache_enabled == SQ_CACHE_NOT_INITIALIZED)
	{
	  if (sq_get (regu_var->xasl, regu_var))
	    {
	      regu_var->xasl->status = XASL_SUCCESS;
	      return true;
	    }
	  EXECUTE_REGU_VARIABLE_XASL (thread_p, regu_var, vd);
	  if (CHECK_REGU_VARIABLE_XASL_STATUS (regu_var) != XASL_SUCCESS)
	    {
	      return false;
	    }
	  sq_put (regu_var->xasl, regu_var);

	  return false;
	}

      if (regu_var->xasl->sq_cache_enabled == SQ_CACHE_DISABLED)
	{
	  regu_var->xasl->sq_cache_enabled = SQ_CACHE_NOT_INITIALIZED;
	}

    }

  EXECUTE_REGU_VARIABLE_XASL (thread_p, regu_var, vd);

  return false;
}
