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
#include "dbtype_function.h"
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
  , m_scancache ()
  , m_scancache_started (false)
  , m_attrinfo ()
  , m_attrinfo_started (false)
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

  /* initialize attribute info for all instance attributes */
  error = heap_attrinfo_start (thread_p, &m_class_oid, -1, NULL, &m_attrinfo);
  if (error != NO_ERROR)
    {
      return error;
    }
  m_attrinfo_started = true;

  /* build m_attr_ids: map column index (def_order) to attribute repr ID.
   * When no explicit column list is given, columns arrive in def_order.
   * The last_classrepr->attributes[] array is in storage order, so we
   * need to sort by def_order to get the correct column-to-attr mapping. */
  {
    int n_attrs = m_attrinfo.last_classrepr->n_attributes;
    OR_ATTRIBUTE *attrs = m_attrinfo.last_classrepr->attributes;

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

  /* start scan cache for modification */
  error = heap_scancache_start_modify (thread_p, &m_scancache, &m_hfid, &m_class_oid, SINGLE_ROW_MODIFY, NULL);
  if (error != NO_ERROR)
    {
      heap_attrinfo_end (thread_p, &m_attrinfo);
      m_attrinfo_started = false;
      return error;
    }
  m_scancache_started = true;

  return NO_ERROR;
}

int
copy_session::receive_data (THREAD_ENTRY *thread_p, const char *data, int data_len)
{
  int error = NO_ERROR;
  int pos = 0;

  DB_VALUE *vals = (DB_VALUE *) db_private_alloc (thread_p, m_num_cols * sizeof (DB_VALUE));
  if (vals == NULL)
    {
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  for (int i = 0; i < m_num_cols; i++)
    {
      db_make_null (&vals[i]);
    }

  while (pos < data_len)
    {
      int bytes_consumed = 0;
      error = decode_binary_row (data + pos, data_len - pos, m_col_types.data (), m_num_cols, vals, &bytes_consumed);

      if (error == 1)
	{
	  /* footer sentinel — end of data */
	  pos += bytes_consumed;
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
	    error = heap_attrinfo_set (&m_class_oid, m_attr_ids[i], &vals[i], &m_attrinfo);
	    if (error != NO_ERROR)
	      {
		log_sysop_abort (thread_p);
		goto cleanup;
	      }
	  }

	/* transform attribute info to a proper on-disk RECDES */
	record_descriptor new_recdes (cubmem::STANDARD_BLOCK_ALLOCATOR);
	RECDES *old_recdes = NULL;

	if (heap_attrinfo_transform_to_disk_except_lob (thread_p, &m_attrinfo, old_recdes, &new_recdes)
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
				      SINGLE_ROW_INSERT, &m_scancache, &force_count, DB_NOT_PARTITIONED_CLASS,
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
      heap_attrinfo_clear_dbvalues (&m_attrinfo);
    }

cleanup:
  for (int i = 0; i < m_num_cols; i++)
    {
      db_value_clear (&vals[i]);
    }
  db_private_free (thread_p, vals);

  return error;
}

int
copy_session::finish (THREAD_ENTRY *thread_p, int *rows_loaded)
{
  if (m_scancache_started)
    {
      heap_scancache_end_modify (thread_p, &m_scancache);
      m_scancache_started = false;
    }

  if (m_attrinfo_started)
    {
      heap_attrinfo_end (thread_p, &m_attrinfo);
      m_attrinfo_started = false;
    }

  *rows_loaded = m_rows_loaded;
  return NO_ERROR;
}

void
copy_session::abort (THREAD_ENTRY *thread_p)
{
  if (m_scancache_started)
    {
      heap_scancache_end_modify (thread_p, &m_scancache);
      m_scancache_started = false;
    }

  if (m_attrinfo_started)
    {
      heap_attrinfo_end (thread_p, &m_attrinfo);
      m_attrinfo_started = false;
    }

  m_rows_loaded = 0;
}
