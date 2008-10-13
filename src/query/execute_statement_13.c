/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * do_tran.c - DO functions for transaction management
 *
 * Note:
 */

#ident "$Id$"

#include "config.h"

#include "error_manager.h"
#include "db.h"
#include "parser.h"
#include "msgexec.h"
#include "parser.h"
#include "qo.h"
#include "execute_statement_11.h"
#include "transaction_cl.h"
#include "qo.h"
#include "system_parameter.h"
#include "dbval.h"

static int map_iso_levels (PARSER_CONTEXT * parser, PT_NODE * statement,
			   DB_TRAN_ISOLATION * tran_isolation,
			   PT_NODE * node);
static int set_iso_level (PARSER_CONTEXT * parser,
			  DB_TRAN_ISOLATION * tran_isolation, bool * async_ws,
			  PT_NODE * statement, const DB_VALUE * level);
static int check_timeout_value (PARSER_CONTEXT * parser, PT_NODE * statement,
				DB_VALUE * val);
static char *get_savepoint_name_from_db_value (DB_VALUE * val);

/*
 * do_attach() - Attaches to named (distributed 2pc) transaction
 *   return: Error code if attach fails
 *   parser(in): Parser context
 *   statement(in): Parse tree of an attach statement
 *
 * Note:
 */
int
do_attach (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  if (!parser
      || pt_has_error (parser)
      || !statement || statement->node_type != PT_ATTACH)
    {
      return ER_GENERIC_ERROR;
    }
  else
    return db_2pc_attach_transaction (statement->info.attach.trans_id);
}

/*
 * do_prepare_to_commit() - Prepare to commit local participant of i
 *			    (distributed 2pc) transaction
 *   return: Error code if prepare-to-commit fails
 *   parser(in): Parser context
 *   statement(in): Parse tree of a prepare-to-commit statement
 *
 * Note:
 */
int
do_prepare_to_commit (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  if (!parser
      || pt_has_error (parser)
      || !statement || statement->node_type != PT_PREPARE_TO_COMMIT)
    {
      return ER_GENERIC_ERROR;
    }
  else
    return db_2pc_prepare_to_commit_transaction (statement->info.
						 prepare_to_commit.trans_id);
}

/*
 * do_commit() - Commit a transaction
 *   return: Error code
 *   parser(in): Parser context
 *   statement(in): Parse tree of a commit statement
 *
 * Note:
 */
int
do_commit (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  return tran_commit (statement->info.commit_work.retain_lock ? true : false);
}

/*
 * do_rollback_savepoints() - Rollback savepoints of named ldbs
 *   return: Error code
 *   parser(in): Parser context
 *   sp_nane(in): Savepoint name
 */
int
do_rollback_savepoints (PARSER_CONTEXT * parser, const char *sp_name)
{
  if (!sp_name)
    {
      return db_abort_transaction ();
    }

  return NO_ERROR;
}

/*
 * do_rollback() - Rollbacks a transaction
 *   return: Error code
 *   parser(in): Parser context
 *   statement(in): Parse tree of a rollback statement (for regularity)
 *
 * Note: If a savepoint name is given, the transaction is rolled back to
 *   the savepoint, otherwise the entire transaction is rolled back.
 *   The function requires ldbnames is a list of ldbs
 *   and effects doing savepoints of named ldbs
 */
int
do_rollback (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int error = NO_ERROR;
  const char *save_name;
  PT_NODE *name;
  DB_VALUE val;

  name = statement->info.rollback_work.save_name;
  if (name == NULL)
    {
      error = tran_abort ();
    }
  else
    {
      if (name->node_type == PT_NAME
	  && name->info.name.meta_class != PT_PARAMETER)
	{
	  save_name = name->info.name.original;
	  error = db_abort_to_savepoint_internal (save_name);
	}
      else
	{
	  pt_evaluate_tree (parser, name, &val);
	  if (pt_has_error (parser))
	    {
	      return ER_GENERIC_ERROR;
	    }
	  save_name = get_savepoint_name_from_db_value (&val);
	  if (!save_name)
	    {
	      return er_errid ();
	    }
	  error = db_abort_to_savepoint_internal (save_name);
	  db_value_clear (&val);
	}
    }

  return error;
}

