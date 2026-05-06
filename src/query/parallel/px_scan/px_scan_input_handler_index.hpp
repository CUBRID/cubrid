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
 * px_scan_input_handler_index.hpp
 */

#ifndef _PX_SCAN_INPUT_HANDLER_INDEX_HPP_
#define _PX_SCAN_INPUT_HANDLER_INDEX_HPP_

#include "px_interrupt.hpp"
#include "scan_manager.h"
#include "access_spec.hpp"
#include "btree.h"
#include "dbtype.h"
#include <mutex>

namespace parallel_scan
{
  class input_handler_index
  {
      using interrupt = parallel_query::interrupt;
      using err_messages_with_lock = parallel_query::err_messages_with_lock;
    public:
      input_handler_index (interrupt *interrupt_p, err_messages_with_lock *err_messages_p)
	: m_leaf_ended (true),
	  m_descent_done (false),
	  m_interrupt_p (interrupt_p),
	  m_err_messages_p (err_messages_p),
	  m_indx_info (nullptr),
	  m_use_desc_index (false),
	  m_scan_id (nullptr),
	  m_vd (nullptr),
	  m_descent_key_valid (false),
	  m_descent_key_initialized (false)
      {
	memset (&m_btid_int, 0, sizeof (m_btid_int));
	memset (&m_btid, 0, sizeof (m_btid));
	memset (&m_descent_key_range, 0, sizeof (m_descent_key_range));
	db_make_null (&m_descent_key_range.key1);
	db_make_null (&m_descent_key_range.key2);
	VPID_SET_NULL (&m_current_leaf_vpid);
      }
      int init_on_main (THREAD_ENTRY *thread_p, INDX_INFO *indx_info, SCAN_ID *scan_id, val_descr *vd, int parallelism);

      /* single READ-latch fix; out_page ownership transfers on S_SUCCESS, first call descends from root with latch coupling */
      SCAN_CODE get_next_page_with_fix (THREAD_ENTRY *thread_p, PAGE_PTR &out_page);
      int initialize (THREAD_ENTRY *thread_p, HFID *hfid, SCAN_ID *scan_id);
      int finalize (THREAD_ENTRY *thread_p);
      void cleanup_keys (THREAD_ENTRY *thread_p);

      BTID_INT *get_btid_int ()
      {
	return &m_btid_int;
      }

      INDX_INFO *get_indx_info ()
      {
	return m_indx_info;
      }

      bool is_desc_index () const
      {
	return m_use_desc_index;
      }

    private:
      /* requires m_leaf_mutex; on S_SUCCESS out_leaf is READ-latched and m_btid_int is populated */
      SCAN_CODE descend_to_first_leaf (THREAD_ENTRY *thread_p, PAGE_PTR &out_leaf);
      /* one-shot evaluation of a single-range descent key from m_indx_info */
      void try_prepare_descent_key (THREAD_ENTRY *thread_p);

      VPID m_current_leaf_vpid;         /* next leaf to fix (mutex-protected) */
      bool m_leaf_ended;
      bool m_descent_done;              /* first worker has descended to a leaf */
      std::mutex m_leaf_mutex;
      BTID_INT m_btid_int;
      BTID m_btid;
      interrupt *m_interrupt_p;
      err_messages_with_lock *m_err_messages_p;
      INDX_INFO *m_indx_info;          /* original INDX_INFO pointer for workers */
      bool m_use_desc_index;

      SCAN_ID *m_scan_id;              /* needed by scan_regu_key_to_index_key */
      val_descr *m_vd;                 /* needed by scan_regu_key_to_index_key */
      KEY_VAL_RANGE m_descent_key_range;
      bool m_descent_key_valid;        /* true → key-compare descent; false → endmost-slot descent */
      bool m_descent_key_initialized;  /* one-shot guard for try_prepare_descent_key */
  };
}

#endif /* _PX_SCAN_INPUT_HANDLER_INDEX_HPP_ */
