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
 * query_hash_lookup.cpp - the hash over a list subquery's values for the equality lookups of a predicate
 *
 * A list subquery's R_EQ lookups read the whole list for every row that finds no match in it. From a later lookup of
 * one execution of the subquery on (hash_lookup_get), a hash over the list answers them: a value is hashed in the
 * type its comparison compares it in, and only the list values of the same hash are given back, for the caller to
 * compare by that same comparison. A value the hash cannot take as its comparison compares it (a conversion that
 * fails, a NaN, a type or a collation left out) sends that lookup, or the whole execution, back to reading the list.
 * Only an uncorrelated subquery's list is hashed (eval_some_list_lookup); one that executes again builds its hash
 * again.
 */

#include "config.h"

#include <string.h>

#include "query_hash_lookup.hpp"

#include "dbtype.h"
#include "domain_rules.h"
#include "error_manager.h"
#include "language_support.h"
#include "list_file.h"
#include "memory_alloc.h"
#include "memory_hash.h"
#include "numeric_opfunc.h"
#include "object_primitive.h"
#include "qfile_tuple_layout.h"
#include "query_hash_scan.h"
#include "query_manager.h"
#include "system_parameter.h"
#include "xasl.h"
#include "xasl_iteration.hpp"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

struct hash_lookup
{
  INT64 rows_read;		/* rows of the list the lookups without a table read (hash_lookup_count_read) */
  bool unusable;		/* the lookups read the list: a value the table cannot take, or hashing turned off */
  bool has_null;		/* the list holds a NULL */
  const DOMAIN_COMPARE *compare;	/* the comparison the table was built for */
  const LANG_COLLATION *lang_coll;	/* its collation if it compares strings, looked up once */
  LANG_FASTCMP fastcmp;		/* lang_coll's compare function */
  HASH_LIST_SCAN hash_scan;	/* the table; HASH_METH_NOT_USE: none */
  QFILE_LIST_SCAN_ID list_scan;	/* open while the table is: the table is built over it and reads tuples through it */
  QFILE_TUPLE_RECORD tuple_record;	/* the tuple the last hash_lookup_next found */
  DB_VALUE value;		/* that tuple's list value */
};

static bool hash_lookup_type (DB_TYPE type);
static bool hash_lookup_comparable (const DOMAIN_COMPARE *compare);
static bool hash_lookup_value (const HASH_LOOKUP *hash, const DB_VALUE *value, unsigned int *hash_key);
static bool hash_lookup_key (const HASH_LOOKUP *hash, int side, const DB_VALUE *value, unsigned int *hash_key);
static int hash_lookup_build (THREAD_ENTRY *thread_p, HASH_LOOKUP *hash, QFILE_LIST_ID *list_id,
			      const DOMAIN_COMPARE *compare, HASH_METHOD method);
static void hash_lookup_clear (THREAD_ENTRY *thread_p, HASH_LOOKUP *hash);

/* building a hash file measured about 21 reads of its list with 2,000 INT rows a page of the list, and about 3 with 73
 * VARCHAR of 200 bytes: a wider row costs a read more. 64 builds after 31 and 1 reads of the whole list there, later
 * and earlier than measured; measure again if the cost of a hash file changes */
static const int HASH_LOOKUP_FILE_ROWS_PER_READ = 64;

/*
 * Hash values
 */

/* The types whose values hash_lookup_value hashes alike when their comparison finds them equal. */
static bool
hash_lookup_type (DB_TYPE type)
{
  switch (type)
    {
    case DB_TYPE_SMALLINT:
    case DB_TYPE_INTEGER:
    case DB_TYPE_BIGINT:
    case DB_TYPE_NUMERIC:
    case DB_TYPE_FLOAT:
    case DB_TYPE_DOUBLE:
    case DB_TYPE_CHAR:
    case DB_TYPE_VARCHAR:
    case DB_TYPE_DATE:
    case DB_TYPE_TIME:
    case DB_TYPE_TIMESTAMP:
    case DB_TYPE_DATETIME:
      return true;
    default:
      return false;
    }
}

/*
 * hash_lookup_comparable () - a comparison the hash can answer for: its two sides are the row's values, and it either
 *			     compares them as they are or converts them once each, without a failure resolved in
 *			     advance or a codeset brought to an ENUM's, and compares strings in a collation the hash takes
 */
static bool
hash_lookup_comparable (const DOMAIN_COMPARE *compare)
{
  return (compare->method == DOMAIN_COMPARE_DIRECT || compare->method == DOMAIN_COMPARE_CONVERT)
	 && compare->value[0] < 0 && compare->value[1] < 0 && compare->failed == 0 && compare->codeset_side < 0
	 && compare->cmp != NULL && hash_lookup_type (compare->cmp->id)
	 && (!TP_IS_CHAR_TYPE (compare->cmp->id)
	     || (compare->collation >= 0
		 && lang_get_coll_fastcmp (lang_get_collation (compare->collation)) != LANG_FASTCMP_OTHER));
}

