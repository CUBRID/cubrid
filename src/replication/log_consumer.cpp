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
#include "stream_packer.hpp"
#include "packing_stream_buffer.hpp"

#define LC_BUFFER_CAPACITY (1 * 1024 * 1024)

int log_consumer::append_entry (replication_stream_entry *entry)
{
  m_stream_entries.push_back (entry);
  return NO_ERROR;
}

int log_consumer::consume_thread (void)
{
  for (;;)
    {
      replication_stream_entry* se = new replication_stream_entry ();
      se->receive (m_serializator);

      /* TODO : notify log_applier threads of new stream entry */
    }
  return NO_ERROR;
}

log_consumer* log_consumer::new_instance (const CONSUMER_TYPE req_type, const stream_position &start_position)
{
  int error_code = NO_ERROR;
  packing_stream_buffer *first_buffer = NULL;
  buffered_range *granted_range;

  log_consumer *new_lc = new log_consumer ();
  new_lc->curr_position = start_position;
  new_lc->m_type = req_type;

  new_lc->consume_stream = new packing_stream (this);
  new_lc->consume_stream->init (new_lc->curr_position);

  new_lc->consume_stream->acquire_new_write_buffer (this, new_lc->curr_position, LC_BUFFER_CAPACITY, NULL);

  m_serializator = new stream_packer (new_lc->consume_stream);

  return NO_ERROR; 
}

int log_consumer::fetch_for_read (packing_stream_buffer *existing_buffer, const size_t &amount)
{
  NOT_IMPLEMENTED ();
  return NO_ERROR;
}

 
int log_consumer::flush_ready_stream (void)
{
  NOT_IMPLEMENTED ();
  return NO_ERROR;
}

packing_stream * log_consumer::get_write_stream (void)
{
  return consume_stream;
}
