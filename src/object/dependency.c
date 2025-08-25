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
#include "dbi.h"
#include "dbtype.h"
#include "dbtype_def.h"
#include "error_code.h"
#include "error_manager.h"
#include "locator_cl.h"
#include "schema_manager.h"
#include "schema_system_catalog_constants.h"
#include "transaction_cl.h"
#include <cstdio>

#define SAVEPOINT_DELETE_DEPENDENCY "DELETEDEPENDENCY"
#define SAVEPOINT_INVALIDATE_DEPENDENCIES "INVALIDATEDEPENDENCIES"

static int create_dependency (const char *unique_name, DEP_OBJECT_TYPE type, const char *ref_unique_name,
			      DB_OBJECT * ref_owner, DEP_OBJECT_TYPE ref_type, DEP_DEPENDENCY_TYPE dep_type);
static int dep_set_validity (MOP class_, const char *unique_name, DEP_VALIDITY_TYPE type);

int
dep_set_validity (MOP class_, const char *unique_name, DEP_VALIDITY_TYPE type)
{
  assert (unique_name != NULL);

  int error = NO_ERROR;
  DB_VALUE unique_name_value, validity_value;
  MOP obj = NULL;
  DB_OTMPL *obt_p = NULL;

  db_make_string (&unique_name_value, unique_name);
  db_make_int (&validity_value, type);

  obj = db_find_unique (class_, "unique_name", &unique_name_value);
  if (obj == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      return error;
    }

  obt_p = dbt_edit_object (obj);
  db_make_int (&validity_value, (int) type);
  error = dbt_put_internal (obt_p, "validity", &validity_value);
  if (error != NO_ERROR)
    {
      return error;
    }

  if (dbt_finish_object (obt_p) == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      return error;
    }

  return NO_ERROR;
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

  error = db_compile_and_execute_local (query, &query_result, NULL);
  if (error < 0)
    {
      goto end;
    }
  else if (error == 0)
    {
      return NO_ERROR;
    }

  AU_DISABLE (save);

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
      if (dep_unique_name == NULL)
	{
	  ASSERT_ERROR_AND_SET (error);
	  goto end;
	}

      error = dep_set_validity (sp_class, unique_name, DEP_INVALID);
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

  // TODO: test that unique_name is single quote
  snprintf (query, sizeof (query),
	    "SELECT [dep], [dep].[%s] "
	    "FROM [%s] AS [dep] "
	    "WHERE ([dep].[%s] = '%s' AND [dep].[%s] = %d) "
	    "OR ([dep].[%s] = '%s' AND [dep].[%s] = %d)",
	    CT_DEPENDENCY_UNIQUE_NAME_COLUMN,
	    CT_DEPENDENCY_NAME,
	    CT_DEPENDENCY_UNIQUE_NAME_COLUMN, unique_name, CT_DEPENDENCY_TYPE_COLUMN, (int) type,
	    CT_DEPENDENCY_REFERENCED_UNIQUE_NAME_COLUMN, unique_name, CT_DEPENDENCY_REFERENCED_TYPE_COLUMN, (int) type);

  error = db_compile_and_execute_local (query, &query_result, NULL);
  if (error < 0)
    {
      goto end;
    }
  else if (error == 0)
    {
      return NO_ERROR;
    }

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
	  error = dep_set_validity (sp_class, unique_name, DEP_INVALID);
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

  if (error != NO_ERROR && has_savepoint)
    {
      tran_abort_upto_system_savepoint (SAVEPOINT_DELETE_DEPENDENCY);
    }

  AU_ENABLE (save);

  return error;
}

// int
// dep_delete (const char *unique_name, DEP_OBJECT_TYPE type)
// {
//   assert (unique_name != NULL);

//   int error = NO_ERROR;
//   char query[256];
//   DB_QUERY_RESULT *query_result = NULL;
//   DB_VALUE dep_value, unique_name_value, type_value, referenced_unique_name_value, referenced_type_value;
//   MOP class_obj = NULL;
//   int save;

//   AU_DISABLE (save);

//   // TODO: replace literal with macro
//   sprintf (query,
//         "SELECT [X], [X].[unique_name], [X].[type], [X].[referenced_unique_name], [X].[referenced_type] FROM [%s] AS [X] "
//         "WHERE ([%s] = '%s' AND [%s] = %d)",
//         CT_DEPENDENCY_NAME, CT_DEPENDENCY_UNIQUE_NAME_COLUMN, unique_name, CT_DEPENDENCY_TYPE_COLUMN, type);

//   error = db_compile_and_execute_local (query, &query_result, NULL);
//   // error is row count if not negative
//   if (error < 0)
//     {
//       goto end;
//     }

//   while (db_query_next_tuple (query_result) == DB_CURSOR_SUCCESS)
//     {
//       error = db_query_get_tuple_value (query_result, 0, &dep_value);
//       if (error != NO_ERROR)
//      {
//        goto end;
//      }
//       error = db_query_get_tuple_value (query_result, 1, &unique_name_value);
//       if (error != NO_ERROR)
//      {
//        goto end;
//      }
//       error = db_query_get_tuple_value (query_result, 2, &type_value);
//       if (error != NO_ERROR)
//      {
//        goto end;
//      }

//       error = db_query_get_tuple_value (query_result, 3, &referenced_unique_name_value);
//       if (error != NO_ERROR)
//      {
//        goto end;
//      }

//       error = db_query_get_tuple_value (query_result, 4, &referenced_type_value);
//       if (error != NO_ERROR)
//      {
//        goto end;
//      }

