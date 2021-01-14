/*
 * Copyright (C) 2008 Search Solution Corporation. 
 * Copyright (c) 2016 CUBRID Corporation.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * - Neither the name of the <ORGANIZATION> nor the names of its contributors
 *   may be used to endorse or promote products derived from this software without
 *   specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 */


/*
 * cci_handle_mng.h -
 */

#ifndef	_CCI_HANDLE_MNG_H_
#define	_CCI_HANDLE_MNG_H_

#ident "$Id$"

#ifdef CAS
#error include error
#endif

#include "config.h"

/************************************************************************
 * IMPORTED SYSTEM HEADER FILES						*
 ************************************************************************/

/************************************************************************
 * OTHER IMPORTED HEADER FILES						*
 ************************************************************************/

#include "cci_common.h"
#include "cas_cci.h"
#include "cas_protocol.h"
#include <openssl/ssl.h>

/************************************************************************
 * PUBLIC DEFINITIONS							*
 ************************************************************************/

#define QUERY_RESULT_FREE(REQ_HANDLE)		\
	do {					\
	  qe_query_result_free((REQ_HANDLE)->num_query_res, (REQ_HANDLE)->qr); \
	  (REQ_HANDLE)->cur_fetch_tuple_index = 0; \
	  (REQ_HANDLE)->num_query_res = 0;	\
	  (REQ_HANDLE)->current_query_res = 0;  \
	  (REQ_HANDLE)->num_tuple = 0;		\
	  (REQ_HANDLE)->qr = NULL;		\
	} while (0)

#define ALTER_HOST_MAX_SIZE                     256
#define DEFERRED_CLOSE_HANDLE_ALLOC_SIZE        256
#define MONITORING_INTERVAL		    	60

#define DOES_CONNECTION_HAVE_STMT_POOL(c) \
  ((c)->datasource && (c)->datasource->pool_prepared_statement)
#define HAS_REACHED_LIMIT_OPEN_STATEMENT(c) \
  ((c)->open_prepared_statement_count >= (c)->datasource->max_open_prepared_statement)

#define REACHABLE       true
#define UNREACHABLE     false
/************************************************************************
 * PUBLIC TYPE DEFINITIONS						*
 ************************************************************************/

typedef enum
{
  CCI_CON_STATUS_OUT_TRAN = 0,
  CCI_CON_STATUS_IN_TRAN = 1
} T_CCI_CON_STATUS;

typedef struct t_cci_session_id T_CCI_SESSION_ID;
struct t_cci_session_id
{
  char id[DRIVER_SESSION_SIZE];
};

typedef enum
{
  HANDLE_PREPARE,
  HANDLE_OID_GET,
  HANDLE_SCHEMA_INFO,
  HANDLE_COL_GET
} T_HANDLE_TYPE;

typedef struct
{
  int pageid;
  short slotid;
  short volid;
} T_OBJECT;

typedef struct
{
  int tuple_index;
  T_OBJECT tuple_oid;
  char **column_ptr;
  char **decoded_ptr;
} T_TUPLE_VALUE;

typedef struct
{
  T_CCI_U_TYPE u_type;		/* primary type (without any collection flags) */
  int size;			/* bind_param : value size bind_param_array : a_type of value */
  void *value;
  int *null_ind;
  char flag;
} T_BIND_VALUE;

typedef struct
{
  int size;
  void *data;
} T_VALUE_BUF;

typedef struct
{
  int req_handle_index;
  int mapped_stmt_id;
  char prepare_flag;
  char execute_flag;
  char handle_type;
  char handle_sub_type;
  char updatable_flag;
  char *sql_text;
  int max_row;
  int server_handle_id;
  int num_tuple;
  T_CCI_CUBRID_STMT stmt_type;
  T_CCI_CUBRID_STMT first_stmt_type;
  int num_bind;
  T_BIND_VALUE *bind_value;
  char *bind_mode;
  T_CCI_COL_INFO *col_info;
  int bind_array_size;
  int num_col_info;
  int fetch_size;
  char *msg_buf;
  int cursor_pos;
  int fetched_tuple_begin;
  int fetched_tuple_end;
  int cur_fetch_tuple_index;
  T_TUPLE_VALUE *tuple_value;
  T_VALUE_BUF conv_value_buffer;
  T_CCI_QUERY_RESULT *qr;
  int num_query_res;
  int current_query_res;
  int valid;
  int query_timeout;
  int is_closed;
  int is_from_current_transaction;
  int shard_id;
  char is_fetch_completed;	/* used only cas4oracle */
  void *prev;
  void *next;
} T_REQ_HANDLE;

