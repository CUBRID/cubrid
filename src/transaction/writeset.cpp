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
 * writeset.cpp - writeset collection and global commit history for parallel applylogdb
 */

#ident "$Id$"

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <atomic>
#include <new>

#include "writeset.hpp"

#include "error_manager.h"
#include "log_impl.h"
#include "object_domain.h"
#include "object_primitive.h"
#include "object_representation.h"
#include "language_support.h"
#include "porting.h"
#include "system_parameter.h"
#include "db_value_printer.hpp"
#include "string_buffer.hpp"

#include "memory_wrapper.hpp" // XXX: SHOULD BE THE LAST INCLUDE HEADER

#if defined(SERVER_MODE) || defined(SA_MODE)

/* FNV-1a 64-bit constants */
#define LOG_WSET_FNV_OFFSET_BASIS ((UINT64) 0xcbf29ce484222325ULL)
#define LOG_WSET_FNV_PRIME        ((UINT64) 0x00000100000001b3ULL)

/* Global WRITE/REF commit history indexed by writeset key hash. */
LOG_WSET_HISTORY wset_History;

struct wset_statistics
{
  std::atomic<UINT64> overflow_count { 0 };
  std::atomic<UINT64> history_clear_count { 0 };
  std::atomic<UINT64> commit_order_fallback_count { 0 };
  std::atomic<UINT64> write_publish_count { 0 };
  std::atomic<UINT64> ref_publish_count { 0 };
};

/* TODO(CBRD-27508): expose these process-local counters through the selected monitoring interface. */
static wset_statistics wset_Statistics;
static int wset_Trace_level = 0;

/* Tracks whether the history locks were initialized.
 * Initialization and finalization are expected to run on a single thread. */
static bool wset_History_initialized = false;

static UINT64 wset_fnv1a (const OID *class_oid, const VFID *index_vfid, const char *packed, int len);
static UINT64 wset_string_hash (const OID *class_oid, const VFID *index_vfid, DB_VALUE *v);
static const char *wset_kind_name (LOG_WSET_KIND kind);
static const char *wset_fallback_reason_name (WSET_FALLBACK_REASON reason);
static void wset_trace_collected_key (LOG_TDES *tdes, const OID *class_oid, const VFID *index_vfid,
				      DB_VALUE *key, UINT64 hash, LOG_WSET_KIND kind);
static int wset_error (int error, const char *detail);
static int wset_cast_value_to_domain (DB_VALUE *source, DB_VALUE *destination, TP_DOMAIN *domain);
static int wset_cast_midxkey_element (DB_MIDXKEY *fk_midxkey, int index, TP_DOMAIN *parent_domain,
				      DB_VALUE *parent_value, bool *has_reference);
static int wset_make_parent_midxkey (DB_VALUE *fk_value, TP_DOMAIN *parent_pk_domain, DB_VALUE *parent_key,
				     bool *has_reference);
static int wset_push_hash (LOG_TDES *tdes, UINT64 hash, LOG_WSET_KIND kind);
static void wset_advance_commit_baseline (const LOG_LSA *commit_lsa);
static void wset_clear_history_with_lock (const LOG_LSA *commit_lsa, LOG_LSA *history_start_out);
static bool wset_history_has_capacity (size_t publish_count);
static void wset_publish_keys (LOG_TDES *tdes, const LOG_LSA *commit_lsa);
static void wset_clear_overflow_history (LOG_TDES *tdes, const LOG_LSA *commit_lsa);
static void wset_publish_with_capacity (LOG_TDES *tdes, const LOG_LSA *commit_lsa);

#define WSET_TRACE(level, ...) \
  do \
    { \
      if (wset_Trace_level >= (level)) \
	{ \
	  _er_log_debug (ARG_FILE_LINE, "[WSET] " __VA_ARGS__); \
	} \
    } \
  while (0)

static const char *
wset_kind_name (LOG_WSET_KIND kind)
{
  return kind == LOG_WSET_KIND_WRITE ? "WRITE" : "REF";
}

static const char *
wset_fallback_reason_name (WSET_FALLBACK_REASON reason)
{
  switch (reason)
    {
    case WSET_FALLBACK_TRANSACTION_LIMIT:
      return "TRANSACTION_KEY_LIMIT";
    case WSET_FALLBACK_STATEMENT_REPLICATION:
      return "STATEMENT_REPLICATION";
    default:
      return "UNKNOWN";
    }
}

