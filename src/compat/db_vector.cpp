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
#include <limits>
#include <string>
#include <string_view>
#include <vector>
#include <charconv>
#include <optional>
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

std::optional<std::vector<float>>
db_string_to_vector (std::string_view input)
{
  static constexpr size_t max_vector_size = 2000;
  static constexpr size_t number_buffer_size = 64;

  // Skip leading whitespace
  auto start = input.find_first_not_of (" \t\n\r");
  if (start == std::string_view::npos || input[start] != '[')
    {
      return std::nullopt;
    }

  std::vector<float> result;
  result.reserve (64); // Reserve some initial capacity

  size_t pos = start + 1;
  std::string number_buffer;
  number_buffer.reserve (number_buffer_size);

  while (pos < input.size() && result.size() < max_vector_size)
    {
      // Skip whitespace before number
      pos = input.find_first_not_of (" \t\n\r", pos);
      if (pos == std::string_view::npos)
	{
	  return std::nullopt;
	}

      // Check for closing bracket
      if (input[pos] == ']')
	{
	  break;
	}

      // Extract number until comma or closing bracket
      number_buffer.clear();
      size_t number_end = pos;
      while (number_end < input.size() &&
	     input[number_end] != ',' &&
	     input[number_end] != ']' &&
	     number_buffer.size() < number_buffer_size - 1)
	{
	  if (!std::isspace (input[number_end]))
	    {
	      number_buffer.push_back (input[number_end]);
	    }
	  ++number_end;
	}

      if (number_buffer.empty() || number_buffer.size() >= number_buffer_size - 1)
	{
	  return std::nullopt;
	}

      // Convert string to float
      try
	{
	  float value = std::stof (number_buffer);
	  result.push_back (value);
	}
      catch (const std::exception &)
	{
	  return std::nullopt;
	}

      pos = number_end;

      // Skip whitespace after number
      pos = input.find_first_not_of (" \t\n\r", pos);
      if (pos == std::string_view::npos)
	{
	  return std::nullopt;
	}

      // Must be comma or closing bracket
      if (input[pos] == ']')
	{
	  break;
	}
      if (input[pos] != ',')
	{
	  return std::nullopt;
	}
      ++pos;
    }

  // Verify proper ending
  if (pos >= input.size() || input[pos] != ']')
    {
      return std::nullopt;
    }

  // Check for trailing content
  auto end = input.find_first_not_of (" \t\n\r", pos + 1);
  if (end != std::string_view::npos || result.empty())
    {
      return std::nullopt;
    }

  return result;
}

// Optional wrapper function to maintain backward compatibility
int db_string_to_vector (const char* p, int str_len, float* vector, int* p_count)
{
  if (!p || !vector || !p_count || str_len <= 0)
    {
      return ER_FAILED;
    }

  auto result = db_string_to_vector (std::string_view (p, static_cast<size_t> (str_len)));
  if (!result)
    {
      return ER_FAILED;
    }

  if (result->size() > static_cast<size_t> (std::numeric_limits<int>::max()))
    {
      return ER_FAILED;
    }

  std::copy (result->begin(), result->end(), vector);
  *p_count = static_cast<int> (result->size());
  return NO_ERROR;
}
