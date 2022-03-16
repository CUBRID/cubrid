/*
 * Copyright 2008 Search Solution Corporation
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
 * trigger_description.cpp
 */

#include "trigger_description.hpp"

#include "authenticate.h"
#include "dbi.h"
#include "dbtype_function.h"
#include "object_accessor.h"
#include "object_print.h"
#include "object_printer.hpp"
#include "object_print_util.hpp"
#include "printer.hpp"
#include "set_object.h"
#include "schema_manager.h"
#include "trigger_manager.h"
#include "work_space.h"

/* safe string free */
#define STRFREE_W(string)                               \
  if (string != NULL) db_string_free((char *) (string))

static int is_required_trigger (TR_TRIGGER *trigger, DB_OBJLIST *classes);
static char *get_user_name (DB_OBJECT *user);

trigger_description::trigger_description ()
  : name (0)
  , event (0)
  , class_name (0)
  , attribute (0)
  , full_event (0)
  , status (0)
  , priority (0)
  , condition_time (0)
  , condition (0)
  , action_time (0)
  , action (0)
  , comment (0)
{
}

int trigger_description::init (const char *name)
{
  struct db_object *trobj = tr_find_trigger (name);

  if (trobj == NULL)
    {
      return ER_FAILED;
    }

  return init (trobj);
}

int trigger_description::init (struct db_object *trobj)
{
  assert (trobj != NULL);

  char *condition = NULL, *action = NULL, *classname;
  TR_TRIGGER *trigger;

  trigger = tr_map_trigger (trobj, 1);
  if (trigger == NULL)
    {
      return ER_FAILED;
    }

  /* even though we have the trigger, use these to get the expressions translated into a simple string */
  if (db_trigger_condition (trobj, &condition) != NO_ERROR)
    {
      ws_free_string (condition);
      return ER_FAILED;
    }
  if (db_trigger_action (trobj, &action) != NO_ERROR)
    {
      ws_free_string (action);
      return ER_FAILED;
    }

  /* copy these */
  this->name = object_print::copy_string (trigger->name);
  this->attribute = object_print::copy_string (trigger->attribute);
  this->comment = object_print::copy_string (trigger->comment);

  /* these are already copies */
  this->condition = condition;
  this->action = action;

  /* these are constant strings that don't need to ever change */
  this->event = tr_event_as_string (trigger->event);
  this->condition_time = object_printer::describe_trigger_condition_time (*trigger);
  this->action_time = object_printer::describe_trigger_action_time (*trigger);

  /* only show status if its inactive */
  if (trigger->status != TR_STATUS_ACTIVE)
    {
      this->status = tr_status_as_string (trigger->status);
    }

  /* if its 0, leave it out */
  if (trigger->priority != 0.0)
    {
      char temp_buffer[64];

      sprintf (temp_buffer, "%f", trigger->priority);
      this->priority = object_print::copy_string (temp_buffer);
    }

  if (trigger->class_mop != NULL)
    {
      classname = (char *) sm_get_ch_name (trigger->class_mop);
      if (classname != NULL)
	{
	  this->class_name = object_print::copy_string ((char *) classname);
	}
      else
	{
	  this->class_name = object_print::copy_string ("*** deleted class ***");
	}

      /* format the full event specification so csql can display it without being dependent on syntax */

      char buffer[SM_MAX_IDENTIFIER_LENGTH * 2 + 32];

      if (this->attribute != NULL)
	{
	  sprintf (buffer, "%s ON %s(%s)", this->event, this->class_name, this->attribute);
	}
      else
	{
	  sprintf (buffer, "%s ON %s", this->event, this->class_name);
	}

      this->full_event = object_print::copy_string (buffer);
    }
  else
    {
      /* just make a copy of this so csql can simply use it without thinking */
      this->full_event = object_print::copy_string ((char *) this->event);
    }

  return NO_ERROR;
}

trigger_description::~trigger_description ()
{
  /* these were allocated by this module and can be freed with free_and_init() */
  free (name);
  free (attribute);
  free (class_name);
  free (full_event);
  free (priority);
  if (comment)
    {
      free ((void *) comment);
    }

  /* these were returned by the trigger manager and must be freed with db_string_free() */
  STRFREE_W (condition);
  STRFREE_W (action);
}

