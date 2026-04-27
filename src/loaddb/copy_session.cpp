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
#include "btree.h"
#include "dbtype.h"
#include "error_manager.h"
#include "heap_file.h"
#include "locator_sr.h"
#include "log_manager.h"
#include "object_representation.h"
#include "object_representation_sr.h"
#include "record_descriptor.hpp"

#include <algorithm>
#include <cstring>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

copy_session::copy_session ()
  : m_class_oid (OID_INITIALIZER)
  , m_hfid (HFID_INITIALIZER)
  , m_col_types ()
  , m_attr_ids ()
  , m_num_cols (0)
  , m_rows_loaded (0)
{
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

    /* reject mismatched column count: client claims more columns than the
     * table actually has → leftover m_attr_ids would default to 0 and
     * silently target the wrong attribute. */
    if (num_cols > n_attrs)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_COPY_BINARY_PROTOCOL_GENERIC, 1,
		"num_cols exceeds table attribute count");
	heap_attrinfo_end (thread_p, &attrinfo);
	return ER_COPY_BINARY_PROTOCOL_GENERIC;
      }

    m_attr_ids.resize (num_cols);
    for (int i = 0; i < num_cols; i++)
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
  HEAP_SCANCACHE scancache;
  bool attrinfo_started = false;
  bool scancache_started = false;

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

  error = heap_scancache_start_modify (thread_p, &scancache, &m_hfid, &m_class_oid, SINGLE_ROW_MODIFY, NULL);
  if (error != NO_ERROR)
    {
      goto cleanup;
    }
  scancache_started = true;

  while (pos < buf_len)
    {
      int bytes_consumed = 0;
      error = decode_binary_row (buf + pos, buf_len - pos, m_col_types.data (), m_num_cols, vals, &bytes_consumed);

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

      /* insert the row using heap_attrinfo + locator_insert_force */
      {
	OID dummy_oid = OID_INITIALIZER;
	int force_count = 0;

	log_sysop_start (thread_p);

	/* set each decoded value into the attribute info cache */
	for (int i = 0; i < m_num_cols; i++)
	  {
	    error = heap_attrinfo_set (&m_class_oid, m_attr_ids[i], &vals[i], &attrinfo);
	    if (error != NO_ERROR)
	      {
		log_sysop_abort (thread_p);
		goto cleanup;
	      }
	  }

	/* transform attribute info to a proper on-disk RECDES */
	record_descriptor new_recdes (cubmem::STANDARD_BLOCK_ALLOCATOR);
	RECDES *old_recdes = NULL;

	if (heap_attrinfo_transform_to_disk_except_lob (thread_p, &attrinfo, old_recdes, &new_recdes)
	    != S_SUCCESS)
	  {
	    log_sysop_abort (thread_p);
	    error = er_errid ();
	    if (error == NO_ERROR)
	      {
		error = ER_FAILED;
	      }
	    goto cleanup;
	  }

	RECDES local_record = new_recdes.get_recdes ();
	error = locator_insert_force (thread_p, &m_hfid, &m_class_oid, &dummy_oid, &local_record, true,
				      SINGLE_ROW_INSERT, &scancache, &force_count, DB_NOT_PARTITIONED_CLASS,
				      NULL, NULL, UPDATE_INPLACE_NONE, NULL, false, true, false);

	if (error != NO_ERROR)
	  {
	    ASSERT_ERROR ();
	    log_sysop_abort (thread_p);
	    goto cleanup;
	  }

	log_sysop_attach_to_outer (thread_p);
	m_rows_loaded++;
      }

      /* clear values for next row */
      for (int i = 0; i < m_num_cols; i++)
	{
	  db_value_clear (&vals[i]);
	  db_make_null (&vals[i]);
	}
      heap_attrinfo_clear_dbvalues (&attrinfo);
    }

cleanup:
  for (int i = 0; i < m_num_cols; i++)
    {
      db_value_clear (&vals[i]);
    }
  db_private_free (thread_p, vals);

  if (scancache_started)
    {
      heap_scancache_end_modify (thread_p, &scancache);
    }
  if (attrinfo_started)
    {
      heap_attrinfo_end (thread_p, &attrinfo);
    }

  return error;
}

int
copy_session::finish (THREAD_ENTRY *thread_p, int *rows_loaded)
{
  *rows_loaded = m_rows_loaded;
  return NO_ERROR;
}

void
copy_session::abort (THREAD_ENTRY *thread_p)
{
  m_rows_loaded = 0;
}
