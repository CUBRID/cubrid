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

#include "log_replication.hpp"

#include "log_impl.h"
#include "recovery.h"
#include "thread_looper.hpp"
#include "thread_manager.hpp"
#include "transaction_global.hpp"

#include <cassert>
#include <chrono>
#include <functional>

namespace cublog
{

  replicator::replicator (const log_lsa &start_redo_lsa)
    : m_redo_lsa { start_redo_lsa }
  {
    // Create the daemon
    cubthread::looper loop (std::chrono::milliseconds (1));   // loop every 1 millisecond
    auto task_func = std::bind (&replicator::redo_upto_nxiolsa, std::ref (*this), std::placeholders::_1);
    auto task = new cubthread::entry_callable_task (task_func);

    m_daemon = cubthread::get_manager ()->create_daemon (loop, task, "cublog::replicator");
  }

  replicator::~replicator ()
  {
    cubthread::get_manager ()->destroy_daemon (m_daemon);
  }

  void
  replicator::redo_upto_nxiolsa (cubthread::entry &thread_entry)
  {
    thread_entry.tran_index = LOG_SYSTEM_TRAN_INDEX;

    while (true)
      {
	log_lsa nxio_lsa = log_Gl.append.get_nxio_lsa ();
	if (m_redo_lsa < nxio_lsa)
	  {
	    redo_upto (thread_entry, nxio_lsa);
	  }
	else
	  {
	    assert (m_redo_lsa == nxio_lsa);
	    break;
	  }
      }
  }

  void
  replicator::redo_upto (cubthread::entry &thread_entry, const log_lsa &end_redo_lsa)
  {
    assert (m_redo_lsa < end_redo_lsa);

    // redo all records from current position (m_redo_lsa) until end_redo_lsa

    // make sure the log page is refreshed. otherwise it may be outdated and new records may be missed
    m_reader.set_lsa_and_fetch_page (m_redo_lsa, log_reader::fetch_mode::FORCE);

    while (m_redo_lsa < end_redo_lsa)
      {
	// read and redo a record
	m_reader.set_lsa_and_fetch_page (m_redo_lsa);

	log_rec_header header = m_reader.reinterpret_copy_and_add_align<log_rec_header> ();

	switch (header.type)
	  {
	  case LOG_REDO_DATA:
	    read_and_redo_record<log_rec_redo> (thread_entry, header.type, m_redo_lsa);
	    break;
	  case LOG_MVCC_REDO_DATA:
	    read_and_redo_record<log_rec_mvcc_redo> (thread_entry, header.type, m_redo_lsa);
	    break;
	  case LOG_UNDOREDO_DATA:
	  case LOG_DIFF_UNDOREDO_DATA:
	    read_and_redo_record<log_rec_undoredo> (thread_entry, header.type, m_redo_lsa);
	    break;
	  case LOG_MVCC_UNDOREDO_DATA:
	  case LOG_MVCC_DIFF_UNDOREDO_DATA:
	    read_and_redo_record<log_rec_mvcc_undoredo> (thread_entry, header.type, m_redo_lsa);
	    break;
	  case LOG_RUN_POSTPONE:
	    read_and_redo_record<log_rec_run_postpone> (thread_entry, header.type, m_redo_lsa);
	    break;
	  case LOG_COMPENSATE:
	    read_and_redo_record<log_rec_compensate> (thread_entry, header.type, m_redo_lsa);
	    break;
	  case LOG_DBEXTERN_REDO_DATA:
	  {
	    log_rec_dbout_redo dbout_redo = m_reader.reinterpret_copy_and_add_align<log_rec_dbout_redo> ();
	    log_rcv rcv;
	    rcv.length = dbout_redo.length;
	    log_rv_redo_record (&thread_entry, m_reader, RV_fun[dbout_redo.rcvindex].redofun, &rcv, &m_redo_lsa, 0,
				nullptr, m_redo_unzip);
	  }
	  break;
	  default:
	    // do nothing
	    break;
	  }

	m_redo_lsa = header.forw_lsa;
      }
  }

  template <typename T>
  void
  replicator::read_and_redo_record (cubthread::entry &thread_entry, LOG_RECTYPE rectype, const log_lsa &rec_lsa)
  {
    T log_rec = m_reader.reinterpret_copy_and_add_align<T> ();
    log_rv_redo_record_sync_or_dispatch_parallel<T> (&thread_entry, m_reader, log_rec, rec_lsa, nullptr, rectype,
	m_undo_unzip, m_redo_unzip);
  }
} // namespace cublog
