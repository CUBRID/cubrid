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

static int update_authorization_for_new_owner (DB_OBJECT_TYPE obj_type, MOP new_owner_mop, const char *unique_name,
    int *row_count);
static int update_auth_for_new_owner (DB_OBJECT_TYPE obj_type, MOP new_owner_mop, const char *unique_name);

struct authorization_keyhash
{
  std::size_t operator() (const std::tuple<MOP, MOP> &k) const
  {
    return std::hash<MOP>() (std::get<0> (k)) ^
	   std::hash<MOP>() (std::get<1> (k));
  }
};

struct authorization_keyequal
{
  bool operator() (const std::tuple<MOP, MOP> &lhs,
		   const std::tuple<MOP, MOP> &rhs) const
  {
    return lhs == rhs;
  }
};

struct auth_keyhash
{
  std::size_t operator() (const std::tuple<MOP, MOP, MOP, DB_AUTH> &k) const
  {
    return std::hash<MOP>() (std::get<0> (k)) ^
	   std::hash<MOP>() (std::get<1> (k)) ^
	   std::hash<MOP>() (std::get<2> (k)) ^
	   std::hash<DB_AUTH>() (std::get<3> (k));
  }
};

struct auth_keyequal
{
  bool operator() (const std::tuple<MOP, MOP, MOP, DB_AUTH> &lhs,
		   const std::tuple<MOP, MOP, MOP, DB_AUTH> &rhs) const
  {
    return lhs == rhs;
  }
};

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

au_auth_accessor::au_auth_accessor ()
  : m_au_obj (nullptr)
  , m_au_class_mop (nullptr)
{}

