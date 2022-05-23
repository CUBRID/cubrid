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
#include "system_parameter.h"
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
    assert (MVCCID_IS_NORMAL (mvccid));
    //assert (m_mapped_mvccids.find (tranid) == m_mapped_mvccids.cend ());

    const auto found_it = m_mapped_mvccids.find (tranid);
    if (found_it == m_mapped_mvccids.cend ())
      {
	m_mapped_mvccids.emplace (tranid, mvccid);
      }
    else
      {
	// only one mvccid per transaction is assumed
	// sub-transaction mvccid's are not implemented yet
	assert (found_it->second == mvccid);
      }

    if (prm_get_bool_value (PRM_ID_ER_LOG_PTS_REPL_DEBUG))
      {
	_er_log_debug (ARG_FILE_LINE, "[REPLICATOR_MVCC] new_assigned_mvccid tranid=%d mvccid=%lld\n",
		       tranid, (long long)mvccid);
	dump_map ();
      }
  }

  void
  replicator_mvcc::complete_mvcc (TRANID tranid, bool committed)
  {
    const map_type::iterator found_it = m_mapped_mvccids.find (tranid);

    if (found_it != m_mapped_mvccids.cend ())
      {
	const MVCCID found_mvccid = found_it->second;
	log_Gl.mvcc_table.complete_mvcc (tranid, found_mvccid, committed);
	m_mapped_mvccids.erase (found_it);

	if (prm_get_bool_value (PRM_ID_ER_LOG_PTS_REPL_DEBUG))
	  {
	    _er_log_debug (ARG_FILE_LINE, "[REPLICATOR_MVCC] complete_mvcc FOUND tranid=%d mvccid=%lld %s\n",
			   tranid, (long long)found_mvccid, (committed ? "COMMITED" : "ABORTED"));
	    dump_map ();
	  }
      }
    else
      {
        // if not found the transaction never assigned an mvccid
        // TODO: if not found:
        //  - if the transaction has no sub-transaction
        // , it means the transaction contains proper MVCC log records
	if (prm_get_bool_value (PRM_ID_ER_LOG_PTS_REPL_DEBUG))
	  {
	    _er_log_debug (ARG_FILE_LINE, "[REPLICATOR_MVCC] complete_mvcc NOT_FOUND tranid=%d %s\n",
			   tranid, (committed ? "COMMITED" : "ABORTED"));
	    dump_map ();
	  }
      }
  }

  void
  replicator_mvcc::dump_map () const
  {
#if !defined (NDEBUG)
    int index = 1;
    for (const auto &pair: m_mapped_mvccids)
      {
	_er_log_debug (ARG_FILE_LINE, "[REPLICATOR_MVCC] index=%d/%d tranid=%d mvccid=%lld\n",
		       index, m_mapped_mvccids.size (), pair.first, (long long)pair.second);
	++index;
      }
#endif /* !NDEBUG */
  }
}
