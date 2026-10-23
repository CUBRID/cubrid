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
#include <algorithm>
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
#include "boot_sr.h"
#include "db_date.h"
#include "heap_file.h"
#include "log_append.hpp"
#include "log_manager.h"
#include "object_representation_sr.h"
#include "page_buffer.h"
#include "record_descriptor.hpp"
#include "schema_system_catalog_constants.h"
#include "server_support.h"
#include "slotted_page.h"
#include "xserver_interface.h"
#if defined (SERVER_MODE)
#include "thread_daemon.hpp"
#include "thread_entry_task.hpp"
#include "thread_looper.hpp"
#include "thread_manager.hpp"
#endif /* SERVER_MODE */

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#if defined(SERVER_MODE) || defined(SA_MODE)

/* FNV-1a 64-bit constants */
#define LOG_WSET_FNV_OFFSET_BASIS ((UINT64) 0xcbf29ce484222325ULL)
#define LOG_WSET_FNV_PRIME        ((UINT64) 0x00000100000001b3ULL)

/* Global WRITE/REF commit history indexed by writeset key hash. */
LOG_WSET_HISTORY wset_History;

/* Counted since the server started. The active server writes the ones that tell how much parallelism the
 * writeset gives into _db_ha_writeset_stat (see wset_stat_store); the others are kept for debugging. */
struct wset_statistics
{
  std::atomic<UINT64> overflow_count { 0 };
  std::atomic<UINT64> history_clear_count { 0 };
  std::atomic<UINT64> commit_order_fallback_count { 0 };
  std::atomic<UINT64> write_publish_count { 0 };
  std::atomic<UINT64> ref_publish_count { 0 };

  std::atomic<UINT64> commit_count { 0 };		/* commits that wrote a writeset label */
  std::atomic<UINT64> commit_fallback_count { 0 };	/* of those, the ones labeled for commit order */
  std::atomic<UINT64> fallback_key_limit_count { 0 };	/* ... because the transaction had too many keys */
  std::atomic<UINT64> fallback_statement_count { 0 };	/* ... because a change could not give its keys */
  std::atomic<UINT64> conflict_ref_count { 0 };	/* commits whose dependency is a referencer of one of its keys */
  std::atomic<UINT64> conflict_write_count { 0 };	/* commits whose dependency is a writer of one of its keys */
  std::atomic<UINT64> clear_capacity_count { 0 };	/* times the history was cleared because it was full */
};

static wset_statistics wset_Statistics;

/* Keys that committers holding the shared history lock are about to publish. The capacity check adds its own
 * keys here before comparing, so several committers that pass the check at the same moment see one another's
 * keys and cannot together push the map past history_size. Always brought back to zero by the publisher. */
static std::atomic<INT64> wset_Publish_reserved { 0 };
static int wset_Trace_level = 0;

/* Tracks whether the history locks were initialized.
 * Initialization and finalization are expected to run on a single thread. */
static bool wset_History_initialized = false;

static UINT64 wset_fnv1a (const OID *class_oid, const VFID *index_vfid, const char *packed, int len);
static void wset_string_pseudo_bytes (const LANG_COLLATION *lc, const unsigned char *s, int size,
				      unsigned char pbytes[5]);
static UINT64 wset_string_hash (const OID *class_oid, const VFID *index_vfid, DB_VALUE *v);
static int wset_midxkey_hash (const OID *class_oid, const VFID *index_vfid, DB_VALUE *key, UINT64 *hash_out);
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
static void wset_clear_history_locked (const LOG_LSA *clear_floor, LOG_LSA *history_start_out);
static bool wset_history_has_capacity (size_t publish_count);
static void wset_find_dependency_in_range (LOG_TDES *tdes, size_t begin, LOG_LSA *wset_parent_out);
static bool wset_publish_keys (LOG_TDES *tdes, size_t begin, size_t end, const LOG_LSA *commit_lsa);
static void wset_clear_overflow_history (LOG_TDES *tdes, const LOG_LSA *commit_lsa);
static void wset_publish_with_capacity (LOG_TDES *tdes, size_t begin, size_t end, const LOG_LSA *commit_lsa);

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

  WSET_TRACE (3,
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
  unsigned char pbytes[5];

  lc = lang_get_collation (db_get_string_collation (v));
  s = (const unsigned char *) db_get_string (v);
  assert (lc != NULL);
  assert (s != NULL);

  wset_string_pseudo_bytes (lc, s, db_get_string_size (v), pbytes);

  return wset_fnv1a (class_oid, index_vfid, (const char *) pbytes, 5);
}

/*
 * wset_string_pseudo_bytes - reduce a character string to the 5 bytes hashed for it
 *
 *   lc(in): collation of the index column the string belongs to
 *   s(in): string bytes
 *   size(in): string size in bytes
 *   pbytes(out): 0xC5 tag followed by the 4-byte collation pseudo key
 *
 * Note: The collation pseudo key is hashed instead of the raw string bytes, so strings the collation
 *       treats as equal share a writeset hash without unconditional case folding. The 0xC5 tag keeps a
 *       string pseudo key from colliding with a numeric key that might pack to the same four bytes.
 */
static void
wset_string_pseudo_bytes (const LANG_COLLATION *lc, const unsigned char *s, int size, unsigned char pbytes[5])
{
  unsigned int pseudo;

  pseudo = lc->mht2str (lc, s, size);

  pbytes[0] = 0xC5;
  pbytes[1] = (unsigned char) (pseudo & 0xff);
  pbytes[2] = (unsigned char) ((pseudo >> 8) & 0xff);
  pbytes[3] = (unsigned char) ((pseudo >> 16) & 0xff);
  pbytes[4] = (unsigned char) ((pseudo >> 24) & 0xff);
}

