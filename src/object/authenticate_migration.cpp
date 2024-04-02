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
 * authenticate_migration.cpp -
 */
#include "authenticate.h"

#include "db.h"
#include "dbi.h"
#include "dbtype.h"

#include "authenticate_grant.hpp"
#include "object_accessor.h"
#include "object_print.h"
#include "set_object.h"
#include "schema_manager.h" /* sm_get_ch_name () */
#include "extract_schema.hpp" /* extract_context */
#include "printer.hpp" /* print_output */

/*
 * CLASS_GRANT
 *
 * Maintains information about a desired grant request.
 */
typedef struct class_grant CLASS_GRANT;
struct class_grant
{
  struct class_grant *next;

  struct class_user *user;
  int cache;
};

/*
 * CLASS_USER
 *
 * Maintains information about a desired grant subject user.
 */
typedef struct class_user CLASS_USER;
struct class_user
{
  struct class_user *next;

  MOP obj;

  CLASS_GRANT *grants;
  int available_auth;
};

/*
 * CLASS_AUTH
 *
 * Maintains information about the grants on a particular class.
 */
typedef struct class_auth CLASS_AUTH;
struct class_auth
{

  MOP class_mop;
  MOP owner;
  CLASS_USER *users;
};

static CLASS_GRANT *make_class_grant (CLASS_USER *user, int cache);
static CLASS_USER *make_class_user (MOP user_obj);
static void free_class_grants (CLASS_GRANT *grants);
static void free_class_users (CLASS_USER *users);
static CLASS_USER *find_or_add_user (CLASS_AUTH *auth, MOP user_obj);
static int add_class_grant (CLASS_AUTH *auth, MOP source, MOP user, int cache);
static int build_class_grant_list (CLASS_AUTH *cl_auth, MOP class_mop);

static void issue_grant_statement (extract_context &ctxt, print_output &output_ctx, CLASS_AUTH *auth,
				   CLASS_GRANT *grant, int authbits);
static int class_grant_loop (extract_context &ctxt, print_output &output_ctx, CLASS_AUTH *auth);

/*
 * MIGRATION SUPPORT
 *
 * These functions provide a way to dump the authorization catalog
 * as a sequence of CSQL statements.  When the statements are evaluated
 * by the interpreter, it will reconstruct the authorization catalog.
 */

/*
 * au_export_users - Generates a sequence of add_user and add_member method
 *                   calls that when evaluated, will re-create the current
 *                   user/group hierarchy.
 *   return: error code
 *   output_ctx(in/out): print context
 */
