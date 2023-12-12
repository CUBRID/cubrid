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

#include "log_replication.cpp.hpp"
#include "log_replication_atomic.hpp"
#include "log_replication_jobs.hpp"

#include "heap_file.h"
#include "locator_sr.h"
#include "log_record.hpp"
#include "log_recovery_redo_parallel.hpp"
#include "oid.h"
#include "xasl_cache.h"

namespace cublog
{

  atomic_replicator::atomic_replicator (const log_lsa &start_redo_lsa, const log_lsa &prev_redo_lsa,
					thread_type replication_thread_type)
    : replicator (start_redo_lsa, OLD_PAGE_IF_IN_BUFFER_OR_IN_TRANSIT, 0, replication_thread_type)
    , m_processed_lsa { prev_redo_lsa }
    , m_lowest_unapplied_lsa { start_redo_lsa }
  {

  }

  atomic_replicator::~atomic_replicator ()
  {
    /*
     * a passive transaction server is a "transient" server instance; it does not store any data
     * and, thus, does not need to be left in consistent state; thus, no check as to the consistent
     * termination state for atomic replication is needed
     */
  }

  void
  atomic_replicator::redo_upto (cubthread::entry &thread_entry, const log_lsa &end_redo_lsa)
  {
    assert (m_redo_lsa < end_redo_lsa);

    // redo all records from current position (m_redo_lsa) until end_redo_lsa

    m_perfmon_redo_sync.start ();
    // make sure the log page is refreshed. otherwise it may be outdated and new records may be missed
    (void) m_redo_context.m_reader.set_lsa_and_fetch_page (m_redo_lsa, log_reader::fetch_mode::FORCE);

    while (m_redo_lsa < end_redo_lsa)
      {
	// read and redo a record
	(void) m_redo_context.m_reader.set_lsa_and_fetch_page (m_redo_lsa);

	const LOG_RECORD_HEADER header = m_redo_context.m_reader.reinterpret_copy_and_add_align<LOG_RECORD_HEADER> ();

	{
	  std::unique_lock<std::mutex> lock (m_processed_lsa_mutex);
	  m_processed_lsa = m_redo_lsa;
	}

	switch (header.type)
	  {
	  case LOG_REDO_DATA:
	    read_and_redo_record<LOG_REC_REDO> (thread_entry, header, m_redo_lsa);
	    break;
	  case LOG_MVCC_REDO_DATA:
	    read_and_redo_record<LOG_REC_MVCC_REDO> (thread_entry, header, m_redo_lsa);
	    break;
	  case LOG_UNDOREDO_DATA:
	  case LOG_DIFF_UNDOREDO_DATA:
	    read_and_redo_record<LOG_REC_UNDOREDO> (thread_entry, header, m_redo_lsa);
	    break;
	  case LOG_MVCC_UNDOREDO_DATA:
	  case LOG_MVCC_DIFF_UNDOREDO_DATA:
	    read_and_redo_record<LOG_REC_MVCC_UNDOREDO> (thread_entry, header, m_redo_lsa);
	    break;
	  case LOG_RUN_POSTPONE:
	    read_and_redo_record<LOG_REC_RUN_POSTPONE> (thread_entry, header, m_redo_lsa);
	    break;
	  case LOG_COMPENSATE:
	    read_and_redo_record<LOG_REC_COMPENSATE> (thread_entry, header, m_redo_lsa);
	    break;
	  case LOG_DBEXTERN_REDO_DATA:
	  {
	    m_redo_context.m_reader.advance_when_does_not_fit (sizeof (LOG_REC_DBOUT_REDO));
	    const LOG_REC_DBOUT_REDO dbout_redo =
		    m_redo_context.m_reader.reinterpret_copy_and_add_align<LOG_REC_DBOUT_REDO> ();

	    if (dbout_redo.rcvindex == RVDK_NEWVOL || dbout_redo.rcvindex == RVDK_EXPAND_VOLUME)
	      {
		/* Recovery redo for RVDK_NEWVOL and RVDK_EXPAND_VOLUME will not be replicated,
		 * because fileIO operations are required for those redo functions.
		 * However fileIO operations are not required in PTS, so it skip these logs
		 */

		break;
	      }

	    log_rcv rcv;
	    rcv.length = dbout_redo.length;

	    log_rv_redo_record (&thread_entry, m_redo_context.m_reader, RV_fun[dbout_redo.rcvindex].redofun, &rcv,
				&m_redo_lsa, 0, nullptr, m_redo_context.m_redo_zip);
	    break;
	  }
	  case LOG_COMMIT:
	    assert (!m_atomic_helper.is_part_of_atomic_replication (header.trid));

	    if (m_replicate_mvcc)
	      {
		m_replicator_mvccid->complete_mvcc (header.trid, replicator_mvcc::COMMITTED);
	      }

	    if (m_locked_classes.count (header.trid) > 0)
	      {
		discard_caches_for_ddl (thread_entry, header.trid);
	      }

	    if (m_locked_classes.count (header.trid) + m_locked_serials.count (header.trid) > 0)
	      {
		release_all_locks_for_ddl (thread_entry, header.trid);
	      }

	    calculate_replication_delay_or_dispatch_async<LOG_REC_DONETIME> (
		    thread_entry, m_redo_lsa);
	    break;
	  case LOG_ABORT:
	    // TODO: there are 2 identified sources for aborted transactions:
	    //  *1* transactions aborted by the client
	    //  *2* unilaterally aborted transactions by the engine (for whatever reason,
	    //    eg. log recovery)
	    // in both these cases, there can remain suspended atomic sequences
	    // which need to be applied; however at abort it is already too late
	    // to identify this state of a transaction (eg: at least for *2* the
	    // recovery state could be identified by processing the log compensate records;
	    //
	    // for *2*, the issue http://jira.cubrid.org/browse/LETS-572 has been added;
	    //
	    // for now, a naive aproach of asserting and forcibly eliminating the atomic sequence
	    assert (!m_atomic_helper.is_part_of_atomic_replication (header.trid)
		    || m_atomic_helper.all_log_entries_are_control (header.trid));
	    if (m_atomic_helper.is_part_of_atomic_replication (header.trid)
		&& m_atomic_helper.all_log_entries_are_control (header.trid))
	      {
		m_atomic_helper.forcibly_remove_sequence (header.trid);
		set_lowest_unapplied_lsa ();
	      }

	    if (m_replicate_mvcc)
	      {
		m_replicator_mvccid->complete_mvcc (header.trid, replicator_mvcc::ABORTED);
	      }

	    if (m_locked_classes.count (header.trid) + m_locked_serials.count (header.trid) > 0)
	      {
		release_all_locks_for_ddl (thread_entry, header.trid);
	      }

	    calculate_replication_delay_or_dispatch_async<LOG_REC_DONETIME> (
		    thread_entry, m_redo_lsa);
	    break;
	  case LOG_DUMMY_HA_SERVER_STATE:
	    calculate_replication_delay_or_dispatch_async<LOG_REC_HA_SERVER_STATE> (
		    thread_entry, m_redo_lsa);
	    break;
	  case LOG_START_ATOMIC_REPL:
	    // nested atomic replication are not allowed
	    assert (!m_atomic_helper.is_part_of_atomic_replication (header.trid));
	    m_atomic_helper.append_control_log (&thread_entry, header.trid, header.type, m_redo_lsa, m_redo_context);
	    set_lowest_unapplied_lsa ();
	    break;
	  case LOG_END_ATOMIC_REPL:
	    assert (m_atomic_helper.is_part_of_atomic_replication (header.trid));
	    m_atomic_helper.append_control_log (&thread_entry, header.trid, header.type, m_redo_lsa, m_redo_context);
	    set_lowest_unapplied_lsa ();
	    break;
	  case LOG_SYSOP_ATOMIC_START:
	    m_atomic_helper.append_control_log (&thread_entry, header.trid, header.type, m_redo_lsa, m_redo_context);
	    set_lowest_unapplied_lsa ();
	    break;
	  case LOG_MVCC_UNDO_DATA:
	  {
	    m_redo_context.m_reader.advance_when_does_not_fit (sizeof (LOG_REC_MVCC_UNDO));
	    const LOG_REC_MVCC_UNDO log_rec =
		    m_redo_context.m_reader.reinterpret_copy_and_add_align<LOG_REC_MVCC_UNDO> ();

	    read_and_bookkeep_mvcc_vacuum<LOG_REC_MVCC_UNDO> (header.back_lsa, m_redo_lsa, log_rec, true);
	    break;
	  }
	  case LOG_SYSOP_END:
	  {
	    m_redo_context.m_reader.advance_when_does_not_fit (sizeof (LOG_REC_SYSOP_END));
	    const LOG_REC_SYSOP_END log_rec =
		    m_redo_context.m_reader.reinterpret_copy_and_add_align<LOG_REC_SYSOP_END> ();

	    m_atomic_helper.append_control_log_sysop_end (
		    &thread_entry, header.trid, m_redo_lsa, log_rec.type, log_rec.lastparent_lsa);
	    set_lowest_unapplied_lsa ();

	    read_and_bookkeep_mvcc_vacuum<LOG_REC_SYSOP_END> (header.back_lsa, m_redo_lsa, log_rec, false);
	    if (m_replicate_mvcc)
	      {
		replicate_sysop_end_mvcc (header.trid, m_redo_lsa, log_rec);
	      }
	    break;
	  }
	  case LOG_SYSOP_START_POSTPONE:
	    if (m_replicate_mvcc)
	      {
		replicate_sysop_start_postpone (thread_entry, header);
	      }
	    break;
	  case LOG_ASSIGNED_MVCCID:
	  {
	    m_redo_context.m_reader.advance_when_does_not_fit (sizeof (LOG_REC_ASSIGNED_MVCCID));
	    const LOG_REC_ASSIGNED_MVCCID log_rec =
		    m_redo_context.m_reader.reinterpret_copy_and_add_align<LOG_REC_ASSIGNED_MVCCID> ();
	    if (m_bookkeep_mvcc)
	      {
		log_Gl.mvcc_table.set_mvccid_from_active_transaction_server (log_rec.mvccid);
	      }
	    if (m_replicate_mvcc)
	      {
		m_replicator_mvccid->new_assigned_mvccid (header.trid, log_rec.mvccid);
	      }
	    break;
	  }
	  case LOG_LOCKED_OBJECT:
	  {
	    m_redo_context.m_reader.advance_when_does_not_fit (sizeof (LOG_REC_LOCKED_OBJECT));
	    const LOG_REC_LOCKED_OBJECT log_rec =
		    m_redo_context.m_reader.reinterpret_copy_and_add_align<LOG_REC_LOCKED_OBJECT> ();

	    const bool is_class = OID_EQ (&log_rec.classoid, oid_Root_class_oid);
	    if (is_locked_for_ddl (header.trid, &log_rec.oid, is_class))
	      {
		break;
	      }

	    acquire_lock_for_ddl (thread_entry, header.trid, log_rec, is_class);

	    if (is_class)
	      {
		bookkeep_classname_for_ddl (thread_entry, &log_rec.oid);
	      }
	    break;
	  }
	  default:
	    // do nothing
	    break;
	  }

	{
	  std::unique_lock<std::mutex> lock (m_redo_lsa_mutex);

	  // better to be checked as soon as possible during the processing loop
	  // however, this would need one more mutex lock; therefore, suffice to do it here
	  assert (m_replication_active);

	  m_redo_lsa = header.forw_lsa;
	}
	set_lowest_unapplied_lsa ();

	// to accurately track progress and avoid clients to wait for too long, notify each change
	m_redo_lsa_condvar.notify_all ();

	m_perfmon_redo_sync.track_and_start ();
      }
  }

