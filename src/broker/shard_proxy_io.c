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
 * shard_proxy_io.c -
 *               
 */

#ident "$Id$"


#include <assert.h>
#include <signal.h>

#include "shard_proxy_io.h"
#include "shard_proxy_handler.h"
#include "cas_protocol.h"
#include "cas_error.h"
#include "shard_shm.h"

#define PROC_TYPE_CLIENT        0
#define PROC_TYPE_CAS           1
#define PROC_TYPE_BROKER        2

#define READ_TYPE       1
#define WRITE_TYPE      2

#define CLIENT_READ_ERROR(i)    io_error(i, PROC_TYPE_CLIENT, READ_TYPE)
#define CLIENT_WRITE_ERROR(i)   io_error(i, PROC_TYPE_CLIENT, WRITE_TYPE)
#define CAS_READ_ERROR(i)       io_error(i, PROC_TYPE_CAS, READ_TYPE)
#define CAS_WRITE_ERROR(i)      io_error(i, PROC_TYPE_CAS, WRITE_TYPE)

#define MAX_NUM_NEW_CLIENT	5

extern T_SHM_APPL_SERVER *shm_as_p;
extern T_SHM_PROXY *shm_proxy_p;
extern T_PROXY_INFO *proxy_info_p;
extern T_SHM_SHARD_USER *shm_user_p;
extern int proxy_id;

extern T_PROXY_HANDLER proxy_Handler;
extern T_PROXY_CONTEXT proxy_Context;

extern void proxy_term (void);

extern const char *rel_build_number (void);

static int shard_io_set_fl (int fd, int flags);
#if defined (ENABLE_UNUSED_FUNCTION)
static int shard_io_clr_fl (int fd, int flags);
static int shard_io_setsockbuf (int fd, int size);
#endif /* ENABLE_UNUSED_FUNCTION */

static void proxy_socket_io_clear (T_SOCKET_IO * sock_io_p);
static int proxy_socket_io_initialize (void);
static void proxy_socket_io_destroy (void);
static T_SOCKET_IO *proxy_socket_io_add (SOCKET fd, bool from_cas);
static T_SOCKET_IO *proxy_socket_io_find (SOCKET fd);
static int proxy_socket_io_new_client (SOCKET lsnr_fd);

static int proxy_process_client_register (T_SOCKET_IO * sock_io_p);
static int proxy_process_client_request (T_SOCKET_IO * sock_io_p);
static int proxy_process_client_conn_error (T_SOCKET_IO * sock_io_p);
static int proxy_process_client_write_error (T_SOCKET_IO * sock_io_p);
static int proxy_process_client_read_error (T_SOCKET_IO * sock_io_p);
static int proxy_process_client_message (T_SOCKET_IO * sock_io_p);
static int proxy_process_cas_register (T_SOCKET_IO * sock_io_p);
static int proxy_process_cas_response (T_SOCKET_IO * sock_io_p);
static int proxy_process_cas_conn_error (T_SOCKET_IO * sock_io_p);
static int proxy_process_cas_write_error (T_SOCKET_IO * sock_io_p);
static int proxy_process_cas_read_error (T_SOCKET_IO * sock_io_p);
static int proxy_process_cas_message (T_SOCKET_IO * sock_io_p);
static int proxy_socket_io_write_internal (T_SOCKET_IO * sock_io_p);
static void proxy_socket_io_write_to_cas (T_SOCKET_IO * sock_io_p);
static void proxy_socket_io_write_to_client (T_SOCKET_IO * sock_io_p);

static int proxy_socket_io_read_internal (T_SOCKET_IO * sock_io_p);
static void proxy_socket_io_read_from_cas_next (T_SOCKET_IO * sock_io_p);
static void proxy_socket_io_read_from_cas_first (T_SOCKET_IO * sock_io_p);
static void proxy_socket_io_read_from_cas (T_SOCKET_IO * sock_io_p);
static void proxy_socket_io_read_from_client_next (T_SOCKET_IO * sock_io_p);
static void proxy_socket_io_read_from_client_first (T_SOCKET_IO * sock_io_p);
static void proxy_socket_io_read_from_client (T_SOCKET_IO * sock_io_p);
static void proxy_socket_io_write (T_SOCKET_IO * sock_io_p);
static void proxy_socket_io_write_error (T_SOCKET_IO * sock_io_p);
static void proxy_socket_io_read (T_SOCKET_IO * sock_io_p);
static void proxy_socket_io_read_error (T_SOCKET_IO * sock_io_p);

static int proxy_client_io_initialize (void);
static void proxy_client_io_destroy (void);
static T_CLIENT_IO *proxy_client_io_new (SOCKET fd);

static int proxy_cas_io_initialize (int shard_id, T_CAS_IO ** cas_io_pp,
				    int size);
static int proxy_shard_io_initialize (void);
static void proxy_shard_io_destroy (void);
static T_SHARD_IO *proxy_shard_io_find (int shard_id);
static T_CAS_IO *proxy_cas_io_new (int shard_id, int cas_id, SOCKET fd);
static void proxy_cas_io_free (int shard_id, int cas_id);
static T_CAS_IO *proxy_cas_io_find_by_fd (int shard_id, int cas_id,
					  SOCKET fd);

static int proxy_client_add_waiter_by_shard (T_SHARD_IO * shard_io_p,
					     int ctx_cid, int ctx_uid);
static void proxy_client_check_waiter_and_wakeup (T_SHARD_IO * shard_io_p,
						  T_CAS_IO * cas_io_p);

static SOCKET proxy_io_connect_to_broker (void);
static int proxy_io_register_to_broker (void);
static int proxy_io_unixd_lsnr (char *unixd_sock_name);
static int proxy_io_cas_lsnr (void);
static SOCKET proxy_io_unixd_accept (SOCKET lsnr_fd);
static SOCKET proxy_io_cas_accept (SOCKET lsnr_fd);


fd_set rset, wset, allset, wnewset, wallset;
SOCKET broker_conn_fd = INVALID_SOCKET;
SOCKET cas_lsnr_fd = INVALID_SOCKET;
int maxfd = 0;

T_SOCKET_IO_GLOBAL proxy_Socket_io;
T_CLIENT_IO_GLOBAL proxy_Client_io;
T_SHARD_IO_GLOBAL proxy_Shard_io;


/* SHARD ONLY SUPPORT ver.8.1.6 ~ */
int cas_info_size = CAS_INFO_SIZE;


/*** 
  THIS FUNCTION IS LOCATED IN ORIGINAL CAS FILES 
 ***/
#if 1				/* SHARD TODO -- remove this functions -- tigger */
void
init_msg_header (MSG_HEADER * header)
{
  header->msg_body_size_ptr = (int *) (header->buf);
  header->info_ptr = (char *) (header->buf + MSG_HEADER_MSG_SIZE);

  *(header->msg_body_size_ptr) = 0;
  header->info_ptr[CAS_INFO_STATUS] = CAS_INFO_STATUS_INACTIVE;
  header->info_ptr[CAS_INFO_RESERVED_1] = CAS_INFO_RESERVED_DEFAULT;
  header->info_ptr[CAS_INFO_RESERVED_2] = CAS_INFO_RESERVED_DEFAULT;
  header->info_ptr[CAS_INFO_ADDITIONAL_FLAG] = CAS_INFO_RESERVED_DEFAULT;
}

void
set_data_length (char *buffer, int length)
{
  assert (buffer);

  /* length : first 4 bytes */
  *((int *) buffer) = htonl (length);
  return;
}

int
get_data_length (char *buffer)
{
  int length;

  assert (buffer);

  /* length : first 4 bytes */
  length = *((int *) (buffer));
  return ntohl (length);
}

int
get_msg_length (char *buffer)
{
  return (get_data_length (buffer) + MSG_HEADER_SIZE);
}

int
net_decode_str (char *msg, int msg_size, char *func_code, void ***ret_argv)
{
  int remain_size = msg_size;
  char *cur_p = msg;
  char *argp;
  int i_val;
  void **argv = NULL;
  int argc = 0;

  *ret_argv = (void **) NULL;

  if (remain_size < 1)
    return CAS_ER_COMMUNICATION;

  *func_code = *cur_p;
  cur_p += 1;
  remain_size -= 1;

  while (remain_size > 0)
    {
      if (remain_size < 4)
	{
	  FREE_MEM (argv);
	  return CAS_ER_COMMUNICATION;
	}
      argp = cur_p;
      memcpy ((char *) &i_val, cur_p, 4);
      i_val = ntohl (i_val);
      remain_size -= 4;
      cur_p += 4;

      if (remain_size < i_val)
	{
	  FREE_MEM (argv);
	  return CAS_ER_COMMUNICATION;
	}

      argc++;
      argv = (void **) REALLOC (argv, sizeof (void *) * argc);
      if (argv == NULL)
	return CAS_ER_NO_MORE_MEMORY;

      argv[argc - 1] = argp;

      cur_p += i_val;
      remain_size -= i_val;
    }

  *ret_argv = argv;
  return argc;
}
#endif /* if 1 */

static int
shard_io_set_fl (int fd, int flags)
{				/* flags are file status flags to turn on */
#if defined(WINDOWS)
  u_long argp;

  if (flags == O_NONBLOCK)
    {
      argp = 1;			/* 1:non-blocking, 0:blocking */
      if (ioctlsocket ((SOCKET) fd, FIONBIO, &argp) == SOCKET_ERROR)
	{
	  PROXY_LOG (PROXY_LOG_MODE_ERROR,
		     "Failed to ioctlsocket(%d, FIONBIO, %u). " "(error:%d).",
		     fd, argp, SOCKET_ERROR);
	  return -1;
	}
    }
#else
  int val;

  if ((val = fcntl (fd, F_GETFL, 0)) < 0)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Failed to fcntl(%d, F_GETFL). (val:%d, errno:%d).", fd, val,
		 errno);
      return -1;
    }

  val |= flags;			/* turn on flags */

  if (fcntl (fd, F_SETFL, val) < 0)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Failed to fcntl(%d, F_SETFL, %d). (errno:%d).", fd, val,
		 errno);
      return -1;
    }
#endif
  return 1;
}

#if defined (ENABLE_UNUSED_FUNCTION)
static int
shard_io_clr_fl (int fd, int flags)
{				/* flags are file status flags to turn on */
#if defined(WINDOWS)
  u_long argp;

  if (flags == O_NONBLOCK)
    {
      argp = 0;			/* 1:non-blocking, 0:blocking */
      if (ioctlsocket ((SOCKET) fd, FIONBIO, &argp) == SOCKET_ERROR)
	{
	  PROXY_LOG (PROXY_LOG_MODE_ERROR,
		     "Failed to ioctlsocket(%d, FIONBIO, %u). (error:%d).",
		     fd, argp, SOCKET_ERROR);
	  return -1;
	}
    }
#else
  int val;

  if ((val = fcntl (fd, F_GETFL, 0)) < 0)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Failed to fcntl(%d, F_GETFL). (val:%d, errno:%d).", fd, val,
		 errno);
      return -1;
    }

  val &= ~flags;		/* turn off flags */

  if (fcntl (fd, F_SETFL, val) < 0)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Failed to fcntl(%d, F_SETFL, %d). (errno:%d).", fd, val,
		 errno);
      return -1;
    }
#endif
  return 1;
}

static int
shard_io_setsockbuf (int fd, int size)
{
  int n, val;
  socklen_t len;

  val = size;
  len = sizeof (int);
  if (getsockopt (fd, SOL_SOCKET, SO_SNDBUF, (char *) &val, &len) < 0)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Failed to getsockopt(%d, SO_SND_BUF, %d, %d). "
		 "(errno:%d).", fd, val, len, errno);
      return -1;
    }
  if (val < size)
    {
      val = size;
      len = sizeof (int);
      setsockopt (fd, SOL_SOCKET, SO_SNDBUF, (char *) &val, len);
    }

  val = size;
  len = sizeof (int);
  if (getsockopt (fd, SOL_SOCKET, SO_RCVBUF, (char *) &val, &len) < 0)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Failed to getsockopt(%d, SO_RCV_BUF, %d, %d). "
		 "(errno:%d).", fd, val, len, errno);

      return -1;
    }
  if (val < size)
    {
      val = size;
      len = sizeof (int);
      setsockopt (fd, SOL_SOCKET, SO_RCVBUF, (char *) &val, len);
    }

  return 1;
}
#endif /* ENABLE_UNUSED_FUNCTION */

char *
proxy_dup_msg (char *msg)
{
  char *p;
  int length;

  length = get_msg_length (msg);

  p = (char *) malloc (length * sizeof (char));
  if (p)
    {
      memcpy (p, msg, length);
    }

  return p;
}

void
proxy_set_con_status_in_tran (char *msg)
{
  int pos;

  assert (msg);

  pos = MSG_HEADER_INFO_SIZE + CAS_INFO_STATUS;

  msg[pos] = CAS_INFO_STATUS_ACTIVE;

  return;
}

void
proxy_set_con_status_out_tran (char *msg)
{
  int pos;

  assert (msg);

  pos = MSG_HEADER_INFO_SIZE + CAS_INFO_STATUS;

  msg[pos] = CAS_INFO_STATUS_INACTIVE;

  return;
}

void
proxy_set_force_out_tran (char *msg)
{
  int pos;

  assert (msg);

  pos = MSG_HEADER_INFO_SIZE + CAS_INFO_ADDITIONAL_FLAG;

  msg[pos] |= CAS_INFO_FLAG_MASK_FORCE_OUT_TRAN;

  return;
}

void
proxy_unset_force_out_tran (char *msg)
{
  int pos;

  assert (msg);

  pos = MSG_HEADER_INFO_SIZE + CAS_INFO_ADDITIONAL_FLAG;

  msg[pos] &= ~CAS_INFO_FLAG_MASK_FORCE_OUT_TRAN;

  return;
}

