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
 * adjustable_array.c - adjustable array functions
 */

#ident "$Id$"

#include "config.h"

#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <assert.h>

#include "porting.h"
#include "adjustable_array.h"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/*
 * adj_ar_concat_strings() - concatenate the specified strings
 *   return: concatenated string
 *   string1(in): the first string to be concatednated
 *   string2(in): the second string to be concatednated
 *
 * Note: The returned pointer must not be freed, and its contents must not be
 *       accessed after the next call to this function.
 */
const char *
adj_ar_concat_strings (const char *string1, const char *string2, ...)
{
  va_list next_arg;
  const char *next_string;
  static ADJ_ARRAY *string_buffer = NULL;

  if (string_buffer == NULL)
    {
      string_buffer = adj_ar_new (1, 0, 2.0);
    }

  (void) adj_ar_reset (string_buffer, 1, 64, 2.0);

  if (!string1)
    {
      string1 = "?";
    }
  (void) adj_ar_append (string_buffer, string1, (int) strlen (string1));
  if (!string2)
    {
      string2 = "?";
    }
  (void) adj_ar_append (string_buffer, string2, (int) strlen (string2));

  va_start (next_arg, string2);
  while ((next_string = va_arg (next_arg, const char *)))
    {
      (void) adj_ar_append (string_buffer, next_string, (int) strlen (next_string));
    }

  (void) adj_ar_append (string_buffer, "\0", 1);

  va_end (next_arg);

  return (const char *) adj_ar_get_buffer (string_buffer);
}

/*
 * adj_ar_new() - allocate a new ADJ_ARRAY structure.
 *   return: ADJ_ARRAY pointer if suucess,
 *           NULL otherwise.
 *   element_size(in): size of array element
 *   min(in) : minimum elements in array
 *   growth_rate(in) : growth rate of array
 *
 */
ADJ_ARRAY *
adj_ar_new (int element_size, int min, float growth_rate)
{
  ADJ_ARRAY *adj_array = NULL;

  adj_array = (ADJ_ARRAY *) malloc (sizeof (ADJ_ARRAY));
  if (adj_array)
    {
      adj_array->buffer = NULL;
      adj_array->max_length = 0;
      adj_array->element_size = element_size;
      if (adj_ar_reset (adj_array, element_size, min, growth_rate) != ADJ_NOERROR)
	{
	  free (adj_array);
	  adj_array = NULL;
	}
    }
  return adj_array;
}

/*
 * ard_ar_free() - deallocate the memory block previously
 *                 allocated by adj_ar_new().
 *   return: nothing
 *   adj_array_p(in/out): the ADJ_ARRAY pointer
 *
 */
void
adj_ar_free (ADJ_ARRAY * adj_array_p)
{
  if (adj_array_p)
    {
      if (adj_array_p->buffer)
	{
	  free (adj_array_p->buffer);
	}
      free (adj_array_p);
    }
}

/*
 * adj_ar_reset() - reset ADJ_ARRAY with the given values.
 *   return: ADJ_ERR_CODE
 *   adj_array_p(in/out) : the ADJ_ARRAY pointer
 *   element_size(in) : size of array element
 *   min(in) : minimum elements in array
 *   growth_rate(in) : growth rate of array
 */
int
adj_ar_reset (ADJ_ARRAY * adj_array_p, int element_size, int min, float growth_rate)
{
  assert (adj_array_p != NULL);

  if (element_size < 1)
    {
      return ADJ_ERR_BAD_ELEMENT;
    }
  if (min < 0)
    {
      return ADJ_ERR_BAD_MIN;
    }
  if (growth_rate < 1.0)
    {
      return ADJ_ERR_BAD_RATE;
    }

  adj_array_p->max_length *= adj_array_p->element_size / element_size;
  adj_array_p->cur_length = 0;
  adj_array_p->element_size = element_size;
  adj_array_p->min_length = min;
  adj_array_p->rate = growth_rate;

  return ADJ_NOERROR;
}

/*
 * adj_ar_initialize() - initialize ADJ_ARRAY buffer with the given data.
 *   return: ADJ_ERR_CODE
 *   adj_array_p(in/out) : the ADJ_ARRAY pointer
 *   initial(in)         : the initial buffer contents
 *   initial_length(in)  : number of elements contained in 'initial' buffer
 */
int
adj_ar_initialize (ADJ_ARRAY * adj_array_p, const void *initial, int initial_length)
{
  assert (adj_array_p != NULL);

  if (adj_array_p->cur_length > 0)
    {
      return ADJ_ERR_BAD_INIT;
    }
  if (initial_length < 0)
    {
      return ADJ_ERR_BAD_LENGTH;
    }

  if (initial_length == 0)
    {
      initial_length = adj_array_p->min_length;
    }

  /* Reset array size to initial_length. */
  adj_ar_remove (adj_array_p, 0, ADJ_AR_EOA);
  adj_ar_append (adj_array_p, NULL, initial_length);

  /* Copy initial value into array elements. */
  if (initial != NULL)
    {
      if (adj_array_p->element_size == 1)
	{
	  memset ((char *) adj_array_p->buffer, *((unsigned char *) initial), initial_length);
	}
      else
	{
	  void *p;
	  for (p = adj_array_p->buffer; initial_length-- > 0; p = (void *) ((char *) p + adj_array_p->element_size))
	    {
	      memmove (p, initial, adj_array_p->element_size);
	      /* TODO ?? initial = (char*) initial + adj_array_p->element_size */
	    }
	}
    }

  return ADJ_NOERROR;
}

