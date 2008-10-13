/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * execute_statement.c - Entry functions to do execute
 *
 * TODO: rename this file to execute_statement.c
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#if defined(WINDOWS)
#include <process.h>		/* for getpid() */
#include <winsock2.h>		/* for struct timeval */
#else /* WINDOWS */
#include <unistd.h>		/* for getpid() */
#include <libgen.h>		/* for dirname, basename() */
#include <sys/time.h>		/* for struct timeval */
#endif /* WINDOWS */
#include <string.h>
#include <limits.h>
#include <errno.h>

#include "porting.h"
#include "error_manager.h"
#include "parser.h"
#include "semantic_check.h"
#include "msgexec.h"
#include "db.h"
#include "execute_schema_2.h"
#include "execute_statement_11.h"
#include "system_parameter.h"
#include "server.h"
#include "transaction_cl.h"
#include "execute_statement_10.h"
#include "object_print_1.h"
#include "jsp_sky.h"

#define ER_PT_UNKNOWN_STATEMENT ER_GENERIC_ERROR
#define UNIQUE_SAVEPOINT_EXTERNAL_STATEMENT "eXTERNALsTATEMENT"

bool do_Trigger_involved;

/*
 * do_statement() -
 *   return: Error code
 *   parser(in): Parser context
 *   statement(in): Parse tree of a statement
 *
 * Note: Side effects can exist at the statement, especially schema information
 */
