/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * cas_log.h -
 */

#ifndef	_CAS_LOG_H_
#define	_CAS_LOG_H_

#ident "$Id$"

extern void cas_log_init (T_TIMEVAL * start_time);
extern void cas_log_open (char *br_name, int as_index);
extern void cas_log_reset (char *br_name, int as_index);
#ifdef CAS_ERROR_LOG
extern void cas_error_log (int err_code, char *err_msg, int client_ip_addr);
#endif
extern int cas_access_log (T_TIMEVAL * start_time, int as_index,
			   int client_ip_addr);
extern void cas_log_end (T_TIMEVAL * start_time, char *br_name, int as_index,
			 int sql_log_time, char *sql_log2_filename,
			 int sql_log_max_size, int close_flag);
extern void cas_log_write (unsigned int seq_num, char print_new_line,
			   char *fmt, ...);
extern void cas_log_write2 (char *fmt, ...);
extern void cas_log_write_query_string (char *query, char print_new_line);

extern char *cas_log_query_plan_file (int id);
extern char *cas_log_query_histo_file (int id);
extern int cas_log_query_info_init (int id);
extern void cas_log_query_info_next (void);
extern void cas_log_query_info_end (void);

#endif /* _CAS_LOG_H_ */
