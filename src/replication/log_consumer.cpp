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
#include "stream_buffer.hpp"

namespace cubreplication
{

#define LC_BUFFER_CAPACITY (1 * 1024 * 1024)

int log_consumer::append_entry (replication_stream_entry *entry)
{
  /* TODO : split list of entries by transaction */
  m_stream_entries.push_back (entry);
  return NO_ERROR;
}

int log_consumer::fetch_stream_entry (replication_stream_entry **entry)
{
  int err = NO_ERROR;

  replication_stream_entry* se = new replication_stream_entry (get_write_stream ());

  err = se->prepare ();
  if (err != NO_ERROR)
    {
      return err;
    }

  *entry = se;

  return err;
}

int log_consumer::consume_thread (void)
{
  int err = NO_ERROR;
  
   for (;;)
    {
      replication_stream_entry * se = NULL;
      err = fetch_stream_entry (&se);
      if (err != NO_ERROR)
        {
          break;
        }
      append_entry (se);
    }
  return NO_ERROR;
}

log_consumer* log_consumer::new_instance (const CONSUMER_TYPE req_type,
                                          const cubstream::stream_position &start_position)
{
  int error_code = NO_ERROR;

  log_consumer *new_lc = new log_consumer ();
  new_lc->curr_position = start_position;
  new_lc->m_type = req_type;

  new_lc->consume_stream = new cubstream::packing_stream (new_lc);
  new_lc->consume_stream->init (new_lc->curr_position);

  new_lc->consume_stream->acquire_new_write_buffer (new_lc, new_lc->curr_position, LC_BUFFER_CAPACITY, NULL);
  new_lc->consume_stream->set_fetch_data_handler (new_lc);

  return new_lc; 
}

int log_consumer::fetch_data (char *ptr, const size_t &amount)
{
  // m_src->receive (ptr, amount);
  return NO_ERROR;
}
 

cubstream::packing_stream * log_consumer::get_write_stream (void)
{
  return consume_stream;
}

} /* namespace cubreplication */