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
 * cci_handle_mng.c -
 */

#ident "$Id$"

/************************************************************************
 * IMPORTED SYSTEM HEADER FILES						*
 ************************************************************************/
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#if defined(WINDOWS)
#include <winsock2.h>
#include <windows.h>
#else
#include <netdb.h>
#include <pthread.h>
#endif

/************************************************************************
 * OTHER IMPORTED HEADER FILES						*
 ************************************************************************/

#include "cci_common.h"
#include "cci_handle_mng.h"
#include "cas_cci.h"
#include "cci_util.h"
#include "cci_query_execute.h"
#include "cas_protocol.h"
#include "cci_network.h"
#include "cci_map.h"
#include "cci_ssl.h"
#include <cstdint>
/************************************************************************
 * PRIVATE DEFINITIONS							*
 ************************************************************************/

#define MAX_CON_HANDLE                  2048

#define REQ_HANDLE_ALLOC_SIZE           256

#define CCI_MAX_CONNECTION_POOL         256

/************************************************************************
 * PRIVATE TYPE DEFINITIONS						*
 ************************************************************************/

typedef struct
{
  T_ALTER_HOST host;		/* host info (ip, port) */
  bool is_reachable;
} T_HOST_STATUS;

static T_HOST_STATUS host_status[MAX_CON_HANDLE];
static int host_status_count = 0;

#if defined(WINDOWS)
HANDLE host_status_mutex;
#else
T_MUTEX host_status_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

static int conn_pool[CCI_MAX_CONNECTION_POOL];
static unsigned int num_conn_pool = 0;


/************************************************************************
 * PRIVATE FUNCTION PROTOTYPES						*
 ************************************************************************/

static int compare_conn_info (unsigned char *ip_addr, int port, char *dbname, char *dbuser, char *dbpasswd,
			      T_CON_HANDLE * con_handle);
static int init_con_handle (T_CON_HANDLE * con_handle, char *ip_str, int port, char *db_name, char *db_user,
			    char *db_passwd);
static int new_con_handle_id (void);
static int new_req_handle_id (T_CON_HANDLE * con_handle);
static void con_handle_content_free (T_CON_HANDLE * con_handle);
static void ipstr2uchar (char *ip_str, unsigned char *ip_addr);
static int is_ip_str (char *ip_str);

static int hm_find_host_status_index (unsigned char *ip_addr, int port);
static void hm_set_host_status_by_addr (unsigned char *ip_addr, int port, bool is_reachable);
static THREAD_RET_T THREAD_CALLING_CONVENTION hm_thread_health_checker (void *arg);

/************************************************************************
 * INTERFACE VARIABLES							*
 ************************************************************************/

/************************************************************************
 * PUBLIC VARIABLES							*
 ************************************************************************/

static T_CON_HANDLE *con_handle_table[MAX_CON_HANDLE];
static int con_handle_current_index;

/************************************************************************
 * PRIVATE VARIABLES							*
 ************************************************************************/

/************************************************************************
 * IMPLEMENTATION OF PUBLIC FUNCTIONS	 				*
 ************************************************************************/
static int
compare_conn_info (unsigned char *ip_addr, int port, char *dbname, char *dbuser, char *dbpasswd,
		   T_CON_HANDLE * con_handle)
{
  if (memcmp (ip_addr, con_handle->ip_addr, 4) != 0 || port != con_handle->port
      || strcmp (dbname, con_handle->db_name) != 0 || strcmp (dbuser, con_handle->db_user) != 0
      || strcmp (dbpasswd, con_handle->db_passwd) != 0)
    {
      return 0;
    }

  return 1;
}

T_CON_HANDLE *
hm_get_con_from_pool (unsigned char *ip_addr, int port, char *dbname, char *dbuser, char *dbpasswd)
{
  int con = -1;
  unsigned int i;

  for (i = 0; i < num_conn_pool; i++)
    {
      if (compare_conn_info (ip_addr, port, dbname, dbuser, dbpasswd, con_handle_table[conn_pool[i] - 1]))
	{
	  con = conn_pool[i];
	  conn_pool[i] = conn_pool[--num_conn_pool];
	  break;
	}
    }

  if (con < 0)
    {
      return NULL;
    }

  return con_handle_table[con - 1];
}

int
hm_put_con_to_pool (int con)
{
  if (num_conn_pool >= sizeof (conn_pool) / sizeof (int))
    {
      return -1;
    }
  conn_pool[num_conn_pool++] = con;
  return 0;
}

int
hm_ip_str_to_addr (char *ip_str, unsigned char *ip_addr)
{
  if (is_ip_str (ip_str))
    {
      ipstr2uchar (ip_str, ip_addr);
    }
  else
    {
      if (hostname2uchar (ip_str, ip_addr) < 0)
	{
	  return CCI_ER_HOSTNAME;
	}
    }
  return 0;
}

void
hm_con_handle_table_init ()
{
  int i;

  for (i = 0; i < MAX_CON_HANDLE; i++)
    {
      con_handle_table[i] = NULL;
    }

  con_handle_current_index = 0;
}

T_CON_HANDLE *
hm_con_handle_alloc (char *ip_str, int port, char *db_name, char *db_user, char *db_passwd)
{
  int handle_id;
  int error = 0;
  T_CON_HANDLE *con_handle = NULL;

  handle_id = new_con_handle_id ();
  if (handle_id <= 0)
    {
      goto error_end;
    }

  con_handle = (T_CON_HANDLE *) MALLOC (sizeof (T_CON_HANDLE));
  if (con_handle == NULL)
    {
      goto error_end;
    }
  error = init_con_handle (con_handle, ip_str, port, db_name, db_user, db_passwd);
  if (error < 0)
    {
      goto error_end;
    }

  con_handle_table[handle_id - 1] = con_handle;
  con_handle->id = handle_id;

  return con_handle;

error_end:
  hm_ssl_free (con_handle);
  FREE_MEM (con_handle);
  return NULL;
}

