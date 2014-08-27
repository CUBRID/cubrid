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
 * trigger_manager.c - Trigger Manager
 */

#ident "$Id$"

#include "config.h"

#include "misc_string.h"
#include "memory_alloc.h"
#include "error_manager.h"
#include "dbtype.h"
#include "dbdef.h"
#include "trigger_manager.h"
#include "memory_hash.h"
#include "work_space.h"
#include "schema_manager.h"
#include "object_accessor.h"
#include "set_object.h"
#include "authenticate.h"
#include "db.h"
#include "parser.h"
#include "system_parameter.h"
#include "locator_cl.h"

#include "dbval.h"		/* this must be the last header file included!!! */

#define TR_EXECUTION_ENABLED (tr_Execution_enabled == true)


/*
 * IS_USER_EVENT
 * IS_CLASSEVENT
 *
 * Note:
 *    Shorthand macros for determing if an event constant represents
 *    one of the "user" events or one of the "class" events.
 *
 */

#define IS_USER_EVENT(event) \
  ((event) == TR_EVENT_COMMIT || \
   (event) == TR_EVENT_ROLLBACK || \
   (event) == TR_EVENT_ABORT || \
   (event) == TR_EVENT_TIMEOUT)

/* TR_EVENT_NULL and TR_EVENT_ALL are treated as class events */
#define IS_CLASS_EVENT(event) (!IS_USER_EVENT(event))

/*
 * Use this to compare trigger names, should be usign the same thing
 * we use for class names.
 */

#define COMPARE_TRIGGER_NAMES intl_identifier_casecmp


/* Should have something in ER for this */
#define MAX_ERROR_STRING 2048

/*
 * IMPORTANT, while evaluating a trigger condition/action, we
 * set the effective user to the owner of the trigger.   This
 * is primarily of importance if the trigger is accessed through
 * a view.
 */


#define MAX_USER_NAME 32	/* actually its 8 */

static char namebuf[MAX_USER_NAME];

/*
 * TR_RETURN_ codes
 *
 * Note:
 *    These are used by functions that need to return 3 valued logic.
 */

static const int TR_RETURN_ERROR = -1;
static const int TR_RETURN_FALSE = 0;
static const int TR_RETURN_TRUE = 1;

/*
 * tr_init
 *
 * Note: Trigger initialization function.
 *              This function should be called at the beginning of a
 *              transaction.
 */

static const int TR_EST_MAP_SIZE = 1024;

static const char *OBJ_REFERENCE_NAME = "obj";
static const char *NEW_REFERENCE_NAME = "new";
static const char *OLD_REFERENCE_NAME = "old";

/*
 * Formerly had a semicolon at the end, not sure that is acceptable since
 * pt_compile_trigger_stmt is going to surround this in a SCOPE___ statement.
 * Currently, the evaluate grammar must have parens surrounding the expression.
 */

static const char *EVAL_PREFIX = "EVALUATE ( ";
static const char *EVAL_SUFFIX = " ) ";

const char *TR_CLASS_NAME = "db_trigger";
const char *TR_ATT_NAME = "name";
const char *TR_ATT_OWNER = "owner";
const char *TR_ATT_EVENT = "event";
const char *TR_ATT_STATUS = "status";
const char *TR_ATT_PRIORITY = "priority";
const char *TR_ATT_CLASS = "target_class";
const char *TR_ATT_ATTRIBUTE = "target_attribute";
const char *TR_ATT_CLASS_ATTRIBUTE = "target_class_attribute";
const char *TR_ATT_CONDITION_TYPE = "condition_type";
const char *TR_ATT_CONDITION_TIME = "condition_time";
const char *TR_ATT_CONDITION = "condition";
const char *TR_ATT_ACTION_TYPE = "action_type";
const char *TR_ATT_ACTION_TIME = "action_time";
const char *TR_ATT_ACTION = "action_definition";
const char *TR_ATT_ACTION_OLD = "action";
const char *TR_ATT_PROPERTIES = "properties";

int tr_Current_depth = 0;
int tr_Maximum_depth = TR_MAX_RECURSION_LEVEL;
OID tr_Stack[TR_MAX_RECURSION_LEVEL + 1];

bool tr_Invalid_transaction = false;
char tr_Invalid_transaction_trigger[SM_MAX_IDENTIFIER_LENGTH + 2];

bool tr_Trace = true;

TR_DEFERRED_CONTEXT *tr_Deferred_activities = NULL;
TR_DEFERRED_CONTEXT *tr_Deferred_activities_tail = NULL;

static int tr_User_triggers_valid = 0;
static int tr_User_triggers_modified = 0;
static TR_TRIGLIST *tr_User_triggers = NULL;

static TR_TRIGLIST *tr_Uncommitted_triggers = NULL;
static TR_SCHEMA_CACHE *tr_Schema_caches = NULL;

/*
 * Global trigger firing state flag, used to determine if triggers are fired.
 * This can be modified using tr_set_execution_enabled().
 */
static bool tr_Execution_enabled = true;

/*
 * tr_object_map
 *
 * Note:
 *    This is a hash table that maps trigger object MOPs into trigger
 *    structures.
 *    It will be initialized and freed by tr_init & tr_final.
 *
 */

static MHT_TABLE *tr_object_map = NULL;

static const char *time_as_string (DB_TRIGGER_TIME tr_time);
static char *tr_process_name (const char *name_string);
static TR_ACTIVITY *make_activity (void);
static void free_activity (TR_ACTIVITY * act);
static TR_TRIGGER *tr_make_trigger (void);
static void tr_clear_trigger (TR_TRIGGER * trigger);
static void free_trigger (TR_TRIGGER * trigger);

static int insert_trigger_list (TR_TRIGLIST ** list, TR_TRIGGER * trigger);
static int merge_trigger_list (TR_TRIGLIST ** list, TR_TRIGLIST * more,
			       int destructive);
static void remove_trigger_list_element (TR_TRIGLIST ** list,
					 TR_TRIGLIST * element);
static void remove_trigger_list (TR_TRIGLIST ** list, TR_TRIGGER * trigger);
static void reinsert_trigger_list (TR_TRIGLIST ** list, TR_TRIGGER * trigger);
static TR_DEFERRED_CONTEXT *add_deferred_activity_context (void);
static int add_deferred_activities (TR_TRIGLIST * triggers, MOP current);
static void flush_deferred_activities (void);
static void remove_deferred_activity (TR_DEFERRED_CONTEXT * context,
				      TR_TRIGLIST * element);
static void remove_deferred_context (TR_DEFERRED_CONTEXT * c);

static TR_STATE *make_state (void);
static void free_state (TR_STATE * state);
static DB_OBJECT *trigger_to_object (TR_TRIGGER * trigger);
static int object_to_trigger (DB_OBJECT * object, TR_TRIGGER * trigger);
static void get_reference_names (TR_TRIGGER * trigger, TR_ACTIVITY * activity,
				 const char **curname, const char **tempname);
static int compile_trigger_activity (TR_TRIGGER * trigger,
				     TR_ACTIVITY * activity,
				     int with_evaluate);
static int validate_trigger (TR_TRIGGER * trigger);

static int register_user_trigger (DB_OBJECT * object);
static int unregister_user_trigger (TR_TRIGGER * trigger, int rollback);
static int get_user_trigger_objects (DB_TRIGGER_EVENT event,
				     bool active_filter,
				     DB_OBJLIST ** trigger_list);

static void reorder_schema_caches (TR_TRIGGER * trigger);
static int trigger_table_add (const char *name, DB_OBJECT * trigger);
static int trigger_table_find (const char *name, DB_OBJECT ** trigger_p);
static int trigger_table_rename (DB_OBJECT * trigger_object,
				 const char *newname);
static int trigger_table_drop (const char *name);
static bool check_authorization (TR_TRIGGER * trigger, bool alter_flag);
static int find_all_triggers (bool active_filter, bool alter_filter,
			      DB_OBJLIST ** list);
static int get_schema_trigger_objects (DB_OBJECT * class_mop,
				       const char *attribute,
				       DB_TRIGGER_EVENT event,
				       bool active_flag,
				       DB_OBJLIST ** objlist);
static int find_event_triggers (DB_TRIGGER_EVENT event, DB_OBJECT * class_mop,
				const char *attribute, bool active_filter,
				DB_OBJLIST ** list);
static bool check_target (DB_TRIGGER_EVENT event, DB_OBJECT * class_mop,
			  const char *attribute);
static int check_semantics (TR_TRIGGER * trigger);
static PT_NODE *tr_check_correlation (PARSER_CONTEXT * parser, PT_NODE * node,
				      void *arg, int *walk_on);

static int tr_drop_trigger_internal (TR_TRIGGER * trigger, int rollback);

static bool value_as_boolean (DB_VALUE * value);
static int signal_evaluation_error (TR_TRIGGER * trigger, int error);
static int eval_condition (TR_TRIGGER * trigger, DB_OBJECT * current,
			   DB_OBJECT * temp, bool * status);
static int eval_action (TR_TRIGGER * trigger, DB_OBJECT * current,
			DB_OBJECT * temp, bool * reject);
static int execute_activity (TR_TRIGGER * trigger, DB_TRIGGER_TIME tr_time,
			     DB_OBJECT * current, DB_OBJECT * temp,
			     bool * rejected);
static int tr_execute_activities (TR_STATE * state,
				  DB_TRIGGER_TIME tr_time,
				  DB_OBJECT * current, DB_OBJECT * temp);
static int run_user_triggers (DB_TRIGGER_EVENT event, DB_TRIGGER_TIME time);
static int compare_recursion_levels (int rl_1, int rl_2);
static TR_STATE *start_state (TR_STATE ** current, const char *name);
static void tr_finish (TR_STATE * state);

static int its_deleted (DB_OBJECT * object);
static char *get_user_name (DB_OBJECT * user);
static int is_required_trigger (TR_TRIGGER * trigger, DB_OBJLIST * classes);

static int map_flush_helper (const void *key, void *data, void *args);
static int define_trigger_classes (void);

static TR_RECURSION_DECISION tr_check_recursivity (OID oid, OID stack[],
						   int stack_size,
						   bool is_statement);

/* ERROR HANDLING */

/*
 * time_as_string() - The function returns a illuminating string for a time
 *                    constant.
 *    return: const char
 *    tr_time(in): execution time
 *
 */
static const char *
time_as_string (DB_TRIGGER_TIME tr_time)
{
  const char *return_str;

  switch (tr_time)
    {
    case TR_TIME_NULL:
      return_str = "NULL";
      break;

    case TR_TIME_BEFORE:
      return_str = "BEFORE";
      break;

    case TR_TIME_AFTER:
      return_str = "AFTER";
      break;

    case TR_TIME_DEFERRED:
      return_str = "DEFERRED";
      break;

    default:
      return_str = "UNKNOWN";
      break;
    }

  return return_str;
}

/* UTILITIES */

/*
 * tr_process_name() - This copies and processes a trigger name string.
 *    return: trigger name string
 *    str(in): proposed name string
 *
 * Note:
 *    The processing for trigger names is similar to that for class names.
 *    The name must not contain any invalid characters
 *    as defined by sm_check_name and will be downcased before
 *    it is assigned.
 */
static char *
tr_process_name (const char *name_string)
{
  char buffer[SM_MAX_IDENTIFIER_LENGTH + 2];
  char *name = NULL;

  if (sm_check_name (name_string))
    {
      sm_downcase_name (name_string, buffer, SM_MAX_IDENTIFIER_LENGTH);
      name = strdup (buffer);
    }
  return (name);
}


/* ACTIVITY STRUCTURES */

/*
 * make_activity() - Construct an activity structure.
 *    return: allocate and initialize an activity structure
 */
static TR_ACTIVITY *
make_activity (void)
{
  TR_ACTIVITY *act;

  act = (TR_ACTIVITY *) malloc (sizeof (TR_ACTIVITY));
  if (act != NULL)
    {
      act->type = TR_ACT_NULL;
      act->time = TR_TIME_NULL;
      act->source = NULL;
      act->parser = NULL;
      act->statement = NULL;
      act->exec_cnt = 0;
    }
  return (act);
}

/*
 * free_activity() - This frees storage for an activity.
 *    return: none
 *    activity(in): activity to free
 */
static void
free_activity (TR_ACTIVITY * activity)
{
  if (activity)
    {
      if (activity->source)
	{
	  free_and_init (activity->source);
	}

      if (activity->parser != NULL)
	{
	  /*
	   * We need to free the statement explicitly here since it may
	   *  contain pointers to db_values in the workspace.
	   */
	  parser_free_tree ((PARSER_CONTEXT *) activity->parser,
			    (PT_NODE *) activity->statement);
	  parser_free_parser ((PARSER_CONTEXT *) activity->parser);
	}
      free_and_init (activity);
    }
}


/* TRIGGER STRUCTURES */

/*
 * tr_make_trigger() - Memory allocation function for TR_TRIGGER
 *    return: a TR_TRIGGER pointer
 *
 */
static TR_TRIGGER *
tr_make_trigger (void)
{
  TR_TRIGGER *trigger;

  trigger = (TR_TRIGGER *) malloc (sizeof (TR_TRIGGER));

  if (trigger != NULL)
    {
      trigger->owner = NULL;
      trigger->object = NULL;
      trigger->name = NULL;
      trigger->status = TR_STATUS_INVALID;
      trigger->priority = TR_LOWEST_PRIORITY;
      trigger->event = TR_EVENT_NULL;
      trigger->class_mop = NULL;
      trigger->attribute = NULL;
      trigger->class_attribute = 0;
      trigger->condition = NULL;
      trigger->action = NULL;
      trigger->current_refname = NULL;
      trigger->temp_refname = NULL;
      trigger->chn = NULL_CHN;
    }

  return (trigger);
}

/*
 * tr_clear_trigger() - This clears the contents of a trigger.
 *    return: none
 *    trigger(in): trigger to clear
 *
 * Note:
 *    Don't clear the cache pointer since it remains valid.
 *    Since we cant alter the trigger class/owner etc. without deleting
 *    the trigger, we probably don't need to reset those fields either.
 */
static void
tr_clear_trigger (TR_TRIGGER * trigger)
{
  if (trigger)
    {
      /* make sure to clear any DB_OBJECT* pointers for garbage collection */
      trigger->owner = NULL;
      trigger->object = NULL;
      trigger->class_mop = NULL;

      if (trigger->name)
	{
	  free_and_init (trigger->name);
	}
      if (trigger->attribute)
	{
	  free_and_init (trigger->attribute);
	}
      if (trigger->condition)
	{
	  free_activity (trigger->condition);
	  trigger->condition = NULL;
	}
      if (trigger->action)
	{
	  free_activity (trigger->action);
	  trigger->action = NULL;
	}
    }
}

/*
 * free_trigger() - Frees all storage for an allocated trigger.
 *    return: none
 *    trigger(in): trigger to free
 *
 */
static void
free_trigger (TR_TRIGGER * trigger)
{
  if (trigger)
    {
      tr_clear_trigger (trigger);
      free_and_init (trigger);
    }
}


/* TRIGLIST STRUCTURES */

/*
 * tr_free_trigger_list() - Memory free function for TR_TRIGLIST.
 *    return: none
 *    list(in): pointer to a trigger list
 *
 * Note:
 *    Since these things can go in the schema cache attached
 *    to the class, use WS_ALLOC to avoid warnings when shutting down
 *    the database.
 */
void
tr_free_trigger_list (TR_TRIGLIST * list)
{
  TR_TRIGLIST *node, *next;

  for (node = list, next = NULL; node != NULL; node = next)
    {
      next = node->next;
      db_ws_free (node);
    }
}

/*
 * insert_trigger_list() - This inserts a node in a triglist for a new trigger
 *                     structure.
 *    return: error code
 *    list(in/out): pointer to a list head
 *    trigger(in): trigger to insert
 *
 * Note:
 *    The nodes in the triglist are ordered based on trigger priority.
 *    Since these things can go in the schema cache attached to the class,
 *    use WS_ALLOC to avoid warnings when shutting down the database.
 */
