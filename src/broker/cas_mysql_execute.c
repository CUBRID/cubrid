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
 * cas_mysql_execute.c -
 */

#ident "$Id$"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#if defined(WINDOWS)
#include <winsock2.h>
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <process.h>
#else
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#endif

#include "porting.h"
#include "cas.h"
#include "cas_common.h"
#include "cas_execute.h"
#include "cas_network.h"
#include "cas_util.h"
#include "cas_schema_info.h"
#include "cas_log.h"
#include "cas_str_like.h"
#include "cas_mysql.h"

#include "broker_filename.h"
#include "cas_sql_log2.h"

#include "release_string.h"
#include "cas_error_log.h"

#include "error_code.h"

#define MYSQL_DEFAULT_WAIT_TIMEOUT 18000

#if !defined(WINDOWS)
#define STRING_APPEND(buffer_p, avail_size_holder, ...) \
  do {                                                          \
    if (avail_size_holder > 0) {                                \
      int n = snprintf (buffer_p, avail_size_holder, __VA_ARGS__);	\
      if (n > 0)        {                                       \
        if (n < avail_size_holder) {                            \
          buffer_p += n; avail_size_holder -= n;                \
        } else {                                                \
          buffer_p += (avail_size_holder - 1); 			\
	  avail_size_holder = 0; 				\
        }                                                       \
      }                                                         \
    }								\
  } while (0)
#else /* !WINDOWS */
#define STRING_APPEND(buffer_p, avail_size_holder, ...) \
  do {                                                          \
    if (avail_size_holder > 0) {                                \
      int n = _snprintf (buffer_p, avail_size_holder, __VA_ARGS__);	\
      if (n < 0 || n >= avail_size_holder) {                    \
        buffer_p += (avail_size_holder - 1);                    \
        avail_size_holder = 0;                                  \
        *buffer_p = '\0';                                       \
      } else {                                                  \
        buffer_p += n; avail_size_holder -= n;                  \
      }                                                         \
    }                                                           \
  } while (0)
#endif /* !WINDOWS */

typedef int (*T_FETCH_FUNC) (T_SRV_HANDLE *, int, int, char, int,
			     T_NET_BUF *, T_REQ_INFO *);

typedef struct t_priv_table T_PRIV_TABLE;
struct t_priv_table
{
  char *class_name;
  char priv;
  char grant;
};

typedef struct t_attr_table T_ATTR_TABLE;
struct t_attr_table
{
  char *class_name;
  char *attr_name;
  char *source_class;
  int precision;
  short scale;
  short attr_order;
  void *default_val;
  char domain;
  char indexed;
  char non_null;
  char shared;
  char unique;
  char set_domain;
  char is_key;
};

static int mysql_wait_timeout = MYSQL_DEFAULT_WAIT_TIMEOUT;

static int cubval_to_mysqlval (int cub_type);
static int make_bind_value (int num_bind, int argc, void **argv,
			    MYSQL_BIND * ret_val, DB_VALUE ** db_vals,
			    T_NET_BUF * net_buf, int desired_type);
static int netval_to_dbval (void *type, void *value, MYSQL_BIND * out_val,
			    DB_VALUE * db_val, T_NET_BUF * net_buf,
			    int desired_type);
static int cur_tuple (T_SRV_HANDLE * q_result, int tuple,
		      T_NET_BUF * net_buf);
static int dbval_to_net_buf (void *val, int type, my_bool is_null,
			     unsigned long length, T_NET_BUF * net_buf);
#if 0
static int get_attr_name_from_argv (int argc, void **argv,
				    char ***ret_attr_name);
#endif /* 0 */
static int prepare_column_list_info_set (T_SRV_HANDLE * srv_handle,
					 T_NET_BUF * net_buf);
static int send_prepare_info (T_SRV_HANDLE * srv_handle, T_NET_BUF * net_buf);
static int set_metadata_info (T_SRV_HANDLE * srv_handle);
static void prepare_column_info_set (T_NET_BUF * net_buf, char ut,
				     short scale, int prec,
				     const char *col_name,
				     const char *default_value,
				     char auto_increment, char unique_key,
				     char primary_key, char reverse_index,
				     char reverse_unique, char foreign_key,
				     char shared, const char *attr_name,
				     const char *class_name, char nullable);

static int fetch_result (T_SRV_HANDLE *, int, int, char, int, T_NET_BUF *,
			 T_REQ_INFO *);

static void add_res_data_bytes (T_NET_BUF * net_buf, char *str, int size,
				char type);
static void add_res_data_string (T_NET_BUF * net_buf, const char *str,
				 int size, char type);
static void add_res_data_int (T_NET_BUF * net_buf, int value, char type);
static void add_res_data_bigint (T_NET_BUF * net_buf, DB_BIGINT value,
				 char type);
static void add_res_data_short (T_NET_BUF * net_buf, short value, char type);
static void add_res_data_float (T_NET_BUF * net_buf, float value, char type);
static void add_res_data_double (T_NET_BUF * net_buf, double value,
				 char type);
static void add_res_data_timestamp (T_NET_BUF * net_buf, short yr, short mon,
				    short day, short hh, short mm, short ss,
				    char type);
static void add_res_data_datetime (T_NET_BUF * net_buf, short yr, short mon,
				   short day, short hh, short mm, short ss,
				   short ms, char type);
static void add_res_data_time (T_NET_BUF * net_buf, short hh, short mm,
			       short ss, char type);
static void add_res_data_date (T_NET_BUF * net_buf, short yr, short mon,
			       short day, char type);
#if 0
static void add_res_data_object (T_NET_BUF * net_buf, T_OBJECT * obj,
				 char type);
static void trigger_event_str (DB_TRIGGER_EVENT trig_event, char *buf);
static void trigger_status_str (DB_TRIGGER_STATUS trig_status, char *buf);
static void trigger_time_str (DB_TRIGGER_TIME trig_time, char *buf);
#endif /* 0 */

static char get_stmt_type (char *stmt);
static int execute_info_set (T_SRV_HANDLE * srv_handle, T_NET_BUF * net_buf,
			     T_BROKER_VERSION client_version, char exec_flag);

T_PREPARE_CALL_INFO *make_prepare_call_info (int num_args);
static bool has_stmt_result_set (int stmt_type);
static bool check_auto_commit_after_fetch_done (T_SRV_HANDLE * srv_handle);

static int ux_execute_internal (T_SRV_HANDLE * srv_handle, char flag,
				int max_col_size, int max_row, int argc,
				void **argv, T_NET_BUF * net_buf,
				T_REQ_INFO * req_info,
				CACHE_TIME * clt_cache_time,
				int *clt_cache_reusable, bool is_all);

static int cas_mysql_get_errno (void);
static int cas_mysql_get_stmt_errno (MYSQL_STMT * stmt);
static void cas_mysql_set_host_connected (const char *dbname,
					  const char *host, int port);
static int cas_mysql_stmt_bind_result (MYSQL_STMT * stmt, MYSQL_BIND ** bind);
static int cas_mysql_stmt_store_result (MYSQL_STMT * stmt);
static const char *cas_mysql_get_errmsg (void);
static int cas_mysql_find_db (const char *alias, char *dbname, char *host,
			      int *port);
static const char *cas_mysql_get_connected_host_info (void);
static int cas_mysql_connect_db (char *alias, char *user, char *passwd);
static void cas_mysql_disconnect_db (void);
static int cas_mysql_prepare (T_SRV_HANDLE ** new_handle, char *sql_stmt,
			      int flag, char auto_commit_mode,
			      unsigned int query_seq_num);
static int cas_mysql_param_count (MYSQL_STMT * stmt);
static void cas_mysql_bind_param_clear (MYSQL_BIND * param);
static int cas_mysql_stmt_result_metadata (MYSQL_STMT * stmt,
					   MYSQL_RES ** result);
static MYSQL_FIELD *cas_mysql_fetch_field (MYSQL_RES * result);
static void cas_mysql_free_result (MYSQL_RES * result);
static int cas_mysql_stmt_bind_param (MYSQL_STMT * stmt, MYSQL_BIND * bindIn);
static int cas_mysql_stmt_execute (MYSQL_STMT * stmt);
static int cas_mysql_stmt_fetch (MYSQL_STMT * stmt);
static int cas_mysql_end_tran (int tran_type);
static int cas_mysql_num_fields (MYSQL_RES * metaResult);
static bool cas_mysql_autocommit (bool autoCommit);
static int db_value_clear (DB_VALUE * value);

static MYSQL *_db_conn;
static char mysql_connected_info[DBINFO_MAX_LENGTH] = "";
static int mysql_connect_status = DB_CONNECTION_STATUS_NOT_CONNECTED;

static char database_name[MAX_HA_DBNAME_LENGTH] = "";
static char database_user[SRV_CON_DBUSER_SIZE] = "";
static char database_passwd[SRV_CON_DBPASSWD_SIZE] = "";
static int multi_byte_character_max_length = 3;

static void update_query_execution_count (T_APPL_SERVER_INFO * as_info_p,
					  char stmt_type);
static void cas_mysql_set_mysql_wait_timeout (void);

int
ux_check_connection (void)
{

  if (ux_is_database_connected ())
    {
      return 0;
    }
  return -1;
}

int
ux_database_connect (char *db_alias, char *db_user, char *db_passwd,
		     char **db_err_msg)
{
  int err_code;
  const char *p;
  int size;
  const char *host_connected = NULL;
  MY_CHARSET_INFO cs;

  if (db_alias == NULL || db_alias[0] == '\0')
    return -1;

  if (get_db_connect_status () != DB_CONNECTION_STATUS_CONNECTED
      || database_name[0] == '\0' || strcmp (database_name, db_alias) != 0)
    {
      if (database_name[0] != '\0')
	{
	  ux_database_shutdown ();
	}
      err_code = cas_mysql_connect_db (db_alias, db_user, db_passwd);
      if (err_code < 0)
	{
	  goto connect_error;
	}

      cas_mysql_set_mysql_wait_timeout ();

      host_connected = cas_mysql_get_connected_host_info ();

      mysql_get_character_set_info (_db_conn, &cs);
      multi_byte_character_max_length = cs.mbmaxlen;

      cas_log_debug (ARG_FILE_LINE,
		     "ux_database_connect: cas_mysql_connect_db(%s, %s) at %s",
		     db_user, db_alias, host_connected);
      /* as_info->database_name is alias name */
      strncpy (as_info->database_name, db_alias, MAX_HA_DBNAME_LENGTH - 1);
      /* as_info->database_host is real_db_name:connected_host_addr:connected_port */
      strncpy (as_info->database_host, host_connected, MAXHOSTNAMELEN);
      as_info->last_connect_time = time (NULL);

      strncpy (database_name, db_alias, sizeof (database_name) - 1);
      strncpy (database_user, db_user, sizeof (database_user) - 1);
      strncpy (database_passwd, db_passwd, sizeof (database_passwd) - 1);
    }
  else if (strcmp (database_user, db_user) != 0
	   || strcmp (database_passwd, db_passwd) != 0)
    {
      ux_database_shutdown ();
      return ux_database_connect (db_alias, db_user, db_passwd, db_err_msg);
    }

  return 0;

connect_error:
  p = (const char *) cas_mysql_get_errmsg ();
  if (p)
    {
      size = strlen (p) + 1;
    }
  else
    {
      size = 1;
    }
  if (db_err_msg)
    {
      *db_err_msg = (char *) malloc (size);
      if ((*db_err_msg) && (p != NULL))
	{
	  strcpy (*db_err_msg, p);
	}
    }
  cas_mysql_disconnect_db ();
  return err_code;
}


int
ux_is_database_connected (void)
{
  return (database_name[0] != '\0');
}

void
ux_database_shutdown (void)
{
  cas_mysql_disconnect_db ();

  cas_log_debug (ARG_FILE_LINE,
		 "ux_database_shutdown: cas_mysql_disconnect_db ()");
  as_info->database_name[0] = '\0';
  as_info->database_host[0] = '\0';
  as_info->last_connect_time = 0;
  memset (database_name, 0, sizeof (database_name));
  memset (database_user, 0, sizeof (database_user));
  memset (database_passwd, 0, sizeof (database_passwd));
}

