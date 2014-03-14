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
 * show_scan.h  
 */
#ifndef _SHOW_SCAN_H_
#define _SHOW_SCAN_H_

#ident "$Id$"

#include "thread.h"
#include "dbtype.h"

typedef struct showstmt_array_context SHOWSTMT_ARRAY_CONTEXT;
struct showstmt_array_context
{
  DB_VALUE **tuples;		/* tuples, each tuple is composed by
				   DB_VALUE arrays */
  int num_cols;			/* columns count */
  int num_used;			/* used tuples count */
  int num_total;		/* total allocated tuples count */
};

extern SHOWSTMT_ARRAY_CONTEXT *showstmt_alloc_array_context (THREAD_ENTRY *
							     thread_p,
							     int num_capacity,
							     int num_cols);
extern void showstmt_free_array_context (THREAD_ENTRY * thread_p,
					 SHOWSTMT_ARRAY_CONTEXT * ctx);
extern DB_VALUE *showstmt_alloc_tuple_in_context (THREAD_ENTRY * thread_p,
						  SHOWSTMT_ARRAY_CONTEXT *
						  ctx);

#endif /* _SHOW_SCAN_H_ */
