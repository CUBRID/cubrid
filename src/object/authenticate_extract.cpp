/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

/*
 * authenticate_extract.cpp - authorization schema extraction
 */

#ident "$Id$"


#include "authenticate_extract.hpp"
#include "authenticate.h"
#include "db.h"
#include "dbtype.h"
#include "extract_schema.hpp"
#include "object_accessor.h"
#include "object_print.h"
#include "schema_manager.h"
#include "set_object.h"

static int class_grant_loop (extract_output &output_ctx, CLASS_AUTH *auth);
static void issue_grant_statement (extract_output &output_ctx, CLASS_AUTH *auth, CLASS_GRANT *grant, int authbits);
/*
 * au_export_users - Generates a sequence of add_user and add_member method
 *                   calls that when evaluated, will re-create the current
 *                   user/group hierarchy.
 *   return: error code
 *   outfp(in): output file
 */
int
au_export_users (extract_output &output_ctx)
{
  int error;
  DB_SET *direct_groups;
  DB_VALUE value, gvalue;
  MOP user, pwd;
  int g, gcard;
  char *uname, *str, *gname, *comment;
  char passbuf[AU_MAX_PASSWORD_BUF];
  char *query;
  size_t query_size;
  DB_QUERY_RESULT *query_result;
  DB_QUERY_ERROR query_error;
  DB_VALUE user_val;
  const char *qp1 = "select [%s] from [%s];";
  char encrypt_mode = 0x00;

  query_size = strlen (qp1) + strlen (AU_USER_CLASS_NAME) * 2;
  query = (char *) malloc (query_size);
  if (query == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, query_size);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  sprintf (query, qp1, AU_USER_CLASS_NAME, AU_USER_CLASS_NAME);

  error = db_compile_and_execute_local (query, &query_result, &query_error);
  /* error is row count if not negative. */
  if (error < 0)
    {
      free_and_init (query);
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
      encrypt_mode = 0x00;

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
		  output_ctx ("call [add_user]('%s', '') on class [db_root];\n", uname);
		}
	      else
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
	  else
	    {
	      if (strlen (passbuf))
		{
		  output_ctx ( "call [find_user]('%s') on class [db_user] to [auser];\n", uname);
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
	  ws_free_string (uname);
	}
      if (comment != NULL)
	{
	  ws_free_string (comment);
	}
    }

  /* group hierarchy */
  if (db_query_first_tuple (query_result) == DB_CURSOR_SUCCESS)
    {
      output_ctx ("call [find_user]('PUBLIC') on class [db_user] to [g_public];\n");
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
	      ws_free_string (uname);
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
		  gname = (char *) (db_get_string (&value));
		}

	      if (gname != NULL)
		{
		  output_ctx ("call [find_user]('%s') on class [db_user] to [g_%s];\n", gname, gname);
		  output_ctx ("call [add_member]('%s') on [g_%s];\n", uname, gname);
		  ws_free_string (gname);
		}
	    }

	  set_free (direct_groups);
	  ws_free_string (uname);
	}
      while (db_query_next_tuple (query_result) == DB_CURSOR_SUCCESS);
    }

  db_query_end (query_result);
  free_and_init (query);

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
au_export_grants (extract_output &output_ctx, MOP class_mop)
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
      while ((statements = class_grant_loop (output_ctx, &cl_auth)));

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
	      output_ctx (msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_AUTHORIZATION, MSGCAT_AUTH_GRANT_DUMP_ERROR),
			  uname);
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
class_grant_loop (extract_output &output_ctx, CLASS_AUTH *auth)
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
		      issue_grant_statement (output_ctx, auth, grant, authbits);
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
issue_grant_statement (extract_output &output_ctx, CLASS_AUTH *auth, CLASS_GRANT *grant, int authbits)
{
  const char *gtype, *classname;
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
  classname = sm_get_ch_name (auth->class_mop);
  username = au_get_user_name (grant->user->obj);

  output_ctx ("GRANT %s ON ", gtype);
  output_ctx ("[%s]", classname);

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
