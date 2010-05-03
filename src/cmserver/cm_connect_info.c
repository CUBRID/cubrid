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
 * cm_connect_info.c -
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#if defined(WINDOWS)
#include <process.h>
#else
#include <unistd.h>
#endif

#include "cm_porting.h"
#include "cm_server_util.h"
#include "cm_config.h"
#include "cm_connect_info.h"

int
dbmt_con_search (const char *ip, const char *port, char *cli_ver)
{
  FILE *infile;
  int lfd, retval;
  int get_len, buf_len;
  char *strbuf;
  char sbuf[512];
  char ip_t[20], port_t[10];

  /* check if ip is an existing ip */
  retval = 0;
  lfd = uCreateLockFile (conf_get_dbmt_file (FID_LOCK_CONN_LIST, sbuf));
  if (lfd < 0)
    return ERR_TMPFILE_OPEN_FAIL;

  infile = fopen (conf_get_dbmt_file (FID_CONN_LIST, sbuf), "r");
  if (infile != NULL)
    {
      strbuf = NULL;
      buf_len = get_len = 0;
      while ((get_len = ut_getline (&strbuf, &buf_len, infile)) != -1)
	{
	  /* remove character '\n' */
	  strbuf[strlen (strbuf) - 1] = '\0';

	  if (sscanf (strbuf, "%19s %9s", ip_t, port_t) == 2
	      && uStringEqual (ip, ip_t) && uStringEqual (port, port_t))
	    {
	      retval = 1;
	      sscanf (strbuf, "%*s %*s %*s %*s %14s", cli_ver);
	      break;
	    }
	  FREE_MEM (strbuf);
	  buf_len = 0;
	}
      fclose (infile);

      if (strbuf != NULL)
	{
	  FREE_MEM (strbuf);
	}
    }
  uRemoveLockFile (lfd);

  return retval;
}

int
dbmt_con_add (const char *ip, const char *port, const char *cli_ver,
	      const char *user_name)
{
  FILE *outfile;
  char strbuf[512];
  int lock_fd, retval;

  lock_fd = uCreateLockFile (conf_get_dbmt_file (FID_LOCK_CONN_LIST, strbuf));
  if (lock_fd < 0)
    return -1;

  retval = -1;
  outfile = fopen (conf_get_dbmt_file (FID_CONN_LIST, strbuf), "a");
  if (outfile != NULL)
    {
      time_to_str (time (NULL), "%04d/%02d/%02d %02d:%02d:%02d", strbuf,
		   TIME_STR_FMT_DATE_TIME);
      fprintf (outfile, "%s %s %s %s %s\n", ip, port, strbuf, cli_ver,
	       user_name);
      fclose (outfile);
      retval = 0;
    }
  uRemoveLockFile (lock_fd);

  return retval;
}

int
dbmt_con_delete (const char *ip, const char *port)
{
  char ip_t[20];
  char port_t[10];
  char version[16];
  char c_date[16];
  char c_time[16];
  char *strbuf;
  int buf_len, get_len;
  FILE *infile, *outfile;
  char tmpfile[512];
  char conn_list_file[512];
  int lock_fd, retval;

  lock_fd =
    uCreateLockFile (conf_get_dbmt_file (FID_LOCK_CONN_LIST, tmpfile));
  if (lock_fd < 0)
    return -1;

  conf_get_dbmt_file (FID_CONN_LIST, conn_list_file);
  infile = fopen (conn_list_file, "r");
  sprintf (tmpfile, "%s/DBMT_util_014.%d", sco.dbmt_tmp_dir, (int) getpid ());
  outfile = fopen (tmpfile, "w");

  if (infile == NULL || outfile == NULL)
    {
      if (infile != NULL)
	fclose (infile);
      if (outfile != NULL)
	fclose (outfile);
      retval = -1;
    }
  else
    {
      strbuf = NULL;
      buf_len = get_len = 0;
      while ((get_len = ut_getline (&strbuf, &buf_len, infile)) != -1)
	{
	  ut_trim (strbuf);
	  sscanf (strbuf, "%19s %9s %15s %15s %15s", ip_t, port_t, c_date,
		  c_time, version);

	  if (!uStringEqual (ip, ip_t) || !uStringEqual (port, port_t))
	    {
	      fprintf (outfile, "%s\n", strbuf);
	    }
	  FREE_MEM (strbuf);
	  buf_len = 0;
	}
      fclose (outfile);
      fclose (infile);

      if (strbuf != NULL)
	{
	  FREE_MEM (strbuf);
	}

      move_file (tmpfile, conn_list_file);
      retval = 0;
    }
  uRemoveLockFile (lock_fd);

  return retval;
}

