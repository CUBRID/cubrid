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
 * dbmt_user.c - 
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

#include "cm_porting.h"
#include "cm_server_util.h"
#include "cm_config.h"
#include "cm_user.h"

#define CUBRID_PASS_OPEN_TAG		"<<<:"
#define CUBRID_PASS_OPEN_TAG_LEN	strlen(CUBRID_PASS_OPEN_TAG)
#define CUBRID_PASS_CLOSE_TAG		">>>:"
#define CUBRID_PASS_CLOSE_TAG_LEN	strlen(CUBRID_PASS_CLOSE_TAG)

static int get_dbmt_user_dbinfo (char *strbuf, T_DBMT_USER_DBINFO * dbinfo);

int
dbmt_user_read (T_DBMT_USER * dbmt_user, char *_dbmt_error)
{
  T_DBMT_USER_INFO *user_info = NULL;
  T_DBMT_USER_DBINFO *dbinfo;
  int num_dbmt_user = 0;
  int num_dbinfo;
  FILE *fp = NULL;
  char strbuf[1024];
  char cur_user[DBMT_USER_NAME_LEN];
  int retval = ERR_NO_ERROR;
  int lock_fd;

  memset (dbmt_user, 0, sizeof (T_DBMT_USER));

  lock_fd = uCreateLockFile (conf_get_dbmt_file (FID_LOCK_DBMT_PASS, strbuf));
  if (lock_fd < 0)
    return ERR_TMPFILE_OPEN_FAIL;

  fp = fopen (conf_get_dbmt_file (FID_DBMT_CUBRID_PASS, strbuf), "r");
  if (fp == NULL)
    {
      strcpy (_dbmt_error,
	      conf_get_dbmt_file2 (FID_DBMT_CUBRID_PASS, strbuf));
      retval = ERR_FILE_OPEN_FAIL;
      goto read_dbmt_user_error;
    }

  memset (cur_user, 0, sizeof (cur_user));
  while (fgets (strbuf, sizeof (strbuf), fp))
    {
      ut_trim (strbuf);
      if (strncmp (strbuf, CUBRID_PASS_OPEN_TAG, CUBRID_PASS_OPEN_TAG_LEN) ==
	  0)
	{
	  strcpy (cur_user, strbuf + CUBRID_PASS_OPEN_TAG_LEN);
	  if (cur_user[0] == '\0')
	    continue;
	  num_dbmt_user++;
	  MALLOC_USER_INFO (user_info, num_dbmt_user);
	  if (user_info == NULL)
	    {
	      retval = ERR_MEM_ALLOC;
	      goto read_dbmt_user_error;
	    }

	  /* user name set */
	  strcpy (user_info[num_dbmt_user - 1].user_name, cur_user);
	  dbinfo = NULL;
	  num_dbinfo = 0;
	  continue;
	}

      if (cur_user[0] == '\0')
	continue;

      if (strncmp (strbuf, CUBRID_PASS_CLOSE_TAG, CUBRID_PASS_CLOSE_TAG_LEN)
	  == 0)
	{
	  if (strcmp (strbuf + CUBRID_PASS_CLOSE_TAG_LEN, cur_user) != 0)
	    {
	      strcpy (_dbmt_error,
		      conf_get_dbmt_file2 (FID_DBMT_CUBRID_PASS, strbuf));
	      retval = ERR_FILE_INTEGRITY;
	      goto read_dbmt_user_error;
	    }
	  user_info[num_dbmt_user - 1].dbinfo = dbinfo;
	  user_info[num_dbmt_user - 1].num_dbinfo = num_dbinfo;
	  cur_user[0] = '\0';
	}
      else
	{
	  T_DBMT_USER_DBINFO tmp_dbinfo;
	  if (get_dbmt_user_dbinfo (strbuf, &tmp_dbinfo) < 0)
	    continue;
	  num_dbinfo++;
	  MALLOC_USER_DBINFO (dbinfo, num_dbinfo);
	  if (dbinfo == NULL)
	    {
	      retval = ERR_MEM_ALLOC;
	      goto read_dbmt_user_error;
	    }
	  dbinfo[num_dbinfo - 1] = tmp_dbinfo;
	}
    }
  fclose (fp);
  fp = NULL;

  if (num_dbmt_user < 1)
    {
      strcpy (_dbmt_error,
	      conf_get_dbmt_file2 (FID_DBMT_CUBRID_PASS, strbuf));
      retval = ERR_FILE_INTEGRITY;
      goto read_dbmt_user_error;
    }

  dbmt_user->num_dbmt_user = num_dbmt_user;
  dbmt_user->user_info = user_info;

  fp = fopen (conf_get_dbmt_file (FID_DBMT_PASS, strbuf), "r");
  if (fp == NULL)
    {
      strcpy (_dbmt_error, conf_get_dbmt_file2 (FID_DBMT_PASS, strbuf));
      retval = ERR_FILE_OPEN_FAIL;
      goto read_dbmt_user_error;
    }

  while (fgets (strbuf, sizeof (strbuf), fp))
    {
      char *tok[2];
      int i;

      ut_trim (strbuf);
      if (string_tokenize2 (strbuf, tok, 2, ':') < 0)
	continue;
      for (i = 0; i < dbmt_user->num_dbmt_user; i++)
	{
	  if (strcmp (tok[0], dbmt_user->user_info[i].user_name) == 0)
	    {
	      strcpy (dbmt_user->user_info[i].user_passwd, tok[1]);
	      break;
	    }
	}
    }
  fclose (fp);

  uRemoveLockFile (lock_fd);
  return ERR_NO_ERROR;

read_dbmt_user_error:
  if (fp)
    fclose (fp);
  dbmt_user_free (dbmt_user);
  uRemoveLockFile (lock_fd);
  return retval;
}

