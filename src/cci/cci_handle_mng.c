/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
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

/************************************************************************
 * PRIVATE DEFINITIONS							*
 ************************************************************************/

#define MAX_CON_HANDLE                  1024

#define REQ_HANDLE_ALLOC_SIZE           256

#define CCI_MAX_CONNECTION_POOL         256


/************************************************************************
 * PRIVATE TYPE DEFINITIONS						*
 ************************************************************************/

typedef struct
{
  T_ALTER_HOST host;		/* host info (ip, port) */
  int cur_host_id;		/* now connected host id */
  time_t last_rc_time;		/* last failback try time */
} T_HA_STATUS;

static T_HA_STATUS ha_status[MAX_CON_HANDLE];
static int ha_status_count = 0;

#if defined(WINDOWS)
HANDLE ha_status_mutex;
#else
T_MUTEX ha_status_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

static int conn_pool[CCI_MAX_CONNECTION_POOL];
static unsigned int num_conn_pool = 0;


/************************************************************************
 * PRIVATE FUNCTION PROTOTYPES						*
 ************************************************************************/

static int compare_conn_info (unsigned char *ip_addr, int port, char *dbname,
			      char *dbuser, char *dbpasswd,
			      T_CON_HANDLE * con_handle);
static int init_con_handle (T_CON_HANDLE * con_handle, char *ip_str, int port,
			    char *db_name, char *db_user, char *db_passwd);
static int new_con_handle_id (void);
static int new_req_handle_id (T_CON_HANDLE * con_handle);
static void con_handle_content_free (T_CON_HANDLE * con_handle);
static void ipstr2uchar (char *ip_str, unsigned char *ip_addr);
static int is_ip_str (char *ip_str);

static int hm_find_ha_status_index (T_CON_HANDLE * con_handle);

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
compare_conn_info (unsigned char *ip_addr, int port, char *dbname,
		   char *dbuser, char *dbpasswd, T_CON_HANDLE * con_handle)
{
  if (memcmp (ip_addr, con_handle->ip_addr, 4) != 0 ||
      port != con_handle->port ||
      strcmp (dbname, con_handle->db_name) != 0 ||
      strcmp (dbuser, con_handle->db_user) != 0 ||
      strcmp (dbpasswd, con_handle->db_passwd) != 0)
    {
      return 0;
    }

  return 1;
}

