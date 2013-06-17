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
 * event_log.h - event log module (server)
 */

#ifndef _EVENT_LOG_H_
#define _EVENT_LOG_H_

#ident "$Id$"

#include <stdio.h>

#include "thread.h"
#include "query_list.h"

#define EVENT_EMPTY_QUERY "***EMPTY***"

extern void event_log_init (const char *db_name);
extern void event_log_final (void);
extern FILE *event_log_start (THREAD_ENTRY * thread_p,
			      const char *event_name);
extern void event_log_end (THREAD_ENTRY * thread_p);
extern void event_log_print_client_info (int tran_index, int indent);
extern void event_log_sql_string (THREAD_ENTRY * thread_p, FILE * log_fp,
				  XASL_ID * xasl_id, int indent);
extern void event_log_bind_values (FILE * log_fp, int tran_index,
				   int bind_index);
#endif /* _EVENT_LOG_H_ */