int
hm_con_handle_free (T_CON_HANDLE * con_handle)
{
  if (con_handle == NULL)
    {
      return CCI_ER_CON_HANDLE;
    }

  con_handle_table[con_handle->id - 1] = NULL;
  if (!IS_INVALID_SOCKET (con_handle->sock_fd))
    {
      hm_ssl_free (con_handle);
      CLOSE_SOCKET (con_handle->sock_fd);
      con_handle->sock_fd = INVALID_SOCKET;
    }

  hm_req_handle_free_all (con_handle);
  con_handle_content_free (con_handle);
  hm_ssl_free (con_handle);
  FREE_MEM (con_handle);

  return CCI_ER_NO_ERROR;
}

static int
hm_pool_add_node_to_list (T_REQ_HANDLE ** head, T_REQ_HANDLE ** tail, T_REQ_HANDLE * target)
{
  target->next = NULL;
  target->prev = *tail;
  if (*tail == NULL)
    {
      *head = target;
    }
  else
    {
      (*tail)->next = target;
    }
  *tail = target;

  return CCI_ER_NO_ERROR;
}

static int
hm_pool_drop_node_from_list (T_REQ_HANDLE ** head, T_REQ_HANDLE ** tail, T_REQ_HANDLE * target)
{
  T_REQ_HANDLE *prev_target, *next_target;

  assert (*head != NULL && *tail != NULL);

  if (*head == NULL || *tail == NULL)
    {
      return CCI_ER_REQ_HANDLE;
    }

  prev_target = (T_REQ_HANDLE *) target->prev;
  next_target = (T_REQ_HANDLE *) target->next;
  if (prev_target != NULL)
    {
      prev_target->next = next_target;
    }
  if (next_target != NULL)
    {
      next_target->prev = prev_target;
    }

  if (target == *head)
    {
      *head = next_target;
    }
  if (target == *tail)
    {
      *tail = prev_target;
    }

  return CCI_ER_NO_ERROR;
}

static int
hm_pool_move_node_from_use_to_lru (T_CON_HANDLE * connection, int statement_id)
{
  T_REQ_HANDLE *target;

  statement_id = statement_id % CON_HANDLE_ID_FACTOR;
  target = connection->req_handle_table[statement_id - 1];
  assert (target != NULL);

  if (target == NULL)
    {
      return CCI_ER_REQ_HANDLE;
    }

  /* cut from use */
  hm_pool_drop_node_from_list (&connection->pool_use_head, &connection->pool_use_tail, target);

  /* add to lru */
  hm_pool_add_node_to_list (&connection->pool_lru_head, &connection->pool_lru_tail, target);
  connection->open_prepared_statement_count++;

  return CCI_ER_NO_ERROR;
}

static int
hm_pool_move_node_from_lru_to_use (T_CON_HANDLE * connection, int statement_id)
{
  T_REQ_HANDLE *target;

  statement_id = statement_id % CON_HANDLE_ID_FACTOR;
  target = connection->req_handle_table[statement_id - 1];
  assert (target != NULL);

  if (target == NULL)
    {
      return CCI_ER_REQ_HANDLE;
    }

  /* cut from lru */
  hm_pool_drop_node_from_list (&connection->pool_lru_head, &connection->pool_lru_tail, target);
  connection->open_prepared_statement_count--;

  /* add to use */
  hm_pool_add_node_to_list (&connection->pool_use_head, &connection->pool_use_tail, target);

  return CCI_ER_NO_ERROR;
}

static T_REQ_HANDLE *
hm_pool_victimize_last_node_from_lru (T_CON_HANDLE * connection)
{
  T_REQ_HANDLE *victim;

  if (connection->pool_lru_head == NULL)
    {
      return NULL;
    }

  victim = connection->pool_lru_head;
  hm_pool_drop_node_from_list (&connection->pool_lru_head, &connection->pool_lru_tail, victim);

  connection->open_prepared_statement_count--;

  return victim;
}

int
hm_pool_restore_used_statements (T_CON_HANDLE * connection)
{
  T_REQ_HANDLE *r;

  r = connection->pool_use_head;
  while (r != NULL)
    {
      int statement = r->req_handle_index;

      req_handle_content_free_for_pool (r);
      r = (T_REQ_HANDLE *) r->next;
      hm_pool_move_node_from_use_to_lru (connection, statement);
    }

  connection->pool_use_head = NULL;
  connection->pool_use_tail = NULL;

  return CCI_ER_NO_ERROR;
}

int
hm_pool_add_statement_to_use (T_CON_HANDLE * connection, int statement_id)
{
  T_REQ_HANDLE *statement;

  statement_id = statement_id % CON_HANDLE_ID_FACTOR;
  statement = connection->req_handle_table[statement_id - 1];
  assert (statement != NULL);

  hm_pool_add_node_to_list (&connection->pool_use_head, &connection->pool_use_tail, statement);

  return CCI_ER_NO_ERROR;
}

