/*
 *
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
 * dependency.h - object dependency management
 */

#ifndef _DEPENDENCY_H_
#define _DEPENDENCY_H_

#ident "$Id$"

typedef enum
{
  DEP_OBJ_NONE = 0,
  DEP_OBJ_TABLE = 1,
  DEP_OBJ_VIEW = 2,
  DEP_OBJ_TRIGGER = 3,
  DEP_OBJ_FUNCTION = 4,
  DEP_OBJ_PROCEDURE = 5,
  DEP_OBJ_SERIAL = 6,
  DEP_OBJ_SYNONYM = 7,
} DEP_OBJECT_TYPE;

// TODO: comment hard and ref
typedef enum
{
  DEP_TYPE_HARD,
  DEP_TYPE_REF
} DEP_DEPENDENCY_TYPE;

typedef enum
{
  DEP_VALID,
  DEP_INVALID,
} DEP_VALIDITY_TYPE;

#if !defined (SERVER_MODE)

#include "parse_tree.h"

int dep_create_dependencies (PARSER_CONTEXT * parser, PT_NODE * node, const char *unique_name, DEP_OBJECT_TYPE type);
int dep_delete (const char *unique_name, DEP_OBJECT_TYPE type);
int dep_invalidate_dependencies (const char *unique_name, DEP_OBJECT_TYPE type);

#endif /* (SERVER_MODE) */

#endif /* _DEPENDENCY_H_ */
