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
// hnsw_usearch_ng.cpp - new implementation of HNSW index
//

#include "hnsw_api.hpp"

#include "page_buffer.h"
#include "storage_common.h"
#include "thread_compat.hpp"
#include "thread_manager.hpp"
#include "vector_distance_enum.h"

#include "hnsw_algo.hpp"

// #include "hnsw_storage_mem.hpp"
#include "hnsw_storage_disk.hpp"

#include "btree_load.h"
#include "slotted_page.h"

#include "db_vector.hpp"	// db_vector_is_all_zeros
#if defined (SERVER_MODE)
#include "thread_worker_pool.hpp"
#endif
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

class hnsw_usearch_ng_backend final:public hnsw_index_backend
{
  public:
    hnsw_usearch_ng_backend (const std::string &id):hnsw_index_backend (id)
    {
    }
    ~hnsw_usearch_ng_backend () override = default;

    virtual bool is_metric_supported (const DB_VECTOR_DISTANCE_METRIC &metric)
    const override;
    virtual bool is_disk_index () const override
    {
      return true;
    }

    virtual hnsw_index *create_index (THREAD_ENTRY *thread_p,
				      const BTID *btid,
				      const std::string &name,
				      const hnsw_build_params &build_params)
    override;
    virtual int drop_index (THREAD_ENTRY *thread_p, const BTID *btid) override
    {
      return NO_ERROR;
    }
};

/* thread scope */
class hnsw_usearch_ng final:public hnsw_index
{
  public:
    // TODO: factory pattern
    using traits = cubhnsw::disk_traits_t;

    using algo_type = cubhnsw::algo < traits >;
    using storage_type = cubhnsw::disk_storage;

    hnsw_usearch_ng (hnsw_index_backend &backend, const BTID &btid,
		     const std::string &name,
		     const hnsw_build_params &build_params);
    ~hnsw_usearch_ng () override;

    int init (cubthread::entry *thread_p, PAGE_PTR page_ptr, RECDES &rec);

    virtual int prepare_to_add (cubthread::entry *thread_p, int n_vectors, const OID *oid,
				const float *vector) override;
    virtual int add (cubthread::entry *thread_p, int n_vectors, const OID *oid,
		     const float *vector) override;
    virtual int search (cubthread::entry *thread_p, const float *query, const int k, const int ef_search,
			OID *rec_oids, float *distances) override;

    virtual int remove (cubthread::entry *thread_p, const OID *oid) override;
    virtual int update (cubthread::entry *thread_p, const OID *oid, const float *vector) override;

    // SCAN_PRED from query_evaluator.h
    virtual int filtered_search (cubthread::entry *thread_p, const float *query, const int k,
				 const SCAN_PRED &filter, OID *rec_oids,
				 float *distances) override;
    virtual int dump (cubthread::entry *thread_p, FILE *fp) override;

    int add_vector (cubthread::entry &thread_ref, const OID oid, const float *vector, const int tran_index);

    void update_pca_online (const float *vectors, int n_vectors, int dim,
			    bool normalize_inputs);

    VPID m_root_vpid;

    std::unique_ptr < algo_type > m_algo;
    std::unique_ptr < storage_type > m_storage;

    // For PCA Reordering
    std::vector<float> m_pca_mean;  // running mean (dimension)
    std::vector<float> m_pca_pc1;   // running PC1 (dimension)
    uint64_t m_pca_seen {0};        // number of samples seen (for step schedule)
    bool m_pca_inited {false};

#if defined (SERVER_MODE)
    cubthread::entry_workpool *m_worker_pool;
    size_t m_worker_pool_size;
#endif
};

// Helper functions for vector preprocessing

static inline float dot_f (const float *a, const float *b, int dim)
{
  float s = 0.0f;
  for (int i = 0; i < dim; ++i)
    {
      s += a[i] * b[i];
    }
  return s;
}

static inline float l2norm_f (const float *a, int dim)
{
  return std::sqrt (dot_f (a, a, dim));
}

static inline void normalize_vec (std::vector<float> &v)
{
  float n = std::sqrt (dot_f (v.data (), v.data (), (int) v.size ()));
  if (n > 0.0f)
    {
      for (auto &x : v)
	{
	  x /= n;
	}
    }
}

static inline bool is_all_zeros_f (const float *v, int dim)
{
  for (int i = 0; i < dim; ++i) if (v[i] != 0.0f)
      {
	return false;
      }
  return true;
}

