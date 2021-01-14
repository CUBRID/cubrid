/*
 * Copyright 2008 Search Solution Corporation
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
 * shard_proxy_function.c -
 *
 */

#ident "$Id$"


#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <math.h>

#include "porting.h"
#include "shard_proxy.h"
#include "shard_proxy_handler.h"
#include "shard_proxy_function.h"
#include "shard_statement.h"
#include "shard_parser.h"
#include "shard_key_func.h"
#include "system_parameter.h"
#include "dbtype.h"

extern T_SHM_SHARD_KEY *shm_key_p;
extern T_PROXY_INFO *proxy_info_p;
extern T_PROXY_HANDLER proxy_Handler;
extern T_PROXY_CONTEXT proxy_Context;

extern int make_net_buf (T_NET_BUF * net_buf, int size);
extern int make_header_info (T_NET_BUF * net_buf, MSG_HEADER * client_msg_header);
static void proxy_set_wait_timeout (T_PROXY_CONTEXT * ctx_p, int query_timeout);

static int proxy_get_shard_id (T_SHARD_STMT * stmt_p, void **argv, T_SHARD_KEY_RANGE ** range_p_out);
static T_SHARD_KEY_RANGE *proxy_get_range_by_param (SP_PARSER_HINT * hint_p, void **argv);
static void proxy_update_shard_stats (T_SHARD_STMT * stmt_p, T_SHARD_KEY_RANGE * range_p);
static void proxy_update_shard_stats_without_hint (int shard_id);
static int proxy_client_execute_internal (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p, int argc, char **argv,
					  char _func_code, int query_timeout, int bind_value_index);
static int proxy_cas_execute_internal (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p);
static bool proxy_is_invalid_statement (int error_ind, int error_code, char *driver_info,
					T_BROKER_VERSION client_version);
static bool proxy_has_different_column_info (const char *r1, size_t r1_len, const char *r2, size_t r2_len);


void
proxy_set_wait_timeout (T_PROXY_CONTEXT * ctx_p, int query_timeout)
{
  int proxy_wait_timeout_sec;
  int query_timeout_sec;

  query_timeout_sec = ceil ((double) query_timeout / 1000);
  proxy_wait_timeout_sec = ctx_p->wait_timeout;

  if (proxy_wait_timeout_sec == 0 || query_timeout_sec == 0)
    {
      ctx_p->wait_timeout = proxy_wait_timeout_sec + query_timeout_sec;
    }
  else if (proxy_wait_timeout_sec < query_timeout_sec)
    {
      ctx_p->wait_timeout = proxy_wait_timeout_sec;
    }
  else
    {
      ctx_p->wait_timeout = query_timeout_sec;
    }

  return;
}

int
proxy_check_cas_error (char *read_msg)
{
  char *data;

  /* error_ind is ... old < R8.3.0 : err_no or svr_h_id R8.3.0 ~ current : error_indicator or srv_h_id */
  int error_ind;

  data = read_msg + MSG_HEADER_SIZE;
  error_ind = (int) ntohl (*(int *) data);

  return error_ind;
}

int
proxy_get_cas_error_code (char *read_msg, T_BROKER_VERSION client_version)
{
  char *data;
  int error_code;

  if (client_version >= CAS_MAKE_VER (8, 3, 0))
    {
      data = read_msg + MSG_HEADER_SIZE + sizeof (int) /* error indicator */ ;
    }
  else
    {
      data = read_msg + MSG_HEADER_SIZE;
    }
  error_code = (int) ntohl (*(int *) data);

  return error_code;
}

int
proxy_send_request_to_cas (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p, int func_code)
{
  int error = 0;
  T_CAS_IO *cas_io_p;

  ENTER_FUNC ();

  cas_io_p =
    proxy_cas_alloc_by_ctx (ctx_p->client_id, ctx_p->shard_id, ctx_p->cas_id, ctx_p->cid, ctx_p->uid,
			    ctx_p->wait_timeout, func_code);
  if (cas_io_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Failed to allocate CAS. " "(shard_id:%d, cas_id:%d, context id:%d, context uid:%d).", ctx_p->shard_id,
		 ctx_p->cas_id, ctx_p->cid, ctx_p->uid);
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
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to write to CAS. " "CAS(%s). event(%s).", proxy_str_cas_io (cas_io_p),
		 proxy_str_event (event_p));
      EXIT_FUNC ();
      return -1;
    }
  event_p = NULL;

  EXIT_FUNC ();
  return 0;
}

static int
proxy_send_request_to_cas_with_new_event (T_PROXY_CONTEXT * ctx_p, unsigned int type, int from,
					  T_PROXY_EVENT_FUNC req_func)
{
  int error = 0;
  T_PROXY_EVENT *event_p = NULL;
  char *driver_info;

  driver_info = proxy_get_driver_info_by_ctx (ctx_p);

  event_p = proxy_event_new_with_req (driver_info, type, from, req_func);
  if (event_p == NULL)
    {
      error = -1;
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to make request event. (error:%d). context(%s).", error,
		 proxy_str_context (ctx_p));

      goto error_return;
    }

  error = proxy_send_request_to_cas (ctx_p, event_p, ctx_p->func_code);
  if (error < 0)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to send request to CAS. " "(error=%d). context(%s). evnet(%s).", error,
		 proxy_str_context (ctx_p), proxy_str_event (event_p));
      goto error_return;
    }
  else if (error > 0)		/* TODO : DELETE */
    {
      goto error_return;
    }

  event_p = NULL;

  return error;

error_return:
  if (event_p != NULL)
    {
      proxy_event_free (event_p);
      event_p = NULL;
    }
  return error;
}

#if defined(ENABLE_UNUSED_FUNCTION)
static int
proxy_send_request_to_cas_with_stored_server_handle_id (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p, int argc,
							char **argv)
{
  int error = 0;
  int srv_h_id, cas_srv_h_id;

  T_CAS_IO *cas_io_p;

  ENTER_FUNC ();

  /* find idle cas */
  cas_io_p = proxy_cas_alloc_by_ctx (ctx_p->shard_id, ctx_p->cas_id, ctx_p->cid, ctx_p->uid, ctx_p->wait_timeout);
  if (cas_io_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Failed to allocate CAS. " "(shard_id:%d, cas_id:%d, context id:%d, context uid:%d).", ctx_p->shard_id,
		 ctx_p->cas_id, ctx_p->cid, ctx_p->uid);

      goto free_context;
    }
  else if (cas_io_p == (T_CAS_IO *) SHARD_TEMPORARY_UNAVAILABLE)
    {
      assert (ctx_p->shard_id < 0 || ctx_p->cas_id < 0);

      goto free_context;
    }

  assert (ctx_p->shard_id == cas_io_p->shard_id);
  assert (ctx_p->cas_id == cas_io_p->cas_id);

  if (ctx_p->is_in_tran == false)
    {
      proxy_context_set_in_tran (ctx_p, cas_io_p->shard_id, cas_io_p->cas_id);
    }

  /* find stored server handle id for this shard/cas */
  net_arg_get_int (&srv_h_id, argv[0]);

  cas_srv_h_id = shard_stmt_find_srv_h_id_for_shard_cas (srv_h_id, ctx_p->shard_id, ctx_p->cas_id);

  if (cas_srv_h_id <= 0)
    {
      proxy_context_set_error (ctx_p, CAS_ERROR_INDICATOR, CAS_ER_SRV_HANDLE);

      goto free_context;
    }

  net_arg_put_int (argv[0], &(cas_srv_h_id));


  error = proxy_cas_io_write (cas_io_p, event_p);
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to write to CAS. " "CAS(%s). event(%s).", proxy_str_cas_io (cas_io_p),
		 proxy_str_event (event_p));

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
#endif /* ENABLE_UNUSED_FUNCTION */

int
proxy_send_response_to_client (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p)
{
  int error = 0;
  T_CLIENT_IO *cli_io_p;

  ENTER_FUNC ();

  /* find client io */
  cli_io_p = proxy_client_io_find_by_ctx (ctx_p->client_id, ctx_p->cid, ctx_p->uid);
  if (cli_io_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to find client. " "(client_id:%d, context id:%d, context uid:%u).",
		 ctx_p->client_id, ctx_p->cid, ctx_p->uid);

      EXIT_FUNC ();
      return -1;
    }

  error = proxy_client_io_write (cli_io_p, event_p);
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to write to client. " "client(%s). event(%s).",
		 proxy_str_client_io (cli_io_p), proxy_str_event (event_p));
      EXIT_FUNC ();
      return -1;
    }

  EXIT_FUNC ();
  return 0;
}

int
proxy_send_response_to_client_with_new_event (T_PROXY_CONTEXT * ctx_p, unsigned int type, int from,
					      T_PROXY_EVENT_FUNC resp_func)
{
  int error = 0;
  char *driver_info;
  T_PROXY_EVENT *event_p = NULL;
  T_CLIENT_INFO *client_info_p = NULL;

  client_info_p = shard_shm_get_client_info (proxy_info_p, ctx_p->client_id);
  if (client_info_p != NULL)
    {
      client_info_p->res_time = time (NULL);
    }

  driver_info = proxy_get_driver_info_by_ctx (ctx_p);

  event_p = proxy_event_new_with_rsp (driver_info, type, from, resp_func);
  if (event_p == NULL)
    {
      error = -1;
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to make response event. (error:%d). context(%s).", error,
		 proxy_str_context (ctx_p));

      goto error_return;
    }

  error = proxy_send_response_to_client (ctx_p, event_p);
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to response to the client. " "(error=%d). context(%s). evnet(%s).",
		 error, proxy_str_context (ctx_p), proxy_str_event (event_p));

      goto error_return;
    }

  event_p = NULL;

  return error;

error_return:
  if (event_p != NULL)
    {
      proxy_event_free (event_p);
      event_p = NULL;
    }
  return -1;
}

static int
proxy_send_prepared_stmt_to_client (T_PROXY_CONTEXT * ctx_p, T_SHARD_STMT * stmt_p)
{
  int error;
  int length;
  char *prepare_resp = NULL;
  T_PROXY_EVENT *event_p = NULL;

  prepare_resp = proxy_dup_msg ((char *) stmt_p->reply_buffer);
  if (prepare_resp == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Not enough virtual memory. " "failed to duplicate prepare request. ");

      EXIT_FUNC ();
      return -1;
    }

  event_p = proxy_event_new (PROXY_EVENT_IO_WRITE, PROXY_EVENT_FROM_CLIENT);
  if (event_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to make event. " "(%s, %s). context(%s).", "PROXY_EVENT_IO_WRITE",
		 "PROXY_EVENT_FROM_CLIENT", proxy_str_context (ctx_p));

      goto error_return;
    }

  length = get_msg_length (prepare_resp);
  proxy_event_set_buffer (event_p, prepare_resp, length);
  prepare_resp = NULL;

  error = proxy_send_response_to_client (ctx_p, event_p);
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to send response " "to the client. (error:%d). context(%s). event(%s). ",
		 error, proxy_str_context (ctx_p), proxy_str_event (event_p));

      goto error_return;
    }
  event_p = NULL;

  EXIT_FUNC ();
  return 0;

error_return:
  if (prepare_resp != NULL)
    {
      FREE_MEM (prepare_resp);
    }

  if (event_p != NULL)
    {
      proxy_event_free (event_p);
      event_p = NULL;
    }
  EXIT_FUNC ();
  return -1;
}