int
do_statement (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int error = NO_ERROR;
  QUERY_EXEC_MODE old_exec_mode;

  /* If it is an internally created statement,
     set it's host variable info again to search host variables at parent parser */
  SET_HOST_VARIABLES_IF_INTERNAL_STATEMENT (parser);

  if (statement)
    {
      /* only SELECT query can be executed in async mode */
      old_exec_mode = parser->exec_mode;
      parser->exec_mode = (statement->node_type == PT_SELECT) ?
	old_exec_mode : SYNC_EXEC;

      /* for the subset of nodes which represent top level statements,
       * process them. For any other node, return an error.
       */

      switch (statement->node_type)
	{
	case PT_ALTER:
	  error = do_check_internal_statements (parser, statement,
						/* statement->info.alter.
						   internal_stmts, */
						do_alter);
	  break;

	case PT_ATTACH:
	  error = do_attach (parser, statement);
	  break;

	case PT_PREPARE_TO_COMMIT:
	  error = do_prepare_to_commit (parser, statement);
	  break;

	case PT_COMMIT_WORK:
	  error = do_commit (parser, statement);
	  break;

	case PT_CREATE_ENTITY:
	  error = do_check_internal_statements (parser, statement,
						/* statement->info.create_entity.
						   internal_stmts, */
						do_create_entity);
	  break;

	case PT_CREATE_INDEX:
	  error = do_create_index (parser, statement);
	  break;

	case PT_EVALUATE:
	  error = do_evaluate (parser, statement);
	  break;

	case PT_SCOPE:
	  error = do_scope (parser, statement);
	  break;

	case PT_DELETE:
	  error = do_check_delete_trigger (parser, statement, do_delete);
	  break;

	case PT_DROP:
	  error = do_check_internal_statements (parser, statement,
						/* statement->info.drop.
						   internal_stmts, */
						do_drop);
	  break;

	case PT_DROP_INDEX:
	  error = do_drop_index (parser, statement);
	  break;

	case PT_ALTER_INDEX:
	  error = do_alter_index (parser, statement);
	  break;

	case PT_DROP_VARIABLE:
	  error = do_drop_variable (parser, statement);
	  break;

	case PT_GRANT:
	  error = do_grant (parser, statement);
	  break;

	case PT_INSERT:
	  error = do_check_insert_trigger (parser, statement, do_insert);
	  break;

	case PT_RENAME:
	  error = do_rename (parser, statement);
	  break;

	case PT_REVOKE:
	  error = do_revoke (parser, statement);
	  break;

	case PT_CREATE_USER:
	  error = do_create_user (parser, statement);
	  break;

	case PT_DROP_USER:
	  error = do_drop_user (parser, statement);
	  break;

	case PT_ALTER_USER:
	  error = do_alter_user (parser, statement);
	  break;

	case PT_SET_XACTION:
	  error = do_set_xaction (parser, statement);
	  break;

	case PT_GET_XACTION:
	  error = do_get_xaction (parser, statement);
	  break;

	case PT_ROLLBACK_WORK:
	  error = do_rollback (parser, statement);
	  break;

	case PT_SAVEPOINT:
	  error = do_savepoint (parser, statement);
	  break;

	case PT_UNION:
	case PT_DIFFERENCE:
	case PT_INTERSECTION:
	case PT_SELECT:
	  error = do_select (parser, statement);
	  break;

	case PT_UPDATE:
	  error = do_check_update_trigger (parser, statement, do_update);
	  break;

	case PT_UPDATE_STATS:
	  error = do_update_stats (parser, statement);
	  break;

	case PT_GET_STATS:
	  error = do_get_stats (parser, statement);
	  break;

	case PT_METHOD_CALL:
	  error = do_call_method (parser, statement);
	  break;

	case PT_CREATE_TRIGGER:
	  error = do_create_trigger (parser, statement);
	  break;

	case PT_DROP_TRIGGER:
	  error = do_drop_trigger (parser, statement);
	  break;

	case PT_SET_TRIGGER:
	  error = do_set_trigger (parser, statement);
	  break;

	case PT_GET_TRIGGER:
	  error = do_get_trigger (parser, statement);
	  break;

	case PT_RENAME_TRIGGER:
	  error = do_rename_trigger (parser, statement);
	  break;

	case PT_ALTER_TRIGGER:
	  error = do_alter_trigger (parser, statement);
	  break;

	case PT_EXECUTE_TRIGGER:
	  error = do_execute_trigger (parser, statement);
	  break;

	case PT_REMOVE_TRIGGER:
	  error = do_remove_trigger (parser, statement);
	  break;

	case PT_CREATE_SERIAL:
	  error = do_create_serial (parser, statement);
	  break;

	case PT_ALTER_SERIAL:
	  error = do_alter_serial (parser, statement);
	  break;

	case PT_DROP_SERIAL:
	  error = do_drop_serial (parser, statement);
	  break;

	case PT_GET_OPT_LVL:
	  error = do_get_optimization_param (parser, statement);
	  break;

	case PT_SET_OPT_LVL:
	  error = do_set_optimization_param (parser, statement);
	  break;

	case PT_SET_SYS_PARAMS:
	  error = do_set_sys_params (parser, statement);
	  break;

	case PT_CREATE_STORED_PROCEDURE:
	  error = jsp_create_stored_procedure (parser, statement);
	  break;

	case PT_DROP_STORED_PROCEDURE:
	  error = jsp_drop_stored_procedure (parser, statement);
	  break;

	default:
	  er_set (ER_ERROR_SEVERITY, __FILE__, statement->line_number,
		  ER_PT_UNKNOWN_STATEMENT, 1, statement->node_type);
	  break;
	}

      /* restore execution flag */
      parser->exec_mode = old_exec_mode;

      if (error == NO_ERROR)
	{
	  do_replicate_schema (parser, statement);
	}
    }


  /* There may be parse tree fragments that were collected during the
   * execution of the statement that should be freed now.
   */
  pt_free_orphans (parser);

  /* During query execution,
   * if current transaction was rollbacked by the system,
   * abort transaction on client side also.
   */
  if (error == ER_LK_UNILATERALLY_ABORTED)
    {
      (void) tran_abort_only_client (false);
    }

  RESET_HOST_VARIABLES_IF_INTERNAL_STATEMENT (parser);
  return error;
}

/*
 * do_prepare_statement() - Prepare a given statement for execution
 *   return: Error code
 *   parser(in): Parser context
 *   statement(in): Parse tree of a statement
 *
 * Note:
 * 	PEPARE includes query optimization and plan generation (XASL) for the SQL
 * 	statement. EXECUTE means requesting the server to execute the given XASL.
 *
 * 	Some type of statement is not necessary or not able to do PREPARE stage.
 * 	They can or must be EXECUTEd directly without PREPARE. For those types of
 * 	statements, this function will return NO_ERROR.
 */
