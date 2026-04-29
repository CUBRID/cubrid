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
 * px_scan_input_handler_list.hpp
 */

#ifndef _PX_SCAN_INPUT_HANDLER_LIST_HPP_
#define _PX_SCAN_INPUT_HANDLER_LIST_HPP_

#include "px_interrupt.hpp"
#include "query_list.h"
#include "query_manager.h"
#include "scan_manager.h"
#include <atomic>
#include <vector>

namespace parallel_scan
{
  class input_handler_list
  {
      using interrupt = parallel_query::interrupt;
      using err_messages_with_lock = parallel_query::err_messages_with_lock;

      /* [start, end) into m_sector_info; iter advances as sectors are consumed. */
      struct worker_slice
      {
	int start;
	int end;
	int iter;
      };

    public:
      input_handler_list (interrupt *interrupt_p, err_messages_with_lock *err_messages_p)
	: m_sector_info (QFILE_LIST_SECTOR_INFO_INITIALIZER),
	  m_worker_slice_idx (0),
	  m_membuf_claimed (false),
	  m_list_id (nullptr),
	  m_interrupt_p (interrupt_p),
	  m_err_messages_p (err_messages_p)
      {
      }

      int init_on_main (THREAD_ENTRY *thread_p, QFILE_LIST_ID *list_id, int parallelism);

      /* Single READ-latch fix; ownership of out_page transfers to caller on S_SUCCESS. */
      SCAN_CODE get_next_page_with_fix (THREAD_ENTRY *thread_p,
					PAGE_PTR &out_page,
					QMGR_TEMP_FILE *&out_tfile);

      int initialize (THREAD_ENTRY *thread_p, HFID *hfid, SCAN_ID *scan_id);
      int finalize (THREAD_ENTRY *thread_p);

      /* Manager must call before destruction (implicit dtor lacks THREAD_ENTRY). Idempotent. */
      void cleanup_on_main (THREAD_ENTRY *thread_p);

      QFILE_LIST_ID *get_list_id ()
      {
	return m_list_id;
      }

    private:
      QFILE_LIST_SECTOR_INFO m_sector_info;
      std::vector<worker_slice> m_worker_slices;
      std::atomic_int m_worker_slice_idx;
      /* CAS membuf ownership — idx==0 failure cannot strand the membuf */
      std::atomic<bool> m_membuf_claimed;

      QFILE_LIST_ID *m_list_id;
      interrupt *m_interrupt_p;
      err_messages_with_lock *m_err_messages_p;

      thread_local static worker_slice *m_tl_slice;
      thread_local static UINT64 m_tl_bitmap;
      thread_local static VSID m_tl_vsid;
      thread_local static QMGR_TEMP_FILE *m_tl_current_tfile;
      thread_local static bool m_tl_is_membuf_worker;
      thread_local static int m_tl_membuf_pageid;
  };
}

#endif /* _PX_SCAN_INPUT_HANDLER_LIST_HPP_ */
