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
 * db_vdb.c - Stubs for SQL interface functions.
 */

#ident "$Id$"

#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/timeb.h>
#include <time.h>
#include "db.h"
#include "dbi.h"
#include "db_query.h"
#include "error_manager.h"
#include "chartype.h"
#include "system_parameter.h"
#include "environment_variable.h"
#include "memory_alloc.h"
#include "parser.h"
#include "parser_message.h"
#include "object_domain.h"
#include "schema_manager.h"
#include "view_transform.h"
#include "execute_statement.h"
#include "xasl_generation.h"	/* TODO: remove */
#include "locator_cl.h"
#include "server_interface.h"
#include "query_manager.h"
#include "api_compat.h"
#include "network_interface_cl.h"
#include "transaction_cl.h"

#define BUF_SIZE 1024

#define MAX_SERVER_TIME_CACHE	60	/* secs */

enum
{
  StatementInitialStage = 0,
  StatementCompiledStage,
  StatementPreparedStage,
  StatementExecutedStage,
};

static struct timeb base_server_timeb = { 0, 0, 0, 0 };
static struct timeb base_client_timeb = { 0, 0, 0, 0 };


static int get_dimension_of (PT_NODE ** array);
static DB_SESSION *db_open_local (void);
static DB_SESSION *initialize_session (DB_SESSION * session);
static int db_execute_and_keep_statement_local (DB_SESSION * session,
						int stmt_ndx,
						DB_QUERY_RESULT ** result);
static DB_OBJLIST *db_get_all_chosen_classes (int (*p) (MOBJ o));
static int is_vclass_object (MOBJ class_);
static char *get_reasonable_predicate (DB_ATTRIBUTE * att);
static void update_execution_values (PARSER_CONTEXT * parser, int result,
				     CUBRID_STMT_TYPE statement_type);
static void copy_execution_values (EXECUTION_STATE_VALUES * source,
				   EXECUTION_STATE_VALUES * destination);
static int values_list_to_values_array (PARSER_CONTEXT * parser,
					PT_NODE * values_list,
					DB_VALUE_ARRAY * values_array);
static int set_prepare_info_into_list (DB_PREPARE_INFO * prepare_info,
				       PT_NODE * statement);
static PT_NODE *char_array_to_name_list (PARSER_CONTEXT * parser,
					 char **names, int length);
static int do_process_prepare_statement (DB_SESSION * session,
					 PT_NODE * statement);
static int do_get_prepared_statement_info (DB_SESSION * session,
					   int stmt_idx);
static int do_set_user_host_variables (DB_SESSION * session,
				       PT_NODE * using_list);
static int do_cast_host_variables_to_expected_domain (DB_SESSION * session);
static int do_recompile_and_execute_prepared_statement (DB_SESSION * session,
							PT_NODE * statement,
							DB_QUERY_RESULT **
							result);
static int do_process_deallocate_prepare (DB_SESSION * session,
					  PT_NODE * statement);
static bool is_allowed_as_prepared_statement (PT_NODE * node);
static bool is_allowed_as_prepared_statement_with_hv (PT_NODE * node);
static bool db_check_limit_need_recompile (PARSER_CONTEXT * parser,
					   PT_NODE * statement,
					   int xasl_flag);

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
 * db_statement_count() - This function returns the number of statements
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
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, sizeof (DB_SESSION));
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
  session->is_subsession_for_prepared = false;
  session->next = NULL;
  session->ddl_stmts_for_replication = NULL;

  return session;
}

/*
 * initialize_session() -
 * returns  : DB_SESSION *, NULL if fails
 * session(in/out):
 */
static DB_SESSION *
initialize_session (DB_SESSION * session)
{
  assert (session != NULL && session->statements != NULL);

  session->dimension = get_dimension_of (session->statements);

  return session;
}

/*
 * db_open_buffer_local() - Please refer to the db_open_buffer() function
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
      session->statements =
	parser_parse_string_with_escapes (session->parser, buffer, false);
      if (session->statements)
	{
	  return initialize_session (session);
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
  CHECK_CONNECT_NULL ();

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

  CHECK_CONNECT_NULL ();

  session = db_open_local ();
  if (session)
    {
      session->statements = parser_parse_file (session->parser, file);
      if (session->statements)
	{
	  return initialize_session (session);
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

  CHECK_CONNECT_NULL ();

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

      session->dimension = 0;
      session->stmt_ndx = 0;

      session->parser->stack_top = 0;
      if (session->stage)
	{
	  session->stage[0] = StatementInitialStage;
	}
    }

  if (parse_one_statement (1) == 0
      && !pt_has_error (session->parser)
      && session->parser->stack_top > 0
      && session->parser->node_stack != NULL)
    {
      session->parser->statements =
	(PT_NODE **) parser_alloc (session->parser, 2 * sizeof (PT_NODE *));
      if (session->parser->statements == NULL)
	{
	  return -1;
	}

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

  CHECK_CONNECT_NULL ();

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
	  return initialize_session (session);
	}
    }

  return session;
}

/*
 * db_calculate_current_server_time () -
 * return:
 * parser(in) :
 * server_info(in) :
 */
static void
db_calculate_current_server_time (PARSER_CONTEXT * parser)
{

  struct tm *c_time_struct;
  DB_DATETIME datetime;

  struct timeb curr_server_timeb;
  struct timeb curr_client_timeb;
  int diff_mtime;
  int diff_time;

  if (base_server_timeb.time == 0)
    {
      return;
    }

  ftime (&curr_client_timeb);
  diff_time = curr_client_timeb.time - base_client_timeb.time;
  diff_mtime = curr_client_timeb.millitm - base_client_timeb.millitm;

  if (diff_time > MAX_SERVER_TIME_CACHE)
    {
      base_server_timeb.time = 0;
    }
  else
    {
      curr_server_timeb.time = base_server_timeb.time;
      curr_server_timeb.millitm = base_server_timeb.millitm;

      /* timeb.millitm is unsigned short, so should prevent underflow */
      if (diff_mtime < 0)
	{
	  curr_server_timeb.time--;
	  curr_server_timeb.millitm += 1000;
	}

      curr_server_timeb.time += diff_time;
      curr_server_timeb.millitm += diff_mtime;

      if (curr_server_timeb.millitm >= 1000)
	{
	  curr_server_timeb.time++;
	  curr_server_timeb.millitm -= 1000;
	}

      c_time_struct = localtime (&curr_server_timeb.time);
      if (c_time_struct == NULL)
	{
	  base_server_timeb.time = 0;
	}
      else
	{
	  db_datetime_encode (&datetime, c_time_struct->tm_mon + 1,
			      c_time_struct->tm_mday,
			      c_time_struct->tm_year + 1900,
			      c_time_struct->tm_hour,
			      c_time_struct->tm_min,
			      c_time_struct->tm_sec,
			      curr_server_timeb.millitm);

	  DB_MAKE_DATETIME (&parser->sys_datetime, &datetime);
	  DB_MAKE_TIMESTAMP (&parser->sys_epochtime,
			     (DB_TIMESTAMP) curr_server_timeb.time);
	}
    }
}

/*
 * db_set_base_server_time() -
 * return:
 * server_info(in) :
 */
