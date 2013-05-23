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
 * shard_proxy_handler.h - 
 *              
 */

#ifndef _SHARD_PROXY_HANDLER_H_
#define _SHARD_PROXY_HANDLER_H_

#ident "$Id$"


#include "porting.h"
#include "shard_proxy_common.h"

typedef int (*T_PROXY_EVENT_FUNC) (char *driver_info, char **buffer);
typedef int (*T_PROXY_EVENT_FUNC_EX) (char *driver_info, char **buffer,
				      void *argv);

extern T_WAIT_CONTEXT *proxy_waiter_new (int ctx_cid, unsigned int ctx_uid,
					 int timeout);
extern void proxy_waiter_free (T_WAIT_CONTEXT * waiter);
extern void proxy_waiter_timeout (T_SHARD_QUEUE * waitq, int *counter,
				  int now);
extern int proxy_waiter_comp_fn (const void *arg1, const void *arg2);


extern bool proxy_handler_is_cas_in_tran (int shard_id, int cas_id);
extern void proxy_context_set_error (T_PROXY_CONTEXT * ctx_p, int error_ind,
				     int error_code);
extern void proxy_context_set_error_with_msg (T_PROXY_CONTEXT * ctx_p,
					      int error_ind, int error_code,
					      const char *error_msg);
extern void proxy_context_clear_error (T_PROXY_CONTEXT * ctx_p);
extern int proxy_context_send_error (T_PROXY_CONTEXT * ctx_p);

extern void proxy_context_set_in_tran (T_PROXY_CONTEXT * ctx_p,
				       int shard_id, int cas_id);
extern void proxy_context_set_out_tran (T_PROXY_CONTEXT * ctx_p);

extern T_PROXY_CONTEXT *proxy_context_new (void);
extern void proxy_context_free (T_PROXY_CONTEXT * ctx_p);
#if defined (ENABLE_UNUSED_FUNCTION)
extern void proxy_context_free_by_cid (int cid, unsigned int uid);
#endif /* ENABLE_UNUSED_FUNCTION */
extern T_PROXY_CONTEXT *proxy_context_find (int cid, unsigned int uid);
extern T_PROXY_CONTEXT *proxy_context_find_by_socket_client_io (T_SOCKET_IO *
								sock_io_p);
extern T_CONTEXT_STMT *proxy_context_find_stmt (T_PROXY_CONTEXT * ctx_p,
						int stmt_h_id);
extern T_CONTEXT_STMT *proxy_context_add_stmt (T_PROXY_CONTEXT * ctx_p,
					       T_SHARD_STMT * stmt_p);
extern void proxy_context_free_stmt (T_PROXY_CONTEXT * ctx_p);
extern void proxy_context_timeout (T_PROXY_CONTEXT * ctx_p);

#if defined (PROXY_VERBOSE_DEBUG)
extern void proxy_context_dump_stmt (FILE * fp, T_PROXY_CONTEXT * ctx_p);
extern void proxy_context_dump_title (FILE * fp);
extern void proxy_context_dump (FILE * fp, T_PROXY_CONTEXT * ctx_p);
extern void proxy_context_dump_all (FILE * fp);
extern void proxy_context_print (bool print_all);
#endif /* PROXY_VERBOSE_DEBUG */
extern char *proxy_str_context (T_PROXY_CONTEXT * ctx_p);


extern void proxy_handler_destroy (void);
extern int proxy_handler_initialize (void);
extern void proxy_handler_process (void);

extern int proxy_wakeup_context_by_shard (T_WAIT_CONTEXT * waiter_p,
					  int shard_id, int cas_id);
extern int proxy_wakeup_context_by_statement (T_WAIT_CONTEXT * waiter_p);

extern T_PROXY_EVENT *proxy_event_new (unsigned int type, int from_cas);
extern T_PROXY_EVENT *proxy_event_dup (T_PROXY_EVENT * event_p);
extern
  T_PROXY_EVENT *proxy_event_new_with_req (char *driver_info,
					   unsigned int type, int from,
					   T_PROXY_EVENT_FUNC req_func);
extern T_PROXY_EVENT *proxy_event_new_with_rsp (char *driver_info,
						unsigned int type, int from,
						T_PROXY_EVENT_FUNC resp_func);
extern T_PROXY_EVENT *proxy_event_new_with_rsp_ex (char *driver_info,
						   unsigned int type,
						   int from,
						   T_PROXY_EVENT_FUNC_EX
						   resp_func, void *argv);
extern T_PROXY_EVENT *proxy_event_new_with_error (char *driver_info,
						  unsigned int type, int from,
						  int (*err_func) (char
								   *driver_info,
								   char
								   **buffer,
								   int
								   error_ind,
								   int
								   error_code,
								   char
								   *error_msg,
								   char
								   is_in_tran),
						  int error_ind,
						  int error_code,
						  char *error_msg,
						  char is_in_tran);

extern int proxy_event_alloc_buffer (T_PROXY_EVENT * event_p,
				     unsigned int size);
extern int proxy_event_realloc_buffer (T_PROXY_EVENT * event_p,
				       unsigned int size);
extern void proxy_event_set_buffer (T_PROXY_EVENT * event_p, char *data,
				    unsigned int size);
extern void proxy_event_set_type_from (T_PROXY_EVENT * event_p,
				       unsigned int type, int from_cas);
extern void proxy_event_set_context (T_PROXY_EVENT * event_p, int cid,
				     unsigned int uid);
extern void proxy_event_set_shard (T_PROXY_EVENT * event_p, int shard_id,
				   int cas_id);
extern bool proxy_event_io_read_complete (T_PROXY_EVENT * event_p);
#if defined (ENABLE_UNUSED_FUNCTION)
extern bool proxy_event_io_write_complete (T_PROXY_EVENT * event_p);
#endif /* ENABLE_UNUSED_FUNCTION */
extern void proxy_event_free (T_PROXY_EVENT * event_p);
extern char *proxy_str_event (T_PROXY_EVENT * event_p);

extern void proxy_timer_process (void);

extern char *shard_str_sqls (char *sql);
#endif /* _SHARD_PROXY_HANDLER_H_ */
