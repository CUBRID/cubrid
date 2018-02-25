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

log_generator * log_generator::global_log_generator = NULL;

int log_generator::new_instance (cubthread::entry *th_entry, const stream_position &start_position)
{
  int error_code = NO_ERROR;
  buffered_range *granted_range;

  log_generator *new_lg = new log_generator ();
  new_lg->append_position = start_position;

  if (th_entry == NULL)
    {
      /* this is the global instance */
      global_log_generator = new_lg;

      /* attach a log_file */
      new_lg->file = new log_file ();
      new_lg->file->open_file (log_file::get_filename (start_position));
    }
  else
    {
      /* TODO[arnia] : set instance per thread  */
    }

  new_lg->stream = new packing_stream (new_lg);
  new_lg->stream->init (new_lg->append_position);

  new_lg->stream->acquire_new_write_buffer (new_lg, new_lg->append_position, LG_GLOBAL_INSTANCE_BUFFER_CAPACITY, NULL);

  new_lg->m_serializator = new stream_packer (new_lg->stream);

  return NO_ERROR;
}

stream_entry* log_generator::get_stream_entry (cubthread::entry *th_entry)
{
  stream_entry *my_stream_entry = stream_entries[th_entry->index];
  return my_stream_entry;
}

int log_generator::append_repl_entry (cubthread::entry *th_entry, packable_object *repl_entry)
{
  stream_entry *my_stream_entry = get_stream_entry (th_entry);

  my_stream_entry->add_packable_entry (repl_entry);

  return NO_ERROR;
}

int log_generator::pack_stream_entries (cubthread::entry *th_entry)
{
  int i;
  size_t repl_entries_size = 0;

  if (th_entry == NULL)
    {
      for (i = 0; i < stream_entries.size (); i++)
        {
          stream_entries[i]->pack (m_serializator);
        }
    }
  else
    {
      stream_entry *my_stream_entry = get_stream_entry (th_entry);
      my_stream_entry->pack (m_serializator);
    }

  return NO_ERROR;
}

int log_generator::fetch_for_read (packing_stream_buffer *existing_buffer, const size_t &amount)
{
  /* data is pushed to log_generator, we don't ask for it */
  assert (false);
  return NO_ERROR;
}

int log_generator::flush_ready_stream (void)
{
  int error_code;
  /* detach filled buffers from write stream and send them to MRC_Manager */
  std::vector <buffered_range> ready_buffers;

  error_code = stream->detach_written_buffers (ready_buffers);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  /* re-attach the buffers to MRC_Manager */
  master_replication_channel_manager::get_instance()->add_buffers (ready_buffers);

  return error_code;
}
