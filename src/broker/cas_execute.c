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
 * cas_execute.c -
 */

#ident "$Id$"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

#include "cas_db_inc.h"
#include "glo_class.h"

#include "cas.h"
#include "cas_common.h"
#include "cas_execute.h"
#include "cas_network.h"
#include "cas_util.h"
#include "cas_schema_info.h"
#include "cas_log.h"
#include "cas_str_like.h"

#include "broker_filename.h"
#include "cas_sql_log2.h"

#include "release_string.h"
#include "perf_monitor.h"

#define OUT_STR		1
#define IN_STR		0

#define QUERY_BUFFER_MAX                4096

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

typedef struct t_class_table T_CLASS_TABLE;
struct t_class_table
{
  char *class_name;
  short class_type;
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

static int netval_to_dbval (void *type, void *value, DB_VALUE * db_val,
			    T_NET_BUF * net_buf, char desired_type);
static int cur_tuple (T_QUERY_RESULT * q_result, int max_col_size,
		      char sensitive_flag, DB_OBJECT * db_obj,
		      T_NET_BUF * net_buf);
static int dbval_to_net_buf (DB_VALUE * val, T_NET_BUF * net_buf, char flag,
			     int max_col_size, char column_type_flag);
static void uobj_to_cas_obj (DB_OBJECT * obj, T_OBJECT * cas_obj);
static int get_attr_name (DB_OBJECT * obj, char ***ret_attr_name);
static int get_attr_name_from_argv (int argc, void **argv,
				    char ***ret_attr_name);
static int oid_attr_info_set (T_NET_BUF * net_buf, DB_OBJECT * obj,
			      int num_attr, char **attr_name);
static int oid_data_set (T_NET_BUF * net_buf, DB_OBJECT * obj, int attr_num,
			 char **attr_name);
static int prepare_column_list_info_set (DB_SESSION * session,
					 char prepare_flag,
					 T_QUERY_RESULT * q_result,
					 T_NET_BUF * net_buf,
					 T_BROKER_VERSION client_version);
static void prepare_column_info_set (T_NET_BUF * net_buf, char ut,
				     short scale, int prec,
				     const char *col_name,
				     const char *attr_name,
				     const char *class_name, char nullable,
				     T_BROKER_VERSION client_version);

/*
  fetch_xxx prototype:
  fetch_xxx(T_SRV_HANDLE *, int cursor_pos, int fetch_count, char fetch_flag, int result_set_idx, T_NET_BUF *);
*/
static int fetch_result (T_SRV_HANDLE *, int, int, char, int, T_NET_BUF *,
			 T_REQ_INFO *);
static int fetch_class (T_SRV_HANDLE *, int, int, char, int, T_NET_BUF *,
			T_REQ_INFO *);
static int fetch_attribute (T_SRV_HANDLE *, int, int, char, int, T_NET_BUF *,
			    T_REQ_INFO *);
static int fetch_method (T_SRV_HANDLE *, int, int, char, int, T_NET_BUF *,
			 T_REQ_INFO *);
static int fetch_methfile (T_SRV_HANDLE *, int, int, char, int, T_NET_BUF *,
			   T_REQ_INFO *);
static int fetch_constraint (T_SRV_HANDLE *, int, int, char, int, T_NET_BUF *,
			     T_REQ_INFO *);
static int fetch_trigger (T_SRV_HANDLE *, int, int, char, int, T_NET_BUF *,
			  T_REQ_INFO *);
static int fetch_privilege (T_SRV_HANDLE *, int, int, char, int, T_NET_BUF *,
			    T_REQ_INFO *);
static int fetch_primary_key (T_SRV_HANDLE *, int, int, char, int,
			      T_NET_BUF *, T_REQ_INFO *);

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
static void add_res_data_object (T_NET_BUF * net_buf, T_OBJECT * obj,
				 char type);
static void trigger_event_str (DB_TRIGGER_EVENT trig_event, char *buf);
static void trigger_status_str (DB_TRIGGER_STATUS trig_status, char *buf);
static void trigger_time_str (DB_TRIGGER_TIME trig_time, char *buf);

static int get_num_markers (char *stmt);
static char get_stmt_type (char *stmt);
static int execute_info_set (T_SRV_HANDLE * srv_handle, T_NET_BUF * net_buf,
			     T_BROKER_VERSION client_version, char exec_flag);
static char get_attr_type (DB_OBJECT * obj_p, char *attr_name);

static char *get_domain_str (DB_DOMAIN * domain);

static void glo_err_msg_get (int err_code, char *err_msg);
static void glo_err_msg_set (T_NET_BUF * net_buf, int err_code,
			     const char *m_name);
static DB_OBJECT *ux_str_to_obj (char *str);

static int sch_class_info (T_NET_BUF * net_buf, char *class_name,
			   char pattern_flag, char flag, T_SRV_HANDLE *,
			   T_BROKER_VERSION client_version);
static int sch_attr_info (T_NET_BUF * net_buf, char *class_name,
			  char *attr_name, char pattern_flag, char flag,
			  T_SRV_HANDLE *);
static int sch_queryspec (T_NET_BUF * net_buf, char *class_name,
			  T_SRV_HANDLE *);
static void sch_method_info (T_NET_BUF * net_buf, char *class_name, char flag,
			     void **result);
static void sch_methfile_info (T_NET_BUF * net_buf, char *class_name,
			       void **result);
static int sch_superclass (T_NET_BUF * net_buf, char *class_name, char flag,
			   T_SRV_HANDLE * srv_handle);
static void sch_constraint (T_NET_BUF * net_buf, char *class_name,
			    void **result);
static void sch_trigger (T_NET_BUF * net_buf, char *trigger_name,
			 void **result);
static int sch_class_priv (T_NET_BUF * net_buf, char *class_name,
			   char pat_flag, T_SRV_HANDLE * srv_handle);
static int sch_attr_priv (T_NET_BUF * net_buf, char *class_name,
			  char *attr_name, char pat_flag,
			  T_SRV_HANDLE * srv_handle);
static int sch_direct_super_class (T_NET_BUF * net_buf, char *class_name,
				   int pattern_flag,
				   T_SRV_HANDLE * srv_handle);

static int class_type (DB_OBJECT * class_obj);
static int class_attr_info (char *class_name, DB_ATTRIBUTE * attr,
			    char *attr_pattern, char pat_flag,
			    T_ATTR_TABLE * attr_table);
static int set_priv_table (unsigned int class_priv, char *name,
			   T_PRIV_TABLE * priv_table, int index);
static int sch_query_execute (T_SRV_HANDLE * srv_handle, char *sql_stmt,
			      T_NET_BUF * net_buf);
static int sch_primary_key (T_NET_BUF * net_buf);
static short constraint_dbtype_to_castype (int db_const_type);

#if 0
static int prepare_call_token (char *sql, char **out_call_func,
			       char **out_call_target, int *is_first_out);
#endif
static T_PREPARE_CALL_INFO *make_prepare_call_info (int num_args,
						    int is_first_out);
static void prepare_call_info_dbval_clear (T_PREPARE_CALL_INFO * call_info);
static int fetch_call (T_SRV_HANDLE * srv_handle, T_NET_BUF * net_buf);
#if 0
static void method_err_msg_set (T_NET_BUF * net_buf, int err_code,
				DB_VALUE * ret_val);
#endif
#if 0				/* FIXME - NBD Benchmark */
static int check_class_chn (T_SRV_HANDLE * srv_handle);
#else
#define check_class_chn(s) 0
#endif
static int get_client_result_cache_lifetime (DB_SESSION * session,
					     int stmt_id);

static char cas_u_type[] = { 0,	/* 0 */
  CCI_U_TYPE_INT,		/* 1 */
  CCI_U_TYPE_FLOAT,		/* 2 */
  CCI_U_TYPE_DOUBLE,		/* 3 */
  CCI_U_TYPE_STRING,		/* 4 */
  CCI_U_TYPE_OBJECT,		/* 5 */
  CCI_U_TYPE_SET,		/* 6 */
  CCI_U_TYPE_MULTISET,		/* 7 */
  CCI_U_TYPE_SEQUENCE,		/* 8 */
  0,				/* 9 */
  CCI_U_TYPE_TIME,		/* 10 */
  CCI_U_TYPE_TIMESTAMP,		/* 11 */
  CCI_U_TYPE_DATE,		/* 12 */
  CCI_U_TYPE_MONETARY,		/* 13 */
  0, 0, 0, 0,			/* 14 - 17 */
  CCI_U_TYPE_SHORT,		/* 18 */
  0, 0, 0,			/* 19 - 21 */
  CCI_U_TYPE_NUMERIC,		/* 22 */
  CCI_U_TYPE_BIT,		/* 23 */
  CCI_U_TYPE_VARBIT,		/* 24 */
  CCI_U_TYPE_CHAR,		/* 25 */
  CCI_U_TYPE_NCHAR,		/* 26 */
  CCI_U_TYPE_VARNCHAR,		/* 27 */
  CCI_U_TYPE_RESULTSET,		/* 28 */
  0, 0,				/* 29 - 30 */
  CCI_U_TYPE_BIGINT,		/* 31 */
  CCI_U_TYPE_DATETIME,		/* 32 */
};

static T_FETCH_FUNC fetch_func[] = {
  fetch_result,			/* query */
  fetch_result,			/* SCH_CLASS */
  fetch_result,			/* SCH_VCLASS */
  fetch_result,			/* SCH_QUERY_SPEC */
  fetch_attribute,		/* SCH_ATTRIBUTE */
  fetch_attribute,		/* SCH_CLASS_ATTRIBUTE */
  fetch_method,			/* SCH_METHOD */
  fetch_method,			/* SCH_CLASS_METHOD */
  fetch_methfile,		/* SCH_METHOD_FILE */
  fetch_class,			/* SCH_SUPERCLASS */
  fetch_class,			/* SCH_SUBCLASS */
  fetch_constraint,		/* SCH_CONSTRAINT */
  fetch_trigger,		/* SCH_TRIGGER */
  fetch_privilege,		/* SCH_CLASS_PRIVILEGE */
  fetch_privilege,		/* SCH_ATTR_PRIVILEGE */
  fetch_result,			/* SCH_DIRECT_SUPER_CLASS */
  fetch_primary_key		/* SCH_PRIMARY_KEY */
};

static char database_name[SRV_CON_DBNAME_SIZE] = "";
static char database_user[SRV_CON_DBUSER_SIZE] = "";
static char database_passwd[SRV_CON_DBPASSWD_SIZE] = "";
static char cas_db_sys_param[128] = "";

int
ux_check_connection (void)
{
#ifndef LIBCAS_FOR_JSP

  if (ux_is_database_connected ())
    {
      if (db_ping_server (0, NULL) < 0)
	{
	  cas_log_debug (ARG_FILE_LINE,
			 "ux_check_connection: db_ping_server() error");
	  cas_log_write_and_end (0, true, "SERVER DOWN");
	  if (as_info->cur_statement_pooling)
	    {
	      cas_log_debug (ARG_FILE_LINE,
			     "ux_check_connection: cur_statement_pooling");
	      return -1;
	    }
	  else
	    {
	      char dbname[SRV_CON_DBNAME_SIZE];
	      char dbuser[SRV_CON_DBUSER_SIZE];
	      char dbpasswd[SRV_CON_DBPASSWD_SIZE];

	      strcpy (dbname, database_name);
	      strcpy (dbuser, database_user);
	      strcpy (dbpasswd, database_passwd);

	      cas_log_debug (ARG_FILE_LINE,
			     "ux_check_connection: ux_database_shutdown()"
			     " ux_database_connect(%s, %s)", dbname, dbuser);
	      ux_database_shutdown ();
	      ux_database_connect (dbname, dbuser, dbpasswd, NULL);
	    }
	}
      return 0;
    }
  else
    {
      return -1;
    }
#endif
  return 0;
}

#ifndef LIBCAS_FOR_JSP
int
ux_database_connect (char *db_name, char *db_user, char *db_passwd,
		     char **db_err_msg)
{
  int err_code, client_type;
  char *p;
  const char *host_connected = NULL;

  if (db_name == NULL || db_name[0] == '\0')
    return -1;

  host_connected = db_get_host_connected ();

  if (db_Connect_status != 1	/* DB_CONNECTION_STATUS_CONNECTED */
      || database_name[0] == '\0'
      || strcmp (database_name, db_name) != 0
      || strcmp (as_info->database_host, host_connected) != 0)
    {
      if (database_name[0] != '\0')
	{
	  ux_database_shutdown ();
	}
      if (shm_appl->access_mode == READ_ONLY_ACCESS_MODE)
	{
	  client_type = 5;	/* DB_CLIENT_TYPE_READ_ONLY_BROKER in db.h */
	  cas_log_debug (ARG_FILE_LINE,
			 "ux_database_connect: read_only_broker");
	}
      else if (shm_appl->access_mode == SLAVE_ONLY_ACCESS_MODE)
	{
	  client_type = 6;	/* DB_CLIENT_TYPE_SLAVE_ONLY_BROKER in db.h */
	  cas_log_debug (ARG_FILE_LINE,
			 "ux_database_connect: slave_only_broker");
	}
      else
	{
	  client_type = 4;	/* DB_CLIENT_TYPE_BROKER in db.h */
	}
      db_disable_first_user ();
      err_code = db_restart_ex (program_name, db_name, db_user, db_passwd,
				client_type);
      if (err_code < 0)
	{
	  goto connect_error;
	}
      cas_log_debug (ARG_FILE_LINE,
		     "ux_database_connect: db_login(%s) db_restart(%s) at %s",
		     db_user, db_name, host_connected);
      strncpy (as_info->database_name, db_name, SRV_CON_DBNAME_SIZE - 1);
      strncpy (as_info->database_host, host_connected, MAXHOSTNAMELEN);
      as_info->last_connect_time = time (NULL);

      strncpy (database_name, db_name, sizeof (database_name) - 1);
      strncpy (database_user, db_user, sizeof (database_user) - 1);
      strncpy (database_passwd, db_passwd, sizeof (database_passwd) - 1);

      ux_get_default_setting ();
    }
  else if (shm_appl->cache_user_info == OFF
	   || strcmp (database_user, db_user) != 0
	   || strcmp (database_passwd, db_passwd) != 0)
    {
      int err_code;

      err_code = db_login (db_user, db_passwd);
      if (err_code < 0)
	{
	  ux_database_shutdown ();

	  return ux_database_connect (db_name, db_user, db_passwd,
				      db_err_msg);
	}
      strncpy (database_user, db_user, sizeof (database_user) - 1);
      strncpy (database_passwd, db_passwd, sizeof (database_passwd) - 1);
    }

  return 0;

connect_error:
  p = (char *) db_error_string (1);
  if (p == NULL)
    p = (char *) "";
  if (db_err_msg)
    {
      *db_err_msg = (char *) malloc (strlen (p) + 1);
      if (*db_err_msg)
	strcpy (*db_err_msg, p);
    }
  return err_code;
}

int
ux_database_reconnect (void)
{
  int err_code, client_type;
  const char *host_connected = NULL;

  if (!ux_is_database_connected ())
    return 1;

  if ((err_code = db_shutdown ()) < 0)
    {
      goto reconnect_error;
    }
  cas_log_debug (ARG_FILE_LINE, "ux_database_reconnect: db_shutdown()");

  if (shm_appl->access_mode == READ_ONLY_ACCESS_MODE)
    {
      client_type = 5;		/* DB_CLIENT_TYPE_READ_ONLY_BROKER in db.h */
      cas_log_debug (ARG_FILE_LINE,
		     "ux_database_reconnect: read_only_broker");
    }
  else if (shm_appl->access_mode == SLAVE_ONLY_ACCESS_MODE)
    {
      client_type = 6;		/* DB_CLIENT_TYPE_SLAVE_ONLY_BROKER in db.h */
      cas_log_debug (ARG_FILE_LINE,
		     "ux_database_reconnect: slave_only_broker");
    }
  else
    {
      client_type = 4;		/* DB_CLIENT_TYPE_BROKER in db.h */
    }
  db_disable_first_user ();
  err_code = db_restart_ex (program_name, database_name, database_user,
			    database_passwd, client_type);

  if (err_code < 0)
    {
      goto reconnect_error;
    }

  host_connected = db_get_host_connected ();
  cas_log_debug (ARG_FILE_LINE,
		 "ux_database_reconnect: db_login (%s) db_restart(%s) at %s",
		 database_user, database_name, host_connected);
  as_info->last_connect_time = time (NULL);

  ux_get_default_setting ();

  return 0;

reconnect_error:
  return err_code;
}
#endif

int
ux_is_database_connected (void)
{
  return (database_name[0] != '\0');
}

void
ux_get_default_setting ()
{
  ux_get_tran_setting (&cas_default_lock_timeout,
		       &cas_default_isolation_level);
  if (cas_default_isolation_level < TRAN_MINVALUE_ISOLATION
      || cas_default_isolation_level > TRAN_MAXVALUE_ISOLATION)
    {
      cas_default_isolation_level = 0;
    }

  strcpy (cas_db_sys_param,
	  "index_scan_in_oid_order;garbage_collection;optimization_level;");

  if (db_get_system_parameters (cas_db_sys_param,
				sizeof (cas_db_sys_param) - 1) < 0)
    {
      cas_db_sys_param[0] = '\0';
    }
}

void
ux_set_default_setting ()
{
  int cur_isolation_level;
  int cur_lock_timeout;

  ux_get_tran_setting (&cur_lock_timeout, &cur_isolation_level);

  if (cas_default_isolation_level != cur_isolation_level)
    {
      ux_set_isolation_level (cas_default_isolation_level, NULL);
    }
  if (cas_default_lock_timeout != cur_lock_timeout)
    ux_set_lock_timeout (cas_default_lock_timeout);

  if (cas_db_sys_param[0])
    {
      db_set_system_parameters (cas_db_sys_param);
    }
}

void
ux_database_shutdown ()
{
  db_shutdown ();
  cas_log_debug (ARG_FILE_LINE, "ux_database_shutdown: db_shutdown()");
#ifndef LIBCAS_FOR_JSP
  as_info->database_name[0] = '\0';
  as_info->database_host[0] = '\0';
  as_info->last_connect_time = 0;
#endif
  memset (database_name, 0, sizeof (database_name));
  memset (database_user, 0, sizeof (database_user));
  memset (database_passwd, 0, sizeof (database_passwd));
  cas_default_isolation_level = 0;
  cas_default_lock_timeout = -1;
}

int
ux_prepare (char *sql_stmt, int flag, bool auto_commit_mode,
	    T_NET_BUF * net_buf, T_REQ_INFO * req_info,
	    unsigned int query_seq_num)
{
  int stmt_id;
  T_SRV_HANDLE *srv_handle = NULL;
  DB_SESSION *session = NULL;
  int srv_h_id;
  int err_code;
  int num_markers;
  char stmt_type;
  char updatable_flag;
  T_QUERY_RESULT *q_result = NULL;
  T_BROKER_VERSION client_version = req_info->client_version;
  int is_first_out = 0;
  char *tmp;
  int result_cache_lifetime;

  srv_h_id = hm_new_srv_handle (&srv_handle, query_seq_num);
  if (srv_h_id < 0)
    {
      err_code = srv_h_id;
      goto prepare_error1;
    }
  srv_handle->schema_type = -1;
  srv_handle->auto_commit_mode = auto_commit_mode;

  ALLOC_COPY (srv_handle->sql_stmt, sql_stmt);
  if (srv_handle->sql_stmt == NULL)
    {
      err_code = CAS_ER_NO_MORE_MEMORY;
      goto prepare_error1;
    }

  if (flag & CCI_PREPARE_CALL)
    {
      T_PREPARE_CALL_INFO *prepare_call_info;

      tmp = sql_stmt;
      if (sql_stmt[0] == '?')
	{
	  is_first_out = 1;
	  while (*tmp)
	    {
	      if (*tmp == '=')
		{
		  break;
		}
	      tmp++;
	    }

	  if (!(*tmp))
	    {
	      err_code = CAS_ER_INVALID_CALL_STMT;
	      goto prepare_error1;
	    }

	  tmp++;
	}

      ut_trim (tmp);
      stmt_type = get_stmt_type (tmp);
      if (stmt_type != CUBRID_STMT_CALL)
	{
	  err_code = CAS_ER_INVALID_CALL_STMT;
	  goto prepare_error1;
	}

      session = db_open_buffer (tmp);
      if (!session)
	{
	  err_code = CAS_ER_NO_MORE_MEMORY;
	  goto prepare_error1;
	}

      stmt_id = db_compile_statement (session);
      if (stmt_id < 0)
	{
	  DB_ERR_MSG_SET (net_buf, stmt_id);
	  err_code = stmt_id;
	  goto prepare_error2;
	}

      num_markers = get_num_markers (sql_stmt);
      stmt_type = CUBRID_STMT_CALL_SP;
      srv_handle->is_prepared = TRUE;

      prepare_call_info = make_prepare_call_info (num_markers, is_first_out);
      if (prepare_call_info == NULL)
	{
	  err_code = CAS_ER_NO_MORE_MEMORY;
	  goto prepare_error1;
	}
      srv_handle->prepare_call_info = prepare_call_info;

      goto prepare_result_set;
    }

  session = db_open_buffer (sql_stmt);
  if (!session)
    {
      err_code = CAS_ER_NO_MORE_MEMORY;
      goto prepare_error1;
    }

  updatable_flag = flag & CCI_PREPARE_UPDATABLE;
  if (updatable_flag)
    {
      flag |= CCI_PREPARE_INCLUDE_OID;
    }

  if (flag & CCI_PREPARE_INCLUDE_OID)
    {
      db_include_oid (session, DB_ROW_OIDS);
    }

  stmt_id = db_compile_statement (session);
  if (stmt_id < 0)
    {
      stmt_type = get_stmt_type (sql_stmt);
      if (stmt_id == ER_PT_SEMANTIC && stmt_type != CUBRID_MAX_STMT_TYPE)
	{
	  db_close_session (session);
	  session = NULL;
	  num_markers = get_num_markers (sql_stmt);
	}
      else
	{
	  DB_ERR_MSG_SET (net_buf, stmt_id);
	  err_code = stmt_id;
	  goto prepare_error2;
	}
      srv_handle->is_prepared = FALSE;
    }
  else
    {
      num_markers = get_num_markers (sql_stmt);
      stmt_type = db_get_statement_type (session, stmt_id);
      srv_handle->is_prepared = TRUE;
    }

prepare_result_set:
  srv_handle->num_markers = num_markers;
  srv_handle->prepare_flag = flag;

  net_buf_cp_int (net_buf, srv_h_id, NULL);

  result_cache_lifetime = get_client_result_cache_lifetime (session, stmt_id);
  net_buf_cp_int (net_buf, result_cache_lifetime, NULL);

  net_buf_cp_byte (net_buf, stmt_type);
  net_buf_cp_int (net_buf, num_markers, NULL);

  q_result = (T_QUERY_RESULT *) malloc (sizeof (T_QUERY_RESULT));
  if (q_result == NULL)
    {
      err_code = CAS_ER_NO_MORE_MEMORY;
      goto prepare_error1;
    }
  hm_qresult_clear (q_result);
  q_result->stmt_type = stmt_type;
  q_result->stmt_id = stmt_id;

#if 0				/* FIXME - NBD Benchmark */
#ifndef LIBCAS_FOR_JSP
  if (as_info->cur_statement_pooling)
    {
      int num_classes = 0;
      DB_OBJECT **classes;
      int *classes_chn;
      char **class_name;

      class_name = db_get_lock_classes (session);
      if (class_name)
	{
	  for (num_classes = 0; class_name[num_classes]; num_classes++)
	    ;
	}

      if (num_classes > 0)
	{
	  int i;

	  classes =
	    (DB_OBJECT **) malloc (sizeof (DB_OBJECT *) * num_classes);
	  classes_chn = (int *) malloc (sizeof (int) * num_classes);
	  if (classes == NULL || classes_chn == NULL)
	    {
	      err_code = CAS_ER_NO_MORE_MEMORY;
	      goto prepare_error1;
	    }

	  for (i = 0; i < num_classes; i++)
	    {
	      classes[i] = db_find_class (class_name[i]);
	      classes_chn[i] = db_chn (classes[i], DB_FETCH_READ);
	    }

	  srv_handle->num_classes = num_classes;
	  srv_handle->classes = (void **) classes;
	  srv_handle->classes_chn = classes_chn;
	}
    }
#endif
#endif /* FIXME - NBD Benchmark */

  err_code = prepare_column_list_info_set (session, flag, q_result, net_buf,
					   client_version);
  if (err_code < 0)
    {
      FREE_MEM (q_result);
      goto prepare_error2;
    }

  srv_handle->session = (void *) session;

  srv_handle->q_result = q_result;
  srv_handle->num_q_result = 0;
  srv_handle->cur_result = NULL;
  srv_handle->cur_result_index = 0;

  db_get_cacheinfo (session, stmt_id, &srv_handle->use_plan_cache,
		    &srv_handle->use_query_cache);

  return srv_h_id;

prepare_error1:
  net_buf_clear (net_buf);
  net_buf_cp_int (net_buf, err_code, NULL);

prepare_error2:
#ifndef LIBCAS_FOR_JSP
  if (auto_commit_mode == true)
    {
      req_info->need_auto_commit = TRAN_AUTOROLLBACK;
    }
#endif
  errors_in_transaction++;

  if (srv_handle)
    {
      hm_srv_handle_free (srv_h_id);
    }

  if (session)
    {
      db_close_session (session);
    }

  return err_code;
}

int
ux_end_tran (int tran_type, bool reset_con_status)
{
  int err_code;

#ifndef LIBCAS_FOR_JSP
  if (!as_info->cur_statement_pooling)
    {
      hm_srv_handle_free_all ();
    }
#else
  hm_srv_handle_free_all ();
#endif

  if (tran_type == CCI_TRAN_COMMIT)
    {
      err_code = db_commit_transaction ();
      cas_log_debug (ARG_FILE_LINE,
		     "ux_end_tran: db_commit_transaction() = %d", err_code);
    }
  else if (tran_type == CCI_TRAN_ROLLBACK)
    {
      err_code = db_abort_transaction ();
      cas_log_debug (ARG_FILE_LINE,
		     "ux_end_tran: db_abort_transaction() = %d", err_code);
    }
  else
    {
      err_code = CAS_ER_INTERNAL;
    }

  if (err_code >= 0)
    {
      xa_prepare_flag = 0;
#ifndef LIBCAS_FOR_JSP
      if (reset_con_status)
	as_info->con_status = CON_STATUS_OUT_TRAN;
#endif
    }
  else
    {
      errors_in_transaction++;
    }

  return err_code;
}

int
ux_execute (T_SRV_HANDLE * srv_handle, char flag, int max_col_size,
	    int max_row, int argc, void **argv, T_NET_BUF * net_buf,
	    T_REQ_INFO * req_info, CACHE_TIME * clt_cache_time,
	    int *clt_cache_reusable)
{
  int err_code;
  DB_VALUE *value_list = NULL;
  int num_bind = 0;
  int i;
  int n;
  int stmt_id;
  DB_QUERY_RESULT *result = NULL;
  DB_SESSION *session;
  T_BROKER_VERSION client_version = req_info->client_version;

  srv_handle->query_info_flag = FALSE;

  hm_qresult_end (srv_handle, FALSE);

  if (srv_handle->is_prepared == FALSE)
    {
      hm_session_free (srv_handle);

      if (!(session = db_open_buffer (srv_handle->sql_stmt)))
	{
	  err_code = CAS_ER_NO_MORE_MEMORY;
	  goto execute_error1;
	}
      srv_handle->session = session;
    }
  else
    {
      session = (DB_SESSION *) srv_handle->session;
    }

  num_bind = srv_handle->num_markers;

  if (num_bind > 0)
    {
      err_code =
	make_bind_value (num_bind, argc, argv, &value_list, net_buf,
			 DB_TYPE_NULL);
      if (err_code < 0)
	{
	  if (err_code == CAS_ER_DBMS)
	    goto execute_error2;
	  else
	    goto execute_error1;
	}

      db_push_values (session, num_bind, value_list);
    }

  if (srv_handle->is_prepared == FALSE)
    {
      stmt_id = db_compile_statement (session);
      if (stmt_id < 0)
	{
	  DB_ERR_MSG_SET (net_buf, stmt_id);
	  err_code = stmt_id;
	  goto execute_error2;
	}
    }
  else
    {
      stmt_id = srv_handle->q_result->stmt_id;
    }

  if (((flag & CCI_EXEC_ASYNC) || (max_row > 0))
      && db_is_query_async_executable (session, stmt_id))
    {
      db_set_session_mode_async (session);
      srv_handle->q_result->async_flag = TRUE;
    }
  else
    {
      db_set_session_mode_sync (session);
      srv_handle->q_result->async_flag = FALSE;
    }

#if 0				/* yaw */
  if ((err_code = check_class_chn (srv_handle)) < 0)
    {
      goto execute_error1;
    }
#endif

  if (flag & CCI_EXEC_QUERY_INFO)
    {
      if (cas_log_query_info_init (srv_handle->id) < 0)
	{
	  flag &= ~CCI_EXEC_QUERY_INFO;
#ifndef LIBCAS_FOR_JSP
	  SQL_LOG2_EXEC_BEGIN (as_info->cur_sql_log2, stmt_id);
#endif
	}
      else
	{
	  if (flag & CCI_EXEC_ONLY_QUERY_PLAN)
	    set_optimization_level (514);
	  else
	    set_optimization_level (513);
#if !defined(WINDOWS)
	  histo_clear ();
#endif
	}
    }
  else
    {
#ifndef LIBCAS_FOR_JSP
      SQL_LOG2_EXEC_BEGIN (as_info->cur_sql_log2, stmt_id);
#endif
    }

  if (clt_cache_time)
    {
      db_set_client_cache_time (session, stmt_id, clt_cache_time);
    }

  n = db_execute_and_keep_statement (session, stmt_id, &result);
#ifndef LIBCAS_FOR_JSP
  as_info->num_queries_processed %= MAX_DIAG_DATA_VALUE;
  as_info->num_queries_processed++;
#endif
  if (n < 0)
    {
      DB_ERR_MSG_SET (net_buf, n);
    }
#if 1
  else if (clt_cache_time && db_is_client_cache_reusable (result))
    {
      *clt_cache_reusable = TRUE;
    }
  else
    {
      /* success; peek the values in tuples */
      (void) db_query_set_copy_tplvalue (result, 0 /* peek */ );
      if (srv_handle->q_result->async_flag == TRUE)
	{
	  int tmp_err_code;

	  tmp_err_code = db_query_first_tuple (result);
	  if (tmp_err_code == DB_CURSOR_END)
	    {
	      n = 0;
	      srv_handle->q_result->async_flag = FALSE;
	    }
	  else if (tmp_err_code < 0)
	    {
	      n = tmp_err_code;
	      DB_ERR_MSG_SET (net_buf, n);
	    }
	}
    }
#endif

  if (flag & CCI_EXEC_QUERY_INFO)
    {
#if !defined(WINDOWS)
      cas_log_query_info_next ();
      histo_print ();
      cas_log_query_info_end ();
#ifndef LIBCAS_FOR_JSP
      SQL_LOG2_EXEC_APPEND (as_info->cur_sql_log2, stmt_id, n,
			    cas_log_query_plan_file (srv_handle->id),
			    cas_log_query_histo_file (srv_handle->id));
#endif
#endif
      srv_handle->query_info_flag = TRUE;
      set_optimization_level (1);
    }
  else
    {
#ifndef LIBCAS_FOR_JSP
      SQL_LOG2_EXEC_END (as_info->cur_sql_log2, stmt_id, n);
#endif
    }

  if (n < 0)
    {
      if (srv_handle->is_pooled
	  && (n == ER_QPROC_INVALID_XASLNODE || n == ER_HEAP_UNKNOWN_OBJECT))
	{
	  err_code = CAS_ER_STMT_POOLING;
	  goto execute_error1;
	}
      err_code = n;
      goto execute_error2;
    }

  if ((!(flag & CCI_EXEC_ASYNC))
      && (max_row > 0)
      && (db_get_statement_type (session, stmt_id) == CUBRID_STMT_SELECT)
      && *clt_cache_reusable == FALSE)
    {
      err_code = db_query_seek_tuple (result, max_row, 1);
      if (err_code < 0)
	{
	  DB_ERR_MSG_SET (net_buf, err_code);
	  goto execute_error2;
	}
      else if (err_code == DB_CURSOR_END)
	{
	  db_query_sync (result, true);
	  n = db_query_tuple_count (result);
	}
      else
	{
	  n = max_row;
	}
      n = MIN (n, max_row);
      srv_handle->q_result->async_flag = FALSE;
    }

  net_buf_cp_int (net_buf, n, NULL);

  srv_handle->max_col_size = max_col_size;
  srv_handle->q_result->result = (void *) result;
  srv_handle->q_result->tuple_count = n;
  srv_handle->cur_result = (void *) srv_handle->q_result;
  srv_handle->cur_result_index = 1;
  srv_handle->num_q_result = 1;
  srv_handle->max_row = max_row;

  db_get_cacheinfo (session, stmt_id, &srv_handle->use_plan_cache,
		    &srv_handle->use_query_cache);

#ifndef LIBCAS_FOR_JSP
  if (!(srv_handle->q_result->stmt_type == CUBRID_STMT_SELECT
	|| srv_handle->q_result->stmt_type == CUBRID_STMT_CALL
	|| srv_handle->q_result->stmt_type == CUBRID_STMT_GET_STATS
	|| srv_handle->q_result->stmt_type == CUBRID_STMT_EVALUATE)
      && srv_handle->auto_commit_mode == TRUE)
    {
      req_info->need_auto_commit = TRAN_AUTOCOMMIT;
    }
#endif

  if (value_list)
    {
      for (i = 0; i < num_bind; i++)
	db_value_clear (&(value_list[i]));
      FREE_MEM (value_list);
    }

  if (*clt_cache_reusable == TRUE)
    {
      net_buf_cp_byte (net_buf, 1);
      return 0;
    }
  else
    {
      net_buf_cp_byte (net_buf, 0);
    }

  return (execute_info_set (srv_handle, net_buf, client_version, flag));

execute_error1:
  net_buf_clear (net_buf);
  net_buf_cp_int (net_buf, err_code, NULL);
execute_error2:

#ifndef LIBCAS_FOR_JSP
  if (srv_handle->auto_commit_mode)
    {
      req_info->need_auto_commit = TRAN_AUTOROLLBACK;
    }
#endif
  errors_in_transaction++;

  if (value_list)
    {
      for (i = 0; i < num_bind; i++)
	db_value_clear (&(value_list[i]));
      FREE_MEM (value_list);
    }
  return err_code;
}

int
ux_execute_all (T_SRV_HANDLE * srv_handle, char flag, int max_col_size,
		int max_row, int argc, void **argv, T_NET_BUF * net_buf,
		T_REQ_INFO * req_info, CACHE_TIME * clt_cache_time,
		int *clt_cache_reusable)
{
  int err_code;
  DB_VALUE *value_list = NULL;
  int num_bind = 0;
  int i;
  int n;
  int stmt_type = -1, stmt_id = -1;
  int q_res_idx;
  char is_prepared, async_flag;
  char is_first_stmt = TRUE;
  DB_QUERY_RESULT *result = NULL;
  DB_SESSION *session = NULL;
  T_BROKER_VERSION client_version = req_info->client_version;

  srv_handle->query_info_flag = FALSE;

  is_prepared = srv_handle->is_prepared;

  hm_qresult_end (srv_handle, FALSE);

  if (is_prepared == TRUE)
    {
      session = (DB_SESSION *) srv_handle->session;
      stmt_id = srv_handle->q_result->stmt_id;
      stmt_type = srv_handle->q_result->stmt_type;
    }
  else
    {
      hm_session_free (srv_handle);

      if (!(session = db_open_buffer (srv_handle->sql_stmt)))
	{
	  err_code = db_error_code ();
	  DB_ERR_MSG_SET (net_buf, err_code);
	  goto execute_all_err_db;
	}
      srv_handle->session = session;
    }

  num_bind = srv_handle->num_markers;
  if (num_bind > 0)
    {
      err_code =
	make_bind_value (num_bind, argc, argv, &value_list, net_buf,
			 DB_TYPE_NULL);
      if (err_code < 0)
	{
	  if (err_code == CAS_ER_DBMS)
	    goto execute_all_err_db;
	  else
	    goto execute_all_err_cas;
	}

      db_push_values (session, num_bind, value_list);
    }

#if 0				/* yaw */
  if ((err_code = check_class_chn (srv_handle)) < 0)
    {
      goto execute_all_err_cas;
    }
#endif

  q_res_idx = 0;
  while (1)
    {
      if (is_prepared == FALSE)
	{
	  stmt_id = db_compile_statement (session);
	  if (stmt_id == 0)
	    {
	      break;
	    }
	  if (stmt_id < 0)
	    {
	      err_code = stmt_id;
	      DB_ERR_MSG_SET (net_buf, err_code);
	      goto execute_all_err_db;
	    }

	  if ((stmt_type = db_get_statement_type (session, stmt_id)) < 0)
	    {
	      err_code = stmt_type;
	      DB_ERR_MSG_SET (net_buf, err_code);
	      goto execute_all_err_db;
	    }
	}

      if (((flag & CCI_EXEC_ASYNC) || (max_row > 0))
	  && db_is_query_async_executable (session, stmt_id))
	{
	  db_set_session_mode_async (session);
	  async_flag = TRUE;
	}
      else
	{
	  db_set_session_mode_sync (session);
	  async_flag = FALSE;
	}

      if (clt_cache_time)
	db_set_client_cache_time (session, stmt_id, clt_cache_time);

#ifndef LIBCAS_FOR_JSP
      SQL_LOG2_EXEC_BEGIN (as_info->cur_sql_log2, stmt_id);
#endif
      n = db_execute_and_keep_statement (session, stmt_id, &result);
#ifndef LIBCAS_FOR_JSP
      as_info->num_queries_processed %= MAX_DIAG_DATA_VALUE;
      as_info->num_queries_processed++;
#endif
#ifndef LIBCAS_FOR_JSP
      SQL_LOG2_EXEC_END (as_info->cur_sql_log2, stmt_id, n);
#endif
      if (n < 0)
	{
	  if (srv_handle->is_pooled
	      && (n == ER_QPROC_INVALID_XASLNODE
		  || n == ER_HEAP_UNKNOWN_OBJECT))
	    {
	      err_code = CAS_ER_STMT_POOLING;
	      goto execute_all_err_cas;
	    }
	  err_code = n;
	  DB_ERR_MSG_SET (net_buf, err_code);
	  goto execute_all_err_db;
	}
#if 1
      else if (clt_cache_time && db_is_client_cache_reusable (result))
	{
	  *clt_cache_reusable = TRUE;
	}
      else if (result != NULL)
	{
	  /* success; peek the values in tuples */
	  (void) db_query_set_copy_tplvalue (result, 0 /* peek */ );
	  if (is_first_stmt == TRUE && async_flag == TRUE)
	    {
	      int tmp_err_code;
	      tmp_err_code = db_query_first_tuple (result);
	      if (tmp_err_code == DB_CURSOR_END)
		{
		  n = 0;
		  async_flag = FALSE;
		}
	      else if (tmp_err_code < 0)
		{
		  err_code = tmp_err_code;
		  DB_ERR_MSG_SET (net_buf, err_code);
		  goto execute_all_err_db;
		}
	    }
	}
#endif

      if ((!(flag & CCI_EXEC_ASYNC))
	  && (max_row > 0)
	  && (db_get_statement_type (session, stmt_id) == CUBRID_STMT_SELECT)
	  && *clt_cache_reusable == FALSE)
	{
	  err_code = db_query_seek_tuple (result, max_row, 1);
	  if (err_code < 0)
	    {
	      DB_ERR_MSG_SET (net_buf, err_code);
	      goto execute_all_err_db;
	    }
	  else if (err_code == DB_CURSOR_END)
	    {
	      db_query_sync (result, true);
	      n = db_query_tuple_count (result);
	    }
	  else
	    {
	      n = max_row;
	    }
	  n = MIN (n, max_row);
	  async_flag = FALSE;
	}

      if (is_first_stmt == TRUE)
	{
	  net_buf_cp_int (net_buf, n, NULL);
	  srv_handle->num_q_result = 0;
	  is_first_stmt = FALSE;
	}

      q_res_idx = srv_handle->num_q_result;
      srv_handle->q_result = (T_QUERY_RESULT *) REALLOC (srv_handle->q_result,
							 sizeof
							 (T_QUERY_RESULT) *
							 (q_res_idx + 1));
      if (srv_handle->q_result == NULL)
	{
	  db_query_end (result);
	  err_code = CAS_ER_NO_MORE_MEMORY;
	  goto execute_all_err_cas;
	}

      if (is_prepared == FALSE)
	{
	  hm_qresult_clear (&(srv_handle->q_result[q_res_idx]));
	  srv_handle->q_result[q_res_idx].stmt_type = stmt_type;
	  srv_handle->q_result[q_res_idx].stmt_id = stmt_id;
	}

      db_get_cacheinfo (session, stmt_id, &srv_handle->use_plan_cache,
			&srv_handle->use_query_cache);

      srv_handle->q_result[q_res_idx].async_flag = async_flag;
      srv_handle->q_result[q_res_idx].result = result;
      srv_handle->q_result[q_res_idx].tuple_count = n;
      (srv_handle->num_q_result)++;
      is_prepared = FALSE;
    }

  srv_handle->max_row = max_row;
  srv_handle->max_col_size = max_col_size;
  srv_handle->cur_result = (void *) srv_handle->q_result;
  srv_handle->cur_result_index = 1;

#ifndef LIBCAS_FOR_JSP
  if (srv_handle->num_q_result < 2
      && !(srv_handle->q_result->stmt_type == CUBRID_STMT_SELECT ||
	   srv_handle->q_result->stmt_type == CUBRID_STMT_CALL ||
	   srv_handle->q_result->stmt_type == CUBRID_STMT_GET_STATS ||
	   srv_handle->q_result->stmt_type == CUBRID_STMT_EVALUATE)
      && srv_handle->auto_commit_mode == TRUE)
    {
      req_info->need_auto_commit = TRAN_AUTOCOMMIT;
    }
#endif

  if (value_list)
    {
      for (i = 0; i < num_bind; i++)
	db_value_clear (&(value_list[i]));
      FREE_MEM (value_list);
    }

  if (*clt_cache_reusable == TRUE)
    {
      net_buf_cp_byte (net_buf, 1);
      return 0;
    }
  else
    {
      net_buf_cp_byte (net_buf, 0);
    }

  return (execute_info_set (srv_handle, net_buf, client_version, flag));

execute_all_err_cas:
  net_buf_clear (net_buf);
  net_buf_cp_int (net_buf, err_code, NULL);
execute_all_err_db:
#ifndef LIBCAS_FOR_JSP
  if (srv_handle->auto_commit_mode)
    {
      req_info->need_auto_commit = TRAN_AUTOROLLBACK;
    }
#endif
  errors_in_transaction++;

  if (value_list)
    {
      for (i = 0; i < num_bind; i++)
	db_value_clear (&(value_list[i]));
      FREE_MEM (value_list);
    }
  return err_code;
}

extern DB_VALUE *db_get_hostvars (DB_SESSION * session);
extern void jsp_set_prepare_call ();
extern void jsp_unset_prepare_call ();

int
ux_execute_call (T_SRV_HANDLE * srv_handle, char flag, int max_col_size,
		 int max_row, int argc, void **argv, T_NET_BUF * net_buf,
		 T_REQ_INFO * req_info, CACHE_TIME * clt_cache_time,
		 int *clt_cache_reusable)
{
  int err_code;
  DB_VALUE *value_list = NULL, *out_vals;
  int num_bind;
  int i, j;
  int n;
  int stmt_id;
  DB_QUERY_RESULT *result = NULL;
  DB_SESSION *session;
  T_BROKER_VERSION client_version = req_info->client_version;
  T_PREPARE_CALL_INFO *call_info;

  call_info = srv_handle->prepare_call_info;
  srv_handle->query_info_flag = FALSE;
  hm_qresult_end (srv_handle, FALSE);
  session = (DB_SESSION *) srv_handle->session;
  num_bind = srv_handle->num_markers;

  if (num_bind > 0)
    {
      err_code =
	make_bind_value (num_bind, argc, argv, &value_list, net_buf,
			 DB_TYPE_NULL);
      if (err_code < 0)
	{
	  if (err_code == CAS_ER_DBMS)
	    goto execute_error2;
	  else
	    goto execute_error1;
	}

      if (call_info->is_first_out)
	{
	  db_push_values (session, num_bind - 1, &(value_list[1]));
	}
      else
	{
	  db_push_values (session, num_bind, value_list);
	}
    }

  stmt_id = srv_handle->q_result->stmt_id;
  db_set_session_mode_sync (session);
  srv_handle->q_result->async_flag = FALSE;

  jsp_set_prepare_call ();
  n = db_execute_and_keep_statement (session, stmt_id, &result);
#ifndef LIBCAS_FOR_JSP
  as_info->num_queries_processed %= MAX_DIAG_DATA_VALUE;
  as_info->num_queries_processed++;
#endif
  jsp_unset_prepare_call ();

  if (n < 0)
    {
      DB_ERR_MSG_SET (net_buf, n);
      err_code = n;
      goto execute_error2;
    }

  /* success; copy the values in tuples */
  (void) db_query_set_copy_tplvalue (result, 1 /* copy */ );

  i = 0;
  if (call_info->is_first_out)
    {
      db_query_get_tuple_value (result, 0,
				((DB_VALUE **) call_info->dbval_args)[0]);
      i++;
    }

  out_vals = db_get_hostvars (session);

  for (j = 0; i < call_info->num_args; i++, j++)
    {
      *(((DB_VALUE **) call_info->dbval_args)[i]) = out_vals[j];
    }

  net_buf_cp_int (net_buf, n, NULL);

  srv_handle->max_col_size = max_col_size;
  srv_handle->q_result->result = (void *) result;
  srv_handle->q_result->tuple_count = n;
  srv_handle->cur_result = (void *) srv_handle->q_result;
  srv_handle->cur_result_index = 1;
  srv_handle->num_q_result = 1;
  srv_handle->max_row = max_row;

  if (value_list)
    {
      for (i = 0; i < num_bind; i++)
	db_value_clear (&(value_list[i]));
      FREE_MEM (value_list);
    }

  net_buf_cp_byte (net_buf, 0);	/* client cache reusable - always false */

  return (execute_info_set (srv_handle, net_buf, client_version, flag));

execute_error1:
  net_buf_clear (net_buf);
  net_buf_cp_int (net_buf, err_code, NULL);
execute_error2:
  if (value_list)
    {
      for (i = 0; i < num_bind; i++)
	db_value_clear (&(value_list[i]));
      FREE_MEM (value_list);
    }
  return err_code;
}

int
ux_next_result (T_SRV_HANDLE * srv_handle, char flag, T_NET_BUF * net_buf,
		T_REQ_INFO * req_info)
{
  int err_code;
  T_QUERY_RESULT *cur_result;
  T_QUERY_RESULT *prev_result;
  T_BROKER_VERSION client_version = req_info->client_version;

  if (srv_handle == NULL || srv_handle->schema_type >= CCI_SCH_FIRST)
    {
      err_code = CAS_ER_SRV_HANDLE;
      goto next_result_error1;
    }

  if (srv_handle->cur_result_index >= srv_handle->num_q_result)
    {
      err_code = CAS_ER_NO_MORE_RESULT_SET;
      goto next_result_error1;
    }

  if (srv_handle->cur_result_index > 0)
    {
      prev_result = &(srv_handle->q_result[srv_handle->cur_result_index - 1]);
      if (prev_result->result && flag != CCI_KEEP_CURRENT_RESULT)
	{
	  db_query_end ((DB_QUERY_RESULT *) (prev_result->result));
	  prev_result->result = NULL;
	}
    }

  cur_result = &(srv_handle->q_result[srv_handle->cur_result_index]);

#if 1
  if (cur_result->async_flag == TRUE)
    {
      int tmp_err_code;
      tmp_err_code =
	db_query_first_tuple ((DB_QUERY_RESULT *) cur_result->result);
      if (tmp_err_code == DB_CURSOR_END)
	{
	  cur_result->tuple_count = 0;
	  cur_result->async_flag = FALSE;
	}
      else if (tmp_err_code < 0)
	{
	  err_code = tmp_err_code;
	  DB_ERR_MSG_SET (net_buf, err_code);
	  goto next_result_error2;
	}
    }
#endif

  net_buf_cp_int (net_buf, 0, NULL);	/* result code */

  /* result msg */
  net_buf_cp_int (net_buf, cur_result->tuple_count, NULL);
  net_buf_cp_byte (net_buf, cur_result->stmt_type);

  err_code = prepare_column_list_info_set ((DB_SESSION *) srv_handle->session,
					   srv_handle->prepare_flag,
					   cur_result, net_buf,
					   client_version);
  if (err_code < 0)
    {
      goto next_result_error2;
    }

  srv_handle->cur_result = cur_result;
  (srv_handle->cur_result_index)++;

  return 0;

next_result_error1:
  net_buf_clear (net_buf);
  net_buf_cp_int (net_buf, err_code, NULL);

next_result_error2:
  return err_code;
}

int
ux_execute_batch (int argc, void **argv, T_NET_BUF * net_buf,
		  T_REQ_INFO * req_info)
{
  int i;
  int err_code, sql_size, res_count, stmt_id;
  bool use_plan_cache, use_query_cache;
  char *sql_stmt, *err_msg;
  char stmt_type;
  char auto_commit_mode;
  DB_SESSION *session;
  DB_QUERY_RESULT *result;

  NET_ARG_GET_CHAR (auto_commit_mode, argv[0]);
  argc--;			/* real query num is argc-1 (because auto commit) */

  net_buf_cp_int (net_buf, 0, NULL);	/* result code */

  net_buf_cp_int (net_buf, argc, NULL);	/* result msg. num_query */


#ifndef LIBCAS_FOR_JSP
  if (auto_commit_mode == TRUE)
    {
      req_info->need_auto_commit = TRAN_AUTOCOMMIT;
    }
#endif

  for (i = 1; i <= argc; i++)
    {
      use_plan_cache = false;
      use_query_cache = false;

      NET_ARG_GET_STR (sql_stmt, sql_size, argv[i]);

      cas_log_write_nonl (0, false, "batch %d : %s", i,
			  (sql_stmt ? sql_stmt : ""));

      session = NULL;

      if (!(session = db_open_buffer (sql_stmt)))
	{
	  cas_log_write2 ("");
	  goto batch_error;
	}
#ifndef LIBCAS_FOR_JSP
      SQL_LOG2_COMPILE_BEGIN (as_info->cur_sql_log2, sql_stmt);
#endif /* LIBCAS_FOR_JSP */
      if ((stmt_id = db_compile_statement (session)) < 0)
	{
	  cas_log_write2 ("");
	  goto batch_error;
	}
      if ((stmt_type = db_get_statement_type (session, stmt_id)) < 0)
	{
	  cas_log_write2 ("");
	  goto batch_error;
	}

#ifndef LIBCAS_FOR_JSP
      SQL_LOG2_EXEC_BEGIN (as_info->cur_sql_log2, stmt_id);
#endif /* LIBCAS_FOR_JSP */
      db_get_cacheinfo (session, stmt_id, &use_plan_cache, &use_query_cache);

      cas_log_write2_nonl (" %s\n", use_plan_cache ? "(PC)" : "");

      res_count = db_execute_statement (session, stmt_id, &result);
#ifndef LIBCAS_FOR_JSP
      as_info->num_queries_processed %= MAX_DIAG_DATA_VALUE;
      as_info->num_queries_processed++;
#endif /* LIBCAS_FOR_JSP */

#ifndef LIBCAS_FOR_JSP
      SQL_LOG2_EXEC_END (as_info->cur_sql_log2, stmt_id, res_count);
#endif /* LIBCAS_FOR_JSP */
      if (res_count < 0)
	{
	  goto batch_error;
	}

      /* success; peek the values in tuples */
      (void) db_query_set_copy_tplvalue (result, 0 /* peek */ );

      net_buf_cp_byte (net_buf, stmt_type);
      net_buf_cp_int (net_buf, res_count, NULL);

      do
	{
	  DB_VALUE val;
	  DB_OBJECT *ins_obj_p;
	  T_OBJECT ins_oid;

	  memset (&ins_oid, 0, sizeof (T_OBJECT));
	  if ((stmt_type == CUBRID_STMT_INSERT) && (res_count == 1)
	      && (result != NULL)
	      && (db_query_get_tuple_value (result, 0, &val) >= 0))
	    {
	      ins_obj_p = db_get_object (&val);
	      uobj_to_cas_obj (ins_obj_p, &ins_oid);
	      db_value_clear (&val);
	    }
	  NET_BUF_CP_OBJECT (net_buf, &ins_oid);
	}
      while (0);

      db_query_end (result);
      db_close_session (session);

      continue;

    batch_error:
      stmt_type = CUBRID_MAX_STMT_TYPE;

      err_code = db_error_code ();
      if (err_code < 0)
	{
	  err_msg = (char *) db_error_string (1);

	}
      else
	{
	  err_code = -1;
	  err_msg = (char *) "";
	}

      net_buf_cp_byte (net_buf, stmt_type);
      net_buf_cp_int (net_buf, err_code, NULL);
      net_buf_cp_int (net_buf, strlen (err_msg) + 1, NULL);
      net_buf_cp_str (net_buf, err_msg, strlen (err_msg) + 1);

      if (session)
	db_close_session (session);
    }

  return 0;
}

int
ux_execute_array (T_SRV_HANDLE * srv_handle, int argc, void **argv,
		  T_NET_BUF * net_buf, T_REQ_INFO * req_info)
{
  DB_VALUE *value_list = NULL;
  int err_code;
  int i, num_bind = 0;
  int num_markers;
  int stmt_id = -1;
  int first_value;
  int res_count;
  int num_query;
  int num_query_msg_offset;
  char is_prepared;
  char *err_msg;
  DB_SESSION *session = NULL;
  DB_QUERY_RESULT *result;
  int stmt_type = -1;
  T_OBJECT ins_oid;
  char auto_commit_mode;

  NET_ARG_GET_CHAR (auto_commit_mode, argv[1]);

#ifndef LIBCAS_FOR_JSP
  if (auto_commit_mode == TRUE)
    {
      req_info->need_auto_commit = TRAN_AUTOCOMMIT;
    }
#endif /* LIBCAS_FOR_JSP */

  if (srv_handle == NULL || srv_handle->schema_type >= CCI_SCH_FIRST)
    {
      err_code = CAS_ER_SRV_HANDLE;
      goto execute_array_error1;
    }

  srv_handle->auto_commit_mode = auto_commit_mode;

  if (srv_handle->prepare_flag & CCI_PREPARE_CALL)
    {
      net_buf_cp_int (net_buf, 0, NULL);	/* result code */
      net_buf_cp_int (net_buf, 0, NULL);	/* num_query */
      return 0;
    }

  hm_qresult_end (srv_handle, FALSE);
  is_prepared = srv_handle->is_prepared;

  if (is_prepared == FALSE)
    {
      hm_session_free (srv_handle);
      /*
         num_markers = 1;
       */
    }
  else
    {
      session = (DB_SESSION *) srv_handle->session;
      stmt_id = srv_handle->q_result->stmt_id;
      /*
         num_markers = db_number_of_input_markers(session, stmt_id);
       */
    }

  num_markers = srv_handle->num_markers;

  net_buf_cp_int (net_buf, 0, NULL);	/* result code */

  num_query = 0;
  net_buf_cp_int (net_buf, num_query, &num_query_msg_offset);


  if (argc <= 3 || num_markers < 1)
    {
      return 0;
    }

  num_bind = (argc - 2) / 2;

  err_code = make_bind_value (num_bind, argc - 2, argv + 2, &value_list,
			      net_buf, DB_TYPE_NULL);
  if (err_code < 0)
    {
      if (err_code == CAS_ER_DBMS)
	goto execute_array_error2;
      else
	goto execute_array_error1;
    }

  first_value = 0;

  while (num_bind >= num_markers)
    {
      num_query++;

      if (is_prepared == FALSE)
	{
	  if (!(session = db_open_buffer (srv_handle->sql_stmt)))
	    {
	      goto exec_db_error;
	    }
	}

      db_push_values (session, num_markers, &(value_list[first_value]));

      if (is_prepared == FALSE)
	{
	  stmt_id = db_compile_statement (session);
	  if (stmt_id < 0)
	    {
	      goto exec_db_error;
	    }
	  /*
	     num_markers = db_number_of_input_markers(session, stmt_id);
	   */
	}

#ifndef LIBCAS_FOR_JSP
      SQL_LOG2_EXEC_BEGIN (as_info->cur_sql_log2, stmt_id);
#endif /* LIBCAS_FOR_JSP */
      res_count = db_execute_and_keep_statement (session, stmt_id, &result);
#ifndef LIBCAS_FOR_JSP
      as_info->num_queries_processed %= MAX_DIAG_DATA_VALUE;
      as_info->num_queries_processed++;
#endif /* LIBCAS_FOR_JSP */
#ifndef LIBCAS_FOR_JSP
      SQL_LOG2_EXEC_END (as_info->cur_sql_log2, stmt_id, res_count);
#endif /* LIBCAS_FOR_JSP */
      if (res_count < 0)
	{
	  goto exec_db_error;
	}
      db_get_cacheinfo (session, stmt_id, &srv_handle->use_plan_cache,
			&srv_handle->use_query_cache);

      /* success; peek the values in tuples */
      (void) db_query_set_copy_tplvalue (result, 0 /* peek */ );

      do
	{
	  DB_VALUE val;
	  DB_OBJECT *ins_obj_p;

	  if (stmt_type < 0)
	    {
	      stmt_type = db_get_statement_type (session, stmt_id);
	    }

	  memset (&ins_oid, 0, sizeof (T_OBJECT));
	  if ((stmt_type == CUBRID_STMT_INSERT) && (res_count == 1)
	      && (result != NULL)
	      && (db_query_get_tuple_value (result, 0, &val) >= 0))
	    {
	      ins_obj_p = db_get_object (&val);
	      uobj_to_cas_obj (ins_obj_p, &ins_oid);
	      db_value_clear (&val);
	    }
	}
      while (0);

      db_query_end (result);
      if (is_prepared == FALSE)
	{
	  db_close_session (session);
	}

      net_buf_cp_int (net_buf, res_count, NULL);
      NET_BUF_CP_OBJECT (net_buf, &ins_oid);

      num_bind -= num_markers;
      first_value += num_markers;

      continue;

    exec_db_error:
      err_code = db_error_code ();
      if (err_code < 0)
	{
	  err_msg = (char *) db_error_string (1);
	  errors_in_transaction++;
	}
      else
	{
	  err_code = -1;
	  err_msg = (char *) "";
	}

      net_buf_cp_int (net_buf, err_code, NULL);
      net_buf_cp_int (net_buf, strlen (err_msg) + 1, NULL);
      net_buf_cp_str (net_buf, err_msg, strlen (err_msg) + 1);
      if (is_prepared == FALSE && session != NULL)
	db_close_session (session);

      num_bind -= num_markers;
      first_value += num_markers;
    }

  net_buf_overwrite_int (net_buf, num_query_msg_offset, num_query);

  if (value_list)
    {
      for (i = 0; i < num_bind; i++)
	db_value_clear (&(value_list[i]));
      FREE_MEM (value_list);
    }
  return 0;

execute_array_error1:
  net_buf_clear (net_buf);
  net_buf_cp_int (net_buf, err_code, NULL);
execute_array_error2:
  errors_in_transaction++;

  if (value_list)
    {
      for (i = 0; i < num_bind; i++)
	db_value_clear (&(value_list[i]));
      FREE_MEM (value_list);
    }
  return err_code;
}

void
ux_get_tran_setting (int *lock_wait, int *isol_level)
{
  int tmp_lock_wait;
  DB_TRAN_ISOLATION tmp_isol_level;

  db_get_tran_settings (&tmp_lock_wait, &tmp_isol_level);

  if (lock_wait)
    *lock_wait = (int) tmp_lock_wait;
  if (isol_level)
    *isol_level = (int) tmp_isol_level;
}

int
ux_set_isolation_level (int new_isol_level, T_NET_BUF * net_buf)
{
  int err_code;
  err_code = db_set_isolation ((DB_TRAN_ISOLATION) new_isol_level);
  if (err_code < 0)
    {
      errors_in_transaction++;
      DB_ERR_MSG_SET (net_buf, err_code);
      return CAS_ER_DBMS;
    }
  return 0;
}

void
ux_set_lock_timeout (int lock_timeout)
{
#if 1				/* yaw */
  extern float tran_reset_wait_times (float waitsecs);
  (void) tran_reset_wait_times ((float) lock_timeout / 1000);
#else
  db_set_lock_timeout (lock_timeout);
#endif
}

int
ux_fetch (T_SRV_HANDLE * srv_handle, int cursor_pos, int fetch_count,
	  char fetch_flag, int result_set_index, T_NET_BUF * net_buf,
	  T_REQ_INFO * req_info)
{
  int err_code;
  int fetch_func_index;

  if (srv_handle == NULL)
    {
      err_code = CAS_ER_SRV_HANDLE;
      goto fetch_error1;
    }

  if (srv_handle->prepare_flag & CCI_PREPARE_CALL)
    {
      net_buf_cp_int (net_buf, 0, NULL);	/* result code */
      return (fetch_call (srv_handle, net_buf));
    }

  if (srv_handle->schema_type < 0)
    {
      fetch_func_index = 0;
    }
  else if (srv_handle->schema_type >= CCI_SCH_FIRST
	   && srv_handle->schema_type <= CCI_SCH_LAST)
    {
      fetch_func_index = srv_handle->schema_type;
    }
  else
    {
      err_code = CAS_ER_SCHEMA_TYPE;
      goto fetch_error1;
    }

  net_buf_cp_int (net_buf, 0, NULL);	/* result code */

  if (fetch_count <= 0)
    fetch_count = 100;

  err_code =
    (*(fetch_func[fetch_func_index])) (srv_handle, cursor_pos,
				       fetch_count, fetch_flag,
				       result_set_index, net_buf, req_info);

  if (err_code < 0)
    {
      if (err_code == CAS_ER_DBMS)
	goto fetch_error2;
      else
	goto fetch_error1;
    }

  return 0;

fetch_error1:
  net_buf_clear (net_buf);
  net_buf_cp_int (net_buf, err_code, NULL);
fetch_error2:
  errors_in_transaction++;
  return err_code;
}

int
ux_oid_get (int argc, void **argv, T_NET_BUF * net_buf)
{
  DB_OBJECT *obj;
  int err_code = 0;
  int attr_num;
  char **attr_name = NULL;
  char *class_name;

  NET_ARG_GET_OBJECT (obj, argv[0]);
  if ((err_code = ux_check_object (obj, net_buf)) < 0)
    {
      if (err_code == CAS_ER_DBMS)
	goto oid_get_error2;
      else
	goto oid_get_error1;
    }

  if (argc > 1)
    {
      attr_num = get_attr_name_from_argv (argc, argv, &attr_name);
    }
  else
    {
      attr_num = get_attr_name (obj, &attr_name);
    }
  if (attr_num < 0)
    {
      err_code = attr_num;
      goto oid_get_error1;
    }

  net_buf_cp_int (net_buf, 0, NULL);	/* result code */

  class_name = (char *) db_get_class_name (obj);
  if (class_name == NULL)
    {
      net_buf_cp_int (net_buf, 1, NULL);
      net_buf_cp_byte (net_buf, '\0');
    }
  else
    {
      net_buf_cp_int (net_buf, strlen (class_name) + 1, NULL);
      net_buf_cp_str (net_buf, class_name, strlen (class_name) + 1);
    }

  net_buf_cp_int (net_buf, attr_num, NULL);

  err_code = oid_attr_info_set (net_buf, obj, attr_num, attr_name);
  if (err_code < 0)
    {
      if (err_code == CAS_ER_DBMS)
	goto oid_get_error2;
      else
	goto oid_get_error1;
    }
  if (oid_data_set (net_buf, obj, attr_num, attr_name) < 0)
    goto oid_get_error2;

  FREE_MEM (attr_name);
  obj = NULL;
  return 0;

oid_get_error1:
  net_buf_clear (net_buf);
  net_buf_cp_int (net_buf, err_code, NULL);
oid_get_error2:
  errors_in_transaction++;
  FREE_MEM (attr_name);
  obj = NULL;
  if (err_code >= 0)
    err_code = -1;
  return err_code;
}

int
ux_glo_new (char *class_name, char *filename, T_NET_BUF * net_buf)
{
  DB_OBJECT *class_obj;
  DB_VALUE obj_val, arg_val, ret_val;
  int err_code;
  DB_OBJECT *newobj;
  T_OBJECT cas_obj;

  class_obj = db_find_class (class_name);
  if (class_obj == NULL)
    {
      err_code = db_error_code ();
      DB_ERR_MSG_SET (net_buf, err_code);
      goto glo_new_error2;
    }

  err_code = db_make_object (&obj_val, class_obj);
  if (err_code < 0)
    {
      DB_ERR_MSG_SET (net_buf, err_code);
      goto glo_new_error2;
    }

  if (filename)
    {
      err_code = db_make_string (&arg_val, filename);
    }
  else
    {
      err_code = db_make_null (&arg_val);
    }

  if (err_code < 0)
    {
      DB_ERR_MSG_SET (net_buf, err_code);
      goto glo_new_error2;
    }

  err_code = db_send (class_obj, "new_lo_import", &ret_val, &arg_val);
  if (err_code < 0)
    {
      DB_ERR_MSG_SET (net_buf, err_code);
      goto glo_new_error2;
    }

  net_buf_cp_int (net_buf, 0, NULL);	/* result code */

  newobj = db_get_object (&ret_val);
  uobj_to_cas_obj (newobj, &cas_obj);
  NET_BUF_CP_OBJECT (net_buf, &cas_obj);	/* result msg */

  db_value_clear (&ret_val);
  db_value_clear (&arg_val);
  newobj = NULL;
  class_obj = NULL;
  return 0;

glo_new_error2:
  errors_in_transaction++;
  class_obj = NULL;
  return err_code;
}

int
ux_glo_new2 (char *class_name, char glo_type, char *filename,
	     T_NET_BUF * net_buf)
{
  DB_OBJECT *class_obj;
  DB_VALUE ret_val;
  DB_OBJECT *newobj;
  T_OBJECT cas_obj;
  int err_code;

  class_obj = db_find_class (class_name);
  if (class_obj == NULL)
    {
      errors_in_transaction++;
      DB_ERR_MSG_SET (net_buf, db_error_code ());
      return -1;
    }

  if (glo_type == CAS_GLO_NEW_LO)
    {
      err_code = db_send (class_obj, "new_lo", &ret_val);
    }
  else
    {
      DB_VALUE arg_val;

      if (filename)
	err_code = db_make_string (&arg_val, filename);
      else
	err_code = db_make_null (&arg_val);

      if (err_code < 0)
	{
	  DB_ERR_MSG_SET (net_buf, err_code);
	  return -1;
	}
      err_code = db_send (class_obj, "new_fbo", &ret_val, &arg_val);
      db_value_clear (&arg_val);
    }

  if (err_code < 0)
    {
      errors_in_transaction++;
      DB_ERR_MSG_SET (net_buf, err_code);
      return -1;
    }

  net_buf_cp_int (net_buf, 0, NULL);

  newobj = db_get_object (&ret_val);
  uobj_to_cas_obj (newobj, &cas_obj);
  NET_BUF_CP_OBJECT (net_buf, &cas_obj);

  db_value_clear (&ret_val);
  return 0;
}

int
ux_glo_save (DB_OBJECT * obj, char *filename, T_NET_BUF * net_buf)
{
  DB_VALUE arg_val, ret_val;
  int err_code;

  err_code = db_make_string (&arg_val, filename);
  if (err_code < 0)
    {
      DB_ERR_MSG_SET (net_buf, err_code);
      goto glo_save_error2;
    }

  err_code = db_send (obj, "copy_from", &ret_val, &arg_val);
  if (err_code < 0)
    {
      DB_ERR_MSG_SET (net_buf, err_code);
      goto glo_save_error2;
    }
  db_value_clear (&ret_val);
  db_value_clear (&arg_val);

  err_code = db_send (obj, "truncate_data", &ret_val);
  if (err_code < 0)
    {
      DB_ERR_MSG_SET (net_buf, err_code);
      goto glo_save_error2;
    }
  db_value_clear (&ret_val);

  net_buf_cp_int (net_buf, 0, NULL);	/* result code */

  return 0;

glo_save_error2:
  errors_in_transaction++;
  return err_code;
}

int
ux_glo_load (SOCKET sock_fd, DB_OBJECT * obj, T_NET_BUF * net_buf)
{
  DB_VALUE arg_val, ret_val;
  int err_code;
  char tmp_file[PATH_MAX], dirname[PATH_MAX];
  int size;

  get_cubrid_file (FID_CAS_TMPGLO_DIR, dirname);
#ifdef LIBCAS_FOR_JSP
  snprintf (tmp_file, sizeof (tmp_file) - 1, "%s%d.glo", dirname,
	    (int) getpid ());
#else
  snprintf (tmp_file, sizeof (tmp_file) - 1, "%s%s_%d.glo", dirname,
	    broker_name, shm_as_index + 1);
#endif

  err_code = db_make_string (&arg_val, tmp_file);
  if (err_code < 0)
    {
      DB_ERR_MSG_SET (net_buf, err_code);
      goto glo_load_error2;
    }

  err_code = db_send (obj, "copy_to", &ret_val, &arg_val);
  if (err_code < 0)
    {
      db_value_clear (&arg_val);
      DB_ERR_MSG_SET (net_buf, err_code);
      goto glo_load_error2;
    }

  size = db_get_int (&ret_val);
  db_value_clear (&ret_val);
  db_value_clear (&arg_val);

  net_buf_cp_post_send_file (net_buf, size, tmp_file);

  net_buf_cp_int (net_buf, 0, NULL);	/* result code */
  net_buf_cp_int (net_buf, size, NULL);	/* result msg */

  return 0;

glo_load_error2:
  errors_in_transaction++;
  return err_code;
}

int
ux_cursor (int srv_h_id, int offset, int origin, T_NET_BUF * net_buf)
{
  T_SRV_HANDLE *srv_handle;
  int err_code;
  int done, error, count;
  char *err_str = NULL;
  DB_QUERY_RESULT *result;

  srv_handle = hm_find_srv_handle (srv_h_id);
  if (srv_handle == NULL || srv_handle->schema_type >= CCI_SCH_FIRST)
    {
      err_code = CAS_ER_SRV_HANDLE;
      goto cursor_error1;
    }

  if (srv_handle->cur_result == NULL)
    {
      err_code = CAS_ER_SRV_HANDLE;
      goto cursor_error1;
    }

  if (((T_QUERY_RESULT *) (srv_handle->cur_result))->async_flag == FALSE)
    {
      count = ((T_QUERY_RESULT *) (srv_handle->cur_result))->tuple_count;
      net_buf_cp_int (net_buf, 0, NULL);	/* result code */
      net_buf_cp_int (net_buf, count, NULL);	/* result msg */
      return 0;
    }

  result =
    (DB_QUERY_RESULT *) ((T_QUERY_RESULT *) (srv_handle->cur_result))->result;
  if (result == NULL)
    {
      err_code = CAS_ER_NO_MORE_DATA;
      goto cursor_error1;
    }

#if 0
  while (1)
    {
      err_code = db_query_get_info (result, &done, &count, &error, &err_str);
      if (err_code < 0)
	{
	  DB_ERR_MSG_SET (net_buf, db_error_code ());
	  goto cursor_error2;
	}
      if (error < 0)
	{
	  NET_BUF_ERROR_MSG_SET (net_buf, error, err_str);
	  goto cursor_error2;
	}

      if (done == true && count >= 0)
	break;

      if (origin == CCI_CURSOR_FIRST)
	{
	  if (offset < count)
	    {
	      count = -1;
	      break;
	    }
	}

      SLEEP_MILISEC (0, 100);
    }
#else
  if (origin == CCI_CURSOR_FIRST)
    {
      err_code = db_query_seek_tuple (result, offset - 1, 1);
      if (err_code < 0)
	{
	  DB_ERR_MSG_SET (net_buf, db_error_code ());
	  goto cursor_error2;
	}
      else if (err_code == DB_CURSOR_END)
	{
	  err_code = db_query_sync (result, true);
	  if (err_code < 0)
	    {
	      DB_ERR_MSG_SET (net_buf, db_error_code ());
	      goto cursor_error2;
	    }
	  count = db_query_tuple_count (result);
	  if (count < 0)
	    {
	      DB_ERR_MSG_SET (net_buf, db_error_code ());
	      goto cursor_error2;
	    }
	  if (srv_handle->max_row > 0)
	    {
	      count = MIN (count, srv_handle->max_row);
	    }
	}
      else
	{
	  srv_handle->cursor_pos = offset;
	  count = 0;
	  err_code = db_query_get_info (result, &done, &count, &error,
					&err_str);
	  if (err_code < 0 || error < 0)
	    count = -1;
	  if ((srv_handle->max_row > 0) && (count >= srv_handle->max_row))
	    {
	      count = srv_handle->max_row;
	    }
	  else
	    {
	      if (done != true || count < 0)
		count = -1;
#ifdef UNIXWARE7
	      if (count < offset)
		{
		  err_code = db_query_sync (result, true);
		  if (err_code < 0)
		    {
		      DB_ERR_MSG_SET (net_buf, db_error_code ());
		      goto cursor_error2;
		    }
		  count = db_query_tuple_count (result);
		  if (count < 0)
		    {
		      DB_ERR_MSG_SET (net_buf, db_error_code ());
		      goto cursor_error2;
		    }
		  if ((srv_handle->max_row > 0)
		      && (count >= srv_handle->max_row))
		    {
		      count = srv_handle->max_row;
		    }
		}
#endif
	    }
	}
    }
  else if (origin == CCI_CURSOR_LAST)
    {
      if (srv_handle->max_row > 0)
	{
	  err_code = db_query_seek_tuple (result, srv_handle->max_row, 1);
	  if (err_code < 0)
	    {
	      DB_ERR_MSG_SET (net_buf, db_error_code ());
	      goto cursor_error2;
	    }
	  err_code = 0;
	  count = db_query_tuple_count (result);
	  count = MIN (count, srv_handle->max_row);
	}
      else
	{
	  err_code = db_query_sync (result, true);
	  if (err_code < 0)
	    {
	      DB_ERR_MSG_SET (net_buf, db_error_code ());
	      goto cursor_error2;
	    }
	  count = db_query_tuple_count (result);
	  if (count < 0)
	    {
	      DB_ERR_MSG_SET (net_buf, db_error_code ());
	      goto cursor_error2;
	    }
	}
    }
  else
    {
      err_code = CAS_ER_INTERNAL;
      goto cursor_error1;
    }
#endif

  net_buf_cp_int (net_buf, 0, NULL);	/* result code */
  net_buf_cp_int (net_buf, count, NULL);	/* result msg */
  FREE_MEM (err_str);
  return 0;

cursor_error1:
  net_buf_clear (net_buf);
  net_buf_cp_int (net_buf, err_code, NULL);
cursor_error2:
  errors_in_transaction++;
  FREE_MEM (err_str);
  return err_code;
}

int
ux_cursor_update (T_SRV_HANDLE * srv_handle, int cursor_pos, int argc,
		  void **argv, T_NET_BUF * net_buf)
{
  int col_idx;
  int err_code;
  DB_VALUE *attr_val = NULL;
  DB_VALUE oid_val;
  T_QUERY_RESULT *q_result = NULL;
  DB_QUERY_RESULT *result;
  DB_OBJECT *obj_p = NULL;

  if (srv_handle == NULL
      || srv_handle->schema_type >= CCI_SCH_FIRST
      || srv_handle->cur_result == NULL)
    {
      err_code = CAS_ER_SRV_HANDLE;
      goto cursor_update_error1;
    }

  q_result = (T_QUERY_RESULT *) (srv_handle->cur_result);
  result = (DB_QUERY_RESULT *) q_result->result;

  net_buf_cp_int (net_buf, 0, NULL);	/* result code */

  if (result == NULL
      || q_result->col_updatable == FALSE
      || q_result->col_update_info == NULL)
    {
      return 0;
    }

  if (db_query_seek_tuple (result, cursor_pos - 1, 1) != DB_CURSOR_SUCCESS)
    {
      return 0;
    }

  err_code = db_query_get_tuple_oid (result, &oid_val);
  if (err_code < 0)
    {
      DB_ERR_MSG_SET (net_buf, err_code);
      goto cursor_update_error2;
    }
  obj_p = db_get_object (&oid_val);

  argc -= 2;
  argv += 2;

  for (; argc >= 3; argc -= 3, argv += 3)
    {
      char desired_type;

      NET_ARG_GET_INT (col_idx, argv[0]);

      if (q_result->col_update_info[col_idx - 1].attr_name == NULL)
	{
	  continue;
	}

      desired_type =
	get_attr_type (obj_p,
		       q_result->col_update_info[col_idx - 1].attr_name);

      err_code =
	make_bind_value (1, 2, argv + 1, &attr_val, net_buf, desired_type);
      if (err_code < 0)
	{
	  if (err_code == CAS_ER_DBMS)
	    goto cursor_update_error2;
	  else
	    goto cursor_update_error1;
	}

      err_code =
	db_put (obj_p, q_result->col_update_info[col_idx - 1].attr_name,
		attr_val);
      if (err_code < 0)
	{
	  DB_ERR_MSG_SET (net_buf, err_code);
	  goto cursor_update_error2;
	}

      db_value_clear (attr_val);
      FREE_MEM (attr_val);
    }

  db_value_clear (&oid_val);
  obj_p = NULL;

  return 0;
cursor_update_error1:
  net_buf_clear (net_buf);
  net_buf_cp_int (net_buf, err_code, NULL);
cursor_update_error2:
  errors_in_transaction++;
  if (attr_val)
    {
      db_value_clear (attr_val);
      FREE_MEM (attr_val);
    }
  if (obj_p)
    {
      db_value_clear (&oid_val);
      obj_p = NULL;
    }
  return err_code;
}

int
ux_get_db_version (T_NET_BUF * net_buf, T_REQ_INFO * req_info)
{
  char *p;

  p = (char *) rel_build_number ();

  net_buf_cp_int (net_buf, 0, NULL);	/* result code */
  if (p == NULL)
    net_buf_cp_byte (net_buf, '\0');
  else
    net_buf_cp_str (net_buf, p, strlen (p) + 1);

  return 0;
}

int
ux_get_class_num_objs (char *class_name, int flag, T_NET_BUF * net_buf)
{
  DB_OBJECT *class_obj;
  int err_code;
  int num_objs, num_pages;

  class_obj = db_find_class (class_name);
  if (class_obj == NULL)
    {
      err_code = db_error_code ();
      DB_ERR_MSG_SET (net_buf, err_code);
      goto class_num_objs_error2;
    }

  err_code = db_get_class_num_objs_and_pages (class_obj, flag,
					      &num_objs, &num_pages);

  if (err_code < 0)
    {
      DB_ERR_MSG_SET (net_buf, err_code);
      goto class_num_objs_error2;
    }

  net_buf_cp_int (net_buf, 0, NULL);	/* result code */
  net_buf_cp_int (net_buf, num_objs, NULL);
  net_buf_cp_int (net_buf, num_pages, NULL);

  class_obj = NULL;
  return 0;

class_num_objs_error2:
  errors_in_transaction++;
  class_obj = NULL;
  return err_code;
}

void
ux_col_get (DB_COLLECTION * col, char col_type, char ele_type,
	    DB_DOMAIN * ele_domain, T_NET_BUF * net_buf)
{
  int col_size, i;
  DB_VALUE ele_val;

  if (col == NULL)
    {
      col_size = -1;
    }
  else
    {
      col_size = db_col_size (col);
    }

  net_buf_cp_int (net_buf, 0, NULL);

  net_buf_cp_byte (net_buf, col_type);	/* set type (set, multiset, sequence) */

  net_buf_cp_int (net_buf, 1, NULL);	/* num column info */
  net_buf_column_info_set (net_buf,
			   ele_type,
			   (short) db_domain_scale (ele_domain),
			   db_domain_precision (ele_domain), NULL);

  net_buf_cp_int (net_buf, col_size, NULL);	/* set size */
  if (col_size > 0)
    {
      for (i = 0; i < col_size; i++)
	{
	  if (db_col_get (col, i, &ele_val) < 0)
	    {
	      db_make_null (&ele_val);
	    }
	  dbval_to_net_buf (&ele_val, net_buf, 1, 0, 0);
	  db_value_clear (&ele_val);
	}
    }
}

void
ux_col_size (DB_COLLECTION * col, T_NET_BUF * net_buf)
{
  int col_size;

  if (col == NULL)
    col_size = -1;
  else
    col_size = db_col_size (col);

  net_buf_cp_int (net_buf, 0, NULL);	/* result code */

  net_buf_cp_int (net_buf, col_size, NULL);	/* result msg */
}

int
ux_col_set_drop (DB_COLLECTION * col, DB_VALUE * ele_val, T_NET_BUF * net_buf)
{
  int err_code = 0;

  if (col != NULL)
    {
      err_code = db_set_drop (col, ele_val);
      if (err_code < 0)
	{
	  errors_in_transaction++;
	  DB_ERR_MSG_SET (net_buf, err_code);
	  return -1;
	}
    }
  net_buf_cp_int (net_buf, 0, NULL);	/* result code */
  return 0;
}

int
ux_col_set_add (DB_COLLECTION * col, DB_VALUE * ele_val, T_NET_BUF * net_buf)
{
  int err_code = 0;

  if (col != NULL)
    {
      err_code = db_set_add (col, ele_val);
      if (err_code < 0)
	{
	  errors_in_transaction++;
	  DB_ERR_MSG_SET (net_buf, err_code);
	  return -1;
	}
    }
  net_buf_cp_int (net_buf, 0, NULL);	/* result code */
  return 0;
}

int
ux_col_seq_drop (DB_COLLECTION * col, int index, T_NET_BUF * net_buf)
{
  int err_code = 0;

  if (col != NULL)
    {
      err_code = db_seq_drop (col, index - 1);
      if (err_code < 0)
	{
	  errors_in_transaction++;
	  DB_ERR_MSG_SET (net_buf, err_code);
	  return -1;
	}
    }
  net_buf_cp_int (net_buf, 0, NULL);	/* result code */
  return 0;
}

int
ux_col_seq_insert (DB_COLLECTION * col, int index, DB_VALUE * ele_val,
		   T_NET_BUF * net_buf)
{
  int err_code = 0;

  if (col != NULL)
    {
      err_code = db_seq_insert (col, index - 1, ele_val);
      if (err_code < 0)
	{
	  errors_in_transaction++;
	  DB_ERR_MSG_SET (net_buf, err_code);
	  return -1;
	}
    }
  net_buf_cp_int (net_buf, 0, NULL);	/* result code */
  return 0;
}

int
ux_col_seq_put (DB_COLLECTION * col, int index, DB_VALUE * ele_val,
		T_NET_BUF * net_buf)
{
  int err_code = 0;

  if (col != NULL)
    {
      err_code = db_seq_put (col, index - 1, ele_val);
      if (err_code < 0)
	{
	  errors_in_transaction++;
	  DB_ERR_MSG_SET (net_buf, err_code);
	  return -1;
	}
    }
  net_buf_cp_int (net_buf, 0, NULL);	/* result code */
  return 0;
}

int
ux_oid_put (int argc, void **argv, T_NET_BUF * net_buf)
{
  DB_OBJECT *obj;
  int err_code;
  char *attr_name;
  int name_size;
  char attr_type;
  DB_VALUE *attr_val = NULL;
  DB_OTMPL *otmpl;

  NET_ARG_GET_OBJECT (obj, argv[0]);
  if ((err_code = ux_check_object (obj, net_buf)) < 0)
    {
      if (err_code == CAS_ER_DBMS)
	goto oid_put_error2;
      else
	goto oid_put_error1;
    }

  argc--;
  argv++;

  otmpl = dbt_edit_object (obj);
  if (otmpl == NULL)
    {
      err_code = db_error_code ();
      DB_ERR_MSG_SET (net_buf, err_code);
      goto oid_put_error2;
    }

  for (; argc >= 3; argc -= 3, argv += 3)
    {
      NET_ARG_GET_STR (attr_name, name_size, argv[0]);
      if (name_size < 1)
	continue;

      attr_type = get_attr_type (obj, attr_name);

      err_code =
	make_bind_value (1, 2, argv + 1, &attr_val, net_buf, attr_type);
      if (err_code < 0)
	{
	  dbt_abort_object (otmpl);
	  if (err_code == CAS_ER_DBMS)
	    goto oid_put_error2;
	  else
	    goto oid_put_error1;
	}

      err_code = dbt_put (otmpl, attr_name, attr_val);
      if (err_code < 0)
	{
	  DB_ERR_MSG_SET (net_buf, err_code);
	  db_value_clear (attr_val);
	  FREE_MEM (attr_val);
	  dbt_abort_object (otmpl);
	  goto oid_put_error2;
	}

      db_value_clear (attr_val);
      FREE_MEM (attr_val);
    }

  obj = dbt_finish_object (otmpl);
  if (obj == NULL)
    {
      err_code = db_error_code ();
      DB_ERR_MSG_SET (net_buf, err_code);
      goto oid_put_error2;
    }

  net_buf_cp_int (net_buf, 0, NULL);

  obj = NULL;
  return 0;

oid_put_error1:
  net_buf_clear (net_buf);
  net_buf_cp_int (net_buf, err_code, NULL);
oid_put_error2:
  errors_in_transaction++;
  obj = NULL;
  return err_code;
}

void
db_err_msg_set (T_NET_BUF * net_buf, int err_code, const char *file, int line)
{
#if 0
  int err_code2;
#endif
  char *err_msg;

  if (err_code == -1)		/* might be connection error */
    {
      err_code = er_errid ();
    }
  err_msg = (char *) db_error_string (1);

  if (net_buf != NULL)
    {
      net_buf_error_msg_set (net_buf, err_code, err_msg, file, line);
      cas_log_debug (ARG_FILE_LINE,
		     "db_err_msg_set: err_code %d err_msg %s file %s line %d",
		     err_code, err_msg, file, line);
    }
#ifndef LIBCAS_FOR_JSP
  else if (err_code == ER_TM_SERVER_DOWN_UNILATERALLY_ABORTED)
    {
      is_server_aborted = true;
    }
#endif

  switch (err_code)
    {
    case -111:			/* ER_TM_SERVER_DOWN_UNILATERALLY_ABORTED */
    case -199:			/* ER_NET_SERVER_CRASHED */
    case -224:			/* ER_OBJ_NO_CONNECT */
      /*case -581: *//* ER_DB_NO_MODIFICATIONS */
#ifndef LIBCAS_FOR_JSP
      as_info->reset_flag = TRUE;
      cas_log_debug (ARG_FILE_LINE, "db_err_msg_set: set reset_flag");
#endif
      break;
    }
}

char
get_set_domain (DB_DOMAIN * set_domain, int *precision, short *scale,
		char *db_type)
{
  DB_DOMAIN *ele_domain;
  int set_domain_count = 0;
  int set_type = DB_TYPE_NULL;

  ele_domain = db_domain_set (set_domain);
  for (; ele_domain; ele_domain = db_domain_next (ele_domain))
    {
      set_domain_count++;
      set_type = db_domain_type (ele_domain);
      if (precision)
	*precision = db_domain_precision (ele_domain);
      if (scale)
	*scale = (short) db_domain_scale (ele_domain);
    }

  if (db_type)
    {
      *db_type = (set_domain_count != 1) ? DB_TYPE_NULL : set_type;
    }

  if (set_domain_count != 1)
    return 0;

  return (ux_db_type_to_cas_type (set_type));
}

int
make_bind_value (int num_bind, int argc, void **argv, DB_VALUE ** ret_val,
		 T_NET_BUF * net_buf, char desired_type)
{
  DB_VALUE *value_list = NULL;
  int i, type_idx, val_idx;
  int err_code;

  *ret_val = NULL;

  if (num_bind != (argc / 2))
    {
      return CAS_ER_NUM_BIND;
    }

  value_list = (DB_VALUE *) MALLOC (sizeof (DB_VALUE) * num_bind);
  if (value_list == NULL)
    {
      return CAS_ER_NO_MORE_MEMORY;
    }
  memset (value_list, 0, sizeof (DB_VALUE) * num_bind);
  for (i = 0; i < num_bind; i++)
    {
      type_idx = 2 * i;
      val_idx = 2 * i + 1;
      err_code =
	netval_to_dbval (argv[type_idx], argv[val_idx], &(value_list[i]),
			 net_buf, desired_type);
      if (err_code < 0)
	{
	  for (i--; i >= 0; i--)
	    {
	      db_value_clear (&(value_list[i]));
	    }
	  FREE_MEM (value_list);
	  return err_code;
	}
    }				/* end of for */

  *ret_val = value_list;

  return 0;
}

int
ux_get_attr_type_str (char *class_name, char *attr_name, T_NET_BUF * net_buf,
		      T_REQ_INFO * req_info)
{
  int err_code;
  DB_OBJECT *class_obj;
  DB_ATTRIBUTE *attribute;
  DB_DOMAIN *domain;
  char *domain_str = NULL;

  class_obj = db_find_class (class_name);
  if (class_obj == NULL)
    {
      errors_in_transaction++;
      err_code = db_error_code ();
      DB_ERR_MSG_SET (net_buf, err_code);
      return err_code;
    }
  attribute = db_get_attribute (class_obj, attr_name);
  if (attr_name == NULL)
    {
      errors_in_transaction++;
      err_code = db_error_code ();
      DB_ERR_MSG_SET (net_buf, err_code);
      return err_code;
    }

  domain = db_attribute_domain (attribute);
  if (domain == NULL)
    {
      errors_in_transaction++;
      err_code = db_error_code ();
      DB_ERR_MSG_SET (net_buf, err_code);
      return err_code;
    }

  net_buf_cp_int (net_buf, 0, NULL);	/* result code */

  domain_str = get_domain_str (domain);
  if (domain_str == NULL)
    {
      net_buf_cp_byte (net_buf, '\0');
    }
  else
    {
      net_buf_cp_str (net_buf, domain_str, strlen (domain_str) + 1);
      FREE_MEM (domain_str);
    }

  return 0;
}

int
ux_get_query_info (int srv_h_id, char info_type, T_NET_BUF * net_buf)
{
  T_SRV_HANDLE *srv_handle;
  int err_code;
  char *file_name;
  int fd;
  char read_buf[1024];
  int read_len;

  srv_handle = hm_find_srv_handle (srv_h_id);
  if (srv_handle == NULL)
    {
      errors_in_transaction++;
      err_code = CAS_ER_SRV_HANDLE;
      net_buf_clear (net_buf);
      net_buf_cp_int (net_buf, err_code, NULL);
      return err_code;
    }

  if (info_type == CAS_GET_QUERY_INFO_PLAN)
    file_name = cas_log_query_plan_file (srv_h_id);
  else if (info_type == CAS_GET_QUERY_INFO_HISTOGRAM)
    file_name = cas_log_query_histo_file (srv_h_id);
  else
    file_name = NULL;

  if (srv_handle->query_info_flag == FALSE)
    {
      file_name = NULL;
    }

  if (file_name)
    {
      fd = open (file_name, O_RDONLY);
    }

  net_buf_cp_int (net_buf, 0, NULL);	/* result code */

  if (file_name == NULL || fd < 0)
    {
      net_buf_cp_byte (net_buf, '\0');
      return 0;
    }

  while ((read_len = read (fd, read_buf, sizeof (read_buf))) > 0)
    {
      net_buf_cp_str (net_buf, read_buf, read_len);
    }
  net_buf_cp_byte (net_buf, '\0');
  close (fd);

  return 0;
}

int
ux_get_parameter_info (int srv_h_id, T_NET_BUF * net_buf)
{
  T_SRV_HANDLE *srv_handle;
  int err_code;
  DB_SESSION *session;
  int stmt_id, num_markers;
  DB_MARKER *param;
  DB_TYPE db_type;
  char cas_type, param_mode;
  int precision;
  short scale;
  int i;
  DB_DOMAIN *domain;

  srv_handle = hm_find_srv_handle (srv_h_id);
  if (srv_handle == NULL || srv_handle->schema_type >= CCI_SCH_FIRST)
    {
      err_code = CAS_ER_SRV_HANDLE;
      goto parameter_info_error1;
    }

  if (srv_handle->schema_type >= CCI_SCH_FIRST)
    {
      session = NULL;
      num_markers = 0;
      stmt_id = 0;
    }
  else
    {
      session = (DB_SESSION *) srv_handle->session;
      stmt_id = srv_handle->q_result->stmt_id;
      num_markers = srv_handle->num_markers;
    }

  param = NULL;
  if (session != NULL && stmt_id > 0)
    {
      param = db_get_input_markers (session, stmt_id);
    }

  net_buf_cp_int (net_buf, num_markers, NULL);	/* result code */

  for (i = 0; i < num_markers; i++)
    {
      domain = NULL;
      param_mode = CCI_PARAM_MODE_UNKNOWN;
      db_type = DB_TYPE_NULL;
      precision = 0;
      scale = 0;

      if (param != NULL)
	{
	  domain = db_marker_domain (param);
	  db_type = db_domain_type (domain);

	  if (db_type != DB_TYPE_NULL)
	    {
	      param_mode = CCI_PARAM_MODE_IN;
	      precision = db_domain_precision (domain);
	      scale = db_domain_scale (domain);
	    }

	  param = db_marker_next (param);
	}

      if (IS_SET_TYPE (db_type))
	{
	  char set_type;
	  set_type = get_set_domain (domain, NULL, NULL, NULL);
	  cas_type = CAS_TYPE_COLLECTION (db_type, set_type);
	}
      else
	{
	  cas_type = ux_db_type_to_cas_type (db_type);
	}

      net_buf_cp_byte (net_buf, param_mode);
      net_buf_cp_byte (net_buf, cas_type);
      net_buf_cp_short (net_buf, scale);
      net_buf_cp_int (net_buf, precision, NULL);
    }

  return 0;

parameter_info_error1:
  errors_in_transaction++;
  net_buf_clear (net_buf);
  net_buf_cp_int (net_buf, err_code, NULL);
  return err_code;
}

int
ux_glo_method_call (T_NET_BUF * net_buf, char check_ret, DB_OBJECT * glo_obj,
		    const char *method_name, DB_VALUE * ret_val,
		    DB_VALUE ** args)
{
  int err_code;
  int ret;

  err_code = db_send_argarray (glo_obj, method_name, ret_val, args);
  if (err_code < 0)
    {
      errors_in_transaction++;
      DB_ERR_MSG_SET (net_buf, err_code);
      return -1;
    }
  ret = db_get_int (ret_val);
  if ((check_ret) && (ret < 0))
    {
      errors_in_transaction++;
      db_value_clear (ret_val);
      if ((err_code = db_send (glo_obj, "get_error", ret_val)) < 0)
	{
	  DB_ERR_MSG_SET (net_buf, err_code);
	  return -1;
	}
      ret = db_get_int (ret_val);
      glo_err_msg_set (net_buf, ret, method_name);
      return -1;
    }
  return 0;
}

int
ux_check_object (DB_OBJECT * obj, T_NET_BUF * net_buf)
{
  int err_code;

  if (obj == NULL)
    {
      return CAS_ER_OBJECT;
    }

  er_clear ();

  if (db_is_instance (obj))
    {
      return 0;
    }

  err_code = db_error_code ();
  if (err_code < 0)
    {
      if (net_buf)
	{
	  DB_ERR_MSG_SET (net_buf, err_code);
	}
      return CAS_ER_DBMS;
    }

  return CAS_ER_OBJECT;
}

void
ux_free_result (void *res)
{
  db_query_end ((DB_QUERY_RESULT *) res);
}

char
ux_db_type_to_cas_type (int db_type)
{
  if (db_type < DB_TYPE_FIRST || db_type > DB_TYPE_LAST)
    return CCI_U_TYPE_NULL;

  return (cas_u_type[db_type]);
}

int
ux_schema_info (int schema_type, char *class_name, char *attr_name, char flag,
		T_NET_BUF * net_buf, T_REQ_INFO * req_info,
		unsigned int query_seq_num)
{
  int srv_h_id;
  int err_code = 0;
  T_SRV_HANDLE *srv_handle = NULL;
  T_BROKER_VERSION client_version = req_info->client_version;

  srv_h_id = hm_new_srv_handle (&srv_handle, query_seq_num);
  if (srv_h_id < 0)
    {
      err_code = srv_h_id;
      goto schema_info_error1;
    }
  srv_handle->schema_type = schema_type;

  net_buf_cp_int (net_buf, srv_h_id, NULL);	/* result code */

  switch (schema_type)
    {
    case CCI_SCH_CLASS:
      err_code =
	sch_class_info (net_buf, class_name, flag, 0, srv_handle,
			client_version);
      break;
    case CCI_SCH_VCLASS:
      err_code =
	sch_class_info (net_buf, class_name, flag, 1, srv_handle,
			client_version);
      break;
    case CCI_SCH_QUERY_SPEC:
      err_code = sch_queryspec (net_buf, class_name, srv_handle);
      break;
    case CCI_SCH_ATTRIBUTE:
      err_code =
	sch_attr_info (net_buf, class_name, attr_name, flag, 0, srv_handle);
      break;
    case CCI_SCH_CLASS_ATTRIBUTE:
      err_code =
	sch_attr_info (net_buf, class_name, attr_name, flag, 1, srv_handle);
      break;
    case CCI_SCH_METHOD:
      sch_method_info (net_buf, class_name, 0, &(srv_handle->session));
      break;
    case CCI_SCH_CLASS_METHOD:
      sch_method_info (net_buf, class_name, 1, &(srv_handle->session));
      break;
    case CCI_SCH_METHOD_FILE:
      sch_methfile_info (net_buf, class_name, &(srv_handle->session));
      break;
    case CCI_SCH_SUPERCLASS:
      err_code = sch_superclass (net_buf, class_name, 1, srv_handle);
      break;
    case CCI_SCH_SUBCLASS:
      err_code = sch_superclass (net_buf, class_name, 0, srv_handle);
      break;
    case CCI_SCH_CONSTRAINT:
      sch_constraint (net_buf, class_name, &(srv_handle->session));
      break;
    case CCI_SCH_TRIGGER:
      sch_trigger (net_buf, class_name, &(srv_handle->session));
      break;
    case CCI_SCH_CLASS_PRIVILEGE:
      err_code = sch_class_priv (net_buf, class_name, flag, srv_handle);
      break;
    case CCI_SCH_ATTR_PRIVILEGE:
      err_code =
	sch_attr_priv (net_buf, class_name, attr_name, flag, srv_handle);
      break;
    case CCI_SCH_DIRECT_SUPER_CLASS:
      err_code =
	sch_direct_super_class (net_buf, class_name, flag, srv_handle);
      break;
    case CCI_SCH_PRIMARY_KEY:
      err_code = sch_primary_key (net_buf);
      break;
    default:
      err_code = CAS_ER_SCHEMA_TYPE;
      goto schema_info_error1;
    }

  if (err_code < 0)
    {
      if (err_code == CAS_ER_DBMS)
	goto schema_info_error2;
      else
	goto schema_info_error1;
    }

  if (schema_type == CCI_SCH_CLASS
      || schema_type == CCI_SCH_VCLASS
      || schema_type == CCI_SCH_ATTRIBUTE
      || schema_type == CCI_SCH_CLASS_ATTRIBUTE
      || schema_type == CCI_SCH_QUERY_SPEC
      || schema_type == CCI_SCH_DIRECT_SUPER_CLASS)
    {
      srv_handle->cursor_pos = 0;
    }
  else
    {
      srv_handle->cur_result = srv_handle->session;
      if (srv_handle->cur_result == NULL)
	srv_handle->cursor_pos = 0;
      else
	srv_handle->cursor_pos = 1;
    }

  return srv_h_id;

schema_info_error1:
  net_buf_clear (net_buf);
  net_buf_cp_int (net_buf, err_code, NULL);

schema_info_error2:
  errors_in_transaction++;
  if (srv_handle)
    hm_srv_handle_free (srv_h_id);
  return err_code;
}

void
ux_prepare_call_info_free (T_PREPARE_CALL_INFO * call_info)
{
  if (call_info)
    {
      int i;

      prepare_call_info_dbval_clear (call_info);
      FREE_MEM (call_info->dbval_ret);
      for (i = 0; i < call_info->num_args; i++)
	{
	  FREE_MEM (((DB_VALUE **) call_info->dbval_args)[i]);
	}
      FREE_MEM (call_info->dbval_args);
      FREE_MEM (call_info->param_mode);
      FREE_MEM (call_info);
    }
}

void
ux_call_info_cp_param_mode (T_SRV_HANDLE * srv_handle, char *param_mode,
			    int num_param)
{
  T_PREPARE_CALL_INFO *call_info = srv_handle->prepare_call_info;

  if (call_info == NULL)
    return;

  memcpy (call_info->param_mode, param_mode,
	  MIN (num_param, call_info->num_args));
}

static void
prepare_column_info_set (T_NET_BUF * net_buf, char ut, short scale, int prec,
			 const char *col_name, const char *attr_name,
			 const char *class_name, char is_non_null,
			 T_BROKER_VERSION client_version)
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
    is_non_null = 1;
  else if (is_non_null < 0)
    is_non_null = 0;

