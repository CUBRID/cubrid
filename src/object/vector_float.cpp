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
#include "object_representation.h"
#include <sstream>

int
or_put_float_array_internal (OR_BUF *buf, const float *float_array, int array_length, int align)
{
  ASSERT_CUBVEC (false);
}

std::string db_vector_float_to_string (const db_vector_float vf)
{
  const int kElementsToShow = 5;
  const int dim = vf.dim;
  const float *float_array = vf.float_array;
  std::ostringstream result;

  // Add dimension information
  result << "dim:" << dim << " [";

  // If vector is small enough, show all elements
  if (dim <= kElementsToShow * 2)
    {
      for (int i = 0; i < dim; ++i)
	{
	  result << float_array[i];
	  if (i < dim - 1)
	    {
	      result << ", ";
	    }
	}
    }
  // Otherwise show first and last kElementsToShow elements
  else
    {
      // First kElementsToShow elements
      for (int i = 0; i < kElementsToShow; ++i)
	{
	  result << float_array[i] << ", ";
	}

      // Add ellipsis for skipped elements
      result << "... ";

      // Last kElementsToShow elements
      for (int i = dim - kElementsToShow; i < dim; ++i)
	{
	  result << float_array[i];
	  if (i < dim - 1)
	    {
	      result << ", ";
	    }
	}
    }

  result << "]";
  return result.str();
}