int
au_auth_accessor::create_new_auth ()
{
  if (m_au_class_mop == nullptr)
    {
      m_au_class_mop = sm_find_class (CT_CLASSAUTH_NAME);
      if (m_au_class_mop == nullptr)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_AU_MISSING_CLASS, 1, CT_CLASSAUTH_NAME);
	}
    }

  m_au_obj = db_create_internal (m_au_class_mop);
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
  DB_VALUE value;
  MOP db_class = nullptr, inst_mop = nullptr;
  DB_AUTH type;
  int i;
  int error = NO_ERROR;
  char unique_name[DB_MAX_IDENTIFIER_LENGTH + 1];
  unique_name[0] = '\0';

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
      inst_mop = obj_mop;
    }
  else
    {
      // TODO: CBRD-24912
      if (jsp_get_unique_name (obj_mop, unique_name, DB_MAX_IDENTIFIER_LENGTH) == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  pr_clear_value (&value);
	  return er_errid ();
	}

      inst_mop = jsp_find_stored_procedure (unique_name, DB_AUTH_NONE);
      if (inst_mop == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  pr_clear_value (&value);
	  return er_errid ();
	}
    }

  db_make_int (&value, (int) obj_type);
  obj_set (m_au_obj, "object_type", &value);

  db_make_object (&value, inst_mop);
  obj_set (m_au_obj, "object_of", &value);

  for (type = DB_AUTH_SELECT, i = 0; type != auth_type; type = (DB_AUTH) (type << 1), i++);

  db_make_varchar (&value, 7, AU_TYPE_SET[i], AU_TYPE_SET_LEN[i], LANG_SYS_CODESET, LANG_SYS_COLLATION);
  obj_set (m_au_obj, "auth_type", &value);

  db_make_int (&value, (int) grant_option);
  obj_set (m_au_obj, "is_grantable", &value);

  pr_clear_value (&value);
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
	  " WHERE [au].[grantee].[name] = ? AND [au].[grantor].[name] = ?"
	  " AND [au].[object_of] = (%s) AND [au].[auth_type] = ?";
  char obj_fetch_query[256];
  const char *class_unique_name = NULL;
  char sp_unique_name[DB_MAX_IDENTIFIER_LENGTH + 1];
  char error_msg[ERR_MSG_SIZE];

  for (i = 0; i < COUNT_FOR_VARIABLES; i++)
    {
      db_make_null (&val[i]);
    }

  db_make_null (&grant_value);

  /* Disable the checking for internal authorization object access */
  AU_DISABLE (save);

  switch (obj_type)
    {
    case DB_OBJECT_CLASS:
      class_unique_name = sm_get_ch_name (obj_mop);
      if (class_unique_name == NULL)
	{
	  assert (false);
	  error = ER_UNEXPECTED;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, "Cannot get class name of mop.");
	  goto exit;
	}

      sprintf (obj_fetch_query, sql_query, "SELECT [cl].[class_of] FROM " CT_CLASS_NAME "[cl] WHERE [unique_name] = ?");
      break;
    case DB_OBJECT_PROCEDURE:
      sp_unique_name[0] = '\0';
      if (jsp_get_unique_name (obj_mop, sp_unique_name, DB_MAX_IDENTIFIER_LENGTH) == NULL)
	{
	  assert (false);
	  error = ER_UNEXPECTED;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, "Cannot get stored procedure name of mop.");
	  goto exit;
	}

      sprintf (obj_fetch_query, sql_query, "SELECT [sp] FROM " CT_STORED_PROC_NAME "[sp] WHERE [unique_name] = ?");
      break;
    default:
      assert (false);
      error = ER_UNEXPECTED;
      error_msg[0] = '\0';
      snprintf (error_msg, sizeof (error_msg) - 1, "unknown database object id: %d.", obj_type);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, error_msg);
      goto exit;
    }

  session = db_open_buffer_local (obj_fetch_query);
  if (session == NULL)
    {
      assert (er_errid () != NO_ERROR);
      goto exit;
    }

  error = db_set_system_generated_statement (session);
  if (error != NO_ERROR)
    {
      goto release;
    }

  stmt_id = db_compile_statement_local (session);
  if (stmt_id != 1)
    {
      assert (er_errid () != NO_ERROR);
      goto release;
    }

  /* Prepare DB_VALUEs for host variables */
  error = obj_get (user, "name", &val[INDEX_FOR_GRANTEE_NAME]);
  if (error != NO_ERROR)
    {
      goto release;
    }
  else if (!DB_IS_STRING (&val[INDEX_FOR_GRANTEE_NAME]) || DB_IS_NULL (&val[INDEX_FOR_GRANTEE_NAME])
	   || db_get_string (&val[INDEX_FOR_GRANTEE_NAME]) == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_AU_MISSING_OR_INVALID_USER, 0);
      goto release;
    }

  error = obj_get (grantor, "name", &val[INDEX_FOR_GRANTOR_NAME]);
  if (error != NO_ERROR)
    {
      goto release;
    }
  else if (!DB_IS_STRING (&val[INDEX_FOR_GRANTOR_NAME]) || DB_IS_NULL (&val[INDEX_FOR_GRANTOR_NAME])
	   || db_get_string (&val[INDEX_FOR_GRANTOR_NAME]) == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_AU_MISSING_OR_INVALID_USER, 0);
      goto release;
    }

  switch (obj_type)
    {
    case DB_OBJECT_CLASS:
      db_make_string (&val[INDEX_FOR_OBJECT_NAME], class_unique_name);
      break;
    case DB_OBJECT_PROCEDURE:
      db_make_string (&val[INDEX_FOR_OBJECT_NAME], sp_unique_name);
      break;
    default:
      assert (false);
      error = ER_FAILED;
      goto release;
    }

  i = 0;
  for (DB_AUTH type = DB_AUTH_SELECT; type != auth_type; type = (DB_AUTH) (type << 1))
    {
      i++;
    }
  db_make_string (&val[INDEX_FOR_AUTH_TYPE], AU_TYPE_SET[i]);

  error = db_push_values (session, COUNT_FOR_VARIABLES, val);
  if (error != NO_ERROR)
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
  const char *sql_query = "DELETE FROM [" CT_CLASSAUTH_NAME "] [au]" " WHERE [au].[object_of] IN (%s);";
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
      sprintf (obj_fetch_query, sql_query, "SELECT [sp] FROM " CT_STORED_PROC_NAME "[sp] WHERE [unique_name] = ?");
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

/*
 * au_delete_authorizartion_of_dropping_user - delete a db_authorization record refers to the given user.
 *   return: error code
 *   user(in): the user name to be dropped
 */
