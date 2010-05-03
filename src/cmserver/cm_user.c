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

#if defined(WINDOWS)
#include <process.h>
#else
#include <unistd.h>
#endif

#include "cm_porting.h"
#include "cm_server_util.h"
#include "cm_user.h"

#define CUBRID_PASS_OPEN_TAG		"<<<:"
#define CUBRID_PASS_OPEN_TAG_LEN	strlen(CUBRID_PASS_OPEN_TAG)
#define CUBRID_PASS_CLOSE_TAG		">>>:"
#define CUBRID_PASS_CLOSE_TAG_LEN	strlen(CUBRID_PASS_CLOSE_TAG)

int
dbmt_user_read (T_DBMT_USER * dbmt_user, char *_dbmt_error)
{
  T_DBMT_USER_INFO *user_info = NULL;
  T_DBMT_USER_DBINFO *user_dbinfo = NULL;
  T_DBMT_USER_AUTHINFO *user_authinfo = NULL;
  int num_dbmt_user = 0;
  int num_dbinfo = 0;
  int num_authinfo = 0;
  char *authinfo[2];
  char *dbinfo[4];
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
	  snprintf (cur_user, sizeof (cur_user) - 1, "%s", strbuf
		    + CUBRID_PASS_OPEN_TAG_LEN);
	  if (cur_user[0] == '\0')
	    continue;

	  user_info =
	    (T_DBMT_USER_INFO *) increase_capacity (user_info,
						    sizeof (T_DBMT_USER_INFO),
						    num_dbmt_user,
						    num_dbmt_user + 1);
	  if (user_info == NULL)
	    {
	      retval = ERR_MEM_ALLOC;
	      goto read_dbmt_user_error;
	    }
	  num_dbmt_user++;

	  /* user name set */
	  strcpy_limit (user_info[num_dbmt_user - 1].user_name, cur_user,
			DBMT_USER_NAME_LEN);
	  user_dbinfo = NULL;
	  num_dbinfo = 0;
	  num_authinfo = 0;
	  continue;
	}

      if (cur_user[0] == '\0')
	{
	  continue;
	}

      if (strncmp (strbuf, CUBRID_PASS_CLOSE_TAG, CUBRID_PASS_CLOSE_TAG_LEN)
	  == 0)
	{
	  if (strcmp (strbuf + CUBRID_PASS_CLOSE_TAG_LEN, cur_user) != 0)
	    {
	      strcpy (_dbmt_error, conf_get_dbmt_file2 (FID_DBMT_CUBRID_PASS,
							strbuf));
	      retval = ERR_FILE_INTEGRITY;
	      goto read_dbmt_user_error;
	    }
	  if (user_info != NULL)
	    {
	      user_info[num_dbmt_user - 1].dbinfo = user_dbinfo;
	      user_dbinfo = NULL;
	      user_info[num_dbmt_user - 1].num_dbinfo = num_dbinfo;

	      user_info[num_dbmt_user - 1].authinfo = user_authinfo;
	      user_authinfo = NULL;
	      user_info[num_dbmt_user - 1].num_authinfo = num_authinfo;
	    }
	  cur_user[0] = '\0';
	}
      else
	{
	  /* get authority info and database info */
	  if (string_tokenize2 (strbuf, authinfo, 2, ':') < 0)
	    {
	      continue;
	    }
	  else
	    {
	      if ((strcmp (authinfo[0], "unicas") == 0) ||
		  (strcmp (authinfo[0], "dbcreate") == 0) ||
		  (strcmp (authinfo[0], "statusmonitorauth") == 0))
		{
		  T_DBMT_USER_AUTHINFO tmp_authinfo;
		  memset (&tmp_authinfo, 0, sizeof (T_DBMT_USER_AUTHINFO));

		  dbmt_user_set_authinfo (&tmp_authinfo, authinfo[0],
					  authinfo[1]);

		  user_authinfo =
		    (T_DBMT_USER_AUTHINFO *) increase_capacity (user_authinfo,
								sizeof
								(T_DBMT_USER_AUTHINFO),
								num_authinfo,
								num_authinfo +
								1);
		  if (user_authinfo == NULL)
		    {
		      retval = ERR_MEM_ALLOC;
		      goto read_dbmt_user_error;
		    }
		  num_authinfo++;
		  user_authinfo[num_authinfo - 1] = tmp_authinfo;
		}
	      else
		{
		  T_DBMT_USER_DBINFO tmp_dbinfo;
		  memset (&tmp_dbinfo, 0, sizeof (T_DBMT_USER_DBINFO));

		  if (string_tokenize2 (authinfo[1], dbinfo, 4, ';') < 0)
		    continue;
		  dbmt_user_set_dbinfo (&tmp_dbinfo, authinfo[0], dbinfo[0],
					dbinfo[1], dbinfo[2], dbinfo[3]);

		  user_dbinfo =
		    (T_DBMT_USER_DBINFO *) increase_capacity (user_dbinfo,
							      sizeof
							      (T_DBMT_USER_DBINFO),
							      num_dbinfo,
							      num_dbinfo + 1);
		  if (user_dbinfo == NULL)
		    {
		      retval = ERR_MEM_ALLOC;
		      goto read_dbmt_user_error;
		    }
		  num_dbinfo++;
		  user_dbinfo[num_dbinfo - 1] = tmp_dbinfo;
		}
	    }
	}
    }
  fclose (fp);
  fp = NULL;

  if (user_dbinfo != NULL)
    {
      free (user_dbinfo);
      user_dbinfo = NULL;
    }
  if (user_authinfo != NULL)
    {
      free (user_authinfo);
      user_authinfo = NULL;
    }

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
	      snprintf (dbmt_user->user_info[i].user_passwd,
			sizeof (dbmt_user->user_info[i].user_passwd) - 1,
			"%s", tok[1]);
	      break;
	    }
	}
    }
  fclose (fp);

  uRemoveLockFile (lock_fd);
  return ERR_NO_ERROR;

