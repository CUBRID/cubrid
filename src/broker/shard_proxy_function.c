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
 * shard_proxy_function.c - 
 *     
 */

#ident "$Id$"


#include <stdio.h>
#include <stdlib.h>
#include <assert.h>


#include "shard_proxy.h"
#include "shard_proxy_handler.h"
#include "shard_proxy_function.h"
#include "shard_statement.h"
#include "shard_parser.h"
#include "shard_key_func.h"

extern T_SHM_SHARD_KEY *shm_key_p;
extern T_PROXY_INFO *proxy_info_p;
extern T_PROXY_HANDLER proxy_Handler;
extern T_PROXY_CONTEXT proxy_Context;

extern int make_net_buf (T_NET_BUF * net_buf, int size);
extern int make_header_info (T_NET_BUF * net_buf,
			     MSG_HEADER * client_msg_header);

static int proxy_get_shard_id (T_SHARD_STMT * stmt_p, void **argv,
			       T_SHARD_KEY_RANGE ** range_p_out);
static T_SHARD_KEY_RANGE *proxy_get_range_by_param (SP_PARSER_HINT * hint_p,
						    void **argv);
static void proxy_update_shard_id_statistics (T_SHARD_STMT * stmt_p,
					      T_SHARD_KEY_RANGE * range_p);

int
proxy_check_cas_error (char *read_msg)
{
  /* Protocol Phase 1 */
  /* INT : ERROR_INDICATOR(ERROR_INDICATOR_UNSET | CAS_ERROR_INDICATOR | DBMS_ERROR_INDICATOR) */
  /* INT : ERROR_CODE */
  /* */
  /* Protocol Phase 2 : ERROR_STRING OR '\0' */
  /* STRING : ERROR_MSG */

  char *data;
  int error_ind;

  data = read_msg + MSG_HEADER_SIZE;
  error_ind = (int) ntohl (*(int *) data);

  return error_ind;
}

int
proxy_get_cas_error_code (char *read_msg)
{
  char *data;
  int error_code;

  data = read_msg + MSG_HEADER_SIZE + sizeof (int) /* error indicator */ ;
  error_code = (int) ntohl (*(int *) data);

  return error_code;
}

static int
proxy_send_request_to_cas (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p)
{
  int error, length;
  T_CAS_IO *cas_io_p;

  ENTER_FUNC ();

  cas_io_p = proxy_cas_alloc_by_ctx (ctx_p->shard_id, ctx_p->cas_id,
				     ctx_p->cid, ctx_p->uid);
  if (cas_io_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to allocate CAS. "
		 "(shard_id:%d, cas_id:%d, context id:%d, context uid:%d).",
		 ctx_p->shard_id, ctx_p->cas_id, ctx_p->cid, ctx_p->uid);
      EXIT_FUNC ();
      return -1;
    }
  else if (cas_io_p == (T_CAS_IO *) SHARD_TEMPORARY_UNAVAILABLE)
    {
      assert (ctx_p->shard_id < 0 || ctx_p->cas_id < 0);

      EXIT_FUNC ();
      return 1 /* waiting idle cas */ ;
    }

  if (ctx_p->is_in_tran == false)
    {
      proxy_context_set_in_tran (ctx_p, cas_io_p->shard_id, cas_io_p->cas_id);
    }

  error = proxy_cas_io_write (cas_io_p, event_p);
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to write to CAS. "
		 "CAS(%s). event(%s).",
		 proxy_str_cas_io (cas_io_p), proxy_str_event (event_p));
      EXIT_FUNC ();
      return -1;
    }
  event_p = NULL;

  EXIT_FUNC ();
  return 0;
}

static int
proxy_send_request_to_cas_with_new_event (T_PROXY_CONTEXT * ctx_p,
					  unsigned int type, int from,
					  T_PROXY_EVENT_FUNC req_func)
{
  int error;
  T_PROXY_EVENT *event_p = NULL;

  event_p = proxy_event_new_with_req (type, from, req_func);
  if (event_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Failed to make request event. (error:%d). context(%s).",
		 error, proxy_str_context (ctx_p));

      error = -1;
      goto error;
    }

  error = proxy_send_request_to_cas (ctx_p, event_p);
  if (error < 0)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Failed to send request to CAS. "
		 "(error=%d). context(%s). evnet(%s).", error,
		 proxy_str_context (ctx_p), proxy_str_event (event_p));
      goto error;
    }
  else if (error > 0)
    {
      goto error;
    }

  event_p = NULL;

  return error;

error:
  if (event_p != NULL)
    {
      proxy_event_free (event_p);
      event_p = NULL;
    }
  return error;
}

int
proxy_send_response_to_client (T_PROXY_CONTEXT * ctx_p,
			       T_PROXY_EVENT * event_p)
{
  int error;
  int length;
  T_CLIENT_IO *cli_io_p;

  ENTER_FUNC ();

  /* find client io */
  cli_io_p = proxy_client_io_find_by_ctx (ctx_p->client_id, ctx_p->cid,
					  ctx_p->uid);
  if (cli_io_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to find client. "
		 "(client_id:%d, context id:%d, context uid:%u).",
		 ctx_p->client_id, ctx_p->cid, ctx_p->uid);

      EXIT_FUNC ();
      return -1;
    }

  error = proxy_client_io_write (cli_io_p, event_p);
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to write to client. "
		 "client(%s). event(%s).",
		 proxy_str_client_io (cli_io_p), proxy_str_event (event_p));
      EXIT_FUNC ();
      return -1;
    }

  EXIT_FUNC ();
  return 0;
}

int
proxy_send_response_to_client_with_new_event (T_PROXY_CONTEXT * ctx_p,
					      unsigned int type, int from,
					      T_PROXY_EVENT_FUNC resp_func)
{
  int error;
  T_PROXY_EVENT *event_p = NULL;

  event_p = proxy_event_new_with_rsp (type, from, resp_func);
  if (event_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Failed to make response event. (error:%d). context(%s).",
		 error, proxy_str_context (ctx_p));

      goto error;
    }

  error = proxy_send_response_to_client (ctx_p, event_p);
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Failed to response to the client. "
		 "(error=%d). context(%s). evnet(%s).", error,
		 proxy_str_context (ctx_p), proxy_str_event (event_p));

      goto error;
    }

  event_p = NULL;

  return error;

error:
  if (event_p != NULL)
    {
      proxy_event_free (event_p);
      event_p = NULL;
    }
  return -1;
}

static int
proxy_send_prepared_stmt_to_client (T_PROXY_CONTEXT * ctx_p,
				    T_SHARD_STMT * stmt_p)
{
  int error;
  int length;
  char *prepare_resp;
  T_CLIENT_IO *cli_io_p;
  T_PROXY_EVENT *event_p;

  prepare_resp = proxy_dup_msg (stmt_p->reply_buffer);
  if (prepare_resp == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Not enough virtual memory. "
		 "failed to duplicate prepare request. ");

      EXIT_FUNC ();
      return -1;
    }

  event_p = proxy_event_new (PROXY_EVENT_IO_WRITE, PROXY_EVENT_FROM_CLIENT);
  if (event_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to make event. "
		 "(%s, %s). context(%s).", "PROXY_EVENT_IO_WRITE",
		 "PROXY_EVENT_FROM_CLIENT", proxy_str_context (ctx_p));

      EXIT_FUNC ();
      return -1;
    }

  length = get_msg_length (prepare_resp);
  proxy_event_set_buffer (event_p, prepare_resp, length);

  error = proxy_send_response_to_client (ctx_p, event_p);
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to send response "
		 "to the client. (error:%d). context(%s). event(%s). ", error,
		 proxy_str_context (ctx_p), proxy_str_event (event_p));

      proxy_event_free (event_p);
      event_p = NULL;

      EXIT_FUNC ();
      return -1;
    }
  event_p = NULL;

  EXIT_FUNC ();
  return 0;
}