  net_buf_cp_byte (net_buf, is_non_null);
}

static int
netval_to_dbval (void *net_type, void *net_value, DB_VALUE * out_val,
		 T_NET_BUF * net_buf, char desired_type)
{
  char type;
  int err_code = 0;
  int data_size;
  DB_VALUE db_val;
  char coercion_flag = TRUE;

  NET_ARG_GET_CHAR (type, net_type);

  if (type == CCI_U_TYPE_STRING || type == CCI_U_TYPE_CHAR)
    {
      if (desired_type == DB_TYPE_NUMERIC)
	{
	  type = CCI_U_TYPE_NUMERIC;
	}
      else if (desired_type == DB_TYPE_NCHAR
	       || desired_type == DB_TYPE_VARNCHAR)
	{
	  type = CCI_U_TYPE_NCHAR;
	}
    }

  if (type == CCI_U_TYPE_DATETIME)
    {
      if (desired_type == DB_TYPE_TIMESTAMP)
	type = CCI_U_TYPE_TIMESTAMP;
      else if (desired_type == DB_TYPE_DATE)
	type = CCI_U_TYPE_DATE;
      else if (desired_type == DB_TYPE_TIME)
	type = CCI_U_TYPE_TIME;
    }

  NET_ARG_GET_SIZE (data_size, net_value);
  if (data_size <= 0)
    {
      type = CCI_U_TYPE_NULL;
      data_size = 0;
    }

  switch (type)
    {
    case CCI_U_TYPE_NULL:
      err_code = db_make_null (&db_val);
      coercion_flag = FALSE;
      break;
    case CCI_U_TYPE_CHAR:
    case CCI_U_TYPE_STRING:
      {
	char *value;
	int val_size;
	NET_ARG_GET_STR (value, val_size, net_value);

	if (desired_type == DB_TYPE_OBJECT)
	  {
	    DB_OBJECT *obj_p;
	    obj_p = ux_str_to_obj (value);
	    if (obj_p == NULL)
	      {
		return CAS_ER_TYPE_CONVERSION;
	      }
	    err_code = db_make_object (&db_val, obj_p);
	    obj_p = NULL;
	    coercion_flag = FALSE;
	  }
	else
	  {
	    err_code = db_make_string (&db_val, value);
	  }
      }
      break;
    case CCI_U_TYPE_NCHAR:
    case CCI_U_TYPE_VARNCHAR:
      {
	char *value;
	int val_size;
	NET_ARG_GET_STR (value, val_size, net_value);
	val_size--;
	err_code = db_make_nchar (&db_val, val_size, value, val_size);
      }
      break;
    case CCI_U_TYPE_BIT:
    case CCI_U_TYPE_VARBIT:
      {
	char *value;
	int val_size;
	NET_ARG_GET_STR (value, val_size, net_value);
	err_code = db_make_bit (&db_val, val_size * 8, value, val_size * 8);
      }
      break;
    case CCI_U_TYPE_NUMERIC:
      {
	char *value, *p;
	int val_size;
	int precision, scale;
	char tmp[BUFSIZ];

	NET_ARG_GET_STR (value, val_size, net_value);
	if (value != NULL)
	  {
	    strcpy (tmp, value);
	  }
	tmp[val_size] = '\0';
	ut_trim (tmp);
	precision = strlen (tmp);
	p = strchr (tmp, '.');
	if (p == NULL)
	  {
	    scale = 0;
	  }
	else
	  {
	    scale = strlen (p + 1);
	    precision--;
	  }
	if (tmp[0] == '-')
	  {
	    precision--;
	  }

	err_code =
	  db_value_domain_init (&db_val, DB_TYPE_NUMERIC, precision, scale);
	if (err_code == 0)
	  {
	    err_code =
	      db_value_put (&db_val, DB_TYPE_C_CHAR, tmp, strlen (tmp));
	  }
      }
      coercion_flag = FALSE;
      break;
    case CCI_U_TYPE_BIGINT:
      {
	DB_BIGINT bi_val;

	NET_ARG_GET_BIGINT (bi_val, net_value);
	err_code = db_make_bigint (&db_val, bi_val);
      }
      break;
    case CCI_U_TYPE_INT:
      {
	int i_val;

	NET_ARG_GET_INT (i_val, net_value);
	err_code = db_make_int (&db_val, i_val);
      }
      break;
    case CCI_U_TYPE_SHORT:
      {
	short s_val;

	NET_ARG_GET_SHORT (s_val, net_value);
	err_code = db_make_short (&db_val, s_val);
      }
      break;
    case CCI_U_TYPE_MONETARY:
      {
	double d_val;
	NET_ARG_GET_DOUBLE (d_val, net_value);
	err_code =
	  db_make_monetary (&db_val, db_get_currency_default (), d_val);
      }
      break;
    case CCI_U_TYPE_FLOAT:
      {
	float f_val;
	NET_ARG_GET_FLOAT (f_val, net_value);
	err_code = db_make_float (&db_val, f_val);
      }
      break;
    case CCI_U_TYPE_DOUBLE:
      {
	double d_val;
	NET_ARG_GET_DOUBLE (d_val, net_value);
	err_code = db_make_double (&db_val, d_val);
      }
      break;
    case CCI_U_TYPE_DATE:
      {
	int month, day, year;
	NET_ARG_GET_DATE (year, month, day, net_value);
	err_code = db_make_date (&db_val, month, day, year);
      }
      break;
    case CCI_U_TYPE_TIME:
      {
	int hh, mm, ss;
	NET_ARG_GET_TIME (hh, mm, ss, net_value);
	err_code = db_make_time (&db_val, hh, mm, ss);
      }
      break;
    case CCI_U_TYPE_TIMESTAMP:
      {
	int yr, mon, day, hh, mm, ss;
	DB_DATE date;
	DB_TIME time;
	DB_TIMESTAMP ts;

	NET_ARG_GET_TIMESTAMP (yr, mon, day, hh, mm, ss, net_value);
	db_date_encode (&date, mon, day, yr);
	db_time_encode (&time, hh, mm, ss);
	db_timestamp_encode (&ts, &date, &time);
	err_code = db_make_timestamp (&db_val, ts);
      }
      break;
    case CCI_U_TYPE_DATETIME:
      {
	int yr, mon, day, hh, mm, ss, ms;
	DB_DATETIME dt;

	NET_ARG_GET_DATETIME (yr, mon, day, hh, mm, ss, ms, net_value);
	db_datetime_encode (&dt, mon, day, yr, hh, mm, ss, ms);
	err_code = db_make_datetime (&db_val, &dt);
      }
      break;
    case CCI_U_TYPE_SET:
    case CCI_U_TYPE_MULTISET:
    case CCI_U_TYPE_SEQUENCE:
      {
	char *cur_p;
	int remain_size = data_size;
	void *ele_type_arg = net_value;
	int ele_size;
	DB_VALUE ele_val;
	DB_SET *set = NULL;
	DB_SEQ *seq = NULL;
	int seq_index = 0;

	cur_p = ((char *) net_value) + 4;	/* data size */
	cur_p += 1;		/* element type */
	remain_size -= 1;

	if (type == CCI_U_TYPE_SET)
	  set = db_set_create_basic (NULL, NULL);
	else if (type == CCI_U_TYPE_MULTISET)
	  set = db_set_create_multi (NULL, NULL);
	else
	  seq = db_seq_create (NULL, NULL, 0);

	while (remain_size > 0)
	  {
	    NET_ARG_GET_SIZE (ele_size, cur_p);
	    if (ele_size + 4 > remain_size)
	      break;

	    ele_size =
	      netval_to_dbval (ele_type_arg, cur_p, &ele_val, net_buf,
			       desired_type);
	    if (ele_size < 0)
	      {
		return ele_size;
	      }
	    ele_size += 4;
	    cur_p += ele_size;
	    remain_size -= ele_size;

	    if (type == CCI_U_TYPE_SEQUENCE)
	      db_seq_insert (seq, seq_index++, &ele_val);
	    else
	      db_set_add (set, &ele_val);
	  }

	if (type == CCI_U_TYPE_SEQUENCE)
	  err_code = db_make_sequence (&db_val, seq);
	else
	  err_code = db_make_set (&db_val, set);

	type = CCI_U_TYPE_SET;
      }
      coercion_flag = FALSE;
      break;
    case CCI_U_TYPE_OBJECT:
      {
	DB_OBJECT *obj_p;
	NET_ARG_GET_OBJECT (obj_p, net_value);
	if (ux_check_object (obj_p, NULL) < 0)
	  err_code = db_make_null (&db_val);
	else
	  err_code = db_make_object (&db_val, obj_p);
      }
      coercion_flag = FALSE;
      break;
    default:
      return (CAS_ER_UNKNOWN_U_TYPE);
    }

  if (err_code < 0)
    {
      DB_ERR_MSG_SET (net_buf, err_code);
      return CAS_ER_DBMS;
    }

  if (desired_type == DB_TYPE_NULL || coercion_flag == FALSE)
    {
      db_value_clone (&db_val, out_val);
    }
  else
    {
      DB_DOMAIN *domain;

      domain = db_type_to_db_domain ((DB_TYPE) desired_type);
      err_code = db_value_coerce (&db_val, out_val, domain);
      if (err_code < 0)
	{
	  DB_ERR_MSG_SET (net_buf, err_code);
	  return CAS_ER_DBMS;
	}
    }

  db_value_clear (&db_val);

  return data_size;
}

