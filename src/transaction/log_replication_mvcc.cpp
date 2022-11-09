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
#include "server_type.hpp"
#include "system_parameter.h"
#include "thread_entry.hpp"

namespace cublog
{
  replicator_mvcc::replicator_mvcc ()
  {
    assert (is_passive_transaction_server ());
  }

  replicator_mvcc::~replicator_mvcc ()
  {
    // passive transaction server can be shutdown at any moment, in any replication state
    // thus, no verification here
  }

  void
  replicator_mvcc::new_assigned_mvccid (TRANID tranid, MVCCID mvccid)
  {
    assert (MVCCID_IS_NORMAL (mvccid));

    const auto found_it = m_mapped_mvccids.find (tranid);
    if (found_it == m_mapped_mvccids.cend ())
      {
	m_mapped_mvccids.emplace (tranid, tran_mvccid_info { mvccid });
      }
    else
      {
	// assert that main mvccid and sub-mvccid are consistent;
	// this is the consequence of the following scenario which includes selupd operations
	// (see qexec_execute_selupd_list function):
	//  - both a new main mvccid and sub-mvccid are allocated at the same time (see implementation
	//    of logtb_get_new_subtransaction_mvccid function)
	//  - during the selupd implementation and transaction logging, first the sub-mvccid appears
	//    as part of log records and it is registered as such
	//  - at some point both the main mvccid and the sub-mvccid appear as part of a
	//    LOG_SYSOP_END_LOGICAL_MVCC_UNDO record; in which case we re-assign the - previously thought -
	//    main mvccid as sub-mvccid and the new main mvccid as proper main
	//  - subsequently, MVCC log records are replicated with the sub-mvccid figuring as 'main' again
	//
	// TODO: might this situation actually be a transactional logging bug?
	assert (found_it->second.m_id == mvccid
		|| (found_it->second.m_sub_ids.size () == 1
		    && found_it->second.m_sub_ids[0] == mvccid));
      }

    if (prm_get_bool_value (PRM_ID_ER_LOG_MVCC_REPL_DEBUG))
      {
	_er_log_debug (ARG_FILE_LINE, "[REPL_MVCC] new_assigned_mvccid tranid=%d mvccid=%llu\n",
		       tranid, (unsigned long long)mvccid);
	dump ();
      }
  }

  void
  replicator_mvcc::new_assigned_sub_mvccid_or_mvccid (TRANID tranid, MVCCID mvccid, MVCCID parent_mvccid)
  {
    assert (MVCCID_IS_NORMAL (mvccid));

    if (MVCCID_IS_VALID (parent_mvccid))
      {
	// mvccid is a sub-id, as it has a valid parent mvccid
	assert (MVCCID_IS_NORMAL (parent_mvccid));

	const auto found_it = m_mapped_mvccids.find (tranid);
	assert_release (found_it != m_mapped_mvccids.cend ());
	if (found_it != m_mapped_mvccids.cend ())
	  {
	    if (found_it->second.m_id == parent_mvccid)
	      {
		// all good, previously seen mvccid is an actual proper parent/main transaction mvccid
		// fall through to assign the sub-mvccid
	      }
	    else
	      {
		// previosly seen, "main" mvccid, appears now as a sub-mvccid
		// see comment above, before assert in new_assigned_mvccid function
		if (prm_get_bool_value (PRM_ID_ER_LOG_MVCC_REPL_DEBUG))
		  {
		    _er_log_debug (ARG_FILE_LINE, "[REPL_MVCC] new_assigned_sub_mvccid_or_mvccid"
				   " WARNING previosly seen main mvccid, appears now as a sub-mvccid"
				   " tranid=%d parent_mvccid=%llu\n",
				   tranid, (unsigned long long)parent_mvccid);
		    dump ();
		  }
		// re-assign previosly seen "main"
		assert (found_it->second.m_id == mvccid);
		found_it->second.m_id = parent_mvccid;
	      }

	    assert (found_it->second.m_sub_ids.empty ());
	    found_it->second.m_sub_ids.push_back (mvccid);
	  }

	if (prm_get_bool_value (PRM_ID_ER_LOG_MVCC_REPL_DEBUG))
	  {
	    _er_log_debug (ARG_FILE_LINE, "[REPL_MVCC] new_assigned_sub_mvccid_or_mvccid tranid=%d"
			   " mvccid=%llu parent_mvccid=%llu\n",
			   tranid, (unsigned long long)mvccid, (unsigned long long)parent_mvccid);
	    dump ();
	  }
      }
    else
      {
	// mvccid is not a sub-id, as no valid parent mvccid exists
	new_assigned_mvccid (tranid, mvccid);
      }
  }

