/*
 * Copyright 2008 Search Solution Corporation
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
 * parallel.c - parallel module
 */

#include "px_parallel.hpp"

#include <mutex>

#include "log_impl.h"		/* LOG_FIND_CURRENT_TDES, log_tdes::is_under_sysop */
#include "system.h"		/* UINT32, UINT64 */
#include "system_parameter.h"	/* sysprm_get_range, PRM_ID_PARALLELISM */
#include "thread_manager.hpp"	/* cubthread::system_core_count */

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_query
{
  /*
   * is_under_system_operation () - is the calling transaction inside a system operation?
   *   return: true if a system operation is open on the current thread's transaction
   *
   * A system operation (log_sysop_start) holds the transaction's rmutex_topop, which is re-entrant only for the
   * owning thread. Every parallel worker runs on the leader's transaction index, so a worker that needs a system
   * operation of its own (e.g. creating a temporary file when the temp file cache is empty: file_create ->
   * disk_reserve_sectors -> log_sysop_start) blocks on that mutex forever while the leader waits for the worker.
   * MERGE opens its statement-level system operation before executing its sub-SELECTs (CBRD-27492); the same risk
   * exists for any other context that would spawn workers while a system operation is open, so the check gates
   * every parallel degree decision (compute_parallel_degree) rather than one executor site.
   *
   * topops.last is read without synchronization: the caller is either the leader itself (exact) or a worker of a
   * leader that keeps its system operation open until all workers finish (stable while the worker runs).
   */
  static bool is_under_system_operation ()
  {
    LOG_TDES *tdes = LOG_FIND_CURRENT_TDES ();

    return tdes != NULL && tdes->is_under_sysop ();
  }

  UINT32 compute_parallel_degree (parallel_type type, UINT64 num_pages, int hint_degree) noexcept
  {
    static std::once_flag once;
    static std::size_t system_core_count;
    static int parallelism;

    static int scan_page_threshold;
    static int hash_join_page_threshold;
    static int sort_page_threshold;

    // *INDENT-OFF*
    std::call_once(once, [] {
      parallelism = prm_get_integer_value (PRM_ID_PARALLELISM);
      system_core_count = cubthread::system_core_count ();
      assert (parallelism >= 0);
      assert ((std::size_t) parallelism <= system_core_count);

      scan_page_threshold = prm_get_integer_value (PRM_ID_PARALLEL_SCAN_PAGE_THRESHOLD);
      hash_join_page_threshold = prm_get_integer_value (PRM_ID_PARALLEL_HASH_JOIN_PAGE_THRESHOLD);
      sort_page_threshold = prm_get_integer_value (PRM_ID_PARALLEL_SORT_PAGE_THRESHOLD);
    });
    // *INDENT-ON*

    UINT32 page_threshold;
    UINT32 auto_degree;
    UINT32 degree;
    const UINT32 start_degree = 2;

    if (system_core_count <= start_degree)
      {
	return 0;	/* disable */
      }

    assert (hint_degree == -1 /* auto-compute */ || (hint_degree >= 0 && hint_degree <= PRM_MAX_PARALLELISM));

    if (is_under_system_operation ())
      {
	return 0;	/* disable: workers sharing this transaction would deadlock on its system operation (CBRD-27492) */
      }

    switch (type)
      {
      case parallel_type::SCAN:
	page_threshold = (UINT32) scan_page_threshold;
	break;

      case parallel_type::HASH_JOIN:
	page_threshold = (UINT32) hash_join_page_threshold;
	break;

      case parallel_type::SORT:
	page_threshold = (UINT32) sort_page_threshold;
	break;

      case parallel_type::SUBQUERY:
      {
	assert (num_pages == 0);

	/* TODO: degree fixed at 1 (main + gather = 2)
	 *       to be revised when exact parallel count is available
	 *       for many uncorrelated subqueries */
	auto_degree = 1;

	/* hint handling */
	if (hint_degree < 0 /* auto-compute */)
	  {
	    return MIN (auto_degree, (UINT32) parallelism);
	  }
	else if ((UINT32) hint_degree >= start_degree)
	  {
	    /* hint ignored, degree fixed for subquery, ignore the parallelism parameter */
	    return auto_degree;
	  }
	else
	  {
	    /* hint > 0 and < start_degree disables parallel execution */
	    return 0;
	  }
	}	/* case parallel_type::SUBQUERY */

      default:
	/* impossible case */
	assert_release_error (false);
	return 0;	/* disable */
      }

    page_threshold = MAX (page_threshold, start_degree);

    /* threshold check */
    if (num_pages < page_threshold)
      {
	return 0;	/* disable */
      }

    /* hint handling */
    if (hint_degree < 0 /* auto-compute */)
      {
	/* compute degree based on number of pages */
      }
    else if ((UINT32) hint_degree >= start_degree)
      {
	hint_degree = MIN (hint_degree, system_core_count);

	/* hint first, ignore the parallelism parameter */
	if (num_pages < (UINT64) hint_degree)
	  {
	    return num_pages;
	  }
	else
	  {
	    return hint_degree;
	  }
      }
    else
      {
	/* hint >= 0 and < start_degree disables parallel execution */
	return 0;
      }

    UINT64 x = num_pages / page_threshold;

    // *INDENT-OFF*
#if defined(__GNUC__) || defined(__clang__)
    auto_degree = (63 - __builtin_clzll (x)) + start_degree;
#else
    {
      int msb = 0;

      if (x >= (1ull << 32)) { x >>= 32; msb += 32; }
      if (x >= (1ull << 16)) { x >>= 16; msb += 16; }
      if (x >= (1ull <<  8)) { x >>=  8; msb +=  8; }
      if (x >= (1ull <<  4)) { x >>=  4; msb +=  4; }
      if (x >= (1ull <<  2)) { x >>=  2; msb +=  2; }
      if (x >= (1ull <<  1)) {           msb +=  1; }

      auto_degree = msb + start_degree;
    }
#endif
    // *INDENT-ON*

    degree = MIN (auto_degree, (UINT32) parallelism);

    /* SCAN/HASH_JOIN/SORT use 0 for serial execution and require at least 2 for real parallelism. */
    return (degree < start_degree) ? 0 : degree;
  }

}				/* namespace parallel_query */
