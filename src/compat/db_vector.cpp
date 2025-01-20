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
 * db_vector.cpp
 */

#include "error_code.h"
#include <cerrno>
#include <cstdlib>
#include <cctype>
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

int
db_string_to_vector (const char *p, int str_len, float * vector, int *p_count)
{
  const char *end = p + str_len;
  int count = 0;
  const int number_buffer_size = 64;
  char number_buffer[number_buffer_size];
  int buffer_idx;
  const int max_vector_size = 2000;

  if (p == nullptr || vector == nullptr || p_count == nullptr)
    {
      return ER_FAILED;
    }

  // Skip leading spaces and opening bracket
  while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
    {
      p++;
    }
  if (p >= end || *p != '[')
    {
      return ER_FAILED;
    }
  p++;

  while (p < end && count < max_vector_size)
    {
      // Skip spaces before number
      while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
        {
          p++;
        }
      if (p >= end)
        {
          return ER_FAILED;
        }
      // Check for closing bracket
      if (*p == ']')
        {
          break;
        }

      // Get number into buffer
      buffer_idx = 0;
      while (p < end && *p != ',' && *p != ']' && buffer_idx < number_buffer_size - 1)
        {
          if (!isspace (*p))
            {
              number_buffer[buffer_idx++] = *p;
            }
          p++;
        }
      if (buffer_idx == 0 || buffer_idx >= number_buffer_size - 1)
        {
          return ER_FAILED;
        }
      number_buffer[buffer_idx] = '\0';

      // Convert to float
      char *end_ptr = nullptr;
      errno = 0;
      vector[count] = strtof (number_buffer, &end_ptr);
      if (errno == ERANGE)
        {
          return ER_FAILED;
        }
      if (*end_ptr != '\0')
        {
          return ER_FAILED;
        }
      count++;

      // Skip spaces after number
      while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
        {
          p++;
        }
      if (p >= end)
        {
          return ER_FAILED;
        }
      // Must be comma or closing bracket
      if (*p == ']')
        {
          break;
        }
      else if (*p != ',')
        {
          return ER_FAILED;
        }
      p++;
    }

  // Check for closing bracket
  if (p >= end || *p != ']')
    {
      return ER_FAILED;
    }
  p++;

  // Skip trailing spaces
  while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
    {
      p++;
    }
  if (p != end)
    {
      return ER_FAILED;
    }
  if (count == 0)
    {
      return ER_FAILED;
    }

  *p_count = count;
  return NO_ERROR;
}