static void
db_set_base_server_time (DB_VALUE * db_val)
{
  struct tm c_time_struct;
  DB_TIME time_val;
  DB_DATETIME *dt = db_get_datetime (db_val);

  time_val = dt->time / 1000;	/* milliseconds to seconds */
  db_tm_encode (&c_time_struct, &dt->date, &time_val);

  base_server_timeb.millitm = dt->time % 1000;	/* set milliseconds */

  base_server_timeb.time = mktime (&c_time_struct);
  ftime (&base_client_timeb);
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
  PT_NODE *statement = NULL;
  PT_NODE *statement_result = NULL;
  DB_QUERY_TYPE *qtype;
  int cmd_type;
  int err;
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
  if (session->type_list == NULL)
    {
      size_t size = session->dimension * sizeof (DB_QUERY_TYPE *)
	+ session->dimension * sizeof (char);
      void *p = malloc (size);
      if (p == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, size);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
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
  statement->use_plan_cache = 0;
  statement->use_query_cache = 0;

  /* check if the statement is already processed */
  if (session->stage[stmt_ndx] >= StatementPreparedStage)
    {
      return stmt_ndx + 1;
    }

  /* forget about any previous parsing errors, if any */
  pt_reset_error (parser);

  /* get type list describing the output columns titles of the given query */
  cmd_type = pt_node_to_cmd_type (statement);
  qtype = NULL;
  if (cmd_type == CUBRID_STMT_SELECT)
    {
      qtype = pt_get_titles (parser, statement);
      /* to prevent a memory leak, register the query type list to session */
      session->type_list[stmt_ndx] = qtype;
    }
  if (cmd_type == CUBRID_STMT_EXECUTE_PREPARE)
    {
      /* we don't actually have the statement which will be executed
         and we need to get some information about it from the server */
      int err = do_get_prepared_statement_info (session, stmt_ndx);
      if (err != NO_ERROR)
	{
	  return err;
	}
    }
  /* prefetch and lock classes to avoid deadlock */
  (void) pt_class_pre_fetch (parser, statement);
  if (pt_has_error (parser))
    {
      pt_report_to_ersys_with_statement (parser, PT_SYNTAX, statement);
      return er_errid ();
    }

  if (seed == 0)
    {
      srand48 (seed = (long) time (NULL));
    }

  if (prm_get_integer_value (PRM_ID_HA_MODE) != HA_MODE_OFF
      && is_schema_repl_log_statment (statement)
      && log_does_allow_replication () == true)
    {
      unsigned int save_custom;
      if (session->ddl_stmts_for_replication == NULL)
	{
	  int size = sizeof (char *) * session->dimension;

	  session->ddl_stmts_for_replication = (char **) malloc (size);

	  if (session->ddl_stmts_for_replication == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_OUT_OF_VIRTUAL_MEMORY, 1, size);
	      return ER_OUT_OF_VIRTUAL_MEMORY;
	    }
	  memset (session->ddl_stmts_for_replication, '\0', size);
	}

      save_custom = parser->custom_print;
      parser->custom_print |= PT_CHARSET_COLLATE_USER_ONLY;
      session->ddl_stmts_for_replication[stmt_ndx] =
	parser_print_tree_with_quotes (parser, statement);
      parser->custom_print = save_custom;

      assert_release (session->ddl_stmts_for_replication[stmt_ndx] != NULL);
    }

  /* do semantic check for the statement */
  statement_result = pt_compile (parser, statement);

  if (statement_result == NULL || pt_has_error (parser))
    {
      pt_report_to_ersys_with_statement (parser, PT_SEMANTIC, statement);
      return er_errid ();
    }
  statement = statement_result;

  /* get type list describing the output columns titles of the given query */
  if (cmd_type == CUBRID_STMT_SELECT)
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
	     query, ie related to the original text. It may guess
	     wrong about attribute/column updatability.
	     Thats what they asked for. */
	  qtype = pt_fillin_type_size (parser, statement, qtype, DB_NO_OIDS,
				       false, false);
	}
    }

  /* translate views or virtual classes into base classes */
  statement_result = mq_translate (parser, statement);
  if (!statement_result || pt_has_error (parser))
    {
      pt_report_to_ersys_with_statement (parser, PT_SEMANTIC, statement);
      return er_errid ();
    }
  statement = statement_result;

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
      if (mq_updatable (parser, statement) == PT_UPDATABLE)
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
     prepare_and_execute_query(). */
  if (prm_get_integer_value (PRM_ID_XASL_MAX_PLAN_CACHE_ENTRIES) > 0
      && statement->cannot_prepare == 0)
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
 * db_rewind_statement() -
 * return:
 * session(in) :
 */
void
db_rewind_statement (DB_SESSION * session)
{
  if (session->dimension == session->stmt_ndx)
    {
      session->stmt_ndx = 1;
    }
}

/*
 * db_session_is_last_statement() -
 * return:
 * session(in) :
 */
