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

#include "method_query_cursor.hpp"

#include "dbtype.h"
#include "dbtype_def.h"
#include "list_file.h"
#include "log_impl.h"
#include "query_manager.h"
#include "object_representation.h"

namespace cubmethod
{
  query_cursor::query_cursor (cubthread::entry *thread_p, QUERY_ID query_id, bool is_oid_included)
  {
    m_query_id = query_id;
    m_thread = thread_p;
    reset (query_id);
    m_is_oid_included = is_oid_included;
  }

  int
  query_cursor::reset (QUERY_ID query_id)
  {
    if (query_id == NULL_QUERY_ID)
      {
	return ER_QPROC_UNKNOWN_QUERYID;
      }
    else if (query_id < SHRT_MAX)
      {
	int tran_index = LOG_FIND_THREAD_TRAN_INDEX (m_thread);
	QMGR_QUERY_ENTRY *query_entry_p = qmgr_get_query_entry (m_thread, query_id, tran_index);
	if (query_entry_p == NULL)
	  {
	    return ER_QPROC_UNKNOWN_QUERYID;
	  }
	else
	  {
	    m_list_id = query_entry_p->list_id;
	    m_current_row_index = 0;
	    m_current_tuple.resize (m_list_id->type_list.type_cnt);
	    qfile_update_qlist_count (m_thread, m_list_id, 1);
	  }
      }
    else
      {
	// tfile_vfid_p = (QMGR_TEMP_FILE *) query_id;
	assert (false);
      }
    return NO_ERROR;
  }

  int
  query_cursor::open ()
  {
    return qfile_open_list_scan (m_list_id, &m_scan_id);
  }

  void
  query_cursor::close ()
  {
    clear ();
    qfile_close_scan (m_thread, &m_scan_id);
  }

  void
  query_cursor::clear ()
  {
    m_current_tuple.clear ();
    m_current_row_index = 0;
  }

  SCAN_CODE
  query_cursor::prev_row ()
  {
    QFILE_TUPLE_RECORD tuple_record = { NULL, 0 };
    SCAN_CODE scan_code = qfile_scan_list_prev (m_thread, &m_scan_id, &tuple_record, PEEK);
    if (scan_code == S_SUCCESS)
      {
	m_current_row_index--;
	char *ptr;
	int length;
	OR_BUF buf;

	for (int i = 0; i < m_list_id->type_list.type_cnt; i++)
	  {
	    QFILE_TUPLE_VALUE_FLAG flag = (QFILE_TUPLE_VALUE_FLAG) qfile_locate_tuple_value_r (tuple_record.tpl, i, &ptr, &length);
	    OR_BUF_INIT (buf, ptr, length);

	    TP_DOMAIN *domain = m_list_id->type_list.domp[i];
	    if (domain == NULL || domain->type == NULL)
	      {
		//TODO: error handling?
		qfile_close_scan (m_thread, &m_scan_id);
	      }

	    DB_VALUE *value = &m_current_tuple[i];
	    PR_TYPE *pr_type = domain->type;

	    db_make_null (value);
	    if (flag == V_BOUND)
	      {
		if (pr_type->data_readval (&buf, value, domain, -1, true, NULL, 0) != NO_ERROR)
		  {
		    scan_code = S_ERROR;
		    break;
		  }
	      }
	    else
	      {
		/* If value is NULL, properly initialize the result */
		db_value_domain_init (value, pr_type->id, DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
	      }
	  }
      }

    return scan_code;
  }

  SCAN_CODE
  query_cursor::next_row ()
  {
    QFILE_TUPLE_RECORD tuple_record = { NULL, 0 };
    SCAN_CODE scan_code = qfile_scan_list_next (m_thread, &m_scan_id, &tuple_record, PEEK);
    if (scan_code == S_SUCCESS)
      {
	m_current_row_index++;

	char *ptr;
	int length;
	OR_BUF buf;

	for (int i = 0; i < m_list_id->type_list.type_cnt; i++)
	  {
	    DB_VALUE *value = &m_current_tuple[i];
	    db_make_null (value);

	    QFILE_TUPLE_VALUE_FLAG flag = (QFILE_TUPLE_VALUE_FLAG) qfile_locate_tuple_value (tuple_record.tpl, i, &ptr, &length);
	    if (flag == V_BOUND)
	      {
		TP_DOMAIN *domain = m_list_id->type_list.domp[i];
		if (domain == NULL || domain->type == NULL)
		  {
		    //TODO: error handling?
		    qfile_close_scan (m_thread, &m_scan_id);
		    return S_ERROR;
		  }

		PR_TYPE *pr_type = domain->type;
		if (pr_type == NULL)
		  {
		    return S_ERROR;
		  }

		OR_BUF_INIT (buf, ptr, length);

		if (pr_type->data_readval (&buf, value, domain, -1, true, NULL, 0) != NO_ERROR)
		  {
		    scan_code = S_ERROR;
		    break;
		  }
	      }
	  }
      }

    return scan_code;
  }

  std::vector<DB_VALUE>
  query_cursor::get_current_tuple ()
  {
    return m_current_tuple;
  }

  void clear ();

  int
  query_cursor::get_current_index ()
  {
    return m_current_row_index;
  }

  OID *
  query_cursor::get_current_oid ()
  {
    if (m_is_oid_included)
      {
	DB_VALUE *first_value = &m_current_tuple[0];
	DB_TYPE type = DB_VALUE_DOMAIN_TYPE (first_value);

	if (type == DB_TYPE_OID)
	  {
	    return db_get_oid (first_value);
	  }
      }
    return NULL;
  }

  bool
  query_cursor::get_is_oid_included ()
  {
    return m_is_oid_included;
  }

  QUERY_ID
  query_cursor::get_query_id ()
  {
    return m_query_id;
  }
}
