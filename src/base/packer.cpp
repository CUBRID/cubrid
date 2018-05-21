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
 * packer.cpp
 */

#ident "$Id$"

#include "packer.hpp"
#include "dbtype_def.h"
#include "object_representation.h"

#include <vector>
#include <string>

namespace cubpacking
{
#define MAX_SMALL_STRING_SIZE 255
#define LARGE_STRING_CODE 0xff

  STATIC_INLINE void check_range (const char *ptr, const char *endptr, const size_t amount)
  __attribute__ ((ALWAYS_INLINE));

  STATIC_INLINE void check_range (const char *ptr, const char *endptr, const size_t amount)
  {
    assert (ptr + amount <= endptr);
    if (ptr + amount > endptr)
      {
	abort ();
      }
  }

  packer::packer (char *storage, const size_t amount)
  {
    init (storage, amount);
  }

  int packer::init (char *storage, const size_t amount)
  {
    m_ptr = storage;
    m_end_ptr = storage + amount;

    return NO_ERROR;
  }

  size_t packer::get_packed_int_size (size_t curr_offset)
  {
    return DB_ALIGN (curr_offset, INT_ALIGNMENT) - curr_offset + OR_INT_SIZE;
  }

  int packer::pack_int (const int value)
  {
    align (INT_ALIGNMENT);
    check_range (m_ptr, m_end_ptr, OR_INT_SIZE);

    OR_PUT_INT (m_ptr, value);
    m_ptr += OR_INT_SIZE;

    return NO_ERROR;
  }

  int packer::unpack_int (int *value)
  {
    align (INT_ALIGNMENT);
    check_range (m_ptr, m_end_ptr, OR_INT_SIZE);

    *value = OR_GET_INT (m_ptr);
    m_ptr += OR_INT_SIZE;

    return NO_ERROR;
  }

  int packer::peek_unpack_int (int *value)
  {
    align (INT_ALIGNMENT);
    check_range (m_ptr, m_end_ptr, OR_INT_SIZE);

    *value = OR_GET_INT (m_ptr);

    return NO_ERROR;
  }

  size_t packer::get_packed_short_size (size_t curr_offset)
  {
    return DB_ALIGN (curr_offset, SHORT_ALIGNMENT) - curr_offset + OR_SHORT_SIZE;
  }

  int packer::pack_short (short *value)
  {
    align (SHORT_ALIGNMENT);
    check_range (m_ptr, m_end_ptr, OR_SHORT_SIZE);

    OR_PUT_SHORT (m_ptr, *value);
    m_ptr += OR_SHORT_SIZE;

    return NO_ERROR;
  }

  int packer::unpack_short (short *value)
  {
    align (SHORT_ALIGNMENT);
    check_range (m_ptr, m_end_ptr, OR_SHORT_SIZE);

    *value = OR_GET_SHORT (m_ptr);
    m_ptr += OR_SHORT_SIZE;

    return NO_ERROR;
  }

  size_t packer::get_packed_bigint_size (size_t curr_offset)
  {
    return DB_ALIGN (curr_offset, MAX_ALIGNMENT) - curr_offset + OR_BIGINT_SIZE;
  }

  int packer::pack_bigint (std::int64_t *value)
  {
    align (MAX_ALIGNMENT);
    check_range (m_ptr, m_end_ptr, OR_BIGINT_SIZE);

    OR_PUT_INT64 (m_ptr, value);
    m_ptr += OR_BIGINT_SIZE;

    return NO_ERROR;
  }

  int packer::unpack_bigint (std::int64_t *value)
  {
    align (MAX_ALIGNMENT);
    check_range (m_ptr, m_end_ptr, OR_BIGINT_SIZE);

    OR_GET_INT64 (m_ptr, value);
    m_ptr += OR_BIGINT_SIZE;

    return NO_ERROR;
  }

  int packer::pack_int_array (const int *array, const int count)
  {
    int i;

    align (INT_ALIGNMENT);
    check_range (m_ptr, m_end_ptr, (OR_INT_SIZE * (count + 1)));

    OR_PUT_INT (m_ptr, count);
    m_ptr += OR_INT_SIZE;
    for (i = 0; i < count; i++)
      {
	OR_PUT_INT (m_ptr, array[i]);
	m_ptr += OR_INT_SIZE;
      }

    return NO_ERROR;
  }

