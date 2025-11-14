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
  using level_t = uint16_t;

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
  // graph
  // =====================================================================


  struct graph_root
  {
    hnsw_build_params build_params;
    level_t entry_level;
    VPID layers[MAX_LEVELS];

    graph_root (const hnsw_build_params &build_params)
      : build_params (build_params), entry_level (-1)
    {
      for (int i = 0; i < MAX_LEVELS; i++)
	{
	  layers[i] = VPID_INITIALIZER;
	}
    }
  };


  struct graph_node_header
  {
    uint16_t level;
    VPID sibling_vpid; // next page id in the same level
  };

  // memory layout
  struct graph_neighbor
  {
    uint16_t n_neighbors;
    OID *neighbors;
  };

  struct graph_node
  {
    OID key;
    float *vecs;
    uint16_t level; // max level of the node
    graph_neighbor *neighbors;
  };

  template <class VPID>
  struct graph_node_block
  {
    VPID id;
    uint16_t n_nodes;
    graph_node *nodes;
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

  using visited_set_t = std::unordered_set<OID>;
  using candidates_view_t = std::vector<candidate_t>;
  using top_candidates_t = std::priority_queue<candidate_t, std::vector<candidate_t>, less_candidate_t>;
  using next_candidates_t = std::priority_queue<candidate_t, std::vector<candidate_t>, less_candidate_t>;

  class algo
  {
    public:

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
      OID search_for_one_ (const float *query, const OID &closest_oid, const level_t begin_level, const level_t end_level);
      int search_to_insert_ (const float *query, const OID &start_oid, const level_t level, const std::size_t top_limit);
      int search_to_find_in_base_ (const float *query, const OID &closest_oid, const std::size_t expansion);

      candidates_view_t form_links_to_closest_ (const OID &new_slot, const level_t level);
      int form_reverse_links_ (OID new_slot_oid, float *value, candidates_view_t &new_neighbors, level_t level);

      level_t choose_random_level_ (std::default_random_engine &generator, double inverse_log_connectivity);

      candidates_view_t refine_ (top_candidates_t &top) const;

      distance_t compute_distance_ (const float *v1, const float *v2) const
      {
	return metric_table[static_cast<size_t> (m_metric)] (v1, v2, m_dimension);
      }

      // variables
      context_t m_context;
      storage_t m_storage;

      vector_distance_metric_t m_metric;
      std::size_t m_dimension;
      std::size_t m_connectivity;
      double m_inverse_log_connectivity;
  };

  // =====================================================================
  // algo class implementation
  // =====================================================================

  algo::algo (cubthread::entry *thread_p, const hnsw_build_params &build_params)
    : m_storage (thread_p), m_dimension ((size_t) build_params.dimension), m_connectivity (build_params.ef_construction)
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

    // precomputed
    m_inverse_log_connectivity = 1.0 / std::log (static_cast<double> (build_params.ef_construction));
  }

  add_result_t
  algo::add (const OID &oid, const float *vector)
  {
    add_result_t result;
    m_context.clear_candidates();

    // TODO: get_root_node
    std::size_t top_limit = m_connectivity;

    level_t curr_max_level = root_node.entry_level; // get max_level from root page
    level_t new_target_level = choose_random_level_ (m_context.m_level_generator, m_inverse_log_connectivity);

    if (new_target_level > MAX_LEVELS)
      {
	// For optimzation, if new_target_level is greater than max_level, we can just use max_level
	new_target_level = MAX_LEVELS;
      }

    // get entry_oid from root
    OID entry_oid;
    OID_SET_NULL (&entry_oid);

    if (OID_ISNULL (&entry_oid) || new_target_level > curr_max_level)
      {
	// first element or new_target_level is greater than curr_max_level
	// it requires promotion of write latch on the root page
      }
    else
      {
	// from the second element

	// TODO: implement storage
	entry_oid = m_storage.get_entry_oid ();

	OID closest_oid = search_for_one_ (vector, entry_oid, curr_max_level, new_target_level);
	for (level_t level = (std::min) (new_target_level, curr_max_level); level >= 0; --level)
	  {
	    (void) search_to_insert_ (vector, closest_oid, level, top_limit);
	    candidates_view_t closest_view;
	    {
	      new_node.clear_neighbors ();
	      closest_view = form_links_to_closests_ (new_node, new_oid, level);
	      closest_oid = closest_view[0].key;
	    }
	    form_reverse_links_ (new_node, new_oid, closest_view, level);
	  }
      }

    return result;
  }

  search_result_t
  algo::search (const float *query, const std::size_t k)
  {
    int error = NO_ERROR;
    search_result_t result;
    if (k ==0)
      {
	return result;
      }

    // TODO: get from root page
    level_t current_max_level = 16;

    // TODO
    OID entry_oid;
    OID_SET_NULL (&entry_oid);

    OID closest_oid = search_for_one_ (
			      query, entry_oid, current_max_level, 0);

    error = search_to_find_in_base_ (query, closest_oid);

    return result;
  }


  OID
  algo::search_for_one_ (const float *query, const OID &start_oid, const level_t begin_level, const level_t end_level)
  {
// asserts
    assert (begin_level >= end_level);

    OID closest_oid = start_oid;
    distance_t closest_dist = compute_distance_ (query, m_storage.vector_at_ (closest_oid));
    for (level_t level = begin_level; level > end_level; --level)
      {
	bool changed = false;
	do
	  {
	    changed = false;

	    neighbors_ref_t neighbors = neighbors_non_base_ (closest_oid, level);

	    for (int i = 0; i < neighbors.size (); ++i)
	      {
		OID neighbor_oid = neighbors.at (i);
		distance_t candidate_dist = compute_distance_ (query, m_storage.vector_at_ (neighbor_oid));
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

    return closest_oid;
  }

  int
  algo::search_to_insert_ (const float *query, const OID &start_oid, const uint16_t level, const std::size_t top_limit)
  {
    distance_t radius = compute_distance_ (query, m_storage.vector_at_ (start_oid));

    next_candidates_t &next = m_context.m_next_candidates;
    top_candidates_t &top = m_context.m_top_candidates;
    visited_set_t &visits = m_context.m_visits;

    next.push (candidate_t {-radius, start_oid});
    top.push (candidate_t {radius, start_oid});
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
	neighbors_ref_t candidate_neighbors = neighbors_ (candidate_oid, level);
	for (int i = 0; i < candidate_neighbors.size (); ++i)
	  {
	    OID successor_oid = candidate_neighbors.at (i);

	    if (visits.find (successor_oid) != visits.end ())
	      {
		// already visited
		continue;
	      }

	    distance_t sucessor_dist = compute_distance_ (query, m_storage.vector_at_ (successor_oid));
	    if (top.size () < top_limit || sucessor_dist < radius)
	      {
		next.push (candidate_t {-sucessor_dist, successor_oid});
		top.push (candidate_t {sucessor_dist, successor_oid});
		radius = top.top().distance;
	      }
	  }
	// pgbuf_unfix
      }

    return NO_ERROR;
  }

  int
  algo::search_to_find_in_base_ (const float *query, const OID &start_oid, const std::size_t expansion)
  {
    int error = NO_ERROR;

    next_candidates_t &next = m_context.m_next_candidates;
    top_candidates_t &top = m_context.m_top_candidates;
    visited_set_t &visits = m_context.m_visits;
    std::size_t top_limit = expansion;

    m_context.clear_candidates();

    OID closest_oid = start_oid;
    distance_t radius = compute_distance_ (query, m_storage.vector_at_ (closest_oid));
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

	neighbors_ref_t candidate_neighbors = neighbors_base_ (candidacy.key);
	for (int i = 0; i < candidate_neighbors.size (); ++i)
	  {
	    OID successor_oid = candidate_neighbors.at (i);
	    if (visits.find (successor_oid) != visits.end ())
	      {
		continue;
	      }

	    distance_t sucessor_dist = compute_distance_ (query, m_storage.vector_at_ (successor_oid));
	    if (top.size() < expansion || sucessor_dist < radius)
	      {
		next.push (candidate_t {-sucessor_dist, successor_oid});
		top.push (candidate_t {sucessor_dist, successor_oid});
		radius = top.top().distance;
	      }
	  }
      }

    return NO_ERROR;
  }

  candidates_view_t
  algo::form_links_to_closest_ (const OID &new_slot_oid, const level_t level)
  {
    top_candidates_t &top = m_context.m_top_candidates;
    candidates_view_t top_view = refine_ (top);

// outgoing links from new node
    neighbors_ref_t new_neighbors = neighbors_ (new_slot_oid, level);
    for (std::size_t i = 0; i != top_view.size(); i++)
      {
	new_neighbors.push_back (top_view[i].key);
      }
  }

  int
  algo::form_reverse_links_ (OID new_slot_oid, float *value, candidates_view_t &new_neighbors, level_t level)
  {
    for (auto n : new_neighbors)
      {
	OID close_oid = n.key;
	if (close_oid == new_slot_oid)
	  {
	    continue;
	  }

	neighbors_ref_t close_header = neighbors_ (close_oid, level);
	if (close_header.size () < m_connectivity)
	  {
	    close_header.push_back (new_slot_oid);
	    continue;
	  }

  m_context.m_top_for_refine = {}; // clear
  top_candidates_t& top_for_refine = m_context.m_top_for_refine;

	top_for_refine.push ({compute_distance_ (value, m_storage.vector_at_(close_oid)), new_slot_oid});
	for (int i = 0; i < close_header.size (); i++)
	  {
	    OID successor_oid = close_header.at (i);
	    top_for_refine.push ({compute_distance_ (m_storage.vector_at_(close_oid), m_storage.vector_at_(successor_oid)), successor_oid});
	  }

	// remove all neighbors from close_header
	close_header.clear();
	candidates_view_t top_view = refine_ (top_for_refine);
	for (std::size_t i = 0; i != top_view.size (); i++)
	  {
	    close_header.push_back (top_view[i].key);
	  }
      }

    return NO_ERROR;
  }
}

#endif