static int
cur_tuple (T_QUERY_RESULT * q_result, int max_col_size, char sensitive_flag,
	   DB_OBJECT * tuple_obj, T_NET_BUF * net_buf)
{
  int ncols;
  DB_VALUE val;
  int i;
  int error;
  int data_size = 0;
  DB_QUERY_RESULT *result = (DB_QUERY_RESULT *) q_result->result;
  T_COL_UPDATE_INFO *col_update_info = q_result->col_update_info;
  char *null_type_column = q_result->null_type_column;

  ncols = db_query_column_count (result);
  for (i = 0; i < ncols; i++)
    {
      if (sensitive_flag == TRUE && col_update_info[i].updatable == TRUE)
	{
	  if (tuple_obj == NULL)
	    {
	      error = db_make_null (&val);
	    }
	  else
	    {
	      error = db_get (tuple_obj, col_update_info[i].attr_name, &val);
	    }
	}
      else
	{
	  error = db_query_get_tuple_value (result, i, &val);
	}
      if (error < 0)
	{
	  DB_ERR_MSG_SET (net_buf, error);
	  tuple_obj = NULL;
	  return -1;
	}

      data_size += dbval_to_net_buf (&val, net_buf, 1, max_col_size,
				     null_type_column ? null_type_column[i] :
				     0);
      db_value_clear (&val);
    }

  tuple_obj = NULL;
  return data_size;
}

