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

/*
 * hnsw_api.hpp -
 */

#ifndef _HNSW_API_HPP_
#define _HNSW_API_HPP_

#include <unordered_map>
#include <memory>
#include <filesystem>
#include <cassert>

#include "storage_common.h"
#include "dbtype_def.h"
#include "vector_distance_enum.h"
#include "thread_compat.hpp"
#include "query_evaluator.h"
#include "btid_hash.hpp"

// forward declarations
class hnsw_index_backend;
class hnsw_index;
class hnsw_index_manager;

struct hnsw_build_params
{
  int dimension;
  int m;
  int ef_construction;
  DB_VECTOR_DISTANCE_METRIC metric;

  hnsw_build_params() : dimension (10), m (16), ef_construction (64),
    metric (DB_VECTOR_DISTANCE_METRIC::METRIC_EUCLIDEAN) {}
  hnsw_build_params (int dimension, int m, int ef_construction, DB_VECTOR_DISTANCE_METRIC metric) : dimension (dimension),
    m (m), ef_construction (ef_construction), metric (metric) {}
};

struct hnsw_index_meta
{
  std::string backend_id;
  hnsw_build_params build_params;

  friend std::ostream &operator<< (std::ostream &os, const hnsw_index_meta &meta)
  {
    os << meta.backend_id << " "
       << meta.build_params.dimension << " "
       << meta.build_params.m << " "
       << meta.build_params.ef_construction << " "
       << static_cast<int> (meta.build_params.metric);
    return os;
  }

  friend std::istream &operator>> (std::istream &is, hnsw_index_meta &meta)
  {
    int metric_int;
    is >> meta.backend_id
       >> meta.build_params.dimension
       >> meta.build_params.m
       >> meta.build_params.ef_construction
       >> metric_int;
    meta.build_params.metric = static_cast<DB_VECTOR_DISTANCE_METRIC> (metric_int);
    return is;
  }
};

template <typename id_type>
class hnsw_oid_encoder
{
  public:
    virtual ~hnsw_oid_encoder() = default;

    virtual id_type encode_oid (const OID &oid)=0;
    virtual OID decode_oid (const id_type &id)=0;
};

class hnsw_oid_encoder_default: public hnsw_oid_encoder<int64_t>
{
  public:
    int64_t encode_oid (const OID &oid) override;
    OID decode_oid (const int64_t &id) override;
};

using hnsw_backend_factory_fn = std::function<std::unique_ptr<hnsw_index_backend> (hnsw_index_manager &)>;

class hnsw_index_backend
{
  public:
    explicit hnsw_index_backend (hnsw_index_manager &mgr, const std::string &id) : m_id (id), m_mgr (mgr) {}
    virtual ~hnsw_index_backend() = default;

    hnsw_index_backend (const hnsw_index_backend &) = delete;
    hnsw_index_backend &operator= (const hnsw_index_backend &) = delete;
    hnsw_index_backend (hnsw_index_backend &&) = delete;
    hnsw_index_backend &operator= (hnsw_index_backend &&) = delete;

    virtual std::string get_id() const;
    virtual bool is_metric_supported (const DB_VECTOR_DISTANCE_METRIC &metric) const = 0;

    virtual hnsw_index *create_index (THREAD_ENTRY *thread_p, const BTID *btid, const std::string &name,
				      const hnsw_build_params &build_params) = 0;
    virtual int drop_index (THREAD_ENTRY *thread_p, const BTID *btid) = 0;

  private:
    const std::string m_id;
    const hnsw_index_manager &m_mgr;
};

class hnsw_index
{
  public:
    virtual ~hnsw_index() = default;

    virtual const BTID &get_id() const;
    virtual const std::string get_name() const;
    virtual DB_VECTOR_DISTANCE_METRIC get_metric() const;
    virtual int get_dimension() const;
    virtual int get_ef_construction() const;
    virtual const hnsw_build_params &get_build_params() const;

    virtual const hnsw_index_backend &get_backend() const;

