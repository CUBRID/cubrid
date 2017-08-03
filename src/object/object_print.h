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

#include "dbtype.h"

#if !defined (SERVER_MODE)
#include "parse_tree.h"
#endif /* !SERVER_MODE */

#ifdef __cplusplus
extern "C"
{
#endif

#if !defined (SERVER_MODE)
#define OBJ_SPRINT_DB_FLOAT(buf, value)       \
  sprintf(buf, "%#.*g", DB_FLOAT_DECIMAL_PRECISION, value)
#define OBJ_SPRINT_DB_DOUBLE(buf, value)      \
  sprintf(buf, "%#.*g", DB_DOUBLE_DECIMAL_PRECISION, value)

/* HELP STRUCTURES */
/*
 * CLASS_HELP
 *
 * Note :
 *    This structure contains information about a class defined in the
 *    database.  This will be built and returned by help_class or
 *    help_class_name.
 */
  typedef struct class_description CLASS_HELP;
  struct class_description
  {
    char *name;
    char *class_type;
    char *collation;
    char **supers;
    char **subs;
    char **attributes;
    char **class_attributes;
    char **methods;
    char **class_methods;
    char **resolutions;
    char **method_files;
    char **query_spec;
    char *object_id;
    char **triggers;
    char **constraints;
    char **partition;
    char *comment;
  };

/*
 * OBJ_HELP
 *
 * Note :
 *    This structure contains information about an instance.  This will
 *    built and returned by help_obj().
 */
  typedef struct object_description OBJ_HELP;
  struct object_description
  {
    char *classname;
    char *oid;
    char **attributes;
    char **shared;
  };

/*
 * TRIGGER_HELP
 *
 * Note :
 *    This structure contains a description of a trigger object.
 */

  typedef struct trigger_description TRIGGER_HELP;
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
  };

  typedef enum obj_print_type
  {
    OBJ_PRINT_CSQL_SCHEMA_COMMAND = 0,
    OBJ_PRINT_SHOW_CREATE_TABLE
  } OBJ_PRINT_TYPE;

/*
 * COMMAND_HELP
 *
 * Note :
 *    Structure containing a description of a command.  Returned by
 *    help_command().
 */

  typedef struct command_description COMMAND_HELP;
  struct command_description
  {
    char *name;
    char **description;
    char **bnf;
    char **example;
  };

/* HELP FUNCTIONS */

/* Class help */
  extern void obj_print_help_free_class (CLASS_HELP * info);
  extern CLASS_HELP *obj_print_help_class (MOP op, OBJ_PRINT_TYPE prt_type);
  extern CLASS_HELP *obj_print_help_class_name (const char *name);

/* Instance help */

  extern OBJ_HELP *help_obj (MOP op);
  extern void help_free_obj (OBJ_HELP * info);


/* Trigger help */

  extern TRIGGER_HELP *help_trigger (DB_OBJECT * trobj);
  extern TRIGGER_HELP *help_trigger_name (const char *name);
  extern void help_free_trigger (TRIGGER_HELP * help);
  extern int help_trigger_names (char ***names_ptr);

/* This can be used to free the class name list or the trigger name list */
  extern void help_free_names (char **names);


/* Class/Instance printing */
  extern void help_fprint_obj (FILE * fp, MOP obj);

/* Class name help */
  extern char **help_class_names (const char *qualifier);
  extern void help_free_class_names (char **names);
  extern void help_fprint_class_names (FILE * fp, const char *qualifier);

/* Misc help */
  extern void help_print_info (const char *command, FILE * fpp);
  extern int help_describe_mop (DB_OBJECT * obj, char *buffer, int maxlen);

  extern void help_print_trigger (const char *name, FILE * fpp);

  extern PARSER_VARCHAR *obj_print_describe_class (const PARSER_CONTEXT * parser, CLASS_HELP * class_schema,
						   DB_OBJECT * class_op);


  extern PARSER_VARCHAR *describe_money (const PARSER_CONTEXT * parser, PARSER_VARCHAR * buffer,
					 const DB_MONETARY * value);
  extern PARSER_VARCHAR *describe_data (const PARSER_CONTEXT * parser, PARSER_VARCHAR * buffer, const DB_VALUE * value);
  extern PARSER_VARCHAR *describe_value (const PARSER_CONTEXT * inparser, PARSER_VARCHAR * buffer,
					 const DB_VALUE * value);
  extern PARSER_VARCHAR *describe_value2 (const PARSER_CONTEXT * inparser, PARSER_VARCHAR * buffer,
					  const DB_VALUE * value);
  extern PARSER_VARCHAR *describe_string (const PARSER_CONTEXT * parser, PARSER_VARCHAR * buffer, const char *str,
					  size_t str_length, int max_token_length);
  extern char *describe_comment (PARSER_CONTEXT * parser, const char *comment);
#endif				/* !SERVER_MODE */

  extern void help_fprint_value (FILE * fp, const DB_VALUE * value);
  extern int help_sprint_value (const DB_VALUE * value, char *buffer, int max_length);
  extern void help_fprint_describe_comment (FILE * fp, const char *comment);

#if defined(CUBRID_DEBUG)
  extern char *dbg_value (const DB_VALUE * value);
#endif

#ifdef __cplusplus
}
#endif

#endif				/* _OBJECT_PRINT_H */