int
ux_prepare (char *sql_stmt, int flag, char auto_commit_mode,
	    T_NET_BUF * net_buf, T_REQ_INFO * req_info,
	    unsigned int query_seq_num)
{
  T_SRV_HANDLE *srv_handle = NULL;
  int err_code = CAS_NO_ERROR;
  int srv_h_id;

  srv_h_id = cas_mysql_prepare (&srv_handle, sql_stmt, flag,
				auto_commit_mode, query_seq_num);
  if (srv_h_id < 0)
    {
      err_code = srv_h_id;
      goto prepare_error;
    }

  if (DOES_CLIENT_UNDERSTAND_THE_PROTOCOL
      (req_info->client_version, PROTOCOL_V2) == false
      && srv_handle->stmt_type == CUBRID_STMT_SELECT)
    {
      srv_handle->send_metadata_before_execute = TRUE;
    }

  net_buf_cp_int (net_buf, srv_h_id, NULL);
  net_buf_cp_int (net_buf, -1, NULL);	/* result_cache_lifetime */
  net_buf_cp_byte (net_buf, srv_handle->stmt_type);
  net_buf_cp_int (net_buf, srv_handle->num_markers, NULL);

  srv_handle->prepare_call_info =
    make_prepare_call_info (srv_handle->num_markers);
  if (srv_handle->prepare_call_info == NULL)
    {
      goto prepare_error;
    }

  err_code = prepare_column_list_info_set (srv_handle, net_buf);
  if (err_code < 0)
    {
      goto prepare_error;
    }

  return srv_h_id;

prepare_error:
  NET_BUF_ERR_SET (net_buf);

  if (auto_commit_mode == true)
    {
      req_info->need_auto_commit = TRAN_AUTOROLLBACK;
    }

  errors_in_transaction++;

  if (srv_handle)
    {
      hm_srv_handle_free (srv_h_id);
    }

  return err_code;
}

int
ux_end_tran (int tran_type, bool reset_con_status)
{
  int err_code;

  if (!as_info->cur_statement_pooling)
    {
      hm_srv_handle_free_all (true);
    }
  else
    {
      hm_srv_handle_qresult_end_all (false);
    }

  err_code = cas_mysql_end_tran (tran_type);
  cas_log_debug (ARG_FILE_LINE,
		 "ux_end_tran: cas_mysql_end_tran() = %d", err_code);
  if (err_code >= 0)
    {
      unset_xa_prepare_flag ();
      if (reset_con_status)
	{
	  as_info->con_status = CON_STATUS_OUT_TRAN;
	  as_info->transaction_start_time = (time_t) 0;
	}
    }
  else
    {
      errors_in_transaction++;
    }

  if (get_db_connect_status () == DB_CONNECTION_STATUS_RESET)
    {
      as_info->reset_flag = TRUE;
    }

  return err_code;
}

int
ux_execute_all (T_SRV_HANDLE * srv_handle, char flag, int max_col_size,
		int max_row, int argc, void **argv, T_NET_BUF * net_buf,
		T_REQ_INFO * req_info, CACHE_TIME * clt_cache_time,
		int *clt_cache_reusable)
{
  return ux_execute_internal (srv_handle, flag, max_col_size,
			      max_row, argc, argv, net_buf, req_info,
			      clt_cache_time, clt_cache_reusable, TRUE);
}

int
ux_execute (T_SRV_HANDLE * srv_handle, char flag, int max_col_size,
	    int max_row, int argc, void **argv, T_NET_BUF * net_buf,
	    T_REQ_INFO * req_info, CACHE_TIME * clt_cache_time,
	    int *clt_cache_reusable)
{
  return ux_execute_internal (srv_handle, flag, max_col_size,
			      max_row, argc, argv, net_buf, req_info,
			      clt_cache_time, clt_cache_reusable, FALSE);
}

static int
ux_execute_internal (T_SRV_HANDLE * srv_handle, char flag, int max_col_size,
		     int max_row, int argc, void **argv, T_NET_BUF * net_buf,
		     T_REQ_INFO * req_info, CACHE_TIME * clt_cache_time,
		     int *clt_cache_reusable, bool is_all)
{
  int err_code;
  MYSQL_BIND *value_list = NULL;
  int num_bind = 0;
  int i;
  int n;
  int num_tuple, num_tuple_msg_offset;
  int stmt_type = -1, stmt_id = -1;
  int q_res_idx;
  int status;
  char is_prepared, async_flag;
  char is_first_stmt = TRUE;
  MYSQL_STMT *stmt = NULL;
  MYSQL_RES *metaResult;
  T_PREPARE_CALL_INFO *call_info;
  DB_VALUE **db_vals = NULL;
  T_BROKER_VERSION client_version = req_info->client_version;
  MYSQL_BIND *defines;

  hm_qresult_end (srv_handle, FALSE);

  stmt = (MYSQL_STMT *) srv_handle->session;
  stmt_id = stmt->stmt_id;
  call_info = srv_handle->prepare_call_info;

  if (srv_handle->stmt_type != CUBRID_STMT_SELECT
      && shm_appl->access_mode == READ_ONLY_ACCESS_MODE)
    {
      err_code =
	ERROR_INFO_SET_WITH_MSG (ER_DB_NO_MODIFICATIONS, DBMS_ERROR_INDICATOR,
				 "Attempted to update the database when updates are disabled.");
      goto execute_all_error;
    }

  num_bind = call_info->num_args;
  if (num_bind > 0)
    {
      value_list = (MYSQL_BIND *) call_info->bind;
      db_vals = (DB_VALUE **) call_info->dbval_args;
      if ((value_list == NULL) || (db_vals == NULL))
	{
	  err_code = ERROR_INFO_SET (CAS_ER_INTERNAL, CAS_ERROR_INDICATOR);
	  goto execute_all_error;
	}

      err_code =
	make_bind_value (num_bind, argc, argv, value_list, db_vals, net_buf,
			 MYSQL_TYPE_NULL);
      if (err_code < 0)
	{
	  goto execute_all_error;
	}

      err_code = cas_mysql_stmt_bind_param (stmt, value_list);
      if (err_code < 0)
	{
	  goto execute_all_error;
	}
    }

  q_res_idx = 0;
  SQL_LOG2_EXEC_BEGIN (as_info->cur_sql_log2, stmt_id);
  n = cas_mysql_stmt_execute (stmt);

  update_query_execution_count (as_info, srv_handle->stmt_type);

  SQL_LOG2_EXEC_END (as_info->cur_sql_log2, stmt_id, n);
  if (n < 0)
    {
      err_code = n;
      goto execute_all_error;
    }

  if (srv_handle->stmt_type == CUBRID_STMT_SELECT &&
      srv_handle->send_metadata_before_execute == FALSE)
    {
      err_code = set_metadata_info (srv_handle);
      if (err_code != CAS_NO_ERROR)
	{
	  goto execute_all_error;
	}

      srv_handle->send_metadata_before_execute = TRUE;
    }

  if ((is_all == TRUE) && (is_first_stmt == TRUE))
    {
      net_buf_cp_int (net_buf, 0, &num_tuple_msg_offset);	/* number of resultset */
      is_first_stmt = FALSE;
    }
  else if (is_all == FALSE)
    {
      net_buf_cp_int (net_buf, 0, &num_tuple_msg_offset);	/* number of resultset */
    }

  if (srv_handle->stmt_type == CUBRID_STMT_SELECT)
    {
      defines = srv_handle->q_result->defines;
      err_code = cas_mysql_stmt_bind_result (stmt, &defines);
      if (err_code < 0)
	{
	  goto execute_all_error;
	}

      err_code = cas_mysql_stmt_store_result (stmt);
      if (err_code < 0)
	{
	  goto execute_all_error;
	}
    }

  srv_handle->max_row = max_row;
  srv_handle->max_col_size = max_col_size;

  if (has_stmt_result_set (srv_handle->stmt_type) == false
      && srv_handle->auto_commit_mode == TRUE)
    {
      req_info->need_auto_commit = TRAN_AUTOCOMMIT;
    }

  for (i = 0; i < num_bind; i++)
    {
      db_value_clear (db_vals[i]);
    }

  net_buf_cp_byte (net_buf, 0);

  num_tuple = execute_info_set (srv_handle, net_buf, client_version, flag);
  net_buf_overwrite_int (net_buf, num_tuple_msg_offset, num_tuple);
  srv_handle->tuple_count = num_tuple;

  if (DOES_CLIENT_UNDERSTAND_THE_PROTOCOL (req_info->client_version,
					   PROTOCOL_V2))
    {
      net_buf_cp_byte (net_buf, 1);	/* include_column_info */
      net_buf_cp_int (net_buf, 0, NULL);	/* result_cache_lifetime */
      net_buf_cp_byte (net_buf, srv_handle->stmt_type);
      net_buf_cp_int (net_buf, srv_handle->num_markers, NULL);
      err_code = send_prepare_info (srv_handle, net_buf);
      if (err_code != CAS_NO_ERROR)
	{
	  goto execute_all_error;
	}
    }

  if (DOES_CLIENT_UNDERSTAND_THE_PROTOCOL (client_version, PROTOCOL_V5))
    {
      net_buf_cp_int (net_buf, shm_shard_id, NULL);
    }

  return num_tuple;

execute_all_error:
  NET_BUF_ERR_SET (net_buf);

  if (srv_handle->auto_commit_mode)
    {
      req_info->need_auto_commit = TRAN_AUTOROLLBACK;
    }

  errors_in_transaction++;

  if (db_vals)
    {
      for (i = 0; i < num_bind; i++)
	{
	  db_value_clear (db_vals[i]);
	}
    }

  return err_code;
}