/*
 * do_savepoint() - Creates a transaction savepoint
 *   return: Error code if savepoint fails
 *   parser(in): Parser context of a savepoint statement
 *   statement(in): Parse tree of a rollback statement (for regularity)
 *
 * Note: If a savepoint name is given, the savepoint is created
 *   with that name, if no savepoint name is given, we generate a unique one.
 */
int
do_savepoint (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int error = NO_ERROR;
  const char *save_name;
  PT_NODE *name;
  DB_VALUE val;

  name = statement->info.savepoint.save_name;
  if (name == NULL)
    {
      PT_INTERNAL_ERROR (parser, "transactions");
    }
  else
    {
      if (name->node_type == PT_NAME
	  && name->info.name.meta_class != PT_PARAMETER)
	{
	  save_name = name->info.name.original;
	  error = db_savepoint_transaction_internal (save_name);
	}
      else
	{
	  pt_evaluate_tree (parser, name, &val);
	  if (pt_has_error (parser))
	    {
	      return ER_GENERIC_ERROR;
	    }
	  save_name = get_savepoint_name_from_db_value (&val);
	  if (!save_name)
	    {
	      return er_errid ();
	    }
	  error = db_savepoint_transaction_internal (save_name);
	  db_value_clear (&val);
	}
    }

  return error;
}

/*
 * do_get_xaction() - Gets the isolation level and/or timeout value for
 *      	      a transaction
 *   return: Error code if it fails
 *   parser(in): Parser context
 *   statement(in/out): Parse tree of a get transaction statement
 *
 * Note:
 */
int
do_get_xaction (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  float lock_timeout = 0;
  DB_TRAN_ISOLATION tran_isolation = TRAN_UNKNOWN_ISOLATION;
  bool async_ws;
  int tran_num;
  const char *into_label;
  DB_VALUE *ins_val;
  PT_NODE *into_var;
  int error = NO_ERROR;

  (void) tran_get_tran_settings (&lock_timeout, &tran_isolation, &async_ws);

  /* create a DB_VALUE to hold the result */
  ins_val = db_value_create ();
  db_make_int (ins_val, 0);

  switch (statement->info.get_xaction.option)
    {
    case PT_ISOLATION_LEVEL:
      tran_num = (int) tran_isolation;
      if (async_ws)
	{
	  tran_num |= TRAN_ASYNC_WS_BIT;
	}
      db_make_int (ins_val, tran_num);
      break;

    case PT_LOCK_TIMEOUT:
      db_make_float (ins_val, lock_timeout);
      break;

    default:
      break;
    }

  statement->etc = (void *) ins_val;

  into_var = statement->info.get_xaction.into_var;
  if (into_var != NULL
      && into_var->node_type == PT_NAME
      && (into_label = into_var->info.name.original) != NULL)
    {
      /*
       * create another DB_VALUE of the new instance for
       * the label_table
       */
      ins_val = db_value_create ();
      db_make_int (ins_val, 0);

      switch (statement->info.get_xaction.option)
	{
	case PT_ISOLATION_LEVEL:
	  tran_num = (int) tran_isolation;
	  if (async_ws)
	    {
	      tran_num |= TRAN_ASYNC_WS_BIT;
	    }
	  db_make_int (ins_val, tran_num);
	  break;

	case PT_LOCK_TIMEOUT:
	  db_make_float (ins_val, lock_timeout);
	  break;

	default:
	  break;
	}

      /* enter {label, ins_val} pair into the label_table */
      error = pt_associate_label_with_value (into_label, ins_val);
    }

  return error;
}

/*
 * do_set_xaction() - Sets the isolation level and/or timeout value for
 *      	      a transaction
 *   return: Error code if it fails
 *   parser(in): Parser context
 *   statement(in): Parse tree of a set transaction statement
 *
 * Note:
 */