static int
insert_trigger_list (TR_TRIGLIST ** list, TR_TRIGGER * trigger)
{
  TR_TRIGLIST *t, *prev, *new_;

  /* scoot up to the appropriate position in the list */
  for (t = *list, prev = NULL;
       t != NULL && t->trigger->priority > trigger->priority;
       prev = t, t = t->next);

  /* make a new node and link it in */
  new_ = (TR_TRIGLIST *) db_ws_alloc (sizeof (TR_TRIGLIST));
  if (new_ == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  new_->trigger = trigger;
  new_->next = t;
  new_->prev = prev;
  new_->target = NULL;

  if (t != NULL)
    {
      t->prev = new_;
    }

  if (prev == NULL)
    {
      *list = new_;
    }
  else
    {
      prev->next = new_;
    }

  return NO_ERROR;
}

/*
 * Hack on this, it could be better.
 *
 * Need to fix this so that it pays attention to the filter_inactive flag.
 */


/*
 * merge_trigger_list() - This adds the contents of one sorted trigger list
 *                       to another.
 *    return: none
 *    list(in/out): pointer to a list head
 *    more(in): list to merge
 *    destructive(in): non-zero if the new list can be physically combined
 *
 * Note:
 *    It is used to combine the various sorted trigger lists to construct
 *    the consolodated trigger list for a particular event.
 *    Since these things can go in the schema cache attached to the class,
 *    use WS_ALLOC to avoid warnings when shutting down the database.
 */
static int
merge_trigger_list (TR_TRIGLIST ** list, TR_TRIGLIST * more, int destructive)
{
  TR_TRIGLIST *t1, *t2, *prev, *new_, *t2_next;

  /* quick exit if nothing to do */
  if (more == NULL)
    return NO_ERROR;

  t1 = *list;
  t2 = more;
  prev = NULL;

  while (t2 != NULL)
    {
      t2_next = t2->next;

      /* scoot up to the appropriate position in the list */
      for (; t1 != NULL && t1->trigger->priority > t2->trigger->priority;
	   prev = t1, t1 = t1->next);

      if (destructive)
	{
	  new_ = t2;
	}
      else
	{
	  new_ = (TR_TRIGLIST *) db_ws_alloc (sizeof (TR_TRIGLIST));
	  if (new_ == NULL)
	    {
	      assert (er_errid () != NO_ERROR);
	      return er_errid ();
	    }
	  new_->trigger = t2->trigger;
	  new_->target = NULL;
	}

      new_->next = t1;
      new_->prev = prev;

      if (prev == NULL)
	{
	  *list = new_;
	}
      else
	{
	  prev->next = new_;
	}

      if (t1 != NULL)
	{
	  t1->prev = new_;
	}

      prev = new_;
      t2 = t2_next;
    }

  return NO_ERROR;
}

/*
 * remove_trigger_list_element() - This removes a particular element from
 *                                 a trigger list.
 *    return: none
 *    list(in/out): trigger list
 *    element(in): trigger list element
 *
 * Note:
 *    It is assumed that the given element actually exists in the list.
 *
 */
static void
remove_trigger_list_element (TR_TRIGLIST ** list, TR_TRIGLIST * element)
{
  if (element->prev == NULL)
    {
      *list = element->next;
    }
  else
    {
      element->prev->next = element->next;
    }

  if (element->next != NULL)
    {
      element->next->prev = element->prev;
    }

  element->next = NULL;
  tr_free_trigger_list (element);
}

/*
 * remove_trigger_list() - This removes a trigger from a trigger list.
 *    return: none
 *    list(in/out): trigger list
 *    trigger(in): trigger to remove
 *
 */
static void
remove_trigger_list (TR_TRIGLIST ** list, TR_TRIGGER * trigger)
{
  TR_TRIGLIST *element;

  for (element = *list; element != NULL && element->trigger != trigger;
       element = element->next);
  if (element != NULL)
    {
      remove_trigger_list_element (list, element);
    }
}

/*
 * reinsert_trigger_list() - This is used primarily to implement the altering
 *                           of trigger priorities.
 *    return: none
 *    list(in/out): trigger list pointer
 *    trigger(in): trigger to insert/re-insert
 *
 * Note:
 *    It will search a trigger list for the given trigger,
 *    if the trigger is found in the list, it will be removed from
 *    its current location and re-inserted in the list based on its
 *    current priority.  If the trigger isn't in the list, it does nothing.
 *
 */
static void
reinsert_trigger_list (TR_TRIGLIST ** list, TR_TRIGGER * trigger)
{
  TR_TRIGLIST *element;

  for (element = *list; element != NULL && element->trigger != trigger;
       element = element->next);
  /*
   * note, since we just freed a triglist element, it should be possible
   * to allocated a new one without error, need to have these in
   * a resource for faster allocation.
   */
  if (element != NULL)
    {
      remove_trigger_list_element (list, element);
      insert_trigger_list (list, trigger);
    }
}

/* DEFERRED ACTIVITY MAINTENANCE */

/*
 * add_deferred_activity_context() - This adds another element to the deferred
 *                                   activity list.
 *    return: new context structure
 *
 * Note:
 *    This adds another element to the deferred activity list.
 *    This is called once to establish the initial context and will also
 *    be called each time a subsequent savepoint context is required.
 */
static TR_DEFERRED_CONTEXT *
add_deferred_activity_context (void)
{
  TR_DEFERRED_CONTEXT *def;

  def = (TR_DEFERRED_CONTEXT *) malloc (sizeof (TR_DEFERRED_CONTEXT));
  if (def == NULL)
    {
      return NULL;
    }
  else
    {
      def->next = NULL;
      def->prev = NULL;
      def->head = NULL;
      def->tail = NULL;
      def->savepoint_id = (void *) -1;
    }

  if (tr_Deferred_activities == NULL)
    {
      tr_Deferred_activities = def;
      tr_Deferred_activities_tail = def;
    }
  else
    {
      tr_Deferred_activities_tail->next = def;
      def->prev = tr_Deferred_activities_tail;
    }

  return def;
}

/*
 * add_deferred_activities() - This adds a list of triggers to the current
 *                             deferred activity context.
 *    return: error code
 *    triggers(in): trigger list
 *    current(in): associated target object
 *
 * Note:
 *    If a context has not been allocated, a new one is created.
 *    The triggers are appended to the context's trigger list.
 *    Each trigger is stamped with the current recursion level and
 *    the associated target object.
 */
static int
add_deferred_activities (TR_TRIGLIST * triggers, MOP current)
{
  TR_DEFERRED_CONTEXT *def;
  TR_TRIGLIST *t, *last;

  def = tr_Deferred_activities_tail;
  if (def == NULL)
    {
      def = add_deferred_activity_context ();
      if (def == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  return er_errid ();
	}
    }

  /*
   * tag the list entries with the current recursion level and
   * the target object
   */
  for (t = triggers, last = NULL; t != NULL; t = t->next)
    {
      t->recursion_level = tr_Current_depth;
      last = t;
      t->target = current;
    }

  /* concatenate the activites to the master list */
  if (def->head == NULL)
    {
      def->head = triggers;
    }
  else
    {
      def->tail->next = triggers;
      triggers->prev = def->tail;
    }
  def->tail = last;

  return NO_ERROR;
}

/*
 * flush_deferred_activities() - Flushes any remaining entries on the
 *                               deferred activity list.
 *    return: none
 *
 */
static void
flush_deferred_activities (void)
{
  TR_DEFERRED_CONTEXT *c, *next;

  for (c = tr_Deferred_activities, next = NULL; c != NULL; c = next)
    {
      next = c->next;
      tr_free_trigger_list (c->head);
      free_and_init (c);
    }
  tr_Deferred_activities = tr_Deferred_activities_tail = NULL;
}

/*
 * remove_deferred_activity() - This removes an element from the trigger list
 *                              of a deferred activity context.
 *    return: none
 *    context(in): activity context
 *    element(in): activity to remove
 *
 */
static void
remove_deferred_activity (TR_DEFERRED_CONTEXT * context,
			  TR_TRIGLIST * element)
{
  if (context->tail == element)
    {
      context->tail = element->prev;
    }
  remove_trigger_list_element (&context->head, element);
}

/*
 * remove_deferred_context() - Removes an activity context from the global list
 *                             This can happen if the context's trigger list
 *                             becomes empty.
 *    return: none
 *    context(in): context
 *
 */
static void
remove_deferred_context (TR_DEFERRED_CONTEXT * context)
{
  if (context->prev != NULL)
    context->prev->next = context->next;
  else
    {
      if (context->next == NULL)
	{
	  tr_Deferred_activities = tr_Deferred_activities_tail = NULL;
	}
      else
	{
	  tr_Deferred_activities = context->next;
	}
    }
  free_and_init (context);
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * tr_set_savepoint() - This establishes a new context for scheduling deferred
 *                      trigger activities.
 *    return: error code
 *    savepoint_id(in): savepoint id
 *
 * Note:
 *    It will be called by the transaction manager whenever a savepoint is
 *    established.
 */
int
tr_set_savepoint (void *savepoint_id)
{
  TR_DEFERRED_CONTEXT *def;

  def = tr_Deferred_activities_tail;
  if (def != NULL && def->head != NULL)
    {
      /* mark this with the supplied savepoint id */
      def->savepoint_id = savepoint_id;

      /* build a new one on top of this */
      if (add_deferred_activity_context ())
	{
	  assert (er_errid () != NO_ERROR);
	  return er_errid ();
	}
    }
  return NO_ERROR;
}

/*
 * tr_abort_to_savepoint() - This aborts the activity contexts back to
 *                           a context with an id that matches the supplied id.
 *    return: error code
 *    savepoint_id(in): savepoint id to abort back to
 *
 */
int
tr_abort_to_savepoint (void *savepoint_id)
{
  TR_DEFERRED_CONTEXT *save, *prev;

  for (save = tr_Deferred_activities_tail, prev = NULL;
       save != NULL && save->savepoint_id != savepoint_id; save = prev)
    {
      prev = save->prev;
      /* throw away the scheduled triggers */
      tr_free_trigger_list (save->head);
      free_and_init (save);
    }
  if (save != NULL)
    {
      save->next = NULL;
    }

  return NO_ERROR;
}
#endif /* ENABLE_UNUSED_FUNCTION */

/* TRIGGER STATE STRUCTURES */

/*
 * make_state() - Memory allocation function for TR_STATE.
 *    return: TR_STATE *
 *
 */
static TR_STATE *
make_state (void)
{
  TR_STATE *state;

  state = (TR_STATE *) malloc (sizeof (TR_STATE));
  if (state != NULL)
    {
      state->triggers = NULL;
    }

  return state;
}

/*
 * free_state() - Frees a trigger state structure.
 *
 *    return: none
 *    state(in): state structure to free
 *
 */
static void
free_state (TR_STATE * state)
{
  if (state)
    {
      tr_free_trigger_list (state->triggers);
      free_and_init (state);
    }
}


/* TRIGGER INSTANCES */

/*
 * The trigger instance is used to store the trigger definition in
 * the database.  When the trigger is brought into memory, it is converted
 * into a run-time trigger structure for fast access.  See the
 * trigger object map below.
 *
 */

/*
 * trigger_to_object() - This converts a trigger trigger into a database object
 *    return: trigger database object
 *    trigger(in/out): trigger structure
 *
 */
static DB_OBJECT *
trigger_to_object (TR_TRIGGER * trigger)
{
  DB_OBJECT *object_p, *class_p;
  DB_OTMPL *obt_p;
  DB_VALUE value;
  int save;
  MOBJ obj;

  AU_DISABLE (save);

  object_p = NULL;
  obt_p = NULL;

  if ((class_p = db_find_class (TR_CLASS_NAME)) == NULL)
    goto error;

  if ((obt_p = dbt_create_object_internal (class_p)) == NULL)
    goto error;

  db_make_object (&value, trigger->owner);

  if (dbt_put_internal (obt_p, TR_ATT_OWNER, &value))
    goto error;

  db_make_string (&value, trigger->name);

  if (dbt_put_internal (obt_p, TR_ATT_NAME, &value))
    goto error;

  db_make_int (&value, trigger->status);

  if (dbt_put_internal (obt_p, TR_ATT_STATUS, &value))
    goto error;

  db_make_float (&value, (float) trigger->priority);

  if (dbt_put_internal (obt_p, TR_ATT_PRIORITY, &value))
    goto error;

  db_make_int (&value, trigger->event);

  if (dbt_put_internal (obt_p, TR_ATT_EVENT, &value))
    goto error;

  db_make_object (&value, trigger->class_mop);

  if (dbt_put_internal (obt_p, TR_ATT_CLASS, &value))
    goto error;

  db_make_string (&value, trigger->attribute);

  if (dbt_put_internal (obt_p, TR_ATT_ATTRIBUTE, &value))
    goto error;

  db_make_int (&value, trigger->class_attribute);

  if (dbt_put_internal (obt_p, TR_ATT_CLASS_ATTRIBUTE, &value))
    goto error;

  if (trigger->condition != NULL)
    {
      db_make_int (&value, trigger->condition->type);

      if (dbt_put_internal (obt_p, TR_ATT_CONDITION_TYPE, &value))
	goto error;

      db_make_int (&value, trigger->condition->time);

      if (dbt_put_internal (obt_p, TR_ATT_CONDITION_TIME, &value))
	goto error;

      db_make_string (&value, trigger->condition->source);

      if (dbt_put_internal (obt_p, TR_ATT_CONDITION, &value))
	goto error;
    }

  if (trigger->action != NULL)
    {
      db_make_int (&value, trigger->action->type);

      if (dbt_put_internal (obt_p, TR_ATT_ACTION_TYPE, &value))
	goto error;

      db_make_int (&value, trigger->action->time);

      if (dbt_put_internal (obt_p, TR_ATT_ACTION_TIME, &value))
	goto error;

      db_make_string (&value, trigger->action->source);

      if (dbt_put_internal (obt_p, TR_ATT_ACTION, &value))
	{
	  /* hack, try old name before aborting */
	  if (dbt_put_internal (obt_p, TR_ATT_ACTION_OLD, &value))
	    goto error;

	}
    }

  if ((object_p = dbt_finish_object (obt_p)) != NULL)
    {
      trigger->object = object_p;
      obt_p = NULL;

      /* get the CHN so we know this template is still valid */
      if (au_fetch_instance_force (object_p, &obj, AU_FETCH_READ) == 0)
	{
	  trigger->chn = WS_CHN (obj);
	}
      else
	{
	  /* shoudln't happen, make sure the trigger is disconnected */
	  trigger->object = NULL;
	  object_p = NULL;
	}
    }

error:
  if (obt_p != NULL)
    {
      dbt_abort_object (obt_p);
    }

  AU_ENABLE (save);
  return object_p;
}

/*
 * object_to_trigger() - This converts a trigger object from the database into
 *                       a C structure.
 *    return: error code
 *    object(in): trigger object
 *    trigger(in/out): trigger structure to fill in
 *
 */
static int
object_to_trigger (DB_OBJECT * object, TR_TRIGGER * trigger)
{
  DB_VALUE value;
  int save;
  MOBJ obj;
  SM_CLASS *class_;
  char *tmp;

  AU_DISABLE (save);

  /* initialize the trigger to a known default state */
  trigger->owner = NULL;
  trigger->object = object;
  trigger->name = NULL;
  trigger->status = TR_STATUS_INVALID;
  trigger->priority = TR_LOWEST_PRIORITY;
  trigger->event = TR_EVENT_NULL;
  trigger->class_mop = NULL;
  trigger->attribute = NULL;
  trigger->condition = NULL;
  trigger->action = NULL;

  /*
   * Save the cache coherency number so we know when to re-calculate the
   * cache
   */
  if (au_fetch_instance_force (object, &obj, AU_FETCH_READ))
    goto error;

  trigger->chn = WS_CHN (obj);

  /* OWNER */
  if (db_get (object, TR_ATT_OWNER, &value))
    goto error;

  if (DB_VALUE_TYPE (&value) == DB_TYPE_OBJECT)
    {
      if (DB_IS_NULL (&value))
	{
	  trigger->owner = NULL;
	}
      else
	{
	  trigger->owner = DB_GET_OBJECT (&value);
	}
    }

  /* NAME */
  if (db_get (object, TR_ATT_NAME, &value))
    goto error;

  if (DB_VALUE_TYPE (&value) == DB_TYPE_STRING && !DB_IS_NULL (&value))
    {
      tmp = DB_GET_STRING (&value);
      if (tmp)
	{
	  trigger->name = strdup (tmp);
	}
    }
  db_value_clear (&value);

  /* STATUS */
  if (db_get (object, TR_ATT_STATUS, &value))
    goto error;

  if (DB_VALUE_TYPE (&value) == DB_TYPE_INTEGER)
    trigger->status = (DB_TRIGGER_STATUS) DB_GET_INTEGER (&value);

  /* PRIORITY */
  if (db_get (object, TR_ATT_PRIORITY, &value))
    goto error;

  if (DB_VALUE_TYPE (&value) == DB_TYPE_DOUBLE)
    trigger->priority = DB_GET_DOUBLE (&value);

  /* EVENT */
  if (db_get (object, TR_ATT_EVENT, &value))
    goto error;

  if (DB_VALUE_TYPE (&value) == DB_TYPE_INTEGER)
    trigger->event = (DB_TRIGGER_EVENT) DB_GET_INTEGER (&value);

  /* CLASS */
  if (db_get (object, TR_ATT_CLASS, &value))
    goto error;

  if (DB_VALUE_TYPE (&value) == DB_TYPE_OBJECT)
    {
      if (DB_IS_NULL (&value))
	{
	  trigger->class_mop = NULL;
	}
      else
	{
	  trigger->class_mop = DB_GET_OBJECT (&value);
	}
      /*
       * Check to make sure the class is still available.  It is possible
       * that the class can be deleted and it wasn't possible to inform
       * the associated trigger objects of that fact.
       */
      if (trigger->class_mop != NULL)
	{
	  if (au_fetch_class_force (trigger->class_mop, &class_,
				    AU_FETCH_READ) == ER_HEAP_UNKNOWN_OBJECT)
	    {
	      trigger->status = TR_STATUS_INVALID;
	    }
	}
    }

  /* ATTRIBUTE */
  if (db_get (object, TR_ATT_ATTRIBUTE, &value))
    goto error;

  if (DB_VALUE_TYPE (&value) == DB_TYPE_STRING && !DB_IS_NULL (&value))
    {
      tmp = DB_GET_STRING (&value);
      if (tmp)
	{
	  trigger->attribute = strdup (tmp);
	}
    }

  db_value_clear (&value);

  /* CLASS ATTRIBUTE */
  if (db_get (object, TR_ATT_CLASS_ATTRIBUTE, &value))
    goto error;

  if (DB_VALUE_TYPE (&value) == DB_TYPE_INTEGER)
    trigger->class_attribute = DB_GET_INTEGER (&value);

  /* CONDITION TYPE */
  if (db_get (object, TR_ATT_CONDITION_TYPE, &value))
    goto error;

  if (DB_VALUE_TYPE (&value) == DB_TYPE_INTEGER)
    {
      trigger->condition = make_activity ();
      if (trigger->condition == NULL)
	goto error;

      trigger->condition->type = (DB_TRIGGER_ACTION) DB_GET_INTEGER (&value);

      /* CONDITION TIME */
      if (db_get (object, TR_ATT_CONDITION_TIME, &value))
	goto error;

      if (DB_VALUE_TYPE (&value) == DB_TYPE_INTEGER)
	trigger->condition->time = (DB_TRIGGER_TIME) DB_GET_INTEGER (&value);

      /*  CONDITION SOURCE */
      if (db_get (object, TR_ATT_CONDITION, &value))
	goto error;

      if (DB_VALUE_TYPE (&value) == DB_TYPE_STRING && !DB_IS_NULL (&value))
	{
	  tmp = DB_GET_STRING (&value);
	  if (tmp)
	    {
	      trigger->condition->source = strdup (tmp);
	    }
	}
      db_value_clear (&value);
    }

  /* ACTION TYPE */
  if (db_get (object, TR_ATT_ACTION_TYPE, &value))
    goto error;

  if (DB_VALUE_TYPE (&value) == DB_TYPE_INTEGER)
    {
      trigger->action = make_activity ();
      if (trigger->action == NULL)
	goto error;

      trigger->action->type = (DB_TRIGGER_ACTION) DB_GET_INTEGER (&value);

      /* ACTION TIME */
      if (db_get (object, TR_ATT_ACTION_TIME, &value))
	goto error;

      if (DB_VALUE_TYPE (&value) == DB_TYPE_INTEGER)
	{
	  trigger->action->time = (DB_TRIGGER_TIME) DB_GET_INTEGER (&value);
	}

      /*  ACTION SOURCE */
      if (db_get (object, TR_ATT_ACTION, &value))
	{
	  /* hack, try old name if error */
	  if (db_get (object, TR_ATT_ACTION_OLD, &value))
	    goto error;
	}

      if (DB_VALUE_TYPE (&value) == DB_TYPE_STRING && !DB_IS_NULL (&value))
	{
	  tmp = DB_GET_STRING (&value);
	  if (tmp)
	    {
	      trigger->action->source = strdup (tmp);
	    }
	}
      db_value_clear (&value);
    }

  AU_ENABLE (save);
  return NO_ERROR;

error:
  AU_ENABLE (save);

  assert (er_errid () != NO_ERROR);
  return er_errid ();
}


/* EXPRESSION COMPILATION */

/*
 * get_reference_names() - This determines which of the various reference
 *                         names are valid for a particular trigger expression
 *    return: none
 *    trigger(in): trigger of interest
 *    activity(in): action
 *    curname(out): current name (returned)
 *    tempname(out): temp name (returned)
 *
 */
static void
get_reference_names (TR_TRIGGER * trigger, TR_ACTIVITY * activity,
		     const char **curname, const char **tempname)
{
  *curname = NULL;
  *tempname = NULL;

  if (!prm_get_bool_value (PRM_ID_MYSQL_TRIGGER_CORRELATION_NAMES))
    {
      switch (trigger->event)
	{
	case TR_EVENT_INSERT:
	  switch (activity->time)
	    {
	    case TR_TIME_BEFORE:
	      *tempname = NEW_REFERENCE_NAME;
	      break;
	    case TR_TIME_AFTER:
	    case TR_TIME_DEFERRED:
	      *curname = OBJ_REFERENCE_NAME;
	      break;
	    default:
	      break;
	    }
	  break;

	case TR_EVENT_UPDATE:
	  switch (activity->time)
	    {
	    case TR_TIME_BEFORE:
	      *curname = OBJ_REFERENCE_NAME;
	      *tempname = NEW_REFERENCE_NAME;
	      break;
	    case TR_TIME_AFTER:
	      *curname = OBJ_REFERENCE_NAME;
	      *tempname = OLD_REFERENCE_NAME;
	      break;
	    case TR_TIME_DEFERRED:
	      *curname = OBJ_REFERENCE_NAME;
	      break;
	    default:
	      break;
	    }
	  break;

	case TR_EVENT_DELETE:
	  switch (activity->time)
	    {
	    case TR_TIME_BEFORE:
	      *curname = OBJ_REFERENCE_NAME;
	      break;
	    case TR_TIME_AFTER:
	    case TR_TIME_DEFERRED:
	      break;
	    default:
	      break;
	    }
	  break;

	default:
	  break;
	}
    }
  else
    {
      switch (trigger->event)
	{
	case TR_EVENT_INSERT:
	  switch (activity->time)
	    {
	    case TR_TIME_BEFORE:
	      *tempname = NEW_REFERENCE_NAME;
	      break;
	    case TR_TIME_AFTER:
	    case TR_TIME_DEFERRED:
	      *curname = NEW_REFERENCE_NAME;
	      break;
	    default:
	      break;
	    }
	  break;

	case TR_EVENT_UPDATE:
	  switch (activity->time)
	    {
	    case TR_TIME_BEFORE:
	      *curname = OLD_REFERENCE_NAME;
	      *tempname = NEW_REFERENCE_NAME;
	      break;
	    case TR_TIME_AFTER:
	      *curname = NEW_REFERENCE_NAME;
	      *tempname = OLD_REFERENCE_NAME;
	      break;
	    case TR_TIME_DEFERRED:
	      *curname = NEW_REFERENCE_NAME;
	      break;
	    default:
	      break;
	    }
	  break;

	case TR_EVENT_DELETE:
	  switch (activity->time)
	    {
	    case TR_TIME_BEFORE:
	      *curname = OLD_REFERENCE_NAME;
	      break;
	    case TR_TIME_AFTER:
	    case TR_TIME_DEFERRED:
	      break;
	    default:
	      break;
	    }
	  break;

	default:
	  break;
	}
    }
}

/*
 * compile_trigger_activity() - This is used to compile the condition or
 *                              action expressions of a trigger.
 *    return: error code
 *    trigger(in): trigger being updated
 *    activity(in): activity to compile
 *    with_evaluate(in):
 *
 * Note:
 *    Normally this is done once and then left cached in the trigger structure
 *    It will be recompiled if the trigger object is changed in any way.
 *    This is detected by examining the cache coherency number for the
 *    trigger object.  When this changes the cache (including the parse trees)
 *    will be flushed and the parse trees will be generated again.
 *    Parse trees must also be generated after a trigger has been loaded
 *    from disk for the first time.
 */
static int
compile_trigger_activity (TR_TRIGGER * trigger, TR_ACTIVITY * activity,
			  int with_evaluate)
{
  int error = NO_ERROR;
  const char *curname, *tempname;
  DB_OBJECT *class_mop;
  char *text;
  int length;
  PT_NODE *err;
  int stmt, line, column;
  const char *msg;
  PT_NODE **node_ptr;

  if (activity != NULL && activity->type == TR_ACT_EXPRESSION
      && activity->source != NULL)
    {

      if (activity->parser != NULL)
	{
	  parser_free_parser ((PARSER_CONTEXT *) activity->parser);
	  activity->parser = NULL;
	  activity->statement = NULL;
	}

      /* build a string suitable for compilation */
      if (!with_evaluate)
	{
	  text = activity->source;
	}
      else
	{
	  length =
	    strlen (EVAL_PREFIX) + strlen (activity->source) +
	    strlen (EVAL_SUFFIX) + 1;
	  text = (char *) malloc (length);
	  if (text == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_OUT_OF_VIRTUAL_MEMORY, 1, length);
	      return er_errid ();
	    }
	  strcpy (text, EVAL_PREFIX);
	  strcat (text, activity->source);
	  strcat (text, EVAL_SUFFIX);
	}

      /* get a parser for this statement */
      activity->parser = parser_create_parser ();
      if (activity->parser == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  return er_errid ();
	}

      get_reference_names (trigger, activity, &curname, &tempname);

      /*
       * The pt_ interface doesn't like the first name to be NULL and the
       * second name to be non-NULL. For cases like BEFORE INSERT where
       * we have a temp object but no "real" object, shift the tempname
       * down to the curname.  Must remember to do the same thing
       * when the objects are passed to pt_exec_trigger_stmt()
       */

      if (curname == NULL)
	{
	  curname = tempname;
	  tempname = NULL;
	}

      /*
       * if both correlation names are NULL, don't pass in the class pointer
       * because it adds an empty FROM clause to the expression which
       * results in compile errors
       */
      class_mop = ((curname == NULL && tempname == NULL)
		   ? NULL : trigger->class_mop);

      activity->statement =
	pt_compile_trigger_stmt ((PARSER_CONTEXT *) activity->parser, text,
				 class_mop, curname, tempname);
      if (activity->statement == NULL
	  || pt_has_error ((PARSER_CONTEXT *) activity->parser))
	{
	  err = pt_get_errors ((PARSER_CONTEXT *) activity->parser);
	  if (err == NULL)
	    {
	      /* missing compiler error list */
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TR_INTERNAL_ERROR,
		      1, trigger->name);
	    }
	  else
	    {
	      error = ER_EMERGENCY_ERROR;
	      while (err != NULL)
		{
		  err = pt_get_next_error (err, &stmt, &line, &column, &msg);
		  er_set (ER_SYNTAX_ERROR_SEVERITY, ARG_FILE_LINE, error, 1,
			  msg);
		}

	      /* package up the last error into a general trigger error */
	      error =
		(with_evaluate) ? ER_TR_CONDITION_COMPILE :
		ER_TR_ACTION_COMPILE;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 2,
		      trigger->name, msg);
	    }

	  /*
	   * since we had a problem with the compilation, don't leave
	   * the parser hanging around in the trigger cache
	   */
	  if (activity->statement)
	    {
	      parser_free_tree ((PARSER_CONTEXT *) activity->parser,
				(PT_NODE *) activity->statement);
	    }
	  activity->statement = NULL;
	  parser_free_parser ((PARSER_CONTEXT *) activity->parser);
	  activity->parser = NULL;
	}

      if (activity->statement)
	{
	  /*
	   * We can't allow the user to have a before insert trigger that
	   * uses the OID of the new value.  i.e. they can reference
	   * "new.a" but not "new".  Let's walk the statement and look
	   * for this case.
	   */
	  if (trigger->event == TR_EVENT_INSERT
	      && activity->time == TR_TIME_BEFORE)
	    {
	      node_ptr = &((PT_NODE *) activity->statement)->info.scope.stmt;
	      *node_ptr =
		parser_walk_tree ((PARSER_CONTEXT *) activity->parser,
				  *node_ptr, NULL, NULL, tr_check_correlation,
				  NULL);
	      if (pt_has_error ((PARSER_CONTEXT *) activity->parser))
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_TR_CORRELATION_ERROR, 0);
		  error = er_errid ();
		  parser_free_tree ((PARSER_CONTEXT *) activity->parser,
				    (PT_NODE *) activity->statement);
		  activity->statement = NULL;
		  parser_free_parser ((PARSER_CONTEXT *) activity->parser);
		  activity->parser = NULL;
		}
	    }
	}

      /* free the computed string */
      if (with_evaluate)
	{
	  free_and_init (text);
	}
    }
  return (error);
}

/* TRIGGER STRUCTURE & OBJECT MAP */


/*
 * validate_trigger() - This is used to check the validity of a cached
 *                      trigger structure.
 *    return: error code
 *    trigger(in/out): trigger structure
 *
 * Note:
 *    This is used to check the validity of a cached trigger structure.
 *    It should be called once before any API level trigger operation
 *    takes place.
 *    Among other things, this must make sure the condition and action
 *    statements are compiled and ready to go.
 *    It also checks "quickly" to see if the associated trigger instance
 *    was modified since the last time the trigger was cached.
 *    This could be faster.
 */
static int
validate_trigger (TR_TRIGGER * trigger)
{
  DB_OBJECT *object_p;
  MOBJ obj;

  /*
   * should have a quicker lock check mechanism, could call
   * locator directly here if it speeds things up
   */

  if (au_fetch_instance_force (trigger->object, &obj, AU_FETCH_READ))
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  if (trigger->chn != WS_CHN (obj))
    {
      /* cache coherency numbers have changed, recache */
      object_p = trigger->object;	/* have to save this */
      tr_clear_trigger (trigger);

      if (object_to_trigger (object_p, trigger))
	{
	  assert (er_errid () != NO_ERROR);
	  return er_errid ();
	}

      if (compile_trigger_activity (trigger, trigger->condition, 1))
	{
	  trigger->status = TR_STATUS_INVALID;
	}

      if (compile_trigger_activity (trigger, trigger->action, 0))
	{
	  trigger->status = TR_STATUS_INVALID;
	}

    }
  return (NO_ERROR);
}

/*
 * tr_map_trigger() - This creates a trigger cache strcuture for
 *                    a trigger instance and optionally validates the cache.
 *    return: trigger structure
 *    object(in): trigger object handle
 *    fetch(in): non-zero if the cache is to be updated
 *
 * Note:
 *    This creates a trigger cache strcuture for a trigger instance and
 *    optionally validates the cache.
 *    This is used whenever a trigger object handle needs to be mapped
 *    into a trigger structure.  The structure will be created if one
 *    has not yet been allocated.
 *    This is called by the schema manager when a class is loaded in order
 *    to build the class trigger cache list.  In this case, the fetch
 *    flag is off so we don't cause recursive fetches during the
 *    transformation of the class object.
 *    It is also called by the trigger API functions which take MOPs
 *    as arguments but which need to convert these to the run-time
 *    trigger structures.  In these cases, the fetch flag is set because
 *    the trigger will need to be immediately validated.
 *
 */
TR_TRIGGER *
tr_map_trigger (DB_OBJECT * object, int fetch)
{
  TR_TRIGGER *trigger = NULL;

  trigger = (TR_TRIGGER *) mht_get (tr_object_map, (void *) object);
  if (trigger != NULL)
    {
      if (fetch && validate_trigger (trigger) != NO_ERROR)
	trigger = NULL;
    }
  else
    {
      trigger = tr_make_trigger ();
      if (trigger != NULL)
	{
	  if (object_to_trigger (object, trigger) != NO_ERROR)
	    {
	      free_trigger (trigger);
	      trigger = NULL;
	    }
	  else
	    {
	      if (mht_put (tr_object_map, object, trigger) == NULL)
		{
		  free_trigger (trigger);
		  trigger = NULL;
		}
	    }
	}
    }
  return (trigger);
}

