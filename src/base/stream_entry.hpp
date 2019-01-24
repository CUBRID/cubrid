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
#include "packer.hpp"
#include "multi_thread_stream.hpp"
#include "error_manager.h"
#include <mutex>
#include <functional>
#include <vector>

namespace cubstream
{
  /*
   * entry of a stream: this is an abstract class which encapsulates a collection of objects packable into a stream
   *
   * it has a mandatory header structure to be implemented by derived class along with associated methods:
   *  - get_packed_header_size,
   *  - set_header_data_size,
   *  - pack_stream_entry_header
   *  - unpack_stream_entry_header
   *  - get_packable_entry_count_from_header
   * The header size must be aligned at MAX_ALIGNMENT.
   * The objects are of type PO (template argument).
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
  template <typename PO>
  class entry
  {
    protected:
      std::vector <PO *> m_packable_entries;

      multi_thread_stream *m_stream;

      stream_position m_data_start_position;

      stream::write_func_t m_packing_func;
      stream::read_prepare_func_t m_prepare_func;
      stream::read_func_t m_unpack_func;

      /*
       * packing_function :
       *  1. init packer (set start pointer and amount of buffer)
       *  2. pack header of entry
       *  3. pack each object of entry
       *  Header is aligned at MAX_ALIGNMENT.
       *  Each packed object is aligned at MAX_ALIGNMENT.
       */
      int packing_func (const stream_position &pos, char *ptr, const size_t reserved_amount)
      {
	unsigned int i;
	cubpacking::packer *serializator = get_packer ();

	size_t aligned_amount = DB_ALIGN (reserved_amount, MAX_ALIGNMENT);
	serializator->set_buffer (ptr, aligned_amount);

	pack_stream_entry_header ();

	for (i = 0; i < m_packable_entries.size (); i++)
	  {
	    serializator->align (MAX_ALIGNMENT);
#if !defined (NDEBUG)
	    const char *start_ptr = serializator->get_curr_ptr ();
	    const char *curr_ptr;
	    size_t entry_size = m_packable_entries[i]->get_packed_size (serializator);
#endif

	    m_packable_entries[i]->pack (serializator);

#if !defined (NDEBUG)
	    curr_ptr = serializator->get_curr_ptr ();
	    assert (curr_ptr - start_ptr == (int) entry_size);
#endif
	  }
	serializator->align (MAX_ALIGNMENT);

	int packed_amount = (int) (serializator->get_curr_ptr () - serializator->get_buffer_start ());

	return packed_amount;
      };

      /* callback header-read function for entry (called with multi_thread_stream::read_serial)
       * 1. init packer
       * 2. unpack entry header
       * 3. saves start of data payload logical position in entry object (to be retrieved at unpack of entry)
       * 4. returns size of payload (value unpacked in header)
       */
      int prepare_func (const stream_position &data_start_pos, char *ptr, const size_t header_size,
			size_t &payload_size)
      {
	cubpacking::unpacker *serializator = get_unpacker ();
	int error_code;

	assert (header_size == get_packed_header_size ());

	serializator->set_buffer (ptr, header_size);

	error_code = unpack_stream_entry_header ();
	if (error_code != NO_ERROR)
	  {
	    return error_code;
	  }

	m_data_start_position = data_start_pos;
	payload_size = get_data_packed_size ();

	return error_code;
      };


      /* unpack function for entry;
       * this unpacks only the payload (objects) of the the entry, the header was unpacked with entry::prepare_func
       * 1. init (un-)packer
       * 2. reads count of packed objects to expect from entry header (previously decoded at prepare_func)
       * 3. peeks the id of next object (the current pointer of packer is not advanced)
       * 4. creates an object using the id
       * 5. unpacks the object
       * 6. adds the object to entry
       */
      int unpack_func (char *ptr, const size_t data_size)
      {
	unsigned int i;
	int error_code = NO_ERROR;
	int object_id;
	cubpacking::unpacker *deserializator = get_unpacker ();
	size_t count_packable_entries = get_packable_entry_count_from_header ();

	deserializator->set_buffer (ptr, data_size);

	for (i = 0 ; i < count_packable_entries; i++)
	  {
	    deserializator->align (MAX_ALIGNMENT);
	    /* peek type of object : it will be consumed by object's unpack */
	    deserializator->peek_unpack_int (object_id);

	    PO *packable_entry = get_builder ()->create_object (object_id);
	    if (packable_entry == NULL)
	      {
		error_code = ER_STREAM_UNPACKING_INV_OBJ_ID;
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_STREAM_UNPACKING_INV_OBJ_ID, 1, object_id);
		return error_code;
	      }

	    assert (packable_entry != NULL);

	    /* unpack one replication entry */
	    add_packable_entry (packable_entry);
	    packable_entry->unpack (deserializator);
	  }
	deserializator->align (MAX_ALIGNMENT);

