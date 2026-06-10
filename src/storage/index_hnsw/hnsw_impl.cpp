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
// hnsw_impl.cpp - new implementation of HNSW index
//

#include "hnsw_api.hpp"

#include <cstring>

#include "page_buffer.h"
#include "storage_common.h"
#include "thread_compat.hpp"
#include "thread_manager.hpp"
#include "vector_distance_enum.h"

#include "hnsw_algo.hpp"

#include "hnsw_storage.hpp"

#include "btree_load.h"
#include "slotted_page.h"

#include "db_vector.hpp"	// db_vector_is_all_zeros
#include "log_manager.h"
#include "oid.h"
#if defined (SERVER_MODE)
#include "thread_worker_pool.hpp"
#include "thread_worker_pool_taskcap.hpp"
#include "lockfree_circular_queue.hpp"
#endif
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace
{
  class aligned_vector_buffer final
  {
    public:
      explicit aligned_vector_buffer (std::size_t dimension)
      {
	m_data = db_vector_allocate_float_array (static_cast<int> (dimension));
	assert (m_data != nullptr);
      }

      ~aligned_vector_buffer ()
      {
	db_vector_free_float_array (m_data);
      }

      aligned_vector_buffer (const aligned_vector_buffer &) = delete;
      aligned_vector_buffer &operator= (const aligned_vector_buffer &) = delete;

      float *data () noexcept
      {
	return m_data;
      }

      const float *data () const noexcept
      {
	return m_data;
      }

      void copy_from (const float *source, std::size_t dimension) noexcept
      {
	std::memcpy (m_data, source, dimension * sizeof (float));
      }

    private:
      float *m_data {nullptr};
  };
}