  template <typename T>
  void
  atomic_replicator::read_and_redo_record (cubthread::entry &thread_entry, const LOG_RECORD_HEADER &rec_header,
      const log_lsa &rec_lsa)
  {
    m_redo_context.m_reader.advance_when_does_not_fit (sizeof (T));
    const log_rv_redo_rec_info<T> record_info (rec_lsa, rec_header.type,
	m_redo_context.m_reader.reinterpret_copy_and_add_align<T> ());

    // only mvccids that pertain to redo's are processed here
    const MVCCID mvccid = log_rv_get_log_rec_mvccid (record_info.m_logrec);
    log_replication_update_header_mvcc_vacuum_info (mvccid, rec_header.back_lsa, rec_lsa, m_bookkeep_mvcc);
    if (m_replicate_mvcc && MVCCID_IS_NORMAL (mvccid))
      {
	m_replicator_mvccid->new_assigned_mvccid (rec_header.trid, mvccid);
      }

    // Redo b-tree stats differs from what the recovery usually does. Get the recovery index before deciding how to
    // proceed.
    const LOG_RCVINDEX rcvindex = log_rv_get_log_rec_data (record_info.m_logrec).rcvindex;
    if (rcvindex == RVBT_LOG_GLOBAL_UNIQUE_STATS_COMMIT)
      {
	read_and_redo_btree_stats (thread_entry, record_info);
      }
    else
      {
	const VPID log_vpid = log_rv_get_log_rec_vpid<T> (record_info.m_logrec);
	if (m_atomic_helper.is_part_of_atomic_replication (rec_header.trid))
	  {
	    // return code ignored because it refers to failure to fix heap page
	    // this is expected in the context of passive transaction server
	    m_atomic_helper.append_log (rec_header.trid, rec_lsa, rcvindex, log_vpid);
	  }
	else
	  {
#ifdef ATOMIC_REPL_PAGE_BELONGS_TO_SINGLE_ATOMIC_SEQUENCE_CHECK
	    m_atomic_helper.check_vpid_not_part_of_any_sequence (log_vpid);
#endif
	    log_rv_redo_record_sync<T> (&thread_entry, m_redo_context, record_info, log_vpid);
	  }
      }
  }