static void
proxy_update_shard_id_statistics (T_SHARD_STMT * stmt_p,
				  T_SHARD_KEY_RANGE * range_p)
{
  SP_PARSER_HINT *first_hint_p;

  T_SHM_SHARD_KEY_STAT *key_stat_p;
  T_SHM_SHARD_CONN_STAT *shard_stat_p;

  int shard_id, key_index, range_index;

  first_hint_p = sp_get_first_hint (stmt_p->parser);
  if (first_hint_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Failed to update stats. No hint available. ");
      return;
    }

  switch (first_hint_p->hint_type)
    {
    case HT_KEY:
      if (range_p == NULL)
	{
	  PROXY_LOG (PROXY_LOG_MODE_ERROR,
		     "Failed to update stats. Invalid hint key range. (hint_type:%s).",
		     "HT_KEY");
	  assert (range_p);
	  return;
	}
      shard_id = range_p->shard_id;

      shard_stat_p = shard_shm_get_shard_stat (proxy_info_p, shard_id);
      if (shard_stat_p == NULL)
	{
	  PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to update stats. "
		     "Invalid shm shard stats. (hint_type:%s, proxy_id:%d, shard_id:%d).",
		     "HT_KEY", proxy_info_p->proxy_id, shard_id);

	  assert (shard_stat_p);
	  return;
	}

      shard_stat_p->num_hint_key_queries_requested++;

      key_index = range_p->key_index;
      range_index = range_p->range_index;

      key_stat_p = shard_shm_get_key_stat (proxy_info_p, key_index);
      if (key_stat_p == NULL)
	{
	  PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to update stats. "
		     "Invalid shm shard key stats. (hint_type:%s, proxy_id:%d, key_index:%d).",
		     "HT_KEY", proxy_info_p->proxy_id, key_index);

	  assert (key_stat_p);
	  return;
	}

      key_stat_p->stat[range_index].num_range_queries_requested++;
      return;

    case HT_ID:
      assert (first_hint_p->arg.type == VT_INTEGER);
      if (first_hint_p->arg.type == VT_INTEGER)
	{
	  shard_id = first_hint_p->arg.integer;
	  if (proxy_info_p->num_shard_conn <= shard_id)
	    {
	      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to update stats. "
			 "Invalid shard id. (hint_type:%s, shard_id:%d).",
			 "HT_ID", shard_id);
	    }

	  shard_stat_p = shard_shm_get_shard_stat (proxy_info_p, shard_id);
	  if (shard_stat_p == NULL)
	    {
	      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to update stats. "
			 "Invalid shm shard stats. (hint_type:%s, proxy_id:%d, shard_id:%d).",
			 "HT_ID", proxy_info_p->proxy_id, shard_id);

	      assert (shard_stat_p);
	      return;
	    }

	  shard_stat_p->num_hint_id_queries_requested++;
	  return;
	}
      else
	{
	  PROXY_LOG (PROXY_LOG_MODE_ERROR,
		     "Unsupported hint value type. (hint_value_type:%d).",
		     first_hint_p->arg.type);

	  return;
	}
      break;

    default:
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Unsupported hint type. (hint_type:%d).",
		 first_hint_p->hint_type);

      assert (false);
      break;
    }

  return;
}

static int
proxy_get_shard_id (T_SHARD_STMT * stmt_p, void **argv,
		    T_SHARD_KEY_RANGE ** range_p_out)
{
  SP_PARSER_HINT *first_hint_p;
  T_SHARD_KEY_RANGE *range_p;

  *range_p_out = NULL;

  first_hint_p = sp_get_first_hint (stmt_p->parser);
  if (first_hint_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Unable to get shard id. No hint available.");

      return PROXY_INVALID_SHARD;
    }

  switch (first_hint_p->hint_type)
    {
    case HT_KEY:
      range_p = proxy_get_range_by_param (first_hint_p, argv);
      if (range_p == NULL)
	{
	  PROXY_LOG (PROXY_LOG_MODE_ERROR,
		     "Unable to get shard id. Invalid hint key range. (hint_type:%s).",
		     "HT_KEY");
	  return PROXY_INVALID_SHARD;
	}
      *range_p_out = range_p;
      assert (range_p->shard_id >= 0);
      return range_p->shard_id;

    case HT_ID:
      if (first_hint_p->arg.type == VT_INTEGER)
	{
	  return first_hint_p->arg.integer;
	}
      else
	{
	  PROXY_LOG (PROXY_LOG_MODE_ERROR,
		     "Unable to get shard id. shard id is not integer type. (hint_type:%s, type:%d).",
		     "HT_ID", first_hint_p->arg.type);

	  return PROXY_INVALID_SHARD;
	}
      break;

    default:

      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Unsupported hint type. (hint_type:%d).",
		 first_hint_p->hint_type);
      return PROXY_INVALID_SHARD;
    }

  return PROXY_INVALID_SHARD;
}

static T_SHARD_KEY_RANGE *
proxy_get_range_by_param (SP_PARSER_HINT * hint_p, void **argv)
{
  int ret;
  int hint_position, num_bind;
  int type_idx, val_idx;

  char type;
  int data_size;
  void *net_type, *net_value;

  T_SHARD_KEY *key_p;
  T_SHARD_KEY_RANGE *range_p = NULL;

  int shard_key_id = 0;
  int shard_key_val_int;
  char *shard_key_val_string;
  int shard_key_val_len;
  const char *key_column;

  /* Phase 0 : hint position get */
  /* SHARD TODO : find statement entry, and param position & etc */
  /* SHARD TODO : multiple key_value */

  assert (shm_key_p->num_shard_key == 1);
  if (shm_key_p->num_shard_key != 1)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Too may shard key column in config. "
		 "(num_shard_key:%d).", shm_key_p->num_shard_key);
      return NULL;
    }

  key_p = (T_SHARD_KEY *) (&(shm_key_p->shard_key[0]));
  key_column = key_p->key_column;

  switch (hint_p->bind_type)
    {
    case BT_STATIC:
      shard_key_id =
	proxy_find_shard_id_by_hint_value (&hint_p->value, key_column);
      if (shard_key_id < 0)
	{
	  return NULL;
	}
      num_bind = 0;
      break;
    case BT_DYNAMIC:
      assert (hint_p->bind_position >= 0);
      hint_position = hint_p->bind_position;
      num_bind = 1;
      break;
    default:
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Unsupported hint bind type. (bind_type:%d).",
		 hint_p->bind_type);
      return NULL;
    }

  /* Phase 1 : Param value get */
  if (num_bind > 0)
    {
      type_idx = 2 * hint_position;
      val_idx = 2 * hint_position + 1;
      net_type = argv[type_idx];
      net_value = argv[val_idx];

      net_arg_get_char (type, net_type);

      switch (type)
	{
	case CCI_U_TYPE_INT:
	  {
	    int i_val;
	    net_arg_get_size (&data_size, net_value);
	    if (data_size <= 0)
	      {
		PROXY_LOG (PROXY_LOG_MODE_ERROR,
			   "Unexpected integer hint value size."
			   "(size:%d).", data_size);
		return NULL;
	      }

	    net_arg_get_int (&i_val, net_value);
	    shard_key_id =
	      (*fn_get_shard_key) (key_column, SHARD_U_TYPE_INT, &i_val,
				   sizeof (int));
	  }
	  break;
	case CCI_U_TYPE_STRING:
	  {
	    char *s_val;
	    int s_len;
	    net_arg_get_str (&s_val, &s_len, net_value);
	    if (s_val == NULL || s_len == 0)
	      {
		PROXY_LOG (PROXY_LOG_MODE_ERROR,
			   "Invalid string hint values. (len:%d).", s_len);
		return NULL;
	      }
	    shard_key_id =
	      (*fn_get_shard_key) (key_column, SHARD_U_TYPE_STRING, s_val,
				   s_len);
	  }
	  break;

	  /* SHARD TODO : support other hint data types */

	default:
	  PROXY_LOG (PROXY_LOG_MODE_ERROR,
		     "Unsupported hint value type. (type:%d).", type);

	  return NULL;

	}
    }

  if (shard_key_id < 0)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to get shard key id. "
		 "(shard_key_id:%d).", shard_key_id);
      return NULL;
    }

  /* Phase 2 : Shard range get */
  range_p =
    shard_metadata_find_shard_range (shm_key_p, (char *) key_column,
				     shard_key_id);
  if (range_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Unable to find shm shard range. (key:%s, key_id:%d).",
		 key_column, shard_key_id);
      return NULL;
    }

  PROXY_LOG (PROXY_LOG_MODE_SHARD_DETAIL,
	     "Select shard. (shard_id:%d, key_column:[%s], shard_key_id:%d).",
	     range_p->shard_id, key_column, shard_key_id);

  return range_p;
}