static void
wset_trace_collected_key (LOG_TDES *tdes, const OID *class_oid, const VFID *index_vfid,
			  DB_VALUE *key, UINT64 hash, LOG_WSET_KIND kind)
{
  char value_text[257];
  string_buffer value;
  const char *printed;
  size_t i;

  if (wset_Trace_level < 2)
    {
      return;
    }

  value_text[0] = '\0';
  if (wset_Trace_level >= 3)
    {
      db_sprint_value (key, value);
      printed = value.get_buffer ();
      if (printed == NULL)
	{
	  printed = "";
	}
      snprintf (value_text, sizeof (value_text), "%.*s", (int) sizeof (value_text) - 1, printed);
      for (i = 0; value_text[i] != '\0'; i++)
	{
	  if (value_text[i] == '\n' || value_text[i] == '\r' || value_text[i] == '\t')
	    {
	      value_text[i] = ' ';
	    }
	}
    }

  WSET_TRACE (2,
	      "COLLECT trid=%d kind=%s hash=%016llx class_oid=%d|%d|%d index_vfid=%d|%d%s%s\n",
	      tdes->trid, wset_kind_name (kind), (unsigned long long) hash,
	      class_oid->volid, class_oid->pageid, class_oid->slotid,
	      index_vfid->volid, index_vfid->fileid,
	      wset_Trace_level >= 3 ? " key=" : "", wset_Trace_level >= 3 ? value_text : "");
}

bool
wset_trace_enabled (int level)
{
  return wset_Trace_level >= level;
}

void
wset_trace_key_source (LOG_TDES *tdes, const char *operation, const char *image, LOG_WSET_KIND kind,
		       const OID *table_oid, const OID *row_oid, const char *table_name, const char *constraint_name,
		       const OID *key_class_oid, const VFID *key_index_vfid)
{
  if (wset_Trace_level < 3 || tdes == NULL)
    {
      return;
    }

  WSET_TRACE (3,
	      "SOURCE trid=%d operation=%s image=%s kind=%s table=%s table_oid=%d|%d|%d row_oid=%d|%d|%d constraint=%s key_class_oid=%d|%d|%d key_index_vfid=%d|%d\n",
	      tdes->trid, operation, image, wset_kind_name (kind),
	      table_name != NULL ? table_name : "<unknown>", table_oid->volid, table_oid->pageid,
	      table_oid->slotid, row_oid->volid, row_oid->pageid, row_oid->slotid,
	      constraint_name != NULL ? constraint_name : "<unnamed>", key_class_oid->volid,
	      key_class_oid->pageid, key_class_oid->slotid, key_index_vfid->volid, key_index_vfid->fileid);
}

/*
 * wset_fnv1a - hash a constraint key as class OID, index VFID, and packed key
 *
 * return: writeset key hash
 *
 * Note: The index VFID separates key spaces of different indexes in the same class.
 *       An FK REF uses the referenced parent class and index so it matches the
 *       parent's WRITE hash.
 */
static UINT64
wset_fnv1a (const OID *class_oid, const VFID *index_vfid, const char *packed, int len)
{
  UINT64 hash = LOG_WSET_FNV_OFFSET_BASIS;
  unsigned char oid_bytes[8];
  unsigned char vfid_bytes[6];
  int i;

  /* class_oid: 8 bytes = pageid (4) + slotid (2) + volid (2) */
  memcpy (&oid_bytes[0], &class_oid->pageid, sizeof (class_oid->pageid));
  memcpy (&oid_bytes[4], &class_oid->slotid, sizeof (class_oid->slotid));
  memcpy (&oid_bytes[6], &class_oid->volid, sizeof (class_oid->volid));

  for (i = 0; i < 8; i++)
    {
      hash ^= (UINT64) oid_bytes[i];
      hash *= LOG_WSET_FNV_PRIME;
    }

  /* index VFID: 6 bytes = fileid (4) + volid (2) */
  memcpy (&vfid_bytes[0], &index_vfid->fileid, sizeof (index_vfid->fileid));
  memcpy (&vfid_bytes[4], &index_vfid->volid, sizeof (index_vfid->volid));

  for (i = 0; i < 6; i++)
    {
      hash ^= (UINT64) vfid_bytes[i];
      hash *= LOG_WSET_FNV_PRIME;
    }

  for (i = 0; i < len; i++)
    {
      hash ^= (UINT64) (unsigned char) packed[i];
      hash *= LOG_WSET_FNV_PRIME;
    }

  return hash;
}

/*
 * wset_string_hash - hash a character key according to its index collation
 *
 *   class_oid(in): class OID used by the writeset key
 *   index_vfid(in): index VFID used by the writeset key
 *   v(in): non-NULL string value carrying the target index collation
 *
 * return: writeset key hash
 */
static UINT64
wset_string_hash (const OID *class_oid, const VFID *index_vfid, DB_VALUE *v)
{
  LANG_COLLATION *lc;
  const unsigned char *s;
  unsigned int pseudo;
  unsigned char pbytes[5];

  lc = lang_get_collation (db_get_string_collation (v));
  s = (const unsigned char *) db_get_string (v);
  assert (lc != NULL);
  assert (s != NULL);

  /* Hash the collation pseudo key instead of the raw string bytes. This makes
   * collation-equal strings share a writeset hash without unconditional case folding. */
  pseudo = lc->mht2str (lc, s, db_get_string_size (v));

  /* 0xC5 tag keeps a string pseudo key from colliding with a numeric key that
   * might pack to the same four bytes on the same class. */
  pbytes[0] = 0xC5;
  pbytes[1] = (unsigned char) (pseudo & 0xff);
  pbytes[2] = (unsigned char) ((pseudo >> 8) & 0xff);
  pbytes[3] = (unsigned char) ((pseudo >> 16) & 0xff);
  pbytes[4] = (unsigned char) ((pseudo >> 24) & 0xff);

  return wset_fnv1a (class_oid, index_vfid, (const char *) pbytes, 5);
}

