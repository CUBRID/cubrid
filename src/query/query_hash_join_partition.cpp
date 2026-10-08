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
 * query_hash_join_partition.cpp - writing the partition lists of a hash join
 */

#include "query_hash_join_partition.hpp"

#include "error_manager.h"		/* er_set, er_errid, assert_release_error */
#include "list_file.h"			/* qfile_add_tuple_to_staged_list, qfile_write_staged_list_page */
#include "memory_alloc.h"		/* db_private_alloc, db_private_free_and_init, DB_ALIGN */
#include "object_representation.h"	/* OR_GET_INT for QFILE_GET_TUPLE_LENGTH */

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/* buffer of a partition writer, shared by all partitions, so its size does not grow with the partition count */
#define HJOIN_PART_WRITER_BUFFER_SIZE (64 * DB_PAGESIZE)
#define HJOIN_PART_WRITER_ENTRY_HEADER_SIZE DB_ALIGN (sizeof (int), MAX_ALIGNMENT)

namespace cubquery
{
  /*
   * hjoin_part_set
   */

  hjoin_part_set::hjoin_part_set ()
    : m_part_lists (nullptr)
    , m_last_pages (nullptr)
    , m_part_mutexes (nullptr)
    , m_part_cnt (0)
  {
    //
  }

  hjoin_part_set::~hjoin_part_set ()
  {
    clear ();
  }

