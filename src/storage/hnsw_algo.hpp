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

#include "hnsw_api.hpp"
#include "hnsw_utils.hpp"
#include "hnsw_graph_base.hpp"

#include <functional>
#include <random>

#include "storage_common.h" // RECDES
#include "record_descriptor.hpp"

#include "faiss/utils/distances.h" // faiss
#include "vector_distance_enum.h"

#include "hnsw_storage.hpp" // storage_t

#define HNSW_ALGO_DEBUG 0
#define HNSW_ALGO_PRINT(fmt, ...) do { if (HNSW_ALGO_DEBUG) { fprintf (stdout, fmt, ##__VA_ARGS__); fflush (stdout); } } while (0)

namespace cubhnsw
{

// =====================================================================
// distance
// =====================================================================

  enum class vector_distance_metric_t
  {
    COSINE,
    EUCLIDEAN,
    MAX
  };

  inline float cubvec_cosine_distance (const float *vec1, const float *vec2, size_t dim)
  {
    // dot(vec1, vec2)
    const float ip = faiss::fvec_inner_product (vec1, vec2, dim);

    // squared norms
    const float norm1 = faiss::fvec_norm_L2sqr (vec1, dim);
    const float norm2 = faiss::fvec_norm_L2sqr (vec2, dim);

    // Handle zero vectors to avoid division by zero
    if (norm1 == 0.0f || norm2 == 0.0f)
      {
	// NaN distance
	return std::numeric_limits<float>::quiet_NaN();
	// return 1.0f;
      }

    const float denom = sqrtf (norm1 * norm2);

    float similarity = ip / denom;
    similarity = std::max (-1.0f, std::min (1.0f, similarity));

    // cosine distance = 1 - cosine similarity
    float distance = 1.0f - similarity;

    // clamp again to [0,2] to avoid FP drift
    distance = std::max (0.0f, std::min (2.0f, distance));

    assert (distance >= 0.0f && distance <= 2.0f);

    return distance;
  }

  inline float cubvec_l2_distance (const float *vec1, const float *vec2, size_t dim)
  {
    float l2 = faiss::fvec_L2sqr (vec1, vec2, dim);
    return std::sqrt (l2);
  }

  using distance_t = float;
  using Fn = distance_t (*) (const float *, const float *, size_t);

  constexpr std::array<Fn, static_cast<size_t> (vector_distance_metric_t::MAX)> metric_table =
  {
    cubvec_cosine_distance,
    cubvec_l2_distance
  };

  // =====================================================================
  // algo's base structs
  // =====================================================================
  template <typename Traits>
  struct candidate_t
  {
    using slot_id_t = typename Traits::slot_id_t;
    distance_t distance;
    slot_id_t slot;

    candidate_t (distance_t distance, slot_id_t slot): distance (distance), slot (slot) {}
    inline bool operator< (candidate_t other) const noexcept
    {
      return distance < other.distance;
    }
  };

  template <typename Traits>
  struct closer_candidate_t
  {
    bool operator() (candidate_t<Traits> const &a,
		     candidate_t<Traits> const &b) const noexcept
    {
      return a.distance < b.distance; // min-heap or ascending
    }
  };


  struct oid_hash
  {
    std::size_t operator() (const OID &o) const noexcept
    {
      std::size_t h = 0;
      auto mix = [&h] (auto v)
      {
	std::size_t x = std::hash<std::decay_t<decltype (v)>> {} (v);
	h ^= x + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
      };

      mix (o.volid);
      mix (o.pageid);
      mix (o.slotid);
      return h;
    }
  };

  struct oid_equal
  {
    bool operator() (const OID &a, const OID &b) const noexcept
    {
      return a.volid == b.volid
	     && a.pageid == b.pageid
	     && a.slotid == b.slotid;
    }
  };

  template <typename T>
  struct visit_set_helper
  {
    using type = std::unordered_set<T>;
  };