/*
 * tr_unmap_trigger() - This is used to release a reference to
 *                      a trigger structure.
 *    return: error code
 *    trigger(in): trigger structure
 *
 * Note:
 *    This is used to release a reference to a trigger structure.
 *    The trigger can be removed from the mapping table and freed.
 *    This MUST ONLY be called if the caller knows that there will be no other
 *    references to this trigger structure.
 *    This is the case for most class triggers.
 *    If this cannot be guaranteed, the trigger can simply be left in
 *    the mapping table and it will be freed during shutdown.
 *
 */
int
tr_unmap_trigger (TR_TRIGGER * trigger)
{
  int error = NO_ERROR;

  /*
   * better make damn sure that the caller is the only one
   * that could be referencing this trigger structure
   */

  if (mht_rem (tr_object_map, trigger->object, NULL, NULL) != NO_ERROR)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
    }

  return (error);
}

/* USER TRIGGER CACHE */

/*
 * The user object has an attribute that may contain a sequence
 * of trigger object pointers.  These were once in the au_ module but
 * since they don't really need any special support there, they were
 * moved here to keep au_ from having to know too much about triggers.
 * The triggers in this list are only the "user" level triggers such
 * as COMMIT, ABORT, TIMEOUT, and ROLLBACK.  These triggers
 * are extracted from the user object and placed in a global trigger cache
 * list.  This saves the expense of performing the usual db_get()
 * operations to extract the trigger info every time a user event
 * happens.  Proper maintenance of the cache depends on the
 * function tr_check_rollback_triggers being called inside
 * the tran_abort() function.  When the transaction is rolled back,
 * we universally invalidate the user cache so that it can be recalculated
 * for the next transaction.  We only really need to do this if
 * the user object was modified during the transaction.
 *
 */

/*
 * register_user_trigger() - This stores a new trigger object inside
 *                           the current user object
 *    return: error code
 *    object(in): trigger object
 *
 * Note:
 *    If successful, it will recalculate the user trigger cache list as well.
 */
static int
register_user_trigger (DB_OBJECT * object)
{
  int error = NO_ERROR;
  DB_SET *table;
  DB_VALUE value;
  int save;

  AU_DISABLE (save);
  if (Au_user != NULL
      && (error = obj_lock (Au_user, 1)) == NO_ERROR
      && (error = obj_get (Au_user, "triggers", &value)) == NO_ERROR)
    {
      if (DB_IS_NULL (&value))
	{
	  table = NULL;
	}
      else
	{
	  table = DB_GET_SET (&value);
	}

      if (table == NULL)
	{
	  table = set_create_sequence (0);
	  db_make_sequence (&value, table);
	  obj_set (Au_user, "triggers", &value);
	  /*
	   * remember, because of coercion, we have to either set the
	   * domain properly to begin with or we have to get the
	   * coerced set back out after it has been assigned
	   */
	  set_free (table);
	  obj_get (Au_user, "triggers", &value);
	  if (DB_IS_NULL (&value))
	    {
	      table = NULL;
	    }
	  else
	    {
	      table = DB_GET_SET (&value);
	    }
	}
      db_make_object (&value, object);
      error = set_insert_element (table, 0, &value);
      /* if an error is set, probably must abort the transaction */
    }
  AU_ENABLE (save);

  if (!error)
    {
      tr_User_triggers_modified = 1;
      tr_update_user_cache ();
    }

  return (error);
}

/*
 * unregister_user_trigger() - This removes a trigger from the user object and
 *                             associated caches
 *    return: error code
 *    trigger(in): trigger structure
 *    rollback(in): non-zero if we're performing a rollback
 *
 * Note:
 *    This removes a trigger from the user object and associated caches.
 *    If the rollback flag is set, it indicates that we're trying to
 *    remove a trigger that was defined during the current transaction.
 *    In that case, try to be more tolerant of errors that may occurr.
 */
static int
unregister_user_trigger (TR_TRIGGER * trigger, int rollback)
{
  int error = NO_ERROR;
  DB_SET *table;
  DB_VALUE value;
  int save;

  if (rollback)
    {
      /*
       * Carefully remove it from the user cache first, if we have
       * errors touching the user object then at least it will be
       * out of the cache
       */
      (void) remove_trigger_list (&tr_User_triggers, trigger);
    }

  AU_DISABLE (save);
  if (Au_user != NULL
      && (error = obj_lock (Au_user, 1)) == NO_ERROR
      && (error = obj_get (Au_user, "triggers", &value)) == NO_ERROR)
    {
      if (DB_IS_NULL (&value))
	{
	  table = NULL;
	}
      else
	{
	  table = DB_GET_SET (&value);
	}
      if (table != NULL)
	{
	  db_make_object (&value, trigger->object);
	  error = set_drop_element (table, &value, false);
	  set_free (table);
	}
      /* else, should have "trigger not found" error ? */
    }
  AU_ENABLE (save);

  /* don't bother updating the cache now if its a rollback */
  if (!error && !rollback)
    {
      tr_User_triggers_modified = 1;
      tr_update_user_cache ();
    }

  return (error);
}

/*
 * get_user_trigger_objects() - This is used to build and return a list of
 *                              all the user triggers currently defined
 *    return: error code
 *    event(in): event type
 *    active_filter(in): non-zero to filter out inactive triggers
 *    trigger_list(in/out): returned list of trigger obects
 *
 */
static int
get_user_trigger_objects (DB_TRIGGER_EVENT event, bool active_filter,
			  DB_OBJLIST ** trigger_list)
{
  int error = NO_ERROR;
  DB_SET *table;
  DB_VALUE value;
  DB_TRIGGER_EVENT e;
  TR_TRIGGER *trigger;
  int max, i;

  *trigger_list = NULL;

  if (Au_user != NULL
      && (error = obj_get (Au_user, "triggers", &value)) == NO_ERROR)
    {
      if (DB_IS_NULL (&value))
	{
	  table = NULL;
	}
      else
	{
	  table = DB_GET_SET (&value);
	}
      if (table != NULL)
	{
	  error = set_filter (table);
	  max = set_size (table);
	  for (i = 0; i < max && error == NO_ERROR; i++)
	    {
	      if ((error = set_get_element (table, i, &value)) == NO_ERROR)
		{
		  if (DB_VALUE_TYPE (&value) == DB_TYPE_OBJECT
		      && !DB_IS_NULL (&value)
		      && DB_GET_OBJECT (&value) != NULL)
		    {
		      /* deleted objects should have been filtered by now */
		      trigger = tr_map_trigger (DB_GET_OBJECT (&value), 1);
		      if (trigger == NULL)
			{
			  assert (er_errid () != NO_ERROR);
			  error = er_errid ();
			}
		      else
			{
			  if (!active_filter
			      || trigger->status == TR_STATUS_ACTIVE)
			    {
			      if (event == TR_EVENT_NULL)
				{
				  /* unconditionally collect all the trigger
				   * objects
				   */
				  error =
				    ml_ext_add (trigger_list,
						DB_GET_OBJECT (&value), NULL);
				}
			      else
				{
				  /* must check for a specific event */
				  if ((error =
				       tr_trigger_event (DB_GET_OBJECT
							 (&value),
							 &e)) == NO_ERROR)
				    {
				      if (e == event)
					{
					  error =
					    ml_ext_add (trigger_list,
							DB_GET_OBJECT
							(&value), NULL);
					}
				    }
				}
			    }
			}
		    }
		}
	    }
	  set_free (table);
	}
    }
  if (error != NO_ERROR && *trigger_list != NULL)
    {
      ml_ext_free (*trigger_list);
      *trigger_list = NULL;
    }
  return (error);
}

/*
 * tr_update_user_cache() - This is used to establish the user trigger cache
 *    return: error
 *
 * Note:
 *    It goes through the trigger objects defined for the user, maps
 *    them to trigger objects and stores them on specific lists for
 *    quick access.
 */
int
tr_update_user_cache (void)
{
  int error = NO_ERROR;
  DB_SET *table;
  DB_VALUE value;
  TR_TRIGGER *trigger;
  int max, i;

  tr_User_triggers_valid = 0;
  if (tr_User_triggers != NULL)
    {
      tr_free_trigger_list (tr_User_triggers);
      tr_User_triggers = NULL;
    }

  if (Au_user != NULL
      && (error = obj_get (Au_user, "triggers", &value)) == NO_ERROR)
    {
      if (DB_IS_NULL (&value))
	{
	  table = NULL;
	}
      else
	{
	  table = DB_GET_SET (&value);
	}

      if (table != NULL)
	{
	  error = set_filter (table);
	  max = set_size (table);
	  for (i = 0; i < max && error == NO_ERROR; i++)
	    {
	      if ((error = set_get_element (table, i, &value)) == NO_ERROR)
		{
		  if (DB_VALUE_TYPE (&value) == DB_TYPE_OBJECT
		      && !DB_IS_NULL (&value)
		      && DB_GET_OBJECT (&value) != NULL)
		    {
		      /* deleted objects will have been filtered by now */
		      trigger = tr_map_trigger (DB_GET_OBJECT (&value), 1);
		      if (trigger == NULL)
			{
			  assert (er_errid () != NO_ERROR);
			  error = er_errid ();
			}
		      else
			{
			  error =
			    insert_trigger_list (&tr_User_triggers, trigger);
			}
		    }
		}
	    }
	  set_free (table);
	}
    }

  if (!error)
    {
      tr_User_triggers_valid = 1;
    }
  else if (tr_User_triggers != NULL)
    {
      tr_free_trigger_list (tr_User_triggers);
      tr_User_triggers = NULL;
    }

  return (error);
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * tr_invalidate_user_cache() - This is called to invalidate the user trigger
 *                              cache and cause it to be recalculated the next
 *                              time a user event is encountered.
 *    return: none
 *
 * Note:
 *    It is intended to be called only by au_set_user() when the user
 *    object has been changed.  Since this can happen frequently under
 *    some conditions, don't do much complicated here.  Delay the actual
 *    cache calulation until it is needed.
 */
void
tr_invalidate_user_cache (void)
{
  tr_User_triggers_valid = 0;
}
#endif /* ENABLE_UNUSED_FUNCTION */

/* SCHEMA CACHE */

/*
 * Pointers to trigger cache structures are placed directly in the
 * schema structures so that the triggers for a particular class/attribute
 * can be quickly located.
 *
 */

/*
 * tr_make_schema_cache() - This creates a schema class structure
 *    return: schema cache structure
 *    type(in): type of cache to create
 *    objects(in): initial list of schema objects
 *
 * Note:
 *    This creates a schema class structure.  The length of the cache
 *    array will vary depending on the type of the cache.
 *    Since these are going to be attached to class objects and may not
 *    get freed unless the class is specifically flushed, we allocate them
 *    in the workspace rather than using malloc so we don't get
 *    a bunch of dangling allocation references when we shut down.
 *    Since we use WS_ALLOC, we have to be careful about garbage collecting
 *    the containined MOPs.  However, since these MOPs are going to
 *    be in the global trigger object map table it doesn't really matter.
 */
TR_SCHEMA_CACHE *
tr_make_schema_cache (TR_CACHE_TYPE type, DB_OBJLIST * objects)
{
  TR_SCHEMA_CACHE *cache = NULL;
  int elements, size, i;

  if (type == TR_CACHE_CLASS)
    {
      elements = TR_MAX_CLASS_TRIGGERS;
    }
  else
    {
      elements = TR_MAX_ATTRIBUTE_TRIGGERS;
    }

  /*
   * there is already space for one ponter in the structure
   * so decrement the element count multiplier
   */
  size = sizeof (TR_SCHEMA_CACHE) + (sizeof (TR_TRIGLIST *) * (elements - 1));

  cache = (TR_SCHEMA_CACHE *) db_ws_alloc (size);
  if (cache != NULL)
    {
      cache->objects = objects;
      cache->compiled = 0;
      cache->array_length = elements;
      for (i = 0; i < elements; i++)
	{
	  cache->triggers[i] = NULL;
	}

      /* add it to the global list */
      cache->next = tr_Schema_caches;
      tr_Schema_caches = cache;
    }

  return cache;
}

/*
 * tr_copy_schema_cache() - This is called by the schema manager during class
 *                          flattening
 *    return: new cache
 *    cache(in): cache to copy
 *    filter_class(in): non-null if filtering triggers
 *
 * Note:
 *    It creates a copy of an existing schema cache.  Don't bother
 *    compiling it at this time, just copy the trigger object list
 *    and let it get compiled the next time the class is referenced.
 *    If the filter_class is non-NULL, the copied cache will contain
 *    only triggers that are defined with targets directly on the
 *    filter_class.  This is used to filter out triggers in the
 *    cache that were inherited from super classes.
 */
TR_SCHEMA_CACHE *
tr_copy_schema_cache (TR_SCHEMA_CACHE * cache, MOP filter_class)
{
  TR_SCHEMA_CACHE *new_;
  TR_TRIGGER *trigger;
  TR_CACHE_TYPE type;
  DB_OBJLIST *obj_list;

  new_ = NULL;
  if (cache != NULL)
    {
      type = (cache->array_length == TR_MAX_CLASS_TRIGGERS) ?
	TR_CACHE_CLASS : TR_CACHE_ATTRIBUTE;

      new_ = tr_make_schema_cache (type, NULL);
      if (new_ != NULL)
	{
	  if (cache->objects != NULL)
	    {
	      if (filter_class == NULL)
		{
		  new_->objects = ml_copy (cache->objects);
		  if (new_->objects == NULL)
		    goto abort_it;
		}
	      else
		{
		  for (obj_list = cache->objects; obj_list != NULL;
		       obj_list = obj_list->next)
		    {
		      trigger = tr_map_trigger (obj_list->op, 1);
		      if (trigger == NULL)
			goto abort_it;

		      if (trigger->class_mop == filter_class)
			{
			  if (ml_add (&new_->objects, obj_list->op, NULL))
			    goto abort_it;
			}
		    }
		}
	    }
	}
    }
  return new_;

abort_it:
  if (new_ != NULL)
    {
      tr_free_schema_cache (new_);
    }
  return NULL;
}

/*
 * tr_merge_schema_cache() - This is a support routine called by the schema
 *                           manager during class flattening.
 *    return: error code
 *    dest(in): destination cache
 *    src(in): source cache
 *
 * Note:
 *    It will merge the contents of one trigger cache with another.
 */
int
tr_merge_schema_cache (TR_SCHEMA_CACHE * destination,
		       TR_SCHEMA_CACHE * source)
{
  int error = NO_ERROR;
  DB_OBJLIST *obj_list;

  if (destination != NULL && source != NULL)
    {
      for (obj_list = source->objects; obj_list != NULL && !error;
	   obj_list = obj_list->next)
	error = ml_add (&destination->objects, obj_list->op, NULL);
    }
  return error;
}

/*
 * tr_empty_schema_cache() - Looks to see if a cache is empty.
 *    return: non-zero if the schema cache is empty
 *    cache(in):
 *
 */
int
tr_empty_schema_cache (TR_SCHEMA_CACHE * cache)
{
  return ((cache == NULL) || (cache->objects == NULL));
}

/*
 * tr_free_schema_cache() - This is called to free a schema cache.
 *    return: none
 *    cache(in): cache structure
 *
 * Note:
 *    Normally this is done only when a class is deleted or swapped out of
 *    the workspace.
 */
void
tr_free_schema_cache (TR_SCHEMA_CACHE * cache)
{
  TR_SCHEMA_CACHE *c, *prev;
  int i;

  if (cache != NULL)
    {
      for (i = 0; i < cache->array_length; i++)
	{
	  if (cache->triggers[i])
	    {
	      tr_free_trigger_list (cache->triggers[i]);
	    }
	}

      if (cache->objects != NULL)
	{
	  ml_free (cache->objects);
	}

      /* unlink it from the global list */
      for (c = tr_Schema_caches, prev = NULL; c != NULL && c != cache;
	   c = c->next)
	{
	  prev = c;
	}
      if (c == cache)
	{
	  if (prev != NULL)
	    {
	      prev->next = c->next;
	    }
	  else
	    {
	      tr_Schema_caches = c->next;
	    }
	}
      db_ws_free (cache);
    }
}

/*
 * tr_add_cache_trigger() - This is a callback function called by the schema
 *                          manager during the processing of an sm_add_trigger
 *                          request
 *    return: error code
 *    cache(in/out): cache pointer
 *    trigger_object(in): trigger (object) to add
 *
 * Note:
 *    As the schema manager walks through the class hierarchy identifying
 *    subclasses that need to inherit the trigger, it will create
 *    caches using tr_make_schema_cache and then call tr_add_cache_trigger
 *    to add the trigger object.
 *    If the cache has already been compiled, we map the trigger and
 *    add it to the appropriate list.  If it hasn't yet been compiled
 *    we simply add it to the object list.
 */
int
tr_add_cache_trigger (TR_SCHEMA_CACHE * cache, DB_OBJECT * trigger_object)
{
  int error = NO_ERROR;
  TR_TRIGGER *trigger;

  if (cache != NULL)
    {
      error = ml_add (&cache->objects, trigger_object, NULL);
      if (!error)
	{
	  if (cache->compiled)
	    {
	      trigger = tr_map_trigger (trigger_object, 1);
	      if (trigger == NULL)
		{
		  assert (er_errid () != NO_ERROR);
		  error = er_errid ();
		}
	      else
		{
		  error =
		    insert_trigger_list (&(cache->triggers[trigger->event]),
					 trigger);
		}
	    }
	}
    }
  return error;
}

/*
 * tr_drop_cache_trigger() - This is a callback function called by the schema
 *                           manager during the processing of
 *                           an sm_drop_trigger request.
 *    return: error code
 *    cache(in): schema cache
 *    trigger_object(in): trigger object to remove
 *
 * Note:
 *    As the schema
 *    manager walks the class hierarchy, it will call this function
 *    to remove a particular trigger object from the cache of each
 *    of the affected subclasses.  Compare with the function
 *    tr_add_cache_trigger.
 */
int
tr_drop_cache_trigger (TR_SCHEMA_CACHE * cache, DB_OBJECT * trigger_object)
{
  int error = NO_ERROR;
  TR_TRIGGER *trigger;

  if (cache != NULL)
    {
      ml_remove (&cache->objects, trigger_object);
      if (cache->compiled)
	{
	  trigger = tr_map_trigger (trigger_object, 1);
	  if (trigger == NULL)
	    {
	      assert (er_errid () != NO_ERROR);
	      error = er_errid ();
	    }
	  else
	    {
	      (void) remove_trigger_list (&(cache->triggers[trigger->event]),
					  trigger);
	    }
	}
    }
  return error;
}

/*
 * tr_gc_schema_cache() - This is called by the schema manager gc markers
 *                        to walk the trigger caches
 *    return: none
 *    cache(in): cache structure
 *    gcmarker(in): marker function
 *
 * Note:
 *    Since we currently keep triggers in the global mapping table which
 *    is also in non-workspace memory, we don't really have to do anything
 *    here.  If this should change, we need to mark all the MOPs
 *    in the cache's object list.
 *    Also the trigger structures themselves will also contain a pointer
 *    to their MOPs and these are allocated with malloc so they will
 *    already be marked.
 */
void
tr_gc_schema_cache (TR_SCHEMA_CACHE * cache, void (*gcmarker) (MOP))
{
}

/*
 * tr_get_cache_objects() - This builds an object list for all of the triggers
 *                          found in a schema cache.
 *    return: error code
 *    cache(in): schema cache
 *    list(out): returned object list
 *
 * Note:
 *    This is used for storing classes on disk.  Since
 *    we don't need the full trigger cache structure on disk, we just
 *    store a list of all the associated trigger objects.  When the class
 *    is brought back in, we convert the object list back into a trigger
 *    cache.
 *    The list returned by this function will become part of the cache
 *    if it doesn't already exist and should NOT be freed by the caller.
 *    There shouldn't be any conditions where the list doesn't match
 *    the cache but build it by hand just in case.
 */
int
tr_get_cache_objects (TR_SCHEMA_CACHE * cache, DB_OBJLIST ** list)
{
  int error = NO_ERROR;
  TR_TRIGLIST *t;
  int i;

  if (cache == NULL)
    {
      *list = NULL;
    }
  else
    {
      if (cache->objects == NULL)
	{
	  for (i = 0; i < cache->array_length && !error; i++)
	    {
	      for (t = cache->triggers[i]; t != NULL; t = t->next)
		error = ml_add (&cache->objects, t->trigger->object, NULL);
	    }
	}
      *list = cache->objects;
    }
  return (error);
}

/*
 * tr_validate_schema_cache() - This must be called by anyone that is about to
 *                              "look inside" a class cache structure.
 *    return: error
 *    cache(in): class cache attached to schema
 *
 * Note:
 *    This must be called by anyone that is about to "look inside"
 *    a class cache structure.  It makes sure that the cache objects
 *    have been converted to trigger structures and sorted on the
 *    proper list.
 */
int
tr_validate_schema_cache (TR_SCHEMA_CACHE * cache)
{
  int error = NO_ERROR;
  DB_OBJLIST *object_list, *prev, *next;
  TR_TRIGGER *trigger;

  if (cache != NULL)
    {
      if (!cache->compiled)
	{
	  for (object_list = cache->objects, prev = NULL, next = NULL;
	       object_list != NULL && cache != NULL; object_list = next)
	    {
	      next = object_list->next;
	      trigger = tr_map_trigger (object_list->op, 1);

	      /* check for deleted objects that need to be quietly removed */
	      if (trigger != NULL && trigger->event < cache->array_length)
		{
		  if (insert_trigger_list
		      (&(cache->triggers[trigger->event]), trigger))
		    {
		      assert (er_errid () != NO_ERROR);
		      return er_errid ();	/* memory error */
		    }
		  prev = object_list;
		}
	      else
		{
		  if (trigger == NULL
		      && er_errid () != ER_HEAP_UNKNOWN_OBJECT)
		    {
		      /* we got some kind of severe error, abort */
		      assert (er_errid () != NO_ERROR);
		      error = er_errid ();
		    }
		  else
		    {
		      /*
		       * else, got a bogus trigger object in the cache,
		       * remove it
		       */
		      if (prev == NULL)
			{
			  cache->objects = next;
			}
		      else
			{
			  prev->next = next;
			}
		      object_list->next = NULL;
		      ml_free (object_list);
		    }
		}
	    }
	  if (!error)
	    {
	      cache->compiled = 1;
	    }
	}
    }
  return (error);
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * tr_reset_schema_cache() - This is called by functions that alter triggers
 *                           in such a way that the schema caches need
 *                           to be rebuilt
 *    return: error
 *    cache(in): cache to clear
 *
 * Note:
 *    Currently this is called only by tr_set_priority.
 */

int
tr_reset_schema_cache (TR_SCHEMA_CACHE * cache)
{
  int error = NO_ERROR;
  int i;

  if (cache != NULL)
    {
      /* remove the existing trigger lists if any */
      for (i = 0; i < cache->array_length; i++)
	{
	  if (cache->triggers[i])
	    {
	      tr_free_trigger_list (cache->triggers[i]);
	      cache->triggers[i] = NULL;
	    }
	}
      cache->compiled = 0;
      error = tr_validate_schema_cache (cache);
    }
  return (error);
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * reorder_schema_caches() - This finds all the schema caches that point to
 *                           a particular trigger and reorders their lists
 *                           based on the new trigger priority
 *    return: none
 *    trigger(in/out): trigger that has been modified
 *
 * Note:
 *    This could be more effecient if we had a more directed way
 *    to determine which caches a trigger might be on.  For now just look
 *    at all of them.  Changing trigger priorities shouldn't be a very
 *    common operation so it isn't worth walking class hierarchies at
 *    this point.
 */
static void
reorder_schema_caches (TR_TRIGGER * trigger)
{
  TR_SCHEMA_CACHE *c;
  int i;

  for (c = tr_Schema_caches; c != NULL; c = c->next)
    {
      if (c->compiled)
	{
	  for (i = 0; i < c->array_length; i++)
	    {
	      reinsert_trigger_list (&(c->triggers[i]), trigger);
	    }
	}
    }
}

/*
 * tr_active_schema_cache() - This is used to determine if a cache contains
 *                            active event_type triggers.
 *    return: 0 = none, >0 = active, <0 = error
 *    cache(in): schema cache to examine
 *    event_type(in) : event type.
 *
 * Note:
 *     The schema manager calls this to cache the trigger
 *    activity status so that it can be faster in cases where there
 *    are no active triggers defined for a class.
 *    Returns -1 on error, this can only happen if there are storage
 *    allocation problems trying to build the schema cache.
 *
 */
int
tr_active_schema_cache (TR_SCHEMA_CACHE * cache, DB_TRIGGER_EVENT event_type,
			bool * has_event_type_triggers)
{
  TR_TRIGLIST *t;
  int i, active;
  bool result = false;

  active = 0;
  if (cache != NULL)
    {
      if (tr_validate_schema_cache (cache))
	{
	  return -1;
	}
      for (i = 0; i < cache->array_length && !result; i++)
	{
	  for (t = cache->triggers[i]; t != NULL && !result; t = t->next)
	    {
	      if (t->trigger->status == TR_STATUS_ACTIVE)
		{
		  active = 1;
		  if ((event_type == TR_EVENT_ALL)
		      || (t->trigger->event == event_type))
		    {
		      result = true;
		    }
		}
	    }
	}
    }

  if (has_event_type_triggers)
    {
      *has_event_type_triggers = result;
    }

  return active;
}

/*
 * tr_delete_schema_cache() - This is called by the schema manager to notify
 *                            the trigger manager that a class or attribute is
 *                            being deleted and the schema cache is no longer
 *                            needed
 *    return: error code
 *    cache(in): schema cache
 *    class_object(in): class_object
 *
 * Note:
 *    This is different than tr_free_schema_cache
 *    because we can also mark the associated triggers as being invalid
 *    since their targets are gone.  Note that marking the triggers
 *    invalid is only performed if the trigger target class is the same
 *    as the supplied class.  This is because this function may be
 *    called by subclasses that are losing the attribute but the attribute
 *    still exists in a super class and the trigger is still applicable
 *    to the super class attribute.
 */
int
tr_delete_schema_cache (TR_SCHEMA_CACHE * cache, DB_OBJECT * class_object)
{
  DB_VALUE value;
  DB_OBJLIST *m;
  TR_TRIGGER *trigger;
  int save;

  AU_DISABLE (save);

  /* make a value container for marking the trigger object as invalid */
  db_make_int (&value, (int) TR_STATUS_INVALID);
  if (cache != NULL)
    {
      for (m = cache->objects; m != NULL; m = m->next)
	{
	  trigger = tr_map_trigger (m->op, 0);

	  /*
	   * if the trigger has already been marked invalid, don't
	   * bother updating the object
	   */
	  if (trigger != NULL)
	    {
	      /*
	       * only invalidate the trigger if it is defined directly
	       * on this class
	       */
	      if (trigger->class_mop == class_object)
		{
		  /*
		   * don't bother updating the trigger object if its already
		   * invalid
		   */
		  if (trigger->status != TR_STATUS_INVALID)
		    {
		      trigger->status = TR_STATUS_INVALID;

		      /*
		       * Store the status permanently in the trigger object.
		       * This may result in an access violation, if so ignore
		       * it but be prepared to recognize it when the trigger
		       * is loaded again.
		       */
		      (void) db_put_internal (m->op, TR_ATT_STATUS, &value);
		    }
		}
	    }
	}

      tr_free_schema_cache (cache);
    }
  AU_ENABLE (save);
  return NO_ERROR;
}


/*
 * tr_delete_triggers_for_class - Finds all triggers on a class and deletes
 *                                them one by one.
 *                                WARNING: it really deletes them, not the wimpy
 *                                delete_schema_cache() which only invalidates them.
 *
 *
 *
 *    return: error code
 *    cache(in): schema cache
 *    class_object(in): class_object
 *
 * Note:
 *    This removes only the triggers that have the given class as
 *    the trigger object, and not attributes, or super- or subclasses.
 *    The idea is that if the user deletes a class, it is reasonable
 *    to assume that he does not want its triggers to remain around.
 */
int
tr_delete_triggers_for_class (TR_SCHEMA_CACHE * cache,
			      DB_OBJECT * class_object)
{
  TR_TRIGGER *trigger;
  int save;
  DB_OBJLIST *m;
  int didwork = 1;
  int error;

  if (NULL == cache)
    {
      return (NO_ERROR);
    }

  AU_DISABLE (save);

  while (didwork)
    {
      didwork = 0;

      /* need better error handling here */
      if ((error = tr_validate_schema_cache (cache)) != NO_ERROR)
	{
	  break;
	}
      for (m = cache->objects; m != NULL; m = m->next)
	{
	  trigger = tr_map_trigger (m->op, 0);
	  if (trigger == NULL)
	    {
	      continue;
	    }
	  if (trigger->class_mop != class_object)
	    {
	      continue;
	    }

	  tr_drop_trigger_internal (trigger, 0);
	  didwork = 1;

	  /* we can only delete one trigger per operation, because we need
	   * to re-validate the schema cache after each delete, so that we
	   * can iterate through the rest of its elements.
	   */
	  break;
	}
    }


  tr_free_schema_cache (cache);

  AU_ENABLE (save);
  return NO_ERROR;
}



/* TRIGGER TABLE */

/*
 * This is the global trigger table.  All triggers are entered into
 * the table and must be uniquely identifiable by name.
 * This should be one of the new "transaction aware" hash tables on
 * the server but until that facility is available we have to keep
 * a global list.  The disadvantate with this approach is that
 * there will be more contention.
 */

/*
 * trigger_table_add() - Adds a trigger to the global name table
 *    return: error code
 *    name(in): trigger name
 *    trigger(in): trigger object
 *
 * Note:
 *    We can assume here that a trigger with this name does not exist
 */
static int
trigger_table_add (const char *name, DB_OBJECT * trigger)
{
  int error = NO_ERROR;
  DB_SET *table = NULL;
  DB_VALUE value;
  int max, save;

  AU_DISABLE (save);

  if (Au_root != NULL
      && (error = obj_lock (Au_root, 1)) == NO_ERROR
      && (error = obj_get (Au_root, "triggers", &value)) == NO_ERROR)
    {
      if (DB_IS_NULL (&value))
	{
	  table = NULL;
	}
      else
	{
	  table = DB_GET_SET (&value);
	}
      if (table == NULL)
	{
	  table = set_create_sequence (0);
	  if (table == NULL)
	    {
	      error = er_errid ();
	      if (error == NO_ERROR)
		{
		  error = ER_GENERIC_ERROR;
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
		}

	      goto end;
	    }

	  error = db_make_sequence (&value, table);
	  if (error != NO_ERROR)
	    {
	      goto end;
	    }

	  error = obj_set (Au_root, "triggers", &value);
	  if (error != NO_ERROR)
	    {
	      goto end;
	    }
	  /*
	   * remember, because of coercion, we have to either set the
	   * domain properly to begin with or we have to get the
	   * coerced set back out after it has been assigned
	   */
	  set_free (table);
	  table = NULL;

	  error = obj_get (Au_root, "triggers", &value);
	  if (error != NO_ERROR)
	    {
	      goto end;
	    }

	  if (DB_IS_NULL (&value))
	    {
	      table = NULL;

	      goto end;
	    }
	  else
	    {
	      table = DB_GET_SET (&value);
	    }
	}
      max = set_size (table);

      db_make_string (&value, name);
      if ((error = set_put_element (table, max, &value)) == NO_ERROR)
	{
	  db_make_object (&value, trigger);
	  error = set_put_element (table, max + 1, &value);
	  /*
	   * if we have an error at this point, we probably should abort the
	   * transaction, we now have a partial update of the trigger
	   * association list
	   */
	}
      set_free (table);
      table = NULL;
    }

end:

  if (table != NULL)
    {
      set_free (table);
      table = NULL;
    }

  AU_ENABLE (save);

  return error;
}

/*
 * trigger_table_find() - This finds a trigger object by name
 *    return: error code
 *    name(in): trigger name
 *    trigger_ptr(out):  trigger object (returned)
 *
 * Note:
 *    All triggers must have a globally unique name.
 *    Should be modified to use an actual persistent table rather than
 *    the temporary in-memory table.
 */
static int
trigger_table_find (const char *name, DB_OBJECT ** trigger_p)
{
  int error = NO_ERROR;
  DB_SET *table;
  DB_VALUE value;
  int max, i, found;

  *trigger_p = NULL;
  if (Au_root != NULL
      && (error = obj_get (Au_root, "triggers", &value)) == NO_ERROR)
    {
      if (DB_IS_NULL (&value))
	{
	  table = NULL;
	}
      else
	{
	  table = DB_GET_SET (&value);
	}
      if (table != NULL)
	{
	  error = set_filter (table);
	  max = set_size (table);
	  /* see if the name is already used */
	  for (i = 0, found = -1; i < max && error == NO_ERROR && found == -1;
	       i += 2)
	    {
	      error = set_get_element (table, i, &value);
	      if (error == NO_ERROR)
		{
		  if (DB_VALUE_TYPE (&value) == DB_TYPE_STRING
		      && !DB_IS_NULL (&value)
		      && DB_GET_STRING (&value) != NULL
		      && COMPARE_TRIGGER_NAMES (DB_PULL_STRING (&value),
						name) == 0)
		    {
		      found = i;
		    }
		  pr_clear_value (&value);
		}
	    }
	  if (found != -1)
	    {
	      error = set_get_element (table, found + 1, &value);
	      if (error == NO_ERROR)
		{
		  if (DB_VALUE_TYPE (&value) == DB_TYPE_OBJECT)
		    {
		      if (DB_IS_NULL (&value))
			{
			  *trigger_p = NULL;
			}
		      else
			{
			  *trigger_p = DB_GET_OBJECT (&value);
			}
		    }
		  pr_clear_value (&value);
		}
	    }
	  set_free (table);
	}
    }

  return error;
}

/*
 * trigger_table_rename() - This is called when a trigger is renamed
 *    return: error code
 *    trigger_object(in): trigger object
 *    newname(in): new name
 *
 * Note:
 *    Since the name table
 *    is managed as an association list of name/object pairs, we must
 *    change the name in the table for the associated trigger.
 *    Note that if the transaction is aborted, global trigger object
 *    is decached so we don't have to unwind our change to the alist
 *    before rollback.
 */
static int
trigger_table_rename (DB_OBJECT * trigger_object, const char *newname)
{
  int error = NO_ERROR;
  DB_SET *table;
  DB_VALUE value;
  int max, save, i, found;
  DB_OBJECT *exists;

  /* make sure we don't already have one */
  if (trigger_table_find (newname, &exists))
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  if (exists != NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TR_TRIGGER_EXISTS, 1,
	      newname);
      return er_errid ();
    }

  /* change the name */
  AU_DISABLE (save);
  if (Au_root != NULL
      && (error = obj_lock (Au_root, 1)) == NO_ERROR
      && (error = obj_get (Au_root, "triggers", &value)) == NO_ERROR)
    {
      if (DB_IS_NULL (&value))
	{
	  table = NULL;
	}
      else
	{
	  table = DB_GET_SET (&value);
	}

      if (table == NULL)
	{
	  error = ER_TR_TRIGGER_NOT_FOUND;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, newname);
	}
      else
	{
	  error = set_filter (table);
	  max = set_size (table);
	  for (i = 1, found = -1; i < max && error == NO_ERROR && found == -1;
	       i += 2)
	    {
	      if ((error = set_get_element (table, i, &value)) == NO_ERROR)
		{
		  if (DB_VALUE_TYPE (&value) == DB_TYPE_OBJECT
		      && DB_GET_OBJECT (&value) == trigger_object)
		    {
		      found = i;
		    }
		  pr_clear_value (&value);
		}
	    }
	  if (found == -1)
	    {
	      error = ER_TR_INTERNAL_ERROR;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, newname);
	    }
	  else
	    {
	      /*
	       * the name is the kept in the element immediately preceeding
	       * this one
	       */
	      db_make_string (&value, newname);
	      error = set_put_element (table, found - 1, &value);
	    }
	  set_free (table);
	}
    }
  AU_ENABLE (save);
  return error;
}

