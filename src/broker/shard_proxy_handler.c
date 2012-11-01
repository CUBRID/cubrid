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
 * shard_proxy_handler.c
 */

#ident "$Id$"


#include <assert.h>

#if defined(WINDOWS)
#include "porting.h"
#endif /* WINDOWS */

#include "broker_config.h"
#include "broker_shm.h"
#include "shard_proxy_common.h"
#include "cas_protocol.h"
#include "shard_shm.h"

#include "shard_proxy_handler.h"
#include "shard_proxy_io.h"
#include "shard_proxy_queue.h"
#include "shard_parser.h"
#include "shard_proxy_function.h"

extern char *shm_as_cp;
extern T_SHM_APPL_SERVER *shm_as_p;
extern T_SHM_PROXY *shm_proxy_p;
extern T_PROXY_INFO *proxy_info_p;

SP_PARSER_CTX *parser = NULL;

T_PROXY_HANDLER proxy_Handler;
T_PROXY_CONTEXT_GLOBAL proxy_Context;

static void proxy_handler_process_cas_response (T_PROXY_EVENT * event_p);
static int proxy_handler_process_cas_error (T_PROXY_EVENT * event_p);
static void proxy_handler_process_cas_conn_error (T_PROXY_EVENT * event_p);
static void proxy_handler_process_cas_event (T_PROXY_EVENT * event_p);
static void proxy_handler_process_client_request (T_PROXY_EVENT * event_p);
static void proxy_handler_process_client_conn_error (T_PROXY_EVENT * event_p);
static void proxy_handler_process_client_wakeup_by_shard (T_PROXY_EVENT *
							  event_p);
static void proxy_handler_process_client_wakeup_by_statement (T_PROXY_EVENT *
							      event_p);
static void proxy_handler_process_client_event (T_PROXY_EVENT * event_p);

static void proxy_context_clear (T_PROXY_CONTEXT * ctx_p);
static void proxy_context_free_client (T_PROXY_CONTEXT * ctx_p);
static void proxy_context_free_shard (T_PROXY_CONTEXT * ctx_p);

static int proxy_context_send_error (T_PROXY_CONTEXT * ctx_p);

static int proxy_context_initialize (void);
static void proxy_context_destroy (void);

static T_PROXY_CLIENT_FUNC proxy_client_fn_table[] = {
  fn_proxy_client_end_tran,
  fn_proxy_client_prepare,
  fn_proxy_client_execute,
  fn_proxy_client_not_supported,	/* fn_get_db_parameter */
  fn_proxy_client_not_supported,	/* fn_set_db_parameter */
  fn_proxy_client_close_req_handle,	/* fn_close_req_handle */
  fn_proxy_client_not_supported,	/* fn_cursor */
  fn_proxy_client_fetch,
  fn_proxy_client_not_supported,	/* fn_schema_info */
  fn_proxy_client_not_supported,	/* fn_oid_get */
  fn_proxy_client_not_supported,	/* fn_oid_put */
  fn_proxy_client_not_supported,	/* fn_deprecated */
  fn_proxy_client_not_supported,	/* fn_deprecated */
  fn_proxy_client_not_supported,	/* fn_deprecated */
  fn_proxy_client_get_db_version,
  fn_proxy_client_not_supported,	/* fn_get_class_num_objs */
  fn_proxy_client_not_supported,	/* fn_oid */
  fn_proxy_client_not_supported,	/* fn_collection */
  fn_proxy_client_not_supported,	/* fn_next_result */
  fn_proxy_client_not_supported,	/* fn_execute_batch */
  fn_proxy_client_not_supported,	/* fn_execute_array */
  fn_proxy_client_not_supported,	/* fn_cursor_update */
  fn_proxy_client_not_supported,	/* fn_get_attr_type_str */
  fn_proxy_client_not_supported,	/* fn_get_query_info */
  fn_proxy_client_not_supported,	/* fn_deprecated */
  fn_proxy_client_not_supported,	/* fn_savepoint */
  fn_proxy_client_not_supported,	/* fn_parameter_info */
  fn_proxy_client_not_supported,	/* fn_xa_prepare */
  fn_proxy_client_not_supported,	/* fn_xa_recover */
  fn_proxy_client_not_supported,	/* fn_xa_end_tran */
  fn_proxy_client_con_close,	/* fn_con_close */
  fn_proxy_client_check_cas,
  fn_proxy_client_not_supported,	/* fn_make_out_rs */
  fn_proxy_client_not_supported,	/* fn_get_generated_keys */
  fn_proxy_client_not_supported,	/* fn_lob_new */
  fn_proxy_client_not_supported,	/* fn_lob_write */
  fn_proxy_client_not_supported,	/* fn_lob_read */
  fn_proxy_client_not_supported,	/* fn_end_session */
  fn_proxy_client_not_supported,	/* fn_get_row_count */
  fn_proxy_client_not_supported,	/* fn_get_last_insert_id */
  fn_proxy_client_not_supported	/* fn_prepare_and_execute */
};


static T_PROXY_CAS_FUNC proxy_cas_fn_table[] = {
  fn_proxy_cas_end_tran,	/* fn_end_tran */
  fn_proxy_cas_prepare,
  fn_proxy_cas_execute,
  fn_proxy_cas_relay_only,	/* fn_get_db_parameter */
  fn_proxy_cas_relay_only,	/* fn_set_db_parameter */
  fn_proxy_cas_relay_only,	/* fn_close_req_handle */
  fn_proxy_cas_relay_only,	/* fn_cursor */
  fn_proxy_cas_fetch,
  fn_proxy_cas_relay_only,	/* fn_schema_info */
  fn_proxy_cas_relay_only,	/* fn_oid_get */
  fn_proxy_cas_relay_only,	/* fn_oid_put */
  fn_proxy_cas_relay_only,	/* fn_deprecated */
  fn_proxy_cas_relay_only,	/* fn_deprecated */
  fn_proxy_cas_relay_only,	/* fn_deprecated */
  fn_proxy_cas_relay_only,	/* fn_get_db_version */
  fn_proxy_cas_relay_only,	/* fn_get_class_num_objs */
  fn_proxy_cas_relay_only,	/* fn_oid */
  fn_proxy_cas_relay_only,	/* fn_collection */
  fn_proxy_cas_relay_only,	/* fn_next_result */
  fn_proxy_cas_relay_only,	/* fn_execute_batch */
  fn_proxy_cas_relay_only,	/* fn_execute_array */
  fn_proxy_cas_relay_only,	/* fn_cursor_update */
  fn_proxy_cas_relay_only,	/* fn_get_attr_type_str */
  fn_proxy_cas_relay_only,	/* fn_get_query_info */
  fn_proxy_cas_relay_only,	/* fn_deprecated */
  fn_proxy_cas_relay_only,	/* fn_savepoint */
  fn_proxy_cas_relay_only,	/* fn_parameter_info */
  fn_proxy_cas_relay_only,	/* fn_xa_prepare */
  fn_proxy_cas_relay_only,	/* fn_xa_recover */
  fn_proxy_cas_relay_only,	/* fn_xa_end_tran */
  fn_proxy_cas_relay_only,	/* fn_con_close */
  fn_proxy_cas_relay_only,	/* fn_check_cas */
  fn_proxy_cas_relay_only,	/* fn_make_out_rs */
  fn_proxy_cas_relay_only,	/* fn_get_generated_keys */
  fn_proxy_cas_relay_only,	/* fn_lob_new */
  fn_proxy_cas_relay_only,	/* fn_lob_write */
  fn_proxy_cas_relay_only,	/* fn_lob_read */
  fn_proxy_cas_relay_only,	/* fn_end_session */
  fn_proxy_cas_relay_only,	/* fn_get_row_count */
  fn_proxy_cas_relay_only,	/* fn_get_last_insert_id */
  fn_proxy_cas_relay_only	/* fn_prepare_and_execute */
};


