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
 * object_print.c - Routines to print dbvalues
 */

#ident "$Id$"

#include "config.h"

#include <stdlib.h>
#include <float.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

#include "porting.h"
#include "chartype.h"
#include "misc_string.h"

#include "error_manager.h"
#include "memory_alloc.h"
#include "class_object.h"
#include "schema_manager.h"
#include "locator_cl.h"
#include "object_accessor.h"
#include "db.h"
#include "object_print.h"
#include "set_object.h"
#include "trigger_manager.h"
#include "virtual_object.h"
#include "message_catalog.h"
#include "parser.h"
#include "statistics.h"
#include "server_interface.h"
#include "execute_schema.h"
#include "class_object.h"
#include "network_interface_cl.h"
#include "transaction_cl.h"

#include "dbtype.h"
#include "language_support.h"
#include "string_opfunc.h"
#include "dbval.h"		/* this must be the last header file included!!! */

#if !defined(SERVER_MODE)
/*
 * Message id in the set MSGCAT_SET_HELP
 * in the message catalog MSGCAT_CATALOG_CUBRID (file cubrid.msg).
 */
#define MSGCAT_HELP_ROOTCLASS_TITLE     (1)
#define MSGCAT_HELP_CLASS_TITLE         (2)
#define MSGCAT_HELP_SUPER_CLASSES       (3)
#define MSGCAT_HELP_SUB_CLASSES         (4)
#define MSGCAT_HELP_ATTRIBUTES          (5)
#define MSGCAT_HELP_METHODS             (6)
#define MSGCAT_HELP_CLASS_ATTRIBUTES    (7)
#define MSGCAT_HELP_CLASS_METHODS       (8)
#define MSGCAT_HELP_RESOLUTIONS         (9)
#define MSGCAT_HELP_METHOD_FILES        (10)
#define MSGCAT_HELP_QUERY_SPEC          (11)
#define MSGCAT_HELP_OBJECT_TITLE        (12)
#define MSGCAT_HELP_CMD_DESCRIPTION     (13)
#define MSGCAT_HELP_CMD_STRUCTURE       (14)
#define MSGCAT_HELP_CMD_EXAMPLE         (15)
#define MSGCAT_HELP_META_CLASS_HEADER   (16)
#define MSGCAT_HELP_CLASS_HEADER        (17)
#define MSGCAT_HELP_VCLASS_HEADER       (18)
#define MSGCAT_HELP_LDB_VCLASS_HEADER   (19)
#define MSGCAT_HELP_GENERAL_TXT         (20)


#if !defined(ER_HELP_INVALID_COMMAND)
#define ER_HELP_INVALID_COMMAND ER_GENERIC_ERROR
#endif /* !ER_HELP_INVALID_COMMAND */

/* safe string free */
#define STRFREE_W(string) \
  if (string != NULL) db_string_free((char *) (string))

#define MATCH_TOKEN(string, token) \
  ((string == NULL) ? 0 : intl_mbs_casecmp(string, token) == 0)

/*
 * STRLIST
 *
 * Note :
 *    Internal structure used for maintaining lists of strings.
 *    Makes it easier to collect up strings before putting them into a
 *    fixed length array.
 *    Could be generalized into a more globally useful utility.
 *
 */

typedef struct strlist
{
  struct strlist *next;
  const char *string;
} STRLIST;

extern unsigned int db_on_server;

/* Constant for routines that have static file buffers */
/* this allows some overhead plus max string length of ESCAPED characters! */
const int MAX_LINE = 4096;

/* maximum lines per section */
const int MAX_LINES = 1024;

#endif /* !SERVER_MODE */
/*
 * help_Max_set_elements
 *
 * description:
 *    Variable to control the printing of runaway sets.
 *    Should be a parameter ?
 */
static int help_Max_set_elements = 20;



static PARSER_VARCHAR *describe_set (const PARSER_CONTEXT * parser, PARSER_VARCHAR * buffer, const DB_SET * set);
static PARSER_VARCHAR *describe_midxkey (const PARSER_CONTEXT * parser, PARSER_VARCHAR * buffer,
					 const DB_MIDXKEY * midxkey);
static PARSER_VARCHAR *describe_double (const PARSER_CONTEXT * parser, PARSER_VARCHAR * buffer, const double value);
static PARSER_VARCHAR *describe_float (const PARSER_CONTEXT * parser, PARSER_VARCHAR * buffer, const float value);
static PARSER_VARCHAR *describe_bit_string (const PARSER_CONTEXT * parser, PARSER_VARCHAR * buffer,
					    const DB_VALUE * value);

#if !defined(SERVER_MODE)
static void obj_print_free_strarray (char **strs);
static char *obj_print_copy_string (const char *source);
static const char **obj_print_convert_strlist (STRLIST * str_list);
static const char *obj_print_trigger_condition_time (TR_TRIGGER * trigger);
static const char *obj_print_trigger_action_time (TR_TRIGGER * trigger);
static PARSER_VARCHAR *obj_print_describe_domain (PARSER_CONTEXT * parser, PARSER_VARCHAR * buffer, TP_DOMAIN * domain,
						  OBJ_PRINT_TYPE prt_type, bool force_print_collation);
static PARSER_VARCHAR *obj_print_identifier (PARSER_CONTEXT * parser, PARSER_VARCHAR * buffer, const char *identifier,
					     OBJ_PRINT_TYPE prt_type);
static char *obj_print_describe_attribute (MOP class_p, PARSER_CONTEXT * parser, SM_ATTRIBUTE * attribute_p,
					   bool is_inherited, OBJ_PRINT_TYPE prt_type, bool force_print_collation);
static char *obj_print_describe_partition_parts (PARSER_CONTEXT * parser, SM_PARTITION * parts,
						 OBJ_PRINT_TYPE prt_type);
static char *obj_print_describe_partition_info (PARSER_CONTEXT * parser, SM_PARTITION * partinfo);
static char *obj_print_describe_constraint (PARSER_CONTEXT * parser, SM_CLASS * class_p,
					    SM_CLASS_CONSTRAINT * constraint_p, OBJ_PRINT_TYPE prt_type);
static PARSER_VARCHAR *obj_print_describe_argument (PARSER_CONTEXT * parser, PARSER_VARCHAR * buffer,
						    SM_METHOD_ARGUMENT * argument_p, OBJ_PRINT_TYPE prt_type);
static PARSER_VARCHAR *obj_print_describe_signature (PARSER_CONTEXT * parser, PARSER_VARCHAR * buffer,
						     SM_METHOD_SIGNATURE * signature_p, OBJ_PRINT_TYPE prt_type);
static PARSER_VARCHAR *obj_print_describe_method (PARSER_CONTEXT * parser, MOP op, SM_METHOD * method,
						  OBJ_PRINT_TYPE prt_type);
static PARSER_VARCHAR *obj_print_describe_resolution (PARSER_CONTEXT * parser, SM_RESOLUTION * resolution_p,
						      OBJ_PRINT_TYPE prt_type);
static PARSER_VARCHAR *obj_print_describe_method_file (PARSER_CONTEXT * parser, MOP class_p, SM_METHOD_FILE * file_p);
static PARSER_VARCHAR *obj_print_describe_class_trigger (PARSER_CONTEXT * parser, TR_TRIGGER * trigger);
static void obj_print_describe_trigger_list (PARSER_CONTEXT * parser, TR_TRIGLIST * triggers, STRLIST ** strings);
static const char **obj_print_describe_class_triggers (PARSER_CONTEXT * parser, SM_CLASS * class_p, MOP class_mop);
static CLASS_HELP *obj_print_make_class_help (void);
static TRIGGER_HELP *obj_print_make_trigger_help (void);
static OBJ_HELP *obj_print_make_obj_help (void);
static char **obj_print_read_section (FILE * fp);
static COMMAND_HELP *obj_print_load_help_file (FILE * fp, const char *keyword);
static char *obj_print_next_token (char *ptr, char *buf);


/* This will be in one of the language directories under $CUBRID/msg */

static PARSER_CONTEXT *parser;

/*
 * obj_print_free_strarray() -  Most of the help functions build an array of
 *                              strings that contains the descriptions
 *                              of the object
 *      return: none
 *  strs(in) : array of strings
 *
 *  Note :
 *      This function frees the array when it is no longer necessary.
 */

static void
obj_print_free_strarray (char **strs)
{
  int i;

  if (strs == NULL)
    {
      return;
    }
  for (i = 0; strs[i] != NULL; i++)
    {
      free_and_init (strs[i]);
    }
  free_and_init (strs);
}

/*
 * obj_print_copy_string() - Copies a string, allocating space with malloc
 *      return: new string
 *  source(in) : string to copy
 *
 */

static char *
obj_print_copy_string (const char *source)
{
  char *new_str = NULL;

  if (source != NULL)
    {
      new_str = (char *) malloc (strlen (source) + 1);
      if (new_str != NULL)
	{
	  strcpy (new_str, source);
	}
    }
  return new_str;
}

/*
 * obj_print_convert_strlist() - This converts a string list into an array
 *                               of strings
 *      return: NULL terminated array of strings
 *  str_list(in) : string list
 *
 *  Note :
 *      Since the strings are pushed on the list in reverse order, we
 *      build the array in reverse order so the resulting array will
 *      "read" correctly.
 *
 */

static const char **
obj_print_convert_strlist (STRLIST * str_list)
{
  STRLIST *l, *next;
  const char **array;
  int count, i;

  assert (str_list != NULL);

  array = NULL;
  count = ws_list_length ((DB_LIST *) str_list);

  if (count)
    {
      array = (const char **) malloc (sizeof (char *) * (count + 1));
      if (array != NULL)
	{
	  for (i = count - 1, l = str_list, next = NULL; i >= 0; i--, l = next)
	    {
	      next = l->next;
	      array[i] = l->string;
	      free_and_init (l);
	    }
	  array[count] = NULL;
	}
    }
  return array;
}

/* TRIGGER SUPPORT FUNCTIONS */

/*
 * Support routines for trigger descriptions found
 * in both class help and trigger help.
 *
 */

/*
 * obj_print_trigger_condition_time() -
 *      return: char *
 *  trigger(in) :
 *
 */


static const char *
obj_print_trigger_condition_time (TR_TRIGGER * trigger)
{
  DB_TRIGGER_TIME time = TR_TIME_NULL;

  if (trigger->condition != NULL)
    {
      time = trigger->condition->time;
    }
  else if (trigger->action != NULL)
    {
      time = trigger->action->time;
    }

  return (tr_time_as_string (time));
}

/*
 * obj_print_trigger_action_time() -
 *      return: char *
 *  trigger(in) :
 *
 */

static const char *
obj_print_trigger_action_time (TR_TRIGGER * trigger)
{
  DB_TRIGGER_TIME time = TR_TIME_NULL;

  if (trigger->action != NULL)
    {
      time = trigger->action->time;
    }

  return (tr_time_as_string (time));
}

/*
 * object_print_identifier() - help function to print identifier string.
 *                             if prt_type is OBJ_PRINT_SHOW_CREATE_TABLE,
 *                             we need wrap it with "[" and "]".
 *      return: advanced buffer pointer
 *  parser(in) :
 *  buffer(in) : current buffer pointer
 *  identifier(in) : identifier string,.such as: table name.
 *  prt_type(in): the print type: csql schema or show create table
 *
 */
static PARSER_VARCHAR *
obj_print_identifier (PARSER_CONTEXT * parser, PARSER_VARCHAR * buffer, const char *identifier, OBJ_PRINT_TYPE prt_type)
{
  if (prt_type == OBJ_PRINT_CSQL_SCHEMA_COMMAND)
    {
      buffer = pt_append_nulstring (parser, buffer, identifier);
    }
  else
    {				/* prt_type == OBJ_PRINT_SHOW_CREATE_TABLE */
      buffer = pt_append_nulstring (parser, buffer, "[");
      buffer = pt_append_nulstring (parser, buffer, identifier);
      buffer = pt_append_nulstring (parser, buffer, "]");
    }

  return buffer;
}

/* CLASS COMPONENT DESCRIPTION FUNCTIONS */

/*
 * obj_print_describe_domain() - Describe the domain of an attribute
 *      return: advanced buffer pointer
 *  parser(in) :
 *  buffer(in) : current buffer pointer
 *  domain(in) : domain structure to describe
 *  prt_type(in): the print type: csql schema or show create table
 *  force_print_collation(in): true if collation is printed no matter system
 *			       collation
 *
 */

static PARSER_VARCHAR *
obj_print_describe_domain (PARSER_CONTEXT * parser, PARSER_VARCHAR * buffer, TP_DOMAIN * domain,
			   OBJ_PRINT_TYPE prt_type, bool force_print_collation)
{
  TP_DOMAIN *temp_domain;
  char temp_buffer[27];
  char temp_buffer_numeric[50];
  int precision = 0, idx, count;
  int has_collation;

  if (domain == NULL)
    {
      return buffer;
    }

  /* filter first, usually not necessary but this is visible */
  sm_filter_domain (domain, NULL);

  for (temp_domain = domain; temp_domain != NULL; temp_domain = temp_domain->next)
    {
      has_collation = 0;
      switch (TP_DOMAIN_TYPE (temp_domain))
	{
	case DB_TYPE_INTEGER:
	case DB_TYPE_BIGINT:
	case DB_TYPE_FLOAT:
	case DB_TYPE_DOUBLE:
	case DB_TYPE_BLOB:
	case DB_TYPE_CLOB:
	case DB_TYPE_TIME:
	case DB_TYPE_TIMETZ:
	case DB_TYPE_TIMELTZ:
	case DB_TYPE_UTIME:
	case DB_TYPE_TIMESTAMPTZ:
	case DB_TYPE_TIMESTAMPLTZ:
	case DB_TYPE_DATETIME:
	case DB_TYPE_DATETIMETZ:
	case DB_TYPE_DATETIMELTZ:
	case DB_TYPE_DATE:
	case DB_TYPE_MONETARY:
	case DB_TYPE_SUB:
	case DB_TYPE_POINTER:
	case DB_TYPE_ERROR:
	case DB_TYPE_SHORT:
	case DB_TYPE_VOBJ:
	case DB_TYPE_OID:
	case DB_TYPE_NULL:
	case DB_TYPE_VARIABLE:
	case DB_TYPE_DB_VALUE:
	  strcpy (temp_buffer, temp_domain->type->name);
	  ustr_upper (temp_buffer);
	  buffer = pt_append_nulstring (parser, buffer, temp_buffer);
	  break;

	case DB_TYPE_OBJECT:
	  if (temp_domain->class_mop != NULL)
	    {
	      buffer = obj_print_identifier (parser, buffer, sm_get_ch_name (temp_domain->class_mop), prt_type);
	    }
	  else
	    {
	      buffer = pt_append_nulstring (parser, buffer, temp_domain->type->name);
	    }
	  break;

	case DB_TYPE_VARCHAR:
	  has_collation = 1;
	  if (temp_domain->precision == TP_FLOATING_PRECISION_VALUE)
	    {
	      buffer = pt_append_nulstring (parser, buffer, "STRING");
	      break;
	    }
	  /* fall through */
	case DB_TYPE_CHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARNCHAR:
	  has_collation = 1;
	case DB_TYPE_BIT:
	case DB_TYPE_VARBIT:
	  strcpy (temp_buffer, temp_domain->type->name);
	  ustr_upper (temp_buffer);
	  buffer = pt_append_nulstring (parser, buffer, temp_buffer);
	  if (temp_domain->precision == TP_FLOATING_PRECISION_VALUE)
	    {
	      precision = DB_MAX_STRING_LENGTH;
	    }
	  else
	    {
	      precision = temp_domain->precision;
	    }
	  sprintf (temp_buffer, "(%d)", precision);
	  buffer = pt_append_nulstring (parser, buffer, temp_buffer);
	  break;

	case DB_TYPE_NUMERIC:
	  strcpy (temp_buffer, temp_domain->type->name);
	  ustr_upper (temp_buffer);
	  buffer = pt_append_nulstring (parser, buffer, temp_buffer);
	  sprintf (temp_buffer_numeric, "(%d,%d)", temp_domain->precision, temp_domain->scale);
	  buffer = pt_append_nulstring (parser, buffer, temp_buffer_numeric);
	  break;

	case DB_TYPE_SET:
	case DB_TYPE_MULTISET:
	case DB_TYPE_SEQUENCE:
	  strcpy (temp_buffer, temp_domain->type->name);
	  ustr_upper (temp_buffer);
	  buffer = pt_append_nulstring (parser, buffer, temp_buffer);
	  buffer = pt_append_nulstring (parser, buffer, " OF ");
	  if (temp_domain->setdomain != NULL)
	    {
	      if (temp_domain->setdomain->next != NULL && prt_type == OBJ_PRINT_SHOW_CREATE_TABLE)
		{
		  buffer = pt_append_nulstring (parser, buffer, "(");
		  buffer =
		    obj_print_describe_domain (parser, buffer, temp_domain->setdomain, prt_type, force_print_collation);
		  buffer = pt_append_nulstring (parser, buffer, ")");
		}
	      else
		{
		  buffer =
		    obj_print_describe_domain (parser, buffer, temp_domain->setdomain, prt_type, force_print_collation);
		}
	    }
	  break;
	case DB_TYPE_ENUMERATION:
	  has_collation = 1;
	  strcpy (temp_buffer, temp_domain->type->name);
	  ustr_upper (temp_buffer);
	  buffer = pt_append_nulstring (parser, buffer, temp_buffer);
	  buffer = pt_append_nulstring (parser, buffer, "(");
	  count = DOM_GET_ENUM_ELEMS_COUNT (temp_domain);
	  for (idx = 1; idx <= count; idx++)
	    {
	      if (idx > 1)
		{
		  buffer = pt_append_nulstring (parser, buffer, ", ");
		}
	      buffer = pt_append_nulstring (parser, buffer, "'");
	      buffer =
		pt_append_bytes (parser, buffer, DB_GET_ENUM_ELEM_STRING (&DOM_GET_ENUM_ELEM (temp_domain, idx)),
				 DB_GET_ENUM_ELEM_STRING_SIZE (&DOM_GET_ENUM_ELEM (temp_domain, idx)));
	      buffer = pt_append_nulstring (parser, buffer, "'");
	    }
	  buffer = pt_append_nulstring (parser, buffer, ")");
	  break;
	default:
	  break;
	}

      if (has_collation && (force_print_collation || temp_domain->collation_id != LANG_SYS_COLLATION))
	{
	  buffer = pt_append_nulstring (parser, buffer, " COLLATE ");
	  buffer = pt_append_nulstring (parser, buffer, lang_get_collation_name (temp_domain->collation_id));
	}
      if (temp_domain->next != NULL)
	{
	  buffer = pt_append_nulstring (parser, buffer, ", ");
	}
    }
  return buffer;
}

