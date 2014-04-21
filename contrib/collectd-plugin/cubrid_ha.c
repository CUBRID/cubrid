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
 * cubrid_ha.c - collectd plugin
 */

#include "cubrid_config.h"

#if defined (C_API_MODE)

#include "system.h"
#include "dbi.h"
#else
#include "cas_cci.h"
#endif

#include "collectd.h"
#include "common.h"
#include "plugin.h"

/* cubrid broker include files */
#include "cubrid_ha.h"
#define PLUGIN_NAME	"cubrid_ha"

static const char *config_keys[] = {
  "Host",
  "Port",
  "User",
  "Password",
  "Database_1",
  "LogPath_1_1",
  "LogPath_1_2",
  "Database_2",
  "LogPath_2_1",
  "LogPath_2_2",
  NULL
};

static int config_keys_num = 10;

static char *host = "localhost";
static int port = 33000;
static char *user = "dba";
static char *pass = "";
static char *db_1 = NULL;
static char *logpath_1_1 = NULL;
static char *logpath_1_2 = NULL;
static char *db_2 = NULL;
static char *logpath_2_1 = NULL;
static char *logpath_2_2 = NULL;
#if defined (C_API_MODE)
#else
static int con = 0;
static int isConnected = 0;
#endif

static int
config (const char *key, const char *value)
{
  if (strcasecmp (key, "host") == 0)
    {
      host = strdup (value);
      return 1;
    }
  else if (strcasecmp (key, "port") == 0)
    {
      char *str_port;
      str_port = strdup (value);
      port = atoi (str_port);
      free (str_port);
      return 1;
    }
  else if (strcasecmp (key, "user") == 0)
    {
      user = strdup (value);
      return 1;
    }
  else if (strcasecmp (key, "password") == 0)
    {
      pass = strdup (value);
      return 1;
    }
  else if (strcasecmp (key, "database_1") == 0)
    {
      db_1 = strdup (value);
      return 1;
    }
  else if (strcasecmp (key, "logpath_1_1") == 0)
    {
      logpath_1_1 = strdup (value);
      return 1;
    }
  else if (strcasecmp (key, "logpath_1_2") == 0)
    {
      logpath_1_2 = strdup (value);
      return 1;
    }
  else if (strcasecmp (key, "database_2") == 0)
    {
      db_2 = strdup (value);
      return 1;
    }
  else if (strcasecmp (key, "logpath_2_1") == 0)
    {
      logpath_2_1 = strdup (value);
      return 1;
    }
  else if (strcasecmp (key, "logpath_2_2") == 0)
    {
      logpath_2_2 = strdup (value);
      return 1;
    }
  return -1;
}

static int ha_read_info (char *dbname, char *logpath, int idx);
static int cubrid_ha_init (void);
static int cubrid_ha_read (void);
static void submit (char *type, char *type_instance, value_t * values,
		    int value_cnt);

static int
cubrid_ha_init (void)
{
  return 0;
}