int
do_set_xaction (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  DB_TRAN_ISOLATION tran_isolation;
  DB_VALUE val;
  PT_NODE *mode = statement->info.set_xaction.xaction_modes;
  int error = NO_ERROR;
  bool async_ws;

  while ((error == NO_ERROR) && (mode != NULL))
    {
      switch (mode->node_type)
	{
	case PT_ISOLATION_LVL:
	  if (mode->info.isolation_lvl.level == NULL)
	    {
	      /* map schema/instance pair to level */
	      error = map_iso_levels (parser, statement, &tran_isolation,
				      mode);
	      async_ws = mode->info.isolation_lvl.async_ws ? true : false;
	    }
	  else
	    {
	      pt_evaluate_tree (parser, mode->info.isolation_lvl.level, &val);

	      if (parser->error_msgs)
		{
		  return ER_GENERIC_ERROR;
		}

	      error = set_iso_level (parser, &tran_isolation, &async_ws,
				     statement, &val);
	    }

	  if (error == NO_ERROR)
	    {
	      error = tran_reset_isolation (tran_isolation, async_ws);
	    }
	  break;
	case PT_TIMEOUT:
	  pt_evaluate_tree (parser, mode->info.timeout.val, &val);
	  if (parser->error_msgs)
	    {
	      return ER_GENERIC_ERROR;
	    }

	  if (check_timeout_value (parser, statement, &val) != NO_ERROR)
	    {
	      return ER_GENERIC_ERROR;
	    }
	  else
	    {
	      (void) tran_reset_wait_times (DB_GET_FLOAT (&val));
	    }
	  break;
	default:
	  return ER_GENERIC_ERROR;
	}

      mode = mode->next;
    }

  return error;
}

/*
 * do_get_optimization_level() - Determine the current optimization and
 *				 return it through the statement parameter.
 *   return: Error code if it fails
 *   parser(in): Parser context
 *   statement(in/out): Parse tree of a get transaction statement
 *
 * Note:
 */
int
do_get_optimization_param (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  DB_VALUE *val;
  PT_NODE *into_var;
  const char *into_name;
  char cost[2];
  int error = NO_ERROR;

  val = db_value_create ();
  if (val == NULL)
    {
      return er_errid ();
    }

  switch (statement->info.get_opt_lvl.option)
    {
    case PT_OPT_LVL:
      {
	int i;
	qo_get_optimization_param (&i, QO_PARAM_LEVEL);
	db_make_int (val, i);
	break;
      }
    case PT_OPT_COST:
      {
	DB_VALUE plan;
	pt_evaluate_tree (parser, statement->info.get_opt_lvl.args, &plan);
	if (parser->error_msgs)
	  {
	    return ER_OBJ_INVALID_ARGUMENTS;
	  }
	qo_get_optimization_param (cost, QO_PARAM_COST,
				   DB_GET_STRING (&plan));
	pr_clear_value (&plan);
	db_make_string (val, cost);
      }
    default:
      /*
       * Default ok; nothing else can get in here.
       */
      break;
    }

  statement->etc = (void *) val;

  into_var = statement->info.get_opt_lvl.into_var;
  if (into_var != NULL
      && into_var->node_type == PT_NAME
      && (into_name = into_var->info.name.original) != NULL)
    {
      error = pt_associate_label_with_value (into_name, db_value_copy (val));
    }

  return error;
}

/*
 * do_set_optimization_param() - Set the optimization level to the indicated
 *				 value and return the old value through the
 *				 statement paramter.
 *   return: Error code if it fails
 *   parser(in): Parser context
 *   statement(in): Parse tree of a set transaction statement
 *
 * Note:
 */
