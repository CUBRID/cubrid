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
#include "btree.h"
#include "connection_defs.h"
#include "dbtype.h"
#include "error_manager.h"
#include "heap_file.h"
#include "lock_manager.h"
#include "locator_sr.h"
#include "log_manager.h"
#include "object_representation.h"
#include "object_representation_sr.h"
#include "record_descriptor.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/* definitions of the extern probe globals declared in copy_session.hpp */
long long g_copy_probe_ns_alloc_page = 0;
long long g_copy_probe_ns_insert_force = 0;
long long g_copy_probe_ns_log_redo = 0;
int g_copy_probe_pages_allocated = 0;
int g_copy_probe_insert_force_calls = 0;
long long g_copy_probe_ns_fa_fix_header = 0;
long long g_copy_probe_ns_fa_sysop = 0;
long long g_copy_probe_ns_fa_sysop_tail = 0;
long long g_copy_probe_ns_fa_perm_alloc = 0;
long long g_copy_probe_ns_fa_numerable = 0;
long long g_copy_probe_ns_fa_init_page = 0;
long long g_copy_probe_ns_fa_attach_watcher = 0;

/* Maximum number of rows buffered in m_recdes_collected before flushing to the
 * heap. Larger batches reduce per-flush fixed overhead (scancache open/close,
 * postpone-log records) at the cost of transient memory. */
static constexpr std::size_t COPY_FLUSH_BATCH_ROWS = 4096;

copy_session::copy_session ()
  : m_class_oid (OID_INITIALIZER)
  , m_hfid (HFID_INITIALIZER)
  , m_col_types ()
  , m_attr_ids ()
  , m_num_cols (0)
  , m_rows_loaded (0)
  , m_flush_fast_count (0)
  , m_flush_slow_count (0)
  , m_flush_fast_rows (0)
  , m_flush_slow_rows (0)
  , m_ns_decode (0)
  , m_ns_attrinfo_set (0)
  , m_ns_transform (0)
  , m_ns_flush (0)
  , m_ns_scancache_start (0)
  , m_ns_multi_insert (0)
  , m_ns_scancache_end (0)
  , m_ns_alloc_page (0)
  , m_ns_insert_force (0)
  , m_ns_log_redo (0)
  , m_pages_allocated (0)
  , m_insert_force_calls (0)
  , m_ns_fa_fix_header (0)
  , m_ns_fa_sysop (0)
  , m_ns_fa_sysop_tail (0)
  , m_ns_fa_perm_alloc (0)
  , m_ns_fa_numerable (0)
  , m_ns_fa_init_page (0)
  , m_ns_fa_attach_watcher (0)
{
}

static inline long long
copy_now_ns ()
{
  return std::chrono::duration_cast<std::chrono::nanoseconds> (
	   std::chrono::steady_clock::now ().time_since_epoch ()).count ();
}

copy_session::~copy_session ()
{
}

