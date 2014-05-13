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
 * esql_cli.c - CUBRID Call Interface
 */

#ident "$Id$"

#include "config.h"

#if !defined(WINDOWS)
#include <unistd.h>
#include <libgen.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#include <errno.h>
#include "misc_string.h"
#include "error_manager.h"
#include "system_parameter.h"

#define	_ESQL_KERNEL_
#include "cubrid_esql.h"

#include "memory_alloc.h"
#include "dbi.h"
#include "db.h"
#include "db_query.h"
#include "parser.h"
#include "esql_gadget.h"
#include "environment_variable.h"
#include "authenticate.h"
#include "dbval.h"		/* this must be the last header file included!!! */

#define UCI_OPT_UNSAFE_NULL     0x0001

/* # of entries at every expansion of monotonically increasing tables */
#define	REPETITIVE_EXPANSION_UNIT	100	/* repetitive stmt table */
#define	DYNAMIC_EXPANSION_UNIT		20	/* dynamically prepared stmts */
#define	POINTER_EXPANSION_UNIT		10	/* pointers from DBI */
#define	CS_STMT_ID_EXPANSION_UNIT	5	/* cs stmt ids for a cursor */
#define UCI_ENV_STACK_EXPANSION_UNIT	4	/* stack expansion unit */
#define DB_VALS_STCK_EXPANSION_UNIT     4	/* db values stack */

#define	IS_NULL_INDICATED(ind)	((ind) < 0)	/* negative */

/* macro to check if there was a error.
 * execution of an Embedded SQL/X statement is stopped upon error.
 * But, a user can specify 'CONTINUE' even though there is an error.
 * To implement such behaviour, every statement calls uci_start() function
 * first to clear current error code (if any set) and call successive
 * uci_ functions. Every uci_ function (except uci_stop()) will return
 * if there was an error in previous uci function.
 */
#define	CHK_SQLCODE()  \
  do {                 \
    if(SQLCODE < 0)    \
      {                \
        return;        \
      }                \
  } while(0)

/* macro to check if there was a warning in the previous DBI function call.
 * If it was, set the warning code to sqlca area.
 */
#define CHK_DBI_WARN()                       \
  do {                                       \
    if (sql_warn.is_warning)                 \
      {                                      \
	if (sql_warn.null_in_agg)            \
	  {                                  \
	    SET_WARN_NULL_IN_AGG ();         \
	    sql_warn.null_in_agg = false;    \
	  }                                  \
	else if (sql_warn.no_where)          \
	  {                                  \
	    SET_WARN_NO_WHERE ();            \
	    sql_warn.no_where = false;       \
	  }                                  \
	sql_warn.is_warning = false;         \
      }                                      \
  }                                          \
while (0)

/*
 * for short usage of frequent DBI function call result check.
 * NOTE: every dbi function call should use this macro for
 * consistent error processing
 */
#define	CHECK_DBI(predicate, err_action)  \
  do {                                   \
    if(predicate) { /* error from dbi */ \
      {                                  \
        set_sqlca_err();                 \
      }                                  \
      if(SQLCODE == ER_LK_UNILATERALLY_ABORTED ||           \
         SQLCODE == ER_TM_SERVER_DOWN_UNILATERALLY_ABORTED) \
        {                                \
          clean_up();                    \
        }                                \
      err_action;                        \
    }                                    \
    CHK_DBI_WARN();                      \
  } while(0)

/* to check if the given type belongs to legal esql DB_TYPE.
 * Note that some of DB_TYPE are not supported in esql
 */
#define	IS_USER_DB_TYPE(t)	((t) >= DB_TYPE_FIRST && (t) <= DB_TYPE_LAST)

/* to check if the given C type is a string type */
#define STRING_C_TYPE(s)	(((s)==DB_TYPE_C_CHAR) || \
				 ((s)==DB_TYPE_C_VARCHAR) || \
				 ((s)==DB_TYPE_C_NCHAR) || \
				 ((s)==DB_TYPE_C_VARNCHAR) || \
				 ((s)==DB_TYPE_C_BIT) || \
				 ((s)==DB_TYPE_C_VARBIT))

/* to check if the stmt type has connection to WHERE-clause */
#define	AFFECT_OBJECTS(stmt_type)	(stmt_type == CUBRID_STMT_SELECT || \
					 stmt_type == CUBRID_STMT_UPDATE || \
					 stmt_type == CUBRID_STMT_DELETE || \
					 stmt_type == CUBRID_STMT_INSERT)

/* to check if the stmt type will bring result or not */
#define	HAS_RESULT(stmt_type)	(stmt_type == CUBRID_STMT_SELECT || \
				 stmt_type == CUBRID_STMT_CALL || \
				 stmt_type == CUBRID_STMT_INSERT || \
				 stmt_type == CUBRID_STMT_GET_ISO_LVL || \
				 stmt_type == CUBRID_STMT_GET_TIMEOUT || \
				 stmt_type == CUBRID_STMT_GET_OPT_LVL || \
				 stmt_type == CUBRID_STMT_GET_STATS)

/* redefinition of readability of used SQLCA members */
#define SQLCA_NUM_AFFECTED_OBJECTS	sqlca.sqlerrd[2]
#define	SQLCA_IS_WARNING		SQLWARN0
#define	SQLCA_OUTPUT_TRUNC		SQLWARN1
#define	SQLCA_NULL_IN_AGG		SQLWARN2
#define	SQLCA_VARS_MISMATCH		SQLWARN3
#define	SQLCA_NO_WHERE			SQLWARN4

/* macros to set warnings */

/* string truncation in output */
#define	SET_WARN_OUTPUT_TRUNC() SQLCA_OUTPUT_TRUNC = \
				SQLCA_IS_WARNING = SQL_WARNING_CHAR

/* null is ignored in evaluation of aggregation function */
#define	SET_WARN_NULL_IN_AGG()	SQLCA_NULL_IN_AGG = \
				SQLCA_IS_WARNING = SQL_WARNING_CHAR

/* too many input vars or too few output vars */
#define	SET_WARN_VARS_MISMATCH() \
				SQLCA_VARS_MISMATCH = \
				SQLCA_IS_WARNING = SQL_WARNING_CHAR

/* update/delete without WHERE-clause is compiled */
#define	SET_WARN_NO_WHERE()	SQLCA_NO_WHERE = \
				SQLCA_IS_WARNING = SQL_WARNING_CHAR

/* error codes introduced by esql_cli module */

/* no more memory */
#define	PUT_UCI_ERR_NOMOREMEMORY(size)	\
	er_set(ER_ERROR_SEVERITY, __FILE__, __LINE__, \
		ER_OUT_OF_VIRTUAL_MEMORY, 1, (size))

/* input host variables is given less than # of positional markers */
#define	PUT_UCI_ERR_TOOFEWHOSTVARS(num_markers)	\
	er_set(ER_ERROR_SEVERITY, __FILE__, __LINE__, \
		ER_UCI_TOO_FEW_HOST_VARS, 0)

/* output host variables is given more than # of positional markers */
#define	PUT_UCI_ERR_TOOMANYHOSTVARS(num_cols)	\
	er_set(ER_ERROR_SEVERITY, __FILE__, __LINE__, \
		ER_UCI_TOO_MANY_HOST_VARS, 0)

/* db value is null but indicator is not given */
#define	PUT_UCI_ERR_NULLINDNEEDED()	\
	er_set(ER_ERROR_SEVERITY, __FILE__, __LINE__, \
		ER_UCI_NULL_IND_NEEDED, 0)

/* the stmt on which a cursor declared is not prepared when open the cursor */
#define	PUT_UCI_ERR_NOTPREPAREDSTMT()	\
	er_set(ER_ERROR_SEVERITY, __FILE__, __LINE__, \
		ER_UCI_NOT_PREPARED_STMT, 0)

/* the stmt should be SELECT */
#define	PUT_UCI_ERR_NOTSELECTSTMT()	\
	er_set(ER_ERROR_SEVERITY, __FILE__, __LINE__, \
		ER_UCI_NOT_SELECT_STMT, 0)

/* the cursor is not opened yet */
#define	PUT_UCI_ERR_CURSORNOTOPENED()	\
	er_set(ER_ERROR_SEVERITY, __FILE__, __LINE__, \
		ER_UCI_CURSOR_NOT_OPENED, 0)

/* attempt to open a cursor which is still open */
#define	PUT_UCI_ERR_CURSORSTILLOPEN()	\
	er_set(ER_ERROR_SEVERITY, __FILE__, __LINE__, \
		ER_UCI_CURSOR_STILL_OPEN, 0)

/* more than one instances in result of static statement */
#define	PUT_UCI_ERR_MULTIPLEOBJECTS()	\
	er_set(ER_ERROR_SEVERITY, __FILE__, __LINE__, \
		ER_UCI_MULTIPLE_OBJECTS, 0)

/* more than one statement is given */
#define	PUT_UCI_ERR_NOMARKALLOWED()	\
	er_set(ER_ERROR_SEVERITY, __FILE__, __LINE__, \
		ER_UCI_NO_MARK_ALLOWED, 0)

/* invalid data type is given */
#define	PUT_UCI_ERR_INVALIDDATATYPE()	\
	er_set(ER_ERROR_SEVERITY, __FILE__, __LINE__, \
		ER_UCI_INVALID_DATA_TYPE, 0)

/* invalid cursor position */
#define	PUT_UCI_ERR_INVALIDCSPOS()	\
	er_set(ER_ERROR_SEVERITY, __FILE__, __LINE__, \
		ER_QPROC_INVALID_CRSPOS, 0)

#if defined(UCI_TEMPORARY)
typedef struct sql_warn_type
{
  unsigned int is_warning:1;
  unsigned int null_in_agg:1;
  unsigned int no_where:1;
} SQL_WARN_TYPE;
#endif

/* every pointer value returned from db_query_get_value_to_pointer()
 * should be kept until next SELECT or FETCH statement
 */
typedef struct pointer
{
  DB_VALUE value_copy;
} POINTER;

/* descriptors for repetitive statement
 * during life-time of the process, it is increasing (never shrinking).
 */
typedef struct repetitive
{
  void *file_id;		/* source file identifier */
  int no;			/* stmt no in the file */
  DB_SESSION *session;		/* query compile/execute context */
  STATEMENT_ID stmt_id;		/* compiled stmt number */
  CUBRID_STMT_TYPE stmt_type;	/* true if the stmt is SELECT */
  DB_GADGET *gadget;
} REPETITIVE;

/* structure of a dynamic statement prepared.
 * during life-time of a transaction, it is increasing (never shrinking).
 * NOTE: currently, query result library function to get result column
 * information requires statement contents itself. `stmt' member is only
 * for this function. if the function is changed to get the information
 * using compiled stmt, then, `stmt' is no longer required.
 */