/* 
 * request event 
 */
int
fn_proxy_client_end_tran (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p,
			  int argc, char **argv)
{
  int error = 0;
  int length;

  T_CAS_IO *cas_io_p;

  const char func_code = CAS_FC_END_TRAN;

  ENTER_FUNC ();

  /* set client tran status as OUT_TRAN, when end_tran */
  ctx_p->is_client_in_tran = false;

  if (ctx_p->is_in_tran)
    {
      error = proxy_send_request_to_cas (ctx_p, event_p);
      if (error < 0)
	{
	  PROXY_LOG (PROXY_LOG_MODE_ERROR,
		     "Failed to send request to CAS. "
		     "(error=%d). context(%s). evnet(%s).", error,
		     proxy_str_context (ctx_p), proxy_str_event (event_p));

	  goto free_context;
	}
      else if (error > 0)
	{
	  assert (false);
	  goto free_context;
	}

      ctx_p->func_code = func_code;
    }
  else
    {
      error = proxy_send_response_to_client_with_new_event (ctx_p,
							    PROXY_EVENT_IO_WRITE,
							    PROXY_EVENT_FROM_CLIENT,
							    proxy_io_make_end_tran_ok);
      if (error)
	{
	  goto free_context;
	}

      proxy_event_free (event_p);
      event_p = NULL;
    }

  /* free statement list */
  proxy_context_free_stmt (ctx_p);

  EXIT_FUNC ();
  return 0;

free_context:
  if (event_p)
    {
      proxy_event_free (event_p);
      event_p = NULL;
    }

  ctx_p->free_context = true;

  EXIT_FUNC ();
  return -1;
}

int
fn_proxy_client_prepare (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p,
			 int argc, char **argv)
{
  int error = 0;
  int i;
  int length;

  /* sql statement */
  char *sql_stmt;
  char *organized_sql_stmt = NULL;
  int sql_size;

  /* argv */
  char flag;
  char auto_commit_mode;
  int srv_h_id;

  /* io/statement entries */
  T_CLIENT_IO *cli_io_p;
  T_CAS_IO *cas_io_p;
  T_SHARD_STMT *stmt_p;
  T_WAIT_CONTEXT *waiter_p;

  char *request_p;

  const char func_code = CAS_FC_PREPARE;

  ENTER_FUNC ();

  /* set client tran status as IN_TRAN, when prepare */
  ctx_p->is_client_in_tran = true;

  request_p = event_p->buffer.data;
  assert (request_p);		// __FOR_DEBUG

  if (ctx_p->waiting_event)
    {
      assert (false);

      proxy_event_free (ctx_p->waiting_event);
      ctx_p->waiting_event = NULL;

      goto free_context;
    }

  /* process argv */
  if (argc < 2)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Invalid argument. (argc:%d). context(%s).", argc,
		 proxy_str_context (ctx_p));
      proxy_context_set_error (ctx_p, CAS_ERROR_INDICATOR, CAS_ER_ARGS);

      proxy_event_free (event_p);
      event_p = NULL;

      EXIT_FUNC ();
      return -1;
    }

  net_arg_get_str (&sql_stmt, &sql_size, argv[0]);
  net_arg_get_char (flag, argv[1]);
  if (argc > 2)
    {
      net_arg_get_char (auto_commit_mode, argv[2]);
      for (i = 3; i < argc; i++)
	{
	  int deferred_close_handle;
	  net_arg_get_int (&deferred_close_handle, argv[i]);

	  /* SHARD TODO : what to do for deferred close handle? */
	}
    }
  else
    {
      auto_commit_mode = FALSE;
    }

  PROXY_DEBUG_LOG ("Process requested prepare sql statement. "
		   "(sql_stmt:[%s]). context(%s).",
		   sql_stmt, proxy_str_context (ctx_p));

  organized_sql_stmt =
    shard_stmt_rewrite_sql (sql_stmt, proxy_info_p->appl_server);
  if (organized_sql_stmt == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Failed to rewrite sql statement. (sql_stmt:[%s]).",
		 sql_stmt);
      proxy_context_set_error (ctx_p, CAS_ERROR_INDICATOR, CAS_ER_ARGS);

      proxy_event_free (event_p);
      event_p = NULL;

      EXIT_FUNC ();
      return -1;
    }
  PROXY_DEBUG_LOG ("Rewrite sql statement. "
		   "(organized_sql_stmt:[%s]). context(%s).",
		   organized_sql_stmt, proxy_str_context (ctx_p));

  stmt_p = shard_stmt_find_by_sql (organized_sql_stmt);
  if (stmt_p)
    {
      switch (stmt_p->status)
	{
	case SHARD_STMT_STATUS_COMPLETE:
	  error = proxy_send_prepared_stmt_to_client (ctx_p, stmt_p);
	  if (error)
	    {
	      PROXY_LOG (PROXY_LOG_MODE_ERROR,
			 "Failed to send prepared statment to client. "
			 "(error:%d). context(%s).", error,
			 proxy_str_context (ctx_p));
	      goto free_context;
	    }

	  /* save statement to the context */
	  if (proxy_context_add_stmt (ctx_p, stmt_p) == NULL)
	    {
	      PROXY_LOG (PROXY_LOG_MODE_ERROR,
			 "Failed to add statement to context. "
			 "statement(%s). context(%s).",
			 shard_str_stmt (stmt_p), proxy_str_context (ctx_p));
	      goto free_context;
	    }

	  /* we should free event right now */
	  proxy_event_free (event_p);
	  event_p = NULL;


	  /* do not relay request to the shard/cas */
	  error = 0;
	  goto end;

	case SHARD_STMT_STATUS_IN_PROGRESS:

	  /* if this context was woken up by shard/cas resource 
	   * and try dummy prepare again */
	  if (stmt_p->ctx_cid == ctx_p->cid && stmt_p->ctx_uid == ctx_p->uid)
	    {
	      assert (ctx_p->prepared_stmt == stmt_p);
	      goto relay_prepare_request;
	    }

	  waiter_p = proxy_waiter_new (ctx_p->cid, ctx_p->uid);
	  if (waiter_p == NULL)
	    {
	      goto free_context;
	    }

	  error = shard_queue_enqueue (&stmt_p->waitq, (void *) waiter_p);
	  if (error)
	    {
	      proxy_waiter_free (waiter_p);
	      waiter_p = NULL;

	      goto free_context;
	    }

	  ctx_p->waiting_event = event_p;
	  event_p = NULL;	/* DO NOT DELETE */

	  error = 0;
	  goto end;

	default:
	  assert (false);
	  goto free_context;
	}

      assert (false);
      goto free_context;
    }

  stmt_p = shard_stmt_new (organized_sql_stmt, ctx_p->cid, ctx_p->uid);
  if (stmt_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Failed to create new statement. context(%s).",
		 proxy_str_context (ctx_p));
      proxy_context_set_error (ctx_p, CAS_ERROR_INDICATOR, CAS_ER_INTERNAL);

      proxy_event_free (event_p);
      event_p = NULL;

      error = -1;
      goto end;
    }

  PROXY_DEBUG_LOG ("Create new sql statement. "
		   "(index:%d). statement(%s). context(%s).",
		   stmt_p->index, shard_str_stmt (stmt_p),
		   proxy_str_context (ctx_p));

  error = shard_stmt_set_hint_list (stmt_p);
  if (error < 0)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Failed to set hint list. statement(%s). context(%s).",
		 shard_str_stmt (stmt_p), proxy_str_context (ctx_p));

      proxy_context_set_error (ctx_p, CAS_ERROR_INDICATOR, CAS_ER_INTERNAL);

      /* check and wakeup statement waiter */
      shard_stmt_check_waiter_and_wakeup (stmt_p);

      /* 
       * there must be no context sharing this statement at this time.
       * so, we can free statement.
       */
      shard_stmt_free (stmt_p);
      stmt_p = NULL;

      proxy_event_free (event_p);
      event_p = NULL;

      error = -1;
      goto end;
    }

  ctx_p->prepared_stmt = stmt_p;

  /* save statement to the context */
  if (proxy_context_add_stmt (ctx_p, stmt_p) == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Failed to link statement to context. statement(%s). context(%s).",
		 shard_str_stmt (stmt_p), proxy_str_context (ctx_p));
      goto free_context;
    }

  /* save prepare request */
  stmt_p->request_buffer = proxy_dup_msg (request_p);
  if (stmt_p->request_buffer == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Failed to duplicate prepared statement request. statement(%s). context(%s).",
		 shard_str_stmt (stmt_p), proxy_str_context (ctx_p));
      goto free_context;
    }
  stmt_p->request_buffer_length = get_msg_length (request_p);