static int
dbval_to_net_buf (DB_VALUE * val, T_NET_BUF * net_buf, char fetch_flag,
		  int max_col_size, char column_type_flag)
{
  int data_size;
  char col_type;

  if (db_value_is_null (val) == true)
    {
      net_buf_cp_int (net_buf, -1, NULL);
      return 4;
    }

  if (column_type_flag)
    col_type = ux_db_type_to_cas_type (db_value_type (val));
  else
    col_type = 0;

  switch (db_value_type (val))
    {
    case DB_TYPE_OBJECT:
      {
	DB_OBJECT *obj;
	T_OBJECT cas_obj;

	obj = db_get_object (val);
	uobj_to_cas_obj (obj, &cas_obj);
	add_res_data_object (net_buf, &cas_obj, col_type);
	data_size = SIZE_OBJECT + 4 + ((col_type) ? 1 : 0);
      }
      break;
    case DB_TYPE_VARBIT:
    case DB_TYPE_BIT:
      {
	DB_C_BIT bit;
	int length = 0;

	bit = db_get_bit (val, &length);
	length = (length + 7) / 8;
	if (max_col_size > 0)
	  length = MIN (length, max_col_size);
	/* do not append NULL terminator */
	add_res_data_bytes (net_buf, bit, length, col_type);
	data_size = length + 4 + ((col_type) ? 1 : 0);
      }
      break;
    case DB_TYPE_VARCHAR:
    case DB_TYPE_CHAR:
      {
	DB_C_CHAR str;
	int length = 0;

	str = db_get_char (val, &length);
	if (max_col_size > 0)
	  {
	    length = MIN (length, max_col_size);
	  }
	add_res_data_string (net_buf, str, length, col_type);
	data_size = 4 + length + 1 + ((col_type) ? 1 : 0);
      }
      break;
    case DB_TYPE_VARNCHAR:
    case DB_TYPE_NCHAR:
      {
	DB_C_NCHAR nchar;
	int length = 0;

	nchar = db_get_nchar (val, &length);
	if (max_col_size > 0)
	  {
	    length = MIN (length, max_col_size);
	  }
	add_res_data_string (net_buf, nchar, length, col_type);
	data_size = 4 + length + 1 + ((col_type) ? 1 : 0);
      }
      break;
    case DB_TYPE_SMALLINT:
      {
	short smallint;
	smallint = db_get_short (val);
	add_res_data_short (net_buf, smallint, col_type);
	data_size = 4 + 2 + ((col_type) ? 1 : 0);
      }
      break;
    case DB_TYPE_INTEGER:
      {
	int int_val;
	int_val = db_get_int (val);
	add_res_data_int (net_buf, int_val, col_type);
	data_size = 4 + 4 + ((col_type) ? 1 : 0);
      }
      break;
    case DB_TYPE_BIGINT:
      {
	DB_BIGINT bigint_val;
	bigint_val = db_get_bigint (val);
	add_res_data_bigint (net_buf, bigint_val, col_type);
	data_size = 4 + 8 + ((col_type) ? 1 : 0);
      }
      break;
    case DB_TYPE_DOUBLE:
      {
	double d_val;
	d_val = db_get_double (val);
	add_res_data_double (net_buf, d_val, col_type);
	data_size = 4 + 8 + ((col_type) ? 1 : 0);
      }
      break;
    case DB_TYPE_MONETARY:
      {
	double d_val;
	d_val = db_value_get_monetary_amount_as_double (val);
	add_res_data_double (net_buf, d_val, col_type);
	data_size = 4 + 8 + ((col_type) ? 1 : 0);
      }
      break;
    case DB_TYPE_FLOAT:
      {
	float f_val;
	f_val = db_get_float (val);
	add_res_data_float (net_buf, f_val, col_type);
	data_size = 4 + 4 + ((col_type) ? 1 : 0);
      }
      break;
    case DB_TYPE_DATE:
      {
	DB_DATE *db_date;
	int yr, mon, day;
	db_date = db_get_date (val);
	db_date_decode (db_date, &mon, &day, &yr);
	add_res_data_date (net_buf, (short) yr, (short) mon, (short) day,
			   col_type);
	data_size = 4 + SIZE_DATE + ((col_type) ? 1 : 0);
      }
      break;
    case DB_TYPE_TIME:
      {
	DB_DATE *time;
	int hour, minute, second;
	time = db_get_time (val);
	db_time_decode (time, &hour, &minute, &second);
	add_res_data_time (net_buf, (short) hour, (short) minute,
			   (short) second, col_type);
	data_size = 4 + SIZE_TIME + ((col_type) ? 1 : 0);
      }
      break;
    case DB_TYPE_TIMESTAMP:
      {
	DB_TIMESTAMP *ts;
	DB_DATE date;
	DB_TIME time;
	int yr, mon, day, hh, mm, ss;
	ts = db_get_timestamp (val);
	db_timestamp_decode (ts, &date, &time);
	db_date_decode (&date, &mon, &day, &yr);
	db_time_decode (&time, &hh, &mm, &ss);
	add_res_data_timestamp (net_buf, (short) yr, (short) mon, (short) day,
				(short) hh, (short) mm, (short) ss, col_type);
	data_size = 4 + CAS_TIMESTAMP_SIZE + ((col_type) ? 1 : 0);
      }
      break;
    case DB_TYPE_DATETIME:
      {
	DB_DATETIME *dt;
	int yr, mon, day, hh, mm, ss, ms;
	dt = db_get_datetime (val);
	db_datetime_decode (dt, &mon, &day, &yr, &hh, &mm, &ss, &ms);
	add_res_data_datetime (net_buf, (short) yr, (short) mon, (short) day,
			       (short) hh, (short) mm, (short) ss, (short) ms,
			       col_type);
	data_size = 4 + CAS_DATETIME_SIZE + ((col_type) ? 1 : 0);
      }
      break;
    case DB_TYPE_NUMERIC:
      {
	DB_DOMAIN *char_domain;
	DB_VALUE v;
	char *str;
	int len, err;
	char buf[128];

	char_domain = db_type_to_db_domain (DB_TYPE_VARCHAR);
	err = db_value_coerce (val, &v, char_domain);
	if (err < 0)
	  {
	    net_buf_cp_int (net_buf, -1, NULL);
	    data_size = 4;
	  }
	else
	  {
	    str = db_get_char (&v, &len);
	    if (str != NULL)
	      {
		strncpy (buf, str, sizeof (buf) - 1);
		buf[sizeof (buf) - 1] = '\0';
		ut_trim (buf);
		add_res_data_string (net_buf, buf, strlen (buf), col_type);
		data_size = 4 + strlen (buf) + 1 + ((col_type) ? 1 : 0);
	      }
	    else
	      {
		net_buf_cp_int (net_buf, -1, NULL);
		data_size = 4;
	      }
	    db_value_clear (&v);
	  }
      }
      break;
    case DB_TYPE_LIST:
    case DB_TYPE_MULTISET:
    case DB_TYPE_SET:
      {
	DB_SET *set;
	int i;
	DB_VALUE *element;
	int num_element;
	char cas_type = CCI_U_TYPE_NULL;
	char err_flag = 0;

	set = db_get_set (val);

	num_element = db_set_size (set);
	element = (DB_VALUE *) malloc (sizeof (DB_VALUE) * num_element);
	if (element == NULL)
	  {
	    err_flag = 1;
	  }

	if (!err_flag)
	  {
	    if (num_element <= 0)
	      {
		DB_DOMAIN *set_domain;
		char element_type;
		set_domain = db_col_domain (set);
		element_type = get_set_domain (set_domain, NULL, NULL, NULL);
		if (element_type > 0)
		  {
		    cas_type = element_type;
		  }
	      }
	    else
	      {
		for (i = 0; i < num_element; i++)
		  {
		    db_set_get (set, i, &(element[i]));
		    if (i == 0 || cas_type == CCI_U_TYPE_NULL)
		      {
			cas_type =
			  ux_db_type_to_cas_type (db_value_type
						  (&(element[i])));
		      }
		    else
		      {
			char tmp_type;
			tmp_type =
			  ux_db_type_to_cas_type (db_value_type
						  (&(element[i])));
			if (db_value_is_null (&(element[i])) == false
			    && cas_type != tmp_type)
			  {
			    err_flag = 1;
			    break;
			  }
		      }
		  }		/* end of for */
	      }

	    if ((err_flag) && (element != NULL))
	      {
		for (; i >= 0; i--)
		  {
		    db_value_clear (&(element[i]));
		  }
		FREE_MEM (element);
	      }
	  }

	if (err_flag)
	  {
	    net_buf_cp_int (net_buf, -1, NULL);
	    data_size = 4;
	  }
	else
	  {
	    int set_data_size;
	    int set_size_msg_offset;

	    set_data_size = 0;
	    net_buf_cp_int (net_buf, set_data_size, &set_size_msg_offset);

	    if (col_type)
	      {
		net_buf_cp_byte (net_buf, col_type);
		set_data_size++;
	      }

	    net_buf_cp_byte (net_buf, cas_type);
	    set_data_size++;

	    net_buf_cp_int (net_buf, num_element, NULL);
	    set_data_size += 4;

	    for (i = 0; i < num_element; i++)
	      {
		set_data_size +=
		  dbval_to_net_buf (&(element[i]), net_buf, 1, max_col_size,
				    0);
		db_value_clear (&(element[i]));
	      }
	    FREE_MEM (element);

	    net_buf_overwrite_int (net_buf, set_size_msg_offset,
				   set_data_size);
	    data_size = 4 + set_data_size;
	  }

	if (fetch_flag)
	  db_set_free (set);
      }
      break;

    case DB_TYPE_RESULTSET:
      {
	int h_id;

	h_id = db_get_resultset (val);
	add_res_data_int (net_buf, h_id, col_type);
	data_size = 4 + 4 + ((col_type) ? 1 : 0);
      }
      break;

    default:
      net_buf_cp_int (net_buf, -1, NULL);	/* null */
      data_size = 4;
      break;
    }

  return data_size;
}

