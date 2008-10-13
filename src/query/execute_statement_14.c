/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * do_trig.c - DO functions for trigger management
 *
 * Note: 
 */

#ident "$Id$"

#include "config.h"

#include "error_manager.h"
#include "memory_manager_2.h"
#include "dbtype.h"
#include "dbdef.h"
#include "parser.h"
#include "object_domain.h"
#include "db.h"
#include "trigger_manager.h"
#include "execute_statement_11.h"
#include "system_parameter.h"
#include "schema_manager_3.h"
#include "release_string.h"
#include "dbval.h"

/* Value supplied in statement has an invalid type */
#define ER_TR_INVALID_VALUE_TYPE ER_GENERIC_ERROR

#define MAX_DOMAIN_NAME_SIZE 150

/*
 * PARSE TREE MACROS                                                          
 *                                                                            
 * arguments:                                                                 
 *	statement: parser node                                                
 *                                                                            
 * returns/side-effects: non-zero                                             
 *                                                                            
 * description:                                                               
 *    These are used as shorthand for parse tree access.                      
 *    Given a statement node, they test for certain characteristics           
 *    and return a boolean.                                                  
 */

#define IS_REJECT_ACTION_STATEMENT(statement) \
  ((statement)->node_type == PT_TRIGGER_ACTION && \
   (statement)->info.trigger_action.action_type == PT_REJECT)

#define IS_INVALIDATE_ACTION_STATEMENT(statement) \
  ((statement)->node_type == PT_TRIGGER_ACTION && \
   (statement)->info.trigger_action.action_type == PT_INVALIDATE_XACTION)

#define IS_PRINT_ACTION_STATEMENT(statement) \
  ((statement)->node_type == PT_TRIGGER_ACTION && \
   (statement)->info.trigger_action.action_type == PT_PRINT)

#define PT_NODE_TR_NAME(node) \
 ((node)->info.create_trigger.trigger_name->info.name.original)
#define PT_NODE_TR_STATUS(node) \
 (convert_misc_to_tr_status((node)->info.create_trigger.trigger_status))
#define PT_NODE_TR_PRI(node) \
  ((node)->info.create_trigger.trigger_priority)
#define PT_NODE_TR_EVENT_TYPE(node) \
  (convert_event_to_tr_event((node)->info.create_trigger.trigger_event->info.event_spec.event_type))

#define PT_NODE_TR_TARGET(node) \
  ((node)->info.create_trigger.trigger_event->info.event_spec.event_target)
#define PT_TR_TARGET_CLASS(target) \
  ((target)->info.event_target.class_name->info.name.original)
#define PT_TR_TARGET_ATTR(target) \
  ((target)->info.event_target.attribute)
#define PT_TR_ATTR_NAME(attr) \
  ((attr)->info.name.original)

#define PT_NODE_COND(node) \
  ((node)->info.create_trigger.trigger_condition)
#define PT_NODE_COND_TIME(node) \
  (convert_misc_to_tr_time((node)->info.create_trigger.condition_time))

#define PT_NODE_ACTION(node) \
  ((node)->info.create_trigger.trigger_action)
#define PT_NODE_ACTION_TIME(node) \
  (convert_misc_to_tr_time((node)->info.create_trigger.action_time))

#define PT_NODE_TR_REF(node) \
  ((node)->info.create_trigger.trigger_reference)
#define PT_TR_REF_REFERENCE(ref) \
  (&(ref)->info.event_object)

static int merge_mop_list_extension (DB_OBJLIST * new_objlist,
				     DB_OBJLIST ** list);
static DB_TRIGGER_EVENT convert_event_to_tr_event (const PT_EVENT_TYPE ev);
static DB_TRIGGER_TIME convert_misc_to_tr_time (const PT_MISC_TYPE pt_time);
static DB_TRIGGER_STATUS convert_misc_to_tr_status (const PT_MISC_TYPE
						    pt_status);
static int convert_speclist_to_objlist (DB_OBJLIST ** triglist,
					PT_NODE * specnode);
static int check_trigger (DB_TRIGGER_EVENT event, DB_OBJECT * class_,
			  const char **attributes, int attribute_count,
			  PT_DO_FUNC * do_func, PARSER_CONTEXT * parser,
			  PT_NODE * statement);
static const char **find_update_columns (int *count_ptr, PT_NODE * statement);
static void get_activity_info (PARSER_CONTEXT * parser,
			       DB_TRIGGER_ACTION * type, const char **source,
			       PT_NODE * statement);

/*
 * Could be added to wslist.c at some point.
 */

