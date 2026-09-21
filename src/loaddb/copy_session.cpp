/*
 *
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
 * copy_session.cpp - Server-side COPY FROM STDIN session implementation
 */

#include "config.h"

#include "copy_session.hpp"
#include "copy_binary_decoder.hpp"
#include "copy_binary_format.hpp"
#include "copy_csv_decoder.hpp"
#include "btree.h"
#include "dbtype.h"
#include "error_manager.h"
#include "heap_file.h"
#include "locator_sr.h"
#include "lock_manager.h"	/* lock_has_lock_on_object */
#include "log_impl.h"
#include "log_manager.h"
#include "object_representation.h"
#include "language_support.h"
#include "object_domain.h"
#include "object_representation_sr.h"
#include "record_descriptor.hpp"
#include "xserver_interface.h"

#include <cstring>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/* rows buffered before a batch flush to the heap (amortizes scancache open/close
 * and enables the bulk multi-insert fast path) */
static const std::size_t COPY_FLUSH_BATCH_ROWS = 4096;

/* upper bound on the bytes of a single row carried across chunk boundaries. A
 * stream that never completes a row (an unbalanced CSV quote, a corrupt binary
 * field length) would otherwise buffer without limit. */
static const std::size_t COPY_MAX_ROW_BYTES = 64 * 1024 * 1024;

/* bytes of a new chunk appended at a time while completing a row that straddles
 * the chunk boundary. It doubles from here, so carrying that row costs O(row)
 * copies instead of copying the whole chunk. */
static const std::size_t COPY_CARRY_STEP = 4 * 1024;

/* savepoint taken when the session opens; the whole COPY is undone to it when the
 * stream fails, so a failed COPY leaves no rows behind */
static const char COPY_SAVEPOINT_NAME[] = "cOPYfROMsTDIN";

copy_session::copy_session ()
  : m_class_oid (OID_INITIALIZER)
  , m_hfid (HFID_INITIALIZER)
  , m_col_types ()
  , m_col_domains ()
  , m_attr_ids ()
  , m_num_cols (0)
  , m_format (COPY_FORMAT_BINARY)
  , m_delimiter (',')
  , m_quote ('"')
  , m_skip_header (false)
  , m_bulk (false)
  , m_rows_loaded (0)
  , m_savepoint_lsa (NULL_LSA)
  , m_recdes_collected ()
{
}

copy_session::~copy_session ()
{
}

