#ifndef _LOG_REPLICATION_MVCC_HPP_
#define _LOG_REPLICATION_MVCC_HPP_

#endif // _LOG_REPLICATION_MVCC_HPP_

#include "log_lsa.hpp"
#include "storage_common.h"

#include <map>

namespace cublog
{
  /*
   * */
  class replicator_mvcc
  {
    public:
      replicator_mvcc () = default;

      replicator_mvcc (const replicator_mvcc &) = delete;
      replicator_mvcc (replicator_mvcc &&) = delete;

      ~replicator_mvcc ();

      replicator_mvcc &operator = (const replicator_mvcc &) = delete;
      replicator_mvcc &operator = (replicator_mvcc &&) = delete;

      void new_assigned_mvccid (TRANID tranid, MVCCID mvccid);
      void complete_mvccid (TRANID tranid, bool committed);

    private:
      using map_type = std::map<TRANID, MVCCID>;

      map_type m_mapped_mvccids;
  };
}