int
do_set_optimization_param (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  PT_NODE *p1, *p2;
  DB_VALUE val1, val2;
  char *plan, *cost;

  db_make_null (&val1);
  db_make_null (&val2);

  p1 = statement->info.set_opt_lvl.val;

  if (p1 == NULL)
    {
      er_set (ER_ERROR_SEVERITY, __FILE__, __LINE__,
	      ER_OBJ_INVALID_ARGUMENTS, 0);
      return ER_OBJ_INVALID_ARGUMENTS;
    }

  pt_evaluate_tree (parser, p1, &val1);
  if (parser->error_msgs)
    {
      pr_clear_value (&val1);
      return NO_ERROR;
    }

  switch (statement->info.set_opt_lvl.option)
    {
    case PT_OPT_LVL:
      qo_set_optimization_param (NULL, QO_PARAM_LEVEL,
				 (int) DB_GET_INTEGER (&val1));
      break;
    case PT_OPT_COST:
      plan = DB_GET_STRING (&val1);
      p2 = p1->next;
      pt_evaluate_tree (parser, p2, &val2);
      if (parser->error_msgs)
	{
	  pr_clear_value (&val1);
	  pr_clear_value (&val2);
	  return ER_OBJ_INVALID_ARGUMENTS;
	}
      switch (DB_VALUE_TYPE (&val2))
	{
	case DB_TYPE_INTEGER:
	  qo_set_optimization_param (NULL, QO_PARAM_COST, plan,
				     DB_GET_INT (&val2));
	  break;
	case DB_TYPE_CHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARCHAR:
	case DB_TYPE_VARNCHAR:
	  cost = DB_GET_STRING (&val2);
	  qo_set_optimization_param (NULL, QO_PARAM_COST, plan,
				     (int) cost[0]);
	  break;
	default:
	  er_set (ER_ERROR_SEVERITY, __FILE__, __LINE__,
		  ER_OBJ_INVALID_ARGUMENTS, 0);
	  pr_clear_value (&val1);
	  pr_clear_value (&val2);
	  return ER_OBJ_INVALID_ARGUMENTS;
	}
      break;
    default:
      /*
       * Default ok; no other options available.
       */
      break;
    }

  pr_clear_value (&val1);
  pr_clear_value (&val2);
  return NO_ERROR;
}

/*
 * do_set_sys_params() - Set the system paramters defined in 'cubrid.conf'.
 *   return: Error code if it fails
 *   parser(in): Parser context
 *   statement(in): Parse tree of a set transaction statement
 *
 * Note:
 */
int
do_set_sys_params (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  PT_NODE *val;
  DB_VALUE db_val;
  int error = NO_ERROR;

  val = statement->info.set_sys_params.val;
  if (val == NULL)
    {
      return ER_OBJ_INVALID_ARGUMENTS;
    }

  db_make_null (&db_val);
  while (val && error == NO_ERROR)
    {
      pt_evaluate_tree (parser, val, &db_val);

      if (parser->error_msgs)
	{
	  error = ER_GENERIC_ERROR;
	}
      else
	{
	  error = db_set_system_parameters (DB_GET_STRING (&db_val));
	}

      pr_clear_value (&db_val);
      val = val->next;
    }

  return error;
}

/*
 * map_iso_levels() - Maps the schema/instance isolation level to the
 *      	      DB_TRAN_ISOLATION enumerated type.
 *   return: Error code if it fails
 *   parser(in): Parser context
 *   tran_isolation(out):
 *   node(in): Parse tree of a set transaction statement
 *
 * Note: Initializes isolation_levels array
 */