int
proxy_make_net_buf (T_NET_BUF * net_buf, int size)
{
  net_buf_init (net_buf);

  net_buf->data = (char *) MALLOC (size);
  if (net_buf->data == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Not enough virtual memory. "
		 "Failed to alloc net buffer. "
		 "(errno:%d, size:%d).", errno, size);
      return -1;
    }
  net_buf->alloc_size = size;

  return 0;
}

void
proxy_init_net_buf (T_NET_BUF * net_buf)
{
  MSG_HEADER msg_header;

  assert (net_buf);
  assert (net_buf->data);

  init_msg_header (&msg_header);

  *(msg_header.msg_body_size_ptr) = htonl (net_buf->data_size);

  /* length */
  memcpy (net_buf->data, msg_header.buf, NET_BUF_HEADER_MSG_SIZE);

  /* cas info */
  /* 0:cas status */
  msg_header.info_ptr[CAS_INFO_STATUS] = CAS_INFO_STATUS_INACTIVE;

  /* 3:cci default autocommit */
  msg_header.info_ptr[CAS_INFO_ADDITIONAL_FLAG] &=
    ~CAS_INFO_FLAG_MASK_AUTOCOMMIT;
  msg_header.info_ptr[CAS_INFO_ADDITIONAL_FLAG] |=
    (shm_as_p->cci_default_autocommit & CAS_INFO_FLAG_MASK_AUTOCOMMIT);

  memcpy (net_buf->data + NET_BUF_HEADER_MSG_SIZE,
	  msg_header.info_ptr, CAS_INFO_SIZE);

  return;
}

/* error */
int
proxy_io_make_error_msg (char **buffer, int error_ind, int error_code,
			 char *error_msg, char is_in_tran)
{
  int error;
  T_NET_BUF net_buf;

  assert (buffer);
  assert (*buffer == NULL);

  error = proxy_make_net_buf (&net_buf, NET_BUF_ALLOC_SIZE);
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to make net buffer. "
		 "(error:%d).", error);
      goto error_return;
    }

  proxy_init_net_buf (&net_buf);

  if (is_in_tran)
    {
      proxy_set_con_status_in_tran (net_buf.data);
    }

  if (error_ind != CAS_NO_ERROR)
    {
      /* error indicator */
      net_buf_cp_int (&net_buf, error_ind, NULL);
    }

  /* error code */
  net_buf_cp_int (&net_buf, error_code, NULL);

  if (error_msg && error_msg[0])
    {
      /* error messgae */
      net_buf_cp_str (&net_buf, error_msg, strlen (error_msg) + 1);
    }

  *buffer = net_buf.data;
  set_data_length (*buffer, net_buf.data_size);

  net_buf.data = NULL;

  return (net_buf.data_size + MSG_HEADER_SIZE);

error_return:
  *buffer = NULL;

  return -1;
}

int
proxy_io_make_no_error (char **buffer)
{
  return proxy_io_make_error_msg (buffer, CAS_NO_ERROR, CAS_NO_ERROR, NULL,
				  false);
}

int
proxy_io_make_con_close_ok (char **buffer)
{
  return proxy_io_make_no_error (buffer);
}

int
proxy_io_make_end_tran_ok (char **buffer)
{
  return proxy_io_make_no_error (buffer);
}

int
proxy_io_make_check_cas_ok (char **buffer)
{
  return proxy_io_make_no_error (buffer);
}

int
proxy_io_make_end_tran_request (char **buffer, bool commit)
{
  int error;
  T_NET_BUF net_buf;
  unsigned char func_code;
  unsigned char tran_commit;

  assert (buffer);
  assert (*buffer == NULL);

  error = proxy_make_net_buf (&net_buf, NET_BUF_ALLOC_SIZE);
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Failed to make net buffer. (error:%d).", error);
      goto error_return;
    }

  proxy_init_net_buf (&net_buf);

  func_code = CAS_FC_END_TRAN;
  tran_commit = (commit) ? CCI_TRAN_COMMIT : CCI_TRAN_ROLLBACK;

  /* function code */
  net_buf_cp_byte (&net_buf, (unsigned char) func_code);

  /* arg1 : commit or rollback */
  net_buf_cp_int (&net_buf, NET_SIZE_BYTE, NULL);
  net_buf_cp_byte (&net_buf, tran_commit);

  *buffer = net_buf.data;
  set_data_length (*buffer, net_buf.data_size);

  net_buf.data = NULL;

  return (net_buf.data_size + MSG_HEADER_SIZE);

error_return:
  *buffer = NULL;

  return -1;
}

int
proxy_io_make_end_tran_commit (char **buffer)
{
  return proxy_io_make_end_tran_request (buffer, true);
}

int
proxy_io_make_end_tran_abort (char **buffer)
{
  return proxy_io_make_end_tran_request (buffer, false);
}

int
proxy_io_make_close_req_handle_ok (char **buffer, bool is_in_tran)
{
  int error;
  char *p;
  T_NET_BUF net_buf;

  assert (buffer);
  assert (*buffer == NULL);

  error = proxy_make_net_buf (&net_buf, NET_BUF_ALLOC_SIZE);
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Failed to make net buffer. (error:%d).", error);
      goto error_return;
    }

  proxy_init_net_buf (&net_buf);

  p = (char *) (net_buf.data + MSG_HEADER_MSG_SIZE);
  if (is_in_tran)
    {
      p[CAS_INFO_STATUS] = CAS_INFO_STATUS_ACTIVE;
    }
  else
    {
      p[CAS_INFO_STATUS] = CAS_INFO_STATUS_INACTIVE;
    }

  /* error code */
  net_buf_cp_int (&net_buf, 0 /* success */ , NULL);

  *buffer = net_buf.data;
  set_data_length (*buffer, net_buf.data_size);

  net_buf.data = NULL;

  return (net_buf.data_size + MSG_HEADER_SIZE);

error_return:
  *buffer = NULL;

  return -1;
}

int
proxy_io_make_close_req_handle_in_tran_ok (char **buffer)
{
  return proxy_io_make_close_req_handle_ok (buffer, true /* in_tran */ );
}

int
proxy_io_make_close_req_handle_out_tran_ok (char **buffer)
{
  return proxy_io_make_close_req_handle_ok (buffer, false /* out_tran */ );
}

int
proxy_io_make_get_db_version (char **buffer)
{
  int error;
  T_NET_BUF net_buf;
  char *p;

  assert (buffer);
  assert (*buffer == NULL);

  error = proxy_make_net_buf (&net_buf, NET_BUF_ALLOC_SIZE);
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Failed to make net buffer. (error:%d).", error);
      goto error_return;
    }

  proxy_init_net_buf (&net_buf);

  p = (char *) rel_build_number ();

  net_buf_cp_int (&net_buf, 0 /* ok */ , NULL);
  if (p == NULL)
    {
      net_buf_cp_byte (&net_buf, '\0');
    }
  else
    {
      net_buf_cp_str (&net_buf, p, strlen (p) + 1);
    }

  *buffer = net_buf.data;
  set_data_length (*buffer, net_buf.data_size);

  net_buf.data = NULL;

  return (net_buf.data_size + MSG_HEADER_SIZE);

error_return:
  *buffer = NULL;

  return -1;
}

int
proxy_io_make_client_conn_ok (char **buffer)
{
  (*buffer) = (char *) malloc (sizeof (int));

  if ((*buffer) == NULL)
    {
      return 0;
    }

  memset ((*buffer), 0, sizeof (int));	/* dummy port id */

  return sizeof (int);
}

int
proxy_io_make_client_dbinfo_ok (char **buffer)
{
  char *p;
  int reply_size;
  int cas_info_size;
  int proxy_pid;
  unsigned int session_id;
  char broker_info[BROKER_INFO_SIZE];

  assert (buffer);

  memset (broker_info, 0, BROKER_INFO_SIZE);
  broker_info[BROKER_INFO_DBMS_TYPE] = CCI_DBMS_CUBRID;
  broker_info[BROKER_INFO_MAJOR_VERSION] = MAJOR_VERSION;
  broker_info[BROKER_INFO_MINOR_VERSION] = MINOR_VERSION;
  broker_info[BROKER_INFO_PATCH_VERSION] = PATCH_VERSION;
  broker_info[BROKER_INFO_KEEP_CONNECTION] = CAS_KEEP_CONNECTION_ON;
  if (shm_as_p->statement_pooling)
    {
      broker_info[BROKER_INFO_STATEMENT_POOLING] = CAS_STATEMENT_POOLING_ON;
    }
  else
    {
      broker_info[BROKER_INFO_STATEMENT_POOLING] = CAS_STATEMENT_POOLING_OFF;
    }
  broker_info[BROKER_INFO_CCI_PCONNECT] =
    (shm_as_p->cci_pconnect ? CCI_PCONNECT_ON : CCI_PCONNECT_OFF);

  *buffer = (char *) malloc (PROXY_CONNECTION_REPLY_SIZE * sizeof (char));
  if (*buffer == NULL)
    {
      return -1;
    }

  reply_size = htonl (CAS_CONNECTION_REPLY_SIZE);
  cas_info_size = htonl (CAS_INFO_SIZE);
  proxy_pid = htonl (getpid ());
  session_id = htonl (0);

  /* length */
  p = *(buffer);
  memcpy (p, &reply_size, PROTOCOL_SIZE);
  p += PROTOCOL_SIZE;

  /* cas info */
  memcpy (p, &cas_info_size, CAS_INFO_SIZE);
  p += CAS_INFO_SIZE;

  /* proxy pid */
  memcpy (p, &proxy_pid, CAS_PID_SIZE);
  p += CAS_PID_SIZE;

  /* brokerinfo */
  memcpy (p, broker_info, BROKER_INFO_SIZE);
  p += BROKER_INFO_SIZE;

  /* session id */
  memcpy (p, &session_id, SESSION_ID_SIZE);

  return PROXY_CONNECTION_REPLY_SIZE;
}

void
proxy_io_buffer_clear (T_IO_BUFFER * io_buffer)
{
  assert (io_buffer);

  io_buffer->length = 0;
  io_buffer->offset = 0;
  FREE_MEM (io_buffer->data);

  return;
}

static void
proxy_socket_io_clear (T_SOCKET_IO * sock_io_p)
{
  assert (sock_io_p);

/* UNUSED */
#if 0
  ENTER_FUNC ();

  PROXY_DEBUG_LOG ("free socket io.(fd:%d,from_cas:%s,shard/cas:%d/%d).\n",
		   sock_io_p->fd,
		   (sock_io_p->from_cas == PROXY_EVENT_FROM_CAS) ?
		   "cas" : "client",
		   sock_io_p->id.shard.shard_id, sock_io_p->id.shard.cas_id);
#endif

  sock_io_p->fd = INVALID_SOCKET;
  sock_io_p->status = SOCK_IO_IDLE;
  sock_io_p->ip_addr = 0;
  sock_io_p->from_cas = PROXY_EVENT_FROM_CLIENT;
  sock_io_p->id.shard.shard_id = PROXY_INVALID_SHARD;
  sock_io_p->id.shard.cas_id = PROXY_INVALID_CAS;

  if (sock_io_p->read_event)
    {
      proxy_event_free (sock_io_p->read_event);
      sock_io_p->read_event = NULL;
    }

  if (sock_io_p->write_event)
    {
      proxy_event_free (sock_io_p->write_event);
      sock_io_p->write_event = NULL;
    }

/* UNUSED */
#if 0
  EXIT_FUNC ();
#endif
  return;
}


static int
proxy_socket_io_initialize (void)
{
  int i;
  int size;
  T_SOCKET_IO *sock_io_p;

  if (proxy_Socket_io.ent)
    {
      /* alredy initialized */
      return 0;
    }

  proxy_Socket_io.max_socket = MAX_FD;
  proxy_Socket_io.cur_socket = 0;

  size = sizeof (T_SOCKET_IO) * MAX_FD;
  proxy_Socket_io.ent = (T_SOCKET_IO *) malloc (size);
  if (proxy_Socket_io.ent == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Not enough virtual memory. "
		 "Failed to alloc socket entry. "
		 "(errno:%d, size:%d).", errno, size);
      return -1;
    }
  memset (proxy_Socket_io.ent, 0, size);

  for (i = 0; i < MAX_FD; i++)
    {
      sock_io_p = &(proxy_Socket_io.ent[i]);

      proxy_socket_io_clear (sock_io_p);
    }

  return 0;
}

static void
proxy_socket_io_destroy (void)
{
  int i;
  T_SOCKET_IO *sock_io_p;

  if (proxy_Socket_io.ent == NULL)
    {
      return;
    }

  for (i = 0; i < proxy_Socket_io.max_socket; i++)
    {
      sock_io_p = &(proxy_Socket_io.ent[i]);

      if (sock_io_p->fd != INVALID_SOCKET)
	{
	  FD_CLR (sock_io_p->fd, &allset);
	  FD_CLR (sock_io_p->fd, &wnewset);

	  CLOSE_SOCKET (sock_io_p->fd);
	}
      proxy_socket_io_clear (sock_io_p);
    }

  proxy_Socket_io.max_socket = 0;
  proxy_Socket_io.cur_socket = 0;
  FREE_MEM (proxy_Socket_io.ent);

  return;
}

