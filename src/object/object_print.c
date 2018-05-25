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

#include "object_print.h"
#include "config.h"
#include "db_private_allocator.hpp"
#include "db_value_printer.hpp"
#include "mem_block.hpp"

#include <stdlib.h>
#include <float.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>


#include "error_manager.h"
#if !defined (SERVER_MODE)
#include "chartype.h"
#include "class_description.hpp"
#include "misc_string.h"
#include "dbi.h"
#include "schema_manager.h"
#include "trigger_description.hpp"
#include "trigger_manager.h"
#include "virtual_object.h"
#include "set_object.h"
#include "parse_tree.h"
#include "parser.h"
#include "transaction_cl.h"
#include "msgcat_help.hpp"
#include "network_interface_cl.h"
#include "object_description.hpp"
#include "object_printer.hpp"
#include "object_print_util.hpp"
#include "class_object.h"
#include "work_space.h"
#endif /* !defined (SERVER_MODE) */
#include "string_buffer.hpp"
#include "dbtype.h"

#if defined (SUPPRESS_STRLEN_WARNING)
#define strlen(s1)  ((int) strlen(s1))
#endif /* defined (SUPPRESS_STRLEN_WARNING) */

#if !defined(SERVER_MODE)

#define MATCH_TOKEN(string, token) \
  ((string == NULL) ? 0 : intl_mbs_casecmp(string, token) == 0)

extern unsigned int db_on_server;

static char **obj_print_read_section (FILE * fp);
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
 * help_fprint_obj () - Prints the description of a class or instance object
 *                      to the file.
 *   return: none
 *   fp(in):file pointer
 *   obj(in):class or instance to describe
 */