int
au_export_users (extract_context &ctxt, print_output &output_ctx)
{
  int error = NO_ERROR;
  DB_SET *direct_groups = NULL;
  DB_VALUE value, gvalue;
  MOP user = NULL, pwd = NULL;
  int g, gcard;
  const char *uname = NULL, *str = NULL, *gname = NULL, *comment = NULL;
  char passbuf[AU_MAX_PASSWORD_BUF] = { '\0' };
  char *query = NULL;
  size_t query_size;
  DB_QUERY_RESULT *query_result = NULL;
  DB_QUERY_ERROR query_error;
  DB_VALUE user_val;
  DB_VALUE user_group[2] = { 0, };
  const char *dba_query = "select [%s] from [%s];";
  const char *user_query = "select [%s] from [%s] where name='%s';";
  const char *group_query =
	  "select u.name, [t].[g].name from [db_user] [u], TABLE([u].[groups]) [t]([g]) where [t].[g].name = '%s';";
  char encrypt_mode = ENCODE_PREFIX_DEFAULT;
  char *upper_case_name = NULL;
  size_t upper_case_name_size = 0;

  if (ctxt.is_dba_user || ctxt.is_dba_group_member)
    {
      query_size = strlen (dba_query) + strlen (AU_USER_CLASS_NAME) * 2;
      query = (char *) malloc (query_size);
      if (query == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, query_size);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
      sprintf (query, dba_query, AU_USER_CLASS_NAME, AU_USER_CLASS_NAME);
    }
  else
    {
      upper_case_name_size = intl_identifier_upper_string_size (ctxt.login_user);
      upper_case_name = (char *) malloc (upper_case_name_size + 1);
      if (upper_case_name == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, upper_case_name_size);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}

      intl_identifier_upper (ctxt.login_user, upper_case_name);

      query_size = strlen (user_query) + strlen (AU_USER_CLASS_NAME) * 2 + strlen (upper_case_name);
      query = (char *) malloc (query_size);
      if (query == NULL)
	{
	  if (upper_case_name != NULL)
	    {
	      free_and_init (upper_case_name);
	    }

	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, query_size);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}

      sprintf (query, user_query, AU_USER_CLASS_NAME, AU_USER_CLASS_NAME, upper_case_name);
    }

  error = db_compile_and_execute_local (query, &query_result, &query_error);
  /* error is row count if not negative. */
  if (error < NO_ERROR)
    {
      if (upper_case_name != NULL)
	{
	  free_and_init (upper_case_name);
	}

      if (query != NULL)
	{
	  free_and_init (query);
	}

      if (query_result != NULL)
	{
	  db_query_end (query_result);
	  query_result = NULL;
	}
      return error;
    }

  while (db_query_next_tuple (query_result) == DB_CURSOR_SUCCESS)
    {
      if (db_query_get_tuple_value (query_result, 0, &user_val) != NO_ERROR)
	{
	  continue;
	}

      if (DB_IS_NULL (&user_val))
	{
	  user = NULL;
	}
      else
	{
	  user = db_get_object (&user_val);
	}

      uname = au_get_user_name (user);
      strcpy (passbuf, "");
      encrypt_mode = ENCODE_PREFIX_DEFAULT;

      /* retrieve password */
      error = obj_get (user, "password", &value);
      if (error == NO_ERROR)
	{
	  if (DB_IS_NULL (&value))
	    {
	      pwd = NULL;
	    }
	  else
	    {
	      pwd = db_get_object (&value);
	    }

	  if (pwd != NULL)
	    {
	      error = obj_get (pwd, "password", &value);
	      if (error == NO_ERROR)
		{
		  if (!DB_IS_NULL (&value) && DB_IS_STRING (&value))
		    {
		      /*
		       * copy password string using malloc
		       * to be consistent with encrypt_password
		       */
		      str = db_get_string (&value);
		      if (IS_ENCODED_DES (str))
			{
			  /* strip off the prefix so its readable */
			  snprintf (passbuf, AU_MAX_PASSWORD_BUF - 1, "%s", str + 1);
			  encrypt_mode = ENCODE_PREFIX_DES;
			}
		      else if (IS_ENCODED_SHA1 (str))
			{
			  /* strip off the prefix so its readable */
			  snprintf (passbuf, AU_MAX_PASSWORD_BUF - 1, "%s", str + 1);
			  encrypt_mode = ENCODE_PREFIX_SHA1;
			}
		      else if (IS_ENCODED_SHA2_512 (str))
			{
			  /* not strip off the prefix */
			  snprintf (passbuf, AU_MAX_PASSWORD_BUF - 1, "%s", str);
			  encrypt_mode = ENCODE_PREFIX_SHA2_512;
			}
		      else if (strlen (str))
			{
			  /* sha2 hashing with prefix */
			  encrypt_password_sha2_512 (str, passbuf);
			}
		      ws_free_string (str);
		    }
		}
	    }
	}

      /* retrieve comment */
      error = obj_get (user, "comment", &value);
      if (error == NO_ERROR)
	{
	  if (DB_IS_NULL (&value))
	    {
	      comment = NULL;
	    }
	  else
	    {
	      comment = db_get_string (&value);
	    }
	}

      if (error == NO_ERROR)
	{
	  if (!ws_is_same_object (user, Au_dba_user) && !ws_is_same_object (user, Au_public_user))
	    {
	      if (!strlen (passbuf))
		{
		  if (ctxt.is_dba_user || ctxt.is_dba_group_member)
		    {
		      output_ctx ("call [add_user]('%s', '') on class [db_root];\n", uname);
		    }
		}
	      else
		{
		  if (ctxt.is_dba_user || ctxt.is_dba_group_member)
		    {
		      output_ctx ("call [add_user]('%s', '') on class [db_root] to [auser];\n", uname);
		      if (encrypt_mode == ENCODE_PREFIX_DES)
			{
			  output_ctx ("call [set_password_encoded]('%s') on [auser];\n", passbuf);
			}
		      else
			{
			  output_ctx ("call [set_password_encoded_sha1]('%s') on [auser];\n", passbuf);
			}
		    }
		}
	    }
	  else
	    {
	      if (strlen (passbuf))
		{
		  output_ctx ("call [find_user]('%s') on class [db_user] to [auser];\n", uname);
		  if (encrypt_mode == ENCODE_PREFIX_DES)
		    {
		      output_ctx ("call [set_password_encoded]('%s') on [auser];\n", passbuf);
		    }
		  else
		    {
		      output_ctx ("call [set_password_encoded_sha1]('%s') on [auser];\n", passbuf);
		    }
		}
	    }

	  /* export comment */
	  if (comment != NULL && comment[0] != '\0')
	    {
	      output_ctx ("ALTER USER [%s] ", uname);
	      help_print_describe_comment (output_ctx, comment);
	      output_ctx (";\n");
	    }
	}

      /* remember, these were allocated in the workspace */
      if (uname != NULL)
	{
	  ws_free_string_and_init (uname);
	}
      if (comment != NULL)
	{
	  ws_free_string_and_init (comment);
	}
    }

  /* group hierarchy */
  if (ctxt.is_dba_user || ctxt.is_dba_group_member)
    {
      if (db_query_first_tuple (query_result) == DB_CURSOR_SUCCESS)
	{
	  do
	    {
	      if (db_query_get_tuple_value (query_result, 0, &user_val) != NO_ERROR)
		{
		  continue;
		}

	      if (DB_IS_NULL (&user_val))
		{
		  user = NULL;
		}
	      else
		{
		  user = db_get_object (&user_val);
		}

	      uname = au_get_user_name (user);
	      if (uname == NULL)
		{
		  continue;
		}

	      if (au_get_set (user, "direct_groups", &direct_groups) != NO_ERROR)
		{
		  ws_free_string_and_init (uname);
		  continue;
		}

	      gcard = set_cardinality (direct_groups);
	      for (g = 0; g < gcard && !error; g++)
		{
		  if (set_get_element (direct_groups, g, &gvalue) != NO_ERROR)
		    {
		      continue;
		    }

		  if (ws_is_same_object (db_get_object (&gvalue), Au_public_user))
		    {
		      continue;
		    }

		  error = obj_get (db_get_object (&gvalue), "name", &value);
		  if (error != NO_ERROR)
		    {
		      continue;
		    }

		  if (DB_IS_NULL (&value))
		    {
		      gname = NULL;
		    }
		  else
		    {
		      gname = db_get_string (&value);
		    }

		  if (gname != NULL)
		    {
		      output_ctx ("call [find_user]('%s') on class [db_user] to [g_%s];\n", gname, gname);
		      output_ctx ("call [add_member]('%s') on [g_%s];\n", uname, gname);
		    }
		}

	      set_free (direct_groups);
	      if (uname != NULL)
		{
		  ws_free_string_and_init (uname);
		}

	      if (gname != NULL)
		{
		  ws_free_string_and_init (gname);
		}
	    }
	  while (db_query_next_tuple (query_result) == DB_CURSOR_SUCCESS);
	}

      if (query_result != NULL)
	{
	  db_query_end (query_result);
	  query_result = NULL;
	}

      if (upper_case_name != NULL)
	{
	  free_and_init (upper_case_name);
	}

      if (query != NULL)
	{
	  free_and_init (query);
	}

    }
  else
    {
      // Initializing memory used by user query ("select [%s] from [%s] where name='%s';")
      if (query_result != NULL)
	{
	  db_query_end (query_result);
	  query_result = NULL;
	}

      if (upper_case_name != NULL)
	{
	  free_and_init (upper_case_name);
	}

      if (query != NULL)
	{
	  free_and_init (query);
	}

      upper_case_name_size = intl_identifier_upper_string_size (ctxt.login_user);
      upper_case_name = (char *) malloc (upper_case_name_size + 1);
      if (upper_case_name == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, upper_case_name_size);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}

      intl_identifier_upper (ctxt.login_user, upper_case_name);

      query_size = strlen (group_query) + strlen (upper_case_name);
      query = (char *) malloc (query_size);
      if (query == NULL)
	{
	  if (upper_case_name != NULL)
	    {
	      free_and_init (upper_case_name);
	    }

	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, query_size);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}

      sprintf (query, group_query, upper_case_name);

      error = db_compile_and_execute_local (query, &query_result, &query_error);

      /* error is row count if not negative. */
      if (error < NO_ERROR)
	{
	  if (query_result != NULL)
	    {
	      db_query_end (query_result);
	      query_result = NULL;
	    }

	  if (upper_case_name != NULL)
	    {
	      free_and_init (upper_case_name);
	    }

	  if (query != NULL)
	    {
	      free_and_init (query);
	    }
	  return error;
	}

      while (db_query_next_tuple (query_result) == DB_CURSOR_SUCCESS)
	{
	  if (db_query_get_tuple_value (query_result, 0, &user_group[0]) != NO_ERROR)
	    {
	      continue;
	    }

	  if (db_query_get_tuple_value (query_result, 1, &user_group[1]) != NO_ERROR)
	    {
	      continue;
	    }

	  if (DB_IS_NULL (&user_group[0]) == false)
	    {
	      uname = db_get_string (&user_group[0]);
	    }

	  if (DB_IS_NULL (&user_group[1]) == false)
	    {
	      gname = db_get_string (&user_group[1]);
	    }

	  if (uname != NULL && gname != NULL)
	    {
	      output_ctx ("call [find_user]('%s') on class [db_user] to [g_%s];\n", gname, gname);
	      output_ctx ("call [add_member]('%s') on [g_%s];\n", uname, gname);
	    }

	  if (uname != NULL)
	    {
	      ws_free_string_and_init (uname);
	    }

	  if (gname != NULL)
	    {
	      ws_free_string_and_init (gname);
	    }
	}

      if (query_result != NULL)
	{
	  db_query_end (query_result);
	  query_result = NULL;
	}

      if (upper_case_name != NULL)
	{
	  free_and_init (upper_case_name);
	}

      if (query != NULL)
	{
	  free_and_init (query);
	}
    }

  return (error);
}