int
hm_req_handle_alloc (T_CON_HANDLE * con_handle, T_REQ_HANDLE ** ret_req_handle)
{
  int req_handle_id;
  T_REQ_HANDLE *req_handle = NULL;

  *ret_req_handle = NULL;

  req_handle_id = new_req_handle_id (con_handle);
  if (req_handle_id < 0)
    {
      return (req_handle_id);
    }

  req_handle = (T_REQ_HANDLE *) MALLOC (sizeof (T_REQ_HANDLE));
  if (req_handle == NULL)
    {
      return CCI_ER_NO_MORE_MEMORY;
    }

  memset (req_handle, 0, sizeof (T_REQ_HANDLE));
  req_handle->req_handle_index = req_handle_id;
  req_handle->mapped_stmt_id = -1;
  req_handle->fetch_size = 100;
  req_handle->query_timeout = con_handle->query_timeout;
  req_handle->shard_id = CCI_SHARD_ID_INVALID;
  req_handle->is_fetch_completed = 0;

  con_handle->req_handle_table[req_handle_id - 1] = req_handle;
  ++(con_handle->req_handle_count);

  *ret_req_handle = req_handle;
  return MAKE_REQ_ID (con_handle->id, req_handle_id);
}

int
hm_req_add_to_pool (T_CON_HANDLE * con, char *sql, int mapped_statement_id, T_REQ_HANDLE * statement)
{
  char *key;
  int *data;

  if (sql == NULL)
    {
      assert (sql != NULL);
      return CCI_ER_REQ_HANDLE;
    }

  data = (int *) cci_mht_get (con->stmt_pool, sql);
  if (data != NULL)
    {
      hm_pool_drop_node_from_list (&con->pool_use_head, &con->pool_use_tail, statement);
      return CCI_ER_REQ_HANDLE;
    }

  if (HAS_REACHED_LIMIT_OPEN_STATEMENT (con))
    {
      T_REQ_HANDLE *victim = hm_pool_victimize_last_node_from_lru (con);
      if (victim == NULL)
	{
	  return CCI_ER_REQ_HANDLE;
	}

      if (victim->handle_type == HANDLE_PREPARE || victim->handle_type == HANDLE_SCHEMA_INFO)
	{
	  /* because the statement will be terminated by restarting cas all errors of qe_close_req_handle() are ignored */
	  qe_close_req_handle (victim, con);
	}
      cci_mht_rem (con->stmt_pool, victim->sql_text, true, true);
      hm_req_handle_free (con, victim);
      victim = NULL;
    }

  key = strdup (sql);
  if (key == NULL)
    {
      return CCI_ER_NO_MORE_MEMORY;
    }
  data = (int *) MALLOC (sizeof (int));
  if (data == NULL)
    {
      FREE (key);
      return CCI_ER_NO_MORE_MEMORY;
    }

  if (map_get_ots_value (mapped_statement_id, data, true) != CCI_ER_NO_ERROR)
    {
      FREE (key);
      FREE (data);

      return CCI_ER_REQ_HANDLE;
    }

  if (!cci_mht_put_data (con->stmt_pool, key, data))
    {
      FREE (key);
      FREE (data);
      return CCI_ER_NO_MORE_MEMORY;
    }

  hm_pool_move_node_from_use_to_lru (con, *data);

  return CCI_ER_NO_ERROR;
}

int
hm_req_get_from_pool (T_CON_HANDLE * con, T_REQ_HANDLE ** req, const char *sql)
{
  int req_id;
  void *data;

  data = cci_mht_rem (con->stmt_pool, sql, true, false);
  if (data == NULL)
    {
      return CCI_ER_REQ_HANDLE;
    }
  req_id = *((int *) data);
  FREE_MEM (data);

  hm_pool_move_node_from_lru_to_use (con, req_id);

  if (req != NULL)
    {
      *req = con->req_handle_table[GET_REQ_ID (req_id) - 1];
      if (*req == NULL)
	{
	  return CCI_ER_REQ_HANDLE;
	}
    }

  return req_id;
}

T_CCI_ERROR_CODE
hm_get_connection_by_resolved_id (int resolved_id, T_CON_HANDLE ** connection)
{
  if (connection == NULL || con_handle_table[resolved_id - 1] == NULL)
    {
      return CCI_ER_CON_HANDLE;
    }

  *connection = con_handle_table[resolved_id - 1];
  assert (*connection != NULL);
  return CCI_ER_NO_ERROR;
}

static T_CCI_ERROR_CODE
hm_get_connection_internal (int mapped_id, T_CON_HANDLE ** connection, bool force)
{
  T_CCI_ERROR_CODE error;
  int connection_id;

  if (connection == NULL)
    {
      return CCI_ER_CON_HANDLE;
    }
  *connection = NULL;

  error = map_get_otc_value (mapped_id, &connection_id, force);
  if (error != CCI_ER_NO_ERROR)
    {
      return error;
    }
  if (connection_id < 1 || connection_id > MAX_CON_HANDLE)
    {
      return CCI_ER_CON_HANDLE;
    }

  *connection = con_handle_table[connection_id - 1];
  if (*connection == NULL)
    {
      return CCI_ER_CON_HANDLE;
    }

  return CCI_ER_NO_ERROR;
}

T_CCI_ERROR_CODE
hm_get_connection_force (int mapped_id, T_CON_HANDLE ** connection)
{
  return hm_get_connection_internal (mapped_id, connection, true);
}

T_CCI_ERROR_CODE
hm_get_connection (int mapped_id, T_CON_HANDLE ** connection)
{
  return hm_get_connection_internal (mapped_id, connection, false);
}