int
au_delete_authorizartion_of_dropping_user (MOP user)
{
  int error = NO_ERROR, save;
  const char *sql_query = "DELETE FROM [" CT_AUTHORIZATION_NAME "] [au] WHERE [au].[owner] = ?;";
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
 * au_object_revoke_all_privileges - drop a class, virtual class and procedure all privileges are revoked.
 *   return: error code
 *   obj_type(in) : objcet type
 *   grantor_mop(in): grantor user
 *   unique_name(in): class/stored procedure unique_name
 */
int
au_object_revoke_all_privileges (DB_OBJECT_TYPE obj_type, MOP grantor_mop, const char *unique_name)
{
  int error = NO_ERROR, save, len, i = 0;
  const char *auth_type_char;
  DB_AUTH db_auth;
  MOP grantee_mop, object_of_mop;
  DB_VALUE val[2];
  DB_VALUE grantee_value, object_of_value, auth_type_value;
  DB_QUERY_RESULT *result = NULL;
  DB_SESSION *session = NULL;
  int stmt_id;
  int row_count = -1;
  char obj_fetch_query[256];
  const char *sql_query =
	  "SELECT [au].grantee, [au].object_of, [au].auth_type FROM [" CT_CLASSAUTH_NAME "] [au]"
	  " WHERE [au].[grantor].[name] = ? AND [au].[object_of] = (%s);";

  assert (grantor_mop != NULL && unique_name != NULL);

  for (i = 0; i < 2; i++)
    {
      db_make_null (&val[i]);
    }

  db_make_null (&grantee_value);
  db_make_null (&object_of_value);
  db_make_null (&auth_type_value);

  /* Disable the checking for internal authorization object access */
  AU_DISABLE (save);

  switch (obj_type)
    {
    case DB_OBJECT_CLASS:
      sprintf (obj_fetch_query, sql_query, "SELECT [cl].[class_of] FROM " CT_CLASS_NAME "[cl] WHERE [unique_name] = ?");
      break;
    case DB_OBJECT_PROCEDURE:
      sprintf (obj_fetch_query, sql_query, "SELECT [sp] FROM " CT_STORED_PROC_NAME "[sp] WHERE [unique_name] = ?");
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

  /* Prepare DB_VALUEs for host variables */
  error = obj_get (grantor_mop, "name", &val[0]);
  if (error != NO_ERROR)
    {
      goto release;
    }
  else if (!DB_IS_STRING (&val[0]) || DB_IS_NULL (&val[0])
	   || db_get_string (&val[0]) == NULL)
    {
      error = ER_AU_MISSING_OR_INVALID_USER;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      goto release;
    }

  db_make_string (&val[1], unique_name);

  error = db_push_values (session, 2, val);
  if (error != NO_ERROR)
    {
      assert (er_errid () != NO_ERROR);
      goto release;
    }

  error = db_execute_statement_local (session, stmt_id, &result);

  /* The error value is row count if it's not negative value. */
  if (error == 0)
    {
      row_count = error;
      goto release;
    }
  else if (error < 0)
    {
      assert (er_errid () != NO_ERROR);
      goto release;
    }

  row_count = error;
  error = NO_ERROR;

  while (db_query_next_tuple (result) == DB_CURSOR_SUCCESS)
    {
      if (db_query_get_tuple_value (result, 0, &grantee_value) == NO_ERROR)
	{
	  grantee_mop = NULL;
	  if (!DB_IS_NULL (&grantee_value))
	    {
	      grantee_mop = db_get_object (&grantee_value);
	    }
	  else
	    {
	      goto release;
	    }
	}

      if (db_query_get_tuple_value (result, 1, &object_of_value) == NO_ERROR)
	{
	  object_of_mop = NULL;
	  if (!DB_IS_NULL (&object_of_value))
	    {
	      object_of_mop = db_get_object (&object_of_value);
	    }
	  else
	    {
	      goto release;
	    }
	}

      if (db_query_get_tuple_value (result, 2, &auth_type_value) == NO_ERROR)
	{
	  auth_type_char = NULL;

	  if (!DB_IS_NULL (&auth_type_value))
	    {
	      auth_type_char = db_get_char (&auth_type_value, &len);

	      switch (auth_type_char[0])
		{
		case 'A':
		  db_auth = DB_AUTH_ALTER;
		  break;

		case 'D':
		  db_auth = DB_AUTH_DELETE;
		  break;

		case 'E':
		  db_auth = DB_AUTH_EXECUTE;
		  break;

		case 'I':
		  if (auth_type_char[2] == 'D')
		    {
		      db_auth = DB_AUTH_INDEX;
		    }
		  else if (auth_type_char[2] == 'S')
		    {
		      db_auth = DB_AUTH_INSERT;
		    }
		  else
		    {
		      db_auth = DB_AUTH_NONE;
		    }
		  break;

		case 'S':
		  db_auth = DB_AUTH_SELECT;
		  break;

		case 'U':
		  db_auth = DB_AUTH_UPDATE;
		  break;

		default:
		  db_auth = DB_AUTH_NONE;
		  break;
		}
	    }
	  else
	    {
	      goto release;
	    }
	}

      assert (grantee_mop != NULL && object_of_mop != NULL && db_auth != DB_AUTH_NONE);

      error = au_revoke (obj_type, grantee_mop, object_of_mop, db_auth, NULL);
      if (error != NO_ERROR)
	{
	  goto release;
	}
    }

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

  db_value_clear (&grantee_value);
  db_value_clear (&object_of_value);
  db_value_clear (&auth_type_value);

  for (i = 0; i < 2; i++)
    {
      db_value_clear (&val[i]);
    }

  if (row_count < 0 && er_errid () == NO_ERROR && (grantee_mop == NULL || object_of_mop == NULL
      || db_auth == DB_AUTH_NONE))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      error = ER_GENERIC_ERROR;
    }

  return (error);
}

/*
 * au_user_revoke_all_privileges - when a user is deleted, all of their privileges are revoked.
 *   return: error code
 *   user_mop(in): a user object
 */
int
au_user_revoke_all_privileges (MOP user_mop)
{
  int error = NO_ERROR, save, len;
  int object_type;
  DB_OBJECT_TYPE obj_type;
  const char *auth_type_char;
  DB_AUTH db_auth;
  MOP grantee_mop, obj_mop;
  DB_VALUE name;
  DB_VALUE grantee_value, object_type_value, object_of_value, auth_type_value;
  DB_QUERY_RESULT *result = NULL;
  DB_SESSION *session = NULL;
  int stmt_id;
  int row_count = -1;
  const char *sql_query =
	  "SELECT [au].grantee, [au].object_type, [au].object_of, [au].auth_type FROM [" CT_CLASSAUTH_NAME "] [au]"
	  " WHERE [au].[grantor].[name] = ?";

  assert (user_mop != NULL);


  db_make_null (&name);
  db_make_null (&grantee_value);
  db_make_null (&object_type_value);
  db_make_null (&object_of_value);
  db_make_null (&auth_type_value);

  /* Disable the checking for internal authorization object access */
  AU_DISABLE (save);

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

  /* Prepare DB_VALUEs for host variables */
  error = obj_get (user_mop, "name", &name);
  if (error != NO_ERROR)
    {
      goto release;
    }
  else if (!DB_IS_STRING (&name) || DB_IS_NULL (&name)
	   || db_get_string (&name) == NULL)
    {
      error = ER_AU_MISSING_OR_INVALID_USER;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      goto release;
    }

  error = db_push_values (session, 1, &name);
  if (error != NO_ERROR)
    {
      assert (er_errid () != NO_ERROR);
      goto release;
    }

  error = db_execute_statement_local (session, stmt_id, &result);

  /* The error value is row count if it's not negative value. */
  if (error == 0)
    {
      row_count = error;
      goto release;
    }
  else if (error < 0)
    {
      assert (er_errid () != NO_ERROR);
      goto release;
    }

  row_count = error;
  error = NO_ERROR;

  while (db_query_next_tuple (result) == DB_CURSOR_SUCCESS)
    {
      if (db_query_get_tuple_value (result, 0, &grantee_value) == NO_ERROR)
	{
	  grantee_mop = NULL;
	  if (!DB_IS_NULL (&grantee_value))
	    {
	      grantee_mop = db_get_object (&grantee_value);
	    }
	  else
	    {
	      goto release;
	    }
	}

      if (db_query_get_tuple_value (result, 1, &object_type_value) == NO_ERROR)
	{
	  object_type = 0;
	  if (!DB_IS_NULL (&object_type_value))
	    {
	      object_type = db_get_int (&object_type_value);
	      switch (object_type)
		{
		case 0:
		  obj_type = DB_OBJECT_CLASS;
		  break;

		case 5:
		  obj_type = DB_OBJECT_PROCEDURE;
		  break;

		default:
		  assert (object_type == 0 || object_type == 5);
		  goto release;
		}
	    }
	  else
	    {
	      goto release;
	    }
	}

      if (db_query_get_tuple_value (result, 2, &object_of_value) == NO_ERROR)
	{
	  obj_mop = NULL;
	  if (!DB_IS_NULL (&object_of_value))
	    {
	      obj_mop = db_get_object (&object_of_value);
	    }
	  else
	    {
	      goto release;
	    }
	}

      if (db_query_get_tuple_value (result, 3, &auth_type_value) == NO_ERROR)
	{
	  auth_type_char = NULL;

	  if (!DB_IS_NULL (&auth_type_value))
	    {
	      auth_type_char = db_get_char (&auth_type_value, &len);

	      switch (auth_type_char[0])
		{
		case 'A':
		  db_auth = DB_AUTH_ALTER;
		  break;

		case 'D':
		  db_auth = DB_AUTH_DELETE;
		  break;

		case 'E':
		  db_auth = DB_AUTH_EXECUTE;
		  break;

		case 'I':
		  if (auth_type_char[2] == 'D')
		    {
		      db_auth = DB_AUTH_INDEX;
		    }
		  else if (auth_type_char[2] == 'S')
		    {
		      db_auth = DB_AUTH_INSERT;
		    }
		  else
		    {
		      db_auth = DB_AUTH_NONE;
		    }
		  break;

		case 'S':
		  db_auth = DB_AUTH_SELECT;
		  break;

		case 'U':
		  db_auth = DB_AUTH_UPDATE;
		  break;

		default:
		  db_auth = DB_AUTH_NONE;
		  break;
		}
	    }
	  else
	    {
	      goto release;
	    }
	}

      assert (grantee_mop != NULL && obj_mop != NULL && db_auth != DB_AUTH_NONE);

      error = au_revoke (obj_type, grantee_mop, obj_mop, db_auth, user_mop);
      if (error != NO_ERROR)
	{
	  goto release;
	}
    }

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

  db_value_clear (&grantee_value);
  db_value_clear (&object_type_value);
  db_value_clear (&object_of_value);
  db_value_clear (&auth_type_value);
  db_value_clear (&name);

  if (row_count < 0 && er_errid () == NO_ERROR && (grantee_mop == NULL || obj_mop == NULL
      || db_auth == DB_AUTH_NONE || (object_type != 0 && object_type != 5)))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      error = ER_GENERIC_ERROR;
    }

  return (error);
}

