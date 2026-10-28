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
 * A list subquery's R_EQ lookups read the whole list for every row that finds no match in it. From the first lookup
 * of one execution of the subquery on (hash_lookup_get), a hash over the list in memory answers them; a list too large
 * for memory is read instead. A value is hashed in the type its comparison compares it in, and only the list values
 * of the same hash are given back, for the caller to compare by that same comparison. A value the hash cannot take as
 * its comparison compares it (a conversion that fails, a NaN, a type or a collation left out) sends that lookup, or
 * the whole execution, back to reading the list.
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

/* the table over a subquery's list: built once, then only read, by the lookups of the subquery's node and of the PX
 * scan workers' clones of it that share the list (hash_lookup_share) */
typedef struct hash_lookup_table HASH_LOOKUP_TABLE;
struct hash_lookup_table
{
  bool unusable;		/* the lookups read the list: a value the table cannot take, a list too large for memory, or
				 * hashing turned off */
  bool has_null;		/* the list holds a NULL */
  DOMAIN_COMPARE compare;	/* the comparison the table was built for: a copy, as each worker's term has its own */
  const LANG_COLLATION *lang_coll;	/* its collation if it compares strings, looked up once */
  LANG_FASTCMP fastcmp;		/* lang_coll's compare function */
  HASH_LIST_SCAN hash_scan;	/* the table; HASH_METH_NOT_USE: none */
};

/* one subquery node's lookups of a table: of its own, or of the leader's for a PX scan worker's clone */
struct hash_lookup
{
  HASH_LOOKUP_TABLE *table;	/* NULL: none yet */
  bool owns_table;		/* false: the leader's, which the leader frees */
  bool scan_open;		/* list_scan is open */
  HASH_LIST_SCAN hash_scan;	/* the table's, with this node's key and entry */
  QFILE_LIST_SCAN_ID list_scan;	/* a scan of the list: a HYBRID table's tuples are read by their positions through it */
  QFILE_TUPLE_RECORD tuple_record;	/* the tuple the last hash_lookup_next found */
  DB_VALUE value;		/* that tuple's list value */
};

static bool hash_lookup_type (DB_TYPE type);
static bool hash_lookup_comparable (const DOMAIN_COMPARE *compare);
static bool hash_lookup_same_compare (const DOMAIN_COMPARE *a, const DOMAIN_COMPARE *b);
static bool hash_lookup_value (const HASH_LOOKUP_TABLE *table, const DB_VALUE *value, unsigned int *hash_key);
static bool hash_lookup_key (const HASH_LOOKUP_TABLE *table, int side, const DB_VALUE *value, unsigned int *hash_key);
static HASH_LOOKUP *hash_lookup_create (THREAD_ENTRY *thread_p, HASH_LOOKUP_TABLE *table, bool owns_table);
static int hash_lookup_new_table (THREAD_ENTRY *thread_p, xasl_node *subquery, QFILE_LIST_ID *list_id,
				  const DOMAIN_COMPARE *compare);
static int hash_lookup_build (THREAD_ENTRY *thread_p, HASH_LOOKUP_TABLE *table, QFILE_LIST_ID *list_id,
			      const DOMAIN_COMPARE *compare, HASH_METHOD method);

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
 * hash_lookup_same_compare () - a lookup's comparison is the one the table was built for: the fields its hash and its
 *				 hash_lookup_comparable read
 */
static bool
hash_lookup_same_compare (const DOMAIN_COMPARE *a, const DOMAIN_COMPARE *b)
{
  return a->method == b->method && a->cmp == b->cmp && a->conv[0] == b->conv[0] && a->conv[1] == b->conv[1]
	 && a->target[0] == b->target[0] && a->target[1] == b->target[1] && a->value[0] == b->value[0]
	 && a->value[1] == b->value[1] && a->collation == b->collation && a->failed == b->failed
	 && a->codeset_side == b->codeset_side;
}

/*
 * hash_lookup_value () - the hash of a value in the type its comparison compares it in: values the comparison finds
 *			equal hash alike
 *   return: false if the value has no such hash
 */
