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
		     const hnsw_build_params &build_params,
		     PAGE_PTR page_ptr, RECDES &rec);
    ~hnsw_usearch_ng () = default;

    virtual int prepare_to_add (int n_vectors, const OID *oid,
				const float *vector) override;
    virtual int add (int n_vectors, const OID *oid,
		     const float *vector) override;

    virtual int search (const float *query, const int k, const int ef_search,
			OID *rec_oids, float *distances) override;
    virtual int remove (const OID *oid) override;
    virtual int update (const OID *oid, const float *vector) override;

    // SCAN_PRED from query_evaluator.h
    virtual int filtered_search (const float *query, const int k,
				 const SCAN_PRED &filter, OID *rec_oids,
				 float *distances) override;
    virtual int dump (FILE *fp) override;

    virtual int save (const std::string &path) override;
    virtual int load (const std::string &path) override;

    THREAD_ENTRY *m_thread_p;
    VPID m_root_vpid;

    std::unique_ptr < algo_type > m_algo;
    std::unique_ptr < storage_type > m_storage;
};

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

  hnsw_index *index =
	  new hnsw_usearch_ng (*this, *btid, name, build_params, page_ptr, rec);

  // pgbuf_unfix_and_init_after_check (thread_p, page_ptr);
  //log_sysop_attach_to_outer (thread_p);
  //vacuum_log_add_dropped_file (thread_p, &btid->vfid, NULL, VACUUM_LOG_ADD_DROPPED_FILE_UNDO);

  return index;
}

// =====================================================================
// hnsw_usearch_ng
// =====================================================================

hnsw_usearch_ng::hnsw_usearch_ng (hnsw_index_backend &backend, const BTID &btid, const std::string &name,
				  const hnsw_build_params &build_params, PAGE_PTR page_ptr, RECDES &rec):hnsw_index (backend, btid, name,
					build_params)
{
  m_root_vpid =
  {
    btid.root_pageid, btid.vfid.volid
  };
  this->m_thread_p = thread_get_thread_entry_info ();

  m_storage = std::make_unique < cubhnsw::disk_storage > (btid, build_params);
  m_storage->set_thread_entry (thread_get_thread_entry_info ());

  std::size_t root_size;
  m_storage->init_root (reinterpret_cast < std::byte * > (rec.data),
			root_size);
  rec.length = (int) root_size;

  if (spage_insert_at (this->m_thread_p, page_ptr, 1, &rec) != SP_SUCCESS)
    {
      assert (false);
    }

  pgbuf_set_dirty (this->m_thread_p, page_ptr, FREE);
  page_ptr = NULL;

  m_algo = std::make_unique < algo_type > (build_params);
  m_algo->set_storage (m_storage.get ());
}

int
hnsw_usearch_ng::prepare_to_add (int n_vectors, const OID *oid,
				 const float *vector)
{
  // do nothing
  return NO_ERROR;
}

int
hnsw_usearch_ng::add (int n_vectors, const OID *oid, const float *vector)
{
  #pragma omp parallel
  #pragma omp for
  for (int i = 0; i < n_vectors; ++i)
    {
      if (m_build_params.metric == DB_VECTOR_DISTANCE_METRIC::METRIC_COSINE
	  && db_vector_is_all_zeros (vector + i * m_build_params.dimension,
				     m_build_params.dimension))
	{
	  er_log_debug (ARG_FILE_LINE, "Vector is all zeros, skipping search");
	  continue;
	}
      m_algo->add (oid[i], vector + i * m_build_params.dimension,
		   m_build_params.ef_construction);
    }
  return NO_ERROR;
}

int
hnsw_usearch_ng::search (const float *query, const int k, const int ef_search,
			 OID *rec_oids, float *distances)
{
  if (m_build_params.metric == DB_VECTOR_DISTANCE_METRIC::METRIC_COSINE
      && db_vector_is_all_zeros (query, m_build_params.dimension))
    {
      er_log_debug (ARG_FILE_LINE, "Vector is all zeros, skipping search");
      return NO_ERROR;
    }

  auto results = m_algo->search (query, k, ef_search);
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
hnsw_usearch_ng::remove (const OID *oid)
{
  return ER_FAILED;
}

int
hnsw_usearch_ng::update (const OID *oid, const float *vector)
{
  return ER_FAILED;
}

int
hnsw_usearch_ng::filtered_search (const float *query, const int k,
				  const SCAN_PRED &filter, OID *rec_oids,
				  float *distances)
{
  return ER_FAILED;
}

int
hnsw_usearch_ng::dump (FILE *fp)
{
  return ER_FAILED;
}

int
hnsw_usearch_ng::save (const std::string &path)
{
  return ER_FAILED;
}

int
hnsw_usearch_ng::load (const std::string &path)
{
  return ER_FAILED;
}

HNSW_REGISTER_BACKEND ("usearchng",[] (const char *id)
{
  return std::make_unique < hnsw_usearch_ng_backend >
	 (id);
}
		      );