int
copy_session::init (THREAD_ENTRY *thread_p, const OID *class_oid, const DB_TYPE *col_types, const int *attr_ids,
		    int num_cols, int format, int delimiter, int quote, int header, int bulk)
{
  int error = NO_ERROR;
  HEAP_CACHE_ATTRINFO attrinfo;
  bool attrinfo_started = false;

  COPY_OID (&m_class_oid, class_oid);
  m_num_cols = num_cols;
  m_format = format;
  m_delimiter = (delimiter != 0) ? (char) delimiter : ',';
  m_quote = (quote != 0) ? (char) quote : '"';
  m_skip_header = (header != 0);
  m_bulk = (bulk != 0);
  m_col_types.assign (col_types, col_types + num_cols);
  m_rows_loaded = 0;

  FILE_TYPE ftype;
  error = heap_get_class_info (thread_p, &m_class_oid, &m_hfid, &ftype, NULL);
  if (error != NO_ERROR)
    {
      return error;
    }

  /* This attrinfo only computes the attribute id mapping; it must be released
   * within this request or the per-worker resource tracker flags it as leaked. */
  error = heap_attrinfo_start (thread_p, &m_class_oid, -1, NULL, &attrinfo);
  if (error != NO_ERROR)
    {
      return error;
    }
  attrinfo_started = true;

  /* The client resolved the target columns (an explicit column list, or every
   * instance attribute in schema order) and sent their attribute ids, so the
   * mapping is taken from the request rather than re-derived here. Each id is
   * still checked against the class representation this session will insert
   * through, which may have changed since the client resolved it. */
  {
    int n_attrs = attrinfo.last_classrepr->n_attributes;
    OR_ATTRIBUTE *attrs = attrinfo.last_classrepr->attributes;

    if (num_cols > n_attrs)
      {
	error = ER_COPY_NOT_SUPPORTED;
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, "more COPY columns than the table has attributes");
	goto exit;
      }

    m_attr_ids.resize (num_cols);
    m_col_domains.resize (num_cols);
    m_col_notnull.resize (num_cols);
    for (int i = 0; i < num_cols; i++)
      {
	int j;

	for (j = 0; j < n_attrs; j++)
	  {
	    if (attrs[j].id == attr_ids[i])
	      {
		break;
	      }
	  }
	if (j == n_attrs)
	  {
	    error = ER_COPY_NOT_SUPPORTED;
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, "a COPY target column no longer exists");
	    goto exit;
	  }
	m_attr_ids[i] = attr_ids[i];

	/* The representation is released before the first row arrives, so copy
	 * the domain out rather than keeping the pointer. */
	TP_DOMAIN *dom = attrs[j].domain;
	m_col_domains[i].precision = (dom != NULL) ? dom->precision : 0;
	m_col_domains[i].codeset = (dom != NULL) ? (int) dom->codeset : (int) LANG_SYS_CODESET;
	m_col_domains[i].collation_id = (dom != NULL) ? dom->collation_id : LANG_SYS_COLLATION;
	m_col_notnull[i] = attrs[j].is_notnull ? 1 : 0;
      }
  }

  /* Rows are attached to the outer transaction as each batch is flushed, so a
   * failure part-way through the stream cannot be undone by dropping the
   * in-memory batch alone. Mark the transaction here and roll back to this
   * point in abort (). */
  error = xtran_server_savepoint (thread_p, COPY_SAVEPOINT_NAME, &m_savepoint_lsa);
  if (error != NO_ERROR)
    {
      ASSERT_ERROR ();
      LSA_SET_NULL (&m_savepoint_lsa);
      goto exit;
    }

exit:
  if (attrinfo_started)
    {
      heap_attrinfo_end (thread_p, &attrinfo);
    }

  return error;
}

