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
 * px_list_merger.cpp - parallel list merger
 */

#include "px_list_merger.hpp"
#include "page_buffer.h"
#include "object_representation.h"
#include "thread_manager.hpp"
#include "query_manager.h"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_query
{
  list_merger::list_merger (THREAD_ENTRY *thread_p)
  {
    m_thread_p = thread_p;
    m_head_list_id = NULL;
  }

  list_merger::~list_merger ()
  {
    assert (m_head_list_id == NULL);
  }

  void list_merger::add_list_id (QFILE_LIST_ID *list_id)
  {
    if (list_id == NULL)
      {
	return;
      }
    if (list_id->tuple_cnt <= 0)
      {
	if (list_id->type_list.type_cnt != 0)
	  {
	    qfile_destroy_list (m_thread_p, list_id);
	  }
	return;
      }
    assert (!list_id->sort_list);
    if (m_head_list_id == NULL)
      {
	m_head_list_id = list_id;
	return;
      }
    assert (m_head_list_id->type_list.type_cnt == list_id->type_list.type_cnt);
    if (!m_head_list_id->is_domain_resolved)
      {
	if (list_id->is_domain_resolved)
	  {
	    QFILE_TUPLE_VALUE_TYPE_LIST tmp_type_list = m_head_list_id->type_list;
	    m_head_list_id->type_list = list_id->type_list;
	    list_id->type_list = tmp_type_list;
	  }
      }
    if (m_head_list_id->last_pgptr != NULL)
      {
	qfile_close_list (m_thread_p, m_head_list_id);
      }
    if (list_id->last_pgptr != NULL)
      {
	qfile_close_list (m_thread_p, list_id);
      }
    /* head last page -> list_id first page (next) */
    PAGE_PTR head_last_pgptr = qmgr_get_old_page (m_thread_p, &m_head_list_id->last_vpid, m_head_list_id->tfile_vfid);
    assert (head_last_pgptr != NULL);
    QFILE_PUT_NEXT_VPID (head_last_pgptr, &list_id->first_vpid);
    pgbuf_set_dirty (m_thread_p, head_last_pgptr, FREE);

    /* list_id first page -> head last page (prev) */
    PAGE_PTR list_id_first_pgptr = qmgr_get_old_page (m_thread_p, &list_id->first_vpid, list_id->tfile_vfid);
    assert (list_id_first_pgptr != NULL);
    /* The list_id to be appended must not use membuf. */
    assert (list_id->first_vpid.volid != NULL_VOLID);
    QFILE_PUT_PREV_VPID (list_id_first_pgptr, &m_head_list_id->last_vpid);
    pgbuf_set_dirty (m_thread_p, list_id_first_pgptr, FREE);
    /* append list_id to m_head_list_id */
    m_head_list_id->tuple_cnt += list_id->tuple_cnt;
    m_head_list_id->page_cnt += list_id->page_cnt;
    m_head_list_id->last_vpid = list_id->last_vpid;
    m_head_list_id->last_offset = list_id->last_offset;
    m_head_list_id->lasttpl_len = list_id->lasttpl_len;
    assert (m_head_list_id->query_id == list_id->query_id);
    /* clear list_id, but not free tfile,
     * it will be free in qmgr_free_query_temp_file_helper()*/
    qfile_clear_list_id (list_id);
  }

  QFILE_LIST_ID *list_merger::get_merged_list_id ()
  {
    QFILE_LIST_ID *ret = m_head_list_id;
    m_head_list_id = NULL;
    return ret;
  }

  void list_merger::swap_and_destroy_list_id (THREAD_ENTRY *thread_p, QFILE_LIST_ID **orig_list, QFILE_LIST_ID **new_list)
  {
    QFILE_LIST_ID *ret;
    if ((*orig_list) != NULL && (*orig_list)->tuple_cnt != 0)
      {
	list_merger merger (thread_p);
	merger.add_list_id (*orig_list);
	merger.add_list_id (*new_list);
	merger.clear();
      }
    else
      {
	qfile_destroy_list (thread_p, *orig_list);
	qfile_copy_list_id (*orig_list, *new_list, false);
      }
  }
}