typedef struct dynamic
{
  void *file_id;		/* source file identifier */
  int stmt_no;			/* statement no */
  CUBRID_STMT_TYPE stmt_type;	/* statement type */
  DB_SESSION *session;		/* query compile/execute context */
  STATEMENT_ID stmt_id;		/* compiled stmt number */
  int num_markers;		/* # of input markers */
  DB_QUERY_TYPE *column_spec;	/* result column descriptor */
  char *saved_stmt;
  int saved_length;
} DYNAMIC;

/* simple structure to maintain compiled stmt identifiers of update/delete
 * through cursor stmts which is belong to a cursor.
 */
typedef struct cs_stmt_id
{
  int no;			/* serial number given by pre-processor */
  DB_SESSION *session;		/* query compile/execute context */
  STATEMENT_ID stmt_id;		/* compiled stmt id */
} CS_STMT_ID;

/* run-time cursor information.
 * this is a linked-list structure because it is dynamically increased
 * and shrinked during run-time by OPEN/CLOSE cursor statement.
 */
typedef struct cursor
{
  void *file_id;		/* file of the cursor */
#if defined(UCI_TEMPORARY)
  DB_SESSION *static_session;
  STATEMENT_ID static_stmt_id;
#endif
  int no;			/* sequential number */
  int num_stmt_ids;		/* # of element in `stmt_ids' */
  CS_STMT_ID *stmt_ids;		/* array of stmt id for update/
				 * delete cursor stmts which
				 * belongs to this cursor
				 */
  int last_stmt_id;		/* last searched stmt_id */
  DB_QUERY_RESULT *result;	/* result pointer */
  long fetched_tuples;		/* # of fetched tuples */
  int num_columns;		/* # of result cols */
  int curr_column;		/* current column */
  POINTER *pointers;		/* pointers to be free */
  int num_pointers;		/* # of pointers to be free */
  struct cursor *next;		/* pointer to next entry */
} CURSOR;

/* structure of an entry of the stack */
typedef struct uci_env_stack_entry
{
  char saved_sqlca_sqlwarn[sizeof (((CUBRIDCA *) 0)->sqlwarn)];
  void *saved_db_values;
  int saved_num_db_values;
  int saved_db_value_top;
  void *saved_curr_file;
  const char *saved_curr_filename;
  int saved_curr_lineno;
} UCI_ENV_STACK_ENTRY;

#if defined(UCI_TEMPORARY)
SQL_WARN_TYPE sql_warn;
#endif

unsigned int _uci_opt;
CUBRIDCA sqlca;
DB_INDICATOR uci_null_ind;
DB_VALUE *default_value;

/* executable file name of this process */
static char program_name[PATH_MAX];

/* information of table for db values (bound on host variables) to CUBRID */
static struct
{
  DB_VALUE *db_values;		/* table */
  int num_db_values;		/* # of db_value */
  int db_value_top;		/* top index */
} db_Value_table;

/* pointer to the head of cursor list */
static CURSOR *cursor_list = (CURSOR *) NULL;

/* repetitive statements table */
static REPETITIVE *repetitive_stmts;	/* table */
static int num_repetitive_stmts = 0;	/* # of entries */

/* the index of repetitive stmt table most recently searched. it is used to
 * minimize the cost of succeeding search trial for the same stmt entry.
 */
static int last_repetitive;

/* dynamically prepared statements table */
static DYNAMIC *dynamic_stmts;	/* table */
static int num_dynamic_stmts = 0;	/* # of entries */

/* the index of dynamic stmt table most recently searched. it is used to
 * minimize the cost of succeeding search trial for the same stmt entry.
 */
static int last_dynamic;

/* pointer table from DBI */
static POINTER *pointers;	/* table */
static int num_pointers = 0;	/* # of entries */

/* source file identifier which is executing esql_cli functions currently.
 * it is set by uci_start() function.
 */
static void *curr_file;
static const char *curr_filename;
static int curr_lineno;

/* result container for statement most recently executed */
static DB_QUERY_RESULT *curr_result = (DB_QUERY_RESULT *) NULL;	/* result */
static int num_curr_result_columns = 0;	/* # of columns */
static int curr_result_column = 0;	/* current column */

/* flags to identify status of client process */
static bool connected = false;

/* to indicate which of uci_start()/uci_end() is called more recently */
static bool is_uci_start_state = false;

/* varaibles for stack and its description */
static UCI_ENV_STACK_ENTRY *uci_env_stack;
static int num_uci_env_stack_entries = 0;
static int uci_env_stack_top = -1;

static void check_stack_size (void);
static void free_db_values (void);
static int uci_get_next_column (int cs_no, DB_VALUE * dbval);
static void uci_get_value_indirect (int cs_no,
				    DB_INDICATOR * ind, void **bufp,
				    int *sizep);
static REPETITIVE *alloc_repetitive (int no, DB_SESSION * session,
				     STATEMENT_ID stmt_id,
				     CUBRID_STMT_TYPE stmt_type,
				     DB_GADGET * gadget);
static REPETITIVE *get_repetitive (int no);
static void free_repetitive (void);
static DYNAMIC *alloc_dynamic (int stmt_no, CUBRID_STMT_TYPE stmt_type,
			       DB_SESSION * session, STATEMENT_ID stmt_id,
			       int num_markers, DB_QUERY_TYPE * col_spec,
			       char *stmt, int length);
static DYNAMIC *get_dynamic (int stmt_no);
static void free_dynamic (void);
#if defined(UCI_TEMPORARY)
static CURSOR *alloc_cursor (int no, DB_SESSION * static_session,
			     STATEMENT_ID static_stmt_id,
			     DB_QUERY_RESULT * result);
#else /* UCI_TEMPORARY */
static CURSOR *alloc_cursor (int no, DB_QUERY_RESULT * result);
#endif /* UCI_TEMPORARY */
static CURSOR *get_cursor (int no);
static void free_cursor (int no);
static POINTER *put_pointer (CURSOR * cs, DB_VALUE * addr);
static void free_pointers (CURSOR * cs);
static void copy_column_spec_to_sqlda (DB_QUERY_TYPE * col_spec,
				       CUBRIDDA * desc);
static void set_sqlca_err (void);
static int push_uci_environment (void);
static void pop_uci_environment (void);
static void drop_uci_env_stack (void);
static void clean_up (void);

/*
 * uci_startup() - tell esql_cli the `pgm_name' is the executable file name
 * return : void
 * pgm_name(in) : executable file name of this process
 * note : it will be used for db_restart(), see uci_connect()).
 *    initialize some esql_cli global variables.
 */
void
uci_startup (const char *pgm_name)
{
  unsigned int i;

  strcpy (program_name, (pgm_name != NULL) ? pgm_name : "");

  sqlca.sqlcode = 0;
  sqlca.sqlfile = NULL;
  sqlca.sqlline = 0;
  strcpy (sqlca.sqlcaid, "SQLCA ");
  sqlca.sqlcabc = sizeof (CUBRIDCA);
  sqlca.sqlerrm.sqlerrml = 0;
  sqlca.sqlerrm.sqlerrmc[0] = '\0';
  sqlca.sqlerrp[0] = '\0';
  for (i = 0; i < DIM (sqlca.sqlerrd); i++)
    {
      sqlca.sqlerrd[i] = 0L;
    }
  sqlca.sqlwarn.sqlwarn0 = '\0';
  sqlca.sqlwarn.sqlwarn1 = '\0';
  sqlca.sqlwarn.sqlwarn2 = '\0';
  sqlca.sqlwarn.sqlwarn3 = '\0';
  sqlca.sqlwarn.sqlwarn4 = '\0';
  sqlca.sqlwarn.sqlwarn5 = '\0';
  sqlca.sqlwarn.sqlwarn6 = '\0';
  sqlca.sqlwarn.sqlwarn7 = '\0';
  for (i = 0; i < sizeof (sqlca.sqlext); i++)
    {
      sqlca.sqlext[i] = ' ';
    }

  uci_null_ind = 0;
  db_Value_table.db_values = NULL;
  db_Value_table.num_db_values = 0;
  db_Value_table.db_value_top = 0;
}

/*
 * uci_start() - This function tells esql_cli the start of new esql C statement.
 *    initialize sqlca variable, C source file identifier,
 *    db_Value_table.db_value_top.
 * return : void
 * file_id(in) : C source file identifier
 */
void
uci_start (void *file_id, const char *filename, int lineno, unsigned int opt)
{
  int i;
  bool is_new_environ = is_uci_start_state;

  is_uci_start_state = true;

  if (is_new_environ && push_uci_environment () != NO_ERROR)
    {
      return;
    }

  SQLCODE = 0;
  SQLFILE = filename;
  SQLLINE = lineno;
  if (SQLERRML > 0)
    {
      SQLERRML = 0;
      SQLERRMC[0] = '\0';
    }
  SQLCA_NUM_AFFECTED_OBJECTS = 0L;
  SQLCA_IS_WARNING = '\0';
  SQLCA_OUTPUT_TRUNC = '\0';
  SQLCA_NULL_IN_AGG = '\0';
  SQLCA_VARS_MISMATCH = '\0';
  SQLCA_NO_WHERE = '\0';

  curr_file = file_id;
  curr_filename = filename;
  curr_lineno = lineno;

  /* reset any current db value entries */
  for (i = 0; i < db_Value_table.db_value_top; i++)
    {
      db_value_clear (&db_Value_table.db_values[i]);
    }
  db_Value_table.db_value_top = 0;

  /* set UCI runtime option */
  _uci_opt = opt;
}

/*
 * uci_end() - This function finalizes stuffs which is specific to the current
 *    statement (i.e. effective from the very previous uci_start().
 * return : void
 *
 * note : Be sure to defer CHECK_DBI() until the end to make sure that we NULL
 *   out the curr_result pointer.
 */
void
uci_end (void)
{
  int status = 0;

  if (curr_result != NULL)
    {
      status = db_query_end (curr_result);
      curr_result = (DB_QUERY_RESULT *) NULL;
    }

  is_uci_start_state = false;
  CHECK_DBI (status < 0, return);
}

/*
 * uci_stop() - This function is used only for exiting upon failure. Rolls back
 *    the current transaction and disconnect, exit the process with displaying
 *    the current error msg.
 * return : none
 */
void
uci_stop (void)
{
  fprintf (stderr, "%s\n", SQLERRMC);
  fflush (stderr);

  if (connected)
    {
      uci_start ((void *) NULL, NULL, 0, 0);	/* clear current error status */
      uci_rollback ();
      uci_disconnect ();
    }

  exit (EXIT_FAILURE);
}

/*
 * uci_get_sqlcode() - Get UCI SQL Code
 * return : sql code
 */