  log_lsa
  atomic_replicator::get_highest_processed_lsa () const
  {
    std::lock_guard<std::mutex> lockg (m_processed_lsa_mutex);
    return m_processed_lsa;
  }

  log_lsa
  atomic_replicator::get_lowest_unapplied_lsa () const
  {
    std::lock_guard<std::mutex> lockg (m_lowest_unapplied_lsa_mutex);
    return m_lowest_unapplied_lsa;
  }

  void
  atomic_replicator::set_lowest_unapplied_lsa ()
  {
    // this function must be called when any change happens that affects the calculated value:
    //  - redo lsa value changes
    //  - an atomic sequence is created or removed

    assert (!LSA_ISNULL (&m_redo_lsa));
    const LOG_LSA helper_lowest_unapplied_lsa = m_atomic_helper.get_the_lowest_start_lsa ();
    assert (!LSA_ISNULL (&helper_lowest_unapplied_lsa));
    const LOG_LSA value_to_change = (m_redo_lsa < helper_lowest_unapplied_lsa) ? m_redo_lsa : helper_lowest_unapplied_lsa;
    {
      std::lock_guard<std::mutex> lockg (m_lowest_unapplied_lsa_mutex);
      m_lowest_unapplied_lsa = value_to_change;
    }
  }

  void
  atomic_replicator::replicate_sysop_start_postpone (cubthread::entry &thread_entry,
      const LOG_RECORD_HEADER &rec_header)
  {
    // - if type is LOG_SYSOP_END_COMMIT it starts a sequence of sysop postpones
    // - after each sysop postpone, a LOG_SYSOP_END with LOG_SYSOP_END_LOGICAL_RUN_POSTPONE
    //    occurs with run_postpone_lsa pointing to *the first* RUN_POSTPONE
    // - at the end, there is another LOG_SYSOP_END with LOG_SYSOP_END_COMMIT

    m_redo_context.m_reader.advance_when_does_not_fit (sizeof (LOG_REC_SYSOP_START_POSTPONE));
    const LOG_REC_SYSOP_START_POSTPONE log_rec =
	    m_redo_context.m_reader.reinterpret_copy_and_add_align<LOG_REC_SYSOP_START_POSTPONE> ();
    LOG_SYSOP_END_TYPE_CHECK (log_rec.sysop_end.type);

    if (log_rec.sysop_end.type == LOG_SYSOP_END_COMMIT)
      {
	if (m_atomic_helper.is_part_of_atomic_replication (rec_header.trid))
	  {
	    // only interprete LOG_SYSOP_START_POSTPONE if already part of an atomic replication sequence
	    // apply modifications for all log records which are already part of the sequence
	    m_atomic_helper.append_control_log (&thread_entry, rec_header.trid, LOG_SYSOP_START_POSTPONE,
						m_redo_lsa, m_redo_context);
	  }
	else
	  {
	    // not already part of an atomic replication sequence
	    // if the postpone operation itself will contain a logical (compound) operation guarded
	    // by an atomic sequence; that will be treated in a standalone fashion
	  }
      }
  }

