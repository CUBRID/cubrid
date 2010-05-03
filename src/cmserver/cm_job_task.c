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
 * cm_job_task.c -
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>		/* umask()         */
#include <sys/stat.h>		/* umask(), stat() */
#include <time.h>
#include <ctype.h>		/* isalpha(), isspace() */
#include <fcntl.h>
#include <errno.h>

#if defined(WINDOWS)
#include <winsock2.h>
#include <windows.h>
#include <process.h>
#include <io.h>
#else
#include <libgen.h>		/* strfind() */
#include <sys/shm.h>
#include <unistd.h>
#include <sys/wait.h>		/* wait()          */
#include <dirent.h>		/* opendir() ...   */
#include <pwd.h>		/* getpwuid_r() */
#include <netdb.h>
#if !defined(HPUX) && !defined(AIX)
#include <sys/procfs.h>
#endif
#endif

#include "cm_stat.h"

#include "cm_porting.h"
#include "cm_server_util.h"
#include "cm_job_task.h"
#include "cm_dep.h"
#include "cm_config.h"
#include "cm_command_execute.h"
#include "cm_user.h"
#include "cm_text_encryption.h"
#include "cm_connect_info.h"

#include<assert.h>

#ifdef _DEBUG_
#include "deb.h"
#define MALLOC(p) debug_malloc(p)
#else
#define MALLOC(p) malloc(p)
#endif

#define PATTERN_LOG 1
#define PATTERN_VOL 2


#define DBMT_ERR_MSG_SET(ERR_BUF, MSG)	\
	strncpy(ERR_BUF, MSG, DBMT_ERROR_MSG_SIZE - 1)
#define CUBRID_ERR_MSG_SET(ERR_BUF)	\
	DBMT_ERR_MSG_SET(ERR_BUF, db_error_string(1))

#define MAX_BROKER_NAMELENGTH 128
#define MAX_AS_COUNT          200
#define SET_LONGLONG_STR(STR, LL_VALUE) sprintf(STR, "%lld", (long long) LL_VALUE);

#define QUERY_BUFFER_MAX        4096

#if !defined(WINDOWS)
#define STRING_APPEND(buffer_p, avail_size_holder, ...) \
  do {                                                          \
    if (avail_size_holder > 0) {                                \
      int n = snprintf (buffer_p, avail_size_holder, __VA_ARGS__);      \
      if (n > 0)        {                                       \
        if ((size_t) n < avail_size_holder) {                   \
          buffer_p += n; avail_size_holder -= n;                \
        } else {                                                \
          buffer_p += (avail_size_holder - 1);                  \
          avail_size_holder = 0;                                \
        }                                                       \
      }                                                         \
    }                                                           \
  } while (0)
#else /* !WINDOWS */
#define STRING_APPEND(buffer_p, avail_size_holder, ...) \
  do {                                                          \
    if (avail_size_holder > 0) {                                \
      int n = _snprintf (buffer_p, avail_size_holder, __VA_ARGS__);     \
      if (n < 0 || (size_t) n >= avail_size_holder) {           \
        buffer_p += (avail_size_holder - 1);                    \
        avail_size_holder = 0;                                  \
        *buffer_p = '\0';                                       \
      } else {                                                  \
        buffer_p += n; avail_size_holder -= n;                  \
      }                                                         \
    }                                                           \
  } while (0)
#endif /* !WINDOWS */

extern T_EMGR_VERSION CLIENT_VERSION;

#if defined(WINDOWS)
static void replace_colon (char *path);
#endif

static char *to_upper_str (char *str, char *buf);
static int uca_conf_write (T_CM_BROKER_CONF * uc_conf, char *del_broekr,
			   char *_dbmt_error);
static char *get_user_name (int uid, char *name_buf);
static char *_op_get_port_from_config (T_CM_BROKER_CONF * uc_conf,
				       char *broker_name);

static int _tsAppendDBList (nvplist * res, char dbdir_flag);

static int _tsParseSpacedb (nvplist * req, nvplist * res, char *dbname,
			    char *_dbmt_error, T_SPACEDB_RESULT * cmd_res);
static void _ts_gen_spaceinfo (nvplist * res, const char *filename,
			       const char *dbinstalldir, const char *type,
			       int pagesize);

static void _tsAppendDBMTUserList (nvplist * res, T_DBMT_USER * dbmt_user,
				   char *_dbmt_error);
static int _ts_lockdb_parse_us (nvplist * res, FILE * infile);


static int get_dbitemdir (char *item_dir, size_t item_dir_size, char *dbname,
			  char *err_buf, int itemtype);
static int get_dblogdir (char *log_dir, size_t log_dir_size, char *dbname,
			 char *err_buf);
static int get_dbvoldir (char *vol_dir, size_t vol_dir_size, char *dbname,
			 char *err_buf);
static int getservershmid (char *dir, char *dbname, char *err_buf);
static int op_make_triggerinput_file_add (nvplist * req,
					  char *input_filename);
static int op_make_triggerinput_file_drop (nvplist * req,
					   char *input_filename);
static int op_make_triggerinput_file_alter (nvplist * req,
					    char *input_filename);
static void op_auto_exec_query_get_newplan_id (char *id_buf, char *filename);

static int get_broker_info_from_filename (char *path, char *br_name,
					  int *as_id);
static char *_ts_get_error_log_param (char *dbname);

static char *cm_get_abs_file_path (const char *filename, char *buf);
static int check_dbpath (char *dir, char *_dbmt_error);

static int file_to_nvpairs (FILE * infile, nvplist * res);
static int file_to_nvp_by_separator (FILE * fp, nvplist * res,
				     char separater);
/* if cubrid.conf's error_log is null, construct it by default value if existing */
static char *
_ts_get_error_log_param (char *dbname)
{
  char *tok[2];
  FILE *infile;
  char buf[PATH_MAX], dbdir[PATH_MAX];

  if ((uRetrieveDBDirectory (dbname, dbdir)) != ERR_NO_ERROR)
    {
      return NULL;
    }
  snprintf (buf, sizeof (buf) - 1, "%s/%s", dbdir, CUBRID_CUBRID_CONF);
  if ((infile = fopen (buf, "r")) == NULL)
    {
      return NULL;
    }

  while (fgets (buf, sizeof (buf), infile))
    {
      ut_trim (buf);
      if (isalpha ((int) buf[0]))
	{
	  if (string_tokenize2 (buf, tok, 2, '=') < 0)
	    {
	      continue;
	    }
	  if (uStringEqual (tok[0], "error_log"))
	    {
	      fclose (infile);
	      if (tok[1][0] == '\0')
		{
		  return NULL;
		}
#if defined(WINDOWS)
	      unix_style_path (tok[1]);
#endif
	      return (strdup (tok[1]));
	    }
	}
    }
  return NULL;
}

int
ts_get_broker_diagdata (nvplist * cli_request, nvplist * cli_response,
			char *_dbmt_error)
{
  int i;
  char *broker_name = NULL;
  T_CM_BROKER_INFO_ALL uc_info;
  T_CM_BROKER_INFO *br_info = NULL;
  int num_busy_count = 0;
  INT64 num_req, num_query, num_tran, num_long_query, num_long_tran,
    num_error_query;
  T_CM_ERROR error;

  num_req = num_query = num_tran = num_long_query = num_long_tran =
    num_error_query = 0;

  /* get broker info, if broker name is NULL then get all of them, 
     else get specified broker diagdata. */
  broker_name = nv_get_val (cli_request, "bname");

  if (cm_get_broker_info (&uc_info, &error) < 0)
    {
      strcpy (_dbmt_error, error.err_msg);
      return ERR_NO_ERROR;
    }

  for (i = 0; i < uc_info.num_info; i++)
    {
      br_info = uc_info.br_info + i;

      if (strcmp (br_info->status, "OFF") != 0 &&
	  (broker_name == NULL
	   || strcasecmp (broker_name, br_info->name) == 0))
	{
	  num_req += br_info->num_req;
	  num_query += br_info->num_query;
	  num_tran += br_info->num_tran;
	  num_long_query += br_info->num_long_query;
	  num_long_tran += br_info->num_long_tran;
	  num_error_query += br_info->num_error_query;
	  num_busy_count += br_info->num_busy_count;
	}
    }

  if (broker_name != NULL)
    {
      nv_add_nvp (cli_response, "bname", broker_name);
    }

  nv_add_nvp_time (cli_response, "time", time (NULL),
		   "%04d/%02d/%02d %02d:%02d:%02d", TIME_STR_FMT_DATE_TIME);
  nv_add_nvp (cli_response, "cas_mon", "start");
  nv_add_nvp_int (cli_response, "cas_mon_act_session", num_busy_count);
  nv_add_nvp_int64 (cli_response, "cas_mon_req", num_req);
  nv_add_nvp_int64 (cli_response, "cas_mon_query", num_query);
  nv_add_nvp_int64 (cli_response, "cas_mon_tran", num_tran);
  nv_add_nvp_int64 (cli_response, "cas_mon_long_query", num_long_query);
  nv_add_nvp_int64 (cli_response, "cas_mon_long_tran", num_long_tran);
  nv_add_nvp_int64 (cli_response, "cas_mon_error_query", num_error_query);
  nv_add_nvp (cli_response, "cas_mon", "end");

  return ERR_NO_ERROR;
}

int
ts_get_diagdata (nvplist * cli_request, nvplist * cli_response,
		 char *_dbmt_error)
{
  int i;
  void *br_shm = NULL, *as_shm = NULL;
  /*T_CM_DIAG_MONITOR_DB_VALUE server_result; */
  char *db_name, *broker_name;
  char *mon_db, *mon_cas;
  T_CM_BROKER_INFO_ALL uc_info;
  T_CM_BROKER_INFO *br_info;
  int num_busy_count = 0;
  INT64 num_req, num_query, num_tran, num_long_query, num_long_tran,
    num_error_query;
  T_CM_ERROR error;

  db_name = nv_get_val (cli_request, "db_name");
  mon_db = nv_get_val (cli_request, "mon_db");
  mon_cas = nv_get_val (cli_request, "mon_cas");

  /*
     if (cm_get_diag_data (&server_result, db_name, mon_db) == 0)
     {
     nv_add_nvp (cli_response, "db_mon", "start");
     nv_add_nvp_int64 (cli_response, "mon_cub_query_open_page",
     server_result.query_open_page);
     nv_add_nvp_int64 (cli_response, "mon_cub_query_opened_page",
     server_result.query_opened_page);
     nv_add_nvp_int64 (cli_response, "mon_cub_query_slow_query",
     server_result.query_slow_query);
     nv_add_nvp_int64 (cli_response, "mon_cub_query_full_scan",
     server_result.query_full_scan);
     nv_add_nvp_int64 (cli_response, "mon_cub_lock_deadlock",
     server_result.lock_deadlock);
     nv_add_nvp_int64 (cli_response, "mon_cub_lock_request",
     server_result.lock_request);
     nv_add_nvp_int64 (cli_response, "mon_cub_conn_cli_request",
     server_result.conn_cli_request);
     nv_add_nvp_int64 (cli_response, "mon_cub_conn_aborted_clients",
     server_result.conn_aborted_clients);
     nv_add_nvp_int64 (cli_response, "mon_cub_conn_conn_req",
     server_result.conn_conn_req);
     nv_add_nvp_int64 (cli_response, "mon_cub_conn_conn_reject",
     server_result.conn_conn_reject);
     nv_add_nvp_int64 (cli_response, "mon_cub_buffer_page_write",
     server_result.buffer_page_write);
     nv_add_nvp_int64 (cli_response, "mon_cub_buffer_page_read",
     server_result.buffer_page_read);
     nv_add_nvp (cli_response, "db_mon", "end");
     }
   */

  if (mon_cas != NULL && strcmp (mon_cas, "yes") == 0)
    {
      num_req = num_query = num_tran = num_long_query = num_long_tran =
	num_error_query = 0;

      broker_name = nv_get_val (cli_request, "broker_name");

      if (cm_get_broker_info (&uc_info, &error) < 0)
	{
	  strcpy (_dbmt_error, error.err_msg);
	  return ERR_NO_ERROR;
	}

      for (i = 0; i < uc_info.num_info; i++)
	{
	  br_info = uc_info.br_info + i;

	  if (strcmp (br_info->status, "OFF") != 0 &&
	      (broker_name == NULL
	       || strcasecmp (broker_name, br_info->name) == 0))
	    {
	      num_req += br_info->num_req;
	      num_query += br_info->num_query;
	      num_tran += br_info->num_tran;
	      num_long_query += br_info->num_long_query;
	      num_long_tran += br_info->num_long_tran;
	      num_error_query += br_info->num_error_query;
	      num_busy_count += br_info->num_busy_count;
	    }
	}

      nv_add_nvp (cli_response, "cas_mon", "start");
      nv_add_nvp_int (cli_response, "cas_mon_act_session", num_busy_count);
      nv_add_nvp_int64 (cli_response, "cas_mon_req", num_req);
      nv_add_nvp_int64 (cli_response, "cas_mon_query", num_query);
      nv_add_nvp_int64 (cli_response, "cas_mon_tran", num_tran);
      nv_add_nvp_int64 (cli_response, "cas_mon_long_query", num_long_query);
      nv_add_nvp_int64 (cli_response, "cas_mon_long_tran", num_long_tran);
      nv_add_nvp_int64 (cli_response, "cas_mon_error_query", num_error_query);
      nv_add_nvp (cli_response, "cas_mon", "end");
    }

  return ERR_NO_ERROR;

}




int
ts_userinfo (nvplist * req, nvplist * res, char *_dbmt_error)
{
  return cm_ts_userinfo (req, res, _dbmt_error);
}

int
ts_create_user (nvplist * req, nvplist * res, char *_dbmt_error)
{
  return cm_ts_create_user (req, res, _dbmt_error);
}

int
ts_delete_user (nvplist * req, nvplist * res, char *_dbmt_error)
{
  return cm_ts_delete_user (req, res, _dbmt_error);
}

int
ts_update_user (nvplist * req, nvplist * res, char *_dbmt_error)
{
  T_DBMT_USER dbmt_user;
  T_DBMT_CON_DBINFO con_dbinfo;
  const char *new_db_user_name;
  const char *new_db_user_pass;
  char *db_name;
  char *ip, *port;
  int i, ret;

  new_db_user_name = nv_get_val (req, "username");
  new_db_user_pass = nv_get_val (req, "userpass");
  db_name = nv_get_val (req, "_DBNAME");

  if (new_db_user_pass)
    {
      if (uStringEqual (new_db_user_pass, "__NULL__"))
	{
	  new_db_user_pass = "";
	}
    }

  ret = cm_ts_update_user (req, res, _dbmt_error);
  if (ret != ERR_NO_ERROR)
    {
      return ret;
    }

#ifndef PK_AUTHENTICAITON
  if (new_db_user_pass)
    {
      char hexacoded[PASSWD_ENC_LENGTH];
      uEncrypt (PASSWD_LENGTH, new_db_user_pass, hexacoded);
      /* update cmdb.pass dbinfo */
      if (dbmt_user_read (&dbmt_user, _dbmt_error) == ERR_NO_ERROR)
	{
	  int src_dbinfo;

	  for (i = 0; i < dbmt_user.num_dbmt_user; i++)
	    {
	      src_dbinfo =
		dbmt_user_search (&(dbmt_user.user_info[i]), db_name);
	      if (src_dbinfo < 0)
		{
		  continue;
		}
	      if (strcmp
		  (dbmt_user.user_info[i].dbinfo[src_dbinfo].uid,
		   new_db_user_name) != 0)
		{
		  continue;
		}
	      strcpy (dbmt_user.user_info[i].dbinfo[src_dbinfo].passwd,
		      hexacoded);
	    }
	  dbmt_user_write_auth (&dbmt_user, _dbmt_error);
	  dbmt_user_free (&dbmt_user);
	}

      /* update conlist dbinfo */
      dbmt_con_set_dbinfo (&con_dbinfo, db_name, new_db_user_name, hexacoded);
      ip = nv_get_val (req, "_IP");
      port = nv_get_val (req, "_PORT");
      if (dbmt_con_write_dbinfo
	  (&con_dbinfo, ip, port, db_name, 1, _dbmt_error) < 0)
	{
	  return ERR_WITH_MSG;
	}
    }
#endif
  return ERR_NO_ERROR;

}


int
ts_class_info (nvplist * req, nvplist * res, char *_dbmt_error)
{
  return cm_ts_class_info (req, res, _dbmt_error);
}

int
ts_class (nvplist * req, nvplist * res, char *_dbmt_error)
{
  return cm_ts_class (req, res, _dbmt_error);
}

#if defined(WINDOWS)
static void
replace_colon (char *path)
{
  char *p;
  for (p = path; *p; p++)
    {
      if (*p == '|')
	*p = ':';
    }
}
#endif

int
ts_update_attribute (nvplist * req, nvplist * res, char *_dbmt_error)
{
  return cm_ts_update_attribute (req, res, _dbmt_error);
}

int
ts2_get_unicas_info (nvplist * in, nvplist * out, char *_dbmt_error)
{
  T_CM_BROKER_INFO_ALL uc_info;
  int i;
  T_CM_BROKER_CONF uc_conf;
  T_CM_ERROR error;
  char *broker_name;

  broker_name = nv_get_val (in, "bname");

  if (cm_get_broker_conf (&uc_conf, NULL, &error) < 0)
    {
      strcpy (_dbmt_error, error.err_msg);
      return ERR_WITH_MSG;
    }
  if (cm_get_broker_info (&uc_info, &error) < 0)
    {
      char *p;
      int tmp_val;

      strcpy (_dbmt_error, error.err_msg);
      nv_add_nvp (out, "open", "brokersinfo");
      for (i = 0; i < uc_conf.num_broker; i++)
	{
	  /*
	   *add broker info to response according to the requested
	   * dbname.if dbname is null, then print all the brokers. 
	   */
	  if ((broker_name != NULL) &&
	      (strcmp (uc_info.br_info[i].name, broker_name) != 0))
	    {
	      continue;
	    }
	  nv_add_nvp (out, "open", "broker");
	  nv_add_nvp (out, "name",
		      cm_br_conf_get_value (&(uc_conf.br_conf[i]), "%"));
	  nv_add_nvp (out, "port",
		      cm_br_conf_get_value (&(uc_conf.br_conf[i]),
					    "BROKER_PORT"));
	  nv_add_nvp (out, "appl_server_shm_id",
		      cm_br_conf_get_value (&(uc_conf.br_conf[i]),
					    "APPL_SERVER_SHM_ID"));
	  p = cm_br_conf_get_value (&(uc_conf.br_conf[i]), "SOURCE_ENV");
	  tmp_val = 1;
	  if (p == NULL || *p == '\0')
	    tmp_val = 0;
	  nv_add_nvp_int (out, "source_env", tmp_val);
	  p = cm_br_conf_get_value (&(uc_conf.br_conf[i]), "ACCESS_LIST");
	  tmp_val = 1;
	  if (p == NULL || *p == '\0')
	    tmp_val = 0;
	  nv_add_nvp_int (out, "access_list", tmp_val);
	  nv_add_nvp (out, "close", "broker");
	}
      nv_add_nvp (out, "close", "brokersinfo");
      nv_add_nvp (out, "brokerstatus", "OFF");
    }
  else
    {
      char *shmid;
      nv_add_nvp (out, "open", "brokersinfo");
      for (i = 0; i < uc_info.num_info; i++)
	{
	  /* 
	   *add broker info to response according to the requested
	   * dbname.if dbname is null, then print all the brokers. 
	   */
	  if ((broker_name != NULL) &&
	      (strcmp (uc_info.br_info[i].name, broker_name) != 0))
	    {
	      continue;
	    }
	  nv_add_nvp (out, "open", "broker");
	  nv_add_nvp (out, "name", uc_info.br_info[i].name);
	  nv_add_nvp (out, "type", uc_info.br_info[i].as_type);
	  if (strcmp (uc_info.br_info[i].status, "OFF") != 0)
	    {
	      nv_add_nvp_int (out, "pid", uc_info.br_info[i].pid);
	      nv_add_nvp_int (out, "port", uc_info.br_info[i].port);
	      nv_add_nvp_int (out, "as", uc_info.br_info[i].num_as);
	      nv_add_nvp_int (out, "jq", uc_info.br_info[i].num_job_q);
#ifdef GET_PSINFO
	      nv_add_nvp_int (out, "thr", uc_info.br_info[i].num_thr);
	      nv_add_nvp_float (out, "cpu", uc_info.br_info[i].pcpu, "%.2f");
	      nv_add_nvp_int (out, "time", uc_info.br_info[i].cpu_time);
#endif
	      nv_add_nvp_int (out, "req", uc_info.br_info[i].num_req);
	      nv_add_nvp_int64 (out, "tran", uc_info.br_info[i].num_tran);
	      nv_add_nvp_int64 (out, "query", uc_info.br_info[i].num_query);
	      nv_add_nvp_int64 (out, "long_tran",
				uc_info.br_info[i].num_long_tran);
	      nv_add_nvp_int64 (out, "long_query",
				uc_info.br_info[i].num_long_query);
	      nv_add_nvp_int64 (out, "error_query",
				uc_info.br_info[i].num_error_query);
	      nv_add_nvp_float (out, "long_tran_time",
				uc_info.br_info[i].
				long_transaction_time / 1000.0, "%.2f");
	      nv_add_nvp_float (out, "long_query_time",
				uc_info.br_info[i].
				long_query_time / 1000.0, "%.2f");

	      nv_add_nvp (out, "keep_conn",
			  uc_info.br_info[i].keep_connection);

	      nv_add_nvp (out, "auto", uc_info.br_info[i].auto_add);
	      nv_add_nvp (out, "ses", uc_info.br_info[i].session_timeout);
	      nv_add_nvp (out, "sqll", uc_info.br_info[i].sql_log_mode);
	      nv_add_nvp (out, "log", uc_info.br_info[i].log_dir);
	      nv_add_nvp (out, "access_mode", uc_info.br_info[i].access_mode);
	    }
	  else
	    {
	      nv_add_nvp (out, "port",
			  _op_get_port_from_config (&uc_conf,
						    uc_info.br_info[i].name));
	    }
	  nv_add_nvp (out, "state", uc_info.br_info[i].status);
	  nv_add_nvp_int (out, "source_env",
			  uc_info.br_info[i].source_env_flag);
	  nv_add_nvp_int (out, "access_list",
			  uc_info.br_info[i].access_list_flag);
	  shmid =
	    cm_br_conf_get_value (cm_conf_find_broker
				  (&uc_conf, uc_info.br_info[i].name),
				  "APPL_SERVER_SHM_ID");
	  nv_add_nvp (out, "appl_server_shm_id", shmid);
	  nv_add_nvp (out, "close", "broker");
	}
      nv_add_nvp (out, "close", "brokersinfo");
      nv_add_nvp (out, "brokerstatus", "ON");
      cm_broker_info_free (&uc_info);
    }

  cm_broker_conf_free (&uc_conf);
  return ERR_NO_ERROR;
}


int
ts2_start_unicas (nvplist * in, nvplist * out, char *_dbmt_error)
{
  T_CM_ERROR error;
  if (cm_broker_env_start (&error) < 0)
    {
      strcpy (_dbmt_error, error.err_msg);
      return ERR_WITH_MSG;
    }

  return ERR_NO_ERROR;
}

int
ts2_stop_unicas (nvplist * in, nvplist * out, char *_dbmt_error)
{
  T_CM_ERROR error;
  if (cm_broker_env_stop (&error) < 0)
    {
      strcpy (_dbmt_error, error.err_msg);
      return ERR_WITH_MSG;
    }

  return ERR_NO_ERROR;
}

int
ts2_get_admin_log_info (nvplist * in, nvplist * out, char *_dbmt_error)
{
  char buf[PATH_MAX];
  struct stat statbuf;

  cm_get_broker_file (UC_FID_ADMIN_LOG, buf);

  if (stat (buf, &statbuf) != 0)
    {
      return ERR_STAT;
    }
  nv_add_nvp (out, "open", "adminloginfo");
  nv_add_nvp (out, "path", buf);
  nv_add_nvp (out, "owner", get_user_name (statbuf.st_uid, buf));
  nv_add_nvp_int (out, "size", statbuf.st_size);
  nv_add_nvp_time (out, "lastupdate", statbuf.st_mtime, "%04d.%02d.%02d",
		   NV_ADD_DATE);
  nv_add_nvp (out, "close", "adminloginfo");

  return ERR_NO_ERROR;
}

int
ts2_get_logfile_info (nvplist * in, nvplist * out, char *_dbmt_error)
{
#if defined(WINDOWS)
  HANDLE handle;
  WIN32_FIND_DATA data;
  char find_file[PATH_MAX];
  int found;
#else
  DIR *dp;
  struct dirent *dirp;
#endif
  struct stat statbuf;
  T_CM_BROKER_CONF uc_conf;
  char logdir[PATH_MAX], err_logdir[PATH_MAX], access_logdir[PATH_MAX];
  const char *v;
  char *bname, *from, buf[1024], scriptdir[PATH_MAX];
  char *cur_file;
  T_CM_ERROR error;

  bname = nv_get_val (in, "broker");
  from = nv_get_val (in, "from");
  nv_add_nvp (out, "broker", bname);
  nv_add_nvp (out, "from", from);
  nv_add_nvp (out, "open", "logfileinfo");

  if (bname == NULL)
    {
      nv_add_nvp (out, "open", "logfileinfo");
      nv_add_nvp (out, "close", "logfileinfo");
      return ERR_NO_ERROR;
    }

  if (cm_get_broker_conf (&uc_conf, NULL, &error) < 0)
    {
      strcpy (_dbmt_error, error.err_msg);
      return ERR_WITH_MSG;
    }
  v =
    cm_br_conf_get_value (cm_conf_find_broker (&uc_conf, bname),
			  "ERROR_LOG_DIR");
  if (v == NULL)
    {
      v = BROKER_LOG_DIR "/error_log";
    }
  cm_get_abs_file_path (v, err_logdir);

  v = cm_br_conf_get_value (cm_conf_find_broker (&uc_conf, bname), "LOG_DIR");
  if (v == NULL)
    {
      v = BROKER_LOG_DIR "/sql_log";
    }
  cm_get_abs_file_path (v, logdir);

  cm_get_abs_file_path (BROKER_LOG_DIR, access_logdir);

  cm_broker_conf_free (&uc_conf);


#if defined(WINDOWS)
  snprintf (find_file, PATH_MAX - 1, "%s/*", access_logdir);
  handle = FindFirstFile (find_file, &data);
  if (handle == INVALID_HANDLE_VALUE)
    {
      nv_add_nvp (out, "open", "logfileinfo");
      nv_add_nvp (out, "close", "logfileinfo");
      return ERR_NO_ERROR;
    }
#else
  dp = opendir (access_logdir);
  if (dp == NULL)
    {
      nv_add_nvp (out, "open", "logfileinfo");
      nv_add_nvp (out, "close", "logfileinfo");
      return ERR_NO_ERROR;
    }
#endif

#if defined(WINDOWS)
  for (found = 1; found; found = FindNextFile (handle, &data))
#else
  while ((dirp = readdir (dp)) != NULL)
#endif
    {
#if defined(WINDOWS)
      cur_file = data.cFileName;
#else
      cur_file = dirp->d_name;
#endif
      if (strstr (cur_file, bname) != NULL)
	{
	  nv_add_nvp (out, "open", "logfile");
	  if (strstr (cur_file, "access") != NULL)
	    {
	      nv_add_nvp (out, "type", "access");
	    }
	  snprintf (buf, sizeof (buf) - 1, "%s/%s", access_logdir, cur_file);
	  nv_add_nvp (out, "path", buf);
	  stat (buf, &statbuf);
	  nv_add_nvp (out, "owner", get_user_name (statbuf.st_uid, buf));
	  nv_add_nvp_int (out, "size", statbuf.st_size);
	  nv_add_nvp_time (out, "lastupdate", statbuf.st_mtime,
			   "%04d.%02d.%02d", NV_ADD_DATE);
	  nv_add_nvp (out, "close", "logfile");
	}
    }
#if defined(WINDOWS)
  FindClose (handle);
#else
  closedir (dp);
#endif

#if defined(WINDOWS)
  snprintf (find_file, PATH_MAX - 1, "%s/*", err_logdir);
  handle = FindFirstFile (find_file, &data);
  if (handle == INVALID_HANDLE_VALUE)
    {
      nv_add_nvp (out, "open", "logfileinfo");
      nv_add_nvp (out, "close", "logfileinfo");
      return ERR_NO_ERROR;
    }
#else
  dp = opendir (err_logdir);
  if (dp == NULL)
    {
      nv_add_nvp (out, "open", "logfileinfo");
      nv_add_nvp (out, "close", "logfileinfo");
      return ERR_NO_ERROR;
    }
#endif

#if defined(WINDOWS)
  for (found = 1; found; found = FindNextFile (handle, &data))
#else
  while ((dirp = readdir (dp)) != NULL)
#endif
    {
#if defined(WINDOWS)
      cur_file = data.cFileName;
#else
      cur_file = dirp->d_name;
#endif
      if (strstr (cur_file, bname) != NULL)
	{
	  nv_add_nvp (out, "open", "logfile");
	  if (strstr (cur_file, "access") != NULL)
	    {
	      nv_add_nvp (out, "type", "access");
	    }
	  else if (strstr (cur_file, "err") != NULL)
	    {
	      nv_add_nvp (out, "type", "error");
	    }
	  snprintf (buf, sizeof (buf) - 1, "%s/%s", err_logdir, cur_file);
	  nv_add_nvp (out, "path", buf);
	  stat (buf, &statbuf);
	  nv_add_nvp (out, "owner", get_user_name (statbuf.st_uid, buf));
	  nv_add_nvp_int (out, "size", statbuf.st_size);
	  nv_add_nvp_time (out, "lastupdate", statbuf.st_mtime,
			   "%04d.%02d.%02d", NV_ADD_DATE);
	  nv_add_nvp (out, "close", "logfile");
	}
    }
#if defined(WINDOWS)
  FindClose (handle);
#else
  closedir (dp);
#endif

#if defined(WINDOWS)
  snprintf (find_file, PATH_MAX - 1, "%s/*", logdir);
  handle = FindFirstFile (find_file, &data);
  if (handle == INVALID_HANDLE_VALUE)
    {
      return ERR_OPENDIR;
    }
#else
  dp = opendir (logdir);
  if (dp == NULL)
    {
      return ERR_OPENDIR;
    }
#endif

#if defined(WINDOWS)
  for (found = 1; found; found = FindNextFile (handle, &data))
#else
  while ((dirp = readdir (dp)) != NULL)
#endif
    {
#if defined(WINDOWS)
      cur_file = data.cFileName;
#else
      cur_file = dirp->d_name;
#endif
      if (strstr (cur_file, bname) != NULL)
	{
	  nv_add_nvp (out, "open", "logfile");
	  if (strstr (cur_file, "access") != NULL)
	    {
	      nv_add_nvp (out, "type", "access");
	    }
	  snprintf (buf, sizeof (buf) - 1, "%s/%s", logdir, cur_file);
	  nv_add_nvp (out, "path", buf);
	  stat (buf, &statbuf);
	  nv_add_nvp (out, "owner", get_user_name (statbuf.st_uid, buf));
	  nv_add_nvp_int (out, "size", statbuf.st_size);
	  nv_add_nvp_time (out, "lastupdate", statbuf.st_mtime,
			   "%04d.%02d.%02d", NV_ADD_DATE);
	  nv_add_nvp (out, "close", "logfile");
	}
    }
#if defined(WINDOWS)
  FindClose (handle);
#else
  closedir (dp);
#endif

  snprintf (scriptdir, PATH_MAX - 1, "%s", logdir);
#if defined(WINDOWS)
  snprintf (find_file, PATH_MAX - 1, "%s/*", scriptdir);
  handle = FindFirstFile (find_file, &data);
  if (handle == INVALID_HANDLE_VALUE)
    {
      return ERR_OPENDIR;
    }
#else
  dp = opendir (scriptdir);
  if (dp == NULL)
    {
      return ERR_OPENDIR;
    }
#endif

  sprintf (bname, "%s_", bname);
#if defined(WINDOWS)
  for (found = 1; found; found = FindNextFile (handle, &data))
#else
  while ((dirp = readdir (dp)) != NULL)
#endif
    {
#if defined(WINDOWS)
      cur_file = data.cFileName;
#else
      cur_file = dirp->d_name;
#endif

      if (strstr (cur_file, bname) != NULL)
	{
	  nv_add_nvp (out, "open", "logfile");
	  nv_add_nvp (out, "type", "script");
	  snprintf (buf, sizeof (buf) - 1, "%s/%s", scriptdir, cur_file);
	  nv_add_nvp (out, "path", buf);
	  stat (buf, &statbuf);
	  nv_add_nvp (out, "owner", get_user_name (statbuf.st_uid, buf));
	  nv_add_nvp_int (out, "size", statbuf.st_size);
	  nv_add_nvp_time (out, "lastupdate", statbuf.st_mtime,
			   "%04d.%02d.%02d", NV_ADD_DATE);
	  nv_add_nvp (out, "close", "logfile");
	}
    }
#if defined(WINDOWS)
  FindClose (handle);
#else
  closedir (dp);
#endif
  nv_add_nvp (out, "close", "logfileinfo");

  return ERR_NO_ERROR;
}


int
ts2_get_add_broker_info (nvplist * in, nvplist * out, char *_dbmt_error)
{
  FILE *infile;
  char broker_conf_path[PATH_MAX], strbuf[1024];

  cm_get_broker_file (UC_FID_CUBRID_BROKER_CONF, broker_conf_path);

  if (access (broker_conf_path, F_OK) < 0)
    {
      return ERR_FILE_OPEN_FAIL;
    }

  infile = fopen (broker_conf_path, "r");
  if (infile == NULL)
    {
      strcpy (_dbmt_error, broker_conf_path);
      return ERR_FILE_OPEN_FAIL;
    }

  nv_add_nvp (out, "confname", "broker");
  nv_add_nvp (out, "open", "conflist");

  while (fgets (strbuf, sizeof (strbuf), infile) != NULL)
    {
      nv_add_nvp (out, "confdata", strbuf);
    }
  nv_add_nvp (out, "close", "conflist");
  fclose (infile);

  return ERR_NO_ERROR;
}

int
ts2_delete_broker (nvplist * in, nvplist * out, char *_dbmt_error)
{
  FILE *infile, *outfile;
  char *bname;
  char buf[1024], tmpfile[PATH_MAX];
  char fpath[PATH_MAX];
  int retval;
  T_CM_BROKER_CONF uc_conf;
  T_CM_ERROR error;

  if ((bname = nv_get_val (in, "bname")) == NULL)
    {
      return ERR_PARAM_MISSING;
    }
  if (cm_get_broker_conf (&uc_conf, NULL, &error) < 0)
    {
      strcpy (_dbmt_error, error.err_msg);
      return ERR_WITH_MSG;
    }

  retval = uca_conf_write (&uc_conf, bname, _dbmt_error);
  cm_broker_conf_free (&uc_conf);

  if (retval != ERR_NO_ERROR)
    {
      return retval;
    }
  /* autounicasm.conf */
  conf_get_dbmt_file (FID_AUTO_UNICASM_CONF, fpath);
  infile = fopen (fpath, "r");
  snprintf (tmpfile, PATH_MAX - 1, "%s/DBMT_casop_%d.%d", sco.dbmt_tmp_dir,
	    TS2_DELETEBROKER, (int) getpid ());
  outfile = fopen (tmpfile, "w");
  if (infile == NULL || outfile == NULL)
    {
      if (infile)
	fclose (infile);
      if (outfile)
	fclose (outfile);
    }
  else
    {
      char conf_bname[128];
      while (fgets (buf, sizeof (buf), infile))
	{
	  if (sscanf (buf, "%127s", conf_bname) < 1)
	    {
	      continue;
	    }
	  if (strcmp (conf_bname, bname) != 0)
	    {
	      fputs (buf, outfile);
	    }
	}
      fclose (infile);
      fclose (outfile);

      move_file (tmpfile, fpath);
    }

  return ERR_NO_ERROR;
}


