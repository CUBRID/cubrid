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
 * dynamic_array.c
 */

#include <string.h>
#include <malloc.h>

#include "error_code.h"
#include "dynamic_array.h"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

dynamic_array *
da_create (int count, size_t len)
{
  dynamic_array *da = (dynamic_array *) malloc (sizeof (dynamic_array));
  if (da == NULL)
    {
      return NULL;
    }

  if (count == 0)
    {
      count = 1000;
    }

  da->max = -1;
  da->len = (int) len;
  da->count = count;
  da->array = (unsigned char *) calloc (len, count);
  if (da->array == NULL)
    {
      free (da);
      return NULL;
    }

  return da;
}

static int
da_expand (dynamic_array * da, int max)
{
  int count = da->count * 2;
  int i;

  while (max >= count)
    {
      count *= 2;
    }

  da->array = (unsigned char *) realloc (da->array, da->len * count);
  if (da->array == NULL)
    {
      return ER_FAILED;
    }

  for (i = da->count * da->len; i < count * da->len; i++)
    {
      da->array[i] = 0;
    }
  da->count = count;
  return NO_ERROR;
}

int
da_add (dynamic_array * da, const void *data)
{
  int pos = da->max + 1;
  return da_put (da, pos, data);
}

int
da_put (dynamic_array * da, int pos, const void *data)
{
  if (pos >= da->count && da_expand (da, pos) != NO_ERROR)
    {
      return ER_FAILED;
    }

  if (pos > da->max)
    {
      da->max = pos;
    }

  memcpy (&(da->array[pos * da->len]), data, da->len);
  return NO_ERROR;
}

int
da_get (dynamic_array * da, int pos, void *data)
{
  if (pos > da->max)
    {
      return ER_FAILED;
    }

  memcpy (data, &(da->array[pos * da->len]), da->len);
  return NO_ERROR;
}

int
da_size (dynamic_array * da)
{
  if (da == NULL)
    {
      return 0;
    }

  return da->max + 1;
}

int
da_destroy (dynamic_array * da)
{
  if (da)
    {
      if (da->array)
	{
	  free (da->array);
	}
      free (da);
    }

  return NO_ERROR;
}