/*
 * hash_lookup_value () - the hash of a value in the type its comparison compares it in: values the comparison finds
 *			equal hash alike
 *   return: false if the value has no such hash
 */
static bool
hash_lookup_value (const HASH_LOOKUP *hash, const DB_VALUE *value, unsigned int *hash_key)
{
  const DOMAIN_COMPARE *compare = hash->compare;
  const DB_TYPE type = DB_VALUE_DOMAIN_TYPE (value);
  if (type != compare->cmp->id && ! (TP_IS_CHAR_TYPE (type) && TP_IS_CHAR_TYPE (compare->cmp->id)))
    {
      return false;
    }
  switch (type)
    {
    case DB_TYPE_CHAR:
    case DB_TYPE_VARCHAR:
    {
      /* the comparison's collation, not the value's (hash_lookup_comparable). The collations' hash functions
       * (lang_mht2str_*) tell apart strings their comparison finds equal */
      if (db_get_string_codeset (value) != hash->lang_coll->codeset)
	{
	  return false;
	}
      *hash_key = qdata_hash_string (hash->lang_coll, hash->fastcmp, (const unsigned char *) db_get_string (value),
				     db_get_string_size (value));
      return true;
    }
    case DB_TYPE_NUMERIC:
    case DB_TYPE_DOUBLE:
    case DB_TYPE_FLOAT:
      /* 1.00 and 1.000, 0.0 and -0.0 compare equal; a NaN compares equal to every value and has no hash */
      return qdata_hash_number (value, hash_key);
    default:
      *hash_key = mht_get_hash_number (UINT_MAX, value);
      return true;
    }
}

/*
 * hash_lookup_key () - the hash of a comparison side's value, converted as the comparison converts that side
 *   return: false if the conversion fails or the value has no hash
 *   side(in): 0 the row's item, 1 a list value
 */
static bool
hash_lookup_key (const HASH_LOOKUP *hash, int side, const DB_VALUE *value, unsigned int *hash_key)
{
  const DOMAIN_COMPARE *compare = hash->compare;
  if (compare->conv[side] == NULL)
    {
      return hash_lookup_value (hash, value, hash_key);
    }
  DB_VALUE converted;
  bool hashed = false;
  if (tp_value_convert (compare->conv[side], compare->target[side], value, &converted) == DOMAIN_COMPATIBLE)
    {
      hashed = hash_lookup_value (hash, &converted, hash_key);
    }
  pr_clear_value (&converted);
  return hashed;
}

/*
 * The hash of a subquery's list, owned by the subquery's XASL node (xasl_node::hash_lookup). Only an uncorrelated
 * subquery has one: it fills its list once in a query, and qexec_clear_xasl frees the hash at the query's end
 */

/*
 * hash_lookup_create () - a hash with no table yet
 *   return: the hash, or NULL with an error set
 */
HASH_LOOKUP *
hash_lookup_create (THREAD_ENTRY *thread_p)
{
  HASH_LOOKUP *hash = (HASH_LOOKUP *) db_private_alloc (thread_p, sizeof (HASH_LOOKUP));
  if (hash == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (HASH_LOOKUP));
      return NULL;
    }
  /* no table (HASH_METH_NOT_USE is 0), so no list scan open */
  memset (hash, 0, sizeof (HASH_LOOKUP));
  return hash;
}

/*
 * hash_lookup_destroy () - frees a hash hash_lookup_create made, with its table (qexec_clear_xasl)
 */
void
hash_lookup_destroy (THREAD_ENTRY *thread_p, HASH_LOOKUP *hash)
{
  hash_lookup_clear (thread_p, hash);
  db_private_free (thread_p, hash);
}

/*
 * hash_lookup_destroy_tree () - frees the hashes in an XASL tree: a parallel aptr worker frees those it made in its own
 *			       private heap before another thread clears the tree (qexec_clear_xasl)
 */
void
hash_lookup_destroy_tree (THREAD_ENTRY *thread_p, xasl_node *xasl)
{
  cubxasl::iterate_xasl_tree<bool> (xasl, [thread_p] (XASL_NODE *node)
  {
    if (node->hash_lookup != NULL)
      {
	hash_lookup_destroy (thread_p, node->hash_lookup);
	node->hash_lookup = NULL;
      }
    return true;
  }, true);
}

/*
 * hash_lookup_build () - the table over the list's values for the comparison; hash->unusable instead when a value
 *			has no hash
 *   return: error code
 *   method(in): the table's method (qdata_hscan_method)
 */