int
ts2_get_broker_status (nvplist * in, nvplist * out, char *_dbmt_error)
{
  T_CM_CAS_INFO_ALL as_info_set;
  T_CM_JOB_INFO_ALL job_info_set;
  T_CM_ERROR error;
  char *bname, buf[1024];
  int i;

  if ((bname = nv_get_val (in, "bname")) == NULL)
    {
      return ERR_PARAM_MISSING;
    }
  nv_add_nvp (out, "bname", bname);
  nv_add_nvp_time (out, "time", time (NULL),
		   "%04d/%02d/%02d %02d:%02d:%02d", TIME_STR_FMT_DATE_TIME);
  if (cm_get_cas_info (bname, &as_info_set, &job_info_set, &error) < 0)
    {
      strcpy (_dbmt_error, error.err_msg);
      return ERR_NO_ERROR;
    }
  for (i = 0; i < as_info_set.num_info; i++)
    {
      if (strcmp (as_info_set.as_info[i].service_flag, "ON") != 0)
	continue;

      nv_add_nvp (out, "open", "asinfo");

      nv_add_nvp_int (out, "as_id", as_info_set.as_info[i].id);
      nv_add_nvp_int (out, "as_pid", as_info_set.as_info[i].pid);
      nv_add_nvp_int (out, "as_c", as_info_set.as_info[i].num_request);
      /* add "as_port" nvp in windows: 
         as_port only shows up on windows platform. */
#if defined(WINDOWS)
      nv_add_nvp_int (out, "as_port", as_info_set.as_info[i].as_port);
#endif
      nv_add_nvp_int (out, "as_psize", as_info_set.as_info[i].psize);
      nv_add_nvp (out, "as_status", as_info_set.as_info[i].status);
      nv_add_nvp_float (out, "as_cpu", as_info_set.as_info[i].pcpu, "%.2f");
      cm_cpu_time_str (as_info_set.as_info[i].cpu_time, buf);
      nv_add_nvp (out, "as_ctime", buf);
      nv_add_nvp_time (out, "as_lat",
		       as_info_set.as_info[i].last_access_time,
		       "%02d/%02d/%02d %02d:%02d:%02d", NV_ADD_DATE_TIME);
      nv_add_nvp (out, "as_cur", as_info_set.as_info[i].log_msg);
      nv_add_nvp_int64 (out, "as_num_query",
			as_info_set.as_info[i].num_queries_processed);
      nv_add_nvp_int64 (out, "as_num_tran",
			as_info_set.as_info[i].num_transactions_processed);
      nv_add_nvp_int64 (out, "as_long_query",
			as_info_set.as_info[i].num_long_queries);
      nv_add_nvp_int64 (out, "as_long_tran",
			as_info_set.as_info[i].num_long_transactions);
      nv_add_nvp_int64 (out, "as_error_query",
			as_info_set.as_info[i].num_error_queries);
      nv_add_nvp (out, "as_dbname", as_info_set.as_info[i].database_name);
      nv_add_nvp (out, "as_dbhost", as_info_set.as_info[i].database_host);
      nv_add_nvp_time (out, "as_lct",
		       as_info_set.as_info[i].last_connect_time,
		       "%02d/%02d/%02d %02d:%02d:%02d", NV_ADD_DATE_TIME);
      /* add "as_client_ip" nvp. */
      nv_add_nvp (out, "as_client_ip", as_info_set.as_info[i].clt_ip_addr);
      nv_add_nvp (out, "close", "asinfo");
    }
  for (i = 0; i < job_info_set.num_info; i++)
    {
      nv_add_nvp (out, "open", "jobinfo");
      nv_add_nvp_int (out, "job_id", job_info_set.job_info[i].id);
      nv_add_nvp_int (out, "job_priority", job_info_set.job_info[i].priority);
      nv_add_nvp (out, "job_ip", job_info_set.job_info[i].ipstr);
      nv_add_nvp_time (out, "job_time", job_info_set.job_info[i].recv_time,
		       "%02d:%02d:%02d", NV_ADD_TIME);
      snprintf (buf, sizeof (buf) - 1, "%s:%s",
		job_info_set.job_info[i].script,
		job_info_set.job_info[i].prgname);
      nv_add_nvp (out, "job_request", buf);
      nv_add_nvp (out, "close", "jobinfo");
    }

  cm_cas_info_free (&as_info_set, &job_info_set);

  return ERR_NO_ERROR;
}



int
ts2_set_broker_conf (nvplist * in, nvplist * out, char *_dbmt_error)
{
  FILE *outfile;
  char broker_conf_path[PATH_MAX];
  char *conf, *confdata;
  int nv_len, i;

  cm_get_broker_file (UC_FID_CUBRID_BROKER_CONF, broker_conf_path);

  if ((outfile = fopen (broker_conf_path, "w")) == NULL)
    {
      return ERR_TMPFILE_OPEN_FAIL;
    }

  nv_len = in->nvplist_leng;
  for (i = 1; i < nv_len; i++)
    {
      nv_lookup (in, i, &conf, &confdata);
      if ((conf != NULL) && (strcmp (conf, "confdata") == 0))
	{
	  if (confdata == NULL)
	    {
	      fprintf (outfile, "\n");
	    }
	  else
	    {
	      fprintf (outfile, "%s\n", confdata);
	    }
	}
    }

  fclose (outfile);

  return ERR_NO_ERROR;
}


int
ts2_start_broker (nvplist * in, nvplist * out, char *_dbmt_error)
{
  char *bname;
  T_CM_ERROR error;

  if ((bname = nv_get_val (in, "bname")) == NULL)
    {
      strcpy (_dbmt_error, "broker name");
      return ERR_PARAM_MISSING;
    }

  if (cm_broker_on (bname, &error) < 0)
    {
      strcpy (_dbmt_error, error.err_msg);
      return ERR_WITH_MSG;
    }
  return ERR_NO_ERROR;
}

int
ts2_stop_broker (nvplist * in, nvplist * out, char *_dbmt_error)
{
  char *bname;
  T_CM_ERROR error;

  if ((bname = nv_get_val (in, "bname")) == NULL)
    {
      strcpy (_dbmt_error, "broker name");
      return ERR_PARAM_MISSING;
    }

  if (cm_broker_off (bname, &error) < 0)
    {
      strcpy (_dbmt_error, error.err_msg);
      return ERR_WITH_MSG;
    }
  return ERR_NO_ERROR;
}

int
ts2_restart_broker_as (nvplist * in, nvplist * out, char *_dbmt_error)
{
  char *bname, *asnum;
  T_CM_ERROR error;

  bname = nv_get_val (in, "bname");
  asnum = nv_get_val (in, "asnum");
  if (bname == NULL || asnum == NULL)
    {
      return ERR_PARAM_MISSING;
    }
  if (cm_broker_as_restart (bname, atoi (asnum), &error) < 0)
    {
      strcpy (_dbmt_error, error.err_msg);
      return ERR_WITH_MSG;
    }
  return ERR_NO_ERROR;
}

int
ts_set_sysparam (nvplist * req, nvplist * res, char *_dbmt_error)
{
  FILE *outfile;
  char conf_path[PATH_MAX];
  char *conf, *conf_data, *conf_name;
  int nv_len, i;

  conf_name = nv_get_val (req, "confname");
  if (conf_name == NULL)
    return ERR_PARAM_MISSING;

  if (strcmp (conf_name, "cubridconf") == 0)
    {
      snprintf (conf_path, PATH_MAX - 1, "%s/conf/%s", sco.szCubrid,
		CUBRID_CUBRID_CONF);
    }
  else if (strcmp (conf_name, "cmconf") == 0)
    {
      snprintf (conf_path, PATH_MAX - 1, "%s/conf/%s", sco.szCubrid,
		CUBRID_DBMT_CONF);
    }
  else
    {
      strcpy (_dbmt_error, "confname error");
      return ERR_GENERAL_ERROR;
    }

  if ((outfile = fopen (conf_path, "w")) == NULL)
    {
      return ERR_TMPFILE_OPEN_FAIL;
    }

  nv_len = req->nvplist_leng;
  for (i = 1; i < nv_len; i++)
    {
      nv_lookup (req, i, &conf, &conf_data);
      if ((conf != NULL) && (strcmp (conf, "confdata") == 0))
	{
	  if (conf_data == NULL)
	    {
	      fprintf (outfile, "\n");
	    }
	  else
	    {
	      fprintf (outfile, "%s\n", conf_data);
	    }
	}
    }

  fclose (outfile);

  return ERR_NO_ERROR;
}


int
ts_get_all_sysparam (nvplist * req, nvplist * res, char *_dbmt_error)
{
  FILE *infile;
  char conf_path[PATH_MAX], strbuf[1024];
  char *conf_name;

  conf_name = nv_get_val (req, "confname");
  if (conf_name == NULL)
    return ERR_PARAM_MISSING;

  if (strcmp (conf_name, "cubridconf") == 0)
    {
      snprintf (conf_path, PATH_MAX - 1, "%s/conf/%s", sco.szCubrid,
		CUBRID_CUBRID_CONF);
    }
  else if (strcmp (conf_name, "cmconf") == 0)
    {
      snprintf (conf_path, PATH_MAX - 1, "%s/conf/%s", sco.szCubrid,
		CUBRID_DBMT_CONF);
    }
  else
    {
      strcpy (_dbmt_error, "confname error");
      return ERR_GENERAL_ERROR;
    }

  if (access (conf_path, F_OK) < 0)
    {
      return ERR_FILE_OPEN_FAIL;
    }

  infile = fopen (conf_path, "r");
  if (infile == NULL)
    {
      strcpy (_dbmt_error, conf_path);
      return ERR_FILE_OPEN_FAIL;
    }

  nv_add_nvp (res, "confname", conf_name);
  nv_add_nvp (res, "open", "conflist");

  while (fgets (strbuf, sizeof (strbuf), infile) != NULL)
    {
      nv_add_nvp (res, "confdata", strbuf);
    }
  nv_add_nvp (res, "close", "conflist");
  fclose (infile);

  return ERR_NO_ERROR;
}

int
tsCreateDBMTUser (nvplist * req, nvplist * res, char *_dbmt_error)
{
  int num_authinfo = 0, num_dbmt_user;
  char *dbmt_id, *passwd_p;
  int i, retval;
  char dbmt_passwd[PASSWD_ENC_LENGTH];
  const char *casauth, *dbcreate, *status_monitor;
  T_DBMT_USER dbmt_user;
  T_DBMT_USER_AUTHINFO *authinfo = NULL;

  memset (&dbmt_user, 0, sizeof (T_DBMT_USER));

  if ((dbmt_id = nv_get_val (req, "targetid")) == NULL)
    {
      sprintf (_dbmt_error, "%s", "target id");
      return ERR_PARAM_MISSING;
    }
  if ((passwd_p = nv_get_val (req, "password")) == NULL)
    {
      sprintf (_dbmt_error, "%s", "password");
      return ERR_PARAM_MISSING;
    }
  uEncrypt (PASSWD_LENGTH, passwd_p, dbmt_passwd);

  if ((retval = dbmt_user_read (&dbmt_user, _dbmt_error)) != ERR_NO_ERROR)
    {
      return retval;
    }
  num_dbmt_user = dbmt_user.num_dbmt_user;
  for (i = 0; i < num_dbmt_user; i++)
    {
      if (strcmp (dbmt_user.user_info[i].user_name, dbmt_id) == 0)
	{
	  dbmt_user_free (&dbmt_user);
	  sprintf (_dbmt_error, "%s", dbmt_id);
	  return ERR_DBMTUSER_EXIST;
	}
    }

  /* set authority info */
  if ((casauth = nv_get_val (req, "casauth")) == NULL)
    {
      casauth = "";
    }
  authinfo =
    (T_DBMT_USER_AUTHINFO *) increase_capacity (authinfo,
						sizeof (T_DBMT_USER_AUTHINFO),
						num_authinfo,
						num_authinfo + 1);
  if (authinfo == NULL)
    {
      dbmt_user_free (&dbmt_user);
      return ERR_MEM_ALLOC;
    }
  num_authinfo++;
  dbmt_user_set_authinfo (&(authinfo[num_authinfo - 1]), "unicas", casauth);

  if ((dbcreate = nv_get_val (req, "dbcreate")) == NULL)
    {
      dbcreate = "";
    }
  authinfo =
    (T_DBMT_USER_AUTHINFO *) increase_capacity (authinfo,
						sizeof (T_DBMT_USER_AUTHINFO),
						num_authinfo,
						num_authinfo + 1);
  if (authinfo == NULL)
    {
      dbmt_user_free (&dbmt_user);
      return ERR_MEM_ALLOC;
    }
  num_authinfo++;
  dbmt_user_set_authinfo (&(authinfo[num_authinfo - 1]), "dbcreate",
			  dbcreate);

  if ((status_monitor = nv_get_val (req, "statusmonitorauth")) == NULL)
    status_monitor = "";
  authinfo =
    (T_DBMT_USER_AUTHINFO *) increase_capacity (authinfo,
						sizeof (T_DBMT_USER_AUTHINFO),
						num_authinfo,
						num_authinfo + 1);
  if (authinfo == NULL)
    {
      dbmt_user_free (&dbmt_user);
      return ERR_MEM_ALLOC;
    }
  num_authinfo++;
  dbmt_user_set_authinfo (&(authinfo[num_authinfo - 1]), "statusmonitorauth",
			  status_monitor);

  /* set user info */
  dbmt_user.user_info =
    (T_DBMT_USER_INFO *) increase_capacity (dbmt_user.user_info,
					    sizeof (T_DBMT_USER_INFO),
					    num_dbmt_user, num_dbmt_user + 1);
  if (dbmt_user.user_info == NULL)
    {
      dbmt_user_free (&dbmt_user);
      if (authinfo != NULL)
	{
	  free (authinfo);
	}
      return ERR_MEM_ALLOC;
    }
  num_dbmt_user++;
  dbmt_user_set_userinfo (&(dbmt_user.user_info[num_dbmt_user - 1]), dbmt_id,
			  dbmt_passwd, num_authinfo, authinfo, 0, NULL);
  dbmt_user.num_dbmt_user = num_dbmt_user;

  retval = dbmt_user_write_auth (&dbmt_user, _dbmt_error);
  if (retval != ERR_NO_ERROR)
    {
      dbmt_user_free (&dbmt_user);
      return retval;
    }
  dbmt_user_write_pass (&dbmt_user, _dbmt_error);

  /* add dblist */
  retval = _tsAppendDBList (res, 0);
  if (retval != ERR_NO_ERROR)
    {
      ut_error_log (req, "error while adding database lists to response");
      return retval;
    }

  _tsAppendDBMTUserList (res, &dbmt_user, _dbmt_error);
  dbmt_user_free (&dbmt_user);

  return ERR_NO_ERROR;
}


int
tsDeleteDBMTUser (nvplist * req, nvplist * res, char *_dbmt_error)
{
  T_DBMT_USER dbmt_user;
  char *dbmt_id;
  int i, retval, usr_index;
  char file[PATH_MAX];

  if ((dbmt_id = nv_get_val (req, "targetid")) == NULL)
    {
      sprintf (_dbmt_error, "%s", "target id");
      return ERR_PARAM_MISSING;
    }

  if ((retval = dbmt_user_read (&dbmt_user, _dbmt_error)) != ERR_NO_ERROR)
    return retval;

  usr_index = -1;
  for (i = 0; i < dbmt_user.num_dbmt_user; i++)
    {
      if (strcmp (dbmt_user.user_info[i].user_name, dbmt_id) == 0)
	{
	  dbmt_user.user_info[i].user_name[0] = '\0';
	  usr_index = i;
	  break;
	}
    }
  if (usr_index < 0)
    {
      strcpy (_dbmt_error, conf_get_dbmt_file2 (FID_DBMT_CUBRID_PASS, file));
      dbmt_user_free (&dbmt_user);
      return ERR_FILE_INTEGRITY;
    }

  retval = dbmt_user_write_auth (&dbmt_user, _dbmt_error);
  if (retval != ERR_NO_ERROR)
    {
      dbmt_user_free (&dbmt_user);
      return retval;
    }
  dbmt_user_write_pass (&dbmt_user, _dbmt_error);

  /* add dblist */
  retval = _tsAppendDBList (res, 0);
  if (retval != ERR_NO_ERROR)
    {
      return retval;
    }

  _tsAppendDBMTUserList (res, &dbmt_user, _dbmt_error);
  dbmt_user_free (&dbmt_user);

  return ERR_NO_ERROR;
}

int
tsUpdateDBMTUser (nvplist * req, nvplist * res, char *_dbmt_error)
{
  int i, j, usr_index, retval;
  int cas_idx = -1, dbcreate_idx = -1, status_monitor_idx = -1;
  char *dbmt_id;
  char file[PATH_MAX];
  T_DBMT_USER dbmt_user;
  T_DBMT_USER_DBINFO *usr_dbinfo = NULL;
  T_DBMT_USER_AUTHINFO *usr_authinfo = NULL;
  int num_dbinfo = 0, num_authinfo = 0;
  char *z_name, *z_value;
  char *dbname, *dbid, *dbpassword, *casauth, *dbcreate, *status_monitor;
  char *broker_address;
  char passwd_hexa[PASSWD_ENC_LENGTH];

  memset (&dbmt_user, 0, sizeof (T_DBMT_USER));

  if ((dbmt_id = nv_get_val (req, "targetid")) == NULL)
    {
      sprintf (_dbmt_error, "%s", "target id");
      return ERR_PARAM_MISSING;
    }

  for (i = 0; i < req->nvplist_leng; ++i)
    {
      dbname = dbid = dbpassword = NULL;
      broker_address = NULL;

      nv_lookup (req, i, &z_name, &z_value);
      if (uStringEqual (z_name, "open"))
	{
	  nv_lookup (req, ++i, &z_name, &z_value);
	  while (!uStringEqual (z_name, "close"))
	    {
	      if (uStringEqual (z_name, "dbname"))
		{
		  dbname = z_value;
		}
	      else if (uStringEqual (z_name, "dbid"))
		{
		  dbid = z_value;
		}
	      else if (uStringEqual (z_name, "dbpassword"))
		{
		  dbpassword = z_value;
		}
	      else if (uStringEqual (z_name, "dbbrokeraddress"))
		{
		  broker_address = z_value;
		}
	      else
		{
		  if (usr_dbinfo != NULL)
		    {
		      free (usr_dbinfo);
		    }
		  return ERR_REQUEST_FORMAT;
		}
	      nv_lookup (req, ++i, &z_name, &z_value);
	      if (i >= req->nvplist_leng)
		break;
	    }
	}
      if (dbname == NULL || dbid == NULL)
	{
	  continue;
	}
      uEncrypt (PASSWD_LENGTH, dbpassword, passwd_hexa);
      usr_dbinfo =
	(T_DBMT_USER_DBINFO *) increase_capacity (usr_dbinfo,
						  sizeof (T_DBMT_USER_DBINFO),
						  num_dbinfo, num_dbinfo + 1);
      if (usr_dbinfo == NULL)
	{
	  return ERR_MEM_ALLOC;
	}
      num_dbinfo++;
      dbmt_user_set_dbinfo (&(usr_dbinfo[num_dbinfo - 1]), dbname, "admin",
			    dbid, passwd_hexa, broker_address);
    }

  if ((casauth = nv_get_val (req, "casauth")) != NULL)
    {
      cas_idx = num_authinfo;
    }
  if ((dbcreate = nv_get_val (req, "dbcreate")) != NULL)
    {
      dbcreate_idx = num_authinfo + 1;
    }
  if ((status_monitor = nv_get_val (req, "statusmonitorauth")) != NULL)
    {
      status_monitor_idx = num_authinfo + 2;
    }

  if ((casauth != NULL) || (dbcreate != NULL))
    {
      usr_authinfo =
	(T_DBMT_USER_AUTHINFO *) increase_capacity (usr_authinfo,
						    sizeof
						    (T_DBMT_USER_AUTHINFO),
						    num_authinfo,
						    num_authinfo + 3);
      if (usr_authinfo == NULL)
	{
	  return ERR_MEM_ALLOC;
	}
      num_authinfo += 3;
    }

  if (casauth != NULL && cas_idx >= 0)
    {
      dbmt_user_set_authinfo (&(usr_authinfo[cas_idx]), "unicas", casauth);
    }
  if (dbcreate != NULL && dbcreate_idx >= 0)
    {
      dbmt_user_set_authinfo (&(usr_authinfo[dbcreate_idx]), "dbcreate",
			      dbcreate);
    }
  if (status_monitor != NULL && status_monitor_idx >= 0)
    {
      dbmt_user_set_authinfo (&(usr_authinfo[status_monitor_idx]),
			      "statusmonitorauth", status_monitor);
    }

  if ((retval = dbmt_user_read (&dbmt_user, _dbmt_error)) != ERR_NO_ERROR)
    {
      if (usr_dbinfo != NULL)
	{
	  free (usr_dbinfo);
	}
      if (usr_authinfo != NULL)
	{
	  free (usr_authinfo);
	}
      return retval;
    }

  usr_index = -1;
  for (i = 0; i < dbmt_user.num_dbmt_user; i++)
    {
      if (strcmp (dbmt_user.user_info[i].user_name, dbmt_id) == 0)
	{
	  usr_index = i;
	  break;
	}
    }
  if (usr_index < 0)
    {
      strcpy (_dbmt_error, conf_get_dbmt_file2 (FID_DBMT_CUBRID_PASS, file));
      dbmt_user_free (&dbmt_user);
      if (usr_dbinfo != NULL)
	{
	  free (usr_dbinfo);
	}
      if (usr_authinfo != NULL)
	{
	  free (usr_authinfo);
	}
      return ERR_FILE_INTEGRITY;
    }

  /* auth info */
  if (dbmt_user.user_info[usr_index].authinfo == NULL)
    {
      dbmt_user.user_info[usr_index].num_authinfo = num_authinfo;
      dbmt_user.user_info[usr_index].authinfo = usr_authinfo;
      usr_authinfo = NULL;
    }
  else if (usr_authinfo != NULL)
    {
      T_DBMT_USER_INFO *current_user_info =
	(T_DBMT_USER_INFO *) & (dbmt_user.user_info[usr_index]);

      for (j = 0; j < num_authinfo; j++)
	{
	  int find_idx = -1;
	  for (i = 0; i < current_user_info->num_authinfo; i++)
	    {
	      if (strcmp
		  (current_user_info->authinfo[i].domain,
		   usr_authinfo[j].domain) == 0)
		{
		  find_idx = i;
		  break;
		}
	    }
	  if (find_idx == -1)
	    {
	      current_user_info->authinfo =
		(T_DBMT_USER_AUTHINFO *)
		increase_capacity (current_user_info->authinfo,
				   sizeof (T_DBMT_USER_AUTHINFO),
				   current_user_info->num_authinfo,
				   current_user_info->num_authinfo + 1);
	      if (current_user_info->authinfo == NULL)
		{
		  return ERR_MEM_ALLOC;
		}
	      current_user_info->num_authinfo++;
	      find_idx = current_user_info->num_authinfo - 1;
	    }
	  dbmt_user_set_authinfo (&(current_user_info->authinfo[find_idx]),
				  usr_authinfo[j].domain,
				  usr_authinfo[j].auth);
	}
    }

  /* db info */
  if (dbmt_user.user_info[usr_index].dbinfo == NULL)
    {
      dbmt_user.user_info[usr_index].num_dbinfo = num_dbinfo;
      dbmt_user.user_info[usr_index].dbinfo = usr_dbinfo;
      usr_dbinfo = NULL;
    }
  else if (usr_dbinfo != NULL)
    {
      T_DBMT_USER_INFO *current_user_info =
	(T_DBMT_USER_INFO *) & (dbmt_user.user_info[usr_index]);

      for (j = 0; j < num_dbinfo; j++)
	{
	  int find_idx = -1;
	  for (i = 0; i < current_user_info->num_dbinfo; i++)
	    {
	      if (strcmp (current_user_info->dbinfo[i].dbname,
			  usr_dbinfo[j].dbname) == 0)
		{
		  find_idx = i;
		  break;
		}
	    }
	  if (find_idx == -1)
	    {
	      current_user_info->dbinfo =
		(T_DBMT_USER_DBINFO *) increase_capacity (current_user_info->
							  dbinfo,
							  sizeof
							  (T_DBMT_USER_DBINFO),
							  current_user_info->
							  num_dbinfo,
							  current_user_info->
							  num_dbinfo + 1);
	      if (current_user_info->dbinfo == NULL)
		{
		  return ERR_MEM_ALLOC;
		}
	      current_user_info->num_dbinfo++;
	      find_idx = current_user_info->num_dbinfo - 1;
	    }
	  dbmt_user_set_dbinfo (&(current_user_info->dbinfo[find_idx]),
				usr_dbinfo[j].dbname, usr_dbinfo[j].auth,
				usr_dbinfo[j].uid, usr_dbinfo[j].passwd,
				usr_dbinfo[j].broker_address);
	}
    }

  retval = dbmt_user_write_auth (&dbmt_user, _dbmt_error);
  if (retval != ERR_NO_ERROR)
    {
      dbmt_user_free (&dbmt_user);
      if (usr_dbinfo)
	{
	  free (usr_dbinfo);
	}
      if (usr_authinfo)
	{
	  free (usr_authinfo);
	}
      return retval;
    }

  /* add dblist */
  retval = _tsAppendDBList (res, 0);
  if (retval != ERR_NO_ERROR)
    {
      return retval;
    }

  _tsAppendDBMTUserList (res, &dbmt_user, _dbmt_error);
  dbmt_user_free (&dbmt_user);
  if (usr_dbinfo)
    {
      free (usr_dbinfo);
    }
  if (usr_authinfo)
    {
      free (usr_authinfo);
    }
  return ERR_NO_ERROR;
}

int
tsChangeDBMTUserPasswd (nvplist * req, nvplist * res, char *_dbmt_error)
{
  T_DBMT_USER dbmt_user;
  int i, retval, usr_index;
  char *dbmt_id, *new_passwd;
  char file[PATH_MAX];

  if ((dbmt_id = nv_get_val (req, "targetid")) == NULL)
    {
      sprintf (_dbmt_error, "%s", "target id");
      return ERR_PARAM_MISSING;
    }
  if ((new_passwd = nv_get_val (req, "newpassword")) == NULL)
    {
      sprintf (_dbmt_error, "%s", "new password");
      return ERR_PARAM_MISSING;
    }

  if ((retval = dbmt_user_read (&dbmt_user, _dbmt_error)) != ERR_NO_ERROR)
    {
      return retval;
    }
  usr_index = -1;
  for (i = 0; i < dbmt_user.num_dbmt_user; i++)
    {
      if (strcmp (dbmt_user.user_info[i].user_name, dbmt_id) == 0)
	{
	  if (new_passwd == NULL)
	    {
	      dbmt_user.user_info[i].user_passwd[0] = '\0';
	    }
	  else
	    {
	      char hexacoded[PASSWD_ENC_LENGTH];

	      uEncrypt (PASSWD_LENGTH, new_passwd, hexacoded);
	      strcpy (dbmt_user.user_info[i].user_passwd, hexacoded);
	    }
	  usr_index = i;
	  break;
	}
    }
  if (usr_index < 0)
    {
      strcpy (_dbmt_error, conf_get_dbmt_file2 (FID_DBMT_CUBRID_PASS, file));
      dbmt_user_free (&dbmt_user);
      return ERR_FILE_INTEGRITY;
    }

  retval = dbmt_user_write_pass (&dbmt_user, _dbmt_error);
  if (retval != ERR_NO_ERROR)
    {
      dbmt_user_free (&dbmt_user);
      return retval;
    }

  /* add dblist */
  retval = _tsAppendDBList (res, 0);
  if (retval != ERR_NO_ERROR)
    {
      return retval;
    }

  _tsAppendDBMTUserList (res, &dbmt_user, _dbmt_error);
  dbmt_user_free (&dbmt_user);

  return ERR_NO_ERROR;
}


int
tsGetDBMTUserInfo (nvplist * req, nvplist * res, char *_dbmt_error)
{
  T_DBMT_USER dbmt_user;
  int retval;

  retval = _tsAppendDBList (res, 0);
  if (retval != ERR_NO_ERROR)
    {
      return retval;
    }
  retval = dbmt_user_read (&dbmt_user, _dbmt_error);
  if (retval != ERR_NO_ERROR)
    {
      return retval;
    }
  _tsAppendDBMTUserList (res, &dbmt_user, _dbmt_error);
  dbmt_user_free (&dbmt_user);

  return ERR_NO_ERROR;
}

