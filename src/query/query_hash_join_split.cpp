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
 * query_hash_join_split.cpp - splitting the inputs of a hash join into partition lists
 */

#include "query_hash_join_split.hpp"

#include "error_manager.h"	/* er_set, er_errid, assert_release_error */
#include "list_file.h"		/* qfile_add_tuple_to_staged_page, qfile_copy_to_external_file_page */
#include "memory_alloc.h"	/* db_private_alloc, DB_ALIGN */
#include "object_representation.h"	/* OR_GET_INT for QFILE_GET_TUPLE_LENGTH */
#include "query_hash_join_constants.hpp"	/* HASHJOIN_SPLIT_LANDING_BUFFER_SIZE, ... */
#include "query_manager.h"	/* QMGR_TEMP_FILE */

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/* Hash Join Split Writer */
static int hjoin_append_to_part_list (THREAD_ENTRY *thread_p, HASHJOIN_SPLIT_WRITER *writer, UINT32 part_id,
				      QFILE_TUPLE tuple);

/*
 * hjoin_get_split_part_id() - the partition of a tuple at the first split, by its join column values
 *   return: Error code (NO_ERROR if successful, error code otherwise).
 *   thread_p(in): Thread entry.
 *   manager(in): Hash join manager containing shared state.
 *   fetch_info(in): Input the tuple comes from.
 *   tuple_record(in/out): Tuple; the hash key of its join column values is stored in it (hjoin_update_tuple_hash_key).
 *   temp_key(in/out): Space for reading join column values.
 *   part_cnt(in): Number of partitions, the partition of NULL join keys included.
 *   part_id(out): Partition of the tuple.
 *   is_skipped(out): true if the tuple has a NULL join column and the join does not NULL-fill: it joins nothing.
 */
int
hjoin_get_split_part_id (THREAD_ENTRY *thread_p, HASHJOIN_MANAGER *manager, HASHJOIN_FETCH_INFO *fetch_info,
			 QFILE_TUPLE_RECORD *tuple_record, HASH_SCAN_KEY *temp_key, UINT32 part_cnt,
			 UINT32 *part_id, bool *is_skipped)
{
  unsigned int hash_key;
  bool has_null_fill_side;
  bool need_skip_next = false;
  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (manager != NULL);
  assert (fetch_info != NULL);
  assert (tuple_record != NULL);
  assert (temp_key != NULL);
  assert (part_cnt > 1);
  assert (part_id != NULL);
  assert (is_skipped != NULL);

  *is_skipped = false;

  has_null_fill_side = IS_NULL_FILL_JOIN_TYPE (manager->join_type);

  error = hjoin_fetch_key (thread_p, fetch_info, tuple_record, temp_key, NULL /* compare_key */, &need_skip_next);
  if (error != NO_ERROR)
    {
      return error;
    }

  if (need_skip_next)
    {
      if (has_null_fill_side)
	{
	  /* In joins that NULL-fill one side,
	   * tuples with NULL in any join column are placed in the last partition.
	   * HASHJOIN_STATUS_FILL_NULL_VALUES is triggered for all tuples in that partition.
	   */
	  *part_id = part_cnt - 1;
	}
      else
	{
	  *is_skipped = true;
	}

      return NO_ERROR;
    }

  hash_key = qdata_hash_scan_key (temp_key, UINT_MAX, HASH_METH_IN_MEM);
  *part_id = HJOIN_HASH_TO_PARTITION (HJOIN_HASH_MIX (hash_key), (has_null_fill_side) ? part_cnt - 1 : part_cnt);

  hjoin_update_tuple_hash_key (thread_p, tuple_record, hash_key);

  return NO_ERROR;
}

/*
 * hjoin_get_resplit_part_id() - the partition of a tuple when its partition is split again, by the hash key stored in
 *                               it at the first split; the partition of NULL join keys is not split again
 *   return: Partition of the tuple.
 *   tuple_record(in): Tuple.
 *   divisor(in): Multiplier of the mixed hash key giving the position of the tuple in its partition (HASHJOIN_CONTEXT).
 *   part_cnt(in): Number of partitions.
 */
UINT32
hjoin_get_resplit_part_id (QFILE_TUPLE_RECORD *tuple_record, UINT32 divisor, UINT32 part_cnt)
{
  UINT32 hash_key, sub_hash_key;

  assert (tuple_record != NULL);
  assert (part_cnt > 1);

  hash_key = (UINT32) OR_GET_INT (hjoin_locate_tuple_hash_key (tuple_record));

  /* the position of the mixed key within its partition of the first split */
  sub_hash_key = (UINT32) ((UINT64) HJOIN_HASH_MIX (hash_key) * divisor);

  return HJOIN_HASH_TO_PARTITION (sub_hash_key, part_cnt);
}

