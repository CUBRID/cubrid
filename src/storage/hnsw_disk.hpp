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
// hnsw_disk.hpp - disk-based HNSW index
//

#ifndef _HNSW_DISK_HPP_
#define _HNSW_DISK_HPP_

#include "hnsw_api.hpp"

#include "storage_common.h" // RECDES
#include "record_descriptor.hpp"

#include "faiss/utils/distances.h" // faiss

constexpr int MAX_LEVELS = 16;

template <typename T>
inline T rec_read (RECDES &rc, size_t offset)
{
  static_assert (std::is_trivially_copyable_v<T>);
  assert (offset + sizeof (T) <= (size_t)rc.length);

  T out;
  std::memcpy (&out, rc.data + offset, sizeof (T));
  return out;
}

template <typename T>
inline void rec_write (RECDES &rc, size_t offset, const T &value)
{
  static_assert (std::is_trivially_copyable_v<T>);
  assert (offset + sizeof (T) <= (size_t)rc.length);

  std::memcpy (rc.data + offset, &value, sizeof (T));
}

template <typename T>
inline void rec_write_many (RECDES &rc, size_t offset, const T *values, size_t count)
{
  static_assert (std::is_trivially_copyable_v<T>);
  assert (offset + sizeof (T) * count <= (size_t)rc.length);

  std::memcpy (rc.data + offset, values, sizeof (T) * count);
}

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

using Fn = distance_t(*)(const float *, const float *, size_t);

constexpr std::array<Fn, static_cast<size_t>(vector_distance_metric_t::MAX)> table = {
  cubvec_cosine_distance,
  cubvec_l2_distance
};

namespace cubhnsw
{
  using level_t = uint16_t;

  struct root_node
  {
    hnsw_build_params build_params;
    level_t entry_level;
    VPID layers[MAX_LEVELS];

    root_node (const hnsw_build_params &build_params)
      : build_params (build_params), entry_level (-1)
    {
      for (int i = 0; i < MAX_LEVELS; i++)
        {
          layers[i] = VPID_INITIALIZER;
        }
    }

    char*
    at_build_params (RECDES &rc)
    {
      return (char*) rc.data + offsetof(root_node, build_params);
    }

    char*
    at_entry_level (RECDES &rc)
    {
      return (char*) rc.data + offsetof(root_node, entry_level);
    }

    char*
    at_layers_offset (RECDES &rc, level_t level)
    {
      return (char*) rc.data + offsetof(root_node, layers) + level * sizeof(VPID);
    }

    RECDES to_recdes ()
    {
      RECDES rc { DB_PAGESIZE, (int) sizeof (root_node), REC_HOME, (char *) this};
      return rc;
    }
  };

  struct graph_node_page_header
  {
    uint16_t level;
    VPID sibling_vpid; // next page id in the same level
  };

  struct graph_node_page_common
  {
    OID oid;              // unique id for the data
    uint16_t n_neighbors; // number of neighbors
    OID *neighbors;       // array of neighbor ids
  };

  struct graph_node_page_base : public graph_node_page_common
  {
    float *vecs;          // vector data
  };

  using distance_t = double;
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

  struct neighbors_ref_t
  {   
    neighbors_ref_t (OID *neighbors, uint16_t n_neighbors) : m_neighbors (neighbors), m_n_neighbors (n_neighbors) {}

    OID at (uint16_t index) const
    {
      return m_neighbors[index];
    }

    uint16_t size () const
    {
      return m_n_neighbors;
    }
  };

  struct add_result_t
  {
    int error {NO_ERROR};
  };

  struct search_result_t
  {
    int error {NO_ERROR};
    std::vector <candidate_t> results {};
  }

  using visited_set_t = std::unordered_set<OID>;
  using top_candidates_t = std::priority_queue<candidate_t, vector<candidate_t>, less_candidate_t>;
  using next_candidates_t = std::priority_queue<candidate_t, vector<candidate_t>, less_candidate_t>;

