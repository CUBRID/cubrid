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
// hnsw_algo_common_graph_structure_profile.hpp
//

#ifndef _HNSW_ALGO_COMMON_GRAPH_STRUCTURE_PROFILE_HPP_
#define _HNSW_ALGO_COMMON_GRAPH_STRUCTURE_PROFILE_HPP_

#include <array>
#include <atomic>
#include <cstdint>
#include <sstream>
#include <string>

namespace cubhnsw
{
  using graph_level_t = std::int16_t;

  constexpr std::size_t HNSW_MAX_LEVEL_COUNT = 16;

  struct graph_structure_profile_t
  {
    std::array<std::atomic<size_t>, HNSW_MAX_LEVEL_COUNT> nodes_per_level;
    std::array<std::atomic<size_t>, HNSW_MAX_LEVEL_COUNT> degree_sum_per_level;

    std::atomic<graph_level_t> max_level;
    std::atomic<std::size_t> total_nodes;

    graph_structure_profile_t ()
    {
      total_nodes.store (0, std::memory_order_relaxed);
      max_level.store (0, std::memory_order_relaxed);
      for (size_t i = 0; i < HNSW_MAX_LEVEL_COUNT; ++i)
	{
	  nodes_per_level[i].store (0, std::memory_order_relaxed);
	  degree_sum_per_level[i].store (0, std::memory_order_relaxed);
	}
    }

    void on_node_added (graph_level_t level)
    {
      for (graph_level_t l = 0; l <= level; ++l)
	{
	  nodes_per_level[l]++;
	}

      if (level > max_level)
	{
	  max_level.store (level, std::memory_order_relaxed);
	}

      total_nodes++;
    }

    void on_edges_added (graph_level_t level, std::size_t count)
    {
      degree_sum_per_level[level].fetch_add (count, std::memory_order_relaxed);
    }

    void on_edges_removed (graph_level_t level, std::size_t count)
    {
      degree_sum_per_level[level].fetch_sub (count, std::memory_order_relaxed);
    }

    std::string to_string () const
    {
      std::ostringstream oss;

      if (total_nodes == 0)
	{
	  return oss.str();
	}

      oss << "==== HNSW Graph Profile ====\n";
      oss << "Total nodes: " << total_nodes << "\n";
      oss << "Max level: " << max_level << "\n";

      for (graph_level_t l = 0; l <= max_level; ++l)
	{
	  std::size_t n = nodes_per_level[l];
	  std::size_t deg_sum = degree_sum_per_level[l];

	  double avg = (n > 0)
		       ? static_cast<double> (deg_sum) / static_cast<double> (n)
		       : 0.0;

	  oss << "[Level " << l
	      << "] nodes: " << n
	      << ", avg_degree: " << avg
	      << "\n";
	}

      return oss.str();
    }
  };
}

#endif // _HNSW_ALGO_COMMON_GRAPH_STRUCTURE_PROFILE_HPP_
