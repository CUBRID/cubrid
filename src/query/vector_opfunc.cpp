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

#include <stdexcept>
#include "vector_opfunc.hpp"
#include "dbtype.h"
#include "dbtype_def.h"
#include "db_vector.hpp"
#include "faiss/utils/distances.h"
#include "vector_distance_enum.h"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

static float cubvec_l2_distance (const float *vec1, const float *vec2, size_t dim);
static float cubvec_cosine_distance (const float *vec1, const float *vec2, size_t dim);

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
/* deprecated */
std::vector<float> db_value_get_stdvector_float (const DB_VALUE *value)
{
  /* The design of DB_TYPE_VECTOR has drastically changed. It is recommended that you use db_get_vector_float and access arr instead. */
  cubvec_log ("WARNING: This function is deprecated.");

  assert (value != nullptr && DB_VALUE_TYPE (value) == DB_TYPE_VECTOR);

  const DB_VECTOR_FLOAT *vf = db_get_vector_float (value);
  const auto dim = vf->dim;
  const auto arr = vf->float_array;

  return std::vector<float> (arr, arr + static_cast<size_t> (dim));
}


/**
 * @brief Computes the distance between two vector DB_VALUE objects using a specified metric.
 *
 * This function extracts two std::vector<float> from the provided DB_VALUE objects,
 * computes the distance between them based on the specified metric (default is cosine, though currently only
 * the Euclidean metric is supported), and stores the result in the provided DB_VALUE result.
 * If a third argument is provided, it is used to select the distance metric.
 *
 * @param result A pointer to a DB_VALUE where the computed distance will be stored.
 * @param args An array of pointers to DB_VALUE objects; expects exactly two vectors and a metric specifier.
 * @param num_args The number of arguments provided in the args array; should be 3.
 * @return int NO_ERROR if the computation is successful.
 */
int vector_distance (DB_VALUE *result, DB_VALUE *args[], int num_args)
{
  assert (num_args == 2 || num_args == 3);

  try
    {
      if (num_args == 2)
	{
	  // TODO: if index exists, use the metric used to create the index
	  // otherwise, use cosine distance as default
	  return vector_cosine_distance (result, args, 2);
	}

      assert (num_args == 3 && args[2] != nullptr && (DB_VALUE_TYPE (args[2]) == DB_TYPE_INTEGER));
      DB_VECTOR_DISTANCE_METRIC metric = static_cast<DB_VECTOR_DISTANCE_METRIC> (db_get_int (args[2]));

      // Use a switch statement to handle different metrics
      switch (metric)
	{
	case DB_VECTOR_DISTANCE_METRIC::METRIC_COSINE:
	  return vector_cosine_distance (result, args, 2);

	case DB_VECTOR_DISTANCE_METRIC::METRIC_DOT:
	  return vector_negative_inner_product (result, args, 2);

	case DB_VECTOR_DISTANCE_METRIC::METRIC_EUCLIDEAN:
	  return vector_l2_distance (result, args, 2);

	case DB_VECTOR_DISTANCE_METRIC::METRIC_MANHATTAN:
	  return vector_l1_distance (result, args, 2);

	default:
	  throw std::invalid_argument ("Unsupported distance metric.");
	}
    }
  catch (const std::exception &e)
    {
      // TODO: handle this error with CUBRID error code.
      std::fprintf (stderr, "faiss error: %s\n", e.what());
      std::abort();
    }
}

static int vector_distance_internal (DB_VALUE *result, DB_VALUE *args[], int num_args,
				     float (*distance_calculation) (const float *, const float *, size_t))
{
  // Ensure we have the correct number of arguments.
  assert (num_args == 2);

  if (DB_IS_NULL (args[0]) || DB_IS_NULL (args[1]))
    {
      db_make_null (result);
      return NO_ERROR;
    }

  const DB_VECTOR_FLOAT *vf1 = db_get_vector_float (args[0]);
  const auto dim1 = vf1->dim;
  const auto arr1 = vf1->float_array;

  const DB_VECTOR_FLOAT *vf2 = db_get_vector_float (args[1]);
  const auto arr2 = vf2->float_array;

  ASSERT_CUBVEC (vf1->dim == vf2->dim);

  float distance = 0.0f;
  try
    {
      distance = distance_calculation (arr1, arr2, dim1);
    }
  catch (const std::exception &e)
    {
      std::fprintf (stderr, "faiss error: %s\n", e.what());
      std::abort();
    }

  if (std::isnan (distance))
    {
      db_make_null (result);
    }
  else
    {
      db_make_double (result, static_cast<double> (distance));
    }

  return NO_ERROR;
}

int vector_l1_distance (DB_VALUE *result, DB_VALUE *args[], int num_args)
{
  return vector_distance_internal (result, args, num_args, faiss::fvec_L1);
}

int vector_l2_distance (DB_VALUE *result, DB_VALUE *args[], int num_args)
{
  return vector_distance_internal (result, args, num_args, cubvec_l2_distance);
}

static float cubvec_l2_distance (const float *vec1, const float *vec2, size_t dim)
{
  float l2 = faiss::fvec_L2sqr (vec1, vec2, dim);
  return std::sqrt (l2);
}

int vector_inner_product (DB_VALUE *result, DB_VALUE *args[], int num_args)
{
  return vector_distance_internal (result, args, num_args, faiss::fvec_inner_product);
}

int vector_negative_inner_product (DB_VALUE *result, DB_VALUE *args[], int num_args)
{
  int retval = vector_distance_internal (result, args, num_args, faiss::fvec_inner_product);
  db_make_double (result, -db_get_double (result));
  return retval;
}

int vector_cosine_distance (DB_VALUE *result, DB_VALUE *args[], int num_args)
{
  return vector_distance_internal (result, args, num_args, cubvec_cosine_distance);
}

static float cubvec_cosine_distance (const float *vec1, const float *vec2, size_t dim)
{

  float ip = faiss::fvec_inner_product (vec1, vec2, dim);
  float norm1 = faiss::fvec_norm_L2sqr (vec1, dim);
  float norm2 = faiss::fvec_norm_L2sqr (vec2, dim);

  // Handle zero vectors to avoid division by zero
  if (norm1 == 0.0f || norm2 == 0.0f)
    {
      // NaN distance
      return std::numeric_limits<float>::quiet_NaN();
    }

  float similarity = ip / (sqrtf (norm1) * sqrtf (norm2));

  // Clamp the similarity value to [-1, 1] to handle floating-point errors
  if (similarity > 1.0f)
    {
      similarity = 1.0f;
    }
  if (similarity < -1.0f)
    {
      similarity = -1.0f;
    }

  // Cosine distance is 1 - cosine similarity
  float distance = 1.0f - similarity;
  assert (distance <= 2.0f && distance >= 0.0f);
  return distance;

}