  struct context_t
  {
    cubthread::entry *m_thread_p;
    top_candidates_t m_top_candidates;
    top_candidates_t m_top_for_refine;
    next_candidates_t m_next_candidates;
    visited_set_t m_visits;
    std::default_random_engine m_level_generator;

    /* stats*/
    std::size_t iteration_cycles{};
    std::size_t computed_distances{};
    std::size_t computed_distances_in_refines{};
    std::size_t computed_distances_in_reverse_refines{};
  };

  /* this class is modified version of the usearch implementation */
  class algo
  {
    public:

      algo (cubthread::entry *thread_p, size_t dimension, vector_distance_metric_t metric)
      {
        m_context.m_thread_p = thread_p;
        m_dimension = dimension;
        m_metric = metric;
      }

      add_result_t add (const OID &oid, const float *vector);
      int search (const float *query, std::size_t k);

    protected:
      OID search_for_one_ (const float *query, const OID &closest_oid, const level_t begin_level, const level_t end_level);
      int search_to_insert_ (const float *query, const OID &start_oid, const level_t level, const std::size_t top_limit);
      int search_to_find_in_base_ (const float *query, const OID &closest_oid, const std::size_t expansion);

      candidates_view_t form_links_to_closest_ (const OID &new_slot, level_t level);
      int form_reverse_links_ (OID new_slot_oid, float *value, candidates_view_t &new_neighbors, level_t level);

      level_t choose_random_level_ (std::default_random_engine &generator, double inverse_log_connectivity);

      distance_t compute_distance_ (const float *vector, const OID oid);
      distance_t compute_distance_ (const OID oid, const float *vector);
      distance_t compute_distance_ (const float *v1, const float *v2)
      {
        return table[static_cast<size_t>(m_metric)] (v1, v2, m_dimension);
      }
      distance_t compute_distance_ (const OID o1, const OID o2);

      // variables
      context_t m_context;
      vector_distance_metric_t m_metric;
      size_t m_dimension;
  };

  algo::algo (cubthread::entry *thread_p)
  {
    m_context.m_thread_p = thread_p;
  }