T_CCI_ERROR_CODE
hm_get_statement (int mapped_id, T_CON_HANDLE ** connection, T_REQ_HANDLE ** statement)
{
  int connection_id;
  int statement_id;
  T_CON_HANDLE *conn;
  T_CCI_ERROR_CODE error;

  if (connection != NULL)
    {
      *connection = NULL;
    }

  if (statement == NULL)
    {
      return CCI_ER_REQ_HANDLE;
    }
  *statement = NULL;

  error = map_get_ots_value (mapped_id, &statement_id, false);
  if (error != CCI_ER_NO_ERROR)
    {
      return error;
    }

  connection_id = GET_CON_ID (statement_id);
  statement_id = GET_REQ_ID (statement_id);
  if (connection_id < 1 || statement_id < 1)
    {
      return CCI_ER_REQ_HANDLE;
    }

  conn = con_handle_table[connection_id - 1];
  if (conn == NULL)
    {
      return CCI_ER_REQ_HANDLE;
    }

  if (statement_id > conn->max_req_handle)
    {
      return CCI_ER_REQ_HANDLE;
    }

  *statement = conn->req_handle_table[statement_id - 1];
  if (connection != NULL)
    {
      *connection = conn;
    }

  assert (*statement != NULL);
  return CCI_ER_NO_ERROR;
}

static T_CCI_ERROR_CODE
hm_release_connection_internal (int mapped_id, T_CON_HANDLE ** connection, bool delete_handle)
{
  T_CCI_ERROR_CODE error;

  error = map_close_otc (mapped_id);
  if (error != CCI_ER_NO_ERROR)
    {
      return error;
    }

  if (delete_handle)
    {
      error = (T_CCI_ERROR_CODE) hm_con_handle_free (*connection);
      *connection = NULL;
    }

  return error;
}

T_CCI_ERROR_CODE
hm_release_connection (int mapped_id, T_CON_HANDLE ** connection)
{
  return hm_release_connection_internal (mapped_id, connection, false);
}

T_CCI_ERROR_CODE
hm_delete_connection (int mapped_id, T_CON_HANDLE ** connection)
{
  return hm_release_connection_internal (mapped_id, connection, true);
}

T_CCI_ERROR_CODE
hm_release_statement (int mapped_id, T_CON_HANDLE ** connection, T_REQ_HANDLE ** statement)
{
  T_CCI_ERROR_CODE error;

  if (*statement)
    {
      assert (mapped_id == (*statement)->mapped_stmt_id);
      (*statement)->mapped_stmt_id = -1;
    }

  error = map_close_ots (mapped_id);
  if (error != CCI_ER_NO_ERROR)
    {
      return error;
    }

  *connection = NULL;
  *statement = NULL;

  return error;
}

void
hm_req_handle_free (T_CON_HANDLE * con_handle, T_REQ_HANDLE * req_handle)
{
  con_handle->req_handle_table[req_handle->req_handle_index - 1] = NULL;
  --(con_handle->req_handle_count);

  req_handle_content_free (req_handle, 0);
  FREE_MEM (req_handle);
}

void
hm_req_handle_free_all (T_CON_HANDLE * con_handle)
{
  int i;
  T_REQ_HANDLE *req_handle = NULL;

  for (i = 0; i < con_handle->max_req_handle; i++)
    {
      req_handle = con_handle->req_handle_table[i];
      if (req_handle == NULL)
	{
	  continue;
	}
      req_handle_content_free (req_handle, 0);
      FREE_MEM (req_handle);
      con_handle->req_handle_table[i] = NULL;
      --(con_handle->req_handle_count);
    }
}

void
hm_req_handle_free_all_unholdable (T_CON_HANDLE * con_handle)
{
  int i;
  T_REQ_HANDLE *req_handle = NULL;

  for (i = 0; i < con_handle->max_req_handle; i++)
    {
      req_handle = con_handle->req_handle_table[i];
      if (req_handle == NULL)
	{
	  continue;
	}
      if ((req_handle->prepare_flag & CCI_PREPARE_HOLDABLE) != 0)
	{
	  /* do not free holdable req_handles */
	  continue;
	}
      req_handle_content_free (req_handle, 0);
      FREE_MEM (req_handle);
      con_handle->req_handle_table[i] = NULL;
      --(con_handle->req_handle_count);
    }
}

void
hm_req_handle_close_all_resultsets (T_CON_HANDLE * con_handle)
{
  int i;
  T_REQ_HANDLE *req_handle = NULL;

  for (i = 0; i < con_handle->max_req_handle; i++)
    {
      req_handle = con_handle->req_handle_table[i];
      if (req_handle == NULL)
	{
	  continue;
	}

      if ((req_handle->prepare_flag & CCI_PREPARE_HOLDABLE) != 0 && !req_handle->is_from_current_transaction)
	{
	  continue;
	}

      req_handle->is_closed = 1;
    }
}

void
hm_req_handle_close_all_unholdable_resultsets (T_CON_HANDLE * con_handle)
{
  int i;
  T_REQ_HANDLE *req_handle = NULL;

  for (i = 0; i < con_handle->max_req_handle; i++)
    {
      req_handle = con_handle->req_handle_table[i];
      if (req_handle == NULL)
	{
	  continue;
	}

      if ((req_handle->prepare_flag & CCI_PREPARE_HOLDABLE) != 0)
	{
	  /* skip holdable req_handles */
	  req_handle->is_from_current_transaction = 0;
	  continue;
	}

      req_handle->is_closed = 1;
    }
}