/*
 * hjoin_init_split_landing_buffer() -
 *   return: Error code (NO_ERROR if successful, error code otherwise).
 *   thread_p(in): Thread entry.
 *   landing_buffer(out): Landing buffer to initialize.
 *   part_cnt(in): Number of partitions.
 */
int
hjoin_init_split_landing_buffer (THREAD_ENTRY *thread_p, HASHJOIN_SPLIT_LANDING_BUFFER *landing_buffer,
				 UINT32 part_cnt)
{
  UINT32 part_index;

  assert (thread_p != NULL);
  assert (landing_buffer != NULL);
  assert (part_cnt > 1);

  /* one block: the tuple area, then part_first, part_last and filled_parts of part_cnt entries each */
  landing_buffer->buffer =
	  (char *) db_private_alloc (thread_p, HASHJOIN_SPLIT_LANDING_BUFFER_SIZE + 3 * part_cnt * sizeof (int));
  if (landing_buffer->buffer == NULL)
    {
      assert_release_error (er_errid () != NO_ERROR);
      return er_errid ();
    }
  landing_buffer->part_first = (int *) (landing_buffer->buffer + HASHJOIN_SPLIT_LANDING_BUFFER_SIZE);
  landing_buffer->part_last = landing_buffer->part_first + part_cnt;
  landing_buffer->filled_parts = (UINT32 *) (landing_buffer->part_last + part_cnt);
  landing_buffer->buffer_used = 0;
  landing_buffer->filled_cnt = 0;

  for (part_index = 0; part_index < part_cnt; part_index++)
    {
      landing_buffer->part_first[part_index] = -1;
      landing_buffer->part_last[part_index] = -1;
    }

  return NO_ERROR;
}

/*
 * hjoin_clear_split_landing_buffer() -
 *   return: None.
 *   thread_p(in): Thread entry.
 *   landing_buffer(in): Landing buffer to clear. Tuples not yet flushed are discarded.
 */
void
hjoin_clear_split_landing_buffer (THREAD_ENTRY *thread_p, HASHJOIN_SPLIT_LANDING_BUFFER *landing_buffer)
{
  assert (thread_p != NULL);
  assert (landing_buffer != NULL);

  /* part_first, part_last and filled_parts are in the block of buffer */
  if (landing_buffer->buffer != NULL)
    {
      db_private_free_and_init (thread_p, landing_buffer->buffer);
    }

  landing_buffer->part_first = NULL;
  landing_buffer->part_last = NULL;
  landing_buffer->filled_parts = NULL;
}

/*
 * hjoin_init_split_writer() - map a writer to the way of its tuples; nothing is allocated
 *   return: None.
 *   writer(out): Split writer to initialize.
 *   landing_buffer(in): Landing buffer of this writer.
 *   staging_buffer(in): Staged last pages of the partition lists, shared by the writers of a split.
 *   part_mutexes(in): Mutex of each staged page, or nullptr in a serial split.
 *   worker_tfile(in): Temporary file the places of the pages written by this writer are allocated from.
 *   part_list_id(in): Partition lists to write to.
 */
// *INDENT-OFF*
void
hjoin_init_split_writer (HASHJOIN_SPLIT_WRITER * writer, HASHJOIN_SPLIT_LANDING_BUFFER * landing_buffer,
			 HASHJOIN_SPLIT_STAGING_BUFFER * staging_buffer, std::mutex * part_mutexes,
			 QMGR_TEMP_FILE * worker_tfile, QFILE_LIST_ID ** part_list_id)
// *INDENT-ON*
{
  assert (writer != NULL);
  assert (landing_buffer != NULL);
  assert (staging_buffer != NULL);
  assert (worker_tfile != NULL);
  assert (part_list_id != NULL);

  writer->landing_buffer = landing_buffer;
  writer->staging_buffer = staging_buffer;
  writer->part_mutexes = part_mutexes;
  writer->worker_tfile = worker_tfile;
  writer->part_list_id = part_list_id;
}

/*
 * hjoin_add_to_split_writer() -
 *   return: Error code (NO_ERROR if successful, error code otherwise).
 *   thread_p(in): Thread entry.
 *   writer(in/out): Split writer.
 *   part_id(in): Partition of the tuple.
 *   tuple(in): Tuple to add. A tuple larger than the landing buffer is appended to the partition list at once.
 */
