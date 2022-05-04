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
  class atomic_replicator : replicator
  {
    public:
      atomic_replicator (const log_lsa &start_redo_lsa, PAGE_FETCH_MODE page_fetch_mode);

      atomic_replicator (const atomic_replicator &) = delete;
      atomic_replicator (atomic_replicator &&) = delete;

      ~atomic_replicator ();

      atomic_replicator &operator= (const atomic_replicator &) = delete;
      atomic_replicator &operator= (atomic_replicator &&) = delete;

    private:
      void redo_upto (cubthread::entry &thread_entry, const log_lsa &end_redo_lsa) override;
      // TODO: change name
      template <typename T>
      void read_and_redo_record_with_atomic_consideration (cubthread::entry &thread_entry, LOG_RECTYPE rectype,
	  const log_lsa &prev_rec_lsa, const log_lsa &rec_lsa, TRANID trid);

    private:
      atomic_replication_helper m_atomic_helper;
  };
}

#endif // _ATOMIC_REPLICATOR_HPP_