/*
 * wset_history_initialize - initialize the global writeset history
 *
 * Note: The global writeset history is initialized once during cub_server startup and finalized
 *       once at shutdown. Caches ha_writeset_history_size and does not access domains or DB_VALUEs.
 *
 * TODO: If the commit LSA sequence can be reinitialized while cub_server is running, clear the
 *       history before processing new transactions.
 */
void
wset_history_initialize (void)
{
  if (wset_History_initialized)
    {
      /* already initialized: do not re-create the locks */
      return;
    }

  pthread_rwlock_init (&wset_History.history_lock, NULL);
  pthread_mutex_init (&wset_History.seq_lock, NULL);

  wset_History.map.clear ();
  LSA_SET_NULL (&wset_History.history_start);
  LSA_SET_NULL (&wset_History.prev_commit_lsa);
  wset_History.history_size = (INT64) prm_get_integer_value (PRM_ID_HA_WRITESET_HISTORY_SIZE);
  wset_Trace_level = prm_get_integer_value (PRM_ID_HA_WRITESET_TRACE_LEVEL);

  wset_History_initialized = true;

  WSET_TRACE (1, "INITIALIZE history_size=%lld trace_level=%d\n",
	      (long long) wset_History.history_size, wset_Trace_level);
}

/*
 * wset_history_finalize - release the global commit history
 *
 * Note: Runs once at final shutdown when no transaction is probing or publishing, so the locks can
 *       be destroyed safely.
 */
void
wset_history_finalize (void)
{
  if (!wset_History_initialized)
    {
      /* init never ran (boot error path) or already finalized: nothing to destroy */
      return;
    }

  wset_History.map.clear ();
  LSA_SET_NULL (&wset_History.history_start);
  LSA_SET_NULL (&wset_History.prev_commit_lsa);

  pthread_mutex_destroy (&wset_History.seq_lock);
  pthread_rwlock_destroy (&wset_History.history_lock);

  wset_History_initialized = false;
  wset_Trace_level = 0;
}

/*
 * wset_push_hash - append one collected key hash to the transaction's writeset
 *
 *   tdes(in/out): transaction descriptor
 *   hash(in): the key hash
 *   kind(in): WRITE (this transaction owns the key) or REF (foreign-key reference only)
 *
 * return: NO_ERROR
 *
 * Note: The public collection functions skip calls when collection is disabled. When the
 *       per-transaction limit is exceeded here, every collected key is dropped and the transaction
 *       falls back to commit order (see wset_overflow in log_impl.h). REF hashes are counted against
 *       the same limit because they use the same per-transaction memory.
 */
static int
wset_push_hash (LOG_TDES *tdes, UINT64 hash, LOG_WSET_KIND kind)
{
  LOG_WSET_ENTRY entry;

  assert (tdes != NULL);

  if ((INT64) tdes->wset_hashes.size () >= wset_History.history_size)
    {
      wset_Statistics.overflow_count.fetch_add (1, std::memory_order_relaxed);
      wset_fallback_to_commit_order (tdes, WSET_FALLBACK_TRANSACTION_LIMIT);
      return NO_ERROR;
    }

  entry.hash = hash;
  entry.kind = kind;
  try
    {
      tdes->wset_hashes.push_back (entry);
    }
  catch (const std::bad_alloc &)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      (tdes->wset_hashes.size () + 1) * sizeof (LOG_WSET_ENTRY));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  return NO_ERROR;
}

/* Drop a partial writeset and use the commit-order fallback for the whole transaction. */
void
wset_fallback_to_commit_order (LOG_TDES *tdes, WSET_FALLBACK_REASON reason)
{
  assert (tdes != NULL);
  if (tdes->wset_overflow)
    {
      return;
    }

  WSET_TRACE (1, "FALLBACK trid=%d reason=%s collected=%zu limit=%lld\n",
	      tdes->trid, wset_fallback_reason_name (reason), tdes->wset_hashes.size (),
	      (long long) wset_History.history_size);

  tdes->wset_overflow = true;
  std::vector<LOG_WSET_ENTRY> ().swap (tdes->wset_hashes);
  wset_Statistics.commit_order_fallback_count.fetch_add (1, std::memory_order_relaxed);
}

/*
 * wset_add_write_key - pack a WRITE constraint key value and add its hash
 *
 *   tdes(in/out): transaction descriptor
 *   class_oid(in): class OID of the modified instance
 *   index_vfid(in): VFID of the index this key belongs to
 *   key(in): constraint key value (primary or unique key)
 *
 * return: NO_ERROR, or an error code on failure
 *
 * Note: Character-string keys are hashed through their collation pseudo key
 *       (wset_string_hash), so collation-equal strings produce one hash;
 *       every other type is hashed over its packed bytes.
 */
