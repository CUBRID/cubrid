/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

/*
 * packer.cpp
 */

#ident "$Id$"

#include "packer.hpp"

#include "dbtype_def.h"
#include "memory_alloc.h"
#include "object_representation.h"
#include "packable_object.hpp"

#include <algorithm>
#include <cstring>
#include <vector>
#include <string>
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubpacking
{
#define MAX_SMALL_STRING_SIZE 255
#define LARGE_STRING_CODE 0xff

  //
  // static function declarations
  //
  static void check_range (const char *ptr, const char *endptr, const size_t amount);

  //
  // static function definitions
  static void
  check_range (const char *ptr, const char *endptr, const size_t amount)
  {
    assert (ptr + amount <= endptr);
    if (ptr + amount > endptr)
      {
	er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_INTERFACE_NOT_ENOUGH_DATA_SIZE, 0);
	abort ();
      }
  }

  //
  // packer
  //

  packer::packer (void)
  {
    // all pointers are initialized to NULL
  }

  packer::packer (char *storage, const size_t amount)
  {
    set_buffer (storage, amount);
  }

  void
  packer::set_buffer (char *storage, const size_t amount)
  {
    m_start_ptr = storage;
    m_ptr = storage;
    m_end_ptr = m_start_ptr + amount;
  }

  unpacker::unpacker (const char *storage, const size_t amount)
  {
    set_buffer (storage, amount);
  }

  unpacker::unpacker (const cubmem::block &blk)
  {
    set_buffer (blk.ptr, blk.dim);
  }

  void
  unpacker::set_buffer (const char *storage, const size_t amount)
  {
    m_start_ptr = storage;
    m_ptr = storage;
    m_end_ptr = m_start_ptr + amount;
  }

  size_t
  packer::get_packed_int_size (size_t curr_offset)
  {
    return DB_ALIGN (curr_offset, INT_ALIGNMENT) - curr_offset + OR_INT_SIZE;
  }

  void
  packer::pack_int (const int value)
  {
    align (INT_ALIGNMENT);
    check_range (m_ptr, m_end_ptr, OR_INT_SIZE);

    OR_PUT_INT (m_ptr, value);
    m_ptr += OR_INT_SIZE;
  }

  size_t
  packer::get_packed_size_overloaded (int value, size_t curr_offset)
  {
    return get_packed_int_size (curr_offset);
  }

  void
  packer::pack_overloaded (int value)
  {
    pack_int (value);
  }

  void
  unpacker::unpack_int (int &value)
  {
    align (INT_ALIGNMENT);
    check_range (m_ptr, m_end_ptr, OR_INT_SIZE);

    value = OR_GET_INT (m_ptr);
    m_ptr += OR_INT_SIZE;
  }

  void
  unpacker::unpack_overloaded (int &value)
  {
    unpack_int (value);
  }

  void
  unpacker::peek_unpack_int (int &value)
  {
    align (INT_ALIGNMENT);
    check_range (m_ptr, m_end_ptr, OR_INT_SIZE);

    value = OR_GET_INT (m_ptr);
  }

  size_t
  packer::get_packed_bool_size (size_t curr_offset)
  {
    return get_packed_int_size (curr_offset);
  }

  void
  packer::pack_bool (bool value)
  {
    pack_int (value ? 1 : 0);
  }

  size_t
  packer::get_packed_size_overloaded (bool value, size_t curr_offset)
  {
    return get_packed_bool_size (curr_offset);
  }

  void
  packer::pack_overloaded (bool value)
  {
    pack_bool (value);
  }

  void
  unpacker::unpack_bool (bool &value)
  {
    int int_val;
    unpack_int (int_val);
    assert (int_val == 1 || int_val == 0);
    value = int_val != 0;
  }

  void
  unpacker::unpack_overloaded (bool &value)
  {
    unpack_bool (value);
  }

  size_t
  packer::get_packed_short_size (size_t curr_offset)
  {
    return DB_ALIGN (curr_offset, SHORT_ALIGNMENT) - curr_offset + OR_SHORT_SIZE;
  }

  void
  packer::pack_short (const short value)
  {
    align (SHORT_ALIGNMENT);
    check_range (m_ptr, m_end_ptr, OR_SHORT_SIZE);

    OR_PUT_SHORT (m_ptr, value);
    m_ptr += OR_SHORT_SIZE;
  }

  size_t
  packer::get_packed_size_overloaded (short value, size_t curr_offset)
  {
    return get_packed_short_size (curr_offset);
  }

  void
  packer::pack_overloaded (short value)
  {
    pack_short (value);
  }

  void
  unpacker::unpack_short (short &value)
  {
    align (SHORT_ALIGNMENT);
    check_range (m_ptr, m_end_ptr, OR_SHORT_SIZE);

    value = OR_GET_SHORT (m_ptr);
    m_ptr += OR_SHORT_SIZE;
  }

  void
  unpacker::unpack_overloaded (short &value)
  {
    unpack_short (value);
  }

  size_t
  packer::get_packed_bigint_size (size_t curr_offset)
  {
    return DB_ALIGN (curr_offset, MAX_ALIGNMENT) - curr_offset + OR_BIGINT_SIZE;
  }

  void
  packer::pack_bigint (const std::int64_t &value)
  {
    align (MAX_ALIGNMENT);
    check_range (m_ptr, m_end_ptr, OR_BIGINT_SIZE);

    OR_PUT_INT64 (m_ptr, &value);
    m_ptr += OR_BIGINT_SIZE;
  }

  void
  unpacker::unpack_bigint (std::int64_t &value)
  {
    align (MAX_ALIGNMENT);
    check_range (m_ptr, m_end_ptr, OR_BIGINT_SIZE);

    OR_GET_INT64 (m_ptr, &value);
    m_ptr += OR_BIGINT_SIZE;
  }

  void
  packer::pack_bigint (const std::uint64_t &value)
  {
    align (MAX_ALIGNMENT);
    check_range (m_ptr, m_end_ptr, OR_BIGINT_SIZE);

    OR_PUT_INT64 (m_ptr, &value);
    m_ptr += OR_BIGINT_SIZE;
  }

  size_t
  packer::get_packed_size_overloaded (const std::int64_t &value, size_t curr_offset)
  {
    return get_packed_bigint_size (curr_offset);
  }

  size_t
  packer::get_packed_size_overloaded (const std::uint64_t &value, size_t curr_offset)
  {
    return get_packed_bigint_size (curr_offset);
  }

  void
  packer::pack_overloaded (const std::int64_t &value)
  {
    pack_bigint (value);
  }

  void
  packer::pack_overloaded (const std::uint64_t &value)
  {
    pack_bigint (value);
  }

  void
  unpacker::unpack_bigint (std::uint64_t &value)
  {
    align (MAX_ALIGNMENT);
    check_range (m_ptr, m_end_ptr, OR_BIGINT_SIZE);

    OR_GET_INT64 (m_ptr, &value);
    m_ptr += OR_BIGINT_SIZE;
  }

  void
  unpacker::unpack_overloaded (std::int64_t &value)
  {
    unpack_bigint (value);
  }

  void
  unpacker::unpack_overloaded (std::uint64_t &value)
  {
    unpack_bigint (value);
  }

  void
  packer::pack_int_array (const int *array, const int count)
  {
    align (INT_ALIGNMENT);
    check_range (m_ptr, m_end_ptr, (OR_INT_SIZE * (count + 1)));

    OR_PUT_INT (m_ptr, count);
    m_ptr += OR_INT_SIZE;
    for (int i = 0; i < count; i++)
      {
	OR_PUT_INT (m_ptr, array[i]);
	m_ptr += OR_INT_SIZE;
      }
  }

  void
  unpacker::unpack_int_array (int *array, int &count)
  {
    align (INT_ALIGNMENT);
    check_range (m_ptr, m_end_ptr, OR_INT_SIZE);

    count = OR_GET_INT (m_ptr);
    m_ptr += OR_INT_SIZE;

    if (count == 0)
      {
	return;
      }

    check_range (m_ptr, m_end_ptr, OR_INT_SIZE * count);

    for (int i = 0; i < count; i++)
      {
	array[i] = OR_GET_INT (m_ptr);
	m_ptr += OR_INT_SIZE;
      }
  }

  size_t
  packer::get_packed_int_vector_size (size_t curr_offset, const size_t count)
  {
    return DB_ALIGN (curr_offset, INT_ALIGNMENT) - curr_offset + (OR_INT_SIZE * (count + 1));
  }

  void
  packer::pack_int_vector (const std::vector<int> &array)
  {
    const size_t count = array.size ();

    align (INT_ALIGNMENT);
    check_range (m_ptr, m_end_ptr, (OR_INT_SIZE * (count + 1)));

    OR_PUT_INT (m_ptr, count);
    m_ptr += OR_INT_SIZE;
    for (size_t i = 0; i < count; ++i)
      {
	OR_PUT_INT (m_ptr, array[i]);
	m_ptr += OR_INT_SIZE;
      }
  }

  void
  unpacker::unpack_int_vector (std::vector<int> &array)
  {
    int count;

    align (INT_ALIGNMENT);
    check_range (m_ptr, m_end_ptr, OR_INT_SIZE);

    count = OR_GET_INT (m_ptr);
    m_ptr += OR_INT_SIZE;

    check_range (m_ptr, m_end_ptr, OR_INT_SIZE * count);

    for (int i = 0; i < count; i++)
      {
	array.push_back (OR_GET_INT (m_ptr));
	m_ptr += OR_INT_SIZE;
      }
  }

  size_t
  packer::get_packed_db_value_size (const db_value &value, size_t curr_offset)
  {
    size_t aligned_offset = DB_ALIGN (curr_offset, MAX_ALIGNMENT);
    size_t unaligned_size = or_packed_value_size (&value, 1, 1, 0);
    size_t aligned_size = unaligned_size;
    return aligned_size + aligned_offset - curr_offset;
  }

  void
  packer::pack_db_value (const db_value &value)
  {
    size_t value_size = or_packed_value_size (&value, 1, 1, 0);

    align (MAX_ALIGNMENT);
    check_range (m_ptr, m_end_ptr, value_size);

    OR_BUF orbuf;
    delegate_to_or_buf (value_size, orbuf);
    or_put_value (&orbuf, (db_value *) &value, 1, 1, 0);

    check_range (m_ptr, m_end_ptr, 0);
  }

  size_t
  packer::get_packed_size_overloaded (const db_value &value, size_t curr_offset)
  {
    return get_packed_db_value_size (value, curr_offset);
  }

  void
  packer::pack_overloaded (const db_value &value)
  {
    pack_db_value (value);
  }

  void
  unpacker::unpack_db_value (db_value &value)
  {
    const char *old_ptr;

    align (MAX_ALIGNMENT);
    old_ptr = m_ptr;
    m_ptr = or_unpack_value (m_ptr, &value);

    size_t value_size = or_packed_value_size (&value, 1, 1, 0);
    assert (old_ptr + value_size == m_ptr);

    check_range (m_ptr, m_end_ptr, 0);
  }

  void
  unpacker::unpack_overloaded (db_value &value)
  {
    unpack_db_value (value);
  }

  size_t
  packer::get_packed_small_string_size (const char *string, const size_t curr_offset)
  {
    size_t entry_size;

    entry_size = OR_BYTE_SIZE + strlen (string);

    return DB_ALIGN (curr_offset + entry_size, INT_ALIGNMENT) - curr_offset;
  }

  void
  packer::pack_small_string (const char *string, const size_t str_size)
  {
    size_t len;

    if (str_size == 0)
      {
	len = strlen (string);
      }
    else
      {
	len = str_size;
      }

    if (len > MAX_SMALL_STRING_SIZE)
      {
	assert (false);
	pack_c_string (string, len);
	return;
      }

    check_range (m_ptr, m_end_ptr, len + 1);

    OR_PUT_BYTE (m_ptr, len);
    m_ptr += OR_BYTE_SIZE;
    if (len > 0)
      {
	std::memcpy (m_ptr, string, len);
	m_ptr += len;
      }

    align (INT_ALIGNMENT);
  }

  void
  unpacker::unpack_small_string (char *string, const size_t max_size)
  {
    size_t len;

    check_range (m_ptr, m_end_ptr, OR_BYTE_SIZE);

    len = OR_GET_BYTE (m_ptr);
    if (len > max_size)
      {
	assert (false);
	return;
      }

    m_ptr += OR_BYTE_SIZE;

    check_range (m_ptr, m_end_ptr, len);
    if (len > 0)
      {
	std::memcpy (string, m_ptr, len);
	string[len] = '\0';
	m_ptr += len;
      }
    else
      {
	*string = '\0';
      }

    align (INT_ALIGNMENT);
  }


  size_t
  packer::get_packed_large_string_size (const std::string &str, const size_t curr_offset)
  {
    size_t entry_size;

    entry_size = OR_INT_SIZE + str.size ();

    return DB_ALIGN (curr_offset + entry_size, INT_ALIGNMENT) - curr_offset;
  }

  void
  packer::pack_large_c_string (const char *string, const size_t str_size)
  {
    size_t len;

    if (str_size == 0)
      {
	len = strlen (string);
      }
    else
      {
	len = str_size;
      }

    align (INT_ALIGNMENT);
    check_range (m_ptr, m_end_ptr, len + OR_INT_SIZE);

    OR_PUT_INT (m_ptr, len);
    m_ptr += OR_INT_SIZE;

    std::memcpy (m_ptr, string, len);
    m_ptr += len;

    align (INT_ALIGNMENT);
  }

  void
  packer::pack_large_string (const std::string &str)
  {
    pack_large_c_string (str.c_str (), str.size ());
  }

  void
  unpacker::unpack_large_string (std::string &str)
  {
    size_t len;

    align (INT_ALIGNMENT);

    check_range (m_ptr, m_end_ptr, OR_INT_SIZE);

    len = OR_GET_INT (m_ptr);
    m_ptr += OR_INT_SIZE;

    if (len > 0)
      {
	check_range (m_ptr, m_end_ptr, len);
	str = std::string (m_ptr, len);
	m_ptr += len;
      }

    align (INT_ALIGNMENT);
  }

  size_t
  packer::get_packed_string_size (const std::string &str, const size_t curr_offset)
  {
    return get_packed_c_string_size (str.c_str (), str.size (), curr_offset);
  }

  void
  packer::pack_string (const std::string &str)
  {
    size_t len = str.size ();

    pack_c_string (str.c_str (), len);
  }

  size_t
  packer::get_packed_size_overloaded (const std::string &value, size_t curr_offset)
  {
    return get_packed_string_size (value, curr_offset);
  }

  void
  packer::pack_overloaded (const std::string &str)
  {
    pack_string (str);
  }

  void
  unpacker::unpack_string (std::string &str)
  {
    size_t len;

    check_range (m_ptr, m_end_ptr, 1);

    len = OR_GET_BYTE (m_ptr);

    if (len == LARGE_STRING_CODE)
      {
	m_ptr++;
	unpack_large_string (str);
      }
    else
      {
	m_ptr++;

	str = std::string (m_ptr, len);
	m_ptr += len;

	align (INT_ALIGNMENT);
      }
  }

  void
  unpacker::unpack_overloaded (std::string &str)
  {
    return unpack_string (str);
  }

  size_t
  packer::get_packed_c_string_size (const char *str, const size_t str_size, const size_t curr_offset)
  {
    size_t entry_size;

    if (str_size < MAX_SMALL_STRING_SIZE)
      {
	entry_size = OR_BYTE_SIZE + str_size;
      }
    else
      {
	entry_size = DB_ALIGN (OR_BYTE_SIZE, INT_ALIGNMENT) + OR_INT_SIZE + str_size;
      }

    return DB_ALIGN (curr_offset + entry_size, INT_ALIGNMENT) - curr_offset;
  }

  void
  packer::pack_c_string (const char *str, const size_t str_size)
  {
    if (str_size < MAX_SMALL_STRING_SIZE)
      {
	pack_small_string (str, str_size);
      }
    else
      {
	check_range (m_ptr, m_end_ptr, str_size + 1 + OR_INT_SIZE);

	OR_PUT_BYTE (m_ptr, LARGE_STRING_CODE);
	m_ptr++;

	pack_large_c_string (str, str_size);
      }
  }

  void
  unpacker::unpack_string_size (size_t &len)
  {
    check_range (m_ptr, m_end_ptr, 1);
    len = OR_GET_BYTE (m_ptr);
    if (len == LARGE_STRING_CODE)
      {
	m_ptr++;

	align (OR_INT_SIZE);

	len = OR_GET_INT (m_ptr);
	m_ptr += OR_INT_SIZE;
      }
    else
      {
	m_ptr++;
      }
    if (len > 0)
      {
	check_range (m_ptr, m_end_ptr, len);
      }
  }

  void
  unpacker::unpack_c_string (char *str, const size_t max_str_size)
  {
    size_t len = 0;

    unpack_string_size (len);

    if (len >= max_str_size)
      {
	assert (false);
	return;
      }
    if (len > 0)
      {
	std::memcpy (str, m_ptr, len);
	m_ptr += len;
      }

    str[len] = '\0';

    align (INT_ALIGNMENT);
  }

  void
  unpacker::unpack_string_to_memblock (cubmem::extensible_block &blk)
  {
    size_t len;
    unpack_string_size (len);

    // make sure memory size is enough
    blk.extend_to (len + 1);

    if (len > 0)
      {
	std::memcpy (blk.get_ptr (), m_ptr, len);
	m_ptr += len;
      }
    blk.get_ptr ()[len] = '\0';

    align (INT_ALIGNMENT);
  }

  size_t
  packer::get_packed_size_overloaded (const packable_object &po, size_t curr_offset)
  {
    // align first
    size_t aligned_offset = DB_ALIGN (curr_offset, MAX_ALIGNMENT);
    return po.get_packed_size (*this) + aligned_offset - curr_offset;
  }

  void
  packer::pack_overloaded (const packable_object &po)
  {
    po.pack (*this);
  }

  void
  cubpacking::unpacker::unpack_overloaded (packable_object &po)
  {
    po.unpack (*this);
  }

  size_t
  packer::get_packed_oid_size (const size_t curr_offset)
  {
    return DB_ALIGN (curr_offset + OR_OID_SIZE, INT_ALIGNMENT) - curr_offset;
  }

  void
  packer::pack_oid (const OID &oid)
  {
    align (INT_ALIGNMENT);
    check_range (m_ptr, m_end_ptr, OR_OID_SIZE);

    OR_PUT_OID (m_ptr, &oid);
    m_ptr += OR_OID_SIZE;
  }

  size_t
  packer::get_packed_size_overloaded (const OID &oid, size_t curr_offset)
  {
    return get_packed_oid_size (curr_offset);
  }

  void
  packer::pack_overloaded (const OID &oid)
  {
    pack_oid (oid);
  }

  void
  unpacker::unpack_oid (OID &oid)
  {
    align (INT_ALIGNMENT);
    check_range (m_ptr, m_end_ptr, OR_OID_SIZE);

    OR_GET_OID (m_ptr, &oid);
    m_ptr += OR_OID_SIZE;
  }

  void
  unpacker::unpack_overloaded (OID &oid)
  {
    return unpack_oid (oid);
  }

  void unpacker::peek_unpack_block_length (int &value)
  {
    return peek_unpack_int (value);
  }

  size_t
  packer::get_packed_size_overloaded (const cubmem::block &blk, size_t curr_offset)
  {
    return get_packed_buffer_size (blk.ptr, blk.dim, curr_offset);
  }

  void
  packer::pack_overloaded (const cubmem::block &blk)
  {
    pack_buffer_with_length (blk.ptr, blk.dim);
  }

  void
  unpacker::unpack_overloaded (cubmem::block &blk)
  {
    return unpack_buffer_with_length (blk.ptr, blk.dim);
  }

  const char *
  unpacker::get_curr_ptr (void)
  {
    return m_ptr;
  }

  void
  unpacker::align (const size_t req_alignment)
  {
    m_ptr = PTR_ALIGN (m_ptr, req_alignment);
  }

  size_t
  unpacker::get_current_size (void)
  {
    return get_curr_ptr () - get_buffer_start ();
  }

  const char *
  unpacker::get_buffer_start (void)
  {
    return m_start_ptr;
  }

  const char *
  unpacker::get_buffer_end (void)
  {
    return m_end_ptr;
  }

  bool
  unpacker::is_ended (void)
  {
    return get_curr_ptr () == get_buffer_end ();
  }

  size_t
  packer::get_packed_buffer_size (const char *stream, const size_t length, const size_t curr_offset) const
  {
    size_t actual_length = 0;

    if (stream != NULL)
      {
	actual_length = length;
      }

    size_t entry_size = OR_INT_SIZE + actual_length;

    return DB_ALIGN (curr_offset, INT_ALIGNMENT) + entry_size - curr_offset;
  }

  void
  packer::pack_buffer_with_length (const char *stream, const size_t length)
  {
    align (INT_ALIGNMENT);

    check_range (m_ptr, m_end_ptr, length + OR_INT_SIZE);

    OR_PUT_INT (m_ptr, length);
    m_ptr += OR_INT_SIZE;

    if (length > 0)
      {
	std::memcpy (m_ptr, stream, length);
	m_ptr += length;

	align (INT_ALIGNMENT);
      }
  }

  void unpacker::peek_unpack_buffer_length (int &value)
  {
    return peek_unpack_int (value);
  }

  /*
   * unpack_buffer_with_length : unpacks a stream into a preallocated buffer
   * stream (in/out) : output stream
   * max_length (in) : maximum length to unpack
   *
   * Note : the unpacker pointer is incremented with the actual length of buffer (found in unpacker)
   */
  void
  unpacker::unpack_buffer_with_length (char *stream, const size_t max_length)
  {
    size_t actual_len, copy_length;

    align (INT_ALIGNMENT);

    actual_len = OR_GET_INT (m_ptr);
    m_ptr += OR_INT_SIZE;

    assert (actual_len <= max_length);
    copy_length = std::min (actual_len, max_length);

    check_range (m_ptr, m_end_ptr, actual_len);

    if (copy_length > 0)
      {
	memcpy (stream, m_ptr, copy_length);
      }

    m_ptr += actual_len;
    align (INT_ALIGNMENT);
  }


  void
  packer::delegate_to_or_buf (const size_t size, or_buf &buf)
  {
    check_range (m_ptr, m_end_ptr, size);
    or_init (&buf, m_ptr, size);
    m_ptr += size;
  }

  const char *
  packer::get_curr_ptr (void)
  {
    return m_ptr;
  }

  size_t
  packer::get_current_size (void)
  {
    return get_curr_ptr () - get_buffer_start ();
  }

  void
  packer::align (const size_t req_alignment)
  {
    m_ptr = PTR_ALIGN (m_ptr, req_alignment);
  }

  const char *
  packer::get_buffer_start (void)
  {
    return m_start_ptr;
  }

  const char *
  packer::get_buffer_end (void)
  {
    return m_end_ptr;
  }

  bool
  packer::is_ended (void)
  {
    return get_curr_ptr () == get_buffer_end ();
  }

  void
  unpacker::delegate_to_or_buf (const size_t size, or_buf &buf)
  {
    check_range (m_ptr, m_end_ptr, size);
    // promise you won't write on it!
    or_init (&buf, const_cast <char *> (m_ptr), size);
    m_ptr += size;
  }

} /* namespace cubpacking */