T_WAIT_CONTEXT *
proxy_waiter_new (int ctx_cid, unsigned int ctx_uid)
{
  T_WAIT_CONTEXT *n;

  n = (T_WAIT_CONTEXT *) malloc (sizeof (T_WAIT_CONTEXT));
  if (n)
    {
      n->ctx_cid = ctx_cid;
      n->ctx_uid = ctx_uid;
    }
  return n;
}

void
proxy_waiter_free (T_WAIT_CONTEXT * waiter)
{
  assert (waiter);

  FREE_MEM (waiter);
}

bool
proxy_handler_is_cas_in_tran (int shard_id, int cas_id)
{
  T_APPL_SERVER_INFO *as_info = NULL;

  assert (shard_id >= 0);
  assert (cas_id >= 0);

  as_info = shard_shm_get_as_info (proxy_info_p, shard_id, cas_id);
  if (as_info)
    {
      return (as_info->con_status == CON_STATUS_IN_TRAN
	      || as_info->num_holdable_results > 0) ? true : false;
    }

  return false;
}

void
proxy_context_set_error (T_PROXY_CONTEXT * ctx_p, int error_ind,
			 int error_code)
{
  assert (ctx_p);

  assert (ctx_p->error_ind == CAS_NO_ERROR);
  assert (ctx_p->error_code == CAS_NO_ERROR);
  assert (ctx_p->error_msg[0] == '\0');

  ctx_p->error_ind = error_ind;
  ctx_p->error_code = error_code;
  ctx_p->error_msg[0] = '\0';

  return;
}

#if defined (ENABLE_UNUSED_FUNCTION)
void
proxy_context_set_error_with_msg (T_PROXY_CONTEXT * ctx_p, int error_ind,
				  int error_code, const char *fmt, va_list ap)
{
  assert (fmt);

  proxy_context_set_error (ctx_p, error_ind, error_code);
  vsnprintf (ctx_p->error_msg, sizeof (ctx_p->error_msg), fmt, ap);

  return;
}
#endif /* ENABLE_UNUSED_FUNCTION */

void
proxy_context_clear_error (T_PROXY_CONTEXT * ctx_p)
{
  assert (ctx_p);

  ctx_p->error_ind = CAS_NO_ERROR;
  ctx_p->error_code = CAS_NO_ERROR;
  ctx_p->error_msg[0] = '\0';

  return;
}

static int
proxy_context_send_error (T_PROXY_CONTEXT * ctx_p)
{
  int error;
  char *error_msg = NULL;
  T_PROXY_EVENT *event_p;
  T_BROKER_VERSION client_version;

  ENTER_FUNC ();

  assert (ctx_p->error_ind != CAS_NO_ERROR);
  assert (ctx_p->error_code != CAS_NO_ERROR);

  client_version = proxy_client_io_version_find_by_ctx (ctx_p);

  event_p = proxy_event_new_with_error (client_version, PROXY_EVENT_IO_WRITE,
					PROXY_EVENT_FROM_CLIENT,
					proxy_io_make_error_msg,
					ctx_p->error_ind, ctx_p->error_code,
					ctx_p->error_msg,
					ctx_p->is_client_in_tran);
  if (event_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to make error message. "
		 "context(%s).", proxy_str_context (ctx_p));
      goto error_return;
    }

  PROXY_DEBUG_LOG ("send error(msg) to the client. "
		   "(error_ind:%d, error_code:%d, errro_msg:%s)",
		   ctx_p->error_ind, ctx_p->error_code,
		   (ctx_p->error_msg
		    && ctx_p->error_msg[0]) ? ctx_p->error_msg : "-");

  error = proxy_send_response_to_client (ctx_p, event_p);
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to send response "
		 "to the client. (error:%d). context(%s). event(%s). ", error,
		 proxy_str_context (ctx_p), proxy_str_event (event_p));
      goto error_return;
    }
  event_p = NULL;

  EXIT_FUNC ();
  return 0;

error_return:

  if (event_p)
    {
      proxy_event_free (event_p);
      event_p = NULL;
    }

  ctx_p->free_context = true;
  return -1;
}

static void
proxy_handler_process_cas_response (T_PROXY_EVENT * event_p)
{
  int error = NO_ERROR;
  int error_ind;
  char *response_p;
  int argc;
  char **argv = NULL;
  T_PROXY_CONTEXT *ctx_p = NULL;
  T_CLIENT_INFO *client_info_p;
  char func_code;

  T_PROXY_CAS_FUNC proxy_cas_fn;

  ENTER_FUNC ();

  assert (event_p->from_cas == PROXY_EVENT_FROM_CAS);

  response_p = event_p->buffer.data;
  assert (response_p);

  ctx_p = proxy_context_find (event_p->cid, event_p->uid);
  if (ctx_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Unable to find context. event(%s).",
		 proxy_str_event (event_p));

      proxy_event_free (event_p);
      event_p = NULL;

      EXIT_FUNC ();
      return;
    }

  client_info_p = shard_shm_get_client_info (proxy_info_p, ctx_p->client_id);

  error_ind = proxy_check_cas_error (response_p);
  if (error_ind < 0)
    {
      error = proxy_handler_process_cas_error (event_p);
      if (error)
	{
	  proxy_event_free (event_p);
	  event_p = NULL;
	  goto end;
	}
    }

  func_code = ctx_p->func_code;
  if (func_code <= 0 || func_code >= CAS_FC_MAX)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Unsupported function code. "
		 "(func_code:%d). context(%s).",
		 func_code, proxy_str_context (ctx_p));
      /*
       * 1*) drop unexpected messages from cas ?
       * 2) free context ?
       */
      proxy_event_free (event_p);
      goto end;
    }

  proxy_cas_fn = proxy_cas_fn_table[func_code - 1];
  error = proxy_cas_fn (ctx_p, event_p);
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Error returned. (CAS function, func_code:%d, error:%d).",
		 func_code, error);

      event_p = NULL;
      goto end;
    }
  event_p = NULL;

  ctx_p->is_cas_in_tran = proxy_handler_is_cas_in_tran (ctx_p->shard_id,
							ctx_p->cas_id);
  ctx_p->is_in_tran = ctx_p->is_cas_in_tran;
  if (ctx_p->is_in_tran == false)
    {
      /* release shard/cas */
      proxy_cas_release_by_ctx (ctx_p->shard_id, ctx_p->cas_id, ctx_p->cid,
				ctx_p->uid);

      proxy_context_set_out_tran (ctx_p);
      ctx_p->prepared_stmt = NULL;
      ctx_p->stmt_h_id = SHARD_STMT_INVALID_HANDLE_ID;

      if (ctx_p->dont_free_statement == false)
	{
	  proxy_context_free_stmt (ctx_p);
	}
    }