static void
uobj_to_cas_obj (DB_OBJECT * obj, T_OBJECT * cas_obj)
{
  DB_IDENTIFIER *oid;

  oid = db_identifier (obj);

  if (oid == NULL)
    {
      cas_obj->pageid = 0;
      cas_obj->volid = 0;
      cas_obj->slotid = 0;
    }
  else
    {
      cas_obj->pageid = oid->pageid;
      cas_obj->volid = oid->volid;
      cas_obj->slotid = oid->slotid;
    }
}

static int
get_attr_name (DB_OBJECT * obj, char ***ret_attr_name)
{
  DB_ATTRIBUTE *attributes, *att;
  char **attr_name = NULL;
  int num_attr;
  int alloc_num;

  attributes = db_get_attributes (obj);

  alloc_num = 100;
  attr_name = (char **) MALLOC (sizeof (char *) * alloc_num);
  if (attr_name == NULL)
    {
      return CAS_ER_NO_MORE_MEMORY;
    }

  for (num_attr = 0, att = attributes; att; att = db_attribute_next (att))
    {
      if (num_attr >= alloc_num)
	{
	  alloc_num += 100;
	  attr_name =
	    (char **) REALLOC (attr_name, sizeof (char *) * alloc_num);
	  if (attr_name == NULL)
	    {
	      return CAS_ER_NO_MORE_MEMORY;
	    }
	}
      attr_name[num_attr] = (char *) db_attribute_name (att);
      num_attr++;
    }

  *ret_attr_name = attr_name;
  return num_attr;
}

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

      NET_ARG_GET_STR (tmp_p, name_size, argv[i + 1]);
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

static int
oid_attr_info_set (T_NET_BUF * net_buf, DB_OBJECT * obj, int attr_num,
		   char **attr_name)
{
  DB_ATTRIBUTE *attr = NULL;
  DB_DOMAIN *domain;
  int i;
  int db_type;
  char set_type, cas_type;
  int precision;
  short scale;
  char *p;

  for (i = 0; i < attr_num; i++)
    {
      p = strrchr (attr_name[i], '.');
      if (p == NULL)
	{
	  attr = db_get_attribute (obj, attr_name[i]);
	  if (attr == NULL)
	    {
	      DB_ERR_MSG_SET (net_buf, db_error_code ());
	      return CAS_ER_DBMS;
	    }
	}
      else
	{
	  DB_VALUE path_val;
	  int err_code;
	  DB_OBJECT *path_obj;
	  *p = '\0';
	  err_code = db_get (obj, attr_name[i], &path_val);
	  if (err_code < 0 || db_value_is_null (&path_val) == true)
	    {
	      attr = NULL;
	    }
	  else
	    {
	      path_obj = db_get_object (&path_val);
	      attr = db_get_attribute (path_obj, p + 1);
	      if (attr == NULL)
		{
		  DB_ERR_MSG_SET (net_buf, db_error_code ());
		  return CAS_ER_DBMS;
		}
	    }
	  *p = '.';
	  path_obj = NULL;
	  db_value_clear (&path_val);
	}

      if (attr == NULL)
	{
	  cas_type = CCI_U_TYPE_NULL;
	  scale = 0;
	  precision = 0;
	}
      else
	{
	  domain = db_attribute_domain (attr);
	  db_type = db_domain_type (domain);
	  if (IS_SET_TYPE (db_type))
	    {
	      set_type = get_set_domain (domain, &precision, &scale, NULL);
	      cas_type = CAS_TYPE_COLLECTION (db_type, set_type);
	    }
	  else
	    {
	      cas_type = ux_db_type_to_cas_type (db_type);
	      precision = db_domain_precision (domain);
	      scale = (short) db_domain_scale (domain);
	    }
	}

      net_buf_column_info_set (net_buf, (char) cas_type,
			       scale, precision, attr_name[i]);
    }

  return 0;
}

static int
oid_data_set (T_NET_BUF * net_buf, DB_OBJECT * obj, int attr_num,
	      char **attr_name)
{
  int err_code;
  int i;
  DB_VALUE val;

  for (i = 0; i < attr_num; i++)
    {
      err_code = db_get (obj, attr_name[i], &val);
      if (err_code < 0)
	{
	  db_make_null (&val);
	}
      dbval_to_net_buf (&val, net_buf, 1, 0, 0);
      db_value_clear (&val);
    }

  return 0;
}

static int
fetch_result (T_SRV_HANDLE * srv_handle, int cursor_pos, int fetch_count,
	      char fetch_flag, int result_set_idx, T_NET_BUF * net_buf,
	      T_REQ_INFO * req_info)
{
  T_OBJECT tuple_obj;
  int err_code;
  int num_tuple_msg_offset;
  int num_tuple;
  DB_QUERY_RESULT *result;
  T_QUERY_RESULT *q_result;
  char sensitive_flag = fetch_flag & CCI_FETCH_SENSITIVE;
  DB_OBJECT *db_obj;

  if (result_set_idx <= 0)
    {
      q_result = (T_QUERY_RESULT *) (srv_handle->cur_result);
      if (q_result == NULL)
	{
	  return CAS_ER_NO_MORE_DATA;
	}
    }
  else
    {
      if (result_set_idx > srv_handle->cur_result_index)
	return CAS_ER_NO_MORE_RESULT_SET;
      q_result = srv_handle->q_result + (result_set_idx - 1);
    }

  if ((sensitive_flag) && (q_result->col_updatable == TRUE)
      && (q_result->col_update_info != NULL))
    {
      sensitive_flag = TRUE;
      db_synchronize_cache ();
    }
  else
    {
      sensitive_flag = FALSE;
    }

  result = (DB_QUERY_RESULT *) q_result->result;
  if ((result == NULL)
      || (q_result->stmt_type != CUBRID_STMT_SELECT
	  && q_result->stmt_type != CUBRID_STMT_GET_STATS
	  && q_result->stmt_type != CUBRID_STMT_CALL
	  && q_result->stmt_type != CUBRID_STMT_EVALUATE))
    {
      return CAS_ER_NO_MORE_DATA;
    }

  if (srv_handle->cursor_pos != cursor_pos)
    {
      if (cursor_pos == 1)
	err_code = db_query_first_tuple (result);
      else
	err_code = db_query_seek_tuple (result, cursor_pos - 1, 1);
      if (err_code == DB_CURSOR_SUCCESS)
	{
	  net_buf_cp_int (net_buf, 0, &num_tuple_msg_offset);	/* tuple num */
	}
      else if (err_code == DB_CURSOR_END)
	{
	  net_buf_cp_int (net_buf, 0, NULL);

#ifndef LIBCAS_FOR_JSP
	  if (srv_handle->num_q_result < 2
	      && (srv_handle->q_result->stmt_type == CUBRID_STMT_SELECT ||
		  srv_handle->q_result->stmt_type == CUBRID_STMT_CALL ||
		  srv_handle->q_result->stmt_type == CUBRID_STMT_GET_STATS ||
		  srv_handle->q_result->stmt_type == CUBRID_STMT_EVALUATE)
	      && srv_handle->auto_commit_mode == TRUE
	      && srv_handle->forward_only_cursor == TRUE)
	    {
	      req_info->need_auto_commit = TRAN_AUTOCOMMIT;
	    }
#endif
	  return 0;
	}
      else
	{
	  DB_ERR_MSG_SET (net_buf, err_code);
	  return CAS_ER_DBMS;
	}
    }
  else
    {
      net_buf_cp_int (net_buf, 0, &num_tuple_msg_offset);
    }

  num_tuple = 0;
  while (CHECK_NET_BUF_SIZE (net_buf))
    {				/* currently, don't check fetch_count */
      memset ((char *) &tuple_obj, 0, sizeof (T_OBJECT));

      net_buf_cp_int (net_buf, cursor_pos, NULL);

      db_obj = NULL;

      if (q_result->include_oid)
	{
	  DB_VALUE oid_val;

	  er_clear ();

	  if (db_query_get_tuple_oid (result, &oid_val) >= 0)
	    {
	      if (db_value_type (&oid_val) == DB_TYPE_OBJECT)
		{
		  db_obj = db_get_object (&oid_val);
		  if (db_is_instance (db_obj))
		    {
		      uobj_to_cas_obj (db_obj, &tuple_obj);
		    }
		  else if (db_error_code () == 0 || db_error_code () == -48)
		    {
		      memset ((char *) &tuple_obj, 0xff, sizeof (T_OBJECT));
		      db_obj = NULL;
		    }
		  else
		    {
		      DB_ERR_MSG_SET (net_buf, db_error_code ());
		      return CAS_ER_DBMS;
		    }
		}
	      db_value_clear (&oid_val);
	    }
	}

      NET_BUF_CP_OBJECT (net_buf, &tuple_obj);

      if (cur_tuple
	  (q_result, srv_handle->max_col_size, sensitive_flag, db_obj,
	   net_buf) < 0)
	{
	  return CAS_ER_DBMS;
	}

      num_tuple++;
      cursor_pos++;
      if (srv_handle->max_row > 0 && cursor_pos > srv_handle->max_row)
	{
#ifndef LIBCAS_FOR_JSP
	  if (srv_handle->num_q_result < 2
	      && (srv_handle->q_result->stmt_type == CUBRID_STMT_SELECT ||
		  srv_handle->q_result->stmt_type == CUBRID_STMT_CALL ||
		  srv_handle->q_result->stmt_type == CUBRID_STMT_GET_STATS ||
		  srv_handle->q_result->stmt_type == CUBRID_STMT_EVALUATE)
	      && srv_handle->auto_commit_mode == TRUE
	      && srv_handle->forward_only_cursor == TRUE)
	    {
	      req_info->need_auto_commit = TRAN_AUTOCOMMIT;
	    }
#endif
	  break;
	}
      err_code = db_query_next_tuple (result);
      if (err_code == DB_CURSOR_SUCCESS)
	{
	}
      else if (err_code == DB_CURSOR_END)
	{
#ifndef LIBCAS_FOR_JSP
	  if (srv_handle->num_q_result < 2
	      && (srv_handle->q_result->stmt_type == CUBRID_STMT_SELECT ||
		  srv_handle->q_result->stmt_type == CUBRID_STMT_CALL ||
		  srv_handle->q_result->stmt_type == CUBRID_STMT_GET_STATS ||
		  srv_handle->q_result->stmt_type == CUBRID_STMT_EVALUATE)
	      && srv_handle->auto_commit_mode == TRUE
	      && srv_handle->forward_only_cursor == TRUE)
	    {
	      req_info->need_auto_commit = TRAN_AUTOCOMMIT;
	    }
#endif
	  break;
	}
      else
	{
	  DB_ERR_MSG_SET (net_buf, err_code);
	  return CAS_ER_DBMS;
	}
    }
  net_buf_overwrite_int (net_buf, num_tuple_msg_offset, num_tuple);

  srv_handle->cursor_pos = cursor_pos;

  db_obj = NULL;
  return 0;
}

static int
fetch_class (T_SRV_HANDLE * srv_handle, int cursor_pos, int fetch_count,
	     char fetch_flag, int result_set_idx, T_NET_BUF * net_buf,
	     T_REQ_INFO * req_info)
{
  T_OBJECT dummy_obj;
  int tuple_num, tuple_num_msg_offset;
  int i;
  int num_result;
  T_CLASS_TABLE *class_table;

  class_table = (T_CLASS_TABLE *) (srv_handle->session);
  if (class_table == NULL)
    return CAS_ER_NO_MORE_DATA;
  num_result = srv_handle->sch_tuple_num;

  memset (&dummy_obj, 0, sizeof (T_OBJECT));

  if (num_result < cursor_pos)
    return CAS_ER_NO_MORE_DATA;

  tuple_num = 0;
  net_buf_cp_int (net_buf, tuple_num, &tuple_num_msg_offset);
  for (i = 0; (i < fetch_count) && (cursor_pos <= num_result); i++)
    {
      char *p;

      net_buf_cp_int (net_buf, cursor_pos, NULL);
      NET_BUF_CP_OBJECT (net_buf, &dummy_obj);

      /* 1. name */
      p = class_table[cursor_pos - 1].class_name;
      add_res_data_string (net_buf, p, strlen (p), 0);

      /* 2. type */
      add_res_data_short (net_buf, class_table[cursor_pos - 1].class_type, 0);

      tuple_num++;
      cursor_pos++;
    }
  net_buf_overwrite_int (net_buf, tuple_num_msg_offset, tuple_num);

  return 0;
}

