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
#include "db_date.h"
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
#include "query_executor.h"
#include "heap_oos.hpp"
#include "heap_pending_record.hpp"
#include "session.h"
#include "string_opfunc.h"
#include "util_func.h"
#include "xserver_interface.h"

#include <algorithm>
#include <cstring>
#include <ctime>
#include <new>

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

/* rejected rows whose reason ON_ERROR IGNORE writes to the server's error log.
 * A stream whose every row fails -- one sent with the wrong DELIMITER -- would
 * otherwise log a line per row, some 300 bytes each, and rotate the log past
 * everything else in it; finish () notes how many went unlogged. */
static const std::int64_t COPY_REJECTS_LOGGED = 100;

/* The statement's time for the date and time defaults, read as qexec_execute_query () reads it for INSERT. */
static int
copy_statement_time (DB_DATETIME *datetime, DB_TIMESTAMP *epochtime)
{
  time_t sec;
  int millisec;
  struct tm tm_val;

  util_get_second_and_ms_since_epoch (&sec, &millisec);
  if (localtime_r (&sec, &tm_val) == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DATE_CONVERSION, 0);
      return ER_DATE_CONVERSION;
    }
  *epochtime = (DB_TIMESTAMP) sec;

  return db_datetime_encode (datetime, tm_val.tm_mon + 1, tm_val.tm_mday, tm_val.tm_year + 1900, tm_val.tm_hour,
			     tm_val.tm_min, tm_val.tm_sec, millisec);
}

/* Cast a default into its attribute's domain, as INSERT does before it sets the value. */
static int
copy_cast_default (OR_ATTRIBUTE *attr, DB_VALUE *value)
{
  TP_DOMAIN_STATUS status = tp_value_cast (value, value, attr->domain, false);

  if (status != DOMAIN_COMPATIBLE)
    {
      return tp_domain_status_er_set (status, ARG_FILE_LINE, value, attr->domain);
    }
  return NO_ERROR;
}

copy_session::copy_session ()
  : m_class_oid (OID_INITIALIZER)
  , m_hfid (HFID_INITIALIZER)
  , m_col_types ()
  , m_col_domains ()
  , m_attr_ids ()
  , m_auto_incr_pos (-1)
  , m_auto_incr_col (-1)
  , m_auto_incr_serial_name ()
  , m_omitted_pos ()
  , m_sys_datetime ()
  , m_sys_epochtime (0)
  , m_uuid_last_ms (0)
  , m_uuid_seq (0)
  , m_num_cols (0)
  , m_format (COPY_FORMAT_BINARY)
  , m_delimiter (',')
  , m_quote ('"')
  , m_on_error_ignore (false)
  , m_reject_limit (0)
  , m_rows_rejected (0)
  , m_skip_header (false)
  , m_bulk (false)
  , m_footer_seen (false)
  , m_rows_loaded (0)
  , m_row_number (0)
  , m_recdes_collected ()
{
}

copy_session::~copy_session ()
{
}

