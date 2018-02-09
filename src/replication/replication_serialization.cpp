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
 * replication_serialization.cpp
 */

#ident "$Id$"

#ifndef _REPLICATION_SERIALIZATION_CPP_
#define _REPLICATION_SERIALIZATION_CPP_

#include "common_utils.hpp"
#include "replication_serialization.hpp"
#include "replication_entry.hpp"
#include "replication_buffer.hpp"
#include "replication_stream.hpp"
#include "object_representation.h"

#define CHECK_RANGE(ptr, endptr, amount) \
  do \
    { \
      assert ((ptr) + (amount) < (end_ptr)); \
      if ((ptr) + (amount) >= (end_ptr)) \
        { \
          throw ("serialization range not properly initialized"); \
        } \
    }

/* TODO[arnia] : error codes and mesages for ER_FAILED return codes */

replication_serialization::replication_serialization (replication_stream *stream_arg)
{
  stream = stream_arg;
  ptr = stream_arg->get_curr_ptr ();
  end_ptr = ptr;
}

BUFFER_UNIT *replication_serialization::reserve_range (const size_t amount, buffered_range **granted_range)
{
  ptr = stream->reserve_with_buffer (amount, granted_range);
  if (ptr != NULL)
    {
      end_ptr = ptr + amount;
      end_stream_serialization_scope = (*granted_range)->last_pos;
      return ptr;
    }
  else
    {
      /* no global buffer available, create a new one */
      stream_position first_pos = stream->reserve_no_buffer (amount);
      stream_position last_pos = mapped_range.first_pos + amount - 1;

      end_stream_serialization_scope = last_pos;

      replication_buffer *buffer = new replication_buffer (amount);

      stream->add_buffer_mapping (buffer, WRITE_STREAM, first_pos, last_pos, granted_range);

      ptr = buffer->get_curr_append_ptr ();

      return ptr;
    }

  return NULL;
}

int replication_serialization::serialization_completed (void)
{
  stream->update_contiguous_filled_pos (end_stream_serialization_scope);

  return NO_ERROR;
}

int replication_serialization::pack_int (const int value)
{
  CHECK_RANGE (ptr, end_ptr, OR_INT_SIZE);
   
  OR_PUT_INT (ptr, value);
  ptr += OR_INT_SIZE;
  return NO_ERROR;
}

int replication_serialization::unpack_int (int &value)
{
  if (ptr >= end_ptr)
    {
      ptr = stream->check_space_and_advance (OR_INT_SIZE);
      if (ptr == NULL)
        {
          return ER_FAILED;
        }
    }

  value = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;

  return NO_ERROR;
}

int replication_serialization::pack_int_array (const int *array, const int count)
{
  int i;

  CHECK_RANGE (ptr, end_ptr, (OR_INT_SIZE * (count + 1)));
  
  OR_PUT_INT (ptr, count);
  ptr += OR_INT_SIZE;
  for (i = 0; i < count; i++)
    {
      OR_PUT_INT (ptr, array[i]);
      ptr += OR_INT_SIZE;
    }

  return NO_ERROR;
}

int replication_serialization::unpack_int_array (int *array, int &count)
{
  BUFFER_UNIT *ptr;
  int i;

  ptr = stream->check_space_and_advance (OR_INT_SIZE);
  if (ptr == NULL)
    {
      return ER_FAILED;
    }

  count = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;

  if (count == 0)
    {
      return NO_ERROR;
    }

  ptr = stream->check_space_and_advance_with_ptr (ptr, OR_INT_SIZE * count);
  if (ptr == NULL)
    {
      return ER_FAILED;
    }
     
  for (i = 0; i < count; i++)
    {
      array[i] = OR_GET_INT (ptr);
      ptr += OR_INT_SIZE;
    }

  return NO_ERROR;
}

int replication_serialization::pack_int_vector (const vector<int> &array)
{
  BUFFER_UNIT *ptr;
  const int count = array.size();
  int i;

  CHECK_RANGE (ptr, end_ptr, (OR_INT_SIZE * (count + 1)));

  OR_PUT_INT (ptr, count);
  ptr += OR_INT_SIZE;
  for (i = 0; i < count; i++)
    {
      OR_PUT_INT (ptr, array[i]);
      ptr += OR_INT_SIZE;
    }

  return NO_ERROR;
}

int replication_serialization::unpack_int_vector (vector<int> &array)
{
  BUFFER_UNIT *ptr;
  int i;
  int count;

  ptr = stream->check_space_and_advance (OR_INT_SIZE);
  if (ptr == NULL)
    {
      return ER_FAILED;
    }
     
  count = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;

  ptr = stream->check_space_and_advance_with_ptr (ptr, count * OR_INT_SIZE);
  if (ptr == NULL)
    {
      return ER_FAILED;
    }

  for (i = 0; i < count; i++)
    {
      array.push_back (OR_GET_INT (ptr));
      ptr += OR_INT_SIZE;
    }

  return NO_ERROR;
}

int replication_serialization::pack_db_value (const DB_VALUE &value)
{
  BUFFER_UNIT *old_ptr = ptr;

  size_t value_size = or_packed_value_size ((DB_VALUE *)&value, 1, 0, 0);

  CHECK_RANGE (ptr, end_ptr, value_size);

  ptr = or_pack_value ((char *) ptr, (DB_VALUE *) &value);

  assert (old_ptr + value_size == ptr);

  return NO_ERROR;
}

int replication_serialization::unpack_db_value (DB_VALUE *value)
{
  BUFFER_UNIT *ptr;

  /* TODO[arnia] */
  ptr = or_unpack_value ((char *) ptr, (DB_VALUE *) value);

  return NO_ERROR;
}

int replication_serialization::pack_small_string (const char *string)
{
  BUFFER_UNIT *ptr;

  size_t len;
  
  len = strlen (string);

  CHECK_RANGE (ptr, end_ptr, len + 1);

  OR_PUT_BYTE (ptr, len);
  ptr += OR_BYTE_SIZE;
  (void) memcpy (ptr, string, len);
  ptr += len;

  return NO_ERROR;
}

int replication_serialization::unpack_small_string (char *string, const size_t max_size)
{
  BUFFER_UNIT *ptr;

  size_t len;

  ptr = stream->check_space_and_advance (OR_BYTE_SIZE);
  if (ptr == NULL)
    {
      return ER_FAILED;
    }

  len = (size_t) *ptr;
  ptr += OR_BYTE_SIZE;
  if (len > 0)
    {
      if (len <= max_size)
        {
          (void) memcpy (string, ptr, len);
          ptr += len;
        }
      else
        {
          return ER_FAILED;
        }
    }

  return NO_ERROR;
}

int replication_serialization::pack_stream_entry_header (const stream_entry_header &stream_header)
{
  NOT_IMPLEMENTED();
  return NO_ERROR;
}

int replication_serialization::unpack_stream_entry_header (stream_entry_header &stream_header)
{
  NOT_IMPLEMENTED();
  return NO_ERROR;
}


#endif /* _REPLICATION_SERIALIZATION_CPP_ */