static bool
hash_lookup_value (const HASH_LOOKUP_TABLE *table, const DB_VALUE *value, unsigned int *hash_key)
{
  const DOMAIN_COMPARE *compare = &table->compare;
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
      if (db_get_string_codeset (value) != table->lang_coll->codeset)
	{
	  return false;
	}
      *hash_key = qdata_hash_string (table->lang_coll, table->fastcmp, (const unsigned char *) db_get_string (value),
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
hash_lookup_key (const HASH_LOOKUP_TABLE *table, int side, const DB_VALUE *value, unsigned int *hash_key)
{
  const DOMAIN_COMPARE *compare = &table->compare;
  if (compare->conv[side] == NULL)
    {
      return hash_lookup_value (table, value, hash_key);
    }
  DB_VALUE converted;
  bool hashed = false;
  if (tp_value_convert (compare->conv[side], compare->target[side], value, &converted) == DOMAIN_COMPATIBLE)
    {
      hashed = hash_lookup_value (table, &converted, hash_key);
    }
  pr_clear_value (&converted);
  return hashed;
}

/*
 * The hash of a subquery's list, owned by the subquery's XASL node (xasl_node::hash_lookup). Only an uncorrelated
 * subquery has one: it fills its list once in a query, and qexec_clear_xasl frees the hash at the query's end. The
 * thread that executes the list builds the table; a PX scan worker's clone of the subquery shares the leader's list
 * and table (hash_lookup_share). The hash is in the global heap: a parallel aptr worker may build it for a subquery
 * another thread clears
 */

/*
 * hash_lookup_create () - a hash over the table, or with no table yet
 *   return: the hash, or NULL with an error set
 *   owns_table(in): false: the table is the leader's, which the leader frees
 */
static HASH_LOOKUP *
hash_lookup_create (THREAD_ENTRY *thread_p, HASH_LOOKUP_TABLE *table, bool owns_table)
{
  HL_HEAPID old_heap = db_change_private_heap (thread_p, 0);
  HASH_LOOKUP *hash = (HASH_LOOKUP *) db_private_alloc (thread_p, sizeof (HASH_LOOKUP));
  (void) db_change_private_heap (thread_p, old_heap);
  if (hash == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (HASH_LOOKUP));
      return NULL;
    }
  /* no list scan open, and a key on no table (HASH_METH_NOT_USE is 0) */
  memset (hash, 0, sizeof (HASH_LOOKUP));
  hash->table = table;
  hash->owns_table = owns_table;
  return hash;
}

/*
 * hash_lookup_destroy () - frees a hash hash_lookup_create made, with its table if it owns it (qexec_clear_xasl)
 */
void
hash_lookup_destroy (THREAD_ENTRY *thread_p, HASH_LOOKUP *hash)
{
  HL_HEAPID old_heap = db_change_private_heap (thread_p, 0);

  if (hash->scan_open)
    {
      qfile_close_scan (thread_p, &hash->list_scan);
    }
  pr_clear_value (&hash->value);
  if (hash->owns_table && hash->table != NULL)
    {
      qdata_hscan_destroy (thread_p, &hash->table->hash_scan);
      db_private_free (thread_p, hash->table);
    }
  db_private_free (thread_p, hash);
  (void) db_change_private_heap (thread_p, old_heap);
}

/*
 * hash_lookup_destroy_tree () - frees the hashes in an XASL tree: a parallel aptr worker frees those of the tree it ran
 *			       as it clears the tree (qexec_clear_xasl_for_parallel_aptr does not)
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
 * hash_lookup_share () - a PX scan worker's clone of the subquery looks its list up through the leader's table
 *   return: error code
 *   subquery(in): the clone, whose term reads the leader's list
 *   leader(in): the leader's node, which executed the list (eval_hash_lookup_prepare); with no table, the clone's
 *		 lookups read the list
 */
int
hash_lookup_share (THREAD_ENTRY *thread_p, xasl_node *subquery, const xasl_node *leader)
{
  assert (subquery->hash_lookup == NULL);
  subquery->hash_lookup =
	  hash_lookup_create (thread_p, leader->hash_lookup != NULL ? leader->hash_lookup->table : NULL, false);
  return subquery->hash_lookup != NULL ? NO_ERROR : er_errid ();
}

/*
 * hash_lookup_build () - the table over the list's values for the comparison; table->unusable instead when a value
 *			has no hash
 *   return: error code
 *   method(in): the table's method (qdata_hscan_method)
 */
static int
hash_lookup_build (THREAD_ENTRY *thread_p, HASH_LOOKUP_TABLE *table, QFILE_LIST_ID *list_id,
		   const DOMAIN_COMPARE *compare, HASH_METHOD method)
{
  QFILE_LIST_SCAN_ID list_scan;
  QFILE_TUPLE_RECORD tuple_record = QFILE_TUPLE_RECORD_INITIALIZER;
  SCAN_CODE qp_scan;
  DB_VALUE value;
  unsigned int hash_key;
  bool is_null, hashed;
  int error;

  error = qfile_open_list_scan (list_id, &list_scan);
  if (error != NO_ERROR)
    {
      table->unusable = true;
      return error;
    }
  error = qdata_hscan_create (thread_p, &table->hash_scan, method, list_id->tuple_cnt, "Eval Hash");
  if (error != NO_ERROR)
    {
      qfile_close_scan (thread_p, &list_scan);
      table->unusable = true;
      return error;
    }
  table->compare = *compare;
  table->lang_coll = NULL;
  table->fastcmp = LANG_FASTCMP_OTHER;
  if (TP_IS_CHAR_TYPE (compare->cmp->id))
    {
      table->lang_coll = lang_get_collation (compare->collation);
      table->fastcmp = lang_get_coll_fastcmp (table->lang_coll);
    }

  while ((qp_scan = qfile_scan_list_next (thread_p, &list_scan, &tuple_record, PEEK)) == S_SUCCESS)
    {
      error = qfile_slot_read_column_value (&tuple_record, 0, list_id->type_list.domp[0], &value, false, &is_null);
      if (error != NO_ERROR)
	{
	  break;
	}
      if (is_null)
	{
	  table->has_null = true;
	  continue;
	}
      hashed = hash_lookup_key (table, 1, &value, &hash_key);
      /* read in place, but a compressed string is decompressed into its own buffer */
      pr_clear_value (&value);
      if (!hashed)
	{
	  table->unusable = true;
	  break;
	}
      qdata_hscan_set_key (&table->hash_scan, hash_key);
      error = qdata_hscan_insert (thread_p, &table->hash_scan, &list_scan, &tuple_record);
      if (error != NO_ERROR)
	{
	  break;
	}
    }
  if (error == NO_ERROR && !table->unusable && qp_scan != S_END)
    {
      assert_release_error (er_errid () != NO_ERROR);
      error = er_errid ();
    }
  /* a HYBRID table keeps the tuples' positions, which the lookups read through scans of their own */
  qfile_close_scan (thread_p, &list_scan);

  if (error != NO_ERROR || table->unusable)
    {
      qdata_hscan_destroy (thread_p, &table->hash_scan);
      table->unusable = true;
    }
  return error;
}

/*
 * hash_lookup_new_table () - the subquery's table over its list for the comparison, or one the lookups do not use:
 *			    they read the list
 *   return: error code; no table for a comparison the hash cannot answer for, which a later lookup checks again
 */
static int
hash_lookup_new_table (THREAD_ENTRY *thread_p, xasl_node *subquery, QFILE_LIST_ID *list_id,
		       const DOMAIN_COMPARE *compare)
{
  const UINT64 mem_limit = (UINT64) prm_get_bigint_value (PRM_ID_MAX_HASH_LIST_SCAN_SIZE);
  /* 0 turns hashing a list off, as it does for a hash list scan */
  const HASH_METHOD method = mem_limit == 0 ? HASH_METH_NOT_USE : qdata_hscan_method (list_id, mem_limit);
  HASH_LOOKUP_TABLE *table;
  HL_HEAPID old_heap;
  int error = NO_ERROR;

  /* checked here, once a table would be built, rather than at each lookup */
  if (method != HASH_METH_NOT_USE && method != HASH_METH_HASH_FILE && !hash_lookup_comparable (compare))
    {
      return NO_ERROR;
    }

  old_heap = db_change_private_heap (thread_p, 0);
  table = (HASH_LOOKUP_TABLE *) db_private_alloc (thread_p, sizeof (HASH_LOOKUP_TABLE));
  if (table == NULL)
    {
      (void) db_change_private_heap (thread_p, old_heap);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (HASH_LOOKUP_TABLE));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  /* no table (HASH_METH_NOT_USE is 0) */
  memset (table, 0, sizeof (HASH_LOOKUP_TABLE));
  if (method == HASH_METH_NOT_USE || method == HASH_METH_HASH_FILE)
    {
      /* a hash file costs more to build than reading the list for a few lookups: the lookups read the list */
      table->unusable = true;
    }
  else
    {
      error = hash_lookup_build (thread_p, table, list_id, compare, method);
    }
  (void) db_change_private_heap (thread_p, old_heap);
  subquery->hash_lookup->table = table;

  if (error != NO_ERROR && error != ER_INTERRUPTED && er_errid () != ER_INTERRUPTED)
    {
      /* as a hash join falls back to a simpler method: the lookups read the list */
      er_clear ();
      error = NO_ERROR;
    }
  if (error == NO_ERROR && !table->unusable)
    {
      subquery->xasl_stats.hash_lookup_builds++;
      subquery->xasl_stats.hash_lookup_method = table->hash_scan.hash_list_scan_type;
    }
  return error;
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
  HASH_LOOKUP *hash_lookup;
  HL_HEAPID old_heap;
  int error;

  *hash = NULL;
  if (subquery->hash_lookup == NULL)
    {
      subquery->hash_lookup = hash_lookup_create (thread_p, NULL, true);
      if (subquery->hash_lookup == NULL)
	{
	  return er_errid ();
	}
    }
  hash_lookup = subquery->hash_lookup;

  if (hash_lookup->table == NULL && hash_lookup->owns_table)
    {
      error = hash_lookup_new_table (thread_p, subquery, list_id, compare);
      if (error != NO_ERROR)
	{
	  return error;
	}
    }
  if (hash_lookup->table == NULL || hash_lookup->table->unusable
      || !hash_lookup_same_compare (&hash_lookup->table->compare, compare))
    {
      /* none built, or built for a term other than this one */
      return NO_ERROR;
    }

  if (!hash_lookup->scan_open)
    {
      old_heap = db_change_private_heap (thread_p, 0);
      error = qfile_open_list_scan (list_id, &hash_lookup->list_scan);
      (void) db_change_private_heap (thread_p, old_heap);
      if (error != NO_ERROR)
	{
	  return error;
	}
      hash_lookup->scan_open = true;
      /* PX scan workers read the leader's list by positions in any order: read latches, as PX list scans take */
      hash_lookup->list_scan.is_read_only = true;
      hash_lookup->hash_scan = hash_lookup->table->hash_scan;
      qfile_slot_set_layout (&hash_lookup->tuple_record, &hash_lookup->list_scan.list_id.type_list);
    }
  /* the list the table was built over: the subquery has not filled it again */
  assert (hash_lookup->list_scan.list_id.tuple_cnt == list_id->tuple_cnt
	  && VPID_EQ (&hash_lookup->list_scan.list_id.first_vpid, &list_id->first_vpid));

  *hash = hash_lookup;
  return NO_ERROR;
}

/*
 * hash_lookup_start () - starts the list values of the item's hash (hash_lookup_next)
 *   return: false if the item has no hash in the type its comparison compares it in: the caller reads the list
 */
bool
hash_lookup_start (HASH_LOOKUP *hash, const DB_VALUE *item)
{
  unsigned int hash_key;

  if (!hash_lookup_key (hash->table, 0, item, &hash_key))
    {
      return false;
    }
  qdata_hscan_set_key (&hash->hash_scan, hash_key);
  qfile_slot_reset (&hash->tuple_record);
  return true;
}

/*
 * hash_lookup_has_null () - the list holds a NULL: a lookup that finds no equal value is UNKNOWN, not FALSE
 */
bool
hash_lookup_has_null (const HASH_LOOKUP *hash)
{
  return hash->table->has_null;
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
  /* read in the looking-up thread's own heap, not the hash's: freed here, before another thread frees the hash */
  if (hash->list_scan.tplrec.tpl != NULL)
    {
      /* a tuple larger than a page, assembled there */
      db_private_free_and_init (thread_p, hash->list_scan.tplrec.tpl);
      hash->list_scan.tplrec.size = 0;
    }
  pr_clear_value (&hash->value);
}