long
uci_get_sqlcode (void)
{
  return sqlca.sqlcode;
}

/*
 * uci_get_sqlwarn_0() - Get UCI SQL warning 0
 * return : warning
 */
char
uci_get_sqlwarn_0 (void)
{
  return sqlca.sqlwarn.sqlwarn0;
}



/*
 * uci_connect() - Connect to a database. if already connected, disconnect
 *    from the current database and connect.
 * return : void
 * db_name(in) : database name
 * user_name(in) : user name
 * passwd(in) : password string
 *
 * note :  Connect as user 'PUBLIC' if no username is specified.
 */
void
uci_connect (const char *db_name, const char *user_name, const char *passwd)
{
  int error;

  CHK_SQLCODE ();
  if (connected)
    {
      /* already connected to some database */
      uci_disconnect ();
      CHK_SQLCODE ();
    }

  error =
    db_login ((user_name ? user_name : au_get_public_user_name ()), passwd);
  CHECK_DBI (error < 0, return);

  /* error handling option will be initialized by db_restart */
  error = db_restart (program_name, false, db_name);
  CHECK_DBI (error < 0, return);

  connected = true;
}

/*
 * uci_disconnect() - disconnect from the current database server.
 *    Free repetitive plans and binding table.
 * return : void
 */
void
uci_disconnect (void)
{
  int status = 0;

  CHK_SQLCODE ();

  if (connected)
    {
      if (prm_get_commit_on_shutdown ())
	{
	  uci_commit ();
	}
      else
	{
	  uci_rollback ();
	}
      CHK_SQLCODE ();

      free_repetitive ();	/* free all memory for repetitive statements */
      free_db_values ();	/* free binding table */
      drop_uci_env_stack ();	/* free env stack */

      /* shutdown server */
      status = db_shutdown ();
      connected = false;

      CHECK_DBI (status < 0, return);
    }
}

/*
 * uci_commit() - commit the current transaction and remove all cursors and
 *    prepared statements. free values spaces of previous non-cursor statement
 *    results.
 * return : none
 */
void
uci_commit (void)
{
  int error;

  CHK_SQLCODE ();

  free_cursor (-1);
  free_pointers ((CURSOR *) NULL);
  free_dynamic ();

  error = db_commit_transaction ();
  CHECK_DBI (error < 0, return);
}

/*
 * uci_rollback() - rollback current transaction and remove all cursors
 *    and prepared statements. free values spaces of previous non-cursor
 *    statement results.
 * return : void
 */
void
uci_rollback (void)
{
  int error;

  CHK_SQLCODE ();

  free_cursor (-1);
  free_pointers ((CURSOR *) NULL);
  free_dynamic ();

  error = db_abort_transaction ();
  CHECK_DBI (error < 0, return);
}

/*
 * uci_static() - execute static statement (not cursor/dynamic stmt).
 *    if stmt_no is negative, the stmt is regarded as ad hoc stmt.
 *    otherwise, it is regarded as repetitive. if `num_out_vars' is negative,
 *    it does not set warning when # of out vars mismatches # of output
 *    columns.
 * return : none
 * stmt_no(in) : constitute unique stmt id
 * stmt(in) : pointer to statement
 * length(in) : length of stmt
 * num_out_vars(in) : # of output host variables
 *
 * note : uci_static should only be called with a nonnegative, repetitive
 *    stmt_no when the preprocessor has determined that the statement is an
 *    INSERT statement which is a candidate for execution through the
 *    db_gadget interface (i.e. it is not a nested insert and does not have a
 *    subquery and does not insert multiple tuples).  Other usages of
 *    repetitive statements will not be successful, and repetitive statements
 *    which can't be translated into valid gadgets will be run as ad hoc
 *    statements.
 */
void
uci_static (int stmt_no, const char *stmt, int length, int num_out_vars)
{
  STATEMENT_ID stmt_id;
  REPETITIVE *rt;
  CUBRID_STMT_TYPE stmt_type;
  int e;
  DB_SESSION *session;
  DB_GADGET *gadget = NULL;
  DB_NODE *statement;
  int error;

  CHK_SQLCODE ();

  session = NULL;
  stmt_id = -1;
  stmt_type = (CUBRID_STMT_TYPE) 0;

  if (stmt_no >= 0)
    {
      rt = get_repetitive (stmt_no);
      if (rt != NULL)
	{
	  session = rt->session;
	  stmt_id = rt->stmt_id;
	  stmt_type = rt->stmt_type;
	  gadget = rt->gadget;
	}
    }

  if (stmt_id != -1)
    {
      /* must be a repetition */
      if (!gadget)
	{
	  db_push_values (session, db_Value_table.db_value_top,
			  db_Value_table.db_values);
	}
    }
  else
    {
      /* ad hoc or first trial of repetitive */
      session = db_open_buffer (stmt);
      if (session == NULL)
	{
	  /* alwarys er_error() < 0 */
	  CHECK_DBI (true, return);
	}

      statement = db_get_statement (session, 0);

      /* create gadget if possible */
      if (stmt_no >= 0 && statement->node_type == PT_INSERT)
	{
	  DB_NODE *att = NULL, *val = NULL, *val_list = NULL;
	  int attrlist_len = pt_length_of_list (pt_attrs_part (statement)), i;
	  const char *cname =
	    pt_get_name (pt_from_entity_part (pt_class_part (statement)));
	  char **attrs;

	  /* build attribute name array */
	  attrs = (char **) malloc ((attrlist_len + 1) * sizeof (char *));
	  if (attrs == NULL)
	    {
	      PUT_UCI_ERR_NOMOREMEMORY ((attrlist_len + 1) * sizeof (char *));
	      db_close_session (session);
	      CHECK_DBI (true, return);
	    }

	  for (i = 0, att = pt_attrs_part (statement); att;
	       i++, att = att->next)
	    {
	      attrs[i] = (char *) pt_get_name (att);
	    }
	  attrs[i] = NULL;

	  val_list = pt_values_part (statement);
	  assert (val_list != NULL);
	  if (val_list != NULL)
	    {
	      val = val_list->info.node_list.list;
	    }
	  if (val_list == NULL || val_list->next == NULL)
	    {
	      /* This is a single tuple insert. */
	      /* If gadget cannot be created, attempt to handle it as a
	         regular insert statement. */
	      gadget = db_gadget_create (cname, (const char **) attrs);
	    }
	  else
	    {
	      /* If the this is a multiple tuples insert handle it as a
	         regular insert statement. */
	      assert (gadget == NULL);
	    }

	  if (gadget)
	    {
	      PARSER_CONTEXT *parser = db_get_parser (session);
	      DB_VALUE tmp_val, *insert_value = NULL;

	      /* bind literal values */
	      for (i = 0; val != NULL;
		   i++, val = val->next, insert_value = NULL)
		{
		  if (val->node_type == PT_HOST_VAR)
		    {
		      continue;
		    }
		  if (pt_is_value_node (val))
		    {
		      insert_value = pt_value_to_db (parser, val);
		    }
		  else if (pt_is_expr_node (val))
		    {
		      /* Try to handle simple expressions so we don't
		         choke on things like negative numbers */
		      if (pt_is_value_node (val->info.expr.arg1)
			  && (!val->info.expr.arg2
			      || pt_is_value_node (val->info.expr.arg2))
			  && (!val->info.expr.arg3
			      || pt_is_value_node (val->info.expr.arg3)))
			{
			  pt_evaluate_tree_having_serial (parser, val,
							  &tmp_val, 1);
			  if (!parser->error_msgs)
			    {
			      insert_value = &tmp_val;
			    }
			}
		    }

		  if (insert_value == NULL || (db_gadget_bind (gadget,
							       gadget->
							       attrs[i].
							       attr_desc->
							       name,
							       insert_value))
		      != NO_ERROR)
		    {
		      /* If we've had a binding error or encountered a node
		         type we can't handle, free gadget and run a regular
		         insert statement */
		      db_gadget_destroy (gadget);
		      gadget = NULL;
		      break;
		    }
		}

	      /* fake these, which would have been obtained from
	         compiled statement */
	      stmt_id = 0;
	      stmt_type = CUBRID_STMT_INSERT;
	    }

	  free_and_init (attrs);
	}

      if (!gadget)
	{
	  /* non-repetitive or non-insert, or failed gadget */
	  db_push_values (session, db_Value_table.db_value_top,
			  db_Value_table.db_values);
	  stmt_id = db_compile_statement (session);
	  if (stmt_id < 0)
	    {
	      db_close_session (session);
	      CHECK_DBI (true, return);
	    }

	  stmt_type = (CUBRID_STMT_TYPE) db_get_statement_type (session,
								stmt_id);
	}

      if (gadget
	  && alloc_repetitive (stmt_no, session, stmt_id, stmt_type,
			       gadget) == NULL)
	{
	  db_close_session (session);
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	  CHECK_DBI (error < 0, return);
	  return;
	}
    }

  /* if it brings result, free pointers of previous results */
  if (HAS_RESULT (stmt_type))
    {
      free_pointers ((CURSOR *) NULL);
    }

  /* execute the compiled stmt */
  if (gadget)
    {
      DB_VALUE val;
      DB_OBJECT *obj;

      /* manufacture query result from object returned from db_gadget_exec */
      obj =
	db_gadget_exec (gadget, db_Value_table.db_value_top,
			db_Value_table.db_values);
      if (obj)
	{
	  e = 1;
	  db_make_object (&val, obj);
	  curr_result = db_get_db_value_query_result (&val);
	}
      else
	{
	  assert (er_errid () != NO_ERROR);
	  e = er_errid ();
	}
    }
  else
    {
      /* non-repetitive, non-insert, or failed gadget */
      e = db_execute_statement (session, stmt_id, &curr_result);
      db_push_values (session, 0, NULL);
      db_close_session (session);
    }

  if (!is_uci_start_state)	/* nested stmt was executed */
    {
      pop_uci_environment ();
    }

  CHECK_DBI (e < 0, return);

  if (AFFECT_OBJECTS (stmt_type))
    {
      SQLCA_NUM_AFFECTED_OBJECTS = e;
      if (e == 0)
	{
	  SQLCODE = SQL_NOT_FOUND;
	}
    }

  if (HAS_RESULT (stmt_type) && curr_result != NULL)
    {

      e = db_query_tuple_count (curr_result);
      if (e < 0)
	{
	  CHECK_DBI (1, return);
	}
      else if (e == 0)
	{
	  SQLCODE = SQL_NOT_FOUND;
	  return;
	}
      else if (e > 1)
	{
	  PUT_UCI_ERR_MULTIPLEOBJECTS ();
	  set_sqlca_err ();
	  return;
	}

      /* locate the first result object */
      error = db_query_first_tuple (curr_result);
      CHECK_DBI (error < 0, return);

      num_curr_result_columns = db_query_column_count (curr_result);
      CHECK_DBI (num_curr_result_columns < 0, return);

      if (num_out_vars >= 0 && num_out_vars < num_curr_result_columns)
	{
	  SET_WARN_VARS_MISMATCH ();	/* two few output vars */
	}

      curr_result_column = 0;
    }
}

