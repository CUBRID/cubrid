/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * db_vdb.c - Stubs for SQLX interface functions.
 *
 */
#ident "$Id$"

#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "db.h"
#include "dbi.h"
#include "db_query.h"
#include "error_manager.h"
#include "chartype.h"
#include "system_parameter.h"
#include "environment_variable.h"
#include "memory_manager_2.h"
#include "parser.h"
#include "msgexec.h"
#include "object_domain.h"
#include "schema_manager_3.h"
#include "view_transform_1.h"
#include "execute_statement_11.h"
#include "xasl_generation_2.h"	/* TODO: remove */
#include "locator_cl.h"
#include "server.h"
#include "query_manager.h"
#include "api_compat.h"
#include "network_interface_sky.h"

#define BUF_SIZE 1024

enum
{
  StatementInitialStage = 0,
  StatementCompiledStage,
  StatementPreparedStage,
  StatementExecutedStage,
};

#if 0
struct db_session
{
  char *stage;			/* vector of statements' stage */
  char include_oid;		/* NO_OIDS, ROW_OIDS           */
  int dimension;		/* Number of statements        */
  int stmt_ndx;			/* 0 <= stmt_ndx < DIM(statements)     */
  /* statements[stmt_ndx] will be processed by
     next call to db_compile_statement.       */
  int line_offset;		/* amount to add to parsers line number */
  int column_offset;		/* amount to add to parsers column number   */

  PARSER_CONTEXT *parser;	/* handle to parser context structure */
  DB_QUERY_TYPE **type_list;	/* for storing "nice" column headings */
  /* type_list[stmt_ndx] is itself an array.  */
  PT_NODE **statements;		/* statements to be processed in this session */
};
#endif

static int get_dimension_of (PT_NODE ** array);
static DB_SESSION *db_open_local (void);
static int db_execute_and_keep_statement_local (DB_SESSION * session,
						int stmt_ndx,
						DB_QUERY_RESULT ** result);
static DB_OBJLIST *db_get_all_chosen_classes (int (*p) (MOBJ o));
static int is_vclass_object (MOBJ class_);
static int is_vclass_object_on_ldb (MOBJ class_);
static char *get_reasonable_predicate (DB_ATTRIBUTE * att);

/*
 * get_dimemsion_of() - returns the number of elements of a null-terminated
 *   pointer array
 * returns  : number of elements of array
 * array (in): a null-terminated array of pointers
 */
static int
get_dimension_of (PT_NODE ** array)
{
  int rank = 0;

  if (!array)
    {
      return rank;
    }

  while (*array++)
    {
      rank++;
    }

  return rank;
}

/*
 * db_statement_count() - This funciton returns the number of statements
 *    in a session.
 * return : number of statements in the session
 * session(in): compiled session
 */
int
db_statement_count (DB_SESSION * session)
{
  int retval;

  if (session == NULL)
    {
      return 0;
    }

  retval = get_dimension_of (session->statements);

  return (retval);
}

/*
 * db_open_local() - Starts a new SQL empty compile session
 * returns : new DB_SESSION
 */
static DB_SESSION *
db_open_local (void)
{
  DB_SESSION *session = NULL;

  session = (DB_SESSION *) malloc (sizeof (DB_SESSION));
  if (session == NULL)
    {
      return NULL;
    }

  session->parser = parser_create_parser ();
  if (session->parser == NULL)
    {
      free_and_init (session);
      return NULL;
    }

  session->stage = NULL;
  session->dimension = 0;
  session->stmt_ndx = 0;
  session->type_list = NULL;
  session->line_offset = 0;
  session->include_oid = DB_NO_OIDS;
  session->statements = NULL;

  return session;
}

/*
 * db_open_buffer() - Please refer to the db_open_buffer() function
 * returns  : new DB_SESSION
 * buffer(in): contains query text to be compiled
 */
DB_SESSION *
db_open_buffer_local (const char *buffer)
{
  DB_SESSION *session;

  CHECK_1ARG_NULL (buffer);

  session = db_open_local ();

  if (session)
    {
      session->statements = parser_parse_string (session->parser, buffer);
      if (session->statements)
	{
	  session->dimension = get_dimension_of (session->statements);
	}
    }

  return session;
}

/*
 * db_open_buffer() - Starts a new SQL compile session on a nul terminated
 *    string
 * return:new DB_SESSION
 * buffer(in) : contains query text to be compiled
 */
DB_SESSION *
db_open_buffer (const char *buffer)
{
  DB_SESSION *session;

  CHECK_1ARG_NULL (buffer);

  session = db_open_buffer_local (buffer);

  return session;
}


/*
 * db_open_file() - Starts a new SQL compile session on a query file
 * returns  : new DB_SESSION
 * file(in): contains query text to be compiled
 */
DB_SESSION *
db_open_file (FILE * file)
{
  DB_SESSION *session;

  session = db_open_local ();

  if (session)
    {
      session->statements = parser_parse_file (session->parser, file);
      if (session->statements)
	{
	  session->dimension = get_dimension_of (session->statements);
	}
    }

  return session;
}

/*
 * db_make_session_for_one_statement_execution() -
 * return:
 * file(in) :
 */
DB_SESSION *
db_make_session_for_one_statement_execution (FILE * file)
{
  DB_SESSION *session;

  session = db_open_local ();

  if (session)
    {
      pt_init_one_statement_parser (session->parser, file);
      parse_one_statement (0);
    }

  return session;
}

/*
 * db_parse_one_statement() -
 * return:
 * session(in) :
 */
int
db_parse_one_statement (DB_SESSION * session)
{
  if (session->dimension > 0)
    {
      /* check if this statement is skipped */
      if (session->type_list)
	{
	  db_free_query_format (session->type_list[0]);
	}
      if (session->statements[0])
	{
	  parser_free_tree (session->parser, session->statements[0]);
	  session->statements[0] = NULL;
	}
      session->stage[0] = StatementInitialStage;
      session->dimension = 0;
      session->stmt_ndx = 0;

      session->parser->stack_top = 0;
      if (session->stage)
	{
	  session->stage[0] = StatementInitialStage;
	}
    }

  if (parse_one_statement (1) == 0 &&
      session->parser->error_msgs == NULL &&
      session->parser->stack_top > 0 && session->parser->node_stack != NULL)
    {
      session->parser->statements =
	(PT_NODE **) parser_alloc (session->parser, 2 * sizeof (PT_NODE *));
      session->parser->statements[0] = session->parser->node_stack[0];
      session->parser->statements[1] = NULL;

      session->statements = session->parser->statements;
      session->dimension = get_dimension_of (session->statements);

      return session->dimension;
    }
  else
    {
      session->parser->statements = NULL;
      return -1;
    }
}

/*
 * db_get_parser_line_col() -
 * return:
 * session(in) :
 * line(out) :
 * col(out) :
 */
int
db_get_parser_line_col (DB_SESSION * session, int *line, int *col)
{
  if (line)
    {
      *line = session->parser->line;
    }
  if (col)
    {
      *col = session->parser->column;
    }
  return 0;
}

/*
 * db_open_file_name() - This functions allocates and initializes a session and
 *    parses the named file. Similar to db_open_file() except that it takes a
 *    name rather than a file handle.
 * return : new session
 * name(in): file name
 */
DB_SESSION *
db_open_file_name (const char *name)
{
  FILE *fp;
  DB_SESSION *session;

  session = db_open_local ();

  if (session)
    {
      fp = fopen (name, "r");
      if (fp != NULL)
	{
	  session->statements = parser_parse_file (session->parser, fp);
	  fclose (fp);
	}
      if (session->statements)
	{
	  session->dimension = get_dimension_of (session->statements);
	}
    }

  return session;
}


/*
 * db_compile_statement_local() -
 * return:
 * session(in) :
 */
