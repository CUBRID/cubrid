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
 * dependency.c - object dependency management
 */

#ident "$Id$"

#include "dependency.h"
#include "authenticate.h"
#include "db.h"
#include "dbtype.h"
#include "locator_cl.h"
#include "schema_manager.h"
#include "transaction_cl.h"
#include "pl_struct_compile.hpp"
#include "jsp_cl.h"

#define SAVEPOINT_DELETE_DEPENDENCY "DELETEDEPENDENCY"
#define SAVEPOINT_INVALIDATE_DEPENDENCIES "INVALIDATEDEPENDENCIES"

#define UNIQUE_NAME_BUF_SIZE (DB_MAX_USER_LENGTH + DB_MAX_IDENTIFIER_LENGTH + 2)

static const char *get_unique_name (const char *name, char *buf);

static const char *
get_unique_name (const char *name, char *buf)
{
  if (strchr (name, '.') == NULL)
    {
      snprintf (buf, UNIQUE_NAME_BUF_SIZE, "%s.%s", Au_user_name, name);
      return buf;
    }

  return name;
}

PT_NODE *
dep_collect_dependencies_of_plcsql (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
{
  assert (parser != NULL && node != NULL && arg != NULL);

  cubpl_sql_semantics *semantics = (cubpl_sql_semantics *) arg;
  const char *ref_unique_name = NULL;
  MOP class_ = NULL;
  DEP_OBJECT_TYPE ref_type = DEP_OBJ_NONE;
  int error = NO_ERROR;
  char unique_name_buf[UNIQUE_NAME_BUF_SIZE];

  switch (node->node_type)
    {
    case PT_SPEC:		// Table, View, Synonym
      {
	if (PT_SPEC_IS_DERIVED (node))
	  {
	    return node;
	  }

	PT_NODE *entity = PT_SPEC_ENTITY_NAME (node);
	const char *name = NULL;
	if (PT_NAME_RESOLVED (entity))
	  {
	    snprintf (unique_name_buf, UNIQUE_NAME_BUF_SIZE, "%s.%s", PT_NAME_RESOLVED (entity),
		      PT_NAME_ORIGINAL (entity));
	    name = unique_name_buf;
	  }
	else
	  {
	    name = PT_NAME_ORIGINAL (entity);
	  }

	if (sm_is_system_class (name))
	  {
	    ref_type = DEP_OBJ_TABLE;
	    ref_unique_name = name;
	  }
	else if (sm_is_system_vclass (name))
	  {
	    ref_type = DEP_OBJ_VIEW;
	    ref_unique_name = name;
	  }
	else
	  {
	    ref_unique_name = get_unique_name (name, unique_name_buf);
	    ref_type = dep_resolve_entity_type (ref_unique_name);
	    if (ref_type == DEP_OBJ_NONE)
	      {
		return NULL;
	      }
	  }
	break;
      }
    case PT_FUNCTION:
      {
	if (node->info.function.function_type != PT_GENERIC)	// built-in functions
	  {
	    return node;
	  }

	ref_unique_name = get_unique_name (node->info.function.generic_name, unique_name_buf);
	ref_type = DEP_OBJ_FUNCTION;	// procedures not allowed in static SQL
	break;
      }
    case PT_EXPR:
      {
	if (!PT_IS_SERIAL (PT_EXPR_OP (node)))
	  {
	    return node;
	  }

	ref_unique_name = get_unique_name (PT_NAME_ORIGINAL (PT_EXPR_ARG1 (node)), unique_name_buf);
	class_ = db_find_class (CT_SERIAL_NAME);
	if (class_ == NULL)
	  {
	    return NULL;
	  }

	DB_VALUE value;
	db_make_string (&value, ref_unique_name);
	if (db_find_unique (class_, SERIAL_ATTR_UNIQUE_NAME, &value) == NULL)
	  {
	    return NULL;
	  }

	ref_type = DEP_OBJ_SERIAL;
	break;
      }
    default:
      return node;
    }

  error = plcsql_add_dependency (semantics, ref_type, ref_unique_name);
  return (error == NO_ERROR) ? node : NULL;
}

int
dep_set_validity (const char *name, DEP_VALIDITY_TYPE type)
{
  assert (name != NULL);

  int error = NO_ERROR;
  DB_VALUE name_value, validity_value;
  MOP obj = NULL;
  DB_OTMPL *obt_p = NULL;
  int save;

  db_make_string (&name_value, name);
  db_make_int (&validity_value, type);

  AU_DISABLE (save);

  obj = jsp_find_stored_procedure (name, DB_AUTH_UPDATE);
  if (obj == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      goto end;
    }

  obt_p = dbt_edit_object (obj);
  db_make_int (&validity_value, (int) type);
  error = dbt_put_internal (obt_p, SP_ATTR_VALIDITY, &validity_value);
  if (error != NO_ERROR)
    {
      goto end;
    }

  if (dbt_finish_object (obt_p) == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      goto end;
    }

end:
  AU_ENABLE (save);
  return error;
}

int
dep_invalidate_dependencies (const char *unique_name, DEP_OBJECT_TYPE type)
{
  assert (unique_name != NULL);

  char query[1024];
  int error = NO_ERROR;
  DB_QUERY_RESULT *query_result = NULL;
  int save = 0;
  DB_VALUE dep_unique_name_value;
  const char *dep_unique_name = NULL;
  MOP sp_class = NULL;
  int has_savepoint = false;

  snprintf (query, sizeof (query),
	    "SELECT [dep].[%s] "
	    "FROM [%s] AS [dep] "
	    "WHERE [dep].[%s] = '%s' AND [dep].[%s] = %d",
	    CT_DEPENDENCY_UNIQUE_NAME_COLUMN,
	    CT_DEPENDENCY_NAME,
	    CT_DEPENDENCY_REFERENCED_UNIQUE_NAME_COLUMN, unique_name, CT_DEPENDENCY_REFERENCED_TYPE_COLUMN, (int) type);

  AU_DISABLE (save);

  error = db_compile_and_execute_local (query, &query_result, NULL);
  if (error <= 0)
    {
      goto end;
    }

  sp_class = db_find_class (CT_STORED_PROC_NAME);
  if (sp_class == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      goto end;
    }

  error = tran_system_savepoint (SAVEPOINT_INVALIDATE_DEPENDENCIES);
  if (error != NO_ERROR)
    {
      goto end;
    }
  has_savepoint = true;

  while (db_query_next_tuple (query_result) == DB_CURSOR_SUCCESS)
    {
      error = db_query_get_tuple_value (query_result, 0, &dep_unique_name_value);
      if (error != NO_ERROR)
	{
	  goto end;
	}

      dep_unique_name = db_get_string (&dep_unique_name_value);
      error = dep_set_validity (dep_unique_name, DEP_INVALID);
      if (error != NO_ERROR)
	{
	  goto end;
	}
    }

  error = locator_flush_all_instances (sp_class, false);

end:
  if (query_result)
    {
      db_query_end (query_result);
    }

  if (has_savepoint && error != NO_ERROR)
    {
      tran_abort_upto_system_savepoint (SAVEPOINT_INVALIDATE_DEPENDENCIES);
    }

  AU_ENABLE (save);

  return error;
}

int
dep_delete (const char *unique_name, DEP_OBJECT_TYPE type)
{
  assert (unique_name != NULL);

  char query[1024];
  int error = NO_ERROR;
  DB_QUERY_RESULT *query_result = NULL;
  int save = 0;
  DB_VALUE dep_unique_name_value;
  const char *dep_unique_name = NULL;
  MOP sp_class = NULL;
  int has_savepoint = false;

  snprintf (query, sizeof (query),
	    "SELECT [dep], [dep].[%s] "
	    "FROM [%s] AS [dep] "
	    "WHERE ([dep].[%s] = '%s' AND [dep].[%s] = %d) "
	    "OR ([dep].[%s] = '%s' AND [dep].[%s] = %d)",
	    CT_DEPENDENCY_UNIQUE_NAME_COLUMN,
	    CT_DEPENDENCY_NAME,
	    CT_DEPENDENCY_UNIQUE_NAME_COLUMN, unique_name, CT_DEPENDENCY_TYPE_COLUMN, (int) type,
	    CT_DEPENDENCY_REFERENCED_UNIQUE_NAME_COLUMN, unique_name, CT_DEPENDENCY_REFERENCED_TYPE_COLUMN, (int) type);

  AU_DISABLE (save);

  sp_class = db_find_class (CT_STORED_PROC_NAME);
  if (sp_class == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      goto end;
    }

  error = tran_system_savepoint (SAVEPOINT_DELETE_DEPENDENCY);
  if (error != NO_ERROR)
    {
      goto end;
    }
  has_savepoint = true;

  error = db_compile_and_execute_local (query, &query_result, NULL);
  if (error <= 0)
    {
      goto end;
    }

  while (db_query_next_tuple (query_result) == DB_CURSOR_SUCCESS)
    {
      error = db_query_get_tuple_value (query_result, 1, &dep_unique_name_value);
      if (error != NO_ERROR)
	{
	  goto end;
	}

      dep_unique_name = db_get_string (&dep_unique_name_value);
      if (dep_unique_name == NULL)
	{
	  ASSERT_ERROR_AND_SET (error);
	  goto end;
	}

      // drop dependent object
      if (strcmp (dep_unique_name, unique_name) == 0)
	{
	  DB_VALUE dep_obj_value;
	  MOP dep_obj = NULL;

	  error = db_query_get_tuple_value (query_result, 0, &dep_obj_value);
	  if (error != NO_ERROR)
	    {
	      goto end;
	    }

	  dep_obj = db_get_object (&dep_obj_value);
	  if (dep_obj == NULL)
	    {
	      ASSERT_ERROR_AND_SET (error);
	      goto end;
	    }

	  error = db_drop (dep_obj);
	  if (error != NO_ERROR)
	    {
	      goto end;
	    }
	}
      else
	{
	  error = dep_set_validity (unique_name, DEP_INVALID);
	  if (error != NO_ERROR)
	    {
	      goto end;
	    }
	}

    }

  error = locator_flush_all_instances (sp_class, false);

end:
  if (query_result)
    {
      db_query_end (query_result);
    }

  if (has_savepoint && error != NO_ERROR)
    {
      tran_abort_upto_system_savepoint (SAVEPOINT_DELETE_DEPENDENCY);
    }

  AU_ENABLE (save);

  return error;
}

int
dep_create_dependency (const char *unique_name, DEP_OBJECT_TYPE type,
		       const char *ref_unique_name, DB_OBJECT * ref_owner, DEP_OBJECT_TYPE ref_type,
		       DEP_DEPENDENCY_TYPE dep_type)
{
  assert (unique_name != NULL && ref_unique_name != NULL && ref_owner != NULL);

  int save;
  int error = NO_ERROR;
  DB_OBJECT *dep_class = NULL;
  DB_OTMPL *obj_tmpl = NULL;
  DB_VALUE value;

  AU_DISABLE (save);

  dep_class = sm_find_class (CT_DEPENDENCY_NAME);
  if (dep_class == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      goto end;
    }

  obj_tmpl = dbt_create_object_internal ((MOP) dep_class);
  if (obj_tmpl == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      goto end;
    }

  db_make_string (&value, unique_name);
  error = dbt_put_internal (obj_tmpl, CT_DEPENDENCY_UNIQUE_NAME_COLUMN, &value);
  if (error != NO_ERROR)
    {
      goto end;
    }

  db_make_string (&value, sm_remove_qualifier_name (unique_name));
  error = dbt_put_internal (obj_tmpl, CT_DEPENDENCY_NAME_COLUMN, &value);
  if (error != NO_ERROR)
    {
      goto end;
    }

  db_make_object (&value, Au_user);
  error = dbt_put_internal (obj_tmpl, CT_DEPENDENCY_OWNER_COLUMN, &value);
  if (error != NO_ERROR)
    {
      goto end;
    }

  db_make_int (&value, type);
  error = dbt_put_internal (obj_tmpl, CT_DEPENDENCY_TYPE_COLUMN, &value);
  if (error != NO_ERROR)
    {
      goto end;
    }

  db_make_string (&value, ref_unique_name);
  error = dbt_put_internal (obj_tmpl, CT_DEPENDENCY_REFERENCED_UNIQUE_NAME_COLUMN, &value);
  if (error != NO_ERROR)
    {
      goto end;
    }

  db_make_string (&value, sm_remove_qualifier_name (ref_unique_name));
  error = dbt_put_internal (obj_tmpl, CT_DEPENDENCY_REFERENCED_NAME_COLUMN, &value);
  if (error != NO_ERROR)
    {
      goto end;
    }

  db_make_object (&value, ref_owner);
  error = dbt_put_internal (obj_tmpl, CT_DEPENDENCY_REFERENCED_OWNER_COLUMN, &value);
  if (error != NO_ERROR)
    {
      goto end;
    }

  db_make_int (&value, ref_type);
  error = dbt_put_internal (obj_tmpl, CT_DEPENDENCY_REFERENCED_TYPE_COLUMN, &value);
  if (error != NO_ERROR)
    {
      goto end;
    }

  db_make_int (&value, dep_type);
  error = dbt_put_internal (obj_tmpl, CT_DEPENDENCY_DEPENDENCY_TYPE_COLUMN, &value);
  if (error != NO_ERROR)
    {
      goto end;
    }

  if (dbt_finish_object (obj_tmpl) == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
    }

end:
  if (obj_tmpl && error != NO_ERROR)
    {
      dbt_abort_object (obj_tmpl);
    }
  AU_ENABLE (save);

  return error;
}

bool
dep_is_valid (const char *name)
{
  MOP sp_mop = NULL;
  DB_VALUE value;
  int save;

  AU_DISABLE (save);

  sp_mop = jsp_find_stored_procedure (name, DB_AUTH_SELECT);
  if (sp_mop == NULL)
    {
      assert (false);
      goto end;
    }

  if (db_get (sp_mop, SP_ATTR_VALIDITY, &value) != NO_ERROR)
    {
      assert (false);
      goto end;
    }

end:
  AU_ENABLE (save);
  return ((DEP_VALIDITY_TYPE) db_get_int (&value)) == DEP_VALID;
}

DEP_OBJECT_TYPE
dep_resolve_entity_type (const char *unique_name)
{
  assert (unique_name != NULL);

  MOP class_ = NULL;
  SM_CLASS_TYPE class_type;

  if (db_find_synonym (unique_name) != NULL)
    {
      return DEP_OBJ_SYNONYM;
    }

  // synonym_obj == NULL, continue to find class
  if (er_errid () != ER_SYNONYM_NOT_EXIST)
    {
      return DEP_OBJ_NONE;
    }
  er_clear ();

  class_ = db_find_class (unique_name);
  if (class_ == NULL)
    {
      return DEP_OBJ_NONE;
    }

  class_type = sm_get_class_type ((SM_CLASS *) class_->object);
  return dep_get_object_type (class_type);
}
