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
 * of one execution of the subquery on (hash_lookup_get), a hash table over the list in memory answers them; a list too
 * large for memory is read instead. A value is hashed in the type its comparison compares it in, and only the list
 * values of the same hash are given back, for the caller to compare by that same comparison. A value the hash cannot
 * take as its comparison compares it (a conversion that fails, a type left out) sends that lookup, or the whole
 * execution, back to reading the list.
 *
 * Only an uncorrelated subquery's list is hashed (eval_some_list_lookup), and one that executes again builds its table
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
#include "thread_manager.hpp"
#include "xasl.h"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/* the table over a subquery's list: built once, then only read, by the lookups of the subquery's node and of the PX
 * scan workers' clones of it that share the list (hash_lookup_bind_clone) */
typedef struct hash_lookup_table HASH_LOOKUP_TABLE;
struct hash_lookup_table
{
  MHT_HLS_TABLE *hash_table;	/* NULL: the lookups read the list (a comparison or a value the table cannot take, or a
				 * list too large for memory) */
  HASH_METHOD method;		/* HASH_METH_IN_MEM (the tuples) or HASH_METH_HYBRID (their positions) */
  bool has_null;		/* the list holds a NULL */
  DOMAIN_COMPARE compare;	/* the comparison the table is built for: a copy, as each clone's term has its own */
};

/*
 * One subquery node's lookups of its list, owned by the node (xasl_node::hash_lookup) and in the global heap: a
 * parallel aptr worker may build it for a subquery another thread clears. The node's own table is freed with it; a PX
 * scan worker's clone borrows the leader's table until hash_lookup_unbind_clone.
 */
struct hash_lookup
{
  HASH_LOOKUP_TABLE *table;	/* NULL: none yet */
  bool scan_open;		/* list_scan is open */
  QFILE_LIST_SCAN_ID list_scan;	/* a scan of the list: a HYBRID table's tuples are read by their positions through it */
  unsigned int hash_key;	/* the hash of the item hash_lookup_start started */
  MHT_HLS_ENTRY *entry;		/* the entry of the tuple the last hash_lookup_next found */
  QFILE_TUPLE_RECORD tuple_record;	/* that tuple */
  DB_VALUE value;		/* that tuple's list value */
  HASH_LOOKUP_STATS stats;	/* kept until the subquery's XASL is cleared, past hash_lookup_release */
};

static bool hash_lookup_type (DB_TYPE type);
static bool hash_lookup_comparable (const DOMAIN_COMPARE *compare);
static bool hash_lookup_value (const DOMAIN_COMPARE *compare, const DB_VALUE *value, unsigned int *hash_key);
static bool hash_lookup_valhash (const DB_VALUE *value, unsigned int *hash_key);
static bool hash_lookup_key (const DOMAIN_COMPARE *compare, int side, const DB_VALUE *value, unsigned int *hash_key);
static HASH_LOOKUP *hash_lookup_create (THREAD_ENTRY *thread_p);
static int hash_lookup_new_table (THREAD_ENTRY *thread_p, HASH_LOOKUP *hash, QFILE_LIST_ID *list_id,
				  const DOMAIN_COMPARE *compare);
static int hash_lookup_build (THREAD_ENTRY *thread_p, HASH_LOOKUP_TABLE *table, QFILE_LIST_ID *list_id,
			      HASH_METHOD method);
static void hash_lookup_free_table (THREAD_ENTRY *thread_p, HASH_LOOKUP_TABLE *table);

/*
 * hash_lookup_type () - a type whose values hash_lookup_value hashes alike when their comparison finds them equal
 *
 * A list of its own beside domain_hashable_resolve (): the comparison here gives the type it compares in, not the two
 * sides' domains.
 */
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
 * hash_lookup_comparable () - a comparison the hash can answer for: it compares the two values as they are or converts
 *			     each once, in a type hash_lookup_type takes. A string brought to an ENUM's codeset is left
 *			     out: hash_lookup_key converts by conv and target alone.
 */
static bool
hash_lookup_comparable (const DOMAIN_COMPARE *compare)
{
  return (compare->method == DOMAIN_COMPARE_DIRECT || compare->method == DOMAIN_COMPARE_CONVERT)
	 && compare->codeset_side < 0 && compare->cmp != NULL && hash_lookup_type (compare->cmp->id);
}

/*
 * hash_lookup_value () - the hash of a value in the type its comparison compares it in: values the comparison finds
 *			equal hash alike
 *   return: false if the value has no such hash
 */