int
db_compile_statement_local (DB_SESSION * session)
{
  PARSER_CONTEXT *parser;
  int stmt_ndx;
  PT_NODE *statement;
  DB_QUERY_TYPE *qtype;
  int cmd_type;
  int err;

  DB_VALUE *hv;
  int i;
  SERVER_INFO server_info;
  static long seed = 0;

  /* obvious error checking - invalid parameter */
  if (!session || !session->parser)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_IT_INVALID_SESSION, 0);
      return er_errid ();
    }
  /* no statement was given in the session */
  if (session->dimension == 0 || !session->statements)
    {
      /* if the parser already has something wrong - syntax error */
      if (pt_has_error (session->parser))
	{
	  pt_report_to_ersys (session->parser, PT_SYNTAX);
	  return er_errid ();
	}

      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_IT_EMPTY_STATEMENT, 0);
      return er_errid ();
    }
  /* check the end of statements */
  if (session->stmt_ndx == session->dimension)
    {
      /* return 0 if all statement were compiled */
      return 0;
    }

  /* allocate memory for session->type_list and session->stage
     if not allocated */
  if (!session->type_list && !session->stage)
    {
      size_t size = session->dimension * sizeof (DB_QUERY_TYPE *)
	+ session->dimension * sizeof (char);
      void *p = malloc (size);
      if (!p)
	{
	  return er_errid ();
	}
      (void) memset (p, 0, size);
      session->type_list = (DB_QUERY_TYPE **) p;
      session->stage =
	(char *) p + session->dimension * sizeof (DB_QUERY_TYPE *);
    }

  /*
   * Compilation Stage
   */

  /* the statements in this session have been parsed without error
     and it is time to compile the next statement */
  parser = session->parser;
  stmt_ndx = session->stmt_ndx++;
  statement = session->statements[stmt_ndx];

  /* check if the statement is already processed */
  if (session->stage[stmt_ndx] >= StatementPreparedStage)
    {
      return stmt_ndx;
    }

  /* forget about any previous parsing errors, if any */
  pt_reset_error (parser);

  /* get type list describing the output columns titles of the given query */
  cmd_type = pt_node_to_cmd_type (statement);
  qtype = NULL;
  if (cmd_type == SQLX_CMD_SELECT)
    {
      qtype = pt_get_titles (parser, statement);
      /* to prevent a memory leak, register the query type list to session */
      session->type_list[stmt_ndx] = qtype;
    }

  /* prefetch and lock classes to avoid deadlock */
  (void) pt_class_pre_fetch (parser, statement);
  if (pt_has_error (parser))
    {
      pt_report_to_ersys_with_statement (parser, PT_SYNTAX, statement);
      return er_errid ();
    }

  /* get sys_date, sys_time, sys_timestamp values from the server */
  server_info.info_bits = 0;	/* init */
  if (statement->si_timestamp)
    {
      server_info.info_bits |= SI_SYS_TIMESTAMP;
      server_info.value[0] = &parser->sys_timestamp;
    }
  if (statement->si_tran_id)
    {
      server_info.info_bits |= SI_LOCAL_TRANSACTION_ID;
      server_info.value[1] = &parser->local_transaction_id;
    }
  /* request to the server */
  if (server_info.info_bits)
    {
      (void) qp_get_server_info (&server_info);
    }

  if (seed == 0)
    {
      srand48 (seed = (long) time (NULL));
    }

  /* do semantic check for the statement */
  statement = pt_compile (parser, statement);

  if (!statement || pt_has_error (parser))
    {
      pt_report_to_ersys_with_statement (parser, PT_SEMANTIC, statement);
      return er_errid ();
    }

  /* get type list describing the output columns titles of the given query */
  if (cmd_type == SQLX_CMD_SELECT)
    {
      /* for a select-type query of the form:
         SELECT * FROM class c
         we store into type_list the nice column headers:
         c.attr1    attr2    c.attr3
         before they get fully resolved by mq_translate(). */
      if (!qtype)
	{
	  qtype = pt_get_titles (parser, statement);
	  /* to prevent a memory leak,
	     register the query type list to session */
	  session->type_list[stmt_ndx] = qtype;
	}
      if (qtype)
	{
	  /* NOTE, this is here on purpose. If something is busting
	     because it tries to continue corresponding this type
	     information and the list file columns after having jacked
	     with the list file, by for example adding a hidden OID
	     column, fix the something else.
	     This needs to give the results as user views the
	     querie, ie related to the original text. It may guess
	     wrong about attribute/column updatability.
	     Thats what they asked for. */
	  qtype = pt_fillin_type_size (parser, statement, qtype, DB_NO_OIDS);
	}
    }

  /* reset auto parameterized variables */
  for (i = 0, hv = parser->host_variables + parser->host_var_count;
       i < parser->auto_param_count; i++, hv++)
    db_value_clear (hv);
  parser->auto_param_count = 0;
  /* translate views or virtual classes into base classes */
  statement = mq_translate (parser, statement);
  if (!statement || pt_has_error (parser))
    {
      pt_report_to_ersys_with_statement (parser, PT_SYNTAX, statement);
      return er_errid ();
    }

  /* prefetch and lock translated real classes to avoid deadlock */
  (void) pt_class_pre_fetch (parser, statement);
  if (pt_has_error (parser))
    {
      pt_report_to_ersys_with_statement (parser, PT_SYNTAX, statement);
      return er_errid ();
    }

  /* validate include_oid setting in the session */
  if (session->include_oid)
    {
      if (mq_updatable (parser, statement))
	{
	  if (session->include_oid == DB_ROW_OIDS)
	    {
	      (void) pt_add_row_oid (parser, statement);
	    }
	}
      else
	{
	  /* disallow OID column for non-updatable query */
	  session->include_oid = DB_NO_OIDS;
	}
    }

  /* so now, the statement is compiled */
  session->statements[stmt_ndx] = statement;
  session->stage[stmt_ndx] = StatementCompiledStage;


  /*
   * Preparation Stage
   */

  statement->xasl_id = NULL;	/* bullet proofing */

  /* New interface of do_prepare_statement()/do_execute_statment() is used
     only when the XASL cache is enabled. If it is disabled, old interface
     of do_statement() will be used instead. do_statement() makes a XASL
     everytime rather than using XASL cache. Also, it can be executed in
     the server without touching the XASL cache by calling
     query_prepare_and_execute(). */
  if (PRM_XASL_MAX_PLAN_CACHE_ENTRIES >= 0 && statement->cannot_prepare == 0)
    {

      /* now, prepare the statement by calling do_prepare_statement() */
      err = do_prepare_statement (parser, statement);
#if 0
      if (err == ER_QPROC_INVALID_XASLNODE)
	{
	  /* There is a kind of problem in the XASL cache.
	     It is possible when the cache entry was deleted by the other.
	     In this case, retry to prepare once more (generate and stored
	     the XASL again). */
	  statement->xasl_id = NULL;
	  er_clear ();
	  /* execute the statement by calling do_statement() */
	  err = do_prepare_statement (parser, statement);
	}
#endif
      if (err < 0)
	{
	  if (pt_has_error (parser))
	    {
	      pt_report_to_ersys_with_statement (parser, PT_SEMANTIC,
						 statement);
	      return er_errid ();
	    }
	  return err;
	}
    }

  /* so now, the statement is prepared */
  session->stage[stmt_ndx] = StatementPreparedStage;

  return stmt_ndx + 1;
}

/*
 * db_compile_statement() - This function compiles the next statement in the
 *    session. The first compilation reports any syntax errors that occurred
 *    in the entire session.
 * return: an integer that is the relative statement ID of the next statement
 *    within the session, with the first statement being statement number 1.
 *    If there are no more statements in the session (end of statements), 0 is
 *    returned. If an error occurs, the return code is negative.
 * session(in) : session handle
 */
int
db_compile_statement (DB_SESSION * session)
{
  int statement_id;

  er_clear ();

  CHECK_CONNECT_MINUSONE ();

  statement_id = db_compile_statement_local (session);

  return statement_id;
}

/*
 * db_set_client_cache_time() -
 * return:
 * session(in) :
 * stmt_ndx(in) :
 * cache_time(in) :
 */
