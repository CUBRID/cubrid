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
 * cas_common_function.h 
 */

#ifndef	_CAS_COMMON_FUNCTION_H_
#define	_CAS_COMMON_FUNCTION_H_

#ident "$Id$"

#include "cas_common_vars.h"
#include "intl_support.h"

typedef enum
{
  FN_KEEP_CONN = 0,
  FN_CLOSE_CONN = -1,
  FN_KEEP_SESS = -2,
  FN_GRACEFUL_DOWN = -3
} FN_RETURN;

/* Server function pointer type */
typedef FN_RETURN (*T_SERVER_FUNC) (SOCKET, int, void **, T_NET_BUF *, T_REQ_INFO *);

void cas_common_bind_value_print (char type, void *net_value, bool slow_log, INTL_CODESET charset);
void cas_common_bind_value_log (struct timeval *log_time, int start, int argc, void **argv, int param_size,
				char *param_mode, unsigned int query_seq_num, bool slow_log, INTL_CODESET charset);

FN_RETURN fn_not_supported (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info);
FN_RETURN fn_deprecated (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info);

#endif /* _CAS_COMMON_FUNCTION_H_ */
