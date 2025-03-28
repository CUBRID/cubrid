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
	qfile_update_qlist_count (thread_get_thread_entry_info (), list_id, 1);
	qfile_destroy_list (m_thread_p, list_id);
	return;
      }
    assert (!list_id->sort_list);
    if (m_head_list_id == NULL)
      {
	m_head_list_id = list_id;
	return;
      }
    assert (m_head_list_id->type_list.type_cnt == list_id->type_list.type_cnt);
    /* head last page -> list_id first page (next) */
    PAGE_PTR head_last_pgptr = pgbuf_fix (m_thread_p, &m_head_list_id->last_vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
					  PGBUF_UNCONDITIONAL_LATCH);
    assert (head_last_pgptr != NULL);
    QFILE_PUT_NEXT_VPID (head_last_pgptr, &list_id->first_vpid);
    pgbuf_unfix (m_thread_p, head_last_pgptr);

    /* list_id first page -> head last page (prev) */
    PAGE_PTR list_id_first_pgptr = pgbuf_fix (m_thread_p, &list_id->first_vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
				   PGBUF_UNCONDITIONAL_LATCH);
    assert (list_id_first_pgptr != NULL);
    QFILE_PUT_PREV_VPID (list_id_first_pgptr, &m_head_list_id->last_vpid);
    pgbuf_unfix (m_thread_p, list_id_first_pgptr);

    /* append list_id to m_head_list_id */
    m_head_list_id->tuple_cnt += list_id->tuple_cnt;
    m_head_list_id->page_cnt += list_id->page_cnt;
    m_head_list_id->last_vpid = list_id->last_vpid;
    m_head_list_id->last_pgptr = list_id->last_pgptr;
    m_head_list_id->last_offset = list_id->last_offset;
    m_head_list_id->lasttpl_len = list_id->lasttpl_len;
    assert (m_head_list_id->query_id == list_id->query_id);
    /* clear list_id, but not free tfile,
     * it will be free in qmgr_free_query_temp_file_helper()*/
    qfile_update_qlist_count (thread_get_thread_entry_info (), list_id, 1);
    qfile_clear_list_id (list_id);
  }

  QFILE_LIST_ID *list_merger::get_merged_list_id ()
  {
    QFILE_LIST_ID *ret = m_head_list_id;
    assert (ret != NULL);
    m_head_list_id = NULL;
    return ret;
  }

  void list_merger::swap_and_destroy_list_id (THREAD_ENTRY *thread_p, QFILE_LIST_ID **orig_list, QFILE_LIST_ID **new_list)
  {
    qfile_destroy_list (thread_p, *orig_list);
    qfile_copy_list_id (*orig_list, *new_list, false);
  }
}
