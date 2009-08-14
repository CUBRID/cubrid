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
 * cas_log.h -
 */

#ifndef	_CAS_LOG_H_
#define	_CAS_LOG_H_

#ident "$Id$"

extern void cas_log_open (char *br_name, int as_index);
extern void cas_log_reset (char *br_name, int as_index);
extern void cas_log_close (bool flag);
#ifdef CAS_ERROR_LOG
extern void cas_error_log (int err_code, char *err_msg, int client_ip_addr);
#endif
extern int cas_access_log (struct timeval *start_time, int as_index,
			   int client_ip_addr);
extern void cas_log_end (int mode, int run_time_sec, int run_time_msec);
extern void cas_log_write_nonl (unsigned int seq_num, bool unit_start,
			      const char *fmt, ...);
extern void cas_log_write (unsigned int seq_num, bool unit_start,
			   const char *fmt, ...);
extern void cas_log_write_and_end (unsigned int seq_num, bool unit_start,
				   const char *fmt, ...);
extern void cas_log_write2_nonl (const char *fmt, ...);
extern void cas_log_write2 (const char *fmt, ...);
extern void cas_log_write_value_string (char *value, int size);
extern void cas_log_write_query_string (char *query, int size);

#define ARG_FILE_LINE   __FILE__, __LINE__
#if defined (NDEBUG)
#define cas_log_debug(...)
#else
extern void cas_log_debug (const char *file_name, const int line_no,
			   const char *fmt, ...);
#endif /* !NDEBUG */

extern char *cas_log_query_plan_file (int id);
extern char *cas_log_query_histo_file (int id);
extern int cas_log_query_info_init (int id);
extern void cas_log_query_info_next (void);
extern void cas_log_query_info_end (void);

extern void cas_log_error_handler_begin (void);
extern void cas_log_error_handler_end (void);
extern void cas_log_error_handler_clear (void);
extern char *cas_log_error_handler_asprint (char *buf, size_t bufsz,
					    bool clear);
#endif /* _CAS_LOG_H_ */