//       class_obj = db_get_object (&dep_value);
//       if (class_obj == NULL)
//      {
//        goto end;
//      }

//       error = db_drop (class_obj);
//       if (error != NO_ERROR)
//      {
//        goto end;
//      }
//       else
//      {
//        const char *unique_name = db_get_string (&unique_name_value);
//        int type = db_get_int (&type_value);
//        char query[256];
//        DB_QUERY_RESULT *query_result = NULL;

//        sprintf (query,
//                 "SELECT [X], [X].[unique_name], [X].[type] FROM [%s] AS [X] "
//                 "WHERE ([%s] = '%s' AND [%s] = %d)",
//                 CT_DEPENDENCY_NAME, CT_DEPENDENCY_REFERENCED_UNIQUE_NAME_COLUMN,
//                 unique_name, CT_DEPENDENCY_REFERENCED_TYPE_COLUMN, type);

//        error = db_compile_and_execute_local (query, &query_result, NULL);
//        // error is row count if not negative
//        if (error < 0)
//          {
//            goto end;
//          }

//        while (db_query_next_tuple (query_result) == DB_CURSOR_SUCCESS)
//          {
//            error = db_query_get_tuple_value (query_result, 0, &dep_value);
//            if (error != NO_ERROR)
//              {
//                goto end;
//              }
//            error = db_query_get_tuple_value (query_result, 1, &unique_name_value);
//            if (error != NO_ERROR)
//              {
//                goto end;
//              }
//            error = db_query_get_tuple_value (query_result, 2, &type_value);
//            if (error != NO_ERROR)
//              {
//                goto end;
//              }

//            class_obj = db_get_object (&dep_value);
//            if (class_obj == NULL)
//              {
//                goto end;
//              }

//            type = db_get_int (&type_value);
//            if ((DEP_OBJECT_TYPE) type == DEP_OBJ_SYNONYM)
//              {
//                DB_VALUE value;
//                MOP class_obj = db_find_class (CT_SYNONYM_NAME);
//                const char *synonym_name = db_get_string (&unique_name_value);

//                db_make_string (&value, synonym_name);
//                MOP instance_obj = db_find_unique (class_obj, "unique_name", &value);

//                DB_OTMPL *obj_tmpl = dbt_edit_object (instance_obj);
//                db_make_int (&value, 1);
//                error = dbt_put_internal (obj_tmpl, "validity", &value);
//                db_value_clear (&value);

//                instance_obj = dbt_finish_object (obj_tmpl);
//                if (instance_obj == NULL)
//                  {
//                    ASSERT_ERROR_AND_SET (error);
//                    goto end;
//                  }

//                error = locator_flush_instance (instance_obj);
//                if (error != NO_ERROR)
//                  {
//                    ASSERT_ERROR ();
//                  }
//              }
//            else if ((DEP_OBJECT_TYPE) type == DEP_OBJ_TRIGGER)
//              {
//                DB_VALUE value;
//                MOP class_obj = db_find_class (CT_TRIGGER_NAME);
//                const char *trigger_name = db_get_string (&unique_name_value);

//                db_make_string (&value, trigger_name);
//                MOP instance_obj = db_find_unique (class_obj, "unique_name", &value);

//                DB_OTMPL *obj_tmpl = dbt_edit_object (instance_obj);
//                db_make_int (&value, 1);
//                error = dbt_put_internal (obj_tmpl, "validity", &value);
//                db_value_clear (&value);

//                instance_obj = dbt_finish_object (obj_tmpl);
//                if (instance_obj == NULL)
//                  {
//                    ASSERT_ERROR_AND_SET (error);
//                    goto end;
//                  }

//                error = locator_flush_instance (instance_obj);
//                if (error != NO_ERROR)
//                  {
//                    ASSERT_ERROR ();
//                  }
//              }
//            else if ((DEP_OBJECT_TYPE) type == DEP_OBJ_VIEW)
//              {
//                DB_VALUE value;
//                MOP class_obj = db_find_class (CT_CLASS_NAME);
//                const char *view_name = db_get_string (&unique_name_value);

//                db_make_string (&value, view_name);
//                MOP instance_obj = db_find_unique (class_obj, "unique_name", &value);

//                DB_OTMPL *obj_tmpl = dbt_edit_object (instance_obj);
//                db_make_int (&value, 1);
//                error = dbt_put_internal (obj_tmpl, "validity", &value);
//                db_value_clear (&value);

//                instance_obj = dbt_finish_object (obj_tmpl);
//                if (instance_obj == NULL)
//                  {
//                    ASSERT_ERROR_AND_SET (error);
//                    goto end;
//                  }

//                error = locator_flush_instance (instance_obj);
//                if (error != NO_ERROR)
//                  {
//                    ASSERT_ERROR ();
//                  }
//              }
//          }
//        // TODO: invalid object
//      }
//     }

// end:
//   if (query_result)
//     {
//       db_query_end (query_result);
//     }
//   AU_ENABLE (save);
//   return error;
// }

// TODO: comment for only calling not duplicate
static int
create_dependency (const char *unique_name, DEP_OBJECT_TYPE type,
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
      // TODO: add error code
      error = ER_GENERIC_ERROR;
      goto end;
    }

  obj_tmpl = dbt_create_object_internal ((MOP) dep_class);
  if (obj_tmpl == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
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
      error = er_errid ();
    }

end:
  if (obj_tmpl && error != NO_ERROR)
    {
      dbt_abort_object (obj_tmpl);
    }
  AU_ENABLE (save);

  return error;
}
