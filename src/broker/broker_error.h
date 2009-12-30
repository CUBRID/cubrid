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
 * broker_error.h - Error code and message handling module
 *           This file contains exported stuffs from the error code and message
 *           handling modules.
 */

#ifndef	_BROKER_ERROR_H_
#define	_BROKER_ERROR_H_

#ident "$Id$"

#include "broker_error_def.i"

#define	UW_SET_ERROR_CODE(code, os_errno) \
		uw_set_error_code(__FILE__, __LINE__, (code), (os_errno))

extern void uw_set_error_code (const char *file_name, int line_no,
			       int error_code, int os_errno);
extern int uw_get_error_code (void);
extern int uw_get_os_error_code (void);
extern const char *uw_get_error_message (int error_code, int os_errno);
#if defined (ENABLE_UNUSED_FUNCTION)
extern const char *uw_error_message (int error_code);
extern void uw_error_message_r (int error_code, char *err_msg);
extern void uw_os_err_msg (int err_code, char *err_msg);
#endif

#endif /* _BROKER_ERROR_H_ */
