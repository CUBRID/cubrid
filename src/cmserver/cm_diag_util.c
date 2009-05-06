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
#include "cm_diag_client_request.h"
#include "cm_config.h"
#include "cm_diag_util.h"

static char *utrim (char *str);

int
init_diag_server_value (T_DIAG_MONITOR_DB_VALUE * server_value)
{
  if (!server_value)
    return 0;
  server_value->query_open_page = 0;
  server_value->query_opened_page = 0;
  server_value->query_slow_query = 0;
  server_value->query_full_scan = 0;
  server_value->conn_cli_request = 0;
  server_value->conn_aborted_clients = 0;
  server_value->conn_conn_req = 0;
  server_value->conn_conn_reject = 0;
  server_value->buffer_page_write = 0;
  server_value->buffer_page_read = 0;
  server_value->lock_deadlock = 0;
  server_value->lock_request = 0;

  return 1;
}

int
init_diag_cas_value (T_DIAG_MONITOR_CAS_VALUE * cas_value)
{
  if (!cas_value)
    return 0;

  cas_value->reqs_in_interval = 0;
  cas_value->active_sessions = 0;
  cas_value->transactions_in_interval = 0;

  return 1;
}

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

      if (sscanf (cbuf, "%s %s", ent_name, ent_val) < 2)
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

int
init_monitor_config (T_CLIENT_MONITOR_CONFIG * c_config)
{
  if (!c_config)
    return -1;

  if (init_cas_monitor_config (&(c_config->cas)) < 0)
    return -1;
  if (init_server_monitor_config (&(c_config->server)) < 0)
    return -1;

  return 0;
}

int
init_cas_monitor_config (MONITOR_CAS_CONFIG * c_cas)
{
  if (!c_cas)
    return -1;
  c_cas->head = '\0';
  memset (c_cas->body, '\0', sizeof (c_cas->body));

  return 0;
}

int
init_server_monitor_config (MONITOR_SERVER_CONFIG * c_server)
{
  if (!c_server)
    return -1;
  memset (c_server->head, '\0', sizeof (c_server->head));
  memset (c_server->body, '\0', sizeof (c_server->body));

  return 0;
}
