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

#include <random>

#include "storage_common.h" // RECDES
#include "record_descriptor.hpp"

#include "faiss/utils/distances.h" // faiss
#include "vector_distance_enum.h"

#include "hnsw_storage.hpp" // storage_t

constexpr int MAX_LEVELS = 16;

enum class vector_distance_metric_t
{
  COSINE,
  EUCLIDEAN,
  MAX
};

inline float cubvec_cosine_distance (const float *vec1, const float *vec2, size_t dim)
{
  float ip = faiss::fvec_inner_product (vec1, vec2, dim);
  float norm1 = faiss::fvec_norm_L2sqr (vec1, dim);
  float norm2 = faiss::fvec_norm_L2sqr (vec2, dim);

  // Handle zero vectors to avoid division by zero
  if (norm1 == 0.0f || norm2 == 0.0f)
    {
      // NaN distance
      return std::numeric_limits<float>::quiet_NaN();
    }

  float similarity = ip / (sqrtf (norm1) * sqrtf (norm2));

  // Clamp the similarity value to [-1, 1] to handle floating-point errors
  if (similarity > 1.0f)
    {
      similarity = 1.0f;
    }
  if (similarity < -1.0f)
    {
      similarity = -1.0f;
    }

  // Cosine distance is 1 - cosine similarity
  float distance = 1.0f - similarity;
  assert (distance <= 2.0f && distance >= 0.0f);
  return distance;
}

inline float cubvec_l2_distance (const float *vec1, const float *vec2, size_t dim)
{
  float l2 = faiss::fvec_L2sqr (vec1, vec2, dim);
  return std::sqrt (l2);
}

namespace cubhnsw
{
  // =====================================================================
  // distance
  // =====================================================================

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
  struct candidate_t
  {
    distance_t distance;
    OID key;

    candidate_t (distance_t distance, OID key): distance (distance), key (key) {}
    inline bool operator< (candidate_t other) const noexcept
    {
      return distance < other.distance;
    }
    // inline bool operator<(const candidate_t& other) const noexcept { return distance < other.distance; }
  };

  struct less_candidate_t
  {
    bool operator() (const candidate_t &a, const candidate_t &b) const
    {
      return a.distance < b.distance;
    }
  };

  using visited_set_t = std::unordered_set<OID>;
  using candidates_view_t = std::vector<candidate_t>;
  using top_candidates_t = sorted_buffer_gt<candidate_t, std::less<candidate_t>, std::allocator<candidate_t>>;
  using next_candidates_t = std::priority_queue<candidate_t, std::vector<candidate_t>, less_candidate_t>;

  struct add_result_t
  {
    int error {NO_ERROR};
    OID result;
  };

  struct search_result_t
  {
    int error {NO_ERROR};
    candidates_view_t results {};
  };

  // =====================================================================
  // algo's graph structs
  // =====================================================================

  // =====================================================================
  // graph
  // =====================================================================
  template <typename ID_TRAITS>
  class root_t
  {
    protected:
      byte_t *tape_ {};

    public:
      using slot_id_t = typename ID_TRAITS::slot_id_t;

      explicit root_t (byte_t *tape) noexcept : tape_ (tape) {}
      byte_t *tape() const noexcept
      {
	return tape_;
      }

      explicit operator bool() const noexcept
      {
	return tape_;
      }

      root_t() = default;
      root_t (root_t const &) = default;
      root_t &operator= (root_t const &) = default;

      static constexpr std::size_t offset_params = 0;
      static constexpr std::size_t offset_level = sizeof (hnsw_build_params);
      static constexpr std::size_t offset_entry = offset_level + sizeof (level_t);

      misaligned_ref_gt<hnsw_build_params> get_params () const noexcept
      {
	return {tape_};
      }
      void set_params (hnsw_build_params v) noexcept
      {
	return misaligned_store<hnsw_build_params> (tape_, v);
      }

      misaligned_ref_gt<level_t> get_level() const noexcept
      {
	return {tape_ + offset_level};
      }
      void set_level (level_t v) noexcept
      {
	return misaligned_store<level_t> (tape_ + offset_level, v);
      }

