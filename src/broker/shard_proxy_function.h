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
 * shard_proxy_function.h -
 */

#ifndef	_SHARD_PROXY_FUNCTION_H_
#define	_SHARD_PROXY_FUNCTION_H_

#ident "$Id$"

#include "cas_net_buf.h"
#include "shard_proxy_common.h"

typedef int (*T_PROXY_CLIENT_FUNC) (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p, int argc, char **argv);
typedef int (*T_PROXY_CAS_FUNC) (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p);

extern int proxy_check_cas_error (char *read_msg);
extern int proxy_get_cas_error_code (char *read_msg, T_BROKER_VERSION client_version);
extern int proxy_send_request_to_cas (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p, int func_code);

/* process client request */
extern int fn_proxy_client_end_tran (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p, int argc, char **argv);
extern int fn_proxy_client_prepare (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p, int argc, char **argv);
extern int fn_proxy_client_execute (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p, int argc, char **argv);
extern int fn_proxy_client_set_db_parameter (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p, int argc, char **argv);
extern int fn_proxy_client_get_db_parameter (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p, int argc, char **argv);
extern int fn_proxy_client_close_req_handle (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p, int argc, char **argv);
extern int fn_proxy_client_cursor (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p, int argc, char **argv);
extern int fn_proxy_client_fetch (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p, int argc, char **argv);
extern int fn_proxy_client_schema_info (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p, int argc, char **argv);
extern int fn_proxy_client_check_cas (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p, int argc, char **argv);
extern int fn_proxy_client_con_close (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p, int argc, char **argv);
extern int fn_proxy_client_get_db_version (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p, int argc, char **argv);
extern int fn_proxy_client_cursor_close (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p, int argc, char **argv);
extern int fn_proxy_client_execute_array (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p, int argc, char **argv);
extern int fn_proxy_get_shard_info (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p, int argc, char **argv);
extern int fn_proxy_client_prepare_and_execute (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p, int argc,
						char **argv);
extern int fn_proxy_client_not_supported (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p, int argc, char **argv);

/* process cas response */
extern int fn_proxy_cas_end_tran (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p);
extern int fn_proxy_cas_prepare (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p);
extern int fn_proxy_cas_execute (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p);
extern int fn_proxy_cas_execute_array (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p);
extern int fn_proxy_cas_fetch (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p);
extern int fn_proxy_cas_schema_info (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p);
extern int fn_proxy_cas_prepare_and_execute (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p);
extern int fn_proxy_cas_check_cas (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p);

extern int fn_proxy_cas_relay_only (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p);

extern int fn_proxy_client_conn_error (T_PROXY_CONTEXT * ctx_p);
extern int fn_proxy_cas_conn_error (T_PROXY_CONTEXT * ctx_p);

extern int proxy_send_response_to_client (T_PROXY_CONTEXT * ctx_p, T_PROXY_EVENT * event_p);
extern int proxy_send_response_to_client_with_new_event (T_PROXY_CONTEXT * ctx_p, unsigned int type, int from,
							 T_PROXY_EVENT_FUNC resp_func);


#endif /* _SHARD_PROXY_FUNCTION_H_ */