  void
  atomic_replicator::release_all_locks_for_ddl (cubthread::entry &thread_entry, const TRANID trid)
  {
    auto unlock_objects_within_transaction = [] (cubthread::entry &thread_entry, const TRANID trid, auto &objects)
    {
      auto [begin, end] = objects.equal_range (trid);
      for (auto it = begin; it != end; ++it)
	{
	  const auto &log_rec = it->second;
	  lock_unlock_object_and_cleanup (nullptr, &log_rec.oid, &log_rec.classoid, log_rec.lock_mode);
	}

      objects.erase (trid);
    };

    unlock_objects_within_transaction (thread_entry, trid, m_locked_classes);
    unlock_objects_within_transaction (thread_entry, trid, m_locked_serials);
  }

  void
  atomic_replicator::acquire_lock_for_ddl (cubthread::entry &thread_entry, const TRANID trid,
      const LOG_REC_LOCKED_OBJECT &log_rec, const bool is_class)
  {
    assert (!OID_ISNULL (&log_rec.classoid) && !OID_ISTEMP (&log_rec.classoid));
    assert (!OID_ISNULL (&log_rec.oid) && !OID_ISTEMP (&log_rec.oid));

    /* TODO:
     * If a PTS read transaction holds a lock for an extended period without releasing it for the same class,
     * the replicator could wait too long to acquire the lock.
     * In such cases, there might be an introduction of a mechanism to abort read transaction that holds lock.
     */

    if (lock_object (&thread_entry, &log_rec.oid, &log_rec.classoid, log_rec.lock_mode, LK_UNCOND_LOCK) != LK_GRANTED)
      {
	assert_release (false);
      }

    if (is_class)
      {
	m_locked_classes.emplace (trid, log_rec);
      }
    else
      {
	assert (OID_EQ (&log_rec.classoid, oid_Serial_class_oid));
	m_locked_serials.emplace (trid, log_rec);
      }
  }

