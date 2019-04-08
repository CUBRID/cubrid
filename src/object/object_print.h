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
class extract_output;

/* HELP FUNCTIONS */

/* Trigger help */
int help_trigger_names (char ***names_ptr);

/* This can be used to free the class name list or the trigger name list */
void help_free_names (char **names);


/* Class/Instance printing */
void help_print_obj (extract_output & output_ctx, MOP obj);

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

void help_print_describe_comment (extract_output & output_ctx, const char *comment);

#endif /* _OBJECT_PRINT_H */
