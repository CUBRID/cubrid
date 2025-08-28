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
 * db_vector.hpp -  Definitions for the vector utilities.
 */

#ifndef _DB_VECTOR_HPP_
#define _DB_VECTOR_HPP_

#include <string>
#include "dbtype_def.h"

#ident "$Id$"

extern int db_string_to_vector (const char *p, int str_len, float *vector, int *count);

std::string db_vector_float_to_string (const DB_VECTOR_FLOAT &vf);

bool db_vector_is_all_zeros (const float *vf, int dim);

#endif /* _DB_VECTOR_HPP_ */
