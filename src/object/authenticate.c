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
 * authenticate.c - Authorization manager
 */

#ident "$Id$"

/*
 * Note:
 * Need to remove calls to the db_ layer since there are some
 * nasty dependency problems during restart and when the server
 * crashes since we need to touch objects before the database is
 * officially open.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

#include "porting.h"
#include "misc_string.h"
#include "memory_alloc.h"
#include "dbtype.h"
#include "error_manager.h"
#include "work_space.h"
#include "object_primitive.h"
#include "class_object.h"
#include "schema_manager.h"
#include "authenticate.h"
#include "object_accessor.h"
#include "encryption.h"
#include "crypt_opfunc.h"
#include "message_catalog.h"
#include "string_opfunc.h"
#include "transaction_cl.h"
#include "db.h"
#include "trigger_manager.h"
#include "transform.h"
#include "schema_system_catalog_constants.h"
#include "environment_variable.h"
#include "jsp_cl.h"
#include "object_print.h"
#include "execute_schema.h"	/* UNIQUE_PARTITION_SAVEPOINT_OWNER */
#include "execute_statement.h"	/* do_get_serial_obj_id () */
#include "optimizer.h"
#include "network_interface_cl.h"
#include "printer.hpp"
#include "authenticate_cache.hpp"
#include "authenticate_grant.hpp"

#if defined (SUPPRESS_STRLEN_WARNING)
#define strlen(s1)  ((int) strlen(s1))
#endif /* defined (SUPPRESS_STRLEN_WARNING) */

#if defined(SA_MODE)
extern bool catcls_Enable;
#endif /* SA_MODE */

/* Macro to determine if a name is system catalog class */

/*
 * AU_OBJECT_CLASS_NAME
 *
 * This is list of class names that CUBRID manages as database objects
 * Their existence is checked when dropping an user
 */
const char *AU_OBJECT_CLASS_NAME[] = {
  CT_CLASS_NAME,		/* AU_OBJECT_CLASS */
  CT_TRIGGER_NAME,		/* AU_OBJECT_TRIGGER */
  CT_SERIAL_NAME,		/* AU_OBJECT_SERIAL */
  CT_DB_SERVER_NAME,		/* AU_OBJECT_SERVER */
  CT_SYNONYM_NAME,		/* AU_OBJECT_SYNONYM */
  CT_STORED_PROC_NAME,		/* AU_OBJECT_PROCEDURE */
  NULL
};

/*
 *
 *
 * 
 */
authenticate_context *au_ctx = nullptr;

void
au_init (void)
{
  if (au_ctx == nullptr)
    {
      au_ctx = new authenticate_context ();
    }
}

void
au_final (void)
{
  if (au_ctx != nullptr)
    {
      delete au_ctx;
      au_ctx = nullptr;
    }
}

int
au_login (const char *name, const char *password, bool ignore_dba_privilege)
{
  au_init ();
  return au_ctx->login (name, password, ignore_dba_privilege);
}

/* 'get_attribute_number' is a statically linked method used only for QA
   scenario */
void get_attribute_number (DB_OBJECT * target, DB_VALUE * result, DB_VALUE * attr_name);

/*
 * au_static_links
 *
 * Since authorization is always linked in with the database, the
 * methods are defined statically.  The linkage will be done
 * during au_init even though it is redundant on subsequent
 * restart calls.
 */
static DB_METHOD_LINK au_static_links[] = {
  {"au_add_user_method", (METHOD_LINK_FUNCTION) au_add_user_method},
  {"au_drop_user_method", (METHOD_LINK_FUNCTION) au_drop_user_method},
  {"au_find_user_method", (METHOD_LINK_FUNCTION) au_find_user_method},
  {"au_add_member_method", (METHOD_LINK_FUNCTION) au_add_member_method},
  {"au_drop_member_method", (METHOD_LINK_FUNCTION) au_drop_member_method},
  {"au_set_password_method", (METHOD_LINK_FUNCTION) au_set_password_method},
  {"au_set_password_encoded_method", (METHOD_LINK_FUNCTION) au_set_password_encoded_method},
  {"au_set_password_encoded_sha1_method", (METHOD_LINK_FUNCTION) au_set_password_encoded_sha1_method},
  {"au_describe_user_method", (METHOD_LINK_FUNCTION) au_describe_user_method},
  {"au_describe_root_method", (METHOD_LINK_FUNCTION) au_describe_root_method},
  {"au_info_method", (METHOD_LINK_FUNCTION) au_info_method},
  {"au_login_method", (METHOD_LINK_FUNCTION) au_login_method},
  {"au_change_owner_method", (METHOD_LINK_FUNCTION) au_change_owner_method},
  {"au_change_trigger_owner_method", (METHOD_LINK_FUNCTION) au_change_trigger_owner_method},
  {"au_get_owner_method", (METHOD_LINK_FUNCTION) au_get_owner_method},
  {"au_check_authorization_method", (METHOD_LINK_FUNCTION) au_check_authorization_method},

  /*
   * qo_set_cost
   *
   * This function is exported by optimizer/query_planner.c, and provides a backdoor that
   * allows us some gross manipulation capabilities for the query
   * optimizer.  By adding it to the list of method implementations that
   * are statically linked we make it easy for us to add a method to an
   * arbitrary class for those cases where we need to poke the optimizer.
   * To use this, try
   *
   *      alter class foo add method class set_cost(string, string)
   *              function qo_set_cost;
   *
   * and then utter
   *
   *      call set_cost('iscan', '0') on class foo;
   */
  {"qo_set_cost", (METHOD_LINK_FUNCTION) qo_set_cost},
  {"get_attribute_number", (METHOD_LINK_FUNCTION) get_attribute_number},
  {"dbmeth_class_name", (METHOD_LINK_FUNCTION) dbmeth_class_name},
  {"dbmeth_print", (METHOD_LINK_FUNCTION) dbmeth_print},
  {"au_change_sp_owner_method", (METHOD_LINK_FUNCTION) au_change_sp_owner_method},
  {"au_change_serial_owner_method", (METHOD_LINK_FUNCTION) au_change_serial_owner_method},

  {NULL, NULL}
};

static void au_print_grant_entry (DB_SET * grants, int grant_index, FILE * fp);
static void au_print_auth (MOP auth, FILE * fp);

/*
 * DB_ EXTENSION FUNCTIONS
 */


/*
 * au_get_set
 *   return: error code
 *   obj(in):
 *   attname(in):
 *   set(in):
 */
int
au_get_set (MOP obj, const char *attname, DB_SET ** set)
{
  int error = NO_ERROR;
  DB_VALUE value;

  *set = NULL;
  error = obj_get (obj, attname, &value);
  if (error == NO_ERROR)
    {
      if (!TP_IS_SET_TYPE (DB_VALUE_TYPE (&value)))
	{
	  error = ER_OBJ_DOMAIN_CONFLICT;
	}
      else
	{
	  if (DB_IS_NULL (&value))
	    {
	      *set = NULL;
	    }
	  else
	    {
	      *set = db_get_set (&value);
	    }

	  /*
	   * since we almost ALWAYS end up iterating through the sets fetching
	   * objects, do a vector fetch immediately to avoid
	   * multiple server calls.
	   * Should have a sub db_ function for doing this.
	   */
	  if (*set != NULL)
	    {
	      error = db_fetch_set (*set, DB_FETCH_READ, 0);
	      if (error == NO_ERROR)
		{
		  error = set_filter (*set);
		}
	      /*
	       * shoudl be detecting the filtered elements and marking the
	       * object dirty if possible
	       */
	    }
	}
    }
  return (error);
}

