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
 * csql_result.c : Query execution / result handling routine
 */

#ident "$Id$"

#include "config.h"

#include <float.h>
#include <signal.h>

#include "csql.h"
#include "memory_alloc.h"
#include "porting.h"

/* this must be the last header file included!!! */
#include "dbval.h"

/* max columns to display each data type
 * NOTE: some of these are totally dependent on report-writer's
 * rendering library.
 */
#define	MAX_SHORT_DISPLAY_LENGTH	6
#define	MAX_INTEGER_DISPLAY_LENGTH	11
#define	MAX_FLOAT_DISPLAY_LENGTH	(FLT_DIG + 7)
#define	MAX_DOUBLE_DISPLAY_LENGTH	(DBL_DIG + 9)
#define	MAX_TIME_DISPLAY_LENGTH		11
#define	MAX_UTIME_DISPLAY_LENGTH	25
#define	MAX_DATE_DISPLAY_LENGTH		10
#define	MAX_MONETARY_DISPLAY_LENGTH	20
#define	MAX_DEFAULT_DISPLAY_LENGTH	20

/* structure for current query result information */
typedef struct
{
  DB_QUERY_RESULT *query_result;
  int num_attrs;
  char **attr_names;
  int *attr_lengths;
  int max_attr_name_length;
  CUBRID_STMT_TYPE curr_stmt_type;
  int curr_stmt_line_no;
} CUR_RESULT_INFO;

typedef struct
{
  CUBRID_STMT_TYPE stmt_type;
  const char *cmd_string;
} CSQL_CMD_STRING_TABLE;

static CSQL_CMD_STRING_TABLE csql_Cmd_string_table[] = {
  {CUBRID_STMT_SELECT, "SELECT"},
  {CUBRID_STMT_SELECT_UPDATE, "SELECT"},
  {CUBRID_STMT_CALL, "CALL"},
  {CUBRID_STMT_EVALUATE, "EVALUATE"},
  {CUBRID_STMT_GET_ISO_LVL, "GET ISOLATION LEVEL"},
  {CUBRID_STMT_GET_TIMEOUT, "GET LOCK TIMEOUT"},
  {CUBRID_STMT_GET_OPT_LVL, "GET OPTIMIZATION"},
  {CUBRID_STMT_GET_TRIGGER, "GET TRIGGER"},
  {CUBRID_STMT_UPDATE, "UPDATE"},
  {CUBRID_STMT_DELETE, "DELETE"},
  {CUBRID_STMT_INSERT, "INSERT"},
  {CUBRID_STMT_ALTER_CLASS, "ALTER"},
  {CUBRID_STMT_COMMIT_WORK, "COMMIT"},
  {CUBRID_STMT_CREATE_CLASS, "CREATE"},
  {CUBRID_STMT_CREATE_INDEX, "CREATE INDEX"},
  {CUBRID_STMT_DROP_DATABASE, "DROP LDB"},
  {CUBRID_STMT_DROP_CLASS, "DROP"},
  {CUBRID_STMT_DROP_INDEX, "DROP INDEX"},
  {CUBRID_STMT_ALTER_INDEX, "ALTER INDEX"},
  {CUBRID_STMT_DROP_LABEL, "DROP "},
  {CUBRID_STMT_RENAME_CLASS, "RENAME"},
  {CUBRID_STMT_ROLLBACK_WORK, "ROLLBACK"},
  {CUBRID_STMT_GRANT, "GRANT"},
  {CUBRID_STMT_REVOKE, "REVOKE"},
  {CUBRID_STMT_CREATE_USER, "CREATE USER"},
  {CUBRID_STMT_DROP_USER, "DROP USER"},
  {CUBRID_STMT_ALTER_USER, "ALTER USER"},
  {CUBRID_STMT_UPDATE_STATS, "UPDATE STATISTICS"},
  {CUBRID_STMT_GET_STATS, "GET STATISTICS"},
  {CUBRID_STMT_SCOPE, "SCOPE"},
  {CUBRID_STMT_REGISTER_DATABASE, "REGISTER"},
  {CUBRID_STMT_CREATE_TRIGGER, "CREATE TRIGGER"},
  {CUBRID_STMT_DROP_TRIGGER, "DROP TRIGGER"},
  {CUBRID_STMT_SET_OPT_LVL, "SET OPTIMIZATION"},
  {CUBRID_STMT_SET_SYS_PARAMS, "SET SYSTEM PARAMETERS"},
  {CUBRID_STMT_SET_TRIGGER, "SET TRIGGER"},
  {CUBRID_STMT_SAVEPOINT, "SAVEPOINT"},
  {CUBRID_STMT_PREPARE, "PREPARE"},
  {CUBRID_STMT_ATTACH, "ATTACH"},
  {CUBRID_STMT_USE, "USE/EXCLUDE"},
  {CUBRID_STMT_REMOVE_TRIGGER, "REMOVE TRIGGER"},
  {CUBRID_STMT_RENAME_TRIGGER, "RENAME TRIGGER"},
  {CUBRID_STMT_ON_LDB, "ON LDB"},
  {CUBRID_STMT_GET_LDB, "GET LDB"},
  {CUBRID_STMT_SET_LDB, "SET LDB"},
  {CUBRID_STMT_ALTER_SERIAL, "ALTER SERIAL"},
  {CUBRID_STMT_CREATE_SERIAL, "CREATE SERIAL"},
  {CUBRID_STMT_DROP_SERIAL, "DROP SERIAL"},
  {CUBRID_STMT_CREATE_STORED_PROCEDURE, "CREATE PROCEDURE"},
  {CUBRID_STMT_DROP_STORED_PROCEDURE, "DROP PROCEDURE"}
};