static int
map_iso_levels (PARSER_CONTEXT * parser, PT_NODE * statement,
		DB_TRAN_ISOLATION * tran_isolation, PT_NODE * node)
{
  PT_MISC_TYPE instances = node->info.isolation_lvl.instances;
  PT_MISC_TYPE schema = node->info.isolation_lvl.schema;

  switch (schema)
    {
    case PT_SERIALIZABLE:
      if (instances == PT_SERIALIZABLE)
	{
	  *tran_isolation = TRAN_SERIALIZABLE;
	}
      else
	{
	  PT_ERRORmf2 (parser, statement, MSGCAT_SET_PARSER_RUNTIME,
		       MSGCAT_RUNTIME_XACT_INVALID_ISO_LVL_MSG,
		       pt_show_misc_type (schema),
		       pt_show_misc_type (instances));
	  return ER_GENERIC_ERROR;
	}
      break;
    case PT_REPEATABLE_READ:
      if (instances == PT_READ_UNCOMMITTED)
	{
	  *tran_isolation = TRAN_REP_CLASS_UNCOMMIT_INSTANCE;
	}
      else if (instances == PT_READ_COMMITTED)
	{
	  *tran_isolation = TRAN_REP_CLASS_COMMIT_INSTANCE;
	}
      else if (instances == PT_REPEATABLE_READ)
	{
	  *tran_isolation = TRAN_REP_CLASS_REP_INSTANCE;
	}
      else
	{
          PT_ERRORmf2 (parser, statement, MSGCAT_SET_PARSER_RUNTIME,
                       MSGCAT_RUNTIME_XACT_INVALID_ISO_LVL_MSG,
                       pt_show_misc_type (schema),
                       pt_show_misc_type (instances));
	  return ER_GENERIC_ERROR;
	}
      break;
    case PT_READ_COMMITTED:
      if (instances == PT_READ_UNCOMMITTED)
	{
	  *tran_isolation = TRAN_COMMIT_CLASS_UNCOMMIT_INSTANCE;
	}
      else if (instances == PT_READ_COMMITTED)
	{
	  *tran_isolation = TRAN_COMMIT_CLASS_COMMIT_INSTANCE;
	}
      else
	{
          PT_ERRORmf2 (parser, statement, MSGCAT_SET_PARSER_RUNTIME,
                       MSGCAT_RUNTIME_XACT_INVALID_ISO_LVL_MSG,
                       pt_show_misc_type (schema),
                       pt_show_misc_type (instances));
	  return ER_GENERIC_ERROR;
	}
      break;
    case PT_READ_UNCOMMITTED:
      PT_ERRORmf2 (parser, statement, MSGCAT_SET_PARSER_RUNTIME,
                   MSGCAT_RUNTIME_XACT_INVALID_ISO_LVL_MSG,
                   pt_show_misc_type (schema),
                   pt_show_misc_type (instances));
      return ER_GENERIC_ERROR;
    default:
      return ER_GENERIC_ERROR;
    }

  return NO_ERROR;
}

/*
 * set_iso_level() -
 *   return: Error code if it fails
 *   parser(in): Parser context
 *   tran_isolation(out): Isolation level set as a side effect
 *   async_ws(out):
 *   statement(in): Parse tree of a set transaction statement
 *   level(in):
 *
 * Note: Translates the user entered isolation level (1,2,3,4,5) into
 *       the enumerated type.
 */
