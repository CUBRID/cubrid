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
 * px_heap_scan_mergable_list.cpp - mergable list for parallel heap scan
 */
#include "xasl.h"

#include "px_heap_scan_mergable_list.hpp"
#include "query_opfunc.h"
#include "regu_var.hpp"
#include "object_representation.h"
#include "query_manager.h"
#include "thread_manager.hpp"
#include "dbtype.h"
#include "fetch.h"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
namespace parallel_heap_scan
{
  mergable_list_array::mergable_list_array (THREAD_ENTRY *thread_p, std::size_t size)
  {
    m_thread_p = thread_p;
    m_size = size;
    m_list_ids.resize (size);
  }

  mergable_list_array::~mergable_list_array()
  {
    for (QFILE_LIST_ID *list_id : m_list_ids)
      {
	if (list_id)
	  {
	    if (list_id->type_list.type_cnt != 0)
	      {
		qfile_clear_list_id (list_id);
	      }
	  }
      }
  }

  QFILE_LIST_ID *mergable_list_array::get_merged_list_id()
  {
    parallel_query::list_merger merger (m_thread_p);
    for (QFILE_LIST_ID *list_id : m_list_ids)
      {
	merger.add_list_id (list_id);
      }
    return merger.get_merged_list_id();
  }

  QFILE_LIST_ID **mergable_list_array::get_list_id_p (std::size_t index)
  {
    return &m_list_ids[index];
  }

  mergable_list_writer::mergable_list_writer (QFILE_LIST_ID **list_id_p, QUERY_ID query_id, VALPTR_LIST *outptr_list)
  {
    m_list_id_p = list_id_p;
    m_query_id = query_id;
    m_outptr_list = outptr_list;
    m_tpl_buf.tpl = (char *) malloc (DB_PAGESIZE);
    m_tpl_buf.size = DB_PAGESIZE;
    m_is_list_id_domain_resolved = false;
  }

  mergable_list_writer::~mergable_list_writer()
  {
    free (m_tpl_buf.tpl);
  }

  bool mergable_list_writer::open (THREAD_ENTRY *thread_p, PARALLEL_HEAP_SCAN_ID *phsid,
				   REGU_VARIABLE_LIST regu_list_pred, REGU_VARIABLE_LIST regu_list_rest, VAL_DESCR *vd)
  {
    QFILE_TUPLE_VALUE_TYPE_LIST type_list;
    REGU_VARIABLE_LIST valptr, orig_pred_regu, new_pred_regu, orig_rest_regu, new_rest_regu;
    int valptr_idx;
    size_t size;
    QFILE_LIST_ID *list_id;
    int i;
    m_vd = vd;
    qdata_get_valptr_type_list (thread_p, m_outptr_list, &type_list);

    (*m_list_id_p) = qfile_open_list (thread_p, &type_list, NULL, m_query_id, QFILE_FLAG_ALL|QFILE_NOT_USE_MEMBUF, NULL);

    if (!*m_list_id_p)
      {
	return false;
      }
    list_id = *m_list_id_p;

    size = list_id->type_list.type_cnt * DB_SIZEOF (DB_VALUE *);

    list_id->tpl_descr.f_valp = (DB_VALUE **) malloc (size);
    if (list_id->tpl_descr.f_valp == NULL)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, size);
	return false;
      }

    size = list_id->type_list.type_cnt * sizeof (bool);
    list_id->tpl_descr.clear_f_val_at_clone_decache = (bool *) malloc (size);
    if (list_id->tpl_descr.clear_f_val_at_clone_decache == NULL)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, size);
	return false;
      }
    for (i = 0; i < list_id->type_list.type_cnt; i++)
      {
	list_id->tpl_descr.clear_f_val_at_clone_decache[i] = false;
      }

    return true;
  }

  void mergable_list_writer::close (THREAD_ENTRY *thread_p)
  {
    qfile_close_list (thread_p, *m_list_id_p);
  }

  int mergable_list_writer::write (THREAD_ENTRY *thread_p)
  {
    int err_code = NO_ERROR;
    QPROC_TPLDESCR_STATUS status;
    QFILE_TUPLE_RECORD *tplrec;

    __builtin_prefetch ((*m_list_id_p), 1, 3);

    status = qdata_generate_tuple_desc_for_valptr_list (thread_p, m_outptr_list, m_vd, & ((*m_list_id_p)->tpl_descr));

    if (unlikely (!m_is_list_id_domain_resolved))
      {
	qfile_update_domains_on_type_list (thread_p, *m_list_id_p, m_outptr_list);
	m_is_list_id_domain_resolved = (*m_list_id_p)->is_domain_resolved;
      }

    if (likely (status == QPROC_TPLDESCR_SUCCESS))
      {
	if (qfile_generate_tuple_into_list (thread_p, (*m_list_id_p), T_NORMAL) != NO_ERROR)
	  {
	    return ER_FAILED;
	  }

	return NO_ERROR;
      }
    else if (unlikely (status == QPROC_TPLDESCR_FAILURE))
      {
	return ER_FAILED;
      }
    else if (unlikely (status == QPROC_TPLDESCR_RETRY_SET_TYPE || status == QPROC_TPLDESCR_RETRY_BIG_REC))
      {
	tplrec = make_tuple_record (thread_p);
	if (tplrec == nullptr)
	  {
	    return ER_FAILED;
	  }
	err_code = qfile_add_tuple_to_list (thread_p, (*m_list_id_p), tplrec->tpl);
	return err_code;
      }
    assert (0);
    return ER_FAILED;
  }

  QFILE_TUPLE_RECORD *mergable_list_writer::make_tuple_record (THREAD_ENTRY *thread_p)
  {
    int error_code = NO_ERROR;

    error_code = qdata_copy_valptr_list_to_tuple (thread_p, m_outptr_list, m_vd, &m_tpl_buf);
    if (error_code != NO_ERROR)
      {
	return nullptr;
      }
    if (!m_is_list_id_domain_resolved)
      {
	qfile_update_domains_on_type_list (thread_p, *m_list_id_p, m_outptr_list);
	m_is_list_id_domain_resolved = (*m_list_id_p)->is_domain_resolved;
      }

    return &m_tpl_buf;
  }

  bool mergable_list_writer::is_tfile_allocated() const
  {
    return (*m_list_id_p)->tfile_vfid->temp_vfid.fileid != NULL_FILEID;
  }
}
