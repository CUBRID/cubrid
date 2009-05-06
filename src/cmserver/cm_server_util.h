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
#include "cm_nameval.h"
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
#define PASSWD_LENGTH 16
#define PASSWD_ENC_LENGTH	(PASSWD_LENGTH * 2 + 1)

#define REMOVE_DIR_FORCED	1
#define REMOVE_EMPTY_DIR	0

#define MAX_AUTOQUERY_SCRIPT_SIZE	4095
#define MAX_JOB_CONFIG_FILE_LINE_LENGTH	(4096 + 256)

/* error codes */
#define ERR_NO_ERROR		1000
#define ERR_GENERAL_ERROR	1010
#define ERR_UNDEFINED_TASK	1020
#define ERR_DBDIRNAME_NULL	1030
#define ERR_REQUEST_FORMAT	1040
#define ERR_DATABASETXT_OPEN	1050
#define ERR_USER_CAPABILITY	1060
#define ERR_FILE_INTEGRITY	1070
#define ERR_FILE_COMPRESS	1075
#define ERR_SYSTEM_CALL		1080
#define ERR_PASSWORD_FILE	1090
#define ERR_PARAM_MISSING	1100
#define ERR_DIR_CREATE_FAIL	1110
#define ERR_DIR_REMOVE_FAIL 1115
#define ERR_GET_FILE 		1117
#define ERR_FILE_OPEN_FAIL	1120
#define ERR_FILE_CREATE_FAIL	1125
#define ERR_SYSTEM_CALL_CON_DUMP 1130
#define ERR_STAT		1140
#define ERR_OPENDIR		1150
#define ERR_UNICASCONF_OPEN	1160
#define ERR_UNICASCONF_PARAM_MISSING 1170
#define ERR_STANDALONE_MODE	1180
#define ERR_DB_ACTIVE		1190
#define ERR_DB_INACTIVE		1195
#define ERR_DB_NONEXISTANT	1200
#define ERR_DBMTUSER_EXIST	1210
#define ERR_DIROPENFAIL		1220
#define ERR_PERMISSION		1230
#define ERR_INVALID_TOKEN	1240
#define ERR_DBLOGIN_FAIL	1250
#define ERR_DBRESTART_FAIL	1260
#define ERR_DBUSER_NOTFOUND	1270
#define ERR_DBPASSWD_CLEAR	1280
#define ERR_DBPASSWD_SET	1290
#define ERR_MEM_ALLOC		1300
#define ERR_TMPFILE_OPEN_FAIL	1310
#define ERR_WITH_MSG		1320
#define ERR_UPA_SYSTEM		1330
#ifdef DIAG_DEVEL
#define ERR_TEMPLATE_ALREADY_EXIST 1340
#endif

typedef enum
{
  DB_SERVICE_MODE_NONE = 0,
  DB_SERVICE_MODE_SA = 1,
  DB_SERVICE_MODE_CS = 2
} T_DB_SERVICE_MODE;

typedef enum
{
  TIME_STR_FMT_DATE = NV_ADD_DATE,
  TIME_STR_FMT_TIME = NV_ADD_TIME,
  TIME_STR_FMT_DATE_TIME = NV_ADD_DATE_TIME
} T_TIME_STR_FMT_TYPE;

int ut_error_log (nvplist * req, const char *errmsg);
int ut_access_log (nvplist * req, const char *msg);
void uRemoveCRLF (char *str);
int uStringEqual (const char *str1, const char *str2);
int uStringEqualIgnoreCase (const char *str1, const char *str2);
int ut_gettaskcode (char *task);
int ut_send_response (SOCKET fd, nvplist * res);
int ut_receive_request (SOCKET fd, nvplist * req);
void ut_daemon_start (void);
int uIsDatabaseActive (char *dbname);
int uIsDatabaseActive2 (T_COMMDB_RESULT * cmd_res, char *dbn);
void ut_dump_file_to_string (char *string, char *fname);
int uRetrieveDBDirectory (const char *dbname, char *target);
int uRetrieveDBLogDirectory (char *dbname, char *target);
T_DB_SERVICE_MODE uDatabaseMode (char *dbname);
int _isRegisteredDB (char *);
int uReadDBnfo (char *);
void uWriteDBnfo (void);
void uWriteDBnfo2 (T_COMMDB_RESULT * cmd_res);
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
int get_db_server_pid (char *dbname);
int ut_get_task_info (char *task, char *access_log_flag,
		      T_TASK_FUNC * task_func);
char *time_to_str (time_t t, const char *fmt, char *buf, int type);
int read_from_socket (SOCKET fd, char *buf, int size);
int write_to_socket (SOCKET fd, const char *buf, int size);
int run_child (const char *const argv[], int wait_flag,
	       const char *stdin_file, char *stdout_file, char *stderr_file,
	       int *exit_status);
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