int
hjoin_add_to_split_writer (THREAD_ENTRY *thread_p, HASHJOIN_SPLIT_WRITER *writer, UINT32 part_id, QFILE_TUPLE tuple)
{
  HASHJOIN_SPLIT_LANDING_BUFFER *landing_buffer;
  int tuple_length, entry_offset, entry_size;
  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (writer != NULL);
  assert (tuple != NULL);

  landing_buffer = writer->landing_buffer;
  assert (part_id < writer->staging_buffer->page_cnt);

  tuple_length = QFILE_GET_TUPLE_LENGTH (tuple);
  entry_size = HASHJOIN_SPLIT_LANDING_ENTRY_HEADER_SIZE + DB_ALIGN (tuple_length, MAX_ALIGNMENT);
  if (entry_size > HASHJOIN_SPLIT_LANDING_BUFFER_SIZE)
    {
      return hjoin_append_to_part_list (thread_p, writer, part_id, tuple);
    }

  if (landing_buffer->buffer_used + entry_size > HASHJOIN_SPLIT_LANDING_BUFFER_SIZE)
    {
      error = hjoin_flush_split_writer (thread_p, writer);
      if (error != NO_ERROR)
	{
	  return error;
	}
    }

  entry_offset = landing_buffer->buffer_used;
  * (int *) (landing_buffer->buffer + entry_offset) = -1;
  memcpy (landing_buffer->buffer + entry_offset + HASHJOIN_SPLIT_LANDING_ENTRY_HEADER_SIZE, tuple, tuple_length);

  if (landing_buffer->part_last[part_id] == -1)
    {
      landing_buffer->part_first[part_id] = entry_offset;
      landing_buffer->filled_parts[landing_buffer->filled_cnt++] = part_id;
    }
  else
    {
      * (int *) (landing_buffer->buffer + landing_buffer->part_last[part_id]) = entry_offset;
    }
  landing_buffer->part_last[part_id] = entry_offset;

  landing_buffer->buffer_used += entry_size;

  return NO_ERROR;
}

/*
 * hjoin_flush_split_writer() - append the tuples of the landing buffer to the partition lists, one partition at a time
 *   return: Error code (NO_ERROR if successful, error code otherwise).
 *   thread_p(in): Thread entry.
 *   writer(in/out): Split writer.
 */
int
hjoin_flush_split_writer (THREAD_ENTRY *thread_p, HASHJOIN_SPLIT_WRITER *writer)
{
  HASHJOIN_SPLIT_LANDING_BUFFER *landing_buffer;
  UINT32 filled_index, part_id;
  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (writer != NULL);

  landing_buffer = writer->landing_buffer;

  for (filled_index = 0; filled_index < landing_buffer->filled_cnt; filled_index++)
    {
      part_id = landing_buffer->filled_parts[filled_index];

      error = hjoin_append_to_part_list (thread_p, writer, part_id, NULL);
      if (error != NO_ERROR)
	{
	  return error;
	}

      landing_buffer->part_first[part_id] = -1;
      landing_buffer->part_last[part_id] = -1;
    }

  landing_buffer->filled_cnt = 0;
  landing_buffer->buffer_used = 0;

  return NO_ERROR;
}

/*
 * hjoin_append_to_part_list() - append tuples to a partition list through its staged page, holding its mutex in
 *                               parallel split
 *   return: Error code (NO_ERROR if successful, error code otherwise).
 *   thread_p(in): Thread entry.
 *   writer(in): Split writer.
 *   part_id(in): Partition to append to.
 *   tuple(in): Tuple to append, or NULL to append the tuples of the partition in the landing buffer.
 */
static int
hjoin_append_to_part_list (THREAD_ENTRY *thread_p, HASHJOIN_SPLIT_WRITER *writer, UINT32 part_id, QFILE_TUPLE tuple)
{
  HASHJOIN_SPLIT_LANDING_BUFFER *landing_buffer;
  HASHJOIN_SPLIT_STAGING_BUFFER *staging_buffer;
  QFILE_LIST_ID *list_id;
  PAGE_PTR *staged_page = NULL;
  QFILE_TUPLE next_tuple;
  int entry_offset = -1;
  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (writer != NULL);

  landing_buffer = writer->landing_buffer;
  staging_buffer = writer->staging_buffer;
  assert (part_id < staging_buffer->page_cnt);

  list_id = writer->part_list_id[part_id];

  // *INDENT-OFF*
  std::unique_lock<std::mutex> lock;
  if (writer->part_mutexes != nullptr)
    {
      lock = std::unique_lock<std::mutex> (writer->part_mutexes[part_id]);
    }
  // *INDENT-ON*

  assert (list_id->last_pgptr == NULL);

  staged_page = &staging_buffer->pages[part_id];
  if (*staged_page == NULL)
    {
      /* calloc, not db_private_alloc: the main thread frees the pages of all writers */
      *staged_page = (PAGE_PTR) calloc (1, DB_PAGESIZE);
      if (*staged_page == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) DB_PAGESIZE);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
    }

  if (tuple != NULL)
    {
      next_tuple = tuple;
    }
  else
    {
      entry_offset = landing_buffer->part_first[part_id];
      next_tuple = landing_buffer->buffer + entry_offset + HASHJOIN_SPLIT_LANDING_ENTRY_HEADER_SIZE;
    }

  while (next_tuple != NULL)
    {
      error = qfile_add_tuple_to_staged_page (thread_p, list_id, *staged_page, writer->worker_tfile, next_tuple);
      if (error != NO_ERROR)
	{
	  break;
	}

      if (entry_offset == -1)
	{
	  next_tuple = NULL;
	}
      else
	{
	  entry_offset = * (int *) (landing_buffer->buffer + entry_offset);
	  if (entry_offset == -1)
	    {
	      next_tuple = NULL;
	    }
	  else
	    {
	      next_tuple = landing_buffer->buffer + entry_offset + HASHJOIN_SPLIT_LANDING_ENTRY_HEADER_SIZE;
	    }
	}
    }

  return error;
}

