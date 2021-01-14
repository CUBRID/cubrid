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
 * xasl_to_steam.h
 */

#ifndef _XASL_TO_STREAM_H_
#define _XASL_TO_STREAM_H_

#if defined (SERVER_MODE)
#error Does not belong to server module
#endif /* defined (SERVER_MODE) */

// forward definition
struct func_pred;
struct pred_expr_with_context;
struct xasl_node;
struct xasl_stream;

extern int xts_map_xasl_to_stream (const xasl_node * xasl, xasl_stream * stream);
extern int xts_map_filter_pred_to_stream (const pred_expr_with_context * pred, char **stream, int *size);
extern int xts_map_func_pred_to_stream (const func_pred * xasl, char **stream, int *size);

#endif /* !_XASL_TO_STREAM_H_ */