	assert (deserializator->get_curr_ptr () - ptr == (int) data_size);

	return error_code;
      };

    public:
      using packable_factory = cubbase::factory<int, PO>;

      virtual packable_factory *get_builder () = 0;

      entry (multi_thread_stream *stream)
      {
	m_stream = stream;
	m_data_start_position = 0;

	m_packing_func = std::bind (&entry::packing_func, std::ref (*this),
				    std::placeholders::_1,
				    std::placeholders::_2,
				    std::placeholders::_3);

	m_prepare_func = std::bind (&entry::prepare_func, std::ref (*this),
				    std::placeholders::_1,
				    std::placeholders::_2,
				    std::placeholders::_3,
				    std::placeholders::_4);
	m_unpack_func = std::bind (&entry::unpack_func, std::ref (*this),
				   std::placeholders::_1,
				   std::placeholders::_2);
      };

      virtual ~entry ()
      {
	reset ();
      };

      void set_stream (multi_thread_stream *stream)
      {
	m_stream = stream;
      }

      /*
       * pack method:
       *  1. compute header and data size
       *  2. set data size value in header field (before packing)
       *  3. stream.write using packing_function as argument
       */
      int pack (void)
      {
	size_t total_stream_entry_size;
	size_t data_size;
	int err;
	static size_t header_size = get_packed_header_size ();

	assert (DB_WASTED_ALIGN (header_size, MAX_ALIGNMENT) == 0);

	data_size = (m_packable_entries.size () > 0) ? get_entries_size () : 0;
	data_size = DB_ALIGN (data_size, MAX_ALIGNMENT);
	total_stream_entry_size = header_size + data_size;

	set_header_data_size (data_size);
	assert (DB_WASTED_ALIGN (total_stream_entry_size, MAX_ALIGNMENT) == 0);

	err = m_stream->write (total_stream_entry_size, m_packing_func);

	return (err < 0) ? err : NO_ERROR;
      };

      /*
       * this is pre-unpack method : it fetches enough data to unpack stream header contents,
       * then fetches (receive from socket) the actual amount of data without unpacking it.
       */
      int prepare (void)
      {
	static size_t stream_entry_header_size = get_packed_header_size ();
	int err;

	err = m_stream->read_serial (stream_entry_header_size, m_prepare_func);

	return (err < 0) ? err : NO_ERROR;
      }

      int unpack (void)
      {
	int err = NO_ERROR;
	size_t data_size = get_data_packed_size ();

	if (data_size > 0)
	  {
	    /* read the stream starting from data contents */
	    err = m_stream->read (m_data_start_position, data_size, m_unpack_func);
	  }

	return (err < 0) ? err : NO_ERROR;
      };

      void reset (void)
      {
	destroy_objects ();
      };

      size_t get_entries_size (void)
      {
	size_t entry_size, total_size = 0;
	unsigned int i;

	cubpacking::packer *serializator = get_packer ();
	for (i = 0; i < m_packable_entries.size (); i++)
	  {
	    total_size = DB_ALIGN (total_size, MAX_ALIGNMENT);
	    entry_size = m_packable_entries[i]->get_packed_size (serializator);
	    total_size += entry_size;
	  }

	return total_size;
      };

      int add_packable_entry (PO *entry)
      {
	m_packable_entries.push_back (entry);

	return NO_ERROR;
      };

      PO *get_object_at (int pos)
      {
	return m_packable_entries[pos];
      }

      /* stream entry header methods : header is implementation dependent, is not known here ! */
      virtual cubpacking::packer *get_packer () = 0;
      virtual cubpacking::unpacker *get_unpacker () = 0;
      virtual size_t get_packed_header_size (void) = 0;
      virtual void set_header_data_size (const size_t &data_size) = 0;
      virtual size_t get_data_packed_size (void) = 0;
      virtual int pack_stream_entry_header (void) = 0;
      virtual int unpack_stream_entry_header (void) = 0;
      virtual int get_packable_entry_count_from_header (void) = 0;
      virtual bool is_equal (const entry *other) = 0;

      virtual void destroy_objects ()
      {
	for (unsigned int i = 0; i < m_packable_entries.size (); i++)
	  {
	    if (m_packable_entries[i] != NULL)
	      {
		delete (m_packable_entries[i]);
	      }
	  }
	m_packable_entries.clear ();
      };
  };

} /* namespace cubstream */

#endif /* _STREAM_ENTRY_HPP_ */
