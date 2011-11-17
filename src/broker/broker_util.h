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

#include <time.h>

#include "cas_common.h"
#include "porting.h"

#if !defined(LIBCAS_FOR_JSP)
extern char *trim (char *str);
#if defined (ENABLE_UNUSED_FUNCTION)
extern int ut_file_lock (char *lock_file);
extern void ut_file_unlock (char *);
#endif
extern int ut_access_log (int as_index, struct timeval *start_time,
			  char err_flag, int e_offset);
extern int ut_kill_process (int pid, char *br_name, int as_index);
extern void ut_cd_work_dir (void);

extern int ut_set_keepalive (int sock, int keepalive_time);

#if defined(WINDOWS)
extern int run_child (const char *appl_name);
#endif

extern void as_pid_file_create (char *br_name, int as_index);
extern int as_get_my_as_info (char *br_name, int *as_index);
extern void as_db_err_log_set (char *br_name, int as_index);
#endif

extern int ut_time_string (char *buf);
extern char *ut_get_ipv4_string (char *ip_str, int len,
				 unsigned char *ip_addr);
#endif /* _BROKER_UTIL_H_ */
