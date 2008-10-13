/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * do_meth.c - Implement method calls
 *
 * Note:
 */

#ident "$Id$"

#include "config.h"

#include "error_manager.h"
#include "parser.h"
#include "msgexec.h"
#include "db.h"
#include "memory_manager_2.h"
#include "environment_variable.h"
#include "jsp_sky.h"
#include "execute_statement_11.h"
#include "dbval.h"

static int call_method (PARSER_CONTEXT * parser, PT_NODE * statement);

/*
 * call_method() -
 *   return: Value returned by method if success, otherwise an error code
 *   parser(in): Parser context
 *   node(in): Parse tree of a call statement
 *
 * Note:
 */
static int
call_method (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  const char *into_label, *proc;
  int error = NO_ERROR;
  DB_OBJECT *obj = NULL;
  DB_VALUE target_value, *ins_val, ret_val, db_value;
  DB_VALUE_LIST *val_list = 0, *vl, **next_val_list;
  PT_NODE *vc, *into, *target, *method;

  db_make_null (&ret_val);
  db_make_null (&target_value);

  /*
   * The method name and ON name are required.
   */
  if (!statement
      || !(method = statement->info.method_call.method_name)
      || method->node_type != PT_NAME || !(proc = method->info.name.original)
      || !(target = statement->info.method_call.on_call_target))
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS,
	      0);
      return er_errid ();
    }

  /*
   * Determine whether the object is a class or instance.
   */

  pt_evaluate_tree (parser, target, &target_value);
  if (parser->error_msgs)
    {
      pt_report_to_ersys (parser, PT_SEMANTIC);
      return er_errid ();
    }

  if (DB_VALUE_TYPE (&target_value) == DB_TYPE_NULL)
    {
      /*
       * Don't understand the rationale behind this case.  What's the
       * point here?  MRS 4/30/96
       */
      error = NO_ERROR;
    }
  else
    {
      if (DB_VALUE_TYPE (&target_value) == DB_TYPE_OBJECT)
	{
	  obj = DB_GET_OBJECT ((&target_value));
	}

      if (!obj || parser->error_msgs)
	{
	  PT_ERRORm (parser, statement, MSGCAT_SET_PARSER_SEMANTIC,
		     MSGCAT_SEMANTIC_METH_TARGET_NOT_OBJ);
	  return er_errid ();
	}

      /*
       * Build an argument list.
       */
      next_val_list = &val_list;
      vc = statement->info.method_call.arg_list;
      for (; vc != NULL; vc = vc->next)
	{
	  DB_VALUE *db_val;
	  *next_val_list =
	    (DB_VALUE_LIST *) calloc (1, sizeof (DB_VALUE_LIST));
	  if (*next_val_list == NULL)
	    {
	      return er_errid ();
	    }
	  (*next_val_list)->next = (DB_VALUE_LIST *) 0;

	  /*
	   * Don't clone host vars; they may actually be acting as output
	   * variables (e.g., a character array that is intended to receive
	   * bytes from the method), and cloning will ensure that the
	   * results never make it to the expected area.  Since
	   * pt_evaluate_tree() always clones its db_values we must not
	   * use pt_evaluate_tree() to extract the db_value from a host
	   * variable;  instead extract it ourselves.
	   */
	  if (vc->node_type == PT_HOST_VAR)
	    {
	      db_val = pt_value_to_db (parser, vc);
	    }
	  else
	    {
	      pt_evaluate_tree (parser, vc, &db_value);
	      if (parser->error_msgs)
		{
		  break;
		}
	      db_val = &db_value;
	    }

	  (*next_val_list)->val = *db_val;

	  next_val_list = &(*next_val_list)->next;
	}

      /*
       * Call the method.
       */
      if (parser->error_msgs)
	{
	  pt_report_to_ersys (parser, PT_SEMANTIC);
	  error = er_errid ();
	}
      else
	{
	  error = db_send_arglist (obj, proc, &ret_val, val_list);
	}

      /*
       * Free the argument list.  Again, it is important to be careful
       * with host variables.  Since we didn't clone them, we shouldn't
       * free or clear them.
       */
      vc = statement->info.method_call.arg_list;
      for (; val_list && vc; vc = vc->next)
	{
	  vl = val_list->next;
	  if (vc->node_type != PT_HOST_VAR)
	    {
	      db_value_clear (&val_list->val);
	    }
	  free_and_init (val_list);
	  val_list = vl;
	}

      if (error == NO_ERROR)
	{
	  /*
	   * Save the method result.
	   */
	  statement->etc = (void *) db_value_copy (&ret_val);

	  if ((into = statement->info.method_call.to_return_var) != NULL
	      && into->node_type == PT_NAME
	      && (into_label = into->info.name.original) != NULL)
	    {
	      /* create another DB_VALUE of the new instance for the label_table */
	      ins_val = db_value_copy (&ret_val);

	      /* enter {label, ins_val} pair into the label_table */
	      error = pt_associate_label_with_value (into_label, ins_val);
	    }
	}
    }

  db_value_clear (&ret_val);
  return error;
}