int
db_set_client_cache_time (DB_SESSION * session, int stmt_ndx,
			  CACHE_TIME * cache_time)
{
  PT_NODE *statement;
  int result = NO_ERROR;

  if (!session
      || !session->parser
      || session->dimension == 0
      || !session->statements
      || stmt_ndx < 1
      || stmt_ndx > session->dimension
      || !(statement = session->statements[stmt_ndx - 1]))
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS,
	      0);
      result = er_errid ();
    }
  else
    {
      if (cache_time)
	{
	  statement->cache_time = *cache_time;
	  statement->clt_cache_check = 1;
	}
    }

  return result;
}

/*
 * db_get_jdbccachehint() -
 * return:
 * session(in) :
 * stmt_ndx(in) :
 * life_time(out) :
 */
bool
db_get_jdbccachehint (DB_SESSION * session, int stmt_ndx, int *life_time)
{
  PT_NODE *statement;

  /* obvious error checking - invalid parameter */
  if (!session
      || !session->parser
      || session->dimension == 0
      || !session->statements
      || stmt_ndx < 1
      || stmt_ndx > session->dimension
      || !(statement = session->statements[stmt_ndx - 1]))
    {
      return false;
    }

  if (statement->info.query.q.select.hint & PT_HINT_JDBC_CACHE)
    {
      if (life_time != NULL &&
	  statement->info.query.q.select.jdbc_life_time->info.name.original !=
	  NULL)
	{
	  *life_time =
	    atoi (statement->info.query.q.select.jdbc_life_time->info.name.
		  original);
	}
      return true;
    }

  return false;
}

/*
 * db_get_errors() - This function returns a list of errors that occurred during
 *    compilation. NULL is returned if no errors occurred.
 * returns : compilation error list
 * session(in): session handle
 *
 * note : A call to the db_get_next_error() function can be used to examine
 *    each error. You do not free this list of errors.
 */
DB_SESSION_ERROR *
db_get_errors (DB_SESSION * session)
{
  DB_SESSION_ERROR *result;

  if (!session || !session->parser)
    {
      result = NULL;
    }
  else
    {
      result = pt_get_errors (session->parser);
    }

  return result;
}

/*
 * db_get_next_error() - This function returns the line and column number of
 *    the next error that was passed in the compilation error list.
 * return : next error in compilation error list
 * errors (in) : DB_SESSION_ERROR iterator
 * line(out): source line number of error
 * col(out): source column number of error
 *
 * note : Do not free this list of errors.
 */
DB_SESSION_ERROR *db_get_next_error
  (DB_SESSION_ERROR * errors, int *line, int *col)
{
  DB_SESSION_ERROR *result;
  int stmt_no;
  const char *e_msg = NULL;

  if (!errors)
    {
      return NULL;
    }

  result = pt_get_next_error (errors, &stmt_no, line, col, &e_msg);
  if (e_msg)
    {
      er_set (ER_SYNTAX_ERROR_SEVERITY, ARG_FILE_LINE, ER_PT_ERROR, 1, e_msg);
    }

  return result;
}

/*
 * db_get_warnings: This function returns a list of warnings that occurred
 *    during the compilation. NULL is returned if no warnings are found.
 *    A non-NULL return value indicates that one or more warnings occurred
 *    during compilation.
 * returns: DB_SESSION_WARNING iterator if there were any compilation warnings,
 *          NULL, otherwise.
 * session(in): session handle
 *
 * note : Do not free this list of warnings.
 */
DB_SESSION_WARNING *
db_get_warnings (DB_SESSION * session)
{
  DB_SESSION_WARNING *result;

  if (!session || !session->parser)
    {
      result = NULL;
    }
  else
    {
      result = pt_get_warnings (session->parser);
    }

  return result;
}

/*
 * db_get_next_warning: This function returns the line and column number of the
 *    next warning that was passed in the compilation warning list.
 * returns: DB_SESSION_WARNING iterator if there are more compilation warnings
 *          NULL, otherwise.
 * warnings(in) : DB_SESSION_WARNING iterator
 * line(out): source line number of warning
 * col(out): source column number of warning
 *
 * note : Do not free this list of warnings.
 */
DB_SESSION_WARNING *db_get_next_warning
  (DB_SESSION_WARNING * warnings, int *line, int *col)
{
  DB_SESSION_WARNING *result;
  int stmt_no;
  const char *e_msg = NULL;

  if (!warnings)
    {
      return NULL;
    }

  result = pt_get_next_error (warnings, &stmt_no, line, col, &e_msg);
  if (e_msg)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_PT_ERROR, 1, e_msg);
    }

  return result;
}

/*
 * db_get_line_col_of_1st_error() - get the source line & column of first error
 * returns: 1 if there were any query compilation errors, 0, otherwise.
 * session(in) : contains the SQL query that has just been compiled
 * linecol(out): the source line & column of first error if any
 *
 * note : DO NOT USE THIS FUNCTION.  USE db_get_errors & db_get_next_error
 *	  instead.  This function is provided for the sole purpose of
 *	  facilitating conversion of old code.
 */
int
db_get_line_col_of_1st_error (DB_SESSION * session, DB_QUERY_ERROR * linecol)
{
  if (!session || !session->parser || !pt_has_error (session->parser))
    {
      if (linecol)
	linecol->err_lineno = linecol->err_posno = 0;
      return 0;
    }
  else
    {
      PT_NODE *errors;
      int stmt_no;
      const char *msg;
      errors = pt_get_errors (session->parser);
      if (linecol)
	pt_get_next_error (errors, &stmt_no, &linecol->err_lineno,
			   &linecol->err_posno, &msg);
      return 1;
    }
}

/*
 * db_number_of_input_markers() -
 * return : number of host variable input markers in statement
 * session(in): compilation session
 * stmt(in): statement number of compiled statement
 */
int
db_number_of_input_markers (DB_SESSION * session, int stmt)
{
  PARSER_CONTEXT *parser;
  PT_NODE *statement;
  int result = 0;

  if (!session
      || !(parser = session->parser)
      || !session->statements
      || stmt < 1
      || stmt > session->dimension
      || !(statement = session->statements[stmt - 1]))
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS,
	      0);
      result = er_errid ();
    }
  else
    {
      result = parser->host_var_count;
    }

  return result;
}

/*
 * db_number_of_output_markers() -
 * return : number of host variable output markers in statement
 * session(in): compilation session
 * stmt(in): statement number of compiled statement
 */
int
db_number_of_output_markers (DB_SESSION * session, int stmt)
{
  PARSER_CONTEXT *parser;
  PT_NODE *statement;
  int result = 0;

  if (!session
      || !(parser = session->parser)
      || !session->statements
      || stmt < 1
      || stmt > session->dimension
      || !(statement = session->statements[stmt - 1]))
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS,
	      0);
      result = er_errid ();
    }
  else
    {
      (void) parser_walk_tree (parser, statement, pt_count_output_markers,
			       &result, NULL, NULL);
    }

  return result;
}

/*
 * db_get_input_markers() -
 * return : host variable input markers list in statement
 * session(in): compilation session
 * stmt(in): statement number of compiled statement
 */
DB_MARKER *
db_get_input_markers (DB_SESSION * session, int stmt)
{
  PARSER_CONTEXT *parser;
  PT_NODE *statement;
  DB_MARKER *result = NULL;
  PT_HOST_VARS *hv;

  if (!session
      || !(parser = session->parser)
      || !session->statements
      || stmt < 1
      || stmt > session->dimension
      || !(statement = session->statements[stmt - 1])
      || pt_has_error (parser))
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS,
	      0);
      result = NULL;
    }
  else
    {
      hv = pt_host_info (parser, statement);
      result = pt_get_input_host_vars (hv);
      pt_free_host_info (hv);
    }

  return result;
}

/*
 * db_get_output_markers() -
 * return : host variable output markers list in statement
 * session(in): compilation session
 * stmt(in): statement number of compiled statement
 */
DB_MARKER *
db_get_output_markers (DB_SESSION * session, int stmt)
{
  PARSER_CONTEXT *parser;
  PT_NODE *statement;
  DB_MARKER *result = NULL;
  PT_HOST_VARS *hv;

  if (!session
      || !(parser = session->parser)
      || !session->statements
      || stmt < 1
      || stmt > session->dimension
      || !(statement = session->statements[stmt - 1])
      || pt_has_error (parser))
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS,
	      0);
      result = NULL;
    }
  else
    {
      hv = pt_host_info (parser, statement);
      result = pt_get_output_host_vars (hv);
      pt_free_host_info (hv);
    }

  return result;
}

