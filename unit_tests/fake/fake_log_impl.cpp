/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

#include "log_impl.h"

namespace cubtx
{
  namespace group_complete
  {
    registry::registry () = default;
    registry::~registry () = default;

    log_tran_complete_manager_type registry::get_manager_type () const
    {
      return LOG_TRAN_COMPLETE_NO_MANAGER;
    }
    complete_interface::id_type registry::register_transaction (int tran_index, MVCCID mvccid, TRAN_STATE state)
    {
      return 0;
    }
    void registry::complete_mvcc (id_type group_id)
    {
    }
    void registry::complete_logging (id_type group_id)
    {
    }
    void registry::complete (id_type group_id)
    {
    }
    void registry::complete_latest_id ()
    {
    }
    void registry::on_register_transaction ()
    {
    }
    bool registry::can_close_current_group ()
    {
      return false;
    }
  } // namespace group_complete
} // namespace cubtx

log_global::log_global () = default;
log_global::~log_global () = default;
LOG_GLOBAL log_Gl;

log_append_info::log_append_info () = default;
log_prior_lsa_info::log_prior_lsa_info () = default;

mvcctable::mvcctable () = default;
mvcctable::~mvcctable () = default;

mvcc_trans_status::mvcc_trans_status () = default;
mvcc_trans_status::~mvcc_trans_status () = default;

mvcc_active_tran::mvcc_active_tran () = default;

mvcc_snapshot::mvcc_snapshot () = default;

bool
log_does_allow_replication (void)
{
  return false;
}
