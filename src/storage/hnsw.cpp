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
// hnsw.cpp - common implementation of HNSW index
//

#include "hnsw.hpp"

#include <fstream>

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
#include "hnsw_api.hpp"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

// =====================================================================
// hnsw_index_manager declaration
// =====================================================================

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

// singleton instances
// TODO: dynamically load backend implementations
static hnsw_index_manager *index_manager = nullptr;

// =====================================================================
// high-level APIs
// =====================================================================

int
xhnsw_initialize (THREAD_ENTRY *thread_p)
{
  index_manager = &hnsw_index_manager::instance();
  index_manager->create_index_directory();

  return NO_ERROR;
}

int
xhnsw_finalize (THREAD_ENTRY *thread_p)
{
  index_manager->save_all_indices (thread_p);
  return NO_ERROR;
}

int
xhnsw_add_index (THREAD_ENTRY *thread_p, const hnsw_build_params &params, BTID &btid_out)
{
  hnsw_index_backend *backend_instance = index_manager->get_backend ();
  if (!backend_instance)
    {
      assert (false);
      return ER_FAILED;
    }

  bool is_metric_supported = backend_instance->is_metric_supported (params.metric);
  if (!is_metric_supported)
    {
      ASSERT_CUBVEC (false);
      return ER_FAILED;
    }

  btid_out = index_manager->create_btid (backend_instance);

  hnsw_index *index = backend_instance->create_index (thread_p, &btid_out, "", params);
  if (index == nullptr)
    {
      // failed to create index
      assert (false);
      return ER_FAILED;
    }

  int error = index_manager->add_index (&btid_out, index);
  if (error != NO_ERROR)
    {
      // failed to add index
      assert (false);
      return ER_FAILED;
    }

#if !defined(NDEBUG)
  _er_log_debug (ARG_FILE_LINE, "HNSW Index added with ID %d", btid_out.root_pageid);

  index_manager->print_index_info (&btid_out);
#endif

  return error;
}

int
xhnsw_delete_index (THREAD_ENTRY *thread_p, BTID *btid)
{
  return index_manager->delete_index_on_disk (index_manager->get_backend()->get_id(), btid);
}

