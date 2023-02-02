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

// *INDENT-OFF*
struct extract_context
{
  /* input */
  int do_auth;
  EMIT_STORAGE_ORDER storage_order;
  const char *exec_name;
  const char *output_dirname;
  const char *output_prefix;

  /* working */
  int has_indexes;
  DB_OBJLIST *classes;
  DB_OBJLIST *vclass_list_has_using_index;

  extract_context ():
    do_auth (0),
    storage_order (FOLLOW_STORAGE_ORDER),
    exec_name (NULL),
    output_dirname(NULL),
    output_prefix(NULL),
    has_indexes (0),
    classes (NULL),
    vclass_list_has_using_index (NULL)
  {
  }

  void clear_schema_workspace (void);
};
// *INDENT-ON*
#endif /* _EXTRACT_SCHEMA_HPP_ */
