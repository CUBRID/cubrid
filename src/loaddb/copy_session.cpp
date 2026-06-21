/*
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
#include "connection_defs.h"	/* HA_DISABLED */
#include "heap_file.h"
#include "locator_sr.h"
#include "lock_manager.h"	/* lock_has_lock_on_object */
#include "log_manager.h"
#include "object_representation.h"
#include "object_representation_sr.h"
#include "record_descriptor.hpp"

#include <algorithm>
#include <cstring>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/* rows buffered before a batch flush to the heap (amortizes scancache open/close
 * and enables the bulk multi-insert fast path) */
static const std::size_t COPY_FLUSH_BATCH_ROWS = 4096;

copy_session::copy_session ()
  : m_class_oid (OID_INITIALIZER)
  , m_hfid (HFID_INITIALIZER)
  , m_col_types ()
  , m_attr_ids ()
  , m_num_cols (0)
  , m_format (COPY_FORMAT_BINARY)
  , m_delimiter (',')
  , m_quote ('"')
  , m_skip_header (false)
  , m_bulk (false)
  , m_rows_loaded (0)
  , m_recdes_collected ()
{
}

copy_session::~copy_session ()
{
}

int
copy_session::init (THREAD_ENTRY *thread_p, const OID *class_oid, const DB_TYPE *col_types, int num_cols, int format,
		    int delimiter, int quote, int header, int bulk)
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

  /* get HFID from class OID */
  FILE_TYPE ftype;
  error = heap_get_class_info (thread_p, &m_class_oid, &m_hfid, &ftype, NULL);
  if (error != NO_ERROR)
    {
      return error;
    }

  /* Open a temporary heap_attrinfo only to compute the attribute id mapping.
   * We must release it within this request so the per-worker resource tracker
   * does not flag the allocation as leaked when the task ends. */
  error = heap_attrinfo_start (thread_p, &m_class_oid, -1, NULL, &attrinfo);
  if (error != NO_ERROR)
    {
      return error;
    }
  attrinfo_started = true;

  /* build m_attr_ids: map column index (def_order) to attribute repr ID.
   * When no explicit column list is given, columns arrive in def_order.
   * The last_classrepr->attributes[] array is in storage order, so we
   * need to sort by def_order to get the correct column-to-attr mapping. */
  {
    int n_attrs = attrinfo.last_classrepr->n_attributes;
    OR_ATTRIBUTE *attrs = attrinfo.last_classrepr->attributes;

    /* build sorted index array by def_order */
    std::vector<int> order (n_attrs);
    for (int i = 0; i < n_attrs; i++)
      {
	order[i] = i;
      }
    std::sort (order.begin (), order.end (), [&attrs] (int a, int b)
    {
      return attrs[a].def_order < attrs[b].def_order;
    });

    m_attr_ids.resize (num_cols);
    for (int i = 0; i < num_cols && i < n_attrs; i++)
      {
	m_attr_ids[i] = attrs[order[i]].id;
      }
  }

  if (attrinfo_started)
    {
      heap_attrinfo_end (thread_p, &attrinfo);
    }

  return NO_ERROR;
}

int
copy_session::receive_chunk (THREAD_ENTRY *thread_p, const char *data, int data_len)
{
  int error = NO_ERROR;
  int pos = 0;
  HEAP_CACHE_ATTRINFO attrinfo;
  bool attrinfo_started = false;

  /* If a previous chunk ended mid-row, prepend the leftover bytes so the
   * combined buffer starts at a row boundary. */
  std::vector<char> combined;
  const char *buf;
  int buf_len;
  if (!m_leftover.empty ())
    {
      combined.reserve (m_leftover.size () + data_len);
      combined.insert (combined.end (), m_leftover.begin (), m_leftover.end ());
      combined.insert (combined.end (), data, data + data_len);
      m_leftover.clear ();
      buf = combined.data ();
      buf_len = (int) combined.size ();
    }
  else
    {
      buf = data;
      buf_len = data_len;
    }

  DB_VALUE *vals = (DB_VALUE *) db_private_alloc (thread_p, m_num_cols * sizeof (DB_VALUE));
  if (vals == NULL)
    {
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

  while (pos < buf_len)
    {
      int bytes_consumed = 0;
      if (m_format == COPY_FORMAT_CSV)
	{
	  /* skip a leading header line (HEADER option) before decoding data rows */
	  bool skip_only = m_skip_header;
	  error = decode_csv_row (buf + pos, buf_len - pos, m_col_types.data (), m_num_cols, vals,
				  m_csv_fields, m_csv_quoted, m_delimiter, m_quote, skip_only, &bytes_consumed);
	  if (skip_only && error == NO_ERROR)
	    {
	      m_skip_header = false;
	      pos += bytes_consumed;
	      continue;
	    }
	}
      else
	{
	  error = decode_binary_row (buf + pos, buf_len - pos, m_col_types.data (), m_num_cols, vals, &bytes_consumed);
	}

      if (error == COPY_DECODE_FOOTER)
	{
	  pos += bytes_consumed;
	  error = NO_ERROR;
	  break;
	}

      if (error == COPY_DECODE_NEED_MORE)
	{
	  /* partial row at the tail — save it for the next call */
	  m_leftover.assign (buf + pos, buf + buf_len);
	  error = NO_ERROR;
	  break;
	}

      if (error != NO_ERROR)
	{
	  goto cleanup;
	}

      pos += bytes_consumed;

      /* pack the row into a record_descriptor and queue it for batch insert */
      for (int i = 0; i < m_num_cols; i++)
	{
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

      /* clear values for next row */
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
  /* BU_LOCK is pre-acquired at open when the BULK option is set (scopy_from_init). */
  bool has_BU_lock = lock_has_lock_on_object (&m_class_oid, oid_Root_class_oid, BU_LOCK);
  int force_count = 0;
  OID dummy_oid = OID_INITIALIZER;

  error = heap_scancache_start_modify (thread_p, &scancache, &m_hfid, &m_class_oid, MULTI_ROW_INSERT, NULL);
  if (error != NO_ERROR)
    {
      goto done;
    }
  scancache_started = true;

  if (has_BU_lock && HA_DISABLED ())
    {
      /* bulk fast path: one multi-row insert emits page-image redo (no per-row
       * undo / MVCC insert-id / per-row class lock). Requires HA disabled, since
       * the page-based log record has no accurate per-row LSA for replication. */
      log_sysop_start (thread_p);
      error = locator_multi_insert_force (thread_p, &m_hfid, &m_class_oid, m_recdes_collected, true,
					  MULTI_ROW_INSERT, &scancache, &force_count, DB_NOT_PARTITIONED_CLASS,
					  NULL, NULL, UPDATE_INPLACE_NONE, true);
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
      /* normal path: per-row insert (MVCC, per-row lock) grouped under one
       * scancache. has_BU_lock is false here unless HA is enabled in bulk mode. */
      for (std::size_t i = 0; i < m_recdes_collected.size (); ++i)
	{
	  log_sysop_start (thread_p);
	  RECDES local_record = m_recdes_collected[i].get_recdes ();
	  error = locator_insert_force (thread_p, &m_hfid, &m_class_oid, &dummy_oid, &local_record, true,
					MULTI_ROW_INSERT, &scancache, &force_count, DB_NOT_PARTITIONED_CLASS,
					NULL, NULL, UPDATE_INPLACE_NONE, NULL, has_BU_lock, true, false);
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
}