int
db_session_is_last_statement (DB_SESSION * session)
{
  assert (session->dimension > 0);
  return session->dimension == session->stmt_ndx;
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
 * db_get_useplancache() -
 * return:
 * session(in) :
 * stmt_ndx(in) :
 * life_time(out) :
 */
bool
db_get_cacheinfo (DB_SESSION * session, int stmt_ndx,
		  bool * use_plan_cache, bool * use_query_cache)
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

  if (use_plan_cache)
    {
      if (statement->use_plan_cache)
	{
	  *use_plan_cache = true;
	}
      else
	{
	  *use_plan_cache = false;
	}
    }

  if (use_query_cache)
    {
      if (statement->use_query_cache)
	{
	  *use_query_cache = true;
	}
      else
	{
	  *use_query_cache = false;
	}
    }

  return true;
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
DB_SESSION_ERROR *
db_get_next_error (DB_SESSION_ERROR * errors, int *line, int *col)
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
DB_SESSION_WARNING *
db_get_next_warning (DB_SESSION_WARNING * warnings, int *line, int *col)
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
 * db_session_set_holdable () - mark session as holdable
 * return : void
 * session (in) :
 * holdable (in) :
 */
void
db_session_set_holdable (DB_SESSION * session, bool holdable)
{
  if (session == NULL || session->parser == NULL)
    {
      return;
    }
  session->parser->is_holdable = holdable ? 1 : 0;
}

/*
 * db_session_set_return_generated_keys () - return generated keys for insert
 *					  statements
 * return : void
 * session (in) :
 * return_generated_keys (in) :
 */
void
db_session_set_return_generated_keys (DB_SESSION * session,
				      bool return_generated_keys)
{
  if (session == NULL || session->parser == NULL)
    {
      return;
    }
  session->parser->return_generated_keys = return_generated_keys ? 1 : 0;
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
	{
	  linecol->err_lineno = linecol->err_posno = 0;
	}
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
      result = marker->expected_domain;
      if (result == NULL)
	{
	  result = pt_node_to_db_domain (NULL, marker, NULL);
	}
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

  if (statement != NULL && statement->node_type == PT_EXECUTE_PREPARE)
    {
      return db_cp_query_type (session->type_list[stmt_ndx], true);
    }

  cmd_type = pt_node_to_cmd_type (statement);
  if (cmd_type == CUBRID_STMT_SELECT)
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
	    case CUBRID_STMT_CALL:
	      qtype->db_type = pt_node_to_db_type (statement);
	      break;
	    case CUBRID_STMT_INSERT:
	      /* the type of result of INSERT is object */
	      qtype->db_type = DB_TYPE_OBJECT;
	      break;
	    case CUBRID_STMT_GET_ISO_LVL:
	    case CUBRID_STMT_GET_TIMEOUT:
	    case CUBRID_STMT_GET_OPT_LVL:
	    case CUBRID_STMT_GET_TRIGGER:
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
      if (statement != NULL && statement->node_type == PT_EXECUTE_PREPARE)
	{
	  retval = statement->info.execute.stmt_type;
	}
      else
	{
	  retval = pt_node_to_cmd_type (statement);
	}
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
 * return : integer, negative implies error.
 * session(in): contains the SQL query that has been compiled
 * count(in): number of elements in in_values table
 * in_values(in): a table of host_variable initialized DB_VALUEs
 */
int
db_push_values (DB_SESSION * session, int count, DB_VALUE * in_values)
{
  PARSER_CONTEXT *parser;

  if (session)
    {
      parser = session->parser;
      if (parser)
	{
	  pt_set_host_variables (parser, count, in_values);

	  if (parser->host_var_count > 0 && parser->set_host_var == 0)
	    {
	      if (pt_has_error (session->parser))
		{
		  /* This error can occur when using the statement pooling */
		  pt_report_to_ersys (session->parser, PT_SEMANTIC);
		  /* forget about any previous compilation errors, if any */
		  pt_reset_error (session->parser);

		  return ER_PT_SEMANTIC;
		}
	    }
	}
    }

  return NO_ERROR;
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

  /* async execution mode is not supported */
  db_set_sync_flag (session, SYNC_EXEC);

  return NO_ERROR;
}

/*
 * db_execute_and_keep_statement_local() - This function executes the SQL
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
  int server_info_bits;

  SEMANTIC_CHK_INFO sc_info = { NULL, NULL, 0, 0, 0, false, false };
  DB_CLASS_MODIFICATION_STATUS cls_status = DB_CLASS_NOT_MODIFIED;

  if (result != NULL)
    {
      *result = NULL;
    }

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
  if (stmt_ndx < 0 || stmt_ndx >= session->dimension
      || !session->statements[stmt_ndx])
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE,
	      ER_OBJ_INVALID_ARGUMENTS, 0);
      return er_errid ();
    }

  /* valid host variable was not set before */
  if (session->parser->host_var_count > 0
      && session->parser->set_host_var == 0)
    {
      if (pt_has_error (session->parser))
	{
	  pt_report_to_ersys (session->parser, PT_SEMANTIC);
	  /* forget about any previous compilation errors, if any */
	  pt_reset_error (session->parser);
	}
      else
	{
	  /* parsed statement has some host variable parameters
	     (input marker '?'), but no host variable (DB_VALUE array) was set
	     by db_push_values() API */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_UCI_TOO_FEW_HOST_VARS,
		  0);
	}
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

  /* if the statement was not compiled and prepared, do it */
  if (session->stage[stmt_ndx] < StatementPreparedStage)
    {
      session->stmt_ndx = stmt_ndx;
      if (db_compile_statement_local (session) < 0)
	{
	  assert (er_errid () != NO_ERROR);
	  return er_errid ();
	}
    }

  if (session->ddl_stmts_for_replication != NULL)
    {
      parser->ddl_stmt_for_replication =
	session->ddl_stmts_for_replication[stmt_ndx];
    }

  /* forget about any previous compilation errors, if any */
  pt_reset_error (parser);

  /* get sys_date, sys_time, sys_timestamp, sys_datetime values from the server */
  server_info_bits = 0;		/* init */
  if (statement->si_datetime
      || (statement->node_type == PT_CREATE_ENTITY
	  || statement->node_type == PT_ALTER))
    {
      /* Some create and alter statement require the server timestamp
       * even though it does not explicitly refer timestamp-related pseudocolumns.
       * For instance,
       *   create table foo (a timestamp default systimestamp);
       *   create view v_foo as select * from foo;
       */
      db_calculate_current_server_time (parser);

      if (base_server_timeb.time == 0)
	{
	  server_info_bits |= SI_SYS_DATETIME;
	}
    }

  if (statement->si_tran_id && DB_IS_NULL (&parser->local_transaction_id))
    {
      /* if it was reset in the previous execution step, fills it now */
      server_info_bits |= SI_LOCAL_TRANSACTION_ID;
    }

  /* request to the server */
  if (server_info_bits)
    {
      err = qp_get_server_info (parser, server_info_bits);
      if (err != NO_ERROR)
	{
	  return err;
	}
    }

  if (server_info_bits & SI_SYS_DATETIME)
    {
      db_set_base_server_time (&parser->sys_datetime);
    }

  if (statement->node_type == PT_PREPARE_STATEMENT)
    {
      err = do_process_prepare_statement (session, statement);
      update_execution_values (parser, -1, CUBRID_MAX_STMT_TYPE);
      assert (result == NULL || *result == NULL);
      return err;
    }
  else if (statement->node_type == PT_EXECUTE_PREPARE)
    {
      bool do_recompile = false;
      if (statement->info.execute.stmt_type == CUBRID_STMT_SELECT)
	{
	  if (!statement->xasl_id || XASL_ID_IS_NULL (statement->xasl_id)
	      || statement->info.execute.recompile)
	    {
	      do_recompile = true;
	    }
	}
      else
	{
	  do_recompile = true;
	}

      if (do_recompile)
	{
	  return
	    do_recompile_and_execute_prepared_statement (session, statement,
							 result);
	}
    }
  else if (statement->node_type == PT_DEALLOCATE_PREPARE)
    {
      err = do_process_deallocate_prepare (session, statement);
      update_execution_values (parser, -1, CUBRID_MAX_STMT_TYPE);
      assert (result == NULL || *result == NULL);
      return err;
    }

  /* New interface of do_prepare_statement()/do_execute_statment() is used
     only when the XASL cache is enabled. If it is disabled, old interface
     of do_statement() will be used instead. do_statement() makes a XASL
     everytime rather than using XASL cache. Also, it can be executed in
     the server without touching the XASL cache by calling
     prepare_and_execute_query(). */
  do_Trigger_involved = false;

  pt_null_etc (statement);
  if (statement->xasl_id == NULL
      && ((cls_status = pt_has_modified_class (parser, statement))
	  != DB_CLASS_NOT_MODIFIED))
    {
      if (cls_status == DB_CLASS_MODIFIED)
	{
	  err = ER_QPROC_INVALID_XASLNODE;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 0);
	}
      else
	{
	  assert (cls_status == DB_CLASS_ERROR);
	  assert (er_errid () != NO_ERROR);
	  err = er_errid ();
	}
    }
  else if (prm_get_integer_value (PRM_ID_XASL_MAX_PLAN_CACHE_ENTRIES) > 0
	   && statement->cannot_prepare == 0)
    {
      /* now, execute the statement by calling do_execute_statement() */
      err = do_execute_statement (parser, statement);
      if (err == ER_QPROC_INVALID_XASLNODE
	  && session->stage[stmt_ndx] == StatementPreparedStage)
	{
	  /* The cache entry was deleted before 'execute' */
	  if (statement->xasl_id)
	    {
	      pt_free_statement_xasl_id (statement);
	    }

	  cls_status = pt_has_modified_class (parser, statement);
	  if (cls_status == DB_CLASS_NOT_MODIFIED)
	    {
	      /* forget all errors */
	      er_clear ();
	      pt_reset_error (parser);

	      /* retry the statement by calling do_prepare/execute_statement() */
	      if (do_prepare_statement (parser, statement) == NO_ERROR)
		{
		  err = do_execute_statement (parser, statement);
		}
	    }
	  else if (cls_status == DB_CLASS_MODIFIED)
	    {
	      err = ER_QPROC_INVALID_XASLNODE;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 0);
	    }
	  else
	    {
	      assert (cls_status == DB_CLASS_ERROR);
	      assert (er_errid () != NO_ERROR);
	      err = er_errid ();
	    }
	}
    }
  else
    {
      /* bind and resolve host variables */
      assert (parser->host_var_count >= 0 && parser->auto_param_count >= 0);
      if (parser->host_var_count > 0)
	{
	  assert (parser->set_host_var == 1);
	}
      if (parser->host_var_count > 0 || parser->auto_param_count > 0)
	{
	  /* In this case, pt_bind_values_to_hostvars() will change
	     PT_HOST_VAR node. Must duplicate the statement and execute with
	     the new one and free the copied one before returning */
	  statement = parser_copy_tree_list (parser, statement);
	  statement = mq_reset_ids_in_statement (parser, statement);

	  sc_info.top_node = statement;
	  sc_info.donot_fold = false;

	  if (!(statement = pt_bind_values_to_hostvars (parser, statement))
	      || !(statement = pt_resolve_names (parser, statement, &sc_info))
	      || !(statement = pt_semantic_type (parser, statement,
						 &sc_info)))
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
		  pt_reset_error (parser);
		}
	      if (statement != session->statements[stmt_ndx])
		{
		  parser_free_tree (parser, statement);
		}
	      assert (er_errid () != NO_ERROR);
	      return er_errid ();
	    }
	}

      err = do_statement (parser, statement);
    }

  do_Trigger_involved = false;
  if (err < 0)
    {
      /* Do not override original error id with */
      if (er_errid () == NO_ERROR
	  && pt_has_error (parser) && err != ER_QPROC_INVALID_XASLNODE)
	{
	  pt_report_to_ersys_with_statement (parser, PT_EXECUTION, statement);
	  err = er_errid ();
	}
      /* free the allocated list_id area before leaving */
      pt_free_query_etc_area (parser, statement);
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
	  if (qres == NULL)
	    {
	      assert (er_errid () != NO_ERROR);
	      err = er_errid ();
	    }
	}
      else
	{
	  CUBRID_STMT_TYPE stmt_type = pt_node_to_cmd_type (statement);
	  switch (stmt_type)
	    {
	    case CUBRID_STMT_SELECT:
	    case CUBRID_STMT_EXECUTE_PREPARE:
	      /* Check whether pt_new_query_result_descriptor() fails.
	         Similar tests are required for CUBRID_STMT_INSERT and
	         CUBRID_STMT_CALL cases. */
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
		  assert (er_errid () != NO_ERROR);
		  err = er_errid ();
		}
	      break;

	    case CUBRID_STMT_DO:
	      pt_free_query_etc_area (parser, statement);
	      break;

	    case CUBRID_STMT_GET_ISO_LVL:
	    case CUBRID_STMT_GET_TIMEOUT:
	    case CUBRID_STMT_GET_OPT_LVL:
	    case CUBRID_STMT_GET_TRIGGER:
	    case CUBRID_STMT_EVALUATE:
	    case CUBRID_STMT_CALL:
	    case CUBRID_STMT_INSERT:
	    case CUBRID_STMT_GET_STATS:
	      /* csql (in csql.c) may throw away any non-null *result,
	         but we create a DB_QUERY_RESULT structure anyway for other
	         callers of db_execute that use the *result like esql_cli.c  */
	      if (pt_is_server_insert_with_generated_keys (parser, statement))
		{
		  qres = pt_new_query_result_descriptor (parser, statement);
		  if (qres)
		    {
		      /* get number of rows as result */
		      qres->query_type =
			db_cp_query_type (session->type_list[stmt_ndx],
					  false);
		      qres->res.s.stmt_id = stmt_ndx;
		    }
		  else
		    {
		      assert (er_errid () != NO_ERROR);
		      err = er_errid ();
		    }
		  break;
		}

	      if (stmt_type == CUBRID_STMT_INSERT
		  && (statement->info.insert.server_allowed ==
		      SERVER_INSERT_IS_ALLOWED))
		{
		  val = db_value_create ();
		  if (val == NULL)
		    {
		      assert (er_errid () != NO_ERROR);
		      err = er_errid ();
		      break;
		    }
		  db_make_object (val, NULL);
		}
	      else
		{
		  val = (DB_VALUE *) pt_node_etc (statement);
		}

	      if (val)
		{
		  /* got a result, so use it */
		  qres = db_get_db_value_query_result (val);
		  if (qres)
		    {
		      /* get number of rows as result */
		      int row_count = err;
		      err = db_query_tuple_count (qres);
		      /* We have a special case for REPLACE INTO:
		         pt_node_etc (statement) holds only the inserted row
		         but we might have done a delete before.
		         For this case, if err>row_count we will not change
		         the row count */
		      if (stmt_type == CUBRID_STMT_INSERT)
			{
			  if ((DB_VALUE_DOMAIN_TYPE (val) == DB_TYPE_OBJECT
			       && DB_IS_NULL (val))
			      || (statement->info.insert.do_replace
				  && row_count > err))
			    {
			      err = row_count;
			    }
			}
		    }
		  else
		    {
		      assert (er_errid () != NO_ERROR);
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

  /* Do not override original error id with  */
  /* last error checking */
  if (er_errid () == NO_ERROR
      && pt_has_error (parser) && err != ER_QPROC_INVALID_XASLNODE)
    {
      pt_report_to_ersys_with_statement (parser, PT_EXECUTION, statement);
      err = er_errid ();
    }

  /* reset the parser values */
  if (statement->si_datetime)
    {
      db_make_null (&parser->sys_datetime);
      db_make_null (&parser->sys_epochtime);
    }
  if (statement->si_tran_id)
    {
      db_make_null (&parser->local_transaction_id);
    }

  update_execution_values (parser, err, pt_node_to_cmd_type (statement));

  /* free if the statement was duplicated for host variable binding */
  if (statement != session->statements[stmt_ndx])
    {
      parser_free_tree (parser, statement);
    }

  return err;
}

static void
update_execution_values (PARSER_CONTEXT * parser, int result,
			 CUBRID_STMT_TYPE statement_type)
{
  if (result < 0)
    {
      parser->execution_values.row_count = -1;
    }
  else if (statement_type == CUBRID_STMT_UPDATE
	   || statement_type == CUBRID_STMT_INSERT
	   || statement_type == CUBRID_STMT_DELETE)
    {
      parser->execution_values.row_count = result;
    }
  else
    {
      parser->execution_values.row_count = -1;
    }
  db_update_row_count_cache (parser->execution_values.row_count);
}

static void
copy_execution_values (EXECUTION_STATE_VALUES * source,
		       EXECUTION_STATE_VALUES * destination)
{
  assert (destination != NULL && source != NULL);
  destination->row_count = source->row_count;
}

static int
values_list_to_values_array (PARSER_CONTEXT * parser, PT_NODE * values_list,
			     DB_VALUE_ARRAY * values_array)
{
  DB_VALUE_ARRAY values;
  PT_NODE *current_value = values_list;
  int i = 0;
  int err = NO_ERROR;

  values.size = 0;
  values.vals = NULL;

  if (parser == NULL || values_array == NULL || values_array->size != 0
      || values_array->vals != NULL)
    {
      assert (false);
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS,
	      0);
      err = er_errid ();
      goto error_exit;
    }

  if (values_list == NULL)
    {
      return NO_ERROR;
    }
  while (current_value != NULL)
    {
      values.size++;
      current_value = current_value->next;
    }

  values.vals = malloc (values.size * sizeof (DB_VALUE));
  if (values.vals == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      values.size * sizeof (DB_VALUE));
      err = er_errid ();
      goto error_exit;
    }

  for (i = 0; i < values.size; ++i)
    {
      db_make_null (&values.vals[i]);
    }
  for (current_value = values_list, i = 0;
       current_value != NULL; current_value = current_value->next, ++i)
    {
      if (current_value->node_type == PT_EXPR
	  && current_value->info.expr.op == PT_EVALUATE_VARIABLE)
	{
	  /* this is a session variable */
	  DB_VALUE val;
	  DB_VALUE *name;

	  assert (current_value->info.expr.arg1->node_type == PT_VALUE);

	  name = pt_value_to_db (parser, current_value->info.expr.arg1);
	  DB_MAKE_NULL (&val);
	  if (db_get_variable (name, &val) != NO_ERROR)
	    {
	      assert (er_errid () != NO_ERROR);
	      err = er_errid ();
	      goto error_exit;
	    }
	  pr_clone_value (&val, &values.vals[i]);
	  pr_clear_value (&val);
	}
      else
	{
	  DB_VALUE *db_val = NULL;
	  int more_type_info_needed = 0;
	  db_val = pt_value_to_db (parser, current_value);
	  if (db_val == NULL)
	    {
	      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE,
		      ER_OBJ_INVALID_ARGUMENTS, 0);
	      err = er_errid ();
	      goto error_exit;
	    }
	  pr_clone_value (db_val, &values.vals[i]);
	}
    }

  values_array->size = values.size;
  values_array->vals = values.vals;
  values.size = 0;
  values.vals = NULL;

  return err;

