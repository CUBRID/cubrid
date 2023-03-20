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
 * trigger_description.hpp
 */

#ifndef _TRIGGER_DESCRIPTION_HPP_
#define _TRIGGER_DESCRIPTION_HPP_

#if defined(SERVER_MODE)
#error Does not belong to server module
#endif //defined(SERVER_MODE)

#include <stdio.h>
#include "dbtype_def.h"
#include "extract_schema.hpp"
struct db_object;
class print_output;

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

int tr_dump_trigger (extract_context &ctxt, print_output &output_ctx, DB_OBJECT *trigger_object);
int tr_dump_selective_triggers (extract_context &ctxt, print_output &output_ctx, DB_OBJLIST *classes);

#endif // _TRIGGER_DESCRIPTION_HPP_
