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
 * px_scan_index_key_range_list.hpp
 */

#ifndef _PX_SCAN_INDEX_KEY_RANGE_LIST_HPP_
#define _PX_SCAN_INDEX_KEY_RANGE_LIST_HPP_

#include "access_spec.hpp"
#include "btree.h"
#include "dbtype.h"
#include "scan_manager.h"
#include "storage_common.h"

#include <vector>
#include <cstdint>

namespace parallel_index_scan
{
  /* main-thread lifecycle: init_on_main converts KEY_RANGE -> key_val_range once; after that read-only. */
  class key_range_list
  {
    public:
      key_range_list ()
	: m_indx_info (nullptr),
	  m_use_desc_index (false),
	  m_key_val_ranges (),
	  m_part_key_desc (false)
      {
	memset (&m_btid_int, 0, sizeof (m_btid_int));
	memset (&m_btid, 0, sizeof (m_btid));
      }

      int init_on_main (THREAD_ENTRY *thread_p, INDX_INFO *indx_info, SCAN_ID *scan_id, val_descr *vd);
      void cleanup_keys (THREAD_ENTRY *thread_p);

      BTID_INT *get_btid_int ()
      {
	return &m_btid_int;
      }
      BTID *get_btid ()
      {
	return &m_btid;
      }
      INDX_INFO *get_indx_info ()
      {
	return m_indx_info;
      }
      bool is_desc_index () const
      {
	return m_use_desc_index;
      }
      key_val_range *get_key_val_ranges ()
      {
	return m_key_val_ranges.empty () ? nullptr : m_key_val_ranges.data ();
      }
      int get_num_key_ranges () const
      {
	return static_cast<int> (m_key_val_ranges.size ());
      }

    private:
      int convert_all_key_ranges (THREAD_ENTRY *thread_p, SCAN_ID *worker_scan_id, val_descr *vd);

      BTID_INT m_btid_int;
      BTID m_btid;
      INDX_INFO *m_indx_info;
      bool m_use_desc_index;
      /* std::vector — alloc/dealloc thread-context-independent (any worker may finalize). */
      std::vector<key_val_range> m_key_val_ranges;
      bool m_part_key_desc;
  };
}

#endif /* _PX_SCAN_INDEX_KEY_RANGE_LIST_HPP_ */
