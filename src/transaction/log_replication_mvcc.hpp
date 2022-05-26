#ifndef _LOG_REPLICATION_MVCC_HPP_
#define _LOG_REPLICATION_MVCC_HPP_

#endif // _LOG_REPLICATION_MVCC_HPP_

#include "log_lsa.hpp"
#include "storage_common.h"

#include <map>
#include <vector>

namespace cublog
{
  /*
   * */
  class replicator_mvcc
  {
    public:
      static constexpr bool COMMITTED = true;
      static constexpr bool ABORTED = false;

    public:
      replicator_mvcc () = default;

      replicator_mvcc (const replicator_mvcc &) = delete;
      replicator_mvcc (replicator_mvcc &&) = delete;

      ~replicator_mvcc ();

      replicator_mvcc &operator = (const replicator_mvcc &) = delete;
      replicator_mvcc &operator = (replicator_mvcc &&) = delete;

      void new_assigned_mvccid (TRANID tranid, MVCCID mvccid);
      void new_assigned_sub_mvccid (TRANID tranid, MVCCID sub_mvccid, MVCCID mvccid);

      void complete_sub_mvcc (TRANID tranid, bool committed);
      void complete_mvcc (TRANID tranid, bool committed);

    private:
      void dump_map () const;

    private:
      struct tran_mvccid_info
      {
	using mvccid_vec_type = std::vector<MVCCID>;

	MVCCID id;
	mvccid_vec_type sub_ids;

	explicit tran_mvccid_info (MVCCID mvccid)
	  : id { mvccid }
	{
	}

	tran_mvccid_info (tran_mvccid_info const &) = delete;
	tran_mvccid_info (tran_mvccid_info &&that)
	  : id { that.id }
	{
	  // move only allowed right after initialization
	  assert (that.sub_ids.empty ());
	}

	tran_mvccid_info &operator = (tran_mvccid_info const &) = delete;
	tran_mvccid_info &operator = (tran_mvccid_info &&) = delete;
      };

      using map_type = std::map<TRANID, tran_mvccid_info>;

      map_type m_mapped_mvccids;
  };
}
