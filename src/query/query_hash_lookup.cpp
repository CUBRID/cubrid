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
};

/*
 * One subquery node's lookups of its list, owned by the node (xasl_node::hash_lookup) and in the global heap
 * (hash_lookup_create ()). The node's own table is freed with it; a PX scan worker's clone borrows the leader's table
 * until hash_lookup_unbind_clone.
 */
struct hash_lookup
{
  HASH_LOOKUP_TABLE *table;	/* NULL: none yet */

  QFILE_LIST_SCAN_ID list_scan;	/* a scan of the list: a HYBRID table's tuples are read by their positions through it */
  bool scan_open;		/* list_scan is open */

  unsigned int hash_key;	/* the lookup of the item hash_lookup_start started */
  MHT_HLS_ENTRY *entry;		/* the entry of the tuple the last hash_lookup_next found */
  QFILE_TUPLE_RECORD tuple_record;	/* that tuple */
  DB_VALUE value;		/* that tuple's list value */

  HASH_LOOKUP_STATS stats;	/* kept until the subquery's XASL is cleared, past hash_lookup_release */
  TSC_TICKS start_tick;		/* the start of the lookup hash_lookup_start started, while traced */
  bool on_trace;		/* the lookup is timed: hash_lookup_end adds its time */
};

/* the hash of a value: the comparisons and the values it takes */
static bool hash_lookup_type (DB_TYPE type);
static bool hash_lookup_comparable (const DOMAIN_COMPARE *compare);
static bool hash_lookup_key (const DOMAIN_COMPARE *compare, int side, const DB_VALUE *value, unsigned int *hash_key);
static bool hash_lookup_value_hash (const DB_VALUE *value, unsigned int *hash_key);

/* a node's lookups and their table */
static HASH_LOOKUP *hash_lookup_create (THREAD_ENTRY *thread_p);
static int hash_lookup_create_table (THREAD_ENTRY *thread_p, HASH_LOOKUP *lookup, QFILE_LIST_ID *list_id,
				     const DOMAIN_COMPARE *compare);
static int hash_lookup_build (THREAD_ENTRY *thread_p, HASH_LOOKUP_TABLE *table, QFILE_LIST_ID *list_id,
			      const DOMAIN_COMPARE *compare, HASH_METHOD method);
static void hash_lookup_destroy_table (THREAD_ENTRY *thread_p, HASH_LOOKUP_TABLE *table);

/*
 * hash_lookup_type () - whether hash_lookup_key () hashes the values of a type as their comparison compares them
 *   return: true for such a type
 *   type(in): the type a comparison compares in
 *
 * Reason:
 *   The list is its own, beside domain_hashable_resolve (): a comparison here gives the type it compares in, not the
 *   two sides' domains.
 *
 * Note:
 *   Once CBRD-28060 merges, domain_hashable () can decide this for the type's default domain on both sides, with
 *   domain_hash_type (): the types whose hash follows their comparison whatever their domains, and CHAR and VARCHAR,
 *   whose collation hash_lookup_key () checks on each value. An ENUM and a collection stay out: the comparison does not
 *   give the two sides' domains their hash depends on.
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
 * hash_lookup_comparable () - whether the hash can answer for a comparison
 *   return: true for a comparison the hash follows
 *   compare(in): the comparison of a lookup's item with the list's column
 *
 * Behavior:
 *   The comparison compares the two values as they are or converts each once, in a type hash_lookup_type () takes.
 *
 * Reason:
 *   A string brought to an ENUM's codeset is left out: hash_lookup_key () converts by conv and target alone.
 */
static bool
hash_lookup_comparable (const DOMAIN_COMPARE *compare)
{
  assert (compare != NULL);

  return (compare->method == DOMAIN_COMPARE_DIRECT || compare->method == DOMAIN_COMPARE_CONVERT)
	 && compare->codeset_side < 0 && compare->cmp != NULL && hash_lookup_type (compare->cmp->id);
}

