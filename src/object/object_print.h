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

#include <stdio.h>

#include "dbtype_def.h"
#include "thread_compat.hpp"

#if !defined (SERVER_MODE)
#include "parse_tree.h"
#endif /* !SERVER_MODE */

class string_buffer;

#ifdef __cplusplus
extern "C"
{
#endif

#if !defined (SERVER_MODE)

  struct trigger_description;

/* HELP FUNCTIONS */

/* Trigger help */
  int help_trigger_names (char ***names_ptr);

/* This can be used to free the class name list or the trigger name list */
  void help_free_names (char **names);


/* Class/Instance printing */
  void help_fprint_obj (FILE * fp, MOP obj);

/* Class name help */
  extern char **help_class_names (const char *qualifier);
  extern void help_free_class_names (char **names);

/* Misc help */
  void help_print_info (const char *command, FILE * fpp);
  int help_describe_mop (DB_OBJECT * obj, char *buffer, int maxlen);
#endif				/* !SERVER_MODE */

  void help_fprint_value (THREAD_ENTRY * thread_p, FILE * fp, const DB_VALUE * value);
  void help_sprint_value (const DB_VALUE * value, string_buffer & sb);
  void help_fprint_describe_comment (FILE * fp, const char *comment);

#ifdef __cplusplus
}
#endif

#endif				/* _OBJECT_PRINT_H */
