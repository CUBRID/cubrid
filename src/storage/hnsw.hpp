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
 * hnsw.hpp -
 */

#ifndef _HNSW_HPP_
#define _HNSW_HPP_

#include <unordered_map>
#include <memory>

#include "storage_common.h"
#include "faiss/IndexHNSW.h"

int hnsw_add_index (BTID *btid, int dimension, int hnsw_M, int hnsw_efConstruction, enum faiss::MetricType metric_type);
int hnsw_delete_index (BTID *btid);

extern int hnsw_index_id;
extern std::unordered_map<int, std::unique_ptr<faiss::IndexHNSW>> hnsw_index_map;
#endif