class hnsw_impl_backend final:public hnsw_index_backend
{
  public:
    hnsw_impl_backend (const std::string &id):hnsw_index_backend (id)
    {
    }
    ~hnsw_impl_backend () override = default;

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
    virtual hnsw_index *load_index (THREAD_ENTRY *thread_p,
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
class hnsw_impl final:public hnsw_index
{
  public:
    using algo_type = cubhnsw::algo;
    using storage_type = cubhnsw::storage;

    hnsw_impl (hnsw_index_backend &backend, const BTID &btid,
	       const std::string &name,
	       const hnsw_build_params &build_params);
    ~hnsw_impl () override;

    int init (cubthread::entry *thread_p, PAGE_PTR page_ptr, RECDES &rec);
    int init_for_load (cubthread::entry *thread_p);
    int init_for_recovery ();

    virtual int prepare_to_add (cubthread::entry *thread_p, int n_vectors, const OID *oid,
				const float *vector) override;
    virtual int add (cubthread::entry *thread_p, int n_vectors, const OID *oid,
		     const float *vector) override;
    virtual int search (cubthread::entry *thread_p, const float *query, const int k, const int ef_search,
			OID *rec_oids, float *distances) override;

    virtual int remove (cubthread::entry *thread_p, const OID *oid) override;
    virtual int update (cubthread::entry *thread_p, const OID *oid, const float *vector) override;
    virtual int revive (cubthread::entry *thread_p, const OID *oid, const float *vector) override;
    virtual int peek_live_vector (cubthread::entry *thread_p, const OID *oid, float *out_vector,
				  bool *found) override;

    // SCAN_PRED from query_evaluator.h
    virtual int filtered_search (cubthread::entry *thread_p, const float *query, const int k,
				 const SCAN_PRED &filter, OID *rec_oids,
				 float *distances) override;
    virtual int dump (cubthread::entry *thread_p, FILE *fp) override;

  protected:

    struct hnsw_build_worker_context final
    {
      std::mutex m_mutex;
      std::condition_variable m_cv;

      std::atomic<int> m_active_tasks {0};
      int m_pushed_tasks {0};

      int tran_index {0};
    };

    struct hnsw_build_worker_job final
    {
      hnsw_build_worker_context *m_ctx;
      OID m_oid;
      const float *m_vector;
    };

    void init_worker_pool ();
    void add_internal (cubthread::entry &thread_ref, hnsw_build_worker_job &job);
    void update_pca_online (const float *vectors, int n_vectors, int dim,
			    bool normalize_inputs);

    VPID m_root_vpid;

    std::unique_ptr < algo_type > m_algo;
    std::unique_ptr < storage_type > m_storage;

    // For PCA Reordering
    // TODO: thread-safe
    std::vector<float> m_pca_mean;  // running mean (dimension)
    std::vector<float> m_pca_pc1;   // running PC1 (dimension)
    uint64_t m_pca_seen {0};        // number of samples seen (for step schedule)
    bool m_pca_inited {false};

#if defined (SERVER_MODE)
    cubthread::entry_workpool *m_build_worker_pool;
    cubthread::worker_pool_task_capper<cubthread::entry> *m_build_worker_task_capper;
    std::vector<cubthread::entry_callable_task> m_build_worker_tasks;
    std::size_t m_build_worker_size;

    std::atomic<int> m_pending_jobs {0};
    std::mutex m_worker_mtx;
    std::condition_variable m_worker_cv;
    std::atomic<bool> m_stop {false};

    std::mutex m_single_thread_mtx;
    bool m_force_single_thread {true};
#endif
    lockfree::circular_queue<hnsw_build_worker_job> *m_build_worker_job_queue;
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

void
hnsw_impl::update_pca_online (const float *vectors, int n_vectors, int dim,
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
	  if (db_vector_is_all_zeros (centered.data (), dim))
	    {
	      continue;
	    }
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
// hnsw_impl_backend
// =====================================================================

bool
hnsw_impl_backend::
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
hnsw_impl_backend::create_index (THREAD_ENTRY *thread_p,
				 const BTID *btid,
				 const std::string &name,
				 const hnsw_build_params &build_params)
{
  hnsw_impl *index =
	  new hnsw_impl (*this, *btid, name, build_params);
  if (index == NULL)
    {
      return NULL;
    }

  if (log_is_in_crash_recovery ())
    {
      VPID root_vpid = { btid->root_pageid, btid->vfid.volid };
      PAGE_PTR page_ptr =
	      pgbuf_fix (thread_p, &root_vpid, RECOVERY_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
      if (page_ptr == NULL)
	{
	  ASSERT_ERROR ();
	  delete index;
	  return NULL;
	}

      pgbuf_set_page_ptype (thread_p, page_ptr, PAGE_HNSW);
      spage_initialize (thread_p, page_ptr, UNANCHORED_KEEP_SEQUENCE, HNSW_MAX_ALIGN, DONT_SAFEGUARD_RVSPACE);

      char header_buf[IO_MAX_PAGE_SIZE + HNSW_MAX_ALIGN];
      HNSW_HEADER hnsw_header;
      hnsw_header.dimension = build_params.dimension;
      hnsw_header.hnsw_M = build_params.m;
      hnsw_header.hnsw_efConstruction = build_params.ef_construction;
      hnsw_header.metric = static_cast<int> (build_params.metric);

      RECDES header_rec;
      header_rec.area_size = DB_PAGESIZE;
      header_rec.data = PTR_ALIGN (header_buf, HNSW_MAX_ALIGN);
      memcpy (header_rec.data, &hnsw_header, sizeof (hnsw_header));
      header_rec.length = sizeof (hnsw_header);
      header_rec.type = REC_HOME;

      if (spage_insert_at (thread_p, page_ptr, HNSW_HEADER_NUM, &header_rec) != SP_SUCCESS)
	{
	  ASSERT_ERROR ();
	  pgbuf_unfix_and_init_after_check (thread_p, page_ptr);
	  delete index;
	  return NULL;
	}

      char rec_buf[IO_MAX_PAGE_SIZE + INT_ALIGNMENT];
      RECDES rec
      {
	DB_PAGESIZE, 0, REC_HOME, PTR_ALIGN (rec_buf, INT_ALIGNMENT)};

      if (index->init (thread_p, page_ptr, rec) != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  pgbuf_unfix_and_init_after_check (thread_p, page_ptr);
	  delete index;
	  return NULL;
	}

      return index;
    }

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

  if (index->init (thread_p, page_ptr, rec) != NO_ERROR)
    {
      ASSERT_ERROR ();
      pgbuf_unfix_and_init (thread_p, page_ptr);
      delete index;
      return NULL;
    }

  // pgbuf_unfix_and_init_after_check (thread_p, page_ptr);
  //log_sysop_attach_to_outer (thread_p);
  //vacuum_log_add_dropped_file (thread_p, &btid->vfid, NULL, VACUUM_LOG_ADD_DROPPED_FILE_UNDO);

  return index;
}

hnsw_index *
hnsw_impl_backend::load_index (THREAD_ENTRY *thread_p,
			       const BTID *btid,
			       const std::string &name,
			       const hnsw_build_params &build_params)
{
  hnsw_impl *index =
	  new hnsw_impl (*this, *btid, name, build_params);

  if (index == NULL)
    {
      return NULL;
    }

  if (index->init_for_load (thread_p) != NO_ERROR)
    {
      delete index;
      return NULL;
    }

  return index;
}

// =====================================================================
// hnsw_impl
// =====================================================================

hnsw_impl::hnsw_impl (hnsw_index_backend &backend, const BTID &btid, const std::string &name,
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
hnsw_impl::init (cubthread::entry *thread_p, PAGE_PTR page_ptr, RECDES &rec)
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

  init_worker_pool ();

  return NO_ERROR;
}

int
hnsw_impl::init_for_load (cubthread::entry *thread_p)
{
  PAGE_PTR page_ptr = pgbuf_fix (thread_p, &m_root_vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (page_ptr == NULL)
    {
      ASSERT_ERROR ();
      return ER_FAILED;
    }

  SPAGE_SLOT *slotp = spage_get_slot (page_ptr, 1);
  if (slotp == NULL || slotp->record_length < static_cast<int> (cubhnsw::root_t::get_size ()))
    {
      pgbuf_unfix_and_init (thread_p, page_ptr);
      return ER_FAILED;
    }

  cubhnsw::root_t root { reinterpret_cast<cubhnsw::byte_t *> (page_ptr) + slotp->offset_to_record };
  cubhnsw::slot_id_t entry = root.get_entry ();
  m_storage->set_empty (OID_ISNULL (&entry));

  pgbuf_unfix_and_init (thread_p, page_ptr);

  m_algo->set_storage (m_storage.get ());
  if (!m_storage->is_empty ())
    {
      if (m_storage->rebuild_node_slots_cache (thread_p) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }
  init_worker_pool ();
  return NO_ERROR;
}

int
hnsw_impl::init_for_recovery ()
{
  m_algo->set_storage (m_storage.get ());
  init_worker_pool ();
  return NO_ERROR;
}

void
hnsw_impl::init_worker_pool ()
{
#if defined (SERVER_MODE)
  // TODO (CUBVEC): parameterize the worker size
  m_build_worker_size = std::min (static_cast<std::size_t> (std::thread::hardware_concurrency ()),
				  static_cast<std::size_t> (16));

  if (m_build_worker_size > 1)
    {
      m_build_worker_pool = cubthread::get_manager ()->create_worker_pool (
				    m_build_worker_size, m_build_worker_size, "hnsw insertion worker pool", NULL, 1, false);
      m_build_worker_task_capper = new cubthread::worker_pool_task_capper<cubthread::entry> (m_build_worker_pool);

      const std::size_t queue_size = m_build_worker_size * 256;
      m_build_worker_job_queue = new lockfree::circular_queue<hnsw_build_worker_job> (queue_size);
      m_build_worker_tasks.reserve (m_build_worker_size);
      for (std::size_t i = 0; i < m_build_worker_size; ++i)
	{
	  auto exec_func = [this] (cubthread::entry &entry)
	  {
	    while (true)
	      {
		std::unique_lock<std::mutex> lock (m_worker_mtx);
		m_worker_cv.wait (lock, [&] { return m_pending_jobs.load () > 0 || m_stop.load (); });

		if (m_stop.load (std::memory_order_acquire))
		  {
		    // exit
		    return;
		  }

		if (m_pending_jobs.fetch_sub (1, std::memory_order_acq_rel) <= 0)
		  {
		    continue;
		  }

		hnsw_build_worker_job job;
		if (!m_build_worker_job_queue->consume (job))
		  {
		    // should not happen
		    assert (false);
		    continue;
		  }

		this->add_internal (entry, job);
	      }
	  };
	  m_build_worker_task_capper->push_task (new cubthread::entry_callable_task (exec_func));
	}
    }
  else
    {
      m_build_worker_pool = nullptr;
      m_build_worker_task_capper = nullptr;
      m_build_worker_job_queue = nullptr;
    }
#endif
}

hnsw_impl::~hnsw_impl ()
{
#if defined (SERVER_MODE)
  m_stop.store (true, std::memory_order_release);

  m_worker_cv.notify_all ();

  cubthread::get_manager()->destroy_worker_pool (m_build_worker_pool);

  if (m_build_worker_task_capper != nullptr)
    {
      delete m_build_worker_task_capper;
    }
  if (m_build_worker_job_queue)
    {
      delete m_build_worker_job_queue;
    }
#endif
}

int
hnsw_impl::prepare_to_add (cubthread::entry *thread_p, int n_vectors, const OID *oid,
			   const float *vector)
{
  // do nothing
  return NO_ERROR;
}

int
hnsw_impl::add (cubthread::entry *thread_p, int n_vectors, const OID *oid, const float *vector)
{
  hnsw_build_worker_context ctx;
  ctx.tran_index = thread_p->tran_index;

  const int dim = m_build_params.dimension;
  const bool normalize_inputs =
	  (m_build_params.metric == DB_VECTOR_DISTANCE_METRIC::METRIC_COSINE);

  auto is_skippable = [&] (const float *v) -> bool
  {
    return (normalize_inputs && db_vector_is_all_zeros (v, dim));
  };

  std::vector<hnsw_build_worker_job> jobs;
  jobs.reserve (n_vectors);

  // Preserve input order to avoid build-quality bias from PCA-based reordering.
  for (int idx = 0; idx < n_vectors; ++idx)
    {
      const float *v = vector + idx * dim;
      if (is_skippable (v))
	{
	  er_log_debug (ARG_FILE_LINE, "Vector is all zeros, skipping add");
	  continue;
	}

      hnsw_build_worker_job job;
      job.m_ctx = &ctx;
      job.m_oid = oid[idx];
      job.m_vector = vector + idx * dim;

      jobs.emplace_back (job);
    }

  for (auto &job : jobs)
    {
#if defined (SERVER_MODE)
      if (m_stop.load (std::memory_order_acquire))
	{
	  // exit
	  return ER_FAILED;
	}

      if (m_force_single_thread)
	{
	  std::lock_guard<std::mutex> lk (m_single_thread_mtx);
	  job.m_ctx = nullptr;
	  this->add_internal (*thread_p, job);
	}
      else if (m_build_worker_job_queue != nullptr && m_build_worker_job_queue->produce (std::move (job)))
	{
	  ctx.m_pushed_tasks++;
	}
      else
#endif
	{
	  // SERVER_MODE: if worker pool is not available, perform the job on the current thread
	  // SA_MODE : always reach here
	  // fallback to single thread execution
	  job.m_ctx = nullptr;
	  this->add_internal (*thread_p, job);
	}
    }

#if defined (SERVER_MODE)
  if (ctx.m_pushed_tasks > 0)
    {
      // notify the worker pool to start working
      m_pending_jobs.fetch_add (ctx.m_pushed_tasks, std::memory_order_relaxed);
      m_worker_cv.notify_all ();

      // wait for all jobs to be completed
      std::unique_lock<std::mutex> lock (ctx.m_mutex);
      ctx.m_cv.wait (lock, [&ctx]
      {
	return ctx.m_active_tasks.load (std::memory_order_acquire) == 0;
      });
    }
#endif

  // debugging mode (TODO)
  const std::string &graph_profile = m_algo->dump ();
  if (!graph_profile.empty())
    {
      fprintf (stdout, "%s\n", graph_profile.c_str ());
    }

  return NO_ERROR;
}

void
hnsw_impl::add_internal (cubthread::entry &thread_ref, hnsw_build_worker_job &job)
{
  int error = NO_ERROR;
  hnsw_build_worker_context *ctx = job.m_ctx;
  if (ctx)
    {
      thread_ref.tran_index = ctx->tran_index;
      ctx->m_active_tasks.fetch_add (1, std::memory_order_relaxed);
    }

  if (m_build_params.metric == DB_VECTOR_DISTANCE_METRIC::METRIC_COSINE
      && db_vector_is_all_zeros (job.m_vector, m_build_params.dimension))
    {
      // Just skip if the vector is all zeros
      error = NO_ERROR;
    }
  else
    {
      const bool needs_vector_copy =
	      m_build_params.metric == DB_VECTOR_DISTANCE_METRIC::METRIC_COSINE
	      || !db_vector_is_aligned (job.m_vector);

      std::unique_ptr<aligned_vector_buffer> aligned_vector;
      const float *vector_ptr = job.m_vector;

      if (needs_vector_copy)
	{
	  aligned_vector = std::make_unique<aligned_vector_buffer> (m_build_params.dimension);
	  aligned_vector->copy_from (job.m_vector, m_build_params.dimension);
	  vector_ptr = aligned_vector->data ();
	}

      auto result = m_algo->add (&thread_ref, job.m_oid, vector_ptr);
      error = result.error;
    }

  if (ctx && ctx->m_active_tasks.fetch_sub (1, std::memory_order_acq_rel) == 1)
    {
      std::lock_guard<std::mutex> lk (ctx->m_mutex);
      ctx->m_cv.notify_all ();
    }
}

int
hnsw_impl::search (cubthread::entry *thread_p, const float *query, const int k, const int ef_search,
		   OID *rec_oids, float *distances)
{
  if (m_storage->is_empty ())
    {
      return NO_ERROR;
    }
  const bool needs_query_copy =
	  m_build_params.metric == DB_VECTOR_DISTANCE_METRIC::METRIC_COSINE
	  || !db_vector_is_aligned (query);

  std::unique_ptr<aligned_vector_buffer> aligned_query;
  const float *query_ptr = query;

  if (needs_query_copy)
    {
      aligned_query = std::make_unique<aligned_vector_buffer> (m_build_params.dimension);
      aligned_query->copy_from (query, m_build_params.dimension);
      query_ptr = aligned_query->data ();
    }

  if (m_build_params.metric == DB_VECTOR_DISTANCE_METRIC::METRIC_COSINE
      && db_vector_is_all_zeros (query_ptr, m_build_params.dimension))
    {
      er_log_debug (ARG_FILE_LINE, "Vector is all zeros, skipping search");
      return NO_ERROR;
    }

  auto results = m_algo->search (thread_p, query_ptr, k, ef_search);
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
hnsw_impl::remove (cubthread::entry *thread_p, const OID *oid)
{
  assert (oid != nullptr);

  cubhnsw::algo_context_t context;
  context.m_thread_p = thread_p;

  const std::vector<cubhnsw::slot_id_t> *cached_slots = m_storage->get_node_slots_cached_ids (context, *oid);
  if (cached_slots == nullptr)
    {
      return NO_ERROR;
    }

  std::vector<cubhnsw::slot_id_t> node_slots = *cached_slots;
  for (const cubhnsw::slot_id_t &node_slot : node_slots)
    {
      cubhnsw::pinned_t node_blk = m_storage->get_node_by_slot_id (context, node_slot, cubhnsw::lock_mode::exclusive);
      if (!node_blk || node_blk->data == nullptr)
	{
	  return ER_FAILED;
	}

      cubhnsw::node_t node { reinterpret_cast<cubhnsw::byte_t *> (node_blk->data) };
      OID node_key = node.get_key ();
      if (!OID_EQ (&node_key, oid))
	{
	  assert (false);
	  return ER_FAILED;
	}

      if (node.is_tombstoned ())
	{
	  /* Already tombstoned: keep the slot in the all-slots cache so a later UNDO can
	   * relocate and revive it. Liveness is read from the node, not the cache. */
	  continue;
	}

      node.set_tombstoned (true);
      node_blk.reset ();
    }

  return NO_ERROR;
}

/*
 * hnsw_impl::peek_live_vector() - copy the vector of the live node for `oid` into `out_vector`.
 *
 * Used by the delete path to capture the old vector for the UNDO (revive) record, so a rolled-back
 * delete can relocate and revive the exact node. *found is set false if there is no live node.
 */
int
hnsw_impl::peek_live_vector (cubthread::entry *thread_p, const OID *oid, float *out_vector, bool *found)
{
  assert (oid != nullptr);
  assert (out_vector != nullptr);

  if (found != nullptr)
    {
      *found = false;
    }

  cubhnsw::algo_context_t context;
  context.m_thread_p = thread_p;

  const std::vector<cubhnsw::slot_id_t> *cached_slots = m_storage->get_node_slots_cached_ids (context, *oid);
  if (cached_slots == nullptr)
    {
      return NO_ERROR;
    }

  std::vector<cubhnsw::slot_id_t> node_slots = *cached_slots;
  for (const cubhnsw::slot_id_t &node_slot : node_slots)
    {
      cubhnsw::pinned_t node_blk = m_storage->get_node_by_slot_id (context, node_slot, cubhnsw::lock_mode::shared);
      if (!node_blk || node_blk->data == nullptr)
	{
	  return ER_FAILED;
	}

      cubhnsw::node_t node { reinterpret_cast<cubhnsw::byte_t *> (node_blk->data) };
      OID node_key = node.get_key ();
      if (!OID_EQ (&node_key, oid) || node.is_tombstoned ())
	{
	  continue;
	}

      memcpy (out_vector, node.get_vector (), m_build_params.dimension * sizeof (float));
      if (found != nullptr)
	{
	  *found = true;
	}
      return NO_ERROR;
    }

  return NO_ERROR;
}

/*
 * hnsw_impl::revive() - clear the tombstone of the dead node for `oid` whose vector matches `vector`.
 *
 * In-place inverse of delete (transaction UNDO). The all-slots cache retains tombstoned slots, so
 * the node is relocated by OID without a disk scan; the vector disambiguates among multiple dead
 * nodes for the same OID (e.g. after an UPDATE rollback). Idempotent if no match is found.
 */
int
hnsw_impl::revive (cubthread::entry *thread_p, const OID *oid, const float *vector)
{
  assert (oid != nullptr);
  assert (vector != nullptr);

  cubhnsw::algo_context_t context;
  context.m_thread_p = thread_p;

  const std::vector<cubhnsw::slot_id_t> *cached_slots = m_storage->get_node_slots_cached_ids (context, *oid);
  if (cached_slots == nullptr)
    {
      return NO_ERROR;
    }

  std::vector<cubhnsw::slot_id_t> node_slots = *cached_slots;
  for (const cubhnsw::slot_id_t &node_slot : node_slots)
    {
      cubhnsw::pinned_t node_blk = m_storage->get_node_by_slot_id (context, node_slot, cubhnsw::lock_mode::exclusive);
      if (!node_blk || node_blk->data == nullptr)
	{
	  return ER_FAILED;
	}

      cubhnsw::node_t node { reinterpret_cast<cubhnsw::byte_t *> (node_blk->data) };
      OID node_key = node.get_key ();
      if (!OID_EQ (&node_key, oid) || !node.is_tombstoned ())
	{
	  continue;
	}

      if (memcmp (node.get_vector (), vector, m_build_params.dimension * sizeof (float)) == 0)
	{
	  node.set_tombstoned (false);
	  node_blk.reset ();   // releases the exclusive page with pgbuf_set_dirty
	  return NO_ERROR;
	}
    }

  return NO_ERROR;
}

int
hnsw_impl::update (cubthread::entry *thread_p, const OID *oid, const float *vector)
{
  assert (oid != nullptr);
  assert (vector != nullptr);

  int error = remove (thread_p, oid);
  if (error != NO_ERROR)
    {
      return error;
    }

  return add (thread_p, 1, oid, vector);
}

int
hnsw_impl::filtered_search (cubthread::entry *thread_p, const float *query, const int k,
			    const SCAN_PRED &filter, OID *rec_oids,
			    float *distances)
{
  return ER_FAILED;
}

int
hnsw_impl::dump (cubthread::entry *thread_p, FILE *fp)
{
  return ER_FAILED;
}

HNSW_REGISTER_BACKEND ("hnsw_impl",[] (const char *id)
{
  return std::make_unique < hnsw_impl_backend >
	 (id);
}
		      );