      misaligned_ref_gt<slot_id_t> get_entry() const noexcept
      {
	return {tape_ + offset_entry};
      }
      void set_entry (slot_id_t v) noexcept
      {
	return misaligned_store<slot_id_t> (tape_ + offset_entry, v);
      }

      static constexpr std::size_t root_bytes_() noexcept
      {
	return sizeof (hnsw_build_params) + sizeof (level_t) + sizeof (slot_id_t);
      }
  };

  template <class ID_TRAITS>
  class node_t
  {
    protected:
      byte_t *tape_ {};

    public:
      using slot_id_t = typename ID_TRAITS::slot_id_t;

      explicit node_t (byte_t *tape) noexcept : tape_ (tape) {}
      byte_t *tape() const noexcept
      {
	return tape_;
      }
      byte_t *neighbors_tape() const noexcept
      {
	return tape_ + node_head_bytes_();
      }
      explicit operator bool() const noexcept
      {
	return tape_;
      }

      node_t() = default;
      node_t (node_t const &) = default;
      node_t &operator= (node_t const &) = default;

      static constexpr std::size_t offset_key = 0;
      static constexpr std::size_t offset_level = sizeof (OID);

      misaligned_ref_gt<OID> get_key() const noexcept
      {
	return {tape_};
      }
      void set_key (OID v) noexcept
      {
	return misaligned_store<OID> (tape_, v);
      }

      misaligned_ref_gt<level_t> get_level() const noexcept
      {
	return {tape_ + offset_level};
      }
      void set_level (level_t v) noexcept
      {
	return misaligned_store<level_t> (tape_ + offset_level, v);
      }

      // from usearch
      static constexpr std::size_t node_head_bytes_() noexcept
      {
	return sizeof (slot_id_t) + sizeof (level_t);
      }
  };

  template <class ID_TRAITS>
  class neighbors_ref_t
  {
    protected:
      byte_t *tape_ {};

      static constexpr std::size_t shift (std::size_t i = 0) noexcept
      {
	return sizeof (neighbors_count_t) + sizeof (slot_id_t) * i;
      }

    public:
      using slot_id_t = typename ID_TRAITS::slot_id_t;

      explicit neighbors_ref_t (byte_t *tape) noexcept : tape_ (tape) {}
      byte_t *tape() const noexcept
      {
	return tape_;
      }
      explicit operator bool() const noexcept
      {
	return tape_;
      }

      neighbors_ref_t() = default;
      neighbors_ref_t (neighbors_ref_t const &) = default;
      neighbors_ref_t &operator= (neighbors_ref_t const &) = default;

      std::size_t size() const noexcept
      {
	return misaligned_load<neighbors_count_t> (tape_);
      }
      void clear() noexcept
      {
	neighbors_count_t n = misaligned_load<neighbors_count_t> (tape_);
	std::memset (tape_, 0, shift (n));
	misaligned_store<neighbors_count_t> (tape_, 0);
      }
      void push_back (slot_id_t slot) noexcept
      {
	neighbors_count_t n = misaligned_load<neighbors_count_t> (tape_);
	misaligned_store<slot_id_t> (tape_ + shift (n), slot);
	misaligned_store<neighbors_count_t> (tape_, n + 1);
      }

      slot_id_t at (std::size_t index) const noexcept
      {
	return misaligned_load<slot_id_t> (tape_ + shift (index));
      }

      template <typename allow_slot_at> std::size_t erase_if (allow_slot_at &&allow_slot) noexcept
      {
	std::size_t old_count = misaligned_load<neighbors_count_t> (tape_);
	std::size_t removed_count = 0;
	for (std::size_t i = 0; i < old_count; ++i)
	  {
	    slot_id_t slot = misaligned_load<slot_id_t> (tape_ + shift (i));
	    if (allow_slot (slot))
	      {
		removed_count++;
	      }
	    else
	      {
		misaligned_store<slot_id_t> (tape_ + shift (i - removed_count), slot);
	      }
	  }
	misaligned_store<neighbors_count_t> (tape_, old_count - removed_count);
	return removed_count;
      }
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
      using slot_id_t  = typename traits::slot_id_t;

      using pinned_t   = pinned_block<Traits>;
      using storage_t   = storage<traits>;
      using root_type   = root_t<traits>;
      using node_type   = node_t<traits>;
      using neighbors_ref_type = neighbors_ref_t<traits>;