int
tsCreateDB (nvplist * req, nvplist * res, char *_dbmt_error)
{
  int retval = ERR_NO_ERROR;
  char *dbname = NULL;
  char *numpage = NULL;
  char *pagesize = NULL;
  char *logpagesize = NULL;
  char *genvolpath = NULL;
  char *numlogpage = NULL;
  char *logvolpath = NULL, logvolpath_buf[1024];
  char *overwrite_config_file = NULL;
  char targetdir[PATH_MAX];
  char extvolfile[PATH_MAX] = "";
  char *cubrid_err_file;
  char *ip, *port;
  T_DBMT_USER dbmt_user;
  T_DBMT_CON_DBINFO con_dbinfo;
  char cmd_name[CUBRID_CMD_NAME_LEN];
  const char *argv[20];
  int argc = 0;

  int gen_dir_created, log_dir_created, ext_dir_created;

  gen_dir_created = log_dir_created = ext_dir_created = 0;

  if ((dbname = nv_get_val (req, "dbname")) == NULL)
    {
      strcpy (_dbmt_error, "database name");
      return ERR_PARAM_MISSING;
    }

  numpage = nv_get_val (req, "numpage");
  pagesize = nv_get_val (req, "pagesize");
  logpagesize = nv_get_val (req, "logpagesize");
  numlogpage = nv_get_val (req, "logsize");
  genvolpath = nv_get_val (req, "genvolpath");
  logvolpath = nv_get_val (req, "logvolpath");
  overwrite_config_file = nv_get_val (req, "overwrite_config_file");

  if (genvolpath == NULL)
    {
      strcpy (_dbmt_error, "volumn path");
      return ERR_PARAM_MISSING;
    }
  if ((retval = check_dbpath (genvolpath, _dbmt_error)) != ERR_NO_ERROR)
    {
      return retval;
    }

  if (logvolpath != NULL && logvolpath[0] == '\0')
    {
      logvolpath = NULL;
    }
  if (logvolpath != NULL
      && (retval = check_dbpath (logvolpath, _dbmt_error)) != ERR_NO_ERROR)
    {
      return retval;
    }

  /* create directory */
  strcpy (targetdir, genvolpath);
  if (access (genvolpath, F_OK) < 0)
    {
      retval = uCreateDir (genvolpath);
      if (retval != ERR_NO_ERROR)
	{
	  return retval;
	}
      else
	{
	  gen_dir_created = 1;
	}
    }
  if (logvolpath != NULL && access (logvolpath, F_OK) < 0)
    {
      retval = uCreateDir (logvolpath);
      if (retval != ERR_NO_ERROR)
	{
	  return retval;
	}
      else
	{
	  log_dir_created = 1;
	}
    }

  if (access (genvolpath, W_OK) < 0)
    {
      sprintf (_dbmt_error, "%s: %s\n", genvolpath, strerror (errno));
      return ERR_WITH_MSG;
    }
  if (logvolpath != NULL && access (logvolpath, W_OK) < 0)
    {
      sprintf (_dbmt_error, "%s: %s\n", genvolpath, strerror (errno));
      return ERR_WITH_MSG;
    }

  /* copy config file to the directory and update config file */
  if ((overwrite_config_file == NULL)	/* for backword compatibility */
      || (strcasecmp (overwrite_config_file, "NO") != 0))
    {
      char strbuf[1024];
      FILE *infile = NULL;
      FILE *outfile = NULL;
      char dstrbuf[PATH_MAX];

#if !defined (DO_NOT_USE_CUBRIDENV)
      snprintf (dstrbuf, PATH_MAX - 1, "%s/conf/%s", sco.szCubrid,
		CUBRID_CUBRID_CONF);
#else
      snprintf (dstrbuf, PATH_MAX - 1, "%s/%s", CUBRID_CONFDIR,
		CUBRID_CUBRID_CONF);
#endif
      infile = fopen (dstrbuf, "r");
      if (infile == NULL)
	{
	  strcpy (_dbmt_error, dstrbuf);
	  return ERR_FILE_OPEN_FAIL;
	}

      snprintf (dstrbuf, PATH_MAX - 1, "%s/%s", targetdir,
		CUBRID_CUBRID_CONF);
      outfile = fopen (dstrbuf, "w");
      if (outfile == NULL)
	{
	  fclose (infile);
	  strcpy (_dbmt_error, dstrbuf);
	  return ERR_FILE_OPEN_FAIL;
	}

      while (fgets (strbuf, sizeof (strbuf), infile))
	{
	  const char *param;

	  param = "data_buffer_pages";
	  if (!strncmp (strbuf, param, strlen (param))
	      && nv_get_val (req, param))
	    {
	      fprintf (outfile, "%s=%s\n", param, nv_get_val (req, param));
	      continue;
	    }

	  param = "media_failure_support";
	  if (!strncmp (strbuf, param, strlen (param))
	      && nv_get_val (req, param))
	    {
	      fprintf (outfile, "%s=%s\n", param, nv_get_val (req, param));
	      continue;
	    }

	  param = "max_clients";
	  if (!strncmp (strbuf, param, strlen (param))
	      && nv_get_val (req, param))
	    {
	      fprintf (outfile, "%s=%s\n", param, nv_get_val (req, param));
	      continue;
	    }

	  fputs (strbuf, outfile);
	}
      fclose (infile);
      fclose (outfile);
    }

  /* remove warning out of space message.
   * judge creation failed if created page size is smaller then
   * 343 page, write message with volumn expend to error file.
   */
  if (0)
    {
      FILE *infile = NULL;
      FILE *outfile = NULL;
      char oldfilename[PATH_MAX];
      char newfilename[PATH_MAX];
      memset (oldfilename, '\0', sizeof (oldfilename));
      memset (newfilename, '\0', sizeof (newfilename));

      snprintf (oldfilename, PATH_MAX - 1, "%s/%s", targetdir,
		CUBRID_CUBRID_CONF);
      infile = fopen (oldfilename, "r");
      if (infile == NULL)
	{
	  strcpy (_dbmt_error, oldfilename);
	  return ERR_FILE_OPEN_FAIL;
	}

      snprintf (newfilename, PATH_MAX - 1, "%s/tempcubrid.conf", targetdir);
      outfile = fopen (newfilename, "w");
      if (outfile == NULL)
	{
	  fclose (infile);
	  strcpy (_dbmt_error, newfilename);
	  return ERR_FILE_OPEN_FAIL;
	}

      fclose (infile);
      fclose (outfile);

      unlink (oldfilename);
      rename (newfilename, oldfilename);
    }

  /* construct spec file */
  if (1)
    {
      int pos, len, i;
      char *tn, *tv;
      FILE *outfile;
      char buf[1024], *val[3];
      char val2_buf[1024];

      snprintf (extvolfile, PATH_MAX - 1, "%s/extvol.spec", targetdir);
      outfile = fopen (extvolfile, "w");
      if (outfile == NULL)
	{
	  strcpy (_dbmt_error, extvolfile);
	  return ERR_FILE_OPEN_FAIL;
	}

      nv_locate (req, "exvol", &pos, &len);
      for (i = pos; i < len + pos; ++i)
	{
	  nv_lookup (req, i, &tn, &tv);
	  if (tv == NULL)
	    continue;
	  strcpy (buf, tv);
	  if (string_tokenize2 (buf, val, 3, ';') < 0)
	    {
	      continue;
	    }

#if defined(WINDOWS)
	  val[2] = nt_style_path (val[2], val2_buf);
#endif
	  fprintf (outfile, "NAME %s PATH %s PURPOSE %s NPAGES %s\n\n",
		   tn, val[2], val[0], val[1]);
	  /* create directory, if needed */
	  if (access (val[2], F_OK) < 0)
	    {
	      retval = uCreateDir (val[2]);
	      if (retval != ERR_NO_ERROR)
		{
		  return retval;
		}
	      else
		{
		  ext_dir_created = 1;
		}
	    }
	}
      fclose (outfile);
    }

  /* construct command */
  cubrid_cmd_name (cmd_name);
#if defined(WINDOWS)
  nt_style_path (targetdir, targetdir);
#endif
  argv[argc++] = cmd_name;
  argv[argc++] = UTIL_OPTION_CREATEDB;
  argv[argc++] = "--" CREATE_FILE_PATH_L;
  argv[argc++] = targetdir;

  if (numpage)
    {
      argv[argc++] = "--" CREATE_PAGES_L;
      argv[argc++] = numpage;
    }
  if (pagesize)
    {
      argv[argc++] = "--" CREATE_PAGE_SIZE_L;
      argv[argc++] = pagesize;
    }
  if (logpagesize)
    {
      argv[argc++] = "--" CREATE_LOG_PAGE_SIZE_L;
      argv[argc++] = logpagesize;
    }
  if (numlogpage)
    {
      argv[argc++] = "--" CREATE_LOG_PAGE_COUNT_L;
      argv[argc++] = numlogpage;
    }
  if (logvolpath)
    {
#if defined(WINDOWS)
      logvolpath = nt_style_path (logvolpath, logvolpath_buf);
      /*
         remove_end_of_dir_ch(logvolpath);
       */
#endif
      argv[argc++] = "--" CREATE_LOG_PATH_L;
      argv[argc++] = logvolpath;
    }
  if (extvolfile[0] != '\0')
    {
      argv[argc++] = "--" CREATE_MORE_VOLUME_FILE_L;
      argv[argc++] = extvolfile;
    }

  argv[argc++] = dbname;
  argv[argc++] = NULL;

  INIT_CUBRID_ERROR_FILE (cubrid_err_file);

  retval = run_child (argv, 1, NULL, NULL, cubrid_err_file, NULL);	/* createdb */

  if (read_error_file (cubrid_err_file, _dbmt_error, DBMT_ERROR_MSG_SIZE) < 0)
    {
      int pos, len, i;
      char *tn, *tv;
      char buf[1024], *val[3];

      if ((access (genvolpath, F_OK) == 0) && (gen_dir_created))
	uRemoveDir (genvolpath, REMOVE_DIR_FORCED);
      if ((logvolpath != NULL) && (access (logvolpath, F_OK) == 0)
	  && (log_dir_created))
	uRemoveDir (logvolpath, REMOVE_DIR_FORCED);

      nv_locate (req, "exvol", &pos, &len);
      for (i = pos; i < len + pos; ++i)
	{
	  nv_lookup (req, i, &tn, &tv);
	  if (tv == NULL)
	    {
	      continue;
	    }
	  strcpy (buf, tv);
	  if (string_tokenize2 (buf, val, 3, ';') < 0)
	    {
	      continue;
	    }
	  if ((access (val[2], F_OK) == 0) && (ext_dir_created))
	    uRemoveDir (val[2], REMOVE_DIR_FORCED);	/* ext vol path */
	}
      return ERR_WITH_MSG;
    }

  if (retval < 0)
    {
      int pos, len, i;
      char *tn, *tv;
      char buf[1024], *val[3];

      if (access (genvolpath, F_OK) == 0)
	{
	  uRemoveDir (genvolpath, REMOVE_DIR_FORCED);
	}
      if (logvolpath != NULL && access (logvolpath, F_OK) == 0)
	{
	  uRemoveDir (logvolpath, REMOVE_DIR_FORCED);
	}

      nv_locate (req, "exvol", &pos, &len);
      for (i = pos; i < len + pos; ++i)
	{
	  nv_lookup (req, i, &tn, &tv);
	  if (tv == NULL)
	    {
	      continue;
	    }
	  strcpy (buf, tv);
	  if (string_tokenize2 (buf, val, 3, ';') < 0)
	    {
	      continue;
	    }
	  if (access (val[2], F_OK) == 0)
	    uRemoveDir (val[2], REMOVE_DIR_FORCED);	/* ext vol path */
	}

      sprintf (_dbmt_error, "%s", argv[0]);
      return ERR_SYSTEM_CALL;
    }

  /* add dbinfo to cmdb.pass */
  if (dbmt_user_read (&dbmt_user, _dbmt_error) == ERR_NO_ERROR)
    {
      int i;
      T_DBMT_USER_DBINFO tmp_dbinfo;

      memset (&tmp_dbinfo, 0, sizeof (tmp_dbinfo));
      dbmt_user_set_dbinfo (&tmp_dbinfo, dbname, "admin", "dba", "", "");

      dbmt_user_db_delete (&dbmt_user, dbname);
      for (i = 0; i < dbmt_user.num_dbmt_user; i++)
	{
	  if (strcmp (dbmt_user.user_info[i].user_name, "admin") == 0)
	    {
	      if (dbmt_user_add_dbinfo
		  (&(dbmt_user.user_info[i]), &tmp_dbinfo) == ERR_NO_ERROR)
		{
		  dbmt_user_write_auth (&dbmt_user, _dbmt_error);
		}
	      break;
	    }
	}
      dbmt_user_free (&dbmt_user);
    }

  /* add dbinfo to conlist */
  memset (&con_dbinfo, 0, sizeof (con_dbinfo));
  dbmt_con_set_dbinfo (&con_dbinfo, dbname, "dba", "");
  ip = nv_get_val (req, "_IP");
  port = nv_get_val (req, "_PORT");
  dbmt_con_write_dbinfo (&con_dbinfo, ip, port, dbname, 1, _dbmt_error);

  /* restore warn out of space value */
  if (0)
    {
      char strbuf[1024];
      FILE *infile = NULL;
      FILE *outfile = NULL;
      char oldfilename[PATH_MAX];
      char newfilename[PATH_MAX];
      memset (oldfilename, '\0', sizeof (oldfilename));
      memset (newfilename, '\0', sizeof (newfilename));

      snprintf (oldfilename, PATH_MAX - 1, "%s/%s", targetdir,
		CUBRID_CUBRID_CONF);
      infile = fopen (oldfilename, "r");
      if (infile == NULL)
	{
	  strcpy (_dbmt_error, oldfilename);
	  return ERR_FILE_OPEN_FAIL;
	}

      snprintf (newfilename, PATH_MAX - 1, "%s/tempcubrid.conf", targetdir);
      outfile = fopen (newfilename, "w");
      if (outfile == NULL)
	{
	  fclose (infile);
	  strcpy (_dbmt_error, newfilename);
	  return ERR_FILE_OPEN_FAIL;
	}

      while (fgets (strbuf, sizeof (strbuf), infile))
	{
	  const char *p;
	  p = "warn_outofspace_factor";
	  if (!strncmp (strbuf, p, strlen (p)))
	    {
	      fprintf (outfile, "%s", p);
	      continue;
	    }

	  fputs (strbuf, outfile);
	}
      fclose (infile);
      fclose (outfile);

      unlink (oldfilename);
      rename (newfilename, oldfilename);
    }

  if ((overwrite_config_file == NULL)	/* for backward compatibility */
      || (strcasecmp (overwrite_config_file, "NO") != 0))
    {
      char strbuf[PATH_MAX];

      snprintf (strbuf, PATH_MAX - 1, "%s/%s", targetdir, CUBRID_CUBRID_CONF);
      unlink (strbuf);
    }

  if (extvolfile[0] != '\0')
    {
      unlink (extvolfile);
    }
  return ERR_NO_ERROR;
}

int
tsDeleteDB (nvplist * req, nvplist * res, char *_dbmt_error)
{
  T_DBMT_USER dbmt_user;
  int retval = ERR_NO_ERROR;
  char *dbname = NULL, *delbackup;
  char *cubrid_err_file;
  char cmd_name[CUBRID_CMD_NAME_LEN];
  const char *argv[6];
  int argc = 0;

  /*dbvol path & db log path. */
  char dbvolpath[PATH_MAX];
  char dblogpath[PATH_MAX];

  if ((dbname = nv_get_val (req, "_DBNAME")) == NULL)
    {
      sprintf (_dbmt_error, "%s", "database name");
      return ERR_PARAM_MISSING;
    }

  delbackup = nv_get_val (req, "delbackup");

  cubrid_cmd_name (cmd_name);
  argv[argc++] = cmd_name;
  argv[argc++] = UTIL_OPTION_DELETEDB;
  if (uStringEqual (delbackup, "y"))
    argv[argc++] = "--" DELETE_DELETE_BACKUP_L;
  argv[argc++] = dbname;
  argv[argc++] = NULL;
  INIT_CUBRID_ERROR_FILE (cubrid_err_file);

  /*get dbvolpath and dblogpath. */
  get_dbvoldir (dbvolpath, sizeof (dbvolpath), dbname, cubrid_err_file);
  get_dblogdir (dblogpath, sizeof (dblogpath), dbname, cubrid_err_file);

  retval = run_child (argv, 1, NULL, NULL, cubrid_err_file, NULL);	/* deletedb */

  if (read_error_file (cubrid_err_file, _dbmt_error, DBMT_ERROR_MSG_SIZE) < 0)
    {
      return ERR_WITH_MSG;
    }
  if (retval < 0)
    {
      strcpy (_dbmt_error, argv[0]);
      return ERR_SYSTEM_CALL;
    }

  auto_conf_addvol_delete (FID_AUTO_ADDVOLDB_CONF, dbname);
  auto_conf_backup_delete (FID_AUTO_BACKUPDB_CONF, dbname);
  auto_conf_history_delete (FID_AUTO_HISTORY_CONF, dbname);
  auto_conf_execquery_delete (FID_AUTO_EXECQUERY_CONF, dbname);

  if (dbmt_user_read (&dbmt_user, _dbmt_error) == ERR_NO_ERROR)
    {
      dbmt_user_db_delete (&dbmt_user, dbname);
      dbmt_user_write_auth (&dbmt_user, _dbmt_error);
      dbmt_user_free (&dbmt_user);
    }

  /* The following delete sequence can delete folder hierarchy like : 
   * <database log folder>/<database vol folder>
   * and <database vol folder>/<database log folder>.
   */
  rmdir (dbvolpath);
  rmdir (dblogpath);
  rmdir (dbvolpath);

  return ERR_NO_ERROR;
}

int
tsRenameDB (nvplist * req, nvplist * res, char *_dbmt_error)
{
  T_DBMT_USER dbmt_user;
  int retval;
  char *dbname = NULL;
  char *newdbname = NULL;
  char *exvolpath, *advanced, *forcedel;
  char tmpfile[PATH_MAX];
  char *cubrid_err_file;
  char cmd_name[CUBRID_CMD_NAME_LEN];
  const char *argv[10];
  int argc = 0;
  T_DB_SERVICE_MODE db_mode;

  if ((dbname = nv_get_val (req, "_DBNAME")) == NULL)
    {
      sprintf (_dbmt_error, "%s", "database name");
      return ERR_PARAM_MISSING;
    }

  db_mode = uDatabaseMode (dbname, NULL);
  if (db_mode == DB_SERVICE_MODE_SA)
    {
      sprintf (_dbmt_error, "%s", dbname);
      return ERR_STANDALONE_MODE;
    }
  else if (db_mode == DB_SERVICE_MODE_CS)
    {
      sprintf (_dbmt_error, "%s", dbname);
      return ERR_DB_ACTIVE;
    }

  if ((newdbname = nv_get_val (req, "rename")) == NULL)
    {
      sprintf (_dbmt_error, "%s", "new database name");
      return ERR_PARAM_MISSING;
    }

  exvolpath = nv_get_val (req, "exvolpath");
  advanced = nv_get_val (req, "advanced");
  forcedel = nv_get_val (req, "forcedel");

  cubrid_cmd_name (cmd_name);
  argv[argc++] = cmd_name;
  argv[argc++] = UTIL_OPTION_RENAMEDB;

  if (uStringEqual (advanced, "on"))
    {
      FILE *outfile;
      int i, flag = 0, line = 0;
      char *n, *v, n_buf[1024], v_buf[1024];
      char *p;

      snprintf (tmpfile, PATH_MAX - 1, "%s/DBMT_task_%d.%d", sco.dbmt_tmp_dir,
		TS_RENAMEDB, (int) getpid ());
      if ((outfile = fopen (tmpfile, "w")) == NULL)
	{
	  return ERR_TMPFILE_OPEN_FAIL;
	}
      for (i = 0; i < req->nvplist_leng; i++)
	{
	  nv_lookup (req, i, &n, &v);
	  if (n == NULL || v == NULL)
	    {
	      fclose (outfile);
	      if (v != NULL)
		{
		  strcpy (_dbmt_error, v);
		}
	      return ERR_DIR_CREATE_FAIL;
	    }

	  if (!strcmp (n, "open") && !strcmp (v, "volume"))
	    {
	      flag = 1;
	    }
	  else if (!strcmp (n, "close") && !strcmp (v, "volume"))
	    {
	      flag = 0;
	    }
	  else if (flag == 1)
	    {
#if defined(WINDOWS)
	      replace_colon (n);
	      replace_colon (v);
#endif
	      p = strrchr (v, '/');
	      if (p)
		*p = '\0';
	      if (uCreateDir (v) != ERR_NO_ERROR)
		{
		  fclose (outfile);
		  strcpy (_dbmt_error, v);
		  return ERR_DIR_CREATE_FAIL;
		}
	      if (p)
		*p = '/';
#if defined(WINDOWS)
	      n = nt_style_path (n, n_buf);
	      v = nt_style_path (v, v_buf);
#endif
	      fprintf (outfile, "%d %s %s\n", line++, n, v);

	    }			/* close "else if (flag == 1)" */
	}			/* close "for" loop */
      fclose (outfile);
      argv[argc++] = "--" RENAME_CONTROL_FILE_L;
      argv[argc++] = tmpfile;
    }				/* close "if (adv_flag != NULL)" */
  else if (exvolpath != NULL && !uStringEqual (exvolpath, "none"))
    {
      if (uCreateDir (exvolpath) != ERR_NO_ERROR)
	{
	  strcpy (_dbmt_error, exvolpath);
	  return ERR_DIR_CREATE_FAIL;
	}
      argv[argc++] = "--" RENAME_EXTENTED_VOLUME_PATH_L;
      argv[argc++] = exvolpath;
    }

  if (uStringEqual (forcedel, "y"))
    argv[argc++] = "--" RENAME_DELETE_BACKUP_L;

  argv[argc++] = dbname;
  argv[argc++] = newdbname;
  argv[argc++] = NULL;

  INIT_CUBRID_ERROR_FILE (cubrid_err_file);

  retval = run_child (argv, 1, NULL, NULL, cubrid_err_file, NULL);	/* renamedb */

  if (read_error_file (cubrid_err_file, _dbmt_error, DBMT_ERROR_MSG_SIZE) < 0)
    return ERR_WITH_MSG;
  if (retval < 0)
    {
      strcpy (_dbmt_error, argv[0]);
      return ERR_SYSTEM_CALL;
    }

  auto_conf_addvol_rename (FID_AUTO_ADDVOLDB_CONF, dbname, newdbname);
  auto_conf_backup_rename (FID_AUTO_BACKUPDB_CONF, dbname, newdbname);
  auto_conf_history_rename (FID_AUTO_HISTORY_CONF, dbname, newdbname);
  auto_conf_execquery_rename (FID_AUTO_EXECQUERY_CONF, dbname, newdbname);

  if (dbmt_user_read (&dbmt_user, _dbmt_error) == ERR_NO_ERROR)
    {
      int i, j;
      for (i = 0; i < dbmt_user.num_dbmt_user; i++)
	{
	  for (j = 0; j < dbmt_user.user_info[i].num_dbinfo; j++)
	    {
	      if (strcmp (dbmt_user.user_info[i].dbinfo[j].dbname, dbname) ==
		  0)
		{
		  strcpy (dbmt_user.user_info[i].dbinfo[j].dbname, newdbname);
		}
	    }
	}
      dbmt_user_write_auth (&dbmt_user, _dbmt_error);
      dbmt_user_free (&dbmt_user);
    }

  return ERR_NO_ERROR;
}

int
tsStartDB (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char *dbname;
  char err_buf[ERR_MSG_SIZE];
  T_DB_SERVICE_MODE db_mode;
  int retval;

  if ((dbname = nv_get_val (req, "_DBNAME")) == NULL)
    {
      sprintf (_dbmt_error, "%s", "database name");
      return ERR_PARAM_MISSING;
    }

  db_mode = uDatabaseMode (dbname, NULL);
  if (db_mode == DB_SERVICE_MODE_SA)
    {
      sprintf (_dbmt_error, "%s", dbname);
      return ERR_STANDALONE_MODE;
    }
  if (db_mode == DB_SERVICE_MODE_CS)
    {
      return ERR_NO_ERROR;
    }

  retval = cmd_start_server (dbname, err_buf, sizeof (err_buf));
  if (retval < 0)
    {
      DBMT_ERR_MSG_SET (_dbmt_error, err_buf);
      return ERR_WITH_MSG;
    }
  else if (retval == 1)
    {
      DBMT_ERR_MSG_SET (_dbmt_error, err_buf);
    }

  /* recount active db num and write to file */
  uWriteDBnfo ();

  return retval == 0 ? ERR_NO_ERROR : ERR_WARNING;
}

int
tsStopDB (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char *dbname;

  if ((dbname = nv_get_val (req, "_DBNAME")) == NULL)
    {
      sprintf (_dbmt_error, "%s", "database name");
      return ERR_PARAM_MISSING;
    }

  if (cmd_stop_server (dbname, _dbmt_error, DBMT_ERROR_MSG_SIZE) < 0)
    {
      return ERR_WITH_MSG;
    }

  /* recount active db num and write to file */
  uWriteDBnfo ();

  return ERR_NO_ERROR;
}

int
tsDbspaceInfo (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char *dbname = NULL;
  int retval = ERR_NO_ERROR;
  char dbname_at_hostname[MAXHOSTNAMELEN + DB_NAME_LEN];
  int ha_mode = 0;
  T_CUBRID_MODE cubrid_mode;
  T_SPACEDB_RESULT *cmd_res;
  T_DB_SERVICE_MODE db_mode;

  /* get dbname */
  if ((dbname = nv_get_val (req, "_DBNAME")) == NULL)
    {
      sprintf (_dbmt_error, "%s", "database name");
      return ERR_PARAM_MISSING;
    }

  db_mode = uDatabaseMode (dbname, &ha_mode);
  if (db_mode == DB_SERVICE_MODE_SA)
    {
      sprintf (_dbmt_error, "%s", dbname);
      return ERR_STANDALONE_MODE;
    }

  nv_add_nvp (res, "dbname", dbname);
  nv_add_nvp (res, "pagesize", "-1");
  nv_add_nvp (res, "logpagesize", "-1");

  cubrid_mode =
    (db_mode == DB_SERVICE_MODE_NONE) ? CUBRID_MODE_SA : CUBRID_MODE_CS;

  if (ha_mode != 0)
    {
      append_host_to_dbname (dbname_at_hostname, dbname,
			     sizeof (dbname_at_hostname));
      cmd_res = cmd_spacedb (dbname_at_hostname, cubrid_mode);
    }
  else
    {
      cmd_res = cmd_spacedb (dbname, cubrid_mode);
    }

  if (cmd_res == NULL)
    {
      sprintf (_dbmt_error, "spacedb %s", dbname);
      retval = ERR_SYSTEM_CALL;
    }
  else if (cmd_res->err_msg[0])
    {
      strcpy (_dbmt_error, cmd_res->err_msg);
      retval = ERR_WITH_MSG;
    }
  else
    {
      retval = _tsParseSpacedb (req, res, dbname, _dbmt_error, cmd_res);
    }
  cmd_spacedb_result_free (cmd_res);

  return retval;
}

int
tsRunAddvoldb (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char *dbname;
  char dbname_at_hostname[MAXHOSTNAMELEN + DB_NAME_LEN];
  int ha_mode = 0;
  char *numpage;
  char *volpu;
  char *volpath;
#if defined(WINDOWS)
  char volpath_buf[PATH_MAX];
#endif
  char *volname;
  char *size_need_mb;
  char db_dir[PATH_MAX];
  T_DB_SERVICE_MODE db_mode;
  char *err_file;
  int ret;
  char cmd_name[CUBRID_CMD_NAME_LEN];
  const char *argv[15];
  int argc = 0;
  int free_space_mb;

  if ((dbname = nv_get_val (req, "_DBNAME")) == NULL)
    {
      sprintf (_dbmt_error, "%s", "database name");
      return ERR_PARAM_MISSING;
    }

  db_mode = uDatabaseMode (dbname, &ha_mode);
  if (db_mode == DB_SERVICE_MODE_SA)
    {
      sprintf (_dbmt_error, "%s", dbname);
      return ERR_STANDALONE_MODE;
    }

  if ((numpage = nv_get_val (req, "numberofpages")) == NULL)
    {
      sprintf (_dbmt_error, "%s", "number of pages");
      return ERR_PARAM_MISSING;
    }
  if ((volpu = nv_get_val (req, "purpose")) == NULL)
    {
      sprintf (_dbmt_error, "%s", "purpose");
      return ERR_PARAM_MISSING;
    }

  volpath = nv_get_val (req, "path");
  volname = nv_get_val (req, "volname");
  size_need_mb = nv_get_val (req, "size_need_mb");

  if (uRetrieveDBDirectory (dbname, db_dir) != ERR_NO_ERROR)
    {
      strcpy (_dbmt_error, dbname);
      return ERR_DBDIRNAME_NULL;
    }

  /* check permission of the directory */
  if (access (db_dir, W_OK | X_OK | R_OK) < 0)
    {
      sprintf (_dbmt_error, "%s", db_dir);
      return ERR_PERMISSION;
    }

  if (volpath == NULL)
    volpath = db_dir;

  if (access (volpath, F_OK) < 0)
    {
      if (uCreateDir (volpath) != ERR_NO_ERROR)
	{
	  sprintf (_dbmt_error, "%s", volpath);
	  return ERR_DIR_CREATE_FAIL;
	}
    }

  free_space_mb = ut_disk_free_space (volpath);
  if (size_need_mb && (free_space_mb < atoi (size_need_mb)))
    {
      sprintf (_dbmt_error, "Not enough dist free space");
      return ERR_WITH_MSG;
    }

  cubrid_cmd_name (cmd_name);
  argv[argc++] = cmd_name;
  argv[argc++] = UTIL_OPTION_ADDVOLDB;

  if (db_mode == DB_SERVICE_MODE_NONE)
    argv[argc++] = "--" ADDVOL_SA_MODE_L;

#if defined(WINDOWS)
  volpath = nt_style_path (volpath, volpath_buf);
#endif
  argv[argc++] = "--" ADDVOL_FILE_PATH_L;
  argv[argc++] = volpath;

  if (volname)
    {
      argv[argc++] = "--" ADDVOL_VOLUME_NAME_L;
      argv[argc++] = volname;
    }

  argv[argc++] = "--" ADDVOL_PURPOSE_L;
  argv[argc++] = volpu;

  if (ha_mode != 0)
    {
      append_host_to_dbname (dbname_at_hostname, dbname,
			     sizeof (dbname_at_hostname));
      argv[argc++] = dbname_at_hostname;
    }
  else
    {
      argv[argc++] = dbname;
    }

  argv[argc++] = numpage;
  argv[argc++] = NULL;

  INIT_CUBRID_ERROR_FILE (err_file);

  ret = run_child (argv, 1, NULL, NULL, err_file, NULL);	/* addvoldb */
  if (read_error_file (err_file, _dbmt_error, DBMT_ERROR_MSG_SIZE) < 0)
    return ERR_WITH_MSG;
  if (ret < 0)
    {
      sprintf (_dbmt_error, "%s", argv[0]);
      return ERR_SYSTEM_CALL;
    }

  nv_add_nvp (res, "dbname", dbname);
  nv_add_nvp (res, "purpose", volpu);

  return ERR_NO_ERROR;
}

int
ts_copydb (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char *srcdbname, *destdbname, *logpath, *destdbpath, *exvolpath;
  char move_flag, overwrite_flag, adv_flag;
  char tmpfile[PATH_MAX];
  char src_conf_file[PATH_MAX], dest_conf_file[PATH_MAX], conf_dir[PATH_MAX];
  int i, retval;
  char *cubrid_err_file;
  T_DBMT_USER dbmt_user;
  char cmd_name[CUBRID_CMD_NAME_LEN];
  T_DB_SERVICE_MODE db_mode;
  const char *argv[15];
  int argc = 0;

  if ((srcdbname = nv_get_val (req, "srcdbname")) == NULL)
    {
      strcpy (_dbmt_error, "source database name");
      return ERR_PARAM_MISSING;
    }
  if ((destdbname = nv_get_val (req, "destdbname")) == NULL)
    {
      strcpy (_dbmt_error, "destination database name");
      return ERR_PARAM_MISSING;
    }


  adv_flag = uStringEqual (nv_get_val (req, "advanced"), "on") ? 1 : 0;
  overwrite_flag = uStringEqual (nv_get_val (req, "overwrite"), "y") ? 1 : 0;
  move_flag = uStringEqual (nv_get_val (req, "move"), "y") ? 1 : 0;
  if ((logpath = nv_get_val (req, "logpath")) == NULL)
    {
      strcpy (_dbmt_error, "log path");
      return ERR_PARAM_MISSING;
    }
  if ((destdbpath = nv_get_val (req, "destdbpath")) == NULL && adv_flag == 0)
    {
      strcpy (_dbmt_error, "database directory path");
      return ERR_PARAM_MISSING;
    }
  if ((exvolpath = nv_get_val (req, "exvolpath")) == NULL && adv_flag == 0)
    {
      strcpy (_dbmt_error, "extended volume path");
      return ERR_PARAM_MISSING;
    }

  db_mode = uDatabaseMode (srcdbname, NULL);
  if (db_mode == DB_SERVICE_MODE_SA)
    {
      sprintf (_dbmt_error, "%s", srcdbname);
      return ERR_STANDALONE_MODE;
    }
  else if (db_mode == DB_SERVICE_MODE_CS)
    {
      sprintf (_dbmt_error, "%s", srcdbname);
      return ERR_DB_ACTIVE;
    }

  /* create command */
  cubrid_cmd_name (cmd_name);
  argv[argc++] = cmd_name;
  argv[argc++] = UTIL_OPTION_COPYDB;
  argv[argc++] = "--" COPY_LOG_PATH_L;
  argv[argc++] = logpath;

  if (adv_flag)
    {
      FILE *outfile;
      int flag = 0, line = 0;
      char *n, *v, n_buf[1024], v_buf[1024];
      char *p;

      snprintf (tmpfile, PATH_MAX - 1, "%s/DBMT_task_%d.%d", sco.dbmt_tmp_dir,
		TS_COPYDB, (int) getpid ());
      if ((outfile = fopen (tmpfile, "w")) == NULL)
	{
	  return ERR_TMPFILE_OPEN_FAIL;
	}
      for (i = 0; i < req->nvplist_leng; i++)
	{
	  nv_lookup (req, i, &n, &v);
	  if (n == NULL || v == NULL)
	    {
	      fclose (outfile);
	      if (v != NULL)
		{
		  strcpy (_dbmt_error, v);
		}
	      return ERR_DIR_CREATE_FAIL;
	    }

	  if (!strcmp (n, "open") && !strcmp (v, "volume"))
	    flag = 1;
	  else if (!strcmp (n, "close") && !strcmp (v, "volume"))
	    flag = 0;
	  else if (flag == 1)
	    {
#if defined(WINDOWS)
	      replace_colon (n);
	      replace_colon (v);
#endif
	      p = strrchr (v, '/');
	      if (p)
		*p = '\0';
	      if (uCreateDir (v) != ERR_NO_ERROR)
		{
		  fclose (outfile);
		  strcpy (_dbmt_error, v);
		  return ERR_DIR_CREATE_FAIL;
		}
	      if (p)
		*p = '/';
#if defined(WINDOWS)
	      n = nt_style_path (n, n_buf);
	      v = nt_style_path (v, v_buf);
#endif
	      fprintf (outfile, "%d %s %s\n", line++, n, v);
	    }
	}
      fclose (outfile);
      argv[argc++] = "--" COPY_CONTROL_FILE_L;
      argv[argc++] = tmpfile;
    }
  else
    {				/* adv_flag == 0 */
      if (uCreateDir (destdbpath) != ERR_NO_ERROR)
	{
	  strcpy (_dbmt_error, destdbpath);
	  return ERR_DIR_CREATE_FAIL;
	}
      if (uCreateDir (exvolpath) != ERR_NO_ERROR)
	{
	  strcpy (_dbmt_error, exvolpath);
	  return ERR_DIR_CREATE_FAIL;
	}
      argv[argc++] = "--" COPY_FILE_PATH_L;
      argv[argc++] = destdbpath;
      argv[argc++] = "--" COPY_EXTENTED_VOLUME_PATH_L;
      argv[argc++] = exvolpath;
    }
  if (overwrite_flag)
    argv[argc++] = "--" COPY_REPLACE_L;
  argv[argc++] = srcdbname;
  argv[argc++] = destdbname;
  argv[argc++] = NULL;

  if (uCreateDir (logpath) != ERR_NO_ERROR)
    {
      strcpy (_dbmt_error, logpath);
      return ERR_DIR_CREATE_FAIL;
    }

  INIT_CUBRID_ERROR_FILE (cubrid_err_file);

  retval = run_child (argv, 1, NULL, NULL, cubrid_err_file, NULL);	/* copydb */
  if (adv_flag)
    unlink (tmpfile);
  if (read_error_file (cubrid_err_file, _dbmt_error, DBMT_ERROR_MSG_SIZE) < 0)
    return ERR_WITH_MSG;
  if (retval < 0)
    {
      strcpy (_dbmt_error, argv[0]);
      return ERR_SYSTEM_CALL;
    }

  /* copy config file */
  if (uRetrieveDBDirectory (srcdbname, conf_dir) != ERR_NO_ERROR)
    {
      strcpy (_dbmt_error, srcdbname);
      return ERR_DBDIRNAME_NULL;
    }
  snprintf (src_conf_file, sizeof (src_conf_file) - 1, "%s/%s", conf_dir,
	    CUBRID_CUBRID_CONF);

  if (uRetrieveDBDirectory (destdbname, conf_dir) != ERR_NO_ERROR)
    {
      strcpy (_dbmt_error, destdbname);
      return ERR_DBDIRNAME_NULL;
    }
  snprintf (dest_conf_file, sizeof (dest_conf_file) - 1, "%s/%s", conf_dir,
	    CUBRID_CUBRID_CONF);

  /* Doesn't copy if src and desc is same */
  if (strcmp (src_conf_file, dest_conf_file) != 0)
    file_copy (src_conf_file, dest_conf_file);

  /* if move, delete exist database */
  if (move_flag)
    {
      char cmd_name[CUBRID_CMD_NAME_LEN];
      const char *argv[5];

      cubrid_cmd_name (cmd_name);
      argv[0] = cmd_name;
      argv[1] = UTIL_OPTION_DELETEDB;
      argv[2] = srcdbname;
      argv[3] = NULL;
      retval = run_child (argv, 1, NULL, NULL, NULL, NULL);	/* deletedb */
      if (retval < 0)
	{
	  strcpy (_dbmt_error, argv[0]);
	  return ERR_SYSTEM_CALL;
	}
    }

  /* cmdb.pass update after delete */
  if (dbmt_user_read (&dbmt_user, _dbmt_error) != ERR_NO_ERROR)
    {
      goto copydb_finale;
    }

  dbmt_user_db_delete (&dbmt_user, destdbname);
  for (i = 0; i < dbmt_user.num_dbmt_user; i++)
    {
      int dbinfo_idx;
      T_DBMT_USER_DBINFO tmp_info;

      dbinfo_idx = dbmt_user_search (&(dbmt_user.user_info[i]), srcdbname);
      if (dbinfo_idx < 0)
	continue;
      tmp_info = dbmt_user.user_info[i].dbinfo[dbinfo_idx];
      strcpy (tmp_info.dbname, destdbname);
      if (dbmt_user_add_dbinfo (&(dbmt_user.user_info[i]), &tmp_info) !=
	  ERR_NO_ERROR)
	{
	  dbmt_user_free (&dbmt_user);
	  goto copydb_finale;
	}

    }
  if (move_flag)
    {
      dbmt_user_db_delete (&dbmt_user, srcdbname);
    }
  dbmt_user_write_auth (&dbmt_user, _dbmt_error);
  dbmt_user_free (&dbmt_user);

copydb_finale:
  return ERR_NO_ERROR;
}

int
ts_plandump (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char *dbname = NULL;
  char dbname_at_hostname[MAXHOSTNAMELEN + DB_NAME_LEN];
  int ha_mode = 0;
  T_DB_SERVICE_MODE db_mode;
  char cmd_name[CUBRID_CMD_NAME_LEN];
  char *plandrop = NULL;
  const char *argv[10];
  int argc = 0;
  char *cubrid_err_file;
  FILE *infile = NULL;
  char tmpfilepath[PATH_MAX];
  int retval = ERR_NO_ERROR;

  if ((dbname = nv_get_val (req, "dbname")) == NULL)
    {
      strcpy (_dbmt_error, "database name");
      return ERR_PARAM_MISSING;
    }
  plandrop = nv_get_val (req, "plandrop");

  /*
   * check the running mode of current database,
   * return error if it is DB_SERVICE_MODE_SA.
   */
  db_mode = uDatabaseMode (dbname, &ha_mode);
  if (db_mode == DB_SERVICE_MODE_SA)
    {
      sprintf (_dbmt_error, "%s", dbname);
      return ERR_STANDALONE_MODE;
    }

  cubrid_cmd_name (cmd_name);
  argv[argc++] = cmd_name;
  argv[argc++] = UTIL_OPTION_PLANDUMP;
  if (uStringEqual (plandrop, "y"))
    {
      argv[argc++] = "--" PLANDUMP_DROP_L;
    }

  if (ha_mode != 0)
    {
      append_host_to_dbname (dbname_at_hostname, dbname,
			     sizeof (dbname_at_hostname));
      argv[argc++] = dbname_at_hostname;
    }
  else
    {
      argv[argc++] = dbname;
    }

  argv[argc++] = NULL;

  INIT_CUBRID_ERROR_FILE (cubrid_err_file);

  /*
   * create a new tmp file to record the content 
   * that returned by plandump. 
   */
  snprintf (tmpfilepath, PATH_MAX - 1, "%s/DBMT_task_%d.%d",
	    sco.dbmt_tmp_dir, TS_PLANDUMP, (int) getpid ());
  if ((infile = fopen (tmpfilepath, "w")) == NULL)
    {
      return ERR_TMPFILE_OPEN_FAIL;
    }
  fclose (infile);

  if (run_child (argv, 1, NULL, tmpfilepath, cubrid_err_file, NULL) < 0)	/* plandump */
    {
      strcpy (_dbmt_error, argv[0]);
      retval = ERR_SYSTEM_CALL;
      goto rm_tmpfile;
    }

  if ((infile = fopen (tmpfilepath, "r")) == NULL)
    {
      retval = ERR_TMPFILE_OPEN_FAIL;
      goto rm_tmpfile;
    }

  /* add file content to response line by line. */
  file_to_nvpairs (infile, res);

  /* close tmp file. */
  fclose (infile);

  if (read_error_file (cubrid_err_file, _dbmt_error, DBMT_ERROR_MSG_SIZE))
    {
      retval = ERR_WITH_MSG;
    }

rm_tmpfile:
  unlink (tmpfilepath);
  return retval;
}

