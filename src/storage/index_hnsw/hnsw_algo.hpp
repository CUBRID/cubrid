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
// hnsw_algo.hpp
//

#ifndef _HNSW_ALGO_HPP_
#define _HNSW_ALGO_HPP_

#include <functional>

#include "hnsw_api.hpp"
#include "hnsw_utils.hpp"
#include "hnsw_graph_base.hpp"
#include "hnsw_algo_common.hpp"
#include "hnsw_storage.hpp" // storage_t
#include "vector_distance.hpp"
#include "perf_monitor.h"
#include "system_parameter.h"

#define HNSW_ALGO_DEBUG 0
#define HNSW_ALGO_PRINT(fmt, ...) do { if (HNSW_ALGO_DEBUG) { fprintf (stdout, fmt, ##__VA_ARGS__); fflush (stdout); } } while (0)

namespace cubhnsw
{
  /* this class is modified version of the usearch implementation */
  // =====================================================================
  // algo class definition
  // =====================================================================
  class algo
  {
    public:
      using root_type   = root_t;
      using node_type   = node_t;
      using neighbors_ref_type = neighbors_ref_t;

      algo (const hnsw_build_params &build_params);

      // high-level APIs
      add_result_t add (cubthread::entry *thread_p, const key_id_t &oid, const float *vector);
      search_result_t search (cubthread::entry *thread_p, const float *query, const std::size_t k,
			      const std::size_t expansion);

      void set_storage (storage *storage) noexcept
      {
	m_storage = storage;
      }

      std::string dump ()
      {
	return m_graph_structure_profile.to_string ();
      }

    protected:
      inline uint64_t encode_slot_id_ (const slot_id_t &slot) const noexcept
      {
	return encode_oid_key (slot);
      }

      // horizontal seeking
      int seek_on_layer_ (algo_context_t &context, const float *query, const slot_id_t &start_slot,
			  const std::size_t expansion_limit);

      // vertical seeking
      int seek_down_ (algo_context_t &context, const float *query, const slot_id_t &start_slot,
		      const level_t target_level,
		      slot_id_t &closest_slot);

      // refining links
      void form_links_to_closest_ (algo_context_t &context, const pinned_t &new_slot,
				   candidates_view_t &out);
      int form_reverse_links_ (algo_context_t &context, const pinned_t &new_slot, const float *value,
			       candidates_view_t &new_neighbors);
      void refine_ (algo_context_t &context, std::size_t needed, top_candidates_t &top,
		    candidates_view_t &out,
		    algo_stats_t::refine_distance_collector refine_distance_collector) const;

      // random level generation
      level_t choose_random_level_ (std::default_random_engine &generator, double inverse_log_connectivity);

      // distance
      inline distance_t compute_distance_ (algo_context_t &context, const float *v1, const float *v2) const
      {
	context.m_stats.on_distance_computed (context.m_is_perf_tracking, context.m_level);
	return metric_table[static_cast<size_t> (m_metric)] (v1, v2, m_dimension);
      }

      inline distance_t compute_distance_from_query_ (algo_context_t &context, const float *query,
	  const slot_id_t &slot) const
      {
	const float *vec = m_storage->get_vector_by_slot_id (context, slot, lock_mode::shared);
	return compute_distance_ (context, query, vec);
      }

      inline distance_t compute_distance_between (algo_context_t &context, const slot_id_t &a,
	  const slot_id_t &b) const
      {
	const float *avec = m_storage->get_vector_by_slot_id (context, a, lock_mode::shared);
	const float *bvec = m_storage->get_vector_by_slot_id (context, b, lock_mode::shared);
	return compute_distance_ (context, avec, bvec);
      }

      inline neighbors_ref_type get_neighbors (algo_context_t &context,
	  const pinned_t &node_blk,
	  const level_t level)
      {
	context.m_stats.on_neighbors_page_fix (context.m_is_perf_tracking, level);

	node_type node = node_type (node_blk->data);
	neighbors_ref_type neighbors =
		neighbors_ref_type (node.neighbors_tape() + m_storage->node_neighbors_offset_ (level));

	HNSW_ALGO_PRINT ("[node] node: %s\n", node.dump().c_str());
	HNSW_ALGO_PRINT ("[neighbors of level %d] neighbors: %s\n", (int)level, neighbors.dump().c_str());

	return neighbors;
      }

      inline std::size_t get_layer_connectivity (level_t level, std::size_t connectivity) const noexcept
      {
	return level == 0 ? connectivity * 2 : connectivity;
      }

