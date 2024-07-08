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

#include "authenticate_access_auth.hpp"

//
#include "authenticate.h"
#include "authenticate_grant.hpp"
#include "set_object.h"
#include "dbtype.h"
#include "error_manager.h"
#include "object_accessor.h"
#include "object_primitive.h"

#include "db.h"
#include "dbi.h"
#include "schema_manager.h"
#include "schema_system_catalog_constants.h"

#include "jsp_cl.h"

const char *AU_TYPE_SET[] =
{
  "SELECT",			/* DB_AUTH_SELECT */
  "INSERT",			/* DB_AUTH_INSERT */
  "UPDATE",			/* DB_AUTH_UPDATE */
  "DELETE",			/* DB_AUTH_DELETE */
  "ALTER",			/* DB_AUTH_ALTER */
  "INDEX",			/* DB_AUTH_INDEX */
  "EXECUTE"			/* DB_AUTH_EXECUTE */
};

const int AU_TYPE_SET_LEN[] =
{
  strlen ("SELECT"),		/* DB_AUTH_SELECT */
  strlen ("INSERT"),		/* DB_AUTH_INSERT */
  strlen ("UPDATE"),		/* DB_AUTH_UPDATE */
  strlen ("DELETE"),		/* DB_AUTH_DELETE */
  strlen ("ALTER"),		/* DB_AUTH_ALTER */
  strlen ("INDEX"),		/* DB_AUTH_INDEX */
  strlen ("EXECUTE")		/* DB_AUTH_EXECUTE */
};

MOP au_auth_accessor::au_class_mop = nullptr;

au_auth_accessor::au_auth_accessor ()
  : m_au_obj (nullptr)
{}

int
au_auth_accessor::create_new_auth ()
{
  if (au_class_mop == nullptr)
    {
      au_class_mop = sm_find_class (CT_CLASSAUTH_NAME);
      if (au_class_mop == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_AU_MISSING_CLASS, 1, CT_CLASSAUTH_NAME);
	}
    }

  m_au_obj = db_create_internal (au_class_mop);
  if (m_au_obj == NULL)
    {
      assert (er_errid () != NO_ERROR);
    }
  return er_errid ();
}

int
au_auth_accessor::set_new_auth (DB_OBJECT_TYPE obj_type, MOP au_obj, MOP grantor, MOP user, MOP obj_mop,
				DB_AUTH auth_type, bool grant_option)
{
  DB_VALUE value, class_name_val, name_val;
  MOP db_class = nullptr, inst_mop = nullptr;
  DB_AUTH type;
  int i;
  int error = NO_ERROR;

  m_au_obj = au_obj;
  if (m_au_obj == nullptr)
    {
      error = create_new_auth ();
    }

  db_make_object (&value, grantor);
  obj_set (m_au_obj, AU_AUTH_ATTR_GRANTOR, &value);

  db_make_object (&value, user);
  obj_set (m_au_obj, AU_AUTH_ATTR_GRANTEE, &value);

  if (obj_type == DB_OBJECT_CLASS)
    {
      /*
      db_class = sm_find_class (CT_CLASS_NAME);
      if (db_class == NULL)
      {
        assert (er_errid () != NO_ERROR);
        return er_errid ();
      }

      db_make_string (&name_val, sm_get_ch_name (obj_mop));
      inst_mop = obj_find_unique (db_class, "unique_name", &name_val, AU_FETCH_READ);
      if (inst_mop == NULL)
      {
        assert (er_errid () != NO_ERROR);
        pr_clear_value (&name_val);
        return er_errid ();
      }
      */

      inst_mop = obj_mop;
    }
  else
    {
      // TODO: CBRD-24912
      db_make_string (&name_val, jsp_get_name (obj_mop));
      inst_mop = jsp_find_stored_procedure (db_get_string (&name_val), DB_AUTH_NONE);
      if (inst_mop == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  pr_clear_value (&name_val);
	  return er_errid ();
	}
    }

  db_make_int (&value, (int) obj_type);
  obj_set (m_au_obj, "object_type", &value);

  db_make_object (&value, inst_mop);
  obj_set (m_au_obj, "class_of", &value);

  for (type = DB_AUTH_SELECT, i = 0; type != auth_type; type = (DB_AUTH) (type << 1), i++);

  db_make_varchar (&value, 7, AU_TYPE_SET[i], AU_TYPE_SET_LEN[i], LANG_SYS_CODESET, LANG_SYS_COLLATION);
  obj_set (m_au_obj, "auth_type", &value);

  db_make_int (&value, (int) grant_option);
  obj_set (m_au_obj, "is_grantable", &value);

  pr_clear_value (&class_name_val);
  return NO_ERROR;
}