/* 
 * merge_mop_list_extension() - 
 *   return: Number of MOPs to be added
 *   new(in):
 *   list(in): 
 *
 * Note:
 */
static int
merge_mop_list_extension (DB_OBJLIST * new_objlist, DB_OBJLIST ** list)
{
  DB_OBJLIST *obj, *next;
  int added = 0;

  for (obj = new_objlist, next = NULL; obj != NULL; obj = next)
    {
      next = obj->next;
      if (ml_find (*list, obj->op))
	{
	  obj->next = NULL;
	  ml_ext_free (obj);
	}
      else
	{
	  obj->next = *list;
	  *list = obj;
	  added++;
	}
    }

  return added;
}

/* 
 * These translate parse tree things into corresponding trigger things.
 */

/* 
 * convert_event_to_tr_event() - Converts a PT_EV type into the corresponding 
 *				 TR_EVENT_ type.
 *   return: DB_TRIGER_EVENT
 *   ev(in): One of PT_EVENT_TYPE
 *
 * Note:
 */
static DB_TRIGGER_EVENT
convert_event_to_tr_event (const PT_EVENT_TYPE ev)
{
  DB_TRIGGER_EVENT event = TR_EVENT_NULL;

  switch (ev)
    {
    case PT_EV_INSERT:
      event = TR_EVENT_INSERT;
      break;
    case PT_EV_STMT_INSERT:
      event = TR_EVENT_STATEMENT_INSERT;
      break;
    case PT_EV_DELETE:
      event = TR_EVENT_DELETE;
      break;
    case PT_EV_STMT_DELETE:
      event = TR_EVENT_STATEMENT_DELETE;
      break;
    case PT_EV_UPDATE:
      event = TR_EVENT_UPDATE;
      break;
    case PT_EV_STMT_UPDATE:
      event = TR_EVENT_STATEMENT_UPDATE;
      break;
    case PT_EV_ALTER:
      event = TR_EVENT_ALTER;
      break;
    case PT_EV_DROP:
      event = TR_EVENT_DROP;
      break;
    case PT_EV_COMMIT:
      event = TR_EVENT_COMMIT;
      break;
    case PT_EV_ROLLBACK:
      event = TR_EVENT_ROLLBACK;
      break;
    case PT_EV_ABORT:
      event = TR_EVENT_ABORT;
      break;
    case PT_EV_TIMEOUT:
      event = TR_EVENT_TIMEOUT;
      break;
    default:
      break;
    }

  return event;
}

/* 
 * convert_misc_to_tr_time() - Converts a PT_MISC_TYPE into a corresponding 
 *    			       TR_TYPE_TYPE constant.
 *   return: DB_TRIGGER_TIME
 *   pt_time(in): One of PT_MISC_TYPE
 *
 * Note:
 */
static DB_TRIGGER_TIME
convert_misc_to_tr_time (const PT_MISC_TYPE pt_time)
{
  DB_TRIGGER_TIME time;

  switch (pt_time)
    {
    case PT_AFTER:
      time = TR_TIME_AFTER;
      break;
    case PT_BEFORE:
      time = TR_TIME_BEFORE;
      break;
    case PT_DEFERRED:
      time = TR_TIME_DEFERRED;
      break;
    default:
      time = TR_TIME_NULL;
      break;
    }

  return time;
}

/* 
 * convert_misc_to_tr_status() - Converts a PT_MISC_TYPE into the correspondint
 *				 TR_STATUE_TYPE.
 *   return: DB_TRIGGER_STATUS
 *   pt_status(in): One of PT_MISC_TYPE
 *
 * Note:
 */
static DB_TRIGGER_STATUS
convert_misc_to_tr_status (const PT_MISC_TYPE pt_status)
{
  DB_TRIGGER_STATUS status;

  switch (pt_status)
    {
    case PT_ACTIVE:
      status = TR_STATUS_ACTIVE;
      break;
    case PT_INACTIVE:
      status = TR_STATUS_INACTIVE;
      break;
    default:			/* if we get bogus input, should it be inactive ? */
      status = TR_STATUS_ACTIVE;
      break;
    }

  return status;
}

/* 
 * convert_speclist_to_objlist() - Converts a PT_MISC_TYPE into the 
 *				   correspondint TR_STATUE_TYPE.
 *   return: Error code
 *   triglist(out): Returned trigger object list 
 *   specnode(in): Node with PT_TRIGGER_SPEC_LIST_INFO 
 *
 * Note:
 *    This function converts a trigger specification list in PT format        
 *    into a list of the corresponding trigger objects.                       
 *    This is used by a variety of the functions that accept trigger          
 *    specifications.                                                         
 *    The list is an external MOP list and must be freed with ml_ext_free()   
 *    or db_objlist_free.                                                     
 *    The alter flag is set for operations that alter triggers based          
 *    on the WITH EVENT and ALL TRIGGERS specification.  In these cases       
 *    we need to automatically filter out the triggers in the list for        
 *    which we don't have authorization.  
 */
