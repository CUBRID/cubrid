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
 * shard_proxy_common.h - 
 *              
 */

#ifndef _SHARD_PROXY_COMMON_H_
#define _SHARD_PROXY_COMMON_H_

#ident "$Id$"

#include "porting.h"
#include "broker_shm.h"
#include "shard_proxy_queue.h"
#include "shard_proxy_log.h"
#include "memory_hash.h"
#include "shard_statement.h"

#if defined(WINDOWS)
#define MAX_FD 1024
#else /* WINDOWS */
#if defined(LINUX)
#define PROXY_MAX_CLIENT 	(100000)
#else /* LINUX */
#undef MAX_FD
#define MAX_FD FD_SETSIZE
#endif /* !LINUX */
#endif /* !WINDOWS */

/*
 * broker receive
 * cas accept
 * error log
 * access log
 * stdin / stdout / stderr
 * RESERVED
 * */
#if defined(LINUX)
#define RESERVED_FD 	(256)
#else /* LINUX */
#define RESERVED_FD 	(128)
#endif /* !LINUX */

#define PROXY_INVALID_ID			(-1)
#define PROXY_INVALID_SHARD			(PROXY_INVALID_ID)
#define PROXY_INVALID_CAS		(PROXY_INVALID_ID)
#define PROXY_INVALID_CLIENT		(PROXY_INVALID_ID)
#define PROXY_INVALID_FUNC_CODE		(-1)
#define PROXY_INVALID_CONTEXT		(-1)

#define PROXY_EVENT_NULL_BUFFER 	(NULL)
#define PROXY_EVENT_FROM_CAS 		(true)
#define PROXY_EVENT_FROM_CLIENT 	(false)

typedef struct t_io_buffer T_IO_BUFFER;
struct t_io_buffer
{
  int length;

  int offset;
  char *data;
};

enum
{
  PROXY_EVENT_IO_READ = 0,
  PROXY_EVENT_IO_WRITE = 1,
  PROXY_EVENT_CLIENT_REQUEST = 2,
  PROXY_EVENT_CAS_RESPONSE = 3,
  PROXY_EVENT_CLIENT_CONN_ERROR = 4,
  PROXY_EVENT_CAS_CONN_ERROR = 5,
  PROXY_EVENT_CLIENT_WAKEUP_BY_SHARD = 6,
  PROXY_EVENT_CLIENT_WAKEUP_BY_STATEMENT = 7
} E_PROXY_EVENT;

typedef struct t_proxy_event T_PROXY_EVENT;
struct t_proxy_event
{
  unsigned short type;
  bool from_cas;

  int cid;
  unsigned int uid;

  /* for EVENT_WAKEUP_BY_SHARD */
  int shard_id;
  int cas_id;

  T_IO_BUFFER buffer;
};

typedef struct t_wait_context T_WAIT_CONTEXT;
struct t_wait_context
{
  int ctx_cid;
  int ctx_uid;
  time_t expire_time;
};

enum
{
  SOCK_IO_IDLE = 0,
  SOCK_IO_REG_WAIT = 1,
  SOCK_IO_ESTABLISHED = 2,
  SOCK_IO_CLOSE_WAIT = 3
} T_SOCKET_IO_STATUS;

typedef struct t_socket_io T_SOCKET_IO;
struct t_socket_io
{
  SOCKET fd;
  int status;
  int ip_addr;

  bool from_cas;
  union
  {
    int client_id;
    struct
    {
      int shard_id;
      int cas_id;
    } shard;
  } id;

  T_PROXY_EVENT *read_event;
  T_PROXY_EVENT *write_event;
};

typedef struct t_socket_io_global T_SOCKET_IO_GLOBAL;
struct t_socket_io_global
{
  int max_socket;
  int cur_socket;

  T_SOCKET_IO *ent;
};


typedef struct t_client_io T_CLIENT_IO;
struct t_client_io
{
  int client_id;
  bool is_busy;

  SOCKET fd;

  int ctx_cid;
  unsigned int ctx_uid;

  char driver_info[SRV_CON_CLIENT_INFO_SIZE];

  /* send queue ? */
};

typedef struct t_client_io_global T_CLIENT_IO_GLOBAL;
struct t_client_io_global
{
  int max_client;
  int cur_client;
  int max_context;

  T_SHARD_CQUEUE freeq;
  T_CLIENT_IO *ent;
};

enum
{
  CAS_IO_NOT_CONNECTED = 0,
  CAS_IO_CONNECTED = 1,
  CAS_IO_CLOSE_WAIT = 2
} T_CAS_IO_STATUS;

typedef struct t_cas_io T_CAS_IO;
struct t_cas_io
{
  int status;

  int cas_id;
  int shard_id;
  bool is_in_tran;

  int ctx_cid;
  unsigned int ctx_uid;

  SOCKET fd;

  /* send queue ? */
};

typedef struct t_shard_io T_SHARD_IO;
struct t_shard_io
{
  int shard_id;

  int max_num_cas;
  int cur_num_cas;

  int num_cas_in_tran;

  T_SHARD_QUEUE waitq;
  T_CAS_IO *ent;
};

typedef struct t_shard_io_global T_SHARD_IO_GLOBAL;
struct t_shard_io_global
{
  int max_shard;

  T_SHARD_IO *ent;
};

typedef struct t_context_stmt T_CONTEXT_STMT;
struct t_context_stmt
{
  T_CONTEXT_STMT *next;
  int stmt_h_id;
};

typedef struct t_proxy_context T_PROXY_CONTEXT;
struct t_proxy_context
{
  int cid;			/* context id */
  unsigned int uid;		/* unique id */

  /* flags */
  bool is_busy;
  bool is_in_tran;
  bool is_prepare_for_execute;
  bool free_on_end_tran;
  bool free_on_client_io_write;
  bool free_context;
  bool is_client_in_tran;	/* it is only for faking cas status 
				 * when check_cas is requested. 
				 * after completion dummy prepare,
				 * context status will be out_tran.
				 * In this case we should fake transaction 
				 * status to in_tran. 
				 */
  bool is_cas_in_tran;		/* cas transaction status */
  bool waiting_dummy_prepare;
  bool dont_free_statement;

  /* context */
  T_PROXY_EVENT *waiting_event;
  int func_code;
  int stmt_h_id;
  int stmt_hint_type;
  T_SHARD_STMT *prepared_stmt;
  int wait_timeout;

  /* connection info */
  bool is_connected;
  char database_user[SRV_CON_DBUSER_SIZE];
  char database_passwd[SRV_CON_DBPASSWD_SIZE];

  /* statement list */
  T_CONTEXT_STMT *stmt_list;

  /* client, shard/cas */
  int client_id;
  int shard_id;
  int cas_id;

  /* error */
  int error_ind;
  int error_code;
  char error_msg[256];
};

typedef struct t_proxy_context_global T_PROXY_CONTEXT_GLOBAL;
struct t_proxy_context_global
{
  int size;

  T_SHARD_CQUEUE freeq;
  T_PROXY_CONTEXT *ent;
};

typedef struct t_proxy_handler T_PROXY_HANDLER;
struct t_proxy_handler
{
  short index;			/* further use */
  unsigned char state;		/* further use */
  bool shutdown;		/* further use */

  pthread_t thread_id;		/* further use */

  T_SHARD_QUEUE cas_rcv_q;
  T_SHARD_QUEUE cli_ret_q;
  T_SHARD_QUEUE cli_rcv_q;
};

#endif /* _SHARD_PROXY_COMMON_H_ */