int
copy_session::init (THREAD_ENTRY *thread_p, const OID *class_oid, const DB_TYPE *col_types, int num_cols)
{
  int error = NO_ERROR;
  HEAP_CACHE_ATTRINFO attrinfo;
  bool attrinfo_started = false;

  COPY_OID (&m_class_oid, class_oid);
  m_num_cols = num_cols;
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
copy_session::receive_data (THREAD_ENTRY *thread_p, const char *data, int data_len)
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

  error = heap_attrinfo_start (thread_p, &m_class_oid, -1, NULL, &attrinfo);
  if (error != NO_ERROR)
    {
      goto cleanup;
    }
  attrinfo_started = true;

  while (pos < buf_len)
    {
      int bytes_consumed = 0;
      long long t0 = copy_now_ns ();
      error = decode_binary_row (buf + pos, buf_len - pos, m_col_types.data (), m_num_cols, vals, &bytes_consumed);
      m_ns_decode += copy_now_ns () - t0;

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

      /* Pack the row into a record_descriptor and queue it for batch insert. */
      {
	long long t1 = copy_now_ns ();
	for (int i = 0; i < m_num_cols; i++)
	  {
	    error = heap_attrinfo_set (&m_class_oid, m_attr_ids[i], &vals[i], &attrinfo);
	    if (error != NO_ERROR)
	      {
		m_ns_attrinfo_set += copy_now_ns () - t1;
		goto cleanup;
	      }
	  }
	m_ns_attrinfo_set += copy_now_ns () - t1;

	record_descriptor new_recdes (cubmem::STANDARD_BLOCK_ALLOCATOR);
	RECDES *old_recdes = NULL;

	long long t2 = copy_now_ns ();
	SCAN_CODE xform_rc =
	  heap_attrinfo_transform_to_disk_except_lob (thread_p, &attrinfo, old_recdes, &new_recdes);
	m_ns_transform += copy_now_ns () - t2;
	if (xform_rc != S_SUCCESS)
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
	  long long tf = copy_now_ns ();
	  error = flush_batch (thread_p);
	  m_ns_flush += copy_now_ns () - tf;
	  if (error != NO_ERROR)
	    {
	      goto cleanup;
	    }
	}
    }

  /* Flush whatever remains in this chunk before releasing the scancache. */
  if (error == NO_ERROR && !m_recdes_collected.empty ())
    {
      long long tf = copy_now_ns ();
      error = flush_batch (thread_p);
      m_ns_flush += copy_now_ns () - tf;
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

  /* Mirrors server_object_loader::flush_records. When HA is enabled, the
   * per-row log records carry accurate LSAs that the replication log needs,
   * so we loop one-by-one. Otherwise, a single multi-insert call can emit
   * page-image redo records for a big WAL reduction. */
  HEAP_SCANCACHE scancache;
  int error = NO_ERROR;
  bool scancache_started = false;
  bool has_BU_lock = lock_has_lock_on_object (&m_class_oid, oid_Root_class_oid, BU_LOCK);
  int force_count = 0;
  OID dummy_oid = OID_INITIALIZER;

  {
    long long ts = copy_now_ns ();
    error = heap_scancache_start_modify (thread_p, &scancache, &m_hfid, &m_class_oid, MULTI_ROW_INSERT, NULL);
    m_ns_scancache_start += copy_now_ns () - ts;
  }
  if (error != NO_ERROR)
    {
      goto done;
    }
  scancache_started = true;

  if (has_BU_lock && HA_DISABLED ())
    {
      m_flush_fast_count++;
      m_flush_fast_rows += (int) m_recdes_collected.size ();
      /* Use an atomic sysop so recovery can abort the entire batch on crash.
       * Required because file_perm_expand opens and commits a nested sysop
       * (durable), emitting RVFL_EXPAND inside it. The atomic_sysop_start_lsa
       * marker installed here is the ONLY crash-recovery path that can undo
       * those durably-committed nested records. Also required by file_alloc's
       * skip_inner_sysop branch, which asserts via
       * log_check_atomic_sysop_is_started before eliding its own inner sysop. */
      log_sysop_start_atomic (thread_p);
      /* reset probe globals so we capture only this call's contribution */
      g_copy_probe_ns_alloc_page = 0;
      g_copy_probe_ns_insert_force = 0;
      g_copy_probe_ns_log_redo = 0;
      g_copy_probe_pages_allocated = 0;
      g_copy_probe_insert_force_calls = 0;
      g_copy_probe_ns_fa_fix_header = 0;
      g_copy_probe_ns_fa_sysop = 0;
      g_copy_probe_ns_fa_sysop_tail = 0;
      g_copy_probe_ns_fa_perm_alloc = 0;
      g_copy_probe_ns_fa_numerable = 0;
      g_copy_probe_ns_fa_init_page = 0;
      g_copy_probe_ns_fa_attach_watcher = 0;
      long long tm = copy_now_ns ();
      error = locator_multi_insert_force (thread_p, &m_hfid, &m_class_oid, m_recdes_collected, true,
					  MULTI_ROW_INSERT, &scancache, &force_count, DB_NOT_PARTITIONED_CLASS,
					  NULL, NULL, UPDATE_INPLACE_NONE, true);
      m_ns_multi_insert += copy_now_ns () - tm;
      m_ns_alloc_page += g_copy_probe_ns_alloc_page;
      m_ns_insert_force += g_copy_probe_ns_insert_force;
      m_ns_log_redo += g_copy_probe_ns_log_redo;
      m_pages_allocated += g_copy_probe_pages_allocated;
      m_insert_force_calls += g_copy_probe_insert_force_calls;
      m_ns_fa_fix_header += g_copy_probe_ns_fa_fix_header;
      m_ns_fa_sysop += g_copy_probe_ns_fa_sysop;
      m_ns_fa_sysop_tail += g_copy_probe_ns_fa_sysop_tail;
      m_ns_fa_perm_alloc += g_copy_probe_ns_fa_perm_alloc;
      m_ns_fa_numerable += g_copy_probe_ns_fa_numerable;
      m_ns_fa_init_page += g_copy_probe_ns_fa_init_page;
      m_ns_fa_attach_watcher += g_copy_probe_ns_fa_attach_watcher;
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
      m_flush_slow_count++;
      m_flush_slow_rows += (int) m_recdes_collected.size ();
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
      long long te = copy_now_ns ();
      heap_scancache_end_modify (thread_p, &scancache);
      m_ns_scancache_end += copy_now_ns () - te;
    }
  return error;
}

int
copy_session::finish (THREAD_ENTRY *thread_p, int *rows_loaded)
{
  int error = NO_ERROR;

  if (!m_recdes_collected.empty ())
    {
      error = flush_batch (thread_p);
    }

  /* Diagnostic: branch + per-phase timings. Appended to a known file rather
   * than stderr (cub_server stderr is redirected to a deleted tmp file by the
   * service scripts) or er_log_debug (gated by PRM_ID_ER_LOG_DEBUG).
   *
   * Timings are cumulative nanoseconds across the whole session. flush time
   * covers everything inside locator_multi_insert_force (heap + WAL). The
   * remainder of wall-clock time not accounted for here is roughly chunk
   * setup, heap_attrinfo_start/end, and wire receive overhead. */
  {
    FILE *f = fopen ("/tmp/copy_counter.log", "a");
    if (f != NULL)
      {
	fprintf (f,
		 "[COPY] rows=%d fast=%d/%d slow=%d/%d | decode=%.3fs attrinfo_set=%.3fs transform=%.3fs "
		 "flush=%.3fs (scan_start=%.3fs multi_insert=%.3fs scan_end=%.3fs) "
		 "mi{alloc_page=%.3fs pages=%d, insert_force=%.3fs calls=%d, log_redo=%.3fs} "
		 "fa{fix_hdr=%.3fs sysop=%.3fs sysop_tail=%.3fs perm_alloc=%.3fs numerable=%.3fs init_page=%.3fs attach=%.3fs}\n",
		 m_rows_loaded, m_flush_fast_count, m_flush_fast_rows,
		 m_flush_slow_count, m_flush_slow_rows,
		 m_ns_decode / 1e9, m_ns_attrinfo_set / 1e9,
		 m_ns_transform / 1e9, m_ns_flush / 1e9,
		 m_ns_scancache_start / 1e9, m_ns_multi_insert / 1e9,
		 m_ns_scancache_end / 1e9,
		 m_ns_alloc_page / 1e9, m_pages_allocated,
		 m_ns_insert_force / 1e9, m_insert_force_calls,
		 m_ns_log_redo / 1e9,
		 m_ns_fa_fix_header / 1e9, m_ns_fa_sysop / 1e9,
		 m_ns_fa_sysop_tail / 1e9,
		 m_ns_fa_perm_alloc / 1e9, m_ns_fa_numerable / 1e9,
		 m_ns_fa_init_page / 1e9, m_ns_fa_attach_watcher / 1e9);
	fclose (f);
      }
  }

  *rows_loaded = m_rows_loaded;
  return error;
}

void
copy_session::abort (THREAD_ENTRY *thread_p)
{
  m_recdes_collected.clear ();
  m_leftover.clear ();
  m_rows_loaded = 0;
}
