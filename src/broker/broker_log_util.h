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
 * broker_log_util.h -
 */

#ifndef	_BROKER_LOG_UTIL_H_
#define	_BROKER_LOG_UTIL_H_

#ident "$Id$"

#if defined(WINDOWS)
#include <windows.h>
#else
#include <sys/time.h>
#endif

#include "cas_common.h"
#include "log_top_string.h"

#define CAS_LOG_BEGIN_WITH_YEAR 1
#define CAS_LOG_BEGIN_WITH_MONTH 2

#define CAS_LOG_MSG_INDEX 22
#define CAS_RUN_NEW_LINE_CHAR	1

#define DATE_STR_LEN    21

#define GET_CUR_DATE_STR(BUF, LINEBUF)  \
        do  {                           \
          strncpy(BUF, LINEBUF, DATE_STR_LEN);  \
          BUF[DATE_STR_LEN] = '\0';             \
        } while (0)

extern char *ut_trim (char *);
extern void ut_tolower (char *str);
extern int ut_get_line (FILE * fp, T_STRING * t_str, char **out_str,
			int *lineno);
extern int is_cas_log (char *str);
extern char *get_msg_start_ptr (char *linebuf);

extern int str_to_log_date_format (char *str, char *date_format_str);
extern char *ut_get_execute_type (char *msg_p, int *prepare_flag,
				  int *execute_flag);
extern int ut_check_log_valid_time (const char *log_date,
				    const char *from_date,
				    const char *to_date);
extern double ut_diff_time (struct timeval *begin, struct timeval *end);

#endif /* _BROKER_LOG_UTIL_H_ */
