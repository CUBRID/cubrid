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
 * master_replication_channel.cpp
 */

#ident "$Id$"

#include "master_replication_channel.hpp"
#include "log_file.hpp"

namespace cubreplication
{

master_replication_channel_manager::master_replication_channel_manager (const cubstream::stream_position &start_position)
{
  init (start_position);
}

int master_replication_channel_manager::init (const cubstream::stream_position &start_position)
{
  /* attach a log_file */
  //m_file = new log_file ();
  //m_file->open_file (log_file::get_filename (start_position));
  return NO_ERROR;
}

master_replication_channel_manager *master_replication_channel_manager::get_instance (void)
{
  static master_replication_channel_manager global_mrcm (0);

  return &global_mrcm;
}

int master_replication_channel_manager::add_buffers (std::vector <cubstream::buffer_context> &bufferred_ranges)
{
  std::vector<cubstream::buffer_context>::iterator it;

  for (it = bufferred_ranges.begin (); it != bufferred_ranges.end (); it++)
    {
      add_buffer (it->mapped_buffer);
    }
  
  return NO_ERROR;
}

int master_replication_channel_manager::fetch_data (char *ptr, const size_t &amount)
{
  return NO_ERROR;
}
  
int master_replication_channel_manager::flush_old_stream_data (void)
{
  return NO_ERROR;
}

} /* namespace cubreplication */