  int packer::unpack_int_array (int *array, int &count)
  {
    int i;

    align (INT_ALIGNMENT);
    check_range (m_ptr, m_end_ptr, OR_INT_SIZE);

    count = OR_GET_INT (m_ptr);
    m_ptr += OR_INT_SIZE;

    if (count == 0)
      {
	return NO_ERROR;
      }

    check_range (m_ptr, m_end_ptr, OR_INT_SIZE * count);

    for (i = 0; i < count; i++)
      {
	array[i] = OR_GET_INT (m_ptr);
	m_ptr += OR_INT_SIZE;
      }

    return NO_ERROR;
  }

  size_t packer::get_packed_int_vector_size (size_t curr_offset, const int count)
  {
    return DB_ALIGN (curr_offset, INT_ALIGNMENT) - curr_offset + (OR_INT_SIZE * (count + 1));
  }

  int packer::pack_int_vector (const std::vector<int> &array)
  {
    const int count = (const int) array.size ();
    int i;

    align (INT_ALIGNMENT);
    check_range (m_ptr, m_end_ptr, (OR_INT_SIZE * (count + 1)));

    OR_PUT_INT (m_ptr, count);
    m_ptr += OR_INT_SIZE;
    for (i = 0; i < count; i++)
      {
	OR_PUT_INT (m_ptr, array[i]);
	m_ptr += OR_INT_SIZE;
      }

    return NO_ERROR;
  }

  int packer::unpack_int_vector (std::vector<int> &array)
  {
    int i;
    int count;

    align (INT_ALIGNMENT);
    check_range (m_ptr, m_end_ptr, OR_INT_SIZE);

    count = OR_GET_INT (m_ptr);
    m_ptr += OR_INT_SIZE;

    check_range (m_ptr, m_end_ptr, OR_INT_SIZE * count);

    for (i = 0; i < count; i++)
      {
	array.push_back (OR_GET_INT (m_ptr));
	m_ptr += OR_INT_SIZE;
      }

    return NO_ERROR;
  }

  size_t packer::get_packed_db_value_size (const DB_VALUE &value, size_t curr_offset)
  {
    size_t aligned_offset = DB_ALIGN (curr_offset, MAX_ALIGNMENT);
    size_t unaligned_size = or_packed_value_size ((DB_VALUE *) &value, 1, 1, 0);
    size_t aligned_size = DB_ALIGN (unaligned_size, MAX_ALIGNMENT);
    return aligned_size + aligned_offset - curr_offset;
  }

  int packer::pack_db_value (const DB_VALUE &value)
  {
    char *old_ptr;

    size_t value_size = or_packed_value_size ((DB_VALUE *) &value, 1, 1, 0);

    align (MAX_ALIGNMENT);
    check_range (m_ptr, m_end_ptr, value_size);
    old_ptr = m_ptr;

    m_ptr = or_pack_value (m_ptr, (DB_VALUE *) &value);
    assert (old_ptr + value_size == m_ptr);

    check_range (m_ptr, m_end_ptr, 0);

    return NO_ERROR;
  }

  int packer::unpack_db_value (DB_VALUE *value)
  {
    char *old_ptr;

    align (MAX_ALIGNMENT);
    old_ptr = m_ptr;
    m_ptr = or_unpack_value (m_ptr, (DB_VALUE *) value);

    size_t value_size = or_packed_value_size (value, 1, 1, 0);
    assert (old_ptr + value_size == m_ptr);

    check_range (m_ptr, m_end_ptr, 0);

    return NO_ERROR;
  }

  size_t packer::get_packed_small_string_size (const char *string, const size_t curr_offset)
  {
    size_t entry_size;

    entry_size = OR_BYTE_SIZE + strlen (string);

    return DB_ALIGN (curr_offset + entry_size, INT_ALIGNMENT) - curr_offset;
  }

  int packer::pack_small_string (const char *string)
  {
    size_t len;

    len = strlen (string);

    if (len > MAX_SMALL_STRING_SIZE)
      {
	return ER_FAILED;
      }

    check_range (m_ptr, m_end_ptr, len + 1);

    OR_PUT_BYTE (m_ptr, len);
    m_ptr += OR_BYTE_SIZE;
    if (len > 0)
      {
	(void) memcpy (m_ptr, string, len);
	m_ptr += len;
      }

    align (INT_ALIGNMENT);

    return NO_ERROR;
  }

