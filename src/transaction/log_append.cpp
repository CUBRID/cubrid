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

#include "log_append.hpp"

#include "log_impl.h"
#include "log_manager.h"

log_data_addr::log_data_addr (const VFID *vfid_arg, PAGE_PTR pgptr_arg, PGLENGTH offset_arg)
  : vfid (vfid_arg)
  , pgptr (pgptr_arg)
  , offset (offset_arg)
{
}

log_append_info::log_append_info ()
  : vdes (NULL_VOLDES)
  , nxio_lsa (NULL_LSA)
  , prev_lsa (NULL_LSA)
  , log_pgptr (NULL)
{

}

log_append_info::log_append_info (const log_append_info &other)
  : vdes (other.vdes)
  , nxio_lsa {other.nxio_lsa.load ()}
  , prev_lsa (other.prev_lsa)
  , log_pgptr (other.log_pgptr)
{

}

LOG_LSA
log_append_info::get_nxio_lsa () const
{
  return nxio_lsa.load ();
}

void
log_append_info::set_nxio_lsa (const LOG_LSA &next_io_lsa)
{
  nxio_lsa.store (next_io_lsa);
}

log_prior_lsa_info::log_prior_lsa_info ()
  : prior_lsa (NULL_LSA)
  , prev_lsa (NULL_LSA)
  , prior_list_header (NULL)
  , prior_list_tail (NULL)
  , list_size (0)
  , prior_flush_list_header (NULL)
  , prior_lsa_mutex ()
{
}

void
LOG_RESET_APPEND_LSA (const LOG_LSA *lsa)
{
  // todo - concurrency safe-guard
  log_Gl.hdr.append_lsa = *lsa;
  log_Gl.prior_info.prior_lsa = *lsa;
}

void
LOG_RESET_PREV_LSA (const LOG_LSA *lsa)
{
  // todo - concurrency safe-guard
  log_Gl.append.prev_lsa = *lsa;
  log_Gl.prior_info.prev_lsa = *lsa;
}

char *
LOG_APPEND_PTR ()
{
  // todo - concurrency safe-guard
  return log_Gl.append.log_pgptr->area + log_Gl.hdr.append_lsa.offset;
}

bool
log_prior_has_worker_log_records (THREAD_ENTRY *thread_p)
{
  LOG_CS_ENTER (thread_p);
  std::unique_lock<std::mutex> ulock (log_Gl.prior_info.prior_lsa_mutex);
  LOG_LSA nxio_lsa = log_Gl.append.get_nxio_lsa ();
  if (!LSA_EQ (&nxio_lsa, &log_Gl.prior_info.prior_lsa))
    {
      LOG_PRIOR_NODE *node;

      assert (LSA_LT (&nxio_lsa, &log_Gl.prior_info.prior_lsa));
      node = log_Gl.prior_info.prior_list_header;
      while (node != NULL)
	{
	  if (node->log_header.trid != LOG_SYSTEM_TRANID)
	    {
	      ulock.unlock ();
	      LOG_CS_EXIT (thread_p);
	      return true;
	    }
	  node = node->next;
	}
    }
  ulock.unlock ();
  LOG_CS_EXIT (thread_p);

  return false;
}