read_dbmt_user_error:
  if (fp != NULL)
    {
      fclose (fp);
    }
  if (user_info != NULL)
    {
      free (user_info);
    }
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
	  if (dbmt_user->user_info[i].authinfo)
	    free (dbmt_user->user_info[i].authinfo);
	}
      free (dbmt_user->user_info);
    }
}

int
dbmt_user_write_auth (T_DBMT_USER * dbmt_user, char *_dbmt_error)
{
  FILE *fp;
  char tmpfile[512];
  int i, j;
  char strbuf[1024];
  int lock_fd;

#if !defined (DO_NOT_USE_CUBRIDENV)
  sprintf (tmpfile, "%s/tmp/DBMT_util_pass.%d", sco.szCubrid,
	   (int) getpid ());
#else
  sprintf (tmpfile, "%s/DBMT_util_pass.%d", CUBRID_TMPDIR, (int) getpid ());
#endif
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
      for (j = 0; j < dbmt_user->user_info[i].num_authinfo; j++)
	{
	  if (dbmt_user->user_info[i].authinfo[j].domain[0] == '\0')
	    continue;
	  fprintf (fp, "%s:%s\n", dbmt_user->user_info[i].authinfo[j].domain,
		   dbmt_user->user_info[i].authinfo[j].auth);
	}
      for (j = 0; j < dbmt_user->user_info[i].num_dbinfo; j++)
	{
	  if (dbmt_user->user_info[i].dbinfo[j].dbname[0] == '\0')
	    continue;
	  fprintf (fp, "%s:%s;%s;%s;%s\n",
		   dbmt_user->user_info[i].dbinfo[j].dbname,
		   dbmt_user->user_info[i].dbinfo[j].auth,
		   dbmt_user->user_info[i].dbinfo[j].uid,
		   dbmt_user->user_info[i].dbinfo[j].passwd,
		   dbmt_user->user_info[i].dbinfo[j].broker_address);
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
dbmt_user_set_dbinfo (T_DBMT_USER_DBINFO * dbinfo, const char *dbname,
		      const char *auth, const char *uid, const char *passwd,
		      const char *broker_address)
{
  strcpy_limit (dbinfo->dbname, dbname, sizeof (dbinfo->dbname));
  strcpy_limit (dbinfo->auth, auth, sizeof (dbinfo->auth));
  strcpy_limit (dbinfo->uid, uid, sizeof (dbinfo->uid));
  strcpy_limit (dbinfo->passwd, passwd, sizeof (dbinfo->passwd));
  if (broker_address == NULL)
    {
      dbinfo->broker_address[0] = '\0';
    }
  else
    {
      strcpy_limit (dbinfo->broker_address, broker_address,
		    sizeof (dbinfo->broker_address));
    }
}

void
dbmt_user_set_authinfo (T_DBMT_USER_AUTHINFO * authinfo, const char *domain,
			const char *auth)
{
  strcpy_limit (authinfo->domain, domain, sizeof (authinfo->domain));
  strcpy_limit (authinfo->auth, auth, sizeof (authinfo->auth));
}

void
dbmt_user_set_userinfo (T_DBMT_USER_INFO * usrinfo, char *user_name,
			char *user_passwd, int num_authinfo,
			T_DBMT_USER_AUTHINFO * authinfo, int num_dbinfo,
			T_DBMT_USER_DBINFO * dbinfo)
{
  strcpy_limit (usrinfo->user_name, user_name, sizeof (usrinfo->user_name));
  strcpy_limit (usrinfo->user_passwd, user_passwd,
		sizeof (usrinfo->user_passwd));
  usrinfo->num_authinfo = num_authinfo;
  usrinfo->authinfo = authinfo;
  usrinfo->num_dbinfo = num_dbinfo;
  usrinfo->dbinfo = dbinfo;
}

int
dbmt_user_search (T_DBMT_USER_INFO * user_info, const char *dbname)
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

#if !defined (DO_NOT_USE_CUBRIDENV)
  sprintf (tmpfile, "%s/tmp/DBMT_util_pass.%d", sco.szCubrid,
	   (int) getpid ());
#else
  sprintf (tmpfile, "%s/DBMT_util_pass.%d", CUBRID_TMPDIR, (int) getpid ());
#endif

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
  int i = usrinfo->num_dbinfo;

  usrinfo->dbinfo =
    (T_DBMT_USER_DBINFO *) increase_capacity (usrinfo->dbinfo,
					      sizeof (T_DBMT_USER_DBINFO), i,
					      i + 1);
  if (usrinfo->dbinfo == NULL)
    return ERR_MEM_ALLOC;
  i++;
  usrinfo->num_dbinfo = i;
  for (i--; i >= 1; i--)
    usrinfo->dbinfo[i] = usrinfo->dbinfo[i - 1];
  usrinfo->dbinfo[0] = *dbinfo;

  return ERR_NO_ERROR;
}
