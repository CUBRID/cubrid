/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * do_eval.c - Implements the evaluate statement.
 */

#ident "$Id$"

#include "config.h"

#include "error_manager.h"
#include "db.h"
#include "parser.h"
#include "execute_statement_11.h"
#include "dbval.h"

/* 
 * do_evaluate() - Evaluates an expression
 *   return: Error code
 *   parser(in): Parser context
 *   statement(in/out): Parse tree of a insert statement
 */
int
do_evaluate (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int error = NO_ERROR;
  DB_VALUE expr_value, *into_val;
  PT_NODE *expr, *into_var;
  const char *into_label;

  db_make_null (&expr_value);

  if (!statement || !((expr = statement->info.evaluate.expression)))
    {
      return ER_GENERIC_ERROR;
    }

  pt_evaluate_tree (parser, expr, &expr_value);
  if (parser->error_msgs)
    {
      pt_report_to_ersys (parser, PT_SEMANTIC);
      return ER_PT_SEMANTIC;
    }

  statement->etc = (void *) db_value_copy (&expr_value);
  into_var = statement->info.evaluate.into_var;

  if (into_var
      && into_var->node_type == PT_NAME
      && (into_label = into_var->info.name.original) != NULL)
    {
      /* create another DB_VALUE of the new instance for 
         the label_table */
      into_val = db_value_copy (&expr_value);

      /* enter {label, ins_val} pair into the label_table */
      error = pt_associate_label_with_value (into_label, into_val);
    }

  pr_clear_value (&expr_value);
  return error;
}