void
help_fprint_obj (FILE * fp, MOP obj)
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
	  fprintf (fp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_HELP, MSGCAT_HELP_ROOTCLASS_TITLE));
	}
      else
	{
	  class_description cinfo;
	  if (cinfo.init (obj, class_description::CSQL_SCHEMA_COMMAND) == NO_ERROR)
	    {
	      fprintf (fp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_HELP, MSGCAT_HELP_CLASS_TITLE),
		       cinfo.class_type, cinfo.name);

	      if (cinfo.supers != NULL)
		{
		  fprintf (fp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_HELP, MSGCAT_HELP_SUPER_CLASSES));
		  for (i = 0; cinfo.supers[i] != NULL; i++)
		    {
		      fprintf (fp, "  %s\n", cinfo.supers[i]);
		    }
		}
	      if (cinfo.subs != NULL)
		{
		  fprintf (fp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_HELP, MSGCAT_HELP_SUB_CLASSES));
		  for (i = 0; cinfo.subs[i] != NULL; i++)
		    {
		      fprintf (fp, "  %s\n", cinfo.subs[i]);
		    }
		}
	      if (cinfo.attributes != NULL)
		{
		  fprintf (fp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_HELP, MSGCAT_HELP_ATTRIBUTES));
		  for (i = 0; cinfo.attributes[i] != NULL; i++)
		    {
		      fprintf (fp, "  %s\n", cinfo.attributes[i]);
		    }
		}
	      if (cinfo.methods != NULL)
		{
		  fprintf (fp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_HELP, MSGCAT_HELP_METHODS));
		  for (i = 0; cinfo.methods[i] != NULL; i++)
		    {
		      fprintf (fp, "  %s\n", cinfo.methods[i]);
		    }
		}
	      if (cinfo.class_attributes != NULL)
		{
		  fprintf (fp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_HELP, MSGCAT_HELP_CLASS_ATTRIBUTES));
		  for (i = 0; cinfo.class_attributes[i] != NULL; i++)
		    {
		      fprintf (fp, "  %s\n", cinfo.class_attributes[i]);
		    }
		}
	      if (cinfo.class_methods != NULL)
		{
		  fprintf (fp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_HELP, MSGCAT_HELP_CLASS_METHODS));
		  for (i = 0; cinfo.class_methods[i] != NULL; i++)
		    {
		      fprintf (fp, "  %s\n", cinfo.class_methods[i]);
		    }
		}
	      if (cinfo.resolutions != NULL)
		{
		  fprintf (fp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_HELP, MSGCAT_HELP_RESOLUTIONS));
		  for (i = 0; cinfo.resolutions[i] != NULL; i++)
		    {
		      fprintf (fp, "  %s\n", cinfo.resolutions[i]);
		    }
		}
	      if (cinfo.method_files != NULL)
		{
		  fprintf (fp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_HELP, MSGCAT_HELP_METHOD_FILES));
		  for (i = 0; cinfo.method_files[i] != NULL; i++)
		    {
		      fprintf (fp, "  %s\n", cinfo.method_files[i]);
		    }
		}
	      if (cinfo.query_spec != NULL)
		{
		  fprintf (fp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_HELP, MSGCAT_HELP_QUERY_SPEC));
		  for (i = 0; cinfo.query_spec[i] != NULL; i++)
		    {
		      fprintf (fp, "  %s\n", cinfo.query_spec[i]);
		    }
		}
	      if (cinfo.triggers.size () > 0)	//triggers
		{
		  /* fprintf(fp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_HELP, MSGCAT_HELP_TRIGGERS)); */
		  fprintf (fp, "Triggers:\n");
		  for (size_t n = cinfo.triggers.size (), i = 0; i < n; ++i)
		    {
		      fprintf (fp, "  %s\n", cinfo.triggers[i]);
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
	      fprintf (fp, "Trigger : %s", tinfo.name);
	      if (tinfo.status)
		{
		  fprintf (fp, " (INACTIVE)\n");
		}
	      else
		{
		  fprintf (fp, "\n");
		}

	      fprintf (fp, "  %s %s ", tinfo.condition_time, tinfo.event);
	      if (tinfo.class_name != NULL)
		{
		  if (tinfo.attribute != NULL)
		    {
		      fprintf (fp, "%s ON %s ", tinfo.attribute, tinfo.class_name);
		    }
		  else
		    {
		      fprintf (fp, "ON %s ", tinfo.class_name);
		    }
		}

	      fprintf (fp, "PRIORITY %s\n", tinfo.priority);

	      if (tinfo.condition)
		{
		  fprintf (fp, "  IF %s\n", tinfo.condition);
		}

	      if (tinfo.action != NULL)
		{
		  fprintf (fp, "  EXECUTE ");
		  if (strcmp (tinfo.condition_time, tinfo.action_time) != 0)
		    {
		      fprintf (fp, "%s ", tinfo.action_time);
		    }
		  fprintf (fp, "%s\n", tinfo.action);
		}

	      if (tinfo.comment != NULL && tinfo.comment[0] != '\0')
		{
		  fprintf (fp, " ");
		  help_fprint_describe_comment (fp, tinfo.comment);
		  fprintf (fp, "\n");
		}
	    }
	}
      else
	{
	  object_description oinfo;

	  if (oinfo.init (obj) == NO_ERROR)
	    {
	      fprintf (fp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_HELP, MSGCAT_HELP_OBJECT_TITLE),
		       oinfo.classname);
	      if (oinfo.attributes != NULL)
		{
		  for (i = 0; oinfo.attributes[i] != NULL; i++)
		    {
		      fprintf (fp, "%s\n", oinfo.attributes[i]);
		    }
		}
	      if (oinfo.shared != NULL)
		{
		  for (i = 0; oinfo.shared[i] != NULL; i++)
		    {
		      fprintf (fp, "%s\n", oinfo.shared[i]);
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
		      tmp = db_get_string (&owner_name);
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

		  names[outcount++] = object_print::copy_string (buffer);
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

#endif /* defined (SERVER_MODE) */

/*
 * help_fprint_value() -  Prints a description of the contents of a DB_VALUE
 *                        to the file
 *   return: none
 *   fp(in) : FILE stream pointer
 *   value(in) : value to print
 */
void
help_fprint_value (THREAD_ENTRY * thread_p, FILE * fp, const DB_VALUE * value)
{
/* *INDENT-OFF* */
  db_private_allocator<char> private_allocator{thread_p};

#if defined(NO_GCC_44) //temporary until evolve above gcc 4.4.7
  string_buffer sb{
    [&private_allocator] (mem::block& block, size_t len)
    {
      mem::block b{block.dim + len, private_allocator.allocate (block.dim + len)};
      memcpy (b.ptr, block.ptr, block.dim);
      private_allocator.deallocate (block.ptr);
      block = std::move (b);
    },
    [&private_allocator] (mem::block& block)
    {
      private_allocator.deallocate (block.ptr);
      block = {};
    }
  };
#else
  string_buffer sb{&mem::private_realloc, &mem::private_dealloc};
#endif
/* *INDENT-ON* */

  db_value_printer printer (sb);
  printer.describe_value (value);
  fprintf (fp, "%.*s", (int) sb.len (), sb.get_buffer ());
}

/*
 * help_sprint_value() - This places a printed representation of the supplied value in a buffer.
 *   value(in) : value to describe
 *   sb(in/out) : auto resizable buffer to contain description
 */
void
help_sprint_value (const DB_VALUE * value, string_buffer & sb)
{
  db_value_printer printer (sb);
  printer.describe_value (value);
}

/*
 * help_fprint_describe_comment() - Print description of a comment to a file.
 *   return: N/A
 *   comment(in) : a comment string to be printed
 */
void
help_fprint_describe_comment (FILE * fp, const char *comment)
{
#if !defined (SERVER_MODE)
  string_buffer sb;
  object_printer printer (sb);

  assert (fp != NULL);
  assert (comment != NULL);

  printer.describe_comment (comment);
  fprintf (fp, "%.*s", int (sb.len ()), sb.get_buffer ());
#endif /* !defined (SERVER_MODE) */
}
