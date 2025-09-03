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

typedef enum
{
  DEP_TYPE_HARD,		/* Strong dependency: invalid if referenced object is dropped/changed */
  DEP_TYPE_REF			/* Reference-only: still usable if referenced object is dropped (e.g. Materialized View) */
} DEP_DEPENDENCY_TYPE;

typedef enum
{
  DEP_VALID,
  DEP_INVALID,
} DEP_VALIDITY_TYPE;

#if !defined (SERVER_MODE)

#include "parse_tree.h"
#include "class_object.h"
#include "sp_constants.hpp"

static inline DEP_OBJECT_TYPE
dep_get_object_type (SM_CLASS_TYPE class_type)
{
  return (class_type == SM_CLASS_CT) ? DEP_OBJ_TABLE : DEP_OBJ_VIEW;
}

static inline DEP_OBJECT_TYPE
dep_get_object_type (PT_MISC_TYPE class_type)
{
  return (class_type == PT_CLASS) ? DEP_OBJ_TABLE : DEP_OBJ_VIEW;
}

static inline DEP_OBJECT_TYPE
dep_get_object_type (SP_TYPE_ENUM sp_type)
{
  return (sp_type == SP_TYPE_PROCEDURE) ? DEP_OBJ_PROCEDURE : DEP_OBJ_FUNCTION;
}

int dep_create_dependencies (PARSER_CONTEXT * parser, PT_NODE * node, const char *unique_name, DEP_OBJECT_TYPE type);
int dep_delete (const char *unique_name, DEP_OBJECT_TYPE type);
int dep_invalidate_dependencies (const char *unique_name, DEP_OBJECT_TYPE type);
PT_NODE *dep_collect_dependencies_of_plcsql (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk);
int dep_create_dependency (const char *unique_name, DEP_OBJECT_TYPE type,
			   const char *ref_unique_name, DB_OBJECT * ref_owner, DEP_OBJECT_TYPE ref_type,
			   DEP_DEPENDENCY_TYPE dep_type);
int dep_set_validity (const char *unique_name, DEP_VALIDITY_TYPE type);
bool dep_is_valid (const char *name);
DEP_OBJECT_TYPE dep_resolve_entity_type (const char *unique_name);

#endif /* (SERVER_MODE) */

#endif /* _DEPENDENCY_H_ */
