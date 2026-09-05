/*
 *
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */
/*
 * cas_dispatch.c - the CAS request dispatch, extracted from cas.c /
 *                  cas_common_main.c (stage B1, #117)
 *
 * Everything here is moved VERBATIM so the same request loop serves both the
 * standalone cub_cas process and the folded in-server CAS speaker
 * (driver_session.cpp): the function table, process_request (extern as
 * cas_process_request), its keep-connection header readers, and the
 * restart/db-parameter helpers they call.  Only the linkage changed.
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#if defined(WINDOWS)
#include <winsock2.h>
#include <windows.h>
#else
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/time.h>
#endif

#include "cas_common.h"
#include "cas_common_main.h"
#include "cas_common_vars.h"
#include "broker_shm.h"
#include "broker_filename.h"
#include "cas_log.h"
#include "cas_common_execute.h"
#include "perf_monitor.h"
#include "cas_sql_log2.h"
#include "error_manager.h"
#include "ddl_log.h"
#include "cas.h"
#include "cas_dispatch.h"
#include "cas_network.h"
#include "cas_function.h"
#include "cas_net_buf.h"
#include "cas_execute.h"
#include "query_replace.h"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/* protocol violation bound for a single request body (mirrors
 * driver_session.cpp REQUEST_BODY_MAX) */
#define CAS_DISPATCH_REQUEST_BODY_MAX (16 * 1024 * 1024)

static void set_db_parameter (void);

static T_SERVER_FUNC server_fn_table[] = {
  fn_end_tran,			/* CAS_FC_END_TRAN */
  fn_prepare,			/* CAS_FC_PREPARE */
  fn_execute,			/* CAS_FC_EXECUTE */
  fn_get_db_parameter,		/* CAS_FC_GET_DB_PARAMETER */
  fn_set_db_parameter,		/* CAS_FC_SET_DB_PARAMETER */
  fn_close_req_handle,		/* CAS_FC_CLOSE_REQ_HANDLE */
  fn_cursor,			/* CAS_FC_CURSOR */
  fn_fetch,			/* CAS_FC_FETCH */
  fn_schema_info,		/* CAS_FC_SCHEMA_INFO */
  fn_oid_get,			/* CAS_FC_OID_GET */
  fn_oid_put,			/* CAS_FC_OID_SET */
  fn_deprecated,		/* CAS_FC_DEPRECATED1 *//* fn_glo_new */
  fn_deprecated,		/* CAS_FC_DEPRECATED2 *//* fn_glo_save */
  fn_deprecated,		/* CAS_FC_DEPRECATED3 *//* fn_glo_load */
  fn_get_db_version,		/* CAS_FC_GET_DB_VERSION */
  fn_get_class_num_objs,	/* CAS_FC_GET_CLASS_NUM_OBJS */
  fn_oid,			/* CAS_FC_OID_CMD */
  fn_collection,		/* CAS_FC_COLLECTION */
  fn_next_result,		/* CAS_FC_NEXT_RESULT */
  fn_execute_batch,		/* CAS_FC_EXECUTE_BATCH */
  fn_execute_array,		/* CAS_FC_EXECUTE_ARRAY */
  fn_cursor_update,		/* CAS_FC_CURSOR_UPDATE */
  fn_get_attr_type_str,		/* CAS_FC_GET_ATTR_TYPE_STR */
  fn_get_query_info,		/* CAS_FC_GET_QUERY_INFO */
  fn_deprecated,		/* CAS_FC_DEPRECATED4 *//* fn_glo_cmd */
  fn_savepoint,			/* CAS_FC_SAVEPOINT */
  fn_parameter_info,		/* CAS_FC_PARAMETER_INFO */
  fn_xa_prepare,		/* CAS_FC_XA_PREPARE */
  fn_xa_recover,		/* CAS_FC_XA_RECOVER */
  fn_xa_end_tran,		/* CAS_FC_XA_END_TRAN */
  fn_con_close,			/* CAS_FC_CON_CLOSE */
  fn_check_cas,			/* CAS_FC_CHECK_CAS */
  fn_make_out_rs,		/* CAS_FC_MAKE_OUT_RS */
  fn_get_generated_keys,	/* CAS_FC_GET_GENERATED_KEYS */
  fn_lob_new,			/* CAS_FC_LOB_NEW */
  fn_lob_write,			/* CAS_FC_LOB_WRITE */
  fn_lob_read,			/* CAS_FC_LOB_READ */
  fn_end_session,		/* CAS_FC_END_SESSION */
  fn_get_row_count,		/* CAS_FC_GET_ROW_COUNT */
  fn_get_last_insert_id,	/* CAS_FC_GET_LAST_INSERT_ID */
  fn_prepare_and_execute,	/* CAS_FC_PREPARE_AND_EXECUTE */
  fn_cursor_close,		/* CAS_FC_CURSOR_CLOSE */
  fn_not_supported,		/* CAS_FC_GET_SHARD_INFO */
  fn_set_cas_change_mode,	/* CAS_FC_SET_CAS_CHANGE_MODE */
#if defined(SERVER_MODE)
  fn_csql_request		/* CAS_FC_CSQL_REQUEST */
#else
  fn_not_supported		/* CAS_FC_CSQL_REQUEST (thin csql is Linux/server-fold only) */
#endif
};

