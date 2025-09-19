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
  class list_id_atomic_vpid
  {
    public:
      std::atomic<VPID> first_vpid;
      std::atomic<VPID> last_vpid;
      std::atomic_bool last_pgptr_released;
      std::atomic_bool ended;
      list_id_atomic_vpid() : first_vpid ({-1,-1}), last_vpid ({-1,-1}), last_pgptr_released (false), ended (false) {}
      ~list_id_atomic_vpid() {}

      list_id_atomic_vpid (const list_id_atomic_vpid &other)
	: first_vpid (other.first_vpid.load()), last_vpid (other.last_vpid.load()),
	  last_pgptr_released (other.last_pgptr_released.load()), ended (other.ended.load()) {}

      list_id_atomic_vpid &operator= (const list_id_atomic_vpid &other)
      {
	if (this != &other)
	  {
	    first_vpid.store (other.first_vpid.load());
	    last_vpid.store (other.last_vpid.load());
	    last_pgptr_released.store (other.last_pgptr_released.load());
	    ended.store (other.ended.load());
	  }
	return *this;
      }
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
				    bool should_check_instnum, err_messages_with_lock *err_messages_p, int parallelism)
	: result_handler (query_id, interrupt_p, atomic_instnum_p, should_check_instnum, err_messages_p,
			  RESULT_TYPE::XASL_SNAPSHOT)
      {
	m_parallelism = parallelism;
	m_result_list_ids_count = 0;
	m_is_waiting_atomic_vpid = false;
	m_current_list_id_index = -1;
      }

    private:

      /* for reader */
      std::vector<QFILE_LIST_SCAN_ID> m_reader_result_list_scan_ids;
      int m_current_list_id_index;
      QFILE_TUPLE_RECORD m_reader_tpl_buf;

      /* for writer */
      std::vector<QFILE_LIST_ID *> m_writer_result_list_ids;
      std::mutex m_writer_result_list_ids_mutex;
      thread_local static QFILE_LIST_ID *m_tl_writer_result_list_id;
      thread_local static QFILE_TUPLE_RECORD *m_tl_tpl_buf;

      /* for both */
      int m_parallelism;
      std::mutex m_result_list_ids_mutex;
      std::condition_variable m_result_list_ids_condition_variable;
      int m_result_list_ids_count;

      std::vector<list_id_atomic_vpid> m_result_list_ids_atomic_vpid;
      std::mutex m_result_list_ids_atomic_vpid_mutex;
      std::condition_variable m_result_list_ids_atomic_vpid_condition_variable;
      bool m_is_waiting_atomic_vpid;

  };

  int update_domains_on_type_list_by_val_list (THREAD_ENTRY *thread_p, QFILE_LIST_ID *list_id_p, VAL_LIST *val_list_p);

}


#endif /*_PX_HEAP_SCAN_RESULT_HANDLER_XASL_SNAPSHOT_HPP_ */