/*
 * obj_print_describe_attribute() - Describes the definition of an attribute
 *                                  in a class
 *      return: advanced bufbuffer pointer
 *  class_p(in) : class being examined
 *  parser(in) :
 *  attribute_p(in) : attribute of the class
 *  is_inherited(in) : is the attribute inherited 
 *  prt_type(in): the print type: csql schema or show create table
 *  obj_print_describe_attribute(in):
 *
 */

static char *
obj_print_describe_attribute (MOP class_p, PARSER_CONTEXT * parser, SM_ATTRIBUTE * attribute_p, bool is_inherited,
			      OBJ_PRINT_TYPE prt_type, bool force_print_collation)
{
  char *start;
  PARSER_VARCHAR *buffer;
  char line[SM_MAX_IDENTIFIER_LENGTH + 4];	/* Include room for _:_\0 */
  char str_buf[NUMERIC_MAX_STRING_SIZE];

  if (attribute_p == NULL)
    {
      return NULL;
    }

  buffer = NULL;
  if (prt_type == OBJ_PRINT_CSQL_SCHEMA_COMMAND)
    {
      sprintf (line, "%-20s ", attribute_p->header.name);
      buffer = pt_append_nulstring (parser, buffer, line);
    }
  else
    {				/* prt_type == OBJ_PRINT_SHOW_CREATE_TABLE */
      buffer = obj_print_identifier (parser, buffer, attribute_p->header.name, prt_type);
      buffer = pt_append_nulstring (parser, buffer, " ");
    }

  start = (char *) pt_get_varchar_bytes (buffer);
  /* could filter here but do in describe_domain */

  buffer = obj_print_describe_domain (parser, buffer, attribute_p->domain, prt_type, force_print_collation);

  if (attribute_p->header.name_space == ID_SHARED_ATTRIBUTE)
    {
      buffer = pt_append_nulstring (parser, buffer, " SHARED ");
      if (!DB_IS_NULL (&attribute_p->default_value.value))
	{
	  buffer = describe_value (parser, buffer, &attribute_p->default_value.value);
	}
    }
  else if (attribute_p->header.name_space == ID_ATTRIBUTE)
    {
      if (attribute_p->flags & SM_ATTFLAG_AUTO_INCREMENT)
	{
	  buffer = pt_append_nulstring (parser, buffer, " AUTO_INCREMENT ");

	  assert (is_inherited || attribute_p->auto_increment != NULL);

	  if (prt_type == OBJ_PRINT_SHOW_CREATE_TABLE)
	    {
	      DB_VALUE min_val, inc_val;
	      char buf[DB_MAX_NUMERIC_PRECISION * 2 + 4];
	      int offset;

	      assert (attribute_p->auto_increment != NULL);

	      DB_MAKE_NULL (&min_val);
	      DB_MAKE_NULL (&inc_val);

	      if (db_get (attribute_p->auto_increment, "min_val", &min_val) != NO_ERROR)
		{
		  return NULL;
		}

	      if (db_get (attribute_p->auto_increment, "increment_val", &inc_val) != NO_ERROR)
		{
		  pr_clear_value (&min_val);
		  return NULL;
		}

	      offset =
		snprintf (buf, DB_MAX_NUMERIC_PRECISION + 3, "(%s, ", numeric_db_value_print (&min_val, str_buf));
	      snprintf (buf + offset, DB_MAX_NUMERIC_PRECISION + 1, "%s)", numeric_db_value_print (&inc_val, str_buf));
	      buffer = pt_append_nulstring (parser, buffer, buf);

	      pr_clear_value (&min_val);
	      pr_clear_value (&inc_val);
	    }
	}
      if (!DB_IS_NULL (&attribute_p->default_value.value)
	  || attribute_p->default_value.default_expr.default_expr_type != DB_DEFAULT_NONE)
	{
	  const char *default_expr_type_str;

	  buffer = pt_append_nulstring (parser, buffer, " DEFAULT ");

	  if (attribute_p->default_value.default_expr.default_expr_op == T_TO_CHAR)
	    {
	      buffer = pt_append_nulstring (parser, buffer, "TO_CHAR(");
	    }

	  default_expr_type_str =
	    db_default_expression_string (attribute_p->default_value.default_expr.default_expr_type);
	  if (default_expr_type_str != NULL)
	    {
	      buffer = pt_append_nulstring (parser, buffer, default_expr_type_str);
	    }
	  else
	    {
	      assert (attribute_p->default_value.default_expr.default_expr_op == NULL_DEFAULT_EXPRESSION_OPERATOR);
	      buffer = describe_value (parser, buffer, &attribute_p->default_value.value);
	    }

	  if (attribute_p->default_value.default_expr.default_expr_op == T_TO_CHAR)
	    {
	      if (attribute_p->default_value.default_expr.default_expr_format)
		{
		  buffer = pt_append_nulstring (parser, buffer, ", \'");
		  buffer = pt_append_nulstring (parser, buffer,
						attribute_p->default_value.default_expr.default_expr_format);
		  buffer = pt_append_nulstring (parser, buffer, "\'");
		}

	      buffer = pt_append_nulstring (parser, buffer, ")");
	    }
	}
    }
  else if (attribute_p->header.name_space == ID_CLASS_ATTRIBUTE)
    {
      if (!DB_IS_NULL (&attribute_p->default_value.value))
	{
	  buffer = pt_append_nulstring (parser, buffer, " VALUE ");
	  buffer = describe_value (parser, buffer, &attribute_p->default_value.value);
	}
    }

  if (attribute_p->flags & SM_ATTFLAG_NON_NULL)
    {
      buffer = pt_append_nulstring (parser, buffer, " NOT NULL");
    }
  if (attribute_p->class_mop != NULL && attribute_p->class_mop != class_p)
    {
      buffer = pt_append_nulstring (parser, buffer, " /* from ");
      buffer = obj_print_identifier (parser, buffer, sm_get_ch_name (attribute_p->class_mop), prt_type);
      buffer = pt_append_nulstring (parser, buffer, " */");
    }

  if (attribute_p->comment != NULL && attribute_p->comment[0] != '\0')
    {
      buffer = pt_append_nulstring (parser, buffer, " ");
      buffer = pt_append_nulstring (parser, buffer, describe_comment (parser, attribute_p->comment));
    }

  /* let the higher level display routine do this */
  /* 
   * buffer = pt_append_nulstring(parser,buffer,"\n"); */
  return ((char *) pt_get_varchar_bytes (buffer));
}

/*
 * obj_print_describe_partition_parts() -
 *      return: char *
 *  parser(in) :
 *  parts(in) :
 *  prt_type(in): the print type: csql schema or show create table
 *
 */

static char *
obj_print_describe_partition_parts (PARSER_CONTEXT * parser, SM_PARTITION * parts, OBJ_PRINT_TYPE prt_type)
{
  DB_VALUE ele;
  PARSER_VARCHAR *buffer;
  int setsize, i;

  if (parts == NULL)
    {
      return NULL;
    }

  buffer = NULL;

  DB_MAKE_NULL (&ele);

  buffer = pt_append_nulstring (parser, buffer, "PARTITION ");
  buffer = obj_print_identifier (parser, buffer, parts->pname, prt_type);

  switch (parts->partition_type)
    {
    case PT_PARTITION_HASH:
      break;
    case PT_PARTITION_RANGE:
      buffer = pt_append_nulstring (parser, buffer, " VALUES LESS THAN ");
      if (!set_get_element (parts->values, 1, &ele))
	{			/* 0:MIN, 1: MAX */
	  if (DB_IS_NULL (&ele))
	    {
	      buffer = pt_append_nulstring (parser, buffer, "MAXVALUE");
	    }
	  else
	    {
	      buffer = pt_append_nulstring (parser, buffer, "(");
	      buffer = describe_value (parser, buffer, &ele);
	      buffer = pt_append_nulstring (parser, buffer, ")");
	    }
	}
      break;
    case PT_PARTITION_LIST:
      buffer = pt_append_nulstring (parser, buffer, " VALUES IN (");
      setsize = set_size (parts->values);
      for (i = 0; i < setsize; i++)
	{
	  if (i > 0)
	    {
	      buffer = pt_append_nulstring (parser, buffer, ", ");
	    }
	  if (set_get_element (parts->values, i, &ele) == NO_ERROR)
	    {
	      buffer = describe_value (parser, buffer, &ele);
	    }
	}
      buffer = pt_append_nulstring (parser, buffer, ")");
      break;
    }

  if (parts->comment != NULL && parts->comment[0] != '\0')
    {
      buffer = pt_append_nulstring (parser, buffer, " ");
      buffer = pt_append_nulstring (parser, buffer, describe_comment (parser, parts->comment));
    }

  pr_clear_value (&ele);

  return ((char *) pt_get_varchar_bytes (buffer));
}

/*
 * obj_print_describe_partition_info() -
 *      return: char *
 *  parser(in) :
 *  partinfo(in) :
 *
 */

static char *
obj_print_describe_partition_info (PARSER_CONTEXT * parser, SM_PARTITION * partinfo)
{
  DB_VALUE ele;
  PARSER_VARCHAR *buffer;
  char line[SM_MAX_IDENTIFIER_LENGTH + 1], *ptr, *ptr2, *tmp;
  char col_name[DB_MAX_IDENTIFIER_LENGTH + 1];

  if (partinfo == NULL)
    {
      return NULL;
    }

  buffer = NULL;

  buffer = pt_append_nulstring (parser, buffer, "PARTITION BY ");

  switch (partinfo->partition_type)
    {
    case PT_PARTITION_HASH:
      buffer = pt_append_nulstring (parser, buffer, "HASH (");
      break;
    case PT_PARTITION_RANGE:
      buffer = pt_append_nulstring (parser, buffer, "RANGE (");
      break;
    case PT_PARTITION_LIST:
      buffer = pt_append_nulstring (parser, buffer, "LIST (");
      break;
    }

  tmp = partinfo->expr;
  assert (tmp != NULL);

  ptr = tmp ? strstr (tmp, "SELECT ") : NULL;
  if (ptr)
    {
      ptr2 = strstr (ptr + 7, " FROM ");
      if (ptr2)
	{
	  strncpy (col_name, ptr + 7, CAST_STRLEN (ptr2 - (ptr + 7)));
	  col_name[CAST_STRLEN (ptr2 - (ptr + 7))] = 0;
	  buffer = pt_append_nulstring (parser, buffer, col_name);
	  buffer = pt_append_nulstring (parser, buffer, ") ");
	}
    }

  if (partinfo->partition_type == PT_PARTITION_HASH)
    {
      if (set_get_element (partinfo->values, 1, &ele) == NO_ERROR)
	{
	  sprintf (line, "PARTITIONS %d", DB_GET_INTEGER (&ele));
	  buffer = pt_append_nulstring (parser, buffer, line);
	}
    }

  return ((char *) pt_get_varchar_bytes (buffer));
}

/*
 * obj_print_describe_constraint() - Describes the definition of an attribute
 *                                   in a class
 *      return: advanced buffer pointer
 *  parser(in) :
 *  class_p(in) : class being examined
 *  constraint_p(in) :
 *  prt_type(in): the print type: csql schema or show create table
 *
 */

static char *
obj_print_describe_constraint (PARSER_CONTEXT * parser, SM_CLASS * class_p, SM_CLASS_CONSTRAINT * constraint_p,
			       OBJ_PRINT_TYPE prt_type)
{
  PARSER_VARCHAR *buffer;
  SM_ATTRIBUTE **attribute_p;
  const int *asc_desc;
  const int *prefix_length;
  char temp[20];
  int k, n_attrs = 0;

  buffer = NULL;

  if (!class_p || !constraint_p)
    {
      return NULL;
    }

  if (prt_type == OBJ_PRINT_CSQL_SCHEMA_COMMAND)
    {
      switch (constraint_p->type)
	{
	case SM_CONSTRAINT_INDEX:
	  buffer = pt_append_nulstring (parser, buffer, "INDEX ");
	  break;
	case SM_CONSTRAINT_UNIQUE:
	  buffer = pt_append_nulstring (parser, buffer, "UNIQUE ");
	  break;
	case SM_CONSTRAINT_REVERSE_INDEX:
	  buffer = pt_append_nulstring (parser, buffer, "REVERSE INDEX ");
	  break;
	case SM_CONSTRAINT_REVERSE_UNIQUE:
	  buffer = pt_append_nulstring (parser, buffer, "REVERSE UNIQUE ");
	  break;
	case SM_CONSTRAINT_PRIMARY_KEY:
	  buffer = pt_append_nulstring (parser, buffer, "PRIMARY KEY ");
	  break;
	case SM_CONSTRAINT_FOREIGN_KEY:
	  buffer = pt_append_nulstring (parser, buffer, "FOREIGN KEY ");
	  break;
	default:
	  buffer = pt_append_nulstring (parser, buffer, "CONSTRAINT ");
	  break;
	}

      buffer = pt_append_nulstring (parser, buffer, constraint_p->name);
      buffer = pt_append_nulstring (parser, buffer, " ON ");
      buffer = pt_append_nulstring (parser, buffer, sm_ch_name ((MOBJ) class_p));
      buffer = pt_append_nulstring (parser, buffer, " (");

      asc_desc = NULL;		/* init */
      if (!SM_IS_CONSTRAINT_REVERSE_INDEX_FAMILY (constraint_p->type))
	{
	  asc_desc = constraint_p->asc_desc;
	}
    }
  else
    {				/* prt_type == OBJ_PRINT_SHOW_CREATE_TABLE */
      switch (constraint_p->type)
	{
	case SM_CONSTRAINT_INDEX:
	case SM_CONSTRAINT_REVERSE_INDEX:
	  buffer = pt_append_nulstring (parser, buffer, " INDEX ");
	  buffer = obj_print_identifier (parser, buffer, constraint_p->name, prt_type);
	  break;
	case SM_CONSTRAINT_UNIQUE:
	case SM_CONSTRAINT_REVERSE_UNIQUE:
	  buffer = pt_append_nulstring (parser, buffer, " CONSTRAINT ");
	  buffer = obj_print_identifier (parser, buffer, constraint_p->name, prt_type);
	  buffer = pt_append_nulstring (parser, buffer, " UNIQUE KEY ");
	  break;
	case SM_CONSTRAINT_PRIMARY_KEY:
	  buffer = pt_append_nulstring (parser, buffer, " CONSTRAINT ");
	  buffer = obj_print_identifier (parser, buffer, constraint_p->name, prt_type);
	  buffer = pt_append_nulstring (parser, buffer, " PRIMARY KEY ");
	  break;
	case SM_CONSTRAINT_FOREIGN_KEY:
	  buffer = pt_append_nulstring (parser, buffer, " CONSTRAINT ");
	  buffer = obj_print_identifier (parser, buffer, constraint_p->name, prt_type);
	  buffer = pt_append_nulstring (parser, buffer, " FOREIGN KEY ");
	  break;
	default:
	  assert (false);
	  break;
	}

      buffer = pt_append_nulstring (parser, buffer, " (");
      asc_desc = constraint_p->asc_desc;
    }

  prefix_length = constraint_p->attrs_prefix_length;

  /* If the index is a function index, then the corresponding expression is printed at the right position in the
   * attribute list. Since the expression is not part of the attribute list, when searching through that list we must
   * check if the position of the expression is reached and then print it. */
  k = 0;
  if (constraint_p->func_index_info)
    {
      n_attrs = constraint_p->func_index_info->attr_index_start + 1;
    }
  else
    {
      for (attribute_p = constraint_p->attributes; *attribute_p; attribute_p++)
	{
	  n_attrs++;
	}
    }
  for (attribute_p = constraint_p->attributes; k < n_attrs; attribute_p++)
    {
      if (constraint_p->func_index_info && k == constraint_p->func_index_info->col_id)
	{
	  if (k > 0)
	    {
	      buffer = pt_append_nulstring (parser, buffer, ", ");
	    }
	  buffer = pt_append_nulstring (parser, buffer, constraint_p->func_index_info->expr_str);
	  if (constraint_p->func_index_info->fi_domain->is_desc)
	    {
	      buffer = pt_append_nulstring (parser, buffer, " DESC");
	    }
	  k++;
	}
      if (k == n_attrs)
	{
	  break;
	}
      if (k > 0)
	{
	  buffer = pt_append_nulstring (parser, buffer, ", ");
	}
      buffer = obj_print_identifier (parser, buffer, (*attribute_p)->header.name, prt_type);

      if (prefix_length)
	{
	  if (*prefix_length != -1)
	    {
	      sprintf (temp, "(%d)", *prefix_length);
	      pt_append_nulstring (parser, buffer, temp);
	    }

	  prefix_length++;
	}

      if (asc_desc)
	{
	  if (*asc_desc == 1)
	    {
	      buffer = pt_append_nulstring (parser, buffer, " DESC");
	    }
	  asc_desc++;
	}
      k++;
    }
  buffer = pt_append_nulstring (parser, buffer, ")");

  if (constraint_p->filter_predicate && constraint_p->filter_predicate->pred_string)
    {
      buffer = pt_append_nulstring (parser, buffer, " WHERE ");
      buffer = pt_append_nulstring (parser, buffer, constraint_p->filter_predicate->pred_string);
    }

  if (constraint_p->type == SM_CONSTRAINT_FOREIGN_KEY && constraint_p->fk_info)
    {
      MOP ref_clsop;
      SM_CLASS *ref_cls;
      SM_CLASS_CONSTRAINT *c;

      ref_clsop = ws_mop (&(constraint_p->fk_info->ref_class_oid), NULL);
      if (au_fetch_class_force (ref_clsop, &ref_cls, AU_FETCH_READ) != NO_ERROR)
	{
	  return NULL;
	}

      buffer = pt_append_nulstring (parser, buffer, " REFERENCES ");
      buffer = obj_print_identifier (parser, buffer, sm_ch_name ((MOBJ) ref_cls), prt_type);

      if (prt_type == OBJ_PRINT_SHOW_CREATE_TABLE)
	{
	  for (c = ref_cls->constraints; c; c = c->next)
	    {
	      if (c->type == SM_CONSTRAINT_PRIMARY_KEY && c->attributes != NULL)
		{
		  buffer = pt_append_nulstring (parser, buffer, " (");
		  for (k = 0; k < n_attrs; k++)
		    {
		      if (c->attributes[k] != NULL)
			{
			  buffer = obj_print_identifier (parser, buffer, c->attributes[k]->header.name, prt_type);
			  if (k != (n_attrs - 1))
			    {
			      buffer = pt_append_nulstring (parser, buffer, ", ");
			    }
			}
		    }

		  buffer = pt_append_nulstring (parser, buffer, ")");

		  break;
		}
	    }
	}

      buffer = pt_append_nulstring (parser, buffer, " ON DELETE ");
      buffer =
	pt_append_nulstring (parser, buffer,
			     classobj_describe_foreign_key_action (constraint_p->fk_info->delete_action));

      if (prt_type == OBJ_PRINT_CSQL_SCHEMA_COMMAND)
	{
	  buffer = pt_append_nulstring (parser, buffer, ",");
	}
      buffer = pt_append_nulstring (parser, buffer, " ON UPDATE ");
      buffer =
	pt_append_nulstring (parser, buffer,
			     classobj_describe_foreign_key_action (constraint_p->fk_info->update_action));
    }

  if (constraint_p->comment != NULL && constraint_p->comment[0] != '\0')
    {
      buffer = pt_append_nulstring (parser, buffer, " ");
      buffer = pt_append_nulstring (parser, buffer, describe_comment (parser, constraint_p->comment));
    }

  return ((char *) pt_get_varchar_bytes (buffer));
}				/* describe_constraint() */

