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
 * Query processor main interface
 */

#ifndef _QUERY_CL_H_
#define _QUERY_CL_H_

#include "parse_tree.h"

#if defined (SERVER_MODE)
#error Does not belong to server module
#endif /* defined (SERVER_MODE) */

// forward definition
struct compile_context;
struct xasl_stream;

extern int prepare_query (compile_context * context, xasl_stream * stream);
extern int execute_query (const XASL_ID * xasl_id, QUERY_ID * query_idp, int var_cnt, const DB_VALUE * varptr,
			  QFILE_LIST_ID ** list_idp, QUERY_FLAG flag, CACHE_TIME * clt_cache_time,
			  CACHE_TIME * srv_cache_time);
extern int prepare_and_execute_query (char *stream, int stream_size, QUERY_ID * query_id, int var_cnt,
				      DB_VALUE * varptr, QFILE_LIST_ID ** result, QUERY_FLAG flag);

#endif /* _QUERY_CL_H_ */
