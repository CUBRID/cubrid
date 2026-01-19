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
 * px_sort.h - parallel sorting module
 */

#ifndef _PARALLEL_SORT_H_
#define _PARALLEL_SORT_H_

#ident "$Id$"

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs to server module
#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

#include "error_manager.h"
#include "thread_compat.hpp"

#define SORT_PX_MERGE_FILES	4
#define SORT_MAX_PARALLEL	PRM_MAX_PARALLELISM
#define SORT_IS_PARALLEL(t)	((t)->px_parallel_num > 1)

// *INDENT-OFF*
#define SORT_EXECUTE_PARALLEL(num, px_sort_param, function)  \
    do {                          \
	for (int i = 0; i < num; i++) {				\
	    parallel_query::callable_task * task =		\
	       new parallel_query::callable_task (sort_param->px_worker_manager, std::		\
	          bind (function, std::placeholders::_1, &px_sort_param[i]));	\
	    sort_param->px_worker_manager->push_task(task);	\
	  }	\
    } while (0)
// *INDENT-ON*

#define SORT_WAIT_PARALLEL(parallel_num, sort_param, px_sort_param) \
    do {                          \
	  pthread_mutex_lock (sort_param->px_mtx); \
      while (1) \
	{ \
	  int done = true;  \
	  for (int i = 0; i < parallel_num; i++)  \
	    {  \
	      if (px_sort_param[i].px_status == PX_PROGRESS)  \
		{  \
		  done = false; \
		  break; \
		} \
	      else if (px_sort_param[i].px_status == PX_ERR_FAILED) \
		{ \
		  error = ER_FAILED; \
		} \
	    } \
	  if (done) \
	    { \
	      break; \
	    } \
	  pthread_cond_wait (sort_param->complete_cond, sort_param->px_mtx); \
	} \
      pthread_mutex_unlock (sort_param->px_mtx); \
      sort_param->px_worker_manager->wait_workers (); \
    } while (0)

enum px_status
{
  PX_ERR_FAILED = -1,
  PX_DONE = 0,
  PX_PROGRESS
};
typedef enum px_status PX_STATUS;

enum parallel_type
{
  PX_SINGLE = 0,
  PX_MAIN_IN_PARALLEL = 1,
  PX_THREAD_IN_PARALLEL
};
typedef enum parallel_type PARALLEL_TYPE;

typedef struct result_run RESULT_RUN;
struct result_run
{
  VFID temp_file;
  int num_pages;
};

typedef struct sort_param SORT_PARAM;

/* start parallel sort */
void sort_listfile_execute (cubthread::entry & thread_ref, SORT_PARAM * sort_param);
int sort_copy_sort_param (THREAD_ENTRY * thread_p, SORT_PARAM * dest_param, SORT_PARAM * src_param, int parallel_num);
int sort_copy_sort_info (THREAD_ENTRY * thread_p, SORT_INFO ** dest_sort_info, SORT_INFO * src_sort_info);
int sort_split_input_temp_file (THREAD_ENTRY * thread_p, SORT_PARAM * dest_param, SORT_PARAM * src_param,
				int parallel_num);
int sort_merge_run_for_parallel (THREAD_ENTRY * thread_p, SORT_PARAM * dest_param, SORT_PARAM * src_param,
				 int parallel_num);
int sort_merge_nruns (THREAD_ENTRY * thread_p, SORT_PARAM * sort_param);
int sort_check_parallelism (THREAD_ENTRY * thread_p, SORT_PARAM * sort_param);
int sort_start_parallelism (THREAD_ENTRY * thread_p, SORT_PARAM * px_sort_param, SORT_PARAM * sort_param);
int sort_end_parallelism (THREAD_ENTRY * thread_p, SORT_PARAM * px_sort_param, SORT_PARAM * sort_param);
void sort_put_result_for_parallel (cubthread::entry & thread_ref, SORT_PARAM * sort_param);
void sort_merge_nruns_parallel (cubthread::entry & thread_ref, SORT_PARAM * sort_param);
void sort_split_last_run (THREAD_ENTRY * thread_p, SORT_PARAM * px_sort_param, SORT_PARAM * sort_param,
			  int parallel_num);
int sort_put_result_from_tmpfile (THREAD_ENTRY * thread_p, SORT_PARAM * sort_param, int start_pagenum);
/* end parallel sort */

#endif /* _PARALLEL_SORT_H_ */
