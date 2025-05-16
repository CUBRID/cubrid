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
 * vector_distance_enum.h - vector distance metric enum
 */

#ifndef _VECTOR_DISTANCE_ENUM_H_
#define _VECTOR_DISTANCE_ENUM_H_

#include <string.h>

enum DB_VECTOR_DISTANCE_METRIC
{
  METRIC_UNKNOWN = 0,
  METRIC_COSINE = 1,
  METRIC_DOT = 2,
  METRIC_EUCLIDEAN = 3,
  // METRIC_EUCLIDEAN_SQUARED = 4,
  // METRIC_HAMMING = 5,
  METRIC_MANHATTAN = 6,
  // METRIC_JACCARD = 7
};

typedef struct
{
  const char *name;
  enum DB_VECTOR_DISTANCE_METRIC metric;
} metric_entry;

static const metric_entry metric_table[] = {
  { "cosine", METRIC_COSINE },
  { "dot", METRIC_DOT },
  { "euclidean", METRIC_EUCLIDEAN },
  { "manhattan", METRIC_MANHATTAN },
  { NULL, METRIC_UNKNOWN } // sentinel
};

static enum DB_VECTOR_DISTANCE_METRIC
string_to_distance_metric (const char *name)
{
  if (name == NULL)
    return METRIC_UNKNOWN;

  for (int i = 0; metric_table[i].name != NULL; ++i)
    {
      if (strcasecmp(name, metric_table[i].name) == 0)
        return metric_table[i].metric;
    }

  return METRIC_UNKNOWN;
}

#endif // _VECOTR_DISTANCE_ENUM_H_
