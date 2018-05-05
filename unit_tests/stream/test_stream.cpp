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
#include "buffer_provider.hpp"
#include "object_representation.h"
#include "packing_stream.hpp"
#include "thread_compat.hpp"
#include "thread_manager.hpp"
#include <iostream>

namespace test_stream
{

  int stream_handler_write::write_action (const cubstream::stream_position pos, char *ptr, const size_t byte_count)
  {
    int i;
    char *start_ptr = ptr;

    assert (byte_count > OR_INT_SIZE);

    OR_PUT_INT (ptr, byte_count);
    ptr += OR_INT_SIZE;

    for (i = 0; i < byte_count - OR_INT_SIZE; i++)
      {
	*ptr = byte_count % 255;
	ptr++;
      }

    return ptr - start_ptr;
  }

  int stream_handler_read::read_action (const cubstream::stream_position pos, char *ptr, const size_t byte_count,
					size_t *processed_bytes)
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

	for (i = 0; i < to_read; i++)
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

    if (processed_bytes != NULL)
      {
	*processed_bytes = byte_count - byte_count_rem;
      }
    return NO_ERROR;
  }

  void generate_str (char *str, size_t len)
  {
    const char chars[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ+_)(*&^%$#@!";
    size_t i = 0;
    while (i < len)
      {
	str[i] = chars[std::rand () % sizeof (chars)];
	i++;
      }
    str[i] = '\0';
  }

  /* po1 */
  int po1::pack (cubpacking::packer *serializator)
  {
    int res = 0;

    serializator->pack_int (po1::ID);

    serializator->pack_int (i1);
    serializator->pack_short (&sh1);
    serializator->pack_bigint (&b1);
    serializator->pack_int_array (int_a, sizeof (int_a) / sizeof (int_a[0]));
    serializator->pack_int_vector (int_v);
    for (int i = 0; i < sizeof (values) / sizeof (values[0]); i++)
      {
	serializator->pack_db_value (values[i]);
      }
    res = serializator->pack_small_string (small_str);
    assert (res == 0);
    res = serializator->pack_large_string (large_str);
    assert (res == 0);

    res = serializator->pack_string (str1);
    assert (res == 0);

    res = serializator->pack_c_string (str2, strlen (str2));
    assert (res == 0);

    return NO_ERROR;
  }

  int po1::unpack (cubpacking::packer *serializator)
  {
    int cnt;
    int res;

    serializator->unpack_int (&cnt);
    assert (cnt == po1::ID);

    serializator->unpack_int (&i1);
    serializator->unpack_short (&sh1);
    serializator->unpack_bigint (&b1);
    serializator->unpack_int_array (int_a, cnt);
    assert (cnt == sizeof (int_a) / sizeof (int_a[0]));

    serializator->unpack_int_vector (int_v);

    for (int i = 0; i < sizeof (values) / sizeof (values[0]); i++)
      {
	serializator->unpack_db_value (&values[i]);
      }
    res = serializator->unpack_small_string (small_str, sizeof (small_str));
    assert (res == 0);
    res = serializator->unpack_large_string (large_str);
    assert (res == 0);

    res = serializator->unpack_string (str1);
    assert (res == 0);
    res = serializator->unpack_c_string (str2, sizeof (str2));
    assert (res == 0);

    return NO_ERROR;
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
    for (int i = 0; i < sizeof (int_a) / sizeof (int_a[0]); i++)
      {
	if (int_a[i] != other_po1->int_a[i])
	  {
	    return false;
	  }
      }

    for (int i = 0; i < sizeof (values) / sizeof (values[0]); i++)
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

  size_t po1::get_packed_size (cubpacking::packer *serializator)
  {
    size_t entry_size = 0;
    /* ID :*/
    entry_size += serializator->get_packed_int_size (entry_size);

    entry_size += serializator->get_packed_int_size (entry_size);
    entry_size += serializator->get_packed_short_size (entry_size);
    entry_size += serializator->get_packed_bigint_size (entry_size);
    entry_size += serializator->get_packed_int_vector_size (entry_size, sizeof (int_a) / sizeof (int_a[0]));
    entry_size += serializator->get_packed_int_vector_size (entry_size, int_v.size ());
    for (int i = 0; i < sizeof (values) / sizeof (values[0]); i++)
      {
	entry_size += serializator->get_packed_db_value_size (values[i], entry_size);
      }
    entry_size += serializator->get_packed_small_string_size (small_str, entry_size);
    entry_size += serializator->get_packed_large_string_size (large_str, entry_size);

    entry_size += serializator->get_packed_string_size (str1, entry_size);
    entry_size += serializator->get_packed_c_string_size (str2, strlen (str2), entry_size);
    return entry_size;
  }

  void po1::generate_obj (void)
  {
    char *tmp_str;
    size_t str_size;

    i1 = std::rand ();
    sh1 = std::rand ();
    b1 = std::rand ();
    for (int i = 0; i < sizeof (int_a) / sizeof (int_a[0]); i++)
      {
	int_a[i] = std::rand ();
      }
    for (int i = 0; i < sizeof (values) / sizeof (values[0]); i++)
      {
	switch (std::rand () % 5)
	  {
	  case 0:
	    db_make_int (&values[i], std::rand());
	    break;
	  case 1:
	    db_make_short (&values[i], std::rand());
	    break;
	  case 2:
	    db_make_bigint (&values[i], std::rand());
	    break;
	  case 3:
	    db_make_double (&values[i], (double) std::rand() / (std::rand() + 1.0f));
	    break;
	  case 4:
	    str_size = std::rand () % 1000 + 1;
	    tmp_str = new char[str_size + 1];
	    db_make_char (&values[i], str_size, tmp_str, str_size, INTL_CODESET_ISO88591, LANG_COLL_ISO_BINARY);
	    break;
	  }
      }

    generate_str (small_str, sizeof (small_str) - 1);

    str_size = std::rand () % 10000 + 1;
    tmp_str = new char[str_size + 1];
    generate_str (tmp_str, str_size);
    large_str = tmp_str;

    str_size = std::rand () % 10000 + 1;
    tmp_str = new char[str_size + 1];
    generate_str (tmp_str, str_size);
    str1 = tmp_str;

    generate_str (str2, sizeof (str2) - 1);
  }
  /* po2 */
  int po2::pack (cubpacking::packer *serializator)
  {
    int res = 0;

    res = serializator->pack_int (po2::ID);
    assert (res == 0);

    res = serializator->pack_large_string (large_str);
    assert (res == 0);

    return NO_ERROR;
  }

  int po2::unpack (cubpacking::packer *serializator)
  {
    int res;
    int id;

    serializator->unpack_int (&id);
    assert (id == po2::ID);

    res = serializator->unpack_large_string (large_str);
    assert (res == 0);

    return NO_ERROR;
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

  size_t po2::get_packed_size (cubpacking::packer *serializator)
  {
    size_t entry_size = 0;
    /* ID :*/
    entry_size += serializator->get_packed_int_size (entry_size);


    entry_size += serializator->get_packed_large_string_size (large_str, entry_size);

    return entry_size;
  }

  void po2::generate_obj (void)
  {
    char *tmp_str;
    size_t str_size;

    str_size = std::rand () % 10000 + 1;
    tmp_str = new char[str_size + 1];
    generate_str (tmp_str, str_size);
    large_str = tmp_str;
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

    cubstream::packing_stream *my_stream = new cubstream::packing_stream (10 * 1024 * 1024, 100);

    stream_handler_write writer;
    stream_handler_read reader;

    std::cout << "  Testing stream write/read with bytes" << std::endl;

    /* writing in stream */
    for (rem_amount = desired_amount, i = 0; rem_amount > 5; i++)
      {
	int amount = 5 + std::rand () % max_data_size;
	rem_amount -= amount;

	res = my_stream->write (amount, &writer);
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

	amount = MIN (writted_amount - curr_read_pos, amount);

	res = my_stream->read_partial (curr_read_pos, amount, &processed_amount, &reader);
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


  int init_common_cubrid_modules (void)
  {
    int res;
    THREAD_ENTRY *thread_p = NULL;


    lang_init ();
    tp_init ();
    lang_set_charset_lang ("en_US.iso88591");

    cubthread::initialize (thread_p);
    res = cubthread::initialize_thread_entries ();
    if (res != NO_ERROR)
      {
	ASSERT_ERROR ();
	return res;
      }
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


  class stream_mover : public cubstream::read_handler, public cubstream::write_handler
    {
    private:
      static const int BUF_SIZE = 1024;
      char m_buffer[BUF_SIZE];
    public:

      int read_action (const cubstream::stream_position pos, char *ptr, const size_t byte_count)
        {
          memcpy (m_buffer, ptr, byte_count);
          return byte_count;
        };

      int write_action (const cubstream::stream_position pos, char *ptr, const size_t byte_count)
        {
          memcpy (ptr, m_buffer, byte_count);
          return byte_count;
        };

      size_t get_buf_size (void) { return sizeof (m_buffer); };
    };

  int test_stream2 (void)
  {
#define TEST_OBJ_CNT 100

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
    cubstream::packing_stream test_stream_for_pack (10 * 1024 * 1024, 10);

    test_stream_entry se (&test_stream_for_pack);

    for (i = 0; i < TEST_OBJ_CNT; i++)
      {
	se.add_packable_entry (test_objects[i]);
      }
    se.set_packable (true);
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
        res = -1;
      }

    /* create a new stream and copy buffers */
    std::cout << "  Testing packing/unpacking of objects using different streams. Copying stream contents." << std::endl;
    cubstream::packing_stream test_stream_for_unpack (10 * 1024 * 1024, 10);

    cubstream::stream_position last_pos = test_stream_for_pack.get_last_committed_pos ();
    cubstream::stream_position curr_pos;

    stream_mover test_stream_mover;
    size_t copy_chunk_size = test_stream_mover.get_buf_size ();
    int read_bytes, written_bytes;

    for (curr_pos = 0; curr_pos <= last_pos;)
      {
        char buf[1024];

        copy_chunk_size = MIN (copy_chunk_size, last_pos - curr_pos);

        read_bytes = test_stream_for_pack.read (curr_pos, copy_chunk_size, &test_stream_mover);
        if (read_bytes <= 0)
          {
            break;
          }

        written_bytes = test_stream_for_unpack.write (read_bytes, &test_stream_mover);
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
        }

    return res;
  }

}
