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

static int update_authorization_for_new_owner (DB_OBJECT_TYPE obj_type, MOP old_owner_mop, MOP new_owner_mop,
    const char *unique_name,
    int *row_count);
static int update_auth_for_new_owner (DB_OBJECT_TYPE obj_type, MOP old_owner_mop, MOP new_owner_mop,
				      const char *unique_name);

using AuthorizationKey = std::tuple<MOP, MOP, MOP>;
using AuthKey = std::tuple<MOP, MOP, MOP, DB_AUTH>;

inline void hash_combine (std::size_t &seed, std::size_t hash)
{
  /* referenced boost::hash_combine. */
  seed ^= hash + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

template <typename Tuple, std::size_t Index>
struct TupleHashHelper
{
  static std::size_t hash (const Tuple &t)
  {
    std::size_t seed = TupleHashHelper<Tuple, Index - 1>::hash (t);
    std::size_t element_hash = std::hash<typename std::tuple_element<Index - 1, Tuple>::type>() (std::get<Index - 1> (t));

    hash_combine (seed, element_hash);
    return seed;
  }
};

template <typename Tuple>
struct TupleHashHelper<Tuple, 0>
{
  static std::size_t hash (const Tuple &)
  {
    return 0;
  }
};

template<typename Tuple>
struct tuple_hash
{
  std::size_t operator() (const Tuple &t) const
  {
    return TupleHashHelper<Tuple, std::tuple_size<Tuple>::value>::hash (t);
  }
};

template <typename Tuple>
struct tuple_equal
{
  bool operator() (const Tuple &lhs, const Tuple &rhs) const
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

constexpr int AU_TYPE_SET_LEN[] =
{
  sizeof ("SELECT") - 1,		/* DB_AUTH_SELECT */
  sizeof ("INSERT") - 1,		/* DB_AUTH_INSERT */
  sizeof ("UPDATE") - 1,		/* DB_AUTH_UPDATE */
  sizeof ("DELETE") - 1,		/* DB_AUTH_DELETE */
  sizeof ("ALTER") - 1,		/* DB_AUTH_ALTER */
  sizeof ("INDEX") - 1,		/* DB_AUTH_INDEX */
  sizeof ("EXECUTE") - 1		/* DB_AUTH_EXECUTE */
};

au_auth_accessor::au_auth_accessor ()
  : m_au_class_mop (nullptr)
  , m_au_obj (nullptr)
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
  int error = NO_ERROR, save, i = 0;
  const char *auth_type_char;
  DB_AUTH db_auth = DB_AUTH_NONE;
  MOP grantee_mop = NULL, object_of_mop = NULL;
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
      goto exit;
    }

  stmt_id = db_compile_statement_local (session);
  if (stmt_id < 0)
    {
      ASSERT_ERROR_AND_SET (error);
      goto exit;
    }

  /* Prepare DB_VALUEs for host variables */
  error = obj_get (grantor_mop, "name", &val[0]);
  if (error != NO_ERROR)
    {
      goto exit;
    }
  else if (!DB_IS_STRING (&val[0]) || DB_IS_NULL (&val[0])
	   || db_get_string (&val[0]) == NULL)
    {
      error = ER_AU_MISSING_OR_INVALID_USER;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      goto exit;
    }

  db_make_string (&val[1], unique_name);

  error = db_push_values (session, 2, val);
  if (error != NO_ERROR)
    {
      assert (er_errid () != NO_ERROR);
      goto exit;
    }

  error = db_execute_statement_local (session, stmt_id, &result);

  /* The error value is row count if it's not negative value. */
  if (error == 0)
    {
      row_count = error;
      goto exit;
    }
  else if (error < 0)
    {
      assert (er_errid () != NO_ERROR);
      goto exit;
    }

  row_count = error;
  error = NO_ERROR;

  while (db_query_next_tuple (result) == DB_CURSOR_SUCCESS)
    {
      if (db_query_get_tuple_value (result, 0, &grantee_value) == NO_ERROR)
	{
	  if (DB_IS_NULL (&grantee_value))
	    {
	      goto exit;
	    }

	  grantee_mop = db_get_object (&grantee_value);
	}

      if (db_query_get_tuple_value (result, 1, &object_of_value) == NO_ERROR)
	{
	  if (DB_IS_NULL (&object_of_value))
	    {
	      goto exit;
	    }

	  object_of_mop = db_get_object (&object_of_value);
	}

      if (db_query_get_tuple_value (result, 2, &auth_type_value) == NO_ERROR)
	{
	  auth_type_char = NULL;

	  if (DB_IS_NULL (&auth_type_value))
	    {
	      goto exit;
	    }

	  auth_type_char = db_get_char (&auth_type_value);

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

      assert (grantee_mop != NULL && object_of_mop != NULL && db_auth != DB_AUTH_NONE);

      error = au_revoke (obj_type, grantee_mop, object_of_mop, db_auth, NULL);
      if (error != NO_ERROR)
	{
	  goto exit;
	}
    }

exit:
  if (result != NULL)
    {
      db_query_end (result);
    }
  if (session != NULL)
    {
      db_close_session (session);
    }

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
  int error = NO_ERROR, save;
  int object_type = 0;
  DB_OBJECT_TYPE obj_type = DB_OBJECT_UNKNOWN;
  const char *auth_type_char;
  DB_AUTH db_auth = DB_AUTH_NONE;
  MOP grantee_mop = NULL, obj_mop = NULL;
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
      goto exit;
    }

  stmt_id = db_compile_statement_local (session);
  if (stmt_id < 0)
    {
      ASSERT_ERROR_AND_SET (error);
      goto exit;
    }

  /* Prepare DB_VALUEs for host variables */
  error = obj_get (user_mop, "name", &name);
  if (error != NO_ERROR)
    {
      goto exit;
    }
  else if (!DB_IS_STRING (&name) || DB_IS_NULL (&name)
	   || db_get_string (&name) == NULL)
    {
      error = ER_AU_MISSING_OR_INVALID_USER;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      goto exit;
    }

  error = db_push_values (session, 1, &name);
  if (error != NO_ERROR)
    {
      assert (er_errid () != NO_ERROR);
      goto exit;
    }

  error = db_execute_statement_local (session, stmt_id, &result);

  /* The error value is row count if it's not negative value. */
  if (error == 0)
    {
      row_count = error;
      goto exit;
    }
  else if (error < 0)
    {
      assert (er_errid () != NO_ERROR);
      goto exit;
    }

  row_count = error;
  error = NO_ERROR;

  while (db_query_next_tuple (result) == DB_CURSOR_SUCCESS)
    {
      if (db_query_get_tuple_value (result, 0, &grantee_value) == NO_ERROR)
	{
	  if (DB_IS_NULL (&grantee_value))
	    {
	      goto exit;
	    }

	  grantee_mop = db_get_object (&grantee_value);
	}

      if (db_query_get_tuple_value (result, 1, &object_type_value) == NO_ERROR)
	{
	  if (DB_IS_NULL (&object_type_value))
	    {
	      goto exit;
	    }

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
	      goto exit;
	    }
	}

      if (db_query_get_tuple_value (result, 2, &object_of_value) == NO_ERROR)
	{
	  if (DB_IS_NULL (&object_of_value))
	    {
	      goto exit;
	    }

	  obj_mop = db_get_object (&object_of_value);
	}

      if (db_query_get_tuple_value (result, 3, &auth_type_value) == NO_ERROR)
	{
	  auth_type_char = NULL;

	  if (DB_IS_NULL (&auth_type_value))
	    {
	      goto exit;
	    }

	  auth_type_char = db_get_char (&auth_type_value);

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

      assert (grantee_mop != NULL && obj_type != DB_OBJECT_UNKNOWN && obj_mop != NULL && db_auth != DB_AUTH_NONE);

      error = au_revoke (obj_type, grantee_mop, obj_mop, db_auth, user_mop);
      if (error != NO_ERROR)
	{
	  goto exit;
	}
    }

exit:
  if (result != NULL)
    {
      db_query_end (result);
    }
  if (session != NULL)
    {
      db_close_session (session);
    }

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
 *   old_owner_mop(in): class/stored procedure old owner
 *   new_owner_mop(in): class/stored procedure new owner
 *   unique_name(in):
 * NOTE
 * When the owner of a class, virtual class, or procedure is changed, the previous owner's privileges are transferred to the new owner.
 *
 * However, if the new owner already possesses the privileges granted by the previous owner, those privileges are removed.
 * Reason: The REVOKE statement cannot revoke privileges from the owner.
 */
int
au_object_owner_change_privileges (DB_OBJECT_TYPE obj_type, MOP object_mop, MOP old_owner_mop, MOP new_owner_mop,
				   const char *unique_name)
{
  int error = NO_ERROR;
  int update_count_db_authorization = 0;

  assert (old_owner_mop != NULL && new_owner_mop != NULL && unique_name != NULL);

  /* modify db_authorization catalog */
  error = update_authorization_for_new_owner (obj_type, old_owner_mop, new_owner_mop, unique_name,
	  &update_count_db_authorization);
  if (error != NO_ERROR)
    {
      ASSERT_ERROR_AND_SET (error);
      goto exit;
    }

  /* if there are no results from querying the db authorization catalog, there is no need to check db_auth. */
  if (update_count_db_authorization)
    {
      /* modify db_auth catalog */
      error = update_auth_for_new_owner (obj_type, old_owner_mop, new_owner_mop, unique_name);
      if (error != NO_ERROR)
	{
	  ASSERT_ERROR_AND_SET (error);
	  goto exit;
	}

      if (obj_type == DB_OBJECT_CLASS)
	{
	  SM_CLASS *classobj;
	  if ((error = au_fetch_class_force (object_mop, &classobj, AU_FETCH_READ)) == NO_ERROR)
	    {
	      /*
	       * clear the cache for this user/class pair to make sure we
	       * recalculate it the next time it is referenced
	       */
	      Au_cache.reset_cache_for_user_and_class (classobj);
	    }
	}

      /*
       * Make sure that we don't keep any parse trees
       * around that rely on obsolete authorization.
       * This may not be necessary.
       */
      sm_bump_local_schema_version ();
    }

exit:
  return (error);
}

static int
update_authorization_for_new_owner (DB_OBJECT_TYPE obj_type, MOP old_owner_mop, MOP new_owner_mop,
				    const char *unique_name, int *row_count)
{
  int error = NO_ERROR, save, current_cache;
  char obj_fetch_query[256];
  const char *sql_query =
	  "SELECT [au].grantee, [au].object_of FROM [" CT_CLASSAUTH_NAME "] [au]"
	  " WHERE [au].[object_of] = (%s)"
	  " GROUP BY [au].grantee";
  DB_VALUE val, element, grantee_value, object_of_value;
  DB_SESSION *session = NULL;
  int stmt_id;
  DB_QUERY_RESULT *result = NULL;
  MOP grantor_mop = NULL, grantee_mop = NULL, object_of_mop = NULL, auth = NULL;
  DB_SET *grants = NULL;
  int gindex, gsize;
  std::unordered_map<AuthorizationKey, int,
      tuple_hash<AuthorizationKey>, tuple_equal<AuthorizationKey>> authorization_unordered_map;
  AuthorizationKey key;

  *row_count = -1;

  assert (old_owner_mop != NULL && new_owner_mop != NULL && unique_name != NULL);

  AU_DISABLE (save);

  db_make_null (&val);
  db_make_null (&element);
  db_make_null (&grantee_value);
  db_make_null (&object_of_value);

  switch (obj_type)
    {
    case DB_OBJECT_CLASS:
      sprintf (obj_fetch_query, sql_query, "SELECT [cl].[class_of] FROM " CT_CLASS_NAME "[cl] WHERE [unique_name] = ?");
      break;
    case DB_OBJECT_PROCEDURE:
      sprintf (obj_fetch_query, sql_query, "SELECT [sp] FROM " CT_STORED_PROC_NAME "[sp] WHERE [unique_name] = ?");
      break;
    default:
      error = ER_FAILED;
      ASSERT_ERROR_AND_SET (error);
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
      goto exit;
    }

  stmt_id = db_compile_statement_local (session);
  if (stmt_id < 0)
    {
      ASSERT_ERROR_AND_SET (error);
      goto exit;
    }

  /* Prepare DB_VALUEs for host variables */
  db_make_string (&val, unique_name);

  error = db_push_values (session, 1, &val);
  if (error != NO_ERROR)
    {
      ASSERT_ERROR_AND_SET (error);
      goto exit;
    }

  error = db_execute_statement_local (session, stmt_id, &result);

  /* The error value is row count if it's not negative value. */
  if (error == 0)
    {
      *row_count = error;
      goto exit;
    }
  else if (error < 0)
    {
      ASSERT_ERROR_AND_SET (error);
      goto exit;
    }

  *row_count = error;
  error = NO_ERROR;

  while (db_query_next_tuple (result) == DB_CURSOR_SUCCESS)
    {
      if (db_query_get_tuple_value (result, 0, &grantee_value) == NO_ERROR)
	{
	  if (DB_IS_NULL (&grantee_value))
	    {
	      goto exit;
	    }

	  grantee_mop = db_get_object (&grantee_value);
	}

      if (db_query_get_tuple_value (result, 1, &object_of_value) == NO_ERROR)
	{
	  if (DB_IS_NULL (&object_of_value))
	    {
	      goto exit;
	    }

	  object_of_mop = db_get_object (&object_of_value);
	}

      assert (grantee_mop != NULL && object_of_mop != NULL);

      if (au_get_object (grantee_mop, "authorization", &auth) != NO_ERROR)
	{
	  error = ER_AU_ACCESS_ERROR;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 2, AU_USER_CLASS_NAME, "authorization");
	  goto exit;
	}
      else if (au_fetch_instance (auth, NULL, AU_FETCH_UPDATE, LC_FETCH_MVCC_VERSION, AU_UPDATE) != NO_ERROR)
	{
	  error = ER_AU_CANT_UPDATE;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	  goto exit;
	}
      else if ((error = obj_inst_lock (auth, 1)) == NO_ERROR && (error = get_grants (auth, &grants, 1)) == NO_ERROR)
	{
	  gsize = set_size (grants);
	  for (gindex = 0; gindex < gsize && error == NO_ERROR; gindex += GRANT_ENTRY_LENGTH)
	    {
	      error = set_get_element (grants, GRANT_ENTRY_CLASS (gindex), &element);
	      if (error != NO_ERROR)
		{
		  ASSERT_ERROR_AND_SET (error);
		  goto exit;
		}

	      if (ws_is_same_object (db_get_object (&element), object_of_mop))
		{
		  /*
		   * when the grantee becomes the new owner, previously granted privileges are removed.
		   * reason: privileges of the owner cannot be revoked.
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
		      /* privileges cannot be granted to the owner, so they are removed immediately without being temporarily stored. */
		    }
		  else
		    {
		      error = set_get_element (grants, GRANT_ENTRY_SOURCE (gindex), &element);
		      if (error != NO_ERROR)
			{
			  ASSERT_ERROR_AND_SET (error);
			  goto exit;
			}
		      grantor_mop = db_get_object (&element);

		      error = set_get_element (grants, GRANT_ENTRY_CACHE (gindex), &element);
		      if (error != NO_ERROR)
			{
			  ASSERT_ERROR_AND_SET (error);
			  goto exit;
			}
		      current_cache = db_get_int (&element);

		      /* before deleting the data in db_authorization, merge the data and temp store it. */
		      std::get<0> (key) = ws_is_same_object (grantor_mop, old_owner_mop) ? new_owner_mop : grantor_mop;
		      std::get<1> (key) = grantee_mop;
		      std::get<2> (key) = object_of_mop;

		      if (authorization_unordered_map.find (key) == authorization_unordered_map.end())
			{
			  /* storing unique data */
			  authorization_unordered_map[key] = current_cache;
			}
		      else
			{
			  /* update the mask value of duplicated data. */
			  authorization_unordered_map[key] |= current_cache;
			}
		    }

		  drop_grant_entry (grants, gindex);
		  gindex -= GRANT_ENTRY_LENGTH;
		  gsize -= GRANT_ENTRY_LENGTH;
		}
	    }
	}
    }

  /* reinsert the merged temp data */
  for (const auto &entry : authorization_unordered_map)
    {
      const auto &key = entry.first;
      current_cache = entry.second;

      if (au_get_object (std::get<1> (key), "authorization", &auth) != NO_ERROR)
	{
	  error = ER_AU_ACCESS_ERROR;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 2, AU_USER_CLASS_NAME, "authorization");
	  goto exit;
	}
      else if ((error = obj_inst_lock (auth, 1)) == NO_ERROR
	       && (error = get_grants (auth, &grants, 1)) == NO_ERROR)
	{
	  gindex = add_grant_entry (grants, obj_type, std::get<2> (key), std::get<0> (key));
	  db_make_int (&element, current_cache);
	  error = set_put_element (grants, GRANT_ENTRY_CACHE (gindex), &element);

	  /* Fail to insert, never change the grant entry set. */
	  if (error != NO_ERROR)
	    {
	      goto exit;
	    }
	}
    }