/*
 * uci_open_cs() - Execute a statement and create a cursor on the results.
 *   if `stmt' is NULL, it is assumed that the cursor is declared on a prepared
 *   stmt and `stmt_no' indicates the prepared stmt number. otherwise, `stmt'
 *   should points to the stmt string to be executed.
 * return : void
 * cs_no(in) : constitute unique cursor id
 * stmt(in) : statement string if it is not prepared stmt
 * stmt_no(in) : prepared dynamic statement no
 * readonly(in) : true if readonly results.
 */
void
uci_open_cs (int cs_no, const char *stmt, int length, int stmt_no,
	     int readonly)
{
  DB_SESSION *session;
  STATEMENT_ID stmt_id;
  DYNAMIC *dt;
  CUBRID_STMT_TYPE stmt_type;
  int num_markers;
  DB_QUERY_RESULT *tmp_result;
  int n;
  int error;
  bool prm_query_mode_sync = prm_get_query_mode_sync ();

  CHK_SQLCODE ();

  if (get_cursor (cs_no) != NULL)
    {
      /* not closed */
      PUT_UCI_ERR_CURSORSTILLOPEN ();
      set_sqlca_err ();
      return;
    }

  if (stmt == NULL)
    {
      /* prepared statement */
      dt = get_dynamic (stmt_no);
      if (dt == NULL)
	{
	  /* not prepared yet */
	  PUT_UCI_ERR_NOTPREPAREDSTMT ();
	  set_sqlca_err ();
	  return;
	}
      session = dt->session;
      stmt_id = dt->stmt_id;
      stmt_type = dt->stmt_type;
      num_markers = dt->num_markers;
      db_push_values (session, db_Value_table.db_value_top,
		      db_Value_table.db_values);
    }
  else
    {
      /* directly given statement */
      session = db_open_buffer (stmt);
      if (!(session))
	{
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	  CHECK_DBI (error < 0, return);
	}

      db_push_values (session, db_Value_table.db_value_top,
		      db_Value_table.db_values);
      db_include_oid (session, !readonly);
      stmt_id = db_compile_statement (session);
      if (stmt_id < 0)
	{
	  db_close_session (session);
	  CHECK_DBI (true, return);
	}

      num_markers = db_number_of_input_markers (session, stmt_id);
      stmt_type = (CUBRID_STMT_TYPE) db_get_statement_type (session, stmt_id);
    }

  if (stmt_type != CUBRID_STMT_SELECT)
    {
      /* cursor stmt should be SELECT */
      PUT_UCI_ERR_NOTSELECTSTMT ();
      set_sqlca_err ();
      goto error;
    }

  if (db_Value_table.db_value_top < num_markers)
    {
      /* too few input vars */
      PUT_UCI_ERR_TOOFEWHOSTVARS (num_markers);
      set_sqlca_err ();
      goto error;
    }
  else if (db_Value_table.db_value_top > num_markers)	/* too many input vars */
    {
      SET_WARN_VARS_MISMATCH ();
    }

  if (prm_query_mode_sync)
    {
      db_set_session_mode_sync (session);
    }
  else
    {
      db_set_session_mode_async (session);
    }

  /* execute the compiled stmt */
  tmp_result = (DB_QUERY_RESULT *) NULL;
  n = db_execute_and_keep_statement (session, stmt_id, &tmp_result);
  db_push_values (session, 0, NULL);

  if (!is_uci_start_state)	/* nested stmt was executed */
    {
      pop_uci_environment ();
    }

  if (n < 0)
    {
      goto error;
    }

  SQLCA_NUM_AFFECTED_OBJECTS = n;
  if (prm_query_mode_sync)
    {
      if (n == 0)
	{
	  SQLCODE = SQL_NOT_FOUND;
	}
    }

#if defined(UCI_TEMPORARY)
  alloc_cursor (cs_no,
		(stmt == NULL) ? 0 : session,
		(stmt == NULL) ? -1 : stmt_id, tmp_result);
#else
  alloc_cursor (cs_no, tmp_result);	/* create a cursor */
#endif
  return;

error:
  if (stmt != NULL)
    {
      /* static cursor statement open fail */
      db_close_session (session);
    }
  assert (er_errid () != NO_ERROR);
  error = er_errid ();
  CHECK_DBI (error < 0, return);
}

/*
 * uci_fetch_cs() - Advance the current result object to next.
 *   If `num_out_vars' is negative, it does not set warning when # of out
 *   vars mismatches # of output columns.
 * return : void
 * cs_no(in) : constitute unique cursor id
 * num_out_vars(in) : # of output variables
 */
void
uci_fetch_cs (int cs_no, int num_out_vars)
{
  CURSOR *cs;
  int e;

  CHK_SQLCODE ();
  cs = get_cursor (cs_no);
  if (cs == NULL)
    {
      PUT_UCI_ERR_CURSORNOTOPENED ();
      set_sqlca_err ();
      return;
    }

  free_pointers (cs);		/* free previous results if any */

  if (cs->result == NULL)
    {
      SQLCODE = SQL_NOT_FOUND;
      return;
    }

  /* advance to next tuple */
  e = db_query_next_tuple (cs->result);
  CHECK_DBI (e < 0, return);

  if (e == DB_CURSOR_END)
    {
      SQLCODE = SQL_NOT_FOUND;
      return;
    }

  cs->fetched_tuples++;
  SQLCA_NUM_AFFECTED_OBJECTS = cs->fetched_tuples;

  cs->num_columns = db_query_column_count (cs->result);
  CHECK_DBI (cs->num_columns < 0, return);
  cs->curr_column = 0;

  if (num_out_vars >= 0 && num_out_vars < cs->num_columns)
    {
      SET_WARN_VARS_MISMATCH ();	/* too few output vars */
    }
}

/*
 * uci_delete_cs() - Perform delete current object through cursor.
 * return : void
 * cs_no(in) : cursor number
 */
void
uci_delete_cs (int cs_no)
{
  CURSOR *cs;
  int e;
  DB_VALUE oid;

  CHK_SQLCODE ();
  cs = get_cursor (cs_no);
  if (cs == NULL)
    {
      PUT_UCI_ERR_CURSORNOTOPENED ();
      set_sqlca_err ();
      return;
    }

  if (cs->result == NULL)
    {
      SQLCODE = SQL_NOT_FOUND;
      return;
    }

  e = db_query_get_tuple_oid (cs->result, &oid);
  CHECK_DBI (e < 0, return);

  e = db_drop (DB_GET_OBJECT (&oid));
  CHECK_DBI (e < 0, return);
}

/*
 * uci_close_cs() - Remove the specified cursor. if cs_no is negative,
 *    this function closes all the current cursors
 * return : void
 * cs_no(in) : cursor identifier
 */
void
uci_close_cs (int cs_no)
{
  CURSOR *cs;

  CHK_SQLCODE ();

  cs = get_cursor (cs_no);
  if (cs == NULL)
    {
      PUT_UCI_ERR_CURSORNOTOPENED ();
      set_sqlca_err ();
      return;
    }
  SQLCA_NUM_AFFECTED_OBJECTS = cs->fetched_tuples;
  free_cursor (cs_no);
}

/*
 * uci_psh_curr_csr_oid() - Push the oid of the current object pointed to
 *    by the cursor on the stack.
 * return : void
 * cs_no(in) : constitute unique cursor id
 */
void
uci_psh_curr_csr_oid (int cs_no)
{
  CURSOR *cs;
  int e;
  DB_VALUE oid;

  CHK_SQLCODE ();

  cs = get_cursor (cs_no);
  if (cs == NULL)
    {
      PUT_UCI_ERR_CURSORNOTOPENED ();
      set_sqlca_err ();
      return;
    }

  if (cs->result == NULL)
    {
      SQLCODE = SQL_NOT_FOUND;
      return;
    }

  e = db_query_get_tuple_oid (cs->result, &oid);
  CHECK_DBI (e < 0, return);

  check_stack_size ();
  db_Value_table.db_values[db_Value_table.db_value_top++] = oid;
}

/*
 * uci_prepare() - Prepare (compile) a statement. if the statement is already
 *    exist, replace it by the new statement
 * return : void
 * stmt_no(in) : statement identifier
 * stmt(in) : pointer to statement contents
 * length : length of stmt
 */
void
uci_prepare (int stmt_no, const char *stmt, int length)
{
  DYNAMIC *dt;
  int markers;
  CUBRID_STMT_TYPE stmt_type;
  DB_QUERY_TYPE *col_spec;
  DB_SESSION *session;
  STATEMENT_ID stmt_id;
  int error;

  CHK_SQLCODE ();

  col_spec = (DB_QUERY_TYPE *) NULL;
  session = db_open_buffer (stmt);
  if (session == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      CHECK_DBI (error < 0, return);
    }

  stmt_id = db_compile_statement (session);
  if (stmt_id < 0)
    {
      db_close_session (session);
      CHECK_DBI (true, return);
    }

  markers = db_number_of_input_markers (session, stmt_id);
  stmt_type = (CUBRID_STMT_TYPE) db_get_statement_type (session, stmt_id);
  col_spec = db_get_query_type_list (session, stmt_id);

  /* register into dynamic stmt table */
  dt = alloc_dynamic (stmt_no, stmt_type, session, stmt_id, markers, col_spec,
		      (char *) stmt, length);
  if (dt == NULL)
    {
      if (col_spec != NULL)
	{
	  db_query_format_free (col_spec);
	}
      db_close_session (session);
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      CHECK_DBI (error < 0, return);
      return;
    }
}

/*
 * uci_describe() - Get result attribute information and put it via `desc'
 * return : void
 * stmt_no(in) : stmt identifier
 * desc(out) : pointer to descriptor to be filled with the result column info
 */
void
uci_describe (int stmt_no, CUBRIDDA * desc)
{
  DYNAMIC *dt;

  CHK_SQLCODE ();

  if (desc == NULL)
    {
      return;
    }

  dt = get_dynamic (stmt_no);
  if (dt == NULL)
    {
      PUT_UCI_ERR_NOTPREPAREDSTMT ();
      set_sqlca_err ();
      return;
    }

  desc->sqlcmd = dt->stmt_type;

  if (!HAS_RESULT (dt->stmt_type))
    {
      desc->sqldesc = 0;
      return;
    }

  copy_column_spec_to_sqlda (dt->column_spec, desc);
}