int
copy_session::init (THREAD_ENTRY *thread_p, const OID *class_oid, const COPY_CONFIG &config)
{
  int error = NO_ERROR;
  HEAP_CACHE_ATTRINFO attrinfo;
  bool attrinfo_started = false;

  COPY_OID (&m_class_oid, class_oid);
  m_num_cols = config.num_cols;
  m_format = config.format;
  m_delimiter = (config.delimiter != 0) ? (char) config.delimiter : ',';
  m_quote = (config.quote != 0) ? (char) config.quote : '"';
  m_null_str = (config.null_str != NULL) ? config.null_str : "";
  m_on_error_ignore = (config.on_error_ignore != 0);
  m_reject_limit = config.reject_limit;
  m_rows_rejected = 0;
  m_skip_header = (config.header != 0);
  m_bulk = (config.bulk != 0);
  m_col_types.assign (config.col_types, config.col_types + config.num_cols);
  m_rows_loaded = 0;

  /* reset at open, so a COPY that fails once opened does not report an earlier COPY's count */
  (void) session_set_copy_rejected_rows (thread_p, 0);

  FILE_TYPE ftype;
  bool has_heap = false;
  error = heap_get_class_info (thread_p, &m_class_oid, &m_hfid, &ftype, &has_heap);
  if (error != NO_ERROR)
    {
      return error;
    }
  if (!has_heap)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_COPY_NOT_SUPPORTED, 1, "the target has no heap");
      return ER_COPY_NOT_SUPPORTED;
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

    if (m_num_cols > n_attrs)
      {
	error = ER_COPY_NOT_SUPPORTED;
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, "more COPY columns than the table has attributes");
	goto exit;
      }

    for (int j = 0; j < n_attrs; j++)
      {
	if (attrs[j].is_autoincrement)
	  {
	    m_auto_incr_pos = j;
	    break;
	  }
      }

    m_attr_ids.resize (m_num_cols);
    m_col_domains.resize (m_num_cols);
    m_col_notnull.resize (m_num_cols);
    for (int i = 0; i < m_num_cols; i++)
      {
	int j;

	for (j = 0; j < n_attrs; j++)
	  {
	    if (attrs[j].id == config.attr_ids[i])
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
	m_attr_ids[i] = config.attr_ids[i];

	/* The client chose each column's decoder from the type it saw. A type
	 * changed since then would decode the bytes as something else. */
	if (attrs[j].type != config.col_types[i])
	  {
	    error = ER_COPY_NOT_SUPPORTED;
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, "a COPY target column changed its type");
	    goto exit;
	  }

	/* The representation is released before the first row arrives, so copy
	 * the domain out rather than keeping the pointer. */
	TP_DOMAIN *dom = attrs[j].domain;
	m_col_domains[i].precision = (dom != NULL) ? dom->precision : 0;
	m_col_domains[i].codeset = (dom != NULL) ? (int) dom->codeset : (int) LANG_SYS_CODESET;
	m_col_domains[i].collation_id = (dom != NULL) ? dom->collation_id : LANG_SYS_COLLATION;
	m_col_domains[i].domain = dom;
	if (j == m_auto_incr_pos)
	  {
	    m_auto_incr_col = i;
	  }
	/* NULL in an AUTO_INCREMENT column takes the next value, as in INSERT */
	m_col_notnull[i] = (attrs[j].is_notnull && j != m_auto_incr_pos) ? 1 : 0;
      }

    /* the heap alone would fill a left-out column with its original default, not its current one */
    for (int j = 0; j < n_attrs; j++)
      {
	if (j != m_auto_incr_pos
	    && std::find (m_attr_ids.begin (), m_attr_ids.end (), attrs[j].id) == m_attr_ids.end ())
	  {
	    m_omitted_pos.push_back (j);
	  }
      }
    if (!m_omitted_pos.empty ())
      {
	m_uuid_last_ms = thread_p->uuidv7_last_ms;
	m_uuid_seq = thread_p->uuidv7_seq;
	error = copy_statement_time (&m_sys_datetime, &m_sys_epochtime);
      }
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
  HEAP_SCANCACHE serial_scancache;	/* heap_set_autoincrement_value () refuses NULL; a modify one fixes no page */
  bool serial_scancache_started = false;
  /* the attrinfo may point into these rather than copy them, so they outlive every record built from it */
  std::vector<DB_VALUE> defaults (m_omitted_pos.size ());
  UUID_STATE uuid_state = { &m_uuid_last_ms, &m_uuid_seq };	/* kept in the session: UUID (7)s stay in order */

  for (DB_VALUE &value : defaults)
    {
      db_make_null (&value);
    }

  if (data_len < 0)
    {
      return format_error ("negative chunk length");
    }

  /* The footer says the encoder is done. Anything after it is a stream the two
   * sides disagree about, not data to append. */
  if (m_footer_seen && data_len > 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_COPY_BINARY_FORMAT_ERROR, 1,
	      "data arrived after the end-of-data marker");
      return ER_COPY_BINARY_FORMAT_ERROR;
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

  if (m_auto_incr_pos >= 0)
    {
      error = heap_scancache_start_modify (thread_p, &serial_scancache, &m_hfid, &m_class_oid, SINGLE_ROW_INSERT, NULL);
      if (error != NO_ERROR)
	{
	  goto cleanup;
	}
      serial_scancache_started = true;
    }

  error = eval_statement_defaults (thread_p, attrinfo, defaults);
  if (error != NO_ERROR)
    {
      goto cleanup;
    }

  while (pos < data_len || !m_leftover.empty ())
    {
      int bytes_consumed = 0;
      copy_row_error row_error = { -1, NULL };
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
	  try
	    {
	      error = decode_csv_row (row, row_len, m_col_types.data (), m_col_domains.data (), m_num_cols,
				      vals, m_csv_fields, m_csv_quoted, m_delimiter, m_quote, m_null_str, skip_only,
				      &bytes_consumed, &row_error);
	    }
	  catch (const std::bad_alloc &)
	    {
	      /* the field strings grow to the longest field the client sends */
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) row_len);
	      error = ER_OUT_OF_VIRTUAL_MEMORY;
	      goto cleanup;
	    }
	  skipped_header = (skip_only && error == NO_ERROR);
	}
      else
	{
	  error = decode_binary_row (row, row_len, m_col_types.data (), m_col_domains.data (),
				     m_num_cols, vals, &bytes_consumed, m_row_number + 1);
	}

      if (error == COPY_DECODE_NEED_MORE)
	{
	  error = NO_ERROR;

	  if (!from_carry)
	    {
	      /* partial row at the tail — carry it, and only it, to the next call */
	      error = carry (data + pos, (std::size_t) (data_len - pos));
	      if (error != NO_ERROR)
		{
		  goto cleanup;
		}
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
		error = format_error ("a single row exceeds the maximum buffered size");
		goto cleanup;
	      }

	    error = carry (data + pos + carry_taken, take);
	    if (error != NO_ERROR)
	      {
		goto cleanup;
	      }
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
	  m_footer_seen = true;
	  error = NO_ERROR;

	  if (pos < data_len)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_COPY_BINARY_FORMAT_ERROR, 1,
		      "data follows the end-of-data marker in the same chunk");
	      error = ER_COPY_BINARY_FORMAT_ERROR;
	      goto cleanup;
	    }
	  break;
	}

      /* The CSV decoder leaves the error to be raised here, where it is known
       * whether the row ends the COPY. Its errors come after it found the row's
       * line terminator, so with ON_ERROR IGNORE the stream goes on after it. */
      if (error != NO_ERROR)
	{
	  if (m_format != COPY_FORMAT_CSV)
	    {
	      goto cleanup;
	    }
	  assert (error == ER_COPY_CSV_FORMAT_ERROR && row_error.reason != NULL);
	  if (!m_on_error_ignore || bytes_consumed <= 0)
	    {
	      error = copy_format_error (ER_COPY_CSV_FORMAT_ERROR, m_row_number + 1, row_error.col, row_error.reason);
	      goto cleanup;
	    }
	  error = reject_row (vals, m_row_number + 1, row_error.col, row_error.reason);
	  if (error != NO_ERROR)
	    {
	      goto cleanup;
	    }
	  pos += advance;
	  m_row_number++;
	  continue;
	}

      pos += advance;
      m_row_number++;

      /* Neither the heap nor the locator enforces NOT NULL -- it is the
       * inserter's job, as it is the executor's for INSERT. An unquoted empty
       * CSV field decodes to NULL, so this is one character away. The error
       * names the row and column, as a decoder's does; INSERT's NOT NULL error
       * names neither. Checked before any column is set, so that a row ON_ERROR
       * IGNORE skips leaves nothing behind. */
      {
	int null_col = -1;

	for (int i = 0; i < m_num_cols && null_col < 0; i++)
	  {
	    if (m_col_notnull[i] && DB_IS_NULL (&vals[i]))
	      {
		null_col = i;
	      }
	  }
	if (null_col >= 0)
	  {
	    if (!m_on_error_ignore)
	      {
		error = format_error ("NULL in a NOT NULL column", m_row_number, null_col);
		goto cleanup;
	      }
	    error = reject_row (vals, m_row_number, null_col, "NULL in a NOT NULL column");
	    if (error != NO_ERROR)
	      {
		goto cleanup;
	      }
	    continue;
	  }
      }

      /* pack the row into a record and queue it for batch insert */
      for (int i = 0; i < m_num_cols; i++)
	{
	  if (i == m_auto_incr_col && DB_IS_NULL (&vals[i]))
	    {
	      continue;
	    }
	  error = heap_attrinfo_set (&m_class_oid, m_attr_ids[i], &vals[i], &attrinfo);
	  if (error != NO_ERROR)
	    {
	      goto cleanup;
	    }
	}

      error = set_defaults (attrinfo, defaults, &uuid_state);
      if (error != NO_ERROR)
	{
	  goto cleanup;
	}

      if (m_auto_incr_pos >= 0)
	{
	  int is_set;

	  error = heap_set_autoincrement_value (thread_p, &attrinfo, &serial_scancache, &is_set, m_auto_incr_pos,
						m_auto_incr_serial_name);
	  if (error != NO_ERROR)
	    {
	      goto cleanup;
	    }
	}

      {
	heap_pending_record record;

	/* the record is as large as the row; a failed allocation comes back as an error, with its size */
	if (heap_attrinfo_prepare_record (thread_p, &attrinfo, NULL, &record, false) != S_SUCCESS)
	  {
	    error = er_errid ();
	    if (error == NO_ERROR)
	      {
		error = ER_FAILED;
	      }
	    goto cleanup;
	  }

	const std::size_t record_bytes = record.retained_bytes ();
	try
	  {
	    m_recdes_collected.push_back (std::move (record));
	  }
	catch (const std::bad_alloc &)
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, record_bytes);
	    error = ER_OUT_OF_VIRTUAL_MEMORY;
	    goto cleanup;
	  }
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

  if (serial_scancache_started)
    {
      (void) heap_scancache_end (thread_p, &serial_scancache);
    }
  if (attrinfo_started)
    {
      heap_attrinfo_end (thread_p, &attrinfo);
    }
  for (DB_VALUE &value : defaults)
    {
      pr_clear_value (&value);
    }
  if (m_uuid_last_ms > thread_p->uuidv7_last_ms
      || (m_uuid_last_ms == thread_p->uuidv7_last_ms && m_uuid_seq > thread_p->uuidv7_seq))
    {
      thread_p->uuidv7_last_ms = m_uuid_last_ms;
      thread_p->uuidv7_seq = m_uuid_seq;
    }

  return error;
}