  template <>
  struct visit_set_helper<OID>
  {
    using type = std::unordered_set<OID, oid_hash, oid_equal>;
  };

  template <typename Traits>
  using visited_set_t = typename visit_set_helper<typename Traits::slot_id_t>::type;

  template <typename Traits>
  using candidates_view_t = std::vector<candidate_t<Traits>>;

  template <typename Traits>
  using candidates_allocator_t = std::allocator<candidate_t<Traits>>;

  template <typename Traits>
  using top_candidates_t =
	  sorted_buffer_gt<candidate_t<Traits>, std::less<candidate_t<Traits>>, candidates_allocator_t<Traits>>;

  template <typename Traits>
  using next_candidates_t =
	  max_heap_gt<candidate_t<Traits>, std::less<candidate_t<Traits>>, candidates_allocator_t<Traits>>;

  template <typename Traits>
  struct add_result_t
  {
    int error {NO_ERROR};
    typename Traits::slot_id_t result;
  };

  template <typename Traits>
  struct search_result_t
  {
    int error {NO_ERROR};
    candidates_view_t<Traits> results {};
    std::vector<OID> oids {};
  };

  /* this class is modified version of the usearch implementation */
  // =====================================================================
  // algo class definition
  // =====================================================================
  template <typename Traits>
  class algo
  {
    public:
      using traits      = Traits;

      using key_id_t    = OID;
      using slot_id_t  = typename traits::slot_id_t;

      using pinned_t   = pinned_block<Traits, std::function<void()>>;
      using storage_t   = storage<traits>;

      using root_type   = root_t<traits>;
      using node_type   = node_t<traits>;
      using neighbors_ref_type = neighbors_ref_t<traits>;

      struct context_t
      {
	top_candidates_t<Traits> m_top_candidates;
	top_candidates_t<Traits> m_top_for_refine;
	next_candidates_t<Traits> m_next_candidates;
	visited_set_t<Traits> m_visits;
	std::default_random_engine m_level_generator;

	void clear_candidates ()
	{
	  m_top_candidates.clear ();
	  m_top_for_refine.clear ();
	  m_next_candidates.clear();
	  m_visits.clear();
	}
      };

      algo (const hnsw_build_params &build_params);

      add_result_t<Traits> add (const key_id_t &oid, const float *vector, const std::size_t expansion);
      search_result_t<Traits> search (const float *query, const std::size_t k, const std::size_t expansion);

      void set_storage (storage_t *storage) noexcept
      {
	m_storage = storage;
      }

    protected:

      void search_for_one_ (const float *query, const pinned_t &start_slot, const level_t begin_level,
			    const level_t end_level, slot_id_t &closest_slot);

      int search_to_insert_ (const float *query, const slot_id_t &start_slot, const level_t level,
			     const std::size_t top_limit);
      int search_to_find_in_base_ (const float *query, const slot_id_t &start_slot, const std::size_t expansion);

      void form_links_to_closest_ (const pinned_t &new_slot, const level_t level, candidates_view_t<Traits> &out);
      int form_reverse_links_ (const pinned_t &new_slot, const float *value, candidates_view_t<Traits> &new_neighbors,
			       level_t level);

      level_t choose_random_level_ (std::default_random_engine &generator, double inverse_log_connectivity);

      void refine_ (std::size_t needed, top_candidates_t<Traits> &top, candidates_view_t<Traits> &out) const;

      inline neighbors_ref_type get_neighbors (const pinned_t &node_blk, const level_t level)
      {
	node_type node = node_type (node_blk.data());
	neighbors_ref_type neighbors = neighbors_ref_type (node.neighbors_tape() + m_storage->node_neighbors_offset_ (level));

	HNSW_ALGO_PRINT ("[node] node: %s\n", node.dump().c_str());
	HNSW_ALGO_PRINT ("[neighbors of level %d] neighbors: %s\n", (int)level, neighbors.dump().c_str());

	return neighbors;
      }

