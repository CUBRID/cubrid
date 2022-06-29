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
   */
  class atomic_replicator : public replicator
  {
    public:
      atomic_replicator (const log_lsa &start_redo_lsa);

      atomic_replicator (const atomic_replicator &) = delete;
      atomic_replicator (atomic_replicator &&) = delete;

      ~atomic_replicator () override;

      atomic_replicator &operator= (const atomic_replicator &) = delete;
      atomic_replicator &operator= (atomic_replicator &&) = delete;

      /* return the lowest value lsa that was not applied, the next in line lsa */
      log_lsa get_lowest_unapplied_lsa () const override;
    private:
      void redo_upto (cubthread::entry &thread_entry, const log_lsa &end_redo_lsa) override;
      template <typename T>
      void read_and_redo_record (cubthread::entry &thread_entry, const LOG_RECORD_HEADER &rec_header,
				 const log_lsa &rec_lsa);
      void set_lowest_unapplied_lsa (log_lsa value);

    private:
      atomic_replication_helper m_atomic_helper;
      log_lsa m_lowest_unapplied_lsa;
      bool m_should_update_lowest_lsa = true;
      mutable std::mutex m_lowest_unapplied_lsa_mutex;
  };
}

#endif // _ATOMIC_REPLICATOR_HPP_