end:
  ctx_p->func_code = PROXY_INVALID_FUNC_CODE;

  if (ctx_p->free_context)
    {
      proxy_context_free (ctx_p);
    }

  shard_shm_set_client_info_response (client_info_p);

  EXIT_FUNC ();
  return;
}

static int
proxy_handler_process_cas_error (T_PROXY_EVENT * event_p)
{
  T_PROXY_CONTEXT *ctx_p;

  ENTER_FUNC ();

  assert (event_p);

  ctx_p = proxy_context_find (event_p->cid, event_p->uid);
  if (ctx_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Unable to find context. event(%s).",
		 proxy_str_event (event_p));

      goto end;
    }

  if (ctx_p->is_in_tran)
    {
      if (ctx_p->func_code)
	{
	  /*
	   * we should relay error event to the client
	   * so, must not call proxy_event_free().
	   */
	  return 0;
	}
      else
	{
	  /* unexpected error, free context */
	  /* it may be session timeout error, ... */
	  ctx_p->free_context = true;
	}
    }
  else
    {
      /* discard unexpected error */
    }

end:
  proxy_event_free (event_p);

  EXIT_FUNC ();
  return -1;
}

static void
proxy_handler_process_cas_conn_error (T_PROXY_EVENT * event_p)
{
  int error;
  T_PROXY_CONTEXT *ctx_p;

  ENTER_FUNC ();

  assert (event_p->from_cas == PROXY_EVENT_FROM_CAS);

  ctx_p = proxy_context_find (event_p->cid, event_p->uid);
  if (ctx_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Unable to find context. event(%s).",
		 proxy_str_event (event_p));
      goto end;
    }

  PROXY_DEBUG_LOG ("Process CAS connection error. context(%s).",
		   proxy_str_context (ctx_p));

  if (ctx_p->is_in_tran)
    {
      if (ctx_p->waiting_event
	  && ctx_p->is_cas_in_tran == false
	  && (ctx_p->func_code == CAS_FC_PREPARE
	      || ctx_p->func_code == CAS_FC_EXECUTE))
	{
	  PROXY_DEBUG_LOG ("Context is in_tran status "
			   "and waiting prepare/execute response. "
			   "retransmit reqeust to the CAS. context(%s).",
			   proxy_str_context (ctx_p));
	  /* release this shard/cas */
	  proxy_context_free_shard (ctx_p);

	  /* set transaction status 'out_tran' */
	  proxy_context_set_out_tran (ctx_p);

	  /* retry previous request */
	  error = shard_queue_enqueue (&proxy_Handler.cli_ret_q,
				       (void *) ctx_p->waiting_event);
	  if (error)
	    {
	      assert (false);

	      proxy_context_free (ctx_p);
	      goto end;
	    }

	  /* reset waiting event */
	  ctx_p->waiting_event = NULL;
	}
      else
	{
	  PROXY_DEBUG_LOG ("context is in_tran status. "
			   "and function code %d, "
			   "cas_is_in_ran %s, "
			   "waiting_event %p.",
			   ctx_p->func_code,
			   (ctx_p->is_cas_in_tran) ? "in_tran" : "out_tran",
			   ctx_p->waiting_event);

	  /* TODO : send error to the client */
	  proxy_context_free (ctx_p);
	}
    }
  else
    {
      PROXY_DEBUG_LOG ("context is out_tran status.");
      proxy_context_free (ctx_p);
    }

end:
  proxy_event_free (event_p);
  event_p = NULL;

  EXIT_FUNC ();
  return;
}

static void
proxy_handler_process_cas_event (T_PROXY_EVENT * event_p)
{
  ENTER_FUNC ();

  assert (event_p);

  switch (event_p->type)
    {
    case PROXY_EVENT_CAS_RESPONSE:
      proxy_handler_process_cas_response (event_p);
      break;

    case PROXY_EVENT_CAS_CONN_ERROR:
      proxy_handler_process_cas_conn_error (event_p);
      break;

    default:
      assert (false);
      break;
    }

  EXIT_FUNC ();
  return;
}

static void
proxy_handler_process_client_request (T_PROXY_EVENT * event_p)
{
  int error = NO_ERROR;
  char *request_p;
  char *payload;
  int length;
  int argc;
  char **argv = NULL;
  char func_code;
  T_PROXY_CONTEXT *ctx_p;
  T_CLIENT_INFO *client_info_p;

  T_PROXY_CLIENT_FUNC proxy_client_fn;

  ENTER_FUNC ();

  assert (event_p);
  assert (event_p->from_cas == PROXY_EVENT_FROM_CLIENT);

  ctx_p = proxy_context_find (event_p->cid, event_p->uid);
  if (ctx_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Unable to find context. event(%s).",
		 proxy_str_event (event_p));

      proxy_event_free (event_p);

      EXIT_FUNC ();
      return;
    }
  client_info_p = shard_shm_get_client_info (proxy_info_p, ctx_p->client_id);

  request_p = event_p->buffer.data;
  assert (request_p);

  payload = request_p + MSG_HEADER_SIZE;
  length = get_data_length (request_p);

  argc = net_decode_str (payload, length, &func_code, (void ***) (&argv));
  if (argc < 0)
    {
      error = ER_FAILED;

      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Invalid argument. (argc:%d). event(%s).", argc,
		 proxy_str_event (event_p));

      proxy_event_free (event_p);
      goto end;
    }

  if (func_code <= 0 || func_code >= CAS_FC_MAX)
    {
      error = ER_FAILED;

      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Unsupported function code. "
		 "(func_code:%d). context(%s).",
		 func_code, proxy_str_context (ctx_p));

      proxy_event_free (event_p);
      goto end;
    }

  /* SHARD TODO : fix cci/jdbc */
  proxy_unset_force_out_tran (request_p);

  shard_shm_set_client_info_request (client_info_p, func_code);

  proxy_client_fn = proxy_client_fn_table[func_code - 1];
  error = proxy_client_fn (ctx_p, event_p, argc, argv);
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Error returned. (client function, func_code:%d, error:%d).",
		 func_code, error);

      event_p = NULL;
      goto end;
    }
  event_p = NULL;

