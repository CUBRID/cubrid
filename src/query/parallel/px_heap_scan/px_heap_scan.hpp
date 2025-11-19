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
 * px_heap_scan.hpp
 */

#ifndef _PX_HEAP_SCAN_MANAGER_HPP_
#define _PX_HEAP_SCAN_MANAGER_HPP_

#include "xasl.h"
#include "px_worker_manager.hpp"
#include "px_heap_scan_result_handler.hpp"
#include "px_heap_scan_input_handler.hpp"
#include "px_heap_scan_trace_handler.hpp"
#include "px_heap_scan_result_type.hpp"
#include "query_manager.h"
#include "thread_worker_pool.hpp"	/* cubthread::system_core_count */

#define PARALLEL_HEAP_SCAN_MIN_USER_PAGES ((int)32)

namespace parallel_heap_scan
{
  template <RESULT_TYPE result_type>
  class manager
  {
      using interrupt = parallel_query::interrupt;
      using err_messages_with_lock = parallel_query::err_messages_with_lock;
      using input_handler = parallel_heap_scan::input_handler;
      using atomic_instnum = parallel_query::atomic_instnum;
      using worker_manager = parallel_query::worker_manager;
    public:
      manager (THREAD_ENTRY *thread_p, QUERY_ID query_id, SCAN_ID *scan_id, xasl_node *xasl, int parallelism, HFID hfid,
	       OID cls_oid,
	       val_descr *vd,
	       bool is_fixed, bool is_grouped,
	       worker_manager *worker_manager)
	: m_thread_p (thread_p),
	  m_query_id (query_id),
	  m_scan_id (scan_id),
	  m_query_entry (nullptr),
	  m_xasl (xasl),
	  m_parallelism (parallelism),
	  m_hfid (hfid),
	  m_cls_oid (cls_oid),
	  m_vd (vd),
	  m_input_handler (nullptr),
	  m_result_handler (nullptr),
	  m_on_trace (false),
	  m_px_stats_initialized_by_me (false),
	  m_result_handler_read_initialized (false),
	  m_task_started (false),
	  m_trace_handler (),
	  m_interrupt (),
	  m_atomic_instnum (),
	  m_err_messages (),
	  m_worker_manager (worker_manager),
	  m_is_fixed (is_fixed),
	  m_is_grouped (is_grouped),
	  m_uses_xasl_clone (false),
	  m_g_agg_domain_resolve_need (false)
      {}
      ~manager();
      int open();
      int start_tasks();
      SCAN_CODE next();
      int reset();
      int end();
      int close();
      trace_handler &get_trace_handler()
      {
	return m_trace_handler;
      }
      RESULT_TYPE get_result_type()
      {
	return result_type;
      }

    private:
      THREAD_ENTRY *m_thread_p;
      QUERY_ID m_query_id;
      SCAN_ID *m_scan_id;
      QMGR_QUERY_ENTRY *m_query_entry;
      xasl_node *m_xasl;
      int m_parallelism;
      HFID m_hfid;
      OID m_cls_oid;
      val_descr *m_vd;
      input_handler *m_input_handler;
      result_handler<result_type> *m_result_handler;
      bool m_on_trace;
      bool m_px_stats_initialized_by_me;
      bool m_result_handler_read_initialized;
      bool m_task_started;
      trace_handler m_trace_handler;
      interrupt m_interrupt;
      atomic_instnum m_atomic_instnum;
      err_messages_with_lock m_err_messages;
      worker_manager *m_worker_manager;
      bool m_is_fixed;
      bool m_is_grouped;
      bool m_uses_xasl_clone;
      bool m_g_agg_domain_resolve_need;
  };

  static constexpr UINT64 px_heap_scan_parallel_threshold_pages = 100; // TODO: by youngjinj - 4096
  static constexpr int px_heap_scan_min_parallel_degree = 2;

  inline int
  px_heap_scan_compute_parallel_degree (UINT64 num_pages, int hint_degree = -1) noexcept
  {
    static std::once_flag once;
    static int upper_limit = 0;

    // *INDENT-OFF*
    std::call_once(once, [] {
      sysprm_get_range(PRM_ID_PARALLELISM, nullptr, &upper_limit);
      upper_limit = MIN (upper_limit, cubthread::system_core_count ());
    });
    // *INDENT-ON*

    /* threshold check */
    if (num_pages < px_heap_scan_parallel_threshold_pages)
      {
	return 0;
      }

    /* hint handling */
    if (hint_degree < 0)
      {
	/* fall through */
      }
    else if (hint_degree > 1)
      {
	return MIN (hint_degree, upper_limit);
      }
    else
      {
	/* hint 0 or 1 disables parallel execution */
	return 0;
      }

    UINT64 x = num_pages / px_heap_scan_parallel_threshold_pages;
    int degree;

    // *INDENT-OFF*
#if defined(__GNUC__) || defined(__clang__)
    degree = (63 - __builtin_clzll (x)) + px_heap_scan_min_parallel_degree;
#else
    {
      int msb = 0;

      if (x >= (1ull << 32)) { x >>= 32; msb += 32; }
      if (x >= (1ull << 16)) { x >>= 16; msb += 16; }
      if (x >= (1ull <<  8)) { x >>=  8; msb +=  8; }
      if (x >= (1ull <<  4)) { x >>=  4; msb +=  4; }
      if (x >= (1ull <<  2)) { x >>=  2; msb +=  2; }
      if (x >= (1ull <<  1)) {           msb +=  1; }

      degree = msb + px_heap_scan_min_parallel_degree;
    }
#endif
    // *INDENT-ON*

    return MIN (degree, upper_limit);
  }
}

extern "C"
{
  extern int scan_open_parallel_heap_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id, ACCESS_SPEC_TYPE *curr_spec,
      int fixed_scan, int grouped_scan, bool mvcc_select_lock_needed, XASL_NODE *xasl, QUERY_ID query_id, VAL_DESCR *vd);
  extern void scan_close_parallel_heap_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id);
  extern int scan_start_parallel_heap_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id);
  extern void scan_end_parallel_heap_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id);
  extern SCAN_CODE scan_next_parallel_heap_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id);
  extern int scan_reset_scan_block_parallel_heap_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id);
}

#endif /*_PX_HEAP_SCAN_MANAGER_HPP_ */
