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

#include "object_print.h"
#include "object_print_common.hpp"
#include "object_print_parser.hpp"

#include "error_manager.h"
#if !defined (SERVER_MODE)
#include "chartype.h"
#include "misc_string.h"
#include "dbi.h"
#include "schema_manager.h"
#include "trigger_manager.h"
#include "virtual_object.h"
#include "set_object.h"
#include "parse_tree.h"
#include "parser.h"
#include "transaction_cl.h"
#include "network_interface_cl.h"
#include "class_object.h"
#include "work_space.h"
#endif /* !defined (SERVER_MODE) */
#include "string_buffer.hpp"
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

static void obj_print_free_strarray (char **strs);
static char *obj_print_copy_string (const char *source);
static const char **obj_print_convert_strlist (STRLIST * str_list);
void obj_print_describe_trigger_list(PARSER_CONTEXT* parser, TR_TRIGLIST* triggers, STRLIST** strings);
static const char **obj_print_describe_class_triggers (PARSER_CONTEXT * parser, SM_CLASS * class_p, MOP class_mop);
static CLASS_HELP *obj_print_make_class_help (void);
static TRIGGER_HELP *obj_print_make_trigger_help (void);
static OBJ_HELP *obj_print_make_obj_help (void);
static char **obj_print_read_section (FILE * fp);
static COMMAND_HELP *obj_print_load_help_file (FILE * fp, const char *keyword);
static char *obj_print_next_token (char *ptr, char *buf);

/* Instance help */
OBJ_HELP *help_obj (MOP op);
void help_free_obj (OBJ_HELP * info);

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