end:

  if (ctx_p->error_ind)
    {
      proxy_context_send_error (ctx_p);
      proxy_context_clear_error (ctx_p);
    }

  if (ctx_p->free_context)
    {
      proxy_context_free (ctx_p);
    }

  if (error)
    {
      shard_shm_init_client_info_request (client_info_p);
    }

  if (argv)
    {
      FREE_MEM (argv);
    }

  EXIT_FUNC ();

  return;
}

static void
proxy_handler_process_client_conn_error (T_PROXY_EVENT * event_p)
{
  int error;
  T_PROXY_CONTEXT *ctx_p;

  ENTER_FUNC ();

  assert (event_p->from_cas == PROXY_EVENT_FROM_CLIENT);

  ctx_p = proxy_context_find (event_p->cid, event_p->uid);
  if (ctx_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Unable to find context. event(%s).",
		 proxy_str_event (event_p));

      proxy_event_free (event_p);
      event_p = NULL;
      EXIT_FUNC ();
      return;
    }

  error = fn_proxy_client_conn_error (ctx_p);
  if (error)
    {
      ;
    }

end:
  proxy_event_free (event_p);
  event_p = NULL;

  if (ctx_p->free_context)
    {
      proxy_context_free (ctx_p);
    }

  EXIT_FUNC ();
  return;
}

static void
proxy_handler_process_client_wakeup_by_shard (T_PROXY_EVENT * event_p)
{
  int error;
  T_PROXY_EVENT *waiting_event;
  T_PROXY_CONTEXT *ctx_p;

  ENTER_FUNC ();

  assert (event_p->from_cas == PROXY_EVENT_FROM_CLIENT);

  ctx_p = proxy_context_find (event_p->cid, event_p->uid);
  if (ctx_p == NULL)
    {
      proxy_event_free (event_p);
      event_p = NULL;

      EXIT_FUNC ();
      return;
    }

  /* set in_tran, shard/cas */
  proxy_context_set_in_tran (ctx_p, event_p->shard_id, event_p->cas_id);

  /* retry */
  waiting_event = ctx_p->waiting_event;
  ctx_p->waiting_event = NULL;

  proxy_handler_process_client_request (waiting_event);	/* DO NOT MOVE */

  proxy_event_free (event_p);
  event_p = NULL;

  EXIT_FUNC ();
  return;
}

static void
proxy_handler_process_client_wakeup_by_statement (T_PROXY_EVENT * event_p)
{
  int error;
  T_PROXY_EVENT *waiting_event;
  T_PROXY_CONTEXT *ctx_p;

  ENTER_FUNC ();

  assert (event_p->from_cas == PROXY_EVENT_FROM_CLIENT);

  ctx_p = proxy_context_find (event_p->cid, event_p->uid);
  if (ctx_p == NULL)
    {
      proxy_event_free (event_p);
      event_p = NULL;

      EXIT_FUNC ();
      return;
    }

  /* retry */
  waiting_event = ctx_p->waiting_event;
  ctx_p->waiting_event = NULL;

  proxy_handler_process_client_request (waiting_event);	/* DO NOT MOVE */

  proxy_event_free (event_p);
  event_p = NULL;

  EXIT_FUNC ();
  return;
}

static void
proxy_handler_process_client_event (T_PROXY_EVENT * event_p)
{
  ENTER_FUNC ();

  assert (event_p);

  switch (event_p->type)
    {
    case PROXY_EVENT_CLIENT_REQUEST:
      proxy_handler_process_client_request (event_p);
      break;

    case PROXY_EVENT_CLIENT_CONN_ERROR:
      proxy_handler_process_client_conn_error (event_p);
      break;

    case PROXY_EVENT_CLIENT_WAKEUP_BY_SHARD:
      proxy_handler_process_client_wakeup_by_shard (event_p);
      break;

    case PROXY_EVENT_CLIENT_WAKEUP_BY_STATEMENT:
      proxy_handler_process_client_wakeup_by_statement (event_p);
      break;

    default:
      assert (false);
      break;
    }

  EXIT_FUNC ();
  return;
}

static int
proxy_context_initialize (void)
{
  int error;
  int i, size;

  T_PROXY_CONTEXT *ctx_p;

  static bool is_init = false;

  if (is_init == true)
    {
      return 0;
    }

  proxy_Context.size = shm_proxy_p->max_client;

  error = shard_cqueue_initialize (&proxy_Context.freeq, proxy_Context.size);
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Failed to initialize context free queue. " "(error:%d).",
		 error);

      goto error_return;
    }

  size = proxy_Context.size * sizeof (T_PROXY_CONTEXT);
  proxy_Context.ent = (T_PROXY_CONTEXT *) malloc (size);
  if (proxy_Context.ent == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Not enough virtual memory. "
		 "Failed to alloc context entries. "
		 "(errno:%d, size:%d).", errno, size);
      goto error_return;
    }
  memset (proxy_Context.ent, 0, size);

  for (i = 0; i < proxy_Context.size; i++)
    {
      ctx_p = &(proxy_Context.ent[i]);

      proxy_context_clear (ctx_p);
      ctx_p->cid = i;

      error = shard_cqueue_enqueue (&proxy_Context.freeq, (void *) ctx_p);
      if (error)
	{
	  PROXY_LOG (PROXY_LOG_MODE_ERROR,
		     "Failed to initialize context free queue entries."
		     "(error:%d).", error);
	  goto error_return;
	}
    }

  is_init = true;
  return 0;

error_return:
  return -1;
}

static void
proxy_context_destroy (void)
{
  /* SHARD TODO : not implemented yet */

  return;
}

#if defined(PROXY_VERBOSE_DEBUG)
void
proxy_context_dump_stmt (FILE * fp, T_PROXY_CONTEXT * ctx_p)
{
  T_CONTEXT_STMT *stmt_list_p;

  fprintf (fp, "* STMT_LIST: ");
  for (stmt_list_p = ctx_p->stmt_list; stmt_list_p;
       stmt_list_p = stmt_list_p->next)
    {
      fprintf (fp, "%-10d ", stmt_list_p->stmt_h_id);
    }
  fprintf (fp, "\n");

  return;
}