int
xhnsw_load_index (THREAD_ENTRY *thread_p, BTID *btid, OID *oid, int n_classes, int n_attrs, int *attr_ids,
		  HFID *hfids, const hnsw_build_params &params)
{
  HEAP_SCANCACHE scan_cache;
  SCAN_CODE scan_result;
  RECDES in_recdes;
  DB_VALUE *key_dbvalue;
  HEAP_CACHE_ATTRINFO attr_info;
  OID cur_oid;
  int cur_class = 0;
  int attr_offset = 0;
  OID_SET_NULL (&cur_oid);
  BTID new_btid;

  if (xhnsw_add_index (thread_p, params, new_btid) != NO_ERROR)
    {
      assert (false);
      return ER_FAILED;
    }

  while (cur_class < n_classes && HFID_IS_NULL (&hfids[cur_class]))
    {
      cur_class++;
    }

  if (heap_scancache_start (thread_p, &scan_cache, &hfids[cur_class], &oid[cur_class], true, NULL) != NO_ERROR)
    {
      return ER_FAILED;
    }

  attr_offset = cur_class * n_attrs;

  if (heap_attrinfo_start (thread_p, &oid[cur_class], n_attrs, &attr_ids[attr_offset], &attr_info) != NO_ERROR)
    {
      (void) heap_scancache_end (thread_p, &scan_cache);
      return ER_FAILED;
    }

  /* -------- Batch buffers --------
  - oids:    growable array of OID (count elements)
  - vectors: contiguous float buffer of size (capacity * dimension)
  */
  int dimension = params.dimension;
  int capacity = 1024;
  int count = 0;
  OID *oids = (OID *) malloc ((size_t) capacity * sizeof (OID));
  float *vectors = (float *) malloc ((size_t) capacity * (size_t) dimension * sizeof (float));
  if (oids == NULL || vectors == NULL)
    {
      if (oids)
	{
	  free (oids);
	}
      if (vectors)
	{
	  free (vectors);
	}
      heap_attrinfo_end (thread_p, &attr_info);
      (void) heap_scancache_end (thread_p, &scan_cache);
      return ER_FAILED;
    }

  auto ensure_capacity = [&] (void) -> bool
  {
    if (count < capacity)
      {
	return true;
      }
    int new_cap = capacity * 2;
    OID *new_oids = (OID *) realloc (oids, (size_t) new_cap * sizeof (OID));
    float *new_vectors = (float *) realloc (vectors, (size_t) new_cap * (size_t) dimension * sizeof (float));
    if (new_oids == NULL || new_vectors == NULL)
      {
	if (new_oids)
	  {
	    oids = new_oids;
	  }
	if (new_vectors)
	  {
	    vectors = new_vectors;
	  }
	return false;
      }
    oids = new_oids;
    vectors = new_vectors;
    capacity = new_cap;
    return true;
  };

  do
    {
      scan_result = heap_next (thread_p, &hfids[cur_class], &oid[cur_class], &cur_oid,
			       &in_recdes, &scan_cache,
			       scan_cache.cache_last_fix_page ? PEEK : COPY);

      switch (scan_result)
	{
	case S_SUCCESS:
	  heap_attrinfo_read_dbvalues (thread_p, &cur_oid, &in_recdes, &attr_info);

	  key_dbvalue = &attr_info.values[0].dbvalue;
	  assert (db_value_type (key_dbvalue) == DB_TYPE_VECTOR);

	  {
	    const DB_VECTOR_FLOAT *vf = db_get_vector_float (key_dbvalue);
	    assert (vf != NULL && vf->dim == dimension);

	    if (!ensure_capacity ())
	      {
		if (oids)
		  {
		    free (oids);
		  }
		if (vectors)
		  {
		    free (vectors);
		  }
		heap_attrinfo_end (thread_p, &attr_info);
		(void) heap_scancache_end (thread_p, &scan_cache);
		return ER_FAILED;
	      }

	    oids[count] = cur_oid;
	    float *dst = vectors + ((size_t) count * (size_t) dimension);
	    memcpy (dst, vf->float_array, (size_t) dimension * sizeof (float));

	    count++;
	  }
	  continue;

	case S_END:
	{
	  hnsw_add_element (thread_p, &new_btid, oids, vectors, count);

	  if (oids)
	    {
	      free (oids);
	    }
	  if (vectors)
	    {
	      free (vectors);
	    }

	  heap_attrinfo_end (thread_p, &attr_info);
	  (void) heap_scancache_end (thread_p, &scan_cache);

	  *btid = new_btid;
	  return NO_ERROR;
	}

	default:
	  if (oids)
	    {
	      free (oids);
	    }
	  if (vectors)
	    {
	      free (vectors);
	    }
	  heap_attrinfo_end (thread_p, &attr_info);
	  (void) heap_scancache_end (thread_p, &scan_cache);
	  assert (false);
	  return ER_FAILED;
	}
    }
  while (true);

  return NO_ERROR;
}

int
hnsw_add_element (THREAD_ENTRY *thread_p, BTID *btid, OID *oid, float *vector, int n_vectors)
{
  assert (oid);
  assert (vector);
  assert (n_vectors > 0);

  if (!btid)
    {
      assert (false);
      return ER_FAILED;
    }

  auto *index = index_manager->get_index (btid);
  if (index == nullptr)
    {
      if (index_manager->load_index (thread_p, btid, index) != NO_ERROR)
	{
	  // failed to load index
	  assert (false);
	  return ER_FAILED;
	}
    }

  if (index->prepare_to_add (n_vectors, oid, vector) != NO_ERROR)
    {
      assert (false);
      return ER_FAILED;
    }

  return index->add (n_vectors, oid, vector);
}

