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
 * px_scan_slot_iterator_list.hpp
 */

#ifndef _PX_SCAN_SLOT_ITERATOR_LIST_HPP_
#define _PX_SCAN_SLOT_ITERATOR_LIST_HPP_

#include "query_evaluator.h"
#include "query_list.h"
#include "query_manager.h"
#include "scan_manager.h"
#include "storage_common.h"

namespace parallel_scan
{
  class slot_iterator_list
  {
    public:
      slot_iterator_list ();
      ~slot_iterator_list ();
      int initialize (THREAD_ENTRY *thread_p, SCAN_ID *scan_id, val_descr *vd);
      int finalize (THREAD_ENTRY *thread_p);
      /* adopts page pre-fixed by input_handler_list (no re-fix). */
      int set_page (THREAD_ENTRY *thread_p, PAGE_PTR page, QMGR_TEMP_FILE *tfile);
      SCAN_CODE next_qualified_slot_with_peek (THREAD_ENTRY *thread_p);

    private:
      PAGE_PTR m_curr_pgptr;
      QFILE_TUPLE m_curr_tpl;
      int m_curr_tplno;
      int m_tuple_count;
      QMGR_TEMP_FILE *m_curr_tfile;
      QFILE_LIST_ID *m_list_id;
      SCAN_PRED m_scan_pred;
      regu_variable_list_node *m_rest_regu_list;
      QFILE_TUPLE_RECORD *m_tplrecp;
      QFILE_TUPLE_RECORD m_tplrec;
      val_list_node *m_val_list;
      val_descr *m_vd;
      SCAN_STATS *m_scan_stats;
      bool m_on_trace;
  };
}

#endif /*_PX_SCAN_SLOT_ITERATOR_LIST_HPP_ */