/*
 * au_object_owner_change_privileges
 *   return: error code
 *   obj_type(in): the object type
 *   new_owner_mop(in): class/stored procedure new owner
 *   unique_name(in):
 * NOTE
 * When the owner of a class, virtual class, or procedure is changed, the previous owner's privileges are transferred to the new owner.
 *
 * However, if the new owner already possesses the privileges granted by the previous owner, those privileges are removed.
 * Reason: The REVOKE statement cannot revoke privileges from the owner.
 */
int
au_object_owner_change_privileges (DB_OBJECT_TYPE obj_type, MOP new_owner_mop, const char *unique_name)
{
  int error = NO_ERROR, save;
  int update_count_db_authorization = 0;

  assert (new_owner_mop != NULL && unique_name != NULL);

  AU_DISABLE (save);

  /* 1. db_authorization 카탈로그 수정
  error = update_authorization_for_new_owner (obj_type, new_owner_mop, unique_name, &update_count_db_authorization);
  if (error != NO_ERROR)
    {
      ASSERT_ERROR_AND_SET (error);
      goto exit;
    }
  */
  // 2. _db_auth 카탈로그 수정
  if (update_count_db_authorization)
    {
      // 2. _db_auth 카탈로그 삭제 & 기록
      error = update_auth_for_new_owner (obj_type, new_owner_mop, unique_name);
      if (error != NO_ERROR)
	{
	  ASSERT_ERROR_AND_SET (error);
	  goto exit;
	}
    }

exit:
  AU_ENABLE (save);

  return (error);
}

