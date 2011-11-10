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
 * broker_config.c - broker configuration utilities
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#if defined (WINDOWS)
#include <io.h>
#include <direct.h>
#else
#include <unistd.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <assert.h>

#include "porting.h"
#include "cas_common.h"
#include "broker_config.h"
#include "broker_shm.h"
#include "broker_filename.h"
#include "broker_util.h"
#include "cas_sql_log2.h"

#include "ini_parser.h"
#include "environment_variable.h"

#define DEFAULT_ADMIN_LOG_FILE		"log/broker/cubrid_broker.log"
#define DEFAULT_SESSION_TIMEOUT		300	/* seconds */
#define DEFAULT_MAX_QUERY_TIMEOUT       0	/* seconds */
#define DEFAULT_JOB_QUEUE_SIZE		500
#define DEFAULT_APPL_SERVER		"CAS"
#define DEFAULT_EMPTY_STRING		"\0"
#define DEFAULT_FILE_UPLOAD_DELIMITER   "^^"
#define DEFAULT_SQL_LOG_MODE		"ALL"
#define DEFAULT_KEEP_CONNECTION         "AUTO"
#define DEFAULT_JDBC_CACHE_LIFE_TIME    1000
#define DEFAULT_MAX_PREPARED_STMT_COUNT 2000

#define	TRUE	1
#define	FALSE	0

typedef struct t_conf_table T_CONF_TABLE;
struct t_conf_table
{
  const char *conf_str;
  int conf_value;
};

enum
{ PARAM_NO_ERROR = 0, PARAM_INVAL_SEC = 1,
  PARAM_BAD_VALUE = 2, PARAM_BAD_RANGE = 3
};

static void conf_file_has_been_loaded (const char *conf_path);
static int check_port_number (T_BROKER_INFO * br_info, int num_brs);
static void dir_repath (char *path);
static int get_conf_value (const char *string, T_CONF_TABLE * conf_table);
static const char *get_conf_string (int value, T_CONF_TABLE * conf_table);


static T_CONF_TABLE tbl_appl_server[] = {
  {APPL_SERVER_CAS_TYPE_NAME, APPL_SERVER_CAS},
  {APPL_SERVER_CAS_ORACLE_TYPE_NAME, APPL_SERVER_CAS_ORACLE},
  {APPL_SERVER_CAS_MYSQL_TYPE_NAME, APPL_SERVER_CAS_MYSQL},
  {NULL, 0}
};

static T_CONF_TABLE tbl_on_off[] = {
  {"ON", ON},
  {"OFF", OFF},
  {NULL, 0}
};

static T_CONF_TABLE tbl_sql_log_mode[] = {
  {"ALL", SQL_LOG_MODE_ALL},
  {"ON", SQL_LOG_MODE_ALL},
  {"NOTICE", SQL_LOG_MODE_NOTICE},
  {"TIMEOUT", SQL_LOG_MODE_TIMEOUT},
  {"ERROR", SQL_LOG_MODE_ERROR},
  {"NONE", SQL_LOG_MODE_NONE},
  {"OFF", SQL_LOG_MODE_NONE},
  {NULL, 0}
};

static T_CONF_TABLE tbl_keep_connection[] = {
  {"ON", KEEP_CON_ON},
  {"OFF", KEEP_CON_OFF},
  {"AUTO", KEEP_CON_AUTO},
  {NULL, 0}
};

static T_CONF_TABLE tbl_access_mode[] = {
  {"RW", READ_WRITE_ACCESS_MODE},
  {"RO", READ_ONLY_ACCESS_MODE},
  {"SO", SLAVE_ONLY_ACCESS_MODE},
  {"PHRO", PH_READ_ONLY_ACCESS_MODE},
  {NULL, 0}
};

static const char BROKER_SECTION[] = "broker";

static const char *tbl_conf_err_msg[] = {
  "",
  "Cannot find any section in conf file.",
  "Value type does not match parameter type.",
  "Value is out of range."
};

/* conf files that have been loaded */
#define MAX_NUM_OF_CONF_FILE_LOADED     5
static char *conf_file_loaded[MAX_NUM_OF_CONF_FILE_LOADED];

/*
 * conf_file_has_been_loaded - record the file path that has been loaded
 *   return: none
 *   conf_path(in): path of the conf file to be recorded
 */
static void
conf_file_has_been_loaded (const char *conf_path)
{
  int i;
  assert (conf_path != NULL);

  for (i = 0; i < MAX_NUM_OF_CONF_FILE_LOADED; i++)
    {
      if (conf_file_loaded[i] == NULL)
	{
	  conf_file_loaded[i] = strdup (conf_path);
	  return;
	}
    }
}

