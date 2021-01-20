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

#define MAKE_TUPLE_POSTION(tuple_pos, simple_pos, scan_id_p) \
  do \
    { \
      tuple_pos.status = scan_id_p->status; \
      tuple_pos.position = S_ON; \
      tuple_pos.vpid = simple_pos->vpid; \
      tuple_pos.offset = simple_pos->offset; \
      tuple_pos.tpl = NULL; \
      tuple_pos.tplno = 0; /* If tplno is needed, add it from scan_build_hash_list_scan() */ \
    } \
  while (0)

/* kind of hash list scan method */
enum hash_method
{
  NOT_USE = 0,
  IN_MEMORY = 1,
  HYBRID_IN_MEMORY = 2,
  HASH_TEMP_FILE = 3 /* not used */
};
typedef enum hash_method HASH_METHOD;

/* Tuple position structure for hash value */
typedef struct qfile_tuple_simple_pos QFILE_TUPLE_SIMPLE_POS;
struct qfile_tuple_simple_pos
{
  VPID vpid;			/* Real tuple page identifier */
  int offset;			/* Tuple offset inside the page */
};

/* hash scan value */
typedef union hash_scan_value HASH_SCAN_VALUE;
union hash_scan_value
{
  void *data;			/* for free() */
  QFILE_TUPLE_SIMPLE_POS *pos;	/* tuple position of temp file */
  QFILE_TUPLE tuple;		/* tuple data */
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
  regu_variable_list_node *build_regu_list;	/* regulator variable list */
  regu_variable_list_node *probe_regu_list;	/* regulator variable list */
  mht_hls_table *hash_table;	/* memory hash table for hash list scan */
  hash_scan_key *temp_key;	/* temp probe key */
  hash_scan_key *temp_new_key;	/* temp probe key with db_value */
  HENTRY_HLS_PTR curr_hash_entry;	/* current hash entry */
  int hash_list_scan_yn;	/* Is hash list scan possible? */
  bool need_coerce_type;	/* Are the types of probe and build different? */
};

HASH_SCAN_KEY *qdata_alloc_hscan_key (THREAD_ENTRY * thread_p, int val_cnt, bool alloc_vals);
HASH_SCAN_VALUE *qdata_alloc_hscan_value (THREAD_ENTRY * thread_p, QFILE_TUPLE tpl);
HASH_SCAN_VALUE *qdata_alloc_hscan_value_OID (THREAD_ENTRY * thread_p, QFILE_LIST_SCAN_ID * scan_id_p);

void qdata_free_hscan_key (THREAD_ENTRY * thread_p, HASH_SCAN_KEY * key, int val_count);
void qdata_free_hscan_value (THREAD_ENTRY * thread_p, HASH_SCAN_VALUE * value);
int qdata_free_hscan_entry (const void *key, void *data, void *args);

int qdata_hscan_key_eq (const void *key1, const void *key2);

int qdata_build_hscan_key (THREAD_ENTRY * thread_p, val_descr * vd, REGU_VARIABLE_LIST regu_list, HASH_SCAN_KEY * key);
unsigned int qdata_hash_scan_key (const void *key, unsigned int ht_size);
HASH_SCAN_KEY *qdata_copy_hscan_key (THREAD_ENTRY * thread_p, HASH_SCAN_KEY * key,
				     REGU_VARIABLE_LIST probe_regu_list, val_descr * vd);
HASH_SCAN_KEY *qdata_copy_hscan_key_without_alloc (THREAD_ENTRY * thread_p, HASH_SCAN_KEY * key,
						   REGU_VARIABLE_LIST probe_regu_list, HASH_SCAN_KEY * new_key);

int qdata_print_hash_scan_entry (THREAD_ENTRY * thread_p, FILE * fp, const void *data, void *args);

#endif /* _QUERY_HASH_SCAN_H_ */
