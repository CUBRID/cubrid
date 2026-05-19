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
// hnsw_algo_common_stats.hpp
//

#ifndef _HNSW_ALGO_COMMON_STATS_HPP_
#define _HNSW_ALGO_COMMON_STATS_HPP_

#include <cstddef>
#include <cstdint>

#include "perf_monitor.h"
#include "thread_entry.hpp"

namespace cubhnsw
{
  struct algo_stats_t
  {
    // ===========================
    // base stats
    // ===========================
    std::size_t visited_nodes {};
    std::size_t computed_distances {};
    std::size_t computed_distances_in_refines {};
    std::size_t computed_distances_in_reverse_refines {};

    std::size_t candidates_push {};
    std::size_t candidates_pop {};
    std::size_t candidates_prune {};
    std::size_t neighbors_scan {};

    std::size_t page_access {};
    std::size_t vector_access {};
    std::size_t vector_cache_hit {};
    std::size_t vector_cache_miss {};
    std::size_t neighbors_cache_hits {};
    std::size_t neighbors_page_fixes {};

    // ===========================
    // layer 0 stats
    // ===========================
    std::size_t visited_nodes_l0 {};
    std::size_t computed_distances_l0 {};
    std::size_t computed_distances_in_refines_l0 {};
    std::size_t computed_distances_in_reverse_refines_l0 {};

    std::size_t candidates_push_l0 {};
    std::size_t candidates_pop_l0 {};
    std::size_t candidates_prune_l0 {};
    std::size_t neighbors_scan_l0 {};

    std::size_t page_access_l0 {};
    std::size_t vector_access_l0 {};
    std::size_t vector_cache_hit_l0 {};
    std::size_t vector_cache_miss_l0 {};
    std::size_t neighbors_cache_hits_l0 {};
    std::size_t neighbors_page_fixes_l0 {};

    // ===========================
    // entry point
    // ===========================
    std::size_t entrypoint_updates {};

    struct refine_distance_collector
    {
      std::size_t *m_counter {nullptr};

      void add (std::size_t count) const
      {
	if (m_counter != nullptr)
	  {
	    *m_counter += count;
	  }
      }
    };

    struct seek_layer_collector
    {
      std::size_t visited_nodes {0};
      std::size_t candidates_push {0};
      std::size_t candidates_pop {0};
      std::size_t candidates_prune {0};
      std::size_t neighbors_scan {0};

      void on_start_node ()
      {
	visited_nodes++;
	candidates_push++;
      }

      void on_visit ()
      {
	visited_nodes++;
      }

      void on_candidate_push ()
      {
	candidates_push++;
      }

      void on_candidate_pop ()
      {
	candidates_pop++;
      }

      void on_candidate_prune ()
      {
	candidates_prune++;
      }

      void on_neighbor_scan ()
      {
	neighbors_scan++;
      }

      void commit (algo_stats_t &stats, bool is_perf_tracking, std::int16_t level) const
      {
	stats.add_stat (is_perf_tracking, level, stats.visited_nodes, stats.visited_nodes_l0, visited_nodes);
	stats.add_stat (is_perf_tracking, level, stats.candidates_push, stats.candidates_push_l0, candidates_push);
	stats.add_stat (is_perf_tracking, level, stats.candidates_pop, stats.candidates_pop_l0, candidates_pop);
	stats.add_stat (is_perf_tracking, level, stats.candidates_prune, stats.candidates_prune_l0, candidates_prune);
	stats.add_stat (is_perf_tracking, level, stats.neighbors_scan, stats.neighbors_scan_l0, neighbors_scan);
      }
    };

    struct seek_down_collector
    {
      std::size_t visited_nodes {0};
      std::size_t neighbors_scan {0};

      void on_start_node ()
      {
	visited_nodes++;
      }

      void on_visit ()
      {
	visited_nodes++;
      }

      void on_neighbor_scan ()
      {
	neighbors_scan++;
      }

