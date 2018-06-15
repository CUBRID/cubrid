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
 * replication_stream_entry.hpp
 */

#ident "$Id$"

#ifndef _REPLICATION_STREAM_ENTRY_HPP_
#define _REPLICATION_STREAM_ENTRY_HPP_

#include "stream_entry.hpp"
#include "replication_entry.hpp"
#include "storage_common.h"
#include <vector>

namespace cubreplication
{

  struct replication_stream_entry_header
  {
    cubstream::stream_position prev_record;
    MVCCID mvccid;
    unsigned int count_replication_entries;
    int data_size;

    replication_stream_entry_header ()
      : prev_record (0),
	mvccid (MVCCID_NULL),
	count_replication_entries (0)
    {
    };
  };

  class replication_stream_entry : public cubstream::entry
  {
    private:
      replication_stream_entry_header m_header;
      cubpacking::packer m_serializator;

    public:
      replication_stream_entry (cubstream::packing_stream *stream_p)
	: entry (stream_p)
      {
      };

      size_t get_header_size ();
      size_t get_data_packed_size (void);
      void set_header_data_size (const size_t &data_size);

      cubstream::entry::packable_factory *get_builder ();

      cubpacking::packer *get_packer ()
      {
	return &m_serializator;
      };

      int pack_stream_entry_header ();
      int unpack_stream_entry_header ();
      int get_packable_entry_count_from_header (void);

      bool is_equal (const cubstream::entry *other);
  };

} /* namespace cubreplication */

#endif /* _REPLICATION_STREAM_ENTRY_HPP_ */