int
ux_execute_array (T_SRV_HANDLE * srv_handle, int argc, void **argv,
		  T_NET_BUF * net_buf, T_REQ_INFO * req_info)
{
  int err_code;
  int i, n, va;
  int num_bind = 0;
  int num_value = 0;
  int stmt_id = -1;
  int num_query, num_query_msg_offset;
  char *err_msg;
  MYSQL_STMT *stmt = NULL;
  T_OBJECT ins_oid;
  T_PREPARE_CALL_INFO *call_info;
  MYSQL_BIND *value_list = NULL;
  DB_VALUE **db_vals = NULL;
  void **start_argv = argv;
  T_BROKER_VERSION client_version = req_info->client_version;

  if (srv_handle->auto_commit_mode == TRUE)
    {
      req_info->need_auto_commit = TRAN_AUTOCOMMIT;
    }

  if (srv_handle == NULL || srv_handle->schema_type >= CCI_SCH_FIRST)
    {
      err_code = ERROR_INFO_SET (CAS_ER_SRV_HANDLE, CAS_ERROR_INDICATOR);
      goto execute_array_error;
    }

  if (srv_handle->prepare_flag & CCI_PREPARE_CALL)
    {
      net_buf_cp_int (net_buf, 0, NULL);	/* result code */
      net_buf_cp_int (net_buf, 0, NULL);	/* num_query */
      goto return_success;
    }

  hm_qresult_end (srv_handle, FALSE);

  stmt = (MYSQL_STMT *) srv_handle->session;
  stmt_id = stmt->stmt_id;
  call_info = srv_handle->prepare_call_info;

  num_bind = call_info->num_args;
  if (num_bind > 0)
    {
      value_list = (MYSQL_BIND *) call_info->bind;
      db_vals = (DB_VALUE **) call_info->dbval_args;
      if ((value_list == NULL) || (db_vals == NULL))
	{
	  err_code = ERROR_INFO_SET (CAS_ER_INTERNAL, CAS_ERROR_INDICATOR);
	  goto execute_array_error;
	}
    }

  memset (&ins_oid, 0, sizeof (T_OBJECT));

  net_buf_cp_int (net_buf, 0, NULL);	/* result code */
  num_query = 0;
  net_buf_cp_int (net_buf, num_query, &num_query_msg_offset);

  if (argc <= 1)
    {
      return 0;
    }
  num_value = argc / 2;

  for (va = 0; va < num_value; va += num_bind)
    {
      num_query++;

      err_code =
	make_bind_value (num_bind, num_bind * 2, start_argv + (va * 2),
			 value_list, db_vals, net_buf, MYSQL_TYPE_NULL);
      if (err_code < 0)
	{
	  goto execute_array_error;
	}

      err_code = cas_mysql_stmt_bind_param (stmt, value_list);
      if (err_code < 0)
	{
	  goto exec_db_error;
	}

      SQL_LOG2_EXEC_BEGIN (as_info->cur_sql_log2, stmt_id);

      n = cas_mysql_stmt_execute (stmt);

      update_query_execution_count (as_info, srv_handle->stmt_type);

      SQL_LOG2_EXEC_END (as_info->cur_sql_log2, stmt_id, n);

      if (n < 0)
	{
	  goto exec_db_error;
	}

      n = cas_mysql_stmt_affected_rows (stmt);
      net_buf_cp_int (net_buf, n, NULL);
      net_buf_cp_object (net_buf, &ins_oid);

      continue;

    exec_db_error:
      errors_in_transaction++;

      err_code = err_info.err_number;
      err_msg = err_info.err_string;

      if (DOES_CLIENT_UNDERSTAND_THE_PROTOCOL (client_version, PROTOCOL_V3))
	{
	  net_buf_cp_int (net_buf, DBMS_ERROR_INDICATOR, NULL);
	}
      net_buf_cp_int (net_buf, err_code, NULL);
      net_buf_cp_int (net_buf, strlen (err_msg) + 1, NULL);
      net_buf_cp_str (net_buf, err_msg, strlen (err_msg) + 1);

    }

  net_buf_overwrite_int (net_buf, num_query_msg_offset, num_query);

  for (i = 0; i < num_bind; i++)
    {
      db_value_clear (db_vals[i]);
    }

return_success:
  if (DOES_CLIENT_UNDERSTAND_THE_PROTOCOL (client_version, PROTOCOL_V5))
    {
      net_buf_cp_int (net_buf, shm_shard_id, NULL);
    }

  return 0;

execute_array_error:
  NET_BUF_ERR_SET (net_buf);

  if (srv_handle->auto_commit_mode)
    {
      req_info->need_auto_commit = TRAN_AUTOROLLBACK;
    }

  errors_in_transaction++;

  if (db_vals)
    {
      for (i = 0; i < num_bind; i++)
	{
	  db_value_clear (db_vals[i]);
	}
    }

  return err_code;
}

int
ux_execute_call (T_SRV_HANDLE * srv_handle, char flag, int max_col_size,
		 int max_row, int argc, void **argv, T_NET_BUF * net_buf,
		 T_REQ_INFO * req_info, CACHE_TIME * clt_cache_time,
		 int *clt_cache_reusable)
{
  int err_code;

  err_code = ERROR_INFO_SET (CAS_ER_NOT_IMPLEMENTED, CAS_ERROR_INDICATOR);
  NET_BUF_ERR_SET (net_buf);

  return err_code;
}

int
ux_fetch (T_SRV_HANDLE * srv_handle, int cursor_pos, int fetch_count,
	  char fetch_flag, int result_set_index, T_NET_BUF * net_buf,
	  T_REQ_INFO * req_info)
{
  int err_code;

  if (srv_handle == NULL)
    {
      err_code = ERROR_INFO_SET (CAS_ER_SRV_HANDLE, CAS_ERROR_INDICATOR);
      goto fetch_error;
    }

  if (srv_handle->next_cursor_pos != cursor_pos)
    {
      err_code =
	ERROR_INFO_SET (CAS_ER_INVALID_CURSOR_POS, CAS_ERROR_INDICATOR);
      goto fetch_error;
    }

  net_buf_cp_int (net_buf, 0, NULL);	/* result code */

  if (fetch_count <= 0)
    fetch_count = 100;

  err_code = fetch_result (srv_handle, cursor_pos,
			   fetch_count, fetch_flag,
			   result_set_index, net_buf, req_info);

  if (err_code < 0)
    {
      goto fetch_error;
    }

  return 0;

fetch_error:
  NET_BUF_ERR_SET (net_buf);

  if (cas_shard_flag == ON
      && (srv_handle != NULL
	  && srv_handle->auto_commit_mode == TRUE
	  && srv_handle->forward_only_cursor == TRUE))
    {
      req_info->need_auto_commit = TRAN_AUTOROLLBACK;
    }

  errors_in_transaction++;
  return err_code;
}


int
ux_get_db_version (T_NET_BUF * net_buf, T_REQ_INFO * req_info)
{
  char *p;

  p = (char *) makestring (BUILD_NUMBER);

  net_buf_cp_int (net_buf, 0, NULL);	/* result code */
  net_buf_cp_str (net_buf, p, strlen (p) + 1);

  return 0;
}

static int
make_bind_value (int num_bind, int argc, void **argv, MYSQL_BIND * value_list,
		 DB_VALUE ** db_vals, T_NET_BUF * net_buf, int desired_type)
{
  int i, type_idx, val_idx;
  int err_code;

  if (num_bind != (argc / 2))
    {
      return ERROR_INFO_SET (CAS_ER_NUM_BIND, CAS_ERROR_INDICATOR);
    }

  for (i = 0; i < num_bind; i++)
    {
      type_idx = 2 * i;
      val_idx = 2 * i + 1;
      err_code =
	netval_to_dbval (argv[type_idx], argv[val_idx], &(value_list[i]),
			 db_vals[i], net_buf, desired_type);
      if (err_code < 0)
	{
	  return err_code;
	}
    }				/* end of for */

  return 0;
}

void
ux_free_result (void *res)
{
  int i;
  int column_count;
  T_QUERY_RESULT *result;

  result = res;
  if (result == NULL)
    {
      return;
    }

  column_count = result->column_count;
  if (column_count <= 0)
    {
      return;
    }
  for (i = 0; i < column_count; i++)
    {
      db_value_clear (&(result->columns[i]));
    }
  FREE_MEM (result->columns);
  FREE_MEM (result->defines);
  result->column_count = 0;

  return;
}

char
ux_db_type_to_cas_type (int db_type)
{
  char cas_type;
  switch (db_type)
    {
      /* DB_TYPE_NUMERIC  */
    case MYSQL_TYPE_DECIMAL:
    case MYSQL_TYPE_NEWDECIMAL:
      cas_type = CCI_U_TYPE_NUMERIC;
      break;
      /* DB_TYPE_INTEGER */
    case MYSQL_TYPE_INT24:
    case MYSQL_TYPE_LONG:
      cas_type = CCI_U_TYPE_INT;
      break;
      /* DB_TYPE_SHORT */
    case MYSQL_TYPE_SHORT:
    case MYSQL_TYPE_TINY:
      cas_type = CCI_U_TYPE_SHORT;
      break;
      /* DB_TYPE_FLOAT */
    case MYSQL_TYPE_FLOAT:
      cas_type = CCI_U_TYPE_FLOAT;
      break;
      /* DB_TYPE_DOUBLE */
    case MYSQL_TYPE_DOUBLE:
      cas_type = CCI_U_TYPE_DOUBLE;
      break;
      /* DB_TYPE_TIMESTAMP */
    case MYSQL_TYPE_TIMESTAMP:
      cas_type = CCI_U_TYPE_TIMESTAMP;
      break;
      /* DB_TYPE_BIGINT */
    case MYSQL_TYPE_LONGLONG:
      cas_type = CCI_U_TYPE_BIGINT;
      break;
      /* DB_TYPE_DATE */
    case MYSQL_TYPE_DATE:
    case MYSQL_TYPE_NEWDATE:
      cas_type = CCI_U_TYPE_DATE;
      break;
      /* DB_TYPE_TIME */
    case MYSQL_TYPE_TIME:
      cas_type = CCI_U_TYPE_TIME;
      break;
      /* DB_TYPE_DATETIME */
    case MYSQL_TYPE_DATETIME:
      cas_type = CCI_U_TYPE_DATETIME;
      break;
      /* DB_TYPE_VARCHAR */
    case MYSQL_TYPE_VARCHAR:
    case MYSQL_TYPE_VAR_STRING:
      cas_type = CCI_U_TYPE_STRING;
      break;
      /* DB_TYPE_CHAR */
    case MYSQL_TYPE_STRING:
      cas_type = CCI_U_TYPE_CHAR;
      break;
      /* DB_TYPE_BIT */
    case MYSQL_TYPE_BIT:
      cas_type = CCI_U_TYPE_BIT;
      break;
      /* DB_TYPE_SET */
    case MYSQL_TYPE_SET:
      cas_type = CCI_U_TYPE_SET;
      break;
      /* DB_TYPE_VARBIT */
    case MYSQL_TYPE_TINY_BLOB:
    case MYSQL_TYPE_MEDIUM_BLOB:
    case MYSQL_TYPE_LONG_BLOB:
    case MYSQL_TYPE_BLOB:
      cas_type = CCI_U_TYPE_VARBIT;
      break;
      /* DB_TYPE_NULL */
    case MYSQL_TYPE_NULL:
      cas_type = CCI_U_TYPE_NULL;
      break;
    case MYSQL_TYPE_ENUM:
      cas_type = CCI_U_TYPE_ENUM;
      break;
    case MYSQL_TYPE_YEAR:
    case MYSQL_TYPE_GEOMETRY:
    default:
      cas_type = CCI_U_TYPE_UNKNOWN;
      break;
    }
  return cas_type;
}
static void
prepare_column_info_set (T_NET_BUF * net_buf, char ut, short scale, int prec,
			 const char *col_name, const char *default_value,
			 char auto_increment, char unique_key,
			 char primary_key, char reverse_index,
			 char reverse_unique, char foreign_key, char shared,
			 const char *attr_name, const char *class_name,
			 char is_non_null)
{
  const char *attr_name_p, *class_name_p;
  int attr_name_len, class_name_len;

  net_buf_column_info_set (net_buf, ut, scale, prec, col_name);

  attr_name_p = (attr_name != NULL) ? attr_name : "";
  attr_name_len = strlen (attr_name_p);

  class_name_p = (class_name != NULL) ? class_name : "";
  class_name_len = strlen (class_name_p);

  net_buf_cp_int (net_buf, attr_name_len + 1, NULL);
  net_buf_cp_str (net_buf, attr_name_p, attr_name_len + 1);

  net_buf_cp_int (net_buf, class_name_len + 1, NULL);
  net_buf_cp_str (net_buf, class_name_p, class_name_len + 1);

  if (is_non_null >= 1)
    {
      is_non_null = 1;
    }
  else if (is_non_null < 0)
    {
      is_non_null = 0;
    }

  net_buf_cp_byte (net_buf, is_non_null);

  /* 3.0 protocol */
  if (default_value == NULL)
    {
      net_buf_cp_int (net_buf, 1, NULL);
      net_buf_cp_byte (net_buf, '\0');
    }
  else
    {
      char *tmp_str;

      ALLOC_COPY (tmp_str, default_value);
      if (tmp_str == NULL)
	{
	  net_buf_cp_int (net_buf, 1, NULL);
	  net_buf_cp_byte (net_buf, '\0');
	}
      else
	{
	  ut_trim (tmp_str);
	  net_buf_cp_int (net_buf, strlen (tmp_str) + 1, NULL);
	  net_buf_cp_str (net_buf, tmp_str, strlen (tmp_str) + 1);
	  FREE_MEM (tmp_str);
	}
    }

  net_buf_cp_byte (net_buf, auto_increment);
  net_buf_cp_byte (net_buf, unique_key);
  net_buf_cp_byte (net_buf, primary_key);
  net_buf_cp_byte (net_buf, reverse_index);
  net_buf_cp_byte (net_buf, reverse_unique);
  net_buf_cp_byte (net_buf, foreign_key);
  net_buf_cp_byte (net_buf, shared);
}

