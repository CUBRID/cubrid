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
 * shard_proxy_function.h -
 */

#ifndef	_SHARD_PROXY_FUNCTION_H_
#define	_SHARD_PROXY_FUNCTION_H_

#ident "$Id$"

#include "cas_net_buf.h"
#include "shard_proxy_common.h"

typedef int (*T_PROXY_CLIENT_FUNC) (T_PROXY_CONTEXT * ctx_p,
				    T_PROXY_EVENT * event_p, int argc,
				    char **argv);
typedef int (*T_PROXY_CAS_FUNC) (T_PROXY_CONTEXT * ctx_p,
				 T_PROXY_EVENT * event_p);

extern int proxy_check_cas_error (char *read_msg);
extern int proxy_get_cas_error_code (char *read_msg,
				     T_BROKER_VERSION client_version);


/* process client request */
extern int fn_proxy_client_end_tran (T_PROXY_CONTEXT * ctx_p,
				     T_PROXY_EVENT * event_p, int argc,
				     char **argv);
extern int fn_proxy_client_prepare (T_PROXY_CONTEXT * ctx_p,
				    T_PROXY_EVENT * event_p, int argc,
				    char **argv);
extern int fn_proxy_client_execute (T_PROXY_CONTEXT * ctx_p,
				    T_PROXY_EVENT * event_p, int argc,
				    char **argv);
extern int fn_proxy_client_close_req_handle (T_PROXY_CONTEXT * ctx_p,
					     T_PROXY_EVENT * event_p,
					     int argc, char **argv);
extern int fn_proxy_client_cursor (T_PROXY_CONTEXT * ctx_p,
				   T_PROXY_EVENT * event_p, int argc,
				   char **argv);
extern int fn_proxy_client_fetch (T_PROXY_CONTEXT * ctx_p,
				  T_PROXY_EVENT * event_p, int argc,
				  char **argv);
extern int fn_proxy_client_schema_info (T_PROXY_CONTEXT * ctx_p,
					T_PROXY_EVENT * event_p, int argc,
					char **argv);
extern int fn_proxy_client_check_cas (T_PROXY_CONTEXT * ctx_p,
				      T_PROXY_EVENT * event_p, int argc,
				      char **argv);
extern int fn_proxy_client_con_close (T_PROXY_CONTEXT * ctx_p,
				      T_PROXY_EVENT * event_p, int argc,
				      char **argv);
extern int fn_proxy_client_get_db_version (T_PROXY_CONTEXT * ctx_p,
					   T_PROXY_EVENT * event_p, int argc,
					   char **argv);
extern int fn_proxy_client_cursor_close (T_PROXY_CONTEXT * ctx_p,
					 T_PROXY_EVENT * event_p, int argc,
					 char **argv);
extern int fn_proxy_client_execute_array (T_PROXY_CONTEXT * ctx_p,
					  T_PROXY_EVENT * event_p, int argc,
					  char **argv);
extern int fn_proxy_get_shard_info (T_PROXY_CONTEXT * ctx_p,
				    T_PROXY_EVENT * event_p, int argc,
				    char **argv);
extern int fn_proxy_client_not_supported (T_PROXY_CONTEXT * ctx_p,
					  T_PROXY_EVENT * event_p, int argc,
					  char **argv);

/* process cas response */
extern int fn_proxy_cas_end_tran (T_PROXY_CONTEXT * ctx_p,
				  T_PROXY_EVENT * event_p);
extern int fn_proxy_cas_prepare (T_PROXY_CONTEXT * ctx_p,
				 T_PROXY_EVENT * event_p);
extern int fn_proxy_cas_execute (T_PROXY_CONTEXT * ctx_p,
				 T_PROXY_EVENT * event_p);
extern int fn_proxy_cas_execute_array (T_PROXY_CONTEXT * ctx_p,
				       T_PROXY_EVENT * event_p);
extern int fn_proxy_cas_fetch (T_PROXY_CONTEXT * ctx_p,
			       T_PROXY_EVENT * event_p);
extern int fn_proxy_cas_schema_info (T_PROXY_CONTEXT * ctx_p,
				     T_PROXY_EVENT * event_p);

extern int fn_proxy_cas_relay_only (T_PROXY_CONTEXT * ctx_p,
				    T_PROXY_EVENT * event_p);

extern int fn_proxy_client_conn_error (T_PROXY_CONTEXT * ctx_p);
extern int fn_proxy_cas_conn_error (T_PROXY_CONTEXT * ctx_p);

extern int proxy_send_response_to_client (T_PROXY_CONTEXT * ctx_p,
					  T_PROXY_EVENT * event_p);
extern int proxy_send_response_to_client_with_new_event (T_PROXY_CONTEXT *
							 ctx_p,
							 unsigned int type,
							 int from,
							 T_PROXY_EVENT_FUNC
							 resp_func);


#endif /* _SHARD_PROXY_FUNCTION_H_ */
