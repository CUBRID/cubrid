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
 * replication_stream_entry.cpp
 */

#ident "$Id$"

#include "replication_entry.hpp"
#include "replication_stream_entry.hpp"
#include "stream_entry.hpp"
#include "error_code.h"
#include <algorithm>

namespace cubreplication
{

  size_t replication_stream_entry::get_header_size ()
  {
    size_t header_size = 0;
    cubpacking::packer *serializator = get_packer ();

    header_size += serializator->get_packed_bigint_size (header_size);
    header_size += serializator->get_packed_bigint_size (header_size);
    header_size += serializator->get_packed_int_size (header_size);
    header_size += serializator->get_packed_int_size (header_size);

    return header_size;
  }

  size_t replication_stream_entry::get_data_packed_size (void)
  {
    return m_header.data_size;
  }

  void replication_stream_entry::set_header_data_size (const size_t &data_size)
  {
    m_header.data_size = (int) data_size;
  }

  cubstream::entry::packable_factory *replication_stream_entry::get_builder ()
  {
    static cubstream::entry::packable_factory replication_factory_po;
    static bool created = false;

    if (created == false)
      {
	replication_factory_po.register_creator<sbr_repl_entry> (sbr_repl_entry::ID);
	replication_factory_po.register_creator<single_row_repl_entry> (single_row_repl_entry::ID);
	created = true;
      }

    return &replication_factory_po;
  }

  int replication_stream_entry::pack_stream_entry_header ()
  {
    cubpacking::packer *serializator = get_packer ();

    m_header.count_replication_entries = (int) m_packable_entries.size ();
    serializator->pack_bigint ((DB_BIGINT *) &m_header.prev_record);
    serializator->pack_bigint ((DB_BIGINT *) &m_header.mvccid);
    serializator->pack_int (m_header.count_replication_entries);
    serializator->pack_int (m_header.data_size);

    return NO_ERROR;
  }

  int replication_stream_entry::unpack_stream_entry_header ()
  {
    cubpacking::packer *serializator = get_packer ();

    serializator->unpack_bigint ((DB_BIGINT *) &m_header.prev_record);
    serializator->unpack_bigint ((DB_BIGINT *) &m_header.mvccid);
    serializator->unpack_int ((int *) &m_header.count_replication_entries);
    serializator->unpack_int (&m_header.data_size);

    return NO_ERROR;
  }

  int replication_stream_entry::get_packable_entry_count_from_header (void)
  {
    return m_header.count_replication_entries;
  }

  bool replication_stream_entry::is_equal (const cubstream::entry *other)
  {
    int i;
    const replication_stream_entry *other_t = dynamic_cast <const replication_stream_entry *> (other);

    if (other_t == NULL)
      {
	return false;
      }

    if (m_header.prev_record != other_t->m_header.prev_record
	|| m_header.mvccid != other_t->m_header.mvccid
	|| m_header.data_size != other_t->m_header.data_size
	|| m_header.count_replication_entries != other_t->m_header.count_replication_entries
	|| m_packable_entries.size () != other_t->m_packable_entries.size ())
      {
	return false;
      }

    for (i = 0; i < m_packable_entries.size (); i++)
      {
	if (m_packable_entries[i]->is_equal (other_t->m_packable_entries[i]) == false)
	  {
	    return false;
	  }
      }

    return true;
  }

} /* namespace cubreplication */
