/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * util.h - 
 */

#ifndef	_UTIL_H_
#define	_UTIL_H_

#ident "$Id$"

#ifdef CAS
#error include error
#endif

#include <time.h>

#include "cas_common.h"

char *trim (char *str);
int ut_file_lock (char *lock_file);
void ut_file_unlock (char *);
char *ut_timestamp_to_str (time_t ts);
void ut_get_num_request (T_TIMEVAL * cur_tv, int as_num, char *access_file,
			 int *num_req, int check_period);
void ut_get_request_time (T_TIMEVAL * cur_tv, int as_num, char *access_file,
			  time_t *sec_array, int *msec_array, int check_period);
int ut_access_log (int as_index, T_TIMEVAL * start_time, char err_flag,
		   int e_offset);
int ut_kill_process (int pid, char *br_name, int as_index);
void ut_cd_work_dir (void);

#ifdef WIN32
int run_child (char *appl_name);
#endif

#endif /* _UTIL_H_ */