relay_prepare_request:
  if (ctx_p->is_in_tran == false)
    {
      proxy_set_force_out_tran (request_p);
    }

  if (stmt_p->status != SHARD_STMT_STATUS_IN_PROGRESS)
    {
      PROXY_DEBUG_LOG
	("Unexpected statement status. (status=%d). statement(%s). context(%s).",
	 stmt_p->status, shard_str_stmt (stmt_p), proxy_str_context (ctx_p));
      assert (false);
      goto free_context;
    }

  cas_io_p = proxy_cas_alloc_by_ctx (ctx_p->shard_id, ctx_p->cas_id,
				     ctx_p->cid, ctx_p->uid);
  if (cas_io_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to allocate CAS. context(%s).",
		 proxy_str_context (ctx_p));

      proxy_context_set_error (ctx_p, CAS_ERROR_INDICATOR, CAS_ER_INTERNAL);

      /* wakeup and reset statment */
      shard_stmt_check_waiter_and_wakeup (ctx_p->prepared_stmt);
      shard_stmt_free (ctx_p->prepared_stmt);
      ctx_p->prepared_stmt = NULL;

      proxy_event_free (event_p);
      event_p = NULL;

      EXIT_FUNC ();
      return -1;
    }
  else if (cas_io_p == (T_CAS_IO *) SHARD_TEMPORARY_UNAVAILABLE)
    {
      /* waiting idle shard/cas */
      ctx_p->waiting_event = event_p;
      event_p = NULL;

      EXIT_FUNC ();
      return 0;
    }

  ctx_p->waiting_event = proxy_event_dup (event_p);
  if (ctx_p->waiting_event == NULL)
    {
      goto free_context;
    }

  error = proxy_cas_io_write (cas_io_p, event_p);
  if (error)
    {
      goto free_context;
    }
  event_p = NULL;

  proxy_context_set_in_tran (ctx_p, cas_io_p->shard_id, cas_io_p->cas_id);

  ctx_p->func_code = func_code;

end:
  if (organized_sql_stmt)
    {
      FREE_MEM (organized_sql_stmt);
    }

  EXIT_FUNC ();
  return error;

free_context:
  if (organized_sql_stmt)
    {
      FREE_MEM (organized_sql_stmt);
    }

  if (ctx_p->waiting_event == event_p)
    {
      ctx_p->waiting_event = NULL;
    }

  if (event_p)
    {
      proxy_event_free (event_p);
      event_p = NULL;
    }

  ctx_p->free_context = true;

  EXIT_FUNC ();
  return error;
}