      void commit (algo_stats_t &stats, bool is_perf_tracking) const
      {
	stats.add_stat (is_perf_tracking, stats.visited_nodes, visited_nodes);
	stats.add_stat (is_perf_tracking, stats.neighbors_scan, neighbors_scan);
      }
    };

    struct refine_collector
    {
      std::size_t computed_distances {0};

      refine_distance_collector get_refine_distance_collector ()
      {
	return refine_distance_collector {&computed_distances};
      }

      void commit (algo_stats_t &stats, bool is_perf_tracking, std::int16_t level) const
      {
	stats.add_stat (is_perf_tracking, level,
			stats.computed_distances_in_refines,
			stats.computed_distances_in_refines_l0,
			computed_distances);
      }
    };

    struct reverse_link_collector
    {
      std::size_t visited_nodes {0};
      std::size_t computed_distances_in_refines {0};

      explicit reverse_link_collector (std::size_t initial_visited_nodes)
	: visited_nodes (initial_visited_nodes)
      {
      }

      refine_distance_collector get_refine_distance_collector ()
      {
	return refine_distance_collector {&computed_distances_in_refines};
      }

      void commit (algo_stats_t &stats, bool is_perf_tracking, std::int16_t level) const
      {
	stats.add_stat (is_perf_tracking, level, stats.visited_nodes, stats.visited_nodes_l0, visited_nodes);
	stats.add_stat (is_perf_tracking, level,
			stats.computed_distances_in_reverse_refines,
			stats.computed_distances_in_reverse_refines_l0,
			computed_distances_in_refines);
      }
    };

    void collect_perf_stats (cubthread::entry *thread_p, bool &is_perf_tracking)
    {
      if (!is_perf_tracking)
	{
	  return;
	}

      auto add_stat_if_positive = [thread_p] (PERF_STAT_ID stat_id, std::int64_t value)
      {
	if (value > 0)
	  {
	    perfmon_add_stat (thread_p, stat_id, value);
	  }
      };

      add_stat_if_positive (PSTAT_HNSW_NUM_VISITED_NODE, visited_nodes);
      add_stat_if_positive (PSTAT_HNSW_NUM_COMPUTED_DISTANCES, computed_distances);
      add_stat_if_positive (PSTAT_HNSW_NUM_COMPUTED_DISTANCES_IN_REFINES, computed_distances_in_refines);
      add_stat_if_positive (PSTAT_HNSW_NUM_COMPUTED_DISTANCES_IN_REVERSE_REFINES,
			    computed_distances_in_reverse_refines);

      add_stat_if_positive (PSTAT_HNSW_NUM_VISITED_NODE_L0, visited_nodes_l0);
      add_stat_if_positive (PSTAT_HNSW_NUM_COMPUTED_DISTANCES_L0, computed_distances_l0);
      add_stat_if_positive (PSTAT_HNSW_NUM_COMPUTED_DISTANCES_IN_REFINES_L0,
			    computed_distances_in_refines_l0);
      add_stat_if_positive (PSTAT_HNSW_NUM_COMPUTED_DISTANCES_IN_REVERSE_REFINES_L0,
			    computed_distances_in_reverse_refines_l0);

      add_stat_if_positive (PSTAT_HNSW_NUM_PAGE_ACCESS, page_access);
      add_stat_if_positive (PSTAT_HNSW_NUM_PAGE_ACCESS_L0, page_access_l0);

      add_stat_if_positive (PSTAT_HNSW_NUM_VECTOR_ACCESS, vector_access);
      add_stat_if_positive (PSTAT_HNSW_NUM_VECTOR_ACCESS_L0, vector_access_l0);

      add_stat_if_positive (PSTAT_HNSW_NUM_VECTOR_CACHE_HIT, vector_cache_hit);
      add_stat_if_positive (PSTAT_HNSW_NUM_VECTOR_CACHE_HIT_L0, vector_cache_hit_l0);

      add_stat_if_positive (PSTAT_HNSW_NUM_VECTOR_CACHE_MISS, vector_cache_miss);
      add_stat_if_positive (PSTAT_HNSW_NUM_VECTOR_CACHE_MISS_L0, vector_cache_miss_l0);

      add_stat_if_positive (PSTAT_HNSW_NUM_NEIGHBORS_CACHE_HIT, neighbors_cache_hits);
      add_stat_if_positive (PSTAT_HNSW_NUM_NEIGHBORS_PAGE_FIX, neighbors_page_fixes);
      add_stat_if_positive (PSTAT_HNSW_NUM_NEIGHBORS_CACHE_HIT_L0, neighbors_cache_hits_l0);
      add_stat_if_positive (PSTAT_HNSW_NUM_NEIGHBORS_PAGE_FIX_L0, neighbors_page_fixes_l0);

      add_stat_if_positive (PSTAT_HNSW_NUM_ENTRYPOINT_UPDATES, entrypoint_updates);

      add_stat_if_positive (PSTAT_HNSW_NUM_CANDIDATES_PUSH, candidates_push);
      add_stat_if_positive (PSTAT_HNSW_NUM_CANDIDATES_POP, candidates_pop);
      add_stat_if_positive (PSTAT_HNSW_NUM_CANDIDATES_PRUNE, candidates_prune);
      add_stat_if_positive (PSTAT_HNSW_NUM_NEIGHBORS_SCAN, neighbors_scan);

      add_stat_if_positive (PSTAT_HNSW_NUM_CANDIDATES_PUSH_L0, candidates_push_l0);
      add_stat_if_positive (PSTAT_HNSW_NUM_CANDIDATES_POP_L0, candidates_pop_l0);
      add_stat_if_positive (PSTAT_HNSW_NUM_CANDIDATES_PRUNE_L0, candidates_prune_l0);
      add_stat_if_positive (PSTAT_HNSW_NUM_NEIGHBORS_SCAN_L0, neighbors_scan_l0);

      is_perf_tracking = false;
    }

