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
}

int
t_string_add (T_STRING * t_str, char *str, int str_len)
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
