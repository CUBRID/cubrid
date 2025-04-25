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
 * vector_float.cpp
 */

#include "vector_float.hpp"
#include "cubvec_assert.h"
#include <sstream>
#include <iomanip>
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/**
 * Converts a floating point vector to a string representation.
 * For vectors with more than 2*kElementsToShow elements, only the first and last kElementsToShow
 * elements are shown with an ellipsis in between.
 *
 * @param vf The vector to convert to string
 * @return String representation of the vector
 */
std::string db_vector_float_to_string (const db_vector_float &vf)
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

