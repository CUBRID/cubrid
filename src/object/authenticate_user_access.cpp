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
 * authenticate_user_access.cpp -
 */

#include "authenticate_user_access.hpp"

#include "authenticate.h"
#include "authenticate_cache.hpp"
#include "authenticate_auth_access.hpp"

#include "set_object.h"
#include "dbtype.h"
#include "error_manager.h"
#include "object_accessor.h"
#include "object_primitive.h"
#include "network_interface_cl.h"
#include "transaction_cl.h" /* TM_TRAN_READ_FETCH_VERSION */

#include "db.h"
#include "dbi.h"
#include "schema_manager.h"
#include "schema_system_catalog_constants.h"

/*
 * USER/GROUP ACCESS
 */

static MOP au_make_user (const char *name);
static int au_add_direct_groups (DB_SET *new_groups, DB_VALUE *value);
static int au_compute_groups (MOP member, const char *name);
static int au_add_member_internal (MOP group, MOP member, int new_user);

/*
 * au_find_user - Find a user object by name.
 *   return: user object
 *   user_name(in): name
 *
 * Note: The db_root class used to have a users attribute which was a set
 *       containing the object-id for all users.
 *       The users attribute has been eliminated for performance reasons.
 *       A query is now used to find the user.  Since the user name is not
 *       case insensitive, it is set to upper case in the query.  This forces
 *       user names to be set to upper case when users are added.
 */