/*
 * trigger_table_drop() - Removes a trigger entry from the global trigger
 *                        name table
 *    return: error code
 *    name(in): trigger name
 *
 */
static int
trigger_table_drop (const char *name)
{
  int error = NO_ERROR;
  DB_SET *table;
  DB_VALUE value;
  int max, i, found, save;

  AU_DISABLE (save);

  if (Au_root != NULL
      && (error = obj_lock (Au_root, 1)) == NO_ERROR
      && (error = obj_get (Au_root, "triggers", &value)) == NO_ERROR)
    {
      if (DB_IS_NULL (&value))
	{
	  table = NULL;
	}
      else
	{
	  table = DB_GET_SET (&value);
	}
      if (table == NULL)
	{
	  error = ER_TR_TRIGGER_NOT_FOUND;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, name);
	}
      else
	{
	  error = set_filter (table);
	  max = set_size (table);
	  for (i = 0, found = -1; i < max && error == NO_ERROR && found == -1;
	       i += 2)
	    {
	      if ((error = set_get_element (table, i, &value)) == NO_ERROR)
		{
		  if (DB_VALUE_TYPE (&value) == DB_TYPE_STRING
		      && !DB_IS_NULL (&value)
		      && DB_GET_STRING (&value) != NULL
		      && COMPARE_TRIGGER_NAMES (DB_PULL_STRING (&value),
						name) == 0)
		    {
		      found = i;
		    }
		  pr_clear_value (&value);
		}
	    }
	  if (error == NO_ERROR)
	    {
	      if (found == -1)
		{
		  error = ER_TR_TRIGGER_NOT_FOUND;
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, name);
		}
	      else
		{
		  error = set_drop_seq_element (table, found);
		  if (error == NO_ERROR)
		    {
		      error = set_drop_seq_element (table, found);
		    }
		  /*
		   * if we get an error on either of these, abort the
		   * transaction since the trigger table is now in an
		   * inconsistent state
		   */
		}
	    }
	  set_free (table);
	}
    }

  AU_ENABLE (save);

  return error;
}

/*
 * check_authorization() - This checks authorization on a trigger structure
 *    return: non-zero if authorization is ok
 *    trigger(in): trigger to examine
 *    alter_flag(in): non-zero if we're going to be modifying this trigger
 *
 * Note:
 *    If the trigger is associated with a class, the current user must
 *    have appropriate class authorization.
 *    If the trigger is associated with a user, the current user must
 *    be the owner of the trigger.
 *    If the alter flag is set, it means that the trigger is about to
 *    be modified in some way.
 */
static bool
check_authorization (TR_TRIGGER * trigger, bool alter_flag)
{
  int error;
  bool status = false;

  /*
   * When classes are deleted, their associated triggers
   * are marked as invalid but the triggers themselves are not
   * deleted.  Need to recognize this.  If the trigger is invalid
   * then it can be dropped only by its owner.
   */

  if (trigger->status == TR_STATUS_INVALID)
    {
      if (au_is_dba_group_member (Au_user)
	  || ws_is_same_object (trigger->owner, Au_user))
	{
	  status = true;
	}
    }
  else if (IS_CLASS_EVENT (trigger->event))
    {
      if (trigger->class_mop != NULL)
	{
	  /* must check authorization against the associated class */
	  if (alter_flag)
	    {
	      error = au_check_authorization (trigger->class_mop, AU_ALTER);
	    }
	  else
	    {
	      error = au_check_authorization (trigger->class_mop, AU_SELECT);
	    }

	  if (error == NO_ERROR)
	    {
	      status = true;
	    }
	}
      else
	{
	  error = ER_TR_INTERNAL_ERROR;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, trigger->name);
	}
    }
  else
    {
      /* its a user trigger, must be the active user */
      if (ws_is_same_object (trigger->owner, Au_user))
	{
	  status = true;
	}
    }
  return (status);
}

/*
 * find_all_triggers() - Returns a list of all trigger objects that have been
 *                       defined.
 *    return: error code
 *    active_filter(in):
 *    alter_filter(in):
 *    list(out): trigger list (returned)
 *
 * Note:
 *    List must be freed by db_objlist_free (ml_ext_free) when no longer
 *    required.
 */
static int
find_all_triggers (bool active_filter, bool alter_filter, DB_OBJLIST ** list)
{
  int error = NO_ERROR;
  TR_TRIGGER *trigger;
  DB_SET *table;
  DB_VALUE value;
  int max, i;

  *list = NULL;

  if (Au_root != NULL
      && (error = obj_get (Au_root, "triggers", &value)) == NO_ERROR)
    {
      if (DB_IS_NULL (&value))
	{
	  table = NULL;
	}
      else
	{
	  table = DB_GET_SET (&value);
	}

      if (table != NULL)
	{
	  error = set_filter (table);
	  max = set_size (table);
	  for (i = 1; i < max && error == NO_ERROR; i += 2)
	    {
	      error = set_get_element (table, i, &value);
	      if (error == NO_ERROR)
		{
		  if (DB_VALUE_TYPE (&value) == DB_TYPE_OBJECT
		      && !DB_IS_NULL (&value)
		      && DB_GET_OBJECT (&value) != NULL)
		    {
		      /*
		       * think about possibly avoiding this, especially
		       * if we're going to turn around and delete it
		       */
		      trigger = tr_map_trigger (DB_GET_OBJECT (&value), 1);
		      if (trigger == NULL)
			{
			  assert (er_errid () != NO_ERROR);
			  error = er_errid ();
			}
		      else
			{
			  if ((!active_filter
			       || trigger->status == TR_STATUS_ACTIVE)
			      && check_authorization (trigger, alter_filter))
			    {
			      error =
				ml_ext_add (list, DB_GET_OBJECT (&value),
					    NULL);
			    }
			}
		    }
		}
	    }
	  set_free (table);
	}
    }
  if (error != NO_ERROR && *list != NULL)
    {
      ml_ext_free (*list);
      *list = NULL;
    }

  return (error);
}

/*
 * get_schema_trigger_objects() - Work function for find_event_triggers
 *    return: error code
 *    class(in): class object
 *    attribute(in): attribute name
 *    event(in): event type
 *    active_flag(in): non-zero to check for active status
 *    object_list(out): trigger object list (returned)
 *
 * Note:
 *    Class has already been checked for user ALTER privilege in
 *    check_target called from tr_find_event_triggers.  Locate the trigger
 *    list for a particular schema event.
 */
static int
get_schema_trigger_objects (DB_OBJECT * class_mop,
			    const char *attribute, DB_TRIGGER_EVENT event,
			    bool active_flag, DB_OBJLIST ** object_list)
{
  TR_SCHEMA_CACHE *cache;
  TR_TRIGLIST *t = NULL;
  int error = NO_ERROR;

  *object_list = NULL;

  if (sm_get_trigger_cache (class_mop, attribute, 0, (void **) &cache))
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  if (cache != NULL)
    {
      if (tr_validate_schema_cache (cache))
	{
	  assert (er_errid () != NO_ERROR);
	  return er_errid ();
	}

      if (event == TR_EVENT_ALL)
	{
	  if (!active_flag)
	    {
	      /* if we're lucky we can just use the existing object list */
	      *object_list = ml_ext_copy (cache->objects);
	    }
	  else
	    {
	      int e;
	      /* get all active trigger objects */
	      for (e = 0; e < cache->array_length; e++)
		{
		  for (t = cache->triggers[e]; t && error == NO_ERROR;
		       t = t->next)
		    {
		      if (t->trigger->status == TR_STATUS_ACTIVE)
			{
			  error =
			    ml_ext_add (object_list, t->trigger->object,
					NULL);
			}
		    }
		}
	    }
	}
      else if (event < cache->array_length)
	{
	  for (t = cache->triggers[event]; t && error == NO_ERROR;
	       t = t->next)
	    {
	      if (!active_flag || t->trigger->status == TR_STATUS_ACTIVE)
		{
		  error = ml_ext_add (object_list, t->trigger->object, NULL);
		}
	    }
	}

      if (error != NO_ERROR && *object_list != NULL)
	{
	  ml_ext_free (*object_list);
	  *object_list = NULL;
	}

    }

  return (error);
}

/*
 * find_event_triggers() - The following function finds all the triggers that
 *                         have the given event, class, and attribute.
 *    return: error code
 *    event(in): event type
 *    class(in): class object (optional)
 *    attribute(in): attribute name (optinal)
 *    active_filter(in): active status flag
 *    list(out): trigger list (returned)
 *
 * Note:
 *
 * The following function finds all the triggers that have the given event,
 * class, and attribute. The trigger objects are returned in a moplist "list".
 * If there are no triggers with the given event, class, and attribute,
 * the argument list returns NULL.
 * NOTE THAT THIS IS NOT AN ERROR (NO ERROR CODE IS SET).
 * The combination of trigger event, class, and attribute must have been
 * validated. The list needs to be freed using db_objlist_free when it is no
 * longer needed.  Note that the constructed object list is an "external"
 * object list so that it will be a GC root.
 * Note that the function does NOT need to take care of the implication that
 * any event that raises a trigger with a target class and a target attribute
 * should also raise every trigger that has the same target class and no
 * target attribute. The implication is done in the trigger manager.
 */
static int
find_event_triggers (DB_TRIGGER_EVENT event, DB_OBJECT * class_mop,
		     const char *attribute, bool active_filter,
		     DB_OBJLIST ** list)
{
  int error = NO_ERROR;

  *list = NULL;

  if (class_mop == NULL)
    {
      error = get_user_trigger_objects (event, active_filter, list);
    }
  else
    {
      error = get_schema_trigger_objects (class_mop, attribute, event,
					  active_filter, list);
    }

  return (error);
}

/* TRIGGER CREATION AND SEMANTIC ANALYSIS */

/*
 * check_target() - This checks to see if the target parameters make sense
 *    return: non-zero if target is valid
 *    event(in): trigger event
 *    class_mop(in): class
 *    attribute(in): attribute name
 *
 */
static bool
check_target (DB_TRIGGER_EVENT event, DB_OBJECT * class_mop,
	      const char *attribute)
{
  bool status = false;

  /* If this is a class event, the class argument must be supplied */
  if (IS_CLASS_EVENT (event))
    {
      /* class event, at least the class must be specified */
      if (class_mop == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_TR_MISSING_TARGET_CLASS, 0);
	}

      /* User must have ALTER privilege for the class */
      else if (au_check_authorization (class_mop, AU_ALTER) == NO_ERROR)
	{

	  if (attribute == NULL)
	    {
	      status = true;
	    }
	  else
	    {
	      /* attribute is only allowed for update events */
	      if (event != TR_EVENT_UPDATE
		  && event != TR_EVENT_STATEMENT_UPDATE
		  && event != TR_EVENT_ALL)
		{		/* <-- used in find_event_triggers */
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_TR_BAD_TARGET_ATTR, 1, attribute);
		}
	      /* attribute must be defined in the class */
	      else if (db_get_attribute (class_mop, attribute) != NULL)
		{
		  status = true;
		}
	    }
	}
    }
  else
    {
      /* not a class event, class and attribute must be unspecified */
      if (class_mop != NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TR_BAD_TARGET_CLASS,
		  1, sm_class_name (class_mop));
	}
      else if (attribute != NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TR_BAD_TARGET_ATTR,
		  1, attribute);
	}
      else
	{
	  status = true;
	}
    }

  return (status);
}

/*
 * check_semantics() - This function checks the validity of a trigger
 *                     structure about to be installed.
 *    return: error code
 *    trigger(in): proposed trigger structure
 *
 */
