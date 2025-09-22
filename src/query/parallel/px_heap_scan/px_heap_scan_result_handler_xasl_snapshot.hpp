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
 * px_heap_scan_result_handler_xasl_snapshot.hpp
 */

#ifndef _PX_HEAP_SCAN_RESULT_HANDLER_XASL_SNAPSHOT_HPP_
#define _PX_HEAP_SCAN_RESULT_HANDLER_XASL_SNAPSHOT_HPP_

#include "px_heap_scan_result_handler.hpp"
#include "query_list.h"

namespace parallel_heap_scan
{
  class list_id_header
  {
    public:
      VPID m_first_vpid;
      VPID m_last_vpid;
      bool m_list_closed;
      bool m_valid;
      QFILE_TUPLE_VALUE_TYPE_LIST m_type_list;
      QFILE_LIST_ID *m_list_id_p;
      std::mutex m_mutex;

      list_id_header();
      ~list_id_header();
  };

  class result_handler_xasl_snapshot : public result_handler<VAL_LIST, VAL_LIST>
  {
      using interrupt = parallel_query::interrupt;
      using atomic_instnum = parallel_query::atomic_instnum;
      using err_messages_with_lock = parallel_query::err_messages_with_lock;
    public:
      ~result_handler_xasl_snapshot() = default;
      void read_initialize (THREAD_ENTRY *thread_p) override;
      SCAN_CODE get_next (THREAD_ENTRY *thread_p, VAL_LIST *result) override;
      void read_finalize (THREAD_ENTRY *thread_p) override;
      void write_initialize (THREAD_ENTRY *thread_p) override;
      bool write (THREAD_ENTRY *thread_p, VAL_LIST *input) override;
      void write_finalize (THREAD_ENTRY *thread_p) override;

      result_handler_xasl_snapshot (QUERY_ID query_id, interrupt *interrupt_p, atomic_instnum *atomic_instnum_p,
				    bool should_check_instnum, err_messages_with_lock *err_messages_p, int parallelism);

    private:
      struct list_id_header_for_read
      {
	list_id_header *m_list_id_header;
	QFILE_LIST_SCAN_ID m_list_scan_id;
	bool m_read_ended;
	bool m_list_scan_id_opened;
      };
      /* for both */
      int m_parallelism;
      std::mutex m_cv_mutex;
      std::condition_variable m_readable_list_exists_cv;
      bool m_reader_wait;
      int m_reader_list_id_index_hint;
      std::atomic_int m_writer_ended_cnt;
      std::atomic_int m_writer_null_list_id_ended_cnt;

      /* for writer */
      std::vector<list_id_header> m_writer_list_id_headers;
      std::atomic_int m_writer_list_id_index;
      thread_local static list_id_header *m_tl_writer_list_id_header;
      thread_local static QFILE_TUPLE_RECORD m_tl_tpl_buf;
      thread_local static int m_tl_list_id_index;

      /* for reader */
      std::vector<list_id_header_for_read> m_reader_list_id_headers;
      QFILE_TUPLE_RECORD m_reader_tpl_buf;
      list_id_header m_current_list_id_header;
      list_id_header_for_read *m_current_list_id_header_for_read;

      /* helper functions */
      bool get_next_available_list_id_header ();
      void send_prev_vpid_to_reader (VPID prev_vpid);

  };

  /* helper functions */
  bool get_list_id_header_if_readable (list_id_header *src_list_id_header, list_id_header *dest_list_id_header);
  bool is_next_tuple_on_not_readable_page (list_id_header *list_id_header, QFILE_LIST_SCAN_ID *list_scan_id);

  int update_domains_on_type_list_by_val_list (THREAD_ENTRY *thread_p, QFILE_LIST_ID *list_id_p, VAL_LIST *val_list_p);
}


#endif /*_PX_HEAP_SCAN_RESULT_HANDLER_XASL_SNAPSHOT_HPP_ */
