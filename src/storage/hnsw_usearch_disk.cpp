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
// hnsw_usearch.cpp - implementation of HNSW index using usearch
//

#include "hnsw_api.hpp"

#include "page_buffer.h"
#include "storage_common.h"
#include "thread_compat.hpp"
#include "vector_distance_enum.h"

#include "hnsw_algo.hpp"
#include "hnsw_storage.hpp"

#include "btree_load.h"
#include "slotted_page.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"
class hnsw_disk_usearch_backend final:public hnsw_index_backend
{
  public:
    hnsw_disk_usearch_backend (const std::string &id):hnsw_index_backend (id) {}
    ~hnsw_disk_usearch_backend () override = default;

    virtual bool is_metric_supported (const DB_VECTOR_DISTANCE_METRIC &metric) const override;
    virtual bool is_disk_index () const override
    {
      return true;
    }

    virtual hnsw_index *create_index (THREAD_ENTRY *thread_p, const BTID *btid, const std::string &name,
				      const hnsw_build_params &build_params) override;
    virtual int drop_index (THREAD_ENTRY *thread_p, const BTID *btid) override
    {
      return ER_FAILED;
    }
};

/* thread scope */
class hnsw_disk_usearch final:public hnsw_index
{
  public:

    hnsw_disk_usearch (hnsw_index_backend &backend, const BTID &btid, const std::string &name,
		       const hnsw_build_params &build_params, std::unique_ptr<cubhnsw::algo<cubhnsw::memory_id_traits>> algo);
    ~hnsw_disk_usearch () = default;

    virtual int prepare_to_add (int n_vectors, const OID *oid, const float *vector) override;
    virtual int add (int n_vectors, const OID *oid, const float *vector) override;

    virtual int search (const float *query, const int k, const int ef_search, OID *rec_oids, float *distances) override;
    virtual int remove (const OID *oid) override;
    virtual int update (const OID *oid, const float *vector) override;

    // SCAN_PRED from query_evaluator.h
    virtual int filtered_search (const float *query, const int k, const SCAN_PRED &filter, OID *rec_oids,
				 float *distances) override;
    virtual int dump (FILE *fp) override;

    virtual int save (const std::string &path) override;
    virtual int load (const std::string &path) override;

    THREAD_ENTRY *m_thread_p;
    VPID m_root_vpid;
    std::unique_ptr<cubhnsw::algo<cubhnsw::memory_id_traits>> m_algo;
};

// =====================================================================
// hnsw_disk_usearch_backend
// =====================================================================

bool
hnsw_disk_usearch_backend::is_metric_supported (const DB_VECTOR_DISTANCE_METRIC &metric) const
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
hnsw_disk_usearch_backend::create_index (THREAD_ENTRY *thread_p, const BTID *btid, const std::string &name,
    const hnsw_build_params &build_params)
{
  VPID root_vpid = {btid->root_pageid, btid->vfid.volid};
  PAGE_PTR page_ptr = pgbuf_fix (thread_p, &root_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (page_ptr == NULL)
    {
      ASSERT_ERROR ();
      return NULL;
    }

  char rec_buf [IO_MAX_PAGE_SIZE + INT_ALIGNMENT];
  RECDES rec { DB_PAGESIZE, 0, REC_HOME, PTR_ALIGN (rec_buf, INT_ALIGNMENT)};

  BTREE_ROOT_HEADER dummy_header;

  dummy_header.num_oids = build_params.dimension;
  dummy_header.num_keys = build_params.m;
  dummy_header.num_nulls = build_params.ef_construction;
  dummy_header.unique_pk = build_params.metric;

  COPY_OID (& (dummy_header.topclass_oid), &oid_Null_oid);

  VFID_SET_NULL (& (dummy_header.ovfid));
  dummy_header.creator_mvccid = MVCCID_NULL;

  dummy_header.node.node_level = 1; // dummy
  dummy_header.node.max_key_len = 0; // dummy

  /* pack header's content */
  /*
  or_init (&buf, rec.data, rec.area_size);

  or_put_int (&buf, build_params.dimension);
  or_put_int (&buf, build_params.m);
  or_put_int (&buf, build_params.ef_construction);
  or_put_int (&buf, (int) build_params.metric);
  */
  int fixed_size = offsetof (BTREE_ROOT_HEADER, packed_key_domain);
  memcpy (rec.data, &dummy_header, fixed_size);
  rec.length = fixed_size;

  /* insert the root header information into the root page */
  const int DUMMY_HEADER_SLOT_ID = 0;
  if (spage_insert_at (thread_p, page_ptr, DUMMY_HEADER_SLOT_ID, &rec) != SP_SUCCESS)
    {
      return NULL;
    }

  pgbuf_unfix_and_init_after_check(thread_p, page_ptr);

  log_sysop_attach_to_outer (thread_p);
  vacuum_log_add_dropped_file (thread_p, &btid->vfid, NULL, VACUUM_LOG_ADD_DROPPED_FILE_UNDO);

  std::unique_ptr<cubhnsw::algo<cubhnsw::memory_id_traits>> algo = std::make_unique<cubhnsw::algo<cubhnsw::memory_id_traits>>(thread_p, build_params);
  hnsw_index *index = new hnsw_disk_usearch (*this, *btid, name, build_params, std::move (algo));
  return index;
}


// =====================================================================
// hnsw_disk_usearch
// =====================================================================

hnsw_disk_usearch::hnsw_disk_usearch (hnsw_index_backend &backend, const BTID &btid, const std::string &name,
				      const hnsw_build_params &build_params, std::unique_ptr<cubhnsw::algo<cubhnsw::memory_id_traits>> algo)
  : hnsw_index (backend, btid, name, build_params)
{
  m_root_vpid = { btid.root_pageid, btid.vfid.volid};
  m_algo = std::move (algo);
}

int
hnsw_disk_usearch::prepare_to_add (int n_vectors, const OID *oid, const float *vector)
{
  // do nothing
  return NO_ERROR;
}

int
hnsw_disk_usearch::add (int n_vectors, const OID *oid, const float *vector)
{
  for (int i = 0; i < n_vectors; ++i)
  {
    m_algo->add (oid[i], vector + i * m_build_params.dimension);
  }
  return NO_ERROR;
}

int
hnsw_disk_usearch::search (const float *query, const int k, const int ef_search, OID *rec_oids, float *distances)
{
  return ER_FAILED;
}

int
hnsw_disk_usearch::remove (const OID *oid)
{
  return ER_FAILED;
}

int
hnsw_disk_usearch::update (const OID *oid, const float *vector)
{
  return ER_FAILED;
}

int
hnsw_disk_usearch::filtered_search (const float *query, const int k, const SCAN_PRED &filter, OID *rec_oids,
				    float *distances)
{
  return ER_FAILED;
}

int
hnsw_disk_usearch::dump (FILE *fp)
{
  return ER_FAILED;
}

int
hnsw_disk_usearch::save (const std::string &path)
{
  return ER_FAILED;
}

int
hnsw_disk_usearch::load (const std::string &path)
{
  return ER_FAILED;
}

HNSW_REGISTER_BACKEND ("usearchng",
  [] (const char *id)
{
return std::make_unique<hnsw_disk_usearch_backend> (id);
});
