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
 * log_consumer.cpp
 */

#ident "$Id$"

#include "log_consumer.hpp"
#include "replication_stream.hpp"
#include "replication_serialization.hpp"
#include "replication_buffer.hpp"

#define LC_BUFFER_CAPACITY (1 * 1024 * 1024)

int log_consumer::append_entry (stream_entry *entry)
{
  stream_entries.push_back (entry);
  return NO_ERROR;
}

int log_consumer::consume_thread (void)
{
  for (;;)
    {
      stream_entry* se = new stream_entry ();
      se->receive (serializator);

      /* TODO : notify log_applier threads of new stream entry */
    }
  return NO_ERROR;
}

log_consumer* log_consumer::new_instance (const CONSUMER_TYPE req_type, const stream_position &start_position)
{
  int error_code = NO_ERROR;
  serial_buffer *first_buffer = NULL;
  buffered_range *granted_range;

  log_consumer *new_lc = new log_consumer ();
  new_lc->curr_position = start_position;
  new_lc->m_type = req_type;

  new_lc->consume_stream = new replication_stream (this);
  new_lc->consume_stream->init (new_lc->curr_position);

  error_code = extend_for_write (&first_buffer, LC_BUFFER_CAPACITY);
  if (error_code != NO_ERROR)
    {
      return NO_ERROR;
    }

  new_lc->consume_stream->add_buffer_mapping (first_buffer, WRITE_STREAM, granted_range->first_pos, granted_range->last_pos, &granted_range);

  serializator = new replication_serialization (new_lc->consume_stream);

  return NO_ERROR; 
}

int log_consumer::fetch_for_read (serial_buffer *existing_buffer, const size_t amount)
{
  NOT_IMPLEMENTED ();
  return NO_ERROR;
}


int log_consumer::extend_for_write (serial_buffer **existing_buffer, const size_t amount)
{
  if (*existing_buffer != NULL)
    {
      /* TODO[arnia] : to extend an existing buffer with an amount : 
       * I am not sure we want to do that
       */
      NOT_IMPLEMENTED();
    }


  replication_buffer *my_new_buffer = new replication_buffer (amount);
  my_new_buffer->init (amount);

  add_buffer (my_new_buffer);

  *existing_buffer = my_new_buffer;

  return NO_ERROR;
}
 
int log_consumer::flush_ready_stream (void)
{
  NOT_IMPLEMENTED ();
  return NO_ERROR;
}

replication_stream * log_consumer::get_write_stream (void)
{
  return consume_stream;
}