static void
proxy_update_shard_stats (T_SHARD_STMT * stmt_p, T_SHARD_KEY_RANGE * range_p)
{
  SP_PARSER_HINT *first_hint_p;

  T_SHM_SHARD_KEY_STAT *key_stat_p;
  T_SHM_SHARD_CONN_STAT *shard_stat_p;

  int shard_id, key_index, range_index;

  first_hint_p = sp_get_first_hint (stmt_p->parser);
  if (first_hint_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to update stats. No hint available. ");
      return;
    }

  switch (first_hint_p->hint_type)
    {
    case HT_KEY:
      if (range_p == NULL)
	{
	  PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to update stats. Invalid hint key range. (hint_type:%s).", "HT_KEY");
	  assert (range_p);
	  return;
	}
      shard_id = range_p->shard_id;

      shard_stat_p = shard_shm_get_shard_stat (proxy_info_p, shard_id);
      if (shard_stat_p == NULL)
	{
	  PROXY_LOG (PROXY_LOG_MODE_ERROR,
		     "Failed to update stats. " "Invalid shm shard stats. (hint_type:%s, proxy_id:%d, shard_id:%d).",
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
	  PROXY_LOG (PROXY_LOG_MODE_ERROR,
		     "Failed to update stats. "
		     "Invalid shm shard key stats. (hint_type:%s, proxy_id:%d, key_index:%d).", "HT_KEY",
		     proxy_info_p->proxy_id, key_index);

	  assert (key_stat_p);
	  return;
	}

      key_stat_p->stat[range_index].num_range_queries_requested++;
      return;

    case HT_ID:
      assert (first_hint_p->arg.type == VT_INTEGER);
      if (first_hint_p->arg.type == VT_INTEGER)
	{
	  INT64 integer = first_hint_p->arg.integer;
	  if (integer < 0 || integer >= MAX_SHARD_CONN)
	    {
	      shard_id = PROXY_INVALID_SHARD;
	    }
	  else
	    {
	      shard_id = (int) integer;
	    }

	  if (proxy_info_p->num_shard_conn <= shard_id)
	    {
	      PROXY_LOG (PROXY_LOG_MODE_ERROR,
			 "Failed to update stats. " "Invalid shard id. (hint_type:%s, shard_id:%d).", "HT_ID",
			 shard_id);
	      return;
	    }

	  shard_stat_p = shard_shm_get_shard_stat (proxy_info_p, shard_id);
	  if (shard_stat_p == NULL)
	    {
	      PROXY_LOG (PROXY_LOG_MODE_ERROR,
			 "Failed to update stats. "
			 "Invalid shm shard stats. (hint_type:%s, proxy_id:%d, shard_id:%d).", "HT_ID",
			 proxy_info_p->proxy_id, shard_id);

	      assert (shard_stat_p);
	      return;
	    }

	  shard_stat_p->num_hint_id_queries_requested++;
	  return;
	}
      else
	{
	  PROXY_LOG (PROXY_LOG_MODE_ERROR, "Unsupported hint value type. (hint_value_type:%d).",
		     first_hint_p->arg.type);

	  return;
	}
      break;

    default:
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Unsupported hint type. (hint_type:%d).", first_hint_p->hint_type);

      assert (false);
      break;
    }

  return;
}

static void
proxy_update_shard_stats_without_hint (int shard_id)
{
  T_SHM_SHARD_CONN_STAT *shard_stat_p;

  assert (shard_id >= 0);
  if (shard_id < 0)
    {
      return;
    }

  shard_stat_p = shard_shm_get_shard_stat (proxy_info_p, shard_id);
  if (shard_stat_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to update stats. (shard_id:%d).", shard_id);

      assert (shard_stat_p);
      return;
    }

  shard_stat_p->num_no_hint_queries_requested++;

  return;
}

static int
proxy_get_shard_id (T_SHARD_STMT * stmt_p, void **argv, T_SHARD_KEY_RANGE ** range_p_out)
{
  int compare_flag = 0;
  int shard_id = -1, next_shard_id = -1;
  SP_PARSER_HINT *hint_p;
  T_SHARD_KEY_RANGE *range_p = NULL;

  *range_p_out = NULL;

  hint_p = sp_get_first_hint (stmt_p->parser);
  if (hint_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Unable to get shard id. No hint available.");
      proxy_info_p->num_hint_err_queries_processed++;

      return PROXY_INVALID_SHARD;
    }

  for (; hint_p; hint_p = sp_get_next_hint (hint_p))
    {
      switch (hint_p->hint_type)
	{
	case HT_KEY:
	  range_p = proxy_get_range_by_param (hint_p, argv);
	  if (range_p == NULL)
	    {
	      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Unable to get shard id. Invalid hint key range. (hint_type:%s).",
			 "HT_KEY");
	      return PROXY_INVALID_SHARD;
	    }
	  assert (range_p->shard_id >= 0);
	  if (shard_id < 0)
	    {
	      shard_id = range_p->shard_id;
	    }
	  else
	    {
	      next_shard_id = range_p->shard_id;
	      compare_flag = 1;
	    }
	  break;

	case HT_ID:
	  if (hint_p->arg.type == VT_INTEGER)
	    {
	      INT64 integer = hint_p->arg.integer;
	      if (integer < 0 || integer >= MAX_SHARD_CONN)
		{
		  PROXY_LOG (PROXY_LOG_MODE_ERROR, "Unable to get shard id. Invalid hint id. (hint_type:%s).", "HT_ID");
		  return PROXY_INVALID_SHARD;
		}

	      if (shard_id < 0)
		{
		  shard_id = (int) integer;
		}
	      else
		{
		  next_shard_id = (int) integer;
		  compare_flag = 1;
		}
	    }
	  else
	    {
	      PROXY_LOG (PROXY_LOG_MODE_ERROR,
			 "Unable to get shard id. shard id is not integer type. (hint_type:%s, type:%d).", "HT_ID",
			 hint_p->arg.type);

	      return PROXY_INVALID_SHARD;
	    }
	  break;

	default:

	  PROXY_LOG (PROXY_LOG_MODE_ERROR, "Unsupported hint type. (hint_type:%d).", hint_p->hint_type);
	  return PROXY_INVALID_SHARD;
	}

      if (compare_flag > 0 && shard_id != next_shard_id)
	{
	  PROXY_LOG (PROXY_LOG_MODE_ERROR, "Shard id is different. " "(first_shard_id:%d, next_shard_id:%d). ",
		     shard_id, next_shard_id);
	  return PROXY_INVALID_SHARD;
	}

    }

  if (range_p != NULL)
    {
      *range_p_out = range_p;
    }

  return shard_id;
}

static T_SHARD_KEY_RANGE *
proxy_get_range_by_param (SP_PARSER_HINT * hint_p, void **argv)
{
  int hint_position, num_bind;
  int type_idx, val_idx;

  char type;
  int data_size;
  void *net_type, *net_value;

  T_SHARD_KEY *key_p;
  T_SHARD_KEY_RANGE *range_p = NULL;

  int shard_key_id = 0;
  const char *key_column;

  /* Phase 0 : hint position get */
  /* SHARD TODO : find statement entry, and param position & etc */
  /* SHARD TODO : multiple key_value */

  assert (shm_key_p->num_shard_key == 1);
  if (shm_key_p->num_shard_key != 1)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Too may shard key column in config. " "(num_shard_key:%d).",
		 shm_key_p->num_shard_key);
      return NULL;
    }

  key_p = (T_SHARD_KEY *) (&(shm_key_p->shard_key[0]));
  key_column = key_p->key_column;

  switch (hint_p->bind_type)
    {
    case BT_STATIC:
      shard_key_id = proxy_find_shard_id_by_hint_value (&hint_p->value, key_column);
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
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Unsupported hint bind type. (bind_type:%d).", hint_p->bind_type);
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
	case CCI_U_TYPE_UINT:
	  {
	    int i_val;
	    net_arg_get_size (&data_size, net_value);
	    if (data_size <= 0)
	      {
		PROXY_LOG (PROXY_LOG_MODE_ERROR, "Unexpected integer hint value size." "(size:%d).", data_size);
		return NULL;
	      }

	    net_arg_get_int (&i_val, net_value);
	    shard_key_id = (*fn_get_shard_key) (key_column, (T_SHARD_U_TYPE) type, &i_val, sizeof (int));
	  }
	  break;
	case CCI_U_TYPE_SHORT:
	case CCI_U_TYPE_USHORT:
	  {
	    short s_val;
	    net_arg_get_size (&data_size, net_value);
	    if (data_size <= 0)
	      {
		PROXY_LOG (PROXY_LOG_MODE_ERROR, "Unexpected short hint value size." "(size:%d).", data_size);
		return NULL;
	      }

	    net_arg_get_short (&s_val, net_value);
	    shard_key_id = (*fn_get_shard_key) (key_column, (T_SHARD_U_TYPE) type, &s_val, sizeof (short));

	  }
	  break;
	case CCI_U_TYPE_BIGINT:
	case CCI_U_TYPE_UBIGINT:
	  {
	    INT64 l_val;
	    net_arg_get_size (&data_size, net_value);
	    if (data_size <= 0)
	      {
		PROXY_LOG (PROXY_LOG_MODE_ERROR, "Unexpected big integer hint value size." "(size:%d).", data_size);
		return NULL;
	      }

	    net_arg_get_bigint (&l_val, net_value);
	    shard_key_id = (*fn_get_shard_key) (key_column, (T_SHARD_U_TYPE) type, &l_val, sizeof (INT64));

	  }
	  break;
	case CCI_U_TYPE_ENUM:
	case CCI_U_TYPE_STRING:
	  {
	    char *s_val;
	    int s_len;
	    net_arg_get_str (&s_val, &s_len, net_value);
	    if (s_val == NULL || s_len == 0)
	      {
		PROXY_LOG (PROXY_LOG_MODE_ERROR, "Invalid string hint values. (len:%d).", s_len);
		return NULL;
	      }
	    shard_key_id = (*fn_get_shard_key) (key_column, SHARD_U_TYPE_STRING, s_val, s_len);
	  }
	  break;

	  /* SHARD TODO : support other hint data types */

	default:
	  PROXY_LOG (PROXY_LOG_MODE_ERROR, "Unsupported hint value type. (type:%d).", type);

	  return NULL;

	}
    }

  if (shard_key_id < 0)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to get shard key id. " "(shard_key_id:%d).", shard_key_id);
      return NULL;
    }

  /* Phase 2 : Shard range get */
  range_p = shard_metadata_find_shard_range (shm_key_p, (char *) key_column, shard_key_id);
  if (range_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Unable to find shm shard range. (key:%s, key_id:%d).", key_column,
		 shard_key_id);
      return NULL;
    }

  PROXY_LOG (PROXY_LOG_MODE_SHARD_DETAIL, "Select shard. (shard_id:%d, key_column:[%s], shard_key_id:%d).",
	     range_p->shard_id, key_column, shard_key_id);

  return range_p;
}

/*
 * request event
 */