/* Append to the bytes of a row that straddles a chunk boundary. The client decides how many (up to
 * COPY_MAX_ROW_BYTES), so running out of memory fails this COPY rather than the server. */
int
copy_session::carry (const char *from, std::size_t len)
{
  try
    {
      m_leftover.insert (m_leftover.end (), from, from + len);
    }
  catch (const std::bad_alloc &)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, m_leftover.size () + len);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  return NO_ERROR;
}

int
copy_session::eval_statement_defaults (THREAD_ENTRY *thread_p, HEAP_CACHE_ATTRINFO &attrinfo,
				       std::vector<DB_VALUE> &defaults)
{
  for (std::size_t d = 0; d < m_omitted_pos.size (); d++)
    {
      OR_ATTRIBUTE *attr = &attrinfo.last_classrepr->attributes[m_omitted_pos[d]];
      DB_DEFAULT_EXPR_TYPE type = attr->current_default_value.default_expr.default_expr_type;

      if (!DB_IS_DEFAULT_DETERMINE_BY_ROW (type))
	{
	  int error = qexec_eval_statement_default (thread_p, attr, &m_sys_datetime, &defaults[d]);

	  /* a default value is kept in the attribute's domain; only an expression's is cast, as in INSERT */
	  if (error == NO_ERROR && type != DB_DEFAULT_NONE)
	    {
	      error = copy_cast_default (attr, &defaults[d]);
	    }
	  if (error != NO_ERROR)
	    {
	      return error;
	    }
	}
    }
  return NO_ERROR;
}

