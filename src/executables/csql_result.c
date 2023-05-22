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
 * csql_result.c : Query execution / result handling routine
 */

#ident "$Id$"

#include "config.h"

#include <assert.h>
#include <float.h>
#include <setjmp.h>
#include <signal.h>

#include "csql.h"
#include "dbtran_def.h"
#include "dbtype.h"
#include "memory_alloc.h"
#include "object_primitive.h"
#include "porting.h"
#include "transaction_cl.h"

#if defined (SUPPRESS_STRLEN_WARNING)
#define strlen(s1)  ((int) strlen(s1))
#endif /* defined (SUPPRESS_STRLEN_WARNING) */

/* max columns to display each data type
 * NOTE: some of these are totally dependent on report-writer's
 * rendering library.
 */
#define	MAX_SHORT_DISPLAY_LENGTH	  6
#define	MAX_INTEGER_DISPLAY_LENGTH	  11
#define	MAX_BIGINT_DISPLAY_LENGTH	  20
#define	MAX_FLOAT_DISPLAY_LENGTH	  (FLT_DIG + 7)
#define	MAX_DOUBLE_DISPLAY_LENGTH	  (DBL_DIG + 9)
#define	MAX_TIME_DISPLAY_LENGTH		  11
#define MAX_TIMEZONE_DISPLAY_LENGTH	  18
#define	MAX_UTIME_DISPLAY_LENGTH	  25
#define	MAX_TIMESTAMPTZ_DISPLAY_LENGTH  \
  (MAX_UTIME_DISPLAY_LENGTH + MAX_TIMEZONE_DISPLAY_LENGTH)
#define MAX_DATETIME_DISPLAY_LENGTH       29
#define MAX_DATETIMETZ_DISPLAY_LENGTH  \
  (MAX_DATETIME_DISPLAY_LENGTH + MAX_TIMEZONE_DISPLAY_LENGTH)
#define	MAX_DATE_DISPLAY_LENGTH		  10
#define	MAX_MONETARY_DISPLAY_LENGTH	  20
#define	MAX_DEFAULT_DISPLAY_LENGTH	  20
#define STRING_TYPE_PREFIX_SUFFIX_LENGTH  2
#define NSTRING_TYPE_PREFIX_SUFFIX_LENGTH 3
#define BIT_TYPE_PREFIX_SUFFIX_LENGTH     3

/* structure for current query result information */
typedef struct
{
  DB_QUERY_RESULT *query_result;
  int num_attrs;
  char **attr_names;
  int *attr_lengths;
  DB_TYPE *attr_types;
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
  {CUBRID_STMT_DROP_STORED_PROCEDURE, "DROP PROCEDURE"},
  {CUBRID_STMT_TRUNCATE, "TRUNCATE"},
  {CUBRID_STMT_SET_SESSION_VARIABLES, "SET"},
  {CUBRID_STMT_DROP_SESSION_VARIABLES, "DROP VARIABLE"},
  {CUBRID_STMT_DO, "DO"},
  {CUBRID_STMT_SET_NAMES, "SET NAMES"},
  {CUBRID_STMT_VACUUM, "VACUUM"},
  {CUBRID_STMT_SET_TIMEZONE, "SET TIMEZONE"},
  {CUBRID_STMT_CREATE_SERVER, "CREATE SERVER"},
  {CUBRID_STMT_DROP_SERVER, "DROP SERVER"},
  {CUBRID_STMT_RENAME_SERVER, "RENAME SERVER"},
  {CUBRID_STMT_ALTER_SERVER, "ALTER SERVER"},
  {CUBRID_STMT_ALTER_SYNONYM, "ALTER SYNONYM"},
  {CUBRID_STMT_CREATE_SYNONYM, "CREATE SYNONYM"},
  {CUBRID_STMT_DROP_SYNONYM, "DROP SYNONYM"},
  {CUBRID_STMT_RENAME_SYNONYM, "RENAME SYNONYM"}
};

