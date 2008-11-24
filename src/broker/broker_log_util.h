/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 *   This program is free software; you can redistribute it and/or modify 
 *   it under the terms of the GNU General Public License as published by 
 *   the Free Software Foundation; version 2 of the License. 
 *
 *  This program is distributed in the hope that it will be useful, 
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of 
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the 
 *  GNU General Public License for more details. 
 *
 *  You should have received a copy of the GNU General Public License 
 *  along with this program; if not, write to the Free Software 
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA 
 *
 */


/*
 * broker_log_util.h -
 */

#ifndef	_BROKER_LOG_UTIL_H_
#define	_BROKER_LOG_UTIL_H_

#ident "$Id$"

#ifdef WIN32
#include <windows.h>
#else
#include <sys/time.h>
#endif

#include "cas_common.h"
#include "log_top_string.h"

extern char *ut_uchar2ipstr (unsigned char *ip_addr);
extern char *ut_trim (char *);
extern void ut_tolower (char *str);
extern void ut_timeval_diff (T_TIMEVAL * start, T_TIMEVAL * end, int *res_sec,
			     int *res_msec);
extern int ut_get_line (FILE * fp, T_STRING * t_str, char **out_str,
			int *lineno);


#endif /* _BROKER_LOG_UTIL_H_ */
