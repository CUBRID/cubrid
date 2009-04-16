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
 * broker_util.h - 
 */

#ifndef	_BROKER_UTIL_H_
#define	_BROKER_UTIL_H_

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
int ut_access_log (int as_index, T_TIMEVAL * start_time, char err_flag,
		   int e_offset);
int ut_kill_process (int pid, char *br_name, int as_index);
void ut_cd_work_dir (void);

#ifdef WIN32
int run_child (char *appl_name);
#endif

void as_pid_file_create (char *br_name, int as_index);
int as_get_my_as_info (char *br_name, int *as_index);
void as_db_err_log_set (char *br_name, int as_index);

#endif /* _BROKER_UTIL_H_ */
