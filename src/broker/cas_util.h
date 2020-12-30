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
 * cas_util.h -
 */

#ifndef	_CAS_UTIL_H_
#define	_CAS_UTIL_H_

#ident "$Id$"

#if defined(WINDOWS)
#include <winsock2.h>
#include <windows.h>
#endif

extern char *ut_uchar2ipstr (unsigned char *ip_addr);
extern char *ut_trim (char *);
extern void ut_tolower (char *str);
extern void ut_timeval_diff (struct timeval *start, struct timeval *end, int *res_sec, int *res_msec);
extern int ut_check_timeout (struct timeval *start_time, struct timeval *end_time, int timeout_msec, int *res_sec,
			     int *res_msec);

#endif /* _CAS_UTIL_H_ */
