/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * log_util.h -
 */

#ifndef	_LOG_UTIL_H_
#define	_LOG_UTIL_H_

#ident "$Id$"

#ifdef WIN32
#include <windows.h>
#else
#include <sys/time.h>
#endif

#include "cas_common.h"
#include "t_string.h"

extern char *ut_uchar2ipstr (unsigned char *ip_addr);
extern char *ut_trim (char *);
extern void ut_tolower (char *str);
extern void ut_timeval_diff (T_TIMEVAL * start, T_TIMEVAL * end, int *res_sec,
			     int *res_msec);
extern int ut_get_line (FILE * fp, T_STRING * t_str, char **out_str,
			int *lineno);


#endif /* _LOG_UTIL_H_ */
