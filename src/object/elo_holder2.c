/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * elo_holder2.c
 *
 * Note :
 *              Holder of the elo. This object is tho only object in the
 *              system that can have an elo primitive type. This is because
 *              we want to make sure that any user of GLOs will get the one
 *              and only Holder that points to a particular file.
 *              This means that Holders cannot be inherited or copied, or
 *              even known by the user.
 * TODO: merge this file into elo_holder.c
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include "db.h"
#include "elo_holder.h"
#include "authenticate.h"
#include "db_query.h"
#include "glo_class.h"
#include "transaction_cl.h"
#include "network_interface_sky.h"

/* this must be the last header file included!!! */
#include "dbval.h"


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
