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

// TODO : When cub_server terminates, hnsw_index_id will be reset to 0.
//        This is not a problem in current implementation, but it may be a problem in the future,
//        such as duplicate hnsw_index_id when cub_server restarts.
//        We need to consider a better way to identify the hnsw index.
int hnsw_index_id = 0;
std::unordered_map<int, std::unique_ptr<faiss::IndexHNSW>> hnsw_index_map;

int hnsw_add_index (BTID *btid, int dimension = 10, int hnsw_M = 128, int hnsw_efConstruction = 40,
		    enum faiss::MetricType metric_type = faiss::METRIC_L2)
{
  std::unique_ptr<faiss::IndexHNSW> index = std::make_unique<faiss::IndexHNSW> (dimension, hnsw_M, metric_type);
  index->hnsw.efConstruction = hnsw_efConstruction;

  btid->vfid.volid = -1;
  btid->vfid.fileid = -1;
  btid->root_pageid = ++hnsw_index_id;

  hnsw_index_map[hnsw_index_id] = std::move (index);
  return NO_ERROR;
}

int hnsw_delete_index (BTID *btid)
{
  if (!btid)
    {
      return ER_FAILED;
    }

  int hnsw_id = btid->root_pageid;
  auto it = hnsw_index_map.find (hnsw_id);

  if (it == hnsw_index_map.end())
    {
      return ER_FAILED;
    }
  else
    {
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
      return ER_FAILED;
    }

  else
    {
      std::unique_ptr<faiss::IndexHNSW> &index = it->second;

      er_log_debug (ARG_FILE_LINE, "HNSW Index Information for ID %d:", hnsw_id);
      er_log_debug (ARG_FILE_LINE, "  - Dimension: %d", index->d);
      er_log_debug (ARG_FILE_LINE, "  - Metric Type: %d", index->metric_type);
      er_log_debug (ARG_FILE_LINE, "  - Total Elements: %d", index->ntotal);
      er_log_debug (ARG_FILE_LINE, "  - HNSW efConstruction: %d", index->hnsw.efConstruction);
      er_log_debug (ARG_FILE_LINE, "  - HNSW efSearch: %d", index->hnsw.efSearch);
    }

  return NO_ERROR;
}
