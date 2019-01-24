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

#include "replication_object.hpp"
#include "replication_stream_entry.hpp"
#include "stream_entry.hpp"
#include "error_code.h"
#include <algorithm>

namespace cubreplication
{

  size_t stream_entry::get_data_packed_size (void)
  {
    return m_header.data_size;
  }

  void stream_entry::set_header_data_size (const size_t &data_size)
  {
    m_header.data_size = (int) data_size;
  }

  cubstream::entry<replication_object>::packable_factory *stream_entry::get_builder ()
  {
    static cubstream::entry<replication_object>::packable_factory replication_factory_po;
    static bool created = false;

    if (created == false)
      {
	replication_factory_po.register_creator<sbr_repl_entry> (sbr_repl_entry::PACKING_ID);
	replication_factory_po.register_creator<single_row_repl_entry> (single_row_repl_entry::PACKING_ID);
	created = true;
      }

    return &replication_factory_po;
  }

  int stream_entry::pack_stream_entry_header ()
  {
    cubpacking::packer *serializator = get_packer ();
    unsigned int count_and_flags;
    unsigned int state_flags;

    m_header.count_replication_entries = (int) m_packable_entries.size ();
    serializator->pack_bigint (m_header.prev_record);
    serializator->pack_bigint (m_header.mvccid);

    assert ((m_header.count_replication_entries & stream_entry_header::COUNT_VALUE_MASK)
	    == m_header.count_replication_entries);

    state_flags = m_header.tran_state;
    state_flags = state_flags << (32 - stream_entry_header::STATE_BITS);

    count_and_flags = m_header.count_replication_entries | state_flags;

    serializator->pack_int (count_and_flags);
    serializator->pack_int (m_header.data_size);

    return NO_ERROR;
  }

  int stream_entry::unpack_stream_entry_header ()
  {
    cubpacking::unpacker *serializator = get_unpacker ();
    unsigned int count_and_flags;
    unsigned int state_flags;

    serializator->unpack_bigint (m_header.prev_record);
    serializator->unpack_bigint (m_header.mvccid);
    serializator->unpack_int (reinterpret_cast<int &> (count_and_flags)); // is this safe?s

    state_flags = (count_and_flags & stream_entry_header::STATE_MASK) >> (32 - stream_entry_header::STATE_BITS);
    m_header.tran_state = (stream_entry_header::TRAN_STATE) state_flags;

    m_header.count_replication_entries = count_and_flags & stream_entry_header::COUNT_VALUE_MASK;
    serializator->unpack_int (m_header.data_size);

    return NO_ERROR;
  }

  int stream_entry::get_packable_entry_count_from_header (void)
  {
    return m_header.count_replication_entries;
  }

  bool stream_entry::is_equal (const cubstream::entry<replication_object> *other)
  {
    size_t i;
    const stream_entry *other_t = dynamic_cast <const stream_entry *> (other);

    if (other_t == NULL)
      {
	return false;
      }

    if (m_header.prev_record != other_t->m_header.prev_record
	|| m_header.mvccid != other_t->m_header.mvccid
	|| m_header.data_size != other_t->m_header.data_size
	|| m_header.tran_state != other_t->m_header.tran_state
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

  size_t stream_entry::compute_header_size (void)
  {
    stream_entry_header e;
    cubpacking::packer serializator;

    size_t stream_entry_header_size = e.get_size (serializator);
    size_t aligned_stream_entry_header_size = DB_ALIGN (stream_entry_header_size, MAX_ALIGNMENT);

    return aligned_stream_entry_header_size;
  }

  size_t stream_entry::s_header_size = stream_entry::compute_header_size ();

} /* namespace cubreplication */
