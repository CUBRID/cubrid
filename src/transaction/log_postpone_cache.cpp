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

/*
 * log_postpone_cache.cpp - log postpone cache module
 */

#include "log_postpone_cache.hpp"

#include "memory_alloc.h"
#include "object_representation.h"
#include "log_manager.h"

void
log_postpone_cache::reset ()
{
  m_cursor = 0;
}

/**
 * Cache redo data for log postpone
 *
 * @param node - log prior node
 * @return void
 */
void
log_postpone_cache::add_redo_data (const log_prior_node &node)
{
  assert (node.data_header != NULL);
  assert (node.rlength == 0 || node.rdata != NULL);
  assert (m_cursor <= MAX_CACHE_ENTRIES);

  if (is_full ())
    {
      // Cannot cache postpones, cache reached max capacity
      return;
    }

  // Cache a new postpone log record entry
  cache_entry *new_entry = &m_cache_entries[m_cursor];
  new_entry->m_lsa.set_null ();

  // Cache log_rec_redo from data_header
  memcpy (new_entry->m_data_header, node.data_header, sizeof (log_rec_redo));

  // Cache recovery data
  assert (((log_rec_redo *) node.data_header)->length == node.rlength);
  if (node.rlength > 0)
    {
      new_entry->m_redo_data.extend_to (node.rlength);
      memcpy (new_entry->m_redo_data.get_ptr (), node.rdata, node.rlength);
    }
}

/**
 * Save LSA of postpone operations
 *
 * @param lsa - log postpone LSA
 * @return void
 *
 * NOTE: This saves LSA after a new entry and its redo data have already been added.
 *       They couldn't both be added in the same step.
 */
void
log_postpone_cache::add_lsa (const log_lsa &lsa)
{
  assert (!lsa.is_null ());

  if (is_full ())
    {
      return;
    }

  assert (m_cursor < MAX_CACHE_ENTRIES);

  cache_entry *new_entry = &m_cache_entries[m_cursor];
  new_entry->m_lsa = lsa;

  /* Now that all needed data is saved, increment cached entries counter. */
  m_cursor++;
}

/**
 * Do postpone from cached postpone entries.
 *
 * @param thread_ref - thread entry
 * @param start_postpone_lsa - start postpone LSA
 * @return - true if postpone was run from cached entries, false otherwise
 */
bool
log_postpone_cache::do_postpone (cubthread::entry &thread_ref, const log_lsa &start_postpone_lsa)
{
  assert (!start_postpone_lsa.is_null ());

  if (is_full ())
    {
      // Cache is not usable
      reset ();
      return false;
    }

  // First cached postpone entry at start_postpone_lsa
  int start_index = -1;
  for (std::size_t i = 0; i < m_cursor; ++i)
    {
      if (m_cache_entries[i].m_lsa == start_postpone_lsa)
	{
	  // Found start lsa
	  start_index = i;
	  break;
	}
    }

  if (start_index < 0)
    {
      // Start LSA was not found. Unexpected situation
      return false;
    }

  // Run all postpones after start_index
  for (std::size_t i = start_index; i < m_cursor; ++i)
    {
      cache_entry *entry = &m_cache_entries[i];

      // Get redo data header
      log_rec_redo *redo = (log_rec_redo *) entry->m_data_header;

      // Get recovery data
      (void) log_execute_run_postpone (&thread_ref, &entry->m_lsa, redo, entry->m_redo_data.get_ptr ());
    }

  // Finished running postpones, update the number of entries which should be run on next commit
  m_cursor = start_index;

  return true;
}

bool
log_postpone_cache::is_full () const
{
  return m_cursor == MAX_CACHE_ENTRIES;
}
