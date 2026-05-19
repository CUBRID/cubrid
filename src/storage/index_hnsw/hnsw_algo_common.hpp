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

//
// hnsw_algo_common.hpp
//

#ifndef _HNSW_ALGO_COMMON_HPP_
#define _HNSW_ALGO_COMMON_HPP_

#include <array>
#include <random>
#include <ankerl/unordered_dense.h>

#include "hnsw_api.hpp"
#include "hnsw_algo_common_graph_structure_profile.hpp"
#include "hnsw_algo_common_stats.hpp"
#include "hnsw_utils.hpp"
#include "thread_entry.hpp"
#include "vector_distance.hpp"
#include "environment_variable.h"

namespace cubhnsw
{
  // =====================================================================
  // algo's base structs
  // =====================================================================
  using index_id_t = BTID;
  using block_group_id_t = VFID;
  using block_id_t = VPID;
  using slot_id_t = OID;
  using key_id_t = OID;

  using level_t = int16_t;

  constexpr level_t MAX_LEVELS = 16;

  static_assert (MAX_LEVELS == HNSW_MAX_LEVEL_COUNT, "profile level count must match MAX_LEVELS");
  struct candidate_t
  {
    distance_t distance;
    slot_id_t slot;

    candidate_t (distance_t distance, slot_id_t slot): distance (distance), slot (slot) {}
    inline bool operator< (candidate_t other) const noexcept
    {
      return distance < other.distance;
    }
  };

  struct closer_candidate_t
  {
    bool operator() (candidate_t const &a,
		     candidate_t const &b) const noexcept
    {
      return a.distance < b.distance; // min-heap or ascending
    }
  };

  inline uint64_t encode_oid_key (const OID &o) noexcept
  {
    return (uint64_t (uint32_t (o.pageid)) << 32)
	   | (uint64_t (uint16_t (o.slotid)) << 16)
	   | uint64_t (uint16_t (o.volid));
  }

  struct visit_set_helper
  {
    using type = ankerl::unordered_dense::set<uint64_t>;
  };

  using visited_set_t = visit_set_helper::type;

  struct vector_cache_helper
  {
    using type = ankerl::unordered_dense::map<uint64_t, std::vector<float>>;
  };

  using vector_cache_t = vector_cache_helper::type;

  using neighbors_cache_per_level_t =
	  ankerl::unordered_dense::map<uint64_t, std::vector<slot_id_t>>;

  // One map per level; level is the array index, so the in-map key is just the
  // encoded slot (uint64_t). Avoids a composite key struct/hash on the hot path.
  using neighbors_cache_t = std::array<neighbors_cache_per_level_t, MAX_LEVELS>;

  using candidates_view_t = std::vector<candidate_t>;

  using candidates_allocator_t = std::allocator<candidate_t>;

  using top_candidates_t =
	  sorted_buffer_gt<candidate_t, std::less<candidate_t>, candidates_allocator_t>;

  using next_candidates_t =
	  max_heap_gt<candidate_t, std::less<candidate_t>, candidates_allocator_t>;

  struct add_result_t
  {
    int error {NO_ERROR};
    slot_id_t result;
  };

  struct search_result_t
  {
    int error {NO_ERROR};
    candidates_view_t results {};
    std::vector<OID> oids {};
  };

  struct algo_context_t
  {
    top_candidates_t m_top_candidates;
    top_candidates_t m_top_for_refine;
    next_candidates_t m_next_candidates;
    visited_set_t m_visits;

    cubthread::entry *m_thread_p {nullptr};
    level_t m_level {0};

    // stats
    bool m_is_perf_tracking {false};
    bool m_is_debugging {false};
    FILE *m_debug_fp {nullptr};
    std::vector<std::string> m_accessed_nodes; // for debug

    void open_debug_file (std::size_t level_start_debug_cnt, std::size_t debug_cnt, int level)
    {
      char path[PATH_MAX];
      if (!m_is_debugging)
	{
	  return;
	}

      constexpr std::size_t GROUP_SIZE = 10000;
      std::size_t group_start =
	      level_start_debug_cnt +
	      ((debug_cnt - level_start_debug_cnt) / GROUP_SIZE) * GROUP_SIZE;

      std::string filename =
	      "hnsw_debug_" +
	      std::to_string (group_start) +
	      "_L" + std::to_string (level) +
	      ".log";

      envvar_tmpdir_file (path, PATH_MAX, filename.c_str());

      m_debug_fp = fopen (path, "a");
    }

    void close_debug_file()
    {
      if (m_debug_fp)
	{
	  fclose (m_debug_fp);
	  m_debug_fp = nullptr;
	}
    }
    algo_stats_t m_stats;

    void clear_candidates ()
    {
      m_top_candidates.clear ();
      m_next_candidates.clear();
      m_visits.clear();
    }

    void collect_perf_stats ()
    {
      m_stats.collect_perf_stats (m_thread_p, m_is_perf_tracking);
    }
  };
}

#endif // _HNSW_ALGO_COMMON_HPP_