/*
 * db_marker_next: This function returns the next marker in the list
 * return : the next host variable (input/output) marker in the list or NULL
 * marker(in): DB_MARKER
 */
DB_MARKER *
db_marker_next (DB_MARKER * marker)
{
  DB_MARKER *result = NULL;

  if (marker)
    {
      result = pt_node_next (marker);
    }

  return result;
}

/*
 * db_marker_index() - This function return the index of an host variable
 *    (input/output) marker
 * return : index of an marker
 * marker(in): DB_MARKER
 */
int
db_marker_index (DB_MARKER * marker)
{
  int result = -1;

  if (marker)
    {
      result = pt_host_var_index (marker);
    }

  return result;
}

/*
 * db_marker_domain() - This function returns the domain of an host variable
 *    (input/output) marker
 * return : domain of marker
 * marker(in): DB_MARKER
 */
DB_DOMAIN *
db_marker_domain (DB_MARKER * marker)
{
  DB_DOMAIN *result = NULL;

  if (marker)
    {
      result = pt_node_to_db_domain (NULL, marker, NULL);
    }
  /* it is safet to call pt_node_to_db_domain() without parser */

  return result;
}

/*
 * db_is_input_marker() - Returns true iff it is the host variable input marker
 * return : boolean
 * marker(in): DB_MARKER
 */
bool
db_is_input_marker (DB_MARKER * marker)
{
  bool result = false;

  if (marker)
    {
      result = pt_is_input_hostvar (marker);
    }

  return result;
}

/*
 * db_is_output_marker() - Returns true iff it is the host variable
 *    output marker
 * return : boolean
 * marker(in): DB_MARKER
 */
bool
db_is_output_marker (DB_MARKER * marker)
{
  bool result = false;

  if (marker)
    {
      result = pt_is_output_hostvar (marker);
    }

  return result;
}


/*
 * db_get_query_type_list() - This function returns a type list that describes
 *    the columns of a SELECT statement. This includes the column title, data
 *    type, and size. The statement ID must have been returned by a previously
 *    successful call to the db_compile_statement() function. The query type
 *    list is freed by using the db_query_format_free() function.
 * return : query type.
 * session(in): session handle
 * stmt(in): statement id
 */
DB_QUERY_TYPE *
db_get_query_type_list (DB_SESSION * session, int stmt_ndx)
{
  PT_NODE *statement;
  DB_QUERY_TYPE *qtype;
  int cmd_type;

  /* obvious error checking - invalid parameter */
  if (!session || !session->parser)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_IT_INVALID_SESSION, 0);
      return NULL;
    }
  /* no statement was given in the session */
  if (session->dimension == 0 || !session->statements)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_IT_EMPTY_STATEMENT, 0);
      return NULL;
    }
  /* invalid parameter */
  statement = session->statements[--stmt_ndx];
  if (stmt_ndx < 0 || stmt_ndx >= session->dimension || !statement)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE,
	      ER_OBJ_INVALID_ARGUMENTS, 0);
      return NULL;
    }
  /* check if the statement is compiled and prepared */
  if (session->stage[stmt_ndx] < StatementPreparedStage)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_IT_INVALID_SESSION, 0);
      return NULL;
    }

  /* make DB_QUERY_TYPE structure to return */
  cmd_type = pt_node_to_cmd_type (statement);
  if (cmd_type == SQLX_CMD_SELECT)
    {
      PT_NODE *select_list = pt_get_select_list (session->parser, statement);
      if (pt_length_of_select_list (select_list, EXCLUDE_HIDDEN_COLUMNS) > 0)
	{
	  /* duplicate one from stored list */
	  qtype = db_cp_query_type (session->type_list[stmt_ndx], true);
	}
      else
	{
	  qtype = NULL;
	}
    }
  else
    {
      /* make new one containing single value */
      qtype = db_alloc_query_format (1);
      if (qtype)
	{
	  switch (cmd_type)
	    {
	    case SQLX_CMD_CALL:
	      qtype->db_type = pt_node_to_db_type (statement);
	      break;
	    case SQLX_CMD_INSERT:
	      /* the type of result of INSERT is object */
	      qtype->db_type = DB_TYPE_OBJECT;
	      break;
	    case SQLX_CMD_GET_ISO_LVL:
	    case SQLX_CMD_GET_TIMEOUT:
	    case SQLX_CMD_GET_OPT_LVL:
	    case SQLX_CMD_GET_TRIGGER:
	    case SQLX_CMD_GET_LDB:
	      /* the type of result of some command is integer */
	      qtype->db_type = DB_TYPE_INTEGER;
	      break;
	    }
	}
    }

  return qtype;
}

/*
 * db_get_query_type_ptr() - This function returns query_type of query result
 * return : result->query_type
 * result(in): query result
 */
DB_QUERY_TYPE *
db_get_query_type_ptr (DB_QUERY_RESULT * result)
{
  return (result->query_type);
}

/*
 * db_get_start_line() - This function returns source line position of
 *    a query statement
 * return : stmt's source line position
 * session(in): contains the SQL query that has been compiled
 * stmt(in): int returned by a successful compilation
 */
int
db_get_start_line (DB_SESSION * session, int stmt)
{
  int retval;
  PARSER_CONTEXT *parser;
  PT_NODE *statement;

  if (!session
      || !(parser = session->parser)
      || !session->statements
      || stmt < 1
      || stmt > session->dimension
      || !(statement = session->statements[stmt - 1]))
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS,
	      0);
      retval = er_errid ();
    }
  else
    {
      retval = pt_statement_line_number (statement);
    }

  return (retval);
}

/*
 * db_get_statement_type() - This function returns query statement node type
 * return : stmt's node type
 * session(in): contains the SQL query that has been compiled
 * stmt(in): statement id returned by a successful compilation
 */
int
db_get_statement_type (DB_SESSION * session, int stmt)
{
  int retval;
  PARSER_CONTEXT *parser;
  PT_NODE *statement;

  if (!session
      || !(parser = session->parser)
      || !session->statements
      || stmt < 1
      || stmt > session->dimension
      || !(statement = session->statements[stmt - 1]))
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS,
	      0);
      retval = er_errid ();
    }
  else
    {
      retval = pt_node_to_cmd_type (statement);
    }

  return retval;
}

/*
 * db_include_oid() - This function set the session->parser->oid_included flag
 * return : void
 * session(in): the current session context
 * include_oid(in): non-zero means include oid,
 *	            zero means don't include it.
 */
void
db_include_oid (DB_SESSION * session, int include_oid)
{
  if (!session)
    {
      return;
    }

  session->include_oid = include_oid;
}

/*
 * db_push_values() - This function set session->parser->host_variables
 *   & host_var_count
 * return : void
 * session(in): contains the SQL query that has been compiled
 * count(in): number of elements in in_values table
 * in_values(in): a table of host_variable initialized DB_VALUEs
 */
void
db_push_values (DB_SESSION * session, int count, DB_VALUE * in_values)
{
  PARSER_CONTEXT *parser;

  if (!session || !(parser = session->parser))
    {
      /* nothing */
    }
  else
    {
      pt_set_host_variables (parser, count, in_values);
    }
}

/*
 * db_get_hostvars() -
 * return:
 * session(in) :
 */
DB_VALUE *
db_get_hostvars (DB_SESSION * session)
{
  return session->parser->host_variables;
}

/*
 * db_get_lock_classes() -
 * return:
 * session(in) :
 */
char **
db_get_lock_classes (DB_SESSION * session)
{
  if (session == NULL || session->parser == NULL)
    {
      return NULL;
    }
  return (char **) (session->parser->lcks_classes);
}

/*
 * db_set_sync_flag() -
 * return:
 * session(in) :
 * exec_mode(in) :
 */
void
db_set_sync_flag (DB_SESSION * session, QUERY_EXEC_MODE exec_mode)
{
  session->parser->exec_mode = exec_mode;
}

/*
 * db_get_session_mode() -
 * return:
 * session(in) :
 */
