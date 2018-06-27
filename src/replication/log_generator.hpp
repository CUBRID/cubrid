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


#include "replication_object.hpp"
#include "replication_stream_entry.hpp"
#include "packing_stream.hpp"
#include "thread_compat.hpp"
#include <vector>

namespace cubreplication
{

  /*
   * main class for producing log stream entries
   * it is a templatized class : stream entries depends on the actual stream contents
   * only a global instance (per template class) is allowed
   */

  class log_generator
  {
    private:
      std::vector<replication_stream_entry *> m_stream_entries;

      cubstream::packing_stream *m_stream;

      /* start append position of generator stream */
      cubstream::stream_position m_start_append_position;

      static log_generator *global_log_generator;

    public:

      log_generator () : m_stream (NULL) { };

      ~log_generator ();

      int start_tran_repl (THREAD_ENTRY *th_entry, MVCCID mvccid);

      int set_commit_repl (THREAD_ENTRY *th_entry, bool commit_tran_flag);

      int append_repl_entry (THREAD_ENTRY *th_entry, replication_object *object);

      replication_stream_entry *get_stream_entry (THREAD_ENTRY *th_entry);

      int pack_stream_entries (THREAD_ENTRY *th_entry);

      int pack_group_commit_entry (void);

      static log_generator *new_instance (const cubstream::stream_position &start_position);

      cubstream::packing_stream *get_stream (void)
      {
	return m_stream;
      };
  };

} /* namespace cubreplication */

#endif /* _LOG_GENERATOR_HPP_ */
