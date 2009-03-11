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

#include "parser.h"

#if !defined (SERVER_MODE)
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

};

/*
 * DB_HELP_COMMAND
 *
 * Note :
 *    Constants to indicate a command for which help is available.
 */

typedef enum help_commands
{

  DB_HELP_GENERAL = 0,
  DB_HELP_CREATE,
  DB_HELP_ALTER,
  DB_HELP_INSERT,
  DB_HELP_DROP,
  DB_HELP_SELECT,
  DB_HELP_UPDATE,
  DB_HELP_DELETE,
  DB_HELP_COMMIT,
  DB_HELP_ROLLBACK,
  DB_HELP_INDEX,
  DB_HELP_GRANT,
  DB_HELP_REVOKE,
  DB_HELP_CALL,
  DB_HELP_STATISTICS,
  DB_HELP_RENAME,

  DB_HELP_REGISTER,
  DB_HELP_DROPLDB,
  DB_HELP_EXCLUDE,
  DB_HELP_USE,
  DB_HELP_PROXY,

  DB_HELP_GET,
  DB_HELP_SET,
  DB_HELP_TRIGGER,
  DB_HELP_EXECUTE,

  DB_HELP_PARTITION,
  DB_HELP_STORED_PROCEDURE,
  DB_HELP_USER
} DB_HELP_COMMAND;

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
extern CLASS_HELP *obj_print_help_class (MOP op);
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
extern void help_print_obj (MOP obj);

/* Command help */

extern COMMAND_HELP *help_command_name (const char *name);
extern COMMAND_HELP *help_command (DB_HELP_COMMAND cmd);
extern void help_free_command (COMMAND_HELP * help);
extern void help_print_command (DB_HELP_COMMAND cmd);
extern void help_fprint_command (FILE * fp, DB_HELP_COMMAND cmd);

/* Class name help */

extern char **help_class_names (const char *qualifier);
extern char **help_base_class_names (void);
extern void help_free_class_names (char **names);
extern void help_fprint_class_names (FILE * fp, const char *qualifier);
extern void help_print_class_names (const char *qualifier);

/* Misc help */

extern void help_fprint_all_classes (FILE * fp);
extern void help_fprint_resident_instances (FILE * fp, MOP op);
extern void help_print_info (const char *command, FILE * fpp);
extern int help_describe_mop (DB_OBJECT * obj, char *buffer, int maxlen);

extern void help_print_trigger (const char *name, FILE * fpp);

#endif /* !SERVER_MODE */

extern PARSER_VARCHAR *describe_money (const PARSER_CONTEXT * parser,
				       PARSER_VARCHAR * buffer,
				       const DB_MONETARY * value);
extern PARSER_VARCHAR *describe_data (const PARSER_CONTEXT * parser,
				      PARSER_VARCHAR * buffer,
				      const DB_VALUE * value);
extern PARSER_VARCHAR *describe_value (const PARSER_CONTEXT * inparser,
				       PARSER_VARCHAR * buffer,
				       const DB_VALUE * value);
extern PARSER_VARCHAR *describe_value2 (const PARSER_CONTEXT * inparser,
					PARSER_VARCHAR * buffer,
					const DB_VALUE * value);
extern PARSER_VARCHAR *describe_string (const PARSER_CONTEXT * parser,
					PARSER_VARCHAR * buffer,
					const char *str, size_t str_length,
					int max_token_length);
extern void help_fprint_value (FILE * fp, const DB_VALUE * value);
extern int help_sprint_value (const DB_VALUE * value, char *buffer,
			      int max_length);
extern char *dbg_value (const DB_VALUE * value);


#endif /* _OBJECT_PRINT_H */