#if defined(PROXY_VERBOSE_DEBUG)
void
proxy_socket_io_print (bool print_all)
{
  int i;
  int client_id, shard_id, cas_id;
  char *from_cas;
  T_SOCKET_IO *sock_io_p;

  PROXY_LOG (PROXY_LOG_MODE_SCHEDULE_DETAIL, "* SOCKET IO *");
  PROXY_LOG (PROXY_LOG_MODE_SCHEDULE_DETAIL, "%-20s = %d",
	     "max_socket", proxy_Socket_io.max_socket);
  PROXY_LOG (PROXY_LOG_MODE_SCHEDULE_DETAIL, "%-20s = %d",
	     "cur_socket", proxy_Socket_io.cur_socket);

  PROXY_LOG (PROXY_LOG_MODE_SCHEDULE_DETAIL,
	     "%-7s     %-5s %-8s %-16s %-8s %-10s %-10s %-10s", "idx",
	     "fd", "status", "ip_addr", "cas", "client_id", "shard_id",
	     "cas_id");
  if (proxy_Socket_io.ent)
    {
      for (i = 0; i < proxy_Socket_io.max_socket; i++)
	{
	  sock_io_p = &(proxy_Socket_io.ent[i]);
	  if (!print_all && sock_io_p->fd <= INVALID_SOCKET)
	    {
	      continue;
	    }

	  if (sock_io_p->from_cas)
	    {
	      client_id = PROXY_INVALID_CAS;
	      shard_id = sock_io_p->id.shard.shard_id;
	      cas_id = sock_io_p->id.shard.cas_id;
	      from_cas = (char *) "cas";
	    }
	  else
	    {
	      client_id = sock_io_p->id.client_id;
	      shard_id = PROXY_INVALID_SHARD;
	      cas_id = PROXY_INVALID_CAS;
	      from_cas = (char *) "client";
	    }

	  PROXY_LOG (PROXY_LOG_MODE_SCHEDULE_DETAIL,
		     "[%-5d]     %-5d %-8d %-16s %-8s %-10d %-10d %-10d",
		     i, sock_io_p->fd, sock_io_p->status,
		     inet_ntoa (*((struct in_addr *) &sock_io_p->ip_addr)),
		     from_cas, client_id, shard_id, cas_id);

	}
    }

  return;
}
#endif /* PROXY_VERBOSE_DEBUG */

static T_SOCKET_IO *
proxy_socket_io_add (SOCKET fd, bool from_cas)
{
  T_SOCKET_IO *sock_io_p;

  assert (proxy_Socket_io.ent);
  if (proxy_Socket_io.ent == NULL)
    {
      return NULL;
    }

  if (fd >= MAX_FD)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "socket fd exceeds MAX_FD. (fd:%d, MAX_FD:%d).", fd, MAX_FD);
      return NULL;
    }

  if (proxy_Socket_io.cur_socket >= proxy_Socket_io.max_socket)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Number of socket entry exceeds max_socket. "
		 "(current_socket:%d, max_socket:%d).",
		 proxy_Socket_io.cur_socket, proxy_Socket_io.max_socket);
      return NULL;
    }

  sock_io_p = &(proxy_Socket_io.ent[fd]);

  assert (sock_io_p->fd <= INVALID_SOCKET);
  assert (sock_io_p->status == SOCK_IO_IDLE);

  sock_io_p->fd = fd;
  sock_io_p->status = SOCK_IO_REG_WAIT;
  sock_io_p->ip_addr = 0;

  sock_io_p->from_cas = from_cas;

  sock_io_p->id.shard.shard_id = PROXY_INVALID_SHARD;
  sock_io_p->id.shard.cas_id = PROXY_INVALID_CAS;

  assert (sock_io_p->read_event == NULL);
  assert (sock_io_p->write_event == NULL);
  sock_io_p->read_event = NULL;
  sock_io_p->write_event = NULL;

  shard_io_set_fl (fd, O_NONBLOCK);

  FD_SET (fd, &allset);
  maxfd = max (maxfd, fd);

  proxy_Socket_io.cur_socket++;

  PROXY_LOG (PROXY_LOG_MODE_SHARD_DETAIL, "New socket io created. (fd:%d).",
	     fd);
#if defined(PROXY_VERBOSE_DEBUG)
  proxy_socket_io_print (false);
#endif /* PROXY_VERBOSE_DEBUG */

  return sock_io_p;
}

int
proxy_socket_io_delete (SOCKET fd)
{
  T_SOCKET_IO *sock_io_p;

  ENTER_FUNC ();

  assert (proxy_Socket_io.ent);
  assert (proxy_Socket_io.cur_socket > 0);

  if (fd >= MAX_FD)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "socket fd exceeds MAX_FD. (fd:%d, MAX_FD:%d).", fd, MAX_FD);
      EXIT_FUNC ();
      return -1;
    }

  sock_io_p = &(proxy_Socket_io.ent[fd]);

  if (sock_io_p->fd != INVALID_SOCKET)
    {
      FD_CLR (sock_io_p->fd, &allset);
      FD_CLR (sock_io_p->fd, &wnewset);

      PROXY_DEBUG_LOG ("Close socket. (fd:%d).", sock_io_p->fd);
      CLOSE_SOCKET (sock_io_p->fd);
    }
  proxy_socket_io_clear (sock_io_p);
  proxy_Socket_io.cur_socket--;

  EXIT_FUNC ();
  return 0;
}

static T_SOCKET_IO *
proxy_socket_io_find (SOCKET fd)
{
  T_SOCKET_IO *sock_io_p;

  assert (proxy_Socket_io.ent);

  sock_io_p = &(proxy_Socket_io.ent[fd]);
  return (sock_io_p->status == SOCK_IO_IDLE) ? NULL : sock_io_p;
}

int
proxy_socket_set_write_event (T_SOCKET_IO * sock_io_p,
			      T_PROXY_EVENT * event_p)
{
  assert (sock_io_p);
  assert (event_p);

  if (sock_io_p->write_event)
    {
      /* the procotol between driver and proxy must be broken */
      proxy_event_free (sock_io_p->write_event);
      sock_io_p->write_event = NULL;

      return -1;
    }

  event_p->buffer.offset = 0;	// set offset to start of the write buffer

  sock_io_p->write_event = event_p;

  FD_SET (sock_io_p->fd, &wnewset);

  return 0;
}

static int
proxy_socket_io_new_client (SOCKET lsnr_fd)
{
#if !defined(WINDOWS)
  int error;
  int client_ip;
  int length;
  char *buffer;
  SOCKET client_fd;
  T_CLIENT_IO *cli_io_p;
  T_SOCKET_IO *sock_io_p;
  T_CLIENT_INFO *client_info_p;
  T_PROXY_EVENT *event_p;
  int proxy_status = 0;

  client_fd = recv_fd (lsnr_fd, &client_ip);
  if (client_fd < 0)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Failed to receive socket fd. "
		 "(lsnf_fd:%d, client_fd:%d).", lsnr_fd, client_fd);

      /* shard_broker must be abnormal status */
      proxy_term ();
    }

  proxy_status = 0;
  length = WRITESOCKET (lsnr_fd, &proxy_status, sizeof (proxy_status));
  if (length != sizeof (proxy_status))
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Failed to send proxy status to broker. "
		 "(lsnr_fd:%d).", lsnr_fd);
      CLOSE_SOCKET (client_fd);
      return -1;
    }

  ENTER_FUNC ();

  sock_io_p = proxy_socket_io_add (client_fd, PROXY_IO_FROM_CLIENT);
  if (sock_io_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Failed to add socket entry. (client fd:%d).", client_fd);
      CLOSE_SOCKET (client_fd);
      return -1;
    }

  cli_io_p = proxy_client_io_new (client_fd);
  if (cli_io_p == NULL)
    {
      proxy_socket_io_delete (client_fd);
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Failed to create client entry. (client fd:%d).", client_fd);
      return -1;
    }

  sock_io_p->ip_addr = client_ip;
  sock_io_p->id.client_id = cli_io_p->client_id;

  /* set shared memory T_CLIENT_INFO */
  client_info_p =
    shard_shm_get_client_info (proxy_info_p, cli_io_p->client_id);
  client_info_p->client_id = cli_io_p->client_id;
  client_info_p->client_ip = client_ip;
  client_info_p->connect_time = time (NULL);

  /* send client_conn_ok to the client */
  event_p = proxy_event_new_with_rsp (PROXY_EVENT_IO_WRITE,
				      PROXY_EVENT_FROM_CLIENT,
				      proxy_io_make_client_conn_ok);
  if (event_p == NULL)
    {
      proxy_socket_io_read_error (sock_io_p);
      EXIT_FUNC ();
      return -1;
    }

  /* set write event to the socket io */
  error = proxy_socket_set_write_event (sock_io_p, event_p);
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to set write buffer. "
		 "(fd:%d). event(%s).", client_fd, proxy_str_event (event_p));

      proxy_event_free (event_p);
      event_p = NULL;

      proxy_socket_io_read_error (sock_io_p);

      EXIT_FUNC ();
      return -1;
    }

  EXIT_FUNC ();
#endif /* WINDOWS */
  return 0;
}

static int
proxy_process_client_register (T_SOCKET_IO * sock_io_p)
{
  int error;
  int i;
  int length;
  char *db_name, *db_user, *db_passwd;
  char *db_info_ok;
  struct timeval client_start_time;
  T_IO_BUFFER *read_buffer;
  T_SHARD_USER *user_p;
  T_PROXY_EVENT *event_p;

  ENTER_FUNC ();

  assert (sock_io_p);
  assert (sock_io_p->read_event);

  gettimeofday (&client_start_time, NULL);

  read_buffer = &(sock_io_p->read_event->buffer);
  assert (read_buffer);		// __FOR_DEBUG

  db_name = read_buffer->data;
  db_name[SRV_CON_DBNAME_SIZE - 1] = '\0';

  db_user = db_name + SRV_CON_DBNAME_SIZE;
  db_user[SRV_CON_DBUSER_SIZE - 1] = '\0';
  if (db_user[0] == '\0')
    {
      strcpy (db_user, "public");
    }

  db_passwd = db_user + SRV_CON_DBUSER_SIZE;
  db_passwd[SRV_CON_DBPASSWD_SIZE - 1] = '\0';

  user_p = shard_metadata_get_shard_user (shm_user_p);
  assert (user_p);

  if (proxy_info_p->appl_server == APPL_SERVER_CAS_MYSQL)
    {
      if (strcmp (db_name, user_p->db_name)
	  || strcmp (db_user, user_p->db_user)
	  || strcmp (db_passwd, user_p->db_password))
	{
	  PROXY_LOG (PROXY_LOG_MODE_ERROR, "Authentication failure. "
		     "(db_name:[%s], db_user:[%s], db_passwd:[%s]).",
		     db_name, db_user, db_passwd);
	  proxy_socket_io_read_error (sock_io_p);

	  EXIT_FUNC ();
	  return -1;
	}
    }
  else
    {
      if (strcasecmp (db_name, user_p->db_name)
	  || strcasecmp (db_user, user_p->db_user)
	  || strcmp (db_passwd, user_p->db_password))
	{
	  PROXY_LOG (PROXY_LOG_MODE_ERROR, "Authentication failure. "
		     "(db_name:[%s], db_user:[%s], db_passwd:[%s]).",
		     db_name, db_user, db_passwd);
	  proxy_socket_io_read_error (sock_io_p);

	  EXIT_FUNC ();
	  return -1;
	}
    }

  /* SHARD TODO : check acl */

  /* send dbinfo_ok to the client */
  event_p = proxy_event_new_with_rsp (PROXY_EVENT_IO_WRITE,
				      PROXY_EVENT_FROM_CLIENT,
				      proxy_io_make_client_dbinfo_ok);
  if (event_p == NULL)
    {
      proxy_socket_io_read_error (sock_io_p);
      EXIT_FUNC ();
      return -1;
    }

  /* set write event to the socket io */
  error = proxy_socket_set_write_event (sock_io_p, event_p);
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to set write buffer. "
		 "(fd:%d). event(%s).",
		 sock_io_p->fd, proxy_str_event (event_p));

      proxy_event_free (event_p);
      event_p = NULL;

      proxy_socket_io_read_error (sock_io_p);
      EXIT_FUNC ();
      return -1;
    }

  sock_io_p->status = SOCK_IO_ESTABLISHED;

  if (shm_as_p->access_log == ON)
    {
      proxy_access_log (&client_start_time,
			sock_io_p->ip_addr, db_name, db_user, true);
    }

  assert (sock_io_p->read_event);
  proxy_event_free (sock_io_p->read_event);
  sock_io_p->read_event = NULL;

  EXIT_FUNC ();
  return 0;
}

static int
proxy_process_client_request (T_SOCKET_IO * sock_io_p)
{
  int error;
  int length;
  char *buffer;
  T_PROXY_CONTEXT *ctx_p;
  T_CLIENT_IO *cli_io_p;
  T_PROXY_EVENT *event_p;

  ENTER_FUNC ();

  assert (sock_io_p);
  assert (sock_io_p->read_event);
  assert (sock_io_p->from_cas == PROXY_EVENT_FROM_CLIENT);

  event_p = sock_io_p->read_event;
  sock_io_p->read_event = NULL;

  ctx_p = proxy_context_find_by_socket_client_io (sock_io_p);
  if (ctx_p == NULL)
    {
      proxy_event_free (event_p);
      event_p = NULL;
      return -1;
    }

  proxy_event_set_type_from (event_p, PROXY_EVENT_CLIENT_REQUEST,
			     PROXY_EVENT_FROM_CLIENT);
  proxy_event_set_context (event_p, ctx_p->cid, ctx_p->uid);

  error = shard_queue_enqueue (&proxy_Handler.cli_rcv_q, (void *) event_p);
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to queue client event. "
		 "context(%s). event(%s).",
		 proxy_str_context (ctx_p), proxy_str_event (event_p));

      proxy_event_free (event_p);
      event_p = NULL;

      proxy_context_free (ctx_p);

      EXIT_FUNC ();
      return -1;
    }

  EXIT_FUNC ();
  return 0;
}

