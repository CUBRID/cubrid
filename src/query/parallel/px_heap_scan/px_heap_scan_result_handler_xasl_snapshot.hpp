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
  union VPID64_t
  {
    uint64_t uint64;
    VPID vpid;
  };

  class list_id_header
  {
    public:
      std::atomic<VPID64_t> m_first_vpid;
      std::atomic<VPID64_t> m_last_vpid;
      std::atomic<bool> m_list_closed;
      std::atomic<bool> m_valid;
      QFILE_LIST_ID *m_list_id_p;
      std::vector<std::atomic<TP_DOMAIN *>*> m_type_list;
      int m_type_cnt;

      list_id_header()
	: m_first_vpid(), m_last_vpid(), m_list_closed (false), m_valid (false),
	  m_list_id_p (nullptr), m_type_cnt (0) {}
      list_id_header (const list_id_header &other)
	: m_first_vpid (other.m_first_vpid.load()),
	  m_last_vpid (other.m_last_vpid.load()),
	  m_list_closed (other.m_list_closed.load()),
	  m_valid (other.m_valid.load()),
	  m_list_id_p (other.m_list_id_p),
	  m_type_list (other.m_type_list),
	  m_type_cnt (other.m_type_cnt) {}
      list_id_header (list_id_header &&other) noexcept
	: m_first_vpid (other.m_first_vpid.load()),
	  m_last_vpid (other.m_last_vpid.load()),
	  m_list_closed (other.m_list_closed.load()),
	  m_valid (other.m_valid.load()),
	  m_list_id_p (other.m_list_id_p),
	  m_type_list (std::move (other.m_type_list)),
	  m_type_cnt (other.m_type_cnt)
      {
	other.m_list_id_p = nullptr;
	other.m_type_cnt = 0;
	other.m_list_closed.store (false);
	other.m_valid.store (false);
      }
      list_id_header &operator= (const list_id_header &other)
      {
	if (this != &other)
	  {
	    m_first_vpid.store (other.m_first_vpid.load());
	    m_last_vpid.store (other.m_last_vpid.load());
	    m_list_closed.store (other.m_list_closed.load());
	    m_valid.store (other.m_valid.load());
	    m_list_id_p = other.m_list_id_p;
	    m_type_list = other.m_type_list;
	    m_type_cnt = other.m_type_cnt;
	  }
	return *this;
      }
      list_id_header &operator= (list_id_header &&other) noexcept
      {
	if (this != &other)
	  {
	    m_first_vpid.store (other.m_first_vpid.load());
	    m_last_vpid.store (other.m_last_vpid.load());
	    m_list_closed.store (other.m_list_closed.load());
	    m_valid.store (other.m_valid.load());
	    m_list_id_p = other.m_list_id_p;
	    m_type_list = std::move (other.m_type_list);
	    m_type_cnt = other.m_type_cnt;
	    other.m_list_id_p = nullptr;
	    other.m_type_cnt = 0;
	    other.m_list_closed.store (false);
	    other.m_valid.store (false);
	  }
	return *this;
      }
  };
  struct read_spec
  {
    list_id_header *list_id_header_p;
    bool read_ended;
    bool list_scan_id_opened;
    QFILE_LIST_SCAN_ID list_scan_id;
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
      int m_parallelism;

      /* reader-writer communication */
      std::mutex m_cv_mutex;
      std::condition_variable m_readable_list_exists_cv;

      /* storage */
      std::vector<list_id_header> m_list_id_headers;
      std::vector<read_spec> m_read_specs;
      std::atomic_int m_list_id_header_index;
      thread_local static list_id_header *tl_list_id_header;
      thread_local static QFILE_TUPLE_RECORD tl_tpl_buf;

      /* for continuable read */
      read_spec *m_current_read_spec;
      void get_valid_read_spec ();
  };

  int update_domains_on_type_list_by_val_list (THREAD_ENTRY *thread_p, QFILE_LIST_ID *list_id_p, VAL_LIST *val_list_p);
}


#endif /*_PX_HEAP_SCAN_RESULT_HANDLER_XASL_SNAPSHOT_HPP_ */