int
copy_session::set_defaults (HEAP_CACHE_ATTRINFO &attrinfo, std::vector<DB_VALUE> &defaults, UUID_STATE *uuid_state)
{
  for (std::size_t d = 0; d < m_omitted_pos.size (); d++)
    {
      OR_ATTRIBUTE *attr = &attrinfo.last_classrepr->attributes[m_omitted_pos[d]];
      int error = NO_ERROR;

      if (DB_IS_DEFAULT_DETERMINE_BY_ROW (attr->current_default_value.default_expr.default_expr_type))
	{
	  error = qexec_eval_row_default_expr (attr, &m_sys_datetime, m_sys_epochtime, uuid_state, &defaults[d]);
	  if (error == NO_ERROR)
	    {
	      error = copy_cast_default (attr, &defaults[d]);
	    }
	}
      if (error == NO_ERROR)
	{
	  error = heap_attrinfo_set (&m_class_oid, attr->id, &defaults[d], &attrinfo);
	}
      if (error != NO_ERROR)
	{
	  return error;
	}
    }
  return NO_ERROR;
}

/* Raise this stream's format error; a row < 0 names no position. */
int
copy_session::format_error (const char *reason, std::int64_t row, int col) const
{
  int error_code = (m_format == COPY_FORMAT_CSV) ? ER_COPY_CSV_FORMAT_ERROR : ER_COPY_BINARY_FORMAT_ERROR;

  if (row < 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 1, reason);
      return error_code;
    }
  return copy_format_error (error_code, row, col, reason);
}