/*
 * uci_execute() - Execute the specified stmt. if `num_out_vars' is negative,
 *    it does not set warning when # of out vars mismatches # of output
 *    columns.
 * return : void
 * stmt_no(in) : stmt identifier
 * num_out_vars(in) : # of output variables
 */
void
uci_execute (int stmt_no, int num_out_vars)
{
  DYNAMIC *dt;
  int e;

  CHK_SQLCODE ();

  dt = get_dynamic (stmt_no);
  if (dt == NULL)
    {
      PUT_UCI_ERR_NOTPREPAREDSTMT ();
      set_sqlca_err ();
      return;
    }

  if (db_Value_table.db_value_top < dt->num_markers)
    {
      PUT_UCI_ERR_TOOFEWHOSTVARS (dt->num_markers);
      set_sqlca_err ();
      return;
    }
  else if (db_Value_table.db_value_top > dt->num_markers)
    {
      SET_WARN_VARS_MISMATCH ();
    }

  /* if it brings result, free pointers of previous results */
  if (HAS_RESULT (dt->stmt_type))
    {
      free_pointers ((CURSOR *) NULL);
    }

  /* execute the compiled stmt */
  db_push_values (dt->session, db_Value_table.db_value_top,
		  db_Value_table.db_values);
  e = db_execute_and_keep_statement (dt->session, dt->stmt_id, &curr_result);
  db_push_values (dt->session, 0, NULL);

  if (!is_uci_start_state)	/* nested stmt was executed */
    {
      pop_uci_environment ();
    }

  /*
   * It is evidently ok to return here without closing dt->session, since
   * it will be closed by alloc_dynamic() the next time we need a
   * statement.
   */
  CHECK_DBI (e < 0, return);

  if (AFFECT_OBJECTS (dt->stmt_type))
    {
      SQLCA_NUM_AFFECTED_OBJECTS = e;
      if (e == 0)
	{
	  SQLCODE = SQL_NOT_FOUND;
	}
    }

  if (HAS_RESULT (dt->stmt_type) && curr_result != NULL)
    {

      e = db_query_tuple_count (curr_result);
      CHECK_DBI (e < 0, return);
      if (e > 1)
	{
	  PUT_UCI_ERR_MULTIPLEOBJECTS ();
	  set_sqlca_err ();
	  return;
	}

      /* locate the first result object */
      e = db_query_first_tuple (curr_result);
      CHECK_DBI (e < 0, return);

      num_curr_result_columns = db_query_column_count (curr_result);
      CHECK_DBI (num_curr_result_columns < 0, return);

      if (num_out_vars >= 0 && num_out_vars < num_curr_result_columns)
	{
	  SET_WARN_VARS_MISMATCH ();
	}

      curr_result_column = 0;
    }
}

/*
 * uci_execute_immediate() - Execute statement without using host variables.
 * return : void
 * stmt(in) : pointer to statement contents
 * length(in) : length of stmt
 */
void
uci_execute_immediate (const char *stmt, int length)
{
  DB_SESSION *session;
  STATEMENT_ID stmt_id;
  int markers;
  CUBRID_STMT_TYPE stmt_type;
  int e;
  DB_QUERY_RESULT *dummy_result;

  CHK_SQLCODE ();

  session = db_open_buffer (stmt);
  if (!session)
    {
      assert (er_errid () != NO_ERROR);
      e = er_errid ();
      CHECK_DBI (e < 0, return);
    }

  db_push_values (session, 0, NULL);
  stmt_id = db_compile_statement (session);
  if (stmt_id < 0)
    {
      db_close_session (session);
      CHECK_DBI (true, return);
    }

  markers = db_number_of_input_markers (session, stmt_id);
  stmt_type = (CUBRID_STMT_TYPE) db_get_statement_type (session, stmt_id);

  if (markers > 0)
    {
      /* no marks are allowed */
      PUT_UCI_ERR_NOMARKALLOWED ();
      set_sqlca_err ();
      goto error;
    }

  /* execute the compiled stmt */
  dummy_result = (DB_QUERY_RESULT *) NULL;
  e = db_execute_statement (session, stmt_id, &dummy_result);

  db_close_session (session);

  if (!is_uci_start_state)	/* nested stmt was executed */
    {
      pop_uci_environment ();
    }

  CHECK_DBI (e < 0, return);

  if (AFFECT_OBJECTS (stmt_type))
    {
      SQLCA_NUM_AFFECTED_OBJECTS = e;
      if (e == 0)
	{
	  SQLCODE = SQL_NOT_FOUND;
	}
    }

  if (dummy_result != NULL)	/* throw away the results */
    {
      e = db_query_end (dummy_result);
      CHECK_DBI (e < 0, return);
    }

  return;

error:
  /* drop the stmt */
  db_close_session (session);
  assert (er_errid () != NO_ERROR);
  e = er_errid ();
  CHECK_DBI (e < 0, return);
}

/*
 * uci_object_describe() - Get the attribute information of the given object
 *    and pass it via `desc'.
 * return : `desc' will be filled with the specified attribute information of
 *          the object.
 *
 * obj(in) : pointer to object to be described
 * num_attrs(in) : # of attribute names
 * attr_names(in) : array of attribute names
 * desc(out) : SQLDA where the description will be stored.
 */
void
uci_object_describe (DB_OBJECT * obj, int num_attrs,
		     const char **attr_names, CUBRIDDA * desc)
{
  DB_QUERY_TYPE *object_col_spec = NULL;
  int error;

  CHK_SQLCODE ();

  if (desc == NULL)
    {
      return;
    }

  error = db_object_describe (obj, num_attrs, attr_names, &object_col_spec);
  CHECK_DBI (error < 0, return);
  copy_column_spec_to_sqlda (object_col_spec, desc);
  if (object_col_spec != NULL)
    {
      db_query_format_free (object_col_spec);
    }
}

/*
 * uci_object_fetch() - Get the specified attribute values of the given object.
 *    if `num_out_vars' is negative, it does not set warning when # of out vars
 *    mismatches # of output columns.
 * return : void
 * obj(out) : pointer to the object to be fetched
 * num_attrs(in) : # of attribute names
 * attr_names(in) : array of attribute names
 * num_out_vars(in) : # of output host variables
 */
void
uci_object_fetch (DB_OBJECT * obj, int num_attrs, const char **attr_names,
		  int num_out_vars)
{
  int error;

  CHK_SQLCODE ();
  error = db_object_fetch (obj, num_attrs, attr_names, &curr_result);
  CHECK_DBI (error < 0, return);

  error = db_query_first_tuple (curr_result);
  CHECK_DBI (error < 0, return);

  num_curr_result_columns = db_query_column_count (curr_result);
  CHECK_DBI (num_curr_result_columns < 0, return);
  if (num_out_vars >= 0 && num_out_vars < num_curr_result_columns)
    {
      SET_WARN_VARS_MISMATCH ();	/* two few output vars */
    }

  curr_result_column = 0;
}

/*
 * check_stack_size() - allocates a new data values stack if needed
 * return : void
 *
 * note : For performance, this function never shrinks the table size.
 */
static void
check_stack_size (void)
{
  DB_VALUE *tmp;
  int old_num;

  CHK_SQLCODE ();

  if (db_Value_table.db_value_top < db_Value_table.num_db_values)
    {
      return;
    }

  old_num = db_Value_table.num_db_values;

  if (db_Value_table.num_db_values == 0)
    {
      db_Value_table.num_db_values = DB_VALS_STCK_EXPANSION_UNIT;
      tmp =
	(DB_VALUE *) malloc (sizeof (DB_VALUE) *
			     db_Value_table.num_db_values);
      if (tmp == NULL)
	{
	  PUT_UCI_ERR_NOMOREMEMORY (sizeof (DB_VALUE) *
				    db_Value_table.num_db_values);
	  set_sqlca_err ();
	  return;
	}
    }
  else
    {
      db_Value_table.num_db_values += DB_VALS_STCK_EXPANSION_UNIT;
      tmp =
	(DB_VALUE *) malloc (sizeof (DB_VALUE) *
			     db_Value_table.num_db_values);
      if (tmp == NULL)
	{
	  PUT_UCI_ERR_NOMOREMEMORY (sizeof (DB_VALUE) *
				    db_Value_table.num_db_values);
	  set_sqlca_err ();
	  return;
	}

      (void) memcpy ((void *) tmp, (const void *) db_Value_table.db_values,
		     sizeof (DB_VALUE) * old_num);
      free (db_Value_table.db_values);
    }

  db_Value_table.db_values = tmp;
}

/*
 * free_db_values() - free `db_Value_table.db_values' table
 * return : void
 */
static void
free_db_values (void)
{
  while (db_Value_table.db_value_top)
    {
      db_value_clear (&db_Value_table.
		      db_values[--db_Value_table.db_value_top]);
    }

  /*
   * free() used intentionally here (rather than free_and_init()) to avoid
   * misleading shutdown messages about space leaks.
   */
  if (db_Value_table.num_db_values > 0)
    {
      free (db_Value_table.db_values);
      db_Value_table.num_db_values = 0;
    }
}

/*
 * uci_put_value() - Translates the given C data into a DB_VALUE and stores
 *   it in the internal db_Value_table.db_values stack.  Uses db_value_put
 *    to take advantage of automatic coercion, etc.
 * return : void
 *
 * indicator(in) : pointer to NULL/non-NULL indicator var
 * type(in) : intended DB_TYPE of the input variable
 * precision(in) : intended precision of the input variable
 * scale(in) : intended scale of the input variable
 * ctype(in) : the actual C type of the supplied buffer
 * buf(in) : pointer to the actual C data
 * bufsize(in) : length of the actual C data
 */
void
uci_put_value (DB_INDICATOR * indicator,
	       DB_TYPE type,
	       int precision,
	       int scale, DB_TYPE_C ctype, void *buf, int bufsize)
{
  DB_VALUE *dbval;

  CHK_SQLCODE ();
  check_stack_size ();
  dbval = &db_Value_table.db_values[db_Value_table.db_value_top++];

  if (indicator && IS_NULL_INDICATED (*indicator))
    {
      db_make_null (dbval);
      return;
    }

  if (type == DB_TYPE_DB_VALUE)
    {
      (void) db_value_clone ((DB_VALUE *) buf, dbval);
      return;
    }

  /*
   * (char *) pointers cause special problems, because there is no way
   * for the preprocessor to guess the precision or buffer length.
   * Instead, we simply have it emit 0 for those things and then we
   * compute them at runtime. strlen() isn't wonderful, but it's all
   * we've got. That means that users can't transfer strings with
   * embedded nulls using (char *) host vars.
   */
  if (ctype == DB_TYPE_C_CHAR || ctype == DB_TYPE_C_NCHAR ||
      ctype == DB_TYPE_C_VARCHAR || ctype == DB_TYPE_C_VARNCHAR)
    {
      int tmp_len = strlen ((char *) buf);
      if (precision == 0)
	{
	  precision = (tmp_len ? tmp_len : DB_DEFAULT_PRECISION);
	}
      if (bufsize == 0)
	{
	  bufsize = tmp_len;
	}
      if (bufsize > precision)
	{
	  bufsize = precision;
	}
    }

  if (db_value_domain_init (dbval, type, precision, scale) != NO_ERROR ||
      db_value_put (dbval, ctype, buf, bufsize) != NO_ERROR)
    {
      set_sqlca_err ();
    }
}