static int
cubval_to_mysqlval (int cub_type)
{
  if (cub_type > CCI_U_TYPE_LAST)
    return ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
  switch (cub_type)
    {
    case CCI_U_TYPE_NULL:
      return MYSQL_TYPE_NULL;
    case CCI_U_TYPE_CHAR:
      return MYSQL_TYPE_STRING;
    case CCI_U_TYPE_STRING:
    case CCI_U_TYPE_VARNCHAR:
      return MYSQL_TYPE_VARCHAR;
    case CCI_U_TYPE_BIT:
      return MYSQL_TYPE_BIT;
    case CCI_U_TYPE_VARBIT:
      return MYSQL_TYPE_BLOB;
    case CCI_U_TYPE_INT:
      return MYSQL_TYPE_LONG;
    case CCI_U_TYPE_BIGINT:
      return MYSQL_TYPE_LONGLONG;
    case CCI_U_TYPE_SHORT:
      return MYSQL_TYPE_SHORT;
    case CCI_U_TYPE_FLOAT:
      return MYSQL_TYPE_FLOAT;
    case CCI_U_TYPE_DOUBLE:
    case CCI_U_TYPE_MONETARY:
      return MYSQL_TYPE_DOUBLE;
    case CCI_U_TYPE_DATE:
      return MYSQL_TYPE_DATE;
    case CCI_U_TYPE_TIME:
      return MYSQL_TYPE_TIME;
    case CCI_U_TYPE_TIMESTAMP:
    case CCI_U_TYPE_DATETIME:
      return MYSQL_TYPE_TIMESTAMP;
    case CCI_U_TYPE_ENUM:
      return MYSQL_TYPE_ENUM;
    case CCI_U_TYPE_NCHAR:
    case CCI_U_TYPE_NUMERIC:
    case CCI_U_TYPE_OBJECT:
    case CCI_U_TYPE_SET:
    case CCI_U_TYPE_MULTISET:
    case CCI_U_TYPE_SEQUENCE:
    case CCI_U_TYPE_RESULTSET:
    default:
      cas_log_write (0, false, "type convert fail : [%d]", cub_type);
      return ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
    }
}

static int
netval_to_dbval (void *net_type, void *net_value, MYSQL_BIND * out_val,
		 DB_VALUE * db_val, T_NET_BUF * net_buf, int desired_type)
{
  char cub_type;
  int type;
  int err_code = 0;
  char *data;
  int data_size;
  char coercion_flag = TRUE;

  net_arg_get_char (cub_type, net_type);
  type = cubval_to_mysqlval (cub_type);
  if (type < 0)
    {
      return type;
    }

  net_arg_get_size (&data_size, net_value);
  if (data_size <= 0)
    {
      type = MYSQL_TYPE_NULL;
      data_size = 0;
    }

  out_val->buffer = NULL;
  out_val->buffer_length = 0;
  out_val->is_null = false;
  out_val->buffer_type = type;
  out_val->is_unsigned = FALSE;
  out_val->length = NULL;	/* for out bind */

  db_val->need_clear = false;
  db_val->db_type = type;

  switch (type)
    {
    case MYSQL_TYPE_NULL:
      break;
    case MYSQL_TYPE_BLOB:
      {
	char *value;
	net_arg_get_str (&value, (int *) (&db_val->size), net_value);

	data = (char *) MALLOC (db_val->size);
	if (data == NULL)
	  {
	    return ERROR_INFO_SET (CAS_ER_NO_MORE_MEMORY,
				   CAS_ERROR_INDICATOR);
	  }
	if (db_val->size > 0)
	  {
	    memcpy (data, value, db_val->size);
	  }

	db_val->data.p = data;
	db_val->need_clear = true;

	out_val->buffer = (void *) db_val->data.p;
	out_val->buffer_length = db_val->size;
      }
      break;
    case MYSQL_TYPE_STRING:	/* CHAR(n) */
    case MYSQL_TYPE_VARCHAR:	/* VARCHAR(n) */
      {
	char *value;
	net_arg_get_str (&value, (int *) (&db_val->size), net_value);

	data = (char *) MALLOC (db_val->size);
	if (data == NULL)
	  {
	    return ERROR_INFO_SET (CAS_ER_NO_MORE_MEMORY,
				   CAS_ERROR_INDICATOR);
	  }
	if (db_val->size > 0)
	  {
	    memcpy (data, value, db_val->size);
	  }

	db_val->data.p = data;
	db_val->need_clear = true;

	out_val->buffer = (void *) db_val->data.p;
	out_val->buffer_length = db_val->size - 1;	/* -1 is \0 */
      }
      break;
    case MYSQL_TYPE_INT24:
    case MYSQL_TYPE_LONG:
      {
	int *i_val = &(db_val->data.i);

	net_arg_get_int (i_val, net_value);
	db_val->size = sizeof (int);

	out_val->buffer = (void *) &(db_val->data.i);
	out_val->buffer_length = db_val->size;
      }
      break;
    case MYSQL_TYPE_LONGLONG:
      {
	int64_t *l_val = &(db_val->data.bi);

	net_arg_get_bigint (l_val, net_value);
	db_val->size = sizeof (int64_t);

	out_val->buffer = (void *) &(db_val->data.bi);
	out_val->buffer_length = db_val->size;
      }
      break;
    case MYSQL_TYPE_SHORT:
      {
	short *s_val = &(db_val->data.sh);

	net_arg_get_short (s_val, net_value);
	db_val->size = sizeof (short);

	out_val->buffer = (void *) &(db_val->data.sh);
	out_val->buffer_length = db_val->size;
      }
      break;
    case MYSQL_TYPE_FLOAT:
      {
	float *f_val = &(db_val->data.f);

	net_arg_get_float (f_val, net_value);
	db_val->size = sizeof (float);

	out_val->buffer = (void *) &(db_val->data.f);
	out_val->buffer_length = db_val->size;
      }
      break;
    case MYSQL_TYPE_DOUBLE:
      {
	double *d_val = &(db_val->data.d);

	net_arg_get_double (d_val, net_value);
	db_val->size = sizeof (double);

	out_val->buffer = (void *) &(db_val->data.d);
	out_val->buffer_length = db_val->size;
      }
      break;
    case MYSQL_TYPE_DATE:
      {
	MYSQL_TIME *mt = &(db_val->data.t);
	short year, month, day;

	net_arg_get_date (&year, &month, &day, net_value);
	mt->year = year;
	mt->month = month;
	mt->day = day;
	db_val->size = sizeof (MYSQL_TIME);

	out_val->buffer = (void *) &(db_val->data.t);
	out_val->buffer_length = db_val->size;
      }
      break;
    case MYSQL_TYPE_TIME:
      {
	MYSQL_TIME *mt = &(db_val->data.t);
	short hour, minute, second;

	net_arg_get_time (&hour, &minute, &second, net_value);
	mt->hour = hour;
	mt->minute = minute;
	mt->second = second;
	db_val->size = sizeof (MYSQL_TIME);

	out_val->buffer = (void *) &(db_val->data.t);
	out_val->buffer_length = db_val->size;
      }
      break;
    case MYSQL_TYPE_TIMESTAMP:
      {
	MYSQL_TIME *mt = &(db_val->data.t);
	short year, month, day, hour, minute, second;

	net_arg_get_timestamp (&year, &month, &day, &hour,
			       &minute, &second, net_value);
	mt->year = year;
	mt->month = month;
	mt->day = day;
	mt->hour = hour;
	mt->minute = minute;
	mt->second = second;
	db_val->size = sizeof (MYSQL_TIME);

	out_val->buffer = (void *) &(db_val->data.t);
	out_val->buffer_length = db_val->size;
      }
      break;
    default:
      return ERROR_INFO_SET (CAS_ER_UNKNOWN_U_TYPE, CAS_ERROR_INDICATOR);
    }

  return data_size;
}

static int
cur_tuple (T_SRV_HANDLE * srv_handle, int tuple, T_NET_BUF * net_buf)
{
  int ncols;
  void *val;
  int type;
  int i;
  int error;
  int data_size = 0;
  MYSQL_STMT *stmt = (MYSQL_STMT *) srv_handle->session;
  T_QUERY_RESULT *q_result = (T_QUERY_RESULT *) srv_handle->q_result;
  MYSQL_BIND *defines = q_result->defines;
  my_bool is_null;
  unsigned long length;


  if (stmt == NULL)
    {
      return ERROR_INFO_SET (CAS_ER_INTERNAL, CAS_ERROR_INDICATOR);
    }

  ncols = cas_mysql_stmt_num_fields (stmt);
  if (ncols != q_result->column_count)
    {
      return ERROR_INFO_SET (CAS_ER_INTERNAL, CAS_ERROR_INDICATOR);
    }
  for (i = 0; i < ncols; i++)
    {
      val = defines[i].buffer;
      type = defines[i].buffer_type;
      is_null = (my_bool) * (defines[i].is_null);
      length = (unsigned long) *(defines[i].length);

      data_size += dbval_to_net_buf (val, type, is_null, length, net_buf);
    }

  return data_size;
}

static int
dbval_to_net_buf (void *val, int type, my_bool is_null, unsigned long length,
		  T_NET_BUF * net_buf)
{
  int data_size;

  if (is_null == true)
    {
      net_buf_cp_int (net_buf, -1, NULL);
      return 4;
    }

  switch (type)
    {
    case MYSQL_TYPE_INT24:
    case MYSQL_TYPE_LONG:
      {
	int int_val;
	memcpy ((char *) &int_val, (char *) val, length);
	add_res_data_int (net_buf, int_val, 0);
	data_size = 4 + 4 + 0;
	break;
      }
    case MYSQL_TYPE_LONGLONG:
      {
	int64_t bigint_val;
	memcpy ((char *) &bigint_val, (char *) val, length);
	add_res_data_bigint (net_buf, bigint_val, 0);
	data_size = 4 + 8 + 0;
	break;
      }
    case MYSQL_TYPE_SHORT:
    case MYSQL_TYPE_TINY:
      {
	short smallint;
	memcpy ((char *) &smallint, (char *) val, NET_SIZE_SHORT);
	add_res_data_short (net_buf, smallint, 0);
	data_size = 4 + 2 + 0;
	break;
      }
    case MYSQL_TYPE_FLOAT:
      {
	float f_val;
	memcpy ((char *) &f_val, (char *) val, length);
	add_res_data_float (net_buf, f_val, 0);
	data_size = 4 + 4 + 0;
	break;
      }
    case MYSQL_TYPE_DOUBLE:
      {
	double d_val;
	memcpy ((char *) &d_val, (char *) val, length);
	add_res_data_double (net_buf, d_val, 0);
	data_size = 4 + 8 + 0;
	break;
      }
      /*
         case MYSQL_TYPE_DECIMAL:
         case MYSQL_TYPE_NEWDECIMAL:
         {
         add_res_data_string(net_buf, val, length, 0);
         data_size = 4 + length + 1 + 0;
         break;
         }
       */
    case MYSQL_TYPE_TIMESTAMP:
      {
	MYSQL_TIME *tm = (MYSQL_TIME *) val;
	add_res_data_timestamp (net_buf, (short) tm->year, (short) tm->month,
				(short) tm->day, (short) tm->hour,
				(short) tm->minute, (short) tm->second, 0);
	data_size = 4 + NET_SIZE_TIMESTAMP + 0;
	break;
      }
    case MYSQL_TYPE_DATETIME:
      {
	MYSQL_TIME *tm = (MYSQL_TIME *) val;
	add_res_data_datetime (net_buf, (short) tm->year, (short) tm->month,
			       (short) tm->day, (short) tm->hour,
			       (short) tm->minute, (short) tm->second,
			       (short) 0, 0);
	data_size = 4 + NET_SIZE_DATETIME + 0;
	break;
      }
    case MYSQL_TYPE_DATE:
    case MYSQL_TYPE_NEWDATE:
      {
	MYSQL_TIME *tm = (MYSQL_TIME *) val;
	add_res_data_date (net_buf, (short) tm->year, (short) tm->month,
			   (short) tm->day, 0);
	data_size = 4 + NET_SIZE_DATE + 0;
	break;
      }
    case MYSQL_TYPE_TIME:
      {
	MYSQL_TIME *tm = (MYSQL_TIME *) val;
	add_res_data_time (net_buf, (short) tm->hour, (short) tm->minute,
			   (short) tm->second, 0);
	data_size = 4 + NET_SIZE_TIME + 0;
	break;
      }
    case MYSQL_TYPE_VARCHAR:
    case MYSQL_TYPE_VAR_STRING:
    case MYSQL_TYPE_STRING:
      {
	char *str;
	str = val;
	add_res_data_string (net_buf, str, length, 0);
	data_size = 4 + length + 1 + 0;
	break;
      }
    case MYSQL_TYPE_BIT:
    case MYSQL_TYPE_TINY_BLOB:
    case MYSQL_TYPE_MEDIUM_BLOB:
    case MYSQL_TYPE_LONG_BLOB:
    case MYSQL_TYPE_BLOB:
      {
	add_res_data_bytes (net_buf, (char *) val, length, 0);
	data_size = 4 + length + 0;
	break;
      }
    case MYSQL_TYPE_NULL:
    case MYSQL_TYPE_SET:
    case MYSQL_TYPE_YEAR:
    case MYSQL_TYPE_ENUM:
    case MYSQL_TYPE_GEOMETRY:
    default:
      net_buf_cp_int (net_buf, -1, NULL);
      data_size = 4;
      break;
    }

  return data_size;
}

