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

#ifndef _STREAM_TO_XASL_H_
#define _STREAM_TO_XASL_H_

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs only to server or stand-alone modules.
#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

#include "thread_compat.hpp"

// forward definitions
struct func_pred;
struct pred_expr_with_context;
struct xasl_node;
struct xasl_node_header;
struct xasl_unpack_info;

extern int stx_map_stream_to_xasl (THREAD_ENTRY * thread_p, xasl_node ** xasl_tree, bool use_xasl_clone,
				   char *xasl_stream, int xasl_stream_size, xasl_unpack_info ** xasl_unpack_info_ptr);
extern int stx_map_stream_to_filter_pred (THREAD_ENTRY * thread_p, pred_expr_with_context ** pred_expr_tree,
					  char *pred_stream, int pred_stream_size);
extern int stx_map_stream_to_func_pred (THREAD_ENTRY * thread_p, func_pred ** xasl, char *xasl_stream,
					int xasl_stream_size, xasl_unpack_info ** xasl_unpack_info_ptr);
extern int stx_map_stream_to_xasl_node_header (THREAD_ENTRY * thread_p, xasl_node_header * xasl_header_p,
					       char *xasl_stream);

#endif /* _STREAM_TO_XASL_H_ */