/*
 * reject_row () - Skip a row under ON_ERROR IGNORE.
 *   return: NO_ERROR to go on with the next row, or the error that ends the COPY
 *           once more rows than REJECT_LIMIT have been rejected
 *   vals(in/out): the row's values, cleared for the next row
 *   row, col, reason(in): where the row failed and why, as copy_format_error ()
 *                         takes them
 *
 * The server's error log is where the reason for a skipped row can be found;
 * the client is told only how many. The first COPY_REJECTS_LOGGED are logged,
 * and the row past REJECT_LIMIT.
 */
int
copy_session::reject_row (DB_VALUE *vals, std::int64_t row, int col, const char *reason)
{
  m_rows_rejected++;
  const bool over_limit = (m_reject_limit > 0 && m_rows_rejected > m_reject_limit);

  /* the row that ends the COPY is logged whatever its number: the limit error
   * the client gets names the row, not why it was refused */
  if (m_rows_rejected <= COPY_REJECTS_LOGGED || over_limit)
    {
      (void) copy_format_error (ER_COPY_CSV_FORMAT_ERROR, row, col, reason);
    }

  if (over_limit)
    {
      char limit_reason[96];

      snprintf (limit_reason, sizeof (limit_reason), "more than REJECT_LIMIT %lld rows rejected",
		(long long) m_reject_limit);
      return copy_format_error (ER_COPY_CSV_FORMAT_ERROR, row, -1, limit_reason);
    }
  er_clear ();
  for (int i = 0; i < m_num_cols; i++)
    {
      db_value_clear (&vals[i]);
      db_make_null (&vals[i]);
    }
  return NO_ERROR;
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
	  (void) heap_oos_begin_insert_publication (thread_p);
	  log_sysop_abort (thread_p);
	  goto done;
	}
      log_sysop_attach_to_outer (thread_p);
      m_rows_loaded += (std::int64_t) m_recdes_collected.size ();
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
					NULL, NULL, UPDATE_INPLACE_NONE, NULL, LC_FORCE_FLAG_NONE, false,
					&m_recdes_collected[i]);
	  if (error != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      (void) heap_oos_begin_insert_publication (thread_p);
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

  /* Bytes still carried here are the front of a row whose rest never arrived:
   * an encoder that died part-way, or a connection cut mid-row. Reporting the
   * rows that did land would call a truncated load a success. For CSV the
   * newline above has already completed a final line that simply lacked one, so
   * anything left after it is genuinely unfinished. */
  if (!m_leftover.empty ())
    {
      return format_error ("the stream ended in the middle of a row");
    }

  /* A BINARY stream ends with the footer, so its absence means the encoder did
   * not get to the end -- a cut exactly on a row boundary otherwise looks like
   * a whole file. CSV has no such marker and cannot be checked this way. */
  if (m_format == COPY_FORMAT_BINARY && !m_footer_seen)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_COPY_BINARY_FORMAT_ERROR, 1,
	      "the stream ended without the end-of-data marker");
      return ER_COPY_BINARY_FORMAT_ERROR;
    }

  /* flush any rows still queued from the last (sub-threshold) batch */
  int error = flush_batch (thread_p);
  if (error != NO_ERROR)
    {
      return error;
    }

  result->count = m_rows_loaded;
  (void) session_set_copy_rejected_rows (thread_p, m_rows_rejected);

  /* Only a COPY that completes says how many reasons went unlogged. One that
   * fails loads nothing, and its error is the last line logged. */
  if (m_rows_rejected > COPY_REJECTS_LOGGED)
    {
      char msg[128];

      snprintf (msg, sizeof (msg), "%lld rows rejected, the reasons of the first %lld logged",
		(long long) m_rows_rejected, (long long) COPY_REJECTS_LOGGED);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_COPY_CSV_FORMAT_ERROR, 1, msg);
      er_clear ();
    }
  return NO_ERROR;
}