#if 0
static int
get_attr_name_from_argv (int argc, void **argv, char ***ret_attr_name)
{
  int attr_num;
  char **attr_name = NULL;
  int i;

  attr_num = argc - 1;
  attr_name = (char **) MALLOC (sizeof (char *) * attr_num);
  if (attr_name == NULL)
    {
      return CAS_ER_NO_MORE_MEMORY;
    }
  for (i = 0; i < attr_num; i++)
    {
      int name_size;
      char *tmp_p;

      net_arg_get_str (&tmp_p, &name_size, argv[i + 1]);
      if (name_size <= 1 || tmp_p[name_size - 1] != '\0')
	{
	  FREE_MEM (attr_name);
	  return CAS_ER_OBJECT;
	}
      attr_name[i] = tmp_p;
    }

  *ret_attr_name = attr_name;
  return attr_num;
}
#endif

static int
fetch_result (T_SRV_HANDLE * srv_handle, int cursor_pos, int fetch_count,
	      char fetch_flag, int result_set_idx, T_NET_BUF * net_buf,
	      T_REQ_INFO * req_info)
{
  T_OBJECT tuple_obj;
  char fetch_end_flag = 0;
  int err_code = 0;
  int num_tuple;
  int tuple;
  T_QUERY_RESULT *result;
  int num_tuple_msg_offset;
  int net_buf_size;
  T_BROKER_VERSION client_version = req_info->client_version;

  result = (T_QUERY_RESULT *) srv_handle->q_result;
  if (result == NULL || has_stmt_result_set (srv_handle->stmt_type) == false)
    {
      return ERROR_INFO_SET (CAS_ER_NO_MORE_DATA, CAS_ERROR_INDICATOR);
    }

  num_tuple = cas_mysql_stmt_num_rows ((MYSQL_STMT *) srv_handle->session);
  net_buf_cp_int (net_buf, num_tuple, &num_tuple_msg_offset);

  if (cas_shard_flag == ON)
    {
      net_buf_size = SHARD_NET_BUF_SIZE;
    }
  else
    {
      net_buf_size = NET_BUF_SIZE;
    }

  tuple = 0;
  while (CHECK_NET_BUF_SIZE (net_buf, net_buf_size))
    {
      err_code = cas_mysql_stmt_fetch ((MYSQL_STMT *) srv_handle->session);
      if (err_code < 0)
	{
	  return err_code;
	}
      else if (err_code == MYSQL_NO_DATA)
	{
	  break;
	}
      memset ((char *) &tuple_obj, 0, sizeof (T_OBJECT));

      net_buf_cp_int (net_buf, cursor_pos, NULL);

      net_buf_cp_object (net_buf, &tuple_obj);

      err_code = cur_tuple (srv_handle, tuple++, net_buf);
      if (err_code < 0)
	{
	  return err_code;
	}
      if (num_tuple < tuple)
	{
	  return ERROR_INFO_SET (CAS_ER_INTERNAL, CAS_ERROR_INDICATOR);
	}

      cursor_pos++;
      if (num_tuple < cursor_pos)
	{
	  err_code = MYSQL_NO_DATA;
	  break;
	}
    }

  if (err_code == MYSQL_NO_DATA)
    {
      fetch_end_flag = 1;

      if (check_auto_commit_after_fetch_done (srv_handle) == true)
	{
	  req_info->need_auto_commit = TRAN_AUTOCOMMIT;
	}
    }

  srv_handle->next_cursor_pos += tuple;

  if (DOES_CLIENT_UNDERSTAND_THE_PROTOCOL (client_version, PROTOCOL_V5))
    {
      net_buf_cp_byte (net_buf, fetch_end_flag);
    }

  net_buf_overwrite_int (net_buf, num_tuple_msg_offset, tuple);
  return 0;

}

#if 0
static int
fetch_primary_key (T_SRV_HANDLE * srv_handle, int cursor_pos, int fetch_count,
		   char fetch_flag, int result_set_idx, T_NET_BUF * net_buf,
		   T_REQ_INFO * req_info)
{
  net_buf_cp_int (net_buf, 0, NULL);
  return 0;
}
#endif /* 0 */

static void
add_res_data_bytes (T_NET_BUF * net_buf, char *str, int size, char type)
{
  if (type)
    {
      net_buf_cp_int (net_buf, size + 1, NULL);	/* type */
      net_buf_cp_byte (net_buf, type);
    }
  else
    {
      net_buf_cp_int (net_buf, size, NULL);
    }
  net_buf_cp_str (net_buf, str, size);
  /* do not append NULL terminator */
}

static void
add_res_data_string (T_NET_BUF * net_buf, const char *str, int size,
		     char type)
{
  if (type)
    {
      net_buf_cp_int (net_buf, size + 1 + 1, NULL);	/* type, NULL terminator */
      net_buf_cp_byte (net_buf, type);
    }
  else
    {
      net_buf_cp_int (net_buf, size + 1, NULL);	/* NULL terminator */
    }
  net_buf_cp_str (net_buf, str, size);
  net_buf_cp_byte (net_buf, '\0');
}

static void
add_res_data_int (T_NET_BUF * net_buf, int value, char type)
{
  if (type)
    {
      net_buf_cp_int (net_buf, 5, NULL);
      net_buf_cp_byte (net_buf, type);
      net_buf_cp_int (net_buf, value, NULL);
    }
  else
    {
      net_buf_cp_int (net_buf, 4, NULL);
      net_buf_cp_int (net_buf, value, NULL);
    }
}

static void
add_res_data_bigint (T_NET_BUF * net_buf, DB_BIGINT value, char type)
{
  if (type)
    {
      net_buf_cp_int (net_buf, 9, NULL);
      net_buf_cp_byte (net_buf, type);
      net_buf_cp_bigint (net_buf, value, NULL);
    }
  else
    {
      net_buf_cp_int (net_buf, 8, NULL);
      net_buf_cp_bigint (net_buf, value, NULL);
    }
}

static void
add_res_data_short (T_NET_BUF * net_buf, short value, char type)
{
  if (type)
    {
      net_buf_cp_int (net_buf, 3, NULL);
      net_buf_cp_byte (net_buf, type);
      net_buf_cp_short (net_buf, value);
    }
  else
    {
      net_buf_cp_int (net_buf, 2, NULL);
      net_buf_cp_short (net_buf, value);
    }
}

static void
add_res_data_float (T_NET_BUF * net_buf, float value, char type)
{
  if (type)
    {
      net_buf_cp_int (net_buf, 5, NULL);
      net_buf_cp_byte (net_buf, type);
      net_buf_cp_float (net_buf, value);
    }
  else
    {
      net_buf_cp_int (net_buf, 4, NULL);
      net_buf_cp_float (net_buf, value);
    }
}

static void
add_res_data_double (T_NET_BUF * net_buf, double value, char type)
{
  if (type)
    {
      net_buf_cp_int (net_buf, 9, NULL);
      net_buf_cp_byte (net_buf, type);
      net_buf_cp_double (net_buf, value);
    }
  else
    {
      net_buf_cp_int (net_buf, 8, NULL);
      net_buf_cp_double (net_buf, value);
    }
}

static void
add_res_data_timestamp (T_NET_BUF * net_buf, short yr, short mon, short day,
			short hh, short mm, short ss, char type)
{
  if (type)
    {
      net_buf_cp_int (net_buf, NET_SIZE_TIMESTAMP + 1, NULL);
      net_buf_cp_byte (net_buf, type);
    }
  else
    net_buf_cp_int (net_buf, NET_SIZE_TIMESTAMP, NULL);

  net_buf_cp_short (net_buf, yr);
  net_buf_cp_short (net_buf, mon);
  net_buf_cp_short (net_buf, day);
  net_buf_cp_short (net_buf, hh);
  net_buf_cp_short (net_buf, mm);
  net_buf_cp_short (net_buf, ss);
}

static void
add_res_data_datetime (T_NET_BUF * net_buf, short yr, short mon, short day,
		       short hh, short mm, short ss, short ms, char type)
{
  if (type)
    {
      net_buf_cp_int (net_buf, NET_SIZE_DATETIME + 1, NULL);
      net_buf_cp_byte (net_buf, type);
    }
  else
    {
      net_buf_cp_int (net_buf, NET_SIZE_DATETIME, NULL);
    }

  net_buf_cp_short (net_buf, yr);
  net_buf_cp_short (net_buf, mon);
  net_buf_cp_short (net_buf, day);
  net_buf_cp_short (net_buf, hh);
  net_buf_cp_short (net_buf, mm);
  net_buf_cp_short (net_buf, ss);
  net_buf_cp_short (net_buf, ms);
}

static void
add_res_data_time (T_NET_BUF * net_buf, short hh, short mm, short ss,
		   char type)
{
  if (type)
    {
      net_buf_cp_int (net_buf, NET_SIZE_TIME + 1, NULL);
      net_buf_cp_byte (net_buf, type);
    }
  else
    {
      net_buf_cp_int (net_buf, NET_SIZE_TIME, NULL);
    }
  net_buf_cp_short (net_buf, hh);
  net_buf_cp_short (net_buf, mm);
  net_buf_cp_short (net_buf, ss);
}

static void
add_res_data_date (T_NET_BUF * net_buf, short yr, short mon, short day,
		   char type)
{
  if (type)
    {
      net_buf_cp_int (net_buf, NET_SIZE_DATE + 1, NULL);
      net_buf_cp_byte (net_buf, type);
    }
  else
    {
      net_buf_cp_int (net_buf, NET_SIZE_DATE, NULL);
    }
  net_buf_cp_short (net_buf, yr);
  net_buf_cp_short (net_buf, mon);
  net_buf_cp_short (net_buf, day);
}

#if 0
static void
add_res_data_object (T_NET_BUF * net_buf, T_OBJECT * obj, char type)
{
  if (type)
    {
      net_buf_cp_int (net_buf, NET_SIZE_OBJECT + 1, NULL);
      net_buf_cp_byte (net_buf, type);
    }
  else
    {
      net_buf_cp_int (net_buf, NET_SIZE_OBJECT, NULL);
    }

  net_buf_cp_object (net_buf, obj);
}

