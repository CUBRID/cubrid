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
 * object_print_util.cpp
 */

#include "object_print_util.hpp"

#include "work_space.h"

#include <assert.h>

/*
 * obj_print_free_strarray() -  Most of the help functions build an array of
 *                              strings that contains the descriptions
 *                              of the object
 *      return: none
 *  strs(in) : array of strings
 *
 *  Note :
 *      This function frees the array when it is no longer necessary.
 */
void object_print::free_strarray (char **strs)
{
  int i;

  if (strs == NULL)
    {
      return;
    }

  for (i = 0; strs[i] != NULL; i++)
    {
      free_and_init (strs[i]);
    }

  free_and_init (strs);
}

/*
 * obj_print_copy_string() - Copies a string, allocating space with malloc
 *      return: new string
 *  source(in) : string to copy
 */
char *object_print::copy_string (const char *source)
{
  char *new_str = NULL;

  if (source != NULL)
    {
      new_str = (char *) malloc (strlen (source) + 1);
      if (new_str != NULL)
	{
	  strcpy (new_str, source);
	}
    }

  return new_str;
}
