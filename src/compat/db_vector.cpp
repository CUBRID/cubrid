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

#include "cubvec_assert.h"
#include "error_code.h"
#include <cmath>
#include <limits>
#include <sstream>
#include <iomanip>
#include <string>
#include "rapidjson/document.h"

#include "dbtype_def.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#define VECTOR_DIM_MAX 2000

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

  // Parse without modifying the length (fixes const assignment error)
  rapidjson::Document doc;
  rapidjson::ParseResult result = doc.Parse (p, static_cast<size_t> (str_len));
  if (!result)
    {
      return ER_FAILED;
    }

  // Check if root is an array
  if (!doc.IsArray())
    {
      return ER_FAILED;
    }

  // Check array size
  size_t size = doc.Size();
  if (size > static_cast<size_t> (std::numeric_limits<int>::max()))
    {
      return ER_FAILED;
    }

  if (size > VECTOR_DIM_MAX)
    {
      vimkim_log ("Parsed vector dim %zu is larger than the limit %d\n", size, VECTOR_DIM_MAX);
      return ER_FAILED;
    }

  // Convert each element to float
  for (size_t i = 0; i < size; i++)
    {
      if (!doc[i].IsNumber())
	{
	  return ER_FAILED;
	}

      float num = doc[i].GetFloat();

      if (std::isinf (num) || std::isnan (num))
	{
	  return ER_FAILED;
	}

      vector[i] = num;

    }

  *p_count = static_cast<int> (size);
  return NO_ERROR;
}

/**
 * Converts a floating point vector to a string representation.
 * @param vf The vector to convert to string
 * @return String representation of the vector
 */
std::string db_vector_float_to_string (const DB_VECTOR_FLOAT &vf)
{

  ASSERT_CUBVEC (vf.float_array != nullptr);
  ASSERT_CUBVEC (vf.dim > 0);

  const auto dim = vf.dim;
  const auto arr = vf.float_array;

  std::ostringstream oss;

  // Set floating point precision
  oss << std::fixed
      << std::setprecision (6);

  oss << '[';

#if defined(CUBVEC_TEAM) && !defined(NDEBUG)

  oss << "dim: " << dim << "] [";

  constexpr int num_show = 5;
  // Helper to append elements [start, end)
  auto append_range = [&] (int start, int end)
  {
    for (int i = start; i < end; ++i)
      {
	if (i > start)
	  {
	    oss << ", ";
	  }
	oss << arr[i];
      }
  };

  if (dim <= 2 * num_show)
    {
      // show all elements
      append_range (0, dim);
    }
  else
    {
      // first kShow
      append_range (0, num_show);
      oss << ", ... ";
      // last kShow
      append_range (dim - num_show, dim);
    }

#else
  for (int i = 0; i < dim; ++i)
    {
      if (i > 0)
	{
	  oss << ", ";
	}
      oss << arr[i];
    }
#endif

  oss << "]";
  return oss.str();
}

