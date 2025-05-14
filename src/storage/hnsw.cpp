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
// hnsw.cpp - implementation of HNSW index
//

#include "hnsw.hpp"
#include "error_manager.h"
#include "system_parameter.h"
#include "vector_opfunc.hpp"
#include "dbtype.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#include "strict_warnings_on.hpp"
// TODO : When cub_server terminates, hnsw_index_id will be reset to 0.
//        This is not a problem in current implementation, but it may be a problem in the future,
//        such as duplicate hnsw_index_id when cub_server restarts.
//        We need to consider a better way to identify the hnsw index.
int hnsw_index_id = 0;
std::unordered_map<int, std::unique_ptr<faiss::IndexIDMap>> hnsw_index_map;

static faiss::idx_t encode_oid (const OID &oid);
static OID decode_oid (faiss::idx_t encoded_oid);

BTID *
xhnsw_add_index (THREAD_ENTRY *thread_p, BTID *btid, int dimension = 10, int hnsw_M = 128, int hnsw_efConstruction = 40,
		 enum faiss::MetricType metric_type = faiss::METRIC_L2)
{
  auto hnsw_index = new faiss::IndexHNSWFlat (dimension, hnsw_M, metric_type);
  hnsw_index->hnsw.efConstruction = hnsw_efConstruction;

  auto index = std::make_unique<faiss::IndexIDMap> (hnsw_index);

  btid->vfid.volid = -1;
  btid->vfid.fileid = -1;
  btid->root_pageid = ++hnsw_index_id;

  hnsw_index_map[hnsw_index_id] = std::move (index);
  er_log_debug (ARG_FILE_LINE, "HNSW Index added with ID %d", hnsw_index_id);
  hnsw_print_index_info (btid);

  return btid;
}

int xhnsw_delete_index (THREAD_ENTRY *thread_p, BTID *btid)
{
  if (!btid)
    {
      assert (false);
      return ER_FAILED;
    }

  int hnsw_id = btid->root_pageid;
  auto it = hnsw_index_map.find (hnsw_id);

  if (it == hnsw_index_map.end())
    {
      er_log_debug (ARG_FILE_LINE, "HNSW Index not found with ID %d", hnsw_id);
      // assert (false);
      // return ER_FAILED;
      return NO_ERROR;
    }
  else
    {
      er_log_debug (ARG_FILE_LINE, "HNSW Index deleted with ID %d", hnsw_id);
      hnsw_print_index_info (btid);

      hnsw_index_map.erase (it);
    }

  return NO_ERROR;
}


int hnsw_print_index_info (BTID *btid)
{
  if (!btid)
    {
      return ER_FAILED;
    }

  int hnsw_id = btid->root_pageid;
  auto it = hnsw_index_map.find (hnsw_id);

  if (it == hnsw_index_map.end())
    {
      er_log_debug (ARG_FILE_LINE, "HNSW Index not found with ID %d", hnsw_id);
      return ER_FAILED;
    }

  else
    {
      std::unique_ptr<faiss::IndexIDMap> &index = it->second;
      auto *hnsw_index = static_cast<faiss::IndexHNSWFlat *> (index->index);

      er_log_debug (ARG_FILE_LINE, "HNSW Index Information for ID %d:", hnsw_id);
      er_log_debug (ARG_FILE_LINE, "  - Dimension: %d", index->d);
      er_log_debug (ARG_FILE_LINE, "  - Metric Type: %d", index->metric_type);
      er_log_debug (ARG_FILE_LINE, "  - Total Elements: %d", index->ntotal);
      er_log_debug (ARG_FILE_LINE, "  - HNSW efConstruction: %d", hnsw_index->hnsw.efConstruction);
      er_log_debug (ARG_FILE_LINE, "  - HNSW efSearch: %d", hnsw_index->hnsw.efSearch);
    }

  return NO_ERROR;
}

int hnsw_add_element (BTID *btid, DB_VALUE *key_dbvalue, OID *oid)
{
  int hnsw_id;
  faiss::idx_t encoded_oid = encode_oid (*oid);

  if (!btid)
    {
      assert (false);
      return ER_FAILED;
    }

  const DB_VECTOR_FLOAT *vf = db_get_vector_float (key_dbvalue);

  hnsw_id = btid->root_pageid;

  auto it = hnsw_index_map.find (hnsw_id);

  if (it == hnsw_index_map.end())
    {
      er_log_debug (ARG_FILE_LINE, "HNSW Index not found with ID %d", hnsw_id);
      assert (false);
      return ER_FAILED;
    }

  std::unique_ptr<faiss::IndexIDMap> &index = it->second;

  index->add_with_ids (1, vf->float_array, &encoded_oid);
  er_log_debug (ARG_FILE_LINE, "Added element with ID %lld to HNSW Index.", static_cast<long long> (encoded_oid));

  return NO_ERROR;
}

int hnsw_search_element (int hnsw_id, DB_VALUE *key_dbvalue, int k, OID *rec_oids, float *distances)
{
  const DB_VECTOR_FLOAT *vf = db_get_vector_float (key_dbvalue);

  assert (hnsw_id > 0);

  auto it = hnsw_index_map.find (hnsw_id);

  if (it == hnsw_index_map.end())
    {
      er_log_debug (ARG_FILE_LINE, "HNSW Index not found with ID %d", hnsw_id);
      assert (false);
      return ER_FAILED;
    }

  std::unique_ptr<faiss::IndexIDMap> &index = it->second;

  int64_t *uids = new int64_t[k * 1];
  index->search (1, vf->float_array, k, distances, uids);

  if (uids != nullptr)
    {
      for (int i = 0; i < k; i++)
	{
	  rec_oids[i] = decode_oid (uids[i]);
	}
      delete[] uids;
    }
  return NO_ERROR;
}

static faiss::idx_t encode_oid (const OID &oid)
{
  return (static_cast<int64_t> (oid.pageid) << 32) |
	 (static_cast<uint32_t> (oid.slotid) << 16) |
	 (static_cast<uint16_t> (oid.volid));
}

static OID decode_oid (faiss::idx_t encoded_oid)
{
  OID oid;
  oid.pageid = static_cast<int32_t> (encoded_oid >> 32);
  oid.slotid = static_cast<int16_t> ((encoded_oid >> 16) & 0xFFFF);
  oid.volid = static_cast<int16_t> (encoded_oid & 0xFFFF);

  return oid;
}

#include "strict_warnings_off.hpp"
