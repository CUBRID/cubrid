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


#include "log_replication_mvcc.hpp"

#include "log_impl.h"
#include "thread_entry.hpp"

namespace cublog
{
  replicator_mvcc::~replicator_mvcc ()
  {
    // passive transaction server can be shutdown at any moment, in any replication state
    // thus, no verification here
  }

  void
  replicator_mvcc::new_assigned_mvccid (TRANID tranid, MVCCID mvccid)
  {
    assert (m_mapped_mvccids.find (tranid) == m_mapped_mvccids.cend ());

    m_mapped_mvccids.emplace (tranid, mvccid);
  }

  void
  replicator_mvcc::complete_mvccid (TRANID tranid, bool committed)
  {
    const map_type::iterator found_it = m_mapped_mvccids.find (tranid);

    if (found_it != m_mapped_mvccids.cend ())
      {
	const MVCCID found_mvccid = found_it->second;
	log_Gl.mvcc_table.complete_mvcc (tranid, found_mvccid, committed);
	m_mapped_mvccids.erase (found_it);
      }
    // if not found, it means the transaction contains proper MVCC log records
  }
}