void
hnsw_usearch_ng::update_pca_online (const float *vectors, int n_vectors, int dim,
				    bool normalize_inputs)
{
  if (m_pca_mean.empty ())
    {
      m_pca_mean.assign (dim, 0.0f);
    }
  if (m_pca_pc1.empty ())
    {
      m_pca_pc1.assign (dim, 0.0f);
    }

  std::vector<float> x (dim);
  std::vector<float> centered (dim);

  for (int i = 0; i < n_vectors; ++i)
    {
      const float *v = vectors + i * dim;

      if (normalize_inputs && db_vector_is_all_zeros (v, dim))
	{
	  continue;
	}

      // x = v (or normalized v)
      if (!normalize_inputs)
	{
	  for (int d = 0; d < dim; ++d)
	    {
	      x[d] = v[d];
	    }
	}
      else
	{
	  float n = l2norm_f (v, dim);
	  if (n <= 0.0f)
	    {
	      continue;
	    }
	  for (int d = 0; d < dim; ++d)
	    {
	      x[d] = v[d] / n;
	    }
	}

      // centered = x - mean(prev)
      for (int d = 0; d < dim; ++d)
	{
	  centered[d] = x[d] - m_pca_mean[d];
	}

      ++m_pca_seen;
      const float inv = 1.0f / (float) m_pca_seen;
      for (int d = 0; d < dim; ++d)
	{
	  m_pca_mean[d] += centered[d] * inv;
	}

      if (!m_pca_inited)
	{
	  m_pca_pc1 = centered;
	  normalize_vec (m_pca_pc1);
	  m_pca_inited = true;
	  continue;
	}

      // Oja update: w <- w + eta * centered * (centered^T w)
      const float eta0 = 0.2f;
      const float eta = eta0 / std::sqrt ((float) m_pca_seen);

      const float proj = dot_f (centered.data (), m_pca_pc1.data (), dim);
      for (int d = 0; d < dim; ++d)
	{
	  m_pca_pc1[d] += eta * centered[d] * proj;
	}
      normalize_vec (m_pca_pc1);
    }
}

// =====================================================================
// hnsw_usearch_ng_backend
// =====================================================================

bool
hnsw_usearch_ng_backend::
is_metric_supported (const DB_VECTOR_DISTANCE_METRIC &metric) const
{
  switch (metric)
    {
    case METRIC_COSINE:
    case METRIC_DOT:
    case METRIC_EUCLIDEAN:
      return true;
    case METRIC_UNKNOWN:
    case METRIC_MANHATTAN:
    default:
      return false;
    }
}

hnsw_index *
hnsw_usearch_ng_backend::create_index (THREAD_ENTRY *thread_p,
				       const BTID *btid,
				       const std::string &name,
				       const hnsw_build_params &build_params)
{
  VPID root_vpid = { btid->root_pageid, btid->vfid.volid };
  PAGE_PTR page_ptr =
	  pgbuf_fix (thread_p, &root_vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
		     PGBUF_UNCONDITIONAL_LATCH);
  if (page_ptr == NULL)
    {
      ASSERT_ERROR ();
      return NULL;
    }

  char rec_buf[IO_MAX_PAGE_SIZE + INT_ALIGNMENT];
  RECDES rec
  {
    DB_PAGESIZE, 0, REC_HOME, PTR_ALIGN (rec_buf, INT_ALIGNMENT)};

  hnsw_usearch_ng *index =
	  new hnsw_usearch_ng (*this, *btid, name, build_params);

  if (index->init (thread_p, page_ptr, rec) != NO_ERROR)
    {
      ASSERT_ERROR ();
      return NULL;
    }

  // pgbuf_unfix_and_init_after_check (thread_p, page_ptr);
  //log_sysop_attach_to_outer (thread_p);
  //vacuum_log_add_dropped_file (thread_p, &btid->vfid, NULL, VACUUM_LOG_ADD_DROPPED_FILE_UNDO);

  return index;
}

// =====================================================================
// hnsw_usearch_ng
// =====================================================================

hnsw_usearch_ng::hnsw_usearch_ng (hnsw_index_backend &backend, const BTID &btid, const std::string &name,
				  const hnsw_build_params &build_params):hnsw_index (backend, btid, name,
					build_params)
{
  m_root_vpid =
  {
    btid.root_pageid, btid.vfid.volid
  };

  m_storage = std::make_unique < storage_type > (btid, build_params);
  m_algo = std::make_unique < algo_type > (build_params);
}

int
hnsw_usearch_ng::init (cubthread::entry *thread_p, PAGE_PTR page_ptr, RECDES &rec)
{
  std::size_t root_size;
  m_storage->init_root (reinterpret_cast < std::byte * > (rec.data),
			root_size);
  rec.length = (int) root_size;

  if (spage_insert_at (thread_p, page_ptr, 1, &rec) != SP_SUCCESS)
    {
      assert (false);
      return ER_FAILED;
    }

  pgbuf_set_dirty (thread_p, page_ptr, FREE);

  m_algo->set_storage (m_storage.get ());

#if defined (SERVER_MODE)
  m_worker_pool_size = std::thread::hardware_concurrency ();
  m_worker_pool = cubthread::get_manager ()->create_worker_pool (
			  m_worker_pool_size, m_worker_pool_size, "hnsw insertion worker pool", NULL, 1, false);
#endif
  return NO_ERROR;
}

hnsw_usearch_ng::~hnsw_usearch_ng ()
{
#if defined (SERVER_MODE)
  cubthread::get_manager()->destroy_worker_pool (m_worker_pool);
#endif
}