static const char *csql_Isolation_level_string[] = {
  "UNKNOWN",
  "READ COMMITTED SCHEMA, READ UNCOMMITTED INSTANCES",
  "READ COMMITTED SCHEMA, READ COMMITTED INSTANCES",
  "REPEATABLE READ SCHEMA, READ UNCOMMITTED INSTANCES",
  "REPEATABLE READ SCHEMA, READ COMMITTED INSTANCES",
  "REPEATABLE READ SCHEMA, REPEATABLE READ INSTANCES",
  "SERIALIZABLE"
};

static jmp_buf csql_Jmp_buf;

static const char *csql_cmd_string (CUBRID_STMT_TYPE stmt_type,
				    const char *default_string);
static void display_empty_result (int stmt_type, int line_no);
static char **get_current_result (int **len,
				  const CUR_RESULT_INFO * result_info);
static int write_results_to_stream (const CSQL_ARGUMENT * csql_arg, FILE * fp,
				    const CUR_RESULT_INFO * result_info);
static char *uncontrol_strdup (const char *from);
static char *uncontrol_strndup (const char *from, int length);

/*
 * csql_results() - display the result
 *   return: none
 *   csql_arg(in): csql argument
 *   result(in): query result structure.
 *   attr_spec(in): result attribute spec structure
 *   line_no(in): line number on which the statement appears
 *   stmt_type(in): query statement type
 *
 * Note: If `result' is NULL, no results is assumed.
 */
