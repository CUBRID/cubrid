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
#include "hnsw.hpp"

#include "error_manager.h"
#include "system_parameter.h"
#include "vector_opfunc.hpp"
#include "boot_sr.h"
#include "file_io.h"
#include "system_parameter.h"
#include "dbtype.h"
#include "db_vector.hpp"
#include "porting.h"
#include "vector_distance_enum.h"
#include "heap_file.h"
#include <cstddef>
#include <fstream>
#include <filesystem>

#include <usearch/index.hpp>
#include <usearch/index_dense.hpp>
#include <usearch/index_plugins.hpp>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

using namespace unum;

static hnsw_oid_encoder_default default_oid_encoder;

class hnsw_usearch_backend final : public hnsw_index_backend
{
  public:
    hnsw_usearch_backend (const std::string &id) : hnsw_index_backend (id) {}
    ~hnsw_usearch_backend() override = default;

    virtual bool is_metric_supported (const DB_VECTOR_DISTANCE_METRIC &metric) const override;

    virtual hnsw_index *create_index (THREAD_ENTRY *thread_p, const BTID *btid, const std::string &name,
				      const hnsw_build_params &build_params) override;
    virtual int drop_index (THREAD_ENTRY *thread_p, const BTID *btid) override;

  private:
    usearch::metric_kind_t to_usearch_metric_kind (const DB_VECTOR_DISTANCE_METRIC &metric) const;
};

class hnsw_index_usearch final: public hnsw_index
{
  public:

    hnsw_index_usearch (hnsw_index_backend &backend, const BTID &btid, const std::string &name,
			const hnsw_build_params &build_params, std::unique_ptr<usearch::index_dense_t> index);
    ~hnsw_index_usearch() = default;

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

  private:
    std::mutex m_index_mutex;
    std::unique_ptr<usearch::index_dense_t> m_index;
};

// ===========================
// hnsw_index_backend_usearch
// ===========================

hnsw_index_usearch::hnsw_index_usearch (hnsw_index_backend &backend, const BTID &btid, const std::string &name,
					const hnsw_build_params &build_params, std::unique_ptr<usearch::index_dense_t> index) : hnsw_index (backend, btid, name,
					      build_params)
{
  if (index == nullptr)
    {
      m_index = std::make_unique<usearch::index_dense_t> ();
    }
  else
    {
      m_index = std::move (index);
    }
}

hnsw_index *
hnsw_usearch_backend::create_index (THREAD_ENTRY *thread_p, const BTID *btid, const std::string &name,
				    const hnsw_build_params &build_params)
{
  usearch::metric_kind_t metric_kind = to_usearch_metric_kind (build_params.metric);
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

usearch::metric_kind_t
hnsw_usearch_backend::to_usearch_metric_kind (const DB_VECTOR_DISTANCE_METRIC &metric) const
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

bool
hnsw_usearch_backend::is_metric_supported (const DB_VECTOR_DISTANCE_METRIC &metric) const
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

int
hnsw_usearch_backend::drop_index (THREAD_ENTRY *thread_p, const BTID *btid)
{
  // it is memory only index, so no need to drop
  return NO_ERROR;
}

// ====================
// hnsw_index_usearch
// ====================

int
hnsw_index_usearch::prepare_to_add (int n_vectors, const OID *oid, const float *vector)
{
  std::lock_guard<std::mutex> lock (m_index_mutex);
  size_t need = m_index->size () + static_cast<size_t> (n_vectors);
  m_index->reserve (need + 1024);
  return NO_ERROR;
}

int
hnsw_index_usearch::add (int n_vectors, const OID *oid, const float *vector)
{
  try
    {
      int64_t encoded_oid;
      int dimension = m_index->dimensions();
      for (int i = 0; i < n_vectors; ++i)
	{
	  if (m_build_params.metric == DB_VECTOR_DISTANCE_METRIC::METRIC_COSINE
	      && db_vector_is_all_zeros (vector + i * dimension, dimension))
	    {
	      er_log_debug (ARG_FILE_LINE, "Vector is all zeros, skipping add");
	      continue;
	    }
	  encoded_oid = default_oid_encoder.encode_oid (oid[i]);
	  m_index->add (encoded_oid, vector + i * dimension);
	  er_log_debug (ARG_FILE_LINE, "Added element with OID %lld to HNSW Index ID %d.",
			static_cast<long long> (encoded_oid), m_btid.root_pageid);
	}
    }
  catch (const std::runtime_error &e)
    {
      er_log_debug (ARG_FILE_LINE, "USearch exception during add: %s", e.what());
      return ER_FAILED;
    }
  return NO_ERROR;
}

int
hnsw_index_usearch::search (const float *query, const int k, const int ef_search, OID *rec_oids, float *distances)
{
  if (m_build_params.metric == DB_VECTOR_DISTANCE_METRIC::METRIC_COSINE
      && db_vector_is_all_zeros (query, m_build_params.dimension))
    {
      er_log_debug (ARG_FILE_LINE, "Vector is all zeros, skipping search");
      return NO_ERROR;
    }

  m_index->change_expansion_search (ef_search);
  auto results = m_index->search (query, k);
  for (std::size_t i = 0; i != results.size(); ++i)
    {
      rec_oids[i] = default_oid_encoder.decode_oid (results[i].member.key);
      distances[i] = results[i].distance;
    }
  return NO_ERROR;
}

int
hnsw_index_usearch::remove (const OID *oid)
{
  assert (false);
  return ER_FAILED;
}

int
hnsw_index_usearch::update (const OID *oid, const float *vector)
{
  assert (false);
  return ER_FAILED;
}

int
hnsw_index_usearch::filtered_search (const float *query, const int k, const SCAN_PRED &filter, OID *rec_oids,
				     float *distances)
{
  assert (false);
  return ER_FAILED;
}

int
hnsw_index_usearch::dump (FILE *fp)
{
  std::ostringstream oss;
  oss << "HNSW Index Information for ID: " << m_btid.root_pageid << "\n";
  oss << "  - Dimension: " << m_index->dimensions() << "\n";
  oss << "  - Metric Type: " << metric_kind_name (m_index->metric_kind()) << "\n";
  oss << "  - Total Elements: " << m_index->size() << "\n";
  oss << "  - HNSW M: " << m_index->connectivity() << "\n";
  oss << "  - HNSW efConstruction: " << m_index->expansion_add() << "\n";
  oss << "  - HNSW efSearch: " << m_index->expansion_search() << "\n";

  fprintf (fp, "%s", oss.str().c_str());

  return NO_ERROR;
}

int
hnsw_index_usearch::save (const std::string &path)
{
  m_index->save (path.c_str());
  return NO_ERROR;
}

int
hnsw_index_usearch::load (const std::string &path)
{
  m_index->load (path.c_str());
  return NO_ERROR;
}

HNSW_REGISTER_BACKEND ("usearch",
		       [] (const char *id)
{
  return std::make_unique<hnsw_usearch_backend> (id);
});
