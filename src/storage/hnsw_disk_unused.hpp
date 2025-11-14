
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

    int form_links_to_closest_ (const OID new_slot_oid, const level_t level)
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