void
hm_req_handle_fetch_buf_free (T_REQ_HANDLE * req_handle)
{
  int i, fetched_tuple;

  if (req_handle->tuple_value)
    {
      fetched_tuple = req_handle->fetched_tuple_end - req_handle->fetched_tuple_begin + 1;
      for (i = 0; i < fetched_tuple; i++)
	{
#if defined(WINDOWS)
	  int j;

	  for (j = 0; j < req_handle->num_col_info; j++)
	    {
	      FREE_MEM (req_handle->tuple_value[i].decoded_ptr[j]);
	    }

	  FREE_MEM (req_handle->tuple_value[i].decoded_ptr);
#endif
	  FREE_MEM (req_handle->tuple_value[i].column_ptr);
	}
      FREE_MEM (req_handle->tuple_value);
    }
  FREE_MEM (req_handle->msg_buf);
  req_handle->fetched_tuple_begin = req_handle->fetched_tuple_end = 0;
  req_handle->cur_fetch_tuple_index = -1;
  req_handle->is_fetch_completed = 0;
}

int
hm_conv_value_buf_alloc (T_VALUE_BUF * val_buf, int size)
{
  if (size <= val_buf->size)
    {
      return 0;
    }

  FREE_MEM (val_buf->data);
  val_buf->size = 0;

  val_buf->data = MALLOC (size);
  if (val_buf->data == NULL)
    {
      return CCI_ER_NO_MORE_MEMORY;
    }

  val_buf->size = size;
  return 0;
}

void
hm_invalidate_all_req_handle (T_CON_HANDLE * con_handle)
{

  int i;
  int count = 0;
  T_REQ_HANDLE *curr_req_handle;

  for (i = 0; i < con_handle->max_req_handle; ++i)
    {
      if (count == con_handle->req_handle_count)
	{
	  break;
	}

      curr_req_handle = con_handle->req_handle_table[i];
      if (curr_req_handle == NULL)
	{
	  continue;
	}

      curr_req_handle->valid = 0;
      curr_req_handle->shard_id = CCI_SHARD_ID_INVALID;
      ++count;
    }
}

void
hm_conv_value_buf_clear (T_VALUE_BUF * val_buf)
{
  FREE_MEM (val_buf->data);
  val_buf->size = 0;
}

void
req_handle_col_info_free (T_REQ_HANDLE * req_handle)
{
  int i;

  if (req_handle->col_info)
    {
      for (i = 0; i < req_handle->num_col_info; i++)
	{
	  FREE_MEM (req_handle->col_info[i].col_name);
	  FREE_MEM (req_handle->col_info[i].real_attr);
	  FREE_MEM (req_handle->col_info[i].class_name);
	  FREE_MEM (req_handle->col_info[i].default_value);
	}
      FREE_MEM (req_handle->col_info);
    }
}

void
req_handle_content_free (T_REQ_HANDLE * req_handle, int reuse)
{
  /*
   * For reusing invalidated req handle, sql_text and prepare flag of req handle are needed. So, they must not be
   * freed. */

  req_close_query_result (req_handle);
  req_handle_col_info_free (req_handle);
  req_handle->shard_id = CCI_SHARD_ID_INVALID;
  req_handle->is_fetch_completed = 0;

  if (!reuse)
    {
      FREE_MEM (req_handle->sql_text);

      qe_bind_value_free (req_handle);
      FREE_MEM (req_handle->bind_mode);
      FREE_MEM (req_handle->bind_value);
    }
  req_handle->valid = 0;
}

void
req_handle_content_free_for_pool (T_REQ_HANDLE * req_handle)
{
  req_close_query_result (req_handle);
  qe_bind_value_free (req_handle);
}

int
req_close_query_result (T_REQ_HANDLE * req_handle)
{
  assert (req_handle != NULL);

  hm_req_handle_fetch_buf_free (req_handle);
  hm_conv_value_buf_clear (&(req_handle->conv_value_buffer));

  if (req_handle->num_query_res == 0 || req_handle->qr == NULL)
    {
      assert (req_handle->num_query_res == 0 && req_handle->qr == NULL);

      return CCI_ER_RESULT_SET_CLOSED;
    }

  QUERY_RESULT_FREE (req_handle);

  return CCI_ER_NO_ERROR;
}

static int
hm_find_host_status_index (unsigned char *ip_addr, int port)
{
  int i, index = -1;

  for (i = 0; i < host_status_count; i++)
    {
      if (memcmp (host_status[i].host.ip_addr, ip_addr, 4) == 0 && host_status[i].host.port == port)
	{
	  index = i;
	  break;
	}
    }

  return index;
}


#if defined (ENABLE_UNUSED_FUNCTION)
int
hm_get_ha_connected_host (T_CON_HANDLE * con_handle)
{
  int i, cur_host_id = -1;

  MUTEX_LOCK (ha_status_mutex);
  i = hm_find_ha_status_index (con_handle);

  if (i >= 0)
    {
      cur_host_id = ha_status[i].cur_host_id;
    }

  MUTEX_UNLOCK (ha_status_mutex);
  return cur_host_id;
}
#endif

bool
hm_is_host_reachable (T_CON_HANDLE * con_handle, int host_id)
{
  int i;
  unsigned char *ip_addr = con_handle->alter_hosts[host_id].ip_addr;
  int port = con_handle->alter_hosts[host_id].port;
  bool is_reachable = true;

  i = hm_find_host_status_index (ip_addr, port);
  if (i >= 0)
    {
      is_reachable = host_status[i].is_reachable;
    }

  return is_reachable;
}