int
ts_paramdump (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char *dbname = NULL;
  char dbname_at_hostname[MAXHOSTNAMELEN + DB_NAME_LEN];
  int ha_mode = 0;
  char *bothclientserver = NULL;
  T_DB_SERVICE_MODE db_mode;
  char cmd_name[CUBRID_CMD_NAME_LEN];
  const char *argv[10];
  int argc = 0;
  char *cubrid_err_file;
  FILE *infile = NULL;
  char tmpfilepath[PATH_MAX];
  int retval = ERR_NO_ERROR;

  if ((dbname = nv_get_val (req, "dbname")) == NULL)
    {
      strcpy (_dbmt_error, "database name");
      return ERR_PARAM_MISSING;
    }

  /* add  both & SA & CS mode. */
  bothclientserver = nv_get_val (req, "both");

  /*
   * check the running mode of current database,
   * return error if it is DB_SERVICE_MODE_SA.
   */
  db_mode = uDatabaseMode (dbname, &ha_mode);
  if (db_mode == DB_SERVICE_MODE_SA)
    {
      sprintf (_dbmt_error, "%s", dbname);
      return ERR_STANDALONE_MODE;
    }

  cubrid_cmd_name (cmd_name);
  argv[argc++] = cmd_name;
  argv[argc++] = UTIL_OPTION_PARAMDUMP;
  if (db_mode == DB_SERVICE_MODE_NONE)
    {
      argv[argc++] = "--" PARAMDUMP_SA_MODE_L;
    }
  else
    {
      argv[argc++] = "--" PARAMDUMP_CS_MODE_L;
    }

  if (uStringEqual (bothclientserver, "y"))
    {
      argv[argc++] = "--" PARAMDUMP_BOTH_L;
    }

  if (ha_mode != 0)
    {
      append_host_to_dbname (dbname_at_hostname, dbname,
			     sizeof (dbname_at_hostname));
      argv[argc++] = dbname_at_hostname;
    }
  else
    {
      argv[argc++] = dbname;
    }

  argv[argc++] = NULL;

  INIT_CUBRID_ERROR_FILE (cubrid_err_file);

  /*
   * create a new tmp file to record the content 
   * that returned by plandump. 
   */
  snprintf (tmpfilepath, PATH_MAX - 1, "%s/DBMT_task_%d.%d",
	    sco.dbmt_tmp_dir, TS_PARAMDUMP, (int) getpid ());
  if ((infile = fopen (tmpfilepath, "w")) == NULL)
    {
      return ERR_TMPFILE_OPEN_FAIL;
    }
  fclose (infile);

  if (run_child (argv, 1, NULL, tmpfilepath, cubrid_err_file, NULL) < 0)	/* paramdump */
    {
      strcpy (_dbmt_error, argv[0]);
      retval = ERR_SYSTEM_CALL;
      goto rm_tmpfile;
    }

  /* add dbname to response. */
  nv_add_nvp (res, "dbname", dbname);

  if ((infile = fopen (tmpfilepath, "r")) == NULL)
    {
      retval = ERR_TMPFILE_OPEN_FAIL;
      goto rm_tmpfile;
    }

  /* add file content to response line by line. */
  if (file_to_nvp_by_separator (infile, res, '=') < 0)
    {
      const char *tmperr = "Can't parse tmpfile of paramdump.";
      strncpy (_dbmt_error, tmperr, strlen (tmperr));
      retval = ERR_WITH_MSG;
      goto rm_tmpfile;
    }
  /* close tmp file. */
  fclose (infile);

  if (read_error_file (cubrid_err_file, _dbmt_error, DBMT_ERROR_MSG_SIZE))
    {
      retval = ERR_WITH_MSG;
    }

rm_tmpfile:
  unlink (tmpfilepath);
  return retval;
}

int
ts_optimizedb (nvplist * req, nvplist * res, char *_dbmt_error)
{
  return cm_ts_optimizedb (req, res, _dbmt_error);
}

int
ts_checkdb (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char *dbname, *repair_db;
  char dbname_at_hostname[MAXHOSTNAMELEN + DB_NAME_LEN];
  int ha_mode = 0;
  char cmd_name[CUBRID_CMD_NAME_LEN];
  T_DB_SERVICE_MODE db_mode;
  const char *argv[7];
  int argc = 0;
  char *cubrid_err_file;

  repair_db = nv_get_val (req, "repairdb");

  if ((dbname = nv_get_val (req, "dbname")) == NULL)
    {
      strcpy (_dbmt_error, "database name");
      return ERR_PARAM_MISSING;
    }

  db_mode = uDatabaseMode (dbname, &ha_mode);
  if (db_mode == DB_SERVICE_MODE_SA)
    {
      sprintf (_dbmt_error, "%s", dbname);
      return ERR_STANDALONE_MODE;
    }

  cubrid_cmd_name (cmd_name);
  argv[argc++] = cmd_name;
  argv[argc++] = UTIL_OPTION_CHECKDB;
  if (db_mode == DB_SERVICE_MODE_NONE)
    argv[argc++] = "--" CHECK_SA_MODE_L;
  else
    argv[argc++] = "--" CHECK_CS_MODE_L;

  if (repair_db != NULL && uStringEqual (repair_db, "y"))
    argv[argc++] = "--" CHECK_REPAIR_L;

  if (ha_mode != 0)
    {
      append_host_to_dbname (dbname_at_hostname, dbname,
			     sizeof (dbname_at_hostname));
      argv[argc++] = dbname_at_hostname;
    }
  else
    {
      argv[argc++] = dbname;
    }

  argv[argc++] = NULL;

  INIT_CUBRID_ERROR_FILE (cubrid_err_file);

  if (run_child (argv, 1, NULL, NULL, cubrid_err_file, NULL) < 0)
    {				/* checkdb */
      strcpy (_dbmt_error, argv[0]);
      return ERR_SYSTEM_CALL;
    }

  if (read_error_file (cubrid_err_file, _dbmt_error, DBMT_ERROR_MSG_SIZE) < 0)
    return ERR_WITH_MSG;

  return ERR_NO_ERROR;
}

int
ts_statdump (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char *dbname;
  T_CM_DB_EXEC_STAT exec_stat;
  T_CM_ERROR err_buf;
  int retval = -1;

  memset (&exec_stat, 0, sizeof (T_CM_DB_EXEC_STAT));
  memset (&err_buf, 0, sizeof (T_CM_ERROR));

  /* check the parameters of input. */
  if ((dbname = nv_get_val (req, "dbname")) == NULL)
    {
      strcpy (_dbmt_error, "dbname");
      retval = ERR_PARAM_MISSING;
      goto statdump_finale;
    }

  /* call cm_get_db_exec_stat to get stat infomation. */
  if (cm_get_db_exec_stat (dbname, &exec_stat, &err_buf) < 0)
    {
      /* return error with message if the operation is not success. */
      strcpy (_dbmt_error, err_buf.err_msg);
      retval = ERR_WITH_MSG;
      goto statdump_finale;
    }

  /* set res with parameter in exec_stat. */
  nv_add_nvp (res, "dbname", dbname);
  nv_add_nvp_time (res, "time", time (NULL),
		   "%04d/%02d/%02d %02d:%02d:%02d", TIME_STR_FMT_DATE_TIME);

  /* Execution statistics for the file io */
  nv_add_nvp_int (res, "num_file_creates", exec_stat.file_num_creates);
  nv_add_nvp_int (res, "num_file_removes", exec_stat.file_num_removes);
  nv_add_nvp_int (res, "num_file_ioreads", exec_stat.file_num_ioreads);
  nv_add_nvp_int (res, "num_file_iowrites", exec_stat.file_num_iowrites);
  nv_add_nvp_int (res, "num_file_iosynches", exec_stat.file_num_iosynches);

  /* Execution statistics for the page buffer manager */
  nv_add_nvp_int (res, "num_data_page_fetches", exec_stat.pb_num_fetches);
  nv_add_nvp_int (res, "num_data_page_dirties", exec_stat.pb_num_dirties);
  nv_add_nvp_int (res, "num_data_page_ioreads", exec_stat.pb_num_ioreads);
  nv_add_nvp_int (res, "num_data_page_iowrites", exec_stat.pb_num_iowrites);
  nv_add_nvp_int (res, "num_data_page_victims", exec_stat.pb_num_victims);
  nv_add_nvp_int (res, "num_data_page_iowrites_for_replacement",
		  exec_stat.pb_num_replacements);

  /* Execution statistics for the log manager */
  nv_add_nvp_int (res, "num_log_page_ioreads", exec_stat.log_num_ioreads);
  nv_add_nvp_int (res, "num_log_page_iowrites", exec_stat.log_num_iowrites);
  nv_add_nvp_int (res, "num_log_append_records",
		  exec_stat.log_num_appendrecs);
  nv_add_nvp_int (res, "num_log_archives", exec_stat.log_num_archives);
  nv_add_nvp_int (res, "num_log_checkpoints", exec_stat.log_num_checkpoints);
  nv_add_nvp_int (res, "num_log_wals", exec_stat.log_num_wals);

  /* Execution statistics for the lock manager */
  nv_add_nvp_int (res, "num_page_locks_acquired",
		  exec_stat.lk_num_acquired_on_pages);
  nv_add_nvp_int (res, "num_object_locks_acquired",
		  exec_stat.lk_num_acquired_on_objects);
  nv_add_nvp_int (res, "num_page_locks_converted",
		  exec_stat.lk_num_converted_on_pages);
  nv_add_nvp_int (res, "num_object_locks_converted",
		  exec_stat.lk_num_converted_on_objects);
  nv_add_nvp_int (res, "num_page_locks_re_requested",
		  exec_stat.lk_num_re_requested_on_pages);
  nv_add_nvp_int (res, "num_object_locks_re_requested",
		  exec_stat.lk_num_re_requested_on_objects);
  nv_add_nvp_int (res, "num_page_locks_waits",
		  exec_stat.lk_num_waited_on_pages);
  nv_add_nvp_int (res, "num_object_locks_waits",
		  exec_stat.lk_num_waited_on_objects);

  /* Execution statistics for transactions */
  nv_add_nvp_int (res, "num_tran_commits", exec_stat.tran_num_commits);
  nv_add_nvp_int (res, "num_tran_rollbacks", exec_stat.tran_num_rollbacks);
  nv_add_nvp_int (res, "num_tran_savepoints", exec_stat.tran_num_savepoints);
  nv_add_nvp_int (res, "num_tran_start_topops",
		  exec_stat.tran_num_start_topops);
  nv_add_nvp_int (res, "num_tran_end_topops", exec_stat.tran_num_end_topops);
  nv_add_nvp_int (res, "num_tran_interrupts", exec_stat.tran_num_interrupts);

  /* Execution statistics for the btree manager */
  nv_add_nvp_int (res, "num_btree_inserts", exec_stat.bt_num_inserts);
  nv_add_nvp_int (res, "num_btree_deletes", exec_stat.bt_num_deletes);
  nv_add_nvp_int (res, "num_btree_updates", exec_stat.bt_num_updates);

  /* Execution statistics for the query manager */
  nv_add_nvp_int (res, "num_query_selects", exec_stat.qm_num_selects);
  nv_add_nvp_int (res, "num_query_inserts", exec_stat.qm_num_inserts);
  nv_add_nvp_int (res, "num_query_deletes", exec_stat.qm_num_deletes);
  nv_add_nvp_int (res, "num_query_updates", exec_stat.qm_num_updates);
  nv_add_nvp_int (res, "num_query_sscans", exec_stat.qm_num_sscans);
  nv_add_nvp_int (res, "num_query_iscans", exec_stat.qm_num_iscans);
  nv_add_nvp_int (res, "num_query_lscans", exec_stat.qm_num_lscans);
  nv_add_nvp_int (res, "num_query_setscans", exec_stat.qm_num_setscans);
  nv_add_nvp_int (res, "num_query_methscans", exec_stat.qm_num_methscans);
  nv_add_nvp_int (res, "num_query_nljoins", exec_stat.qm_num_nljoins);
  nv_add_nvp_int (res, "num_query_mjoins", exec_stat.qm_num_mjoins);
  nv_add_nvp_int (res, "num_query_objfetches", exec_stat.qm_num_objfetches);

  /* Execution statistics for network communication */
  nv_add_nvp_int (res, "num_network_requests", exec_stat.net_num_requests);

  /* Other statistics */
  nv_add_nvp_int (res, "data_page_buffer_hit_ratio", exec_stat.pb_hit_ratio);

  /* flush control stat */
  nv_add_nvp_int (res, "num_adaptive_flush_pages", exec_stat.fc_num_pages);
  nv_add_nvp_int (res, "num_adaptive_flush_log_pages",
		  exec_stat.fc_num_log_pages);
  nv_add_nvp_int (res, "num_adaptive_flush_max_pages", exec_stat.fc_tokens);

  retval = ERR_NO_ERROR;

statdump_finale:
  return retval;
}

int
ts_compactdb (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char *dbname;
  char *verbose;
  char cmd_name[CUBRID_CMD_NAME_LEN];
  const char *argv[6];
  int argc = 0;
  T_DB_SERVICE_MODE db_mode;
  char *cubrid_err_file;
  char compactfilepath[PATH_MAX];
  int retval = ERR_NO_ERROR;
  FILE *infile;
  int createtmpfile = 0;
  dbname = nv_get_val (req, "dbname");
  verbose = nv_get_val (req, "verbose");

  db_mode = uDatabaseMode (dbname, NULL);
  if (db_mode == DB_SERVICE_MODE_SA)
    {
      sprintf (_dbmt_error, "%s", dbname);
      return ERR_STANDALONE_MODE;
    }
  else if (db_mode == DB_SERVICE_MODE_CS)
    {
      sprintf (_dbmt_error, "%s", dbname);
      return ERR_DB_ACTIVE;
    }

  cubrid_cmd_name (cmd_name);
  argv[argc++] = cmd_name;
  argv[argc++] = UTIL_OPTION_COMPACTDB;
  if (uStringEqual (verbose, "y"))
    {
      argv[argc++] = "--" COMPACT_VERBOSE_L;
      createtmpfile = 1;
    }
  argv[argc++] = dbname;
  argv[argc++] = NULL;

  INIT_CUBRID_ERROR_FILE (cubrid_err_file);

  if (createtmpfile != 0)
    {
      snprintf (compactfilepath, PATH_MAX - 1, "%s/DBMT_task_%d.%d",
		sco.dbmt_tmp_dir, TS_COMPACTDB, (int) getpid ());
      /* Create a tmp file. */
      infile = fopen (compactfilepath, "w");
      if (infile == NULL)
	{
	  return ERR_TMPFILE_OPEN_FAIL;
	}
      fclose (infile);
    }

  if (run_child
      (argv, 1, NULL, ((createtmpfile != 0) ? compactfilepath : NULL),
       cubrid_err_file, NULL) < 0)
    {				/* compactdb */
      strcpy (_dbmt_error, argv[0]);
      retval = ERR_SYSTEM_CALL;
      goto rm_tmpfile;
    }

  if (createtmpfile != 0)
    {
      /* open tmp file. */
      infile = fopen (compactfilepath, "r");
      if (infile == NULL)
	{
	  retval = ERR_TMPFILE_OPEN_FAIL;
	  goto rm_tmpfile;
	}

      /* add file content to response line by line. */
      file_to_nvpairs (infile, res);

      /* close tmp file. */
      fclose (infile);
    }
  if (read_error_file (cubrid_err_file, _dbmt_error, DBMT_ERROR_MSG_SIZE) < 0)
    {
      retval = ERR_WITH_MSG;
      goto rm_tmpfile;
    }

rm_tmpfile:
  unlink (compactfilepath);
  return retval;
}

int
ts_backupdb (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char *dbname, *level, *removelog, *volname, *backupdir, *check;
  char dbname_at_hostname[MAXHOSTNAMELEN + DB_NAME_LEN];
  int ha_mode = 0;
  char *mt, *zip, *safe_replication;
  char backupfilepath[PATH_MAX];
  char inputfilepath[PATH_MAX];
  char cmd_name[CUBRID_CMD_NAME_LEN];
  char sp_option[256];
  const char *argv[16];
  int argc = 0;
  FILE *inputfile;
  T_DB_SERVICE_MODE db_mode;
  char *cubrid_err_file;

  if ((dbname = nv_get_val (req, "dbname")) == NULL)
    {
      strcpy (_dbmt_error, "database name");
      return ERR_PARAM_MISSING;
    }

  db_mode = uDatabaseMode (dbname, &ha_mode);
  if (db_mode == DB_SERVICE_MODE_SA)
    {
      sprintf (_dbmt_error, "%s", dbname);
      return ERR_STANDALONE_MODE;
    }
  level = nv_get_val (req, "level");
  volname = nv_get_val (req, "volname");
  backupdir = nv_get_val (req, "backupdir");
  removelog = nv_get_val (req, "removelog");
  check = nv_get_val (req, "check");
  mt = nv_get_val (req, "mt");
  zip = nv_get_val (req, "zip");
  safe_replication = nv_get_val (req, "safereplication");

  if (backupdir == NULL)
    {
      strcpy (_dbmt_error, "backupdir");
      return ERR_PARAM_MISSING;
    }

  /* create directory */
  if (access (backupdir, F_OK) < 0)
    {
      if (uCreateDir (backupdir) != ERR_NO_ERROR)
	{
	  strcpy (_dbmt_error, backupdir);
	  return ERR_DIR_CREATE_FAIL;
	}
    }

  snprintf (backupfilepath, PATH_MAX - 1, "%s/%s", backupdir, volname);

  cubrid_cmd_name (cmd_name);
  argv[argc++] = cmd_name;
  argv[argc++] = UTIL_OPTION_BACKUPDB;
  if (db_mode == DB_SERVICE_MODE_NONE)
    argv[argc++] = "--" BACKUP_SA_MODE_L;
  else
    argv[argc++] = "--" BACKUP_CS_MODE_L;
  argv[argc++] = "--" BACKUP_LEVEL_L;
  argv[argc++] = level;
  argv[argc++] = "--" BACKUP_DESTINATION_PATH_L;
  argv[argc++] = backupfilepath;
  if (uStringEqual (removelog, "y"))
    argv[argc++] = "--" BACKUP_REMOVE_ARCHIVE_L;
  if (uStringEqual (check, "n"))
    argv[argc++] = "--" BACKUP_NO_CHECK_L;
  if (mt != NULL)
    {
      argv[argc++] = "--" BACKUP_THREAD_COUNT_L;
      argv[argc++] = mt;
    }
  if (zip != NULL && uStringEqual (zip, "y"))
    {
      argv[argc++] = "--" BACKUP_COMPRESS_L;
    }

  if (safe_replication != NULL && uStringEqual (safe_replication, "y"))
    {
      snprintf (sp_option, sizeof (sp_option) - 1,
		"--safe-page-id `repl_safe_page %s`", dbname);
      argv[argc++] = sp_option;
    }

  if (ha_mode != 0)
    {
      append_host_to_dbname (dbname_at_hostname, dbname,
			     sizeof (dbname_at_hostname));
      argv[argc++] = dbname_at_hostname;
    }
  else
    {
      argv[argc++] = dbname;
    }

  argv[argc++] = NULL;

  snprintf (inputfilepath, PATH_MAX - 1, "%s/DBMT_task_%d.%d",
	    sco.dbmt_tmp_dir, TS_BACKUPDB, (int) getpid ());
  inputfile = fopen (inputfilepath, "w");
  if (inputfile)
    {
      fprintf (inputfile, "y");
      fclose (inputfile);
    }
  else
    {
      return ERR_FILE_OPEN_FAIL;
    }

  INIT_CUBRID_ERROR_FILE (cubrid_err_file);

  if (run_child (argv, 1, inputfilepath, NULL, cubrid_err_file, NULL) < 0)
    {				/* backupdb */
      strcpy (_dbmt_error, argv[0]);
      unlink (inputfilepath);
      return ERR_SYSTEM_CALL;
    }

  unlink (inputfilepath);

  if (read_error_file (cubrid_err_file, _dbmt_error, DBMT_ERROR_MSG_SIZE) < 0)
    {
      return ERR_WITH_MSG;
    }

  return ERR_NO_ERROR;
}

int
ts_unloaddb (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char *dbname, *targetdir, *usehash, *hashdir, *target, *s1, *s2,
    *ref, *classonly, *delimit, *estimate, *prefix, *cach, *lofile,
    buf[PATH_MAX], infofile[PATH_MAX], tmpfile[PATH_MAX], temp[PATH_MAX],
    n[256], v[256], cname[256], p1[64], p2[8], p3[8];

  char dbname_at_hostname[MAXHOSTNAMELEN + DB_NAME_LEN];
  int ha_mode = 0;
  char cmd_name[CUBRID_CMD_NAME_LEN];
  char *cubrid_err_file;
  FILE *infile, *outfile;
  int i, flag = 0, no_class = 0, index_exist = 0, trigger_exist = 0;
  struct stat statbuf;
  const char *argv[30];
  int argc = 0;
  T_DB_SERVICE_MODE db_mode;

  dbname = nv_get_val (req, "dbname");
  if (dbname == NULL)
    {
      strcpy (_dbmt_error, "database name");
      return ERR_PARAM_MISSING;
    }

  targetdir = nv_get_val (req, "targetdir");
  usehash = nv_get_val (req, "usehash");
  hashdir = nv_get_val (req, "hashdir");
  target = nv_get_val (req, "target");
  ref = nv_get_val (req, "ref");
  classonly = nv_get_val (req, "classonly");
  delimit = nv_get_val (req, "delimit");
  estimate = nv_get_val (req, "estimate");
  prefix = nv_get_val (req, "prefix");
  cach = nv_get_val (req, "cach");
  lofile = nv_get_val (req, "lofile");

  if (target == NULL)
    {
      strcpy (_dbmt_error, "target");
      return ERR_PARAM_MISSING;
    }

  db_mode = uDatabaseMode (dbname, &ha_mode);
  if (db_mode == DB_SERVICE_MODE_SA)
    {
      sprintf (_dbmt_error, "%s", dbname);
      return ERR_STANDALONE_MODE;
    }

  if (targetdir == NULL)
    {
      strcpy (_dbmt_error, "targetdir");
      return ERR_PARAM_MISSING;
    }

  if (access (targetdir, F_OK) < 0)
    {
      if (uCreateDir (targetdir) != ERR_NO_ERROR)
	{
	  strcpy (_dbmt_error, targetdir);
	  return ERR_DIR_CREATE_FAIL;
	}
    }

  /* makeup upload class list file */
  snprintf (tmpfile, PATH_MAX - 1, "%s/DBMT_task_101.%d", sco.dbmt_tmp_dir,
	    (int) getpid ());
  if ((outfile = fopen (tmpfile, "w")) == NULL)
    {
      return ERR_TMPFILE_OPEN_FAIL;
    }
  for (i = 0; i < req->nvplist_leng; i++)
    {
      nv_lookup (req, i, &s1, &s2);
      if (s1 == NULL)
	{
	  continue;
	}

      if (!strcmp (s1, "open"))
	{
	  flag = 1;
	}
      else if (!strcmp (s1, "close"))
	{
	  flag = 0;
	}
      else if (flag == 1 && !strcmp (s1, "classname"))
	{
	  snprintf (buf, sizeof (buf) - 1, "%s\n", s2);
	  fputs (buf, outfile);
	  no_class++;
	}
    }
  fclose (outfile);

  /* makeup command and execute */
  cubrid_cmd_name (cmd_name);
  argv[argc++] = cmd_name;
  argv[argc++] = UTIL_OPTION_UNLOADDB;
  if (db_mode == DB_SERVICE_MODE_NONE)
    argv[argc++] = "--" UNLOAD_SA_MODE_L;
  else
    argv[argc++] = "--" UNLOAD_CS_MODE_L;
  if (no_class > 0)
    {
      argv[argc++] = "--" UNLOAD_INPUT_CLASS_FILE_L;
      argv[argc++] = tmpfile;
    }
  argv[argc++] = "--" UNLOAD_OUTPUT_PATH_L;
  argv[argc++] = targetdir;
  if ((usehash != NULL) && (strcmp (usehash, "yes") == 0))
    {
      argv[argc++] = "--" UNLOAD_HASH_FILE_L;
      argv[argc++] = hashdir;
    }

  if (strcmp (target, "both") == 0)
    {
      argv[argc++] = "--" UNLOAD_SCHEMA_ONLY_L;
      argv[argc++] = "--" UNLOAD_DATA_ONLY_L;
    }
  else if (strcmp (target, "schema") == 0)
    {
      argv[argc++] = "--" UNLOAD_SCHEMA_ONLY_L;
    }
  else if (strcmp (target, "object") == 0)
    {
      argv[argc++] = "--" UNLOAD_DATA_ONLY_L;
    }

  if (uStringEqual (ref, "yes"))
    argv[argc++] = "--" UNLOAD_INCLUDE_REFERENCE_L;
  if (uStringEqual (classonly, "yes"))
    argv[argc++] = "--" UNLOAD_INPUT_CLASS_ONLY_L;
  if (uStringEqual (delimit, "yes"))
    argv[argc++] = "--" UNLOAD_USE_DELIMITER_L;
  if (estimate != NULL && !uStringEqual (estimate, "none"))
    {
      argv[argc++] = "--" UNLOAD_ESTIMATED_SIZE_L;
      argv[argc++] = estimate;
    }
  if (prefix != NULL && !uStringEqual (prefix, "none"))
    {
      argv[argc++] = "--" UNLOAD_OUTPUT_PREFIX_L;
      argv[argc++] = prefix;
    }
  if (cach != NULL && !uStringEqual (cach, "none"))
    {
      argv[argc++] = "--" UNLOAD_CACHED_PAGES_L;
      argv[argc++] = cach;
    }
  if (lofile != NULL && !uStringEqual (lofile, "none"))
    {
      argv[argc++] = "--" UNLOAD_LO_COUNT_L;
      argv[argc++] = lofile;
    }

  if (ha_mode != 0)
    {
      append_host_to_dbname (dbname_at_hostname, dbname,
			     sizeof (dbname_at_hostname));
      argv[argc++] = dbname_at_hostname;
    }
  else
    {
      argv[argc++] = dbname;
    }

  argv[argc++] = NULL;

  INIT_CUBRID_ERROR_FILE (cubrid_err_file);

  if (run_child (argv, 1, NULL, NULL, cubrid_err_file, NULL) < 0)
    {				/* unloaddb */
      strcpy (_dbmt_error, argv[0]);
      unlink (tmpfile);
      return ERR_SYSTEM_CALL;
    }

  if (read_error_file (cubrid_err_file, _dbmt_error, DBMT_ERROR_MSG_SIZE) < 0)
    {
      unlink (tmpfile);
      return ERR_WITH_MSG;
    }

  unlink (tmpfile);

  /* makeup upload result information in unload.log file */
  snprintf (buf, sizeof (buf) - 1, "%s_unloaddb.log", dbname);
  nv_add_nvp (res, "open", "result");
  if ((infile = fopen (buf, "rt")) != NULL)
    {
      flag = 0;
      while (fgets (buf, sizeof (buf), infile))
	{
	  if (buf[0] == '-')
	    {
	      flag++;
	    }
	  else if (flag == 2 &&
		   sscanf (buf, "%255s %*s %63s %7s %*s %7s", cname, p1, p2,
			   p3) == 4)
	    {
	      snprintf (buf, sizeof (buf) - 1, "%s %s/%s", p1, p2, p3);
	      nv_add_nvp (res, cname, buf);
	    }
	}
      fclose (infile);
    }
  nv_add_nvp (res, "close", "result");
  unlink ("unload.log");

  /* save uploaded result file to 'unloaddb.info' file */
  flag = 0;
  snprintf (infofile, PATH_MAX - 1, "%s/unloaddb.info",
	    sco.szCubrid_databases);
  if ((infile = fopen (infofile, "rt")) == NULL)
    {
      outfile = fopen (infofile, "w");
      if (outfile == NULL)
	{
	  strcpy (_dbmt_error, infofile);
	  return ERR_FILE_OPEN_FAIL;
	}

      snprintf (buf, sizeof (buf) - 1, "%% %s\n", dbname);
      fputs (buf, outfile);

      if (!strcmp (target, "both"))
	{
	  fprintf (outfile, "schema %s/%s%s\n", targetdir, dbname,
		   CUBRID_UNLOAD_EXT_SCHEMA);
	  fprintf (outfile, "object %s/%s%s\n", targetdir, dbname,
		   CUBRID_UNLOAD_EXT_OBJ);
	}
      else if (!strcmp (target, "schema"))
	{
	  fprintf (outfile, "schema %s/%s%s\n", targetdir, dbname,
		   CUBRID_UNLOAD_EXT_SCHEMA);
	}
      else if (!strcmp (target, "object"))
	{
	  fprintf (outfile, "object %s/%s%s\n", targetdir, dbname,
		   CUBRID_UNLOAD_EXT_OBJ);
	}

      /* check index file and append if exist */
      snprintf (buf, sizeof (buf) - 1, "%s/%s%s", targetdir, dbname,
		CUBRID_UNLOAD_EXT_INDEX);
      if (stat (buf, &statbuf) == 0)
	{
	  fprintf (outfile, "index %s/%s%s\n", targetdir, dbname,
		   CUBRID_UNLOAD_EXT_INDEX);
	}
      /* check trigger file and append if exist */
      snprintf (buf, sizeof (buf) - 1, "%s/%s%s", targetdir, dbname,
		CUBRID_UNLOAD_EXT_TRIGGER);
      if (stat (buf, &statbuf) == 0)
	{
	  fprintf (outfile, "trigger %s/%s%s\n", targetdir, dbname,
		   CUBRID_UNLOAD_EXT_TRIGGER);
	}
      fclose (outfile);
    }
  else
    {
      snprintf (tmpfile, PATH_MAX - 1, "%s/DBMT_task_102.%d",
		sco.dbmt_tmp_dir, (int) getpid ());
      outfile = fopen (tmpfile, "w");
      if (outfile == NULL)
	{
	  fclose (infile);
	  strcpy (_dbmt_error, tmpfile);
	  return ERR_TMPFILE_OPEN_FAIL;
	}

      while (fgets (buf, sizeof (buf), infile))
	{
	  if (sscanf (buf, "%255s %255s", n, v) != 2)
	    {
	      fputs (buf, outfile);
	      continue;
	    }
	  if (!strcmp (n, "%") && !strcmp (v, dbname))
	    {
	      fputs (buf, outfile);

	      if (!strcmp (target, "both"))
		{
		  fprintf (outfile, "schema %s/%s%s\n", targetdir, dbname,
			   CUBRID_UNLOAD_EXT_SCHEMA);
		  fprintf (outfile, "object %s/%s%s\n", targetdir, dbname,
			   CUBRID_UNLOAD_EXT_OBJ);
		}
	      else if (!strcmp (target, "schema"))
		{
		  fprintf (outfile, "schema %s/%s%s\n", targetdir, dbname,
			   CUBRID_UNLOAD_EXT_SCHEMA);
		}
	      else if (!strcmp (target, "object"))
		{
		  fprintf (outfile, "object %s/%s%s\n", targetdir, dbname,
			   CUBRID_UNLOAD_EXT_OBJ);
		}

	      /* check index file and append if exist */
	      snprintf (temp, PATH_MAX - 1, "%s/%s%s", targetdir, dbname,
			CUBRID_UNLOAD_EXT_INDEX);
	      if (stat (temp, &statbuf) == 0)
		{
		  fprintf (outfile, "index %s/%s%s\n", targetdir, dbname,
			   CUBRID_UNLOAD_EXT_INDEX);
		  index_exist = 1;
		}
	      /* check trigger file and append if exist */
	      snprintf (temp, PATH_MAX - 1, "%s/%s%s", targetdir, dbname,
			CUBRID_UNLOAD_EXT_TRIGGER);
	      if (stat (temp, &statbuf) == 0)
		{
		  fprintf (outfile, "trigger %s/%s%s\n", targetdir, dbname,
			   CUBRID_UNLOAD_EXT_TRIGGER);
		  trigger_exist = 1;
		}
	      flag = 1;
	      continue;
	    }
	  if (!strcmp (target, "both") || !strcmp (target, "schema"))
	    {
	      snprintf (temp, PATH_MAX - 1, "%s/%s%s", targetdir, dbname,
			CUBRID_UNLOAD_EXT_SCHEMA);
	      if (!strcmp (n, "schema") && !strcmp (v, temp))
		continue;
	    }
	  if (!strcmp (target, "both") || !strcmp (target, "object"))
	    {
	      snprintf (temp, PATH_MAX - 1, "%s/%s%s", targetdir, dbname,
			CUBRID_UNLOAD_EXT_OBJ);
	      if (!strcmp (n, "object") && !strcmp (v, temp))
		continue;
	    }
	  if (index_exist)
	    {
	      snprintf (temp, PATH_MAX - 1, "%s/%s%s", targetdir, dbname,
			CUBRID_UNLOAD_EXT_INDEX);
	      if (!strcmp (n, "index") && !strcmp (v, temp))
		continue;
	    }
	  if (trigger_exist)
	    {
	      snprintf (temp, PATH_MAX - 1, "%s/%s%s", targetdir, dbname,
			CUBRID_UNLOAD_EXT_TRIGGER);
	      if (!strcmp (n, "trigger") && !strcmp (v, temp))
		continue;
	    }
	  fputs (buf, outfile);
	}			/* end of while(fgets()) */
      if (flag == 0)
	{
	  fprintf (outfile, "%% %s\n", dbname);
	  if (!strcmp (target, "both"))
	    {
	      fprintf (outfile, "schema %s/%s%s\n", targetdir, dbname,
		       CUBRID_UNLOAD_EXT_SCHEMA);
	      fprintf (outfile, "object %s/%s%s\n", targetdir, dbname,
		       CUBRID_UNLOAD_EXT_OBJ);
	    }
	  else if (!strcmp (target, "schema"))
	    {
	      fprintf (outfile, "schema %s/%s%s\n", targetdir, dbname,
		       CUBRID_UNLOAD_EXT_SCHEMA);
	    }
	  else if (!strcmp (target, "object"))
	    {
	      fprintf (outfile, "object %s/%s%s\n", targetdir, dbname,
		       CUBRID_UNLOAD_EXT_OBJ);
	    }
	  /* check index file and append if exist */
	  snprintf (temp, PATH_MAX - 1, "%s/%s%s", targetdir, dbname,
		    CUBRID_UNLOAD_EXT_INDEX);
	  if (stat (temp, &statbuf) == 0)
	    {
	      fprintf (outfile, "index %s/%s%s\n", targetdir, dbname,
		       CUBRID_UNLOAD_EXT_INDEX);
	      index_exist = 1;
	    }
	  /* check trigger file and append if exist */
	  snprintf (temp, PATH_MAX - 1, "%s/%s%s", targetdir, dbname,
		    CUBRID_UNLOAD_EXT_TRIGGER);
	  if (stat (temp, &statbuf) == 0)
	    {
	      fprintf (outfile, "trigger %s/%s%s\n", targetdir, dbname,
		       CUBRID_UNLOAD_EXT_TRIGGER);
	      trigger_exist = 1;
	    }
	}
      fclose (infile);
      fclose (outfile);

      /* copyback */
      infile = fopen (tmpfile, "rt");
      if (infile == NULL)
	{
	  strcpy (_dbmt_error, tmpfile);
	  return ERR_TMPFILE_OPEN_FAIL;
	}

      outfile = fopen (infofile, "w");
      if (outfile == NULL)
	{
	  strcpy (_dbmt_error, infofile);
	  return ERR_FILE_OPEN_FAIL;
	}

      while (fgets (buf, sizeof (buf), infile))
	{
	  fputs (buf, outfile);
	}
      fclose (infile);
      fclose (outfile);
      unlink (tmpfile);
    }				/* end of if */

  return ERR_NO_ERROR;
}

