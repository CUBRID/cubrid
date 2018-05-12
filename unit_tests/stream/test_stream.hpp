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

#ifndef _TEST_STREAM_HPP_
#define _TEST_STREAM_HPP_

#include "packable_object.hpp"
#include "packing_stream.hpp"
#include "stream_packer.hpp"
#include "thread_task.hpp"
#include "thread_worker_pool.hpp"
#include "thread_entry_task.hpp"
#include <vector>


namespace test_stream
{

  /* testing of stream with byte objects */
  int test_stream1 (void);
  /* testing of stream with packable objects */
  int test_stream2 (void);

  /* testing of stream with packable objects and multiple cubstream:entry */
  int test_stream3 (void);

  /* testing of stream with packable objects and multiple cubstream:entry using multiple threads */
  int test_stream_mt (void);

  class stream_handler_write : public cubstream::write_handler
  {
    public:
      int write_action (const cubstream::stream_position pos, char *ptr, const size_t byte_count);
  };


  class stream_handler_read : public cubstream::partial_read_handler
  {
    private:
      size_t m_remaining_to_read;
      char expected_val;
    public:
      stream_handler_read ()
      {
	m_remaining_to_read = 0;
      };

      int read_action (const cubstream::stream_position pos, char *ptr, const size_t byte_count, size_t *processed_bytes);
  };

  /* testing of stream with packable objects */
  /* TODO: this code is copied from test_packing unit tests  */
  class po1 : public cubpacking::packable_object
  {
    public:
      static int ID;

      int i1;
      short sh1;
      std::int64_t b1;
      int int_a[5];
      std::vector<int> int_v;
      DB_VALUE values[10];
      char small_str[256];
      std::string large_str;
      std::string str1;
      char str2[300];

    public:

      int pack (cubpacking::packer *serializator);
      int unpack (cubpacking::packer *serializator);

      bool is_equal (const packable_object *other);

      size_t get_packed_size (cubpacking::packer *serializator);

      void generate_obj (void);
  };

  class po2 : public cubpacking::packable_object
  {
    public:
      static int ID;

      std::string large_str;

    public:

      int pack (cubpacking::packer *serializator);
      int unpack (cubpacking::packer *serializator);

      bool is_equal (const packable_object *other);

      size_t get_packed_size (cubpacking::packer *serializator);

      void generate_obj (void);
  };

  struct test_stream_entry_header
  {
    int tran_id;
    int mvcc_id;
    unsigned int count_objects;
    int data_size;
  };

  class test_stream_entry : public cubstream::entry
  {
    private:
      test_stream_entry_header m_header;

      cubstream::stream_packer m_serializator;

    public:
      test_stream_entry (cubstream::packing_stream *stream_p) : entry (stream_p), m_serializator (stream_p) { };

      packable_factory *get_builder ();

      size_t get_header_size ()
      {
	size_t header_size = 0;
	cubstream::stream_packer *serializator = get_packer ();
        header_size += serializator->get_packed_int_size (header_size);
        header_size += serializator->get_packed_int_size (header_size);
	header_size += serializator->get_packed_int_size (header_size);
	header_size += serializator->get_packed_int_size (header_size);

	return header_size;
      };

      size_t get_data_packed_size (void)
      {
	return m_header.data_size;
      };

      void set_tran_id (const int tr_id)
        {
          m_header.tran_id = tr_id;
        }

      void set_mvcc_id (const int mvcc_id)
        {
          m_header.mvcc_id = mvcc_id;
        }

      int &get_tran_id ()
        {
          return m_header.tran_id;
        }
      int &get_mvcc_id ()
        {
          return m_header.mvcc_id;
        }

      void set_header_data_size (const size_t &data_size)
      {
	m_header.data_size = (int) data_size;
      };

      int pack_stream_entry_header ()
      {
	cubstream::stream_packer *serializator = get_packer ();
	m_header.count_objects = (int) m_packable_entries.size ();

        serializator->pack_int (m_header.tran_id);
        serializator->pack_int (m_header.mvcc_id);
	serializator->pack_int (m_header.count_objects);
	serializator->pack_int (m_header.data_size);

	return NO_ERROR;
      };

      int unpack_stream_entry_header ()
      {
	cubstream::stream_packer *serializator = get_packer ();
        serializator->unpack_int ((int *) &m_header.tran_id);
        serializator->unpack_int ((int *) &m_header.mvcc_id);
	serializator->unpack_int ((int *) &m_header.count_objects);
	serializator->unpack_int (&m_header.data_size);

	return NO_ERROR;
      };

      int get_packable_entry_count_from_header (void)
      {
	return m_header.count_objects;
      }

      cubstream::stream_packer *get_packer ()
      {
	return &m_serializator;
      };

      bool is_equal (const cubstream::entry *other)
      {
	int i;
	const test_stream_entry *other_t = dynamic_cast <const test_stream_entry *> (other);

	if (other_t == NULL)
	  {
	    return false;
	  }
	if (m_header.data_size != other_t->m_header.data_size
	    || m_header.count_objects != other_t->m_header.count_objects
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
      };
  };


  class stream_worker_context
    {
    };

  class stream_context_manager : public cubthread::entry_manager
    {
    public:
  

      static int g_cnt_packing_entries_per_thread;
      static int g_cnt_unpacking_entries_per_thread;

      static int g_pack_threads;
      static int g_unpack_threads;

      static test_stream_entry **g_entries;
      static test_stream_entry **g_unpacked_entries;

      static cubstream::packing_stream *g_stream;

      static int g_packed_entries_cnt;
      static int g_unpacked_entries_cnt;

      static bool g_pause_packer;
      static bool g_pause_unpacker;

      static bool g_stop_packer;

      static std::bitset<1024> g_running_packers;
    };

  class stream_pack_task : public cubthread::task<cubthread::entry>
  {
  public:
      void execute (context_type &context);

      int m_tran_id;
  };

  class stream_unpack_task : public cubthread::task<cubthread::entry>
  {
  public:
      void execute (context_type &context);
  }; 

}

#endif /* _TEST_STREAM_HPP_ */