MOP
au_find_user (const char *user_name)
{
  MOP obj, user = NULL;
  int save;
  char *query;
  DB_QUERY_RESULT *query_result;
  DB_QUERY_ERROR query_error;
  int error = NO_ERROR;
  DB_VALUE user_val;
  const char *qp1 = "select [%s] from [%s] where [name] = '%s' using index none";
  MOP user_class;
  char *upper_case_name;
  size_t upper_case_name_size;
  DB_VALUE user_name_string;

  if (user_name == NULL)
    {
      return NULL;
    }

  {
    /*
     * To reduce unnecessary code execution,
     * the current schema name can be used instead of the current user name.
     *
     * Returns the current user object when the user name is the same as the current schema name.
     *
     * Au_user_name cannot be used because it does not always store the current user name.
     * When au_login_method () is called, Au_user_name is not changed.
     */
    const char *sc_name = sc_current_schema_name ();
    if (Au_user && sc_name && sc_name[0] != '\0' && intl_identifier_casecmp (sc_name, user_name) == 0)
      {
	return Au_user;
      }
  }

  /* disable checking of internal authorization object access */
  AU_DISABLE (save);

  user = NULL;

  upper_case_name_size = intl_identifier_upper_string_size (user_name);
  upper_case_name = (char *) malloc (upper_case_name_size + 1);
  if (upper_case_name == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, upper_case_name_size);
      return NULL;
    }
  intl_identifier_upper (user_name, upper_case_name);

  /*
   * first try to find the user id by index. This is faster than
   * a query, and will not get blocked out as a server request
   * if the query processing resources are all used up at the moment.
   * This is primarily of importance during logging in.
   */
  user_class = db_find_class ("db_user");
  if (user_class)
    {
      db_make_string (&user_name_string, upper_case_name);
      user = obj_find_unique (user_class, "name", &user_name_string, AU_FETCH_READ);
    }
  error = er_errid ();

  if (error != NO_ERROR)
    {
      if (error == ER_OBJ_OBJECT_NOT_FOUND)
	{
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_AU_INVALID_USER, 1, user_name);
	}
      goto exit;
    }

  if (error == NO_ERROR && !user)
    {
      /* proceed with the query version of the function */
      query = (char *) malloc (strlen (qp1) + (2 * strlen (AU_USER_CLASS_NAME)) + strlen (upper_case_name) + 1);
      if (query)
	{
	  sprintf (query, qp1, AU_USER_CLASS_NAME, AU_USER_CLASS_NAME, upper_case_name);

	  lang_set_parser_use_client_charset (false);
	  error = db_compile_and_execute_local (query, &query_result, &query_error);
	  lang_set_parser_use_client_charset (true);
	  /* error is row count if not negative. */
	  if (error > 0)
	    {
	      if (db_query_first_tuple (query_result) == DB_CURSOR_SUCCESS)
		{
		  if (db_query_get_tuple_value (query_result, 0, &user_val) == NO_ERROR)
		    {
		      if (DB_IS_NULL (&user_val))
			{
			  obj = NULL;
			}
		      else
			{
			  obj = db_get_object (&user_val);
			}
		      if (obj)
			{
			  user = obj;
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

exit:
  AU_ENABLE (save);

  if (upper_case_name)
    {
      free_and_init (upper_case_name);
    }
  return (user);
}

/*
 * au_find_user_to_drop - Find a user object by name for dropping.
 *
 *   return: error code
 *   user_name(in): name
 *   user(out): user object
 *
 * Note:  X_Lock will be added on this user_object
          We also need check whether ths user is an active user.
 */
int
au_find_user_to_drop (const char *user_name, MOP *user)
{
  int error = NO_ERROR;
  bool existed;
  MOP user_class;
  char *upper_case_name = NULL;
  size_t upper_case_name_size;
  DB_VALUE user_name_string;

  *user = NULL;

  /* check current user is DBA group */
  if (Au_dba_user != NULL && !au_is_dba_group_member (Au_user))
    {
      error = ER_AU_DBA_ONLY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, "drop_user");
      goto exit;
    }

  upper_case_name_size = intl_identifier_upper_string_size (user_name);
  upper_case_name = (char *) malloc (upper_case_name_size + 1);
  if (upper_case_name == NULL)
    {
      error = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, upper_case_name_size);
      goto exit;
    }
  intl_identifier_upper (user_name, upper_case_name);

  /* find the user object */
  user_class = db_find_class (AU_USER_CLASS_NAME);
  if (user_class == NULL)
    {
      error = er_errid ();
      assert (error != NO_ERROR);
      goto exit;
    }

  db_make_string (&user_name_string, upper_case_name);
  *user = obj_find_unique (user_class, "name", &user_name_string, AU_FETCH_WRITE);
  if ((*user) == NULL)
    {
      error = er_errid ();
      assert (error != NO_ERROR);
      if (error == ER_OBJ_OBJECT_NOT_FOUND)
	{
	  error = ER_AU_INVALID_USER;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, user_name);
	}
      goto exit;
    }

  /* check whether this user is an active user */
  error = log_does_active_user_exist (upper_case_name, &existed);
  if (error == NO_ERROR && existed)
    {
      error = ER_AU_NOT_ALLOW_TO_DROP_ACTIVE_USER;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, user_name);
    }

exit:

  if (upper_case_name)
    {
      free_and_init (upper_case_name);
    }

  if (error != NO_ERROR)
    {
      *user = NULL;
    }

  return error;
}

/*
 * au_make_user -  Create a new user object. Convert the name to upper case
 *                 so that au_find_user can use a query.
 *   return: new user object
 *   name(in): user name
 */
static MOP
au_make_user (const char *name)
{
  MOP uclass, aclass, user, auth;
  DB_VALUE value;
  DB_SET *set;
  char *lname;
  int error;

  user = NULL;
  uclass = sm_find_class (AU_USER_CLASS_NAME);
  if (uclass == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_AU_MISSING_CLASS, 1, AU_USER_CLASS_NAME);
    }
  else
    {
      aclass = sm_find_class (AU_AUTH_CLASS_NAME);
      if (aclass == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_AU_MISSING_CLASS, 1, AU_AUTH_CLASS_NAME);
	}
      else
	{
	  int name_size;

	  user = obj_create (uclass);
	  name_size = intl_identifier_upper_string_size (name);
	  lname = (char *) malloc (name_size + 1);
	  if (lname)
	    {
	      intl_identifier_upper (name, lname);
	      db_make_string (&value, lname);
	      error = obj_set (user, "name", &value);
	      free_and_init (lname);
	      if (error != NO_ERROR)
		{
		  if (!ER_IS_ABORTED_DUE_TO_DEADLOCK (error))
		    {
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_AU_ACCESS_ERROR, 2, AU_USER_CLASS_NAME, "name");
		      obj_delete (user);
		    }
		  user = NULL;
		}
	      else
		{
		  /* flattened group list */
		  set = set_create_basic ();
		  if (set == NULL)
		    {
		      goto memory_error;
		    }
		  db_make_set (&value, set);
		  obj_set (user, "groups", &value);
		  set_free (set);

		  /* direct group list */
		  set = set_create_basic ();
		  if (set == NULL)
		    {
		      goto memory_error;
		    }
		  db_make_set (&value, set);
		  obj_set (user, "direct_groups", &value);
		  set_free (set);

		  /* authorization object */
		  auth = obj_create (aclass);
		  if (auth == NULL)
		    {
		      goto memory_error;
		    }
		  db_make_object (&value, user);
		  /* back pointer to user object */
		  obj_set (auth, "owner", &value);
		  set = set_create_sequence (0);
		  if (set == NULL)
		    {
		      goto memory_error;
		    }
		  db_make_sequence (&value, set);
		  obj_set (auth, "grants", &value);
		  set_free (set);

		  db_make_object (&value, auth);
		  obj_set (user, "authorization", &value);

		  db_make_null (&value);
		  obj_set (user, "comment", &value);
		}
	    }
	  else
	    {
	      goto memory_error;
	    }
	}
    }
  return (user);

