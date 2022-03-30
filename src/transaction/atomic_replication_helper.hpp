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

#include "log_lsa.hpp"
#include "log_record.hpp"

namespace cublog
{

  class atomic_replication_helper
  {
    public:
      void add_atomic_replication_unit ();
      void apply_log_redo_on_atomic_replication_sequence ();
      void unfix_atomic_replication_sequence ();

    private:
      class atomic_replication_unit
      {
	public:
	  void apply_log_redo ();
	  PAGE_PTR fix_atomic_replication_unit ();
	  void unfix_atomic_replication_unit ();

	private:
	  log_lsa m_record_lsa;
	  log_rectype m_record_type;
	  VPID m_vpid;
	  PAGE_PTR m_page_ptr;
	  LOG_RCVINDEX m_record_index;
	  TRANID m_record_tran_id;
      };

      //Hashmap
      std::map<TRANID, atomic_replication_unit> m_atomic_sequences_map;
  };
}

#endif // _ATOMIC_REPLICATION_HELPER_HPP_