int
fn_proxy_client_execute (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p,
			 int argc, char **argv)
{
  int error = 0;
  int srv_h_id;
  int cas_srv_h_id;
  char *prepare_request = NULL;
  int i;
  int cas_index, shard_id;
  int length;
  SP_HINT_TYPE hint_type;
  const int bind_value_index = 9;
  const int argc_mod_2 = 1;
  T_CAS_IO *cas_io_p;
  T_SHARD_STMT *stmt_p;
  T_SHARD_KEY_RANGE *range_p = NULL;

  T_PROXY_EVENT *new_event_p = NULL;

  char *request_p;

  char func_code = CAS_FC_EXECUTE;

  ENTER_FUNC ();

  request_p = event_p->buffer.data;
  assert (request_p);		// __FOR_DEBUG

  if (ctx_p->waiting_event)
    {
      assert (false);

      proxy_event_free (ctx_p->waiting_event);
      ctx_p->waiting_event = NULL;

      goto free_context;
    }

  if ((argc < bind_value_index) || (argc % 2 != argc_mod_2))
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Invalid argument. (argc:%d). context(%s).", argc,
		 proxy_str_context (ctx_p));

      proxy_context_set_error (ctx_p, CAS_ERROR_INDICATOR, CAS_ER_ARGS);

      proxy_event_free (event_p);
      event_p = NULL;

      EXIT_FUNC ();
      return -1;
    }

  net_arg_get_int (&srv_h_id, argv[0]);

  /* arg9 ~ : bind variables, even:bind type, odd:bind value */

  stmt_p = shard_stmt_find_by_stmt_h_id (srv_h_id);
  if (stmt_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Unable to find statement handle ideitifier. "
		 "(srv_h_id:%d). context(%s).", srv_h_id,
		 proxy_str_context (ctx_p));

      proxy_context_set_error (ctx_p, CAS_ERROR_INDICATOR,
			       CAS_ER_STMT_POOLING);

      proxy_event_free (event_p);
      event_p = NULL;

      EXIT_FUNC ();
      return -1;
    }

  hint_type = shard_stmt_get_hint_type (stmt_p);
  if (hint_type <= HT_INVAL || hint_type > HT_EOF)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Unsupported hint type. (hint_type:%d). context(%s).",
		 hint_type, proxy_str_context (ctx_p));

      proxy_context_set_error (ctx_p, CAS_ERROR_INDICATOR, CAS_ER_INTERNAL);

      proxy_event_free (event_p);
      event_p = NULL;

      EXIT_FUNC ();
      return -1;
    }

  shard_id =
    proxy_get_shard_id (stmt_p, (void **) (argv + BIND_VALUE_INDEX),
			&range_p);

  PROXY_LOG (PROXY_LOG_MODE_SHARD_DETAIL, "Select shard. "
	     "(prev_shard_id:%d, curr_shard_id:%d). context(%s).",
	     ctx_p->shard_id, shard_id, proxy_str_context (ctx_p));

  /* check shard_id */
  if (shard_id == PROXY_INVALID_SHARD)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Invalid shard id. (shard_id:%d). context(%s).",
		 shard_id, proxy_str_context (ctx_p));

      proxy_context_set_error (ctx_p, CAS_ERROR_INDICATOR, CAS_ER_INTERNAL);

      proxy_event_free (event_p);
      event_p = NULL;

      EXIT_FUNC ();
      return -1;
    }

  if (ctx_p->shard_id != PROXY_INVALID_SHARD && ctx_p->shard_id != shard_id)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Shard id couldn't be changed in a transaction. "
		 "(prev_shard_id:%d, curr_shard_id:%d). context(%s).",
		 ctx_p->shard_id, shard_id, proxy_str_context (ctx_p));

      proxy_context_set_error (ctx_p, CAS_ERROR_INDICATOR, CAS_ER_INTERNAL);

      proxy_event_free (event_p);
      event_p = NULL;

      EXIT_FUNC ();
      return -1;
    }

  ctx_p->shard_id = shard_id;

  cas_io_p = proxy_cas_alloc_by_ctx (ctx_p->shard_id, ctx_p->cas_id,
				     ctx_p->cid, ctx_p->uid);
  if (cas_io_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to allocate CAS. context(%s).",
		 proxy_str_context (ctx_p));

      proxy_context_set_error (ctx_p, CAS_ERROR_INDICATOR, CAS_ER_INTERNAL);

      proxy_event_free (event_p);
      event_p = NULL;


      EXIT_FUNC ();
      return -1;
    }
  else if (cas_io_p == (T_CAS_IO *) SHARD_TEMPORARY_UNAVAILABLE)
    {
      /* waiting idle shard/cas */
      ctx_p->waiting_event = event_p;
      event_p = NULL;

      EXIT_FUNC ();
      return 0;
    }

  proxy_context_set_in_tran (ctx_p, cas_io_p->shard_id, cas_io_p->cas_id);

  /* 
   * find stored server handle id for this shard/cas, if exist, do execute 
   * with it. otherwise, do dummy prepare for exeucte to get server handle id.
   */
  cas_srv_h_id = shard_stmt_find_srv_h_id_for_shard_cas (srv_h_id,
							 cas_io_p->shard_id,
							 cas_io_p->cas_id);
  if (cas_srv_h_id < 0)		/* not prepared */
    {
      PROXY_LOG (PROXY_LOG_MODE_SHARD_DETAIL,
		 "Do prepare before execute. cas(%s). context(%s).",
		 proxy_str_cas_io (cas_io_p), proxy_str_context (ctx_p));

      /* make prepare request event */
      prepare_request = proxy_dup_msg (stmt_p->request_buffer);
      if (prepare_request == NULL)
	{
	  goto free_context;
	}
      length = get_msg_length (prepare_request);

      /* unset force_out_tran bitmask */
      proxy_unset_force_out_tran (request_p);

      new_event_p = proxy_event_new (PROXY_EVENT_IO_WRITE,
				     PROXY_EVENT_FROM_CAS);
      if (new_event_p == NULL)
	{
	  goto free_context;
	}
      proxy_event_set_buffer (new_event_p, prepare_request, length);

      ctx_p->is_prepare_for_execute = true;
      ctx_p->prepared_stmt = stmt_p;
      ctx_p->waiting_event = event_p;
      event_p = NULL;		/* DO NOT DELETE */

      /* __FOR_DEBUG */
      assert (ctx_p->prepared_stmt->status == SHARD_STMT_STATUS_COMPLETE);

      func_code = CAS_FC_PREPARE;

      goto relay_request;
    }
  else
    {
      ctx_p->waiting_event = proxy_event_dup (event_p);
      if (ctx_p->waiting_event == NULL)
	{
	  goto free_context;
	}

      net_arg_put_int (argv[0], &cas_srv_h_id);
    }

  ctx_p->stmt_hint_type = hint_type;
  ctx_p->stmt_h_id = srv_h_id;

  /* update shard statistics */
  proxy_update_shard_id_statistics (stmt_p, range_p);

  new_event_p = event_p;	// add comment  

relay_request:

  error = proxy_cas_io_write (cas_io_p, new_event_p);
  if (error)
    {
      goto free_context;
    }

  ctx_p->func_code = func_code;

  if (event_p && event_p != new_event_p)
    {
      proxy_event_free (event_p);
      event_p = NULL;
    }
  new_event_p = NULL;

  EXIT_FUNC ();
  return 0;

free_context:
  if (ctx_p->is_prepare_for_execute)
    {
      ctx_p->is_prepare_for_execute = false;
    }

  if (ctx_p->prepared_stmt)
    {
      ctx_p->prepared_stmt = NULL;
    }

  if (ctx_p->waiting_event)
    {
      ctx_p->waiting_event = NULL;
    }

  if (event_p == new_event_p)
    {
      proxy_event_free (event_p);
      event_p = new_event_p = NULL;
    }
  else
    {
      if (event_p)
	{
	  proxy_event_free (event_p);
	  event_p = NULL;
	}

      if (new_event_p)
	{
	  proxy_event_free (new_event_p);
	  new_event_p = NULL;
	}
    }

  ctx_p->free_context = true;

  EXIT_FUNC ();
  return -1;
}

int
fn_proxy_client_close_req_handle (T_PROXY_CONTEXT * ctx_p,
				  T_PROXY_EVENT * event_p, int argc,
				  char **argv)
{
  int error = 0;
  T_CAS_IO *cas_io_p;
  int srv_h_id;
  int cas_srv_h_id;

  const char func_code = CAS_FC_CLOSE_REQ_HANDLE;

  ENTER_FUNC ();

  net_arg_get_int (&srv_h_id, argv[0]);

  if (ctx_p->is_in_tran)
    {
      cas_io_p = proxy_cas_alloc_by_ctx (ctx_p->shard_id, ctx_p->cas_id,
					 ctx_p->cid, ctx_p->uid);
      if (cas_io_p == NULL)
	{
	  goto free_context;
	}
      else if (cas_io_p == (T_CAS_IO *) SHARD_TEMPORARY_UNAVAILABLE)
	{
	  assert (false);	/* it cannot happen */

	  goto free_context;
	}
      assert (ctx_p->shard_id == cas_io_p->shard_id);
      assert (ctx_p->cas_id == cas_io_p->cas_id);
      proxy_context_set_in_tran (ctx_p, cas_io_p->shard_id, cas_io_p->cas_id);

      cas_srv_h_id =
	shard_stmt_find_srv_h_id_for_shard_cas (srv_h_id,
						cas_io_p->shard_id,
						cas_io_p->cas_id);

      /* remove statement handle id for this cas */
      shard_stmt_del_srv_h_id_for_shard_cas (srv_h_id, ctx_p->shard_id,
					     ctx_p->cas_id);
      if (cas_srv_h_id >= 0)
	{
	  /* relay request to the CAS */
	  net_arg_put_int (argv[0], &cas_srv_h_id);

	  error = proxy_cas_io_write (cas_io_p, event_p);
	  if (error)
	    {
	      goto free_context;
	    }
	  event_p = NULL;

	  ctx_p->func_code = func_code;

	  EXIT_FUNC ();
	  return 0;
	}
    }

  /* send close_req_handle response to the client, 
   * if this context is not IN_TRAN, 
   * or context is IN_TRAN status but cas_srv_h_id is not found */
  error =
    proxy_send_response_to_client_with_new_event (ctx_p, PROXY_EVENT_IO_WRITE,
						  PROXY_EVENT_FROM_CLIENT,
						  proxy_io_make_close_req_handle_out_tran_ok);
  if (error)
    {
      goto free_context;
    }

  proxy_event_free (event_p);
  event_p = NULL;

  EXIT_FUNC ();
  return 0;

free_context:
  if (event_p)
    {
      proxy_event_free (event_p);
      event_p = NULL;
    }

  ctx_p->free_context = true;

  EXIT_FUNC ();
  return -1;
}

