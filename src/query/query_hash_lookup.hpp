/*
 * Copyright 2008 Search Solution Corporation
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
 * query_hash_lookup.hpp - the hash over a list subquery's values that answers the equality lookups of a predicate
 *			 (IN, NOT IN, = SOME, <> ALL) in place of reading the whole list
 */

#ifndef _QUERY_HASH_LOOKUP_HPP_
#define _QUERY_HASH_LOOKUP_HPP_

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs to server module
#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

#include "dbtype_def.h"
#include "storage_common.h"
#include "thread_compat.hpp"

struct DOMAIN_COMPARE;
struct qfile_list_id;
struct xasl_node;

typedef struct hash_lookup HASH_LOOKUP;

extern HASH_LOOKUP *hash_lookup_create (THREAD_ENTRY *thread_p);
extern void hash_lookup_destroy (THREAD_ENTRY *thread_p, HASH_LOOKUP *hash);
extern void hash_lookup_destroy_tree (THREAD_ENTRY *thread_p, xasl_node *xasl);

extern int hash_lookup_get (THREAD_ENTRY *thread_p, xasl_node *subquery, qfile_list_id *list_id,
			    const DOMAIN_COMPARE *compare, HASH_LOOKUP **hash);
extern void hash_lookup_count_read (xasl_node *subquery, INT64 rows);
extern bool hash_lookup_has_null (const HASH_LOOKUP *hash);
extern bool hash_lookup_start (HASH_LOOKUP *hash, const DB_VALUE *item);
extern SCAN_CODE hash_lookup_next (THREAD_ENTRY *thread_p, HASH_LOOKUP *hash, DB_VALUE **value);
extern void hash_lookup_end (THREAD_ENTRY *thread_p, HASH_LOOKUP *hash);

#endif /* _QUERY_HASH_LOOKUP_HPP_ */