static void
trigger_event_str (DB_TRIGGER_EVENT trig_event, char *buf)
{
  if (trig_event == TR_EVENT_UPDATE)
    strcpy (buf, "UPDATE");
  else if (trig_event == TR_EVENT_STATEMENT_UPDATE)
    strcpy (buf, "STATEMENT_UPDATE");
  else if (trig_event == TR_EVENT_DELETE)
    strcpy (buf, "DELETE");
  else if (trig_event == TR_EVENT_STATEMENT_DELETE)
    strcpy (buf, "STATEMENT_DELETE");
  else if (trig_event == TR_EVENT_INSERT)
    strcpy (buf, "INSERT");
  else if (trig_event == TR_EVENT_STATEMENT_INSERT)
    strcpy (buf, "STATEMENT_INSERT");
  else if (trig_event == TR_EVENT_ALTER)
    strcpy (buf, "ALTER");
  else if (trig_event == TR_EVENT_DROP)
    strcpy (buf, "DROP");
  else if (trig_event == TR_EVENT_COMMIT)
    strcpy (buf, "COMMIT");
  else if (trig_event == TR_EVENT_ROLLBACK)
    strcpy (buf, "ROLLBACK");
  else if (trig_event == TR_EVENT_ABORT)
    strcpy (buf, "ABORT");
  else if (trig_event == TR_EVENT_TIMEOUT)
    strcpy (buf, "TIMEOUT");
  else if (trig_event == TR_EVENT_ALL)
    strcpy (buf, "ALL");
  else
    strcpy (buf, "NULL");
}

static void
trigger_status_str (DB_TRIGGER_STATUS trig_status, char *buf)
{
  if (trig_status == TR_STATUS_INACTIVE)
    strcpy (buf, "INACTIVE");
  else if (trig_status == TR_STATUS_ACTIVE)
    strcpy (buf, "ACTIVE");
  else if (trig_status == TR_STATUS_INVALID)
    strcpy (buf, "INVALID");
  else
    strcpy (buf, "");
}

static void
trigger_time_str (DB_TRIGGER_TIME trig_time, char *buf)
{
  if (trig_time == TR_TIME_BEFORE)
    strcpy (buf, "BEFORE");
  else if (trig_time == TR_TIME_AFTER)
    strcpy (buf, "AFTER");
  else if (trig_time == TR_TIME_DEFERRED)
    strcpy (buf, "DEFERRED");
  else
    strcpy (buf, "");
}
#endif /* 0 */

static char
get_stmt_type (char *stmt)
{
  char *tbuf = stmt;
  int size = strlen (stmt);
  int i;
  for (i = 0; i < size; i++)
    {
      if (char_isalpha (tbuf[i]))
	{
	  break;
	}
    }
  if (i >= strlen (stmt))
    {
      return CUBRID_MAX_STMT_TYPE;
    }
  tbuf += i;
  if (strncasecmp (tbuf, "insert", 6) == 0)
    {
      return CUBRID_STMT_INSERT;
    }
  else if (strncasecmp (tbuf, "update", 6) == 0)
    {
      return CUBRID_STMT_UPDATE;
    }
  else if (strncasecmp (tbuf, "delete", 6) == 0)
    {
      return CUBRID_STMT_DELETE;
    }
  else if (strncasecmp (tbuf, "select", 6) == 0)
    {
      return CUBRID_STMT_SELECT;
    }
  else
    {
      return CUBRID_MAX_STMT_TYPE;
    }
}

static int
prepare_column_list_info_set (T_SRV_HANDLE * srv_handle, T_NET_BUF * net_buf)
{
  int err_code = CAS_NO_ERROR;

  err_code = send_prepare_info (srv_handle, net_buf);
  if (err_code != CAS_NO_ERROR)
    {
      return err_code;
    }

  if (srv_handle->stmt_type == CUBRID_STMT_SELECT &&
      srv_handle->send_metadata_before_execute)
    {
      err_code = set_metadata_info (srv_handle);
      if (err_code != CAS_NO_ERROR)
	{
	  return err_code;
	}
    }

  return err_code;
}


static int
send_prepare_info (T_SRV_HANDLE * srv_handle, T_NET_BUF * net_buf)
{
  int err_code = CAS_NO_ERROR;
  MYSQL_STMT *stmt = NULL;
  MYSQL_RES *result = NULL;
  int num_cols = 0;
  DB_VALUE *columns = NULL;
  MYSQL_BIND *defines = NULL;
  MYSQL_FIELD *col;
  int i, type, size, scale, precision;

  net_buf_cp_byte (net_buf, 0);	/* updatable_flag */

  if (srv_handle->stmt_type != CUBRID_STMT_SELECT)
    {
      net_buf_cp_int (net_buf, 0, NULL);
      return CAS_NO_ERROR;
    }

  stmt = (MYSQL_STMT *) srv_handle->session;

  err_code = cas_mysql_stmt_result_metadata (stmt, &result);
  if (err_code < 0)
    {
      err_code = cas_mysql_get_stmt_errno (stmt);
      goto error;
    }

  num_cols = cas_mysql_num_fields (result);
  net_buf_cp_int (net_buf, num_cols, NULL);

  if (num_cols > 0)
    {
      for (i = 0; i < num_cols; i++)
	{
	  col = cas_mysql_fetch_field (result);
	  char set_type, cas_type;
	  cas_type = ux_db_type_to_cas_type (col->type);
	  size = col->length;
	  type = col->type;
	  scale = col->decimals;
	  precision = col->length;

	  prepare_column_info_set (net_buf, (char) cas_type,
				   scale, precision,
				   col->name, (const char *) col->def, 0,
				   0, 0, 0, 0, 0, 0, col->name,
				   col->org_table, IS_NOT_NULL (col->flags));
	}
    }

  if (result != NULL)
    {
      cas_mysql_free_result (result);
    }

  return err_code;

error:
  if (result != NULL)
    {
      cas_mysql_free_result (result);
    }

  return err_code;
}

static int
set_metadata_info (T_SRV_HANDLE * srv_handle)
{
  int err_code = CAS_NO_ERROR;
  MYSQL_STMT *stmt = NULL;
  MYSQL_RES *result = NULL;
  int asize;
  T_QUERY_RESULT *q_result;
  int num_cols = 0;
  DB_VALUE *columns = NULL;
  MYSQL_BIND *defines = NULL;
  char *data;
  MYSQL_FIELD *col;
  int i, type, size, scale, precision;
  char set_type, cas_type;

  stmt = (MYSQL_STMT *) srv_handle->session;

  err_code = cas_mysql_stmt_result_metadata (stmt, &result);
  if (err_code != CAS_NO_ERROR)
    {
      err_code = cas_mysql_get_stmt_errno (stmt);
      goto error;
    }

  asize = sizeof (T_QUERY_RESULT);
  q_result = (T_QUERY_RESULT *) MALLOC (asize);
  if (q_result == NULL)
    {
      err_code = ERROR_INFO_SET (CAS_ER_NO_MORE_MEMORY, CAS_ERROR_INDICATOR);
      goto error;
    }
  memset ((char *) q_result, 0x00, asize);

  num_cols = cas_mysql_num_fields (result);

  srv_handle->q_result = q_result;
  srv_handle->q_result->columns = NULL;
  srv_handle->q_result->column_count = num_cols;

  if (num_cols > 0)
    {
      asize = sizeof (DB_VALUE) * (num_cols);
      columns = (DB_VALUE *) MALLOC (asize);
      if (columns == NULL)
	{
	  err_code = ERROR_INFO_SET (CAS_ER_NO_MORE_MEMORY,
				     CAS_ERROR_INDICATOR);
	  goto error;
	}
      memset ((char *) columns, 0x00, asize);
      srv_handle->q_result->columns = columns;

      asize = sizeof (MYSQL_BIND) * (num_cols);
      defines = (MYSQL_BIND *) MALLOC (asize);
      if (defines == NULL)
	{
	  err_code = ERROR_INFO_SET (CAS_ER_NO_MORE_MEMORY,
				     CAS_ERROR_INDICATOR);
	  goto error;
	}
      memset ((char *) defines, 0x00, asize);
      srv_handle->q_result->defines = defines;

      for (i = 0; i < num_cols; i++)
	{
	  col = cas_mysql_fetch_field (result);
	  cas_type = ux_db_type_to_cas_type (col->type);
	  size = col->length;
	  type = col->type;
	  scale = col->decimals;
	  precision = col->length;
	  columns[i].need_clear = false;

	  switch (type)
	    {			/* set type specific value */
	      /*
	         case MYSQL_TYPE_DATETIME:
	         case MYSQL_TYPE_TIMESTAMP:
	         case MYSQL_TYPE_DATE:
	         case MYSQL_TYPE_TIME:
	         size += 24;
	         break;
	       */
	    case MYSQL_TYPE_STRING:
	    case MYSQL_TYPE_VAR_STRING:
	      precision /= multi_byte_character_max_length;
	      size += 1;
	      break;
	    case MYSQL_TYPE_BLOB:
	    case MYSQL_TYPE_MEDIUM_BLOB:
	    case MYSQL_TYPE_LONG_BLOB:
	      if (col->length > MAX_CAS_BLOB_SIZE)	/* length is unsigned long type */
		{
		  size = MAX_CAS_BLOB_SIZE;
		}
	      break;
	    case MYSQL_TYPE_DECIMAL:
	    case MYSQL_TYPE_NEWDECIMAL:
	      if (scale > 0)
		precision -= 1;
	      precision -= 1;
	      type = MYSQL_TYPE_STRING;
	      break;
	    }

	  /* set defines value */
	  defines[i].buffer_type = type;
	  switch (type)		/* set buffer pointer */
	    {
	    case MYSQL_TYPE_NULL:
	      break;
	    case MYSQL_TYPE_TINY_BLOB:
	    case MYSQL_TYPE_BLOB:
	    case MYSQL_TYPE_MEDIUM_BLOB:
	    case MYSQL_TYPE_LONG_BLOB:
	    case MYSQL_TYPE_STRING:
	    case MYSQL_TYPE_VARCHAR:
	    case MYSQL_TYPE_VAR_STRING:
	    case MYSQL_TYPE_BIT:
	      if (type == MYSQL_TYPE_VARCHAR || type == MYSQL_TYPE_VAR_STRING
		  || type == MYSQL_TYPE_STRING)
		{
		  size *= 4;
		}

	      data = (char *) MALLOC (size);
	      if (data == NULL)
		{
		  err_code = ERROR_INFO_SET (CAS_ER_NO_MORE_MEMORY,
					     CAS_ERROR_INDICATOR);
		  goto error;
		}
	      columns[i].data.p = data;
	      columns[i].need_clear = true;

	      defines[i].buffer = (void *) columns[i].data.p;
	      defines[i].buffer_length = size;
	      break;
	    case MYSQL_TYPE_SHORT:
	    case MYSQL_TYPE_TINY:
	      defines[i].buffer = (void *) &(columns[i].data.sh);
	      defines[i].buffer_length = sizeof (short);
	      break;
	    case MYSQL_TYPE_INT24:
	    case MYSQL_TYPE_LONG:
	      defines[i].buffer = (void *) &(columns[i].data.i);
	      defines[i].buffer_length = sizeof (int);
	      break;
	    case MYSQL_TYPE_LONGLONG:
	      defines[i].buffer = (void *) &(columns[i].data.bi);
	      defines[i].buffer_length = sizeof (int64_t);
	      break;
	    case MYSQL_TYPE_FLOAT:
	      defines[i].buffer = (void *) &(columns[i].data.f);
	      defines[i].buffer_length = sizeof (float);
	      break;
	    case MYSQL_TYPE_DOUBLE:
	      defines[i].buffer = (void *) &(columns[i].data.d);
	      defines[i].buffer_length = sizeof (double);
	      break;
	    case MYSQL_TYPE_DATE:
	    case MYSQL_TYPE_TIME:
	    case MYSQL_TYPE_TIMESTAMP:
	    case MYSQL_TYPE_DATETIME:
	      defines[i].buffer = (void *) &(columns[i].data.t);
	      defines[i].buffer_length = sizeof (MYSQL_TIME);
	      break;
	    default:
	      err_code = ERROR_INFO_SET (CAS_ER_UNKNOWN_U_TYPE,
					 CAS_ERROR_INDICATOR);
	      goto error;
	    }
	  defines[i].is_null = &(columns[i].is_null);
	  defines[i].length = &(columns[i].size);
	  defines[i].is_unsigned = col->flags & UNSIGNED_FLAG;
	  defines[i].error = NULL;
	}
    }

  if (result != NULL)
    {
      cas_mysql_free_result (result);
    }

  return err_code;

error:
  if (result != NULL)
    {
      cas_mysql_free_result (result);
      hm_qresult_end (srv_handle, TRUE);
    }

  return err_code;
}

