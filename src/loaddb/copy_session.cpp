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
#include "dbtype_function.h"
#include "error_manager.h"
#include "heap_file.h"
#include "locator_sr.h"
#include "log_manager.h"
#include "object_representation.h"

#include <cstring>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

copy_session::copy_session ()
  : m_class_oid (OID_INITIALIZER)
  , m_hfid (HFID_INITIALIZER)
  , m_scancache ()
  , m_scancache_started (false)
  , m_col_types ()
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

  /* start scan cache for modification */
  error = heap_scancache_start_modify (thread_p, &m_scancache, &m_hfid, &m_class_oid, SINGLE_ROW_MODIFY, NULL);
  if (error != NO_ERROR)
    {
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

      /* insert the row using locator_insert_force */
      {
	OID dummy_oid = OID_INITIALIZER;
	int force_count = 0;

	log_sysop_start (thread_p);

	/* build RECDES from DB_VALUE array */
	/* For M1, use a simple approach: pack values into a record descriptor */
	RECDES recdes;
	recdes.type = REC_HOME;

	/* Estimate record size: header + value data */
	int est_size = OR_HEADER_SIZE;
	for (int i = 0; i < m_num_cols; i++)
	  {
	    est_size += or_packed_value_size (&vals[i], 1, 1, 0);
	  }

	char *rec_buf = (char *) db_private_alloc (thread_p, est_size);
	if (rec_buf == NULL)
	  {
	    log_sysop_abort (thread_p);
	    error = ER_OUT_OF_VIRTUAL_MEMORY;
	    goto cleanup;
	  }

	recdes.data = rec_buf;
	recdes.area_size = est_size;
	recdes.length = 0;

	/* pack the record: header + values */
	OR_BUF orep;
	or_init (&orep, rec_buf, est_size);

	/* write record header */
	or_put_int (&orep, 0);	/* repid + flags placeholder */

	for (int i = 0; i < m_num_cols; i++)
	  {
	    or_put_value (&orep, &vals[i], 1, 1, 0);
	  }

	recdes.length = (int) (orep.ptr - orep.buffer);

	error = locator_insert_force (thread_p, &m_hfid, &m_class_oid, &dummy_oid, &recdes, true,
				      SINGLE_ROW_INSERT, &m_scancache, &force_count, DB_NOT_PARTITIONED_CLASS,
				      NULL, NULL, UPDATE_INPLACE_NONE, NULL, false, true, false);

	db_private_free (thread_p, rec_buf);

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

  m_rows_loaded = 0;
}