/*
 * check_port_number - Check broker's port number
 *   return: 0 or -1 if duplicated
 *   br_info(in):
 *   num_brs(in):
 */
static int
check_port_number (T_BROKER_INFO * br_info, int num_brs)
{
  int i, j;
  int error_flag = FALSE;

  for (i = 0; i < num_brs; i++)
    {
      for (j = i + 1; j < num_brs; j++)
	{
	  if (br_info[i].port == br_info[j].port)
	    {
#if !defined (WINDOWS)
	      printf ("duplicated port number %d\n", br_info[i].port);
#endif
	      error_flag = TRUE;
	    }
	}
    }

  if (error_flag == TRUE)
    {
      return -1;
    }
  return 0;
}

/*
 * dir_repath - Fix path to absolute path
 *   return: void
 *   path(in/out):
 */
static void
dir_repath (char *path)
{
  char tmp_str[PATH_MAX];

  trim (path);

  if (IS_ABS_PATH (path))
    {
      return;
    }

  strcpy (tmp_str, path);
  snprintf (path, PATH_MAX, "%s/%s", get_cubrid_home (), tmp_str);
}

/*
 * get_conf_value_table - get value from table
 *   return: table value or -1 if fail
 *   value(in):
 *   conf_table(in):
 */
static int
get_conf_value (const char *string, T_CONF_TABLE * conf_table)
{
  int i;

  for (i = 0; conf_table[i].conf_str != NULL; i++)
    {
      if (strcasecmp (string, conf_table[i].conf_str) == 0)
	return conf_table[i].conf_value;
    }
  return -1;
}

static const char *
get_conf_string (int value, T_CONF_TABLE * conf_table)
{
  int i;

  for (i = 0; conf_table[i].conf_str != NULL; i++)
    {
      if (conf_table[i].conf_value == value)
	{
	  return conf_table[i].conf_str;
	}
    }
  return NULL;
}

/*
 * broker_config_read_internal - read and parse broker configurations
 *   return: 0 or -1 if fail
 *   br_info(in/out):
 *   num_broker(out):
 *   br_shm_id(out):
 *   admin_log_file(out):
 *   admin_flag(in):
 *   admin_err_msg(in):
 */
