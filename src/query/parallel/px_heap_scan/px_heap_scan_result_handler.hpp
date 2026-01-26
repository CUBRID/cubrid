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
 * px_heap_scan_result_handler.hpp
 */

#ifndef _PX_HEAP_SCAN_RESULT_HANDLER_HPP_
#define _PX_HEAP_SCAN_RESULT_HANDLER_HPP_

#include "query_list.h"
#include "storage_common.h"
#include "thread_entry.hpp"
#include "px_interrupt.hpp"
#include "xasl.h"
#include "px_heap_scan_result_type.hpp"

namespace parallel_heap_scan
{
  class list_id_header;
  struct read_spec;
  union VPID64_t;
  class mergeable_list_variables;
  class xasl_snapshot_variables;
  class mergeable_list_tls;
  class xasl_snapshot_tls;

  template <RESULT_TYPE result_type>
  class result_handler
  {
      using interrupt = parallel_query::interrupt;
      using err_messages_with_lock = parallel_query::err_messages_with_lock;
      using read_dest_type = std::conditional_t<result_type == RESULT_TYPE::MERGEABLE_LIST, QFILE_LIST_ID, VAL_LIST>;
      using write_dest_type = std::conditional_t<result_type == RESULT_TYPE::MERGEABLE_LIST, OUTPTR_LIST, VAL_LIST>;
      using variables =
	      std::conditional_t<result_type == RESULT_TYPE::MERGEABLE_LIST, mergeable_list_variables, xasl_snapshot_variables>;
      using tls = std::conditional_t<result_type == RESULT_TYPE::MERGEABLE_LIST, mergeable_list_tls, xasl_snapshot_tls>;
    public:
      result_handler (QUERY_ID query_id, interrupt *interrupt_p, err_messages_with_lock *err_messages_p, int parallelism,
		      bool g_agg_domain_resolve_need, VAL_LIST *orig_val_list_for_domain_resolve);
      void read_initialize (THREAD_ENTRY *thread_p);
      SCAN_CODE read (THREAD_ENTRY *thread_p, read_dest_type *dest);
      void read_finalize (THREAD_ENTRY *thread_p);
      void write_initialize (THREAD_ENTRY *thread_p, OUTPTR_LIST *outptr_list, VAL_LIST *val_list, VAL_DESCR *vd);
      bool write (THREAD_ENTRY *thread_p, write_dest_type *src);
      void write_finalize (THREAD_ENTRY *thread_p);

    private:
      void get_valid_read_spec ();
      /* common */
      int m_parallelism;
      std::mutex m_result_mutex;
      std::condition_variable m_result_cv;
      QUERY_ID m_query_id;
      interrupt *m_interrupt_p; /* for interrupt */
      err_messages_with_lock *m_err_messages_p; /* for error messages */
      /* specific */
      variables m_;
      thread_local static tls tl;
  };

  class mergeable_list_variables
  {
    public:
      mergeable_list_variables()
	: result_p (nullptr),
	  orig_val_list_for_domain_resolve (nullptr),
	  active_results (0),
	  is_list_id_domain_resolved (false) {}
      ~mergeable_list_variables() = default;
      std::vector<QFILE_LIST_ID *> writer_results;
      std::mutex writer_results_mutex;
      QFILE_LIST_ID *result_p;
      VAL_LIST *orig_val_list_for_domain_resolve;
      int active_results;
      bool is_list_id_domain_resolved;
  };

  class xasl_snapshot_variables
  {
    public:
      xasl_snapshot_variables()
	: list_id_header_index (0),
	  current_read_spec (nullptr) {}
      ~xasl_snapshot_variables() = default;
      std::vector<list_id_header> list_id_headers;
      std::vector<read_spec> read_specs;
      std::atomic_int list_id_header_index;
      read_spec *current_read_spec;
  };

  class mergeable_list_tls
  {
    public:
      mergeable_list_tls()
	: writer_result_p (nullptr),
	  vd (nullptr),
	  val_list_for_domain_resolve (nullptr),
	  val_list_domain_resolved (false) {}
      ~mergeable_list_tls() = default;
      QFILE_LIST_ID *writer_result_p;
      QFILE_TUPLE_RECORD tpl_buf;
      VAL_DESCR *vd;
      VAL_LIST *val_list_for_domain_resolve;
      std::vector<DB_VALUE> dbvals_for_domain_resolve;
      bool val_list_domain_resolved;
  };

  class xasl_snapshot_tls
  {
    public:
      xasl_snapshot_tls()
	: list_id_header_p (nullptr) {}
      ~xasl_snapshot_tls() = default;
      list_id_header *list_id_header_p;
      QFILE_TUPLE_RECORD tpl_buf;
  };

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

  template <>
  class result_handler <RESULT_TYPE::COUNT_DISTINCT>
  {
      using interrupt = parallel_query::interrupt;
      using err_messages_with_lock = parallel_query::err_messages_with_lock;
      using read_dest_type = AGGREGATE_TYPE;
      using write_dest_type = AGGREGATE_TYPE;
    public:
      result_handler (QUERY_ID query_id, interrupt *interrupt_p, err_messages_with_lock *err_messages_p, int parallelism,
		      AGGREGATE_TYPE *orig_agg_list);
      void read_initialize (THREAD_ENTRY *thread_p);
      SCAN_CODE read (THREAD_ENTRY *thread_p, read_dest_type *dest);
      void read_finalize (THREAD_ENTRY *thread_p);
      void write_initialize (THREAD_ENTRY *thread_p, OUTPTR_LIST *outptr_list, write_dest_type *agg_list, VAL_DESCR *vd,
			     xasl_node *xasl_p);
      bool write (THREAD_ENTRY *thread_p);
      void write_finalize (THREAD_ENTRY *thread_p);
    private:
      int m_parallelism;
      std::mutex m_result_mutex;
      std::condition_variable m_result_cv;
      int m_result_completed;
      QUERY_ID m_query_id;
      interrupt *m_interrupt_p; /* for interrupt */
      err_messages_with_lock *m_err_messages_p; /* for error messages */
      AGGREGATE_TYPE *m_orig_agg_list;
      std::mutex writer_results_mutex;
      thread_local static AGGREGATE_TYPE *tl_agg_p;
      thread_local static OUTPTR_LIST *tl_outptr_list_p;
      thread_local static VAL_DESCR *tl_vd;
      thread_local static xasl_node *tl_xasl_p;
      thread_local static QFILE_TUPLE_RECORD tl_tpl_buf;
      thread_local static OR_BUF tl_or_buf;
  };
}

#endif /*_PX_HEAP_SCAN_RESULT_HANDLER_HPP_ */