void
proxy_context_dump_title (FILE * fp)
{
  fprintf (fp, "%-5s %-10s %-10s %-5s %-5s "
	   "%-5s %-5s %-15s "
	   "%-15s %-15s %-15s "
	   "%-10s %-10s %-10s "
	   "%-10s %-10s \n",
	   "CID", "UID", "CLIENT", "SHARD", "CAS",
	   "BUSY", "IN_TRAN", "PREPARE_FOR_EXEC",
	   "FREE_ON_END_TRAN", "FREE_CONTEXT", "CLIENT_END_TRAN",
	   "FUNC_CODE", "STMT_H_ID", "STMT_HINT_TYPE",
	   "ERROR_IND", "ERROR_CODE");

  return;
}


void
proxy_context_dump (FILE * fp, T_PROXY_CONTEXT * ctx_p)
{
  fprintf (fp, "%-5d %-10u %-10d %-5d %-5d "
	   "%-5s %-5s %-15s "
	   "%-15s %-15s %-15s %-15s "
	   "%-10d %-10d %-10d "
	   "%-10d %-10d \n",
	   ctx_p->cid, ctx_p->uid, ctx_p->client_id, ctx_p->shard_id,
	   ctx_p->cas_id, (ctx_p->is_busy) ? "YES" : "NO",
	   (ctx_p->is_in_tran) ? "YES" : "NO",
	   (ctx_p->is_prepare_for_execute) ? "YES" : "NO",
	   (ctx_p->free_on_end_tran) ? "YES" : "NO",
	   (ctx_p->free_on_client_io_write) ? "YES" : "NO",
	   (ctx_p->free_context) ? "YES" : "NO",
	   (ctx_p->is_client_in_tran) ? "YES" : "NO", ctx_p->func_code,
	   ctx_p->stmt_h_id, ctx_p->stmt_hint_type, ctx_p->error_ind,
	   ctx_p->error_code);

  proxy_context_dump_stmt (fp, ctx_p);

  return;
}

void
proxy_context_dump_all (FILE * fp)
{
  T_PROXY_CONTEXT *ctx_p;
  int i;

  fprintf (fp, "\n");
  fprintf (fp, "* %-20s : %d\n", "SIZE", proxy_Context.size);
  fprintf (fp, "\n");

  if (proxy_Context.ent == NULL)
    {
      return;
    }

  proxy_context_dump_title (fp);
  for (i = 0; i < proxy_Context.size; i++)
    {
      ctx_p = &(proxy_Context.ent[i]);
      proxy_context_dump (fp, ctx_p);
    }
  return;
}

void
proxy_context_print (bool print_all)
{
  T_PROXY_CONTEXT *ctx_p;
  int i;

  PROXY_LOG (PROXY_LOG_MODE_SCHEDULE_DETAIL, "* CONTEXT *");
  PROXY_LOG (PROXY_LOG_MODE_SCHEDULE_DETAIL, "%-20s = %d", "size",
	     proxy_Context.size);

  PROXY_LOG (PROXY_LOG_MODE_SCHEDULE_DETAIL,
	     "%-7s    %-5s %-10s %-5s %-5s %-8s "
	     "%-10s %-10s %-10s %-10s %-10s",
	     "idx", "cid", "uid", "busy", "wait", "in_tran",
	     "client_id", "shard_id", "cas_id", "func_code", "stmt_id");
  if (proxy_Context.ent)
    {
      for (i = 0; i < proxy_Context.size; i++)
	{
	  ctx_p = &(proxy_Context.ent[i]);

	  if (!print_all && !ctx_p->is_busy)
	    {
	      continue;
	    }
	  PROXY_LOG (PROXY_LOG_MODE_SCHEDULE_DETAIL,
		     "[%-5d]    %-5d %-10u %-5s %-8s "
		     "%-10d %-10d %-10d %-10d %-10d",
		     i, ctx_p->cid, ctx_p->uid,
		     (ctx_p->is_busy) ? "YES" : "NO",
		     (ctx_p->is_in_tran) ? "YES" : "NO", ctx_p->client_id,
		     ctx_p->shard_id, ctx_p->cas_id, ctx_p->func_code,
		     ctx_p->stmt_h_id);
	}
    }

  return;
}
#endif /* PROXY_VERBOSE_DEBUG */

char *
proxy_str_context (T_PROXY_CONTEXT * ctx_p)
{
  static char buffer[LINE_MAX];

  if (ctx_p == NULL)
    {
      return (char *) "-";
    }

  snprintf (buffer, sizeof (buffer),
	    "cid:%d, uid:%u, "
	    "is_busy:%s, is_in_tran:%s, "
	    "is_prepare_for_execute:%s, free_on_end_tran:%s, "
	    "free_on_client_io_write:%s, "
	    "free_context:%s, is_client_in_tran:%s, "
	    "is_cas_in_tran:%s, "
	    "waiting_event:(%p, %s), "
	    "func_code:%d, stmt_h_id:%d, stmt_hint_type:%d, "
	    "client_id:%d, shard_id:%d, cas_id:%d, "
	    "error_ind:%d, error_code:%d, error_msg:[%s] ",
	    ctx_p->cid, ctx_p->uid,
	    (ctx_p->is_busy) ? "Y" : "N",
	    (ctx_p->is_in_tran) ? "Y" : "N",
	    (ctx_p->is_prepare_for_execute) ? "Y" : "N",
	    (ctx_p->free_on_end_tran) ? "Y" : "N",
	    (ctx_p->free_on_client_io_write) ? "Y" : "N",
	    (ctx_p->free_context) ? "Y" : "N",
	    (ctx_p->is_client_in_tran) ? "Y" : "N",
	    (ctx_p->is_cas_in_tran) ? "Y" : "N",
	    ctx_p->waiting_event, proxy_str_event (ctx_p->waiting_event),
	    ctx_p->func_code, ctx_p->stmt_h_id,
	    ctx_p->stmt_hint_type, ctx_p->client_id,
	    ctx_p->shard_id, ctx_p->cas_id,
	    ctx_p->error_ind, ctx_p->error_code,
	    (ctx_p->error_msg[0]) ? ctx_p->error_msg : "-");

  return (char *) buffer;
}