typedef struct
{
  T_REQ_HANDLE *req_handle;
  char flag;
  int max_col_size;
  T_CCI_ERROR err_buf;
  int ret_code;
  void *con_handle;		/* for thread processing CAS_ER_STMT_POOLING */
} T_EXEC_THR_ARG;

typedef struct
{
  unsigned char ip_addr[4];
  int port;
} T_ALTER_HOST;

typedef struct
{
  SSL *ssl;
  SSL_CTX *ctx;
  bool is_connected;
} T_SSL_HANDLE;

typedef struct
{
  int id;
  char used;
  char is_retry;
  char con_status;
  CCI_AUTOCOMMIT_MODE autocommit_mode;
  unsigned char ip_addr[4];
  int port;
  char *db_name;
  char *db_user;
  char *db_passwd;
  char url[SRV_CON_URL_SIZE];
  SOCKET sock_fd;
  int max_req_handle;
  T_EXEC_THR_ARG thr_arg;
  T_REQ_HANDLE **req_handle_table;
  int req_handle_count;
  int open_prepared_statement_count;
  int cas_pid;
  char broker_info[BROKER_INFO_SIZE];
  char cas_info[CAS_INFO_SIZE];
  int cas_id;
  T_CCI_SESSION_ID session_id;
  T_CCI_DATASOURCE *datasource;
  CCI_MHT_TABLE *stmt_pool;

  /* HA */
  int alter_host_count;
  int alter_host_id;		/* current connected alternative host id */

  /* The connection properties are not supported by the URL */
  T_CCI_TRAN_ISOLATION isolation_level;
  int lock_timeout;
  char *charset;

  /* connection properties */
  T_ALTER_HOST alter_hosts[ALTER_HOST_MAX_SIZE];
  char load_balance;
  char force_failback;
  int rc_time;			/* failback try duration */
  int last_failure_time;
  T_REQ_HANDLE *pool_lru_head;
  T_REQ_HANDLE *pool_lru_tail;
  T_REQ_HANDLE *pool_use_head;
  T_REQ_HANDLE *pool_use_tail;
  int login_timeout;
  int query_timeout;
  char disconnect_on_query_timeout;
  char *log_filename;
  char log_on_exception;
  char log_slow_queries;
  int slow_query_threshold_millis;
  char log_trace_api;
  char log_trace_network;
  char useSSL;

  /* to check timeout */
  struct timeval start_time;	/* function start time to check timeout */
  int current_timeout;		/* login_timeout or query_timeout */
  int deferred_max_close_handle_count;
  int *deferred_close_handle_list;
  int deferred_close_handle_count;
  void *logger;
  int is_holdable;
  int no_backslash_escapes;
  char *last_insert_id;
  T_CCI_ERROR err_buf;

  /* shard */
  int shard_id;

  /* ssl */
  T_SSL_HANDLE ssl_handle;

} T_CON_HANDLE;

/************************************************************************
 * PUBLIC FUNCTION PROTOTYPES						*
 ************************************************************************/

extern void hm_con_handle_table_init (void);
extern T_CON_HANDLE *hm_con_handle_alloc (char *ip_str, int port, char *db_name, char *db_user, char *db_passwd);
extern int hm_req_handle_alloc (T_CON_HANDLE * connection, T_REQ_HANDLE ** statement);
extern void hm_req_handle_free (T_CON_HANDLE * con_handle, T_REQ_HANDLE * req_handle);
extern void hm_req_handle_free_all (T_CON_HANDLE * con_handle);
extern void hm_req_handle_free_all_unholdable (T_CON_HANDLE * con_handle);
extern void hm_req_handle_close_all_resultsets (T_CON_HANDLE * con_handle);
extern void hm_req_handle_close_all_unholdable_resultsets (T_CON_HANDLE * con_handle);
extern int hm_con_handle_free (T_CON_HANDLE * connection);

