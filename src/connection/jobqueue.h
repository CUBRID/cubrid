/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * jobqueue.h -
 */

#ifndef _JOBQUEUE_H_
#define _JOBQUEUE_H_

#ident "$Id$"

#include "conn.h"
#include "defs.h"

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

#endif /* _JOBQUEUE_H_ */