static int
proxy_process_client_conn_error (T_SOCKET_IO * sock_io_p)
{
  int error;
  int length;
  char *buffer;
  T_PROXY_CONTEXT *ctx_p;
  T_CLIENT_INFO *client_info_p;
  T_PROXY_EVENT *event_p;

  ENTER_FUNC ();

  assert (sock_io_p);
  assert (sock_io_p->fd);
  assert (sock_io_p->from_cas == PROXY_EVENT_FROM_CLIENT);

  sock_io_p->status = SOCK_IO_CLOSE_WAIT;

  /* disable socket read/write */
  FD_CLR (sock_io_p->fd, &allset);
  FD_CLR (sock_io_p->fd, &wnewset);

  ctx_p = proxy_context_find_by_socket_client_io (sock_io_p);
  if (ctx_p == NULL)
    {
      return -1;
    }

  /* init shared memory T_CLIENT_INFO */
  client_info_p =
    shard_shm_get_client_info (proxy_info_p, sock_io_p->id.client_id);
  shard_shm_init_client_info (client_info_p);

  event_p =
    proxy_event_new (PROXY_EVENT_CLIENT_CONN_ERROR, PROXY_EVENT_FROM_CLIENT);
  if (event_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to make new event. (%s, %s).",
		 "PROXY_EVENT_CLIENT_CONN_ERROR", "PROXY_EVENT_FROM_CLIENT");

      proxy_context_free (ctx_p);

      EXIT_FUNC ();
      return -1;
    }
  proxy_event_set_context (event_p, ctx_p->cid, ctx_p->uid);

  error = shard_queue_enqueue (&proxy_Handler.cli_rcv_q, (void *) event_p);
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to queue client event. "
		 "context(%s). event(%s).",
		 proxy_str_context (ctx_p), proxy_str_event (event_p));

      proxy_event_free (event_p);
      event_p = NULL;

      proxy_context_free (ctx_p);

      EXIT_FUNC ();
      return -1;
    }

  EXIT_FUNC ();
  return 0;
}

static int
proxy_process_client_write_error (T_SOCKET_IO * sock_io_p)
{
  int error;

  if (sock_io_p->write_event)
    {
      proxy_event_free (sock_io_p->write_event);
      sock_io_p->write_event = NULL;
    }

  error = proxy_process_client_conn_error (sock_io_p);
  if (error)
    {
      if (sock_io_p->fd != INVALID_SOCKET)
	{
	  CLOSE_SOCKET (sock_io_p->fd);
	}
    }

  return error;
}

static int
proxy_process_client_read_error (T_SOCKET_IO * sock_io_p)
{
  int error;

  assert (sock_io_p);

  assert (sock_io_p->read_event);
  if (sock_io_p->read_event)
    {
      proxy_event_free (sock_io_p->read_event);
      sock_io_p->read_event = NULL;
    }

  error = proxy_process_client_conn_error (sock_io_p);
  if (error)
    {
      if (sock_io_p->fd != INVALID_SOCKET)
	{
	  CLOSE_SOCKET (sock_io_p->fd);
	}
    }

  return error;
}

static int
proxy_process_client_message (T_SOCKET_IO * sock_io_p)
{
  int error = 0;

  assert (sock_io_p);
  char *buffer;

  switch (sock_io_p->status)
    {
    case SOCK_IO_REG_WAIT:
      error = proxy_process_client_register (sock_io_p);
      break;

    case SOCK_IO_ESTABLISHED:
      error = proxy_process_client_request (sock_io_p);
      break;

    default:
      break;
    }

  return error;
}

static int
proxy_process_cas_register (T_SOCKET_IO * sock_io_p)
{
  int error;
  int length;
  char *p;
  int shard_id = PROXY_INVALID_SHARD, cas_id = PROXY_INVALID_CAS;
  char func_code;
  T_SHARD_IO *shard_io_p = NULL;
  T_CAS_IO *cas_io_p = NULL;
  T_IO_BUFFER *read_buffer;

  ENTER_FUNC ();

  assert (sock_io_p);
  assert (sock_io_p->read_event);

  read_buffer = &(sock_io_p->read_event->buffer);

  length = get_msg_length (read_buffer->data);
  assert (read_buffer->offset == length);

  /* func code */
  p = (char *) (read_buffer->data + MSG_HEADER_SIZE);
  memcpy (&func_code, p, sizeof (char));
  p += sizeof (char);

  /* shard id */
  memcpy (&shard_id, p, sizeof (int));
  shard_id = ntohl (shard_id);
  p += sizeof (int);

  memcpy (&cas_id, p, sizeof (int));
  cas_id = ntohl (cas_id);

  cas_io_p = proxy_cas_io_new (shard_id, cas_id, sock_io_p->fd);
  if (cas_io_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to create CAS entry. "
		 "(shard_id:%d, cas_id:%d, fd:%d).",
		 shard_id, cas_id, sock_io_p->fd);
      goto error_return;
    }
  assert (sock_io_p->from_cas == PROXY_EVENT_FROM_CAS);

  /* fill socket io entry */
  sock_io_p->status = SOCK_IO_ESTABLISHED;
  sock_io_p->id.shard.shard_id = shard_id;
  sock_io_p->id.shard.cas_id = cas_id;

  /* SHARD TODO : !!! check protocol */


  shard_io_p = proxy_shard_io_find (shard_id);
  assert (shard_io_p);

  /* set cas io status */
  cas_io_p->status = CAS_IO_CONNECTED;

  PROXY_DEBUG_LOG ("New CAS registered. (shard_id:%d). CAS(%s).",
		   shard_io_p->shard_id, proxy_str_cas_io (cas_io_p));

  proxy_client_check_waiter_and_wakeup (shard_io_p, cas_io_p);

  /* in this case, we should free event now */
  proxy_event_free (sock_io_p->read_event);
  sock_io_p->read_event = NULL;

  EXIT_FUNC ();
  return 0;

error_return:

  /* in this case, we should free event now */
  proxy_event_free (sock_io_p->read_event);
  sock_io_p->read_event = NULL;

  if (cas_io_p && shard_id >= 0 && cas_id >= 0)
    {
      proxy_cas_io_free (shard_id, cas_id);
    }

  EXIT_FUNC ();
  return -1;
}

static int
proxy_process_cas_response (T_SOCKET_IO * sock_io_p)
{
  int error;
  int length;
  T_PROXY_CONTEXT *ctx_p;
  T_CAS_IO *cas_io_p;
  T_PROXY_EVENT *event_p;

  ENTER_FUNC ();

  assert (sock_io_p);
  assert (sock_io_p->read_event);
  assert (sock_io_p->status != SOCK_IO_IDLE);
  assert (sock_io_p->from_cas == PROXY_EVENT_FROM_CAS);

  event_p = sock_io_p->read_event;
  sock_io_p->read_event = NULL;

  cas_io_p = proxy_cas_io_find_by_fd (sock_io_p->id.shard.shard_id,
				      sock_io_p->id.shard.cas_id,
				      sock_io_p->fd);
  if (cas_io_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Unable to find CAS entry. "
		 "(shard_id:%d, cas_id:%d, fd:%d). "
		 "event(%s).",
		 sock_io_p->id.shard.shard_id,
		 sock_io_p->id.shard.cas_id,
		 sock_io_p->fd, proxy_str_event (event_p));

      proxy_event_free (event_p);
      event_p = NULL;

      proxy_socket_io_delete (sock_io_p->fd);
      EXIT_FUNC ();
      return -1;
    }

  if (cas_io_p->is_in_tran == false)
    {
      /* in case, cas session timeout !!! */

      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Unexpected CAS transaction status. "
		 "(expected tran status:%d). CAS(%s). event(%s).", true,
		 proxy_str_cas_io (cas_io_p), proxy_str_event (event_p));

      proxy_event_free (event_p);
      event_p = NULL;

      proxy_cas_io_free (cas_io_p->shard_id, cas_io_p->cas_id);
      EXIT_FUNC ();
      return -1;
    }

  ctx_p = proxy_context_find (cas_io_p->ctx_cid, cas_io_p->ctx_uid);
  if (ctx_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Unable to find context. CAS(%s). event(%s).",
		 proxy_str_cas_io (cas_io_p), proxy_str_event (event_p));

      proxy_event_free (event_p);
      event_p = NULL;

      proxy_cas_io_free (cas_io_p->shard_id, cas_io_p->cas_id);
      EXIT_FUNC ();
      return -1;
    }

  proxy_event_set_type_from (event_p, PROXY_EVENT_CAS_RESPONSE,
			     PROXY_EVENT_FROM_CAS);
  proxy_event_set_context (event_p, ctx_p->cid, ctx_p->uid);

  error = shard_queue_enqueue (&proxy_Handler.cas_rcv_q, (void *) event_p);
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to queue client event. "
		 "context(%s). event(%s).",
		 proxy_str_context (ctx_p), proxy_str_event (event_p));

      proxy_event_free (event_p);
      event_p = NULL;

      proxy_context_free (ctx_p);

      EXIT_FUNC ();
      return -1;
    }

  /* in this case, we should detach event from socket io */
  sock_io_p->read_event = NULL;

  EXIT_FUNC ();
  return 0;
}

static int
proxy_process_cas_conn_error (T_SOCKET_IO * sock_io_p)
{
  int error;
  int length;
  char *buffer;
  T_PROXY_CONTEXT *ctx_p;
  T_CAS_IO *cas_io_p;
  T_PROXY_EVENT *event_p;

  ENTER_FUNC ();

  assert (sock_io_p);
  assert (sock_io_p->fd != INVALID_SOCKET);
  assert (sock_io_p->from_cas == PROXY_EVENT_FROM_CAS);

  sock_io_p->status = SOCK_IO_CLOSE_WAIT;

  /* disable socket read/write */
  FD_CLR (sock_io_p->fd, &allset);
  FD_CLR (sock_io_p->fd, &wnewset);

  cas_io_p = proxy_cas_io_find_by_fd (sock_io_p->id.shard.shard_id,
				      sock_io_p->id.shard.cas_id,
				      sock_io_p->fd);
  if (cas_io_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Unable to find CAS entry. "
		 "(shard_id:%d, cas_id:%d, fd:%d). ",
		 sock_io_p->id.shard.shard_id,
		 sock_io_p->id.shard.cas_id, sock_io_p->fd);

      proxy_socket_io_delete (sock_io_p->fd);
      EXIT_FUNC ();
      return -1;
    }

  PROXY_DEBUG_LOG ("Detect CAS connection error. CAS(%s).",
		   proxy_str_cas_io (cas_io_p));

  if (cas_io_p->is_in_tran == false)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Unexpected CAS transaction status. "
		 "(expected tran status:%d). CAS(%s).", true,
		 proxy_str_cas_io (cas_io_p));

      /* __FOR_DEBUG */
      assert (cas_io_p->ctx_cid == PROXY_INVALID_CONTEXT);
      assert (cas_io_p->ctx_uid == 0);

      proxy_cas_io_free (cas_io_p->shard_id, cas_io_p->cas_id);

      EXIT_FUNC ();
      return -1;
    }
  cas_io_p->status = CAS_IO_CLOSE_WAIT;


  ctx_p = proxy_context_find (cas_io_p->ctx_cid, cas_io_p->ctx_uid);
  if (ctx_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Unable to find context. CAS(%s).",
		 proxy_str_cas_io (cas_io_p));

      proxy_cas_io_free (cas_io_p->shard_id, cas_io_p->cas_id);

      EXIT_FUNC ();
      return -1;
    }

  event_p =
    proxy_event_new (PROXY_EVENT_CAS_CONN_ERROR, PROXY_EVENT_FROM_CAS);
  if (event_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Failed to make new event. (%s, %s). context(%s).",
		 "PROXY_EVENT_CAS_CONN_ERROR", "PROXY_EVENT_FROM_CAS",
		 proxy_str_context (ctx_p));

      proxy_context_free (ctx_p);

      EXIT_FUNC ();
      return -1;
    }
  proxy_event_set_context (event_p, ctx_p->cid, ctx_p->uid);

  error = shard_queue_enqueue (&proxy_Handler.cas_rcv_q, (void *) event_p);
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to queue client event. "
		 "context(%s). event(%s).",
		 proxy_str_context (ctx_p), proxy_str_event (event_p));
      proxy_event_free (event_p);
      event_p = NULL;

      proxy_context_free (ctx_p);
      EXIT_FUNC ();
      return -1;
    }

  EXIT_FUNC ();
  return 0;
}

int
proxy_process_cas_write_error (T_SOCKET_IO * sock_io_p)
{
  int error;

  if (sock_io_p->write_event)
    {
      proxy_event_free (sock_io_p->read_event);
      sock_io_p->read_event = NULL;
    }

  error = proxy_process_cas_conn_error (sock_io_p);
  if (error)
    {
      if (sock_io_p->fd != INVALID_SOCKET)
	{
	  CLOSE_SOCKET (sock_io_p->fd);
	}
    }

  return error;
}

int
proxy_process_cas_read_error (T_SOCKET_IO * sock_io_p)
{
  int error;

  assert (sock_io_p);

  assert (sock_io_p->read_event);
  if (sock_io_p->read_event)
    {
      proxy_event_free (sock_io_p->read_event);
      sock_io_p->read_event = NULL;
    }

  error = proxy_process_cas_conn_error (sock_io_p);
  if (error)
    {
      if (sock_io_p->fd != INVALID_SOCKET)
	{
	  CLOSE_SOCKET (sock_io_p->fd);
	}
    }

  return error;
}

static int
proxy_process_cas_message (T_SOCKET_IO * sock_io_p)
{
  int error = 0;

  assert (sock_io_p);

  switch (sock_io_p->status)
    {
    case SOCK_IO_REG_WAIT:
      error = proxy_process_cas_register (sock_io_p);
      break;

    case SOCK_IO_ESTABLISHED:
      error = proxy_process_cas_response (sock_io_p);
      break;

    default:
      break;
    }

  return error;
}

