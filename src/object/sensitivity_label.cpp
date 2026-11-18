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
 * sensitivity_label.cpp - Sensitivity labels and their components in the catalog
 */

#include "sensitivity_label.hpp"
#include "authenticate.h"
#include "db.h"
#include "dbtype.h"
#include "locator_cl.h"
#include "object_accessor.h"
#include "schema_manager.h"
#include "schema_system_catalog_constants.h"

static const char *get_component_class_name (const PT_MISC_TYPE kind);
static const char *get_component_kind_name (const PT_MISC_TYPE kind);

/*
 * get_component_class_name () - the catalog class that holds the grades, the info_types or the sources
 *   return: the class name
 *   kind(in): PT_SENSITIVITY_LABEL_GRADE, PT_SENSITIVITY_LABEL_INFO_TYPE or PT_SENSITIVITY_LABEL_SOURCE
 */
static const char *
get_component_class_name (const PT_MISC_TYPE kind)
{
  switch (kind)
    {
    case PT_SENSITIVITY_LABEL_GRADE:
      return CT_SENSITIVITY_LABEL_GRADE_NAME;
    case PT_SENSITIVITY_LABEL_INFO_TYPE:
      return CT_SENSITIVITY_LABEL_INFO_TYPE_NAME;
    case PT_SENSITIVITY_LABEL_SOURCE:
      return CT_SENSITIVITY_LABEL_SOURCE_NAME;
    default:
      assert (false);
      return NULL;
    }
}

/*
 * get_component_kind_name () - the kind name an error message shows
 *   return: "grade", "info_type" or "source"
 *   kind(in): PT_SENSITIVITY_LABEL_GRADE, PT_SENSITIVITY_LABEL_INFO_TYPE or PT_SENSITIVITY_LABEL_SOURCE
 */
static const char *
get_component_kind_name (const PT_MISC_TYPE kind)
{
  switch (kind)
    {
    case PT_SENSITIVITY_LABEL_GRADE:
      return "grade";
    case PT_SENSITIVITY_LABEL_INFO_TYPE:
      return "info_type";
    case PT_SENSITIVITY_LABEL_SOURCE:
      return "source";
    default:
      assert (false);
      return NULL;
    }
}

/*
 * sensitivity_label_create_component () - create a grade, an info_type or a source
 *   return: error code
 *   kind(in): PT_SENSITIVITY_LABEL_GRADE, PT_SENSITIVITY_LABEL_INFO_TYPE or PT_SENSITIVITY_LABEL_SOURCE
 *   name(in): the name
 *   level(in): the grade_level, or 0 for an info_type or a source
 *   comment(in): the comment, or NULL
 */
int
sensitivity_label_create_component (const PT_MISC_TYPE kind, const char *name, const int level, const char *comment)
{
  DB_OBJECT *class_obj;
  DB_OBJECT *component;
  DB_OTMPL *obj_tmpl = NULL;
  DB_VALUE value;
  char lower_name[DB_MAX_IDENTIFIER_LENGTH];
  int error;
  int save;

  sm_downcase_name (name, lower_name, DB_MAX_IDENTIFIER_LENGTH);

  AU_SAVE_AND_DISABLE (save);

  class_obj = db_find_class (get_component_class_name (kind));
  if (class_obj == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      goto end;
    }

  db_make_string (&value, lower_name);
  if (db_find_unique (class_obj, "name", &value) != NULL)
    {
      ERROR_SET_ERROR_2ARGS (error, ER_SENSITIVITY_LABEL_NAME_ALREADY_EXISTS,
			     get_component_kind_name (kind), lower_name);
      goto end;
    }

  ASSERT_ERROR_AND_SET (error);
  if (error != ER_OBJ_OBJECT_NOT_FOUND)
    {
      goto end;
    }
  er_clear ();

  if (kind == PT_SENSITIVITY_LABEL_GRADE)
    {
      db_make_int (&value, level);
      if (db_find_unique (class_obj, "grade_level", &value) != NULL)
	{
	  ERROR_SET_ERROR_1ARG (error, ER_SENSITIVITY_LABEL_LEVEL_ALREADY_EXISTS, level);
	  goto end;
	}

      ASSERT_ERROR_AND_SET (error);
      if (error != ER_OBJ_OBJECT_NOT_FOUND)
	{
	  goto end;
	}
      er_clear ();
    }

  obj_tmpl = dbt_create_object (class_obj);
  if (obj_tmpl == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      goto end;
    }

  db_make_string (&value, lower_name);
  error = dbt_put_internal (obj_tmpl, "name", &value);
  if (error != NO_ERROR)
    {
      goto end;
    }

  if (kind == PT_SENSITIVITY_LABEL_GRADE)
    {
      db_make_int (&value, level);
      error = dbt_put_internal (obj_tmpl, "grade_level", &value);
      if (error != NO_ERROR)
	{
	  goto end;
	}
    }

  if (comment != NULL)
    {
      db_make_string (&value, comment);
      error = dbt_put_internal (obj_tmpl, "comment", &value);
      if (error != NO_ERROR)
	{
	  goto end;
	}
    }

  error = db_set_otmpl_timestamps (obj_tmpl);
  if (error != NO_ERROR)
    {
      goto end;
    }

  component = dbt_finish_object (obj_tmpl);
  if (component == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      goto end;
    }
  obj_tmpl = NULL;

  error = locator_flush_instance (component);

end:
  if (obj_tmpl != NULL)
    {
      dbt_abort_object (obj_tmpl);
    }

  AU_RESTORE (save);

  return error;
}

