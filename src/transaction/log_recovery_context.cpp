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

//#include "log_impl.h"
#include "log_recovery_context.hpp"

#include "server_type.hpp"
#include "thread_manager.hpp"

//void
//log_recovery_context_mvcc::add (MVCCID id)
//{
//  m_present_mvccids.insert (id);
//}

//bool
//log_recovery_context_mvcc::empty () const
//{
//  return m_present_mvccids.empty();
//}

//void
//log_recovery_context_mvcc::complete_missing_mvccids ()
//{
//  assert (!m_present_mvccids.empty());

//  if (m_present_mvccids.size() > 1)
//    {
//      auto it = m_present_mvccids.cbegin();
//      MVCCID prev_mvccid = *it;
//      ++it;
//      const MVCCID smallest_mvccid = *m_present_mvccids.cbegin();
//      const MVCCID largest_mvccid = *m_present_mvccids.crbegin();
//      assert (smallest_mvccid <= largest_mvccid);

//      log_Gl.hdr.mvcc_next_id = smallest_mvccid;
//      log_Gl.mvcc_table.reset_start_mvccid ();

//      for (; it != m_present_mvccids.cend (); ++it)
//        {
//          const MVCCID curr_mvccid = *it;
//          for (MVCCID missing_mvccid = prev_mvccid + 1; missing_mvccid < curr_mvccid; ++missing_mvccid)
//            {
//              log_Gl.mvcc_table.complete_mvcc (LOG_SYSTEM_TRAN_INDEX, missing_mvccid, true);
//            }
//          prev_mvccid = curr_mvccid;
//        }

//      log_Gl.hdr.mvcc_next_id = largest_mvccid + 1;
//      log_Gl.mvcc_table.reset_start_mvccid ();
//    }
//}

log_recovery_context::log_recovery_context ()
{
  m_is_page_server = get_server_type () == SERVER_TYPE_PAGE;
}

void
log_recovery_context::init_for_recovery (const log_lsa &chkpt_lsa)
{
  // All LSA's are initialized as checkpoint LSA
  m_checkpoint_lsa = chkpt_lsa;
  m_start_redo_lsa = chkpt_lsa;
  m_end_redo_lsa = chkpt_lsa;
}

void
log_recovery_context::init_for_restore (const log_lsa &chkpt_lsa, const time_t *stopat_p)
{
  init_for_recovery (chkpt_lsa);

  m_is_restore_from_backup = true;
  if (stopat_p != nullptr)
    {
      m_restore_stop_point = *stopat_p;
    }
}

bool
log_recovery_context::is_restore_from_backup () const
{
  return m_is_restore_from_backup;
}

bool
log_recovery_context::is_restore_incomplete () const
{
  return m_is_restore_incomplete;
}

void
log_recovery_context::set_forced_restore_stop ()
{
  m_is_restore_incomplete = true;
  m_restore_stop_point = RESTORE_STOP_POINT_NONE; // reset time of stop
}

bool
log_recovery_context::does_restore_stop_before_time (time_t complete_time)
{
  if (m_restore_stop_point <= 0)
    {
      // no stop point
      return false;
    }
  if (difftime (m_restore_stop_point, complete_time) < 0)
    {
      return true;
    }
  return false;
}

void
log_recovery_context::set_incomplete_restore ()
{
  m_is_restore_incomplete = true;
}

void
log_recovery_context::set_end_redo_lsa (const log_lsa &end_redo_lsa)
{
  m_end_redo_lsa = end_redo_lsa;
}

void
log_recovery_context::set_start_redo_lsa (const log_lsa &start_redo_lsa)
{
  m_start_redo_lsa = start_redo_lsa;
}

bool
log_recovery_context::is_page_server () const
{
  return m_is_page_server;
}