static int
ha_read_info (char *dbname, char *logpath, int idx)
{
#if defined (C_API_MODE)
  DB_QUERY_ERROR error_stat;
  DB_QUERY_RESULT *result;
  int errcode;
  DB_VALUE val;
#else
  int req;
  T_CCI_ERROR error;
#endif
  int ind;

  long long value;
  int ivalue;
  int res;
  int query_len;
  char query[512];
  int i;
  char submit_name[256];
  value_t insert_count[1];
  value_t update_count[1];
  value_t delete_count[1];
  value_t schema_count[1];
  value_t commit_count[1];
  value_t fail_count[1];
  value_t delay_time[1];

  sprintf (submit_name, "%s_%s", dbname, logpath);

#if defined (C_API_MODE)
  if (db_login (user, pass) < 0)
    {
      goto error;
    }
  if (db_restart ("cubrid_ha", 0, dbname) < 0)
    {
      goto error;
    }
#else
  if (isConnected == 0)
    {
      if ((con = cci_connect (host, port, dbname, user, pass)) < 0)
	{
	  return -1;
	}
    }
#endif
  sprintf (query,
	   "SELECT insert_counter,update_counter,delete_counter,schema_counter,commit_counter,fail_counter,log_record_time,last_access_time,last_access_time - log_record_time,status from db_ha_apply_info where db_name='%s' and copied_log_path='%s'",
	   dbname, logpath);
  query_len = strlen (query);

#if defined (C_API_MODE)
  errcode = db_execute (query, &result, &error_stat);
  if (errcode < 0)
    {
      db_shutdown ();
      goto error;
    }
#else
  if ((req = cci_prepare (con, query, 0, &error)) < 0)
    {
      goto error;
    }
  isConnected = 1;
  if ((res = cci_execute (req, 0, 0, &error)) < 0)
    {
      goto error;
    }
#endif

#if defined (C_API_MODE)
  res = db_query_tuple_count (result);
  if (res == 0)
#else
  res = cci_cursor (req, 1, CCI_CURSOR_FIRST, &error);
  if (res == CCI_ER_NO_MORE_DATA)
#endif
    {
      // no data
      insert_count[0].counter = 0;
      update_count[0].counter = 0;
      delete_count[0].counter = 0;
      schema_count[0].counter = 0;
      commit_count[0].counter = 0;
      fail_count[0].counter = 0;
      delay_time[0].gauge = 0;
    }
  else
    {
#if defined (C_API_MODE)
      db_query_first_tuple (result);
#else
      res = cci_fetch (req, &error);
      if (res < 0)
	{
	  cci_close_req_handle (req);
	  goto error;
	}
#endif
      for (i = 0; i < 6; i++)
	{
#if defined (C_API_MODE)
	  res = db_query_get_tuple_value (result, i, &val);
	  value = db_get_bigint (&val);
	  ind = 1;
#else
	  res = cci_get_data (req, i + 1, CCI_A_TYPE_BIGINT, &value, &ind);
	  if (res < 0)
	    {
	      cci_close_req_handle (req);
	      goto error;
	    }
#endif
	  switch (i)
	    {
	    case 0:
	      if (ind < 0)
		{
		  insert_count[0].counter = 0;
		}
	      else
		{
		  insert_count[0].counter = value;
		}
	      break;
	    case 1:
	      if (ind < 0)
		{
		  update_count[0].counter = 0;
		}
	      else
		{
		  update_count[0].counter = value;
		}
	      break;
	    case 2:
	      if (ind < 0)
		{
		  delete_count[0].counter = 0;
		}
	      else
		{
		  delete_count[0].counter = value;
		}
	      break;
	    case 3:
	      if (ind < 0)
		{
		  schema_count[0].counter = 0;
		}
	      else
		{
		  schema_count[0].counter = value;
		}
	      break;
	    case 4:
	      if (ind < 0)
		commit_count[0].counter = 0;
	      else
		{
		  commit_count[0].counter = value;
		}
	      break;
	    case 5:
	      if (ind < 0)
		{
		  fail_count[0].counter = 0;
		}
	      else
		{
		  fail_count[0].counter = value;
		}
	      break;
	    default:
	      break;
	    }
	}
    }
#if defined (C_API_MODE)
  res = db_query_get_tuple_value (result, 8, &val);
  value = db_get_bigint (&val);
#else
  res = cci_get_data (req, 9, CCI_A_TYPE_BIGINT, &value, &ind);
  if (res < 0)
    {
      cci_close_req_handle (req);
      goto error;
    }
#endif

#if defined (C_API_MODE)
  res = db_query_get_tuple_value (result, 9, &val);
  ivalue = db_get_int (&val);
#else
  res = cci_get_data (req, 10, CCI_A_TYPE_INT, &ivalue, &ind);
  if (res < 0)
    {
      cci_close_req_handle (req);
      goto error;
    }
#endif

  if (ivalue == 1)
    {
      delay_time[0].gauge = value;
    }
  else
    {
      delay_time[0].gauge = 0;
    }

  submit ("cubrid_ha_delay_time", submit_name, delay_time, 1);
  submit ("cubrid_ha_insert", submit_name, insert_count, 1);
  submit ("cubrid_ha_update", submit_name, update_count, 1);
  submit ("cubrid_ha_delete", submit_name, delete_count, 1);
  submit ("cubrid_ha_schema", submit_name, schema_count, 1);
  submit ("cubrid_ha_commit", submit_name, commit_count, 1);
  submit ("cubrid_ha_fail", submit_name, fail_count, 1);

#if defined (C_API_MODE)
  db_shutdown ();
#else
  cci_close_req_handle (req);
#endif
  return 0;

error:
#if defined (C_API_MODE)
  return -1;
#else
  cci_disconnect (con, &error);
  isConnected = 0;
  return -1;
#endif
}

static int
cubrid_ha_read (void)
{
  if (db_1 != NULL && logpath_1_1 != NULL)
    {
      ha_read_info (db_1, logpath_1_1, 0);
    }
  if (db_1 != NULL && logpath_1_2 != NULL)
    {
      ha_read_info (db_1, logpath_1_2, 1);
    }
  if (db_2 != NULL && logpath_2_1 != NULL)
    {
      ha_read_info (db_2, logpath_2_1, 2);
    }
  if (db_2 != NULL && logpath_2_2 != NULL)
    {
      ha_read_info (db_2, logpath_2_2, 3);
    }
  return 0;
}

static void
submit (char *type, char *type_instance, value_t * values, int value_cnt)
{
  value_list_t vl = VALUE_LIST_INIT;

  vl.values = values;
  vl.values_len = value_cnt;
  vl.time = time (NULL);
  strcpy (vl.host, hostname_g);
  strcpy (vl.plugin, PLUGIN_NAME);
  strcpy (vl.plugin_instance, "");
  strcpy (vl.type_instance, type_instance);

#if defined (COLLECTD_43)
  plugin_dispatch_values (type, &vl);
#else
  strcpy (vl.type, type);
  plugin_dispatch_values (&vl);
#endif
}

void
module_register (void)
{
  printf ("cubrid_ha module register-0\n");
  plugin_register_config (PLUGIN_NAME, config, config_keys, config_keys_num);
  plugin_register_init (PLUGIN_NAME, cubrid_ha_init);
  plugin_register_read (PLUGIN_NAME, cubrid_ha_read);
}