int
copy_session::receive_chunk (THREAD_ENTRY *thread_p, const char *data, int data_len)
{
  int error = NO_ERROR;
  int pos = 0;
  HEAP_CACHE_ATTRINFO attrinfo;
  bool attrinfo_started = false;

  if (data_len < 0)
    {
      int format_error = (m_format == COPY_FORMAT_CSV) ? ER_COPY_CSV_FORMAT_ERROR : ER_COPY_BINARY_FORMAT_ERROR;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, format_error, 1, "negative chunk length");
      return format_error;
    }

  /* The chunk is the transport's receive buffer and is decoded where it lies.
   * Only a row that straddles the boundary is carried, in m_leftover: the tail
   * of the previous chunk, completed here by appending from this one. */
  std::size_t carry_head = m_leftover.size ();
  std::size_t carry_taken = 0;
  std::size_t carry_step = COPY_CARRY_STEP;

  DB_VALUE *vals = (DB_VALUE *) db_private_alloc (thread_p, m_num_cols * sizeof (DB_VALUE));
  if (vals == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      (size_t) (m_num_cols * sizeof (DB_VALUE)));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  for (int i = 0; i < m_num_cols; i++)
    {
      db_make_null (&vals[i]);
    }

  /* attrinfo is used only to transform each decoded row into an on-disk RECDES;
   * the actual heap insert happens in flush_batch under its own scancache. */
  error = heap_attrinfo_start (thread_p, &m_class_oid, -1, NULL, &attrinfo);
  if (error != NO_ERROR)
    {
      goto cleanup;
    }
  attrinfo_started = true;

  while (pos < data_len || !m_leftover.empty ())
    {
      int bytes_consumed = 0;
      bool skipped_header = false;
      /* a carried row is decoded from m_leftover; every other row in place */
      const bool from_carry = !m_leftover.empty ();
      const char *row = from_carry ? m_leftover.data () : data + pos;
      const int row_len = from_carry ? (int) m_leftover.size () : data_len - pos;
      int advance;

      if (m_format == COPY_FORMAT_CSV)
	{
	  /* skip a leading header line (HEADER option) before decoding data rows */
	  bool skip_only = m_skip_header;
	  error = decode_csv_row (row, row_len, m_col_types.data (), m_col_domains.data (), m_num_cols,
				  vals, m_csv_fields, m_csv_quoted, m_delimiter, m_quote, skip_only, &bytes_consumed);
	  skipped_header = (skip_only && error == NO_ERROR);
	}
      else
	{
	  error = decode_binary_row (row, row_len, m_col_types.data (), m_col_domains.data (),
				     m_num_cols, vals, &bytes_consumed);
	}

      if (error == COPY_DECODE_NEED_MORE)
	{
	  error = NO_ERROR;

	  if (!from_carry)
	    {
	      /* partial row at the tail — carry it, and only it, to the next call */
	      m_leftover.assign (data + pos, data + data_len);
	      pos = data_len;
	      carry_head = m_leftover.size ();
	      break;
	    }

	  if (pos + (int) carry_taken == data_len)
	    {
	      /* the whole chunk went into the row and it is still unfinished */
	      break;
	    }

	  {
	    std::size_t available = (std::size_t) data_len - pos - carry_taken;
	    std::size_t take = (carry_step < available) ? carry_step : available;

	    if (m_leftover.size () + take > COPY_MAX_ROW_BYTES)
	      {
		int format_error = (m_format == COPY_FORMAT_CSV) ? ER_COPY_CSV_FORMAT_ERROR : ER_COPY_BINARY_FORMAT_ERROR;
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, format_error, 1,
			"a single row exceeds the maximum buffered size");
		error = format_error;
		goto cleanup;
	      }

	    const char *src = data + pos + carry_taken;
	    m_leftover.insert (m_leftover.end (), src, src + take);
	    carry_taken += take;
	    carry_step *= 2;
	  }
	  continue;
	}

      /* A row (or the header line, or the footer) was decoded. What it consumed
       * of this chunk is its consumed bytes less whatever came from the carry. */
      if (from_carry)
	{
	  advance = bytes_consumed - (int) carry_head;
	  if (advance < 0)
	    {
	      advance = 0;
	    }
	  m_leftover.clear ();
	  carry_head = 0;
	  carry_taken = 0;
	  carry_step = COPY_CARRY_STEP;
	}
      else
	{
	  advance = bytes_consumed;
	}

      if (skipped_header)
	{
	  m_skip_header = false;
	  pos += advance;
	  continue;
	}

      if (error == COPY_DECODE_FOOTER)
	{
	  pos += advance;
	  error = NO_ERROR;
	  break;
	}

      if (error != NO_ERROR)
	{
	  goto cleanup;
	}

      pos += advance;

      /* pack the row into a record_descriptor and queue it for batch insert */
      for (int i = 0; i < m_num_cols; i++)
	{
	  /* Neither the heap nor the locator enforces NOT NULL -- it is the
	   * inserter's job, as it is the executor's for INSERT. An unquoted empty
	   * CSV field decodes to NULL, so this is one character away. */
	  if (m_col_notnull[i] && DB_IS_NULL (&vals[i]))
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NULL_CONSTRAINT_VIOLATION, 0);
	      error = ER_NULL_CONSTRAINT_VIOLATION;
	      goto cleanup;
	    }

	  error = heap_attrinfo_set (&m_class_oid, m_attr_ids[i], &vals[i], &attrinfo);
	  if (error != NO_ERROR)
	    {
	      goto cleanup;
	    }
	}

      {
	record_descriptor new_recdes (cubmem::STANDARD_BLOCK_ALLOCATOR);
	RECDES *old_recdes = NULL;

	if (heap_attrinfo_transform_to_disk_except_lob (thread_p, &attrinfo, old_recdes, &new_recdes) != S_SUCCESS)
	  {
	    error = er_errid ();
	    if (error == NO_ERROR)
	      {
		error = ER_FAILED;
	      }
	    goto cleanup;
	  }

	m_recdes_collected.push_back (std::move (new_recdes));
      }

      for (int i = 0; i < m_num_cols; i++)
	{
	  db_value_clear (&vals[i]);
	  db_make_null (&vals[i]);
	}
      heap_attrinfo_clear_dbvalues (&attrinfo);

      if (m_recdes_collected.size () >= COPY_FLUSH_BATCH_ROWS)
	{
	  error = flush_batch (thread_p);
	  if (error != NO_ERROR)
	    {
	      goto cleanup;
	    }
	}
    }

