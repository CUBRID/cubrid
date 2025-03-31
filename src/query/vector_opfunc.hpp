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


/*
 * vector_opfunc.hpp - Manipulate arbitrary vectors
 */

#ifndef _VECTOR_OPFUNC_H_
#define _VECTOR_OPFUNC_H_

#include "dbtype_def.h"

int
vector_distance (DB_VALUE *result, DB_VALUE *args[], int num_args);

int
vector_l1_distance (DB_VALUE *result, DB_VALUE *args[], int num_args);

int
vector_l2_distance (DB_VALUE *result, DB_VALUE *args[], int num_args);

int
vector_inner_product (DB_VALUE *result, DB_VALUE *args[], int num_args);

int
vector_negative_inner_product (DB_VALUE *result, DB_VALUE *args[], int num_args);

int
vector_cosine_distance (DB_VALUE *result, DB_VALUE *args[], int num_args);

#endif /* _VECTOR_OPFUNC_H_ */