      // variables
      storage *m_storage {nullptr};

      // from build_params
      vector_distance_metric_t m_metric;
      std::size_t m_dimension;
      std::size_t m_connectivity;
      std::size_t m_expansion;

      std::default_random_engine m_level_generator {std::random_device{}()};

      std::size_t m_debug_group_start {0};
      std::size_t m_debug_cnt {0};

      // precomputed
      double m_inverse_log_connectivity;

      // profile
      graph_structure_profile_t m_graph_structure_profile;
  };

  // =====================================================================
  // algo class implementation
  // =====================================================================

  algo::algo (const hnsw_build_params &build_params)
    : m_dimension ((size_t) build_params.dimension), m_connectivity (build_params.m),
      m_expansion (build_params.ef_construction)
  {
    switch (build_params.metric)
      {
      case METRIC_COSINE:
	m_metric = vector_distance_metric_t::COSINE;
	break;
      case METRIC_EUCLIDEAN:
	m_metric = vector_distance_metric_t::EUCLIDEAN;
	break;
      case METRIC_DOT:
	m_metric = vector_distance_metric_t::DOT;
	break;
      default:
	assert (false);
      }

    // precompute inverse log connectivity
    m_inverse_log_connectivity = 1.0 / std::log (static_cast<double> (build_params.m));
  }