static int
set_iso_level (PARSER_CONTEXT * parser,
	       DB_TRAN_ISOLATION * tran_isolation, bool * async_ws,
	       PT_NODE * statement, const DB_VALUE * level)
{
  int error = NO_ERROR;
  int isolvl = DB_GET_INTEGER (level) & 0x0F;
  *async_ws = (DB_GET_INTEGER (level) & 0xF0) ? true : false;

  /* translate to the enumerated type */
  switch (isolvl)
    {
    case 1:
      *tran_isolation = TRAN_COMMIT_CLASS_UNCOMMIT_INSTANCE;
      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_CUBRID,
				       MSGCAT_SET_PARSER_RUNTIME,
				       MSGCAT_RUNTIME_ISO_LVL_SET_TO_MSG));
      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_CUBRID,
				       MSGCAT_SET_PARSER_RUNTIME,
				       MSGCAT_RUNTIME_READCOM_S_READUNC_I));
      break;
    case 2:
      *tran_isolation = TRAN_COMMIT_CLASS_COMMIT_INSTANCE;
      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_CUBRID,
				       MSGCAT_SET_PARSER_RUNTIME,
				       MSGCAT_RUNTIME_ISO_LVL_SET_TO_MSG));
      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_CUBRID,
				       MSGCAT_SET_PARSER_RUNTIME,
				       MSGCAT_RUNTIME_READCOM_S_READCOM_I));
      break;
    case 3:
      *tran_isolation = TRAN_REP_CLASS_UNCOMMIT_INSTANCE;
      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_CUBRID,
				       MSGCAT_SET_PARSER_RUNTIME,
				       MSGCAT_RUNTIME_ISO_LVL_SET_TO_MSG));
      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_CUBRID,
				       MSGCAT_SET_PARSER_RUNTIME,
				       MSGCAT_RUNTIME_REPREAD_S_READUNC_I));
      break;
    case 4:
      *tran_isolation = TRAN_REP_CLASS_COMMIT_INSTANCE;
      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_CUBRID,
				       MSGCAT_SET_PARSER_RUNTIME,
				       MSGCAT_RUNTIME_ISO_LVL_SET_TO_MSG));
      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_CUBRID,
				       MSGCAT_SET_PARSER_RUNTIME,
				       MSGCAT_RUNTIME_REPREAD_S_READCOM_I));
      break;
    case 5:
      *tran_isolation = TRAN_REP_CLASS_REP_INSTANCE;
      fprintf (stdout,
	       msgcat_message (MSGCAT_CATALOG_CUBRID,
			       MSGCAT_SET_PARSER_RUNTIME,
			       MSGCAT_RUNTIME_ISO_LVL_SET_TO_MSG));
      fprintf (stdout,
	       msgcat_message (MSGCAT_CATALOG_CUBRID,
			       MSGCAT_SET_PARSER_RUNTIME,
			       MSGCAT_RUNTIME_REPREAD_S_REPREAD_I));
      break;
    case 6:
      *tran_isolation = TRAN_SERIALIZABLE;
      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_CUBRID,
				       MSGCAT_SET_PARSER_RUNTIME,
				       MSGCAT_RUNTIME_ISO_LVL_SET_TO_MSG));
      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_CUBRID,
				       MSGCAT_SET_PARSER_RUNTIME,
				       MSGCAT_RUNTIME_SERIAL_S_SERIAL_I));
      break;
    case 0:
      if (*async_ws == true)
	{			/* only async workspace is given */
	  float dummy_lktimeout;
	  bool dummy_aws;
	  tran_get_tran_settings (&dummy_lktimeout, tran_isolation,
				  &dummy_aws);
	  break;
	}
      /* fall through */
    default:
      PT_ERRORm (parser, statement, MSGCAT_SET_PARSER_RUNTIME,
		 MSGCAT_RUNTIME_XACT_ISO_LVL_MSG);
      error = ER_GENERIC_ERROR;
    }

  return error;
}

/*
 * check_timeout_value() -
 *   return: Error code if it fails
 *   parser(in): Parser context
 *   statement(in):
 *   val(in): DB_VALUE with the value to set
 *
 * Note: Checks the user entered isolation level. Valid values are:
 *                    -1 : Infinite
 *                     0 : Don't wait
 *                    >0 : Wait this number of seconds
 */
static int
check_timeout_value (PARSER_CONTEXT * parser, PT_NODE * statement,
		     DB_VALUE * val)
{
  float timeout;

  if (db_value_coerce (val, val, &tp_Float_domain) == DOMAIN_COMPATIBLE)
    {
      timeout = DB_GET_FLOAT (val);
      if ((timeout == -1) || (timeout >= 0))
	{
	  return NO_ERROR;
	}
    }
  PT_ERRORm (parser, statement, MSGCAT_SET_PARSER_RUNTIME,
	     MSGCAT_RUNTIME_TIMEOUT_VALUE_MSG);
  return ER_GENERIC_ERROR;
}

/*
 * get_savepoint_name_from_db_value() -
 *   return: a NULL if the value doesn't properly describe the name
 *           of a savepoint.
 *   val(in):
 *
 * Note: Mutates the contents of val to hold a NULL terminated string
 *       holding a valid savepoint name.  If the value is already of
 *       type string, a NULL termination will be assumed since the
 *       name came from a parse tree.
 */
static char *
get_savepoint_name_from_db_value (DB_VALUE * val)
{
  if (DB_VALUE_TYPE (val) != DB_TYPE_CHAR &&
      DB_VALUE_TYPE (val) != DB_TYPE_VARCHAR &&
      DB_VALUE_TYPE (val) != DB_TYPE_NCHAR &&
      DB_VALUE_TYPE (val) != DB_TYPE_VARNCHAR)
    {
      if (tp_value_cast (val, val, tp_Domains[DB_TYPE_VARCHAR], false)
	  != DOMAIN_COMPATIBLE)
	{
	  return (char *) NULL;
	}
    }

  return db_get_string (val);
}
