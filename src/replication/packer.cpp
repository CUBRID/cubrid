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

#include "packing_common.hpp"
#include "packer.hpp"
#include "packing_buffer.hpp"
#include "object_representation.h"

#include <vector>
#include <string>

#define CHECK_RANGE(ptr, endptr, amount) \
  do \
    { \
      assert ((ptr) + (amount) <= (endptr)); \
      if ((ptr) + (amount) > (endptr)) \
        { \
          throw ("serialization range not properly initialized"); \
        } \
    }\
  while (0);

/* TODO[arnia] : error codes and mesages for ER_FAILED return codes */

packer::packer (BUFFER_UNIT *storage, const size_t amount)
{
  init (storage, amount);
}

int packer::init (BUFFER_UNIT *storage, const size_t amount)
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
  m_ptr = (BUFFER_UNIT *) PTR_ALIGN (m_ptr, INT_ALIGNMENT);
  CHECK_RANGE (m_ptr, m_end_ptr, OR_INT_SIZE);
   
  OR_PUT_INT (m_ptr, value);
  m_ptr += OR_INT_SIZE;
  return NO_ERROR;
}

int packer::unpack_int (int *value)
{
  m_ptr = (BUFFER_UNIT *) PTR_ALIGN (m_ptr, INT_ALIGNMENT);
  CHECK_RANGE (m_ptr, m_end_ptr, OR_INT_SIZE);

  *value = OR_GET_INT (m_ptr);
  m_ptr += OR_INT_SIZE;

  return NO_ERROR;
}

int packer::peek_unpack_int (int *value)
{
  m_ptr = (BUFFER_UNIT *) PTR_ALIGN (m_ptr, INT_ALIGNMENT);
  CHECK_RANGE (m_ptr, m_end_ptr, OR_INT_SIZE);

  *value = OR_GET_INT (m_ptr);

  return NO_ERROR;
}


size_t packer::get_packed_short_size (size_t curr_offset)
{
  return DB_ALIGN (curr_offset, SHORT_ALIGNMENT) - curr_offset + OR_SHORT_SIZE;
}

int packer::pack_short (short *value)
{
  m_ptr = (BUFFER_UNIT *) PTR_ALIGN (m_ptr, SHORT_ALIGNMENT);
  CHECK_RANGE (m_ptr, m_end_ptr, OR_SHORT_SIZE);
   
  OR_PUT_SHORT (m_ptr, *value);
  m_ptr += OR_SHORT_SIZE;
  return NO_ERROR;
}

int packer::unpack_short (short *value)
{
  m_ptr = (BUFFER_UNIT *) PTR_ALIGN (m_ptr, SHORT_ALIGNMENT);
  CHECK_RANGE (m_ptr, m_end_ptr, OR_SHORT_SIZE);

  *value = OR_GET_SHORT (m_ptr);
  m_ptr += OR_SHORT_SIZE;

  return NO_ERROR;
}

size_t packer::get_packed_bigint_size (size_t curr_offset)
{
  return DB_ALIGN (curr_offset, MAX_ALIGNMENT) - curr_offset + OR_BIGINT_SIZE;
}

int packer::pack_bigint (DB_BIGINT *value)
{
  m_ptr = (BUFFER_UNIT *) PTR_ALIGN (m_ptr, MAX_ALIGNMENT);
  CHECK_RANGE (m_ptr, m_end_ptr, OR_BIGINT_SIZE);
   
  OR_PUT_BIGINT (m_ptr, value);
  m_ptr += OR_BIGINT_SIZE;
  return NO_ERROR;
}

int packer::unpack_bigint (DB_BIGINT *value)
{
  m_ptr = (BUFFER_UNIT *) PTR_ALIGN (m_ptr, MAX_ALIGNMENT);
  CHECK_RANGE (m_ptr, m_end_ptr, OR_BIGINT_SIZE);

  OR_GET_BIGINT (m_ptr, value);
  m_ptr += OR_BIGINT_SIZE;

  return NO_ERROR;
}

