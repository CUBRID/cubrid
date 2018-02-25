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

#include "common_utils.hpp"
#include "packable_object.hpp"
#include "packing_buffer.hpp"
#include "object_representation.h"

#include <vector>

#define CHECK_RANGE(ptr, endptr, amount) \
  do \
    { \
      assert ((ptr) + (amount) < (endptr)); \
      if ((ptr) + (amount) >= (endptr)) \
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

int packer::pack_int (const int value)
{
  CHECK_RANGE (m_ptr, m_end_ptr, OR_INT_SIZE);
   
  OR_PUT_INT (m_ptr, value);
  m_ptr += OR_INT_SIZE;
  return NO_ERROR;
}

int packer::unpack_int (int *value)
{
  CHECK_RANGE (m_ptr, m_end_ptr, OR_INT_SIZE);

  *value = OR_GET_INT (m_ptr);
  m_ptr += OR_INT_SIZE;

  return NO_ERROR;
}

int packer::peek_unpack_int (int *value)
{
  CHECK_RANGE (m_ptr, m_end_ptr, OR_INT_SIZE);

  *value = OR_GET_INT (m_ptr);

  return NO_ERROR;
}

int packer::pack_bigint (const DB_BIGINT &value)
{
  CHECK_RANGE (m_ptr, m_end_ptr, OR_BIGINT_SIZE);
   
  OR_PUT_BIGINT (m_ptr, value);
  m_ptr += OR_BIGINT_SIZE;
  return NO_ERROR;
}

int packer::unpack_bigint (DB_BIGINT *value)
{
  CHECK_RANGE (m_ptr, m_end_ptr, OR_BIGINT_SIZE);

  OR_GET_BIGINT (m_ptr, value);
  m_ptr += OR_BIGINT_SIZE;

  return NO_ERROR;
}

int packer::pack_int_array (const int *array, const int count)
{
  int i;

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

int packer::pack_int_vector (const std::vector<int> &array)
{
  const int count = (const int) array.size();
  int i;

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

int packer::pack_db_value (const DB_VALUE &value)
{
  BUFFER_UNIT *old_ptr = m_ptr;

  size_t value_size = or_packed_value_size ((DB_VALUE *)&value, 1, 0, 0);

  CHECK_RANGE (m_ptr, m_end_ptr, value_size);

  m_ptr = (BUFFER_UNIT *) or_pack_value ((char *) m_ptr, (DB_VALUE *) &value);

  assert (old_ptr + value_size == m_ptr);

  return NO_ERROR;
}

int packer::unpack_db_value (DB_VALUE *value)
{
  /* TODO[arnia] */
  m_ptr = (BUFFER_UNIT *) or_unpack_value ((char *) m_ptr, (DB_VALUE *) value);

  return NO_ERROR;
}

int packer::pack_small_string (const char *string)
{
  size_t len;
  
  len = strlen (string);

  CHECK_RANGE (m_ptr, m_end_ptr, len + 1);

  OR_PUT_BYTE (m_ptr, len);
  m_ptr += OR_BYTE_SIZE;
  (void) memcpy (m_ptr, string, len);
  m_ptr += len;

  return NO_ERROR;
}

int packer::unpack_small_string (char *string, const size_t max_size)
{
  size_t len;

  CHECK_RANGE (m_ptr, m_end_ptr, OR_BYTE_SIZE);

  len = (size_t) *m_ptr;
  m_ptr += OR_BYTE_SIZE;
  if (len > 0)
    {
      CHECK_RANGE (m_ptr, m_end_ptr, len);
      (void) memcpy (string, m_ptr, len);
      m_ptr += len;
    }

  return NO_ERROR;
}