/*
 * au_get_object
 *   return: error code
 *   obj(in):
 *   attname(in):
 *   mop_ptr(in):
 */
int
au_get_object (MOP obj, const char *attname, MOP * mop_ptr)
{
  int error = NO_ERROR;
  DB_VALUE value;

  *mop_ptr = NULL;
  error = obj_get (obj, attname, &value);
  if (error == NO_ERROR)
    {
      if (DB_VALUE_TYPE (&value) != DB_TYPE_OBJECT)
	{
	  error = ER_OBJ_DOMAIN_CONFLICT;
	}
      else
	{
	  if (DB_IS_NULL (&value))
	    {
	      *mop_ptr = NULL;
	    }
	  else
	    {
	      *mop_ptr = db_get_object (&value);
	    }
	}
    }
  return (error);
}

/*
 * au_set_get_obj -
 *   return: error code
 *   set(in):
 *   index(in):
 *   obj(out):
 */
int
au_set_get_obj (DB_SET * set, int index, MOP * obj)
{
  int error = NO_ERROR;
  DB_VALUE value;

  *obj = NULL;

  error = set_get_element (set, index, &value);
  if (error == NO_ERROR)
    {
      if (DB_VALUE_TYPE (&value) != DB_TYPE_OBJECT)
	{
	  error = ER_OBJ_DOMAIN_CONFLICT;
	}
      else
	{
	  if (DB_IS_NULL (&value))
	    {
	      *obj = NULL;
	    }
	  else
	    {
	      *obj = db_get_object (&value);
	    }
	}
    }

  return error;
}

/*
 * au_drop_member_method -  Method interface for au_drop_member.
 *   return: none
 *   user(in): user object
 *   returnval(in): return value of this method
 *   memval(in):
 */
void
au_drop_member_method (MOP user, DB_VALUE * returnval, DB_VALUE * memval)
{
  int error = NO_ERROR;
  MOP member;

  if (memval != NULL)
    {
      member = NULL;
      if (DB_VALUE_TYPE (memval) == DB_TYPE_OBJECT && !DB_IS_NULL (memval) && db_get_object (memval) != NULL)
	{
	  member = db_get_object (memval);
	}
      else if (DB_IS_STRING (memval) && !DB_IS_NULL (memval) && db_get_string (memval) != NULL)
	{
	  member = au_find_user (db_get_string (memval));
	  if (member == NULL)
	    {
	      assert (er_errid () != NO_ERROR);
	      error = er_errid ();
	      goto error;
	    }
	}

      if (member != NULL)
	{
	  if (ws_is_same_object (user, Au_user) || au_is_dba_group_member (Au_user))
	    {
	      error = au_drop_member (user, member);
	    }
	  else
	    {
	      error = ER_AU_NOT_OWNER;
	      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 0);
	    }
	}
      else
	{
	  error = ER_AU_INVALID_USER;
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 1, "");
	}
    }
  else
    {
      error = ER_AU_INVALID_USER;
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 1, "");
    }

error:
  if (error == NO_ERROR)
    {
      db_make_null (returnval);
    }
  else
    {
      db_make_error (returnval, error);
    }
}

/*
 * au_drop_user_method - Method interface for au_drop_user.
 *   return: none
 *   root(in):
 *   returnval(out): return value of this method
 *   name(in):
 */
void
au_drop_user_method (MOP root, DB_VALUE * returnval, DB_VALUE * name)
{
  int error;
  DB_OBJECT *user = NULL;

  db_make_null (returnval);

  if (Au_dba_user != NULL && !au_is_dba_group_member (Au_user))
    {
      error = ER_AU_DBA_ONLY;
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 1, "drop_user");
      db_make_error (returnval, error);
    }
  else
    {
      user = NULL;
      if (name != NULL && DB_IS_STRING (name) && db_get_string (name) != NULL)
	{
	  error = au_find_user_to_drop (db_get_string (name), &user);
	  if (error == NO_ERROR)
	    {
	      assert (user != NULL);
	      error = au_drop_user (user);
	    }

	  if (error != NO_ERROR)
	    {
	      db_make_error (returnval, error);
	    }
	}
      else
	{
	  error = ER_AU_INVALID_USER;
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 1, "");
	  db_make_error (returnval, error);
	}
    }
}

/*
 * au_find_user_method - Method interface to au_find_user.
 *   return: none
 *   class(in):
 *   returnval(out):
 *   name(in):
 */
void
au_find_user_method (MOP class_mop, DB_VALUE * returnval, DB_VALUE * name)
{
  MOP user;
  int error = NO_ERROR;

  db_make_null (returnval);
  if (name != NULL && DB_IS_STRING (name) && !DB_IS_NULL (name) && db_get_string (name) != NULL)
    {
      user = au_find_user (db_get_string (name));
      if (user != NULL)
	{
	  db_make_object (returnval, user);
	}
    }
  else
    {
      error = ER_AU_INVALID_USER;
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 1, "");
      db_make_error (returnval, error);
    }
}

/*
 * au_delete_auth_of_dropping_table - delete _db_auth records refers to the given table.
 *   return: error code
 *   class_name(in): the class name to be dropped
 */
int
au_delete_auth_of_dropping_table (const char *class_name)
{
  int error = NO_ERROR, save;
  const char *sql_query =
    "DELETE FROM [" CT_CLASSAUTH_NAME "] [au]" " WHERE [au].[class_of] IN" " (SELECT [cl] FROM " CT_CLASS_NAME
    " [cl] WHERE [unique_name] = ?);";
  DB_VALUE val;
  DB_QUERY_RESULT *result = NULL;
  DB_SESSION *session = NULL;
  int stmt_id;

  db_make_null (&val);

  /* Disable the checking for internal authorization object access */
  AU_DISABLE (save);

  assert (class_name != NULL);

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

  db_make_string (&val, class_name);
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
 * au_add_user_method
 *   return: none
 *   class(in): class object
 *   returnval(out): return value of this method
 *   name(in):
 *   password(in):
 */
void
au_add_user_method (MOP class_mop, DB_VALUE * returnval, DB_VALUE * name, DB_VALUE * password)
{
  int error;
  int exists;
  MOP user;
  const char *tmp = NULL;

  if (name != NULL && DB_IS_STRING (name) && !DB_IS_NULL (name) && ((tmp = db_get_string (name)) != NULL))
    {
      if (intl_identifier_upper_string_size (tmp) >= DB_MAX_USER_LENGTH)
	{
	  error = ER_USER_NAME_TOO_LONG;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	  db_make_error (returnval, error);
	  return;
	}
      /*
       * although au_set_password will check this, check it out here before
       * we bother creating the user object
       */
      if (password != NULL && DB_IS_STRING (password) && !DB_IS_NULL (password) && (tmp = db_get_string (password))
	  && strlen (tmp) > AU_MAX_PASSWORD_CHARS)
	{
	  error = ER_AU_PASSWORD_OVERFLOW;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	  db_make_error (returnval, error);
	}
      else
	{
	  user = au_add_user (db_get_string (name), &exists);
	  if (user == NULL)
	    {
	      /* return the error that was set */
	      db_make_error (returnval, er_errid ());
	    }
	  else if (exists)
	    {
	      error = ER_AU_USER_EXISTS;
	      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 1, db_get_string (name));
	      db_make_error (returnval, error);
	    }
	  else
	    {
	      if (password != NULL && DB_IS_STRING (password) && !DB_IS_NULL (password))
		{
		  error = au_set_password (user, db_get_string (password));
		  if (error != NO_ERROR)
		    {
		      db_make_error (returnval, error);
		    }
		  else
		    {
		      db_make_object (returnval, user);
		    }
		}
	      else
		{
		  db_make_object (returnval, user);
		}
	    }
	}
    }
  else
    {
      error = ER_AU_INVALID_USER;
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 1, "");
      db_make_error (returnval, error);
    }
}

