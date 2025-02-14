/*
 *
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

#include "vector_opfunc.hpp"
#include "dbtype.h"
#include "faiss/utils/distances.h"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

static std::vector<float> db_value_get_stdvector_float (const DB_VALUE *value)
{
  assert (value != nullptr && DB_VALUE_TYPE (value) == DB_TYPE_VECTOR);

  DB_SET *set_ref = db_get_set (value);

  int size = db_set_size (set_ref);

  std::vector<float> result;
  result.reserve (static_cast<size_t> (size));

  DB_VALUE element;
  for (int i = 0; i < size; ++i)
    {
      if (db_set_get (set_ref, i, &element) != NO_ERROR)
	{
	  assert (false);
	}
      result.push_back ((db_get_float (&element)));
    }

  for (auto i : result)
    {
      printf ("%f\n", i);
    }


  return result;
}


/**
* Computes the L2 distance between two Vector DB_VALUE objects.
*/
int vector_l2_distance (DB_VALUE* result, DB_VALUE* args[], int num_args)
{

  assert (num_args == 2);

  // Extract float vectors from the provided DB_VALUE objects.
  const std::vector<float> vec1 = db_value_get_stdvector_float (args[0]);
  const std::vector<float> vec2 = db_value_get_stdvector_float (args[1]);

  assert (vec1.size() == vec2.size());

  const float distance = faiss::fvec_L2sqr (vec1.data(), vec2.data(), vec1.size());

  // Store the result.
  db_make_double (result, distance);
  return NO_ERROR;
}

