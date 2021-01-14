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
 * log_top_string.c -
 */

#ident "$Id$"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cas_common.h"
#include "log_top_string.h"

#define STR_ALLOC_SIZE(X)	(((X) + 1023) / 1024 * 1024)

T_STRING *
t_string_make (int init_size)
{
  T_STRING *t_str;

  t_str = (T_STRING *) MALLOC (sizeof (T_STRING));
  if (t_str == NULL)
    return NULL;

  if (init_size <= 0)
    init_size = 1;
  init_size = STR_ALLOC_SIZE (init_size);

  t_str->data = (char *) MALLOC (init_size);
  if (t_str->data == NULL)
    {
      FREE_MEM (t_str);
      return NULL;
    }
  t_str->alloc_size = init_size;
  t_string_clear (t_str);
  return t_str;
}

void
t_string_clear (T_STRING * t_str)
{
  t_str->data[0] = '\0';
  t_str->data_len = 0;
  t_str->bind_len = 0;
}

int
t_string_add (T_STRING * t_str, char *str, int str_len)
{
  return t_bind_string_add (t_str, str, str_len, 0);
}

int
t_bind_string_add (T_STRING * t_str, char *str, int str_len, int bind_len)
{
  if (t_str->alloc_size < t_str->data_len + str_len + 1)
    {
      int new_alloc_size = STR_ALLOC_SIZE (t_str->data_len + str_len + 1);
      t_str->data = (char *) REALLOC (t_str->data, new_alloc_size);
      if (t_str->data == NULL)
	return -1;
      t_str->alloc_size = new_alloc_size;
    }
  memcpy (t_str->data + t_str->data_len, str, str_len);
  t_str->data_len += str_len;
  t_str->data[t_str->data_len] = '\0';
  t_str->bind_len = bind_len;
  return 0;
}

void
t_string_free (T_STRING * t_str)
{
  if (t_str)
    {
      FREE_MEM (t_str->data);
      FREE_MEM (t_str);
    }
}

char *
t_string_str (T_STRING * t_str)
{
  return t_str->data;
}

int
t_string_len (T_STRING * t_str)
{
  return t_str->data_len;
}

int
t_string_bind_len (T_STRING * t_str)
{
  return t_str->bind_len;
}