/*
 * au_set_password_method -  Method interface for au_set_password.
 *   return: none
 *   user(in): user object
 *   returnval(out): return value of this method
 *   password(in): new password
 */
void
au_set_password_method (MOP user, DB_VALUE * returnval, DB_VALUE * password)
{
  int error;
  const char *string = NULL;

  db_make_null (returnval);
  if (password != NULL)
    {
      if (DB_IS_STRING (password) && !DB_IS_NULL (password))
	{
	  string = db_get_string (password);
	}

      error = au_set_password (user, string);
      if (error != NO_ERROR)
	{
	  db_make_error (returnval, error);
	}
    }
  else
    {
      error = ER_AU_INVALID_PASSWORD;
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 0);
      db_make_error (returnval, error);
    }
}

/*
 * au_set_password_encoded_method - Method interface for setting
 *                                  encoded passwords.
 *   return: none
 *   user(in): user object
 *   returnval(out): return value of this object
 *   password(in): new password
 *
 * Note: We don't check for the 8 character limit here because this is intended
 *       to be used only by the schema generated by unloaddb.  For this
 *       application, the password length was validated when it was first
 *       created.
 */
void
au_set_password_encoded_method (MOP user, DB_VALUE * returnval, DB_VALUE * password)
{
  int error;
  const char *string = NULL;

  db_make_null (returnval);
  if (password != NULL)
    {
      if (DB_IS_STRING (password))
	{
	  if (DB_IS_NULL (password))
	    {
	      string = NULL;
	    }
	  else
	    {
	      string = db_get_string (password);
	    }
	}

      error = au_set_password (user, string, 0, ENCODE_PREFIX_DES);
      if (error != NO_ERROR)
	{
	  db_make_error (returnval, error);
	}
    }
  else
    {
      error = ER_AU_INVALID_PASSWORD;
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 0);
      db_make_error (returnval, error);
    }
}

/*
 * au_set_password_encoded_sha1_method - Method interface for setting sha1/2 passwords.
 *   return: none
 *   user(in): user object
 *   returnval(out): return value of this object
 *   password(in): new password
 *
 * Note: We don't check for the 8 character limit here because this is intended
 *       to be used only by the schema generated by unloaddb.  For this
 *       application, the password length was validated when it was first
 *       created.
 */
void
au_set_password_encoded_sha1_method (MOP user, DB_VALUE * returnval, DB_VALUE * password)
{
  int error;
  const char *string = NULL;

  db_make_null (returnval);
  if (password != NULL)
    {
      if (DB_IS_STRING (password))
	{
	  if (DB_IS_NULL (password))
	    {
	      string = NULL;
	    }
	  else
	    {
	      string = db_get_string (password);
	    }
	}

      /* in case of SHA2, prefix is not stripped */
      if (string != NULL && IS_ENCODED_SHA2_512 (string))
	{
	  error = au_set_password (user, string + 1 /* 1 for prefix */ , 0, ENCODE_PREFIX_SHA2_512);
	}
      else
	{
	  error = au_set_password (user, string, 0, ENCODE_PREFIX_SHA1);
	}

      if (error != NO_ERROR)
	{
	  db_make_error (returnval, error);
	}
    }
  else
    {
      error = ER_AU_INVALID_PASSWORD;
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 0);
      db_make_error (returnval, error);
    }
}

/*
 * AUTHORIZATION CACHING
 */


/*
 * REVOKE OPERATION
 */



/*
 * MISC UTILITIES
 */

/*
 * au_change_owner_method - Method interface to au_change_owner
 *   return: none
 *   obj(in): class whose owner is to change
 *   returnval(out): return value of this method
 *   class(in):
 *   owner(in): new owner
 */
void
au_change_owner_method (MOP obj, DB_VALUE * return_val, DB_VALUE * class_val, DB_VALUE * owner_val)
{
  MOP class_mop = NULL;
  SM_CLASS *class_ = NULL;
  MOP *sub_partitions = NULL;
  MOP owner_mop = NULL;
  const char *class_name = NULL;
  const char *owner_name = NULL;
  int is_partition = DB_NOT_PARTITIONED_CLASS;
  bool has_savepoint = false;
  int i;
  int error = NO_ERROR;

  if (!return_val || !class_val || !owner_val)
    {
      ERROR_SET_WARNING (error, ER_AU_INVALID_ARGUMENTS);
      db_make_error (return_val, error);
      return;
    }

  if (!DB_IS_STRING (class_val) || (class_name = db_get_string (class_val)) == NULL)
    {
      ERROR_SET_WARNING_1ARG (error, ER_AU_INVALID_CLASS, "");
      db_make_error (return_val, error);
      return;
    }

  if (!DB_IS_STRING (owner_val) || (owner_name = db_get_string (owner_val)) == NULL)
    {
      ERROR_SET_WARNING_1ARG (error, ER_AU_INVALID_USER, "");
      db_make_error (return_val, error);
      return;
    }

  class_mop = sm_find_class (class_name);
  if (class_mop == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      db_make_error (return_val, error);
      return;
    }

  error = au_fetch_class_force (class_mop, &class_, AU_FETCH_UPDATE);
  if (error != NO_ERROR)
    {
      ASSERT_ERROR_AND_SET (error);
      return;
    }

  /* To change the owner of a system class is not allowed. */
  if (sm_issystem (class_))
    {
      ERROR_SET_ERROR_1ARG (error, ER_AU_CANT_ALTER_OWNER_OF_SYSTEM_CLASS, "");
      db_make_error (return_val, error);
      return;
    }

  owner_mop = au_find_user (owner_name);
  if (owner_mop == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      db_make_error (return_val, error);
      return;
    }

  error = sm_partitioned_class_type (class_mop, &is_partition, NULL, &sub_partitions);
  if (error != NO_ERROR)
    {
      ASSERT_ERROR ();
      db_make_error (return_val, error);
      return;
    }

  if (is_partition == DB_PARTITION_CLASS)	/* if partition; error */
    {
      ERROR_SET_ERROR (error, ER_NOT_ALLOWED_ACCESS_TO_PARTITION);
      db_make_error (return_val, error);
      goto end;
    }

  if (is_partition == DB_PARTITIONED_CLASS)	/* if partitioned class; do actions to all partitions */
    {
      error = tran_system_savepoint (UNIQUE_PARTITION_SAVEPOINT_OWNER);
      if (error != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  db_make_error (return_val, error);
	  goto end;
	}

      has_savepoint = true;

      for (i = 0; sub_partitions[i]; i++)
	{
	  error = au_change_owner (sub_partitions[i], owner_mop);
	  if (error != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      db_make_error (return_val, error);
	      goto end;
	    }
	}
    }

  error = au_change_owner (class_mop, owner_mop);
  if (error != NO_ERROR)
    {
      ASSERT_ERROR ();
      db_make_error (return_val, error);
    }

end:
  if (sub_partitions)
    {
      free_and_init (sub_partitions);
    }

  if (has_savepoint && error != NO_ERROR && error != ER_LK_UNILATERALLY_ABORTED)
    {
      tran_abort_upto_system_savepoint (UNIQUE_PARTITION_SAVEPOINT_OWNER);
    }
}

