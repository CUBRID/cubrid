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
#include "replication_buffer.hpp"

static log_generator * log_generator::global_log_generator = NULL;

static int log_generator::new_instance (cubthread::entry *th_entry, stream_position start_position)
{
  log_generator *new_lg = new log_generator ();
  new_lg->curr_position = start_position;

  if (th_entry == NULL)
    {
      /* this is the global instance */
      global_log_generator = new_lg;

      /* attach a log_file */
      new_lg->file = new log_file();
      new_lg->file->open_file (log_file::get_filename (start_position));
    }
  else
    {
      /* TODO[arnia] : set instance per thread  */
    }
  new_lg->buffer = new replication_buffer ();
  new_lg->buffer->init (LG_GLOBAL_INSTANCE_BUFFER_CAPACITY);

  new_lg->stream = new replication_stream (this);
  new_lg->stream->init (new_lg->curr_position);

  serializator = new replication_serialization (new_lg->buffer);

  return NO_ERROR;
}

stream_entry* log_generator::get_stream_entry (cubthread::entry *th_entry)
{
  stream_entry *my_stream_entry = stream_entries[th_entry->index];
  return my_stream_entry;
}

int log_generator::append_entry (cubthread::entry *th_entry, replication_entry *repl_entry)
{
  stream_entry *my_stream_entry = get_stream_entry (th_entry);

  my_stream_entry.add_repl_entry (entry);
}

int log_generator::pack_stream_entries (cubthread::entry *th_entry)
{
  int i;
  size_t repl_entries_size = 0;

  if (th_entry == NULL)
    {
      for (i = 0; i < entries.size (); i++)
        {
          stream_entries[i]->pack (serializator);
        }
    }
  else
    {
      stream_entry *my_stream_entry = get_stream_entry (th_entry);
      my_stream_entry->pack (serializator);
    }

  return NO_ERROR;
}
