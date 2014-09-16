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
 * job_queue.h -
 */

#ifndef _JOB_QUEUE_H_
#define _JOB_QUEUE_H_

#ident "$Id$"

#include "connection_sr.h"
#include "connection_defs.h"

typedef struct css_job_entry CSS_JOB_ENTRY;
struct css_job_entry
{
  int jobq_index;		/* job queue index */
  CSS_CONN_ENTRY *conn_entry;	/* conn entry from which we read request */
  CSS_THREAD_FN func;		/* request handling function */
  CSS_THREAD_ARG arg;		/* handling function argument */
  CSS_JOB_ENTRY *next;
};

extern CSS_JOB_ENTRY *css_make_job_entry (CSS_CONN_ENTRY * conn,
					  CSS_THREAD_FN func,
					  CSS_THREAD_ARG arg, int index);
extern void css_free_job_entry (CSS_JOB_ENTRY * p);
extern void css_init_job_queue (void);
extern void css_final_job_queue (void);
extern void css_add_to_job_queue (CSS_JOB_ENTRY * job);
extern CSS_JOB_ENTRY *css_get_new_job (void);
extern void css_incr_job_queue_counter (int jobq_index, CSS_THREAD_FN func);
extern void css_decr_job_queue_counter (int jobq_index, CSS_THREAD_FN func);

extern int css_job_queues_start_scan (THREAD_ENTRY * thread_p,
				      int show_type,
				      DB_VALUE ** arg_values,
				      int arg_cnt, void **ptr);

#if !defined(SERVER_MODE)
#define css_job_queues_start_scan NULL
#endif /* !SERVER_MODE */

#endif /* _JOB_QUEUE_H_ */
