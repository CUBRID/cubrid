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

#include "log_record.hpp"
#include "memory_alloc.h"
#include "object_representation.h"
#include "log_manager.h"

void
log_postpone_cache::clear ()
{
  m_redo_data_ptr = NULL;
  m_cache_entries_cursor = 0;
  m_cache_status = LOG_POSTPONE_CACHE_NO;
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

  if (m_cache_status == LOG_POSTPONE_CACHE_OVERFLOW)
    {
      // Cannot cache postpones
      return;
    }

  if (m_cache_status == LOG_POSTPONE_CACHE_NO)
    {
      // Initialize data to cache postpones
      m_cache_entries_cursor = 0;
      m_redo_data_ptr = m_redo_data_buf;
      m_cache_status = LOG_POSTPONE_CACHE_YES;
    }

  assert (m_cache_entries_cursor <= MAX_CACHE_ENTRIES);
  assert (m_redo_data_ptr != NULL);
  assert (m_redo_data_ptr >= m_redo_data_buf);
  assert ((std::size_t) (m_redo_data_ptr - m_redo_data_buf) <= REDO_DATA_SIZE);
  ASSERT_ALIGN (m_redo_data_ptr, MAX_ALIGNMENT);

  if (m_cache_entries_cursor == MAX_CACHE_ENTRIES)
    {
      // Could not store all postpone records
      m_cache_status = LOG_POSTPONE_CACHE_OVERFLOW;
      return;
    }

  // Check if recovery data fits in preallocated buffer
  std::size_t total_data_size = m_redo_data_ptr - m_redo_data_buf;
  total_data_size += sizeof (log_rec_redo);
  total_data_size += node.rlength;
  total_data_size += 2 * MAX_ALIGNMENT;
  if (total_data_size > REDO_DATA_SIZE)
    {
      // Cannot store all recovery data
      m_cache_status = LOG_POSTPONE_CACHE_OVERFLOW;
      return;
    }

  // Cache a new postpone log record entry
  cache_entry *new_entry = &m_cache_entries[m_cache_entries_cursor];
  new_entry->m_redo_data = m_redo_data_ptr;
  new_entry->m_lsa.set_null ();

  // Cache log_rec_redo from data_header
  memcpy (m_redo_data_ptr, node.data_header, sizeof (log_rec_redo));
  m_redo_data_ptr += sizeof (log_rec_redo);
  m_redo_data_ptr = PTR_ALIGN (m_redo_data_ptr, MAX_ALIGNMENT);

  // Cache recovery data
  assert (((log_rec_redo *) node.data_header)->length == node.rlength);
  if (node.rlength > 0)
    {
      memcpy (m_redo_data_ptr, node.rdata, node.rlength);
      m_redo_data_ptr += node.rlength;
      m_redo_data_ptr = PTR_ALIGN (m_redo_data_ptr, MAX_ALIGNMENT);
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
  assert (m_cache_status != LOG_POSTPONE_CACHE_NO);

  if (m_cache_status == LOG_POSTPONE_CACHE_OVERFLOW)
    {
      return;
    }

  assert (m_cache_entries_cursor < MAX_CACHE_ENTRIES);

  cache_entry *new_entry = &m_cache_entries[m_cache_entries_cursor];
  new_entry->m_lsa = lsa;

  /* Now that all needed data is saved, increment cached entries counter. */
  m_cache_entries_cursor++;
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
  assert (m_cache_status != LOG_POSTPONE_CACHE_NO);

  if (m_cache_status == LOG_POSTPONE_CACHE_OVERFLOW)
    {
      // Cache is not usable
      m_cache_status = LOG_POSTPONE_CACHE_NO;
      return false;
    }

  // First cached postpone entry at start_postpone_lsa
  int start_index = -1;
  for (std::size_t i = 0; i < m_cache_entries_cursor; ++i)
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
      assert (false);
      return false;
    }

  // Run all postpones after start_index
  for (std::size_t i = start_index; i < m_cache_entries_cursor; ++i)
    {
      cache_entry *entry = &m_cache_entries[i];

      // Get redo data header
      log_rec_redo *redo = (log_rec_redo *) entry->m_redo_data;

      // Get recovery data
      char *rcv_data = entry->m_redo_data + sizeof (log_rec_redo);
      rcv_data = PTR_ALIGN (rcv_data, MAX_ALIGNMENT);
      (void) log_execute_run_postpone (&thread_ref, &entry->m_lsa, redo, rcv_data);
    }

  // Finished running postpones
  if (start_index == 0)
    {
      // All postpone entries were run
      m_cache_status = LOG_POSTPONE_CACHE_NO;
    }
  else
    {
      // Only some postpone entries were run. Update the number of entries which should be run on next commit
      m_cache_entries_cursor = start_index;
      m_redo_data_ptr = m_cache_entries[start_index].m_redo_data;
    }

  return true;
}