/*
 * wset_midxkey_hash - hash a composite key element by element
 *
 *   class_oid(in): class OID used by the writeset key
 *   index_vfid(in): index VFID used by the writeset key
 *   key(in): composite key whose domain lists the element domains
 *   hash_out(out): writeset key hash
 *
 * return: NO_ERROR, or an error code on failure
 *
 * Note: The packed image of a composite key keeps each character element as its stored bytes, so two keys
 *       that the index treats as equal under a case- or accent-insensitive collation would hash apart, and
 *       a parent write and the child reference spelled differently would not find each other. Each element
 *       is therefore reduced the way a single-column key is: a character element to the pseudo key of its
 *       column's collation, any other element to its packed bytes, and a NULL element to a fixed tag. The
 *       pieces are concatenated in element order and hashed once.
 */
static int
wset_midxkey_hash (const OID *class_oid, const VFID *index_vfid, DB_VALUE *key, UINT64 *hash_out)
{
  DB_MIDXKEY *midxkey;
  TP_DOMAIN *element_domain;
  DB_VALUE element;
  LANG_COLLATION *lc;
  char *buf = NULL;
  int buf_len = 0;
  int buf_cap = 0;
  int packed_len;
  int error = NO_ERROR;
  int pass;
  int i;

  assert (DB_VALUE_DOMAIN_TYPE (key) == DB_TYPE_MIDXKEY);

  midxkey = db_get_midxkey (key);
  if (midxkey == NULL || midxkey->domain == NULL)
    {
      return wset_error (NO_ERROR, "composite key without a domain");
    }

  /* the first pass only measures the reduced form, the second fills the buffer sized by the first */
  for (pass = 0; pass < 2 && error == NO_ERROR; pass++)
    {
      if (pass == 1)
	{
	  buf = (char *) malloc ((size_t) buf_cap);
	  if (buf == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) buf_cap);
	      return ER_OUT_OF_VIRTUAL_MEMORY;
	    }
	  /* the alignment padding skipped by or_pack_mem_value is hashed too, so it must not hold garbage */
	  memset (buf, 0, (size_t) buf_cap);
	}

      element_domain = midxkey->domain->setdomain;
      for (i = 0; i < midxkey->ncolumns && element_domain != NULL; i++, element_domain = element_domain->next)
	{
	  db_make_null (&element);
	  error = pr_midxkey_get_element_nocopy (midxkey, i, &element, NULL, NULL);
	  if (error != NO_ERROR)
	    {
	      error = wset_error (error, "failed to extract a composite key element");
	      break;
	    }

	  if (DB_IS_NULL (&element))
	    {
	      /* 0xC0 tag: a NULL element, distinct from any packed value and from a string pseudo key */
	      if (pass == 1)
		{
		  buf[buf_len] = (char) 0xC0;
		}
	      buf_len += 1;
	    }
	  else if (TP_IS_CHAR_TYPE (DB_VALUE_DOMAIN_TYPE (&element)))
	    {
	      lc = lang_get_collation (TP_DOMAIN_COLLATION (element_domain));
	      if (lc == NULL)
		{
		  pr_clear_value (&element);
		  error = wset_error (NO_ERROR, "unknown collation of a composite key element");
		  break;
		}

	      if (pass == 1)
		{
		  wset_string_pseudo_bytes (lc, (const unsigned char *) db_get_string (&element),
					    db_get_string_size (&element), (unsigned char *) buf + buf_len);
		}
	      buf_len += 5;
	    }
	  else
	    {
	      /* 0xC1 tag followed by the packed element in its aligned size, so the layout does not depend on
	       * how much of that size the packer used. or_pack_mem_value aligns its start and its value to
	       * MAX_ALIGNMENT, and OR_VALUE_ALIGNED_SIZE leaves room for that only from an aligned start, so
	       * the element starts at the next aligned offset, as or_pack_value aligns its buffer first. The
	       * buffer comes from malloc, so an aligned offset is an aligned address, and the skipped bytes
	       * are zero. */
	      if (pass == 1)
		{
		  buf[buf_len] = (char) 0xC1;
		}
	      buf_len = DB_ALIGN (buf_len + 1, MAX_ALIGNMENT);
	      if (pass == 1)
		{
		  int used_len;

		  (void) or_pack_mem_value (buf + buf_len, &element, &used_len);
		}
	      packed_len = OR_VALUE_ALIGNED_SIZE (&element);
	      buf_len += packed_len;
	    }

	  pr_clear_value (&element);
	}

      if (pass == 0)
	{
	  buf_cap = buf_len;
	  buf_len = 0;
	}
    }

  if (error == NO_ERROR)
    {
      assert (buf_len == buf_cap);
      *hash_out = wset_fnv1a (class_oid, index_vfid, buf, buf_len);
    }

  if (buf != NULL)
    {
      free_and_init (buf);
    }

  return error;
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
  LSA_COPY (&entry.lsa, &tdes->tail_lsa);
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
  tdes->wset_fallback_reason = (int) reason;
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
 *       a composite key is reduced element by element the same way (wset_midxkey_hash);
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

  /* without its domain a composite key packs as a bare NULL-domain header, so every key of the index would
   * hash alike and miss the hash of the same row collected with the domain set */
  if (DB_VALUE_DOMAIN_TYPE (key) == DB_TYPE_MIDXKEY && key->data.midxkey.domain == NULL)
    {
      assert (false);
      return wset_error (NO_ERROR, "composite key without a domain");
    }

  /* a composite key is reduced element by element so that its character elements follow their collation */
  if (DB_VALUE_DOMAIN_TYPE (key) == DB_TYPE_MIDXKEY)
    {
      error = wset_midxkey_hash (class_oid, index_vfid, key, &hash);
      if (error != NO_ERROR)
	{
	  return error;
	}

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

      /* the same element-wise reduction as the parent's WRITE key, now in the parent's domain */
      error = wset_midxkey_hash (ref_class_oid, index_vfid, &casted, &hash);
      if (error == NO_ERROR)
	{
	  error = wset_push_hash (tdes, hash, LOG_WSET_KIND_REF);
	  if (error == NO_ERROR && !tdes->wset_overflow)
	    {
	      wset_trace_collected_key (tdes, ref_class_oid, index_vfid, &casted, hash, LOG_WSET_KIND_REF);
	    }
	}

      pr_clear_value (&casted);
      return error;
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
 *       An overflowed writeset uses prev_commit_lsa, raised to history_start like the writeset
 *       dependency. If the selected candidate came from ref_seq,
 *       it is returned unchanged. Otherwise, the earlier of the writeset dependency and prev_commit_lsa
 *       is returned.
 */
static void
wset_find_dependency_in_range (LOG_TDES *tdes, size_t begin, LOG_LSA *wset_parent_out)
{
  LOG_LSA wset_parent;
  LOG_LSA prev_commit_snapshot;
  LOG_LSA history_start_snapshot;
  bool dependency_from_ref_seq = false;
  bool key_conflict = false;
  size_t end = tdes->wset_hashes.size ();
  size_t index;

  pthread_mutex_lock (&wset_History.seq_lock);
  LSA_COPY (&prev_commit_snapshot, &wset_History.prev_commit_lsa);
  pthread_rwlock_rdlock (&wset_History.history_lock);
  LSA_COPY (&history_start_snapshot, &wset_History.history_start);
  pthread_mutex_unlock (&wset_History.seq_lock);

  LSA_COPY (&wset_parent, &history_start_snapshot);

  for (index = begin; index < end; index++)
    {
      const LOG_WSET_ENTRY &e = tdes->wset_hashes[index];
      LOG_WSET_SLOTS slots;
      LOG_LSA candidate;
      bool candidate_is_ref;

      if (!wset_History.map.find (e.hash, slots))
	{
	  WSET_TRACE (3, "PROBE trid=%d kind=%s hash=%016llx result=MISS\n",
		      tdes->trid, wset_kind_name (e.kind), (unsigned long long) e.hash);
	  continue;
	}

      wset_select_candidate (e, slots, &candidate, &candidate_is_ref);
      WSET_TRACE (3,
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
      key_conflict = true;
    }

  pthread_rwlock_unlock (&wset_History.history_lock);

  if (dependency_from_ref_seq)
    {
      wset_Statistics.conflict_ref_count.fetch_add (1, std::memory_order_relaxed);
    }
  else if (key_conflict)
    {
      wset_Statistics.conflict_write_count.fetch_add (1, std::memory_order_relaxed);
    }

  if (dependency_from_ref_seq)
    {
      /* Independent REFs are represented by their maximum commit LSA; preserve it. */
      LSA_COPY (wset_parent_out, &wset_parent);
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

  if (!LSA_ISNULL (&history_start_snapshot)
      && (LSA_ISNULL (wset_parent_out) || LSA_LT (wset_parent_out, &history_start_snapshot)))
    {
      LSA_COPY (wset_parent_out, &history_start_snapshot);
    }

  WSET_TRACE (1,
	      "DEPENDENCY trid=%d source=WRITE_BOUND history_start=%lld|%d prev_commit=%lld|%d dependency=%lld|%d\n",
	      tdes->trid, (long long) history_start_snapshot.pageid,
	      (int) history_start_snapshot.offset, (long long) prev_commit_snapshot.pageid,
	      (int) prev_commit_snapshot.offset, (long long) wset_parent_out->pageid,
	      (int) wset_parent_out->offset);
}

void
wset_find_dependency_from_history (LOG_TDES *tdes, LOG_LSA *wset_parent_out)
{
  assert (tdes != NULL);
  assert (wset_parent_out != NULL);

  if (tdes->wset_overflow)
    {
      pthread_mutex_lock (&wset_History.seq_lock);
      LSA_COPY (wset_parent_out, &wset_History.prev_commit_lsa);
      /* The floor makes every later transaction wait on the slave for what was written before it: everything
       * before a restart, or a serial operation applied in commit order. prev_commit_lsa does not cover it
       * after a restart, where it is NULL, or after a serial operation, which can lie above it. Without the
       * floor a statement replicated after a restart, a DDL for instance, carries no dependency and can run
       * on the slave beside the transactions written before the restart. Every writer of history_start
       * holds seq_lock, so it is read consistently with prev_commit_lsa here. */
      if (!LSA_ISNULL (&wset_History.history_start)
	  && (LSA_ISNULL (wset_parent_out) || LSA_LT (wset_parent_out, &wset_History.history_start)))
	{
	  LSA_COPY (wset_parent_out, &wset_History.history_start);
	}
      pthread_mutex_unlock (&wset_History.seq_lock);
      WSET_TRACE (1, "DEPENDENCY trid=%d mode=COMMIT_ORDER dependency=%lld|%d\n",
		  tdes->trid, (long long) wset_parent_out->pageid, (int) wset_parent_out->offset);
      return;
    }

  wset_find_dependency_in_range (tdes, 0, wset_parent_out);
}

/*
 * wset_publish_keys - publish this commit's collected keys into the history map
 *
 * return: true, or false when the history map ran out of memory; some keys may then be published
 *
 * Note: The caller holds the history lock (shared for the normal path, exclusive when it also
 *       cleared the map). WRITE keys stamp the write slot; a same-key successor is serialized
 *       behind this transaction by the row X-lock, so the assignment is already monotonic. REF keys
 *       stamp the read slot, kept as the newest referencer, so a later writer of the key sees and
 *       waits behind its newest referencer. A brand-new map entry starts with both slots null.
 */
static bool
wset_publish_keys (LOG_TDES *tdes, size_t begin, size_t end, const LOG_LSA *commit_lsa)
{
  for (size_t index = begin; index < end; index++)
    {
      const LOG_WSET_ENTRY &e = tdes->wset_hashes[index];
      try
	{
	  wset_History.map.publish (e.hash, e.kind, *commit_lsa);
	}
      catch (const std::bad_alloc &)
	{
	  /* the history map allocates a node for a new key; the commit is already logged, so the
	   * caller handles this the same way as a full history */
	  return false;
	}
      WSET_TRACE (3, "PUBLISH trid=%d kind=%s hash=%016llx commit_lsa=%lld|%d\n",
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

  return true;
}

static void
wset_advance_commit_baseline (const LOG_LSA *commit_lsa)
{
  assert (commit_lsa != NULL);

  pthread_mutex_lock (&wset_History.seq_lock);
  if (LSA_ISNULL (&wset_History.prev_commit_lsa) || LSA_GT (commit_lsa, &wset_History.prev_commit_lsa))
    {
      LSA_COPY (&wset_History.prev_commit_lsa, commit_lsa);
    }
  pthread_mutex_unlock (&wset_History.seq_lock);
}

static void
wset_clear_history_locked (const LOG_LSA *clear_floor, LOG_LSA *history_start_out)
{
  assert (clear_floor != NULL);
  assert (!LSA_ISNULL (clear_floor));

  if (LSA_ISNULL (&wset_History.history_start) || LSA_GT (clear_floor, &wset_History.history_start))
    {
      LSA_COPY (&wset_History.history_start, clear_floor);
    }

  wset_History.map.clear ();
  wset_Statistics.history_clear_count.fetch_add (1, std::memory_order_relaxed);
  LSA_COPY (history_start_out, &wset_History.history_start);
}

/*
 * wset_history_has_capacity - check that publish_count more keys fit in the history
 *
 *   publish_count(in): keys the caller is about to publish
 *
 * Note: For the exclusive-lock path. The exclusive lock keeps every shared-lock publisher out, so no
 *       reservation is in flight and the map size alone is the truth.
 */
static bool
wset_history_has_capacity (size_t publish_count)
{
  return (INT64) (wset_History.map.size () + publish_count) <= wset_History.history_size;
}

/*
 * wset_history_reserve_capacity - claim room for publish_count keys under the shared lock
 *
 *   publish_count(in): keys the caller is about to publish
 *
 * return: true when the keys fit counting every other reservation in flight; the claim is kept and the
 *         caller releases it with wset_history_release_capacity after publishing. false when they do not
 *         fit; nothing is kept.
 */
static bool
wset_history_reserve_capacity (size_t publish_count)
{
  INT64 reserved = wset_Publish_reserved.fetch_add ((INT64) publish_count,
		   std::memory_order_acq_rel) + (INT64) publish_count;

  if ((INT64) wset_History.map.size () + reserved <= wset_History.history_size)
    {
      return true;
    }

  wset_Publish_reserved.fetch_sub ((INT64) publish_count, std::memory_order_acq_rel);

  return false;
}

static void
wset_history_release_capacity (size_t publish_count)
{
  wset_Publish_reserved.fetch_sub ((INT64) publish_count, std::memory_order_acq_rel);
}

static void
wset_clear_overflow_history (LOG_TDES *tdes, const LOG_LSA *commit_lsa)
{
  LOG_LSA history_start;
  size_t old_size;

  pthread_mutex_lock (&wset_History.seq_lock);
  pthread_rwlock_wrlock (&wset_History.history_lock);
  old_size = wset_History.map.size ();
  wset_clear_history_locked (&wset_History.prev_commit_lsa, &history_start);
  pthread_rwlock_unlock (&wset_History.history_lock);
  pthread_mutex_unlock (&wset_History.seq_lock);

  WSET_TRACE (1,
	      "CLEAR trid=%d reason=COMMIT_ORDER_FALLBACK old_size=%zu new_size=0 commit_lsa=%lld|%d history_start=%lld|%d\n",
	      tdes->trid, old_size, (long long) commit_lsa->pageid, (int) commit_lsa->offset,
	      (long long) history_start.pageid, (int) history_start.offset);
}

static void
wset_publish_with_capacity (LOG_TDES *tdes, size_t begin, size_t end, const LOG_LSA *commit_lsa)
{
  const size_t publish_count = end - begin;
  LOG_LSA history_start;
  bool history_cleared = false;
  bool published;
  const char *clear_reason = "HISTORY_CAPACITY";
  size_t old_size = 0;
  size_t new_size = 0;

  pthread_rwlock_rdlock (&wset_History.history_lock);
  if (wset_history_reserve_capacity (publish_count))
    {
      published = wset_publish_keys (tdes, begin, end, commit_lsa);
      wset_history_release_capacity (publish_count);
      pthread_rwlock_unlock (&wset_History.history_lock);
      if (published)
	{
	  return;
	}
    }
  else
    {
      pthread_rwlock_unlock (&wset_History.history_lock);
      published = true;
    }

  pthread_mutex_lock (&wset_History.seq_lock);
  pthread_rwlock_wrlock (&wset_History.history_lock);

  if (!published)
    {
      /* Out of memory while publishing under the shared lock: as with a full history, clear it so every
       * later transaction waits for this commit instead of for the keys that may be missing. */
      old_size = wset_History.map.size ();
      wset_clear_history_locked (&wset_History.prev_commit_lsa, &history_start);
      history_cleared = true;
      clear_reason = "OUT_OF_MEMORY";
    }
  else
    {
      /* The map may change while replacing the shared lock with the exclusive lock. */
      if (!wset_history_has_capacity (publish_count))
	{
	  old_size = wset_History.map.size ();
	  wset_clear_history_locked (&wset_History.prev_commit_lsa, &history_start);
	  history_cleared = true;
	  wset_Statistics.clear_capacity_count.fetch_add (1, std::memory_order_relaxed);
	}

      if (!wset_publish_keys (tdes, begin, end, commit_lsa))
	{
	  old_size = wset_History.map.size ();
	  wset_clear_history_locked (&wset_History.prev_commit_lsa, &history_start);
	  history_cleared = true;
	  clear_reason = "OUT_OF_MEMORY";
	}
    }

  new_size = wset_History.map.size ();
  pthread_rwlock_unlock (&wset_History.history_lock);
  pthread_mutex_unlock (&wset_History.seq_lock);

  if (history_cleared)
    {
      WSET_TRACE (1,
		  "CLEAR trid=%d reason=%s old_size=%zu publish=%zu new_size=%zu commit_lsa=%lld|%d history_start=%lld|%d\n",
		  tdes->trid, clear_reason, old_size, publish_count, new_size,
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
	      "COMMIT trid=%d commit_lsa=%lld|%d dependency=%lld|%d entries=%zu fallback=%d\n",
	      tdes->trid, (long long) commit_lsa->pageid, (int) commit_lsa->offset,
	      (long long) tdes->wset_dependency_seq.pageid, (int) tdes->wset_dependency_seq.offset,
	      tdes->wset_hashes.size (), tdes->wset_overflow ? 1 : 0);

  wset_Statistics.commit_count.fetch_add (1, std::memory_order_relaxed);

  if (tdes->wset_overflow)
    {
      wset_Statistics.commit_fallback_count.fetch_add (1, std::memory_order_relaxed);
      if (tdes->wset_fallback_reason == WSET_FALLBACK_TRANSACTION_LIMIT)
	{
	  wset_Statistics.fallback_key_limit_count.fetch_add (1, std::memory_order_relaxed);
	}
      else
	{
	  wset_Statistics.fallback_statement_count.fetch_add (1, std::memory_order_relaxed);
	}

      wset_clear_overflow_history (tdes, commit_lsa);
      return;
    }

  if (tdes->wset_hashes.empty ())
    {
      return;
    }

  wset_publish_with_capacity (tdes, 0, tdes->wset_hashes.size (), commit_lsa);
}

/*
 * wset_discard_sysop_keys - drop the keys a system operation collected from the outer writeset
 *
 *   tdes(in/out): transaction holding the collected keys
 *   sysop_key_begin(in): index in wset_hashes where the operation's keys start
 *
 * Note: The operation's keys are the tail of wset_hashes at SYSOP_END time. They were never published
 *       (the operation is labeled for commit order), and the row locks they describe were released at
 *       SYSOP_END, so the outer commit must not probe or publish them.
 */
void
wset_discard_sysop_keys (LOG_TDES *tdes, size_t sysop_key_begin)
{
  assert (tdes != NULL);

  if (sysop_key_begin >= tdes->wset_hashes.size ())
    {
      return;
    }

  tdes->wset_hashes.erase (tdes->wset_hashes.begin () + sysop_key_begin, tdes->wset_hashes.end ());
}

/*
 * wset_abort_after_lsa - drop the keys collected by a system operation that is being aborted
 *
 *   tdes(in/out): transaction holding the collected keys
 *   start_lsa(in): LSA at which the aborted system operation started
 *
 * Note: The counterpart of repl_log_abort_after_lsa for the writeset, called at the same point with the
 *       same LSA, so a key stays exactly when the replication records it describes stay. A rolled-back
 *       statement and a partial rollback to a savepoint both abort a system operation. A key collected
 *       before the operation wrote any log record carries the parent LSA itself and is kept, which only
 *       makes later transactions wait longer. An overflow raised inside the operation stays raised.
 */
void
wset_abort_after_lsa (LOG_TDES *tdes, const LOG_LSA *start_lsa)
{
  assert (tdes != NULL);
  assert (start_lsa != NULL);

  tdes->wset_hashes.erase (std::remove_if (tdes->wset_hashes.begin (), tdes->wset_hashes.end (),
			   [start_lsa] (const LOG_WSET_ENTRY &e)
  {
    return LSA_GT (&e.lsa, start_lsa);
  }), tdes->wset_hashes.end ());
}

/*
 * wset_find_sysop_fallback_dependency - commit-order label for a system operation that replicated rows
 *
 *   tdes(in/out): transaction ending the system operation; the dependency source flag is set
 *   wset_parent_out(out): the label, the latest coordinate published so far (commit or earlier sysop)
 *
 * Note: The operation's keys are not published, so a later writer of the same row cannot find it in the
 *       history. Order is kept by labels alone, the same way a statement-replicated commit is handled:
 *       the operation waits for everything published before it, and wset_raise_history_start then makes
 *       every later transaction wait for the operation.
 */
void
wset_find_sysop_fallback_dependency (LOG_TDES *tdes, LOG_LSA *wset_parent_out)
{
  assert (tdes != NULL);
  assert (wset_parent_out != NULL);

  pthread_mutex_lock (&wset_History.seq_lock);
  pthread_rwlock_rdlock (&wset_History.history_lock);
  LSA_COPY (wset_parent_out, &wset_History.prev_commit_lsa);
  if (!LSA_ISNULL (&wset_History.history_start)
      && (LSA_ISNULL (wset_parent_out) || LSA_LT (wset_parent_out, &wset_History.history_start)))
    {
      LSA_COPY (wset_parent_out, &wset_History.history_start);
    }
  pthread_rwlock_unlock (&wset_History.history_lock);
  pthread_mutex_unlock (&wset_History.seq_lock);

  WSET_TRACE (1, "DEPENDENCY trid=%d mode=SYSOP_COMMIT_ORDER dependency=%lld|%d\n", tdes->trid,
	      (long long) wset_parent_out->pageid, (int) wset_parent_out->offset);
}

/*
 * wset_raise_history_start - make every later transaction depend on at least this coordinate
 *
 *   lsa(in): coordinate of a system operation applied in commit order on the slave, or the end of the
 *            recovered log at server restart
 *   reason(in): short tag for the trace line, so the log tells the two apart
 *
 * Note: Only the floor moves; the map is kept. Entries at or below the new floor can no longer lower a
 *       label, so keeping them costs nothing but avoids rebuilding the map on every operation.
 */
void
wset_raise_history_start (const LOG_LSA *lsa, const char *reason)
{
  assert (lsa != NULL);
  if (LSA_ISNULL (lsa))
    {
      assert (false);
      return;
    }

  pthread_mutex_lock (&wset_History.seq_lock);
  pthread_rwlock_wrlock (&wset_History.history_lock);
  if (LSA_ISNULL (&wset_History.history_start) || LSA_GT (lsa, &wset_History.history_start))
    {
      LSA_COPY (&wset_History.history_start, lsa);
    }
  pthread_rwlock_unlock (&wset_History.history_lock);
  pthread_mutex_unlock (&wset_History.seq_lock);

  WSET_TRACE (1, "HISTORY_START raise reason=%s history_start=%lld|%d\n", reason, (long long) lsa->pageid,
	      (int) lsa->offset);
}

/*
 * _db_ha_writeset_stat: the active server writes the counters of wset_Statistics into the one row of this
 * table, created with the database (catcls_add_ha_writeset_stat). The row is updated in place on its heap
 * page under a system operation, as a serial update is, and nothing is written for replication: each node's
 * database keeps the statistics of its own server. MVCC is disabled for the table, so the update leaves no
 * version behind. Every column is fixed size, so the new row always fits where the old one was.
 */
#if defined (SERVER_MODE)

typedef enum
{
  WSET_STAT_SERVER_START_TIME = 0,
  WSET_STAT_LAST_UPDATE_TIME,
  WSET_STAT_HISTORY_SIZE,
  WSET_STAT_HISTORY_ENTRY_COUNT,
  WSET_STAT_COMMIT_COUNT,
  WSET_STAT_COMMIT_FALLBACK_COUNT,
  WSET_STAT_FALLBACK_KEY_LIMIT_COUNT,
  WSET_STAT_FALLBACK_STATEMENT_COUNT,
  WSET_STAT_CONFLICT_REF_COUNT,
  WSET_STAT_CONFLICT_WRITE_COUNT,
  WSET_STAT_CLEAR_CAPACITY_COUNT,
  WSET_STAT_COLUMN_COUNT
} WSET_STAT_COLUMN;

static const char *wset_Stat_column_names[WSET_STAT_COLUMN_COUNT] =
{
  CT_HAWS_SERVER_START_TIME, CT_HAWS_LAST_UPDATE_TIME, CT_HAWS_HISTORY_SIZE, CT_HAWS_HISTORY_ENTRY_COUNT,
  CT_HAWS_COMMIT_COUNT, CT_HAWS_COMMIT_FALLBACK_COUNT, CT_HAWS_FALLBACK_KEY_LIMIT_COUNT,
  CT_HAWS_FALLBACK_STATEMENT_COUNT, CT_HAWS_CONFLICT_REF_COUNT, CT_HAWS_CONFLICT_WRITE_COUNT,
  CT_HAWS_CLEAR_CAPACITY_COUNT
};

/* where the row is; looked up on the first write. Only the daemon and the final write at shutdown use these,
 * and the shutdown write runs after the daemon is destroyed, so they need no lock. */
typedef struct wset_stat_row WSET_STAT_ROW;
struct wset_stat_row
{
  bool found;			/* class, attribute ids and row are known */
  bool disabled;		/* the database has no _db_ha_writeset_stat (made by an older version): never look again */
  bool error_reported;		/* a failed write was put in the error log once; later failures are not */
  OID class_oid;
  OID row_oid;
  int attrids[WSET_STAT_COLUMN_COUNT];
};

static WSET_STAT_ROW wset_Stat_row = { false, false, false, OID_INITIALIZER, OID_INITIALIZER, { 0 } };
static time_t wset_Stat_server_start_time = 0;

// *INDENT-OFF*
static cubthread::daemon *wset_Stat_daemon = NULL;
// *INDENT-ON*

static int wset_stat_find_row (THREAD_ENTRY *thread_p);
static int wset_stat_set_values (HEAP_CACHE_ATTRINFO *attr_info);
static int wset_stat_store (THREAD_ENTRY *thread_p);
static void wset_stat_execute (cubthread::entry &thread_ref);

/*
 * wset_stat_find_row - find the class, its attribute ids and its one row
 *
 * return: NO_ERROR, or an error code. When the class does not exist, wset_Stat_row.disabled is set.
 */
static int
wset_stat_find_row (THREAD_ENTRY *thread_p)
{
  HEAP_SCANCACHE scan_cache;
  RECDES recdes = RECDES_INITIALIZER;
  HFID hfid;
  OID inst_oid;
  SCAN_CODE scan;
  int i;
  int error;

  if (xlocator_find_class_oid (thread_p, CT_HA_WRITESET_STAT_NAME, &wset_Stat_row.class_oid, NULL_LOCK)
      != LC_CLASSNAME_EXIST)
    {
      er_clear ();
      wset_Stat_row.disabled = true;
      return ER_FAILED;
    }

  heap_scancache_quick_start_root_hfid (thread_p, &scan_cache);
  if (heap_get_class_record (thread_p, &wset_Stat_row.class_oid, &recdes, &scan_cache, PEEK) != S_SUCCESS)
    {
      heap_scancache_end (thread_p, &scan_cache);
      return ER_FAILED;
    }

  for (i = 0; i < WSET_STAT_COLUMN_COUNT; i++)
    {
      error = or_get_attrid (&recdes, wset_Stat_column_names[i], &wset_Stat_row.attrids[i]);
      if (error != NO_ERROR || wset_Stat_row.attrids[i] < 0)
	{
	  heap_scancache_end (thread_p, &scan_cache);
	  return (error != NO_ERROR) ? error : ER_FAILED;
	}
    }
  heap_scancache_end (thread_p, &scan_cache);

  error = heap_get_class_hfid (thread_p, &wset_Stat_row.class_oid, &hfid, NULL);
  if (error != NO_ERROR)
    {
      return error;
    }

  error = heap_scancache_start (thread_p, &scan_cache, &hfid, NULL, true, NULL);
  if (error != NO_ERROR)
    {
      return error;
    }

  OID_SET_NULL (&inst_oid);
  scan = heap_next (thread_p, &hfid, NULL, &inst_oid, &recdes, &scan_cache, PEEK);
  heap_scancache_end (thread_p, &scan_cache);
  if (scan != S_SUCCESS)
    {
      /* the row was deleted; nothing is written until it is there again */
      return ER_FAILED;
    }

  COPY_OID (&wset_Stat_row.row_oid, &inst_oid);
  wset_Stat_row.found = true;

  return NO_ERROR;
}

/*
 * wset_stat_set_values - put the current statistics into the attribute values of the row
 */
static int
wset_stat_set_values (HEAP_CACHE_ATTRINFO *attr_info)
{
  DB_VALUE values[WSET_STAT_COLUMN_COUNT];
  DB_DATETIME server_start_time;
  DB_DATETIME now;
  time_t now_sec = time (NULL);
  int i;
  int error = NO_ERROR;

  db_localdatetime (&wset_Stat_server_start_time, &server_start_time);
  db_localdatetime (&now_sec, &now);

  db_make_datetime (&values[WSET_STAT_SERVER_START_TIME], &server_start_time);
  db_make_datetime (&values[WSET_STAT_LAST_UPDATE_TIME], &now);
  db_make_bigint (&values[WSET_STAT_HISTORY_SIZE], wset_History.history_size);
  db_make_bigint (&values[WSET_STAT_HISTORY_ENTRY_COUNT], (DB_BIGINT) wset_History.map.size ());

#define WSET_STAT_COUNTER(column, counter) \
  db_make_bigint (&values[column], (DB_BIGINT) wset_Statistics.counter.load (std::memory_order_relaxed))

  WSET_STAT_COUNTER (WSET_STAT_COMMIT_COUNT, commit_count);
  WSET_STAT_COUNTER (WSET_STAT_COMMIT_FALLBACK_COUNT, commit_fallback_count);
  WSET_STAT_COUNTER (WSET_STAT_FALLBACK_KEY_LIMIT_COUNT, fallback_key_limit_count);
  WSET_STAT_COUNTER (WSET_STAT_FALLBACK_STATEMENT_COUNT, fallback_statement_count);
  WSET_STAT_COUNTER (WSET_STAT_CONFLICT_REF_COUNT, conflict_ref_count);
  WSET_STAT_COUNTER (WSET_STAT_CONFLICT_WRITE_COUNT, conflict_write_count);
  WSET_STAT_COUNTER (WSET_STAT_CLEAR_CAPACITY_COUNT, clear_capacity_count);

#undef WSET_STAT_COUNTER

  for (i = 0; i < WSET_STAT_COLUMN_COUNT && error == NO_ERROR; i++)
    {
      error = heap_attrinfo_set (&wset_Stat_row.row_oid, wset_Stat_row.attrids[i], &values[i], attr_info);
    }

  return error;
}

/*
 * wset_stat_store - write the statistics into the row of _db_ha_writeset_stat
 *
 * return: NO_ERROR, or an error code; the row keeps its previous values on an error
 *
 * Note: The caller is a system worker, so the system operation below needs no user transaction.
 */
static int
wset_stat_store (THREAD_ENTRY *thread_p)
{
  // *INDENT-OFF*
  cubmem::stack_block<IO_MAX_PAGE_SIZE> copyarea;
  // *INDENT-ON*
  record_descriptor new_recdes;
  HEAP_CACHE_ATTRINFO attr_info;
  RECDES old_recdes = RECDES_INITIALIZER;
  LOG_DATA_ADDR addr = LOG_DATA_ADDR_INITIALIZER;
  PAGE_PTR pgptr = NULL;
  VPID vpid;
  bool attr_info_started = false;
  int error = NO_ERROR;

  if (wset_Stat_row.disabled)
    {
      return NO_ERROR;
    }

  if (!wset_Stat_row.found)
    {
      error = wset_stat_find_row (thread_p);
      if (error != NO_ERROR)
	{
	  return wset_Stat_row.disabled ? NO_ERROR : error;
	}
    }

  /* read the class representation before the row's page is latched, so no other page is fixed under that latch */
  error = heap_attrinfo_start (thread_p, &wset_Stat_row.class_oid, -1, NULL, &attr_info);
  if (error != NO_ERROR)
    {
      return error;
    }
  attr_info_started = true;

  VPID_GET_FROM_OID (&vpid, &wset_Stat_row.row_oid);
  pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (pgptr == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      wset_Stat_row.found = false;
      goto end;
    }
  (void) pgbuf_check_page_ptype (thread_p, pgptr, PAGE_HEAP);

  if (spage_get_record (thread_p, pgptr, wset_Stat_row.row_oid.slotid, &old_recdes, PEEK) != S_SUCCESS
      || old_recdes.type != REC_HOME)
    {
      /* the row is gone or was moved (a user changed the table); look for it again next time */
      wset_Stat_row.found = false;
      error = ER_FAILED;
      goto end;
    }

  error = heap_attrinfo_read_dbvalues (thread_p, &wset_Stat_row.row_oid, &old_recdes, &attr_info);
  if (error != NO_ERROR)
    {
      goto end;
    }

  error = wset_stat_set_values (&attr_info);
  if (error != NO_ERROR)
    {
      goto end;
    }

  new_recdes.set_external_buffer (copyarea);
  if (heap_attrinfo_transform_to_disk (thread_p, &attr_info, &old_recdes, &new_recdes) != S_SUCCESS)
    {
      error = ER_FAILED;
      goto end;
    }
  new_recdes.set_type (REC_HOME);

  if (!spage_is_updatable (thread_p, pgptr, wset_Stat_row.row_oid.slotid, (int) new_recdes.get_size ()))
    {
      error = ER_FAILED;
      goto end;
    }

  addr.pgptr = pgptr;
  addr.offset = wset_Stat_row.row_oid.slotid;

  log_sysop_start (thread_p);
  log_append_redo_recdes (thread_p, RVHF_UPDATE, &addr, &new_recdes.get_recdes ());
  if (spage_update (thread_p, pgptr, wset_Stat_row.row_oid.slotid, &new_recdes.get_recdes ()) != SP_SUCCESS)
    {
      log_sysop_abort (thread_p);
      error = ER_FAILED;
      goto end;
    }
  pgbuf_set_dirty (thread_p, pgptr, DONT_FREE);
  log_sysop_commit (thread_p);

end:
  if (pgptr != NULL)
    {
      pgbuf_unfix_and_init (thread_p, pgptr);
    }
  if (attr_info_started)
    {
      heap_attrinfo_end (thread_p, &attr_info);
    }

  return error;
}

/*
 * wset_stat_execute - the daemon's work: write the statistics while this server is the active one
 *
 * Note: A standby server writes nothing and its row keeps the values of the last time it was active. A system
 *       worker is claimed for each write and given back right after, so the thread holds none between writes.
 */
static void
wset_stat_execute (cubthread::entry &thread_ref)
{
  int error;

  if (!BO_IS_SERVER_RESTARTED () || css_ha_server_state () != HA_SERVER_STATE_ACTIVE)
    {
      return;
    }

  thread_ref.claim_system_worker ();
  error = wset_stat_store (&thread_ref);
  thread_ref.retire_system_worker ();

  if (error != NO_ERROR && !wset_Stat_row.error_reported)
    {
      wset_Stat_row.error_reported = true;
      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_HA_GENERIC_ERROR, 1,
	      "failed to write the writeset statistics into " CT_HA_WRITESET_STAT_NAME
	      "; later failures are not reported");
    }
}

/*
 * wset_stat_daemon_init - start writing _db_ha_writeset_stat every ha_writeset_stat_interval_in_secs seconds
 *
 * Note: Nothing is started when HA is off or the interval is 0. The counters start from 0 with the server,
 *       and so does the row: server_start_time tells from when.
 */
void
wset_stat_daemon_init (void)
{
  int interval_secs = prm_get_integer_value (PRM_ID_HA_WRITESET_STAT_INTERVAL_IN_SECS);

  wset_Stat_server_start_time = time (NULL);

  if (HA_DISABLED () || interval_secs <= 0)
    {
      return;
    }

  assert (wset_Stat_daemon == NULL);

  // *INDENT-OFF*
  cubthread::looper looper = cubthread::looper (std::chrono::seconds (interval_secs));
  cubthread::entry_callable_task *daemon_task = new cubthread::entry_callable_task (wset_stat_execute);
  // *INDENT-ON*

  wset_Stat_daemon = cubthread::get_manager ()->create_daemon (looper, daemon_task, "ha-writeset-stat");
}
#endif /* SERVER_MODE */

/*
 * wset_stat_finalize - stop the periodic write and write the statistics a last time
 *
 *   thread_p(in): the shutting-down thread, holding a system worker
 *
 * Note: Called at server shutdown while the heap and log managers are still up. The last write is skipped
 *       when the server is no longer active.
 */
void
wset_stat_finalize (THREAD_ENTRY *thread_p)
{
#if defined (SERVER_MODE)
  if (wset_Stat_daemon == NULL)
    {
      return;
    }

  cubthread::get_manager ()->destroy_daemon (wset_Stat_daemon);
  wset_Stat_daemon = NULL;

  if (css_ha_server_state () == HA_SERVER_STATE_ACTIVE)
    {
      (void) wset_stat_store (thread_p);
    }
#endif /* SERVER_MODE */
}

#endif /* SERVER_MODE || SA_MODE */
