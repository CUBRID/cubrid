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
 * px_scan_slot_iterator_list.cpp
 */

#include "px_scan_slot_iterator_list.hpp"
#include "fetch.h"
#include "list_file.h"
#include "object_representation.h"
#include "query_manager.h"
#include "error_manager.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_scan
{
  slot_iterator_list::slot_iterator_list ()
    : m_curr_pgptr (nullptr),
      m_curr_tpl (nullptr),
      m_curr_tplno (0),
      m_tuple_count (0),
      m_curr_tfile (nullptr),
      m_list_id (nullptr),
      m_rest_regu_list (nullptr),
      m_tplrecp (nullptr),
      m_val_list (nullptr),
      m_vd (nullptr),
      m_scan_stats (nullptr),
      m_on_trace (false)
  {
    m_scan_pred = { nullptr, nullptr, nullptr };
    m_tplrec.size = 0;
    m_tplrec.tpl = nullptr;
  }

  slot_iterator_list::~slot_iterator_list ()
  {
  }

  int
  slot_iterator_list::initialize (THREAD_ENTRY *thread_p, SCAN_ID *scan_id, val_descr *vd)
  {
    LLIST_SCAN_ID *llsidp = &scan_id->s.llsid;
    m_scan_pred = llsidp->scan_pred;
    m_rest_regu_list = llsidp->rest_regu_list;
    m_tplrecp = llsidp->tplrecp;
    m_list_id = llsidp->list_id;
    m_curr_tfile = nullptr;
    m_val_list = scan_id->val_list;
    m_vd = vd;
    m_scan_stats = &scan_id->scan_stats;
    m_on_trace = thread_p->on_trace;
    m_curr_pgptr = nullptr;
    m_curr_tpl = nullptr;
    m_curr_tplno = 0;
    m_tuple_count = 0;
    return NO_ERROR;
  }

  int
  slot_iterator_list::finalize (THREAD_ENTRY *thread_p)
  {
    if (m_curr_pgptr != nullptr)
      {
	qmgr_free_old_page (thread_p, m_curr_pgptr, m_curr_tfile);
	m_curr_pgptr = nullptr;
      }
    if (m_tplrec.tpl != nullptr)
      {
	db_private_free_and_init (thread_p, m_tplrec.tpl);
	m_tplrec.size = 0;
      }
    return NO_ERROR;
  }

  int
  slot_iterator_list::set_page (THREAD_ENTRY *thread_p, PAGE_PTR page, QMGR_TEMP_FILE *tfile)
  {
    /* Free previous page with its own tfile, then adopt the new fix. */
    if (m_curr_pgptr != nullptr)
      {
	qmgr_free_old_page (thread_p, m_curr_pgptr, m_curr_tfile);
	m_curr_pgptr = nullptr;
      }

    m_curr_pgptr = page;
    m_curr_tfile = tfile;
    m_curr_tpl = (char *) m_curr_pgptr + QFILE_PAGE_HEADER_SIZE;
    m_curr_tplno = 0;
    m_tuple_count = QFILE_GET_TUPLE_COUNT (m_curr_pgptr);
    return NO_ERROR;
  }

  SCAN_CODE
  slot_iterator_list::next_qualified_slot_with_peek (THREAD_ENTRY *thread_p)
  {
    DB_LOGICAL ev_res;
    bool has_overflow_page = (QFILE_GET_OVERFLOW_PAGE_ID (m_curr_pgptr) != NULL_PAGEID);

    while (m_curr_tplno < m_tuple_count)
      {
	QFILE_TUPLE tpl;

	if (has_overflow_page)
	  {
	    /* qfile_get_tuple delegates to qfile_assemble_overflow_tuple at list_file.c:4673 when overflow page;
	     * has_overflow_page guard above ensures equivalent path. */
	    if (qfile_assemble_overflow_tuple (thread_p, m_curr_pgptr, &m_tplrec, m_list_id->tfile_vfid) != NO_ERROR)
	      {
		return S_ERROR;
	      }
	    tpl = m_tplrec.tpl;
	  }
	else
	  {
	    tpl = m_curr_tpl;
	  }

	m_curr_tpl += QFILE_GET_TUPLE_LENGTH (m_curr_tpl);
	m_curr_tplno++;

	if (m_val_list)
	  {
	    if (fetch_val_list (thread_p, m_scan_pred.regu_list, m_vd, nullptr, nullptr, tpl, PEEK) != NO_ERROR)
	      {
		return S_ERROR;
	      }
	  }

	if (m_on_trace)
	  {
	    m_scan_stats->read_rows++;
	  }

	ev_res = V_TRUE;
	if (m_scan_pred.pr_eval_fnc && m_scan_pred.pred_expr)
	  {
	    ev_res = (*m_scan_pred.pr_eval_fnc) (thread_p, m_scan_pred.pred_expr, m_vd, nullptr);
	    if (ev_res == V_ERROR)
	      {
		return S_ERROR;
	      }
	  }

	if (ev_res != V_TRUE)
	  {
	    continue;
	  }

	if (m_on_trace)
	  {
	    m_scan_stats->qualified_rows++;
	  }

	if (m_val_list && m_rest_regu_list)
	  {
	    if (fetch_val_list (thread_p, m_rest_regu_list, m_vd, nullptr, nullptr, tpl, PEEK) != NO_ERROR)
	      {
		return S_ERROR;
	      }
	  }

	if (m_tplrecp)
	  {
	    m_tplrecp->tpl = tpl;
	  }

	return S_SUCCESS;
      }

    return S_END;
  }
}