static bool
hash_lookup_value (const DOMAIN_COMPARE *compare, const DB_VALUE *value, unsigned int *hash_key)
{
  const DB_TYPE type = DB_VALUE_DOMAIN_TYPE (value);

  if (TP_IS_CHAR_TYPE (compare->cmp->id))
    {
      /* a string hashes by its collation, which must be the comparison's; CHAR and VARCHAR hash alike */
      if (!TP_IS_CHAR_TYPE (type) || db_get_string_collation (value) != compare->collation)
	{
	  return false;
	}
    }
  else if (type != compare->cmp->id)
    {
      return false;
    }

  return hash_lookup_valhash (value, hash_key);
}

/*
 * hash_lookup_valhash () - mht_valhash () as CBRD-28060 makes it, for the types hash_lookup_type takes: values their
 *			    comparison finds equal hash alike. Once CBRD-28060 merges, hash_lookup_value calls
 *			    mht_valhash () in its place.
 *   return: false for a string of a collation it does not follow (the lookup reads the list)
 */
static bool
hash_lookup_valhash (const DB_VALUE *value, unsigned int *hash_key)
{
  switch (DB_VALUE_DOMAIN_TYPE (value))
    {
    case DB_TYPE_NUMERIC:
    {
      uint8_t calc_buf[DB_NUMERIC_BUF_SIZE];
      int precision, scale;
      bool is_float_numeric;

      /* 1.0 and 1.00 alike: the trailing zeros of the scale left out, as mht_get_hash_number () does for a float
       * NUMERIC */
      db_get_numeric_precision_and_scale (value, &precision, &scale, &is_float_numeric);
      if (scale == 0 || (precision == DB_MAX_NUMERIC_PRECISION && scale < 0))
	{
	  *hash_key = numeric_repr_fold32 (db_locate_numeric (value));
	}
      else
	{
	  float_numeric_normalize_for_hash ((DB_C_NUMERIC) db_locate_numeric (value), calc_buf, precision, scale);
	  *hash_key = numeric_repr_fold32 (calc_buf);
	}
      return true;
    }

    case DB_TYPE_CHAR:
    case DB_TYPE_VARCHAR:
    {
      const unsigned char *str = (const unsigned char *) db_get_string (value);
      const int size = db_get_string_size (value);
      const unsigned int *weights;
      unsigned int hash = 0, weight;
      int i, zeros = 0;

      /* the collations lang_fastcmp_byte () compares: it weighs a space 0 and any other byte by the collation's
       * weights, and leaves out trailing weights 0 when it ignores trailing spaces */
      switch (db_get_string_collation (value))
	{
	case LANG_COLL_ISO_BINARY:
	case LANG_COLL_UTF8_BINARY:
	case LANG_COLL_ISO_EN_CS:
	case LANG_COLL_ISO_EN_CI:
	case LANG_COLL_UTF8_EN_CS:
	case LANG_COLL_UTF8_EN_CI:
	  break;
	default:
	  return false;
	}

      weights = lang_get_collation (db_get_string_collation (value))->coll.weights;
      for (i = 0; i < size; i++)
	{
	  weight = (str[i] == ' ') ? 0 : weights[str[i]];
	  if (weight == 0)
	    {
	      /* hashed only if a weight other than 0 follows */
	      zeros++;
	      continue;
	    }
	  for (; zeros > 0; zeros--)
	    {
	      hash = hash * 31;
	    }
	  hash = hash * 31 + weight;
	}
      *hash_key = hash;
      return true;
    }

    default:
      *hash_key = qdata_hash_scan_value (value, UINT_MAX);
      return true;
    }
}

/*
 * hash_lookup_key () - the hash of a comparison side's value, converted as the comparison converts that side
 *   return: false if the conversion fails or the value has no hash
 *   side(in): 0 the row's item, 1 a list value
 */
static bool
hash_lookup_key (const DOMAIN_COMPARE *compare, int side, const DB_VALUE *value, unsigned int *hash_key)
{
  DB_VALUE converted;
  bool hashed = false;

  if (compare->conv[side] == NULL)
    {
      return hash_lookup_value (compare, value, hash_key);
    }

  if (tp_value_convert (compare->conv[side], compare->target[side], value, &converted) == DOMAIN_COMPATIBLE)
    {
      hashed = hash_lookup_value (compare, &converted, hash_key);
    }
  pr_clear_value (&converted);
  return hashed;
}