int
wset_add_write_key (LOG_TDES *tdes, const OID *class_oid, const VFID *index_vfid, DB_VALUE *key)
{
  char *buf = NULL;
  UINT64 hash;
  int buf_len;
  int packed_len = 0;
  int error;

  assert (tdes != NULL);
  assert (class_oid != NULL);
  assert (index_vfid != NULL);
  assert (key != NULL);

  if (tdes->wset_overflow || tdes->suppress_replication != 0)
    {
      return NO_ERROR;
    }

  /* A NULL unique key has no character data to collation-hash; it falls through and is
   * collected below as its packed NULL representation (conservative design policy). */
  if (TP_IS_CHAR_TYPE (DB_VALUE_DOMAIN_TYPE (key)) && !DB_IS_NULL (key))
    {
      hash = wset_string_hash (class_oid, index_vfid, key);
      error = wset_push_hash (tdes, hash, LOG_WSET_KIND_WRITE);
      if (error == NO_ERROR && !tdes->wset_overflow)
	{
	  wset_trace_collected_key (tdes, class_oid, index_vfid, key, hash, LOG_WSET_KIND_WRITE);
	}

      return error;
    }

  buf_len = OR_VALUE_ALIGNED_SIZE (key);
  buf = (char *) malloc ((size_t) buf_len);
  if (buf == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) buf_len);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  /* the alignment padding skipped by or_pack_mem_value is hashed too, so it must not hold garbage */
  memset (buf, 0, (size_t) buf_len);

  (void) or_pack_mem_value (buf, key, &packed_len);

  hash = wset_fnv1a (class_oid, index_vfid, buf, packed_len);
  error = wset_push_hash (tdes, hash, LOG_WSET_KIND_WRITE);
  if (error == NO_ERROR && !tdes->wset_overflow)
    {
      wset_trace_collected_key (tdes, class_oid, index_vfid, key, hash, LOG_WSET_KIND_WRITE);
    }

  free_and_init (buf);

  return error;
}

/*
 * wset_error - preserve a lower-layer error or set a writeset key-build error
 *
 * return: error code
 */
static int
wset_error (int error, const char *detail)
{
  if (error != NO_ERROR && error != ER_FAILED)
    {
      return error;
    }
  if (error == ER_FAILED && er_errid () != NO_ERROR)
    {
      return er_errid ();
    }
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HA_WRITESET_KEY_BUILD_FAILED, 1, detail);

  return ER_HA_WRITESET_KEY_BUILD_FAILED;
}

/*
 * wset_cast_value_to_domain - cast one key value to the referenced parent index domain
 *
 * return: NO_ERROR, or an error code on failure
 */
static int
wset_cast_value_to_domain (DB_VALUE *source, DB_VALUE *destination, TP_DOMAIN *domain)
{
  TP_DOMAIN_STATUS status = tp_value_cast (source, destination, domain, false);

  if (status == DOMAIN_ERROR && er_errid () != NO_ERROR)
    {
      return er_errid ();
    }
  if (status == DOMAIN_OVERFLOW)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TP_CANT_COERCE_OVERFLOW, 2,
	      pr_type_name (DB_VALUE_DOMAIN_TYPE (source)), pr_type_name (TP_DOMAIN_TYPE (domain)));
      return ER_TP_CANT_COERCE_OVERFLOW;
    }
  if (status != DOMAIN_COMPATIBLE)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TP_CANT_COERCE, 2,
	      pr_type_name (DB_VALUE_DOMAIN_TYPE (source)), pr_type_name (TP_DOMAIN_TYPE (domain)));
      return ER_TP_CANT_COERCE;
    }

  return NO_ERROR;
}

/*
 * wset_cast_midxkey_element - cast one child FK element to its parent PK element domain
 *
 * return: NO_ERROR, or an error code on failure
 *
 * Note: Under MATCH SIMPLE, a NULL element sets has_reference to false and returns NO_ERROR.
 */
static int
wset_cast_midxkey_element (DB_MIDXKEY *fk_midxkey, int index, TP_DOMAIN *parent_domain,
			   DB_VALUE *parent_value, bool *has_reference)
{
  DB_VALUE fk_element;
  int error;

  db_make_null (&fk_element);
  error = pr_midxkey_get_element_nocopy (fk_midxkey, index, &fk_element, NULL, NULL);
  if (error != NO_ERROR)
    {
      pr_clear_value (&fk_element);
      return wset_error (error, "failed to extract a composite foreign-key element");
    }

  if (DB_IS_NULL (&fk_element))
    {
      *has_reference = false;
      pr_clear_value (&fk_element);
      return NO_ERROR;
    }

  error = wset_cast_value_to_domain (&fk_element, parent_value, parent_domain);
  pr_clear_value (&fk_element);

  return error;
}

