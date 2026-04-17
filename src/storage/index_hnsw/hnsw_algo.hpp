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

#include <cmath>
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
	return metric_table_both_aligned_fast[static_cast<size_t> (m_metric)] (v1, v2, m_dimension);
      }

      inline void prepare_query_i8_ (algo_context_t &context, const float *query) const
      {
	if (context.m_query_i8_ready)
	  {
	    return;
	  }

	float max_abs = 0.0f;
	for (std::size_t i = 0; i < m_dimension; ++i)
	  {
	    max_abs = std::max (max_abs, std::fabs (query[i]));
	  }

	context.m_query_i8.scale = (max_abs > 0.0f) ? max_abs / 127.0f : 1.0f;

	// Ensure 64-byte aligned buffer (matches i8_cache_block slot alignment).
	const std::size_t needed = ((m_dimension + VECTOR_CACHE_ALIGNMENT - 1) / VECTOR_CACHE_ALIGNMENT)
				   * VECTOR_CACHE_ALIGNMENT;
	if (needed > context.m_query_i8_raw_capacity)
	  {
	    free (context.m_query_i8_raw);
	    context.m_query_i8_raw = std::aligned_alloc (VECTOR_CACHE_ALIGNMENT, needed);
	    assert (context.m_query_i8_raw != nullptr);
	    context.m_query_i8_raw_capacity = needed;
	  }

	std::int8_t *buf = static_cast<std::int8_t *> (context.m_query_i8_raw);
	for (std::size_t i = 0; i < m_dimension; ++i)
	  {
	    const float scaled = query[i] / context.m_query_i8.scale;
	    const float rounded = std::round (scaled);
	    const float clamped = std::max (-127.0f, std::min (127.0f, rounded));
	    buf[i] = static_cast<std::int8_t> (clamped);
	  }
	context.m_query_i8.values = buf;
	context.m_query_i8_ready = true;
      }

      inline distance_t compute_distance_i8_ (algo_context_t &context, const quantized_vector_i8 &v1,
					      const quantized_vector_i8 &v2) const
      {
	context.m_stats.on_distance_computed (context.m_is_perf_tracking, context.m_level, true);
	// Both v1 (query, aligned_alloc) and v2 (i8_cache_block slot) are 64-byte aligned.
	return metric_table_i8_aligned[static_cast<size_t> (m_metric)] (v1.values, v1.scale,
	       v2.values, v2.scale, m_dimension);
      }

      inline distance_t get_i8_recheck_window_ (float multiplier, distance_t radius) const
      {
	const distance_t base = std::max (std::fabs (radius), 1.0f);
	switch (m_metric)
	  {
	  case vector_distance_metric_t::EUCLIDEAN:
	    return multiplier * std::max (0.02f * base, static_cast<distance_t> (0.001f * m_dimension));
	  case vector_distance_metric_t::COSINE:
	  case vector_distance_metric_t::DOT:
	    return multiplier * 0.02f * base;
	  default:
	    assert (false);
	    return multiplier * 0.02f * base;
	  }
      }

      inline bool should_recheck_candidate_fp32_ (const algo_context_t &context,
	  distance_t coarse_dist, distance_t radius) const
      {
	return coarse_dist <= radius + get_i8_recheck_window_ (context.m_i8_prefilter_multiplier, radius);
      }

      // Slot-to-slot i8 coarse distance using pre-fetched cached_vector refs.
      // Both vectors must already be retrieved via get_cached_vector_by_slot_id().
      inline distance_t compute_distance_i8_between_ (algo_context_t &context,
	  const cached_vector &avec, const cached_vector &bvec) const
      {
	return compute_distance_i8_ (context, avec.values_i8, bvec.values_i8);
      }

      // Like should_recheck_candidate_fp32_() but for slot-to-slot (pair-wise) comparisons
      // in refine_(). Uses a wider window because both vectors were independently quantized
      // at insertion time, combining two independent quantization errors (variance ~2x vs
      // query-to-candidate, hence sqrt(2)~1.41x theoretical; 2.0x adds a safety margin).
      inline bool should_recheck_pairwise_fp32_ (const algo_context_t &context,
	  distance_t coarse_dist, distance_t threshold) const
      {
	constexpr float PAIRWISE_WINDOW_SCALE = 2.0f;
	return coarse_dist <= threshold
	       + PAIRWISE_WINDOW_SCALE * get_i8_recheck_window_ (context.m_i8_prefilter_multiplier, threshold);
      }

      inline distance_t compute_distance_from_query_ (algo_context_t &context, const float *query,
	  const cached_vector &vec) const
      {
	return compute_distance_ (context, query, vec.values);
      }

      inline distance_t compute_distance_from_query_ (algo_context_t &context, const float *query,
	  const slot_id_t &slot) const
      {
	const cached_vector *vec = m_storage->get_cached_vector_by_slot_id (context, slot, lock_mode::shared);
	return compute_distance_from_query_ (context, query, *vec);
      }

      inline distance_t compute_distance_from_query_i8_ (algo_context_t &context,
	  const cached_vector &vec) const
      {
	return compute_distance_i8_ (context, context.m_query_i8, vec.values_i8);
      }

      inline distance_t compute_distance_between (algo_context_t &context, const slot_id_t &a,
	  const slot_id_t &b) const
      {
	const cached_vector *avec = m_storage->get_cached_vector_by_slot_id (context, a, lock_mode::shared);
	const cached_vector *bvec = m_storage->get_cached_vector_by_slot_id (context, b, lock_mode::shared);
	return compute_distance_ (context, avec->values, bvec->values);
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

    assert (static_cast<std::size_t> (build_params.m) <= HNSW_MAX_M);

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
    context.m_i8_prefilter_multiplier = prm_get_float_value (PRM_ID_VECTOR_INDEX_I8_PREFILTER_MULTIPLIER);
    context.m_i8_only_build = prm_get_bool_value (PRM_ID_VECTOR_INDEX_I8_ONLY_BUILD);

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
    context.m_i8_prefilter_multiplier = prm_get_float_value (PRM_ID_VECTOR_INDEX_I8_PREFILTER_MULTIPLIER);
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

    prepare_query_i8_ (context, query);

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
    top.shrink (k);
    result.results.assign (top.data(), top.data() + top.size());
    for (std::size_t i = 0; i < top.size (); ++i)
      {
	pinned_t node_blk = m_storage->get_node_by_slot_id (context, result.results[i].slot, lock_mode::shared);
	result.oids.push_back (node_type (node_blk->data).get_key());
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
    prepare_query_i8_ (context, query);

    // Use epoch-based visited tracking for in-memory builds (O(1) array vs hash set).
    const bool use_epoch_visited = m_storage->has_fast_visited ();
    if (use_epoch_visited)
      {
	m_storage->visit_new_round ();
	m_storage->visit_check_and_mark (start_slot);
      }
    else
      {
	visits.insert (encode_oid_key (start_slot));
      }

    distance_t radius;
    if (context.m_i8_only_build)
      {
	const cached_vector *sv =
		m_storage->get_cached_vector_by_slot_id (context, start_slot, lock_mode::shared);
	radius = compute_distance_from_query_i8_ (context, *sv);
      }
    else
      {
	radius = compute_distance_from_query_ (context, query, start_slot);
      }

    next.insert_reserved (candidate_t (-radius, start_slot));
    top.insert_reserved (candidate_t (radius, start_slot));
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

	// Prefetch the next iteration's neighbor list data while we process the current candidate.
	// try_get_neighbors_cached is a pure hash-map lookup (no stats); the prefetch issues
	// a cache-line read for the flat neighbor array before we need it next iteration.
	if (!next.empty ())
	  {
	    neighbors_view pf = m_storage->try_get_neighbors_cached (next.top ().slot, level);
	    if (pf)
	      {
		__builtin_prefetch (pf.data, 0, 1);
	      }
	  }

	slot_id_t candidate_slot = candidacy.slot;

	// Try neighbors cache first (disk storage); fallback to direct neighbors_ref_type.
	neighbors_view cached_neighbors =
		m_storage->get_neighbors_cached_ids (context, candidate_slot, level);

	if (cached_neighbors)
	  {
	    context.m_stats.on_neighbors_cache_hit (context.m_is_perf_tracking, level);
	    const std::size_t neigh_n = cached_neighbors.size;

	    // Two-pass over unvisited neighbors to hide DRAM latency.
	    // Pass 1: resolve cached_vector* via hash map (L3 hit) + issue __builtin_prefetch
	    //         for float/i8 data immediately. Each prefetch has (n_resolved - i) *
	    //         T_pass2 cycles until consumed — far exceeding ~300 cycle DRAM latency.
	    // Pass 2: compute distances — data arrives from DRAM while we iterate.
	    constexpr std::size_t MAX_RESOLVED = 128; // >= 2*M for M up to 64
	    const cached_vector *resolved_vecs[MAX_RESOLVED];
	    slot_id_t            resolved_slots[MAX_RESOLVED];
	    std::size_t          n_resolved = 0;

	    for (std::size_t ni = 0; ni < neigh_n && n_resolved < MAX_RESOLVED; ++ni)
	      {
		slot_id_t successor_slot = cached_neighbors.data[ni];
		if (use_epoch_visited)
		  {
		    if (!m_storage->visit_check_and_mark (successor_slot))
		      {
			continue;
		      }
		  }
		else
		  {
		    auto [it, inserted] = visits.insert (encode_oid_key (successor_slot));
		    if (!inserted)
		      {
			continue;
		      }
		  }
		stats.on_visit ();
		const cached_vector *vec =
			m_storage->get_cached_vector_by_slot_id (context, successor_slot, lock_mode::shared);
		__builtin_prefetch (vec->values, 0, 0);
		__builtin_prefetch (vec->values + 16, 0, 0);
		if (vec->values_i8.values)
		  {
		    __builtin_prefetch (vec->values_i8.values, 0, 0);
		    if (m_dimension > 64)
		      __builtin_prefetch (vec->values_i8.values + 64, 0, 0);
		    if (m_dimension > 128)
		      __builtin_prefetch (vec->values_i8.values + 128, 0, 0);
		    if (m_dimension > 192)
		      __builtin_prefetch (vec->values_i8.values + 192, 0, 0);
		  }
		resolved_vecs[n_resolved] = vec;
		resolved_slots[n_resolved] = successor_slot;
		++n_resolved;
	      }

	    for (std::size_t i = 0; i < n_resolved; ++i)
	      {
		slot_id_t successor_slot = resolved_slots[i];
		const cached_vector *successor_vec = resolved_vecs[i];
		distance_t successor_dist;
		if (context.m_i8_only_build)
		  {
		    // i8 is the final metric; no fp32 refinement needed.
		    successor_dist = compute_distance_from_query_i8_ (context, *successor_vec);
		    if (top.size () >= expansion_limit && successor_dist >= radius)
		      {
			stats.on_candidate_prune ();
			continue;
		      }
		  }
		else
		  {
		    context.m_stats.on_prefilter_checked (context.m_is_perf_tracking, context.m_level);
		    distance_t d_i8 = compute_distance_from_query_i8_ (context, *successor_vec);
		    if (top.size () >= expansion_limit
			&& !should_recheck_candidate_fp32_ (context, d_i8, radius))
		      {
			context.m_stats.on_prefilter_rejected (context.m_is_perf_tracking, context.m_level);
			stats.on_candidate_prune ();
			continue;
		      }
		    context.m_stats.on_prefilter_passed_to_fp32 (context.m_is_perf_tracking, context.m_level);
		    successor_dist = compute_distance_from_query_ (context, query, *successor_vec);
		  }
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

	static constexpr std::size_t MAX_NEIGH = HNSW_MAX_M * 2 + 1;
	slot_id_t neigh_buf[MAX_NEIGH];
	std::size_t neigh_count = 0;

	for (std::size_t i = 0; i < candidate_neighbors.size (); ++i)
	  {
	    slot_id_t successor_slot = candidate_neighbors.at (i);
	    if (neigh_count < MAX_NEIGH)
	      {
		neigh_buf[neigh_count++] = successor_slot;
	      }
	    stats.on_neighbor_scan ();

	    if (use_epoch_visited)
	      {
		if (!m_storage->visit_check_and_mark (successor_slot))
		  {
		    continue;
		  }
	      }
	    else
	      {
		auto [it, inserted] = visits.insert (encode_oid_key (successor_slot));
		if (!inserted)
		  {
		    continue;
		  }
	      }
	    stats.on_visit ();

	    const cached_vector *successor_vec =
		    m_storage->get_cached_vector_by_slot_id (context, successor_slot, lock_mode::shared);
	    distance_t successor_dist;
	    if (context.m_i8_only_build)
	      {
		successor_dist = compute_distance_from_query_i8_ (context, *successor_vec);
		if (top.size () >= expansion_limit && successor_dist >= radius)
		  {
		    stats.on_candidate_prune ();
		    continue;
		  }
	      }
	    else
	      {
		context.m_stats.on_prefilter_checked (context.m_is_perf_tracking, context.m_level);
		distance_t d_i8 = compute_distance_from_query_i8_ (context, *successor_vec);
		if (top.size () >= expansion_limit
		    && !should_recheck_candidate_fp32_ (context, d_i8, radius))
		  {
		    context.m_stats.on_prefilter_rejected (context.m_is_perf_tracking, context.m_level);
		    stats.on_candidate_prune ();
		    continue;
		  }
		context.m_stats.on_prefilter_passed_to_fp32 (context.m_is_perf_tracking, context.m_level);
		successor_dist = compute_distance_from_query_ (context, query, *successor_vec);
	      }
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
	m_storage->set_neighbors_cached_ids (context, candidate_slot, level, neigh_buf, neigh_count);
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
    prepare_query_i8_ (context, query);

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
	    neighbors_view cached_neighbors =
		    m_storage->get_neighbors_cached_ids (context, closest_slot, level);

	    if (cached_neighbors)
	      {
		context.m_stats.on_neighbors_cache_hit (context.m_is_perf_tracking, level);
		const std::size_t neigh_n = cached_neighbors.size;

		// Two-pass: resolve + prefetch in pass 1, compute in pass 2.
		constexpr std::size_t MAX_RESOLVED = 128; // >= M for any supported M
		const cached_vector *resolved_vecs[MAX_RESOLVED];
		slot_id_t            resolved_slots[MAX_RESOLVED];
		std::size_t          n_resolved = 0;

		for (std::size_t ni = 0; ni < neigh_n && n_resolved < MAX_RESOLVED; ++ni)
		  {
		    slot_id_t neighbor_id = cached_neighbors.data[ni];
		    stats.on_neighbor_scan ();
		    const cached_vector *vec =
			    m_storage->get_cached_vector_by_slot_id (context, neighbor_id, lock_mode::shared);
		    __builtin_prefetch (vec->values, 0, 0);
		    __builtin_prefetch (vec->values + 16, 0, 0);
		    if (vec->values_i8.values)
		      {
			__builtin_prefetch (vec->values_i8.values, 0, 0);
			if (m_dimension > 64)
			  __builtin_prefetch (vec->values_i8.values + 64, 0, 0);
			if (m_dimension > 128)
			  __builtin_prefetch (vec->values_i8.values + 128, 0, 0);
			if (m_dimension > 192)
			  __builtin_prefetch (vec->values_i8.values + 192, 0, 0);
		      }
		    resolved_vecs[n_resolved] = vec;
		    resolved_slots[n_resolved] = neighbor_id;
		    ++n_resolved;
		  }

		for (std::size_t i = 0; i < n_resolved; ++i)
		  {
		    const cached_vector *neighbor_vec = resolved_vecs[i];
		    slot_id_t neighbor_id = resolved_slots[i];
		    context.m_stats.on_prefilter_checked (context.m_is_perf_tracking, context.m_level);
		    distance_t candidate_dist_i8 = compute_distance_from_query_i8_ (context, *neighbor_vec);
		    if (!should_recheck_candidate_fp32_ (context, candidate_dist_i8, closest_dist))
		      {
			context.m_stats.on_prefilter_rejected (context.m_is_perf_tracking, context.m_level);
			continue;
		      }
		    context.m_stats.on_prefilter_passed_to_fp32 (context.m_is_perf_tracking, context.m_level);
		    distance_t candidate_dist = compute_distance_from_query_ (context, query, *neighbor_vec);
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

		    const cached_vector *neighbor_vec =
			    m_storage->get_cached_vector_by_slot_id (context, neighbor_id, lock_mode::shared);
		    context.m_stats.on_prefilter_checked (context.m_is_perf_tracking, context.m_level);
		    distance_t candidate_dist_i8 = compute_distance_from_query_i8_ (context, *neighbor_vec);
		    if (!should_recheck_candidate_fp32_ (context, candidate_dist_i8, closest_dist))
		      {
			context.m_stats.on_prefilter_rejected (context.m_is_perf_tracking, context.m_level);
			continue;
		      }
		    context.m_stats.on_prefilter_passed_to_fp32 (context.m_is_perf_tracking, context.m_level);
		    distance_t candidate_dist = compute_distance_from_query_ (context, query, *neighbor_vec);
		    if (candidate_dist < closest_dist)
		      {
			closest_dist = candidate_dist;
			closest_slot = neighbor_id;
			changed = true;
		      }
		  }
		m_storage->set_neighbors_cached_ids (context, original_closest_slot, level, neigh.data (), neigh.size ());
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
    static constexpr std::size_t MAX_NEIGH = HNSW_MAX_M * 2 + 1;
    slot_id_t neigh_buf[MAX_NEIGH];
    std::size_t neigh_count = 0;
    for (std::size_t i = 0; i < new_neighbors.size () && neigh_count < MAX_NEIGH; ++i)
      {
	neigh_buf[neigh_count++] = new_neighbors.at (i);
      }
    m_storage->set_neighbors_cached_ids (context, new_node_blk->id, level, neigh_buf, neigh_count);
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
	      static constexpr std::size_t MAX_NEIGH = HNSW_MAX_M * 2 + 1;
	      slot_id_t neigh_buf[MAX_NEIGH];
	      std::size_t neigh_count = 0;
	      for (std::size_t i = 0; i < close_header.size () && neigh_count < MAX_NEIGH; ++i)
		{
		  neigh_buf[neigh_count++] = close_header.at (i);
		}
	      m_storage->set_neighbors_cached_ids (context, close_slot, level, neigh_buf, neigh_count);
	      m_graph_structure_profile.on_edges_added (level, 1);
	      continue;
	    }
	}

	top_candidates_t &top_for_refine = context.m_top_for_refine;
	top_for_refine.clear ();

	// n.distance already holds distance(value, close_slot) from seek_on_layer_/refine_.
	distance_t dist = n.distance;

	top_for_refine.insert_reserved (candidate_t (dist, new_slot));

	// Hoist close_vec lookup outside the inner loop — close_slot is invariant.
	const cached_vector *close_vec =
		m_storage->get_cached_vector_by_slot_id (context, close_slot, lock_mode::shared);
	std::size_t close_header_size = close_header.size ();
	for (std::size_t i = 0; i < close_header_size; i++)
	  {
	    slot_id_t successor_slot = close_header.at (i);
	    const cached_vector *succ_vec =
		    m_storage->get_cached_vector_by_slot_id (context, successor_slot, lock_mode::shared);
	    dist = compute_distance_ (context, close_vec->values, succ_vec->values);
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
	static constexpr std::size_t MAX_NEIGH_REV = HNSW_MAX_M * 2 + 1;
	slot_id_t neigh_buf_rev[MAX_NEIGH_REV];
	std::size_t neigh_count_rev = 0;
	for (std::size_t i = 0; i < close_header.size () && neigh_count_rev < MAX_NEIGH_REV; ++i)
	  {
	    neigh_buf_rev[neigh_count_rev++] = close_header.at (i);
	  }
	m_storage->set_neighbors_cached_ids (context, close_slot, level, neigh_buf_rev, neigh_count_rev);
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

    // Loop-invariant: multiplier does not change during refine.
    // When disabled (multiplier == 0), fall back to fp32-only compute_distance_between().
    const bool use_i8_prefilter = (context.m_i8_prefilter_multiplier != 0.0f);

    constexpr std::size_t MAX_SUBMITTED_CACHE = 128;
    const cached_vector *submitted_vec_cache[MAX_SUBMITTED_CACHE] = {};
    std::size_t submitted_vec_cached_count = 0;

    auto ensure_submitted_cached = [&] (std::size_t up_to)
    {
      for (; submitted_vec_cached_count < up_to; ++submitted_vec_cached_count)
        {
          submitted_vec_cache[submitted_vec_cached_count] =
              m_storage->get_cached_vector_by_slot_id (context,
                  top_data[submitted_vec_cached_count].slot, lock_mode::shared);
        }
    };

    std::size_t submitted_count = 1;
    std::size_t consumed_count = 1; /// Always equal or greater than `submitted_count`.

    if (use_i8_prefilter && top_count > 0)
      {
        ensure_submitted_cached (1);
      }

    while (submitted_count < needed && consumed_count < top_count)
      {
	candidate_t candidate = top_data[consumed_count];
	bool good = true;
	std::size_t idx = 0;

	// Fetch candidate vector once outside the inner loop (constant across all submitted entries).
	const cached_vector *cand_vec = use_i8_prefilter
					? m_storage->get_cached_vector_by_slot_id (context, candidate.slot, lock_mode::shared)
					: nullptr;
	assert (!use_i8_prefilter || cand_vec != nullptr);

	for (; idx < submitted_count; idx++)
	  {
	    candidate_t submitted = top_data[idx];

	    if (use_i8_prefilter)
	      {
		// i8 prefilter: skip fp32 when inter-distance is clearly > candidate.distance.
		// Uses a 2x-widened window (see should_recheck_pairwise_fp32_()) because both
		// vectors carry independent quantization errors from their insertion time.
		// On skip, continue the inner loop — candidate may still be pruned by a later submitted entry.
		const cached_vector *subm_vec = submitted_vec_cache[idx];
		context.m_stats.on_prefilter_checked (context.m_is_perf_tracking, context.m_level);
		distance_t i8_dist = compute_distance_i8_between_ (context, *cand_vec, *subm_vec);
		if (!should_recheck_pairwise_fp32_ (context, i8_dist, candidate.distance))
		  {
		    context.m_stats.on_prefilter_rejected (context.m_is_perf_tracking, context.m_level);
		    continue;
		  }
		context.m_stats.on_prefilter_passed_to_fp32 (context.m_is_perf_tracking, context.m_level);
		distance_t inter_result_dist = compute_distance_ (context, cand_vec->values, subm_vec->values);
		if (inter_result_dist < candidate.distance)
		  {
		    good = false;
		    break;
		  }
	      }
	    else
	      {
		distance_t inter_result_dist = compute_distance_between (context, candidate.slot, submitted.slot);
		if (inter_result_dist < candidate.distance)
		  {
		    good = false;
		    break;
		  }
	      }
	  }

	if (good)
	  {
	    top_data[submitted_count] = top_data[consumed_count];
	    submitted_count++;
	    if (use_i8_prefilter)
	      {
		ensure_submitted_cached (submitted_count);
	      }
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
