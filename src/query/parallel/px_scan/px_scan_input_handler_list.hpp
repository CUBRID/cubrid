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

#include "px_scan_ftab_set.hpp"
#include "px_interrupt.hpp"
#include "query_manager.h"
#include "scan_manager.h"
#include <vector>
#include <atomic>

namespace parallel_scan
{
  class input_handler_list
  {
      using interrupt = parallel_query::interrupt;
      using err_messages_with_lock = parallel_query::err_messages_with_lock;
    public:
      input_handler_list (interrupt *interrupt_p, err_messages_with_lock *err_messages_p)
	: m_splited_ftab_set_idx (0),
	  m_has_membuf (false),
	  m_membuf_last (-1),
	  m_tfile_vfid (nullptr),
	  m_list_id (nullptr),
	  m_interrupt_p (interrupt_p),
	  m_err_messages_p (err_messages_p)
      {
      }
      int init_on_main (THREAD_ENTRY *thread_p, QFILE_LIST_ID *list_id, int parallelism);
      SCAN_CODE get_next_vpid_with_fix (THREAD_ENTRY *thread_p, VPID *vpid);
      int initialize (THREAD_ENTRY *thread_p, HFID *hfid, SCAN_ID *scan_id);
      int finalize (THREAD_ENTRY *thread_p);
      QFILE_LIST_ID *get_list_id ()
      {
	return m_list_id;
      }

    private:
      /* Sector-based (ftabs pattern) */
      ftab_set m_ftab_set;
      std::vector<ftab_set> m_splited_ftab_set;
      std::atomic_int m_splited_ftab_set_idx;

      /* membuf info */
      bool m_has_membuf;
      int m_membuf_last;
      QMGR_TEMP_FILE *m_tfile_vfid;

      QFILE_LIST_ID *m_list_id;
      interrupt *m_interrupt_p;
      err_messages_with_lock *m_err_messages_p;

      /* Thread-local state */
      thread_local static ftab_set *m_tl_ftab_set;
      thread_local static VPID m_tl_vpid;
      thread_local static size_t m_tl_pgoffset;
      thread_local static FILE_PARTIAL_SECTOR m_tl_ftab;
      thread_local static bool m_tl_is_membuf_worker;
      thread_local static int m_tl_membuf_pageid;
  };
}

#endif /*_PX_SCAN_INPUT_HANDLER_LIST_HPP_ */
