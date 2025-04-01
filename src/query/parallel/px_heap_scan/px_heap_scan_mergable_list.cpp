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
#if SERVER_MODE && !WINDOWS

#include "px_heap_scan_mergable_list.hpp"
#include "query_opfunc.h"
#include "regu_var.hpp"
#include "object_representation.h"
#include "query_manager.h"
#include "thread_manager.hpp"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

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
	if (list_id->type_list.type_cnt != 0)
	  {
	    qfile_update_qlist_count (thread_get_thread_entry_info (), list_id, 1);
	    qfile_clear_list_id (list_id);
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
    m_dbv_arr.resize (outptr_list->valptr_cnt);
  }

  mergable_list_writer::~mergable_list_writer()
  {
    free (m_tpl_buf.tpl);
  }

  bool mergable_list_writer::open (THREAD_ENTRY *thread_p, PARALLEL_HEAP_SCAN_ID *phsid,
				   REGU_VARIABLE_LIST regu_list_pred, REGU_VARIABLE_LIST regu_list_rest)
  {
    QFILE_TUPLE_VALUE_TYPE_LIST type_list;
    REGU_VARIABLE_LIST valptr, orig_pred_regu, new_pred_regu, orig_rest_regu, new_rest_regu;
    int valptr_idx;
    qdata_get_valptr_type_list (thread_p, m_outptr_list, &type_list);

    (*m_list_id_p) = qfile_open_list (thread_p, &type_list, NULL, m_query_id, QFILE_FLAG_ALL, NULL);
    (*m_list_id_p)->tfile_vfid->membuf_last = prm_get_integer_value (PRM_ID_TEMP_MEM_BUFFER_PAGES) - 1;
    (*m_list_id_p)->tfile_vfid->membuf = NULL;
    (*m_list_id_p)->tfile_vfid->membuf_npages = 0;
    (*m_list_id_p)->tfile_vfid->membuf_type = TEMP_FILE_MEMBUF_NONE;
    (*m_list_id_p)->tfile_vfid->preserved = false;
    (*m_list_id_p)->tfile_vfid->tde_encrypted = false;

    for (valptr_idx = 0, valptr = m_outptr_list->valptrp; valptr != NULL;
	 valptr_idx++, valptr = valptr->next)
      {
	bool found = false;
	REGU_VARIABLE *valptr_var = &valptr->value;
	assert (valptr_var->type == TYPE_CONSTANT);

	for (orig_pred_regu = phsid->scan_pred.regu_list, new_pred_regu = regu_list_pred;
	     orig_pred_regu != NULL
	     && new_pred_regu != NULL; orig_pred_regu = orig_pred_regu->next, new_pred_regu = new_pred_regu->next)
	  {
	    REGU_VARIABLE *orig_pred_regu_var = &orig_pred_regu->value;
	    if (orig_pred_regu_var->vfetch_to == valptr_var->value.dbvalptr)
	      {
		REGU_VARIABLE *new_pred_regu_var = &new_pred_regu->value;
		m_dbv_arr[valptr_idx] = new_pred_regu_var->vfetch_to;
		found = true;
		break;
	      }
	  }

	if (!found)
	  {
	    for (orig_rest_regu = phsid->rest_regu_list, new_rest_regu = regu_list_rest;
		 orig_rest_regu != NULL
		 && new_rest_regu != NULL; orig_rest_regu = orig_rest_regu->next, new_rest_regu = new_rest_regu->next)
	      {
		REGU_VARIABLE *orig_rest_regu_var = &orig_rest_regu->value;
		if (orig_rest_regu_var->vfetch_to == valptr_var->value.dbvalptr)
		  {
		    REGU_VARIABLE *new_rest_regu_var = &new_rest_regu->value;
		    m_dbv_arr[valptr_idx] = new_rest_regu_var->vfetch_to;
		    found = true;
		    break;
		  }
	      }

	    assert (found); /* NOT FOUND : assert() */
	  }


      }


    return true;
  }

  void mergable_list_writer::close (THREAD_ENTRY *thread_p)
  {
    qfile_close_list (thread_p, *m_list_id_p);
  }

  void mergable_list_writer::write (THREAD_ENTRY *thread_p)
  {
    QFILE_TUPLE_RECORD *tplrec = make_tuple_record (thread_p);
    int err = qfile_add_tuple_to_list (thread_p, *m_list_id_p, tplrec->tpl);
    assert (err == NO_ERROR);
  }

  QFILE_TUPLE_RECORD *mergable_list_writer::make_tuple_record (THREAD_ENTRY *thread_p)
  {
    REGU_VARIABLE_LIST p;
    int n_preds, n_rests, n_all;
    char *tuple_p;
    int i = 0, tval_size = 0, tlen, tpl_size;
    int n_size, toffset;
    bool clear_compressed_string = true;

    tpl_size = 0;
    tlen = QFILE_TUPLE_LENGTH_SIZE;
    toffset = 0;

    tuple_p = (char *) (m_tpl_buf.tpl) + tlen;
    toffset += tlen;

    for (DB_VALUE *dbval_p : m_dbv_arr)
      {
	n_size = qdata_get_tuple_value_size_from_dbval (dbval_p);
	assert (n_size != ER_FAILED);
	if (tlen + n_size > m_tpl_buf.size)
	  {
	    tpl_size = MAX (tlen, QFILE_TUPLE_LENGTH_SIZE);
	    tpl_size += MAX (n_size, DB_PAGESIZE);
	    tpl_size = ((tpl_size + DB_PAGESIZE - 1) / DB_PAGESIZE) * DB_PAGESIZE;
	    m_tpl_buf.size += tpl_size;
	    m_tpl_buf.tpl = (char *) realloc ((void *) m_tpl_buf.tpl, m_tpl_buf.size);
	    assert_release (m_tpl_buf.tpl != NULL);
	    tuple_p = (char *) (m_tpl_buf.tpl) + toffset;
	  }

	if (qdata_copy_db_value_to_tuple_value (dbval_p, clear_compressed_string, tuple_p, &tval_size) != NO_ERROR)
	  {
	    assert (false);
	  }

	tlen += tval_size;
	tuple_p += tval_size;
	toffset += tval_size;
      }

    QFILE_PUT_TUPLE_LENGTH (m_tpl_buf.tpl, tlen);

    return &m_tpl_buf;
  }



}

#endif /* SERVER_MODE && !WINDOWS */