void
csql_results (const CSQL_ARGUMENT * csql_arg, DB_QUERY_RESULT * result,
	      DB_QUERY_TYPE * attr_spec, int line_no,
	      CUBRID_STMT_TYPE stmt_type)
{
  int i;
  DB_QUERY_TYPE *t;		/* temp ptr for attr_spec */
  int err;
  int *attr_name_lengths = NULL;	/* attribute name length array */
  CUR_RESULT_INFO result_info;
  int num_attrs = 0;
  char **attr_names = NULL;
  int *attr_lengths = NULL;
  int max_attr_name_length = 0;

  /* trivial case - no results */
  if (result == NULL
      || (err = db_query_first_tuple (result)) == DB_CURSOR_END)
    {
      display_empty_result (stmt_type, line_no);
      return;
    }

  if (err < 0)
    {
      csql_Error_code = CSQL_ERR_SQL_ERROR;
      goto error;
    }

  for (t = attr_spec; t != NULL; t = db_query_format_next (t), num_attrs++)
    {
      ;
    }

  /* allocate pointer array for attr names and int array for attr lengths */
  attr_names = (char **) malloc (sizeof (char *) * num_attrs);
  if (attr_names == NULL)
    {
      csql_Error_code = CSQL_ERR_NO_MORE_MEMORY;
      goto error;
    }
  for (i = 0; i < num_attrs; i++)
    {
      attr_names[i] = (char *) NULL;
    }
  attr_name_lengths = (int *) malloc (sizeof (int) * num_attrs);
  attr_lengths = (int *) malloc (sizeof (int) * num_attrs);
  if (attr_name_lengths == NULL || attr_lengths == NULL)
    {
      csql_Error_code = CSQL_ERR_NO_MORE_MEMORY;
      goto error;
    }

  /* get the result attribute names */

  max_attr_name_length = 0;

  for (i = 0, t = attr_spec; t != NULL; t = db_query_format_next (t), i++)
    {
      const char *temp;

      if ((temp = db_query_format_name (t)) == NULL)
	{
	  attr_names[i] = (char *) malloc (7);
	  if (attr_names[i] == NULL)
	    {
	      csql_Error_code = CSQL_ERR_NO_MORE_MEMORY;
	      goto error;
	    }
	  strcpy (attr_names[0], "Result");
	}
      else
	{
	  attr_names[i] = uncontrol_strdup (temp);
	  if (attr_names[i] == NULL)
	    {
	      goto error;
	    }
	}
      attr_name_lengths[i] = strlen (attr_names[i]);
      max_attr_name_length = MAX (max_attr_name_length, attr_name_lengths[i]);

      switch (db_query_format_type (t))
	{
	case DB_TYPE_SHORT:
	  attr_lengths[i] =
	    MAX (MAX_SHORT_DISPLAY_LENGTH, attr_name_lengths[i]);
	  break;
	case DB_TYPE_INTEGER:
	  attr_lengths[i] =
	    MAX (MAX_INTEGER_DISPLAY_LENGTH, attr_name_lengths[i]);
	  break;
	case DB_TYPE_FLOAT:
	  attr_lengths[i] =
	    MAX (MAX_FLOAT_DISPLAY_LENGTH, attr_name_lengths[i]);
	  break;
	case DB_TYPE_DOUBLE:
	  attr_lengths[i] =
	    MAX (MAX_DOUBLE_DISPLAY_LENGTH, attr_name_lengths[i]);
	  break;
	case DB_TYPE_TIME:
	  attr_lengths[i] =
	    -MAX (MAX_TIME_DISPLAY_LENGTH, attr_name_lengths[i]);
	  break;
	case DB_TYPE_UTIME:
	  attr_lengths[i] =
	    -MAX (MAX_UTIME_DISPLAY_LENGTH, attr_name_lengths[i]);
	  break;
	case DB_TYPE_DATE:
	  attr_lengths[i] =
	    -MAX (MAX_DATE_DISPLAY_LENGTH, attr_name_lengths[i]);
	  break;
	case DB_TYPE_MONETARY:
	  attr_lengths[i] = MAX (MAX_MONETARY_DISPLAY_LENGTH,
				 attr_name_lengths[i]);
	  break;
	default:
	  attr_lengths[i] = -MAX_DEFAULT_DISPLAY_LENGTH;
	  break;
	}
    }

  result_info.query_result = result;
  result_info.num_attrs = num_attrs;
  result_info.attr_names = attr_names;
  result_info.attr_lengths = attr_lengths;
  result_info.max_attr_name_length = max_attr_name_length;
  result_info.curr_stmt_type = stmt_type;
  result_info.curr_stmt_line_no = line_no;

  if (write_results_to_stream (csql_arg, csql_Output_fp, &result_info) ==
      CSQL_FAILURE)
    {
      if (csql_Error_code == CSQL_ERR_SQL_ERROR)
	csql_display_csql_err (0, 0);
      else
	nonscr_display_error (csql_Scratch_text, SCRATCH_TEXT_LEN);
    }
  /* free memories */
  if (attr_names != NULL)
    {
      for (i = 0; i < num_attrs; i++)
	{
	  if (attr_names[i] != NULL)
	    {
	      free_and_init (attr_names[i]);
	    }
	}
      free_and_init (attr_names);
    }
  if (attr_name_lengths != NULL)
    {
      free_and_init (attr_name_lengths);
    }
  if (attr_lengths != NULL)
    {
      free_and_init (attr_lengths);
    }

  return;

error:

  if (csql_Error_code == CSQL_ERR_SQL_ERROR)
    {
      csql_display_csql_err (line_no, 0);
      csql_check_server_down ();
      /* for correct csql return code */
      csql_Num_failures++;
    }

  /* free memories */
  if (attr_names != NULL)
    {
      for (i = 0; i < num_attrs; i++)
	{
	  if (attr_names[i] != NULL)
	    {
	      free_and_init (attr_names[i]);
	    }
	}
      free_and_init (attr_names);
    }
  if (attr_name_lengths != NULL)
    {
      free_and_init (attr_name_lengths);
    }
  if (attr_lengths != NULL)
    {
      free_and_init (attr_lengths);
    }
}