/*
 * au_export_grants() - Issues a sequence of CSQL grant statements related
 *                      to the given class.
 *   return: error code
 *   output_ctx(in): output context
 *   class_mop(in): class of interest
 *   quoted_id_flag(in):
 */
int
au_export_grants (extract_context &ctxt, print_output &output_ctx, MOP class_mop)
{
  int error = NO_ERROR;
  CLASS_AUTH cl_auth;
  CLASS_USER *u;
  int statements, ecount;
  char *uname;

  cl_auth.class_mop = class_mop;
  cl_auth.owner = au_get_class_owner (class_mop);
  cl_auth.users = NULL;

  /* make an entry for the owner with complete authorization */
  u = find_or_add_user (&cl_auth, cl_auth.owner);
  u->available_auth = AU_FULL_AUTHORIZATION;

  /* add entries for the other users with authorization on this class */
  error = build_class_grant_list (&cl_auth, class_mop);
  if (error == NO_ERROR)
    {
      /* loop through the grant list, issuing grant statements */
      while ((statements = class_grant_loop (ctxt, output_ctx, &cl_auth)))
	;

      for (u = cl_auth.users, ecount = 0; u != NULL; u = u->next)
	{
	  if (u->grants != NULL)
	    {
	      uname = au_get_user_name (u->obj);

	      /*
	       * should this be setting an error condition ?
	       * for now, leave a comment in the output file
	       */
	      output_ctx ("/*");
	      output_ctx (msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_AUTHORIZATION,
					  MSGCAT_AUTH_GRANT_DUMP_ERROR), uname);
	      output_ctx ("*/\n");
	      ws_free_string (uname);
	      ecount++;
	    }
	}
      if (ecount)
	{
	  error = ER_GENERIC_ERROR;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	}
    }

  free_class_users (cl_auth.users);

  return (error);
}