static int
fetch_attribute (T_SRV_HANDLE * srv_handle, int cursor_pos, int fetch_count,
		 char fetch_flag, int result_set_idx, T_NET_BUF * net_buf,
		 T_REQ_INFO * req_info)
{
  T_OBJECT tuple_obj;
  int err_code;
  int num_tuple_msg_offset;
  int num_tuple;
  int i;
  DB_QUERY_RESULT *result;
  T_QUERY_RESULT *q_result;
  DB_VALUE val_class, val_attr;
  DB_OBJECT *class_obj;
  DB_ATTRIBUTE *db_attr;
  char *class_name, *attr_name, *p;
  T_ATTR_TABLE attr_info;

  q_result = (T_QUERY_RESULT *) (srv_handle->cur_result);
  if (q_result == NULL)
    {
      return CAS_ER_NO_MORE_DATA;
    }

  result = (DB_QUERY_RESULT *) q_result->result;
  if (result == NULL || q_result->stmt_type != CUBRID_STMT_SELECT)
    {
      return CAS_ER_NO_MORE_DATA;
    }

  if (srv_handle->cursor_pos != cursor_pos)
    {
      err_code = db_query_seek_tuple (result, cursor_pos - 1, 1);
      if (err_code == DB_CURSOR_SUCCESS)
	{
	  net_buf_cp_int (net_buf, 0, &num_tuple_msg_offset);	/* tuple num */
	}
      else if (err_code == DB_CURSOR_END)
	{
	  net_buf_cp_int (net_buf, 0, NULL);
	  return 0;
	}
      else
	{
	  DB_ERR_MSG_SET (net_buf, err_code);
	  return CAS_ER_DBMS;
	}
    }
  else
    {
      net_buf_cp_int (net_buf, 0, &num_tuple_msg_offset);
    }

  memset ((char *) &tuple_obj, 0, sizeof (T_OBJECT));
  num_tuple = 0;
  for (i = 0; i < fetch_count; i++)
    {
      net_buf_cp_int (net_buf, cursor_pos, NULL);
      NET_BUF_CP_OBJECT (net_buf, &tuple_obj);

      err_code = db_query_get_tuple_value (result, 0, &val_class);
      if (err_code < 0)
	{
	  DB_ERR_MSG_SET (net_buf, err_code);
	  return CAS_ER_DBMS;
	}
      class_name = db_get_string (&val_class);
      class_obj = db_find_class (class_name);
      if (class_obj == NULL)
	{
	  DB_ERR_MSG_SET (net_buf, db_error_code ());
	  return CAS_ER_DBMS;
	}

      err_code = db_query_get_tuple_value (result, 1, &val_attr);
      if (err_code < 0)
	{
	  DB_ERR_MSG_SET (net_buf, err_code);
	  return CAS_ER_DBMS;
	}
      attr_name = db_get_string (&val_attr);
      if (srv_handle->schema_type == CCI_SCH_CLASS_ATTRIBUTE)
	db_attr = db_get_class_attribute (class_obj, attr_name);
      else
	db_attr = db_get_attribute (class_obj, attr_name);
      if (db_attr == NULL)
	{
	  DB_ERR_MSG_SET (net_buf, db_error_code ());
	  return CAS_ER_DBMS;
	}

      memset (&attr_info, 0, sizeof (attr_info));
      class_attr_info (class_name, db_attr, NULL, 0, &attr_info);

      /* 1. attr name */
      p = attr_info.attr_name;
      add_res_data_string (net_buf, p, strlen (p), 0);

      /* 2. domain */
      add_res_data_short (net_buf, (short) attr_info.domain, 0);

      /* 3. scale */
      add_res_data_short (net_buf, (short) attr_info.scale, 0);

      /* 4. precision */
      add_res_data_int (net_buf, attr_info.precision, 0);

      /* 5. indexed */
      add_res_data_short (net_buf, (short) attr_info.indexed, 0);

      /* 6. non_null */
      add_res_data_short (net_buf, (short) attr_info.non_null, 0);

      /* 7. shared */
      add_res_data_short (net_buf, (short) attr_info.shared, 0);

      /* 8. unique */
      add_res_data_short (net_buf, (short) attr_info.unique, 0);

      /* 9. default */
      dbval_to_net_buf ((DB_VALUE *) attr_info.default_val, net_buf, 0, 0, 1);

      /* 10. order */
      add_res_data_int (net_buf, (int) attr_info.attr_order, 0);

      /* 11. class name */
      p = attr_info.class_name;
      if (p == NULL)
	add_res_data_string (net_buf, "", 0, 0);
      else
	add_res_data_string (net_buf, p, strlen (p), 0);

      /* 12. source class */
      p = attr_info.source_class;
      if (p == NULL)
	add_res_data_string (net_buf, "", 0, 0);
      else
	add_res_data_string (net_buf, p, strlen (p), 0);

      /* 13. is_key */
      add_res_data_short (net_buf, (short) attr_info.is_key, 0);

      db_value_clear (&val_class);
      db_value_clear (&val_attr);

      num_tuple++;
      cursor_pos++;
      err_code = db_query_next_tuple (result);
      if (err_code == DB_CURSOR_SUCCESS)
	{
	}
      else if (err_code == DB_CURSOR_END)
	{
	  break;
	}
      else
	{
	  DB_ERR_MSG_SET (net_buf, err_code);
	  return CAS_ER_DBMS;
	}
    }
  net_buf_overwrite_int (net_buf, num_tuple_msg_offset, num_tuple);

  srv_handle->cursor_pos = cursor_pos;

  return 0;
}

static int
fetch_method (T_SRV_HANDLE * srv_handle, int cursor_pos, int fetch_count,
	      char fetch_flag, int result_set_idx, T_NET_BUF * net_buf,
	      T_REQ_INFO * req_info)
{
  T_OBJECT dummy_obj;
  DB_METHOD *tmp_p;
  int tuple_num, tuple_num_msg_offset;
  int i, j;
  DB_DOMAIN *domain;
  char *name;
  int db_type;
  char arg_str[128];
  int num_args;

  memset (&dummy_obj, 0, sizeof (T_OBJECT));

  if (srv_handle->cursor_pos != cursor_pos)
    {
      tmp_p = (DB_METHOD *) (srv_handle->session);
      for (i = 1; (i < cursor_pos) && (tmp_p); i++)
	{
	  tmp_p = db_method_next (tmp_p);
	}
      if (i != cursor_pos)
	return CAS_ER_NO_MORE_DATA;
    }
  else
    tmp_p = (DB_METHOD *) (srv_handle->cur_result);

  tuple_num = 0;
  net_buf_cp_int (net_buf, tuple_num, &tuple_num_msg_offset);

  for (i = 0; (tmp_p) && (i < fetch_count); i++)
    {
      char set_type, cas_type;

      net_buf_cp_int (net_buf, cursor_pos, NULL);
      NET_BUF_CP_OBJECT (net_buf, &dummy_obj);

      /* 1. name */
      name = (char *) db_method_name (tmp_p);
      add_res_data_string (net_buf, name, strlen (name), 0);

      /* 2. ret domain */
      domain = db_method_arg_domain (tmp_p, 0);
      db_type = db_domain_type (domain);
      if (IS_SET_TYPE (db_type))
	{
	  set_type = get_set_domain (domain, NULL, NULL, NULL);
	  cas_type = CAS_TYPE_COLLECTION (db_type, set_type);
	}
      else
	{
	  cas_type = ux_db_type_to_cas_type (db_type);
	}
      add_res_data_short (net_buf, (short) cas_type, 0);

      /* 3. arg domain */
      arg_str[0] = '\0';
      num_args = db_method_arg_count (tmp_p);
      for (j = 1; j <= num_args; j++)
	{
	  domain = db_method_arg_domain (tmp_p, j);
	  db_type = db_domain_type (domain);
	  if (IS_SET_TYPE (db_type))
	    {
	      set_type = get_set_domain (domain, NULL, NULL, NULL);
	      cas_type = CAS_TYPE_COLLECTION (db_type, set_type);
	    }
	  else
	    {
	      cas_type = ux_db_type_to_cas_type (db_type);
	    }
	  sprintf (arg_str, "%s%d ", arg_str, cas_type);
	}
      add_res_data_string (net_buf, arg_str, strlen (arg_str), 0);

      tuple_num++;
      cursor_pos++;

      tmp_p = db_method_next (tmp_p);
    }
  net_buf_overwrite_int (net_buf, tuple_num_msg_offset, tuple_num);

  srv_handle->cur_result = (void *) tmp_p;
  srv_handle->cursor_pos = cursor_pos;

  return 0;
}

static int
fetch_methfile (T_SRV_HANDLE * srv_handle, int cursor_pos, int fetch_count,
		char fetch_flag, int result_set_idx, T_NET_BUF * net_buf,
		T_REQ_INFO * req_info)
{
  T_OBJECT dummy_obj;
  DB_METHFILE *tmp_p;
  char *name;
  int tuple_num, tuple_num_msg_offset;
  int i;

  memset (&dummy_obj, 0, sizeof (T_OBJECT));

  if (srv_handle->cursor_pos != cursor_pos)
    {
      tmp_p = (DB_METHFILE *) (srv_handle->session);
      for (i = 1; (i < cursor_pos) && (tmp_p); i++)
	{
	  tmp_p = db_methfile_next (tmp_p);
	}
      if (i != cursor_pos)
	return CAS_ER_NO_MORE_DATA;
    }
  else
    tmp_p = (DB_METHFILE *) (srv_handle->cur_result);

  tuple_num = 0;
  net_buf_cp_int (net_buf, tuple_num, &tuple_num_msg_offset);

  for (i = 0; (tmp_p) && (i < fetch_count); i++)
    {
      net_buf_cp_int (net_buf, cursor_pos, NULL);
      NET_BUF_CP_OBJECT (net_buf, &dummy_obj);

      /* 1. name */
      name = (char *) db_methfile_name (tmp_p);
      add_res_data_string (net_buf, name, strlen (name), 0);

      tuple_num++;
      cursor_pos++;

      tmp_p = db_methfile_next (tmp_p);
    }
  net_buf_overwrite_int (net_buf, tuple_num_msg_offset, tuple_num);

  srv_handle->cur_result = (void *) tmp_p;
  srv_handle->cursor_pos = cursor_pos;

  return 0;
}

static int
fetch_constraint (T_SRV_HANDLE * srv_handle, int cursor_pos, int fetch_count,
		  char fetch_flag, int result_set_idx, T_NET_BUF * net_buf,
		  T_REQ_INFO * req_info)
{
  T_OBJECT dummy_obj;
  DB_CONSTRAINT *tmp_p;
  int tuple_num, tuple_num_msg_offset;
  int i, j;
  DB_ATTRIBUTE **attr = NULL;
  short type;
  char *name, *attr_name;
  int bt_total_pages, bt_num_keys, bt_leaf_pages, bt_height;
  int asc_desc;

  memset (&dummy_obj, 0, sizeof (T_OBJECT));

  tmp_p = (DB_CONSTRAINT *) (srv_handle->session);

  tuple_num = 0;
  cursor_pos = 1;
  net_buf_cp_int (net_buf, tuple_num, &tuple_num_msg_offset);

  for (i = 1; tmp_p; i++)
    {
      type = db_constraint_type (tmp_p);
      switch (type)
	{
	case DB_CONSTRAINT_UNIQUE:
	case DB_CONSTRAINT_INDEX:
	  asc_desc = 0;		/* 'A' */
	  break;
	case DB_CONSTRAINT_REVERSE_UNIQUE:
	case DB_CONSTRAINT_REVERSE_INDEX:
	  asc_desc = 1;		/* 'D' */
	  break;
	default:
	  goto const_next;
	}

      name = (char *) db_constraint_name (tmp_p);

      bt_total_pages = bt_num_keys = bt_leaf_pages = bt_height = 0;
#ifdef GET_INDEX_INFO
      db_get_btree_statistics (tmp_p,
			       &bt_leaf_pages, &bt_total_pages,
			       &bt_num_keys, &bt_height);
#endif

      attr = db_constraint_attributes (tmp_p);
      for (j = 0; attr[j]; j++)
	{
	  attr_name = (char *) db_attribute_name (attr[j]);

	  net_buf_cp_int (net_buf, cursor_pos, NULL);
	  NET_BUF_CP_OBJECT (net_buf, &dummy_obj);

	  /* 1. type */
	  add_res_data_short (net_buf, constraint_dbtype_to_castype (type),
			      0);

	  /* 2. const name */
	  add_res_data_string (net_buf, name, strlen (name), 0);

	  /* 3. attr name */
	  add_res_data_string (net_buf, attr_name, strlen (attr_name), 0);

	  /* 4. num pages */
	  add_res_data_int (net_buf, bt_total_pages, 0);

	  /* 5. num keys */
	  add_res_data_int (net_buf, bt_num_keys, 0);

	  /* 6. primary key */
	  add_res_data_short (net_buf, 0, 0);

	  /* 7. key_order */
	  add_res_data_short (net_buf, j + 1, 0);

	  /* 8. asc_desc */
	  add_res_data_string (net_buf, asc_desc ? "D" : "A", 1, 0);

	  tuple_num++;
	  cursor_pos++;
	}
    const_next:
      tmp_p = db_constraint_next (tmp_p);
    }
  net_buf_overwrite_int (net_buf, tuple_num_msg_offset, tuple_num);

  return 0;
}

static int
fetch_trigger (T_SRV_HANDLE * srv_handle, int cursor_pos, int fetch_count,
	       char fetch_flag, int result_set_idx, T_NET_BUF * net_buf,
	       T_REQ_INFO * req_info)
{
  T_OBJECT dummy_obj;
  DB_OBJLIST *tmp_p;
  int i;
  int tuple_num, tuple_num_msg_offset;

  memset (&dummy_obj, 0, sizeof (T_OBJECT));

  if (srv_handle->cursor_pos != cursor_pos)
    {
      tmp_p = (DB_OBJLIST *) (srv_handle->session);
      for (i = 1; (i < cursor_pos) && (tmp_p); i++)
	{
	  tmp_p = tmp_p->next;
	}
      if (i != cursor_pos)
	return CAS_ER_NO_MORE_DATA;
    }
  else
    {
      tmp_p = (DB_OBJLIST *) (srv_handle->cur_result);
    }

  tuple_num = 0;
  net_buf_cp_int (net_buf, tuple_num, &tuple_num_msg_offset);

  for (i = 0; (tmp_p) && (i < fetch_count); i++, tmp_p = tmp_p->next)
    {
      char *tmp_str;
      DB_OBJECT *target_class_obj;
      char str_buf[32];
      double trig_priority;
      DB_TRIGGER_STATUS trig_status;
      DB_TRIGGER_EVENT trig_event;
      DB_TRIGGER_TIME trig_time;

      net_buf_cp_int (net_buf, cursor_pos, NULL);
      NET_BUF_CP_OBJECT (net_buf, &dummy_obj);

      /* 1. name */
      if (db_trigger_name (tmp_p->op, &tmp_str) < 0)
	{
	  add_res_data_string (net_buf, "", 0, 0);
	}
      else
	{
	  if (tmp_str != NULL)
	    {
	      add_res_data_string (net_buf, tmp_str, strlen (tmp_str), 0);
	      db_string_free (tmp_str);
	    }
	}

      /* 2. status */
      if (db_trigger_status (tmp_p->op, &trig_status) < 0)
	str_buf[0] = '\0';
      else
	trigger_status_str (trig_status, str_buf);
      add_res_data_string (net_buf, str_buf, strlen (str_buf), 0);

      /* 3. event */
      if (db_trigger_event (tmp_p->op, &trig_event) < 0)
	str_buf[0] = '\0';
      else
	trigger_event_str (trig_event, str_buf);
      add_res_data_string (net_buf, str_buf, strlen (str_buf), 0);

      /* 4. target class */
      if (db_trigger_class (tmp_p->op, &target_class_obj) < 0)
	{
	  add_res_data_string (net_buf, "", 0, 0);
	}
      else
	{
	  tmp_str = (char *) db_get_class_name (target_class_obj);
	  if (tmp_str == NULL)
	    tmp_str = (char *) "";
	  add_res_data_string (net_buf, tmp_str, strlen (tmp_str), 0);
	}

      /* 5. target attribute */
      tmp_str = NULL;
      if ((db_trigger_attribute (tmp_p->op, &tmp_str) < 0)
	  || (tmp_str == NULL))
	{
	  add_res_data_string (net_buf, "", 0, 0);
	}
      else
	{
	  add_res_data_string (net_buf, tmp_str, strlen (tmp_str), 0);
	  db_string_free (tmp_str);
	}

      /* 6. action time */
      if (db_trigger_action_time (tmp_p->op, &trig_time) < 0)
	str_buf[0] = '\0';
      else
	trigger_time_str (trig_time, str_buf);
      add_res_data_string (net_buf, str_buf, strlen (str_buf), 0);

      /* 7. action */
      tmp_str = NULL;
      if ((db_trigger_action (tmp_p->op, &tmp_str) < 0) || (tmp_str == NULL))
	{
	  add_res_data_string (net_buf, "", 0, 0);
	}
      else
	{
	  add_res_data_string (net_buf, tmp_str, strlen (tmp_str), 0);
	  db_string_free (tmp_str);
	}

      /* 8. priority */
      trig_priority = 0;
      db_trigger_priority (tmp_p->op, &trig_priority);
      add_res_data_float (net_buf, (float) trig_priority, 0);

      /* 9. condition time */
      if (db_trigger_condition_time (tmp_p->op, &trig_time) < 0)
	str_buf[0] = '\0';
      else
	trigger_time_str (trig_time, str_buf);
      add_res_data_string (net_buf, str_buf, strlen (str_buf), 0);

      /* 10. condition */
      tmp_str = NULL;
      if ((db_trigger_condition (tmp_p->op, &tmp_str) < 0)
	  || (tmp_str == NULL))
	{
	  add_res_data_string (net_buf, "", 0, 0);
	}
      else
	{
	  add_res_data_string (net_buf, tmp_str, strlen (tmp_str), 0);
	  db_string_free (tmp_str);
	}

      tuple_num++;
      cursor_pos++;
    }
  net_buf_overwrite_int (net_buf, tuple_num_msg_offset, tuple_num);

  srv_handle->cur_result = (void *) tmp_p;
  srv_handle->cursor_pos = cursor_pos;

  return 0;
}

static int
fetch_privilege (T_SRV_HANDLE * srv_handle, int cursor_pos, int fetch_count,
		 char fetch_flag, int result_set_idx, T_NET_BUF * net_buf,
		 T_REQ_INFO * req_info)
{
  T_OBJECT dummy_obj;
  int num_result;
  int tuple_num, tuple_num_msg_offset;
  int i;
  T_PRIV_TABLE *priv_table;

  priv_table = (T_PRIV_TABLE *) (srv_handle->session);
  if (priv_table == NULL)
    return CAS_ER_NO_MORE_DATA;

  num_result = srv_handle->sch_tuple_num;

  memset (&dummy_obj, 0, sizeof (T_OBJECT));

  if (num_result < cursor_pos)
    {
      return CAS_ER_NO_MORE_DATA;
    }

  tuple_num = 0;
  net_buf_cp_int (net_buf, tuple_num, &tuple_num_msg_offset);

  for (i = 0; (i < fetch_count) && (cursor_pos <= num_result); i++)
    {
      const char *p;
      int index;

      index = cursor_pos - 1;

      net_buf_cp_int (net_buf, cursor_pos, NULL);
      NET_BUF_CP_OBJECT (net_buf, &dummy_obj);

      /* 1. name */
      p = priv_table[index].class_name;
      add_res_data_string (net_buf, p, strlen (p), 0);

      /* 2. privilege */
      switch (priv_table[index].priv)
	{
	case DB_AUTH_SELECT:
	  p = "SELECT";
	  break;
	case DB_AUTH_INSERT:
	  p = "INSERT";
	  break;
	case DB_AUTH_UPDATE:
	  p = "UPDATE";
	  break;
	case DB_AUTH_DELETE:
	  p = "DELETE";
	  break;
	case DB_AUTH_ALTER:
	  p = "ALTER";
	  break;
	case DB_AUTH_INDEX:
	  p = "INDEX";
	  break;
	case DB_AUTH_EXECUTE:
	  p = "EXECUTE";
	  break;
	default:
	  p = "NONE";
	  break;
	}
      add_res_data_string (net_buf, p, strlen (p), 0);

      /* 3. grantable */
      if (priv_table[index].grant)
	p = "YES";
      else
	p = "NO";
      add_res_data_string (net_buf, p, strlen (p), 0);

      tuple_num++;
      cursor_pos++;
    }
  net_buf_overwrite_int (net_buf, tuple_num_msg_offset, tuple_num);

  return 0;
}

static int
fetch_primary_key (T_SRV_HANDLE * srv_handle, int cursor_pos, int fetch_count,
		   char fetch_flag, int result_set_idx, T_NET_BUF * net_buf,
		   T_REQ_INFO * req_info)
{
  net_buf_cp_int (net_buf, 0, NULL);
  return 0;
}

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
      net_buf_cp_int (net_buf, CAS_TIMESTAMP_SIZE + 1, NULL);
      net_buf_cp_byte (net_buf, type);
    }
  else
    net_buf_cp_int (net_buf, CAS_TIMESTAMP_SIZE, NULL);

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
      net_buf_cp_int (net_buf, CAS_DATETIME_SIZE + 1, NULL);
      net_buf_cp_byte (net_buf, type);
    }
  else
    {
      net_buf_cp_int (net_buf, CAS_DATETIME_SIZE, NULL);
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
      net_buf_cp_int (net_buf, SIZE_TIME + 1, NULL);
      net_buf_cp_byte (net_buf, type);
    }
  else
    {
      net_buf_cp_int (net_buf, SIZE_TIME, NULL);
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
      net_buf_cp_int (net_buf, SIZE_DATE + 1, NULL);
      net_buf_cp_byte (net_buf, type);
    }
  else
    {
      net_buf_cp_int (net_buf, SIZE_DATE, NULL);
    }
  net_buf_cp_short (net_buf, yr);
  net_buf_cp_short (net_buf, mon);
  net_buf_cp_short (net_buf, day);
}