int
au_auth_accessor::get_new_auth (DB_OBJECT_TYPE obj_type, MOP grantor, MOP user, MOP obj_mop, DB_AUTH auth_type)
{
  int error = NO_ERROR, save, i = 0;
  DB_VALUE val[COUNT_FOR_VARIABLES];
  DB_VALUE grant_value;
  DB_QUERY_RESULT *result = NULL;
  DB_SESSION *session = NULL;
  STATEMENT_ID stmt_id;
  const char *name;
  const char *sql_query =
	  "SELECT [au].object FROM [" CT_CLASSAUTH_NAME "] [au]"
	  " WHERE [au].[grantee].[name] = ? AND [au].[grantor].[name] = ?" " AND [au].[class_of] = ? AND [au].[auth_type] = ?";

  for (i = 0; i < COUNT_FOR_VARIABLES; i++)
    {
      db_make_null (&val[i]);
    }

  db_make_null (&grant_value);

  /* Disable the checking for internal authorization object access */
  AU_DISABLE (save);

  /* Prepare DB_VALUEs for host variables */
  error = obj_get (user, "name", &val[INDEX_FOR_GRANTEE_NAME]);
  if (error != NO_ERROR)
    {
      goto exit;
    }
  else if (!DB_IS_STRING (&val[INDEX_FOR_GRANTEE_NAME]) || DB_IS_NULL (&val[INDEX_FOR_GRANTEE_NAME])
	   || db_get_string (&val[INDEX_FOR_GRANTEE_NAME]) == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_AU_MISSING_OR_INVALID_USER, 0);
      goto exit;
    }

  error = obj_get (grantor, "name", &val[INDEX_FOR_GRANTOR_NAME]);
  if (error != NO_ERROR)
    {
      goto exit;
    }
  else if (!DB_IS_STRING (&val[INDEX_FOR_GRANTOR_NAME]) || DB_IS_NULL (&val[INDEX_FOR_GRANTOR_NAME])
	   || db_get_string (&val[INDEX_FOR_GRANTOR_NAME]) == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_AU_MISSING_OR_INVALID_USER, 0);
      goto exit;
    }

  /*
  name = db_get_class_name (obj_mop);
  if (name == NULL)
  {
          er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SM_INVALID_CLASS, 0);
          goto exit;
  }
  */

  db_make_object (&val[INDEX_FOR_OBJECT_NAME], obj_mop);

  /*
  if (obj_type == DB_OBJECT_CLASS)
    {
      sprintf (sql_query_by_obj_type, sql_query, "[au].[class_of].[unique_name]");
      name = db_get_class_name (obj_mop);
      if (name == NULL)
  {
    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SM_INVALID_CLASS, 0);
    goto exit;
  }
      db_make_string (&val[INDEX_FOR_OBJECT_NAME], name);
    }
  else
    {
      // TODO: only procedure, add more objects
      sprintf (sql_query_by_obj_type, sql_query, "[au].[class_of].[sp_name]");
      name = jsp_get_name (obj_mop);
      if (name == NULL)
  {
    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SM_INVALID_CLASS, 0);
    goto exit;
  }
      db_make_string (&val[INDEX_FOR_OBJECT_NAME], name);
    }
  */

  /*
  class_name = db_get_class_name (obj_mop);
  if (class_name == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SM_INVALID_CLASS, 0);
      goto exit;
    }
  db_make_string (&val[INDEX_FOR_OBJECT_NAME], class_name);
  */

  i = 0;
  for (DB_AUTH type = DB_AUTH_SELECT; type != auth_type; type = (DB_AUTH) (type << 1))
    {
      i++;
    }
  db_make_string (&val[INDEX_FOR_AUTH_TYPE], AU_TYPE_SET[i]);

  session = db_open_buffer (sql_query);
  if (session == NULL)
    {
      assert (er_errid () != NO_ERROR);
      goto release;
    }

  error = db_push_values (session, COUNT_FOR_VARIABLES, val);
  if (error != NO_ERROR)
    {
      assert (er_errid () != NO_ERROR);
      goto release;
    }

  stmt_id = db_compile_statement (session);
  if (stmt_id != 1)
    {
      assert (er_errid () != NO_ERROR);
      goto release;
    }

  error = db_execute_statement_local (session, stmt_id, &result);

  /* The error value is row count if it's not negative value. */
  if (error == 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      error = ER_GENERIC_ERROR;
      goto release;
    }
  else if (error < 0)
    {
      assert (er_errid () != NO_ERROR);
      goto release;
    }

  error = NO_ERROR;

  if (db_query_first_tuple (result) == DB_CURSOR_SUCCESS)
    {
      if (db_query_get_tuple_value (result, 0, &grant_value) == NO_ERROR)
	{
	  m_au_obj = NULL;
	  if (!DB_IS_NULL (&grant_value))
	    {
	      m_au_obj = db_get_object (&grant_value);
	    }
	}

      assert (db_query_next_tuple (result) == DB_CURSOR_END);
    }

  assert (m_au_obj != NULL);