/*
 * uci_get_next_column() - Using DBI interface, get the current column value of
 *    the result which is specified by `cs_no'. if `cs_no' is negative,
 *    the `curr_result' is assumed, otherwise, the result which belongs to the
 *    specified cursor will be used.
 * return : error state.
 * cs_no(in) : cursor to which result belongs
 * dbval(out) : pointer to DB_VALUE struct to be filled
 *
 * note : contents of `dbval' will be changed to hold
 *		data from the next column; internal column state of
 *		the appropriate cursor will be advanced.
 *
 * description:	Using DBI interface, get the current column value of
 *		the result which is specified by `cs_no'.
 *		if `cs_no' is negative, the `curr_result' is assumed,
 *		otherwise, the result which belongs to the specified
 *		cursor will be used.
 */
static int
uci_get_next_column (int cs_no, DB_VALUE * dbval)
{
  CURSOR *ct;
  DB_QUERY_RESULT *r;
  int num_cols;
  int current_column;
  int error;

  if (SQLCODE == SQL_NOT_FOUND)	/* check if NOT FOUND is set */
    {
      return 1;
    }

  if (cs_no >= 0)
    {
      ct = get_cursor (cs_no);
      if (ct == NULL)
	{
	  PUT_UCI_ERR_CURSORNOTOPENED ();
	  set_sqlca_err ();
	  return 1;
	}
      r = ct->result;
      num_cols = ct->num_columns;
      current_column = ct->curr_column++;
    }
  else
    {
      ct = (CURSOR *) NULL;
      r = curr_result;
      num_cols = num_curr_result_columns;
      current_column = curr_result_column++;
    }

  if (r == NULL)
    {
      SQLCODE = SQL_NOT_FOUND;
      return 1;
    }
  if (current_column >= num_cols)
    {
      PUT_UCI_ERR_TOOMANYHOSTVARS (num_cols);
      set_sqlca_err ();
      return 1;
    }

  error = db_query_get_tuple_value (r, current_column, dbval);
  CHECK_DBI (error < 0, return 1);

  put_pointer (ct, dbval);

  return 0;
}

/*
 * uci_get_value() - Using DBI interface, get the current column value of
 *    the result which is specified by `cs_no'. If `cs_no' is negative, the
 *    `curr_result' is assumed, otherwise, the result which belongs to the
 *    specified cursor will be used.
 * return : void
 * cs_no(in) - cursor to which result belongs.
 * ind(in) - pointer to indicator variable.
 * buf(out) - pionter to a C buffer.
 * type(in) - type of the host variable in terms of CUBRID.
 * size(in) - # of bytes in host variable
 * xferlen(out) -
 */
void
uci_get_value (int cs_no,
	       DB_INDICATOR * ind,
	       void *buf, DB_TYPE_C type, int size, int *xferlen)
{
  DB_VALUE val;
  int outlen, tmp_xferlen;
  int error;

  CHK_SQLCODE ();		/* check previous error */

  if (uci_get_next_column (cs_no, &val))
    {
      return;
    }

  if (xferlen == NULL)
    {
      xferlen = &tmp_xferlen;
    }

  (void) memset (buf, 0, size);
  error = db_value_get (&val, type, buf, size, xferlen, &outlen);
  CHECK_DBI (error < 0, return);

#if 0
  if (type == DB_TYPE_C_VARCHAR || type == DB_TYPE_C_CHAR)
    {
      if (outlen != -1)
	{
	  *xferlen += 1;
	}
    }
#endif

  if (outlen > 0)
    {
      /*
       * If truncation occurred, set the truncation warning flag in the
       * SQLCA regardless of whether an indicator variable has been
       * supplied.
       */
      SET_WARN_OUTPUT_TRUNC ();
    }

  if (ind)
    {
      /*
       * If no truncation occurred (and there's no NULL value), the
       * indicator should be set to zero.  Truncation will be indicated
       * by an outlen that's greater than the buffer size.  If truncation
       * occurred, the indicator should be set to the original size of
       * the truncated value.
       */
      *ind = outlen;
    }
  else if (outlen == -1)
    {
      if (!(_uci_opt & UCI_OPT_UNSAFE_NULL))
	{
	  PUT_UCI_ERR_NULLINDNEEDED ();
	  set_sqlca_err ();
	}
    }
}

/*
 * uci_get_value_indirect() - Used only from uci_get_descriptor() context,
 *    and only when the user has declined to give us a buffer area.
 *    We go ahead and tell him where our internal buffer area is by storing
 *    its address in *bufp and its size in sizep.
 * return : void
 * cs_no(in) : cursor to which result belongs
 * ind(in) : pointer to indicator variable.
 * bufp(out) : pointer to char pointer to receive result
 * sizep(out) : pointer to int to receive size of result
 */
static void
uci_get_value_indirect (int cs_no,
			DB_INDICATOR * ind, void **bufp, int *sizep)
{
  DB_VALUE val;
  DB_TYPE sqltype;

  CHK_SQLCODE ();

  if (uci_get_next_column (cs_no, &val))
    {
      return;
    }

  if (DB_IS_NULL (&val))
    {
      if (ind)
	{
	  *ind = -1;
	}
      else
	{
	  if (!(_uci_opt & UCI_OPT_UNSAFE_NULL))
	    {
	      PUT_UCI_ERR_NULLINDNEEDED ();
	      set_sqlca_err ();
	    }
	}
      return;
    }
  else if (ind)
    {
      *ind = 0;
    }

  sqltype = DB_VALUE_TYPE (&val);
  switch (sqltype)
    {
    case DB_TYPE_CHAR:
    case DB_TYPE_VARCHAR:
      {
	/*
	 * Why doesn't this cause us storage management problems?
	 * It would if the user tried to hang on to the pointer
	 * for more than a row.
	 */
	*bufp = db_get_string (&val);
	*sizep = db_get_string_size (&val);
      }
      break;
    case DB_TYPE_NCHAR:
    case DB_TYPE_VARNCHAR:
      {
	*bufp = db_get_string (&val);
	*sizep = db_get_string_size (&val);
      }
      break;
    case DB_TYPE_BIT:
    case DB_TYPE_VARBIT:
      {
	*bufp = db_get_string (&val);
	*sizep = db_get_string_size (&val);
      }
      break;
    default:
      {
	PUT_UCI_ERR_INVALIDDATATYPE ();
	set_sqlca_err ();
      }
      break;
    }
}

/*
 * uci_get_db_value() - This function is special case of 'uci_get_value()'
 *    when the host variable type is not DB_TYPE, but DB_VALUE
 *    itself. See uci_get_value().
 * return/side-effects: `*db_value_ptr' will be filled with result *
 * cs_no(in) : cursor identifier
 * db_value_ptr(out) : pointer to user's host variable
 */
void
uci_get_db_value (int cs_no, DB_VALUE * db_value_ptr)
{
  CURSOR *ct;
  DB_QUERY_RESULT *r;
  int num_cols;
  int *curr_col;
  int error;

  CHK_SQLCODE ();

  if (SQLCODE == SQL_NOT_FOUND)
    {
      return;
    }

  if (cs_no >= 0)
    {
      ct = get_cursor (cs_no);
      if (ct == NULL)
	{
	  PUT_UCI_ERR_CURSORNOTOPENED ();
	  set_sqlca_err ();
	  return;
	}
      r = ct->result;
      num_cols = ct->num_columns;
      curr_col = &ct->curr_column;
    }
  else
    {
      ct = (CURSOR *) NULL;
      r = curr_result;
      num_cols = num_curr_result_columns;
      curr_col = &curr_result_column;
    }

  if (r == NULL)
    {
      SQLCODE = SQL_NOT_FOUND;
      return;
    }
  if (*curr_col >= num_cols)
    {
      PUT_UCI_ERR_TOOMANYHOSTVARS (num_cols);
      set_sqlca_err ();
      return;
    }

  error = db_query_get_tuple_value (r, *curr_col, db_value_ptr);
  CHECK_DBI (error < 0, return);

  if (put_pointer (ct, db_value_ptr) == NULL)
    {
      return;
    }

  (*curr_col)++;
}

/*
 * uci_put_descriptor() - Put user variable information in the given
 *    'desc' into 'db_Value_table.db_values' table.
 * return : void
 * desc(in) : pointer to SQLDA
 */
void
uci_put_descriptor (CUBRIDDA * desc)
{
  int i;			/* loop counter */
  int num_values;		/* # of values in SQLDA */
  CUBRIDVAR *var;		/* alias to sqlca.sqlvar */

  num_values = (desc->sqldesc < 0) ? 0 : desc->sqldesc;

  for (i = 0, var = desc->sqlvar; i < num_values; i++, var++)
    {
      uci_put_value (var->sqlind, var->sqltype, var->sqlprec,
		     var->sqlscale, var->sqlctype, var->sqldata, var->sqllen);
    }
}

/*
 * uci_get_descriptor() - get the current result into the given descriptor.
 * return : void
 * cs_no(in) : cursor identifier
 * desc(out) : pointer to SQLDA variable.
 */
void
uci_get_descriptor (int cs_no, CUBRIDDA * desc)
{
  int i;
  CUBRIDVAR *var;

  for (i = 0, var = desc->sqlvar; i < desc->sqldesc; i++, var++)
    {
      if (STRING_C_TYPE (var->sqlctype) &&
	  (var->sqldata == NULL || var->sqllen == 0))
	{
	  uci_get_value_indirect (cs_no,
				  var->sqlind, &var->sqldata, &var->sqllen);
	}
      else
	{
	  uci_get_value (cs_no, var->sqlind, (void *) var->sqldata,
			 var->sqlctype, var->sqllen, NULL);
	}
    }
}

/*
 * alloc_repetitive() - register a new repetitive statement. Since the life
 *              time of a repetitive statement is not given explicitly
 *              by users, esql_cli keeps all such statements until
 *              uci_disconnect() only increasingly.
 * return : newly registered entry pointer or NULL on error
 * no(in) : repetitive stmt identifier
 * session(in) : query compile/execute context
 * stmt_id(in) : compiled plan number
 * stmt_type(in) : type of the statement
 * gadget(in) :
 */
