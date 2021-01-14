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
 * object_print.h - Routines to print dbvalues
 */

#ifndef _OBJECT_PRINT_H
#define _OBJECT_PRINT_H

#ident "$Id$"

#include "dbtype_def.h"

#include <stdio.h>

#if defined(SERVER_MODE)
#error Does not belong to server module
#endif //defined(SERVER_MODE)

struct trigger_description;
class print_output;

/* HELP FUNCTIONS */

/* Trigger help */
int help_trigger_names (char ***names_ptr);

/* This can be used to free the class name list or the trigger name list */
void help_free_names (char **names);


/* Class/Instance printing */
void help_print_obj (print_output & output_ctx, MOP obj);

/* Class name help */
// ctltool uses the functions
#ifdef __cplusplus
extern "C"
{
#endif
  extern char **help_class_names (const char *qualifier);
  extern void help_free_class_names (char **names);
#ifdef __cplusplus
}
#endif

/* Misc help */
void help_print_info (const char *command, FILE * fpp);
int help_describe_mop (DB_OBJECT * obj, char *buffer, int maxlen);

void help_print_describe_comment (print_output & output_ctx, const char *comment);

#endif /* _OBJECT_PRINT_H */
