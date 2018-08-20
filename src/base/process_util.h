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
 * process_util.h - functions for process manipulation
 */

#ifndef _PROCESS_UTIL_H_
#define _PROCESS_UTIL_H_

typedef int (*check_funct_t) (void *);

int
create_child_process (const char *const argv[], int wait_flag, check_funct_t check_func, void *check_arg,
		      const char *stdin_file, char *stdout_file, char *stderr_file, int *exit_status);

#endif /* _PROCESS_UTIL_H_ */
