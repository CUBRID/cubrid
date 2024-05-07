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
#include "transform.h"
#include "schema_system_catalog_constants.h"
#include "environment_variable.h"

#include "object_print.h"
#include "optimizer.h"
#include "network_interface_cl.h"
#include "printer.hpp"
#include "authenticate_access_auth.hpp"
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
		      au_print_grants (auth, fp);
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
      au_print_grants (auth, fp);
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