  add_result_t
  algo::add (const OID &oid, const float *vector)
    {
    add_result_t result;
    root_node root_node = m_storage.get_root_node ();

    level_t curr_max_level = root_node.entry_level; // get max_level from root page
	  level_t new_target_level = choose_random_level_ (m_context.m_level_generator);

    if (new_target_level > MAX_LEVELS)
      {
        // For optimzation, if new_target_level is greater than max_level, we can just use max_level
        new_target_level = MAX_LEVELS;
      }

    OID entry_oid = NULL_OID;
    graph_node_page new_node;

    if (curr_max_level == -1 || new_target_level > curr_max_level)
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
	      OID new_oid = search_to_insert_ (vector, closest_oid, level);
	      candidates_view_t closest_view;
	      {
    new_node.clear_neighbors ();
		closest_view = form_links_to_closests_ (new_node, new_oid, level);
		closest_oid = closest_view[0].key;
	      }
	      form_reverse_links_ (new_node, new_oid, closest_view, level);
	    }
    }
  }

  class storage_helper
  {
    public:
  
    hnsw_storage_helper (VPID root_vpid, uint16_t dimension, uint16_t connectivity, uint16_t ef_construction,
      DB_VECTOR_DISTANCE_METRIC metric) : m_root_vpid (root_vpid), m_dimension (dimension), m_connectivity (connectivity),
m_ef_construction (ef_construction), m_metric (metric)
   {
// precomute
m_max_level = MAX_LEVELS; // adhoc value
m_inverse_log_connectivity = 1.0 / std::log (static_cast<double> (m_connectivity));
   }

      static int
      create_root (THREAD_ENTRY *thread_p, const BTID *btid, const hnsw_build_params &build_params)
      {
	VPID root_vpid = {btid->root_pageid, btid->vfid.volid};
	PAGE_PTR root_page_ptr = pgbuf_fix (thread_p, &root_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
	if (root_page_ptr == NULL)
	  {
	    ASSERT_ERROR ();
	    return ER_FAILED;
	  }

	scope_exit <std::function<void (void)>> unfix_root_pgptr (
	    [&thread_p, &root_page_ptr] ()
	{
	  pgbuf_unfix_and_init_after_check (thread_p, root_page_ptr);
	});

	// root page structure
	BTREE_ROOT_HEADER dummy_header {};
	RECDES rec { DB_PAGESIZE, (int) sizeof (BTREE_ROOT_HEADER), REC_HOME, (char *) &dummy_header};

	/* insert the root header information into the root page */
	const int DUMMY_HEADER_SLOT_ID = 0;
	if (spage_insert_at (thread_p, root_page_ptr, DUMMY_HEADER_SLOT_ID, &rec) != SP_SUCCESS)
	  {
	    return ER_FAILED;
	  }

	root_node hnsw_root_node (build_params);
	RECDES rec2 { DB_PAGESIZE, (int) sizeof (root_node), REC_HOME, (char *) &hnsw_root_node};

	const int HNSW_HEADER_SLOT_ID = 1;
	if (spage_insert_at (thread_p, root_page_ptr, HNSW_HEADER_SLOT_ID, &rec2) != SP_SUCCESS)
	  {
	    return ER_FAILED;
	  }

	log_sysop_attach_to_outer (thread_p);
	vacuum_log_add_dropped_file (thread_p, &btid->vfid, NULL, VACUUM_LOG_ADD_DROPPED_FILE_UNDO);

	return NO_ERROR;
      }

      int add (THREAD_ENTRY *thread_p, const OID &oid, const float *vector)
      {
	int error = NO_ERROR;

	  // Possibly max_level should be updated if
	  PAGE_PTR root_page_ptr = pgbuf_fix (thread_p, &m_root_vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
	  if (root_page_ptr == NULL)
	    {
	      ASSERT_ERROR ();
	      return ER_FAILED;
	    }

    scope_exit <std::function<void (void)>> unfix_root_pgptr (
        [&thread_p, &root_page_ptr] ()
    {
      pgbuf_unfix_and_init_after_check (thread_p, root_page_ptr);
    });

	  level_t curr_max_level; // get max_level from root page
	  level_t new_target_level = choose_random_level_ (m_context.m_level_generator);

	  if (new_target_level > MAX_LEVELS)
	    {
	      // For optimzation, if new_target_level is greater than max_level, we can just use max_level
	      new_target_level = MAX_LEVELS;
	    }

	  // TODO: entry page's slot
	  OID entry_oid = NULL_OID;
	  OID closest_oid = NULL_OID;

	  error = search_for_one (vector, entry_oid, max_level, new_target_level, closest_oid);

	  for (level_t level = (std::min) (new_target_level, max_level); level >= 0; --level)
	    {
	      error = search_to_insert (vector, closest_oid, level);
	      std::vector<candidate_t> closest_view;
	      {
		closest_view = form_links_to_closests_ (closest_oid, level);
		closest_slot = closest_view[0].key;
	      }
	      form_reverse_links_ ()
	    }

	  // Create a new node record template
	  // TODO

	  // For the first element,
	return error;
      }

      int search (const float *query, std::size_t k)
      {
	int error = NO_ERROR;
	if (k ==0)
	  {
	    // TODO: exceptional
	  }

	// TODO: get from root page
	level_t current_max_level = 16;

	OID entry_oid = NULL_OID;
	OID closest_oid = NULL_OID;

	error = search_for_one_ (
			query, entry_oid, current_max_level, 0, closest_oid);


	error = search_to_find_in_base_ (query, closest_oid);

	return error;
      }

    protected:

      int search_for_one (const float *query, const OID closest_oid, const level_t begin_level, const level_t end_level,
			  OID &output_oid)
      {
	int error = NO_ERROR;

	// asserts
	assert (begin_level >= end_level);

	distance_t closest_dist = compute_distance_ (query, closest_oid);
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
		    distance_t candidate_dist = compute_distance_ (query, neighbor_oid);
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

	output_oid = closest_oid;
	return error;
      }

      int search_to_insert (const float *query, const OID start_oid, const uint16_t level, const std::size_t top_limit)
      {
	distance_t radius = compute_distance_ (query, start_oid);
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

		distance_t sucessor_dist = compute_distance_ (query, successor_oid);
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

      int search_to_find_in_base_ (const float *query, const OID closest_oid, const std::size_t expansion)
      {
	int error = NO_ERROR;


	distance_t radius = compute_distance_ (query, closest_oid)
			    next.push (candidate_t {-radius, closest_oid});
	visits.insert (closest_oid);

	while (!next.empty ())
	  {
	    candidate_t candidacy = next.top ();
	    if ((-candidacy.distance) > radius && top.size() == expansion)
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

		distance_t sucessor_dist = compute_distance_ (query, successor_oid);
		if (top.size() < expansion || sucessor_dist < radius)
		  {
		    next.push (candidate_t {-sucessor_dist, successor_oid});
		    top.insert (candidate_t {sucessor_dist, successor_oid});
		    radius = top.top().distance;
		  }
	      }
	  }

	return NO_ERROR;
      }

      int form_links_to_closest_ (const OID new_slot_oid, const uint16_t level)
      {
	candidates_view_t top_view = refine_ (top);

	// outgoing links from new node
	neighbors_ref_t new_neighbors = neighbors_ (new_slot_oid, level);
	for (int i = 0; i < top_view.size(); i++)
	  {
	    new_neighbors.push_back (top_view[i].key)
	  }

	return NO_ERROR;
      }

      int form_reverse_links_ (OID new_slot_oid, float *value, candidates_view_t &new_neighbors, level_t level)
      {
	top_candidates_t top_for_refine = m_context.m_top_for_refine;

	for (auto n : new_neighbors)
	  {
	    OID close_oid = n.key;
	    if (close_slot == new_slot_oid)
	      {
		continue;
	      }

	    neighbors_ref_t close_header = neighbors_ (close_oid, level);
	    if (close_header.size () < m_connectivity)
	      {
		close_header.push_back (new_slot_oid);
		continue;
	      }

	    top_for_refine.clear();
	    top_for_refine.push ({compute_distance_ (value, close_oid), new_slot_oid});
	    for (int i = 0; i < close_header.size (); i++)
	      {
		OID successor_oid = close_header.at (i);
		top_for_refine.push ({compute_distance_ (close_oid, successor_oid), successor_oid});
	      }

	    // remove all neighbors from close_header
	    close_header.claer();
	    candidates_view_t top_view = refine_ (top_for_refine);
	    for (int i = 0; i < top_view.size (); i++)
	      {
		close_header.push_back (top_view[i].key);
	      }
	  }
      }

      neighbors_ref_t neighbors_non_base_ (const OID closest_oid, const uint16_t level)
      {
	int error = NO_ERROR;
	// TODO:
	return neighbors_ref_t (nullptr, 0);
      }

      neighbors_ref_t neighbors_ (const OID closest_oid, const uint16_t level)
      {
	int error = NO_ERROR;
	// TODO:
	return neighbors_ref_t (nullptr, 0);
      }

      level_t choose_random_level_ (std::default_random_engine &generator)
      {
	std::uniform_real_distribution<double> distribution (0.0, 1.0);
	double r = -std::log (distribution (level_generator)) * m_inverse_log_connectivity;
	return (level_t)r;
      }

    private:
      VPID m_root_vpid;

      uint16_t m_dimension;
      uint16_t m_connectivity;
      uint16_t m_ef_construction;
      DB_VECTOR_DISTANCE_METRIC m_metric;

      uint16_t m_max_level;

      double m_inverse_log_connectivity;
      context_t m_context;
  };


}

#endif