error_exit:

  if (values.vals != NULL)
    {
      db_value_clear_array (&values);
      free_and_init (values.vals);
      values.size = 0;
    }
  return err;
}

static int
set_prepare_info_into_list (DB_PREPARE_INFO * prepare_info,
			    PT_NODE * statement)
{
  int length = 0;
  PT_NODE *name = NULL;

  assert (prepare_info->into_list == NULL);

  prepare_info->into_count = 0;

  if (pt_node_to_cmd_type (statement) != CUBRID_STMT_SELECT)
    {
      return NO_ERROR;
    }

  if (statement->info.query.into_list == NULL)
    {
      return NO_ERROR;
    }

  length = pt_length_of_list (statement->info.query.into_list);
  if (length == 0)
    {
      return NO_ERROR;
    }

  prepare_info->into_list = (char **) malloc (length * sizeof (char *));
  if (prepare_info->into_list == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      length * sizeof (char *));
      goto error;
    }
  name = statement->info.query.into_list;
  length = 0;
  while (name)
    {
      if (PT_IS_NAME_NODE (name))
	{
	  if (name->info.name.original == NULL)
	    {
	      prepare_info->into_list[length] = NULL;
	    }
	  else
	    {
	      char *into_name =
		(char *) malloc (strlen (name->info.name.original) + 1);
	      if (into_name == NULL)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_OUT_OF_VIRTUAL_MEMORY, 1,
			  strlen (name->info.name.original) + 1);
		  goto error;
		}
	      memcpy (into_name, name->info.name.original,
		      strlen (name->info.name.original));
	      into_name[strlen (name->info.name.original)] = 0;
	      prepare_info->into_list[length] = into_name;
	    }
	}
      else
	{
	  prepare_info->into_list[length] = NULL;
	}
      length++;
      name = name->next;
    }

  prepare_info->into_count = length;
  return NO_ERROR;