  add_result_t
  algo::add (cubthread::entry *thread_p, const key_id_t &key, const float *vector)
  {
    add_result_t result;

    algo_context_t context;
    context.m_thread_p = thread_p;
    context.m_is_perf_tracking = perfmon_is_perf_tracking ();
    context.m_is_debugging = prm_get_integer_value (PRM_ID_VECTOR_INDEX_DEBUG) != 0;

    context.clear_candidates();

    std::size_t connectivity_max = m_connectivity * 2 + 1;

    // pre-reserve top_for_refine
    context.m_top_for_refine.reserve (connectivity_max);

    // pre-reserve for visits
    context.m_visits.reserve (connectivity_max);

    top_candidates_t &top = context.m_top_candidates;
    next_candidates_t &next = context.m_next_candidates;

    // TODO: now, connectivity_base is not considered.
    // std::size_t connecitvity_max = m_connectivity;
    std::size_t top_limit = std::max (connectivity_max, m_expansion);
    if (!top.reserve (top_limit) || !next.reserve (top_limit))
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, top_limit * sizeof (candidate_t));
	assert (false);
	return {ER_FAILED, OID_INITIALIZER};
      }

    level_t curr_max_level, new_target_level;
    slot_id_t entry_slot, new_slot;

    pinned_t root_block = m_storage->get_root (context, lock_mode::exclusive);
    root_type root_node = root_type (root_block->data);
    {
      curr_max_level = root_node.get_level(); // get max_level from root page
      new_target_level = choose_random_level_ (m_level_generator, m_inverse_log_connectivity);
      entry_slot = root_node.get_entry();
      if (new_target_level > MAX_LEVELS)
	{
	  // TODO: for optimzation, if new_target_level is greater than max_level, we can just use max_level
	  new_target_level = MAX_LEVELS - 1;
	}

      if (context.m_is_debugging)
	{
	  if (new_target_level > curr_max_level)
	    {
	      m_debug_group_start = m_debug_cnt;
	    }
	  context.open_debug_file (m_debug_group_start, m_debug_cnt, std::max (curr_max_level, new_target_level));
	}

      if (m_metric == vector_distance_metric_t::COSINE)
	{
	  if (!cubvec_cosine_normalize ((float *) vector, m_dimension))
	    {
	      abort ();
	    }
	}
      //
      new_slot = m_storage->add_node (context, key, vector, new_target_level);
      //

      if (m_storage->is_empty())
	{
	  {
	    //pinned_t cleanup {std::move (root_block)};
	  }

	  //pinned_t promoted_root = m_storage->get_root (lock_mode::exclusive);
	  //root_type promoted_root_node = root_type (promoted_root.data());
	  root_node.set_entry (new_slot);
	  root_node.set_level (new_target_level);
	  m_storage->set_empty (false);
	  m_graph_structure_profile.on_node_added (new_target_level);
	  return result;
	}
    }

    if (new_target_level <= curr_max_level)
      {
	// TODO (investigate): it is safe to unlock root here.
	pinned_t cleanup {std::move (root_block)};
      }


    {
      slot_id_t closest_slot {};
      {
	// TODO: error handling
	context.m_level = curr_max_level;
	(void) seek_down_ (context, vector, entry_slot, new_target_level, closest_slot);
      }
      if (context.m_is_debugging)
	{
	  fprintf (context.m_debug_fp, "===== node num: %zu, slot: %s =====\n", m_debug_cnt++, dump_oid (new_slot).c_str());
	  if (!context.m_accessed_nodes.empty ())
	    {
	      for (const auto &node : context.m_accessed_nodes)
		{
		  fprintf (context.m_debug_fp, "(%s) -> ", node.data());
		}
	      fprintf (context.m_debug_fp, "END \n");
	      context.m_accessed_nodes.clear();
	    }
	}
      context.m_level = (std::min) (new_target_level, curr_max_level);

      pinned_t new_node_blk = m_storage->get_node_by_slot_id (context, new_slot, lock_mode::exclusive);

      if (context.m_is_debugging)
	{
	  fprintf (context.m_debug_fp, "target level: %d\n", context.m_level);
	}

      while (context.m_level >= 0)
	{
	  (void) seek_on_layer_ (context, vector, closest_slot,top_limit);

	  candidates_view_t closest_view;
	  {
	    neighbors_ref_type neighbors = get_neighbors (context, new_node_blk, context.m_level);
	    neighbors.clear();

	    form_links_to_closest_ (context, new_node_blk, closest_view);
	    closest_slot = closest_view[0].slot;
	  }
	  form_reverse_links_ (context, new_node_blk, vector, closest_view);

	  if (context.m_is_debugging)
	    {
	      fprintf (context.m_debug_fp, "level: %d\n", context.m_level);
	      if (!context.m_accessed_nodes.empty ())
		{
		  for (const auto &node : context.m_accessed_nodes)
		    {
		      fprintf (context.m_debug_fp, "(%s) -> ", node.data ());
		    }
		  fprintf (context.m_debug_fp, "END \n");
		  context.m_accessed_nodes.clear();
		}
	    }

	  --context.m_level;
	}
    }

    if (context.m_is_debugging)
      {
	context.close_debug_file();
      }

    // TODO: hnsw_debug
    m_graph_structure_profile.on_node_added (new_target_level);

    if (new_target_level > curr_max_level)
      {
	HNSW_ALGO_PRINT ("[add] promotion required: new_target_level: %d, curr_max_level: %d\n", (int)new_target_level,
			 (int)curr_max_level);
	// TODO: latch promotion is required
	{
	  m_storage->promote_root (root_block);
	}
	root_node.set_entry (new_slot);
	root_node.set_level (new_target_level);
	m_storage->set_empty (false);

	context.m_stats.on_entrypoint_updated (context.m_is_perf_tracking);
      }

    context.collect_perf_stats();

    return result;
  }

  search_result_t
  algo::search (cubthread::entry *thread_p, const float *query, const std::size_t k, const std::size_t expansion)
  {
    search_result_t result;
    if (k == 0)
      {
	return result;
      }

    algo_context_t context;
    context.m_thread_p = thread_p;
    context.m_is_perf_tracking = perfmon_is_perf_tracking ();
    context.clear_candidates();

    top_candidates_t &top = context.m_top_candidates;
    next_candidates_t &next = context.m_next_candidates;

    std::size_t expansion_size = std::max (k, expansion);

    // pre-reserve for visits
    context.m_visits.reserve (expansion_size);

    // pre-reserve for top and next
    if (!top.reserve (expansion_size) || !next.reserve (expansion_size))
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, expansion_size * sizeof (candidate_t));
	assert (false);
	return result;
      }

    slot_id_t entry_slot;
    level_t root_level;
    {
      pinned_t root_block = m_storage->get_root (context, lock_mode::shared);
      root_type root_node = root_type (root_block->data);
      entry_slot = root_node.get_entry();
      root_level = root_node.get_level();
    }

    if (m_metric == vector_distance_metric_t::COSINE)
      {
	if (!cubvec_cosine_normalize ((float *) query, m_dimension))
	  {
	    abort ();
	  }
      }

    slot_id_t closest_slot;

    context.m_level = root_level;
    if (seek_down_ (context, query, entry_slot, /* target level */ 0, closest_slot) != NO_ERROR)
      {
	// TODO: error handling
	assert (false);
      }

    // search from base level (0)
    assert (context.m_level == 0);

    if (seek_on_layer_ (context, query, closest_slot, expansion_size) != NO_ERROR)
      {
	// TODO: error handling
	assert (false);
      }

    // not to collect stats after seeking on base layer (0-level)
    context.m_level--;

    top.sort_ascending();
    result.results.reserve (k);
    result.oids.reserve (k);
    for (std::size_t i = 0; i < top.size () && result.results.size () < k; ++i)
      {
	candidate_t candidate = top.data ()[i];
	pinned_t node_blk = m_storage->get_node_by_slot_id (context, candidate.slot, lock_mode::shared);
	node_type node = node_type (node_blk->data);
	if (node.is_tombstoned ())
	  {
	    continue;
	  }

	result.results.push_back (candidate);
	result.oids.push_back (node.get_key ());
      }

    context.collect_perf_stats();

    return result;
  }

  int
  algo::seek_on_layer_ (algo_context_t &context, const float *query, const slot_id_t &start_slot,
			const std::size_t expansion_limit)
  {
    algo_stats_t::seek_layer_collector stats;
    level_t level = context.m_level;

    next_candidates_t &next = context.m_next_candidates;
    top_candidates_t &top = context.m_top_candidates;
    visited_set_t &visits = context.m_visits;

    context.clear_candidates();

    distance_t radius = compute_distance_from_query_ (context, query, start_slot);

    next.insert_reserved (candidate_t (-radius, start_slot));
    top.insert_reserved (candidate_t (radius, start_slot));
    visits.insert (encode_slot_id_ (start_slot));
    stats.on_start_node ();

    while (!next.empty ())
      {
	candidate_t candidacy = next.top ();
	if ((-candidacy.distance) > radius && top.size () == expansion_limit)
	  {
	    break;
	  }

	next.pop ();
	stats.on_candidate_pop ();

	slot_id_t candidate_slot = candidacy.slot;

	// Try neighbors cache first (disk storage); fallback to direct neighbors_ref_type.
	const std::vector<slot_id_t> *cached_neighbors =
		m_storage->get_neighbors_cached_ids (context, candidate_slot, level);

	if (cached_neighbors != nullptr)
	  {
	    context.m_stats.on_neighbors_cache_hit (context.m_is_perf_tracking, level);
	    for (slot_id_t successor_slot : *cached_neighbors)
	      {
		auto [it, inserted] = visits.insert (encode_slot_id_ (successor_slot));
		if (!inserted)
		  {
		    continue;
		  }
		stats.on_visit ();

		distance_t successor_dist = compute_distance_from_query_ (context, query, successor_slot);
		if (top.size () < expansion_limit || successor_dist < radius)
		  {
		    next.insert (candidate_t (-successor_dist, successor_slot));
		    top.insert (candidate_t (successor_dist, successor_slot), expansion_limit);
		    radius = top.top ().distance;

		    HNSW_ALGO_PRINT ("[search_to_insert] radius: %f\n", radius);
		    HNSW_ALGO_PRINT ("[search_to_insert] successor_dist: %f\n", successor_dist);
		    HNSW_ALGO_PRINT ("[search_to_insert] top.size(), expansion_limit: %zu, %zu\n", top.size(), expansion_limit);
		  }
	      }
	    continue;
	  }

	// No cache: load node and neighbors directly and populate cache.
	pinned_t candidate_node_blk = m_storage->get_node_by_slot_id (context, candidate_slot, lock_mode::shared);
	neighbors_ref_type candidate_neighbors = get_neighbors (context, candidate_node_blk, level);

	std::vector<slot_id_t> neigh;
	neigh.reserve (candidate_neighbors.size ());

	for (std::size_t i = 0; i < candidate_neighbors.size (); ++i)
	  {
	    slot_id_t successor_slot = candidate_neighbors.at (i);
	    neigh.push_back (successor_slot);
	    stats.on_neighbor_scan ();

	    auto [it, inserted] = visits.insert (encode_slot_id_ (successor_slot));
	    if (!inserted)
	      {
		continue;
	      }
	    stats.on_visit ();

	    distance_t successor_dist = compute_distance_from_query_ (context, query, successor_slot);
	    if (top.size () < expansion_limit || successor_dist < radius)
	      {
		next.insert (candidate_t (-successor_dist, successor_slot));
		top.insert (candidate_t (successor_dist, successor_slot), expansion_limit);
		radius = top.top ().distance;

		stats.on_candidate_push ();

		HNSW_ALGO_PRINT ("[search_to_insert] radius: %f\n", radius);
		HNSW_ALGO_PRINT ("[search_to_insert] successor_dist: %f\n", successor_dist);
		HNSW_ALGO_PRINT ("[search_to_insert] top.size(), expansion_limit: %zu, %zu\n", top.size(), expansion_limit);
	      }
	    else
	      {
		stats.on_candidate_prune ();
	      }
	  }
	m_storage->set_neighbors_cached_ids (context, candidate_slot, level, std::move (neigh));
      }

    stats.commit (context.m_stats, context.m_is_perf_tracking, context.m_level);

    return NO_ERROR;
  }

  int
  algo::seek_down_ (algo_context_t &context, const float *query, const slot_id_t &start_slot,
		    const level_t target_level, slot_id_t &out_slot)
  {
    algo_stats_t::seek_down_collector stats;

    visited_set_t &visits = context.m_visits;
    visits.clear ();

    slot_id_t closest_slot = start_slot;
    distance_t closest_dist = compute_distance_from_query_ (context, query, closest_slot);
    stats.on_start_node ();
    for (; context.m_level > target_level; --context.m_level)
      {
	bool changed = false;
	do
	  {
	    changed = false;
	    level_t level = context.m_level;
	    // Try neighbors cache first; fallback to direct neighbors_ref_type.
	    const std::vector<slot_id_t> *cached_neighbors =
		    m_storage->get_neighbors_cached_ids (context, closest_slot, level);

	    if (cached_neighbors != nullptr)
	      {
		context.m_stats.on_neighbors_cache_hit (context.m_is_perf_tracking, level);
		for (slot_id_t neighbor_id : *cached_neighbors)
		  {
		    stats.on_neighbor_scan ();
		    distance_t candidate_dist = compute_distance_from_query_ (context, query, neighbor_id);
		    if (candidate_dist < closest_dist)
		      {
			closest_dist = candidate_dist;
			closest_slot = neighbor_id;
			changed = true;
		      }
		  }
	      }
	    else
	      {
		pinned_t closest_node_blk = m_storage->get_node_by_slot_id (context, closest_slot, lock_mode::shared);
		const slot_id_t original_closest_slot = closest_slot;

		neighbors_ref_type neighbors = get_neighbors (context, closest_node_blk, level);
		std::vector<slot_id_t> neigh;
		neigh.reserve (neighbors.size ());

		for (std::size_t i = 0; i < neighbors.size (); ++i)
		  {
		    slot_id_t neighbor_id = neighbors.at (i);
		    neigh.push_back (neighbor_id);
		    stats.on_neighbor_scan ();

		    distance_t candidate_dist = compute_distance_from_query_ (context, query, neighbor_id);
		    if (candidate_dist < closest_dist)
		      {
			closest_dist = candidate_dist;
			closest_slot = neighbor_id;
			changed = true;
		      }
		  }
		m_storage->set_neighbors_cached_ids (context, original_closest_slot, level, std::move (neigh));
	      }
	    stats.on_visit ();
	  }
	while (changed);
      }

    stats.commit (context.m_stats, context.m_is_perf_tracking);

    out_slot = closest_slot;
    return NO_ERROR;
  }

  void
  algo::form_links_to_closest_ (algo_context_t &context, const pinned_t &new_node_blk,
				candidates_view_t &top_view)
  {
    algo_stats_t::refine_collector stats;
    top_candidates_t &top = context.m_top_candidates;

    std::size_t level = context.m_level;
    refine_ (context, m_connectivity, top, top_view, stats.get_refine_distance_collector ());
    stats.commit (context.m_stats, context.m_is_perf_tracking, context.m_level);

    // outgoing links from new node
    neighbors_ref_type new_neighbors = get_neighbors (context, new_node_blk, level);
    for (std::size_t i = 0; i != top_view.size(); i++)
      {
	new_neighbors.push_back (top_view[i].slot);
      }

    // neighbors of new node changed; update in-memory neighbors cache if storage supports it

    std::vector<slot_id_t> neigh;
    neigh.reserve (new_neighbors.size ());
    for (std::size_t i = 0; i < new_neighbors.size (); ++i)
      {
	neigh.push_back (new_neighbors.at (i));
      }
    m_storage->set_neighbors_cached_ids (context, new_node_blk->id, level, std::move (neigh));
    m_graph_structure_profile.on_edges_added (level, top_view.size ());
  }

  int
  algo::form_reverse_links_ (algo_context_t &context, const pinned_t &new_node_blk, const float *value,
			     candidates_view_t &new_neighbors)
  {
    algo_stats_t::reverse_link_collector stats (new_neighbors.size ());

    std::size_t level = context.m_level;
    std::size_t layer_connectivity = get_layer_connectivity (level, m_connectivity);
    for (auto n : new_neighbors)
      {
	slot_id_t close_slot = n.slot;
	slot_id_t new_slot = new_node_blk->id;
	if (close_slot == new_slot)
	  {
	    continue;
	  }

	neighbors_ref_type close_header;

	// TODO: exclusive??
	pinned_t close_node_blk = m_storage->get_node_by_slot_id (context, close_slot, lock_mode::exclusive);
	{
	  close_header = get_neighbors (context, close_node_blk, level);
	  if (close_header.size () < layer_connectivity)
	    {
	      close_header.push_back (new_slot);
	      // neighbors of close_slot changed; update in-memory cache
	      std::vector<slot_id_t> neigh;
	      neigh.reserve (close_header.size ());
	      for (std::size_t i = 0; i < close_header.size (); ++i)
		{
		  neigh.push_back (close_header.at (i));
		}
	      m_storage->set_neighbors_cached_ids (context, close_slot, level, std::move (neigh));
	      m_graph_structure_profile.on_edges_added (level, 1);
	      continue;
	    }
	}

	top_candidates_t &top_for_refine = context.m_top_for_refine;
	top_for_refine.clear ();

	distance_t dist = compute_distance_from_query_ (context, value, close_slot);

	top_for_refine.insert_reserved (candidate_t (dist, new_slot));

	std::size_t close_header_size = close_header.size ();
	for (std::size_t i = 0; i < close_header_size; i++)
	  {
	    slot_id_t successor_slot = close_header.at (i);
	    dist = compute_distance_between (context, close_slot, successor_slot);
	    top_for_refine.insert_reserved (candidate_t (dist, successor_slot));
	  }

	// remove all neighbors from close_header
	m_graph_structure_profile.on_edges_removed (level, close_header_size);
	close_header.clear();
	candidates_view_t top_view;

	refine_ (context, layer_connectivity, top_for_refine, top_view, stats.get_refine_distance_collector ());

	for (std::size_t i = 0; i != top_view.size (); i++)
	  {
	    close_header.push_back (top_view[i].slot);
	  }

	// neighbors of close_slot changed; update in-memory cache

	std::vector<slot_id_t> neigh;
	neigh.reserve (close_header.size ());
	for (std::size_t i = 0; i < close_header.size (); ++i)
	  {
	    neigh.push_back (close_header.at (i));
	  }
	m_storage->set_neighbors_cached_ids (context, close_slot, level, std::move (neigh));
	m_graph_structure_profile.on_edges_added (level, top_view.size ());
      }

    stats.commit (context.m_stats, context.m_is_perf_tracking, context.m_level);

    return NO_ERROR;
  }

  void
  algo::refine_ (algo_context_t &context, std::size_t needed, top_candidates_t &top,
		 candidates_view_t &out,
		 algo_stats_t::refine_distance_collector refine_distance_collector) const
  {
    out = {};
    std::size_t old_computed_distances = 0;

    candidate_t *top_data = top.data();
    std::size_t const top_count = top.size();
    if (top_count < needed)
      {
	out.assign (top_data, top_data + top_count);
	return;
      }

    top.sort_ascending();

    if (context.m_is_perf_tracking)
      {
	old_computed_distances = context.m_stats.computed_distances;
      }

    std::size_t submitted_count = 1;
    std::size_t consumed_count = 1; /// Always equal or greater than `submitted_count`.
    while (submitted_count < needed && consumed_count < top_count)
      {
	candidate_t candidate = top_data[consumed_count];
	bool good = true;
	std::size_t idx = 0;
	for (; idx < submitted_count; idx++)
	  {
	    candidate_t submitted = top_data[idx];

	    distance_t inter_result_dist = compute_distance_between (context, candidate.slot, submitted.slot);
	    if (inter_result_dist < candidate.distance)
	      {
		good = false;
		break;
	      }
	  }

	if (good)
	  {
	    top_data[submitted_count] = top_data[consumed_count];
	    submitted_count++;
	  }
	consumed_count++;
      }

    if (context.m_is_perf_tracking)
      {
	refine_distance_collector.add (context.m_stats.computed_distances - old_computed_distances);
      }

    top.shrink (submitted_count);
    out.assign (top_data, top_data + submitted_count);
  }

  level_t
  algo::choose_random_level_ (std::default_random_engine &generator, double inverse_log_connectivity)
  {
    std::uniform_real_distribution<double> distribution (0.0, 1.0);
    double r = -std::log (distribution (generator)) * inverse_log_connectivity;
    return (level_t)r;
  }
}

#endif
