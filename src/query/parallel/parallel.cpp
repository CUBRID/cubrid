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

#include "parallel.hpp"

#include "system.h"		/* UINT32, UINT64 */
#include "system_parameter.h"	/* sysprm_get_range, PRM_ID_PARALLELISM */
#include "thread_worker_pool.hpp"	/* cubthread::system_core_count */

namespace parallel_query
{
  UINT32 compute_parallel_degree (PARALLEL_TYPE type, UINT64 num_pages, int hint_degree) noexcept
  {
    static std::once_flag once;
    static int prm_upper_limit = 0;
    static int prm_value = 0;

    // *INDENT-OFF*
    std::call_once(once, [] {
      sysprm_get_range(PRM_ID_PARALLELISM, nullptr, &prm_upper_limit);
      prm_upper_limit = MIN (prm_upper_limit, cubthread::system_core_count ());
      assert (prm_upper_limit >= 0);

      prm_value = prm_get_integer_value (PRM_ID_PARALLELISM);
      assert (prm_value >= 0);
    });
    // *INDENT-ON*

    UINT32 page_threshold;

    switch (type)
      {
      case PARALLEL_HEAP_SCAN:
	page_threshold = prm_get_integer_value (PRM_ID_PARALLEL_HEAP_SCAN_PAGE_THRESHOLD);
	break;

      case PARALLEL_HASH_JOIN:
	page_threshold = prm_get_integer_value (PRM_ID_PARALLEL_HASH_JOIN_PAGE_THRESHOLD);
	break;

      case PARALLEL_SORT:
	page_threshold = prm_get_integer_value (PRM_ID_PARALLEL_SORT_PAGE_THRESHOLD);
	break;

      case PARALLEL_SUBQUERY:
	/* degree fixed to 1 (main + gather = 2) */
	assert (num_pages == 0);
	return 1;

      default:
	/* impossible case */
	assert_release_error (false);
	return 0;
      }

    /* threshold check */
    if (num_pages < page_threshold)
      {
	return 0;
      }

    /* hint handling */
    if (hint_degree < 0)
      {
	/* compute degree based on number of pages */
      }
    else if (hint_degree > 1)
      {
	return MIN (hint_degree, (UINT32) prm_upper_limit);
      }
    else
      {
	/* hint 0 or 1 disables parallel execution */
	return 0;
      }

    UINT64 x = num_pages / page_threshold;
    UINT32 degree;

    // *INDENT-OFF*
#if defined(__GNUC__) || defined(__clang__)
    degree = (63 - __builtin_clzll (x)) + min_parallel_degree;
#else
    {
      int msb = 0;

      if (x >= (1ull << 32)) { x >>= 32; msb += 32; }
      if (x >= (1ull << 16)) { x >>= 16; msb += 16; }
      if (x >= (1ull <<  8)) { x >>=  8; msb +=  8; }
      if (x >= (1ull <<  4)) { x >>=  4; msb +=  4; }
      if (x >= (1ull <<  2)) { x >>=  2; msb +=  2; }
      if (x >= (1ull <<  1)) {           msb +=  1; }

      degree = msb + min_parallel_degree;
    }
#endif
    // *INDENT-ON*

    return MIN (degree, (UINT32) prm_value);
  }
}				/* namespace parallel_query */