/*
 * hash_lookup_key () - get the hash key of a comparison side's value
 *   return: false if the conversion fails or the value has no hash key
 *   compare(in): the comparison
 *   side(in): 0 the row's item, 1 a list value
 *   value(in): the value
 *   hash_key(out): the hash key
 *
 * Behavior:
 *   - The value is converted as the comparison converts that side, and hashed in the type the comparison compares it
 *     in: values the comparison finds equal get one hash key.
 *   - A string hashes by its collation, which must be the comparison's. CHAR and VARCHAR hash alike.
 *
 * Steps:
 *   Step 1: convert the value as the comparison converts that side.
 *   Step 2: check that it is of the type the comparison compares in, a string of the comparison's collation.
 *   Step 3: hash it.
 */
static bool
hash_lookup_key (const DOMAIN_COMPARE *compare, int side, const DB_VALUE *value, unsigned int *hash_key)
{
  DB_VALUE converted;
  const DB_VALUE *compared = value;
  DB_TYPE type;
  bool in_compare_type, hashed = false;

  assert (compare != NULL && compare->cmp != NULL);
  assert (side == 0 || side == 1);
  assert (value != NULL);
  assert (hash_key != NULL);

  /* Step 1: the value as the comparison converts it */
  db_make_null (&converted);
  if (compare->conv[side] != NULL)
    {
      if (tp_value_convert (compare->conv[side], compare->target[side], value, &converted) != DOMAIN_COMPATIBLE)
	{
	  pr_clear_value (&converted);
	  return false;
	}
      compared = &converted;
    }

  /* Step 2: the type the comparison compares in */
  type = DB_VALUE_DOMAIN_TYPE (compared);
  if (TP_IS_CHAR_TYPE (compare->cmp->id))
    {
      in_compare_type = TP_IS_CHAR_TYPE (type) && db_get_string_collation (compared) == compare->collation;
    }
  else
    {
      in_compare_type = (type == compare->cmp->id);
    }

  /* Step 3: the hash key */
  if (in_compare_type)
    {
      hashed = hash_lookup_value_hash (compared, hash_key);
    }

  pr_clear_value (&converted);
  return hashed;
}

/*
 * hash_lookup_value_hash () - hash a value of a type hash_lookup_type () takes, as mht_valhash () does from
 *   CBRD-28060 on
 *   return: false for a string of a collation it does not follow: the lookup reads the list
 *   value(in): the value
 *   hash_key(out): the hash
 *
 * Behavior:
 *   Values their comparison finds equal hash alike:
 *     - a number, a date or a time by qdata_hash_scan_value (): -0.0 and 0.0 alike, every NaN alike
 *     - a NUMERIC with the trailing zeros of its scale left out: 1.0 and 1.00 alike
 *     - a CHAR or VARCHAR of a collation lang_fastcmp_byte () compares, by its weights: a space weighs 0, and the
 *       trailing weights 0 are left out as a comparison that ignores trailing spaces leaves them out
 *
 * Method:
 *   A NUMERIC goes through float_numeric_normalize_for_hash () and numeric_repr_fold32 (), as mht_get_hash_number ()
 *   hashes a float NUMERIC.
 *
 * Reason:
 *   mht_valhash () follows the comparison only from CBRD-28060 on.
 *
 * Note:
 *   Once CBRD-28060 merges, mht_valhash () replaces this function: hash_lookup_key () sets the hash key to
 *   mht_valhash (compared, UINT_MAX).
 */