cleanup:
  for (int i = 0; i < m_num_cols; i++)
    {
      db_value_clear (&vals[i]);
    }
  db_private_free (thread_p, vals);

  if (attrinfo_started)
    {
      heap_attrinfo_end (thread_p, &attrinfo);
    }

  return error;
}

int
copy_session::flush_batch (THREAD_ENTRY *thread_p)
{
  if (m_recdes_collected.empty ())
    {
      return NO_ERROR;
    }

  HEAP_SCANCACHE scancache;
  int error = NO_ERROR;
  bool scancache_started = false;
  /* BU_LOCK is pre-acquired at open when the BULK option is set (sstream_from_init). */
  bool has_BU_lock = lock_has_lock_on_object (&m_class_oid, oid_Root_class_oid, BU_LOCK);
  int force_count = 0;
  OID dummy_oid = OID_INITIALIZER;

  error = heap_scancache_start_modify (thread_p, &scancache, &m_hfid, &m_class_oid, MULTI_ROW_INSERT, NULL);
  if (error != NO_ERROR)
    {
      goto done;
    }
  scancache_started = true;

  if (has_BU_lock)
    {
      /* Bulk path: one multi-row insert, batching page allocation and skipping
       * the per-row MVCC insert-id and per-row class lock. It logs one page
       * image where replication is not watching, and a record per row where it
       * is - locator_multi_insert_force settles that itself, so this caller no
       * longer has to ask whether HA is on. */
      log_sysop_start (thread_p);
      /* Check foreign keys, unlike loaddb, which passes dont_check_fk here.
       * loaddb restores an unloaddb image: the rows satisfied their constraints
       * once already, and checking per row would fail on table order anyway.
       * COPY takes arbitrary client data, so that assumption does not hold --
       * and skipping the check does not disable the constraint, it leaves rows
       * that violate a constraint the table still enforces for every INSERT,
       * with nothing that ever finds them again. */
      error = locator_multi_insert_force (thread_p, &m_hfid, &m_class_oid, m_recdes_collected, true,
					  MULTI_ROW_INSERT, &scancache, &force_count, DB_NOT_PARTITIONED_CLASS,
					  NULL, NULL, UPDATE_INPLACE_NONE, false);
      if (error != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  log_sysop_abort (thread_p);
	  goto done;
	}
      log_sysop_attach_to_outer (thread_p);
      m_rows_loaded += (int) m_recdes_collected.size ();
    }
  else
    {
      /* normal path: per-row insert with the full MVCC treatment, grouped under
       * one scancache. Reached when COPY was not given WITH (BULK). */
      for (std::size_t i = 0; i < m_recdes_collected.size (); ++i)
	{
	  log_sysop_start (thread_p);
	  RECDES local_record = m_recdes_collected[i].get_recdes ();
	  error = locator_insert_force (thread_p, &m_hfid, &m_class_oid, &dummy_oid, &local_record, true,
					MULTI_ROW_INSERT, &scancache, &force_count, DB_NOT_PARTITIONED_CLASS,
					NULL, NULL, UPDATE_INPLACE_NONE, NULL, has_BU_lock, false, false);
	  if (error != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      log_sysop_abort (thread_p);
	      goto done;
	    }
	  log_sysop_attach_to_outer (thread_p);
	  m_rows_loaded++;
	}
    }

done:
  m_recdes_collected.clear ();
  if (scancache_started)
    {
      /* MULTI_ROW_INSERT does not apply a unique index's key/oid delta per row;
       * it accumulates it in the scancache and leaves the caller to hand it to
       * the transaction. Nothing else does that here, so without this a COPY
       * into a table carrying a unique index leaves the global statistics at
       * whatever they were - and SELECT COUNT(*) is answered from them, so the
       * table reads as empty. loaddb does the same in stop_scancache (). */
      if (error == NO_ERROR && scancache.m_index_stats != NULL)
	{
	  for (const auto &it : scancache.m_index_stats->get_map ())
	    {
	      if (!it.second.is_unique ())
		{
		  BTREE_SET_UNIQUE_VIOLATION_ERROR (thread_p, NULL, NULL, &m_class_oid, &it.first, NULL);
		  error = ER_BTREE_UNIQUE_FAILED;
		  break;
		}
	      error = logtb_tran_update_unique_stats (thread_p, it.first, it.second, true);
	      if (error != NO_ERROR)
		{
		  ASSERT_ERROR ();
		  break;
		}
	    }
	}
      heap_scancache_end_modify (thread_p, &scancache);
    }
  return error;
}

