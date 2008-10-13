/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * cci_handle_mng.c -
 */

#ident "$Id$"

/************************************************************************
 * IMPORTED SYSTEM HEADER FILES						*
 ************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef WIN32
#include <winsock2.h>
#include <windows.h>
#else
#include <netdb.h>
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

#define CON_HANDLE_ID_FACTOR	1000000

#define MAX_CON_HANDLE		1024

#define REQ_HANDLE_ALLOC_SIZE	256

/************************************************************************
 * PRIVATE TYPE DEFINITIONS						*
 ************************************************************************/

/************************************************************************
 * PRIVATE FUNCTION PROTOTYPES						*
 ************************************************************************/

static int init_con_handle (T_CON_HANDLE * con_handle,
			    char *ip_str,
			    int port,
			    char *db_name, char *db_user, char *db_passwd);
static int new_con_handle_id (void);
static int new_req_handle_id (T_CON_HANDLE * con_handle);
static void con_handle_content_free (T_CON_HANDLE * con_handle);
static void ipstr2uchar (char *ip_str, unsigned char *ip_addr);
static int is_ip_str (char *ip_str);
static int hostname2uchar (char *hostname, unsigned char *ip_addr);

/************************************************************************
 * INTERFACE VARIABLES							*
 ************************************************************************/

/************************************************************************
 * PUBLIC VARIABLES							*
 ************************************************************************/

static T_CON_HANDLE *con_handle_table[MAX_CON_HANDLE];

/************************************************************************
 * PRIVATE VARIABLES							*
 ************************************************************************/

/************************************************************************
 * IMPLEMENTATION OF PUBLIC FUNCTIONS	 				*
 ************************************************************************/

void
hm_con_handle_table_init ()
{
  int i;

  for (i = 0; i < MAX_CON_HANDLE; i++)
    con_handle_table[i] = NULL;
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

  con_handle->req_handle_table[req_handle_id - 1] = req_handle;
  ++(con_handle->req_handle_count);

  *ret_req_handle = req_handle;
  return (con_id * CON_HANDLE_ID_FACTOR + req_handle_id);
}

T_CON_HANDLE *
hm_find_con_handle (int con_handle_id)
{
  if (con_handle_id < 1 || con_handle_id > MAX_CON_HANDLE)
    return NULL;

  return (con_handle_table[con_handle_id - 1]);
}

T_REQ_HANDLE *
hm_find_req_handle (int req_handle_id, T_CON_HANDLE ** ret_con_h)
{
  int con_id;
  int req_id;
  T_CON_HANDLE *con_handle;
  T_REQ_HANDLE *req_handle;

  if (req_handle_id < 1)
    return NULL;

  con_id = req_handle_id / CON_HANDLE_ID_FACTOR;
  req_id = req_handle_id % CON_HANDLE_ID_FACTOR;

  con_handle = con_handle_table[con_id - 1];
  if (con_handle == NULL)
    return NULL;

  if (req_id > con_handle->max_req_handle)
    return NULL;

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
  con_handle->req_handle_table[req_h_id % CON_HANDLE_ID_FACTOR - 1] = NULL;
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
	continue;
      req_handle_content_free (req_handle, 0);
      FREE_MEM (req_handle);
      con_handle->req_handle_table[i] = NULL;
      --(con_handle->req_handle_count);
    }
}

void
hm_req_handle_fetch_buf_free (T_REQ_HANDLE * req_handle)
{
  int i, fetched_tuple;

  if (req_handle->tuple_value)
    {
      fetched_tuple = req_handle->fetched_tuple_end -
	req_handle->fetched_tuple_begin + 1;
      for (i = 0; i < fetched_tuple; i++)
	{
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
    FREE_MEM (req_handle->sql_text);
  req_handle_col_info_free (req_handle);
  if (!reuse)
    qe_bind_value_free (req_handle->num_bind, req_handle->bind_value);
  hm_req_handle_fetch_buf_free (req_handle);
  hm_conv_value_buf_clear (&(req_handle->conv_value_buffer));
  if (!reuse)
    FREE_MEM (req_handle->bind_mode);
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
  con_handle->sock_fd = -1;
  con_handle->ref_count = 0;
  con_handle->default_isolation_level = 0;
  con_handle->is_first = 1;
  con_handle->con_status = CCI_CON_STATUS_OUT_TRAN;
  con_handle->tran_status = CCI_TRAN_STATUS_START;

  con_handle->max_req_handle = REQ_HANDLE_ALLOC_SIZE;
  con_handle->req_handle_table = (T_REQ_HANDLE **)
    MALLOC (sizeof (T_REQ_HANDLE *) * con_handle->max_req_handle);
  if (con_handle->req_handle_table == NULL)
    return CCI_ER_NO_MORE_MEMORY;
  memset (con_handle->req_handle_table,
	  0, sizeof (T_REQ_HANDLE *) * con_handle->max_req_handle);
  con_handle->req_handle_count = 0;
  memset (con_handle->broker_info, 0, BROKER_INFO_SIZE);

  return 0;
}

static int
new_con_handle_id ()
{
  int i;

  for (i = 0; i < MAX_CON_HANDLE; i++)
    {
      if (con_handle_table[i] == NULL)
	{
	  return (i + 1);
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
  FREE_MEM (con_handle->req_handle_table);
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

static int
hostname2uchar (char *hostname, unsigned char *ip_addr)
{
  struct hostent *hp;

  hp = gethostbyname (hostname);
  if (hp == NULL)
    return -1;

  memcpy (ip_addr, hp->h_addr_list[0], 4);

  return 0;
}