void
copy_session::abort (THREAD_ENTRY *thread_p)
{
  /* the batches already flushed are undone by the client, to the savepoint do_copy () names */
  m_recdes_collected.clear ();
  m_rows_loaded = 0;
}

/*
 * copy_session_create () - Unpack COPY's config blob (COPY_CONFIG) and open a copy_session.
 *   config(in), config_len(in): the blob as it came off the wire; every read below is bounded by config_len
 *   error_code(out): NO_ERROR or the failure code
 *   return: the opened copy_session, or NULL
 */
static stream_session *
copy_session_create (THREAD_ENTRY *thread_p, const char *config, int config_len, int *error_code)
{
  /* or_unpack_* take a mutable pointer although they only read through it */
  char *ptr = const_cast<char *> (config);
  const char *config_end = config + config_len;
  COPY_CONFIG cfg = COPY_CONFIG ();
  char *table_name = NULL;
  char *null_str = NULL;
  int name_len = 0;
  int version = 0;
  DB_TYPE *col_types = NULL;
  int *attr_ids = NULL;
  copy_session *session = NULL;

  *error_code = NO_ERROR;

  if (config_len < 2 * OR_INT_SIZE)
    {
      goto invalid_config;
    }

  /* the transport refused a version outside 1 .. COPY_CONFIG_VERSION before calling here */
  ptr = or_unpack_int (ptr, &version);

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
  cfg.table_name = table_name;

  if (config_end - ptr < 6 * OR_INT_SIZE)
    {
      goto invalid_config;
    }

  ptr = or_unpack_int (ptr, &cfg.num_cols);
  ptr = or_unpack_int (ptr, &cfg.format);
  ptr = or_unpack_int (ptr, &cfg.delimiter);
  ptr = or_unpack_int (ptr, &cfg.quote);
  ptr = or_unpack_int (ptr, &cfg.header);
  ptr = or_unpack_int (ptr, &cfg.bulk);

  /* any other value would be decoded as BINARY without its end-of-data check */
  if (cfg.format != COPY_FORMAT_BINARY && cfg.format != COPY_FORMAT_CSV)
    {
      goto invalid_config;
    }

  if (version >= 2)
    {
      int null_len;

      if (config_end - ptr < OR_INT_SIZE)
	{
	  goto invalid_config;
	}
      null_len = OR_GET_INT (ptr);
      if (null_len <= 0 || null_len > config_end - ptr - OR_INT_SIZE)
	{
	  goto invalid_config;
	}
      ptr = or_unpack_string_nocopy (ptr, &null_str);
      if (null_str == NULL || null_str[null_len - 1] != '\0')
	{
	  goto invalid_config;
	}
      cfg.null_str = null_str;
    }

  if (version >= 3)
    {
      if (config_end - ptr < OR_INT_SIZE + OR_INT64_SIZE)
	{
	  goto invalid_config;
	}
      ptr = or_unpack_int (ptr, &cfg.on_error_ignore);
      OR_GET_INT64 (ptr, &cfg.reject_limit);
      ptr += OR_INT64_SIZE;
      /* do_copy refuses these; a client that sends them anyway is not trusted
       * to skip BINARY rows whose boundary it cannot know */
      if ((cfg.on_error_ignore != 0 && cfg.format != COPY_FORMAT_CSV) || cfg.reject_limit < 0)
	{
	  goto invalid_config;
	}
    }

  if (cfg.num_cols <= 0 || (config_end - ptr) / OR_INT_SIZE / 2 < cfg.num_cols)
    {
      goto invalid_config;
    }

  col_types = (DB_TYPE *) db_private_alloc (thread_p, cfg.num_cols * sizeof (DB_TYPE));
  attr_ids = (int *) db_private_alloc (thread_p, cfg.num_cols * sizeof (int));
  if (col_types == NULL || attr_ids == NULL)
    {
      *error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      goto exit;
    }

  for (int i = 0; i < cfg.num_cols; i++)
    {
      int type_val;
      ptr = or_unpack_int (ptr, &type_val);
      col_types[i] = (DB_TYPE) type_val;
    }

  for (int i = 0; i < cfg.num_cols; i++)
    {
      ptr = or_unpack_int (ptr, &attr_ids[i]);
    }
  cfg.col_types = col_types;
  cfg.attr_ids = attr_ids;

  {
    OID class_oid;
    LC_FIND_CLASSNAME status;

    /* bulk mode pre-acquires a class-level BU_LOCK (like loaddb) so the batch
     * insert can skip per-row MVCC-id and per-row class/btree locks. */
    status = xlocator_find_class_oid (thread_p, cfg.table_name, &class_oid, (cfg.bulk ? BU_LOCK : NULL_LOCK));
    if (status != LC_CLASSNAME_EXIST)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LC_UNKNOWN_CLASSNAME, 1, cfg.table_name);
	*error_code = ER_LC_UNKNOWN_CLASSNAME;
	goto exit;
      }

    session = new copy_session ();
    if (session == NULL)
      {
	*error_code = ER_OUT_OF_VIRTUAL_MEMORY;
	goto exit;
      }

    *error_code = session->init (thread_p, &class_oid, cfg);
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
      stream_kind_desc desc = { STREAM_UPLOAD, copy_session_create, NULL, true, COPY_CONFIG_VERSION };

      if (stream_kind_register (STREAM_KIND_COPY, desc) != NO_ERROR)
	{
	  assert (false);
	}
    }
  };

  copy_session_registrar copy_session_registrar_instance;
}