/*
 * sensitivity_label_alter_component () - change a grade, an info_type or a source
 *   return: error code
 *   kind(in): PT_SENSITIVITY_LABEL_GRADE, PT_SENSITIVITY_LABEL_INFO_TYPE or PT_SENSITIVITY_LABEL_SOURCE
 *   name(in): the name
 *   new_name(in): the new name, or NULL to keep the name
 *   level(in): the new grade_level, or 0 to keep it
 *   comment(in): the new comment, or NULL to keep it
 */
int
sensitivity_label_alter_component (const PT_MISC_TYPE kind, const char *name, const char *new_name, const int level,
				   const char *comment)
{
  DB_OBJECT *class_obj;
  DB_OBJECT *component;
  DB_OBJECT *level_obj;
  DB_OTMPL *obj_tmpl = NULL;
  DB_VALUE value;
  char lower_new_name[DB_MAX_IDENTIFIER_LENGTH];
  int error;
  int save;

  /* X-lock the component now: an S lock upgraded at the update can deadlock with another ALTER of it */
  error = sensitivity_label_find_component (kind, name, AU_FETCH_WRITE, &component);
  if (error != NO_ERROR)
    {
      return error;
    }

  AU_SAVE_AND_DISABLE (save);

  class_obj = db_get_class (component);
  if (class_obj == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      goto end;
    }

  if (new_name != NULL)
    {
      sm_downcase_name (new_name, lower_new_name, DB_MAX_IDENTIFIER_LENGTH);

      db_make_string (&value, lower_new_name);
      if (db_find_unique (class_obj, "name", &value) != NULL)
	{
	  ERROR_SET_ERROR_2ARGS (error, ER_SENSITIVITY_LABEL_NAME_ALREADY_EXISTS,
				 get_component_kind_name (kind), lower_new_name);
	  goto end;
	}

      ASSERT_ERROR_AND_SET (error);
      if (error != ER_OBJ_OBJECT_NOT_FOUND)
	{
	  goto end;
	}
      er_clear ();
    }

  if (level > 0)
    {
      assert (kind == PT_SENSITIVITY_LABEL_GRADE);

      db_make_int (&value, level);
      level_obj = db_find_unique (class_obj, "grade_level", &value);
      if (level_obj == NULL)
	{
	  ASSERT_ERROR_AND_SET (error);
	  if (error != ER_OBJ_OBJECT_NOT_FOUND)
	    {
	      goto end;
	    }
	  er_clear ();
	}
      else if (level_obj != component)
	{
	  ERROR_SET_ERROR_1ARG (error, ER_SENSITIVITY_LABEL_LEVEL_ALREADY_EXISTS, level);
	  goto end;
	}
    }

  obj_tmpl = dbt_edit_object (component);
  if (obj_tmpl == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      goto end;
    }

  if (new_name != NULL)
    {
      db_make_string (&value, lower_new_name);
      error = dbt_put_internal (obj_tmpl, "name", &value);
      if (error != NO_ERROR)
	{
	  goto end;
	}
    }

  if (level > 0)
    {
      assert (kind == PT_SENSITIVITY_LABEL_GRADE);

      db_make_int (&value, level);
      error = dbt_put_internal (obj_tmpl, "grade_level", &value);
      if (error != NO_ERROR)
	{
	  goto end;
	}
    }

  if (comment != NULL)
    {
      db_make_string (&value, comment);
      error = dbt_put_internal (obj_tmpl, "comment", &value);
      if (error != NO_ERROR)
	{
	  goto end;
	}
    }

  error = db_update_otmpl_timestamp (obj_tmpl);
  if (error != NO_ERROR)
    {
      goto end;
    }

  component = dbt_finish_object (obj_tmpl);
  if (component == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      goto end;
    }
  obj_tmpl = NULL;

  error = locator_flush_instance (component);

end:
  if (obj_tmpl != NULL)
    {
      dbt_abort_object (obj_tmpl);
    }

  AU_RESTORE (save);

  return error;
}

