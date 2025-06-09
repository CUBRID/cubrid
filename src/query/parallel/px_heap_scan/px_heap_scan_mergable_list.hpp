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
 * px_heap_scan_mergable_list.hpp - mergable list for parallel heap scan
 */

#ifndef _PX_HEAP_SCAN_MERGABLE_LIST_HPP_
#define _PX_HEAP_SCAN_MERGABLE_LIST_HPP_

#if SERVER_MODE && !WINDOWS

#include "px_list_merger.hpp"
#include "query_executor.h"
namespace parallel_heap_scan
{
  class mergable_list_array
  {
    public:
      mergable_list_array() = delete;
      mergable_list_array (THREAD_ENTRY *thread_p, std::size_t size);
      ~mergable_list_array();
      QFILE_LIST_ID *get_merged_list_id();
      QFILE_LIST_ID **get_list_id_p (std::size_t index);

    private:
      THREAD_ENTRY *m_thread_p;
      std::size_t m_size;
      std::vector<QFILE_LIST_ID *> m_list_ids;
  };

  class mergable_list_writer
  {
    public:
      mergable_list_writer() = delete;
      mergable_list_writer (QFILE_LIST_ID **list_id_p, QUERY_ID query_id, VALPTR_LIST *outptr_list);
      ~mergable_list_writer();

      bool open (THREAD_ENTRY *thread_p, PARALLEL_HEAP_SCAN_ID *phsid,
		 REGU_VARIABLE_LIST regu_list_pred, REGU_VARIABLE_LIST regu_list_rest, VAL_DESCR *vd);
      void close (THREAD_ENTRY *thread_p);
      int write (THREAD_ENTRY *thread_p);
      inline bool is_outptr_domain_resolved()
      {
	return (*m_list_id_p)->is_domain_resolved;
      }
      bool is_tfile_allocated() const;

    private:
      QFILE_LIST_ID **m_list_id_p;
      VALPTR_LIST *m_outptr_list;
      QFILE_TUPLE_RECORD m_tpl_buf;
      QUERY_ID m_query_id;
      VAL_DESCR *m_vd;
      int m_error_code;

      QFILE_TUPLE_RECORD *make_tuple_record (THREAD_ENTRY *thread_p);
  };
}

#endif /* SERVER_MODE && !WINDOWS */
#endif /* _PX_HEAP_SCAN_MERGABLE_LIST_HPP_ */
