/*
 *
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
 * sp_context_rules.cpp - what a stored procedure may do, judged by where it was invoked
 */

#include "sp_context_rules.hpp"

#include "error_manager.h"

int sp_In_sql_depth = 0;
int sp_In_trigger_depth = 0;

#if defined (SA_MODE)
bool sp_Sa_query_request = false;
#endif /* SA_MODE */

sp_stmt_category
sp_stmt_category_of (const PT_NODE *statement, PT_NODE_TYPE stmt_type)
{
  switch (stmt_type)
    {
    case PT_COMMIT_WORK:
    case PT_ROLLBACK_WORK:
    case PT_SAVEPOINT:
    case PT_PREPARE_TO_COMMIT:
    case PT_2PC_ATTACH:
    case PT_SET_XACTION:
    case PT_SET_SYS_PARAMS:
    case PT_SET_OPT_LVL:
    case PT_SET_TRIGGER:
    case PT_SET_NAMES:
    case PT_SET_TIMEZONE:
      return SP_STMT_TCL;

    case PT_SET_SESSION_VARIABLES:
      /* SET @v only holds a value in the session, outside the transaction */
      return SP_STMT_OTHER;

    case PT_ALTER:
    case PT_ALTER_INDEX:
    case PT_ALTER_SERIAL:
    case PT_ALTER_STORED_PROCEDURE:
    case PT_ALTER_TRIGGER:
    case PT_ALTER_USER:
    case PT_CREATE_ENTITY:
    case PT_CREATE_INDEX:
    case PT_CREATE_SERIAL:
    case PT_CREATE_STORED_PROCEDURE:
    case PT_CREATE_TRIGGER:
    case PT_CREATE_USER:
    case PT_DROP:
    case PT_DROP_INDEX:
    case PT_DROP_SERIAL:
    case PT_DROP_STORED_PROCEDURE:
    case PT_DROP_TRIGGER:
    case PT_DROP_USER:
    case PT_GRANT:
    case PT_RENAME:
    case PT_REVOKE:
    case PT_REMOVE_TRIGGER:
    case PT_RENAME_TRIGGER:
    case PT_UPDATE_STATS:
    case PT_CREATE_SERVER:
    case PT_DROP_SERVER:
    case PT_RENAME_SERVER:
    case PT_ALTER_SERVER:
    case PT_TRUNCATE:
    case PT_ALTER_SYNONYM:
    case PT_CREATE_SYNONYM:
    case PT_DROP_SYNONYM:
    case PT_RENAME_SYNONYM:
      return SP_STMT_DDL;

    case PT_METHOD_CALL:
      if (statement != NULL && statement->node_type == PT_EXECUTE_PREPARE)
	{
	  /* EXECUTE of a prepared method call
	   *   PREPARE s FROM 'CALL login (...) ON CLASS db_user';
	   *   SELECT f () FROM t;   -- f runs EXECUTE IMMEDIATE 'EXECUTE s'
	   */
	  return statement->info.execute.is_method_on_target ? SP_STMT_DDL : SP_STMT_OTHER;
	}
      /* a method call run directly
       *   SELECT f () FROM t;   -- f runs EXECUTE IMMEDIATE 'CALL add_user (''u'') ON CLASS db_user'
       *                         --     or EXECUTE IMMEDIATE 'CALL add_user (CLASS db_user, ''u'')'
       */
      return (statement != NULL && statement->node_type == PT_METHOD_CALL
	      && statement->info.method_call.on_call_target != NULL) ? SP_STMT_DDL : SP_STMT_OTHER;

    case PT_INSERT:
    case PT_UPDATE:
    case PT_DELETE:
    case PT_MERGE:
    case PT_EXECUTE_TRIGGER:
      return SP_STMT_DML;

    case PT_SELECT:
      if (statement != NULL && statement->node_type == PT_EXECUTE_PREPARE)
	{
	  /* EXECUTE of a prepared INCR
	   *   PREPARE s FROM 'SELECT INCR (cnt) FROM t2';
	   *   SELECT f () FROM t;   -- f runs EXECUTE IMMEDIATE 'EXECUTE s'
	   */
	  return statement->info.execute.is_click_counter ? SP_STMT_DML : SP_STMT_OTHER;
	}
      /* INCR run directly
       *   SELECT f () FROM t;   -- f runs SELECT INCR (cnt) FROM t2
       */
      return (statement != NULL && statement->flag.is_click_counter) ? SP_STMT_DML : SP_STMT_OTHER;

    default:
      return SP_STMT_OTHER;
    }
}

static const char *
sp_stmt_entry_name (sp_stmt_category stmt_category, int stmt_type)
{
  switch (stmt_type)
    {
    case CUBRID_STMT_COMMIT_WORK:
      return "COMMIT";
    case CUBRID_STMT_ROLLBACK_WORK:
      return "ROLLBACK";
    case CUBRID_STMT_SAVEPOINT:
      return "SAVEPOINT";
    case CUBRID_STMT_PREPARE:
      return "PREPARE TO COMMIT";
    case CUBRID_STMT_ATTACH:
      return "ATTACH";
    default:
      return stmt_category == SP_STMT_DDL ? "DDL" : "the statement";
    }
}

/*
 * sp_check_statement_allowed () - apply the rules to a statement a stored procedure asked for
 *   return: NO_ERROR, or the error set
 *   stmt_category(in): category of the statement
 *   stmt_type(in): CUBRID_STMT_TYPE of the statement, for the error message
 *
 * Note: the rules:
 *       1. transaction control or DDL inside a SQL statement or a trigger: rejected
 *       2. DML inside a SQL statement: rejected, whatever the table
 *       A procedure called alone, including by a trigger action's CALL for DML, is not restricted.
 */
int
sp_check_statement_allowed (sp_stmt_category stmt_category, int stmt_type)
{
  const bool in_sql = (sp_In_sql_depth > 0);

  if (stmt_category == SP_STMT_TCL || stmt_category == SP_STMT_DDL)
    {
      if (in_sql)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_TCL_DDL_IN_QUERY, 1,
		  sp_stmt_entry_name (stmt_category, stmt_type));
	  return ER_SP_TCL_DDL_IN_QUERY;
	}

      if (sp_In_trigger_depth > 0)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_TCL_DDL_IN_TRIGGER, 1,
		  sp_stmt_entry_name (stmt_category, stmt_type));
	  return ER_SP_TCL_DDL_IN_TRIGGER;
	}

      return NO_ERROR;
    }

  if (stmt_category == SP_STMT_DML && in_sql)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_DML_IN_QUERY, 0);
      return ER_SP_DML_IN_QUERY;
    }

  return NO_ERROR;
}

void
sp_reset_context (void)
{
  sp_In_sql_depth = 0;
  sp_In_trigger_depth = 0;
#if defined (SA_MODE)
  sp_Sa_query_request = false;
#endif /* SA_MODE */
}