int
ts_loaddb (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char *dbname, *checkoption, *period, *user, *schema, *object, *index,
    *error_control_file, *ignore_class_file, *estimated, *oiduse, *nolog,
    *statisticsuse, buf[1024], tmpfile[PATH_MAX];

  FILE *infile;
  T_DB_SERVICE_MODE db_mode;
  char *dbuser, *dbpasswd;
  char *cubrid_err_file;
  int retval;
  char cmd_name[CUBRID_CMD_NAME_LEN];
  const char *argv[29];
  int argc = 0;
  int status;

  if ((dbname = nv_get_val (req, "_DBNAME")) == NULL)
    {
      sprintf (_dbmt_error, "%s", "database name");
      return ERR_PARAM_MISSING;
    }

  dbuser = nv_get_val (req, "_DBID");
  dbpasswd = nv_get_val (req, "_DBPASSWD");

  checkoption = nv_get_val (req, "checkoption");
  period = nv_get_val (req, "period");
  user = nv_get_val (req, "user");
  schema = nv_get_val (req, "schema");
  object = nv_get_val (req, "object");
  index = nv_get_val (req, "index");
  error_control_file = nv_get_val (req, "errorcontrolfile");
  ignore_class_file = nv_get_val (req, "ignoreclassfile");
#if 0				/* will be added */
  trigger = nv_get_val (req, "trigger");
#endif
  estimated = nv_get_val (req, "estimated");
  oiduse = nv_get_val (req, "oiduse");
  nolog = nv_get_val (req, "nolog");
  statisticsuse = nv_get_val (req, "statisticsuse");

  db_mode = uDatabaseMode (dbname, NULL);
  if (db_mode == DB_SERVICE_MODE_SA)
    {
      sprintf (_dbmt_error, "%s", dbname);
      return ERR_STANDALONE_MODE;
    }
  else if (db_mode == DB_SERVICE_MODE_CS)
    {
      sprintf (_dbmt_error, "%s", dbname);
      return ERR_DB_ACTIVE;
    }

  snprintf (tmpfile, PATH_MAX - 1, "%s/DBMT_task_%d.%d", sco.dbmt_tmp_dir,
	    TS_LOADDB, (int) getpid ());
  cubrid_cmd_name (cmd_name);

  argc = 0;
  argv[argc++] = cmd_name;
  argv[argc++] = UTIL_OPTION_LOADDB;

  if (checkoption != NULL)
    {
      if (strcmp (checkoption, "syntax") == 0)
	{
	  argv[argc++] = "--" LOAD_CHECK_ONLY_L;
	}
      else if (strcmp (checkoption, "load") == 0)
	{
	  argv[argc++] = "--" LOAD_LOAD_ONLY_L;
	}
    }

  if (dbuser)
    {
      argv[argc++] = "--" LOAD_USER_L;
      argv[argc++] = dbuser;
      if (dbpasswd)
	{
	  argv[argc++] = "--" LOAD_PASSWORD_L;
	  argv[argc++] = dbpasswd;
	}
    }

/*    argv[argc++] = "-v";*/
  if (period != NULL && !uStringEqual (period, "none"))
    {
      argv[argc++] = "--" LOAD_PERIODIC_COMMIT_L;
      argv[argc++] = period;
    }

  if ((schema != NULL) && (strcmp (schema, "none") != 0))
    {
      argv[argc++] = "--" LOAD_SCHEMA_FILE_L;
      argv[argc++] = schema;
    }

  if ((object != NULL) && (strcmp (object, "none") != 0))
    {
      argv[argc++] = "--" LOAD_DATA_FILE_L;
      argv[argc++] = object;
    }

  if ((index != NULL) && (strcmp (index, "none") != 0))
    {
      argv[argc++] = "--" LOAD_INDEX_FILE_L;
      argv[argc++] = index;
    }

#if 0				/* will be added */
  if (trigger != NULL && !uStringEqual (trigger, "none"))
    {
      argv[argc++] = "-tf";
      argv[argc++] = trigger;
    }
#endif

  if (estimated != NULL && !uStringEqual (estimated, "none"))
    {
      argv[argc++] = "--" LOAD_ESTIMATED_SIZE_L;
      argv[argc++] = estimated;
    }

  if (uStringEqual (oiduse, "no"))
    argv[argc++] = "--" LOAD_NO_OID_L;

  if (uStringEqual (statisticsuse, "no"))
    argv[argc++] = "--" LOAD_NO_STATISTICS_L;

  if (uStringEqual (nolog, "yes"))
    argv[argc++] = "--" LOAD_IGNORE_LOGGING_L;

  if (error_control_file != NULL
      && !uStringEqual (error_control_file, "none"))
    {
      argv[argc++] = "--" LOAD_ERROR_CONTROL_FILE_L;
      argv[argc++] = error_control_file;
    }

  if (ignore_class_file != NULL && !uStringEqual (ignore_class_file, "none"))
    {
      argv[argc++] = "--" LOAD_IGNORE_CLASS_L;
      argv[argc++] = ignore_class_file;
    }
  argv[argc++] = dbname;
  argv[argc++] = NULL;

  INIT_CUBRID_ERROR_FILE (cubrid_err_file);

  retval = run_child (argv, 1, NULL, tmpfile, cubrid_err_file, &status);	/* loaddb */
  if (status != 0
      && read_error_file (cubrid_err_file, _dbmt_error,
			  DBMT_ERROR_MSG_SIZE) < 0)
    return ERR_WITH_MSG;

  if (retval < 0)
    {
      strcpy (_dbmt_error, argv[0]);
      return ERR_SYSTEM_CALL;
    }

  infile = fopen (tmpfile, "r");
  if (infile == NULL)
    {
      strcpy (_dbmt_error, tmpfile);
      return ERR_TMPFILE_OPEN_FAIL;
    }

  while (fgets (buf, sizeof (buf), infile))
    {
      uRemoveCRLF (buf);
      nv_add_nvp (res, "line", buf);
    }

  fclose (infile);
  unlink (tmpfile);

  return ERR_NO_ERROR;
}

int
ts_restoredb (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char *dbname, *date, *lv, *pathname, *partial, *recovery_path;
  char cmd_name[CUBRID_CMD_NAME_LEN];
  const char *argv[17];
  int argc = 0;
  T_DB_SERVICE_MODE db_mode;
  char *cubrid_err_file;
  int status;

  dbname = nv_get_val (req, "dbname");
  db_mode = uDatabaseMode (dbname, NULL);
  if (db_mode == DB_SERVICE_MODE_SA)
    {
      sprintf (_dbmt_error, "%s", dbname);
      return ERR_STANDALONE_MODE;
    }
  else if (db_mode == DB_SERVICE_MODE_CS)
    {
      sprintf (_dbmt_error, "%s", dbname);
      return ERR_DB_ACTIVE;
    }

  date = nv_get_val (req, "date");
  lv = nv_get_val (req, "level");
  pathname = nv_get_val (req, "pathname");
  partial = nv_get_val (req, "partial");
  recovery_path = nv_get_val (req, "recoverypath");

  cubrid_cmd_name (cmd_name);
  argv[argc++] = cmd_name;
  argv[argc++] = UTIL_OPTION_RESTOREDB;
  if ((date != NULL) && (strcmp (date, "none") != 0))
    {
      argv[argc++] = "--" RESTORE_UP_TO_DATE_L;
      argv[argc++] = date;
    }
  argv[argc++] = "--" RESTORE_LEVEL_L;
  argv[argc++] = lv;
  if (pathname != NULL && !uStringEqual (pathname, "none"))
    {
      argv[argc++] = "--" RESTORE_BACKUP_FILE_PATH_L;
      argv[argc++] = pathname;
    }
  if (uStringEqual (partial, "y"))
    argv[argc++] = "--" RESTORE_PARTIAL_RECOVERY_L;

  if (recovery_path != NULL && !uStringEqual (recovery_path, "")
      && !uStringEqual (recovery_path, "none"))
    {
      /* use -u option to specify restore database path */
      argv[argc++] = "--" RESTORE_USE_DATABASE_LOCATION_PATH_L;
      argv[argc++] = recovery_path;
    }
  argv[argc++] = dbname;
  argv[argc++] = NULL;

  INIT_CUBRID_ERROR_FILE (cubrid_err_file);

#if defined(WINDOWS)
  if (run_child (argv, 1, NULL, NULL, cubrid_err_file, &status) < 0)
#else
  if (run_child (argv, 1, "/dev/null", NULL, cubrid_err_file, &status) < 0)
#endif
    {				/* restoredb */
      strcpy (_dbmt_error, argv[0]);
      return ERR_SYSTEM_CALL;
    }

  if (status != 0 && read_error_file (cubrid_err_file, _dbmt_error,
				      DBMT_ERROR_MSG_SIZE) < 0)
    return ERR_WITH_MSG;

  return ERR_NO_ERROR;
}

int
ts_backup_vol_info (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char *dbname, *lv, *pathname, buf[1024], tmpfile[PATH_MAX];
  char dbname_at_hostname[MAXHOSTNAMELEN + DB_NAME_LEN];
  int ha_mode = 0;
  int ret;
  FILE *infile;
  char cmd_name[CUBRID_CMD_NAME_LEN];
  const char *argv[10];
  int argc = 0;

  dbname = nv_get_val (req, "dbname");
  snprintf (tmpfile, PATH_MAX - 1, "%s/DBMT_task_%d.%d", sco.dbmt_tmp_dir,
	    TS_BACKUPVOLINFO, (int) getpid ());

  if (uIsDatabaseActive (dbname))
    {
      sprintf (_dbmt_error, "%s", dbname);
      return ERR_DB_ACTIVE;
    }

  if (uDatabaseMode (dbname, &ha_mode) == DB_SERVICE_MODE_SA)
    {
      sprintf (_dbmt_error, "%s", dbname);
      return ERR_STANDALONE_MODE;
    }

  lv = nv_get_val (req, "level");
  pathname = nv_get_val (req, "pathname");

  cubrid_cmd_name (cmd_name);
  argv[argc++] = cmd_name;
  argv[argc++] = UTIL_OPTION_RESTOREDB;
  argv[argc++] = "--" RESTORE_LIST_L;
  if (lv != NULL &&
      (uStringEqual (lv, "0") || uStringEqual (lv, "1")
       || uStringEqual (lv, "2")))
    {
      argv[argc++] = "--" RESTORE_LEVEL_L;
      argv[argc++] = lv;
    }
  if (pathname != NULL && !uStringEqual (pathname, "none"))
    {
      argv[argc++] = "--" RESTORE_BACKUP_FILE_PATH_L;
      argv[argc++] = pathname;
    }

  if (ha_mode != 0)
    {
      append_host_to_dbname (dbname_at_hostname, dbname,
			     sizeof (dbname_at_hostname));
      argv[argc++] = dbname_at_hostname;
    }
  else
    {
      argv[argc++] = dbname;
    }

  argv[argc++] = NULL;

#if defined(WINDOWS)
  ret = run_child (argv, 1, NULL, tmpfile, NULL, NULL);	/* restoredb -t */
#else
  ret = run_child (argv, 1, "/dev/null", tmpfile, NULL, NULL);	/* restoredb -t */
#endif
  if (ret < 0)
    {
      sprintf (_dbmt_error, "%s", argv[0]);
      return ERR_SYSTEM_CALL;
    }

  infile = fopen (tmpfile, "r");
  if (infile == NULL)
    {
      strcpy (_dbmt_error, tmpfile);
      return ERR_TMPFILE_OPEN_FAIL;
    }

  while (fgets (buf, sizeof (buf), infile))
    {
      uRemoveCRLF (buf);
      nv_add_nvp (res, "line", buf);
    }
  fclose (infile);
  unlink (tmpfile);

  return ERR_NO_ERROR;
}

int
ts_get_dbsize (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char *dbname;
  char dbname_at_hostname[MAXHOSTNAMELEN + DB_NAME_LEN];
  int ha_mode = 0;
  char strbuf[PATH_MAX], dbdir[PATH_MAX];
  int pagesize, no_tpage = 0, log_size = 0, baselen;
  struct stat statbuf;
  T_SPACEDB_RESULT *cmd_res;
  T_CUBRID_MODE cubrid_mode;
  int i;
#if defined(WINDOWS)
  char find_file[PATH_MAX];
  WIN32_FIND_DATA data;
  HANDLE handle;
  int found;
#else
  DIR *dirp;
  struct dirent *dp;
#endif
  char *cur_file;

  /* get dbname */
  if ((dbname = nv_get_val (req, "dbname")) == NULL)
    {
      sprintf (_dbmt_error, "%s", "database name");
      return ERR_PARAM_MISSING;
    }

  if (uRetrieveDBDirectory (dbname, dbdir) != ERR_NO_ERROR)
    {
      strcpy (_dbmt_error, dbname);
      return ERR_DBDIRNAME_NULL;
    }

  cubrid_mode =
    (uDatabaseMode (dbname, &ha_mode) ==
     DB_SERVICE_MODE_NONE) ? CUBRID_MODE_SA : CUBRID_MODE_CS;

  if (ha_mode != 0)
    {
      append_host_to_dbname (dbname_at_hostname, dbname,
			     sizeof (dbname_at_hostname));
      cmd_res = cmd_spacedb (dbname_at_hostname, cubrid_mode);
    }
  else
    {
      cmd_res = cmd_spacedb (dbname, cubrid_mode);
    }

  if (cmd_res == NULL || cmd_res->err_msg[0])
    {
      sprintf (_dbmt_error, "spacedb %s", dbname);
      cmd_spacedb_result_free (cmd_res);
      return ERR_SYSTEM_CALL;
    }

  for (i = 0; i < cmd_res->num_vol; i++)
    {
      no_tpage += cmd_res->vol_info[i].total_page;
    }
  for (i = 0; i < cmd_res->num_tmp_vol; i++)
    {
      no_tpage += cmd_res->tmp_vol_info[i].total_page;
    }
  pagesize = cmd_res->page_size;
  cmd_spacedb_result_free (cmd_res);

  /* get log volume info */
#if defined(WINDOWS)
  snprintf (find_file, PATH_MAX - 1, "%s/*", dbdir);
  if ((handle = FindFirstFile (find_file, &data)) == INVALID_HANDLE_VALUE)
#else
  if ((dirp = opendir (dbdir)) == NULL)
#endif
    {
      sprintf (_dbmt_error, "%s", dbdir);
      return ERR_DIROPENFAIL;
    }

  baselen = strlen (dbname);
#if defined(WINDOWS)
  for (found = 1; found; found = FindNextFile (handle, &data))
#else
  while ((dp = readdir (dirp)) != NULL)
#endif
    {
#if defined(WINDOWS)
      cur_file = data.cFileName;
#else
      cur_file = dp->d_name;
#endif
      if (!strncmp (cur_file + baselen, "_lginf", 6)
	  || !strcmp (cur_file + baselen, CUBRID_ACT_LOG_EXT)
	  || !strncmp (cur_file + baselen, CUBRID_ARC_LOG_EXT,
		       CUBRID_ARC_LOG_EXT_LEN))
	{
	  snprintf (strbuf, sizeof (strbuf) - 1, "%s/%s", dbdir, cur_file);
	  stat (strbuf, &statbuf);
	  log_size += statbuf.st_size;
	}
    }

  snprintf (strbuf, sizeof (strbuf) - 1, "%d",
	    no_tpage * pagesize + log_size);
  nv_add_nvp (res, "dbsize", strbuf);

  return ERR_NO_ERROR;
}






int
tsGetEnvironment (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char tmpfile[PATH_MAX];
  char strbuf[1024];
  FILE *infile;
  char cmd_name[CUBRID_CMD_NAME_LEN];
  const char *argv[5];

  nv_add_nvp (res, "CUBRID", sco.szCubrid);
  nv_add_nvp (res, "CUBRID_DATABASES", sco.szCubrid_databases);
  nv_add_nvp (res, "CUBRID_DBMT", sco.szCubrid);
  snprintf (tmpfile, PATH_MAX - 1, "%s/DBMT_task_015.%d", sco.dbmt_tmp_dir,
	    (int) getpid ());

  cmd_name[0] = '\0';
  snprintf (cmd_name, sizeof (cmd_name) - 1, "%s/%s%s", sco.szCubrid,
	    CUBRID_DIR_BIN, UTIL_CUBRID_REL_NAME);

  argv[0] = cmd_name;
  argv[1] = NULL;

  run_child (argv, 1, NULL, tmpfile, NULL, NULL);	/* cubrid_rel */

  if ((infile = fopen (tmpfile, "r")) != NULL)
    {
      fgets (strbuf, sizeof (strbuf), infile);
      fgets (strbuf, sizeof (strbuf), infile);
      uRemoveCRLF (strbuf);
      fclose (infile);
      unlink (tmpfile);
      nv_add_nvp (res, "CUBRIDVER", strbuf);
    }
  else
    {
      nv_add_nvp (res, "CUBRIDVER", "version information not available");
    }

  snprintf (tmpfile, PATH_MAX - 1, "%s/DBMT_task_015.%d", sco.dbmt_tmp_dir,
	    (int) getpid ());
  snprintf (cmd_name, sizeof (cmd_name) - 1, "%s/bin/cubrid_broker%s",
	    sco.szCubrid, DBMT_EXE_EXT);

  argv[0] = cmd_name;
  argv[1] = "--version";
  argv[2] = NULL;

  run_child (argv, 1, NULL, tmpfile, NULL, NULL);	/* cubrid_broker --version */

  if ((infile = fopen (tmpfile, "r")) != NULL)
    {
      fgets (strbuf, sizeof (strbuf), infile);
      fclose (infile);
      uRemoveCRLF (strbuf);
      unlink (tmpfile);
      nv_add_nvp (res, "BROKERVER", strbuf);
    }
  else
    nv_add_nvp (res, "BROKERVER", "version information not available");

  if (sco.hmtab1 == 1)
    nv_add_nvp (res, "HOSTMONTAB0", "ON");
  else
    nv_add_nvp (res, "HOSTMONTAB0", "OFF");
  if (sco.hmtab2 == 1)
    nv_add_nvp (res, "HOSTMONTAB1", "ON");
  else
    nv_add_nvp (res, "HOSTMONTAB1", "OFF");
  if (sco.hmtab3 == 1)
    nv_add_nvp (res, "HOSTMONTAB2", "ON");
  else
    nv_add_nvp (res, "HOSTMONTAB2", "OFF");
  if (sco.hmtab4 == 1)
    nv_add_nvp (res, "HOSTMONTAB3", "ON");
  else
    nv_add_nvp (res, "HOSTMONTAB3", "OFF");

#if defined(WINDOWS)
  nv_add_nvp (res, "osinfo", "NT");
#else
  nv_add_nvp (res, "osinfo", "unknown");
#endif


  return ERR_NO_ERROR;
}

int
ts_startinfo (nvplist * req, nvplist * res, char *_dbmt_error)
{
  T_SERVER_STATUS_RESULT *cmd_res;
  int retval;

  /* add dblist */
  retval = _tsAppendDBList (res, 1);
  if (retval != ERR_NO_ERROR)
    return retval;

  nv_add_nvp (res, "open", "activelist");
  cmd_res = cmd_server_status ();
  if (cmd_res != NULL)
    {
      T_SERVER_STATUS_INFO *info = (T_SERVER_STATUS_INFO *) cmd_res->result;
      int i;
      for (i = 0; i < cmd_res->num_result; i++)
	{
	  nv_add_nvp (res, "dbname", info[i].db_name);
	}
    }
  nv_add_nvp (res, "close", "activelist");

  uWriteDBnfo2 (cmd_res);
  cmd_servstat_result_free (cmd_res);

  return ERR_NO_ERROR;
}

int
ts_kill_process (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char *pid_str;
  int pid;
  char *tgt_name;

  if ((pid_str = nv_get_val (req, "pid")) == NULL)
    {
      strcpy (_dbmt_error, "pid");
      return ERR_PARAM_MISSING;
    }
  tgt_name = nv_get_val (req, "name");

  pid = atoi (pid_str);
  if (pid > 0)
    {
      if (kill (pid, SIGTERM) < 0)
	{
	  DBMT_ERR_MSG_SET (_dbmt_error, strerror (errno));
	  return ERR_WITH_MSG;
	}
    }

  nv_add_nvp (res, "name", tgt_name);
  uWriteDBnfo ();
  return ERR_NO_ERROR;
}

int
ts_backupdb_info (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char db_dir[PATH_MAX], log_dir[PATH_MAX];
  char *dbname, vinf[PATH_MAX], buf[PATH_MAX];
  FILE *infile;
  struct stat statbuf;
  char *tok[3];

  if ((dbname = nv_get_val (req, "dbname")) == NULL)
    return ERR_PARAM_MISSING;

  if (uDatabaseMode (dbname, NULL) == DB_SERVICE_MODE_SA)
    {
      sprintf (_dbmt_error, "%s", dbname);
      return ERR_STANDALONE_MODE;
    }

  if (uRetrieveDBDirectory (dbname, db_dir) != ERR_NO_ERROR)
    {
      strcpy (_dbmt_error, dbname);
      return ERR_DBDIRNAME_NULL;
    }
  snprintf (buf, sizeof (buf) - 1, "%s/backup", db_dir);
  nv_add_nvp (res, "dbdir", buf);

  if (uRetrieveDBLogDirectory (dbname, log_dir) != ERR_NO_ERROR)
    {
      strcpy (_dbmt_error, dbname);
      return ERR_DBDIRNAME_NULL;
    }

  snprintf (vinf, sizeof (vinf) - 1, "%s/%s%s", log_dir, dbname,
	    CUBRID_BACKUP_INFO_EXT);
  if ((infile = fopen (vinf, "rt")) != NULL)
    {
      while (fgets (buf, sizeof (buf), infile))
	{
	  ut_trim (buf);
	  if (string_tokenize (buf, tok, 3) < 0)
	    {
	      continue;
	    }
	  if (stat (tok[2], &statbuf) == 0)
	    {
	      snprintf (vinf, sizeof (vinf) - 1, "level%s", tok[0]);
	      nv_add_nvp (res, "open", vinf);
	      nv_add_nvp (res, "path", tok[2]);
	      nv_add_nvp_int (res, "size", statbuf.st_size);
	      nv_add_nvp_time (res, "data", statbuf.st_mtime,
			       "%04d.%02d.%02d.%02d.%02d", NV_ADD_DATE_TIME);
	      nv_add_nvp (res, "close", vinf);
	    }
	}
      fclose (infile);
    }

  nv_add_nvp_int (res, "freespace", ut_disk_free_space (db_dir));

  return ERR_NO_ERROR;
}

int
ts_unloaddb_info (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char n[256], v[256], buf[1024];
  FILE *infile;
  int flag = 0;
  struct stat statbuf;

  snprintf (buf, sizeof (buf) - 1, "%s/unloaddb.info",
	    sco.szCubrid_databases);
  if ((infile = fopen (buf, "rt")) == NULL)
    {
      return ERR_NO_ERROR;
    }

  while (fgets (buf, sizeof (buf), infile))
    {
      if (sscanf (buf, "%255s %255s", n, v) != 2)
	continue;
      if (!strcmp (n, "%"))
	{
	  if (flag == 1)
	    {
	      nv_add_nvp (res, "close", "database");
	    }
	  else
	    {
	      flag = 1;
	    }
	  nv_add_nvp (res, "open", "database");
	  nv_add_nvp (res, "dbname", v);
	}
      else
	{
	  if (stat (v, &statbuf) == 0)
	    {
	      char timestr[64];
	      time_to_str (statbuf.st_mtime, "%04d.%02d.%02d %02d:%02d",
			   timestr, TIME_STR_FMT_DATE_TIME);
	      snprintf (buf, sizeof (buf) - 1, "%s;%s", v, timestr);
	      nv_add_nvp (res, n, buf);
	    }
	}
    }
  if (flag == 1)
    {
      nv_add_nvp (res, "close", "database");
    }
  fclose (infile);

  return ERR_NO_ERROR;
}

/* backup automation */

int
ts_get_backup_info (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char *dbname;
  FILE *infile;
  char strbuf[1024];
  char *conf_item[AUTOBACKUP_CONF_ENTRY_NUM];
  int i;

  if ((dbname = nv_get_val (req, "_DBNAME")) == NULL)
    {
      strcpy (_dbmt_error, "database name");
      return ERR_PARAM_MISSING;
    }

  nv_add_nvp (res, "dbname", dbname);
  infile = fopen (conf_get_dbmt_file (FID_AUTO_BACKUPDB_CONF, strbuf), "r");
  if (infile == NULL)
    {
      return ERR_NO_ERROR;
    }

  while (fgets (strbuf, sizeof (strbuf), infile))
    {
      ut_trim (strbuf);
      if (strbuf[0] == '#')
	{
	  continue;
	}
      if (string_tokenize (strbuf, conf_item, AUTOBACKUP_CONF_ENTRY_NUM) < 0)
	{
	  continue;
	}
      if (strcmp (conf_item[0], dbname) == 0)
	{
	  for (i = 0; i < AUTOBACKUP_CONF_ENTRY_NUM; i++)
	    {
	      nv_add_nvp (res, autobackup_conf_entry[i], conf_item[i]);
	    }
	}
    }
  fclose (infile);

  return ERR_NO_ERROR;
}

int
ts_set_backup_info (nvplist * req, nvplist * res, char *_dbmt_error)
{
  FILE *infile, *outfile;
  char line[1024], tmpfile[PATH_MAX];
  char autofilepath[PATH_MAX];
  char *conf_item[AUTOBACKUP_CONF_ENTRY_NUM];
  int i;

  if ((conf_item[0] = nv_get_val (req, "_DBNAME")) == NULL)
    {
      return ERR_PARAM_MISSING;
    }
  for (i = 1; i < AUTOBACKUP_CONF_ENTRY_NUM; i++)
    {
      conf_item[i] = nv_get_val (req, autobackup_conf_entry[i]);
      if (conf_item[i] == NULL)
	{
	  return ERR_PARAM_MISSING;
	}
    }

  conf_get_dbmt_file (FID_AUTO_BACKUPDB_CONF, autofilepath);
  if (access (autofilepath, F_OK) < 0)
    {
      outfile = fopen (autofilepath, "w");
      if (outfile == NULL)
	{
	  strcpy (_dbmt_error, autofilepath);
	  return ERR_FILE_OPEN_FAIL;
	}
      for (i = 0; i < AUTOBACKUP_CONF_ENTRY_NUM; i++)
	{
	  fprintf (outfile, "%s ", conf_item[i]);
	}
      fprintf (outfile, "\n");
      fclose (outfile);
      return ERR_NO_ERROR;
    }

  if ((infile = fopen (autofilepath, "r")) == NULL)
    {
      strcpy (_dbmt_error, autofilepath);
      return ERR_FILE_OPEN_FAIL;
    }
  snprintf (tmpfile, PATH_MAX - 1, "%s/DBMT_task_%d.%d", sco.dbmt_tmp_dir,
	    TS_SETBACKUPINFO, (int) getpid ());
  if ((outfile = fopen (tmpfile, "w")) == NULL)
    {
      fclose (infile);
      return ERR_TMPFILE_OPEN_FAIL;
    }
  while (fgets (line, sizeof (line), infile))
    {
      char conf_dbname[128], conf_backupid[128];

      if (sscanf (line, "%127s %127s", conf_dbname, conf_backupid) < 2)
	{
	  continue;
	}
      if ((strcmp (conf_dbname, conf_item[0]) == 0) &&
	  (strcmp (conf_backupid, conf_item[1]) == 0))
	{
	  for (i = 0; i < AUTOBACKUP_CONF_ENTRY_NUM; i++)
	    {
	      fprintf (outfile, "%s ", conf_item[i]);
	    }
	  fprintf (outfile, "\n");
	}
      else
	{
	  fputs (line, outfile);
	}
    }
  fclose (infile);
  fclose (outfile);
  move_file (tmpfile, autofilepath);

  return ERR_NO_ERROR;
}

int
ts_add_backup_info (nvplist * req, nvplist * res, char *_dbmt_error)
{
  FILE *outfile;
  char autofilepath[PATH_MAX];
  char *conf_item[AUTOBACKUP_CONF_ENTRY_NUM];
  int i;

  if ((conf_item[0] = nv_get_val (req, "_DBNAME")) == NULL)
    {
      strcpy (_dbmt_error, "database name");
      return ERR_PARAM_MISSING;
    }
  for (i = 1; i < AUTOBACKUP_CONF_ENTRY_NUM; i++)
    {
      conf_item[i] = nv_get_val (req, autobackup_conf_entry[i]);
      if (conf_item[i] == NULL)
	return ERR_PARAM_MISSING;
    }

  conf_get_dbmt_file (FID_AUTO_BACKUPDB_CONF, autofilepath);
  if ((outfile = fopen (autofilepath, "a")) == NULL)
    {
      strcpy (_dbmt_error, autofilepath);
      return ERR_FILE_OPEN_FAIL;
    }
  for (i = 0; i < AUTOBACKUP_CONF_ENTRY_NUM; i++)
    fprintf (outfile, "%s ", conf_item[i]);
  fprintf (outfile, "\n");

  fclose (outfile);

  return ERR_NO_ERROR;
}

int
ts_delete_backup_info (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char *dbname, *backupid;
  FILE *infile, *outfile;
  char line[1024], tmpfile[PATH_MAX];
  char autofilepath[PATH_MAX];

  if ((dbname = nv_get_val (req, "_DBNAME")) == NULL)
    {
      strcpy (_dbmt_error, "database name");
      return ERR_PARAM_MISSING;
    }
  backupid = nv_get_val (req, "backupid");

  conf_get_dbmt_file (FID_AUTO_BACKUPDB_CONF, autofilepath);
  if ((infile = fopen (autofilepath, "r")) == NULL)
    {
      strcpy (_dbmt_error, autofilepath);
      return ERR_FILE_OPEN_FAIL;
    }
  snprintf (tmpfile, PATH_MAX - 1, "%s/DBMT_task_%d.%d", sco.dbmt_tmp_dir,
	    TS_DELETEBACKUPINFO, (int) getpid ());
  if ((outfile = fopen (tmpfile, "w")) == NULL)
    {
      return ERR_TMPFILE_OPEN_FAIL;
    }

  while (fgets (line, sizeof (line), infile))
    {
      char conf_dbname[128], conf_backupid[128];

      if (sscanf (line, "%127s %127s", conf_dbname, conf_backupid) != 2)
	continue;
      if ((strcmp (conf_dbname, dbname) != 0) ||
	  backupid == NULL || (strcmp (conf_backupid, backupid) != 0))
	{
	  fputs (line, outfile);
	}
    }
  fclose (infile);
  fclose (outfile);
  move_file (tmpfile, autofilepath);
  return ERR_NO_ERROR;
}

int
ts_get_log_info (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char *dbname, log_dir[PATH_MAX], buf[PATH_MAX];
  char *error_log_param;
  struct stat statbuf;
#if defined(WINDOWS)
  WIN32_FIND_DATA data;
  HANDLE handle;
  int found;
#else
  DIR *dirp = NULL;
  struct dirent *dp = NULL;
#endif
  char find_file[PATH_MAX];
  char *fname;

  dbname = nv_get_val (req, "_DBNAME");

  if ((dbname == NULL)
      || (uRetrieveDBDirectory (dbname, log_dir) != ERR_NO_ERROR))
    {
      if (dbname != NULL)
	{
	  strcpy (_dbmt_error, dbname);
	}
      return ERR_DBDIRNAME_NULL;
    }

  nv_add_nvp (res, "dbname", dbname);
  nv_add_nvp (res, "open", "loginfo");

  if ((error_log_param = _ts_get_error_log_param (dbname)) == NULL)
    {
      snprintf (buf, sizeof (buf) - 1, "%s/%s.err", log_dir, dbname);
    }
  else if (error_log_param[0] == '/')
    {
      snprintf (buf, sizeof (buf) - 1, "%s", error_log_param);
    }
#if defined(WINDOWS)
  else if (error_log_param[2] == '/')
    {
      snprintf (buf, sizeof (buf) - 1, "%s", error_log_param);
    }
#endif
  else
    {
      snprintf (buf, sizeof (buf) - 1, "%s/%s", log_dir, error_log_param);
    }

  if (stat (buf, &statbuf) == 0)
    {
      nv_add_nvp (res, "open", "log");
      nv_add_nvp (res, "path", buf);
      nv_add_nvp (res, "owner", get_user_name (statbuf.st_uid, buf));
      nv_add_nvp_int (res, "size", statbuf.st_size);
      nv_add_nvp_time (res, "lastupdate", statbuf.st_mtime, "%04d.%02d.%02d",
		       NV_ADD_DATE);
      nv_add_nvp (res, "close", "log");
    }
  FREE_MEM (error_log_param);

  snprintf (buf, sizeof (buf) - 1, "%s/cub_server.err", log_dir);
  if (stat (buf, &statbuf) == 0)
    {
      nv_add_nvp (res, "open", "log");
      nv_add_nvp (res, "path", buf);
      nv_add_nvp (res, "owner", get_user_name (statbuf.st_uid, buf));
      nv_add_nvp_int (res, "size", statbuf.st_size);
      nv_add_nvp_time (res, "lastupdate", statbuf.st_mtime, "%04d.%02d.%02d",
		       NV_ADD_DATE);
      nv_add_nvp (res, "close", "log");
    }

  snprintf (find_file, PATH_MAX - 1, "%s/%s", sco.szCubrid,
	    CUBRID_ERROR_LOG_DIR);
#if defined(WINDOWS)
  snprintf (&find_file[strlen (find_file)], PATH_MAX - strlen (find_file) - 1,
	    "/*");
  if ((handle = FindFirstFile (find_file, &data)) == INVALID_HANDLE_VALUE)
#else
  if ((dirp = opendir (find_file)) == NULL)
#endif
    {
      nv_add_nvp (res, "close", "loginfo");
      return ERR_NO_ERROR;
    }
#if defined(WINDOWS)
  for (found = 1; found; found = FindNextFile (handle, &data))
#else
  while ((dp = readdir (dirp)) != NULL)
#endif
    {
#if defined(WINDOWS)
      fname = data.cFileName;
#else
      fname = dp->d_name;
#endif
      if (strstr (fname, ".err") == NULL)
	{
	  continue;
	}
      if (memcmp (fname, dbname, strlen (dbname)))
	{
	  continue;
	}
      if (isalnum (fname[strlen (dbname)]))
	{
	  continue;
	}
      snprintf (buf, sizeof (buf) - 1, "%s/%s/%s", sco.szCubrid,
		CUBRID_ERROR_LOG_DIR, fname);
      if (stat (buf, &statbuf) == 0)
	{
	  nv_add_nvp (res, "open", "log");
	  nv_add_nvp (res, "path", buf);
	  nv_add_nvp (res, "owner", get_user_name (statbuf.st_uid, buf));
	  nv_add_nvp_int (res, "size", statbuf.st_size);
	  nv_add_nvp_time (res, "lastupdate", statbuf.st_mtime,
			   "%04d.%02d.%02d", NV_ADD_DATE);
	  nv_add_nvp (res, "close", "log");
	}
    }
#if defined(WINDOWS)
  FindClose (handle);
#else
  closedir (dirp);
#endif

  nv_add_nvp (res, "close", "loginfo");
  return ERR_NO_ERROR;
}

