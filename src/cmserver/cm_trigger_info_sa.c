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
 * trigger_info_sa.c -
 */

#ident "$Id$"

#include <stdio.h>
#include <stdlib.h>

#ifdef WIN32
#include <winsock2.h>
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif

#include "dbi.h"
#include "cm_porting.h"

#define MSGFMT	"%s:%s\n"

#ifdef	_DEBUG_
#include <assert.h>
#include "deb.h"
#endif


static void write_err_msg (char *errfile, char *msg);
static void get_trigger_information (FILE * fp, DB_OBJECT * trigger_obj);

int
main (int argc, char *argv[])
{
  char *dbname, *uid, *passwd, *outfile, *errfile;
  FILE *fp;
  DB_OBJLIST *trigger_list, *temp;
  DB_OBJECT *trigger_obj;
  int errcode;

  if (argc < 6)
    {
      return 1;
    }

  putenv ("CUBRID_ERROR_LOG=NULL");
  close (2);

  dbname = argv[1];
  uid = argv[2];
  passwd = argv[3];
  outfile = argv[4];
  errfile = argv[5];

  db_login (uid, passwd);
  if (db_restart (argv[0], 0, dbname) < 0)
    {
      write_err_msg (errfile, (char *) db_error_string (1));
      return 0;
    }

  fp = fopen (outfile, "w+");
  if (fp == NULL)
    {
      db_shutdown ();
      return 0;
    }

  errcode = db_find_all_triggers (&trigger_list);
  if (errcode)
    {
      write_err_msg (errfile, (char *) db_error_string (1));
      db_shutdown ();
      return 0;
    }

  fprintf (fp, MSGFMT, "open", "triggerlist");

  temp = trigger_list;
  while (temp)
    {
      trigger_obj = temp->op;
      fprintf (fp, MSGFMT, "open", "triggerinfo");
      get_trigger_information (fp, trigger_obj);
      fprintf (fp, MSGFMT, "close", "triggerinfo");
      temp = temp->next;
    }

  fprintf (fp, MSGFMT, "close", "triggerlist");
  fclose (fp);

  db_objlist_free (trigger_list);
  db_shutdown ();

  return 0;
}

static void
write_err_msg (char *errfile, char *msg)
{
  FILE *fp;
  fp = fopen (errfile, "w");
  if (fp)
    {
      fprintf (fp, "%s", msg);
      fclose (fp);
    }
  return;
}

static void
get_trigger_information (FILE * fp, DB_OBJECT * triggerobj)
{
  char *trigger_name, *action, *attr, *condition;
  DB_OBJECT *target_class;
  DB_TRIGGER_EVENT event;
  DB_TRIGGER_TIME eventtime, actiontime;
  DB_TRIGGER_ACTION action_type;
  DB_TRIGGER_STATUS status;
  double priority;
  char pri_string[10];

  trigger_name = action = NULL;

  /* trigger name */
  db_trigger_name (triggerobj, &trigger_name);
  fprintf (fp, MSGFMT, "name", trigger_name);
  if (trigger_name)
    db_string_free ((char *) trigger_name);

  /* eventtime */
  db_trigger_condition_time (triggerobj, &eventtime);
  switch (eventtime)
    {
    case TR_TIME_BEFORE:
      fprintf (fp, MSGFMT, "conditiontime", "BEFORE");
      break;
    case TR_TIME_AFTER:
      fprintf (fp, MSGFMT, "conditiontime", "AFTER");
      break;
    case TR_TIME_DEFERRED:
      fprintf (fp, MSGFMT, "conditiontime", "DEFERRED");
      break;
    case TR_TIME_NULL:
      break;
    }

  /* eventtype */
  db_trigger_event (triggerobj, &event);
  switch (event)
    {
    case TR_EVENT_UPDATE:
      fprintf (fp, MSGFMT, "eventtype", "UPDATE");
      break;
    case TR_EVENT_STATEMENT_UPDATE:
      fprintf (fp, MSGFMT, "eventtype", "STATEMENT UPDATE");
      break;
    case TR_EVENT_DELETE:
      fprintf (fp, MSGFMT, "eventtype", "DELETE");
      break;
    case TR_EVENT_STATEMENT_DELETE:
      fprintf (fp, MSGFMT, "eventtype", "STATEMENT DELETE");
      break;
    case TR_EVENT_INSERT:
      fprintf (fp, MSGFMT, "eventtype", "INSERT");
      break;
    case TR_EVENT_STATEMENT_INSERT:
      fprintf (fp, MSGFMT, "eventtype", "STATEMENT INSERT");
      break;

    case TR_EVENT_COMMIT:
      fprintf (fp, MSGFMT, "eventtype", "COMMIT");
      break;
    case TR_EVENT_ROLLBACK:
      fprintf (fp, MSGFMT, "eventtype", "ROLLBACK");
      break;
    default:
      break;
    }

  /* trigger action */
  db_trigger_action_type (triggerobj, &action_type);
  switch (action_type)
    {
    case TR_ACT_EXPRESSION:	/* act like TR_ACT_PRINT */
    case TR_ACT_PRINT:
      db_trigger_action (triggerobj, &action);
      fprintf (fp, MSGFMT, "action", action);
      if (action)
	{
	  db_string_free ((char *) action);
	}
      break;

    case TR_ACT_REJECT:
      fprintf (fp, MSGFMT, "action", "REJECT");
      break;
    case TR_ACT_INVALIDATE:
      fprintf (fp, MSGFMT, "action", "INVALIDATE TRANSACTION");
      break;
    case TR_ACT_NULL:
      break;
    }

  /* target class & att */
  db_trigger_class (triggerobj, &target_class);
  if (target_class)
    {
      fprintf (fp, MSGFMT, "target_class", db_get_class_name (target_class));
    }

  db_trigger_attribute (triggerobj, &attr);
  if (attr)
    {
      fprintf (fp, MSGFMT, "target_att", attr);
      db_string_free ((char *) attr);
    }

  /* condition */
  db_trigger_condition (triggerobj, &condition);
  if (condition)
    {
      fprintf (fp, MSGFMT, "condition", condition);
      db_string_free ((char *) condition);
    }

  /* actiontime */
  db_trigger_action_time (triggerobj, &actiontime);
  switch (actiontime)
    {
    case TR_TIME_BEFORE:
      fprintf (fp, MSGFMT, "actiontime", "BEFORE");
      break;
    case TR_TIME_AFTER:
      fprintf (fp, MSGFMT, "actiontime", "AFTER");
      break;
    case TR_TIME_DEFERRED:
      fprintf (fp, MSGFMT, "actiontime", "DEFERRED");
      break;
    case TR_TIME_NULL:
      break;
    }

  /* status */
  db_trigger_status (triggerobj, &status);
  switch (status)
    {
    case TR_STATUS_ACTIVE:
      fprintf (fp, MSGFMT, "status", "ACTIVE");
      break;
    case TR_STATUS_INACTIVE:
      fprintf (fp, MSGFMT, "status", "INACTIVE");
      break;
    case TR_STATUS_INVALID:
      fprintf (fp, MSGFMT, "status", "INVALID");
    }

  /* priority */
  db_trigger_priority (triggerobj, &priority);
  sprintf (pri_string, "%4.5f", priority);
  fprintf (fp, MSGFMT, "priority", pri_string);
}
