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

#if defined (WIN32)
#include <direct.h>
#else
#include <sys/types.h>
#include <sys/stat.h>
#endif

#include "cas_common.h"
#include "broker_config.h"
#include "broker_shm.h"
#include "broker_filename.h"
#include "broker_util.h"
#include "cas_sql_log2.h"

#include "ini_parser.h"

#define DEFAULT_ADMIN_LOG_FILE		"log/broker/cubrid_broker.log"
#define DEFAULT_SESSION_TIMEOUT		300	/* seconds */
#define DEFAULT_JOB_QUEUE_SIZE		500
#define DEFAULT_APPL_SERVER		"CAS"
#define DEFAULT_EMPTY_STRING		"\0"
#define DEFAULT_ERROR_LOG  		"BOTH"
#define DEFAULT_FILE_UPLOAD_DELIMITER   "^^"
#define DEFAULT_KEEP_CONNECTION         "AUTO"
#define DEFAULT_JDBC_CACHE_LIFE_TIME    1000

#define	TRUE	1
#define	FALSE	0

typedef struct t_conf_table T_CONF_TABLE;
struct t_conf_table
{
  char *conf_str;
  int conf_value;
};

static int check_port_number (T_BROKER_INFO * br_info, int num_brs);
static void dir_repath (char *path);
static int get_conf_value_table (char *value, T_CONF_TABLE * conf_table);


static T_CONF_TABLE tbl_appl_server[] = {
  {"VAS", APPL_SERVER_UTS_C},
  {"AMS", APPL_SERVER_AM},
  {"ULS", APPL_SERVER_UPLOAD},
  {"WAS", APPL_SERVER_UTS_W},
  {"CAS", APPL_SERVER_CAS},
  {NULL, 0}
};

static T_CONF_TABLE tbl_error_log[] = {
  {"NONE", CONF_ERR_LOG_NONE},
  {"LOGFILE", CONF_ERR_LOG_LOGFILE},
  {"BROWSER", CONF_ERR_LOG_BROWSER},
  {"BOTH", CONF_ERR_LOG_BOTH},
  {NULL, 0}
};
static T_CONF_TABLE tbl_on_off[] = {
  {"ON", ON},
  {"OFF", OFF},
  {NULL, 0}
};
static T_CONF_TABLE tbl_keep_connection[] = {
  {"ON", KEEP_CON_ON},
  {"OFF", KEEP_CON_OFF},
  {"AUTO", KEEP_CON_AUTO},
  {NULL, 0}
};

static const char BROKER_SECTION[] = "broker";

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
#if !defined (WIN32)
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
  char tmp_str[256];

  trim (path);

  if (path[0] == '/')
    return;

#if defined (WIN32)
  if (isalpha (path[0]) && path[1] == ':')
    {
      return;
    }
#endif

  strcpy (tmp_str, path);
  sprintf (path, "%s/%s", get_cubrid_home (), tmp_str);
}

/*
 * get_conf_value_table - get value from table
 *   return: table value or -1 if fail
 *   value(in):
 *   conf_table(in):
 */
static int
get_conf_value_table (char *value, T_CONF_TABLE * conf_table)
{
  int i;

  for (i = 0;; i++)
    {
      if (conf_table[i].conf_str == NULL)
	break;
      if (strcasecmp (value, conf_table[i].conf_str) == 0)
	return conf_table[i].conf_value;
    }
  return -1;
}

#if defined (_UC_ADMIN_SO_)
#define PRINTERROR(...)	sprintf(admin_err_msg, __VA_ARGS__)
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
broker_config_read (T_BROKER_INFO * br_info, int *num_broker, int *br_shm_id,
		    char *admin_log_file, char admin_flag,
		    char *admin_err_msg)
#else
#if !defined (WIN32)
#define PRINTERROR(...)	printf(__VA_ARGS__)
#else
#define PRINTERROR(...)
#endif
/*
 * broker_config_read - read and parse broker configurations
 *   return: 0 or -1 if fail
 *   br_info(in/out):
 *   num_broker(out):
 *   br_shm_id(out):
 *   admin_log_file(out):
 */
int
broker_config_read (T_BROKER_INFO * br_info, int *num_broker, int *br_shm_id,
		    char *admin_log_file)
