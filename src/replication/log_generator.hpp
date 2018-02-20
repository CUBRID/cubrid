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
 * log_generator.hpp
 */

#ident "$Id$"

#ifndef _LOG_GENERATOR_HPP_
#define _LOG_GENERATOR_HPP_

/* TODO[arnia] : system parameter */
#define LG_GLOBAL_INSTANCE_BUFFER_CAPACITY  (1 * 1024 * 1024)

#include "thread_entry.hpp"
#include "common_utils.hpp"
#include "stream_provider.hpp"
#include <vector>

class replication_entry;
class stream_entry;
class log_file;
class replication_serialization;
class replication_stream;
class serial_buffer;

/* 
 * main class for producing log replication entries
 * it may be created as a local (per transaction/thread) instance to hold a limited number of replication entries
 * or as a global instance
 *
 * TODO : local instance should only hold replication entries, but not pack into buffers
 *        this will be supported later, but needs centralized stream_position in global log_generator
 */
class log_generator : public stream_provider
{
private:
  std::vector<stream_entry*> stream_entries;

  /* file attached to log_generator (only for global instance) */
  log_file *file; 

  replication_stream *stream;

  /* current append position to be assigned to a new entry */
  stream_position append_position;

  static log_generator *global_log_generator;

  replication_serialization *m_serializator;

public:

  log_generator () { file = NULL; stream = NULL; };

  int append_repl_entry (cubthread::entry *th_entry, replication_entry *repl_entry);

  replication_serialization *get_serializator (void) { return m_serializator; };

  stream_entry* get_stream_entry (cubthread::entry *th_entry);

  int pack_stream_entries (cubthread::entry *th_entry);

  static int new_instance (cubthread::entry *th_entry, const stream_position start_position);

  /* stream_provider methods : */
  int fetch_for_read (serial_buffer *existing_buffer, const size_t amount);
  int extend_for_write (serial_buffer **existing_buffer, const size_t amount);
  int flush_ready_stream (void);
  
  replication_stream * get_write_stream (void);
};

#endif /* _LOG_GENERATOR_HPP_ */
