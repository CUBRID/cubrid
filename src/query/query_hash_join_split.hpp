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
 * query_hash_join_split.hpp - writing the partition lists of a hash join
 */

#ifndef _QUERY_HASH_JOIN_SPLIT_HPP_
#define _QUERY_HASH_JOIN_SPLIT_HPP_

#include "query_list.h"		/* QFILE_LIST_ID, QFILE_TUPLE */
#include "storage_common.h"	/* PAGE_PTR */
#include "system.h"		/* UINT32 */
#include "thread_compat.hpp"	/* THREAD_ENTRY */

#include <mutex>

struct qmgr_temp_file;

namespace cubquery
{
  /*
   * hjoin_part_set - the partition lists of one input, written by one or more hjoin_part_writer
   *
   * The last page of each partition list is kept in memory until it is full (qfile_add_tuple_to_staged_list), and
   * finish () writes the remaining ones after all writers are done. The partition lists and the mutexes are not owned.
   */
  class hjoin_part_set
  {
    public:
      hjoin_part_set ();
      ~hjoin_part_set ();

      hjoin_part_set (const hjoin_part_set &) = delete;
      hjoin_part_set &operator= (const hjoin_part_set &) = delete;

      int init (QFILE_LIST_ID **part_lists, UINT32 part_cnt, std::mutex *part_mutexes);
      int finish (THREAD_ENTRY *thread_p, qmgr_temp_file *spool);
      void clear ();

    private:
      friend class hjoin_part_writer;

      QFILE_LIST_ID **m_part_lists;
      PAGE_PTR *m_last_pages;	/* NULL until the partition gets a tuple */
      std::mutex *m_part_mutexes;	/* nullptr when a single writer writes the lists */
      UINT32 m_part_cnt;
  };

  /*
   * hjoin_part_writer - buffers the tuples of one thread and appends them to a hjoin_part_set, one partition at a time
   */
  class hjoin_part_writer
  {
    public:
      hjoin_part_writer ();
      ~hjoin_part_writer ();

      hjoin_part_writer (const hjoin_part_writer &) = delete;
      hjoin_part_writer &operator= (const hjoin_part_writer &) = delete;

      int init (THREAD_ENTRY *thread_p, hjoin_part_set &part_set, qmgr_temp_file *spool);
      int add (UINT32 part_id, QFILE_TUPLE tuple);
      int flush ();
      void clear ();

    private:
      int append_to_part_list (UINT32 part_id, QFILE_TUPLE tuple);

      THREAD_ENTRY *m_thread_p;
      hjoin_part_set *m_part_set;
      qmgr_temp_file *m_spool;	/* pages of this writer are allocated from it */

      /* Tuples of all partitions, appended to the partition lists when the buffer is full.
       * Each entry is the offset of the next entry of the same partition followed by the tuple. */
      char *m_buffer;
      int m_buffer_used;
      int *m_part_first;	/* offset of the first entry of each partition, -1 if none */
      int *m_part_last;
      UINT32 *m_filled_parts;	/* partitions that have entries, in the order of their first entry */
      UINT32 m_filled_cnt;
  };
} // namespace cubquery

#endif /* _QUERY_HASH_JOIN_SPLIT_HPP_ */
