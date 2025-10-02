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
 * px_heap_scan_result_handler_mergeable_list.hpp
 */

#ifndef _PX_HEAP_SCAN_RESULT_HANDLER_mergeable_list_HPP_
#define _PX_HEAP_SCAN_RESULT_HANDLER_mergeable_list_HPP_

#include "px_heap_scan_result_handler.hpp"
#include <vector>
#include <mutex>

namespace parallel_heap_scan
{
  class result_handler_mergeable_list : public result_handler<QFILE_LIST_ID, OUTPTR_LIST, OUTPTR_LIST *, VAL_DESCR *>
  {
      using interrupt = parallel_query::interrupt;
      using atomic_instnum = parallel_query::atomic_instnum;
      using err_messages_with_lock = parallel_query::err_messages_with_lock;
    public:

      ~result_handler_mergeable_list() = default;

      void read_initialize (THREAD_ENTRY *thread_p, OUTPTR_LIST *outptr_list, VAL_DESCR *vd) override;
      SCAN_CODE get_next (THREAD_ENTRY *thread_p, QFILE_LIST_ID *result) override;
      void read_finalize (THREAD_ENTRY *thread_p) override;

      void write_initialize (THREAD_ENTRY *thread_p, OUTPTR_LIST *outptr_list, VAL_DESCR *vd) override;
      bool write (THREAD_ENTRY *thread_p, OUTPTR_LIST *input) override;
      void write_finalize (THREAD_ENTRY *thread_p) override;

      inline void set_tl_val_list_for_agg_domain_resolve (VAL_LIST *val_list)
      {
	if (m_g_agg_domain_resolve_need)
	  {
	    m_tl_val_list_for_agg_domain_resolve = val_list;
	  }
      }

      result_handler_mergeable_list (QUERY_ID query_id, interrupt *interrupt_p, atomic_instnum *atomic_instnum_p,
				     bool should_check_instnum, err_messages_with_lock *err_messages_p, int parallelism, bool g_agg_domain_resolve_need,
				     VAL_LIST *orig_val_list_for_agg_domain_resolve)
	: result_handler (query_id, interrupt_p, atomic_instnum_p, should_check_instnum, err_messages_p,
			  RESULT_TYPE::MERGEABLE_LIST),
	  m_writer_results (),
	  m_writer_results_mutex (),
	  m_is_list_id_domain_resolved (false),
	  m_result_p (nullptr),
	  m_result_mutex(),
	  m_result_condition_variable(),
	  m_active_results (parallelism),
	  m_parallelism (parallelism),
	  m_g_agg_domain_resolve_need (g_agg_domain_resolve_need),
	  m_orig_val_list_for_agg_domain_resolve (orig_val_list_for_agg_domain_resolve) {}

    private:


      std::vector<QFILE_LIST_ID *> m_writer_results;
      std::mutex m_writer_results_mutex;
      bool m_is_list_id_domain_resolved;
      thread_local static QFILE_LIST_ID *m_tl_writer_result_p;
      thread_local static QFILE_TUPLE_RECORD *m_tl_tpl_buf;
      thread_local static VAL_DESCR *m_tl_vd; /* for valdesc */

      /* for both*/
      QFILE_LIST_ID *m_result_p; /* for result */
      std::mutex m_result_mutex;
      std::condition_variable m_result_condition_variable;
      int m_active_results;
      int m_parallelism;

      bool m_g_agg_domain_resolve_need;
      VAL_LIST *m_orig_val_list_for_agg_domain_resolve;
      thread_local static VAL_LIST *m_tl_val_list_for_agg_domain_resolve;
      thread_local static std::vector<DB_VALUE> m_tl_dbvals_for_agg_domain_resolve;
  };
}
#endif /*_PX_HEAP_SCAN_RESULT_HANDLER_mergeable_list_HPP_ */
