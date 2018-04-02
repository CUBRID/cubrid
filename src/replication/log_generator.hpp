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

#include "stream_common.hpp"
#include "buffer_provider.hpp"
#include "packing_stream.hpp"
#include "thread_compat.hpp"
#include "replication_stream.hpp"
#include <vector>

class cubstream::stream_buffer;
class cubpacking::packable_object;
class cubstream::stream_packer;

/* 
 * main class for producing log replication entries
 * it may be created as a local (per transaction/thread) instance to hold a limited number of replication entries
 * or as a global instance
 *
 * TODO : local instance should only hold replication entries, but not pack into buffers
 *        this will be supported later, but needs centralized stream_position in global log_generator
 */

class log_generator : public cubstream::buffer_provider, public cubstream::notify_handler
{
private:
  std::vector<replication_stream_entry*> m_stream_entries;

  cubstream::packing_stream *m_stream;

  /* current append position to be assigned to a new entry */
  cubstream::stream_position m_append_position;

  static log_generator *global_log_generator;

  cubstream::stream_packer *m_serializator;

public:

  log_generator () { m_stream = NULL; };

  ~log_generator ();

  int append_repl_entry (THREAD_ENTRY *th_entry, cubpacking::packable_object *repl_entry);

  void set_ready_to_pack (THREAD_ENTRY *th_entry);

  cubstream::stream_packer *get_serializator (void) { return m_serializator; };

  replication_stream_entry* get_stream_entry (THREAD_ENTRY *th_entry);

  int pack_stream_entries (THREAD_ENTRY *th_entry);

  static log_generator *new_instance (THREAD_ENTRY *th_entry, const cubstream::stream_position &start_position);

  int notify (const cubstream::stream_position pos, const size_t byte_count)
      { return flush_old_stream_data (); };
  int flush_old_stream_data (void);

  cubstream::packing_stream * get_write_stream (void) { return m_stream; };
};

#endif /* _LOG_GENERATOR_HPP_ */
