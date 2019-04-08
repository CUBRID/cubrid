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
 * trigger_description.hpp
 */

#ifndef _TRIGGER_DESCRIPTION_HPP_
#define _TRIGGER_DESCRIPTION_HPP_

#if defined(SERVER_MODE)
#error Does not belong to server module
#endif //defined(SERVER_MODE)

#include <stdio.h>
#include "dbtype_def.h"
struct db_object;
class extract_output;

/*
 * TRIGGER_HELP
 *
 * Note :
 *    This structure contains a description of a trigger object.
 */
struct trigger_description
{
  char *name;
  const char *event;
  char *class_name;
  char *attribute;
  char *full_event;
  const char *status;
  char *priority;
  const char *condition_time;
  char *condition;
  const char *action_time;
  char *action;
  const char *comment;

  trigger_description ();                         //former obj_print_make_trigger_help()
  ~trigger_description ();                        //former help_free_trigger()

  int init (const char *name);                    //former help_trigger_name()
  int init (struct db_object *trobj);             //former help_trigger()

  void fprint (FILE *file);
};

int tr_dump_trigger (extract_output &output_ctx, DB_OBJECT *trigger_object);
int tr_dump_selective_triggers (extract_output &output_ctx, DB_OBJLIST *classes);

#endif // _TRIGGER_DESCRIPTION_HPP_