/*
 * hjoin_init_split_staging_buffer() -
 *   return: Error code (NO_ERROR if successful, error code otherwise).
 *   thread_p(in): Thread entry.
 *   staging_buffer(out): Staged last pages of the partition lists of one input.
 *   part_cnt(in): Number of partitions.
 */
int
hjoin_init_split_staging_buffer (THREAD_ENTRY *thread_p, HASHJOIN_SPLIT_STAGING_BUFFER *staging_buffer,
				 UINT32 part_cnt)
{
  assert (thread_p != NULL);
  assert (staging_buffer != NULL);
  assert (part_cnt > 1);

  staging_buffer->pages = (PAGE_PTR *) db_private_alloc (thread_p, part_cnt * sizeof (PAGE_PTR));
  if (staging_buffer->pages == NULL)
    {
      assert_release_error (er_errid () != NO_ERROR);
      return er_errid ();
    }
  memset (staging_buffer->pages, 0, part_cnt * sizeof (PAGE_PTR));
  staging_buffer->page_cnt = part_cnt;

  return NO_ERROR;
}

/*
 * hjoin_flush_split_staging_buffer() - copy the staged last page of each partition list, after all writers are
 *                                     flushed
 *   return: Error code (NO_ERROR if successful, error code otherwise).
 *   thread_p(in): Thread entry.
 *   staging_buffer(in): Staged last pages.
 *   part_list_id(in): Partition lists.
 *   worker_tfile(in): One of the worker tfiles; its TDE setting is used for the pages.
 */
int
hjoin_flush_split_staging_buffer (THREAD_ENTRY *thread_p, HASHJOIN_SPLIT_STAGING_BUFFER *staging_buffer,
				  QFILE_LIST_ID **part_list_id, struct qmgr_temp_file *worker_tfile)
{
  UINT32 part_index;
  int error;

  assert (thread_p != NULL);
  assert (staging_buffer != NULL);
  assert (part_list_id != NULL);

  for (part_index = 0; part_index < staging_buffer->page_cnt; part_index++)
    {
      /* the staged page is the last page of a list that got tuples; a page kept from the previous input is stale */
      if (VPID_ISNULL (&part_list_id[part_index]->last_vpid))
	{
	  continue;
	}
      assert (staging_buffer->pages[part_index] != NULL);

      error =
	      qfile_copy_to_external_file_page (thread_p, part_list_id[part_index], staging_buffer->pages[part_index],
		  worker_tfile);
      if (error != NO_ERROR)
	{
	  return error;
	}
    }

  return NO_ERROR;
}

/*
 * hjoin_clear_split_staging_buffer() -
 *   return: None.
 *   thread_p(in): Thread entry that initialized the staging buffer.
 *   staging_buffer(in/out): Staged last pages to free. Pages not yet written are discarded.
 */
void
hjoin_clear_split_staging_buffer (THREAD_ENTRY *thread_p, HASHJOIN_SPLIT_STAGING_BUFFER *staging_buffer)
{
  UINT32 part_index;

  assert (thread_p != NULL);
  assert (staging_buffer != NULL);

  if (staging_buffer->pages == NULL)
    {
      return;
    }

  for (part_index = 0; part_index < staging_buffer->page_cnt; part_index++)
    {
      if (staging_buffer->pages[part_index] != NULL)
	{
	  free_and_init (staging_buffer->pages[part_index]);
	}
    }

  db_private_free_and_init (thread_p, staging_buffer->pages);
  staging_buffer->page_cnt = 0;
}
