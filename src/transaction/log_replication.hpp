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


#ifndef _LOG_REPLICATION_HPP_
#define _LOG_REPLICATION_HPP_

#include "log_compress.h"
#include "log_lsa.hpp"
#include "log_reader.hpp"
#include "log_record.hpp"

namespace cubthread
{
  class daemon;
  class entry;
}

namespace cublog
{
  //  replicator applies redo to replicate changes from the active transaction server
  //
  class replicator
  {
    public:
      replicator () = delete;
      replicator (const log_lsa &start_redo_lsa);
      ~replicator ();

      void wait_replication_finish () const;

    private:
      void redo_upto_nxiolsa (cubthread::entry &thread_entry);
      void redo_upto (cubthread::entry &thread_entry, const log_lsa &end_redo_lsa);
      template <typename T>
      void read_and_redo_record (cubthread::entry &thread_entry, LOG_RECTYPE rectype, const log_lsa &rec_lsa);

      cubthread::daemon *m_daemon = nullptr;

      log_lsa m_redo_lsa = NULL_LSA;
      mutable std::mutex m_redo_mutex;
      std::condition_variable m_redo_condvar;

      log_reader m_reader;
      LOG_ZIP m_undo_unzip;
      LOG_ZIP m_redo_unzip;
  };
}

#endif // !_LOG_REPLICATION_HPP_