void
dbmt_user_free (T_DBMT_USER * dbmt_user)
{
  int i;

  if (dbmt_user->user_info)
    {
      for (i = 0; i < dbmt_user->num_dbmt_user; i++)
	{
	  if (dbmt_user->user_info[i].dbinfo)
	    free (dbmt_user->user_info[i].dbinfo);
	}
      free (dbmt_user->user_info);
    }
}

int
dbmt_user_write_cubrid_pass (T_DBMT_USER * dbmt_user, char *_dbmt_error)
{
  FILE *fp;
  char tmpfile[512];
  int i, j;
  char strbuf[1024];
  int lock_fd;

  sprintf (tmpfile, "%s/tmp/DBMT_util_pass.%d", sco.szCubrid,
	   (int) getpid ());
  fp = fopen (tmpfile, "w");
  if (fp == NULL)
    {
      return ERR_TMPFILE_OPEN_FAIL;
    }
  for (i = 0; i < dbmt_user->num_dbmt_user; i++)
    {
      if (dbmt_user->user_info[i].user_name[0] == '\0')
	continue;
      fprintf (fp, "%s%s\n", CUBRID_PASS_OPEN_TAG,
	       dbmt_user->user_info[i].user_name);
      for (j = 0; j < dbmt_user->user_info[i].num_dbinfo; j++)
	{
	  if (dbmt_user->user_info[i].dbinfo[j].dbname[0] == '\0')
	    continue;
	  dbmt_user_db_auth_str (&(dbmt_user->user_info[i].dbinfo[j]),
				 strbuf);
	  fprintf (fp, "%s:%s\n", dbmt_user->user_info[i].dbinfo[j].dbname,
		   strbuf);
	}
      fprintf (fp, "%s%s\n", CUBRID_PASS_CLOSE_TAG,
	       dbmt_user->user_info[i].user_name);
    }
  fclose (fp);

  lock_fd = uCreateLockFile (conf_get_dbmt_file (FID_LOCK_DBMT_PASS, strbuf));
  if (lock_fd < 0)
    {
      unlink (tmpfile);
      return ERR_TMPFILE_OPEN_FAIL;
    }
  move_file (tmpfile, conf_get_dbmt_file (FID_DBMT_CUBRID_PASS, strbuf));
  uRemoveLockFile (lock_fd);

  return ERR_NO_ERROR;
}

void
dbmt_user_set_dbinfo (T_DBMT_USER_DBINFO * dbinfo, char *dbname, char *auth,
		      char *uid, char *passwd)
{
  strncpy (dbinfo->dbname, dbname, sizeof (dbinfo->dbname) - 1);
  strncpy (dbinfo->auth, auth, sizeof (dbinfo->auth) - 1);
  strncpy (dbinfo->uid, uid, sizeof (dbinfo->uid) - 1);
  strncpy (dbinfo->passwd, passwd, sizeof (dbinfo->passwd) - 1);
}