int
ts_view_log (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char *dbname, *filepath, *startline, *endline, buf[1024];
  FILE *infile;
  int no_line = 0, start, end;

  dbname = nv_get_val (req, "_DBNAME");
  filepath = nv_get_val (req, "path");
  if (filepath == NULL)
    {
      strcpy (_dbmt_error, "filepath");
      return ERR_PARAM_MISSING;
    }

  startline = nv_get_val (req, "start");
  endline = nv_get_val (req, "end");
  if (startline != NULL)
    {
      start = atoi (startline);
    }
  else
    {
      start = -1;
    }

  if (endline != NULL)
    {
      end = atoi (endline);
    }
  else
    {
      end = -1;
    }

  if ((infile = fopen (filepath, "rt")) == NULL)
    {
      sprintf (_dbmt_error, "%s", filepath);
      return ERR_FILE_OPEN_FAIL;
    }
  nv_add_nvp (res, "path", filepath);
  nv_add_nvp (res, "open", "log");
  while (fgets (buf, sizeof (buf), infile))
    {
      no_line++;
      if (start != -1 && end != -1)
	{
	  if (start > no_line || end < no_line)
	    {
	      continue;
	    }
	}
      buf[strlen (buf) - 1] = '\0';
      nv_add_nvp (res, "line", buf);
    }
  fclose (infile);
  nv_add_nvp (res, "close", "log");

  if (start != -1)
    {
      if (start > no_line)
	{
	  snprintf (buf, sizeof (buf) - 1, "%d", no_line);
	}
      else
	{
	  snprintf (buf, sizeof (buf) - 1, "%d", start);
	}
      nv_add_nvp (res, "start", buf);
    }
  if (end != -1)
    {
      if (end > no_line)
	{
	  snprintf (buf, sizeof (buf) - 1, "%d", no_line);
	}
      else
	{
	  snprintf (buf, sizeof (buf) - 1, "%d", end);
	}
      nv_add_nvp (res, "end", buf);
    }
  snprintf (buf, sizeof (buf) - 1, "%d", no_line);
  nv_add_nvp (res, "total", buf);

  return ERR_NO_ERROR;
}

int
ts_reset_log (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char *path;
  FILE *outfile;

  path = nv_get_val (req, "path");
  if (path == NULL)
    {
      strcpy (_dbmt_error, "filepath");
      return ERR_PARAM_MISSING;
    }

  outfile = fopen (path, "w");
  if (outfile == NULL)
    {
      strcpy (_dbmt_error, path);
      return ERR_FILE_OPEN_FAIL;
    }
  fclose (outfile);

  return ERR_NO_ERROR;
}



int
ts_get_auto_add_vol (nvplist * req, nvplist * res, char *_dbmt_error)
{
  FILE *infile = NULL;
  char *dbname;
  char strbuf[1024], file[PATH_MAX];
  char *conf_item[AUTOADDVOL_CONF_ENTRY_NUM];
  int i;

  if ((dbname = nv_get_val (req, "_DBNAME")) == NULL)
    {
      sprintf (_dbmt_error, "%s", "database name");
      return ERR_PARAM_MISSING;
    }

  nv_add_nvp (res, autoaddvol_conf_entry[1], "OFF");
  nv_add_nvp (res, autoaddvol_conf_entry[2], "0.0");
  nv_add_nvp (res, autoaddvol_conf_entry[3], "0");
  nv_add_nvp (res, autoaddvol_conf_entry[4], "OFF");
  nv_add_nvp (res, autoaddvol_conf_entry[5], "0.0");
  nv_add_nvp (res, autoaddvol_conf_entry[6], "0");

  infile = fopen (conf_get_dbmt_file (FID_AUTO_ADDVOLDB_CONF, file), "r");
  if (infile == NULL)
    return ERR_NO_ERROR;

  while (fgets (strbuf, sizeof (strbuf), infile))
    {
      ut_trim (strbuf);
      if (strbuf[0] == '#')
	continue;
      if (string_tokenize (strbuf, conf_item, AUTOADDVOL_CONF_ENTRY_NUM) < 0)
	continue;
      if (strcmp (conf_item[0], dbname) == 0)
	{
	  for (i = 1; i < AUTOADDVOL_CONF_ENTRY_NUM; i++)
	    nv_update_val (res, autoaddvol_conf_entry[i], conf_item[i]);
	  break;
	}
    }
  fclose (infile);

  return ERR_NO_ERROR;
}

int
ts_set_auto_add_vol (nvplist * req, nvplist * res, char *_dbmt_error)
{
  FILE *infile, *outfile;
  char line[1024], tmpfile[PATH_MAX];
  char auto_addvol_conf_file[PATH_MAX];
  char *conf_item[AUTOADDVOL_CONF_ENTRY_NUM];
  int i;

  if ((conf_item[0] = nv_get_val (req, "_DBNAME")) == NULL)
    {
      sprintf (_dbmt_error, "%s", "database name");
      return ERR_PARAM_MISSING;
    }

  for (i = 1; i < AUTOADDVOL_CONF_ENTRY_NUM; i++)
    {
      conf_item[i] = nv_get_val (req, autoaddvol_conf_entry[i]);
      if (conf_item[i] == NULL)
	{
	  strcpy (_dbmt_error, autoaddvol_conf_entry[i]);
	  return ERR_PARAM_MISSING;
	}
    }

  conf_get_dbmt_file (FID_AUTO_ADDVOLDB_CONF, auto_addvol_conf_file);
  if (access (auto_addvol_conf_file, F_OK) < 0)
    {
      outfile = fopen (auto_addvol_conf_file, "w");
      if (outfile == NULL)
	{
	  strcpy (_dbmt_error, auto_addvol_conf_file);
	  return ERR_FILE_OPEN_FAIL;
	}
      for (i = 0; i < AUTOADDVOL_CONF_ENTRY_NUM; i++)
	fprintf (outfile, "%s ", conf_item[i]);
      fprintf (outfile, "\n");
      fclose (outfile);
      return ERR_NO_ERROR;
    }

  infile = fopen (auto_addvol_conf_file, "r");
  if (infile == NULL)
    {
      strcpy (_dbmt_error, auto_addvol_conf_file);
      return ERR_FILE_OPEN_FAIL;
    }
  snprintf (tmpfile, PATH_MAX - 1, "%s/DBMT_task_045.%d", sco.dbmt_tmp_dir,
	    (int) getpid ());
  outfile = fopen (tmpfile, "w");
  if (outfile == NULL)
    {
      fclose (infile);
      return ERR_TMPFILE_OPEN_FAIL;
    }

  while (fgets (line, sizeof (line), infile))
    {
      char conf_dbname[128];

      if (sscanf (line, "%127s", conf_dbname) < 1)
	continue;

      if (strcmp (conf_dbname, conf_item[0]) != 0)
	{
	  fputs (line, outfile);
	}
    }
  for (i = 0; i < AUTOADDVOL_CONF_ENTRY_NUM; i++)
    fprintf (outfile, "%s ", conf_item[i]);
  fprintf (outfile, "\n");
  fclose (infile);
  fclose (outfile);

  move_file (tmpfile, auto_addvol_conf_file);

  return ERR_NO_ERROR;
}

int
ts_get_addvol_status (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char *dbname = NULL;
  char dbdir[PATH_MAX];

  if ((dbname = nv_get_val (req, "_DBNAME")) == NULL)
    {
      sprintf (_dbmt_error, "%s", "database name");
      return ERR_PARAM_MISSING;
    }

  if (uRetrieveDBDirectory (dbname, dbdir) != ERR_NO_ERROR)
    {
      strcpy (_dbmt_error, dbname);
      return ERR_DBDIRNAME_NULL;
    }

  nv_add_nvp_int (res, "freespace", ut_disk_free_space (dbdir));
  nv_add_nvp (res, "volpath", dbdir);
  return ERR_NO_ERROR;
}

int
ts_get_tran_info (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char *dbname, *dbpasswd;
  char buf[1024], tmpfile[PATH_MAX];
  FILE *infile;
  char *tok[5];
  char cmd_name[CUBRID_CMD_NAME_LEN];
  const char *argv[10];
  int argc = 0;
  int retval;

  if ((dbname = nv_get_val (req, "dbname")) == NULL)
    {
      strcpy (_dbmt_error, "database name");
      return ERR_PARAM_MISSING;
    }
  dbpasswd = nv_get_val (req, "_DBPASSWD");

  snprintf (tmpfile, PATH_MAX - 1, "%s/DBMT_task_%d.%d", sco.dbmt_tmp_dir,
	    TS_GETTRANINFO, (int) getpid ());
  cubrid_cmd_name (cmd_name);
  argv[argc++] = cmd_name;
  argv[argc++] = UTIL_OPTION_KILLTRAN;
  if (dbpasswd != NULL)
    {
      argv[argc++] = "--" KILLTRAN_DBA_PASSWORD_L;
      argv[argc++] = dbpasswd;
    }
  argv[argc++] = dbname;
  argv[argc++] = NULL;

  retval = run_child (argv, 1, NULL, tmpfile, NULL, NULL);	/* killtran */
  if (retval < 0)
    {
      strcpy (_dbmt_error, argv[0]);
      return ERR_SYSTEM_CALL;
    }
  if ((infile = fopen (tmpfile, "rt")) == NULL)
    {
      return ERR_TMPFILE_OPEN_FAIL;
    }
  nv_add_nvp (res, "dbname", dbname);
  nv_add_nvp (res, "open", "transactioninfo");
  while (fgets (buf, sizeof (buf), infile))
    {
      if (buf[0] == '-')
	{
	  break;
	}
    }
  while (fgets (buf, sizeof (buf), infile))
    {
      ut_trim (buf);
      if (buf[0] == '-')
	{
	  break;
	}
      if (string_tokenize (buf, tok, 5) < 0)
	{
	  continue;
	}
      nv_add_nvp (res, "open", "transaction");
      nv_add_nvp (res, "tranindex", tok[0]);
      nv_add_nvp (res, "user", tok[1]);
      nv_add_nvp (res, "host", tok[2]);
      nv_add_nvp (res, "pid", tok[3]);
      nv_add_nvp (res, "program", tok[4]);
      nv_add_nvp (res, "close", "transaction");
    }
  nv_add_nvp (res, "close", "transactioninfo");
  fclose (infile);
  unlink (tmpfile);

  return ERR_NO_ERROR;
}

int
ts_killtran (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char *dbname, *dbpasswd, *type, *val;
  char param[256];
  char cmd_name[CUBRID_CMD_NAME_LEN];
  const char *argv[10];
  int argc = 0;
  int exit_code = 0;

  if ((dbname = nv_get_val (req, "dbname")) == NULL)
    return ERR_PARAM_MISSING;
  dbpasswd = nv_get_val (req, "_DBPASSWD");
  if ((type = nv_get_val (req, "type")) == NULL)
    return ERR_PARAM_MISSING;
  if ((val = nv_get_val (req, "parameter")) == NULL)
    return ERR_PARAM_MISSING;

  strncpy (param, val, sizeof (param) - 1);
  param[sizeof (param) - 1] = '\0';

  cubrid_cmd_name (cmd_name);
  argv[argc++] = cmd_name;
  argv[argc++] = UTIL_OPTION_KILLTRAN;
  if (dbpasswd != NULL)
    {
      argv[argc++] = "--" KILLTRAN_DBA_PASSWORD_L;
      argv[argc++] = dbpasswd;
    }
  if (strcmp (type, "t") == 0)
    {
      /* remove (+) from formated string such as "1(+) | 1(-)" */
      char *p = strstr (param, "(");
      if (p != NULL)
	*p = '\0';

      argv[argc++] = "--" KILLTRAN_KILL_TRANSACTION_INDEX_L;
    }
  else if (strcmp (type, "u") == 0)
    {
      argv[argc++] = "--" KILLTRAN_KILL_USER_NAME_L;
    }
  else if (strcmp (type, "h") == 0)
    {
      argv[argc++] = "--" KILLTRAN_KILL_HOST_NAME_L;
    }
  else if (strcmp (type, "pg") == 0)
    {
      argv[argc++] = "--" KILLTRAN_KILL_PROGRAM_NAME_L;
    }

  argv[argc++] = param;
  argv[argc++] = "--" KILLTRAN_FORCE_L;
  argv[argc++] = dbname;
  argv[argc++] = NULL;

  if (run_child (argv, 1, NULL, NULL, NULL, &exit_code) < 0)
    {				/* killtran */
      strcpy (_dbmt_error, argv[0]);
      return ERR_SYSTEM_CALL;
    }

  if (exit_code != EXIT_SUCCESS)
    {
      snprintf (_dbmt_error, DBMT_ERROR_MSG_SIZE,
		"Error killtran : abnormal exit. exit code: %d", exit_code);
      return ERR_WITH_MSG;
    }

  ts_get_tran_info (req, res, _dbmt_error);
  return ERR_NO_ERROR;
}

int
ts_lockdb (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char buf[1024], tmpfile[PATH_MAX], tmpfile2[PATH_MAX], s[32];
  char *dbname;
  FILE *infile, *outfile;
  char cmd_name[CUBRID_CMD_NAME_LEN];
  const char *argv[6];

  dbname = nv_get_val (req, "dbname");
  snprintf (tmpfile, PATH_MAX - 1, "%s/DBMT_task_%d_1.%d", sco.dbmt_tmp_dir,
	    TS_LOCKDB, (int) getpid ());

  cubrid_cmd_name (cmd_name);
  argv[0] = cmd_name;
  argv[1] = UTIL_OPTION_LOCKDB;
  argv[2] = "--" LOCK_OUTPUT_FILE_L;
  argv[3] = tmpfile;
  argv[4] = dbname;
  argv[5] = NULL;

  if (run_child (argv, 1, NULL, NULL, NULL, NULL) < 0)
    {				/* lockdb */
      strcpy (_dbmt_error, argv[0]);
      return ERR_SYSTEM_CALL;
    }

  /* create file that remove line feed at existed outputfile */
  snprintf (tmpfile2, PATH_MAX - 1, "%s/DBMT_task_%d_2.%d", sco.dbmt_tmp_dir,
	    TS_LOCKDB, (int) getpid ());

  infile = fopen (tmpfile, "rt");
  if (infile == NULL)
    {
      return ERR_TMPFILE_OPEN_FAIL;
    }

  outfile = fopen (tmpfile2, "w");
  if (outfile == NULL)
    {
      return ERR_TMPFILE_OPEN_FAIL;
    }

  while (fgets (buf, sizeof (buf), infile))
    {
      if (sscanf (buf, "%31s", s) == 1)
	{
	  fputs (buf, outfile);
	}
    }
  fclose (infile);
  fclose (outfile);
  unlink (tmpfile);
  if ((infile = fopen (tmpfile2, "rt")) == NULL)
    {
      return ERR_TMPFILE_OPEN_FAIL;
    }


  if (_ts_lockdb_parse_us (res, infile) < 0)
    {
      /* parshing error */
      strcpy (_dbmt_error,
	      "Lockdb operation has been failed(Unexpected state).");
      fclose (infile);
      unlink (tmpfile2);
      return ERR_WITH_MSG;
    }

  fclose (infile);
  unlink (tmpfile2);

  return ERR_NO_ERROR;
}

int
ts_get_backup_list (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char buf[1024], file[PATH_MAX], s1[256], s2[256], *dbname, log_dir[512];
  FILE *infile;
  int lv = -1;

  dbname = nv_get_val (req, "dbname");
  if (dbname == NULL)
    {
      strcpy (_dbmt_error, "database name");
      return ERR_PARAM_MISSING;
    }

  if (uRetrieveDBLogDirectory (dbname, log_dir) != ERR_NO_ERROR)
    {
      strcpy (_dbmt_error, dbname);
      return ERR_DBDIRNAME_NULL;
    }

  snprintf (file, PATH_MAX - 1, "%s/%s%s", log_dir, dbname,
	    CUBRID_BACKUP_INFO_EXT);
  if ((infile = fopen (file, "rt")) != NULL)
    {
      while (fgets (buf, sizeof (buf), infile))
	{
	  sscanf (buf, "%255s %*s %255s", s1, s2);
	  lv = atoi (s1);
	  snprintf (buf, sizeof (buf) - 1, "level%d", lv);
	  nv_add_nvp (res, buf, s2);
	}
      fclose (infile);
    }
  for (lv++; lv <= 2; lv++)
    {
      snprintf (buf, sizeof (buf) - 1, "level%d", lv);
      nv_add_nvp (res, buf, "none");
    }

  return ERR_NO_ERROR;
}

int
ts_load_access_log (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char buf[1024], time[256], file[PATH_MAX];
  FILE *infile;
  char *tok[5];

  conf_get_dbmt_file (FID_FSERVER_ACCESS_LOG, file);
  nv_add_nvp (res, "open", "accesslog");
  if ((infile = fopen (file, "rt")) != NULL)
    {
      while (fgets (buf, sizeof (buf), infile) != NULL)
	{
	  ut_trim (buf);
	  if (string_tokenize (buf, tok, 5) < 0)
	    {
	      continue;
	    }
	  nv_add_nvp (res, "user", tok[2]);
	  nv_add_nvp (res, "taskname", tok[4]);
	  snprintf (time, sizeof (time) - 1, "%s %s", tok[0], tok[1]);
	  nv_add_nvp (res, "time", time);
	}
      fclose (infile);
    }

  nv_add_nvp (res, "close", "accesslog");
  conf_get_dbmt_file (FID_FSERVER_ERROR_LOG, file);
  nv_add_nvp (res, "open", "errorlog");
  if ((infile = fopen (file, "rt")) != NULL)
    {
      while (fgets (buf, sizeof (buf), infile) != NULL)
	{
	  ut_trim (buf);
	  if (string_tokenize (buf, tok, 5) < 0)
	    {
	      continue;
	    }
	  nv_add_nvp (res, "user", tok[2]);
	  nv_add_nvp (res, "taskname", tok[4]);
	  snprintf (time, sizeof (time) - 1, "%s %s", tok[0], tok[1]);
	  nv_add_nvp (res, "time", time);
	  nv_add_nvp (res, "errornote", tok[4] + strlen (tok[4]) + 1);
	}
      fclose (infile);
    }
  nv_add_nvp (res, "close", "errorlog");
  return ERR_NO_ERROR;
}

int
ts_delete_access_log (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char file[PATH_MAX];

  conf_get_dbmt_file (FID_FSERVER_ACCESS_LOG, file);
  unlink (file);

  return ERR_NO_ERROR;
}

int
tsGetAutoaddvolLog (nvplist * req, nvplist * res, char *_dbmt_error)
{
  FILE *infile;
  char strbuf[1024];
  char dbname[512];
  char volname[512];
  char purpose[512];
  char page[512];
  char time[512];
  char outcome[512];
  char file[PATH_MAX];

  infile = fopen (conf_get_dbmt_file (FID_AUTO_ADDVOLDB_LOG, file), "r");
  if (infile != NULL)
    {
      while (fgets (strbuf, sizeof (strbuf), infile))
	{
	  uRemoveCRLF (strbuf);
	  sscanf (strbuf, "%511s %511s %511s %511s %511s %511s", dbname,
		  volname, purpose, page, time, outcome);

	  nv_add_nvp (res, "open", "log");
	  nv_add_nvp (res, "dbname", dbname);
	  nv_add_nvp (res, "volname", volname);
	  nv_add_nvp (res, "purpose", purpose);
	  nv_add_nvp (res, "page", page);
	  nv_add_nvp (res, "time", time);
	  nv_add_nvp (res, "outcome", outcome);
	  nv_add_nvp (res, "close", "log");
	}
      fclose (infile);
    }
  return ERR_NO_ERROR;
}

int
ts_delete_error_log (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char file[PATH_MAX];

  conf_get_dbmt_file (FID_FSERVER_ERROR_LOG, file);
  unlink (file);

  return ERR_NO_ERROR;
}

int
ts_check_dir (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char *n, *v;
  int i;
  for (i = 0; i < req->nvplist_leng; i++)
    {
      nv_lookup (req, i, &n, &v);
      if ((n != NULL) && (strcmp (n, "dir") == 0))
	{
	  if ((v == NULL) || (access (v, F_OK) < 0))
	    nv_add_nvp (res, "noexist", v);
	}
    }

  return ERR_NO_ERROR;
}

int
ts_check_file (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char *n, *v;
  int i;
  for (i = 0; i < req->nvplist_leng; i++)
    {
      nv_lookup (req, i, &n, &v);
      if ((n != NULL) && (strcmp (n, "file") == 0))
	{

	  if ((v != NULL) && (access (v, F_OK) == 0))
	    nv_add_nvp (res, "existfile", v);
	}
    }

  return ERR_NO_ERROR;
}

int
ts_get_autobackupdb_error_log (nvplist * req, nvplist * res,
			       char *_dbmt_error)
{
  char buf[1024], logfile[PATH_MAX], s1[256], s2[256], time[512], dbname[256],
    backupid[256];
  FILE *infile;

  snprintf (logfile, PATH_MAX - 1, "%s/log/manager/auto_backupdb.log",
	    sco.szCubrid);
  if ((infile = fopen (logfile, "r")) == NULL)
    {
      return ERR_NO_ERROR;
    }

  while (fgets (buf, sizeof (buf), infile))
    {
      if (sscanf (buf, "%255s %255s", s1, s2) != 2)
	{
	  continue;
	}
      if (!strncmp (s1, "DATE:", 5))
	{
	  snprintf (time, sizeof (time) - 1, "%s %s", s1 + 5, s2 + 5);
	  if (fgets (buf, sizeof (buf), infile) == NULL)
	    {
	      break;
	    }
	  sscanf (buf, "%255s %255s", s1, s2);
	  snprintf (dbname, sizeof (dbname) - 1, "%s", s1 + 7);
	  snprintf (backupid, sizeof (backupid) - 1, "%s", s2 + 9);
	  if (fgets (buf, sizeof (buf), infile) == NULL)
	    {
	      break;
	    }
	  uRemoveCRLF (buf);
	  nv_add_nvp (res, "open", "error");
	  nv_add_nvp (res, "dbname", dbname);
	  nv_add_nvp (res, "backupid", backupid);
	  nv_add_nvp (res, "error_time", time);
	  nv_add_nvp (res, "error_desc", buf + 3);
	  nv_add_nvp (res, "close", "error");
	}
    }
  fclose (infile);

  return ERR_NO_ERROR;
}

int
ts_get_autoexecquery_error_log (nvplist * req, nvplist * res,
				char *_dbmt_error)
{
  char buf[1024], logfile[PATH_MAX], s1[256], s2[256], s3[256], s4[256],
    time[512], dbname[256], username[256], query_id[256], error_code[256];
  FILE *infile;

  snprintf (logfile, PATH_MAX - 1, "%s/log/manager/auto_execquery.log",
	    sco.szCubrid);
  if ((infile = fopen (logfile, "r")) == NULL)
    return ERR_NO_ERROR;

  while (fgets (buf, sizeof (buf), infile))
    {
      if (sscanf (buf, "%255s %255s", s1, s2) != 2)
	{
	  continue;
	}
      if (!strncmp (s1, "DATE:", 5))
	{
	  snprintf (time, sizeof (time) - 1, "%s %s", s1 + 5, s2 + 5);	/* 5 = strlen("DATE:"); 5 = strlen("TIME:"); */
	  if (fgets (buf, sizeof (buf), infile) == NULL)
	    {
	      break;
	    }

	  s3[0] = 0;
	  sscanf (buf, "%255s %255s %255s %255s", s1, s2, s3, s4);
	  snprintf (dbname, sizeof (dbname) - 1, "%s", s1 + 7);	/* 7 = strlen("DBNAME:") */
	  snprintf (username, sizeof (username) - 1, "%s", s2 + 14);	/* 14 = strlen("EMGR-USERNAME:") */
	  snprintf (query_id, sizeof (query_id) - 1, "%s", s3 + 9);	/* 9 = strlen("QUERY-ID:") */
	  snprintf (error_code, sizeof (error_code) - 1, "%s", s4 + 11);	/* 11 = strlen("ERROR-CODE:") */
	  if (fgets (buf, sizeof (buf), infile) == NULL)
	    {
	      break;
	    }

	  uRemoveCRLF (buf);
	  nv_add_nvp (res, "open", "error");
	  nv_add_nvp (res, "dbname", dbname);
	  nv_add_nvp (res, "username", username);
	  nv_add_nvp (res, "query_id", query_id);
	  nv_add_nvp (res, "error_time", time);
	  nv_add_nvp (res, "error_code", error_code);
	  nv_add_nvp (res, "error_desc", buf + 3);
	  nv_add_nvp (res, "close", "error");
	}
    }

  return ERR_NO_ERROR;
}

int
ts_trigger_operation (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char *task, *dbname, *dbuser, *dbpasswd;
  char dbname_at_hostname[MAXHOSTNAMELEN + DB_NAME_LEN];
  int ha_mode = 0;
  char cmd_name[CUBRID_CMD_NAME_LEN];
  const char *argv[11];
  char input_file[PATH_MAX];
  char *cubrid_err_file;
  int retval, argc;
  T_DB_SERVICE_MODE db_mode;

  input_file[0] = '\0';
  task = nv_get_val (req, "task");
  if (task != NULL)
    {
      if (strcmp (task, "addtrigger") == 0)
	{
	  snprintf (input_file, PATH_MAX - 1, "%s/dbmt_task_%d_%d",
		    sco.dbmt_tmp_dir, TS_ADDNEWTRIGGER, (int) getpid ());
	}
      else if (strcmp (task, "droptrigger") == 0)
	{
	  snprintf (input_file, PATH_MAX - 1, "%s/dbmt_task_%d_%d",
		    sco.dbmt_tmp_dir, TS_DROPTRIGGER, (int) getpid ());
	}
      else if (strcmp (task, "altertrigger") == 0)
	{
	  snprintf (input_file, PATH_MAX - 1, "%s/dbmt_task_%d_%d",
		    sco.dbmt_tmp_dir, TS_ALTERTRIGGER, (int) getpid ());
	}
    }

  dbname = nv_get_val (req, "_DBNAME");
  dbuser = nv_get_val (req, "_DBID");
  dbpasswd = nv_get_val (req, "_DBPASSWD");

  cmd_name[0] = '\0';
  snprintf (cmd_name, sizeof (cmd_name) - 1, "%s/%s%s", sco.szCubrid,
	    CUBRID_DIR_BIN, UTIL_CSQL_NAME);
  argc = 0;
  argv[argc++] = cmd_name;

  db_mode = uDatabaseMode (dbname, &ha_mode);
  if (ha_mode != 0)
    {
      append_host_to_dbname (dbname_at_hostname, dbname,
			     sizeof (dbname_at_hostname));
      argv[argc++] = dbname_at_hostname;
    }
  else
    {
      argv[argc++] = dbname;
    }

  argv[argc++] = NULL;
  argv[argc++] = "--" CSQL_INPUT_FILE_L;
  argv[argc++] = input_file;
  if (dbuser)
    {
      argv[argc++] = "--" CSQL_USER_L;
      argv[argc++] = dbuser;
      if (dbpasswd)
	{
	  argv[argc++] = "--" CSQL_PASSWORD_L;
	  argv[argc++] = dbpasswd;
	}
    }

  argv[argc++] = "--" CSQL_NO_AUTO_COMMIT_L;

  for (; argc < 11; argc++)
    {
      argv[argc] = NULL;
    }

  if (!_isRegisteredDB (dbname))
    {
      return ERR_DB_NONEXISTANT;
    }

  if (db_mode == DB_SERVICE_MODE_SA)
    {
      sprintf (_dbmt_error, "%s", dbname);
      return ERR_STANDALONE_MODE;
    }

  if (db_mode == DB_SERVICE_MODE_CS)
    {
      /* run csql command with -cs option */
      argv[2] = "--" CSQL_CS_MODE_L;
    }

  if (db_mode == DB_SERVICE_MODE_NONE)
    {
      /* run csql command with -sa option */
      argv[2] = "--" CSQL_SA_MODE_L;
    }

  /* csql -sa -i input_file dbname  */
  if (task != NULL)
    {
      if (strcmp (task, "addtrigger") == 0)
	{
	  if (op_make_triggerinput_file_add (req, input_file) == 0)
	    {
	      strcpy (_dbmt_error, argv[0]);
	      return ERR_TMPFILE_OPEN_FAIL;
	    }
	}
      else if (strcmp (task, "droptrigger") == 0)
	{
	  if (op_make_triggerinput_file_drop (req, input_file) == 0)
	    {
	      strcpy (_dbmt_error, argv[0]);
	      return ERR_TMPFILE_OPEN_FAIL;
	    }
	}
      else if (strcmp (task, "altertrigger") == 0)
	{
	  if (op_make_triggerinput_file_alter (req, input_file) == 0)
	    {
	      strcpy (_dbmt_error, argv[0]);
	      return ERR_TMPFILE_OPEN_FAIL;
	    }
	}
    }

  INIT_CUBRID_ERROR_FILE (cubrid_err_file);
  SET_TRANSACTION_NO_WAIT_MODE_ENV ();

  retval = run_child (argv, 1, NULL, NULL, cubrid_err_file, NULL);	/* csql - trigger */
  if (strlen (input_file) > 0)
    {
      unlink (input_file);
    }

  if (read_csql_error_file (cubrid_err_file, _dbmt_error, DBMT_ERROR_MSG_SIZE)
      < 0)
    {
      return ERR_WITH_MSG;
    }

  if (retval < 0)
    {
      strcpy (_dbmt_error, argv[0]);
      return ERR_SYSTEM_CALL;
    }

  nv_add_nvp (res, "dbname", dbname);

  return ERR_NO_ERROR;
}

int
ts_get_triggerinfo (nvplist * req, nvplist * res, char *_dbmt_error)
{
  return cm_ts_get_triggerinfo (req, res, _dbmt_error);
}


int
ts_set_autoexec_query (nvplist * req, nvplist * res, char *_dbmt_error)
{
  FILE *conf_file, *temp_file;
  char line[MAX_JOB_CONFIG_FILE_LINE_LENGTH], tmpfile[PATH_MAX];
  char auto_exec_query_conf_file[PATH_MAX];
  char *conf_item[AUTOEXECQUERY_CONF_ENTRY_NUM];
  int i, index, length;
  char *name, *value;

  if ((conf_item[0] = nv_get_val (req, "_DBNAME")) == NULL)
    {
      sprintf (_dbmt_error, "%s", "database name");
      return ERR_PARAM_MISSING;
    }

  if ((conf_item[2] = nv_get_val (req, "_ID")) == NULL)
    {
      sprintf (_dbmt_error, "%s", "database user");
      return ERR_PARAM_MISSING;
    }

  conf_get_dbmt_file (FID_AUTO_EXECQUERY_CONF, auto_exec_query_conf_file);
  nv_locate (req, "planlist", &index, &length);

  /* check query string length */
  for (i = index + 1; i < index + length; i += 6)
    {
      /* get query string */
      nv_lookup (req, i + 3, &name, &value);
      if ((name == NULL) || (strcmp (name, "query_string") != 0))
	{
	  sprintf (_dbmt_error, "%s",
		   "nv order error. [i+3] must is [query_string]");
	  return ERR_WITH_MSG;
	}

      if ((value != NULL) && (strlen (value) > MAX_AUTOQUERY_SCRIPT_SIZE))
	{
	  /* error handle */
	  sprintf (_dbmt_error,
		   "query script too long. do not exceed MAX_AUTOQUERY_SCRIPT_SIZE(%d) bytes.",
		   MAX_AUTOQUERY_SCRIPT_SIZE);
	  return ERR_WITH_MSG;
	}
    }

  conf_file = temp_file = 0;
  name = value = NULL;

  /* open conf file */
  if (access (auto_exec_query_conf_file, F_OK) == 0)
    {
      /* file is existed */
      conf_file = fopen (auto_exec_query_conf_file, "r");
      if (!conf_file)
	{
	  return ERR_FILE_OPEN_FAIL;
	}
    }
  else
    {
      conf_file = fopen (auto_exec_query_conf_file, "w+");
    }

  /* temp file open */
  snprintf (tmpfile, PATH_MAX - 1, "%s/DBMT_task_045.%d", sco.dbmt_tmp_dir,
	    (int) getpid ());
  temp_file = fopen (tmpfile, "w");

  if (temp_file == NULL)
    {
      return ERR_TMPFILE_OPEN_FAIL;
    }

  if (conf_file)
    {
      char username[DBMT_USER_NAME_LEN];
      char db_name[64];
      char scan_format[128];

      snprintf (scan_format, sizeof (scan_format) - 1, "%%%lus %%*s %%%lus",
		(unsigned long) sizeof (db_name) - 1,
		(unsigned long) sizeof (username) - 1);

      while (fgets (line, sizeof (line), conf_file))
	{
	  if (sscanf (line, scan_format, db_name, username) < 1)
	    {
	      continue;
	    }
	  if ((strcmp (username, conf_item[2]) != 0) ||
	      (strcmp (db_name, conf_item[0]) != 0))
	    {
	      /* write temp file if username or dbname is different */
	      fputs (line, temp_file);
	    }
	}
      fclose (conf_file);
    }

  fclose (temp_file);
  move_file (tmpfile, auto_exec_query_conf_file);

  if (length == 0)
    {
      /* open:planlist        *
       * close:planlist       */
      return ERR_NO_ERROR;
    }

  for (i = index + 1; i < index + length; i += 6)
    {
      /* open:queryplan */
      int mem_alloc = 0;

      if (value)
	if (strcasecmp (value, "planlist") == 0)
	  {
	    return ERR_NO_ERROR;
	  }

      temp_file = fopen (tmpfile, "w");
      if (temp_file == NULL)
	{
	  return ERR_TMPFILE_OPEN_FAIL;
	}

      nv_lookup (req, i, &name, &value);
      conf_item[1] = value;
      nv_lookup (req, i + 1, &name, &value);
      conf_item[3] = value;
      nv_lookup (req, i + 2, &name, &value);
      conf_item[4] = value;
      nv_lookup (req, i + 3, &name, &value);
      conf_item[5] = value;

      if (strcasecmp (conf_item[1], "NEW_PLAN") == 0)
	{
	  /* new query plan */
	  /* allocate newer number and input */
	  conf_item[1] = (char *) MALLOC (sizeof (char) * 5);
	  op_auto_exec_query_get_newplan_id (conf_item[1],
					     auto_exec_query_conf_file);
	  mem_alloc = 1;
	}

      if (access (auto_exec_query_conf_file, F_OK) == 0)
	{
	  /* file is existed */
	  conf_file = fopen (auto_exec_query_conf_file, "r");
	  if (!conf_file)
	    {
	      return ERR_FILE_OPEN_FAIL;
	    }
	}

      if (conf_file)
	{
	  while (fgets (line, sizeof (line), conf_file))
	    {
	      char query_id[64];
	      if (sscanf (line, "%*s %63s ", query_id) < 1)
		{
		  continue;
		}
	      if (strcmp (query_id, conf_item[1]) != 0)
		{
		  /* write temp file if query_id is different */
		  fputs (line, temp_file);
		}
	    }
	  fclose (conf_file);
	}

      fprintf (temp_file, "%s %s %s %s %s %s\n", conf_item[0], conf_item[1],
	       conf_item[2], conf_item[3], conf_item[4], conf_item[5]);

      if (mem_alloc)
	{
	  FREE_MEM (conf_item[1]);
	}

      fclose (temp_file);
      move_file (tmpfile, auto_exec_query_conf_file);
      /* close:queryplan */
    }

  return ERR_NO_ERROR;
}

