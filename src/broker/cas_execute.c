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
#else /* WINDOWS */
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#endif /* WINDOWS */
#include <assert.h>

#include "cas_db_inc.h"

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
#include "intl_support.h"
#include "language_support.h"
#include "unicode_support.h"
#include "transaction_cl.h"
#include "authenticate.h"
#include "trigger_manager.h"
#include "system_parameter.h"
#include "schema_manager.h"

#include "dbi.h"

#define QUERY_BUFFER_MAX                4096

#define FK_INFO_SORT_BY_PKTABLE_NAME	1
#define FK_INFO_SORT_BY_FKTABLE_NAME	2

typedef enum
{
  NONE_TOKENS,
  SQL_STYLE_COMMENT,
  C_STYLE_COMMENT,
  CPP_STYLE_COMMENT,
  SINGLE_QUOTED_STRING,
  DOUBLE_QUOTED_STRING
} STATEMENT_STATUS;

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

extern void histo_print (FILE * stream);
extern void histo_clear (void);

static int netval_to_dbval (void *type, void *value, DB_VALUE * db_val,
			    T_NET_BUF * net_buf, char desired_type);
static int cur_tuple (T_QUERY_RESULT * q_result, int max_col_size,
		      char sensitive_flag, DB_OBJECT * obj,
		      T_NET_BUF * net_buf);
static int dbval_to_net_buf (DB_VALUE * val, T_NET_BUF * net_buf, char flag,
			     int max_col_size, char column_type_flag);
static void dbobj_to_casobj (DB_OBJECT * obj, T_OBJECT * cas_obj);
static void casobj_to_dbobj (T_OBJECT * cas_obj, DB_OBJECT ** obj);
static void dblob_to_caslob (DB_VALUE * lob, T_LOB_HANDLE * cas_lob);
static void caslob_to_dblob (T_LOB_HANDLE * cas_lob, DB_VALUE * lob);
static int get_attr_name (DB_OBJECT * obj, char ***ret_attr_name);
static int get_attr_name_from_argv (int argc, void **argv,
				    char ***ret_attr_name);
static int oid_attr_info_set (T_NET_BUF * net_buf, DB_OBJECT * obj,
			      int num_attr, char **attr_name);
static int oid_data_set (T_NET_BUF * net_buf, DB_OBJECT * obj,
			 int attr_num, char **attr_name);
static int prepare_column_list_info_set (DB_SESSION * session,
					 char prepare_flag,
					 T_QUERY_RESULT * q_result,
					 T_NET_BUF * net_buf,
					 T_BROKER_VERSION client_version);
static void prepare_column_info_set (T_NET_BUF * net_buf, char ut,
				     short scale, int prec,
				     const char *col_name,
				     const char *default_value,
				     char auto_increment, char unique_key,
				     char primary_key, char reverse_index,
				     char reverse_unique, char foreign_key,
				     char shared, const char *attr_name,
				     const char *class_name, char nullable,
				     T_BROKER_VERSION client_version);
static void set_column_info (T_NET_BUF * net_buf, char ut, short scale,
			     int prec, const char *col_name,
			     const char *attr_name, const char *class_name,
			     char is_non_null,
			     T_BROKER_VERSION client_version);

/*
  fetch_xxx prototype:
  fetch_xxx(T_SRV_HANDLE *, int cursor_pos, int fetch_count, char fetch_flag,
	    int result_set_idx, T_NET_BUF *);
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
static int fetch_foreign_keys (T_SRV_HANDLE *, int, int, char, int,
			       T_NET_BUF *, T_REQ_INFO *);
static void add_res_data_bytes (T_NET_BUF * net_buf, char *str, int size,
				char type, int *net_size);
static void add_res_data_string (T_NET_BUF * net_buf, const char *str,
				 int size, char type, int *net_size);
static void add_res_data_string_safe (T_NET_BUF * net_buf, const char *str,
				      char type, int *net_size);
static void add_res_data_int (T_NET_BUF * net_buf, int value, char type,
			      int *net_size);
static void add_res_data_bigint (T_NET_BUF * net_buf, DB_BIGINT value,
				 char type, int *net_size);
static void add_res_data_short (T_NET_BUF * net_buf, short value, char type,
				int *net_size);
static void add_res_data_float (T_NET_BUF * net_buf, float value, char type,
				int *net_size);
static void add_res_data_double (T_NET_BUF * net_buf, double value, char type,
				 int *net_size);
static void add_res_data_timestamp (T_NET_BUF * net_buf, short yr, short mon,
				    short day, short hh, short mm, short ss,
				    char type, int *net_size);
static void add_res_data_datetime (T_NET_BUF * net_buf, short yr, short mon,
				   short day, short hh, short mm, short ss,
				   short ms, char type, int *net_size);
static void add_res_data_time (T_NET_BUF * net_buf, short hh, short mm,
			       short ss, char type, int *net_size);
static void add_res_data_date (T_NET_BUF * net_buf, short yr, short mon,
			       short day, char type, int *net_size);
static void add_res_data_object (T_NET_BUF * net_buf, T_OBJECT * obj,
				 char type, int *net_size);
static void add_res_data_lob_handle (T_NET_BUF * net_buf, T_LOB_HANDLE * lob,
				     char type, int *net_size);
static void trigger_event_str (DB_TRIGGER_EVENT trig_event, char *buf);
static void trigger_status_str (DB_TRIGGER_STATUS trig_status, char *buf);
static void trigger_time_str (DB_TRIGGER_TIME trig_time, char *buf);

static int get_num_markers (char *stmt);
static char *consume_tokens (char *stmt, STATEMENT_STATUS stmt_status);
static char get_stmt_type (char *stmt);
static int execute_info_set (T_SRV_HANDLE * srv_handle, T_NET_BUF * net_buf,
			     T_BROKER_VERSION client_version, char exec_flag);
static char get_attr_type (DB_OBJECT * obj_p, char *attr_name);

static char *get_domain_str (DB_DOMAIN * domain);

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
static void sch_trigger (T_NET_BUF * net_buf, char *class_name, char flag,
			 void **result);
static int sch_class_priv (T_NET_BUF * net_buf, char *class_name,
			   char pat_flag, T_SRV_HANDLE * srv_handle);
static int sch_attr_priv (T_NET_BUF * net_buf, char *class_name,
			  char *attr_name, char pat_flag,
			  T_SRV_HANDLE * srv_handle);
static int sch_direct_super_class (T_NET_BUF * net_buf, char *class_name,
				   int pattern_flag,
				   T_SRV_HANDLE * srv_handle);
static int sch_imported_keys (T_NET_BUF * net_buf, char *class_name,
			      void **result);
static int sch_exported_keys_or_cross_reference (T_NET_BUF * net_buf,
						 bool find_cross_ref,
						 char *pktable_name,
						 char *fktable_name,
						 void **result);
static int class_type (DB_OBJECT * class_obj);
static int class_attr_info (char *class_name, DB_ATTRIBUTE * attr,
			    char *attr_pattern, char pat_flag,
			    T_ATTR_TABLE * attr_table);
static int set_priv_table (unsigned int class_priv, char *name,
			   T_PRIV_TABLE * priv_table, int index);
static int sch_query_execute (T_SRV_HANDLE * srv_handle, char *sql_stmt,
			      T_NET_BUF * net_buf);
static int sch_primary_key (T_NET_BUF * net_buf, char *class_name,
			    T_SRV_HANDLE * srv_handle);
static short constraint_dbtype_to_castype (int db_const_type);

static T_PREPARE_CALL_INFO *make_prepare_call_info (int num_args,
						    int is_first_out);
static void prepare_call_info_dbval_clear (T_PREPARE_CALL_INFO * call_info);
static int fetch_call (T_SRV_HANDLE * srv_handle, T_NET_BUF * net_buf);
#define check_class_chn(s) 0
static int get_client_result_cache_lifetime (DB_SESSION * session,
					     int stmt_id);
static bool has_stmt_result_set (char stmt_type);
#ifndef LIBCAS_FOR_JSP
static bool check_auto_commit_after_fetch_done (T_SRV_HANDLE * srv_handle);
#endif /* !LIBCAS_FOR_JSP */

static char *convert_db_value_to_string (DB_VALUE * value,
					 DB_VALUE * value_string);
static void serialize_collection_as_string (DB_VALUE * col, char **out);
static void add_fk_info_before (T_FK_INFO_RESULT * pivot,
				T_FK_INFO_RESULT * new);
static void add_fk_info_after (T_FK_INFO_RESULT * pivot,
			       T_FK_INFO_RESULT * new);
static T_FK_INFO_RESULT *add_fk_info_result (T_FK_INFO_RESULT * fk_res,
					     const char *pktable_name,
					     const char *pkcolumn_name,
					     const char *fktable_name,
					     const char *fkcolumn_name,
					     short key_seq,
					     SM_FOREIGN_KEY_ACTION
					     update_action,
					     SM_FOREIGN_KEY_ACTION
					     delete_action,
					     const char *fk_name,
					     const char *pk_name,
					     int sort_by);

static char *get_backslash_escape_string (void);

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
  CCI_U_TYPE_BLOB,		/* 33 */
  CCI_U_TYPE_CLOB,		/* 34 */
  CCI_U_TYPE_STRING		/* 35 */
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
  fetch_result,			/* SCH_PRIMARY_KEY */
  fetch_foreign_keys,		/* SCH_IMPORTED_KEYS */
  fetch_foreign_keys,		/* SCH_EXPORTED_KEYS */
  fetch_foreign_keys,		/* SCH_CROSS_REFERENCE */
};

#if defined(CURBID_SAHRD)
static char database_name[MAX_HA_DBNAME_LENGTH] = "";
#else /* CUBRID_SHARD */
static char database_name[SRV_CON_DBNAME_SIZE] = "";
#endif /* !CUBRID_SHARD */
static char database_user[SRV_CON_DBUSER_SIZE] = "";
static char database_passwd[SRV_CON_DBPASSWD_SIZE] = "";
static char cas_db_sys_param[128] = "";

/*****************************
  move from cas_log.c
 *****************************/
/* log error handler related fields */
typedef struct cas_error_log_handle_context_s CAS_ERROR_LOG_HANDLE_CONTEXT;
struct cas_error_log_handle_context_s
{
  unsigned int from;
  unsigned int to;
};
static CAS_ERROR_LOG_HANDLE_CONTEXT *cas_EHCTX = NULL;

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
#if defined(CUBRID_SHARD)
	      char dbname[MAX_HA_DBNAME_LENGTH];
#else /* CUBRID_SHARD */
	      char dbname[SRV_CON_DBNAME_SIZE];
#endif /* !CUBRID_SHARD */
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
#endif /* !LIBCAS_FOR_JSP */
  return 0;
}

#ifndef LIBCAS_FOR_JSP
SESSION_ID
ux_get_session_id (void)
{
  return db_get_session_id ();
}

void
ux_set_session_id (const SESSION_ID session_id)
{
  db_set_session_id (session_id);
}

int
ux_database_connect (char *db_name, char *db_user, char *db_passwd,
		     char **db_err_msg)
{
  int err_code, client_type;
  char *p;
  const char *host_connected = NULL;

  if (db_name == NULL || db_name[0] == '\0')
    {
      return ERROR_INFO_SET (-1, CAS_ERROR_INDICATOR);
    }

  host_connected = db_get_host_connected ();

  if (get_db_connect_status () != 1	/* DB_CONNECTION_STATUS_CONNECTED */
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
      else if (shm_appl->access_mode == PH_READ_ONLY_ACCESS_MODE)
	{
	  client_type = 11;	/* DB_CLIENT_TYPE_PREFERRED_HOST_BROKER in db.h */
	  cas_log_debug (ARG_FILE_LINE,
			 "ux_database_connect: preferred_host_broker");
	}
      else
	{
	  client_type = 4;	/* DB_CLIENT_TYPE_BROKER in db.h */
	}
      err_code = db_restart_ex (program_name, db_name, db_user, db_passwd,
				shm_appl->preferred_hosts, client_type);
      if (err_code < 0)
	{
	  goto connect_error;
	}
      cas_log_debug (ARG_FILE_LINE,
		     "ux_database_connect: db_login(%s) db_restart(%s) at %s",
		     db_user, db_name, host_connected);
#if defined(CUBRID_SHARD)
      strncpy (as_info->database_name, db_name, MAX_HA_DBNAME_LENGTH - 1);
#else /* CUBRID_SHARD */
      strncpy (as_info->database_name, db_name, SRV_CON_DBNAME_SIZE - 1);
#endif /* !CUBRID_SHARD */
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

      err_code = au_login (db_user, db_passwd, true);
      if (err_code < 0)
	{
	  ux_database_shutdown ();

	  return ux_database_connect (db_name, db_user, db_passwd,
				      db_err_msg);
	}
      db_check_session ();

      strncpy (database_user, db_user, sizeof (database_user) - 1);
      strncpy (database_passwd, db_passwd, sizeof (database_passwd) - 1);
    }
  else
    {
      /* check session to see if it is still active */
      db_check_session ();
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
  return ERROR_INFO_SET_WITH_MSG (err_code, DBMS_ERROR_INDICATOR, p);
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
  else if (shm_appl->access_mode == PH_READ_ONLY_ACCESS_MODE)
    {
      client_type = 11;		/* DB_CLIENT_TYPE_PREFERRED_HOST_BROKER in db.h */
      cas_log_debug (ARG_FILE_LINE,
		     "ux_database_connect: preferred_host_broker");
    }
  else
    {
      client_type = 4;		/* DB_CLIENT_TYPE_BROKER in db.h */
    }
  err_code = db_restart_ex (program_name, database_name, database_user,
			    database_passwd, shm_appl->preferred_hosts,
			    client_type);

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
#endif /* !LIBCAS_FOR_JSP */

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

  cas_default_ansi_quotes = true;

  ux_get_system_parameter ("ansi_quotes", &cas_default_ansi_quotes);

  cas_default_no_backslash_escapes = true;

  ux_get_system_parameter ("no_backslash_escapes",
			   &cas_default_no_backslash_escapes);

  return;
}

void
ux_get_system_parameter (const char *param, bool * value)
{
  int err_code = 0;
  char buffer[LINE_MAX], *p;

  strncpy (buffer, param, LINE_MAX);
  err_code = db_get_system_parameters (buffer, LINE_MAX);
  if (err_code != NO_ERROR)
    {
      return;
    }

  p = strchr (buffer, '=');
  if (p == NULL)
    {
      return;
    }

  if (*(p + 1) == 'n')
    {
      *value = false;
    }
  else
    {
      *value = true;
    }

  return;
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
#endif /* !LIBCAS_FOR_JSP */
  memset (database_name, 0, sizeof (database_name));
  memset (database_user, 0, sizeof (database_user));
  memset (database_passwd, 0, sizeof (database_passwd));
  cas_default_isolation_level = 0;
  cas_default_lock_timeout = -1;
}

int
ux_prepare (char *sql_stmt, int flag, char auto_commit_mode,
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

  if ((flag & CCI_PREPARE_UPDATABLE) && (flag & CCI_PREPARE_HOLDABLE))
    {
      /* do not allow updatable, holdable results */
      err_code = ERROR_INFO_SET (CAS_ER_HOLDABLE_NOT_ALLOWED,
				 CAS_ERROR_INDICATOR);
      goto prepare_error;
    }

#ifndef LIBCAS_FOR_JSP
  if ((flag & CCI_PREPARE_HOLDABLE)
      && (as_info->cur_keep_con == KEEP_CON_OFF))
    {
      err_code = ERROR_INFO_SET (CAS_ER_HOLDABLE_NOT_ALLOWED_KEEP_CON_OFF,
				 CAS_ERROR_INDICATOR);
      goto prepare_error;
    }
#endif

  srv_h_id = hm_new_srv_handle (&srv_handle, query_seq_num);
  if (srv_h_id < 0)
    {
      err_code = srv_h_id;
      goto prepare_error;
    }
  srv_handle->schema_type = -1;
  srv_handle->auto_commit_mode = auto_commit_mode;

  ALLOC_COPY (srv_handle->sql_stmt, sql_stmt);
  if (srv_handle->sql_stmt == NULL)
    {
      err_code = ERROR_INFO_SET (CAS_ER_NO_MORE_MEMORY, CAS_ERROR_INDICATOR);
      goto prepare_error;
    }

  sql_stmt = srv_handle->sql_stmt;

  if (flag & CCI_PREPARE_QUERY_INFO)
    {
      cas_log_query_info_init (srv_handle->id, FALSE);
      srv_handle->query_info_flag = TRUE;
    }
  else
    {
      srv_handle->query_info_flag = FALSE;
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
	      err_code =
		ERROR_INFO_SET (CAS_ER_INVALID_CALL_STMT,
				CAS_ERROR_INDICATOR);
	      goto prepare_error;
	    }

	  tmp++;
	}

      ut_trim (tmp);
      stmt_type = get_stmt_type (tmp);
      if (stmt_type != CUBRID_STMT_CALL)
	{
	  err_code =
	    ERROR_INFO_SET (CAS_ER_INVALID_CALL_STMT, CAS_ERROR_INDICATOR);
	  goto prepare_error;
	}

      session = db_open_buffer (tmp);
      if (!session)
	{
	  err_code =
	    ERROR_INFO_SET (CAS_ER_NO_MORE_MEMORY, CAS_ERROR_INDICATOR);
	  goto prepare_error;
	}

      stmt_id = db_compile_statement (session);
      if (stmt_id < 0)
	{
	  err_code = ERROR_INFO_SET (stmt_id, DBMS_ERROR_INDICATOR);
	  goto prepare_error;
	}

      num_markers = get_num_markers (sql_stmt);
      stmt_type = CUBRID_STMT_CALL_SP;
      srv_handle->is_prepared = TRUE;

      prepare_call_info = make_prepare_call_info (num_markers, is_first_out);
      if (prepare_call_info == NULL)
	{
	  err_code =
	    ERROR_INFO_SET (CAS_ER_NO_MORE_MEMORY, CAS_ERROR_INDICATOR);
	  goto prepare_error;
	}
      srv_handle->prepare_call_info = prepare_call_info;

      goto prepare_result_set;
    }

  session = db_open_buffer (sql_stmt);
  if (!session)
    {
      err_code = ERROR_INFO_SET (CAS_ER_NO_MORE_MEMORY, CAS_ERROR_INDICATOR);
      goto prepare_error;
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
	  err_code = ERROR_INFO_SET (stmt_id, DBMS_ERROR_INDICATOR);
	  goto prepare_error;
	}
      srv_handle->is_prepared = FALSE;
    }
  else
    {
      num_markers = get_num_markers (sql_stmt);
      stmt_type = db_get_statement_type (session, stmt_id);
      srv_handle->is_prepared = TRUE;
    }

  if (has_stmt_result_set (stmt_type) && (flag & CCI_PREPARE_HOLDABLE))
    {
      hm_srv_handle_set_holdable (srv_handle);
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
      err_code = ERROR_INFO_SET (CAS_ER_NO_MORE_MEMORY, CAS_ERROR_INDICATOR);
      goto prepare_error;
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
      goto prepare_error;
    }

  srv_handle->session = (void *) session;

  srv_handle->q_result = q_result;
  srv_handle->num_q_result = 1;
  srv_handle->cur_result = NULL;
  srv_handle->cur_result_index = 0;

  db_get_cacheinfo (session, stmt_id, &srv_handle->use_plan_cache,
		    &srv_handle->use_query_cache);

  return srv_h_id;