  void
  replicator_mvcc::complete_mvcc (TRANID tranid, bool committed)
  {
    const map_type::iterator found_it = m_mapped_mvccids.find (tranid);

    if (found_it != m_mapped_mvccids.cend ())
      {
	// all sub-ids should have already been completed
	assert (found_it->second.m_sub_ids.empty ());

	if (prm_get_bool_value (PRM_ID_ER_LOG_MVCC_REPL_DEBUG))
	  {
	    _er_log_debug (ARG_FILE_LINE, "[REPL_MVCC] complete_mvcc FOUND tranid=%d mvccid=%llu %s\n",
			   tranid, (unsigned long long)found_it->second.m_id, (committed ? "COMMITED" : "ABORTED"));
	    dump ();
	  }

	// TODO: temporary using system transaction to complete MVCC; if this proves to be incorrect
	// another solution is to reserve an extra transaction in the transaction table (eg: transaction
	// at index 1) and use that specifically for transactional log replication MVCC completion;
	// also, this relates to the transaction index used in the replicator thread (see function
	// replicator::redo_upto_nxio_lsa
	log_Gl.mvcc_table.complete_mvcc (LOG_SYSTEM_TRAN_INDEX, found_it->second.m_id, committed);

	m_mapped_mvccids.erase (found_it);
      }
    else
      {
	// if not found the transaction never assigned an mvccid
	if (prm_get_bool_value (PRM_ID_ER_LOG_MVCC_REPL_DEBUG))
	  {
	    _er_log_debug (ARG_FILE_LINE, "[REPL_MVCC] complete_mvcc NOT_FOUND tranid=%d %s\n",
			   tranid, (committed ? "COMMITED" : "ABORTED"));
	    dump ();
	  }
      }
  }

  void
  replicator_mvcc::complete_sub_mvcc (TRANID tranid)
  {
    const auto found_it = m_mapped_mvccids.find (tranid);
    //assert (found_it != m_mapped_mvccids.cend ());

    // transaction might not have had an mvccid yet
    if (found_it != m_mapped_mvccids.cend ())
      {
	// even if transaction does have an mvccid, it might not have a sub-id
	if (!found_it->second.m_sub_ids.empty ())
	  {
	    assert (found_it->second.m_sub_ids.size () == 1);
	    log_Gl.mvcc_table.complete_sub_mvcc (found_it->second.m_sub_ids.back ());

	    if (prm_get_bool_value (PRM_ID_ER_LOG_MVCC_REPL_DEBUG))
	      {
		_er_log_debug (ARG_FILE_LINE, "[REPL_MVCC] complete_sub_mvcc FOUND tranid=%d mvccid=%llu\n",
			       tranid, (unsigned long long)found_it->second.m_sub_ids.back ());
		dump ();
	      }

	    // when completing the "main" mvccid, it is expected that all sub-ids have already been completed
	    found_it->second.m_sub_ids.pop_back ();
	  }
	else
	  {
	    if (prm_get_bool_value (PRM_ID_ER_LOG_MVCC_REPL_DEBUG))
	      {
		_er_log_debug (ARG_FILE_LINE, "[REPL_MVCC] complete_sub_mvcc NOT_FOUND sub_id tranid=%d\n",
			       tranid);
		dump ();
	      }
	  }
      }
    else
      {
	if (prm_get_bool_value (PRM_ID_ER_LOG_MVCC_REPL_DEBUG))
	  {
	    _er_log_debug (ARG_FILE_LINE, "[REPL_MVCC] complete_sub_mvcc NOT_FOUND tranid=%d\n",
			   tranid);
	    dump ();
	  }
      }
  }

  void
  replicator_mvcc::dump () const
  {
#if !defined (NDEBUG)
    int index = 1;
    for (const auto &info_pair: m_mapped_mvccids)
      {
	_er_log_debug (ARG_FILE_LINE, "[REPL_MVCC] dump index=%d/%d tranid=%d mvccid=%llu\n",
		       index, m_mapped_mvccids.size (), info_pair.first, (unsigned long long)info_pair.second.m_id);
	if (info_pair.second.m_sub_ids.empty ())
	  {
	    _er_log_debug (ARG_FILE_LINE, "[REPL_MVCC] dump sub_ids: EMPTY\n");
	  }
	else
	  {
	    for (const auto &sub_id: info_pair.second.m_sub_ids)
	      {
		_er_log_debug (ARG_FILE_LINE, "[REPL_MVCC] dump sub_ids: sub_id=%llu\n",
			       (unsigned long long)sub_id);
	      }
	  }
	++index;
      }
#endif /* !NDEBUG */
  }
}
