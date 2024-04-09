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
 * db_method_static.cpp: implement static method links and their implementation
 */

#include "dbi.h" /* db_ */
#include "dbtype.h"
#include "dbtype_def.h"

#include "authenticate.h" /* au_ */
#include "network_interface_cl.h" /* clogin_user */
#include "schema_manager.h" /* sm_issystem */
#include "execute_schema.h"
#include "execute_statement.h" /* do_get_serial_obj_id */
#include "transaction_cl.h" /* tran_system_savepoint */
#include "optimizer.h" /* qo_plan_set_cost_fn */
#include "object_print.h" /* help_print_info */
#include "jsp_cl.h" /* jsp_find_stored_procedure */
#include "transform.h" /* DB_MAX_SERIAL_NAME_LENGTH */

// ================================================================
// authenticate methods
// ================================================================
static void au_add_user_method (MOP class_mop, DB_VALUE *returnval, DB_VALUE *name, DB_VALUE *password);
static void au_drop_user_method (MOP root, DB_VALUE *returnval, DB_VALUE *name);
static void au_find_user_method (MOP class_mop, DB_VALUE *returnval, DB_VALUE *name);
static void au_add_member_method (MOP user, DB_VALUE *returnval, DB_VALUE *memval);
static void au_drop_member_method (MOP user, DB_VALUE *returnval, DB_VALUE *memval);
static void au_set_password_method (MOP user, DB_VALUE *returnval, DB_VALUE *password);
static void au_set_password_encoded_method (MOP user, DB_VALUE *returnval, DB_VALUE *password);
static void au_set_password_encoded_sha1_method (MOP user, DB_VALUE *returnval, DB_VALUE *password);
static void au_describe_user_method (MOP user, DB_VALUE *returnval);
static void au_describe_root_method (MOP class_mop, DB_VALUE *returnval, DB_VALUE *info);
static void au_info_method (MOP class_mop, DB_VALUE *returnval, DB_VALUE *info);
static void au_login_method (MOP class_mop, DB_VALUE *returnval, DB_VALUE *user, DB_VALUE *password);
static void au_change_owner_method (MOP obj, DB_VALUE *return_val, DB_VALUE *class_val, DB_VALUE *owner_val);
static void au_change_trigger_owner_method (MOP obj, DB_VALUE *return_val, DB_VALUE *trigger_val, DB_VALUE *owner_val);
static void au_get_owner_method (MOP obj, DB_VALUE *returnval, DB_VALUE *class_);
static void au_check_authorization_method (MOP obj, DB_VALUE *returnval, DB_VALUE *class_, DB_VALUE *auth);
static void au_change_serial_owner_method (MOP obj, DB_VALUE *return_val, DB_VALUE *serial_val, DB_VALUE *owner_val);
static void au_change_sp_owner_method (MOP obj, DB_VALUE *returnval, DB_VALUE *sp, DB_VALUE *owner);

/* 'get_attribute_number' is a statically linked method used only for QA
   scenario */
static void get_attribute_number_method (DB_OBJECT *target, DB_VALUE *result, DB_VALUE *attr_name);
static void dbmeth_class_name (DB_OBJECT *self, DB_VALUE *result);
static void dbmeth_print (DB_OBJECT *self, DB_VALUE *result, DB_VALUE *msg);

// ================================================================
// optimizer methods
// ================================================================
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
static void qo_set_cost (DB_OBJECT *target, DB_VALUE *result, DB_VALUE *plan, DB_VALUE *cost);

// ================================================================
// dbmeth methods
// ================================================================
/*
 * These functions are provided just so we have some builtin gadgets that we can
 * use for quick and dirty method testing.  To get at them, alter your
 * favorite class like this:
 *
 * 	alter class foo
 * 		add method pickaname() string
 * 		function dbmeth_class_name;
 *
 * or
 *
 * 	alter class foo
 * 		add method pickaname(string) string
 * 		function dbmeth_print;
 *
 * After that you should be able to invoke "pickaname" on "foo" instances
 * to your heart's content.  dbmeth_class_name() will retrieve the class
 * name of the target instance and return it as a string; dbmeth_print()
 * will print the supplied value on stdout every time it is invoked.
 */


// ================================================================
// static link lists
// ================================================================
/*
 * db_static_links
 *
 * Since authorization is always linked in with the database, the
 * methods are defined statically.  The linkage will be done
 * during au_init even though it is redundant on subsequent
 * restart calls.
 */