int
do_prepare_statement (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int err = NO_ERROR;

  switch (statement->node_type)
    {
    case PT_DELETE:
      err = do_prepare_delete (parser, statement);
      break;
    case PT_INSERT:
#if 0				/* disabled until implementation completed */
      err = do_prepare_insert (parser, statement);
#endif
      break;
    case PT_UPDATE:
      err = do_prepare_update (parser, statement);
      break;
    case PT_SELECT:
    case PT_DIFFERENCE:
    case PT_INTERSECTION:
    case PT_UNION:
      err = do_prepare_select (parser, statement);
      break;
    default:
      /* there're no actions for other types of statements */
      break;
    }

  return err;
}				/* do_prepare_statement() */

/*
 * do_execute_statement() - Execute a prepared statement
 *   return: Error code
 *   parser(in): Parser context
 *   statement(in): Parse tree of a statement
 *
 * Note:
 * 	The statement should be PREPAREd before to EXECUTE. But, some type of
 * 	statement will be EXECUTEd directly without PREPARE stage because we can
 * 	decide the fact that they should be executed using query plan (XASL)
 * 	at the time of exeuction stage.
 */
int
do_execute_statement (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int err = NO_ERROR;
  QUERY_EXEC_MODE old_exec_mode;

  /* If it is an internally created statement,
     set it's host variable info again to search host variables at parent parser */
  SET_HOST_VARIABLES_IF_INTERNAL_STATEMENT (parser);

  /* only SELECT query can be executed in async mode */
  old_exec_mode = parser->exec_mode;
  parser->exec_mode = (statement->node_type == PT_SELECT) ?
    old_exec_mode : SYNC_EXEC;

  /* for the subset of nodes which represent top level statements,
     process them; for any other node, return an error */
  switch (statement->node_type)
    {
    case PT_CREATE_ENTITY:
      /* err = do_create_entity(parser, statement); */
      /* execute internal statements before and after do_create_entity() */
      err = do_check_internal_statements (parser, statement,
					  /* statement->info.create_entity.
					     internal_stmts, */
					  do_create_entity);
      break;
    case PT_CREATE_INDEX:
      err = do_create_index (parser, statement);
      break;
    case PT_CREATE_SERIAL:
      err = do_create_serial (parser, statement);
      break;
    case PT_CREATE_TRIGGER:
      err = do_create_trigger (parser, statement);
      break;
    case PT_CREATE_USER:
      err = do_create_user (parser, statement);
      break;
    case PT_ALTER:
      /* err = do_alter(parser, statement); */
      /* execute internal statements before and after do_alter() */
      err = do_check_internal_statements (parser, statement,
					  /* statement->info.alter.
					     internal_stmts, */ do_alter);
      break;
    case PT_ALTER_INDEX:
      err = do_alter_index (parser, statement);
      break;
    case PT_ALTER_SERIAL:
      err = do_alter_serial (parser, statement);
      break;
    case PT_ALTER_TRIGGER:
      err = do_alter_trigger (parser, statement);
      break;
    case PT_ALTER_USER:
      err = do_alter_user (parser, statement);
      break;
    case PT_DROP:
      /* err = do_drop(parser, statement); */
      /* execute internal statements before and after do_drop() */
      err = do_check_internal_statements (parser, statement,
					  /* statement->info.drop.internal_stmts, */
					  do_drop);
      break;
    case PT_DROP_INDEX:
      err = do_drop_index (parser, statement);
      break;
    case PT_DROP_SERIAL:
      err = do_drop_serial (parser, statement);
      break;
    case PT_DROP_TRIGGER:
      err = do_drop_trigger (parser, statement);
      break;
    case PT_DROP_USER:
      err = do_drop_user (parser, statement);
      break;
    case PT_DROP_VARIABLE:
      err = do_drop_variable (parser, statement);
      break;
    case PT_RENAME:
      err = do_rename (parser, statement);
      break;
    case PT_RENAME_TRIGGER:
      err = do_rename_trigger (parser, statement);
      break;
    case PT_SET_TRIGGER:
      err = do_set_trigger (parser, statement);
      break;
    case PT_GET_TRIGGER:
      err = do_get_trigger (parser, statement);
      break;
    case PT_EXECUTE_TRIGGER:
      err = do_execute_trigger (parser, statement);
      break;
    case PT_REMOVE_TRIGGER:
      err = do_remove_trigger (parser, statement);
      break;
    case PT_GRANT:
      err = do_grant (parser, statement);
      break;
    case PT_REVOKE:
      err = do_revoke (parser, statement);
      break;
    case PT_ATTACH:
      err = do_attach (parser, statement);
      break;
    case PT_GET_XACTION:
      err = do_get_xaction (parser, statement);
      break;
    case PT_SET_XACTION:
      err = do_set_xaction (parser, statement);
      break;
    case PT_SAVEPOINT:
      err = do_savepoint (parser, statement);
      break;
    case PT_PREPARE_TO_COMMIT:
      err = do_prepare_to_commit (parser, statement);
      break;
    case PT_COMMIT_WORK:
      err = do_commit (parser, statement);
      break;
    case PT_ROLLBACK_WORK:
      err = do_rollback (parser, statement);
      break;
    case PT_SCOPE:
      err = do_scope (parser, statement);
      break;
    case PT_DELETE:
      err = do_check_delete_trigger (parser, statement, do_execute_delete);
      break;
    case PT_INSERT:
#if 0				/* disabled until implementation completed */
      err = do_check_insert_trigger (parser, statement, do_execute_insert);
#else
      err = do_check_insert_trigger (parser, statement, do_insert);
#endif
      break;
    case PT_UPDATE:
      err = do_check_update_trigger (parser, statement, do_execute_update);
      break;
    case PT_SELECT:
    case PT_DIFFERENCE:
    case PT_INTERSECTION:
    case PT_UNION:
      err = do_execute_select (parser, statement);
      break;
    case PT_EVALUATE:
      err = do_evaluate (parser, statement);
      break;
    case PT_METHOD_CALL:
      err = do_call_method (parser, statement);
      break;
    case PT_GET_STATS:
      err = do_get_stats (parser, statement);
      break;
    case PT_UPDATE_STATS:
      err = do_update_stats (parser, statement);
      break;
    case PT_GET_OPT_LVL:
      err = do_get_optimization_param (parser, statement);
      break;
    case PT_SET_OPT_LVL:
      err = do_set_optimization_param (parser, statement);
      break;
    case PT_SET_SYS_PARAMS:
      err = do_set_sys_params (parser, statement);
      break;
    case PT_CREATE_STORED_PROCEDURE:
      err = jsp_create_stored_procedure (parser, statement);
      break;
    case PT_DROP_STORED_PROCEDURE:
      err = jsp_drop_stored_procedure (parser, statement);
      break;
    default:
      er_set (ER_ERROR_SEVERITY, __FILE__, statement->line_number,
	      ER_PT_UNKNOWN_STATEMENT, 1, statement->node_type);
      break;
    }

  /* restore execution flag */
  parser->exec_mode = old_exec_mode;

  if (err == NO_ERROR)
    {
      do_replicate_schema (parser, statement);
    }

  /* There may be parse tree fragments that were collected during the
     execution of the statement that should be freed now. */
  pt_free_orphans (parser);

  /* During query execution,
     if current transaction was rollbacked by the system,
     abort transaction on client side also. */
  if (err == ER_LK_UNILATERALLY_ABORTED)
    {
      (void) tran_abort_only_client (false);
    }

  RESET_HOST_VARIABLES_IF_INTERNAL_STATEMENT (parser);

  return err;
}				/* do_execute_statement() */

