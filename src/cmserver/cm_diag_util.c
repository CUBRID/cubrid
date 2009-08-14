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
 * diag_util.c -
 */

#ident "$Id$"

#include <stdio.h>
#include <stdlib.h>
#if !defined(WINDOWS)
#include <strings.h>
#endif
#include <string.h>
#include "cm_porting.h"
#include "perf_monitor.h"
#include "cm_config.h"
#include "cm_diag_util.h"

static char *utrim (char *str);

int
uReadDiagSystemConfig (DIAG_SYS_CONFIG * config, char *err_buf)
{
  FILE *conf_file;
  char cbuf[1024], file_path[1024];
  char ent_name[128], ent_val[128];

  if (config == NULL)
    return -1;

  /* Initialize config data */
  config->Executediag = 0;
  config->DiagSM_ID_server = 0;

  conf_get_dbmt_file (FID_DBMT_CONF, file_path);

  conf_file = fopen (file_path, "r");

  if (conf_file == NULL)
    {
      if (err_buf)
	sprintf (err_buf, "File(%s) open error.", file_path);

      return -1;
    }

  while (fgets (cbuf, 1024, conf_file))
    {
      utrim (cbuf);
      if (cbuf[0] == '\0' || cbuf[0] == '#')
	continue;

      if (sscanf (cbuf, "%127s %127s", ent_name, ent_val) < 2)
	continue;

      if (strcasecmp (ent_name, "Execute_diag") == 0)
	{
	  if (strcasecmp (ent_val, "ON") == 0)
	    config->Executediag = 1;
	  else
	    config->Executediag = 0;
	}
      else if (strcasecmp (ent_name, "server_long_query_time") == 0)
	{
	  config->server_long_query_time = atoi (ent_val);
	}
    }

  fclose (conf_file);
  return 1;
}

static char *
utrim (char *str)
{
  char *p;
  char *s;

  if (str == NULL)
    return (str);

  for (s = str;
       *s != '\0' && (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r');
       s++)
    ;
  if (*s == '\0')
    {
      *str = '\0';
      return (str);
    }

  /* *s must be a non-white char */
  for (p = s; *p != '\0'; p++)
    ;
  for (p--; *p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'; p--)
    ;
  *++p = '\0';

  if (s != str)
    memcpy (str, s, strlen (s) + 1);

  return (str);
}