  int packer::unpack_small_string (char *string, const size_t max_size)
  {
    size_t len;

    check_range (m_ptr, m_end_ptr, OR_BYTE_SIZE);

    len = OR_GET_BYTE (m_ptr);
    if (len > max_size)
      {
	return ER_FAILED;
      }

    m_ptr += OR_BYTE_SIZE;

    check_range (m_ptr, m_end_ptr, len);
    if (len > 0)
      {
	(void) memcpy (string, m_ptr, len);
	string[len] = '\0';
	m_ptr += len;
      }
    else
      {
	*string = '\0';
      }

    align (INT_ALIGNMENT);

    return NO_ERROR;
  }


  size_t packer::get_packed_large_string_size (const std::string &str, const size_t curr_offset)
  {
    size_t entry_size;

    entry_size = OR_INT_SIZE + str.size ();

    return DB_ALIGN (curr_offset + entry_size, INT_ALIGNMENT) - curr_offset;
  }

  int packer::pack_large_string (const std::string &str)
  {
    size_t len;

    len = str.size ();

    align (INT_ALIGNMENT);
    check_range (m_ptr, m_end_ptr, len + OR_INT_SIZE);

    OR_PUT_INT (m_ptr, len);
    m_ptr += OR_INT_SIZE;

    (void) memcpy (m_ptr, str.c_str (), len);
    m_ptr += len;

    align (INT_ALIGNMENT);

    return NO_ERROR;
  }

  int packer::unpack_large_string (std::string &str)
  {
    size_t len;

    align (INT_ALIGNMENT);

    check_range (m_ptr, m_end_ptr, OR_INT_SIZE);

    len = OR_GET_INT (m_ptr);
    m_ptr += OR_INT_SIZE;

    if (len > 0)
      {
	check_range (m_ptr, m_end_ptr, len);
	str = std::string ((const char *) m_ptr, len);
	m_ptr += len;
      }

    align (INT_ALIGNMENT);

    return NO_ERROR;
  }

  size_t packer::get_packed_string_size (const std::string &str, const size_t curr_offset)
  {
    return get_packed_c_string_size (str.c_str (), str.size (), curr_offset);
  }

  int packer::pack_string (const std::string &str)
  {
    size_t len = str.size ();

    return pack_c_string (str.c_str (), len);
  }

  int packer::unpack_string (std::string &str)
  {
    size_t len;

    check_range (m_ptr, m_end_ptr, 1);

    len = OR_GET_BYTE (m_ptr);

    if (len == LARGE_STRING_CODE)
      {
	m_ptr++;
	return unpack_large_string (str);
      }
    else
      {
	m_ptr++;

	str = std::string ((const char *) m_ptr, len);
	m_ptr += len;

	align (INT_ALIGNMENT);

	return NO_ERROR;
      }
  }

  size_t packer::get_packed_c_string_size (const char *str, const size_t str_size, const size_t curr_offset)
  {
    size_t entry_size;

    if (str_size > MAX_SMALL_STRING_SIZE)
      {
	entry_size = OR_BYTE_SIZE + str_size;
      }
    else
      {
	entry_size = OR_BYTE_SIZE + OR_INT_SIZE + str_size;
      }

    return DB_ALIGN (curr_offset + entry_size, INT_ALIGNMENT) - curr_offset;
  }

  int packer::pack_c_string (const char *str, const size_t str_size)
  {
    if (str_size < MAX_SMALL_STRING_SIZE)
      {
	return pack_small_string (str);
      }
    else
      {
	check_range (m_ptr, m_end_ptr, str_size + 1 + OR_INT_SIZE);

	OR_PUT_BYTE (m_ptr, LARGE_STRING_CODE);
	m_ptr++;

	return pack_large_string (str);
      }
  }

  int packer::unpack_c_string (char *str, const size_t max_str_size)
  {
    size_t len;

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

    if (len >= max_str_size)
      {
	return ER_FAILED;
      }
    if (len > 0)
      {
	check_range (m_ptr, m_end_ptr, len);
	memcpy (str, m_ptr, len);
	m_ptr += len;
      }

    str[len] = '\0';

    align (INT_ALIGNMENT);

    return NO_ERROR;
  }


} /* namespace cubpacking */
