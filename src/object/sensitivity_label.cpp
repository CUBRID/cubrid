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
static int put_components (DB_OTMPL *obj_tmpl, DB_OBJECT *grade, DB_OBJECT *info_type, DB_OBJECT *source);
static int put_labeled_by (DB_OTMPL *obj_tmpl);
static int execute_query_with_object (const char *sql, DB_OBJECT *object, DB_SESSION **session,
				      DB_QUERY_RESULT **result);
static bool has_attribute_id (SM_ATTRIBUTE *att_list, const int attr_id);
static int find_label_by_name (const char *class_name, const char *att_name, MOP *class_mop, SM_CLASS **class_,
			       SM_ATTRIBUTE **att, DB_OBJECT **label);

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
 * execute_query_with_object () - execute a query whose only host variable is an object
 *   return: the number of rows of the result, or an error code
 *   sql(in): the query
 *   object(in): the value of the host variable
 *   session(out): the session of the query, to be closed with db_close_session () unless NULL
 *   result(out): the result of the query, to be ended with db_query_end () unless NULL
 */
static int
execute_query_with_object (const char *sql, DB_OBJECT *object, DB_SESSION **session, DB_QUERY_RESULT **result)
{
  DB_VALUE value;
  STATEMENT_ID stmt_id;
  int error;

  /* the object value needs a host variable, which db_compile_and_execute_local cannot take */
  *session = db_open_buffer_local (sql);
  if (*session == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      return error;
    }

  error = db_set_system_generated_statement (*session);
  if (error != NO_ERROR)
    {
      return error;
    }

  stmt_id = db_compile_statement_local (*session);
  if (stmt_id < 0)
    {
      ASSERT_ERROR_AND_SET (error);
      return error;
    }

  db_make_object (&value, object);
  error = db_push_values (*session, 1, &value);
  if (error != NO_ERROR)
    {
      return error;
    }

  return db_execute_statement_local (*session, stmt_id, result);
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

  /* the lookup above took the snapshot before it waited for the lock: take a new one, so that the check sees a
   * label committed during the wait */
  db_invalidate_mvcc_snapshot_before_statement ();

  error = execute_query_with_object (sql, component, &session, &result);
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

  assert (name != NULL);

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

/*
 * sensitivity_label_find_class () - find the class that a name in a statement refers to
 *   return: error code
 *   name(in): the class name
 *   class_mop(out): the class
 *   class_(out): the class structure
 *
 * Note: unlike sm_find_class (), a synonym is not followed. The class stays SCH-S locked to the end of the
 *       transaction, so a DDL statement on the class and this statement wait for each other.
 */
int
sensitivity_label_find_class (const char *name, MOP *class_mop, SM_CLASS **class_)
{
  char class_name[DB_MAX_IDENTIFIER_LENGTH];
  int error = NO_ERROR;

  sm_user_specified_name (name, class_name, DB_MAX_IDENTIFIER_LENGTH);

  *class_mop = locator_find_class_with_purpose (class_name, false);
  if (*class_mop == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      return error;
    }

  return au_fetch_class_force (*class_mop, class_, AU_FETCH_READ);
}

/*
 * sensitivity_label_find_attribute () - find an attribute by the class name and the attribute name in a statement
 *   return: error code
 *   class_name(in): the class name
 *   att_name(in): the attribute name
 *   class_mop(out): the class
 *   class_(out): the class structure
 *   att(out): the attribute
 */
int
sensitivity_label_find_attribute (const char *class_name, const char *att_name, MOP *class_mop, SM_CLASS **class_,
				  SM_ATTRIBUTE **att)
{
  int error;

  error = sensitivity_label_find_class (class_name, class_mop, class_);
  if (error != NO_ERROR)
    {
      return error;
    }

  /* unlike classobj_find_attribute (), a shared attribute is not found: its value is in the class, not in a row */
  *att = (SM_ATTRIBUTE *) SM_FIND_NAME_IN_COMPONENT_LIST ((*class_)->attributes, att_name);
  if (*att == NULL)
    {
      ERROR_SET_ERROR_1ARG (error, ER_SM_ATTRIBUTE_NOT_FOUND, att_name);
    }

  return error;
}

/*
 * sensitivity_label_find_classes_in_schema () - add the classes of a schema to a list
 *   return: error code
 *   schema_name(in): the name of the schema, that is, of the user that owns the classes
 *   class_list(in/out): the list, to be freed with ml_ext_free ()
 *
 * Note: each class stays SCH-S locked to the end of the transaction, so it keeps its owner until then.
 */
int
sensitivity_label_find_classes_in_schema (const char *schema_name, DB_OBJLIST **class_list)
{
  DB_SESSION *session = NULL;
  DB_QUERY_RESULT *result = NULL;
  DB_VALUE value;
  MOP owner;
  MOP class_mop;
  SM_CLASS *class_;
  int cursor;
  int error;
  int save;

  owner = au_find_user (schema_name);
  if (owner == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      return error;
    }

  AU_SAVE_AND_DISABLE (save);

  error = execute_query_with_object ("SELECT [class_of] FROM [" CT_CLASS_NAME "] WHERE [owner] = ?", owner, &session,
				     &result);
  if (error < NO_ERROR)
    {
      goto end;
    }
  error = NO_ERROR;

  for (cursor = db_query_first_tuple (result); cursor == DB_CURSOR_SUCCESS; cursor = db_query_next_tuple (result))
    {
      error = db_query_get_tuple_value (result, 0, &value);
      if (error != NO_ERROR)
	{
	  goto end;
	}
      class_mop = db_get_object (&value);

      /* a DDL statement the fetch waits for may have dropped the class or changed its owner since the query read it */
      error = au_fetch_class_force (class_mop, &class_, AU_FETCH_READ);
      if (error == ER_HEAP_UNKNOWN_OBJECT)
	{
	  er_clear ();
	  error = NO_ERROR;
	  continue;
	}
      if (error != NO_ERROR)
	{
	  goto end;
	}

      if (!ws_is_same_object (class_->owner, owner))
	{
	  continue;
	}

      error = ml_ext_add (class_list, class_mop, NULL);
      if (error != NO_ERROR)
	{
	  goto end;
	}
    }

  if (cursor != DB_CURSOR_END)
    {
      ASSERT_ERROR_AND_SET (error);
    }

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
 * sensitivity_label_is_allowed () - tell whether the attributes of a class can have sensitivity labels
 *   return: true when the class is neither a view, a system class nor a partition
 *   class_(in): the class structure
 */
bool
sensitivity_label_is_allowed (SM_CLASS *class_)
{
  return class_->class_type == SM_CLASS_CT && ! (class_->flags & SM_CLASSFLAG_SYSTEM)
	 && (class_->partition == NULL || class_->partition->pname == NULL);
}

/*
 * sensitivity_label_find () - find the label of an attribute to replace or drop it
 *   return: error code
 *   class_mop(in): the class
 *   attr_id(in): the attribute id
 *   label(out): the label, or NULL when the attribute has none
 *
 * Note: the lookup X-locks the label and ignores the snapshot: it waits for an uncommitted create or drop.
 */
int
sensitivity_label_find (MOP class_mop, const int attr_id, DB_OBJECT **label)
{
  DB_OBJECT *label_class;
  DB_VALUE value[2];
  DB_VALUE *value_ptrs[2] = { &value[0], &value[1] };
  const char *search_attrs[2] = { "class_of", "attr_id" };
  int error = NO_ERROR;
  int save;

  *label = NULL;

  AU_SAVE_AND_DISABLE (save);

  label_class = db_find_class (CT_SENSITIVITY_LABEL_NAME);
  if (label_class == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      goto end;
    }

  db_make_object (&value[0], class_mop);
  db_make_int (&value[1], attr_id);

  *label = db_find_multi_unique_for_update (label_class, 2, (char **) search_attrs, value_ptrs);
  if (*label == NULL)
    {
      /* no error is left when the attribute has no label */
      error = er_errid ();
    }

end:
  AU_RESTORE (save);

  return error;
}

/*
 * put_components () - put the grade, the info_type and the source into a label
 *   return: error code
 *   obj_tmpl(in): the template of the label
 *   grade(in): the grade
 *   info_type(in): the info_type, or NULL
 *   source(in): the source, or NULL
 */
static int
put_components (DB_OTMPL *obj_tmpl, DB_OBJECT *grade, DB_OBJECT *info_type, DB_OBJECT *source)
{
  DB_VALUE value;
  int error;

  assert (grade != NULL);

  db_make_object (&value, grade);
  error = dbt_put_internal (obj_tmpl, "grade", &value);
  if (error != NO_ERROR)
    {
      return error;
    }

  db_make_object (&value, info_type);
  error = dbt_put_internal (obj_tmpl, "info_type", &value);
  if (error != NO_ERROR)
    {
      return error;
    }

  db_make_object (&value, source);
  return dbt_put_internal (obj_tmpl, "source", &value);
}

/*
 * put_labeled_by () - put the current user into a label as the one who labeled the attribute
 *   return: error code
 *   obj_tmpl(in): the template of the label
 */
static int
put_labeled_by (DB_OTMPL *obj_tmpl)
{
  DB_VALUE value;
  char *user_name;
  int error;

  user_name = db_get_user_name ();
  if (user_name == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      return error;
    }

  db_make_string (&value, user_name);
  error = dbt_put_internal (obj_tmpl, "labeled_by", &value);
  db_string_free (user_name);

  return error;
}

/*
 * sensitivity_label_create () - create the label of an attribute
 *   return: error code
 *   class_mop(in): the class
 *   attr_id(in): the attribute id
 *   grade(in): the grade
 *   info_type(in): the info_type, or NULL
 *   source(in): the source, or NULL
 */
int
sensitivity_label_create (MOP class_mop, const int attr_id, DB_OBJECT *grade, DB_OBJECT *info_type,
			  DB_OBJECT *source)
{
  DB_OBJECT *label_class;
  DB_OBJECT *label;
  DB_OTMPL *obj_tmpl = NULL;
  DB_VALUE value;
  int error;
  int save;

  AU_SAVE_AND_DISABLE (save);

  label_class = db_find_class (CT_SENSITIVITY_LABEL_NAME);
  if (label_class == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      goto end;
    }

  obj_tmpl = dbt_create_object (label_class);
  if (obj_tmpl == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      goto end;
    }

  db_make_object (&value, class_mop);
  error = dbt_put_internal (obj_tmpl, "class_of", &value);
  if (error != NO_ERROR)
    {
      goto end;
    }

  db_make_int (&value, attr_id);
  error = dbt_put_internal (obj_tmpl, "attr_id", &value);
  if (error != NO_ERROR)
    {
      goto end;
    }

  error = put_components (obj_tmpl, grade, info_type, source);
  if (error != NO_ERROR)
    {
      goto end;
    }

  error = put_labeled_by (obj_tmpl);
  if (error != NO_ERROR)
    {
      goto end;
    }

  error = db_set_otmpl_timestamps (obj_tmpl);
  if (error != NO_ERROR)
    {
      goto end;
    }

  label = dbt_finish_object (obj_tmpl);
  if (label == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      goto end;
    }
  obj_tmpl = NULL;

  error = locator_flush_instance (label);

end:
  if (obj_tmpl != NULL)
    {
      dbt_abort_object (obj_tmpl);
    }

  AU_RESTORE (save);

  return error;
}

/*
 * sensitivity_label_replace () - replace the whole label of an attribute
 *   return: error code
 *   label(in): the label
 *   grade(in): the grade
 *   info_type(in): the info_type, or NULL
 *   source(in): the source, or NULL
 */
int
sensitivity_label_replace (DB_OBJECT *label, DB_OBJECT *grade, DB_OBJECT *info_type, DB_OBJECT *source)
{
  DB_OTMPL *obj_tmpl;
  int error;
  int save;

  AU_SAVE_AND_DISABLE (save);

  obj_tmpl = dbt_edit_object (label);
  if (obj_tmpl == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      goto end;
    }

  error = put_components (obj_tmpl, grade, info_type, source);
  if (error != NO_ERROR)
    {
      goto end;
    }

  error = put_labeled_by (obj_tmpl);
  if (error != NO_ERROR)
    {
      goto end;
    }

  error = db_update_otmpl_timestamp (obj_tmpl);
  if (error != NO_ERROR)
    {
      goto end;
    }

  label = dbt_finish_object (obj_tmpl);
  if (label == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      goto end;
    }
  obj_tmpl = NULL;

  error = locator_flush_instance (label);

end:
  if (obj_tmpl != NULL)
    {
      dbt_abort_object (obj_tmpl);
    }

  AU_RESTORE (save);

  return error;
}

/*
 * sensitivity_label_drop () - drop the label of an attribute
 *   return: error code
 *   label(in): the label
 */
int
sensitivity_label_drop (DB_OBJECT *label)
{
  int error;
  int save;

  AU_SAVE_AND_DISABLE (save);

  error = db_drop (label);
  if (error == NO_ERROR)
    {
      error = locator_flush_instance (label);
    }

  AU_RESTORE (save);

  return error;
}

/*
 * sensitivity_label_drop_all () - drop the label of every attribute of a class
 *   return: error code
 *   class_mop(in): the class
 *   class_(in): the class structure
 *   dropped(in/out): the names of the attributes whose label is dropped are appended, to be freed with
 *                    nlist_free () even on error; NULL when the caller does not want them
 *
 * Note: each attribute is looked up by its key: a scan by the snapshot would miss a label committed after it.
 */
int
sensitivity_label_drop_all (MOP class_mop, SM_CLASS *class_, DB_NAMELIST **dropped)
{
  SM_ATTRIBUTE *att;
  DB_OBJECT *label;
  int error;

  /* a class that cannot have labels has none: do not look up each attribute */
  if (!sensitivity_label_is_allowed (class_))
    {
      return NO_ERROR;
    }

  for (att = class_->attributes; att != NULL; att = (SM_ATTRIBUTE *) att->header.next)
    {
      error = sensitivity_label_find (class_mop, att->id, &label);
      if (error != NO_ERROR)
	{
	  return error;
	}

      if (label == NULL)
	{
	  continue;
	}

      error = sensitivity_label_drop (label);
      if (error != NO_ERROR)
	{
	  return error;
	}

      if (dropped != NULL)
	{
	  error = nlist_append (dropped, att->header.name, NULL, NULL);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }
	}
    }

  return NO_ERROR;
}

/*
 * has_attribute_id () - tell whether a list of attributes has an attribute of an id
 *   return: true when it has
 *   att_list(in): the list
 *   attr_id(in): the id
 */
static bool
has_attribute_id (SM_ATTRIBUTE *att_list, const int attr_id)
{
  SM_ATTRIBUTE *att;

  for (att = att_list; att != NULL; att = (SM_ATTRIBUTE *) att->header.next)
    {
      if (att->id == attr_id)
	{
	  return true;
	}
    }

  return false;
}

/*
 * sensitivity_label_drop_of_removed_attributes () - drop the label of each attribute a schema change removes
 *   return: error code
 *   class_mop(in): the class
 *   class_(in): the class structure before the change
 *   flat(in): the flattened template of the change
 *
 * Note: a subclass loses the label of an inherited attribute this way too.
 */
int
sensitivity_label_drop_of_removed_attributes (MOP class_mop, SM_CLASS *class_, SM_TEMPLATE *flat)
{
  SM_ATTRIBUTE *att;
  DB_OBJECT *label;
  int error;

  /* a class that cannot have labels has none: do not look up each attribute */
  if (!sensitivity_label_is_allowed (class_))
    {
      return NO_ERROR;
    }

  for (att = class_->attributes; att != NULL; att = (SM_ATTRIBUTE *) att->header.next)
    {
      if (has_attribute_id (flat->instance_attributes, att->id))
	{
	  continue;
	}

      error = sensitivity_label_find (class_mop, att->id, &label);
      if (error != NO_ERROR)
	{
	  return error;
	}

      if (label == NULL)
	{
	  continue;
	}

      error = sensitivity_label_drop (label);
      if (error != NO_ERROR)
	{
	  return error;
	}
    }

  return NO_ERROR;
}

/*
 * find_label_by_name () - find the label of an attribute by the class name and the attribute name in a statement
 *   return: error code
 *   class_name(in): the class name
 *   att_name(in): the attribute name
 *   class_mop(out): the class
 *   class_(out): the class structure
 *   att(out): the attribute
 *   label(out): the label, or NULL when the attribute has none
 */
static int
find_label_by_name (const char *class_name, const char *att_name, MOP *class_mop, SM_CLASS **class_,
		    SM_ATTRIBUTE **att, DB_OBJECT **label)
{
  int error;

  error = sensitivity_label_find_attribute (class_name, att_name, class_mop, class_, att);
  if (error != NO_ERROR)
    {
      return error;
    }

  return sensitivity_label_find (*class_mop, (*att)->id, label);
}

/*
 * sensitivity_label_create_on_attribute () - create the label of the attribute that a statement names, or replace it
 *   return: error code
 *   class_name(in): the class name
 *   att_name(in): the attribute name
 *   grade(in): the grade
 *   info_type(in): the info_type, or NULL
 *   source(in): the source, or NULL
 *   or_replace(in): true to replace the label that the attribute already has
 */
int
sensitivity_label_create_on_attribute (const char *class_name, const char *att_name, DB_OBJECT *grade,
				       DB_OBJECT *info_type, DB_OBJECT *source, const bool or_replace)
{
  DB_OBJECT *label;
  MOP class_mop;
  SM_CLASS *class_;
  SM_ATTRIBUTE *att;
  int error;

  error = find_label_by_name (class_name, att_name, &class_mop, &class_, &att, &label);
  if (error != NO_ERROR)
    {
      return error;
    }

  if (label == NULL)
    {
      if (!sensitivity_label_is_allowed (class_))
	{
	  ERROR_SET_ERROR_1ARG (error, ER_SENSITIVITY_LABEL_NOT_ALLOWED, sm_ch_name ((MOBJ) class_));
	  return error;
	}

      return sensitivity_label_create (class_mop, att->id, grade, info_type, source);
    }

  if (or_replace)
    {
      return sensitivity_label_replace (label, grade, info_type, source);
    }

  ERROR_SET_ERROR_2ARGS (error, ER_SENSITIVITY_LABEL_ALREADY_EXISTS, att->header.name, sm_ch_name ((MOBJ) class_));
  return error;
}

/*
 * sensitivity_label_drop_on_attribute () - drop the label of the attribute that a statement names
 *   return: error code
 *   class_name(in): the class name
 *   att_name(in): the attribute name
 */
int
sensitivity_label_drop_on_attribute (const char *class_name, const char *att_name)
{
  DB_OBJECT *label;
  MOP class_mop;
  SM_CLASS *class_;
  SM_ATTRIBUTE *att;
  int error;

  error = find_label_by_name (class_name, att_name, &class_mop, &class_, &att, &label);
  if (error != NO_ERROR)
    {
      return error;
    }

  if (label == NULL)
    {
      ERROR_SET_ERROR_2ARGS (error, ER_SENSITIVITY_LABEL_NOT_FOUND, att->header.name, sm_ch_name ((MOBJ) class_));
      return error;
    }

  return sensitivity_label_drop (label);
}