static int
proxy_socket_io_write_internal (T_SOCKET_IO * sock_io_p)
{
  int error;
  int write_len;
  int remain;
  char *p;
  T_IO_BUFFER *send_buffer;

  // __FOR_DEBUG
  assert (sock_io_p);
  assert (sock_io_p->write_event);

  send_buffer = &(sock_io_p->write_event->buffer);

  if (send_buffer->length == send_buffer->offset)
    {
      write_len = 0;
      goto write_end;
    }
  else if (send_buffer->length < send_buffer->offset)
    {
      assert (false);

      write_len = -1;
      goto write_end;
    }

  remain = send_buffer->length - send_buffer->offset;

  p = (char *) (send_buffer->data + send_buffer->offset);

  write_len = WRITESOCKET (sock_io_p->fd, p, remain);
  if (write_len < 0)
    {
#if defined(WINDOWS)
      error = WSAGetLastError ();
      if (error == WSAEWOULDBLOCK)
#else
      if ((errno == EWOULDBLOCK) || (errno == EAGAIN) || (errno == EINTR))
#endif
	{
	  FD_SET (sock_io_p->fd, &wnewset);
	  return 0;
	}

      goto write_end;
    }

  if (write_len < remain)
    {
      FD_SET (sock_io_p->fd, &wnewset);
      return write_len;
    }

write_end:

  proxy_event_free (sock_io_p->write_event);
  sock_io_p->write_event = NULL;

  FD_CLR (sock_io_p->fd, &wnewset);

  return write_len;
}

static void
proxy_socket_io_write_to_cas (T_SOCKET_IO * sock_io_p)
{
  int len;

  assert (sock_io_p);

  len = proxy_socket_io_write_internal (sock_io_p);
  if (len < 0)
    {
      proxy_socket_io_write_error (sock_io_p);
    }

  return;
}

static void
proxy_socket_io_write_to_client (T_SOCKET_IO * sock_io_p)
{
  int len;
  T_PROXY_CONTEXT *ctx_p;
  T_CLIENT_INFO *client_info_p;

  assert (sock_io_p);

  len = proxy_socket_io_write_internal (sock_io_p);
  if (len < 0)
    {
      proxy_socket_io_write_error (sock_io_p);
    }

  ctx_p = proxy_context_find_by_socket_client_io (sock_io_p);
  if (ctx_p == NULL)
    {
      return;
    }

  if (ctx_p->free_on_client_io_write)
    {
      /* init shared memory T_CLIENT_INFO */
      client_info_p =
	shard_shm_get_client_info (proxy_info_p, sock_io_p->id.client_id);
      shard_shm_init_client_info (client_info_p);

      sock_io_p->status = SOCK_IO_CLOSE_WAIT;
      proxy_context_free (ctx_p);
    }

  return;
}

static int
proxy_socket_io_read_internal (T_SOCKET_IO * sock_io_p)
{
  int error;
  int read_len, remain, total_len;
  char *buffer;
  T_IO_BUFFER *read_buffer;

  assert (sock_io_p);
  assert (sock_io_p->read_event);

read_again:

  read_buffer = &(sock_io_p->read_event->buffer);
  buffer = (char *) (read_buffer->data + read_buffer->offset);
  remain = read_buffer->length - read_buffer->offset;

  read_len = READSOCKET (sock_io_p->fd, buffer, remain);
  if (read_len < 0)
    {
#if defined(WINDOWS)
      error = WSAGetLastError ();
      if ((error == WSAECONNRESET) || (error == WSAECONNABORTED))
	{
	  return -1;		/* failed */
	}
      else if (error == WSAEWOULDBLOCK)
#else
      if ((errno == EWOULDBLOCK) || (errno == EAGAIN) || (errno == EINTR))
#endif
	{
	  return 0;		/* retry */
	}
      return -1;		/* disconnected */
    }
  else if (read_len == 0)
    {
      return -1;		/* disconnected */
    }

  read_buffer->offset += read_len;

  /* common message header */
  if (read_buffer->length == MSG_HEADER_SIZE
      && read_buffer->length == read_buffer->offset)
    {
      /* expand buffer to receive payload */
      total_len = get_msg_length (read_buffer->data);
      if (total_len == read_buffer->offset)
	{
	  /* no more data */
	  return read_buffer->offset;
	}

      error = proxy_event_realloc_buffer (sock_io_p->read_event, total_len);
      if (error)
	{
	  PROXY_DEBUG_LOG ("Failed to realloc event buffer. (error:%d).",
			   error);
	  proxy_socket_io_read_error (sock_io_p);
	  return -1;
	}

      goto read_again;
    }

  return read_len;
}

/* proxy_socket_io_read_internal */
static void
proxy_socket_io_read_from_cas_next (T_SOCKET_IO * sock_io_p)
{
  int error;
  T_IO_BUFFER *r_buf;

  assert (sock_io_p);

  error = proxy_socket_io_read_internal (sock_io_p);
  if (error < 0)
    {
      proxy_socket_io_read_error (sock_io_p);
      return;
    }

  if (proxy_event_io_read_complete (sock_io_p->read_event))
    {
      proxy_process_cas_message (sock_io_p);
    }

  return;
}

static void
proxy_socket_io_read_from_cas_first (T_SOCKET_IO * sock_io_p)
{
  int error;
  int length;
  char *buffer;

  assert (sock_io_p);
  assert (sock_io_p->read_event);

  length = MSG_HEADER_SIZE;
  error = proxy_event_alloc_buffer (sock_io_p->read_event, length);
  if (error)
    {
      proxy_socket_io_read_error (sock_io_p);
      return;
    }

  return proxy_socket_io_read_from_cas_next (sock_io_p);
}

static void
proxy_socket_io_read_from_cas (T_SOCKET_IO * sock_io_p)
{
  T_IO_BUFFER *r_buf;

  assert (sock_io_p);
  assert (sock_io_p->read_event);

  r_buf = &(sock_io_p->read_event->buffer);

  if (r_buf->length == 0)
    {
      proxy_socket_io_read_from_cas_first (sock_io_p);
    }
  else
    {
      proxy_socket_io_read_from_cas_next (sock_io_p);
    }

  return;
}

static void
proxy_socket_io_read_from_client_next (T_SOCKET_IO * sock_io_p)
{
  int error;
  T_IO_BUFFER *r_buf;

  assert (sock_io_p);

  error = proxy_socket_io_read_internal (sock_io_p);
  if (error < 0)
    {
      proxy_socket_io_read_error (sock_io_p);
      return;
    }

  if (proxy_event_io_read_complete (sock_io_p->read_event))
    {
      proxy_process_client_message (sock_io_p);
    }

  return;
}

static void
proxy_socket_io_read_from_client_first (T_SOCKET_IO * sock_io_p)
{
  int error;
  int length;

  assert (sock_io_p);
  assert (sock_io_p->read_event);

  if (sock_io_p->status == SOCK_IO_REG_WAIT)
    {
      length = SRV_CON_DB_INFO_SIZE;
    }
  else
    {
      length = MSG_HEADER_SIZE;
    }

  error = proxy_event_alloc_buffer (sock_io_p->read_event, length);
  if (error)
    {
      proxy_socket_io_write_error (sock_io_p);
      return;
    }

  return proxy_socket_io_read_from_client_next (sock_io_p);
}

static void
proxy_socket_io_read_from_client (T_SOCKET_IO * sock_io_p)
{
  T_IO_BUFFER *r_buf;

  assert (sock_io_p);
  assert (sock_io_p->read_event);

  r_buf = &(sock_io_p->read_event->buffer);

  if (r_buf->length == 0)
    {
      proxy_socket_io_read_from_client_first (sock_io_p);
    }
  else
    {
      proxy_socket_io_read_from_client_next (sock_io_p);
    }

  return;
}

static void
proxy_socket_io_write (T_SOCKET_IO * sock_io_p)
{
  assert (sock_io_p);

  if (sock_io_p->status == SOCK_IO_CLOSE_WAIT)
    {
      PROXY_DEBUG_LOG ("Unexpected socket status. (fd:%d, status:%d). \n",
		       sock_io_p->fd, sock_io_p->status);

      /* 
       * free writer event when sock status is 'close wait' 
       */
      if (sock_io_p->write_event)
	{
	  proxy_event_free (sock_io_p->write_event);
	  sock_io_p->write_event = NULL;
	}

      FD_CLR (sock_io_p->fd, &wnewset);

      assert (false);
      return;
    }

  if (sock_io_p->write_event == NULL)
    {
      FD_CLR (sock_io_p->fd, &wnewset);

      PROXY_DEBUG_LOG ("Write event couldn't be NULL. (fd:%d, status:%d). \n",
		       sock_io_p->fd, sock_io_p->status);
      return;
    }

  if (sock_io_p->from_cas)
    {
      proxy_socket_io_write_to_cas (sock_io_p);
    }
  else
    {
      proxy_socket_io_write_to_client (sock_io_p);
    }

  return;
}

static void
proxy_socket_io_write_error (T_SOCKET_IO * sock_io_p)
{
  assert (sock_io_p);

  if (sock_io_p->from_cas)
    {
      proxy_process_cas_write_error (sock_io_p);
    }
  else
    {
      proxy_process_client_write_error (sock_io_p);
    }

  return;
}

static void
proxy_socket_io_read (T_SOCKET_IO * sock_io_p)
{
  assert (sock_io_p);

  if (sock_io_p->status == SOCK_IO_CLOSE_WAIT)
    {
      /* this socket connection will be close */

      PROXY_DEBUG_LOG ("Unexpected socket status. "
		       "socket will be closed. "
		       "(fd:%d, status:%d).",
		       sock_io_p->fd, sock_io_p->status);
      //
      //proxy_io_buffer_clear (&sock_io_p->recv_buffer);

      //assert (false);
      return;
    }

  if (sock_io_p->read_event == NULL)
    {
      sock_io_p->read_event = proxy_event_new (PROXY_EVENT_IO_READ,
					       sock_io_p->from_cas);
      if (sock_io_p->read_event == NULL)
	{
	  assert (false);	/* __FOR_DEBUG */

	  proxy_socket_io_read_error (sock_io_p);

	  return;
	}
    }

  /* __FOR_DEBUG */
  assert (sock_io_p->read_event->type == PROXY_EVENT_IO_READ);

  if (sock_io_p->from_cas)
    {
      proxy_socket_io_read_from_cas (sock_io_p);
    }
  else
    {
      proxy_socket_io_read_from_client (sock_io_p);
    }

  return;
}

static void
proxy_socket_io_read_error (T_SOCKET_IO * sock_io_p)
{
  assert (sock_io_p);

  if (sock_io_p->from_cas)
    {
      proxy_process_cas_read_error (sock_io_p);
    }
  else
    {
      proxy_process_client_read_error (sock_io_p);
    }

  return;
}

static int
proxy_client_io_initialize (void)
{
  int error;
  int i;
  T_CLIENT_IO *client_io_ent_p;

  proxy_Client_io.max_client = shm_proxy_p->max_client;
  proxy_Client_io.cur_client = 0;

  error =
    shard_cqueue_initialize (&proxy_Client_io.freeq,
			     proxy_Client_io.max_client);
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to initialize client entries. "
		 "(error:%d).", error);
      return -1;
    }

  /* make client io entry */
  proxy_Client_io.ent =
    (T_CLIENT_IO *) malloc (sizeof (T_CLIENT_IO) *
			    proxy_Client_io.max_client);
  if (proxy_Client_io.ent == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Not enough virtual memory. "
		 "Failed to alloc client entries. "
		 "(errno:%d, size:%d).", errno,
		 sizeof (T_CLIENT_IO) * proxy_Client_io.max_client);
      goto error_return;
    }

  for (i = 0; i < proxy_Client_io.max_client; i++)
    {
      client_io_ent_p = &(proxy_Client_io.ent[i]);

      client_io_ent_p->client_id = i;
      client_io_ent_p->is_busy = false;
      client_io_ent_p->fd = INVALID_SOCKET;
      client_io_ent_p->ctx_cid = PROXY_INVALID_CONTEXT;
      client_io_ent_p->ctx_uid = 0;

      error =
	shard_cqueue_enqueue (&proxy_Client_io.freeq,
			      (void *) client_io_ent_p);
      if (error)
	{
	  PROXY_LOG (PROXY_LOG_MODE_ERROR,
		     "Failed to initialize client free queue."
		     "(error:%d).", error);
	  goto error_return;
	}
    }

  return 0;

error_return:
  proxy_client_io_destroy ();

  return -1;
}

static void
proxy_client_io_destroy (void)
{
  shard_cqueue_destroy (&proxy_Client_io.freeq);
  FREE_MEM (proxy_Client_io.ent);
  proxy_Client_io.max_client = -1;
  proxy_Client_io.cur_client = 0;
}

#if defined(PROXY_VERBOSE_DEBUG)
void
proxy_client_io_print (bool print_all)
{
  int i;
  T_CLIENT_IO *cli_io_p = NULL;

  PROXY_LOG (PROXY_LOG_MODE_SCHEDULE_DETAIL, "* CLIENT IO *\n");
  PROXY_LOG (PROXY_LOG_MODE_SCHEDULE_DETAIL, "%-20s = %d",
	     "max_client", proxy_Client_io.max_client);
  PROXY_LOG (PROXY_LOG_MODE_SCHEDULE_DETAIL, "%-20s = %d",
	     "cur_client", proxy_Client_io.cur_client);

  PROXY_LOG (PROXY_LOG_MODE_SCHEDULE_DETAIL,
	     "%-7s    %-10s %-5s %-5s %-10s %-10s", "idx", "client_id",
	     "busy", "fd", "context_id", "uid");
  if (proxy_Client_io.ent)
    {
      for (i = 0; i < proxy_Client_io.max_client; i++)
	{
	  cli_io_p = &(proxy_Client_io.ent[i]);
	  if (!print_all && !cli_io_p->is_busy)
	    {
	      continue;
	    }
	  PROXY_LOG (PROXY_LOG_MODE_SCHEDULE_DETAIL,
		     "[%-5d]     %-10d %-5s %-5d %-10d %-10u", i,
		     cli_io_p->client_id,
		     (cli_io_p->is_busy) ? "YES" : "NO", cli_io_p->fd,
		     cli_io_p->ctx_cid, cli_io_p->ctx_uid);
	}
    }

  return;
}
#endif /* PROXY_VERBOSE_DEBUG */