static int
broker_config_read_internal (const char *conf_file,
			     T_BROKER_INFO * br_info, int *num_broker,
			     int *br_shm_id, char *admin_log_file,
			     char admin_flag, bool * acl_flag,
			     char *acl_file, char *admin_err_msg)
{
#if defined (_UC_ADMIN_SO_)
#define PRINTERROR(...)	sprintf(admin_err_msg, __VA_ARGS__)
#else /* _UC_ADMIN_SO_ */
#if !defined (WINDOWS)
#define PRINTERROR(...)	printf(__VA_ARGS__)
#else /* WINDOWS */
#define PRINTERROR(...)
#endif /* !WINDOWS */
#endif /* !_UC_ADMIN_SO_ */
  int num_brs = 0;
  int i, j;
  int master_shm_id = 0, error_flag;
#if defined (WINDOWS)
  char appl_server_port_assigned[MAX_BROKER_NUM];
#endif
  INI_TABLE *ini;
  int tmp_int;
  float tmp_float;
  int lineno = 0;
  int errcode = 0;

  ini = ini_parser_load (conf_file);
  if (ini == NULL)
    {
      PRINTERROR ("cannot open conf file %s\n", conf_file);
      return -1;
    }

#if defined (WINDOWS)
  memset (appl_server_port_assigned, 0, sizeof (appl_server_port_assigned));
#endif

  if (ini->nsec < 1)
    {
      errcode = PARAM_INVAL_SEC;
      goto conf_error;
    }

  /* get [broker] section vars */
  if (!ini_findsec (ini, "broker"))
    {
      PRINTERROR ("cannot find [%s] section in conf file %s\n",
		  BROKER_SECTION, conf_file);
      ini_parser_free (ini);
      return -1;
    }

  master_shm_id =
    ini_gethex (ini, BROKER_SECTION, "MASTER_SHM_ID", 0, &lineno);
  if (admin_log_file != NULL)
    {
      strcpy (admin_log_file,
	      ini_getstr (ini, BROKER_SECTION, "ADMIN_LOG_FILE",
			  DEFAULT_ADMIN_LOG_FILE, &lineno));
    }

  if (acl_flag != NULL)
    {
      tmp_int =
	conf_get_value_table_on_off (ini_getstr
				     (ini, BROKER_SECTION, "ACCESS_CONTROL",
				      "OFF", &lineno));
      if (tmp_int < 0)
	{
	  errcode = PARAM_BAD_RANGE;
	  goto conf_error;
	}
      *acl_flag = tmp_int;
    }

  if (acl_file != NULL)
    {
      strcpy (acl_file,
	      ini_getstr (ini, BROKER_SECTION, "ACCESS_CONTROL_FILE", "",
			  &lineno));
    }

  for (i = 0; i < ini->nsec; i++)
    {
      char *sec_name;

      sec_name = ini_getsecname (ini, i);
      if (sec_name == NULL || strcasecmp (sec_name, BROKER_SECTION) == 0
	  || sec_name[0] != '%')
	{
	  continue;
	}

      if (num_brs >= MAX_BROKER_NUM)
	{
	  errcode = PARAM_BAD_RANGE;
	  goto conf_error;
	}

      strcpy (br_info[num_brs].name, sec_name + 1);

      br_info[num_brs].cci_default_autocommit =
	conf_get_value_table_on_off (ini_getstr
				     (ini, sec_name, "CCI_DEFAULT_AUTOCOMMIT",
				      "ON", &lineno));
      if (br_info[num_brs].cci_default_autocommit < 0)
	{
	  errcode = PARAM_BAD_VALUE;
	  goto conf_error;
	}

      br_info[num_brs].port =
	ini_getint (ini, sec_name, "BROKER_PORT", 0, &lineno);

      br_info[num_brs].service_flag =
	conf_get_value_table_on_off (ini_getstr (ini, sec_name, "SERVICE",
						 "ON", &lineno));
      if (br_info[num_brs].service_flag < 0)
	{
	  errcode = PARAM_BAD_VALUE;
	  goto conf_error;
	}

      br_info[num_brs].appl_server =
	get_conf_value (ini_getstr (ini, sec_name, "APPL_SERVER",
				    DEFAULT_APPL_SERVER, &lineno),
			tbl_appl_server);
      if (br_info[num_brs].appl_server < 0)
	{
	  errcode = PARAM_BAD_VALUE;
	  goto conf_error;
	}

      br_info[num_brs].appl_server_min_num =
	ini_getuint (ini, sec_name, "MIN_NUM_APPL_SERVER",
		     DEFAULT_AS_MIN_NUM, &lineno);
      br_info[num_brs].appl_server_num = br_info[num_brs].appl_server_min_num;

      br_info[num_brs].appl_server_max_num =
	ini_getuint (ini, sec_name, "MAX_NUM_APPL_SERVER",
		     DEFAULT_AS_MAX_NUM, &lineno);

#if defined (WINDOWS)
      tmp_int = ini_getint (ini, sec_name, "APPL_SERVER_PORT", -1, &lineno);
      if (tmp_int < 0)
	{
	  br_info[num_brs].appl_server_port = 0;	/* default value */
	}
      else
	{
	  br_info[num_brs].appl_server_port = tmp_int;
	  appl_server_port_assigned[num_brs] = TRUE;
	}
#endif

      br_info[num_brs].appl_server_shm_id =
	ini_gethex (ini, sec_name, "APPL_SERVER_SHM_ID", 0, &lineno);

      br_info[num_brs].appl_server_max_size =
	ini_getint (ini, sec_name, "APPL_SERVER_MAX_SIZE",
		    DEFAULT_SERVER_MAX_SIZE, &lineno);
      br_info[num_brs].appl_server_max_size *= ONE_K;	/* K bytes */

      br_info[num_brs].appl_server_hard_limit =
	ini_getint (ini, sec_name, "APPL_SERVER_MAX_SIZE_HARD_LIMIT",
		    DEFAULT_SERVER_HARD_LIMIT, &lineno);
      br_info[num_brs].appl_server_hard_limit *= ONE_K;	/* K bytes */

      br_info[num_brs].session_timeout =
	ini_getint (ini, sec_name, "SESSION_TIMEOUT",
		    DEFAULT_SESSION_TIMEOUT, &lineno);

      strcpy (br_info[num_brs].log_dir,
	      ini_getstr (ini, sec_name, "LOG_DIR", DEFAULT_LOG_DIR,
			  &lineno));
      strcpy (br_info[num_brs].slow_log_dir,
	      ini_getstr (ini, sec_name, "SLOW_LOG_DIR", DEFAULT_SLOW_LOG_DIR,
			  &lineno));
      strcpy (br_info[num_brs].err_log_dir,
	      ini_getstr (ini, sec_name, "ERROR_LOG_DIR", DEFAULT_ERR_DIR,
			  &lineno));
      strcpy (br_info[num_brs].access_log_file, CUBRID_BASE_DIR);
      strcpy (br_info[num_brs].error_log_file, CUBRID_BASE_DIR);

      br_info[num_brs].max_prepared_stmt_count =
	ini_getint (ini, sec_name, "MAX_PREPARED_STMT_COUNT",
		    DEFAULT_MAX_PREPARED_STMT_COUNT, &lineno);
      if (br_info[num_brs].max_prepared_stmt_count < 1)
	{
	  errcode = PARAM_BAD_VALUE;
	  goto conf_error;
	}

      br_info[num_brs].log_backup =
	conf_get_value_table_on_off (ini_getstr (ini, sec_name, "LOG_BACKUP",
						 "OFF", &lineno));
      if (br_info[num_brs].log_backup < 0)
	{
	  errcode = PARAM_BAD_VALUE;
	  goto conf_error;
	}

      strcpy (br_info[num_brs].source_env,
	      ini_getstr (ini, sec_name, "SOURCE_ENV", DEFAULT_EMPTY_STRING,
			  &lineno));

      br_info[num_brs].sql_log_mode =
	conf_get_value_sql_log_mode (ini_getstr
				     (ini, sec_name, "SQL_LOG",
				      DEFAULT_SQL_LOG_MODE, &lineno));
      if (br_info[num_brs].sql_log_mode < 0)
	{
	  errcode = PARAM_BAD_VALUE;
	  goto conf_error;
	}

      br_info[num_brs].slow_log_mode =
	conf_get_value_table_on_off (ini_getstr (ini, sec_name, "SLOW_LOG",
						 "ON", &lineno));
      if (br_info[num_brs].slow_log_mode < 0)
	{
	  errcode = PARAM_BAD_VALUE;
	  goto conf_error;
	}

#if !defined (WINDOWS)
      br_info[num_brs].sql_log2 =
	ini_getuint_max (ini, sec_name, "SQL_LOG2", SQL_LOG2_NONE,
			 SQL_LOG2_MAX, &lineno);
#endif

      br_info[num_brs].sql_log_max_size =
	ini_getuint_max (ini, sec_name, "SQL_LOG_MAX_SIZE",
			 DEFAULT_SQL_LOG_MAX_SIZE, MAX_SQL_LOG_MAX_SIZE,
			 &lineno);

      tmp_float = ini_getfloat (ini, sec_name, "LONG_QUERY_TIME",
				(float) DEFAULT_LONG_QUERY_TIME, &lineno);
      if (tmp_float <= 0)
	{
	  errcode = PARAM_BAD_VALUE;
	  goto conf_error;
	}
      else
	{
	  /* change float to msec */
	  br_info[num_brs].long_query_time = (int) (tmp_float * 1000.0);
	}

      tmp_float = ini_getfloat (ini, sec_name, "LONG_TRANSACTION_TIME",
				(float) DEFAULT_LONG_TRANSACTION_TIME,
				&lineno);
      if (tmp_float <= 0)
	{
	  errcode = PARAM_BAD_VALUE;
	  goto conf_error;
	}
      else
	{
	  /* change float to msec */
	  br_info[num_brs].long_transaction_time = (int) (tmp_float * 1000.0);
	}

      br_info[num_brs].auto_add_appl_server =
	conf_get_value_table_on_off (ini_getstr (ini, sec_name,
						 "AUTO_ADD_APPL_SERVER",
						 "ON", &lineno));
      if (br_info[num_brs].auto_add_appl_server < 0)
	{
	  errcode = PARAM_BAD_VALUE;
	  goto conf_error;
	}

      br_info[num_brs].job_queue_size =
	ini_getuint_max (ini, sec_name, "JOB_QUEUE_SIZE",
			 DEFAULT_JOB_QUEUE_SIZE, JOB_QUEUE_MAX_SIZE, &lineno);

      br_info[num_brs].time_to_kill =
	ini_getuint (ini, sec_name, "TIME_TO_KILL", DEFAULT_TIME_TO_KILL,
		     &lineno);

      br_info[num_brs].access_log =
	conf_get_value_table_on_off (ini_getstr (ini, sec_name, "ACCESS_LOG",
						 "ON", &lineno));
      if (br_info[num_brs].access_log < 0)
	{
	  errcode = PARAM_BAD_VALUE;
	  goto conf_error;
	}

      strcpy (br_info[num_brs].acl_file,
	      ini_getstr (ini, sec_name, "ACCESS_LIST",
			  DEFAULT_EMPTY_STRING, &lineno));

      br_info[num_brs].max_string_length =
	ini_getint (ini, sec_name, "MAX_STRING_LENGTH", -1, &lineno);

      br_info[num_brs].stripped_column_name =
	conf_get_value_table_on_off (ini_getstr (ini, sec_name,
						 "STRIPPED_COLUMN_NAME", "ON",
						 &lineno));
      if (br_info[num_brs].stripped_column_name < 0)
	{
	  errcode = PARAM_BAD_VALUE;
	  goto conf_error;
	}

      br_info[num_brs].keep_connection =
	conf_get_value_keep_con (ini_getstr (ini, sec_name, "KEEP_CONNECTION",
					     DEFAULT_KEEP_CONNECTION,
					     &lineno));
      if (br_info[num_brs].keep_connection < 0)
	{
	  errcode = PARAM_BAD_VALUE;
	  goto conf_error;
	}

      br_info[num_brs].cache_user_info =
	conf_get_value_table_on_off (ini_getstr (ini, sec_name,
						 "CACHE_USER_INFO", "OFF",
						 &lineno));
      if (br_info[num_brs].cache_user_info < 0)
	{
	  errcode = PARAM_BAD_VALUE;
	  goto conf_error;
	}

      br_info[num_brs].statement_pooling =
	conf_get_value_table_on_off (ini_getstr (ini, sec_name,
						 "STATEMENT_POOLING", "ON",
						 &lineno));
      if (br_info[num_brs].statement_pooling < 0)
	{
	  errcode = PARAM_BAD_VALUE;
	  goto conf_error;
	}

      br_info[num_brs].jdbc_cache =
	conf_get_value_table_on_off (ini_getstr (ini, sec_name, "JDBC_CACHE",
						 "OFF", &lineno));
      if (br_info[num_brs].jdbc_cache < 0)
	{
	  errcode = PARAM_BAD_VALUE;
	  goto conf_error;
	}

      br_info[num_brs].jdbc_cache_only_hint =
	conf_get_value_table_on_off (ini_getstr (ini, sec_name,
						 "JDBC_CACHE_ONLY_HINT",
						 "OFF", &lineno));
      if (br_info[num_brs].jdbc_cache_only_hint < 0)
	{
	  errcode = PARAM_BAD_VALUE;
	  goto conf_error;
	}

      br_info[num_brs].jdbc_cache_life_time =
	ini_getint (ini, sec_name, "JDBC_CACHE_LIFE_TIME",
		    DEFAULT_JDBC_CACHE_LIFE_TIME, &lineno);

      br_info[num_brs].cci_pconnect =
	conf_get_value_table_on_off (ini_getstr (ini, sec_name,
						 "CCI_PCONNECT", "OFF",
						 &lineno));
      if (br_info[num_brs].cci_pconnect < 0)
	{
	  errcode = PARAM_BAD_VALUE;
	  goto conf_error;
	}

      br_info[num_brs].select_auto_commit =
	conf_get_value_table_on_off (ini_getstr (ini, sec_name,
						 "SELECT_AUTO_COMMIT", "OFF",
						 &lineno));
      if (br_info[num_brs].select_auto_commit < 0)
	{
	  errcode = PARAM_BAD_VALUE;
	  goto conf_error;
	}

      tmp_int =
	conf_get_value_table_on_off (ini_getstr (ini, sec_name,
						 "READ_ONLY_BROKER", "OFF",
						 &lineno));
      if (tmp_int < 0)
	{
	  errcode = PARAM_BAD_VALUE;
	  goto conf_error;
	}
      else if (tmp_int == ON)
	{
	  br_info[num_brs].access_mode =
	    get_conf_value (ini_getstr (ini, sec_name, "ACCESS_MODE",
					"RO", &lineno), tbl_access_mode);
	}
      else
	{
	  br_info[num_brs].access_mode =
	    get_conf_value (ini_getstr (ini, sec_name, "ACCESS_MODE",
					"RW", &lineno), tbl_access_mode);
	}
      if (br_info[num_brs].access_mode < 0)
	{
	  errcode = PARAM_BAD_VALUE;
	  goto conf_error;
	}

      strcpy (br_info[num_brs].preferred_hosts,
	      ini_getstr (ini, sec_name, "PREFERRED_HOSTS",
			  DEFAULT_EMPTY_STRING, &lineno));
      if (br_info[num_brs].access_mode == PH_READ_ONLY_ACCESS_MODE
	  && br_info[num_brs].preferred_hosts[0] == '\0')
	{
	  errcode = PARAM_BAD_VALUE;
	  goto conf_error;
	}

      br_info[num_brs].query_timeout =
	ini_getint (ini, sec_name, "MAX_QUERY_TIMEOUT",
		    DEFAULT_MAX_QUERY_TIMEOUT, &lineno);
      if (br_info[num_brs].query_timeout < 0
	  || br_info[num_brs].query_timeout > MAX_QUERY_TIMEOUT_LIMIT)
	{
	  errcode = PARAM_BAD_VALUE;
	  goto conf_error;
	}

      num_brs++;
    }

  ini_parser_free (ini);
  ini = NULL;

  if (master_shm_id <= 0)
    {
      PRINTERROR ("config error, invalid MASTER_SHM_ID\n");
      error_flag = TRUE;
    }
  else
    {
      error_flag = FALSE;
    }

  if (admin_flag)
    {
      for (i = 0; i < num_brs; i++)
	{
	  if (master_shm_id == br_info[i].appl_server_shm_id)
	    {
	      PRINTERROR
		("config error, MASTER_SHM_ID = broker %s APPL_SERVER_SHM_ID\n",
		 br_info[i].name);
	      error_flag = TRUE;
	    }
	  for (j = i + 1; j < num_brs; j++)
	    {
	      if (strcmp (br_info[i].name, br_info[j].name) == 0)
		{
		  PRINTERROR ("duplicated broker name : %s\n",
			      br_info[i].name);
		  error_flag = TRUE;
		}
	      if (br_info[i].appl_server_shm_id ==
		  br_info[j].appl_server_shm_id)
		{
		  PRINTERROR
		    ("duplicated APPL_SERVER_SHM_ID (broker %s, %s)\n",
		     br_info[i].name, br_info[j].name);
		  error_flag = TRUE;
		}
	    }
	}			/* end for (i) */

      for (i = 0; i < num_brs; i++)
	{
	  if (br_info[i].port <= 0)
	    {
	      PRINTERROR ("config error, %s, BROKER_PORT\n", br_info[i].name);
	      error_flag = TRUE;
	    }
	  if (br_info[i].appl_server_min_num > br_info[i].appl_server_max_num)
	    {
	      br_info[i].appl_server_max_num = br_info[i].appl_server_min_num;
	    }
	  if (br_info[i].appl_server_shm_id <= 0)
	    {
	      PRINTERROR ("config error, %s, APPL_SHM_ID\n", br_info[i].name);
	      error_flag = TRUE;
#if defined (_UC_ADMIN_SO_)
	      break;
#endif
	    }
	}			/* end for (i) */
    }				/* end if (admin_flag) */
  if (error_flag == TRUE)
    {
      return -1;
    }

  if (admin_flag)
    {
      if (check_port_number (br_info, num_brs) < 0)
	{
	  return -1;
	}

      for (i = 0; i < num_brs; i++)
	{
	  dir_repath (br_info[i].access_log_file);
	  dir_repath (br_info[i].error_log_file);
	  if (br_info[i].source_env[0] != '\0')
	    {
	      dir_repath (br_info[i].source_env);
	    }
	  if (br_info[i].acl_file[0] != '\0')
	    {
	      dir_repath (br_info[i].acl_file);
	    }
	}
      if (admin_log_file != NULL)
	{
	  dir_repath (admin_log_file);
	}
    }

#if defined (WINDOWS)
  for (i = 0; i < num_brs; i++)
    {
      if (appl_server_port_assigned[i] == FALSE)
	{
	  br_info[i].appl_server_port = br_info[i].port + 1;
	}
    }
#endif

  *num_broker = num_brs;
  *br_shm_id = master_shm_id;

  conf_file_has_been_loaded (conf_file);

  return 0;

conf_error:
#if !defined (WINDOWS)
#if !defined (_UC_ADMIN_SO_)
  PRINTERROR ("Line %d in config file %s : %s\n", lineno, conf_file,
	      tbl_conf_err_msg[errcode]);
#endif
#endif
  if (ini)
    {
      ini_parser_free (ini);
    }

  return -1;
}