int
ts_get_autoexec_query (nvplist * req, nvplist * res, char *_dbmt_error)
{
  FILE *conf_file;
  char buf[MAX_JOB_CONFIG_FILE_LINE_LENGTH];
  char auto_exec_query_conf_file[PATH_MAX];
  char id_num[64], db_name[64], user[64], period[8];
  char detail1[32], detail2[8], query_string[MAX_AUTOQUERY_SCRIPT_SIZE + 1],
    detail[64];
  char *dbname, *dbmt_username;

  conf_get_dbmt_file (FID_AUTO_EXECQUERY_CONF, auto_exec_query_conf_file);
  dbname = nv_get_val (req, "_DBNAME");
  dbmt_username = nv_get_val (req, "_ID");

  nv_add_nvp (res, "dbname", dbname);
  if (access (auto_exec_query_conf_file, F_OK) == 0)
    {
      conf_file = fopen (auto_exec_query_conf_file, "r");
      if (!conf_file)
	{
	  return ERR_FILE_OPEN_FAIL;
	}
    }
  else
    {
      nv_add_nvp (res, "open", "planlist");
      nv_add_nvp (res, "close", "planlist");
      return ERR_NO_ERROR;
    }

  nv_add_nvp (res, "open", "planlist");

  if (dbname == NULL || dbmt_username == NULL)
    {
      goto finalize;
    }

  while (fgets (buf, sizeof (buf), conf_file))
    {
      int scan_matched;
      char *p;

      scan_matched =
	sscanf (buf, "%63s %63s %63s %7s %31s %7s", db_name, id_num, user,
		period, detail1, detail2);

      if (scan_matched != 6)
	{
	  continue;
	}
      if (strcmp (dbname, db_name) != 0)
	{
	  continue;
	}
      if (strcmp (dbmt_username, user) != 0)
	{
	  continue;
	}
      nv_add_nvp (res, "open", "queryplan");
      nv_add_nvp (res, "query_id", id_num);
      nv_add_nvp (res, "period", period);

      snprintf (detail, sizeof (detail) - 1, "%s %s", detail1, detail2);
      nv_add_nvp (res, "detail", detail);

      p = strstr (buf, detail2);
      p = p + strlen (detail2) + 1;
      strcpy (query_string, p);
      query_string[strlen (query_string) - 1] = '\0';
      nv_add_nvp (res, "query_string", query_string);
      nv_add_nvp (res, "close", "queryplan");
    }

finalize:
  nv_add_nvp (res, "close", "planlist");

  fclose (conf_file);
  return ERR_NO_ERROR;
}


int
ts_addstatustemplate (nvplist * cli_request, nvplist * cli_response,
		      char *diag_error)
{
  FILE *templatefile, *tempfile;
  char templatefilepath[PATH_MAX], tempfilepath[PATH_MAX];
  char buf[1024];
  char *templatename, *desc, *sampling_term, *dbname;
  int ret_val = ERR_NO_ERROR;

  templatename = nv_get_val (cli_request, "name");
  desc = nv_get_val (cli_request, "desc");
  sampling_term = nv_get_val (cli_request, "sampling_term");
  dbname = nv_get_val (cli_request, "db_name");

  if (templatename == NULL)
    {
      return ERR_PARAM_MISSING;
    }

  /* write related information to template config file */
  conf_get_dbmt_file (FID_DIAG_STATUS_TEMPLATE, templatefilepath);
  if (access (templatefilepath, F_OK) < 0)
    {
      templatefile = fopen (templatefilepath, "w");
      if (templatefile == NULL)
	{
	  if (diag_error)
	    {
	      strcpy (diag_error, templatefilepath);
	    }
	  return ERR_FILE_OPEN_FAIL;
	}
      fclose (templatefile);
    }

  if ((templatefile = fopen (templatefilepath, "r")) == NULL)
    {
      if (diag_error)
	{
	  strcpy (diag_error, templatefilepath);
	}
      return ERR_FILE_OPEN_FAIL;
    }

  snprintf (tempfilepath, PATH_MAX - 1, "%s/statustemplate_add.tmp",
	    sco.dbmt_tmp_dir);
  if ((tempfile = fopen (tempfilepath, "w+")) == NULL)
    {
      if (diag_error)
	{
	  strcpy (diag_error, tempfilepath);
	  fclose (templatefile);
	}
      return ERR_FILE_OPEN_FAIL;
    }

  while (fgets (buf, sizeof (buf), templatefile))
    {
      if (strncmp (buf, "<<<", 3) == 0)
	{
	  fprintf (tempfile, "%s", buf);

	  /* template name */
	  if (!(fgets (buf, sizeof (buf), templatefile)))
	    {
	      break;
	    }
	  buf[strlen (buf) - 1] = '\0';
	  if (strcmp (buf, templatename) == 0)
	    {
	      strcpy (diag_error, templatename);
	      ret_val = ERR_TEMPLATE_ALREADY_EXIST;
	      break;
	    }

	  fprintf (tempfile, "%s\n", buf);

	  /* copy others */
	  while (fgets (buf, sizeof (buf), templatefile))
	    {
	      fprintf (tempfile, "%s", buf);
	      if (strncmp (buf, ">>>", 3) == 0)
		{
		  break;
		}
	    }
	}
    }

  if (ret_val == ERR_NO_ERROR)
    {
      int i, config_index, config_length;
      char *target_name, *target_value;

      /* add new template config */
      fprintf (tempfile, "<<<\n");
      fprintf (tempfile, "%s\n", templatename);
      if (desc)
	{
	  fprintf (tempfile, "%s\n", desc);
	}
      else
	{
	  fprintf (tempfile, " \n");
	}

      if (dbname)
	{
	  fprintf (tempfile, "%s\n", dbname);
	}
      else
	{
	  fprintf (tempfile, " \n");
	}

      fprintf (tempfile, "%s\n", sampling_term);

      if (nv_locate
	  (cli_request, "target_config", &config_index, &config_length) != 1)
	{
	  ret_val = ERR_REQUEST_FORMAT;
	}
      else
	{
	  for (i = config_index; i < config_index + config_length; i++)
	    {
	      if (nv_lookup (cli_request, i, &target_name, &target_value) ==
		  1)
		{
		  fprintf (tempfile, "%s %s\n", target_name, target_value);
		}
	      else
		{
		  ret_val = ERR_REQUEST_FORMAT;
		  break;
		}

	    }

	  fprintf (tempfile, ">>>\n");
	}
    }

  fclose (tempfile);
  fclose (templatefile);

  if (ret_val == ERR_NO_ERROR)
    {
      unlink (templatefilepath);
      rename (tempfilepath, templatefilepath);
    }
  else
    {
      unlink (tempfilepath);
    }

  return ret_val;
}

int
ts_removestatustemplate (nvplist * cli_request, nvplist * cli_response,
			 char *diag_error)
{
  FILE *templatefile, *tempfile;
  char templatefilepath[PATH_MAX], tempfilepath[PATH_MAX];
  char buf[1024];
  char *templatename;
  int ret_val = ERR_NO_ERROR;

  templatename = nv_get_val (cli_request, "name");

  if (templatename == NULL)
    {
      return ERR_PARAM_MISSING;
    }

  /* write related information to template config file */
  conf_get_dbmt_file (FID_DIAG_STATUS_TEMPLATE, templatefilepath);
  if ((templatefile = fopen (templatefilepath, "r")) == NULL)
    {
      if (diag_error)
	{
	  strcpy (diag_error, templatefilepath);
	}
      return ERR_FILE_OPEN_FAIL;
    }

  snprintf (tempfilepath, PATH_MAX - 1, "%s/statustemplate_remove.tmp",
	    sco.dbmt_tmp_dir);
  if ((tempfile = fopen (tempfilepath, "w+")) == NULL)
    {
      if (diag_error)
	{
	  strcpy (diag_error, tempfilepath);
	  fclose (templatefile);
	}
      return ERR_FILE_OPEN_FAIL;
    }

  while (fgets (buf, sizeof (buf), templatefile))
    {
      if (strncmp (buf, "<<<", 3) == 0)
	{
	  /* template name */
	  if (!(fgets (buf, sizeof (buf), templatefile)))
	    {
	      break;
	    }
	  buf[strlen (buf) - 1] = '\0';
	  if (strcmp (buf, templatename) == 0)
	    {
	      continue;
	    }

	  fprintf (tempfile, "<<<\n");
	  fprintf (tempfile, "%s\n", buf);

	  /* copy others */
	  while (fgets (buf, sizeof (buf), templatefile))
	    {
	      fprintf (tempfile, "%s", buf);
	      if (strncmp (buf, ">>>", 3) == 0)
		{
		  break;
		}
	    }
	}
    }

  fclose (tempfile);
  fclose (templatefile);

  if (ret_val == ERR_NO_ERROR)
    {
      unlink (templatefilepath);
      rename (tempfilepath, templatefilepath);
    }
  else
    {
      unlink (tempfilepath);
    }

  return ret_val;
}

int
ts_updatestatustemplate (nvplist * cli_request, nvplist * cli_response,
			 char *diag_error)
{
  FILE *templatefile, *tempfile;
  char templatefilepath[PATH_MAX], tempfilepath[PATH_MAX];
  char buf[1024];
  char *templatename, *desc, *sampling_term, *dbname;
  int ret_val = ERR_NO_ERROR;

  templatename = nv_get_val (cli_request, "name");
  desc = nv_get_val (cli_request, "desc");
  sampling_term = nv_get_val (cli_request, "sampling_term");
  dbname = nv_get_val (cli_request, "db_name");

  if (templatename == NULL)
    {
      return ERR_PARAM_MISSING;
    }

  /* write related information to template config file */
  conf_get_dbmt_file (FID_DIAG_STATUS_TEMPLATE, templatefilepath);
  if ((templatefile = fopen (templatefilepath, "r")) == NULL)
    {
      if (diag_error)
	{
	  strcpy (diag_error, templatefilepath);
	}
      return ERR_FILE_OPEN_FAIL;
    }

  snprintf (tempfilepath, PATH_MAX - 1, "%s/statustemplate_update_%d.tmp",
	    sco.dbmt_tmp_dir, getpid ());
  if ((tempfile = fopen (tempfilepath, "w+")) == NULL)
    {
      if (diag_error)
	{
	  strcpy (diag_error, tempfilepath);
	  fclose (templatefile);
	}
      return ERR_FILE_OPEN_FAIL;
    }

  while (fgets (buf, sizeof (buf), templatefile))
    {
      if (strncmp (buf, "<<<", 3) == 0)
	{
	  fprintf (tempfile, "%s", buf);

	  /* template name */
	  if (!(fgets (buf, sizeof (buf), templatefile)))
	    {
	      break;
	    }
	  buf[strlen (buf) - 1] = '\0';
	  if (strcmp (buf, templatename) == 0)
	    {
	      int i, config_index, config_length;
	      char *target_name, *target_value;

	      /* add new configuration */
	      fprintf (tempfile, "%s\n", templatename);
	      if (desc)
		{
		  fprintf (tempfile, "%s\n", desc);
		}
	      else
		{
		  fprintf (tempfile, " \n");
		}

	      if (dbname)
		{
		  fprintf (tempfile, "%s\n", dbname);
		}
	      else
		{
		  fprintf (tempfile, " \n");
		}

	      fprintf (tempfile, "%s\n", sampling_term);

	      if (nv_locate
		  (cli_request, "target_config", &config_index,
		   &config_length) != 1)
		{
		  ret_val = ERR_REQUEST_FORMAT;
		  break;
		}
	      else
		{
		  for (i = config_index; i < config_index + config_length;
		       i++)
		    {
		      if (nv_lookup
			  (cli_request, i, &target_name, &target_value) == 1)
			{
			  fprintf (tempfile, "%s %s\n", target_name,
				   target_value);
			}
		      else
			{
			  ret_val = ERR_REQUEST_FORMAT;
			  break;
			}
		    }
		  if (ret_val != ERR_NO_ERROR)
		    {
		      break;
		    }
		  fprintf (tempfile, ">>>\n");
		}

	      continue;
	    }

	  fprintf (tempfile, "%s\n", buf);

	  /* copy others */
	  while (fgets (buf, sizeof (buf), templatefile))
	    {
	      fprintf (tempfile, "%s", buf);
	      if (strncmp (buf, ">>>", 3) == 0)
		{
		  break;
		}
	    }
	}
    }

  fclose (tempfile);
  fclose (templatefile);

  if (ret_val == ERR_NO_ERROR)
    {
      unlink (templatefilepath);
      rename (tempfilepath, templatefilepath);
    }
  else
    {
      unlink (tempfilepath);
    }

  return ret_val;
}

int
ts_getstatustemplate (nvplist * cli_request, nvplist * cli_response,
		      char *diag_error)
{
  FILE *templatefile;
  char templatefilepath[PATH_MAX];
  char buf[1024];
  char *templatename;
  char targetname[100], targetcolor[8], targetmag[32];
  int ret_val = ERR_NO_ERROR;

  templatename = nv_get_val (cli_request, "name");

  /* write related information to template config file */
  conf_get_dbmt_file (FID_DIAG_STATUS_TEMPLATE, templatefilepath);
  if ((templatefile = fopen (templatefilepath, "r")) == NULL)
    {
      return ERR_NO_ERROR;
    }

  nv_add_nvp (cli_response, "start", "templatelist");
  while (fgets (buf, sizeof (buf), templatefile))
    {
      if (strncmp (buf, "<<<", 3) == 0)
	{
	  /* template name */
	  if (!(fgets (buf, sizeof (buf), templatefile)))
	    {
	      break;
	    }
	  buf[strlen (buf) - 1] = '\0';
	  if (templatename)
	    {
	      if (strcmp (buf, templatename) != 0)
		{
		  continue;
		}
	    }

	  nv_add_nvp (cli_response, "start", "template");
	  nv_add_nvp (cli_response, "name", buf);
	  if (!fgets (buf, sizeof (buf), templatefile))
	    {
	      ret_val = ERR_WITH_MSG;
	      if (diag_error)
		{
		  strcpy (diag_error, "Invalid file format\n");
		  strcat (diag_error, templatefilepath);
		}

	      break;
	    }
	  buf[strlen (buf) - 1] = '\0';
	  nv_add_nvp (cli_response, "desc", buf);

	  if (!fgets (buf, sizeof (buf), templatefile))
	    {
	      ret_val = ERR_WITH_MSG;
	      if (diag_error)
		{
		  strcpy (diag_error, "Invalid file format\n");
		  strcat (diag_error, templatefilepath);
		}

	      break;
	    }
	  buf[strlen (buf) - 1] = '\0';
	  nv_add_nvp (cli_response, "db_name", buf);

	  if (!fgets (buf, sizeof (buf), templatefile))
	    {
	      ret_val = ERR_WITH_MSG;
	      if (diag_error)
		{
		  strcpy (diag_error, "Invalid file format\n");
		  strcat (diag_error, templatefilepath);
		}

	      break;
	    }
	  buf[strlen (buf) - 1] = '\0';
	  nv_add_nvp (cli_response, "sampling_term", buf);

	  nv_add_nvp (cli_response, "start", "target_config");
	  while (fgets (buf, sizeof (buf), templatefile))
	    {
	      int matched;
	      if (strncmp (buf, ">>>", 3) == 0)
		{
		  break;
		}
	      matched =
		sscanf (buf, "%99s %7s %31s", targetname, targetcolor,
			targetmag);
	      if (matched != 3)
		{
		  continue;	/* error file format */
		}
	      nv_add_nvp (cli_response, targetname, targetcolor);
	      nv_add_nvp (cli_response, targetname, targetmag);
	    }
	  nv_add_nvp (cli_response, "end", "target_config");

	  nv_add_nvp (cli_response, "end", "template");
	}
    }
  nv_add_nvp (cli_response, "end", "templatelist");

  fclose (templatefile);

  return ret_val;
}


int
ts_analyzecaslog (nvplist * cli_request, nvplist * cli_response,
		  char *diag_error)
{
  int retval, i, arg_index;
  int matched, sect, sect_len;
  char tmpfileQ[PATH_MAX], tmpfileRes[PATH_MAX], tmpfileT[PATH_MAX],
    tmpfileanalyzeresult[PATH_MAX];
  char *logfile, *option_t;
  char cmd_name[CUBRID_CMD_NAME_LEN];
  char *diag_err_file;
  const char *argv[256];
  char buf[1024], logbuf[2048];
  char qnum[16], max[32], min[32], avg[32], cnt[16], err[16];
  FILE *fdRes, *fdQ, *fdT, *fdAnalyzeResult;
#if defined(WINDOWS)
  DWORD th_id;
#else
  T_THREAD th_id;
#endif

  logfile = nv_get_val (cli_request, "logfile");
  option_t = nv_get_val (cli_request, "option_t");

  /* set prarameter with logfile and execute broker_log_top */
  /* execute at current directory and copy result to $CUBRID/tmp directory */
  snprintf (cmd_name, sizeof (cmd_name) - 1, "%s/bin/broker_log_top%s",
	    sco.szCubrid, DBMT_EXE_EXT);
  arg_index = 0;
  argv[arg_index++] = cmd_name;
  if (option_t && !strcmp (option_t, "yes"))
    {
      argv[arg_index++] = "-t";
    }
  nv_locate (cli_request, "logfilelist", &sect, &sect_len);
  if (sect == -1)
    {
      return ERR_PARAM_MISSING;
    }
  for (i = 0; i < sect_len; i++)
    {
      nv_lookup (cli_request, sect + i, NULL, &logfile);
      if (logfile)
	{
	  argv[arg_index++] = logfile;
	}
    }
  argv[arg_index++] = NULL;
  INIT_CUBRID_ERROR_FILE (diag_err_file);

  retval = run_child (argv, 1, NULL, NULL, diag_err_file, NULL);	/* broker_log_top */
  if (read_error_file (diag_err_file, diag_error, DBMT_ERROR_MSG_SIZE) < 0)
    {
      return ERR_WITH_MSG;
    }
  if (retval < 0)
    {
      if (diag_error)
	{
	  strcpy (diag_error, argv[0]);
	}
      return ERR_SYSTEM_CALL;
    }

  snprintf (tmpfileanalyzeresult, PATH_MAX - 1, "%s/analyzelog_%d.res",
	    sco.dbmt_tmp_dir, (int) getpid ());
  fdAnalyzeResult = fopen (tmpfileanalyzeresult, "w+");
  if (fdAnalyzeResult == NULL)
    {
      if (diag_error)
	{
	  strcpy (diag_error, "Tmpfile");
	}
      return ERR_FILE_OPEN_FAIL;
    }

  if ((option_t != NULL) && (strcmp (option_t, "yes") == 0))
    {
      int log_init_flag, log_index;

      snprintf (tmpfileT, PATH_MAX - 1, "%s/log_top_%d.t", sco.dbmt_tmp_dir,
		(int) getpid ());
      rename ("./log_top.t", tmpfileT);

      fdT = fopen (tmpfileT, "r");
      if (fdT == NULL)
	{
	  if (diag_error)
	    {
	      strcpy (diag_error, "log_top.t");
	    }
	  return ERR_FILE_OPEN_FAIL;
	}

      log_index = 1;
      log_init_flag = 1;

      nv_add_nvp (cli_response, "resultlist", "start");
      while (fgets (buf, sizeof (buf), fdT))
	{
	  if (strlen (buf) == 1)
	    {
	      continue;
	    }

	  if (log_init_flag == 1)
	    {
	      nv_add_nvp (cli_response, "result", "start");
	      snprintf (qnum, sizeof (qnum) - 1, "[Q%d]", log_index);
	      fprintf (fdAnalyzeResult, "%s\n", qnum);
	      nv_add_nvp (cli_response, "qindex", qnum);
	      log_index++;
	      log_init_flag = 0;
	    }

	  if (!strncmp (buf, "***", 3))
	    {
	      buf[strlen (buf) - 1] = '\0';
	      nv_add_nvp (cli_response, "exec_time", buf + 4);
	      nv_add_nvp (cli_response, "result", "end");
	      log_init_flag = 1;
	    }
	  else
	    {
	      fprintf (fdAnalyzeResult, "%s", buf);
	    }
	}

      nv_add_nvp (cli_response, "resultlist", "end");

      fclose (fdT);
      unlink (tmpfileT);
    }
  else
    {
#if defined(WINDOWS)
      th_id = GetCurrentThreadId ();
#else
      th_id = getpid ();
#endif
      snprintf (tmpfileQ, PATH_MAX - 1, "%s/log_top_%lu.q", sco.dbmt_tmp_dir,
		th_id);
      snprintf (tmpfileRes, PATH_MAX - 1, "%s/log_top_%lu.res",
		sco.dbmt_tmp_dir, th_id);

      rename ("./log_top.q", tmpfileQ);
      rename ("./log_top.res", tmpfileRes);

      fdQ = fopen (tmpfileQ, "r");
      if (fdQ == NULL)
	{
	  if (diag_error)
	    {
	      strcpy (diag_error, "log_top.q");
	    }
	  return ERR_FILE_OPEN_FAIL;
	}

      fdRes = fopen (tmpfileRes, "r");
      if (fdRes == NULL)
	{
	  if (diag_error)
	    {
	      strcpy (diag_error, "log_top.res");
	    }
	  return ERR_FILE_OPEN_FAIL;
	}

      memset (buf, '\0', sizeof (buf));
      memset (logbuf, '\0', sizeof (logbuf));

      nv_add_nvp (cli_response, "resultlist", "start");
      /* read result, log file and create msg with them */
      while (fgets (buf, sizeof (buf), fdRes))
	{
	  if (strlen (buf) == 1)
	    {
	      continue;
	    }

	  if (!strncmp (buf, "[Q", 2))
	    {
	      nv_add_nvp (cli_response, "result", "start");

	      matched =
		sscanf (buf, "%15s %31s %31s %31s %15s %15s", qnum, max, min,
			avg, cnt, err);
	      if (matched != 6)
		{
		  continue;
		}
	      nv_add_nvp (cli_response, "qindex", qnum);
	      nv_add_nvp (cli_response, "max", max);
	      nv_add_nvp (cli_response, "min", min);
	      nv_add_nvp (cli_response, "avg", avg);
	      nv_add_nvp (cli_response, "cnt", cnt);
	      err[strlen (err) - 1] = '\0';
	      nv_add_nvp (cli_response, "err", err + 1);

	      fprintf (fdAnalyzeResult, "%s\n", qnum);

	      while (strncmp (logbuf, qnum, 4) != 0)
		{
		  if (fgets (logbuf, sizeof (logbuf), fdQ) == NULL)
		    {
		      if (diag_error)
			{
			  strcpy (diag_error,
				  "log_top.q file format is not valid");
			}
		      return ERR_WITH_MSG;
		    }
		}

	      while (fgets (logbuf, sizeof (logbuf), fdQ))
		{
		  if (!strncmp (logbuf, "[Q", 2))
		    {
		      break;
		    }
		  fprintf (fdAnalyzeResult, "%s", logbuf);
		}
	      nv_add_nvp (cli_response, "result", "end");
	    }
	}

      nv_add_nvp (cli_response, "resultlist", "end");

      fclose (fdRes);
      fclose (fdQ);

      unlink (tmpfileQ);
      unlink (tmpfileRes);
    }

  fclose (fdAnalyzeResult);
  nv_add_nvp (cli_response, "resultfile", tmpfileanalyzeresult);

  return ERR_NO_ERROR;
}

int
ts_executecasrunner (nvplist * cli_request, nvplist * cli_response,
		     char *diag_error)
{
  int i, sect, sect_len;
  char *log_string, *brokername, *username, *passwd;
  char *num_thread, *repeat_count, *show_queryresult;
  char *dbname, *casrunnerwithFile, *logfilename;
  char *show_queryplan;
  char bport[6], buf[1024];
  FILE *flogfile, *fresfile2;
  char tmplogfilename[PATH_MAX], resfile[PATH_MAX], resfile2[PATH_MAX];
  char log_converter_res[PATH_MAX];
  char cmd_name[CUBRID_CMD_NAME_LEN];
  const char *argv[25];
  T_CM_BROKER_CONF uc_conf;
  char out_msg_file_env[1024];
  T_CM_ERROR error;
#if defined(WINDOWS)
  DWORD th_id;
#else
  T_THREAD th_id;
#endif
  char use_tmplogfile = FALSE;

  brokername = nv_get_val (cli_request, "brokername");
  dbname = nv_get_val (cli_request, "dbname");
  username = nv_get_val (cli_request, "username");
  passwd = nv_get_val (cli_request, "passwd");
  num_thread = nv_get_val (cli_request, "num_thread");
  repeat_count = nv_get_val (cli_request, "repeat_count");
  show_queryresult = nv_get_val (cli_request, "show_queryresult");
  show_queryplan = nv_get_val (cli_request, "show_queryplan");
  casrunnerwithFile = nv_get_val (cli_request, "executelogfile");
  logfilename = nv_get_val (cli_request, "logfile");

  if ((!brokername) || (!username) || (!dbname))
    {
      return ERR_PARAM_MISSING;
    }

#if defined(WINDOWS)
  th_id = GetCurrentThreadId ();
#else
  th_id = getpid ();
#endif

  snprintf (resfile, PATH_MAX - 1, "%s/log_run_%lu.res", sco.dbmt_tmp_dir,
	    th_id);
  snprintf (resfile2, PATH_MAX - 1, "%s/log_run_%lu.res2", sco.dbmt_tmp_dir,
	    th_id);

  /* get right port number with broker name */
  if (cm_get_broker_conf (&uc_conf, NULL, &error) < 0)
    {
      strcpy (diag_error, error.err_msg);
      return ERR_WITH_MSG;
    }

  memset (bport, 0x0, sizeof (bport));
  for (i = 0; i < uc_conf.num_broker; i++)
    {
      char *confvalue = cm_br_conf_get_value (&(uc_conf.br_conf[i]), "%");

      if ((confvalue != NULL) && (strcmp (brokername, confvalue) == 0))
	{
	  confvalue =
	    cm_br_conf_get_value (&(uc_conf.br_conf[i]), "BROKER_PORT");
	  if (confvalue != NULL)
	    {
	      snprintf (bport, sizeof (bport) - 1, "%s", confvalue);
	    }
	  break;
	}
    }

  cm_broker_conf_free (&uc_conf);

  if ((casrunnerwithFile != NULL) && (strcmp (casrunnerwithFile, "yes") == 0)
      && (logfilename != NULL))
    {
      snprintf (tmplogfilename, PATH_MAX - 1, "%s", logfilename);
    }
  else
    {
      use_tmplogfile = TRUE;
      /* create logfile */
      snprintf (tmplogfilename, PATH_MAX - 1, "%s/cas_log_tmp_%lu.q",
		sco.dbmt_tmp_dir, th_id);

      flogfile = fopen (tmplogfilename, "w+");
      if (!flogfile)
	{
	  return ERR_FILE_OPEN_FAIL;
	}

      nv_locate (cli_request, "logstring", &sect, &sect_len);
      if (sect >= 0)
	{
	  for (i = 0; i < sect_len; ++i)
	    {
	      nv_lookup (cli_request, sect + i, NULL, &log_string);
	      fprintf (flogfile, "%s\n",
		       (log_string == NULL) ? " " : log_string);
	    }
	}
      fclose (flogfile);
    }

  /* execute broker_log_converter why logfile is created */
  snprintf (log_converter_res, PATH_MAX - 1, "%s/log_converted_%lu.q_res",
	    sco.dbmt_tmp_dir, th_id);
  snprintf (cmd_name, sizeof (cmd_name) - 1, "%s/bin/broker_log_converter%s",
	    sco.szCubrid, DBMT_EXE_EXT);

  i = 0;
  argv[i] = cmd_name;
  argv[++i] = tmplogfilename;
  argv[++i] = log_converter_res;
  argv[++i] = NULL;

  if (run_child (argv, 1, NULL, NULL, NULL, NULL) < 0)
    {				/* broker_log_converter */
      strcpy (diag_error, argv[0]);
      return ERR_SYSTEM_CALL;
    }

  /* execute broker_log_runner through logfile that converted */
  snprintf (cmd_name, sizeof (cmd_name) - 1, "%s/bin/broker_log_runner%s",
	    sco.szCubrid, DBMT_EXE_EXT);
  i = 0;
  argv[i] = cmd_name;
  argv[++i] = "-I";
  argv[++i] = "localhost";
  argv[++i] = "-P";
  argv[++i] = bport;
  argv[++i] = "-d";
  argv[++i] = dbname;
  argv[++i] = "-u";
  argv[++i] = username;
  if (passwd)
    {
      argv[++i] = "-p";
      argv[++i] = passwd;
    }
  argv[++i] = "-t";
  argv[++i] = num_thread;
  argv[++i] = "-r";
  argv[++i] = repeat_count;
  if (show_queryplan && !strcmp (show_queryplan, "yes"))
    {
      argv[++i] = "-Q";
    }
  argv[++i] = "-o";
  argv[++i] = resfile;
  argv[++i] = log_converter_res;
  argv[++i] = NULL;

  snprintf (out_msg_file_env, sizeof (out_msg_file_env) - 1,
	    "CUBRID_MANAGER_OUT_MSG_FILE=%s", resfile2);
  putenv (out_msg_file_env);

  if (run_child (argv, 1, NULL, NULL, NULL, NULL) < 0)
    {				/* broker_log_runner */
      return ERR_SYSTEM_CALL;
    }

  /* create message with read file's content */
  nv_add_nvp (cli_response, "result_list", "start");
  fresfile2 = fopen (resfile2, "r");
  if (fresfile2)
    {
      while (fgets (buf, sizeof (buf), fresfile2))
	{
	  if (!strncmp (buf, "cas_ip", 6))
	    continue;
	  if (!strncmp (buf, "cas_port", 8))
	    continue;
	  if (!strncmp (buf, "num_thread", 10))
	    continue;
	  if (!strncmp (buf, "repeat", 6))
	    continue;
	  if (!strncmp (buf, "dbname", 6))
	    continue;
	  if (!strncmp (buf, "dbuser", 6))
	    continue;
	  if (!strncmp (buf, "dbpasswd", 6))
	    continue;
	  if (!strncmp (buf, "result_file", 11))
	    continue;

	  buf[strlen (buf) - 1] = '\0';	/* remove new line ch */
	  nv_add_nvp (cli_response, "result", buf);
	}
      fclose (fresfile2);
    }
  nv_add_nvp (cli_response, "result_list", "end");
  nv_add_nvp (cli_response, "query_result_file", resfile);
  nv_add_nvp (cli_response, "query_result_file_num", num_thread);

  if ((show_queryresult != NULL) && !strcmp (show_queryresult, "no"))
    {
      /* remove query result file - resfile */
      int i, n = 0;
      char filename[PATH_MAX];
      if (num_thread != NULL)
	{
	  n = atoi (num_thread);
	}

      for (i = 0; i < n && i < MAX_SERVER_THREAD_COUNT; i++)
	{
	  snprintf (filename, PATH_MAX - 1, "%s.%d", resfile, i);
	  unlink (filename);
	}
    }

  unlink (log_converter_res);	/* broker_log_converter execute result */
  unlink (resfile2);		/* cas log execute result */

  if (use_tmplogfile == TRUE)
    {
      unlink (tmplogfilename);	/* temp logfile */
    }
  return ERR_NO_ERROR;
}

int
ts_removecasrunnertmpfile (nvplist * cli_request, nvplist * cli_response,
			   char *diag_error)
{
  char *filename;
  char command[2048];

  filename = nv_get_val (cli_request, "filename");
  if (filename)
    {
      snprintf (command, sizeof (command) - 1, "%s %s %s*", DEL_DIR,
		DEL_DIR_OPT, filename);
      if (system (command) == -1)
	{
#if defined(WINDOWS)
	  snprintf (command, sizeof (command) - 1, "%s %s %s*", DEL_FILE,
		    DEL_FILE_OPT, filename);
	  if (system (command) == -1)
#endif
	    return ERR_DIR_REMOVE_FAIL;
	}
    }
  return ERR_NO_ERROR;
}

int
ts_getcaslogtopresult (nvplist * cli_request, nvplist * cli_response,
		       char *diag_error)
{
  char *filename, *qindex;
  FILE *fd;
  char buf[1024];
  int find_flag;

  filename = nv_get_val (cli_request, "filename");
  qindex = nv_get_val (cli_request, "qindex");
  if (!filename || !qindex)
    {
      return ERR_PARAM_MISSING;
    }

  fd = fopen (filename, "r");
  if (!fd)
    {
      return ERR_FILE_OPEN_FAIL;
    }

  find_flag = 0;
  nv_add_nvp (cli_response, "logstringlist", "start");
  while (fgets (buf, sizeof (buf), fd))
    {
      if (!strncmp (buf, "[Q", 2))
	{
	  if (find_flag == 1)
	    {
	      break;
	    }
	  if (!strncmp (buf, qindex, strlen (qindex)))
	    {
	      find_flag = 1;
	      continue;
	    }
	}

      if (find_flag == 1)
	{
	  buf[strlen (buf) - 1] = '\0';
	  nv_add_nvp (cli_response, "logstring", buf);
	}
    }

  nv_add_nvp (cli_response, "logstringlist", "end");

  fclose (fd);
  return ERR_NO_ERROR;
}

int
tsDBMTUserLogin (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char hexacoded[PASSWD_ENC_LENGTH];
  int ret;
  T_DBMT_CON_DBINFO con_dbinfo;
  char *dbname, *dbpasswd, *dbuser;
  char *ip, *port;

  ret = cm_tsDBMTUserLogin (req, res, _dbmt_error);
  if (ret != ERR_NO_ERROR)
    {
      return ret;
    }

  ip = nv_get_val (req, "_IP");
  port = nv_get_val (req, "_PORT");
  dbname = nv_get_val (req, "dbname");
  dbpasswd = nv_get_val (req, "dbpasswd");
  dbuser = nv_get_val (req, "dbuser");

  /* update dbinfo to conlist */
  uEncrypt (PASSWD_LENGTH, dbpasswd, hexacoded);
  memset (&con_dbinfo, 0, sizeof (T_DBMT_CON_DBINFO));
  dbmt_con_set_dbinfo (&con_dbinfo, dbname, dbuser, hexacoded);
  if (dbmt_con_write_dbinfo (&con_dbinfo, ip, port, dbname, 1, _dbmt_error) <
      0)
    {
      return ERR_WITH_MSG;
    }

  return ERR_NO_ERROR;
}


int
ts_remove_log (nvplist * req, nvplist * res, char *_dbmt_error)
{
  char *path;
  int i;
  int sect, sect_len;
  char broker_name[1024];
  int as_id;
  T_CM_ERROR error;

  nv_locate (req, "files", &sect, &sect_len);
  if (sect >= 0)
    {
      for (i = 0; i < sect_len; ++i)
	{
	  nv_lookup (req, sect + i, NULL, &path);
	  if ((path != NULL) && (unlink (path) != 0) && (errno != ENOENT))
	    {
	      sprintf (_dbmt_error, "Cannot remove file '%s' (%s)", path,
		       strerror (errno));

	      if (get_broker_info_from_filename (path, broker_name, &as_id) <
		  0 || cm_del_cas_log (broker_name, as_id, &error) < 0)
		{
		  strcpy (_dbmt_error, error.err_msg);
		  return ERR_WITH_MSG;
		}
	      _dbmt_error[0] = '\0';
	    }
	}			/* end of for */
    }
  return ERR_NO_ERROR;
}

static int
_tsAppendDBList (nvplist * res, char dbdir_flag)
{
  FILE *infile;
  char *dbinfo[4];
  char strbuf[1024], file[PATH_MAX];
  char hname[128];
  struct hostent *hp;
  unsigned char ip_addr[4];
  char *token = NULL;

  snprintf (file, PATH_MAX - 1, "%s/%s", sco.szCubrid_databases,
	    CUBRID_DATABASE_TXT);
  if ((infile = fopen (file, "rt")) == NULL)
    {
      return ERR_DATABASETXT_OPEN;
    }

  memset (hname, 0, sizeof (hname));
  gethostname (hname, sizeof (hname));
  if ((hp = gethostbyname (hname)) == NULL)
    {
      return ERR_NO_ERROR;
    }
  memcpy (ip_addr, hp->h_addr_list[0], 4);

  nv_add_nvp (res, "open", "dblist");
  while (fgets (strbuf, sizeof (strbuf), infile))
    {
      if (string_tokenize (strbuf, dbinfo, 4) < 0)
	{
	  continue;
	}
      for (token = strtok (dbinfo[2], ":"); token != NULL;
	   token = strtok (NULL, ":"))
	{
	  if ((hp = gethostbyname (token)) == NULL)
	    continue;

	  if (_op_check_is_localhost (token, hname) >= 0)
	    {
	      nv_add_nvp (res, "dbname", dbinfo[0]);

	      if (dbdir_flag)
		{
		  nv_add_nvp (res, "dbdir", dbinfo[1]);
		}
	      break;
	    }
	}
    }
  nv_add_nvp (res, "close", "dblist");
  fclose (infile);
  return ERR_NO_ERROR;
}

