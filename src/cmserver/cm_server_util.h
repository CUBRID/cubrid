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
 * cm_server_util.h -
 */

#ifndef _CM_SERVER_UTIL_H_
#define _CM_SERVER_UTIL_H_

#ident "$Id$"

#if defined(WINDOWS)
#include <winsock.h>
#endif

#include "cm_porting.h"
#include "cm_dep.h"
#include "cm_command_execute.h"
#include "cm_job_task.h"

#define makestring1(x) #x
#define makestring(x) makestring1(x)

#if defined(WINDOWS)
#define PSERVER_MODULE_NAME "cub_auto.exe"
#define FSERVER_MODULE_NAME "cub_js.exe"
#else
#define PSERVER_MODULE_NAME "cub_auto"
#define FSERVER_MODULE_NAME "cub_js"
#endif

#define TOKEN_LENGTH 40		/* multiple of 8 */
#define TOKEN_ENC_LENGTH	(TOKEN_LENGTH * 2 + 1)
#define PASSWD_LENGTH 32
#define PASSWD_ENC_LENGTH	(PASSWD_LENGTH * 2 + 1)

#define REMOVE_DIR_FORCED	1
#define REMOVE_EMPTY_DIR	0

#define MAX_AUTOQUERY_SCRIPT_SIZE	4095
#define MAX_JOB_CONFIG_FILE_LINE_LENGTH	(4096 + 256)

typedef enum
{
  TIME_STR_FMT_DATE = NV_ADD_DATE,
  TIME_STR_FMT_TIME = NV_ADD_TIME,
  TIME_STR_FMT_DATE_TIME = NV_ADD_DATE_TIME
} T_TIME_STR_FMT_TYPE;

int _op_check_is_localhost (char *token, char *tmpdbname);
void append_host_to_dbname (char *name_buf, const char *dbname, int buf_len);
void *increase_capacity (void *ptr, int block_size, int old_count,
			 int new_count);
char *strcpy_limit (char *dest, const char *src, int buf_len);
int ut_getdelim (char **lineptr, int *n, int delimiter, FILE * fp);
int ut_getline (char **lineptr, int *n, FILE * fp);
int ut_error_log (nvplist * req, const char *errmsg);
int ut_access_log (nvplist * req, const char *msg);
void uRemoveCRLF (char *str);
int uStringEqual (const char *str1, const char *str2);
int uStringEqualIgnoreCase (const char *str1, const char *str2);
int ut_gettaskcode (char *task);
int ut_send_response (SOCKET fd, nvplist * res);
int ut_receive_request (SOCKET fd, nvplist * req);
void ut_daemon_start (void);
void ut_dump_file_to_string (char *string, char *fname);
int uRetrieveDBDirectory (const char *dbname, char *target);
int _isRegisteredDB (char *);
int uReadDBnfo (char *);
void uWriteDBnfo (void);
void uWriteDBnfo2 (T_SERVER_STATUS_RESULT * cmd_res);
int uCreateLockFile (char *filename);
void uRemoveLockFile (int fd);
int uCreateDir (char *path);
int uRemoveDir (char *dir, int remove_file_in_dir);
void send_msg_with_file (SOCKET sock_fd, char *filename);
int send_file_to_client (SOCKET sock_fd, char *file_name, FILE * log_file);
int string_tokenize_accept_laststring_space (char *str, char *tok[],
					     int num_tok);
int make_version_info (char *cli_ver, int *major_ver, int *minor_ver);
int file_copy (char *src_file, char *dest_file);
int move_file (char *src_file, char *dest_file);

void close_all_fds (int init_fd);
char *ut_trim (char *str);
void server_fd_clear (SOCKET srv_fd);
int ut_write_pid (char *pid_file);
int ut_disk_free_space (char *path);
char *ip2str (unsigned char *ip, char *ip_str);
int string_tokenize (char *str, char *tok[], int num_tok);
int string_tokenize2 (char *str, char *tok[], int num_tok, int c);
int ut_get_task_info (char *task, char *access_log_flag,
		      T_TASK_FUNC * task_func);
char *time_to_str (time_t t, const char *fmt, char *buf, int type);
int read_from_socket (SOCKET fd, char *buf, int size);
int write_to_socket (SOCKET fd, const char *buf, int size);
void wait_proc (int pid);
int is_cmserver_process (int pid, const char *module_name);
int make_default_env (void);
#if defined(WINDOWS)
void remove_end_of_dir_ch (char *path);
#endif

#if defined(WINDOWS)
int kill (int pid, int signo);
void unix_style_path (char *path);
char *nt_style_path (char *path, char *new_path_buf);
#endif
#endif /* _CM_SERVER_UTIL_H_ */