void
dbmt_user_set_userinfo (T_DBMT_USER_INFO * usrinfo, char *user_name,
			char *user_passwd, int num_dbinfo,
			T_DBMT_USER_DBINFO * dbinfo)
{
  strncpy (usrinfo->user_name, user_name, sizeof (usrinfo->user_name) - 1);
  strncpy (usrinfo->user_passwd, user_passwd,
	   sizeof (usrinfo->user_passwd) - 1);
  usrinfo->num_dbinfo = num_dbinfo;
  usrinfo->dbinfo = dbinfo;
}

void
dbmt_user_db_auth_str (T_DBMT_USER_DBINFO * dbinfo, char *buf)
{
  if ((strcmp (dbinfo->dbname, "unicas") == 0) ||
      strcmp (dbinfo->dbname, "dbcreate") == 0)
    {
      strcpy (buf, dbinfo->auth);
    }
  else
    {
      sprintf (buf, "%s;%s;%s", dbinfo->auth, dbinfo->uid, dbinfo->passwd);
    }
}

int
dbmt_user_search (T_DBMT_USER_INFO * user_info, char *dbname)
{
  int i;

  for (i = 0; i < user_info->num_dbinfo; i++)
    {
      if (strcmp (user_info->dbinfo[i].dbname, dbname) == 0)
	return i;
    }
  return -1;
}

int
dbmt_user_write_pass (T_DBMT_USER * dbmt_user, char *_dbmt_error)
{
  char tmpfile[512], strbuf[1024];
  FILE *fp;
  int i, lock_fd;

  sprintf (tmpfile, "%s/tmp/DBMT_util_pass.%d", sco.szCubrid,
	   (int) getpid ());
  fp = fopen (tmpfile, "w");
  if (fp == NULL)
    {
      return ERR_TMPFILE_OPEN_FAIL;
    }
  for (i = 0; i < dbmt_user->num_dbmt_user; i++)
    {
      if (dbmt_user->user_info[i].user_name[0] == '\0')
	continue;
      fprintf (fp, "%s:%s\n", dbmt_user->user_info[i].user_name,
	       dbmt_user->user_info[i].user_passwd);
    }
  fclose (fp);

  lock_fd = uCreateLockFile (conf_get_dbmt_file (FID_LOCK_DBMT_PASS, strbuf));
  if (lock_fd < 0)
    {
      unlink (tmpfile);
      return ERR_TMPFILE_OPEN_FAIL;
    }
  move_file (tmpfile, conf_get_dbmt_file (FID_DBMT_PASS, strbuf));
  uRemoveLockFile (lock_fd);

  return ERR_NO_ERROR;
}

void
dbmt_user_db_delete (T_DBMT_USER * dbmt_user, char *dbname)
{
  int i, j;

  for (i = 0; i < dbmt_user->num_dbmt_user; i++)
    {
      for (j = 0; j < dbmt_user->user_info[i].num_dbinfo; j++)
	{
	  if (strcmp (dbmt_user->user_info[i].dbinfo[j].dbname, dbname) == 0)
	    dbmt_user->user_info[i].dbinfo[j].dbname[0] = '\0';
	}
    }
}

int
dbmt_user_add_dbinfo (T_DBMT_USER_INFO * usrinfo, T_DBMT_USER_DBINFO * dbinfo)
{
  int i;

  i = usrinfo->num_dbinfo + 1;
  MALLOC_USER_DBINFO (usrinfo->dbinfo, i);
  if (usrinfo->dbinfo == NULL)
    return ERR_MEM_ALLOC;
  usrinfo->num_dbinfo = i;
  for (i--; i >= 1; i--)
    usrinfo->dbinfo[i] = usrinfo->dbinfo[i - 1];
  usrinfo->dbinfo[0] = *dbinfo;
  return ERR_NO_ERROR;
}

static int
get_dbmt_user_dbinfo (char *strbuf, T_DBMT_USER_DBINFO * usr_dbinfo)
{
  char *dbinfo[2], *user_info[3];

  memset (usr_dbinfo, 0, sizeof (T_DBMT_USER_DBINFO));

  if (string_tokenize2 (strbuf, dbinfo, 2, ':') < 0)
    return -1;

  if ((strcmp (dbinfo[0], "unicas") == 0) ||
      (strcmp (dbinfo[0], "dbcreate") == 0))
    {
      user_info[0] = dbinfo[1];
      user_info[1] = user_info[2] = "";
    }
  else
    {
      if (string_tokenize2 (dbinfo[1], user_info, 3, ';') < 0)
	return -1;
    }
  dbmt_user_set_dbinfo (usr_dbinfo, dbinfo[0], user_info[0], user_info[1],
			user_info[2]);
  return 0;
}