memory_error:
  if (user != NULL)
    {
      obj_delete (user);
    }
  return NULL;
}


/*
 * au_is_dba_group_member -  Determines if a given user is the DBA/a member
 *                           of the DBA group, or not
 *   return: true or false
 *   user(in): user object
 */
bool
au_is_dba_group_member (MOP user)
{
  DB_SET *groups;
  DB_VALUE value;
  bool is_member = false;
  LC_FETCH_VERSION_TYPE read_fetch_instance_version;

  if (!user)
    {
      return false;		/* avoid gratuitous er_set later */
    }

  if (ws_is_same_object (user, Au_dba_user))
    {
      return true;
    }

  /* Set fetch version type to read dirty version. */
  read_fetch_instance_version = TM_TRAN_READ_FETCH_VERSION ();
  db_set_read_fetch_instance_version (LC_FETCH_DIRTY_VERSION);

  if (au_get_set (user, "groups", &groups) == NO_ERROR)
    {
      db_make_object (&value, Au_dba_user);
      is_member = set_ismember (groups, &value);
      set_free (groups);
    }

  /* Restore fetch version type. */
  db_set_read_fetch_instance_version (read_fetch_instance_version);

  return is_member;
}

bool
au_is_user_group_member (MOP group_user, MOP user)
{
  DB_SET *groups;
  DB_VALUE group_user_val;
  int error = NO_ERROR;

  db_make_null (&group_user_val);

  if (!group_user || !user)
    {
      return false;
    }

  if (ws_is_same_object (group_user, user))
    {
      return true;
    }

  db_make_object (&group_user_val, group_user);

  if (au_get_set (user, "groups", &groups) == NO_ERROR)
    {
      if (set_ismember (groups, &group_user_val))
	{
	  set_free (groups);
	  return true;
	}
    }
  else
    {
      assert (er_errid () != NO_ERROR);
    }

  if (groups)
    {
      set_free (groups);
    }

  return false;
}

/*
 * TODO: return NO_ERROR in the previous implementation
 * check_user_name
 *   return: error code
 *   name(in): proposed user name
 *
 * Note: This is made void for ansi compatibility. It previously insured
 *       that identifiers which were accepted could be parsed in the
 *       language interface.
 *
 *       ANSI allows any character in an identifier. It also allows reserved
 *       words. In order to parse identifiers with non-alpha characters
 *       or that are reserved words, an escape syntax is definned with double
 *       quotes, "FROM", for example.
 */
#define check_user_name(name) NO_ERROR


/*
 * au_add_user -  Add a user object if one does not already exist.
 *   return: new or existing user object
 *   name(in): user name
 *   exists(out): flag set if user already existed
 *
 * Note: If one already exists, return it and set the flag.
 *       The db_root class used to have a user attribute which was a set
 *       containing the object-id for all users.  The users attribute has been
 *       eliminated for performance reasons.
 *
 */
