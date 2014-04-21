/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

/*
 * dynamic_array.c
 */

#include <string.h>
#include <malloc.h>

#include "error_code.h"
#include "dynamic_array.h"

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
  da->len = len;
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
da_add (dynamic_array * da, void *data)
{
  int pos = da->max + 1;
  return da_put (da, pos, data);
}

int
da_put (dynamic_array * da, int pos, void *data)
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