char *
proxy_str_client_io (T_CLIENT_IO * cli_io_p)
{
  static char buffer[LINE_MAX];

  if (cli_io_p == NULL)
    {
      return (char *) "-";
    }

  snprintf (buffer, sizeof (buffer),
	    "client_id:%d, is_busy:%s, fd:%d, ctx_cid:%d, ctx_uid:%u",
	    cli_io_p->client_id, (cli_io_p->is_busy) ? "Y" : "N",
	    cli_io_p->fd, cli_io_p->ctx_cid, cli_io_p->ctx_uid);

  return (char *) buffer;
}

static T_CLIENT_IO *
proxy_client_io_new (SOCKET fd)
{
  T_PROXY_CONTEXT *ctx_p;
  T_CLIENT_IO *cli_io_p = NULL;

  cli_io_p = (T_CLIENT_IO *) shard_cqueue_dequeue (&proxy_Client_io.freeq);

  if (cli_io_p)
    {
      cli_io_p->fd = fd;
      cli_io_p->is_busy = true;

      proxy_Client_io.cur_client++;
      if (proxy_Client_io.cur_client > proxy_Client_io.max_client)
	{
	  assert (false);
	  proxy_Client_io.cur_client = proxy_Client_io.max_client;
	}
      proxy_info_p->cur_client = proxy_Client_io.cur_client;

      ctx_p = proxy_context_new ();
      if (ctx_p == NULL)
	{
	  PROXY_LOG (PROXY_LOG_MODE_ERROR,
		     "Unable to make new context. client(%s).",
		     proxy_str_client_io (cli_io_p));

	  proxy_client_io_free (cli_io_p);
	  return NULL;
	}

      cli_io_p->ctx_cid = ctx_p->cid;
      cli_io_p->ctx_uid = ctx_p->uid;

      ctx_p->client_id = cli_io_p->client_id;

      PROXY_LOG (PROXY_LOG_MODE_SHARD_DETAIL,
		 "New client connected. client(%s).",
		 proxy_str_client_io (cli_io_p));
    }

#if defined(PROXY_VERBOSE_DEBUG)
  proxy_client_io_print (false);
#endif /* PROXY_VERBOSE_DEBUG */

  return cli_io_p;
}

void
proxy_client_io_free (T_CLIENT_IO * cli_io_p)
{
  int error;

  assert (cli_io_p->fd != INVALID_SOCKET);
  assert (cli_io_p->is_busy == true);

  proxy_socket_io_delete (cli_io_p->fd);

  cli_io_p->fd = INVALID_SOCKET;
  cli_io_p->is_busy = false;
  cli_io_p->ctx_cid = PROXY_INVALID_CONTEXT;
  cli_io_p->ctx_uid = 0;

  proxy_Client_io.cur_client--;
  if (proxy_Client_io.cur_client < 0)
    {
      assert (false);
      proxy_Client_io.cur_client = 0;
    }
  proxy_info_p->cur_client = proxy_Client_io.cur_client;

  error = shard_cqueue_enqueue (&proxy_Client_io.freeq, (void *) cli_io_p);
  if (error)
    {
      assert (false);
      return;
    }

  return;
}

void
proxy_client_io_free_by_ctx (int client_id, int ctx_cid, int ctx_uid)
{
  T_CLIENT_IO *cli_io_p;

  cli_io_p = proxy_client_io_find_by_ctx (client_id, ctx_cid, ctx_uid);
  if (cli_io_p == NULL)
    {
      return;
    }

  proxy_client_io_free (cli_io_p);

  return;
}


T_CLIENT_IO *
proxy_client_io_find_by_ctx (int client_id, int ctx_cid, unsigned int ctx_uid)
{
  T_CLIENT_IO *cli_io_p;

  if (client_id < 0 || client_id >= proxy_Client_io.max_client)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Invalid client id. (client_id;%d, max_client:%d).",
		 client_id, proxy_Client_io.max_client);
      return NULL;
    }

  cli_io_p = &(proxy_Client_io.ent[client_id]);

  if (cli_io_p->ctx_cid != ctx_cid || cli_io_p->ctx_uid != ctx_uid)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Unable to find client by context. "
		 "(context id:%d/%d, context uid:%d/%d).", cli_io_p->ctx_cid,
		 ctx_cid, cli_io_p->ctx_uid, ctx_uid);
      return NULL;
    }

  return (cli_io_p->is_busy) ? cli_io_p : NULL;
}

T_CLIENT_IO *
proxy_client_io_find_by_fd (int client_id, SOCKET fd)
{
  T_CLIENT_IO *cli_io_p;

  if (client_id < 0 || client_id >= proxy_Client_io.max_client)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Invalid client id. (client_id;%d, max_client:%d).",
		 client_id, proxy_Client_io.max_client);
      return NULL;
    }

  cli_io_p = &(proxy_Client_io.ent[client_id]);

  /* client io's socket fd must be the same requested fd */
  assert (cli_io_p->fd == fd);

  return (cli_io_p->is_busy) ? cli_io_p : NULL;
}

int
proxy_client_io_write (T_CLIENT_IO * cli_io_p, T_PROXY_EVENT * event_p)
{
  int error;

  T_SOCKET_IO *sock_io_p;

  assert (cli_io_p);
  assert (event_p);

  sock_io_p = proxy_socket_io_find (cli_io_p->fd);
  if (sock_io_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Failed to find socket entry. (fd:%d).", cli_io_p->fd);
      return -1;
    }

  error = proxy_socket_set_write_event (sock_io_p, event_p);
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to set write buffer. "
		 "client(%s). event(%s).",
		 proxy_str_client_io (cli_io_p), proxy_str_event (event_p));
      return -1;
    }

  return 0;
}

static int
proxy_cas_io_initialize (int shard_id, T_CAS_IO ** cas_io_pp, int size)
{
  int i;
  T_CAS_IO *cas_io_p;
  T_CAS_IO *buffer;

  assert (cas_io_pp);

  buffer = (T_CAS_IO *) malloc (sizeof (T_CAS_IO) * size);
  if (buffer == NULL)
    {
      return -1;
    }

  for (i = 0; i < size; i++)
    {
      cas_io_p = &(buffer[i]);

      cas_io_p->cas_id = i;
      cas_io_p->shard_id = shard_id;
      cas_io_p->is_in_tran = false;
      cas_io_p->ctx_cid = PROXY_INVALID_CONTEXT;
      cas_io_p->ctx_uid = 0;
      cas_io_p->fd = INVALID_SOCKET;
    }

  *cas_io_pp = buffer;

  return 0;
}

int
proxy_cas_io_write (T_CAS_IO * cas_io_p, T_PROXY_EVENT * event_p)
{
  int error;
  T_SOCKET_IO *sock_io_p;

  assert (cas_io_p);
  assert (event_p);

  sock_io_p = proxy_socket_io_find (cas_io_p->fd);
  if (sock_io_p == NULL)
    {
      assert (false);
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Unable to find socket entry. (fd:%d).", cas_io_p->fd);
      return -1;
    }

  error = proxy_socket_set_write_event (sock_io_p, event_p);
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to set write buffer. "
		 "CAS(%s). event(%s).",
		 proxy_str_cas_io (cas_io_p), proxy_str_event (event_p));
      return -1;
    }

  return 0;
}

static int
proxy_shard_io_initialize (void)
{
  int error;
  int i;
  T_SHARD_IO *shard_io_p;
  T_SHARD_INFO *shard_info_p;
  int max_appl_server;

  proxy_Shard_io.max_shard = proxy_info_p->max_shard;

  proxy_Shard_io.ent =
    (T_SHARD_IO *) malloc (sizeof (T_SHARD_IO) * proxy_Shard_io.max_shard);
  if (proxy_Shard_io.ent == NULL)
    {
      return -1;
    }

  shard_info_p = shard_shm_get_first_shard_info (proxy_info_p);
  max_appl_server = shard_info_p->max_appl_server;

  for (i = 0; i < proxy_Shard_io.max_shard; i++)
    {
      shard_io_p = &(proxy_Shard_io.ent[i]);

      shard_io_p->shard_id = i;
      shard_io_p->max_num_cas = max_appl_server;
      shard_io_p->cur_num_cas = 0;
      shard_io_p->num_cas_in_tran = 0;
      error = shard_queue_initialize (&shard_io_p->waitq);
      if (error)
	{
	  goto error_return;
	}

      error =
	proxy_cas_io_initialize (shard_io_p->shard_id, &shard_io_p->ent,
				 shard_io_p->max_num_cas);
      if (error)
	{
	  goto error_return;
	}
    }

  return 0;

error_return:
  proxy_shard_io_destroy ();
  return -1;
}

static void
proxy_shard_io_destroy (void)
{
  int i, j;
  T_SHARD_IO *shard_io_p;

  for (i = 0; i < proxy_Shard_io.max_shard; i++)
    {
      shard_io_p = &(proxy_Shard_io.ent[i]);

      shard_queue_destroy (&shard_io_p->waitq);
      FREE_MEM (shard_io_p->ent);
    }

  FREE_MEM (proxy_Shard_io.ent);

  return;
}

#if defined(PROXY_VERBOSE_DEBUG)
void
proxy_shard_io_print (bool print_all)
{
  int i, j;
  T_SHARD_IO *shard_io_p;
  T_CAS_IO *cas_io_p;

  PROXY_LOG (PROXY_LOG_MODE_SCHEDULE_DETAIL, "* SHARD IO *");
  PROXY_LOG (PROXY_LOG_MODE_SCHEDULE_DETAIL, "%-20s = %d",
	     "max_shard", proxy_Shard_io.max_shard);

  PROXY_LOG (PROXY_LOG_MODE_SCHEDULE_DETAIL,
	     "%-7s    %-15s %-15s %-15s %-15s", "idx", "shard_id",
	     "max_cas", "cur_cas", "in_tran");
  if (proxy_Shard_io.ent)
    {
      for (i = 0; i < proxy_Shard_io.max_shard; i++)
	{
	  shard_io_p = &(proxy_Shard_io.ent[i]);

	  PROXY_LOG (PROXY_LOG_MODE_SCHEDULE_DETAIL,
		     "[%-5d]     %-15d %-15d %-15d %-15d", i,
		     shard_io_p->shard_id, shard_io_p->max_num_cas,
		     shard_io_p->cur_num_cas, shard_io_p->num_cas_in_tran);

	  PROXY_LOG (PROXY_LOG_MODE_SCHEDULE_DETAIL,
		     "       <cas>   %-7s    %-10s %-10s %-10s %-10s",
		     "idx", "cas_id", "shard_id", "in_tran", "fd");
	  if (shard_io_p->ent)
	    {
	      for (j = 0; j < shard_io_p->max_num_cas; j++)
		{
		  cas_io_p = &(shard_io_p->ent[j]);
		  if (!print_all && cas_io_p->fd <= INVALID_SOCKET)
		    {
		      continue;
		    }

		  PROXY_LOG (PROXY_LOG_MODE_SCHEDULE_DETAIL,
			     "               [%-5d]    %-10d %-10d %-10s %-10d",
			     j, cas_io_p->cas_id, cas_io_p->shard_id,
			     (cas_io_p->is_in_tran) ? "YES" : "NO",
			     cas_io_p->fd);
		}
	    }
	}
    }

  return;
}
#endif /* PROXY_VERBOSE_DEBUG */

char *
proxy_str_cas_io (T_CAS_IO * cas_io_p)
{
  static char buffer[LINE_MAX];

  if (cas_io_p == NULL)
    {
      return (char *) "-";
    }

  snprintf (buffer, sizeof (buffer),
	    "cas_id:%d, shard_id:%d, is_in_tran:%s, "
	    "status:%d, ctx_cid:%d, ctx_uid:%u, fd:%d",
	    cas_io_p->cas_id, cas_io_p->shard_id,
	    (cas_io_p->is_in_tran) ? "Y" : "N",
	    cas_io_p->status, cas_io_p->ctx_cid, cas_io_p->ctx_uid,
	    cas_io_p->fd);

  return (char *) buffer;
}

static T_SHARD_IO *
proxy_shard_io_find (int shard_id)
{
  T_SHARD_IO *shard_io_p;

  if (shard_id < 0 || shard_id >= proxy_Shard_io.max_shard)
    {
      PROXY_DEBUG_LOG ("Invalid shard id. (shard_id:%d, max_shard:%d).",
		       shard_id, proxy_Shard_io.max_shard);
      return NULL;
    }

  shard_io_p = &(proxy_Shard_io.ent[shard_id]);

  return shard_io_p;
}

static T_CAS_IO *
proxy_cas_io_new (int shard_id, int cas_id, SOCKET fd)
{
  T_SHARD_IO *shard_io_p;
  T_CAS_IO *cas_io_p;

  if (shard_id < 0 || shard_id >= proxy_Shard_io.max_shard)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Invalid shard id. (shard_id:%d, max_shard:%d).",
		 shard_id, proxy_Shard_io.max_shard);
      return NULL;
    }

  shard_io_p = &(proxy_Shard_io.ent[shard_id]);
  if (cas_id < 0 || cas_id >= shard_io_p->max_num_cas)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Invalid CAS id. (cas_id:%d, max_num_cas:%d).", cas_id,
		 shard_io_p->max_num_cas);
      return NULL;
    }

  cas_io_p = &(shard_io_p->ent[cas_id]);

  if (cas_io_p->fd != INVALID_SOCKET || cas_io_p->is_in_tran)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Already registered CAS. "
		 "(shard_id:%d, cas_id:%d, max_num_cas:%d).",
		 shard_id, cas_id, shard_io_p->max_num_cas);
      return NULL;
    }
  assert (cas_io_p->fd == INVALID_SOCKET);
  assert (cas_io_p->is_in_tran == false);

  cas_io_p->status = CAS_IO_NOT_CONNECTED;
  cas_io_p->fd = fd;
  cas_io_p->is_in_tran = false;
  cas_io_p->ctx_cid = PROXY_INVALID_CONTEXT;
  cas_io_p->ctx_uid = 0;

  shard_io_p->cur_num_cas++;

  PROXY_LOG (PROXY_LOG_MODE_SHARD_DETAIL, "New CAS connected. CAS(%s).",
	     proxy_str_cas_io (cas_io_p));
