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
#include "hnsw_api.hpp"

int xhnsw_initialize (THREAD_ENTRY *thread_p);
int xhnsw_finalize (THREAD_ENTRY *thread_p);

int xhnsw_add_index (THREAD_ENTRY *thread_p, const hnsw_build_params &params, BTID &btid_out);
int xhnsw_delete_index (THREAD_ENTRY *thread_p, BTID *btid);
int xhnsw_load_index (THREAD_ENTRY *thread_p, BTID *btid, OID *oid, int n_classes, int n_attrs, int *attr_ids,
		      HFID *hfids, const hnsw_build_params &params);

int hnsw_add_element (THREAD_ENTRY *thread_p, BTID *btid, OID *oid, float *vector, int n_vectors);
int hnsw_search_element (THREAD_ENTRY *thread_p, BTID *btid, DB_VALUE *key_dbvalue, int k, OID *rec_oids,
			 float *distances);

#endif