static int
execute_info_set (T_SRV_HANDLE * srv_handle, T_NET_BUF * net_buf,
		  T_BROKER_VERSION client_version, char exec_flag)
{
  int i, tuple_count, error;
  char stmt_type;
  int status = 1;
  int num_q_result;
  T_OBJECT ins_oid;

  /* not support MULTIRESULT */
  num_q_result = 1;
  net_buf_cp_int (net_buf, num_q_result, NULL);
  net_buf_cp_byte (net_buf, srv_handle->stmt_type);

  if (srv_handle->stmt_type == CUBRID_STMT_SELECT)
    {
      tuple_count =
	cas_mysql_stmt_num_rows ((MYSQL_STMT *) srv_handle->session);
    }
  else
    {
      tuple_count =
	cas_mysql_stmt_affected_rows ((MYSQL_STMT *) srv_handle->session);
    }
  net_buf_cp_int (net_buf, tuple_count, NULL);

  memset (&ins_oid, 0, sizeof (T_OBJECT));
  net_buf_cp_object (net_buf, &ins_oid);
  net_buf_cp_int (net_buf, 0, NULL);	/* cache time sec */
  net_buf_cp_int (net_buf, 0, NULL);	/* cache time usec */

  return tuple_count;
}

int
ux_auto_commit (T_NET_BUF * net_buf, T_REQ_INFO * req_info)
{
  int err_code;
  int elapsed_sec = 0, elapsed_msec = 0;

  if (req_info->need_auto_commit == TRAN_AUTOCOMMIT)
    {
      cas_log_write (0, false, "auto_commit");
      err_code = ux_end_tran (CCI_TRAN_COMMIT, true);
      cas_log_write (0, false, "auto_commit %d", err_code);
    }
  else if (req_info->need_auto_commit == TRAN_AUTOROLLBACK)
    {
      cas_log_write (0, false, "auto_rollback");
      err_code = ux_end_tran (CCI_TRAN_ROLLBACK, true);
      cas_log_write (0, false, "auto_rollback %d", err_code);
    }
  else
    {
      err_code = ERROR_INFO_SET (CAS_ER_INTERNAL, CAS_ERROR_INDICATOR);
    }

  if (err_code < 0)
    {
      NET_BUF_ERR_SET (net_buf);
      req_info->need_rollback = TRUE;
      errors_in_transaction++;
    }
  else
    {
      req_info->need_rollback = FALSE;
    }

  tran_timeout = ut_check_timeout (&tran_start_time, NULL,
				   shm_appl->long_transaction_time,
				   &elapsed_sec, &elapsed_msec);
  if (tran_timeout >= 0)
    {
      as_info->num_long_transactions %= MAX_DIAG_DATA_VALUE;
      as_info->num_long_transactions++;
    }
  if (err_code < 0 || errors_in_transaction > 0)
    {
      cas_log_end (SQL_LOG_MODE_ERROR, elapsed_sec, elapsed_msec);
      errors_in_transaction = 0;
    }
  else
    {
      if (tran_timeout >= 0 || query_timeout >= 0)
	{
	  cas_log_end (SQL_LOG_MODE_TIMEOUT, elapsed_sec, elapsed_msec);
	}
      else
	{
	  cas_log_end (SQL_LOG_MODE_NONE, elapsed_sec, elapsed_msec);
	}
    }
  gettimeofday (&tran_start_time, NULL);
  gettimeofday (&query_start_time, NULL);
  tran_timeout = 0;
  query_timeout = 0;

  if (as_info->cas_log_reset)
    {
      cas_log_reset (broker_name);
    }
  if (!ux_is_database_connected () || restart_is_needed ())
    {
      return -1;
    }

  if (shm_appl->sql_log2 != as_info->cur_sql_log2)
    {
      sql_log2_end (false);
      as_info->cur_sql_log2 = shm_appl->sql_log2;
      sql_log2_init (broker_name, shm_as_index, as_info->cur_sql_log2, true);
    }
  return 0;
}

static bool
has_stmt_result_set (int stmt_type)
{
  if (stmt_type == CUBRID_STMT_SELECT)
    {
      return true;
    }

  return false;
}

static bool
check_auto_commit_after_fetch_done (T_SRV_HANDLE * srv_handle)
{
  if (has_stmt_result_set (srv_handle->stmt_type) == true
      && srv_handle->auto_commit_mode == TRUE
      && srv_handle->forward_only_cursor == TRUE)
    {
      return true;
    }
  return false;
}

static int
cas_mysql_get_errno (void)
{
  int e;
  const char *emsg;

  if (_db_conn == NULL)
    return ERROR_INFO_SET (CAS_ER_SRV_HANDLE, CAS_ERROR_INDICATOR);

  e = mysql_errno (_db_conn);
  emsg = mysql_error (_db_conn);
  cas_error_log_write (e, emsg);

  return ERROR_INFO_SET_WITH_MSG (e, DBMS_ERROR_INDICATOR, emsg);
}

static const char *
cas_mysql_get_errmsg (void)
{
  if (_db_conn == NULL)
    return NULL;

  return mysql_error (_db_conn);
}

static int
cas_mysql_get_stmt_errno (MYSQL_STMT * stmt)
{
  int e;
  const char *emsg;

  if (stmt == NULL)
    return ERROR_INFO_SET (CAS_ER_SRV_HANDLE, CAS_ERROR_INDICATOR);

  e = mysql_stmt_errno (stmt);
  emsg = mysql_stmt_error (stmt);
  cas_error_log_write (e, emsg);

  return ERROR_INFO_SET_WITH_MSG (e, DBMS_ERROR_INDICATOR, emsg);
}

static int
cas_mysql_find_db (const char *alias, char *dbname, char *host, int *port)
{
  DB_INFO *db_info_all_p, *db_info_p;
  int ret;
  char delim[] = ":";
  char *str, tmpdbinfo[BROKER_PATH_MAX], *save;

  db_info_all_p = db_info_p = NULL;

  if (alias == NULL)
    {
      goto cas_mysql_find_db_error;
    }

  if (cas_shard_flag == ON)
    {
      memset (tmpdbinfo, 0x00, BROKER_PATH_MAX);
      memcpy (tmpdbinfo, shm_appl->shard_conn_info[shm_shard_id].db_host,
	      BROKER_PATH_MAX);

      strcpy (dbname, alias);

      str = strtok_r (tmpdbinfo, delim, &save);	/* SET HOST ADDRESS */
      if (str == NULL)
	{
	  goto cas_mysql_find_db_error;
	}
      strncpy (host, str, MAX_HOSTNAME_LENGTH);

      str = strtok_r (NULL, delim, &save);	/* SET PORT */
      if (str == NULL)
	{
	  *port = DEFAULT_MYSQL_PORT;
	}
      else
	{
	  *port = atoi (str);
	}
    }
  else
    {
      if ((ret = cfg_read_dbinfo (&db_info_all_p)) < 0)
	{
	  return ret;
	}
      if (db_info_all_p == NULL)
	{
	  goto cas_mysql_find_db_error;
	}

      db_info_p = cfg_find_db_list (db_info_all_p, alias);
      if (db_info_p == NULL)
	{
	  goto cas_mysql_find_db_error;
	}
      else
	{
	  if (db_info_p->dbinfo == NULL)
	    {
	      goto cas_mysql_find_db_error;
	    }
	  else
	    {
	      memset (tmpdbinfo, 0x00, BROKER_PATH_MAX);
	      strncpy (tmpdbinfo, db_info_p->dbinfo, BROKER_PATH_MAX - 1);

	      str = strtok_r (tmpdbinfo, delim, &save);	/* SET DBNAME */
	      if (str == NULL)
		{
		  goto cas_mysql_find_db_error;
		}
	      strncpy (dbname, str, MAX_DBNAME_LENGTH);

	      str = strtok_r (NULL, delim, &save);	/* SET HOST ADDRESS */
	      if (str == NULL)
		{
		  goto cas_mysql_find_db_error;
		}
	      strncpy (host, str, MAX_HOSTNAME_LENGTH);

	      str = strtok_r (NULL, delim, &save);	/* SET PORT */
	      if (str == NULL)
		{
		  *port = DEFAULT_MYSQL_PORT;
		}
	      else
		{
		  *port = atoi (str);
		}
	      cfg_free_dbinfo_all (db_info_all_p);
	    }
	}
    }
  return 0;

cas_mysql_find_db_error:
  if (db_info_all_p)
    {
      cfg_free_dbinfo_all (db_info_all_p);
    }
  return ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
}

static const char *
cas_mysql_get_connected_host_info (void)
{
  return mysql_connected_info;
}

static void
cas_mysql_set_host_connected (const char *dbname, const char *host, int port)
{
  memset (mysql_connected_info, 0x00, DBINFO_MAX_LENGTH);
  sprintf (mysql_connected_info, "%s:%s:%d", dbname, host, port);
}

static int
cas_mysql_connect_db (char *alias, char *user, char *passwd)
{
  int ret;
  char dbname[MAX_DBNAME_LENGTH] = "";
  char host[MAX_HOSTNAME_LENGTH] = "";
  int port;

  _db_conn = mysql_init (NULL);
  if (_db_conn == NULL)
    {
      return cas_mysql_get_errno ();
    }

  ret = cas_mysql_find_db (alias, dbname, host, &port);
  if (ret < 0)
    {
      return ret;
    }

  mysql_options (_db_conn, MYSQL_SET_CHARSET_NAME, "utf8");
  mysql_options (_db_conn, MYSQL_INIT_COMMAND,
		 "SET SESSION sql_mode=STRICT_TRANS_TABLES");
  if (!mysql_real_connect
      (_db_conn, host, user, passwd, dbname, port, NULL,
       CLIENT_MULTI_STATEMENTS | CLIENT_FOUND_ROWS))
    return cas_mysql_get_errno ();

  mysql_set_server_option (_db_conn, MYSQL_OPTION_MULTI_STATEMENTS_ON);
  set_db_connect_status (DB_CONNECTION_STATUS_CONNECTED);
  cas_mysql_set_host_connected (dbname, host, port);

  cas_mysql_autocommit (false);

  return 0;
}

static void
cas_mysql_disconnect_db (void)
{
  set_db_connect_status (DB_CONNECTION_STATUS_NOT_CONNECTED);

  if (_db_conn == NULL)
    return;

  mysql_close (_db_conn);
  _db_conn = NULL;
}

static int
cas_mysql_prepare (T_SRV_HANDLE ** new_handle, char *sql_stmt, int flag,
		   char auto_commit_mode, unsigned int query_seq_num)
{
  int err_code = CAS_NO_ERROR;
  int srv_h_id;
  MYSQL_STMT *stmt = NULL;

  if (sql_stmt == NULL)
    {
      err_code = ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
      goto prepare_error_internal;
    }

  srv_h_id = hm_new_srv_handle (new_handle, query_seq_num);
  if (srv_h_id < 0)
    {
      err_code = srv_h_id;
      goto prepare_error_internal;
    }

  (*new_handle)->schema_type = -1;
  (*new_handle)->prepare_flag = flag;
  (*new_handle)->auto_commit_mode = auto_commit_mode;

  ALLOC_COPY ((*new_handle)->sql_stmt, sql_stmt);
  if ((*new_handle)->sql_stmt == NULL)
    {
      err_code = ERROR_INFO_SET (CAS_ER_NO_MORE_MEMORY, CAS_ERROR_INDICATOR);
      goto prepare_error;
    }

  if (_db_conn == NULL)
    {
      goto prepare_error;
    }

  stmt = mysql_stmt_init (_db_conn);
  if (stmt == NULL)
    {
      goto prepare_error;
    }

  err_code = mysql_stmt_prepare (stmt, sql_stmt, strlen (sql_stmt));
  if (err_code != 0)
    {
      goto prepare_error;
    }

  (*new_handle)->num_markers = cas_mysql_param_count (stmt);
  (*new_handle)->stmt_type = get_stmt_type (sql_stmt);

  (*new_handle)->session = (void *) stmt;
  (*new_handle)->is_prepared = TRUE;

  return srv_h_id;

prepare_error:
  err_code = cas_mysql_get_stmt_errno (stmt);

prepare_error_internal:
  if (stmt)
    {
      mysql_stmt_close (stmt);
    }

  if (*new_handle)
    {
      hm_srv_handle_free (srv_h_id);
    }

  return err_code;
}