prepare_error:
  NET_BUF_ERR_SET (net_buf);
#ifndef LIBCAS_FOR_JSP
  if (auto_commit_mode == TRUE)
    {
      req_info->need_auto_commit = TRAN_AUTOROLLBACK;
    }
#endif /* !LIBCAS_FOR_JSP */
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
  if (shm_appl->select_auto_commit == ON)
    {
      hm_srv_handle_reset_active ();
    }

  if (!as_info->cur_statement_pooling)
    {
      if (tran_type == CCI_TRAN_COMMIT)
	{
	  hm_srv_handle_free_all (false);
	}
      else
	{
	  hm_srv_handle_free_all (true);
	}
    }
  else
    {
      if (tran_type == CCI_TRAN_COMMIT)
	{
	  /* do not close holdable results on commit */
	  hm_srv_handle_qresult_end_all (false);
	}
      else
	{
	  /* clear all queries */
	  hm_srv_handle_qresult_end_all (true);
	}
    }

#else /* !LIBCAS_FOR_JSP */
  hm_srv_handle_free_all (true);
#endif /* !LIBCAS_FOR_JSP */

  if (tran_type == CCI_TRAN_COMMIT)
    {
      err_code = db_commit_transaction ();
      cas_log_debug (ARG_FILE_LINE,
		     "ux_end_tran: db_commit_transaction() = %d", err_code);
      if (err_code < 0)
	{
	  err_code = ERROR_INFO_SET (err_code, DBMS_ERROR_INDICATOR);
	}
    }
  else if (tran_type == CCI_TRAN_ROLLBACK)
    {
      err_code = db_abort_transaction ();
      cas_log_debug (ARG_FILE_LINE,
		     "ux_end_tran: db_abort_transaction() = %d", err_code);
      if (err_code < 0)
	{
	  err_code = ERROR_INFO_SET (err_code, DBMS_ERROR_INDICATOR);
	}
    }
  else
    {
      err_code = ERROR_INFO_SET (CAS_ER_INTERNAL, CAS_ERROR_INDICATOR);
    }

  if (err_code >= 0)
    {
      unset_xa_prepare_flag ();
#ifndef LIBCAS_FOR_JSP
      if (reset_con_status)
	{
	  assert_release (as_info->con_status == CON_STATUS_IN_TRAN
	                  || as_info->con_status == CON_STATUS_OUT_TRAN);
	  as_info->con_status = CON_STATUS_OUT_TRAN;
	  as_info->transaction_start_time = (time_t) 0;
	}
#endif /* !LIBCAS_FOR_JSP */
    }
  else
    {
      errors_in_transaction++;
    }

#ifndef LIBCAS_FOR_JSP
  if (get_db_connect_status () == -1 /* DB_CONNECTION_STATUS_RESET */ )
    {
      as_info->reset_flag = TRUE;
    }
#endif /* !LIBCAS_FOR_JSP */

  return err_code;
}

int
ux_end_session (void)
{
  return db_end_session ();
}

int
ux_get_row_count (T_NET_BUF * net_buf)
{
  int row_count = 0;
  row_count = db_get_row_count_cache ();
  if (row_count == DB_ROW_COUNT_NOT_SET)
    {
      /* get the value from the server */
      int err = db_get_row_count (&row_count);
      if (err != NO_ERROR)
	{
	  err = ERROR_INFO_SET (err, DBMS_ERROR_INDICATOR);
	  NET_BUF_ERR_SET (net_buf);
	  errors_in_transaction++;
	  return err;
	}
    }
  net_buf_cp_int (net_buf, NO_ERROR, NULL);	/* result code */
  net_buf_cp_int (net_buf, row_count, NULL);

  return NO_ERROR;
}

int
ux_get_last_insert_id (T_NET_BUF * net_buf)
{
  int err = NO_ERROR;
  DB_VALUE lid;
  DB_MAKE_NULL (&lid);

  err = db_get_last_insert_id (&lid);
  if (err != NO_ERROR)
    {
      err = ERROR_INFO_SET (err, DBMS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (net_buf);
      errors_in_transaction++;
      return err;
    }

  net_buf_cp_int (net_buf, NO_ERROR, NULL);	/* result code */
  dbval_to_net_buf (&lid, net_buf, 1, 0, 1);

  db_value_clear (&lid);

  return NO_ERROR;
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

  hm_qresult_end (srv_handle, FALSE);

  if (srv_handle->is_prepared == TRUE
      && srv_handle->query_info_flag == FALSE && (flag & CCI_EXEC_QUERY_INFO))
    {
      /* A statement was already prepared, but an user wants to see a plan
       * for the statement after execution. See the following Java codes:
       *
       *   PreparedStatement ps = con.prepareStatement("...");
       *   ((CUBRIDPreparedStatement) ps).setQueryInfo(true);
       *   ps.executeQuery();
       *   String plan = ((CUBRIDPreparedStatement) ps).getQueryplan();
       *
       * In this case, we just recompile the statement at execution time
       * and writes its plan to a temporary plan dump file.
       */
      srv_handle->is_prepared = FALSE;
    }

  if (srv_handle->is_prepared == FALSE)
    {
      hm_session_free (srv_handle);

      if (!(session = db_open_buffer (srv_handle->sql_stmt)))
	{
	  err_code =
	    ERROR_INFO_SET (CAS_ER_NO_MORE_MEMORY, CAS_ERROR_INDICATOR);
	  goto execute_error;
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
	  goto execute_error;
	}

      err_code = db_push_values (session, num_bind, value_list);
      if (err_code != NO_ERROR)
	{
	  err_code = ERROR_INFO_SET (err_code, DBMS_ERROR_INDICATOR);
	  goto execute_error;
	}
    }

  if (srv_handle->is_prepared == FALSE)
    {
      if (flag & CCI_EXEC_QUERY_INFO)
	{
	  cas_log_query_info_init (srv_handle->id,
				   flag & CCI_EXEC_ONLY_QUERY_PLAN);
	  srv_handle->query_info_flag = TRUE;
	}

      stmt_id = db_compile_statement (session);
      if (stmt_id < 0)
	{
	  err_code = ERROR_INFO_SET (stmt_id, DBMS_ERROR_INDICATOR);
	  goto execute_error;
	}
    }
  else
    {
      if (flag & CCI_EXEC_ONLY_QUERY_PLAN)
	{
	  set_optimization_level (514);
	}

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

  db_session_set_holdable (srv_handle->session, srv_handle->is_holdable);
  srv_handle->is_from_current_transaction = true;
#if 0				/* yaw */
  if ((err_code = check_class_chn (srv_handle)) < 0)
    {
      goto execute_error1;
    }
#endif

#ifndef LIBCAS_FOR_JSP
  if (!(flag & CCI_EXEC_QUERY_INFO))
    {
      SQL_LOG2_EXEC_BEGIN (as_info->cur_sql_log2, stmt_id);
    }
#endif /* !LIBCAS_FOR_JSP */

  if (clt_cache_time)
    {
      db_set_client_cache_time (session, stmt_id, clt_cache_time);
    }

  n = db_execute_and_keep_statement (session, stmt_id, &result);
#ifndef LIBCAS_FOR_JSP
  as_info->num_queries_processed %= MAX_DIAG_DATA_VALUE;
  as_info->num_queries_processed++;
#endif /* !LIBCAS_FOR_JSP */
  if (n < 0)
    {
      err_code = ERROR_INFO_SET (n, DBMS_ERROR_INDICATOR);
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
	      err_code = ERROR_INFO_SET (n, DBMS_ERROR_INDICATOR);
	    }
	}
    }
#endif

  if (flag & CCI_EXEC_QUERY_INFO)
    {
#ifndef LIBCAS_FOR_JSP
      SQL_LOG2_EXEC_APPEND (as_info->cur_sql_log2, stmt_id, n,
			    cas_log_query_plan_file (srv_handle->id));
#endif /* !LIBCAS_FOR_JSP */
      set_optimization_level (1);
    }
#ifndef LIBCAS_FOR_JSP
  else
    {
      SQL_LOG2_EXEC_END (as_info->cur_sql_log2, stmt_id, n);
    }
#endif /* !LIBCAS_FOR_JSP */

  if (n < 0)
    {
      if (srv_handle->is_pooled
	  && (n == ER_QPROC_INVALID_XASLNODE || n == ER_HEAP_UNKNOWN_OBJECT))
	{
	  err_code =
	    ERROR_INFO_SET_FORCE (CAS_ER_STMT_POOLING, CAS_ERROR_INDICATOR);
	  goto execute_error;
	}
      err_code = ERROR_INFO_SET (n, DBMS_ERROR_INDICATOR);
      goto execute_error;
    }

  if ((!(flag & CCI_EXEC_ASYNC))
      && (max_row > 0)
      && (db_get_statement_type (session, stmt_id) == CUBRID_STMT_SELECT)
      && *clt_cache_reusable == FALSE)
    {
      err_code = db_query_seek_tuple (result, max_row, 1);
      if (err_code < 0)
	{
	  err_code = ERROR_INFO_SET (err_code, DBMS_ERROR_INDICATOR);
	  goto execute_error;
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
  srv_handle->num_q_result = 1;
  srv_handle->q_result->result = (void *) result;
  srv_handle->q_result->tuple_count = n;
  srv_handle->cur_result = (void *) srv_handle->q_result;
  srv_handle->cur_result_index = 1;
  srv_handle->max_row = max_row;

  db_get_cacheinfo (session, stmt_id, &srv_handle->use_plan_cache,
		    &srv_handle->use_query_cache);

#ifndef LIBCAS_FOR_JSP
  if (has_stmt_result_set (srv_handle->q_result->stmt_type) == false
      && srv_handle->auto_commit_mode == TRUE)
    {
      req_info->need_auto_commit = TRAN_AUTOCOMMIT;
    }
#endif /* !LIBCAS_FOR_JSP */

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

  err_code = execute_info_set (srv_handle, net_buf, client_version, flag);
  if (err_code != NO_ERROR)
    {
      goto execute_error;
    }

  if (DOES_CLIENT_UNDERSTAND_THE_PROTOCOL (client_version, PROTOCOL_V2))
    {
      int result_cache_lifetime;
      char include_column_info;

      if (db_check_single_query (session) == NO_ERROR)
	{
	  include_column_info = 0;
	}
      else
	{
	  include_column_info = 1;
	}

      net_buf_cp_byte (net_buf, include_column_info);
      if (include_column_info == 1)
	{
	  result_cache_lifetime = get_client_result_cache_lifetime (session,
								    stmt_id);

	  net_buf_cp_int (net_buf, result_cache_lifetime, NULL);
	  net_buf_cp_byte (net_buf, srv_handle->q_result->stmt_type);
	  net_buf_cp_int (net_buf, srv_handle->num_markers, NULL);
	  err_code = prepare_column_list_info_set (session,
						   srv_handle->prepare_flag,
						   srv_handle->q_result,
						   net_buf, client_version);
	  if (err_code != NO_ERROR)
	    {
	      goto execute_error;
	    }
	}
    }

  return err_code;

execute_error:
  NET_BUF_ERR_SET (net_buf);
#ifndef LIBCAS_FOR_JSP
  if (srv_handle->auto_commit_mode)
    {
      req_info->need_auto_commit = TRAN_AUTOROLLBACK;
    }
#endif /* !LIBCAS_FOR_JSP */
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
	  err_code = ERROR_INFO_SET (db_error_code (), DBMS_ERROR_INDICATOR);
	  goto execute_all_error;
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
	  goto execute_all_error;
	}

      err_code = db_push_values (session, num_bind, value_list);
      if (err_code != NO_ERROR)
	{
	  err_code = ERROR_INFO_SET (err_code, DBMS_ERROR_INDICATOR);
	  goto execute_all_error;
	}
    }

#if 0				/* yaw */
  if ((err_code = check_class_chn (srv_handle)) < 0)
    {
      goto execute_all_err_cas;
    }
#endif

  q_res_idx = 0;
  db_rewind_statement (session);
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
	      err_code = ERROR_INFO_SET (stmt_id, DBMS_ERROR_INDICATOR);
	      goto execute_all_error;
	    }

	  if ((stmt_type = db_get_statement_type (session, stmt_id)) < 0)
	    {
	      err_code = ERROR_INFO_SET (stmt_type, DBMS_ERROR_INDICATOR);
	      goto execute_all_error;
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

      db_session_set_holdable (srv_handle->session, srv_handle->is_holdable);
      srv_handle->is_from_current_transaction = true;

      if (clt_cache_time)
	db_set_client_cache_time (session, stmt_id, clt_cache_time);

#ifndef LIBCAS_FOR_JSP
      SQL_LOG2_EXEC_BEGIN (as_info->cur_sql_log2, stmt_id);
#endif /* !LIBCAS_FOR_JSP */
      n = db_execute_and_keep_statement (session, stmt_id, &result);
#ifndef LIBCAS_FOR_JSP
      as_info->num_queries_processed %= MAX_DIAG_DATA_VALUE;
      as_info->num_queries_processed++;
#endif /* !LIBCAS_FOR_JSP */
#ifndef LIBCAS_FOR_JSP
      SQL_LOG2_EXEC_END (as_info->cur_sql_log2, stmt_id, n);
#endif /* !LIBCAS_FOR_JSP */
      if (n < 0)
	{
	  if (srv_handle->is_pooled
	      && (n == ER_QPROC_INVALID_XASLNODE
		  || n == ER_HEAP_UNKNOWN_OBJECT))
	    {
	      err_code =
		ERROR_INFO_SET_FORCE (CAS_ER_STMT_POOLING,
				      CAS_ERROR_INDICATOR);
	      goto execute_all_error;
	    }
	  err_code = ERROR_INFO_SET (n, DBMS_ERROR_INDICATOR);
	  goto execute_all_error;
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
		  err_code =
		    ERROR_INFO_SET (tmp_err_code, DBMS_ERROR_INDICATOR);
		  goto execute_all_error;
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
	      err_code = ERROR_INFO_SET (err_code, DBMS_ERROR_INDICATOR);
	      goto execute_all_error;
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
	  err_code =
	    ERROR_INFO_SET (CAS_ER_NO_MORE_MEMORY, CAS_ERROR_INDICATOR);
	  goto execute_all_error;
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
      && has_stmt_result_set (srv_handle->q_result->stmt_type) == false
      && srv_handle->auto_commit_mode == TRUE)
    {
      req_info->need_auto_commit = TRAN_AUTOCOMMIT;
    }
#endif /* !LIBCAS_FOR_JSP */

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

  err_code = execute_info_set (srv_handle, net_buf, client_version, flag);
  if (err_code != NO_ERROR)
    {
      goto execute_all_error;
    }

  if (DOES_CLIENT_UNDERSTAND_THE_PROTOCOL (client_version, PROTOCOL_V2))
    {
      int result_cache_lifetime;
      char include_column_info;

      if (db_check_single_query (session) == NO_ERROR)
	{
	  include_column_info = 0;
	}
      else
	{
	  include_column_info = 1;
	}

      net_buf_cp_byte (net_buf, include_column_info);
      if (include_column_info == 1)
	{
	  result_cache_lifetime = get_client_result_cache_lifetime (session,
								    stmt_id);

	  net_buf_cp_int (net_buf, result_cache_lifetime, NULL);
	  net_buf_cp_byte (net_buf, srv_handle->q_result[0].stmt_type);
	  net_buf_cp_int (net_buf, srv_handle->num_markers, NULL);
	  err_code = prepare_column_list_info_set (session,
						   srv_handle->prepare_flag,
						   &srv_handle->q_result[0],
						   net_buf, client_version);
	  if (err_code != NO_ERROR)
	    {
	      goto execute_all_error;
	    }
	}
    }

  return err_code;

execute_all_error:
  NET_BUF_ERR_SET (net_buf);
#ifndef LIBCAS_FOR_JSP
  if (srv_handle->auto_commit_mode)
    {
      req_info->need_auto_commit = TRAN_AUTOROLLBACK;
    }
#endif /* !LIBCAS_FOR_JSP */
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
	  goto execute_error;
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
#endif /* !LIBCAS_FOR_JSP */
  jsp_unset_prepare_call ();

  if (n < 0)
    {
      err_code = ERROR_INFO_SET (n, DBMS_ERROR_INDICATOR);
      goto execute_error;
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
  srv_handle->num_q_result = 1;
  srv_handle->q_result->result = (void *) result;
  srv_handle->q_result->tuple_count = n;
  srv_handle->cur_result = (void *) srv_handle->q_result;
  srv_handle->cur_result_index = 1;
  srv_handle->max_row = max_row;

  if (value_list)
    {
      for (i = 0; i < num_bind; i++)
	db_value_clear (&(value_list[i]));
      FREE_MEM (value_list);
    }

  net_buf_cp_byte (net_buf, 0);	/* client cache reusable - always false */

  err_code = execute_info_set (srv_handle, net_buf, client_version, flag);
  if (err_code != NO_ERROR)
    {
      goto execute_error;
    }

  if (DOES_CLIENT_UNDERSTAND_THE_PROTOCOL (client_version, PROTOCOL_V2))
    {
      int result_cache_lifetime;
      char include_column_info;

      if (db_check_single_query (session) == NO_ERROR)
	{
	  include_column_info = 0;
	}
      else
	{
	  include_column_info = 1;
	}

      net_buf_cp_byte (net_buf, include_column_info);
      if (include_column_info == 1)
	{
	  result_cache_lifetime = get_client_result_cache_lifetime (session,
								    stmt_id);

	  net_buf_cp_int (net_buf, result_cache_lifetime, NULL);
	  net_buf_cp_byte (net_buf, srv_handle->q_result->stmt_type);
	  net_buf_cp_int (net_buf, srv_handle->num_markers, NULL);
	  err_code = prepare_column_list_info_set (session,
						   srv_handle->prepare_flag,
						   srv_handle->q_result,
						   net_buf, client_version);
	  if (err_code != NO_ERROR)
	    {
	      goto execute_error;
	    }
	}
    }

  return err_code;

execute_error:
  NET_BUF_ERR_SET (net_buf);
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
      err_code = ERROR_INFO_SET (CAS_ER_SRV_HANDLE, CAS_ERROR_INDICATOR);
      goto next_result_error;
    }

  if (srv_handle->cur_result_index >= srv_handle->num_q_result)
    {
      err_code =
	ERROR_INFO_SET (CAS_ER_NO_MORE_RESULT_SET, CAS_ERROR_INDICATOR);
      goto next_result_error;
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
	  err_code = ERROR_INFO_SET (tmp_err_code, DBMS_ERROR_INDICATOR);
	  goto next_result_error;
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
      goto next_result_error;
    }

  srv_handle->cur_result = cur_result;
  (srv_handle->cur_result_index)++;

  return 0;

next_result_error:
  NET_BUF_ERR_SET (net_buf);
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

  net_arg_get_char (auto_commit_mode, argv[0]);
  argc--;			/* real query num is argc-1 (because auto commit) */

  net_buf_cp_int (net_buf, 0, NULL);	/* result code */

  net_buf_cp_int (net_buf, argc, NULL);	/* result msg. num_query */


#ifndef LIBCAS_FOR_JSP
  if (auto_commit_mode == TRUE)
    {
      req_info->need_auto_commit = TRAN_AUTOCOMMIT;
    }
#endif /* !LIBCAS_FOR_JSP */

  for (i = 1; i <= argc; i++)
    {
      use_plan_cache = false;
      use_query_cache = false;

      net_arg_get_str (&sql_stmt, &sql_size, argv[i]);

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
	      dbobj_to_casobj (ins_obj_p, &ins_oid);
	      db_value_clear (&val);
	    }
	  net_buf_cp_object (net_buf, &ins_oid);
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

      ERROR_INFO_SET_WITH_MSG (err_code, DBMS_ERROR_INDICATOR, err_msg);
      NET_BUF_ERR_SET (net_buf);

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

  net_arg_get_char (auto_commit_mode, argv[1]);

#ifndef LIBCAS_FOR_JSP
  if (auto_commit_mode == TRUE)
    {
      req_info->need_auto_commit = TRAN_AUTOCOMMIT;
    }
#endif /* LIBCAS_FOR_JSP */

  if (srv_handle == NULL || srv_handle->schema_type >= CCI_SCH_FIRST)
    {
      err_code = ERROR_INFO_SET (CAS_ER_SRV_HANDLE, CAS_ERROR_INDICATOR);
      goto execute_array_error;
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
      goto execute_array_error;
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
	      dbobj_to_casobj (ins_obj_p, &ins_oid);
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
      net_buf_cp_object (net_buf, &ins_oid);

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
      ERROR_INFO_SET_WITH_MSG (err_code, DBMS_ERROR_INDICATOR, err_msg);
      NET_BUF_ERR_SET (net_buf);

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

execute_array_error:
  NET_BUF_ERR_SET (net_buf);
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
  bool dummy;

  tran_get_tran_settings (&tmp_lock_wait, &tmp_isol_level,
			  &dummy /* async_ws */ );

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
      err_code = ERROR_INFO_SET (err_code, DBMS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (net_buf);
      return err_code;
    }
  return 0;
}

void
ux_set_lock_timeout (int lock_timeout)
{
  (void) tran_reset_wait_times (lock_timeout);
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
      err_code = ERROR_INFO_SET (CAS_ER_SRV_HANDLE, CAS_ERROR_INDICATOR);
      goto fetch_error;
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
      err_code = ERROR_INFO_SET (CAS_ER_SCHEMA_TYPE, CAS_ERROR_INDICATOR);
      goto fetch_error;
    }

  net_buf_cp_int (net_buf, 0, NULL);	/* result code */

  if (fetch_count <= 0)
    {
      fetch_count = 100;
    }

  err_code =
    (*(fetch_func[fetch_func_index])) (srv_handle, cursor_pos,
				       fetch_count, fetch_flag,
				       result_set_index, net_buf, req_info);

  if (err_code < 0)
    {
      goto fetch_error;
    }

  return 0;

fetch_error:
  NET_BUF_ERR_SET (net_buf);
#if defined(CUBRID_SHARD)
#ifndef LIBCAS_FOR_JSP
  if (srv_handle->auto_commit_mode == TRUE
      && srv_handle->forward_only_cursor == TRUE)
    {
      req_info->need_auto_commit = TRAN_AUTOROLLBACK;
    }
#endif /* !LIBCAS_FOR_JSP */
#endif /* CUBRID_SHARD */
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

  net_arg_get_dbobject (&obj, argv[0]);
  if ((err_code = ux_check_object (obj, net_buf)) < 0)
    {
      goto oid_get_error;
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
      goto oid_get_error;
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
      goto oid_get_error;
    }
  if (oid_data_set (net_buf, obj, attr_num, attr_name) < 0)
    {
      goto oid_get_error;
    }

  FREE_MEM (attr_name);
  obj = NULL;
  return 0;

oid_get_error:
  NET_BUF_ERR_SET (net_buf);
  errors_in_transaction++;
  FREE_MEM (attr_name);
  obj = NULL;
  if (err_code >= 0)
    err_code = -1;
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
      err_code = ERROR_INFO_SET (CAS_ER_SRV_HANDLE, CAS_ERROR_INDICATOR);
      goto cursor_error;
    }

  if (srv_handle->cur_result == NULL)
    {
      err_code = ERROR_INFO_SET (CAS_ER_SRV_HANDLE, CAS_ERROR_INDICATOR);
      goto cursor_error;
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
      err_code = ERROR_INFO_SET (CAS_ER_NO_MORE_DATA, CAS_ERROR_INDICATOR);
      goto cursor_error;
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
	  err_code = ERROR_INFO_SET (db_error_code (), DBMS_ERROR_INDICATOR);
	  goto cursor_error;
	}
      else if (err_code == DB_CURSOR_END)
	{
	  err_code = db_query_sync (result, true);
	  if (err_code < 0)
	    {
	      err_code =
		ERROR_INFO_SET (db_error_code (), DBMS_ERROR_INDICATOR);
	      goto cursor_error;
	    }
	  count = db_query_tuple_count (result);
	  if (count < 0)
	    {
	      err_code =
		ERROR_INFO_SET (db_error_code (), DBMS_ERROR_INDICATOR);
	      goto cursor_error;
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
		      err_code =
			ERROR_INFO_SET (db_error_code (),
					DBMS_ERROR_INDICATOR);
		      goto cursor_error;
		    }
		  count = db_query_tuple_count (result);
		  if (count < 0)
		    {
		      err_code =
			ERROR_INFO_SET (db_error_code (),
					DBMS_ERROR_INDICATOR);
		      goto cursor_error2;
		    }
		  if ((srv_handle->max_row > 0)
		      && (count >= srv_handle->max_row))
		    {
		      count = srv_handle->max_row;
		    }
		}
