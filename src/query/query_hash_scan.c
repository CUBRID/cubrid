/*
 * Copyright (C) 2016 CUBRID Corporation
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

//
// query_aggregate - implementation of aggregate functions execution during queries
//

#include "fetch.h"
#include "memory_alloc.h"
#include "memory_hash.h"
#include "object_domain.h"
#include "object_primitive.h"
#include "object_representation.h"
#include "query_opfunc.h"
#include "string_opfunc.h"
#include "query_hash_scan.h"
#include "db_value_printer.hpp"
#include "dbtype.h"

static bool safe_memcpy (void *data, void *source, int size);
static DB_VALUE_COMPARE_RESULT qdata_hscan_key_compare (HASH_SCAN_KEY * ckey1, HASH_SCAN_KEY * ckey2, int *diff_pos);

/*
 * qdata_alloc_hscan_key () - allocate new hash key
 *   returns: pointer to new structure or NULL on error
 *   thread_p(in): thread
 *   val_cnt(in): size of key
 *   alloc_vals(in): if true will allocate dbvalues
 */
HASH_SCAN_KEY *
qdata_alloc_hscan_key (cubthread::entry * thread_p, int val_cnt, bool alloc_vals)
{
  HASH_SCAN_KEY *key;
  int i;

  key = (HASH_SCAN_KEY *) db_private_alloc (thread_p, sizeof (HASH_SCAN_KEY));
  if (key == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (HASH_SCAN_KEY));
      return NULL;
    }

  key->values = (DB_VALUE **) db_private_alloc (thread_p, sizeof (DB_VALUE *) * val_cnt);
  if (key->values == NULL)
    {
      db_private_free (thread_p, key);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (DB_VALUE *) * val_cnt);
      return NULL;
    }

  if (alloc_vals)
    {
      for (i = 0; i < val_cnt; i++)
	{
	  key->values[i] = pr_make_value ();
	  if (key->values[i] == NULL)
	    {
	      key->free_values = true;
	      qdata_free_hscan_key (thread_p, key, i);
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (DB_VALUE *));
	      return NULL;
	    }
	}
    }

  key->val_count = val_cnt;
  key->free_values = alloc_vals;
  return key;
}

/*
 * qdata_free_hscan_key () - free hash key
 *   thread_p(in): thread
 *   key(in): hash key
 */
void
qdata_free_hscan_key (cubthread::entry * thread_p, HASH_SCAN_KEY * key, int val_count)
{
  if (key == NULL)
    {
      return;
    }

  if (key->values != NULL)
    {
      if (key->free_values)
	{
	  for (int i = 0; i < val_count; i++)
	    {
	      if (key->values[i])
		{
		  pr_free_value (key->values[i]);
		}
	    }
	}

      /* free values array */
      db_private_free (thread_p, key->values);
    }

  /* free structure */
  db_private_free (thread_p, key);
}

/*
 * qdata_hash_scan_key () - compute hash of aggregate key
 *   returns: hash value
 *   key(in): key
 *   ht_size(in): hash table size (in buckets)
 */
unsigned int
qdata_hash_scan_key (const void *key, unsigned int ht_size)
{
  HASH_SCAN_KEY *ckey = (HASH_SCAN_KEY *) key;
  unsigned int hash_val = 0, tmp_hash_val;
  int i;

  /* build hash value */
  for (i = 0; i < ckey->val_count; i++)
    {
      tmp_hash_val = mht_get_hash_number (ht_size, ckey->values[i]);
      hash_val = hash_val ^ tmp_hash_val;
      if (hash_val == 0)
	{
	  hash_val = tmp_hash_val;
	}
    }

  return hash_val;
}

/*
 * qdata_hscan_key_compare () - compare two aggregate keys
 *   returns: comparison result
 *   key1(in): first key
 *   key2(in): second key
 *   diff_pos(out): if not equal, position of difference, otherwise -1
 */
