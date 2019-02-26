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
#include "thread_entry.hpp"
#include "thread_manager.hpp"

#include <forward_list>
#include <mutex>

std::mutex systb_Mutex;
std::forward_list<log_tdes *> systb_Free_tdes_list;
TRANID systb_Next_tranid = LOG_SYSTEM_WORKER_FIRST_TRANID;

std::map<TRANID, log_system_tdes *> systb_Recovery_system_tdes;

log_system_tdes::~log_system_tdes ()
{
  retire_tdes ();
}

void
log_system_tdes::create_tdes (TRANID trid)
{
  m_tdes = new log_tdes;
  logtb_initialize_tdes (m_tdes, LOG_SYSTEM_TRAN_INDEX);
  m_tdes->trid = trid;
}

void
log_system_tdes::destroy_tdes ()
{
  if (m_tdes != NULL)
    {
      logtb_finalize_tdes (NULL, m_tdes);
      m_tdes = NULL;
    }
}

void
log_system_tdes::claim_tdes ()
{
  std::unique_lock<std::mutex> ulock (systb_Mutex);
  assert (m_tdes == NULL);

  if (systb_Free_tdes_list.empty ())
    {
      // generate new log_tdes
      create_tdes (systb_Next_tranid);
      systb_Next_tranid += LOG_SYSTEM_WORKER_INCR_TRANID;
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

void
log_system_tdes::rv_set_system_tdes (TRANID trid)
{
  auto it = systb_Recovery_system_tdes.find (trid);
  if (it == systb_Recovery_system_tdes.end ())
    {
      assert (false);
    }
  else
    {
      cubthread::entry &thread_r = cubthread::get_entry ();
      thread_r.set_system_tdes (*it->second);
    }
}

void
log_system_tdes::rv_unset_system_tdes ()
{
  cubthread::entry &thread_r = cubthread::get_entry ();
  thread_r.reset_system_tdes ();
}

void
log_system_tdes::init_system_transations ()
{
  // nothing to do so far
}

void
log_system_tdes::destroy_system_transactions ()
{
  log_tdes *tdes;
  while (!systb_Free_tdes_list.empty ())
    {
      tdes = systb_Free_tdes_list.front();
      systb_Free_tdes_list.pop_front ();
      logtb_finalize_tdes (NULL, tdes);
    }
}

log_tdes *
log_system_tdes::rv_get_tdes (TRANID trid)
{
  auto it = systb_Recovery_system_tdes.find (trid);
  if (it != systb_Recovery_system_tdes.end ())
    {
      return it->second->get_tdes ();
    }
  else
    {
      return NULL;
    }
}

log_tdes *
log_system_tdes::rv_get_or_alloc_tdes (TRANID trid)
{
  log_tdes *tdes = rv_get_tdes (trid);
  if (tdes == NULL)
    {
      log_system_tdes *sys_tdes = new log_system_tdes ();
      sys_tdes->create_tdes (trid);
      systb_Recovery_system_tdes.insert (std::make_pair (trid, sys_tdes));
      return sys_tdes->get_tdes ();
    }
  else
    {
      return tdes;
    }
}

void
log_system_tdes::rv_map_all_tdes (const rv_map_func &func)
{
  for (auto it : systb_Recovery_system_tdes)
    {
      log_tdes *tdes = it.second->get_tdes ();
      assert (tdes != NULL);
      func (*tdes);
    }
}

void
log_system_tdes::rv_delete_all_tdes_if (const rv_delete_if_func &func)
{
  for (auto it = systb_Recovery_system_tdes.begin (); it != systb_Recovery_system_tdes.end ();)
    {
      if (func (* (it->second->get_tdes ())))
	{
	  it = systb_Recovery_system_tdes.erase (it);
	}
      else
	{
	  ++it;
	}
    }
}

void
log_system_tdes::rv_delete_tdes (TRANID trid)
{
  auto it = systb_Recovery_system_tdes.find (trid);
  if (it != systb_Recovery_system_tdes.end ())
    {
      it->second->destroy_tdes ();
      (void) systb_Recovery_system_tdes.erase (it);
    }
  else
    {
      assert (false);
    }
}