static DB_METHOD_LINK db_static_links[] =
{
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

  {"qo_set_cost", (METHOD_LINK_FUNCTION) qo_set_cost},
  {"get_attribute_number", (METHOD_LINK_FUNCTION) get_attribute_number_method},
  {"dbmeth_class_name", (METHOD_LINK_FUNCTION) dbmeth_class_name},
  {"dbmeth_print", (METHOD_LINK_FUNCTION) dbmeth_print},
  {"au_change_sp_owner_method", (METHOD_LINK_FUNCTION) au_change_sp_owner_method},
  {"au_change_serial_owner_method", (METHOD_LINK_FUNCTION) au_change_serial_owner_method},

  {NULL, NULL}
};

/*
 * db_install_static_methods() - Called during the restart sequence to statically
 *                            link the methods defined in this file
 *    return: none
 */
void db_install_static_methods ()
{
  db_link_static_methods (&db_static_links[0]);
}

/*
 * au_add_user_method
 *   return: none
 *   class(in): class object
 *   returnval(out): return value of this method
 *   name(in):
 *   password(in):
 */
static void
au_add_user_method (MOP class_mop, DB_VALUE *returnval, DB_VALUE *name, DB_VALUE *password)
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
 * au_drop_user_method - Method interface for au_drop_user.
 *   return: none
 *   root(in):
 *   returnval(out): return value of this method
 *   name(in):
 */
static void
au_drop_user_method (MOP root, DB_VALUE *returnval, DB_VALUE *name)
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
static void
au_find_user_method (MOP class_mop, DB_VALUE *returnval, DB_VALUE *name)
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
 * au_add_member_method -  Method interface to au_add_member.
 *   return: none
 *   user(in): user object
 *   returnval(out): return value of this method
 *   memval(in):
 */
static void
au_add_member_method (MOP user, DB_VALUE *returnval, DB_VALUE *memval)
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
	      error = au_add_member (user, member);
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
 * au_drop_member_method -  Method interface for au_drop_member.
 *   return: none
 *   user(in): user object
 *   returnval(in): return value of this method
 *   memval(in):
 */
static void
au_drop_member_method (MOP user, DB_VALUE *returnval, DB_VALUE *memval)
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
 * au_set_password_method -  Method interface for au_set_password.
 *   return: none
 *   user(in): user object
 *   returnval(out): return value of this method
 *   password(in): new password
 */
static void
au_set_password_method (MOP user, DB_VALUE *returnval, DB_VALUE *password)
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
static void
au_set_password_encoded_method (MOP user, DB_VALUE *returnval, DB_VALUE *password)
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
static void
au_set_password_encoded_sha1_method (MOP user, DB_VALUE *returnval, DB_VALUE *password)
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
	  error = au_set_password (user, string + 1 /* 1 for prefix */, 0, ENCODE_PREFIX_SHA2_512);
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
 * au_describe_user_method() - Method interface to au_dump_user.
 *                             Can only be called when using the csql
 *                             interpreter since it dumps to stdout
 *   return: none
 *   user(in): user object
 *   returnval(out): return value of this method
 */