      using key_id_t    = OID;

      struct context_t
      {
	top_candidates_t m_top_candidates;
	top_candidates_t m_top_for_refine;
	next_candidates_t m_next_candidates;
	visited_set_t m_visits;
	std::default_random_engine m_level_generator;

	void clear_candidates ()
	{
	  m_top_candidates.clear ();
	  m_top_for_refine.clear ();
	  m_next_candidates = {};
	}
      };

      algo (const hnsw_build_params &build_params);

      add_result_t add (const OID &oid, const float *vector, const std::size_t expansion);
      search_result_t search (const float *query, const std::size_t k, const std::size_t expansion);

      void set_storage (storage_t *storage) noexcept
      {
	m_storage = storage;
      }

    protected:

      void search_for_one_ (const float *query, const OID &start_oid, const level_t begin_level, const level_t end_level,
			    OID &out);

      int search_to_insert_ (const float *query, const OID &start_oid, const level_t level, const std::size_t top_limit);
      int search_to_find_in_base_ (const float *query, const OID &start_oid, const std::size_t expansion);

      void form_links_to_closest_ (const pinned_t &new_slot, const level_t level, candidates_view_t &out);
      int form_reverse_links_ (const pinned_t &new_slot, const float *value, candidates_view_t &new_neighbors, level_t level);

      level_t choose_random_level_ (std::default_random_engine &generator, double inverse_log_connectivity);

      void refine_ (std::size_t needed, top_candidates_t &top, candidates_view_t &out) const;

      distance_t compute_distance_ (const float *v1, const float *v2) const
      {
	return metric_table[static_cast<size_t> (m_metric)] (v1, v2, m_dimension);
      }

      // helpers
      neighbors_ref_type get_neighbors (const node_type &node_at, const level_t level)
      {
	return neighbors_ref_type (node_at.neighbors_tape() + node_neighbors_bytes_ (level));
      }

      inline std::size_t node_neighbors_bytes_ (level_t level) const noexcept
      {
	std::size_t neighbors_byte = m_connectivity * sizeof (slot_id_t) + sizeof (neighbors_count_t);
	return neighbors_byte * (level);
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
  }