/*
 * hash_lookup_create () - a node's lookups with no table yet
 *   return: the hash, or NULL with an error set
 */
static HASH_LOOKUP *
hash_lookup_create (THREAD_ENTRY *thread_p)
{
  HL_HEAPID save_heap_id;
  HASH_LOOKUP *hash;

  save_heap_id = db_change_private_heap (thread_p, 0);
  hash = (HASH_LOOKUP *) db_private_alloc (thread_p, sizeof (HASH_LOOKUP));
  (void) db_change_private_heap (thread_p, save_heap_id);
  if (hash == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (HASH_LOOKUP));
      return NULL;
    }

  /* no table, no list scan open, and a NULL value */
  memset (hash, 0, sizeof (HASH_LOOKUP));
  return hash;
}

/*
 * hash_lookup_new_table () - the node's table over its list for the comparison, with no hash table when the lookups
 *			    read the list
 *   return: error code
 */
static int
hash_lookup_new_table (THREAD_ENTRY *thread_p, HASH_LOOKUP *hash, QFILE_LIST_ID *list_id,
		       const DOMAIN_COMPARE *compare)
{
  const HASH_METHOD method =
	  qdata_hash_scan_method (list_id, (UINT64) prm_get_bigint_value (PRM_ID_MAX_HASH_LIST_SCAN_SIZE));
  HL_HEAPID save_heap_id;
  HASH_LOOKUP_TABLE *table;
  int error = NO_ERROR;

  save_heap_id = db_change_private_heap (thread_p, 0);
  table = (HASH_LOOKUP_TABLE *) db_private_alloc (thread_p, sizeof (HASH_LOOKUP_TABLE));
  if (table == NULL)
    {
      (void) db_change_private_heap (thread_p, save_heap_id);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (HASH_LOOKUP_TABLE));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  /* no hash table */
  memset (table, 0, sizeof (HASH_LOOKUP_TABLE));
  table->compare = *compare;

  /* a hash file costs more to build than reading the list for a few lookups; an empty list has no method. A term's
   * comparison stays, so the later lookups of a table without a hash table keep reading the list */
  if ((method == HASH_METH_IN_MEM || method == HASH_METH_HYBRID) && hash_lookup_comparable (compare))
    {
      error = hash_lookup_build (thread_p, table, list_id, method);
    }
  (void) db_change_private_heap (thread_p, save_heap_id);
  hash->table = table;

  if (error != NO_ERROR && error != ER_INTERRUPTED && er_errid () != ER_INTERRUPTED)
    {
      /* as a hash join falls back to a simpler method: the lookups read the list */
      er_clear ();
      error = NO_ERROR;
    }
  if (table->hash_table != NULL)
    {
      hash->stats.builds++;
      hash->stats.method = method;
    }
  return error;
}

/*
 * hash_lookup_build () - the hash table over the list's values for the table's comparison; none when a value has no
 *			hash
 *   return: error code
 *   method(in): HASH_METH_IN_MEM or HASH_METH_HYBRID (qdata_hash_scan_method)
 */
static int
hash_lookup_build (THREAD_ENTRY *thread_p, HASH_LOOKUP_TABLE *table, QFILE_LIST_ID *list_id, HASH_METHOD method)
{
  QFILE_LIST_SCAN_ID list_scan;
  QFILE_TUPLE_RECORD tuple_record = QFILE_TUPLE_RECORD_INITIALIZER;
  MHT_HLS_ENTRY *entry;
  SCAN_CODE qp_scan;
  DB_VALUE value;
  unsigned int hash_key;
  bool is_null, hashed = true;
  int error;

  error = qfile_open_list_scan (list_id, &list_scan);
  if (error != NO_ERROR)
    {
      return error;
    }
  table->hash_table = mht_create_hls ("Hash Lookup", (int) list_id->tuple_cnt, NULL, NULL);
  if (table->hash_table == NULL)
    {
      qfile_close_scan (thread_p, &list_scan);
      assert_release_error (er_errid () != NO_ERROR);
      return er_errid ();
    }
  table->method = method;

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
      hashed = hash_lookup_key (&table->compare, 1, &value, &hash_key);
      /* read in place, but a compressed string is decompressed into its own buffer */
      pr_clear_value (&value);
      if (!hashed)
	{
	  break;
	}

      /* IN_MEM keeps a copy of the tuple, HYBRID its position */
      entry = (method == HASH_METH_IN_MEM
	       ? qdata_alloc_hscan_value (thread_p, table->hash_table->heap_id, tuple_record.tpl)
	       : qdata_alloc_hscan_value_OID (thread_p, table->hash_table->heap_id, &list_scan));
      if (entry == NULL || mht_put_hls (table->hash_table, &hash_key, entry) == NULL)
	{
	  assert_release_error (er_errid () != NO_ERROR);
	  error = er_errid ();
	  break;
	}
    }
  if (error == NO_ERROR && hashed && qp_scan != S_END)
    {
      assert_release_error (er_errid () != NO_ERROR);
      error = er_errid ();
    }
  /* a HYBRID table keeps the tuples' positions, which the lookups read through scans of their own */
  qfile_close_scan (thread_p, &list_scan);

  if (error != NO_ERROR || !hashed)
    {
      mht_destroy_hls (table->hash_table);
      table->hash_table = NULL;
    }
  return error;
}

