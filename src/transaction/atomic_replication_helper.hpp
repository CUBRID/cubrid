/*
 * Copyright 2008 Search Solution Corporation
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

#ifndef _ATOMIC_REPLICATION_HELPER_HPP_
#define _ATOMIC_REPLICATION_HELPER_HPP_

#include <map>
#include <deque>

#include "log_lsa.hpp"
#include "log_record.hpp"
#include "page_buffer.h"
#include "thread_entry.hpp"

namespace cublog
{

  class atomic_replication_helper
  {
    public:
      void add_atomic_replication_unit (THREAD_ENTRY *thread_p, PGBUF_WATCHER *watcher, TRANID tranid, log_lsa record_lsa,
					log_rectype record_type, VPID vpid);
      void unfix_atomic_replication_sequence (THREAD_ENTRY *thread_p, PGBUF_WATCHER *pg_watcher, TRANID tranid);
      bool is_part_of_atomic_replication (TRANID tranid) const;
#if !defined (NDEBUG)
      bool is_page_part_of_atomic_replication_sequence (TRANID tranid, VPID vpid) const;
#endif

    private:
      class atomic_replication_unit
      {
	public:
	  atomic_replication_unit (log_lsa lsa, log_rectype rectype, VPID vpid, TRANID record_tranid);

	  void apply_log_redo ();
	  void fix_page (THREAD_ENTRY *thread_p, PGBUF_WATCHER *pg_watcher);
	  void unfix_page (THREAD_ENTRY *thread_p, PGBUF_WATCHER *pg_watcher);
	  VPID get_vpid ();

	private:
	  log_lsa m_record_lsa;
	  log_rectype m_record_type;
	  VPID m_vpid;
	  PAGE_PTR m_page_ptr;
	  TRANID m_record_tranid;
      };

      //Hashmap
      std::map<TRANID, std::deque<atomic_replication_unit>> m_atomic_sequences_map;
  };
}

#endif // _ATOMIC_REPLICATION_HELPER_HPP_
