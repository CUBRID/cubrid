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

/**
 * @brief Converts a DB_VALUE vector of floats into a std::vector<float>.
 *
 * This function extracts a set of float elements from the given DB_VALUE object,
 * which is expected to be of type DB_TYPE_VECTOR. It then constructs and returns
 * a std::vector<float> containing these elements.
 *
 * @param value A pointer to a DB_VALUE object that holds a vector.
 * @return std::vector<float> A vector containing the float elements extracted from the DB_VALUE.
 */
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
      result.push_back (db_get_float (&element));
    }

  return result;
}


/**
 * @brief Computes the squared L2 distance between two vector DB_VALUE objects.
 *
 * This function extracts two std::vector<float> from the provided DB_VALUE objects,
 * computes the squared Euclidean (L2) distance between them using the faiss::fvec_L2sqr function,
 * and stores the result in the provided DB_VALUE result.
 *
 * @param result A pointer to a DB_VALUE where the computed distance will be stored.
 * @param args An array of pointers to DB_VALUE objects; expects exactly two vectors.
 * @param num_args The number of arguments provided in the args array; should be 2.
 * @return int NO_ERROR if the computation is successful.
 */
int vector_l2_distance (DB_VALUE* result, DB_VALUE* args[], int num_args)
{
  assert (num_args == 2);

  const std::vector<float> vec1 = db_value_get_stdvector_float (args[0]);
  const std::vector<float> vec2 = db_value_get_stdvector_float (args[1]);

  assert (vec1.size() == vec2.size());

  const float distance = faiss::fvec_L2sqr (vec1.data(), vec2.data(), vec1.size());

  db_make_double (result, distance);
  return NO_ERROR;
}