error:
  if (prepare_info->into_list != NULL)
    {
      int i = 0;
      for (i = 0; i < length; i++)
	{
	  if (prepare_info->into_list[i] != NULL)
	    {
	      free_and_init (prepare_info->into_list[i]);
	    }
	}
      free_and_init (prepare_info->into_list);
    }
  return ER_FAILED;
}

static PT_NODE *
char_array_to_name_list (PARSER_CONTEXT * parser, char **names, int length)
{
  PT_NODE *name = NULL;
  PT_NODE *list = NULL;
  int i;

  for (i = 0; i < length; i++)
    {
      name = pt_name (parser, names[i]);
      list = parser_append_node (name, list);
    }

  return list;
}

/*
 * do_process_prepare_statement () - execute a 'PREPARE STMT FROM ...'
 *				     statement
 * return:   error code or NO_ERROR
 * session (in)	  : client session for this statement
 * statement (in) : the statement
 */
static int
do_process_prepare_statement (DB_SESSION * session, PT_NODE * statement)
{
  DB_PREPARE_INFO prepare_info;
  DB_SESSION *prepared_session = NULL;
  int prepared_statement_ndx = 0;
  PT_NODE *prepared_stmt = NULL;
  int include_oids = 0;
  const char *const name = statement->info.prepare.name->info.name.original;
  const char *const statement_literal =
    (char *) statement->info.prepare.statement->info.value.data_value.str->
    bytes;
  int err = NO_ERROR;
  char *stmt_info = NULL;
  int info_len = 0;
  assert (statement->node_type == PT_PREPARE_STATEMENT);
  db_init_prepare_info (&prepare_info);

  prepared_session = db_open_buffer_local (statement_literal);
  if (prepared_session == NULL)
    {
      assert (er_errid () != NO_ERROR);
      err = er_errid ();
      goto cleanup;
    }

  /* we need to copy all the relevant settings */
  prepared_session->include_oid = session->include_oid;

  prepared_statement_ndx = db_compile_statement_local (prepared_session);
  if (prepared_statement_ndx < 0)
    {
      err = prepared_statement_ndx;
      goto cleanup;
    }

  err = db_check_single_query (prepared_session);
  if (err != NO_ERROR)
    {
      err = ER_IT_MULTIPLE_STATEMENT;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IT_MULTIPLE_STATEMENT, 0);
      goto cleanup;
    }

  assert (prepared_statement_ndx == 1);
  assert (prepared_session->dimension == 1);
  assert (prepared_session->statements[0] != NULL);

  prepared_stmt = prepared_session->statements[0];

  if (!is_allowed_as_prepared_statement (prepared_stmt))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_IT_IS_DISALLOWED_AS_PREPARED, 0);
      err = ER_FAILED;
      goto cleanup;
    }

  if (prepared_session->parser->host_var_count > 0
      && !is_allowed_as_prepared_statement_with_hv (prepared_stmt))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_CANNOT_PREPARE_WITH_HOST_VAR, 1,
	      pt_show_node_type (prepared_session->statements[0]));
      err = ER_FAILED;
      goto cleanup;
    }

  /* set statement literal */
  prepare_info.statement = (char *) statement_literal;
  /* set columns */
  prepare_info.columns = prepared_session->type_list[0];
  /* set statement type */
  prepare_info.stmt_type = pt_node_to_cmd_type (prepared_stmt);
  /* set host variables */
  prepare_info.host_variables.size =
    prepared_session->parser->host_var_count
    + prepared_session->parser->auto_param_count;
  prepare_info.host_variables.vals = prepared_session->parser->host_variables;
  prepare_info.host_var_expected_domains =
    prepared_session->parser->host_var_expected_domains;
  /* set autoparam count */
  prepare_info.auto_param_count = prepared_session->parser->auto_param_count;
  /* set recompile */
  prepare_info.recompile = prepared_stmt->recompile;
  /* set OIDs included */
  if (prepare_info.stmt_type == CUBRID_STMT_SELECT)
    {
      prepare_info.oids_included = prepared_stmt->info.query.oids_included;
    }

  err = set_prepare_info_into_list (&prepare_info, prepared_stmt);
  if (err != NO_ERROR)
    {
      goto cleanup;
    }

  err = db_pack_prepare_info (&prepare_info, &stmt_info);
  if (err < 0)
    {
      goto cleanup;
    }
  info_len = err;

  err = csession_create_prepared_statement (name, prepared_stmt->alias_print,
					    stmt_info, info_len);

cleanup:
  if (err < 0 && name != NULL)
    {
      /* clear the previously cached one with the same name if exists */
      er_stack_push ();
      csession_delete_prepared_statement (name);
      er_stack_pop ();
    }

  if (stmt_info != NULL)
    {
      free_and_init (stmt_info);
    }

  if (prepared_session)
    {
      db_close_session_local (prepared_session);
    }

  if (prepare_info.into_list != NULL)
    {
      int i = 0;
      for (i = 0; i < prepare_info.into_count; i++)
	{
	  free_and_init (prepare_info.into_list[i]);
	}
      free_and_init (prepare_info.into_list);
    }

  return err;
}

/*
 * do_get_prepared_statement_info () - get prepared statement information
 * return : error code or NO_ERROR
 * session (in) : client session context
 * stmt_idx (in) : statement index
 */
static int
do_get_prepared_statement_info (DB_SESSION * session, int stmt_idx)
{
  const char *name = NULL;
  char *stmt_info = NULL;
  XASL_ID xasl_id;
  int err = NO_ERROR, i = 0;
  DB_VALUE *hv = NULL;
  PT_NODE *statement = session->statements[stmt_idx];
  PARSER_CONTEXT *parser = session->parser;
  DB_PREPARE_INFO prepare_info;
  DB_QUERY_TYPE *col = NULL;
  XASL_NODE_HEADER xasl_header;

  assert (pt_node_to_cmd_type (statement) == CUBRID_STMT_EXECUTE_PREPARE);
  db_init_prepare_info (&prepare_info);

  name = statement->info.execute.name->info.name.original;
  err =
    csession_get_prepared_statement (name, &xasl_id, &stmt_info,
				     &xasl_header);
  if (err != NO_ERROR)
    {
      return err;
    }

  db_unpack_prepare_info (&prepare_info, stmt_info);

  statement->info.execute.column_count = 0;
  col = prepare_info.columns;
  while (col)
    {
      statement->info.execute.column_count++;
      col = col->next;
    }

  /* set session type list */
  session->type_list[stmt_idx] = prepare_info.columns;

  statement->info.execute.into_list =
    char_array_to_name_list (session->parser, prepare_info.into_list,
			     prepare_info.into_count);

  statement->info.execute.stmt_type = prepare_info.stmt_type;

  /* set query */
  statement->info.execute.query =
    pt_make_string_value (parser, prepare_info.statement);
  if (statement->info.execute.query == NULL)
    {
      PT_INTERNAL_ERROR (parser, "allocate new node");
    }
  statement->info.execute.recompile = prepare_info.recompile;
  statement->info.execute.oids_included = prepare_info.oids_included;

  XASL_ID_COPY (&statement->info.execute.xasl_id, &xasl_id);

  /* restore host variables used by this statement */
  for (i = 0, hv = parser->host_variables;
       i < parser->host_var_count + parser->auto_param_count; i++, hv++)
    {
      pr_clear_value (hv);
    }

  if (parser->host_variables)
    {
      free_and_init (parser->host_variables);
    }

  if (parser->host_var_expected_domains)
    {
      free_and_init (parser->host_var_expected_domains);
    }

  parser->auto_param_count = 0;
  parser->host_var_count = 0;

  parser->auto_param_count = prepare_info.auto_param_count;
  parser->host_variables = prepare_info.host_variables.vals;
  parser->host_var_expected_domains = prepare_info.host_var_expected_domains;
  parser->host_var_count =
    prepare_info.host_variables.size - prepare_info.auto_param_count;

  err = do_set_user_host_variables (session,
				    statement->info.execute.using_list);
  if (err != NO_ERROR)
    {
      goto cleanup;
    }

  parser->host_var_count += prepare_info.auto_param_count;
  parser->auto_param_count = 0;
  parser->set_host_var = 1;

  /* Multi range optimization check:
   * if host-variables were used (not auto-parameterized), the orderby_num ()
   * limit may change and invalidate or validate multi range optimization.
   * Check if query needs to be recompiled.
   */
  if (!XASL_ID_IS_NULL (&xasl_id)	/* xasl_id should not be null */
      && !statement->info.execute.recompile	/* recompile is already planned */
      && (prepare_info.host_variables.size > prepare_info.auto_param_count))
    {
      /* query has to be multi range opt candidate */
      if (xasl_header.xasl_flag & (MRO_CANDIDATE | MRO_IS_USED
				   | SORT_LIMIT_CANDIDATE | SORT_LIMIT_USED))
	{
	  if (db_check_limit_need_recompile (parser, statement,
					     xasl_header.xasl_flag))
	    {
	      /* need recompile, set XASL_ID to NULL */
	      XASL_ID_SET_NULL (&statement->info.execute.xasl_id);
	    }
	}
    }

cleanup:
  if (stmt_info != NULL)
    {
      free_and_init (stmt_info);
    }
  if (prepare_info.statement != NULL)
    {
      free_and_init (prepare_info.statement);
    }
  if (prepare_info.into_list != NULL)
    {
      for (i = 0; i < prepare_info.into_count; i++)
	{
	  if (prepare_info.into_list[i] != NULL)
	    {
	      free_and_init (prepare_info.into_list[i]);
	    }
	}
      free_and_init (prepare_info.into_list);
    }
  return err;
}