void
hm_set_host_status (T_CON_HANDLE * con_handle, int host_id, bool is_reachable)
{
  unsigned char *ip_addr = con_handle->alter_hosts[host_id].ip_addr;
  int port = con_handle->alter_hosts[host_id].port;

  hm_set_host_status_by_addr (ip_addr, port, is_reachable);
  if (!is_reachable)
    {
      con_handle->last_failure_time = time (NULL);
    }
}

void
hm_set_con_handle_holdable (T_CON_HANDLE * con_handle, int holdable)
{
  con_handle->is_holdable = holdable;
}

int
hm_get_con_handle_holdable (T_CON_HANDLE * con_handle)
{
  T_BROKER_VERSION broker;

  if (con_handle->is_holdable)
    {
      broker = hm_get_broker_version (con_handle);

      if (hm_broker_support_holdable_result (con_handle) || broker == CAS_PROTO_MAKE_VER (PROTOCOL_V2))
	{
	  return true;
	}
    }

  return false;
}

int
hm_get_req_handle_holdable (T_CON_HANDLE * con_handle, T_REQ_HANDLE * req_handle)
{
  T_BROKER_VERSION broker;

  assert (con_handle != NULL && req_handle != NULL);

  if ((req_handle->prepare_flag & CCI_PREPARE_HOLDABLE) != 0)
    {
      broker = hm_get_broker_version (con_handle);

      if (hm_broker_support_holdable_result (con_handle) || broker == CAS_PROTO_MAKE_VER (PROTOCOL_V2))
	{
	  return true;
	}
    }

  return false;
}

T_BROKER_VERSION
hm_get_broker_version (T_CON_HANDLE * con_handle)
{
  T_BROKER_VERSION version = 0;

  if (con_handle->broker_info[BROKER_INFO_PROTO_VERSION] & CAS_PROTO_INDICATOR)
    {
      version = CAS_PROTO_UNPACK_NET_VER (con_handle->broker_info[BROKER_INFO_PROTO_VERSION]);
    }
  else
    {
      version =
	CAS_MAKE_VER (con_handle->broker_info[BROKER_INFO_MAJOR_VERSION],
		      con_handle->broker_info[BROKER_INFO_MINOR_VERSION],
		      con_handle->broker_info[BROKER_INFO_PATCH_VERSION]);
    }

  return version;
}

bool
hm_broker_understand_renewed_error_code (T_CON_HANDLE * con_handle)
{
  char f = con_handle->broker_info[BROKER_INFO_FUNCTION_FLAG];
  char p = con_handle->broker_info[BROKER_INFO_PROTO_VERSION];

  if ((p & CAS_PROTO_INDICATOR) != CAS_PROTO_INDICATOR)
    {
      return false;
    }

  return (f & BROKER_RENEWED_ERROR_CODE) == BROKER_RENEWED_ERROR_CODE;
}

bool
hm_broker_understand_the_protocol (T_BROKER_VERSION broker_version, int require)
{
  if (broker_version >= CAS_PROTO_MAKE_VER (require))
    {
      return true;
    }
  else
    {
      return false;
    }
}

bool
hm_broker_match_the_protocol (T_BROKER_VERSION broker_version, int require)
{
  if (broker_version == CAS_PROTO_MAKE_VER (require))
    {
      return true;
    }
  else
    {
      return false;
    }
}

bool
hm_broker_support_holdable_result (T_CON_HANDLE * con_handle)
{
  char f = con_handle->broker_info[BROKER_INFO_FUNCTION_FLAG];

  return (f & BROKER_SUPPORT_HOLDABLE_RESULT) == BROKER_SUPPORT_HOLDABLE_RESULT;
}

bool
hm_broker_reconnect_when_server_down (T_CON_HANDLE * con_handle)
{
  char f = con_handle->broker_info[BROKER_INFO_FUNCTION_FLAG];

  return (f & BROKER_RECONNECT_WHEN_SERVER_DOWN) == BROKER_RECONNECT_WHEN_SERVER_DOWN;
}

void
hm_check_rc_time (T_CON_HANDLE * con_handle)
{
  time_t cur_time, failure_time;

  if (IS_INVALID_SOCKET (con_handle->sock_fd))
    {
      return;
    }

  if (con_handle->alter_host_id > 0 && con_handle->rc_time > 0)
    {
      cur_time = time (NULL);
      failure_time = con_handle->last_failure_time;
      if (failure_time > 0 && con_handle->rc_time < (cur_time - failure_time))
	{
	  if (hm_is_host_reachable (con_handle, 0))
	    {
	      con_handle->force_failback = true;
	      con_handle->last_failure_time = 0;
	    }
	}
    }
}

void
hm_create_health_check_th (char useSSL)
{
  int rv;
  pthread_attr_t thread_attr;
  pthread_t health_check_th;

#if !defined(WINDOWS)
  rv = pthread_attr_init (&thread_attr);
  rv = pthread_attr_setdetachstate (&thread_attr, PTHREAD_CREATE_DETACHED);
  rv = pthread_attr_setscope (&thread_attr, PTHREAD_SCOPE_SYSTEM);
#endif /* WINDOWS */
  rv = pthread_create (&health_check_th, &thread_attr, hm_thread_health_checker, (void *) (size_t) useSSL);
}

/************************************************************************
 * IMPLEMENTATION OF PRIVATE FUNCTIONS	 				*
 ************************************************************************/

bool
hm_is_empty_session (T_CCI_SESSION_ID * id)
{
  size_t i;

  for (i = 0; i < DRIVER_SESSION_SIZE; i++)
    {
      if (id->id[i] != 0)
	{
	  return false;
	}
    }

  return true;
}