/*
 * csql_cmd_string() - return the command string associated with a statement enum
 *   return:  const char*
 *   stmt_type(in): statement enum
 *   default_string(in): default command string if stmt_type is invallid
 */
static const char *
csql_cmd_string (CUBRID_STMT_TYPE stmt_type, const char *default_string)
{
  int i;
  int table_size = DIM (csql_Cmd_string_table);

  for (i = 0; i < table_size; i++)
    {
      if (csql_Cmd_string_table[i].stmt_type == stmt_type)
	{
	  return (csql_Cmd_string_table[i].cmd_string);
	}
    }
  return default_string;
}

/*
 * display_empty_result() - display the empty result message
 *   return: none
 *   stmt_type(in): current statement type
 *   line_no(in): current statement line number
 */
static void
display_empty_result (int stmt_type, int line_no)
{
  FILE *pf;			/* pipe stream to pager */

  sprintf (csql_Scratch_text, msgcat_message (MSGCAT_CATALOG_CSQL,
					      MSGCAT_CSQL_SET_CSQL,
					      CSQL_RESULT_STMT_TITLE_FORMAT),
	   csql_cmd_string ((CUBRID_STMT_TYPE) stmt_type, ""), line_no);


  pf = csql_popen (csql_Pager_cmd, csql_Output_fp);

  csql_fputs ("\n=== ", pf);
  csql_fputs (csql_Scratch_text, pf);
  csql_fputs (" ===\n\n", pf);
  csql_fputs (msgcat_message (MSGCAT_CATALOG_CSQL,
			      MSGCAT_CSQL_SET_CSQL,
			      CSQL_STAT_NONSCR_EMPTY_RESULT_TEXT), pf);
  csql_fputs ("\n", pf);

  csql_pclose (pf, csql_Output_fp);

  return;
}

/*
 * get_current_result() - get the attribute values of the current result
 *   return: pointer newly allocated value array. On error, NULL.
 *   lengths(out): lengths of returned values
 *   result_info(in): pointer to current query result info structure
 *
 * Note:
 *   Caller should be responsible for free the return array and its elements.
 */
