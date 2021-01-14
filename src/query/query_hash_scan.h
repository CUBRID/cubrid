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
// query_hash_scan - interface for hash list scan during queries
//

#ifndef _QUERY_HASH_SCAN_H_
#define _QUERY_HASH_SCAN_H_

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Wrong module
#endif // not server and not SA mode

#include "regu_var.hpp"

/* hash scan value */
typedef struct hash_scan_value HASH_SCAN_VALUE;
struct hash_scan_value
{
  QFILE_TUPLE tuple;		/* tuple */
};

/* hash scan key */
typedef struct hash_scan_key HASH_SCAN_KEY;
struct hash_scan_key
{
  int val_count;		/* key size */
  bool free_values;		/* true if values need to be freed */
  db_value **values;		/* value array */
};

/* hash list scan */
typedef struct hash_list_scan HASH_LIST_SCAN;
struct hash_list_scan
{
  bool hash_list_scan_yn;	/* Is hash list scan possible? */
  regu_variable_list_node *build_regu_list;	/* regulator variable list */
  regu_variable_list_node *probe_regu_list;	/* regulator variable list */
  mht_table *hash_table;	/* memory hash table for hash list scan */
  hash_scan_key *temp_key;	/* temp probe key */
  HENTRY_PTR curr_hash_entry;	/* current hash entry */
};

HASH_SCAN_KEY *qdata_alloc_hscan_key (THREAD_ENTRY * thread_p, int val_cnt, bool alloc_vals);
HASH_SCAN_VALUE *qdata_alloc_hscan_value (THREAD_ENTRY * thread_p, QFILE_TUPLE tpl);

void qdata_free_hscan_key (THREAD_ENTRY * thread_p, HASH_SCAN_KEY * key, int val_count);
void qdata_free_hscan_value (THREAD_ENTRY * thread_p, HASH_SCAN_VALUE * value);
int qdata_free_hscan_entry (const void *key, void *data, void *args);

int qdata_hscan_key_eq (const void *key1, const void *key2);

int qdata_build_hscan_key (THREAD_ENTRY * thread_p, val_descr * vd, REGU_VARIABLE_LIST regu_list, HASH_SCAN_KEY * key);
unsigned int qdata_hash_scan_key (const void *key, unsigned int ht_size);
HASH_SCAN_KEY *qdata_copy_hscan_key (THREAD_ENTRY * thread_p, HASH_SCAN_KEY * key,
				     REGU_VARIABLE_LIST probe_regu_list, val_descr * vd);

int qdata_print_hash_scan_entry (THREAD_ENTRY * thread_p, FILE * fp, const void *key, void *data, void *args);

#endif /* _QUERY_HASH_SCAN_H_ */