/*
 * GRANT EXPORT
 *
 * This is in support of the authorization migration utilities.  We build
 * hierarchy of grant information and then generate a sequence of
 * CSQL statemenets to recreate the grants.  Note that the grants have
 * to be done in order of dependencies.
 */

/*
 * make_class_grant - Create a temporary class grant structure.
 *   return: new class grant
 *   user(in): subject user
 *   cache(in): authorization bits to grant
 */
static CLASS_GRANT *
make_class_grant (CLASS_USER *user, int cache)
{
  CLASS_GRANT *grant;

  if ((grant = (CLASS_GRANT *) malloc (sizeof (CLASS_GRANT))) != NULL)
    {
      grant->next = NULL;
      grant->user = user;
      grant->cache = cache;
    }
  return (grant);
}

/*
 * make_class_user - Create a temporary class user structure.
 *   return: new class user structure
 *   user_obj(in): pointer to actual database object for this user
 */
static CLASS_USER *
make_class_user (MOP user_obj)
{
  CLASS_USER *u;

  if ((u = (CLASS_USER *) malloc (sizeof (CLASS_USER))) != NULL)
    {
      u->next = NULL;
      u->obj = user_obj;
      u->grants = NULL;

      /*
       * This authorization of this user class structure would normally
       * be filled in by examining authorizations granted by other users.
       * The DBA user is special in that it should have full authorization
       * without being granted it by any users.  Therefore we need to set
       * the authorization explicitly before any code checks it.
       */
      if (ws_is_same_object (user_obj, Au_dba_user))
	{
	  u->available_auth = AU_FULL_AUTHORIZATION;
	}
      else
	{
	  u->available_auth = 0;
	}
    }
  return (u);
}

