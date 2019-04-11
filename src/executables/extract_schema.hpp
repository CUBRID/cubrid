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
 * extract_schema.hpp -
 */

#ifndef _EXTRACT_SCHEMA_HPP_
#define _EXTRACT_SCHEMA_HPP_

#include "dbtype_def.h"
#include <stdio.h>

typedef enum
{
  FOLLOW_STORAGE_ORDER = 0,
  FOLLOW_ATTRIBUTE_ORDER = 1
} EMIT_STORAGE_ORDER;

struct extract_context
{
  /* input */
  int do_auth;
  EMIT_STORAGE_ORDER storage_order;
  const char *exec_name;

  /* working */
  int has_indexes;
  DB_OBJLIST *classes;
  DB_OBJLIST *vclass_list_has_using_index;

  extract_context ():
    do_auth (0),
    storage_order (FOLLOW_STORAGE_ORDER),
    exec_name (NULL),
    has_indexes (0),
    classes (NULL),
    vclass_list_has_using_index (NULL)
  {
  }

  void clear_schema_workspace (void);
};

#endif /* _EXTRACT_SCHEMA_HPP_ */