/* Errors : None */
static int
cas_mysql_param_count (MYSQL_STMT * stmt)
{
  return mysql_stmt_param_count (stmt);
}

static int
cas_mysql_stmt_result_metadata (MYSQL_STMT * stmt, MYSQL_RES ** result)
{
  MYSQL_RES *tresult;

  if (stmt == NULL)
    {
      *result = NULL;
      return cas_mysql_get_stmt_errno (stmt);
    }

  tresult = mysql_stmt_result_metadata (stmt);
  if (tresult == NULL)
    {
      *result = NULL;
      return cas_mysql_get_stmt_errno (stmt);
    }

  *result = tresult;

  return 0;
}

static int
cas_mysql_stmt_bind_result (MYSQL_STMT * stmt, MYSQL_BIND ** bind)
{
  int ret;

  ret = mysql_stmt_bind_result (stmt, *bind);
  if (ret != 0)
    {
      return cas_mysql_get_stmt_errno (stmt);
    }

  return 0;
}

static int
cas_mysql_stmt_store_result (MYSQL_STMT * stmt)
{
  int ret;

  ret = mysql_stmt_store_result (stmt);
  if (ret != 0)
    {
      return cas_mysql_get_stmt_errno (stmt);
    }

  return 0;
}

/* Errors : None */
static MYSQL_FIELD *
cas_mysql_fetch_field (MYSQL_RES * result)
{
  return mysql_fetch_field (result);
}

static void
cas_mysql_free_result (MYSQL_RES * result)
{
  mysql_free_result (result);
  result = NULL;
}

void
cas_mysql_stmt_free_result (MYSQL_STMT * stmt)
{
  (void) mysql_stmt_free_result (stmt);
  return;
}

void
cas_mysql_stmt_close (MYSQL_STMT * stmt)
{
  mysql_stmt_close (stmt);
  stmt = NULL;
}

static int
cas_mysql_stmt_bind_param (MYSQL_STMT * stmt, MYSQL_BIND * bindIn)
{
  int ret;

  if (stmt == NULL)
    return ERROR_INFO_SET (CAS_ER_INTERNAL, CAS_ERROR_INDICATOR);

  ret = mysql_stmt_bind_param (stmt, bindIn);
  if (ret != 0)
    {
      return cas_mysql_get_stmt_errno (stmt);
    }

  return 0;
}

static void
cas_mysql_bind_param_clear (MYSQL_BIND * param)
{
  FREE_MEM (param->buffer);
}

static int
cas_mysql_stmt_execute (MYSQL_STMT * stmt)
{
  int ret;

  ret = mysql_stmt_execute (stmt);
  if (ret != 0)
    {
      return cas_mysql_get_stmt_errno (stmt);
    }

  return 0;
}

/* Errors : None */
int
cas_mysql_stmt_num_rows (MYSQL_STMT * stmt)
{
  return mysql_stmt_num_rows (stmt);
}

/* Errors : None */
int
cas_mysql_stmt_affected_rows (MYSQL_STMT * stmt)
{
  return mysql_stmt_affected_rows (stmt);
}

/* Errors : None */
int
cas_mysql_stmt_num_fields (MYSQL_STMT * stmt)
{
  return mysql_stmt_field_count (stmt);
}

static int
cas_mysql_stmt_fetch (MYSQL_STMT * stmt)
{
  int ret;

  ret = mysql_stmt_fetch (stmt);
  if ((ret == MYSQL_NO_DATA) || (ret == MYSQL_DATA_TRUNCATED) || (ret == 0))
    {
      return ret;
    }
  return cas_mysql_get_stmt_errno (stmt);
}

static int
cas_mysql_end_tran (int tran_type)
{
  int ret;

  if (!_db_conn)
    {
      return cas_mysql_get_errno ();
    }

  switch (tran_type)
    {
    case CCI_TRAN_COMMIT:
      ret = mysql_commit (_db_conn);
      break;
    case CCI_TRAN_ROLLBACK:
      ret = mysql_rollback (_db_conn);
      break;
    default:
      return ERROR_INFO_SET (CAS_ER_INTERNAL, CAS_ERROR_INDICATOR);
    }

  if (ret != 0)
    {
      return cas_mysql_get_errno ();
    }
  return ret;
}

/* Errors : None */
static int
cas_mysql_num_fields (MYSQL_RES * metaResult)
{
  return mysql_num_fields (metaResult);
}

int
get_db_connect_status (void)
{
  if (!is_server_alive ())
    {
      set_db_connect_status (DB_CONNECTION_STATUS_NOT_CONNECTED);
    }

  return mysql_connect_status;
}

void
set_db_connect_status (int status)
{
  mysql_connect_status = status;
}

static bool
cas_mysql_autocommit (bool auto_commit_mode)
{
  return mysql_autocommit (_db_conn, auto_commit_mode);
}

bool
is_server_alive (void)
{
  int ret;

  if (_db_conn == NULL)
    {
      return false;
    }

  if (mysql_ping (_db_conn))
    {
      return false;
    }
  return true;
}

int
get_tuple_count (T_SRV_HANDLE * srv_handle)
{
  return srv_handle->tuple_count;
}

static void
db_make_null (DB_VALUE * value)
{
  memset ((char *) value, 0x00, sizeof (DB_VALUE));
  value->is_null = true;
}

static int
db_value_clear (DB_VALUE * value)
{
  if (value == NULL)
    {
      return ERROR_INFO_SET (CAS_ER_DB_VALUE, CAS_ERROR_INDICATOR);
    }
  if (value->need_clear)
    {
      FREE_MEM (value->data.p);
    }
  memset ((char *) value, 0x00, sizeof (DB_VALUE));
  return 0;
}

T_PREPARE_CALL_INFO *
make_prepare_call_info (int num_args)
{
  T_PREPARE_CALL_INFO *call_info;
  MYSQL_BIND *bind = NULL;
  DB_VALUE **arg_val = NULL;
  char *param_mode = NULL;
  int i, err_code;

  call_info = (T_PREPARE_CALL_INFO *) MALLOC (sizeof (T_PREPARE_CALL_INFO));
  if (call_info == NULL)
    {
      ERROR_INFO_SET (CAS_ER_NO_MORE_MEMORY, CAS_ERROR_INDICATOR);
      return NULL;
    }
  memset (call_info, 0, sizeof (T_PREPARE_CALL_INFO));

  if (num_args > 0)
    {
      bind = (MYSQL_BIND *) MALLOC (sizeof (MYSQL_BIND) * (num_args));
      if (bind == NULL)
	{
	  err_code = CAS_ER_NO_MORE_MEMORY;
	  goto exit_on_error;
	}
      memset (bind, 0, sizeof (MYSQL_BIND) * (num_args));

      param_mode = (char *) MALLOC (sizeof (char) * num_args);
      if (param_mode == NULL)
	{
	  err_code = CAS_ER_NO_MORE_MEMORY;
	  goto exit_on_error;
	}

      arg_val = (DB_VALUE **) MALLOC (sizeof (DB_VALUE *) * (num_args));
      if (arg_val == NULL)
	{
	  err_code = CAS_ER_NO_MORE_MEMORY;
	  goto exit_on_error;
	}
      memset (arg_val, 0, sizeof (DB_VALUE *) * (num_args));

      for (i = 0; i < num_args; i++)
	{
	  arg_val[i] = (DB_VALUE *) MALLOC (sizeof (DB_VALUE));
	  if (arg_val[i] == NULL)
	    {
	      err_code = CAS_ER_NO_MORE_MEMORY;
	      goto exit_on_error;
	    }
	  db_make_null (arg_val[i]);

	  bind[i].buffer = arg_val[i]->buf;

	  param_mode[i] = CCI_PARAM_MODE_UNKNOWN;
	}
    }

  call_info->dbval_ret = NULL;
  call_info->dbval_args = arg_val;
  call_info->num_args = num_args;
  call_info->bind = bind;
  call_info->param_mode = param_mode;

  return call_info;

exit_on_error:
  FREE_MEM (call_info);
  FREE_MEM (bind);
  FREE_MEM (param_mode);
  if (arg_val != NULL)
    {
      for (i = 0; i < num_args; i++)
	{
	  FREE_MEM (arg_val[i]);
	}
      FREE_MEM (arg_val);
    }
  ERROR_INFO_SET (err_code, CAS_ERROR_INDICATOR);
  return NULL;
}

void
ux_prepare_call_info_free (T_PREPARE_CALL_INFO * call_info)
{
  DB_VALUE **args;
  MYSQL_BIND *bind;

  if (call_info)
    {
      int i;

      FREE_MEM (call_info->dbval_ret);

      args = (DB_VALUE **) call_info->dbval_args;
      for (i = 0; i < call_info->num_args; i++)
	{
	  db_value_clear (args[i]);
	  FREE_MEM (args[i]);
	}

      FREE_MEM (call_info->dbval_args);
      FREE_MEM (call_info->bind);
      FREE_MEM (call_info->param_mode);

      FREE_MEM (call_info);
    }
}

static void
update_query_execution_count (T_APPL_SERVER_INFO * as_info_p, char stmt_type)
{
  assert (as_info_p != NULL);

  as_info_p->num_queries_processed %= MAX_DIAG_DATA_VALUE;
  as_info_p->num_queries_processed++;

  switch (stmt_type)
    {
    case CUBRID_STMT_SELECT:
      as_info_p->num_select_queries %= MAX_DIAG_DATA_VALUE;
      as_info_p->num_select_queries++;
      break;
    case CUBRID_STMT_INSERT:
      as_info_p->num_insert_queries %= MAX_DIAG_DATA_VALUE;
      as_info_p->num_insert_queries++;
      break;
    case CUBRID_STMT_UPDATE:
      as_info_p->num_update_queries %= MAX_DIAG_DATA_VALUE;
      as_info_p->num_update_queries++;
      break;
    case CUBRID_STMT_DELETE:
      as_info_p->num_delete_queries %= MAX_DIAG_DATA_VALUE;
      as_info_p->num_delete_queries++;
      break;
    default:
      break;
    }
}

static void
cas_mysql_set_mysql_wait_timeout (void)
{
  char *p = NULL;
  int ret_val = 0;
  int wait_timeout = MYSQL_DEFAULT_WAIT_TIMEOUT;
  int num_fields = 0;
  MYSQL_RES *res = NULL;
  MYSQL_ROW row;

  ret_val =
    mysql_query (_db_conn, "show session variables like 'wait_timeout'");
  if (ret_val != 0)
    {
      goto release_and_return;
    }

  res = mysql_store_result (_db_conn);
  if (res == NULL)
    {
      goto release_and_return;
    }

  row = mysql_fetch_row (res);
  if (row == NULL)
    {
      goto release_and_return;
    }

  num_fields = mysql_num_fields (res);
  if (num_fields != 2)
    {
      goto release_and_return;
    }

  wait_timeout = (int) strtol (row[1], &p, 10);
  if (p != NULL)
    {
      goto release_and_return;
    }

  wait_timeout = (int) (wait_timeout * 2 / 3);

  /**
   * CAS will be stopped if there is no request 60 seconds.
   * so we cannot set mysql wait timeout under 60 seconds.
   */
  if (wait_timeout < 60)
    {
      wait_timeout = 60;
    }

release_and_return:
  if (res != NULL)
    {
      mysql_free_result (res);
    }

  mysql_wait_timeout = wait_timeout;
}

int
cas_mysql_get_mysql_wait_timeout (void)
{
  return mysql_wait_timeout;
}

int
cas_mysql_execute_dummy (void)
{
  int ret_val = 0;

  ret_val = mysql_query (_db_conn, "select 1 from dual");
  if (ret_val != 0)
    {
      return -1;
    }
  else
    {
      return 0;
    }
}
