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

#include "pl_query_cursor.hpp"

#include "dbtype.h"
#include "dbtype_def.h"
#include "list_file.h"
#include "log_impl.h"
#include "object_representation.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubpl
{
  query_cursor::query_cursor (cubthread::entry *thread_p, QMGR_QUERY_ENTRY *query_entry_p, bool oid_included)
    : m_thread (thread_p)
    , m_is_oid_included (oid_included)
    , m_is_opened (false)
    , m_fetch_count (1000) // FIXME: change the fixed value, 1000
    , m_query_id (0)
  {
    reset (query_entry_p);
  }

  query_cursor::~query_cursor ()
  {
    close ();
  }

  int
  query_cursor::reset (QMGR_QUERY_ENTRY *query_entry_p)
  {
    assert (query_entry_p != NULL);

    m_query_id = query_entry_p->query_id;
    m_list_id = query_entry_p->list_id;
    m_current_row_index = 0;
    m_current_tuple.resize (m_list_id->type_list.type_cnt);
    for (DB_VALUE &val : m_current_tuple)
      {
	db_make_null (&val);
      }

    return NO_ERROR;
  }

  int
  query_cursor::open ()
  {
    if (m_is_opened == false)
      {
	qfile_open_list_scan (m_list_id, &m_scan_id);

	m_is_opened = true;
      }
    return m_is_opened ? NO_ERROR : ER_FAILED;
  }

  void
  query_cursor::close ()
  {
    if (m_is_opened)
      {
	clear ();
	qfile_close_scan (m_thread, &m_scan_id);
	m_is_opened = false;
      }
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
	    or_init (&buf, ptr, length);

	    TP_DOMAIN *domain = m_list_id->type_list.domp[i];
	    if (domain == NULL || domain->type == NULL)
	      {
		scan_code = S_ERROR;
		qfile_close_scan (m_thread, &m_scan_id);
		break;
	      }

	    DB_VALUE *value = &m_current_tuple[i];
	    PR_TYPE *pr_type = domain->type;

	    if (flag == V_BOUND)
	      {
		if (pr_type->data_readval (&buf, value, domain, -1, false /* Don't copy */, NULL, 0) != NO_ERROR)
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
    else if (scan_code == S_END)
      {
	close ();
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
	    QFILE_TUPLE_VALUE_FLAG flag = (QFILE_TUPLE_VALUE_FLAG) qfile_locate_tuple_value (tuple_record.tpl, i, &ptr, &length);
	    if (flag == V_BOUND)
	      {
		TP_DOMAIN *domain = m_list_id->type_list.domp[i];
		if (domain == NULL || domain->type == NULL)
		  {
		    scan_code = S_ERROR;
		    qfile_close_scan (m_thread, &m_scan_id);
		    break;
		  }

		PR_TYPE *pr_type = domain->type;
		if (pr_type == NULL)
		  {
		    return S_ERROR;
		  }

		or_init (&buf, ptr, length);

		if (pr_type->data_readval (&buf, value, domain, -1, false /* Don't copy */, NULL, 0) != NO_ERROR)
		  {
		    qfile_close_scan (m_thread, &m_scan_id);
		    return S_ERROR;
		  }
	      }
	  }
      }
    else if (scan_code == S_END)
      {
	close ();
      }

    return scan_code;
  }

  void
  query_cursor::change_owner (cubthread::entry *thread_p)
  {
    if (thread_p == nullptr)
      {
	close ();
	// qfile_update_qlist_count (m_thread, m_list_id, -1);
	m_thread = nullptr;
	return;
      }

    if (m_thread != nullptr && m_thread->get_id () == thread_p->get_id ())
      {
	return;
      }

    close ();

    // change owner thread
    m_thread = thread_p;

    // m_list_id is going to be destoryed on server-side, so that qlist_count has to be updated
    // qfile_update_qlist_count (thread_p, m_list_id, 1);
  }

  cubthread::entry *
  query_cursor::get_owner () const
  {
    return m_thread;
  }

  std::vector<DB_VALUE>
  query_cursor::get_current_tuple ()
  {
    return m_current_tuple;
  }

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

  bool
  query_cursor::get_is_opened ()
  {
    return m_is_opened;
  }

  int
  query_cursor::get_fetch_count ()
  {
    return m_fetch_count;
  }

  void
  query_cursor::set_fetch_count (int cnt)
  {
    if (cnt > 0 && cnt < INT32_MAX) // check invalid value
      {
	m_fetch_count = cnt;
      }
  }

}
