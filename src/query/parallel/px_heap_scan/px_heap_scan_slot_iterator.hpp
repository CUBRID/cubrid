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
 * px_heap_scan_slot_iterator.hpp
 */

#ifndef _PX_HEAP_SCAN_SLOT_ITERATOR_HPP_
#define _PX_HEAP_SCAN_SLOT_ITERATOR_HPP_

#include "heap_file.h"
#include "query_executor.h"
#include "storage_common.h"
#include "scan_manager.h"
#include "xasl.h"

namespace parallel_heap_scan
{
  class slot_iterator
  {
    public:
      slot_iterator();
      ~slot_iterator();
      int initialize (THREAD_ENTRY *thread_p, SCAN_ID *scan_id, val_descr *vd);
      int finalize (THREAD_ENTRY *thread_p);
      int set_page (THREAD_ENTRY *thread_p, VPID *vpid);
      SCAN_CODE next_qualified_slot_with_peek (THREAD_ENTRY *thread_p);

    private:
      FILTER_INFO m_data_filter;
      bool m_is_peeking;
      OID m_cur_oid;
      OID m_class_oid;
      OID m_next_oid;
      RECDES m_recdes;
      LOG_LSA m_ref_lsa;
      regu_variable_list_node *m_rest_regu_list;
      HEAP_CACHE_ATTRINFO *m_rest_attr_cache;
      VAL_LIST *m_val_list;
      HEAP_SCANCACHE *m_scan_cache;
      VPID m_vpid;
      HFID m_hfid;
      val_descr *m_vd;
      SCAN_STATS *m_scan_stats;
      bool m_on_trace;
  };
}

#endif /*_PX_HEAP_SCAN_SLOT_ITERATOR_HPP_ */
