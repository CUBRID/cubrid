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
 * condition_handler.h : condition handling interfaces
 *
 */

#ifndef _CONDITION_HANDLER_H_
#define _CONDITION_HANDLER_H_

#ident "$Id$"

#include <stdio.h>
#include <stdarg.h>

/* condition severity level */
typedef enum
{
  CO_ERROR_SEVERITY,
  CO_FATAL_SEVERITY,
  CO_WARNING_SEVERITY
} CO_SEVERITY;

/*
 * current condition message detail level
 * the detail level must start with 1 for message catalog set id
 */
typedef enum
{
  CO_DETAIL_USER = 1,
  CO_DETAIL_DBA,
  CO_DETAIL_DEBUG,
  CO_DETAIL_MAX
} CO_DETAIL;

extern int co_signal (int code, const char *format, ...);
#if defined(ENABLE_UNUSED_FUNCTION)
extern int co_code_module (int code);
extern int co_code_id (int code);
extern void co_report (FILE * file, CO_SEVERITY severity);
extern int co_put_detail (CO_DETAIL level);
extern const char *co_message (void);
#endif
extern int co_code (void);
extern void co_final (void);

#endif /* _CONDITION_HANDLER_H_ */