      inline distance_t compute_distance_ (const float *v1, const float *v2) const
      {
	return metric_table[static_cast<size_t> (m_metric)] (v1, v2, m_dimension);
      }

      // variables
      context_t m_context;
      storage_t *m_storage {nullptr};

      // from build_params
      vector_distance_metric_t m_metric;
      std::size_t m_dimension;
      std::size_t m_connectivity;
      std::size_t m_expansion;

      // precomputed
      double m_inverse_log_connectivity;
  };

  // =====================================================================
  // algo class implementation
  // =====================================================================

  template <typename Traits>
  algo<Traits>::algo (const hnsw_build_params &build_params)
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
      default:
	assert (false);
      }

    // precompute inverse log connectivity
    m_inverse_log_connectivity = 1.0 / std::log (static_cast<double> (build_params.m));

    // pre-reserve top_for_refine
    m_context.m_top_for_refine.reserve (m_connectivity + 1);
  }

  template <typename Traits>
  add_result_t<Traits>
  algo<Traits>::add (const key_id_t &key, const float *vector, const std::size_t expansion)
  {
    add_result_t<Traits> result;

    m_storage->set_thread_entry (thread_get_thread_entry_info());

    m_context.clear_candidates();
    top_candidates_t<Traits> &top = m_context.m_top_candidates;
    next_candidates_t<Traits> &next = m_context.m_next_candidates;

    // TODO: now, connectivity_base is not considered.
    // std::size_t connecitvity_max = m_connectivity;
    std::size_t top_limit = std::max (m_connectivity + 1, expansion);
    if (!top.reserve (top_limit))
      {
	assert (false);
	return {ER_FAILED, OID_INITIALIZER};
      }
    if (!next.reserve (expansion))
      {
	assert (false);
	return {ER_FAILED, OID_INITIALIZER};
      }

    level_t curr_max_level;
    level_t new_target_level;

    slot_id_t entry_slot, new_slot;
    pinned_t root_block = m_storage->get_root (lock_mode::exclusive);
    root_type root_node = root_type (root_block.data());
    {
      curr_max_level = root_node.get_level(); // get max_level from root page
      new_target_level = choose_random_level_ (m_context.m_level_generator, m_inverse_log_connectivity);
      entry_slot = root_node.get_entry();
      if (new_target_level > MAX_LEVELS)
	{
	  // TODO: for optimzation, if new_target_level is greater than max_level, we can just use max_level
	  new_target_level = MAX_LEVELS;
	}

      //
      new_slot = m_storage->add_node (key, vector, new_target_level);
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
	  return result;
	}
    }

    if (new_target_level <= curr_max_level)
      {
	// unlock root
	//pinned_t cleanup {std::move (root_block)};
	//HNSW_ALGO_PRINT ("[add] unlock root\n");
      }

    {
      slot_id_t closest_slot {};
      {
	pinned_t entry_block = m_storage->get_node_by_slot_id (entry_slot, lock_mode::shared);
	(void) search_for_one_ (vector, entry_block, curr_max_level, new_target_level, closest_slot);
      }

      level_t level = (std::min) (new_target_level, curr_max_level);

      pinned_t new_node_blk = m_storage->get_node_by_slot_id (new_slot, lock_mode::exclusive);
      while (true)
	{
	  (void) search_to_insert_ (vector, closest_slot, level, top_limit);

	  candidates_view_t<Traits> closest_view;
	  {
	    neighbors_ref_type neighbors = get_neighbors (new_node_blk, level);
	    neighbors.clear();

	    form_links_to_closest_ (new_node_blk, level, closest_view);
	    closest_slot = closest_view[0].slot;
	  }
	  form_reverse_links_ (new_node_blk, vector, closest_view, level);
	  if (level == 0)
	    {
	      break;
	    }

	  --level;
	}
    }

    if (new_target_level > curr_max_level)
      {
	// promotion required
	//{
	//  pinned_t cleanup {std::move (root_block)};
	//}
	HNSW_ALGO_PRINT ("[add] promotion required: new_target_level: %d, curr_max_level: %d\n", (int)new_target_level,
			 (int)curr_max_level);
	root_node.set_entry (new_slot);
	root_node.set_level (new_target_level);
	m_storage->set_empty (false);
      }

    return result;
  }

  template <typename Traits>
  search_result_t<Traits>
  algo<Traits>::search (const float *query, const std::size_t k, const std::size_t expansion)
  {
    search_result_t<Traits> result;
    if (k ==0)
      {
	return result;
      }

    m_storage->set_thread_entry (thread_get_thread_entry_info());

    top_candidates_t<Traits> &top = m_context.m_top_candidates;
    next_candidates_t<Traits> &next = m_context.m_next_candidates;
    std::size_t expansion_size = std::max (k, expansion);

    m_context.clear_candidates();

    if (!top.reserve (expansion_size))
      {
	// TODO: error handling
	assert (false);
	return result;
      }
    if (!next.reserve (expansion_size))
      {
	assert (false);
	return result;
      }

    slot_id_t entry_slot;
    level_t root_level;
    {
      pinned_t root_block = m_storage->get_root (lock_mode::shared);
      root_type root_node = root_type (root_block.data());
      entry_slot = root_node.get_entry();
      root_level = root_node.get_level();
    }

    pinned_t entry_block = m_storage->get_node_by_slot_id (entry_slot, lock_mode::shared);

    slot_id_t closest_slot;
    (void) search_for_one_ (query, entry_block, root_level, 0, closest_slot);

    if (search_to_find_in_base_ (query, closest_slot, expansion_size) != NO_ERROR)
      {
	// TODO: error handling
	assert (false);
      }

    top.sort_ascending();
    top.shrink (k);

    result.results.assign (top.data(), top.data() + top.size());
    for (std::size_t i = 0; i < top.size (); ++i)
      {
	pinned_t node_blk = m_storage->get_node_by_slot_id (result.results[i].slot, lock_mode::shared);
	node_type node = node_type (node_blk.data());
	result.oids.push_back (node.get_key());
      }
    return result;
  }

  template <typename Traits>
  void
  algo<Traits>::search_for_one_ (const float *query, const pinned_t &start_slot, const level_t begin_level,
				 const level_t end_level, slot_id_t &out_slot)
  {
    visited_set_t<Traits> &visits = m_context.m_visits;
    visits.clear ();

    distance_t closest_dist;
    slot_id_t closest_slot;
    {
      node_type start_node = node_type (start_slot.data());
      pinned_t closest_vector_blk = m_storage->get_vector_by_slot_id (start_node.get_vec_slot(),
				    lock_mode::shared);

      closest_dist = compute_distance_ (query, reinterpret_cast<float *> (closest_vector_blk.data()));
      closest_slot = start_slot.id ();
    }

    node_type closest_node;
    for (level_t level = begin_level; level > end_level; --level)
      {
	bool changed = false;
	do
	  {
	    changed = false;

	    pinned_t closest_node_blk = m_storage->get_node_by_slot_id (closest_slot, lock_mode::shared);
	    neighbors_ref_type neighbors = get_neighbors (closest_node_blk, level);
	    for (std::size_t i = 0; i < neighbors.size (); ++i)
	      {
		slot_id_t neighbor_id = neighbors.at (i);
		pinned_t neighbor_node_blk = m_storage->get_node_by_slot_id (neighbor_id, lock_mode::shared);
		node_type neighbor_node = node_type (neighbor_node_blk.data());

		distance_t candidate_dist;
		{
		  pinned_t neighbor_vector_blk = m_storage->get_vector_by_slot_id (neighbor_node.get_vec_slot(),
						 lock_mode::shared);
		  candidate_dist = compute_distance_ (query, reinterpret_cast<float *> (neighbor_vector_blk.data()));
		}

		if (candidate_dist < closest_dist)
		  {
		    closest_dist = candidate_dist;
		    closest_slot = neighbor_id;
		    changed = true;
		  }
	      }
	  }
	while (changed);
      }

    out_slot = closest_slot;
  }

  template <typename Traits>
  int
  algo<Traits>::search_to_insert_ (const float *query, const slot_id_t &start_slot, const level_t level,
				   const std::size_t top_limit)
  {
    next_candidates_t<Traits> &next = m_context.m_next_candidates;
    top_candidates_t<Traits> &top = m_context.m_top_candidates;
    visited_set_t<Traits> &visits = m_context.m_visits;

    m_context.clear_candidates();

    distance_t radius;
    {
      pinned_t start_node_blk = m_storage->get_node_by_slot_id (start_slot, lock_mode::shared);
      node_type start_node = node_type (start_node_blk.data());
      pinned_t start_vector_blk = m_storage->get_vector_by_slot_id (start_node.get_vec_slot(), lock_mode::shared);
      radius = compute_distance_ (query, reinterpret_cast<float *> (start_vector_blk.data()));
    }

    //next.push (candidate_t<Traits> (radius, start_slot));
    next.insert_reserved (candidate_t<Traits> (-radius, start_slot));
    top.insert_reserved (candidate_t<Traits> (radius, start_slot));
    visits.insert (start_slot);

    while (!next.empty ())
      {
	candidate_t candidacy = next.top ();
	if ((-candidacy.distance) > radius && top.size () == top_limit)
	  {
	    break;
	  }

	next.pop ();

	slot_id_t candidate_slot = candidacy.slot;
	pinned_t candidate_node_blk = m_storage->get_node_by_slot_id (candidate_slot, lock_mode::shared);
	neighbors_ref_type candidate_neighbors = get_neighbors (candidate_node_blk, level);
	for (std::size_t i = 0; i < candidate_neighbors.size (); ++i)
	  {
	    slot_id_t successor_slot = candidate_neighbors.at (i);

	    bool already_visited = (visits.find (successor_slot) != visits.end());
	    if (already_visited)
	      {
		continue;
	      }
	    else
	      {
		// HNSW_ALGO_PRINT("[search_to_insert] insert successor_slot: %s\n", dump_slot (successor_slot).c_str());
		visits.insert (successor_slot);
	      }

	    distance_t sucessor_dist;
	    {
	      pinned_t successor_node_blk = m_storage->get_node_by_slot_id (successor_slot, lock_mode::shared);
	      node_type successor_node = node_type (successor_node_blk.data());
	      pinned_t successor_vector_blk = m_storage->get_vector_by_slot_id (successor_node.get_vec_slot(),
					      lock_mode::shared);
	      sucessor_dist = compute_distance_ (query, reinterpret_cast<float *> (successor_vector_blk.data()));
	    }

	    if (top.size () < top_limit || sucessor_dist < radius)
	      {
		next.insert (candidate_t<Traits> (-sucessor_dist, successor_slot));
		top.insert (candidate_t<Traits> (sucessor_dist, successor_slot), top_limit);
		radius = top.top ().distance;

		HNSW_ALGO_PRINT ("[search_to_insert] radius: %f\n", radius);
		HNSW_ALGO_PRINT ("[search_to_insert] sucessor_dist: %f\n", sucessor_dist);
		HNSW_ALGO_PRINT ("[search_to_insert] top.size(), top_limit: %zu, %zu\n", top.size(), top_limit);
	      }
	  }
      }

    return NO_ERROR;
  }

  template <typename Traits>
  int
  algo<Traits>::search_to_find_in_base_ (const float *query, const slot_id_t &start_slot, const std::size_t expansion)
  {
    next_candidates_t<Traits> &next = m_context.m_next_candidates;
    top_candidates_t<Traits> &top = m_context.m_top_candidates;
    visited_set_t<Traits> &visits = m_context.m_visits;
    std::size_t top_limit = expansion;

    m_context.clear_candidates();

    distance_t radius;
    {
      pinned_t start_node_blk = m_storage->get_node_by_slot_id (start_slot, lock_mode::shared);
      node_type start_node = node_type (start_node_blk.data());
      pinned_t start_vector_blk = m_storage->get_vector_by_slot_id (start_node.get_vec_slot(), lock_mode::shared);
      radius = compute_distance_ (query, reinterpret_cast<float *> (start_vector_blk.data()));
    }

    next.insert_reserved (candidate_t<Traits> (-radius, start_slot));
    top.insert_reserved (candidate_t<Traits> (radius, start_slot));
    visits.insert (start_slot);

    while (!next.empty())
      {
	candidate_t candidacy = next.top();
	if ((-candidacy.distance) > radius && top.size() == top_limit)
	  {
	    break;
	  }

	next.pop ();

	slot_id_t candidacy_slot = candidacy.slot;
	pinned_t candidacy_node_blk = m_storage->get_node_by_slot_id (candidacy_slot, lock_mode::shared);
	neighbors_ref_type candidate_neighbors = get_neighbors (candidacy_node_blk, 0);
	for (std::size_t i = 0; i < candidate_neighbors.size (); ++i)
	  {
	    slot_id_t successor_slot = candidate_neighbors.at (i);

	    bool already_visited = (visits.find (successor_slot) != visits.end());
	    if (already_visited)
	      {
		continue;
	      }
	    else
	      {
		visits.insert (successor_slot);
	      }

	    distance_t sucessor_dist;
	    {
	      pinned_t successor_node_blk = m_storage->get_node_by_slot_id (successor_slot, lock_mode::shared);
	      node_type successor_node = node_type (successor_node_blk.data());
	      pinned_t successor_vector_blk = m_storage->get_vector_by_slot_id (successor_node.get_vec_slot(),
					      lock_mode::shared);
	      sucessor_dist = compute_distance_ (query, reinterpret_cast<float *> (successor_vector_blk.data()));
	    }

	    if (top.size() < top_limit || sucessor_dist < radius)
	      {
		next.insert (candidate_t<Traits> (-sucessor_dist, successor_slot));
		top.insert (candidate_t<Traits> (sucessor_dist, successor_slot), top_limit);
		radius = top.top ().distance;
	      }
	  }
      }

    return NO_ERROR;
  }

  template <typename Traits>
  void
  algo<Traits>::form_links_to_closest_ (const pinned_t &new_node_blk, const level_t level,
					candidates_view_t<Traits> &top_view)
  {
    top_candidates_t<Traits> &top = m_context.m_top_candidates;
    refine_ (m_connectivity,top, top_view);

    // outgoing links from new node
    neighbors_ref_type new_neighbors = get_neighbors (new_node_blk, level);
    for (std::size_t i = 0; i != top_view.size(); i++)
      {
	new_neighbors.push_back (top_view[i].slot);
      }
  }

  template <typename Traits>
  int
  algo<Traits>::form_reverse_links_ (const pinned_t &new_node_blk, const float *value,
				     candidates_view_t<Traits> &new_neighbors,
				     level_t level)
  {
    node_type new_node = node_type (new_node_blk.data());

    for (auto n : new_neighbors)
      {
	slot_id_t close_slot = n.slot;
	if (close_slot == new_node_blk.id())
	  {
	    continue;
	  }

	// TODO: exclusive??
	pinned_t close_node_blk = m_storage->get_node_by_slot_id (close_slot, lock_mode::shared);
	neighbors_ref_type close_header = get_neighbors (close_node_blk, level);
	if (close_header.size () < m_connectivity)
	  {
	    close_header.push_back (new_node_blk.id());
	    continue;
	  }

	top_candidates_t<Traits> &top_for_refine = m_context.m_top_for_refine;
	top_for_refine.clear ();

	distance_t dist;
	{
	  node_type close_node = node_type (close_node_blk.data());
	  pinned_t close_vector_blk = m_storage->get_vector_by_slot_id (close_node.get_vec_slot(), lock_mode::shared);
	  dist = compute_distance_ (value, reinterpret_cast<float *> (close_vector_blk.data()));
	  top_for_refine.insert_reserved (candidate_t<Traits> (dist, close_slot));
	}

	for (std::size_t i = 0; i < close_header.size (); i++)
	  {
	    slot_id_t successor_slot = close_header.at (i);
	    pinned_t successor_node_blk = m_storage->get_node_by_slot_id (successor_slot, lock_mode::shared);
	    node_type successor_node = node_type (successor_node_blk.data());

	    {
	      node_type close_node = node_type (close_node_blk.data());
	      pinned_t close_vector_blk = m_storage->get_vector_by_slot_id (close_node.get_vec_slot(),  lock_mode::shared);
	      pinned_t successor_vector_blk = m_storage->get_vector_by_slot_id (successor_node.get_vec_slot(),
					      lock_mode::shared);

	      float *close_oid_vec = reinterpret_cast<float *> (close_vector_blk.data());
	      float *successor_oid_vec = reinterpret_cast<float *> (successor_vector_blk.data());
	      dist = compute_distance_ (close_oid_vec, successor_oid_vec);
	      top_for_refine.insert_reserved (candidate_t<Traits> (dist, successor_slot));
	    }
	  }

	// remove all neighbors from close_header
	close_header.clear();
	candidates_view_t<Traits> top_view;
	(void) refine_ (m_connectivity, top_for_refine, top_view);
	for (std::size_t i = 0; i != top_view.size (); i++)
	  {
	    close_header.push_back (top_view[i].slot);
	  }
      }

    return NO_ERROR;
  }

  template <typename Traits>
  void
  algo<Traits>::refine_ (std::size_t needed, top_candidates_t<Traits> &top, candidates_view_t<Traits> &out) const
  {
    out = {};

    candidate_t<Traits> *top_data = top.data();
    std::size_t const top_count = top.size();
    if (top_count < needed)
      {
	out.assign (top_data, top_data + top_count);
	return;
      }

    top.sort_ascending();

    std::size_t submitted_count = 1;
    std::size_t consumed_count = 1; /// Always equal or greater than `submitted_count`.
    while (submitted_count < needed && consumed_count < top_count)
      {
	candidate_t<Traits> candidate = top_data[consumed_count];
	bool good = true;
	std::size_t idx = 0;
	for (; idx < submitted_count; idx++)
	  {
	    candidate_t submitted = top_data[idx];

	    distance_t inter_result_dist;
	    {
	      pinned_t candidate_node_blk = m_storage->get_node_by_slot_id (candidate.slot, lock_mode::shared);
	      node_type candidate_node = node_type (candidate_node_blk.data());
	      pinned_t submitted_node_blk = m_storage->get_node_by_slot_id (submitted.slot, lock_mode::shared);
	      node_type submitted_node = node_type (submitted_node_blk.data());

	      pinned_t candidate_vector_blk = m_storage->get_vector_by_slot_id (candidate_node.get_vec_slot(),
					      lock_mode::shared);
	      pinned_t submitted_vector_blk = m_storage->get_vector_by_slot_id (submitted_node.get_vec_slot(),
					      lock_mode::shared);

	      float *candidate_oid_vec = reinterpret_cast<float *> (candidate_vector_blk.data());
	      float *submitted_oid_vec = reinterpret_cast<float *> (submitted_vector_blk.data());
	      inter_result_dist = compute_distance_ (candidate_oid_vec, submitted_oid_vec);
	    }

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

    top.shrink (submitted_count);

    out.assign (top_data, top_data + submitted_count);
  }

  template <typename Traits>
  level_t
  algo<Traits>::choose_random_level_ (std::default_random_engine &generator, double inverse_log_connectivity)
  {
    std::uniform_real_distribution<double> distribution (0.0, 1.0);
    double r = -std::log (distribution (generator)) * inverse_log_connectivity;
    return (level_t)r;
  }
}

#endif
