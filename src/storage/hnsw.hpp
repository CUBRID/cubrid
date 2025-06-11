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

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs to server module
#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

#include <unordered_map>
#include <memory>

#include "storage_common.h"
#include "dbtype_def.h"
#include "thread_compat.hpp"

BTID *xhnsw_add_index (THREAD_ENTRY *thread_p, BTID *btid, int dimension, int hnsw_M, int hnsw_efConstruction,
		       int metric);
int xhnsw_delete_index (THREAD_ENTRY *thread_p, BTID *btid);
int hnsw_print_index_info (BTID *btid);

int hnsw_search_element (int hnsw_id, DB_VALUE *key_dbvalue, int k, OID *rec_oids, float *distances);
int hnsw_add_element (BTID *btid, OID *oid, DB_VALUE *key_dbvalue);
void dump_all_hnsw_indices_to_files ();

#endif
