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

#include "list_file.h"
#include "log_impl.h"
#include "query_manager.h"
#include "object_representation.h"
#include "dbtype.h"

namespace cubmethod
{
  query_cursor::query_cursor (THREAD_ENTRY *thread_p, QUERY_ID query_id)
  {
    m_thread = thread_p;
    reset (query_id);
  }

  void
  query_cursor::reset (QUERY_ID query_id)
  {
    if (query_id == NULL_QUERY_ID)
      {
	// TODO: error handling
      }
    else if (query_id < SHRT_MAX)
      {
	int tran_index = LOG_FIND_THREAD_TRAN_INDEX (m_thread);
	QMGR_QUERY_ENTRY *query_entry_p = qmgr_get_query_entry (m_thread, query_id, tran_index);
	if (query_entry_p == NULL)
	  {
	    // TODO: error handling
	    // return ER_QPROC_UNKNOWN_QUERYID;
	    assert (false);
	  }
	else
	  {
	    m_list_id = query_entry_p->list_id;
	    m_current_row = 0;
	    m_current_tuple.resize (m_list_id->type_list.type_cnt);
	  }
      }
    else
      {
	assert (false);
      }
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
    m_current_row = 0;
  }

  SCAN_CODE
  query_cursor::prev_row ()
  {
    m_current_row--;

    QFILE_TUPLE_RECORD tuple_record = { NULL, 0 };
    SCAN_CODE scan_code = qfile_scan_list_prev (m_thread, &m_scan_id, &tuple_record, PEEK);
    if (scan_code == S_SUCCESS)
      {
	char *ptr;
	int length;
	OR_BUF buf;

	for (int i = 0; i < m_list_id->type_list.type_cnt; i++)
	  {
	    QFILE_TUPLE_VALUE_FLAG flag = (QFILE_TUPLE_VALUE_FLAG) qfile_locate_tuple_value (tuple_record.tpl, i, &ptr, &length);
	    OR_BUF_INIT (buf, ptr, length);

	    TP_DOMAIN *domain = m_list_id->type_list.domp[i];
	    if (domain == NULL || domain->type == NULL)
	      {
		//TODO: error handling
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
    m_current_row++;

    QFILE_TUPLE_RECORD tuple_record = { NULL, 0 };
    SCAN_CODE scan_code = qfile_scan_list_next (m_thread, &m_scan_id, &tuple_record, PEEK);
    if (scan_code == S_SUCCESS)
      {
	char *ptr;
	int length;
	OR_BUF buf;

	for (int i = 0; i < m_list_id->type_list.type_cnt; i++)
	  {
	    QFILE_TUPLE_VALUE_FLAG flag = (QFILE_TUPLE_VALUE_FLAG) qfile_locate_tuple_value (tuple_record.tpl, i, &ptr, &length);
	    OR_BUF_INIT (buf, ptr, length);

	    TP_DOMAIN *domain = m_list_id->type_list.domp[i];
	    if (domain == NULL || domain->type == NULL)
	      {
		//TODO: error handling
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
}