  /*
   * hjoin_part_set::init () -
   *   return: Error code (NO_ERROR if successful, error code otherwise).
   *   part_lists(in): Partition lists to write to.
   *   part_cnt(in): Number of partitions.
   *   part_mutexes(in): Mutex of each partition list, or nullptr when a single writer writes the lists.
   */
  int
  hjoin_part_set::init (QFILE_LIST_ID **part_lists, UINT32 part_cnt, std::mutex *part_mutexes)
  {
    assert (part_lists != nullptr);
    assert (part_cnt > 1);
    assert (m_last_pages == nullptr);

    m_last_pages = (PAGE_PTR *) calloc (part_cnt, sizeof (PAGE_PTR));
    if (m_last_pages == nullptr)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, part_cnt * sizeof (PAGE_PTR));
	return ER_OUT_OF_VIRTUAL_MEMORY;
      }

    m_part_lists = part_lists;
    m_part_mutexes = part_mutexes;
    m_part_cnt = part_cnt;

    return NO_ERROR;
  }

  /*
   * hjoin_part_set::finish () - write the last page of each partition list, after all writers are done
   *   return: Error code (NO_ERROR if successful, error code otherwise).
   *   thread_p(in): Thread entry.
   *   spool(in): One of the spools; its TDE setting is used for the pages.
   */
  int
  hjoin_part_set::finish (THREAD_ENTRY *thread_p, qmgr_temp_file *spool)
  {
    UINT32 part_index;
    int error;

    assert (thread_p != nullptr);

    for (part_index = 0; part_index < m_part_cnt; part_index++)
      {
	if (m_last_pages[part_index] == nullptr)
	  {
	    continue;
	  }

	error = qfile_write_staged_list_page (thread_p, m_part_lists[part_index], m_last_pages[part_index], spool);
	if (error != NO_ERROR)
	  {
	    return error;
	  }
      }

    return NO_ERROR;
  }

  /*
   * hjoin_part_set::clear () - free the last pages; pages not yet written are discarded
   */
  void
  hjoin_part_set::clear ()
  {
    UINT32 part_index;

    if (m_last_pages == nullptr)
      {
	return;
      }

    for (part_index = 0; part_index < m_part_cnt; part_index++)
      {
	if (m_last_pages[part_index] != nullptr)
	  {
	    free_and_init (m_last_pages[part_index]);
	  }
      }

    free_and_init (m_last_pages);
    m_part_lists = nullptr;
    m_part_mutexes = nullptr;
    m_part_cnt = 0;
  }

  /*
   * hjoin_part_writer
   */

  hjoin_part_writer::hjoin_part_writer ()
    : m_thread_p (nullptr)
    , m_part_set (nullptr)
    , m_spool (nullptr)
    , m_buffer (nullptr)
    , m_buffer_used (0)
    , m_part_first (nullptr)
    , m_part_last (nullptr)
    , m_filled_parts (nullptr)
    , m_filled_cnt (0)
  {
    //
  }

  hjoin_part_writer::~hjoin_part_writer ()
  {
    clear ();
  }

  /*
   * hjoin_part_writer::init () -
   *   return: Error code (NO_ERROR if successful, error code otherwise).
   *   thread_p(in): Thread entry. The writer is used and cleared only by this thread.
   *   part_set(in): Partition lists to write to.
   *   spool(in): Temporary file the pages written by this writer are allocated from.
   */
  int
  hjoin_part_writer::init (THREAD_ENTRY *thread_p, hjoin_part_set &part_set, qmgr_temp_file *spool)
  {
    UINT32 part_cnt, part_index;

    assert (thread_p != nullptr);
    assert (part_set.m_last_pages != nullptr);
    assert (spool != nullptr);
    assert (m_buffer == nullptr);

    part_cnt = part_set.m_part_cnt;

    m_thread_p = thread_p;
    m_part_set = &part_set;
    m_spool = spool;
    m_buffer_used = 0;
    m_filled_cnt = 0;

    m_buffer = (char *) db_private_alloc (thread_p, HJOIN_PART_WRITER_BUFFER_SIZE);
    m_part_first = (int *) db_private_alloc (thread_p, part_cnt * sizeof (int));
    m_part_last = (int *) db_private_alloc (thread_p, part_cnt * sizeof (int));
    m_filled_parts = (UINT32 *) db_private_alloc (thread_p, part_cnt * sizeof (UINT32));
    if (m_buffer == nullptr || m_part_first == nullptr || m_part_last == nullptr || m_filled_parts == nullptr)
      {
	clear ();

	assert_release_error (er_errid () != NO_ERROR);
	return er_errid ();
      }

    for (part_index = 0; part_index < part_cnt; part_index++)
      {
	m_part_first[part_index] = -1;
	m_part_last[part_index] = -1;
      }

    return NO_ERROR;
  }

  /*
   * hjoin_part_writer::add () -
   *   return: Error code (NO_ERROR if successful, error code otherwise).
   *   part_id(in): Partition of the tuple.
   *   tuple(in): Tuple to add. A tuple larger than the buffer is appended to the partition list at once.
   */
  int
  hjoin_part_writer::add (UINT32 part_id, QFILE_TUPLE tuple)
  {
    int tuple_length, entry_offset, entry_size;
    int error = NO_ERROR;

    assert (m_buffer != nullptr);
    assert (part_id < m_part_set->m_part_cnt);
    assert (tuple != nullptr);

    tuple_length = QFILE_GET_TUPLE_LENGTH (tuple);
    entry_size = HJOIN_PART_WRITER_ENTRY_HEADER_SIZE + DB_ALIGN (tuple_length, MAX_ALIGNMENT);
    if (entry_size > HJOIN_PART_WRITER_BUFFER_SIZE)
      {
	return append_to_part_list (part_id, tuple);
      }

    if (m_buffer_used + entry_size > HJOIN_PART_WRITER_BUFFER_SIZE)
      {
	error = flush ();
	if (error != NO_ERROR)
	  {
	    return error;
	  }
      }

    entry_offset = m_buffer_used;
    * (int *) (m_buffer + entry_offset) = -1;
    memcpy (m_buffer + entry_offset + HJOIN_PART_WRITER_ENTRY_HEADER_SIZE, tuple, tuple_length);

    if (m_part_last[part_id] == -1)
      {
	m_part_first[part_id] = entry_offset;
	m_filled_parts[m_filled_cnt++] = part_id;
      }
    else
      {
	* (int *) (m_buffer + m_part_last[part_id]) = entry_offset;
      }
    m_part_last[part_id] = entry_offset;

    m_buffer_used += entry_size;

    return NO_ERROR;
  }

  /*
   * hjoin_part_writer::flush () - append the buffered tuples to the partition lists, one partition at a time
   *   return: Error code (NO_ERROR if successful, error code otherwise).
   */
  int
  hjoin_part_writer::flush ()
  {
    UINT32 filled_index, part_id;
    int error = NO_ERROR;

    assert (m_buffer != nullptr);

    for (filled_index = 0; filled_index < m_filled_cnt; filled_index++)
      {
	part_id = m_filled_parts[filled_index];

	error = append_to_part_list (part_id, nullptr);
	if (error != NO_ERROR)
	  {
	    return error;
	  }

	m_part_first[part_id] = -1;
	m_part_last[part_id] = -1;
      }

    m_filled_cnt = 0;
    m_buffer_used = 0;

    return NO_ERROR;
  }

  /*
   * hjoin_part_writer::clear () - free the buffer; tuples not yet flushed are discarded
   */
  void
  hjoin_part_writer::clear ()
  {
    if (m_buffer != nullptr)
      {
	db_private_free_and_init (m_thread_p, m_buffer);
      }

    if (m_part_first != nullptr)
      {
	db_private_free_and_init (m_thread_p, m_part_first);
      }

    if (m_part_last != nullptr)
      {
	db_private_free_and_init (m_thread_p, m_part_last);
      }

    if (m_filled_parts != nullptr)
      {
	db_private_free_and_init (m_thread_p, m_filled_parts);
      }
  }

  /*
   * hjoin_part_writer::append_to_part_list () - append tuples to a partition list, holding its mutex if any
   *   return: Error code (NO_ERROR if successful, error code otherwise).
   *   part_id(in): Partition to append to.
   *   tuple(in): Tuple to append, or nullptr to append the buffered tuples of the partition.
   */
  int
  hjoin_part_writer::append_to_part_list (UINT32 part_id, QFILE_TUPLE tuple)
  {
    QFILE_LIST_ID *list_id;
    PAGE_PTR *last_page;
    QFILE_TUPLE next_tuple;
    int entry_offset = -1;
    int error = NO_ERROR;

    assert (part_id < m_part_set->m_part_cnt);

    list_id = m_part_set->m_part_lists[part_id];

    std::unique_lock<std::mutex> lock;
    if (m_part_set->m_part_mutexes != nullptr)
      {
	lock = std::unique_lock<std::mutex> (m_part_set->m_part_mutexes[part_id]);
      }

    assert (list_id->last_pgptr == NULL);

    last_page = &m_part_set->m_last_pages[part_id];
    if (*last_page == nullptr)
      {
	/* calloc, not db_private_alloc: the thread that finishes the partition set frees the pages of all writers */
	*last_page = (PAGE_PTR) calloc (1, DB_PAGESIZE);
	if (*last_page == nullptr)
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) DB_PAGESIZE);
	    return ER_OUT_OF_VIRTUAL_MEMORY;
	  }
      }

    if (tuple != nullptr)
      {
	next_tuple = tuple;
      }
    else
      {
	entry_offset = m_part_first[part_id];
	next_tuple = m_buffer + entry_offset + HJOIN_PART_WRITER_ENTRY_HEADER_SIZE;
      }

    while (next_tuple != nullptr)
      {
	error = qfile_add_tuple_to_staged_list (m_thread_p, list_id, *last_page, m_spool, next_tuple);
	if (error != NO_ERROR)
	  {
	    break;
	  }

	if (entry_offset == -1)
	  {
	    next_tuple = nullptr;
	  }
	else
	  {
	    entry_offset = * (int *) (m_buffer + entry_offset);
	    next_tuple = (entry_offset == -1) ? nullptr : m_buffer + entry_offset + HJOIN_PART_WRITER_ENTRY_HEADER_SIZE;
	  }
      }

    return error;
  }
} // namespace cubquery