  void
  atomic_replicator::bookkeep_classname_for_ddl (cubthread::entry &thread_entry, const OID *classoid)
  {
    char *classname = nullptr;
    int error_code = NO_ERROR;

    error_code = heap_get_class_name (&thread_entry, classoid, &classname);
    if (error_code == NO_ERROR)
      {
	assert (classname != nullptr);
	m_classname_map.emplace (*classoid, std::string (classname));

	free_and_init (classname);
      }
    else if (error_code == ER_HEAP_UNKNOWN_OBJECT)
      {
	assert (classname == nullptr);
	/* If there is no class record in the heap, it means it is creating the class */
      }
    else
      {
	assert (false);
      }
  }

  void
  atomic_replicator::update_classname_cache_for_ddl (cubthread::entry &thread_entry, const OID *classoid)
  {
    /* update locator_Mht_classnames only if needed */
    char *classname = nullptr;
    int error_code = NO_ERROR;

    auto it = m_classname_map.find (*classoid);

    error_code = heap_get_class_name (&thread_entry, classoid, &classname);
    if (error_code == NO_ERROR)
      {
	assert (classname != nullptr);
	if (it == m_classname_map.end ())
	  {
	    error_code = locator_put_classname_entry (&thread_entry, classname, classoid);
	    if (error_code != NO_ERROR)
	      {
		_er_log_debug (ARG_FILE_LINE, "[REPL_DDL] Failed to put entry for (%s) to locator_Mht_classnames for classoid\n",
			       classname);
	      }
	  }
	else
	  {
	    if (it->second != classname)
	      {
		error_code = locator_update_classname_entry (&thread_entry, it->second.c_str(), classname);
		if (error_code != NO_ERROR)
		  {
		    _er_log_debug (ARG_FILE_LINE, "[REPL_DDL] Failed to update entry for (%s) to locator_Mht_classnames for classoid\n",
				   classname);
		  }
	      }
	  }

	free_and_init (classname);
      }
    else if (error_code == ER_HEAP_UNKNOWN_OBJECT)
      {
	assert (classname == nullptr);
	/* If there is no class record in the heap, it means that the class (table/view) has been dropped during the transaction */

	if (it != m_classname_map.end ())
	  {
	    error_code = locator_remove_classname_entry (&thread_entry, it->second.c_str ());
	    if (error_code != NO_ERROR)
	      {
		_er_log_debug (ARG_FILE_LINE, "[REPL_DDL] Failed to remove entry for (%s) from locator_Mht_classnames for classoid\n",
			       it->second.c_str ());
	      }
	  }
	else
	  {
	    /* This is when the table is CREATEd and DROPped within the same transaction */
	  }
      }
    else
      {
	assert (false);
      }

    if (it != m_classname_map.end ())
      {
	m_classname_map.erase (it);
      }
  }

  void
  atomic_replicator::discard_caches_for_ddl (cubthread::entry &thread_entry, const TRANID trid)
  {
    auto [begin, end] = m_locked_classes.equal_range (trid);
    for (auto it = begin; it != end; ++it)
      {
	auto log_rec = it->second;

	update_classname_cache_for_ddl (thread_entry, &log_rec.oid);
	(void) heap_classrepr_decache (&thread_entry, &log_rec.oid);
	(void) heap_delete_hfid_from_cache (&thread_entry, &log_rec.oid);
	xcache_remove_by_oid (&thread_entry, &log_rec.oid);
	partition_decache_class (&thread_entry, &log_rec.oid);
      }
  }

  bool
  atomic_replicator::is_locked_for_ddl (const TRANID trid, const OID *oid, const bool is_class)
  {
    const auto locked_objects = is_class ? m_locked_classes : m_locked_serials;

    auto [begin, end] = locked_objects.equal_range (trid);
    for (auto it = begin; it != end; ++it)
      {
	auto log_rec = it->second;
	if (OID_EQ (&log_rec.oid, oid))
	  {
	    return true;
	  }
      }

    return false;
  }
}
