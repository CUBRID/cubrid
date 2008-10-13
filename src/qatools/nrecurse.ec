/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * nrecurse.ec
 */

/******************      SYSTEM INCLUDE FILES      ***************************/

/******************     PRIVATE INCLUDE FILES      ***************************/

#include <dbi.h>
#include <string.h>
#include <stdlib.h>
#include "cubrid_esql.h"

/******************     IMPORTED DECLARATIONS      ***************************/

/****************** PUBLIC (EXPORTED) DECLARATIONS ***************************/

/******************        PRIVATE DEFINES         ***************************/

/******************        PRIVATE TYPEDEFS        ***************************/

/******************      PRIVATE DECLARATIONS      ***************************/

/******************           FUNCTIONS            ***************************/

static void
warn_on_condition (int condition, const char *text)
{
  if (condition == 'W')
    {
      fprintf (stdout, "*** %s, line %ld: warning: %s\n", SQLFILE,
	       SQLLINE, text);
      fflush (stdout);
    }
}

static void
sql_warn (void)
{
  warn_on_condition (SQLWARN1, "output truncated");
  warn_on_condition (SQLWARN2, "NULL in aggregate function evaluation");
  warn_on_condition (SQLWARN3, "fewer host variables than results");
  warn_on_condition (SQLWARN4, "update/delete without search condition");
}

static void
sql_error (void)
{
  fprintf (stdout, "Problem while executing embedded SQL/X statement\n");
  fprintf (stdout, "%s, line %ld: %s (sqlcode = %ld)\n", SQLFILE, SQLLINE,
	   SQLERRMC, SQLCODE);
  fflush (stdout);
  exit (1);
}

void
new_recurse (DB_OBJECT * self, DB_VALUE * result, DB_VALUE * arg0,
	     DB_VALUE * arg1)
{
  char new_stmt_buf[500];

  exec sqlx begin declare section;
  int n;
  char *new_stmt = new_stmt_buf;
  exec sqlx end declare section;

  EXEC SQLX WHENEVER SQLWARNING CALL sql_warn;
  EXEC SQLX WHENEVER SQLERROR CALL sql_error;
  EXEC SQLX WHENEVER NOT FOUND CALL sql_error;

  if (!DB_IS_NULL (arg1))
    {

      n = DB_GET_INT (arg0);
      sprintf (new_stmt, "SELECT sum(new_recurse(%s, %d, \'%s\')) FROM %s;",
	       db_get_string (arg1), (n - 1), db_get_string (arg1),
	       db_get_string (arg1));

      if (n > 1)
	{
	  EXEC SQLX PREPARE stmt FROM:new_stmt;
	  EXEC SQLX EXECUTE stmt INTO:n;
	}
      DB_MAKE_INT (result, n);
    }
  return;
}