/*
 * wset_make_parent_midxkey - rebuild a composite child FK in the parent PK domain
 *
 * return: NO_ERROR, or an error code on failure
 *
 * Note: Cast each child FK element to the corresponding parent PK element domain and rebuild the
 *       complete tuple as one MIDXKEY. MATCH SIMPLE means a tuple containing NULL has no reference.
 */
static int
wset_make_parent_midxkey (DB_VALUE *fk_value, TP_DOMAIN *parent_pk_domain, DB_VALUE *parent_key,
			  bool *has_reference)
{
  DB_MIDXKEY *fk_midxkey;
  DB_MIDXKEY parent_midxkey;
  DB_VALUE *parent_values;
  TP_DOMAIN *parent_domain;
  int column_count;
  int initialized_count = 0;
  int error = NO_ERROR;
  int i;

  assert (fk_value != NULL);
  assert (parent_pk_domain != NULL);
  assert (parent_key != NULL);
  assert (has_reference != NULL);

  fk_midxkey = db_get_midxkey (fk_value);
  column_count = tp_domain_size (parent_pk_domain->setdomain);

  *has_reference = false;
  if (fk_midxkey == NULL || column_count <= 0 || fk_midxkey->ncolumns != column_count)
    {
      return wset_error (NO_ERROR, "child FK and parent PK column counts do not match");
    }

  parent_values = (DB_VALUE *) malloc ((size_t) column_count * sizeof (*parent_values));
  if (parent_values == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      (size_t) column_count * sizeof (*parent_values));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  for (i = 0; i < column_count; i++)
    {
      db_make_null (&parent_values[i]);
      initialized_count++;
    }

  *has_reference = true;
  parent_domain = parent_pk_domain->setdomain;
  for (i = 0; i < column_count; i++, parent_domain = parent_domain->next)
    {
      error = wset_cast_midxkey_element (fk_midxkey, i, parent_domain, &parent_values[i], has_reference);
      if (error != NO_ERROR || !*has_reference)
	{
	  goto end;
	}
    }

  parent_midxkey.buf = NULL;
  parent_midxkey.domain = parent_pk_domain;
  parent_midxkey.ncolumns = 0;
  parent_midxkey.size = 0;
  parent_midxkey.min_max_val.position = -1;
  db_make_midxkey (parent_key, &parent_midxkey);
  parent_key->need_clear = true;

  error = pr_midxkey_add_elements (parent_key, parent_values, column_count, parent_pk_domain->setdomain);
  if (error != NO_ERROR)
    {
      error = wset_error (error, "failed to build a parent-domain composite REF key");
      pr_clear_value (parent_key);
      goto end;
    }

  *has_reference = true;

end:
  for (i = 0; i < initialized_count; i++)
    {
      pr_clear_value (&parent_values[i]);
    }
  free_and_init (parent_values);

  return error;
}

/*
 * wset_add_ref_key - add a foreign-key reference hash to the transaction
 *
 *   tdes(in/out): transaction descriptor
 *   ref_class_oid(in): class OID of the referenced (parent) table
 *   index_vfid(in): VFID of the parent primary-key index (so the reference hashes into the same slot
 *                   as the parent's own write over that key)
 *   fk_value(in): the child row's foreign-key value
 *   parent_pk_domain(in): domain of the parent primary-key b-tree
 *
 * return: NO_ERROR, or an error code on failure
 *
 * Note: Normalize the child FK value to the referenced parent key's type, codeset, collation, and
 *       precision before hashing. This gives logically equal parent and child values the same key
 *       representation and hash.
 *       A REF waits for the previous WRITE and is published to ref_seq. It does not wait for
 *       previous REFs, so sibling child transactions remain independent.
 *       NULL FKs have no parent reference and produce no REF. Composite FKs are rebuilt as one complete
 *       MIDXKEY in the parent PK domain before hashing.
 */