static void
au_describe_user_method (MOP user, DB_VALUE *returnval)
{
  db_make_null (returnval);
  if (user != NULL)
    {
      au_dump_user (user, stdout);
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
static void
au_describe_root_method (MOP class_mop, DB_VALUE *returnval, DB_VALUE *info)
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
static void
au_info_method (MOP class_mop, DB_VALUE *returnval, DB_VALUE *info)
{
  db_make_null (returnval);

  if (info != NULL && DB_IS_STRING (info) && !DB_IS_NULL (info) && db_get_string (info) != NULL)
    {
      /* this dumps stuff to stdout */
      help_print_info (db_get_string (info), stdout);
    }
}

/*
 * au_login_method - Method interface to au_login.
 *   return: none
 *   class_mop(in): class object
 *   returnval(out): return value of this method
 *   user(in): user name
 *   password(in): password
 */
static void
au_login_method (MOP class_mop, DB_VALUE *returnval, DB_VALUE *user, DB_VALUE *password)
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
 * au_change_owner_method - Method interface to au_change_owner
 *   return: none
 *   obj(in): class whose owner is to change
 *   returnval(out): return value of this method
 *   class(in):
 *   owner(in): new owner
 */
static void
au_change_owner_method (MOP obj, DB_VALUE *return_val, DB_VALUE *class_val, DB_VALUE *owner_val)
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

  error = au_change_class_owner (class_mop, owner_mop);
  if (error != NO_ERROR)
    {
      ASSERT_ERROR_AND_SET (error);
      db_make_error (return_val, error);
    }
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
au_change_trigger_owner_method (MOP obj, DB_VALUE *return_val, DB_VALUE *trigger_val, DB_VALUE *owner_val)
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
 * au_get_owner_method - Method interface to au_change_owner
 *   return: none
 *   obj(in):
 *   returnval(out): return value of this method
 *   class(in): class object
 */
static void
au_get_owner_method (MOP obj, DB_VALUE *returnval, DB_VALUE *class_)
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
static void
au_check_authorization_method (MOP obj, DB_VALUE *returnval, DB_VALUE *class_, DB_VALUE *auth)
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
 * au_change_sp_owner_method -
 *   return: none
 *   obj(in):
 *   returnval(in):
 *   sp(in):
 *   owner(in):
 */
static void
au_change_sp_owner_method (MOP obj, DB_VALUE *returnval, DB_VALUE *sp, DB_VALUE *owner)
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
 * au_change_serial_owner_method() - Method interface to au_change_serial_owner
 *   return: none
 *   obj(in): class whose owner is to change
 *   returnval(out): return value of this method
 *   serial(in): serial name
 *   owner(in): new owner
 */
static void
au_change_serial_owner_method (MOP obj, DB_VALUE *return_val, DB_VALUE *serial_val, DB_VALUE *owner_val)
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
 * qo_set_cost () - csql method interface to qo_set_cost_fn()
 *   return: nothing
 *   target(in): The target of the method; we don't care
 *   result(in): The result returned by the method; we don't care
 *   plan(in): The plan type to get jacked
 *   cost(in): The new cost for that plan type
 *
 * Note: This should get registered in the schema as
 *
 *		alter class foo
 *			add class method opt_set_cost(string, string)
 *			function qo_set_cost;
 *
 *	No libraries or other files are required, since this will
 *	always be linked in to the base executable.  Once linked, you
 *	should be able to do things like
 *
 *		call opt_set_cost("iscan", "0") on class foo
 *
 *	from csql
 */
static void
qo_set_cost (DB_OBJECT *target, DB_VALUE *result, DB_VALUE *plan, DB_VALUE *cost)
{
  const char *plan_string;
  const char *cost_string;

  switch (DB_VALUE_TYPE (plan))
    {
    case DB_TYPE_STRING:
    case DB_TYPE_CHAR:
    case DB_TYPE_NCHAR:
      plan_string = db_get_string (plan);
      break;
    default:
      plan_string = "unknown";
      break;
    }

  switch (DB_VALUE_TYPE (cost))
    {
    case DB_TYPE_STRING:
    case DB_TYPE_CHAR:
    case DB_TYPE_NCHAR:
      cost_string = db_get_string (cost);
      break;
    default:
      cost_string = "d";
      break;
    }

  /*
   * This relies on the fact that qo_plan_set_cost_fn is returning a
   * CONST string.  That way we don't need to dup it, and therefore we
   * won't leak it when the return value is discarded.
   */
  plan_string = qo_plan_set_cost_fn (plan_string, cost_string[0]);
  if (plan_string != NULL)
    {
      db_make_string (result, plan_string);
    }
  else
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      db_make_error (result, ER_GENERIC_ERROR);
    }
}

/*
 * get_attribute_number - attribute number of the given attribute/class
 *   return:
 *   arg1(in):
 *   arg2(in):
 */
void
get_attribute_number_method (DB_OBJECT *target, DB_VALUE *result, DB_VALUE *attr_name)
{
  int attrid, shared;
  DB_DOMAIN *dom;

  db_make_null (result);

  if (DB_VALUE_TYPE (attr_name) != DB_TYPE_STRING)
    {
      return;
    }

  /* we will only look for regular attributes and not class attributes. this is a limitation of this method. */
  if (sm_att_info (target, db_get_string (attr_name), &attrid, &dom, &shared, 0 /* non-class attrs */ )
      < 0)
    {
      return;
    }

  db_make_int (result, attrid);
}

/*
 * dbmeth_class_name() -
 *   return: None
 *   self(in): Class object
 *   result(out): DB_VALUE for a class name
 *
 * Note: Position of function arguments must be kept
 *   for pre-defined function pointers(au_static_links)
 */
static void
dbmeth_class_name (DB_OBJECT *self, DB_VALUE *result)
{
  const char *cname;

  cname = db_get_class_name (self);

  /*
   * Make a string and clone it so that it won't become invalid if the
   * underlying class object that gave us the string goes away.  Of
   * course, this gives the responsibility for freeing the cloned
   * string to someone else; is anybody accepting it?
   */
  db_make_string (result, cname);
}

/*
 * dbmeth_print() -
 *   return: None
 *   self(in): Class object
 *   result(out): NULL value
 *   msg(in): DB_VALUE for a message
 *
 * Note: Position of function arguments must be kept
 *   for pre-defined function pointers(au_static_links)
 */
static void
dbmeth_print (DB_OBJECT *self, DB_VALUE *result, DB_VALUE *msg)
{
  db_value_print (msg);
  printf ("\n");
  db_make_null (result);
}