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
#include "dbtype.h"

#include "tbb/concurrent_queue.h"

namespace parallel_heap_scan
{
  class list_id_wrapper
  {
    public:
      enum class status
      {
	NONE,
	READ_SUCCESS,
	READ_CURPAGE_END,
	READ_END,
	READ_ERROR,
	WRITE_SUCCESS,
	WRITE_CURPAGE_END,
	WRITE_END,
	WRITE_ERROR
      };
      QFILE_LIST_SCAN_ID m_list_scan_id;
      bool m_list_scan_opened;
      bool m_list_id_closed;
      list_id_wrapper() = delete;
      list_id_wrapper (THREAD_ENTRY *thread_p, QUERY_ID query_id, QFILE_TUPLE_VALUE_TYPE_LIST *type_list);
      ~list_id_wrapper();
      int open_list_scan ();
      int close_list_scan ();
      status read (THREAD_ENTRY *thread_p, SCAN_ID *scan_id, QFILE_LIST_SCAN_ID *list_scan_id);
      status write (THREAD_ENTRY *thread_p, QFILE_TUPLE_RECORD *tplrec);
      bool open (THREAD_ENTRY *thread_p);
      void close ();
      VPID m_read_vpid;
      VPID m_write_vpid;
      QFILE_LIST_ID *m_list_id;
    private:
      QUERY_ID m_query_id;
      QFILE_TUPLE_VALUE_TYPE_LIST *m_type_list;
      THREAD_ENTRY *m_main_thread_p;
      THREAD_ENTRY *m_task_thread_p;
  };

  class list_id_data
  {
    public:
      list_id_data()
      {
	m_list_id_wrapper_p = nullptr;
	VPID_SET_NULL (&m_vpid);
      }
      list_id_data (list_id_wrapper *list_id_wrapper_p, VPID vpid)
	: m_list_id_wrapper_p (list_id_wrapper_p), m_vpid (vpid) {}
      ~list_id_data() {}
      list_id_wrapper *m_list_id_wrapper_p;
      VPID m_vpid;
  };

  class list_stream
  {
    public:
      list_stream (THREAD_ENTRY *thread_p, int parallelism, int size, QUERY_ID query_id, SCAN_ID *scan_id);
      ~list_stream();
      void enqueue (list_id_data &data);
      bool dequeue_timeout (list_id_data &data, int milliseconds);
      inline QFILE_TUPLE_VALUE_TYPE_LIST *get_type_list()
      {
	return &m_type_list;
      }
      inline size_t size()
      {
	return m_queue.size();
      }
      inline QUERY_ID get_query_id()
      {
	return m_query_id;
      }
      void clear();
      std::vector<std::shared_ptr<list_id_wrapper>> m_list_id_wrappers;
    private:
      THREAD_ENTRY *m_thread_p;
      int m_parallelism;
      int m_size;
      QUERY_ID m_query_id;
      SCAN_ID *m_scan_id;
      tbb::concurrent_bounded_queue<list_id_data> m_queue;
      QFILE_TUPLE_VALUE_TYPE_LIST m_type_list;
  };

  class list_reader
  {
    public:
      list_reader ()
      {
	m_cur_data_valid = false;
      }
      ~list_reader() {}

      list_id_data m_cur_data;
      bool m_cur_data_valid;
      list_id_wrapper *m_list_id_wrapper_p;
  };

  class list_writer
  {
    public:
      list_writer() = delete;
      list_writer (std::shared_ptr<list_stream> stream, list_id_wrapper *list_id_wrapper_p)
	: m_stream (stream), m_list_id_wrapper_p (list_id_wrapper_p)
      {
	m_tpl_buf.tpl = (char *) malloc (DB_PAGESIZE);
	m_tpl_buf.size = 0;
	m_tpl_buf_alloc_size = DB_PAGESIZE;
      }
      ~list_writer()
      {
	free (m_tpl_buf.tpl);
      }

      int write (THREAD_ENTRY *thread_p, SCAN_ID *scan_id, list_id_data &data);
      void close (list_id_data &data);

      bool is_tfile_allocated() const;

    private:
      std::shared_ptr<list_stream> m_stream;
      list_id_wrapper *m_list_id_wrapper_p;

      QFILE_TUPLE_RECORD m_tpl_buf;
      std::size_t m_tpl_buf_alloc_size;
      QFILE_TUPLE_RECORD *make_tuple_record (THREAD_ENTRY *thread_p, SCAN_ID *scan_id);
  };
}

#endif /* SERVER_MODE && !WINDOWS */
#endif /* _PX_HEAP_SCAN_LIST_STREAM_HPP_ */