/*
 * help_print_trigger () - Debug function, primarily for help_print_info,
 *                         can be useful in the debugger as well.
 *                         Display the description of a trigger to stdout.
 *   return: none
 *   name(in): trigger name
 *   file(in):
 */
void trigger_description::fprint (FILE *file)
{
  if (name != NULL)
    {
      fprintf (file, "Trigger   : %s\n", name);

      if (status != NULL)
	{
	  fprintf (file, "Status    : %s\n", status);
	}

      if (priority != NULL)
	{
	  fprintf (file, "Priority  : %s\n", priority);
	}

      fprintf (file, "Event     : %s %s\n", condition_time, full_event);

      if (condition != NULL)
	{
	  fprintf (file, "Condition : %s\n", condition);
	}

      if (condition_time != action_time)
	{
	  fprintf (file, "Action    : %s %s\n", action_time, action);
	}
      else
	{
	  fprintf (file, "Action    : %s\n", action);
	}

      if (comment != NULL)
	{
	  fprintf (file, "Comment '%s'\n", comment);
	}
    }
}

/*
 * tr_dump_trigger() - This function is used to dump a trigger definition in ASCII format so that it can be read and
 * 		       re-defined from the csql interpreter.
 *                     It is intended to support the unloaddb/loadbdb migration utilities.
 *    return: error code
 *    output_ctx(in/out): output context
 *    trigger_object(in): trigger object
 *    quoted_id_flag(in):
 */
int
tr_dump_trigger (print_output &output_ctx, DB_OBJECT *trigger_object)
{
  int error = NO_ERROR;
  TR_TRIGGER *trigger;
  DB_TRIGGER_TIME time;
  int save;
  const char *name;
  char owner_name[DB_MAX_USER_LENGTH] = { '\0' };
  const char *trigger_name = NULL;
  const char *class_name = NULL;

  AU_DISABLE (save);

  trigger = tr_map_trigger (trigger_object, 1);

  if (trigger == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
    }
  else if (trigger->status != TR_STATUS_INVALID)
    {
      /* automatically filter out invalid triggers */
      output_ctx ("CREATE TRIGGER ");
      output_ctx ("[%s]\n", sm_remove_qualifier_name (trigger->name));
      output_ctx ("  STATUS %s\n", tr_status_as_string (trigger->status));
      output_ctx ("  PRIORITY %f\n", trigger->priority);

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
      output_ctx ("  %s %s", tr_time_as_string (time), tr_event_as_string (trigger->event));

      if (trigger->class_mop != NULL)
	{
	  name = db_get_class_name (trigger->class_mop);
	  if (sm_qualifier_name (name, owner_name, DB_MAX_USER_LENGTH) == NULL)
	    {
	      ASSERT_ERROR_AND_SET (error);
	      return error;
	    }
	  class_name = sm_remove_qualifier_name (name);
	  output_ctx (" ON ");
	  output_ctx ("[%s].[%s]", owner_name, class_name);

	  if (trigger->attribute != NULL)
	    {
	      output_ctx ("([%s])", trigger->attribute);
	    }
	}
      output_ctx ("\n");

      if (trigger->condition != NULL)
	{
	  output_ctx ("IF %s\n", trigger->condition->source);
	}

      if (trigger->action != NULL)
	{
	  output_ctx ("  EXECUTE ");
	  if (trigger->action->time != time)
	    {
	      output_ctx ("%s ", tr_time_as_string (trigger->action->time));
	    }
	  switch (trigger->action->type)
	    {
	    case TR_ACT_EXPRESSION:
	      output_ctx ("%s", trigger->action->source);
	      break;
	    case TR_ACT_REJECT:
	      output_ctx ("REJECT");
	      break;
	    case TR_ACT_INVALIDATE:
	      output_ctx ("INVALIDATE TRANSACTION");
	      break;
	    case TR_ACT_PRINT:
	      output_ctx ("PRINT '%s'", trigger->action->source);
	      break;
	    default:
	      output_ctx ("???");
	      break;
	    }
	}

      if (trigger->comment != NULL && trigger->comment[0] != '\0')
	{
	  output_ctx (" ");
	  help_print_describe_comment (output_ctx, trigger->comment);
	}

      output_ctx (";\n");
    }

  AU_ENABLE (save);
  return error;
}