void
hm_make_empty_session (T_CCI_SESSION_ID * id)
{
  memset (id->id, 0, DRIVER_SESSION_SIZE);
}

static int
init_con_handle (T_CON_HANDLE * con_handle, char *ip_str, int port, char *db_name, char *db_user, char *db_passwd)
{
  unsigned char ip_addr[4];

  if (is_ip_str (ip_str))
    {
      ipstr2uchar (ip_str, ip_addr);
    }
  else
    {
      if (hostname2uchar (ip_str, ip_addr) < 0)
	{
	  return CCI_ER_HOSTNAME;
	}
    }

  memset (con_handle, 0, sizeof (T_CON_HANDLE));

  memcpy (con_handle->ip_addr, ip_addr, 4);
  con_handle->port = port;
  ALLOC_COPY (con_handle->db_name, db_name);
  ALLOC_COPY (con_handle->db_user, db_user);
  ALLOC_COPY (con_handle->db_passwd, db_passwd);
  snprintf (con_handle->url, SRV_CON_URL_SIZE, "cci:cubrid:%d.%d.%d.%d:%d:%s:%s:********:", ip_addr[0], ip_addr[1],
	    ip_addr[2], ip_addr[3], port, (db_name ? db_name : ""), (db_user ? db_user : ""));
  con_handle->sock_fd = -1;
  con_handle->isolation_level = TRAN_UNKNOWN_ISOLATION;
  con_handle->lock_timeout = CCI_LOCK_TIMEOUT_DEFAULT;
  con_handle->is_retry = 0;
  con_handle->used = false;
  con_handle->con_status = CCI_CON_STATUS_OUT_TRAN;
  con_handle->autocommit_mode = CCI_AUTOCOMMIT_TRUE;
  hm_make_empty_session (&con_handle->session_id);

  con_handle->max_req_handle = REQ_HANDLE_ALLOC_SIZE;
  con_handle->req_handle_table = (T_REQ_HANDLE **) MALLOC (sizeof (T_REQ_HANDLE *) * con_handle->max_req_handle);
  if (con_handle->req_handle_table == NULL)
    {
      FREE_MEM (con_handle->db_name);
      FREE_MEM (con_handle->db_user);
      FREE_MEM (con_handle->db_passwd);
      memset (con_handle, 0, sizeof (T_CON_HANDLE));
      return CCI_ER_NO_MORE_MEMORY;
    }

  con_handle->stmt_pool = cci_mht_create (0, 1000, cci_mht_5strhash, cci_mht_strcasecmpeq);
  if (con_handle->stmt_pool == NULL)
    {
      FREE_MEM (con_handle->db_name);
      FREE_MEM (con_handle->db_user);
      FREE_MEM (con_handle->db_passwd);
      FREE_MEM (con_handle->req_handle_table);
      return CCI_ER_NO_MORE_MEMORY;
    }

  memset (con_handle->req_handle_table, 0, sizeof (T_REQ_HANDLE *) * con_handle->max_req_handle);
  con_handle->req_handle_count = 0;
  con_handle->open_prepared_statement_count = 0;
  memset (con_handle->broker_info, 0, BROKER_INFO_SIZE);

  con_handle->cas_info[CAS_INFO_STATUS] = CAS_INFO_STATUS_INACTIVE;
  con_handle->cas_info[CAS_INFO_RESERVED_1] = CAS_INFO_RESERVED_DEFAULT;
  con_handle->cas_info[CAS_INFO_RESERVED_2] = CAS_INFO_RESERVED_DEFAULT;
  con_handle->cas_info[CAS_INFO_ADDITIONAL_FLAG] = CAS_INFO_RESERVED_DEFAULT;

  memset (con_handle->alter_hosts, 0, sizeof (T_ALTER_HOST) * ALTER_HOST_MAX_SIZE);
  con_handle->load_balance = false;
  con_handle->force_failback = false;
  con_handle->alter_host_count = 0;
  con_handle->alter_host_id = -1;
  con_handle->rc_time = 600;
  con_handle->last_failure_time = 0;
  con_handle->datasource = NULL;
  con_handle->login_timeout = CCI_LOGIN_TIMEOUT_DEFAULT;
  con_handle->query_timeout = 0;
  con_handle->disconnect_on_query_timeout = false;
  con_handle->start_time.tv_sec = 0;
  con_handle->start_time.tv_usec = 0;
  con_handle->current_timeout = 0;

  con_handle->log_filename = NULL;
  con_handle->log_on_exception = false;
  con_handle->log_slow_queries = false;
  con_handle->slow_query_threshold_millis = 60000;
  con_handle->log_trace_api = false;
  con_handle->log_trace_network = false;
  con_handle->ssl_handle.ssl = NULL;
  con_handle->ssl_handle.ctx = NULL;
  con_handle->useSSL = false;
  con_handle->deferred_max_close_handle_count = DEFERRED_CLOSE_HANDLE_ALLOC_SIZE;
  con_handle->deferred_close_handle_list = (int *) MALLOC (sizeof (int) * con_handle->deferred_max_close_handle_count);
  con_handle->deferred_close_handle_count = 0;

  con_handle->is_holdable = 1;
  con_handle->no_backslash_escapes = CCI_NO_BACKSLASH_ESCAPES_NOT_SET;
  con_handle->last_insert_id = NULL;

  con_handle->shard_id = CCI_SHARD_ID_INVALID;

  con_handle->ssl_handle.is_connected = false;
  return 0;
}

