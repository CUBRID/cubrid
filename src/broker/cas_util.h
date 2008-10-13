/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * cas_util.h -
 */

#ifndef	_CAS_UTIL_H_
#define	_CAS_UTIL_H_

#ident "$Id$"

#ifdef WIN32
#include <winsock2.h>
#include <windows.h>
#endif

extern char *ut_uchar2ipstr (unsigned char *ip_addr);
extern char *ut_trim (char *);
extern void ut_tolower (char *str);
extern void ut_timeval_diff (T_TIMEVAL * start, T_TIMEVAL * end, int *res_sec,
			     int *res_msec);

#endif /* _CAS_UTIL_H_ */
