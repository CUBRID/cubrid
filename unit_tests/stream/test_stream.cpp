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

#include "test_stream.hpp"

#include "dbtype.h"
#include "multi_thread_stream.hpp"
#include "stream_file.hpp"
#include "object_primitive.h"
#include "object_representation.h"
#include "thread_compat.hpp"
#include "thread_entry.hpp"
#include "thread_manager.hpp"
#include "thread_task.hpp"

#include <iostream>

namespace test_stream
{
//#define GENERATE_RANDOM

  int write_action (const cubstream::stream_position pos, char *ptr, const size_t byte_count)
  {
    int i;
    char *start_ptr = ptr;

    assert (byte_count > OR_INT_SIZE);

    OR_PUT_INT (ptr, byte_count);
    ptr += OR_INT_SIZE;

    for (i = 0; i < (int) byte_count - OR_INT_SIZE; i++)
      {
	*ptr = byte_count % 255;
	ptr++;
      }

    return (int) (ptr - start_ptr);
  }

  int stream_read_partial_context::read_action (char *ptr, const size_t byte_count, size_t &processed_bytes)
  {
    int i;
    size_t to_read;
    size_t byte_count_rem = byte_count;

    while (byte_count_rem > 0)
      {
	if (m_remaining_to_read <= 0)
	  {
	    if (byte_count_rem < OR_INT_SIZE)
	      {
		/* not_enough to decode size prefix */
		break;
	      }

	    m_remaining_to_read = OR_GET_INT (ptr);
	    ptr += OR_INT_SIZE;
	    expected_val = m_remaining_to_read % 255;
	    m_remaining_to_read -= OR_INT_SIZE;
	    byte_count_rem -= OR_INT_SIZE;
	  }

	to_read = MIN (byte_count_rem, m_remaining_to_read);

	for (i = 0; i < (int) to_read; i++)
	  {
	    if (*ptr != expected_val)
	      {
		return ER_FAILED;
	      }
	    ptr++;
	  }

	m_remaining_to_read -= i;

	if (to_read > byte_count_rem)
	  {
	    return ER_FAILED;
	  }
	byte_count_rem -= to_read;
      }

    processed_bytes = byte_count - byte_count_rem;

    return NO_ERROR;
  }