static void
proxy_context_clear (T_PROXY_CONTEXT * ctx_p)
{
  assert (ctx_p);

  ctx_p->uid = 0;
  ctx_p->is_busy = false;
  ctx_p->is_in_tran = false;
  ctx_p->is_prepare_for_execute = false;
  ctx_p->free_on_end_tran = false;
  ctx_p->free_on_client_io_write = false;
  ctx_p->free_context = false;
  ctx_p->is_client_in_tran = false;
  ctx_p->is_cas_in_tran = false;
  ctx_p->waiting_dummy_prepare = false;
  ctx_p->dont_free_statement = false;

  if (ctx_p->waiting_event)
    {
      proxy_event_free (ctx_p->waiting_event);
      ctx_p->waiting_event = NULL;
    }

  ctx_p->func_code = PROXY_INVALID_FUNC_CODE;
  ctx_p->stmt_h_id = SHARD_STMT_INVALID_HANDLE_ID;
  ctx_p->stmt_hint_type = HT_INVAL;

  if (ctx_p->prepared_stmt
      && ctx_p->prepared_stmt->status == SHARD_STMT_STATUS_IN_PROGRESS)
    {
      /* check and wakeup statement waiter */
      shard_stmt_check_waiter_and_wakeup (ctx_p->prepared_stmt);

      /* free statement */
      shard_stmt_free (ctx_p->prepared_stmt);
    }
  ctx_p->prepared_stmt = NULL;

  proxy_context_free_stmt (ctx_p);

  ctx_p->client_id = PROXY_INVALID_CLIENT;
  ctx_p->shard_id = PROXY_INVALID_SHARD;
  ctx_p->cas_id = PROXY_INVALID_CAS;

  proxy_context_clear_error (ctx_p);
  return;
}

void
proxy_context_set_in_tran (T_PROXY_CONTEXT * ctx_p, int shard_id, int cas_id)
{
  assert (ctx_p);
  assert (shard_id >= 0);
  assert (cas_id >= 0);

  ctx_p->is_in_tran = true;
  ctx_p->shard_id = shard_id;
  ctx_p->cas_id = cas_id;
  ctx_p->dont_free_statement = false;

  return;
}

void
proxy_context_set_out_tran (T_PROXY_CONTEXT * ctx_p)
{
  assert (ctx_p);

  ctx_p->is_in_tran = false;
  ctx_p->shard_id = PROXY_INVALID_SHARD;
  ctx_p->cas_id = PROXY_INVALID_CAS;

  return;
}


T_PROXY_CONTEXT *
proxy_context_new (void)
{
  T_PROXY_CONTEXT *ctx_p;
  static unsigned int uid = 0;

  ctx_p = (T_PROXY_CONTEXT *) shard_cqueue_dequeue (&proxy_Context.freeq);
  if (ctx_p)
    {
      assert (ctx_p->is_busy == false);
      assert (ctx_p->is_in_tran == false);

      proxy_context_clear (ctx_p);
      ctx_p->uid = (++uid == 0) ? ++uid : uid;
      ctx_p->is_busy = true;

      PROXY_LOG (PROXY_LOG_MODE_SHARD_DETAIL,
		 "New context created. context(%s).",
		 proxy_str_context (ctx_p));
    }

#if defined(PROXY_VERBOSE_DEBUG)
  proxy_context_print (false);
#endif /* PROXY_VERBOSE_DEBUG */

  return ctx_p;
}


static void
proxy_context_free_client (T_PROXY_CONTEXT * ctx_p)
{
  ENTER_FUNC ();

  assert (ctx_p);

  if (ctx_p->client_id < 0)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Invalid client identifier. "
		 "(client_id:%d). context(%s).",
		 ctx_p->client_id, proxy_str_context (ctx_p));
      EXIT_FUNC ();
      return;
    }

  proxy_client_io_free_by_ctx (ctx_p->client_id, ctx_p->cid, ctx_p->uid);

  EXIT_FUNC ();
  return;
}

static void
proxy_context_free_shard (T_PROXY_CONTEXT * ctx_p)
{
  ENTER_FUNC ();

  assert (ctx_p);

  if (ctx_p->is_in_tran == false)
    {
      EXIT_FUNC ();
      return;
    }

  proxy_cas_io_free_by_ctx (ctx_p->shard_id, ctx_p->cas_id, ctx_p->cid,
			    ctx_p->uid);

  EXIT_FUNC ();
  return;
}

void
proxy_context_free (T_PROXY_CONTEXT * ctx_p)
{
  int error;

  ENTER_FUNC ();

  assert (ctx_p);

  proxy_context_free_shard (ctx_p);
  proxy_context_free_client (ctx_p);
  proxy_context_clear (ctx_p);

  error = shard_cqueue_enqueue (&proxy_Context.freeq, (void *) ctx_p);
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Failed to queue context to free queue. (error:%d).", error);
      assert (false);
    }

  EXIT_FUNC ();
  return;
}

#if defined (ENABLE_UNUSED_FUNCTION)
void
proxy_context_free_by_cid (int cid, unsigned int uid)
{
  int error;
  T_PROXY_CONTEXT *ctx_p;

  ctx_p = proxy_context_find (cid, uid);
  if (ctx_p)
    {
      proxy_context_free (ctx_p);
    }
  return;
}
#endif /* ENABLE_UNUSED_FUNCTION */

T_PROXY_CONTEXT *
proxy_context_find (int cid, unsigned int uid)
{
  T_PROXY_CONTEXT *ctx_p;

  ctx_p = &(proxy_Context.ent[cid]);

  return (ctx_p->is_busy && ctx_p->uid == uid) ? ctx_p : NULL;
}

T_PROXY_CONTEXT *
proxy_context_find_by_socket_client_io (T_SOCKET_IO * sock_io_p)
{
  T_PROXY_CONTEXT *ctx_p = NULL;
  T_CLIENT_IO *cli_io_p = NULL;

  assert (sock_io_p);

  cli_io_p =
    proxy_client_io_find_by_fd (sock_io_p->id.client_id, sock_io_p->fd);
  if (cli_io_p == NULL)
    {
      proxy_socket_io_delete (sock_io_p->fd);

      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Unable to find client entry. "
		 "(client_id:%d, fd:%d).",
		 sock_io_p->id.client_id, sock_io_p->fd);
      return NULL;
    }

  ctx_p = proxy_context_find (cli_io_p->ctx_cid, cli_io_p->ctx_uid);
  if (ctx_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Unable to find context. client(%s).",
		 proxy_str_client_io (cli_io_p));

      proxy_client_io_free (cli_io_p);
      return NULL;
    }

  return ctx_p;
}

T_CONTEXT_STMT *
proxy_context_find_stmt (T_PROXY_CONTEXT * ctx_p, int stmt_h_id)
{
  T_CONTEXT_STMT *stmt_list_p;

  for (stmt_list_p = ctx_p->stmt_list; stmt_list_p;
       stmt_list_p = stmt_list_p->next)
    {
      if (stmt_list_p->stmt_h_id != stmt_h_id)
	{
	  continue;
	}
      return stmt_list_p;
    }

  return NULL;
}