#endif /* UNIXWARE7 */
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
	      err_code =
		ERROR_INFO_SET (db_error_code (), DBMS_ERROR_INDICATOR);
	      goto cursor_error;
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
	      err_code =
		ERROR_INFO_SET (db_error_code (), DBMS_ERROR_INDICATOR);
	      goto cursor_error;
	    }
	  count = db_query_tuple_count (result);
	  if (count < 0)
	    {
	      err_code =
		ERROR_INFO_SET (db_error_code (), DBMS_ERROR_INDICATOR);
	      goto cursor_error;
	    }
	}
    }
  else
    {
      err_code = ERROR_INFO_SET (CAS_ER_INTERNAL, CAS_ERROR_INDICATOR);
      goto cursor_error;
    }
#endif /* 0 */

  net_buf_cp_int (net_buf, 0, NULL);	/* result code */
  net_buf_cp_int (net_buf, count, NULL);	/* result msg */
  FREE_MEM (err_str);
  return 0;

cursor_error:
  NET_BUF_ERR_SET (net_buf);
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
      err_code = ERROR_INFO_SET (CAS_ER_SRV_HANDLE, CAS_ERROR_INDICATOR);
      goto cursor_update_error;
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
      err_code = ERROR_INFO_SET (err_code, DBMS_ERROR_INDICATOR);
      goto cursor_update_error;
    }
  obj_p = db_get_object (&oid_val);

  argc -= 2;
  argv += 2;

  for (; argc >= 3; argc -= 3, argv += 3)
    {
      char desired_type;

      net_arg_get_int (&col_idx, argv[0]);

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
	  goto cursor_update_error;
	}

      err_code =
	db_put (obj_p, q_result->col_update_info[col_idx - 1].attr_name,
		attr_val);
      if (err_code < 0)
	{
	  err_code = ERROR_INFO_SET (err_code, DBMS_ERROR_INDICATOR);
	  goto cursor_update_error;
	}

      db_value_clear (attr_val);
      FREE_MEM (attr_val);
    }

  db_value_clear (&oid_val);
  obj_p = NULL;

  return 0;
cursor_update_error:
  NET_BUF_ERR_SET (net_buf);
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

void
ux_cursor_close (T_SRV_HANDLE * srv_handle, bool unset_holdable)
{
  int idx = 0;

  if (srv_handle == NULL)
    {
      return;
    }

  idx = srv_handle->cur_result_index - 1;
  if (idx < 0)
    {
      return;
    }

  db_query_end (srv_handle->q_result[idx].result);

  if (unset_holdable == true && srv_handle->is_holdable == true)
    {
      srv_handle->is_holdable = false;
#if !defined(LIBCAS_FOR_JSP)
      as_info->num_holdable_results--;
#endif
    }
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
      err_code = ERROR_INFO_SET (db_error_code (), DBMS_ERROR_INDICATOR);
      goto class_num_objs_error;
    }

  err_code = db_get_class_num_objs_and_pages (class_obj, flag,
					      &num_objs, &num_pages);

  if (err_code < 0)
    {
      err_code = ERROR_INFO_SET (err_code, DBMS_ERROR_INDICATOR);
      goto class_num_objs_error;
    }

  net_buf_cp_int (net_buf, 0, NULL);	/* result code */
  net_buf_cp_int (net_buf, num_objs, NULL);
  net_buf_cp_int (net_buf, num_pages, NULL);

  class_obj = NULL;
  return 0;

class_num_objs_error:
  NET_BUF_ERR_SET (net_buf);
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
	  ERROR_INFO_SET (err_code, DBMS_ERROR_INDICATOR);
	  NET_BUF_ERR_SET (net_buf);
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
	  ERROR_INFO_SET (err_code, DBMS_ERROR_INDICATOR);
	  NET_BUF_ERR_SET (net_buf);
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
	  ERROR_INFO_SET (err_code, DBMS_ERROR_INDICATOR);
	  NET_BUF_ERR_SET (net_buf);
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
	  ERROR_INFO_SET (err_code, DBMS_ERROR_INDICATOR);
	  NET_BUF_ERR_SET (net_buf);
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
	  ERROR_INFO_SET (err_code, DBMS_ERROR_INDICATOR);
	  NET_BUF_ERR_SET (net_buf);
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

  net_arg_get_dbobject (&obj, argv[0]);
  if ((err_code = ux_check_object (obj, net_buf)) < 0)
    {
      goto oid_put_error;
    }

  argc--;
  argv++;

  otmpl = dbt_edit_object (obj);
  if (otmpl == NULL)
    {
      err_code = ERROR_INFO_SET (db_error_code (), DBMS_ERROR_INDICATOR);
      goto oid_put_error;
    }

  for (; argc >= 3; argc -= 3, argv += 3)
    {
      net_arg_get_str (&attr_name, &name_size, argv[0]);
      if (name_size < 1)
	continue;

      attr_type = get_attr_type (obj, attr_name);

      err_code =
	make_bind_value (1, 2, argv + 1, &attr_val, net_buf, attr_type);
      if (err_code < 0)
	{
	  dbt_abort_object (otmpl);
	  goto oid_put_error;
	}

      err_code = dbt_put (otmpl, attr_name, attr_val);
      if (err_code < 0)
	{
	  err_code = ERROR_INFO_SET (err_code, DBMS_ERROR_INDICATOR);
	  db_value_clear (attr_val);
	  FREE_MEM (attr_val);
	  dbt_abort_object (otmpl);
	  goto oid_put_error;
	}

      db_value_clear (attr_val);
      FREE_MEM (attr_val);
    }

  obj = dbt_finish_object (otmpl);
  if (obj == NULL)
    {
      err_code = ERROR_INFO_SET (db_error_code (), DBMS_ERROR_INDICATOR);
      goto oid_put_error;
    }

  net_buf_cp_int (net_buf, 0, NULL);

  obj = NULL;
  return 0;

oid_put_error:
  NET_BUF_ERR_SET (net_buf);
  errors_in_transaction++;
  obj = NULL;
  return err_code;
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
      set_type = TP_DOMAIN_TYPE (ele_domain);
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
      return ERROR_INFO_SET (CAS_ER_NUM_BIND, CAS_ERROR_INDICATOR);
    }

  value_list = (DB_VALUE *) MALLOC (sizeof (DB_VALUE) * num_bind);
  if (value_list == NULL)
    {
      return ERROR_INFO_SET (CAS_ER_NO_MORE_MEMORY, CAS_ERROR_INDICATOR);
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
      err_code = ERROR_INFO_SET (db_error_code (), DBMS_ERROR_INDICATOR);
      goto ux_get_attr_type_str_error;
    }
  attribute = db_get_attribute (class_obj, attr_name);
  if (attr_name == NULL)
    {
      errors_in_transaction++;
      err_code = ERROR_INFO_SET (db_error_code (), DBMS_ERROR_INDICATOR);
      goto ux_get_attr_type_str_error;
    }

  domain = db_attribute_domain (attribute);
  if (domain == NULL)
    {
      errors_in_transaction++;
      err_code = ERROR_INFO_SET (db_error_code (), DBMS_ERROR_INDICATOR);
      goto ux_get_attr_type_str_error;
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

ux_get_attr_type_str_error:
  NET_BUF_ERR_SET (net_buf);
  return err_code;
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
      err_code = ERROR_INFO_SET (CAS_ER_SRV_HANDLE, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (net_buf);
      return err_code;
    }

  if (srv_handle->query_info_flag == FALSE
      || info_type != CAS_GET_QUERY_INFO_PLAN)
    {
      /* Before R4.0, one more type "CAS_GET_QUERY_INFO_HISTOGRAM" was
       * supported. So we should check info_type for backward compatibility.
       */
      file_name = NULL;
    }
  else
    {
      file_name = cas_log_query_plan_file (srv_h_id);
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
      err_code = ERROR_INFO_SET (CAS_ER_SRV_HANDLE, CAS_ERROR_INDICATOR);
      goto parameter_info_error;
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
	  db_type = TP_DOMAIN_TYPE (domain);

	  if (db_type != DB_TYPE_NULL)
	    {
	      param_mode = CCI_PARAM_MODE_IN;
	      precision = db_domain_precision (domain);
	      scale = db_domain_scale (domain);
	    }

	  param = db_marker_next (param);
	}

      if (TP_IS_SET_TYPE (db_type))
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

parameter_info_error:
  errors_in_transaction++;
  NET_BUF_ERR_SET (net_buf);
  return err_code;
}

int
ux_check_object (DB_OBJECT * obj, T_NET_BUF * net_buf)
{
  int err_code;

  if (obj == NULL)
    {
      return ERROR_INFO_SET (CAS_ER_OBJECT, CAS_ERROR_INDICATOR);
    }

  er_clear ();

  if (db_is_instance (obj))
    {
      return 0;
    }

  err_code = db_error_code ();
  if (err_code < 0)
    {
      return ERROR_INFO_SET (err_code, DBMS_ERROR_INDICATOR);
    }

  return ERROR_INFO_SET (CAS_ER_OBJECT, CAS_ERROR_INDICATOR);
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
ux_schema_info (int schema_type, char *arg1, char *arg2, char flag,
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
      goto schema_info_error;
    }
  srv_handle->schema_type = schema_type;

  net_buf_cp_int (net_buf, srv_h_id, NULL);	/* result code */

  switch (schema_type)
    {
    case CCI_SCH_CLASS:
      err_code =
	sch_class_info (net_buf, arg1, flag, 0, srv_handle, client_version);
      break;
    case CCI_SCH_VCLASS:
      err_code =
	sch_class_info (net_buf, arg1, flag, 1, srv_handle, client_version);
      break;
    case CCI_SCH_QUERY_SPEC:
      err_code = sch_queryspec (net_buf, arg1, srv_handle);
      break;
    case CCI_SCH_ATTRIBUTE:
      err_code = sch_attr_info (net_buf, arg1, arg2, flag, 0, srv_handle);
      break;
    case CCI_SCH_CLASS_ATTRIBUTE:
      err_code = sch_attr_info (net_buf, arg1, arg2, flag, 1, srv_handle);
      break;
    case CCI_SCH_METHOD:
      sch_method_info (net_buf, arg1, 0, &(srv_handle->session));
      break;
    case CCI_SCH_CLASS_METHOD:
      sch_method_info (net_buf, arg1, 1, &(srv_handle->session));
      break;
    case CCI_SCH_METHOD_FILE:
      sch_methfile_info (net_buf, arg1, &(srv_handle->session));
      break;
    case CCI_SCH_SUPERCLASS:
      err_code = sch_superclass (net_buf, arg1, 1, srv_handle);
      break;
    case CCI_SCH_SUBCLASS:
      err_code = sch_superclass (net_buf, arg1, 0, srv_handle);
      break;
    case CCI_SCH_CONSTRAINT:
      sch_constraint (net_buf, arg1, &(srv_handle->session));
      break;
    case CCI_SCH_TRIGGER:
      sch_trigger (net_buf, arg1, flag, &(srv_handle->session));
      break;
    case CCI_SCH_CLASS_PRIVILEGE:
      err_code = sch_class_priv (net_buf, arg1, flag, srv_handle);
      break;
    case CCI_SCH_ATTR_PRIVILEGE:
      err_code = sch_attr_priv (net_buf, arg1, arg2, flag, srv_handle);
      break;
    case CCI_SCH_DIRECT_SUPER_CLASS:
      err_code = sch_direct_super_class (net_buf, arg1, flag, srv_handle);
      break;
    case CCI_SCH_PRIMARY_KEY:
      err_code = sch_primary_key (net_buf, arg1, srv_handle);
      break;
    case CCI_SCH_IMPORTED_KEYS:
      err_code = sch_imported_keys (net_buf, arg1, &(srv_handle->session));
      break;
    case CCI_SCH_EXPORTED_KEYS:
      err_code =
	sch_exported_keys_or_cross_reference (net_buf, false, arg1, NULL,
					      &(srv_handle->session));
      break;
    case CCI_SCH_CROSS_REFERENCE:
      err_code =
	sch_exported_keys_or_cross_reference (net_buf, true, arg1, arg2,
					      &(srv_handle->session));
      break;
    default:
      err_code = ERROR_INFO_SET (CAS_ER_SCHEMA_TYPE, CAS_ERROR_INDICATOR);
      goto schema_info_error;
    }

  if (err_code < 0)
    {
      goto schema_info_error;
    }

  if (schema_type == CCI_SCH_CLASS
      || schema_type == CCI_SCH_VCLASS
      || schema_type == CCI_SCH_ATTRIBUTE
      || schema_type == CCI_SCH_CLASS_ATTRIBUTE
      || schema_type == CCI_SCH_QUERY_SPEC
      || schema_type == CCI_SCH_DIRECT_SUPER_CLASS
      || schema_type == CCI_SCH_PRIMARY_KEY)
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

schema_info_error:
  NET_BUF_ERR_SET (net_buf);
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
			 const char *col_name, const char *default_value,
			 char auto_increment, char unique_key,
			 char primary_key, char reverse_index,
			 char reverse_unique, char foreign_key,
			 char shared, const char *attr_name,
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
    {
      is_non_null = 1;
    }
  else if (is_non_null < 0)
    {
      is_non_null = 0;
    }

  net_buf_cp_byte (net_buf, is_non_null);

  if (client_version >= CAS_MAKE_VER (8, 3, 0))
    {
      if (default_value == NULL)
	{
	  net_buf_cp_int (net_buf, 1, NULL);
	  net_buf_cp_byte (net_buf, '\0');
	}
      else
	{
	  int len = strlen (default_value) + 1;

	  net_buf_cp_int (net_buf, len, NULL);
	  net_buf_cp_str (net_buf, default_value, len);
	}

      net_buf_cp_byte (net_buf, auto_increment);
      net_buf_cp_byte (net_buf, unique_key);
      net_buf_cp_byte (net_buf, primary_key);
      net_buf_cp_byte (net_buf, reverse_index);
      net_buf_cp_byte (net_buf, reverse_unique);
      net_buf_cp_byte (net_buf, foreign_key);
      net_buf_cp_byte (net_buf, shared);
    }
}