int
fn_proxy_client_end_tran (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p, int argc, char **argv)
{
  int error = 0;
  const char func_code = CAS_FC_END_TRAN;

  ENTER_FUNC ();

  /* set client tran status as OUT_TRAN, when end_tran */
  ctx_p->is_client_in_tran = false;

  if (ctx_p->is_in_tran)
    {
      error = proxy_send_request_to_cas (ctx_p, event_p, func_code);
      if (error < 0)
	{
	  PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to send request to CAS. " "(error=%d). context(%s). evnet(%s).",
		     error, proxy_str_context (ctx_p), proxy_str_event (event_p));

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
      error =
	proxy_send_response_to_client_with_new_event (ctx_p, PROXY_EVENT_IO_WRITE, PROXY_EVENT_FROM_CLIENT,
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
fn_proxy_client_prepare (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p, int argc, char **argv)
{
  int error = 0;
  int i;

  /* sql statement */
  char *sql_stmt;
  char *organized_sql_stmt = NULL;
  int sql_size;

  /* argv */
  char flag;
  char auto_commit_mode;

  /* io/statement entries */
  T_CAS_IO *cas_io_p;
  T_SHARD_STMT *stmt_p;
  T_WAIT_CONTEXT *waiter_p;

  int shard_id;
  T_SHARD_KEY_RANGE *dummy_range_p = NULL;

  char *driver_info;
  T_BROKER_VERSION client_version;

  char *request_p;

  bool has_shard_val_hint = false;
  bool use_temp_statement = false;

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
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Invalid argument. (argc:%d). context(%s).", argc, proxy_str_context (ctx_p));
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

  PROXY_DEBUG_LOG ("Process requested prepare sql statement. " "(sql_stmt:[%s]). context(%s).",
		   shard_str_sqls (sql_stmt), proxy_str_context (ctx_p));

  organized_sql_stmt = shard_stmt_rewrite_sql (&has_shard_val_hint, sql_stmt, proxy_info_p->appl_server);

  if (organized_sql_stmt == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to rewrite sql statement. (sql_stmt:[%s]).", sql_stmt);
      proxy_context_set_error (ctx_p, CAS_ERROR_INDICATOR, CAS_ER_ARGS);

      proxy_event_free (event_p);
      event_p = NULL;

      error = -1;
      goto end;
    }
  PROXY_DEBUG_LOG ("Rewrite sql statement. " "(organized_sql_stmt:[%s]). context(%s).",
		   shard_str_sqls (organized_sql_stmt), proxy_str_context (ctx_p));

  driver_info = proxy_get_driver_info_by_ctx (ctx_p);
  client_version = CAS_MAKE_PROTO_VER (driver_info);

  if (ctx_p->is_prepare_for_execute == false)
    {
      proxy_info_p->num_request_stmt++;
    }

  stmt_p = shard_stmt_find_by_sql (organized_sql_stmt, ctx_p->database_user, client_version);
  if (stmt_p)
    {
      if (ctx_p->is_prepare_for_execute == false)
	{
	  proxy_info_p->num_request_stmt_in_pool++;
	}

      PROXY_DEBUG_LOG ("success to find statement. (stmt:%s).", shard_str_stmt (stmt_p));

      switch (stmt_p->status)
	{
	case SHARD_STMT_STATUS_COMPLETE:
	  /**
	   * this statement has been used previously. if we use it,
	   * we will get the corrupted result of statement.
	   */
	  if (proxy_context_find_stmt (ctx_p, stmt_p->stmt_h_id) != NULL)
	    {
	      use_temp_statement = true;
	      break;
	    }

	  error = proxy_send_prepared_stmt_to_client (ctx_p, stmt_p);
	  if (error)
	    {
	      PROXY_LOG (PROXY_LOG_MODE_ERROR,
			 "Failed to send prepared statment to client. " "(error:%d). context(%s).", error,
			 proxy_str_context (ctx_p));
	      goto free_context;
	    }

	  /* save statement to the context */
	  if (proxy_context_add_stmt (ctx_p, stmt_p) == NULL)
	    {
	      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to add statement to context. " "statement(%s). context(%s).",
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

	  /* if this context was woken up by shard/cas resource and try dummy prepare again */
	  if (stmt_p->ctx_cid == ctx_p->cid && stmt_p->ctx_uid == ctx_p->uid)
	    {
	      assert (ctx_p->prepared_stmt == stmt_p);
	      goto relay_prepare_request;
	    }

	  if (stmt_p->stmt_type != SHARD_STMT_TYPE_PREPARED)
	    {
	      assert (false);

	      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Unexpected statement type. expect(%d), statement(%s). context(%s).",
			 SHARD_STMT_TYPE_PREPARED, shard_str_stmt (stmt_p), proxy_str_context (ctx_p));
	      goto free_context;
	    }

	  waiter_p = proxy_waiter_new (ctx_p->cid, ctx_p->uid, ctx_p->wait_timeout);
	  if (waiter_p == NULL)
	    {
	      goto free_context;
	    }

	  error = shard_queue_ordered_enqueue (&stmt_p->waitq, (void *) waiter_p, proxy_waiter_comp_fn);
	  if (error)
	    {
	      proxy_waiter_free (waiter_p);
	      waiter_p = NULL;

	      goto free_context;
	    }

	  proxy_info_p->stmt_waiter_count++;
	  PROXY_DEBUG_LOG ("Add stmt waiter. (waiter_coutn:%d).", proxy_info_p->stmt_waiter_count);

	  ctx_p->waiting_event = event_p;
	  event_p = NULL;	/* DO NOT DELETE */

	  error = 0;
	  goto end;

	default:
	  assert (false);
	  goto free_context;
	}
    }

  if (use_temp_statement)
    {
      stmt_p = shard_stmt_new_exclusive (organized_sql_stmt, ctx_p->cid, ctx_p->uid, client_version);
    }
  else
    {
      stmt_p = shard_stmt_new_prepared_stmt (organized_sql_stmt, ctx_p->cid, ctx_p->uid, client_version);
    }
  if (stmt_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to create new statement. context(%s).", proxy_str_context (ctx_p));
      proxy_context_set_error (ctx_p, CAS_ERROR_INDICATOR, CAS_ER_INTERNAL);

      proxy_event_free (event_p);
      event_p = NULL;

      error = -1;
      goto end;
    }

  PROXY_DEBUG_LOG ("Create new sql statement. " "(index:%d). statement(%s). context(%s).", stmt_p->index,
		   shard_str_stmt (stmt_p), proxy_str_context (ctx_p));

  if (proxy_info_p->ignore_shard_hint == OFF)
    {
      error = shard_stmt_set_hint_list (stmt_p);
      if (error < 0)
	{
	  PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to set hint list. statement(%s). context(%s).",
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
    }

  ctx_p->prepared_stmt = stmt_p;

  /* save statement to the context */
  if (proxy_context_add_stmt (ctx_p, stmt_p) == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to link statement to context. statement(%s). context(%s).",
		 shard_str_stmt (stmt_p), proxy_str_context (ctx_p));
      goto free_context;
    }

  error =
    shard_stmt_save_prepare_request (stmt_p, has_shard_val_hint, &event_p->buffer.data, &event_p->buffer.length,
				     argv[0], argv[1], organized_sql_stmt);
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to save prepared statement request. statement(%s). context(%s).",
		 shard_str_stmt (stmt_p), proxy_str_context (ctx_p));
      goto free_context;
    }
  request_p = event_p->buffer.data;	/* DO NOT DELETE */

relay_prepare_request:

  if (ctx_p->is_in_tran == false || ctx_p->waiting_dummy_prepare == true)
    {
      proxy_set_force_out_tran (request_p);
    }
  ctx_p->waiting_dummy_prepare = false;

  if (stmt_p->status != SHARD_STMT_STATUS_IN_PROGRESS)
    {
      PROXY_DEBUG_LOG ("Unexpected statement status. (status=%d). statement(%s). context(%s).", stmt_p->status,
		       shard_str_stmt (stmt_p), proxy_str_context (ctx_p));
      assert (false);
      goto free_context;
    }

  /* if shard_key is static value(or shard_id), dummy prepare send to values's shard */
  if (proxy_info_p->ignore_shard_hint == OFF)
    {
      if (sp_is_hint_static (stmt_p->parser))
	{
	  shard_id = proxy_get_shard_id (stmt_p, NULL, &dummy_range_p);
	  if (shard_id == PROXY_INVALID_SHARD)
	    {
	      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Invalid shard id. (shard_id:%d). context(%s).", shard_id,
			 proxy_str_context (ctx_p));

	      proxy_context_set_error (ctx_p, CAS_ERROR_INDICATOR, CAS_ER_INTERNAL);

	      /* wakeup and reset statment */
	      shard_stmt_check_waiter_and_wakeup (ctx_p->prepared_stmt);
	      shard_stmt_free (ctx_p->prepared_stmt);
	      ctx_p->prepared_stmt = NULL;

	      proxy_event_free (event_p);
	      event_p = NULL;

	      error = -1;
	      goto end;
	    }
	  ctx_p->shard_id = shard_id;
	}
    }

  cas_io_p =
    proxy_cas_alloc_by_ctx (ctx_p->client_id, ctx_p->shard_id, ctx_p->cas_id, ctx_p->cid, ctx_p->uid,
			    ctx_p->wait_timeout, func_code);
  if (cas_io_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to allocate CAS. context(%s).", proxy_str_context (ctx_p));

      proxy_context_set_error (ctx_p, CAS_ERROR_INDICATOR, CAS_ER_INTERNAL);

      /* wakeup and reset statment */
      shard_stmt_check_waiter_and_wakeup (ctx_p->prepared_stmt);
      shard_stmt_free (ctx_p->prepared_stmt);
      ctx_p->prepared_stmt = NULL;

      proxy_event_free (event_p);
      event_p = NULL;

      if (ctx_p->shard_id != PROXY_INVALID_SHARD)
	{
	  EXIT_FUNC ();
	  goto free_context;
	}
      else
	{
	  error = -1;
	  goto end;
	}
    }
  else if (cas_io_p == (T_CAS_IO *) SHARD_TEMPORARY_UNAVAILABLE)
    {
      /* waiting idle shard/cas */
      ctx_p->waiting_event = event_p;
      event_p = NULL;

      ctx_p->waiting_dummy_prepare = true;

      error = 0;
      goto end;
    }

  /* we should bind context and shard/cas after complete to allocate */
  proxy_context_set_in_tran (ctx_p, cas_io_p->shard_id, cas_io_p->cas_id);

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

  ctx_p->func_code = func_code;

end:
  if (organized_sql_stmt && organized_sql_stmt != sql_stmt)
    {
      FREE_MEM (organized_sql_stmt);
    }

  EXIT_FUNC ();
  return error;

free_context:
  if (organized_sql_stmt && organized_sql_stmt != sql_stmt)
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
fn_proxy_client_execute (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p, int argc, char **argv)
{
  int query_timeout;
  int bind_value_index = 9;
  T_BROKER_VERSION client_version;
  char *driver_info;

  char func_code = CAS_FC_EXECUTE;

  driver_info = proxy_get_driver_info_by_ctx (ctx_p);
  client_version = CAS_MAKE_PROTO_VER (driver_info);
  if (client_version >= CAS_PROTO_MAKE_VER (PROTOCOL_V1))
    {
      bind_value_index++;

      net_arg_get_int (&query_timeout, argv[9]);
      if (!DOES_CLIENT_UNDERSTAND_THE_PROTOCOL (client_version, PROTOCOL_V2))
	{
	  /* protocol version v1 driver send query timeout in second */
	  query_timeout *= 1000;
	}
    }
  else
    {
      query_timeout = 0;
    }

  return proxy_client_execute_internal (ctx_p, event_p, argc, argv, func_code, query_timeout, bind_value_index);
}

static int
proxy_client_execute_internal (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p, int argc, char **argv, char _func_code,
			       int query_timeout, int bind_value_index)
{
  int error = 0;
  int srv_h_id;
  int cas_srv_h_id;
  char *prepare_request = NULL;
  int i;
  int shard_id, next_shard_id;
  int length;
  SP_HINT_TYPE hint_type = HT_NONE;
  int bind_value_size;
  int argc_mod_2;
  T_CAS_IO *cas_io_p;
  T_SHARD_STMT *stmt_p = NULL;
  T_SHARD_KEY_RANGE *range_p = NULL;

  T_PROXY_EVENT *new_event_p = NULL;

  char *request_p;

  char func_code = _func_code;

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

  argc_mod_2 = bind_value_index % 2;

  if ((argc < bind_value_index) || (argc % 2 != argc_mod_2))
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Invalid argument. (argc:%d). context(%s).", argc, proxy_str_context (ctx_p));

      proxy_context_set_error (ctx_p, CAS_ERROR_INDICATOR, CAS_ER_ARGS);

      proxy_event_free (event_p);
      event_p = NULL;

      EXIT_FUNC ();
      return -1;
    }

  net_arg_get_int (&srv_h_id, argv[0]);

  /* bind variables, even:bind type, odd:bind value */

  stmt_p = shard_stmt_find_by_stmt_h_id (srv_h_id);
  if (stmt_p == NULL || stmt_p->status == SHARD_STMT_STATUS_INVALID)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Unable to find statement handle identifier. " "(srv_h_id:%d). context(%s).",
		 srv_h_id, proxy_str_context (ctx_p));

      proxy_context_set_error (ctx_p, CAS_ERROR_INDICATOR, CAS_ER_STMT_POOLING);

      proxy_event_free (event_p);
      event_p = NULL;

      EXIT_FUNC ();
      return -1;
    }

  if (proxy_info_p->ignore_shard_hint == OFF)
    {
      hint_type = (SP_HINT_TYPE) shard_stmt_get_hint_type (stmt_p);
      if (hint_type <= HT_INVAL || hint_type > HT_EOF)
	{
	  PROXY_LOG (PROXY_LOG_MODE_ERROR, "Unsupported hint type. (hint_type:%d). context(%s).", hint_type,
		     proxy_str_context (ctx_p));

	  proxy_context_set_error (ctx_p, CAS_ERROR_INDICATOR, CAS_ER_INTERNAL);

	  proxy_event_free (event_p);
	  event_p = NULL;

	  EXIT_FUNC ();
	  return -1;
	}

      shard_id = proxy_get_shard_id (stmt_p, (void **) (argv + bind_value_index), &range_p);

      PROXY_LOG (PROXY_LOG_MODE_SHARD_DETAIL, "Select shard. " "(prev_shard_id:%d, curr_shard_id:%d). context(%s).",
		 ctx_p->shard_id, shard_id, proxy_str_context (ctx_p));

      /* check shard_id */
      if (shard_id == PROXY_INVALID_SHARD)
	{
	  PROXY_LOG (PROXY_LOG_MODE_ERROR, "Invalid shard id. (shard_id:%d). context(%s).", shard_id,
		     proxy_str_context (ctx_p));

	  proxy_context_set_error (ctx_p, CAS_ERROR_INDICATOR, CAS_ER_INTERNAL);

	  proxy_event_free (event_p);
	  event_p = NULL;

	  EXIT_FUNC ();
	  return -1;
	}

      if (func_code == CAS_FC_EXECUTE_ARRAY)
	{
	  /* bind value size = bind count * 2(type:value) */
	  bind_value_size = stmt_p->parser->bind_count * 2;

	  /* compare with next batch shard_id when execute_array */
	  for (i = bind_value_index + bind_value_size; i < argc; i += bind_value_size)
	    {
	      next_shard_id = proxy_get_shard_id (stmt_p, (void **) (argv + i), &range_p);

	      if (shard_id != next_shard_id)
		{
		  PROXY_LOG (PROXY_LOG_MODE_ERROR,
			     "Shard id is different. " "(first_shard_id:%d, next_shard_id:%d). context(%s).", shard_id,
			     next_shard_id, proxy_str_context (ctx_p));

		  proxy_context_set_error (ctx_p, CAS_ERROR_INDICATOR, CAS_ER_INTERNAL);

		  proxy_event_free (event_p);
		  event_p = NULL;

		  EXIT_FUNC ();
		  return -1;
		}
	    }
	}

      if (ctx_p->shard_id != PROXY_INVALID_SHARD && ctx_p->shard_id != shard_id)
	{
	  PROXY_LOG (PROXY_LOG_MODE_ERROR,
		     "Shard id couldn't be changed in a transaction. "
		     "(prev_shard_id:%d, curr_shard_id:%d). context(%s).", ctx_p->shard_id, shard_id,
		     proxy_str_context (ctx_p));

	  proxy_context_set_error (ctx_p, CAS_ERROR_INDICATOR, CAS_ER_INTERNAL);

	  proxy_event_free (event_p);
	  event_p = NULL;

	  EXIT_FUNC ();
	  goto free_context;
	}

      ctx_p->shard_id = shard_id;
    }

  proxy_set_wait_timeout (ctx_p, query_timeout);

  cas_io_p =
    proxy_cas_alloc_by_ctx (ctx_p->client_id, ctx_p->shard_id, ctx_p->cas_id, ctx_p->cid, ctx_p->uid,
			    ctx_p->wait_timeout, func_code);
  if (cas_io_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to allocate CAS. context(%s).", proxy_str_context (ctx_p));

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

  /* save statement to the context */
  if (proxy_context_add_stmt (ctx_p, stmt_p) == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to link statement to context. statement(%s). context(%s).",
		 shard_str_stmt (stmt_p), proxy_str_context (ctx_p));
      goto free_context;
    }

  /*
   * find stored server handle id for this shard/cas, if exist, do execute
   * with it. otherwise, do dummy prepare for exeucte to get server handle id.
   */
  cas_srv_h_id = shard_stmt_find_srv_h_id_for_shard_cas (srv_h_id, cas_io_p->shard_id, cas_io_p->cas_id);
  if (cas_srv_h_id < 0)		/* not prepared */
    {
      PROXY_LOG (PROXY_LOG_MODE_SHARD_DETAIL, "Do prepare before execute. cas(%s). context(%s).",
		 proxy_str_cas_io (cas_io_p), proxy_str_context (ctx_p));

      /* make prepare request event */
      prepare_request = proxy_dup_msg ((char *) stmt_p->request_buffer);
      if (prepare_request == NULL)
	{
	  goto free_context;
	}
      length = get_msg_length (prepare_request);

      /* unset force_out_tran bitmask */
      proxy_unset_force_out_tran (request_p);

      new_event_p = proxy_event_new (PROXY_EVENT_IO_WRITE, PROXY_EVENT_FROM_CAS);
      if (new_event_p == NULL)
	{
	  goto free_context;
	}
      proxy_event_set_buffer (new_event_p, prepare_request, length);
      prepare_request = NULL;

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
      /* If we will fail to execute query, then we have to retry. */
      ctx_p->waiting_event = proxy_event_dup (event_p);
      if (ctx_p->waiting_event == NULL)
	{
	  goto free_context;
	}

      net_arg_put_int (argv[0], &cas_srv_h_id);
    }

  ctx_p->stmt_hint_type = hint_type;
  ctx_p->stmt_h_id = srv_h_id;

  if (proxy_info_p->ignore_shard_hint == OFF)
    {
      /* update shard statistics */
      if (stmt_p)
	{
	  proxy_update_shard_stats (stmt_p, range_p);
	}
      else
	{
	  assert (false);
	}
    }
  else
    {
      proxy_update_shard_stats_without_hint (ctx_p->shard_id);
    }

  new_event_p = event_p;	/* add comment */

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
      if (event_p)
	{
	  proxy_event_free (event_p);
	}
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

  if (prepare_request)
    {
      FREE_MEM (prepare_request);
    }

  ctx_p->free_context = true;

  EXIT_FUNC ();
  return -1;
}