/*
 * broker_config_read - read and parse broker configurations
 *   return: 0 or -1 if fail
 *   br_info(in/out):
 *   num_broker(out):
 *   br_shm_id(out):
 *   admin_log_file(out):
 *   admin_flag(in):
 *   admin_err_msg(in):
 */
int
broker_config_read (const char *conf_file, T_BROKER_INFO * br_info,
		    int *num_broker, int *br_shm_id, char *admin_log_file,
		    char admin_flag, bool * acl_flag, char *acl_file,
		    char *admin_err_msg)
{
  int err = 0;
  char default_conf_file_path[PATH_MAX], file_name[PATH_MAX],
    file_being_dealt_with[PATH_MAX];
  struct stat stat_buf;

#if !defined (_UC_ADMIN_SO_)
  admin_flag = 1;
  admin_err_msg = NULL;
#endif /* !_UC_ADMIN_SO_ */

  memset (br_info, 0, sizeof (T_BROKER_INFO) * MAX_BROKER_NUM);

  get_cubrid_file (FID_CUBRID_BROKER_CONF, default_conf_file_path);
  basename_r (default_conf_file_path, file_name, PATH_MAX);

  if (conf_file == NULL)
    {
      /* use environment variable's value if exist */
      conf_file = envvar_get ("BROKER_CONF_FILE");
      if (conf_file != NULL && *conf_file == '\0')
	{
	  conf_file = NULL;
	}
    }

  if (conf_file != NULL)
    {
      strcpy (file_being_dealt_with, conf_file);
    }
  else
    {
      /* $CUBRID/conf/cubrid_broker.conf */
      strcpy (file_being_dealt_with, default_conf_file_path);
    }
  if (stat (file_being_dealt_with, &stat_buf) == 0)
    {
      err = broker_config_read_internal (file_being_dealt_with, br_info,
					 num_broker, br_shm_id,
					 admin_log_file, admin_flag,
					 acl_flag, acl_file, admin_err_msg);
    }

  if (conf_file == NULL)
    {
      /* $PWD/cubrid_broker.conf if exist */
#if defined (WINDOWS)
      sprintf (file_being_dealt_with, ".\\%s", file_name);
#else /* WINDOWS */
      snprintf (file_being_dealt_with, (PATH_MAX - 1), "./%s", file_name);
#endif /* WINDOWS */
      if (stat (file_being_dealt_with, &stat_buf) == 0)
	{
	  err = broker_config_read_internal (file_being_dealt_with, br_info,
					     num_broker, br_shm_id,
					     admin_log_file, admin_flag,
					     acl_flag, acl_file,
					     admin_err_msg);
	}
    }

  return err;
}

