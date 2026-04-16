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
#include "btree.h"
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
          m_interrupt_p (interrupt_p),
          m_err_messages_p (err_messages_p),
          m_indx_info (nullptr),
          m_use_desc_index (false)
      {
        memset (&m_btid_int, 0, sizeof (m_btid_int));
        memset (&m_btid, 0, sizeof (m_btid));
        VPID_SET_NULL (&m_current_leaf_vpid);
      }
      int init_on_main (THREAD_ENTRY *thread_p, INDX_INFO *indx_info, int parallelism);
      SCAN_CODE get_next_vpid_with_fix (THREAD_ENTRY *thread_p, VPID *vpid);
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
      VPID m_current_leaf_vpid;         // shared cursor (mutex-protected)
      bool m_leaf_ended;
      std::mutex m_leaf_mutex;
      BTID_INT m_btid_int;
      BTID m_btid;
      INDX_INFO *m_indx_info;          // original INDX_INFO pointer for workers
      bool m_use_desc_index;
      interrupt *m_interrupt_p;
      err_messages_with_lock *m_err_messages_p;
  };
}

#endif /* _PX_SCAN_INPUT_HANDLER_INDEX_HPP_ */
