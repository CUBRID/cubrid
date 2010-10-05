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

#define CAS_LOG_MSG_INDEX 19
#define CAS_RUN_NEW_LINE_CHAR	1

#define GET_MSG_START_PTR(MSG_P, LINEBUF)               \
        do {                                            \
          char *tmp_ptr;                                \
          tmp_ptr = LINEBUF + CAS_LOG_MSG_INDEX;        \
          tmp_ptr = strchr(tmp_ptr, ' ');               \
          if (tmp_ptr == NULL)                          \
            MSG_P = (char *) "";                        \
          else                                          \
            MSG_P = tmp_ptr + 1;                        \
        } while (0)

extern char *ut_trim (char *);
extern void ut_tolower (char *str);
extern int ut_get_line (FILE * fp, T_STRING * t_str, char **out_str,
			int *lineno);


#endif /* _BROKER_LOG_UTIL_H_ */
