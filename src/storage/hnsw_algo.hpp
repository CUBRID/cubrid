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
  };

  struct less_candidate_t
  {
    bool operator() (const candidate_t &a, const candidate_t &b) const
    {
      return a.distance < b.distance;
    }
  };

  struct add_result_t
  {
    int error {NO_ERROR};
    OID result;
  };

  struct search_result_t
  {
    int error {NO_ERROR};
    std::vector <candidate_t> results {};
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
      using block_id_t  = typename traits::block_id_t;
      using storage_t   = cubhnsw::storage_t<traits>;
      using root_type   = cubhnsw::root_t<traits>;
      using node_type   = cubhnsw::node_t<traits>;
      using neighbors_ref_type = cubhnsw::neighbors_ref_t<traits>;

      using key_id_t    = OID;


      using visited_set_t = std::unordered_set<OID>;
      using candidates_view_t = std::vector<candidate_t>;
      using top_candidates_t = std::priority_queue<candidate_t, std::vector<candidate_t>, less_candidate_t>;
      using next_candidates_t = std::priority_queue<candidate_t, std::vector<candidate_t>, less_candidate_t>;

      struct context_t
      {
	top_candidates_t m_top_candidates;
	top_candidates_t m_top_for_refine;
	next_candidates_t m_next_candidates;
	visited_set_t m_visits;
	std::default_random_engine m_level_generator;

	void clear_candidates ()
	{
	  m_top_candidates = {};
	  m_top_for_refine = {};
	  m_next_candidates = {};
	}
      };

      algo (cubthread::entry *thread_p, const hnsw_build_params &build_params);

      add_result_t add (const OID &oid, const float *vector);
      search_result_t search (const float *query, const std::size_t k);

    protected:

      void search_for_one_ (const float *query, const OID &start_oid, const level_t begin_level, const level_t end_level,
			    OID &out);

      int search_to_insert_ (const float *query, const OID &start_oid, const level_t level, const std::size_t top_limit);
      int search_to_find_in_base_ (const float *query, const OID &start_oid, const std::size_t expansion);

      void form_links_to_closest_ (const block_id_t &new_slot, const level_t level, candidates_view_t &out);
      int form_reverse_links_ (block_id_t new_slot, const float *value, candidates_view_t &new_neighbors, level_t level);

      level_t choose_random_level_ (std::default_random_engine &generator, double inverse_log_connectivity);

      void refine_ (std::size_t needed, top_candidates_t &top, candidates_view_t &out) const;

      distance_t compute_distance_ (const float *v1, const float *v2) const
      {
	return metric_table[static_cast<size_t> (m_metric)] (v1, v2, m_dimension);
      }

      // variables
      context_t m_context;
      std::unique_ptr<storage_t> m_storage;

      // from build_params
      vector_distance_metric_t m_metric;
      std::size_t m_dimension;
      std::size_t m_connectivity;

      // precomputed
      double m_inverse_log_connectivity;
  };

  // =====================================================================
  // algo class implementation
  // =====================================================================

  template <typename Traits>
  algo<Traits>::algo (cubthread::entry *thread_p, const hnsw_build_params &build_params)
    : m_dimension ((size_t) build_params.dimension), m_connectivity (build_params.ef_construction)
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
    m_inverse_log_connectivity = 1.0 / std::log (static_cast<double> (build_params.ef_construction));

    BTID giid = BTID_INITIALIZER;
    m_storage = std::make_unique<cubhnsw::memory_storage_t> (thread_p, giid, build_params);
  }


  template <typename Traits>
  add_result_t
  algo<Traits>::add (const OID &key, const float *vector)
  {
    add_result_t result;
    m_context.clear_candidates();

    // TODO: now, connectivity_base is not considered.
    // std::size_t connecitvity_max = m_connectivity;
    std::size_t top_limit = m_connectivity + 1;

    root_t root_node = m_storage->get_root();

    level_t curr_max_level = root_node.get_level(); // get max_level from root page
    level_t new_target_level = choose_random_level_ (m_context.m_level_generator, m_inverse_log_connectivity);
    if (new_target_level > MAX_LEVELS)
      {
	// For optimzation, if new_target_level is greater than max_level, we can just use max_level
	new_target_level = MAX_LEVELS;
      }

    //
    node_type new_node = m_storage->make_node (key, new_target_level);
    block_id_t new_slot = m_storage->add_node (new_node);
    block_id_t new_vec_slot = m_storage->add_vector (key, vector);
    //

    if (m_storage->is_empty())
      {
	root_node.set_entry (new_slot);
	root_node.set_level (new_target_level);
	return result;
      }

    if (new_target_level > curr_max_level)
      {
	// promotion required
	// TODO: implement promotion
      }

    block_id_t entry = root_node.get_entry();
    node_type entry_node = m_storage->get_node (entry);
    {
      // from the second element
      OID closest_oid;
      search_for_one_ (vector, entry_node.get_key(), curr_max_level, new_target_level, closest_oid);

      level_t level = (std::min) (new_target_level, curr_max_level);
      while (true)
	{
	  (void) search_to_insert_ (vector, closest_oid, level, top_limit);

	  candidates_view_t closest_view;
	  {
	    neighbors_ref_type neighbors = m_storage->get_neighbors (new_slot, level);
	    neighbors.clear();

	    form_links_to_closest_ (new_slot, level, closest_view);
	    closest_oid = closest_view[0].key;
	  }
	  form_reverse_links_ (new_slot, vector, closest_view, level);
	  if (level == 0)
	    {
	      break;
	    }

	  --level;
	}
    }

    return result;
  }


  template <typename Traits>
  search_result_t
  algo<Traits>::search (const float *query, const std::size_t k)
  {
    search_result_t result;
    if (k ==0)
      {
	return result;
      }

#if 0
    // TODO: get from root page
    level_t current_max_level = 16;

    // TODO
    OID entry_oid;
    OID_SET_NULL (&entry_oid);

    OID closest_oid = search_for_one_ (
			      query, entry_oid, current_max_level, 0);

    if (search_to_find_in_base_ (query, closest_oid) != NO_ERROR)
      {
	// TODO: error handling
      }
#endif

    return result;
  }

  template <typename Traits>
  void
  algo<Traits>::search_for_one_ (const float *query, const OID &start_oid, const level_t begin_level,
				 const level_t end_level, OID &out)
  {
// asserts
    assert (begin_level >= end_level);
    visited_set_t &visits = m_context.m_visits;
    visits.clear ();

    OID closest_oid = start_oid;
    block_id_t closest_vec_id = m_storage->vector_at (closest_oid);
    distance_t closest_dist = compute_distance_ (query, m_storage->get_vector (closest_vec_id));
    for (level_t level = begin_level; level > end_level; --level)
      {
	bool changed = false;
	do
	  {
	    changed = false;

	    block_id_t closest_node_at = m_storage->node_at (closest_oid);
	    neighbors_ref_type neighbors = m_storage->get_neighbors (closest_node_at, level);
	    for (std::size_t i = 0; i < neighbors.size (); ++i)
	      {
		block_id_t neighbor_id = neighbors.at (i);
		node_type neighbor_node = m_storage->get_node (neighbor_id);
		OID neighbor_oid = neighbor_node.get_key();

		distance_t candidate_dist = compute_distance_ (query, m_storage->get_vector (m_storage->vector_at (neighbor_oid)));
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
    distance_t radius = compute_distance_ (query, m_storage->get_vector (m_storage->vector_at (start_oid)));

    next_candidates_t &next = m_context.m_next_candidates;
    top_candidates_t &top = m_context.m_top_candidates;
    visited_set_t &visits = m_context.m_visits;

    next.push (candidate_t (-radius, start_oid));
    top.push (candidate_t (radius, start_oid));
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

	// pgbuf_fix

	// get neighbors
	neighbors_ref_type candidate_neighbors = m_storage->get_neighbors (m_storage->node_at (candidate_oid), level);
	for (std::size_t i = 0; i < candidate_neighbors.size (); ++i)
	  {
	    block_id_t successor_id = candidate_neighbors.at (i);
	    node_type successor_node = m_storage->get_node (successor_id);
	    OID successor_oid = successor_node.get_key();

	    if (visits.find (successor_oid) != visits.end ())
	      {
		// already visited
		continue;
	      }

	    distance_t sucessor_dist = compute_distance_ (query, m_storage->get_vector (m_storage->vector_at (successor_oid)));
	    if (top.size () < top_limit || sucessor_dist < radius)
	      {
		next.push (candidate_t (-sucessor_dist, successor_oid));
		top.push (candidate_t (sucessor_dist, successor_oid));
		radius = top.top().distance;
	      }
	  }
	// pgbuf_unfix
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
    distance_t radius = compute_distance_ (query, m_storage->get_vector (closest_oid));
    next.push ({-radius, closest_oid});
    visits.insert (closest_oid);

    top.push ({radius, closest_oid});

    while (!next.empty())
      {
	candidate_t candidacy = next.top();
	if ((-candidacy.distance) > radius && top.size() == top_limit)
	  {
	    break;
	  }

	next.pop ();

	neighbors_ref_type candidate_neighbors = m_storage->get_neighbors (m_storage->node_at (candidacy.key), 0);
	for (int i = 0; i < candidate_neighbors.size (); ++i)
	  {
	    block_id_t successor_id = candidate_neighbors.at (i);
	    node_type successor_node = m_storage->get_node (successor_id);
	    OID successor_oid = successor_node.get_key();
	    if (visits.find (successor_oid) != visits.end ())
	      {
		continue;
	      }

	    distance_t sucessor_dist = compute_distance_ (query, m_storage->get_vector (successor_oid));
	    if (top.size() < expansion || sucessor_dist < radius)
	      {
		next.push (candidate_t (-sucessor_dist, successor_oid));
		top.push (candidate_t (sucessor_dist, successor_oid));
		radius = top.top().distance;
	      }
	  }
      }

    return NO_ERROR;
  }

  template <typename Traits>
  void
  algo<Traits>::form_links_to_closest_ (const block_id_t &new_slot, const level_t level, candidates_view_t &top_view)
  {
    top_candidates_t &top = m_context.m_top_candidates;
    refine_ (m_connectivity,top, top_view);

// outgoing links from new node
    neighbors_ref_type new_neighbors = m_storage->get_neighbors (new_slot, level);
    for (std::size_t i = 0; i != top_view.size(); i++)
      {
	new_neighbors.push_back (m_storage->node_at (top_view[i].key));
      }
  }

  template <typename Traits>
  int
  algo<Traits>::form_reverse_links_ (block_id_t new_slot, const float *value, candidates_view_t &new_neighbors,
				     level_t level)
  {
    node_type new_node = m_storage->get_node (new_slot);
    for (auto n : new_neighbors)
      {
	OID close_oid = n.key;
	if (close_oid == new_node.get_key())
	  {
	    continue;
	  }

	neighbors_ref_type close_header = m_storage->get_neighbors (m_storage->node_at (close_oid), level);
	if (close_header.size () < m_connectivity)
	  {
	    close_header.push_back (new_slot);
	    continue;
	  }

	m_context.m_top_for_refine = {}; // clear
	top_candidates_t &top_for_refine = m_context.m_top_for_refine;

	distance_t dist = compute_distance_ (value, m_storage->get_vector (m_storage->vector_at (close_oid)));
	top_for_refine.push (candidate_t (dist, close_oid));
	for (std::size_t i = 0; i < close_header.size (); i++)
	  {
	    block_id_t successor_id = close_header.at (i);
	    node_type successor_node = m_storage->get_node (successor_id);
	    OID successor_oid = successor_node.get_key();
	    top_for_refine.push (candidate_t (compute_distance_ (m_storage->get_vector (m_storage->vector_at (close_oid)),
					      m_storage->get_vector (m_storage->vector_at (successor_oid))), successor_oid));
	  }

	// remove all neighbors from close_header
	close_header.clear();
	candidates_view_t top_view;
	(void) refine_ (m_connectivity, top_for_refine, top_view);
	for (std::size_t i = 0; i != top_view.size (); i++)
	  {
	    close_header.push_back (m_storage->node_at (top_view[i].key));
	  }
      }

    return NO_ERROR;
  }

  template <typename Traits>
  void
  algo<Traits>::refine_ (std::size_t needed, top_candidates_t &top, candidates_view_t &out) const
  {
    out = {};
    if (top.size() < needed)
      {
	while (!top.empty())
	  {
	    out.push_back (top.top());
	    top.pop();
	  }
	return;
      }


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