int
fn_proxy_client_fetch (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p,
		       int argc, char **argv)
{
  int error = 0;
  int length;
  int srv_h_id, cas_srv_h_id;

  T_CAS_IO *cas_io_p;
  T_SHARD_STMT *stmt_p;

  const char func_code = CAS_FC_FETCH;

  ENTER_FUNC ();

  net_arg_get_int (&srv_h_id, argv[0]);

  /* find idle cas */
  cas_io_p = proxy_cas_alloc_by_ctx (ctx_p->shard_id, ctx_p->cas_id,
				     ctx_p->cid, ctx_p->uid);
  if (cas_io_p == NULL)
    {
      goto free_context;
    }
  else if (cas_io_p == (T_CAS_IO *) SHARD_TEMPORARY_UNAVAILABLE)
    {
      assert (false);		/* it cannot happen */

      goto free_context;
    }
  assert (ctx_p->shard_id == cas_io_p->shard_id);
  assert (ctx_p->cas_id == cas_io_p->cas_id);
  proxy_context_set_in_tran (ctx_p, cas_io_p->shard_id, cas_io_p->cas_id);

  /* 
   * find stored server handle id for this shard/cas, if exist, do fetch 
   * with it. otherwise, returns proxy internal error to the client.
   */
  cas_srv_h_id = shard_stmt_find_srv_h_id_for_shard_cas (srv_h_id,
							 ctx_p->shard_id,
							 ctx_p->cas_id);
  if (cas_srv_h_id <= 0)
    {
      proxy_context_set_error (ctx_p, CAS_ERROR_INDICATOR, CAS_ER_SRV_HANDLE);

      proxy_event_free (event_p);
      event_p = NULL;

      EXIT_FUNC ();
      return -1;
    }

  net_arg_put_int (argv[0], &(cas_srv_h_id));

relay_request:

  error = proxy_cas_io_write (cas_io_p, event_p);
  if (error)
    {
      goto free_context;
    }

  /* in this case, we must not free event */
  event_p = NULL;

  ctx_p->func_code = func_code;

  EXIT_FUNC ();
  return 0;

free_context:

  if (event_p)
    {
      proxy_event_free (event_p);
      event_p = NULL;
    }

  ctx_p->free_context = true;

  EXIT_FUNC ();
  return -1;
}

int
fn_proxy_client_check_cas (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p,
			   int argc, char **argv)
{
  int error = 0;
  char *response_p;
  T_PROXY_EVENT *new_event_p = NULL;

  ENTER_FUNC ();

  new_event_p =
    proxy_event_new_with_rsp (PROXY_EVENT_IO_WRITE, PROXY_EVENT_FROM_CLIENT,
			      proxy_io_make_check_cas_ok);
  if (new_event_p == NULL)
    {
      goto free_context;
    }

  response_p = new_event_p->buffer.data;
  assert (response_p);		// __FOR_DEBUG

  if (ctx_p->is_client_in_tran)
    {
      proxy_set_con_status_in_tran (response_p);
    }

  error = proxy_send_response_to_client (ctx_p, new_event_p);
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to response to the client. "
		 "(error=%d). context(%s). evnet(%s).",
		 error, proxy_str_context (ctx_p),
		 proxy_str_event (new_event_p));

      goto free_context;
    }
  new_event_p = NULL;

  proxy_event_free (event_p);
  event_p = NULL;

  EXIT_FUNC ();
  return 0;

free_context:
  if (event_p)
    {
      proxy_event_free (event_p);
      event_p = NULL;
    }

  if (new_event_p)
    {
      proxy_event_free (new_event_p);
      new_event_p = NULL;
    }

  ctx_p->free_context = true;

  EXIT_FUNC ();
  return -1;
}

int
fn_proxy_client_con_close (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p,
			   int argc, char **argv)
{
  int error = 0;

  ENTER_FUNC ();

  /* set client tran status as OUT_TRAN, when con_close */
  ctx_p->is_client_in_tran = false;
  ctx_p->free_on_client_io_write = true;

  if (ctx_p->is_in_tran)
    {
      ctx_p->func_code = CAS_FC_END_TRAN;

      error = proxy_send_request_to_cas_with_new_event (ctx_p,
							PROXY_EVENT_IO_WRITE,
							PROXY_EVENT_FROM_CAS,
							proxy_io_make_end_tran_abort);
      if (error < 0)
	{
	  goto free_context;
	}
      else if (error > 0)
	{
	  assert (false);
	  goto free_context;
	}
    }
  else
    {
      error = proxy_send_response_to_client_with_new_event (ctx_p,
							    PROXY_EVENT_IO_WRITE,
							    PROXY_EVENT_FROM_CLIENT,
							    proxy_io_make_con_close_ok);
      if (error)
	{
	  goto free_context;
	}
    }

  /* free statement list */
  proxy_context_free_stmt (ctx_p);

  proxy_event_free (event_p);
  event_p = NULL;

  EXIT_FUNC ();
  return 0;

free_context:
  if (event_p)
    {
      proxy_event_free (event_p);
      event_p = NULL;
    }

  ctx_p->free_context = true;

  EXIT_FUNC ();
  return -1;
}

int
fn_proxy_client_get_db_version (T_PROXY_CONTEXT * ctx_p,
				T_PROXY_EVENT * event_p, int argc,
				char **argv)
{
  int error = 0;
  char *db_version = NULL;
  char auto_commit_mode;

  const char func_code = CAS_FC_GET_DB_VERSION;

  ENTER_FUNC ();

  if (argc < 1)
    {
      proxy_context_set_error (ctx_p, CAS_ERROR_INDICATOR, CAS_ER_ARGS);

      proxy_event_free (event_p);
      event_p = NULL;

      EXIT_FUNC ();
      return -1;
    }

  /* arg0 */
  net_arg_get_char (auto_commit_mode, argv[0]);


  error = proxy_send_response_to_client_with_new_event (ctx_p,
							PROXY_EVENT_IO_WRITE,
							PROXY_EVENT_FROM_CLIENT,
							proxy_io_make_get_db_version);
  if (error)
    {
      goto free_context;
    }

  proxy_event_free (event_p);
  event_p = NULL;

  EXIT_FUNC ();
  return 0;

free_context:
  if (event_p)
    {
      proxy_event_free (event_p);
      event_p = NULL;
    }

  ctx_p->free_context = true;

  EXIT_FUNC ();
  return -1;
}

int
fn_proxy_client_not_supported (T_PROXY_CONTEXT * ctx_p,
			       T_PROXY_EVENT * event_p, int argc, char **argv)
{
  proxy_context_set_error (ctx_p, CAS_ERROR_INDICATOR,
			   CAS_ER_NOT_IMPLEMENTED);

  ENTER_FUNC ();

  assert (event_p);
  if (event_p)
    {
      proxy_event_free (event_p);
      event_p = NULL;
    }

  EXIT_FUNC ();
  return 0;
}