static int
check_semantics (TR_TRIGGER * trigger)
{
  int error;
  DB_OBJECT *object;
  TR_ACTIVITY *condition, *action;
  const char *c_time, *a_time;

  /* See if the trigger already exists. */
  if ((error = trigger_table_find (trigger->name, &object)) != NO_ERROR)
    return error;

  if (object != NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TR_TRIGGER_EXISTS,
	      1, trigger->name);
      return er_errid ();
    }

  /* Priority must be non-negative. */
  if (trigger->priority < 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TR_INVALID_PRIORITY, 0);
      return er_errid ();
    }

  /* Check event for usable trigger events */
  if (trigger->event == TR_EVENT_NULL || trigger->event == TR_EVENT_ALL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TR_INVALID_EVENT, 0);
      return er_errid ();
    }

  /* Check target class, attribute, and authorization. */
  if (!check_target (trigger->event, trigger->class_mop, trigger->attribute))
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  /*
   * CONDITION
   * Should compile if necessary.
   */
  condition = trigger->condition;
  if (condition != NULL && condition->type != TR_ACT_NULL)
    {

      /* Must be an expression suitable for EVALUATE */
      if (condition->type != TR_ACT_EXPRESSION)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_TR_INVALID_CONDITION_TYPE, 0);
	  return er_errid ();
	}

      /*
       * Formerly, we checked here to make sure that there was no
       * subquery in the search condition.  Not sure how to detect this,
       * we may just want to make this a supported but "not recommended"
       * option
       */
    }

  /*
   * ACTION
   * Should compile if necessary
   */
  action = trigger->action;
  if (action != NULL)
    {

      /* REJECT actions cannot be AFTER or DEFERRED */
      if (action->type == TR_ACT_REJECT
	  && (action->time == TR_TIME_AFTER
	      || action->time == TR_TIME_DEFERRED))
	{
	  /*
	   * REJECT action cannot be used with an action time of
	   * AFTER or DEFERRED
	   */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TR_REJECT_AFTER_EVENT,
		  0);
	  return er_errid ();
	}

      /* REJECT actions cannot be applied to TIMEOUT or ABORT events */
      if (action->type == TR_ACT_REJECT
	  && (trigger->event == TR_EVENT_ABORT
	      || trigger->event == TR_EVENT_TIMEOUT))
	{
	  /* REJECT action cannot be used with the ABORT or TIMEOUT events */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TR_REJECT_NOT_POSSIBLE,
		  0);
	  return er_errid ();
	}

      /* INVALIDATE TRANSACTION events cannot be DEFERRED
         Why not?? */
#if 0
      if (action->type == TR_ACT_INVALIDATE
	  && action->time == TR_TIME_DEFERRED)
	{
	  /* INVALIDATE TRANCACTION action cannot be DEFERRED */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TR_INVALIDATE_DEFERRED,
		  0);
	  return er_errid ();
	}
#endif /* 0 */

      /*
       * Formerly tested to allow only CALL statements in the action.
       * Others might be useful but "not recommended" since the side effects
       * of the statement might effect the behavior of the trigger.
       */
    }

  /*
   * TIME
   * action time must be greater or equal to condition time
   */
  if (trigger->condition != NULL && trigger->action != NULL
      && trigger->action->time < trigger->condition->time)
    {

      c_time = time_as_string (trigger->condition->time);
      a_time = time_as_string (trigger->action->time);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TR_INVALID_ACTION_TIME,
	      2, c_time, a_time);
      return er_errid ();
    }

  return NO_ERROR;
}

/*
 * tr_check_correlation() - Trigger semantics disallow the use of "new" in
 *                          a before insert trigger to reference the OID of
 *                          the (yet to be) inserted instance.  So, we must
 *                          walk the statement searching for this special case
 *    return: the node
 *    parser(in): the parser context
 *    node(in): the activity statement
 *    arg(in): unused
 *    walk_on(in): controls traversal
 *
 */
static PT_NODE *
tr_check_correlation (PARSER_CONTEXT * parser, PT_NODE * node,
		      void *arg, int *walk_on)
{

  /*
   * If we have a name node, then we already know it's a before insert
   *  trigger, so check to see if the resolved name is "new".
   *  If so, and there is no original, the it must be "new" and not "new.a".
   */
  /* TODO: char *info.name.original; compare with NULL?? */
  if (node->node_type == PT_NAME && node->info.name.resolved
      && node->info.name.original && (node->info.name.original == NULL)
      && !strcmp (node->info.name.resolved, NEW_REFERENCE_NAME))
    {
      PT_ERROR (parser, node, er_msg ());
    }

  return node;
}

/*
 * tr_create_trigger() - Primary interface function for defining a trigger
 *    return: trigger object (persistent database object)
 *    name(in): unique name
 *    status(in): active/inactive
 *    priority(in): relative priority (default is 0.0)
 *    event(in): event type
 *    class(in): target class
 *    attribute(in): target attribute name
 *    cond_time(in): condition time
 *    cond_source(in): condition source string
 *    action_time(in): action time
 *    action_type(in): action type
 *    action_source(in): action expression source
 *
 * Note:
 *      Errors: ER_TR_TRIGGER_EXISTS:
 *              ER_TR_INVALIDE_PRIORITY:
 *              ER_TR_MISSING_TARGET_CLASS:
 *              ER_AU_ALTER_FAILURE:
 *              ER_SM_ATTRIBUTE_NOT_FOUND:
 *              ER_TR_INCONSISTENT_TARGET:
 *              ER_TR_INVALID_ACTION_TIME:
 *              ER_TR_INVALID_CONDITION:
 *              ER_TR_INVALID_ACTION:
 *              ER_TR_INVALID_EVENT:
 */
DB_OBJECT *
tr_create_trigger (const char *name,
		   DB_TRIGGER_STATUS status,
		   double priority,
		   DB_TRIGGER_EVENT event,
		   DB_OBJECT * class_mop,
		   const char *attribute,
		   DB_TRIGGER_TIME cond_time,
		   const char *cond_source,
		   DB_TRIGGER_TIME action_time,
		   DB_TRIGGER_ACTION action_type, const char *action_source)
{
  TR_TRIGGER *trigger;
  DB_OBJECT *object;
  char realname[SM_MAX_IDENTIFIER_LENGTH];
  bool tr_object_map_added = false;

  object = NULL;
  trigger = tr_make_trigger ();
  if (trigger == NULL)
    {
      return NULL;
    }

  /* Initialize a trigger */
  trigger->owner = Au_user;
  trigger->status = status;
  trigger->priority = priority;
  trigger->event = event;
  trigger->class_mop = class_mop;

  if (class_mop != NULL && db_is_vclass (class_mop))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TR_NO_VCLASSES, 1,
	      db_get_class_name (class_mop));
      goto error;
    }

  trigger->name = tr_process_name (name);
  if (trigger->name == NULL)
    goto error;

  if (attribute != NULL)
    {
      sm_downcase_name (attribute, realname, SM_MAX_IDENTIFIER_LENGTH);
      trigger->attribute = strdup (realname);
    }
  else
    {
      trigger->attribute = NULL;
    }
  if (attribute != NULL && trigger->attribute == NULL)
    goto error;

  /* build the condition */
  if (cond_source != NULL)
    {
      trigger->condition = make_activity ();
      if (trigger->condition == NULL)
	goto error;

      trigger->condition->type = TR_ACT_EXPRESSION;
      trigger->condition->source = strdup (cond_source);
      if (trigger->condition->source == NULL)
	goto error;

      if (cond_time != TR_TIME_NULL)
	{
	  trigger->condition->time = cond_time;
	}
      else if (action_time != TR_TIME_NULL)
	{
	  trigger->condition->time = action_time;
	}
      else
	{
	  trigger->condition->time = TR_TIME_AFTER;
	}
    }

  /* build the action */
  if (action_type != TR_ACT_NULL)
    {
      if (action_type == TR_ACT_EXPRESSION && action_source == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_TR_MISSING_ACTION_STRING, 0);
	  goto error;
	}

      trigger->action = make_activity ();
      if (trigger->action == NULL)
	goto error;

      trigger->action->type = action_type;
      if (action_source != NULL)
	{
	  trigger->action->source = strdup (action_source);
	  if (trigger->action->source == NULL)
	    goto error;
	}

      if (action_time != TR_TIME_NULL)
	{
	  trigger->action->time = action_time;
	}
      else if (cond_time != TR_TIME_NULL)
	{
	  trigger->action->time = cond_time;
	}
      else
	{
	  trigger->action->time = TR_TIME_AFTER;
	}
    }

  /* make sure everything looks ok */
  if (check_semantics (trigger))
    goto error;

  /* be sure that the condition and action expressions can be compiled */
  if (trigger->condition != NULL
      && compile_trigger_activity (trigger, trigger->condition, 1))
    goto error;

  if (trigger->action != NULL
      && compile_trigger_activity (trigger, trigger->action, 0))
    goto error;

  /*
   * from here down, the unwinding when errors are encountered gets
   * rather complex
   */

  /* convert to a persistent instance */
  object = trigger_to_object (trigger);
  if (object == NULL)
    goto error;

  /* put it on the "new" trigger list */
  if (insert_trigger_list (&tr_Uncommitted_triggers, trigger))
    {
      /*
       * we could't delete the trigger objects we just created ?
       * db_drop(object);
       */
      goto error;
    }

  /* add to global name table */
  if (trigger_table_add (trigger->name, object))
    goto error;

  /*
   * add to object map table, could check for trigger already at this
   * location but since this is a new MOP that "can't" happen
   */
  if (mht_put (tr_object_map, object, trigger) == NULL)
    goto error;

  tr_object_map_added = true;

  /*
   * cache the trigger in the the schema or the user object
   * for later reference
   */
  if (trigger->class_mop != NULL)
    {
      if (sm_add_trigger (trigger->class_mop, trigger->attribute, 0,
			  trigger->object))
	goto error;
    }
  else
    {
      if (register_user_trigger (object))
	goto error;
    }

  return (object);

error:
  if (trigger != NULL)
    {
      if (object != NULL)
	{
	  if (tr_object_map_added)
	    {
	      (void) mht_rem (tr_object_map, trigger->object, NULL, NULL);
	    }

	  (void) trigger_table_drop (trigger->name);
	}
      remove_trigger_list (&tr_Uncommitted_triggers, trigger);
      free_trigger (trigger);
    }

  return (NULL);
}


/* TRIGGER OBJECT LOCATION */

/*
 * These functions locate triggers and return trigger instances.
 *
 */

/*
 * tr_find_all_triggers() - This function returns a list of object pointers
 *    return: error code
 *    list(out): pointer to the return trigger object list
 *
 * Note:
 *    The return list contains every user trigger owned by the user, and every
 *    class trigger such that the user has the SELECT privilege for
 *    the class in its event target. The return object pointer list
 *    must be freed using db_objlist_free it is no longer needed.
 *
 */
int
tr_find_all_triggers (DB_OBJLIST ** list)
{
  int error;
  int save;

  AU_DISABLE (save);

  error = find_all_triggers (false, false, list);

  AU_ENABLE (save);
  return error;
}

/*
 * tr_find_trigger() - This function returns the object pointer of the trigger
 *                     with the input name.
 *    return: DB_OBJECT
 *    name(in): trigger name
 * Note :
 *      If no existing trigger has the name, or the
 *      user does not have the access privilege of the trigger, NULL
 *      will be returned. If NULL is returned, the system will set the
 *      global error status indicating the exact nature of the error.
 *
 * Errors:
 *      ER_TR_TRIGGER_NOT_FOUND:
 *       A trigger with the specified name could not be located.
 *      ER_TR_TRIGGER_SELECT_FAILURE:
 *       The trigger is a user trigger that does not belong to the user.
 *      ER_AU_SELECT_FAILURE:
 *       The user does not have the SELECT privilege for the target class
 *       of the specified target (the trigger must be a class trigger).
 *
 */
DB_OBJECT *
tr_find_trigger (const char *name)
{
  DB_OBJECT *object;
  TR_TRIGGER *trigger;
  int save;

  object = NULL;
  AU_DISABLE (save);

  if (trigger_table_find (name, &object) == NO_ERROR)
    {
      if (object == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TR_TRIGGER_NOT_FOUND,
		  1, name);
	}
      else
	{
	  trigger = tr_map_trigger (object, 1);
	  if (trigger == NULL)
	    {
	      object = NULL;
	    }
	  else
	    {
	      if (!check_authorization (trigger, 0))
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_TR_TRIGGER_SELECT_FAILURE, 1, name);
		  object = NULL;
		}
	    }
	}
    }

  AU_ENABLE (save);
  return (object);
}

/*
 * tr_find_event_triggers() - Locate all triggers that are defined for
 *                            a particular event
 *    return: int
 *    event(in): event type
 *    class_mop(in): target class
 *    attribute(in): target attribute
 *    active(in): flag to retrieve active status triggers only
 *    list(out): pointer to the return list of object pointers
 *
 */
int
tr_find_event_triggers (DB_TRIGGER_EVENT event, DB_OBJECT * class_mop,
			const char *attribute, bool active,
			DB_OBJLIST ** list)
{
  int error = NO_ERROR;
  int save;

  AU_DISABLE (save);

  /* check for sensible parameters and ALTER authorization for class */
  if (!check_target (event, class_mop, attribute))
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
    }
  else
    {
      error = find_event_triggers (event, class_mop, attribute, active, list);
    }

  AU_ENABLE (save);
  return error;
}

/*
 * tr_check_authorization() - This is used to see if a particular authorization
 *                            is enabled for a trigger object
 *    return: error code
 *    trigger_object(in): trigger object
 *    alter_flag(in): non-zero if needing alter authorization
 *
 * Note:
 *    This is used to see if a particular authorization is enabled
 *    for a trigger object.  It is intended to be called by do_trigger
 *    to make sure that statement operations involving multiple
 *    triggers can be performed without authorization errors.
 *    Since trigger objects are individually authorized, we can't
 *    use db_check_authorization because the db_trigger class
 *    is normally completely protected.
 *    If the alter-flag is zero, we just check for basic read authorization
 *    if the flag is non-zero, we also check for ALTER authorization on
 *    the associated class.
 */
int
tr_check_authorization (DB_OBJECT * trigger_object, int alter_flag)
{
  int error = NO_ERROR;
  TR_TRIGGER *trigger;
  int save;

  AU_DISABLE (save);

  trigger = tr_map_trigger (trigger_object, 1);

  if (trigger == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
    }
  else
    {
      if (!check_authorization (trigger, alter_flag))
	{
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	}
    }

  AU_ENABLE (save);
  return (error);
}

/* TRIGGER REMOVAL */

/*
 * tr_drop_trigger_internal() - This is a work function for tr_drop_trigger
 *                              and is also called by tr_check_rollback_event
 *                              to get rid of triggers created during
 *                              the current transaction
 *    return: error code
 *    trigger(in): trigger cache structure
 *    rollback(in):
 *
 * Note:
 *    It removes the trigger from the various
 *    structures it may be attached to and deletes it.  If this is
 *    part of the rollback operation, we're more tolerant of errors since
 *    this operation has to be performed regardless.
 *    ROLLBACK NOTES:
 *    It probably isn't necessary that we remove things from the schema
 *    cache because the class will have been marked dirty and will be
 *    re-loaded with the old trigger state during the next transaction.
 *    The main thing this does is remove the object from the trigger
 *    mapping table.
 *
 */
static int
tr_drop_trigger_internal (TR_TRIGGER * trigger, int rollback)
{
  int error = NO_ERROR;
  int save;

  AU_DISABLE (save);

  /* remove it from the class or user cache */
  if (trigger->class_mop == NULL)
    {
      error = unregister_user_trigger (trigger, rollback);
    }
  else
    {
      error = sm_drop_trigger (trigger->class_mop, trigger->attribute, 0,
			       trigger->object);
      /* if the class has been deleted, just ignore the error */
      if (error == ER_HEAP_UNKNOWN_OBJECT)
	{
	  error = NO_ERROR;
	}
    }

  if (!error || rollback)
    {
      /* remove it from the uncommitted trigger list (if its on there) */
      remove_trigger_list (&tr_Uncommitted_triggers, trigger);

      /* remove it from the memory cache */
      error = tr_unmap_trigger (trigger);

      if (!error || rollback)
	{
	  /* remove it from the global name table */
	  error = trigger_table_drop (trigger->name);

	  if (!error && !rollback)
	    {
	      /*
	       * if this isn't a rollback, delete the object, otherwise
	       * it will already be marked as deleted as part of the normal
	       * transaction cleanup
	       */
	      db_drop (trigger->object);

	      /* check whether successfully dropped as follow:
	       *  - flush, decache, fetch again 
	       */
	      error = locator_flush_instance (trigger->object);
	      if (error == NO_ERROR)
		{
		  ws_decache (trigger->object);
		  ws_clear_hints (trigger->object, false);
		  error = au_fetch_instance_force (trigger->object, NULL,
						   AU_FETCH_WRITE);
		  if (error == NO_ERROR)
		    {
		      /* 
		       * The object was not deleted in fact. This is possible
		       * when we start delete from intermediary version
		       * (not the last one). This may happen when another
		       * concurrent transaction has updated the trigger before me.
		       * The current solution may be expensive, but drop trigger 
		       * is rarely used.
		       * A better solution would be to get & lock the last
		       * version from beginning, not the visible one.
		       */
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			      ER_TR_TRIGGER_NOT_FOUND, 1, trigger->name);
		      error = ER_TR_TRIGGER_NOT_FOUND;
		    }
		  else if (error == ER_HEAP_UNKNOWN_OBJECT)
		    {
		      /* clear the error - the object was successfully dropped */
		      er_clear ();
		      error = NO_ERROR;
		    }
		}
	    }

	  /* free the cache structure */
	  free_trigger (trigger);
	}
    }

  AU_ENABLE (save);
  return (error);
}

/*
 * tr_drop_trigger() - Removes a trigger from the system.
 *    return: error
 *    obj(in): trigger object
 *    call_from_api(in): call from api flag
 *
 * Note:
 *    We still check to make sure the active user is the owner
 *    of the trigger before proceeding.
 */
int
tr_drop_trigger (DB_OBJECT * obj, bool call_from_api)
{
  int error = NO_ERROR;
  TR_TRIGGER *trigger;
  char *trigger_name = NULL;
  int save;

  /* Do we need to disable authorizationjust for check_authorization ? */
  AU_DISABLE (save);

  /*
   * Turn off the "fetch" flag to tr_map_trigger so we don't attempt
   * to validate the trigger by compiling the statements, etc.  As we're going
   * to delete it, we don't care if the trigger is valid or not.
   * In particular this is necessary if any of the triggers referenced classes
   * have been deleted because the validation will fail tr_map_trigger would
   *  return an error.
   */

  trigger = tr_map_trigger (obj, false);
  if (trigger == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
    }
  else
    {
      trigger_name = strdup (trigger->name);
    }

  if (trigger == NULL)
    {
      ;
    }
  else if (!check_authorization (trigger, true))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TR_TRIGGER_DELETE_FAILURE,
	      1, trigger->name);
      error = er_errid ();
    }
  else
    {
      if ((trigger->condition
	   && trigger->condition->time == TR_TIME_DEFERRED)
	  || (trigger->action && trigger->action->time == TR_TIME_DEFERRED))
	{
	  error = tr_drop_deferred_activities (obj, NULL);
	}

      if (error == NO_ERROR)
	{
	  error = tr_drop_trigger_internal (trigger, 0);
	}
    }

  AU_ENABLE (save);

  if (trigger_name)
    {
      free_and_init (trigger_name);
    }

  return error;
}

/* CONDITION & ACTION EVALUATION */

/*
 * value_as_boolean() - This maps a value container into a boolesn value.
 *    return: boolean (zero or non-zero)
 *    value(in): standard value container
 *
 * Note:
 *    It is intended
 *    to be used where condition expressions can return any value but
 *    the result must be treated as a boolean.
 *    The status will be false if the value contains one of the numeric
 *    types whose value is zero.  The status will also be false if
 *    the value is of type DB_TYPE_NULL.
 *    For all other data types, the status will be true.
 *    NOTE: This means that an empty string will be true as will sets
 *    with no elements.
 */
static bool
value_as_boolean (DB_VALUE * value)
{
  bool status = true;

  switch (DB_VALUE_TYPE (value))
    {

    case DB_TYPE_NULL:
      status = false;
      break;
    case DB_TYPE_SHORT:
      status = (DB_GET_SHORT (value) == 0) ? false : true;
      break;
    case DB_TYPE_INTEGER:
      status = (DB_GET_INT (value) == 0) ? false : true;
      break;
    case DB_TYPE_BIGINT:
      status = (DB_GET_BIGINT (value) == 0) ? false : true;
      break;
    case DB_TYPE_FLOAT:
      status = (DB_GET_FLOAT (value) == 0) ? false : true;
      break;
    case DB_TYPE_DOUBLE:
      status = (DB_GET_DOUBLE (value) == 0) ? false : true;
      break;
    case DB_TYPE_TIME:
      status = (*DB_GET_TIME (value) == 0) ? false : true;
      break;
    case DB_TYPE_UTIME:
      status = (*DB_GET_UTIME (value) == 0) ? false : true;
      break;
    case DB_TYPE_DATETIME:
      status = (DB_GET_DATETIME (value)->date == 0
		&& DB_GET_DATETIME (value)->time == 0) ? false : true;
      break;
    case DB_TYPE_DATE:
      status = (*DB_GET_DATE (value) == 0) ? false : true;
      break;
    case DB_TYPE_MONETARY:
      status = (DB_GET_MONETARY (value)->amount == 0) ? false : true;
      break;

    default:
      status = true;
    }

  return (status);
}

/*
 * signal_evaluation_error() - This is called when a trigger condition or
 *                             action could not be evaluated.
 *    return: error code
 *    trigger(in): trigger of interest
 *    error(in):
 *
 * Note:
 *    We take whatever the last error was set by the
 *    parser and package it up into an error that contains the name
 *    of the trigger so we have some context to determine what went
 *    wrong.  This is especially if the error happens during the
 *    evaluation of a deferred trigger at commit time.
 *    Note that since the error text is kept in a static buffer, we can't
 *    pass it to er_set() without corrupting things.  Must copy it
 *    into a temp buffer.
 */
static int
signal_evaluation_error (TR_TRIGGER * trigger, int error)
{
  char buffer[MAX_ERROR_STRING];
  const char *msg;

  /*
   * if we've already set this error, don't do it again, this
   * is for recursive triggers so we don't keep tacking
   * on the name 'n' times for each level of call
   */

  if (er_errid () != error && er_errid () != ER_LK_UNILATERALLY_ABORTED)
    {
      msg = er_msg ();
      if (msg == NULL)
	{
	  strcpy (buffer, "");
	}
      else
	{
	  strncpy (buffer, msg, sizeof (buffer) - 1);
	  buffer[MAX_ERROR_STRING - 1] = '\0';
	}

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 2, trigger->name,
	      buffer);
    }

  return error;
}

/*
 * eval_condition() - The function evaluates the input trigger condition.
 *    return: TR_RETURN_ERROR/TR_RETURN_TRUE/TR_RETURN_FALSE
 *    trigger(in):
 *    current(in):
 *    temp(in):
 *    status(in):
 *
 */
static int
eval_condition (TR_TRIGGER * trigger, DB_OBJECT * current, DB_OBJECT * temp,
		bool * status)
{
  int error = NO_ERROR;
  TR_ACTIVITY *act;
  DB_VALUE value;
  int pt_status;

  act = trigger->condition;
  if (act != NULL)
    {
      if (tr_Trace)
	{
	  fprintf (stdout,
		   "TRACE: Evaluating condition for trigger \"%s\".\n",
		   trigger->name);
	}

      if (act->type != TR_ACT_EXPRESSION)
	{
	  /* this should have been checked by now */
	  error = ER_TR_INVALID_CONDITION_TYPE;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	}
      else
	{
	  /* should have been done by now */
	  if (tr_Current_depth <= 1
	      && ++act->exec_cnt >
	      prm_get_integer_value (PRM_ID_RESET_TR_PARSER)
	      && prm_get_integer_value (PRM_ID_RESET_TR_PARSER) > 0)
	    {
	      parser_free_parser ((PARSER_CONTEXT *) act->parser);
	      act->parser = NULL;
	      act->exec_cnt = 0;
	    }
	  if (act->parser == NULL)
	    {
	      error = compile_trigger_activity (trigger, act, 1);
	    }

	  if (error == NO_ERROR)
	    {
	      if (act->parser == NULL || act->statement == NULL)
		{
		  /* shouldn't happen */
		  error = ER_TR_INTERNAL_ERROR;
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1,
			  trigger->name);
		}
	      else
		{
		  if (current == NULL)
		    {
		      current = temp;
		      temp = NULL;
		    }

		  pt_status =
		    pt_exec_trigger_stmt ((PARSER_CONTEXT *) act->parser,
					  (PT_NODE *) act->statement, current,
					  temp, &value);

		  /*
		   * recall that pt_exec_trigger_stmt can return a positive
		   * value for success, errors must be checked against negative
		   */
		  if (pt_status < 0)
		    {
		      error = signal_evaluation_error (trigger,
						       ER_TR_CONDITION_EVAL);
		    }
		  else
		    {
		      *status = value_as_boolean (&value);
		    }

		  /*
		   * kludage, until we figure out how to reuse the same
		   * parser over and over, we have to throw away the
		   * copmiled expression and do it again the next time
		   */
#if 0
		  parser_free_parser (act->parser);
		  act->parser = NULL;
		  act->statement = NULL;
#endif /* 0 */

		}
	    }
	}
    }
  return (error);
}