static int
update_authorization_for_new_owner (DB_OBJECT_TYPE obj_type, MOP new_owner_mop, const char *unique_name, int *row_count)
{
  int error = NO_ERROR, save;
  char obj_fetch_query[256];
  const char *sql_query =
	  "SELECT [au].grantee, [au].object_of FROM [" CT_CLASSAUTH_NAME "] [au]"
	  " WHERE [au].[object_of] = (%s)"
	  " GROUP BY [au].grantee";
  DB_VALUE val;
  DB_SESSION *session = NULL;
  int stmt_id;
  DB_QUERY_RESULT *result = NULL;
  DB_VALUE grantee_value, object_of_value;
  MOP grantee_mop, object_of_mop, auth, new_auth;
  DB_SET *grants = NULL, *new_grants = NULL;
  int gindex, gsize, new_gindex;
  std::unordered_map<std::tuple<MOP, MOP>, int, authorization_keyhash, authorization_keyequal>
  authorization_unordered_map;

  assert (new_owner_mop != NULL && unique_name != NULL);

  db_make_null (&val);
  db_make_null (&grantee_value);
  db_make_null (&object_of_value);

  //AU_DISABLE (save);

  switch (obj_type)
    {
    case DB_OBJECT_CLASS:
      sprintf (obj_fetch_query, sql_query, "SELECT [cl].[class_of] FROM " CT_CLASS_NAME "[cl] WHERE [unique_name] = ?");
      break;
    case DB_OBJECT_PROCEDURE:
      sprintf (obj_fetch_query, sql_query, "SELECT [sp] FROM " CT_STORED_PROC_NAME "[sp] WHERE [unique_name] = ?");
      break;
    default:
      assert (false);
      error = ER_FAILED;
      goto exit;
    }

  /* 1. 쿼리 조회 */
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

  /* Prepare DB_VALUEs for host variables */
  db_make_string (&val, unique_name);

  error = db_push_values (session, 1, &val);
  if (error != NO_ERROR)
    {
      ASSERT_ERROR_AND_SET (error);
      goto release;
    }

  error = db_execute_statement_local (session, stmt_id, &result);

  /* The error value is row count if it's not negative value. */
  if (error == 0)
    {
      *row_count = error;
      goto release;
    }
  else if (error < 0)
    {
      ASSERT_ERROR_AND_SET (error);
      goto release;
    }

  *row_count = error;
  error = NO_ERROR;

  /* 2. 결과 값을 가지고 _db_auth와 db_authorization 카탈로그 수정 */
  while (db_query_next_tuple (result) == DB_CURSOR_SUCCESS)
    {
      if (db_query_get_tuple_value (result, 0, &grantee_value) == NO_ERROR)
	{
	  grantee_mop = NULL;
	  if (!DB_IS_NULL (&grantee_value))
	    {
	      grantee_mop = db_get_object (&grantee_value);
	    }
	  else
	    {
	      goto release;
	    }
	}

      if (db_query_get_tuple_value (result, 1, &object_of_value) == NO_ERROR)
	{
	  object_of_mop = NULL;
	  if (!DB_IS_NULL (&object_of_value))
	    {
	      object_of_mop = db_get_object (&object_of_value);
	    }
	  else
	    {
	      goto release;
	    }
	}

      assert (grantee_mop != NULL && object_of_mop != NULL);

      /* 3. db_authorization 카탈로그의 값을 수정하는 로직 */
      if (au_get_object (grantee_mop, "authorization", &auth) != NO_ERROR)
	{
	  error = ER_AU_ACCESS_ERROR;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 2, AU_USER_CLASS_NAME, "authorization");
	  goto release; //break;
	}
      else if (au_fetch_instance (auth, NULL, AU_FETCH_UPDATE, LC_FETCH_MVCC_VERSION, AU_UPDATE) != NO_ERROR)
	// 오브젝트(Instance)를 가져오는 핵심 함수, 메모리에서 먼저 찾고, 없으면 데이터베이스에서 가져옴
	{
	  error = ER_AU_CANT_UPDATE;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	  goto release; //break;
	}
      else if ((error = obj_inst_lock (auth, 1)) == NO_ERROR && (error = get_grants (auth, &grants, 1)) == NO_ERROR)
	{
	  gsize = set_size (grants);
	  for (gindex = 0; gindex < gsize && error == NO_ERROR; gindex += GRANT_ENTRY_LENGTH)
	    {
	      if (set_get_element (grants, GRANT_ENTRY_CLASS (gindex), &val))
		{
		  ASSERT_ERROR_AND_SET (error);
		  goto release;
		}

	      if (db_get_object (&val) == object_of_mop)
		{
		  /*
		   * grantee가 새로운 소유자가될 때, 이전에 부여받은 권한은 삭제
		   * 이유 : 소유자의 권한을 revoke 할 수 없기 때문
		   * grantee_mop : grantee_user
		   * new_owner_mop : grnator_user
		   *
		   * ex) SELECT * FROM db_authorization;
		   *   owner            grants
		   * ================================
		   *   grantee         {..,unique_name, grantor, ..}
		   */
		  if (ws_is_same_object (grantee_mop, new_owner_mop))
		    {
		      // 4-1. db_authorization 카탈로그의 grants 삭제 (grentee == new_owner)
		      drop_grant_entry (grants, gindex);
		      gindex -= GRANT_ENTRY_LENGTH;
		      gsize -= GRANT_ENTRY_LENGTH;
		    }
		  else
		    {
		      // 4-2. db_authorization 카탈로그의 grants 변경 (grentee != new_owner)
		      int current_cache;

		      error = set_get_element (grants, GRANT_ENTRY_CACHE (gindex), &val);
		      current_cache = db_get_int (&val);

		      //기록, mask를 합쳐야하고, 지워야하고
		      auto key = std::make_tuple (grantee_mop, object_of_mop);
		      if (authorization_unordered_map.find (key) == authorization_unordered_map.end())
			{
			  authorization_unordered_map[key] = current_cache;  // 첫 번째 값 저장
			}
		      else
			{
			  authorization_unordered_map[key] |= current_cache;  // 기존 값과 bitwise OR 수행
			}

		      drop_grant_entry (grants, gindex);
		      gindex -= GRANT_ENTRY_LENGTH;
		      gsize -= GRANT_ENTRY_LENGTH;
		    }
		}
	    }
	}
    }

  // bit 값을 or 수행한거 다시 삽입
  for (const auto &entry : authorization_unordered_map)
    {
      const auto &key = entry.first;
      int current_cache = entry.second;

      if (au_get_object (std::get<0> (key), "authorization", &new_auth) != NO_ERROR)
	{
	  error = ER_AU_ACCESS_ERROR;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 2, AU_USER_CLASS_NAME, "authorization");
	  goto release; //break;
	}
      else if ((error = obj_inst_lock (new_auth, 1)) == NO_ERROR
	       && (error = get_grants (new_auth, &new_grants, 1)) == NO_ERROR)
	{
	  new_gindex = add_grant_entry (new_grants, obj_type, std::get<1> (key), new_owner_mop);
	  db_make_int (&val, current_cache);
	  set_put_element (new_grants, GRANT_ENTRY_CACHE (new_gindex), &val);

	  /* Fail to insert/update, never change the grant entry set. */
	  if (error != NO_ERROR)
	    {
	      goto release;
	    }
	}
    }

  if (obj_type == DB_OBJECT_CLASS)
    {
      SM_CLASS *classobj;

      if ((error = au_fetch_class_force (object_of_mop, &classobj, AU_FETCH_READ)) == NO_ERROR)
	{
	  /*
	   * clear the cache for this user/class pair to make sure we
	   * recalculate it the next time it is referenced
	   */
	  Au_cache.reset_cache_for_user_and_class (classobj);
	}
    }

  /*
   * Make sure any cached parse trees are rebuild.  This proabably
   * isn't necessary for GRANT, only REVOKE.
   */
  sm_bump_local_schema_version ();

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
  //AU_ENABLE (save);

  pr_clear_value (&val);
  pr_clear_value (&grantee_value);
  pr_clear_value (&object_of_value);

  if (grants != NULL)
    {
      set_free (grants);
    }

  if (new_grants != NULL)
    {
      set_free (new_grants);
    }

  if (grantee_mop == NULL && object_of_mop == NULL && er_errid () == NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      error = ER_GENERIC_ERROR;
    }

  return (error);
}