    inline void on_distance_computed (bool is_perf_tracking, std::int16_t level)
    {
      add_stat (is_perf_tracking, level, computed_distances, computed_distances_l0, 1);
    }

    inline void on_entrypoint_updated (bool is_perf_tracking)
    {
      add_stat (is_perf_tracking, entrypoint_updates, 1);
    }

    inline void on_page_access (bool is_perf_tracking, std::int16_t level)
    {
      add_stat (is_perf_tracking, level, page_access, page_access_l0, 1);
    }

    inline void on_vector_access (bool is_perf_tracking, std::int16_t level)
    {
      add_stat (is_perf_tracking, level, vector_access, vector_access_l0, 1);
    }

    inline void on_vector_cache_hit (bool is_perf_tracking, std::int16_t level)
    {
      add_stat (is_perf_tracking, level, vector_cache_hit, vector_cache_hit_l0, 1);
    }

    inline void on_vector_cache_miss (bool is_perf_tracking, std::int16_t level)
    {
      add_stat (is_perf_tracking, level, vector_cache_miss, vector_cache_miss_l0, 1);
    }

    inline void on_neighbors_cache_hit (bool is_perf_tracking, std::int16_t level)
    {
      add_stat (is_perf_tracking, level, neighbors_cache_hits, neighbors_cache_hits_l0, 1);
    }

    inline void on_neighbors_page_fix (bool is_perf_tracking, std::int16_t level)
    {
      add_stat (is_perf_tracking, level, neighbors_page_fixes, neighbors_page_fixes_l0, 1);
    }

    inline void add_stat (bool is_perf_tracking, std::int16_t level,
			  std::size_t &stat, std::size_t &stat_l0, std::size_t counter)
    {
      if (!is_perf_tracking)
	{
	  return;
	}

      stat += counter;
      if (level == 0)
	{
	  stat_l0 += counter;
	}
    }

    inline void add_stat (bool is_perf_tracking, std::size_t &stat, std::size_t counter)
    {
      if (!is_perf_tracking)
	{
	  return;
	}

      stat += counter;
    }
  };
}

#endif // _HNSW_ALGO_COMMON_STATS_HPP_
