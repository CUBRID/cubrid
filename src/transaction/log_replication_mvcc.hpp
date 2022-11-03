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

#ifndef _LOG_REPLICATION_MVCC_HPP_
#define _LOG_REPLICATION_MVCC_HPP_

#endif // _LOG_REPLICATION_MVCC_HPP_

#include "log_lsa.hpp"
#include "storage_common.h"

#include <map>
#include <vector>

namespace cublog
{
  /* Implements mvccid/sub-mvccid registration & completion during passive transaction
   * server replication. Mimics the structure of:
   *  - [at most] one main mvccid per transaction
   *  - [at most] one sub-mvccid per transaction
   * that is also present in the structure LOG_TDES and associated logic.
   *
   * NOTE: even if implementation supports more than one sub-mvccid per transaction, currently
   * this is not implemented/supported in practice; hence, there are asserts to make sure that at most
   * one sub-mvccid is present
   * */
  class replicator_mvcc
  {
    public:
      static constexpr bool COMMITTED = true;
      static constexpr bool ABORTED = false;

    public:
      replicator_mvcc ();

      replicator_mvcc (const replicator_mvcc &) = delete;
      replicator_mvcc (replicator_mvcc &&) = delete;

      ~replicator_mvcc ();

      replicator_mvcc &operator = (const replicator_mvcc &) = delete;
      replicator_mvcc &operator = (replicator_mvcc &&) = delete;

      void new_assigned_mvccid (TRANID tranid, MVCCID mvccid);
      void new_assigned_sub_mvccid_or_mvccid (TRANID tranid, MVCCID mvccid, MVCCID parent_mvccid);

      void complete_mvcc (TRANID tranid, bool committed);
      void complete_sub_mvcc (TRANID tranid);

    private:
      void dump () const;

    private:
      struct tran_mvccid_info
      {
	using mvccid_vec_type = std::vector<MVCCID>;

	MVCCID m_id;
	mvccid_vec_type m_sub_ids;

	explicit tran_mvccid_info (MVCCID mvccid)
	  : m_id { mvccid }
	{
	}

	tran_mvccid_info (tran_mvccid_info const &) = delete;
	tran_mvccid_info (tran_mvccid_info &&that)
	  : m_id { that.m_id }
	{
	  // move only allowed right after initialization
	  assert (that.m_sub_ids.empty ());
	}

	tran_mvccid_info &operator = (tran_mvccid_info const &) = delete;
	tran_mvccid_info &operator = (tran_mvccid_info &&) = delete;
      };

      using map_type = std::map<TRANID, tran_mvccid_info>;

      map_type m_mapped_mvccids;
  };
}
