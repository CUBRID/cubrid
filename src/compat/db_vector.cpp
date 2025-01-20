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
 * @file db_vector.cpp
 * @brief Implements string to vector conversion functionality
 */

#include "error_code.h"
#include <limits>
#include <string>
#include <string_view>
#include <vector>
#include <optional>
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace
{
  /**
   * Configuration constants for vector parsing
   */
  constexpr size_t MAX_VECTOR_SIZE         = 2000;  // Maximum allowed elements
  constexpr size_t NUMBER_BUFFER_SIZE      = 64;    // Maximum digits per number
  constexpr size_t INITIAL_VECTOR_CAPACITY = 128;   // Initial allocation size
  constexpr std::string_view WHITESPACE    = " \t\n\r";

  /**
   * @brief Checks if vector parsing should continue
   * @param pos Current position in input string
   * @param current_size Current vector size
   * @param input Input string being parsed
   * @return true if parsing should continue
   */
  bool should_continue_parsing (
	  size_t pos,
	  size_t current_size,
	  const std::string_view& input
  )
  {
    return pos < input.size() && current_size < MAX_VECTOR_SIZE;
  }

  /**
   * @brief Validates the proper ending of vector string
   * @param pos Current position in input string
   * @param input Input string being parsed
   * @param result Vector being constructed
   * @return true if ending is valid
   */
  bool has_valid_ending (
	  size_t pos,
	  const std::string_view& input,
	  const std::vector<float> &result
  )
  {
    if (pos >= input.size() || input[pos] != ']')
      {
	return false;
      }

    size_t end = input.find_first_not_of (WHITESPACE, pos + 1);
    return end == std::string_view::npos && !result.empty();
  }
}  // anonymous namespace

/**
 * @brief Converts a string representation of a vector to std::vector<float>
 * @param input String view containing vector in format "[n1, n2, ...]"
 * @return Optional vector of floats, nullopt if parsing fails
 */
std::optional<std::vector<float>>
db_string_to_vector (std::string_view input)
{
  // Validate input starting with '['
  size_t start = input.find_first_not_of (WHITESPACE);
  if (start == std::string_view::npos || input[start] != '[')
    {
      return std::nullopt;
    }

  // Initialize result vector and number buffer
  std::vector<float> result;
  result.reserve (INITIAL_VECTOR_CAPACITY);

  std::string number_buffer;
  number_buffer.reserve (NUMBER_BUFFER_SIZE);

  // Parse numbers until end of input or max size reached
  size_t pos = start + 1;
  while (should_continue_parsing (pos, result.size(), input))
    {
      // Skip leading whitespace before number
      pos = input.find_first_not_of (WHITESPACE, pos);
      if (pos == std::string_view::npos)
	{
	  return std::nullopt;
	}

      // Check for end of vector
      if (input[pos] == ']')
	{
	  break;
	}

      // Extract and parse number
      number_buffer.clear();
      size_t number_end = pos;

      // Build number string, skipping whitespace
      while (number_end < input.size() &&
	     input[number_end] != ',' &&
	     input[number_end] != ']' &&
	     number_buffer.size() < NUMBER_BUFFER_SIZE - 1)
	{
	  if (!std::isspace (input[number_end]))
	    {
	      number_buffer.push_back (input[number_end]);
	    }
	  ++number_end;
	}

      // Validate number buffer
      if (number_buffer.empty() ||
	  number_buffer.size() >= NUMBER_BUFFER_SIZE - 1)
	{
	  return std::nullopt;
	}

      // Convert to float and add to result
      try
	{
	  result.push_back (std::stof (number_buffer));
	}
      catch (const std::exception &)
	{
	  return std::nullopt;
	}

      // Move position and check delimiter
      pos = input.find_first_not_of (WHITESPACE, number_end);
      if (pos == std::string_view::npos)
	{
	  return std::nullopt;
	}

      // Handle end of vector or comma separator
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

  return has_valid_ending (pos, input, result)
	 ? std::make_optional (std::move (result))
	 : std::nullopt;
}

/**
 * @brief Backward compatibility wrapper for C-style interface
 * @param p Input string
 * @param str_len Length of input string
 * @param vector Output buffer for floats
 * @param p_count Output parameter for number of floats
 * @return NO_ERROR on success, ER_FAILED on failure
 */
int db_string_to_vector (
	const char *p,
	int str_len,
	float *vector,
	int *p_count
)
{
  // Validate input parameters
  if (!p || !vector || !p_count || str_len <= 0)
    {
      return ER_FAILED;
    }

  // Convert string to vector
  std::optional<std::vector<float>> result =
	  db_string_to_vector (std::string_view (p, static_cast<size_t> (str_len)));

  // Validate result size and copy data
  if (!result ||
      result->size() > static_cast<size_t> (std::numeric_limits<int>::max()))
    {
      return ER_FAILED;
    }

  std::copy (result->begin(), result->end(), vector);
  *p_count = static_cast<int> (result->size());

  return NO_ERROR;
}
