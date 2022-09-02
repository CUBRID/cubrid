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
 * cas_log.h -
 */

#ifndef	_CAS_LOG_H_
#define	_CAS_LOG_H_

#ident "$Id$"

typedef enum
{
  NEW_CONNECTION,
  CLIENT_CHANGED,
  ACL_REJECTED
} ACCESS_LOG_TYPE;

extern void cas_log_open (char *br_name);
extern void cas_log_reset (char *br_name);
extern void cas_log_close (bool flag);
#ifdef CAS_ERROR_LOG
extern void cas_error_log (int err_code, char *err_msg, int client_ip_addr);
#endif

extern int cas_access_log (struct timeval *start_time, int as_index, int client_ip_addr, char *dbname, char *dbuser,
			   ACCESS_LOG_TYPE log_type);
extern void cas_log_end (int mode, int run_time_sec, int run_time_msec);
extern void cas_log_write_nonl (unsigned int seq_num, bool unit_start, const char *fmt, ...);
extern void cas_log_write (unsigned int seq_num, bool unit_start, const char *fmt, ...);
extern void cas_log_write_and_end (unsigned int seq_num, bool unit_start, const char *fmt, ...);
extern void cas_log_write2_nonl (const char *fmt, ...);
extern void cas_log_write2 (const char *fmt, ...);
extern void cas_log_write_value_string (char *value, int size);
extern void cas_log_write_query_string (char *query, int size, int *pwd_offset_ptr);
extern void cas_log_write_client_ip (const unsigned char *ip_addr);
extern void cas_log_write_query_string_nonl (char *query, int size, int *pwd_offset_ptr);

#define ARG_FILE_LINE   __FILE__, __LINE__
#if defined (NDEBUG)
#define cas_log_debug(...)
#else
extern void cas_log_debug (const char *file_name, const int line_no, const char *fmt, ...);
#endif /* !NDEBUG */

extern char *cas_log_query_plan_file (int id);
extern void cas_log_query_info_init (int id, char is_only_query_plan);

extern void cas_slow_log_open (char *br_name);
extern void cas_slow_log_reset (char *br_name);
extern void cas_slow_log_close (void);
extern void cas_slow_log_end (void);
extern void cas_slow_log_write (struct timeval *log_time, unsigned int seq_num, bool unit_start, const char *fmt, ...);
extern void cas_slow_log_write_and_end (struct timeval *log_time, unsigned int seq_num, const char *fmt, ...);

extern void cas_slow_log_write2 (const char *fmt, ...);
extern void cas_slow_log_write_value_string (char *value, int size);
extern void cas_slow_log_write_query_string (char *query, int size, int *pwd_offset_ptr);
#endif /* _CAS_LOG_H_ */
