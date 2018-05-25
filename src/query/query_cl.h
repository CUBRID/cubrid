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
 * Query processor main interface
 */

#ifndef _QUERY_CL_H_
#define _QUERY_CL_H_

#include "parse_tree.h"

#if defined (SERVER_MODE)
#error Does not belong to server module
#endif /* defined (SERVER_MODE) */

extern int prepare_query (COMPILE_CONTEXT * context, XASL_STREAM * stream);
extern int execute_query (const XASL_ID * xasl_id, QUERY_ID * query_idp, int var_cnt, const DB_VALUE * varptr,
			  QFILE_LIST_ID ** list_idp, QUERY_FLAG flag, CACHE_TIME * clt_cache_time,
			  CACHE_TIME * srv_cache_time, DB_QUERY_EXECUTION_ENDING_TYPE * query_execution_ending_type);
extern int prepare_and_execute_query (char *stream, int stream_size, QUERY_ID * query_id, int var_cnt,
				      DB_VALUE * varptr, QFILE_LIST_ID ** result, QUERY_FLAG flag);

#endif /* _QUERY_CL_H_ */