/*
 * broker_config_dump - print out current broker configurations
 *   return: none
 *   fp(in):
 */
void
broker_config_dump (FILE * fp, const T_BROKER_INFO * br_info,
		    int num_broker, int br_shm_id)
{
  int i;
  const char *tmp_str;

  if (br_info == NULL || num_broker <= 0 || num_broker > MAX_BROKER_NUM
      || br_shm_id <= 0)
    return;

  fprintf (fp, "#\n# cubrid_broker.conf\n#\n\n");
  fprintf (fp, "# broker parameters were loaded from the files\n");

  for (i = 0; i < MAX_NUM_OF_CONF_FILE_LOADED; i++)
    {
      if (conf_file_loaded[i] != NULL)
	{
	  fprintf (fp, "# %s\n", conf_file_loaded[i]);
	}
    }

  fprintf (fp, "\n# broker parameters\n");

  fprintf (fp, "[broker]\n");
  fprintf (fp, "MASTER_SHM_ID\t=%x\n\n", br_shm_id);

  for (i = 0; i < num_broker; i++)
    {
      fprintf (fp, "[%%%s]\n", br_info[i].name);
      tmp_str = get_conf_string (br_info[i].service_flag, tbl_on_off);
      if (tmp_str)
	{
	  fprintf (fp, "SERVICE\t\t\t=%s\n", tmp_str);
	}
      tmp_str = get_conf_string (br_info[i].appl_server, tbl_appl_server);
      if (tmp_str)
	{
	  fprintf (fp, "APPL_SERVER\t\t=%s\n", tmp_str);
	}
      fprintf (fp, "MIN_NUM_APPL_SERVER\t=%d\n",
	       br_info[i].appl_server_min_num);
      fprintf (fp, "MAX_NUM_APPL_SERVER\t=%d\n",
	       br_info[i].appl_server_min_num);
#if defined (WINDOWS)
      fprintf (fp, "APPL_SERVER_PORT\t=%d\n", br_info[i].appl_server_port);
#endif
      fprintf (fp, "APPL_SERVER_SHM_ID\t=%x\n",
	       br_info[i].appl_server_shm_id);
      fprintf (fp, "APPL_SERVER_MAX_SIZE\t=%d\n",
	       br_info[i].appl_server_max_size / ONE_K);
      fprintf (fp, "SESSION_TIMEOUT\t\t=%d\n", br_info[i].session_timeout);
      fprintf (fp, "LOG_DIR\t\t\t=%s\n", br_info[i].log_dir);
      fprintf (fp, "SLOW_LOG_DIR\t\t\t=%s\n", br_info[i].slow_log_dir);
      fprintf (fp, "ERROR_LOG_DIR\t\t=%s\n", br_info[i].err_log_dir);
      tmp_str = get_conf_string (br_info[i].log_backup, tbl_on_off);
      if (tmp_str)
	{
	  fprintf (fp, "LOG_BACKUP\t\t=%s\n", tmp_str);
	}
      fprintf (fp, "SOURCE_ENV\t\t=%s\n", br_info[i].source_env);
      tmp_str = get_conf_string (br_info[i].sql_log_mode, tbl_sql_log_mode);
      if (tmp_str)
	{
	  fprintf (fp, "SQL_LOG\t\t\t=%s\n", tmp_str);
	}
      tmp_str = get_conf_string (br_info[i].slow_log_mode, tbl_on_off);
      if (tmp_str)
	{
	  fprintf (fp, "SLOW_LOG\t\t=%s\n", tmp_str);
	}
      fprintf (fp, "SQL_LOG_MAX_SIZE\t=%d\n", br_info[i].sql_log_max_size);
      fprintf (fp, "LONG_QUERY_TIME\t\t=%.2f\n",
	       (br_info[i].long_query_time / 1000.0));
      fprintf (fp, "LONG_TRANSACTION_TIME\t=%.2f\n",
	       (br_info[i].long_transaction_time / 1000.0));
      tmp_str = get_conf_string (br_info[i].auto_add_appl_server, tbl_on_off);
      if (tmp_str)
	{
	  fprintf (fp, "AUTO_ADD_APPL_SERVER\t=%s\n", tmp_str);
	}
      fprintf (fp, "JOB_QUEUE_SIZE\t\t=%d\n", br_info[i].job_queue_size);
      fprintf (fp, "TIME_TO_KILL\t\t=%d\n", br_info[i].time_to_kill);
      tmp_str = get_conf_string (br_info[i].access_log, tbl_on_off);
      if (tmp_str)
	{
	  fprintf (fp, "ACCESS_LOG\t\t=%s\n", tmp_str);
	}
      fprintf (fp, "ACCESS_LIST\t\t=%s\n", br_info[i].acl_file);
      fprintf (fp, "MAX_STRING_LENGTH\t=%d\n", br_info[i].max_string_length);
      tmp_str =
	get_conf_string (br_info[i].keep_connection, tbl_keep_connection);
      if (tmp_str)
	{
	  fprintf (fp, "KEEP_CONNECTION\t\t=%s\n", tmp_str);
	}
      tmp_str = get_conf_string (br_info[i].statement_pooling, tbl_on_off);
      if (tmp_str)
	{
	  fprintf (fp, "STATEMENT_POOLING\t=%s\n", tmp_str);
	}
      tmp_str = get_conf_string (br_info[i].cci_pconnect, tbl_on_off);
      if (tmp_str)
	{
	  fprintf (fp, "CCI_PCONNECT\t\t=%s\n", tmp_str);
	}
      tmp_str = get_conf_string (br_info[i].access_mode, tbl_access_mode);
      if (tmp_str)
	{
	  fprintf (fp, "ACCESS_MODE\t\t=%s\n", tmp_str);
	}
      fprintf (fp, "MAX_QUERY_TIMEOUT\t\t=%d\n", br_info[i].query_timeout);
      fprintf (fp, "\n");
    }

  return;

}

/*
 * conf_get_value_table_on_off - get value from on/off table
 *   return: 0, 1 or -1 if fail
 *   value(in):
 */
int
conf_get_value_table_on_off (const char *value)
{
  return (get_conf_value (value, tbl_on_off));
}

/*
 * conf_get_value_sql_log_mode - get value from sql_log_mode table
 *   return: -1 if fail
 *   value(in):
 */
int
conf_get_value_sql_log_mode (const char *value)
{
  return (get_conf_value (value, tbl_sql_log_mode));
}

/*
 * conf_get_value_keep_con - get value from keep_connection table
 *   return: -1 if fail
 *   value(in):
 */
int
conf_get_value_keep_con (const char *value)
{
  return (get_conf_value (value, tbl_keep_connection));
}

/*
 * conf_get_value_access_mode - get value from access_mode table
 *   return: -1 if fail
 *   value(in):
 */
int
conf_get_value_access_mode (const char *value)
{
  return (get_conf_value (value, tbl_access_mode));
}
