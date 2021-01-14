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
 * adjustable_array.h : adjustable array definitions
 *
 */

#ifndef _ADJUSTABLE_ARRAY_H_
#define _ADJUSTABLE_ARRAY_H_

#ident "$Id$"

#include <stdio.h>
#include <stdlib.h>

#include "porting.h"

#define ADJ_AR_EOA -1

enum adj_err_code
{
  ADJ_NOERROR = 0,
  ADJ_ERR_BAD_START = -1,
  ADJ_ERR_BAD_END = -2,
  ADJ_ERR_BAD_NFROM = -3,
  ADJ_ERR_BAD_ALLOC = -4,
  ADJ_ERR_BAD_ELEMENT = -5,
  ADJ_ERR_BAD_MIN = -6,
  ADJ_ERR_BAD_RATE = -7,
  ADJ_ERR_BAD_INIT = -8,
  ADJ_ERR_BAD_INITIAL = -9,
  ADJ_ERR_BAD_LENGTH = -10,
  ADJ_ERR_BAD_ADJ_ARR_PTR = -99
};
typedef enum adj_err_code ADJ_ERR_CODE;

typedef struct adj_array ADJ_ARRAY;
struct adj_array
{
  int cur_length;		/* current array length */
  int max_length;		/* maximum elements in buffer */
  int min_length;		/* minimum elements in buffer */
  int element_size;		/* size of array element in bytes */
  void *buffer;			/* current array buffer */
  float rate;			/* growth rate (>= 1.0) */
};

extern const char *adj_ar_concat_strings (const char *string1, const char *string2, ...);

extern ADJ_ARRAY *adj_ar_new (int element_size, int min, float growth_rate);

extern void adj_ar_free (ADJ_ARRAY * adj_array_p);

extern int adj_ar_reset (ADJ_ARRAY * adj_array_p, int element_size, int min, float growth_rate);

extern int adj_ar_initialize (ADJ_ARRAY * adj_array_p, const void *initial, int initial_length);

extern int adj_ar_replace (ADJ_ARRAY * adj_array_p, const void *src, int src_length, int start, int end);

extern int adj_ar_remove (ADJ_ARRAY * adj_array_p, int start, int end);

extern int adj_ar_insert (ADJ_ARRAY * adj_array_p, const void *src, int src_length, int start);

extern int adj_ar_append (ADJ_ARRAY * adj_array_p, const void *src, int src_length);

extern void *adj_ar_get_buffer (const ADJ_ARRAY * adj_array_p);

extern int adj_ar_length (const ADJ_ARRAY * adj_array_p);

#if defined(ENABLE_UNUSED_FUNCTION)
extern void *adj_ar_get_nth_buffer (const ADJ_ARRAY * adj_array_p, int n);
#endif

#endif /* _ADJUSTABLE_ARRAY_H_ */
