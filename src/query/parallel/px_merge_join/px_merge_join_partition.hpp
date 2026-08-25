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
 * px_merge_join_partition.hpp - range partitioning of the two sorted inputs of a merge join (CBRD-27307)
 */

#ifndef _PX_MERGE_JOIN_PARTITION_HPP_
#define _PX_MERGE_JOIN_PARTITION_HPP_

#include "dbtype_def.h"
#include "query_list.h"
#include "thread_compat.hpp"

#include <vector>

namespace parallel_query
{
  namespace merge_join
  {
    /* one range boundary: join-key column values copied from a real input tuple */
    class partition_key
    {
      public:
	partition_key () = default;
	partition_key (const partition_key &) = delete;
	partition_key &operator= (const partition_key &) = delete;
	partition_key (partition_key &&other) noexcept;
	partition_key &operator= (partition_key &&other) noexcept;
	~partition_key ();

	void clear ();

	std::vector<DB_VALUE> m_vals;
    };

    /* where a partition begins on one input list. jump-scan to m_pos re-reads that first tuple. */
    struct partition_start
    {
      QFILE_TUPLE_POSITION m_pos;
      bool m_exhausted;		/* no tuple with key > boundary: this and all later ranges are empty on this side */
    };

    /* B boundary keys split each list into B + 1 aligned ranges:
     * (-inf incl. NULL keys, k0], (k0, k1], ..., (k_B-1, +inf).
     * Boundaries are key VALUES, so a duplicate-key group never straddles two ranges
     * and outer/inner ranges align by construction. Range 0 starts at the list head;
     * range i + 1 starts at starts[i]. */
    struct merge_partitions
    {
      std::vector<partition_key> m_boundaries;
      std::vector<partition_start> m_outer_starts;
      std::vector<partition_start> m_inner_starts;
    };

    /* phase-1 gate: inner join, no single-fetch, both inputs large enough */
    bool is_applicable (const QFILE_LIST_MERGE_INFO &merge_info, const QFILE_LIST_ID *outer_list_id,
			const QFILE_LIST_ID *inner_list_id);

    /* Computes the range split of both sorted inputs. can_partition comes back false (with NO_ERROR)
     * when no useful split exists: degree <= 1, boundaries collapse under heavy duplication, or an
     * incomparable key pair (DB_UNK) was met — callers fall back to the serial merge. */
    int compute_partitions (THREAD_ENTRY *thread_p, QFILE_LIST_ID *outer_list_id, QFILE_LIST_ID *inner_list_id,
			    const QFILE_LIST_MERGE_INFO &merge_info, int degree, merge_partitions &result,
			    bool &can_partition);
  } /* namespace merge_join */
} /* namespace parallel_query */

#endif /* _PX_MERGE_JOIN_PARTITION_HPP_ */