    // operations
    virtual int prepare_to_add (int n_vectors, const OID *oid, const float *vector)=0;
    virtual int add (int n_vectors, const OID *oid, const float *vector)=0;

    virtual int search (const float *query, const int k, const int ef_search, OID *rec_oids, float *distances)=0;
    virtual int remove (const OID *oid)=0;
    virtual int update (const OID *oid, const float *vector)=0;

    // SCAN_PRED from query_evaluator.h
    virtual int filtered_search (const float *query, const int k, const SCAN_PRED &filter, OID *rec_oids,
				 float *distances)=0;
    virtual int dump (FILE *fp)=0;

    // serialize
    virtual int save (const std::string &path)=0;
    virtual int load (const std::string &path)=0;

  protected:
    hnsw_index (hnsw_index_backend &backend, const BTID &btid, const std::string &name,
		const hnsw_build_params &build_params);

    const hnsw_index_backend &m_backend;
    const BTID m_btid;
    const std::string m_name;
    const hnsw_build_params m_build_params;
};

namespace fs = std::filesystem;

class hnsw_index_manager
{
  public:

    static hnsw_index_manager &instance()
    {
      static hnsw_index_manager inst;
      return inst;
    }

    fs::path get_index_file_path (const std::string &prefix, const BTID *btid) const;
    fs::path get_index_meta_file_path (const std::string &prefix, const BTID *btid) const;
    fs::path get_index_directory_path() const;

    void create_index_directory();
    bool is_index_file_exists (const std::string &prefix, const BTID *btid) const;
    bool is_index_meta_file_exists (const std::string &prefix, const BTID *btid) const;

    BTID create_btid (const hnsw_index_backend *backend);

    // index management on memory
    bool is_index_loaded (const BTID *btid) const;
    int add_index (const BTID *btid, hnsw_index *index);
    hnsw_index *get_index (const BTID *btid) const;
    int delete_index (const BTID *btid);

    void print_index_info (const BTID *btid);

    // index management on disk
    int save_index (THREAD_ENTRY *thread_p, hnsw_index *index);
    int load_index (THREAD_ENTRY *thread_p, const BTID *btid, hnsw_index *&index);
    int save_index_meta (THREAD_ENTRY *thread_p, const BTID *btid, const hnsw_index_meta &meta);
    int load_index_meta (THREAD_ENTRY *thread_p, const BTID *btid, hnsw_index_meta &meta);
    int save_all_indices (THREAD_ENTRY *thread_p);
    int delete_index_on_disk (const std::string &prefix, const BTID *btid);

    // backend management
    void register_backend (std::unique_ptr<hnsw_index_backend> backend);
    const hnsw_index_backend *get_backend () const;
    hnsw_index_backend *get_backend ();

    ~hnsw_index_manager() = default;

  private:
    fs::path get_vindex_root_path() const;

    /* singleton */
    hnsw_index_manager();

    hnsw_index_manager (const hnsw_index_manager &) = delete;
    hnsw_index_manager &operator= (const hnsw_index_manager &) = delete;
    hnsw_index_manager (hnsw_index_manager &&) = delete;
    hnsw_index_manager &operator= (hnsw_index_manager &&) = delete;

    /* index directory root path */
    fs::path m_root_path;
    int m_last_index_id;

    std::unordered_map<BTID, std::unique_ptr<hnsw_index>> m_index_map;
    std::unique_ptr<hnsw_index_backend> m_backend;
};

namespace hnsw_backend_registry
{
  std::unordered_map<std::string, hnsw_backend_factory_fn> &factories();
  void register_factory (std::string id, hnsw_backend_factory_fn fn);
}

/* do not use the following macro in the header file */
#define HNSW_REGISTER_BACKEND(ID_STR, FACTORY_LAMBDA)                     \
  inline const bool hnsw_backend_registered = [] {                        \
    hnsw_backend_registry::register_factory(                              \
        std::string(ID_STR), hnsw_backend_factory_fn(FACTORY_LAMBDA));    \
    return true;                                                          \
  }()

#endif
