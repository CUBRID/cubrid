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

#include "test_packing.hpp"
#include "pinnable_buffer.hpp"
#include "thread_compat.hpp"
#include "thread_manager.hpp"
#include "object_representation.h"
#include "dbtype.h"

namespace test_packing
{

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

  void po1::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_int (i1);
    serializator.pack_short (sh1);
    serializator.pack_bigint (b1);
    serializator.pack_int_array (int_a, sizeof (int_a) / sizeof (int_a[0]));
    serializator.pack_int_vector (int_v);
    for (int i = 0; i < sizeof (values) / sizeof (values[0]); i++)
      {
	serializator.pack_db_value (values[i]);
      }
    serializator.pack_small_string (small_str);
    serializator.pack_large_string (large_str);

    serializator.pack_string (str1);

    serializator.pack_c_string (str2, sizeof (str2) - 1);
  }

  void po1::unpack (cubpacking::unpacker &deserializator)
  {
    int cnt = 0;

    deserializator.unpack_int (i1);
    deserializator.unpack_short (sh1);
    deserializator.unpack_bigint (b1);
    deserializator.unpack_int_array (int_a, cnt);
    assert (cnt == sizeof (int_a) / sizeof (int_a[0]));

    deserializator.unpack_int_vector (int_v);

    for (int i = 0; i < sizeof (values) / sizeof (values[0]); i++)
      {
	deserializator.unpack_db_value (values[i]);
      }
    deserializator.unpack_small_string (small_str, sizeof (small_str));
    deserializator.unpack_large_string (large_str);

    deserializator.unpack_string (str1);
    deserializator.unpack_c_string (str2, sizeof (str2));
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

  size_t po1::get_packed_size (cubpacking::packer &serializator) const
  {
    size_t entry_size = 0;

    entry_size += serializator.get_packed_int_size (entry_size);
    entry_size += serializator.get_packed_short_size (entry_size);
    entry_size += serializator.get_packed_bigint_size (entry_size);
    entry_size += serializator.get_packed_int_vector_size (entry_size, sizeof (int_a) / sizeof (int_a[0]));
    entry_size += serializator.get_packed_int_vector_size (entry_size, (int) int_v.size ());
    for (int i = 0; i < sizeof (values) / sizeof (values[0]); i++)
      {
	entry_size += serializator.get_packed_db_value_size (values[i], int_v.size ());
      }
    entry_size += serializator.get_packed_small_string_size (small_str, entry_size);
    entry_size += serializator.get_packed_large_string_size (large_str, entry_size);

    entry_size += serializator.get_packed_string_size (str1, entry_size);
    entry_size += serializator.get_packed_c_string_size (str2, sizeof (str2), entry_size);
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
	    db_make_char (&values[i], (int) str_size, tmp_str, (int) str_size, INTL_CODESET_ISO88591,
			  LANG_COLL_ISO_BINARY);
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

/////////////////////////////

  void buffer_manager::allocate_bufer (cubmem::pinnable_buffer *&buf, const size_t &amount)
  {
    char *ptr;
    ptr = new char[amount];

    cubmem::pinnable_buffer *new_buf = new cubmem::pinnable_buffer;
    new_buf->init (ptr, amount, this);

    buffers.push_back (new_buf);
    buf = new_buf;
  }

  void buffer_manager::free_storage()
  {
    for (auto it : buffers)
      {
	delete [] (it->get_buffer ());

	unpin (it);

	delete (it);
      }

    assert (check_references () == true);
    unpin_all ();
  }

/////////////////////


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

#define TEST_OBJ_CNT 100
  int test_packing1 (void)
  {
    int res = 0;
    int i;
    size_t obj_size = 0;

    init_common_cubrid_modules ();

    po1 *test_objects = new po1[TEST_OBJ_CNT];
    po1 *test_objects_unpack = new po1[TEST_OBJ_CNT];

    for (i = 0; i < TEST_OBJ_CNT; i++)
      {
	test_objects[i].generate_obj ();
      }

    char *ptr1, *ptr2;
    size_t buf_size = 1024 * 1024;
    ptr1 = new char[buf_size];
    ptr2 = new char[buf_size];
    cubmem::pinnable_buffer buf1 (ptr1, buf_size);
    cubmem::pinnable_buffer buf2 (ptr2, buf_size);

    cubpacking::packer packer_instance { buf1.get_buffer (), buf1.get_buffer_size () };
    cubpacking::unpacker unpacker_instance { buf2.get_buffer (), buf2.get_buffer_size () };

    obj_size = 0;
    for (i = 0; i < TEST_OBJ_CNT; i++)
      {
	obj_size += test_objects[i].get_packed_size (packer_instance);
      }

    //assert (obj_size < buf1.get_buffer_size ());

    for (i = 0; i < TEST_OBJ_CNT; i++)
      {
	test_objects[i].pack (packer_instance);
      }

    memcpy (buf2.get_buffer (), buf1.get_buffer (), buf1.get_buffer_size());

    for (i = 0; i < TEST_OBJ_CNT; i++)
      {
	test_objects_unpack[i].unpack (unpacker_instance);
      }

    for (i = 0; i < TEST_OBJ_CNT; i++)
      {
	if (test_objects_unpack[i].is_equal (&test_objects[i]) == false)
	  {
	    res = -1;
	    assert (false);
	  }
      }

    delete []ptr1;
    delete []ptr2;

    delete []test_objects;
    delete []test_objects_unpack;

    return res;
  }

  int test_packing_buffer1 (void)
  {
    int res = 0;

    buffer_manager bm;
    cubmem::pinnable_buffer *buf[100];

    for (int i = 0; i < 100; i++)
      {
	bm.allocate_bufer (buf[i], 1024);

	memset (buf[i]->get_buffer (), 33, buf[i]->get_buffer_size ());

	assert (buf[i] != NULL);
      }

    bm.unpin (buf[0]);
    bm.unpin (buf[1]);

    bm.pin (buf[0]);

    bm.free_storage ();

    res = (bm.check_references () == true) ? 0 : -1;

    return res;
  }
}