/*
 * obj_print_describe_argument() - Describes a method argument
 *      return: advanced buffer pointer
 *  parser(in) :
 *  buffer(in) : current buffer pointer
 *  argument_p(in) : method argument to describe
 *  prt_type(in): the print type: csql schema or show create table
 *
 */

static PARSER_VARCHAR *
obj_print_describe_argument (PARSER_CONTEXT * parser, PARSER_VARCHAR * buffer, SM_METHOD_ARGUMENT * argument_p,
			     OBJ_PRINT_TYPE prt_type)
{
  if (argument_p != NULL)
    {
      if (argument_p->domain != NULL)
	{
	  /* method and its arguments do not inherit collation from class, collation printing is not enforced */
	  buffer = obj_print_describe_domain (parser, buffer, argument_p->domain, prt_type, false);
	}
      else if (argument_p->type)
	{
	  buffer = pt_append_nulstring (parser, buffer, argument_p->type->name);
	}
      else
	{
	  buffer = pt_append_nulstring (parser, buffer, "invalid type");
	}
    }
  return buffer;
}

/*
 * obj_print_describe_signature() - Describes a method signature
 *      return: advanced buffer pointer
 *  parser(in) :
 *  buffer(in) : current buffer pointer
 *  signature_p(in) : signature to describe
 *  prt_type(in): the print type: csql schema or show create table
 *
 */

static PARSER_VARCHAR *
obj_print_describe_signature (PARSER_CONTEXT * parser, PARSER_VARCHAR * buffer, SM_METHOD_SIGNATURE * signature_p,
			      OBJ_PRINT_TYPE prt_type)
{
  SM_METHOD_ARGUMENT *argument_p;
  int i;

  if (signature_p == NULL)
    {
      return buffer;
    }

  for (i = 1; i <= signature_p->num_args; i++)
    {
      for (argument_p = signature_p->args; argument_p != NULL && argument_p->index != i; argument_p = argument_p->next);
      if (argument_p != NULL)
	{
	  buffer = obj_print_describe_argument (parser, buffer, argument_p, prt_type);
	}
      else
	{
	  buffer = pt_append_nulstring (parser, buffer, "??");
	}
      if (i < signature_p->num_args)
	{
	  buffer = pt_append_nulstring (parser, buffer, ", ");
	}
    }

  return buffer;
}

/*
 * describe_method() - Describes the definition of a method in a class
 *      return: advanced buffer pointer
 *  parser(in) : current buffer pointer
 *  op(in) : class with method
 *  method_p(in) : method to describe
 *  prt_type(in): the print type: csql schema or show create table
 *
 */

static PARSER_VARCHAR *
obj_print_describe_method (PARSER_CONTEXT * parser, MOP op, SM_METHOD * method_p, OBJ_PRINT_TYPE prt_type)
{
  PARSER_VARCHAR *buffer;
  SM_METHOD_SIGNATURE *signature_p;

  /* assume for the moment that there can only be one signature, simplifies the output */

  buffer = NULL;

  if (method_p == NULL)
    {
      return buffer;
    }
  buffer = obj_print_identifier (parser, buffer, method_p->header.name, prt_type);
  signature_p = method_p->signatures;
  if (signature_p == NULL)
    {
      buffer = pt_append_nulstring (parser, buffer, "()");
    }
  else
    {
      buffer = pt_append_nulstring (parser, buffer, "(");
      buffer = obj_print_describe_signature (parser, buffer, signature_p, prt_type);
      buffer = pt_append_nulstring (parser, buffer, ") ");

      if (signature_p->value != NULL)
	{
	  /* make this look more like the actual definition instead strcpy(line, "returns "); line += strlen(line); */
	  buffer = obj_print_describe_argument (parser, buffer, signature_p->value, prt_type);
	}
      if (signature_p->function_name != NULL)
	{
	  buffer = pt_append_nulstring (parser, buffer, " FUNCTION ");
	  buffer = obj_print_identifier (parser, buffer, signature_p->function_name, prt_type);
	}
    }
  /* add the inheritance source */
  if (method_p->class_mop != NULL && method_p->class_mop != op)
    {

      buffer = pt_append_nulstring (parser, buffer, "(from ");
      buffer = obj_print_identifier (parser, buffer, sm_get_ch_name (method_p->class_mop), prt_type);
      buffer = pt_append_nulstring (parser, buffer, ")");
    }

  return buffer;
}

/*
 * obj_print_describe_resolution() - Describes a resolution specifier
 *      return: advanced buffer pointer
 *  parser(in) :
 *  resolution_p(in) : resolution to describe
 *  prt_type(in): the print type: csql schema or show create table
 *
 */

static PARSER_VARCHAR *
obj_print_describe_resolution (PARSER_CONTEXT * parser, SM_RESOLUTION * resolution_p, OBJ_PRINT_TYPE prt_type)
{
  PARSER_VARCHAR *buffer;
  buffer = NULL;

  if (resolution_p != NULL)
    {
      if (prt_type != OBJ_PRINT_SHOW_CREATE_TABLE)
	{
	  if (resolution_p->name_space == ID_CLASS)
	    {
	      buffer = pt_append_nulstring (parser, buffer, "inherit CLASS ");
	    }
	  else
	    {
	      buffer = pt_append_nulstring (parser, buffer, "inherit ");
	    }
	}

      buffer = obj_print_identifier (parser, buffer, resolution_p->name, prt_type);
      buffer = pt_append_nulstring (parser, buffer, " of ");
      buffer = obj_print_identifier (parser, buffer, sm_get_ch_name (resolution_p->class_mop), prt_type);

      if (resolution_p->alias != NULL)
	{
	  buffer = pt_append_nulstring (parser, buffer, " as ");
	  buffer = obj_print_identifier (parser, buffer, resolution_p->alias, prt_type);
	}
    }
  return buffer;
}

/*
 * obj_print_describe_method_file () - Describes a method file.
 *   return: advanced buffer pointer
 *   parser(in) :
 *   class_p(in) :
 *   file_p(in): method file descriptor
 */

static PARSER_VARCHAR *
obj_print_describe_method_file (PARSER_CONTEXT * parser, MOP class_p, SM_METHOD_FILE * file_p)
{
  PARSER_VARCHAR *fbuffer = NULL;
  if (file_p != NULL)
    {
      fbuffer = pt_append_nulstring (parser, fbuffer, file_p->name);
      if (file_p->class_mop != NULL && file_p->class_mop != class_p)
	{
	  pt_append_nulstring (parser, fbuffer, " (from ");
	  pt_append_nulstring (parser, fbuffer, sm_get_ch_name (file_p->class_mop));
	  pt_append_nulstring (parser, fbuffer, ")");
	}
    }
  return fbuffer;
}

/*
 * describe_class_trigger () - Describes the given trigger object to a buffer.
 *   return: PARSER_VARCHAR *
 *   parser(in): buffer for description
 *   trigger(in): trigger object
 * Note :
 *    This description is for the class help and as such it contains
 *    a condensed versino of the trigger help.
 */

static PARSER_VARCHAR *
obj_print_describe_class_trigger (PARSER_CONTEXT * parser, TR_TRIGGER * trigger)
{
  PARSER_VARCHAR *buffer = NULL;

  buffer = pt_append_nulstring (parser, buffer, trigger->name);
  buffer = pt_append_nulstring (parser, buffer, " : ");
  buffer = pt_append_nulstring (parser, buffer, obj_print_trigger_condition_time (trigger));
  buffer = pt_append_nulstring (parser, buffer, " ");
  buffer = pt_append_nulstring (parser, buffer, tr_event_as_string (trigger->event));
  buffer = pt_append_nulstring (parser, buffer, " ");

  if (trigger->attribute != NULL)
    {
      buffer = pt_append_nulstring (parser, buffer, "OF ");
      buffer = pt_append_nulstring (parser, buffer, trigger->attribute);
    }

  if (trigger->comment != NULL && trigger->comment[0] != '\0')
    {
      buffer = pt_append_nulstring (parser, buffer, " ");
      buffer = pt_append_nulstring (parser, buffer, describe_comment (parser, trigger->comment));
    }

  return buffer;
}

/*
 * obj_print_describe_trigger_list () - Describe a list of triggers
 *   return: none
 *   parser(in):
 *   triggers(in): trigger list
 *   strings(in): string list
 *
 * Note :
 *    This description is part of the class help so it contains only
 *    a condensed version of the trigger description.
 */

static void
obj_print_describe_trigger_list (PARSER_CONTEXT * parser, TR_TRIGLIST * triggers, STRLIST ** strings)
{
  TR_TRIGLIST *t;
  STRLIST *new_p;
  PARSER_VARCHAR *buffer;

  for (t = triggers; t != NULL; t = t->next)
    {
      buffer = obj_print_describe_class_trigger (parser, t->trigger);

      /* 
       * this used to be add_strlist, but since it is not used in any other
       * place, or ever will be, I unrolled it here.
       */
      new_p = (STRLIST *) malloc (sizeof (STRLIST));
      if (new_p != NULL)
	{
	  new_p->next = *strings;
	  *strings = new_p;
	  new_p->string = obj_print_copy_string ((char *) pt_get_varchar_bytes (buffer));
	}
    }
}

/*
 * obj_print_describe_class_triggers () - This builds an array of trigger
 *                                        descriptions for a class
 *   return: array of trigger description strings ( or NULL) )
 *   parser(in):
 *   class(in): class to examine
 */

static const char **
obj_print_describe_class_triggers (PARSER_CONTEXT * parser, SM_CLASS * class_p, MOP class_mop)
{
  SM_ATTRIBUTE *attribute_p;
  STRLIST *strings;
  const char **array = NULL;
  TR_SCHEMA_CACHE *cache;
  int i;

  strings = NULL;

  cache = class_p->triggers;
  if (cache != NULL && !tr_validate_schema_cache (cache, class_mop))
    {
      for (i = 0; i < cache->array_length; i++)
	{
	  obj_print_describe_trigger_list (parser, cache->triggers[i], &strings);
	}
    }

  for (attribute_p = class_p->ordered_attributes; attribute_p != NULL; attribute_p = attribute_p->order_link)
    {
      cache = attribute_p->triggers;
      if (cache != NULL && !tr_validate_schema_cache (cache, class_mop))
	{
	  for (i = 0; i < cache->array_length; i++)
	    {
	      obj_print_describe_trigger_list (parser, cache->triggers[i], &strings);
	    }
	}
    }

  for (attribute_p = class_p->class_attributes; attribute_p != NULL;
       attribute_p = (SM_ATTRIBUTE *) attribute_p->header.next)
    {
      cache = attribute_p->triggers;
      if (cache != NULL && !tr_validate_schema_cache (cache, class_mop))
	{
	  for (i = 0; i < cache->array_length; i++)
	    {
	      obj_print_describe_trigger_list (parser, cache->triggers[i], &strings);
	    }
	}
    }

  if (strings != NULL)
    {
      array = obj_print_convert_strlist (strings);
    }
  return array;
}

/* CLASS HELP */

/*
 * obj_print_make_class_help () - Creates an empty class help structure
 *   return: class help structure
 */

static CLASS_HELP *
obj_print_make_class_help (void)
{
  CLASS_HELP *new_p;

  new_p = (CLASS_HELP *) malloc (sizeof (CLASS_HELP));
  if (new_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (CLASS_HELP));
      return NULL;
    }
  new_p->name = NULL;
  new_p->class_type = NULL;
  new_p->collation = NULL;
  new_p->supers = NULL;
  new_p->subs = NULL;
  new_p->attributes = NULL;
  new_p->class_attributes = NULL;
  new_p->methods = NULL;
  new_p->class_methods = NULL;
  new_p->resolutions = NULL;
  new_p->method_files = NULL;
  new_p->query_spec = NULL;
  new_p->object_id = NULL;
  new_p->triggers = NULL;
  new_p->constraints = NULL;
  new_p->partition = NULL;
  new_p->comment = NULL;

  return new_p;
}

/*
 * obj_print_help_free_class () - Frees a class help structure that is no longer needed
 *                      The help structure should have been built
 *                      by help_class()
 *   return: none
 *   info(in): class help structure
 */

void
obj_print_help_free_class (CLASS_HELP * info)
{
  if (info != NULL)
    {
      if (info->name != NULL)
	{
	  free_and_init (info->name);
	}
      if (info->class_type != NULL)
	{
	  free_and_init (info->class_type);
	}
      if (info->object_id != NULL)
	{
	  free_and_init (info->object_id);
	}
      if (info->collation != NULL)
	{
	  free_and_init (info->collation);
	}
      obj_print_free_strarray (info->supers);
      obj_print_free_strarray (info->subs);
      obj_print_free_strarray (info->attributes);
      obj_print_free_strarray (info->class_attributes);
      obj_print_free_strarray (info->methods);
      obj_print_free_strarray (info->class_methods);
      obj_print_free_strarray (info->resolutions);
      obj_print_free_strarray (info->method_files);
      obj_print_free_strarray (info->query_spec);
      obj_print_free_strarray (info->triggers);
      obj_print_free_strarray (info->constraints);
      obj_print_free_strarray (info->partition);
      if (info->comment != NULL)
	{
	  free_and_init (info->comment);
	}
      free_and_init (info);
    }
}

/*
 * describe_class () - Describes the definition of a class
 *   return: create table string
 *   parser(in):
 *   class_schema(in):
 *   class_op(in):
 */
