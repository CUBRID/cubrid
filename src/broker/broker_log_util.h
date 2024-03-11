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

#define ut_trim  trim
extern void ut_tolower (char *str);
extern int ut_get_line (FILE * fp, T_STRING * t_str, char **out_str, int *lineno);
extern int is_cas_log (char *str);
extern char *get_msg_start_ptr (char *linebuf);

extern int str_to_log_date_format (char *str, char *date_format_str);
extern char *ut_get_execute_type (char *msg_p, int *prepare_flag, int *execute_flag);
extern int ut_check_log_valid_time (const char *log_date, const char *from_date, const char *to_date);
extern double ut_diff_time (struct timeval *begin, struct timeval *end);

#endif /* _BROKER_LOG_UTIL_H_ */