/*
 * free_class_grants - Frees list of temporary class grant structures.
 *   return: none
 *   grants(in): list of class grant structures
 */
static void
free_class_grants (CLASS_GRANT *grants)
{
  CLASS_GRANT *g, *next;

  for (g = grants, next = NULL; g != NULL; g = next)
    {
      next = g->next;
      free_and_init (g);
    }
}

/*
 * find_or_add_user - Adds an entry in the user list of a class authorization
 *                    structure for the user object.
 *   return: class user structures
 *   auth(in):class authorization state
 *   user_obj(in):database user object to add
 *
 * Note: If there is already an entry in the list, it returns the found entry
 */
static CLASS_USER *
find_or_add_user (CLASS_AUTH *auth, MOP user_obj)
{
  CLASS_USER *u, *last;

  for (u = auth->users, last = NULL; u != NULL && !ws_is_same_object (u->obj, user_obj); u = u->next)
    {
      last = u;
    }

  if (u == NULL)
    {
      u = make_class_user (user_obj);
      if (last == NULL)
	{
	  auth->users = u;
	}
      else
	{
	  last->next = u;
	}
    }
  return (u);
}

/*
 * add_class_grant - Makes an entry in the class authorization state
 *                   for a desired grant.
 *   return: error code
 *   auth(in): class authorization state
 *   source(in): source user object
 *   user(in): subject user object
 *   cache(in): authorization cache bits
 */
static int
add_class_grant (CLASS_AUTH *auth, MOP source, MOP user, int cache)
{
  CLASS_USER *u, *gu;
  CLASS_GRANT *g;

  u = find_or_add_user (auth, source);

  for (g = u->grants; g != NULL && !ws_is_same_object (g->user->obj, user); g = g->next)
    ;

  if (g == NULL)
    {
      if (!ws_is_same_object (source, user))
	{
	  gu = find_or_add_user (auth, user);
	  g = make_class_grant (gu, cache);
	  if (g == NULL)
	    {
	      assert (er_errid () != NO_ERROR);
	      return er_errid ();
	    }
	  g->next = u->grants;
	  u->grants = g;
	}
    }
  else
    {
      /*
       * this shouldn't happen, multiple grants from source should already have
       * been combined
       */
      g->cache |= cache;
    }
  return NO_ERROR;
}

/*
 * free_class_users - Frees list of class user objects.
 *   return: none
 *   users(in): class user list
 */
static void
free_class_users (CLASS_USER *users)
{
  CLASS_USER *u, *next;

  for (u = users, next = NULL; u != NULL; u = next)
    {
      next = u->next;
      free_class_grants (u->grants);
      free_and_init (u);
    }
}