int
fn_proxy_client_set_db_parameter (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p, int argc, char **argv)
{
  int error = 0;
  int param_name;
  T_CLIENT_INFO *client_info_p = NULL;
  char *driver_info;
  T_PROXY_EVENT *new_event_p = NULL;

  if (argc < 2)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Invalid argument. (argc:%d). context(%s).", argc, proxy_str_context (ctx_p));
      proxy_context_set_error (ctx_p, CAS_ERROR_INDICATOR, CAS_ER_ARGS);
      error = -1;
      goto free_and_return;
    }

  client_info_p = shard_shm_get_client_info (proxy_info_p, ctx_p->client_id);
  if (client_info_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Unable to find cilent info in shared memory. " "(context id:%d, context uid:%d)", ctx_p->cid,
		 ctx_p->uid);
      proxy_context_set_error (ctx_p, CAS_ERROR_INDICATOR, CAS_ER_INTERNAL);
      error = -1;
      goto free_and_return;
    }

  net_arg_get_int (&param_name, argv[0]);
  if (param_name == CCI_PARAM_ISOLATION_LEVEL)
    {
      int isolation_level = 0;

      net_arg_get_int (&isolation_level, argv[1]);
      if (!IS_VALID_ISOLATION_LEVEL (isolation_level))
	{
	  PROXY_LOG (PROXY_LOG_MODE_ERROR,
		     "Invalid isolation level. (isolation:%d). " "(context id:%d, context uid:%d)", isolation_level,
		     ctx_p->cid, ctx_p->uid);
	  proxy_context_set_error (ctx_p, CAS_ERROR_INDICATOR, CAS_ER_INTERNAL);
	  error = -1;
	  goto free_and_return;
	}

      client_info_p->isolation_level = isolation_level;
    }
  else if (param_name == CCI_PARAM_LOCK_TIMEOUT)
    {
      int lock_timeout = -1;

      net_arg_get_int (&lock_timeout, argv[1]);
      if (lock_timeout < CAS_USE_DEFAULT_DB_PARAM)
	{
	  lock_timeout = -1;
	}

      client_info_p->lock_timeout = lock_timeout;
    }

  driver_info = proxy_get_driver_info_by_ctx (ctx_p);
  new_event_p =
    proxy_event_new_with_rsp (driver_info, PROXY_EVENT_IO_WRITE, PROXY_EVENT_FROM_CLIENT,
			      proxy_io_make_set_db_parameter_ok);
  if (new_event_p == NULL)
    {
      goto free_context;
    }

  error = proxy_send_response_to_client (ctx_p, new_event_p);
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to response to the client. " "(error=%d). context(%s). evnet(%s).",
		 error, proxy_str_context (ctx_p), proxy_str_event (new_event_p));

      proxy_event_free (new_event_p);
      goto free_context;
    }

free_and_return:
  if (event_p != NULL)
    {
      proxy_event_free (event_p);
    }

  return error;

free_context:
  ctx_p->free_context = true;

  if (event_p != NULL)
    {
      proxy_event_free (event_p);
    }

  return -1;
}

