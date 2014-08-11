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
#include "shard_shm.h"
#include "shard_metadata.h"
#include "util_func.h"

#include "ini_parser.h"

#define DEFAULT_ADMIN_LOG_FILE		"log/broker/cubrid_broker.log"
#define DEFAULT_SESSION_TIMEOUT		"5min"
#define DEFAULT_MAX_QUERY_TIMEOUT       "0"
#define DEFAULT_MYSQL_READ_TIMEOUT      "0"
#define DEFAULT_JOB_QUEUE_SIZE		500
#define DEFAULT_APPL_SERVER		"CAS"
#define DEFAULT_EMPTY_STRING		"\0"
#define DEFAULT_FILE_UPLOAD_DELIMITER   "^^"
#define DEFAULT_SQL_LOG_MODE		"ALL"
#define DEFAULT_KEEP_CONNECTION         "AUTO"
#define DEFAULT_JDBC_CACHE_LIFE_TIME    1000
#define DEFAULT_PROXY_MAX_PREPARED_STMT_COUNT 10000
#define DEFAULT_CAS_MAX_PREPARED_STMT_COUNT 2000
#define DEFAULT_MONITOR_HANG_INTERVAL   60
#define DEFAULT_HANG_TIMEOUT            60
#define DEFAULT_RECONNECT_TIME          "600s"

#define DEFAULT_SHARD_PROXY_LOG_MODE		"ERROR"
#define DEFAULT_SHARD_KEY_MODULAR	        256
#define DEFAULT_SHARD_PROXY_TIMEOUT 		"30s"
#define DEFAULT_SHARD_PROXY_CONN_WAIT_TIMEOUT   "8h"

#define PORT_NUM_LIMIT                  (USHRT_MAX)	/* 65535 */

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
  PARAM_BAD_VALUE = 2, PARAM_BAD_RANGE = 3,
  SECTION_NAME_TOO_LONG = 4
};

static void conf_file_has_been_loaded (const char *conf_path);
static int check_port_number (T_BROKER_INFO * br_info, int num_brs);
static int get_conf_value (const char *string, T_CONF_TABLE * conf_table);
static const char *get_conf_string (int value, T_CONF_TABLE * conf_table);

static T_CONF_TABLE tbl_appl_server[] = {
  {APPL_SERVER_CAS_TYPE_NAME, APPL_SERVER_CAS},
  {APPL_SERVER_CAS_ORACLE_TYPE_NAME, APPL_SERVER_CAS_ORACLE},
  {APPL_SERVER_CAS_MYSQL_TYPE_NAME, APPL_SERVER_CAS_MYSQL},
  {APPL_SERVER_CAS_MYSQL51_TYPE_NAME, APPL_SERVER_CAS_MYSQL51},
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
  {"AUTO", KEEP_CON_AUTO},
  {NULL, 0}
};

static T_CONF_TABLE tbl_access_mode[] = {
  {"RW", READ_WRITE_ACCESS_MODE},
  {"RO", READ_ONLY_ACCESS_MODE},
  {"SO", SLAVE_ONLY_ACCESS_MODE},
  {NULL, 0}
};

static T_CONF_TABLE tbl_connect_order[] = {
  {"SEQ", CONNECT_ORDER_SEQ},
  {"RANDOM", CONNECT_ORDER_RANDOM},
  {NULL, 0}
};

static T_CONF_TABLE tbl_proxy_log_mode[] = {
  {"ALL", PROXY_LOG_MODE_ALL},
  {"ON", PROXY_LOG_MODE_ALL},
  {"SHARD", PROXY_LOG_MODE_SHARD_DETAIL},
  {"SCHEDULE", PROXY_LOG_MODE_SCHEDULE_DETAIL},
  {"NOTICE", PROXY_LOG_MODE_NOTICE},
  {"TIMEOUT", PROXY_LOG_MODE_TIMEOUT},
  {"ERROR", PROXY_LOG_MODE_ERROR},
  {"NONE", PROXY_LOG_MODE_NONE},
  {"OFF", PROXY_LOG_MODE_NONE},
  {NULL, 0}
};

static const char SECTION_NAME[] = "broker";

