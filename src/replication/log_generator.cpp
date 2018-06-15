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
 * log_generator.cpp
 */

#ident "$Id$"

#include "log_generator.hpp"
#include "replication_stream_entry.hpp"
#include "master_replication_channel.hpp"
#include "thread_entry.hpp"
#include "packing_stream.hpp"

namespace cubreplication
{

  log_generator *log_generator::global_log_generator = NULL;

  log_generator::~log_generator()
  {
    assert (this == global_log_generator);
    delete m_stream;
    log_generator::global_log_generator = NULL;

    for (int i = 0; i < m_stream_entries.size (); i++)
      {
	delete m_stream_entries[i];
	m_stream_entries[i] = NULL;
      }
  }

  log_generator *log_generator::new_instance (const cubstream::stream_position &start_position)
  {
    log_generator *new_lg = new log_generator ();
    new_lg->m_start_append_position = start_position;

    /* create stream only for global instance */
    /* TODO : sys params */
    new_lg->m_stream = new cubstream::packing_stream (100 * 1024 * 1024, 1000);
    new_lg->m_stream->init (new_lg->m_start_append_position);

    /* this is the global instance */
    assert (global_log_generator == NULL);
    global_log_generator = new_lg;
    /* TODO[arnia] : actual number of transactions */
    new_lg->m_stream_entries.resize (1000);
    for (int i = 0; i < 1000; i++)
      {
	new_lg->m_stream_entries[i] = new replication_stream_entry (new_lg->m_stream);
      }

    return new_lg;
  }

  replication_stream_entry *log_generator::get_stream_entry (THREAD_ENTRY *th_entry)
  {
    int stream_entry_idx;

    if (th_entry == NULL)
      {
	stream_entry_idx = 0;
      }
    else
      {
	stream_entry_idx = th_entry->tran_index;
      }
    replication_stream_entry *my_stream_entry = m_stream_entries[stream_entry_idx];
    return my_stream_entry;
  }

  int log_generator::append_repl_entry (THREAD_ENTRY *th_entry, cubpacking::packable_object *repl_entry)
  {
    replication_stream_entry *my_stream_entry = get_stream_entry (th_entry);

    my_stream_entry->add_packable_entry (repl_entry);

    return NO_ERROR;
  }

  void log_generator::set_ready_to_pack (THREAD_ENTRY *th_entry)
  {
    if (th_entry == NULL)
      {
	int i;
	/* TODO : this should be used only for testing */
	for (i = 0; i < m_stream_entries.size (); i++)
	  {
	    m_stream_entries[i]->set_packable (true);
	  }
      }
    else
      {
	cubstream::entry *my_stream_entry = get_stream_entry (th_entry);
	my_stream_entry->set_packable (true);
      }
  }

  int log_generator::pack_stream_entries (THREAD_ENTRY *th_entry)
  {
    int i;
    size_t repl_entries_size = 0;

    if (th_entry == NULL)
      {
	for (i = 0; i < m_stream_entries.size (); i++)
	  {
	    m_stream_entries[i]->pack ();
	    m_stream_entries[i]->reset ();
	  }
      }
    else
      {
	cubstream::entry *my_stream_entry = get_stream_entry (th_entry);
	my_stream_entry->pack ();
	my_stream_entry->reset ();
      }

    return NO_ERROR;
  }

} /* namespace cubreplication */
