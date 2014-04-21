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
 * cm_class_info_sa.c -
 */

#ident "$Id$"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#if defined(WINDOWS)
#include <winsock2.h>
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif

#include "dbi.h"
#include "cm_portable.h"
#include "cm_execute_sa.h"

#define MSGFMT	"%s %s\n"

static int dbmt_user_login (int argc, char *argv[], int opt_begin);
static int class_info (int argc, char *argv[], int opt_begin);

static void write_err_msg (const char *errfile, char *msg);
static void write_class_info (FILE * fp, DB_OBJECT * classobj);

int
main (int argc, char *argv[])
{
  int opcode;
  int err_code;

  if (argc < 2)
    return 1;

  opcode = atoi (argv[1]);

  switch (opcode)
    {
    case EMS_SA_CLASS_INFO:
      err_code = class_info (argc, argv, 2);
      break;
    case EMS_SA_DBMT_USER_LOGIN:
      err_code = dbmt_user_login (argc, argv, 2);
      break;
    default:
      err_code = 1;
      break;
    }

  return err_code;
}

static int
class_info (int argc, char *argv[], int opt_begin)
{
  const char *dbname, *uid, *passwd, *outfile, *errfile;
  FILE *fp;
  DB_OBJLIST *objlist, *temp;
  DB_OBJECT *classobj;
  const char *ver_str;

  if (argc < 8)
    {
      return 1;
    }

  putenv ((char *) "CUBRID_ERROR_LOG=NULL");
  close (2);

  dbname = argv[opt_begin++];
  dbname = (dbname) ? dbname : "";

  uid = argv[opt_begin++];
  uid = (uid) ? uid : "";

  passwd = argv[opt_begin++];
  passwd = (passwd) ? passwd : "";

  outfile = argv[opt_begin++];
  outfile = (outfile) ? outfile : "";

  errfile = argv[opt_begin++];
  errfile = (errfile) ? errfile : "";

  ver_str = argv[opt_begin++];
  ver_str = (ver_str) ? ver_str : "";

  db_login (uid, passwd);
  if (db_restart (argv[0], 0, dbname) < 0)
    {
      write_err_msg (errfile, (char *) db_error_string (1));
      return 0;
    }

  fp = fopen (outfile, "w");
  if (fp == NULL)
    {
      db_shutdown ();
      return 0;
    }

  fprintf (fp, MSGFMT, "open", "systemclass");
  objlist = db_get_all_classes ();
  for (temp = objlist; temp != NULL; temp = temp->next)
    {
      classobj = temp->op;
      if (db_is_system_class (classobj))
	write_class_info (fp, classobj);
    }
  db_objlist_free (objlist);
  fprintf (fp, MSGFMT, "close", "systemclass");

  fprintf (fp, MSGFMT, "open", "userclass");
  objlist = db_get_all_classes ();
  for (temp = objlist; temp != NULL; temp = temp->next)
    {
      classobj = temp->op;
      if (!db_is_system_class (classobj))
	write_class_info (fp, classobj);
    }
  db_objlist_free (objlist);
  fprintf (fp, MSGFMT, "close", "userclass");

  fclose (fp);
  db_shutdown ();
  return 0;
}

static void
write_err_msg (const char *errfile, char *msg)
{
  FILE *fp;

  fp = fopen (errfile, "w");
  if (fp != NULL)
    {
      fprintf (fp, "%s", msg);
      fclose (fp);
    }
  return;
}

static void
write_class_info (FILE * fp, DB_OBJECT * classobj)
{
  DB_OBJLIST *objlist, *temp;
  DB_OBJECT *obj;
  DB_VALUE v;

  fprintf (fp, MSGFMT, "open", "class");
  fprintf (fp, MSGFMT, "classname", db_get_class_name (classobj));

  obj = db_get_owner (classobj);
  db_get (obj, "name", &v);
  fprintf (fp, MSGFMT, "owner", db_get_string (&v));

  objlist = db_get_superclasses (classobj);
  for (temp = objlist; temp != NULL; temp = temp->next)
    {
      fprintf (fp, MSGFMT, "superclass", db_get_class_name (temp->op));
    }
  if (objlist != NULL)
    db_objlist_free (objlist);

  if (db_is_vclass (classobj))
    fprintf (fp, MSGFMT, "virtual", "view");
  else
    fprintf (fp, MSGFMT, "virtual", "normal");

  fprintf (fp, MSGFMT, "close", "class");
}

static int
dbmt_user_login (int argc, char *argv[], int opt_begin)
{
  const char *outfile, *errfile, *dbname, *dbuser, *dbpasswd;
  FILE *outfp = NULL, *errfp = NULL;
  bool isdba = false;

  if (argc - opt_begin < 5)
    return -1;

  outfile = argv[opt_begin++];
  outfile = (outfile) ? outfile : "";

  errfile = argv[opt_begin++];
  errfile = (errfile) ? errfile : "";

  dbname = argv[opt_begin++];
  dbname = (dbname) ? dbname : "";

  dbuser = argv[opt_begin++];
  dbuser = (dbuser) ? dbuser : "";

  dbpasswd = argv[opt_begin++];
  dbpasswd = (dbpasswd) ? dbpasswd : "";

  outfp = fopen (outfile, "w");
  errfp = fopen (errfile, "w");
  if (outfp == NULL || errfp == NULL)
    {
      goto login_err;
    }

  db_login (dbuser, dbpasswd);
  if (db_restart (argv[0], 0, dbname) < 0)
    {
      fprintf (errfp, "%s", db_error_string (1));
      goto login_err;
    }

  if (strcasecmp (dbuser, "DBA") == 0)
    {
      isdba = true;
    }
  else
    {
      DB_OBJECT *user, *obj;
      DB_VALUE v;
      DB_COLLECTION *col;
      int i;
      char *username;

      user = db_find_user (dbuser);
      if (user == NULL)
	{
	  fprintf (errfp, "%s", db_error_string (1));
	  goto login_err;
	}
      db_get (user, "groups", &v);
      col = db_get_set (&v);
      for (i = 0; i < db_set_size (col); i++)
	{
	  db_set_get (col, i, &v);
	  obj = db_get_object (&v);
	  db_get (obj, "name", &v);
	  username = db_get_string (&v);
	  if (username != NULL && strcasecmp (username, "DBA") == 0)
	    {
	      isdba = true;
	      break;
	    }
	}
    }

  if (isdba == true)
    fprintf (outfp, "isdba\n");
  else
    fprintf (outfp, "isnotdba\n");

  db_shutdown ();
  fclose (outfp);
  fclose (errfp);
  return 0;

login_err:
  db_shutdown ();
  if (outfp != NULL)
    fclose (outfp);
  if (errfp != NULL)
    fclose (errfp);
  return -1;
}