static int
convert_speclist_to_objlist (DB_OBJLIST ** triglist, PT_NODE * specnode)
{
  int error = NO_ERROR;
  DB_OBJLIST *triggers, *etrigs;
  PT_NODE *names, *n, *events, *e;
  PT_EVENT_SPEC_INFO *espec;
  PT_EVENT_TARGET_INFO *target;
  const char *str, *attribute;
  DB_TRIGGER_EVENT tr_event;
  DB_OBJECT *trigger, *class_;

  triggers = NULL;

  if (specnode != NULL)
    {
      if (specnode->info.trigger_spec_list.all_triggers)
	{
	  error = tr_find_all_triggers (&triggers);
	}

      else if ((names = specnode->info.trigger_spec_list.trigger_name_list)
	       != NULL)
	{
	  /* since this is an explicitly specified list, if we do not have
	     alter authorization for any of the specified triggers, we need
	     to make sure the statement is not executed (no triggers are dropped).
	     Use tr_check_authorization to find out.
	   */
	  for (n = names; n != NULL && error == NO_ERROR; n = n->next)
	    {
	      str = n->info.name.original;
	      trigger = tr_find_trigger (str);
	      if (trigger == NULL)
		{
		  error = er_errid ();
		}
	      else
		{
		  error = ml_ext_add (&triggers, trigger, NULL);
		}
	    }
	}
      else if ((events = specnode->info.trigger_spec_list.event_list) != NULL)
	{
	  for (e = events; e != NULL && error == NO_ERROR; e = e->next)
	    {
	      class_ = NULL;
	      attribute = NULL;
	      espec = &(e->info.event_spec);
	      tr_event = convert_event_to_tr_event (espec->event_type);

	      if (espec->event_target != NULL)
		{
		  target = &(espec->event_target->info.event_target);
		  class_ =
		    db_find_class (target->class_name->info.name.original);
		  if (class_ == NULL)
		    {
		      error = er_errid ();
		    }
		  else
		    {
		      if (target->attribute != NULL)
			{
			  attribute = target->attribute->info.name.original;
			}

		      error =
			tr_find_event_triggers (tr_event, class_, attribute,
						true, &etrigs);
		      if (error == NO_ERROR)
			{
			  merge_mop_list_extension (etrigs, &triggers);
			}
		    }
		}
	    }
	}
    }

  if (error)
    {
      ml_ext_free (triggers);
    }
  else
    {
      *triglist = triggers;
    }

  return error;
}

/* 
 * get_priority() - 
 *   return: Double value
 *   parser(in): Parser context
 *   node(in): Priority value node
 *
 * Note:
 *    Shorthand function for getting the priority value out of the parse 
 *    tree.  Formerly, we just assumed that this would be represented         
 *    with a double value.  Now we use coersion.                              
 */
static double
get_priority (PARSER_CONTEXT * parser, PT_NODE * node)
{
  DB_VALUE *src, value;
  double priority;

  priority = TR_LOWEST_PRIORITY;

  src = pt_value_to_db (parser, node);
  if (src != NULL
      && tp_value_coerce (src, &value, &tp_Double_domain)
      == DOMAIN_COMPATIBLE)
    {
      priority = DB_GET_DOUBLE (&value);
    }
  /* else, should be setting some kind of error */

  return priority;
}

/*
 * INSERT, UPDATE, & DELETE STATEMENTS 
 */

/* 
 * check_trigger() - 
 *   return: Error code
 *   event(in): Trigger event type                                         
 *   class(in): Target class                          
 *   attributes(in): Target attributes                     
 *   attribute_count(in): Number of target attributes
 *   do_func(in): Function to do 
 *   parser(in): Parser context used by do_func
 *   statement(in): Parse tree of a statement used by do_func
 *
 * Note: The function checks if there is any active trigger defined on 
 *       the target. If there is one, raise the trigger. Otherwise,    
 *       perform the given do_ function.                               
 */
