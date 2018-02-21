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
 * log_consumer.hpp
 */

#ident "$Id$"

#ifndef _LOG_CONSUMER_HPP_
#define _LOG_CONSUMER_HPP_

#include "stream_provider.hpp"
#include "common_utils.hpp"
#include <cstddef>

class stream_entry;
class replication_stream;
class log_file;
class replication_serialization;
class slave_replication_channel;

enum consumer_type
{
  REPLICATION_DATA_APPLIER = 0,
  DATABASE_COPY,
  REPLICATION_DATA_DUMP
};
typedef enum consumer_type CONSUMER_TYPE;

/* 
 * main class for consuming log replication entries
 * it should be created only as a global instance
 */
class log_consumer : public stream_provider
{
protected:
  CONSUMER_TYPE m_type;

  std::vector<stream_entry*> stream_entries;

  /* file attached to log_generator (only for global instance) */
  log_file *file;

  replication_stream *consume_stream;

  replication_serialization *serializator;

  slave_replication_channel *m_src;

  /* current append position to be assigned to a new entry */
  stream_position curr_position;

public:

  log_consumer () { file = NULL; };

  int append_entry (stream_entry *entry);

  int consume_thread (void);

  log_consumer* new_instance (const CONSUMER_TYPE req_type, const stream_position &start_position);

  int extend_for_write (serial_buffer **existing_buffer, const size_t amount);

  int fetch_for_read (serial_buffer *existing_buffer, const size_t amount);
  
  int flush_ready_stream (void);

  replication_stream * get_write_stream (void);
};

#endif /* _LOG_CONSUMER_HPP_ */