static const char *tbl_conf_err_msg[] = {
  "",
  "Cannot find any section in conf file.",
  "Value type does not match parameter type.",
  "Value is out of range.",
  "Section name is too long. Section name must be less than 64."
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
void
dir_repath (char *path, size_t path_len)
{
  char tmp_str[BROKER_PATH_MAX];

  trim (path);

  if (IS_ABS_PATH (path))
    {
      return;
    }

  strncpy (tmp_str, path, BROKER_PATH_MAX);
  snprintf (path, path_len, "%s/%s", get_cubrid_home (), tmp_str);
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
#define PRINTERROR(...)	\
  do {\
    PRINT_AND_LOG_ERR_MSG(__VA_ARGS__); \
  } \
  while (0)

#endif /* !_UC_ADMIN_SO_ */
  int num_brs = 0;
  int num_proxy = 0;
  int i, j;
  int master_shm_id = 0, proxy_shm_id = 0;
  int error_flag;
#if defined (WINDOWS)
  char appl_server_port_assigned[MAX_BROKER_NUM];
#endif
  INI_TABLE *ini;
  int tmp_int;
  float tmp_float;
  int lineno = 0;
  int errcode = 0;
  const char *ini_string;
  char library_name[BROKER_PATH_MAX];
  char size_str[LINE_MAX];
  char time_str[LINE_MAX];

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
  if (!ini_findsec (ini, SECTION_NAME))
    {
      PRINTERROR ("cannot find [%s] section in conf file %s\n",
		  SECTION_NAME, conf_file);
      ini_parser_free (ini);
      return -1;
    }

  master_shm_id = ini_gethex (ini, SECTION_NAME, "MASTER_SHM_ID", 0, &lineno);
  if (admin_log_file != NULL)
    {
      ini_string = ini_getstr (ini, SECTION_NAME, "ADMIN_LOG_FILE",
			       DEFAULT_ADMIN_LOG_FILE, &lineno);
      MAKE_FILEPATH (admin_log_file, ini_string, BROKER_PATH_MAX);
    }

  if (acl_flag != NULL)
    {
      tmp_int =
	conf_get_value_table_on_off (ini_getstr
				     (ini, SECTION_NAME, "ACCESS_CONTROL",
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
      ini_string = ini_getstr (ini, SECTION_NAME, "ACCESS_CONTROL_FILE", "",
			       &lineno);
      MAKE_FILEPATH (acl_file, ini_string, BROKER_PATH_MAX);
    }

  for (i = 0; i < ini->nsec; i++)
    {
      char *sec_name;

      sec_name = ini_getsecname (ini, i, &lineno);
      if (sec_name == NULL || strcasecmp (sec_name, SECTION_NAME) == 0
	  || sec_name[0] != '%')
	{
	  continue;
	}

      /* sec_name : %broker_name */
      if ((strlen (sec_name)) > BROKER_NAME_LEN)
	{
	  errcode = SECTION_NAME_TOO_LONG;
	  goto conf_error;
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
      if (br_info[num_brs].appl_server_min_num > APPL_SERVER_NUM_LIMIT)
	{
	  errcode = PARAM_BAD_VALUE;
	  goto conf_error;
	}


      br_info[num_brs].appl_server_max_num =
	ini_getuint (ini, sec_name, "MAX_NUM_APPL_SERVER",
		     DEFAULT_AS_MAX_NUM, &lineno);
      if (br_info[num_brs].appl_server_max_num > APPL_SERVER_NUM_LIMIT)
	{
	  errcode = PARAM_BAD_VALUE;
	  goto conf_error;
	}

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

      strncpy (size_str,
	       ini_getstr (ini, sec_name, "APPL_SERVER_MAX_SIZE",
			   DEFAULT_SERVER_MAX_SIZE, &lineno),
	       sizeof (size_str));
      br_info[num_brs].appl_server_max_size =
	(int) ut_size_string_to_kbyte (size_str, "M");
      if (br_info[num_brs].appl_server_max_size < 0)
	{
	  errcode = PARAM_BAD_VALUE;
	  goto conf_error;
	}

      strncpy (size_str,
	       ini_getstr (ini, sec_name, "APPL_SERVER_MAX_SIZE_HARD_LIMIT",
			   DEFAULT_SERVER_HARD_LIMIT, &lineno),
	       sizeof (size_str));
      br_info[num_brs].appl_server_hard_limit =
	(int) ut_size_string_to_kbyte (size_str, "M");
      if (br_info[num_brs].appl_server_hard_limit <= 0)
	{
	  errcode = PARAM_BAD_VALUE;
	  goto conf_error;
	}

      strncpy (time_str,
	       ini_getstr (ini, sec_name, "SESSION_TIMEOUT",
			   DEFAULT_SESSION_TIMEOUT, &lineno),
	       sizeof (time_str));
      br_info[num_brs].session_timeout =
	(int) ut_time_string_to_sec (time_str, "sec");
      if (br_info[num_brs].session_timeout < 0)
	{
	  errcode = PARAM_BAD_VALUE;
	  goto conf_error;
	}

      ini_string = ini_getstr (ini, sec_name, "LOG_DIR",
			       DEFAULT_LOG_DIR, &lineno);
      MAKE_FILEPATH (br_info[num_brs].log_dir, ini_string, CONF_LOG_FILE_LEN);
      ini_string = ini_getstr (ini, sec_name, "SLOW_LOG_DIR",
			       DEFAULT_SLOW_LOG_DIR, &lineno);
      MAKE_FILEPATH (br_info[num_brs].slow_log_dir, ini_string,
		     CONF_LOG_FILE_LEN);
      ini_string =
	ini_getstr (ini, sec_name, "ERROR_LOG_DIR", DEFAULT_ERR_DIR, &lineno);
      MAKE_FILEPATH (br_info[num_brs].err_log_dir, ini_string,
		     CONF_LOG_FILE_LEN);

      ini_string = ini_getstr (ini, sec_name, "ACCESS_LOG_DIR",
			       DEFAULT_ACCESS_LOG_DIR, &lineno);
      MAKE_FILEPATH (br_info[num_brs].access_log_dir, ini_string,
		     CONF_LOG_FILE_LEN);
      ini_string = ini_getstr (ini, sec_name, "DATABASES_CONNECTION_FILE",
			       DEFAULT_EMPTY_STRING, &lineno);
      MAKE_FILEPATH (br_info[num_brs].db_connection_file, ini_string,
		     BROKER_INFO_PATH_MAX);

      strcpy (br_info[num_brs].access_log_file,
	      br_info[num_brs].access_log_dir);
      strcpy (br_info[num_brs].error_log_file, CUBRID_BASE_DIR);

      br_info[num_brs].max_prepared_stmt_count =
	ini_getint (ini, sec_name, "MAX_PREPARED_STMT_COUNT",
		    DEFAULT_CAS_MAX_PREPARED_STMT_COUNT, &lineno);
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

      strncpy (size_str,
	       ini_getstr (ini, sec_name, "SQL_LOG_MAX_SIZE",
			   DEFAULT_SQL_LOG_MAX_SIZE, &lineno),
	       sizeof (size_str));
      br_info[num_brs].sql_log_max_size =
	(int) ut_size_string_to_kbyte (size_str, "K");
      if (br_info[num_brs].sql_log_max_size < 0)
	{
	  errcode = PARAM_BAD_VALUE;
	  goto conf_error;
	}
      else if (br_info[num_brs].sql_log_max_size > MAX_SQL_LOG_MAX_SIZE)
	{
	  errcode = PARAM_BAD_RANGE;
	  goto conf_error;
	}

      strncpy (time_str,
	       ini_getstr (ini, sec_name, "LONG_QUERY_TIME",
			   DEFAULT_LONG_QUERY_TIME, &lineno),
	       sizeof (time_str));
      tmp_float = (float) ut_time_string_to_sec (time_str, "sec");
      if (tmp_float < 0 || tmp_float > LONG_QUERY_TIME_LIMIT)
	{
	  errcode = PARAM_BAD_VALUE;
	  goto conf_error;
	}
      /* change float to msec */
      br_info[num_brs].long_query_time = (int) (tmp_float * 1000.0);

      strncpy (time_str,
	       ini_getstr (ini, sec_name, "LONG_TRANSACTION_TIME",
			   DEFAULT_LONG_TRANSACTION_TIME, &lineno),
	       sizeof (time_str));
      tmp_float = (float) ut_time_string_to_sec (time_str, "sec");
      if (tmp_float < 0 || tmp_float > LONG_TRANSACTION_TIME_LIMIT)
	{
	  errcode = PARAM_BAD_VALUE;
	  goto conf_error;
	}
      /* change float to msec */
      br_info[num_brs].long_transaction_time = (int) (tmp_float * 1000.0);

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

      strncpy (time_str,
	       ini_getstr (ini, sec_name, "TIME_TO_KILL",
			   DEFAULT_TIME_TO_KILL, &lineno), sizeof (time_str));
      br_info[num_brs].time_to_kill =
	(int) ut_time_string_to_sec (time_str, "sec");
      if (br_info[num_brs].time_to_kill < 0)
	{
	  errcode = PARAM_BAD_VALUE;
	  goto conf_error;
	}

      br_info[num_brs].access_log =
	conf_get_value_table_on_off (ini_getstr (ini, sec_name, "ACCESS_LOG",
						 "OFF", &lineno));
      if (br_info[num_brs].access_log < 0)
	{
	  errcode = PARAM_BAD_VALUE;
	  goto conf_error;
	}

      strncpy (size_str,
	       ini_getstr (ini, sec_name, "ACCESS_LOG_MAX_SIZE",
			   DEFAULT_ACCESS_LOG_MAX_SIZE, &lineno),
	       sizeof (size_str));
      br_info[num_brs].access_log_max_size =
	(int) ut_size_string_to_kbyte (size_str, "K");

      if (br_info[num_brs].access_log_max_size < 0)
	{
	  errcode = PARAM_BAD_VALUE;
	  goto conf_error;
	}
      else if (br_info[num_brs].access_log_max_size > MAX_ACCESS_LOG_MAX_SIZE)
	{
	  errcode = PARAM_BAD_RANGE;
	  goto conf_error;
	}

      ini_string = ini_getstr (ini, sec_name, "ACCESS_LIST",
			       DEFAULT_EMPTY_STRING, &lineno);
      MAKE_FILEPATH (br_info[num_brs].acl_file, ini_string,
		     CONF_LOG_FILE_LEN);

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

      br_info[num_brs].replica_only_flag =
	conf_get_value_table_on_off (ini_getstr
				     (ini, sec_name, "REPLICA_ONLY", "OFF",
				      &lineno));
      if (br_info[num_brs].replica_only_flag < 0)
	{
	  errcode = PARAM_BAD_VALUE;
	  goto conf_error;
	}

      strcpy (br_info[num_brs].preferred_hosts,
	      ini_getstr (ini, sec_name, "PREFERRED_HOSTS",
			  DEFAULT_EMPTY_STRING, &lineno));

      br_info[num_brs].connect_order =
	conf_get_value_connect_order (ini_getstr (ini, sec_name,
						  "CONNECT_ORDER",
						  "SEQ", &lineno));
      if (br_info[num_brs].connect_order < 0)
	{
	  errcode = PARAM_BAD_VALUE;
	  goto conf_error;
	}

      br_info[num_brs].max_num_delayed_hosts_lookup =
	ini_getint (ini, sec_name, "MAX_NUM_DELAYED_HOSTS_LOOKUP",
		    DEFAULT_MAX_NUM_DELAYED_HOSTS_LOOKUP, &lineno);
      if (br_info[num_brs].max_num_delayed_hosts_lookup <
	  DEFAULT_MAX_NUM_DELAYED_HOSTS_LOOKUP)
	{
	  errcode = PARAM_BAD_VALUE;
	  goto conf_error;
	}

      strncpy (time_str,
	       ini_getstr (ini, sec_name, "RECONNECT_TIME",
			   DEFAULT_RECONNECT_TIME, &lineno),
	       sizeof (time_str));
      br_info[num_brs].cas_rctime =
	(int) ut_time_string_to_sec (time_str, "sec");
      if (br_info[num_brs].cas_rctime < 0)
	{
	  errcode = PARAM_BAD_VALUE;
	  goto conf_error;
	}

      strncpy (time_str,
	       ini_getstr (ini, sec_name, "MAX_QUERY_TIMEOUT",
			   DEFAULT_MAX_QUERY_TIMEOUT, &lineno),
	       sizeof (time_str));
      br_info[num_brs].query_timeout =
	(int) ut_time_string_to_sec (time_str, "sec");
      if (br_info[num_brs].query_timeout < 0)
	{
	  errcode = PARAM_BAD_VALUE;
	  goto conf_error;
	}
      else if (br_info[num_brs].query_timeout > MAX_QUERY_TIMEOUT_LIMIT)
	{
	  errcode = PARAM_BAD_RANGE;
	  goto conf_error;
	}

      strncpy (time_str,
	       ini_getstr (ini, sec_name, "MYSQL_READ_TIMEOUT",
			   DEFAULT_MYSQL_READ_TIMEOUT, &lineno),
	       sizeof (time_str));
      br_info[num_brs].mysql_read_timeout =
	(int) ut_time_string_to_sec (time_str, "sec");
      if (br_info[num_brs].mysql_read_timeout < 0)
	{
	  errcode = PARAM_BAD_VALUE;
	  goto conf_error;
	}
      else if (br_info[num_brs].mysql_read_timeout > MAX_QUERY_TIMEOUT_LIMIT)
	{
	  errcode = PARAM_BAD_RANGE;
	  goto conf_error;
	}

      /* parameters related to checking hanging cas */
      br_info[num_brs].reject_client_flag = false;
      tmp_int = conf_get_value_table_on_off (ini_getstr (ini, sec_name,
							 "ENABLE_MONITOR_HANG",
							 "OFF", &lineno));
      if (tmp_int < 0)
	{
	  errcode = PARAM_BAD_VALUE;
	  goto conf_error;
	}
      else
	{
	  br_info[num_brs].monitor_hang_flag = tmp_int;
	  br_info[num_brs].monitor_hang_interval =
	    DEFAULT_MONITOR_HANG_INTERVAL;
	  br_info[num_brs].hang_timeout = DEFAULT_HANG_TIMEOUT;
	}

      br_info[num_brs].trigger_action_flag =
	conf_get_value_table_on_off (ini_getstr
				     (ini, sec_name, "TRIGGER_ACTION", "ON",
				      &lineno));
      if (br_info[num_brs].trigger_action_flag < 0)
	{
	  errcode = PARAM_BAD_VALUE;
	  goto conf_error;
	}

      br_info[num_brs].shard_flag =
	conf_get_value_table_on_off (ini_getstr (ini, sec_name,
						 "SHARD", "OFF", &lineno));
      if (br_info[num_brs].shard_flag < 0)
	{
	  errcode = PARAM_BAD_VALUE;
	  goto conf_error;
	}

      /* SHARD PHASE0 */
      br_info[num_brs].proxy_shm_id =
	ini_gethex (ini, sec_name, "SHARD_PROXY_SHM_ID", 0, &lineno);

      strncpy (br_info[num_brs].shard_db_name,
	       ini_getstr (ini, sec_name, "SHARD_DB_NAME",
			   DEFAULT_EMPTY_STRING, &lineno),
	       sizeof (br_info[num_brs].shard_db_name));

      strncpy (br_info[num_brs].shard_db_user,
	       ini_getstr (ini, sec_name, "SHARD_DB_USER",
			   DEFAULT_EMPTY_STRING, &lineno),
	       sizeof (br_info[num_brs].shard_db_user));

      strncpy (br_info[num_brs].shard_db_password,
	       ini_getstr (ini, sec_name, "SHARD_DB_PASSWORD",
			   DEFAULT_EMPTY_STRING, &lineno),
	       sizeof (br_info[num_brs].shard_db_password));

      br_info[num_brs].num_proxy =
	ini_getuint (ini, sec_name, "SHARD_NUM_PROXY",
		     DEFAULT_SHARD_NUM_PROXY, &lineno);

      if (br_info[num_brs].num_proxy > MAX_PROXY_NUM)
	{
	  errcode = PARAM_BAD_VALUE;
	  goto conf_error;
	}

      strcpy (br_info[num_brs].proxy_log_dir,
	      ini_getstr (ini, sec_name, "SHARD_PROXY_LOG_DIR",
			  DEFAULT_SHARD_PROXY_LOG_DIR, &lineno));

      br_info[num_brs].proxy_log_mode =
	conf_get_value_proxy_log_mode (ini_getstr
				       (ini, sec_name, "SHARD_PROXY_LOG",
					DEFAULT_SHARD_PROXY_LOG_MODE,
					&lineno));
      if (br_info[num_brs].proxy_log_mode < 0)
	{
	  errcode = PARAM_BAD_VALUE;
	  goto conf_error;
	}

      br_info[num_brs].max_client =
	ini_getuint (ini, sec_name, "SHARD_MAX_CLIENTS",
		     DEFAULT_SHARD_MAX_CLIENTS, &lineno);
      if (br_info[num_brs].max_client >
	  CLIENT_INFO_SIZE_LIMIT * br_info[num_brs].num_proxy)
	{
	  errcode = PARAM_BAD_VALUE;
	  goto conf_error;
	}

      strncpy (br_info[num_brs].shard_connection_file,
	       ini_getstr (ini, sec_name, "SHARD_CONNECTION_FILE",
			   "shard_connection.txt", &lineno),
	       sizeof (br_info[num_brs].shard_connection_file));
      if (br_info[num_brs].shard_connection_file[0] == '\0')
	{
	  errcode = PARAM_BAD_VALUE;
	  goto conf_error;
	}

      strncpy (br_info[num_brs].shard_key_file,
	       ini_getstr (ini, sec_name, "SHARD_KEY_FILE",
			   "shard_key.txt", &lineno),
	       sizeof (br_info[num_brs].shard_key_file));
      if (br_info[num_brs].shard_key_file[0] == '\0')
	{
	  errcode = PARAM_BAD_VALUE;
	  goto conf_error;
	}

      br_info[num_brs].shard_key_modular =
	ini_getuint (ini, sec_name, "SHARD_KEY_MODULAR",
		     DEFAULT_SHARD_KEY_MODULAR, &lineno);
      if (br_info[num_brs].shard_key_modular < 0)
	{
	  errcode = PARAM_BAD_VALUE;
	  goto conf_error;
	}
      strcpy (library_name,
	      ini_getstr (ini, sec_name, "SHARD_KEY_LIBRARY_NAME",
			  DEFAULT_EMPTY_STRING, &lineno));

      if (library_name[0] != 0 && !IS_ABS_PATH (library_name))
	{
	  envvar_libdir_file (br_info[num_brs].shard_key_library_name,
			      BROKER_PATH_MAX, library_name);
	}
      else
	{
	  snprintf (br_info[num_brs].shard_key_library_name, BROKER_PATH_MAX,
		    "%s", library_name);
	}

      strcpy (br_info[num_brs].shard_key_function_name,
	      ini_getstr (ini, sec_name, "SHARD_KEY_FUNCTION_NAME",
			  DEFAULT_EMPTY_STRING, &lineno));

      strncpy (size_str,
	       ini_getstr (ini, sec_name, "SHARD_PROXY_LOG_MAX_SIZE",
			   DEFAULT_SHARD_PROXY_LOG_MAX_SIZE, &lineno),
	       sizeof (size_str));
      br_info[num_brs].proxy_log_max_size =
	(int) ut_size_string_to_kbyte (size_str, "K");
      if (br_info[num_brs].proxy_log_max_size < 0)
	{
	  errcode = PARAM_BAD_VALUE;
	  goto conf_error;
	}
      else if (br_info[num_brs].proxy_log_max_size > MAX_PROXY_LOG_MAX_SIZE)
	{
	  errcode = PARAM_BAD_RANGE;
	  goto conf_error;
	}

      br_info[num_brs].proxy_max_prepared_stmt_count =
	ini_getint (ini, sec_name, "SHARD_MAX_PREPARED_STMT_COUNT",
		    DEFAULT_PROXY_MAX_PREPARED_STMT_COUNT, &lineno);
      if (br_info[num_brs].proxy_max_prepared_stmt_count < 1)
	{
	  errcode = PARAM_BAD_VALUE;
	  goto conf_error;
	}

      br_info[num_brs].ignore_shard_hint =
	conf_get_value_table_on_off (ini_getstr (ini, sec_name,
						 "SHARD_IGNORE_HINT", "OFF",
						 &lineno));
      if (br_info[num_brs].ignore_shard_hint < 0)
	{
	  errcode = PARAM_BAD_VALUE;
	  goto conf_error;
	}

      strncpy (time_str,
	       ini_getstr (ini, sec_name, "SHARD_PROXY_TIMEOUT",
			   DEFAULT_SHARD_PROXY_TIMEOUT, &lineno),
	       sizeof (time_str));
      br_info[num_brs].proxy_timeout =
	(int) ut_time_string_to_sec (time_str, "sec");
      if (br_info[num_brs].proxy_timeout < 0)
	{
	  errcode = PARAM_BAD_VALUE;
	  goto conf_error;
	}
      else if (br_info[num_brs].proxy_timeout > MAX_PROXY_TIMEOUT_LIMIT)
	{
	  errcode = PARAM_BAD_RANGE;
	  goto conf_error;
	}

      strncpy (time_str,
	       ini_getstr (ini, sec_name, "SHARD_PROXY_CONN_WAIT_TIMEOUT",
			   DEFAULT_SHARD_PROXY_CONN_WAIT_TIMEOUT, &lineno),
	       sizeof (time_str));
      br_info[num_brs].proxy_conn_wait_timeout =
	(int) ut_time_string_to_sec (time_str, "sec");
      if (br_info[num_brs].proxy_conn_wait_timeout < 0)
	{
	  errcode = PARAM_BAD_VALUE;
	  goto conf_error;
	}
      else if (br_info[num_brs].proxy_conn_wait_timeout >
	       MAX_PROXY_TIMEOUT_LIMIT)
	{
	  errcode = PARAM_BAD_RANGE;
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
	  if (br_info[i].port <= 0 || br_info[i].port > PORT_NUM_LIMIT)
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

	  if (br_info[i].shard_flag == ON)
	    {
	      if (br_info[i].proxy_shm_id <= 0)
		{
		  PRINTERROR ("config error, %s, SHARD_PROXY_SHM_ID\n",
			      br_info[i].name);
		  error_flag = TRUE;
		}
	      if (br_info[i].shard_db_name[0] == '\0')
		{
		  PRINTERROR ("config error, %s, SHARD_DB_NAME\n",
			      br_info[i].name);
		  error_flag = TRUE;
		}
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
	  dir_repath (br_info[i].access_log_file, CONF_LOG_FILE_LEN);
	  dir_repath (br_info[i].error_log_file, CONF_LOG_FILE_LEN);
	  if (br_info[i].source_env[0] != '\0')
	    {
	      dir_repath (br_info[i].source_env, CONF_LOG_FILE_LEN);
	    }
	  if (br_info[i].acl_file[0] != '\0')
	    {
	      dir_repath (br_info[i].acl_file, CONF_LOG_FILE_LEN);
	    }
	  if (br_info[i].proxy_log_dir[0] != '\0')
	    {
	      dir_repath (br_info[i].proxy_log_dir, CONF_LOG_FILE_LEN);
	    }
	}
      if (admin_log_file != NULL)
	{
	  dir_repath (admin_log_file, CONF_LOG_FILE_LEN);
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
#if !defined (_UC_ADMIN_SO_)
  PRINTERROR ("Line %d in config file %s : %s\n", lineno, conf_file,
	      tbl_conf_err_msg[errcode]);
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
  bool is_conf_found = false;
  char default_conf_file_path[BROKER_PATH_MAX], file_name[BROKER_PATH_MAX],
    file_being_dealt_with[BROKER_PATH_MAX];
  struct stat stat_buf;

#if !defined (_UC_ADMIN_SO_)
  admin_flag = 1;
  admin_err_msg = NULL;
#endif /* !_UC_ADMIN_SO_ */

  memset (br_info, 0, sizeof (T_BROKER_INFO) * MAX_BROKER_NUM);

  get_cubrid_file (FID_CUBRID_BROKER_CONF, default_conf_file_path,
		   BROKER_PATH_MAX);

  basename_r (default_conf_file_path, file_name, BROKER_PATH_MAX);

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
      is_conf_found = true;
      err = broker_config_read_internal (file_being_dealt_with, br_info,
					 num_broker, br_shm_id,
					 admin_log_file, admin_flag,
					 acl_flag, acl_file, admin_err_msg);
    }

  if (!is_conf_found)
    {
      err = -1;
      PRINT_AND_LOG_ERR_MSG ("Error: can't find %s\n",
			     (conf_file ==
			      NULL) ? default_conf_file_path : conf_file);
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
	       br_info[i].appl_server_max_num);
#if defined (WINDOWS)
      fprintf (fp, "APPL_SERVER_PORT\t=%d\n", br_info[i].appl_server_port);
#endif
      fprintf (fp, "APPL_SERVER_SHM_ID\t=%x\n",
	       br_info[i].appl_server_shm_id);
      fprintf (fp, "APPL_SERVER_MAX_SIZE\t=%d\n",
	       br_info[i].appl_server_max_size / ONE_K);
      fprintf (fp, "SESSION_TIMEOUT\t\t=%d\n", br_info[i].session_timeout);
      fprintf (fp, "LOG_DIR\t\t\t=%s\n", br_info[i].log_dir);
      fprintf (fp, "SLOW_LOG_DIR\t\t=%s\n", br_info[i].slow_log_dir);
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
      fprintf (fp, "ACCESS_LOG_MAX_SIZE\t=%dK\n",
	       (br_info[i].access_log_max_size));
      fprintf (fp, "ACCESS_LOG_DIR\t\t=%s\n", br_info[i].access_log_dir);
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
      tmp_str = get_conf_string (br_info[i].connect_order, tbl_connect_order);
      if (tmp_str)
	{
	  fprintf (fp, "CONNECT_ORDER\t\t=%s\n", tmp_str);
	}

      fprintf (fp, "MAX_NUM_DELAYED_HOSTS_LOOKUP\t=%d\n",
	       br_info[i].max_num_delayed_hosts_lookup);

      fprintf (fp, "RECONNECT_TIME\t\t=%d\n", br_info[i].cas_rctime);

      tmp_str = get_conf_string (br_info[i].replica_only_flag, tbl_on_off);
      if (tmp_str)
	{
	  fprintf (fp, "REPLICA_ONLY\t\t=%s\n", tmp_str);
	}

      tmp_str = get_conf_string (br_info[i].trigger_action_flag, tbl_on_off);
      if (tmp_str)
	{
	  fprintf (fp, "TRIGGER_ACTION\t\t=%s\n", tmp_str);
	}

      fprintf (fp, "MAX_QUERY_TIMEOUT\t=%d\n", br_info[i].query_timeout);

      if (br_info[i].appl_server == APPL_SERVER_CAS_MYSQL
	  || br_info[i].appl_server == APPL_SERVER_CAS_MYSQL51)
	{
	  fprintf (fp, "MYSQL_READ_TIMEOUT\t=%d\n",
		   br_info[i].mysql_read_timeout);
	}

      tmp_str = get_conf_string (br_info[i].monitor_hang_flag, tbl_on_off);
      if (tmp_str)
	{
	  fprintf (fp, "ENABLE_MONITOR_HANG\t=%s\n", tmp_str);
	}
      fprintf (fp, "REJECTED_CLIENTS_COUNT\t=%d\n",
	       br_info[i].reject_client_count);

      tmp_str = get_conf_string (br_info[i].stripped_column_name, tbl_on_off);
      if (tmp_str)
	{
	  fprintf (fp, "STRIPPED_COLUMN_NAME\t=%s\n", tmp_str);
	}

      tmp_str = get_conf_string (br_info[i].cache_user_info, tbl_on_off);
      if (tmp_str)
	{
	  fprintf (fp, "CACHE_USER_INFO\t\t=%s\n", tmp_str);
	}

#if !defined (WINDOWS)
      fprintf (fp, "SQL_LOG2\t\t=%d\n", br_info[i].sql_log2);
#endif
      fprintf (fp, "BROKER_PORT\t\t\t=%d\n", br_info[i].port);
      fprintf (fp, "APPL_SERVER_NUM\t\t=%d\n", br_info[i].appl_server_num);
      fprintf (fp, "APPL_SERVER_MAX_SIZE_HARD_LIMIT\t=%d\n",
	       br_info[i].appl_server_hard_limit / ONE_K);
      fprintf (fp, "MAX_PREPARED_STMT_COUNT\t=%d\n",
	       br_info[i].max_prepared_stmt_count);
      fprintf (fp, "PREFERRED_HOSTS\t\t=%s\n", br_info[i].preferred_hosts);

      tmp_str = get_conf_string (br_info[i].jdbc_cache, tbl_on_off);
      if (tmp_str)
	{
	  fprintf (fp, "JDBC_CACHE\t\t=%s\n", tmp_str);
	}

      tmp_str = get_conf_string (br_info[i].jdbc_cache_only_hint, tbl_on_off);
      if (tmp_str)
	{
	  fprintf (fp, "JDBC_CACHE_ONLY_HINT\t=%s\n", tmp_str);
	}

      fprintf (fp, "JDBC_CACHE_LIFE_TIME\t=%d\n",
	       br_info[i].jdbc_cache_life_time);
      tmp_str =
	get_conf_string (br_info[i].cci_default_autocommit, tbl_on_off);
      if (tmp_str)
	{
	  fprintf (fp, "CCI_DEFAULT_AUTOCOMMIT\t=%s\n", tmp_str);
	}

      fprintf (fp, "MONITOR_HANG_INTERVAL\t=%d\n",
	       br_info[i].monitor_hang_interval);
      fprintf (fp, "HANG_TIMEOUT\t\t=%d\n", br_info[i].hang_timeout);
      get_conf_string (br_info[i].reject_client_flag, tbl_on_off);
      if (tmp_str)
	{
	  fprintf (fp, "REJECT_CLIENT_FLAG\t=%s\n", tmp_str);
	}

      if (br_info[i].shard_flag == OFF)
	{
	  fprintf (fp, "\n");
	  continue;
	}

      fprintf (fp, "SHARD_DB_NAME\t\t=%s\n", br_info[i].shard_db_name);
      fprintf (fp, "SHARD_DB_USER\t\t=%s\n", br_info[i].shard_db_user);

      fprintf (fp, "SHARD_NUM_PROXY\t\t=%d\n", br_info[i].num_proxy);
      fprintf (fp, "SHARD_PROXY_LOG_DIR\t\t=%s\n", br_info[i].proxy_log_dir);
      tmp_str =
	get_conf_string (br_info[i].proxy_log_mode, tbl_proxy_log_mode);
      if (tmp_str)
	{
	  fprintf (fp, "SHARD_PROXY_LOG\t\t\t=%s\n", tmp_str);
	}
      fprintf (fp, "SHARD_MAX_CLIENTS\t\t=%d\n", br_info[i].max_client);

      fprintf (fp, "SHARD_CONNECTION_FILE\t\t=%s\n",
	       br_info[i].shard_connection_file);
      fprintf (fp, "SHARD_KEY_FILE\t\t=%s\n", br_info[i].shard_key_file);

      fprintf (fp, "SHARD_KEY_MODULAR\t\t=%d\n",
	       br_info[i].shard_key_modular);
      fprintf (fp, "SHARD_KEY_LIBRARY_NAME\t\t=%s\n",
	       br_info[i].shard_key_library_name);
      fprintf (fp, "SHARD_KEY_FUNCTION_NAME\t\t=%s\n",
	       br_info[i].shard_key_function_name);
      fprintf (fp, "SHARD_PROXY_LOG_MAX_SIZE\t\t=%d\n",
	       br_info[i].proxy_log_max_size);
      fprintf (fp, "SHARD_MAX_PREPARED_STMT_COUNT\t\t=%d\n",
	       br_info[i].proxy_max_prepared_stmt_count);
      tmp_str = get_conf_string (br_info[i].ignore_shard_hint, tbl_on_off);
      if (tmp_str)
	{
	  fprintf (fp, "SHARD_IGNORE_HINT\t\t=%s\n", tmp_str);
	}
      fprintf (fp, "SHARD_PROXY_TIMEOUT\t\t=%d\n", br_info[i].proxy_timeout);
      fprintf (fp, "SHARD_PROXY_SHM_ID\t\t=%d\n", br_info[i].proxy_shm_id);

      shard_metadata_dump (fp, br_info[i].proxy_shm_id);
      shard_shm_dump_appl_server (fp, br_info[i].proxy_shm_id);

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

/*
 * conf_get_value_connect_order - get value from connect_order table
 *   return: -1 if fail
 *   value(in):
 */
int
conf_get_value_connect_order (const char *value)
{
  return (get_conf_value (value, tbl_connect_order));
}

/*
 * conf_get_value_proxy_log_mode - get value from proxy_log_mode table
 *   return: -1 if fail
 *   value(in):
 */
int
conf_get_value_proxy_log_mode (const char *value)
{
  return (get_conf_value (value, tbl_proxy_log_mode));
}