static int
check_trigger (DB_TRIGGER_EVENT event,
	       DB_OBJECT * class_,
	       const char **attributes,
	       int attribute_count,
	       PT_DO_FUNC * do_func, PARSER_CONTEXT * parser,
	       PT_NODE * statement)
{
  int err, result = NO_ERROR;
  TR_STATE *state;

  /* Prepare a trigger state for any triggers that must be raised in
     this statement */

  state = NULL;

  result = tr_prepare_statement (&state, event, class_,
				 attribute_count, attributes);

  if (result == NO_ERROR)
    {
      if (state == NULL)
	{
	  /* no triggers, just do it */
	  /* result = do_func(parser, statement); */
	  /* execute internal statements before and after do_func() */
	  result = do_check_internal_statements (parser, statement,
						 /*
						    (statement->node_type ==
						    PT_INSERT ? statement->info.
						    insert.
						    internal_stmts
						    : (statement->node_type ==
						    PT_UPDATE ? statement->
						    info.update.
						    internal_stmts :
						    statement->info.delete_.
						    internal_stmts)), */
						 do_func);
	}
      else
	{
	  /* fire BEFORE STATEMENT triggers */
	  result = tr_before (state);
	  if (result == NO_ERROR)
	    {
	      /* note, do_insert, do_update, & do_delete don't return just errors, 
	         they can also return positive result counts.  Need to specifically
	         check for result < 0
	       */
	      /* result = do_func(parser, statement); */
	      /* execute internal statements before and after do_func() */
	      result = do_check_internal_statements (parser, statement,
						     /*
						        (statement->node_type ==
						        PT_INSERT ? statement->
						        info.insert.
						        internal_stmts
						        : (statement->
						        node_type ==
						        PT_UPDATE ?
						        statement->info.
						        update.
						        internal_stmts :
						        statement->info.
						        delete_.
						        internal_stmts)), */
						     do_func);
	      if (result < NO_ERROR)
		{
		  tr_abort (state);
		}
	      else
		{
		  /* try to preserve the usual result value */
		  err = tr_after (state);
		  if (err)
		    {
		      result = err;
		    }
		}
	    }
	}
    }

  return result;
}

/* 
 * do_check_delete_trigger() - 
 *   return: Error code
 *   parser(in): Parser context
 *   statement(in): Parse tree of a statement
 *   do_func(in): Function to do
 *
 * Note: The function checks if there is any active trigger with event
 *   TR_EVENT_STATEMENT_DELETE defined on the target. 
 *   If there is one, raise the trigger. Otherwise, perform the 
 *   given do_ function.
 */
int
do_check_delete_trigger (PARSER_CONTEXT * parser, PT_NODE * statement,
			 PT_DO_FUNC * do_func)
{
  PT_NODE *flat;
  DB_OBJECT *class_obj;

  if (PRM_BLOCK_NOWHERE_STATEMENT
      && statement->info.delete_.search_cond == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_AU_AUTHORIZATION_FAILURE,
	      0);
      return ER_AU_AUTHORIZATION_FAILURE;
    }

  /* get the delete class */
  flat = (statement->info.delete_.spec) ?
    statement->info.delete_.spec->info.spec.flat_entity_list : NULL;
  class_obj = (flat) ? flat->info.name.db_object : NULL;

  if (class_obj)
    {
      return check_trigger (TR_EVENT_STATEMENT_DELETE, class_obj, NULL, 0,
			    do_func, parser, statement);
    }
  return NO_ERROR;
}

/* 
 * do_check_insert_trigger() - 
 *   return: Error code
 *   parser(in): Parser context
 *   statement(in): Parse tree of a statement
 *   do_func(in): Function to do
 *
 * Note: The function checks if there is any active trigger with event
 *   TR_EVENT_STATEMENT_INSERT defined on the target. 
 *   If there is one, raise the trigger. Otherwise, perform the 
 *   given do_ function.
 */
int
do_check_insert_trigger (PARSER_CONTEXT * parser, PT_NODE * statement,
			 PT_DO_FUNC * do_func)
{
  PT_NODE *flat;
  DB_OBJECT *class_obj;

  /* get the insert class */
  flat = (statement->info.insert.spec) ?
    statement->info.insert.spec->info.spec.flat_entity_list : NULL;
  class_obj = (flat) ? flat->info.name.db_object : NULL;

  if (class_obj)
    {
      return check_trigger (TR_EVENT_STATEMENT_INSERT, class_obj, NULL, 0,
			    do_func, parser, statement);
    }
  return NO_ERROR;
}

/* 
 * find_update_columns() - 
 *   return: Attribute (column) name array
 *   count_ptr(out): Returned name count
 *   statement(in): Parse tree of a statement to examine
 *
 * Note: 
 *    This is used to to find the attribute/column names referenced in        
 *    the statement.  It builds a array of strings and returns the length of  
 *    the array.                                                           
 */
