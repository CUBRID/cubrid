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
 * util_func.h : miscellaneous utility functions interface
 *
 */

#ifndef _UTIL_FUNC_H_
#define _UTIL_FUNC_H_

#ident "$Id$"

#include <sys/types.h>
#include <math.h>

#define infinity()     (HUGE_VAL)

extern unsigned int hashpjw (const char *);

extern int util_compare_filepath (const char *file1, const char *file2);


typedef void (*SIG_HANDLER) (void);
extern void util_arm_signal_handlers (SIG_HANDLER DB_INT32_handler,
				      SIG_HANDLER quit_handler);
extern void util_disarm_signal_handlers (void);
extern char **util_split_string (const char *str, const char *delim);
extern void util_free_string_array (char **array);
extern time_t util_str_to_time_since_epoch (char *str);

#endif /* _UTIL_FUNC_H_ */