int
hnsw_usearch_ng::prepare_to_add (cubthread::entry *thread_p, int n_vectors, const OID *oid,
				 const float *vector)
{
  // do nothing
  return NO_ERROR;
}

int
hnsw_usearch_ng::add (cubthread::entry *thread_p, int n_vectors,
		      const OID *oid, const float *vector)
{
  const int dim = m_build_params.dimension;
  const bool normalize_inputs =
	  (m_build_params.metric == DB_VECTOR_DISTANCE_METRIC::METRIC_COSINE);

  // 0) Update global PC1 using this batch
  update_pca_online (vector, n_vectors, dim, normalize_inputs);

  auto is_skippable = [&] (const float *v) -> bool
  {
    return (normalize_inputs && db_vector_is_all_zeros (v, dim));
  };

  // 1) score + ordering by projection onto (updated) PC1
  std::vector<std::pair<float, int>> scored;
  scored.reserve (n_vectors);

  for (int i = 0; i < n_vectors; ++i)
    {
      const float *v = vector + i * dim;

      if (is_skippable (v))
	{
	  er_log_debug (ARG_FILE_LINE, "Vector is all zeros, skipping add");
	  continue;
	}

      float score = 0.0f;

      // score = dot(pc1, (x - mean))
      if (!normalize_inputs)
	{
	  for (int d = 0; d < dim; ++d)
	    {
	      score += m_pca_pc1[d] * (v[d] - m_pca_mean[d]);
	    }
	}
      else
	{
	  float n = l2norm_f (v, dim);
	  if (n <= 0.0f)
	    {
	      continue;
	    }

	  for (int d = 0; d < dim; ++d)
	    {
	      const float x = v[d] / n;
	      score += m_pca_pc1[d] * (x - m_pca_mean[d]);
	    }
	}

      scored.emplace_back (score, i);
    }

  std::sort (scored.begin (), scored.end (),
	     [] (const auto &a, const auto &b)
  {
    return a.first < b.first;
  });

  // 2) Push tasks / execute inserts in PCA order
  for (const auto &kv : scored)
    {
      const int idx = kv.second;
      const float *v = vector + idx * dim;

#if defined (SERVER_MODE)
      auto exec_f = std::bind (&hnsw_usearch_ng::add_vector,
			       std::ref (*this),
			       std::placeholders::_1,
			       oid[idx],
			       v,
			       thread_p->tran_index);
      cubthread::entry_callable_task *task = new cubthread::entry_callable_task (exec_f);
      cubthread::get_manager()->push_task (m_worker_pool, task);
#else
      m_algo->add (thread_p, oid[idx], v);
#endif
    }
  return NO_ERROR;
}

int
hnsw_usearch_ng::add_vector (cubthread::entry &thread_ref, const OID oid, const float *vector, const int tran_index)
{
  thread_ref.tran_index = tran_index;

  if (m_build_params.metric == DB_VECTOR_DISTANCE_METRIC::METRIC_COSINE
      && db_vector_is_all_zeros (vector, m_build_params.dimension))
    {
      er_log_debug (ARG_FILE_LINE, "Vector is all zeros, skipping search");
      return NO_ERROR;
    }

  m_algo->add (&thread_ref, oid, vector);
  return NO_ERROR;
}

int
hnsw_usearch_ng::search (cubthread::entry *thread_p, const float *query, const int k, const int ef_search,
			 OID *rec_oids, float *distances)
{
  if (m_build_params.metric == DB_VECTOR_DISTANCE_METRIC::METRIC_COSINE
      && db_vector_is_all_zeros (query, m_build_params.dimension))
    {
      er_log_debug (ARG_FILE_LINE, "Vector is all zeros, skipping search");
      return NO_ERROR;
    }

  auto results = m_algo->search (thread_p, query, k, ef_search);
  if (results.error != NO_ERROR)
    {
      er_log_debug (ARG_FILE_LINE, "Error during search: %s", results.error);
      // TODO: error handling
      assert (false);
      return ER_FAILED;
    }

  const auto &results_view = results.results;
  for (std::size_t i = 0; i != results_view.size (); ++i)
    {
      rec_oids[i] = results.oids[i];
      distances[i] = results_view[i].distance;
    }
  return NO_ERROR;
}

int
hnsw_usearch_ng::remove (cubthread::entry *thread_p, const OID *oid)
{
  return ER_FAILED;
}

int
hnsw_usearch_ng::update (cubthread::entry *thread_p, const OID *oid, const float *vector)
{
  return ER_FAILED;
}

int
hnsw_usearch_ng::filtered_search (cubthread::entry *thread_p, const float *query, const int k,
				  const SCAN_PRED &filter, OID *rec_oids,
				  float *distances)
{
  return ER_FAILED;
}

int
hnsw_usearch_ng::dump (cubthread::entry *thread_p, FILE *fp)
{
  return ER_FAILED;
}

HNSW_REGISTER_BACKEND ("usearchng",[] (const char *id)
{
  return std::make_unique < hnsw_usearch_ng_backend >
	 (id);
}
		      );
