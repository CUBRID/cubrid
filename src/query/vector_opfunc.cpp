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
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"
#include "set_object.h"

/**
 * Computes the L2 distance between two string representations.
 *
 * Note: This is a temporary implementation. A constant value is returned,
 * and the Faiss library will be used for an actual calculation soon.
 *
 * @param result  Pointer to the DB_VALUE that will hold the result.
 * @param args    Array of DB_VALUE pointers; expects exactly two string values.
 * @param num_args The number of arguments provided.
 *
 * @return 0 on success; a non-zero error code otherwise.
 */
int
vector_l2_distance (DB_VALUE *result, DB_VALUE *args[], int num_args)
{
  // Validate the number of arguments.
  if (num_args != 2)
    {
      fprintf (stderr, "vector_l2_distance error: expected 2 arguments, but received %d.\n", num_args);
      return ER_OBJ_INVALID_ARGUMENTS;
    }

  // Log the input arguments for debugging purposes.
  // Using a conditional operator to handle potential NULL values.
  DB_VALUE *arg0 = args[0];
  DB_VALUE *arg1 = args[1];

  printf ("Computing L2 distance between two vectors:\n");
  printf ("arg 1: ");
  db_value_print (arg0);
  printf ("\n");
  printf ("arg 2: ");
  db_value_print (arg1);
  printf ("\n");

  DB_SET *set0 = db_get_set (arg0);
  set_size(set0);
  // printf ("size: %d\n", size);

  // TODO: Replace this constant with a real computation using the Faiss library.
  db_make_double (result, 9999999.99999999);

  return 0;
}