MOP
au_add_user (const char *name, int *exists)
{
  MOP user;
  DB_VALUE value;
  int save;

  user = NULL;
  if (Au_dba_user != NULL && !au_is_dba_group_member (Au_user))
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_AU_DBA_ONLY, 1, "add_user");
    }
  else if (!check_user_name (name))
    {
      AU_DISABLE (save);
      user = NULL;
      if (exists != NULL)
	{
	  *exists = 0;
	}
      if (name == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_AU_INVALID_USER_NAME, 1, "");
	}
      else
	{
	  user = au_find_user (name);
	  if (user != NULL)
	    {
	      if (exists != NULL)
		{
		  *exists = 1;
		}
	    }
	  else
	    {
	      if (er_errid () != ER_AU_INVALID_USER)
		{
		  AU_ENABLE (save);
		  return NULL;
		}

	      /* clear error */
	      er_clear ();

	      user = au_make_user (name);
	      if (user != NULL)
		{
		  db_make_object (&value, user);
		  if (Au_public_user != NULL)
		    {
		      /*
		       * every user is a member of the PUBLIC group,
		       * must make sure that the exported routines can't
		       * be used to violate this internal connection
		       */
		      if (au_add_member_internal (Au_public_user, user, 1) != NO_ERROR)
			{
			  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_AU_CANT_ADD_MEMBER, 2, name, "PUBLIC");
			}
		    }

		  /*
		   * do we want to do this ?? - logically it is ok but this
		   * means we can't have DBA members since this would
		   * cause user hierarchy cycles.
		   */
#if 0
		  if (Au_dba_user != NULL)
		    {
		      if (au_get_set (Au_dba_user, "groups", &dba_groups) == NO_ERROR)
			{
			  db_make_object (&value, user);
			  if (!set_ismember (dba_groups, &value))
			    {
			      db_set_add (dba_groups, &value);
			    }
			  set_free (dba_groups);
			}
		    }
#endif /* 0 */
		}
	    }
	}
      AU_ENABLE (save);
    }
  return (user);
}


/*
 * au_set_user_comment() -  Set the comment string for a user.
 *   return: error code
 *   user(in): user object
 *   comment(in): a comment string
 */
int
au_set_user_comment (MOP user, const char *comment)
{
  int error = NO_ERROR;
  DB_VALUE value;
  int len = 0, save;

  AU_SAVE_AND_DISABLE (save);
  if (!ws_is_same_object (Au_user, user) && !au_is_dba_group_member (Au_user))
    {
      error = ER_AU_UPDATE_FAILURE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
    }
  else
    {
      if (comment != NULL)
	{
	  len = strlen (comment);
	}

      if (len == 0)
	{
	  comment = NULL;
	}

      if (len > AU_MAX_COMMENT_CHARS)
	{
	  error = ER_AU_COMMENT_OVERFLOW;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	}
      else
	{
	  db_make_string (&value, comment);
	  error = obj_set (user, "comment", &value);
	  pr_clear_value (&value);
	}
    }
  AU_RESTORE (save);

  return error;
}

/*
 * GROUP HIERARCHY MAINTENANCE
 */

/*
 * au_add_direct_groups - Add the group to the new_groups and then add
 *                        the group's groups.
 *   return: error status
 *   new_groups(in):the set to add to
 *   value(in): the group to add
 */
static int
au_add_direct_groups (DB_SET *new_groups, DB_VALUE *value)
{
  int error;
  MOP group;
  DB_SET *direct_groups;
  int gcard, g;
  DB_VALUE gvalue;

  if ((error = db_set_add (new_groups, value)) == NO_ERROR)
    {
      if (DB_IS_NULL (value))
	{
	  group = NULL;
	}
      else
	{
	  group = db_get_object (value);
	}
      if ((error = au_get_set (group, "direct_groups", &direct_groups)) == NO_ERROR)
	{
	  gcard = set_cardinality (direct_groups);
	  for (g = 0; g < gcard && !error; g++)
	    {
	      if ((error = set_get_element (direct_groups, g, &gvalue)) == NO_ERROR)
		{
		  error = au_add_direct_groups (new_groups, &gvalue);
		}
	    }
	  set_free (direct_groups);
	}
    }

  return error;
}