static REPETITIVE *
alloc_repetitive (int no, DB_SESSION * session,
		  STATEMENT_ID stmt_id,
		  CUBRID_STMT_TYPE stmt_type, DB_GADGET * gadget)
{
  int new_num;
  REPETITIVE *rt;
  REPETITIVE *t_repetitive;

  if (num_repetitive_stmts % REPETITIVE_EXPANSION_UNIT == 0)
    {
      /* expansion is needed */
      new_num = num_repetitive_stmts + REPETITIVE_EXPANSION_UNIT;
      t_repetitive =
	(num_repetitive_stmts == 0) ?
	(REPETITIVE *) malloc (sizeof (REPETITIVE) * new_num) :
	(REPETITIVE *) realloc (repetitive_stmts,
				sizeof (REPETITIVE) * new_num);
      if (t_repetitive == NULL)
	{
	  PUT_UCI_ERR_NOMOREMEMORY (sizeof (REPETITIVE) * new_num);
	  set_sqlca_err ();
	  return ((REPETITIVE *) NULL);
	}
      else
	{
	  repetitive_stmts = t_repetitive;
	}
    }

  /* here, the entry indicated by `num_repetitive_stmts' is free */
  rt = repetitive_stmts + num_repetitive_stmts;

  /* fill this entry with given information */
  rt->file_id = curr_file;
  rt->no = no;
  rt->session = session;
  rt->stmt_id = stmt_id;
  rt->stmt_type = stmt_type;
  rt->gadget = gadget;

  /* mark the last searched index (`last_repetitive') so that the immediately
   * succeeding get_repetitive() can find this entry with minimum cost.
   * And increment # of repetitive statements.
   */
  last_repetitive = num_repetitive_stmts++;

  return (rt);
}

/*
 * get_repetitive() - Search repetitive stmt table to locate the specified
 *    stmt and return the entry pointer.
 * return : an entry of repetitive stmt table or NULL on error.
 * no(in) : repetitive stmt identifier
 */
static REPETITIVE *
get_repetitive (int no)
{
  int i;
  REPETITIVE *rt;
  bool found = true;

  if (num_repetitive_stmts <= 0)
    {
      return ((REPETITIVE *) NULL);
    }

  rt = repetitive_stmts;

  i = last_repetitive;
  while (rt[i].no != no || rt[i].file_id != curr_file)
    {
      if (++i == num_repetitive_stmts)
	{
	  i = 0;		/* circular search */
	}
      if (i == last_repetitive)
	{
	  found = false;
	  break;
	}
    }
  if (found)
    {
      last_repetitive = i;
      return (rt + i);
    }
  else
    {
      return ((REPETITIVE *) NULL);
    }
}

/*
 * free_repetitive() - free all repetitive stmt entries.
 * return : void
 */
static void
free_repetitive (void)
{
  if (num_repetitive_stmts > 0)
    {
      int i;
      REPETITIVE *r;
      for (i = 0, r = repetitive_stmts; i < num_repetitive_stmts; i++, r++)
	{
	  db_close_session (r->session);
	  if (r->gadget)
	    {
	      db_gadget_destroy (r->gadget);
	      r->gadget = NULL;
	    }
	}
      free_and_init (repetitive_stmts);
      num_repetitive_stmts = 0;
    }
}

/*
 * alloc_dynamic() - Register a new dynamic statement. Since the life
 *    time of a dynamic statement is not given explicitly by users, esql_cli
 *    keeps all such statements until uci_commit/uci_rollback() only
 *    increasingly.
 * return : newly registered entry pointer or NULL on error
 * stmt_no(in) : dynamic stmt identifier
 * stmt_type(in) : type of the statement
 * session(in) : query compile/execute context
 * stmt_id(in) : compiled plan number
 * num_markers(in) :  # of input positional markers
 * column_spec(in) :  result column spec.
 * stmt(in) : statement buffer.
 * length(in) : length of stmt.
 */
static DYNAMIC *
alloc_dynamic (int stmt_no, CUBRID_STMT_TYPE stmt_type,
	       DB_SESSION * session, STATEMENT_ID stmt_id,
	       int num_markers, DB_QUERY_TYPE * column_spec,
	       char *stmt, int length)
{
  int new_num;
  DYNAMIC *dt;
  DYNAMIC *t_dynamic;

  dt = get_dynamic (stmt_no);
  if (dt != NULL)
    {
      /* already exist, overwrite */
      db_query_format_free (dt->column_spec);
      dt->column_spec = NULL;
      db_close_session (dt->session);
      if (dt->saved_stmt)
	{
	  free ((void *) dt->saved_stmt);
	  dt->saved_stmt = NULL;
	}
    }
  else
    {
      if (num_dynamic_stmts % DYNAMIC_EXPANSION_UNIT == 0)
	{
	  /* expansion is needed */
	  new_num = num_dynamic_stmts + DYNAMIC_EXPANSION_UNIT;
	  t_dynamic =
	    (num_dynamic_stmts == 0) ?
	    (DYNAMIC *) malloc (sizeof (DYNAMIC) * new_num) :
	    (DYNAMIC *) realloc (dynamic_stmts, sizeof (DYNAMIC) * new_num);
	  if (t_dynamic == NULL)
	    {
	      PUT_UCI_ERR_NOMOREMEMORY (sizeof (DYNAMIC) * new_num);
	      set_sqlca_err ();
	      return ((DYNAMIC *) NULL);
	    }
	  else
	    {
	      dynamic_stmts = t_dynamic;
	    }
	}

      /* here, the entry indicated by `num_dynamic_stmts' is free */
      dt = dynamic_stmts + num_dynamic_stmts;
      (void) memset (dt, 0, sizeof (DYNAMIC));

      /* mark the last searched index (`last_dynamic') so that the immediately
       * succeeding get_dynamic() can find this entry with minimum cost.
       * And increment # of dynamic statements.
       */
      last_dynamic = num_dynamic_stmts++;
    }

  /* fill this entry with given information */
  dt->file_id = curr_file;
  dt->stmt_no = stmt_no;
  dt->stmt_type = stmt_type;
  dt->session = session;
  dt->stmt_id = stmt_id;
  dt->num_markers = num_markers;
  dt->column_spec = column_spec;

  dt->saved_stmt = strdup (stmt);
  dt->saved_length = length;
  return (dt);
}

/*
 * get_dynamic() - Search dynamic stmt table to locate the specified
 *   stmt and return the entry pointer.
 * return : An entry of dynamic stmt table or NULL on error.
 * stmt_no(in) : dynamic stmt identifier
 */
static DYNAMIC *
get_dynamic (int stmt_no)
{
  int i;
  DYNAMIC *dt;

  if (num_dynamic_stmts <= 0)
    {
      return ((DYNAMIC *) NULL);
    }

  dt = dynamic_stmts;

  i = last_dynamic;
  while (dt[i].stmt_no != stmt_no || dt[i].file_id != curr_file)
    {
      if (++i == num_dynamic_stmts)
	{
	  i = 0;		/* circular search */
	}
      if (i == last_dynamic)
	{
	  return ((DYNAMIC *) NULL);
	}
    }

  last_dynamic = i;
  return (dt + i);
}

/*
 * free_dynamic() - Free all dynamic stmt entries.
 * return : void
 */
static void
free_dynamic (void)
{
  int i;
  DYNAMIC *dt;

  if (num_dynamic_stmts <= 0)
    {
      return;
    }

  for (i = 0, dt = dynamic_stmts; i < num_dynamic_stmts; i++, dt++)
    {
      if (dt->column_spec != NULL)
	{
	  db_query_format_free (dt->column_spec);
	  dt->column_spec = NULL;
	}
      db_close_session (dt->session);
      if (dt->saved_stmt)
	{
	  free ((void *) dt->saved_stmt);
	  dt->saved_stmt = NULL;
	}
    }
  free_and_init (dynamic_stmts);

  num_dynamic_stmts = 0;
}

/*
 * alloc_cursor() - create a new cursor structure with the given result and
 *    returns the newly built entry.
 * return : newly registered entry pointer or NULL on error
 * no - cursor identifier
 * result - pointer to query result structure
 */
#if defined(UCI_TEMPORARY)
static CURSOR *
alloc_cursor (int no, DB_SESSION * static_session,
	      STATEMENT_ID static_stmt_id, DB_QUERY_RESULT * result)
#else
static CURSOR *
alloc_cursor (int no, DB_QUERY_RESULT * result)
#endif
{
  CURSOR *cs;

  cs = (CURSOR *) malloc (sizeof (CURSOR));
  if (cs == NULL)
    {
      PUT_UCI_ERR_NOMOREMEMORY (sizeof (CURSOR));
      set_sqlca_err ();
      return ((CURSOR *) NULL);
    }

  cs->file_id = curr_file;
  cs->no = no;
#if defined(UCI_TEMPORARY)
  cs->static_session = static_session;
  cs->static_stmt_id = static_stmt_id;
#endif
  cs->num_stmt_ids = 0;
  cs->result = result;
  cs->fetched_tuples = 0;
  cs->num_pointers = 0;

  /* insert this node at the front of cursor list so that
   * rightly succeeding get_cursor() function (OPEN followed by FETCH
   * statement) can find this entry with minimum cost.
   */
  cs->next = cursor_list;
  cursor_list = cs;

  return (cs);
}

/*
 * get_cursor() - Search cursor list to locate the specified cursor and
 *    return the entry pointer.
 * return : An entry of cursor stmt table or NULL on error.
 * no(in) : cursor identifier
 */
static CURSOR *
get_cursor (int no)
{
  CURSOR *cs;

  for (cs = cursor_list; cs != NULL; cs = cs->next)
    {
      if (cs->no == no && cs->file_id == curr_file)
	{
	  break;
	}
    }
  return (cs);
}

/*
 * free_cursor() - free the specified cursor entry. If 'no' is negative,
 *    free all cursors.
 * return : void
 * no(in) - cursor identifier
 *
 * note: this function does not set error code even though the cursor
 *       is not found (i.e., it's assumed already removed).
 */
