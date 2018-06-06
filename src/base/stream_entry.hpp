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
 * stream_entry.hpp
 */

#ifndef _STREAM_ENTRY_HPP_
#define _STREAM_ENTRY_HPP_

#include "object_factory.hpp"
#include "packable_object.hpp"
#include "packer.hpp"
#include "packing_stream.hpp"

#include <mutex>
#include <functional>
#include <vector>

namespace cubstream
{
  /*
   * entry of a stream: this is an abstract class which encapsulates a collection of objects packable into a stream
   *
   * it has a mandatory header structure to be implemented by derived class along with associated methods:
   *  - get_header_size,
   *  - set_header_data_size,
   *  - pack_stream_entry_header
   *  - unpack_stream_entry_header
   *  - get_packable_entry_count_from_header
   * The header size must be aligned at MAX_ALIGNEMENT.
   * The objects must derive from packable_object.
   * The concrete class (of entry) must provide the factory get_builder method to build objects
   *
   * As a "protocol", each object has an integer identifier which is packed before actual object packaging:
   * at unpack, this identifier is used to instantiate an object (using the integrated factory), before calling
   * object.unpack
   *
   * get_packer method needs to be implemented (it should be per instance object for multi-threaded usage)
   *
   * is_equal methods is mainly needed for unit testing (is expected to validate that the entire trees of two entries
   * are equivalent)
   */
  class entry
  {
    private:
      bool m_is_packable;

    protected:
      std::vector <cubpacking::packable_object *> m_packable_entries;

      packing_stream *m_stream;

      stream_position m_data_start_position;

      stream::write_func_t m_packing_func;
      stream::read_prepare_func_t m_prepare_func;
      stream::read_func_t m_unpack_func;

      int packing_func (const stream_position &pos, char *ptr, const size_t amount);
      int prepare_func (const stream_position &data_start_pos, char *ptr, const size_t header_size,
			size_t &payload_size);
      int unpack_func (char *ptr, const size_t data_size);

    public:
      using packable_factory = cubbase::factory<int, cubpacking::packable_object>;

      virtual packable_factory *get_builder () = 0;

      entry (packing_stream *stream);

      virtual ~entry()
      {
	reset ();
      };

      int pack (void);

      int prepare (void);

      int unpack (void);

      void reset (void)
      {
	set_packable (false);
	destroy_objects ();
      };

      size_t get_entries_size (void);

      int add_packable_entry (cubpacking::packable_object *entry);

      void set_packable (const bool is_packable)
      {
	m_is_packable = is_packable;
      };

      /* stream entry header methods : header is implementation dependent, is not known here ! */
      virtual cubpacking::packer *get_packer () = 0;
      virtual size_t get_header_size (void) = 0;
      virtual void set_header_data_size (const size_t &data_size) = 0;
      virtual size_t get_data_packed_size (void) = 0;
      virtual int pack_stream_entry_header (void) = 0;
      virtual int unpack_stream_entry_header (void) = 0;
      virtual int get_packable_entry_count_from_header (void) = 0;
      virtual bool is_equal (const entry *other) = 0;
      virtual void destroy_objects ();
  };

} /* namespace cubstream */

#endif /* _STREAM_ENTRY_HPP_ */