/*
 * do_statements() - Execute a prepared statement
 *   return: Error code
 *   parser(in): Parser context
 *   statement_list(in): Parse tree of a statement list
 *
 * Note: Side effects can exist at the statement list
 */
int
do_statements (PARSER_CONTEXT * parser, PT_NODE * statement_list)
{
  int error = 0;
  PT_NODE *statement;

  /* for each of a list of statement nodes, process it. */
  for (statement = statement_list; statement != NULL;
       statement = statement->next)
    {
      do_Trigger_involved = false;
      error = do_statement (parser, statement);
      do_Trigger_involved = false;
      if (error)
	{
	  break;
	}
    }

  return error;
}

/*
 * do_check_internal_statements() -
 *   return: Error code
 *   parser(in): Parser context
 *   statement_list(in): Parse tree of a statement
 *   internal_stmt_list(in):
 *   do_func(in):
 *
 * Note:
 *   Do savepoint and execute statements before and after do_func()
 *   if an error happens, rollback to the savepoint.
 */
int
do_check_internal_statements (PARSER_CONTEXT * parser, PT_NODE * statement,
			      /* PT_NODE * internal_stmt_list, */
			      PT_DO_FUNC do_func)
{
#if 0				/* to disable TEXT */
  const char *savepoint_name = UNIQUE_SAVEPOINT_EXTERNAL_STATEMENT;
  int error = NO_ERROR, num_rows = NO_ERROR;

  if (internal_stmt_list == NULL)
    {
#endif
      return do_func (parser, statement);
#if 0				/* to disable TEXT */
    }
  else
    {
      error = tran_savepoint (savepoint_name, false);
      if (error != NO_ERROR)
	return error;

      error = do_internal_statements (parser, internal_stmt_list, 0);
      if (error >= NO_ERROR)
	{
	  /* The main statement cas use out parameters from internal statements,
	     and the internal statements generate the parameters at execution time.
	     So, it need to bind the paramters again */
	  (void) parser_walk_tree (parser, statement, pt_bind_param_node,
				   NULL, NULL, NULL);
	  num_rows = error = do_func (parser, statement);
#if defined(CUBRID_DEBUG)
	  er_log_debug (ARG_FILE_LINE,
			"do_check_internal_statements : execute %s statement, %s\n",
			"main", parser_print_tree (parser, statement));
#endif
	  if (error >= NO_ERROR)
	    {
	      error = do_internal_statements (parser, internal_stmt_list, 1);
	    }
	}
      if (error < NO_ERROR)
	{
	  (void) tran_abort_upto_savepoint (savepoint_name);
	  return error;
	}
      return num_rows;
    }
#endif
}