PARSER_VARCHAR *
obj_print_describe_class (const PARSER_CONTEXT * parser, CLASS_HELP * class_schema, DB_OBJECT * class_op)
{
  PARSER_VARCHAR *buffer;
  char **line_ptr;

  buffer = NULL;

  /* class name */
  buffer = pt_append_nulstring (parser, buffer, "CREATE TABLE ");
  buffer = pt_append_nulstring (parser, buffer, class_schema->name);

  /* under or as subclass of */
  if (class_schema->supers != NULL)
    {
      buffer = pt_append_nulstring (parser, buffer, " UNDER ");

      for (line_ptr = class_schema->supers; *line_ptr != NULL; line_ptr++)
	{
	  if (line_ptr != class_schema->supers)
	    {
	      buffer = pt_append_nulstring (parser, buffer, ", ");
	    }
	  buffer = pt_append_nulstring (parser, buffer, *line_ptr);
	}
    }

  /* class attributes */
  if (class_schema->class_attributes != NULL)
    {
      buffer = pt_append_nulstring (parser, buffer, " CLASS ATTRIBUTE (");

      for (line_ptr = class_schema->class_attributes; *line_ptr != NULL; line_ptr++)
	{
	  if (line_ptr != class_schema->class_attributes)
	    {
	      buffer = pt_append_nulstring (parser, buffer, ", ");
	    }
	  buffer = pt_append_nulstring (parser, buffer, *line_ptr);
	}

      buffer = pt_append_nulstring (parser, buffer, ")");
    }

  /* attributes and constraints */
  if (class_schema->attributes != NULL || class_schema->constraints != NULL)
    {
      buffer = pt_append_nulstring (parser, buffer, " (");
      if (class_schema->attributes != NULL)
	{
	  for (line_ptr = class_schema->attributes; *line_ptr != NULL; line_ptr++)
	    {
	      if (line_ptr != class_schema->attributes)
		{
		  buffer = pt_append_nulstring (parser, buffer, ", ");
		}
	      buffer = pt_append_nulstring (parser, buffer, *line_ptr);
	    }
	}
      if (class_schema->constraints != NULL)
	{
	  for (line_ptr = class_schema->constraints; *line_ptr != NULL; line_ptr++)
	    {
	      if (line_ptr != class_schema->constraints || class_schema->attributes != NULL)
		{
		  buffer = pt_append_nulstring (parser, buffer, ", ");
		}
	      buffer = pt_append_nulstring (parser, buffer, *line_ptr);
	    }
	}
      buffer = pt_append_nulstring (parser, buffer, ")");
    }

  /* reuse_oid flag */
  if (sm_is_reuse_oid_class (class_op))
    {
      buffer = pt_append_nulstring (parser, buffer, " REUSE_OID");
      if (class_schema->collation != NULL)
	{
	  buffer = pt_append_nulstring (parser, buffer, ",");
	}
      else
	{
	  buffer = pt_append_nulstring (parser, buffer, " ");
	}

    }

  /* collation */
  if (class_schema->collation != NULL)
    {
      buffer = pt_append_nulstring (parser, buffer, " COLLATE ");
      buffer = pt_append_nulstring (parser, buffer, class_schema->collation);
    }

  /* methods and class_methods */
  if (class_schema->methods != NULL || class_schema->class_methods != NULL)
    {
      buffer = pt_append_nulstring (parser, buffer, " METHOD ");
      if (class_schema->methods != NULL)
	{
	  for (line_ptr = class_schema->methods; *line_ptr != NULL; line_ptr++)
	    {
	      if (line_ptr != class_schema->methods)
		{
		  buffer = pt_append_nulstring (parser, buffer, ", ");
		}
	      buffer = pt_append_nulstring (parser, buffer, *line_ptr);
	    }
	}
      if (class_schema->class_methods != NULL)
	{
	  for (line_ptr = class_schema->class_methods; *line_ptr != NULL; line_ptr++)
	    {
	      if (line_ptr != class_schema->class_methods || class_schema->methods != NULL)
		{
		  buffer = pt_append_nulstring (parser, buffer, ", ");
		}
	      buffer = pt_append_nulstring (parser, buffer, " CLASS ");
	      buffer = pt_append_nulstring (parser, buffer, *line_ptr);
	    }
	}
    }

  /* method files */
  if (class_schema->method_files != NULL)
    {
      char tmp[PATH_MAX + 2];

      buffer = pt_append_nulstring (parser, buffer, " FILE ");
      for (line_ptr = class_schema->method_files; *line_ptr != NULL; line_ptr++)
	{
	  if (line_ptr != class_schema->method_files)
	    {
	      buffer = pt_append_nulstring (parser, buffer, ", ");
	    }
	  snprintf (tmp, PATH_MAX + 2, "'%s'", *line_ptr);
	  buffer = pt_append_nulstring (parser, buffer, tmp);
	}
    }

  /* inherit */
  if (class_schema->resolutions != NULL)
    {
      buffer = pt_append_nulstring (parser, buffer, " INHERIT ");
      for (line_ptr = class_schema->resolutions; *line_ptr != NULL; line_ptr++)
	{
	  if (line_ptr != class_schema->resolutions)
	    {
	      buffer = pt_append_nulstring (parser, buffer, ", ");
	    }
	  buffer = pt_append_nulstring (parser, buffer, *line_ptr);
	}
    }

  /* partition */
  if (class_schema->partition != NULL)
    {
      char **first_ptr;

      line_ptr = class_schema->partition;
      buffer = pt_append_nulstring (parser, buffer, " ");
      buffer = pt_append_nulstring (parser, buffer, *line_ptr);
      line_ptr++;
      if (*line_ptr != NULL)
	{
	  buffer = pt_append_nulstring (parser, buffer, " (");
	  for (first_ptr = line_ptr; *line_ptr != NULL; line_ptr++)
	    {
	      if (line_ptr != first_ptr)
		{
		  buffer = pt_append_nulstring (parser, buffer, ", ");
		}
	      buffer = pt_append_nulstring (parser, buffer, *line_ptr);
	    }
	  buffer = pt_append_nulstring (parser, buffer, ")");
	}
    }

  /* comment */
  if (class_schema->comment != NULL && class_schema->comment[0] != '\0')
    {
      DB_VALUE comment_value;
      DB_MAKE_NULL (&comment_value);
      DB_MAKE_STRING (&comment_value, class_schema->comment);

      buffer = pt_append_nulstring (parser, buffer, " COMMENT=");
      buffer = describe_value (parser, buffer, &comment_value);

      pr_clear_value (&comment_value);
    }

  return buffer;
}

/*
 * obj_print_help_class () - Constructs a class help structure containing textual
 *                 information about the class.
 *   return: class help structure
 *   op(in): class object
 *   prt_type(in): the print type: csql schema or show create table
 */

CLASS_HELP *
obj_print_help_class (MOP op, OBJ_PRINT_TYPE prt_type)
{
  SM_CLASS *class_;
  SM_ATTRIBUTE *a;
  SM_METHOD *m;
  SM_RESOLUTION *r;
  SM_METHOD_FILE *f;
  SM_QUERY_SPEC *p;
  CLASS_HELP *info = NULL;
  DB_OBJLIST *super, *user;
  int count, i, is_cubrid = 0;
  char **strs;
  const char *kludge;
  PARSER_VARCHAR *buffer;
  int is_class = 0;
  SM_CLASS *subclass;
  char *description;
  char name_buf[SM_MAX_IDENTIFIER_LENGTH + SM_MAX_CLASS_COMMENT_LENGTH + 50];
  bool include_inherited;
  bool force_print_att_coll = false;
  bool has_comment = false;
  int max_name_size = SM_MAX_IDENTIFIER_LENGTH + 50;
  size_t buf_size = 0;
  STRLIST *str_list_head = NULL, *current_str = NULL, *tmp_str = NULL;

  buffer = NULL;
  if (parser == NULL)
    {
      parser = parser_create_parser ();
    }
  if (parser == NULL)
    {
      goto error_exit;
    }

  include_inherited = (prt_type == OBJ_PRINT_CSQL_SCHEMA_COMMAND);

  is_class = locator_is_class (op, DB_FETCH_READ);
  if (is_class < 0)
    {
      goto error_exit;
    }
  if (!is_class || locator_is_root (op))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
      goto error_exit;
    }

  else if (au_fetch_class (op, &class_, AU_FETCH_READ, AU_SELECT) == NO_ERROR)
    {
      if (class_->comment != NULL && class_->comment[0] != '\0')
	{
	  has_comment = true;
	  max_name_size = SM_MAX_IDENTIFIER_LENGTH + SM_MAX_CLASS_COMMENT_LENGTH + 50;
	}

      force_print_att_coll = (class_->collation_id != LANG_SYS_COLLATION) ? true : false;
      /* make sure all the information is up to date */
      if (sm_clean_class (op, class_) != NO_ERROR)
	{
	  goto error_exit;
	}

      info = obj_print_make_class_help ();
      if (info == NULL)
	{
	  goto error_exit;
	}

      if (prt_type == OBJ_PRINT_CSQL_SCHEMA_COMMAND)
	{
	  /* 
	   * For the case of "print schema",
	   * info->name is set to:
	   *   exact class name
	   *   + COLLATE collation_name if exists;
	   *   + COMMENT 'text' if exists;
	   *
	   * The caller uses info->name to fill in "<Class Name> $name"
	   */
	  if (class_->collation_id == LANG_SYS_COLLATION)
	    {
	      if (has_comment)
		{
		  snprintf (name_buf, max_name_size, "%-20s %s", (char *) sm_ch_name ((MOBJ) class_),
			    describe_comment (parser, class_->comment));
		}
	      else
		{
		  snprintf (name_buf, max_name_size, "%s", (char *) sm_ch_name ((MOBJ) class_));
		}
	    }
	  else
	    {
	      if (has_comment)
		{
		  snprintf (name_buf, max_name_size, "%-20s COLLATE %s %s", sm_ch_name ((MOBJ) class_),
			    lang_get_collation_name (class_->collation_id), describe_comment (parser, class_->comment));
		}
	      else
		{
		  snprintf (name_buf, max_name_size, "%-20s COLLATE %s", sm_ch_name ((MOBJ) class_),
			    lang_get_collation_name (class_->collation_id));
		}
	    }
	  info->name = obj_print_copy_string (name_buf);
	}
      else
	{
	  /* 
	   * For the case prt_type == OBJ_PRINT_SHOW_CREATE_TABLE
	   * info->name is set to the exact class name
	   */
	  snprintf (name_buf, SM_MAX_IDENTIFIER_LENGTH + 2, "[%s]", sm_ch_name ((MOBJ) class_));
	  info->name = obj_print_copy_string (name_buf);
	}

      switch (class_->class_type)
	{
	default:
	  info->class_type =
	    obj_print_copy_string (msgcat_message
				   (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_HELP, MSGCAT_HELP_META_CLASS_HEADER));
	  break;
	case SM_CLASS_CT:
	  info->class_type =
	    obj_print_copy_string (msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_HELP, MSGCAT_HELP_CLASS_HEADER));
	  break;
	case SM_VCLASS_CT:
	  info->class_type =
	    obj_print_copy_string (msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_HELP, MSGCAT_HELP_VCLASS_HEADER));
	  break;
	}

      info->collation = obj_print_copy_string (lang_get_collation_name (class_->collation_id));
      if (info->collation == NULL)
	{
	  goto error_exit;
	}

      if (has_comment && prt_type != OBJ_PRINT_CSQL_SCHEMA_COMMAND)
	{
	  /* 
	   * For the case except "print schema",
	   * comment is copied to info->comment anyway
	   */
	  info->comment = obj_print_copy_string (class_->comment);
	  if (info->comment == NULL)
	    {
	      goto error_exit;
	    }
	}

      if (class_->inheritance != NULL)
	{
	  count = ws_list_length ((DB_LIST *) class_->inheritance);
	  buf_size = sizeof (char *) * (count + 1);
	  strs = (char **) malloc (buf_size);
	  if (strs == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, buf_size);
	      goto error_exit;
	    }
	  i = 0;
	  for (super = class_->inheritance; super != NULL; super = super->next)
	    {
	      /* kludge for const vs. non-const warnings */
	      kludge = sm_get_ch_name (super->op);
	      if (kludge == NULL)
		{
		  assert (er_errid () != NO_ERROR);
		  goto error_exit;
		}

	      if (prt_type == OBJ_PRINT_CSQL_SCHEMA_COMMAND)
		{
		  strs[i] = obj_print_copy_string ((char *) kludge);
		}
	      else
		{		/* prt_type == OBJ_PRINT_SHOW_CREATE_TABLE */
		  snprintf (name_buf, SM_MAX_IDENTIFIER_LENGTH + 2, "[%s]", kludge);
		  strs[i] = obj_print_copy_string (name_buf);
		}
	      i++;
	    }
	  strs[i] = NULL;
	  info->supers = strs;
	}

      if (class_->users != NULL)
	{
	  count = ws_list_length ((DB_LIST *) class_->users);
	  buf_size = sizeof (char *) * (count + 1);
	  strs = (char **) malloc (buf_size);
	  if (strs == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, buf_size);
	      goto error_exit;
	    }
	  i = 0;
	  for (user = class_->users; user != NULL; user = user->next)
	    {
	      /* kludge for const vs. non-const warnings */
	      kludge = sm_get_ch_name (user->op);
	      if (kludge == NULL)
		{
		  assert (er_errid () != NO_ERROR);
		  goto error_exit;
		}

	      if (prt_type == OBJ_PRINT_CSQL_SCHEMA_COMMAND)
		{
		  strs[i] = obj_print_copy_string ((char *) kludge);
		}
	      else
		{		/* prt_type == OBJ_PRINT_SHOW_CREATE_TABLE */
		  snprintf (name_buf, SM_MAX_IDENTIFIER_LENGTH + 2, "[%s]", kludge);
		  strs[i] = obj_print_copy_string (name_buf);
		}

	      i++;
	    }
	  strs[i] = NULL;
	  info->subs = strs;
	}

      if (class_->attributes != NULL || class_->shared != NULL)
	{
	  if (include_inherited)
	    {
	      count = class_->att_count + class_->shared_count;
	    }
	  else
	    {
	      count = 0;
	      /* find the number own by itself */
	      for (a = class_->ordered_attributes; a != NULL; a = a->order_link)
		{
		  if (a->class_mop == op)
		    {
		      count++;
		    }
		}
	    }

	  if (count > 0)
	    {
	      buf_size = sizeof (char *) * (count + 1);
	      strs = (char **) malloc (buf_size);
	      if (strs == NULL)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, buf_size);
		  goto error_exit;
		}

	      i = 0;
	      for (a = class_->ordered_attributes; a != NULL; a = a->order_link)
		{
		  if (include_inherited || (!include_inherited && a->class_mop == op))
		    {
		      description =
			obj_print_describe_attribute (op, parser, a, (a->class_mop != op), prt_type,
						      force_print_att_coll);
		      if (description == NULL)
			{
			  goto error_exit;
			}
		      strs[i] = obj_print_copy_string (description);
		      i++;
		    }
		}
	      strs[i] = NULL;
	      info->attributes = strs;
	    }
	}

      if (class_->class_attributes != NULL)
	{
	  if (include_inherited)
	    {
	      count = class_->class_attribute_count;
	    }
	  else
	    {
	      count = 0;
	      /* find the number own by itself */
	      for (a = class_->class_attributes; a != NULL; a = (SM_ATTRIBUTE *) a->header.next)
		{
		  if (a->class_mop == op)
		    {
		      count++;
		    }
		}
	    }

	  if (count > 0)
	    {
	      buf_size = sizeof (char *) * (count + 1);
	      strs = (char **) malloc (buf_size);
	      if (strs == NULL)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, buf_size);
		  goto error_exit;
		}

	      i = 0;
	      for (a = class_->class_attributes; a != NULL; a = (SM_ATTRIBUTE *) a->header.next)
		{
		  if (include_inherited || (!include_inherited && a->class_mop == op))
		    {
		      description =
			obj_print_describe_attribute (op, parser, a, (a->class_mop != op), prt_type,
						      force_print_att_coll);
		      if (description == NULL)
			{
			  goto error_exit;
			}
		      strs[i] = obj_print_copy_string (description);
		      i++;
		    }
		}
	      strs[i] = NULL;
	      info->class_attributes = strs;
	    }
	}

      if (class_->methods != NULL)
	{
	  if (include_inherited)
	    {
	      count = class_->method_count;
	    }
	  else
	    {
	      count = 0;
	      /* find the number own by itself */
	      for (m = class_->methods; m != NULL; m = (SM_METHOD *) m->header.next)
		{
		  if (m->class_mop == op)
		    {
		      count++;
		    }
		}
	    }

	  if (count > 0)
	    {
	      buf_size = sizeof (char *) * (count + 1);
	      strs = (char **) malloc (buf_size);
	      if (strs == NULL)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, buf_size);
		  goto error_exit;
		}
	      i = 0;
	      for (m = class_->methods; m != NULL; m = (SM_METHOD *) m->header.next)
		{
		  if (include_inherited || (!include_inherited && m->class_mop == op))
		    {
		      buffer = obj_print_describe_method (parser, op, m, prt_type);
		      strs[i] = obj_print_copy_string ((char *) pt_get_varchar_bytes (buffer));
		      i++;
		    }
		}
	      strs[i] = NULL;
	      info->methods = strs;
	    }
	}

      if (class_->class_methods != NULL)
	{
	  if (include_inherited)
	    {
	      count = class_->class_method_count;
	    }
	  else
	    {
	      count = 0;
	      /* find the number own by itself */
	      for (m = class_->class_methods; m != NULL; m = (SM_METHOD *) m->header.next)
		{
		  if (m->class_mop == op)
		    {
		      count++;
		    }
		}
	    }

	  if (count > 0)
	    {
	      buf_size = sizeof (char *) * (count + 1);
	      strs = (char **) malloc (buf_size);
	      if (strs == NULL)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, buf_size);
		  goto error_exit;
		}
	      i = 0;
	      for (m = class_->class_methods; m != NULL; m = (SM_METHOD *) m->header.next)
		{
		  if (include_inherited || (!include_inherited && m->class_mop == op))
		    {
		      buffer = obj_print_describe_method (parser, op, m, prt_type);
		      strs[i] = obj_print_copy_string ((char *) pt_get_varchar_bytes (buffer));
		      i++;
		    }
		}
	      strs[i] = NULL;
	      info->class_methods = strs;
	    }
	}

      if (class_->resolutions != NULL)
	{
	  count = ws_list_length ((DB_LIST *) class_->resolutions);
	  buf_size = sizeof (char *) * (count + 1);
	  strs = (char **) malloc (buf_size);
	  if (strs == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, buf_size);
	      goto error_exit;
	    }
	  i = 0;

	  for (r = class_->resolutions; r != NULL; r = r->next)
	    {
	      buffer = obj_print_describe_resolution (parser, r, prt_type);
	      strs[i] = obj_print_copy_string ((char *) pt_get_varchar_bytes (buffer));
	      i++;
	    }
	  strs[i] = NULL;
	  info->resolutions = strs;
	}

      if (class_->method_files != NULL)
	{
	  if (include_inherited)
	    {
	      count = ws_list_length ((DB_LIST *) class_->method_files);
	    }
	  else
	    {
	      count = 0;
	      /* find the number own by itself */
	      for (f = class_->method_files; f != NULL; f = f->next)
		{
		  if (f->class_mop == op)
		    {
		      count++;
		    }
		}
	    }

	  if (count > 0)
	    {
	      buf_size = sizeof (char *) * (count + 1);
	      strs = (char **) malloc (buf_size);
	      if (strs == NULL)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, buf_size);
		  goto error_exit;
		}
	      i = 0;
	      for (f = class_->method_files; f != NULL; f = f->next)
		{
		  if (include_inherited || (!include_inherited && f->class_mop == op))
		    {
		      buffer = obj_print_describe_method_file (parser, op, f);
		      strs[i] = obj_print_copy_string ((char *) pt_get_varchar_bytes (buffer));
		      i++;
		    }

		}
	      strs[i] = NULL;
	      info->method_files = strs;
	    }
	}

      if (class_->query_spec != NULL)
	{
	  count = ws_list_length ((DB_LIST *) class_->query_spec);
	  buf_size = sizeof (char *) * (count + 1);
	  strs = (char **) malloc (buf_size);
	  if (strs == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, buf_size);
	      goto error_exit;
	    }
	  i = 0;
	  for (p = class_->query_spec; p != NULL; p = p->next)
	    {
	      strs[i] = obj_print_copy_string ((char *) p->specification);
	      i++;
	    }
	  strs[i] = NULL;
	  info->query_spec = strs;
	}

      /* these are a bit more complicated */
      info->triggers = (char **) obj_print_describe_class_triggers (parser, class_, op);

      /* 
       *  Process multi-column class constraints (Unique and Indexes).
       *  Single column constraints (NOT NULL) are displayed along with
       *  the attributes.
       */
      info->constraints = NULL;	/* initialize */
      if (class_->constraints != NULL)
	{
	  SM_CLASS_CONSTRAINT *c;

	  count = 0;
	  for (c = class_->constraints; c; c = c->next)
	    {
	      if (SM_IS_CONSTRAINT_INDEX_FAMILY (c->type))
		{
		  /* Csql schema command will print all constraints, which include the constraints belong to the table
		   * itself and belong to the parent table. But show create table will only print the constraints which 
		   * belong to the table itself. */
		  if (include_inherited
		      || (!include_inherited && c->attributes[0] != NULL && c->attributes[0]->class_mop == op))
		    {
		      count++;
		    }
		}
	    }

	  if (count > 0)
	    {
	      buf_size = sizeof (char *) * (count + 1);
	      strs = (char **) malloc (buf_size);
	      if (strs == NULL)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, buf_size);
		  goto error_exit;
		}

	      i = 0;
	      for (c = class_->constraints; c; c = c->next)
		{
		  if (SM_IS_CONSTRAINT_INDEX_FAMILY (c->type))
		    {
		      if (include_inherited
			  || (!include_inherited && c->attributes[0] != NULL && c->attributes[0]->class_mop == op))
			{
			  description = obj_print_describe_constraint (parser, class_, c, prt_type);
			  strs[i] = obj_print_copy_string (description);
			  if (strs[i] == NULL)
			    {
			      info->constraints = strs;
			      goto error_exit;
			    }
			  i++;
			}
		    }
		}
	      strs[i] = NULL;
	      info->constraints = strs;
	    }
	}

      info->partition = NULL;	/* initialize */
      if (class_->partition != NULL && class_->partition->pname == NULL)
	{
	  bool is_print_partition = true;

	  count = 0;

	  /* Show create table will not print the sub partition for hash partition table. */
	  if (prt_type == OBJ_PRINT_SHOW_CREATE_TABLE)
	    {
	      is_print_partition = (class_->partition->partition_type != PT_PARTITION_HASH);
	    }

	  if (is_print_partition)
	    {
	      for (user = class_->users; user != NULL; user = user->next)
		{
		  if (au_fetch_class (user->op, &subclass, AU_FETCH_READ, AU_SELECT) != NO_ERROR)
		    {
		      goto error_exit;
		    }

		  if (subclass->partition)
		    {
		      description = obj_print_describe_partition_parts (parser, subclass->partition, prt_type);

		      /* Temporarily store it into STRLIST, later we will copy it into a fixed length array of which
		       * the size should be determined by the counter of this iteration. */
		      buf_size = sizeof (STRLIST);
		      tmp_str = (STRLIST *) malloc (buf_size);
		      if (tmp_str == NULL)
			{
			  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, buf_size);
			  goto error_exit;
			}

		      tmp_str->next = NULL;
		      tmp_str->string = description;

		      /* Whether it is the first node. */
		      if (str_list_head == NULL)
			{
			  /* Set the head of the list. */
			  str_list_head = tmp_str;
			}
		      else
			{
			  /* Link it at the end of the list. */
			  current_str->next = tmp_str;
			}

		      current_str = tmp_str;

		      count++;
		    }

		}
	    }

	  /* Allocate a fixed array to store the strings involving class-partition, sub-partitions and a NULL to
	   * indicate the end position. */
	  buf_size = sizeof (char *) * (count + 2);
	  strs = (char **) malloc (buf_size);
	  if (strs == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, buf_size);
	      goto error_exit;
	    }

	  memset (strs, 0, buf_size);

	  description = obj_print_describe_partition_info (parser, class_->partition);
	  strs[0] = obj_print_copy_string (description);

	  /* Copy all from the list into the array and release the list */
	  for (current_str = str_list_head, i = 1; current_str != NULL; i++)
	    {
	      strs[i] = obj_print_copy_string (current_str->string);

	      tmp_str = current_str;
	      current_str = current_str->next;

	      free_and_init (tmp_str);
	    }

	  strs[i] = NULL;
	  info->partition = strs;
	}

    }

  parser_free_parser (parser);
  parser = NULL;		/* Remember, it's a global! */
  return info;

