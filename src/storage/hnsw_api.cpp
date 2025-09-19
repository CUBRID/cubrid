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

#include <fstream>

#include "boot_sr.h"
#include "hnsw_api.hpp"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace hnsw_backend_registry
{
  static std::unordered_map<std::string, hnsw_backend_factory_fn> &map_ref()
  {
    static std::unordered_map<std::string, hnsw_backend_factory_fn> m;
    return m;
  }

  std::unordered_map<std::string, hnsw_backend_factory_fn> &factories()
  {
    return map_ref();
  }

  void register_factory (std::string id, hnsw_backend_factory_fn fn)
  {
    auto &m = map_ref();
    if (!m.empty())
      {
	auto it = m.find (id);
	if (it == m.end())
	  {
	    assert (false && "Only one HNSW backend allowed in this build.");
	    return;
	  }
	it->second = std::move (fn);
	return;
      }
    m.emplace (std::move (id), std::move (fn));
  }
}

hnsw_index_manager::hnsw_index_manager()
{
  auto &f = ::hnsw_backend_registry::factories();
  if (!f.empty())
    {
      assert (f.size() == 1 && "Only one HNSW backend factory must be registered.");
      auto it = f.begin();
      auto id = it->first;
      auto &fn = it->second;
      register_backend (fn (*this)); // unique_ptr<hnsw_index_backend>
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

hnsw_index_backend *hnsw_index_manager::get_backend ()
{
  return m_backend.get();
}
// ====================
// hnsw_index_backend
// ====================

std::string
hnsw_index_backend::get_id() const
{
  return m_id;
}

// ====================
// hnsw_index
// ====================

hnsw_index::hnsw_index (hnsw_index_backend &backend, const BTID &btid, const std::string &name,
			const hnsw_build_params &build_params)
  : m_backend (backend), m_btid (btid), m_name (name), m_build_params (build_params)

{}

const BTID &
hnsw_index::get_id() const
{
  return m_btid;
}

const std::string
hnsw_index::get_name() const
{
  return m_name;
}

DB_VECTOR_DISTANCE_METRIC
hnsw_index::get_metric() const
{
  return m_build_params.metric;
}

int
hnsw_index::get_dimension() const
{
  return m_build_params.dimension;
}

int
hnsw_index::get_ef_construction() const
{
  return m_build_params.ef_construction;
}

const hnsw_build_params &
hnsw_index::get_build_params() const
{
  return m_build_params;
}

const hnsw_index_backend &
hnsw_index::get_backend() const
{
  return m_backend;
}

// ====================
// hnsw_oid_encoder_default
// ====================

int64_t
hnsw_oid_encoder_default::encode_oid (const OID &oid)
{
  return (static_cast<int64_t> (oid.pageid) << 32) |
	 (static_cast<uint32_t> (oid.slotid) << 16) |
	 (static_cast<uint16_t> (oid.volid));
}

OID
hnsw_oid_encoder_default::decode_oid (const int64_t &id)
{
  OID oid;
  oid.pageid = static_cast<int32_t> (id >> 32);
  oid.slotid = static_cast<int16_t> ((id >> 16) & 0xFFFF);
  oid.volid = static_cast<int16_t> (id & 0xFFFF);

  return oid;
}
