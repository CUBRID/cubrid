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

//
// query_aggregate - interface for executing aggregate function during queries
//

#ifndef _QUERY_AGGREGATE_HPP_
#define _QUERY_AGGREGATE_HPP_

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Wrong module
#endif // not server and not SA mode

#include "external_sort.h"    // SORTKEY_INFO
#include "query_list.h"
#include "storage_common.h"   // AGGREGATE_HASH_STATE, SCAN_CODE, FUNC_TYPE
#include "heap_file.h"

#include <vector>

// forward definitions
struct db_value;
struct mht_table;
struct tp_domain;
struct val_descr;

namespace cubxasl
{
  struct aggregate_accumulator;
  struct aggregate_accumulator_domain;
  struct aggregate_list_node;
} // namespace cubxasl

namespace cubquery
{
  /* aggregate evaluation hash value */
  struct aggregate_hash_value
  {
    int curr_size;		/* last computed size of structure */
    int tuple_count;		/* # of tuples aggregated in structure */
    int func_count;		/* # of functions (i.e. accumulators) */
    cubxasl::aggregate_accumulator *accumulators;	/* function accumulators */
    qfile_tuple_record first_tuple;	/* first aggregated tuple */
  };

  /* aggregate evaluation hash key */
  struct aggregate_hash_key
  {
    int val_count;		/* key size */
    bool free_values;		/* true if values need to be freed */
    db_value **values;		/* value array */
  };


  struct aggregate_hash_context
  {
    /* hash table stuff */
    mht_table *hash_table;	/* memory hash table for hash aggregate eval */
    aggregate_hash_key *temp_key;	/* temporary key used for fetch */
    AGGREGATE_HASH_STATE state;	/* state of hash aggregation */
    tp_domain **key_domains;	/* hash key domains */
    cubxasl::aggregate_accumulator_domain **accumulator_domains;	/* accumulator domains */

    /* runtime statistics stuff */
    int hash_size;		/* hash table size */
    int group_count;		/* groups processed in hash table */
    int tuple_count;		/* tuples processed in hash table */

    /* partial list file stuff */
    SCAN_CODE part_scan_code;	/* scan status of partial list file */
    qfile_list_id *part_list_id;	/* list with partial accumulators */
    qfile_list_id *sorted_part_list_id;	/* sorted list with partial acc's */
    qfile_list_scan_id part_scan_id;	/* scan on partial list */
    db_value *temp_dbval_array;	/* temporary array of dbvalues, used for saving entries to list files */

    /* partial list file sort stuff */
    QFILE_TUPLE_RECORD input_tuple;	/* tuple record used while sorting */
    SORTKEY_INFO sort_key;	/* sort key for partial list */
    RECDES tuple_recdes;		/* tuple recdes */
    aggregate_hash_key *curr_part_key;	/* current partial key */
    aggregate_hash_key *temp_part_key;	/* temporary partial key */
    aggregate_hash_value *curr_part_value;	/* current partial value */
    aggregate_hash_value *temp_part_value;	/* temporary partial value */
    int sorted_count;
  };


  struct hierarchy_aggregate_helper
  {
    BTID *btids;			/* hierarchy indexes */
    HFID *hfids;			/* HFIDs for classes in the hierarchy */
    int count;			/* number of classes in the hierarchy */
  };
} // namespace cubquery

// legacy aliases
using AGGREGATE_HASH_VALUE = cubquery::aggregate_hash_value;
using AGGREGATE_HASH_KEY = cubquery::aggregate_hash_key;
using AGGREGATE_HASH_CONTEXT = cubquery::aggregate_hash_context;
using HIERARCHY_AGGREGATE_HELPER = cubquery::hierarchy_aggregate_helper;

int qdata_initialize_aggregate_list (cubthread::entry *thread_p, cubxasl::aggregate_list_node *agg_list,
				     QUERY_ID query_id);