/*
 * build_class_grant_list - Adds grant entries in cl_auth for every grant entry
 *                          found in the authorization catalog for
 *                          the class "class".
 *   return: error code
 *   cl_auth(in):  class authorization state
 *   class(in): class object
 *
 * Note: The db_root class used to have a user attribute which was a set
 *       containing the object-id for all users.  The users attribute has been
 *       eliminated for performance reasons.  A query on the db_user class is
 *       now used to find all users.
 */
static int
build_class_grant_list (CLASS_AUTH *cl_auth, MOP class_mop)
{
  int error;
  MOP user, auth, source;
  DB_SET *grants;
  DB_VALUE value;
  int j, gsize, cache;
  char *query;
  size_t query_size;
  DB_QUERY_RESULT *query_result;
  DB_QUERY_ERROR query_error;
  DB_VALUE user_val;
  const char *qp1 = "select [%s] from [%s];";

  query_size = strlen (qp1) + strlen (AU_USER_CLASS_NAME) * 2;
  query = (char *) malloc (query_size);
  if (query == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, query_size);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  sprintf (query, qp1, AU_USER_CLASS_NAME, AU_USER_CLASS_NAME);

  error = db_compile_and_execute_local (query, &query_result, &query_error);
  if (error < 0)
    /* error is row count if not negative. */
    {
      free_and_init (query);
      return error;
    }

  while (db_query_next_tuple (query_result) == DB_CURSOR_SUCCESS)
    {
      if (db_query_get_tuple_value (query_result, 0, &user_val) == NO_ERROR)
	{
	  if (DB_IS_NULL (&user_val))
	    {
	      user = NULL;
	    }
	  else
	    {
	      user = db_get_object (&user_val);
	    }

	  error = au_get_object (user, "authorization", &auth);
	  /* ignore the deleted object errors */
	  if (error != NO_ERROR)
	    {
	      if (error == ER_HEAP_UNKNOWN_OBJECT)
		{
		  error = NO_ERROR;
		}
	    }
	  else
	    {
	      if ((error = get_grants (auth, &grants, 1)) == NO_ERROR)
		{
		  gsize = set_size (grants);
		  for (j = 0; j < gsize; j += GRANT_ENTRY_LENGTH)
		    {
		      error = set_get_element (grants, GRANT_ENTRY_CLASS (j), &value);
		      if (error == NO_ERROR && DB_VALUE_TYPE (&value) == DB_TYPE_OBJECT
			  && db_get_object (&value) == class_mop)
			{
			  error = set_get_element (grants, GRANT_ENTRY_SOURCE (j), &value);
			  if (error == NO_ERROR && DB_VALUE_TYPE (&value) == DB_TYPE_OBJECT && !DB_IS_NULL (&value)
			      && (source = db_get_object (&value)) != NULL)
			    {
			      error = set_get_element (grants, GRANT_ENTRY_CACHE (j), &value);
			      if (error == NO_ERROR)
				{
				  cache = db_get_int (&value);
				  error = add_class_grant (cl_auth, source, user, cache);
				}
			    }
			}
		    }
		  set_free (grants);
		}
	    }
	}			/* if */
    }				/* while */

  db_query_end (query_result);
  free_and_init (query);

  return (error);
}


/*
 * issue_grant_statement - Generates an CSQL "grant" statement.
 *   return: none
 *   output_ctx(in/out): output context
 *   auth(in): class authorization state
 *   grant(in): desired grant
 *   authbits(in): specific authorization to grant
 *   quoted_id_flag(in):
 */