/*
 * au_compute_groups - Compute the groups attribute from the direct_groups
 *                     attribute for those users that have a particular
 *                     user/group in their groups attribute.
 *   return: error status
 *   member(in): the new member
 *   name(in): the new member name
 */
static int
au_compute_groups (MOP member, const char *name)
{
  int error = NO_ERROR;
  DB_SET *new_groups, *direct_groups;
  DB_VALUE value, gvalue, user_val;
  MOP user;
  int g, gcard;
  DB_SESSION *session;
  DB_VALUE val[3];
  STATEMENT_ID stmt_id;
  DB_QUERY_RESULT *result = (DB_QUERY_RESULT *) 0;
  const char *qstr = "select [d] from [db_user] [d] where ? in [d].[groups] or [d].[name] = ?;";

  db_make_object (&val[0], member);
  db_make_string (&val[1], name);

  session = db_open_buffer (qstr);
  if (!session)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto ret;
    }

  db_push_values (session, 2, val);

  stmt_id = db_compile_statement (session);
  if (stmt_id != 1)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto ret;
    }

  error = db_execute_statement_local (session, stmt_id, &result);
  if (error < 0)
    {
      goto ret;
    }

  /* error is row count if not negative. */
  if (error > 0)
    {
      error = NO_ERROR;
      while (db_query_next_tuple (result) == DB_CURSOR_SUCCESS)
	{
	  if ((error = db_query_get_tuple_value (result, 0, &user_val)) == NO_ERROR)
	    {
	      if (DB_IS_NULL (&user_val))
		{
		  user = NULL;
		}
	      else
		{
		  user = db_get_object (&user_val);
		}
	      new_groups = set_create_basic ();
	      if (new_groups)
		{
		  if ((error = au_get_set (user, "direct_groups", &direct_groups)) == NO_ERROR)
		    {
		      /* compute closure */
		      gcard = set_cardinality (direct_groups);
		      for (g = 0; g < gcard && !error; g++)
			{
			  if ((error = set_get_element (direct_groups, g, &gvalue)) == NO_ERROR)
			    {
			      error = au_add_direct_groups (new_groups, &gvalue);
			    }
			}
		      set_free (direct_groups);
		    }
		}
	      else
		{
		  assert (er_errid () != NO_ERROR);
		  error = er_errid ();
		}
	      if (error == NO_ERROR)
		{
		  db_make_set (&value, new_groups);
		  obj_set (user, "groups", &value);
		}
	      if (new_groups)
		{
		  set_free (new_groups);
		}
	    }
	}
    }

ret:
  if (result)
    {
      db_query_end (result);
    }
  if (session)
    {
      db_close_session (session);
    }

  return error;
}

/*
 * au_add_member_internal - Add a member to a group and propagate the member
 *                          to all affected	sub-groups.  If the call is
 *                          for a new user, then no other user can be part of
 *                          this user(group)
 *    return: error status
 *    group(in): group to get new member
 *    member(in): the new member
 *    new_user(in): whether the call is for a new user
 *
 * Note:
 *    the db_user class used to have a groups and a members attribute.  the
 *    members attribute was eliminated as a performance improvement, but the
 *    direct_groups attribute has been added.  both groups and direct_groups
 *    are sets.  the direct_groups attribute indicates which groups the user/
 *    group is an immediate member of.  the groups attribute indicates which
 *    groups the user/group is a member of (immediate or otherwise).  the
 *    group attribute is a flattened set.  when a user/group is added to a
 *    new group, the new group is added to both the direct_groups and groups
 *    attributes for the user/group.  then that change is propagated to other
 *    users/groups.
 *    for example,  if u1 is in g1 and g1 is added to g2, g2 is added to g1's
 *    direct_groups and groups attributes and g2 is also added to u1's groups
 *    attributes.
 */
