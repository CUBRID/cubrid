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
 * replication_stream.cpp
 */

#ident "$Id$"

#include "error_code.h"
#include "replication_entry.hpp"
#include "replication_stream.hpp"
#include "stream_packer.hpp"
#include <algorithm>

size_t replication_stream_entry::get_header_size (void)
{
  return sizeof (m_header);
}

size_t replication_stream_entry::get_data_packed_size (void)
{
  return m_header.data_size;
} 

void replication_stream_entry::set_header_data_size (const size_t &data_size)
{
  m_header.data_size = data_size;
}


int replication_stream_entry::pack_stream_entry_header (stream_packer *serializator)
{
  serializator->pack_bigint (m_header.prev_record);
  serializator->pack_bigint (m_header.mvccid);
  serializator->pack_int (m_header.count_replication_entries);
  serializator->pack_int (m_header.data_size);

  return NO_ERROR;
}

int replication_stream_entry::unpack_stream_entry_header (stream_packer *serializator)
{
  serializator->unpack_bigint ((DB_BIGINT *) &m_header.prev_record);
  serializator->unpack_bigint ((DB_BIGINT *) &m_header.mvccid);
  serializator->unpack_int ((int *) &m_header.count_replication_entries);
  serializator->unpack_int (&m_header.data_size);

  return NO_ERROR;
}