static void
free_cursor (int no)
{
  CURSOR *cs;
  CURSOR **p_cs;
  int i;

  for (p_cs = &cursor_list; *p_cs != NULL;)
    {

      if ((no < 0) || ((*p_cs)->no == no && (*p_cs)->file_id == curr_file))
	{
	  cs = *p_cs;		/* set alias to speed up */

	  /* free compiled plans */
	  if (cs->num_stmt_ids > 0)
	    {
	      for (i = 0; i < cs->num_stmt_ids; i++)
		{
		  db_close_session (cs->stmt_ids[i].session);
		}
	      free_and_init (cs->stmt_ids);
	    }

	  /* free results */
	  if (cs->result != NULL)
	    {
	      /*
	       * db_query_end() may "fail" if if there was a runtime error in
	       * the statement it is associated with.  That is of no concern
	       * here, and we need to make sure that we continue on to clean up
	       * the various structures.
	       */
	      (void) db_query_end (cs->result);
	    }

#if defined(UCI_TEMPORARY)
	  if (cs->static_session && cs->static_stmt_id >= 0)
	    {
	      db_close_session (cs->static_session);
	    }
#endif

	  /* free pointers */
	  free_pointers (cs);

	  /* remove the entry, but, p_cs is not changed */
	  *p_cs = (*p_cs)->next;
	  free_and_init (cs);

	  /* the specified entry is removed? */
	  if (no >= 0)
	    {
	      return;
	    }
	}
      else
	{
	  p_cs = &(*p_cs)->next;
	}
    }
}

/*
 * put_pointer() - Put an address which should be freed when the result
 *    pointed by the address is not used any more into the pointer table.
 *    'cs' will indicate the cursor with which the result is associated.
 *    If cs' is NULL, it is assumed that the result is associated with
 *    non-cursor statement.
 * return : newly registered entry pointer or NULL on error
 * cs(in) : pointer to cursor
 * value(in) : pointer to the space where the result is stored.
 */
static POINTER *
put_pointer (CURSOR * cs, DB_VALUE * value)
{
  int new_num;
  POINTER **p_p;
  int *p_n;
  POINTER *pt;
  POINTER *t_pointer;

  if (cs == NULL)
    {
      /* non-cursor related */
      p_p = &pointers;
      p_n = &num_pointers;
    }
  else
    {
      p_p = &cs->pointers;
      p_n = &cs->num_pointers;
    }

  if (*p_n % POINTER_EXPANSION_UNIT == 0)
    {
      /* expansion is needed */
      new_num = *p_n + POINTER_EXPANSION_UNIT;
      if (*p_n == 0)
	{
	  t_pointer = (POINTER *) malloc (sizeof (POINTER) * new_num);
	}
      else
	{
	  t_pointer = (POINTER *) realloc (*p_p, sizeof (POINTER) * new_num);
	}
      if (t_pointer == NULL)
	{
	  PUT_UCI_ERR_NOMOREMEMORY (sizeof (POINTER) * new_num);
	  set_sqlca_err ();
	  return ((POINTER *) NULL);
	}
      else
	{
	  *p_p = t_pointer;
	}
    }

  /* here, the entry indicated by `*p_n' is free */
  pt = *p_p + *p_n;

  /* fill this entry with given information */
  pt->value_copy = *value;

  (*p_n)++;

  return (pt);
}

/*
 * free_pointers() - Free all spaces to be alloated to store the result values
 *    which is associated with the given cursor. if `cs' is NULL, free the
 *    spaces which is associated with non-cursor statements.
 * return : void
 * cs : pointer to cursor table entry
 */
static void
free_pointers (CURSOR * cs)
{
  int i;
  POINTER *p;
  int *pn;

  if (cs == NULL)
    {
      /* non-cursor related */
      p = pointers;
      pn = &num_pointers;
    }
  else
    {
      p = cs->pointers;
      pn = &cs->num_pointers;
    }

  if (*pn > 0)
    {
      for (i = 0; i < *pn; i++)
	{
	  db_value_clear (&p[i].value_copy);
	}
      free_and_init (p);
      *pn = 0;
    }
}

/*
 * copy_column_spec_to_sqlda() - Fill SQLDA information from the given result
 *     column information. If `col_spec' is NULL, it is assumed that there is
 *     no result column.
 * return : void
 * col_spec(in) : result column spec
 * desc(out) : SQLDA pointer
 *
 * NOTE: desc->sqlmax should notifies # of sqlvars entries which are
 *	 allocated by users. After this function, desc->sqldesc will
 *	 have the # of sqlvars which is described.
 */
static void
copy_column_spec_to_sqlda (DB_QUERY_TYPE * col_spec, CUBRIDDA * desc)
{
  int i;
  DB_DOMAIN *domain;

  for (i = 0; col_spec != NULL;
       col_spec = db_query_format_next (col_spec), i++)
    {

      if (i >= desc->sqlmax)	/* not suffcient sqlvars */
	{
	  continue;
	}

      domain = db_query_format_domain (col_spec);

      desc->sqlvar[i].sqltype = TP_DOMAIN_TYPE (domain);
      desc->sqlvar[i].sqlprec = db_domain_precision (domain);
      desc->sqlvar[i].sqlscale = db_domain_scale (domain);
      desc->sqlvar[i].sqllen = db_query_format_size (col_spec);
      desc->sqlvar[i].sqlname = db_query_format_name (col_spec);

      /* for undefined type, we'd better tell users to use DB_VALUE
       * itself instead of just DB_TYPE_NULL.
       */
      if (desc->sqlvar[i].sqltype == DB_TYPE_NULL)
	{
	  desc->sqlvar[i].sqltype = DB_TYPE_DB_VALUE;
	  desc->sqlvar[i].sqllen = sizeof (DB_VALUE);
	}
    }
  desc->sqldesc = i;
}

/*
 * set_sqlca_err() - get the current error information from CUBRID. And
 *   copy them into SQLCA.
 * return : void
 */
static void
set_sqlca_err (void)
{
  const char *msg;

  if (SQLCODE != 0)
    {
      return;
    }

  assert (er_errid () != NO_ERROR);
  SQLCODE = er_errid ();
  msg = er_msg ();
  strncpy (SQLERRMC, (msg == NULL) ? "" : msg, sizeof (SQLERRMC) - 1);
  SQLERRMC[sizeof (SQLERRMC) - 1] = '\0';
  SQLERRML = strlen (SQLERRMC);
}

/*
 * push_uci_environment() - push the stmt-sensitive global variables into
 *    the top of stack. In order to see what variables are pushed, see the
 *    documentation in this file.
 * return : NO_ERROR or error code
 */
static int
push_uci_environment (void)
{
  UCI_ENV_STACK_ENTRY *t;
  int new_size;

  if (uci_env_stack_top + 1 == num_uci_env_stack_entries)
    {
      /* expansion is needed */
      new_size = sizeof (UCI_ENV_STACK_ENTRY) *
	(num_uci_env_stack_entries + UCI_ENV_STACK_EXPANSION_UNIT);
      t = (UCI_ENV_STACK_ENTRY *) ((num_uci_env_stack_entries == 0)
				   ? malloc (new_size)
				   : realloc (uci_env_stack, new_size));
      if (t == NULL)
	{
	  PUT_UCI_ERR_NOMOREMEMORY (new_size);
	  set_sqlca_err ();
	  return (ER_OUT_OF_VIRTUAL_MEMORY);
	}
      uci_env_stack = t;
      num_uci_env_stack_entries += UCI_ENV_STACK_EXPANSION_UNIT;
    }

  uci_env_stack_top++;
  t = uci_env_stack + uci_env_stack_top;

  /* save stmt-sentisive variables */

  /* special treatement of SQLCA fields. For many field except warning
   * do not have to be saved because they are all initial status before
   * execute a statement. Therefore, they'll not be saved, but will be
   * re-initialized at the pop operation time.
   */
  memmove (&t->saved_sqlca_sqlwarn, &sqlca.sqlwarn, sizeof (sqlca.sqlwarn));
  t->saved_db_values = db_Value_table.db_values;
  t->saved_num_db_values = db_Value_table.num_db_values;
  t->saved_db_value_top = db_Value_table.db_value_top;
  t->saved_curr_file = curr_file;
  t->saved_curr_filename = curr_filename;
  t->saved_curr_lineno = curr_lineno;

  /* special consideration of db_Value_table.db_values array - since uci
   * functions would re-use the array for performance, we should reset
   * the db_Value_table.num_db_values to allocate new table.
   */
  db_Value_table.num_db_values = 0;
  db_Value_table.db_value_top = 0;

  return (NO_ERROR);
}

/*
 * pop_uci_environment() - pop the variables from the top of stack. In order
 *    to see what variables are popped, see the documentation in this file.
 *    the global variables will be changed to the original value.
 * return : return void.
 */
static void
pop_uci_environment (void)
{
  UCI_ENV_STACK_ENTRY *t;

  t = uci_env_stack + uci_env_stack_top;

  /* special consideration of db_Value_table.db_values array - since it will
   * not be accessed any more, it should be freed.
   */
  free_db_values ();

  memmove (&sqlca.sqlwarn, &t->saved_sqlca_sqlwarn, sizeof (sqlca.sqlwarn));
  db_Value_table.db_values = (DB_VALUE *) t->saved_db_values;
  db_Value_table.num_db_values = t->saved_num_db_values;
  db_Value_table.db_value_top = t->saved_db_value_top;
  curr_file = t->saved_curr_file;
  curr_filename = t->saved_curr_filename;
  curr_lineno = t->saved_curr_lineno;

  /* the followings were not saved because we already know the previous
   * status (initially all zeros).
   */
  SQLCODE = 0;
  if (SQLERRML > 0)
    {
      SQLERRML = 0;
      SQLERRMC[0] = '\0';
    }
  SQLCA_NUM_AFFECTED_OBJECTS = 0L;

  uci_env_stack_top--;
}

/*
 * drop_uci_env_stack() - Free the statement environment stack.
 * return : void.
 */
static void
drop_uci_env_stack (void)
{
  if (num_uci_env_stack_entries > 0)
    {
      free_and_init (uci_env_stack);
      num_uci_env_stack_entries = 0;
      uci_env_stack_top = -1;
    }
}

/*
 * clean_up() - take care of clean-up esql_cli stuffs if abmornal things happen.
 * return : return void.
 *
 * NOTE: currently, this function takes care of only the followings
 *		ER_LK_UNILATERALLY_ABORTED
 *		ER_TM_SERVER_DOWN_UNILATERALLY_ABORTED
 */
static void
clean_up (void)
{
  switch (er_errid ())
    {

    case ER_LK_UNILATERALLY_ABORTED:
      {
	/* transaction aborted unilaterally, but, server is running */
	free_cursor (-1);
	free_pointers ((CURSOR *) NULL);
	free_dynamic ();
      }
      break;

    case ER_TM_SERVER_DOWN_UNILATERALLY_ABORTED:
      {
	/* server down */
	free_cursor (-1);
	free_pointers ((CURSOR *) NULL);
	free_dynamic ();
	free_repetitive ();
	free_db_values ();
	drop_uci_env_stack ();
	connected = false;
      }
      break;

    default:
      break;
    }
}

#if !defined(UCI_TEMPORARY)
#error "We should move READONLY_SELECT flag to db_execute() \
for reduce overhead of cursor on dynamic stmts."
#endif

#if !defined(UCI_TEMPORARY)
#error "We should provide another function/language to get \
Full error message"
#endif