/*
 * tr_dump_selective_triggers() -
 *    return: error code
 *    output_ctx(in/out):
 *    quoted_id_flag(in):
 *    classes(in):
 */
int
tr_dump_selective_triggers (print_output &output_ctx, DB_OBJLIST *classes)
{
  int error = NO_ERROR;
  TR_TRIGGER *trigger;
  DB_SET *table;
  DB_VALUE value;
  DB_OBJECT *trigger_object;
  int max, i;

  if (Au_root == NULL)
    {
      return NO_ERROR;
    }

  error = obj_get (Au_root, "triggers", &value);
  if (error != NO_ERROR)
    {
      return error;
    }

  if (DB_IS_NULL (&value))
    {
      table = NULL;
    }
  else
    {
      table = db_get_set (&value);
    }

  if (table == NULL)
    {
      return NO_ERROR;
    }

  error = set_filter (table);
  max = set_size (table);
  for (i = 1; i < max && error == NO_ERROR; i += 2)
    {
      error = set_get_element (table, i, &value);
      if (error == NO_ERROR)
	{
	  if (DB_VALUE_TYPE (&value) == DB_TYPE_OBJECT && !DB_IS_NULL (&value) && db_get_object (&value) != NULL)
	    {
	      trigger_object = db_get_object (&value);
	      trigger = tr_map_trigger (trigger_object, 1);
	      if (trigger == NULL)
		{
		  ASSERT_ERROR_AND_SET (error);
		}
	      else
		{
		  int is_system_class = 0;

		  if (trigger->class_mop != NULL && !is_required_trigger (trigger, classes))
		    {
		      continue;
		    }

		  /* don't dump system class triggers */
		  if (trigger->class_mop != NULL)
		    {
		      is_system_class = sm_is_system_class (trigger->class_mop);
		    }
		  if (is_system_class == 0)
		    {
		      if (trigger->status != TR_STATUS_INVALID)
			{
			  tr_dump_trigger (output_ctx, trigger_object);
			  output_ctx ("call [change_trigger_owner]('%s'," " '%s') on class [db_root];\n\n",
				      sm_remove_qualifier_name (trigger->name), get_user_name (trigger->owner));
			}
		    }
		  else if (is_system_class < 0)
		    {
		      error = is_system_class;
		    }
		}
	    }
	}
    }
  set_free (table);

  return error;
}

/*
 * is_required_trigger() -
 *    return: int
 *    trigger(in):
 *    classes(in):
 */
static int
is_required_trigger (TR_TRIGGER *trigger, DB_OBJLIST *classes)
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
 * get_user_name() - Shorthand function for getting the user name out of a user object.
 *		     The name is stored in a static array so we don't have to worry about freeing it.
 *    return: user name
 *    user(in): user object
 */
static char *
get_user_name (DB_OBJECT *user)
{
#define MAX_USER_NAME 32	/* actually its 8 */

  static char namebuf[MAX_USER_NAME];

  DB_VALUE value;
  const char *tmp;

  if (db_get (user, "name", &value))
    {
      /* error */
      strcpy (namebuf, "???");
      return namebuf;
    }

  if (DB_VALUE_TYPE (&value) != DB_TYPE_STRING || DB_IS_NULL (&value) || db_get_string (&value) == NULL)
    {
      strcpy (namebuf, "???");
    }
  else
    {
      tmp = db_get_string (&value);
      if (tmp)
	{
	  strncpy (namebuf, tmp, sizeof (namebuf) - 1);
	}
      namebuf[MAX_USER_NAME - 1] = '\0';
    }
  db_value_clear (&value);

  return namebuf;

#undef MAX_USER_NAME
}