int
copy_session::finish (THREAD_ENTRY *thread_p, stream_result *result)
{
  /* CSV has no in-band footer; a final line without a trailing newline is held
   * in m_leftover. Feed a synthetic newline so that last record is decoded and
   * inserted before reporting the count. */
  if (m_format == COPY_FORMAT_CSV && !m_leftover.empty ())
    {
      const char nl = '\n';
      int error = receive_chunk (thread_p, &nl, 1);
      if (error != NO_ERROR)
	{
	  return error;
	}
    }

  /* flush any rows still queued from the last (sub-threshold) batch */
  int error = flush_batch (thread_p);
  if (error != NO_ERROR)
    {
      return error;
    }

  result->count = m_rows_loaded;
  return NO_ERROR;
}

void
copy_session::abort (THREAD_ENTRY *thread_p)
{
  m_recdes_collected.clear ();
  m_rows_loaded = 0;

  /* Undo the batches already flushed, so a COPY that fails part-way leaves the
   * transaction as it was when the session opened. */
  if (!LSA_ISNULL (&m_savepoint_lsa))
    {
      (void) xtran_server_partial_abort (thread_p, COPY_SAVEPOINT_NAME, &m_savepoint_lsa);
      LSA_SET_NULL (&m_savepoint_lsa);
    }
}

/*
 * copy_session_create () - Decode the COPY config blob and build a copy_session.
 *   config(in): pointer to the COPY config bytes (table/ncols/options/col_types)
 *   config_len(in): length of the config blob; every read below is bounded by it
 *   error_code(out): NO_ERROR or the failure code
 *   return: opened copy_session on success, NULL on error
 *
 * COPY config encoding: table_name (string), num_cols (int), format (int),
 * delimiter (int), quote (int), header (int), bulk (int),
 * col_types (int[num_cols]), attr_ids (int[num_cols]).
 */
