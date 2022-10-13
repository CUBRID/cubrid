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

#ifndef _ATOMIC_REPLICATOR_HPP_
#define _ATOMIC_REPLICATOR_HPP_

#include "atomic_replication_helper.hpp"
#include "log_replication.hpp"

namespace cublog
{
  /*
   * Replicator derived class responsible for handling the log redo only on Passive Transaction Servers
   * In addition to the standard replicator, the atomic variant also adds support for atomic log replication
   * with the help of the atomic_replication_helper class.
   *
   * Implementation details:
   * - internally, a thread (daemon) is started which executes replicator::redo_upto_nxio_lsa
   *    continuously in a loop
   * - the thread executes everything synchronously down the path:
   *    -> replicator::redo_upto_nxio_lsa
   *      -> atomic_replicator::redo_upto (overridden function)
   *        -> atomic_replicator::read_and_redo_record | calculate_replication_delay_or_dispatch_async
   *            | m_atomic_helper | read_and_bookkeep_mvcc_vacuum | m_replicator_mvccid
   *
   * - there are two helper classes
   *    - m_atomic_helper - to replication atomic sequences
   *    - m_replicator_mvccid - to update the PTS's MVCC table according to processed log records
   *      - (it is assumed that the MVCC table is correct when the replication is started)
   *
   * - atomic replication is implemented according to the description for the class atomic_replication_helper
   *    by sequentially processing atomic replications markers in the transactional log
   *
   * - the instance also keeps two boundaries LSA's
   *    - lowest_unapplied_lsa - the smallest LSA which is known to not have been applied
   *    - highest_processed_lsa - the highest LSA which is has been processed by the replication
   *    - diagram:
   *
   *                         - active atomic replication
   *  all log records have     sequences                              no log records have
   *  been applied           - some log records have already          been processed
   *                        │  been applied (non-atomically)       │
   *  ──────────────────────┼──────────────────────────────────────┼──────────────────────────►
   *                        │                                      │                 TRANSACTIONAL
   *                      lowest                               highest               LOG axis
   *                      unapplied                            processed
   *                      LSA                                  LSA
   *
   *    - the consequence of this is that, when there are no atomic replication sequences
   *      the two values are equal
   */
  class atomic_replicator : public replicator
  {
    public:
      atomic_replicator (const log_lsa &start_redo_lsa, const log_lsa &prev_redo_lsa);

      atomic_replicator (const atomic_replicator &) = delete;
      atomic_replicator (atomic_replicator &&) = delete;

      ~atomic_replicator () override;

      atomic_replicator &operator= (const atomic_replicator &) = delete;
      atomic_replicator &operator= (atomic_replicator &&) = delete;

      /* return current progress of the replicator; non-blocking call */
      log_lsa get_highest_processed_lsa () const override;
      /* return the lowest value lsa that was not applied, the next in line lsa */
      log_lsa get_lowest_unapplied_lsa () const override;
    private:
      void redo_upto (cubthread::entry &thread_entry, const log_lsa &end_redo_lsa) override;
      template <typename T>
      void read_and_redo_record (cubthread::entry &thread_entry, const LOG_RECORD_HEADER &rec_header,
				 const log_lsa &rec_lsa);
      void set_lowest_unapplied_lsa ();
      void replicate_sysop_start_postpone (cubthread::entry &thread_entry, const LOG_RECORD_HEADER &rec_header);
      void replicate_sysop_end (cubthread::entry &thread_entry, const LOG_RECORD_HEADER &rec_header,
				const LOG_REC_SYSOP_END &log_rec);

    private:
      atomic_replication_helper m_atomic_helper;
      log_lsa m_lowest_unapplied_lsa;
      log_lsa m_processed_lsa = NULL_LSA; /* protected by m_redo_lsa_mutex with m_redo_lsa */
      mutable std::mutex m_lowest_unapplied_lsa_mutex;
  };
}

#endif // _ATOMIC_REPLICATOR_HPP_
