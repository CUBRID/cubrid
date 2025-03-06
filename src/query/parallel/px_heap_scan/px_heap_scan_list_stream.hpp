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
 * px_heap_scan_list_stream.hpp - list stream for parallel heap scan
 */

#ifndef _PX_HEAP_SCAN_LIST_STREAM_HPP_
#define _PX_HEAP_SCAN_LIST_STREAM_HPP_

#if SERVER_MODE && !WINDOWS

#include "list_file.h"
#include "query_list.h"
#include "scan_manager.h"

#include "tbb/concurrent_queue.h"

namespace parallel_heap_scan
{
  class list_page
  {
    public:
      enum class status
      {
	NONE,
	READ_SUCCESS,
	READ_END,
	READ_ERROR,
	WRITE_SUCCESS,
	WRITE_END,
	WRITE_OVERFLOW,
	WRITE_ERROR
      };
      list_page (THREAD_ENTRY *thread_p, QUERY_ID query_id, QFILE_TUPLE_VALUE_TYPE_LIST *type_list);
      ~list_page();
      status read (THREAD_ENTRY *thread_p, SCAN_ID *scan_id, QFILE_LIST_SCAN_ID *list_scan_id);
      status write (THREAD_ENTRY *thread_p, QFILE_TUPLE_RECORD *tplrec);
      void close_list();
      int open_list_scan (QFILE_LIST_SCAN_ID *list_scan_id);
      int close_list_scan (QFILE_LIST_SCAN_ID *list_scan_id);
    private:
      QFILE_LIST_ID *m_list_id;
      THREAD_ENTRY *m_thread_p;
      QFILE_TUPLE_VALUE_TYPE_LIST *m_type_list;
  };

  class list_stream
  {
    public:
      list_stream (int size, QUERY_ID query_id, SCAN_ID *scan_id);
      ~list_stream();

      void enqueue (std::shared_ptr<list_page> page);
      std::shared_ptr<list_page> dequeue();
      bool dequeue_timeout (std::shared_ptr<list_page> &page, int milliseconds);
      QFILE_TUPLE_VALUE_TYPE_LIST *get_type_list();
      size_t size();
      QUERY_ID get_query_id();
      void clear();
    private:
      tbb::concurrent_bounded_queue<std::shared_ptr<list_page>> m_queue;
      QFILE_TUPLE_VALUE_TYPE_LIST m_type_list;
      QUERY_ID m_query_id;
  };

  class list_reader
  {
    public:
      list_reader (std::shared_ptr<list_stream> stream);
      ~list_reader();

      void read (THREAD_ENTRY *thread_p, SCAN_ID *scan_id);
      std::shared_ptr<list_page> m_cur_page;
      QFILE_LIST_SCAN_ID m_scan_id;
    private:
      std::shared_ptr<list_stream> m_stream;

  };

  class list_writer
  {
    public:
      list_writer (std::shared_ptr<list_stream> stream, QFILE_TUPLE_VALUE_TYPE_LIST *type_list);
      ~list_writer();

      void write (THREAD_ENTRY *thread_p, SCAN_ID *scan_id);
      void write_final (THREAD_ENTRY *thread_p, SCAN_ID *scan_id);
      void close ();

    private:
      std::shared_ptr<list_stream> m_stream;
      QFILE_TUPLE_VALUE_TYPE_LIST *m_type_list;
      QUERY_ID m_query_id;
      std::shared_ptr<list_page> m_cur_page;
      QFILE_TUPLE_RECORD m_tpl_buf;

      QFILE_TUPLE_RECORD *make_tuple_record (THREAD_ENTRY *thread_p, SCAN_ID *scan_id);

  };

}

#endif /* SERVER_MODE && !WINDOWS */
#endif /* _PX_HEAP_SCAN_LIST_STREAM_HPP_ */