static const char **
find_update_columns (int *count_ptr, PT_NODE * statement)
{
  PT_NODE *assign;
  const char **columns;
  int count, size, i;
  PT_NODE *lhs, *att;

  assign = statement->info.update.assignment;
  for (count = 0; assign; assign = assign->next)
    {
      lhs = assign->info.expr.arg1;
      if (PT_IS_N_COLUMN_UPDATE_EXPR (lhs))
	{
	  /* multicolumn update */
	  count += pt_length_of_list (lhs->info.expr.arg1);
	}
      else
	{
	  count++;
	}
    }
  size = sizeof (char *) * count;

  columns = (const char **) (malloc (size));
  if (columns == NULL)
    {
      return NULL;
    }

  assign = statement->info.update.assignment;
  for (i = 0; i < count; assign = assign->next)
    {
      lhs = assign->info.expr.arg1;
      if (PT_IS_N_COLUMN_UPDATE_EXPR (lhs))
	{
	  for (att = lhs->info.expr.arg1; att; att = att->next)
	    {
	      columns[i++] = att->info.name.original;
	    }
	}
      else
	{
	  columns[i++] = lhs->info.name.original;
	}
    }

  *count_ptr = count;
  return columns;
}

/* 
 * do_check_update_trigger() - 
 *   return: Error code
 *   parser(in): Parser context
 *   statement(in): Parse tree of a statement 
 *
 * Note: The function checks if there is any active trigger with event
 *   TR_EVENT_STATEMENT_UPDATE defined on the target. 
 *   If there is one, raise the trigger. Otherwise, perform the 
 *   given do_ function.
 */
int
do_check_update_trigger (PARSER_CONTEXT * parser, PT_NODE * statement,
			 PT_DO_FUNC * do_func)
{
  PT_NODE *node;
  DB_OBJECT *class_obj;
  const char **columns;
  int count;
  int err;

  if (PRM_BLOCK_NOWHERE_STATEMENT
      && statement->info.update.search_cond == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_AU_AUTHORIZATION_FAILURE,
	      0);
      return ER_AU_AUTHORIZATION_FAILURE;
    }

  /* If this is an "update object" statement, we may not have a spec list
     yet. This may have been fixed due to the recent changes in 
     pt_exec_trigger_stmt to do name resolution each time. */
  node = (statement->info.update.spec) ?
    statement->info.update.spec->info.spec.flat_entity_list :
    statement->info.update.object_parameter;
  class_obj = (node) ? node->info.name.db_object : NULL;
  if (!class_obj)
    {
      return NO_ERROR;
    }

  columns = find_update_columns (&count, statement);
  err = check_trigger (TR_EVENT_STATEMENT_UPDATE, class_obj, columns, count,
		       do_func, parser, statement);
  if (columns)
    {
      free_and_init (columns);
    }
  return err;
}

/*                                                                            
 * CREATE TRIGGER STATEMENT
 */

/* 
 * get_activity_info() - Works for do_create_trigger
 *   return: None
 *   parser(in): Parse context for the create trigger statement
 *   type(out): Returned type of the activity                           
 *   source(out) : Returned source of the activity (sometimes NULL)           
 *   statement(in): Sub-tree for the condition or action expression            
 *
 * Note:
 *    This is used to convert a parser sub-tree into the corresponding
 *    pair of DB_TRIGGER_ACTIVITY and source string suitable for use             
 *    with tr_create_trigger.                                                 
 *    Since we can't use this parsed representation of the expressions       
 *    anyway (they aren't inside the proper scope), we just convert           
 *    them back into strings with parser_print_tree and let the trigger manager        
 *    call pt_compile_trigger_stmt when necessary.                           
 */
static void
get_activity_info (PARSER_CONTEXT * parser, DB_TRIGGER_ACTION * type,
		   const char **source, PT_NODE * statement)
{
  PT_NODE *str;

  *type = TR_ACT_NULL;
  *source = NULL;

  if (statement != NULL)
    {
      if (IS_REJECT_ACTION_STATEMENT (statement))
	{
	  *type = TR_ACT_REJECT;
	}
      else if (IS_INVALIDATE_ACTION_STATEMENT (statement))
	{
	  *type = TR_ACT_INVALIDATE;
	}
      else if (IS_PRINT_ACTION_STATEMENT (statement))
	{
	  *type = TR_ACT_PRINT;

	  /* extract the print string from the parser node, 
	     not sure if I should be looking at the "data_value.s" field 
	     or the "text" field, they seem to be the same always. */
	  str = statement->info.trigger_action.string;
	  if (str->node_type == PT_VALUE)
	    {
	      *source = str->info.value.text;
	    }
	}
      else
	{
	  /* complex expression */
	  *type = TR_ACT_EXPRESSION;
	  *source = parser_print_tree (parser, statement);
	}
    }
}

