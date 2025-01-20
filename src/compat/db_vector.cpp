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

  /**
   * @brief Extracts and validates a number from the input string
   * @param input Input string being parsed
   * @param pos Current position in input
   * @param number_buffer Buffer to store the number string
   * @return Position after the number, or npos if invalid
   */
  size_t extract_number (
	  const std::string_view& input,
	  size_t pos,
	  std::string& number_buffer
  )
  {
    number_buffer.clear();
    size_t number_end = pos;

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

    return (number_buffer.empty() ||
	    number_buffer.size() >= NUMBER_BUFFER_SIZE - 1)
	   ? std::string_view::npos
	   : number_end;
  }

  /**
   * @brief Converts string to float and adds to result vector
   * @param number_str String containing the number
   * @param result Vector to append the number to
   * @return true if conversion successful
   */
  bool parse_and_add_number (
	  const std::string& number_str,
	  std::vector<float> &result
  )
  {
    try
      {
	result.push_back (std::stof (number_str));
	return true;
      }
    catch (const std::exception &)
      {
	return false;
      }
  }

  /**
   * @brief Checks for valid delimiter after number
   * @param input Input string being parsed
   * @param pos Position to check
   * @return Next position to parse, or npos if invalid
   */
  size_t validate_delimiter (
	  const std::string_view& input,
	  size_t pos
  )
  {
    pos = input.find_first_not_of (WHITESPACE, pos);
    if (pos == std::string_view::npos)
      {
	return std::string_view::npos;
      }

    if (input[pos] == ']')
      {
	return pos;
      }
    if (input[pos] != ',')
      {
	return std::string_view::npos;
      }
    return pos + 1;
  }
}

std::optional<std::vector<float>>
db_string_to_vector (std::string_view input)
{
  // Validate input starting with '['
  size_t pos = input.find_first_not_of (WHITESPACE);
  if (pos == std::string_view::npos || input[pos] != '[')
    {
      return std::nullopt;
    }

  std::vector<float> result;
  result.reserve (INITIAL_VECTOR_CAPACITY);

  std::string number_buffer;
  number_buffer.reserve (NUMBER_BUFFER_SIZE);

  // Parse numbers until end of input or max size reached
  pos = pos + 1;
  while (should_continue_parsing (pos, result.size(), input))
    {
      // Skip leading whitespace and check for end
      pos = input.find_first_not_of (WHITESPACE, pos);
      if (pos == std::string_view::npos)
	{
	  return std::nullopt;
	}
      if (input[pos] == ']')
	{
	  break;
	}

      size_t number_end = extract_number (input, pos, number_buffer);
      if (number_end == std::string_view::npos)
	{
	  return std::nullopt;
	}

      if (!parse_and_add_number (number_buffer, result))
	{
	  return std::nullopt;
	}

      // Validate and move past delimiter
      pos = validate_delimiter (input, number_end);
      if (pos == std::string_view::npos)
	{
	  return std::nullopt;
	}
      if (input[pos - 1] == ']')
	{
	  break;
	}
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