int
dbmt_con_read_dbinfo (T_DBMT_CON_DBINFO * dbinfo, const char *ip,
		      const char *port, const char *dbname, char *_dbmt_error)
{
  FILE *infile;
  int lfd, retval = -1;
  char ip_t[20], port_t[10];
  char *prev, *next, *tok[2];
  char *strbuf;
  char sbuf[512];
  int buf_len, get_len;

  lfd = uCreateLockFile (conf_get_dbmt_file (FID_LOCK_CONN_LIST, sbuf));
  if (lfd < 0)
    {
      strcpy (_dbmt_error, "Open conlist.lock fail");
      return retval;
    }

  infile = fopen (conf_get_dbmt_file (FID_CONN_LIST, sbuf), "r");
  if (infile == NULL)
    {
      strcpy (_dbmt_error, "Open conlist fail");
      goto con_read_dbinfo_err;
    }

  strbuf = NULL;
  buf_len = get_len = 0;
  while ((get_len = ut_getline (&strbuf, &buf_len, infile)) != -1)
    {
      /* remove character '\n' */
      strbuf[strlen (strbuf) - 1] = '\0';

      if (sscanf (strbuf, "%19s %9s", ip_t, port_t) == 2 &&
	  uStringEqual (ip, ip_t) && uStringEqual (port, port_t))
	{
	  /* locate dbinfo_list */
	  int i = 0;
	  prev = strbuf;
	  while (*prev != '\0')
	    {
	      if (isspace (*prev))
		++i;
	      ++prev;
	      if (i == 6)
		break;
	    }
	  if (i == 5)
	    {
	      strcpy (_dbmt_error, "No db exists now");
	      retval = 0;
	      goto con_read_dbinfo_err;
	    }
	  else if (i < 5)
	    {
	      strcpy (_dbmt_error, "Conlist format error");
	      goto con_read_dbinfo_err;
	    }

	  /* parse every dbinfo */
	  while (prev != NULL)
	    {
	      next = strchr (prev, ';');
	      if (next != NULL)
		{
		  *next = '\0';
		  next++;
		}

	      if (string_tokenize2 (prev, tok, 2, ':') < 0)
		{
		  strcpy (_dbmt_error, "Conlist format error");
		  goto con_read_dbinfo_err;
		}

	      if (uStringEqual (dbname, tok[0]))
		{
		  strcpy_limit (dbinfo->dbname, tok[0],
				sizeof (dbinfo->dbname));
		  prev = tok[1];
		  if (string_tokenize2 (prev, tok, 2, ',') < 0)
		    {
		      strcpy (_dbmt_error, "Conlist format error");
		      goto con_read_dbinfo_err;
		    }
		  strcpy_limit (dbinfo->uid, tok[0], sizeof (dbinfo->uid));
		  strcpy_limit (dbinfo->passwd, tok[1],
				sizeof (dbinfo->passwd));

		  retval = 1;
		  break;
		}

	      /* parse the next dbinfo */
	      prev = next;
	    }
	  break;
	}
      FREE_MEM (strbuf);
      buf_len = 0;
    }

  if (retval == -1 && strlen (_dbmt_error) == 0)
    {
      retval = 0;
      strcpy (_dbmt_error, "Database not found");
    }

con_read_dbinfo_err:
  fclose (infile);

  if (strbuf != NULL)
    {
      FREE_MEM (strbuf);
    }

  uRemoveLockFile (lfd);

  return retval;
}

