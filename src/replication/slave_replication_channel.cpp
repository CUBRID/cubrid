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
 * slave_replication_channel.cpp
 */

#ident "$Id$"

#include "slave_replication_channel.hpp"
#include "replication_stream.hpp"

int slave_replication_channel::init (void)
{
  return NO_ERROR;
}


int slave_replication_channel::receive_stream_entry_header (stream_entry_header &se_header)
{

  // receive (socket_id, &stream_entry_header, sizeof (stream_entry_header));

  return NO_ERROR;
}

