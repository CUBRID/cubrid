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
 * writeset.cpp - transaction-local writeset collection
 */

#ident "$Id$"

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <new>

#include "writeset.hpp"
#include "writeset_config.hpp"
#include "writeset_key_hash.hpp"
#include "writeset_stat.hpp"
#include "writeset_trace.hpp"

#include "error_manager.h"
#include "log_impl.h"
#include "object_domain.h"
#include "object_primitive.h"
#include "object_representation.h"
#include "language_support.h"
#include "porting.h"
#include "db_value_printer.hpp"
#include "string_buffer.hpp"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#if defined(SERVER_MODE) || defined(SA_MODE)

static const char *wset_fallback_reason_name (WSET_FALLBACK_REASON reason);
static void wset_trace_log_collected_key (LOG_TDES *tdes, const OID *class_oid, const VFID *index_vfid,
    DB_VALUE *key, UINT64 hash, LOG_WSET_KIND kind);
static int wset_cast_value_to_domain (DB_VALUE *source, DB_VALUE *destination, TP_DOMAIN *domain);
static int wset_cast_midxkey_element (DB_MIDXKEY *fk_midxkey, int index, TP_DOMAIN *parent_domain,
				      DB_VALUE *parent_value, bool *has_reference);
static int wset_make_parent_midxkey (DB_VALUE *fk_value, TP_DOMAIN *parent_pk_domain, DB_VALUE *parent_key,
				     bool *has_reference);
static int wset_append_entry (LOG_TDES *tdes, UINT64 hash, LOG_WSET_KIND kind);

/*
 * wset_fallback_reason_name - the name of a commit-order fallback reason, printed only in the writeset trace
 *
 *   reason(in): fallback reason
 *
 * Note: Only the FALLBACK line of the writeset trace (wset_switch_to_commit_order) uses it.
 */
static const char *
wset_fallback_reason_name (WSET_FALLBACK_REASON reason)
{
  switch (reason)
    {
    case WSET_FALLBACK_TRANSACTION_LIMIT:
      return "TRANSACTION_KEY_LIMIT";
    case WSET_FALLBACK_STATEMENT_REPLICATION:
      return "STATEMENT_REPLICATION";
    case WSET_FALLBACK_NAN_KEY:
      return "NAN_KEY";
    default:
      return "UNKNOWN";
    }
}

static void
wset_trace_log_collected_key (LOG_TDES *tdes, const OID *class_oid, const VFID *index_vfid,
			      DB_VALUE *key, UINT64 hash, LOG_WSET_KIND kind)
{
  char value_text[257];
  string_buffer value;
  const char *printed;
  size_t i;

  if (!wset_trace_is_enabled (2))
    {
      return;
    }

  value_text[0] = '\0';
  if (wset_trace_is_enabled (3))
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
	      tdes->trid, wset_trace_kind_name (kind), (unsigned long long) hash,
	      class_oid->volid, class_oid->pageid, class_oid->slotid,
	      index_vfid->volid, index_vfid->fileid,
	      wset_trace_is_enabled (3) ? " key=" : "", wset_trace_is_enabled (3) ? value_text : "");
}


/*
 * wset_append_entry - append one collected key hash to the transaction's writeset
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
wset_append_entry (LOG_TDES *tdes, UINT64 hash, LOG_WSET_KIND kind)
{
  LOG_WSET_ENTRY entry;

  assert (tdes != NULL);

  if ((INT64) tdes->wset_hashes.size () >= wset_config_get_key_limit ())
    {
      wset_stat_count_key_limit_exceeded ();
      wset_switch_to_commit_order (tdes, WSET_FALLBACK_TRANSACTION_LIMIT);
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
wset_switch_to_commit_order (LOG_TDES *tdes, WSET_FALLBACK_REASON reason)
{
  assert (tdes != NULL);
  if (tdes->wset_overflow)
    {
      return;
    }

  WSET_TRACE (1, "FALLBACK trid=%d reason=%s collected=%zu limit=%lld\n",
	      tdes->trid, wset_fallback_reason_name (reason), tdes->wset_hashes.size (),
	      (long long) wset_config_get_key_limit ());

  tdes->wset_overflow = true;
  tdes->wset_fallback_reason = (int) reason;
  std::vector<LOG_WSET_ENTRY> ().swap (tdes->wset_hashes);
  wset_stat_count_fallback ();
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
 * Note: The key is hashed the way its index compares it (wset_hash_key). A NaN key, which no hash can stand for,
 *       sends the transaction to commit order.
 */