/*
 * au_change_serial_owner() - Change serial object's owner
 *   return: error code
 *   object(in/out): serial object whose owner is to be changed
 *   new_owner(in): new owner
 *   is_auto_increment(in): check if auto increment serial name change is necessary
 */
int
au_change_serial_owner (MOP serial_mop, MOP owner_mop, bool by_class_owner_change)
{
  DB_OBJECT *serial_owner_obj = NULL;
  DB_OBJECT *serial_class_mop = NULL;
  DB_OBJECT *serial_obj = NULL;
  DB_IDENTIFIER serial_obj_id;
  DB_OTMPL *obj_tmpl = NULL;
  DB_VALUE value;
  const char *serial_name = NULL;
  char serial_new_name[DB_MAX_SERIAL_NAME_LENGTH] = { '\0' };
  const char *att_name = NULL;
  char *owner_name = NULL;
  char downcase_owner_name[DB_MAX_USER_LENGTH] = { '\0' };
  bool is_abort = false;
  int save = 0;
  int error = NO_ERROR;

  if (!serial_mop || !owner_mop)
    {
      ERROR_SET_WARNING (error, ER_OBJ_INVALID_ARGUMENTS);
      return error;
    }

  OID_SET_NULL (&serial_obj_id);

  if (!au_is_dba_group_member (Au_user))
    {
      ERROR_SET_WARNING_1ARG (error, ER_AU_DBA_ONLY, "change_serial_owner");
      return error;
    }

  AU_DISABLE (save);

  /*
   * class, serial, and trigger distinguish user schema by unique_name (user_specified_name).
   * so if the owner of class, serial, trigger changes, the unique_name must also change.
   */

  /*
   * after serial.next_value, the currect value maybe changed, but cub_cas
   * still hold the old value. To get the new value. we need decache it
   * then refetch it from server again.
   */
  assert (WS_ISDIRTY (serial_mop) == false);

  ws_decache (serial_mop);

  /* no need to get last version for serial - actually, the purpose is AU_FETCH_WRITE, so fetch type is not relevant;
   * the last version will be locked and it will be considered visibile only if delid is not set */
  error = au_fetch_instance_force (serial_mop, NULL, AU_FETCH_WRITE, LC_FETCH_MVCC_VERSION);
  if (error != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto end;
    }

  if (!by_class_owner_change)
    {
      /* It can be checked as one of unique_name, class_name, and att_name. */
      error = obj_get (serial_mop, SERIAL_ATTR_ATT_NAME, &value);
      if (error != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto end;
	}

      if (!DB_IS_NULL (&value))
	{
	  ERROR_SET_WARNING (error, ER_AU_CANT_ALTER_OWNER_OF_AUTO_INCREMENT);
	  goto end;
	}
    }

  /* Check if the owner to be changed is the same. */
  error = obj_get (serial_mop, SERIAL_ATTR_OWNER, &value);
  if (error != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto end;
    }

  if (DB_VALUE_DOMAIN_TYPE (&value) != DB_TYPE_OBJECT || (serial_owner_obj = db_get_object (&value)) == NULL)
    {
      /* Unable to get attribute value. */
      ERROR_SET_WARNING (error, ER_OBJ_INVALID_ARGUMENTS);
      goto end;
    }

  if (ws_is_same_object (serial_owner_obj, owner_mop))
    {
      goto end;
    }

  error = obj_get (serial_mop, SERIAL_ATTR_NAME, &value);
  if (error != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto end;
    }

  if (!DB_IS_STRING (&value) || (serial_name = db_get_string (&value)) == NULL)
    {
      /* Unable to get attribute value. */
      ERROR_SET_WARNING (error, ER_OBJ_INVALID_ARGUMENTS);
      goto end;
    }

  owner_name = au_get_user_name (owner_mop);
  if (!owner_name)
    {
      ASSERT_ERROR_AND_SET (error);
      goto end;
    }
  sm_downcase_name (owner_name, downcase_owner_name, DB_MAX_USER_LENGTH);
  db_ws_free_and_init (owner_name);

  sprintf (serial_new_name, "%s.%s", downcase_owner_name, serial_name);

  serial_class_mop = sm_find_class (CT_SERIAL_NAME);
  if (serial_class_mop == NULL)
    {
      ERROR_SET_ERROR (error, ER_QPROC_DB_SERIAL_NOT_FOUND);
      goto end;
    }

  if (do_get_serial_obj_id (&serial_obj_id, serial_class_mop, serial_new_name) != NULL)
    {
      ERROR_SET_ERROR_1ARG (error, ER_QPROC_SERIAL_ALREADY_EXIST, serial_new_name);
      goto end;
    }

  obj_tmpl = dbt_edit_object (serial_mop);
  if (!obj_tmpl)
    {
      ASSERT_ERROR ();
      goto end;
    }

  /* unique_name */
  db_make_string (&value, serial_new_name);
  error = dbt_put_internal (obj_tmpl, SERIAL_ATTR_UNIQUE_NAME, &value);
  if (error != NO_ERROR)
    {
      ASSERT_ERROR ();
      is_abort = true;
      goto end;
    }

  /* owner */
  db_make_object (&value, owner_mop);
  error = dbt_put_internal (obj_tmpl, SERIAL_ATTR_OWNER, &value);
  if (error != NO_ERROR)
    {
      ASSERT_ERROR ();
      is_abort = true;
      goto end;
    }

  serial_obj = dbt_finish_object (obj_tmpl);
  if (!serial_obj)
    {
      ASSERT_ERROR_AND_SET (error);
      is_abort = true;
      goto end;
    }

end:
  if (is_abort && obj_tmpl)
    {
      dbt_abort_object (obj_tmpl);
    }

  AU_ENABLE (save);

  return error;
}

/*
 * au_change_serial_owner_method() - Method interface to au_change_serial_owner
 *   return: none
 *   obj(in): class whose owner is to change
 *   returnval(out): return value of this method
 *   serial(in): serial name
 *   owner(in): new owner
 */