static int
hash_lookup_build (THREAD_ENTRY *thread_p, HASH_LOOKUP *hash, QFILE_LIST_ID *list_id,
		   const DOMAIN_COMPARE *compare, HASH_METHOD method)
{
  QFILE_TUPLE_RECORD tuple_record = QFILE_TUPLE_RECORD_INITIALIZER;
  SCAN_CODE qp_scan;
  DB_VALUE value;
  unsigned int hash_key;
  bool is_null, hashed;
  int error;

  error = qfile_open_list_scan (list_id, &hash->list_scan);
  if (error != NO_ERROR)
    {
      return error;
    }
  error =
	  qdata_hscan_create (thread_p, &hash->hash_scan, method, list_id->tuple_cnt, "Eval Hash");
  if (error != NO_ERROR)
    {
      qfile_close_scan (thread_p, &hash->list_scan);
      return error;
    }
  hash->compare = compare;
  hash->lang_coll = NULL;
  hash->fastcmp = LANG_FASTCMP_OTHER;
  if (TP_IS_CHAR_TYPE (compare->cmp->id))
    {
      hash->lang_coll = lang_get_collation (compare->collation);
      hash->fastcmp = lang_get_coll_fastcmp (hash->lang_coll);
    }
  qfile_slot_set_layout (&hash->tuple_record, &hash->list_scan.list_id.type_list);

  while ((qp_scan = qfile_scan_list_next (thread_p, &hash->list_scan, &tuple_record, PEEK)) == S_SUCCESS)
    {
      error = qfile_slot_read_column_value (&tuple_record, 0, list_id->type_list.domp[0], &value, false, &is_null);
      if (error != NO_ERROR)
	{
	  break;
	}
      if (is_null)
	{
	  hash->has_null = true;
	  continue;
	}
      hashed = hash_lookup_key (hash, 1, &value, &hash_key);
      /* read in place, but a compressed string is decompressed into its own buffer */
      pr_clear_value (&value);
      if (!hashed)
	{
	  hash->unusable = true;
	  break;
	}
      qdata_hscan_set_key (&hash->hash_scan, hash_key);
      error = qdata_hscan_insert (thread_p, &hash->hash_scan, &hash->list_scan, &tuple_record);
      if (error != NO_ERROR)
	{
	  break;
	}
    }
  if (error == NO_ERROR && !hash->unusable && qp_scan != S_END)
    {
      assert_release_error (er_errid () != NO_ERROR);
      error = er_errid ();
    }

  if (error != NO_ERROR || hash->unusable)
    {
      hash_lookup_clear (thread_p, hash);
    }
  return error;
}

/*
 * hash_lookup_clear () - frees the table and closes the list scan it reads through: the table's positions first, then
 *			the scan; the state of the execution stays
 */
static void
hash_lookup_clear (THREAD_ENTRY *thread_p, HASH_LOOKUP *hash)
{
  if (hash->hash_scan.hash_list_scan_type != HASH_METH_NOT_USE)
    {
      qdata_hscan_destroy (thread_p, &hash->hash_scan);
      qfile_close_scan (thread_p, &hash->list_scan);
    }
  pr_clear_value (&hash->value);
  hash->has_null = false;
}

/*
 * hash_lookup_get () - the hash that answers a lookup of the subquery's list
 *   return: error code
 *   subquery(in): the subquery whose list list_id is; it owns the hash
 *   compare(in): the lookup's comparison; the table is built only for one hash_lookup_comparable takes
 *   hash(out): the hash with its table, or NULL: the caller reads the list
 */
