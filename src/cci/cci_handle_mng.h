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
 * cci_handle_mng.h -
 */

#ifndef	_CCI_HANDLE_MNG_H_
#define	_CCI_HANDLE_MNG_H_

#ident "$Id$"

#ifdef CAS
#error include error
#endif

/************************************************************************
 * IMPORTED SYSTEM HEADER FILES						*
 ************************************************************************/

/************************************************************************
 * OTHER IMPORTED HEADER FILES						*
 ************************************************************************/

#include "cas_cci.h"
#include "cas_protocol.h"

/************************************************************************
 * PUBLIC DEFINITIONS							*
 ************************************************************************/

#define QUERY_RESULT_FREE(REQ_HANDLE)		\
	do {					\
	  qe_query_result_free((REQ_HANDLE)->num_query_res, (REQ_HANDLE)->qr); \
	  (REQ_HANDLE)->num_query_res = 0;	\
	  (REQ_HANDLE)->qr = NULL;		\
	} while (0)

/************************************************************************
 * PUBLIC TYPE DEFINITIONS						*
 ************************************************************************/

typedef enum
{
  CCI_CON_STATUS_OUT_TRAN = 0,
  CCI_CON_STATUS_IN_TRAN = 1
} T_CCI_CON_STATUS;

typedef enum
{
  CCI_TRAN_STATUS_START = 0,
  CCI_TRAN_STATUS_RUNNING = 1
} T_CCI_TRAN_STATUS;

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
} T_TUPLE_VALUE;

typedef struct
{
  T_CCI_U_TYPE u_type;
  int size;			/* bind_param : value size
				   bind_param_array : a_type of value */
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
  char prepare_flag;
  char execute_flag;
  char handle_type;
  char updatable_flag;
  char *sql_text;
  int max_row;
  int server_handle_id;
  int num_tuple;
  T_CCI_CUBRID_STMT stmt_type;
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
  int valid;
} T_REQ_HANDLE;

typedef struct
{
  T_REQ_HANDLE *req_handle;
  SOCKET sock_fd;
  char flag;
  int max_col_size;
  T_CCI_ERROR err_buf;
  int *ref_count_ptr;
  int ret_code;
  void *con_handle;		/* for thread processing CAS_ER_STMT_POOLING */
} T_EXEC_THR_ARG;

typedef struct
{
  char is_first;
  char con_status;
  char tran_status;
  unsigned char ip_addr[4];
  int port;
  char *db_name;
  char *db_user;
  char *db_passwd;
  SOCKET sock_fd;
  int ref_count;
  int default_isolation_level;
  int max_req_handle;
  T_EXEC_THR_ARG thr_arg;
  T_REQ_HANDLE **req_handle_table;
  int req_handle_count;
  int cas_pid;
  char broker_info[BROKER_INFO_SIZE];
} T_CON_HANDLE;

/************************************************************************
 * PUBLIC FUNCTION PROTOTYPES						*
 ************************************************************************/

extern void hm_con_handle_table_init (void);
extern int hm_con_handle_alloc (char *ip_str,
				int port,
				char *db_name,
				char *db_user, char *db_passwd);
extern int hm_req_handle_alloc (int con_id, T_REQ_HANDLE **);
extern void hm_req_handle_free (T_CON_HANDLE * con_handle,
				int req_h_id, T_REQ_HANDLE * req_handle);
extern void hm_req_handle_free_all (T_CON_HANDLE * con_handle);
extern int hm_con_handle_free (int con_id);

extern T_CON_HANDLE *hm_find_con_handle (int con_handle_id);
extern T_REQ_HANDLE *hm_find_req_handle (int req_handle_id, T_CON_HANDLE **);
extern void hm_req_handle_fetch_buf_free (T_REQ_HANDLE * req_handle);
extern int hm_conv_value_buf_alloc (T_VALUE_BUF * val_buf, int size);

extern void req_handle_col_info_free (T_REQ_HANDLE * req_handle);
extern void hm_conv_value_buf_clear (T_VALUE_BUF * val_buf);
extern void req_handle_content_free (T_REQ_HANDLE * req_handle, int reuse);
extern void hm_invalidate_all_req_handle (T_CON_HANDLE * con_handle);

/************************************************************************
 * PUBLIC VARIABLES							*
 ************************************************************************/

#endif /* _CCI_HANDLE_MNG_H_ */