int
dbmt_con_write_dbinfo (T_DBMT_CON_DBINFO * dbinfo, const char *ip,
		       const char *port, const char *dbname, int creat_flag,
		       char *_dbmt_error)
{
  FILE *infile, *outfile;
  int lfd, retval = -1;
  char tmpfile[512], conn_list_file[512];
  char date[15], time[10];
  char *prev, *next, *tok[2];
  char *strbuf;
  char sbuf[512];
  int buf_len, get_len;

  T_DBMT_CON_INFO con_info;
  memset (&con_info, 0, sizeof (T_DBMT_CON_INFO));

  lfd = uCreateLockFile (conf_get_dbmt_file (FID_LOCK_CONN_LIST, sbuf));
  if (lfd < 0)
    {
      strcpy (_dbmt_error, "Open conlist.lock fail");
      return retval;
    }

  conf_get_dbmt_file (FID_CONN_LIST, conn_list_file);
  sprintf (tmpfile, "%s/DBMT_util_014.%d", sco.dbmt_tmp_dir, (int) getpid ());

  infile = fopen (conf_get_dbmt_file (FID_CONN_LIST, sbuf), "r");
  outfile = fopen (tmpfile, "w");
  if (infile == NULL || outfile == NULL)
    {
      if (infile != NULL)
	fclose (infile);
      if (outfile != NULL)
	fclose (outfile);

      strcpy (_dbmt_error, "Open conlist fail");
      goto con_write_dbinfo_err;
    }

  strbuf = NULL;
  buf_len = get_len = 0;
  while ((get_len = ut_getline (&strbuf, &buf_len, infile)) != -1)
    {
      /* remove character '\next' */
      strbuf[strlen (strbuf) - 1] = '\0';
      memset (&con_info, 0, sizeof (con_info));

      if (sscanf
	  (strbuf, "%19s %9s %10s %8s %15s %64s", con_info.cli_ip,
	   con_info.cli_port, date, time, con_info.cli_ver,
	   con_info.user_name) == 6 && uStringEqual (ip, con_info.cli_ip)
	  && uStringEqual (port, con_info.cli_port))
	{
	  /* locate dbinfo_list */
	  int i = 0;
	  T_DBMT_CON_DBINFO tmp_dbinfo;
	  prev = strbuf;
	  while (*prev != '\0')
	    {
	      if (isspace (*prev))
		++i;
	      ++prev;
	      if (i == 6)
		break;
	    }
	  if (i < 6 && creat_flag == 0)
	    {
	      strcpy (_dbmt_error, "Conlist format error");
	      goto con_write_dbinfo_err;
	    }

	  /* parse every dbinfo */
	  while (prev != NULL)
	    {
	      next = strchr (prev, ';');
	      if (next != NULL)
		{
		  *next = '\0';
		  next++;
		}

	      memset (&tmp_dbinfo, 0, sizeof (T_DBMT_CON_DBINFO));

	      if (string_tokenize2 (prev, tok, 2, ':') < 0)
		{
		  if (creat_flag == 0)
		    {
		      strcpy (_dbmt_error, "Conlist format error");
		      goto con_write_dbinfo_err;
		    }
		  else
		    {
		      creat_flag = 0;
		      /* no db exists, add dbinfo */
		      tmp_dbinfo = *dbinfo;
		    }
		}
	      else
		{
		  if (uStringEqual (dbname, tok[0]))
		    {
		      /* db exists, update dbinfo */
		      creat_flag = 0;
		      tmp_dbinfo = *dbinfo;
		    }
		  else
		    {
		      /* just record dbinfo */
		      strcpy_limit (tmp_dbinfo.dbname, tok[0],
				    sizeof (tmp_dbinfo.dbname) - 1);
		      prev = tok[1];
		      if (string_tokenize2 (prev, tok, 2, ',') < 0)
			{
			  strcpy (_dbmt_error, "Conlist format error");
			  goto con_write_dbinfo_err;
			}
		      strcpy_limit (tmp_dbinfo.uid, tok[0],
				    sizeof (tmp_dbinfo.uid));
		      strcpy_limit (tmp_dbinfo.passwd, tok[1],
				    sizeof (tmp_dbinfo.passwd));
		    }
		}

	      con_info.con_dbinfo =
		(T_DBMT_CON_DBINFO *) increase_capacity (con_info.con_dbinfo,
							 sizeof
							 (T_DBMT_CON_DBINFO),
							 con_info.
							 num_con_dbinfo,
							 con_info.
							 num_con_dbinfo + 1);
	      if (con_info.con_dbinfo == NULL)
		{
		  strcpy (_dbmt_error, "Malloc fail");
		  goto con_write_dbinfo_err;
		}
	      con_info.num_con_dbinfo++;
	      con_info.con_dbinfo[con_info.num_con_dbinfo - 1] = tmp_dbinfo;

	      /* parse the next dbinfo */
	      prev = next;
	    }
	  /* db not found, create it */
	  if (creat_flag == 1)
	    {
	      con_info.con_dbinfo =
		(T_DBMT_CON_DBINFO *) increase_capacity (con_info.con_dbinfo,
							 sizeof
							 (T_DBMT_CON_DBINFO),
							 con_info.
							 num_con_dbinfo,
							 con_info.
							 num_con_dbinfo + 1);
	      if (con_info.con_dbinfo == NULL)
		{
		  strcpy (_dbmt_error, "Malloc fail");
		  goto con_write_dbinfo_err;
		}
	      con_info.num_con_dbinfo++;
	      con_info.con_dbinfo[con_info.num_con_dbinfo - 1] = *dbinfo;
	    }

	  /* update conlist file content */
	  fprintf (outfile, "%s %s %s %s %s %s ", con_info.cli_ip,
		   con_info.cli_port, date, time, con_info.cli_ver,
		   con_info.user_name);
	  for (i = 0; i < con_info.num_con_dbinfo; i++)
	    {
	      fprintf (outfile, "%s:%s,%s", con_info.con_dbinfo[i].dbname,
		       con_info.con_dbinfo[i].uid,
		       con_info.con_dbinfo[i].passwd);
	      if (i != (con_info.num_con_dbinfo - 1))
		{
		  fprintf (outfile, ";");
		}
	    }
	  fprintf (outfile, "\n");

	  retval = 0;
	}
      else
	{
	  fprintf (outfile, "%s\n", strbuf);
	}
      FREE_MEM (strbuf);
      buf_len = 0;
    }

con_write_dbinfo_err:
  fclose (infile);
  fclose (outfile);

  if (strbuf != NULL)
    {
      FREE_MEM (strbuf);
    }
  if (con_info.con_dbinfo != NULL)
    {
      FREE_MEM (con_info.con_dbinfo);
    }
  move_file (tmpfile, conn_list_file);

  uRemoveLockFile (lfd);

  return retval;
}

void
dbmt_con_set_dbinfo (T_DBMT_CON_DBINFO * dbinfo, const char *dbname,
		     const char *uid, const char *passwd)
{
  strcpy_limit (dbinfo->dbname, dbname, sizeof (dbinfo->dbname));
  strcpy_limit (dbinfo->uid, uid, sizeof (dbinfo->uid));
  strcpy_limit (dbinfo->passwd, passwd, sizeof (dbinfo->passwd));
}