void
au_change_serial_owner_method (MOP obj, DB_VALUE * return_val, DB_VALUE * serial_val, DB_VALUE * owner_val)
{
  MOP serial_class_mop = NULL;
  MOP serial_mop = NULL;
  DB_IDENTIFIER serial_obj_id;
  MOP owner_mop = NULL;
  const char *serial_name = NULL;
  char user_specified_serial_name[DB_MAX_SERIAL_NAME_LENGTH] = { '\0' };
  const char *owner_name = NULL;
  int error = NO_ERROR;

  if (!return_val || !serial_val || !owner_val)
    {
      ERROR_SET_WARNING (error, ER_OBJ_INVALID_ARGUMENTS);
      db_make_error (return_val, error);
      return;
    }

  if (!DB_IS_STRING (serial_val) || (serial_name = db_get_string (serial_val)) == NULL)
    {
      ERROR_SET_WARNING_1ARG (error, ER_OBJ_INVALID_ARGUMENT, "");
      db_make_error (return_val, error);
      return;
    }

  if (!DB_IS_STRING (owner_val) || (owner_name = db_get_string (owner_val)) == NULL)
    {
      ERROR_SET_WARNING_1ARG (error, ER_AU_INVALID_USER, "");
      db_make_error (return_val, error);
      return;
    }

  serial_class_mop = sm_find_class (CT_SERIAL_NAME);

  sm_user_specified_name_for_serial (serial_name, user_specified_serial_name, DB_MAX_SERIAL_NAME_LENGTH);
  serial_mop = do_get_serial_obj_id (&serial_obj_id, serial_class_mop, user_specified_serial_name);
  if (serial_mop == NULL)
    {
      ERROR_SET_ERROR_1ARG (error, ER_QPROC_SERIAL_NOT_FOUND, user_specified_serial_name);
      db_make_error (return_val, error);
      return;
    }

  owner_mop = au_find_user (owner_name);
  if (owner_mop == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      db_make_error (return_val, error);
      return;
    }

  error = au_change_serial_owner (serial_mop, owner_mop, false);
  if (error != NO_ERROR)
    {
      ASSERT_ERROR ();
      db_make_error (return_val, error);
    }
}

/*
 * au_change_trigger_owner - This changes the owning user of a trigger.
 *                           This should be called only by the DBA.
 *   return: error code
 *   trigger(in): trigger whose owner is to change
 *   owner(in):new owner
 */
int
au_change_trigger_owner (MOP trigger_mop, MOP owner_mop)
{
  DB_OBJECT *trigger_owner_obj = NULL;
  DB_OBJECT *target_class_obj = NULL;
  SM_CLASS *target_class = NULL;
  DB_VALUE value;
  const char *trigger_old_name = NULL;
  char trigger_new_name[DB_MAX_IDENTIFIER_LENGTH] = { '\0' };
  char *owner_name = NULL;
  char downcase_owner_name[DB_MAX_USER_LENGTH] = { '\0' };
  int save = 0;
  int error = NO_ERROR;

  if (!trigger_mop || !owner_mop)
    {
      ERROR_SET_WARNING (error, ER_OBJ_INVALID_ARGUMENTS);
      return error;
    }

  if (!au_is_dba_group_member (Au_user))
    {
      ERROR_SET_WARNING_1ARG (error, ER_AU_DBA_ONLY, "change_trigger_owner");
      return error;
    }

  AU_DISABLE (save);

  /*
   * class, serial, and trigger distinguish user schema by unique_name (user_specified_name).
   * so if the owner of class, serial, trigger changes, the unique_name must also change.
   */

  error = obj_get (trigger_mop, TR_ATT_UNIQUE_NAME, &value);
  if (error != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto end;
    }

  if (!DB_IS_STRING (&value) || (trigger_old_name = db_get_string (&value)) == NULL)
    {
      ERROR_SET_WARNING_1ARG (error, ER_TR_TRIGGER_NOT_FOUND, "");
      goto end;
    }

  /* Check if the owner to be changed is the same. */
  error = obj_get (trigger_mop, TR_ATT_OWNER, &value);
  if (error != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto end;
    }

  if (DB_VALUE_DOMAIN_TYPE (&value) != DB_TYPE_OBJECT || (trigger_owner_obj = db_get_object (&value)) == NULL)
    {
      /* Unable to get attribute value. */
      ERROR_SET_WARNING (error, ER_OBJ_INVALID_ARGUMENTS);
      goto end;
    }

  if (ws_is_same_object (trigger_owner_obj, owner_mop))
    {
      goto end;
    }

  /* TO BE: It is necessary to check the permission of the target class of the owner to be changed. */
#if 0
  error = obj_get (target_class_obj, TR_ATT_CLASS, &value);
  if (error != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto end;
    }

  error = au_fetch_class (target_class_obj, &target_class, AU_FETCH_READ, AU_SELECT);
  if (error != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto end;
    }
#endif

  owner_name = au_get_user_name (owner_mop);
  if (!owner_name)
    {
      ASSERT_ERROR_AND_SET (error);
      goto end;
    }
  sm_downcase_name (owner_name, downcase_owner_name, DB_MAX_USER_LENGTH);
  db_ws_free_and_init (owner_name);

  snprintf (trigger_new_name, SM_MAX_IDENTIFIER_LENGTH, "%s.%s", downcase_owner_name,
	    sm_remove_qualifier_name (trigger_old_name));

  error = tr_rename_trigger (trigger_mop, trigger_new_name, false, true);
  if (error != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto end;
    }

  /* owner */
  db_make_object (&value, owner_mop);
  error = obj_set (trigger_mop, TR_ATT_OWNER, &value);
  if (error != NO_ERROR)
    {
      ASSERT_ERROR ();

      /* abort on error */
      if (tr_rename_trigger (trigger_mop, trigger_old_name, false, true) != NO_ERROR)
	{
	  assert (false);
	}
    }

end:
  AU_ENABLE (save);

  return error;
}

/*
 * au_change_trigger_owner_method - Method interface to au_change_trigger_owner
 *   return: none
 *   obj(in):
 *   returnval(out): return value of this method
 *   trigger(in): trigger whose owner is to change
 *   owner(in): new owner
 */
void
au_change_trigger_owner_method (MOP obj, DB_VALUE * return_val, DB_VALUE * trigger_val, DB_VALUE * owner_val)
{
  MOP trigger_mop = NULL;
  MOP owner_mop = NULL;
  const char *trigger_name = NULL;
  const char *owner_name = NULL;
  int error = NO_ERROR;

  if (!return_val || !trigger_val || !owner_val)
    {
      ERROR_SET_WARNING (error, ER_OBJ_INVALID_ARGUMENTS);
      db_make_error (return_val, error);
      return;
    }

  if (!DB_IS_STRING (trigger_val) || (trigger_name = db_get_string (trigger_val)) == NULL)
    {
      ERROR_SET_WARNING_1ARG (error, ER_TR_TRIGGER_NOT_FOUND, "");
      db_make_error (return_val, error);
      return;
    }

  if (!DB_IS_STRING (owner_val) || (owner_name = db_get_string (owner_val)) == NULL)
    {
      ERROR_SET_WARNING_1ARG (error, ER_AU_INVALID_USER, "");
      db_make_error (return_val, error);
      return;
    }

  trigger_mop = tr_find_trigger (trigger_name);
  if (trigger_mop == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      db_make_error (return_val, error);
      return;
    }

  owner_mop = au_find_user (owner_name);
  if (owner_mop == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      db_make_error (return_val, error);
      return;
    }

  error = au_change_trigger_owner (trigger_mop, owner_mop);
  if (error != NO_ERROR)
    {
      ASSERT_ERROR ();
      db_make_error (return_val, error);
    }
}

/*
 * au_get_class_owner - This access the user object that is the owner of
 *                      the class.
 *   return: user object (owner of class_)
 *   classmop(in): class object
 */
