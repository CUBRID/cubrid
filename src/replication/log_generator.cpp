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
#include "replication_stream.hpp"
#include "packing_stream_buffer.hpp"
#include "stream_packer.hpp"
#include "log_file.hpp"
#include "master_replication_channel.hpp"
#include "thread_entry.hpp"
#include "packing_stream.hpp"

log_generator * log_generator::global_log_generator = NULL;

log_generator::~log_generator()
{
  if (this == global_log_generator)
    {
      delete stream;
      log_generator::global_log_generator = NULL;
    }

  for (int i = 0; i < m_stream_entries.size (); i++)
    {
      delete m_stream_entries[i];
      m_stream_entries[i] = NULL;
    }
}

log_generator* log_generator::new_instance (THREAD_ENTRY *th_entry, const stream_position &start_position)
{
  int error_code = NO_ERROR;

  log_generator *new_lg = new log_generator ();
  new_lg->m_append_position = start_position;

  new_lg->stream = new packing_stream (new_lg);
  new_lg->stream->init (new_lg->m_append_position);
  new_lg->stream->set_filled_stream_handler (new_lg);

  if (th_entry == NULL)
    {
      /* this is the global instance */
      assert (global_log_generator == NULL);
      global_log_generator = new_lg;
      /* TODO[arnia] : actual number of transactions */
      new_lg->m_stream_entries.resize (100);
      for (int i = 0; i < 100; i++)
        {
          new_lg->m_stream_entries[i] = new replication_stream_entry (new_lg->stream);
        }
    }
  else
    {
      /* TODO[arnia] : set instance per thread  */
      new_lg->m_stream_entries.resize (1);
      new_lg->m_stream_entries[0] = new replication_stream_entry (new_lg->stream);
    }

  new_lg->stream->acquire_new_write_buffer (new_lg, new_lg->m_append_position, LG_GLOBAL_INSTANCE_BUFFER_CAPACITY, NULL);

  return new_lg;
}

replication_stream_entry* log_generator::get_stream_entry (THREAD_ENTRY *th_entry)
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

int log_generator::append_repl_entry (THREAD_ENTRY *th_entry, packable_object *repl_entry)
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
      /* TODO[arnia] : this should be used only for testing */
      for (i = 0; i < m_stream_entries.size (); i++)
        {
          m_stream_entries[i]->set_packable (true);
        }
    }
  else
    {
      stream_entry *my_stream_entry = get_stream_entry (th_entry);
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
      stream_entry *my_stream_entry = get_stream_entry (th_entry);
      my_stream_entry->pack ();
      my_stream_entry->reset ();
    }

  return NO_ERROR;
}

int log_generator::flush_old_stream_data (void)
{
  int error_code;
  /* detach filled buffers from write stream and send them to MRC_Manager */
  std::vector <buffer_context> ready_buffers;

  error_code = stream->collect_buffers (ready_buffers, COLLECT_ONLY_FILLED_BUFFERS, COLLECT_AND_DETACH);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  /* re-attach the buffers to MRC_Manager */
  master_replication_channel_manager::get_instance()->add_buffers (ready_buffers);

  return error_code;
}
