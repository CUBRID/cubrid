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
 * replication_node.cpp
 */

#ident "$Id$"

#include "replication_node.hpp"
#include "multi_thread_stream.hpp"
#include "boot_sr.h"        /* database full name */
#include "error_code.h"
#include "file_io.h"        /* file name manipulation */
#include "stream_file.hpp"

namespace cubreplication
{
  replication_node::~replication_node ()
  {

  }

  std::string replication_node::get_replication_file_path ()
  {
    char buf_temp_path[PATH_MAX];

    char *temp_path = fileio_get_directory_path (buf_temp_path, boot_db_full_name ());
    return std::string (temp_path);
  }

  const unsigned long long replication_node::SETUP_REPLICATION_MAGIC = 0x19912882ULL;
  const unsigned long long replication_node::SETUP_COPY_REPLICATION_MAGIC = 0x36634554ULL;
  const unsigned long long replication_node::SETUP_COPY_END_REPLICATION_MAGIC = 0x28821771ULL;

} /* namespace cubreplication */