/* 
 * do_create_trigger() -
 *   return: Error code
 *   parser(in): Parse context
 *   statement(in): Parse tree of a statement 
 *
 * Note: The function creates a trigger object by calling the trigger
 *   create function.
 */
int
do_create_trigger (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  PT_NODE *cond, *action, *target, *attr, *pri;
  const char *name;
  DB_TRIGGER_STATUS status;
  double priority;
  DB_TRIGGER_EVENT event;
  DB_OBJECT *class_;
  const char *attribute;
  DB_TRIGGER_ACTION cond_type, action_type;
  DB_TRIGGER_TIME cond_time, action_time;
  const char *cond_source, *action_source;
  DB_OBJECT *trigger;

  if (PRM_BLOCK_DDL_STATEMENT)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_AU_AUTHORIZATION_FAILURE,
	      0);
      return ER_AU_AUTHORIZATION_FAILURE;
    }

  name = PT_NODE_TR_NAME (statement);
  status = PT_NODE_TR_STATUS (statement);

  pri = PT_NODE_TR_PRI (statement);
  if (pri != NULL)
    {
      priority = get_priority (parser, pri);
    }
  else
    {
      priority = TR_LOWEST_PRIORITY;
    }

  event = PT_NODE_TR_EVENT_TYPE (statement);
  class_ = NULL;
  attribute = NULL;
  target = PT_NODE_TR_TARGET (statement);
  if (target)
    {
      class_ = db_find_class (PT_TR_TARGET_CLASS (target));
      if (class_ == NULL)
	{
	  return er_errid ();
	}
      if (sm_has_text_domain (db_get_attributes (class_), 1))
	{
	  /* prevent to create a trigger at the class to contain TEXT */
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_REGU_NOT_IMPLEMENTED,
		  1, rel_major_release_string ());
	  return er_errid ();
	}
      attr = PT_TR_TARGET_ATTR (target);
      if (attr)
	{
	  attribute = PT_TR_ATTR_NAME (attr);
	}
    }
  cond = PT_NODE_COND (statement);
  cond_time = PT_NODE_COND_TIME (statement);
  /* note that cond_type can only be TR_ACT_EXPRESSION, if there is
     no conditino node, cond_source will be left NULL */
  get_activity_info (parser, &cond_type, &cond_source, cond);

  action = PT_NODE_ACTION (statement);
  action_time = PT_NODE_ACTION_TIME (statement);
  get_activity_info (parser, &action_type, &action_source, action);

  trigger =
    tr_create_trigger (name, status, priority, event, class_, attribute,
		       cond_time, cond_source, action_time, action_type,
		       action_source);

  if (trigger == NULL)
    {
      return er_errid ();
    }

  /* Save the new trigger object in the parse tree.
     Actually, we probably should also allow INTO variable sub-clause to
     be compatible with INSERT statement. In that case, the portion of
     code in do_insert() for saving the new object and creating a label
     table entry needs to be made a extern function.
   */

  /* This should be treated like a "create class" 
     statement not like an "insert" statement.  The trigger object that
     gets created can't be assigned with an INTO clause so there's no need
     to return it. Assuming this doesn't host anything, delete the
     commented out lines below.
   */
#if 0
  if ((value = db_value_create ()) == NULL)
    return er_errid ();
  db_make_object (value, trigger);
  statement->etc = (void *) value;
#endif

  return NO_ERROR;
}

/* 
 *  MISC TRIGGER OPERATIONS
 */

/* 
 * do_drop_trigger() - Drop one or more triggers based on a trigger spec list.
 *   return: Error code
 *   parser(in): Parse context
 *   statement(in): Parse tree of a statement 
 *
 * Note: 
 */