/*
 * do_call_method() -
 *   return: Value returned by method if success, otherwise an error code
 *   parser(in): Parser context
 *   node(in): Parse tree of a call statement
 *
 * Note:
 */
int
do_call_method (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  PT_NODE *method;

  if (!statement
      || !(method = statement->info.method_call.method_name)
      || method->node_type != PT_NAME || !(method->info.name.original))
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS,
	      0);
      return er_errid ();
    }

  if (statement->info.method_call.on_call_target)
    {
      return call_method (parser, statement);
    }
  else
    {
      return jsp_call_stored_procedure (parser, statement);
    }
}

/*
 * These functions are provided just so we have some builtin gadgets that we can
 * use for quick and dirty method testing.  To get at them, alter your
 * favorite class like this:
 *
 * 	alter class foo
 * 		add method pickaname() string
 * 		function dbmeth_class_name;
 *
 * or
 *
 * 	alter class foo
 * 		add method pickaname(string) string
 * 		function dbmeth_print;
 *
 * After that you should be able to invoke "pickaname" on "foo" instances
 * to your heart's content.  dbmeth_class_name() will retrieve the class
 * name of the target instance and return it as a string; dbmeth_print()
 * will print the supplied value on stdout every time it is invoked.
 */

/*
 * TODO: The following function names need to be fixed.
 * Renaming can affects user interface.
 */

/*
 * dbmeth_class_name() -
 *   return: None
 *   self(in): Class object
 *   result(out): DB_VALUE for a class name
 *
 * Note: Position of function arguments must be kept
 *   for pre-defined function pointers(au_static_links)
 */
void
dbmeth_class_name (DB_OBJECT * self, DB_VALUE * result)
{
  const char *cname;
  DB_VALUE tmp;

  cname = db_get_class_name (self);

  /*
   * Make a string and clone it so that it won't become invalid if the
   * underlying class object that gave us the string goes away.  Of
   * course, this gives the responsibility for freeing the cloned
   * string to someone else; is anybody accepting it?
   */
  db_make_string (&tmp, cname);
  db_value_clone (&tmp, result);
}

/*
 * TODO: The functin name need to be fixed.
 * it is known system method so must fix corresponding qa first
 */

/*
 * dbmeth_print() -
 *   return: None
 *   self(in): Class object
 *   result(out): NULL value
 *   msg(in): DB_VALUE for a message
 *
 * Note: Position of function arguments must be kept
 *   for pre-defined function pointers(au_static_links)
 */
void
dbmeth_print (DB_OBJECT * self, DB_VALUE * result, DB_VALUE * msg)
{
  db_value_print (msg);
  printf ("\n");
  db_make_null (result);
}