extern T_CCI_ERROR_CODE hm_get_connection_by_resolved_id (int resolved_id, T_CON_HANDLE ** connection);
extern T_CCI_ERROR_CODE hm_get_connection_force (int mapped_id, T_CON_HANDLE ** connection);
extern T_CCI_ERROR_CODE hm_get_connection (int connection_id, T_CON_HANDLE ** connection);
extern T_CCI_ERROR_CODE hm_get_statement (int statement_id, T_CON_HANDLE ** connection, T_REQ_HANDLE ** statement);
extern T_CCI_ERROR_CODE hm_release_connection (int connection_id, T_CON_HANDLE ** connection);
extern T_CCI_ERROR_CODE hm_delete_connection (int connection_id, T_CON_HANDLE ** connection);
extern T_CCI_ERROR_CODE hm_release_statement (int statement_id, T_CON_HANDLE ** connection, T_REQ_HANDLE ** statement);
extern void hm_req_handle_fetch_buf_free (T_REQ_HANDLE * req_handle);
extern int hm_conv_value_buf_alloc (T_VALUE_BUF * val_buf, int size);

extern void req_handle_col_info_free (T_REQ_HANDLE * req_handle);
extern void hm_conv_value_buf_clear (T_VALUE_BUF * val_buf);
extern void req_handle_content_free (T_REQ_HANDLE * req_handle, int reuse);
extern void req_handle_content_free_for_pool (T_REQ_HANDLE * req_handle);
extern int req_close_query_result (T_REQ_HANDLE * req_handle);
extern void hm_invalidate_all_req_handle (T_CON_HANDLE * con_handle);
extern int hm_ip_str_to_addr (char *ip_str, unsigned char *ip_addr);
extern T_CON_HANDLE *hm_get_con_from_pool (unsigned char *ip_addr, int port, char *dbname, char *dbuser,
					   char *dbpasswd);
extern int hm_put_con_to_pool (int con);

extern T_BROKER_VERSION hm_get_broker_version (T_CON_HANDLE * con_handle);
extern bool hm_broker_understand_renewed_error_code (T_CON_HANDLE * con_handle);
extern bool hm_broker_understand_the_protocol (T_BROKER_VERSION broker_version, int require);
extern bool hm_broker_match_the_protocol (T_BROKER_VERSION broker_version, int require);

extern bool hm_broker_support_holdable_result (T_CON_HANDLE * con_handle);
extern bool hm_broker_reconnect_when_server_down (T_CON_HANDLE * con_handle);

extern void hm_set_con_handle_holdable (T_CON_HANDLE * con_handle, int holdable);
extern int hm_get_con_handle_holdable (T_CON_HANDLE * con_handle);
extern int hm_get_req_handle_holdable (T_CON_HANDLE * con_handle, T_REQ_HANDLE * req_handle);

extern int hm_req_add_to_pool (T_CON_HANDLE * con, char *sql, int req_id, T_REQ_HANDLE * req);
extern int hm_req_get_from_pool (T_CON_HANDLE * con, T_REQ_HANDLE ** req, const char *sql);

extern int cci_conn_set_properties (T_CON_HANDLE * handle, char *properties);

extern void hm_set_host_status (T_CON_HANDLE * con_handle, int host_id, bool is_reachable);
extern bool hm_is_host_reachable (T_CON_HANDLE * con_handle, int host_id);
extern void hm_check_rc_time (T_CON_HANDLE * con_handle);
extern void hm_create_health_check_th (char useSSL);

extern int hm_pool_restore_used_statements (T_CON_HANDLE * connection);
extern int hm_pool_add_statement_to_use (T_CON_HANDLE * connection, int statement_id);

extern bool hm_is_empty_session (T_CCI_SESSION_ID * id);
extern void hm_make_empty_session (T_CCI_SESSION_ID * id);

extern void hm_force_close_connection (T_CON_HANDLE * con_handle);

extern void hm_ssl_free (T_CON_HANDLE * con_handle);
/************************************************************************
 * PUBLIC VARIABLES							*
 ************************************************************************/


#endif /* _CCI_HANDLE_MNG_H_ */