static void
issue_grant_statement (extract_context &ctxt, print_output &output_ctx, CLASS_AUTH *auth, CLASS_GRANT *grant,
		       int authbits)
{
  const char *gtype;
  char owner_name[DB_MAX_IDENTIFIER_LENGTH] = { '\0' };
  char *class_name = NULL;
  char *username;
  int typebit;

  typebit = authbits & AU_TYPE_MASK;
  switch (typebit)
    {
    case AU_SELECT:
      gtype = "SELECT";
      break;
    case AU_INSERT:
      gtype = "INSERT";
      break;
    case AU_UPDATE:
      gtype = "UPDATE";
      break;
    case AU_DELETE:
      gtype = "DELETE";
      break;
    case AU_ALTER:
      gtype = "ALTER";
      break;
    case AU_INDEX:
      gtype = "INDEX";
      break;
    case AU_EXECUTE:
      gtype = "EXECUTE";
      break;
    default:
      gtype = "???";
      break;
    }
  SPLIT_USER_SPECIFIED_NAME (sm_get_ch_name (auth->class_mop), owner_name, class_name);
  username = au_get_user_name (grant->user->obj);

  output_ctx ("GRANT %s ON ", gtype);

  if (ctxt.is_dba_user || ctxt.is_dba_group_member)
    {
      output_ctx ("[%s].[%s]", owner_name, class_name);
    }
  else
    {
      output_ctx ("[%s]", class_name);
    }

  if (username != NULL)
    {
      output_ctx (" TO [%s]", username);
    }
  else
    {
      output_ctx (" TO %s", "???");
    }

  if (authbits & (typebit << AU_GRANT_SHIFT))
    {
      output_ctx (" WITH GRANT OPTION");
    }
  output_ctx (";\n");

  ws_free_string (username);
}


/*
 * class_grant_loop - Makes a pass on the authorization user list and
 *                    issues grant statements for any users that are able.
 *                    Returns the number of statements issued
 *   return: number of statements issued
 *   output_ctx(in): output context
 *   auth(in): class authorization state
 *   quoted_id_flag(in):
 *
 * Note:
 * If this resturns zero and the user list is not empty, it indicates
 * that there are illegal grants in the hierarchy that were not rooted
 * in the class owner object.
 *
 * It would likely be more efficient if rather than making a full pass
 * on the list we evaluate the first node in the list and then recursively
 * evaluate every mode affected by the first evaluation.  If the first
 * node results in no evaluations, we move to the next node in the list.
 *
 * This will tend to get grants to come out "depth first" which may be
 * more logical when examining the resulting statements.  It will probably
 * result in fewer traversals of the user list as well ?
 *
 * TODO : LP64
 */
static int
class_grant_loop (extract_context &ctxt, print_output &output_ctx, CLASS_AUTH *auth)
{
#define AU_MIN_BIT 1		/* AU_SELECT */
#define AU_MAX_BIT 0x40		/* AU_EXECUTE */

  CLASS_USER *user;
  CLASS_GRANT *grant, *prev_grant, *next_grant;
  int statements = 0;
  int mask, authbits;

  for (user = auth->users; user != NULL; user = user->next)
    {
      for (grant = user->grants, prev_grant = NULL, next_grant = NULL; grant != NULL; grant = next_grant)
	{
	  next_grant = grant->next;
	  mask = AU_SELECT;
	  for (mask = AU_MIN_BIT; mask <= AU_MAX_BIT; mask = mask << 1)
	    {
	      if (grant->cache & mask)
		{
		  /* combine auth type & grant option bit */
		  authbits = mask | (grant->cache & (mask << AU_GRANT_SHIFT));
		  /*
		   * if the user has these same bits available,
		   * issue the grant
		   */
		  if ((user->available_auth & authbits) == authbits)
		    {
		      if (!ws_is_same_object (auth->users->obj, grant->user->obj))
			{
			  issue_grant_statement (ctxt, output_ctx, auth, grant, authbits);
			}

		      /* turn on grant bits in the granted user */
		      grant->user->available_auth |= authbits;
		      /* turn off the pending grant bits in granting user */
		      grant->cache &= ~authbits;
		      statements++;
		    }
		}
	    }
	  if (grant->cache == 0)
	    {
	      /* no more grants, remove it from the list */
	      if (prev_grant == NULL)
		{
		  user->grants = grant->next;
		}
	      else
		{
		  prev_grant->next = grant->next;
		}
	      grant->next = NULL;
	      free_class_grants (grant);
	    }
	  else
	    {
	      prev_grant = grant;
	    }
	}
      /*
       * could remove user from the list but can't free it because
       * structure may be referenced by a grant inside another user
       */
    }
  return (statements);
}
