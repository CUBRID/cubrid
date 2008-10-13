/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * do_repl.c - DO Functions for replication management
 *
 * Note:
 */

#ident "$Id$"

#include <stdarg.h>
#include <ctype.h>
#include <string.h>

#include "parser.h"
#include "replication.h"
#include "locator_cl.h"
#include "execute_statement_11.h"

/*
 * do_replicate_schema() -
 *   return: Error code
 *   parser(in): Parser context
 *   statement(in): The parse tree of a DDL statement
 *
 * Note:
 */
int
do_replicate_schema (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int error = NO_ERROR;
  REPL_INFO repl_info;
  REPL_INFO_SCHEMA repl_schema;
  PARSER_VARCHAR *name = NULL;

  switch (statement->node_type)
    {
    case PT_CREATE_ENTITY:
      name =
	pt_print_bytes (parser, statement->info.create_entity.entity_name);
      repl_schema.statement_type = SQLX_CMD_CREATE_CLASS;
      break;

    case PT_ALTER:
      name = pt_print_bytes (parser, statement->info.alter.entity_name);
      repl_schema.statement_type = SQLX_CMD_ALTER_CLASS;
      break;

    case PT_RENAME:
      name = pt_print_bytes (parser, statement->info.rename.old_name);
      repl_schema.statement_type = SQLX_CMD_RENAME_CLASS;
      break;

    case PT_DROP:
      name = pt_print_bytes_l (parser, statement->info.drop.spec_list);
      repl_schema.statement_type = SQLX_CMD_DROP_CLASS;
      break;

    case PT_CREATE_INDEX:
      name = pt_print_bytes (parser, statement->info.index.indexed_class);
      repl_schema.statement_type = SQLX_CMD_CREATE_INDEX;
      break;

    case PT_ALTER_INDEX:
      name = pt_print_bytes (parser, statement->info.index.indexed_class);
      repl_schema.statement_type = SQLX_CMD_ALTER_INDEX;
      break;

    case PT_DROP_INDEX:
      name = pt_print_bytes (parser, statement->info.index.indexed_class);
      repl_schema.statement_type = SQLX_CMD_DROP_INDEX;
      break;

#if 0
      /* serial replication is not schema replication but data replication */
    case PT_CREATE_SERIAL:
      name = pt_print_bytes (parser, statement->info.serial.serial_name);
      repl_schema.statement_type = SQLX_CMD_CREATE_SERIAL;
      break;

    case PT_ALTER_SERIAL:
      name = pt_print_bytes (parser, statement->info.serial.serial_name);
      repl_schema.statement_type = SQLX_CMD_ALTER_SERIAL;
      break;

    case PT_DROP_SERIAL:
      name = pt_print_bytes (parser, statement);
      repl_schema.statement_type = SQLX_CMD_DROP_SERIAL;
      break;
#endif

    case PT_DROP_VARIABLE:
      name = pt_print_bytes (parser, statement->info.drop_variable.var_names);
      repl_schema.statement_type = SQLX_CMD_DROP_LABEL;
      break;

    case PT_CREATE_STORED_PROCEDURE:
      name = pt_print_bytes (parser, statement);
      repl_schema.statement_type = SQLX_CMD_CREATE_STORED_PROCEDURE;
      break;

    case PT_DROP_STORED_PROCEDURE:
      name = pt_print_bytes (parser, statement);
      repl_schema.statement_type = SQLX_CMD_DROP_STORED_PROCEDURE;
      break;

    case PT_CREATE_USER:
      name = pt_print_bytes (parser, statement->info.create_user.user_name);
      repl_schema.statement_type = SQLX_CMD_CREATE_USER;
      break;

    case PT_ALTER_USER:
      name = pt_print_bytes (parser, statement->info.alter_user.user_name);
      repl_schema.statement_type = SQLX_CMD_ALTER_USER;
      break;

    case PT_DROP_USER:
      name = pt_print_bytes (parser, statement->info.drop_user.user_name);
      repl_schema.statement_type = SQLX_CMD_DROP_USER;
      break;

    case PT_GRANT:
      name = pt_print_bytes (parser, statement->info.grant.user_list);
      repl_schema.statement_type = SQLX_CMD_GRANT;
      break;

    case PT_REVOKE:
      name = pt_print_bytes (parser, statement->info.revoke.user_list);
      repl_schema.statement_type = SQLX_CMD_REVOKE;
      break;

    case PT_CREATE_TRIGGER:
      name =
	pt_print_bytes (parser, statement->info.create_trigger.trigger_name);
      repl_schema.statement_type = SQLX_CMD_CREATE_TRIGGER;
      break;

    case PT_RENAME_TRIGGER:
      name = pt_print_bytes (parser, statement->info.rename_trigger.old_name);
      repl_schema.statement_type = SQLX_CMD_RENAME_TRIGGER;
      break;

    case PT_DROP_TRIGGER:
      name = pt_print_bytes (parser,
			     statement->info.drop_trigger.trigger_spec_list);
      repl_schema.statement_type = SQLX_CMD_DROP_TRIGGER;
      break;

    case PT_REMOVE_TRIGGER:
      name =
	pt_print_bytes (parser,
			statement->info.remove_trigger.trigger_spec_list);
      repl_schema.statement_type = SQLX_CMD_REMOVE_TRIGGER;
      break;

    case PT_ALTER_TRIGGER:
      name = pt_print_bytes_l (parser,
			       statement->info.alter_trigger.
			       trigger_spec_list);
      repl_schema.statement_type = SQLX_CMD_SET_TRIGGER;
      break;

    default:
      break;
    }

  if (name == NULL)
    {
      return NO_ERROR;
    }

  repl_info.repl_info_type = REPL_INFO_TYPE_SCHEMA;
  repl_schema.name = (char *) pt_get_varchar_bytes (name);
  repl_schema.ddl = parser_print_tree (parser, statement);

  repl_info.info = (char *) &repl_schema;

  locator_flush_replication_info (&repl_info);

  return error;
}
