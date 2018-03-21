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

#include "buffer_provider.hpp"
#include "stream_common.hpp"
#include "packing_stream.hpp"
#include <cstddef>

class replication_stream_entry;
class packing_stream_buffer;
class log_file;
class stream_packer;
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
class log_consumer : public buffer_provider, public stream_handler
{
protected:
  CONSUMER_TYPE m_type;

  std::vector<replication_stream_entry*> m_stream_entries;

  /* file attached to log_generator (only for global instance) */
  log_file *file;

  packing_stream *consume_stream;

  slave_replication_channel *m_src;

  /* current append position to be assigned to a new entry */
  stream_position curr_position;

public:

  log_consumer () { file = NULL; };

  int append_entry (replication_stream_entry *entry);

  int fetch_stream_entry (replication_stream_entry **entry);

  int consume_thread (void);

  static log_consumer* new_instance (const CONSUMER_TYPE req_type, const stream_position &start_position);

  int fetch_data (BUFFER_UNIT *ptr, const size_t &amount);
  
  packing_stream * get_write_stream (void);

  int handling_action (BUFFER_UNIT *ptr, size_t byte_count) { return fetch_data (ptr, byte_count); };
};

#endif /* _LOG_CONSUMER_HPP_ */