/*
 * tr_check_recursivity() - Analyze the trigger stack and detect if the
 *                          given trigger has occured earlier: this is a way
 *                          to detect recursive trigger chains at runtime.
 *    return: TR_DECISION_CONTINUE - no recursion found
 *            TR_DECISION_HALT_WITH_ERROR - found recursive triggers
 *            TR_DECISION_DO_NOT_CONTINUE - found recursive STATEMENT trigger
 *    oid (in): OID of trigger to analyze
 *    stack(in): array of stack_size OIDs of the calling triggers
 *    stack_size(in):
 *    is_statement(in): if the current trigger is a STATEMENT one and it turns
 *                      out it is recursive, ignore it silently, with no error
 */
static TR_RECURSION_DECISION
tr_check_recursivity (OID oid, OID stack[], int stack_size, bool is_statement)
{
  int i, min;
  assert (stack);
  assert (stack_size < TR_MAX_RECURSION_LEVEL);

  /* we allow recursive triggers, if they are not STATEMENT triggers */
  if (!is_statement)
    {
      return TR_DECISION_CONTINUE;
    }

  min = MIN (stack_size, TR_MAX_RECURSION_LEVEL);
  for (i = 0; i < min; i++)
    {
      if (oid_compare (&oid, &stack[i]) == 0)
	{
	  /* this is a STATEMENT trigger, we should not go further
	   * with the action, but we should allow the call to succeed.
	   */
	  return TR_DECISION_DO_NOT_CONTINUE;
	}
    }

  return TR_DECISION_CONTINUE;
}

/*
 * eval_action() - The function evaluates the input trigger action.
 *    return: int
 *    trigger(in):
 *    current(in):
 *    temp(in):
 *    reject(in):
 *
 */
static int
eval_action (TR_TRIGGER * trigger, DB_OBJECT * current, DB_OBJECT * temp,
	     bool * reject)
{
  int error = NO_ERROR;
  TR_ACTIVITY *act;
  DB_VALUE value;
  int pt_status;
  OID oid_of_trigger;
  TR_RECURSION_DECISION dec;
  bool is_statement = false;

  if (trigger->object)
    {
      oid_of_trigger = trigger->object->oid_info.oid;
    }
  else
    {
      error = ER_TR_INTERNAL_ERROR;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, trigger->name);
      return error;
    }

  /* If this is NOT a statement trigger, we just continue through. Recursive
   * triggers will step past the max depth and will be rejected.
   * STATEMENT triggers, on the other side, should be fired only once. This
   * is why we keep the OID stack and we check it if we have a STATEMENT trig.
   */
  is_statement = (trigger->event == TR_EVENT_STATEMENT_DELETE ||
		  trigger->event == TR_EVENT_STATEMENT_INSERT ||
		  trigger->event == TR_EVENT_STATEMENT_UPDATE);

  if (is_statement)
    {
      dec = tr_check_recursivity (oid_of_trigger, tr_Stack,
				  tr_Current_depth - 1, is_statement);
      switch (dec)
	{
	case TR_DECISION_HALT_WITH_ERROR:
	  error = ER_TR_EXCEEDS_MAX_REC_LEVEL;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 2,
		  tr_Current_depth, trigger->name);
	  return error;

	case TR_DECISION_DO_NOT_CONTINUE:
	  return NO_ERROR;

	case TR_DECISION_CONTINUE:
	  break;
	}
    }

  if (tr_Current_depth <= TR_MAX_RECURSION_LEVEL)
    {
      tr_Stack[tr_Current_depth - 1] = oid_of_trigger;
    }
  else
    {
      assert (false);
    }

  act = trigger->action;
  if (act != NULL)
    {
      if (tr_Trace)
	{
	  fprintf (stdout, "TRACE: Executing action for trigger \"%s\".\n",
		   trigger->name);
	}

      switch (act->type)
	{
	case TR_ACT_REJECT:
	  *reject = true;
	  break;
	case TR_ACT_INVALIDATE:
	  tr_Invalid_transaction = true;
	  /* remember the name for the error message */
	  strncpy (tr_Invalid_transaction_trigger, trigger->name,
		   sizeof (tr_Invalid_transaction_trigger) - 1);
	  break;
	case TR_ACT_PRINT:
	  if (trigger->action->source != NULL)
	    fprintf (stdout, "%s\n", trigger->action->source);
	  break;
	case TR_ACT_EXPRESSION:
	  if (tr_Current_depth <= 1
	      && ++act->exec_cnt >
	      prm_get_integer_value (PRM_ID_RESET_TR_PARSER)
	      && prm_get_integer_value (PRM_ID_RESET_TR_PARSER) > 0)
	    {
	      parser_free_parser ((PARSER_CONTEXT *) act->parser);
	      act->parser = NULL;
	      act->exec_cnt = 0;
	    }
	  if (act->parser == NULL)
	    {
	      error = compile_trigger_activity (trigger, act, 0);
	    }

	  if (error == NO_ERROR)
	    {
	      if (act->parser == NULL || act->statement == NULL)
		{
		  /* shouldn't happen */
		  error = ER_TR_INTERNAL_ERROR;
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1,
			  trigger->name);
		}
	      else
		{
		  if (current == NULL)
		    {
		      current = temp;
		      temp = NULL;
		    }

		  pt_status =
		    pt_exec_trigger_stmt ((PARSER_CONTEXT *) act->parser,
					  (PT_NODE *) act->statement,
					  current, temp, &value);
		  /*
		   * recall that pt_exec_trigger_stmt can return positive
		   * values to indicate success, errors must be explicitly
		   * checked for < 0
		   */

		  if (pt_status < 0)
		    {
		      error = signal_evaluation_error (trigger,
						       ER_TR_ACTION_EVAL);
		      /*
		       * Reset the error stuff so that we'll try things
		       * afresh the next time we reuse this parser.
		       */
		      pt_reset_error ((PARSER_CONTEXT *) act->parser);
		    }

		  /*
		   * kludge, until we figure out how to reuse the same
		   * parser over and over, we have to throw away the
		   * copmiled expression and do it again the next time
		   */
#if 0
		  parser_free_parser (act->parser);
		  act->parser = NULL;
		  act->statement = NULL;
#endif /* 0 */
		}
	    }
	  break;

	default:
	  error = ER_TR_INTERNAL_ERROR;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, trigger->name);
	  break;
	}
    }

  return (error);
}

/*
 * execute_activity() - The function executes the condition or action or
 *                      condition and action of the trigger according to
 *                      the following rules.
 *    return: int
 *    trigger(in): trigger
 *    tr_time(in): execution time
 *    current(in):
 *    temp(in):
 *    rejected(in):
 *
 * Note:
 *    If the execution of the condition is the same as the argument
 *    time, the condition will be evaluated.
 *    If the condition is evaluated as true, or the execution time
 *    of the condition is less than the argument time,
 *    the action will be considered.
 *    If the action is considered and the execution time of the
 *    action is the same as the argument time, the action will
 *    be executed.
 *    The function returns TR_RETURN_ERROR if there is an error.
 *    Otherwise, it returns TR_RETURN_TRUE if the action is
 *    executed or does not need to be executed (because of the
 *    condition), or TR_RETURN_FALSE for the other cases.
 *    If the action is "REJECT", the argument is_reject_action
 *    returns true.
 */
static int
execute_activity (TR_TRIGGER * trigger, DB_TRIGGER_TIME tr_time,
		  DB_OBJECT * current, DB_OBJECT * temp, bool * rejected)
{
  int rstatus;
  bool execute_action;
  DB_OBJECT *save_user;

  save_user = NULL;
  if (trigger->owner != NULL)
    {
      save_user = Au_user;
      if (AU_SET_USER (trigger->owner))
	return TR_RETURN_ERROR;
    }

  rstatus = TR_RETURN_TRUE;
  *rejected = false;

  /* assume that we will be removing the trigger by returning true */
  execute_action = true;

  /*
   * If the trigger isn't active, ignore it.  It would be more effecient
   * if the inactive triggers could be filtered from the lists as the
   * combined list is built
   */
  if (trigger->status == TR_STATUS_ACTIVE)
    {

      if (trigger->condition != NULL)
	{
	  execute_action = false;
	  if (trigger->condition->time == tr_time)
	    {
	      if (eval_condition (trigger, current, temp, &execute_action) !=
		  NO_ERROR)
		{
		  rstatus = TR_RETURN_ERROR;
		}
	    }
	  else if ((int) trigger->condition->time > (int) tr_time)
	    {
	      /* activity is to be considered later, return false */
	      rstatus = TR_RETURN_FALSE;
	    }

	  else
	    {
	      /*
	       * else, the time has passed, only see this if the condition has
	       * been fired but the action comes later
	       */
	      execute_action = true;
	    }
	}

      if (execute_action && trigger->action != NULL)
	{
	  if (trigger->action->time == tr_time)
	    {
	      if (eval_action (trigger, current, temp, rejected) != NO_ERROR)
		{
		  rstatus = TR_RETURN_ERROR;
		}

	    }
	  else
	    {
	      /*
	       * the action is not ready yet, return false to keep it
	       * on the list
	       */

	      rstatus = TR_RETURN_FALSE;
	    }
	}
    }

  if (save_user != NULL)
    {
      if (AU_SET_USER (save_user))
	/* what can this mean ? */
	rstatus = TR_RETURN_ERROR;
    }

  return (rstatus);
}

/*
 * tr_execute_activities() - The function executes the conditions or actions
 *                           or conditions and actions of all triggers
 *                           in the array that have the input execution time
 *    return: int
 *    state(in): trigger state
 *    tr_time(in): execution time
 *    current(in): object associated with "current"
 *    temp(in): object associated with "new" or "old"
 *
 * Note:
 *    The input execution time must not
 *    be TR_TIME_DEFERRED. For triggers in the array whose
 *    conditions are executed and evaluated as true, their actions
 *    are also executed if they have the same execution time.
 *    These triggers are then removed from the array. If any of
 *    these triggers has an action whose execution time is greater
 *    than the execution time of its condition, the trigger is
 *    still kept in the array.
 *    In the course of evaluating conditions and actions,
 *    if a "REJECT" action is encountered, the event action and
 *    all the conditions and actions that are not yet executed
 *    will be suspended.
 */
static int
tr_execute_activities (TR_STATE * state, DB_TRIGGER_TIME tr_time,
		       DB_OBJECT * current, DB_OBJECT * temp)
{
  int error = NO_ERROR;
  TR_TRIGLIST *t, *next;
  int status;
  bool rejected;

  for (t = state->triggers, next = NULL;
       t != NULL && error == NO_ERROR; t = next)
    {
      next = t->next;

      status =
	execute_activity (t->trigger, tr_time, current, temp, &rejected);

      if (status == TR_RETURN_TRUE)
	{
	  /* if rejected, signal an error and abort */
	  if (rejected)
	    {
	      error = ER_TR_REJECTED;
	      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 1,
		      t->trigger->name);
	    }

	  /* successful processing, remove it from the list */
	  remove_trigger_list_element (&state->triggers, t);
	}
      else if (status == TR_RETURN_ERROR)
	{
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	}

      /* else the trigger isn't ready yet, leave it on the list */
    }

  return (error);
}

/*
 * run_user_triggers() - This runs triggers on the user trigger cache.
 *    return: error code
 *    event(in): event type to run
 *    time(in):
 *
 * Note:
 *    It is called by the various functions that handle various
 *    types of user trigger events.
 *    Because the user triggers are maintained on a cache, we have to
 *    validate it before we can proceed.
 *    We don't really have to worry about times right now because
 *    the user triggers all have limitations on their times.
 *    The trigger cache will be sorted in priority order.  We could have
 *    multiple lists for each type but don't bother right now since there
 *    aren't likely to be very many of these.
 */
static int
run_user_triggers (DB_TRIGGER_EVENT event, DB_TRIGGER_TIME time)
{
  int error = NO_ERROR;
  TR_TRIGLIST *t;
  int status;
  bool rejected;

  /* check the cache */
  if (!tr_User_triggers_valid)
    {
      if (tr_update_user_cache ())
	{
	  assert (er_errid () != NO_ERROR);
	  return er_errid ();
	}
    }

  for (t = tr_User_triggers; t != NULL && !error; t = t->next)
    {
      if (t->trigger->event == event
	  && t->trigger->status == TR_STATUS_ACTIVE)
	{

	  status = execute_activity (t->trigger, time, NULL, NULL, &rejected);

	  if (status == TR_RETURN_TRUE)
	    {
	      /* if rejected, signal an error and abort */
	      if (rejected)
		{
		  error = ER_TR_REJECTED;
		  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 1,
			  t->trigger->name);
		}
	    }
	  else if (status == TR_RETURN_ERROR)
	    {
	      assert (er_errid () != NO_ERROR);
	      error = er_errid ();
	    }
	}
    }

  return (error);
}

/* TRIGGER SIGNALING */

/*
 * compare_recursion_levels() - The function compares two recursion levels.
 *    return: int
 *    rl_1(in): a recursion level
 *    rl_2(in): a recursion level
 *
 * Note:
 *    It returns 0 if they are equal,
 *    1 if the the first is greater than the second, or
 *    -1 if the first is less than the second.
 */
static int
compare_recursion_levels (int rl_1, int rl_2)
{
  int ret;

  if (rl_1 == rl_2)
    {
      ret = 0;
    }
  else
    {
      ret = (rl_1 > rl_2) ? 1 : -1;
    }

  return ret;
}

/*
 * start_state() - This is used to create a new state or validate
 *                 an existing one.
 *    return: state structure
 *    current(in): existing state pointer
 *    name(in):
 *
 * Note:
 *    If we have to create a new one, check the recursion level for
 *    runaway triggers.
 */
static TR_STATE *
start_state (TR_STATE ** current, const char *name)
{
  TR_STATE *state;

  state = *current;
  if (state == NULL)
    {
      ++tr_Current_depth;
      if (compare_recursion_levels (tr_Current_depth, tr_Maximum_depth) > 0)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_TR_EXCEEDS_MAX_REC_LEVEL, 2, tr_Maximum_depth, name);
	  --tr_Current_depth;
	}
      else
	{
	  if ((state = make_state ()) != NULL)
	    {
	      *current = state;
	    }
	}
    }

  return (state);
}

/*
 * tr_prepare_statement() - This is called by do_statement() to prepare
 *                          a trigger state structure for triggers that
 *                          will be raised by a statement.
 *    return: error code
 *    state_p(out):
 *    event(in): event type
 *    class(in): class being touched
 *    attcount(in): number of entries in attribute name array
 *    attnames(in): attribute name array
 *
 * Note:
 *    Could build the trigger list separately and pass it to
 *    tr_prepare() but in this case we will always be throwing away
 *    the merged list so it doesn't really matter.
 *    If the statement is repeatable (in ESQL) it would be better if we
 *    could cache the trigger list in the parse tree rather than having
 *    to derive it each time.
 */
int
tr_prepare_statement (TR_STATE ** state_p,
		      DB_TRIGGER_EVENT event,
		      DB_OBJECT * class_mop, int attcount,
		      const char **attnames)
{
  int error = NO_ERROR;
  TR_STATE *state;
  TR_TRIGLIST *triggers;
  TR_SCHEMA_CACHE *cache;
  int i, save;

  /*
   * Since we may be accessing this through a view, disable authorization
   * during the building of the trigger lists.  Later when we actually
   * evaluate the trigger condition/action, we will temporarily set
   * the effective user to the owner of the trigger.
   */
  AU_DISABLE (save);

  /* locate the list of triggers for this event */
  triggers = NULL;

  /* could avoid repeated schema authorization checks */
  if ((error = sm_get_trigger_cache (class_mop, NULL, 0, (void **) &cache)))
    {
      goto error_return;
    }

  if (cache != NULL)
    {
      if (tr_validate_schema_cache (cache))
	{
	  goto error_return;
	}

      if (event < cache->array_length)
	{
	  if (merge_trigger_list (&triggers, cache->triggers[event], 0))
	    goto error_return;
	}
      /* else error ? */
    }

  for (i = 0; i < attcount && !error; i++)
    {
      if (!
	  (error =
	   sm_get_trigger_cache (class_mop, attnames[i], 0,
				 (void **) &cache)))
	{
	  if (cache != NULL)
	    {
	      if (tr_validate_schema_cache (cache))
		goto error_return;
	      else
		{
		  if (event < cache->array_length)
		    {
		      error =
			merge_trigger_list (&triggers, cache->triggers[event],
					    0);
		    }
		}
	    }
	}
    }

  if (error != NO_ERROR)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto error_return;
    }

  /*
   * Construct a state structure for these events.  If we get to the
   * point where we can cache the trigger list in the parse tree, the
   * list should be returned here and passed to tr_prepare().
   */
  if (triggers != NULL)
    {
      /* pass in the first trigger name for the recursion error message */
      state = start_state (state_p, triggers->trigger->name);
      if (state == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	  goto error_return;
	}
      else
	{
	  if (state->triggers == NULL)
	    {
	      state->triggers = triggers;
	    }
	  else
	    {
	      error = merge_trigger_list (&state->triggers, triggers, 1);
	      if (error != NO_ERROR)
		{
		  assert (er_errid () != NO_ERROR);
		  error = er_errid ();
		  goto error_return;
		}
	    }
	}
    }

  AU_ENABLE (save);
  return (error);

error_return:
  if (triggers != NULL)
    {
      tr_free_trigger_list (triggers);
    }

  AU_ENABLE (save);

  assert (er_errid () != NO_ERROR);
  return er_errid ();
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * tr_prepare() - This begins the preparation for trigger evaluation.
 *    It may be called multiple times before calling tr_before().
 *    return: error code
 *    state_p(in): list of triggers to be prepared
 *    triggers(in):
 *
 */
int
tr_prepare (TR_STATE ** state_p, TR_TRIGLIST * triggers)
{
  int error = NO_ERROR;
  TR_STATE *state;
  const char *name;
  int save;

  /*
   * Disable authorization here since trigger scheduling is independent
   * of the current user.  This will only only be necessary if we
   * have to fetch the trigger's owning class for some reason here.
   */
  AU_DISABLE (save);

  /* pass in the first trigger name for the recursion error message */
  name = (triggers != NULL) ? triggers->trigger->name : NULL;
  state = start_state (state_p, name);
  if (state == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
    }
  else
    {
      merge_trigger_list (&state->triggers, triggers, 0);
    }

  AU_ENABLE (save);

  return (error);
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * tr_prepare_class() - This is used to prepare a trigger state for an event
 *                      on a class.
 *    return: error code
 *    state_p(in/out): trigger state pointer
 *    cache(in): class trigger cache
 *    event(in): event being raised
 *
 */
int
tr_prepare_class (TR_STATE ** state_p, TR_SCHEMA_CACHE * cache,
		  DB_TRIGGER_EVENT event)
{
  int error = NO_ERROR;
  TR_STATE *state;
  TR_TRIGLIST *triggers;
  const char *name;
  int save;

  if (!TR_EXECUTION_ENABLED)
    {
      *state_p = NULL;
      return NO_ERROR;
    }
  if (cache == NULL)
    {
      return NO_ERROR;
    }

  /*
   * Disable authorization here since trigger scheduling is independent
   * of the current user.  This will only only be necessary if we
   * have to fetch the trigger's owning class for some reason here.
   */
  AU_DISABLE (save);

  if (tr_validate_schema_cache (cache) != NO_ERROR)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
    }
  else if (event < cache->array_length)
    {
      triggers = cache->triggers[event];

      /* pass in name of first trigger for message */
      name = (triggers != NULL) ? triggers->trigger->name : NULL;
      state = start_state (state_p, name);
      if (state == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	}
      else
	{
	  merge_trigger_list (&state->triggers, triggers, 0);
	}
    }
  else
    {
      assert (false);
    }

  AU_ENABLE (save);
  return (error);
}

/*
 * tr_finish() - The function wraps up the firing of a trigger.
 *    return: none
 *    state(in): trigger execution state
 *
 * Note: The function wraps up the firing of a trigger.
 *    Note that at this point, only triggers containing deferred
 *    conditions or actions remain in the array and the list of
 *    raised triggers.
 */
static void
tr_finish (TR_STATE * state)
{
  /*
   * if we have to touch the trigger class for any reason, we'll
   * need to disable authorization here
   */

  if (state)
    {
      free_state (state);
      --tr_Current_depth;
    }
}

/*
 * tr_abort() - The function aborts the rest of the trigger firing operation.
 *    In addition to freeing the state, this will also cancel anything
 *    on the raised trigger list.
 *    return: none
 *    state(in): trigger execution state
 *
 */
void
tr_abort (TR_STATE * state)
{
  /*
   * if we have to touch the trigger class for any reason, we'll
   * need to disable authorization here
   */

  /* don't really need to do anything other than free the existing state */
  if (state)
    {
      tr_finish (state);
    }
}

/*
 * tr_before_object() - The function tr_before_object should be used
 *                      after tr_prepare.
 *    return: error
 *    state(in): trigger execution state
 *    current(in): current event object
 *    temp(in): temporary event object
 *
 * Note:
 *    See the Note of tr_prepare.
 *    Trigger conditions and actions with execution time
 *    BEFORE are evaluated and executed in tr_before_object other
 *    conditions and actions are handled in tr_after_object.
 *
 */
int
tr_before_object (TR_STATE * state, DB_OBJECT * current, DB_OBJECT * temp)
{
  int error = NO_ERROR;

  if (!TR_EXECUTION_ENABLED)
    {
      return NO_ERROR;
    }
  if (state)
    {
      error = tr_execute_activities (state, TR_TIME_BEFORE, current, temp);
      if (error != NO_ERROR)
	{
	  tr_abort (state);
	}
    }
  return (error);
}

/*
 * tr_before() - The function tr_before should be used after tr_prepare.
 *    return: error code
 *    state(in): trigger execution state
 *
 * Note:
 *    See the Note of tr_prepare.
 *    Trigger conditions and actions with execution time
 *    BEFORE are evaluated and executed in tr_before; other
 *    conditions and actions are handled in tr_after.
 */
int
tr_before (TR_STATE * state)
{
  return tr_before_object (state, NULL, NULL);
}

/*
 * tr_after_object() - The function executes all AFTER conditions and actions,
 *                     and does preparation work for all DEFERRED conditions
 *                     and actions of the triggers fired.
 *    return: error code
 *    state(in): trigger execution state
 *    current(in): current event object
 *    temp(in): temporary event object
 *
 */
int
tr_after_object (TR_STATE * state, DB_OBJECT * current, DB_OBJECT * temp)
{
  int error = NO_ERROR;

  if (!TR_EXECUTION_ENABLED)
    {
      return NO_ERROR;
    }

  if (state)
    {
      error = tr_execute_activities (state, TR_TIME_AFTER, current, temp);

      if (error != NO_ERROR)
	{
	  tr_abort (state);
	}
      else
	{
	  /*
	   * at this point, the only things remaining on the state trigger list
	   * are deferred conditions and actions.  Add them to the end
	   * of the global list.
	   */
	  if (state->triggers != NULL)
	    {
	      add_deferred_activities (state->triggers, current);
	      state->triggers = NULL;
	    }
	  tr_finish (state);
	}
    }
  return (error);
}

/*
 * tr_after() - The function executes all AFTER conditions and actions, and
 *              does preparation work for all DEFERRED conditions and actions
 *              of the triggers fired.
 *    return: error code
 *    state(in): trigger execution state
 *
 */
int
tr_after (TR_STATE * state)
{
  return tr_after_object (state, NULL, NULL);
}

/*
 * tr_check_commit_triggers() - This is called by tm_commit() early in the
 *                              commit sequence.
 *    return: error code
 *    time(in):
 *
 * Note:
 *    This is called by tran_commit() early in the commit sequence.
 *    It will execute any deferred trigger activities.
 *    It may return an error which means that the transaction is
 *    not committable.
 */