error_exit:

  for (current_str = str_list_head; current_str != NULL;)
    {
      tmp_str = current_str;
      current_str = current_str->next;
      free_and_init (tmp_str);
    }

  if (info)
    {
      obj_print_help_free_class (info);
    }
  if (parser)
    {
      parser_free_parser (parser);
      parser = NULL;		/* Remember, it's a global! */
    }

  return NULL;
}

/*
 * obj_print_help_class_name () - Creates a class help structure for the named class.
 *   return:  class help structure
 *   name(in): class name
 *
 * Note:
 *    Must free the class help structure with obj_print_help_free_class() when
 *    finished.
 */

CLASS_HELP *
obj_print_help_class_name (const char *name)
{
  CLASS_HELP *help = NULL;
  DB_OBJECT *class_;

  /* look up class in all schema's */
  class_ = sm_find_class (name);

  if (class_ != NULL)
    {
      help = obj_print_help_class (class_, OBJ_PRINT_CSQL_SCHEMA_COMMAND);
    }

  return help;
}

/* TRIGGER HELP */

/*
 * obj_print_make_trigger_help () - Constructor for trigger help structure
 *   return: TRIGGER_HELP *
 */

static TRIGGER_HELP *
obj_print_make_trigger_help (void)
{
  TRIGGER_HELP *help_p;

  help_p = (TRIGGER_HELP *) malloc (sizeof (TRIGGER_HELP));
  if (help_p != NULL)
    {
      help_p->name = NULL;
      help_p->event = NULL;
      help_p->class_name = NULL;
      help_p->attribute = NULL;
      help_p->full_event = NULL;
      help_p->status = NULL;
      help_p->priority = NULL;
      help_p->condition_time = NULL;
      help_p->condition = NULL;
      help_p->action_time = NULL;
      help_p->action = NULL;
      help_p->comment = NULL;
    }
  return help_p;
}

/*
 * help_free_trigger () - Frees the help strcuture returned by help_trigger()
 *   return: none
 *   help(in): help structure
 */

void
help_free_trigger (TRIGGER_HELP * help)
{
  if (help != NULL)
    {

      /* these were allocated by this module and can be freed with free_and_init() */
      free_and_init (help->name);
      free_and_init (help->attribute);
      free_and_init (help->class_name);
      free_and_init (help->full_event);
      free_and_init (help->priority);
      if (help->comment != NULL)
	{
	  free_and_init (help->comment);
	}

      /* these were returned by the trigger manager and must be freed with db_string_free() */
      STRFREE_W (help->condition);
      STRFREE_W (help->action);

      /* These are constansts used by the trigger type to string translation functions above.  They don't need to be
       * freed.
       * 
       * event status condition_time action_time */

      free_and_init (help);
    }
}

/*
 * help_trigger () - Returns a help structure for the given trigger object.
 *   return: help structure
 *   trobj(in): trigger object
 */

TRIGGER_HELP *
help_trigger (DB_OBJECT * trobj)
{
  TRIGGER_HELP *help;
  char *condition = NULL, *action = NULL, *classname;
  TR_TRIGGER *trigger;
  char buffer[(SM_MAX_IDENTIFIER_LENGTH * 2) + 32];
  char temp_buffer[64];

  trigger = tr_map_trigger (trobj, 1);
  if (trigger == NULL)
    {
      return NULL;
    }

  /* even though we have the trigger, use these to get the expressions translated into a simple string */
  if (db_trigger_condition (trobj, &condition) != NO_ERROR)
    {
      goto exit_on_error;
    }
  if (db_trigger_action (trobj, &action) != NO_ERROR)
    {
      goto exit_on_error;
    }

  help = obj_print_make_trigger_help ();
  if (help == NULL)
    {
      goto exit_on_error;
    }

  /* copy these */
  help->name = obj_print_copy_string (trigger->name);
  help->attribute = obj_print_copy_string (trigger->attribute);
  help->comment = obj_print_copy_string (trigger->comment);

  /* these are already copies */
  help->condition = condition;
  help->action = action;

  /* these are constant strings that don't need to ever change */
  help->event = tr_event_as_string (trigger->event);
  help->condition_time = obj_print_trigger_condition_time (trigger);
  help->action_time = obj_print_trigger_action_time (trigger);

  /* only show status if its inactive */
  if (trigger->status != TR_STATUS_ACTIVE)
    {
      help->status = tr_status_as_string (trigger->status);
    }

  /* if its 0, leave it out */
  if (trigger->priority != 0.0)
    {
      sprintf (temp_buffer, "%f", trigger->priority);
      help->priority = obj_print_copy_string (temp_buffer);
    }

  if (trigger->class_mop != NULL)
    {
      classname = (char *) sm_get_ch_name (trigger->class_mop);
      if (classname != NULL)
	{
	  help->class_name = obj_print_copy_string ((char *) classname);
	}
      else
	{
	  help->class_name = obj_print_copy_string ("*** deleted class ***");
	}

      /* format the full event specification so csql can display it without being dependent on syntax */
      if (help->attribute != NULL)
	{
	  sprintf (buffer, "%s ON %s(%s)", help->event, help->class_name, help->attribute);
	}
      else
	{
	  sprintf (buffer, "%s ON %s", help->event, help->class_name);
	}
      help->full_event = obj_print_copy_string (buffer);
    }
  else
    {
      /* just make a copy of this so csql can simply use it without thinking */
      help->full_event = obj_print_copy_string ((char *) help->event);
    }

  return help;

exit_on_error:
  if (condition != NULL)
    {
      ws_free_string (condition);
    }
  if (action != NULL)
    {
      ws_free_string (action);
    }
  return NULL;
}

/*
 * help_trigger_name () - Returns a help strcuture for the named trigger
 *   return: help structure
 *   name(in): trigger name
 */

TRIGGER_HELP *
help_trigger_name (const char *name)
{
  TRIGGER_HELP *help;
  DB_OBJECT *trigger;

  help = NULL;
  trigger = tr_find_trigger (name);
  if (trigger != NULL)
    {
      help = help_trigger (trigger);
    }

  return help;
}

/*
 * help_trigger_names () - Returns an array of strings
 *   return: error code
 *   names_ptr(in):
 *
 * Note :
 *    Returns an array of strings.  Each string is the name of
 *    a trigger accessible to the current user.
 *    The array must be freed with help_free_names().
 *    Changed to return an error and return the names through an
 *    argument so we can tell the difference between a system error
 *    and the absense of triggers.
 *    Class names should be the same but we always have classes in the
 *    system so it doesn't really matter.
 */

int
help_trigger_names (char ***names_ptr)
{
  int error = NO_ERROR;
  DB_OBJLIST *triggers, *t;
  char **names;
  char *name;
  int count, i;
  size_t buf_size;

  names = NULL;

  /* this should filter the list based on the current user */
  error = tr_find_all_triggers (&triggers);
  if (error == NO_ERROR)
    {
      count = ws_list_length ((DB_LIST *) triggers);
      if (count)
	{
	  buf_size = sizeof (char *) * (count + 1);
	  names = (char **) malloc (buf_size);
	  if (names == NULL)
	    {
	      error = ER_OUT_OF_VIRTUAL_MEMORY;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, buf_size);
	    }
	  else
	    {
	      for (i = 0, t = triggers; t != NULL; t = t->next)
		{
		  if (tr_trigger_name (t->op, &name) == NO_ERROR)
		    {
		      names[i] = obj_print_copy_string ((char *) name);
		      i++;

		      ws_free_string (name);
		    }
		}
	      names[i] = NULL;
	    }
	}
      if (triggers != NULL)
	{
	  db_objlist_free (triggers);
	}
    }
  *names_ptr = names;
  return error;
}

/*
 * help_print_trigger () - Debug function, primarily for help_print_info,
 *                         can be useful in the debugger as well.
 *                         Display the description of a trigger to stdout.
 *   return: none
 *   name(in): trigger name
 *   fpp(in):
 */

void
help_print_trigger (const char *name, FILE * fpp)
{
  TRIGGER_HELP *help;

  help = help_trigger_name (name);
  if (help != NULL)
    {
      fprintf (fpp, "Trigger   : %s\n", help->name);

      if (help->status != NULL)
	{
	  fprintf (fpp, "Status    : %s\n", help->status);
	}

      if (help->priority != NULL)
	{
	  fprintf (fpp, "Priority  : %s\n", help->priority);
	}

      fprintf (fpp, "Event     : %s %s\n", help->condition_time, help->full_event);

      if (help->condition != NULL)
	{
	  fprintf (fpp, "Condition : %s\n", help->condition);
	}

      if (help->condition_time != help->action_time)
	{
	  fprintf (fpp, "Action    : %s %s\n", help->action_time, help->action);
	}
      else
	{
	  fprintf (fpp, "Action    : %s\n", help->action);
	}

      if (help->comment != NULL)
	{
	  fprintf (fpp, "Comment '%s'\n", help->comment);
	}

      help_free_trigger (help);
    }
}

/* INSTANCE HELP */

/*
 * obj_print_make_obj_help () - Create an empty instance help structure
 *   return: instance help structure
 */

static OBJ_HELP *
obj_print_make_obj_help (void)
{
  OBJ_HELP *new_p;

  new_p = (OBJ_HELP *) malloc (sizeof (OBJ_HELP));
  if (new_p != NULL)
    {
      new_p->classname = NULL;
      new_p->oid = NULL;
      new_p->attributes = NULL;
      new_p->shared = NULL;
    }
  return new_p;
}

/*
 * help_free_obj () - Frees an instance help structure that was built
 *                    by help_obj()
 *   return:
 *   info(in): instance help structure
 */

void
help_free_obj (OBJ_HELP * info)
{
  if (info != NULL)
    {
      free_and_init (info->classname);
      free_and_init (info->oid);
      obj_print_free_strarray (info->attributes);
      obj_print_free_strarray (info->shared);
      free_and_init (info);
    }
}

/*
 * help_obj () - Builds an instance help structure containing a textual
 *               description of the instance.
 *   return: instance help structure
 *   op(in): instance object
 *
 * Note :
 *    The structure must be freed with help_free_obj() when finished.
 */