int
db_get_session_mode (DB_SESSION * session)
{
  if (!session || !session->parser)
    {
      er_set (ER_SYNTAX_ERROR_SEVERITY, ARG_FILE_LINE, ER_IT_INVALID_SESSION,
	      0);
      return ER_IT_INVALID_SESSION;
    }
  else
    {
      return (session->parser->exec_mode == SYNC_EXEC);
    }
}

/*
 * db_set_session_mode_sync() -
 * return:
 * session(in) :
 */
int
db_set_session_mode_sync (DB_SESSION * session)
{
  if (!session || !session->parser)
    {
      er_set (ER_SYNTAX_ERROR_SEVERITY, ARG_FILE_LINE, ER_IT_INVALID_SESSION,
	      0);
      return ER_IT_INVALID_SESSION;
    }
  db_set_sync_flag (session, SYNC_EXEC);
  return NO_ERROR;
}

/*
 * db_set_session_mode_async() -
 * return:
 * session(in) :
 */
int
db_set_session_mode_async (DB_SESSION * session)
{
  if (!session || !session->parser)
    {
      er_set (ER_SYNTAX_ERROR_SEVERITY, ARG_FILE_LINE, ER_IT_INVALID_SESSION,
	      0);
      return ER_IT_INVALID_SESSION;
    }
  db_set_sync_flag (session, ASYNC_EXEC);
  return NO_ERROR;
}

/*
 * db_execute_and_keep_statement_local() - This function executes the SQLX
 *    statement identified by the stmt argument and returns the result.
 *    The statement ID must have already been returned by a successful call
 *    to the db_open_file() function or the db_open_buffer() function that
 *    came from a call to the db_compile_statement()function. The compiled
 *    statement is preserved, and may be executed again within the same
 *    transaction.
 * return : error status, if execution failed
 *          number of affected objects, if a success & stmt is a SELECT,
 *          UPDATE, DELETE, or INSERT
 * session(in) : contains the SQL query that has been compiled
 * stmt(in) : int returned by a successful compilation
 * result(out): query results descriptor
 */
static int
db_execute_and_keep_statement_local (DB_SESSION * session, int stmt_ndx,
				     DB_QUERY_RESULT ** result)
{
  PARSER_CONTEXT *parser;
  PT_NODE *statement;
  DB_QUERY_RESULT *qres;
  DB_VALUE *val;
  int err = NO_ERROR;
  SERVER_INFO server_info;

  /* obvious error checking - invalid parameter */
  if (!session || !session->parser)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_IT_INVALID_SESSION, 0);
      return er_errid ();
    }
  /* no statement was given in the session */
  if (session->dimension == 0 || !session->statements)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_IT_EMPTY_STATEMENT, 0);
      return er_errid ();
    }
  /* invalid parameter */
  stmt_ndx--;
  if (stmt_ndx < 0 || stmt_ndx >= session->dimension ||
      !session->statements[stmt_ndx])
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE,
	      ER_OBJ_INVALID_ARGUMENTS, 0);
      return er_errid ();
    }
  /* no host variable was set before */
  if (session->parser->host_var_count > 0 &&
      session->parser->set_host_var == 0)
    {
      /* parsed statement has some host variable parameters
         (input marker '?'), but no host variable (DB_VALUE array) was set
         by db_push_values() API */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_UCI_TOO_FEW_HOST_VARS, 0);
      return er_errid ();
    }
  /* if the parser already has something wrong - semantic error */
  if (session->stage[stmt_ndx] < StatementExecutedStage
      && pt_has_error (session->parser))
    {
      pt_report_to_ersys (session->parser, PT_SEMANTIC);
      return er_errid ();
    }

  /*
   * Execution Stage
   */
  er_clear ();

  /* now, we have a statement to execute */
  parser = session->parser;
  statement = session->statements[stmt_ndx];

  /* if the statement was no compiled and prepared, do it */
  if (session->stage[stmt_ndx] < StatementPreparedStage)
    {
      session->stmt_ndx = stmt_ndx;
      if (db_compile_statement_local (session) < 0)
	{
	  return er_errid ();
	}
    }

  /* forget about any previous compilation errors, if any */
  pt_reset_error (parser);

  /* get sys_date, sys_time, sys_timestamp values from the server */
  server_info.info_bits = 0;	/* init */
  if (statement->si_timestamp && DB_IS_NULL (&parser->sys_timestamp))
    {
      /* if it was reset in the previous execution step, fills it now */
      server_info.info_bits |= SI_SYS_TIMESTAMP;
      server_info.value[0] = &parser->sys_timestamp;
    }
  if (statement->si_tran_id && DB_IS_NULL (&parser->local_transaction_id))
    {
      /* if it was reset in the previous execution step, fills it now */
      server_info.info_bits |= SI_LOCAL_TRANSACTION_ID;
      server_info.value[1] = &parser->local_transaction_id;
    }
  /* request to the server */
  if (server_info.info_bits)
    {
      (void) qp_get_server_info (&server_info);
    }

  /* New interface of do_prepare_statement()/do_execute_statment() is used
     only when the XASL cache is enabled. If it is disabled, old interface
     of do_statement() will be used instead. do_statement() makes a XASL
     everytime rather than using XASL cache. Also, it can be executed in
     the server without touching the XASL cache by calling
     query_prepare_and_execute(). */
  do_Trigger_involved = false;
  if (PRM_XASL_MAX_PLAN_CACHE_ENTRIES >= 0 && statement->cannot_prepare == 0)
    {
      /* now, execute the statement by calling do_execute_statement() */
      err = do_execute_statement (parser, statement);
      if (err == ER_QPROC_INVALID_XASLNODE &&
	  session->stage[stmt_ndx] == StatementPreparedStage)
	{
	  /* Hmm, there is a kind of problem in the XASL cache.
	     It is possible when the cache entry was deleted before 'execute'
	     and after 'prepare' by the other, e.g. qmgr_drop_all_query_plans().
	     In this case, retry to prepare once more (generate and stored
	     the XASL again). */
	  if (statement->xasl_id)
	    {
	      (void) qmgr_drop_query_plan (NULL, NULL, statement->xasl_id,
					   false);
	      free_and_init (statement->xasl_id);
	      statement->xasl_id = NULL;
	    }
	  /* forget all errors */
	  er_clear ();
	  pt_reset_error (parser);

	  /* retry the statement by calling do_prepare/execute_statement() */
	  err = do_prepare_statement (parser, statement);
	  if (!(err < 0))
	    {
	      err = do_execute_statement (parser, statement);
	    }
	}
    }
  else
    {
      SEMANTIC_CHK_INFO sc_info;
      /* bind and resolve host variables */
      if (parser->host_var_count > 0 && parser->set_host_var == 1)
	{
	  /* In this case, pt_bind_values_to_hostvars() will change
	     PT_HOST_VAR node. Must duplicate the statement and execute with
	     the new one and free the copied one before returning */
	  statement = parser_copy_tree_list (parser, statement);

	  sc_info.attrdefs = NULL;
	  sc_info.top_node = statement;
	  sc_info.donot_fold = false;

	  if (!(statement = pt_bind_values_to_hostvars (parser, statement)) ||
	      !(statement = pt_resolve_names (parser, statement, &sc_info)) ||
	      !(statement = pt_semantic_type (parser, statement, &sc_info)))
	    {
	      /* something wrong */
	      if (er_errid () == NO_ERROR)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_DO_UNKNOWN_HOSTVAR_TYPE, 0);
		}
	      if (pt_has_error (parser))
		{
		  pt_report_to_ersys_with_statement (parser, PT_SYNTAX,
						     statement);
		}
	      if (statement != session->statements[stmt_ndx])
		{
		  parser_free_tree (parser, statement);
		}
	      return er_errid ();
	    }
	}

      err = do_statement (parser, statement);
    }
  do_Trigger_involved = false;
  if (err < 0)
    {
      if (pt_has_error (parser) && err != ER_QPROC_INVALID_XASLNODE)
	{
	  pt_report_to_ersys_with_statement (parser, PT_EXECUTION, statement);
	}
      /* free the allocated list_id area before leaving */
      if (pt_node_to_cmd_type (statement) == SQLX_CMD_SELECT)
	{
	  pt_free_query_etc_area (statement);
	}
    }
  /* so now, the statement is executed */
  session->stage[stmt_ndx] = StatementExecutedStage;

  /* execution succeeded, maybe. process result of the query */
  if (result && !(err < 0))
    {
      qres = NULL;

      if (statement->clt_cache_reusable)
	{
	  qres = pt_make_cache_hit_result_descriptor ();
	  if (!qres)
	    {
	      err = er_errid ();
	    }
	}
      else
	{

	  switch (pt_node_to_cmd_type (statement))
	    {
	    case SQLX_CMD_SELECT:
	      /* Check whether pt_new_query_result_descriptor() fails.
	         Similar tests are required for SQLX_CMD_INSERT and
	         SQLX_CMD_CALL cases. */
	      qres = pt_new_query_result_descriptor (parser, statement);
	      if (qres)
		{
		  /* get number of rows as result */
		  err = db_query_tuple_count (qres);
		  qres->query_type =
		    db_cp_query_type (session->type_list[stmt_ndx], false);
		  qres->res.s.stmt_id = stmt_ndx;
		}
	      else
		{
		  err = er_errid ();
		}
	      break;

	    case SQLX_CMD_GET_ISO_LVL:
	    case SQLX_CMD_GET_TIMEOUT:
	    case SQLX_CMD_GET_OPT_LVL:
	    case SQLX_CMD_GET_TRIGGER:
	    case SQLX_CMD_GET_LDB:
	    case SQLX_CMD_EVALUATE:
	    case SQLX_CMD_CALL:
	    case SQLX_CMD_INSERT:
	    case SQLX_CMD_GET_STATS:
	      /* usqlx (in iqcmd.c) may throw away any non-null *result,
	         but we create a DB_QUERY_RESULT structure anyway for other
	         callers of db_execute that use the *result like unici.c  */
	      val = (DB_VALUE *) pt_node_etc (statement);
	      if (val)
		{
		  /* got a result, so use it */
		  /* Again, be careful here, because someone may be
		     propagating a count of affected objects upward.
		     INSERT is especially vulnerable here, because it may
		     have actually inserted many objects (using INSERT,
		     SELECT ...), but we've only put the first one in the
		     DB_QUERY_RESULT gadget, so its count may not jive.
		     If err is greater than zero, use that rather than
		     the count from the DB_QUERY_RESULT.
		     This whole interface needs to be formalized and
		     cleaned up. */
		  qres = db_get_db_value_query_result (val);
		  if (qres)
		    {
		      /* get number of rows as result */
		      err = db_query_tuple_count (qres);
		    }
		  else
		    {
		      err = er_errid ();
		    }

		  /* db_get_db_value_query_result copied val, so free val */
		  db_value_free (val);
		  pt_null_etc (statement);
		}
	      else
		{
		  /* avoid changing err. it should have been
		     meaningfully set. if err = 0, uci_static will set
		     SQLCA to SQL_NOTFOUND! */
		}
	      break;

	    default:
	      break;
	    }			/* switch (pt_node_to_cmd_type()) */

	}			/* else */

      *result = qres;
    }				/* if (result) */

  /* last error checking */
  if (pt_has_error (parser) && err != ER_QPROC_INVALID_XASLNODE)
    {
      pt_report_to_ersys_with_statement (parser, PT_EXECUTION, statement);
      err = er_errid ();
    }

  /* reset the parser values */
  if (statement->si_timestamp)
    {
      db_make_null (&parser->sys_timestamp);
    }
  if (statement->si_tran_id)
    {
      db_make_null (&parser->local_transaction_id);
    }

  /* free if the statement was duplicated for host variable binding */
  if (statement != session->statements[stmt_ndx])
    {
      parser_free_tree (parser, statement);
    }

  return err;
}