int
fn_proxy_client_get_db_parameter (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p, int argc, char **argv)
{
  int error = 0;
  int param_name;
  T_CLIENT_INFO *client_info_p = NULL;
  char *driver_info;
  T_PROXY_EVENT *new_event_p = NULL;

  if (argc < 1)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Invalid argument. (argc:%d). context(%s).", argc, proxy_str_context (ctx_p));
      proxy_context_set_error (ctx_p, CAS_ERROR_INDICATOR, CAS_ER_ARGS);
      error = -1;
      goto free_and_return;
    }

  client_info_p = shard_shm_get_client_info (proxy_info_p, ctx_p->client_id);
  if (client_info_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Unable to find cilent info in shared memory. " "(context id:%d, context uid:%d)", ctx_p->cid,
		 ctx_p->uid);
      proxy_context_set_error (ctx_p, CAS_ERROR_INDICATOR, CAS_ER_INTERNAL);
      error = -1;
      goto free_and_return;
    }

  net_arg_get_int (&param_name, argv[0]);

  driver_info = proxy_get_driver_info_by_ctx (ctx_p);

  if (param_name == CCI_PARAM_ISOLATION_LEVEL)
    {
      new_event_p =
	proxy_event_new_with_rsp_ex (driver_info, PROXY_EVENT_IO_WRITE, PROXY_EVENT_FROM_CLIENT,
				     proxy_io_make_ex_get_isolation_level, &client_info_p->isolation_level);
    }
  else if (param_name == CCI_PARAM_LOCK_TIMEOUT)
    {
      new_event_p =
	proxy_event_new_with_rsp_ex (driver_info, PROXY_EVENT_IO_WRITE, PROXY_EVENT_FROM_CLIENT,
				     proxy_io_make_ex_get_lock_timeout, &client_info_p->isolation_level);
    }

  if (new_event_p == NULL)
    {
      goto free_context;
    }

  error = proxy_send_response_to_client (ctx_p, new_event_p);
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to response to the client. " "(error=%d). context(%s). evnet(%s).",
		 error, proxy_str_context (ctx_p), proxy_str_event (new_event_p));

      proxy_event_free (new_event_p);
      goto free_context;
    }

free_and_return:
  if (event_p != NULL)
    {
      proxy_event_free (event_p);
    }

  return error;

free_context:
  ctx_p->free_context = true;

  if (event_p != NULL)
    {
      proxy_event_free (event_p);
    }

  return -1;
}