OBJ_HELP *
help_obj (MOP op)
{
  int error;
  SM_CLASS *class_;
  SM_ATTRIBUTE *attribute_p;
  char *obj;
  int i, count, is_class = 0;
  OBJ_HELP *info = NULL;
  char **strs;
  char temp_buffer[SM_MAX_IDENTIFIER_LENGTH + 4];	/* Include room for _=_\0 */
  int pin;
  size_t buf_size;
  DB_VALUE value;
  PARSER_VARCHAR *buffer;

  if (parser == NULL)
    {
      parser = parser_create_parser ();
    }
  if (parser == NULL)
    {
      goto error_exit;
    }

  buffer = NULL;

  if (op != NULL)
    {
      is_class = locator_is_class (op, DB_FETCH_READ);
      if (is_class < 0)
	{
	  goto error_exit;
	}
    }
  if (op == NULL || is_class)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
      goto error_exit;
    }
  else
    {
      error = au_fetch_instance (op, &obj, AU_FETCH_READ, TM_TRAN_READ_FETCH_VERSION (), AU_SELECT);
      if (error == NO_ERROR)
	{
	  pin = ws_pin (op, 1);
	  error = au_fetch_class (ws_class_mop (op), &class_, AU_FETCH_READ, AU_SELECT);
	  if (error == NO_ERROR)
	    {

	      info = obj_print_make_obj_help ();
	      if (info == NULL)
		{
		  goto error_exit;
		}
	      info->classname = obj_print_copy_string ((char *) sm_ch_name ((MOBJ) class_));

	      DB_MAKE_OBJECT (&value, op);
	      buffer = pt_append_varchar (parser, buffer, describe_data (parser, buffer, &value));
	      db_value_clear (&value);
	      DB_MAKE_NULL (&value);

	      info->oid = obj_print_copy_string ((char *) pt_get_varchar_bytes (buffer));

	      if (class_->ordered_attributes != NULL)
		{
		  count = class_->att_count + class_->shared_count + 1;
		  buf_size = sizeof (char *) * count;
		  strs = (char **) malloc (buf_size);
		  if (strs == NULL)
		    {
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, buf_size);
		      goto error_exit;
		    }
		  i = 0;
		  for (attribute_p = class_->ordered_attributes; attribute_p != NULL;
		       attribute_p = attribute_p->order_link)
		    {
		      sprintf (temp_buffer, "%20s = ", attribute_p->header.name);
		      /* 
		       * We're starting a new line here, so we don't
		       * want to append to the old buffer; pass NULL
		       * to pt_append_nulstring so that we start a new
		       * string.
		       */
		      buffer = pt_append_nulstring (parser, NULL, temp_buffer);
		      if (attribute_p->header.name_space == ID_SHARED_ATTRIBUTE)
			{
			  buffer = describe_value (parser, buffer, &attribute_p->default_value.value);
			}
		      else
			{
			  db_get (op, attribute_p->header.name, &value);
			  buffer = describe_value (parser, buffer, &value);
			}
		      strs[i] = obj_print_copy_string ((char *) pt_get_varchar_bytes (buffer));
		      i++;
		    }
		  strs[i] = NULL;
		  info->attributes = strs;
		}

	      /* will we ever want to separate these lists ? */
	    }
	  (void) ws_pin (op, pin);
	}
    }
  parser_free_parser (parser);
  parser = NULL;
  return info;

error_exit:
  if (info)
    {
      help_free_obj (info);
    }
  if (parser)
    {
      parser_free_parser (parser);
      parser = NULL;
    }
  return NULL;
}

/* HELP PRINTING */
/* These functions build help structures and print them to a file. */

/*
 * help_fprint_obj () - Prints the description of a class or instance object
 *                      to the file.
 *   return: none
 *   fp(in):file pointer
 *   obj(in):class or instance to describe
 */

void
help_fprint_obj (FILE * fp, MOP obj)
{
  CLASS_HELP *cinfo;
  OBJ_HELP *oinfo;
  TRIGGER_HELP *tinfo;
  int i, status;

  status = locator_is_class (obj, DB_FETCH_READ);
  if (status < 0)
    {
      return;
    }
  if (status > 0)
    {
      if (locator_is_root (obj))
	{
	  fprintf (fp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_HELP, MSGCAT_HELP_ROOTCLASS_TITLE));
	}
      else
	{
	  cinfo = obj_print_help_class (obj, OBJ_PRINT_CSQL_SCHEMA_COMMAND);
	  if (cinfo != NULL)
	    {
	      fprintf (fp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_HELP, MSGCAT_HELP_CLASS_TITLE),
		       cinfo->class_type, cinfo->name);
	      if (cinfo->supers != NULL)
		{
		  fprintf (fp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_HELP, MSGCAT_HELP_SUPER_CLASSES));
		  for (i = 0; cinfo->supers[i] != NULL; i++)
		    {
		      fprintf (fp, "  %s\n", cinfo->supers[i]);
		    }
		}
	      if (cinfo->subs != NULL)
		{
		  fprintf (fp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_HELP, MSGCAT_HELP_SUB_CLASSES));
		  for (i = 0; cinfo->subs[i] != NULL; i++)
		    {
		      fprintf (fp, "  %s\n", cinfo->subs[i]);
		    }
		}
	      if (cinfo->attributes != NULL)
		{
		  fprintf (fp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_HELP, MSGCAT_HELP_ATTRIBUTES));
		  for (i = 0; cinfo->attributes[i] != NULL; i++)
		    {
		      fprintf (fp, "  %s\n", cinfo->attributes[i]);
		    }
		}
	      if (cinfo->methods != NULL)
		{
		  fprintf (fp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_HELP, MSGCAT_HELP_METHODS));
		  for (i = 0; cinfo->methods[i] != NULL; i++)
		    {
		      fprintf (fp, "  %s\n", cinfo->methods[i]);
		    }
		}
	      if (cinfo->class_attributes != NULL)
		{
		  fprintf (fp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_HELP, MSGCAT_HELP_CLASS_ATTRIBUTES));
		  for (i = 0; cinfo->class_attributes[i] != NULL; i++)
		    {
		      fprintf (fp, "  %s\n", cinfo->class_attributes[i]);
		    }
		}
	      if (cinfo->class_methods != NULL)
		{
		  fprintf (fp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_HELP, MSGCAT_HELP_CLASS_METHODS));
		  for (i = 0; cinfo->class_methods[i] != NULL; i++)
		    {
		      fprintf (fp, "  %s\n", cinfo->class_methods[i]);
		    }
		}
	      if (cinfo->resolutions != NULL)
		{
		  fprintf (fp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_HELP, MSGCAT_HELP_RESOLUTIONS));
		  for (i = 0; cinfo->resolutions[i] != NULL; i++)
		    {
		      fprintf (fp, "  %s\n", cinfo->resolutions[i]);
		    }
		}
	      if (cinfo->method_files != NULL)
		{
		  fprintf (fp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_HELP, MSGCAT_HELP_METHOD_FILES));
		  for (i = 0; cinfo->method_files[i] != NULL; i++)
		    {
		      fprintf (fp, "  %s\n", cinfo->method_files[i]);
		    }
		}
	      if (cinfo->query_spec != NULL)
		{
		  fprintf (fp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_HELP, MSGCAT_HELP_QUERY_SPEC));
		  for (i = 0; cinfo->query_spec[i] != NULL; i++)
		    {
		      fprintf (fp, "  %s\n", cinfo->query_spec[i]);
		    }
		}
	      if (cinfo->triggers != NULL)
		{
		  /* fprintf(fp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_HELP, MSGCAT_HELP_TRIGGERS)); */
		  fprintf (fp, "Triggers:\n");
		  for (i = 0; cinfo->triggers[i] != NULL; i++)
		    {
		      fprintf (fp, "  %s\n", cinfo->triggers[i]);
		    }
		}

	      obj_print_help_free_class (cinfo);
	    }
	}
    }
  else
    {
      (void) tr_is_trigger (obj, &status);
      if (status)
	{
	  tinfo = help_trigger (obj);
	  if (tinfo != NULL)
	    {
	      fprintf (fp, "Trigger : %s", tinfo->name);
	      if (tinfo->status)
		{
		  fprintf (fp, " (INACTIVE)\n");
		}
	      else
		{
		  fprintf (fp, "\n");
		}

	      fprintf (fp, "  %s %s ", tinfo->condition_time, tinfo->event);
	      if (tinfo->class_name != NULL)
		{
		  if (tinfo->attribute != NULL)
		    {
		      fprintf (fp, "%s ON %s ", tinfo->attribute, tinfo->class_name);
		    }
		  else
		    {
		      fprintf (fp, "ON %s ", tinfo->class_name);
		    }
		}

	      fprintf (fp, "PRIORITY %s\n", tinfo->priority);

	      if (tinfo->condition)
		{
		  fprintf (fp, "  IF %s\n", tinfo->condition);
		}

	      if (tinfo->action != NULL)
		{
		  fprintf (fp, "  EXECUTE ");
		  if (strcmp (tinfo->condition_time, tinfo->action_time) != 0)
		    {
		      fprintf (fp, "%s ", tinfo->action_time);
		    }
		  fprintf (fp, "%s\n", tinfo->action);
		}

	      if (tinfo->comment != NULL && tinfo->comment[0] != '\0')
		{
		  fprintf (fp, " ");
		  help_fprint_describe_comment (fp, tinfo->comment);
		  fprintf (fp, "\n");
		}

	      help_free_trigger (tinfo);
	    }
	}
      else
	{
	  oinfo = help_obj (obj);
	  if (oinfo != NULL)
	    {
	      fprintf (fp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_HELP, MSGCAT_HELP_OBJECT_TITLE),
		       oinfo->classname);
	      if (oinfo->attributes != NULL)
		{
		  for (i = 0; oinfo->attributes[i] != NULL; i++)
		    {
		      fprintf (fp, "%s\n", oinfo->attributes[i]);
		    }
		}
	      if (oinfo->shared != NULL)
		{
		  for (i = 0; oinfo->shared[i] != NULL; i++)
		    {
		      fprintf (fp, "%s\n", oinfo->shared[i]);
		    }
		}
	      help_free_obj (oinfo);
	    }
	}
    }
}

/* CLASS LIST HELP */

/*
 * help_class_names () - Returns an array containing the names of
 *                       all classes in the system.
 *   return: array of name strings
 *   qualifier(in):
 *
 *  Note :
 *    The array must be freed with help_free_class_names().
 */

char **
help_class_names (const char *qualifier)
{
  DB_OBJLIST *mops, *m;
  char **names, *tmp;
  const char *cname;
  int count, i, outcount;
  DB_OBJECT *requested_owner, *owner;
  char buffer[2 * DB_MAX_IDENTIFIER_LENGTH + 4];
  DB_VALUE owner_name;

  requested_owner = NULL;
  owner = NULL;
  if (qualifier && *qualifier && strcmp (qualifier, "*") != 0)
    {
      /* look up class in qualifiers' schema */
      requested_owner = db_find_user (qualifier);
      /* if this guy does not exist, it has no classes */
      if (!requested_owner)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_AU_INVALID_USER, 1, qualifier);
	  return NULL;
	}
    }

  names = NULL;
  mops = db_fetch_all_classes (DB_FETCH_READ);

  /* vector fetch as many as possible (void)db_fetch_list(mops, DB_FETCH_READ, 0); */

  count = ws_list_length ((DB_LIST *) mops);
  outcount = 0;
  if (count)
    {
      names = (char **) malloc (sizeof (char *) * (count + 1));
      if (names != NULL)
	{
	  for (i = 0, m = mops; i < count; i++, m = m->next)
	    {
	      owner = db_get_owner (m->op);
	      if (!requested_owner || ws_is_same_object (requested_owner, owner))
		{
		  cname = db_get_class_name (m->op);
		  buffer[0] = '\0';
		  if (!requested_owner && db_get (owner, "name", &owner_name) >= 0)
		    {
		      tmp = DB_GET_STRING (&owner_name);
		      if (tmp)
			{
			  snprintf (buffer, sizeof (buffer) - 1, "%s.%s", tmp, cname);
			}
		      else
			{
			  snprintf (buffer, sizeof (buffer) - 1, "%s.%s", "unknown_user", cname);
			}
		      db_value_clear (&owner_name);
		    }
		  else
		    {
		      snprintf (buffer, sizeof (buffer) - 1, "%s", cname);
		    }

		  names[outcount++] = obj_print_copy_string (buffer);
		}
	    }
	  names[outcount] = NULL;
	}
    }
  if (mops != NULL)
    {
      db_objlist_free (mops);
    }

  return names;
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * help_base_class_names () - Returns an array containing the names of
 *                            all base classes in the system.
 *   return: array of name strings
 *  Note :
 *      A "base class" is a class that has no super classes.
 *      The array must be freed with help_free_class_names().
 */