/* 
 * process cas response 
 */
int
fn_proxy_cas_end_tran (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p)
{
  int error;

  ENTER_FUNC ();

  if (ctx_p->free_on_client_io_write)
    {
      PROXY_DEBUG_LOG ("Free context on client io write. "
		       "context will be terminated. context(%s).",
		       proxy_str_context (ctx_p));

      error = proxy_send_response_to_client_with_new_event (ctx_p,
							    PROXY_EVENT_IO_WRITE,
							    PROXY_EVENT_FROM_CLIENT,
							    proxy_io_make_con_close_ok);

      goto free_event;
    }
  else if (ctx_p->free_on_end_tran)
    {
      PROXY_DEBUG_LOG ("Free context on end tran. "
		       "context will be terminated. context(%s).",
		       proxy_str_context (ctx_p));

      ctx_p->free_context = true;

      error = 0 /* no error */ ;
      goto free_event;
    }

  error = proxy_send_response_to_client (ctx_p, event_p);
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to response to the client. "
		 "(error=%d). context(%s). evnet(%s).",
		 error, proxy_str_context (ctx_p), proxy_str_event (event_p));

      ctx_p->free_context = true;

      error = -1;
      goto free_event;
    }
  event_p = NULL;

  EXIT_FUNC ();
  return error;

free_event:
  if (event_p)
    {
      proxy_event_free (event_p);
      event_p = NULL;
    }

  EXIT_FUNC ();
  return error;
}

int
fn_proxy_cas_prepare (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p)
{
  /* SHARD TODO : REFACTOR */
  /* Protocol Phase 0 : MSG_HEADER */

  /* Protocol Phase 1 : GENERAL INFO */
  /* INT : SRV_H_ID */
  /* INT : RESULT_CACHE_LIFETIME */
  /* CHAR : STMT_TYPE */
  /* INT : NUM_MARKERS */
  /* CHAR : UPDATABLE_FLAG */

  /* Protocol Phase 2 : PREPARED COLUMN COUNT  */
  /* CASE OF SELECT */
  /* INT : NUM_COLS */
  /* CASE OF STMT_CALL, STMT_GET_STATS, STMT_EVALUATE */
  /* INT : NUM_COLS(1) */
  /* ELSE CASE */
  /* INT : NUM_COLS(0) */

  /* Protocol Phase 3 : PREPARED COLUMN INFO LIST (NUM_COLS) -- prepare_column_info_set */

  int error, error_ind, error_code;
  int srv_h_id;
  int stmt_h_id_n;
  int srv_h_id_offset = MSG_HEADER_SIZE;
  char *srv_h_id_pos;
  int length;
  char *prepare_reply;

  T_CLIENT_IO *cli_io_p;
  T_SHARD_STMT *stmt_p;

  char *response_p;

  ENTER_FUNC ();

  response_p = event_p->buffer.data;
  assert (response_p);		// __FOR_DEBUG

  if (ctx_p->is_client_in_tran)
    {
      proxy_set_con_status_in_tran (response_p);
    }

  if (ctx_p->waiting_event && ctx_p->is_prepare_for_execute == false)
    {
      proxy_event_free (ctx_p->waiting_event);
      ctx_p->waiting_event = NULL;
    }

  error_ind = proxy_check_cas_error (response_p);
  if (error_ind == CAS_ERROR_INDICATOR || error_ind == DBMS_ERROR_INDICATOR)
    {
      error_code = proxy_get_cas_error_code (response_p);

      PROXY_LOG (PROXY_LOG_MODE_NOTICE, "CAS response error. "
		 "(error_ind:%d, error_code:%d). context(%s).",
		 error_ind, error_code, proxy_str_context (ctx_p));
      /* relay error to the client */
      error = proxy_send_response_to_client (ctx_p, event_p);
      if (error)
	{
	  PROXY_LOG (PROXY_LOG_MODE_ERROR,
		     "Failed to response to the client. "
		     "(error=%d). context(%s). evnet(%s).", error,
		     proxy_str_context (ctx_p), proxy_str_event (event_p));

	  /* statement waiters may be woken up when freeing context */
	  goto free_context;
	}
      event_p = NULL;

      /* if stmt's status is SHARD_STMT_STATUS_IN_PROGRESS, 
       * wake up waiters and free statement.
       */
      stmt_p = ctx_p->prepared_stmt;
      /* __FOR_DEBUG */
      assert (stmt_p);

      if (stmt_p && stmt_p->status == SHARD_STMT_STATUS_IN_PROGRESS)
	{
	  /* check and wakeup statement waiter */
	  shard_stmt_check_waiter_and_wakeup (ctx_p->prepared_stmt);

	  /* 
	   * there must be no context sharing this statement at this time.
	   * so, we can free statement.
	   */
	  shard_stmt_free (ctx_p->prepared_stmt);
	}

      ctx_p->prepared_stmt = NULL;
      ctx_p->is_prepare_for_execute = false;

      EXIT_FUNC ();
      return 0;
    }

  stmt_p = ctx_p->prepared_stmt;
  if (stmt_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Invalid statement. Statement couldn't be NULL. context(%s).",
		 proxy_str_context (ctx_p));

      goto free_context;
    }

  /* save prepare response to the statement */
  if (stmt_p->reply_buffer == NULL)
    {
      stmt_p->reply_buffer = proxy_dup_msg (response_p);
      if (stmt_p->reply_buffer == NULL)
	{
	  /* statement waiters may be woken up when freeing context */
#if 0
	  ctx_p->prepared_stmt = NULL;
#endif
	  goto free_context;
	}
      stmt_p->reply_buffer_length = get_msg_length (response_p);

      /* modify stmt_h_id */
      stmt_h_id_n = htonl (stmt_p->stmt_h_id);
      memcpy (stmt_p->reply_buffer + srv_h_id_offset,
	      (char *) &stmt_h_id_n, NET_SIZE_INT);
    }

  /* save server handle id for this statement from shard/cas */
  srv_h_id = error_ind;
  error = shard_stmt_add_srv_h_id_for_shard_cas (stmt_p->stmt_h_id,
						 ctx_p->shard_id,
						 ctx_p->cas_id, srv_h_id);
  if (error < 0)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to save server handle "
		 "id to the statement "
		 "(srv_h_id:%d). statement(%s). context(%s). ",
		 srv_h_id, shard_str_stmt (stmt_p),
		 proxy_str_context (ctx_p));
      goto free_context;
    }

  if (ctx_p->is_prepare_for_execute)
    {
      /* 
       * after finishing prepare_for_execute, process previous client exeucte 
       * request again.
       */

      ctx_p->is_prepare_for_execute = false;

      assert (ctx_p->waiting_event);	// __FOR_DEBUG
      error = shard_queue_enqueue (&proxy_Handler.cli_ret_q,
				   (void *) ctx_p->waiting_event);

      if (error)
	{
	  goto free_context;
	}

      ctx_p->waiting_event = NULL;	/* DO NOT DELETE */

      /* do not relay response to the client, free event here */
      proxy_event_free (event_p);
      event_p = NULL;

      EXIT_FUNC ();
      return 0;
    }

  ctx_p->prepared_stmt = NULL;

  /* REPLACE SRV_H_ID */
  srv_h_id_pos = (char *) (response_p + srv_h_id_offset);
  memcpy (srv_h_id_pos, &stmt_h_id_n, NET_SIZE_INT);

  PROXY_LOG (PROXY_LOG_MODE_SHARD_DETAIL,
	     "Replace server handle id. "
	     "(requested_srv_h_id:%d, saved_srv_h_id:%d).", srv_h_id,
	     stmt_p->stmt_h_id);

  error = proxy_send_response_to_client (ctx_p, event_p);
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to response to the client. "
		 "(error=%d). context(%s). evnet(%s).",
		 error, proxy_str_context (ctx_p), proxy_str_event (event_p));

      goto free_context;
    }

  EXIT_FUNC ();
  return 0;

