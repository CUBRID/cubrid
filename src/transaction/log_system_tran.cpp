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

//
// System transactions - can make changes to storage without modifying the database view; it requires logging.
//

#include "log_system_tran.hpp"

#include "log_impl.h"

#include <forward_list>
#include <mutex>

std::mutex systb_Mutex;
std::forward_list<log_tdes *> systb_Free_tdes_list;
TRANID systb_Next_tranid = -1;

log_system_tdes::~log_system_tdes ()
{
  retire_tdes ();
}

void
log_system_tdes::claim_tdes ()
{
  std::unique_lock<std::mutex> ulock (systb_Mutex);
  assert (m_tdes == NULL);

  if (systb_Free_tdes_list.empty ())
    {
      // generate new log_tdes
      m_tdes = new log_tdes ();
      logtb_initialize_tdes (m_tdes, systb_Next_tranid);
      m_tdes->trid = systb_Next_tranid--;
    }
  else
    {
      m_tdes = systb_Free_tdes_list.front ();
      systb_Free_tdes_list.pop_front ();
      logtb_clear_tdes (NULL, m_tdes);
    }
  assert (m_tdes->trid < 0 && m_tdes->trid > systb_Next_tranid);
  m_tdes->state = TRAN_ACTIVE;
}

void
log_system_tdes::retire_tdes ()
{
  std::unique_lock<std::mutex> ulock (systb_Mutex);
  if (m_tdes != NULL)
    {
      systb_Free_tdes_list.push_front (m_tdes);
    }
  m_tdes = NULL;
}

log_tdes *
log_system_tdes::get_tdes ()
{
  return m_tdes;
}

void
log_system_tdes::on_sysop_start ()
{
  assert (m_tdes != NULL);
  if (m_tdes->topops.last < 0)
    {
      assert (m_tdes->topops.last == -1);
      LSA_SET_NULL (&m_tdes->head_lsa);
      LSA_SET_NULL (&m_tdes->tail_lsa);
      LSA_SET_NULL (&m_tdes->undo_nxlsa);
      LSA_SET_NULL (&m_tdes->tail_topresult_lsa);
      LSA_SET_NULL (&m_tdes->rcv.tran_start_postpone_lsa);
      LSA_SET_NULL (&m_tdes->rcv.sysop_start_postpone_lsa);
    }
}

void
log_system_tdes::on_sysop_end ()
{
  assert (m_tdes != NULL);
  if (m_tdes->topops.last < 0)
    {
      assert (m_tdes->topops.last == -1);
      LSA_SET_NULL (&m_tdes->head_lsa);
      LSA_SET_NULL (&m_tdes->tail_lsa);
      LSA_SET_NULL (&m_tdes->undo_nxlsa);
      LSA_SET_NULL (&m_tdes->tail_topresult_lsa);
    }
}