int
do_drop_trigger (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int error = NO_ERROR;
  PT_NODE *speclist;
  DB_OBJLIST *triggers, *t;

  /* The grammar has beem define such that DROP TRIGGER can only
     be used with an explicit list of named triggers.  Although
     convert_speclist_to_objlist will handle the WITH EVENT and ALL TRIGGERS
     cases we shouldn't see those here.  If for some reason they
     do sneak in, we may get errors when we call tr_drop_triggger()
     on triggers we don't own.
   */

  if (PRM_BLOCK_DDL_STATEMENT)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_AU_AUTHORIZATION_FAILURE,
	      0);
      return ER_AU_AUTHORIZATION_FAILURE;
    }

  speclist = statement->info.drop_trigger.trigger_spec_list;
  if (convert_speclist_to_objlist (&triggers, speclist))
    {
      return er_errid ();
    }

  if (triggers != NULL)
    {
      /* make sure we have ALTER authorization on all the triggers
         before proceeding */

      for (t = triggers; t != NULL && error == NO_ERROR; t = t->next)
	{
	  error = tr_check_authorization (t->op, true);
	}

      if (error == NO_ERROR)
	{
	  /* shouldn't encounter errors in this loop, if we do, 
	     may have to abort the transaction */
	  for (t = triggers; t != NULL && error == NO_ERROR; t = t->next)
	    {
	      error = tr_drop_trigger (t->op, false);
	    }
	}

      /* always free this */
      ml_ext_free (triggers);
    }

  return error;
}

/* 
 * do_alter_trigger() - Alter the priority or status of one or more triggers.
 *   return: Error code
 *   parser(in): Parse context
 *   statement(in): Parse tree with alter trigger node
 *
 * Note: 
 */
int
do_alter_trigger (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int error = NO_ERROR;
  PT_NODE *speclist, *p_node;
  DB_OBJLIST *triggers, *t;
  double priority = TR_LOWEST_PRIORITY;
  DB_TRIGGER_STATUS status;

  if (PRM_BLOCK_DDL_STATEMENT)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_AU_AUTHORIZATION_FAILURE,
	      0);
      return ER_AU_AUTHORIZATION_FAILURE;
    }

  triggers = NULL;
  p_node = statement->info.alter_trigger.trigger_priority;
  speclist = statement->info.alter_trigger.trigger_spec_list;
  if (convert_speclist_to_objlist (&triggers, speclist))
    {
      return er_errid ();
    }

  /* currently, we can' set the status and priority at the same time.
     The existance of p_node determines which type of alter statement this is.
   */
  status = TR_STATUS_INVALID;

  if (p_node == NULL)
    {
      status =
	convert_misc_to_tr_status (statement->info.alter_trigger.
				   trigger_status);
    }
  else
    {
      priority = get_priority (parser, p_node);
    }

  if (error == NO_ERROR)
    {
      /* make sure we have ALTER authorization on all the triggers
         before proceeding */
      for (t = triggers; t != NULL && error == NO_ERROR; t = t->next)
	{
	  error = tr_check_authorization (t->op, true);
	}

      if (error == NO_ERROR)
	{
	  for (t = triggers; t != NULL && error == NO_ERROR; t = t->next)
	    {
	      if (status != TR_STATUS_INVALID)
		{
		  error = tr_set_status (t->op, status, false);
		}

	      if (error == NO_ERROR && p_node != NULL)
		{
		  error = tr_set_priority (t->op, priority, false);
		}
	    }
	}
    }

  if (triggers != NULL)
    {
      ml_ext_free (triggers);
    }

  return error;
}

/* 
 * do_execute_trigger() - Execute the deferred activities for one or more 
 *			  triggers.
 *   return: Error code
 *   parser(in): Parse context
 *   statement(in): Parse tree of a execute trigger statement
 *
 * Note: 
 */
int
do_execute_trigger (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int error = NO_ERROR;
  PT_NODE *speclist;
  DB_OBJLIST *triggers, *t;

  speclist = statement->info.execute_trigger.trigger_spec_list;
  error = convert_speclist_to_objlist (&triggers, speclist);

  if (error == NO_ERROR && triggers != NULL)
    {
      for (t = triggers; t != NULL && error == NO_ERROR; t = t->next)
	{
	  error = tr_execute_deferred_activities (t->op, NULL);
	}
      ml_ext_free (triggers);
    }

  return error;
}

/* 
 * do_remove_trigger() - Remove the deferred activities for one or more triggers
 *   return: Error code
 *   parser(in): Parse context
 *   statement(in): Parse tree of a remove trigger statement
 *
 * Note: 
 */
int
do_remove_trigger (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int error = NO_ERROR;
  PT_NODE *speclist;
  DB_OBJLIST *triggers, *t;

  if (PRM_BLOCK_DDL_STATEMENT)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_AU_AUTHORIZATION_FAILURE,
	      0);
      return ER_AU_AUTHORIZATION_FAILURE;
    }

  speclist = statement->info.remove_trigger.trigger_spec_list;
  error = convert_speclist_to_objlist (&triggers, speclist);

  if (error == NO_ERROR && triggers != NULL)
    {
      for (t = triggers; t != NULL && error == NO_ERROR; t = t->next)
	{
	  error = tr_drop_deferred_activities (t->op, NULL);
	}

      ml_ext_free (triggers);
    }

  return error;
}