static void
set_column_info (T_NET_BUF * net_buf, char ut,
		 short scale, int prec,
		 const char *col_name,
		 const char *attr_name,
		 const char *class_name,
		 char is_non_null, T_BROKER_VERSION client_version)
{
  DB_OBJECT *class_obj;
  DB_ATTRIBUTE *attr;
  char auto_increment = 0;
  char unique_key = 0;
  char primary_key = 0;
  char reverse_index = 0;
  char reverse_unique = 0;
  char foreign_key = 0;
  char shared = 0;
  char *default_value_string = NULL;
  char *enum_values = NULL;
  int enum_values_cnt = 0;
  bool alloced_default_value_string = false;
  int def_size = 0;
  char *def_str_p = NULL;
  DB_VALUE default_value;

  db_make_null (&default_value);

  if (client_version >= CAS_MAKE_VER (8, 3, 0))
    {
      DB_VALUE *def = NULL;
      int len, err;

      class_obj = db_find_class (class_name);
      attr = db_get_attribute (class_obj, col_name);

      auto_increment = db_attribute_is_auto_increment (attr);
      unique_key = db_attribute_is_unique (attr);
      primary_key = db_attribute_is_primary_key (attr);
      reverse_index = db_attribute_is_reverse_indexed (attr);
      reverse_unique = db_attribute_is_reverse_unique (attr);
      shared = db_attribute_is_shared (attr);
      foreign_key = db_attribute_is_foreign_key (attr);

      /* Get default value string */
      def = db_attribute_default (attr);
      if (def)
	{
	  switch (db_value_type (def))
	    {
	    case DB_TYPE_UNKNOWN:
	      break;

	    case DB_TYPE_SET:
	    case DB_TYPE_MULTISET:
	    case DB_TYPE_SEQUENCE:	/* DB_TYPE_LIST */
	      alloced_default_value_string = true;
	      serialize_collection_as_string (def, &default_value_string);
	      break;

	    case DB_TYPE_CHAR:
	    case DB_TYPE_NCHAR:
	      def_size = DB_GET_STRING_SIZE (def);
	      def_str_p = DB_GET_STRING (def);
	      if (def_str_p)
		{
		  default_value_string = (char *) malloc (def_size + 1);
		  if (default_value_string != NULL)
		    {
		      alloced_default_value_string = true;
		      memcpy (default_value_string, def_str_p, def_size);
		      default_value_string[def_size] = '\0';
		    }
		}
	      break;

	    case DB_TYPE_VARCHAR:
	    case DB_TYPE_VARNCHAR:
	      default_value_string = db_get_char (def, &len);
	      break;

	    default:
	      err = db_value_coerce (def, &default_value,
				     db_type_to_db_domain (DB_TYPE_VARCHAR));
	      if (err == NO_ERROR)
		{
		  default_value_string = db_get_char (&default_value, &len);
		}
	    }
	}
    }

  prepare_column_info_set (net_buf,
			   ut,
			   scale,
			   prec,
			   col_name,
			   default_value_string,
			   auto_increment,
			   unique_key,
			   primary_key,
			   reverse_index,
			   reverse_unique,
			   foreign_key,
			   shared,
			   attr_name,
			   class_name, is_non_null, client_version);

  if (alloced_default_value_string)
    {
      FREE_MEM (default_value_string);
    }
  db_value_clear (&default_value);
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

  net_arg_get_char (type, net_type);

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

  net_arg_get_size (&data_size, net_value);
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
	char *value, *invalid_pos = NULL;
	int val_size;
	int val_length;
	bool is_composed = false;
	int composed_size;

	net_arg_get_str (&value, &val_size, net_value);

	val_size--;

	if (intl_check_string (value, val_size, &invalid_pos,
			       lang_get_client_charset ()) != 0)
	  {
	    char msg[12];
	    off_t p = invalid_pos != NULL ? (invalid_pos - value) : 0;
	    snprintf (msg, sizeof (msg), "%ld", p);
	    return ERROR_INFO_SET_WITH_MSG (ER_INVALID_CHAR,
					    DBMS_ERROR_INDICATOR, msg);
	  }

	if (lang_get_client_charset () == INTL_CODESET_UTF8
	    && unicode_string_need_compose (value, val_size, &composed_size,
					    lang_get_generic_unicode_norm ()))
	  {
	    char *composed = NULL;

	    composed = db_private_alloc (NULL, composed_size + 1);
	    if (composed == NULL)
	      {
		return ERROR_INFO_SET (CAS_ER_NO_MORE_MEMORY,
				       CAS_ERROR_INDICATOR);
	      }

	    unicode_compose_string (value, val_size, composed,
				    &composed_size, &is_composed,
				    lang_get_generic_unicode_norm ());
	    assert (composed_size <= val_size);
	    composed[composed_size] = '\0';

	    if (is_composed)
	      {
		value = composed;
	      }
	    else
	      {
		db_private_free (NULL, composed);
	      }
	  }

	if (desired_type == DB_TYPE_OBJECT)
	  {
	    DB_OBJECT *obj_p;
	    obj_p = ux_str_to_obj (value);
	    if (obj_p == NULL)
	      {
		return ERROR_INFO_SET (CAS_ER_TYPE_CONVERSION,
				       CAS_ERROR_INDICATOR);
	      }
	    err_code = db_make_object (&db_val, obj_p);
	    obj_p = NULL;
	    coercion_flag = FALSE;
	  }
	else
	  {
	    intl_char_count ((unsigned char *) value, val_size,
			     lang_get_client_charset (), &val_length);
	    err_code = db_make_varchar (&db_val, val_length, value, val_size,
					lang_get_client_charset (),
					lang_get_client_collation ());
	    db_put_cs_and_collation (&db_val, lang_get_client_charset (),
				     lang_get_client_collation ());
	    db_val.need_clear = is_composed;
	  }
      }
      break;
    case CCI_U_TYPE_NCHAR:
    case CCI_U_TYPE_VARNCHAR:
      {
	char *value, *invalid_pos = NULL;
	int val_size;
	int val_length;
	bool is_composed = false;
	int composed_size;

	net_arg_get_str (&value, &val_size, net_value);

	val_size--;

	if (intl_check_string (value, val_size, &invalid_pos,
			       lang_get_client_charset ()) != 0)
	  {
	    char msg[12];
	    off_t p = invalid_pos != NULL ? (invalid_pos - value) : 0;
	    snprintf (msg, sizeof (msg), "%ld", p);
	    return ERROR_INFO_SET_WITH_MSG (ER_INVALID_CHAR,
					    DBMS_ERROR_INDICATOR, msg);
	  }

	if (lang_get_client_charset () == INTL_CODESET_UTF8
	    && unicode_string_need_compose (value, val_size, &composed_size,
					    lang_get_generic_unicode_norm ()))
	  {
	    char *composed = NULL;

	    composed = db_private_alloc (NULL, composed_size + 1);
	    if (composed == NULL)
	      {
		return ERROR_INFO_SET (CAS_ER_NO_MORE_MEMORY,
				       CAS_ERROR_INDICATOR);
	      }

	    unicode_compose_string (value, val_size, composed,
				    &composed_size, &is_composed,
				    lang_get_generic_unicode_norm ());
	    assert (composed_size <= val_size);
	    composed[composed_size] = '\0';

	    if (is_composed)
	      {
		value = composed;
		val_size = composed_size;
	      }
	    else
	      {
		db_private_free (NULL, composed);
	      }
	  }

	intl_char_count ((unsigned char *) value, val_size,
			 LANG_COERCIBLE_CODESET, &val_length);
	err_code = db_make_nchar (&db_val, val_length, value, val_size,
				  lang_get_client_charset (),
				  lang_get_client_collation ());
	db_val.need_clear = is_composed;
      }
      break;
    case CCI_U_TYPE_BIT:
    case CCI_U_TYPE_VARBIT:
      {
	char *value;
	int val_size;
	net_arg_get_str (&value, &val_size, net_value);
	err_code = db_make_bit (&db_val, val_size * 8, value, val_size * 8);
      }
      break;
    case CCI_U_TYPE_NUMERIC:
      {
	char *value, *p;
	int val_size;
	int precision, scale;
	char tmp[BUFSIZ];

	net_arg_get_str (&value, &val_size, net_value);
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

	net_arg_get_bigint (&bi_val, net_value);
	err_code = db_make_bigint (&db_val, bi_val);
      }
      break;
    case CCI_U_TYPE_INT:
      {
	int i_val;

	net_arg_get_int (&i_val, net_value);
	err_code = db_make_int (&db_val, i_val);
      }
      break;
    case CCI_U_TYPE_SHORT:
      {
	short s_val;

	net_arg_get_short (&s_val, net_value);
	err_code = db_make_short (&db_val, s_val);
      }
      break;
    case CCI_U_TYPE_MONETARY:
      {
	double d_val;
	net_arg_get_double (&d_val, net_value);
	err_code =
	  db_make_monetary (&db_val, db_get_currency_default (), d_val);
      }
      break;
    case CCI_U_TYPE_FLOAT:
      {
	float f_val;
	net_arg_get_float (&f_val, net_value);
	err_code = db_make_float (&db_val, f_val);
      }
      break;
    case CCI_U_TYPE_DOUBLE:
      {
	double d_val;
	net_arg_get_double (&d_val, net_value);
	err_code = db_make_double (&db_val, d_val);
      }
      break;
    case CCI_U_TYPE_DATE:
      {
	short month, day, year;
	net_arg_get_date (&year, &month, &day, net_value);
	err_code = db_make_date (&db_val, month, day, year);
      }
      break;
    case CCI_U_TYPE_TIME:
      {
	short hh, mm, ss;
	net_arg_get_time (&hh, &mm, &ss, net_value);
	err_code = db_make_time (&db_val, hh, mm, ss);
      }
      break;
    case CCI_U_TYPE_TIMESTAMP:
      {
	short yr, mon, day, hh, mm, ss;
	DB_DATE date;
	DB_TIME time;
	DB_TIMESTAMP ts;

	net_arg_get_timestamp (&yr, &mon, &day, &hh, &mm, &ss, net_value);
	err_code = db_date_encode (&date, mon, day, yr);
	if (err_code != NO_ERROR)
	  {
	    break;
	  }
	err_code = db_time_encode (&time, hh, mm, ss);
	if (err_code != NO_ERROR)
	  {
	    break;
	  }
	err_code = db_timestamp_encode (&ts, &date, &time);
	if (err_code != NO_ERROR)
	  {
	    break;
	  }
	err_code = db_make_timestamp (&db_val, ts);
      }
      break;
    case CCI_U_TYPE_DATETIME:
      {
	short yr, mon, day, hh, mm, ss, ms;
	DB_DATETIME dt;

	net_arg_get_datetime (&yr, &mon, &day, &hh, &mm, &ss, &ms, net_value);
	err_code = db_datetime_encode (&dt, mon, day, yr, hh, mm, ss, ms);
	if (err_code != NO_ERROR)
	  {
	    break;
	  }
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

	cur_p = ((char *) net_value) + NET_SIZE_INT;	/* data size */
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
	    net_arg_get_size (&ele_size, cur_p);
	    if (ele_size + NET_SIZE_INT > remain_size)
	      break;

	    ele_size =
	      netval_to_dbval (ele_type_arg, cur_p, &ele_val, net_buf,
			       desired_type);
	    if (ele_size < 0)
	      {
		return ele_size;
	      }
	    ele_size += NET_SIZE_INT;
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
	net_arg_get_dbobject (&obj_p, net_value);
	if (ux_check_object (obj_p, NULL) < 0)
	  err_code = db_make_null (&db_val);
	else
	  err_code = db_make_object (&db_val, obj_p);
      }
      coercion_flag = FALSE;
      break;

    case CCI_U_TYPE_BLOB:
    case CCI_U_TYPE_CLOB:
      {
	T_LOB_HANDLE cas_lob;

	net_arg_get_lob_handle (&cas_lob, net_value);
	caslob_to_dblob (&cas_lob, &db_val);
	coercion_flag = FALSE;
      }
      break;

    default:
      return ERROR_INFO_SET (CAS_ER_UNKNOWN_U_TYPE, CAS_ERROR_INDICATOR);
    }

  if (err_code < 0)
    {
      return ERROR_INFO_SET (err_code, DBMS_ERROR_INDICATOR);
    }

  if (desired_type == DB_TYPE_NULL || coercion_flag == FALSE)
    {
      db_value_clone (&db_val, out_val);
    }
  else
    {
      DB_DOMAIN *domain;

      domain = db_type_to_db_domain ((DB_TYPE) desired_type);
      if (domain == NULL)
	{
	  return ERROR_INFO_SET (ER_TP_INCOMPATIBLE_DOMAINS,
				 DBMS_ERROR_INDICATOR);
	}
      err_code = db_value_coerce (&db_val, out_val, domain);
      if (err_code < 0)
	{
	  return ERROR_INFO_SET (err_code, DBMS_ERROR_INDICATOR);
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
  int err_code;

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
	  err_code = ERROR_INFO_SET (error, DBMS_ERROR_INDICATOR);
	  tuple_obj = NULL;
	  return err_code;
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
  int data_size = 0;
  char col_type;

  if (db_value_is_null (val) == true)
    {
      net_buf_cp_int (net_buf, -1, NULL);
      return NET_SIZE_INT;
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
	dbobj_to_casobj (obj, &cas_obj);
	add_res_data_object (net_buf, &cas_obj, col_type, &data_size);
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
	add_res_data_bytes (net_buf, bit, length, col_type, &data_size);
      }
      break;
    case DB_TYPE_VARCHAR:
    case DB_TYPE_CHAR:
      {
	DB_C_CHAR str;
	int dummy = 0;
	int bytes_size = 0;
	int decomp_size;
	char *decomposed = NULL;
	bool need_decomp = false;

	str = db_get_char (val, &dummy);
	bytes_size = DB_GET_STRING_SIZE (val);
	if (max_col_size > 0)
	  {
	    bytes_size = MIN (bytes_size, max_col_size);
	  }

	if (DB_GET_STRING_CODESET (val) == INTL_CODESET_UTF8)
	  {
	    need_decomp =
	      unicode_string_need_decompose (str, bytes_size, &decomp_size,
					     lang_get_generic_unicode_norm
					     ());
	  }


	if (need_decomp)
	  {
	    decomposed = (char *) MALLOC (decomp_size * sizeof (char));
	    if (decomposed != NULL)
	      {
		unicode_decompose_string (str, bytes_size, decomposed,
					  &decomp_size,
					  lang_get_generic_unicode_norm ());

		str = decomposed;
		bytes_size = decomp_size;
	      }
	    else
	      {
		/* set error indicator and send empty string */
		ERROR_INFO_SET (CAS_ER_NO_MORE_MEMORY, CAS_ERROR_INDICATOR);
		bytes_size = 0;
	      }
	  }

	add_res_data_string (net_buf, str, bytes_size, col_type, &data_size);

	if (decomposed != NULL)
	  {
	    FREE (decomposed);
	    decomposed = NULL;
	  }
      }
      break;
    case DB_TYPE_VARNCHAR:
    case DB_TYPE_NCHAR:
      {
	DB_C_NCHAR nchar;
	int dummy = 0;
	int bytes_size = 0;
	int decomp_size;
	char *decomposed = NULL;
	bool need_decomp = false;

	nchar = db_get_nchar (val, &dummy);
	bytes_size = DB_GET_STRING_SIZE (val);
	if (max_col_size > 0)
	  {
	    bytes_size = MIN (bytes_size, max_col_size);
	  }

	if (DB_GET_STRING_CODESET (val) == INTL_CODESET_UTF8)
	  {
	    need_decomp =
	      unicode_string_need_decompose (nchar, bytes_size, &decomp_size,
					     lang_get_generic_unicode_norm
					     ());
	  }

	if (need_decomp)
	  {
	    decomposed = (char *) MALLOC (decomp_size * sizeof (char));
	    if (decomposed != NULL)
	      {
		unicode_decompose_string (nchar, bytes_size, decomposed,
					  &decomp_size,
					  lang_get_generic_unicode_norm ());

		nchar = decomposed;
		bytes_size = decomp_size;
	      }
	    else
	      {
		/* set error indicator and send empty string */
		ERROR_INFO_SET (CAS_ER_NO_MORE_MEMORY, CAS_ERROR_INDICATOR);
		bytes_size = 0;
	      }
	  }

	add_res_data_string (net_buf, nchar, bytes_size, col_type,
			     &data_size);

	if (decomposed != NULL)
	  {
	    FREE (decomposed);
	    decomposed = NULL;
	  }
      }
      break;
    case DB_TYPE_ENUMERATION:
      {
	add_res_data_string (net_buf, db_get_enum_string (val),
			     db_get_enum_string_size (val),
			     col_type ? DB_TYPE_STRING : 0, &data_size);
	break;
      }
    case DB_TYPE_SMALLINT:
      {
	short smallint;
	smallint = db_get_short (val);
	add_res_data_short (net_buf, smallint, col_type, &data_size);
      }
      break;
    case DB_TYPE_INTEGER:
      {
	int int_val;
	int_val = db_get_int (val);
	add_res_data_int (net_buf, int_val, col_type, &data_size);
      }
      break;
    case DB_TYPE_BIGINT:
      {
	DB_BIGINT bigint_val;
	bigint_val = db_get_bigint (val);
	add_res_data_bigint (net_buf, bigint_val, col_type, &data_size);
      }
      break;
    case DB_TYPE_DOUBLE:
      {
	double d_val;
	d_val = db_get_double (val);
	add_res_data_double (net_buf, d_val, col_type, &data_size);
      }
      break;
    case DB_TYPE_MONETARY:
      {
	double d_val;
	d_val = db_value_get_monetary_amount_as_double (val);
	add_res_data_double (net_buf, d_val, col_type, &data_size);
      }
      break;
    case DB_TYPE_FLOAT:
      {
	float f_val;
	f_val = db_get_float (val);
	add_res_data_float (net_buf, f_val, col_type, &data_size);
      }
      break;
    case DB_TYPE_DATE:
      {
	DB_DATE *db_date;
	int yr, mon, day;
	db_date = db_get_date (val);
	db_date_decode (db_date, &mon, &day, &yr);
	add_res_data_date (net_buf, (short) yr, (short) mon, (short) day,
			   col_type, &data_size);
      }
      break;
    case DB_TYPE_TIME:
      {
	DB_DATE *time;
	int hour, minute, second;
	time = db_get_time (val);
	db_time_decode (time, &hour, &minute, &second);
	add_res_data_time (net_buf, (short) hour, (short) minute,
			   (short) second, col_type, &data_size);
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
				(short) hh, (short) mm, (short) ss, col_type,
				&data_size);
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
			       col_type, &data_size);
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
	    data_size = NET_SIZE_INT;
	  }
	else
	  {
	    str = db_get_char (&v, &len);
	    if (str != NULL)
	      {
		strncpy (buf, str, sizeof (buf) - 1);
		buf[sizeof (buf) - 1] = '\0';
		ut_trim (buf);
		add_res_data_string (net_buf, buf, strlen (buf), col_type,
				     &data_size);
	      }
	    else
	      {
		net_buf_cp_int (net_buf, -1, NULL);
		data_size = NET_SIZE_INT;
	      }
	    db_value_clear (&v);
	  }
      }
      break;
    case DB_TYPE_SET:
    case DB_TYPE_MULTISET:
    case DB_TYPE_SEQUENCE:	/* DB_TYPE_LIST */
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
	    data_size = NET_SIZE_INT;
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
	    set_data_size += NET_SIZE_INT;

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
	    data_size = NET_SIZE_INT + set_data_size;
	  }

	if (fetch_flag)
	  db_set_free (set);
      }
      break;

    case DB_TYPE_RESULTSET:
      {
	int h_id;

	h_id = db_get_resultset (val);
	add_res_data_int (net_buf, h_id, col_type, &data_size);
      }
      break;

    case DB_TYPE_BLOB:
    case DB_TYPE_CLOB:
      {
	T_LOB_HANDLE cas_lob;

	dblob_to_caslob (val, &cas_lob);
	add_res_data_lob_handle (net_buf, &cas_lob, col_type, &data_size);
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
dbobj_to_casobj (DB_OBJECT * obj, T_OBJECT * cas_obj)
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

static void
casobj_to_dbobj (T_OBJECT * cas_obj, DB_OBJECT ** obj)
{
  DB_IDENTIFIER oid;

  oid.pageid = cas_obj->pageid;
  oid.volid = cas_obj->volid;
  oid.slotid = cas_obj->slotid;

  *obj = db_object (&oid);
}

static void
dblob_to_caslob (DB_VALUE * lob, T_LOB_HANDLE * cas_lob)
{
  DB_ELO *elo;

  cas_lob->db_type = db_value_type (lob);
  assert (cas_lob->db_type == DB_TYPE_BLOB
	  || cas_lob->db_type == DB_TYPE_CLOB);
  elo = db_get_elo (lob);
  if (elo == NULL)
    {
      cas_lob->lob_size = -1;
      cas_lob->locator_size = 0;
      cas_lob->locator = NULL;
    }
  else
    {
      cas_lob->lob_size = elo->size;
      cas_lob->locator_size = elo->locator ? strlen (elo->locator) + 1 : 0;
      /* including null character */
      cas_lob->locator = elo->locator;
    }
}

static void
caslob_to_dblob (T_LOB_HANDLE * cas_lob, DB_VALUE * db_lob)
{
  DB_ELO elo;

  assert (cas_lob->db_type == DB_TYPE_BLOB
	  || cas_lob->db_type == DB_TYPE_CLOB);
  elo_init_structure (&elo);
  elo.size = cas_lob->lob_size;
  elo.type = ELO_FBO;
  elo.locator = db_private_strdup (NULL, cas_lob->locator);
  db_make_elo (db_lob, cas_lob->db_type, &elo);
  db_lob->need_clear = true;
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
      return ERROR_INFO_SET (CAS_ER_NO_MORE_MEMORY, CAS_ERROR_INDICATOR);
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
	      return ERROR_INFO_SET (CAS_ER_NO_MORE_MEMORY,
				     CAS_ERROR_INDICATOR);
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
      return ERROR_INFO_SET (CAS_ER_NO_MORE_MEMORY, CAS_ERROR_INDICATOR);
    }
  for (i = 0; i < attr_num; i++)
    {
      int name_size;
      char *tmp_p;

      net_arg_get_str (&tmp_p, &name_size, argv[i + 1]);
      if (name_size <= 1 || tmp_p[name_size - 1] != '\0')
	{
	  FREE_MEM (attr_name);
	  return ERROR_INFO_SET (CAS_ER_OBJECT, CAS_ERROR_INDICATOR);
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
	      return ERROR_INFO_SET (db_error_code (), DBMS_ERROR_INDICATOR);
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
		  return ERROR_INFO_SET (db_error_code (),
					 DBMS_ERROR_INDICATOR);
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
	  db_type = TP_DOMAIN_TYPE (domain);
	  if (TP_IS_SET_TYPE (db_type))
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
	  return ERROR_INFO_SET (CAS_ER_NO_MORE_DATA, CAS_ERROR_INDICATOR);
	}
    }
  else
    {
      if (result_set_idx > srv_handle->cur_result_index)
	{
	  return ERROR_INFO_SET (CAS_ER_NO_MORE_RESULT_SET,
				 CAS_ERROR_INDICATOR);
	}
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
  if (result == NULL || has_stmt_result_set (q_result->stmt_type) == false)
    {
      return ERROR_INFO_SET (CAS_ER_NO_MORE_DATA, CAS_ERROR_INDICATOR);
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
	  if (shm_appl->select_auto_commit == ON)
	    {
	      hm_srv_handle_set_fetch_completed (srv_handle);
	    }

	  if (check_auto_commit_after_fetch_done (srv_handle) == true)
	    {
	      ux_cursor_close (srv_handle, true);
	      req_info->need_auto_commit = TRAN_AUTOCOMMIT;
	    }
#endif /* !LIBCAS_FOR_JSP */
	  return 0;
	}
      else
	{
	  return ERROR_INFO_SET (err_code, DBMS_ERROR_INDICATOR);
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
		      dbobj_to_casobj (db_obj, &tuple_obj);
		    }
		  else if (db_error_code () == 0 || db_error_code () == -48)
		    {
		      memset ((char *) &tuple_obj, 0xff, sizeof (T_OBJECT));
		      db_obj = NULL;
		    }
		  else
		    {
		      return ERROR_INFO_SET (db_error_code (),
					     DBMS_ERROR_INDICATOR);
		    }
		}
	      db_value_clear (&oid_val);
	    }
	}

      net_buf_cp_object (net_buf, &tuple_obj);

      err_code =
	cur_tuple (q_result, srv_handle->max_col_size, sensitive_flag, db_obj,
		   net_buf);
      if (err_code < 0)
	{
	  return err_code;
	}

      num_tuple++;
      cursor_pos++;
      if (srv_handle->max_row > 0 && cursor_pos > srv_handle->max_row)
	{
#ifndef LIBCAS_FOR_JSP
	  if (shm_appl->select_auto_commit == ON)
	    {
	      hm_srv_handle_set_fetch_completed (srv_handle);
	    }

	  if (check_auto_commit_after_fetch_done (srv_handle) == true)
	    {
	      ux_cursor_close (srv_handle, true);
	      req_info->need_auto_commit = TRAN_AUTOCOMMIT;
	    }
#endif /* !LIBCAS_FOR_JSP */
	  break;
	}
      err_code = db_query_next_tuple (result);
      if (err_code == DB_CURSOR_SUCCESS)
	{
	}
      else if (err_code == DB_CURSOR_END)
	{
#ifndef LIBCAS_FOR_JSP
	  if (shm_appl->select_auto_commit == ON)
	    {
	      hm_srv_handle_set_fetch_completed (srv_handle);
	    }

	  if (check_auto_commit_after_fetch_done (srv_handle) == true)
	    {
	      ux_cursor_close (srv_handle, true);
	      req_info->need_auto_commit = TRAN_AUTOCOMMIT;
	    }
#endif /* !LIBCAS_FOR_JSP */
	  break;
	}
      else
	{
	  return ERROR_INFO_SET (err_code, DBMS_ERROR_INDICATOR);
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
    {
      return ERROR_INFO_SET (CAS_ER_NO_MORE_DATA, CAS_ERROR_INDICATOR);
    }
  num_result = srv_handle->sch_tuple_num;

  memset (&dummy_obj, 0, sizeof (T_OBJECT));

  if (num_result < cursor_pos)
    {
      return ERROR_INFO_SET (CAS_ER_NO_MORE_DATA, CAS_ERROR_INDICATOR);
    }

  tuple_num = 0;
  net_buf_cp_int (net_buf, tuple_num, &tuple_num_msg_offset);
  for (i = 0; (i < fetch_count) && (cursor_pos <= num_result); i++)
    {
      char *p;

      net_buf_cp_int (net_buf, cursor_pos, NULL);
      net_buf_cp_object (net_buf, &dummy_obj);

      /* 1. name */
      p = class_table[cursor_pos - 1].class_name;
      add_res_data_string (net_buf, p, strlen (p), 0, NULL);

      /* 2. type */
      add_res_data_short (net_buf, class_table[cursor_pos - 1].class_type, 0,
			  NULL);

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
      return ERROR_INFO_SET (CAS_ER_NO_MORE_DATA, CAS_ERROR_INDICATOR);
    }

  result = (DB_QUERY_RESULT *) q_result->result;
  if (result == NULL || q_result->stmt_type != CUBRID_STMT_SELECT)
    {
      return ERROR_INFO_SET (CAS_ER_NO_MORE_DATA, CAS_ERROR_INDICATOR);
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
	  return ERROR_INFO_SET (err_code, DBMS_ERROR_INDICATOR);
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
      net_buf_cp_object (net_buf, &tuple_obj);

      err_code = db_query_get_tuple_value (result, 0, &val_class);
      if (err_code < 0)
	{
	  return ERROR_INFO_SET (err_code, DBMS_ERROR_INDICATOR);
	}
      class_name = db_get_string (&val_class);
      class_obj = db_find_class (class_name);
      if (class_obj == NULL)
	{
	  return ERROR_INFO_SET (db_error_code (), DBMS_ERROR_INDICATOR);
	}

      err_code = db_query_get_tuple_value (result, 1, &val_attr);
      if (err_code < 0)
	{
	  return ERROR_INFO_SET (err_code, DBMS_ERROR_INDICATOR);
	}
      attr_name = db_get_string (&val_attr);
      if (srv_handle->schema_type == CCI_SCH_CLASS_ATTRIBUTE)
	db_attr = db_get_class_attribute (class_obj, attr_name);
      else
	db_attr = db_get_attribute (class_obj, attr_name);
      if (db_attr == NULL)
	{
	  return ERROR_INFO_SET (db_error_code (), DBMS_ERROR_INDICATOR);
	}

      memset (&attr_info, 0, sizeof (attr_info));
      class_attr_info (class_name, db_attr, NULL, 0, &attr_info);

      /* 1. attr name */
      p = attr_info.attr_name;
      add_res_data_string (net_buf, p, strlen (p), 0, NULL);

      /* 2. domain */
      add_res_data_short (net_buf, (short) attr_info.domain, 0, NULL);

      /* 3. scale */
      add_res_data_short (net_buf, (short) attr_info.scale, 0, NULL);

      /* 4. precision */
      add_res_data_int (net_buf, attr_info.precision, 0, NULL);

      /* 5. indexed */
      add_res_data_short (net_buf, (short) attr_info.indexed, 0, NULL);

      /* 6. non_null */
      add_res_data_short (net_buf, (short) attr_info.non_null, 0, NULL);

      /* 7. shared */
      add_res_data_short (net_buf, (short) attr_info.shared, 0, NULL);

      /* 8. unique */
      add_res_data_short (net_buf, (short) attr_info.unique, 0, NULL);

      /* 9. default */
      dbval_to_net_buf ((DB_VALUE *) attr_info.default_val, net_buf, 0, 0, 1);

      /* 10. order */
      add_res_data_int (net_buf, (int) attr_info.attr_order, 0, NULL);

      /* 11. class name */
      p = attr_info.class_name;
      if (p == NULL)
	add_res_data_string (net_buf, "", 0, 0, NULL);
      else
	add_res_data_string (net_buf, p, strlen (p), 0, NULL);

      /* 12. source class */
      p = attr_info.source_class;
      if (p == NULL)
	add_res_data_string (net_buf, "", 0, 0, NULL);
      else
	add_res_data_string (net_buf, p, strlen (p), 0, NULL);

      /* 13. is_key */
      add_res_data_short (net_buf, (short) attr_info.is_key, 0, NULL);

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
	  return ERROR_INFO_SET (err_code, DBMS_ERROR_INDICATOR);
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
	{
	  return ERROR_INFO_SET (CAS_ER_NO_MORE_DATA, CAS_ERROR_INDICATOR);
	}
    }
  else
    tmp_p = (DB_METHOD *) (srv_handle->cur_result);

  tuple_num = 0;
  net_buf_cp_int (net_buf, tuple_num, &tuple_num_msg_offset);

  for (i = 0; (tmp_p) && (i < fetch_count); i++)
    {
      char set_type, cas_type;

      net_buf_cp_int (net_buf, cursor_pos, NULL);
      net_buf_cp_object (net_buf, &dummy_obj);

      /* 1. name */
      name = (char *) db_method_name (tmp_p);
      add_res_data_string (net_buf, name, strlen (name), 0, NULL);

      /* 2. ret domain */
      domain = db_method_arg_domain (tmp_p, 0);
      db_type = TP_DOMAIN_TYPE (domain);
      if (TP_IS_SET_TYPE (db_type))
	{
	  set_type = get_set_domain (domain, NULL, NULL, NULL);
	  cas_type = CAS_TYPE_COLLECTION (db_type, set_type);
	}
      else
	{
	  cas_type = ux_db_type_to_cas_type (db_type);
	}
      add_res_data_short (net_buf, (short) cas_type, 0, NULL);

      /* 3. arg domain */
      arg_str[0] = '\0';
      num_args = db_method_arg_count (tmp_p);
      for (j = 1; j <= num_args; j++)
	{
	  domain = db_method_arg_domain (tmp_p, j);
	  db_type = TP_DOMAIN_TYPE (domain);
	  if (TP_IS_SET_TYPE (db_type))
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
      add_res_data_string (net_buf, arg_str, strlen (arg_str), 0, NULL);

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
	{
	  return ERROR_INFO_SET (CAS_ER_NO_MORE_DATA, CAS_ERROR_INDICATOR);
	}
    }
  else
    tmp_p = (DB_METHFILE *) (srv_handle->cur_result);

  tuple_num = 0;
  net_buf_cp_int (net_buf, tuple_num, &tuple_num_msg_offset);

  for (i = 0; (tmp_p) && (i < fetch_count); i++)
    {
      net_buf_cp_int (net_buf, cursor_pos, NULL);
      net_buf_cp_object (net_buf, &dummy_obj);

      /* 1. name */
      name = (char *) db_methfile_name (tmp_p);
      add_res_data_string (net_buf, name, strlen (name), 0, NULL);

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
	  net_buf_cp_object (net_buf, &dummy_obj);

	  /* 1. type */
	  add_res_data_short (net_buf, constraint_dbtype_to_castype (type),
			      0, NULL);

	  /* 2. const name */
	  add_res_data_string (net_buf, name, strlen (name), 0, NULL);

	  /* 3. attr name */
	  add_res_data_string (net_buf, attr_name, strlen (attr_name), 0,
			       NULL);

	  /* 4. num pages */
	  add_res_data_int (net_buf, bt_total_pages, 0, NULL);

	  /* 5. num keys */
	  add_res_data_int (net_buf, bt_num_keys, 0, NULL);

	  /* 6. primary key */
	  add_res_data_short (net_buf, 0, 0, NULL);

	  /* 7. key_order */
	  add_res_data_short (net_buf, j + 1, 0, NULL);

	  /* 8. asc_desc */
	  add_res_data_string (net_buf, asc_desc ? "D" : "A", 1, 0, NULL);

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
	{
	  return ERROR_INFO_SET (CAS_ER_NO_MORE_DATA, CAS_ERROR_INDICATOR);
	}
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
      net_buf_cp_object (net_buf, &dummy_obj);

      /* 1. name */
      if (db_trigger_name (tmp_p->op, &tmp_str) < 0)
	{
	  add_res_data_string (net_buf, "", 0, 0, NULL);
	}
      else
	{
	  if (tmp_str != NULL)
	    {
	      add_res_data_string (net_buf, tmp_str, strlen (tmp_str), 0,
				   NULL);
	      db_string_free (tmp_str);
	    }
	}

      /* 2. status */
      if (db_trigger_status (tmp_p->op, &trig_status) < 0)
	str_buf[0] = '\0';
      else
	trigger_status_str (trig_status, str_buf);
      add_res_data_string (net_buf, str_buf, strlen (str_buf), 0, NULL);

      /* 3. event */
      if (db_trigger_event (tmp_p->op, &trig_event) < 0)
	str_buf[0] = '\0';
      else
	trigger_event_str (trig_event, str_buf);
      add_res_data_string (net_buf, str_buf, strlen (str_buf), 0, NULL);

      /* 4. target class */
      if (db_trigger_class (tmp_p->op, &target_class_obj) < 0)
	{
	  add_res_data_string (net_buf, "", 0, 0, NULL);
	}
      else
	{
	  tmp_str = (char *) db_get_class_name (target_class_obj);
	  if (tmp_str == NULL)
	    tmp_str = (char *) "";
	  add_res_data_string (net_buf, tmp_str, strlen (tmp_str), 0, NULL);
	}

      /* 5. target attribute */
      tmp_str = NULL;
      if ((db_trigger_attribute (tmp_p->op, &tmp_str) < 0)
	  || (tmp_str == NULL))
	{
	  add_res_data_string (net_buf, "", 0, 0, NULL);
	}
      else
	{
	  add_res_data_string (net_buf, tmp_str, strlen (tmp_str), 0, NULL);
	  db_string_free (tmp_str);
	}

      /* 6. action time */
      if (db_trigger_action_time (tmp_p->op, &trig_time) < 0)
	str_buf[0] = '\0';
      else
	trigger_time_str (trig_time, str_buf);
      add_res_data_string (net_buf, str_buf, strlen (str_buf), 0, NULL);

      /* 7. action */
      tmp_str = NULL;
      if ((db_trigger_action (tmp_p->op, &tmp_str) < 0) || (tmp_str == NULL))
	{
	  add_res_data_string (net_buf, "", 0, 0, NULL);
	}
      else
	{
	  add_res_data_string (net_buf, tmp_str, strlen (tmp_str), 0, NULL);
	  db_string_free (tmp_str);
	}

      /* 8. priority */
      trig_priority = 0;
      db_trigger_priority (tmp_p->op, &trig_priority);
      add_res_data_float (net_buf, (float) trig_priority, 0, NULL);

      /* 9. condition time */
      if (db_trigger_condition_time (tmp_p->op, &trig_time) < 0)
	str_buf[0] = '\0';
      else
	trigger_time_str (trig_time, str_buf);
      add_res_data_string (net_buf, str_buf, strlen (str_buf), 0, NULL);

      /* 10. condition */
      tmp_str = NULL;
      if ((db_trigger_condition (tmp_p->op, &tmp_str) < 0)
	  || (tmp_str == NULL))
	{
	  add_res_data_string (net_buf, "", 0, 0, NULL);
	}
      else
	{
	  add_res_data_string (net_buf, tmp_str, strlen (tmp_str), 0, NULL);
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
    {
      return ERROR_INFO_SET (CAS_ER_NO_MORE_DATA, CAS_ERROR_INDICATOR);
    }

  num_result = srv_handle->sch_tuple_num;

  memset (&dummy_obj, 0, sizeof (T_OBJECT));

  if (num_result < cursor_pos)
    {
      return ERROR_INFO_SET (CAS_ER_NO_MORE_DATA, CAS_ERROR_INDICATOR);
    }

  tuple_num = 0;
  net_buf_cp_int (net_buf, tuple_num, &tuple_num_msg_offset);

  for (i = 0; (i < fetch_count) && (cursor_pos <= num_result); i++)
    {
      const char *p;
      int index;

      index = cursor_pos - 1;

      net_buf_cp_int (net_buf, cursor_pos, NULL);
      net_buf_cp_object (net_buf, &dummy_obj);

      /* 1. name */
      p = priv_table[index].class_name;
      add_res_data_string (net_buf, p, strlen (p), 0, NULL);

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
      add_res_data_string (net_buf, p, strlen (p), 0, NULL);

      /* 3. grantable */
      if (priv_table[index].grant)
	p = "YES";
      else
	p = "NO";
      add_res_data_string (net_buf, p, strlen (p), 0, NULL);

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


static int
fetch_foreign_keys (T_SRV_HANDLE * srv_handle, int cursor_pos,
		    int fetch_count, char fetch_flag, int result_set_idx,
		    T_NET_BUF * net_buf, T_REQ_INFO * req_info)
{
  T_OBJECT dummy_obj = { 0, 0, 0 };
  T_FK_INFO_RESULT *fk_res;
  int tuple_num_msg_offset;

  cursor_pos = 1;
  net_buf_cp_int (net_buf, 0, &tuple_num_msg_offset);

  for (fk_res = (T_FK_INFO_RESULT *) srv_handle->session; fk_res != NULL;
       fk_res = fk_res->next)
    {
      net_buf_cp_int (net_buf, cursor_pos, NULL);
      net_buf_cp_object (net_buf, &dummy_obj);

      /* 1. PKTABLE_NAME */
      add_res_data_string_safe (net_buf, fk_res->pktable_name, 0, NULL);

      /* 2. PKCOLUMN_NAME */
      add_res_data_string_safe (net_buf, fk_res->pkcolumn_name, 0, NULL);

      /* 3. FKTABLE_NAME */
      add_res_data_string_safe (net_buf, fk_res->fktable_name, 0, NULL);

      /* 4. FKCOLUMN_NAME */
      add_res_data_string_safe (net_buf, fk_res->fkcolumn_name, 0, NULL);

      /* 5. KEY_SEQ */
      add_res_data_short (net_buf, fk_res->key_seq, 0, NULL);

      /* 6. UPDATE_RULE */
      add_res_data_short (net_buf, fk_res->update_action, 0, NULL);

      /* 7. DELETE_RULE */
      add_res_data_short (net_buf, fk_res->delete_action, 0, NULL);

      /* 8. FK_NAME */
      add_res_data_string_safe (net_buf, fk_res->fk_name, 0, NULL);

      /* 9. PK_NAME */
      add_res_data_string_safe (net_buf, fk_res->pk_name, 0, NULL);

      cursor_pos++;
    }

  net_buf_overwrite_int (net_buf, tuple_num_msg_offset, cursor_pos - 1);

  return 0;
}

static void
add_res_data_bytes (T_NET_BUF * net_buf, char *str, int size, char type,
		    int *net_size)
{
  if (type)
    {
      net_buf_cp_int (net_buf, NET_SIZE_BYTE + size, NULL);	/* type */
      net_buf_cp_byte (net_buf, type);
    }
  else
    {
      net_buf_cp_int (net_buf, size, NULL);
    }
  /* do not append NULL terminator */
  net_buf_cp_str (net_buf, str, size);
  if (net_size)
    {
      *net_size = NET_SIZE_INT + (type ? NET_SIZE_BYTE : 0) + size;
    }
}

static void
add_res_data_string (T_NET_BUF * net_buf, const char *str, int size,
		     char type, int *net_size)
{
  if (type)
    {
      net_buf_cp_int (net_buf, NET_SIZE_BYTE + size + 1, NULL);	/* type, NULL terminator */
      net_buf_cp_byte (net_buf, type);
    }
  else
    {
      net_buf_cp_int (net_buf, size + 1, NULL);	/* NULL terminator */
    }
  net_buf_cp_str (net_buf, str, size);
  net_buf_cp_byte (net_buf, '\0');
  if (net_size)
    {
      *net_size =
	NET_SIZE_INT + (type ? NET_SIZE_BYTE : 0) + size + NET_SIZE_BYTE;
    }
}

static void
add_res_data_string_safe (T_NET_BUF * net_buf, const char *str,
			  char type, int *net_size)
{
  if (str != NULL)
    {
      add_res_data_string (net_buf, str, strlen (str), type, net_size);
    }
  else
    {
      add_res_data_string (net_buf, "", 0, type, net_size);
    }
}

static void
add_res_data_int (T_NET_BUF * net_buf, int value, char type, int *net_size)
{
  if (type)
    {
      net_buf_cp_int (net_buf, NET_SIZE_BYTE + NET_SIZE_INT, NULL);
      net_buf_cp_byte (net_buf, type);
      net_buf_cp_int (net_buf, value, NULL);
    }
  else
    {
      net_buf_cp_int (net_buf, NET_SIZE_INT, NULL);
      net_buf_cp_int (net_buf, value, NULL);
    }
  if (net_size)
    {
      *net_size = NET_SIZE_INT + (type ? NET_SIZE_BYTE : 0) + NET_SIZE_INT;
    }
}

static void
add_res_data_bigint (T_NET_BUF * net_buf, DB_BIGINT value, char type,
		     int *net_size)
{
  if (type)
    {
      net_buf_cp_int (net_buf, NET_SIZE_BYTE + NET_SIZE_BIGINT, NULL);
      net_buf_cp_byte (net_buf, type);
      net_buf_cp_bigint (net_buf, value, NULL);
    }
  else
    {
      net_buf_cp_int (net_buf, NET_SIZE_BIGINT, NULL);
      net_buf_cp_bigint (net_buf, value, NULL);
    }
  if (net_size)
    {
      *net_size = NET_SIZE_INT + (type ? NET_SIZE_BYTE : 0) + NET_SIZE_BIGINT;
    }
}

static void
add_res_data_short (T_NET_BUF * net_buf, short value, char type,
		    int *net_size)
{
  if (type)
    {
      net_buf_cp_int (net_buf, NET_SIZE_BYTE + NET_SIZE_SHORT, NULL);
      net_buf_cp_byte (net_buf, type);
      net_buf_cp_short (net_buf, value);
    }
  else
    {
      net_buf_cp_int (net_buf, NET_SIZE_SHORT, NULL);
      net_buf_cp_short (net_buf, value);
    }
  if (net_size)
    {
      *net_size = NET_SIZE_INT + (type ? NET_SIZE_BYTE : 0) + NET_SIZE_SHORT;
    }
}

static void
add_res_data_float (T_NET_BUF * net_buf, float value, char type,
		    int *net_size)
{
  if (type)
    {
      net_buf_cp_int (net_buf, NET_SIZE_BYTE + NET_SIZE_FLOAT, NULL);
      net_buf_cp_byte (net_buf, type);
      net_buf_cp_float (net_buf, value);
    }
  else
    {
      net_buf_cp_int (net_buf, NET_SIZE_FLOAT, NULL);
      net_buf_cp_float (net_buf, value);
    }
  if (net_size)
    {
      *net_size = NET_SIZE_INT + (type ? NET_SIZE_BYTE : 0) + NET_SIZE_FLOAT;
    }
}

static void
add_res_data_double (T_NET_BUF * net_buf, double value, char type,
		     int *net_size)
{
  if (type)
    {
      net_buf_cp_int (net_buf, NET_SIZE_BYTE + NET_SIZE_DOUBLE, NULL);
      net_buf_cp_byte (net_buf, type);
      net_buf_cp_double (net_buf, value);
    }
  else
    {
      net_buf_cp_int (net_buf, NET_SIZE_DOUBLE, NULL);
      net_buf_cp_double (net_buf, value);
    }
  if (net_size)
    {
      *net_size = NET_SIZE_INT + (type ? NET_SIZE_BYTE : 0) + NET_SIZE_DOUBLE;
    }
}

static void
add_res_data_timestamp (T_NET_BUF * net_buf, short yr, short mon, short day,
			short hh, short mm, short ss, char type,
			int *net_size)
{
  if (type)
    {
      net_buf_cp_int (net_buf, NET_SIZE_BYTE + NET_SIZE_TIMESTAMP, NULL);
      net_buf_cp_byte (net_buf, type);
    }
  else
    {
      net_buf_cp_int (net_buf, NET_SIZE_TIMESTAMP, NULL);
    }
  net_buf_cp_short (net_buf, yr);
  net_buf_cp_short (net_buf, mon);
  net_buf_cp_short (net_buf, day);
  net_buf_cp_short (net_buf, hh);
  net_buf_cp_short (net_buf, mm);
  net_buf_cp_short (net_buf, ss);
  if (net_size)
    {
      *net_size =
	NET_SIZE_INT + (type ? NET_SIZE_BYTE : 0) + NET_SIZE_TIMESTAMP;
    }
}

static void
add_res_data_datetime (T_NET_BUF * net_buf, short yr, short mon, short day,
		       short hh, short mm, short ss, short ms, char type,
		       int *net_size)
{
  if (type)
    {
      net_buf_cp_int (net_buf, NET_SIZE_BYTE + NET_SIZE_DATETIME, NULL);
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
  if (net_size)
    {
      *net_size =
	NET_SIZE_INT + (type ? NET_SIZE_BYTE : 0) + NET_SIZE_DATETIME;
    }
}

static void
add_res_data_time (T_NET_BUF * net_buf, short hh, short mm, short ss,
		   char type, int *net_size)
{
  if (type)
    {
      net_buf_cp_int (net_buf, NET_SIZE_BYTE + NET_SIZE_TIME, NULL);
      net_buf_cp_byte (net_buf, type);
    }
  else
    {
      net_buf_cp_int (net_buf, NET_SIZE_TIME, NULL);
    }
  net_buf_cp_short (net_buf, hh);
  net_buf_cp_short (net_buf, mm);
  net_buf_cp_short (net_buf, ss);
  if (net_size)
    {
      *net_size = NET_SIZE_INT + (type ? NET_SIZE_BYTE : 0) + NET_SIZE_TIME;
    }
}

static void
add_res_data_date (T_NET_BUF * net_buf, short yr, short mon, short day,
		   char type, int *net_size)
{
  if (type)
    {
      net_buf_cp_int (net_buf, NET_SIZE_BYTE + NET_SIZE_DATE, NULL);
      net_buf_cp_byte (net_buf, type);
    }
  else
    {
      net_buf_cp_int (net_buf, NET_SIZE_DATE, NULL);
    }
  net_buf_cp_short (net_buf, yr);
  net_buf_cp_short (net_buf, mon);
  net_buf_cp_short (net_buf, day);
  if (net_size)
    {
      *net_size = NET_SIZE_INT + (type ? NET_SIZE_BYTE : 0) + NET_SIZE_DATE;
    }
}

static void
add_res_data_object (T_NET_BUF * net_buf, T_OBJECT * obj, char type,
		     int *net_size)
{
  if (type)
    {
      net_buf_cp_int (net_buf, NET_SIZE_BYTE + NET_SIZE_OBJECT, NULL);
      net_buf_cp_byte (net_buf, type);
    }
  else
    {
      net_buf_cp_int (net_buf, NET_SIZE_OBJECT, NULL);
    }
  net_buf_cp_object (net_buf, obj);
  if (net_size)
    {
      *net_size = NET_SIZE_INT + (type ? NET_SIZE_BYTE : 0) + NET_SIZE_OBJECT;
    }
}

static void
add_res_data_lob_handle (T_NET_BUF * net_buf, T_LOB_HANDLE * lob,
			 char type, int *net_size)
{
  int lob_handle_size =
    NET_SIZE_INT + NET_SIZE_INT64 + NET_SIZE_INT + lob->locator_size;
  /* db_type + lob_size + locator_size + locator including null character */
  if (type)
    {
      net_buf_cp_int (net_buf, NET_SIZE_BYTE + lob_handle_size, NULL);
      net_buf_cp_byte (net_buf, type);
    }
  else
    {
      net_buf_cp_int (net_buf, lob_handle_size, NULL);
    }
  net_buf_cp_lob_handle (net_buf, lob);
  if (net_size)
    {
      *net_size = NET_SIZE_INT + (type ? NET_SIZE_BYTE : 0) + lob_handle_size;
    }
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
  char *p;
  int num_markers = 0;

  for (p = stmt; *p; p++)
    {
      if (*p == '?')
	{
	  num_markers++;
	}
      else if (*p == '-' && *(p + 1) == '-')
	{
	  p = consume_tokens (p + 2, SQL_STYLE_COMMENT);
	}
      else if (*p == '/' && *(p + 1) == '*')
	{
	  p = consume_tokens (p + 2, C_STYLE_COMMENT);
	}
      else if (*p == '/' && *(p + 1) == '/')
	{
	  p = consume_tokens (p + 2, CPP_STYLE_COMMENT);
	}
      else if (*p == '\'')
	{
	  p = consume_tokens (p + 1, SINGLE_QUOTED_STRING);
	}
      else if (cas_default_ansi_quotes == false && *p == '\"')
	{
	  p = consume_tokens (p + 1, DOUBLE_QUOTED_STRING);
	}

      if (*p == '\0')
	{
	  break;
	}
    }

  return num_markers;
}

static char *
consume_tokens (char *stmt, STATEMENT_STATUS stmt_status)
{
  char *p = stmt;

  if (stmt_status == SQL_STYLE_COMMENT || stmt_status == CPP_STYLE_COMMENT)
    {
      for (; *p; p++)
	{
	  if (*p == '\n')
	    {
	      break;
	    }
	}
    }
  else if (stmt_status == C_STYLE_COMMENT)
    {
      for (; *p; p++)
	{
	  if (*p == '*' && *(p + 1) == '/')
	    {
	      p++;
	      break;
	    }
	}
    }
  else if (stmt_status == SINGLE_QUOTED_STRING)
    {
      for (; *p; p++)
	{
	  if (*p == '\'' && *(p + 1) == '\'')
	    {
	      p++;
	    }
	  else if (cas_default_no_backslash_escapes == false && *p == '\\')
	    {
	      p++;
	    }
	  else if (*p == '\'')
	    {
	      break;
	    }
	}
    }
  else if (stmt_status == DOUBLE_QUOTED_STRING)
    {
      for (; *p; p++)
	{
	  if (*p == '\"' && *(p + 1) == '\"')
	    {
	      p++;
	    }
	  else if (cas_default_no_backslash_escapes == false && *p == '\\')
	    {
	      p++;
	    }
	  else if (*p == '\"')
	    {
	      break;
	    }
	}
    }

  return p;
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
  if (q_result->null_type_column != NULL)
    {
      FREE_MEM (q_result->null_type_column);
    }

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
	  return ERROR_INFO_SET (db_error_code (), DBMS_ERROR_INDICATOR);
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
	      if (null_type_column != NULL)
		{
		  FREE_MEM (null_type_column);
		}
	      FREE_MEM (col_update_info);
	      return ERROR_INFO_SET (CAS_ER_NO_MORE_MEMORY,
				     CAS_ERROR_INDICATOR);
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
		{
		  col_name = (char *) db_query_format_name (col);
		}
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
	  db_type = TP_DOMAIN_TYPE (domain);
	  if (TP_IS_SET_TYPE (db_type))
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
		{
		  precision = shm_appl->max_string_length;
		}
	    }
#else /* !LIBCAS_FOR_JSP */
	  /* precision = DB_MAX_STRING_LENGTH; */
#endif /* !LIBCAS_FOR_JSP */

	  set_column_info (net_buf,
			   (char) cas_type,
			   scale,
			   precision,
			   col_name,
			   attr_name,
			   class_name,
			   (char)
			   db_query_format_is_non_null (col), client_version);

	  num_cols++;
	}

      q_result->null_type_column = null_type_column;
      net_buf_overwrite_int (net_buf, num_col_offset, num_cols);
      if (column_info)
	{
	  db_query_format_free (column_info);
	}
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
	  return ERROR_INFO_SET (CAS_ER_NO_MORE_MEMORY, CAS_ERROR_INDICATOR);
	}
      q_result->null_type_column[0] = 1;

      updatable_flag = 0;
      net_buf_cp_byte (net_buf, updatable_flag);
      net_buf_cp_int (net_buf, 1, NULL);
      prepare_column_info_set (net_buf, 0, 0, 0, "", "", 0, 0, 0, 0, 0, 0, 0,
			       "", "", 0, client_version);
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
	  error =
	    db_query_get_tuple_value ((DB_QUERY_RESULT *)
				      srv_handle->q_result[i].result, 0,
				      &val);
	  if (error < 0)
	    {
	      memset (&ins_oid, 0, sizeof (T_OBJECT));
	    }
	  else
	    {
	      ins_obj_p = db_get_object (&val);
	      dbobj_to_casobj (ins_obj_p, &ins_oid);
	      db_value_clear (&val);
	    }
	}
      else
	{
	  memset (&ins_oid, 0, sizeof (T_OBJECT));
	}
      net_buf_cp_object (net_buf, &ins_oid);

      db_query_get_cache_time ((DB_QUERY_RESULT *) srv_handle->
			       q_result[i].result, &srv_cache_time);
      /* send CT */
      net_buf_cp_int (net_buf, srv_cache_time.sec, NULL);
      net_buf_cp_int (net_buf, srv_cache_time.usec, NULL);
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

  db_type = TP_DOMAIN_TYPE (attr_domain);

  if (TP_IS_SET_TYPE (db_type))
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
  dtype = TP_DOMAIN_TYPE (domain);
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
    case DB_TYPE_SEQUENCE:	/* DB_TYPE_LIST */
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

      size = strlen (p) + 1;
      if (precision_str[0] != '\0')
	{
	  size += strlen (precision_str) + 2;
	}
      if (scale_str[0] != '\0')
	{
	  size += strlen (scale_str) + 1;
	}
      domain_str = (char *) malloc (size);
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
		      ELSE 1 END";
    }
  else
    {
      case_stmt = "CASE WHEN is_system_class = 'YES' THEN 0 \
		      WHEN class_type = 'CLASS' THEN 2 \
		      ELSE 1 END";
    }
  where_vclass = "class_type = 'VCLASS'";

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
			     "WHERE class_name LIKE '%s' ESCAPE '%s' AND %s",
			     class_name, get_backslash_escape_string (),
			     where_vclass);
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
			     "WHERE class_name LIKE '%s' ESCAPE '%s' ",
			     class_name, get_backslash_escape_string ());
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
			 " AND class_name LIKE '%s' ESCAPE '%s' ",
			 class_name, get_backslash_escape_string ());
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
			 " AND attr_name LIKE '%s' ESCAPE '%s' ",
			 attr_name, get_backslash_escape_string ());
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
	      return ERROR_INFO_SET (CAS_ER_NO_MORE_MEMORY,
				     CAS_ERROR_INDICATOR);
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
sch_trigger (T_NET_BUF * net_buf, char *class_name, char flag, void **result)
{
  DB_OBJLIST *all_trigger = NULL, *tmp_trigger = NULL, *tmp_t;
  int num_trig = 0;
  MOP tmp_obj;
  DB_OBJECT *obj_trigger_target = NULL;
  const char *name_trigger_target = NULL;
  TR_TRIGGER *trigger = NULL;
  int error = NO_ERROR;
  bool is_pattern_match;

  is_pattern_match = (flag & CCI_CLASS_NAME_PATTERN_MATCH) ? true : false;

  if (class_name == NULL && !is_pattern_match)
    {
      goto end;
    }

  if (db_find_all_triggers (&tmp_trigger) < 0)
    {
      goto end;
    }

  if (class_name == NULL)
    {
      all_trigger = tmp_trigger;
      num_trig = ml_size (all_trigger);
    }
  else
    {
      for (tmp_t = tmp_trigger; tmp_t; tmp_t = tmp_t->next)
	{
	  tmp_obj = tmp_t->op;
	  assert (tmp_obj != NULL);

	  trigger = tr_map_trigger (tmp_obj, 1);
	  if (trigger == NULL)
	    {
	      error = er_errid ();
	      break;
	    }

	  obj_trigger_target = trigger->class_mop;
	  assert (obj_trigger_target != NULL);

	  name_trigger_target = sm_class_name (obj_trigger_target);
	  if (name_trigger_target == NULL)
	    {
	      error = er_errid ();
	      break;
	    }

	  if (is_pattern_match)
	    {
	      if (str_like ((char *) name_trigger_target, class_name, '\\') ==
		  1)
		{
		  error = ml_ext_add (&all_trigger, tmp_obj, NULL);
		  if (error != NO_ERROR)
		    {
		      break;
		    }
		  num_trig++;
		}
	    }
	  else
	    {
	      if (strcmp (class_name, name_trigger_target) == 0)
		{
		  error = ml_ext_add (&all_trigger, tmp_obj, NULL);
		  if (error != NO_ERROR)
		    {
		      break;
		    }
		  num_trig++;
		}
	    }
	}

      if (tmp_trigger)
	{
	  ml_ext_free (tmp_trigger);
	}

      if (error != NO_ERROR && all_trigger)
	{
	  ml_ext_free (all_trigger);
	  all_trigger = NULL;
	  num_trig = 0;
	}
    }

end:
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
		  return ERROR_INFO_SET (CAS_ER_NO_MORE_MEMORY,
					 CAS_ERROR_INDICATOR);
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
		{
		  return ERROR_INFO_SET (CAS_ER_NO_MORE_MEMORY,
					 CAS_ERROR_INDICATOR);
		}
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
	      return ERROR_INFO_SET (CAS_ER_NO_MORE_MEMORY,
				     CAS_ERROR_INDICATOR);
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
  db_type = TP_DOMAIN_TYPE (domain);

  attr_table->class_name = class_name;
  attr_table->attr_name = p;
  if (TP_IS_SET_TYPE (db_type))
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
  int err_code;

  lang_set_parser_use_client_charset (false);
  if (!(session = db_open_buffer (sql_stmt)))
    {
      lang_set_parser_use_client_charset (true);
      return ERROR_INFO_SET (CAS_ER_NO_MORE_MEMORY, CAS_ERROR_INDICATOR);
    }
  if ((stmt_id = db_compile_statement (session)) < 0)
    {
      err_code = ERROR_INFO_SET (stmt_id, DBMS_ERROR_INDICATOR);
      db_close_session (session);
      return err_code;
    }
  stmt_type = db_get_statement_type (session, stmt_id);
  lang_set_parser_use_client_charset (false);
  num_result = db_execute_statement (session, stmt_id, &result);
  lang_set_parser_use_client_charset (true);
#ifndef LIBCAS_FOR_JSP
  as_info->num_queries_processed %= MAX_DIAG_DATA_VALUE;
  as_info->num_queries_processed++;
#endif /* !LIBCAS_FOR_JSP */
  if (num_result < 0)
    {
      err_code = ERROR_INFO_SET (stmt_id, DBMS_ERROR_INDICATOR);
      db_close_session (session);
      return err_code;
    }

  /* success; peek the values in tuples */
  (void) db_query_set_copy_tplvalue (result, 0 /* peek */ );

  q_result = (T_QUERY_RESULT *) malloc (sizeof (T_QUERY_RESULT));
  if (q_result == NULL)
    {
      db_query_end (result);
      db_close_session (session);
      return ERROR_INFO_SET (CAS_ER_NO_MORE_MEMORY, CAS_ERROR_INDICATOR);
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
			 "WHERE class_name LIKE '%s' ESCAPE '%s' ",
			 class_name, get_backslash_escape_string ());
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
sch_primary_key (T_NET_BUF * net_buf, char *class_name,
		 T_SRV_HANDLE * srv_handle)
{
  char sql_stmt[QUERY_BUFFER_MAX], *sql_p = sql_stmt;
  int avail_size = sizeof (sql_stmt) - 1;
  int num_result;
  DB_OBJECT *class;

  ut_tolower (class_name);

  /* is it existing class? */
  class = db_find_class (class_name);
  if (class == NULL)
    {
      net_buf_cp_int (net_buf, 0, NULL);
      schema_primarykey_meta (net_buf);
      return 0;
    }

  STRING_APPEND (sql_p, avail_size,
		 "SELECT a.class_name, b.key_attr_name, b.key_order+1, a.index_name");
  STRING_APPEND (sql_p, avail_size,
		 " FROM db_index a, db_index_key b WHERE ");
  STRING_APPEND (sql_p, avail_size, " a.index_name = b.index_name ");
  STRING_APPEND (sql_p, avail_size, " AND a.is_primary_key = 'YES' ");
  STRING_APPEND (sql_p, avail_size, " AND a.class_name = '%s'", class_name);
  STRING_APPEND (sql_p, avail_size, " ORDER BY b.key_attr_name");

  if ((num_result = sch_query_execute (srv_handle, sql_stmt, net_buf)) < 0)
    {
      return num_result;
    }

  net_buf_cp_int (net_buf, num_result, NULL);
  schema_primarykey_meta (net_buf);

  return 0;
}

void
release_all_fk_info_results (T_FK_INFO_RESULT * fk_res)
{
  T_FK_INFO_RESULT *fk, *fk_release;

  fk = fk_res;
  while (fk != NULL)
    {
      fk_release = fk;
      fk = fk->next;

      FREE_MEM (fk_release->pktable_name);
      FREE_MEM (fk_release->pkcolumn_name);
      FREE_MEM (fk_release->fktable_name);
      FREE_MEM (fk_release->fkcolumn_name);
      FREE_MEM (fk_release->fk_name);
      FREE_MEM (fk_release->pk_name);
      FREE_MEM (fk_release);
    }
}

static void
add_fk_info_before (T_FK_INFO_RESULT * pivot, T_FK_INFO_RESULT * new)
{
  assert (pivot != NULL && new != NULL);
  new->prev = pivot->prev;
  if (new->prev != NULL)
    {
      new->prev->next = new;
    }
  pivot->prev = new;
  new->next = pivot;
}

static void
add_fk_info_after (T_FK_INFO_RESULT * pivot, T_FK_INFO_RESULT * new)
{
  assert (pivot != NULL && new != NULL);
  new->next = pivot->next;
  if (new->next != NULL)
    {
      new->next->prev = new;
    }
  pivot->next = new;
  new->prev = pivot;
}

static T_FK_INFO_RESULT *
add_fk_info_result (T_FK_INFO_RESULT * fk_res,
		    const char *pktable_name,
		    const char *pkcolumn_name,
		    const char *fktable_name,
		    const char *fkcolumn_name,
		    short key_seq,
		    SM_FOREIGN_KEY_ACTION update_action,
		    SM_FOREIGN_KEY_ACTION delete_action,
		    const char *fk_name, const char *pk_name, int sort_by)
{
  T_FK_INFO_RESULT *new_res, *t, *last;
  int cmp;

  assert (pktable_name != NULL && fktable_name != NULL);
  new_res = (T_FK_INFO_RESULT *) MALLOC (sizeof (T_FK_INFO_RESULT));
  if (new_res == NULL)
    {
      release_all_fk_info_results (fk_res);
      return NULL;
    }

  new_res->next = NULL;
  new_res->prev = NULL;

  new_res->pktable_name = pktable_name ? strdup (pktable_name) : NULL;
  new_res->pkcolumn_name = pkcolumn_name ? strdup (pkcolumn_name) : NULL;
  new_res->fktable_name = fktable_name ? strdup (fktable_name) : NULL;
  new_res->fkcolumn_name = fkcolumn_name ? strdup (fkcolumn_name) : NULL;
  new_res->key_seq = key_seq;
  new_res->update_action = update_action;
  new_res->delete_action = delete_action;
  new_res->fk_name = fk_name ? strdup (fk_name) : NULL;
  new_res->pk_name = pk_name ? strdup (pk_name) : NULL;

  if (fk_res == NULL)
    {
      return new_res;
    }

  /* insert new result into ordered list */
  t = last = fk_res;
  while (t != NULL)
    {
      if (sort_by == FK_INFO_SORT_BY_PKTABLE_NAME)
	{
	  cmp =
	    intl_identifier_casecmp (t->pktable_name, new_res->pktable_name);
	}
      else
	{
	  cmp =
	    intl_identifier_casecmp (t->fktable_name, new_res->fktable_name);
	}
      if (cmp > 0 || (cmp == 0 && t->key_seq > new_res->key_seq))
	{
	  add_fk_info_before (t, new_res);
	  if (t == fk_res)
	    {
	      fk_res = new_res;
	    }
	  break;
	}

      last = t;
      t = t->next;
    }
  if (t == NULL)
    {
      add_fk_info_after (last, new_res);
    }

  return fk_res;
}

static int
sch_imported_keys (T_NET_BUF * net_buf, char *fktable_name, void **result)
{
  DB_OBJECT *pktable_obj, *fktable_obj;
  DB_ATTRIBUTE **fk_attr = NULL, **pk_attr = NULL;
  DB_CONSTRAINT *fk_const = NULL, *pk = NULL, *pk_const = NULL;
  DB_CONSTRAINT_TYPE type;
  SM_FOREIGN_KEY_INFO *fk_info;
  T_FK_INFO_RESULT *fk_res = NULL;
  const char *pktable_name, *pk_name;
  int num_fk_info = 0, error = NO_ERROR, i;

  assert (result != NULL);
  *result = (void *) NULL;

  if (fktable_name == NULL)
    {
      return ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
    }

  fktable_obj = db_find_class (fktable_name);
  if (fktable_obj == NULL)
    {
      /* The followings are possible situations.
       *  - A table matching fktable_name does not exist.
       *  - User has no authorization on the table.
       *  - Other error we do not expect.
       * In these cases, we will send an empty result.
       * And this rule is also applied to CCI_SCH_EXPORTED_KEYS and CCI_SCH_CROSS_REFERENCE.
       */
      goto send_response;
    }

  for (fk_const = db_get_constraints (fktable_obj); fk_const != NULL;
       fk_const = db_constraint_next (fk_const))
    {
      type = db_constraint_type (fk_const);
      if (type != DB_CONSTRAINT_FOREIGN_KEY)
	{
	  continue;
	}

      fk_info = fk_const->fk_info;

      /* Find referenced table to get table name and columns. */
      pktable_obj = db_get_foreign_key_ref_class (fk_const);
      if (pktable_obj == NULL)
	{
	  error = ERROR_INFO_SET (db_error_code (), DBMS_ERROR_INDICATOR);
	  goto exit_on_error;
	}

      pktable_name = db_get_class_name (pktable_obj);
      if (pktable_name == NULL)
	{
	  error = ERROR_INFO_SET (db_error_code (), DBMS_ERROR_INDICATOR);
	  goto exit_on_error;
	}

      pk = db_constraint_find_primary_key (db_get_constraints (pktable_obj));
      if (pk == NULL)
	{
	  error = ERROR_INFO_SET_WITH_MSG (ER_FK_REF_CLASS_HAS_NOT_PK,
					   DBMS_ERROR_INDICATOR,
					   "Referenced class has no primary key.");
	  goto exit_on_error;
	}

      pk_name = db_constraint_name (pk);

      pk_attr = db_constraint_attributes (pk);
      if (pk_attr == NULL)
	{
	  error = ERROR_INFO_SET_WITH_MSG (ER_SM_INVALID_CONSTRAINT,
					   DBMS_ERROR_INDICATOR,
					   "Primary key has no attribute.");
	  goto exit_on_error;
	}

      fk_attr = db_constraint_attributes (fk_const);
      if (fk_attr == NULL)
	{
	  error = ERROR_INFO_SET_WITH_MSG (ER_SM_INVALID_CONSTRAINT,
					   DBMS_ERROR_INDICATOR,
					   "Foreign key has no attribute.");
	  goto exit_on_error;
	}

      for (i = 0; pk_attr[i] != NULL && fk_attr[i] != NULL; i++)
	{
	  fk_res = add_fk_info_result (fk_res,
				       pktable_name,
				       db_attribute_name (pk_attr[i]),
				       fktable_name,
				       db_attribute_name (fk_attr[i]),
				       (short) i + 1,
				       fk_info->update_action,
				       fk_info->delete_action,
				       fk_info->name,
				       pk_name, FK_INFO_SORT_BY_PKTABLE_NAME);
	  if (fk_res == NULL)
	    {
	      error =
		ERROR_INFO_SET (CAS_ER_NO_MORE_MEMORY, CAS_ERROR_INDICATOR);
	      goto exit_on_error;
	    }

	  num_fk_info++;
	}

      /* pk_attr and fk_attr is null-terminated array. So, they should be null
       * at this time. If one of them is not null, it means that they have
       * different number of attributes.
       */
      assert (pk_attr[i] == NULL && fk_attr[i] == NULL);
      if (pk_attr[i] != NULL || fk_attr[i] != NULL)
	{
	  error = ERROR_INFO_SET_WITH_MSG (ER_FK_NOT_MATCH_KEY_COUNT,
					   DBMS_ERROR_INDICATOR,
					   "The number of keys of the foreign "
					   "key is different from that of the "
					   "primary key.");
	  goto exit_on_error;
	}
    }

  *result = (void *) fk_res;

send_response:
  net_buf_cp_int (net_buf, num_fk_info, NULL);
  schema_fk_info_meta (net_buf);

  return NO_ERROR;

exit_on_error:
  if (fk_res != NULL)
    {
      release_all_fk_info_results (fk_res);
    }
  return error;
}

static int
sch_exported_keys_or_cross_reference (T_NET_BUF * net_buf,
				      bool find_cross_ref, char *pktable_name,
				      char *fktable_name, void **result)
{
  DB_OBJECT *pktable_obj, *fktable_obj;
  DB_ATTRIBUTE **pk_attr = NULL, **fk_attr;
  DB_CONSTRAINT *fk_const = NULL, *pk = NULL;
  SM_FOREIGN_KEY_INFO *fk_info;
  T_FK_INFO_RESULT *fk_res = NULL;
  const char *pk_name;
  int num_fk_info = 0, error = NO_ERROR, i;

  assert (result != NULL);
  *result = (void *) NULL;

  if (pktable_name == NULL)
    {
      return ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
    }

  pktable_obj = db_find_class (pktable_name);
  if (pktable_obj == NULL)
    {
      goto send_response;
    }

  if (find_cross_ref)
    {
      /* If find_cross_ref is true, we will try to find cross reference
       * between primary table and foreign table. Otherwise, we will find
       * all foreign keys referring primary key of given table.
       */
      if (fktable_name == NULL)
	{
	  return ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
	}

      fktable_obj = db_find_class (fktable_name);
      if (fktable_obj == NULL)
	{
	  goto send_response;
	}
    }

  /* If there is no primary key, we will send an empty result. */
  pk = db_constraint_find_primary_key (db_get_constraints (pktable_obj));
  if (pk == NULL)
    {
      goto send_response;
    }

  pk_name = db_constraint_name (pk);

  pk_attr = db_constraint_attributes (pk);
  if (pk_attr == NULL)
    {
      return ERROR_INFO_SET_WITH_MSG (ER_SM_INVALID_CONSTRAINT,
				      DBMS_ERROR_INDICATOR,
				      "Primary key has no attribute.");
    }

  for (fk_info = pk->fk_info; fk_info != NULL; fk_info = fk_info->next)
    {
      if (find_cross_ref)
	{
	  if (WS_ISVID (fktable_obj) ||
	      oid_compare (WS_REAL_OID (fktable_obj),
			   &(fk_info->self_oid)) != 0)
	    {
	      continue;
	    }
	}
      else
	{
	  fktable_obj = ws_mop (&(fk_info->self_oid), NULL);
	  if (fktable_obj == NULL)
	    {
	      error = ERROR_INFO_SET (db_error_code (), DBMS_ERROR_INDICATOR);
	      goto exit_on_error;
	    }

	  fktable_name = (char *) db_get_class_name (fktable_obj);
	  if (fktable_name == NULL)
	    {
	      error = ERROR_INFO_SET (db_error_code (), DBMS_ERROR_INDICATOR);
	      goto exit_on_error;
	    }
	}

      /* Traverse all constraints in foreign table to find a foreign key
       * referring the primary key. If there is no one, return an error.
       */
      fk_attr = NULL;
      for (fk_const = db_get_constraints (fktable_obj);
	   fk_const != NULL; fk_const = db_constraint_next (fk_const))
	{
	  if (db_constraint_type (fk_const) == SM_CONSTRAINT_FOREIGN_KEY
	      && BTID_IS_EQUAL (&(fk_const->fk_info->ref_class_pk_btid),
				&(pk->index_btid)))
	    {
	      fk_attr = db_constraint_attributes (fk_const);
	      if (fk_attr == NULL)
		{
		  error = ERROR_INFO_SET_WITH_MSG (ER_SM_INVALID_CONSTRAINT,
						   DBMS_ERROR_INDICATOR,
						   "Foreign key has no attribute.");
		  goto exit_on_error;
		}
	      break;
	    }
	}
      if (fk_attr == NULL)
	{
	  error = ERROR_INFO_SET_WITH_MSG (ER_SM_INVALID_CONSTRAINT,
					   DBMS_ERROR_INDICATOR,
					   "Primary key has foreign key information, "
					   "but there is no one referring it.");
	  goto exit_on_error;
	}

      for (i = 0; pk_attr[i] != NULL && fk_attr[i] != NULL; i++)
	{
	  fk_res = add_fk_info_result (fk_res,
				       pktable_name,
				       db_attribute_name (pk_attr[i]),
				       fktable_name,
				       db_attribute_name (fk_attr[i]),
				       (short) i + 1,
				       fk_info->update_action,
				       fk_info->delete_action,
				       fk_info->name,
				       pk_name, FK_INFO_SORT_BY_FKTABLE_NAME);
	  if (fk_res == NULL)
	    {
	      error =
		ERROR_INFO_SET (CAS_ER_NO_MORE_MEMORY, CAS_ERROR_INDICATOR);
	      goto exit_on_error;
	    }

	  num_fk_info++;
	}

      /* pk_attr and fk_attr is null-terminated array. So, they should be null
       * at this time. If one of them is not null, it means that they have
       * different number of attributes.
       */
      assert (pk_attr[i] == NULL && fk_attr[i] == NULL);
      if (pk_attr[i] != NULL || fk_attr[i] != NULL)
	{
	  error = ERROR_INFO_SET_WITH_MSG (ER_FK_NOT_MATCH_KEY_COUNT,
					   DBMS_ERROR_INDICATOR,
					   "The number of keys of the foreign "
					   "key is different from that of the "
					   "primary key.");
	  goto exit_on_error;
	}
    }

  *result = (void *) fk_res;

send_response:
  net_buf_cp_int (net_buf, num_fk_info, NULL);
  schema_fk_info_meta (net_buf);

  return NO_ERROR;

exit_on_error:
  if (fk_res != NULL)
    {
      release_all_fk_info_results (fk_res);
    }
  return error;
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
      err_code = ERROR_INFO_SET (CAS_ER_SRV_HANDLE, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (net_buf);
      return err_code;
    }

  net_buf_cp_int (net_buf, 1, NULL);	/* tuple count */
  net_buf_cp_int (net_buf, 1, NULL);	/* cursor position */
  memset (&tuple_obj, 0, sizeof (T_OBJECT));
  net_buf_cp_object (net_buf, &tuple_obj);

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
      err_code = ERROR_INFO_SET (CAS_ER_NO_MORE_MEMORY, CAS_ERROR_INDICATOR);
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
	create_srv_handle_with_query_result ((DB_QUERY_RESULT *)
					     q_result->result,
					     q_result->num_column,
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
      err_code = ERROR_INFO_SET (err_code, DBMS_ERROR_INDICATOR);
      goto ux_get_generated_keys_error;
    }

  obj = db_get_object (&oid_val);
  dbobj_to_casobj (obj, &t_object_autoincrement);

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
	  temp_type = TP_DOMAIN_TYPE (domain);
	  set_type = ux_db_type_to_cas_type (temp_type);

	  attr_name = (char *) db_attribute_name (attr);
	  err_code = db_get (obj, attr_name, &value);
	  if (err_code < 0)
	    {
	      err_code = ERROR_INFO_SET (err_code, DBMS_ERROR_INDICATOR);
	      goto ux_get_generated_keys_error;
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
  net_buf_cp_object (net_buf, &t_object_autoincrement);	/* readOID 8 byte */
  net_buf_cp_str (net_buf, tuple_buf->data + NET_BUF_HEADER_SIZE,
		  tuple_buf->data_size);
  net_buf_clear (tuple_buf);
  net_buf_destroy (tuple_buf);

  return 0;

ux_get_generated_keys_error:
  NET_BUF_ERR_SET (net_buf);
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
      err_code = ERROR_INFO_SET (CAS_ER_SRV_HANDLE, CAS_ERROR_INDICATOR);
      goto ux_make_out_rs_error;
    }

  q_result = (T_QUERY_RESULT *) srv_handle->cur_result;
  if (q_result->stmt_type != CUBRID_STMT_SELECT)
    {
      err_code = ERROR_INFO_SET (CAS_ER_SRV_HANDLE, CAS_ERROR_INDICATOR);
      goto ux_make_out_rs_error;
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
      err_code = ERROR_INFO_SET (CAS_ER_NO_MORE_MEMORY, CAS_ERROR_INDICATOR);
      goto ux_make_out_rs_error;
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
	  err_code = ERROR_INFO_SET (CAS_ER_SRV_HANDLE, CAS_ERROR_INDICATOR);
	  goto ux_make_out_rs_error;
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
      db_type = TP_DOMAIN_TYPE (domain);
      if (TP_IS_SET_TYPE (db_type))
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
#else /* !LIBCAS_FOR_JSP */
      /* precision = DB_MAX_STRING_LENGTH; */
#endif /* !LIBCAS_FOR_JSP */

      set_column_info (net_buf,
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
ux_make_out_rs_error:
  NET_BUF_ERR_SET (net_buf);
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
#else /* !LIBCAS_FOR_JSP */

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

  return err_code;
#endif /* !LIBCAS_FOR_JSP */

  return -1;
}

static bool
has_stmt_result_set (char stmt_type)
{
  if (stmt_type == CUBRID_STMT_SELECT
      || stmt_type == CUBRID_STMT_CALL
      || stmt_type == CUBRID_STMT_GET_STATS
      || stmt_type == CUBRID_STMT_EVALUATE)
    {
      return true;
    }

  return false;
}

#ifndef LIBCAS_FOR_JSP
static bool
check_auto_commit_after_fetch_done (T_SRV_HANDLE * srv_handle)
{
  if (srv_handle->num_q_result < 2
      && ((has_stmt_result_set (srv_handle->q_result->stmt_type) == true
	   && srv_handle->auto_commit_mode == TRUE
	   && srv_handle->forward_only_cursor == TRUE)
	  || (shm_appl->select_auto_commit == ON
	      && hm_srv_handle_is_all_active_fetch_completed () == true)))
    {
      return true;
    }

  return false;
}
#endif /* !LIBCAS_FOR_JSP */

int
get_db_connect_status (void)
{
  return db_Connect_status;
}

void
set_db_connect_status (int status)
{
  db_Connect_status = status;
}

void
cas_log_error_handler (unsigned int eid)
{
  if (cas_EHCTX == NULL)
    {
      return;
    }

  if (cas_EHCTX->from == 0)
    {
      cas_EHCTX->from = eid;
    }
  else
    {
      cas_EHCTX->to = eid;
    }
}

/*****************************
  move from cas_log.c
 *****************************/
void
cas_log_error_handler_begin (void)
{
  CAS_ERROR_LOG_HANDLE_CONTEXT *ectx;

  ectx = malloc (sizeof (*ectx));
  if (ectx == NULL)
    {
      return;
    }

  ectx->from = 0;
  ectx->to = 0;

  if (cas_EHCTX != NULL)
    {
      free (cas_EHCTX);
    }

  cas_EHCTX = ectx;
  (void) db_register_error_log_handler (cas_log_error_handler);
}

void
cas_log_error_handler_end (void)
{
  if (cas_EHCTX != NULL)
    {
      free (cas_EHCTX);
      cas_EHCTX = NULL;
      (void) db_register_error_log_handler (NULL);
    }
}

void
cas_log_error_handler_clear (void)
{
  if (cas_EHCTX == NULL)
    {
      return;
    }

  cas_EHCTX->from = 0;
  cas_EHCTX->to = 0;
}


char *
cas_log_error_handler_asprint (char *buf, size_t bufsz, bool clear)
{
  char *buf_p;
  unsigned int from, to;

  if (buf == NULL || bufsz <= 0)
    {
      return NULL;
    }

  if (cas_EHCTX == NULL || cas_EHCTX->from == 0)
    {
      buf[0] = '\0';
      return buf;
    }

  from = cas_EHCTX->from;
  to = cas_EHCTX->to;

  if (clear)
    {
      cas_EHCTX->from = 0;
      cas_EHCTX->to = 0;
    }

  /* ", EID = <int> ~ <int>" : 32 bytes suffice */
  if (bufsz < 32)
    {
      buf_p = malloc (32);

      if (buf_p == NULL)
	{
	  return NULL;
	}
    }
  else
    {
      buf_p = buf;
    }

  /* actual print */
  if (to != 0)
    {
      snprintf (buf_p, 32, ", EID = %u ~ %u", from, to);
    }
  else
    {
      snprintf (buf_p, 32, ", EID = %u", from);
    }

  return buf_p;
}

int
get_tuple_count (T_SRV_HANDLE * srv_handle)
{
  return srv_handle->q_result->tuple_count;
}

/*****************************
  move from cas_sql_log2.c
 *****************************/
void
set_optimization_level (int level)
{
  DB_QUERY_RESULT *result = NULL;
  DB_QUERY_ERROR error;
  char sql_stmt[64];

  sprintf (sql_stmt, "set optimization level = %d", level);
  db_execute (sql_stmt, &result, &error);
  if (result)
    db_query_end (result);
}

int
ux_lob_new (int lob_type, T_NET_BUF * net_buf)
{
  DB_VALUE lob_dbval;
  int err_code;
  T_LOB_HANDLE cas_lob;
  int lob_handle_size;


  err_code = db_create_fbo (&lob_dbval,
			    (lob_type == CCI_U_TYPE_BLOB)
			    ? DB_TYPE_BLOB : DB_TYPE_CLOB);
  cas_log_debug (ARG_FILE_LINE, "ux_lob_new: result_code=%d", err_code);
  if (err_code < 0)
    {
      errors_in_transaction++;
      err_code = ERROR_INFO_SET (err_code, DBMS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (net_buf);
      return err_code;
    }

  /* set result */
  dblob_to_caslob (&lob_dbval, &cas_lob);
  lob_handle_size =
    NET_SIZE_INT + NET_SIZE_INT64 + NET_SIZE_INT + cas_lob.locator_size;
  net_buf_cp_int (net_buf, lob_handle_size, NULL);
  net_buf_cp_lob_handle (net_buf, &cas_lob);

  cas_log_debug (ARG_FILE_LINE, "ux_lob_new: locator=%s, size=%lld, type=%u",
		 db_get_elo (&lob_dbval)->locator,
		 db_get_elo (&lob_dbval)->size,
		 db_get_elo (&lob_dbval)->type);

  db_value_clear (&lob_dbval);
  return 0;
}

int
ux_lob_write (DB_VALUE * lob_dbval, INT64 offset, int size, char *data,
	      T_NET_BUF * net_buf)
{
  DB_BIGINT size_written;
  int err_code;

  cas_log_debug (ARG_FILE_LINE,
		 "ux_lob_write: locator=%s, size=%lld, type=%u",
		 db_get_elo (lob_dbval)->locator,
		 db_get_elo (lob_dbval)->size, db_get_elo (lob_dbval)->type);

  err_code =
    db_elo_write (db_get_elo (lob_dbval), offset, data, size, &size_written);
  cas_log_debug (ARG_FILE_LINE, "ux_lob_write: result_code=%d", size_written);
  if (err_code < 0)
    {
      errors_in_transaction++;
      err_code = ERROR_INFO_SET (err_code, DBMS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (net_buf);
      return err_code;
    }

  /* set result: on success, bytes written */
  net_buf_cp_int (net_buf, (int) size_written, NULL);

  return 0;
}

int
ux_lob_read (DB_VALUE * lob_dbval, INT64 offset, int size,
	     T_NET_BUF * net_buf)
{
  DB_BIGINT size_read;
  int err_code;
  char *data = NET_BUF_CURR_PTR (net_buf) + NET_SIZE_INT;

  cas_log_debug (ARG_FILE_LINE, "ux_lob_read: locator=%s, size=%lld, type=%u",
		 db_get_elo (lob_dbval)->locator,
		 db_get_elo (lob_dbval)->size, db_get_elo (lob_dbval)->type);

  if (size + NET_SIZE_INT > NET_BUF_FREE_SIZE (net_buf))
    {
      size = NET_BUF_FREE_SIZE (net_buf) - NET_SIZE_INT;
      cas_log_debug (ARG_FILE_LINE, "ux_lob_read: length reduced to %d",
		     size);
    }

  err_code =
    db_elo_read (db_get_elo (lob_dbval), offset, data, size, &size_read);
  cas_log_debug (ARG_FILE_LINE, "ux_lob_read: result_code=%d size_read=%lld",
		 err_code, size_read);
  if (err_code < 0)
    {
      errors_in_transaction++;
      err_code = ERROR_INFO_SET (err_code, DBMS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (net_buf);
      return err_code;
    }

  /* set result: on success, bytes read */
  net_buf_cp_int (net_buf, (int) size_read, NULL);
  net_buf->data_size += size_read;

  return 0;
}

/* converting a DB_VALUE to a char taking care of nchar strings */
static char *
convert_db_value_to_string (DB_VALUE * value, DB_VALUE * value_string)
{
  char *val_str = NULL;
  DB_TYPE val_type;
  int err, len;

  val_type = db_value_type (value);

  if (val_type == DB_TYPE_NCHAR || val_type == DB_TYPE_VARNCHAR)
    {
      err = db_value_coerce (value, value_string,
			     db_type_to_db_domain (DB_TYPE_VARNCHAR));
      if (err >= 0)
	{
	  val_str = db_get_nchar (value_string, &len);
	}
    }
  else
    {
      err = db_value_coerce (value, value_string,
			     db_type_to_db_domain (DB_TYPE_VARCHAR));
      if (err >= 0)
	{
	  val_str = db_get_char (value_string, &len);
	}
    }

  return val_str;
}

/*
 * serialize_collection_as_string() - This function builds a string with all
 *  the values in the collection converted to string.
 * col(in): collection value
 * out(out): out string
 */
static void
serialize_collection_as_string (DB_VALUE * col, char **out)
{
  DB_COLLECTION *db_set;
  DB_VALUE value, value_string;
  int i, size;
  int needed_size = 0;
  char *single_value = NULL;

  *out = NULL;

  if (!TP_IS_SET_TYPE (db_value_type (col)))
    {
      return;
    }

  db_set = db_get_collection (col);
  size = db_set_size (db_set);

  /* first compute the size of the result */
  for (i = 0; i < size; i++)
    {
      if (db_set_get (db_set, i, &value) != NO_ERROR)
	{
	  return;
	}

      single_value = convert_db_value_to_string (&value, &value_string);
      if (single_value == NULL)
	{
	  db_value_clear (&value);
	  return;
	}

      needed_size += strlen (single_value);

      db_value_clear (&value_string);
      db_value_clear (&value);
    }

  /* now compute the result */
  needed_size += 2 * size;	/* for ", " */
  needed_size += 2 + 1;		/* for {} and \0 */

  *out = (char *) MALLOC (needed_size);
  if (*out == NULL)
    {
      return;
    }

  strcpy (*out, "{");

  for (i = 0; i < size; i++)
    {
      if (db_set_get (db_set, i, &value) != NO_ERROR)
	{
	  FREE (*out);
	  *out = NULL;
	  return;
	}

      single_value = convert_db_value_to_string (&value, &value_string);
      if (single_value == NULL)
	{
	  db_value_clear (&value);

	  FREE (*out);
	  *out = NULL;
	  return;
	}

      strcat (*out, single_value);
      if (i != size - 1)
	{
	  strcat (*out, ", ");
	}

      db_value_clear (&value_string);
      db_value_clear (&value);
    }

  strcat (*out, "}");
}

/*
 * get_backslash_escape_string() - This function returns proper backslash escape
 * string according to the value of 'no_backslash_escapes' confiuration.
 */
static char *
get_backslash_escape_string (void)
{
  if (prm_get_bool_value (PRM_ID_NO_BACKSLASH_ESCAPES))
    {
      return (char *) "\\";
    }
  else
    {
      return (char *) "\\\\";
    }
}