int
tr_check_commit_triggers (DB_TRIGGER_TIME time)
{
  int error = NO_ERROR;

  /*
   * If trigger firing has been disabled, do nothing.
   * This is currently used by the loader to disable triggers firing.
   */
  if (!TR_EXECUTION_ENABLED)
    return NO_ERROR;

  /*
   * Do we run the deferred activities before the commit triggers ?
   * If not, the commit trigger can schedule deferred activities as well.
   */

  if (run_user_triggers (TR_EVENT_COMMIT, time))
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  /*
   * if this returns an error, we may wish to override it with a more generic
   * trigger error.
   */
  if (time == TR_TIME_BEFORE)
    {

      if (tr_execute_deferred_activities (NULL, NULL))
	{
	  assert (er_errid () != NO_ERROR);
	  return er_errid ();
	}

      if (tr_Invalid_transaction)
	{
	  error = ER_TR_TRANSACTION_INVALIDATED;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1,
		  tr_Invalid_transaction_trigger);
	}
      else if (tr_Uncommitted_triggers != NULL)
	{
	  /* the things on this list are going to make it */
	  tr_free_trigger_list (tr_Uncommitted_triggers);
	  tr_Uncommitted_triggers = NULL;
	}
    }

  /*
   * clear this flag so we know when triggers are updated during
   * a transaction
   */
  tr_User_triggers_modified = 0;

  return (error);
}

/*
 * tr_check_rollback_triggers() - This is called by tran_abort early
 *                                in the abort sequence.
 *    return: none
 *    time(in):
 *
 * Note:
 *    It will toss out any deferred activities and raise any
 *    triggers on the abort event.
 *    The abort operation itself cannot be rejected.
 *    We also perform some housekeeping here for the user trigger cache.
 */
void
tr_check_rollback_triggers (DB_TRIGGER_TIME time)
{
  TR_TRIGLIST *t, *next;

  /*
   * If trigger firing has been disabled, do nothing.
   * This is currently used by the loader to disable triggers firing.
   */
  if (!TR_EXECUTION_ENABLED)
    return;

  /*
   * Run user triggers FIRST, even if they were created during
   * this transaction.
   */
  (void) run_user_triggers (TR_EVENT_ROLLBACK, time);

  /*
   * can the rollback triggers have deferred activities ? if so
   * need to execute the deferred list now
   */

  /*
   * make sure that any triggers created during this session
   * are removed, especially those on the rollback event itself
   */
  if (tr_Uncommitted_triggers != NULL)
    {
      for (t = tr_Uncommitted_triggers, next = NULL; t != NULL; t = next)
	{
	  next = t->next;
	  /* this will also remove it from the tr_Uncommitted_triggers list */
	  tr_drop_trigger_internal (t->trigger, 1);
	}
      /*
       * shouldn't be necessary if tr_drop_trigger_intenral is doing its job
       */
      tr_free_trigger_list (tr_Uncommitted_triggers);
      tr_Uncommitted_triggers = NULL;
    }

  /* ignore any deferred activities */
  flush_deferred_activities ();

  /* this always gets cleared when the transaction aborts */
  tr_Invalid_transaction = false;

  if (tr_User_triggers_modified)
    {
      tr_User_triggers_valid = 0;
      tr_User_triggers_modified = 0;
    }
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * tr_check_timeout_triggers() - This is called whenever a lock timeout ocurrs.
 *    return: none
 *
 * Note:
 *    The timeout can't be prevented but the user may wish to insert
 *    triggers for side effects.
 */
void
tr_check_timeout_triggers (void)
{
  if (!TR_EXECUTION_ENABLED)
    {
      return;
    }
  (void) run_user_triggers (TR_EVENT_TIMEOUT, TR_TIME_AFTER);
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * tr_check_abort_triggers() - This is called whenever the client is
 *                             unilaterally aborted for some reason.
 *    return: none
 *
 * Note:
 *    This is called whenever the client is unilaterally aborted for
 *    some reason.  This is different than tr_check_rollback_triggers()
 *    because that function is only called if the user voluntarily calls
 *    tran_abort().
 */
void
tr_check_abort_triggers (void)
{
  /*
   * If trigger firing has been disabled, do nothing.
   * This is currently used by the loader to disable triggers firing.
   */
  if (!TR_EXECUTION_ENABLED)
    return;

  (void) run_user_triggers (TR_EVENT_ABORT, TR_TIME_AFTER);

  /*
   * can the abort triggers have deferred activities ? if so
   * need to execute the deferred list now
   */

  /* ignore any deferred activities */
  flush_deferred_activities ();

  /* this always gets cleared when the transaction aborts */
  tr_Invalid_transaction = false;
}

/* DEFERRED ACTIVITY CONTROL */

/*
 * its_deleted() - This is called to look at objects associated with
 *                 deferred triggers to make sure they still exist.
 *    return: non-zero if the object is deleted
 *    object(in): object to examine
 *
 * Note:
 *    Try to determine this as quickly as posible.
 *    This should be a general ws_ functino or something.
 */
static int
its_deleted (DB_OBJECT * object)
{
  int deleted = 0;

  /*
   * Ok, in order for the object to be on the deferred trigger list,
   * it must have existed at the time the trigger was raised.  If it
   * was deleted, the MOP will have the deleted bit set.  The only time
   * we can be referencing a deleted MOP that doesn't have the deleted
   * bit set is if the MOP has never been locked by this transaction.
   * Since we must have locked this object in order to fire a trigger on
   * it, that case isn't possible.
   */

  if (object != NULL)
    {
      /* fast way */
      if (object->decached == 0)
	{
	  deleted = WS_IS_DELETED (object);
	}
      else
	{
	  int error;

	  error = au_fetch_instance_force (object, NULL, AU_FETCH_READ);
	  if (error == ER_HEAP_UNKNOWN_OBJECT)
	    {
	      deleted = 1;
	    }
	}

      /* Slow but safe way */
#if 0
      error = au_fetch_instance_force (object, &mem, AU_FETCH_READ);
      if (error == ER_HEAP_UNKNOWN_OBJECT)
	deleted = 1;
#endif /* 0 */
    }

  return (deleted);
}

/*
 * tr_execute_deferred_activities() - This function executes any deferred
 *                                    activities for a trigger.
 *    return: error code
 *    trigger_object(in): trigger object
 *    target(in): associated target instance
 *
 * Note:
 *    This function executes any deferred activities for a trigger.
 *    If the object argument is NULL, all of the deferred activities for
 *    the given trigger are executed.  If supplied, only those activities
 *    that are associated with the given target object are executed.
 *    If the target argument is NULL, all deferred activities for the
 *    given trigger are executed.  If both arguments are NULL,
 *    all deferred activities are executed unconditionally.
 *
 */
int
tr_execute_deferred_activities (DB_OBJECT * trigger_object,
				DB_OBJECT * target)
{
  int error = NO_ERROR;
  TR_DEFERRED_CONTEXT *c, *c_next;
  TR_TRIGLIST *t, *next;
  TR_TRIGGER *trigger;
  int status;
  bool rejected;

  /*
   * If trigger firing has been disabled, do nothing.
   * This is currently used by the loader to disable trigger firing.
   */
  if (!TR_EXECUTION_ENABLED)
    return NO_ERROR;

  for (c = tr_Deferred_activities, c_next = NULL;
       c != NULL && !error; c = c_next)
    {
      c_next = c->next;

      for (t = c->head, next = NULL; t != NULL && !error; t = next)
	{
	  next = t->next;
	  trigger = t->trigger;

	  if ((trigger_object == NULL || trigger->object == trigger_object)
	      && (target == NULL || t->target == target))
	    {
	      if (its_deleted (t->target))
		{
		  /*
		   * Somewhere along the line, the target object was deleted,
		   * quietly ignore the deferred activity.  If it turns out
		   * that we really want to keep these active, we'll have
		   * to contend with what pt_exec_trigger_stmt is going
		   * to do when we pass it deleted objects.
		   */
		  remove_deferred_activity (c, t);
		}
	      else
		{
		  /*
		   * temporarily restore the recursion level in effect when
		   * this was scheduled
		   */
		  /* Removed: the depth must not be touched since we
		   * keep track of the entire trigger stack to detect recursive
		   * trigger calls. Re-setting the current depth to a (possibly
		   * lower value) could destroy the stack contents. */
		  /* tr_Current_depth = t->recursion_level; */

		  status = execute_activity (trigger, TR_TIME_DEFERRED,
					     t->target, NULL, &rejected);

		  /* execute_activity() maybe include trigger and change the
		   * next pointer. we need get it again.
		   */
		  next = t->next;
		  if (status == TR_RETURN_TRUE)
		    {
		      /* successful processing, remove it from the list */
		      remove_deferred_activity (c, t);

		      /*
		       * reject can't happen here, even if it does,
		       * it is unclear what it would mean
		       */
		    }
		  else if (status == TR_RETURN_ERROR)
		    {
		      /*
		       * if an error happens, should we invalidate
		       * the transaction ?
		       */
		      assert (er_errid () != NO_ERROR);
		      error = er_errid ();
		    }

		  /*
		   * else, thinks the trigger can't be evaluated yet,
		   * shouldn't happen
		   */
		}
	    }
	}

      /*
       * if we deleted all of the deferred triggers in this context, remove
       * the context as well
       */
      if (c->head == NULL)
	{
	  remove_deferred_context (c);
	}
    }

  return (error);
}

/*
 * tr_drop_deferred_activities() - This functio removes any deferred activities
 *                                 for a trigger.
 *    return: error code
 *    trigger_object(in): trigger object
 *    target(in): target object
 *
 * Note:
 *    This functio removes any deferred activities for a trigger.
 *    If the target argument is NULL, all of the deferred activities for
 *    the given trigger are removed.  If supplied, only those
 *    activities associated with the target object are removed.
 */
int
tr_drop_deferred_activities (DB_OBJECT * trigger_object, DB_OBJECT * target)
{
  int error = NO_ERROR;
  TR_DEFERRED_CONTEXT *c, *c_next;
  TR_TRIGLIST *t, *next;

  /* could signal some errors here if the trigger isn't deferrable etc. */

  for (c = tr_Deferred_activities, c_next = NULL; c != NULL && !error;
       c = c_next)
    {
      c_next = c->next;

      for (t = c->head, next = NULL; t != NULL && !error; t = next)
	{
	  next = t->next;

	  if ((trigger_object == NULL || t->trigger->object == trigger_object)
	      && (target == NULL || t->target == target))
	    {
	      if (ws_is_same_object (Au_user, Au_dba_user)
		  || ws_is_same_object (Au_user, t->trigger->owner))
		{
		  remove_deferred_activity (c, t);
		}
	      else
		{
		  error = ER_TR_ACTIVITY_NOT_OWNED;
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
		}
	    }
	}

      if (c->head == NULL)
	{
	  remove_deferred_context (c);
	}
    }

  return (error);
}

/* TRIGGER OBJECT ACCESSORS */

/*
 * tr_trigger_name() - This function finds the name of the input trigger.
 *    return: const char
 *    trigger_object(in): trigger object
 *    name(out):
 *
 * Note:
 *    If access to the internal object that contains the trigger definition
 *    can not be obtained, the trigger cannot be identified or seen
 *    by the user, or the user does not have the SELECT privilege
 *    for the class in the trigger's event target, an error code
 *    will be returned.
 */
int
tr_trigger_name (DB_OBJECT * trigger_object, char **name)
{
  int error = NO_ERROR;
  TR_TRIGGER *trigger;
  int save;

  *name = NULL;
  AU_DISABLE (save);

  trigger = tr_map_trigger (trigger_object, 1);
  if (trigger == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
    }
  else
    {
      *name = ws_copy_string (trigger->name);
    }

  AU_ENABLE (save);
  return (error);
}

/*
 * tr_trigger_status() - This function finds the status of the input trigger.
 *    return: error
 *    trigger_object(in): trigger object
 *    status(in): pointer to the return status of the trigger
 *
 * Note:
 *    If access to the internal object that contains the trigger definition
 *    can not be obtained, the trigger cannot be identified or seen
 *    by the user, or the user does not have the SELECT privilege
 *    for the class in the trigger's event target, an error code
 *    will be returned.
 */
int
tr_trigger_status (DB_OBJECT * trigger_object, DB_TRIGGER_STATUS * status)
{
  int error = NO_ERROR;
  TR_TRIGGER *trigger;
  int save;

  *status = TR_STATUS_INACTIVE;
  AU_DISABLE (save);

  trigger = tr_map_trigger (trigger_object, 1);
  if (trigger == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
    }
  else
    {
      *status = trigger->status;
    }

  AU_ENABLE (save);
  return (error);
}

/*
 * tr_trigger_priority() - This function finds the priority of the
 *                         input trigger.
 *    return: error code
 *    trigger_object(in): trigger object
 *    priority(out): pointer to the return trigger priority
 *
 * Note:
 *    If access to the internal object that contains the trigger definition
 *    can not be obtained, the trigger cannot be identified or seen by
 *    the user, or the user does not have the SELECT privilege for the class
 *    in the trigger's event target, an error code will be returned.
 */
int
tr_trigger_priority (DB_OBJECT * trigger_object, double *priority)
{
  int error = NO_ERROR;
  TR_TRIGGER *trigger;
  int save;

  *priority = TR_LOWEST_PRIORITY;
  AU_DISABLE (save);

  trigger = tr_map_trigger (trigger_object, 1);
  if (trigger == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
    }
  else
    {
      *priority = trigger->priority;
    }

  AU_ENABLE (save);
  return (error);
}

/*
 * tr_trigger_event() - This function finds the event type of
 *                      the input trigger.
 *    return: error code
 *    trigger_object(in): trigger object
 *    event(in): pointer to the return event type of the trigger
 *
 * Note:
 *    If access to the internal object that contains the trigger
 *    definition can not be obtained, the trigger cannot be
 *    identified or seen by the user, or the user does not have the
 *    SELECT privilege for the class in the trigger's event target,
 *    an error code will be returned.
 */
int
tr_trigger_event (DB_OBJECT * trigger_object, DB_TRIGGER_EVENT * event)
{
  int error = NO_ERROR;
  TR_TRIGGER *trigger;
  int save;

  *event = TR_EVENT_NULL;
  AU_DISABLE (save);

  trigger = tr_map_trigger (trigger_object, 1);
  if (trigger == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
    }
  else
    {
      *event = trigger->event;
    }

  AU_ENABLE (save);
  return (error);
}

/*
 * tr_trigger_class() - This function finds the target class of
 *                      the input trigger
 *    return: error code
 *    trigger_object(in): trigger object
 *    class(in): pointer to the return class of the input trigger
 *
 * Note:
 *    A trigger may not have a target class. In this case, the
 *    argument class returns NULL. If access to the internal object
 *    that contains the trigger definition can not be obtained, the
 *    trigger cannot be identified or seen by the user, or the user
 *    does not have the SELECT privilege for the class in the
 *    trigger's event target, an error code will be returned.
 */
int
tr_trigger_class (DB_OBJECT * trigger_object, DB_OBJECT ** class_mop_p)
{
  int error = NO_ERROR;
  TR_TRIGGER *trigger;
  int save;

  *class_mop_p = NULL;
  AU_DISABLE (save);

  trigger = tr_map_trigger (trigger_object, 1);
  if (trigger == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
    }
  else
    {
      *class_mop_p = trigger->class_mop;
    }

  AU_ENABLE (save);
  return (error);
}

/*
 * tr_trigger_attribute() - This function finds the target attribute of
 *                          the input trigger.
 *    return: error code
 *    trigger_object(in): trigger object
 *    attribute(in): pointer to the return target attribute of the trigger
 *
 * Note:
 *    A trigger may not have a target attribute. In this case, the
 *    argument attribute returns NULL. If access to the internal object
 *    that contains the trigger definition can not be obtained, the
 *    trigger cannot be identified or seen by the user, or the user
 *    does not have the SELECT privilege for the class in the
 *    trigger's event target, an error code will be returned.
 */
int
tr_trigger_attribute (DB_OBJECT * trigger_object, char **attribute)
{
  int error = NO_ERROR;
  TR_TRIGGER *trigger;
  int save;

  *attribute = NULL;
  AU_DISABLE (save);

  trigger = tr_map_trigger (trigger_object, 1);
  if (trigger == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
    }
  else
    {
      *attribute = ws_copy_string (trigger->attribute);
    }

  AU_ENABLE (save);
  return (error);
}

/*
 * tr_trigger_condition() - This function finds the condition of
 *                          the input trigger.
 *    return: error code
 *    trigger_object(in): trigger object
 *    condition(out): pointer to the return trigger condition
 *
 * Note:
 *    A trigger may not have a condition. In this case, the argument
 *    condition returns NULL. If access to the internal object that
 *    contains the trigger definition can not be obtained, the
 *    trigger cannot be identified or seen by the user, or the user
 *    does not have the SELECT privilege for the class in the
 *    trigger's event target, an error code will be returned.
 */
int
tr_trigger_condition (DB_OBJECT * trigger_object, char **condition)
{
  int error = NO_ERROR;
  TR_TRIGGER *trigger;
  int save;

  *condition = NULL;
  AU_DISABLE (save);

  trigger = tr_map_trigger (trigger_object, 1);
  if (trigger == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
    }
  else if (trigger->condition != NULL
	   && trigger->condition->type == TR_ACT_EXPRESSION)
    {
      *condition = ws_copy_string (trigger->condition->source);
    }

  AU_ENABLE (save);
  return error;
}

/*
 * tr_trigger_condition_time() - This function finds the execution time of
 *                               the trigger condition
 *    return: error code
 *    trigger_object(in): trigger object
 *    tr_time(in): pointer to the return execution time of
 *                 the trigger condition
 *
 * Note:
 *    Even if the given trigger does not have a
 *    condition, the argument time still returns a default execution
 *    time. If access to the internal object that contains the
 *    trigger definition can not be obtained, the trigger cannot be
 *    identified or seen by the user, or the user does not have the
 *    SELECT privilege for the class in the trigger's event target,
 *    an error code will be returned.
 */
int
tr_trigger_condition_time (DB_OBJECT * trigger_object,
			   DB_TRIGGER_TIME * tr_time)
{
  int error = NO_ERROR;
  TR_TRIGGER *trigger;
  int save;

  *tr_time = TR_TIME_NULL;
  AU_DISABLE (save);

  trigger = tr_map_trigger (trigger_object, 1);
  if (trigger == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
    }
  else if (trigger->condition != NULL)
    {
      *tr_time = trigger->condition->time;
    }

  AU_ENABLE (save);
  return error;
}

/*
 * tr_trigger_action() - This function finds the action of the input trigger.
 *    return: error code
 *    trigger_object(in): trigger object
 *    action(out): pointer to the return trigger action
 *
 * Note:
 *    If access
 *    to the internal object that contains the trigger definition
 *    can not be obtained, the trigger cannot be identified or seen
 *    by the user, or the user does not have the SELECT privilege
 *    for the class in the trigger's event target, an error code
 *    will be returned.
 */
int
tr_trigger_action (DB_OBJECT * trigger_object, char **action)
{
  int error = NO_ERROR;
  TR_TRIGGER *trigger;
  int save;
  char buf[TR_MAX_PRINT_STRING + 32];

  *action = NULL;
  AU_DISABLE (save);

  trigger = tr_map_trigger (trigger_object, 1);
  if (trigger == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
    }
  else if (trigger->action != NULL)
    {
      switch (trigger->action->type)
	{

	case TR_ACT_NULL:
	  /* no condition */
	  break;

	case TR_ACT_EXPRESSION:
	  *action = ws_copy_string (trigger->action->source);
	  break;

	case TR_ACT_REJECT:
	  *action = ws_copy_string ("REJECT");
	  break;

	case TR_ACT_INVALIDATE:
	  *action = ws_copy_string ("INVALIDATE TRANSACTION");
	  break;

	case TR_ACT_PRINT:
	  /* sigh, need a nice adjustable string array package */
	  snprintf (buf, sizeof (buf) - 1, "PRINT '%s'",
		    trigger->action->source);
	  *action = ws_copy_string (buf);
	  break;

	default:
	  /* error ? */
	  break;
	}
    }

  AU_ENABLE (save);
  return (error);
}

/*
 * tr_trigger_action_time() - This function finds the execution time of
 *                            the trigger action.
 *    return: error code
 *    trigger_object(in): trigger object
 *    tr_time(in): pointer to the return execution time of the trigger action
 *
 * Note:
 *    If access to the internal object that contains the trigger
 *    definition can not be obtained, the trigger cannot be
 *    identified or seen by the user, or the user does not have the
 *    SELECT privilege for the class in the trigger's event target,
 *    an error code will be returned.
 */
int
tr_trigger_action_time (DB_OBJECT * trigger_object, DB_TRIGGER_TIME * tr_time)
{
  int error = NO_ERROR;
  TR_TRIGGER *trigger;
  int save;

  *tr_time = TR_TIME_NULL;
  AU_DISABLE (save);

  trigger = tr_map_trigger (trigger_object, 1);
  if (trigger == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
    }
  else if (trigger->action != NULL)
    {
      *tr_time = trigger->action->time;
    }

  AU_ENABLE (save);
  return (error);
}

/*
 * tr_trigger_action_type() - This returns the action type.
 *    return: error code
 *    trigger_object(in): trigger object
 *    type(out): trigger action type
 *
 * Note:
 *    An application would generally call
 *    this first to see if the type is TR_ACT_EXPRESSION and then use
 *    tr_trigger_action to get the expression string.
 */
int
tr_trigger_action_type (DB_OBJECT * trigger_object, DB_TRIGGER_ACTION * type)
{
  int error = NO_ERROR;
  TR_TRIGGER *trigger;
  int save;

  *type = TR_ACT_NULL;
  AU_DISABLE (save);

  trigger = tr_map_trigger (trigger_object, 1);
  if (trigger == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
    }
  else if (trigger->action != NULL)
    {
      *type = trigger->action->type;
    }

  AU_ENABLE (save);
  return (error);
}

/*
 * tr_is_trigger() - This function can be used to test if an object is
 *                   a trigger object.
 *    return: error code
 *    trigger_object(in): trigger object
 *    status(in): return status (non-zero if its a trigger object)
 */
int
tr_is_trigger (DB_OBJECT * trigger_object, int *status)
{
  int error = NO_ERROR;
  DB_OBJECT *tclass, *oclass;

  *status = false;
  tclass = sm_find_class (TR_CLASS_NAME);	/* need to cache this ! */
  oclass = sm_get_class (trigger_object);
  if (tclass == oclass)
    *status = true;

  /* need to properly detect errors on the object accesses */
  return (error);
}


/* TRIGGER MIGRATION SUPPORT */

/*
 * tr_time_as_string() - Returns the ASCII text for the given time constant.
 *    return: const char
 *    time(in): trigger time constant
 */
const char *
tr_time_as_string (DB_TRIGGER_TIME time)
{
  const char *string;

  switch (time)
    {
    case TR_TIME_BEFORE:
      string = "BEFORE";
      break;
    case TR_TIME_AFTER:
      string = "AFTER";
      break;
    case TR_TIME_DEFERRED:
      string = "DEFERRED";
      break;
    default:
      string = "???";
      break;
    }

  return (string);
}

/*
 * tr_event_as_string() - Returns the ASCII representation of an event constant
 *    return: const char
 *    event(in): event constant
 */
const char *
tr_event_as_string (DB_TRIGGER_EVENT event)
{
  const char *string;

  switch (event)
    {
    case TR_EVENT_UPDATE:
      string = "UPDATE";
      break;
    case TR_EVENT_STATEMENT_UPDATE:
      string = "STATEMENT UPDATE";
      break;
    case TR_EVENT_DELETE:
      string = "DELETE";
      break;
    case TR_EVENT_STATEMENT_DELETE:
      string = "STATEMENT DELETE";
      break;
    case TR_EVENT_INSERT:
      string = "INSERT";
      break;
    case TR_EVENT_STATEMENT_INSERT:
      string = "STATEMENT INSERT";
      break;
    case TR_EVENT_ALTER:
      string = "ALTER";
      break;
    case TR_EVENT_DROP:
      string = "DROP";
      break;
    case TR_EVENT_COMMIT:
      string = "COMMIT";
      break;
    case TR_EVENT_ROLLBACK:
      string = "ROLLBACK";
      break;
    case TR_EVENT_ABORT:
      string = "ABORT";
      break;
    case TR_EVENT_TIMEOUT:
      string = "TIMEOUT";
      break;
    case TR_EVENT_NULL:
    case TR_EVENT_ALL:
    default:
      string = "???";
      break;
    }
  return (string);
}

/*
 * tr_status_as_string() - Returns the ASCII representation of a
 *                         trigger status constant.
 *    return: const char *
 *    status(in): status code
 */
const char *
tr_status_as_string (DB_TRIGGER_STATUS status)
{
  const char *string;

  switch (status)
    {
    case TR_STATUS_INVALID:
      string = "INVALID";
      break;
    case TR_STATUS_ACTIVE:
      string = "ACTIVE";
      break;
    case TR_STATUS_INACTIVE:
      string = "INACTIVE";
      break;
    default:
      string = "???";
      break;
    }

  return (string);
}

/*
 * tr_dump_trigger() - This function is used to dump a trigger definition
 *                     in ASCII format so that it can be read and re-defined
 *                     from the SQL/X interpreter.
 *                     It is intended to support the unloaddb/loadbdb
 *                     migration utilities.
 *    return: error code
 *    trigger_object(in): trigger object
 *    fp(in): output file
 *    quoted_id_flag(in):
 */
int
tr_dump_trigger (DB_OBJECT * trigger_object, FILE * fp)
{
  int error = NO_ERROR;
  TR_TRIGGER *trigger;
  DB_TRIGGER_TIME time;
  int save;
  const char *name;

  AU_DISABLE (save);
  trigger = tr_map_trigger (trigger_object, 1);
  if (trigger == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
    }

  /* automatically filter out invalid triggers */
  else if (trigger->status != TR_STATUS_INVALID)
    {

      fprintf (fp, "CREATE TRIGGER ");
      fprintf (fp, "[%s]\n", trigger->name);
      fprintf (fp, "  STATUS %s\n", tr_status_as_string (trigger->status));
      fprintf (fp, "  PRIORITY %f\n", trigger->priority);

      time = TR_TIME_BEFORE;
      if (trigger->condition != NULL)
	{
	  time = trigger->condition->time;
	}
      else if (trigger->action != NULL)
	{
	  time = trigger->action->time;
	}

      /* BEFORE UPDATE etc. */
      fprintf (fp, "  %s %s", tr_time_as_string (time),
	       tr_event_as_string (trigger->event));

      if (trigger->class_mop != NULL)
	{
	  name = db_get_class_name (trigger->class_mop);
	  fprintf (fp, " ON ");
	  fprintf (fp, "[%s]", name);

	  if (trigger->attribute != NULL)
	    {
	      fprintf (fp, "([%s])", trigger->attribute);
	    }
	}
      fprintf (fp, "\n");

      if (trigger->condition != NULL)
	{
	  fprintf (fp, "IF %s\n", trigger->condition->source);
	}

      if (trigger->action != NULL)
	{
	  fprintf (fp, "  EXECUTE ");
	  if (trigger->action->time != time)
	    {
	      fprintf (fp, "%s ", tr_time_as_string (trigger->action->time));
	    }
	  switch (trigger->action->type)
	    {
	    case TR_ACT_EXPRESSION:
	      fprintf (fp, "%s", trigger->action->source);
	      break;
	    case TR_ACT_REJECT:
	      fprintf (fp, "REJECT");
	      break;
	    case TR_ACT_INVALIDATE:
	      fprintf (fp, "INVALIDATE TRANSACTION");
	      break;
	    case TR_ACT_PRINT:
	      fprintf (fp, "PRINT '%s'", trigger->action->source);
	      break;
	    default:
	      fprintf (fp, "???");
	      break;
	    }
	}
      fprintf (fp, ";\n");
    }

  AU_ENABLE (save);
  return error;
}

/*
 * get_user_name() - Shorthand function for getting the user name out of a user
 *                   object.  The name is stored in a static array so
 *                   we don't have to worry about freeing it.
 *    return: user name
 *    user(in): user object
 */
static char *
get_user_name (DB_OBJECT * user)
{
  DB_VALUE value;
  char *tmp;

  if (db_get (user, "name", &value))
    {				/* error */
      strcpy (namebuf, "???");
      return namebuf;
    }

  if (DB_VALUE_TYPE (&value) != DB_TYPE_STRING
      || DB_IS_NULL (&value) || DB_GET_STRING (&value) == NULL)
    {
      strcpy (namebuf, "???");
    }
  else
    {
      tmp = DB_GET_STRING (&value);
      if (tmp)
	{
	  strncpy (namebuf, tmp, sizeof (namebuf) - 1);
	}
      namebuf[MAX_USER_NAME - 1] = '\0';
    }
  db_value_clear (&value);

  return namebuf;
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * tr_dump_all_triggers() - This is intended to support the unloaddb/loaddb
 *                          utilities.
 *    return: error code
 *    fp(in): output file
 *    quoted_id_flag(in):
 *
 * Note:
 *    This is intended to support the unloaddb/loaddb utilities.
 *    It dumps a SQL/X script that can be used to regenerate all
 *    of the currently defined triggers.
 *    It uses the login() method without passwords and as such assumes
 *    that we are running as the 'DBA' user.
 *    NOTE: Do not dump triggers if they are defined on one
 *    of the system classes.  These are defined as part of "createdb" and
 *    must not be emitted in the unloaddb schema file.
 *    This does however prevent users from defining their own triggers
 *    on the system classes but this isn't much of a limitation since
 *    users can't alter the system classes in any other way.
 */
int
tr_dump_all_triggers (FILE * fp, bool quoted_id_flag)
{
  int error = NO_ERROR;
  TR_TRIGGER *trigger;
  DB_SET *table;
  DB_VALUE value;
  DB_OBJECT *trigger_object;
  int max, i;

  if (Au_root != NULL
      && (error = obj_get (Au_root, "triggers", &value)) == NO_ERROR)
    {
      if (DB_IS_NULL (&value))
	{
	  table = NULL;
	}
      else
	{
	  table = DB_GET_SET (&value);
	}
      if (table != NULL)
	{
	  error = set_filter (table);
	  max = set_size (table);
	  for (i = 1; i < max && error == NO_ERROR; i += 2)
	    {
	      if ((error = set_get_element (table, i, &value)) == NO_ERROR)
		{
		  if (DB_VALUE_TYPE (&value) == DB_TYPE_OBJECT
		      && !DB_IS_NULL (&value)
		      && DB_GET_OBJECT (&value) != NULL)
		    {
		      trigger_object = DB_GET_OBJECT (&value);
		      trigger = tr_map_trigger (trigger_object, 1);
		      if (trigger == NULL)
			{
			  assert (er_errid () != NO_ERROR);
			  error = er_errid ();
			}
		      else
			{
			  /* don't dump system class triggers */
			  if (trigger->class_mop == NULL
			      || !sm_is_system_class (trigger->class_mop))
			    {

			      if (trigger->status != TR_STATUS_INVALID)
				{
				  tr_dump_trigger (trigger_object, fp,
						   quoted_id_flag);
				  fprintf (fp,
					   "call [change_trigger_owner]('%s',"
					   " '%s') on class [db_root];\n\n",
					   trigger->name,
					   get_user_name (trigger->owner));
				}
			    }
			}
		    }
		}
	    }
	  set_free (table);
	}
    }

  return error;
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * is_required_trigger() -
 *    return: int
 *    trigger(in):
 *    classes(in):
 */
static int
is_required_trigger (TR_TRIGGER * trigger, DB_OBJLIST * classes)
{
  DB_OBJLIST *cl;

  for (cl = classes; cl != NULL; cl = cl->next)
    {
      if (trigger->class_mop == cl->op)
	{
	  return 1;
	}
    }

  return 0;
}

/*
 * tr_dump_selective_triggers() -
 *    return: error code
 *    fp(in):
 *    quoted_id_flag(in):
 *    classes(in):
 */
int
tr_dump_selective_triggers (FILE * fp, DB_OBJLIST * classes)
{
  int error = NO_ERROR;
  TR_TRIGGER *trigger;
  DB_SET *table;
  DB_VALUE value;
  DB_OBJECT *trigger_object;
  int max, i;

  if (Au_root != NULL
      && (error = obj_get (Au_root, "triggers", &value)) == NO_ERROR)
    {
      if (DB_IS_NULL (&value))
	{
	  table = NULL;
	}
      else
	{
	  table = DB_GET_SET (&value);
	}

      if (table != NULL)
	{
	  error = set_filter (table);
	  max = set_size (table);
	  for (i = 1; i < max && error == NO_ERROR; i += 2)
	    {
	      error = set_get_element (table, i, &value);
	      if (error == NO_ERROR)
		{
		  if (DB_VALUE_TYPE (&value) == DB_TYPE_OBJECT
		      && !DB_IS_NULL (&value)
		      && DB_GET_OBJECT (&value) != NULL)
		    {
		      trigger_object = DB_GET_OBJECT (&value);
		      trigger = tr_map_trigger (trigger_object, 1);
		      if (trigger == NULL)
			{
			  assert (er_errid () != NO_ERROR);
			  error = er_errid ();
			}
		      else
			{
			  if (trigger->class_mop != NULL
			      && !is_required_trigger (trigger, classes))
			    {
			      continue;
			    }

			  /* don't dump system class triggers */
			  if (trigger->class_mop == NULL
			      || !sm_is_system_class (trigger->class_mop))
			    {
			      if (trigger->status != TR_STATUS_INVALID)
				{
				  tr_dump_trigger (trigger_object, fp);
				  fprintf (fp,
					   "call [change_trigger_owner]('%s',"
					   " '%s') on class [db_root];\n\n",
					   trigger->name,
					   get_user_name (trigger->owner));
				}
			    }
			}
		    }
		}
	    }
	  set_free (table);
	}
    }

  return error;
}


/* TRIGGER ALTER OPERATIONS */

/*
 * tr_rename_trigger() - Renames a trigger.
 *                       The new name cannot already be in use.
 *    return: error code
 *    trigger_object(in): trigger object
 *    name(in): new trigger name
 *    call_from_api(in): call from api
 */
int
tr_rename_trigger (DB_OBJECT * trigger_object, const char *name,
		   bool call_from_api)
{
  int error = NO_ERROR;
  TR_TRIGGER *trigger;
  DB_VALUE value;
  char *newname, *oldname;
  char *tr_name = NULL;
  int save;

  /* Do we need to disable authorizationjust for check_authorization ? */
  AU_DISABLE (save);

  trigger = tr_map_trigger (trigger_object, true);
  if (trigger == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
    }
  else
    {
      tr_name = strdup (trigger->name);
    }

  if (trigger == NULL)
    ;
  else if (!check_authorization (trigger, true))
    {
      error = ER_TR_TRIGGER_ALTER_FAILURE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, trigger->name);
    }
  else
    {
      newname = tr_process_name (name);
      if (newname == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	}
      else
	{
	  error = trigger_table_rename (trigger_object, newname);
	  /* might need to abort the transaction here */
	  if (!error)
	    {
	      oldname = trigger->name;
	      trigger->name = newname;
	      db_make_string (&value, newname);
	      newname = NULL;
	      if (db_put_internal (trigger_object, TR_ATT_NAME, &value))
		{
		  assert (er_errid () != NO_ERROR);
		  error = er_errid ();
		  /*
		   * hmm, couldn't set the new name, put the old one back,
		   * we might need to abort the transaction here ?
		   */
		  newname = trigger->name;
		  trigger->name = oldname;
		  /* if we can't do this, the transaction better abort */
		  (void) trigger_table_rename (trigger_object, oldname);
		  oldname = NULL;
		}
	      if (oldname != NULL)
		{
		  free_and_init (oldname);
		}
	    }
	  if (newname != NULL)
	    {
	      free_and_init (newname);
	    }
	}
    }

  AU_ENABLE (save);

  if (tr_name)
    {
      free_and_init (tr_name);
    }

  return (error);
}

/*
 * tr_set_status() - This changes a trigger status.
 *    return: error code
 *    trigger_object(in): trigger object
 *    status(in): new statue
 *    call_from_api(in): call from api
 *
 * Note:
 *    The possible values
 *    are TR_STATUS_ACTIVE and TR_STATUS_INACTIVE.
 *    We reset the associated cache validation flag when the status
 *    changes.
 */
int
tr_set_status (DB_OBJECT * trigger_object, DB_TRIGGER_STATUS status,
	       bool call_from_api)
{
  int error = NO_ERROR;
  TR_TRIGGER *trigger;
  DB_TRIGGER_STATUS oldstatus;
  DB_VALUE value;
  int save;

  AU_DISABLE (save);

  trigger = tr_map_trigger (trigger_object, 1);
  if (trigger == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
    }
  else if (!check_authorization (trigger, true))
    {
      error = ER_TR_TRIGGER_ALTER_FAILURE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, trigger->name);
    }
  else
    {
      oldstatus = trigger->status;
      trigger->status = status;
      db_make_int (&value, status);
      if (db_put_internal (trigger_object, TR_ATT_STATUS, &value))
	{
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	  /*
	   * hmm, couldn't set the new status, put the old one back,
	   * we might need to abort the transaction here ?
	   */
	  trigger->status = oldstatus;
	}
      else
	{
	  /*
	   * The schema manager maintains a flag indicating whether
	   * active triggers are defined for the class.  When the
	   * status of a trigger changes, we need to tell the schema
	   * manager to recalculate this flag.
	   */
	  if (trigger->class_mop != NULL)
	    {
	      error = sm_invalidate_trigger_cache (trigger->class_mop);
	    }

	  /*
	   * If this is a user trigger, the status will be checked
	   * by run_user_triggers so the user cache list doesn't have
	   * to be recalculated.
	   */
	}
    }

  AU_ENABLE (save);

  return error;
}

/*
 * tr_set_priority() - This changes a trigger priority.
 *    The associated trigger caches are rebuilt to reflect the change
 *    in priority.
 *    return: error code
 *    trigger_object(in): trigger object
 *    priority(in): new priority
 *    call_from_api(in): call from api
 */
int
tr_set_priority (DB_OBJECT * trigger_object, double priority,
		 bool call_from_api)
{
  int error = NO_ERROR;
  TR_TRIGGER *trigger;
  double oldpri;
  DB_VALUE value;
  int save;

  AU_DISABLE (save);

  trigger = tr_map_trigger (trigger_object, 1);
  if (trigger == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
    }
  else if (!check_authorization (trigger, true))
    {
      error = ER_TR_TRIGGER_ALTER_FAILURE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, trigger->name);
    }
  else
    {
      oldpri = trigger->priority;
      trigger->priority = priority;
      db_make_double (&value, priority);
      if (db_put_internal (trigger_object, TR_ATT_PRIORITY, &value))
	{
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	  /*
	   * hmm, couldn't set the new status, put the old one back,
	   * we might need to abort the transaction here ?
	   */
	  trigger->priority = oldpri;
	}
      else
	{
	  if (trigger->class_mop != NULL)
	    {
	      /*
	       * its a class trigger, find all the caches that point
	       * to this trigger and cause them to be re-ordered
	       */
	      reorder_schema_caches (trigger);
	    }
	  else
	    {
	      /* this was a user trigger, rebuild the cache */
	      tr_update_user_cache ();
	    }
	}
    }

  AU_ENABLE (save);

  return error;
}


/* TRIGGER PARAMETERS */

/*
 * tr_get_depth() - This returns the maximum call depth allowed
 *                  for nested triggers.
 *    return: depth
 *
 * Note:
 *    A negative value indicates infinite depth.
 *    This can be used to prevent infinite loops in recursive trigger
 *    definitions.
 */
int
tr_get_depth (void)
{
  return tr_Maximum_depth;
}

/*
 * tr_set_depth()
 *    return: error code
 *    depth(in):
 */
int
tr_set_depth (int depth)
{
  if (depth > TR_MAX_RECURSION_LEVEL || depth < 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TR_MAX_DEPTH_TOO_BIG, 1,
	      TR_MAX_RECURSION_LEVEL);
      return ER_TR_MAX_DEPTH_TOO_BIG;
    }

  tr_Maximum_depth = depth;
  return NO_ERROR;
}

