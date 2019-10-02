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

// recovery - simulate the system workers from runtime
std::map<TRANID, log_system_tdes> systb_system_tdes;

static log_tdes *
systdes_create_tdes ()
{
  log_tdes *tdes = new log_tdes ();
  logtb_initialize_tdes (tdes, LOG_SYSTEM_TRAN_INDEX);
  return tdes;
}

void
systdes_destroy_tdes (log_tdes *&tdes)
{
  if (tdes != NULL)
    {
      logtb_finalize_tdes (NULL, tdes);
      delete tdes;
      tdes = NULL;
    }
}

log_tdes *
log_system_tdes::systdes_claim_tdes ()
{
  std::unique_lock<std::mutex> ulock (systb_Mutex);
  log_tdes *tdes = NULL;

  if (systb_Free_tdes_list.empty ())
    {
      // generate new log_tdes
      tdes = systdes_create_tdes ();
      tdes->trid = systb_Next_tranid;
      systb_Next_tranid += LOG_SYSTEM_WORKER_INCR_TRANID;
    }
  else
    {
      tdes = systb_Free_tdes_list.front ();
      systb_Free_tdes_list.pop_front ();
      logtb_clear_tdes (NULL, tdes);
    }
  assert (tdes->trid < 0 && tdes->trid > systb_Next_tranid);
  tdes->state = TRAN_ACTIVE;

  systb_system_tdes.insert (std::make_pair (tdes->trid, log_system_tdes (tdes)));
  return tdes;
}

void
log_system_tdes::systdes_retire_tdes (log_tdes *&tdes)
{
  std::unique_lock<std::mutex> ulock (systb_Mutex);
  if (tdes != NULL)
    {
      systb_Free_tdes_list.push_front (tdes);
      rv_delete_tdes (tdes->trid);
      tdes = NULL;
    }
}

log_system_tdes::log_system_tdes ()
  : m_tdes (NULL)
{
  m_tdes = systdes_claim_tdes ();
}

log_system_tdes::log_system_tdes (log_tdes *tdes)
  : m_tdes (tdes)
{

}

log_system_tdes::~log_system_tdes ()
{
  systdes_retire_tdes (m_tdes);
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
log_system_tdes::rv_simulate_system_tdes (TRANID trid)
{
  auto it = systb_system_tdes.find (trid);
  if (it == systb_system_tdes.end ())
    {
      assert (false);
    }
  else
    {
      cubthread::entry &thread_r = cubthread::get_entry ();
      thread_r.set_system_tdes (it->second);
    }
}

void
log_system_tdes::rv_end_simulation ()
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
      tdes = systb_Free_tdes_list.front ();
      systb_Free_tdes_list.pop_front ();
      systdes_destroy_tdes (tdes);
    }

  systb_system_tdes.clear ();
}

log_tdes *
log_system_tdes::rv_get_tdes (TRANID trid)
{
  auto it = systb_system_tdes.find (trid);
  if (it != systb_system_tdes.end ())
    {
      return it->second.get_tdes ();
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
      log_system_tdes sys_tdes = log_system_tdes ();
      sys_tdes.m_tdes->trid = trid;
      systb_system_tdes.insert (std::make_pair (trid, sys_tdes));
      return sys_tdes.get_tdes ();
    }
  else
    {
      assert (tdes->trid == trid);
      return tdes;
    }
}

void
log_system_tdes::map_all_tdes (const map_func &func)
{
  for (auto el : systb_system_tdes)
    {
      log_tdes *tdes = el.second.get_tdes ();
      assert (tdes != NULL);
      func (*tdes);
    }
}

// do this to chain with other operations under lock
std::unique_lock<std::mutex>
log_system_tdes::get_size_and_lock (size_t &system_trans_size)
{
  std::unique_lock<std::mutex> ul (systb_Mutex);
  system_trans_size = systb_system_tdes.size ();
  return ul;
}

void
log_system_tdes::rv_delete_all_tdes_if (const rv_delete_if_func &func)
{
  for (auto it = systb_system_tdes.begin (); it != systb_system_tdes.end ();)
    {
      if (func (* (it->second.get_tdes ())))
	{
	  it = systb_system_tdes.erase (it);
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
  auto it = systb_system_tdes.find (trid);
  if (it != systb_system_tdes.end ())
    {
      (void) systb_system_tdes.erase (it);
    }
  else
    {
      assert (false);
    }
}

void
log_system_tdes::rv_final ()
{
  assert (systb_system_tdes.empty ());
}
