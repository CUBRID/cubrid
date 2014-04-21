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
 * cas_error_log.h -
 */

#ifndef	_CAS_ERROR_LOG_H_
#define	_CAS_ERROR_LOG_H_

#ident "$Id$"

extern void cas_error_log_open (char *br_name);
extern void cas_error_log_close (bool flag);
extern void cas_error_log_write (int dbms_errno, const char *dbms_errmsg);
extern char *cas_error_log_get_eid (char *buf, size_t bufsz);

#endif /* _CAS_ERROR_LOG_H_ */