static DB_VALUE_COMPARE_RESULT
qdata_hscan_key_compare (HASH_SCAN_KEY * ckey1, HASH_SCAN_KEY * ckey2, int *diff_pos)
{
  DB_VALUE_COMPARE_RESULT result;
  int i;

  assert (diff_pos);
  *diff_pos = -1;

  if (ckey1 == ckey2)
    {
      /* same pointer, same values */
      return DB_EQ;
    }

  if (ckey1->val_count != ckey2->val_count)
    {
      /* can't compare keys of different sizes; shouldn't get here */
      assert (false);
      return DB_UNK;
    }

  for (i = 0; i < ckey1->val_count; i++)
    {
      result = tp_value_compare (ckey1->values[i], ckey2->values[i], 0, 1);
      if (result != DB_EQ)
	{
	  *diff_pos = i;
	  return result;
	}
    }

  /* if we got this far, it's equal */
  return DB_EQ;
}

/*
 * qdata_hscan_key_eq () - check equality of two aggregate keys
 *   returns: true if equal, false otherwise
 *   key1(in): first key
 *   key2(in): second key
 */
int
qdata_hscan_key_eq (const void *key1, const void *key2)
{
  int decoy;

  /* compare for equality */
  return (qdata_hscan_key_compare ((HASH_SCAN_KEY *) key1, (HASH_SCAN_KEY *) key2, &decoy) == DB_EQ);
}

/*
 * qdata_build_hscan_key () - build aggregate key structure from reguvar list
 *   returns: NO_ERROR or error code
 *   thread_p(in): thread
 *   key(out): aggregate key
 *   regu_list(in): reguvar list for fetching values
 */
int
qdata_build_hscan_key (THREAD_ENTRY * thread_p, val_descr * vd, REGU_VARIABLE_LIST regu_list, HASH_SCAN_KEY * key)
{
  int rc = NO_ERROR;

  /* build key */
  key->free_values = false;	/* references precreated DB_VALUES */
  key->val_count = 0;
  while (regu_list != NULL)
    {
      rc = fetch_peek_dbval (thread_p, &regu_list->value, vd, NULL, NULL, NULL, &key->values[key->val_count]);
      if (rc != NO_ERROR)
	{
	  return rc;
	}

      /* next */
      regu_list = regu_list->next;
      key->val_count++;
    }

  /* all ok */
  return NO_ERROR;
}

/*
 * qdata_print_hash_scan_entry () - Print the entry
 *                              Will be used by mht_dump() function
 *   return:
 *   fp(in)     :
 *   key(in)    :
 *   data(in)   :
 *   args(in)   :
 */
int
qdata_print_hash_scan_entry (THREAD_ENTRY * thread_p, FILE * fp, const void *key, void *data, void *args)
{
  HASH_SCAN_VALUE *data2 = (HASH_SCAN_VALUE *) data;
  HASH_SCAN_KEY *key2 = (HASH_SCAN_KEY *) key;
  QFILE_TUPLE_VALUE_TYPE_LIST *list = (QFILE_TUPLE_VALUE_TYPE_LIST *) args;

  if (!data2 || !key2)
    {
      return false;
    }
  if (fp == NULL)
    {
      fp = stdout;
    }

  fprintf (fp, "LIST_CACHE_ENTRY (%p) {\n", data);
  fprintf (fp, "data_size = [%d]  data = [%.*s]\n", QFILE_GET_TUPLE_LENGTH (data2->tuple),
	   QFILE_GET_TUPLE_LENGTH (data2->tuple), data2->tuple);

  fprintf (fp, "key : ");
  for (int i = 0; i < key2->val_count; i++)
    {
      db_fprint_value (fp, key2->values[i]);
      fprintf (fp, " ");
    }
  fprintf (fp, "\n}");

  return true;
}

/*
 * qdata_copy_hscan_key () - deep copy aggregate key
 *   returns: pointer to new aggregate hash key
 *   thread_p(in): thread
 *   key(in): source key
 */