static stream_session *
copy_session_create (THREAD_ENTRY *thread_p, const char *config, int config_len, int *error_code)
{
  /* or_unpack_* take a mutable pointer although they only read through it */
  char *ptr = const_cast<char *> (config);
  const char *config_end = config + config_len;
  char *table_name = NULL;
  int name_len = 0;
  int num_cols = 0;
  int format = 0;
  int delimiter = 0;
  int quote = 0;
  int header = 0;
  int bulk = 0;
  DB_TYPE *col_types = NULL;
  int *attr_ids = NULL;
  copy_session *session = NULL;

  *error_code = NO_ERROR;

  /* The blob comes straight off the wire, so bound every read by config_len
   * before trusting a length taken from it. */
  if (config_len < OR_INT_SIZE)
    {
      goto invalid_config;
    }

  name_len = OR_GET_INT (ptr);
  if (name_len <= 0 || name_len > config_end - ptr - OR_INT_SIZE)
    {
      goto invalid_config;
    }

  ptr = or_unpack_string_nocopy (ptr, &table_name);
  if (table_name == NULL || table_name[name_len - 1] != '\0')
    {
      goto invalid_config;
    }

  if (config_end - ptr < 6 * OR_INT_SIZE)
    {
      goto invalid_config;
    }

  ptr = or_unpack_int (ptr, &num_cols);
  /* format: 0 = BINARY, 1 = CSV */
  ptr = or_unpack_int (ptr, &format);
  ptr = or_unpack_int (ptr, &delimiter);
  ptr = or_unpack_int (ptr, &quote);
  ptr = or_unpack_int (ptr, &header);
  ptr = or_unpack_int (ptr, &bulk);

  if (num_cols <= 0 || (config_end - ptr) / OR_INT_SIZE / 2 < num_cols)
    {
      goto invalid_config;
    }

  col_types = (DB_TYPE *) db_private_alloc (thread_p, num_cols * sizeof (DB_TYPE));
  attr_ids = (int *) db_private_alloc (thread_p, num_cols * sizeof (int));
  if (col_types == NULL || attr_ids == NULL)
    {
      *error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      goto exit;
    }

  for (int i = 0; i < num_cols; i++)
    {
      int type_val;
      ptr = or_unpack_int (ptr, &type_val);
      col_types[i] = (DB_TYPE) type_val;
    }

  for (int i = 0; i < num_cols; i++)
    {
      ptr = or_unpack_int (ptr, &attr_ids[i]);
    }

  {
    OID class_oid;
    LC_FIND_CLASSNAME status;

    /* bulk mode pre-acquires a class-level BU_LOCK (like loaddb) so the batch
     * insert can skip per-row MVCC-id and per-row class/btree locks. */
    status = xlocator_find_class_oid (thread_p, table_name, &class_oid, (bulk ? BU_LOCK : NULL_LOCK));
    if (status != LC_CLASSNAME_EXIST)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LC_UNKNOWN_CLASSNAME, 1, table_name);
	*error_code = ER_LC_UNKNOWN_CLASSNAME;
	goto exit;
      }

    session = new copy_session ();
    if (session == NULL)
      {
	*error_code = ER_OUT_OF_VIRTUAL_MEMORY;
	goto exit;
      }

    *error_code = session->init (thread_p, &class_oid, col_types, attr_ids, num_cols, format, delimiter, quote,
				 header, bulk);
    if (*error_code != NO_ERROR)
      {
	delete session;
	session = NULL;
	goto exit;
      }
  }

  goto exit;

invalid_config:
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_STREAM_SESSION_ERROR, 1, "malformed COPY session configuration");
  *error_code = ER_STREAM_SESSION_ERROR;

exit:
  if (col_types != NULL)
    {
      db_private_free (thread_p, col_types);
    }
  if (attr_ids != NULL)
    {
      db_private_free (thread_p, attr_ids);
    }

  return session;
}


/* COPY registers itself with the transport, so no transport source names
 * copy_session. Runs at load time, before any connection can open a session. */
namespace
{
  struct copy_session_registrar
  {
    copy_session_registrar ()
    {
      /* COPY's END is the end of the statement: the transfer is the whole of it */
      stream_session_register (STREAM_KIND_COPY, copy_session_create, true);
    }
  };

  copy_session_registrar copy_session_registrar_instance;
}
