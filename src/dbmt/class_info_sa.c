/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * class_info_sa.c -
 */

#ident "$Id$"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#ifdef WIN32
#include <winsock2.h>
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif

#include "dbi.h"
#include "emgrver.h"
#include "dbmt_porting.h"
#include "ems_sa.h"
#ifdef	_DEBUG_
#include "deb.h"
#endif

#define MSGFMT	"%s %s\n"

#define ARG_GET(VAR, ARG)                                       \
        do {                                                    \
          char *_macro_tmp_ptr;                                 \
          _macro_tmp_ptr = ARG;                                 \
          VAR = (_macro_tmp_ptr ? _macro_tmp_ptr : "");         \
        } while (0)

static int dbmt_user_login (int argc, char *argv[], int opt_begin);
static int class_info (int argc, char *argv[], int opt_begin);

static void write_err_msg (char *errfile, char *msg);
static void write_class_info (FILE * fp, DB_OBJECT * classobj);
T_EMGR_VERSION CLI_VERSION;

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
  char *dbname, *uid, *passwd, *outfile, *errfile;
  FILE *fp;
  DB_OBJLIST *objlist, *temp;
  DB_OBJECT *classobj;
  char *ver_str;


  if (argc < 8)
    {
      return 1;
    }

  putenv ("CUBRID_ERROR_LOG=NULL");
  close (2);

  ARG_GET (dbname, argv[opt_begin++]);
  ARG_GET (uid, argv[opt_begin++]);
  ARG_GET (passwd, argv[opt_begin++]);
  ARG_GET (outfile, argv[opt_begin++]);
  ARG_GET (errfile, argv[opt_begin++]);
  ARG_GET (ver_str, argv[opt_begin++]);
  CLI_VERSION = (T_EMGR_VERSION) atoi (ver_str);


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
  if (objlist)
    db_objlist_free (objlist);

  if (CLI_VERSION <= EMGR_MAKE_VER (1, 1))
    {
      if (db_is_vclass (classobj))
	fprintf (fp, MSGFMT, "virtual", "y");
      else
	fprintf (fp, MSGFMT, "virtual", "n");
    }
  else
    {
      if (db_is_vclass (classobj))
	fprintf (fp, MSGFMT, "virtual", "view");
      else
	fprintf (fp, MSGFMT, "virtual", "normal");
    }

  fprintf (fp, MSGFMT, "close", "class");
}

static int
dbmt_user_login (int argc, char *argv[], int opt_begin)
{
  char *outfile, *errfile, *dbname, *dbuser, *dbpasswd;
  FILE *outfp = NULL, *errfp = NULL;
  bool isdba = false;

  if (argc - opt_begin < 5)
    return -1;

  ARG_GET (outfile, argv[opt_begin++]);
  ARG_GET (errfile, argv[opt_begin++]);
  ARG_GET (dbname, argv[opt_begin++]);
  ARG_GET (dbuser, argv[opt_begin++]);
  ARG_GET (dbpasswd, argv[opt_begin++]);

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
  if (outfp)
    fclose (outfp);
  if (errfp)
    fclose (errfp);
  return -1;
}