/*
 * do_cast_host_variables_to_expected_domain () - After compilation phase,
 *						  cast all host variables to
 *						  their expected domains
 *
 * return	: error code
 * session (in) : db_session
 */
static int
do_cast_host_variables_to_expected_domain (DB_SESSION * session)
{
  int hv_count = session->parser->host_var_count;
  DB_VALUE *host_vars = session->parser->host_variables;
  TP_DOMAIN **expected_domains = session->parser->host_var_expected_domains;
  DB_VALUE *hv = NULL;
  TP_DOMAIN *hv_dom = NULL, *d = NULL;
  int i = 0;

  for (i = 0; i < hv_count; i++)
    {
      hv = &host_vars[i];
      hv_dom = expected_domains[i];
      if (TP_DOMAIN_TYPE (hv_dom) == DB_TYPE_UNKNOWN
	  || hv_dom->type->id == DB_TYPE_ENUMERATION)
	{
	  /* skip casting enum and unknown type values */
	  continue;
	}
      if (tp_value_cast_preserve_domain (hv, hv, hv_dom, false, true) !=
	  DOMAIN_COMPATIBLE)
	{
	  d = pt_type_enum_to_db_domain (TP_DOMAIN_TYPE (hv_dom));
	  PT_ERRORmf2 (session->parser, NULL, MSGCAT_SET_PARSER_SEMANTIC,
		       MSGCAT_SEMANTIC_CANT_COERCE_TO, "host var", d);
	  tp_domain_free (d);
	  pt_report_to_ersys (session->parser, PT_EXECUTION);
	  pt_reset_error (session->parser);
	  return ER_PT_EXECUTE;
	}
    }

  return NO_ERROR;
}

/*
 * do_set_user_host_variables () - Set host variables values in parser from
 *				   using_list
 *
 * return	   : error code
 * session (in)	   : db_session
 * using_list (in) : list of db_values
 */
static int
do_set_user_host_variables (DB_SESSION * session, PT_NODE * using_list)
{
  DB_VALUE_ARRAY values_array;
  int err = NO_ERROR;

  values_array.size = 0;
  values_array.vals = NULL;

  if (values_list_to_values_array (session->parser, using_list, &values_array)
      != NO_ERROR)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  if (session->parser->host_var_count != values_array.size)
    {
      err = ER_IT_INCORRECT_HOSTVAR_COUNT;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 2, values_array.size,
	      session->parser->host_var_count);
    }
  else
    {
      err = db_push_values (session, values_array.size, values_array.vals);
    }

  db_value_clear_array (&values_array);
  free_and_init (values_array.vals);
  values_array.size = 0;

  return err;
}

/*
 * db_check_limit_need_recompile () - Check if statement has to be recompiled
 *				      for limit optimizations with supplied
 *				      limit value
 *
 * return	  : true if recompile is needed, false otherwise
 * parser (in)	  : parser context for statement
 * statement (in) : execute prepare statement
 * xasl_flag (in) : flag specifying limit optimizations used in XASL
 *
 * NOTE: This function attempts to evaluate superior limit for orderby_num ()
 *	 without doing a full statement recompile.
 */
static bool
db_check_limit_need_recompile (PARSER_CONTEXT * parent_parser,
			       PT_NODE * statement, int xasl_flag)
{
  DB_SESSION *session = NULL;
  PT_NODE *query = NULL, *limit = NULL, *orderby_for = NULL;
  bool cannot_eval = false;
  bool use_mro = false;
  bool do_recompile = false;
  DB_VALUE *save_host_variables = NULL;
  TP_DOMAIN **save_host_var_expected_domains = NULL;
  int save_host_var_count, save_auto_param_count;

  if (statement->node_type != PT_EXECUTE_PREPARE)
    {
      /* statement must be execute prepare */
      return false;
    }
  if (statement->info.execute.stmt_type != CUBRID_STMT_SELECT)
    {
      return false;
    }

  assert (statement->info.execute.query->node_type == PT_VALUE);
  assert (statement->info.execute.query->type_enum == PT_TYPE_CHAR);

  session =
    db_open_buffer_local ((char *) statement->info.execute.query->info.value.
			  data_value.str->bytes);
  if (session == NULL)
    {
      /* error opening session */
      return false;
    }

  if (session->dimension != 1)
    {
      /* need full recompile */
      do_recompile = true;
      goto exit;
    }
  query = session->statements[0];
  assert (PT_IS_QUERY (query));

  /* set host variable info */
  save_auto_param_count = session->parser->auto_param_count;
  save_host_var_count = session->parser->host_var_count;
  save_host_variables = session->parser->host_variables;
  save_host_var_expected_domains = session->parser->host_var_expected_domains;

  session->parser->host_variables = parent_parser->host_variables;
  session->parser->host_var_expected_domains =
    parent_parser->host_var_expected_domains;
  session->parser->host_var_count = parent_parser->host_var_count;
  session->parser->auto_param_count = parent_parser->auto_param_count;
  session->parser->set_host_var = 1;

  if (pt_recompile_for_limit_optimizations (session->parser, query,
					    xasl_flag))
    {
      /* need recompile */
      do_recompile = true;
    }

  /* restore host variable info */
  session->parser->host_variables = save_host_variables;
  session->parser->host_var_expected_domains = save_host_var_expected_domains;
  session->parser->auto_param_count = save_auto_param_count;
  session->parser->host_var_count = save_host_var_count;
  session->parser->set_host_var = 0;

exit:
  /* clean up */
  if (session != NULL)
    {
      db_close_session (session);
    }
  return do_recompile;
}

/*
 * do_recompile_and_execute_prepared_statement () - compile and execute a
 *						    prepared statement
 * return : error code or NO_ERROR
 * session (in)   : client session context
 * statement (in) : statement to be executed
 * result (out)   : execution result
 */