/*
 * do_internal_statements() -
 *   return: Error code
 *   parser(in): Parser context
 *   internal_stmt_list(in):
 *   phase(in):
 *
 * Note:
 *   For input statements, find the statements to do now and
 *   using new parser, parse, check semantics and execute these.
 *
 */
int
do_internal_statements (PARSER_CONTEXT * parser, PT_NODE * internal_stmt_list,
			const int phase)
{
  PT_NODE *stmt_str;
  DB_QUERY_RESULT *query_result;
  DB_QUERY_ERROR query_error;
  PARSER_CONTEXT *save_parser;
  DB_OBJECT *save_user;
  int au_save;
  int error = NO_ERROR;

  save_user = Au_user;
  Au_user = Au_dba_user;
  AU_DISABLE (au_save);

  for (stmt_str = internal_stmt_list; stmt_str != NULL;
       stmt_str = stmt_str->next)
    {
      if ((phase == 0 && stmt_str->etc == NULL)
	  || (phase == 1 && stmt_str->etc != NULL))
	{
	  /* To get host variable info from parent parser, set the parent parser */
	  save_parser = parent_parser;
	  parent_parser = parser;
	  error =
	    db_execute (stmt_str->info.value.text, &query_result,
			&query_error);
	  /* restore the parent parser */
	  parent_parser = save_parser;
	  if (error < NO_ERROR)
	    break;
	}
    }

  Au_user = save_user;
  AU_ENABLE (au_save);

  return error;
}