int
hnsw_search_element (THREAD_ENTRY *thread_p, BTID *btid, DB_VALUE *key_dbvalue, int k, OID *rec_oids, float *distances)
{
  assert (btid);
  assert (key_dbvalue);
  assert (rec_oids);
  assert (distances);
  assert (k > 0);

  hnsw_index *index = index_manager->get_index (btid);
  if (index == nullptr)
    {
      if (index_manager->load_index (thread_p, btid, index) != NO_ERROR)
	{
	  // failed to load index
	  assert (false);
	  return ER_FAILED;
	}
    }

  const DB_VECTOR_FLOAT *vf = db_get_vector_float (key_dbvalue);
  assert (vf != NULL && vf->dim == index->get_dimension());

  int ef_search = prm_get_integer_value (PRM_ID_VECTOR_INDEX_EF_SEARCH);
  return index->search (vf->float_array, k, ef_search, rec_oids, distances);
}

// =====================================================================
// hnsw_index_manager implementation
// =====================================================================

hnsw_index_manager::hnsw_index_manager()
{
  auto &f = ::hnsw_backend_registry::factories();
  if (!f.empty())
    {
      assert (f.size() == 1 && "Only one HNSW backend factory must be registered.");
      auto it = f.begin();
      auto id = it->first;
      auto &fn = it->second;
      register_backend (fn ()); // unique_ptr<hnsw_index_backend>
      assert (m_backend != nullptr && "Only one HNSW backend instance must exist.");
    }

}

fs::path
hnsw_index_manager::get_vindex_root_path() const
{
  char db_path[PATH_MAX];
  fileio_get_directory_path (db_path, boot_db_full_name());
  fs::path root_path = fs::path (db_path) / "vindex";

  return root_path;
}

void
hnsw_index_manager::create_index_directory()
{
  if (m_root_path.empty())
    {
      m_root_path = get_vindex_root_path();
    }

  if (!fs::exists (m_root_path))
    {
      fs::create_directory (m_root_path);
    }
}

fs::path
hnsw_index_manager::get_index_file_path (const std::string &prefix, const BTID *btid) const
{
  return m_root_path / (prefix + "_" + std::to_string (btid->root_pageid) + ".bin");
}

fs::path
hnsw_index_manager::get_index_meta_file_path (const std::string &prefix, const BTID *btid) const
{
  return m_root_path / (prefix + "_" + std::to_string (btid->root_pageid) + ".meta");
}

fs::path
hnsw_index_manager::get_index_directory_path() const
{
  return m_root_path;
}

bool
hnsw_index_manager::is_index_file_exists (const std::string &prefix, const BTID *btid) const
{
  return fs::exists (get_index_file_path (prefix, btid));
}

bool
hnsw_index_manager::is_index_meta_file_exists (const std::string &prefix, const BTID *btid) const
{
  return fs::exists (get_index_meta_file_path (prefix, btid));
}

bool
hnsw_index_manager::is_index_loaded (const BTID *btid) const
{
  return m_index_map.find (*btid) != m_index_map.end();
}

int
hnsw_index_manager::add_index (const BTID *btid, hnsw_index *index)
{
  if (is_index_loaded (btid))
    {
      assert (false);
      _er_log_debug (ARG_FILE_LINE, "HNSW Index already exists with ID %d", btid->root_pageid);
      return ER_FAILED;
    }

  m_index_map[*btid] = std::unique_ptr<hnsw_index> (index);
  return NO_ERROR;
}

hnsw_index *
hnsw_index_manager::get_index (const BTID *btid) const
{
  if (is_index_loaded (btid))
    {
      return m_index_map.at (*btid).get();
    }
  return nullptr;
}

int
hnsw_index_manager::delete_index (const BTID *btid)
{
  m_index_map.erase (*btid);
  return NO_ERROR;
}

void
hnsw_index_manager::print_index_info (const BTID *btid)
{
  if (is_index_loaded (btid))
    {
      m_index_map.at (*btid)->dump (stdout);
    }
}

int
hnsw_index_manager::save_index_meta (THREAD_ENTRY *thread_p, const BTID *btid, const hnsw_index_meta &meta)
{
  // if meta file exists, do not overwrite it
  if (is_index_meta_file_exists (meta.backend_id, btid))
    {
      return NO_ERROR;
    }

  // Write meta to file in text mode (with newline) and check for errors
  const auto meta_path = get_index_meta_file_path (meta.backend_id, btid);
  std::ofstream meta_file (meta_path, std::ios::out | std::ios::trunc);
  if (!meta_file)
    {
      // Could not open file for writing
      return ER_FAILED;
    }
  meta_file << meta << std::endl;
  if (!meta_file)
    {
      // Write failed
      return ER_FAILED;
    }
  return NO_ERROR;
}