T_CONTEXT_STMT *
proxy_context_add_stmt (T_PROXY_CONTEXT * ctx_p, T_SHARD_STMT * stmt_p)
{
  int error = 0;
  T_CONTEXT_STMT *stmt_list_p;

  stmt_list_p = proxy_context_find_stmt (ctx_p, stmt_p->stmt_h_id);
  if (stmt_list_p)
    {
      return stmt_list_p;
    }

  stmt_list_p = (T_CONTEXT_STMT *) malloc (sizeof (T_CONTEXT_STMT));
  if (stmt_list_p == NULL)
    {
      PROXY_DEBUG_LOG ("malloc failed. ");
      return NULL;
    }

  error = shard_stmt_pin (stmt_p);
  if (error < 0)
    {
      FREE_MEM (stmt_list_p);

      return NULL;
    }

  stmt_list_p->stmt_h_id = stmt_p->stmt_h_id;

  stmt_list_p->next = ctx_p->stmt_list;
  ctx_p->stmt_list = stmt_list_p;

  return stmt_list_p;
}

void
proxy_context_free_stmt (T_PROXY_CONTEXT * ctx_p)
{
  T_CONTEXT_STMT *stmt_list_p, *stmt_list_np;
  T_SHARD_STMT *stmt_p;

  for (stmt_list_p = ctx_p->stmt_list; stmt_list_p;
       stmt_list_p = stmt_list_np)
    {
      stmt_list_np = stmt_list_p->next;

      stmt_p = shard_stmt_find_by_stmt_h_id (stmt_list_p->stmt_h_id);
      if (stmt_p)
	{
	  shard_stmt_unpin (stmt_p);
	}

      FREE_MEM (stmt_list_p);
    }

  ctx_p->stmt_list = NULL;

  return;
}

void
proxy_handler_destroy (void)
{
  shard_queue_destroy (&proxy_Handler.cas_rcv_q);
  shard_queue_destroy (&proxy_Handler.cli_ret_q);
  shard_queue_destroy (&proxy_Handler.cli_rcv_q);
}

int
proxy_handler_initialize (void)
{
  int error;
  static bool is_init = false;

  if (is_init == true)
    {
      return 0;
    }

  error = proxy_context_initialize ();
  if (error)
    {
      goto error_return;
    }

  error = shard_queue_initialize (&proxy_Handler.cas_rcv_q);
  if (error)
    {
      goto error_return;
    }

  error = shard_queue_initialize (&proxy_Handler.cli_ret_q);
  if (error)
    {
      goto error_return;
    }

  error = shard_queue_initialize (&proxy_Handler.cli_rcv_q);
  if (error)
    {
      goto error_return;
    }

  is_init = true;
  return 0;

error_return:
  proxy_handler_destroy ();

  return -1;
}

void
proxy_handler_process (void)
{
  T_PROXY_HANDLER *handler_p;
  void *msg;

  handler_p = &(proxy_Handler);

  /* process cas response message */
  while ((msg = shard_queue_dequeue (&handler_p->cas_rcv_q)) != NULL)
    {
      proxy_handler_process_cas_event (msg);
    }

  /* process client retry message */
  while ((msg = shard_queue_dequeue (&handler_p->cli_ret_q)) != NULL)
    {
      proxy_handler_process_client_event (msg);
    }

  /* process client request message */
  while ((msg = shard_queue_dequeue (&handler_p->cli_rcv_q)) != NULL)
    {
      proxy_handler_process_client_event (msg);
    }

  return;
}

int
proxy_wakeup_context_by_shard (T_WAIT_CONTEXT * waiter_p, int shard_id,
			       int cas_id)
{
  int error;
  T_CAS_IO *cas_io_p;
  T_PROXY_EVENT *event_p = NULL;
  T_PROXY_CONTEXT *ctx_p = NULL;

  assert (waiter_p);
  assert (shard_id >= 0);
  assert (cas_id >= 0);

  ctx_p = proxy_context_find (waiter_p->ctx_cid, waiter_p->ctx_uid);
  if (ctx_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Unable to find context. "
		 "(context id:%d, context uid:%d).",
		 waiter_p->ctx_cid, waiter_p->ctx_uid);
      goto error_return;
    }

  PROXY_DEBUG_LOG ("wakeup context(cid:%d, uid:%u) by shard(%d)/cas(%d).",
		   ctx_p->cid, ctx_p->uid, shard_id, cas_id);

  cas_io_p = proxy_cas_alloc_by_ctx (shard_id, cas_id, waiter_p->ctx_cid,
				     waiter_p->ctx_uid);
  if (cas_io_p == NULL)
    {
      PROXY_DEBUG_LOG ("failed to proxy_cas_alloc_by_ctx. "
		       "(shard_id:%d, cas_id:%d, ctx_cid:%d, ctx_uid:%d",
		       shard_id, cas_id,
		       waiter_p->ctx_cid, waiter_p->ctx_uid);
      goto error_return;
    }
  if (cas_io_p == (T_CAS_IO *) SHARD_TEMPORARY_UNAVAILABLE)
    {
      assert (false);
      goto error_return;
    }

  proxy_context_set_in_tran (ctx_p, cas_io_p->shard_id, cas_io_p->cas_id);

  event_p = proxy_event_new (PROXY_EVENT_CLIENT_WAKEUP_BY_SHARD,
			     PROXY_EVENT_FROM_CLIENT);
  if (event_p == NULL)
    {
      PROXY_DEBUG_LOG ("failed to proxy_event_new.");
      goto error_return;
    }
  proxy_event_set_context (event_p, waiter_p->ctx_cid, waiter_p->ctx_uid);
  proxy_event_set_shard (event_p, cas_io_p->shard_id, cas_io_p->cas_id);

  error = shard_queue_enqueue (&proxy_Handler.cli_ret_q, (void *) event_p);
  if (error)
    {
      assert (false);

      proxy_event_free (event_p);
      event_p = NULL;

      PROXY_DEBUG_LOG ("failed to shard_queue_enqueue.");
      goto error_return;
    }

  return 0;

error_return:
  if (ctx_p)
    {
      proxy_context_free (ctx_p);
    }
  return -1;
}

int
proxy_wakeup_context_by_statement (T_WAIT_CONTEXT * waiter_p)
{
  int error;
  T_PROXY_EVENT *event_p;

  assert (waiter_p);

  event_p = proxy_event_new (PROXY_EVENT_CLIENT_WAKEUP_BY_STATEMENT,
			     PROXY_EVENT_FROM_CLIENT);
  if (event_p == NULL)
    {
      PROXY_DEBUG_LOG ("failed to proxy_event_new.");
      return -1;
    }
  proxy_event_set_context (event_p, waiter_p->ctx_cid, waiter_p->ctx_uid);

  error = shard_queue_enqueue (&proxy_Handler.cli_ret_q, (void *) event_p);
  if (error)
    {
      PROXY_DEBUG_LOG ("failed to shard_queue_enqueue.");
      assert (false);
      return -1;
    }

  return 0;
}