int
hm_get_con_from_pool (unsigned char *ip_addr, int port, char *dbname,
		      char *dbuser, char *dbpasswd)
{
  int con = -1;
  unsigned int i;

  for (i = 0; i < num_conn_pool; i++)
    {
      if (compare_conn_info (ip_addr, port, dbname, dbuser, dbpasswd,
			     con_handle_table[conn_pool[i] - 1]))
	{
	  con = conn_pool[i];
	  conn_pool[i] = conn_pool[--num_conn_pool];
	  break;
	}
    }

  return con;
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

int
hm_con_handle_alloc (char *ip_str, int port, char *db_name, char *db_user,
		     char *db_passwd)
{
  int handle_id;
  int ret_val = 0;
  T_CON_HANDLE *con_handle = NULL;

  handle_id = new_con_handle_id ();
  if (handle_id <= 0)
    goto con_alloc_error;

  con_handle = (T_CON_HANDLE *) MALLOC (sizeof (T_CON_HANDLE));
  if (con_handle == NULL)
    {
      ret_val = CCI_ER_NO_MORE_MEMORY;
      goto con_alloc_error;
    }
  ret_val = init_con_handle (con_handle, ip_str, port,
			     db_name, db_user, db_passwd);
  if (ret_val < 0)
    {
      goto con_alloc_error;
    }

  con_handle_table[handle_id - 1] = con_handle;
  con_handle->id = handle_id;

  return (handle_id);

con_alloc_error:
  FREE_MEM (con_handle);
  return ret_val;
}

int
hm_con_handle_free (int con_id)
{
  T_CON_HANDLE *con_handle;

  con_handle = hm_find_con_handle (con_id);
  if (con_handle == NULL)
    return CCI_ER_CON_HANDLE;

  if (!IS_INVALID_SOCKET (con_handle->sock_fd))
    {
      CLOSE_SOCKET (con_handle->sock_fd);
      con_handle->sock_fd = INVALID_SOCKET;
    }

  con_handle_content_free (con_handle);
  FREE_MEM (con_handle);
  con_handle_table[con_id - 1] = NULL;
  return 0;
}

int
hm_req_handle_alloc (int con_id, T_REQ_HANDLE ** ret_req_handle)
{
  T_CON_HANDLE *con_handle;
  int req_handle_id;
  T_REQ_HANDLE *req_handle;

  *ret_req_handle = NULL;

  con_handle = hm_find_con_handle (con_id);

  if (con_handle == NULL)
    {
      return CCI_ER_CON_HANDLE;
    }

  req_handle_id = new_req_handle_id (con_handle);

  if (req_handle_id < 0)
    return (req_handle_id);

  req_handle = (T_REQ_HANDLE *) MALLOC (sizeof (T_REQ_HANDLE));
  if (req_handle == NULL)
    {
      return CCI_ER_NO_MORE_MEMORY;
    }

  memset (req_handle, 0, sizeof (T_REQ_HANDLE));
  req_handle->fetch_size = 100;
  req_handle->query_timeout = con_handle->query_timeout;

  con_handle->req_handle_table[req_handle_id - 1] = req_handle;
  ++(con_handle->req_handle_count);

  *ret_req_handle = req_handle;
  return MAKE_REQ_ID (con_id, req_handle_id);
}

int
hm_req_add_to_pool (T_CON_HANDLE * con, char *sql, int req_id)
{
  char *key;
  int *data;

  key = strdup (sql);
  if (key == NULL)
    {
      return CCI_ER_NO_MORE_MEMORY;
    }
  data = MALLOC (sizeof (int));
  if (data == NULL)
    {
      FREE (key);
      return CCI_ER_NO_MORE_MEMORY;
    }

  *data = req_id;
  if (!mht_put_data (con->stmt_pool, key, data))
    {
      FREE (key);
      FREE (data);
      return CCI_ER_NO_MORE_MEMORY;
    }
  return CCI_ER_NO_ERROR;
}

int
hm_req_get_from_pool (T_CON_HANDLE * con, char *sql)
{
  int req_id;
  void *data = mht_get (con->stmt_pool, sql);

  if (data == NULL)
    {
      return CCI_ER_REQ_HANDLE;
    }
  req_id = *((int *) data);

  return req_id;
}

T_CON_HANDLE *
hm_find_con_handle (int con_handle_id)
{
  if (con_handle_id < 1 || con_handle_id > MAX_CON_HANDLE)
    {
      return NULL;
    }

  return (con_handle_table[con_handle_id - 1]);
}

T_REQ_HANDLE *
hm_find_req_handle (int req_handle_id, T_CON_HANDLE ** ret_con_h)
{
  int con_id;
  int req_id;
  T_CON_HANDLE *con_handle;
  T_REQ_HANDLE *req_handle;

  if (ret_con_h)
    {
      *ret_con_h = NULL;
    }

  if (req_handle_id < 1)
    {
      return NULL;
    }

  con_id = GET_CON_ID (req_handle_id);
  req_id = GET_REQ_ID (req_handle_id);

  if (con_id < 1 || req_id < 1)
    {
      return NULL;
    }

  con_handle = con_handle_table[con_id - 1];
  if (con_handle == NULL)
    {
      return NULL;
    }

  if (req_id > con_handle->max_req_handle)
    {
      return NULL;
    }

  req_handle = con_handle->req_handle_table[req_id - 1];

  if (ret_con_h)
    *ret_con_h = con_handle;

  return req_handle;
}

void
hm_req_handle_free (T_CON_HANDLE * con_handle, int req_h_id,
		    T_REQ_HANDLE * req_handle)
{
  req_handle_content_free (req_handle, 0);
  FREE_MEM (req_handle);
  con_handle->req_handle_table[GET_REQ_ID (req_h_id) - 1] = NULL;
  --(con_handle->req_handle_count);
}

void
hm_req_handle_free_all (T_CON_HANDLE * con_handle)
{
  int i;
  T_REQ_HANDLE *req_handle;

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
  T_REQ_HANDLE *req_handle;

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
  T_REQ_HANDLE *req_handle;

  for (i = 0; i < con_handle->max_req_handle; i++)
    {
      req_handle = con_handle->req_handle_table[i];
      if (req_handle == NULL)
	{
	  continue;
	}

      if ((req_handle->prepare_flag & CCI_PREPARE_HOLDABLE) != 0
          && !req_handle->is_from_current_transaction)
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
  T_REQ_HANDLE *req_handle;

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
  int i, j, fetched_tuple;

  if (req_handle->tuple_value)
    {
      fetched_tuple = req_handle->fetched_tuple_end -
	req_handle->fetched_tuple_begin + 1;
      for (i = 0; i < fetched_tuple; i++)
	{
#if defined(WINDOWS)
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
}

int
hm_conv_value_buf_alloc (T_VALUE_BUF * val_buf, int size)
{
  if (size <= val_buf->size)
    return 0;

  FREE_MEM (val_buf->data);
  val_buf->size = 0;

  val_buf->data = MALLOC (size);
  if (val_buf->data == NULL)
    return CCI_ER_NO_MORE_MEMORY;
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
	continue;

      curr_req_handle->valid = 0;
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
     For reusing invalidated req handle,
     sql_text and prepare flag of req handle are needed.
     So, they must not be freed.
   */

  QUERY_RESULT_FREE (req_handle);
  if (!reuse)
    {
      FREE_MEM (req_handle->sql_text);
    }
  req_handle_col_info_free (req_handle);
  if (!reuse)
    {
      qe_bind_value_free (req_handle->num_bind, req_handle->bind_value);
    }
  hm_req_handle_fetch_buf_free (req_handle);
  hm_conv_value_buf_clear (&(req_handle->conv_value_buffer));
  if (!reuse)
    {
      FREE_MEM (req_handle->bind_mode);
    }
  req_handle->valid = 0;
}

static int
hm_find_ha_status_index (T_CON_HANDLE * con_handle)
{
  int i, index = -1;

  for (i = 0; i < ha_status_count; i++)
    {
      if (memcmp (ha_status[i].host.ip_addr, con_handle->ip_addr, 4) == 0
	  && ha_status[i].host.port == con_handle->port)
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

time_t
hm_get_ha_last_rc_time (T_CON_HANDLE * con_handle)
{
  int i;
  time_t rc_time = 0;

  MUTEX_LOCK (ha_status_mutex);
  i = hm_find_ha_status_index (con_handle);

  if (i >= 0)
    {
      rc_time = ha_status[i].last_rc_time;
    }

  MUTEX_UNLOCK (ha_status_mutex);
  return rc_time;
}

void
hm_set_ha_status (T_CON_HANDLE * con_handle, bool reset_rctime)
{
  int i;

  MUTEX_LOCK (ha_status_mutex);
  i = hm_find_ha_status_index (con_handle);

  if (i < 0)
    {
      i = ha_status_count;
      memcpy (ha_status[i].host.ip_addr, con_handle->ip_addr, 4);
      ha_status[i].host.port = con_handle->port;
      ha_status_count++;
    }

  if (reset_rctime == true ||
      (con_handle->alter_host_id >= 0 && ha_status[i].cur_host_id == -1))
    {
      ha_status[i].last_rc_time = time (NULL);
    }

  ha_status[i].cur_host_id = con_handle->alter_host_id;

  MUTEX_UNLOCK (ha_status_mutex);
}

void
hm_set_con_handle_holdable (T_CON_HANDLE * con_handle, int holdable)
{
  con_handle->is_holdable = holdable;
}

int
hm_get_con_handle_holdable (T_CON_HANDLE * con_handle)
{
  return con_handle->is_holdable;
}

T_BROKER_VERSION
hm_get_broker_version (T_CON_HANDLE * con_handle)
{
  T_BROKER_VERSION version = 0;

  if (con_handle->broker_info[BROKER_INFO_PROTO_VERSION]
      & CAS_PROTO_INDICATOR)
    {
      version =
	CAS_PROTO_UNPACK_NET_VER (con_handle->
				  broker_info[BROKER_INFO_PROTO_VERSION]);
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

/************************************************************************
 * IMPLEMENTATION OF PRIVATE FUNCTIONS	 				*
 ************************************************************************/

static int
init_con_handle (T_CON_HANDLE * con_handle, char *ip_str, int port,
		 char *db_name, char *db_user, char *db_passwd)
{
  unsigned char ip_addr[4];

  if (is_ip_str (ip_str))
    {
      ipstr2uchar (ip_str, ip_addr);
    }
  else
    {
      if (hostname2uchar (ip_str, ip_addr) < 0)
	return CCI_ER_HOSTNAME;
    }

  memset (con_handle, 0, sizeof (T_CON_HANDLE));

  memcpy (con_handle->ip_addr, ip_addr, 4);
  con_handle->port = port;
  ALLOC_COPY (con_handle->db_name, db_name);
  ALLOC_COPY (con_handle->db_user, db_user);
  ALLOC_COPY (con_handle->db_passwd, db_passwd);
  snprintf (con_handle->url, SRV_CON_URL_SIZE,
	    "cci:cubrid:%d.%d.%d.%d:%d:%s:%s:********:",
	    ip_addr[0], ip_addr[1], ip_addr[2], ip_addr[3], port,
	    (db_name ? db_name : ""), (db_user ? db_user : ""));
  con_handle->sock_fd = -1;
  con_handle->ref_count = 0;
  con_handle->isolation_level = TRAN_UNKNOWN_ISOLATION;
  con_handle->lock_timeout = CCI_LOCK_TIMEOUT_DEFAULT;
  con_handle->is_retry = 0;
  con_handle->con_status = CCI_CON_STATUS_OUT_TRAN;
  con_handle->autocommit_mode = CCI_AUTOCOMMIT_TRUE;
  con_handle->session_id = CCI_EMPTY_SESSION;

  con_handle->max_req_handle = REQ_HANDLE_ALLOC_SIZE;
  con_handle->req_handle_table = (T_REQ_HANDLE **)
    MALLOC (sizeof (T_REQ_HANDLE *) * con_handle->max_req_handle);
  if (con_handle->req_handle_table == NULL)
    {
      FREE_MEM (con_handle->db_name);
      FREE_MEM (con_handle->db_user);
      FREE_MEM (con_handle->db_passwd);
      memset (con_handle, 0, sizeof (T_CON_HANDLE));
      return CCI_ER_NO_MORE_MEMORY;
    }

  con_handle->stmt_pool = mht_create (0, 100, mht_5strhash, mht_strcasecmpeq);
  if (con_handle->stmt_pool == NULL)
    {
      FREE_MEM (con_handle->db_name);
      FREE_MEM (con_handle->db_user);
      FREE_MEM (con_handle->db_passwd);
      FREE_MEM (con_handle->req_handle_table);
      return CCI_ER_NO_MORE_MEMORY;
    }

  memset (con_handle->req_handle_table,
	  0, sizeof (T_REQ_HANDLE *) * con_handle->max_req_handle);
  con_handle->req_handle_count = 0;
  memset (con_handle->broker_info, 0, BROKER_INFO_SIZE);

  con_handle->cas_info[CAS_INFO_STATUS] = CAS_INFO_STATUS_INACTIVE;
  con_handle->cas_info[CAS_INFO_RESERVED_1] = CAS_INFO_RESERVED_DEFAULT;
  con_handle->cas_info[CAS_INFO_RESERVED_2] = CAS_INFO_RESERVED_DEFAULT;
  con_handle->cas_info[CAS_INFO_ADDITIONAL_FLAG] = CAS_INFO_RESERVED_DEFAULT;

  memset (con_handle->alter_hosts, 0,
	  sizeof (T_ALTER_HOST) * ALTER_HOST_MAX_SIZE);
  con_handle->alter_host_count = 0;
  con_handle->alter_host_id = -1;
  con_handle->rc_time = 600;
  con_handle->datasource = NULL;
  con_handle->login_timeout = 0;
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

  con_handle->deferred_max_close_handle_count =
    DEFERRED_CLOSE_HANDLE_ALLOC_SIZE;
  con_handle->deferred_close_handle_list =
    (int *) MALLOC (sizeof (int) *
		    con_handle->deferred_max_close_handle_count);
  con_handle->deferred_close_handle_count = 0;

  con_handle->is_holdable = 1;
  con_handle->no_backslash_escapes = CCI_NO_BACKSLASH_ESCAPES_NOT_SET;

  return 0;
}

static int
new_con_handle_id ()
{
  int i;

  for (i = 0; i < MAX_CON_HANDLE; i++)
    {
      con_handle_current_index =
	(con_handle_current_index + 1) % MAX_CON_HANDLE;

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
	return (i + 1);
    }

  new_max_req_handle = con_handle->max_req_handle + REQ_HANDLE_ALLOC_SIZE;
  new_req_handle_table = (T_REQ_HANDLE **)
    REALLOC (con_handle->req_handle_table,
	     sizeof (T_REQ_HANDLE *) * new_max_req_handle);
  if (new_req_handle_table == NULL)
    return CCI_ER_NO_MORE_MEMORY;

  handle_id = con_handle->max_req_handle + 1;

  memset (new_req_handle_table + con_handle->max_req_handle, 0,
	  REQ_HANDLE_ALLOC_SIZE * sizeof (T_REQ_HANDLE *));

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

  if (con_handle->stmt_pool != NULL)
    {
      mht_destroy (con_handle->stmt_pool, true, true);
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
	continue;
      return 0;
    }

  return 1;
}
