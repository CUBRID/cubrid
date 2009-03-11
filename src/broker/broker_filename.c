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
 * broker_filename.c - 
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif

#include "broker_filename.h"

#ifndef PATH_MAX
#define PATH_MAX 256
#endif

static char cubrid_dir[PATH_MAX] = "";

#define NUM_CUBRID_FILE		17
static T_CUBRID_FILE_INFO cubrid_file[NUM_CUBRID_FILE] = {
  {FID_CUBRID_BROKER_CONF, CUBRID_CONF_DIR, "cubrid_broker.conf"},
  {FID_UV_ERR_MSG, CUBRID_CONF_DIR, "uv_er.msg"},
  {FID_V3_OUTFILE_DIR, CUBRID_TMP_DIR, ""},
  {FID_CAS_TMPGLO_DIR, CUBRID_TMP_DIR, ""},
  {FID_CAS_TMP_DIR, CUBRID_TMP_DIR, ""},
  {FID_SOCK_DIR, CUBRID_SOCK_DIR, ""},
  {FID_AS_PID_DIR, CUBRID_ASPID_DIR, ""},
  {FID_ADMIND_PID, CUBRID_ASPID_DIR, "casd.pid"},
  {FID_SQL_LOG_DIR, CUBRID_LOG_DIR, ""},
  {FID_ADMIND_LOG, CUBRID_LOG_DIR, "cas_admind.log"},
  {FID_MONITORD_LOG, CUBRID_LOG_DIR, "cas_monitord.log"},
  {FID_ER_HTML, CUBRID_CONF_DIR, "uw_er.html"},
  {FID_CUBRID_ERR_DIR, CUBRID_ERR_DIR, ""}
};

void
set_cubrid_home ()
{
  char *p;

  p = getenv_cubrid_broker ();
  if (p)
    {
      strcpy (cubrid_dir, p);
      return;
    }
  getcwd (cubrid_dir, sizeof (cubrid_dir));
  strcat (cubrid_dir, "/..");
}

char *
get_cubrid_home ()
{
  if (cubrid_dir[0] == '\0')
    {
      set_cubrid_home ();
    }
  return cubrid_dir;
}


void
set_cubrid_file (T_CUBRID_FILE_ID fid, char *value)
{
  int i;

  for (i = 0; i < NUM_CUBRID_FILE; i++)
    {
      if (fid == cubrid_file[i].fid)
	{
	  strcpy (cubrid_file[i].dir_name, value);
	  break;
	}
    }
}

char *
get_cubrid_file (T_CUBRID_FILE_ID fid, char *buf)
{
  int i;

  buf[0] = '\0';

  if (cubrid_dir[0] == '\0')
    {
      set_cubrid_home ();
    }

  for (i = 0; i < NUM_CUBRID_FILE; i++)
    {
      if (fid == cubrid_file[i].fid)
	{
	  sprintf (buf, "%s/%s/%s", cubrid_dir,
		   cubrid_file[i].dir_name, cubrid_file[i].file_name);
	  break;
	}
    }
  return buf;
}

char *
getenv_cubrid_broker ()
{
  char *p;

  p = getenv ("CUBRID");

  return p;
}
