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

#include "log_recovery_context.hpp"

#include "disk_manager.h"
#include "file_io.h"
#include "log_impl.h"
#include "server_type.hpp"
#include "thread_manager.hpp"

bool
get_disk_checkpoint_min_lsa (THREAD_ENTRY *thread_p, VOLID volid, void *lsa)
{
  // the function signature is adapted to fileio_map_mounted. that last argument is actually a pointer to log_lsa.
  assert (lsa != nullptr);
  log_lsa &global_checkpoint_lsa = * (log_lsa *) lsa;
  log_lsa local_checkpoint_lsa = NULL_LSA;

  const int ret = disk_get_checkpoint (thread_p, volid, &local_checkpoint_lsa);
  if (ret == NO_ERROR)
    {
      if (global_checkpoint_lsa.is_null () || local_checkpoint_lsa < global_checkpoint_lsa)
	{
	  global_checkpoint_lsa = local_checkpoint_lsa;
	}
    }

  return true;
}

log_recovery_context::log_recovery_context ()
{
  m_is_page_server = get_server_type () == SERVER_TYPE_PAGE;
  m_checkpoint_lsa = log_Gl.hdr.chkpt_lsa;
  m_start_redo_lsa = m_checkpoint_lsa;
  m_end_redo_lsa = m_checkpoint_lsa;
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
  m_checkpoint_lsa = chkpt_lsa;
  m_is_restore_from_backup = true;
  if (stopat_p)
    {
      m_restore_stop_point = *stopat_p;
    }

  /* we may have to start from an older checkpoint... */
  (void) fileio_map_mounted (&cubthread::get_entry (), get_disk_checkpoint_min_lsa, &m_checkpoint_lsa);

  // Also init start/end redo LSA's as checkpoint LSA
  m_start_redo_lsa = m_checkpoint_lsa;
  m_end_redo_lsa = m_checkpoint_lsa;
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
log_recovery_context::force_stop_restore_at (time_t stopat)
{
  m_is_restore_incomplete = true;
  m_restore_stop_point = stopat;
}

bool
log_recovery_context::does_restore_stop_before_time (time_t complete_time)
{
  if (m_restore_stop_point == 0)
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

const log_lsa &
log_recovery_context::get_checkpoint_lsa () const
{
  return m_checkpoint_lsa;
}

const log_lsa &
log_recovery_context::get_start_redo_lsa () const
{
  return m_start_redo_lsa;
}

const log_lsa &
log_recovery_context::get_end_redo_lsa () const
{
  return m_end_redo_lsa;
}