static int
au_add_member_internal (MOP group, MOP member, int new_user)
{
  int error = NO_ERROR;
  DB_VALUE membervalue, member_name_val, groupvalue;
  DB_SET *group_groups = NULL, *member_groups = NULL, *member_direct_groups = NULL;
  int save;
  const char *member_name = NULL;

  AU_DISABLE (save);
  db_make_object (&membervalue, member);
  db_make_object (&groupvalue, group);

  /*
   * Skip some checks and processing for a new user/group because it
   * can't have any members yet.
   */
  if ((!new_user) && (group == member))
    {
      error = ER_AU_MEMBER_CAUSES_CYCLES;
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 0);
    }
  else
    {
      if ((!new_user) && ((error = au_get_set (group, "groups", &group_groups)) != NO_ERROR))
	{
	  ;
	}
      else
	{
	  if ((!new_user) && (set_ismember (group_groups, &membervalue)))
	    {
	      error = ER_AU_MEMBER_CAUSES_CYCLES;
	      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 0);
	    }
	  else
	    {
	      error = obj_inst_lock (member, 1);
	      if (error == NO_ERROR)
		{
		  error = au_get_set (member, "groups", &member_groups);
		}

	      if (error == NO_ERROR)
		{
		  error = au_get_set (member, "direct_groups", &member_direct_groups);
		  if (error == NO_ERROR)
		    {
		      if (new_user)
			{
			  error = db_set_add (member_groups, &groupvalue);
			  if (error == NO_ERROR)
			    {
			      error = db_set_add (member_direct_groups, &groupvalue);
			    }
			}
		      else if (!set_ismember (member_direct_groups, &membervalue))
			{
			  error = db_get (member, "name", &member_name_val);
			  if (error == NO_ERROR)
			    {
			      if (DB_IS_NULL (&member_name_val))
				{
				  member_name = NULL;
				}
			      else
				{
				  member_name = db_get_string (&member_name_val);
				}

			      error = db_set_add (member_direct_groups, &groupvalue);
			      if (error == NO_ERROR)
				{
				  error = au_compute_groups (member, member_name);
				}
			      db_value_clear (&member_name_val);
			    }
			}
		      set_free (member_direct_groups);
		    }
		  set_free (member_groups);
		}

	      if (!new_user)
		{
		  set_free (group_groups);
		}
	    }
	}
    }
  AU_ENABLE (save);
  return (error);
}

/*
 * au_add_member - Add a member to a group and propagate the member to
 *                 all affected sub-groups.
 *   return: error status
 *   group(in):  group to get new member
 *   member(in): the new member
 */
int
au_add_member (MOP group, MOP member)
{
  return au_add_member_internal (group, member, 0);
}

/*
 * au_add_member_method -  Method interface to au_add_member.
 *   return: none
 *   user(in): user object
 *   returnval(out): return value of this method
 *   memval(in):
 */
void
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
 * au_drop_member - Remove a member from a group.
 *   return: error code
 *   group(in): group with member to drop
 *   member(in): member to drop
 *
 * Note:
 *
 *    The db_user class used to have a groups and a members attribute.  The
 *    members attribute was eliminated as a performance improvement, but the
 *    direct_groups attribute has been added.  Both groups and direct_groups
 *    are sets.  The direct_groups attribute indicates which groups the user/
 *    group is an immediate member of.  The groups attribute indicates which
 *    groups the user/group is a member of (immediate or otherwise).  The
 *    groups attribute is a flattened set.  When a user/group is dropped from
 *    a group, the group is removed from both the direct_groups and groups
 *    attributes for the user/group.  Then that change is propagated to other
 *    users/groups.
 *    For example,  if U1 is directly in G1 and G1 is directly in G2 and G1
 *    is dropped from G2, G2 is removed from G1's direct_groups and groups
 *    attributes and G2 is also removed from U1's groups attribute.
 */