int
hash_lookup_get (THREAD_ENTRY *thread_p, xasl_node *subquery, QFILE_LIST_ID *list_id,
		 const DOMAIN_COMPARE *compare, HASH_LOOKUP **hash)
{
  HASH_LOOKUP *hash_lookup = subquery->hash_lookup;
  int error;

  *hash = NULL;
  if (hash_lookup == NULL)
    {
      hash_lookup = hash_lookup_create (thread_p);
      if (hash_lookup == NULL)
	{
	  return er_errid ();
	}
      subquery->hash_lookup = hash_lookup;
    }

  if (hash_lookup->unusable)
    {
      return NO_ERROR;
    }
  if (hash_lookup->hash_scan.hash_list_scan_type == HASH_METH_NOT_USE)
    {
      const UINT64 mem_limit = (UINT64) prm_get_bigint_value (PRM_ID_MAX_HASH_LIST_SCAN_SIZE);
      if (mem_limit == 0)
	{
	  /* 0 turns hashing a list off, as it does for a hash list scan */
	  hash_lookup->unusable = true;
	  return NO_ERROR;
	}
      /* a table in memory costs about one read of the list to build: the second lookup builds it. A hash file costs
       * more the more rows a page of the list holds: the lookups before it read the list, until the rows they read cost
       * about what building it does, wherever in the list their matches were */
      const HASH_METHOD method = qdata_hscan_method (list_id, mem_limit);
      INT64 build_rows = 1;
      if (method == HASH_METH_HASH_FILE)
	{
	  const INT64 rows_per_page = list_id->tuple_cnt / MAX (list_id->page_cnt, 1);
	  build_rows = MAX (1, rows_per_page / HASH_LOOKUP_FILE_ROWS_PER_READ) * list_id->tuple_cnt;
	}
      if (hash_lookup->rows_read < build_rows)
	{
	  return NO_ERROR;
	}
      /* checked here, once a table would be built, rather than at each lookup */
      if (!hash_lookup_comparable (compare))
	{
	  return NO_ERROR;
	}
      error = hash_lookup_build (thread_p, hash_lookup, list_id, compare, method);
      if (error != NO_ERROR && error != ER_INTERRUPTED && er_errid () != ER_INTERRUPTED)
	{
	  /* as a hash join falls back to a simpler method: the lookups read the list */
	  er_clear ();
	  error = NO_ERROR;
	  hash_lookup->unusable = true;
	}
      if (error != NO_ERROR || hash_lookup->unusable)
	{
	  return error;
	}
      subquery->xasl_stats.hash_lookup_builds++;
      subquery->xasl_stats.hash_lookup_method = hash_lookup->hash_scan.hash_list_scan_type;
    }
  if (hash_lookup->compare != compare)
    {
      /* a term other than the one the table was built for */
      return NO_ERROR;
    }
  /* the list the table was built over: the subquery has not filled it again */
  assert (hash_lookup->list_scan.list_id.tuple_cnt == list_id->tuple_cnt
	  && VPID_EQ (&hash_lookup->list_scan.list_id.first_vpid, &list_id->first_vpid));

  *hash = hash_lookup;
  return NO_ERROR;
}

/*
 * hash_lookup_count_read () - counts the rows a lookup read from the list for want of a table (hash_lookup_get)
 *   rows(in): the rows the lookup read: up to its match, or the whole list
 */
void
hash_lookup_count_read (xasl_node *subquery, INT64 rows)
{
  assert (subquery->hash_lookup != NULL);
  subquery->hash_lookup->rows_read += rows;
}

/*
 * hash_lookup_has_null () - the list holds a NULL: a lookup that finds no equal value is UNKNOWN, not FALSE
 */
bool
hash_lookup_has_null (const HASH_LOOKUP *hash)
{
  return hash->has_null;
}

/*
 * hash_lookup_start () - starts the list values of the item's hash (hash_lookup_next)
 *   return: false if the item has no hash in the type its comparison compares it in: the caller reads the list
 */
bool
hash_lookup_start (HASH_LOOKUP *hash, const DB_VALUE *item)
{
  unsigned int hash_key;

  if (!hash_lookup_key (hash, 0, item, &hash_key))
    {
      return false;
    }
  qdata_hscan_set_key (&hash->hash_scan, hash_key);
  qfile_slot_reset (&hash->tuple_record);
  return true;
}

/*
 * hash_lookup_next () - the next list value of the item's hash
 *   return: S_SUCCESS, S_END or S_ERROR
 *   value(out): the list value, read in place; it holds until the next call
 */
SCAN_CODE
hash_lookup_next (THREAD_ENTRY *thread_p, HASH_LOOKUP *hash, DB_VALUE **value)
{
  bool is_null;

  if (qdata_hscan_probe (thread_p, &hash->hash_scan, &hash->list_scan, &hash->tuple_record) != NO_ERROR)
    {
      return S_ERROR;
    }
  if (hash->tuple_record.tpl == NULL)
    {
      return S_END;
    }
  pr_clear_value (&hash->value);
  if (qfile_slot_read_column_value (&hash->tuple_record, 0, hash->list_scan.list_id.type_list.domp[0], &hash->value,
				    false, &is_null) != NO_ERROR)
    {
      /* a failed read may have freed what it left in the value */
      db_make_null (&hash->value);
      return S_ERROR;
    }
  /* the list's NULLs are not in the table */
  assert (!is_null);
  *value = &hash->value;
  return S_SUCCESS;
}

/*
 * hash_lookup_end () - ends the list values of an item: a tuple read by its position keeps its list page fixed, so it
 *		      is unfixed between rows, as a hash list scan does when a probe ends
 */
void
hash_lookup_end (THREAD_ENTRY *thread_p, HASH_LOOKUP *hash)
{
  if (hash->list_scan.position == S_ON && hash->list_scan.curr_pgptr != NULL)
    {
      qmgr_free_old_page_and_init (thread_p, hash->list_scan.curr_pgptr, hash->list_scan.list_id.tfile_vfid);
      hash->list_scan.position = S_AFTER;
    }
}
