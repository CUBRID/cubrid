/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * do_partition.c - Implements the scope statement.
 *
 * Note: 
 */

#ident "$Id$"

#include "config.h"

#include "error_manager.h"
#include "db.h"
#include "parser.h"
#include "parser.h"
#include "system_parameter.h"
#include "execute_statement_11.h"

/* 
 * do_scope() - scopes a statement
 *   return: Error code if scope fails
 *   parser(in/out): Parser context
 *   statement(in): The parse tree of a scope statement
 *
 * Note:  
 */
int
do_scope (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int error = NO_ERROR;
  PT_NODE *stmt;

  if (!statement
      || (statement->node_type != PT_SCOPE)
      || !((stmt = statement->info.scope.stmt))
      || (stmt->node_type != PT_TRIGGER_ACTION))
    {
      return ER_GENERIC_ERROR;
    }
  else
    {
      switch (stmt->info.trigger_action.action_type)
	{
	case PT_REJECT:
	case PT_INVALIDATE_XACTION:
	case PT_PRINT:
	  break;

	case PT_EXPRESSION:
	  do_Trigger_involved = true;
#if 0
	  if (PRM_XASL_MAX_PLAN_CACHE_ENTRIES >= 0)
	    {

	      /* prepare a statement to execute */
	      error =
		do_prepare_statement (parser,
				      stmt->info.trigger_action.expression);
	      if (error >= NO_ERROR)
		{
		  /* execute the prepared statement */
		  error =
		    do_execute_statement (parser,
					  stmt->info.trigger_action.
					  expression);
		}
	    }
	  else
	    {
	      error =
		do_statement (parser, stmt->info.trigger_action.expression);
	    }
#else
	  error = do_statement (parser, stmt->info.trigger_action.expression);
#endif
	  /* Do not reset do_Trigger_involved here. This is intention. */
	  break;

	default:
	  break;
	}

      return error;
    }
}