static const char *csql_Isolation_level_string[] = {
  "UNKNOWN",
  "UNKNOWN",
  "UNKNOWN",
  "UNKNOWN",
  "READ COMMITTED",
  "REPEATABLE READ",
  "SERIALIZABLE"
};

static jmp_buf csql_Jmp_buf;

static const char *csql_cmd_string (CUBRID_STMT_TYPE stmt_type, const char *default_string);
static void display_empty_result (int stmt_type, int line_no);
static char **get_current_result (int **len, const CUR_RESULT_INFO * result_info, bool plain_output, bool query_output,
				  bool loaddb_output, char column_enclosure);
static int write_results_to_stream (const CSQL_ARGUMENT * csql_arg, FILE * fp, const CUR_RESULT_INFO * result_info);
static char *uncontrol_strdup (const char *from);
static char *uncontrol_strndup (const char *from, int length);
static int calculate_width (int column_width, int string_width, int origin_width, DB_TYPE attr_type, bool is_null);
static bool is_string_type (DB_TYPE type);
static bool is_nstring_type (DB_TYPE type);
static bool is_bit_type (DB_TYPE type);
static bool is_cuttable_type_by_string_width (DB_TYPE type);
static bool is_type_that_has_suffix (DB_TYPE type);

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
csql_results (const CSQL_ARGUMENT * csql_arg, DB_QUERY_RESULT * result, DB_QUERY_TYPE * attr_spec, int line_no,
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
  DB_TYPE *attr_types = NULL;
  int max_attr_name_length = 0;

  /* trivial case - no results */
  if (result == NULL || (err = db_query_first_tuple (result)) == DB_CURSOR_END)
    {
      if (csql_arg->plain_output == false && csql_arg->query_output == false && csql_arg->loaddb_output == false)
	{
	  display_empty_result (stmt_type, line_no);
	}
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
  attr_types = (DB_TYPE *) malloc (sizeof (DB_TYPE) * num_attrs);
  if (attr_name_lengths == NULL || attr_lengths == NULL || attr_types == NULL)
    {
      csql_Error_code = CSQL_ERR_NO_MORE_MEMORY;
      goto error;
    }

  /* get the result attribute names */

  max_attr_name_length = 0;

  for (i = 0, t = attr_spec; t != NULL; t = db_query_format_next (t), i++)
    {
      const char *temp;

      temp = db_query_format_name (t);
      if (temp == NULL)
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
	  bool is_console_conv = false;

	  /* console encoded attribute name */
	  if (csql_text_utf8_to_console != NULL)
	    {
	      char *attr_name_console_encoded = NULL;
	      int attr_name_console_length = -1;

	      /* try to convert attribute name from utf-8 to console */
	      if ((*csql_text_utf8_to_console)
		  (temp, strlen (temp), &attr_name_console_encoded, &attr_name_console_length) == NO_ERROR)
		{
		  if (attr_name_console_encoded != NULL)
		    {
		      free_and_init (attr_names[i]);
		      attr_names[i] = attr_name_console_encoded;
		      is_console_conv = true;
		    }
		}
	    }

	  if (!is_console_conv)
	    {
	      attr_names[i] = uncontrol_strdup (temp);
	      if (attr_names[i] == NULL)
		{
		  goto error;
		}
	    }
	}
      attr_name_lengths[i] = strlen (attr_names[i]);
      max_attr_name_length = MAX (max_attr_name_length, attr_name_lengths[i]);
      attr_types[i] = db_query_format_type (t);

      switch (attr_types[i])
	{
	case DB_TYPE_SHORT:
	  attr_lengths[i] = MAX (MAX_SHORT_DISPLAY_LENGTH, attr_name_lengths[i]);
	  break;
	case DB_TYPE_INTEGER:
	  attr_lengths[i] = MAX (MAX_INTEGER_DISPLAY_LENGTH, attr_name_lengths[i]);
	  break;
	case DB_TYPE_BIGINT:
	  attr_lengths[i] = MAX (MAX_BIGINT_DISPLAY_LENGTH, attr_name_lengths[i]);
	  break;
	case DB_TYPE_FLOAT:
	  attr_lengths[i] = MAX (MAX_FLOAT_DISPLAY_LENGTH, attr_name_lengths[i]);
	  break;
	case DB_TYPE_DOUBLE:
	  attr_lengths[i] = MAX (MAX_DOUBLE_DISPLAY_LENGTH, attr_name_lengths[i]);
	  break;
	case DB_TYPE_TIME:
	  attr_lengths[i] = -MAX (MAX_TIME_DISPLAY_LENGTH, attr_name_lengths[i]);
	  break;
	case DB_TYPE_TIMESTAMP:
	  attr_lengths[i] = -MAX (MAX_UTIME_DISPLAY_LENGTH, attr_name_lengths[i]);
	  break;
	case DB_TYPE_TIMESTAMPTZ:
	case DB_TYPE_TIMESTAMPLTZ:
	  attr_lengths[i] = -MAX (MAX_TIMESTAMPTZ_DISPLAY_LENGTH, attr_name_lengths[i]);
	  break;
	case DB_TYPE_DATETIME:
	  attr_lengths[i] = -MAX (MAX_DATETIME_DISPLAY_LENGTH, attr_name_lengths[i]);
	  break;
	case DB_TYPE_DATETIMETZ:
	case DB_TYPE_DATETIMELTZ:
	  attr_lengths[i] = -MAX (MAX_DATETIMETZ_DISPLAY_LENGTH, attr_name_lengths[i]);
	  break;
	case DB_TYPE_DATE:
	  attr_lengths[i] = -MAX (MAX_DATE_DISPLAY_LENGTH, attr_name_lengths[i]);
	  break;
	case DB_TYPE_MONETARY:
	  attr_lengths[i] = MAX (MAX_MONETARY_DISPLAY_LENGTH, attr_name_lengths[i]);
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
  result_info.attr_types = attr_types;
  result_info.max_attr_name_length = max_attr_name_length;
  result_info.curr_stmt_type = stmt_type;
  result_info.curr_stmt_line_no = line_no;

  /*
   * Write_results_to_stream may need to fetch instances if value type is object or set of objects.
   * Make sure fetch type is not set to current version since all the versions are identified by
   * the same OID and snapshot must be considered to reach the visible version again. */
  assert (TM_TRAN_READ_FETCH_VERSION () != LC_FETCH_CURRENT_VERSION);
  if (write_results_to_stream (csql_arg, csql_Output_fp, &result_info) == CSQL_FAILURE)
    {
      if (csql_Error_code == CSQL_ERR_SQL_ERROR)
	{
	  goto error;
	}
      else
	{
	  nonscr_display_error (csql_Scratch_text, SCRATCH_TEXT_LEN);
	}
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
  if (attr_types != NULL)
    {
      free_and_init (attr_types);
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
  if (attr_types != NULL)
    {
      free_and_init (attr_types);
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

  snprintf (csql_Scratch_text, SCRATCH_TEXT_LEN,
	    msgcat_message (MSGCAT_CATALOG_CSQL, MSGCAT_CSQL_SET_CSQL, CSQL_RESULT_STMT_TITLE_FORMAT),
	    csql_cmd_string ((CUBRID_STMT_TYPE) stmt_type, ""), line_no);

  pf = csql_popen (csql_Pager_cmd, csql_Output_fp);

  csql_fputs ("\n=== ", pf);
  csql_fputs_console_conv (csql_Scratch_text, pf);
  csql_fputs (" ===\n\n", pf);
  csql_fputs_console_conv (msgcat_message
			   (MSGCAT_CATALOG_CSQL, MSGCAT_CSQL_SET_CSQL, CSQL_STAT_NONSCR_EMPTY_RESULT_TEXT), pf);
  csql_fputs ("\n", pf);

  csql_pclose (pf, csql_Output_fp);

  return;
}

/*
 * get_current_result() - get the attribute values of the current result
 *   return: pointer newly allocated value array. On error, NULL.
 *   lengths(out): lengths of returned values
 *   result_info(in): pointer to current query result info structure
 *   plain_output(in): refine string for plain output
 *   query_output(in): refine string for query output
 *   loaddb_output(in): refine string for loaddb output
 *   column_enclosure(in): column enclosure for query output
 *
 * Note:
 *   Caller should be responsible for free the return array and its elements.
 */
static char **
get_current_result (int **lengths, const CUR_RESULT_INFO * result_info, bool plain_output, bool query_output,
		    bool loaddb_output, char column_enclosure)
{
  int i;
  char **val = NULL;		/* temporary array for values */
  int *len = NULL;		/* temporary array for lengths */
  DB_VALUE db_value;
  CUBRID_STMT_TYPE stmt_type = result_info->curr_stmt_type;
  DB_QUERY_RESULT *result = result_info->query_result;
  int num_attrs = result_info->num_attrs;

  db_make_null (&db_value);

  val = (char **) malloc (sizeof (char *) * num_attrs);
  if (val == NULL)
    {
      csql_Error_code = CSQL_ERR_NO_MORE_MEMORY;
      goto error;
    }
  memset (val, 0, sizeof (char *) * num_attrs);

  len = (int *) malloc (sizeof (int) * num_attrs);
  if (len == NULL)
    {
      csql_Error_code = CSQL_ERR_NO_MORE_MEMORY;
      goto error;
    }
  memset (len, 0, sizeof (int) * num_attrs);

  (void) db_query_set_copy_tplvalue (result, 0 /* peek */ );

  /* get attribute values */
  for (i = 0; i < num_attrs; i++)
    {
      DB_TYPE value_type;

      if (db_query_get_tuple_value (result, i, &db_value) < 0)
	{
	  csql_Error_code = CSQL_ERR_SQL_ERROR;
	  goto error;
	}

      value_type = DB_VALUE_TYPE (&db_value);

      /*
       * This assert is intended to validate that the server returned the
       * expected types for the query results. See the note in
       * pt_print_value () regarding XASL caching.
       */
      /*
       * TODO fix this assert if it fails in valid cases. Perhaps it should
       *      allow DB_TYPE_POINTER? What about DB_TYPE_ERROR?
       */
      /*
       * TODO add a similar check to the ux_* and/or cci_* and/or the server
       *      functions so that the results' types returned through sockets in
       *      CS_MODE are validated.
       */
      assert (value_type == DB_TYPE_NULL
	      /* UNKNOWN, maybe host variable */
	      || result_info->attr_types[i] == DB_TYPE_NULL || result_info->attr_types[i] == DB_TYPE_VARIABLE
	      || value_type == result_info->attr_types[i]);

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
	  sprintf (val[i], "pointer value (%p)", (void *) db_get_pointer (&db_value));
	  break;

	case DB_TYPE_ERROR:	/* error type */
	  val[i] = (char *) malloc (40);
	  if (val[i] == NULL)
	    {
	      csql_Error_code = CSQL_ERR_NO_MORE_MEMORY;
	      goto error;
	    }
	  sprintf (val[i], "error code (%d)", db_get_error (&db_value));
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

	      async_ws = db_get_int (&db_value) & TRAN_ASYNC_WS_BIT;
	      iso_lvl = db_get_int (&db_value) & TRAN_ISO_LVL_BITS;

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

	      sprintf (val[i], "%s%s", csql_Isolation_level_string[iso_lvl], (async_ws ? ", ASYNC WORKSPACE" : ""));
	    }
	  else if ((stmt_type == CUBRID_STMT_GET_TIMEOUT) && (db_get_float (&db_value) == -1.0))
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
	      CSQL_OUTPUT_TYPE output_type;

	      if (query_output == true)
		{
		  output_type = CSQL_QUERY_OUTPUT;
		}
	      else if (loaddb_output == true)
		{
		  output_type = CSQL_LOADDB_OUTPUT;;
		}
	      else
		{
		  output_type = CSQL_UNKNOWN_OUTPUT;
		}

	      temp = csql_db_value_as_string (&db_value, &len[i], plain_output, output_type, column_enclosure);
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

      if (db_value.need_clear)
	{
	  pr_clear_value (&db_value);
	}
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
    {
      free_and_init (len);
    }
  if (db_value.need_clear)
    {
      pr_clear_value (&db_value);
    }
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
write_results_to_stream (const CSQL_ARGUMENT * csql_arg, FILE * fp, const CUR_RESULT_INFO * result_info)
{
  /*
   * These are volatile to avoid dangerous interaction with the longjmp
   * handler for SIGPIPE problems.  The typedef is necessary so that we
   * can tell the compiler that the top POINTER is volatile, not the
   * characters that it eventually points to.
   */
  typedef char **value_array;
  volatile value_array val;	/* attribute values array */
  volatile int error;		/* to switch return of CSQL_FAILURE/CSQL_SUCCESS */
  int i;			/* loop counter */
  int object_no;		/* result object count */
  int e;			/* error code from DBI */
  FILE *pf;			/* pipe stream to pager */
  int n;			/* # of cols for a line */
  CUBRID_STMT_TYPE stmt_type = result_info->curr_stmt_type;
  DB_QUERY_RESULT *result = result_info->query_result;
  DB_TYPE *attr_types = result_info->attr_types;
  int line_no = result_info->curr_stmt_line_no;
  int num_attrs = result_info->num_attrs;
  int *attr_lengths = result_info->attr_lengths;
  char **attr_names = result_info->attr_names;
  char *refined_attr_name = NULL;
  char *value = NULL;
  int max_attr_name_length = result_info->max_attr_name_length;
  int column_width;
  int csql_string_width = csql_arg->string_width;
  char csql_column_delimiter;
  int value_width;
  bool is_null;

  val = (char **) NULL;
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

      if (csql_arg->plain_output == false && csql_arg->query_output == false && csql_arg->loaddb_output == false)
	{
	  csql_fputs ("\n=== ", pf);
	  snprintf (csql_Scratch_text, SCRATCH_TEXT_LEN, csql_get_message (CSQL_RESULT_STMT_TITLE_FORMAT),
		    csql_cmd_string (stmt_type, "UNKNOWN"), line_no);
	  csql_fputs (csql_Scratch_text, pf);
	  csql_fputs (" ===\n\n", pf);
	}

      if (db_query_first_tuple (result) < 0)
	{
	  csql_Error_code = CSQL_ERR_SQL_ERROR;
	  error = TRUE;
	}
      else
	{
	  if (csql_arg->skip_column_names == true || csql_arg->line_output == true)
	    {
	      ;
	    }
	  else if (csql_arg->plain_output == true || csql_arg->query_output == true)
	    {
	      csql_column_delimiter = (csql_arg->query_output == true) ? csql_arg->column_delimiter : '\t';

	      for (i = 0; i < num_attrs; i++)
		{
		  refined_attr_name = csql_string_to_plain_string (attr_names[i], strlen (attr_names[i]), NULL);
		  if (refined_attr_name != NULL)
		    {
		      fprintf (pf, "%s", refined_attr_name);
		      free_and_init (refined_attr_name);
		    }
		  else
		    {
		      fprintf (pf, "UNKNOWN");
		    }

		  if (i == num_attrs - 1)
		    {
		      fprintf (pf, "\n");
		    }
		  else
		    {
		      fprintf (pf, "%c", csql_column_delimiter);
		    }
		}
	    }
	  else if (csql_arg->loaddb_output == true)
	    {
	      fprintf (pf, "%%class [ ] (");
	      for (i = 0; i < num_attrs; i++)
		{
		  refined_attr_name = csql_string_to_plain_string (attr_names[i], strlen (attr_names[i]), NULL);
		  if (refined_attr_name != NULL)
		    {
		      fprintf (pf, "[%s]", refined_attr_name);
		      free_and_init (refined_attr_name);
		    }
		  else
		    {
		      fprintf (pf, "UNKNOWN");
		    }

		  if (i == num_attrs - 1)
		    {
		      fprintf (pf, ")\n");
		    }
		  else
		    {
		      fprintf (pf, " ");
		    }
		}
	    }
	  else
	    {
	      for (n = i = 0; i < num_attrs; i++)
		{
		  fprintf (pf, "  %*s", (int) (attr_lengths[i]), attr_names[i]);
		  n += 2 + ((attr_lengths[i] > 0) ? attr_lengths[i] : -attr_lengths[i]);
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
	      int *len = NULL;

	      val =
		get_current_result (&len, result_info, csql_arg->plain_output, csql_arg->query_output,
				    csql_arg->loaddb_output, csql_arg->column_enclosure);
	      if (val == NULL)
		{
		  csql_Error_code = CSQL_ERR_SQL_ERROR;
		  error = TRUE;
		  if (len != NULL)
		    {
		      free (len);
		    }
		  break;
		}

	      if (csql_arg->line_output == true)
		{
		  fprintf (pf, "<%05d>", object_no);
		  for (i = 0; i < num_attrs; i++)
		    {
		      fprintf (pf, "%*c", (int) ((i == 0) ? 1 : 8), ' ');
		      fprintf (pf, "%*s: %s\n", (int) (-max_attr_name_length), attr_names[i], val[i]);
		    }
		  /* fflush(pf); */
		}
	      else if (csql_arg->plain_output == true)
		{
		  for (i = 0; i < num_attrs - 1; i++)
		    {
		      fprintf (pf, "%s\t", val[i]);
		    }
		  fprintf (pf, "%s\n", val[i]);
		}
	      else if (csql_arg->query_output == true || csql_arg->loaddb_output == true)
		{
		  for (i = 0; i < num_attrs - 1; i++)
		    {
		      fprintf (pf, "%s%c", val[i], csql_arg->column_delimiter);
		    }
		  fprintf (pf, "%s\n", val[i]);
		}
	      else
		{
		  int padding_size;

		  for (i = 0; i < num_attrs; i++)
		    {
		      if (strcmp ("NULL", val[i]) == 0)
			{
			  is_null = true;
			}
		      else
			{
			  is_null = false;
			}

		      column_width = csql_get_column_width (attr_names[i]);
		      value_width = calculate_width (column_width, csql_string_width, len[i], attr_types[i], is_null);

		      padding_size =
			(attr_lengths[i] > 0) ? MAX (attr_lengths[i] - (value_width),
						     0) : MIN (attr_lengths[i] + (value_width), 0);

		      fprintf (pf, "  ");
		      if (padding_size > 0)
			{
			  /* right justified */
			  fprintf (pf, "%*s", (int) padding_size, "");
			}

		      value = val[i];
		      if (is_type_that_has_suffix (attr_types[i]) && is_null == false)
			{
			  value[value_width - 1] = '\'';
			}

		      fwrite (value, 1, value_width, pf);

		      if (padding_size < 0)
			{
			  /* left justified */
			  fprintf (pf, "%*s", (int) (-padding_size), "");
			}
		    }
		  putc ('\n', pf);
		  /* fflush(pf); */
		}
	      if (len != NULL)
		{
		  free (len);
		}

	      /* advance to next */
	      e = db_query_next_tuple (result);
	      if (e < 0)
		{
		  csql_Error_code = CSQL_ERR_SQL_ERROR;
		  error = TRUE;
		  break;
		}
	      else if (e == DB_CURSOR_END)
		{
		  break;
		}
	    }
	  if (error != TRUE)
	    {
	      putc ('\n', pf);
	    }
	}
    }

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

  return ((error) ? CSQL_FAILURE : CSQL_SUCCESS);
}

/*
 * calcluate_width() - calculate column's width
 *   return: width
 *   column_width(in): column width
 *   string_width(in): string width
 *   origin_width(in): real width
 *   attr_type(in): type
 *   is_null(in): check null
 */
int
calculate_width (int column_width, int string_width, int origin_width, DB_TYPE attr_type, bool is_null)
{
  int result = 0;

  if (column_width > 0)
    {
      if (is_null)
	{
	  result = column_width;
	}
      else if (is_string_type (attr_type))
	{
	  result = column_width + STRING_TYPE_PREFIX_SUFFIX_LENGTH;
	}
      else if (is_nstring_type (attr_type))
	{
	  result = column_width + NSTRING_TYPE_PREFIX_SUFFIX_LENGTH;
	}
      else if (is_bit_type (attr_type))
	{
	  result = column_width + BIT_TYPE_PREFIX_SUFFIX_LENGTH;
	}
      else
	{
	  result = column_width;
	}
    }
  else if (is_cuttable_type_by_string_width (attr_type) && string_width > 0)
    {
      if (is_null)
	{
	  result = string_width;
	}
      else if (is_string_type (attr_type))
	{
	  result = string_width + STRING_TYPE_PREFIX_SUFFIX_LENGTH;
	}
      else if (is_nstring_type (attr_type))
	{
	  result = string_width + NSTRING_TYPE_PREFIX_SUFFIX_LENGTH;
	}
      else if (is_bit_type (attr_type))
	{
	  result = string_width + BIT_TYPE_PREFIX_SUFFIX_LENGTH;
	}
      else
	{
	  result = string_width;
	}
    }
  else
    {
      result = origin_width;
    }

  if (result > origin_width)
    {
      result = origin_width;
    }
  if (result < 0)
    {
      result = 0;
    }

  return result;
}

/*
 * is_string_type() - check whether it is a string type or not
 *   return: bool
 *   type(in): type
 */
static bool
is_string_type (DB_TYPE type)
{
  switch (type)
    {
    case DB_TYPE_STRING:
      return true;
    case DB_TYPE_CHAR:
      return true;
    default:
      return false;
    }
  return false;
}

/*
 * is_nstring_type() - check whether it is a nstring type or not
 *   return: bool
 *   type(in): type
 */
static bool
is_nstring_type (DB_TYPE type)
{
  switch (type)
    {
    case DB_TYPE_NCHAR:
      return true;
    case DB_TYPE_VARNCHAR:
      return true;
    default:
      return false;
    }
  return false;
}

/*
 * is_bit_type() - check whether it is a bit type or not
 *   return: bool
 *   type(in): type
 */
static bool
is_bit_type (DB_TYPE type)
{
  switch (type)
    {
    case DB_TYPE_BIT:
      return true;
    case DB_TYPE_VARBIT:
      return true;
    default:
      return false;
    }
  return false;
}

/*
 * is_cuttable_type_by_string_width() - check whether it is cuttable type by string_width or not
 *   return: bool
 *   type(in): type
 */
static bool
is_cuttable_type_by_string_width (DB_TYPE type)
{
  return (is_string_type (type) || is_nstring_type (type) || is_bit_type (type));
}

/*
 * is_type_that_has_suffix() - check whether this type has suffix or not
 *   return: bool
 *   type(in): type
 */
static bool
is_type_that_has_suffix (DB_TYPE type)
{
  return (is_string_type (type) || is_nstring_type (type) || is_bit_type (type));
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