int
au_drop_member (MOP group, MOP member)
{
  int syserr = NO_ERROR, error = NO_ERROR;
  DB_VALUE groupvalue, member_name_val;
  DB_SET *groups = NULL, *member_groups = NULL, *member_direct_groups = NULL;
  int save;
  const char *member_name = NULL;

  AU_DISABLE (save);
  db_make_object (&groupvalue, group);

  if ((syserr = au_get_set (member, "groups", &member_groups)) == NO_ERROR)
    {
      if (!set_ismember (member_groups, &groupvalue))
	{
	  error = ER_AU_MEMBER_NOT_FOUND;
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 0);
	}
      else if ((error = au_get_set (group, "groups", &groups)) == NO_ERROR)
	{
	  if (set_ismember (groups, &groupvalue))
	    {
	      error = ER_AU_MEMBER_NOT_FOUND;
	      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 0);
	    }
	  else
	    {
	      error = obj_inst_lock (member, 1);
	      if (error == NO_ERROR)
		{
		  error = au_get_set (member, "direct_groups", &member_direct_groups);
		}

	      if (error == NO_ERROR)
		{
		  if ((error = db_get (member, "name", &member_name_val)) == NO_ERROR)
		    {
		      if (DB_IS_NULL (&member_name_val))
			{
			  member_name = NULL;
			}
		      else
			{
			  member_name = db_get_string (&member_name_val);
			}
		      if ((error = db_set_drop (member_direct_groups, &groupvalue)) == NO_ERROR)
			{
			  error = au_compute_groups (member, member_name);
			}
		      db_value_clear (&member_name_val);
		    }
		  set_free (member_direct_groups);
		}
	    }
	  set_free (groups);
	}
      set_free (member_groups);
    }
  AU_ENABLE (save);
  return (error);
}


/*
 * au_drop_user - Drop a user from the database.
 *   return: error code
 *   user(in): user object
 *
 * Note:
 *
 *    This should only be called with DBA privilidges.
 *    The db_user class used to have a groups and a members attribute.  The
 *    members attribute was eliminated as a performance improvement, but the
 *    direct_groups attribute has been added.  Both groups and direct_groups
 *    are sets.  The direct_groups attribute indicates which groups the user/
 *    group is an immediate member of.  The groups attribute indicates which
 *    groups the user/group is a member of (immediate or otherwise).  The
 *    groups attribute is a flattened set.  When a user/group is dropped,
 *    the user/group is removed from both the direct_groups and groups
 *    attributes for all users.  For example,  if U1 is directly in G1 and G1
 *    is directly in G2 and G1 is dropped, G1 & G2 are removed from U1's
 *    groups attribute and G1 is also removed from U1's direct_groups
 *    attribute.
 */
