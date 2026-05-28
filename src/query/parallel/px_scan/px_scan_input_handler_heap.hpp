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
 * px_scan_input_handler_heap.hpp
 */

#ifndef _PX_SCAN_INPUT_HANDLER_HEAP_HPP_
#define _PX_SCAN_INPUT_HANDLER_HEAP_HPP_

#include "px_ftab_set.hpp"
#include "px_interrupt.hpp"
#include "scan_manager.h"

namespace parallel_scan
{
  using ftab_set = parallel_query::ftab_set;
  class input_handler_heap
  {
      using interrupt = parallel_query::interrupt;
      using err_messages_with_lock = parallel_query::err_messages_with_lock;
    public:
      input_handler_heap (interrupt *interrupt_p, err_messages_with_lock *err_messages_p)
	: m_splited_ftab_set_idx (0),
	  m_interrupt_p (interrupt_p),
	  m_err_messages_p (err_messages_p)
      {
      }
      int init_on_main (THREAD_ENTRY *thread_p, HFID m_hfid, int parallelism);
      SCAN_CODE get_next_vpid_with_fix (THREAD_ENTRY *thread_p, VPID *vpid);
      int initialize (THREAD_ENTRY *thread_p, HFID *hfid, SCAN_ID *scan_id);
      int finalize (THREAD_ENTRY *thread_p);

    private:
      thread_local static VPID m_tl_vpid;
      thread_local static HEAP_SCANCACHE *m_tl_scan_cache;
      thread_local static PGBUF_WATCHER m_tl_old_page_watcher;
      thread_local static ftab_set *m_tl_ftab_set;
      thread_local static size_t m_tl_pgoffset;
      thread_local static FILE_PARTIAL_SECTOR m_tl_ftab;
      ftab_set m_ftab_set;
      std::vector<ftab_set> m_splited_ftab_set;
      std::atomic_int m_splited_ftab_set_idx;
      HFID m_hfid;
      interrupt *m_interrupt_p;
      err_messages_with_lock *m_err_messages_p;
  };
}

#endif /*_PX_SCAN_INPUT_HANDLER_HEAP_HPP_ */
