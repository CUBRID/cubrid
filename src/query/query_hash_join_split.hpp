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
 * query_hash_join_split.hpp - writing the partition lists of a hash join: tuples land in the landing buffer of
 *   each split writer, are staged in the last page of their partition, and go to the worker tfile when it is full;
 *   a partition whose hash table would not fit is split again by the hash keys stored in its tuples
 */

#ifndef _QUERY_HASH_JOIN_SPLIT_HPP_
#define _QUERY_HASH_JOIN_SPLIT_HPP_

#include "query_hash_join.h"	/* HASHJOIN_MANAGER, HASHJOIN_CONTEXT, HASHJOIN_SPLIT_INFO, HASHJOIN_STATUS */
#include "query_list.h"		/* QFILE_LIST_ID, QFILE_TUPLE */
#include "storage_common.h"	/* PAGE_PTR */
#include "system.h"		/* UINT32 */
#include "thread_compat.hpp"	/* THREAD_ENTRY */

#include <mutex>

struct qmgr_temp_file;
typedef struct qmgr_temp_file QMGR_TEMP_FILE;

/* HASHJOIN_SPLIT_STAGING_BUFFER */
typedef struct hashjoin_split_staging_buffer
{
  /* Last page of each partition list, kept in memory until it is full (qfile_add_tuple_to_staged_page).
   * NULL until the partition gets a tuple. */
  PAGE_PTR *pages;
  UINT32 page_cnt;		/* number of partitions */
} HASHJOIN_SPLIT_STAGING_BUFFER;

/* HASHJOIN_SPLIT_LANDING_BUFFER */
typedef struct hashjoin_split_landing_buffer
{
  /* Tuples of all partitions, where they land first; appended to the staged pages when the buffer is full.
   * Each entry is the offset of the next entry of the same partition followed by the tuple. */
  char *buffer;
  int buffer_used;
  int *part_first;		/* offset of the first entry of each partition, -1 if none */
  int *part_last;
  UINT32 *filled_parts;		/* partitions that have entries, in the order of their first entry */
  UINT32 filled_cnt;
} HASHJOIN_SPLIT_LANDING_BUFFER;

/* HASHJOIN_SPLIT_WRITER */
typedef struct hashjoin_split_writer
{
  /* The way of a tuple: landing_buffer -> staging_buffer -> worker_tfile, as a page of part_list_id.
   * The writer only refers to them; their owners create and free them. */
  HASHJOIN_SPLIT_LANDING_BUFFER *landing_buffer;	/* of this writer only */
  HASHJOIN_SPLIT_STAGING_BUFFER *staging_buffer;	/* shared by the writers of a split */
  // *INDENT-OFF*
  std::mutex *part_mutexes;	/* one per staged page, of the shared split info; nullptr in a serial split */
  // *INDENT-ON*
  QMGR_TEMP_FILE *worker_tfile;	/* of this writer only: the places of new pages are allocated from it */
  QFILE_LIST_ID **part_list_id;
} HASHJOIN_SPLIT_WRITER;

/* Hash Join Partitioning */
int hjoin_get_split_part_id (THREAD_ENTRY *thread_p, HASHJOIN_MANAGER *manager, HASHJOIN_FETCH_INFO *fetch_info,
			     QFILE_TUPLE_RECORD *tuple_record, HASH_SCAN_KEY *temp_key, UINT32 part_cnt,
			     UINT32 *part_id, bool *is_skipped);
UINT32 hjoin_get_resplit_part_id (QFILE_TUPLE_RECORD *tuple_record, UINT32 divisor, UINT32 part_cnt);

/* Hash Join Split Staging Buffer */
int hjoin_init_split_staging_buffer (THREAD_ENTRY *thread_p, HASHJOIN_SPLIT_STAGING_BUFFER *staging_buffer,
				     UINT32 part_cnt);
int hjoin_flush_split_staging_buffer (THREAD_ENTRY *thread_p, HASHJOIN_SPLIT_STAGING_BUFFER *staging_buffer,
				      QFILE_LIST_ID **part_list_id, QMGR_TEMP_FILE *worker_tfile);
void hjoin_clear_split_staging_buffer (THREAD_ENTRY *thread_p, HASHJOIN_SPLIT_STAGING_BUFFER *staging_buffer);

/* Hash Join Split Landing Buffer */
int hjoin_init_split_landing_buffer (THREAD_ENTRY *thread_p, HASHJOIN_SPLIT_LANDING_BUFFER *landing_buffer,
				     UINT32 part_cnt);
void hjoin_clear_split_landing_buffer (THREAD_ENTRY *thread_p, HASHJOIN_SPLIT_LANDING_BUFFER *landing_buffer);

/* Hash Join Split Writer */
// *INDENT-OFF*
void hjoin_init_split_writer (HASHJOIN_SPLIT_WRITER * writer, HASHJOIN_SPLIT_LANDING_BUFFER * landing_buffer,
			      HASHJOIN_SPLIT_STAGING_BUFFER * staging_buffer, std::mutex * part_mutexes,
			      QMGR_TEMP_FILE * worker_tfile, QFILE_LIST_ID ** part_list_id);
// *INDENT-ON*
int hjoin_add_to_split_writer (THREAD_ENTRY *thread_p, HASHJOIN_SPLIT_WRITER *writer, UINT32 part_id,
			       QFILE_TUPLE tuple);
int hjoin_flush_split_writer (THREAD_ENTRY *thread_p, HASHJOIN_SPLIT_WRITER *writer);

#endif /* _QUERY_HASH_JOIN_SPLIT_HPP_ */