/*
 * db_execute_and_keep_statement() - Please refer to the
 *         db_execute_and_keep_statement_local() function
 * return : error status, if execution failed
 *          number of affected objects, if a success & stmt is a SELECT,
 *          UPDATE, DELETE, or INSERT
 * session(in) : contains the SQL query that has been compiled
 * stmt(in) : int returned by a successful compilation
 * result(out): query results descriptor
 */
int
db_execute_and_keep_statement (DB_SESSION * session, int stmt_ndx,
			       DB_QUERY_RESULT ** result)
{
  int err;

  CHECK_CONNECT_MINUSONE ();

  err = db_execute_and_keep_statement_local (session, stmt_ndx, result);

  return err;
}

/*
 * db_execute_statement_local() - This function executes the SQLX statement
 *    identified by the stmt argument and returns the result. The
 *    statement ID must have already been returned by a previously successful
 *    call to the db_compile_statement() function.
 * returns  : error status, if execution failed
 *            number of affected objects, if a success & stmt is a
 *            SELECT, UPDATE, DELETE, or INSERT
 * session(in) : contains the SQL query that has been compiled
 * stmt(in) : int returned by a successful compilation
 * result(out): query results descriptor
 *
 * note : You must free the results of calling this function by using the
 *    db_query_end() function. The resources for the identified compiled
 *    statement (not its result) are freed. Consequently, the statement may
 *    not be executed again.
 */
int
db_execute_statement_local (DB_SESSION * session, int stmt_ndx,
			    DB_QUERY_RESULT ** result)
{
  int err;
  PT_NODE *statement;

  if (session && session->statements &&
      stmt_ndx > 0 && stmt_ndx <= session->dimension &&
      session->statements[stmt_ndx - 1])
    {
      session->statements[stmt_ndx - 1]->do_not_keep = 1;
    }
  err = db_execute_and_keep_statement_local (session, stmt_ndx, result);

  statement = session->statements[stmt_ndx - 1];
  if (statement != NULL)
    {
      /* free XASL_ID allocated by query_prepare()
         before freeing the statement */
      if (statement->xasl_id)
	{
	  free_and_init (statement->xasl_id);
	}
      parser_free_tree (session->parser, statement);
      session->statements[stmt_ndx - 1] = NULL;
    }

  return err;
}

/*
 * db_execute_statement() - Please refer to the
 *    db_execute_statement_local() function
 * returns  : error status, if execution failed
 *            number of affected objects, if a success & stmt is a
 *            SELECT, UPDATE, DELETE, or INSERT
 * session(in) : contains the SQL query that has been compiled
 * stmt(in) : int returned by a successful compilation
 * result(out): query results descriptor
 */
int
db_execute_statement (DB_SESSION * session, int stmt_ndx,
		      DB_QUERY_RESULT ** result)
{
  int err;

  CHECK_CONNECT_MINUSONE ();

  err = db_execute_statement_local (session, stmt_ndx, result);
  return err;
}

/*
 * db_drop_statement() - This function frees the resources allocated to a
 *    compiled statement
 * return : void
 * session(in) : session handle
 * stmt(in) : statement id returned by a successful compilation
 */
void
db_drop_statement (DB_SESSION * session, int stmt)
{
  PT_NODE *statement;

  statement = session->statements[stmt - 1];
  if (statement != NULL)
    {
      /* free XASL_ID allocated by query_prepare()
         before freeing the statement */
      if (statement->xasl_id)
	{
	  if (statement->do_not_keep == 0)
	    {
	      (void) qmgr_drop_query_plan (NULL, NULL, statement->xasl_id,
					   false);
	    }
	  free_and_init (statement->xasl_id);
	}
      parser_free_tree (session->parser, statement);
      session->statements[stmt - 1] = NULL;
      session->stage[stmt - 1] = StatementInitialStage;
    }
}

/*
 * db_drop_all_statements() - This function frees the resources allocated
 *    to a session's compiled statements
 * rerutn : void
 * session(in) : session handle contains the SQL queries that have been
 *   compiled
 */