int
au_drop_user (MOP user)
{
  int save;
  DB_SESSION *session = NULL;
  DB_VALUE val[2], user_val, gvalue, value, password_val;
  STATEMENT_ID stmt_id;
  int error = NO_ERROR;
  DB_QUERY_RESULT *result;
  MOP auser, password;
  DB_SET *new_groups, *direct_groups;
  int g, gcard, i;
  DB_VALUE name;
  char query_buf[1024];

  AU_DISABLE (save);

  if (Au_dba_user != NULL && !au_is_dba_group_member (Au_user))
    {
      error = ER_AU_DBA_ONLY;
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 1, "drop_user");
      goto error;
    }

  /* check if user is dba/public or current user */
  if (ws_is_same_object (user, Au_dba_user) || ws_is_same_object (user, Au_public_user)
      || ws_is_same_object (user, Au_user))
    {
      db_make_null (&name);
      error = obj_get (user, "name", &name);
      if (error != NO_ERROR)
	{
	  goto error;
	}

      error = ER_AU_CANT_DROP_USER;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, db_get_string (&name));
      goto error;
    }

  /* check if user owns class/vclass/trigger/serial/synonym */
  for (i = 0; AU_OBJECT_CLASS_NAME[i] != NULL; i++)
    {
      sprintf (query_buf, "select count(*) from [%s] where [owner] = ?;", AU_OBJECT_CLASS_NAME[i]);
      session = db_open_buffer (query_buf);
      if (session == NULL)
	{
	  goto error;
	}

      db_make_object (&val[0], user);
      db_push_values (session, 1, &val[0]);
      stmt_id = db_compile_statement (session);
      if (stmt_id != 1)
	{
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	  db_close_session (session);
	  goto error;
	}

      error = db_execute_statement_local (session, stmt_id, &result);
      if (error < 0)
	{
	  db_close_session (session);
	  goto error;
	}

      error = db_query_first_tuple (result);
      if (error < 0)
	{
	  db_query_end (result);
	  db_close_session (session);
	  goto error;
	}

      db_make_bigint (&value, 0);
      error = db_query_get_tuple_value (result, 0, &value);
      if (error != NO_ERROR)
	{
	  db_query_end (result);
	  db_close_session (session);
	  goto error;
	}

      if (db_get_bigint (&value) > 0)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_AU_USER_HAS_DATABASE_OBJECTS, 0);
	  db_query_end (result);
	  db_close_session (session);
	  error = ER_AU_USER_HAS_DATABASE_OBJECTS;
	  goto error;
	}

      db_query_end (result);
      db_close_session (session);
      pr_clear_value (&val[0]);
    }


  /* propagate user deletion to groups */
  db_make_object (&val[1], user);

  session =
	  db_open_buffer ("update [db_user] [d] set "
			  "[d].[direct_groups] = [d].[direct_groups] - ? where ? in [d].[direct_groups];");
  if (session == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto error;
    }

  new_groups = set_create_basic ();
  if (new_groups)
    {
      error = db_set_add (new_groups, &val[1]);
      if (error == NO_ERROR)
	{
	  db_make_set (&val[0], new_groups);
	  db_push_values (session, 2, val);
	  stmt_id = db_compile_statement (session);
	  if (stmt_id == 1)
	    {
	      error = db_execute_statement_local (session, stmt_id, &result);
	      db_query_end (result);
	    }
	  else
	    {
	      assert (er_errid () != NO_ERROR);
	      error = er_errid ();
	    }
	}
      /*
       * We need to clear the host variable here to free the set.  set_free()
       * is not sufficient since the set referenced by new_groups may have
       * be replaced as a result of tp_value_cast().
       */
      pr_clear_value (&val[0]);
    }

  db_close_session (session);
  if (error < NO_ERROR)
    {
      goto error;
    }

  session = db_open_buffer ("select [d] from [db_user] [d] where ? in [d].[groups];");
  if (session == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto error;
    }

  db_push_values (session, 1, &val[1]);
  stmt_id = db_compile_statement (session);
  if (stmt_id == 1)
    {
      error = db_execute_statement_local (session, stmt_id, &result);
      if (error > 0)
	{
	  error = NO_ERROR;
	  while (error == NO_ERROR && db_query_next_tuple (result) == DB_CURSOR_SUCCESS)
	    {
	      error = db_query_get_tuple_value (result, 0, &user_val);
	      if (error == NO_ERROR)
		{
		  if (DB_IS_NULL (&user_val))
		    {
		      auser = NULL;
		    }
		  else
		    {
		      auser = db_get_object (&user_val);
		    }

		  new_groups = set_create_basic ();
		  if (new_groups)
		    {
		      error = au_get_set (auser, "direct_groups", &direct_groups);
		      if (error == NO_ERROR)
			{
			  /* compute closure */
			  gcard = set_cardinality (direct_groups);
			  for (g = 0; g < gcard && !error; g++)
			    {
			      error = set_get_element (direct_groups, g, &gvalue);
			      if (error == NO_ERROR)
				{
				  error = au_add_direct_groups (new_groups, &gvalue);
				}
			    }
			  set_free (direct_groups);
			}
		    }
		  else
		    {
		      assert (er_errid () != NO_ERROR);
		      error = er_errid ();
		    }

		  if (error == NO_ERROR)
		    {
		      db_make_set (&value, new_groups);
		      obj_set (auser, "groups", &value);
		    }

		  if (new_groups)
		    {
		      set_free (new_groups);
		    }
		}
	    }
	}
      db_query_end (result);
    }
  else
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
    }

  db_close_session (session);
  if (error < NO_ERROR)
    {
      goto error;
    }

  db_make_null (&password_val);
  error = obj_get (user, "password", &password_val);
  if (!DB_IS_NULL (&password_val))
    {
      password = db_get_object (&password_val);
      error = obj_delete (password);
      if (error == NO_ERROR)
	{
	  db_make_null (&password_val);
	  error = obj_set (user, "password", &password_val);
	}

      if (error != NO_ERROR)
	{
	  goto error;
	}
    }

  /*
   * could go through classes created by this user and change ownership
   * to the DBA ? - do this as the classes are referenced instead
   */

  error = obj_delete (user);
  if (error == NO_ERROR)
    {
      remove_user_cache_references (user);
    }

error:
  AU_ENABLE (save);
  return error;
}
