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
 * object_print.c - Routines to print dbvalues
 */

#ident "$Id$"

#include "object_print.h"

#include "authenticate.h"
#include "chartype.h"
#include "class_description.hpp"
#include "dbi.h"
#include "dbtype.h"
#include "error_manager.h"
#include "locator_cl.h"
#include "message_catalog.h"
#include "msgcat_help.hpp"
#include "network_interface_cl.h"
#include "object_description.hpp"
#include "object_print_util.hpp"
#include "object_printer.hpp"
#include "printer.hpp"
#include "schema_manager.h"
#include "string_buffer.hpp"
#include "trigger_description.hpp"
#include "trigger_manager.h"

#if defined (SUPPRESS_STRLEN_WARNING)
#define strlen(s1)  ((int) strlen(s1))
#endif /* defined (SUPPRESS_STRLEN_WARNING) */

#define MATCH_TOKEN(string, token) \
  ((string == NULL) ? 0 : intl_mbs_casecmp(string, token) == 0)

static char *obj_print_next_token (char *ptr, char *buf);

/* This will be in one of the language directories under $CUBRID/msg */

/* TRIGGER SUPPORT FUNCTIONS */

/* TRIGGER HELP */

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
		      names[i] = object_print::copy_string ((char *) name);
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

/* HELP PRINTING */
/* These functions build help structures and print them to a file. */

/*
 * help_print_obj () - Prints the description of a class or instance object
 *                      to a generic output.
 *   return: none
 *   fp(in):file pointer
 *   obj(in):class or instance to describe
 */