#endif
{
  int num_brs = 0;
  int i, j;
  char conf_file[PATH_MAX];
  int master_shm_id = 0, error_flag;
#if !defined (_UC_ADMIN_SO_)
  char admin_flag = 1;
#endif
#if defined (WIN32)
  char appl_server_port_assigned[MAX_BROKER_NUM];
#endif

  INI_TABLE *ini;
  int tmp_int;

  get_cubrid_file (FID_CUBRID_BROKER_CONF, conf_file);
  ini = ini_parser_load (conf_file);
  if (ini == NULL)
    {
      PRINTERROR ("cannot open conf file %s\n", conf_file);
      return -1;
    }

  memset (br_info, 0, sizeof (T_BROKER_INFO) * MAX_BROKER_NUM);
#if defined (WIN32)
  memset (appl_server_port_assigned, 0, sizeof (appl_server_port_assigned));
#endif

  if (ini->nsec < 1)
    {
      goto conf_error;
    }

  /* get [broker] section vars */
  if (!ini_findsec (ini, "broker"))
    {
      PRINTERROR ("cannot find [%s] section in conf file %s\n",
		  BROKER_SECTION, conf_file);
      return -1;
    }

  master_shm_id = ini_gethex (ini, BROKER_SECTION, "MASTER_SHM_ID", 0);
  strcpy (admin_log_file,
	  ini_getstr (ini, BROKER_SECTION, "ADMIN_LOG_FILE",
		      DEFAULT_ADMIN_LOG_FILE));

  for (i = 0; i < ini->nsec; i++)
    {
      char *sec_name;

      sec_name = ini_getsecname (ini, i);
      if (strcasecmp (sec_name, BROKER_SECTION) == 0 || sec_name[0] != '%')
	{
	  continue;
	}

      if (num_brs >= MAX_BROKER_NUM)
	{
	  goto conf_error;
	}

      strcpy (br_info[num_brs].name, sec_name + 1);

      br_info[num_brs].port = ini_getint (ini, sec_name, "BROKER_PORT", 0);

      br_info[num_brs].service_flag =
	conf_get_value_table_on_off (ini_getstr
				     (ini, sec_name, "SERVICE", "ON"));
      if (br_info[num_brs].service_flag < 0)
	{
	  goto conf_error;
	}

      br_info[num_brs].appl_server =
	get_conf_value_table (ini_getstr
			      (ini, sec_name, "APPL_SERVER",
			       DEFAULT_APPL_SERVER), tbl_appl_server);
      if (br_info[num_brs].appl_server < 0)
	{
	  goto conf_error;
	}

      br_info[num_brs].appl_server_min_num =
	ini_getuint (ini, sec_name, "MIN_NUM_APPL_SERVER",
		     DEFAULT_AS_MIN_NUM);
      br_info[num_brs].appl_server_num = br_info[num_brs].appl_server_min_num;

      br_info[num_brs].appl_server_max_num =
	ini_getuint (ini, sec_name, "MAX_NUM_APPL_SERVER",
		     DEFAULT_AS_MAX_NUM);

#if defined (WIN32)
      tmp_int = ini_getint (ini, sec_name, "APPL_SERVER_PORT", -1);
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
	ini_gethex (ini, sec_name, "APPL_SERVER_SHM_ID", 0);

      br_info[num_brs].appl_server_max_size =
	ini_getuint (ini, sec_name, "APPL_SERVER_MAX_SIZE",
		     DEFAULT_SERVER_MAX_SIZE);
      br_info[num_brs].appl_server_max_size *= 1024;	/* K bytes */

      br_info[num_brs].session_timeout =
	ini_getint (ini, sec_name, "SESSION_TIMEOUT",
		    DEFAULT_SESSION_TIMEOUT);

      strcpy (br_info[num_brs].log_dir,
	      ini_getstr (ini, sec_name, "LOG_DIR", DEFAULT_LOG_DIR));
      strcpy (br_info[num_brs].err_log_dir,
	      ini_getstr (ini, sec_name, "ERROR_LOG_DIR", DEFAULT_ERR_DIR));
      strcpy (br_info[num_brs].access_log_file, CUBRID_BASE_DIR);
      strcpy (br_info[num_brs].error_log_file,
	      ini_getstr (ini, sec_name, "ERROR_LOG_DIR", DEFAULT_ERR_DIR));

      br_info[num_brs].log_backup =
	conf_get_value_table_on_off (ini_getstr
				     (ini, sec_name, "LOG_BACKUP", "OFF"));
      if (br_info[num_brs].log_backup < 0)
	{
	  goto conf_error;
	}

      br_info[num_brs].compress_size =
	ini_getuint (ini, sec_name, "COMPRESS_SIZE", DEFAULT_COMPRESS_SIZE);
      br_info[num_brs].compress_size *= 1024;	/* K bytes */

      strcpy (br_info[num_brs].source_env,
	      ini_getstr (ini, sec_name, "SOURCE_ENV", DEFAULT_EMPTY_STRING));

      tmp_int =
	conf_get_value_table_on_off (ini_getstr
				     (ini, sec_name, "SQL_LOG", "ON"));
      if (tmp_int < 0)
	{
	  goto conf_error;
	}
      else if (tmp_int)
	{
	  br_info[num_brs].sql_log_mode |= SQL_LOG_MODE_ON;
	}
      else
	{
	  br_info[num_brs].sql_log_mode &= ~SQL_LOG_MODE_ON;
	}

      tmp_int =
	conf_get_value_table_on_off (ini_getstr
				     (ini, sec_name, "SQL_LOG_APPEND_MODE",
				      "ON"));
      if (tmp_int < 0)
	{
	  goto conf_error;
	}
      else if (tmp_int)
	{
	  br_info[num_brs].sql_log_mode |= SQL_LOG_MODE_APPEND;
	}
      else
	{
	  br_info[num_brs].sql_log_mode &= ~SQL_LOG_MODE_APPEND;
	}

      tmp_int =
	conf_get_value_table_on_off (ini_getstr
				     (ini, sec_name, "SQL_LOG_BIND_VALUE",
				      "ON"));
      if (tmp_int < 0)
	{
	  goto conf_error;
	}
      else if (tmp_int)
	{
	  br_info[num_brs].sql_log_mode |= SQL_LOG_MODE_BIND_VALUE;
	}
      else
	{
	  br_info[num_brs].sql_log_mode &= ~SQL_LOG_MODE_BIND_VALUE;
	}

      br_info[num_brs].sql_log_time =
	ini_getuint (ini, sec_name, "SQL_LOG_TIME", SQL_LOG_TIME_MAX);

#if !defined (WIN32)
      br_info[num_brs].sql_log2 =
	ini_getuint_max (ini, sec_name, "SQL_LOG2", SQL_LOG2_NONE,
			 SQL_LOG2_MAX);
#endif

      br_info[num_brs].sql_log_max_size =
	ini_getuint_max (ini, sec_name, "SQL_LOG_MAX_SIZE",
			 DEFAULT_SQL_LOG_MAX_SIZE, MAX_SQL_LOG_MAX_SIZE);

      br_info[num_brs].auto_add_appl_server =
	conf_get_value_table_on_off (ini_getstr
				     (ini, sec_name, "AUTO_ADD_APPL_SERVER",
				      "ON"));
      if (br_info[num_brs].auto_add_appl_server < 0)
	{
	  goto conf_error;
	}

      br_info[num_brs].session_flag =
	conf_get_value_table_on_off (ini_getstr
				     (ini, sec_name, "SESSION", "OFF"));
      if (br_info[num_brs].session_flag < 0)
	{
	  goto conf_error;
	}

      br_info[num_brs].job_queue_size =
	ini_getuint_max (ini, sec_name, "JOB_QUEUE_SIZE",
			 DEFAULT_JOB_QUEUE_SIZE, JOB_QUEUE_MAX_SIZE);

      br_info[num_brs].priority_gap =
	ini_getuint (ini, sec_name, "PRIORITY_GAP", DEFAULT_PRIORITY_GAP);

      br_info[num_brs].time_to_kill =
	ini_getuint (ini, sec_name, "TIME_TO_KILL", DEFAULT_TIME_TO_KILL);

      strcpy (br_info[num_brs].doc_root,
	      ini_getstr (ini, sec_name, "APPL_ROOT", DEFAULT_DOC_ROOT_DIR));

      br_info[num_brs].error_log =
	get_conf_value_table (ini_getstr
			      (ini, sec_name, "ERROR_LOG", DEFAULT_ERROR_LOG),
			      tbl_error_log);
      if (br_info[num_brs].error_log < 0)
	{
	  goto conf_error;
	}

      strcpy (br_info[num_brs].file_upload_temp_dir,
	      ini_getstr (ini, sec_name, "FILE_UPLOAD_TEMP_DIR",
			  DEFAULT_FILE_UPLOAD_TEMP_DIR));

      strcpy (br_info[num_brs].file_upload_delimiter,
	      ini_getstr (ini, sec_name, "FILE_UPLOAD_DELIMITER",
			  DEFAULT_FILE_UPLOAD_DELIMITER));

      br_info[num_brs].set_cookie =
	conf_get_value_table_on_off (ini_getstr
				     (ini, sec_name, "SET_COOKIE", "OFF"));
      if (br_info[num_brs].set_cookie < 0)
	{
	  goto conf_error;
	}

      br_info[num_brs].entry_value_trim =
	conf_get_value_table_on_off (ini_getstr
				     (ini, sec_name, "ENTRY_VALUE_TRIM",
				      "ON"));
      if (br_info[num_brs].entry_value_trim < 0)
	{
	  goto conf_error;
	}

      br_info[num_brs].oid_check =
	conf_get_value_table_on_off (ini_getstr
				     (ini, sec_name, "OID_CHECK", "ON"));
      if (br_info[num_brs].entry_value_trim < 0)
	{
	  goto conf_error;
	}

      br_info[num_brs].access_log =
	conf_get_value_table_on_off (ini_getstr
				     (ini, sec_name, "ACCESS_LOG", "ON"));
      if (br_info[num_brs].access_log < 0)
	{
	  goto conf_error;
	}

      strcpy (br_info[num_brs].acl_file,
	      ini_getstr (ini, sec_name, "ACCESS_LIST",
			  DEFAULT_EMPTY_STRING));

      br_info[num_brs].enc_appl_flag =
	conf_get_value_table_on_off (ini_getstr
				     (ini, sec_name, "ENC_APPL", "OFF"));
      if (br_info[num_brs].enc_appl_flag < 0)
	{
	  goto conf_error;
	}

      br_info[num_brs].max_string_length =
	ini_getint (ini, sec_name, "MAX_STRING_LENGTH", -1);

      br_info[num_brs].stripped_column_name =
	conf_get_value_table_on_off (ini_getstr
				     (ini, sec_name, "STRIPPED_COLUMN_NAME",
				      "ON"));
      if (br_info[num_brs].stripped_column_name < 0)
	{
	  goto conf_error;
	}

      br_info[num_brs].keep_connection =
	conf_get_value_keep_con (ini_getstr
				 (ini, sec_name, "KEEP_CONNECTION",
				  DEFAULT_KEEP_CONNECTION));
      if (br_info[num_brs].keep_connection < 0)
	{
	  goto conf_error;
	}

      br_info[num_brs].cache_user_info =
	conf_get_value_table_on_off (ini_getstr
				     (ini, sec_name, "CACHE_USER_INFO",
				      "OFF"));
      if (br_info[num_brs].cache_user_info < 0)
	{
	  goto conf_error;
	}

      br_info[num_brs].statement_pooling =
	conf_get_value_table_on_off (ini_getstr
				     (ini, sec_name, "STATEMENT_POOLING",
				      "OFF"));
      if (br_info[num_brs].statement_pooling < 0)
	{
	  goto conf_error;
	}

      br_info[num_brs].jdbc_cache =
	conf_get_value_table_on_off (ini_getstr
				     (ini, sec_name, "JDBC_CACHE", "OFF"));
      if (br_info[num_brs].jdbc_cache < 0)
	{
	  goto conf_error;
	}

      br_info[num_brs].jdbc_cache_only_hint =
	conf_get_value_table_on_off (ini_getstr
				     (ini, sec_name, "JDBC_CACHE_ONLY_HINT",
				      "OFF"));
      if (br_info[num_brs].jdbc_cache_only_hint < 0)
	{
	  goto conf_error;
	}

      br_info[num_brs].jdbc_cache_life_time =
	ini_getint (ini, sec_name, "JDBC_CACHE_LIFE_TIME",
		    DEFAULT_JDBC_CACHE_LIFE_TIME);

      br_info[num_brs].sql_log_single_line =
	conf_get_value_table_on_off (ini_getstr
				     (ini, sec_name, "SQL_LOG_SINGLE_LINE",
				      "ON"));
      if (br_info[num_brs].sql_log_single_line < 0)
	{
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
#if 0
	  if (br_info[i].appl_server != APPL_SERVER_CAS)
	    {
	      if (br_info[i].session_timeout <= 0)
		br_info[i].session_timeout = DEFAULT_SESSION_TIMEOUT;
	    }
#endif
	  dir_repath (br_info[i].doc_root);
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
	  dir_repath (br_info[i].file_upload_temp_dir);
	  mkdir (br_info[i].file_upload_temp_dir, 0777);
	}
      dir_repath (admin_log_file);
    }

#if defined (WIN32)
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
  return 0;

conf_error:
#if !defined (WIN32)
#if !defined (_UC_ADMIN_SO_)
  printf ("config error, %s\n", conf_file);
#endif
#endif
  if (ini)
    {
      ini_parser_free (ini);
    }

  return -1;
}

/*
 * conf_get_value_table_on_off - get value from on/off table
 *   return: 0, 1 or -1 if fail
 *   value(in):
 */
int
conf_get_value_table_on_off (char *value)
{
  return (get_conf_value_table (value, tbl_on_off));
}

/*
 * conf_get_value_keep_con - get value from keep connection table
 *   return: 0, 1 or -1 if fail
 *   value(in):
 */
int
conf_get_value_keep_con (char *value)
{
  return (get_conf_value_table (value, tbl_keep_connection));
}
