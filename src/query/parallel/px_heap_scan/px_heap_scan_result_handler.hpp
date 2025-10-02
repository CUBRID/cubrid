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
 * px_heap_scan_result_handler.hpp
 */

#ifndef _PX_HEAP_SCAN_RESULT_HANDLER_HPP_
#define _PX_HEAP_SCAN_RESULT_HANDLER_HPP_

#include "storage_common.h"
#include "thread_entry.hpp"
#include "px_interrupt.hpp"
#include "xasl.h"

namespace parallel_heap_scan
{
  enum class RESULT_TYPE
  {
    NONE,
    MERGEABLE_LIST, /* (fast) list-per-thread return, and merge (set dependent) it. */
    XASL_SNAPSHOT, /* (slow) xasl snapshot return (row-by-row) */
    COUNT, /* count(col) or count(*) */
  };

  template<typename ReadType, typename WriteType, typename... Args>
  class result_handler
  {
      using interrupt = parallel_query::interrupt;
      using atomic_instnum = parallel_query::atomic_instnum;
      using err_messages_with_lock = parallel_query::err_messages_with_lock;
    public:
      result_handler (QUERY_ID query_id, interrupt *interrupt_p, atomic_instnum *atomic_instnum_p,
		      bool should_check_instnum, err_messages_with_lock *err_messages_p, RESULT_TYPE result_type)
	: m_result_type (result_type),
	  m_reader_thread_p (nullptr),
	  m_writer_thread_p (nullptr),
	  m_query_id (query_id),
	  m_interrupt_p (interrupt_p),
	  m_atomic_instnum_p (atomic_instnum_p),
	  m_should_check_instnum (should_check_instnum),
	  m_err_messages_p (err_messages_p) {}

      virtual ~result_handler() = default;

      /* reader interface */
      virtual void read_initialize (THREAD_ENTRY *thread_p, Args... args) = 0; /* returns should continue */
      virtual SCAN_CODE get_next (THREAD_ENTRY *thread_p, ReadType *result) = 0;
      virtual void read_finalize (THREAD_ENTRY *thread_p) = 0;

      /* writer interface */
      virtual void write_initialize (THREAD_ENTRY *thread_p, Args... args) = 0;
      virtual bool write (THREAD_ENTRY *thread_p, WriteType *input) = 0; /* returns should continue */
      virtual void write_finalize (THREAD_ENTRY *thread_p) = 0;

      /* for both */
      RESULT_TYPE m_result_type;

      /* for reader */
      THREAD_ENTRY *m_reader_thread_p;

      /* for writer */
      THREAD_ENTRY *m_writer_thread_p;
      QUERY_ID m_query_id;
      interrupt *m_interrupt_p; /* for interrupt */
      atomic_instnum *m_atomic_instnum_p; /* for instnum */
      bool m_should_check_instnum; /* is responsible for checking instnum? */
      err_messages_with_lock *m_err_messages_p; /* for error messages */
  };
}

#endif /*_PX_HEAP_SCAN_RESULT_HANDLER_HPP_ */