void
help_print_obj (print_output & output_ctx, MOP obj)
{
  int i, status;

  status = locator_is_class (obj, DB_FETCH_READ);

  if (status < 0)
    {
      return;
    }
  else if (status > 0)
    {
      if (locator_is_root (obj))
	{
	  output_ctx (msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_HELP, MSGCAT_HELP_ROOTCLASS_TITLE));
	}
      else
	{
	  class_description cinfo;
	  if (cinfo.init (obj, class_description::CSQL_SCHEMA_COMMAND) == NO_ERROR)
	    {
	      output_ctx (msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_HELP, MSGCAT_HELP_CLASS_TITLE),
			  cinfo.class_type, cinfo.name);

	      if (cinfo.supers != NULL)
		{
		  output_ctx (msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_HELP, MSGCAT_HELP_SUPER_CLASSES));
		  for (i = 0; cinfo.supers[i] != NULL; i++)
		    {
		      output_ctx ("  %s\n", cinfo.supers[i]);
		    }
		}
	      if (cinfo.subs != NULL)
		{
		  output_ctx (msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_HELP, MSGCAT_HELP_SUB_CLASSES));
		  for (i = 0; cinfo.subs[i] != NULL; i++)
		    {
		      output_ctx ("  %s\n", cinfo.subs[i]);
		    }
		}
	      if (cinfo.attributes != NULL)
		{
		  output_ctx (msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_HELP, MSGCAT_HELP_ATTRIBUTES));
		  for (i = 0; cinfo.attributes[i] != NULL; i++)
		    {
		      output_ctx ("  %s\n", cinfo.attributes[i]);
		    }
		}
	      if (cinfo.methods != NULL)
		{
		  output_ctx (msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_HELP, MSGCAT_HELP_METHODS));
		  for (i = 0; cinfo.methods[i] != NULL; i++)
		    {
		      output_ctx ("  %s\n", cinfo.methods[i]);
		    }
		}
	      if (cinfo.class_attributes != NULL)
		{
		  output_ctx (msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_HELP, MSGCAT_HELP_CLASS_ATTRIBUTES));
		  for (i = 0; cinfo.class_attributes[i] != NULL; i++)
		    {
		      output_ctx ("  %s\n", cinfo.class_attributes[i]);
		    }
		}
	      if (cinfo.class_methods != NULL)
		{
		  output_ctx (msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_HELP, MSGCAT_HELP_CLASS_METHODS));
		  for (i = 0; cinfo.class_methods[i] != NULL; i++)
		    {
		      output_ctx ("  %s\n", cinfo.class_methods[i]);
		    }
		}
	      if (cinfo.resolutions != NULL)
		{
		  output_ctx (msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_HELP, MSGCAT_HELP_RESOLUTIONS));
		  for (i = 0; cinfo.resolutions[i] != NULL; i++)
		    {
		      output_ctx ("  %s\n", cinfo.resolutions[i]);
		    }
		}
	      if (cinfo.method_files != NULL)
		{
		  output_ctx (msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_HELP, MSGCAT_HELP_METHOD_FILES));
		  for (i = 0; cinfo.method_files[i] != NULL; i++)
		    {
		      output_ctx ("  %s\n", cinfo.method_files[i]);
		    }
		}
	      if (cinfo.query_spec != NULL)
		{
		  output_ctx (msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_HELP, MSGCAT_HELP_QUERY_SPEC));
		  for (i = 0; cinfo.query_spec[i] != NULL; i++)
		    {
		      output_ctx ("  %s\n", cinfo.query_spec[i]);
		    }
		}
	      if (cinfo.triggers.size () > 0)	//triggers
		{
		  /* fprintf(fp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_HELP, MSGCAT_HELP_TRIGGERS)); */
		  output_ctx ("Triggers:\n");
		  for (size_t n = cinfo.triggers.size (), i = 0; i < n; ++i)
		    {
		      output_ctx ("  %s\n", cinfo.triggers[i]);
		    }
		}
	    }
	}
    }
  else
    {
      (void) tr_is_trigger (obj, &status);

      if (status)
	{
	  trigger_description tinfo;

	  if (tinfo.init (obj) == NO_ERROR)
	    {
	      output_ctx ("Trigger : %s", tinfo.name);
	      if (tinfo.status)
		{
		  output_ctx (" (INACTIVE)\n");
		}
	      else
		{
		  output_ctx ("\n");
		}

	      output_ctx ("  %s %s ", tinfo.condition_time, tinfo.event);
	      if (tinfo.class_name != NULL)
		{
		  if (tinfo.attribute != NULL)
		    {
		      output_ctx ("%s ON %s ", tinfo.attribute, tinfo.class_name);
		    }
		  else
		    {
		      output_ctx ("ON %s ", tinfo.class_name);
		    }
		}

	      output_ctx ("PRIORITY %s\n", tinfo.priority);

	      if (tinfo.condition)
		{
		  output_ctx ("  IF %s\n", tinfo.condition);
		}

	      if (tinfo.action != NULL)
		{
		  output_ctx ("  EXECUTE ");
		  if (strcmp (tinfo.condition_time, tinfo.action_time) != 0)
		    {
		      output_ctx ("%s ", tinfo.action_time);
		    }
		  output_ctx ("%s\n", tinfo.action);
		}

	      if (tinfo.comment != NULL && tinfo.comment[0] != '\0')
		{
		  output_ctx (" ");
		  help_print_describe_comment (output_ctx, tinfo.comment);
		  output_ctx ("\n");
		}
	    }
	}
      else
	{
	  object_description oinfo;

	  if (oinfo.init (obj) == NO_ERROR)
	    {
	      output_ctx (msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_HELP, MSGCAT_HELP_OBJECT_TITLE),
			  oinfo.classname);
	      if (oinfo.attributes != NULL)
		{
		  for (i = 0; oinfo.attributes[i] != NULL; i++)
		    {
		      output_ctx ("%s\n", oinfo.attributes[i]);
		    }
		}
	      if (oinfo.shared != NULL)
		{
		  for (i = 0; oinfo.shared[i] != NULL; i++)
		    {
		      output_ctx ("%s\n", oinfo.shared[i]);
		    }
		}
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
  char **names;
  int count, i, outcount;
  DB_OBJECT *requested_owner, *owner;
  char buffer[2 * DB_MAX_IDENTIFIER_LENGTH + 4];
  const char *unique_name;
  const char *class_name;

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
	      unique_name = db_get_class_name (m->op);
	      buffer[0] = '\0';

	      if (!requested_owner && sm_check_name (unique_name))
		{
		  snprintf (buffer, sizeof (buffer) - 1, "%s", unique_name);
		  names[outcount++] = object_print::copy_string (buffer);
		  continue;
		}

	      owner = db_get_owner (m->op);
	      class_name = sm_remove_qualifier_name (unique_name);
	      if (ws_is_same_object (requested_owner, owner) && sm_check_name (class_name))
		{
		  snprintf (buffer, sizeof (buffer) - 1, "%s", class_name);
		  names[outcount++] = object_print::copy_string (buffer);
		  continue;
		}

	      snprintf (buffer, sizeof (buffer) - 1, "%s", "unknown_class");
	      names[outcount++] = object_print::copy_string (buffer);
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
      object_print::free_strarray (names);
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
  while (char_isspace ((DB_C_INT) (*p)) && *p != '\0')
    {
      p++;
    }
  while (!char_isspace ((DB_C_INT) (*p)) && *p != '\0')
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

  file_print_output output_ctx (fpp);
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
	      help_print_obj (output_ctx, class_mop);
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
	{
	  //help_print_trigger (buffer, fpp);
	  trigger_description td;
	  if (td.init (buffer) == NO_ERROR)
	    {
	      td.fprint (fpp);
	    }
	}
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

/*
 * help_print_describe_comment() - Print description of a comment to a generic output.
 *   return: N/A
 *   output_ctx(in/out) : output context
 *   comment(in) : a comment string to be printed
 */
void
help_print_describe_comment (print_output & output_ctx, const char *comment)
{
  /* TODO : optimize printing directly to string_buffer of output_ctx */
  string_buffer sb;
  object_printer printer (sb);

  assert (comment != NULL);

  printer.describe_comment (comment);
  output_ctx ("%.*s", int (sb.len ()), sb.get_buffer ());
}
