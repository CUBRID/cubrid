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
 * replication_apply_db_copy.cpp
 */

#ident "$Id$"

#include "replication_apply_db_copy.hpp"
#include "replication_stream_entry.hpp"

namespace cubreplication
{

  apply_copy_context::apply_copy_context ()
  {
    m_stream = NULL;
  }

  apply_copy_context::init (void)
  {
    INT64 buffer_size = 1 * 1024 * 1024;
    m_stream = new cubstream::multi_thread_stream (buffer_size, 2);
    m_stream->set_name ("repl_copy_" + std::string (hostname) + "_replica");
    m_stream->set_trigger_min_to_read_size (stream_entry::compute_header_size ());
    m_stream->init (0);

    /* create stream file */
    std::string replication_path;
    replication_node::get_replication_file_path (replication_path);
    m_stream_file = new cubstream::stream_file (*instance->m_stream, replication_path);
    
} /* namespace cubreplication */