static bool
hash_lookup_value_hash (const DB_VALUE *value, unsigned int *hash_key)
{
  assert (value != NULL);
  assert (hash_key != NULL);

  switch (DB_VALUE_DOMAIN_TYPE (value))
    {
    case DB_TYPE_NUMERIC:
    {
      uint8_t calc_buf[DB_NUMERIC_BUF_SIZE];
      int precision, scale;
      bool is_float_numeric;

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
 * hash_lookup_create () - create a node's lookups with no table yet
 *   return: the hash lookup, or NULL with an error set
 *   thread_p(in): thread entry
 *
 * Reason:
 *   The hash lookup, its table and its scan of the list are in the global heap, not in the thread's private heap: a
 *   parallel aptr worker may build them for a subquery whose XASL another thread clears, and a block of a thread's
 *   private heap is freed only by that thread.
 */
static HASH_LOOKUP *
hash_lookup_create (THREAD_ENTRY *thread_p)
{
  HL_HEAPID save_heap_id;
  HASH_LOOKUP *lookup;

  save_heap_id = db_change_private_heap (thread_p, 0);
  lookup = (HASH_LOOKUP *) db_private_alloc (thread_p, sizeof (HASH_LOOKUP));
  (void) db_change_private_heap (thread_p, save_heap_id);
  if (lookup == NULL)
    {
      ASSERT_ERROR ();
      return NULL;
    }

  /* no table, no list scan open, and a NULL value */
  memset (lookup, 0, sizeof (HASH_LOOKUP));

  return lookup;
}

/*
 * hash_lookup_create_table () - make the node's table over its list for a comparison
 *   return: error code
 *   thread_p(in): thread entry
 *   lookup(in/out): the node's hash lookup, which gets the table
 *   list_id(in): the subquery's list
 *   compare(in): the comparison of the lookup that makes the table
 *
 * Behavior:
 *   - The table gets a hash table only for a list that fits in memory (HASH_METH_IN_MEM, HASH_METH_HYBRID) and a
 *     comparison hash_lookup_comparable () takes. Without one the lookups read the list.
 *   - A build that fails reads the list too, as a hash join falls back to a simpler method. Only an interrupt is an
 *     error.
 *
 * Steps:
 *   Step 1: allocate the table in the global heap, with no hash table.
 *   Step 2: build the hash table for a list that fits in memory and a comparison hash_lookup_comparable () takes.
 *   Step 3: give the table to the hash lookup; a build that failed reads the list too.
 *   Step 4: count the build for the trace.
 *
 * Reason:
 *   - A hash file costs more to build than reading the list for a few lookups.
 *   - A term's comparison stays for the execution, so the later lookups of a table without a hash table keep reading
 *     the list without building again.
 */
static int
hash_lookup_create_table (THREAD_ENTRY *thread_p, HASH_LOOKUP *lookup, QFILE_LIST_ID *list_id,
			  const DOMAIN_COMPARE *compare)
{
  const HASH_METHOD method =
	  qdata_hash_scan_method (list_id, (UINT64) prm_get_bigint_value (PRM_ID_MAX_HASH_LIST_SCAN_SIZE));
  HL_HEAPID save_heap_id;
  HASH_LOOKUP_TABLE *table;
  TSC_TICKS start_tick, end_tick;
  TSCTIMEVAL tv_diff;
  bool on_trace;
  int error = NO_ERROR;

  assert (lookup != NULL && lookup->table == NULL);
  assert (list_id != NULL);
  assert (compare != NULL);

  /* Step 1: the table, in the global heap with its hash lookup (hash_lookup_create ()) */
  save_heap_id = db_change_private_heap (thread_p, 0);
  table = (HASH_LOOKUP_TABLE *) db_private_alloc (thread_p, sizeof (HASH_LOOKUP_TABLE));
  (void) db_change_private_heap (thread_p, save_heap_id);
  if (table == NULL)
    {
      ASSERT_ERROR ();
      return er_errid ();
    }

  /* no hash table */
  memset (table, 0, sizeof (HASH_LOOKUP_TABLE));

  /* Step 2: the hash table */
  if ((method == HASH_METH_IN_MEM || method == HASH_METH_HYBRID) && hash_lookup_comparable (compare))
    {
      on_trace = thread_is_on_trace (thread_p);
      if (on_trace)
	{
	  tsc_getticks (&start_tick);
	}

      error = hash_lookup_build (thread_p, table, list_id, compare, method);

      if (on_trace)
	{
	  tsc_getticks (&end_tick);
	  tsc_elapsed_time_usec (&tv_diff, end_tick, start_tick);
	  TSC_ADD_TIMEVAL (lookup->stats.build_time, tv_diff);
	}
    }

  /* Step 3: the hash lookup's table */
  lookup->table = table;

  if (error != NO_ERROR)
    {
      if (error == ER_INTERRUPTED || er_errid () == ER_INTERRUPTED)
	{
	  return ER_INTERRUPTED;
	}

      /* as a hash join falls back to a simpler method: the lookups read the list */
      er_clear ();
    }

  /* Step 4: the trace */
  if (table->hash_table != NULL)
    {
      lookup->stats.builds++;
      lookup->stats.method = method;
    }

  ASSERT_NO_ERROR_OR_INTERRUPTED ();
  return NO_ERROR;
}

/*
 * hash_lookup_build () - build the hash table over the list's values for a comparison
 *   return: error code
 *   thread_p(in): thread entry
 *   table(in/out): the table, with no hash table yet
 *   list_id(in): the subquery's list
 *   compare(in): the comparison of the lookup that makes the table
 *   method(in): HASH_METH_IN_MEM or HASH_METH_HYBRID (qdata_hash_scan_method ())
 *
 * Behavior:
 *   - IN_MEM keeps a copy of each tuple, HYBRID its position.
 *   - The list's NULLs are not in the hash table; has_null records them.
 *   - The table keeps no hash table when a value has no hash or the build fails.
 *
 * Steps:
 *   Step 1: open a scan of the list and create the hash table.
 *   Step 2: put each list value's tuple under the value's hash key.
 *   Step 3: close the scan, and drop the hash table if the build stopped.
 */
static int
hash_lookup_build (THREAD_ENTRY *thread_p, HASH_LOOKUP_TABLE *table, QFILE_LIST_ID *list_id,
		   const DOMAIN_COMPARE *compare, HASH_METHOD method)
{
  QFILE_LIST_SCAN_ID list_scan;
  QFILE_TUPLE_RECORD tuple_record = QFILE_TUPLE_RECORD_INITIALIZER;
  MHT_HLS_ENTRY *entry;
  SCAN_CODE qp_scan;
  DB_VALUE value;
  unsigned int hash_key;
  bool is_null, hashed = true;
  int error;

  assert (table != NULL && table->hash_table == NULL);
  assert (list_id != NULL);
  assert (compare != NULL);
  assert (method == HASH_METH_IN_MEM || method == HASH_METH_HYBRID);

  /* Step 1: the scan and the hash table */
  error = qfile_open_list_scan (list_id, &list_scan);
  if (error != NO_ERROR)
    {
      return error;
    }

  table->hash_table = mht_create_hls ("Hash Lookup", (int) list_id->tuple_cnt, NULL, NULL);
  if (table->hash_table == NULL)
    {
      qfile_close_scan (thread_p, &list_scan);
      ASSERT_ERROR ();
      return er_errid ();
    }

  table->method = method;

  /* Step 2: each value: a NULL is recorded, a value with no hash key stops the build */
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

      hashed = hash_lookup_key (compare, 1, &value, &hash_key);
      /* read in place, but a compressed string is decompressed into its own buffer */
      pr_clear_value (&value);
      if (!hashed)
	{
	  break;
	}

      entry = (method == HASH_METH_IN_MEM
	       ? qdata_alloc_hscan_value (thread_p, table->hash_table->heap_id, tuple_record.tpl)
	       : qdata_alloc_hscan_value_OID (thread_p, table->hash_table->heap_id, &list_scan));
      if (entry == NULL)
	{
	  ASSERT_ERROR ();
	  error = er_errid ();
	  break;
	}

      if (mht_put_hls (table->hash_table, &hash_key, entry) == NULL)
	{
	  ASSERT_ERROR ();
	  error = er_errid ();
	  break;
	}
    }

  if (error == NO_ERROR && hashed && qp_scan != S_END)
    {
      ASSERT_ERROR ();
      error = er_errid ();
    }

  /* Step 3: close the scan; a HYBRID table keeps the tuples' positions, which the lookups read through scans of their
   * own */
  qfile_close_scan (thread_p, &list_scan);

  if (error != NO_ERROR || !hashed)
    {
      mht_destroy_hls (table->hash_table);
      table->hash_table = NULL;
    }

  return error;
}

/*
 * hash_lookup_destroy_table () - free a table hash_lookup_create_table () made
 *   thread_p(in): thread entry
 *   table(in): the table, in the global heap the caller switched to
 */
static void
hash_lookup_destroy_table (THREAD_ENTRY *thread_p, HASH_LOOKUP_TABLE *table)
{
  assert (table != NULL);

  if (table->hash_table != NULL)
    {
      mht_destroy_hls (table->hash_table);
    }
  db_private_free (thread_p, table);
}

/*
 * hash_lookup_get () - get the hash that answers a lookup of the subquery's list
 *   return: error code
 *   thread_p(in): thread entry
 *   subquery(in): the subquery whose list list_id is; it owns the hash
 *   list_id(in): the subquery's list
 *   compare(in): the lookup's comparison
 *   lookup(out): the hash lookup with its hash table, or NULL: the caller reads the list
 *
 * Behavior:
 *   - The first lookup of an execution makes the table (hash_lookup_create_table ()).
 *   - The hash opens a scan of the list of its own, through which it reads a HYBRID table's tuples by their positions.
 *
 * Steps:
 *   Step 1: the subquery's hash lookup, created by its first lookup.
 *   Step 2: the table, made by the first lookup of an execution; without a hash table the caller reads the list.
 *   Step 3: the scan of the list, opened once per hash lookup.
 */
int
hash_lookup_get (THREAD_ENTRY *thread_p, xasl_node *subquery, QFILE_LIST_ID *list_id,
		 const DOMAIN_COMPARE *compare, HASH_LOOKUP **lookup)
{
  HASH_LOOKUP *subquery_lookup;
  HL_HEAPID save_heap_id;
  int error;

  if (subquery == NULL || list_id == NULL || compare == NULL || lookup == NULL)
    {
      assert_release_error (false);
      return er_errid ();
    }

  *lookup = NULL;

  /* Step 1: the hash lookup */
  if (subquery->hash_lookup == NULL)
    {
      subquery->hash_lookup = hash_lookup_create (thread_p);
      if (subquery->hash_lookup == NULL)
	{
	  ASSERT_ERROR ();
	  return er_errid ();
	}
    }
  subquery_lookup = subquery->hash_lookup;

  /* Step 2: the table */
  if (subquery_lookup->table == NULL)
    {
      error = hash_lookup_create_table (thread_p, subquery_lookup, list_id, compare);
      if (error != NO_ERROR)
	{
	  return error;
	}
    }

  if (subquery_lookup->table->hash_table == NULL)
    {
      return NO_ERROR;
    }

  /* Step 3: the scan, once per hash lookup and in the global heap with it: it stays open for the execution's lookups,
   * and hash_lookup_release () closes it */
  if (!subquery_lookup->scan_open)
    {
      save_heap_id = db_change_private_heap (thread_p, 0);
      error = qfile_open_list_scan (list_id, &subquery_lookup->list_scan);
      (void) db_change_private_heap (thread_p, save_heap_id);
      if (error != NO_ERROR)
	{
	  return error;
	}
      subquery_lookup->scan_open = true;

      /* PX scan workers read the leader's list by positions in any order: read latches, as PX list scans take */
      subquery_lookup->list_scan.is_read_only = true;

      qfile_slot_set_layout (&subquery_lookup->tuple_record, &subquery_lookup->list_scan.list_id.type_list);
    }

  /* the list the table was built over: the subquery has not filled it again */
  assert (subquery_lookup->list_scan.list_id.tuple_cnt == list_id->tuple_cnt
	  && VPID_EQ (&subquery_lookup->list_scan.list_id.first_vpid, &list_id->first_vpid));

  *lookup = subquery_lookup;

  ASSERT_NO_ERROR_OR_INTERRUPTED ();
  return NO_ERROR;
}

/*
 * hash_lookup_start () - start the list values of an item's hash (hash_lookup_next ())
 *   return: false if the item has no hash in the type its comparison compares it in: the caller reads the list
 *   thread_p(in): thread entry
 *   lookup(in/out): the hash lookup
 *   compare(in): the lookup's comparison, which compares the item with the list values hash_lookup_next () gives
 *   item(in): the row's item
 */
bool
hash_lookup_start (THREAD_ENTRY *thread_p, HASH_LOOKUP *lookup, const DOMAIN_COMPARE *compare, const DB_VALUE *item)
{
  if (lookup == NULL || lookup->table == NULL || lookup->table->hash_table == NULL || compare == NULL || item == NULL)
    {
      /* the caller reads the list */
      assert (false);
      return false;
    }

  /* the time of a lookup runs from its hash key to hash_lookup_end, the comparisons of its list values included */
  lookup->on_trace = thread_is_on_trace (thread_p);
  if (lookup->on_trace)
    {
      tsc_getticks (&lookup->start_tick);
    }

  if (!hash_lookup_key (compare, 0, item, &lookup->hash_key))
    {
      /* the caller reads the list: no hash_lookup_end follows */
      lookup->on_trace = false;
      return false;
    }

  qfile_slot_reset (&lookup->tuple_record);
  lookup->stats.lookups++;
  return true;
}

/*
 * hash_lookup_has_null () - whether the list holds a NULL
 *   return: true if it does: a lookup that finds no equal value is UNKNOWN, not FALSE
 *   lookup(in): the hash lookup
 */
bool
hash_lookup_has_null (const HASH_LOOKUP *lookup)
{
  if (lookup == NULL || lookup->table == NULL)
    {
      assert (false);
      return false;
    }

  return lookup->table->has_null;
}

/*
 * hash_lookup_next () - get the next list value of the item's hash
 *   return: S_SUCCESS, S_END or S_ERROR
 *   thread_p(in): thread entry
 *   lookup(in/out): the hash lookup
 *   value(out): the list value, read in place; it holds until the next call
 *
 * Steps:
 *   Step 1: the next entry of the item's hash key.
 *   Step 2: the entry's tuple: an IN_MEM table holds it, a HYBRID table its position in the list.
 *   Step 3: the tuple's list value.
 */
SCAN_CODE
hash_lookup_next (THREAD_ENTRY *thread_p, HASH_LOOKUP *lookup, DB_VALUE **value)
{
  const HASH_LOOKUP_TABLE *table;
  QFILE_TUPLE_POSITION tuple_position;
  MHT_HLS_ENTRY *entry;
  bool is_null;

  if (lookup == NULL || lookup->table == NULL || lookup->table->hash_table == NULL || value == NULL)
    {
      assert_release_error (false);
      return S_ERROR;
    }

  table = lookup->table;

  /* Step 1: the next entry; a reset tuple record starts at the item's first */
  if (lookup->tuple_record.tpl == NULL)
    {
      entry = mht_get_hls (table->hash_table, &lookup->hash_key, &lookup->entry);
    }
  else
    {
      entry = mht_get_next_hls (table->hash_table, &lookup->hash_key, &lookup->entry);
    }
  if (entry == NULL)
    {
      return S_END;
    }

  /* Step 2: the tuple */
  if (table->method == HASH_METH_IN_MEM)
    {
      /* the payload is a tuple of the list, owned by the table */
      qfile_slot_set_tuple_ptr_and_layout (&lookup->tuple_record, (QFILE_TUPLE) MHT_HLS_ENTRY_PAYLOAD (entry), 0,
					   &lookup->list_scan.list_id.type_list);
    }
  else
    {
      MAKE_TUPLE_POSTION (tuple_position, (QFILE_TUPLE_SIMPLE_POS *) MHT_HLS_ENTRY_PAYLOAD (entry), &lookup->list_scan);
      if (qfile_jump_scan_tuple_position (thread_p, &lookup->list_scan, &tuple_position, &lookup->tuple_record, PEEK)
	  != S_SUCCESS)
	{
	  ASSERT_ERROR ();
	  return S_ERROR;
	}
    }

  /* Step 3: the list value */
  pr_clear_value (&lookup->value);
  if (qfile_slot_read_column_value (&lookup->tuple_record, 0, lookup->list_scan.list_id.type_list.domp[0],
				    &lookup->value, false, &is_null) != NO_ERROR)
    {
      /* a failed read may have freed what it left in the value */
      db_make_null (&lookup->value);
      return S_ERROR;
    }

  /* the list's NULLs are not in the table */
  assert (!is_null);

  lookup->stats.rows++;
  *value = &lookup->value;
  return S_SUCCESS;
}

/*
 * hash_lookup_end () - end the list values of an item
 *   thread_p(in): thread entry
 *   lookup(in/out): the hash lookup
 *
 * Reason:
 *   A tuple read by its position keeps its list page fixed, so the page is unfixed between rows, as a hash list scan
 *   does when a probe ends.
 */
void
hash_lookup_end (THREAD_ENTRY *thread_p, HASH_LOOKUP *lookup)
{
  TSC_TICKS end_tick;
  TSCTIMEVAL tv_diff;

  if (lookup == NULL)
    {
      assert (false);
      return;
    }

  if (lookup->list_scan.position == S_ON && lookup->list_scan.curr_pgptr != NULL)
    {
      qmgr_free_old_page_and_init (thread_p, lookup->list_scan.curr_pgptr, lookup->list_scan.list_id.tfile_vfid);
      lookup->list_scan.position = S_AFTER;
    }

  /* read in the looking-up thread's own heap, not the hash's: freed here, before another thread frees the hash */
  if (lookup->list_scan.tplrec.tpl != NULL)
    {
      /* a tuple larger than a page, assembled there */
      db_private_free_and_init (thread_p, lookup->list_scan.tplrec.tpl);
      lookup->list_scan.tplrec.size = 0;
    }

  pr_clear_value (&lookup->value);

  if (lookup->on_trace)
    {
      tsc_getticks (&end_tick);
      tsc_elapsed_time_usec (&tv_diff, end_tick, lookup->start_tick);
      TSC_ADD_TIMEVAL (lookup->stats.lookup_time, tv_diff);
      lookup->on_trace = false;
    }
}

/*
 * hash_lookup_bind_clone () - lend the leader's table to a PX scan worker's clone of the subquery
 *   return: error code
 *   thread_p(in): thread entry
 *   clone(in/out): the clone, whose term reads the leader's list
 *   leader(in): the leader's node, which executed the list (eval_hash_lookup_prepare ())
 *
 * Behavior:
 *   - The clone gives the table back with hash_lookup_unbind_clone ().
 *   - A leader with no hash lends none: its term looked nothing up, and the clone's term is the same term.
 */
int
hash_lookup_bind_clone (THREAD_ENTRY *thread_p, xasl_node *clone, const xasl_node *leader)
{
  if (clone == NULL || leader == NULL)
    {
      assert_release_error (false);
      return er_errid ();
    }

  assert (clone->hash_lookup == NULL);

  if (leader->hash_lookup == NULL)
    {
      return NO_ERROR;
    }

  clone->hash_lookup = hash_lookup_create (thread_p);
  if (clone->hash_lookup == NULL)
    {
      ASSERT_ERROR ();
      return er_errid ();
    }

  assert (leader->hash_lookup->table != NULL);
  clone->hash_lookup->table = leader->hash_lookup->table;

  ASSERT_NO_ERROR_OR_INTERRUPTED ();
  return NO_ERROR;
}

/*
 * hash_lookup_unbind_clone () - drop the leader's table a clone borrowed, before the clone is cleared
 *   clone(in/out): the clone
 *
 * Behavior:
 *   The clone's clear then frees the clone's hash alone.
 *
 * Reason:
 *   A bound clone has no hash or the leader's table: the leader builds no hash only for a term that looks nothing up
 *   (eval_hash_lookup_prepare ()), and the clone's term is the same term.
 */
void
hash_lookup_unbind_clone (xasl_node *clone)
{
  if (clone != NULL && clone->hash_lookup != NULL)
    {
      clone->hash_lookup->table = NULL;
    }
}

/*
 * hash_lookup_release () - free what the subquery node's hash lookup holds, and keep the hash lookup with its stats
 *   for the trace
 *   thread_p(in): thread entry
 *   subquery(in/out): the subquery node; NULL, or one with no hash lookup, is left as it is
 *
 * Reason:
 *   It is called before the subquery's list is cleared (qexec_clear_xasl (), qexec_clear_xasl_for_parallel_aptr ()):
 *   the table must not outlive the list, which a later execution fills again.
 */
void
hash_lookup_release (THREAD_ENTRY *thread_p, xasl_node *subquery)
{
  HASH_LOOKUP *lookup;
  HL_HEAPID save_heap_id;

  if (subquery == NULL || subquery->hash_lookup == NULL)
    {
      return;
    }

  lookup = subquery->hash_lookup;

  /* the global heap the hash lookup is in (hash_lookup_create ()) */
  save_heap_id = db_change_private_heap (thread_p, 0);

  if (lookup->scan_open)
    {
      qfile_close_scan (thread_p, &lookup->list_scan);
      lookup->scan_open = false;
    }

  if (lookup->table != NULL)
    {
      hash_lookup_destroy_table (thread_p, lookup->table);
      lookup->table = NULL;
    }

  (void) db_change_private_heap (thread_p, save_heap_id);
}

/*
 * hash_lookup_destroy () - free the subquery node's hash lookup with what it holds
 *   thread_p(in): thread entry
 *   subquery(in/out): the subquery node, which has no hash lookup after; NULL, or one with none, is left as it is
 */
void
hash_lookup_destroy (THREAD_ENTRY *thread_p, xasl_node *subquery)
{
  HL_HEAPID save_heap_id;

  if (subquery == NULL || subquery->hash_lookup == NULL)
    {
      return;
    }

  hash_lookup_release (thread_p, subquery);

  /* the global heap the hash lookup is in (hash_lookup_create ()) */
  save_heap_id = db_change_private_heap (thread_p, 0);
  db_private_free_and_init (thread_p, subquery->hash_lookup);
  (void) db_change_private_heap (thread_p, save_heap_id);
}

/*
 * hash_lookup_get_stats () - get the hash's stats for the trace
 *   return: the stats
 *   lookup(in): the hash lookup
 */
const HASH_LOOKUP_STATS *
hash_lookup_get_stats (const HASH_LOOKUP *lookup)
{
  if (lookup == NULL)
    {
      assert (false);
      return NULL;
    }

  return &lookup->stats;
}

/*
 * hash_lookup_merge_stats () - merge the stats of a PX scan worker's clone of a subquery into the leader's node
 *   leader(in/out): the leader's node (xasl_merge_stats ())
 *   clone(in): the clone
 *
 * Behavior:
 *   The leader's node gets a hash for the stats if it has none. Failing to make one loses only the trace's stats.
 */
void
hash_lookup_merge_stats (xasl_node *leader, const xasl_node *clone)
{
  const HASH_LOOKUP_STATS *clone_stats;
  HASH_LOOKUP_STATS *leader_stats;

  if (leader == NULL || clone == NULL)
    {
      assert (false);
      return;
    }

  if (clone->hash_lookup == NULL || clone->hash_lookup->stats.builds + clone->hash_lookup->stats.lookups == 0)
    {
      return;
    }

  if (leader->hash_lookup == NULL)
    {
      leader->hash_lookup = hash_lookup_create (thread_get_thread_entry_info ());
      if (leader->hash_lookup == NULL)
	{
	  /* only the trace's stats are lost: the merge has no caller to return an error to */
	  er_clear ();
	  return;
	}
    }

  clone_stats = &clone->hash_lookup->stats;
  leader_stats = &leader->hash_lookup->stats;

  /* the workers' lookup times as a parallel hash join's probe times: their range, while the counts add up */
  if (leader_stats->workers == 0 || timercmp (&clone_stats->lookup_time, &leader_stats->lookup_time_min, <))
    {
      leader_stats->lookup_time_min = clone_stats->lookup_time;
    }
  if (leader_stats->workers == 0 || timercmp (&clone_stats->lookup_time, &leader_stats->lookup_time_max, >))
    {
      leader_stats->lookup_time_max = clone_stats->lookup_time;
    }
  leader_stats->workers++;

  TSC_ADD_TIMEVAL (leader_stats->build_time, clone_stats->build_time);
  leader_stats->builds += clone_stats->builds;
  leader_stats->lookups += clone_stats->lookups;
  leader_stats->rows += clone_stats->rows;
  if (clone_stats->builds > 0)
    {
      leader_stats->method = clone_stats->method;
    }
}