int qdata_aggregate_accumulator_to_accumulator (cubthread::entry *thread_p, cubxasl::aggregate_accumulator *acc,
    cubxasl::aggregate_accumulator_domain *acc_dom, FUNC_TYPE func_type,
    tp_domain *func_domain, cubxasl::aggregate_accumulator *new_acc);
int qdata_evaluate_aggregate_list (cubthread::entry *thread_p, cubxasl::aggregate_list_node *agg_list, val_descr *vd,
				   cubxasl::aggregate_accumulator *alt_acc_list);
int qdata_evaluate_aggregate_optimize (cubthread::entry *thread_p, cubxasl::aggregate_list_node *agg_ptr, HFID *hfid,
				       OID *partition_cls_oid);
int qdata_evaluate_aggregate_hierarchy (cubthread::entry *thread_p, cubxasl::aggregate_list_node *agg_ptr,
					HFID *root_hfid, BTID *root_btid,
					cubquery::hierarchy_aggregate_helper *helper);
int qdata_finalize_aggregate_list (cubthread::entry *thread_p, cubxasl::aggregate_list_node *agg_list,
				   bool keep_list_file, sampling_info *sampling);

cubquery::aggregate_hash_key *qdata_alloc_agg_hkey (cubthread::entry *thread_p, int val_cnt, bool alloc_vals);
void qdata_free_agg_hkey (cubthread::entry *thread_p, cubquery::aggregate_hash_key *key);
cubquery::aggregate_hash_value *qdata_alloc_agg_hvalue (cubthread::entry *thread_p, int func_cnt, cubxasl::aggregate_list_node *g_agg_list);
void qdata_free_agg_hvalue (cubthread::entry *thread_p, cubquery::aggregate_hash_value *value);
int qdata_get_agg_hkey_size (cubquery::aggregate_hash_key *key);
int qdata_get_agg_hvalue_size (cubquery::aggregate_hash_value *value, bool ret_delta);
int qdata_free_agg_hentry (const void *key, void *data, void *args);
unsigned int qdata_hash_agg_hkey (const void *key, unsigned int ht_size);
DB_VALUE_COMPARE_RESULT qdata_agg_hkey_compare (cubquery::aggregate_hash_key *ckey1,
    cubquery::aggregate_hash_key *ckey2, int *diff_pos);
int qdata_agg_hkey_eq (const void *key1, const void *key2);
cubquery::aggregate_hash_key *qdata_copy_agg_hkey (cubthread::entry *thread_p, cubquery::aggregate_hash_key *key);
void qdata_load_agg_hvalue_in_agg_list (cubquery::aggregate_hash_value *value, cubxasl::aggregate_list_node *agg_list,
					bool copy_vals);
int qdata_save_agg_hentry_to_list (cubthread::entry *thread_p, cubquery::aggregate_hash_key *key,
				   cubquery::aggregate_hash_value *value, DB_VALUE *temp_dbval_array,
				   qfile_list_id *list_id);
int qdata_load_agg_hentry_from_tuple (cubthread::entry *thread_p, QFILE_TUPLE tuple, cubquery::aggregate_hash_key *key,
				      cubquery::aggregate_hash_value *value, tp_domain **key_dom,
				      cubxasl::aggregate_accumulator_domain **acc_dom);
SCAN_CODE qdata_load_agg_hentry_from_list (cubthread::entry *thread_p, qfile_list_scan_id *list_scan_id,
    cubquery::aggregate_hash_key *key, cubquery::aggregate_hash_value *value,
    tp_domain **key_dom, cubxasl::aggregate_accumulator_domain **acc_dom);
int qdata_save_agg_htable_to_list (cubthread::entry *thread_p, mht_table *hash_table, qfile_list_id *tuple_list_id,
				   qfile_list_id *partial_list_id, db_value *temp_dbval_array);

#endif // _QUERY_AGGREGATE_HPP_