T_PROXY_EVENT *
proxy_event_new (unsigned int type, int from_cas)
{
  T_PROXY_EVENT *event_p;

  event_p = (T_PROXY_EVENT *) malloc (sizeof (T_PROXY_EVENT));
  if (event_p)
    {
      event_p->type = type;
      event_p->from_cas = from_cas;

      event_p->cid = PROXY_INVALID_CONTEXT;
      event_p->uid = 0;
      event_p->shard_id = PROXY_INVALID_SHARD;
      event_p->cas_id = PROXY_INVALID_CAS;

      memset (((void *) &event_p->buffer), 0, sizeof (event_p->buffer));
    }

  return event_p;
}

T_PROXY_EVENT *
proxy_event_dup (T_PROXY_EVENT * event_p)
{
  T_PROXY_EVENT *new_event_p;

  new_event_p = (T_PROXY_EVENT *) malloc (sizeof (T_PROXY_EVENT));
  if (new_event_p)
    {
      memcpy ((void *) new_event_p, (void *) event_p,
	      offsetof (T_PROXY_EVENT, buffer));

      new_event_p->buffer.length = event_p->buffer.length;
      new_event_p->buffer.offset = event_p->buffer.offset;
      new_event_p->buffer.data =
	(char *) malloc (event_p->buffer.length * sizeof (char));
      if (new_event_p->buffer.data == NULL)
	{
	  FREE_MEM (new_event_p);
	  return NULL;
	}
      memcpy ((void *) new_event_p->buffer.data,
	      (void *) event_p->buffer.data, event_p->buffer.length);
    }

  return new_event_p;
}

T_PROXY_EVENT *
proxy_event_new_with_req (T_BROKER_VERSION client_version, unsigned int type,
			  int from, T_PROXY_EVENT_FUNC req_func)
{
  T_PROXY_EVENT *event_p;
  char *msg = NULL;
  int length;

  event_p = proxy_event_new (type, from);
  if (event_p == NULL)
    {
      return NULL;
    }

  length = req_func (client_version, &msg);
  if (length <= 0)
    {
      proxy_event_free (event_p);
      return NULL;
    }

  proxy_event_set_buffer (event_p, msg, length);

  return event_p;
}

T_PROXY_EVENT *
proxy_event_new_with_rsp (T_BROKER_VERSION client_version, unsigned int type,
			  int from, T_PROXY_EVENT_FUNC resp_func)
{
  return proxy_event_new_with_req (client_version, type, from, resp_func);
}

T_PROXY_EVENT *
proxy_event_new_with_error (T_BROKER_VERSION client_version,
			    unsigned int type, int from,
			    int (*err_func) (T_BROKER_VERSION client_version,
					     char **buffer, int error_ind,
					     int error_code, char *error_msg,
					     char is_in_tran), int error_ind,
			    int error_code, char *error_msg, char is_in_tran)
{
  T_PROXY_EVENT *event_p;
  char *msg = NULL;
  int length;

  event_p = proxy_event_new (type, from);
  if (event_p == NULL)
    {
      return NULL;
    }

  length =
    err_func (client_version, &msg, error_ind, error_code, error_msg,
	      is_in_tran);
  if (length <= 0)
    {
      proxy_event_free (event_p);
      return NULL;
    }

  proxy_event_set_buffer (event_p, msg, length);

  return event_p;
}

int
proxy_event_alloc_buffer (T_PROXY_EVENT * event_p, unsigned int size)
{
  assert (event_p);

  event_p->buffer.length = size;
  event_p->buffer.offset = 0;
  event_p->buffer.data = (char *) malloc (size * sizeof (char));

  return (event_p->buffer.data != NULL) ? 0 : -1;
}

int
proxy_event_realloc_buffer (T_PROXY_EVENT * event_p, unsigned int size)
{
  char *old_data;

  assert (event_p);
  assert (event_p->buffer.data);

  old_data = event_p->buffer.data;

  assert (event_p->buffer.length < (int) size);

  event_p->buffer.data = (char *) realloc (event_p->buffer.data,
					   size * sizeof (char));
  if (event_p->buffer.data == NULL)
    {
      event_p->buffer.data = old_data;
      return -1;
    }
  event_p->buffer.length = size;

  return 0;
}

void
proxy_event_set_buffer (T_PROXY_EVENT * event_p, char *data,
			unsigned int size)
{
  assert (event_p);

  event_p->buffer.length = size;
  event_p->buffer.offset = 0;
  event_p->buffer.data = data;
}

void
proxy_event_set_type_from (T_PROXY_EVENT * event_p, unsigned int type,
			   int from_cas)
{
  assert (event_p);

  event_p->type = type;
  event_p->from_cas = from_cas;

  return;
}

void
proxy_event_set_context (T_PROXY_EVENT * event_p, int cid, unsigned int uid)
{
  assert (event_p);

  event_p->cid = cid;
  event_p->uid = uid;

  return;
}


void
proxy_event_set_shard (T_PROXY_EVENT * event_p, int shard_id, int cas_id)
{
  assert (event_p);

  event_p->shard_id = shard_id;
  event_p->cas_id = cas_id;

  return;
}

bool
proxy_event_io_read_complete (T_PROXY_EVENT * event_p)
{
  assert (event_p);
  assert (event_p->type == PROXY_EVENT_IO_READ);

  return (event_p->buffer.length != 0
	  && event_p->buffer.length == event_p->buffer.offset) ? true : false;
}

#if defined (ENABLE_UNUSED_FUNCTION)
bool
proxy_event_io_write_complete (T_PROXY_EVENT * event_p)
{
  assert (event_p);
  assert (event_p->type == PROXY_EVENT_IO_WRITE);

  return (event_p->buffer.length != 0
	  && event_p->buffer.length == event_p->buffer.offset) ? true : false;
}
#endif /* ENABLE_UNUSED_FUNCTION */

void
proxy_event_free (T_PROXY_EVENT * event_p)
{
  assert (event_p);

  proxy_io_buffer_clear (&event_p->buffer);
  FREE_MEM (event_p);

  return;
}

char *
proxy_str_event (T_PROXY_EVENT * event_p)
{
  static char buffer[LINE_MAX];

  if (event_p == NULL)
    {
      return (char *) "-";
    }

  snprintf (buffer, sizeof (buffer),
	    "type:%d, from_cas:%s, cid:%d, uid:%u, shard_id:%d, cas_id:%d",
	    event_p->type, (event_p->from_cas) ? "Y" : "N",
	    event_p->cid, event_p->uid, event_p->shard_id, event_p->cas_id);

  return (char *) buffer;
}