free_context:
  if (event_p)
    {
      proxy_event_free (event_p);
      event_p = NULL;
    }

  ctx_p->free_context = true;

  EXIT_FUNC ();
  return -1;
}

int
fn_proxy_cas_execute (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p)
{
  /* Protocol Phase 0 : MSG_HEADER */

  /* Protocol Phase 1 : GENERAL INFO */
  /* INT : ROW_COUNT */
  /* CHAR : CLT_CACHE_REUSABLE(1 or 0) */
  /* INT : NUM_QUERY_RESULT */

  /* Protocol Phase 2 : QUERY RESULT(NUM_QEURY_RESULT) */
  /* CHAR : STMT_TYPE */
  /* INT : TUPLE_COUNT */
  /* T_OBJECT : INS_OID */
  /* INT : SRV_CACHE_TIME.SEC */
  /* INT : SRV_CACHE_TIME.USEC */

  int error;
  int error_ind;
  bool is_in_tran;
  char *response_p;

  ENTER_FUNC ();

  response_p = event_p->buffer.data;
  assert (response_p);		// __FOR_DEBUG

  if (ctx_p->waiting_event)
    {
      proxy_event_free (ctx_p->waiting_event);
      ctx_p->waiting_event = NULL;
    }

  is_in_tran = proxy_handler_is_cas_in_tran (ctx_p->shard_id, ctx_p->cas_id);
  if (is_in_tran == false)
    {
      ctx_p->is_client_in_tran = false;
    }

  error_ind = proxy_check_cas_error (response_p);

  if (error_ind == CAS_ERROR_INDICATOR || error_ind == DBMS_ERROR_INDICATOR)
    {
      PROXY_LOG (PROXY_LOG_MODE_NOTICE,
		 "CAS response error. (error_ind:%d). context(%s).",
		 error_ind, proxy_str_context (ctx_p));

      assert (ctx_p->stmt_h_id >= 0);

      shard_stmt_del_srv_h_id_for_shard_cas (ctx_p->stmt_h_id,
					     ctx_p->shard_id, ctx_p->cas_id);
    }

  switch (ctx_p->stmt_hint_type)
    {
    case HT_NONE:
      proxy_info_p->num_hint_none_queries_processed++;
      break;
    case HT_KEY:
      proxy_info_p->num_hint_key_queries_processed++;
      break;
    case HT_ID:
      proxy_info_p->num_hint_id_queries_processed++;
      break;
    case HT_ALL:
      proxy_info_p->num_hint_all_queries_processed++;
      break;
    default:
      PROXY_LOG (PROXY_LOG_MODE_NOTICE, "Unsupported statement hint type. "
		 "(hint_type:%d). context(%s).",
		 ctx_p->stmt_hint_type, proxy_str_context (ctx_p));
    }

  ctx_p->stmt_hint_type = HT_INVAL;
  ctx_p->stmt_h_id = SHARD_STMT_INVALID_HANDLE_ID;

  error = proxy_send_response_to_client (ctx_p, event_p);
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to response to the client. "
		 "(error=%d). context(%s). evnet(%s).",
		 error, proxy_str_context (ctx_p), proxy_str_event (event_p));

      goto free_context;
    }
  event_p = NULL;

  EXIT_FUNC ();
  return 0;

free_context:
  if (event_p)
    {
      proxy_event_free (event_p);
      event_p = NULL;
    }

  ctx_p->free_context = true;

  EXIT_FUNC ();
  return -1;
}

int
fn_proxy_cas_fetch (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p)
{
  int error;
  bool is_in_tran;

  ENTER_FUNC ();

  is_in_tran = proxy_handler_is_cas_in_tran (ctx_p->shard_id, ctx_p->cas_id);
  if (is_in_tran == false)
    {
      ctx_p->is_client_in_tran = false;
    }

  error = proxy_send_response_to_client (ctx_p, event_p);
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to response to the client. "
		 "(error=%d). context(%s). evnet(%s).",
		 error, proxy_str_context (ctx_p), proxy_str_event (event_p));

      goto free_context;
    }
  event_p = NULL;

  EXIT_FUNC ();
  return 0;

free_context:
  if (event_p)
    {
      proxy_event_free (event_p);
      event_p = NULL;
    }

  ctx_p->free_context = true;

  EXIT_FUNC ();
  return -1;
}

int
fn_proxy_cas_relay_only (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p)
{
  int error;

  ENTER_FUNC ();

  error = proxy_send_response_to_client (ctx_p, event_p);
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to response to the client. "
		 "(error=%d). context(%s). evnet(%s).",
		 error, proxy_str_context (ctx_p), proxy_str_event (event_p));

      goto free_context;
    }
  event_p = NULL;

  EXIT_FUNC ();
  return 0;

free_context:
  if (event_p)
    {
      proxy_event_free (event_p);
      event_p = NULL;
    }

  ctx_p->free_context = true;

  EXIT_FUNC ();
  return -1;
}

/* disconnect event */
int
fn_proxy_client_conn_error (T_PROXY_CONTEXT * ctx_p)
{
  int error = 0;

  ENTER_FUNC ();

  /* 
   * we need not send abort while waiting for a response.
   */
  if (ctx_p->is_in_tran && !IS_VALID_CAS_FC (ctx_p->func_code))
    {
      ctx_p->free_on_end_tran = true;
      ctx_p->func_code = CAS_FC_END_TRAN;

      error = proxy_send_request_to_cas_with_new_event (ctx_p,
							PROXY_EVENT_IO_WRITE,
							PROXY_EVENT_FROM_CAS,
							proxy_io_make_end_tran_abort);
      if (error < 0)
	{
	  error = -1;
	  goto free_context;
	}
      else if (error > 0)
	{
	  assert (false);
	  error = -1;
	  goto free_context;
	}

      EXIT_FUNC ();
      return 0;
    }

free_context:
  ctx_p->free_context = true;

  EXIT_FUNC ();
  return -1;
}

int
fn_proxy_cas_conn_error (T_PROXY_CONTEXT * ctx_p)
{
  /* nothing to do */
  return 0;
}

int
proxy_find_shard_id_by_hint_value (SP_VALUE * value_p, const char *key_column)
{
  T_SHARD_KEY_RANGE *range_p = NULL;

  int shard_key_id = -1;
  int shard_key_val_int;
  char *shard_key_val_string;
  int shard_key_val_len;

  if (value_p->type == VT_INTEGER)
    {
      shard_key_val_int = value_p->integer;
      shard_key_id =
	(*fn_get_shard_key) (key_column, SHARD_U_TYPE_INT,
			     &shard_key_val_int, sizeof (int));
    }
  else if (value_p->type == VT_STRING)
    {
      shard_key_val_string = value_p->string.value;
      shard_key_val_len = value_p->string.length;
      shard_key_id =
	(*fn_get_shard_key) (key_column, SHARD_U_TYPE_STRING,
			     shard_key_val_string, shard_key_val_len);
    }
  else
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Invalid hint value type. (value_type:%d).", value_p->type);
    }
  return shard_key_id;
}

/* wakeup event */