void
db_drop_all_statements (DB_SESSION * session)
{
  PT_NODE *statement;
  int stmt;

  for (stmt = 0; stmt < session->dimension; stmt++)
    {
      statement = session->statements[stmt];
      if (statement != NULL)
	{
	  /* free XASL_ID allocated by query_prepare()
	     before freeing the statement */
	  if (statement->xasl_id)
	    {
	      if (statement->do_not_keep == 0)
		{
		  (void) qmgr_drop_query_plan (NULL, NULL, statement->xasl_id,
					       false);
		}
	      free_and_init (statement->xasl_id);
	    }
	  parser_free_tree (session->parser, statement);
	  session->statements[stmt] = NULL;
	  session->stage[stmt] = StatementInitialStage;
	}
    }
  session->dimension = session->stmt_ndx = 0;
}

/*
 * db_close_session() - This function frees all resources of this session
 *    except query results
 * return : void
 * session(in) : session handle
 */
void
db_close_session_local (DB_SESSION * session)
{
  PARSER_CONTEXT *parser;
  int i;

  if (!session)
    {
      return;
    }

  parser = session->parser;
  for (i = 0; i < session->dimension; i++)
    {
      PT_NODE *statement;

      if (session->type_list && session->type_list[i])
	{
	  db_free_query_format (session->type_list[i]);
	}
      if (session->statements)
	{
	  statement = session->statements[i];
	  if (statement != NULL)
	    {
	      /* free XASL_ID allocated by query_prepare()
	         before freeing the statement */
	      if (statement->xasl_id)
		{
		  if (statement->do_not_keep == 0)
		    {
		      (void) qmgr_drop_query_plan (NULL, NULL,
						   statement->xasl_id, false);
		    }
		  free_and_init (statement->xasl_id);
		}
	      parser_free_tree (parser, statement);
	      session->statements[i] = NULL;
	    }
	}
    }
  session->dimension = session->stmt_ndx = 0;
  if (session->type_list)
    {
      free_and_init (session->type_list);	/* see db_compile_statement_local() */
    }

  if (parser->host_variables)
    {
      DB_VALUE *hv;

      for (i = 0, hv = parser->host_variables;
	   i < parser->host_var_count + parser->auto_param_count; i++, hv++)
	{
	  db_value_clear (hv);
	}
    }
  parser->host_var_count = parser->auto_param_count = 0;

  pt_free_orphans (session->parser);
  parser_free_parser (session->parser);

  free_and_init (session);
}

/*
 * db_close_session() - Please refer to the db_close_session_local() function
 * return: void
 * session(in) : session handle
 */
void
db_close_session (DB_SESSION * session)
{
  db_close_session_local (session);
}


/*
 * db_get_all_chosen_classes() - This funciton returns list of all classes
 *    that pass a predicate
 * return : list of class objects that pass a given predicate, if all OK,
 *          NULL otherwise.
 * p(in) : a predicate function
 *
 * note    : the caller is responsible for freeing the list with a call to
 *	       db_objlist_free.
 *
 */
static DB_OBJLIST *
db_get_all_chosen_classes (int (*p) (MOBJ o))
{
  LIST_MOPS *lmops;
  DB_OBJLIST *objects, *last, *new_;
  int i;

  objects = NULL;
  lmops = NULL;
  if (au_check_user () == NO_ERROR)
    {
      /* make sure we have a user */
      last = NULL;
      lmops = locator_get_all_class_mops (DB_FETCH_CLREAD_INSTREAD, p);
      /* probably should make sure
       * we push here because the list could be long */
      if (lmops != NULL)
	{
	  for (i = 0; i < lmops->num; i++)
	    {
	      /* is it necessary to have this check ? */
	      if (!WS_MARKED_DELETED (lmops->mops[i]) &&
		  lmops->mops[i] != sm_Root_class_mop)
		{
		  /* should have a ext_ append function */
		  new_ = ml_ext_alloc_link ();
		  if (new_ == NULL)
		    {
		      goto memory_error;
		    }
		  new_->op = lmops->mops[i];
		  new_->next = NULL;
		  if (last != NULL)
		    {
		      last->next = new_;
		    }
		  else
		    {
		      objects = new_;
		    }
		  last = new_;
		}
	    }
	  locator_free_list_mops (lmops);
	}
    }
  return (objects);

memory_error:
  if (lmops != NULL)
    {
      locator_free_list_mops (lmops);
    }
  if (objects)
    {
      ml_ext_free (objects);
    }
  return NULL;
}

/*
 * is_vclass_object() -
 * return:
 * class(in) :
 */
static int
is_vclass_object (MOBJ class_)
{
  return sm_get_class_type ((SM_CLASS *) class_) == SM_VCLASS_CT;
}

/*
 * is_vclass_object_on_ldb() -
 * return:
 * class(in) :
 */
static int
is_vclass_object_on_ldb (MOBJ class_)
{
  return sm_get_class_type ((SM_CLASS *) class_) == SM_LDBVCLASS_CT;
}


/*
 * db_get_all_vclasses_on_ldb() - This function returns list of all ldb
 *    virtual classes
 * return : list of all ldb virtual class objects if all OK,
 *	    NULL otherwise.
 */
DB_OBJLIST *
db_get_all_vclasses_on_ldb (void)
{
  DB_OBJLIST *retval;

  retval = db_get_all_chosen_classes (is_vclass_object_on_ldb);

  return (retval);
}

/*
 * db_get_all_vclasses() - This function returns list of all virtual classes
 * returns  : list of all virtual class objects if all OK,
 *	      NULL otherwise.
 */
DB_OBJLIST *
db_get_all_vclasses (void)
{
  DB_OBJLIST *retval;

  retval = db_get_all_chosen_classes (is_vclass_object);

  return (retval);
}


/*
 * db_validate_query_spec() - This function checks that a query_spec is
 *    compatible with a given {ldbvclass|vclass} object
 * return  : an ER status code if an error was found, NO_ERROR otherwise.
 * vclass(in) : an {ldbvclass|vclass} object
 * query_spec(in) : a query specification string
 */
int
db_validate_query_spec (DB_OBJECT * vclass, const char *query_spec)
{
  PARSER_CONTEXT *parser;
  PT_NODE **spec;
  int rc;

  parser = parser_create_parser ();
  if (!parser)
    {
      rc = ER_GENERIC_ERROR;
    }
  else
    {

      spec = parser_parse_string (parser, query_spec);
      if (!pt_has_error (parser))
	{
	  rc = pt_validate_query_spec (parser, *spec, vclass);
	}
      else
	{
	  pt_report_to_ersys (parser, PT_SYNTAX);
	  rc = er_errid ();
	}
    }
  parser_free_parser (parser);

  return rc;
}

/*
 * get_reasonable_predicate() - This function determines if we can compose
 *   any reasonable predicate against this attribute and return that predicate
 * returns: a reasonable predicate against att if one exists, NULL otherwise
 * att(in) : an instance attribute
 */
static char *
get_reasonable_predicate (DB_ATTRIBUTE * att)
{
  static char predicate[300];
  const char *att_name;

  if (!att
      || db_attribute_is_shared (att)
      || !(att_name = db_attribute_name (att)))
    {
      return NULL;
    }

  strcpy (predicate, att_name);

  switch (db_attribute_type (att))
    {
    case DB_TYPE_INTEGER:
    case DB_TYPE_FLOAT:
    case DB_TYPE_DOUBLE:
    case DB_TYPE_SHORT:
    case DB_TYPE_MONETARY:
      strcat (predicate, " = 1 ");
      return predicate;
    case DB_TYPE_STRING:
      strcat (predicate, " = 'x' ");
      return predicate;
    case DB_TYPE_OBJECT:
      strcat (predicate, " is null ");
      return predicate;
    case DB_TYPE_SET:
    case DB_TYPE_MULTISET:
    case DB_TYPE_SEQUENCE:
      strcat (predicate, " = {} ");
      return predicate;
    case DB_TYPE_TIME:
      strcat (predicate, " = '09:30' ");
      return predicate;
    case DB_TYPE_TIMESTAMP:
      strcat (predicate, " = '10/15/1986 5:45 am' ");
      return predicate;
    case DB_TYPE_DATE:
      strcat (predicate, " = '10/15/1986' ");
      return predicate;
    default:
      return NULL;
    }
}

