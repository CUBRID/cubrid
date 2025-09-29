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
 * px_heap_scan_input_handler_single_table.hpp
 */

#ifndef _PX_HEAP_SCAN_INPUT_HANDLER_SINGLE_TABLE_HPP_
#define _PX_HEAP_SCAN_INPUT_HANDLER_SINGLE_TABLE_HPP_

#include "px_heap_scan_input_handler.hpp"

namespace parallel_heap_scan
{
  class input_handler_single_table : public input_handler
  {
      using interrupt = parallel_query::interrupt;
      using err_messages_with_lock = parallel_query::err_messages_with_lock;
    public:
      input_handler_single_table (interrupt *interrupt_p, err_messages_with_lock *err_messages_p)
	: input_handler (interrupt_p, err_messages_p, INPUT_TYPE::SINGLE_TABLE)
      {
	m_vpid = {NULL_PAGEID, NULL_VOLID};
	m_vpid_ended = false;
	m_hfid = {{-1, -1}, -1};
      }

      SCAN_CODE get_next_vpid_with_fix (THREAD_ENTRY *thread_p, VPID *vpid) override;
      int initialize (THREAD_ENTRY *thread_p, HFID *hfid, SCAN_ID *scan_id) override;
      int finalize (THREAD_ENTRY *thread_p) override;

    private:
      VPID m_vpid;
      bool m_vpid_ended;
      std::mutex m_vpid_mutex;
      thread_local static HEAP_SCANCACHE *m_tl_scan_cache;
      thread_local static PGBUF_WATCHER m_tl_old_page_watcher;
      HFID m_hfid;
  };
}

#endif /*_PX_HEAP_SCAN_INPUT_HANDLER_SINGLE_TABLE_HPP_ */
