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
 * trigger_description.cpp
 */

#include "trigger_description.hpp"

#include "dbi.h"
#include "object_printer.hpp"
#include "object_print_util.hpp"
#include "schema_manager.h"
#include "trigger_manager.h"
#include "work_space.h"

/* safe string free */
#define STRFREE_W(string)                               \
  if (string != NULL) db_string_free((char *) (string))

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

      char buffer[ (SM_MAX_IDENTIFIER_LENGTH * 2) + 32];

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