  void generate_str (char *str, size_t len)
  {
    const char chars[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ+_)(*&^%$#@!";
    size_t i = 0;
    while (i < len)
      {
#if defined(GENERATE_RANDOM)
	str[i] = chars[std::rand () % sizeof (chars)];
#else
	str[i] = 'A';
#endif
	i++;
      }
    str[i] = '\0';
  }

  /* po1 */
  void po1::pack (cubpacking::packer &serializer) const
  {
    serializer.pack_int (po1::ID);

    serializer.pack_int (i1);
    serializer.pack_short (sh1);
    serializer.pack_bigint (b1);
    serializer.pack_int_array (int_a, sizeof (int_a) / sizeof (int_a[0]));
    serializer.pack_int_vector (int_v);
    for (unsigned int i = 0; i < sizeof (values) / sizeof (values[0]); i++)
      {
	serializer.pack_db_value (values[i]);
      }
    serializer.pack_small_string (small_str);
    serializer.pack_large_string (large_str);

    serializer.pack_string (str1);

    serializer.pack_c_string (str2, strlen (str2));
  }

  void po1::unpack (cubpacking::unpacker &deserializer)
  {
    int cnt;

    deserializer.unpack_int (cnt);
    assert (cnt == po1::ID);

    deserializer.unpack_int (i1);
    deserializer.unpack_short (sh1);
    deserializer.unpack_bigint (b1);
    deserializer.unpack_int_array (int_a, cnt);
    assert (cnt == sizeof (int_a) / sizeof (int_a[0]));

    deserializer.unpack_int_vector (int_v);

    for (unsigned int i = 0; i < sizeof (values) / sizeof (values[0]); i++)
      {
	deserializer.unpack_db_value (values[i]);
      }
    deserializer.unpack_small_string (small_str, sizeof (small_str));
    deserializer.unpack_large_string (large_str);

    deserializer.unpack_string (str1);
    deserializer.unpack_c_string (str2, sizeof (str2));
  }

  bool po1::is_equal (const cubpacking::packable_object *other)
  {
    const po1 *other_po1 = dynamic_cast<const po1 *> (other);

    if (other_po1 == NULL)
      {
	return false;
      }

    if (i1 != other_po1->i1
	|| sh1 != other_po1->sh1
	|| b1 != other_po1->b1)
      {
	return false;
      }
    for (unsigned int i = 0; i < sizeof (int_a) / sizeof (int_a[0]); i++)
      {
	if (int_a[i] != other_po1->int_a[i])
	  {
	    return false;
	  }
      }

    for (unsigned int i = 0; i < sizeof (values) / sizeof (values[0]); i++)
      {
	if (db_value_compare (&values[i], &other_po1->values[i]) != DB_EQ)
	  {
	    return false;
	  }
      }

    if (strcmp (small_str, other_po1->small_str) != 0)
      {
	return false;
      }

    if (large_str.compare (other_po1->large_str) != 0)
      {
	return false;
      }

    if (str1.compare (other_po1->str1) != 0)
      {
	return false;
      }

    if (strcmp (str2, other_po1->str2) != 0)
      {
	return false;
      }

    return true;
  }

  size_t po1::get_packed_size (cubpacking::packer &serializer, std::size_t start_offset) const
  {
    size_t entry_size = 0;
    /* ID :*/
    entry_size += serializer.get_packed_int_size (entry_size);

    entry_size += serializer.get_packed_int_size (entry_size);
    entry_size += serializer.get_packed_short_size (entry_size);
    entry_size += serializer.get_packed_bigint_size (entry_size);
    entry_size += serializer.get_packed_int_vector_size (entry_size, sizeof (int_a) / sizeof (int_a[0]));
    entry_size += serializer.get_packed_int_vector_size (entry_size, (int) int_v.size ());
    for (unsigned int i = 0; i < sizeof (values) / sizeof (values[0]); i++)
      {
	entry_size += serializer.get_packed_db_value_size (values[i], entry_size);
      }
    entry_size += serializer.get_packed_small_string_size (small_str, entry_size);
    entry_size += serializer.get_packed_large_string_size (large_str, entry_size);

    entry_size += serializer.get_packed_string_size (str1, entry_size);
    entry_size += serializer.get_packed_c_string_size (str2, strlen (str2), entry_size);
    return entry_size;
  }

  void po1::generate_obj (void)
  {
    char *tmp_str;
    size_t str_size;

    i1 = std::rand ();
    sh1 = std::rand ();
    b1 = std::rand ();
    for (unsigned int i = 0; i < sizeof (int_a) / sizeof (int_a[0]); i++)
      {
#if defined (GENERATE_RANDOM)
	int_a[i] = std::rand ();
#else
	int_a[i] = 1000;
#endif
      }
    for (unsigned int i = 0; i < sizeof (values) / sizeof (values[0]); i++)
      {
	switch (std::rand () % 5)
	  {
	  case 0:
#if defined (GENERATE_RANDOM)
	    db_make_int (&values[i], std::rand());
#else
	    db_make_int (&values[i], 10000);
#endif
	    break;
	  case 1:
#if defined (GENERATE_RANDOM)
	    db_make_short (&values[i], std::rand());
#else
	    db_make_short (&values[i], 100);
#endif
	    break;
	  case 2:
#if defined (GENERATE_RANDOM)
	    db_make_bigint (&values[i], std::rand());
#else
	    db_make_bigint (&values[i], 100000);
#endif
	    break;
	  case 3:
#if defined (GENERATE_RANDOM)
	    db_make_double (&values[i], (double) std::rand() / (std::rand() + 1.0f));
#else
	    db_make_double (&values[i], (double) 11);
#endif
	    break;
	  case 4:
	    str_size = std::rand () % 1000 + 1;
	    tmp_str = (char *) db_private_alloc (NULL, str_size + 1);
	    generate_str (tmp_str, str_size);
	    db_make_char (&values[i], (int) str_size, tmp_str, (int) str_size, INTL_CODESET_ISO88591, LANG_COLL_ISO_BINARY);
	    values[i].need_clear = true;
	    break;
	  }
      }

    generate_str (small_str, sizeof (small_str) - 1);

    str_size = std::rand () % 2000 + 1;
    tmp_str = new char[str_size + 1];
    generate_str (tmp_str, str_size);
    large_str = std::string (tmp_str);
    delete []tmp_str;

    str_size = std::rand () % 2000 + 1;
    tmp_str = new char[str_size + 1];
    generate_str (tmp_str, str_size);
    str1 = std::string (tmp_str);
    delete []tmp_str;

    generate_str (str2, sizeof (str2) - 1);
  }

  po1::~po1()
  {
    for (unsigned int i = 0; i < sizeof (values) / sizeof (values[0]); i++)
      {
	if (values[i].need_clear)
	  {
	    pr_clear_value (&values[i]);
	  }
      }
  }
  /* po2 */
  void po2::pack (cubpacking::packer &serializer) const
  {
    serializer.pack_int (po2::ID);

    serializer.pack_large_string (large_str);
  }

  void po2::unpack (cubpacking::unpacker &deserializer)
  {
    int id;

    deserializer.unpack_int (id);

    deserializer.unpack_large_string (large_str);
  }

  bool po2::is_equal (const cubpacking::packable_object *other)
  {
    const po2 *other_po2 = dynamic_cast<const po2 *> (other);

    if (other_po2 == NULL)
      {
	return false;
      }

    if (large_str.compare (other_po2->large_str) != 0)
      {
	return false;
      }

    return true;
  }

  size_t po2::get_packed_size (cubpacking::packer &serializer, std::size_t start_offset) const
  {
    size_t entry_size = 0;
    /* ID :*/
    entry_size += serializer.get_packed_int_size (entry_size);


    entry_size += serializer.get_packed_large_string_size (large_str, entry_size);

    return entry_size;
  }

  void po2::generate_obj (void)
  {
    char *tmp_str;
    size_t str_size;

    str_size = std::rand () % 1000 + 1;
    tmp_str = new char[str_size + 1];
    generate_str (tmp_str, str_size);
    large_str = std::string (tmp_str);
    delete[] tmp_str;
  }

  int po1::ID = 4;
  int po2::ID = 10;

  /* test reading and writing to stream as chunk of bytes with integer header to handle size of chunk */
  int test_stream1 (void)
  {
    int res = 0;
    int i = 0;
    long long desired_amount = 1 * 1024;
    long long writted_amount;
    long long rem_amount;
    int max_data_size = 500;

    cubstream::multi_thread_stream *my_stream = new cubstream::multi_thread_stream (10 * 1024 * 1024, 100);

    cubstream::stream::write_func_t writer_func;
    writer_func = std::bind (&write_action, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);

    stream_read_partial_context reader_context;

    std::cout << "  Testing stream write/read with bytes" << std::endl;

    /* writing in stream */
    for (rem_amount = desired_amount, i = 0; rem_amount > 5; i++)
      {
	int amount = 5 + std::rand () % max_data_size;
	rem_amount -= amount;

	res = my_stream->write (amount, writer_func);
	if (res <= 0)
	  {
	    assert (false);
	    return res;
	  }
      }
    writted_amount = desired_amount - rem_amount;

    /* read from stream */
    cubstream::stream_position start_read_pos = my_stream->get_curr_read_position ();
    cubstream::stream_position curr_read_pos = start_read_pos;
    for (rem_amount = writted_amount; rem_amount > 5; i--)
      {
	int amount = 5 + std::rand () % max_data_size;
	size_t processed_amount;

	amount = MIN ((int) (writted_amount - curr_read_pos), amount);

	res = my_stream->read_partial (curr_read_pos, amount, processed_amount, reader_context.m_reader_partial_func);
	if (res != 0)
	  {
	    assert (false);
	    return res;
	  }

	curr_read_pos += processed_amount;
	rem_amount -= processed_amount;
      }


    return res;
  }

  cubthread::manager *cub_th_m;

  int init_common_cubrid_modules (void)
  {
    static bool initialized = false;
    THREAD_ENTRY *thread_p = NULL;

    if (initialized)
      {
	return 0;
      }

    lang_init ();
    tp_init ();
    er_init ("unit_test", 1);
    lang_set_charset_lang ("en_US.iso88591");
    //cub_th_m.set_max_thread_count (100);

    //cubthread::set_manager (&cub_th_m);
    cubthread::initialize (thread_p);
    cub_th_m = cubthread::get_manager ();
    cub_th_m->set_max_thread_count (100);

    cub_th_m->alloc_entries ();
    cub_th_m->init_entries (false);

    initialized = true;


    return NO_ERROR;
  }

  cubbase::factory<int, cubpacking::packable_object> *test_stream_entry::get_builder ()
  {
    static cubbase::factory<int, cubpacking::packable_object> test_factory_po;
    static bool created = false;
    if (created == false)
      {
	test_factory_po.register_creator<po1> (po1::ID);
	test_factory_po.register_creator<po2> (po2::ID);
	created = true;
      }

    return &test_factory_po;
  }


  class stream_mover
  {
    private:
      static const int BUF_SIZE = 1024;
      char m_buffer[BUF_SIZE];

      int read_action (char *ptr, const size_t byte_count)
      {
	memcpy (m_buffer, ptr, byte_count);
	return (int) byte_count;
      };

      int write_action (const cubstream::stream_position pos, char *ptr, const size_t byte_count)
      {
	memcpy (ptr, m_buffer, byte_count);
	return (int) byte_count;
      };

    public:

      stream_mover ()
      {
	m_reader_func = std::bind (&stream_mover::read_action, std::ref (*this),
				   std::placeholders::_1, std::placeholders::_2);
	m_writer_func = std::bind (&stream_mover::write_action, std::ref (*this),
				   std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);

      };

      size_t get_buf_size (void)
      {
	return sizeof (m_buffer);
      };

      cubstream::stream::read_func_t m_reader_func;
      cubstream::stream::write_func_t m_writer_func;
  };

  int test_stream2 (void)
  {
#define TEST_OBJ_CNT 1000

    int res = 0;
    int i;

    init_common_cubrid_modules ();

    /* create objects */
    std::cout << "  Testing packing/unpacking of objects using same stream" << std::endl;
    cubpacking::packable_object *test_objects[TEST_OBJ_CNT];
    for (i = 0; i < TEST_OBJ_CNT; i++)
      {
	if (std::rand() %2 == 0)
	  {
	    po1 *obj = new po1;
	    obj->generate_obj ();
	    test_objects[i] = obj;
	  }
	else
	  {
	    po2 *obj = new po2;
	    obj->generate_obj ();
	    test_objects[i] = obj;
	  }
      }

    /* create a stream for packing and add pack objects to stream */
    cubstream::multi_thread_stream test_stream_for_pack (10 * 1024 * 1024, 10);
    test_stream_for_pack.set_buffer_reserve_margin (5 * 1024 * 1024);

    test_stream_entry se (&test_stream_for_pack);

    for (i = 0; i < TEST_OBJ_CNT; i++)
      {
	se.add_packable_entry (test_objects[i]);
      }
    res = se.pack ();
    if (res != 0)
      {
	assert (false);
	return res;
      }

    /* use same stream for unpack (same buffers) and create another stream::entry */
    test_stream_entry se2 (&test_stream_for_pack);
    res = se2.prepare ();
    if (res != 0)
      {
	assert (false);
	return res;
      }

    res = se2.unpack ();
    if (res != 0)
      {
	assert (false);
	return res;
      }

    if (se.is_equal (&se2) == false)
      {
	assert (false);
	res = -1;
      }

    /* create a new stream and copy buffers */
    std::cout << "  Testing packing/unpacking of objects using different streams. Copying stream contents." << std::endl;
    cubstream::multi_thread_stream test_stream_for_unpack (10 * 1024 * 1024, 10);

    cubstream::stream_position last_pos = test_stream_for_pack.get_last_committed_pos ();
    cubstream::stream_position curr_pos;

    stream_mover test_stream_mover;
    size_t copy_chunk_size = test_stream_mover.get_buf_size ();
    int read_bytes, written_bytes;

    for (curr_pos = 0; curr_pos < last_pos;)
      {
	copy_chunk_size = MIN (copy_chunk_size, last_pos - curr_pos);

	read_bytes = test_stream_for_pack.read (curr_pos, copy_chunk_size, test_stream_mover.m_reader_func);
	if (read_bytes <= 0)
	  {
	    break;
	  }

	written_bytes = test_stream_for_unpack.write (read_bytes, test_stream_mover.m_writer_func);
	assert (read_bytes == written_bytes);
	if (written_bytes <= 0)
	  {
	    res = -1;
	    return res;
	  }

	curr_pos += read_bytes;
      }

    test_stream_entry se3 (&test_stream_for_unpack);

    res = se3.prepare ();
    if (res != 0)
      {
	assert (false);
	return res;
      }

    res = se3.unpack ();
    if (res != 0)
      {
	assert (false);
	return res;
      }

    if (se.is_equal (&se3) == false)
      {
	res = -1;
	assert (false);
      }

    return res;
#undef TEST_OBJ_CNT
  }

  int test_stream3 (void)
  {
#define TEST_ENTRIES_CNT 300
#define TEST_OBJS_IN_ENTRIES_CNT 200
#define TEST_DELAY_UNPACK_IN_ENTRIES 10

    int res = 0;
    int i, j;

    init_common_cubrid_modules ();

    /* create objects */
    std::cout << "  Testing packing/unpacking of cubstream::entries with sub-objects using same stream" << std::endl;
    /* create a stream for packing and add pack objects to stream */
    cubstream::multi_thread_stream test_stream_for_pack (10 * 1024 * 1024, 10);
    test_stream_for_pack.set_buffer_reserve_margin (100 * 1024);

    std::cout << "      Generating stream entries and objects...";
    test_stream_entry *se_array[TEST_ENTRIES_CNT];
    for (i = 0; i < TEST_ENTRIES_CNT; i++)
      {
	se_array[i] = new test_stream_entry (&test_stream_for_pack);

	for (j = 0; j < TEST_OBJS_IN_ENTRIES_CNT; j++)
	  {
	    if (std::rand() %2 == 0)
	      {
		po1 *obj = new po1;
		obj->generate_obj ();
		se_array[i]->add_packable_entry (obj);
	      }
	    else
	      {
		po2 *obj = new po2;
		obj->generate_obj ();
		se_array[i]->add_packable_entry (obj);
	      }
	  }
      }
    std::cout << "Done" << std::endl;

    std::cout << "      Start test..";
    /* use same stream for unpack (same buffers) and create another stream::entry */
    test_stream_entry *se_array_unpacked[TEST_ENTRIES_CNT];
    for (i = 0; i < TEST_ENTRIES_CNT + TEST_DELAY_UNPACK_IN_ENTRIES; i++)
      {
	if (i < TEST_ENTRIES_CNT)
	  {
	    /* pack original entry */
	    res = se_array[i]->pack ();
	    if (res != 0)
	      {
		assert (false);
		return res;
	      }
	  }

	if (i >= TEST_DELAY_UNPACK_IN_ENTRIES)
	  {
	    se_array_unpacked[i - TEST_DELAY_UNPACK_IN_ENTRIES] = new test_stream_entry (&test_stream_for_pack);

	    /* unpack into a copy */
	    res = se_array_unpacked[i - TEST_DELAY_UNPACK_IN_ENTRIES]->prepare ();
	    if (res != 0)
	      {
		assert (false);
		return res;
	      }

	    res = se_array_unpacked[i - TEST_DELAY_UNPACK_IN_ENTRIES]->unpack ();
	    if (res != 0)
	      {
		assert (false);
		return res;
	      }

	    if (se_array_unpacked[i - TEST_DELAY_UNPACK_IN_ENTRIES]->is_equal (se_array[i - TEST_DELAY_UNPACK_IN_ENTRIES]) == false)
	      {
		res = -1;
		assert (false);
	      }

	    cubstream::stream_position drop_pos = test_stream_for_pack.get_curr_read_position ();

	    test_stream_for_pack.set_last_recyclable_pos (drop_pos);

	  }
      }
    std::cout << "Done" << std::endl;


    return res;

#undef TEST_ENTRIES_CNT
#undef TEST_OBJS_IN_ENTRIES_CNT
#undef TEST_DELAY_UNPACK_IN_ENTRIES
  }


  void stream_pack_task::execute (context_type &context)
  {
    int tran_id = m_tran_id;
    int i;
    int entries_per_tran = stream_context_manager::g_cnt_packing_entries_per_thread;
    test_stream_entry **se_array = stream_context_manager::g_entries;

    stream_context_manager::g_running_packers.set (m_tran_id);

    std::cout << "      Start packing thread " << tran_id << std::endl;
    do
      {
	for (i = entries_per_tran * tran_id; i < entries_per_tran * (tran_id + 1); i++)
	  {
	    if (stream_context_manager::g_stop_packer)
	      {
		break;
	      }

	    while (stream_context_manager::g_pause_packer)
	      {
		std::this_thread::sleep_for (std::chrono::microseconds (100));

		float stream_fill_factor = stream_context_manager::g_stream->stream_fill_factor ();
		if (stream_fill_factor < 0.45f && stream_context_manager::g_pause_packer)
		  {
		    std::cout << "     stream_pack_task : need resume producing;  stream_fill_factor:  " << stream_fill_factor << std::endl;

		    stream_context_manager::g_pause_packer = false;
		  }

		if (stream_context_manager::g_stop_packer)
		  {
		    break;
		  }
	      }
	    stream_context_manager::g_packed_entries_cnt++;
	    se_array[i]->pack ();
	  }

      }
    while (stream_context_manager::g_stop_packer == false);

    std::cout << "      Stopped packing thread:" << tran_id << " at count: " << stream_context_manager::g_packed_entries_cnt
	      << std::endl;
    stream_context_manager::g_running_packers.reset (m_tran_id);
    stream_context_manager::g_pause_unpacker = false;
  }

  void stream_unpack_task::execute (context_type &context)
  {
    int i;
    int res;
    test_stream_entry **se_unpack_array = stream_context_manager::g_unpacked_entries;
    test_stream_entry **se_array = stream_context_manager::g_entries;
    int err = NO_ERROR;

    stream_context_manager::g_running_readers.set (m_reader_id);
    std::cout << "      Start unpacking thread " << std::endl;

    test_stream_entry *prev_se = NULL;
    for (i = 0; ; i++)
      {
	test_stream_entry *se = new test_stream_entry (stream_context_manager::g_stream);

	do
	  {
	    err = se->prepare ();
	    if (err == NO_ERROR)
	      {
		break;
	      }

	    /* unpacking may not have enough data, and could be signalled to block */
	    while (stream_context_manager::g_pause_unpacker == true)
	      {
		std::this_thread::sleep_for (std::chrono::microseconds (10));
	      }

	    if (stream_context_manager::g_running_packers.any () == false
		&& stream_context_manager::g_stream->get_last_committed_pos ()
		== stream_context_manager::g_stream->get_curr_read_position ())
	      {
		/* test has finished, we read all data */
		err = NO_ERROR;
		break;
	      }

	  }
	while (err != NO_ERROR);

	assert (err == NO_ERROR);

	if (stream_context_manager::g_running_packers.any () == false
	    && stream_context_manager::g_stream->get_last_committed_pos ()
	    == stream_context_manager::g_stream->get_curr_read_position ())
	  {
	    /* test has finished, we read all data */
	    delete se;
	    break;
	  }

	do
	  {
	    err = se->unpack ();
	    if (err == NO_ERROR)
	      {
		break;
	      }

	    /* unpacking may not have enough data, and could be signalled to block*/
	    while (stream_context_manager::g_pause_unpacker == true)
	      {
		std::this_thread::sleep_for (std::chrono::microseconds (10));
	      }
	  }
	while (err != NO_ERROR);

	/*/
	if (se_unpack_array[se->get_mvcc_id()] != NULL)
	  {
	    delete se_unpack_array[se->get_mvcc_id()];
	  }
	se_unpack_array[se->get_mvcc_id()] = se;
	*/
	if (se_array != NULL)
	  {
	    res = se->is_equal (se_array[se->get_mvcc_id()]);
	    assert (res == 1);
	  }
	if (prev_se != NULL)
	  {
	    delete prev_se;
	  }
	prev_se = se;
	//delete se;

	stream_context_manager::g_unpacked_entries_cnt++;

	stream_context_manager::update_stream_drop_position ();
      }

    std::cout << "      End of unpacking thread " << std::endl;
    stream_context_manager::g_running_readers.reset (m_reader_id);
  }

  void stream_read_task::execute (context_type &context)
  {
    cubstream::stream_position my_curr_pos = 0;
    cubstream::stream_position last_committed_pos = 0;
    size_t to_read;
    size_t actual_read_bytes;
    int err = NO_ERROR;
    stream_read_partial_copy_context my_read_handler;
    int my_read_cnt = 0;
    bool is_stop = false;

    stream_context_manager::g_running_readers.set (m_reader_id);
    std::cout << "      Start reading (as byte stream) thread " << std::endl;

    while (stream_context_manager::g_running_packers.any () && stream_context_manager::g_stop_packer == false)
      {
	int spin_read = 0;
	to_read = 1 + std::rand () % 1024;

	do
	  {
	    last_committed_pos = stream_context_manager::g_stream->get_last_committed_pos ();
	    if (my_curr_pos + to_read <= last_committed_pos)
	      {
		break;
	      }
	    std::this_thread::sleep_for (std::chrono::microseconds (10));
	    spin_read++;

	    if (spin_read > 100
		&& (stream_context_manager::g_running_packers.any () == false
		    && stream_context_manager::g_stop_packer == true))
	      {
		is_stop = true;
		break;
	      }
	  }
	while (my_curr_pos + to_read > last_committed_pos);

	if (is_stop)
	  {
	    break;
	  }


	err = stream_context_manager::g_stream->read_partial (my_curr_pos, to_read, actual_read_bytes,
	      my_read_handler.m_read_action_func);
	if (err != NO_ERROR)
	  {
	    break;
	  }

	my_curr_pos += actual_read_bytes;
	my_read_cnt++;

	stream_context_manager::g_read_positions[m_reader_id] = my_curr_pos;

	stream_context_manager::update_stream_drop_position ();

	//std::this_thread::sleep_for (std::chrono::microseconds (10));
      }

    std::cout << "      End of reading (as byte stream) thread at position:" <<
	      my_curr_pos << " count:" << my_read_cnt << "err:" << err << std::endl;

    stream_context_manager::g_running_readers.reset (m_reader_id);
  }

  test_stream_entry **stream_context_manager::g_entries = NULL;
  test_stream_entry **stream_context_manager::g_unpacked_entries = NULL;
  int stream_context_manager::g_cnt_packing_entries_per_thread = 0;
  int stream_context_manager::g_cnt_unpacking_entries_per_thread = 0;
  cubstream::multi_thread_stream *stream_context_manager::g_stream = NULL;

  int stream_context_manager::g_pack_threads = 0;
  int stream_context_manager::g_unpack_threads = 0;
  int stream_context_manager::g_read_byte_threads = 0;
  volatile int stream_context_manager::g_packed_entries_cnt = 0;
  volatile int stream_context_manager::g_unpacked_entries_cnt = 0;
  bool stream_context_manager::update_drop_pos_from_readers = true;

  bool stream_context_manager::g_pause_packer = false;
  bool stream_context_manager::g_pause_unpacker = false;
  bool stream_context_manager::g_stop_packer = false;
  std::bitset<1024> stream_context_manager::g_running_packers;
  std::bitset<1024> stream_context_manager::g_running_readers;

  cubstream::stream_position stream_context_manager::g_read_positions[200];

  void stream_context_manager::update_stream_drop_position (void)
  {
    if (stream_context_manager::update_drop_pos_from_readers == false)
      {
	return;
      }

    cubstream::stream_position drop_pos = stream_context_manager::g_stream->get_curr_read_position ();

    for (int j = 0; j < stream_context_manager::g_read_byte_threads; j++)
      {
	drop_pos = MIN (drop_pos, stream_context_manager::g_read_positions[j]);
      }
    stream_context_manager::g_stream->set_last_recyclable_pos (drop_pos);
  }


  class stream_producer_throttling
  {
    public:
      stream_producer_throttling ()
      {
	m_prev_throttle_pos = 0;
	m_notify_func = std::bind (&stream_producer_throttling::notify, std::ref (*this),
				   std::placeholders::_1,
				   std::placeholders::_2);
      };

      cubstream::stream::notify_func_t m_notify_func;
    protected:
      int notify (const cubstream::stream_position pos, const size_t byte_count)
      {
	if (pos >= m_prev_throttle_pos)
	  {
	    m_prev_throttle_pos = pos;
	    std::cout << "      Stream producer throttled position:  " << pos << " bytes: " << byte_count << std::endl;
	    stream_context_manager::g_pause_packer = true;

	    stream_context_manager::update_stream_drop_position ();
	  }

	return NO_ERROR;
      };
    private:
      cubstream::stream_position m_prev_throttle_pos;
  };

  class stream_ready_notifier
  {
    public:
      stream_ready_notifier ()
      {
	m_ready_pos = 0;
	m_notify_func = std::bind (&stream_ready_notifier::notify, std::ref (*this),
				   std::placeholders::_1,
				   std::placeholders::_2);
      };

      cubstream::stream::notify_func_t m_notify_func;
    protected:
      int notify (const cubstream::stream_position pos, const size_t byte_count)
      {
	if (pos > m_ready_pos)
	  {
	    m_ready_pos = pos;
	    std::cout << "      Stream data ready of reading position:  " << pos << " bytes: " << byte_count << std::endl;
	    stream_context_manager::g_pause_unpacker = false;
	  }

	return NO_ERROR;
      };
    private:
      cubstream::stream_position m_ready_pos;
  };
#if 0
  class stream_fetcher
  {
    public:
      stream_fetcher ()
      {
	m_prev_fetch_pos = 0;
	m_fetch_func = std::bind (&stream_fetcher::fetch_action, std::ref (*this),
				  std::placeholders::_1,
				  std::placeholders::_2,
				  std::placeholders::_3,
				  std::placeholders::_4);
      };
      cubstream::stream::fetch_func_t m_fetch_func;
    protected:
      int fetch_action (const cubstream::stream_position pos, char *ptr, const size_t byte_count,
			size_t &processed_bytes)
      {
	int err = ER_FAILED;
	/* for this test, hack processed_bytes as byte_count to avoid stream code assert */
	processed_bytes = byte_count;
	if (pos >= m_prev_fetch_pos)
	  {
	    m_prev_fetch_pos = pos;

	    if (stream_context_manager::g_stream->stream_fill_factor () < 1.0f)
	      {
		std::cout << "      Stream fetch notifier : need resume producing; waiting at position:  " << pos << " bytes: " <<
			  byte_count << std::endl;

		stream_context_manager::g_pause_packer = false;
	      }

	    cubstream::stream_position pos = stream_context_manager::g_stream->get_curr_read_position ();

	    for (int j = 0; j < stream_context_manager::g_read_byte_threads; j++)
	      {
		pos = MIN (pos, stream_context_manager::g_read_positions[j]);
	      }
	    stream_context_manager::g_stream->set_last_dropable_pos (pos);

	    if (pos + byte_count >= stream_context_manager::g_stream->get_last_committed_pos ())
	      {
		err = NO_ERROR;
	      }
	    return err;
	  }

	return err;
      };
    private:
      cubstream::stream_position m_prev_fetch_pos;

      std::mutex m;
      std::condition_variable c;
  };

#endif

  stream_context_manager ctx_m1;
  stream_context_manager ctx_m2;
  stream_context_manager ctx_m3;

  int test_stream_mt (void)
  {
#define TEST_PACK_THREADS 5
#define TEST_UNPACK_THREADS 1
#define TEST_READ_BYTE_THREADS 5
#define TEST_ENTRIES (TEST_PACK_THREADS * 20)
#define TEST_OBJS_IN_ENTRIES_CNT 20

    int res = 0;
    int i, j;

    init_common_cubrid_modules ();

    /* create objects */
    std::cout << "  Testing packing/unpacking of cubstream::entries with sub-objects using same stream and multithreading "
	      << std::endl;
    /* create a stream for packing and add pack objects to stream */
    stream_producer_throttling stream_producer_throttling_handler;
    stream_ready_notifier stream_ready_notify_handler;

    cubstream::multi_thread_stream test_stream_for_pack (10 * 1024 * 1024, TEST_PACK_THREADS);
    test_stream_for_pack.set_buffer_reserve_margin (100 * 1024);
    test_stream_for_pack.set_filled_stream_handler (stream_producer_throttling_handler.m_notify_func);
    test_stream_for_pack.set_ready_pos_handler (stream_ready_notify_handler.m_notify_func);


    std::cout << "      Generating stream entries and objects...";
    test_stream_entry *se_array[TEST_ENTRIES];
    test_stream_entry *se_unpacked_array[TEST_ENTRIES];
    memset (se_unpacked_array, 0, TEST_ENTRIES * sizeof (test_stream_entry *));
    memset (se_array, 0, TEST_ENTRIES * sizeof (test_stream_entry *));

    stream_context_manager::g_entries = se_array;
    stream_context_manager::g_unpacked_entries = se_unpacked_array;

    stream_context_manager::g_cnt_packing_entries_per_thread = TEST_ENTRIES / TEST_PACK_THREADS;
    stream_context_manager::g_cnt_unpacking_entries_per_thread = (TEST_UNPACK_THREADS == 0) ? 1 :
	(TEST_ENTRIES / TEST_UNPACK_THREADS);
    stream_context_manager::g_pack_threads = TEST_PACK_THREADS;
    stream_context_manager::g_unpack_threads = TEST_UNPACK_THREADS;
    stream_context_manager::g_read_byte_threads = TEST_READ_BYTE_THREADS;

    memset (stream_context_manager::g_read_positions, 0, sizeof (stream_context_manager::g_read_positions));

    stream_context_manager::g_running_packers.reset ();
    stream_context_manager::g_running_readers.reset ();

    for (i = 0; i < TEST_ENTRIES; i++)
      {
	se_array[i] = new test_stream_entry (&test_stream_for_pack);

	se_array[i]->set_tran_id (i / stream_context_manager::g_cnt_packing_entries_per_thread);
	se_array[i]->set_mvcc_id (i);

	for (j = 0; j < TEST_OBJS_IN_ENTRIES_CNT; j++)
	  {
	    if (std::rand() %2 == 0)
	      {
		po1 *obj = new po1;
		obj->generate_obj ();
		se_array[i]->add_packable_entry (obj);
	      }
	    else
	      {
		po2 *obj = new po2;
		obj->generate_obj ();
		se_array[i]->add_packable_entry (obj);
	      }
	  }
      }
    std::cout << "Done" << std::endl;

    stream_context_manager::g_stream = &test_stream_for_pack;

    cubthread::entry_workpool *packing_worker_pool =
	    cub_th_m->create_worker_pool (stream_context_manager::g_pack_threads,
					  stream_context_manager::g_pack_threads, NULL, &ctx_m1, 1, false);

    cubthread::entry_workpool *unpacking_worker_pool =
	    cub_th_m->create_worker_pool (stream_context_manager::g_unpack_threads,
					  stream_context_manager::g_unpack_threads, NULL, &ctx_m2, 1, false);

    cubthread::entry_workpool *read_byte_worker_pool =
	    cub_th_m->create_worker_pool (stream_context_manager::g_read_byte_threads,
					  stream_context_manager::g_read_byte_threads, NULL, &ctx_m3, 1, false);

    for (i = 0; i < stream_context_manager::g_pack_threads; i++)
      {
	stream_pack_task *packing_task = new stream_pack_task ();
	packing_task->m_tran_id = i;
	packing_worker_pool->execute (packing_task);
      }

    std::this_thread::sleep_for (std::chrono::milliseconds (1));

    for (i = 0; i < stream_context_manager::g_unpack_threads; i++)
      {
	stream_unpack_task *unpacking_task = new stream_unpack_task ();
	unpacking_task->m_reader_id = i + stream_context_manager::g_read_byte_threads;
	unpacking_worker_pool->execute (unpacking_task);
      }

    for (i = 0; i < stream_context_manager::g_read_byte_threads; i++)
      {
	stream_read_task *read_byte_task = new stream_read_task ();
	read_byte_task->m_reader_id = i;
	read_byte_worker_pool->execute (read_byte_task);
      }

    std::this_thread::sleep_for (std::chrono::seconds (30));
    stream_context_manager::g_stop_packer = true;
    stream_context_manager::g_pause_unpacker = false;
    std::cout << "      Stopping packers" << std::endl;

    stream_context_manager::g_stream->set_stop ();
    while (stream_context_manager::g_running_packers.any ()
	   || stream_context_manager::g_running_readers.any ())
      {
	std::this_thread::sleep_for (std::chrono::milliseconds (100));
      }

    packing_worker_pool->stop_execution ();
    unpacking_worker_pool->stop_execution ();
    read_byte_worker_pool->stop_execution ();

    /* wait for thread manager thread to end */
    cub_th_m->destroy_worker_pool (packing_worker_pool);
    cub_th_m->destroy_worker_pool (unpacking_worker_pool);
    cub_th_m->destroy_worker_pool (read_byte_worker_pool);


    for (i = 0; i < TEST_ENTRIES; i++)
      {
	if (se_array[i] != NULL)
	  {
	    delete se_array[i];
	  }

	if (se_unpacked_array[i] != NULL)
	  {
	    delete se_unpacked_array[i];
	  }
      }

    std::cout << "Done" << std::endl;


    return res;

#undef TEST_PACK_THREADS
#undef TEST_UNPACK_THREADS
#undef TEST_READ_BYTE_THREADS
#undef TEST_ENTRIES
#undef TEST_OBJS_IN_ENTRIES_CNT
  }

  class mts_dummy : public cubstream::multi_thread_stream
  {
    public:
      mts_dummy (const size_t buffer_capacity, const int max_appenders)
	: cubstream::multi_thread_stream (buffer_capacity, max_appenders)
      {
      }

      void force_last_committed (const cubstream::stream_position &pos)
      {
	m_last_committed_pos = pos;
      }
  };

  int test_stream_file1 (size_t file_size, size_t desired_amount, size_t buffer_size)
  {
    int res = 0;
    size_t stream_buffer_size = 1 * 1024 * 1024;

    init_common_cubrid_modules ();

    mts_dummy *my_stream = new mts_dummy (stream_buffer_size, 100);

    my_stream->set_name ("my_test_stream");

    /* path is current folder */
    system ("mkdir test_stream_folder");

    cubstream::stream_file *my_stream_file =
      new cubstream::stream_file (*my_stream, "test_stream_folder", file_size, 2);
    my_stream_file->stop_daemon ();

    cubstream::stream::write_func_t writer_func;
    writer_func = std::bind (&write_action, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);

    std::cout << "  Writing data to stream bytes" << std::endl;

    size_t written_amount;
    cubstream::stream_position stream_pos = 0;

    char *buffer = new char [buffer_size];
    if (buffer == NULL)
      {
	return -1;
      }
    for (int i = 0; i < (int) buffer_size; i++)
      {
	buffer[i] =  std::rand () % 256;
      }

    /* writing directly in stream file */
    for (written_amount = 0; written_amount < desired_amount;)
      {
	int amount = std::rand () % buffer_size;
	amount = (amount == 0) ? 10 : amount;

	my_stream->force_last_committed (stream_pos + amount);
	my_stream_file->start_flush (stream_pos, amount);

	res = my_stream_file->write (stream_pos, buffer, amount);
	if (res < 0)
	  {
	    assert (false);
	    return res;
	  }
	written_amount = written_amount + amount;
	stream_pos += amount;
      }

    my_stream_file->drop_volumes_to_pos (written_amount + my_stream_file->get_volume_size ());

    delete my_stream_file;
    delete my_stream;
    delete[] buffer;
    return res;
  }

  static const char OBJ_BYTE = 0x55;
  static const int OBJ_SIZE = 343;

  int write_object (const cubstream::stream_position pos, char *ptr, const size_t byte_count)
  {
    int i;
    char *start_ptr = ptr;

    OR_PUT_INT (ptr, byte_count);
    ptr += OR_INT_SIZE;

    for (i = 0; i < (int) byte_count - OR_INT_SIZE; i++)
      {
	*ptr = OBJ_BYTE;
	ptr++;
      }

    return (int) (ptr - start_ptr);
  }

  int read_object (char *ptr, const size_t byte_count)
  {
    int i;
    char *start_ptr = ptr;
    int read_byte_count;

    read_byte_count = OR_GET_INT (ptr);
    ptr += OR_INT_SIZE;

    assert (byte_count == read_byte_count);

    for (i = 0; i < (int) read_byte_count - OR_INT_SIZE; i++)
      {
	assert (*ptr == OBJ_BYTE);
	ptr++;
      }

    return (int) (ptr - start_ptr);
  }

  int test_stream_file2 (size_t stream_buffer_size, size_t file_size, size_t desired_amount)
  {
    int res = 0;

    init_common_cubrid_modules ();

    cubstream::multi_thread_stream *my_stream = new cubstream::multi_thread_stream (stream_buffer_size, 100);

    my_stream->set_name ("my_test_stream");

    /* path is current folder */
    system ("mkdir test_stream_folder");
    cubstream::stream_file *my_stream_file = new cubstream::stream_file (*my_stream, "test_stream_folder", file_size);

    cubstream::stream::write_func_t writer_func;
    cubstream::stream::read_func_t reader_func;
    writer_func = std::bind (&write_object, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
    reader_func = std::bind (&read_object, std::placeholders::_1, std::placeholders::_2);

    std::cout << "  Writing data to stream bytes" << std::endl;

    size_t written_amount;
    /* writing in stream */
    for (written_amount = 0; written_amount < desired_amount;)
      {
	res = my_stream->write (OBJ_SIZE, writer_func);
	if (res <= 0)
	  {
	    assert (false);
	    res = -1;
	    return res;
	  }
	written_amount += res;
      }


    cubstream::stream_position pos = 0;
    for (pos = 0; pos < written_amount; )
      {
	res = my_stream->read (pos, OBJ_SIZE, reader_func);
	if (res <= 0)
	  {
	    assert (false);
	    res = -1;
	    return res;
	  }
	pos += res;
      }

    res = 0;


    delete my_stream_file;
    delete my_stream;

    return res;
  }

  stream_context_manager ctx_sf1;
  stream_context_manager ctx_sf2;
  stream_context_manager ctx_sf3;


  int test_stream_file_mt (const int pack_threads,
			   const int unpack_threads,
			   const int read_bytes_threads,
			   const size_t stream_buffer_size,
			   const size_t file_size,
			   const int test_duration)
  {
#define TEST_PACK_THREADS (pack_threads)
#define TEST_UNPACK_THREADS (unpack_threads)
#define TEST_READ_BYTE_THREADS (read_bytes_threads)
#define TEST_ENTRIES (TEST_PACK_THREADS * 20)
#define TEST_OBJS_IN_ENTRIES_CNT 1

    int res = 0;
    int i, j;

    init_common_cubrid_modules ();

    /* create objects */
    std::cout << "  Testing packing/unpacking of cubstream::entries with sub-objects using same"
	      " stream and multithreading and stream_file attached to stream"
	      << std::endl;
    /* create a stream for packing and add pack objects to stream */
    stream_ready_notifier stream_ready_notify_handler;

    cubstream::multi_thread_stream test_stream_for_pack (stream_buffer_size, TEST_PACK_THREADS);
    test_stream_for_pack.set_name ("stream_for_pack_mt");
    test_stream_for_pack.set_buffer_reserve_margin (100 * 1024);

    /* path is current folder */
    system ("mkdir test_stream_folder");
    cubstream::stream_file *my_stream_file =
      new cubstream::stream_file (test_stream_for_pack, "test_stream_folder", file_size);

    //test_stream_for_pack.set_ready_pos_handler (stream_ready_notify_handler.m_notify_func);


    std::cout << "      Generating stream entries and objects...";
    test_stream_entry **se_array = new test_stream_entry*[TEST_ENTRIES];
    test_stream_entry **se_unpacked_array = new test_stream_entry*[TEST_ENTRIES];
    memset (se_unpacked_array, 0, TEST_ENTRIES * sizeof (test_stream_entry *));
    memset (se_array, 0, TEST_ENTRIES * sizeof (test_stream_entry *));

    stream_context_manager::g_entries = se_array;
    stream_context_manager::g_unpacked_entries = se_unpacked_array;

    stream_context_manager::g_cnt_packing_entries_per_thread = TEST_ENTRIES / TEST_PACK_THREADS;
    stream_context_manager::g_cnt_unpacking_entries_per_thread = (TEST_UNPACK_THREADS == 0) ? 1 : TEST_ENTRIES /
	TEST_UNPACK_THREADS;
    stream_context_manager::g_pack_threads = TEST_PACK_THREADS;
    stream_context_manager::g_unpack_threads = TEST_UNPACK_THREADS;
    stream_context_manager::g_read_byte_threads = TEST_READ_BYTE_THREADS;

    memset (stream_context_manager::g_read_positions, 0, sizeof (stream_context_manager::g_read_positions));

    stream_context_manager::g_stop_packer = false;
    stream_context_manager::g_pause_unpacker = false;
    stream_context_manager::g_running_packers.reset ();
    stream_context_manager::g_running_readers.reset ();
    /* drop position of stream should be updated only by flusher */
    stream_context_manager::update_drop_pos_from_readers = false;

    for (i = 0; i < TEST_ENTRIES; i++)
      {
	se_array[i] = new test_stream_entry (&test_stream_for_pack);

	se_array[i]->set_tran_id (i / stream_context_manager::g_cnt_packing_entries_per_thread);
	se_array[i]->set_mvcc_id (i);

	for (j = 0; j < TEST_OBJS_IN_ENTRIES_CNT; j++)
	  {
	    if (std::rand() %2 == 0)
	      {
		po1 *obj = new po1;
		obj->generate_obj ();
		se_array[i]->add_packable_entry (obj);
	      }
	    else
	      {
		po2 *obj = new po2;
		obj->generate_obj ();
		se_array[i]->add_packable_entry (obj);
	      }
	  }
      }
    std::cout << "Done" << std::endl;

    stream_context_manager::g_stream = &test_stream_for_pack;

    cubthread::entry_workpool *packing_worker_pool =
	    cub_th_m->create_worker_pool (stream_context_manager::g_pack_threads,
					  stream_context_manager::g_pack_threads, NULL, &ctx_sf1, 1, false);

    cubthread::entry_workpool *unpacking_worker_pool  = NULL;
    if (stream_context_manager::g_unpack_threads > 0)
      {
	unpacking_worker_pool =
		cub_th_m->create_worker_pool (stream_context_manager::g_unpack_threads,
					      stream_context_manager::g_unpack_threads, NULL, &ctx_sf2, 1, false);
      }

    cubthread::entry_workpool *read_byte_worker_pool = NULL;
    if (stream_context_manager::g_read_byte_threads > 0)
      {
	read_byte_worker_pool =
		cub_th_m->create_worker_pool (stream_context_manager::g_read_byte_threads,
					      stream_context_manager::g_read_byte_threads, NULL, &ctx_sf3, 1, false);
      }

    for (i = 0; i < stream_context_manager::g_pack_threads; i++)
      {
	stream_pack_task *packing_task = new stream_pack_task ();
	packing_task->m_tran_id = i;
	packing_worker_pool->execute (packing_task);
      }

    std::this_thread::sleep_for (std::chrono::milliseconds (1));

    stream_unpack_task *unpacking_task = NULL;
    for (i = 0; i < stream_context_manager::g_unpack_threads; i++)
      {
	unpacking_task = new stream_unpack_task ();
	unpacking_task->m_reader_id = i + stream_context_manager::g_read_byte_threads;
	unpacking_worker_pool->execute (unpacking_task);
      }

    stream_read_task *read_byte_task = NULL;
    for (i = 0; i < stream_context_manager::g_read_byte_threads; i++)
      {
	read_byte_task = new stream_read_task ();
	read_byte_task->m_reader_id = i;
	read_byte_worker_pool->execute (read_byte_task);
      }

    std::chrono::system_clock::time_point start_test_time = std::chrono::system_clock::now ();

    std::chrono::system_clock::time_point now_time;
    std::chrono::system_clock::time_point last_print_time = start_test_time;
    int time_in_sec;
    do
      {
	std::this_thread::sleep_for (std::chrono::seconds (1));
	now_time = std::chrono::system_clock::now ();
	int delta_time_in_sec = std::chrono::duration_cast<std::chrono::seconds> (now_time - last_print_time).count ();
	time_in_sec = std::chrono::duration_cast<std::chrono::seconds> (now_time - start_test_time).count ();

	if (delta_time_in_sec > 10)
	  {
	    cubstream::stream_position lowest_read_pos = std::numeric_limits<cubstream::stream_position>::max ();

	    lowest_read_pos = stream_context_manager::g_stream->get_curr_read_position ();

	    std::cout << "Recyclable_pos: " << stream_context_manager::g_stream->get_last_recyclable_pos () << " (" <<
		      stream_context_manager::g_stream->get_last_recyclable_pos () / time_in_sec / 1024 << " KB/s)";
	    std::cout << " Last_committed: " << stream_context_manager::g_stream->get_last_committed_pos () << " (" <<
		      stream_context_manager::g_stream->get_last_committed_pos () / time_in_sec / 1024 << " KB/s)" << std::endl;
	    std::cout << "Serial_read_pos: " << stream_context_manager::g_stream->get_curr_read_position () << " (" <<
		      stream_context_manager::g_stream->get_curr_read_position () / time_in_sec / 1024 << " KB/s)" << std::endl;
	    std::cout << "Parallel read pos: ";
	    for (i = 0; i < stream_context_manager::g_read_byte_threads; i++)
	      {
		std::cout << stream_context_manager::g_read_positions[i] << " (" << stream_context_manager::g_read_positions[i] /
			  time_in_sec / 1024 << " KB/s),";
		if (stream_context_manager::g_read_positions[i] < lowest_read_pos)
		  {
		    lowest_read_pos = stream_context_manager::g_read_positions[i];
		  }
	      }

	    std::cout << std::endl;
	    std::cout << std::endl;

	    last_print_time = now_time;

	    if (lowest_read_pos < std::numeric_limits<cubstream::stream_position>::max ())
	      {
		std::cout << "Drop volumes up position "<< lowest_read_pos << std::endl;
		cubstream::stream_file *stream_file_p = stream_context_manager::g_stream->get_stream_file();
		stream_file_p->drop_volumes_to_pos (lowest_read_pos);
	      }
	  }
      }
    while (time_in_sec < test_duration);


    stream_context_manager::g_stop_packer = true;
    stream_context_manager::g_pause_unpacker = false;
    std::cout << "      Stopping packers" << std::endl;

    stream_context_manager::g_stream->set_stop ();
    while (stream_context_manager::g_running_packers.any ()
	   || stream_context_manager::g_running_readers.any ())
      {
	std::this_thread::sleep_for (std::chrono::milliseconds (100));
      }

    packing_worker_pool->stop_execution ();
    if (stream_context_manager::g_unpack_threads > 0)
      {
	unpacking_worker_pool->stop_execution ();
      }
    if (stream_context_manager::g_read_byte_threads > 0)
      {
	read_byte_worker_pool->stop_execution ();
      }

    /* wait for thread manager thread to end */
    cub_th_m->destroy_worker_pool (packing_worker_pool);
    if (stream_context_manager::g_unpack_threads > 0)
      {
	cub_th_m->destroy_worker_pool (unpacking_worker_pool);
      }
    if (stream_context_manager::g_read_byte_threads > 0)
      {
	cub_th_m->destroy_worker_pool (read_byte_worker_pool);
      }


    for (i = 0; i < TEST_ENTRIES; i++)
      {
	if (se_array[i] != NULL)
	  {
	    delete se_array[i];
	  }

	if (se_unpacked_array[i] != NULL)
	  {
	    delete se_unpacked_array[i];
	  }
      }

    std::cout << "Done" << std::endl;

    my_stream_file->drop_volumes_to_pos (stream_context_manager::g_stream->get_last_recyclable_pos () +
					 my_stream_file->get_volume_size ());

    delete my_stream_file;

    /* turn back on updating drop pos from readers */
    stream_context_manager::update_drop_pos_from_readers = true;

    return res;

#undef TEST_PACK_THREADS
#undef TEST_UNPACK_THREADS
#undef TEST_READ_BYTE_THREADS
#undef TEST_ENTRIES
#undef TEST_OBJS_IN_ENTRIES_CNT
  }


  int test_stream_file_reader (const unsigned long long stream_read_start,
			       const unsigned long long stream_max_pos,
			       const size_t stream_buffer_size,
			       const size_t file_size)
  {
    int res = 0;
    int i;
    int test_duration = 10000;
    init_common_cubrid_modules ();

    /* create objects */
    std::cout << "  Testing reading from existing stream file"
	      << std::endl;
    /* create a stream for packing and add pack objects to stream */
    stream_ready_notifier stream_ready_notify_handler;

    cubstream::multi_thread_stream test_stream_for_read (stream_buffer_size, 1);
    test_stream_for_read.set_name ("stream_for_pack_mt");
    test_stream_for_read.set_buffer_reserve_margin (100 * 1024);
    test_stream_for_read.init (stream_max_pos);
    test_stream_for_read.force_serial_data_read (stream_read_start);

    /* path is current folder */
    system ("mkdir test_stream_folder");
    cubstream::stream_file *my_stream_file =
      new cubstream::stream_file (test_stream_for_read, "test_stream_folder", file_size);
    my_stream_file->init ("test_stream_folder", stream_max_pos);
    my_stream_file->drop_volumes_to_pos (0, true);

    stream_context_manager::g_stream = &test_stream_for_read;

    cubthread::entry_workpool *unpacking_worker_pool  = NULL;
    stream_context_manager::g_unpack_threads = 1;
    stream_context_manager::g_read_byte_threads = 0;
    if (stream_context_manager::g_unpack_threads > 0)
      {
	unpacking_worker_pool =
		cub_th_m->create_worker_pool (stream_context_manager::g_unpack_threads,
					      stream_context_manager::g_unpack_threads, NULL, &ctx_m2, 1, false);
      }

    std::this_thread::sleep_for (std::chrono::milliseconds (1));

    stream_unpack_task *unpacking_task = NULL;
    for (i = 0; i < stream_context_manager::g_unpack_threads; i++)
      {
	unpacking_task = new stream_unpack_task ();
	unpacking_task->m_reader_id = i + stream_context_manager::g_read_byte_threads;
	unpacking_worker_pool->execute (unpacking_task);
      }

    std::chrono::system_clock::time_point start_test_time = std::chrono::system_clock::now ();

    std::chrono::system_clock::time_point now_time;
    std::chrono::system_clock::time_point last_print_time = start_test_time;
    int time_in_sec;
    do
      {
	std::this_thread::sleep_for (std::chrono::seconds (1));
	now_time = std::chrono::system_clock::now ();
	int delta_time_in_sec = std::chrono::duration_cast<std::chrono::seconds> (now_time - last_print_time).count ();
	time_in_sec = std::chrono::duration_cast<std::chrono::seconds> (now_time - start_test_time).count ();
	if (delta_time_in_sec > 10)
	  {
	    cubstream::stream_position lowest_read_pos = std::numeric_limits<cubstream::stream_position>::max ();

	    lowest_read_pos = stream_context_manager::g_stream->get_curr_read_position ();

	    std::cout << "Recyclable_pos: " << stream_context_manager::g_stream->get_last_recyclable_pos () << " (" <<
		      stream_context_manager::g_stream->get_last_recyclable_pos () / time_in_sec / 1024 << " KB/s)";
	    std::cout << " Last_committed: " << stream_context_manager::g_stream->get_last_committed_pos () << " (" <<
		      stream_context_manager::g_stream->get_last_committed_pos () / time_in_sec / 1024 << " KB/s)" << std::endl;
	    std::cout << "Serial_read_pos: " << stream_context_manager::g_stream->get_curr_read_position () << " (" <<
		      stream_context_manager::g_stream->get_curr_read_position () / time_in_sec / 1024 << " KB/s)" << std::endl;
	    std::cout << "Parallel read pos: ";
	    for (i = 0; i < stream_context_manager::g_read_byte_threads; i++)
	      {
		std::cout << stream_context_manager::g_read_positions[i] << " (" << stream_context_manager::g_read_positions[i] /
			  time_in_sec / 1024 << " KB/s),";
		if (stream_context_manager::g_read_positions[i] < lowest_read_pos)
		  {
		    lowest_read_pos = stream_context_manager::g_read_positions[i];
		  }
	      }

	    std::cout << std::endl;
	    std::cout << std::endl;

	    last_print_time = now_time;

	    if (lowest_read_pos < std::numeric_limits<cubstream::stream_position>::max ())
	      {
		std::cout << "Drop volumes up position "<< lowest_read_pos << std::endl;
		cubstream::stream_file *stream_file_p = stream_context_manager::g_stream->get_stream_file();
		//stream_file_p->drop_volumes_to_pos (lowest_read_pos);
	      }
	  }
      }
    while (time_in_sec < test_duration);


    stream_context_manager::g_stop_packer = true;
    stream_context_manager::g_pause_unpacker = false;
    std::cout << "      Stopping packers" << std::endl;

    stream_context_manager::g_stream->set_stop ();
    while (stream_context_manager::g_running_packers.any ()
	   || stream_context_manager::g_running_readers.any ())
      {
	std::this_thread::sleep_for (std::chrono::milliseconds (100));
      }

    if (stream_context_manager::g_unpack_threads > 0)
      {
	unpacking_worker_pool->stop_execution ();
      }
    /* wait for thread manager thread to end */
    if (stream_context_manager::g_unpack_threads > 0)
      {
	cub_th_m->destroy_worker_pool (unpacking_worker_pool);
      }


    std::cout << "Done" << std::endl;

    my_stream_file->drop_volumes_to_pos (stream_context_manager::g_stream->get_last_recyclable_pos () +
					 my_stream_file->get_volume_size ());

    delete my_stream_file;

    /* turn back on updating drop pos from readers */
    stream_context_manager::update_drop_pos_from_readers = true;

    return res;

#undef TEST_PACK_THREADS
#undef TEST_UNPACK_THREADS
#undef TEST_READ_BYTE_THREADS
#undef TEST_ENTRIES
#undef TEST_OBJS_IN_ENTRIES_CNT
  }
}