MOP
au_get_class_owner (MOP classmop)
{
  MOP owner = NULL;
  SM_CLASS *class_;

  /* should we disable authorization here ? */
  /*
   * should we allow the class owner to be known if the active user doesn't
   * have select authorization ?
   */

  if (au_fetch_class_force (classmop, &class_, AU_FETCH_READ) == NO_ERROR)
    {
      owner = class_->owner;
      if (owner == NULL)
	{
	  /* shouln't we try to update the class if the owner wasn't set ? */
	  owner = Au_dba_user;
	}
    }

  return (owner);
}

/*
 * au_get_owner_method - Method interface to au_change_owner
 *   return: none
 *   obj(in):
 *   returnval(out): return value of this method
 *   class(in): class object
 */
void
au_get_owner_method (MOP obj, DB_VALUE * returnval, DB_VALUE * class_)
{
  MOP user;
  MOP classmop;
  int error = NO_ERROR;

  db_make_null (returnval);
  if (class_ != NULL && DB_IS_STRING (class_) && !DB_IS_NULL (class_) && db_get_string (class_) != NULL)
    {
      classmop = sm_find_class (db_get_string (class_));
      if (classmop != NULL)
	{
	  user = au_get_class_owner (classmop);
	  if (user != NULL)
	    {
	      db_make_object (returnval, user);
	    }
	  else
	    {
	      assert (er_errid () != NO_ERROR);
	      error = er_errid ();
	    }
	}
      else
	{
	  error = ER_AU_INVALID_CLASS;
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 1, db_get_string (class_));
	}
    }
  else
    {
      error = ER_AU_INVALID_CLASS;
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 1, "");
    }

  if (error != NO_ERROR)
    {
      db_make_error (returnval, error);
    }
}

/*
 * au_check_authorization_method -
 *    return: none
 *    obj(in):
 *    returnval(out): return value of this method
 *    class(in):
 *    auth(in):
 */
void
au_check_authorization_method (MOP obj, DB_VALUE * returnval, DB_VALUE * class_, DB_VALUE * auth)
{
  MOP classmop;
  int error = NO_ERROR;

  db_make_null (returnval);
  if (class_ != NULL && DB_IS_STRING (class_) && !DB_IS_NULL (class_) && db_get_string (class_) != NULL)
    {

      classmop = sm_find_class (db_get_string (class_));
      if (classmop != NULL)
	{
	  error = au_check_class_authorization (classmop, (DB_AUTH) db_get_int (auth));
	  db_make_int (returnval, (error == NO_ERROR) ? true : false);
	}
      else
	{
	  error = ER_AU_INVALID_CLASS;
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 1, db_get_string (class_));
	}
    }
  else
    {
      error = ER_AU_INVALID_CLASS;
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 1, "");
    }
}

/*
 * au_add_method_check_authorization -
 *    return: error code
 */
int
au_add_method_check_authorization (void)
{
  MOP auth;
  SM_TEMPLATE *def;
  int save;

  AU_DISABLE (save);

  auth = db_find_class (AU_AUTH_CLASS_NAME);
  if (auth == NULL)
    {
      goto exit_on_error;
    }

  def = smt_edit_class_mop (auth, AU_ALTER);
  if (def == NULL)
    {
      goto exit_on_error;
    }

  smt_add_class_method (def, "check_authorization", "au_check_authorization_method");
  smt_assign_argument_domain (def, "check_authorization", true, NULL, 0, "integer", (DB_DOMAIN *) 0);
  smt_assign_argument_domain (def, "check_authorization", true, NULL, 1, "varchar(255)", (DB_DOMAIN *) 0);
  smt_assign_argument_domain (def, "check_authorization", true, NULL, 2, "integer", (DB_DOMAIN *) 0);
  sm_update_class (def, NULL);

  au_grant (Au_public_user, auth, AU_EXECUTE, false);

  AU_ENABLE (save);
  return NO_ERROR;

exit_on_error:
  AU_ENABLE (save);
  return ER_FAILED;
}

/*
 * au_change_sp_owner -
 *   return: error code
 *   sp(in):
 *   owner(in):
 */
int
au_change_sp_owner (MOP sp, MOP owner)
{
  int error = NO_ERROR;
  int save;
  DB_VALUE value;

  AU_DISABLE (save);
  if (!au_is_dba_group_member (Au_user))
    {
      error = ER_AU_DBA_ONLY;
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 1, "change_sp_owner");
    }
  else
    {
      db_make_object (&value, owner);
      error = obj_set (sp, SP_ATTR_OWNER, &value);
      if (error < 0)
	{
	  goto end;
	}
    }

end:
  AU_ENABLE (save);
  return (error);
}

/*
 * au_change_sp_owner_method -
 *   return: none
 *   obj(in):
 *   returnval(in):
 *   sp(in):
 *   owner(in):
 */
void
au_change_sp_owner_method (MOP obj, DB_VALUE * returnval, DB_VALUE * sp, DB_VALUE * owner)
{
  MOP user, sp_mop;
  int error;
  int ok = 0;

  db_make_null (returnval);
  if (sp != NULL && DB_IS_STRING (sp) && !DB_IS_NULL (sp) && db_get_string (sp) != NULL)
    {
      if (owner != NULL && DB_IS_STRING (owner) && !DB_IS_NULL (owner) && db_get_string (owner) != NULL)
	{
	  sp_mop = jsp_find_stored_procedure (db_get_string (sp));
	  if (sp_mop != NULL)
	    {
	      user = au_find_user (db_get_string (owner));
	      if (user != NULL)
		{
		  error = au_change_sp_owner (sp_mop, user);
		  if (error == NO_ERROR)
		    {
		      ok = 1;
		    }
		}
	    }
	}
      else
	{
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_AU_INVALID_USER, 1, "");
	}
    }
  else
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_SP_NOT_EXIST, 1, "");
    }

  if (!ok)
    {
      db_make_error (returnval, er_errid ());
    }
}

/*
 * LOGIN/LOGOUT
 */

/*
 * au_login_method - Method interface to au_login.
 *   return: none
 *   class_mop(in): class object
 *   returnval(out): return value of this method
 *   user(in): user name
 *   password(in): password
 */
void
au_login_method (MOP class_mop, DB_VALUE * returnval, DB_VALUE * user, DB_VALUE * password)
{
  int error = NO_ERROR;
  char *user_name;

  if (user != NULL)
    {
      if (DB_IS_STRING (user) && !DB_IS_NULL (user) && db_get_string (user) != NULL)
	{
	  if (password != NULL && DB_IS_STRING (password))
	    {
	      error = au_login (db_get_string (user), db_get_string (password), false);
	    }
	  else
	    {
	      error = au_login (db_get_string (user), NULL, false);
	    }
	}
    }
  else
    {
      error = ER_AU_INVALID_USER;
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 1, "");
    }

  if (error == NO_ERROR)
    {
      user_name = db_get_user_name ();
      error = clogin_user (user_name);

      if (error == NO_ERROR)
	{
	  db_make_null (returnval);
	}
      else
	{
	  db_make_error (returnval, error);
	}

      db_string_free (user_name);
    }
  else
    {
      db_make_error (returnval, error);
    }
}

/*
 * DEBUGGING FUNCTIONS
 */

/*
 * au_print_grant_entry() -
 *   return: none
 *   grants(in):
 *   grant_index(in):
 *   fp(in):
 */