static int
do_recompile_and_execute_prepared_statement (DB_SESSION * session,
					     PT_NODE * statement,
					     DB_QUERY_RESULT ** result)
{
  int err = NO_ERROR;
  int idx = 0;
  DB_SESSION *new_session = NULL;
  assert (statement->info.execute.query->node_type == PT_VALUE);
  assert (statement->info.execute.query->type_enum == PT_TYPE_CHAR);

  new_session =
    db_open_buffer_local ((char *) statement->info.execute.query->info.value.
			  data_value.str->bytes);
  if (new_session == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  new_session->is_subsession_for_prepared = true;

  /* add the new session to the subsessions list */
  if (session->next == NULL)
    {
      session->next = new_session;
    }
  else
    {
      new_session->next = session->next;
      session->next = new_session;
    }

  if (statement->info.execute.recompile)
    {
      new_session->statements[0]->recompile =
	statement->info.execute.recompile;
    }

  /* set host variable values in new session */
  assert (session->parser->set_host_var == 1);
  err =
    do_set_user_host_variables (new_session,
				statement->info.execute.using_list);
  if (err != NO_ERROR)
    {
      return err;
    }
  new_session->parser->set_host_var = 0;
  idx = db_compile_statement (new_session);
  if (idx < 0)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  /* cast host variables to their expected domain */
  err = do_cast_host_variables_to_expected_domain (new_session);
  if (err != NO_ERROR)
    {
      return err;
    }
  new_session->parser->set_host_var = 1;

  new_session->parser->is_holdable = session->parser->is_holdable;
  return db_execute_and_keep_statement_local (new_session, 1, result);
}

/*
 * do_process_deallocate_prepare () - deallocate a prepared statement
 * return:   error code or NO_ERROR
 * session (in)	  : client session context
 * statement (in) : statement to be deallocated
 */
static int
do_process_deallocate_prepare (DB_SESSION * session, PT_NODE * statement)
{
  const char *const name = statement->info.prepare.name->info.name.original;
  return csession_delete_prepared_statement (name);
}

/*
 * is_allowed_as_prepared_statement () - check if node type is a valid
 *					 prepared statement
 * return:    true if node is valid prepared statement, false otherwise
 *  node (in): parse tree node to check
 */
static bool
is_allowed_as_prepared_statement (PT_NODE * node)
{
  assert (node);

  switch (node->node_type)
    {
    case PT_PREPARE_STATEMENT:
    case PT_EXECUTE_PREPARE:
    case PT_DEALLOCATE_PREPARE:
      return false;

    default:
      return true;
    }
}

/*
 * is_allowed_as_prepared_statement_with_hv () - check if node type is a valid
 *					         prepared statement that can
 *						 accept hostvars
 * return:    true if node is valid prepared statement, false otherwise
 *  node (in): parse tree node to check
 */
static bool
is_allowed_as_prepared_statement_with_hv (PT_NODE * node)
{
  assert (node);

  switch (node->node_type)
    {
    case PT_SELECT:
    case PT_UNION:
    case PT_DIFFERENCE:
    case PT_INTERSECTION:

    case PT_INSERT:
    case PT_UPDATE:
    case PT_DELETE:
    case PT_MERGE:

    case PT_DO:
    case PT_METHOD_CALL:
    case PT_SET_SESSION_VARIABLES:
    case PT_EVALUATE:
      return true;

    case PT_CREATE_ENTITY:
      return (node->info.create_entity.entity_type == PT_CLASS);

    default:
      return false;
    }
}


/*
 * db_has_modified_class()
 *
 *   return:
 *   session(in):
 *   stmt_id(in):
 */
DB_CLASS_MODIFICATION_STATUS
db_has_modified_class (DB_SESSION * session, int stmt_id)
{
  DB_CLASS_MODIFICATION_STATUS cls_status;
  PT_NODE *statement;

  assert (session != NULL);
  assert (stmt_id < session->dimension);

  cls_status = DB_CLASS_NOT_MODIFIED;
  if (stmt_id < session->dimension)
    {
      statement = session->statements[stmt_id];
      if (statement != NULL)
	{
	  cls_status = pt_has_modified_class (session->parser, statement);
	}
    }

  return cls_status;
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

  db_invalidate_mvcc_snapshot_after_statement ();

  return err;
}

/*
 * db_execute_statement_local() - This function executes the SQL statement
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

  if (session == NULL)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS,
	      0);
      return ER_OBJ_INVALID_ARGUMENTS;
    }

  err = db_execute_and_keep_statement_local (session, stmt_ndx, result);

  statement = session->statements[stmt_ndx - 1];
  if (statement != NULL)
    {
      /* free XASL_ID allocated by query_prepare()
         before freeing the statement */
      pt_free_statement_xasl_id (statement);
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
 *
 * NOTE: db_execute_statement should be used only as entry point for statement
 *	 execution. Otherwise, db_execute_statement_local should be used.
 */
int
db_execute_statement (DB_SESSION * session, int stmt_ndx,
		      DB_QUERY_RESULT ** result)
{
  int err;

  CHECK_CONNECT_MINUSONE ();

  err = db_execute_statement_local (session, stmt_ndx, result);

  db_invalidate_mvcc_snapshot_after_statement ();

  return err;
}

/*
 * db_open_buffer_and_compile_first_statement () - The function will open
 *						   buffer for SQL query string
 *						   and will compile the first
 *						   statement in the list.
 *
 * return		 : Error code.
 * CSQL_query (in)	 : SQL query string.
 * query_error (in)	 : Saved query error for output.
 * include_oid (in)	 : Include OID mode.
 * session (out)	 : Generated session.
 * stmt_no (out)	 : Compiled statement number.
 */
int
db_open_buffer_and_compile_first_statement (const char *CSQL_query,
					    DB_QUERY_ERROR * query_error,
					    int include_oid,
					    DB_SESSION ** session,
					    int *stmt_no)
{
  int error = NO_ERROR;
  DB_SESSION_ERROR *errs;

  CHECK_CONNECT_ERROR ();

  /* Open buffer and generate session */
  *session = db_open_buffer_local (CSQL_query);
  if (*session == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return (er_errid ());
    }

  /* Compile the statement */
  db_include_oid (*session, include_oid);
  *stmt_no = db_compile_statement_local (*session);

  errs = db_get_errors (*session);
  if (errs != NULL)
    {
      int line, col;

      (void) db_get_next_error (errs, &line, &col);

      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      if (query_error)
	{
	  query_error->err_lineno = line;
	  query_error->err_posno = col;
	}
    }

  if (*stmt_no < 0 || error < 0)
    {
      db_close_session_local (*session);
      *session = NULL;
      assert (er_errid () != NO_ERROR);
      return (er_errid ());
    }

  return error;
}

/*
 * db_compile_and_execute_local () - Default compile and execute for local
 *				     calls. See description for
 *				     db_compile_and_execute_queries_internal.
 *
 * return	     : Error code.
 * CSQL_query (in)   : SQL query string.
 * result (out)	     : Statements results.
 * query_error (out) : Saved query error for output.
 *
 * NOTE: Do not call this function as statement execution entry point. It is
 *	 targeted for internal execution calls only!.
 */
int
db_compile_and_execute_local (const char *CSQL_query, void *result,
			      DB_QUERY_ERROR * query_error)
{
  /* Default local compile & execute statements will use:
   * - No oids for include OID mode.
   * - True for execution, not compile only.
   * - Synchronous execution.
   * - This is called during other statement execution, so false for new
   *   statements.
   */
  return db_compile_and_execute_queries_internal (CSQL_query, result,
						  query_error, DB_NO_OIDS, 1,
						  SYNC_EXEC, false);
}

/*
 * db_compile_and_execute_queries_internal () - Compiles CSQL_query, executes
 *						all statements and returns
 *						results.
 *
 * return		 : Error code.
 * CSQL_query (in)	 : SQL query string.
 * result (out)		 : Statements results.
 * query_error (out)	 : Saved query error for output.
 * include_oid (in)	 : Include OID mode.
 * execute (in)		 : True if query should also be executed. If argument
 *			   is false, it will only be compiled.
 * exec_mode (in)	 : Synchronous/Asynchronous execution mode.
 * is_new_statement (in) : True these are new statements. If false they are
 *			   considered as sub-execution for another statement.
 *
 * NOTE: If executed statements are not part of another statement execution,
 *	 before compiling each statement, the snapshot for current transaction
 *	 must be invalidated.
 */
int
db_compile_and_execute_queries_internal (const char *CSQL_query,
					 void *result,
					 DB_QUERY_ERROR * query_error,
					 int include_oid, int execute,
					 QUERY_EXEC_MODE exec_mode,
					 bool is_new_statement)
{
  int error;			/* return code from funcs */
  int stmt_no;			/* compiled stmt number */
  DB_SESSION *session = NULL;

  if (result)
    {
      /* Initialize result */
      *(char **) result = NULL;
    }

  /* Open buffer and compile first statement */
  error =
    db_open_buffer_and_compile_first_statement (CSQL_query, query_error,
						include_oid, &session,
						&stmt_no);
  if (session == NULL)
    {
      /* In case of error, the session is freed */
      return error;
    }

#if defined(CS_MODE)
  /* Pass exec_mode (used for Streaming (asynchronous) queries */
  db_set_sync_flag (session, exec_mode);
#else
  /*
   * In standalone mode, only synchronous queries are supported because there
   * is no thread support in the standalone case.
   */
  db_set_sync_flag (session, SYNC_EXEC);
#endif /* CS_MODE */

  if (execute)
    {
      /* Execute query and compile next one as long as there are statements
       * left.
       */
      while (stmt_no > 0)
	{
	  /* Execute current query */
	  error =
	    db_execute_statement_local (session, stmt_no,
					(DB_QUERY_RESULT **) result);
	  if (error < 0)
	    {
	      break;
	    }
	  /* Need to compile and execute next query. Make sure that current
	   * MVCC Snapshot is invalidated for READ COMMITTED isolation.
	   */
	  if (is_new_statement)
	    {
	      db_invalidate_mvcc_snapshot_after_statement ();
	    }
	  /* Compile a new statement */
	  stmt_no = db_compile_statement_local (session);
	}
    }
  else if (result)
    {
      /* Save query types as result */
      *(DB_QUERY_TYPE **) result = db_get_query_type_list (session, stmt_no);
      if (is_new_statement)
	{
	  db_invalidate_mvcc_snapshot_after_statement ();
	}
    }

  db_close_session_local (session);

  return error;
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
      pt_free_statement_xasl_id (statement);
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
	  pt_free_statement_xasl_id (statement);
	  parser_free_tree (session->parser, statement);
	  session->statements[stmt] = NULL;
	  session->stage[stmt] = StatementInitialStage;
	}
    }
  session->dimension = session->stmt_ndx = 0;
}