  template <typename Traits>
  add_result_t
  algo<Traits>::add (const OID &key, const float *vector, const std::size_t expansion)
  {
    add_result_t result;

    m_context.clear_candidates();
    top_candidates_t &top = m_context.m_top_candidates;
    next_candidates_t &next = m_context.m_next_candidates;

    // TODO: now, connectivity_base is not considered.
    // std::size_t connecitvity_max = m_connectivity;
    std::size_t top_limit = std::max (m_connectivity + 1, expansion);
    if (!top.reserve (top_limit))
      {
	assert (false);
	return {ER_FAILED, OID_INITIALIZER};
      }


    level_t curr_max_level;
    level_t new_target_level;
    slot_id_t entry;

    pinned_t root_block = m_storage->get_root (lock_mode::shared);
    {
      root_type root_node = root_type (root_block.data());
      curr_max_level = root_node.get_level(); // get max_level from root page
      new_target_level = choose_random_level_ (m_context.m_level_generator, m_inverse_log_connectivity);
      slot_id_t entry_slot = root_node.get_entry();
      if (new_target_level > MAX_LEVELS)
	{
	  // TODO: for optimzation, if new_target_level is greater than max_level, we can just use max_level
	  new_target_level = MAX_LEVELS;
	}

      //
      slot_id_t new_slot = m_storage->add_node (key, new_target_level);
      slot_id_t new_vec_slot = m_storage->add_vector (key, vector);
      //

      if (m_storage->is_empty())
	{
	  pinned_t promoted_root = m_storage->promote_root (root_block);
	  root_node = root_type (promoted_root.data());
	  root_node.set_entry (new_slot);
	  root_node.set_level (new_target_level);
	  return result;
	}
    }

    if (new_target_level <= curr_max_level)
      {
	// unlock root
	root_block.release();
      }

    {
      pinned_t entry_block = m_storage->get_node (entry_slot, lock_mode::shared);
      OID closest_oid = node_type (entry_block.data()).get_key();
      search_for_one_ (vector, closest_oid, curr_max_level, new_target_level, closest_oid);

      level_t level = (std::min) (new_target_level, curr_max_level);

      pinned_t new_node_blk = m_storage->get_node (new_slot, lock_mode::shared);
      node_type new_node = node_type (new_node_blk.data());
      while (true)
	{
	  (void) search_to_insert_ (vector, closest_oid, level, top_limit);

	  candidates_view_t closest_view;
	  {
	    neighbors_ref_type neighbors = get_neighbors (new_node, level);
	    neighbors.clear();

	    form_links_to_closest_ (new_node_blk, level, closest_view);
	    closest_oid = closest_view[0].key;
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
	// TODO: implement promotion
	root_node.set_entry (new_slot);
	root_node.set_level (new_target_level);
      }

    return result;
  }

  template <typename Traits>
  search_result_t
  algo<Traits>::search (const float *query, const std::size_t k, const std::size_t expansion)
  {
    search_result_t result;
    if (k ==0)
      {
	return result;
      }

    top_candidates_t &top = m_context.m_top_candidates;
    next_candidates_t &next = m_context.m_next_candidates;
    std::size_t expansion_size = std::max (k, expansion);

    if (!top.reserve (expansion_size))
      {
	// TODO: error handling
	assert (false);
      }

    pinned_t root_block = m_storage->get_root (lock_mode::shared);
    root_type root_node = root_type (root_block.data());

    pinned_t entry_block = m_storage->get_node (root_node.get_entry(), lock_mode::shared);
    node_type entry_node = node_type (entry_block.data());

    OID closest_oid;
    search_for_one_ (query, entry_node.get_key(), root_node.get_level (), 0, closest_oid);

    if (search_to_find_in_base_ (query, closest_oid, expansion_size) != NO_ERROR)
      {
	// TODO: error handling
	assert (false);
      }

    top.sort_ascending();
    top.shrink (k);

    result.results = candidates_view_t (top.data(), top.data() + top.size());
    return result;
  }

  template <typename Traits>
  void
  algo<Traits>::search_for_one_ (const float *query, const OID &start_oid, const level_t begin_level,
				 const level_t end_level, OID &out)
  {
    visited_set_t &visits = m_context.m_visits;
    visits.clear ();

    OID closest_oid = start_oid;
    distance_t closest_dist;
    {
      pinned_t closest_vector_blk = m_storage->get_vector (closest_oid, lock_mode::shared);
      closest_dist = compute_distance_ (query, reinterpret_cast<float *> (closest_vector_blk.data()));
    }

    for (level_t level = begin_level; level > end_level; --level)
      {
	bool changed = false;
	do
	  {
	    changed = false;

	    pinned_t closest_node_blk = m_storage->get_node (closest_oid, lock_mode::shared);
	    node_type closest_node = node_type (closest_node_blk.data());
	    neighbors_ref_type neighbors = get_neighbors (closest_node, level);
	    for (std::size_t i = 0; i < neighbors.size (); ++i)
	      {
		slot_id_t neighbor_id = neighbors.at (i);
		node_type neighbor_node = m_storage->get_node (neighbor_id);
		OID neighbor_oid = neighbor_node.get_key();

		distance_t candidate_dist;
		{
		  pinned_t neighbor_vector_blk = m_storage->get_vector (neighbor_oid, lock_mode::shared);
		  candidate_dist = compute_distance_ (query, reinterpret_cast<float *> (neighbor_vector_blk.data()));
		}

		if (candidate_dist < closest_dist)
		  {
		    closest_dist = candidate_dist;
		    closest_oid = neighbor_oid;
		    changed = true;
		  }
	      }
	  }
	while (changed);
      }

    out = closest_oid;
  }


  template <typename Traits>
  int
  algo<Traits>::search_to_insert_ (const float *query, const OID &start_oid, const level_t level,
				   const std::size_t top_limit)
  {
    next_candidates_t &next = m_context.m_next_candidates;
    top_candidates_t &top = m_context.m_top_candidates;
    visited_set_t &visits = m_context.m_visits;

    m_context.clear_candidates();

    distance_t radius;
    {
      pinned_t start_vector_blk = m_storage->get_vector (start_oid, lock_mode::shared);
      radius = compute_distance_ (query, reinterpret_cast<float *> (start_vector_blk.data()));
    }

    next.push (candidate_t (-radius, start_oid));
    top.insert_reserved (candidate_t (radius, start_oid));
    visits.insert (start_oid);

    while (!next.empty ())
      {
	candidate_t candidacy = next.top ();
	if ((-candidacy.distance) > radius && top.size () < top_limit)
	  {
	    break;
	  }

	next.pop ();

	OID candidate_oid = candidacy.key;
	pinned_t candidate_node_blk = m_storage->get_node (candidate_oid, lock_mode::shared);
	node_type candidate_node = node_type (candidate_node_blk.data());
	neighbors_ref_type candidate_neighbors = get_neighbors (candidate_node, level);
	for (std::size_t i = 0; i < candidate_neighbors.size (); ++i)
	  {
	    slot_id_t successor_id = candidate_neighbors.at (i);
	    pinned_t successor_node_blk = m_storage->get_node (successor_id, lock_mode::shared);
	    node_type successor_node = node_type (successor_node_blk.data());
	    OID successor_oid = successor_node.get_key();

	    // TODO: refactor the following block
	    bool is_visited = (visits.find (successor_oid) != visits.end ());
	    if (is_visited)
	      {
		continue;
	      }
	    else
	      {
		visits.insert (successor_oid);
	      }


	    distance_t sucessor_dist;
	    {
	      pinned_t successor_vector_blk = m_storage->get_vector (successor_oid, lock_mode::shared);
	      sucessor_dist = compute_distance_ (query, reinterpret_cast<float *> (successor_vector_blk.data()));
	    }

	    if (top.size () < top_limit || sucessor_dist < radius)
	      {
		next.push (candidate_t (-sucessor_dist, successor_oid));
		top.insert_reserved (candidate_t (sucessor_dist, successor_oid));
		radius = top.top().distance;
	      }
	  }
      }

    return NO_ERROR;
  }

  template <typename Traits>
  int
  algo<Traits>::search_to_find_in_base_ (const float *query, const OID &start_oid, const std::size_t expansion)
  {
    next_candidates_t &next = m_context.m_next_candidates;
    top_candidates_t &top = m_context.m_top_candidates;
    visited_set_t &visits = m_context.m_visits;
    std::size_t top_limit = expansion;

    m_context.clear_candidates();

    OID closest_oid = start_oid;

    distance_t radius;
    {
      pinned_t closest_vector_blk = m_storage->get_vector (closest_oid, lock_mode::shared);
      radius = compute_distance_ (query, reinterpret_cast<float *> (closest_vector_blk.data()));
    }

    next.push (candidate_t (-radius, closest_oid));
    top.insert_reserved ({radius, closest_oid});
    visits.insert (closest_oid);

    while (!next.empty())
      {
	candidate_t candidacy = next.top();
	if ((-candidacy.distance) > radius && top.size() == top_limit)
	  {
	    break;
	  }

	next.pop ();

	OID candidacy_oid = candidacy.key;
	pinned_t candidacy_node_blk = m_storage->get_node (candidacy_oid, lock_mode::shared);
	node_type candidacy_node = node_type (candidacy_node_blk.data());
	neighbors_ref_type candidate_neighbors = get_neighbors (candidacy_node, 0);

	for (std::size_t i = 0; i < candidate_neighbors.size (); ++i)
	  {
	    slot_id_t successor_id = candidate_neighbors.at (i);

	    pinned_t successor_node_blk = m_storage->get_node (successor_id, lock_mode::shared);
	    node_type successor_node = node_type (successor_node_blk.data());
	    OID successor_oid = successor_node.get_key();

	    // TODO: refactor the following block
	    bool is_visited = (visits.find (successor_oid) != visits.end ());
	    if (is_visited)
	      {
		continue;
	      }
	    else
	      {
		visits.insert (successor_oid);
	      }

	    distance_t sucessor_dist;
	    {
	      pinned_t successor_vector_blk = m_storage->get_vector (successor_oid, lock_mode::shared);
	      sucessor_dist = compute_distance_ (query, reinterpret_cast<float *> (successor_vector_blk.data()));
	    }

	    if (top.size() < expansion || sucessor_dist < radius)
	      {
		next.push (candidate_t (-sucessor_dist, successor_oid));
		top.insert_reserved (candidate_t (sucessor_dist, successor_oid));
		radius = top.top().distance;
	      }
	  }
      }

    return NO_ERROR;
  }

  template <typename Traits>
  void
  algo<Traits>::form_links_to_closest_ (const pinned_t &new_node_blk, const level_t level, candidates_view_t &top_view)
  {
    top_candidates_t &top = m_context.m_top_candidates;
    refine_ (m_connectivity,top, top_view);

    // outgoing links from new node
    node_type new_node = node_type (new_node_blk.data());
    neighbors_ref_type new_neighbors = get_neighbors (new_node, level);
    for (std::size_t i = 0; i != top_view.size(); i++)
      {
	pinned_t top_view_node_blk = m_storage->get_node (top_view[i].key, lock_mode::shared);
	new_neighbors.push_back (top_view_node_blk.get_id ());
      }
  }

  template <typename Traits>
  int
  algo<Traits>::form_reverse_links_ (const pinned_t &new_node_blk, const float *value, candidates_view_t &new_neighbors,
				     level_t level)
  {
    node_type new_node = node_type (new_node_blk.data());

    for (auto n : new_neighbors)
      {
	OID close_oid = n.key;
	if (close_oid == new_node.get_key())
	  {
	    continue;
	  }

	// TODO: exclusive??
	pinned_t close_node_blk = m_storage->get_node (close_oid, lock_mode::shared);
	node_type close_node = node_type (close_node_blk.data());
	neighbors_ref_type close_header = get_neighbors (close_node, level);
	if (close_header.size () < m_connectivity)
	  {
	    close_header.push_back (new_node_blk.get_id ());
	    continue;
	  }

	m_context.m_top_for_refine = {}; // clear
	top_candidates_t &top_for_refine = m_context.m_top_for_refine;

	distance_t dist;
	{
	  pinned_t close_vector_blk = m_storage->get_vector (close_oid, lock_mode::shared);
	  dist = compute_distance_ (value, reinterpret_cast<float *> (close_vector_blk.data()));
	}

	top_for_refine.insert_reserved (candidate_t (dist, close_oid));
	for (std::size_t i = 0; i < close_header.size (); i++)
	  {
	    slot_id_t successor_id = close_header.at (i);
	    pinned_t successor_node_blk = m_storage->get_node (successor_id, lock_mode::shared);
	    node_type successor_node = node_type (successor_node_blk.data());
	    OID successor_oid = successor_node.get_key();

	    distance_t dist;
	    {
	      pinned_t close_vector_blk = m_storage->get_vector (close_oid, lock_mode::shared);
	      pinned_t successor_vector_blk = m_storage->get_vector (successor_oid, lock_mode::shared);

	      float *close_oid_vec = reinterpret_cast<float *> (close_vector_blk.data());
	      float *successor_oid_vec = reinterpret_cast<float *> (successor_vector_blk.data());
	      dist = compute_distance_ (close_oid_vec, successor_oid_vec);
	    }

	    top_for_refine.insert_reserved (candidate_t (dist, successor_oid));
	  }

	// remove all neighbors from close_header
	close_header.clear();
	candidates_view_t top_view;
	(void) refine_ (m_connectivity, top_for_refine, top_view);
	for (std::size_t i = 0; i != top_view.size (); i++)
	  {
	    pinned_t top_view_node_blk = m_storage->get_node (top_view[i].key, lock_mode::shared);
	    node_type top_view_node = node_type (top_view_node_blk.data());
	    close_header.push_back (top_view_node.get_id());
	  }
      }

    return NO_ERROR;
  }

  template <typename Traits>
  void
  algo<Traits>::refine_ (std::size_t needed, top_candidates_t &top, candidates_view_t &out) const
  {
    out = {};

    candidate_t *top_data = top.data();
    std::size_t const top_count = top.size();
    if (top_count < needed)
      {
	out = candidates_view_t (top_data, top_data + top_count);
	return;
      }

    top.sort_ascending();

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

	    distance_t inter_result_dist;
	    {
	      pinned_t candidate_vector_blk = m_storage->get_vector (candidate.key, lock_mode::shared);
	      pinned_t submitted_vector_blk = m_storage->get_vector (submitted.key, lock_mode::shared);

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
    out = candidates_view_t (top_data, top_data + submitted_count);
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
