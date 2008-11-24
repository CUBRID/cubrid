/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

/*
 * elo_holder.c - Holder of the elo.
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>

#include "glo_class.h"
#include "elo_holder.h"
#include "db.h"
#include "elo_class.h"
#include "authenticate.h"
#include "db_query.h"
#include "glo_class.h"
#include "transaction_cl.h"
#include "network_interface_cl.h"

/* this must be the last header file included!!! */
#include "dbval.h"

static DB_OBJECT *Glo_create_name (const char *path_name,
				   DB_OBJECT * glo_holder);

/*
 * Glo_create_name() - create glo object
 *      return: return_value contains the newly created pathname object
 *  path_name(in) : Pathname for an fbo
 *  glo_holder(in) : the glo_holder object associated with this name.
 *
 * Note :
 *   returns a unique pathname object. If a filename is passed as an argument
 *   we check if a pathname object already exists with that filename. If so,
 *   that pathname object is returned. Otherwise, a new pathname object is
 *   created and returned. If the filename is NULL, we will return an LO.
 */
static DB_OBJECT *
Glo_create_name (const char *path_name, DB_OBJECT * glo_holder)
{
  int rc;
  DB_VALUE value;
  DB_OBJECT *GLO_name_class, *name_object = NULL;
  int save;

  db_make_string (&value, path_name);
  AU_DISABLE (save);
  GLO_name_class = db_find_class (GLO_NAME_CLASS_NAME);
  if (GLO_name_class == NULL)
    {
      goto end;
    }
  name_object = db_create_internal (GLO_name_class);

  if (name_object == NULL)
    {
      goto end;
    }

  rc = db_put_internal (name_object, GLO_NAME_PATHNAME, &value);

  if (rc != 0)
    {
      db_drop (name_object);
      name_object = NULL;
    }
  else
    {
      db_make_object (&value, glo_holder);
      rc = db_put_internal (name_object, GLO_NAME_HOLDER_PTR, &value);
      if (rc != 0)
	{
	  db_drop (name_object);
	  name_object = NULL;
	}
    }
end:
  AU_ENABLE (save);
  return (name_object);
}

/*
 * Glo_holder_create() - create glo object
 *      return: return_value contains the newly created pathname object
 *  this_p(in) : glo object
 *  return_argument_p(out) : arguments (none)
 *  path_name(in) : Pathname for an fbo, NULL for an LO
 *
 * Note :
 *   returns a unique pathname object. If a filename is passed as an argument
 *   we check if a pathname object already exists with that filename. If so,
 *   that pathname object is returned. Otherwise, a new pathname object is
 *   created and returned. If the filename is NULL, we will return an LO.
 */
void
Glo_create_holder (DB_OBJECT * this_p, DB_VALUE * return_argument_p,
		   const DB_VALUE * path_name)
{
  DB_ELO *glo_p;
  DB_OBJECT *holder_object_p, *Glo_name_object_p;
  char *pathname = NULL;
  DB_VALUE value;
  int save;

  if (path_name != NULL)
    {
      if (DB_VALUE_TYPE (path_name) != DB_TYPE_NULL)
	{
	  if ((path_name == NULL) || !IS_STRING (path_name))
	    {
	      esm_set_error (INVALID_STRING_INPUT_ARGUMENT);
	      return;
	    }

	}
    }

  if ((path_name == NULL) ||
      (DB_VALUE_TYPE (path_name) == DB_TYPE_NULL) ||
      ((pathname = (char *) DB_GET_STRING (path_name)) == NULL))
    {
      glo_p = elo_create (NULL);
    }
  else
    {
      holder_object_p = esm_find_holder_object (pathname);
      if (holder_object_p != NULL)
	{
	  db_make_object (return_argument_p, holder_object_p);
	  return;
	}
      else
	{
	  glo_p = elo_create (pathname);
	}
    }
  AU_DISABLE (save);
  holder_object_p = db_create_internal (this_p);

  if (holder_object_p == NULL)
    {
      goto error;
    }
  /* Make the name object and save the pathname in the GLO object */
  if (pathname)
    {
      Glo_name_object_p = Glo_create_name (pathname, holder_object_p);
      if (Glo_name_object_p != NULL)
	{
	  db_make_object (&value, Glo_name_object_p);
	}
      db_put_internal (holder_object_p, GLO_HOLDER_NAME_PTR, &value);
    }
  /* Save the glo primitive type */
  db_make_elo (&value, glo_p);
  db_put_internal (holder_object_p, GLO_HOLDER_GLO_NAME, &value);

  db_make_object (return_argument_p, holder_object_p);
  AU_ENABLE (save);
  return;

error:
  db_make_object (return_argument_p, NULL);
  AU_ENABLE (save);

}

/*
 * Glo_holder_lock()  - lock glo object
 *      return: none
 *  this_p(in) : glo object
 *  return_argument_p(out) : if lock success then return true
 *                            else return Error CODE
 *
 */