int
wset_add_ref_key (LOG_TDES *tdes, const OID *ref_class_oid,
		  const VFID *index_vfid, DB_VALUE *fk_value, struct tp_domain *parent_pk_domain)
{
  DB_VALUE casted;
  char *buf = NULL;
  UINT64 hash;
  int buf_len;
  int packed_len = 0;
  int error;
  bool has_reference = true;

  assert (tdes != NULL);
  assert (ref_class_oid != NULL);
  assert (index_vfid != NULL);
  assert (fk_value != NULL);
  assert (parent_pk_domain != NULL);

  if (tdes->wset_overflow || tdes->suppress_replication != 0)
    {
      return NO_ERROR;
    }

  if (DB_IS_NULL (fk_value))
    {
      return NO_ERROR;
    }

  if (TP_DOMAIN_TYPE (parent_pk_domain) == DB_TYPE_MIDXKEY)
    {
      if (DB_VALUE_DOMAIN_TYPE (fk_value) != DB_TYPE_MIDXKEY)
	{
	  return wset_error (NO_ERROR, "composite parent PK requires a composite child FK key");
	}

      db_make_null (&casted);
      error = wset_make_parent_midxkey (fk_value, parent_pk_domain, &casted, &has_reference);
      if (error != NO_ERROR || !has_reference)
	{
	  pr_clear_value (&casted);
	  return error;
	}
      goto pack_value;
    }

  db_make_null (&casted);

  error = wset_cast_value_to_domain (fk_value, &casted, parent_pk_domain);
  if (error != NO_ERROR)
    {
      pr_clear_value (&casted);
      return error;
    }

  if (DB_IS_NULL (&casted))
    {
      pr_clear_value (&casted);
      return wset_error (NO_ERROR, "non-NULL foreign key became NULL after parent-domain cast");
    }

  if (TP_IS_CHAR_TYPE (DB_VALUE_DOMAIN_TYPE (&casted)))
    {
      hash = wset_string_hash (ref_class_oid, index_vfid, &casted);
      error = wset_push_hash (tdes, hash, LOG_WSET_KIND_REF);
      if (error == NO_ERROR && !tdes->wset_overflow)
	{
	  wset_trace_collected_key (tdes, ref_class_oid, index_vfid, &casted, hash, LOG_WSET_KIND_REF);
	}

      pr_clear_value (&casted);
      return error;
    }

pack_value:
  buf_len = OR_VALUE_ALIGNED_SIZE (&casted);
  buf = (char *) malloc ((size_t) buf_len);
  if (buf == NULL)
    {
      pr_clear_value (&casted);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) buf_len);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  /* zero the alignment padding for a stable hash, same reason as the WRITE path */
  memset (buf, 0, (size_t) buf_len);

  (void) or_pack_mem_value (buf, &casted, &packed_len);

  hash = wset_fnv1a (ref_class_oid, index_vfid, buf, packed_len);
  error = wset_push_hash (tdes, hash, LOG_WSET_KIND_REF);
  if (error == NO_ERROR && !tdes->wset_overflow)
    {
      wset_trace_collected_key (tdes, ref_class_oid, index_vfid, &casted, hash, LOG_WSET_KIND_REF);
    }

  free_and_init (buf);
  pr_clear_value (&casted);

  return error;
}

/* Select the history slot that one writeset entry must wait for. */
static void
wset_select_candidate (const LOG_WSET_ENTRY &entry, const LOG_WSET_SLOTS &slots, LOG_LSA *candidate_out,
		       bool *candidate_is_ref_out)
{
  *candidate_is_ref_out = false;

  if (entry.kind == LOG_WSET_KIND_REF)
    {
      LSA_COPY (candidate_out, &slots.write_seq);
      return;
    }

  if (entry.kind != LOG_WSET_KIND_WRITE)
    {
      assert (false);
      LSA_COPY (candidate_out, &slots.write_seq);
      return;
    }

  if (LSA_GT (&slots.ref_seq, &slots.write_seq))
    {
      LSA_COPY (candidate_out, &slots.ref_seq);
      *candidate_is_ref_out = true;
      return;
    }

  LSA_COPY (candidate_out, &slots.write_seq);
}

/*
 * wset_find_dependency_from_history - find the current transaction's dependency in history
 *
 *   tdes(in/out): transaction writeset and dependency source flag
 *   wset_parent_out(out): selected dependency LSA
 *
 * Note: A REF entry probes write_seq because a reference must wait for the last transaction that
 *       changed the referenced key. A WRITE entry probes the later of write_seq and ref_seq
 *       because it must wait for both the last change to the key and any later transaction that
 *       referenced the key's previous state.
 *
 *       Candidate selection starts at history_start, and the latest candidate across all entries
 *       becomes the writeset dependency.
 *
 *       An overflowed writeset uses prev_commit_lsa. If the selected candidate came from ref_seq,
 *       it is returned unchanged and wset_dependency_is_ref is set. Otherwise, the earlier of the
 *       writeset dependency and prev_commit_lsa is returned.
 */