exit:
  if (result != NULL)
    {
      db_query_end (result);
    }
  if (session != NULL)
    {
      db_close_session (session);
    }

  pr_clear_value (&val);
  pr_clear_value (&element);
  pr_clear_value (&grantee_value);
  pr_clear_value (&object_of_value);

  if (grants != NULL)
    {
      set_free (grants);
    }

  if (*row_count < 0 && grantor_mop == NULL && grantee_mop == NULL && object_of_mop == NULL
      && er_errid () == NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      error = ER_GENERIC_ERROR;
    }

  AU_ENABLE (save);

  return (error);
}

static int
update_auth_for_new_owner (DB_OBJECT_TYPE obj_type, MOP old_owner_mop, MOP new_owner_mop, const char *unique_name)
{
  int error = NO_ERROR, save;
  char obj_fetch_query[256];
  const char *sql_query =
	  "SELECT [au].object, [au].grantor, [au].grantee, [au].object_of, [au].auth_type, [au].is_grantable FROM ["
	  CT_CLASSAUTH_NAME "] [au]"
	  " WHERE [au].[object_of] = (%s)";
  DB_SESSION *session = NULL;
  int stmt_id;
  DB_QUERY_RESULT *result = NULL;
  DB_VALUE val, db_auth_object_value, grantor_value, grantee_value, object_of_value, auth_type_value, is_grantable_value;
  MOP db_auth_object_mop = NULL, grantor_mop = NULL, grantee_mop = NULL, object_of_mop = NULL;
  const char *auth_type_char;
  DB_AUTH db_auth = DB_AUTH_NONE;
  int is_grantable = -1;
  MOP auth;
  size_t au_db_auth_size;
  au_auth_accessor accessor;
  std::unordered_map<AuthKey, int, tuple_hash<AuthKey>, tuple_equal<AuthKey>> auth_unordered_map;
  AuthKey key;

  assert (old_owner_mop != NULL && new_owner_mop != NULL && unique_name != NULL);

  AU_DISABLE (save);

  db_make_null (&val);
  db_make_null (&db_auth_object_value);
  db_make_null (&grantor_value);
  db_make_null (&grantee_value);
  db_make_null (&object_of_value);
  db_make_null (&auth_type_value);
  db_make_null (&is_grantable_value);

  switch (obj_type)
    {
    case DB_OBJECT_CLASS:
      sprintf (obj_fetch_query, sql_query, "SELECT [c].[class_of] FROM " CT_CLASS_NAME "[c] WHERE [unique_name] = ?");
      break;
    case DB_OBJECT_PROCEDURE:
      sprintf (obj_fetch_query, sql_query, "SELECT [sp] FROM " CT_STORED_PROC_NAME "[sp] WHERE [unique_name] = ?");
      break;
    default:
      error = ER_FAILED;
      ASSERT_ERROR_AND_SET (error);
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
      goto exit;
    }

  stmt_id = db_compile_statement_local (session);
  if (stmt_id < 0)
    {
      ASSERT_ERROR_AND_SET (error);
      goto exit;
    }

  /* Prepare DB_VALUEs for host variables */
  db_make_string (&val, unique_name);

  error = db_push_values (session, 1, &val);
  if (error != NO_ERROR)
    {
      ASSERT_ERROR_AND_SET (error);
      goto exit;
    }

  error = db_execute_statement_local (session, stmt_id, &result);

  /* The error value is row count if it's not negative value. */
  if (error == 0)
    {
      error = ER_GENERIC_ERROR;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      goto exit;
    }
  else if (error < 0)
    {
      ASSERT_ERROR_AND_SET (error);
      goto exit;
    }

  error = NO_ERROR;
  while (db_query_next_tuple (result) == DB_CURSOR_SUCCESS)
    {
      if (db_query_get_tuple_value (result, 0, &db_auth_object_value) == NO_ERROR)
	{
	  if (DB_IS_NULL (&db_auth_object_value))
	    {
	      goto exit;
	    }

	  db_auth_object_mop = db_get_object (&db_auth_object_value);
	}

      if (db_query_get_tuple_value (result, 1, &grantor_value) == NO_ERROR)
	{
	  if (DB_IS_NULL (&grantor_value))
	    {
	      goto exit;
	    }

	  grantor_mop = db_get_object (&grantor_value);
	}

      if (db_query_get_tuple_value (result, 2, &grantee_value) == NO_ERROR)
	{
	  if (DB_IS_NULL (&grantee_value))
	    {
	      goto exit;
	    }

	  grantee_mop = db_get_object (&grantee_value);
	}

      if (db_query_get_tuple_value (result, 3, &object_of_value) == NO_ERROR)
	{
	  if (DB_IS_NULL (&object_of_value))
	    {
	      goto exit;
	    }

	  object_of_mop = db_get_object (&object_of_value);
	}

      if (db_query_get_tuple_value (result, 4, &auth_type_value) == NO_ERROR)
	{
	  auth_type_char = NULL;

	  if (DB_IS_NULL (&auth_type_value))
	    {
	      error = ER_FAILED;
	      ASSERT_ERROR_AND_SET (error);
	      goto exit;
	    }

	  auth_type_char = db_get_char (&auth_type_value);

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
		  goto exit;
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
	      goto exit;
	      break;
	    }
	}

      if (db_query_get_tuple_value (result, 5, &is_grantable_value) == NO_ERROR)
	{
	  if (DB_IS_NULL (&is_grantable_value))
	    {
	      goto exit;
	    }

	  is_grantable = db_get_int (&is_grantable_value);
	}

      assert (db_auth_object_mop != NULL && grantor_mop != NULL && grantee_mop != NULL && object_of_mop != NULL
	      && db_auth != DB_AUTH_NONE && is_grantable != -1);

      /*
       * when the grantee becomes the new owner, previously granted privileges are removed.
       * reason: privileges of the owner cannot be revoked.
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
	  /* privileges cannot be granted to the owner, so they are removed immediately without being temporarily stored. */
	}
      else
	{
	  /* before deleting the data in db_auth, merge the data and temp store it. */
	  std::get<0> (key) = ws_is_same_object (grantor_mop, old_owner_mop) ? new_owner_mop : grantor_mop;
	  std::get<1> (key) = grantee_mop;
	  std::get<2> (key) = object_of_mop;
	  std::get<3> (key) = db_auth;

	  if (auth_unordered_map.find (key) == auth_unordered_map.end() || auth_unordered_map[key] < is_grantable)
	    {
	      auth_unordered_map[key] = is_grantable;
	    }
	}

      error = obj_inst_lock (db_auth_object_mop, 1);
      if (error != NO_ERROR)
	{
	  ASSERT_ERROR_AND_SET (error);
	  goto exit;
	}

      error = obj_delete (db_auth_object_mop);
      if (error != NO_ERROR)
	{
	  ASSERT_ERROR_AND_SET (error);
	  goto exit;
	}
    }

  /* reinsert the merged temp data */
  for (const auto &entry : auth_unordered_map)
    {
      const auto &key = entry.first;
      is_grantable = entry.second;

      error =
	      accessor.insert_auth (obj_type, std::get<0> (key), std::get<1> (key), std::get<2> (key), std::get<3> (key),
				    (is_grantable) ? std::get<3> (key) : DB_AUTH_NONE);

      /* Fail to insert, do not add data to the _auth catalog. */
      if (error != NO_ERROR)
	{
	  goto exit;
	}
    }


exit:
  if (result != NULL)
    {
      db_query_end (result);
    }
  if (session != NULL)
    {
      db_close_session (session);
    }

  db_value_clear (&val);
  db_value_clear (&db_auth_object_value);
  db_value_clear (&grantor_value);
  db_value_clear (&grantee_value);
  db_value_clear (&object_of_value);
  db_value_clear (&auth_type_value);
  db_value_clear (&is_grantable_value);

  if (db_auth_object_mop == NULL && grantor_mop == NULL && grantee_mop == NULL && object_of_mop == NULL &&
      db_auth == DB_AUTH_NONE && is_grantable == -1 && er_errid () == NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      error = ER_GENERIC_ERROR;
    }

  AU_ENABLE (save);

  return (error);
}