void
Glo_lock_holder (DB_OBJECT * this_p, DB_VALUE * return_argument_p)
{
  DB_VALUE value;
  int save;
  int error;

  db_make_int (return_argument_p, true);

  AU_DISABLE (save);
  db_make_int (&value, 1);
  error = db_lock_write (this_p);

  if (error != NO_ERROR)
    {
      /* return a DB_TYPE_ERROR to indicate a problem occurred */
      db_make_error (return_argument_p, error);
    }
  AU_ENABLE (save);
}




static int
esm_run_query_at_lowest_isolation (const char *buffer,
				   DB_VALUE * dbvals,
				   int nvals, DB_QUERY_RESULT ** result);

/*
 * esm_find_holder_object() - This will return an existing holder object
 *                            if there is one with the pathname requested
 *      return: the holder object
 *  pathname(in) : pathname
 *
 */

DB_OBJECT *
esm_find_holder_object (const char *pathname)
{
  char buffer[1100];
  DB_QUERY_RESULT *result = NULL;
  DB_OBJECT *name_object_p = NULL, *holder_p = NULL;
  DB_VALUE value;
  int save;
  int rc;

  sprintf (buffer, "select x from glo_name x where %s = '%s';",
	   GLO_NAME_PATHNAME, pathname);

  AU_DISABLE (save);
  rc = esm_run_query_at_lowest_isolation (buffer, NULL, 0, &result);
  if (rc == NO_ERROR)
    {
      if (result == NULL)
	goto error;

      if (db_query_tuple_count (result) == 0)
	goto error;

      if (db_query_first_tuple (result) != 0)
	goto error;

      if (db_query_get_tuple_value (result, 0, &value) != 0)
	goto error;

      if (DB_VALUE_TYPE (&value) == DB_TYPE_OBJECT)
	{
	  name_object_p = DB_GET_OBJECT (&value);

	  if (db_get (name_object_p, GLO_NAME_HOLDER_PTR, &value) == 0)
	    {
	      holder_p = DB_GET_OBJECT (&value);
	    }
	}
    }

  if (result)
    {
      db_query_end (result);
    }
  AU_ENABLE (save);
  return holder_p;

error:
  AU_ENABLE (save);
  return NULL;
}

/*
 * esm_find_glo_count() - This will return the count of glo instances
 *                        referencing a glo_holder
 *      return: Error Code
 *  holder_p(in) : the holder object
 *  object_count(out) : count of glo instances
 */

int
esm_find_glo_count (DB_OBJECT * holder_p, int *object_count)
{
  char buffer[1100];
  DB_QUERY_RESULT *result = NULL;
  DB_VALUE holder_value;
  int save, count;

  sprintf (buffer, "select count(x) from all glo x where x.%s = ?",
	   GLO_CLASS_HOLDER_NAME);

  AU_DISABLE (save);
  db_make_object (&holder_value, holder_p);
  count =
    esm_run_query_at_lowest_isolation (buffer, &holder_value, 1, &result);

  if (result != NULL)
    {
      db_query_end (result);
    }

  if (count >= 0)
    {
      *object_count = count;
      AU_ENABLE (save);
      return NO_ERROR;
    }

  /* An error occurred in the query or the cursor functions above */
  AU_ENABLE (save);
  return (db_error_code ());
}

/*
 * esm_run_query_at_lowest_isolation() -
 *      return:
 *  buffer(out) :
 *  dbvals(in/out) :
 *  nvals(in/out) :
 *  result(out) :
 * Note :
 *
 * Because of the nature of isolation levels, the queries needed to
 * implement this class should be run at the LOWEST iso level, and not
 * whatever the user is running.  This routine does that by temporarily
 * resetting the isolation level.
 *
 * This function uses the same return value protocol as db_execute,
 * namely, a negative return value is an error code (a int value),
 * and a non-negative return value is the number of affected objects.
 *
 */

static int
esm_run_query_at_lowest_isolation (const char *buffer,
				   DB_VALUE * dbvals,
				   int nvals, DB_QUERY_RESULT ** result)
{
  float lw;
  TRAN_ISOLATION old_isolation;
  bool dummy;
  DB_SESSION *session;
  int error;

  *result = NULL;
  session = NULL;
  error = NO_ERROR;

  /* temporarily lower the isolation level */
  tran_get_tran_settings (&lw, &old_isolation, &dummy /* async_ws */ );
  /* lower the isolation while holding the locks */
  error = log_reset_isolation (TRAN_READ_UNCOMMITTED, false);
  if (error < 0)
    return error;

  session = db_open_buffer (buffer);
  if (session)
    {
      int stmt;
      db_push_values (session, nvals, dbvals);
      stmt = db_compile_statement (session);

      if (stmt != 0)
	{
	  error = db_execute_statement (session, stmt, result);
	}
      else
	{
	  error = db_error_code ();
	}

      db_close_session (session);
    }
  else
    {
      error = db_error_code ();
    }

  /* Restore the correct isolation level */
  (void) log_reset_isolation (old_isolation, true);

  return error;
}
