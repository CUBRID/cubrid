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

//
// System transactions - can make changes to storage without modifying the database view; it requires logging.
//

#include "log_system_tran.hpp"

#include "log_impl.h"
#include "thread_entry.hpp"
#include "thread_manager.hpp"

#include <forward_list>
#include <mutex>
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

std::mutex systb_Mutex;
std::forward_list<log_tdes *> systb_Free_tdes_list;
TRANID systb_Next_tranid = LOG_SYSTEM_WORKER_FIRST_TRANID;

// recovery - simulate the system workers from runtime
std::map<TRANID, log_tdes *> systb_System_tdes;

static log_tdes *
systdes_create_tdes ()
{
  log_tdes *tdes = new log_tdes ();
  logtb_initialize_tdes (tdes, LOG_SYSTEM_TRAN_INDEX);
  return tdes;
}

static void
systdes_remove_tdes_from_map (TRANID trid)
{
  auto it = systb_System_tdes.find (trid);
  if (it != systb_System_tdes.end ())
    {
      (void) systb_System_tdes.erase (it);
    }
  else
    {
      assert (false);
    }
}

static void
systdes_retire_tdes (log_tdes *tdes)
{
  std::unique_lock<std::mutex> ulock (systb_Mutex);
  if (tdes != NULL)
    {
      logtb_clear_tdes (NULL, tdes);
      systb_Free_tdes_list.push_front (tdes);

      systdes_remove_tdes_from_map (tdes->trid);
      tdes = NULL;
    }
}

log_tdes *
systdes_claim_tdes ()
{
  assert (LOG_ISRESTARTED ()); // Recovery should not reuse tdeses
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
    }
  assert (tdes->trid < NULL_TRANID && tdes->trid > systb_Next_tranid);

  tdes->state = TRAN_ACTIVE;
  systb_System_tdes[tdes->trid] = tdes;

  return tdes;
}

log_system_tdes::log_system_tdes ()
  : m_tdes (NULL)
{
  if (LOG_ISRESTARTED ())
    {
      m_tdes = systdes_claim_tdes ();
    }
  else
    {
      m_tdes = systdes_create_tdes ();
    }
}

log_system_tdes::~log_system_tdes ()
{
  if (LOG_ISRESTARTED ())
    {
      systdes_retire_tdes (m_tdes);
    }
  else
    {
      logtb_finalize_tdes (NULL, m_tdes);
      delete m_tdes;
      m_tdes = NULL;
    }
}

log_system_tdes::log_system_tdes (log_tdes *tdes)
  : m_tdes (tdes)
{

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
  auto it = systb_System_tdes.find (trid);
  if (it == systb_System_tdes.end ())
    {
      assert (false);
    }
  else
    {
      cubthread::entry &thread_r = cubthread::get_entry ();
      thread_r.set_system_tdes (new log_system_tdes (it->second));
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
  std::lock_guard<std::mutex> lg (systb_Mutex);

  while (!systb_Free_tdes_list.empty ())
    {
      tdes = systb_Free_tdes_list.front ();
      systb_Free_tdes_list.pop_front ();

      logtb_finalize_tdes (NULL, tdes);
      delete tdes;
    }
  assert (systb_System_tdes.empty ());
}

log_tdes *
log_system_tdes::rv_get_tdes (TRANID trid)
{
  auto it = systb_System_tdes.find (trid);
  if (it != systb_System_tdes.end ())
    {
      return it->second;
    }
  else
    {
      return NULL;
    }
}

log_tdes *
log_system_tdes::rv_get_or_alloc_tdes (TRANID trid, const LOG_LSA &log_lsa)
{
  log_tdes *tdes = rv_get_tdes (trid);
  if (tdes == NULL)
    {
      log_tdes *tdes = systdes_create_tdes ();
      tdes->state = TRAN_UNACTIVE_UNILATERALLY_ABORTED;
      tdes->trid = trid;
      tdes->head_lsa = log_lsa;
      systb_System_tdes[trid] = tdes;
      return tdes;
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
  std::lock_guard<std::mutex> lg (systb_Mutex);
  for (auto &el : systb_System_tdes)
    {
      log_tdes *tdes = el.second;
      assert (tdes != NULL);
      func (*tdes);
    }
}

void
log_system_tdes::rv_delete_all_tdes_if (const rv_delete_if_func &func)
{
  for (auto it = systb_System_tdes.begin (); it != systb_System_tdes.end ();)
    {
      if (func (* (it->second)))
	{
	  it = systb_System_tdes.erase (it);
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
  auto it = systb_System_tdes.find (trid);
  if (it != systb_System_tdes.end ())
    {
      (void) systb_System_tdes.erase (it);
    }
  else
    {
      assert (false);
    }
}

void
log_system_tdes::rv_final ()
{
  assert (systb_System_tdes.empty ());
  log_system_tdes::destroy_system_transactions ();
}