static char **
get_current_result (int **lengths, const CUR_RESULT_INFO * result_info)
{
  int i;
  char **val;			/* temporary array for values */
  int *len;			/* temporary array for lengths */
  DB_VALUE db_value;
  CUBRID_STMT_TYPE stmt_type = result_info->curr_stmt_type;
  DB_QUERY_RESULT *result = result_info->query_result;
  int num_attrs = result_info->num_attrs;

  val = (char **) malloc (sizeof (char *) * num_attrs);
  len = (int *) malloc (sizeof (int) * num_attrs);
  if (val == NULL || len == NULL)
    {
      csql_Error_code = CSQL_ERR_NO_MORE_MEMORY;
      goto error;
    }
  for (i = 0; i < num_attrs; i++)
    {
      val[i] = (char *) NULL;
      len[i] = 0;
    }

  /* get attribute values */
  for (i = 0; i < num_attrs; i++)
    {
      DB_TYPE value_type;

      if (db_query_get_tuple_value (result, i, &db_value) < 0)
	{
	  csql_Error_code = CSQL_ERR_SQL_ERROR;
	  goto error;
	}

      value_type = DB_VALUE_TYPE (&db_value);	/* copy to register to speed up */

      switch (value_type)
	{
	case DB_TYPE_NULL:	/* null value */
	  val[i] = (char *) malloc (5);
	  if (val[i] == NULL)
	    {
	      csql_Error_code = CSQL_ERR_NO_MORE_MEMORY;
	      goto error;
	    }
	  strcpy (val[i], "NULL");
	  break;

	case DB_TYPE_POINTER:	/* pointer type */
	  val[i] = (char *) malloc (40);
	  if (val[i] == NULL)
	    {
	      csql_Error_code = CSQL_ERR_NO_MORE_MEMORY;
	      goto error;
	    }
	  sprintf (val[i], "pointer value (%p)",
		   (void *) DB_GET_POINTER (&db_value));
	  break;

	case DB_TYPE_ERROR:	/* error type */
	  val[i] = (char *) malloc (40);
	  if (val[i] == NULL)
	    {
	      csql_Error_code = CSQL_ERR_NO_MORE_MEMORY;
	      goto error;
	    }
	  sprintf (val[i], "error code (%d)", DB_GET_ERROR (&db_value));
	  break;

	default:		/* other types */
	  /*
	   * If we are printing the isolation level, we need to
	   * interpret it for the user, not just return a meaningless number.
	   *
	   * Also interpret a lock timeout value of -1
	   */
	  if (stmt_type == CUBRID_STMT_GET_ISO_LVL)
	    {
	      int async_ws, iso_lvl;

	      async_ws = DB_GET_INTEGER (&db_value) & TRAN_ASYNC_WS_BIT;
	      iso_lvl = DB_GET_INTEGER (&db_value) & TRAN_ISO_LVL_BITS;

	      val[i] = (char *) malloc (128);
	      if (val[i] == NULL)
		{
		  csql_Error_code = CSQL_ERR_NO_MORE_MEMORY;
		  goto error;
		}

	      if (iso_lvl < 1 || iso_lvl > 6)
		{
		  iso_lvl = 0;
		  async_ws = false;
		}

	      sprintf (val[i], "%s%s",
		       csql_Isolation_level_string[iso_lvl],
		       (async_ws ? ", ASYNC WORKSPACE" : ""));
	    }
	  else if ((stmt_type == CUBRID_STMT_GET_TIMEOUT)
		   && (DB_GET_FLOAT (&db_value) == -1.0))
	    {
	      val[i] = (char *) malloc (9);
	      if (val[i] == NULL)
		{
		  csql_Error_code = CSQL_ERR_NO_MORE_MEMORY;
		  goto error;
		}
	      strcpy (val[i], "INFINITE");
	    }
	  else
	    {
	      char *temp;

	      temp = csql_db_value_as_string (&db_value, &len[i]);
	      if (temp == NULL)
		{
		  csql_Error_code = CSQL_ERR_NO_MORE_MEMORY;
		  goto error;
		}
	      temp[len[i]] = '\0';
	      val[i] = temp;
	    }
	}

      if (len[i] == 0 && val[i])
	{
	  len[i] = strlen (val[i]);
	}

      /* free the db_value storage */
      db_value_clear (&db_value);
    }

  if (lengths)
    {
      *lengths = len;
    }
  return (val);

error:
  if (val != NULL)
    {
      for (i = 0; i < num_attrs; i++)
	{
	  if (val[i] != NULL)
	    {
	      free_and_init (val[i]);
	    }
	}
      free_and_init (val);
    }
  if (len != NULL)
    free_and_init (len);
  return ((char **) NULL);
}