void
wset_find_dependency_from_history (LOG_TDES *tdes, LOG_LSA *wset_parent_out)
{
  LOG_LSA wset_parent;
  LOG_LSA prev_commit_snapshot;
  LOG_LSA history_start_snapshot;
  bool dependency_from_ref_seq = false;

  if (tdes == NULL || wset_parent_out == NULL)
    {
      return;
    }

  tdes->wset_dependency_is_ref = false;

  pthread_mutex_lock (&wset_History.seq_lock);
  LSA_COPY (&prev_commit_snapshot, &wset_History.prev_commit_lsa);
  pthread_mutex_unlock (&wset_History.seq_lock);

  if (tdes->wset_overflow)
    {
      LSA_COPY (wset_parent_out, &prev_commit_snapshot);
      WSET_TRACE (1, "DEPENDENCY trid=%d mode=COMMIT_ORDER dependency=%lld|%d\n",
		  tdes->trid, (long long) wset_parent_out->pageid, (int) wset_parent_out->offset);
      return;
    }

  pthread_rwlock_rdlock (&wset_History.history_lock);

  LSA_COPY (&history_start_snapshot, &wset_History.history_start);
  LSA_COPY (&wset_parent, &history_start_snapshot);

  for (const LOG_WSET_ENTRY &e : tdes->wset_hashes)
    {
      LOG_WSET_SLOTS slots;
      LOG_LSA candidate;
      bool candidate_is_ref;

      if (!wset_History.map.find (e.hash, slots))
	{
	  WSET_TRACE (2, "PROBE trid=%d kind=%s hash=%016llx result=MISS\n",
		      tdes->trid, wset_kind_name (e.kind), (unsigned long long) e.hash);
	  continue;
	}

      wset_select_candidate (e, slots, &candidate, &candidate_is_ref);
      WSET_TRACE (2,
		  "PROBE trid=%d kind=%s hash=%016llx write_seq=%lld|%d ref_seq=%lld|%d candidate=%lld|%d source=%s\n",
		  tdes->trid, wset_kind_name (e.kind), (unsigned long long) e.hash,
		  (long long) slots.write_seq.pageid, (int) slots.write_seq.offset,
		  (long long) slots.ref_seq.pageid, (int) slots.ref_seq.offset,
		  (long long) candidate.pageid, (int) candidate.offset,
		  candidate_is_ref ? "REF_SLOT" : "WRITE_SLOT");

      if (LSA_ISNULL (&candidate) || !LSA_GT (&candidate, &wset_parent))
	{
	  continue;
	}

      LSA_COPY (&wset_parent, &candidate);
      dependency_from_ref_seq = candidate_is_ref;
    }

  pthread_rwlock_unlock (&wset_History.history_lock);

  if (dependency_from_ref_seq)
    {
      /* Independent REFs are represented by their maximum commit LSA; preserve it and mark its
       * source. */
      LSA_COPY (wset_parent_out, &wset_parent);
      tdes->wset_dependency_is_ref = true;
      WSET_TRACE (1,
		  "DEPENDENCY trid=%d source=REF_SLOT history_start=%lld|%d prev_commit=%lld|%d dependency=%lld|%d\n",
		  tdes->trid, (long long) history_start_snapshot.pageid,
		  (int) history_start_snapshot.offset, (long long) prev_commit_snapshot.pageid,
		  (int) prev_commit_snapshot.offset, (long long) wset_parent_out->pageid,
		  (int) wset_parent_out->offset);
      return;
    }

  /* Both candidates are safe bounds; choose the smaller one to preserve parallelism. */
  if (LSA_ISNULL (&prev_commit_snapshot) || LSA_LT (&wset_parent, &prev_commit_snapshot))
    {
      LSA_COPY (wset_parent_out, &wset_parent);
    }
  else
    {
      LSA_COPY (wset_parent_out, &prev_commit_snapshot);
    }

  WSET_TRACE (1,
	      "DEPENDENCY trid=%d source=WRITE_BOUND history_start=%lld|%d prev_commit=%lld|%d dependency=%lld|%d\n",
	      tdes->trid, (long long) history_start_snapshot.pageid,
	      (int) history_start_snapshot.offset, (long long) prev_commit_snapshot.pageid,
	      (int) prev_commit_snapshot.offset, (long long) wset_parent_out->pageid,
	      (int) wset_parent_out->offset);
}

/*
 * wset_publish_keys - publish this commit's collected keys into the history map
 *
 * Note: The caller holds the history lock (shared for the normal path, exclusive when it also
 *       cleared the map). WRITE keys stamp the write slot; a same-key successor is serialized
 *       behind this transaction by the row X-lock, so the assignment is already monotonic. REF keys
 *       stamp the read slot, kept as the newest referencer, so a later writer of the key sees and
 *       waits behind its newest referencer. A brand-new map entry starts with both slots null.
 */
static void
wset_publish_keys (LOG_TDES *tdes, const LOG_LSA *commit_lsa)
{
  for (const LOG_WSET_ENTRY &e : tdes->wset_hashes)
    {
      wset_History.map.publish (e.hash, e.kind, *commit_lsa);
      WSET_TRACE (2, "PUBLISH trid=%d kind=%s hash=%016llx commit_lsa=%lld|%d\n",
		  tdes->trid, wset_kind_name (e.kind), (unsigned long long) e.hash,
		  (long long) commit_lsa->pageid, (int) commit_lsa->offset);
      if (e.kind == LOG_WSET_KIND_WRITE)
	{
	  wset_Statistics.write_publish_count.fetch_add (1, std::memory_order_relaxed);
	}
      else
	{
	  assert (e.kind == LOG_WSET_KIND_REF);
	  wset_Statistics.ref_publish_count.fetch_add (1, std::memory_order_relaxed);
	}
    }
}