#if defined(PROXY_VERBOSE_DEBUG)
  proxy_shard_io_print (false);
#endif /* PROXY_VERBOSE_DEBUG */

  return cas_io_p;
}

/* by socket event */
static void
proxy_cas_io_free (int shard_id, int cas_id)
{
  T_SHARD_IO *shard_io_p;
  T_CAS_IO *cas_io_p;

  ENTER_FUNC ();

  if (shard_id < 0 || shard_id >= proxy_Shard_io.max_shard)
    {
      PROXY_DEBUG_LOG ("Invalid shard id. (shard_id:%d, max_shard:%d).",
		       shard_id, proxy_Shard_io.max_shard);
      EXIT_FUNC ();
      return;
    }

  shard_io_p = &(proxy_Shard_io.ent[shard_id]);
  if (cas_id < 0 || cas_id >= shard_io_p->max_num_cas)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Invalid CAS id. (cas_id:%d, max_num_cas:%d).", cas_id,
		 shard_io_p->max_num_cas);
      EXIT_FUNC ();
      return;
    }

  cas_io_p = &(shard_io_p->ent[cas_id]);

  proxy_socket_io_delete (cas_io_p->fd);

  if (cas_io_p->fd != INVALID_SOCKET)
    {
      cas_io_p->fd = INVALID_SOCKET;
      shard_stmt_del_all_srv_h_id_for_shard_cas (cas_io_p->shard_id,
						 cas_io_p->cas_id);
    }

  if (cas_io_p->is_in_tran == true)
    {
      shard_io_p->num_cas_in_tran--;

      PROXY_DEBUG_LOG ("shard/CAS status. (num_cas_in_tran=%d, shard_id=%d).",
		       shard_io_p->num_cas_in_tran, shard_io_p->shard_id);

      assert (shard_io_p->num_cas_in_tran >= 0);
      if (shard_io_p->num_cas_in_tran < 0)
	{
	  shard_io_p->num_cas_in_tran = 0;
	}
    }


  cas_io_p->is_in_tran = false;
  cas_io_p->ctx_cid = PROXY_INVALID_CONTEXT;
  cas_io_p->ctx_uid = 0;

  cas_io_p->status = CAS_IO_NOT_CONNECTED;
  shard_io_p->cur_num_cas--;

  EXIT_FUNC ();
  return;
}

/* by context */
void
proxy_cas_io_free_by_ctx (int shard_id, int cas_id, int ctx_cid,
			  int unsigned ctx_uid)
{
  T_SHARD_IO *shard_io_p;
  T_CAS_IO *cas_io_p;

  ENTER_FUNC ();

  if (shard_id < 0 || shard_id >= proxy_Shard_io.max_shard)
    {

      PROXY_DEBUG_LOG ("Invalid shard id. (shard_id:%d, max_shard:%d).",
		       shard_id, proxy_Shard_io.max_shard);
      EXIT_FUNC ();
      return;
    }

  shard_io_p = &(proxy_Shard_io.ent[shard_id]);
  if (cas_id < 0 || cas_id >= shard_io_p->max_num_cas)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Invalid CAS id. (cas_id:%d, max_num_cas:%d).", cas_id,
		 shard_io_p->max_num_cas);
      EXIT_FUNC ();
      return;
    }

  cas_io_p = &(shard_io_p->ent[cas_id]);

  if (cas_io_p->is_in_tran == false || cas_io_p->ctx_cid != ctx_cid
      || cas_io_p->ctx_uid != ctx_uid)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Unable to find CAS entry. "
		 "(context id:%d, context uid:%d). CAS(%S).",
		 ctx_cid, ctx_uid, proxy_str_cas_io (cas_io_p));

      assert (false);

      EXIT_FUNC ();
      return;
    }

  proxy_socket_io_delete (cas_io_p->fd);

  if (cas_io_p->fd != INVALID_SOCKET)
    {
      cas_io_p->fd = INVALID_SOCKET;
      shard_stmt_del_all_srv_h_id_for_shard_cas (cas_io_p->shard_id,
						 cas_io_p->cas_id);
    }

  cas_io_p->is_in_tran = false;
  cas_io_p->status = CAS_IO_NOT_CONNECTED;
  cas_io_p->ctx_cid = PROXY_INVALID_CONTEXT;
  cas_io_p->ctx_uid = 0;

  shard_io_p->cur_num_cas--;
  assert (shard_io_p->cur_num_cas >= 0);
  if (shard_io_p->cur_num_cas < 0)
    {
      shard_io_p->cur_num_cas = 0;
    }

  shard_io_p->num_cas_in_tran--;
  PROXY_DEBUG_LOG ("Shard/CAS status. (num_cas_in_tran=%d, shard_id=%d).",
		   shard_io_p->num_cas_in_tran, shard_io_p->shard_id);
  assert (shard_io_p->num_cas_in_tran >= 0);
  if (shard_io_p->num_cas_in_tran < 0)
    {
      shard_io_p->num_cas_in_tran = 0;
    }

  EXIT_FUNC ();
  return;
}

static T_CAS_IO *
proxy_cas_io_find_by_fd (int shard_id, int cas_id, SOCKET fd)
{
  T_SHARD_IO *shard_io_p;
  T_CAS_IO *cas_io_p;

  if (shard_id < 0 || cas_id < 0 || fd == INVALID_SOCKET)
    {
      PROXY_DEBUG_LOG
	("Unable to find CAS entry. "
	 "(shard:%d, cas:%d, fd:%d).", shard_id, cas_id, fd);
      return NULL;
    }

  /* __FOR_DEBUG */
  assert (shard_id <= proxy_Shard_io.max_shard);
  shard_io_p = &(proxy_Shard_io.ent[shard_id]);

  /* __FOR_DEBUG */
  assert (cas_id <= shard_io_p->max_num_cas);
  cas_io_p = &(shard_io_p->ent[cas_id]);

  assert (cas_io_p->fd == fd);

  return cas_io_p;
}

T_CAS_IO *
proxy_cas_find_io_by_ctx (int shard_id, int cas_id, int ctx_cid,
			  unsigned int ctx_uid)
{
  /* in case, find cas explicitly */
  T_SHARD_IO *shard_io_p;
  T_CAS_IO *cas_io_p;

  if (0 > shard_id || shard_id >= proxy_Shard_io.max_shard)
    {
      PROXY_DEBUG_LOG ("Invalid shard id. (shard_id:%d, max_shard:%d).",
		       shard_id, proxy_Shard_io.max_shard);
      return NULL;
    }
  shard_io_p = &(proxy_Shard_io.ent[shard_id]);

  if (0 > cas_id || cas_id >= shard_io_p->max_num_cas)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Invalid CAS id. (cas_id:%d, max_num_cas:%d).", cas_id,
		 shard_io_p->max_num_cas);
      return NULL;
    }
  cas_io_p = &(shard_io_p->ent[cas_id]);

  if (cas_io_p->is_in_tran == false || cas_io_p->ctx_cid != ctx_cid
      || cas_io_p->ctx_uid != ctx_uid)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Unable to find CAS entry. "
		 "(context id:%d, context uid:%d). CAS(%S).",
		 ctx_cid, ctx_uid, proxy_str_cas_io (cas_io_p));
      return NULL;
    }

  return cas_io_p;
}

T_CAS_IO *
proxy_cas_alloc_by_ctx (int shard_id, int cas_id, int ctx_cid,
			unsigned int ctx_uid)
{
  int error;
  T_SHARD_IO *shard_io_p;
  T_CAS_IO *cas_io_p;
  unsigned int retry_count = 0;

  int i;
  unsigned int curr_shard_id;
  static unsigned int last_shard_id = 0;
  const unsigned int max_retry = (proxy_Shard_io.max_shard / 2);

  /* valid shard id */
  if ((shard_id < 0 && cas_id >= 0) || (shard_id >= proxy_Shard_io.max_shard))
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Invalid shard/CAS id is requested. "
		 "(shard_id:%d, cas_id:%d). ", shard_id, cas_id);

      assert (false);

      return NULL;
    }

  if (shard_id >= 0 && cas_id >= 0)
    {
      shard_io_p = &(proxy_Shard_io.ent[shard_id]);

      if (0 > cas_id || cas_id >= shard_io_p->max_num_cas)
	{
	  PROXY_LOG (PROXY_LOG_MODE_ERROR, "Unable to find available CAS. "
		     "(shard_id:%d, cas_id:%d, max_num_cas:%d).", shard_id,
		     cas_id, shard_io_p->max_num_cas);
	  return NULL;
	}
      cas_io_p = &(shard_io_p->ent[cas_id]);

      if ((cas_io_p->ctx_cid != PROXY_INVALID_CONTEXT
	   && cas_io_p->ctx_cid != ctx_cid)
	  || (cas_io_p->ctx_uid != 0 && cas_io_p->ctx_uid != ctx_uid))
	{
	  PROXY_LOG (PROXY_LOG_MODE_ERROR, "Invalid CAS status. "
		     "(context id:%d, context uid:%d). CAS(%s). ", ctx_cid,
		     ctx_uid, proxy_str_cas_io (cas_io_p));

	  assert (false);
	  return NULL;
	}

      if (cas_io_p->status != CAS_IO_CONNECTED)
	{
	  PROXY_DEBUG_LOG
	    ("Unexpected CAS status. (context id:%d, context uid:%d). "
	     "CAS(%s). ", ctx_cid, ctx_uid, proxy_str_cas_io (cas_io_p));
	  return NULL;
	}

      if (cas_io_p->ctx_cid == PROXY_INVALID_CONTEXT)
	{
	  shard_io_p->num_cas_in_tran++;

	  PROXY_DEBUG_LOG
	    ("Shard IO status. (num_cas_in_tran=%d, shard_id=%d).",
	     shard_io_p->num_cas_in_tran, shard_io_p->shard_id);

	  assert (shard_io_p->cur_num_cas >= shard_io_p->num_cas_in_tran);
	  assert (cas_io_p->is_in_tran == false);
	  assert (cas_io_p->ctx_uid == 0);
	}

      cas_io_p->is_in_tran = true;
      cas_io_p->ctx_cid = ctx_cid;
      cas_io_p->ctx_uid = ctx_uid;

      return cas_io_p;
    }

  do
    {
      if (shard_id >= 0)
	{
	  shard_io_p = &(proxy_Shard_io.ent[shard_id]);
	}
      else
	{
	  /* find any shard */
	  for (i = 0; i < proxy_Shard_io.max_shard; i++)
	    {
	      curr_shard_id = (last_shard_id + i) % proxy_Shard_io.max_shard;
	      shard_io_p = &(proxy_Shard_io.ent[curr_shard_id]);

	      assert (shard_io_p->cur_num_cas >= shard_io_p->num_cas_in_tran);
	      if (shard_io_p->cur_num_cas == shard_io_p->num_cas_in_tran)
		{
		  continue;
		}
	      break;
	    }

	  last_shard_id++;

	  /* not found */
	  if (i >= proxy_Shard_io.max_shard)
	    {
	      PROXY_LOG (PROXY_LOG_MODE_SHARD_DETAIL,
			 "Unable to find avaiable shard. (index:%d, max_shard:%d).",
			 i, proxy_Shard_io.max_shard);
	      goto set_waiter;
	    }
	}

      assert (cas_id <= shard_io_p->max_num_cas);

      /* select any cas */
      for (i = 0; i < shard_io_p->max_num_cas; i++)
	{
	  cas_io_p = &(shard_io_p->ent[i]);

	  /* find idle */
	  if (cas_io_p->is_in_tran || cas_io_p->status != CAS_IO_CONNECTED)
	    {
	      continue;
	    }

	  break;
	}

      /* not found */
#if 0
      if (i >= shard_io_p->cur_num_cas)
#else
      if (i >= shard_io_p->max_num_cas)
#endif
	{
	  PROXY_LOG (PROXY_LOG_MODE_SHARD_DETAIL,
		     "Unable to find avaiable shard. (shard_id:%d, index:%d, max_num_cas:%d).",
		     shard_io_p->shard_id, i, shard_io_p->max_num_cas);

	  cas_io_p = NULL;	/* cannot find idle cas in this shard */
	  if (shard_id >= 0)	/* shard id is specified */
	    {
	      break;
	    }
	  else
	    {
	      /* try again */
	      continue;
	    }
	}

      break;
    }
  while ((++retry_count) < max_retry);

  if (cas_io_p == NULL)
    {
      goto set_waiter;
    }

  shard_io_p->num_cas_in_tran++;

  PROXY_DEBUG_LOG ("Shard status. (num_cas_in_tran=%d, shard_id=%d).",
		   shard_io_p->num_cas_in_tran, shard_io_p->shard_id);

  assert (shard_io_p->cur_num_cas >= shard_io_p->num_cas_in_tran);
  assert (cas_io_p->is_in_tran == false);
  assert (cas_io_p->ctx_cid == PROXY_INVALID_CONTEXT);
  assert (cas_io_p->ctx_uid == 0);
  assert (cas_io_p->fd != INVALID_SOCKET);

  cas_io_p->is_in_tran = true;
  cas_io_p->ctx_cid = ctx_cid;
  cas_io_p->ctx_uid = ctx_uid;

  return cas_io_p;


set_waiter:
  if (shard_id >= 0)
    {
      curr_shard_id = (unsigned int) shard_id;
    }
  else
    {
      curr_shard_id =
	(unsigned int) (last_shard_id - 1) % proxy_Shard_io.max_shard;
    }

  shard_io_p = &(proxy_Shard_io.ent[curr_shard_id]);

  error = proxy_client_add_waiter_by_shard (shard_io_p, ctx_cid, ctx_uid);
  if (error)
    {
      return NULL;
    }

  return (T_CAS_IO *) (SHARD_TEMPORARY_UNAVAILABLE);
}

