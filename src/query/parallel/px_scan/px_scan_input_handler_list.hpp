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
	: m_split_vpids_idx (0),
	  m_interrupt_p (interrupt_p),
	  m_err_messages_p (err_messages_p),
	  m_list_id (nullptr)
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
      thread_local static std::vector<VPID> *m_tl_vpid_list;
      thread_local static int m_tl_vpid_idx;

      std::vector<std::vector<VPID>> m_split_vpids;
      std::atomic_int m_split_vpids_idx;
      QFILE_LIST_ID *m_list_id;
      interrupt *m_interrupt_p;
      err_messages_with_lock *m_err_messages_p;
  };
}

#endif /*_PX_SCAN_INPUT_HANDLER_LIST_HPP_ */