/*
 * csql_pipe_handler() - Generic longjmp'ing signal handler used
 *                     where we need to catch broken pipe.
 *   return: none
 *   sig(in): signal number
 */
static void
csql_pipe_handler (int sig_no)
{
  longjmp (csql_Jmp_buf, 1);
}

static void (*csql_pipe_save) (int sig);
/*
 * write_results_to_stream()
 *   return: CSQL_FAILURE/CSQL_SUCCESS
 *   csql_arg(in): csql argument
 *   fp(in): file stream pointer
 *   result_info(in): pointer to current query result info structure
 *
 * Note: This function may set csql_Error_code CSQL_ERR_SQL_ERROR to indicate
 *       the error
 */
static int
write_results_to_stream (const CSQL_ARGUMENT * csql_arg, FILE * fp,
			 const CUR_RESULT_INFO * result_info)
{
  /*
   * These are volatile to avoid dangerous interaction with the longjmp
   * handler for SIGPIPE problems.  The typedef is necessary so that we
   * can tell the compiler that the top POINTER is volatile, not the
   * characters that it eventually points to.
   */
  typedef char **value_array;
  volatile value_array val;	/* attribute values array */
  int *len;			/* attribute values lengths */
  volatile int error;		/* to switch return of CSQL_FAILURE/CSQL_SUCCESS */
  int i;			/* loop counter */
  int object_no;		/* result object count */
  int e;			/* error code from DBI */
  FILE *pf;			/* pipe stream to pager */
  int n;			/* # of cols for a line */
  CUBRID_STMT_TYPE stmt_type = result_info->curr_stmt_type;
  DB_QUERY_RESULT *result = result_info->query_result;
  int line_no = result_info->curr_stmt_line_no;
  int num_attrs = result_info->num_attrs;
  int *attr_lengths = result_info->attr_lengths;
  char **attr_names = result_info->attr_names;
  int max_attr_name_length = result_info->max_attr_name_length;

  val = (char **) NULL;
  len = NULL;
  error = FALSE;

  /*
   * Do this *before* the setjmp to avoid the possibility of the value
   * being clobbered by a longjmp.  Even if some internal thing longjmps
   * to the end of the next block we still need to be able to close the
   * pipe, so we can't risk having pf set back to some unknown value.
   */
  pf = csql_popen (csql_Pager_cmd, fp);

  if (setjmp (csql_Jmp_buf) == 0)
    {
#if !defined(WINDOWS)
      csql_pipe_save = os_set_signal_handler (SIGPIPE, &csql_pipe_handler);
#endif /* !WINDOWS */

      csql_fputs ("\n=== ", pf);
      sprintf (csql_Scratch_text,
	       csql_get_message (CSQL_RESULT_STMT_TITLE_FORMAT),
	       csql_cmd_string (stmt_type, "UNKNOWN"), line_no);
      csql_fputs (csql_Scratch_text, pf);
      csql_fputs (" ===\n\n", pf);

      if (db_query_first_tuple (result) < 0)
	{
	  csql_Error_code = CSQL_ERR_SQL_ERROR;
	  error = TRUE;
	  goto done;
	}

      if (!csql_arg->line_output)
	{
	  for (n = i = 0; i < num_attrs; i++)
	    {
	      fprintf (pf, "  %*s", (int) (attr_lengths[i]), attr_names[i]);
	      n += 2 + ((attr_lengths[i] > 0) ? attr_lengths[i] :
			-attr_lengths[i]);
	    }
	  putc ('\n', pf);
	  for (; n > 0; n--)
	    {
	      putc ('=', pf);
	    }
	  putc ('\n', pf);
	}

      for (object_no = 1;; object_no++)
	{
	  csql_Row_count = object_no;
	  /* free previous result */
	  if (val != NULL)
	    {
	      for (i = 0; i < num_attrs; i++)
		{
		  free_and_init (val[i]);
		}
	      free_and_init (val);
	    }
	  if (len)
	    {
	      free_and_init (len);
	    }

	  val = get_current_result (&len, result_info);
	  if (val == NULL)
	    {
	      csql_Error_code = CSQL_ERR_SQL_ERROR;
	      error = TRUE;
	      goto done;
	    }

	  if (!csql_arg->line_output)
	    {
	      int l;
	      for (i = 0; i < num_attrs; i++)
		{
		  l = (attr_lengths[i] > 0) ?
		    MAX (attr_lengths[i] - len[i], 0) :
		    MIN (attr_lengths[i] + len[i], 0);
		  fprintf (pf, "  ");
		  if (l > 0)
		    {
		      /* right justified */
		      fprintf (pf, "%*s", (int) l, "");
		    }
		  fwrite (val[i], 1, len[i], pf);
		  if (l < 0)
		    {
		      /* left justified */
		      fprintf (pf, "%*s", (int) (-l), "");
		    }
		}
	      putc ('\n', pf);
	      /* fflush(pf); */
	    }
	  else
	    {
	      fprintf (pf, "<%05d>", object_no);
	      for (i = 0; i < num_attrs; i++)
		{
		  fprintf (pf, "%*c", (int) ((i == 0) ? 1 : 8), ' ');
		  fprintf (pf, "%*s: %s\n", (int) (-max_attr_name_length),
			   attr_names[i], val[i]);
		}
	      /* fflush(pf); */
	    }

	  /* advance to next */
	  e = db_query_next_tuple (result);
	  if (e < 0)
	    {
	      csql_Error_code = CSQL_ERR_SQL_ERROR;
	      error = TRUE;
	      goto done;
	    }
	  else if (e == DB_CURSOR_END)
	    {
	      break;
	    }
	}
      putc ('\n', pf);
    }

done:

  if (pf)
    {
      /*
       * Don't care for a sig pipe error when closing pipe.
       *
       * NOTE if I restore to previous signal handler which could be the
       *      system default, the program could exit.
       *      I cannot use the old error handler since I could not longjmp
       */
#if !defined(WINDOWS)
      (void) os_set_signal_handler (SIGPIPE, SIG_IGN);
#endif /* !WINDOWS */
      csql_pclose (pf, fp);
    }

#if !defined(WINDOWS)
  (void) os_set_signal_handler (SIGPIPE, csql_pipe_save);
#endif /* !WINDOWS */

  /* free result */
  if (val != NULL)
    {
      for (i = 0; i < num_attrs; i++)
	{
	  free_and_init (val[i]);
	}
      free_and_init (val);
    }
  if (len)
    {
      free_and_init (len);
    }

  return ((error) ? CSQL_FAILURE : CSQL_SUCCESS);
}

/*
 * uncontrol_strndup() - variation of strdup()
 *   return:  newly allocated string
 *   from(in): source string
 *   length(in): length of source string
 */
static char *
uncontrol_strndup (const char *from, int length)
{
  char *to;

  /* allocate memory for `to' */
  to = (char *) malloc (length + 1);
  if (to == NULL)
    {
      csql_Error_code = CSQL_ERR_NO_MORE_MEMORY;
      return ((char *) NULL);
    }

  memcpy (to, from, length);
  to[length] = 0;

  return to;
}

/*
 * uncontrol_strdup() - variation of strdup()
 *   return:  newly allocated string
 *   from(in): source string
 */
static char *
uncontrol_strdup (const char *from)
{
  return uncontrol_strndup (from, strlen (from));
}
