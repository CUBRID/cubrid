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
 * px_scan_slot_iterator.cpp
 */

#include "px_scan_slot_iterator.hpp"
#include "fetch.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_scan
{
  slot_iterator::slot_iterator()
    : m_is_peeking (false),
      m_rest_regu_list (nullptr),
      m_rest_attr_cache (nullptr),
      m_val_list (nullptr),
      m_scan_cache (nullptr),
      m_vd (nullptr),
      m_scan_stats (nullptr),
      m_on_trace (false)
  {
  }

  slot_iterator::~slot_iterator()
  {
  }

  int slot_iterator::initialize (THREAD_ENTRY *thread_p, SCAN_ID *scan_id, val_descr *vd)
  {
    HEAP_SCAN_ID *hsidp = &scan_id->s.hsid;
    m_data_filter =
    {
      &hsidp->scan_pred,
      &hsidp->pred_attrs,
      NULL,
      NULL,
      scan_id->val_list,
      scan_id->vd,
      &hsidp->cls_oid,
      NULL,
      NULL,
      NULL,
      0,
      -1
    };
    m_cur_oid.pageid = NULL_PAGEID;
    m_class_oid = hsidp->cls_oid;
    m_is_peeking = (scan_id->fixed != 0);
    m_rest_regu_list = hsidp->rest_regu_list;
    m_val_list = scan_id->val_list;
    m_scan_cache = &hsidp->scan_cache;
    m_vpid = {NULL_PAGEID, NULL_VOLID};
    m_hfid = hsidp->hfid;
    m_rest_attr_cache = hsidp->rest_attrs.attr_cache;
    m_vd = vd;
    m_scan_stats = &scan_id->scan_stats;
    m_on_trace = thread_p->on_trace;
    m_recdes = RECDES_INITIALIZER;
    return NO_ERROR;
  }

  int slot_iterator::finalize (THREAD_ENTRY *thread_p)
  {
    return NO_ERROR;
  }

  int slot_iterator::set_page (THREAD_ENTRY *thread_p, VPID *vpid)
  {
    m_vpid = *vpid;
    OID_SET_NULL (&m_cur_oid);
    assert (m_scan_cache->page_watcher.pgptr != NULL);
    return NO_ERROR;
  }

  SCAN_CODE slot_iterator::next_qualified_slot_with_peek (THREAD_ENTRY *thread_p)
  {
    OID retry_oid;
    SCAN_CODE slot_code;
    DB_LOGICAL ev_res;
    bool is_peeking = m_is_peeking;

    while (true)
      {
	COPY_OID (&retry_oid, &m_cur_oid);
restart_scan_oid:
	m_recdes = RECDES_INITIALIZER;
	slot_code = heap_next_1page (thread_p, &m_hfid, &m_vpid, &m_class_oid,
				     &m_cur_oid, &m_recdes,
				     m_scan_cache, m_is_peeking);
	if (slot_code != S_SUCCESS)
	  {
	    return slot_code == S_END ? S_END : S_ERROR;
	  }
	if (m_scan_cache->page_watcher.pgptr != NULL)
	  {
	    LSA_COPY (&m_ref_lsa, pgbuf_get_lsa (m_scan_cache->page_watcher.pgptr));
	  }
	if (m_on_trace)
	  {
	    m_scan_stats->read_rows++;
	  }
	ev_res = eval_data_filter (thread_p, &m_cur_oid, &m_recdes, m_scan_cache,
				   &m_data_filter);
	if (m_on_trace)
	  {
	    m_scan_stats->qualified_rows++;
	  }
	if (ev_res == V_ERROR)
	  {
	    return S_ERROR;
	  }
	if (is_peeking == PEEK && m_scan_cache->page_watcher.pgptr != NULL
	    && PGBUF_IS_PAGE_CHANGED (m_scan_cache->page_watcher.pgptr, &m_ref_lsa))
	  {
	    is_peeking = COPY;
	    COPY_OID (&m_cur_oid, &retry_oid);
	    goto restart_scan_oid;
	  }
	if (ev_res != V_TRUE)
	  {
	    continue;
	  }
	if (m_rest_regu_list)
	  {
	    heap_attrinfo_read_dbvalues (thread_p, &m_cur_oid, &m_recdes, m_rest_attr_cache);
	    if (is_peeking == PEEK && m_scan_cache->page_watcher.pgptr != NULL
		&& PGBUF_IS_PAGE_CHANGED (m_scan_cache->page_watcher.pgptr, &m_ref_lsa))
	      {
		is_peeking = COPY;
		COPY_OID (&m_cur_oid, &retry_oid);
		goto restart_scan_oid;
	      }
	    if (m_val_list)
	      {
		if (fetch_val_list (thread_p, m_rest_regu_list, m_vd, &m_class_oid, &m_cur_oid,
				    NULL, PEEK) != NO_ERROR)
		  {
		    return S_ERROR;
		  }
		if (is_peeking != 0 && m_scan_cache->page_watcher.pgptr != NULL
		    && PGBUF_IS_PAGE_CHANGED (m_scan_cache->page_watcher.pgptr, &m_ref_lsa))
		  {
		    is_peeking = COPY;
		    COPY_OID (&m_cur_oid, &retry_oid);
		    goto restart_scan_oid;
		  }
	      }
	  }
	return S_SUCCESS;
      }
  }
}