int
hnsw_index_manager::load_index_meta (THREAD_ENTRY *thread_p, const BTID *btid, hnsw_index_meta &meta)
{
  // The backend_id is not known before reading the file, so we must try all possible meta files.
  // For now, try all registered backends.
  for (const auto &pair : hnsw_backend_registry::factories())
    {
      const std::string &backend_id = pair.first;
      const auto meta_path = get_index_meta_file_path (backend_id, btid);
      std::ifstream meta_file (meta_path, std::ios::in);
      if (!meta_file.is_open())
	{
	  continue;
	}
      hnsw_index_meta temp_meta;
      meta_file >> temp_meta;
      if (!meta_file)
	{
	  meta_file.close();
	  continue;
	}
      meta_file.close();
      // Found and successfully read meta
      meta = temp_meta;
      return NO_ERROR;
    }
  // Could not find or read any meta file
  return ER_FAILED;
}

int
hnsw_index_manager::save_all_indices (THREAD_ENTRY *thread_p)
{
  for (const auto &pair : m_index_map)
    {
      const BTID *btid = &pair.first;

      hnsw_index_meta meta;
      meta.backend_id = pair.second->get_backend().get_id();
      meta.build_params = pair.second->get_build_params();

      save_index_meta (thread_p, btid, meta);
      hnsw_index *index = pair.second.get();
      save_index (thread_p, index);
    }
  return NO_ERROR;
}
int hnsw_index_manager::save_index (THREAD_ENTRY *thread_p, hnsw_index *index)
{
  std::string prefix = index->get_backend().get_id();
  const BTID &btid = index->get_id();
  index->save (get_index_file_path (prefix, &btid).string());
  return NO_ERROR;
}

int hnsw_index_manager::load_index (THREAD_ENTRY *thread_p, const BTID *btid, hnsw_index *&index_out)
{
  if (is_index_loaded (btid))
    {
      return NO_ERROR;
    }

  hnsw_index_meta meta;
  int error = load_index_meta (thread_p, btid, meta);
  if (error != NO_ERROR)
    {
      _er_log_debug (ARG_FILE_LINE, "Failed to load HNSW Index meta with ID %d", btid->root_pageid);
      return error;
    }

  hnsw_index_backend *backend = get_backend ();
  if (backend == nullptr)
    {
      assert (false);
      _er_log_debug (ARG_FILE_LINE, "Failed to load HNSW Index backend with ID %s", meta.backend_id.c_str());
      return ER_FAILED;
    }

  index_out = backend->create_index (thread_p, btid, meta.backend_id, meta.build_params);
  if (!index_out)
    {
      return ER_FAILED;
    }
  if (index_out->load (get_index_file_path (meta.backend_id, btid).string()) != NO_ERROR)
    {
      return ER_FAILED;
    }
  add_index (btid, index_out);
  return NO_ERROR;
}

int hnsw_index_manager::delete_index_on_disk (const std::string &prefix, const BTID *btid)
{
  if (is_index_file_exists (prefix, btid))
    {
      fs::remove (get_index_file_path (prefix, btid));
    }
  if (is_index_meta_file_exists (prefix, btid))
    {
      fs::remove (get_index_meta_file_path (prefix, btid));
    }

  delete_index (btid);

  return NO_ERROR;
}


BTID
hnsw_index_manager::create_btid (const hnsw_index_backend *backend)
{
  BTID btid = {.vfid = VFID_INITIALIZER, .root_pageid = m_last_index_id};
  while (is_index_loaded (&btid) || is_index_meta_file_exists (backend->get_id(), &btid))
    {
      btid.root_pageid = ++m_last_index_id;
    }
  return btid;
}

void hnsw_index_manager::register_backend (std::unique_ptr<hnsw_index_backend> backend)
{
  m_backend = std::move (backend);
}

const hnsw_index_backend *hnsw_index_manager::get_backend () const
{
  return m_backend.get();
}

hnsw_index_backend *
hnsw_index_manager::get_backend ()
{
  return m_backend.get();
}