static int
update_auth_for_new_owner (DB_OBJECT_TYPE obj_type, MOP new_owner_mop, const char *unique_name)
{
  int error = NO_ERROR, save;
  char obj_fetch_query[256];
  const char *sql_query =
	  "SELECT [au].object, [au].grantee, [au].object_of, [au].auth_type, [au].is_grantable FROM [" CT_CLASSAUTH_NAME "] [au]"
	  " WHERE [au].[object_of] = (%s)";
  DB_SESSION *session = NULL;
  int stmt_id;
  DB_QUERY_RESULT *result = NULL;
  DB_VALUE val, db_auth_object_value, grantee_value, object_of_value, auth_type_value, is_grantable_value;
  MOP db_auth_object_mop, grantee_mop, object_of_mop;
  const char *auth_type_char;
  int len;
  DB_AUTH db_auth;
  int is_grantable;
  MOP auth;
  size_t au_db_auth_size;
  au_auth_accessor accessor;
  std::unordered_map<std::tuple<MOP, MOP, MOP, DB_AUTH>, int, auth_keyhash, auth_keyequal> auth_unordered_map;

  assert (new_owner_mop != NULL && unique_name != NULL);

  db_make_null (&val);
  db_make_null (&db_auth_object_value);
  db_make_null (&grantee_value);
  db_make_null (&object_of_value);
  db_make_null (&auth_type_value);
  db_make_null (&is_grantable_value);

  //AU_DISABLE (save);

  switch (obj_type)
    {
    case DB_OBJECT_CLASS:
      sprintf (obj_fetch_query, sql_query, "SELECT [c].[class_of] FROM " CT_CLASS_NAME "[c] WHERE [unique_name] = ?");
      break;
    case DB_OBJECT_PROCEDURE:
      sprintf (obj_fetch_query, sql_query, "SELECT [sp] FROM " CT_STORED_PROC_NAME "[sp] WHERE [unique_name] = ?");
      break;
    default:
      assert (false);
      error = ER_FAILED;
      goto exit;
    }

  /* 1. 쿼리 조회 */
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

  /* Prepare DB_VALUEs for host variables */
  db_make_string (&val, unique_name);

  error = db_push_values (session, 1, &val);
  if (error != NO_ERROR)
    {
      ASSERT_ERROR_AND_SET (error);
      goto release;
    }

  error = db_execute_statement_local (session, stmt_id, &result);

  /* The error value is row count if it's not negative value. */
  if (error == 0)
    {
      error = ER_GENERIC_ERROR;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      goto release;
    }
  else if (error < 0)
    {
      ASSERT_ERROR_AND_SET (error);
      goto release;
    }

  error = NO_ERROR;
  while (db_query_next_tuple (result) == DB_CURSOR_SUCCESS)
    {
      /* 2-1. [au].object */
      if (db_query_get_tuple_value (result, 0, &db_auth_object_value) == NO_ERROR)
	{
	  db_auth_object_mop = NULL;
	  if (!DB_IS_NULL (&db_auth_object_value))
	    {
	      db_auth_object_mop = db_get_object (&db_auth_object_value);
	    }
	}

      /* 2-2. [au].grnatee */
      if (db_query_get_tuple_value (result, 1, &grantee_value) == NO_ERROR)
	{
	  grantee_mop = NULL;
	  if (!DB_IS_NULL (&grantee_value))
	    {
	      grantee_mop = db_get_object (&grantee_value);
	    }
	  else
	    {
	      goto release;
	    }
	}

      /* 2-3. [au].object_of */
      if (db_query_get_tuple_value (result, 2, &object_of_value) == NO_ERROR)
	{
	  object_of_mop = NULL;
	  if (!DB_IS_NULL (&object_of_value))
	    {
	      object_of_mop = db_get_object (&object_of_value);
	    }
	  else
	    {
	      goto release;
	    }
	}

      /* 2-4. [au].auth_type */
      if (db_query_get_tuple_value (result, 3, &auth_type_value) == NO_ERROR)
	{
	  auth_type_char = NULL;

	  if (!DB_IS_NULL (&auth_type_value))
	    {
	      auth_type_char = db_get_char (&auth_type_value, &len);

	      switch (auth_type_char[0])
		{
		case 'A':
		  db_auth = DB_AUTH_ALTER;
		  break;

		case 'D':
		  db_auth = DB_AUTH_DELETE;
		  break;

		case 'E':
		  db_auth = DB_AUTH_EXECUTE;
		  break;

		case 'I':
		  if (auth_type_char[2] == 'D')
		    {
		      db_auth = DB_AUTH_INDEX;
		    }
		  else if (auth_type_char[2] == 'S')
		    {
		      db_auth = DB_AUTH_INSERT;
		    }
		  else
		    {
		      db_auth = DB_AUTH_NONE;
		    }
		  break;

		case 'S':
		  db_auth = DB_AUTH_SELECT;
		  break;

		case 'U':
		  db_auth = DB_AUTH_UPDATE;
		  break;

		default:
		  db_auth = DB_AUTH_NONE;
		  break;
		}
	    }
	  else
	    {
	      goto release;
	    }
	}

      /* 2-5. [au].is_grantable */
      if (db_query_get_tuple_value (result, 4, &is_grantable_value) == NO_ERROR)
	{
	  is_grantable = -1;
	  if (!DB_IS_NULL (&is_grantable_value))
	    {
	      is_grantable = db_get_int (&is_grantable_value);
	    }
	  else
	    {
	      goto release;
	    }
	}

      assert (db_auth_object_mop != NULL && grantee_mop != NULL && object_of_mop != NULL && db_auth != DB_AUTH_NONE
	      && is_grantable != -1 );

      /*
       * grantee가 새로운 소유자가될 때, 이전에 부여받은 권한은 삭제
       * 이유 : 소유자의 권한을 revoke 할 수 없기 때문
       * grantee_mop : grantee_user
       * new_owner_mop : grnator_user
       *
       * ex) SELECT * FROM db_authorization;
       *   owner            grants
       * ================================
       *   grantee         {..,unique_name, grantor, ..}
       */
      if (ws_is_same_object (grantee_mop, new_owner_mop))
	{
	  /* 5-1. db_auth 카탈로그 삭제 */
	  //삭제 (accessor.delete_auth)
	  error = obj_inst_lock (db_auth_object_mop, 1);
	  if (error != NO_ERROR)
	    {
	      ASSERT_ERROR_AND_SET (error);
	      goto release; //break;
	    }

	  error = obj_delete (db_auth_object_mop);
	  if (error != NO_ERROR)
	    {
	      ASSERT_ERROR_AND_SET (error);
	      goto release; //break;
	    }
	}
      else
	{
	  /* 5-2. db_auth 카탈로그 기록 및 삭제 */
	  //기록 및 병합
	  auto key = std::make_tuple (new_owner_mop, grantee_mop, object_of_mop, db_auth);

	  printf ("row : %d \n", is_grantable);

	  // 중복된 키가 있으면 grant_option을 업데이트 (더 큰 값 유지)
	  if (auth_unordered_map.find (key) == auth_unordered_map.end() || auth_unordered_map[key] < is_grantable)
	    {
	      auth_unordered_map[key] = is_grantable;
	    }

	  //삭제 (accessor.delete_auth)
	  error = obj_inst_lock (db_auth_object_mop, 1);
	  if (error != NO_ERROR)
	    {
	      ASSERT_ERROR_AND_SET (error);
	      goto release; //break;
	    }

	  error = obj_delete (db_auth_object_mop);
	  if (error != NO_ERROR)
	    {
	      ASSERT_ERROR_AND_SET (error);
	      goto release; //break;
	    }
	}
    }

  // 병합해서 기록한거 다시 추가
  for (const auto &entry : auth_unordered_map)
    {
      const auto &key = entry.first;
      int map_grant_option = entry.second;

      error =
	      accessor.insert_auth (obj_type, std::get<0> (key), std::get<1> (key), std::get<2> (key), std::get<3> (key),
				    (map_grant_option) ? std::get<3> (key) : DB_AUTH_NONE);
    }


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
  //AU_ENABLE (save);

  db_value_clear (&val);
  db_value_clear (&db_auth_object_value);
  db_value_clear (&grantee_value);
  db_value_clear (&object_of_value);
  db_value_clear (&auth_type_value);
  db_value_clear (&is_grantable_value);

  if (db_auth_object_mop == NULL && grantee_mop == NULL && object_of_mop == NULL &&
      db_auth == DB_AUTH_NONE && is_grantable == -1 && er_errid () == NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      error = ER_GENERIC_ERROR;
    }

  return (error);
}