static void
add_res_data_object (T_NET_BUF * net_buf, T_OBJECT * obj, char type)
{
  if (type)
    {
      net_buf_cp_int (net_buf, SIZE_OBJECT + 1, NULL);
      net_buf_cp_byte (net_buf, type);
    }
  else
    {
      net_buf_cp_int (net_buf, SIZE_OBJECT, NULL);
    }

  NET_BUF_CP_OBJECT (net_buf, obj);
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

static int
get_num_markers (char *stmt)
{
  char state = OUT_STR;
  char *p;
  int num_q = 0;

  for (p = stmt; *p; p++)
    {
      if (*p == '?')
	{
	  if (state == OUT_STR)
	    num_q++;
	}
      else if (*p == '\'')
	{
	  state = (state == OUT_STR) ? IN_STR : OUT_STR;
	}
    }
  return num_q;
}

static char
get_stmt_type (char *stmt)
{
  if (strncasecmp (stmt, "insert", 6) == 0)
    {
      return CUBRID_STMT_INSERT;
    }
  else if (strncasecmp (stmt, "update", 6) == 0)
    {
      return CUBRID_STMT_UPDATE;
    }
  else if (strncasecmp (stmt, "delete", 6) == 0)
    {
      return CUBRID_STMT_DELETE;
    }
  else if (strncasecmp (stmt, "call", 4) == 0)
    {
      return CUBRID_STMT_CALL;
    }
  else if (strncasecmp (stmt, "evaluate", 8) == 0)
    {
      return CUBRID_STMT_EVALUATE;
    }
  else
    {
      return CUBRID_MAX_STMT_TYPE;
    }
}

static int
prepare_column_list_info_set (DB_SESSION * session, char prepare_flag,
			      T_QUERY_RESULT * q_result, T_NET_BUF * net_buf,
			      T_BROKER_VERSION client_version)
{
  DB_QUERY_TYPE *column_info = NULL, *col;
  DB_DOMAIN *domain;
  int num_cols;
  DB_TYPE db_type;
  int num_col_offset;
  char *col_name, *attr_name, *class_name;
  T_COL_UPDATE_INFO *col_update_info = NULL;
  char stmt_type = q_result->stmt_type;
  int stmt_id = q_result->stmt_id;
  char updatable_flag = prepare_flag & CCI_PREPARE_UPDATABLE;
  char *null_type_column = NULL;

  q_result->col_updatable = FALSE;
  q_result->include_oid = FALSE;

  if (stmt_type == CUBRID_STMT_SELECT)
    {
      if (updatable_flag)
	updatable_flag = TRUE;

      if (prepare_flag)
	{
	  if (db_query_produce_updatable_result (session, stmt_id) <= 0)
	    {
	      updatable_flag = FALSE;
	    }
	  else
	    {
	      q_result->include_oid = TRUE;
	    }
	}

      column_info = db_get_query_type_list (session, stmt_id);
      if (column_info == NULL)
	{
	  DB_ERR_MSG_SET (net_buf, db_error_code ());
	  return CAS_ER_DBMS;
	}

      net_buf_cp_byte (net_buf, updatable_flag);

      num_cols = 0;
      net_buf_cp_int (net_buf, num_cols, &num_col_offset);
      for (col = column_info; col != NULL; col = db_query_format_next (col))
	{
	  char set_type, cas_type;
	  int precision;
	  short scale;
	  char *temp_column = NULL;

	  temp_column = (char *) REALLOC (null_type_column, num_cols + 1);
	  if (temp_column == NULL)
	    {
	      net_buf_clear (net_buf);
	      net_buf_cp_int (net_buf, CAS_ER_NO_MORE_MEMORY, NULL);
	      if (null_type_column != NULL)
		{
		  FREE_MEM (null_type_column);
		}
	      FREE_MEM (col_update_info);
	      return CAS_ER_NO_MORE_MEMORY;
	    }
	  null_type_column = temp_column;
	  null_type_column[num_cols] = 0;

	  if (stripped_column_name)
	    {
	      col_name = (char *) db_query_format_name (col);
	    }
	  else
	    {
	      col_name = (char *) db_query_format_original_name (col);
	      if (strchr (col_name, '*') != NULL)
		col_name = (char *) db_query_format_name (col);
	    }
	  class_name = (char *) db_query_format_class_name (col);
	  attr_name = (char *) db_query_format_attr_name (col);

	  if (updatable_flag)
	    {
	      col_update_info =
		(T_COL_UPDATE_INFO *) REALLOC (col_update_info,
					       sizeof (T_COL_UPDATE_INFO) *
					       (num_cols + 1));
	      if (col_update_info == NULL)
		{
		  updatable_flag = FALSE;
		}
	      else
		{
		  hm_col_update_info_clear (&(col_update_info[num_cols]));

		  col_update_info[num_cols].updatable = FALSE;
		  if (db_query_format_col_type (col) == DB_COL_NAME)
		    {
		      ALLOC_COPY (col_update_info[num_cols].attr_name,
				  attr_name);
		      ALLOC_COPY (col_update_info[num_cols].class_name,
				  class_name);
		      if (col_update_info[num_cols].attr_name != NULL
			  && col_update_info[num_cols].class_name != NULL)
			{
			  col_update_info[num_cols].updatable = TRUE;
			}
		    }
		}
	    }

	  if (updatable_flag == FALSE
	      || col_update_info[num_cols].updatable == FALSE)
	    {
	      attr_name = (char *) "";
	    }

	  domain = db_query_format_domain (col);
	  db_type = db_domain_type (domain);
	  if (IS_SET_TYPE (db_type))
	    {
	      set_type = get_set_domain (domain, &precision, &scale, NULL);
	      cas_type = CAS_TYPE_COLLECTION (db_type, set_type);
	    }
	  else
	    {
	      cas_type = ux_db_type_to_cas_type (db_type);
	      precision = db_domain_precision (domain);
	      scale = (short) db_domain_scale (domain);
	    }

	  if (cas_type == CCI_U_TYPE_NULL)
	    {
	      null_type_column[num_cols] = 1;
	    }

	  /*
	   * if (cas_type == CCI_U_TYPE_CHAR && precision < 0)
	   *   precision = 0;
	   */
#ifndef LIBCAS_FOR_JSP
	  if (shm_appl->max_string_length >= 0)
	    {
	      if (precision < 0 || precision > shm_appl->max_string_length)
		precision = shm_appl->max_string_length;
	    }
#else
	  /* precision = DB_MAX_STRING_LENGTH; */
#endif

	  prepare_column_info_set (net_buf,
				   (char) cas_type,
				   scale,
				   precision,
				   col_name,
				   attr_name,
				   class_name,
				   (char) db_query_format_is_non_null (col),
				   client_version);

	  num_cols++;
	}
      q_result->null_type_column = null_type_column;
      net_buf_overwrite_int (net_buf, num_col_offset, num_cols);
      if (column_info)
	db_query_format_free (column_info);
      q_result->col_updatable = updatable_flag;
      q_result->num_column = num_cols;
      q_result->col_update_info = col_update_info;
    }
  else if (stmt_type == CUBRID_STMT_CALL
	   || stmt_type == CUBRID_STMT_GET_STATS
	   || stmt_type == CUBRID_STMT_EVALUATE)
    {
      q_result->null_type_column = (char *) MALLOC (1);
      if (q_result->null_type_column == NULL)
	{
	  net_buf_clear (net_buf);
	  net_buf_cp_int (net_buf, CAS_ER_NO_MORE_MEMORY, NULL);
	  return CAS_ER_NO_MORE_MEMORY;
	}
      q_result->null_type_column[0] = 1;

      updatable_flag = 0;
      net_buf_cp_byte (net_buf, updatable_flag);
      net_buf_cp_int (net_buf, 1, NULL);
      prepare_column_info_set (net_buf, 0, 0, 0, "", "", "", 0,
			       client_version);
    }
  else
    {
      updatable_flag = 0;
      net_buf_cp_byte (net_buf, updatable_flag);
      net_buf_cp_int (net_buf, 0, NULL);
    }

  return 0;
}

static int
execute_info_set (T_SRV_HANDLE * srv_handle, T_NET_BUF * net_buf,
		  T_BROKER_VERSION client_version, char exec_flag)
{
  int i, tuple_count, error;
  char stmt_type;
  T_OBJECT ins_oid;
  DB_VALUE val;
  DB_OBJECT *ins_obj_p;
  int retval = 0;
  CACHE_TIME srv_cache_time;

  net_buf_cp_int (net_buf, srv_handle->num_q_result, NULL);

  for (i = 0; i < srv_handle->num_q_result; i++)
    {
      stmt_type = srv_handle->q_result[i].stmt_type;
      tuple_count = srv_handle->q_result[i].tuple_count;

      net_buf_cp_byte (net_buf, stmt_type);
      net_buf_cp_int (net_buf, tuple_count, NULL);

      if (stmt_type == CUBRID_STMT_INSERT && tuple_count == 1
	  && srv_handle->q_result[i].result != NULL)
	{
	  error = db_query_get_tuple_value ((DB_QUERY_RESULT *) srv_handle->
					    q_result[i].result, 0, &val);
	  if (error < 0)
	    {
	      memset (&ins_oid, 0, sizeof (T_OBJECT));
	    }
	  else
	    {
	      ins_obj_p = db_get_object (&val);
	      uobj_to_cas_obj (ins_obj_p, &ins_oid);
	      db_value_clear (&val);
	    }
	}
      else
	{
	  memset (&ins_oid, 0, sizeof (T_OBJECT));
	}
      NET_BUF_CP_OBJECT (net_buf, &ins_oid);

      db_query_get_cache_time ((DB_QUERY_RESULT *) srv_handle->
			       q_result[i].result, &srv_cache_time);
      NET_BUF_CP_CACHE_TIME (net_buf, &srv_cache_time);
    }

  return retval;
}

static char
get_attr_type (DB_OBJECT * obj_p, char *attr_name)
{
  DB_ATTRIBUTE *attribute;
  DB_DOMAIN *attr_domain;
  char db_type;

  attribute = db_get_attribute (obj_p, attr_name);
  if (attribute == NULL)
    {
      return DB_TYPE_NULL;
    }
  attr_domain = db_attribute_domain (attribute);
  if (attr_domain == NULL)
    {
      return DB_TYPE_NULL;
    }

  db_type = db_domain_type (attr_domain);

  if (IS_SET_TYPE (db_type))
    {
      if (get_set_domain (attr_domain, NULL, NULL, &db_type) < 0)
	db_type = DB_TYPE_NULL;
    }

  return db_type;
}

static char *
get_domain_str (DB_DOMAIN * domain)
{
  DB_TYPE dtype;
  DB_DOMAIN *set_domain;
  DB_OBJECT *dclass;
  char precision_str[16], scale_str[16];
  int prec, scale;
  char *domain_str = NULL;
  const char *p = "";
  int size;
  int is_first = TRUE;
  const char *collection_str;
  char *set_dom_name_p;

  precision_str[0] = scale_str[0] = '\0';
  dtype = db_domain_type (domain);
  prec = db_domain_precision (domain);
  scale = db_domain_scale (domain);
  if (prec > 0)
    sprintf (precision_str, "%d", prec);
  if (scale > 0)
    sprintf (scale_str, "%d", scale);
  collection_str = NULL;

  switch (dtype)
    {
    case DB_TYPE_OBJECT:
      dclass = db_domain_class (domain);
      if (dclass == NULL)
	p = "object";
      else
	p = db_get_class_name (dclass);
      break;

    case DB_TYPE_SET:
      collection_str = "set";
      /* fall through */
    case DB_TYPE_MULTISET:
      if (collection_str == NULL)
	collection_str = "multiset";
      /* fall through */
    case DB_TYPE_LIST:		/* DB_TYPE_SEQUENCE */
      if (collection_str == NULL)
	collection_str = "sequence";
      set_domain = db_domain_set (domain);
      size = strlen (collection_str) + 3;
      domain_str = (char *) malloc (size);
      if (domain_str == NULL)
	return NULL;
      sprintf (domain_str, "%s(", collection_str);
      for (; set_domain; set_domain = db_domain_next (set_domain))
	{
	  set_dom_name_p = get_domain_str (set_domain);
	  if (set_dom_name_p == NULL)
	    continue;
	  if (is_first == TRUE)
	    size += (strlen (set_dom_name_p) + 1);
	  else
	    size += (strlen (set_dom_name_p) + 3);
	  domain_str = (char *) realloc (domain_str, size);
	  if (domain_str == NULL)
	    {
	      FREE_MEM (set_dom_name_p);
	      return NULL;
	    }
	  if (is_first == TRUE)
	    is_first = FALSE;
	  else
	    strcat (domain_str, ", ");
	  strcat (domain_str, set_dom_name_p);
	  FREE_MEM (set_dom_name_p);
	}
      strcat (domain_str, ")");
      break;

    case DB_TYPE_NUMERIC:
      sprintf (scale_str, "%d", scale);
      /* fall through */
    default:
      p = (char *) db_get_type_name (dtype);
      if (p == NULL)
	p = "";
    }

  if (domain_str == NULL)
    {
      int avail_size;
      char *domain_str_p;

      size = strlen (p);
      if (precision_str[0] != '\0')
	{
	  size += strlen (precision_str) + 2;
	}
      if (scale_str[0] != '\0')
	{
	  size += strlen (scale_str) + 1;
	}
      domain_str = (char *) malloc (size + 1);
      if (domain_str == NULL)
	{
	  return NULL;
	}
      domain_str_p = domain_str;
      avail_size = size;
      STRING_APPEND (domain_str_p, avail_size, "%s", p);
      if (precision_str[0] != '\0')
	{
	  STRING_APPEND (domain_str_p, avail_size, "(%s", precision_str);
	  if (scale_str[0] != '\0')
	    {
	      STRING_APPEND (domain_str_p, avail_size, ",%s", scale_str);
	    }
	  STRING_APPEND (domain_str_p, avail_size, ")");
	}
    }

  return domain_str;
}

static void
glo_err_msg_set (T_NET_BUF * net_buf, int err_code, const char *method_nm)
{
  char err_msg[256];

  if (net_buf == NULL)
    return;

  glo_err_msg_get (err_code, err_msg);
#ifdef CAS_DEBUG
  sprintf (err_msg, "%s:%s", method_nm, err_msg);
#endif
  NET_BUF_ERROR_MSG_SET (net_buf, CAS_ER_GLO, err_msg);
}

static void
glo_err_msg_get (int err_code, char *err_msg)
{
  switch (err_code)
    {
    case INVALID_STRING_INPUT_ARGUMENT:
      strcpy (err_msg, "INVALID_STRING_INPUT_ARGUMENT");
      break;
    case INVALID_INTEGER_INPUT_ARGUMENT:
      strcpy (err_msg, "INVALID_INTEGER_INPUT_ARGUMENT");
      break;
    case INVALID_STRING_OR_OBJ_ARGUMENT:
      strcpy (err_msg, "INVALID_STRING_OR_OBJ_ARGUMENT");
      break;
    case INVALID_OBJECT_INPUT_ARGUMENT:
      strcpy (err_msg, "INVALID_OBJECT_INPUT_ARGUMENT");
      break;
    case UNABLE_TO_FIND_GLO_STRUCTURE:
      strcpy (err_msg, "UNABLE_TO_FIND_GLO_STRUCTURE");
      break;
    case COULD_NOT_ACQUIRE_WRITE_LOCK:
      strcpy (err_msg, "COULD_NOT_ACQUIRE_WRITE_LOCK");
      break;
    case ERROR_DURING_TRUNCATION:
      strcpy (err_msg, "ERROR_DURING_TRUNCATION");
      break;
    case ERROR_DURING_DELETE:
      strcpy (err_msg, "ERROR_DURING_DELETE");
      break;
    case ERROR_DURING_INSERT:
      strcpy (err_msg, "ERROR_DURING_INSERT");
      break;
    case ERROR_DURING_WRITE:
      strcpy (err_msg, "ERROR_DURING_WRITE");
      break;
    case ERROR_DURING_READ:
      strcpy (err_msg, "ERROR_DURING_READ");
      break;
    case ERROR_DURING_SEEK:
      strcpy (err_msg, "ERROR_DURING_SEEK");
      break;
    case ERROR_DURING_APPEND:
      strcpy (err_msg, "ERROR_DURING_APPEND");
      break;
    case ERROR_DURING_MIGRATE:
      strcpy (err_msg, "ERROR_DURING_MIGRATE");
      break;
    case COPY_TO_ERROR:
      strcpy (err_msg, "COPY_TO_ERROR");
      break;
    case COPY_FROM_ERROR:
      strcpy (err_msg, "COPY_FROM_ERROR");
      break;
    case COULD_NOT_ALLOCATE_SEARCH_BUFFERS:
      strcpy (err_msg, "COULD_NOT_ALLOCATE_SEARCH_BUFFERS");
      break;
    case COULD_NOT_COMPILE_REGULAR_EXPRESSION:
      strcpy (err_msg, "COULD_NOT_COMPILE_REGULAR_EXPRESSION");
      break;
    case COULD_NOT_RESET_WORKING_BUFFER:
      strcpy (err_msg, "COULD_NOT_RESET_WORKING_BUFFER");
      break;
    case SEARCH_ERROR_ON_POSITION_CACHE:
      strcpy (err_msg, "SEARCH_ERROR_ON_POSITION_CACHE");
      break;
    case SEARCH_ERROR_ON_DATA_READ:
      strcpy (err_msg, "SEARCH_ERROR_ON_DATA_READ");
      break;
    case SEARCH_ERROR_DURING_LOOKUP:
      strcpy (err_msg, "SEARCH_ERROR_DURING_LOOKUP");
      break;
    case SEARCH_ERROR_REPOSITIONING_POINTER:
      strcpy (err_msg, "SEARCH_ERROR_REPOSITIONING_POINTER");
      break;
    default:
      sprintf (err_msg, "%d", err_code);
    }
}

static DB_OBJECT *
ux_str_to_obj (char *str)
{
  DB_OBJECT *obj;
  DB_IDENTIFIER oid;
  int page, slot, vol;
  int read;
  char del1, del2, del3;

  if (str == NULL)
    return NULL;

  read =
    sscanf (str, "%c%d%c%d%c%d", &del1, &page, &del2, &slot, &del3, &vol);
  if (read != 6)
    return NULL;
  if (del1 != '@' || del2 != '|' || del3 != '|')
    return NULL;
  if (page < 0 || slot < 0 || vol < 0)
    return NULL;

  oid.pageid = page;
  oid.slotid = slot;
  oid.volid = vol;
  obj = db_object (&oid);
  if (db_is_instance (obj))
    return obj;

  return NULL;
}

static int
sch_class_info (T_NET_BUF * net_buf, char *class_name, char pattern_flag,
		char v_class_flag, T_SRV_HANDLE * srv_handle,
		T_BROKER_VERSION client_version)
{
  char sql_stmt[QUERY_BUFFER_MAX], *sql_p = sql_stmt;
  int avail_size = sizeof (sql_stmt) - 1;
  int num_result;
  const char *case_stmt;
  const char *where_vclass;

  ut_tolower (class_name);

  if (cas_client_type == CAS_CLIENT_CCI)
    {
      case_stmt = "CASE WHEN is_system_class = 'YES' THEN 0 \
		      WHEN class_type = 'CLASS' THEN 2 \
		      WHEN class_type = 'PROXY' THEN 3 \
		      ELSE 1 END";
    }
  else
    {
      case_stmt = "CASE WHEN is_system_class = 'YES' THEN 0 \
		      WHEN class_type = 'CLASS' THEN 2 \
		      ELSE 1 END";
    }
  where_vclass = "class_type in {'VCLASS', 'PROXY'}";

  STRING_APPEND (sql_p, avail_size,
		 "SELECT class_name, CAST(%s AS short) FROM db_class ",
		 case_stmt);
  if (pattern_flag & CCI_CLASS_NAME_PATTERN_MATCH)
    {
      if (v_class_flag)
	{
	  if (class_name)
	    {
	      STRING_APPEND (sql_p, avail_size,
			     "WHERE class_name LIKE '%s' ESCAPE '\\' AND %s",
			     class_name, where_vclass);
	    }
	  else
	    {
	      STRING_APPEND (sql_p, avail_size, "WHERE %s", where_vclass);
	    }
	}
      else
	{
	  if (class_name)
	    {
	      STRING_APPEND (sql_p, avail_size,
			     "WHERE class_name LIKE '%s' ESCAPE '\\' ",
			     class_name);
	    }
	}
    }
  else
    {
      if (class_name == NULL)
	class_name = (char *) "";
      if (v_class_flag)
	{
	  STRING_APPEND (sql_p, avail_size,
			 "WHERE class_name = '%s' AND %s",
			 class_name, where_vclass);
	}
      else
	{
	  STRING_APPEND (sql_p, avail_size,
			 "WHERE class_name = '%s'", class_name);
	}
    }

  if ((num_result = sch_query_execute (srv_handle, sql_stmt, net_buf)) < 0)
    {
      return num_result;
    }

  net_buf_cp_int (net_buf, num_result, NULL);
  schema_table_meta (net_buf);

  return 0;
}

static int
sch_attr_info (T_NET_BUF * net_buf, char *class_name, char *attr_name,
	       char pattern_flag, char class_attr_flag,
	       T_SRV_HANDLE * srv_handle)
{
  char sql_stmt[QUERY_BUFFER_MAX], *sql_p = sql_stmt;
  int avail_size = sizeof (sql_stmt) - 1;
  int num_result;

  ut_tolower (class_name);
  ut_tolower (attr_name);

  STRING_APPEND (sql_p, avail_size,
		 "SELECT class_name, attr_name FROM db_attribute WHERE ");

  if (class_attr_flag)
    {
      STRING_APPEND (sql_p, avail_size, " attr_type = 'CLASS' ");
    }
  else
    {
      STRING_APPEND (sql_p, avail_size,
		     " attr_type in {'INSTANCE', 'SHARED'} ");
    }

  if (pattern_flag & CCI_CLASS_NAME_PATTERN_MATCH)
    {
      if (class_name)
	{
	  STRING_APPEND (sql_p, avail_size,
			 " AND class_name LIKE '%s' ESCAPE '\\' ",
			 class_name);
	}
    }
  else
    {
      if (class_name == NULL)
	{
	  class_name = (char *) "";
	}
      STRING_APPEND (sql_p, avail_size,
		     " AND class_name = '%s' ", class_name);
    }

  if (pattern_flag & CCI_ATTR_NAME_PATTERN_MATCH)
    {
      if (attr_name)
	{
	  STRING_APPEND (sql_p, avail_size,
			 " AND attr_name LIKE '%s' ESCAPE '\\' ", attr_name);
	}
    }
  else
    {
      if (attr_name == NULL)
	{
	  attr_name = (char *) "";
	}
      STRING_APPEND (sql_p, avail_size, " AND attr_name = '%s' ", attr_name);
    }
  STRING_APPEND (sql_p, avail_size, " ORDER BY class_name, def_order");

  if ((num_result = sch_query_execute (srv_handle, sql_stmt, net_buf)) < 0)
    {
      return num_result;
    }

  net_buf_cp_int (net_buf, num_result, NULL);
  schema_attr_meta (net_buf);

  return 0;
}

static int
sch_queryspec (T_NET_BUF * net_buf, char *class_name,
	       T_SRV_HANDLE * srv_handle)
{
  char sql_stmt[1024];
  int num_result;

  if (class_name == NULL)
    class_name = (char *) "";
  ut_tolower (class_name);

  sprintf (sql_stmt,
	   "SELECT vclass_def FROM db_vclass WHERE vclass_name = '%s'",
	   class_name);

  if ((num_result = sch_query_execute (srv_handle, sql_stmt, net_buf)) < 0)
    {
      return num_result;
    }

  net_buf_cp_int (net_buf, num_result, NULL);
  schema_query_spec_meta (net_buf);

  return 0;
}

static void
sch_method_info (T_NET_BUF * net_buf, char *class_name, char flag,
		 void **result)
{
  DB_OBJECT *class_obj;
  DB_METHOD *method, *method_list;
  int num_method;

  class_obj = db_find_class (class_name);

  if (flag)
    method_list = db_get_class_methods (class_obj);
  else
    method_list = db_get_methods (class_obj);

  num_method = 0;
  for (method = method_list; method; method = db_method_next (method))
    {
      num_method++;
    }

  net_buf_cp_int (net_buf, num_method, NULL);
  schema_method_meta (net_buf);

  *result = (void *) method_list;
}

static void
sch_methfile_info (T_NET_BUF * net_buf, char *class_name, void **result)
{
  DB_METHFILE *method_files, *mf;
  DB_OBJECT *class_obj;
  int num_mf;

  class_obj = db_find_class (class_name);
  method_files = db_get_method_files (class_obj);

  num_mf = 0;
  for (mf = method_files; mf; mf = db_methfile_next (mf))
    {
      num_mf++;
    }

  net_buf_cp_int (net_buf, num_mf, NULL);
  schema_methodfile_meta (net_buf);

  *result = (void *) method_files;
}

static int
sch_superclass (T_NET_BUF * net_buf, char *class_name, char flag,
		T_SRV_HANDLE * srv_handle)
{
  DB_OBJECT *class_obj;
  DB_OBJLIST *obj_list, *obj_tmp;
  int num_obj;
  T_CLASS_TABLE *class_table = NULL;
  int alloc_table_size = 0;

  class_obj = db_find_class (class_name);
  if (flag)
    obj_list = db_get_superclasses (class_obj);
  else
    obj_list = db_get_subclasses (class_obj);

  num_obj = 0;
  for (obj_tmp = obj_list; obj_tmp; obj_tmp = obj_tmp->next)
    {
      char *p;

      if (num_obj + 1 > alloc_table_size)
	{
	  alloc_table_size += 10;
	  class_table =
	    (T_CLASS_TABLE *) REALLOC (class_table,
				       sizeof (T_CLASS_TABLE) *
				       alloc_table_size);
	  if (class_table == NULL)
	    {
	      db_objlist_free (obj_list);
	      return CAS_ER_NO_MORE_MEMORY;
	    }
	}

      p = (char *) db_get_class_name (obj_tmp->op);

      class_table[num_obj].class_name = p;
      class_table[num_obj].class_type = class_type (obj_tmp->op);

      num_obj++;
    }
  db_objlist_free (obj_list);

  net_buf_cp_int (net_buf, num_obj, NULL);
  schema_superclasss_meta (net_buf);

  srv_handle->session = (void *) class_table;
  srv_handle->sch_tuple_num = num_obj;
  return 0;
}

static void
sch_constraint (T_NET_BUF * net_buf, char *class_name, void **result)
{
  DB_OBJECT *class_obj;
  DB_CONSTRAINT *constraint, *tmp_c;
  DB_ATTRIBUTE **attr;
  int num_const;
  int i;
  int type;

  class_obj = db_find_class (class_name);
  constraint = db_get_constraints (class_obj);
  num_const = 0;
  for (tmp_c = constraint; tmp_c; tmp_c = db_constraint_next (tmp_c))
    {
      type = db_constraint_type (tmp_c);
      switch (type)
	{
	case DB_CONSTRAINT_UNIQUE:
	case DB_CONSTRAINT_INDEX:
	case DB_CONSTRAINT_REVERSE_UNIQUE:
	case DB_CONSTRAINT_REVERSE_INDEX:
	  attr = db_constraint_attributes (tmp_c);
	  for (i = 0; attr[i]; i++)
	    {
	      num_const++;
	    }
	default:
	  break;
	}
    }

  net_buf_cp_int (net_buf, num_const, NULL);
  schema_constraint_meta (net_buf);

  *result = (void *) constraint;
}

static void
sch_trigger (T_NET_BUF * net_buf, char *trigger_name, void **result)
{
  DB_OBJLIST *all_trigger, *tmp_t;
  int num_trig;

  if (db_find_all_triggers (&all_trigger) < 0)
    {
      all_trigger = NULL;
    }

  num_trig = 0;
  for (tmp_t = all_trigger; tmp_t; tmp_t = tmp_t->next)
    {
      num_trig++;
    }

  net_buf_cp_int (net_buf, num_trig, NULL);
  schema_trigger_meta (net_buf);

  *result = (void *) all_trigger;
}

static int
sch_class_priv (T_NET_BUF * net_buf, char *class_name, char pat_flag,
		T_SRV_HANDLE * srv_handle)
{
  T_PRIV_TABLE *priv_table = NULL;
  int num_tuple = 0;
  int priv_table_alloc_num = 0;
  unsigned int class_priv;

  if ((pat_flag & CCI_CLASS_NAME_PATTERN_MATCH) == 0)
    {
      DB_OBJECT *class_obj;

      num_tuple = 0;

      if (class_name)
	{
	  class_obj = db_find_class (class_name);
	  if (class_obj != NULL)
	    {
	      priv_table =
		(T_PRIV_TABLE *) MALLOC (sizeof (T_PRIV_TABLE) * 8);
	      if (priv_table == NULL)
		{
		  class_obj = NULL;
		  return CAS_ER_NO_MORE_MEMORY;
		}
	      if (db_get_class_privilege (class_obj, &class_priv) >= 0)
		{
		  num_tuple = set_priv_table (class_priv,
					      (char *)
					      db_get_class_name (class_obj),
					      priv_table, 0);
		}
	    }
	}
    }
  else
    {
      DB_OBJLIST *obj_list, *obj_tmp;

      obj_list = db_get_all_classes ();

      num_tuple = 0;
      for (obj_tmp = obj_list; obj_tmp; obj_tmp = obj_tmp->next)
	{
	  char *p;

	  p = (char *) db_get_class_name (obj_tmp->op);
	  if (class_name != NULL && str_like (p, class_name, '\\') < 1)
	    continue;

	  if (num_tuple + 8 > priv_table_alloc_num)
	    {
	      priv_table = (T_PRIV_TABLE *) REALLOC (priv_table,
						     sizeof (T_PRIV_TABLE) *
						     (priv_table_alloc_num +
						      128));
	      priv_table_alloc_num += 128;
	      if (priv_table == NULL)
		return CAS_ER_NO_MORE_MEMORY;
	    }

	  if (db_get_class_privilege (obj_tmp->op, &class_priv) >= 0)
	    {
	      num_tuple += set_priv_table (class_priv,
					   (char *)
					   db_get_class_name (obj_tmp->op),
					   priv_table, num_tuple);
	    }
	}

      db_objlist_free (obj_list);
    }

  if (num_tuple == 0)
    FREE_MEM (priv_table);

  net_buf_cp_int (net_buf, num_tuple, NULL);
  schema_classpriv_meta (net_buf);

  srv_handle->session = (void *) priv_table;
  srv_handle->sch_tuple_num = num_tuple;
  return 0;
}

static int
sch_attr_priv (T_NET_BUF * net_buf, char *class_name, char *attr_name_pat,
	       char pat_flag, T_SRV_HANDLE * srv_handle)
{
  DB_OBJECT *class_obj;
  int num_tuple = 0;
  T_PRIV_TABLE *priv_table = NULL;
  int priv_table_alloc_num = 0;
  unsigned int class_priv;
  char *attr_name;
  DB_ATTRIBUTE *attributes, *attr;

  class_obj = db_find_class (class_name);
  if (class_obj == NULL)
    goto attr_priv_finale;
  if (db_get_class_privilege (class_obj, &class_priv) < 0)
    goto attr_priv_finale;

  attributes = db_get_attributes (class_obj);
  for (attr = attributes; attr; attr = db_attribute_next (attr))
    {
      attr_name = (char *) db_attribute_name (attr);
      if (pat_flag & CCI_ATTR_NAME_PATTERN_MATCH)
	{
	  if (attr_name_pat != NULL
	      && str_like (attr_name, attr_name_pat, '\\') < 1)
	    continue;
	}
      else
	{
	  if (attr_name_pat == NULL || strcmp (attr_name, attr_name_pat) != 0)
	    continue;
	}

      if (num_tuple + 8 > priv_table_alloc_num)
	{
	  priv_table_alloc_num += 100;
	  priv_table =
	    (T_PRIV_TABLE *) REALLOC (priv_table,
				      sizeof (T_PRIV_TABLE) *
				      priv_table_alloc_num);
	  if (priv_table == NULL)
	    {
	      class_obj = NULL;
	      return CAS_ER_NO_MORE_MEMORY;
	    }
	}

      num_tuple +=
	set_priv_table (class_priv, attr_name, priv_table, num_tuple);
    }

attr_priv_finale:

  if (num_tuple == 0)
    FREE_MEM (priv_table);

  net_buf_cp_int (net_buf, num_tuple, NULL);
  schema_attrpriv_meta (net_buf);

  srv_handle->session = (void *) priv_table;
  srv_handle->sch_tuple_num = num_tuple;

  class_obj = NULL;
  return 0;
}

static int
class_type (DB_OBJECT * class_obj)
{
  if (db_is_system_class (class_obj))
    return 0;
  else if (db_is_vclass (class_obj))
    return 1;

  return 2;
}

static int
class_attr_info (char *class_name, DB_ATTRIBUTE * attr, char *attr_pattern,
		 char pat_flag, T_ATTR_TABLE * attr_table)
{
  char *p;
  int db_type;
  DB_DOMAIN *domain;
  DB_OBJECT *class_obj;
  int set_type = 0;
  int precision;
  short scale;

  p = (char *) db_attribute_name (attr);

  domain = db_attribute_domain (attr);
  db_type = db_domain_type (domain);

  attr_table->class_name = class_name;
  attr_table->attr_name = p;
  if (IS_SET_TYPE (db_type))
    {
      set_type = get_set_domain (domain, &precision, &scale, NULL);
      attr_table->domain = CAS_TYPE_COLLECTION (db_type, set_type);
    }
  else
    {
      attr_table->domain = ux_db_type_to_cas_type (db_type);
      precision = db_domain_precision (domain);
      scale = (short) db_domain_scale (domain);
    }
  attr_table->scale = scale;
  attr_table->precision = precision;

  if (db_attribute_is_indexed (attr))
    attr_table->indexed = 1;
  else
    attr_table->indexed = 0;

  if (db_attribute_is_non_null (attr))
    attr_table->non_null = 1;
  else
    attr_table->non_null = 0;

  if (db_attribute_is_shared (attr))
    attr_table->shared = 1;
  else
    attr_table->shared = 0;

  if (db_attribute_is_unique (attr))
    attr_table->unique = 1;
  else
    attr_table->unique = 0;

  attr_table->default_val = db_attribute_default (attr);
  class_obj = db_attribute_class (attr);
  if (class_obj == NULL)
    attr_table->source_class = NULL;
  else
    attr_table->source_class = (char *) db_get_class_name (class_obj);

  attr_table->attr_order = db_attribute_order (attr) + 1;

  attr_table->set_domain = set_type;

  if (db_attribute_is_primary_key (attr))
    attr_table->is_key = 1;
  else
    attr_table->is_key = 0;

  return 1;
}

static int
set_priv_table (unsigned int class_priv, char *name,
		T_PRIV_TABLE * priv_table, int index)
{
  int grant_opt, priv_type;
  int num_tuple;
  int i;

  grant_opt = class_priv >> 8;

  num_tuple = 0;
  priv_type = 1;
  for (i = 0; i < 7; i++)
    {
      if (class_priv & priv_type)
	{
	  priv_table[index].class_name = name;
	  priv_table[index].priv = priv_type;
	  if (grant_opt & priv_type)
	    priv_table[index].grant = 1;
	  else
	    priv_table[index].grant = 0;
	  num_tuple++;
	  index++;
	}
      priv_type <<= 1;
    }

  return num_tuple;
}

static int
sch_query_execute (T_SRV_HANDLE * srv_handle, char *sql_stmt,
		   T_NET_BUF * net_buf)
{
  DB_SESSION *session = NULL;
  int stmt_id, num_result, stmt_type;
  DB_QUERY_RESULT *result = NULL;
  T_QUERY_RESULT *q_result = NULL;

  if (!(session = db_open_buffer (sql_stmt)))
    {
      return CAS_ER_NO_MORE_MEMORY;
    }
  if ((stmt_id = db_compile_statement (session)) < 0)
    {
      DB_ERR_MSG_SET (net_buf, stmt_id);
      db_close_session (session);
      return CAS_ER_DBMS;
    }
  stmt_type = db_get_statement_type (session, stmt_id);
  num_result = db_execute_statement (session, stmt_id, &result);
#ifndef LIBCAS_FOR_JSP
  as_info->num_queries_processed %= MAX_DIAG_DATA_VALUE;
  as_info->num_queries_processed++;
#endif
  if (num_result < 0)
    {
      DB_ERR_MSG_SET (net_buf, stmt_id);
      db_close_session (session);
      return CAS_ER_DBMS;
    }

  /* success; peek the values in tuples */
  (void) db_query_set_copy_tplvalue (result, 0 /* peek */ );

  q_result = (T_QUERY_RESULT *) malloc (sizeof (T_QUERY_RESULT));
  if (q_result == NULL)
    {
      db_query_end (result);
      db_close_session (session);
      return CAS_ER_NO_MORE_MEMORY;
    }
  hm_qresult_clear (q_result);
  q_result->stmt_type = stmt_type;
  q_result->stmt_id = stmt_id;
  q_result->tuple_count = num_result;
  q_result->result = (void *) result;
  q_result->col_updatable = FALSE;
  q_result->include_oid = FALSE;
  q_result->col_update_info = NULL;

  srv_handle->max_col_size = -1;
  srv_handle->session = (void *) session;
  srv_handle->q_result = q_result;
  srv_handle->cur_result = (void *) srv_handle->q_result;
  srv_handle->num_q_result = 1;
  srv_handle->cur_result_index = 1;
  srv_handle->sql_stmt = NULL;

  return num_result;
}

static int
sch_direct_super_class (T_NET_BUF * net_buf, char *class_name,
			int pattern_flag, T_SRV_HANDLE * srv_handle)
{
  int num_result = 0;
  char sql_stmt[QUERY_BUFFER_MAX], *sql_p = sql_stmt;
  int avail_size = sizeof (sql_stmt) - 1;

  ut_tolower (class_name);

  STRING_APPEND (sql_p, avail_size, "SELECT class_name, super_class_name \
		    FROM db_direct_super_class ");
  if (pattern_flag & CCI_CLASS_NAME_PATTERN_MATCH)
    {
      if (class_name)
	{
	  STRING_APPEND (sql_p, avail_size,
			 "WHERE class_name LIKE '%s' ESCAPE '\\' ",
			 class_name);
	}
    }
  else
    {
      if (class_name == NULL)
	{
	  class_name = (char *) "";
	}
      STRING_APPEND (sql_p, avail_size, "WHERE class_name = '%s'",
		     class_name);
    }

  if ((num_result = sch_query_execute (srv_handle, sql_stmt, net_buf)) < 0)
    {
      return num_result;
    }

  net_buf_cp_int (net_buf, num_result, NULL);
  schema_directsuper_meta (net_buf);

  return 0;
}

static int
sch_primary_key (T_NET_BUF * net_buf)
{
  net_buf_cp_int (net_buf, 0, NULL);
  schema_primarykey_meta (net_buf);
  return 0;
}

static short
constraint_dbtype_to_castype (int db_const_type)
{
  if (db_const_type == DB_CONSTRAINT_UNIQUE)
    return CCI_CONSTRAINT_TYPE_UNIQUE;
  if (db_const_type == DB_CONSTRAINT_REVERSE_UNIQUE)
    return CCI_CONSTRAINT_TYPE_UNIQUE;
  return CCI_CONSTRAINT_TYPE_INDEX;
}

#if 0
static int
prepare_call_token (char *sql, char **out_call_func, char **out_call_target,
		    int *is_first_out)
{
  char *p;
  char *call_stmt;
  char *call_func, *call_target;
  char buf[5][256];
  int num_read;

  call_stmt = strdup (sql);
  if (call_stmt == NULL)
    return CAS_ER_NO_MORE_MEMORY;

  /* acceptable call stmt:
     "call <func_name>(?,?...)"
     "call <func_name>(?,?...) on <oid>"
     "call <func_name>(?,?...) on class <class_name>"
     "? = call <func_name>(?,?...)"
   */

  ut_tolower (call_stmt);
  for (p = call_stmt; *p; p++)
    {
      if (*p == '=' || *p == '{' || *p == '}' || *p == '(' || *p == ')'
	  || *p == '?' || *p == ',' || *p == ';')
	{
	  if (*p == '=')
	    *is_first_out = 1;
	  *p = ' ';
	}
    }

  memset (buf, 0, sizeof (buf));
  num_read =
    sscanf (call_stmt, "%s %s %s %s %s", buf[0], buf[1], buf[2], buf[3],
	    buf[4]);
  FREE_MEM (call_stmt);

  if ((num_read < 2) || (num_read == 3) || (strcmp (buf[0], "call") != 0)
      || (num_read > 3 && strcmp (buf[2], "on") != 0)
      || (num_read == 5 && strcmp (buf[3], "class") != 0))
    {
      return CAS_ER_INVALID_CALL_STMT;
    }

  call_func = buf[1];
  if (num_read == 4)
    {
      call_target = buf[3];
    }
  else if (num_read == 5)
    {
      call_target = buf[4];
    }
  else
    {
      /*call_target = getenv("UC_METHOD_CALL_TARGET_CLASS");
         if (call_target == NULL) {
         return CAS_ER_INVALID_CALL_STMT;
         } */
      call_target = NULL;
    }

  *out_call_func = strdup (call_func);
  if (call_target)
    {
      *out_call_target = strdup (call_target);
    }
  else
    {
      *out_call_target = NULL;
    }

  if (*out_call_func == NULL /* || *out_call_target == NULL */ )
    {
      return CAS_ER_NO_MORE_MEMORY;
    }
  return 0;
}
#endif

static T_PREPARE_CALL_INFO *
make_prepare_call_info (int num_args, int is_first_out)
{
  T_PREPARE_CALL_INFO *call_info;
  DB_VALUE *ret_val = NULL;
  DB_VALUE **arg_val = NULL;
  char *param_mode = NULL;
  int i;

  call_info = (T_PREPARE_CALL_INFO *) MALLOC (sizeof (T_PREPARE_CALL_INFO));
  if (call_info == NULL)
    {
      return NULL;
    }

  memset (call_info, 0, sizeof (T_PREPARE_CALL_INFO));

  ret_val = (DB_VALUE *) MALLOC (sizeof (DB_VALUE));
  if (ret_val == NULL)
    {
      goto exit_on_error;
    }
  db_make_null (ret_val);

  if (num_args > 0)
    {
      arg_val = (DB_VALUE **) MALLOC (sizeof (DB_VALUE *) * (num_args + 1));
      if (arg_val == NULL)
	{
	  goto exit_on_error;
	}
      memset (arg_val, 0, sizeof (DB_VALUE *) * (num_args + 1));

      param_mode = (char *) MALLOC (sizeof (char) * num_args);
      if (param_mode == NULL)
	{
	  goto exit_on_error;
	}

      for (i = 0; i < num_args; i++)
	{
	  arg_val[i] = (DB_VALUE *) MALLOC (sizeof (DB_VALUE));
	  if (arg_val[i] == NULL)
	    {
	      goto exit_on_error;
	    }
	  db_make_null (arg_val[i]);
	  param_mode[i] = CCI_PARAM_MODE_UNKNOWN;
	}
    }

  call_info->dbval_ret = ret_val;
  call_info->dbval_args = arg_val;
  call_info->num_args = num_args;
  call_info->param_mode = param_mode;
  call_info->is_first_out = is_first_out;

  return call_info;

exit_on_error:
  FREE_MEM (call_info);
  FREE_MEM (ret_val);
  FREE_MEM (param_mode);
  if (arg_val != NULL)
    {
      for (i = 0; i < num_args; i++)
	{
	  FREE_MEM (arg_val[i]);
	}
      FREE_MEM (arg_val);
    }
  return NULL;
}

static void
prepare_call_info_dbval_clear (T_PREPARE_CALL_INFO * call_info)
{
  DB_VALUE **args;
  int i = 0;

  if (call_info)
    {
      if (call_info->dbval_ret)
	{
	  db_value_clear ((DB_VALUE *) call_info->dbval_ret);
	  db_make_null ((DB_VALUE *) call_info->dbval_ret);
	}

      args = (DB_VALUE **) call_info->dbval_args;

      if (call_info->is_first_out)
	{
	  db_value_clear (args[0]);
	  i++;
	}

      for (; i < call_info->num_args; i++)
	{
	  if (args[i])
	    {
	      db_make_null (args[i]);
	    }
	}
    }
}

static int
fetch_call (T_SRV_HANDLE * srv_handle, T_NET_BUF * net_buf)
{
  T_OBJECT tuple_obj;
  DB_VALUE **out_vals, null_val, *val_ptr;
  int i;
  T_PREPARE_CALL_INFO *call_info = srv_handle->prepare_call_info;

  if (call_info == NULL)
    {
      int err_code;
      err_code = CAS_ER_SRV_HANDLE;
      net_buf_clear (net_buf);
      net_buf_cp_int (net_buf, err_code, NULL);
      return err_code;
    }

  net_buf_cp_int (net_buf, 1, NULL);	/* tuple count */
  net_buf_cp_int (net_buf, 1, NULL);	/* cursor position */
  memset (&tuple_obj, 0, sizeof (T_OBJECT));
  NET_BUF_CP_OBJECT (net_buf, &tuple_obj);

  val_ptr = (DB_VALUE *) call_info->dbval_ret;
  dbval_to_net_buf (val_ptr, net_buf, 0, srv_handle->max_col_size, 1);

  out_vals = (DB_VALUE **) call_info->dbval_args;
  db_make_null (&null_val);

  for (i = 0; i < call_info->num_args; i++)
    {
      val_ptr = &null_val;
      if (call_info->param_mode[i] & CCI_PARAM_MODE_OUT)
	val_ptr = out_vals[i];

      dbval_to_net_buf (val_ptr, net_buf, 0, srv_handle->max_col_size, 1);
    }

  return 0;
}

#if 0
static void
method_err_msg_set (T_NET_BUF * net_buf, int err_code, DB_VALUE * ret_val)
{
  DB_ERR_MSG_SET (net_buf, err_code);
}
#endif

static int
create_srv_handle_with_query_result (DB_QUERY_RESULT * result, int num_column,
				     DB_QUERY_TYPE * column_info,
				     int stmt_type,
				     unsigned int query_seq_num)
{
  int srv_h_id;
  int err_code = 0;
  T_SRV_HANDLE *srv_handle = NULL;
  T_QUERY_RESULT *q_result = NULL;

  srv_h_id = hm_new_srv_handle (&srv_handle, query_seq_num);
  if (srv_h_id < 0)
    {
      err_code = srv_h_id;
      goto error;
    }
  srv_handle->schema_type = -1;

  q_result = (T_QUERY_RESULT *) malloc (sizeof (T_QUERY_RESULT));
  if (q_result == NULL)
    {
      err_code = CAS_ER_NO_MORE_MEMORY;
      goto error;
    }
  hm_qresult_clear (q_result);
  srv_handle->q_result = q_result;

  q_result->result = result;
  q_result->tuple_count = db_query_tuple_count (result);
  q_result->stmt_type = stmt_type;
  q_result->col_updatable = FALSE;
  q_result->include_oid = FALSE;
  q_result->async_flag = FALSE;
  q_result->num_column = num_column;
  q_result->column_info = column_info;

  srv_handle->cur_result = (void *) srv_handle->q_result;
  srv_handle->cur_result_index = 1;
  srv_handle->num_q_result = 1;
  srv_handle->max_row = q_result->tuple_count;

  return srv_h_id;

error:
  if (srv_handle)
    {
      hm_srv_handle_free (srv_h_id);
    }
  return err_code;
}

extern void *jsp_get_db_result_set (int h_id);
extern void jsp_srv_handle_free (int h_id);

static int
ux_use_sp_out (int srv_h_id)
{
  T_SRV_HANDLE *srv_handle;
  T_QUERY_RESULT *q_result;
  DB_QUERY_TYPE *column_info;
  int new_handle_id = 0;

  srv_handle = (T_SRV_HANDLE *) jsp_get_db_result_set (srv_h_id);
  if (srv_handle == NULL || srv_handle->cur_result == NULL)
    {
      jsp_srv_handle_free (srv_h_id);
      return CAS_ER_SRV_HANDLE;
    }

  q_result = (T_QUERY_RESULT *) srv_handle->cur_result;
  if (srv_handle->session != NULL && q_result->stmt_id >= 0)
    {
      column_info =
	db_get_query_type_list ((DB_SESSION *) srv_handle->session,
				q_result->stmt_id);
    }
  else
    {
      column_info = NULL;
    }

  if (q_result->result != NULL && column_info != NULL)
    {
      new_handle_id =
	create_srv_handle_with_query_result ((DB_QUERY_RESULT *) q_result->
					     result, q_result->num_column,
					     column_info, q_result->stmt_type,
					     SRV_HANDLE_QUERY_SEQ_NUM
					     (srv_handle));
      if (new_handle_id > 0)
	{
	  q_result->copied = TRUE;
	}
      else
	{
	  FREE_MEM (column_info);
	}
    }

  jsp_srv_handle_free (srv_h_id);

  return new_handle_id;
}

int
ux_get_generated_keys (T_SRV_HANDLE * srv_handle, T_NET_BUF * net_buf)
{
  T_NET_BUF *tuple_buf, temp_buf;
  DB_OBJECT *obj;
  DB_OBJECT *class_obj;
  DB_ATTRIBUTE *attributes, *attr;
  DB_VALUE oid_val, value;
  const char *attr_name = "";
  char updatable_flag = TRUE;
  int err_code;
  int num_col_offset, num_cols = 0;
  T_OBJECT t_object_autoincrement;

  tuple_buf = &temp_buf;
  net_buf_init (tuple_buf);
  err_code =
    db_query_get_tuple_value ((DB_QUERY_RESULT *) srv_handle->q_result->
			      result, 0, &oid_val);
  if (err_code < 0)
    {
      DB_ERR_MSG_SET (net_buf, err_code);
      goto error2;
    }

  obj = db_get_object (&oid_val);
  uobj_to_cas_obj (obj, &t_object_autoincrement);

  class_obj = db_get_class (obj);
  attributes = db_get_attributes (class_obj);
  net_buf_cp_int (net_buf, 0, NULL);	/* result code */

  /* Result Set make */
  net_buf_cp_byte (net_buf, srv_handle->q_result->stmt_type);	/* commandTypeIs */
  net_buf_cp_int (net_buf, 1, NULL);	/* totalTupleNumber */
  net_buf_cp_byte (net_buf, updatable_flag);	/* isUpdatable */
  net_buf_cp_int (net_buf, num_cols, &num_col_offset);	/* columnNumber */

  for (attr = attributes; attr; attr = db_attribute_next (attr))
    {
      if (db_attribute_is_auto_increment (attr))
	{
	  DB_DOMAIN *domain;
	  char set_type;
	  short scale;
	  int precision;
	  int temp_type;

	  domain = db_attribute_domain (attr);
	  precision = db_domain_precision (domain);
	  scale = db_domain_scale (domain);
	  temp_type = db_domain_type (domain);
	  set_type = ux_db_type_to_cas_type (temp_type);

	  attr_name = (char *) db_attribute_name (attr);
	  err_code = db_get (obj, attr_name, &value);
	  if (err_code < 0)
	    {
	      DB_ERR_MSG_SET (net_buf, err_code);
	      goto error2;
	    }

	  net_buf_cp_byte (net_buf, set_type);
	  net_buf_cp_short (net_buf, scale);
	  net_buf_cp_int (net_buf, precision, NULL);
	  net_buf_cp_int (net_buf, strlen (attr_name) + 1, NULL);
	  net_buf_cp_str (net_buf, attr_name, strlen (attr_name) + 1);

	  /* tuple data */
	  dbval_to_net_buf (&value, tuple_buf, 1, 0, 0);
	  num_cols++;
	}
    }
  net_buf_overwrite_int (net_buf, num_col_offset, num_cols);

  /* UTuples make */
  net_buf_cp_int (net_buf, 1, NULL);	/* fetchedTupleNumber */
  net_buf_cp_int (net_buf, 1, NULL);	/* index */
  NET_BUF_CP_OBJECT (net_buf, &t_object_autoincrement);	/* readOID 8 byte */
  net_buf_cp_str (net_buf, tuple_buf->data + NET_BUF_HEADER_SIZE,
		  tuple_buf->data_size);
  net_buf_clear (tuple_buf);
  net_buf_destroy (tuple_buf);

  return 0;

/*
error1:
  net_buf_clear(net_buf);
  net_buf_cp_int(net_buf, err_code, NULL);
*/
error2:
  return err_code;
}

int
ux_make_out_rs (int srv_h_id, T_NET_BUF * net_buf, T_REQ_INFO * req_info)
{
  T_SRV_HANDLE *srv_handle;
  int err_code;
  T_QUERY_RESULT *q_result;
  char updatable_flag = FALSE;
  int i;
  DB_QUERY_TYPE *col;
  int new_handle_id = 0;
  T_BROKER_VERSION client_version = req_info->client_version;

  new_handle_id = ux_use_sp_out (srv_h_id);
  srv_handle = hm_find_srv_handle (new_handle_id);

  if (srv_handle == NULL || srv_handle->cur_result == NULL)
    {
      err_code = CAS_ER_SRV_HANDLE;
      goto error1;
    }

  q_result = (T_QUERY_RESULT *) srv_handle->cur_result;
  if (q_result->stmt_type != CUBRID_STMT_SELECT)
    {
      err_code = CAS_ER_SRV_HANDLE;
      goto error1;
    }

  net_buf_cp_int (net_buf, 0, NULL);	/* result code */

  net_buf_cp_int (net_buf, new_handle_id, NULL);
  net_buf_cp_byte (net_buf, q_result->stmt_type);
  net_buf_cp_int (net_buf, srv_handle->max_row, NULL);
  net_buf_cp_byte (net_buf, updatable_flag);
  net_buf_cp_int (net_buf, q_result->num_column, NULL);

  q_result->null_type_column = (char *) MALLOC (q_result->num_column);
  if (q_result->null_type_column == NULL)
    {
      err_code = CAS_ER_NO_MORE_MEMORY;
      goto error1;
    }

  for (i = 0, col = (DB_QUERY_TYPE *) q_result->column_info;
       i < q_result->num_column; i++, col = db_query_format_next (col))
    {
      char set_type, cas_type;
      int precision;
      short scale;
      const char *col_name, *attr_name, *class_name;
      DB_DOMAIN *domain;
      DB_TYPE db_type;

      if (col == NULL)
	{
	  err_code = CAS_ER_SRV_HANDLE;
	  goto error1;
	}

      q_result->null_type_column[i] = 0;

      if (stripped_column_name)
	col_name = db_query_format_name (col);
      else
	{
	  col_name = db_query_format_original_name (col);
	  if (strchr (col_name, '*') != NULL)
	    col_name = db_query_format_name (col);
	}
      class_name = db_query_format_class_name (col);
      attr_name = "";

      domain = db_query_format_domain (col);
      db_type = db_domain_type (domain);
      if (IS_SET_TYPE (db_type))
	{
	  set_type = get_set_domain (domain, &precision, &scale, NULL);
	  cas_type = CAS_TYPE_COLLECTION (db_type, set_type);
	}
      else
	{
	  cas_type = ux_db_type_to_cas_type (db_type);
	  precision = db_domain_precision (domain);
	  scale = (short) db_domain_scale (domain);
	}

      if (cas_type == CCI_U_TYPE_NULL)
	{
	  q_result->null_type_column[i] = 1;
	}

#ifndef LIBCAS_FOR_JSP
      if (shm_appl->max_string_length >= 0)
	{
	  if (precision < 0 || precision > shm_appl->max_string_length)
	    precision = shm_appl->max_string_length;
	}
#else
      /* precision = DB_MAX_STRING_LENGTH; */
#endif
      prepare_column_info_set (net_buf,
			       (char) cas_type,
			       scale,
			       precision,
			       col_name,
			       attr_name,
			       class_name,
			       (char) db_query_format_is_non_null (col),
			       client_version);
    }

  return 0;
error1:
  net_buf_clear (net_buf);
  net_buf_cp_int (net_buf, err_code, NULL);
/*
error2:
*/
  return err_code;
}

#if 0				/* FIXME - for NBD Benchmark */
static int
check_class_chn (T_SRV_HANDLE * srv_handle)
{
#ifdef LIBCAS_FOR_JSP
  return 0;
#else
  int i;
  DB_OBJECT **classes;

  if (!as_info->cur_statement_pooling)
    return 0;

  if (!srv_handle->is_pooled)
    return 0;

  if (srv_handle->num_classes <= 0
      || srv_handle->classes == NULL || srv_handle->classes_chn == NULL)
    return CAS_ER_STMT_POOLING;

  classes = (DB_OBJECT **) srv_handle->classes;
  for (i = 0; i < srv_handle->num_classes; i++)
    {
      if (classes[i] == NULL)
	return CAS_ER_STMT_POOLING;
      if (srv_handle->classes_chn[i] != db_chn (classes[i], DB_FETCH_READ))
	{
	  if (db_Connect_status != 1)	/* DB_CONNECTION_STATUS_CONNECTED */
	    return CAS_ER_DBSERVER_DISCONNECTED;
	  return CAS_ER_STMT_POOLING;
	}
    }
  return 0;
#endif
}
#endif

static int
get_client_result_cache_lifetime (DB_SESSION * session, int stmt_id)
{
#ifdef LIBCAS_FOR_JSP
  return -1;
#else

  bool jdbc_cache_is_hint;
  int jdbc_cache_life_time = shm_appl->jdbc_cache_life_time;

  if (shm_appl->jdbc_cache == 0
      || db_get_statement_type (session, stmt_id) != CUBRID_STMT_SELECT
      || cas_default_isolation_level == TRAN_REP_CLASS_REP_INSTANCE
      || cas_default_isolation_level == TRAN_SERIALIZABLE)
    {
      return -1;
    }

  jdbc_cache_is_hint = db_get_jdbccachehint (session, stmt_id,
					     &jdbc_cache_life_time);

  if (shm_appl->jdbc_cache_only_hint && !jdbc_cache_is_hint)
    return -1;

  return jdbc_cache_life_time;
#endif
}

int
ux_auto_commit (T_NET_BUF * net_buf, T_REQ_INFO * req_info)
{

#ifndef LIBCAS_FOR_JSP
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
      err_code = CAS_ER_INTERNAL;
    }

  if (err_code < 0)
    {
      DB_ERR_MSG_SET (net_buf, err_code);
      req_info->need_rollback = TRUE;
      errors_in_transaction++;
    }
  else
    {
      req_info->need_rollback = FALSE;
    }

  tran_timeout = ut_check_timeout (&tran_start_time,
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

  if (as_info->cur_keep_con != KEEP_CON_OFF)
    {
      if (as_info->cas_log_reset)
	{
	  cas_log_reset (broker_name, shm_as_index);
	}
      if (!ux_is_database_connected () || restart_is_needed ())
	{
	  return -1;
	}

      if (shm_appl->sql_log2 != as_info->cur_sql_log2)
	{
	  sql_log2_end (false);
	  as_info->cur_sql_log2 = shm_appl->sql_log2;
	  sql_log2_init (broker_name, shm_as_index, as_info->cur_sql_log2,
			 true);
	}
      return 0;
    }
#endif

  return -1;
}