HASH_SCAN_KEY *
qdata_copy_hscan_key (cubthread::entry * thread_p, HASH_SCAN_KEY * key, REGU_VARIABLE_LIST probe_regu_list,
		      val_descr * vd)
{
  HASH_SCAN_KEY *new_key = NULL;
  int i = 0;
  DB_TYPE vtype1, vtype2;
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  if (key)
    {
      /* make a copy */
      new_key = qdata_alloc_hscan_key (thread_p, key->val_count, false);
    }

  if (new_key)
    {
      /* copy values */
      new_key->val_count = key->val_count;
      new_key->free_values = true;
      for (i = 0; i < key->val_count; i++)
	{
	  vtype1 = REGU_VARIABLE_GET_TYPE (&probe_regu_list->value);
	  vtype2 = DB_VALUE_DOMAIN_TYPE (key->values[i]);

	  if (vtype1 != vtype2)
	    {
	      new_key->values[i] = pr_make_value ();
	      if (new_key->values[i] == NULL)
		{
		  qdata_free_hscan_key (thread_p, new_key, i);
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (DB_VALUE *));
		  return NULL;
		}

	      status = tp_value_coerce (key->values[i], new_key->values[i], probe_regu_list->value.domain);
	      if (status != DOMAIN_COMPATIBLE)
		{
		  qdata_free_hscan_key (thread_p, new_key, ++i);
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TP_CANT_COERCE, 2, pr_type_name (vtype2),
			  pr_type_name (vtype1));
		  return NULL;
		}
	    }
	  else
	    {
	      new_key->values[i] = pr_copy_value (key->values[i]);
	      if (new_key->values[i] == NULL)
		{
		  qdata_free_hscan_key (thread_p, new_key, i);
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (DB_VALUE *));
		  return NULL;
		}
	    }
	  probe_regu_list = probe_regu_list->next;
	}
    }

  return new_key;
}

/*
 * qdata_alloc_agg_hkey () - allocate new hash aggregate key
 *   returns: pointer to new structure or NULL on error
 *   thread_p(in): thread
 */
HASH_SCAN_VALUE *
qdata_alloc_hscan_value (cubthread::entry * thread_p, QFILE_TUPLE tpl)
{
  HASH_SCAN_VALUE *value;
  int tuple_size = QFILE_GET_TUPLE_LENGTH (tpl);

  /* alloc structure */
  value = (HASH_SCAN_VALUE *) db_private_alloc (thread_p, sizeof (HASH_SCAN_VALUE));
  if (value == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (HASH_SCAN_VALUE));
      return NULL;
    }

  value->tuple = (QFILE_TUPLE) db_private_alloc (thread_p, tuple_size);
  if (value->tuple == NULL)
    {
      qdata_free_hscan_value (thread_p, value);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, tuple_size);
      return NULL;
    }
  /* save tuple */
  if (!safe_memcpy (value->tuple, tpl, tuple_size))
    {
      qdata_free_hscan_value (thread_p, value);
      return NULL;
    }
  return value;
}

static bool
safe_memcpy (void *data, void *source, int size)
{
  if (size < 0)
    {
      return false;
    }
  memcpy (data, source, (size_t) size);
  return true;
}

/*
 * qdata_free_hscan_value () - free hash value
 *   thread_p(in): thread
 *   key(in): hash value
 */
void
qdata_free_hscan_value (cubthread::entry * thread_p, HASH_SCAN_VALUE * value)
{
  if (value == NULL)
    {
      return;
    }

  /* free values */
  if (value->tuple != NULL)
    {
      db_private_free_and_init (thread_p, value->tuple);
    }
  /* free structure */
  db_private_free_and_init (thread_p, value);
}

/*
 * qdata_free_agg_hentry () - free key-value pair of hash entry
 *   returns: error code or NO_ERROR
 *   key(in): key pointer
 *   data(in): value pointer
 *   args(in): args passed by mht_rem (should be null)
 */
int
qdata_free_hscan_entry (const void *key, void *data, void *args)
{
  /* free key */
  qdata_free_hscan_key ((cubthread::entry *) args, (HASH_SCAN_KEY *) key,
			key ? ((HASH_SCAN_KEY *) key)->val_count : 0);

  /* free tuple */
  qdata_free_hscan_value ((cubthread::entry *) args, (HASH_SCAN_VALUE *) data);

  /* all ok */
  return NO_ERROR;
}
