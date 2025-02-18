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
#include "xserver_interface.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubpl
{
  query_cursor::query_cursor (cubthread::entry *thread_p, QUERY_ID qid, bool oid_included)
    : m_thread (thread_p)
    , m_is_oid_included (oid_included)
    , m_is_opened (false)
    , m_fetch_count (1000) // FIXME: change the fixed value, 1000
    , m_query_id (qid)
    , m_query_entry (nullptr)
    , m_current_row_index (0)
  {
    //
  }

  query_cursor::~query_cursor ()
  {
    close ();
  }

  int
  query_cursor::reset ()
  {
    m_current_row_index = 0;
    int tran_index = LOG_FIND_THREAD_TRAN_INDEX (m_thread);
    m_query_entry = qmgr_get_query_entry (m_thread, m_query_id, tran_index);
    if (m_query_entry != NULL && m_query_entry->list_id != NULL)
      {
	m_current_tuple.resize (m_query_entry->list_id->type_list.type_cnt);
	for (DB_VALUE &val : m_current_tuple)
	  {
	    db_make_null (&val);
	  }

	return NO_ERROR;
      }

    return er_errid ();
  }

  int
  query_cursor::open ()
  {
    if (m_is_opened == false)
      {
	if (reset () == NO_ERROR && qfile_open_list_scan (m_query_entry->list_id, &m_scan_id) == NO_ERROR)
	  {
	    m_is_opened = true;
	  }
      }
    return m_is_opened ? NO_ERROR : ER_FAILED;
  }

  void
  query_cursor::close ()
  {
    if (m_is_opened)
      {
	qfile_close_scan (m_thread, &m_scan_id);
	if (m_query_entry->list_id)
	  {
	    qfile_close_list (m_thread, m_query_entry->list_id);
	  }
	clear ();
	m_is_opened = false;
      }
  }

  bool
  query_cursor::is_opened () const
  {
    return m_is_opened;
  }

  void
  query_cursor::clear ()
  {
    m_query_entry = nullptr;
    m_current_tuple.clear ();
    m_current_row_index = 0;
    m_fetch_count = 0;
  }

  SCAN_CODE
  query_cursor::prev_row ()
  {
    if (m_is_opened == false)
      {
	return S_END;
      }

    QFILE_TUPLE_RECORD tuple_record = { NULL, 0 };
    SCAN_CODE scan_code = qfile_scan_list_prev (m_thread, &m_scan_id, &tuple_record, PEEK);
    if (scan_code == S_SUCCESS)
      {
	m_current_row_index--;
	char *ptr;
	int length;
	OR_BUF buf;

	assert (m_query_entry != NULL);
	assert (m_query_entry->list_id != NULL);
	QFILE_LIST_ID *list_id = m_query_entry->list_id;
	for (int i = 0; i < list_id->type_list.type_cnt; i++)
	  {
	    QFILE_TUPLE_VALUE_FLAG flag = (QFILE_TUPLE_VALUE_FLAG) qfile_locate_tuple_value_r (tuple_record.tpl, i, &ptr, &length);
	    or_init (&buf, ptr, length);

	    TP_DOMAIN *domain = list_id->type_list.domp[i];
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
    if (m_is_opened == false)
      {
	return S_END;
      }

    QFILE_TUPLE_RECORD tuple_record = { NULL, 0 };
    SCAN_CODE scan_code = qfile_scan_list_next (m_thread, &m_scan_id, &tuple_record, PEEK);
    if (scan_code == S_SUCCESS)
      {
	m_current_row_index++;

	char *ptr;
	int length;
	OR_BUF buf;

	assert (m_query_entry != NULL);
	assert (m_query_entry->list_id != NULL);
	QFILE_LIST_ID *list_id = m_query_entry->list_id;
	for (int i = 0; i < list_id->type_list.type_cnt; i++)
	  {
	    DB_VALUE *value = &m_current_tuple[i];
	    QFILE_TUPLE_VALUE_FLAG flag = (QFILE_TUPLE_VALUE_FLAG) qfile_locate_tuple_value (tuple_record.tpl, i, &ptr, &length);
	    if (flag == V_BOUND)
	      {
		TP_DOMAIN *domain = list_id->type_list.domp[i];
		if (domain == NULL || domain->type == NULL)
		  {
		    scan_code = S_ERROR;
		    break;
		  }

		PR_TYPE *pr_type = domain->type;
		if (pr_type == NULL)
		  {
		    scan_code = S_ERROR;
		    break;
		  }

		or_init (&buf, ptr, length);

		if (pr_type->data_readval (&buf, value, domain, -1, false /* Don't copy */, NULL, 0) != NO_ERROR)
		  {
		    scan_code = S_ERROR;
		    break;
		  }
	      }
	  }
      }

    if (scan_code == S_END || scan_code == S_ERROR)
      {
	close ();
      }

    return scan_code;
  }

  void
  query_cursor::change_owner (cubthread::entry *thread_p)
  {
    if (m_thread != nullptr && m_thread->get_id () == thread_p->get_id ())
      {
	return;
      }

    close ();

    // change owner thread
    m_thread = thread_p;
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