/**
 * get port info from brokers config, that located by <broker_name>
 */
static char *
_op_get_port_from_config (T_CM_BROKER_CONF * uc_conf, char *broker_name)
{
  int pos;
  char *name;
  for (pos = 0; pos < uc_conf->num_broker; pos++)
    {
      name = cm_br_conf_get_value (&(uc_conf->br_conf[pos]), "%");
      if (name != NULL && strcasecmp (broker_name, name) == 0)
	{
	  return cm_br_conf_get_value (&(uc_conf->br_conf[pos]),
				       "BROKER_PORT");
	}
    }

  /* not found. in this case returns zero value, it is means unknown port. */
  return "0";
}

/* Read dbmt password file and append user information to nvplist */
static void
_tsAppendDBMTUserList (nvplist * res, T_DBMT_USER * dbmt_user,
		       char *_dbmt_error)
{
  char decrypted[PASSWD_LENGTH + 1];
  const char *unicas_auth, *dbcreate = NULL, *status_monitor = NULL;
  int i, j;

  nv_add_nvp (res, "open", "userlist");
  for (i = 0; i < dbmt_user->num_dbmt_user; i++)
    {
      if (dbmt_user->user_info[i].user_name[0] == '\0')
	{
	  continue;
	}
      nv_add_nvp (res, "open", "user");
      nv_add_nvp (res, "id", dbmt_user->user_info[i].user_name);
      uDecrypt (PASSWD_LENGTH, dbmt_user->user_info[i].user_passwd,
		decrypted);
      nv_add_nvp (res, "passwd", decrypted);
      nv_add_nvp (res, "open", "dbauth");
      unicas_auth = NULL;
      /* add dbinfo */
      for (j = 0; j < dbmt_user->user_info[i].num_dbinfo; j++)
	{
	  if (dbmt_user->user_info[i].dbinfo[j].dbname[0] == '\0')
	    {
	      continue;
	    }
	  nv_add_nvp (res, "dbname",
		      dbmt_user->user_info[i].dbinfo[j].dbname);
	  nv_add_nvp (res, "dbid", dbmt_user->user_info[i].dbinfo[j].uid);
#if 0
	  uDecrypt (PASSWD_LENGTH, dbmt_user->user_info[i].dbinfo[j].passwd,
		    decrypted);
	  nv_add_nvp (res, "dbpasswd", decrypted);
#endif
	  nv_add_nvp (res, "dbpasswd", "");

	  nv_add_nvp (res, "dbbrokeraddress",
		      dbmt_user->user_info[i].dbinfo[j].broker_address);
	}
      nv_add_nvp (res, "close", "dbauth");

      /* add auth info */
      for (j = 0; j < dbmt_user->user_info[i].num_authinfo; j++)
	{
	  if (dbmt_user->user_info[i].authinfo[j].domain[0] == '\0')
	    {
	      continue;
	    }
	  if (strcmp (dbmt_user->user_info[i].authinfo[j].domain, "unicas") ==
	      0)
	    {
	      unicas_auth = dbmt_user->user_info[i].authinfo[j].auth;
	      nv_add_nvp (res, "casauth",
			  dbmt_user->user_info[i].authinfo[j].auth);
	      continue;
	    }
	  else
	    if (strcmp
		(dbmt_user->user_info[i].authinfo[j].domain, "dbcreate") == 0)
	    {
	      dbcreate = dbmt_user->user_info[i].authinfo[j].auth;
	      nv_add_nvp (res, "dbcreate",
			  dbmt_user->user_info[i].authinfo[j].auth);
	      continue;
	    }
	  else
	    if (strcmp
		(dbmt_user->user_info[i].authinfo[j].domain,
		 "statusmonitorauth") == 0)
	    {
	      nv_add_nvp (res, "statusmonitorauth",
			  dbmt_user->user_info[i].authinfo[j].auth);
	      continue;
	    }
	}
      if (unicas_auth == NULL)
	{
	  nv_add_nvp (res, "casauth", "none");
	}
      if (CLIENT_VERSION >= EMGR_MAKE_VER (7, 0))
	{
	  if (dbcreate == NULL)
	    {
	      nv_add_nvp (res, "dbcreate", "none");
	    }
	}
      nv_add_nvp (res, "close", "user");
    }
  nv_add_nvp (res, "close", "userlist");
}

static char *
get_user_name (int uid, char *name_buf)
{
#if defined(WINDOWS)
  strcpy (name_buf, "");
#else
  struct passwd *pwd;

  pwd = getpwuid (uid);
  if (pwd->pw_name)
    {
      strcpy (name_buf, pwd->pw_name);
    }
  else
    {
      name_buf[0] = '\0';
    }
#endif

  return name_buf;
}

static int
uca_conf_write (T_CM_BROKER_CONF * uc_conf, char *del_broker,
		char *_dbmt_error)
{
  char buf[512];
  FILE *fp;
  int i, j;
  struct stat statbuf;

  for (i = 0; i < uc_conf->num_header; i++)
    {
      if (uc_conf->header_conf[i].name == NULL ||
	  uc_conf->header_conf[i].value == NULL)
	{
	  return ERR_MEM_ALLOC;
	}
    }
  for (i = 0; i < uc_conf->num_broker; i++)
    {
      for (j = 0; j < uc_conf->br_conf[i].num; j++)
	{
	  if (uc_conf->br_conf[i].item[j].name == NULL ||
	      uc_conf->br_conf[i].item[j].value == NULL)
	    {
	      return ERR_MEM_ALLOC;
	    }
	}
    }

  cm_get_broker_file (UC_FID_CUBRID_BROKER_CONF, buf);

  if (stat (buf, &statbuf) < 0)
    {
      cm_get_broker_file (UC_FID_CUBRID_CAS_CONF, buf);
      if (stat (buf, &statbuf) < 0)
	{
	  cm_get_broker_file (UC_FID_UNICAS_CONF, buf);
	  if (stat (buf, &statbuf) < 0)
	    {
	      cm_get_broker_file (UC_FID_CUBRID_BROKER_CONF, buf);
	    }
	}
    }

  if ((fp = fopen (buf, "w")) == NULL)
    {
      strcpy (_dbmt_error, buf);
      return ERR_FILE_OPEN_FAIL;
    }
  fprintf (fp, "[broker]\n");
  for (i = 0; i < uc_conf->num_header; i++)
    {
      fprintf (fp, "%-25s =%s\n",
	       uc_conf->header_conf[i].name, uc_conf->header_conf[i].value);
    }
  fprintf (fp, "\n");
  for (i = 0; i < uc_conf->num_broker; i++)
    {
      if ((del_broker != NULL) &&
	  (strcmp (uc_conf->br_conf[i].item[0].value, del_broker) == 0))
	{
	  continue;
	}
      fprintf (fp, "[%s%s]\n", uc_conf->br_conf[i].item[0].name,
	       to_upper_str (uc_conf->br_conf[i].item[0].value, buf));
      for (j = 1; j < uc_conf->br_conf[i].num; j++)
	{
	  if (uc_conf->br_conf[i].item[j].value[0] == '\0')
	    continue;
	  fprintf (fp, "%-25s =%s\n", uc_conf->br_conf[i].item[j].name,
		   uc_conf->br_conf[i].item[j].value);
	}
      fprintf (fp, "\n");
    }
  fclose (fp);
  return ERR_NO_ERROR;
}

static int
_tsParseSpacedb (nvplist * req, nvplist * res, char *dbname,
		 char *_dbmt_error, T_SPACEDB_RESULT * cmd_res)
{
  int pagesize, logpagesize, i;
  T_SPACEDB_INFO *vol_info;
  char dbdir[PATH_MAX];
#if defined(WINDOWS)
  WIN32_FIND_DATA data;
  char find_file[PATH_MAX];
  HANDLE handle;
  int found;
#else
  DIR *dirp = NULL;
  struct dirent *dp = NULL;
#endif

  pagesize = cmd_res->page_size;
  logpagesize = cmd_res->log_page_size;
  nv_update_val_int (res, "pagesize", pagesize);
  nv_update_val_int (res, "logpagesize", logpagesize);

  vol_info = cmd_res->vol_info;
  for (i = 0; i < cmd_res->num_vol; i++)
    {
      nv_add_nvp (res, "open", "spaceinfo");
      nv_add_nvp (res, "spacename", vol_info[i].vol_name);
      nv_add_nvp (res, "type", vol_info[i].purpose);
      nv_add_nvp (res, "location", vol_info[i].location);
      nv_add_nvp_int (res, "totalpage", vol_info[i].total_page);
      nv_add_nvp_int (res, "freepage", vol_info[i].free_page);
      nv_add_nvp_time (res, "date", vol_info[i].date, "%04d%02d%02d",
		       NV_ADD_DATE);
      nv_add_nvp (res, "close", "spaceinfo");
    }

  vol_info = cmd_res->tmp_vol_info;
  for (i = 0; i < cmd_res->num_tmp_vol; i++)
    {
      nv_add_nvp (res, "open", "spaceinfo");
      nv_add_nvp (res, "spacename", vol_info[i].vol_name);
      nv_add_nvp (res, "type", vol_info[i].purpose);
      nv_add_nvp (res, "location", vol_info[i].location);
      nv_add_nvp_int (res, "totalpage", vol_info[i].total_page);
      nv_add_nvp_int (res, "freepage", vol_info[i].free_page);
      nv_add_nvp_time (res, "date", vol_info[i].date, "%04d%02d%02d",
		       NV_ADD_DATE);
      nv_add_nvp (res, "close", "spaceinfo");
    }

  if (uRetrieveDBLogDirectory (dbname, dbdir) != ERR_NO_ERROR)
    {
      strcpy (_dbmt_error, dbname);
      return ERR_DBDIRNAME_NULL;
    }

  /* read entries in the directory and generate result */
#if defined(WINDOWS)
  snprintf (find_file, PATH_MAX - 1, "%s/*", dbdir);
  if ((handle = FindFirstFile (find_file, &data)) == INVALID_HANDLE_VALUE)
#else
  if ((dirp = opendir (dbdir)) == NULL)
#endif
    {
      sprintf (_dbmt_error, "%s", dbdir);
      return ERR_DIROPENFAIL;
    }

#if defined(WINDOWS)
  for (found = 1; found; found = FindNextFile (handle, &data))
#else
  while ((dp = readdir (dirp)) != NULL)
#endif
    {
      int baselen;
      char *fname;

#if defined(WINDOWS)
      fname = data.cFileName;
#else
      fname = dp->d_name;
#endif
      baselen = strlen (dbname);

      if (strncmp (fname, dbname, baselen) == 0)
	{
	  if (!strcmp (fname + baselen, CUBRID_ACT_LOG_EXT))
	    {
	      _ts_gen_spaceinfo (res, fname, dbdir, "Active_log",
				 logpagesize);
	    }
	  else if (!strncmp
		   (fname + baselen, CUBRID_ARC_LOG_EXT,
		    CUBRID_ARC_LOG_EXT_LEN))
	    {
	      _ts_gen_spaceinfo (res, fname, dbdir, "Archive_log",
				 logpagesize);
	    }
#if 0
	  else if (strncmp (fname + baselen, "_lginf", 6) == 0)
	    {
	      _ts_gen_spaceinfo (res, fname, dbdir, "Generic_log",
				 logpagesize);
	    }
#endif

	}
    }
#if defined(WINDOWS)
  FindClose (handle);
#else
  closedir (dirp);
#endif

  /* add last line */
  nv_add_nvp (res, "open", "spaceinfo");
  nv_add_nvp (res, "spacename", "Total");
  nv_add_nvp (res, "type", "");
  nv_add_nvp (res, "location", "");
  nv_add_nvp (res, "totlapage", "0");
  nv_add_nvp (res, "freepage", "0");
  nv_add_nvp (res, "date", "");
  nv_add_nvp (res, "close", "spaceinfo");

  if (uRetrieveDBDirectory (dbname, dbdir) == ERR_NO_ERROR)
    {
      nv_add_nvp_int (res, "freespace", ut_disk_free_space (dbdir));
    }
  else
    {
      nv_add_nvp_int (res, "freespace", -1);
    }
  return ERR_NO_ERROR;
}

static void
_ts_gen_spaceinfo (nvplist * res, const char *filename,
		   const char *dbinstalldir, const char *type, int pagesize)
{
  char volfile[PATH_MAX];
  struct stat statbuf;

  nv_add_nvp (res, "open", "spaceinfo");
  nv_add_nvp (res, "spacename", filename);
  nv_add_nvp (res, "type", type);
  nv_add_nvp (res, "location", dbinstalldir);

  snprintf (volfile, PATH_MAX - 1, "%s/%s", dbinstalldir, filename);
  stat (volfile, &statbuf);

  nv_add_nvp_int (res, "totalpage", statbuf.st_size / pagesize);
  nv_add_nvp (res, "freepage", " ");

  nv_add_nvp_time (res, "date", statbuf.st_mtime, "%04d%02d%02d",
		   NV_ADD_DATE);

  nv_add_nvp (res, "close", "spaceinfo");
  return;
}

static int
_ts_lockdb_parse_us (nvplist * res, FILE * infile)
{
  char buf[1024], s[256], s1[256], s2[256], s3[256], s4[256];
  char *temp, *temp2;
  int scan_matched;
  int flag = 0;

  nv_add_nvp (res, "open", "lockinfo");
  while (fgets (buf, sizeof (buf), infile))
    {
      sscanf (buf, "%255s", s);

      if (flag == 0 && !strcmp (s, "***"))
	{
	  fgets (buf, sizeof (buf), infile);
	  scan_matched =
	    sscanf (buf, "%*s %*s %*s %*s %255s %*s %*s %*s %*s %255s", s1,
		    s2);

	  if (scan_matched != 2)
	    {
	      return -1;
	    }
	  s1[strlen (s1) - 1] = '\0';
	  nv_add_nvp (res, "esc", s1);
	  nv_add_nvp (res, "dinterval", s2);
	  flag = 1;
	}
      else if (flag == 1)
	{
	  if (strcmp (s, "Transaction") == 0)
	    {
	      scan_matched =
		sscanf (buf, "%*s %*s %255s %255s %255s", s1, s2, s3);

	      if (scan_matched != 3)
		{
		  return -1;
		}
	      s1[strlen (s1) - 1] = '\0';
	      s2[strlen (s2) - 1] = '\0';
	      s3[strlen (s3) - 1] = '\0';

	      nv_add_nvp (res, "open", "transaction");
	      nv_add_nvp (res, "index", s1);
	      nv_add_nvp (res, "pname", s2);

	      temp = strchr (s3, '@');
	      if (temp != NULL)
		{
		  strncpy (buf, s3, (int) (temp - s3));
		  buf[(int) (temp - s3)] = '\0';
		  nv_add_nvp (res, "uid", buf);
		}

	      temp2 = strrchr (s3, '|');
	      if (temp2 != NULL)
		{
		  strncpy (buf, temp + 1, (int) (temp2 - temp - 1));
		  buf[(int) (temp2 - temp) - 1] = '\0';
		  nv_add_nvp (res, "host", buf);
		}
	      nv_add_nvp (res, "pid", temp2 + 1);

	      fgets (buf, sizeof (buf), infile);
	      buf[strlen (buf) - 1] = '\0';
	      nv_add_nvp (res, "isolevel", buf + strlen ("Isolation "));

	      fgets (buf, sizeof (buf), infile);
	      if (strncmp (buf, "State", strlen ("State")) == 0)
		{
		  fgets (buf, sizeof (buf), infile);
		}

	      scan_matched = sscanf (buf, "%*s %255s", s1);

	      if (scan_matched != 1)
		{
		  return -1;
		}
	      nv_add_nvp (res, "timeout", s1);
	      nv_add_nvp (res, "close", "transaction");
	    }
	  else if (strcmp (s, "Object") == 0)
	    {
	      fgets (buf, sizeof (buf), infile);
	      scan_matched =
		sscanf (buf, "%*s %*s %*s %*s %*s %*s %*s %*s %255s", s1);
	      if (scan_matched != 1)
		{
		  return -1;
		}
	      nv_add_nvp (res, "open", "lot");
	      nv_add_nvp (res, "numlocked", s1);

	      fgets (buf, sizeof (buf), infile);
	      scan_matched =
		sscanf (buf, "%*s %*s %*s %*s %*s %*s %*s %*s %*s %255s", s2);
	      if (scan_matched != 1)
		{
		  return -1;
		}
	      nv_add_nvp (res, "maxnumlock", s2);
	      flag = 2;
	    }
	}			/* end of if (flag == 1) */
      else if (flag == 2)
	{
	  char value[1024];

	  while (!strcmp (s, "OID"))
	    {
	      int num_holders, num_b_holders, num_waiters, scan_matched;

	      scan_matched =
		sscanf (buf, "%*s %*s %255s %255s %255s", s1, s2, s3);
	      if (scan_matched != 3)
		return -1;

	      snprintf (value, sizeof (value) - 1, "%s%s%s", s1, s2, s3);

	      nv_add_nvp (res, "open", "entry");
	      nv_add_nvp (res, "oid", value);

	      fgets (buf, sizeof (buf), infile);
	      sscanf (buf, "%*s %*s %255s", s);

	      s1[0] = s2[0] = s3[0] = '\0';
	      scan_matched = 0;
	      if ((strcmp (s, "Class") == 0) || (strcmp (s, "Instance") == 0))
		{
		  char *p;
		  p = strchr (buf, ':');
		  buf[strlen (buf) - 1] = '\0';
		  if (buf[strlen (buf) - 1] == '.')
		    {
		      buf[strlen (buf) - 1] = '\0';
		    }
		  nv_add_nvp (res, "ob_type", p + 2);
		  fgets (buf, sizeof (buf), infile);
		}
	      else if (strcmp (s, "Root") == 0)
		{
		  nv_add_nvp (res, "ob_type", "Root class");
		  fgets (buf, sizeof (buf), infile);
		}
	      else
		{
		  /* Current test is not 'OID = ...' and if 'Total mode of holders ...' then */
		  scan_matched =
		    sscanf (buf, "%*s %*s %255s %*s %*s %255s %*s %*s %255s",
			    s1, s2, s3);
		  if ((strncmp (s1, "of", 2) == 0)
		      && (strncmp (s3, "of", 2) == 0))
		    {
		      nv_add_nvp (res, "ob_type", "None");
		    }
		  else
		    {
		      return -1;
		    }
		}

	      /* already get  scan_matched value, don't sscanf */
	      if (scan_matched == 0)
		scan_matched =
		  sscanf (buf, "%*s %*s %255s %*s %*s %255s %*s %*s %255s",
			  s1, s2, s3);

	      if ((strncmp (s1, "of", 2) == 0)
		  && (strncmp (s3, "of", 2) == 0))
		{
		  /* ignore UnixWare's 'Total mode of ...' text */
		  fgets (buf, sizeof (buf), infile);
		  scan_matched =
		    sscanf (buf, "%*s %*s %255s %*s %*s %255s %*s %*s %255s",
			    s1, s2, s3);
		}

	      if (scan_matched != 3)
		{
		  return -1;
		}
	      s1[strlen (s1) - 1] = '\0';
	      s2[strlen (s2) - 1] = '\0';

	      num_holders = atoi (s1);
	      num_b_holders = atoi (s2);
	      num_waiters = atoi (s3);

	      if (num_waiters < 0 || num_waiters >= INT_MAX)
		{
		  return -1;
		}

	      nv_add_nvp (res, "num_holders", s1);
	      nv_add_nvp (res, "num_b_holders", s2);
	      nv_add_nvp (res, "num_waiters", s3);

	      while (fgets (buf, sizeof (buf), infile))
		{
		  sscanf (buf, "%255s", s);
		  if (strcmp (s, "NON_2PL_RELEASED:") == 0)
		    {
		      fgets (buf, sizeof (buf), infile);
		      while (sscanf (buf, "%255s", s))
			{
			  if (strcmp (s, "Tran_index") != 0)
			    {
			      break;
			    }
			  else
			    {
			      if (fgets (buf, sizeof (buf), infile) == NULL)
				{
				  break;
				}
			    }
			}
		      break;
		    }		/* ignore NON_2PL_RELEASED information */

		  sscanf (buf, "%*s %255s", s);
		  if (strcmp (s, "HOLDERS:") == 0)
		    {
		      int index;

		      for (index = 0; index < num_holders; index++)
			{
			  /* make lock holders information */

			  fgets (buf, sizeof (buf), infile);
			  scan_matched =
			    sscanf (buf,
				    "%*s %*s %255s %*s %*s %255s %*s %*s %255s %*s %*s %255s",
				    s1, s2, s3, s4);

			  /* parshing error */
			  if (scan_matched < 3)
			    {
			      return -1;
			    }
			  if (scan_matched == 4)
			    {
			      /* nsubgranules is existed */
			      s3[strlen (s3) - 1] = '\0';
			    }

			  s1[strlen (s1) - 1] = '\0';
			  s2[strlen (s2) - 1] = '\0';

			  nv_add_nvp (res, "open", "lock_holders");
			  nv_add_nvp (res, "tran_index", s1);
			  nv_add_nvp (res, "granted_mode", s2);
			  nv_add_nvp (res, "count", s3);
			  if (scan_matched == 4)
			    {
			      nv_add_nvp (res, "nsubgranules", s4);
			    }
			  nv_add_nvp (res, "close", "lock_holders");
			}

		      if ((num_b_holders == 0) && (num_waiters == 0))
			{
			  break;
			}
		    }
		  else if (strcmp (s, "LOCK") == 0)
		    {
		      int index;
		      char *p;

		      for (index = 0; index < num_b_holders; index++)
			{
			  /* make blocked lock holders */
			  int scan_matched;
			  fgets (buf, sizeof (buf), infile);
			  scan_matched =
			    sscanf (buf,
				    "%*s %*s %255s %*s %*s %255s %*s %*s %255s %*s %*s %255s",
				    s1, s2, s3, s4);

			  /* parshing error */
			  if (scan_matched < 3)
			    {
			      return -1;
			    }
			  if (scan_matched == 4)
			    {
			      /* nsubgranules is existed */
			      s3[strlen (s3) - 1] = '\0';
			    }

			  s1[strlen (s1) - 1] = '\0';
			  s2[strlen (s2) - 1] = '\0';

			  nv_add_nvp (res, "open", "b_holders");
			  nv_add_nvp (res, "tran_index", s1);
			  nv_add_nvp (res, "granted_mode", s2);
			  nv_add_nvp (res, "count", s3);

			  if (scan_matched == 4)
			    {
			      nv_add_nvp (res, "nsubgranules", s4);
			    }
			  fgets (buf, sizeof (buf), infile);
			  sscanf (buf, "%*s %*s %255s", s1);
			  nv_add_nvp (res, "b_mode", s1);

			  fgets (buf, sizeof (buf), infile);
			  p = strchr (buf, '=');
			  buf[strlen (buf) - 1] = '\0';
			  nv_add_nvp (res, "start_at", p + 2);

			  fgets (buf, sizeof (buf), infile);
			  sscanf (buf, "%*s %*s %255s", s1);
			  nv_add_nvp (res, "waitfornsec", s1);

			  nv_add_nvp (res, "close", "b_holders");
			}

		      if (num_waiters == 0)
			{
			  break;
			}
		    }
		  else if (strcmp (s, "WAITERS:") == 0)
		    {
		      int index;

		      for (index = 0; index < num_waiters; index++)
			{
			  /* make lock waiters */
			  char *p;

			  fgets (buf, sizeof (buf), infile);
			  sscanf (buf, "%*s %*s %255s %*s %*s %255s", s1, s2);
			  s1[strlen (s1) - 1] = '\0';

			  nv_add_nvp (res, "open", "waiters");
			  nv_add_nvp (res, "tran_index", s1);
			  nv_add_nvp (res, "b_mode", s2);

			  fgets (buf, sizeof (buf), infile);
			  p = strchr (buf, '=');
			  buf[strlen (buf) - 1] = '\0';
			  nv_add_nvp (res, "start_at", p + 2);

			  fgets (buf, sizeof (buf), infile);
			  sscanf (buf, "%*s %*s %255s", s1);
			  nv_add_nvp (res, "waitfornsec", s1);

			  nv_add_nvp (res, "close", "waiters");
			}
		      break;
		    }
		}		/* end of while - for just one object */
	      nv_add_nvp (res, "close", "entry");
	    }			/* end of while(OID) */
	}
    }
  nv_add_nvp (res, "close", "lot");
  nv_add_nvp (res, "close", "lockinfo");
  return 0;
}


static char *
to_upper_str (char *str, char *buf)
{
  char *p;

  strcpy (buf, str);
  for (p = buf; *p; p++)
    {
      if (*p >= 'a' && *p <= 'z')
	{
	  *p = *p - 'a' + 'A';
	}
    }
  return buf;
}

static int
op_make_triggerinput_file_add (nvplist * req, char *input_filename)
{
  char *name, *status, *cond_source, *priority;
  char *event_target, *event_type, *event_time, *actiontime, *action;
  FILE *input_file;

  if (input_filename == NULL)
    {
      return 0;
    }

  input_file = fopen (input_filename, "w+");
  if (input_file == NULL)
    {
      return 0;
    }

  name = nv_get_val (req, "triggername");
  status = nv_get_val (req, "status");
  event_type = nv_get_val (req, "eventtype");
  event_target = nv_get_val (req, "eventtarget");
  event_time = nv_get_val (req, "conditiontime");
  cond_source = nv_get_val (req, "condition");
  actiontime = nv_get_val (req, "actiontime");
  action = nv_get_val (req, "action");
  priority = nv_get_val (req, "priority");
/*		  fprintf(input_file, ";autocommit off\n");*/
  fprintf (input_file, "create trigger\t%s\n", name);

  if (status)
    {
      fprintf (input_file, "status\t%s\n", status);
    }
  if (priority)
    {
      fprintf (input_file, "priority\t%s\n", priority);
    }
  fprintf (input_file, "%s\t%s\t", event_time, event_type);

  if (event_target)
    {
      fprintf (input_file, "ON\t%s\n", event_target);
    }
  if (cond_source)
    {
      fprintf (input_file, "if\t%s\n", cond_source);
    }
  fprintf (input_file, "execute\t");

  if (actiontime)
    {
      fprintf (input_file, "%s\t", actiontime);
    }
  fprintf (input_file, "%s\n", action);
  fprintf (input_file, "\n\ncommit\n\n");

  fclose (input_file);

  return 1;
}

static int
op_make_triggerinput_file_drop (nvplist * req, char *input_filename)
{
  char *trigger_name;
  FILE *input_file;

  if (input_filename == NULL)
    {
      return 0;
    }

  input_file = fopen (input_filename, "w+");
  if (input_file == NULL)
    {
      return 0;
    }

  trigger_name = nv_get_val (req, "triggername");
/*		  fprintf(input_file, ";autocommit off\n");*/
  fprintf (input_file, "drop trigger\t%s\n", trigger_name);
  fprintf (input_file, "\n\n\ncommit\n\n");

  fclose (input_file);

  return 1;
}

static int
op_make_triggerinput_file_alter (nvplist * req, char *input_filename)
{
  char *trigger_name, *status, *priority;
  FILE *input_file;

  if (input_filename == NULL)
    {
      return 0;
    }

  input_file = fopen (input_filename, "w+");
  if (input_file == NULL)
    {
      return 0;
    }

  trigger_name = nv_get_val (req, "triggername");
  status = nv_get_val (req, "status");
  priority = nv_get_val (req, "priority");
/*		  fprintf(input_file, ";autocommit off\n");*/
  if (status)
    {
      fprintf (input_file, "alter trigger\t%s\t", trigger_name);
      fprintf (input_file, "status %s\t", status);
    }

  if (priority)
    {
      fprintf (input_file, "alter trigger\t%s\t", trigger_name);
      fprintf (input_file, "priority %s\t", priority);
    }

  fprintf (input_file, "\n\n\ncommit\n\n");
  fclose (input_file);

  return 1;
}

static int
get_broker_info_from_filename (char *path, char *br_name, int *as_id)
{
#if defined(WINDOWS)
  const char *sql_log_ext = ".sql.log";
  int sql_log_ext_len = strlen (sql_log_ext);
  int path_len;
  char *p;

  if (path == NULL)
    {
      return -1;
    }
  path_len = strlen (path);
  if (strncmp (path, sco.szCubrid, strlen (sco.szCubrid)) != 0 ||
      path_len <= sql_log_ext_len ||
      strcmp (path + (path_len - sql_log_ext_len), sql_log_ext) != 0)
    {
      return -1;
    }

  for (p = path + path_len - 1; p >= path; p--)
    {
      if (*p == '/' || *p == '\\')
	{
	  break;
	}
    }
  path = p + 1;
  path_len = strlen (path);

  *(path + path_len - sql_log_ext_len) = '\0';
  p = strrchr (path, '_');

  *as_id = atoi (p + 1);
  if (*as_id <= 0)
    {
      return -1;
    }
  strncpy (br_name, path, p - path);
  *(br_name + (p - path)) = '\0';

  return 0;
#else
  return -1;
#endif
}

static void
op_auto_exec_query_get_newplan_id (char *id_buf, char *conf_filename)
{
  FILE *conf_file;
  char buf[MAX_JOB_CONFIG_FILE_LINE_LENGTH], id_num[5];
  int index;

  for (index = 1; index < 10000; index++)
    {
      int used = 0;
      conf_file = fopen (conf_filename, "r");
      if (conf_file == NULL)
	{
	  strcpy (id_buf, "0001");
	  return;
	}

      while (fgets (buf, sizeof (buf), conf_file))
	{
	  sscanf (buf, "%*s %4s", id_num);
	  if (atoi (id_num) == index)
	    {
	      used = 1;
	      break;
	    }
	}

      if (used == 0)
	{
	  snprintf (id_buf, sizeof (id_buf) - 1, "%04d", index);
	  fclose (conf_file);
	  break;
	}
      fclose (conf_file);
    }
}

/* 
 * check if dir path contains white space character,
 * and if path contains '\', substitute it with '/'
 */
static int
check_dbpath (char *dir, char *_dbmt_error)
{
  if (dir == NULL || *dir == '\0')
    {
      strcpy (_dbmt_error, "Path is NULL!");
      return ERR_WITH_MSG;
    }

  while (*dir != '\0')
    {
      if (isspace (*dir))
	{
	  strcpy (_dbmt_error, "Path contains white space!");
	  return ERR_WITH_MSG;
	}
      else if (*dir == '\\')
	{
	  *dir = '/';
	}
      dir++;
    }

  return ERR_NO_ERROR;
}

static int
get_dbitemdir (char *item_dir, size_t item_dir_size, char *dbname,
	       char *err_buf, int itemtype)
{
  FILE *databases_txt;
  char *envpath;
  char db_txt[PATH_MAX];
  char cbuf[2048];
  char itemname[1024];
  char scan_format[128];
  char pattern[64];
  const char *patternVol = "%%%lus %%%lus %%*s %%*s";
  const char *patternLog = "%%%lus %%*s %%*s %%%lus";

  if (item_dir == NULL || dbname == NULL)
    {
      return -1;
    }
#if !defined (DO_NOT_USE_CUBRIDENV)
  envpath = sco.szCubrid_databases;
#else
  envpath = CUBRID_VARDIR;
#endif
  if ((envpath == NULL) || (strlen (envpath) == 0))
    {
      return -1;
    }
  snprintf (db_txt, sizeof (db_txt) - 1, "%s/%s", envpath,
	    CUBRID_DATABASE_TXT);
  databases_txt = fopen (db_txt, "r");

  /*set the patten to get dir from databases.txt. */
  if (databases_txt == NULL)
    {
      return -1;
    }

  if (itemtype == PATTERN_VOL)
    {
      strcpy_limit (pattern, patternVol, sizeof (pattern));
    }
  else if (itemtype == PATTERN_LOG)
    {
      strcpy_limit (pattern, patternLog, sizeof (pattern));
    }
  else
    {
      return -1;
    }

  snprintf (scan_format, sizeof (scan_format) - 1, pattern,
	    (unsigned long) sizeof (itemname) - 1,
	    (unsigned long) item_dir_size - 1);

  while (fgets (cbuf, sizeof (cbuf), databases_txt) != NULL)
    {
      if (sscanf (cbuf, scan_format, itemname, item_dir) < 2)
	{
	  continue;
	}
      if (strcmp (itemname, dbname) == 0)
	{
	  fclose (databases_txt);
	  return 1;
	}
    }

  fclose (databases_txt);
  return 1;
}


static int
get_dblogdir (char *log_dir, size_t log_dir_size, char *dbname, char *err_buf)
{
  int retVal =
    get_dbitemdir (log_dir, log_dir_size, dbname, err_buf, PATTERN_LOG);
  return retVal;
}

static int
get_dbvoldir (char *vol_dir, size_t vol_dir_size, char *dbname, char *err_buf)
{
  int retVal =
    get_dbitemdir (vol_dir, vol_dir_size, dbname, err_buf, PATTERN_VOL);
  return retVal;
}

static char *
cm_get_abs_file_path (const char *filename, char *buf)
{
  strcpy (buf, filename);

  if (buf[0] == '/')
    return buf;
#if defined(WINDOWS)
  if (buf[2] == '/' || buf[2] == '\\')
    return buf;
#endif
  sprintf (buf, "%s/%s", getenv ("CUBRID"), filename);
  return buf;
}

static int
file_to_nvpairs (FILE * infile, nvplist * res)
{
  char buf[1024];
  int retval = 0;

  /* return error if the input file is NULL. */
  if (infile == NULL)
    {
      retval = -1;
    }

  /* get all file content into response. */
  nv_add_nvp (res, "open", "log");
  while (fgets (buf, sizeof (buf), infile) != NULL)
    {
      nv_add_nvp (res, "line", buf);
    }
  nv_add_nvp (res, "close", "log");

  return retval;
}

static int
file_to_nvp_by_separator (FILE * fp, nvplist * res, char separator)
{
  char buf[1024];
  char *p, *q, *next;
  int clientexist = 0;
  const char *comparestr = "System parameters";

  while (fgets (buf, sizeof (buf), fp) != NULL)
    {
      if (strncmp (buf, comparestr, strlen (comparestr)) == 0)
	{
	  /* check if the sentences contains "client" or "server" or "standalone". */
	  if (strstr (buf, "client") != NULL)
	    {
	      clientexist = 1;
	      nv_add_nvp (res, "open", "client");
	    }
	  else if (strstr (buf, "server") != NULL)
	    {
	      /* if it is "server" then check if there is need to close "client". */
	      if (clientexist != 0)
		nv_add_nvp (res, "close", "client");
	      nv_add_nvp (res, "open", "server");
	    }
	  else if (strstr (buf, "standalone") != NULL)
	    {
	      /* if it is "standalone" then there should return only server paramdump. */
	      nv_add_nvp (res, "open", "server");
	    }
	  else
	    {
	      return -1;
	    }
	}

      /*
       * ignore the lines that start with "#", empty lines, 
       * and lines with out separator. 
       */
      if ('#' == buf[0] || '\n' == buf[0] || strchr (buf, separator) == NULL)
	{
	  continue;
	}
      p = buf;
      next = strchr (p, '\n');
      if (next != NULL)
	{
	  *next = '\0';
	}
      q = strchr (p, '=');
      if (q != NULL)
	{
	  *q = '\0';
	  q++;
	}
      nv_add_nvp (res, p, q);
    }
  nv_add_nvp (res, "close", "server");
  return 0;
}