static void
au_print_grant_entry (DB_SET * grants, int grant_index, FILE * fp)
{
  DB_VALUE value;

  set_get_element (grants, GRANT_ENTRY_CLASS (grant_index), &value);
  fprintf (fp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_AUTHORIZATION, MSGCAT_AUTH_CLASS_NAME),
	   sm_get_ch_name (db_get_object (&value)));
  fprintf (fp, " ");

  set_get_element (grants, GRANT_ENTRY_SOURCE (grant_index), &value);
  obj_get (db_get_object (&value), "name", &value);

  fprintf (fp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_AUTHORIZATION, MSGCAT_AUTH_FROM_USER),
	   db_get_string (&value));

  pr_clear_value (&value);

  set_get_element (grants, GRANT_ENTRY_CACHE (grant_index), &value);
  au_print_cache (db_get_int (&value), fp);
}

/*
 * au_print_auth() -
 *   return: none
 *   auth(in):
 *   fp(in):
 */
static void
au_print_auth (MOP auth, FILE * fp)
{
  DB_VALUE value;
  DB_SET *grants;
  int i, gsize;
  int error;

  /* kludge, some older databases used the name "user", rather than "owner" */
  error = obj_get (auth, "owner", &value);
  if (error != NO_ERROR)
    {
      error = obj_get (auth, "user", &value);
      if (error != NO_ERROR)
	return;			/* punt */
    }

  if (db_get_object (&value) != NULL)
    {
      obj_get (db_get_object (&value), "name", &value);
      fprintf (fp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_AUTHORIZATION, MSGCAT_AUTH_USER_TITLE),
	       db_get_string (&value));
      pr_clear_value (&value);
    }
  else
    {
      fprintf (fp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_AUTHORIZATION, MSGCAT_AUTH_UNDEFINED_USER));
    }

  get_grants (auth, &grants, 1);
  if (grants != NULL)
    {
      gsize = set_size (grants);
      for (i = 0; i < gsize; i += GRANT_ENTRY_LENGTH)
	{
	  au_print_grant_entry (grants, i, fp);
	}
      set_free (grants);
    }
}

/*
 * au_dump_auth() - Prints authorization info for all users.
 *   return: none
 *   fp(in): output file
 *
 * Note: The db_root class used to have a user attribute which was a set
 *       containing the object-id for all users.  The users attribute has been
 *       eliminated for performance reasons.  A query on the db_user class is
 *       new used to find all users.
 */
void
au_dump_auth (FILE * fp)
{
  MOP user, auth;
  char *query;
  DB_QUERY_RESULT *query_result;
  DB_QUERY_ERROR query_error;
  int error;
  DB_VALUE user_val;
  const char *qp1 = "select [%s] from [%s];";

  query = (char *) malloc (strlen (qp1) + strlen (AU_USER_CLASS_NAME) * 2);

  if (query)
    {
      sprintf (query, qp1, AU_USER_CLASS_NAME, AU_USER_CLASS_NAME);

      error = db_compile_and_execute_local (query, &query_result, &query_error);
      /* error is row count if not negative. */
      if (error > 0)
	{
	  while (db_query_next_tuple (query_result) == DB_CURSOR_SUCCESS)
	    {
	      if (db_query_get_tuple_value (query_result, 0, &user_val) == NO_ERROR)
		{
		  user = db_get_object (&user_val);
		  if (au_get_object (user, "authorization", &auth) == NO_ERROR)
		    {
		      au_print_auth (auth, fp);
		    }
		}
	    }
	}
      if (error >= 0)
	{
	  db_query_end (query_result);
	}
      free_and_init (query);
    }
}

/*
 * au_dump_user() - Dumps authorization information for a
 *                  particular user to a file.
 *   return: none
 *   user(in): user object
 *   fp(in): file pointer
 */
void
au_dump_user (MOP user, FILE * fp)
{
  DB_VALUE value;
  DB_SET *groups = NULL;
  MOP auth;
  int i, card;

  if (obj_get (user, "name", &value) == NO_ERROR)
    {
      fprintf (fp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_AUTHORIZATION, MSGCAT_AUTH_USER_NAME),
	       db_get_string (&value));
      pr_clear_value (&value);
    }

  groups = NULL;
  if (au_get_set (user, "direct_groups", &groups) == NO_ERROR)
    {
      fprintf (fp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_AUTHORIZATION, MSGCAT_AUTH_USER_DIRECT_GROUPS));
      card = set_cardinality (groups);
      for (i = 0; i < card; i++)
	{
	  if (set_get_element (groups, i, &value) == NO_ERROR)
	    {
	      if (obj_get (db_get_object (&value), "name", &value) == NO_ERROR)
		{
		  fprintf (fp, "%s ", db_get_string (&value));
		  pr_clear_value (&value);
		}
	    }
	}
      fprintf (fp, "\n");
      set_free (groups);
    }

  groups = NULL;
  if (au_get_set (user, "groups", &groups) == NO_ERROR)
    {
      fprintf (fp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_AUTHORIZATION, MSGCAT_AUTH_USER_GROUPS));
      card = set_cardinality (groups);
      for (i = 0; i < card; i++)
	{
	  if (set_get_element (groups, i, &value) == NO_ERROR)
	    {
	      if (obj_get (db_get_object (&value), "name", &value) == NO_ERROR)
		{
		  fprintf (fp, "%s ", db_get_string (&value));
		  pr_clear_value (&value);
		}
	    }
	}
      fprintf (fp, "\n");
      set_free (groups);
    }

  /* dump local grants */
  if (au_get_object (user, "authorization", &auth) == NO_ERROR)
    {
      au_print_auth (auth, fp);
    }

  /*
   * need to do a walk back through the group hierarchy and collect all
   * inherited grants and their origins
   */
}

/*
 * au_dump_to_file() - Dump all authorization information including
 *                     user/group hierarchy.
 *   return: none
 *   fp(in): file pointer
 *
 * Note: The db_root class used to have a user attribute which was a set
 *       containing the object-id for all users.  The users attribute has been
 *       eliminated for performance reasons.  A query on the db_user class is
 *       new used to find all users.
 */
void
au_dump_to_file (FILE * fp)
{
  MOP user;
  DB_VALUE value;
  char *query = NULL;
  DB_QUERY_RESULT *query_result = NULL;
  DB_QUERY_ERROR query_error;
  int error = NO_ERROR;
  DB_VALUE user_val;
  const char *qp1 = "select [%s] from [%s];";

  /* NOTE: We should be getting the real user name here ! */

  fprintf (fp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_AUTHORIZATION, MSGCAT_AUTH_CURRENT_USER),
	   Au_user_name);

  query = (char *) malloc (strlen (qp1) + strlen (AU_USER_CLASS_NAME) * 2);

  if (query)
    {
      sprintf (query, qp1, AU_USER_CLASS_NAME, AU_USER_CLASS_NAME);

      error = db_compile_and_execute_local (query, &query_result, &query_error);
      /* error is row count if not negative. */
      if (error > 0)
	{
	  fprintf (fp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_AUTHORIZATION, MSGCAT_AUTH_ROOT_USERS));
	  while (db_query_next_tuple (query_result) == DB_CURSOR_SUCCESS)
	    {
	      if (db_query_get_tuple_value (query_result, 0, &user_val) == NO_ERROR)
		{
		  user = db_get_object (&user_val);
		  if (obj_get (user, "name", &value) == NO_ERROR)
		    {
		      fprintf (fp, "%s ", db_get_string (&value));
		      pr_clear_value (&value);
		    }
		}
	    }
	  fprintf (fp, "\n");

	  if (db_query_first_tuple (query_result) == DB_CURSOR_SUCCESS)
	    {
	      do
		{
		  if (db_query_get_tuple_value (query_result, 0, &user_val) == NO_ERROR)
		    {
		      user = db_get_object (&user_val);
		      au_dump_user (user, fp);
		    }
		}
	      while (db_query_next_tuple (query_result) == DB_CURSOR_SUCCESS);
	    }
	  fprintf (fp, "\n");
	}
    }
  if (error >= 0 && query)
    {
      db_query_end (query_result);
    }
  if (query)
    {
      free_and_init (query);
    }

  fprintf (fp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_AUTHORIZATION, MSGCAT_AUTH_AUTH_TITLE));
  au_dump_auth (fp);
}