/* 
 * do_rename_trigger() - Rename a trigger
 *   return: Error code
 *   parser(in): Parse context
 *   statement(in): Parse tree of a rename trigger statement
 *
 * Note: 
 */
int
do_rename_trigger (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int error = NO_ERROR;
  const char *old_name, *new_name;
  DB_OBJECT *trigger;

  if (PRM_BLOCK_DDL_STATEMENT)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_AU_AUTHORIZATION_FAILURE,
	      0);
      return ER_AU_AUTHORIZATION_FAILURE;
    }

  old_name = statement->info.rename_trigger.old_name->info.name.original;
  new_name = statement->info.rename_trigger.new_name->info.name.original;

  trigger = tr_find_trigger (old_name);
  if (trigger == NULL)
    {
      error = er_errid ();
    }
  else
    {
      error = tr_rename_trigger (trigger, new_name, false);
    }

  return error;
}

/* 
 * do_set_trigger() - Set one of the trigger options
 *   return: Error code
 *   parser(in): Parse context
 *   statement(in): Parse tree of a set trigger statement
 *
 * Note: 
 */
int
do_set_trigger (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int error = NO_ERROR;
  DB_VALUE src, dst;

  pt_evaluate_tree (parser, statement->info.set_trigger.val, &src);
  if (pt_has_error (parser))
    {
      pt_report_to_ersys (parser, PT_SEMANTIC);
      error = er_errid ();
    }
  else
    {
      switch (tp_value_coerce (&src, &dst, &tp_Integer_domain))
	{
	case DOMAIN_INCOMPATIBLE:
	  error = ER_TP_CANT_COERCE;
	  break;
	case DOMAIN_OVERFLOW:
	  error = ER_TP_CANT_COERCE_OVERFLOW;
	  break;
	case DOMAIN_ERROR:
	  /*
	   * tp_value_coerce() *appears* to set er_errid whenever it
	   * returns DOMAIN_ERROR (which probably really means malloc
	   * failure or something else).
	   */
	  error = er_errid ();
	  break;
	default:
	  break;
	}
    }

  if (error == ER_TP_CANT_COERCE || error == ER_TP_CANT_COERCE_OVERFLOW)
    {
      char buf1[MAX_DOMAIN_NAME_SIZE];
      char buf2[MAX_DOMAIN_NAME_SIZE];
      (void) tp_value_domain_name (&src, buf1, sizeof (buf1));
      (void) tp_domain_name (&tp_Integer_domain, buf2, sizeof (buf2));
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 2, buf1, buf2);
    }
  else if (error == NO_ERROR)
    {
      PT_MISC_TYPE option;
      int v;

      option = statement->info.set_trigger.option;
      v = DB_GET_INT (&dst);

      if (option == PT_TRIGGER_TRACE)
	{
	  error = tr_set_trace (v);
	}
      else if (option == PT_TRIGGER_DEPTH)
	{
	  error = tr_set_depth (v);
	}
    }

  /*
   * No need to clear dst, because it's either NULL or an integer at
   * this point.  src could be arbitrarily complex, and it was created
   * by pt_evaluate_tree, so we need to clear it before we leave.
   */
  db_value_clear (&src);

  return error;
}

/* 
 * do_get_trigger() - Get one of the trigger option values.
 *   return: Error code
 *   parser(in): Parse context
 *   statement(in/out): Parse tree of a get trigger statement
 *
 * Note: 
 */
int
do_get_trigger (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int error = NO_ERROR;
  const char *into_label;
  DB_VALUE *ins_val;
  PT_NODE *into;
  PT_MISC_TYPE option;

  /* create a value to hold the result */
  ins_val = db_value_create ();

  option = statement->info.set_trigger.option;
  switch (option)
    {
    case PT_TRIGGER_DEPTH:
      db_make_int (ins_val, tr_get_depth ());
      break;
    case PT_TRIGGER_TRACE:
      db_make_int (ins_val, tr_get_trace ());
      break;
    default:
      db_make_null (ins_val);	/* can't happen */
      break;
    }

  statement->etc = (void *) ins_val;

  into = statement->info.get_trigger.into_var;
  if (into != NULL
      && into->node_type == PT_NAME
      && (into_label = into->info.name.original) != NULL)
    {
      /* create another DB_VALUE for the label table */
      ins_val = db_value_copy (ins_val);

      /* enter the value into the table */
      error = pt_associate_label_with_value (into_label, ins_val);
    }

  return error;
}