static int
new_con_handle_id ()
{
  int i;

  for (i = 0; i < MAX_CON_HANDLE; i++)
    {
      con_handle_current_index = (con_handle_current_index + 1) % MAX_CON_HANDLE;

      if (con_handle_table[con_handle_current_index] == NULL)
	{
	  return (con_handle_current_index + 1);
	}
    }

  return CCI_ER_ALLOC_CON_HANDLE;
}

static int
new_req_handle_id (T_CON_HANDLE * con_handle)
{
  int i;
  int handle_id = 0;
  int new_max_req_handle;
  T_REQ_HANDLE **new_req_handle_table = NULL;

  for (i = 0; i < con_handle->max_req_handle; i++)
    {
      if (con_handle->req_handle_table[i] == NULL)
	{
	  return (i + 1);
	}
    }

  new_max_req_handle = con_handle->max_req_handle + REQ_HANDLE_ALLOC_SIZE;
  new_req_handle_table =
    (T_REQ_HANDLE **) REALLOC (con_handle->req_handle_table, sizeof (T_REQ_HANDLE *) * new_max_req_handle);
  if (new_req_handle_table == NULL)
    {
      return CCI_ER_NO_MORE_MEMORY;
    }

  handle_id = con_handle->max_req_handle + 1;

  memset (new_req_handle_table + con_handle->max_req_handle, 0, REQ_HANDLE_ALLOC_SIZE * sizeof (T_REQ_HANDLE *));

  con_handle->max_req_handle = new_max_req_handle;
  con_handle->req_handle_table = new_req_handle_table;

  return handle_id;
}

static void
con_handle_content_free (T_CON_HANDLE * con_handle)
{
  FREE_MEM (con_handle->db_name);
  FREE_MEM (con_handle->db_user);
  FREE_MEM (con_handle->db_passwd);
  con_handle->url[0] = '\0';
  FREE_MEM (con_handle->req_handle_table);
  FREE_MEM (con_handle->deferred_close_handle_list);
  FREE_MEM (con_handle->last_insert_id);

  if (con_handle->stmt_pool != NULL)
    {
      cci_mht_destroy (con_handle->stmt_pool, true, true);
    }
  FREE_MEM (con_handle->log_filename);
}

static void
ipstr2uchar (char *ip_str, unsigned char *ip_addr)
{
  int ip0, ip1, ip2, ip3;

  if (ip_str == NULL)
    {
      memset (ip_addr, 0, 4);
      return;
    }

  ip0 = ip1 = ip2 = ip3 = 0;

  sscanf (ip_str, "%d%*c%d%*c%d%*c%d", &ip0, &ip1, &ip2, &ip3);

  ip_addr[0] = (unsigned char) ip0;
  ip_addr[1] = (unsigned char) ip1;
  ip_addr[2] = (unsigned char) ip2;
  ip_addr[3] = (unsigned char) ip3;
}

static int
is_ip_str (char *ip_str)
{
  char *p;

  for (p = ip_str; *p; p++)
    {
      if ((*p >= '0' && *p <= '9') || (*p == '.'))
	{
	  continue;
	}
      return 0;
    }

  return 1;
}

static void
hm_set_host_status_by_addr (unsigned char *ip_addr, int port, bool is_reachable)
{
  int i;

  MUTEX_LOCK (host_status_mutex);
  i = hm_find_host_status_index (ip_addr, port);

  if (i < 0)
    {
      i = host_status_count;
      memcpy (host_status[i].host.ip_addr, ip_addr, 4);
      host_status[i].host.port = port;
      host_status_count++;
    }
  host_status[i].is_reachable = is_reachable;

  MUTEX_UNLOCK (host_status_mutex);

}

static THREAD_RET_T THREAD_CALLING_CONVENTION
hm_thread_health_checker (void *arg)
{
  int i;
  unsigned char *ip_addr;
  int port;
  char useSSL = ((size_t) arg) != 0 ? USESSL : NON_USESSL;
  time_t start_time;
  time_t elapsed_time;
  while (1)
    {
      start_time = time (NULL);
      for (i = 0; i < host_status_count; i++)
	{
	  ip_addr = host_status[i].host.ip_addr;
	  port = host_status[i].host.port;
	  if (!host_status[i].is_reachable
	      && net_check_broker_alive (ip_addr, port, BROKER_HEALTH_CHECK_TIMEOUT, useSSL))
	    {
	      hm_set_host_status_by_addr (ip_addr, port, true);
	    }
	}
      elapsed_time = time (NULL) - start_time;
      if (elapsed_time < MONITORING_INTERVAL)
	{
	  SLEEP_MILISEC (MONITORING_INTERVAL - elapsed_time, 0);
	}
    }
  return (THREAD_RET_T) 0;
}

void
hm_force_close_connection (T_CON_HANDLE * con_handle)
{
  con_handle->alter_host_id = -1;
  hm_ssl_free (con_handle);
  CLOSE_SOCKET (con_handle->sock_fd);
  con_handle->sock_fd = INVALID_SOCKET;
  con_handle->con_status = CCI_CON_STATUS_OUT_TRAN;
  con_handle->force_failback = 0;
}

void
hm_ssl_free (T_CON_HANDLE * con_handle)
{
  if (con_handle != NULL)
    {
      if (con_handle->ssl_handle.ssl != NULL || con_handle->ssl_handle.ctx != NULL)
	{
	  cleanup_ssl (con_handle->ssl_handle.ssl);
	  cleanup_ssl_ctx (con_handle->ssl_handle.ctx);
	  con_handle->ssl_handle.ssl = NULL;
	  con_handle->ssl_handle.ctx = NULL;
	  con_handle->ssl_handle.is_connected = false;
	}
    }
}