static const char *server_func_name[] = {
  "end_tran",
  "prepare",
  "execute",
  "get_db_parameter",
  "set_db_parameter",
  "close_req_handle",
  "cursor",
  "fetch",
  "schema_info",
  "oid_get",
  "oid_put",
  "glo_new(deprecated)",
  "glo_save(deprecated)",
  "glo_load(deprecated)",
  "get_db_version",
  "get_class_num_objs",
  "oid",
  "collection",
  "next_result",
  "execute_batch",
  "execute_array",
  "cursor_update",
  "get_attr_type_str",
  "get_query_info",
  "glo_cmd(deprecated)",
  "savepoint",
  "parameter_info",
  "xa_prepare",
  "xa_recover",
  "xa_end_tran",
  "con_close",
  "check_cas",
  "fn_make_out_rs",
  "fn_get_generated_keys",
  "fn_lob_new",
  "fn_lob_write",
  "fn_lob_read",
  "fn_end_session",
  "fn_get_row_count",
  "fn_get_last_insert_id",
  "fn_prepare_and_execute",
  "fn_cursor_close",
  "fn_get_shard_info",
  "fn_set_cas_change_mode",
  "fn_csql_request"
};




/* the shard reconnect cluster process_request drives (moved from cas.c;
 * extern so cas.c's main loop keeps using the same state) */
CAS_TLS char cas_db_name[MAX_HA_DBINFO_LENGTH];
CAS_TLS char cas_db_user[SRV_CON_DBUSER_SIZE];
CAS_TLS char cas_db_passwd[SRV_CON_DBPASSWD_SIZE];