/*
 * db_validate() - This function checks if a {class|vclass|proxy} definition
 *    is reasonable
 * returns  : an ER status code if an error was found, NO_ERROR otherwise.
 * vc(in) : a {class|vclass|proxy} object
 */
int
db_validate (DB_OBJECT * vc)
{
  int retval = NO_ERROR;
  DB_QUERY_SPEC *specs;
  const char *s, *separator = " where ";
  char buffer[BUF_SIZE], *pred, *bufp, *newbuf;
  DB_QUERY_RESULT *result = NULL;
  DB_ATTRIBUTE *attributes;
  int len, limit = BUF_SIZE;

  CHECK_CONNECT_ERROR ();

  if (!vc)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS,
	      0);
      retval = er_errid ();
    }
  else
    {
      if (!db_is_any_class (vc))
	{
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_NOT_A_CLASS, 0);
	  retval = er_errid ();
	}
      else
	{

	  for (specs = db_get_query_specs (vc);
	       specs; specs = db_query_spec_next (specs))
	    {
	      s = db_query_spec_string (specs);
	      if (s)
		{
		  retval = db_validate_query_spec (vc, s);
		  if (retval < 0)
		    {
		      break;
		    }
		}
	    }
	}
    }

  if (retval >= 0)
    {
      strcpy (buffer, "select count(*) from ");
      strcat (buffer, db_get_class_name (vc));
      attributes = db_get_attributes (vc);
      len = strlen (buffer);
      bufp = buffer;
      while (attributes)
	{
	  pred = get_reasonable_predicate (attributes);
	  if (pred)
	    {
	      /* make sure we have enough room in the buffer */
	      len += (strlen (separator) + strlen (pred));
	      if (len >= limit)
		{
		  /* increase buffer by BUF_SIZE */
		  limit += BUF_SIZE;
		  newbuf = (char *) malloc (limit * sizeof (char));

		  if (!newbuf)
		    {
		      break;	/* ran out of memory */
		    }

		  /* copy old buffer into new buffer and switch */
		  strcpy (newbuf, bufp);
		  if (bufp != buffer)
		    {
		      free_and_init (bufp);
		    }
		  bufp = newbuf;
		}
	      /* append another predicate */
	      strcat (bufp, separator);
	      strcat (bufp, pred);
	      separator = " and ";
	    }
	  attributes = db_attribute_next (attributes);
	}
      retval = db_query_execute (bufp, &result, NULL);
      if (result)
	{
	  db_query_end (result);
	}
      if (bufp != buffer)
	{
	  free_and_init (bufp);
	}
    }

  return retval;
}

/*
 * db_free_query() - If an implicit query was executed, free the query on the
 *   server.
 * returns  : void
 * session(in) : session handle
 */
void
db_free_query (DB_SESSION * session)
{
  pt_end_query (session->parser);

}

/*
 * db_check_single_query() - This funciton checks to see if there is only
 *    one statement given, and that it is a valid query statement.
 * return : error code
 * session(in) : session handle
 * stmt_no(in) : statement number
 */
int
db_check_single_query (DB_SESSION * session, int stmt_no)
{
  if (session->dimension > 1)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IT_MULTIPLE_STATEMENT, 0);
      return er_errid ();
    }
  return NO_ERROR;
}


/*
 * db_get_parser() - This function returns session's parser
 * returns: session->parser
 * session (in): session handle
 *
 * note : This is a debugging function.
 */
PARSER_CONTEXT *
db_get_parser (DB_SESSION * session)
{
  return session->parser;
}

/*
 * db_get_statement() - This function returns session's statement for id
 * arguments: session (IN): compilation session
 * returns: PT_NODE
 *
 * note : This is a debugging function only.
 *
 */
DB_NODE *
db_get_statement (DB_SESSION * session, int id)
{
  return session->statements[id];
}

/*
 * db_get_parameters() - This function returns a list of the parameters in the
 *    specified statement. There is no implied ordering in the returned list
 *    Parameter names appear once in the returned list, even if they appear
 *    more than once in the statement.
 * return: DB_PARAMETER iterator if there were any parameters in the statement
 * session(in): session handle
 * statement(in): statement number
 */
DB_PARAMETER *
db_get_parameters (DB_SESSION * session, int statement_id)
{
  DB_PARAMETER *result = NULL;
  DB_NODE *statement;

  if (!session
      || !session->parser
      || !session->statements
      || statement_id < 1
      || statement_id > session->dimension
      || !(statement = session->statements[statement_id - 1])
      || pt_has_error (session->parser))
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS,
	      0);
      result = NULL;
    }
  else
    {
      result = pt_get_parameters (session->parser, statement);
    }

  return result;
}

/*
 * db_parameter_next() - This function returns the next parameter in a
 *    parameter list or NULL if at the end of the parameter list. The
 *    value given for param must not be NULL Returns the next parameter
 *    in a parameter list or NULL if at the end of the list.
 * return : next parameter in a parameter list
 * param(in) : a parameter
 */
DB_PARAMETER *
db_parameter_next (DB_PARAMETER * param)
{
  DB_PARAMETER *result = NULL;

  if (param)
    {
      result = pt_node_next (param);
    }

  return result;
}

/*
 * db_parameter_name() - This function returns the name for the given
 *    parameter. param must not be a NULL value.
 * return : parameter name
 * param(in) : a parameter
 */
const char *
db_parameter_name (DB_PARAMETER * param)
{
  const char *result = NULL;

  if (param)
    {
      result = pt_string_part (param);
    }

  return result;
}

/*
 * db_bind_parameter_name() -
 * return: error code
 * name(in) : parameter name
 * value(in) : value to be associated
 *
 * note : This function is analogous to other database vendors' use of the
 *        term bind in that it is an association with a variable location.
 */
int
db_bind_parameter_name (const char *name, DB_VALUE * value)
{
  int result = NO_ERROR;

  pt_associate_label_with_value (name, value);

  return result;
}

/*
 * db_query_produce_updatable_result() -
 * return:
 * session(in) :
 * stmt_ndx(in) :
 */
int
db_query_produce_updatable_result (DB_SESSION * session, int stmt_ndx)
{
  PT_NODE *statement;

  /* obvious error checking - invalid parameter */
  if (!session || !session->parser)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_IT_INVALID_SESSION, 0);
      return er_errid ();
    }
  /* no statement was given in the session */
  if (session->dimension == 0 || !session->statements)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_IT_EMPTY_STATEMENT, 0);
      return er_errid ();
    }
  /* invalid parameter */
  statement = session->statements[--stmt_ndx];
  if (stmt_ndx < 0 || stmt_ndx >= session->dimension || !statement)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE,
	      ER_OBJ_INVALID_ARGUMENTS, 0);
      return er_errid ();
    }
  /* check if the statement is compiled and prepared */
  if (session->stage[stmt_ndx] < StatementPreparedStage)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_IT_INVALID_SESSION, 0);
      return er_errid ();
    }

  if (statement->node_type == PT_SELECT || statement->node_type == PT_UNION)
    {
      return statement->info.query.oids_included;
    }
  else
    {
      return false;
    }
}

/*
 * db_is_query_async_executable() -
 * return:
 * session(in) :
 * stmt_ndx(in) :
 */
bool
db_is_query_async_executable (DB_SESSION * session, int stmt_ndx)
{
  PT_NODE *statement;

  /* obvious error checking - invalid parameter */
  if (!session || !session->parser)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_IT_INVALID_SESSION, 0);
      return false;
    }
  /* no statement was given in the session */
  if (session->dimension == 0 || !session->statements)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_IT_EMPTY_STATEMENT, 0);
      return false;
    }
  /* invalid parameter */
  statement = session->statements[--stmt_ndx];
  if (stmt_ndx < 0 || stmt_ndx >= session->dimension || !statement)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE,
	      ER_OBJ_INVALID_ARGUMENTS, 0);
      return false;
    }
  /* check if the statement is compiled and prepared */
  if (session->stage[stmt_ndx] < StatementPreparedStage)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_IT_INVALID_SESSION, 0);
      return false;
    }

  if (pt_node_to_cmd_type (statement) != SQLX_CMD_SELECT)
    {
      return false;
    }

  return !pt_statement_have_methods (session->parser, statement);
}