int packer::pack_int_array (const int *array, const int count)
{
  int i;

  m_ptr = (BUFFER_UNIT *) PTR_ALIGN (m_ptr, INT_ALIGNMENT);
  CHECK_RANGE (m_ptr, m_end_ptr, (OR_INT_SIZE * (count + 1)));
  
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

  m_ptr = (BUFFER_UNIT *) PTR_ALIGN (m_ptr, INT_ALIGNMENT);
  CHECK_RANGE (m_ptr, m_end_ptr, OR_INT_SIZE);

  count = OR_GET_INT (m_ptr);
  m_ptr += OR_INT_SIZE;

  if (count == 0)
    {
      return NO_ERROR;
    }

  CHECK_RANGE (m_ptr, m_end_ptr, OR_INT_SIZE * count);

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
  const int count = (const int) array.size();
  int i;

  m_ptr = (BUFFER_UNIT *) PTR_ALIGN (m_ptr, INT_ALIGNMENT);
  CHECK_RANGE (m_ptr, m_end_ptr, (OR_INT_SIZE * (count + 1)));

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

  m_ptr = (BUFFER_UNIT *) PTR_ALIGN (m_ptr, INT_ALIGNMENT);
  CHECK_RANGE (m_ptr, m_end_ptr, OR_INT_SIZE);

  count = OR_GET_INT (m_ptr);
  m_ptr += OR_INT_SIZE;

  CHECK_RANGE (m_ptr, m_end_ptr, OR_INT_SIZE * count);

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
  BUFFER_UNIT *old_ptr;

  size_t value_size = or_packed_value_size ((DB_VALUE *)&value, 1, 1, 0);

  m_ptr = (BUFFER_UNIT *) PTR_ALIGN (m_ptr, MAX_ALIGNMENT);
  CHECK_RANGE (m_ptr, m_end_ptr, value_size);
  old_ptr = m_ptr;

  m_ptr = (BUFFER_UNIT *) or_pack_value ((char *) m_ptr, (DB_VALUE *) &value);
  assert (old_ptr + value_size == m_ptr);

  CHECK_RANGE (m_ptr, m_end_ptr, 0);

  return NO_ERROR;
}

int packer::unpack_db_value (DB_VALUE *value)
{
  BUFFER_UNIT *old_ptr;
  
  m_ptr = (BUFFER_UNIT *) PTR_ALIGN (m_ptr, MAX_ALIGNMENT);
  old_ptr = m_ptr;
  m_ptr = (BUFFER_UNIT *) or_unpack_value ((char *) m_ptr, (DB_VALUE *) value);

  size_t value_size = or_packed_value_size (value, 1, 1, 0);
  assert (old_ptr + value_size == m_ptr);

  CHECK_RANGE (m_ptr, m_end_ptr, 0);

  return NO_ERROR;
}

size_t packer::get_packed_small_string_size (const char *string, const size_t curr_offset)
{
  size_t entry_size;

  entry_size = OR_BYTE_SIZE + strlen (string);

  return DB_ALIGN (curr_offset + entry_size, MAX_ALIGNMENT) - curr_offset;
}

int packer::pack_small_string (const char *string)
{
  size_t len;
  
  len = strlen (string);

  if (len >= 255)
    {
      return ER_FAILED;
    }

  CHECK_RANGE (m_ptr, m_end_ptr, len + 1);

  OR_PUT_BYTE (m_ptr, len);
  m_ptr += OR_BYTE_SIZE;
  (void) memcpy (m_ptr, string, len);
  m_ptr += len;

  m_ptr = (BUFFER_UNIT *) PTR_ALIGN (m_ptr, MAX_ALIGNMENT);

  return NO_ERROR;
}

int packer::unpack_small_string (char *string, const size_t max_size)
{
  size_t len;

  CHECK_RANGE (m_ptr, m_end_ptr, OR_BYTE_SIZE);

  len = (size_t) *m_ptr;
  if (len > max_size || len < 0)
    {
      return ER_FAILED;
    }

  m_ptr += OR_BYTE_SIZE;

  CHECK_RANGE (m_ptr, m_end_ptr, len);
  (void) memcpy (string, m_ptr, len);
  *(string + len) = '\0';
  m_ptr += len;

  m_ptr = (BUFFER_UNIT *) PTR_ALIGN (m_ptr, MAX_ALIGNMENT);

  return NO_ERROR;
}


size_t packer::get_packed_large_string_size (const std::string &str, const size_t curr_offset)
{
  size_t entry_size;

  entry_size = OR_INT_SIZE + str.size ();

  return DB_ALIGN (curr_offset + entry_size, MAX_ALIGNMENT) - curr_offset;
}

int packer::pack_large_string (const std::string &str)
{
  size_t len;
  
  len = str.size ();

  CHECK_RANGE (m_ptr, m_end_ptr, len + OR_INT_SIZE);

  OR_PUT_INT (m_ptr, len);
  m_ptr += OR_INT_SIZE;
  (void) memcpy (m_ptr, str.c_str (), len);
  m_ptr += len;
  m_ptr = (BUFFER_UNIT *) PTR_ALIGN (m_ptr, MAX_ALIGNMENT);

  return NO_ERROR;
}

int packer::unpack_large_string (std::string &str)
{
  size_t len;

  CHECK_RANGE (m_ptr, m_end_ptr, OR_INT_SIZE);

  len = OR_GET_INT (m_ptr);
  m_ptr += OR_INT_SIZE;
  if (len > 0)
    {
      CHECK_RANGE (m_ptr, m_end_ptr, len);
      str = std::string ((const char *) m_ptr);
      m_ptr += len;
    }
  m_ptr = (BUFFER_UNIT *)  PTR_ALIGN (m_ptr, MAX_ALIGNMENT);

  return NO_ERROR;
}