int
wset_add_write_key (LOG_TDES *tdes, const OID *class_oid, const VFID *index_vfid, DB_VALUE *key)
{
  UINT64 hash = 0;
  int error;
  bool is_nan = false;

  assert (tdes != NULL);
  assert (class_oid != NULL);
  assert (index_vfid != NULL);
  assert (key != NULL);

  if (tdes->wset_overflow || tdes->suppress_replication != 0)
    {
      return NO_ERROR;
    }

  error = wset_hash_key (index_vfid, key, &hash, &is_nan);
  if (error != NO_ERROR)
    {
      return error;
    }

  if (is_nan)
    {
      wset_switch_to_commit_order (tdes, WSET_FALLBACK_NAN_KEY);
      return NO_ERROR;
    }

  error = wset_append_entry (tdes, hash, LOG_WSET_KIND_WRITE);
  if (error == NO_ERROR && !tdes->wset_overflow)
    {
      wset_trace_log_collected_key (tdes, class_oid, index_vfid, key, hash, LOG_WSET_KIND_WRITE);
    }

  return error;
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
      return wset_ensure_key_error (error, "failed to extract a composite foreign-key element");
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
      return wset_ensure_key_error (NO_ERROR, "child FK and parent PK column counts do not match");
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
      error = wset_ensure_key_error (error, "failed to build a parent-domain composite REF key");
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
  UINT64 hash = 0;
  int error;
  bool has_reference = true;
  bool is_nan = false;

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

  db_make_null (&casted);

  if (TP_DOMAIN_TYPE (parent_pk_domain) == DB_TYPE_MIDXKEY)
    {
      if (DB_VALUE_DOMAIN_TYPE (fk_value) != DB_TYPE_MIDXKEY)
	{
	  return wset_ensure_key_error (NO_ERROR, "composite parent PK requires a composite child FK key");
	}

      error = wset_make_parent_midxkey (fk_value, parent_pk_domain, &casted, &has_reference);
      if (error != NO_ERROR || !has_reference)
	{
	  pr_clear_value (&casted);
	  return error;
	}
    }
  else
    {
      error = wset_cast_value_to_domain (fk_value, &casted, parent_pk_domain);
      if (error != NO_ERROR)
	{
	  pr_clear_value (&casted);
	  return error;
	}

      if (DB_IS_NULL (&casted))
	{
	  pr_clear_value (&casted);
	  return wset_ensure_key_error (NO_ERROR, "non-NULL foreign key became NULL after parent-domain cast");
	}
    }

  /* the same reduction as the parent's WRITE key, now in the parent's domain */
  error = wset_hash_key (index_vfid, &casted, &hash, &is_nan);
  if (error == NO_ERROR && is_nan)
    {
      wset_switch_to_commit_order (tdes, WSET_FALLBACK_NAN_KEY);
    }
  else if (error == NO_ERROR)
    {
      error = wset_append_entry (tdes, hash, LOG_WSET_KIND_REF);
      if (error == NO_ERROR && !tdes->wset_overflow)
	{
	  wset_trace_log_collected_key (tdes, ref_class_oid, index_vfid, &casted, hash, LOG_WSET_KIND_REF);
	}
    }

  pr_clear_value (&casted);

  return error;
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
 * wset_discard_keys_after_lsa - drop keys collected after a rollback boundary
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
wset_discard_keys_after_lsa (LOG_TDES *tdes, const LOG_LSA *start_lsa)
{
  assert (tdes != NULL);
  assert (start_lsa != NULL);

  tdes->wset_hashes.erase (std::remove_if (tdes->wset_hashes.begin (), tdes->wset_hashes.end (),
			   [start_lsa] (const LOG_WSET_ENTRY &e)
  {
    return LSA_GT (&e.lsa, start_lsa);
  }), tdes->wset_hashes.end ());
}

#endif /* SERVER_MODE || SA_MODE */