char **
help_base_class_names (void)
{
  DB_OBJLIST *mops, *m;
  char **names;
  const char *cname;
  int count, i;

  names = NULL;
  mops = db_get_base_classes ();
  /* vector fetch as many as possible */
  (void) db_fetch_list (mops, DB_FETCH_READ, 0);

  count = ws_list_length ((DB_LIST *) mops);
  if (count)
    {
      names = (char **) malloc (sizeof (char *) * (count + 1));
      if (names != NULL)
	{
	  for (i = 0, m = mops; i < count; i++, m = m->next)
	    {
	      cname = db_get_class_name (m->op);
	      names[i] = obj_print_copy_string ((char *) cname);
	    }
	  names[count] = NULL;
	}
    }
  if (mops != NULL)
    {
      db_objlist_free (mops);
    }

  return names;
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * help_free_names () - Frees an array of class names built by
 *                      help_class_names() or help_base_class_names().
 *   return: class name array
 *   names(in): class name array
 */

void
help_free_names (char **names)
{
  if (names != NULL)
    {
      obj_print_free_strarray (names);
    }
}

/*
 * backward compatibility, should be using help_free_names() for all
 * name arrays.
 */

/*
 * help_free_class_names () -
 *   return: none
 *   names(in):
 */

void
help_free_class_names (char **names)
{
  help_free_names (names);
}

/*
 * help_fprint_class_names () - Prints the names of all classes
 *                              in the system to a file.
 *   return: none
 *   fp(in): file pointer
 *   qualifier(in):
 */

void
help_fprint_class_names (FILE * fp, const char *qualifier)
{
  char **names;
  int i;

  names = help_class_names (qualifier);
  if (names != NULL)
    {
      for (i = 0; names[i] != NULL; i++)
	{
	  fprintf (fp, "%s\n", names[i]);
	}
      help_free_class_names (names);
    }
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * help_print_class_names () - Prints the names of all classes
 *                             in the system to stdout.
 *   return: none
 *   qualifier(in):
 */

void
help_print_class_names (const char *qualifier)
{
  help_fprint_class_names (stdout, qualifier);
}
#endif /* ENABLE_UNUSED_FUNCTION */


/* MISC HELP FUNCTIONS */

/*
 * help_describe_mop () - This writes a description of the MOP
 *                        to the given buffer.
 *   return:  number of characters in the description
 *   obj(in): object pointer to describe
 *   buffer(in): buffer to contain the description
 *   maxlen(in): length of the buffer
 *
 * Note :
 *    Used to get a printed representation of a MOP.
 *    This should only be used in special cases since OID's aren't
 *    supposed to be visible.
 */

int
help_describe_mop (DB_OBJECT * obj, char *buffer, int maxlen)
{
  SM_CLASS *class_;
  char oidbuffer[64];		/* three integers, better be big enough */
  int required, total;

  total = 0;
  if ((buffer != NULL) && (obj != NULL) && (maxlen > 0))
    {
      if (au_fetch_class (obj, &class_, AU_FETCH_READ, AU_SELECT) == NO_ERROR)
	{
	  sprintf (oidbuffer, "%ld.%ld.%ld", (DB_C_LONG) WS_OID (obj)->volid, (DB_C_LONG) WS_OID (obj)->pageid,
		   (DB_C_LONG) WS_OID (obj)->slotid);

	  required = strlen (oidbuffer) + strlen (sm_ch_name ((MOBJ) class_)) + 2;
	  if (locator_is_class (obj, DB_FETCH_READ) > 0)
	    {
	      required++;
	      if (maxlen >= required)
		{
		  sprintf (buffer, "*%s:%s", sm_ch_name ((MOBJ) class_), oidbuffer);
		  total = required;
		}
	    }
	  else if (maxlen >= required)
	    {
	      sprintf (buffer, "%s:%s", sm_ch_name ((MOBJ) class_), oidbuffer);
	      total = required;
	    }
	}
    }
  return total;
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * help_fprint_all_classes () - Describe all classes in the system.
 *   return: none
 *   fp(in): file pointer
 *
 * Note:
 *    This should only be used for debugging and testing.
 *    It is not intended to be used by the API.
 */

void
help_fprint_all_classes (FILE * fp)
{
  LIST_MOPS *lmops;
  int i;

  if (au_check_user () == NO_ERROR)
    {
      lmops = locator_get_all_mops (sm_Root_class_mop, DB_FETCH_QUERY_READ, NULL);
      if (lmops != NULL)
	{
	  for (i = 0; i < lmops->num; i++)
	    {
	      if (!WS_IS_DELETED (lmops->mops[i]))
		{
		  help_fprint_obj (fp, lmops->mops[i]);
		}
	    }
	  locator_free_list_mops (lmops);
	}
    }
}

/*
 * help_fprint_resident_instances () - Describe all resident instances of
 *                                     a class.
 *   return: none
 *   fp(in): file
 *   op(in): class object
 *
 * Note:
 *    Describe all resident instances of a class.
 *    Should only be used for testing purposes.  Not intended to be
 *    called by the API.
 */

void
help_fprint_resident_instances (FILE * fp, MOP op)
{
  MOP classmop = NULL;
  SM_CLASS *class_;
  LIST_MOPS *lmops;
  int i;

  if (locator_is_class (op, DB_FETCH_QUERY_READ))
    {
      if (!WS_IS_DELETED (op))
	{
	  classmop = op;
	}
    }
  else
    {
      if (au_fetch_class (op, &class_, AU_FETCH_READ, AU_SELECT) == NO_ERROR)
	{
	  classmop = op->class_mop;
	}
    }

  if (classmop != NULL)
    {
      /* cause the mops to be loaded into the workspace */
      lmops = locator_get_all_mops (classmop, DB_FETCH_QUERY_READ);
      if (lmops != NULL)
	{
	  for (i = 0; i < lmops->num; i++)
	    {
	      if (!WS_IS_DELETED (lmops->mops[i]))
		{
		  help_fprint_obj (fp, lmops->mops[i]);
		}
	    }
	  locator_free_list_mops (lmops);
	}
    }
}
#endif /* ENABLE_UNUSED_FUNCTION */

/* GENERAL INFO */

/*
 * This is used to dump random information about the database
 * to the standard output device.  The information requested
 * comes in as a string that is "parsed" to determine the nature
 * of the request.  This is intended primarily as a backdoor
 * for the "info" method on the root class.  This allows us
 * to get information dumped to stdout from batch CSQL
 * files which isn't possible currently since session commands
 * aren't allowed.
 *
 * The recognized commands are:
 *
 *   schema		display the names of all classes (;schema)
 *   schema foo		display the definition of class foo (;schema foo)
 *   trigger		display the names of all triggers (;trigger)
 *   trigger foo	display the definition of trigger foo (;trigger foo)
 *   workspace		dump the workspace statistics
 *
 */

/*
 * Little tokenizing hack for help_display_info.
 */

/*
 * obj_print_next_token () -
 *   return: char *
 *   ptr(in):
 *   buffer(in):
 */

static char *
obj_print_next_token (char *ptr, char *buffer)
{
  char *p;

  p = ptr;
  while (char_isspace ((DB_C_INT) * p) && *p != '\0')
    {
      p++;
    }
  while (!char_isspace ((DB_C_INT) * p) && *p != '\0')
    {
      *buffer = *p;
      buffer++;
      p++;
    }
  *buffer = '\0';

  return p;
}

/*
 * help_print_info () -
 *   return: none
 *   command(in):
 *   fpp(in):
 */

void
help_print_info (const char *command, FILE * fpp)
{
  char buffer[128];
  char *ptr;
  DB_OBJECT *class_mop;
  char **names;
  int i;

  if (command == NULL)
    {
      return;
    }

  ptr = obj_print_next_token ((char *) command, buffer);
  if (fpp == NULL)
    {
      fpp = stdout;
    }

  if (MATCH_TOKEN (buffer, "schema"))
    {
      ptr = obj_print_next_token (ptr, buffer);
      if (!strlen (buffer))
	{
	  help_fprint_class_names (fpp, NULL);
	}
      else
	{
	  class_mop = db_find_class (buffer);
	  if (class_mop != NULL)
	    {
	      help_fprint_obj (fpp, class_mop);
	    }
	}
    }
  else if (MATCH_TOKEN (buffer, "trigger"))
    {
      ptr = obj_print_next_token (ptr, buffer);
      if (!strlen (buffer))
	{
	  if (!help_trigger_names (&names))
	    {
	      if (names == NULL)
		{
		  fprintf (fpp, "No triggers defined.\n");
		}
	      else
		{
		  fprintf (fpp, "Triggers: \n");
		  for (i = 0; names[i] != NULL; i++)
		    {
		      fprintf (fpp, "  %s\n", names[i]);
		    }
		  help_free_names (names);
		}
	    }
	}
      else
	help_print_trigger (buffer, fpp);
    }
  else if (MATCH_TOKEN (buffer, "deferred"))
    {
      tr_dump (fpp);
    }
  else if (MATCH_TOKEN (buffer, "workspace"))
    {
      ws_dump (fpp);
    }
  else if (MATCH_TOKEN (buffer, "lock"))
    {
      lock_dump (fpp);
    }
  else if (MATCH_TOKEN (buffer, "stats"))
    {
      ptr = obj_print_next_token (ptr, buffer);
      if (!strlen (buffer))
	{
	  fprintf (fpp, "Info stats class-name\n");
	}
      else
	{
	  stats_dump (buffer, fpp);
	}
    }
  else if (MATCH_TOKEN (buffer, "logstat"))
    {
      log_dump_stat (fpp);
    }
  else if (MATCH_TOKEN (buffer, "csstat"))
    {
      thread_dump_cs_stat (fpp);
    }
  else if (MATCH_TOKEN (buffer, "plan"))
    {
      qmgr_dump_query_plans (fpp);
    }
  else if (MATCH_TOKEN (buffer, "qcache"))
    {
      qmgr_dump_query_cache (fpp);
    }
  else if (MATCH_TOKEN (buffer, "trantable"))
    {
      logtb_dump_trantable (fpp);
    }
}

#endif /* ! SERVER_MODE */
/*
 * describe_set() - Print a description of the set
 *                  as null-terminated string
 *   return: printed string (buffer pointer)
 *   parser(in) : parser context
 *   buffer(in/out) : buffer to place the printed string
 *   set(in) :
 */
static PARSER_VARCHAR *
describe_set (const PARSER_CONTEXT * parser, PARSER_VARCHAR * buffer, const DB_SET * set)
{
  DB_VALUE value;
  int size, end, i;

  assert (parser != NULL && set != NULL);

  buffer = pt_append_nulstring (parser, buffer, "{");
  size = set_size ((DB_COLLECTION *) set);
  if (help_Max_set_elements == 0 || help_Max_set_elements > size)
    {
      end = size;
    }
  else
    {
      end = help_Max_set_elements;
    }

  for (i = 0; i < end; ++i)
    {
      set_get_element ((DB_COLLECTION *) set, i, &value);

      buffer = describe_value (parser, buffer, &value);

      db_value_clear (&value);
      if (i < size - 1)
	{
	  buffer = pt_append_nulstring (parser, buffer, ", ");
	}
    }
  if (i < size)
    {
      buffer = pt_append_nulstring (parser, buffer, ". . .");
    }

  buffer = pt_append_nulstring (parser, buffer, "}");
  return buffer;
}

/*
 * describe_midxkey() - Print a description of the midxkey
 *                      as null-terminated string
 *   return: printed string (buffer pointer)
 *   parser(in) : parser context
 *   buffer(in/out) : buffer to place the printed string
 *   midxkey(in) :
 */
static PARSER_VARCHAR *
describe_midxkey (const PARSER_CONTEXT * parser, PARSER_VARCHAR * buffer, const DB_MIDXKEY * midxkey)
{
  DB_VALUE value;
  int size, end, i;
  int prev_i_index;
  char *prev_i_ptr;

  assert (parser != NULL && midxkey != NULL);

  buffer = pt_append_nulstring (parser, buffer, "{");
  size = midxkey->ncolumns;
  if (help_Max_set_elements == 0 || help_Max_set_elements > size)
    {
      end = size;
    }
  else
    {
      end = help_Max_set_elements;
    }

  prev_i_index = 0;
  prev_i_ptr = NULL;
  for (i = 0; i < end; i++)
    {
      pr_midxkey_get_element_nocopy (midxkey, i, &value, &prev_i_index, &prev_i_ptr);

      buffer = describe_value (parser, buffer, &value);

      if (i < size - 1)
	{
	  buffer = pt_append_nulstring (parser, buffer, ", ");
	}

      if (!DB_IS_NULL (&value) && value.need_clear == true)
	{
	  pr_clear_value (&value);
	}
    }
  if (i < size)
    {
      buffer = pt_append_nulstring (parser, buffer, ". . .");
    }

  buffer = pt_append_nulstring (parser, buffer, "}");
  return buffer;
}

/*
 * describe_double() - Print a description of the double value
 *                     as null-terminated string
 *   return: printed string (buffer pointer)
 *   parser(in) : parser context
 *   buffer(in/out) : buffer to place the printed string
 *   value(in) :
 */
static PARSER_VARCHAR *
describe_double (const PARSER_CONTEXT * parser, PARSER_VARCHAR * buffer, const double value)
{
  char tbuf[24];

  assert (parser != NULL);

  OBJ_SPRINT_DB_DOUBLE (tbuf, value);

  if (strstr (tbuf, "Inf"))
    {
      OBJ_SPRINT_DB_DOUBLE (tbuf, (value > 0 ? DBL_MAX : -DBL_MAX));
    }

  buffer = pt_append_nulstring (parser, buffer, tbuf);

  return buffer;
}

/*
 * describe_float() -  Print a description of the float value
 *                     as null-terminated string
 *   return: printed string (buffer pointer)
 *   parser(in) : parser context
 *   buffer(in/out) : buffer to place the printed string
 *   value(in) :
 */
static PARSER_VARCHAR *
describe_float (const PARSER_CONTEXT * parser, PARSER_VARCHAR * buffer, const float value)
{
  char tbuf[24];

  assert (parser != NULL);

  OBJ_SPRINT_DB_FLOAT (tbuf, value);

  if (strstr (tbuf, "Inf"))
    {
      OBJ_SPRINT_DB_FLOAT (tbuf, (value > 0 ? FLT_MAX : -FLT_MAX));
    }

  buffer = pt_append_nulstring (parser, buffer, tbuf);
  return buffer;
}

/*
 * describe_money() - Print a description of the monetary value
 *                    as null-terminated string
 *   return: printed string (buffer pointer)
 *   parser(in) : parser context
 *   buffer(in/out) : buffer to place the printed string
 *   value(in) :
 */
PARSER_VARCHAR *
describe_money (const PARSER_CONTEXT * parser, PARSER_VARCHAR * buffer, const DB_MONETARY * value)
{
  char cbuf[LDBL_MAX_10_EXP + 20 + 1];
  /* 20 == floating fudge factor, 1 == currency symbol */

  assert (parser != NULL && value != NULL);

  sprintf (cbuf, "%s%.2f", intl_get_money_esc_ISO_symbol (value->type), value->amount);

  if (strstr (cbuf, "Inf"))
    {
      sprintf (cbuf, "%s%.2f", intl_get_money_esc_ISO_symbol (value->type), (value->amount > 0 ? DBL_MAX : -DBL_MAX));
    }

  buffer = pt_append_nulstring (parser, buffer, cbuf);
  return buffer;
}

/*
 * describe_bit_string() - Print a description of the bit value
 *                         as null-terminated string
 *   return: printed string (buffer pointer)
 *   parser(in) : parser context
 *   buffer(in/out) : buffer to place the printed string
 *   value(in) : a DB_VALUE of type DB_TYPE_BIT or DB_TYPE_VARBIT
 */
static PARSER_VARCHAR *
describe_bit_string (const PARSER_CONTEXT * parser, PARSER_VARCHAR * buffer, const DB_VALUE * value)
{
  unsigned char *bstring;
  int nibble_length, nibbles, count;
  char tbuf[10];

  assert (parser != NULL && value != NULL);

  bstring = (unsigned char *) db_get_string (value);
  if (bstring == NULL)
    {
      return NULL;
    }

  nibble_length = ((db_get_string_length (value) + 3) / 4);

  for (nibbles = 0, count = 0; nibbles < nibble_length - 1; count++, nibbles += 2)
    {
      sprintf (tbuf, "%02x", bstring[count]);
      tbuf[2] = '\0';
      buffer = pt_append_nulstring (parser, buffer, tbuf);
    }

  /* If we don't have a full byte on the end, print the nibble. */
  if (nibbles < nibble_length)
    {
      if (parser->custom_print & PT_PAD_BYTE)
	{
	  sprintf (tbuf, "%02x", bstring[count]);
	  tbuf[2] = '\0';
	}
      else
	{
	  sprintf (tbuf, "%1x", bstring[count]);
	  tbuf[1] = '\0';
	}
      buffer = pt_append_nulstring (parser, buffer, tbuf);
    }

  return buffer;
}

/*
 * describe_data() - Describes a DB_VALUE of primitive data type
 *   return: printed string (buffer pointer)
 *   parser(in) : parser context
 *   buffer(in/out) : buffer to place the printed string
 *   value(in) :
 */
PARSER_VARCHAR *
describe_data (const PARSER_CONTEXT * parser, PARSER_VARCHAR * buffer, const DB_VALUE * value)
{
  OID *oid;
#if !defined(SERVER_MODE)
  DB_OBJECT *obj;
#endif
  DB_MONETARY *money;
  DB_SET *set;
  DB_ELO *elo;
  DB_MIDXKEY *midxkey;
  char *src, *pos, *end;
  double d;
  char line[1025];
  int length;

  assert (parser != NULL);

  if (DB_IS_NULL (value))
    {
      buffer = pt_append_nulstring (parser, buffer, "NULL");
    }
  else
    {
      switch (DB_VALUE_TYPE (value))
	{
	case DB_TYPE_INTEGER:
	  sprintf (line, "%d", db_get_int (value));
	  buffer = pt_append_nulstring (parser, buffer, line);
	  break;

	case DB_TYPE_BIGINT:
	  sprintf (line, "%lld", (long long) db_get_bigint (value));
	  buffer = pt_append_nulstring (parser, buffer, line);
	  break;

	case DB_TYPE_POINTER:
	  sprintf (line, "%p", db_get_pointer (value));
	  buffer = pt_append_nulstring (parser, buffer, line);
	  break;

	case DB_TYPE_SHORT:
	  sprintf (line, "%d", (int) db_get_short (value));
	  buffer = pt_append_nulstring (parser, buffer, line);
	  break;

	case DB_TYPE_ERROR:
	  sprintf (line, "%d", (int) db_get_error (value));
	  buffer = pt_append_nulstring (parser, buffer, line);
	  break;

	case DB_TYPE_FLOAT:
	  buffer = describe_float (parser, buffer, db_get_float (value));
	  break;

	case DB_TYPE_DOUBLE:
	  buffer = describe_double (parser, buffer, db_get_double (value));
	  break;

	case DB_TYPE_NUMERIC:
	  buffer = pt_append_nulstring (parser, buffer, numeric_db_value_print ((DB_VALUE *) value, line));
	  break;

	case DB_TYPE_BIT:
	case DB_TYPE_VARBIT:
	  buffer = describe_bit_string (parser, buffer, value);
	  break;

	case DB_TYPE_CHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARCHAR:
	case DB_TYPE_VARNCHAR:
	  /* Copy string into buf providing for any embedded quotes. Strings may have embedded NULL characters and
	   * embedded quotes.  None of the supported multibyte character codesets have a conflict between a quote
	   * character and the second byte of the multibyte character. */
	  src = db_get_string (value);
	  end = src + db_get_string_size (value);
	  while (src < end)
	    {
	      /* Find the position of the next quote or the end of the string, whichever comes first.  This loop is
	       * done in place of strchr in case the string has an embedded NULL. */
	      for (pos = src; pos && pos < end && (*pos) != '\''; pos++)
		;

	      /* If pos < end, then a quote was found.  If so, copy the partial buffer and duplicate the quote */
	      if (pos < end)
		{
		  length = CAST_STRLEN (pos - src + 1);
		  buffer = pt_append_bytes (parser, buffer, src, length);
		  buffer = pt_append_nulstring (parser, buffer, "'");
		}
	      /* If not, copy the remaining part of the buffer */
	      else
		{
		  buffer = pt_append_bytes (parser, buffer, src, CAST_STRLEN (end - src));
		}

	      /* advance src to just beyond the point where we left off */
	      src = pos + 1;
	    }
	  break;

	case DB_TYPE_OBJECT:
#if !defined(SERVER_MODE)
	  obj = db_get_object (value);
	  if (obj == NULL)
	    {
	      break;
	    }

	  if (obj->is_vid)
	    {
	      DB_VALUE vobj;

	      vid_object_to_vobj (obj, &vobj);
	      return describe_value (parser, buffer, &vobj);
	    }
	  oid = WS_OID (obj);
	  sprintf (line, "%d", (int) oid->volid);
	  buffer = pt_append_nulstring (parser, buffer, line);
	  buffer = pt_append_nulstring (parser, buffer, "|");
	  sprintf (line, "%d", (int) oid->pageid);
	  buffer = pt_append_nulstring (parser, buffer, line);
	  buffer = pt_append_nulstring (parser, buffer, "|");
	  sprintf (line, "%d", (int) oid->slotid);
	  buffer = pt_append_nulstring (parser, buffer, line);
	  break;
	  /* If we are on the server, fall thru to the oid case The value is probably nonsense, but that is safe to do.
	   * This case should simply not occur. */
#endif

	case DB_TYPE_OID:
	  oid = (OID *) db_get_oid (value);
	  if (oid == NULL)
	    {
	      break;
	    }

	  sprintf (line, "%d", (int) oid->volid);
	  buffer = pt_append_nulstring (parser, buffer, line);
	  buffer = pt_append_nulstring (parser, buffer, "|");
	  sprintf (line, "%d", (int) oid->pageid);
	  buffer = pt_append_nulstring (parser, buffer, line);
	  buffer = pt_append_nulstring (parser, buffer, "|");
	  sprintf (line, "%d", (int) oid->slotid);
	  buffer = pt_append_nulstring (parser, buffer, line);
	  break;

	case DB_TYPE_VOBJ:
	  buffer = pt_append_nulstring (parser, buffer, "vid:");
	case DB_TYPE_SET:
	case DB_TYPE_MULTISET:
	case DB_TYPE_SEQUENCE:
	  set = db_get_set (value);
	  if (set != NULL)
	    {
	      return describe_set (parser, buffer, set);
	    }
	  else
	    {
	      buffer = pt_append_nulstring (parser, buffer, "NULL");
	    }

	  break;

	case DB_TYPE_MIDXKEY:
	  midxkey = DB_GET_MIDXKEY (value);
	  if (midxkey != NULL)
	    {
	      return describe_midxkey (parser, buffer, midxkey);
	    }
	  else
	    {
	      buffer = pt_append_nulstring (parser, buffer, "NULL");
	    }
	  break;

	case DB_TYPE_BLOB:
	case DB_TYPE_CLOB:
	  elo = db_get_elo (value);
	  if (elo != NULL)
	    {
	      if (elo->type == ELO_FBO)
		{
		  assert (elo->locator != NULL);
		  buffer = pt_append_nulstring (parser, buffer, elo->locator);
		}
	      else		/* ELO_LO */
		{
		  /* should not happen for now */
		  assert (0);
		}
	    }
	  else
	    {
	      buffer = pt_append_nulstring (parser, buffer, "NULL");
	    }
	  break;

	  /* 
	   * This constant is necessary to fake out the db_?_to_string()
	   * routines that are expecting a buffer length.  Since we assume
	   * that our buffer is big enough in this code, just pass something
	   * that ought to work for every case.
	   */
#define TOO_BIG_TO_MATTER       1024

	case DB_TYPE_TIME:
	  (void) db_time_to_string (line, TOO_BIG_TO_MATTER, db_get_time (value));
	  buffer = pt_append_nulstring (parser, buffer, line);
	  break;
	case DB_TYPE_TIMELTZ:
	  (void) db_timeltz_to_string (line, TOO_BIG_TO_MATTER, db_get_time (value));
	  buffer = pt_append_nulstring (parser, buffer, line);
	  break;

	case DB_TYPE_TIMETZ:
	  {
	    DB_TIMETZ *time_tz;

	    time_tz = DB_GET_TIMETZ (value);
	    (void) db_timetz_to_string (line, TOO_BIG_TO_MATTER, &time_tz->time, &time_tz->tz_id);
	    buffer = pt_append_nulstring (parser, buffer, line);
	  }
	  break;

	case DB_TYPE_UTIME:
	  (void) db_utime_to_string (line, TOO_BIG_TO_MATTER, DB_GET_UTIME (value));
	  buffer = pt_append_nulstring (parser, buffer, line);
	  break;
	case DB_TYPE_TIMESTAMPLTZ:
	  (void) db_timestampltz_to_string (line, TOO_BIG_TO_MATTER, DB_GET_UTIME (value));
	  buffer = pt_append_nulstring (parser, buffer, line);
	  break;

	case DB_TYPE_TIMESTAMPTZ:
	  {
	    DB_TIMESTAMPTZ *ts_tz;

	    ts_tz = DB_GET_TIMESTAMPTZ (value);
	    (void) db_timestamptz_to_string (line, TOO_BIG_TO_MATTER, &(ts_tz->timestamp), &(ts_tz->tz_id));
	    buffer = pt_append_nulstring (parser, buffer, line);
	  }
	  break;

	case DB_TYPE_DATETIME:
	  (void) db_datetime_to_string (line, TOO_BIG_TO_MATTER, DB_GET_DATETIME (value));
	  buffer = pt_append_nulstring (parser, buffer, line);
	  break;
	case DB_TYPE_DATETIMELTZ:
	  (void) db_datetimeltz_to_string (line, TOO_BIG_TO_MATTER, DB_GET_DATETIME (value));
	  buffer = pt_append_nulstring (parser, buffer, line);
	  break;

	case DB_TYPE_DATETIMETZ:
	  {
	    DB_DATETIMETZ *dt_tz;

	    dt_tz = DB_GET_DATETIMETZ (value);
	    (void) db_datetimetz_to_string (line, TOO_BIG_TO_MATTER, &(dt_tz->datetime), &(dt_tz->tz_id));
	    buffer = pt_append_nulstring (parser, buffer, line);
	  }
	  break;

	case DB_TYPE_DATE:
	  (void) db_date_to_string (line, TOO_BIG_TO_MATTER, db_get_date (value));
	  buffer = pt_append_nulstring (parser, buffer, line);
	  break;

	case DB_TYPE_MONETARY:
	  money = db_get_monetary (value);
	  OR_MOVE_DOUBLE (&money->amount, &d);
	  buffer = describe_money (parser, buffer, money);
	  break;

	case DB_TYPE_NULL:
	  /* Can't get here because the DB_IS_NULL test covers DB_TYPE_NULL */
	  break;

	case DB_TYPE_VARIABLE:
	case DB_TYPE_SUB:
	case DB_TYPE_DB_VALUE:
	  /* make sure line is NULL terminated, may not be necessary line[0] = '\0'; */
	  break;

	default:
	  /* NB: THERE MUST BE NO DEFAULT CASE HERE. ALL TYPES MUST BE HANDLED! */
	  assert (false);
	  break;
	}
    }

  return buffer;
}

/*
 * describe_value() - Describes the contents of a DB_VALUE container
 *   return: printed string (buffer pointer)
 *   parser(in) : parser context
 *   buffer(in/out) : buffer to place the printed string
 *   value(in) : value to describe
 *
 * Note :
 *    Prints a SQL syntactically correct representation of the value.
 *    (assuming one exists )
 */
PARSER_VARCHAR *
describe_value (const PARSER_CONTEXT * parser, PARSER_VARCHAR * buffer, const DB_VALUE * value)
{
  INTL_CODESET codeset = INTL_CODESET_NONE;
#if defined (SERVER_MODE) || defined (SA_MODE)
  char str_int[30];
#endif /* SERVER_MODE || SA_MODE */

  assert (parser != NULL);

  if (DB_IS_NULL (value))
    {
      buffer = pt_append_nulstring (parser, buffer, "NULL");
    }
  else
    {
      /* add some extra info to the basic data value */
      switch (DB_VALUE_TYPE (value))
	{
	case DB_TYPE_CHAR:
	case DB_TYPE_VARCHAR:
	  codeset = DB_GET_STRING_CODESET (value);
	  if (codeset != LANG_SYS_CODESET)
	    {
	      buffer = pt_append_nulstring (parser, buffer, lang_charset_introducer (codeset));
	    }
	  buffer = pt_append_nulstring (parser, buffer, "'");
	  buffer = describe_data (parser, buffer, value);
	  buffer = pt_append_nulstring (parser, buffer, "'");
	  break;

	case DB_TYPE_ENUMERATION:
	  if (DB_GET_ENUM_STRING (value) == NULL && DB_GET_ENUM_SHORT (value) != 0)
	    {
#if defined(SERVER_MODE)
	      /* to print enum index as int */
	      sprintf (str_int, "%d", (int) DB_GET_ENUM_SHORT (value));
	      buffer = pt_append_nulstring (parser, buffer, str_int);
	      break;
#elif defined(SA_MODE)
	      if (db_on_server)
		{
		  /* to print enum index as int */
		  sprintf (str_int, "%d", (int) DB_GET_ENUM_SHORT (value));
		  buffer = pt_append_nulstring (parser, buffer, str_int);
		  break;
		}
	      else
		{
		  /* describe value should not be called on an enumeration which is not fully constructed */
		  assert (false);
		  buffer = pt_append_nulstring (parser, buffer, "''");
		}
#else /* CS_MODE */
	      /* describe value should not be called on an enumeration which is not fully constructed */
	      assert (false);
	      buffer = pt_append_nulstring (parser, buffer, "''");
#endif
	    }
	  else
	    {
	      DB_VALUE varchar_val;
	      /* print enumerations as strings */
	      if (tp_enumeration_to_varchar (value, &varchar_val) == NO_ERROR)
		{
		  codeset = DB_GET_ENUM_CODESET (value);
		  if (codeset != LANG_SYS_CODESET)
		    {
		      buffer = pt_append_nulstring (parser, buffer, lang_charset_introducer (codeset));
		    }
		  buffer = describe_value (parser, buffer, &varchar_val);
		}
	      else
		{
		  /* tp_enumeration_to_varchar only fails if the enum string is null which we already checked */
		  assert (false);
		}
	    }
	  break;

	case DB_TYPE_DATE:
	  buffer = pt_append_nulstring (parser, buffer, "date '");
	  buffer = describe_data (parser, buffer, value);
	  buffer = pt_append_nulstring (parser, buffer, "'");
	  break;

	case DB_TYPE_TIME:
	  buffer = pt_append_nulstring (parser, buffer, "time '");
	  buffer = describe_data (parser, buffer, value);
	  buffer = pt_append_nulstring (parser, buffer, "'");
	  break;

	case DB_TYPE_TIMETZ:
	  buffer = pt_append_nulstring (parser, buffer, "timetz '");
	  buffer = describe_data (parser, buffer, value);
	  buffer = pt_append_nulstring (parser, buffer, "'");
	  break;

	case DB_TYPE_TIMELTZ:
	  buffer = pt_append_nulstring (parser, buffer, "timeltz '");
	  buffer = describe_data (parser, buffer, value);
	  buffer = pt_append_nulstring (parser, buffer, "'");
	  break;

	case DB_TYPE_UTIME:
	  buffer = pt_append_nulstring (parser, buffer, "timestamp '");
	  buffer = describe_data (parser, buffer, value);
	  buffer = pt_append_nulstring (parser, buffer, "'");
	  break;

	case DB_TYPE_TIMESTAMPTZ:
	  buffer = pt_append_nulstring (parser, buffer, "timestamptz '");
	  buffer = describe_data (parser, buffer, value);
	  buffer = pt_append_nulstring (parser, buffer, "'");
	  break;

	case DB_TYPE_TIMESTAMPLTZ:
	  buffer = pt_append_nulstring (parser, buffer, "timestampltz '");
	  buffer = describe_data (parser, buffer, value);
	  buffer = pt_append_nulstring (parser, buffer, "'");
	  break;

	case DB_TYPE_DATETIME:
	  buffer = pt_append_nulstring (parser, buffer, "datetime '");
	  buffer = describe_data (parser, buffer, value);
	  buffer = pt_append_nulstring (parser, buffer, "'");
	  break;

	case DB_TYPE_DATETIMETZ:
	  buffer = pt_append_nulstring (parser, buffer, "datetimetz '");
	  buffer = describe_data (parser, buffer, value);
	  buffer = pt_append_nulstring (parser, buffer, "'");
	  break;

	case DB_TYPE_DATETIMELTZ:
	  buffer = pt_append_nulstring (parser, buffer, "datetimeltz '");
	  buffer = describe_data (parser, buffer, value);
	  buffer = pt_append_nulstring (parser, buffer, "'");
	  break;


	case DB_TYPE_NCHAR:
	case DB_TYPE_VARNCHAR:
	  buffer = pt_append_nulstring (parser, buffer, "N'");
	  buffer = describe_data (parser, buffer, value);
	  buffer = pt_append_nulstring (parser, buffer, "'");
	  break;

	case DB_TYPE_BIT:
	case DB_TYPE_VARBIT:
	  buffer = pt_append_nulstring (parser, buffer, "X'");
	  buffer = describe_data (parser, buffer, value);
	  buffer = pt_append_nulstring (parser, buffer, "'");
	  break;

	case DB_TYPE_BLOB:
	  buffer = pt_append_nulstring (parser, buffer, "BLOB'");
	  buffer = describe_data (parser, buffer, value);
	  buffer = pt_append_nulstring (parser, buffer, "'");
	  break;

	case DB_TYPE_CLOB:
	  buffer = pt_append_nulstring (parser, buffer, "CLOB'");
	  buffer = describe_data (parser, buffer, value);
	  buffer = pt_append_nulstring (parser, buffer, "'");
	  break;

	default:
	  buffer = describe_data (parser, buffer, value);
	  break;
	}
    }

  return buffer;
}

/*
 * describe_bit_string() -
 *   return: printed string (buffer pointer)
 *   parser(in) : parser context
 *   buffer(in/out) : buffer to place the printed string
 *   str(in) :
 *   str_len(in) :
 *   max_token_length(in) :
 */
PARSER_VARCHAR *
describe_string (const PARSER_CONTEXT * parser, PARSER_VARCHAR * buffer, const char *str, size_t str_length,
		 int max_token_length)
{
  const char *src, *end, *pos;
  int token_length, length;
  const char *delimiter = "'+\n '";

  src = str;
  end = src + str_length;

  /* get current buffer length */
  if (buffer == NULL)
    {
      token_length = 0;
    }
  else
    {
      token_length = buffer->length % (max_token_length + strlen (delimiter));
    }
  for (pos = src; pos < end; pos++, token_length++)
    {
      /* Process the case (*pos == '\'') first. Don't break the string in the middle of internal quotes('') */
      if (*pos == '\'')
	{			/* put '\'' */
	  length = CAST_STRLEN (pos - src + 1);
	  buffer = pt_append_bytes (parser, buffer, src, length);
	  buffer = pt_append_nulstring (parser, buffer, "'");
	  token_length += 1;	/* for appended '\'' */

	  src = pos + 1;	/* advance src pointer */
	}
      else if (token_length > max_token_length)
	{			/* long string */
	  length = CAST_STRLEN (pos - src + 1);
	  buffer = pt_append_bytes (parser, buffer, src, length);
	  buffer = pt_append_nulstring (parser, buffer, delimiter);
	  token_length = 0;	/* reset token_len for the next new token */

	  src = pos + 1;	/* advance src pointer */
	}
    }

  /* dump the remainings */
  length = CAST_STRLEN (pos - src);
  buffer = pt_append_bytes (parser, buffer, src, length);

  return buffer;
}

/*
 * help_fprint_value() -  Prints a description of the contents of a DB_VALUE
 *                        to the file
 *   return: none
 *   fp(in) : FILE stream pointer
 *   value(in) : value to print
 */

void
help_fprint_value (FILE * fp, const DB_VALUE * value)
{
  PARSER_VARCHAR *buffer;
  PARSER_CONTEXT *parser;

  parser = parser_create_parser ();
  if (parser == NULL)
    {
      return;
    }

  buffer = describe_value (parser, NULL, value);
  fprintf (fp, "%.*s", (int) pt_get_varchar_length (buffer), pt_get_varchar_bytes (buffer));
  parser_free_parser (parser);
}

/*
 * help_sprint_value() - This places a printed representation of the supplied
 *                       value in a buffer.
 *   return: number of characters in description
 *   value(in) : value to describe
 *   buffer(in/out) : buffer to contain description
 *   max_length(in) : maximum chars in buffer
 *
 *  NOTE:
 *   This entire module needs to be much more careful about
 *   overflowing the internal "linebuf" buffer when using long
 *   strings.
 *   If the description will fit within the buffer, the number of characters
 *   used is returned, otherwise, -1 is returned.
 */
int
help_sprint_value (const DB_VALUE * value, char *buffer, int max_length)
{
  int length;
  PARSER_VARCHAR *buf;
  PARSER_CONTEXT *parser;

  parser = parser_create_parser ();
  if (parser == NULL)
    {
      return 0;
    }

  buf = pt_append_nulstring (parser, NULL, "");
  buf = describe_value (parser, buf, value);
  length = pt_get_varchar_length (buf);
  if (length < max_length)
    {
      memcpy (buffer, (char *) pt_get_varchar_bytes (buf), length);
      buffer[length] = 0;
    }
  else
    {
      length = -length;
    }

  parser_free_parser (parser);

  return length;
}

/*
 * describe_comment() - Return the description string of a comment.
 *   return: a pointer to description string of a comment
 *   comment(in) : a comment string to be described
 */
char *
describe_comment (PARSER_CONTEXT * parser, const char *comment)
{
  DB_VALUE comment_value;
  PARSER_VARCHAR *buffer = NULL;

  assert (parser != NULL);
  assert (comment != NULL);

  DB_MAKE_NULL (&comment_value);
  DB_MAKE_STRING (&comment_value, comment);

  buffer = pt_append_nulstring (parser, buffer, "COMMENT ");
  if (comment != NULL && comment[0] != '\0')
    {
      buffer = describe_value (parser, buffer, &comment_value);
    }
  else
    {
      buffer = pt_append_nulstring (parser, buffer, "''");
    }

  pr_clear_value (&comment_value);

  return ((char *) pt_get_varchar_bytes (buffer));
}

/*
 * help_fprint_describe_comment() - Print description of a comment to a file.
 *   return: N/A
 *   comment(in) : a comment string to be printed
 */
void
help_fprint_describe_comment (FILE * fp, const char *comment)
{
  PARSER_CONTEXT *parser;
  char *desc = NULL;

  assert (fp != NULL);
  assert (comment != NULL);

  parser = parser_create_parser ();
  if (parser == NULL)
    {
      return;
    }

  desc = describe_comment (parser, comment);

  assert (desc != NULL);
  fprintf (fp, "%.*s", strlen (desc), desc);

  parser_free_parser (parser);
}

#if defined(CUBRID_DEBUG)
/*
 * dbg_value() -  This is primarily for debugging
 *   return: a character string representation of the db_value
 *   value(in) : value to describe
 */

char *
dbg_value (const DB_VALUE * value)
{
  PARSER_VARCHAR *buffer;
  PARSER_CONTEXT *parser;
  char *ret;

  parser = parser_create_parser ();
  if (parser == NULL)
    {
      return 0;
    }

  buffer = pt_append_nulstring (parser, NULL, "");
  buffer = describe_value (parser, buffer, value);
  ret = (char *) pt_get_varchar_bytes (buffer);
  parser_free_parser (parser);

  return ret;
}
#endif
