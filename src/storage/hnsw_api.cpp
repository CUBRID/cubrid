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

#include "boot_sr.h"
#include "hnsw_api.hpp"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

// =====================================================================
// hnsw_backend_registry
// =====================================================================
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
