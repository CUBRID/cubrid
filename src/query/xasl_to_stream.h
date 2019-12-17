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