/*
 * sensitivity_label_drop_component () - drop a grade, an info_type or a source no label refers to
 *   return: error code
 *   kind(in): PT_SENSITIVITY_LABEL_GRADE, PT_SENSITIVITY_LABEL_INFO_TYPE or PT_SENSITIVITY_LABEL_SOURCE
 *   name(in): the name
 */
int
sensitivity_label_drop_component (const PT_MISC_TYPE kind, const char *name)
{
  DB_OBJECT *component;
  DB_SESSION *session = NULL;
  DB_QUERY_RESULT *result = NULL;
  DB_VALUE value;
  STATEMENT_ID stmt_id;
  const char *sql;
  char lower_name[DB_MAX_IDENTIFIER_LENGTH];
  int error;
  int save;

  switch (kind)
    {
    case PT_SENSITIVITY_LABEL_GRADE:
      sql = "SELECT 1 FROM [" CT_SENSITIVITY_LABEL_NAME "] WHERE [grade] = ? LIMIT 1";
      break;
    case PT_SENSITIVITY_LABEL_INFO_TYPE:
      sql = "SELECT 1 FROM [" CT_SENSITIVITY_LABEL_NAME "] WHERE [info_type] = ? LIMIT 1";
      break;
    case PT_SENSITIVITY_LABEL_SOURCE:
      sql = "SELECT 1 FROM [" CT_SENSITIVITY_LABEL_NAME "] WHERE [source] = ? LIMIT 1";
      break;
    default:
      assert (false);
      return ER_FAILED;
    }

  /* X-lock the component before the reference check: a label S-locks the components it refers to until the end of
   * its transaction, so the check and a label created on the component at the same time wait for each other */
  error = sensitivity_label_find_component (kind, name, AU_FETCH_WRITE, &component);
  if (error != NO_ERROR)
    {
      return error;
    }

  AU_SAVE_AND_DISABLE (save);

  /* the object value needs a host variable, which db_compile_and_execute_local cannot take */
  session = db_open_buffer_local (sql);
  if (session == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      goto end;
    }

  error = db_set_system_generated_statement (session);
  if (error != NO_ERROR)
    {
      goto end;
    }

  stmt_id = db_compile_statement_local (session);
  if (stmt_id < 0)
    {
      ASSERT_ERROR_AND_SET (error);
      goto end;
    }

  db_make_object (&value, component);
  error = db_push_values (session, 1, &value);
  if (error != NO_ERROR)
    {
      goto end;
    }

  error = db_execute_statement_local (session, stmt_id, &result);
  if (error > 0)
    {
      sm_downcase_name (name, lower_name, DB_MAX_IDENTIFIER_LENGTH);
      ERROR_SET_ERROR_2ARGS (error, ER_SENSITIVITY_LABEL_NAME_IN_USE, get_component_kind_name (kind), lower_name);
    }
  if (error != NO_ERROR)
    {
      goto end;
    }

  error = db_drop (component);
  if (error != NO_ERROR)
    {
      goto end;
    }

  error = locator_flush_instance (component);

end:
  if (result != NULL)
    {
      db_query_end (result);
    }

  if (session != NULL)
    {
      db_close_session (session);
    }

  AU_RESTORE (save);

  return error;
}

/*
 * sensitivity_label_find_component () - find a grade, an info_type or a source by its name
 *   return: error code
 *   kind(in): PT_SENSITIVITY_LABEL_GRADE, PT_SENSITIVITY_LABEL_INFO_TYPE or PT_SENSITIVITY_LABEL_SOURCE
 *   name(in): the name
 *   fetchmode(in): AU_FETCH_READ to S-lock the component, AU_FETCH_WRITE to X-lock it
 *   component(out): the component
 */
int
sensitivity_label_find_component (const PT_MISC_TYPE kind, const char *name, const AU_FETCHMODE fetchmode,
				  DB_OBJECT **component)
{
  DB_OBJECT *class_obj;
  DB_VALUE value;
  char lower_name[DB_MAX_IDENTIFIER_LENGTH];
  int error = NO_ERROR;
  int save;

  *component = NULL;

  sm_downcase_name (name, lower_name, DB_MAX_IDENTIFIER_LENGTH);

  AU_SAVE_AND_DISABLE (save);

  class_obj = db_find_class (get_component_class_name (kind));
  if (class_obj == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      goto end;
    }

  db_make_string (&value, lower_name);
  *component = obj_find_unique (class_obj, "name", &value, fetchmode);
  if (*component == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      if (error == ER_OBJ_OBJECT_NOT_FOUND)
	{
	  er_clear ();
	  ERROR_SET_ERROR_2ARGS (error, ER_SENSITIVITY_LABEL_NAME_NOT_FOUND, get_component_kind_name (kind),
				 lower_name);
	}
    }

end:
  AU_RESTORE (save);

  return error;
}