static void obj_print_describe_trigger_list(PARSER_CONTEXT* parser, TR_TRIGLIST* triggers, STRLIST** strings)
{
  TR_TRIGLIST *t;
  STRLIST *new_p;
  char b[8192] = {0};//bSolo: temp hack
  string_buffer sb(sizeof(b), b);

  for (t = triggers; t != NULL; t = t->next)
    {
      sb.clear();
      object_print_parser print(sb);
      print.describe_class_trigger(t->trigger);

      /* 
       * this used to be add_strlist, but since it is not used in any other
       * place, or ever will be, I unrolled it here.
       */
      new_p = (STRLIST *) malloc (sizeof (STRLIST));
      if (new_p != NULL)
        {
          new_p->next = *strings;
          *strings = new_p;
          new_p->string = obj_print_copy_string(b);
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
 * obj_print_help_class () - Constructs a class help structure containing textual
 *                 information about the class.
 *   return: class help structure
 *   op(in): class object
 *   prt_type(in): the print type: csql schema or show create table
 */
CLASS_HELP* obj_print_help_class (MOP op, OBJ_PRINT_TYPE prt_type)
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
  int is_class = 0;
  SM_CLASS *subclass;
  bool include_inherited;
  bool force_print_att_coll = false;
  bool has_comment = false;
  int max_name_size = SM_MAX_IDENTIFIER_LENGTH + 50;
  size_t buf_size = 0;
  STRLIST *str_list_head = NULL, *current_str = NULL, *tmp_str = NULL;
  char b[8192] = {0};//bSolo: temp hack
  string_buffer sb(sizeof(b), b);
  object_print_parser obj_print(sb);

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
              sb.clear();
	      if (has_comment)
		{
		  sb("%-20s ", (char*)sm_ch_name((MOBJ)class_));
                  obj_print.describe_comment(class_->comment);
		}
	      else
		{
		  sb("%s", (char*)sm_ch_name((MOBJ)class_));
		}
	    }
	  else
	    {
	      if (has_comment)
		{
		  sb("%-20s COLLATE %s ", sm_ch_name((MOBJ)class_), lang_get_collation_name(class_->collation_id));
                  obj_print.describe_comment(class_->comment);
		}
	      else
		{
		  sb("%-20s COLLATE %s", sm_ch_name((MOBJ)class_), lang_get_collation_name(class_->collation_id));
		}
	    }
	  info->name = obj_print_copy_string(sb.get_buffer());
	}
      else
	{
	  /* 
	   * For the case prt_type == OBJ_PRINT_SHOW_CREATE_TABLE
	   * info->name is set to the exact class name
	   */
          sb.clear();
	  sb("[%s]", sm_ch_name ((MOBJ) class_));
	  info->name = obj_print_copy_string(sb.get_buffer());
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
                  sb.clear();
		  sb("[%s]", kludge);
		  strs[i] = obj_print_copy_string (sb.get_buffer());
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
                  sb.clear();
		  sb("[%s]", kludge);
		  strs[i] = obj_print_copy_string(sb.get_buffer());
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
                      sb.clear();
		      obj_print.describe_attribute(op, a, (a->class_mop != op), prt_type, force_print_att_coll);
		      if(sb.len() == 0)
                        {
                          goto error_exit;
                        }
		      strs[i] = obj_print_copy_string(sb.get_buffer());
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
                      sb.clear();
                      obj_print.describe_attribute(op, a, (a->class_mop != op), prt_type, force_print_att_coll);
		      if(sb.len() == 0)
                        {
                          goto error_exit;
                        }
                        strs[i] = obj_print_copy_string(sb.get_buffer());
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
                      sb.clear();
		      obj_print.describe_method(op, m, prt_type);
		      strs[i] = obj_print_copy_string(sb.get_buffer());
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
                      sb.clear();
		      obj_print.describe_method(op, m, prt_type);
		      strs[i] = obj_print_copy_string(sb.get_buffer());
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
              sb.clear();
	      obj_print.describe_resolution(r, prt_type);
	      strs[i] = obj_print_copy_string(sb.get_buffer());
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
                      sb.clear();
		      obj_print.describe_method_file(op, f);
		      strs[i] = obj_print_copy_string(sb.get_buffer());
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
                          sb.clear();
			  obj_print.describe_constraint(class_, c, prt_type);
			  strs[i] = obj_print_copy_string(sb.get_buffer());
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
                      sb.clear();
                      obj_print.describe_partition_parts(subclass->partition, prt_type);
                      PARSER_VARCHAR* descr = pt_append_nulstring(parser, nullptr, sb.get_buffer());

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
		      tmp_str->string = (char*)pt_get_varchar_bytes(descr);

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

          sb.clear();
          obj_print.describe_partition_info(class_->partition);
          strs[0] = obj_print_copy_string(sb.get_buffer());

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
  help->condition_time = object_print_parser::describe_trigger_condition_time(trigger);
  help->action_time = object_print_parser::describe_trigger_action_time(trigger);

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
OBJ_HELP* help_obj (MOP op)
{
  int error;
  SM_CLASS *class_;
  SM_ATTRIBUTE *attribute_p;
  char *obj;
  int i, count, is_class = 0;
  OBJ_HELP *info = NULL;
  char **strs;
  int pin;
  size_t buf_size;
  DB_VALUE value;
  char b[8192] = {0};
  string_buffer sb(sizeof(b), b);
  object_print_common obj_print(sb);

  if (parser == NULL)
    {
      parser = parser_create_parser ();
    }
  if (parser == NULL)
    {
      goto error_exit;
    }

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
              obj_print.describe_data(&value);
	      db_value_clear (&value);
	      DB_MAKE_NULL (&value);

	      info->oid = obj_print_copy_string(sb.get_buffer());

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
		      /* 
		       * We're starting a new line here, so we don't
		       * want to append to the old buffer; pass NULL
		       * to pt_append_nulstring so that we start a new
		       * string.
		       */
                      sb.clear();
		      sb("%20s = ", attribute_p->header.name);
                      if(attribute_p->header.name_space == ID_SHARED_ATTRIBUTE)
                      {
                          obj_print.describe_value(&attribute_p->default_value.value);
                      }
                      else
                      {
                          db_get (op, attribute_p->header.name, &value);
                          obj_print.describe_value(&value);
                      }
                      strs[i] = obj_print_copy_string (sb.get_buffer());
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

#if !defined(SERVER_MODE)
//--------------------------------------------------------------------------------
void* parser_realloc(void* ptr, size_t bytes)//allocate memory using parser_context
{
    static parser_context* pc = parser_create_parser();//there is a global parser context, should I use that one?
    void* p = realloc(ptr, bytes);//temporary fallback to realloc... but is it really necessary to have an allocator based on parser_context???
    return p;
}
#endif

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

#endif /* defined (SERVER_MODE) */

/*
 * help_fprint_value() -  Prints a description of the contents of a DB_VALUE
 *                        to the file
 *   return: none
 *   fp(in) : FILE stream pointer
 *   value(in) : value to print
 */
void help_fprint_value(FILE* fp, const DB_VALUE* value)
{
#if defined(SERVER_MODE)
    char b[8192] = {0};//bSolo ToDo: use server specific allocator
#else
    char b[8192] = {0};//bSolo ToDo: use parser specific allocator
#endif
  string_buffer buf(sizeof(b), b);
  object_print_common obj_print(buf);
  obj_print.describe_value(value);
  if(buf.len() > sizeof(b))//realloc a bigger buffer and try again
  {
      buf.clear();
      obj_print.describe_value(value);
  }
  fprintf (fp, "%.*s", (int)buf.len(), b);
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
int help_sprint_value(const DB_VALUE* value, char *buffer, int max_length)
{
  string_buffer buf(max_length, buffer);
  object_print_common obj_print(buf);
  obj_print.describe_value(value);
  int length = (int)buf.len();
  if(length > max_length)
    {
      length = -length;
    }
  return length;
}

/*
 * help_fprint_describe_comment() - Print description of a comment to a file.
 *   return: N/A
 *   comment(in) : a comment string to be printed
 */
void help_fprint_describe_comment(FILE* fp, const char* comment)
{
#if !defined (SERVER_MODE)
  char *desc = NULL;
  char b[8192] = {0};//bSolo: temp hack
  char* dynBuf = nullptr;
  string_buffer sb(sizeof(b), b);
  object_print_parser obj_print(sb);

  assert (fp != NULL);
  assert (comment != NULL);
RETRY:
  obj_print.describe_comment(comment);
  if(sizeof(b) < sb.len())
  {
      dynBuf = new char[sb.len()+1];//bSolo: use specific allocator
      sb.set_buffer(sb.len()+1, dynBuf);
      goto RETRY;
  }
  assert(sb.len() > 0);
  fprintf(fp, "%.*s", int(sb.len()), sb.get_buffer());
  if(dynBuf)
  {
      delete dynBuf;
  }
#endif /* !defined (SERVER_MODE) */
}