int
net_read_process (SOCKET proxy_sock_fd, MSG_HEADER * client_msg_header, T_REQ_INFO * req_info)
{
  int ret_value = 0;
  int timeout = 0, remained_timeout = 0;
  bool is_proxy_conn_wait_timeout = false;

  if (as_info->con_status == CON_STATUS_IN_TRAN)
    {
      net_timeout_set (CAS_SHM_CFG (session_timeout));
    }
  else
    {
      net_timeout_set (DEFAULT_CHECK_INTERVAL);

      timeout = cas_get_graceful_down_timeout ();
      if (timeout < 0 && as_info->database_user[0] != '\0')
	{
	  timeout = as_info->proxy_conn_wait_timeout;
	  is_proxy_conn_wait_timeout = true;
	}

      remained_timeout = timeout;
    }

  do
    {
      if (as_info->cas_log_reset)
	{
	  cas_log_reset (broker_name);
	}

      if (as_info->con_status == CON_STATUS_CLOSE)
	{
	  break;
	}
      else if (as_info->con_status == CON_STATUS_OUT_TRAN)
	{
	  remained_timeout -= DEFAULT_CHECK_INTERVAL;
	}

      /*
       * net_read_header error case. case 1 : disconnect with proxy_sock_fd case 2 : CON_STATUS_IN_TRAN &&
       * session_timeout case 3 : reset_flag is TRUE */
      if (net_read_header (proxy_sock_fd, client_msg_header) < 0)
	{
	  /* if in-transaction state, return network error */
	  if (as_info->con_status == CON_STATUS_IN_TRAN || !is_net_timed_out ())
	    {
	      ret_value = -1;
	      break;
	    }
	  /* if out-of-transaction state, check whether restart is needed */
	  if (as_info->con_status == CON_STATUS_OUT_TRAN && is_net_timed_out ())
	    {
	      if (as_info->reset_flag == TRUE)
		{
		  ret_value = -1;
		  break;
		}

	      if (restart_is_needed ())
		{
		  cas_log_debug (ARG_FILE_LINE, "net_read_process: " "restart_is_needed()");
		  ret_value = -1;
		  break;
		}

	      /* this is not real timeout. try again. */
	      if (timeout < 0 || remained_timeout > 0)
		{
		  continue;
		}

	      if (is_proxy_conn_wait_timeout)
		{
		  as_info->database_user[0] = '\0';
		  as_info->database_passwd[0] = '\0';
		}

	      /* MYSQL_CONNECT_TIMEOUT case */
	      /* SHARD_CAS expire idle time and restart case */
	      ret_value = -1;
	      break;
	    }
	}
      else
	{
	  break;
	}
    }
  while (1);

  CON_STATUS_LOCK (as_info, CON_STATUS_LOCK_CAS);

  if (as_info->con_status == CON_STATUS_OUT_TRAN)
    {
      as_info->num_request++;
      gettimeofday (&tran_start_time, NULL);
      logddl_set_start_time (&tran_start_time);
    }

  if (as_info->con_status == CON_STATUS_CLOSE)
    {
      ret_value = -1;
    }
  else
    {
      if (as_info->con_status != CON_STATUS_IN_TRAN)
	{
	  if (ret_value >= 0)
	    {
	      as_info->con_status = CON_STATUS_IN_TRAN;
	      errors_in_transaction = 0;
	    }

	  cas_log_write_client_ip (as_info->cas_clt_ip);

	  /* This is a real client protocol version */
	  req_info->client_version = as_info->clt_version;
	  memcpy (req_info->driver_info, as_info->driver_info, SRV_CON_CLIENT_INFO_SIZE);
	  cas_log_write_and_end (0, false, "CLIENT VERSION %s", as_info->driver_version);
	}
    }

  CON_STATUS_UNLOCK (as_info, CON_STATUS_LOCK_CAS);

  return ret_value;
}

void
set_db_connection_info (void)
{
  if (as_info->fixed_shard_user)
    {
      strncpy (as_info->database_user, shm_appl->shard_conn_info[shm_shard_id].db_user, SRV_CON_DBUSER_SIZE - 1);
      as_info->database_user[SRV_CON_DBUSER_SIZE - 1] = '\0';

      strncpy (as_info->database_passwd, shm_appl->shard_conn_info[shm_shard_id].db_password,
	       SRV_CON_DBPASSWD_SIZE - 1);
      as_info->database_passwd[SRV_CON_DBUSER_SIZE - 1] = '\0';
    }

  strncpy (cas_db_user, as_info->database_user, SRV_CON_DBUSER_SIZE - 1);
  cas_db_user[SRV_CON_DBUSER_SIZE - 1] = '\0';

  strncpy (cas_db_passwd, as_info->database_passwd, SRV_CON_DBPASSWD_SIZE - 1);
  cas_db_passwd[SRV_CON_DBPASSWD_SIZE - 1] = '\0';

  cas_log_debug (ARG_FILE_LINE, "db_name %s db_user %s", cas_db_name, cas_db_user);
}

void
clear_db_connection_info (void)
{
  if (as_info->fixed_shard_user)
    {
      return;
    }

  cas_db_user[0] = '\0';
  cas_db_passwd[0] = '\0';
  as_info->database_user[0] = '\0';
  as_info->database_passwd[0] = '\0';
}

bool
need_database_reconnect (void)
{
  if (as_info->force_reconnect)
    {
      return true;
    }
  if (strcasecmp (cas_db_user, as_info->database_user))
    {
      return true;
    }

  if (strcmp (cas_db_passwd, as_info->database_passwd))
    {
      return true;
    }

  return false;
}