/*
 * au_dump() -
 *   return: none
 */
void
au_dump (void)
{
  au_dump_to_file (stdout);
}

/*
 * au_describe_user_method() - Method interface to au_dump_user.
 *                             Can only be called when using the csql
 *                             interpreter since it dumps to stdout
 *   return: none
 *   user(in): user object
 *   returnval(out): return value of this method
 */
void
au_describe_user_method (MOP user, DB_VALUE * returnval)
{
  db_make_null (returnval);
  if (user != NULL)
    {
      au_dump_user (user, stdout);
    }
}

/*
 * au_info_method() - Method interface for authorization dump utilities.
 *   return: none
 *   class_mop(in): class object
 *   returnval(out): return value of this method
 *   info(in):
 *
 * Note: This should be conditionalized so it knows what kind of environment
 *       this is (csql, esql).
 *       For now this is documented to only work within csql because it dumps
 *       to stdout.
 *       There are some hidden parameters that trigger other dump routines.
 *       This is a convenient hook for these things until we
 *       have a more formal way to call them (if we ever do).  These
 *       are not documented in the user manual.
 */
void
au_info_method (MOP class_mop, DB_VALUE * returnval, DB_VALUE * info)
{
  db_make_null (returnval);

  if (info != NULL && DB_IS_STRING (info) && !DB_IS_NULL (info) && db_get_string (info) != NULL)
    {
      /* this dumps stuff to stdout */
      help_print_info (db_get_string (info), stdout);
    }
}

/*
 * au_describe_root_method() - Method interface for authorization dump
 *                             utilities
 *   return: none
 *   class_mop(in): class object
 *   returnval(out): return value of this method
 *   info(in):
 *
 * Note: This should be conditionalized so it knows what kind of environment
 *       this is (csql, esql).  For now this is documented to
 *       only work within csql because it dumps to stdout.
 *       There are some hidden parameters that trigger other dump routines.
 *       This is a convenient hook for these things until we
 *       have a more formal way to call them (if we ever do).  These
 *       are not documented in the user manual.
 */
void
au_describe_root_method (MOP class_mop, DB_VALUE * returnval, DB_VALUE * info)
{
  db_make_null (returnval);

  if (info == NULL)
    {
      au_dump ();
    }
  else
    {
      /*
       * temporary, pass this through for older databases that still
       * have the "info" method pointing to this function
       */
      au_info_method (class_mop, returnval, info);
    }
}


/*
 * AUTHORIZATION CLASSES
 */

/*
 * au_link_static_methods() - Called during the restart sequence to statically
 *                            link the authorization methods.
 *    return: none
 */
void
au_link_static_methods (void)
{
  db_link_static_methods (&au_static_links[0]);
}

/*
 * RESTART/SHUTDOWN
 */

/*
 * au_get_class_privilege() -
 *   return: error code
 *   mop(in):
 *   auth(in):
 */
int
au_get_class_privilege (DB_OBJECT * mop, unsigned int *auth)
{
  SM_CLASS *class_;
  unsigned int *bits = NULL;
  int error = NO_ERROR;

  if (!Au_disable)
    {
      if (mop == NULL)
	{
	  return ER_FAILED;
	}

      class_ = (SM_CLASS *) mop->object;

      bits = get_cache_bits (class_);
      if (bits == NULL)
	{
	  return er_errid ();
	}

      if (*bits == AU_CACHE_INVALID)
	{
	  error = update_cache (mop, class_);
	  if (error == NO_ERROR)
	    {
	      bits = get_cache_bits (class_);
	    }
	}
      *auth = *bits;
    }

  return error;
}

/*
 * get_attribute_number - attribute number of the given attribute/class
 *   return:
 *   arg1(in):
 *   arg2(in):
 */
void
get_attribute_number (DB_OBJECT * target, DB_VALUE * result, DB_VALUE * attr_name)
{
  int attrid, shared;
  DB_DOMAIN *dom;

  db_make_null (result);

  if (DB_VALUE_TYPE (attr_name) != DB_TYPE_STRING)
    return;

  /* we will only look for regular attributes and not class attributes. this is a limitation of this method. */
  if (sm_att_info (target, db_get_string (attr_name), &attrid, &dom, &shared, 0 /* non-class attrs */ )
      < 0)
    return;

  db_make_int (result, attrid);
}

static int
au_check_owner (DB_VALUE * creator_val)
{
  MOP creator;
  DB_SET *groups;
  int ret_val = ER_FAILED;

  creator = db_get_object (creator_val);

  if (ws_is_same_object (creator, Au_user) || au_is_dba_group_member (Au_user))
    {
      ret_val = NO_ERROR;
    }
  else if (au_get_set (Au_user, "groups", &groups) == NO_ERROR)
    {
      if (set_ismember (groups, creator_val))
	{
	  ret_val = NO_ERROR;
	}
      set_free (groups);
    }

  return ret_val;
}

/*
 * au_check_serial_authorization - check whether the current user is able to
 *                                 modify serial object or not
 *   return: NO_ERROR if available, otherwise error code
 *   serial_object(in): serial object pointer
 */
int
au_check_serial_authorization (MOP serial_object)
{
  DB_VALUE creator_val;
  int ret_val;

  ret_val = db_get (serial_object, "owner", &creator_val);
  if (ret_val != NO_ERROR)
    {
      return ret_val;
    }

  assert (!DB_IS_NULL (&creator_val));

  ret_val = au_check_owner (&creator_val);
  if (ret_val != NO_ERROR)
    {
      ret_val = ER_QPROC_CANNOT_UPDATE_SERIAL;
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ret_val, 0);
    }

  pr_clear_value (&creator_val);
  return ret_val;
}

int
au_check_server_authorization (MOP server_object)
{
  DB_VALUE creator_val;
  int ret_val;

  ret_val = db_get (server_object, "owner", &creator_val);
  if (ret_val != NO_ERROR || DB_IS_NULL (&creator_val))
    {
      return ret_val;
    }

  ret_val = au_check_owner (&creator_val);
  if (ret_val != NO_ERROR)
    {
      ret_val = ER_DBLINK_CANNOT_UPDATE_SERVER;
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ret_val, 0);
    }

  pr_clear_value (&creator_val);
  return ret_val;
}

bool
au_is_server_authorized_user (DB_VALUE * owner_val)
{
  return (au_check_owner (owner_val) == NO_ERROR);
}