release:
  if (result != NULL)
    {
      db_query_end (result);
    }
  if (session != NULL)
    {
      db_close_session (session);
    }

exit:
  AU_ENABLE (save);

  db_value_clear (&grant_value);

  for (i = 0; i < COUNT_FOR_VARIABLES; i++)
    {
      db_value_clear (&val[i]);
    }

  if (m_au_obj == NULL && er_errid () == NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      error = ER_GENERIC_ERROR;
    }

  return (error);
}

int
au_auth_accessor::insert_auth (DB_OBJECT_TYPE obj_type, MOP grantor, MOP user, MOP obj_mop, DB_AUTH auth_type,
			       int grant_option)
{
  int error = NO_ERROR;
  for (int index = DB_AUTH_EXECUTE; index; index >>= 1)
    {
      if (auth_type & index)
	{
	  error = set_new_auth (obj_type, NULL, grantor, user, obj_mop, (DB_AUTH) index,
				((grant_option & index) ? true : false));
	  if (error != NO_ERROR)
	    {
	      break;
	    }
	}
    }

  return error;
}

int
au_auth_accessor::update_auth (DB_OBJECT_TYPE obj_type, MOP grantor, MOP user, MOP obj_mop, DB_AUTH auth_type,
			       int grant_option)
{
  int error = NO_ERROR;
  for (int index = DB_AUTH_EXECUTE; index; index >>= 1)
    {
      if (auth_type & index)
	{
	  error = get_new_auth (obj_type, grantor, user, obj_mop, (DB_AUTH) index);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }

	  assert (m_au_obj != NULL);

	  error = obj_inst_lock (m_au_obj, 1);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }

	  error = set_new_auth (obj_type, m_au_obj, grantor, user, obj_mop, (DB_AUTH) index,
				((grant_option & index) ? true : false));
	  if (error != NO_ERROR)
	    {
	      return error;
	    }
	}
    }

  return error;
}

int
au_auth_accessor::delete_auth (DB_OBJECT_TYPE obj_type, MOP grantor, MOP user, MOP obj_mop, DB_AUTH auth_type)
{
  int error = NO_ERROR;
  for (int index = DB_AUTH_EXECUTE; index; index >>= 1)
    {
      if (auth_type & index)
	{
	  error = get_new_auth (obj_type, grantor, user, obj_mop, (DB_AUTH) index);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }

	  assert (m_au_obj != NULL);

	  error = obj_inst_lock (m_au_obj, 1);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }

	  error = obj_delete (m_au_obj);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }
	}
    }

  return error;
}