FN_RETURN
cas_process_request (SOCKET sock_fd, T_NET_BUF * net_buf, T_REQ_INFO * req_info, SOCKET srv_sock_fd)
{
  MSG_HEADER client_msg_header;
  MSG_HEADER cas_msg_header;
  char *read_msg;
  char func_code;
  int argc;
  void **argv = NULL;
  int err_code;

  int con_status_to_restore, old_con_status;
  T_SERVER_FUNC server_fn;
  FN_RETURN fn_ret = FN_KEEP_CONN;

  error_info_clear ();
  init_msg_header (&client_msg_header);
  init_msg_header (&cas_msg_header);

  old_con_status = as_info->con_status;

  if (cas_shard_flag == ON)
    {
      /* set req_info->client_version in net_read_process */
      err_code = net_read_process (sock_fd, &client_msg_header, req_info);
      if (err_code < 0)
	{
	  const char *cas_log_msg = NULL;
	  net_write_error (sock_fd, req_info->client_version, req_info->driver_info, cas_msg_header.info_ptr,
			   cas_info_size, CAS_ERROR_INDICATOR, CAS_ER_COMMUNICATION, NULL);
	  fn_ret = FN_CLOSE_CONN;

	  if (is_net_timed_out ())
	    {
	      if (as_info->reset_flag == TRUE)
		{
		  cas_log_msg = "CONNECTION RESET";
		}
	      else if (cas_get_graceful_down_timeout () > 0)
		{
		  cas_log_msg = "SESSION TIMEOUT AND EXPIRE IDLE TIMEOUT";
		  fn_ret = FN_GRACEFUL_DOWN;
		}
	      else
		{
		  if (as_info->con_status == CON_STATUS_IN_TRAN)
		    {
		      cas_log_msg = "SESSION TIMEOUT";
		    }
		  else
		    {
		      cas_log_msg = "CONNECTION WAIT TIMEOUT";
		    }
		}
	    }
	  else
	    {
	      cas_log_msg = "COMMUNICATION ERROR net_read_header()";
	    }
	  cas_log_write_and_end (0, true, cas_log_msg);
	  return fn_ret;
	}
      else
	{
	  as_info->uts_status = UTS_STATUS_BUSY;

	  if (need_database_reconnect ())
	    {
	      assert (as_info->fixed_shard_user == false);

	      set_db_connection_info ();

	      err_code = ux_database_connect (cas_db_name, cas_db_user, cas_db_passwd, NULL);
	      if (err_code < 0)
		{
		  clear_db_connection_info ();
		  net_write_error (sock_fd, req_info->client_version, req_info->driver_info, cas_msg_header.info_ptr,
				   cas_info_size, err_info.err_indicator, err_info.err_number, err_info.err_string);
		  return FN_CLOSE_CONN;
		}

	      ux_set_default_setting ();

	      /* the db/user this CAS serves changed without going through
	       * cas_db_connect(); refresh the query replace (db,user) rule cache
	       * so lookups match the new connection instead of the previous one. */
	      qr_load_dbuser_has_rules (as_info->database_name, cas_db_user);

	      cas_log_write_and_end (0, false, "connect db %s user %s", cas_db_name, cas_db_user);
	    }
	}
    }
  else
    {
      unset_hang_check_time ();
      if (as_info->cur_keep_con == KEEP_CON_AUTO)
	{
	  err_code = net_read_int_keep_con_auto (sock_fd, &client_msg_header, req_info, srv_sock_fd);
	}
      else
	{
	  err_code = net_read_header_keep_con_on (sock_fd, &client_msg_header);

	  if (as_info->cur_keep_con == KEEP_CON_ON && as_info->con_status == CON_STATUS_OUT_TRAN)
	    {
	      as_info->con_status = CON_STATUS_IN_TRAN;
	      as_info->transaction_start_time = time (0);
	      errors_in_transaction = 0;
	    }
	}
      if (err_code < 0)
	{
	  const char *cas_log_msg = NULL;

	  fn_ret = FN_CLOSE_CONN;

	  if (as_info->reset_flag)
	    {
	      cas_log_msg = "RESET";
	      cas_log_write_and_end (0, true, cas_log_msg);
	      fn_ret = FN_KEEP_SESS;
	      db_set_keep_session (true);
	    }
	  if (as_info->con_status == CON_STATUS_CLOSE_AND_CONNECT)
	    {
	      cas_log_msg = "CHANGE CLIENT";
	      fn_ret = FN_KEEP_SESS;
	      db_set_keep_session (true);
	    }

	  if (cas_log_msg == NULL)
	    {
	      if (is_net_timed_out ())
		{
		  if (as_info->reset_flag == TRUE)
		    {
		      cas_log_msg = "CONNECTION RESET";
		    }
		  else
		    {
		      cas_log_msg = "SESSION TIMEOUT";
		    }
		}
	      else
		{
		  cas_log_msg = "COMMUNICATION ERROR net_read_header()";
		}
	    }
	  cas_log_write_and_end (0, true, cas_log_msg);
	  return fn_ret;
	}
    }

#if !defined(WINDOWS)
  /* Before start to execute a new request, try to reset a previous interrupt request we might have. The interrupt
   * request arrived too late to interrupt the previous request and still remains. */
  db_set_interrupt (0);
#endif /* !WINDOWS */

  if (cas_shard_flag == ON)
    {
      set_db_parameter ();
    }

  if (CAS_SHM_CFG (session_timeout) < 0)
    net_timeout_set (NET_DEFAULT_TIMEOUT);
  else
    net_timeout_set (MIN (CAS_SHM_CFG (session_timeout), NET_DEFAULT_TIMEOUT));

  if (cas_shard_flag == ON && req_info->client_version == 0)
    {
      assert (0);
      req_info->client_version = CAS_PROTO_CURRENT_VER;
    }

  /* the driver-sent body length lands in cub_server's address space now:
   * refuse negative/oversized values before allocating or reading (same
   * bound as driver_session's connect-phase REQUEST_BODY_MAX) */
  if (*(client_msg_header.msg_body_size_ptr) < 0
      || *(client_msg_header.msg_body_size_ptr) > CAS_DISPATCH_REQUEST_BODY_MAX)
    {
      net_write_error (sock_fd, req_info->client_version, req_info->driver_info, cas_msg_header.info_ptr, cas_info_size,
		       CAS_ERROR_INDICATOR, CAS_ER_COMMUNICATION, NULL);
      cas_log_write_and_end (0, true, "COMMUNICATION ERROR invalid message body size %d",
			     *(client_msg_header.msg_body_size_ptr));
      return FN_CLOSE_CONN;
    }

  read_msg = (char *) MALLOC (*(client_msg_header.msg_body_size_ptr));
  if (read_msg == NULL)
    {
      net_write_error (sock_fd, req_info->client_version, req_info->driver_info, cas_msg_header.info_ptr, cas_info_size,
		       CAS_ERROR_INDICATOR, CAS_ER_NO_MORE_MEMORY, NULL);
      return FN_CLOSE_CONN;
    }
  if (net_read_stream (sock_fd, read_msg, *(client_msg_header.msg_body_size_ptr)) < 0)
    {
      FREE_MEM (read_msg);
      net_write_error (sock_fd, req_info->client_version, req_info->driver_info, cas_msg_header.info_ptr, cas_info_size,
		       CAS_ERROR_INDICATOR, CAS_ER_COMMUNICATION, NULL);
      cas_log_write_and_end (0, true, "COMMUNICATION ERROR net_read_stream()");
      return FN_CLOSE_CONN;
    }

  argc = net_decode_str (read_msg, *(client_msg_header.msg_body_size_ptr), &func_code, &argv);
  if (argc < 0)
    {
      FREE_MEM (read_msg);
      net_write_error (sock_fd, req_info->client_version, req_info->driver_info, cas_msg_header.info_ptr, cas_info_size,
		       CAS_ERROR_INDICATOR, CAS_ER_COMMUNICATION, NULL);
      return FN_CLOSE_CONN;
    }

  if (func_code <= 0 || func_code >= CAS_FC_MAX)
    {
      FREE_MEM (argv);
      FREE_MEM (read_msg);
      net_write_error (sock_fd, req_info->client_version, req_info->driver_info, cas_msg_header.info_ptr, cas_info_size,
		       CAS_ERROR_INDICATOR, CAS_ER_COMMUNICATION, NULL);
      return FN_CLOSE_CONN;
    }

  /* PROTOCOL_V2 is used only 9.0.0 */
  if (DOES_CLIENT_MATCH_THE_PROTOCOL (req_info->client_version, PROTOCOL_V2))
    {
      switch (func_code)
	{
	case CAS_FC_PREPARE_AND_EXECUTE:
	  func_code = CAS_FC_PREPARE_AND_EXECUTE_FOR_PROTO_V2;
	  break;
	case CAS_FC_CURSOR_CLOSE:
	  func_code = CAS_FC_CURSOR_CLOSE_FOR_PROTO_V2;
	  break;
	default:
	  break;
	}
    }

  con_status_to_restore = -1;

  if (FUNC_NEEDS_RESTORING_CON_STATUS (func_code))
    {
      if (is_first_request == true)
	{
	  /* If this request is the first request after connection established, con_status should be
	   * CON_STATUS_OUT_TRAN. */
	  con_status_to_restore = CON_STATUS_OUT_TRAN;
	}
      else if (con_status_before_check_cas != -1)
	{
	  con_status_to_restore = con_status_before_check_cas;
	}
      else
	{
	  con_status_to_restore = old_con_status;
	}

      con_status_before_check_cas = -1;
    }
  else if (func_code == CAS_FC_CHECK_CAS)
    {
      con_status_before_check_cas = old_con_status;
    }
  else
    {
      con_status_before_check_cas = -1;
    }

  strcpy (as_info->log_msg, server_func_name[func_code - 1]);

  server_fn = server_fn_table[func_code - 1];

  if (prev_cas_info[CAS_INFO_STATUS] != CAS_INFO_RESERVED_DEFAULT)
    {
      assert (prev_cas_info[CAS_INFO_STATUS] == client_msg_header.info_ptr[CAS_INFO_STATUS]);
#if defined (PROTOCOL_EXTENDS_DEBUG)	/* for debug cas <-> JDBC info */
      if (prev_cas_info[CAS_INFO_STATUS] != client_msg_header.info_ptr[CAS_INFO_STATUS])
	{
	  cas_log_debug (ARG_FILE_LINE,
			 "[%d][PREV : %d, RECV : %d], " "[preffunc : %d, recvfunc : %d], [REQ: %d, REQ: %d], "
			 "[JID : %d] \n", func_code - 1, prev_cas_info[CAS_INFO_STATUS],
			 client_msg_header.info_ptr[CAS_INFO_STATUS], prev_cas_info[CAS_INFO_RESERVED_1],
			 client_msg_header.info_ptr[CAS_INFO_RESERVED_1], prev_cas_info[CAS_INFO_RESERVED_2],
			 client_msg_header.info_ptr[CAS_INFO_RESERVED_2],
			 client_msg_header.info_ptr[CAS_INFO_RESERVED_3]);
	}
#endif /* end for debug */
    }

  req_info->need_auto_commit = TRAN_NOT_AUTOCOMMIT;

  cas_send_result_flag = TRUE;

  /* for 9.0 driver */
  if (DOES_CLIENT_MATCH_THE_PROTOCOL (req_info->client_version, PROTOCOL_V2))
    {
      ux_set_utype_for_enum (CCI_U_TYPE_STRING);
    }

  /* for driver less than 10.0 */
  if (!DOES_CLIENT_UNDERSTAND_THE_PROTOCOL (req_info->client_version, PROTOCOL_V7))
    {
      ux_set_utype_for_datetimetz (CCI_U_TYPE_DATETIME);
      ux_set_utype_for_timestamptz (CCI_U_TYPE_TIMESTAMP);
      ux_set_utype_for_datetimeltz (CCI_U_TYPE_DATETIME);
      ux_set_utype_for_timestampltz (CCI_U_TYPE_TIMESTAMP);
    }

  /* driver version < 10.2 */
  if (!DOES_CLIENT_UNDERSTAND_THE_PROTOCOL (req_info->client_version, PROTOCOL_V8))
    {
      ux_set_utype_for_json (CCI_U_TYPE_STRING);
    }

  as_info->fn_status = FN_STATUS_BUSY;

  net_buf->client_version = req_info->client_version;
  set_hang_check_time ();
  fn_ret = (*server_fn) (sock_fd, argc, argv, net_buf, req_info);
  set_hang_check_time ();

  /* set back original utype for enum, date-time, JSON */
  if (DOES_CLIENT_MATCH_THE_PROTOCOL (req_info->client_version, PROTOCOL_V2))
    {
      ux_set_utype_for_enum (CCI_U_TYPE_ENUM);
    }

  /* for driver less than 10.0 */
  if (!DOES_CLIENT_UNDERSTAND_THE_PROTOCOL (req_info->client_version, PROTOCOL_V7))
    {
      ux_set_utype_for_datetimetz (CCI_U_TYPE_DATETIMETZ);
      ux_set_utype_for_timestamptz (CCI_U_TYPE_TIMESTAMPTZ);
      ux_set_utype_for_datetimeltz (CCI_U_TYPE_DATETIMETZ);
      ux_set_utype_for_timestampltz (CCI_U_TYPE_TIMESTAMPTZ);
    }

  /* driver version < 10.2 */
  if (!DOES_CLIENT_UNDERSTAND_THE_PROTOCOL (req_info->client_version, PROTOCOL_V8))
    {
      ux_set_utype_for_json (CCI_U_TYPE_JSON);
    }

  cas_log_debug (ARG_FILE_LINE, "process_request: %s() err_code %d", server_func_name[func_code - 1],
		 err_info.err_number);

  if (con_status_to_restore != -1)
    {
      CON_STATUS_LOCK (as_info, CON_STATUS_LOCK_CAS);
      as_info->con_status = con_status_to_restore;
      CON_STATUS_UNLOCK (as_info, CON_STATUS_LOCK_CAS);
    }


  if (cas_shard_flag == ON && (func_code == CAS_FC_PREPARE || func_code == CAS_FC_CHECK_CAS)
      && (client_msg_header.info_ptr[CAS_INFO_ADDITIONAL_FLAG] & CAS_INFO_FLAG_MASK_FORCE_OUT_TRAN))
    {
      /* for shard dummy prepare */
      /* for connection check */
      req_info->need_auto_commit = TRAN_AUTOROLLBACK;
    }


  if (fn_ret == FN_KEEP_CONN && net_buf->err_code == 0 && as_info->con_status == CON_STATUS_IN_TRAN
      && req_info->need_auto_commit != TRAN_NOT_AUTOCOMMIT && err_info.err_number != CAS_ER_STMT_POOLING)
    {
      /* no communication error and auto commit is needed */
      err_code = ux_auto_commit (net_buf, req_info);
      if (err_code < 0)
	{
	  fn_ret = FN_CLOSE_CONN;
	}
      else
	{
	  if (as_info->cas_log_reset)
	    {
	      cas_log_reset (broker_name);
	    }
	  if (as_info->cas_slow_log_reset)
	    {
	      cas_slow_log_reset (broker_name);
	    }
	  if (!ux_is_database_connected ())
	    {
	      fn_ret = FN_CLOSE_CONN;
	    }
	  else if (restart_is_needed ())
	    {
	      fn_ret = FN_KEEP_SESS;
	      db_set_keep_session (true);
	    }
	  if (shm_appl->sql_log2 != as_info->cur_sql_log2)
	    {
	      sql_log2_end (false);
	      as_info->cur_sql_log2 = shm_appl->sql_log2;
	      sql_log2_init (broker_name, shm_as_index, as_info->cur_sql_log2, true);
	    }
	}
      as_info->num_transactions_processed %= MAX_DIAG_DATA_VALUE;
      as_info->num_transactions_processed++;

      /* should be OUT_TRAN in auto commit */
      CON_STATUS_LOCK (as_info, CON_STATUS_LOCK_CAS);
      if (as_info->con_status == CON_STATUS_IN_TRAN)
	{
	  as_info->con_status = CON_STATUS_OUT_TRAN;
	}
      CON_STATUS_UNLOCK (as_info, CON_STATUS_LOCK_CAS);
    }

  if ((func_code == CAS_FC_EXECUTE) || (func_code == CAS_FC_SCHEMA_INFO))
    {
      as_info->num_requests_received %= MAX_DIAG_DATA_VALUE;
      as_info->num_requests_received++;
    }
  else if (func_code == CAS_FC_END_TRAN)
    {
      as_info->num_transactions_processed %= MAX_DIAG_DATA_VALUE;
      as_info->num_transactions_processed++;
    }

  as_info->log_msg[0] = '\0';
  if (as_info->con_status == CON_STATUS_IN_TRAN)
    {
      cas_msg_header.info_ptr[CAS_INFO_STATUS] = CAS_INFO_STATUS_ACTIVE;
    }
  else
    {
      cas_msg_header.info_ptr[CAS_INFO_STATUS] = CAS_INFO_STATUS_INACTIVE;
    }

  if (func_code == CAS_FC_EXECUTE || func_code == CAS_FC_EXECUTE_ARRAY || func_code == CAS_FC_EXECUTE_BATCH
      || err_info.err_number < 0)
    {
      logddl_write_end ();
    }

  if (net_buf->err_code)
    {
      net_write_error (sock_fd, req_info->client_version, req_info->driver_info, cas_msg_header.info_ptr, cas_info_size,
		       CAS_ERROR_INDICATOR, net_buf->err_code, NULL);
      fn_ret = FN_CLOSE_CONN;
      goto exit_on_end;
    }

  if (cas_send_result_flag && net_buf->data != NULL)
    {

      cas_msg_header.info_ptr[CAS_INFO_ADDITIONAL_FLAG] &= ~CAS_INFO_FLAG_MASK_AUTOCOMMIT;
      cas_msg_header.info_ptr[CAS_INFO_ADDITIONAL_FLAG] |=
	(as_info->cci_default_autocommit & CAS_INFO_FLAG_MASK_AUTOCOMMIT);

      if (cas_shard_flag == ON)
	{
	  cas_msg_header.info_ptr[CAS_INFO_ADDITIONAL_FLAG] &= ~CAS_INFO_FLAG_MASK_FORCE_OUT_TRAN;
	}
#if defined (PROTOCOL_EXTENDS_DEBUG)	/* for debug cas<->jdbc info */
      cas_msg_header.info_ptr[CAS_INFO_RESERVED_1] = func_code - 1;
      cas_msg_header.info_ptr[CAS_INFO_RESERVED_2] = as_info->num_requests_received % 128;
      prev_cas_info[CAS_INFO_STATUS] = cas_msg_header.info_ptr[CAS_INFO_STATUS];
      prev_cas_info[CAS_INFO_RESERVED_1] = cas_msg_header.info_ptr[CAS_INFO_RESERVED_1];
      prev_cas_info[CAS_INFO_RESERVED_2] = cas_msg_header.info_ptr[CAS_INFO_RESERVED_2];
#endif /* end for debug */



      *(cas_msg_header.msg_body_size_ptr) = htonl (net_buf->data_size);
      memcpy (net_buf->data, cas_msg_header.msg_body_size_ptr, NET_BUF_HEADER_MSG_SIZE);

      if (cas_info_size > 0)
	{
	  memcpy (net_buf->data + NET_BUF_HEADER_MSG_SIZE, cas_msg_header.info_ptr, cas_info_size);
	}

      assert (NET_BUF_CURR_SIZE (net_buf) <= net_buf->alloc_size);
      if (net_write_stream (sock_fd, net_buf->data, NET_BUF_CURR_SIZE (net_buf)) < 0)
	{
	  cas_log_write_and_end (0, true, "COMMUNICATION ERROR net_write_stream()");
	}
    }

  if (cas_shard_flag == OFF && cas_send_result_flag && net_buf->post_send_file != NULL)
    {
      err_code = net_write_from_file (sock_fd, net_buf->post_file_size, net_buf->post_send_file);
      unlink (net_buf->post_send_file);
      if (err_code < 0)
	{
	  fn_ret = FN_CLOSE_CONN;
	  goto exit_on_end;
	}
    }


  if (as_info->reset_flag
      &&
      ((as_info->con_status != CON_STATUS_IN_TRAN && as_info->num_holdable_results < 1
	&& as_info->cas_change_mode == CAS_CHANGE_MODE_AUTO) || (cas_get_db_connect_status () == -1)))
    {
      cas_log_debug (ARG_FILE_LINE, "process_request: reset_flag && !CON_STATUS_IN_TRAN");
      fn_ret = FN_KEEP_SESS;
      db_set_keep_session (true);
      goto exit_on_end;
    }


exit_on_end:

  if (cas_shard_flag == ON && as_info->con_status != CON_STATUS_IN_TRAN && as_info->uts_status == UTS_STATUS_BUSY)
    {
      as_info->uts_status = UTS_STATUS_IDLE;
    }


  net_buf_clear (net_buf);

  FREE_MEM (read_msg);
  FREE_MEM (argv);

  return fn_ret;
}


static void
set_db_parameter (void)
{
  int cur_isolation_level;
  int cur_lock_timeout;
  int isolation_level = as_info->isolation_level;
  int lock_timeout = as_info->lock_timeout;

  if (isolation_level == CAS_USE_DEFAULT_DB_PARAM)
    {
      isolation_level = cas_default_isolation_level;
    }

  if (lock_timeout == CAS_USE_DEFAULT_DB_PARAM)
    {
      lock_timeout = cas_default_lock_timeout;
    }

  ux_get_tran_setting (&cur_lock_timeout, &cur_isolation_level);
  if (cur_lock_timeout != lock_timeout)
    {
      ux_set_lock_timeout (lock_timeout);

      cas_log_write_and_end (0, false, "set_db_parameter lock_timeout %d", lock_timeout);
    }

  if (cur_isolation_level != isolation_level)
    {
      ux_set_isolation_level (isolation_level, NULL);

      cas_log_write_and_end (0, false, "set_db_parameter isolation_level %d", isolation_level);
    }
}
