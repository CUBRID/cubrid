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
 * px_heap_scan_input_handler.hpp
 */

#ifndef _PX_HEAP_SCAN_INPUT_HANDLER_HPP_
#define _PX_HEAP_SCAN_INPUT_HANDLER_HPP_

#include "dbtype_def.h"
#include "thread_entry.hpp"
#include "storage_common.h"
#include "px_interrupt.hpp"
#include "scan_manager.h"

namespace parallel_heap_scan
{
  enum class INPUT_TYPE
  {
    NONE,
    SINGLE_TABLE
    /* partitioned table specification need? */
  };

  class input_handler
  {
      using interrupt = parallel_query::interrupt;
      using err_messages_with_lock = parallel_query::err_messages_with_lock;
    public:
      virtual ~input_handler() = default;

      virtual SCAN_CODE get_next_vpid_with_fix (THREAD_ENTRY *thread_p, VPID *vpid) = 0;
      virtual int initialize (THREAD_ENTRY *thread_p, HFID *hfid, SCAN_ID *scan_id) = 0;
      virtual int finalize (THREAD_ENTRY *thread_p) = 0;

      input_handler (interrupt *interrupt_p,
		     err_messages_with_lock *err_messages_p, INPUT_TYPE input_type)
	: m_input_type (input_type),
	  m_interrupt_p (interrupt_p),
	  m_err_messages_p (err_messages_p)
      {}
      INPUT_TYPE m_input_type;
      interrupt *m_interrupt_p;
      err_messages_with_lock *m_err_messages_p;
  };
}

#endif /*_PX_HEAP_SCAN_INPUT_HANDLER_HPP_ */