/*
 * hash_lookup_free_table () - frees a table hash_lookup_new_table made, in the global heap the caller switched to
 */
static void
hash_lookup_free_table (THREAD_ENTRY *thread_p, HASH_LOOKUP_TABLE *table)
{
  if (table->hash_table != NULL)
    {
      mht_destroy_hls (table->hash_table);
    }
  db_private_free (thread_p, table);
}

/*
 * hash_lookup_get () - the hash that answers a lookup of the subquery's list
 *   return: error code
 *   subquery(in): the subquery whose list list_id is; it owns the hash
 *   compare(in): the lookup's comparison; the hash table is built only for one hash_lookup_comparable takes
 *   hash(out): the hash with its hash table, or NULL: the caller reads the list
 */
int
hash_lookup_get (THREAD_ENTRY *thread_p, xasl_node *subquery, QFILE_LIST_ID *list_id,
		 const DOMAIN_COMPARE *compare, HASH_LOOKUP **hash)
{
  HASH_LOOKUP *hash_lookup;
  HL_HEAPID save_heap_id;
  int error;

  *hash = NULL;
  if (subquery->hash_lookup == NULL)
    {
      subquery->hash_lookup = hash_lookup_create (thread_p);
      if (subquery->hash_lookup == NULL)
	{
	  return er_errid ();
	}
    }
  hash_lookup = subquery->hash_lookup;

  if (hash_lookup->table == NULL)
    {
      error = hash_lookup_new_table (thread_p, hash_lookup, list_id, compare);
      if (error != NO_ERROR)
	{
	  return error;
	}
    }
  if (hash_lookup->table->hash_table == NULL)
    {
      return NO_ERROR;
    }

  if (!hash_lookup->scan_open)
    {
      save_heap_id = db_change_private_heap (thread_p, 0);
      error = qfile_open_list_scan (list_id, &hash_lookup->list_scan);
      (void) db_change_private_heap (thread_p, save_heap_id);
      if (error != NO_ERROR)
	{
	  return error;
	}
      hash_lookup->scan_open = true;
      /* PX scan workers read the leader's list by positions in any order: read latches, as PX list scans take */
      hash_lookup->list_scan.is_read_only = true;
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
  if (!hash_lookup_key (&hash->table->compare, 0, item, &hash->hash_key))
    {
      return false;
    }
  qfile_slot_reset (&hash->tuple_record);
  hash->stats.lookups++;
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
  const HASH_LOOKUP_TABLE *table = hash->table;
  QFILE_TUPLE_POSITION tuple_position;
  MHT_HLS_ENTRY *entry;
  bool is_null;

  /* a reset tuple record: the item's first entry */
  if (hash->tuple_record.tpl == NULL)
    {
      entry = mht_get_hls (table->hash_table, &hash->hash_key, &hash->entry);
    }
  else
    {
      entry = mht_get_next_hls (table->hash_table, &hash->hash_key, &hash->entry);
    }
  if (entry == NULL)
    {
      return S_END;
    }

  if (table->method == HASH_METH_IN_MEM)
    {
      /* the payload is a tuple of the list, owned by the table */
      qfile_slot_set_tuple_ptr_and_layout (&hash->tuple_record, (QFILE_TUPLE) MHT_HLS_ENTRY_PAYLOAD (entry), 0,
					   &hash->list_scan.list_id.type_list);
    }
  else
    {
      MAKE_TUPLE_POSTION (tuple_position, (QFILE_TUPLE_SIMPLE_POS *) MHT_HLS_ENTRY_PAYLOAD (entry), &hash->list_scan);
      if (qfile_jump_scan_tuple_position (thread_p, &hash->list_scan, &tuple_position, &hash->tuple_record, PEEK)
	  != S_SUCCESS)
	{
	  assert_release_error (er_errid () != NO_ERROR);
	  return S_ERROR;
	}
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

/*
 * hash_lookup_bind_clone () - a PX scan worker's clone of the subquery borrows the leader's table
 *			     (hash_lookup_unbind_clone)
 *   return: error code
 *   clone(in): the clone, whose term reads the leader's list
 *   orig(in): the leader's node, which executed the list (eval_hash_lookup_prepare)
 */
int
hash_lookup_bind_clone (THREAD_ENTRY *thread_p, xasl_node *clone, const xasl_node *orig)
{
  assert (clone->hash_lookup == NULL);
  if (orig->hash_lookup == NULL)
    {
      /* the leader's term looked nothing up, and the clone's, the same term, looks nothing up either */
      return NO_ERROR;
    }

  clone->hash_lookup = hash_lookup_create (thread_p);
  if (clone->hash_lookup == NULL)
    {
      return er_errid ();
    }
  assert (orig->hash_lookup->table != NULL);
  clone->hash_lookup->table = orig->hash_lookup->table;
  return NO_ERROR;
}

/*
 * hash_lookup_unbind_clone () - a bound clone drops the leader's table before the clone is cleared, so the clear frees
 *				 the clone's hash alone
 *
 * A bound clone has no hash or the leader's table: the leader builds no hash only for a term that looks nothing up
 * (eval_hash_lookup_prepare), and the clone's term is the same term.
 */
void
hash_lookup_unbind_clone (xasl_node *clone)
{
  if (clone->hash_lookup != NULL)
    {
      clone->hash_lookup->table = NULL;
    }
}

/*
 * hash_lookup_release () - frees what the hash holds and keeps the hash with its stats for the trace. Called before
 *			    the subquery's list is cleared (qexec_clear_xasl, qexec_clear_xasl_for_parallel_aptr): the
 *			    table must not outlive the list, which a later execution fills again.
 */
void
hash_lookup_release (THREAD_ENTRY *thread_p, HASH_LOOKUP *hash)
{
  HL_HEAPID save_heap_id = db_change_private_heap (thread_p, 0);

  if (hash->scan_open)
    {
      qfile_close_scan (thread_p, &hash->list_scan);
      hash->scan_open = false;
    }
  if (hash->table != NULL)
    {
      hash_lookup_free_table (thread_p, hash->table);
      hash->table = NULL;
    }
  (void) db_change_private_heap (thread_p, save_heap_id);
}

/*
 * hash_lookup_destroy () - frees a hash with what it holds (qexec_clear_xasl)
 */
void
hash_lookup_destroy (THREAD_ENTRY *thread_p, HASH_LOOKUP *hash)
{
  HL_HEAPID save_heap_id;

  hash_lookup_release (thread_p, hash);
  save_heap_id = db_change_private_heap (thread_p, 0);
  db_private_free (thread_p, hash);
  (void) db_change_private_heap (thread_p, save_heap_id);
}

/*
 * hash_lookup_get_stats () - the hash's stats for the trace
 */
const HASH_LOOKUP_STATS *
hash_lookup_get_stats (const HASH_LOOKUP *hash)
{
  return &hash->stats;
}

/*
 * hash_lookup_merge_stats () - adds the stats of a PX scan worker's clone of a subquery to the leader's node
 *				(xasl_merge_stats); the leader's node gets a hash for them if it has none
 */
void
hash_lookup_merge_stats (xasl_node *dst, const xasl_node *src)
{
  const HASH_LOOKUP_STATS *src_stats;
  HASH_LOOKUP_STATS *dst_stats;

  if (src->hash_lookup == NULL || src->hash_lookup->stats.builds + src->hash_lookup->stats.lookups == 0)
    {
      return;
    }
  if (dst->hash_lookup == NULL)
    {
      dst->hash_lookup = hash_lookup_create (thread_get_thread_entry_info ());
      if (dst->hash_lookup == NULL)
	{
	  /* only the trace loses the stats */
	  er_clear ();
	  return;
	}
    }

  src_stats = &src->hash_lookup->stats;
  dst_stats = &dst->hash_lookup->stats;
  dst_stats->builds += src_stats->builds;
  dst_stats->lookups += src_stats->lookups;
  if (src_stats->builds > 0)
    {
      dst_stats->method = src_stats->method;
    }
}