void
proxy_cas_release_by_ctx (int shard_id, int cas_id, int ctx_cid,
			  unsigned int ctx_uid)
{
  int error;
  T_SHARD_IO *shard_io_p;
  T_CAS_IO *cas_io_p;

  ENTER_FUNC ();

  if (0 > shard_id || shard_id >= proxy_Shard_io.max_shard)
    {
      PROXY_DEBUG_LOG ("Invalid shard id. (shard_id:%d, max_shard:%d).",
		       shard_id, proxy_Shard_io.max_shard);
      EXIT_FUNC ();
      return;
    }
  shard_io_p = &(proxy_Shard_io.ent[shard_id]);

  if (0 > cas_id || cas_id >= shard_io_p->max_num_cas)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Invalid CAS id. (cas_id:%d, max_num_cas:%d).", cas_id,
		 shard_io_p->max_num_cas);
      EXIT_FUNC ();
      return;
    }
  cas_io_p = &(shard_io_p->ent[cas_id]);

  if (cas_io_p->is_in_tran == false || cas_io_p->ctx_cid != ctx_cid
      || cas_io_p->ctx_uid != ctx_uid)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Unable to find CAS entry. "
		 "(context id:%d, context uid:%d). CAS(%S).",
		 ctx_cid, ctx_uid, proxy_str_cas_io (cas_io_p));

      assert (false);

      EXIT_FUNC ();
      return;
    }

  cas_io_p->is_in_tran = false;
  cas_io_p->ctx_cid = PROXY_INVALID_CONTEXT;
  cas_io_p->ctx_uid = 0;

  /* decrease number of cas in transaction */
  shard_io_p->num_cas_in_tran--;

  PROXY_DEBUG_LOG ("Shard status. (num_cas_in_tran=%d, shard_id=%d).",
		   shard_io_p->num_cas_in_tran, shard_io_p->shard_id);

  assert (shard_io_p->num_cas_in_tran >= 0);
  if (shard_io_p->num_cas_in_tran < 0)
    {
      shard_io_p->num_cas_in_tran = 0;
    }

  /* check and wakeup shard/cas waiter */
  proxy_client_check_waiter_and_wakeup (shard_io_p, cas_io_p);

  EXIT_FUNC ();
  return;
}

static int
proxy_client_add_waiter_by_shard (T_SHARD_IO * shard_io_p, int ctx_cid,
				  int ctx_uid)
{
  int error;
  T_WAIT_CONTEXT *waiter_p;

  ENTER_FUNC ();

  assert (shard_io_p);

  waiter_p = proxy_waiter_new (ctx_cid, ctx_uid);
  if (waiter_p == NULL)
    {
      EXIT_FUNC ();
      return -1;
    }

  PROXY_DEBUG_LOG ("Context(context id:%d, context uid:%u) "
		   "is waiting on shard(shard_id:%d).",
		   ctx_cid, ctx_uid, shard_io_p->shard_id);

  error = shard_queue_enqueue (&shard_io_p->waitq, (void *) waiter_p);
  if (error)
    {
      EXIT_FUNC ();
      return -1;
    }

  EXIT_FUNC ();
  return 0;
}

static void
proxy_client_check_waiter_and_wakeup (T_SHARD_IO * shard_io_p,
				      T_CAS_IO * cas_io_p)
{
  int error;
  T_WAIT_CONTEXT *waiter_p = NULL;

  ENTER_FUNC ();

  assert (shard_io_p);
  assert (cas_io_p);

  waiter_p = (T_WAIT_CONTEXT *) shard_queue_dequeue (&shard_io_p->waitq);
  if (waiter_p)
    {
      PROXY_DEBUG_LOG ("Wakeup waiter by shard. (shard_id:%d, cas_id:%d).",
		       cas_io_p->shard_id, cas_io_p->cas_id);
      error =
	proxy_wakeup_context_by_shard (waiter_p, cas_io_p->shard_id,
				       cas_io_p->cas_id);
      if (error)
	{
	  PROXY_LOG (PROXY_LOG_MODE_ERROR,
		     "Failed to wakeup context by shard. "
		     "(error:%d, shard_id:%d, cas_id:%d).",
		     error, cas_io_p->shard_id, cas_io_p->cas_id);
	}

      FREE_MEM (waiter_p);
    }

  EXIT_FUNC ();
  return;
}


static SOCKET
proxy_io_connect_to_broker (void)
{
  SOCKET fd;
  int len;
  struct sockaddr_un unix_addr;
  char *port_name;

  if ((port_name = getenv (PORT_NAME_ENV_STR)) == NULL)
    {
      return (-1);
    }

  /* FOR DEBUG */
  PROXY_LOG (PROXY_LOG_MODE_NOTICE, "Connect to broker. (port_name:[%s]).",
	     port_name);

  if ((fd = socket (AF_UNIX, SOCK_STREAM, 0)) < 0)
    return (-1);

  memset (&unix_addr, 0, sizeof (unix_addr));
  unix_addr.sun_family = AF_UNIX;
  strcpy (unix_addr.sun_path, port_name);
#ifdef  _SOCKADDR_LEN		/* 4.3BSD Reno and later */
  len = sizeof (unix_addr.sun_len) + sizeof (unix_addr.sun_family) +
    strlen (unix_addr.sun_path) + 1;
  unix_addr.sun_len = len;
#else /* vanilla 4.3BSD */
  len = strlen (unix_addr.sun_path) + sizeof (unix_addr.sun_family) + 1;
#endif

  if (connect (fd, (struct sockaddr *) &unix_addr, len) < 0)
    {
      CLOSESOCKET (fd);
      return (-4);
    }

  return fd;
}

static int
proxy_io_register_to_broker (void)
{
  SOCKET fd = INVALID_SOCKET;
  int tmp_proxy_id;
  int len;
  int num_retry = 0;

  PROXY_LOG (PROXY_LOG_MODE_NOTICE, "Register to broker. ");

  do
    {
      sleep (1);
      fd = proxy_io_connect_to_broker ();
    }
  while (fd == INVALID_SOCKET && (num_retry++) < 5);
  if (fd == INVALID_SOCKET)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Failed to connect to broker. (fd:%d).", fd);
      return -1;
    }

  tmp_proxy_id = htonl (proxy_id);
  len = WRITESOCKET (fd, &tmp_proxy_id, sizeof (tmp_proxy_id));
  if (len != sizeof (tmp_proxy_id))
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Failed to register to broker. (fd:%d).", fd);
      return -1;
    }

  broker_conn_fd = fd;
  FD_SET (broker_conn_fd, &allset);
  maxfd = max (maxfd, broker_conn_fd);

  return 0;
}

static int
proxy_io_unixd_lsnr (char *unixd_sock_name)
{
  SOCKET fd;
  int n, len, backlog_size;
  struct sockaddr_un unix_addr;

  if ((fd = socket (AF_UNIX, SOCK_STREAM, 0)) < 0)
    return (-1);

  memset (&unix_addr, 0, sizeof (unix_addr));
  unix_addr.sun_family = AF_UNIX;
  strcpy (unix_addr.sun_path, unixd_sock_name);

#ifdef  _SOCKADDR_LEN		/* 4.3BSD Reno and later */
  len = sizeof (unix_addr.sun_len) + sizeof (unix_addr.sun_family) +
    strlen (unix_addr.sun_path) + 1;
  unix_addr.sun_len = len;
#else /* vanilla 4.3BSD */
  len = strlen (unix_addr.sun_path) + sizeof (unix_addr.sun_family) + 1;
#endif

  /* bind the name to the descriptor */
  if (bind (fd, (struct sockaddr *) &unix_addr, len) < 0)
    {
      CLOSESOCKET (fd);
      return (-2);
    }

  /* SHARD TODO -- modify backlog size to max_client_size or other rule -- tigger */
  backlog_size = 10;
  if (listen (fd, backlog_size) < 0)
    {
      /* tell kernel we're a server */
      CLOSESOCKET (fd);
      return (-3);
    }

  cas_lsnr_fd = fd;
  FD_SET (cas_lsnr_fd, &allset);
  maxfd = max (cas_lsnr_fd, broker_conn_fd);

  return fd;
}

static int
proxy_io_cas_lsnr (void)
{
  SOCKET fd;

  /* FOR DEBUG */
  PROXY_LOG (PROXY_LOG_MODE_NOTICE, "Listen CAS socket. (port name:[%s])",
	     proxy_info_p->port_name);

  fd = proxy_io_unixd_lsnr (proxy_info_p->port_name);
  if (fd <= INVALID_SOCKET)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Failed to listen CAS socket. (fd:%d).", fd);
      return -1;		/* FAIELD */
    }

  return 0;			/* SUCCESS */
}

static SOCKET
proxy_io_unixd_accept (SOCKET lsnr_fd)
{
  socklen_t len;
  int fd;
  struct sockaddr_un unix_addr;

  len = sizeof (unix_addr);
  if ((fd = accept (lsnr_fd, (struct sockaddr *) &unix_addr, &len)) < 0)
    {
      return (-1);
    }

  return (fd);
}

static SOCKET
proxy_io_cas_accept (SOCKET lsnr_fd)
{
  return proxy_io_unixd_accept (lsnr_fd);
}

int
proxy_io_initialize (void)
{
  int error;

  /* clear fds */
  FD_ZERO (&allset);
  FD_ZERO (&wnewset);

  /* register to broker */
  error = proxy_io_register_to_broker ();
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Unable to register to broker. (error:%d).", error);
      return -1;
    }

#if defined(WINDOWS)
  if (proxy_info_p->proxy_port > 0)
    {
      new_port = proxy_info_p->proxy_port + proxy_id;
    }
  else
    {
      new_port = 0;
    }

  /* SHARD TODO : socket listener for windows */

  proxy_info_p->proxy_port = new_port;
#endif

  /* make listener for cas */
  error = proxy_io_cas_lsnr ();
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Failed to initialize CAS socket listener. (error:%d).",
		 error);
      return -1;
    }

  error = proxy_socket_io_initialize ();
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Failed to initialize socket entries. (error:%d).", error);
      return error;
    }

  /* initialize client io */
  error = proxy_client_io_initialize ();
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Failed to initialize client entries. (error:%d).", error);
      return error;
    }

  /* initialize shard/cas io */
  error = proxy_shard_io_initialize ();
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Failed to initialize shard/CAS entries. (error:%d).",
		 error);
      return error;
    }

  return 0;
}

void
proxy_io_destroy (void)
{
  proxy_io_close_all_fd ();
  proxy_socket_io_destroy ();
  proxy_client_io_destroy ();
  proxy_shard_io_destroy ();

  return;
}

int
proxy_io_close_all_fd (void)
{
  int i;
  T_SOCKET_IO *sock_io_p;

  /* close client/cas fd */
  for (i = 0; i < proxy_Socket_io.max_socket; i++)
    {
      sock_io_p = &(proxy_Socket_io.ent[i]);
      if (sock_io_p->fd == INVALID_SOCKET)
	{
	  continue;
	}
      CLOSE_SOCKET (sock_io_p->fd);
    }

  /* close cas listen fd */
  if (cas_lsnr_fd != INVALID_SOCKET)
    {
      CLOSE_SOCKET (cas_lsnr_fd);
    }

  /* close broker connection fd */
  if (broker_conn_fd != INVALID_SOCKET)
    {
      CLOSE_SOCKET (broker_conn_fd);
    }

  return 1;
}

int
proxy_io_process (void)
{
  int error;
  int n, select_ret;
  int cas_fd;
  int i;
  unsigned int num_new_client = 0;

  fd_set eset;
  struct timeval tv;

  T_SOCKET_IO *sock_io_p;

retry_select:
  rset = allset;
  wset = wallset = wnewset;

  tv.tv_sec = 0;
  tv.tv_usec = 1000000 / HZ;

  select_ret = select (maxfd + 1, &rset, &wset, NULL, &tv);
  if (select_ret < 0)
    {
      if (errno != EINTR)
	{
	  perror ("select error");
	  return -1;
	}
      return 0;			/* or -1 */
    }
  else if (select_ret == 0)
    {
      // print_statistics ();
      return 0;			/* or -1 */
    }

  /* new cas */
  if (FD_ISSET (cas_lsnr_fd, &rset))
    {
      cas_fd = proxy_io_cas_accept (cas_lsnr_fd);
      if (cas_fd >= 0)
	{
	  if (proxy_socket_io_add (cas_fd, PROXY_IO_FROM_CAS) == NULL)
	    {
	      PROXY_DEBUG_LOG ("Close socket. (fd:%d). \n", sock_io_p->fd);
	      CLOSE_SOCKET (cas_fd);
	    }
	}
      else
	{
	  PROXY_LOG (PROXY_LOG_MODE_ERROR, "Accept socket failure. (fd:%d).",
		     cas_fd);
	  return 0;		/* or -1 */
	}
    }

  /* new client */
  if (FD_ISSET (broker_conn_fd, &rset))
    {
      error = proxy_socket_io_new_client (broker_conn_fd);
      if (error == 0)
	{
	  num_new_client++;
	  if (num_new_client < MAX_NUM_NEW_CLIENT)
	    {
	      goto retry_select;
	    }
	}
    }

  /* process socket io */
  for (i = 0; i < proxy_Socket_io.max_socket; i++)
    {
      sock_io_p = &(proxy_Socket_io.ent[i]);
      if (sock_io_p->fd <= INVALID_SOCKET)
	{
	  continue;
	}

      if (sock_io_p->fd > INVALID_SOCKET && FD_ISSET (sock_io_p->fd, &wset))
	{
	  proxy_socket_io_write (sock_io_p);
	}
      if (sock_io_p->fd > INVALID_SOCKET && FD_ISSET (sock_io_p->fd, &rset))
	{
	  proxy_socket_io_read (sock_io_p);
	}
    }

  return 0;
}
