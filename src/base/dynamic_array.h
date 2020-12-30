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
 * dynamic_array.h
 */

#ifndef DYNAMIC_ARRAY_H_
#define DYNAMIC_ARRAY_H_

typedef struct dynamic_array_t dynamic_array;
struct dynamic_array_t
{
  int count;
  int len;
  int max;
  unsigned char *array;
};

extern dynamic_array *da_create (int count, size_t len);
extern int da_add (dynamic_array * da, void *data);
extern int da_put (dynamic_array * da, int pos, void *data);
extern int da_get (dynamic_array * da, int pos, void *data);
extern int da_size (dynamic_array * da);
extern int da_destroy (dynamic_array * da);

#endif /* DYNAMIC_ARRAY_H_ */