int
fn_proxy_client_close_req_handle (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p, int argc, char **argv)
{
  int error = 0;
  T_CAS_IO *cas_io_p;
  int srv_h_id;
  int cas_srv_h_id;

  const char func_code = CAS_FC_CLOSE_REQ_HANDLE;

  ENTER_FUNC ();

  if (ctx_p->is_in_tran)
    {
      cas_io_p =
	proxy_cas_alloc_by_ctx (ctx_p->client_id, ctx_p->shard_id, ctx_p->cas_id, ctx_p->cid, ctx_p->uid,
				ctx_p->wait_timeout, func_code);
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

      net_arg_get_int (&srv_h_id, argv[0]);

      cas_srv_h_id = shard_stmt_find_srv_h_id_for_shard_cas (srv_h_id, cas_io_p->shard_id, cas_io_p->cas_id);
      if (cas_srv_h_id < 0)
	{
	  PROXY_LOG (PROXY_LOG_MODE_ERROR,
		     "Failed to close requested handle. " "cannot find cas server handle. context(%s).",
		     proxy_str_context (ctx_p));
	  assert (false);
	  goto out_tran;
	}

      /* remove statement handle id for this cas */
      shard_stmt_del_srv_h_id_for_shard_cas (srv_h_id, ctx_p->shard_id, ctx_p->cas_id);

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

out_tran:
  /* send close_req_handle response to the client, if this context is not IN_TRAN, or context is IN_TRAN status but
   * cas_srv_h_id is not found */
  /* FIXME : if context is IN_TRAN and cas_srv_hd_id is not found ? */
  error =
    proxy_send_response_to_client_with_new_event (ctx_p, PROXY_EVENT_IO_WRITE, PROXY_EVENT_FROM_CLIENT,
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
fn_proxy_client_cursor (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p, int argc, char **argv)
{
  int error = 0;
  int srv_h_id, cas_srv_h_id;

  T_CAS_IO *cas_io_p;

  const char func_code = CAS_FC_CURSOR;

  ENTER_FUNC ();

  if (ctx_p->is_in_tran == false)
    {
      proxy_context_set_error (ctx_p, CAS_ERROR_INDICATOR, CAS_ER_SRV_HANDLE);

      proxy_event_free (event_p);
      event_p = NULL;

      EXIT_FUNC ();
      return -1;
    }

  /* find idle cas */
  cas_io_p =
    proxy_cas_alloc_by_ctx (ctx_p->client_id, ctx_p->shard_id, ctx_p->cas_id, ctx_p->cid, ctx_p->uid,
			    ctx_p->wait_timeout, func_code);
  if (cas_io_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Unable to find avaiable idle CAS. " "context(%s). evnet(%s).",
		 proxy_str_context (ctx_p), proxy_str_event (event_p));

      goto free_context;
    }
  else if (cas_io_p == (T_CAS_IO *) SHARD_TEMPORARY_UNAVAILABLE)
    {
      assert (false);		/* it cannot happen */

      goto free_context;
    }
  assert (ctx_p->shard_id == cas_io_p->shard_id);
  assert (ctx_p->cas_id == cas_io_p->cas_id);

  /*
   * find stored server handle id for this shard/cas, if exist, do fetch
   * with it. otherwise, returns proxy internal error to the client.
   */
  net_arg_get_int (&srv_h_id, argv[0]);

  cas_srv_h_id = shard_stmt_find_srv_h_id_for_shard_cas (srv_h_id, ctx_p->shard_id, ctx_p->cas_id);
  if (cas_srv_h_id <= 0)
    {
      proxy_context_set_error (ctx_p, CAS_ERROR_INDICATOR, CAS_ER_SRV_HANDLE);

      proxy_event_free (event_p);
      event_p = NULL;

      EXIT_FUNC ();
      return -1;
    }

  net_arg_put_int (argv[0], &(cas_srv_h_id));

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
fn_proxy_client_fetch (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p, int argc, char **argv)
{
  int error = 0;
  int srv_h_id, cas_srv_h_id;

  T_CAS_IO *cas_io_p;

  const char func_code = CAS_FC_FETCH;

  ENTER_FUNC ();

  if (ctx_p->is_in_tran == false)
    {
      proxy_context_set_error (ctx_p, CAS_ERROR_INDICATOR, CAS_ER_SRV_HANDLE);

      proxy_event_free (event_p);
      event_p = NULL;

      EXIT_FUNC ();
      return -1;
    }

  /* find idle cas */
  cas_io_p =
    proxy_cas_alloc_by_ctx (ctx_p->client_id, ctx_p->shard_id, ctx_p->cas_id, ctx_p->cid, ctx_p->uid,
			    ctx_p->wait_timeout, func_code);
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
  net_arg_get_int (&srv_h_id, argv[0]);

  cas_srv_h_id = shard_stmt_find_srv_h_id_for_shard_cas (srv_h_id, ctx_p->shard_id, ctx_p->cas_id);
  if (cas_srv_h_id <= 0)
    {
      proxy_context_set_error (ctx_p, CAS_ERROR_INDICATOR, CAS_ER_SRV_HANDLE);

      proxy_event_free (event_p);
      event_p = NULL;

      EXIT_FUNC ();
      return -1;
    }

  net_arg_put_int (argv[0], &(cas_srv_h_id));

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
fn_proxy_client_schema_info (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p, int argc, char **argv)
{
  int error = 0;
  int shard_id;
  T_SHARD_STMT *stmt_p;
  char *driver_info;
  T_BROKER_VERSION client_version;

  const char func_code = CAS_FC_SCHEMA_INFO;

  ENTER_FUNC ();

  if (ctx_p->waiting_event)
    {
      assert (false);

      proxy_event_free (ctx_p->waiting_event);
      ctx_p->waiting_event = NULL;

      goto free_context;
    }

  if (ctx_p->prepared_stmt == NULL)
    {
      stmt_p = shard_stmt_new_schema_info (ctx_p->cid, ctx_p->uid);
      if (stmt_p == NULL)
	{
	  proxy_context_set_error (ctx_p, CAS_ERROR_INDICATOR, CAS_ER_INTERNAL);

	  proxy_event_free (event_p);
	  event_p = NULL;

	  EXIT_FUNC ();
	  return -1;
	}

      /* save statement to the context */
      ctx_p->prepared_stmt = stmt_p;
      if (proxy_context_add_stmt (ctx_p, stmt_p) == NULL)
	{
	  PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to link statement to context. statement(%s). context(%s).",
		     shard_str_stmt (stmt_p), proxy_str_context (ctx_p));
	  goto free_context;
	}
    }
  else
    {
      /*
       * It can be happened, when schema_info request is re-invoked
       * by shard waiter.
       */

      if (ctx_p->prepared_stmt->stmt_type != SHARD_STMT_TYPE_SCHEMA_INFO)
	{
	  assert (false);

	  PROXY_LOG (PROXY_LOG_MODE_ERROR, "Unexpected statement type. expect(%d), statement(%s). context(%s).",
		     SHARD_STMT_TYPE_SCHEMA_INFO, shard_str_stmt (ctx_p->prepared_stmt), proxy_str_context (ctx_p));

	  goto free_context;
	}

      /* statement linked to the context already */
    }

  driver_info = proxy_get_driver_info_by_ctx (ctx_p);
  client_version = CAS_MAKE_PROTO_VER (driver_info);
  if (DOES_CLIENT_UNDERSTAND_THE_PROTOCOL (client_version, PROTOCOL_V5))
    {
      net_arg_get_int (&shard_id, argv[4]);
    }
  else
    {
      shard_id = 0;		/* default SHARD # 0 */
    }

  if ((shard_id < 0 || shard_id >= proxy_info_p->max_shard)
      || (ctx_p->shard_id != PROXY_INVALID_SHARD && ctx_p->shard_id != shard_id))
    {
      proxy_context_set_error (ctx_p, CAS_ERROR_INDICATOR, CAS_ER_INTERNAL);

      proxy_event_free (event_p);
      event_p = NULL;

      if (ctx_p->prepared_stmt)
	{
	  shard_stmt_free (ctx_p->prepared_stmt);
	  ctx_p->prepared_stmt = NULL;
	}

      EXIT_FUNC ();
      return -1;
    }
  ctx_p->shard_id = shard_id;

  error = proxy_send_request_to_cas (ctx_p, event_p, func_code);
  if (error < 0)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to send request to CAS. " "(error=%d). context(%s). evnet(%s).", error,
		 proxy_str_context (ctx_p), proxy_str_event (event_p));

      goto free_context;
    }
  else if (error > 0)
    {
      /* waiting idle shard/cas */
      ctx_p->waiting_event = event_p;
      event_p = NULL;

      EXIT_FUNC ();
      return 0;
    }

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
fn_proxy_client_check_cas (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p, int argc, char **argv)
{
  int error = 0;
  char *request_p = NULL;
  char *response_p = NULL;
  char *driver_info;
  T_PROXY_EVENT *new_event_p = NULL;

  ENTER_FUNC ();

  if (ctx_p->is_connected)
    {
      driver_info = proxy_get_driver_info_by_ctx (ctx_p);
      new_event_p =
	proxy_event_new_with_rsp (driver_info, PROXY_EVENT_IO_WRITE, PROXY_EVENT_FROM_CLIENT,
				  proxy_io_make_check_cas_ok);
      if (new_event_p == NULL)
	{
	  goto free_context;
	}

      response_p = new_event_p->buffer.data;
      assert (response_p);	// __FOR_DEBUG

      if (ctx_p->is_client_in_tran)
	{
	  proxy_set_con_status_in_tran (response_p);
	}

      error = proxy_send_response_to_client (ctx_p, new_event_p);
      if (error)
	{
	  PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to response to the client. " "(error=%d). context(%s). evnet(%s).",
		     error, proxy_str_context (ctx_p), proxy_str_event (new_event_p));

	  goto free_context;
	}
      new_event_p = NULL;

      proxy_event_free (event_p);
      event_p = NULL;
    }
  else
    {
      request_p = event_p->buffer.data;

      proxy_set_force_out_tran (request_p);

      error = proxy_send_request_to_cas (ctx_p, event_p, CAS_FC_CHECK_CAS);
      if (error < 0)
	{
	  PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to send request to CAS. " "(error=%d). context(%s). evnet(%s).",
		     error, proxy_str_context (ctx_p), proxy_str_event (event_p));

	  proxy_event_free (event_p);
	  event_p = NULL;
	}
      else if (error > 0)
	{
	  PROXY_LOG (PROXY_LOG_MODE_DEBUG, "Waiting cas to connect db context(%s)", proxy_str_context (ctx_p));
	  ctx_p->waiting_event = event_p;
	  event_p = NULL;
	}
      else
	{
	  ctx_p->waiting_event = proxy_event_dup (event_p);
	  if (ctx_p->waiting_event == NULL)
	    {
	      goto free_context;
	    }
	}

      ctx_p->func_code = CAS_FC_CHECK_CAS;
    }

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
fn_proxy_client_con_close (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p, int argc, char **argv)
{
  int error = 0;

  ENTER_FUNC ();

  /* set client tran status as OUT_TRAN, when con_close */
  ctx_p->is_client_in_tran = false;
  ctx_p->free_on_client_io_write = true;
  ctx_p->dont_free_statement = false;

  if (ctx_p->is_in_tran)
    {
      ctx_p->func_code = CAS_FC_END_TRAN;

      error =
	proxy_send_request_to_cas_with_new_event (ctx_p, PROXY_EVENT_IO_WRITE, PROXY_EVENT_FROM_CAS,
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
      error =
	proxy_send_response_to_client_with_new_event (ctx_p, PROXY_EVENT_IO_WRITE, PROXY_EVENT_FROM_CLIENT,
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
fn_proxy_client_get_db_version (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p, int argc, char **argv)
{
  int error = 0;
  char *db_version = NULL;
  char auto_commit_mode;

  ENTER_FUNC ();

  if (argc < 1)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Invalid argument. (argc:%d). context(%s).", argc, proxy_str_context (ctx_p));
      proxy_context_set_error (ctx_p, CAS_ERROR_INDICATOR, CAS_ER_ARGS);

      proxy_event_free (event_p);
      event_p = NULL;

      EXIT_FUNC ();
      return -1;
    }

  /* arg0 */
  net_arg_get_char (auto_commit_mode, argv[0]);


  error =
    proxy_send_response_to_client_with_new_event (ctx_p, PROXY_EVENT_IO_WRITE, PROXY_EVENT_FROM_CLIENT,
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
fn_proxy_client_cursor_close (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p, int argc, char **argv)
{
  int error = 0;
  T_CAS_IO *cas_io_p;
  int srv_h_id;
  int cas_srv_h_id;

  const char func_code = CAS_FC_CURSOR_CLOSE;

  ENTER_FUNC ();

  if (ctx_p->is_in_tran)
    {
      cas_io_p =
	proxy_cas_alloc_by_ctx (ctx_p->client_id, ctx_p->shard_id, ctx_p->cas_id, ctx_p->cid, ctx_p->uid,
				ctx_p->wait_timeout, func_code);
      if (cas_io_p == NULL)
	{
	  goto free_context;
	}
      else if (cas_io_p == (T_CAS_IO *) SHARD_TEMPORARY_UNAVAILABLE)
	{
	  assert (false);	/* it cannot be happened */

	  goto free_context;
	}
      assert (ctx_p->shard_id == cas_io_p->shard_id);
      assert (ctx_p->cas_id == cas_io_p->cas_id);
      proxy_context_set_in_tran (ctx_p, cas_io_p->shard_id, cas_io_p->cas_id);

      net_arg_get_int (&srv_h_id, argv[0]);

      cas_srv_h_id = shard_stmt_find_srv_h_id_for_shard_cas (srv_h_id, cas_io_p->shard_id, cas_io_p->cas_id);
      if (cas_srv_h_id < 0)
	{
	  PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to cursoe close. " "cannot find cas server handle. context(%s).",
		     proxy_str_context (ctx_p));
	  assert (false);
	  goto out_tran;
	}

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

out_tran:
  /* send cursor_close response to the client, if this context is not IN_TRAN, or context is IN_TRAN status but
   * cas_srv_h_id is not found */
  /* FIXME : if context is IN_TRAN and cas_srv_hd_id is not found ? */
  error =
    proxy_send_response_to_client_with_new_event (ctx_p, PROXY_EVENT_IO_WRITE, PROXY_EVENT_FROM_CLIENT,
						  proxy_io_make_cursor_close_out_tran_ok);
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
fn_proxy_client_execute_array (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p, int argc, char **argv)
{
  int query_timeout;
  int bind_value_index = 2;
  char *driver_info;
  T_BROKER_VERSION client_version;

  char func_code = CAS_FC_EXECUTE_ARRAY;

  driver_info = proxy_get_driver_info_by_ctx (ctx_p);
  client_version = CAS_MAKE_PROTO_VER (driver_info);
  if (client_version >= CAS_PROTO_MAKE_VER (PROTOCOL_V4))
    {
      bind_value_index++;

      net_arg_get_int (&query_timeout, argv[1]);
    }
  else
    {
      query_timeout = 0;
    }

  return proxy_client_execute_internal (ctx_p, event_p, argc, argv, func_code, query_timeout, bind_value_index);
}

int
fn_proxy_get_shard_info (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p, int argc, char **argv)
{
  int error = 0;

  ENTER_FUNC ();

  error =
    proxy_send_response_to_client_with_new_event (ctx_p, PROXY_EVENT_IO_WRITE, PROXY_EVENT_FROM_CLIENT,
						  proxy_io_make_shard_info);
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
fn_proxy_client_prepare_and_execute (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p, int argc, char **argv)
{
  int error = 0;
  int i = 0;
  int prepare_argc_count = 0;
  int query_timeout;

  /* sql statement */
  char *sql_stmt = NULL;
  int sql_size = 0;

  /* argv */
  char flag = 0;
  char auto_commit_mode = FALSE;
  char func_code = CAS_FC_PREPARE_AND_EXECUTE;

  T_CAS_IO *cas_io_p;
  T_SHARD_STMT *stmt_p = NULL;
  int shard_id;
  SP_HINT_TYPE hint_type = HT_INVAL;
  T_SHARD_KEY_RANGE *range_p = NULL;

  char *driver_info;
  T_BROKER_VERSION client_version;

  char *request_p;

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
  if (argc < 3)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Invalid argument. (argc:%d). context(%s).", argc, proxy_str_context (ctx_p));
      proxy_context_set_error (ctx_p, CAS_ERROR_INDICATOR, CAS_ER_ARGS);

      proxy_event_free (event_p);
      event_p = NULL;

      EXIT_FUNC ();
      error = -1;
      goto end;
    }

  net_arg_get_int (&prepare_argc_count, argv[0]);
  net_arg_get_str (&sql_stmt, &sql_size, argv[1]);
  net_arg_get_char (flag, argv[2]);
  net_arg_get_char (auto_commit_mode, argv[3]);
  for (i = 4; i < prepare_argc_count + 1; i++)
    {
      int deferred_close_handle;
      net_arg_get_int (&deferred_close_handle, argv[i]);

      /* SHARD TODO : what to do for deferred close handle? */
    }

  driver_info = proxy_get_driver_info_by_ctx (ctx_p);
  client_version = CAS_MAKE_PROTO_VER (driver_info);
  if (client_version >= CAS_PROTO_MAKE_VER (PROTOCOL_V1))
    {
      net_arg_get_int (&query_timeout, argv[prepare_argc_count + 6]);
      if (!DOES_CLIENT_UNDERSTAND_THE_PROTOCOL (client_version, PROTOCOL_V2))
	{
	  /* protocol version v1 driver send query timeout in second */
	  query_timeout *= 1000;
	}
    }
  else
    {
      query_timeout = 0;
    }

  PROXY_DEBUG_LOG ("Process requested prepare and execute sql statement. " "(sql_stmt:[%s]). context(%s).",
		   shard_str_sqls (sql_stmt), proxy_str_context (ctx_p));

  if (ctx_p->prepared_stmt == NULL)
    {
      stmt_p = shard_stmt_new_exclusive (sql_stmt, ctx_p->cid, ctx_p->uid, client_version);
      if (stmt_p == NULL)
	{
	  proxy_context_set_error (ctx_p, CAS_ERROR_INDICATOR, CAS_ER_INTERNAL);

	  proxy_event_free (event_p);
	  event_p = NULL;

	  EXIT_FUNC ();

	  error = -1;
	  goto end;
	}

      if (proxy_info_p->ignore_shard_hint == OFF)
	{
	  error = shard_stmt_set_hint_list (stmt_p);
	  if (error < 0)
	    {
	      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to set hint list. statement(%s). context(%s).",
			 shard_str_stmt (stmt_p), proxy_str_context (ctx_p));

	      proxy_context_set_error (ctx_p, CAS_ERROR_INDICATOR, CAS_ER_INTERNAL);

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
	}

      /* save statement to the context */
      ctx_p->prepared_stmt = stmt_p;
      if (proxy_context_add_stmt (ctx_p, stmt_p) == NULL)
	{
	  PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to link statement to context. statement(%s). context(%s).",
		     shard_str_stmt (stmt_p), proxy_str_context (ctx_p));
	  goto free_context;
	}

      if (proxy_info_p->ignore_shard_hint == OFF)
	{
	  hint_type = (SP_HINT_TYPE) shard_stmt_get_hint_type (stmt_p);
	  if (hint_type <= HT_INVAL || hint_type > HT_EOF)
	    {
	      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Unsupported hint type. (hint_type:%d). context(%s).", hint_type,
			 proxy_str_context (ctx_p));

	      proxy_context_set_error (ctx_p, CAS_ERROR_INDICATOR, CAS_ER_INTERNAL);

	      proxy_event_free (event_p);
	      event_p = NULL;

	      error = -1;
	      goto end;
	    }

	  shard_id = proxy_get_shard_id (stmt_p, NULL, &range_p);
	  if (shard_id == PROXY_INVALID_SHARD)
	    {
	      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Invalid shard id. (shard_id:%d). context(%s).", shard_id,
			 proxy_str_context (ctx_p));

	      proxy_context_set_error (ctx_p, CAS_ERROR_INDICATOR, CAS_ER_INTERNAL);

	      /* wakeup and reset statment */
	      shard_stmt_check_waiter_and_wakeup (ctx_p->prepared_stmt);
	      shard_stmt_free (ctx_p->prepared_stmt);
	      ctx_p->prepared_stmt = NULL;

	      proxy_event_free (event_p);
	      event_p = NULL;

	      error = -1;
	      goto end;
	    }

	  if (ctx_p->shard_id != PROXY_INVALID_SHARD && ctx_p->shard_id != shard_id)
	    {
	      PROXY_LOG (PROXY_LOG_MODE_ERROR,
			 "Shard id couldn't be changed in a transaction. "
			 "(prev_shard_id:%d, curr_shard_id:%d). context(%s).", ctx_p->shard_id, shard_id,
			 proxy_str_context (ctx_p));

	      proxy_context_set_error (ctx_p, CAS_ERROR_INDICATOR, CAS_ER_INTERNAL);

	      EXIT_FUNC ();
	      goto free_context;
	    }

	  ctx_p->shard_id = shard_id;
	}

      proxy_set_wait_timeout (ctx_p, query_timeout);
    }
  else
    {
      if (proxy_info_p->ignore_shard_hint == OFF && ctx_p->shard_id == PROXY_INVALID_SHARD)
	{
	  assert (false);

	  proxy_event_free (event_p);
	  event_p = NULL;

	  error = -1;
	  goto end;
	}

      if (ctx_p->prepared_stmt->stmt_type != SHARD_STMT_TYPE_EXCLUSIVE)
	{
	  assert (false);

	  PROXY_LOG (PROXY_LOG_MODE_ERROR, "Unexpected statement type. expect(%d), statement(%s). context(%s).",
		     SHARD_STMT_TYPE_SCHEMA_INFO, shard_str_stmt (ctx_p->prepared_stmt), proxy_str_context (ctx_p));

	  proxy_event_free (event_p);
	  event_p = NULL;

	  error = -1;
	  goto end;
	}
    }

  if (stmt_p == NULL)
    {
      if (ctx_p->prepared_stmt == NULL)
	{
	  assert (false);
	  PROXY_LOG (PROXY_LOG_MODE_ERROR, "No prepared statement to execute");

	  proxy_context_set_error (ctx_p, CAS_ERROR_INDICATOR, CAS_ER_INTERNAL);
	  EXIT_FUNC ();
	  goto free_context;
	}

      stmt_p = ctx_p->prepared_stmt;
      (void) proxy_get_shard_id (stmt_p, NULL, &range_p);
    }

  cas_io_p =
    proxy_cas_alloc_by_ctx (ctx_p->client_id, ctx_p->shard_id, ctx_p->cas_id, ctx_p->cid, ctx_p->uid,
			    ctx_p->wait_timeout, func_code);
  if (cas_io_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to allocate CAS. context(%s).", proxy_str_context (ctx_p));

      proxy_context_set_error (ctx_p, CAS_ERROR_INDICATOR, CAS_ER_INTERNAL);

      EXIT_FUNC ();
      error = -1;
      goto end;
    }
  else if (cas_io_p == (T_CAS_IO *) SHARD_TEMPORARY_UNAVAILABLE)
    {
      /* waiting idle shard/cas */
      ctx_p->waiting_event = event_p;
      event_p = NULL;

      EXIT_FUNC ();
      error = 0;
      goto end;
    }

  proxy_context_set_in_tran (ctx_p, cas_io_p->shard_id, cas_io_p->cas_id);

  /* If we will fail to execute query, then we have to retry. */
  ctx_p->waiting_event = proxy_event_dup (event_p);
  if (ctx_p->waiting_event == NULL)
    {
      goto free_context;
    }

  ctx_p->stmt_hint_type = hint_type;
  ctx_p->stmt_h_id = stmt_p->stmt_h_id;

  if (proxy_info_p->ignore_shard_hint == OFF)
    {
      /* update shard statistics */
      if (stmt_p)
	{
	  proxy_update_shard_stats (stmt_p, range_p);
	}
      else
	{
	  assert (false);
	}
    }
  else
    {
      proxy_update_shard_stats_without_hint (ctx_p->shard_id);
    }

  error = proxy_cas_io_write (cas_io_p, event_p);
  if (error)
    {
      goto free_context;
    }

  ctx_p->func_code = func_code;

end:
  EXIT_FUNC ();
  return error;

free_context:
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
fn_proxy_client_not_supported (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p, int argc, char **argv)
{
  proxy_context_set_error (ctx_p, CAS_ERROR_INDICATOR, CAS_ER_NOT_IMPLEMENTED);

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

  ctx_p->dont_free_statement = false;

  if (ctx_p->free_on_client_io_write)
    {
      PROXY_DEBUG_LOG ("Free context on client io write. " "context will be terminated. context(%s).",
		       proxy_str_context (ctx_p));

      error =
	proxy_send_response_to_client_with_new_event (ctx_p, PROXY_EVENT_IO_WRITE, PROXY_EVENT_FROM_CLIENT,
						      proxy_io_make_con_close_ok);

      goto free_event;
    }
  else if (ctx_p->free_on_end_tran)
    {
      PROXY_DEBUG_LOG ("Free context on end tran. " "context will be terminated. context(%s).",
		       proxy_str_context (ctx_p));

      ctx_p->free_context = true;

      error = 0 /* no error */ ;
      goto free_event;
    }

  error = proxy_send_response_to_client (ctx_p, event_p);
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to response to the client. " "(error=%d). context(%s). evnet(%s).",
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

  /* Protocol Phase 2 : PREPARED COLUMN COUNT */
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

  T_SHARD_STMT *stmt_p;

  char *response_p;

  char *driver_info;
  T_BROKER_VERSION client_version;

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

  driver_info = proxy_get_driver_info_by_ctx (ctx_p);
  client_version = CAS_MAKE_PROTO_VER (driver_info);

  error_ind = proxy_check_cas_error (response_p);
  if (error_ind < 0)
    {
      error_code = proxy_get_cas_error_code (response_p, client_version);

      PROXY_LOG (PROXY_LOG_MODE_NOTICE, "CAS response error. " "(error_ind:%d, error_code:%d). context(%s).", error_ind,
		 error_code, proxy_str_context (ctx_p));
      /* relay error to the client */
      error = proxy_send_response_to_client (ctx_p, event_p);
      if (error)
	{
	  PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to response to the client. " "(error=%d). context(%s). evnet(%s).",
		     error, proxy_str_context (ctx_p), proxy_str_event (event_p));

	  /* statement waiters may be woken up when freeing context */
	  goto free_context;
	}
      event_p = NULL;

      /* if stmt's status is SHARD_STMT_STATUS_IN_PROGRESS, wake up waiters and free statement. */
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

      if (ctx_p->waiting_event)
	{
	  proxy_event_free (ctx_p->waiting_event);
	  ctx_p->waiting_event = NULL;
	}

      EXIT_FUNC ();
      return 0;
    }

  stmt_p = ctx_p->prepared_stmt;
  if (stmt_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Invalid statement. Statement couldn't be NULL. context(%s).",
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
      memcpy ((char *) stmt_p->reply_buffer + srv_h_id_offset, (char *) &stmt_h_id_n, NET_SIZE_INT);
    }
  else
    {
      if (proxy_has_different_column_info
	  (event_p->buffer.data, event_p->buffer.length, (const char *) stmt_p->reply_buffer,
	   stmt_p->reply_buffer_length))
	{
	  PROXY_LOG (PROXY_LOG_MODE_DEBUG, "Invalid statement. schema info is different. context(%s). " "stmt(%s).",
		     proxy_str_context (ctx_p), shard_str_stmt (stmt_p));

	  assert (stmt_p->status == SHARD_STMT_STATUS_COMPLETE);

	  ctx_p->is_prepare_for_execute = false;

	  if (ctx_p->waiting_event)
	    {
	      proxy_event_free (ctx_p->waiting_event);
	      ctx_p->waiting_event = NULL;
	    }

	  shard_stmt_set_status_invalid (stmt_p->stmt_h_id);

	  proxy_event_free (event_p);
	  event_p =
	    proxy_event_new_with_error (driver_info, PROXY_EVENT_IO_WRITE, PROXY_EVENT_FROM_CLIENT,
					proxy_io_make_error_msg, CAS_ERROR_INDICATOR, CAS_ER_STMT_POOLING, "",
					ctx_p->is_client_in_tran);
	  if (event_p == NULL)
	    {
	      goto free_context;
	    }

	  /* relay error to the client */
	  error = proxy_send_response_to_client (ctx_p, event_p);
	  if (error)
	    {
	      PROXY_LOG (PROXY_LOG_MODE_ERROR,
			 "Failed to response to the client. " "(error=%d). context(%s). evnet(%s).", error,
			 proxy_str_context (ctx_p), proxy_str_event (event_p));

	      /* statement waiters may be woken up when freeing context */
	      goto free_context;
	    }

	  EXIT_FUNC ();
	  return 0;
	}
    }

  /* save server handle id for this statement from shard/cas */
  srv_h_id = error_ind;
  error = shard_stmt_add_srv_h_id_for_shard_cas (stmt_p->stmt_h_id, ctx_p->shard_id, ctx_p->cas_id, srv_h_id);
  if (error < 0)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Failed to save server handle " "id to the statement " "(srv_h_id:%d). statement(%s). context(%s). ",
		 srv_h_id, shard_str_stmt (stmt_p), proxy_str_context (ctx_p));
      goto free_context;
    }

  if (ctx_p->is_prepare_for_execute)
    {
      /*
       * after finishing prepare_for_execute, process previous client exeucte
       * request again.
       */

      ctx_p->prepared_stmt = NULL;
      ctx_p->is_prepare_for_execute = false;

      assert (ctx_p->waiting_event);	// __FOR_DEBUG
      error = shard_queue_enqueue (&proxy_Handler.cli_ret_q, (void *) ctx_p->waiting_event);

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
  else
    {
      /*
       * after dummy prepare,
       * we must not free(unpin) statment event though tran status is OUT_TRAN.
       */
      ctx_p->dont_free_statement = true;
    }

  /* The following codes does not use */

  ctx_p->prepared_stmt = NULL;

  /* REPLACE SRV_H_ID */
  stmt_h_id_n = htonl (stmt_p->stmt_h_id);
  srv_h_id_pos = (char *) (response_p + srv_h_id_offset);
  memcpy (srv_h_id_pos, &stmt_h_id_n, NET_SIZE_INT);

  PROXY_LOG (PROXY_LOG_MODE_SHARD_DETAIL, "Replace server handle id. " "(requested_srv_h_id:%d, saved_srv_h_id:%d).",
	     srv_h_id, stmt_p->stmt_h_id);

  error = proxy_send_response_to_client (ctx_p, event_p);
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to response to the client. " "(error=%d). context(%s). evnet(%s).",
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

  return proxy_cas_execute_internal (ctx_p, event_p);
}

static int
proxy_cas_execute_internal (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p)
{
  int error;
  int error_ind;
  int error_code = 0;
  bool is_in_tran;
  char *response_p;
  char *driver_info;
  T_BROKER_VERSION client_version;

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
  if (error_ind < 0)
    {
      driver_info = proxy_get_driver_info_by_ctx (ctx_p);
      client_version = CAS_MAKE_PROTO_VER (driver_info);
      error_code = proxy_get_cas_error_code (response_p, client_version);

      PROXY_LOG (PROXY_LOG_MODE_NOTICE, "CAS response error. " "(error_ind:%d, error_code:%d). context(%s).", error_ind,
		 error_code, proxy_str_context (ctx_p));

      assert (ctx_p->stmt_h_id >= 0);

      shard_stmt_del_srv_h_id_for_shard_cas (ctx_p->stmt_h_id, ctx_p->shard_id, ctx_p->cas_id);

      if (proxy_is_invalid_statement (error_ind, error_code, driver_info, client_version))
	{
	  shard_stmt_set_status_invalid (ctx_p->stmt_h_id);
	}
    }

  if (proxy_info_p->ignore_shard_hint == OFF)
    {
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
	  PROXY_LOG (PROXY_LOG_MODE_NOTICE, "Unsupported statement hint type. " "(hint_type:%d). context(%s).",
		     ctx_p->stmt_hint_type, proxy_str_context (ctx_p));
	}
    }
  else
    {
      proxy_info_p->num_hint_none_queries_processed++;
    }

  ctx_p->stmt_hint_type = HT_INVAL;
  ctx_p->stmt_h_id = SHARD_STMT_INVALID_HANDLE_ID;

  error = proxy_send_response_to_client (ctx_p, event_p);
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to response to the client. " "(error=%d). context(%s). evnet(%s).",
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
fn_proxy_cas_execute_array (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p)
{
  /* Protocol Phase 0 : MSG_HEADER */

  /* Protocol Phase 1 : GENERAL INFO */
  /* INT : RESULT_CODE */
  /* INT : NUM_QUERY */

  /* Protocol Phase 2 : QUERY RESULT(NUM_QEURY) */
  /* CASE 1 : QUERY SUCCESS */
  /* CHAR : STMT_TYPE */
  /* INT : TUPLE_COUNT */
  /* T_OBJECT : INS_OID */

  /* CASE 2 : QUERY FAIL */
  /* CHAR : STMT_TYPE */
  /* INT : ERROR_INDICATOR */
  /* INT : ERROR_CODE */
  /* INT : ERROR_MSG_LEN */
  /* STR : ERROR_MSG */

  return proxy_cas_execute_internal (ctx_p, event_p);
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
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to response to the client. " "(error=%d). context(%s). evnet(%s).",
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
fn_proxy_cas_schema_info (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p)
{
  int error, error_ind, error_code;
  int srv_h_id;
  int stmt_h_id_n;
  int srv_h_id_offset = MSG_HEADER_SIZE;
  char *srv_h_id_pos;
  T_SHARD_STMT *stmt_p;
  char *response_p;
  char *driver_info;
  T_BROKER_VERSION client_version;

  ENTER_FUNC ();

  response_p = event_p->buffer.data;

  error_ind = proxy_check_cas_error (response_p);
  if (error_ind < 0)
    {
      driver_info = proxy_get_driver_info_by_ctx (ctx_p);
      client_version = CAS_MAKE_PROTO_VER (driver_info);
      error_code = proxy_get_cas_error_code (response_p, client_version);

      PROXY_LOG (PROXY_LOG_MODE_NOTICE, "CAS response error. " "(error_ind:%d, error_code:%d). context(%s).", error_ind,
		 error_code, proxy_str_context (ctx_p));

      /* relay error to the client */
      error = proxy_send_response_to_client (ctx_p, event_p);
      if (error)
	{
	  PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to response to the client. " "(error=%d). context(%s). evnet(%s).",
		     error, proxy_str_context (ctx_p), proxy_str_event (event_p));

	  /* statement waiters may be woken up when freeing context */
	  goto free_context;
	}
      event_p = NULL;

      /* if stmt's status is SHARD_STMT_STATUS_IN_PROGRESS, wake up waiters and free statement. */
      assert (ctx_p->prepared_stmt != NULL);
      stmt_p = ctx_p->prepared_stmt;

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

      assert (ctx_p->waiting_event == NULL);
      EXIT_FUNC ();
      return 0;
    }

  stmt_p = ctx_p->prepared_stmt;
  if (stmt_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Invalid statement. Statement couldn't be NULL. context(%s).",
		 proxy_str_context (ctx_p));

      goto free_context;
    }

  /* save server handle id for this statement from shard/cas */
  srv_h_id = error_ind;
  error = shard_stmt_add_srv_h_id_for_shard_cas (stmt_p->stmt_h_id, ctx_p->shard_id, ctx_p->cas_id, srv_h_id);
  if (error < 0)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Failed to save server handle " "id to the statement " "(srv_h_id:%d). statement(%s). context(%s). ",
		 srv_h_id, shard_str_stmt (stmt_p), proxy_str_context (ctx_p));
      goto free_context;
    }

  ctx_p->prepared_stmt = NULL;

  /* replace server handle id */
  stmt_h_id_n = htonl (stmt_p->stmt_h_id);
  srv_h_id_pos = (char *) (response_p + srv_h_id_offset);
  memcpy (srv_h_id_pos, &stmt_h_id_n, NET_SIZE_INT);

  PROXY_LOG (PROXY_LOG_MODE_SHARD_DETAIL, "Replace server handle id. " "(requested_srv_h_id:%d, saved_srv_h_id:%d).",
	     srv_h_id, stmt_p->stmt_h_id);

  error = proxy_send_response_to_client (ctx_p, event_p);
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to response to the client. " "(error=%d). context(%s). evnet(%s).",
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
fn_proxy_cas_prepare_and_execute (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p)
{
  int error, error_ind, error_code;
  int srv_h_id;
  int stmt_h_id_n;
  int srv_h_id_offset = MSG_HEADER_SIZE;
  char *srv_h_id_pos;
  bool is_in_tran;
  char *response_p;
  char *driver_info;
  T_BROKER_VERSION client_version;
  T_SHARD_STMT *stmt_p = NULL;

  ENTER_FUNC ();

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

  response_p = event_p->buffer.data;

  error_ind = proxy_check_cas_error (response_p);
  if (error_ind < 0)
    {
      driver_info = proxy_get_driver_info_by_ctx (ctx_p);
      client_version = CAS_MAKE_PROTO_VER (driver_info);
      error_code = proxy_get_cas_error_code (response_p, client_version);

      PROXY_LOG (PROXY_LOG_MODE_NOTICE, "CAS response error. " "(error_ind:%d, error_code:%d). context(%s).", error_ind,
		 error_code, proxy_str_context (ctx_p));

      /* relay error to the client */
      error = proxy_send_response_to_client (ctx_p, event_p);
      if (error)
	{
	  PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to response to the client. " "(error=%d). context(%s). evnet(%s).",
		     error, proxy_str_context (ctx_p), proxy_str_event (event_p));

	  /* statement waiters may be woken up when freeing context */
	  goto free_context;
	}
      event_p = NULL;

      assert (ctx_p->prepared_stmt != NULL);
      stmt_p = ctx_p->prepared_stmt;

      if (stmt_p)
	{
	  /*
	   * there must be no context sharing this statement at this time.
	   * so, we can free statement.
	   */
	  shard_stmt_free (ctx_p->prepared_stmt);
	}
      ctx_p->prepared_stmt = NULL;

      assert (ctx_p->waiting_event == NULL);
      EXIT_FUNC ();
      return 0;
    }

  stmt_p = ctx_p->prepared_stmt;
  if (stmt_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Invalid statement. Statement couldn't be NULL. context(%s).",
		 proxy_str_context (ctx_p));

      goto free_context;
    }

  /* save server handle id for this statement from shard/cas */
  srv_h_id = error_ind;
  error = shard_stmt_add_srv_h_id_for_shard_cas (stmt_p->stmt_h_id, ctx_p->shard_id, ctx_p->cas_id, srv_h_id);
  if (error < 0)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Failed to save server handle " "id to the statement " "(srv_h_id:%d). statement(%s). context(%s). ",
		 srv_h_id, shard_str_stmt (stmt_p), proxy_str_context (ctx_p));
      goto free_context;
    }

  ctx_p->prepared_stmt = NULL;

  /* replace server handle id */
  stmt_h_id_n = htonl (stmt_p->stmt_h_id);
  srv_h_id_pos = (char *) (response_p + srv_h_id_offset);
  memcpy (srv_h_id_pos, &stmt_h_id_n, NET_SIZE_INT);

  PROXY_LOG (PROXY_LOG_MODE_SHARD_DETAIL, "Replace server handle id. " "(requested_srv_h_id:%d, saved_srv_h_id:%d).",
	     srv_h_id, stmt_p->stmt_h_id);

  if (proxy_info_p->ignore_shard_hint == OFF)
    {
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
	  PROXY_LOG (PROXY_LOG_MODE_NOTICE, "Unsupported statement hint type. " "(hint_type:%d). context(%s).",
		     ctx_p->stmt_hint_type, proxy_str_context (ctx_p));
	}
    }
  else
    {
      proxy_info_p->num_hint_none_queries_processed++;
    }

  ctx_p->stmt_hint_type = HT_INVAL;
  ctx_p->stmt_h_id = SHARD_STMT_INVALID_HANDLE_ID;

  error = proxy_send_response_to_client (ctx_p, event_p);
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to response to the client. " "(error=%d). context(%s). evnet(%s).",
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
fn_proxy_cas_check_cas (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p)
{
  int error = 0;
  int error_code = 0;
  int error_ind = 0;
  char *response_p = NULL;
  char *driver_info = NULL;
  T_BROKER_VERSION client_version;
  struct timeval client_start_time;
  T_PROXY_EVENT *new_event_p = NULL;

  gettimeofday (&client_start_time, NULL);

  if (ctx_p->waiting_event)
    {
      proxy_event_free (ctx_p->waiting_event);
      ctx_p->waiting_event = NULL;
    }

  response_p = event_p->buffer.data;
  driver_info = proxy_get_driver_info_by_ctx (ctx_p);

  if (event_p->buffer.length > MSG_HEADER_SIZE)
    {
      error_ind = proxy_check_cas_error (response_p);
      assert (error_ind < 0);

      client_version = CAS_MAKE_PROTO_VER (driver_info);
      error_code = proxy_get_cas_error_code (response_p, client_version);

      PROXY_LOG (PROXY_LOG_MODE_NOTICE, "CAS response error. " "(error_ind:%d, error_code:%d). context(%s).", error_ind,
		 error_code, proxy_str_context (ctx_p));

      error = proxy_send_response_to_client (ctx_p, event_p);
      if (error)
	{
	  PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to response to the client. " "(error=%d). context(%s). evnet(%s).",
		     error, proxy_str_context (ctx_p), proxy_str_event (event_p));

	  goto free_context;
	}
    }
  else
    {
      /* send dbinfo_ok to the client */
      new_event_p =
	proxy_event_new_with_rsp (driver_info, PROXY_EVENT_IO_WRITE, PROXY_EVENT_FROM_CLIENT,
				  proxy_io_make_client_dbinfo_ok);
      if (new_event_p == NULL)
	{
	  goto free_context;
	}

      if (event_p)
	{
	  proxy_event_free (event_p);
	  event_p = NULL;
	}

      error = proxy_send_response_to_client (ctx_p, new_event_p);
      if (error)
	{
	  PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to response to the client. " "(error=%d). context(%s). evnet(%s).",
		     error, proxy_str_context (ctx_p), proxy_str_event (new_event_p));

	  goto free_context;
	}
    }

  if (error_ind < 0)
    {
      ctx_p->free_on_client_io_write = true;
    }
  else
    {
      ctx_p->is_connected = true;
      proxy_io_set_established_by_ctx (ctx_p);
    }

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
    }

  ctx_p->free_context = true;
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
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to response to the client. " "(error=%d). context(%s). evnet(%s).",
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

      error =
	proxy_send_request_to_cas_with_new_event (ctx_p, PROXY_EVENT_IO_WRITE, PROXY_EVENT_FROM_CAS,
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

static bool
proxy_is_invalid_statement (int error_ind, int error_code, char *driver_info, T_BROKER_VERSION client_version)
{
  int new_error_code = 0;

  new_error_code = proxy_convert_error_code (error_ind, error_code, driver_info, client_version, PROXY_CONV_ERR_TO_NEW);

  if (client_version < CAS_MAKE_VER (8, 3, 0) && new_error_code == CAS_ER_STMT_POOLING)
    {
      return true;
    }
  else if (error_ind == CAS_ERROR_INDICATOR && new_error_code == CAS_ER_STMT_POOLING)
    {
      return true;
    }

  return false;
}

static bool
proxy_has_different_column_info (const char *r1, size_t r1_len, const char *r2, size_t r2_len)
{
  int ignore_size = MSG_HEADER_SIZE + /* srv_h_id */ NET_SIZE_INT
    + /* result_cache_lifetime */ NET_SIZE_INT;

  r1 += ignore_size;
  r2 += ignore_size;

  if (r1_len != r2_len)
    {
      return true;
    }

  return (memcmp (r1, r2, r1_len - ignore_size) != 0) ? true : false;
}