/*
 * adj_ar_replace() - replace the ADJ_ARRAY buffer with the given data
 *                    to the specified range
 *   return: ADJ_ERR_CODE
 *   adj_array_p(in/out):  the ADJ_ARRAY pointer
 *   src (in): source buffer
 *   src_length (in) : number of elements in src buffer
 *   start (in): start position in the array buffer to be replaced, inclusive
 *   end (in): end position in the array buffer to be replaced, exclusive
 */
int
adj_ar_replace (ADJ_ARRAY * adj_array_p, const void *src, int src_length, int start, int end)
{
  int new_length;

  assert (adj_array_p != NULL);

  /* Expand ADJ_AR_EOA. */
  if (end == ADJ_AR_EOA)
    {
      end = adj_array_p->cur_length;
    }
  if (start == ADJ_AR_EOA)
    {
      start = adj_array_p->cur_length;
    }

  /* Check for errors. */
  if (start < 0)
    {
      return ADJ_ERR_BAD_START;
    }
  else if (start > adj_array_p->cur_length)
    {
      return ADJ_ERR_BAD_START;
    }
  else if (end < start)
    {
      return ADJ_ERR_BAD_END;
    }
  else if (end > adj_array_p->cur_length)
    {
      return ADJ_ERR_BAD_END;
    }
  else if (src_length < 0)
    {
      return ADJ_ERR_BAD_NFROM;
    }

  new_length = adj_array_p->cur_length + src_length - (end - start);
  if (new_length > adj_array_p->max_length)
    {
      /* allocate larger buffer. */
      void *new_buffer;
      int new_max = MAX (adj_array_p->min_length,
			 MAX ((int) (adj_array_p->max_length * adj_array_p->rate),
			      new_length));

      if (adj_array_p->buffer)
	{
	  new_buffer = realloc (adj_array_p->buffer, new_max * adj_array_p->element_size);
	}
      else
	{
	  new_buffer = malloc (new_max * adj_array_p->element_size);
	}

      if (!new_buffer)
	{
	  return ADJ_ERR_BAD_ALLOC;
	}
      adj_array_p->buffer = new_buffer;
      adj_array_p->max_length = new_max;
    }

  /* Shift elements following replaced subarray. */
  memmove ((void *) ((char *) adj_array_p->buffer + (start + src_length) * adj_array_p->element_size),
	   (void *) ((char *) adj_array_p->buffer + end * adj_array_p->element_size),
	   (adj_array_p->cur_length - end) * adj_array_p->element_size);

  if (src)
    {
      memmove ((void *) ((char *) adj_array_p->buffer + start * adj_array_p->element_size), src,
	       src_length * adj_array_p->element_size);
    }
  adj_array_p->cur_length = new_length;

  return ADJ_NOERROR;
}

/*
 * adj_ar_remove() - remove data of the given buffer range
 *   return: ADJ_ERR_CODE
 *   adj_array_p(in/out):  the ADJ_ARRAY pointer
 *   start (in) : start position in the array buffer to be removed, inclusive
 *   end (in) : end position in the array buffer to be removed, exclusive
 */
int
adj_ar_remove (ADJ_ARRAY * adj_array_p, int start, int end)
{
  assert (adj_array_p != NULL);

  return adj_ar_replace (adj_array_p, NULL, 0, start, end);
}

/*
 * adj_ar_insert() - insert data at the specified buffer position
 *   return: ADJ_ERR_CODE
 *   adj_array_p(in/out): the ADJ_ARRAY pointer
 *   src (in) : source buffer to be inserted
 *   src_length (in) : number of elements in 'src' buffer
 *   start (in) : start position in the 'adj_array_p'
 */
int
adj_ar_insert (ADJ_ARRAY * adj_array_p, const void *src, int src_length, int start)
{
  assert (adj_array_p != NULL);

  return adj_ar_replace (adj_array_p, src, src_length, start, start);
}

/*
 * adj_ar_append() - append data at end of ADJ_ARRAY buffer
 *   return: ADJ_ERR_CODE
 *   adj_array_p(in/out):  the ADJ_ARRAY pointer
 *   src (in) : source buffer to be appended
 *   src_length (in) : number of elements in 'src' buffer
 */
int
adj_ar_append (ADJ_ARRAY * adj_array_p, const void *src, int src_length)
{
  assert (adj_array_p != NULL);

  return adj_ar_replace (adj_array_p, src, src_length, ADJ_AR_EOA, ADJ_AR_EOA);
}

/*
 * adj_ar_get_buffer() - return the buffer pointer of ADJ_ARRAY
 *   return: buffer pointer of ADJ_ARRAY
 *   adj_array_p(in): the ADJ_ARRAY pointer
 */
void *
adj_ar_get_buffer (const ADJ_ARRAY * adj_array_p)
{
  assert (adj_array_p != NULL);

  return adj_array_p->buffer;
}

/*
 * adj_ar_length() - return the current length of ADJ_ARRAY buffer
 *   return: current length of ADJ_ARRAY buffer
 *   adj_array_p(in): the ADJ_ARRAY pointer
 */
int
adj_ar_length (const ADJ_ARRAY * adj_array_p)
{
  assert (adj_array_p != NULL);

  return adj_array_p->cur_length;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * adj_ar_get_nth_buffer() - return the nth buffer pointer of ADJ_ARRAY
 *   return: NULL if the specified n is out of buffer range,
 *           nth buffer pointer otherwise
 *   adj_array_p(in): the ADJ_ARRAY pointer
 *   n (in) : buffer position
 */
void *
adj_ar_get_nth_buffer (const ADJ_ARRAY * adj_array_p, int n)
{
  assert (adj_array_p != NULL);

  if (n >= 0 && n < adj_array_p->cur_length)
    {
      return ((char *) adj_array_p->buffer + n * adj_array_p->element_size);
    }

  return NULL;
}
#endif /* ENABLE_UNUSED_FUNCTION */