/*
 * au_delete_auth_of_dropping_user - delete _db_auth records refers to the given grantee user.
 *   return: error code
 *   user(in): the grantee user name to be dropped
 */
int
au_delete_auth_of_dropping_user (MOP user)
{
  int error = NO_ERROR, save;
  const char *sql_query = "DELETE FROM [" CT_CLASSAUTH_NAME "] [au] WHERE [au].[grantee] = ?;";
  DB_VALUE val;
  DB_QUERY_RESULT *result = NULL;
  DB_SESSION *session = NULL;
  int stmt_id;

  db_make_null (&val);

  /* Disable the checking for internal authorization object access */
  AU_DISABLE (save);

  assert (user != NULL);

  session = db_open_buffer_local (sql_query);
  if (session == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      goto exit;
    }

  error = db_set_system_generated_statement (session);
  if (error != NO_ERROR)
    {
      goto release;
    }

  stmt_id = db_compile_statement_local (session);
  if (stmt_id < 0)
    {
      ASSERT_ERROR_AND_SET (error);
      goto release;
    }

  db_make_object (&val, user);
  error = db_push_values (session, 1, &val);
  if (error != NO_ERROR)
    {
      goto release;
    }

  error = db_execute_statement_local (session, stmt_id, &result);
  if (error < 0)
    {
      goto release;
    }

  error = db_query_end (result);

release:
  if (session != NULL)
    {
      db_close_session (session);
    }

exit:
  pr_clear_value (&val);

  AU_ENABLE (save);

  return error;
}

/*
 * au_delete_auth_of_dropping_database_object - delete _db_auth records refers to the given database object.
 *   return: error code
 *   obj_type(in): the object type
 *   name(in): the object name to be dropped
 */
int
au_delete_auth_of_dropping_database_object (DB_OBJECT_TYPE obj_type, const char *name)
{
  int error = NO_ERROR, save;
  const char *sql_query = "DELETE FROM [" CT_CLASSAUTH_NAME "] [au]" " WHERE [au].[class_of] IN (%s);";
  DB_VALUE val;
  DB_QUERY_RESULT *result = NULL;
  DB_SESSION *session = NULL;
  int stmt_id;
  char obj_fetch_query[256];

  db_make_null (&val);

  /* Disable the checking for internal authorization object access */
  AU_DISABLE (save);

  assert (name != NULL);

  switch (obj_type)
    {
    case DB_OBJECT_CLASS:
      sprintf (obj_fetch_query, sql_query, "SELECT [cl].[class_of] FROM " CT_CLASS_NAME "[cl] WHERE [unique_name] = ?");
      break;
    case DB_OBJECT_PROCEDURE:
      sprintf (obj_fetch_query, sql_query, "SELECT [sp] FROM " CT_STORED_PROC_NAME "[sp] WHERE [sp_name] = ?");
      break;
    default:
      assert (false);
      error = ER_FAILED;
      goto exit;
    }

  session = db_open_buffer_local (obj_fetch_query);
  if (session == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      goto exit;
    }

  error = db_set_system_generated_statement (session);
  if (error != NO_ERROR)
    {
      goto release;
    }

  stmt_id = db_compile_statement_local (session);
  if (stmt_id < 0)
    {
      ASSERT_ERROR_AND_SET (error);
      goto release;
    }

  db_make_string (&val, name);
  error = db_push_values (session, 1, &val);
  if (error != NO_ERROR)
    {
      goto release;
    }

  error = db_execute_statement_local (session, stmt_id, &result);
  if (error < 0)
    {
      goto release;
    }

  error = db_query_end (result);

release:
  if (session != NULL)
    {
      db_close_session (session);
    }

exit:
  pr_clear_value (&val);

  AU_ENABLE (save);

  return error;
}