/*
 * db_close_session_local() - This function frees all resources of this session
 *    except query results
 * return : void
 * session(in) : session handle
 */
void
db_close_session_local (DB_SESSION * session)
{
  PARSER_CONTEXT *parser;
  DB_SESSION *prepared;
  int i;

  if (!session)
    {
      return;
    }
  prepared = session->next;
  while (prepared)
    {
      DB_SESSION *next = prepared->next;
      assert (prepared->is_subsession_for_prepared);
      prepared->next = NULL;
      db_close_session_local (prepared);
      prepared = next;
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
	      pt_free_statement_xasl_id (statement);
	      parser_free_tree (parser, statement);
	      session->statements[i] = NULL;
	    }
	}
    }

  parser->ddl_stmt_for_replication = NULL;

  if (session->ddl_stmts_for_replication != NULL)
    {
      free_and_init (session->ddl_stmts_for_replication);
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
      free_and_init (parser->host_variables);
    }

  if (parser->host_var_expected_domains)
    {
      free_and_init (parser->host_var_expected_domains);
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
 * db_get_all_chosen_classes() - This function returns list of all classes
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
      lmops = locator_get_all_class_mops (DB_FETCH_READ, p);
      /* probably should make sure
       * we push here because the list could be long */
      if (lmops != NULL)
	{
	  for (i = 0; i < lmops->num; i++)
	    {
	      /* is it necessary to have this check ? */
	      if (!WS_IS_DELETED (lmops->mops[i])
		  && lmops->mops[i] != sm_Root_class_mop)
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
 * db_get_all_vclasses_on_ldb() - This function returns list of all ldb
 *    virtual classes
 * return : list of all ldb virtual class objects if all OK,
 *	    NULL otherwise.
 */
DB_OBJLIST *
db_get_all_vclasses_on_ldb (void)
{
  return NULL;
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
 *    compatible with a given {vclass} object
 * return  : an ER status code if an error was found, NO_ERROR otherwise.
 * vclass(in) : an {vclass} object
 * query_spec(in) : a query specification string
 */
int
db_validate_query_spec (DB_OBJECT * vclass, const char *query_spec)
{
  PARSER_CONTEXT *parser = NULL;
  PT_NODE **spec = NULL;
  int rc = NO_ERROR;
  const char *const vclass_name = db_get_class_name (vclass);

  if (vclass_name == NULL)
    {
      rc = ER_OBJ_INVALID_ARGUMENTS;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, rc, 0);
      return rc;
    }

  if (!db_is_vclass (vclass))
    {
      rc = ER_SM_NOT_A_VIRTUAL_CLASS;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, rc, 1, vclass_name);
      return rc;
    }

  parser = parser_create_parser ();
  if (parser == NULL)
    {
      rc = ER_GENERIC_ERROR;
      return rc;
    }

  spec = parser_parse_string_use_sys_charset (parser, query_spec);
  if (spec != NULL && !pt_has_error (parser))
    {
      rc = pt_validate_query_spec (parser, *spec, vclass);
    }
  else
    {
      pt_report_to_ersys (parser, PT_SYNTAX);
      rc = er_errid ();
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
  const char *att_name, *cond;

  if (!att
      || db_attribute_is_shared (att)
      || !(att_name = db_attribute_name (att)))
    {
      return NULL;
    }

  switch (db_attribute_type (att))
    {
    case DB_TYPE_INTEGER:
    case DB_TYPE_FLOAT:
    case DB_TYPE_DOUBLE:
    case DB_TYPE_SHORT:
    case DB_TYPE_BIGINT:
    case DB_TYPE_MONETARY:
      cond = " = 1 ";
      break;

    case DB_TYPE_STRING:
      cond = " = 'x' ";
      break;

    case DB_TYPE_OBJECT:
      cond = " is null ";
      break;

    case DB_TYPE_SET:
    case DB_TYPE_MULTISET:
    case DB_TYPE_SEQUENCE:
      cond = " = {} ";
      break;

    case DB_TYPE_TIME:
      cond = " = '09:30' ";
      break;

    case DB_TYPE_TIMESTAMP:
      cond = " = '10/15/1986 5:45 am' ";
      break;

    case DB_TYPE_DATETIME:
      cond = " = '10/15/1986 5:45:15.135 am' ";
      break;

    case DB_TYPE_DATE:
      cond = " = '10/15/1986' ";
      break;

    default:
      return NULL;
    }

  snprintf (predicate, sizeof (predicate) - 1, "%s%s", att_name, cond);
  return predicate;
}

/*
 * db_validate() - This function checks if a {class|vclass} definition
 *    is reasonable
 * returns  : an ER status code if an error was found, NO_ERROR otherwise.
 * vc(in) : a {class|vclass} object
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
		  if (newbuf == NULL)
		    {
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			      ER_OUT_OF_VIRTUAL_MEMORY, 1,
			      limit * sizeof (char));
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

      retval = db_compile_and_execute_local (bufp, &result, NULL);
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
 * db_check_single_query() - This function checks to see if there is only
 *    one statement given, and that it is a valid query statement.
 * return : error code
 * session(in) : session handle
 */
int
db_check_single_query (DB_SESSION * session)
{
  if (session->dimension > 1)
    {
      return ER_IT_MULTIPLE_STATEMENT;
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
  return pt_associate_label_with_value_check_reference (name, value);
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
  /* async execution mode is not supported */
  return false;
#if 0
  PT_NODE *statement;
  bool sync;

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

  if (pt_node_to_cmd_type (statement) != CUBRID_STMT_SELECT)
    {
      return false;
    }

  sync = ((pt_statement_have_methods (session->parser, statement)
	   || (statement->node_type == PT_SELECT
	       && statement->is_click_counter)) ? true : false);

  return !sync;
#endif
}

/*
 * db_invalidate_mvcc_snapshot_after_statement () - When MVCC is enabled,
 *						    server uses a snapshot to
 *						    filter data. Snapshot is
 *						    obtained with the first
 *						    fetch or execution on
 *						    server and should be
 *						    invalidated before
 *						    executing a new statement.
 *
 * return : Void.
 *
 * NOTE: When Repeatable Reads and Serializable Isolation are implemented for
 *	 MVCC, snapshot must be invalidated only on commit/rollback.
 */
void
db_invalidate_mvcc_snapshot_after_statement (void)
{
  if (!prm_get_bool_value (PRM_ID_MVCC_ENABLED))
    {
      /* Snapshot is used only if MVCC is enabled */
      return;
    }

  if (TM_TRAN_ISOLATION () >= TRAN_REPEATABLE_READ)
    {
      /* Do not invalidate snapshot after each statement */
      return;
    }

  /* TODO: Avoid doing a new request on server now. Find an alternative
   *       way by saving invalidated snapshot on client and use first request
   *       in next statement execution to invalidate on server.
   */

  /* Invalidate snapshot on server */
  log_invalidate_mvcc_snapshot ();

  /* Increment snapshot version in work space */
  ws_increment_mvcc_snapshot_version ();
}