/*
 * tr_get_trace() - This is used to access the trigger trace flag.
 *    return: trace
 *
 * Note:
 *    Setting the trace flag to a non-zero value will enable the display
 *    of trace messages to the standard output device.
 */
int
tr_get_trace (void)
{
  return tr_Trace;
}

/*
 * tr_set_trace()
 *    return: error code
 *    trace(in):
 */
int
tr_set_trace (bool trace)
{
  tr_Trace = trace;
  return NO_ERROR;
}


/* TRIGGER MODULE CONTROL */

/*
 * tr_init()
 *    return: none
 */
void
tr_init (void)
{
  tr_Current_depth = 0;
  tr_Maximum_depth = TR_MAX_RECURSION_LEVEL;
  tr_Invalid_transaction = false;
  tr_Deferred_activities = NULL;
  tr_Deferred_activities_tail = NULL;
  tr_User_triggers_valid = 0;
  tr_User_triggers_modified = 0;
  tr_User_triggers = NULL;
  tr_Trace = false;
  tr_Uncommitted_triggers = NULL;
  tr_Invalid_transaction_trigger[0] = '\0';
  tr_Schema_caches = NULL;

  /* create the object map */
  tr_object_map = mht_create ("Trigger object map", TR_EST_MAP_SIZE,
			      mht_ptrhash, mht_compare_ptrs_are_equal);
}

/* Helper routine for tr_final */
/*
 * map_flush_helper() -
 *    return: int
 *    key(in):
 *    data(in):
 *    args(in):
 */
static int
map_flush_helper (const void *key, void *data, void *args)
{
  if (data != NULL)
    {
      free_trigger ((TR_TRIGGER *) data);
    }
  return (NO_ERROR);
}


/*
 * tr_final() - Trigger shutdown function.
 *    return: none
 */
void
tr_final (void)
{
  flush_deferred_activities ();
  tr_free_trigger_list (tr_User_triggers);
  tr_free_trigger_list (tr_Uncommitted_triggers);
  tr_User_triggers = NULL;
  tr_Uncommitted_triggers = NULL;

  /* need to free all trigger structures in the table */
  if (tr_object_map != NULL)
    {
      mht_map (tr_object_map, map_flush_helper, NULL);
      mht_destroy (tr_object_map);
      tr_object_map = NULL;
    }

  /*
   * Don't need to explicitly free these now since
   * they are allocated in the workspace.  May need to change this.
   */
  tr_Schema_caches = NULL;
}

/*
 * tr_dump() - Dump status of the trigger manager.
 *    return: none
 *    fpp(in): ouput file
 *
 * Note:
 *    Mostly this is used to get a list of the currently deferred
 *    activities for debugging purposes.
 *    Since this list is likely to be generally useful, we should
 *    make this available in the API interface as well.
 *    Possibly a session command too.
 */
void
tr_dump (FILE * fpp)
{
  TR_DEFERRED_CONTEXT *c;
  TR_TRIGLIST *t;

  fprintf (fpp, "TRIGGER MANAGER STATISTICS\n");

  fprintf (fpp, "Trigger execution state : %s\n",
	   TR_EXECUTION_ENABLED ? "ENABLED" : "DISABLED");
  if (tr_Deferred_activities == NULL)
    {
      fprintf (fpp, "No deferred triggers.\n");
    }
  else
    {
      fprintf (fpp, "Deferred trigger list:\n");
      for (c = tr_Deferred_activities; c != NULL; c = c->next)
	{
	  for (t = c->head; t != NULL; t = t->next)
	    fprintf (fpp, "  %s\n", t->trigger->name);
	}
    }
}


/*
 * TRIGGER DATABASE INSTALLATION
 */

/*
 * define_trigger_classes() - This defines the classes necessary for
 *                            storing triggers.
 *    return: error code
 *
 * Note:
 *    Currently there is only a single trigger object class.
 *    This should only be called during createdb.
 */
static int
define_trigger_classes (void)
{
  DB_CTMPL *tmp;
  DB_OBJECT *class_mop;
  DB_VALUE value;

  tmp = NULL;

  if ((tmp = dbt_create_class (TR_CLASS_NAME)) == NULL)
    goto tmp_error;

  if (dbt_add_attribute (tmp, TR_ATT_OWNER, "db_user", NULL))
    goto tmp_error;

  if (dbt_add_attribute (tmp, TR_ATT_NAME, "string", NULL))
    goto tmp_error;

  db_make_int (&value, TR_STATUS_ACTIVE);
  if (dbt_add_attribute (tmp, TR_ATT_STATUS, "integer", &value))
    goto tmp_error;

  db_make_float (&value, TR_LOWEST_PRIORITY);
  if (dbt_add_attribute (tmp, TR_ATT_PRIORITY, "double", &value))
    goto tmp_error;

  db_make_int (&value, TR_EVENT_NULL);
  if (dbt_add_attribute (tmp, TR_ATT_EVENT, "integer", &value))
    goto tmp_error;

  if (dbt_add_attribute (tmp, TR_ATT_CLASS, "object", NULL))
    goto tmp_error;

  if (dbt_add_attribute (tmp, TR_ATT_ATTRIBUTE, "string", NULL))
    goto tmp_error;

  db_make_int (&value, 0);
  if (dbt_add_attribute (tmp, TR_ATT_CLASS_ATTRIBUTE, "integer", &value))
    goto tmp_error;

  if (dbt_add_attribute (tmp, TR_ATT_CONDITION_TYPE, "integer", NULL))
    goto tmp_error;

  if (dbt_add_attribute (tmp, TR_ATT_CONDITION, "string", NULL))
    goto tmp_error;

  db_make_int (&value, TR_TIME_AFTER);
  if (dbt_add_attribute (tmp, TR_ATT_CONDITION_TIME, "integer", NULL))
    goto tmp_error;

  if (dbt_add_attribute (tmp, TR_ATT_ACTION_TYPE, "integer", NULL))
    goto tmp_error;

  if (dbt_add_attribute (tmp, TR_ATT_ACTION, "string", NULL))
    goto tmp_error;

  db_make_int (&value, TR_TIME_AFTER);
  if (dbt_add_attribute (tmp, TR_ATT_ACTION_TIME, "integer", NULL))
    goto tmp_error;

  if ((class_mop = dbt_finish_class (tmp)) == NULL)
    goto tmp_error;

  if (locator_create_heap_if_needed (class_mop, true) == NULL)
    {
      goto tmp_error;
    }

  return NO_ERROR;

tmp_error:
  if (tmp != NULL)
    {
      dbt_abort_class (tmp);
    }

  assert (er_errid () != NO_ERROR);
  return er_errid ();
}

/*
 * tr_install() - Trigger installation function.
 *    return: error code
 *
 * Note:
 *    A system class called TRIGGER is created, and initialized.
 *    The function should be called exactly once in createdb.
 */
int
tr_install (void)
{
  return (define_trigger_classes ());
}

/*
 * tr_get_execution_state() - Returns the current trigger execution state.
 *    return: bool
 */
bool
tr_get_execution_state (void)
{
  return (tr_Execution_enabled);
}

/*
 * tr_set_execution_state() - Returns the previous trigger firing enabled
 *                            state.
 *    return: bool
 *    new_state(in): bool
 *              true : enables trigger firing state
 *              false : disables trigger firing state
 */
bool
tr_set_execution_state (bool new_state)
{
  bool old_state = tr_Execution_enabled;
  tr_Execution_enabled = new_state;
  return (old_state);
}

const char *
tr_get_class_name (void)
{
  return TR_CLASS_NAME;
}

#if defined(ENABLE_UNUSED_FUNCTION)

/*
 * tr_downcase_all_trigger_info() -/
 *    return: int
 */
int
tr_downcase_all_trigger_info (void)
{
  DB_OBJLIST *list, *mop;
  MOP class_mop, obj;
  DB_VALUE value;
  char *attribute;

  class_mop = sm_find_class (TR_CLASS_NAME);
  if (class_mop == NULL)
    {
      return ER_FAILED;
    }

  list = sm_fetch_all_objects (class_mop, DB_FETCH_QUERY_WRITE);
  if (list == NULL)
    {
      return ER_FAILED;
    }

  for (mop = list; mop != NULL; mop = mop->next)
    {
      obj = mop->op;
      if (obj_get (obj, "target_attribute", &value) != NO_ERROR)
	break;

      if (!DB_IS_NULL (&value))
	{
	  attribute = DB_GET_STRING (&value);
	  sm_downcase_name (attribute, attribute, SM_MAX_IDENTIFIER_LENGTH);
	  if (obj_set (obj, "target_attribute", &value) != NO_ERROR)
	    break;
	  ws_dirty (obj);
	}
    }
  ml_ext_free (list);
  return ((mop == NULL) ? NO_ERROR : ER_FAILED);
}
#endif /* ENABLE_UNUSED_FUNCTION */
