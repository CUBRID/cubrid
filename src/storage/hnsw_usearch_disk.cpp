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

#include "vector_distance_enum.h"

#include <usearch/index.hpp>
#include <usearch/index_dense.hpp>
#include <usearch/index_plugins.hpp>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

using namespace unum;

class hnsw_disk_usearch_backend final:public hnsw_index_backend
{
  public:
    hnsw_disk_usearch_backend (const std::string &id):hnsw_index_backend (id) {}
    ~hnsw_disk_usearch_backend () override = default;

    virtual bool is_metric_supported (const DB_VECTOR_DISTANCE_METRIC &metric) const override;
    virtual bool is_disk_index () const override { return true; }

    virtual hnsw_index *create_index (THREAD_ENTRY *thread_p, const BTID *btid, const std::string &name,
				      const hnsw_build_params &build_params) override;
    virtual int drop_index (THREAD_ENTRY *thread_p, const BTID *btid) override;

  private:
    usearch::metric_kind_t to_usearch_metric_kind (const DB_VECTOR_DISTANCE_METRIC &metric) const;
};

class hnsw_disk_usearch final:public hnsw_index
{
  public:

    hnsw_disk_usearch (hnsw_index_backend &backend, const BTID &btid, const std::string &name,
		       const hnsw_build_params &build_params);
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
};

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

usearch::metric_kind_t
hnsw_disk_usearch_backend::to_usearch_metric_kind (const DB_VECTOR_DISTANCE_METRIC &metric) const
{
  switch (metric)
    {
    case METRIC_COSINE:
      return usearch::metric_kind_t::cos_k;
    case METRIC_DOT:
      return usearch::metric_kind_t::ip_k;
    case METRIC_EUCLIDEAN:
      return usearch::metric_kind_t::l2sq_k;
    default:
      assert (false);
      return usearch::metric_kind_t::unknown_k;
    }
}

hnsw_index *
hnsw_disk_usearch_backend::create_index (THREAD_ENTRY *thread_p, const BTID *btid, const std::string &name,
				    const hnsw_build_params &build_params)
{
  usearch::metric_kind_t metric_kind = to_usearch_metric_kind (build_params.metric);

  


  
  // TODO
  //

  usearch::metric_punned_t metric_punned (static_cast <std::size_t> (build_params.dimension), metric_kind,
					  usearch::scalar_kind_t::f32_k);

  usearch::index_dense_config_t config;
  config.connectivity = build_params.m;
  config.expansion_add = build_params.ef_construction;

  auto make_result = usearch::index_dense_t::make (metric_punned, config);
  if (!make_result)
    {
      return nullptr;
    }
  std::unique_ptr<usearch::index_dense_t> usearch_index = std::make_unique<usearch::index_dense_t> (std::move (
	      make_result));

  const int initial_size = 1024;
  usearch_index->reserve (initial_size);
  hnsw_index *index = new hnsw_index_usearch (*this, *btid, name, build_params, std::move (usearch_index));
  return index;
}


