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
#include "multi_thread_stream.hpp"
#include <vector>

namespace cubreplication
{

  /*
   * class for producing log stream entries
   * only a global instance (per template class) is allowed
   */

  class log_generator
  {
    private:
      replication_stream_entry m_stream_entry;

      static cubstream::multi_thread_stream *g_stream;

      /* start append position of generator stream */
      static cubstream::stream_position g_start_append_position;

    public:

      log_generator () : m_stream_entry (NULL) { };

      log_generator (cubstream::multi_thread_stream *stream) : m_stream_entry (stream) { };

      ~log_generator ();

      int start_tran_repl (MVCCID mvccid);

      int set_commit_repl (bool commit_tran_flag);

      int append_repl_object (replication_object *object);

      replication_stream_entry *get_stream_entry (void);

      int pack_stream_entry (void);

      static int pack_group_commit_entry (void);

      static int create_stream (const cubstream::stream_position &start_position);

      static cubstream::multi_thread_stream *get_stream (void)
      {
	return g_stream;
      };
  };

} /* namespace cubreplication */

#endif /* _LOG_GENERATOR_HPP_ */