static void
wset_advance_commit_baseline (const LOG_LSA *commit_lsa)
{
  pthread_mutex_lock (&wset_History.seq_lock);
  if (LSA_ISNULL (&wset_History.prev_commit_lsa) || LSA_GT (commit_lsa, &wset_History.prev_commit_lsa))
    {
      LSA_COPY (&wset_History.prev_commit_lsa, commit_lsa);
    }
  pthread_mutex_unlock (&wset_History.seq_lock);
}

static void
wset_clear_history_with_lock (const LOG_LSA *commit_lsa, LOG_LSA *history_start_out)
{
  wset_History.map.clear ();
  wset_Statistics.history_clear_count.fetch_add (1, std::memory_order_relaxed);

  /* Commit flushes may arrive out of LSA order; never move the history floor backward. */
  if (LSA_ISNULL (&wset_History.history_start) || LSA_GT (commit_lsa, &wset_History.history_start))
    {
      LSA_COPY (&wset_History.history_start, commit_lsa);
    }

  LSA_COPY (history_start_out, &wset_History.history_start);
}

static bool
wset_history_has_capacity (size_t publish_count)
{
  return (INT64) (wset_History.map.size () + publish_count) <= wset_History.history_size;
}

static void
wset_clear_overflow_history (LOG_TDES *tdes, const LOG_LSA *commit_lsa)
{
  LOG_LSA history_start;
  size_t old_size;

  pthread_rwlock_wrlock (&wset_History.history_lock);
  old_size = wset_History.map.size ();
  wset_clear_history_with_lock (commit_lsa, &history_start);
  pthread_rwlock_unlock (&wset_History.history_lock);

  WSET_TRACE (1,
	      "CLEAR trid=%d reason=COMMIT_ORDER_FALLBACK old_size=%zu new_size=0 commit_lsa=%lld|%d history_start=%lld|%d\n",
	      tdes->trid, old_size, (long long) commit_lsa->pageid, (int) commit_lsa->offset,
	      (long long) history_start.pageid, (int) history_start.offset);
}

static void
wset_publish_with_capacity (LOG_TDES *tdes, const LOG_LSA *commit_lsa)
{
  const size_t publish_count = tdes->wset_hashes.size ();
  LOG_LSA history_start;
  bool history_cleared = false;
  size_t old_size = 0;
  size_t new_size = 0;

  pthread_rwlock_rdlock (&wset_History.history_lock);
  if (wset_history_has_capacity (publish_count))
    {
      wset_publish_keys (tdes, commit_lsa);
      pthread_rwlock_unlock (&wset_History.history_lock);
      return;
    }
  pthread_rwlock_unlock (&wset_History.history_lock);

  pthread_rwlock_wrlock (&wset_History.history_lock);

  /* The map may change while replacing the shared lock with the exclusive lock. */
  if (!wset_history_has_capacity (publish_count))
    {
      old_size = wset_History.map.size ();
      wset_clear_history_with_lock (commit_lsa, &history_start);
      history_cleared = true;
    }

  wset_publish_keys (tdes, commit_lsa);
  new_size = wset_History.map.size ();
  pthread_rwlock_unlock (&wset_History.history_lock);

  if (history_cleared)
    {
      WSET_TRACE (1,
		  "CLEAR trid=%d reason=HISTORY_CAPACITY old_size=%zu publish=%zu new_size=%zu commit_lsa=%lld|%d history_start=%lld|%d\n",
		  tdes->trid, old_size, publish_count, new_size,
		  (long long) commit_lsa->pageid, (int) commit_lsa->offset,
		  (long long) history_start.pageid, (int) history_start.offset);
    }
}

/*
 * wset_publish_commit_to_history - publish a committed transaction to history
 *
 *   tdes(in): committed transaction
 *   commit_lsa(in): the assigned commit LSA of this transaction
 *
 * Note: Called after COMMIT LSA assignment and before row locks are released. Aborted transactions
 *       must not call this function.
 */
void
wset_publish_commit_to_history (LOG_TDES *tdes, const LOG_LSA *commit_lsa)
{
  assert (tdes != NULL);
  assert (commit_lsa != NULL);

  if (LSA_ISNULL (commit_lsa))
    {
      assert (false);
      return;
    }

  wset_advance_commit_baseline (commit_lsa);
  WSET_TRACE (1,
	      "COMMIT trid=%d commit_lsa=%lld|%d dependency=%lld|%d dependency_source=%s entries=%zu fallback=%d\n",
	      tdes->trid, (long long) commit_lsa->pageid, (int) commit_lsa->offset,
	      (long long) tdes->wset_dependency_seq.pageid, (int) tdes->wset_dependency_seq.offset,
	      tdes->wset_dependency_is_ref ? "REF_SLOT" : "WRITE_BOUND", tdes->wset_hashes.size (),
	      tdes->wset_overflow ? 1 : 0);

  if (tdes->wset_overflow)
    {
      wset_clear_overflow_history (tdes, commit_lsa);
      return;
    }

  if (tdes->wset_hashes.empty ())
    {
      return;
    }

  wset_publish_with_capacity (tdes, commit_lsa);
}

#endif /* SERVER_MODE || SA_MODE */
