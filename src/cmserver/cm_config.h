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
 * cm_config.h -
 */

#ifndef _CM_CONFIG_H_
#define _CM_CONFIG_H_

#ident "$Id$"

#include <stdio.h>

#define MAX_INSTALLED_DB 20
#define MAX_UNICAS_PROC 40

#define AUTOBACKUP_CONF_ENTRY_NUM	14
#define AUTOADDVOL_CONF_ENTRY_NUM	7
#define AUTOHISTORY_CONF_ENTRY_NUM	15
#define AUTOUNICAS_CONF_ENTRY_NUM	9
#define AUTOEXECQUERY_CONF_ENTRY_NUM	7

#if !defined (DO_NOT_USE_CUBRIDENV)
#define DBMT_CONF_DIR                   "conf"
#define DBMT_LOG_DIR                    "log/manager"
#define DBMT_PID_DIR                    "var/manager"
#define DBMT_TMP_DIR                    "tmp"
#else
#define DBMT_CONF_DIR                   CUBRID_CONFDIR
#define DBMT_LOG_DIR                    CUBRID_VARDIR "/manager"
#define DBMT_PID_DIR                    CUBRID_VARDIR "/manager"
#define DBMT_TMP_DIR                    CUBRID_TMPDIR
#endif

#define DBMT_CUB_JS_PID                 "cub_js.pid"
#define DBMT_CUB_AUTO_PID               "cub_auto.pid"

/* utility option list, from executables/utility.h */
#define UTIL_OPTION_CREATEDB                    "createdb"
#define UTIL_OPTION_RENAMEDB                    "renamedb"
#define UTIL_OPTION_COPYDB                      "copydb"
#define UTIL_OPTION_DELETEDB                    "deletedb"
#define UTIL_OPTION_BACKUPDB                    "backupdb"
#define UTIL_OPTION_RESTOREDB                   "restoredb"
#define UTIL_OPTION_ADDVOLDB                    "addvoldb"
#define UTIL_OPTION_SPACEDB                     "spacedb"
#define UTIL_OPTION_LOCKDB                      "lockdb"
#define UTIL_OPTION_KILLTRAN                    "killtran"
#define UTIL_OPTION_OPTIMIZEDB                  "optimizedb"
#define UTIL_OPTION_INSTALLDB                   "installdb"
#define UTIL_OPTION_DIAGDB                      "diagdb"
#define UTIL_OPTION_PATCHDB                     "emergency_patchlog"
#define UTIL_OPTION_CHECKDB                     "checkdb"
#define UTIL_OPTION_ALTERDBHOST                 "alterdbhost"
#define UTIL_OPTION_PLANDUMP                    "plandump"
#define UTIL_OPTION_ESTIMATE_DATA               "estimate_data"
#define UTIL_OPTION_ESTIMATE_INDEX              "estimate_index"
#define UTIL_OPTION_LOADDB                      "loaddb"
#define UTIL_OPTION_UNLOADDB                    "unloaddb"
#define UTIL_OPTION_COMPACTDB                   "compactdb"
#define UTIL_OPTION_PARAMDUMP                   "paramdump"
#define UTIL_OPTION_CHANGEMODE                  "changemode"
#define UTIL_OPTION_COPYLOGDB                   "copylogdb"
#define UTIL_OPTION_APPLYLOGDB                  "applylogdb"
#define UTIL_OPTION_LOGFILEDUMP                 "logfiledump"

/* killtran option list */
#define KILLTRAN_KILL_TRANSACTION_INDEX_S       'i'
#define KILLTRAN_KILL_TRANSACTION_INDEX_L       "kill-transaction-index"
#define KILLTRAN_KILL_USER_NAME_S               11701
#define KILLTRAN_KILL_USER_NAME_L               "kill-user-name"
#define KILLTRAN_KILL_HOST_NAME_S               11702
#define KILLTRAN_KILL_HOST_NAME_L               "kill-host-name"
#define KILLTRAN_KILL_PROGRAM_NAME_S            11703
#define KILLTRAN_KILL_PROGRAM_NAME_L            "kill-program-name"
#define KILLTRAN_DBA_PASSWORD_S                 'p'
#define KILLTRAN_DBA_PASSWORD_L                 "dba-password"
#define KILLTRAN_DISPLAY_INFORMATION_S          'd'
#define KILLTRAN_DISPLAY_INFORMATION_L          "display-information"
#define KILLTRAN_FORCE_S                        'f'
#define KILLTRAN_FORCE_L                        "force"

/* sqlx option list */
#define CSQL_SA_MODE_S                          'S'
#define CSQL_SA_MODE_L                          "SA-mode"
#define CSQL_CS_MODE_S                          'C'
#define CSQL_CS_MODE_L                          "CS-mode"
#define CSQL_USER_S                             'u'
#define CSQL_USER_L                             "user"
#define CSQL_PASSWORD_S                         'p'
#define CSQL_PASSWORD_L                         "password"
#define CSQL_ERROR_CONTINUE_S                   'e'
#define CSQL_ERROR_CONTINUE_L                   "error-continue"
#define CSQL_INPUT_FILE_S                       'i'
#define CSQL_INPUT_FILE_L                       "input-file"
#define CSQL_OUTPUT_FILE_S                      'o'
#define CSQL_OUTPUT_FILE_L                      "output-file"
#define CSQL_SINGLE_LINE_S                      's'
#define CSQL_SINGLE_LINE_L                      "single-line"
#define CSQL_COMMAND_S                          'c'
#define CSQL_COMMAND_L                          "command"
#define CSQL_LINE_OUTPUT_S                      'l'
#define CSQL_LINE_OUTPUT_L                      "line-output"
#define CSQL_READ_ONLY_S                        'r'
#define CSQL_READ_ONLY_L                        "read-only"
#define CSQL_NO_AUTO_COMMIT_S                   12010
#define CSQL_NO_AUTO_COMMIT_L                   "no-auto-commit"
#define CSQL_NO_PAGER_S                         12011
#define CSQL_NO_PAGER_L                         "no-pager"
#define CSQL_SYSADM_S                           12012
#define CSQL_SYSADM_L                           "sysadm"

/* addvoldb option list */
#define ADDVOL_VOLUME_NAME_S                    'n'
#define ADDVOL_VOLUME_NAME_L                    "volume-name"
#define ADDVOL_FILE_PATH_S                      'F'
#define ADDVOL_FILE_PATH_L                      "file-path"
#define ADDVOL_COMMENT_S                        10702
#define ADDVOL_COMMENT_L                        "comment"
#define ADDVOL_PURPOSE_S                        'p'
#define ADDVOL_PURPOSE_L                        "purpose"
#define ADDVOL_SA_MODE_S                        'S'
#define ADDVOL_SA_MODE_L                        "SA-mode"
#define ADDVOL_CS_MODE_S                        'C'
#define ADDVOL_CS_MODE_L                        "CS-mode"

/* backupdb option list */
#define BACKUP_DESTINATION_PATH_S               'D'
#define BACKUP_DESTINATION_PATH_L       "destination-path"
#define BACKUP_REMOVE_ARCHIVE_S                 'r'
#define BACKUP_REMOVE_ARCHIVE_L                 "remove-archive"
#define BACKUP_LEVEL_S                          'l'
#define BACKUP_LEVEL_L                          "level"
#define BACKUP_OUTPUT_FILE_S                    'o'
#define BACKUP_OUTPUT_FILE_L                    "output-file"
#define BACKUP_SA_MODE_S                        'S'
#define BACKUP_SA_MODE_L                        "SA-mode"
#define BACKUP_CS_MODE_S                        'C'
#define BACKUP_CS_MODE_L                        "CS-mode"
#define BACKUP_NO_CHECK_S                       10506
#define BACKUP_NO_CHECK_L                       "no-check"
#define BACKUP_THREAD_COUNT_S                   't'
#define BACKUP_THREAD_COUNT_L                   "thread-count"
#define BACKUP_COMPRESS_S                       'z'
#define BACKUP_COMPRESS_L                       "compress"
#define BACKUP_EXCEPT_ACTIVE_LOG_S              'e'
#define BACKUP_EXCEPT_ACTIVE_LOG_L              "except-active-log"
#define BACKUP_SAFE_PAGE_ID_S                    10510
#define BACKUP_SAFE_PAGE_ID_L                    "safe-page-id"

/* restoredb option list */
#define RESTORE_UP_TO_DATE_S                    'd'
#define RESTORE_UP_TO_DATE_L                    "up-to-date"
#define RESTORE_LIST_S                          10601
#define RESTORE_LIST_L                          "list"
#define RESTORE_BACKUP_FILE_PATH_S              'B'
#define RESTORE_BACKUP_FILE_PATH_L              "backup-file-path"
#define RESTORE_LEVEL_S                         'l'
#define RESTORE_LEVEL_L                         "level"
#define RESTORE_PARTIAL_RECOVERY_S              'p'
#define RESTORE_PARTIAL_RECOVERY_L              "partial-recovery"
#define RESTORE_OUTPUT_FILE_S                   'o'
#define RESTORE_OUTPUT_FILE_L                   "output-file"
#define RESTORE_REPLICATION_MODE_S              'r'
#define RESTORE_REPLICATION_MODE_L              "replication-mode"
#define RESTORE_USE_DATABASE_LOCATION_PATH_S    'u'
#define RESTORE_USE_DATABASE_LOCATION_PATH_L    "use-database-location-path"


/* spacedb option list */
#define SPACE_OUTPUT_FILE_S                     'o'
#define SPACE_OUTPUT_FILE_L                     "output-file"
#define SPACE_SA_MODE_S                         'S'
#define SPACE_SA_MODE_L                         "SA-mode"
#define SPACE_CS_MODE_S                         'C'
#define SPACE_CS_MODE_L                         "CS-mode"
#define SPACE_SIZE_UNIT_S                       10803
#define SPACE_SIZE_UNIT_L                       "size-unit"

/* checkdb option list */
#define CHECK_SA_MODE_S                         'S'
#define CHECK_SA_MODE_L                         "SA-mode"
#define CHECK_CS_MODE_S                         'C'
#define CHECK_CS_MODE_L                         "CS-mode"
#define CHECK_REPAIR_S                          'r'
#define CHECK_REPAIR_L                          "repair"

/* renamedb option list */
#define RENAME_EXTENTED_VOLUME_PATH_S           'E'
#define RENAME_EXTENTED_VOLUME_PATH_L           "extended-volume-path"
#define RENAME_CONTROL_FILE_S                   'i'
#define RENAME_CONTROL_FILE_L                   "control-file"
#define RENAME_DELETE_BACKUP_S                  'd'
#define RENAME_DELETE_BACKUP_L                  "delete-backup"


/* copydb option list */
#define COPY_SERVER_NAME_S                      10300
#define COPY_SERVER_NAME_L                      "server-name"
#define COPY_FILE_PATH_S                        'F'
#define COPY_FILE_PATH_L                        "file-path"
#define COPY_LOG_PATH_S                         'L'
#define COPY_LOG_PATH_L                         "log-path"
#define COPY_EXTENTED_VOLUME_PATH_S             'E'
#define COPY_EXTENTED_VOLUME_PATH_L             "extended-volume-path"
#define COPY_CONTROL_FILE_S                     'i'
#define COPY_CONTROL_FILE_L                     "control-file"
#define COPY_REPLACE_S                          'r'
#define COPY_REPLACE_L                          "replace"
#define COPY_DELETE_SOURCE_S                    'd'
#define COPY_DELETE_SOURCE_L                    "delete-source"

/* deletedb option list */
#define DELETE_OUTPUT_FILE_S                    'o'
#define DELETE_OUTPUT_FILE_L                    "output-file"
#define DELETE_DELETE_BACKUP_S                  'd'
#define DELETE_DELETE_BACKUP_L                  "delete-backup"


/* createdb option list */
#define CREATE_PAGES_S                          'p'
#define CREATE_PAGES_L                          "pages"
#define CREATE_COMMENT_S                        10102
#define CREATE_COMMENT_L                        "comment"
#define CREATE_FILE_PATH_S                      'F'
#define CREATE_FILE_PATH_L                      "file-path"
#define CREATE_LOG_PATH_S                       'L'
#define CREATE_LOG_PATH_L                       "log-path"
#define CREATE_SERVER_NAME_S                    10105
#define CREATE_SERVER_NAME_L                    "server-name"
#define CREATE_REPLACE_S                        'r'
#define CREATE_REPLACE_L                        "replace"
#define CREATE_MORE_VOLUME_FILE_S               10107
#define CREATE_MORE_VOLUME_FILE_L               "more-volume-file"
#define CREATE_USER_DEFINITION_FILE_S           10108
#define CREATE_USER_DEFINITION_FILE_L           "user-definition-file"
#define CREATE_CSQL_INITIALIZATION_FILE_S       10109
#define CREATE_CSQL_INITIALIZATION_FILE_L       "csql-initialization-file"
#define CREATE_OUTPUT_FILE_S                    'o'
#define CREATE_OUTPUT_FILE_L                    "output-file"
#define CREATE_VERBOSE_S                        'v'
#define CREATE_VERBOSE_L                        "verbose"
#define CREATE_CHARSET_S                        10112
#define CREATE_CHARSET_L                        "charset"
#define CREATE_LOG_PAGE_COUNT_S                 'l'
#define CREATE_LOG_PAGE_COUNT_L                 "log-page-count"
#define CREATE_PAGE_SIZE_S                      's'
#define CREATE_PAGE_SIZE_L                      "page-size"
#define CREATE_LOG_PAGE_SIZE_L                  "log-page-size"

/* loaddb option list */
#define LOAD_USER_S                             'u'
#define LOAD_USER_L                             "user"
#define LOAD_PASSWORD_S                         'p'
#define LOAD_PASSWORD_L                         "password"
#define LOAD_CHECK_ONLY_S                       11802
#define LOAD_CHECK_ONLY_L                       "data-file-check-only"
#define LOAD_LOAD_ONLY_S                        'l'
#define LOAD_LOAD_ONLY_L                        "load-only"
#define LOAD_ESTIMATED_SIZE_S                   11804
#define LOAD_ESTIMATED_SIZE_L                   "estimated-size"
#define LOAD_VERBOSE_S                          'v'
#define LOAD_VERBOSE_L                          "verbose"
#define LOAD_NO_STATISTICS_S                    11806
#define LOAD_NO_STATISTICS_L                    "no-statistics"
#define LOAD_PERIODIC_COMMIT_S                  'c'
#define LOAD_PERIODIC_COMMIT_L                  "periodic-commit"
#define LOAD_NO_OID_S                           11808
#define LOAD_NO_OID_L                           "no-oid"
#define LOAD_SCHEMA_FILE_S                      's'
#define LOAD_SCHEMA_FILE_L                      "schema-file"
#define LOAD_INDEX_FILE_S                       'i'
#define LOAD_INDEX_FILE_L                       "index-file"
#define LOAD_IGNORE_LOGGING_S                   11811
#define LOAD_IGNORE_LOGGING_L                   "no-logging"
#define LOAD_DATA_FILE_S                        'd'
#define LOAD_DATA_FILE_L                        "data-file"
#define LOAD_ERROR_CONTROL_FILE_S               'e'
#define LOAD_ERROR_CONTROL_FILE_L               "error-control-file"
#define LOAD_IGNORE_CLASS_S                     11814
#define LOAD_IGNORE_CLASS_L                     "ignore-class-file"
#define LOAD_SA_MODE_S                          'S'
#define LOAD_SA_MODE_L                          "SA-mode"
#define LOAD_CS_MODE_S                          11815
#define LOAD_CS_MODE_L                          "CS-hidden"

/* unloaddb option list */
#define UNLOAD_INPUT_CLASS_FILE_S               'i'
#define UNLOAD_INPUT_CLASS_FILE_L               "input-class-file"
#define UNLOAD_INCLUDE_REFERENCE_S              11901
#define UNLOAD_INCLUDE_REFERENCE_L              "include-reference"
#define UNLOAD_INPUT_CLASS_ONLY_S               11902
#define UNLOAD_INPUT_CLASS_ONLY_L               "input-class-only"
#define UNLOAD_LO_COUNT_S                       11903
#define UNLOAD_LO_COUNT_L                       "lo-count"
#define UNLOAD_ESTIMATED_SIZE_S                 11904
#define UNLOAD_ESTIMATED_SIZE_L                 "estimated-size"
#define UNLOAD_CACHED_PAGES_S                   11905
#define UNLOAD_CACHED_PAGES_L                   "cached-pages"
#define UNLOAD_OUTPUT_PATH_S                    'O'
#define UNLOAD_OUTPUT_PATH_L                    "output-path"
#define UNLOAD_SCHEMA_ONLY_S                    's'
#define UNLOAD_SCHEMA_ONLY_L                    "schema-only"
#define UNLOAD_DATA_ONLY_S                      'd'
#define UNLOAD_DATA_ONLY_L                      "data-only"
#define UNLOAD_OUTPUT_PREFIX_S                  11909
#define UNLOAD_OUTPUT_PREFIX_L                  "output-prefix"
#define UNLOAD_HASH_FILE_S                      11910
#define UNLOAD_HASH_FILE_L                      "hash-file"
#define UNLOAD_VERBOSE_S                        'v'
#define UNLOAD_VERBOSE_L                        "verbose"
#define UNLOAD_USE_DELIMITER_S                  11912
#define UNLOAD_USE_DELIMITER_L                  "use-delimiter"
#define UNLOAD_SA_MODE_S                        'S'
#define UNLOAD_SA_MODE_L                        "SA-mode"
#define UNLOAD_CS_MODE_S                        'C'
#define UNLOAD_CS_MODE_L                        "CS-mode"

/* compactdb option list */
#define COMPACT_VERBOSE_S                       'v'
#define COMPACT_VERBOSE_L                       "verbose"

/* paramdump option list */
#define PARAMDUMP_BOTH_S                        'b'
#define PARAMDUMP_BOTH_L                        "both"
#define PARAMDUMP_SA_MODE_S                     'S'
#define PARAMDUMP_SA_MODE_L                     "SA-mode"
#define PARAMDUMP_CS_MODE_S                     'C'
#define PARAMDUMP_CS_MODE_L                     "CS-mode"

/* plandump option list */
#define PLANDUMP_DROP_S                        'd'
#define PLANDUMP_DROP_L                        "drop"

/* lockdb option list */
#define LOCK_OUTPUT_FILE_S                      'o'
#define LOCK_OUTPUT_FILE_L                      "output-file"

#if defined(WINDOWS)
#define UTIL_EXE_EXT            ".exe"
#else
#define UTIL_EXE_EXT            ""
#endif
#define UTIL_MASTER_NAME        "cub_master" UTIL_EXE_EXT
#define UTIL_COMMDB_NAME        "cub_commdb" UTIL_EXE_EXT
#define UTIL_CUBRID_NAME        "cub_server" UTIL_EXE_EXT
#define UTIL_BROKER_NAME        "cubrid_broker" UTIL_EXE_EXT
#define UTIL_MONITOR_NAME       "broker_monitor" UTIL_EXE_EXT
#define UTIL_CUB_AUTO_NAME      "cub_auto" UTIL_EXE_EXT
#define UTIL_CUB_JS_NAME        "cub_js" UTIL_EXE_EXT
#define UTIL_REPL_SERVER_NAME   "repl_server" UTIL_EXE_EXT
#define UTIL_REPL_AGENT_NAME    "repl_agent" UTIL_EXE_EXT
#define UTIL_ADMIN_NAME         "cub_admin" UTIL_EXE_EXT
#define UTIL_SQLX_NAME          "sqlx" UTIL_EXE_EXT
#define UTIL_CSQL_NAME          "csql" UTIL_EXE_EXT
#define UTIL_CUBRID_REL_NAME    "cubrid_rel" UTIL_EXE_EXT
#define UTIL_OLD_COMMDB_NAME    "commdb" UTIL_EXE_EXT
#define UTIL_CUBRID             "cubrid" UTIL_EXE_EXT


#define COMMDB_SERVER_STOP      "-S"
#define COMMDB_SERVER_STATUS    "-P"
#define COMMDB_REPL_STATUS      "-R"
#define COMMDB_ALL_STATUS       "-O"
#define COMMDB_ALL_STOP         "-A"

#define PRINT_CMD_SERVER        "server"
#define PRINT_CMD_REPL_SERVER   "repl_server"
#define PRINT_CMD_REPL_AGENT    "repl_agent"
#define PRINT_CMD_START         "start"
#define PRINT_CMD_STOP          "stop"
#define PRINT_CMD_STATUS        "status"

/* from perf_monitor.h */
#define MAX_SERVER_THREAD_COUNT         500



#define auto_conf_execquery_delete(FID, DBNAME)	auto_conf_delete(FID, DBNAME)
#define auto_conf_execquery_rename(FID, OLD, NEW)	auto_conf_rename(FID, OLD, NEW)
#define auto_conf_addvol_delete(FID, DBNAME)	auto_conf_delete(FID, DBNAME)
#define auto_conf_backup_delete(FID, DBNAME)	auto_conf_delete(FID, DBNAME)
#define auto_conf_history_delete(FID, DBNAME)	auto_conf_delete(FID, DBNAME)
#define auto_conf_start_delete(FID, DBNAME)	auto_conf_delete(FID, DBNAME)
#define auto_conf_addvol_rename(FID, OLD, NEW)	auto_conf_rename(FID, OLD, NEW)
#define auto_conf_backup_rename(FID, OLD, NEW)	auto_conf_rename(FID, OLD, NEW)
#define auto_conf_history_rename(FID, OLD, NEW)	auto_conf_rename(FID, OLD, NEW)
#define auto_conf_start_rename(FID, OLD, NEW)	auto_conf_rename(FID, OLD, NEW)


typedef enum
{
  TRAN_UNKNOWN_ISOLATION = 0x00,	/*        0  0000 */
  TRAN_COMMIT_CLASS_UNCOMMIT_INSTANCE = 0x01,	/*        0  0001 */
  TRAN_DEGREE_1_CONSISTENCY = 0x01,	/* Alias of above */

  TRAN_COMMIT_CLASS_COMMIT_INSTANCE = 0x02,	/*        0  0010 */
  TRAN_DEGREE_2_CONSISTENCY = 0x02,	/* Alias of above */

  TRAN_REP_CLASS_UNCOMMIT_INSTANCE = 0x03,	/*        0  0011 */
  TRAN_READ_UNCOMMITTED = 0x03,	/* Alias of above */

  TRAN_REP_CLASS_COMMIT_INSTANCE = 0x04,	/*        0  0100 */
  TRAN_READ_COMMITTED = 0x04,	/* Alias of above */
  TRAN_CURSOR_STABILITY = 0x04,	/* Alias of above */

  TRAN_REP_CLASS_REP_INSTANCE = 0x05,	/*        0  0101 */
  TRAN_REP_READ = 0x05,		/* Alias of above */
  TRAN_DEGREE_2_9999_CONSISTENCY = 0x05,	/* Alias of above */

  TRAN_SERIALIZABLE = 0x06,	/*        0  0110 */
  TRAN_DEGREE_3_CONSISTENCY = 0x06,	/* Alias of above */
  TRAN_NO_PHANTOM_READ = 0x06,	/* Alias of above */

  TRAN_DEFAULT_ISOLATION = TRAN_REP_CLASS_UNCOMMIT_INSTANCE,

} DB_TRAN_ISOLATION;		/* extract from dbi.h */


typedef enum
{
  FID_DBMT_CONF,
  FID_FSERVER_PID,
  FID_PSERVER_PID,
  FID_FSERVER_ACCESS_LOG,
  FID_PSERVER_ACCESS_LOG,
  FID_FSERVER_ERROR_LOG,
  FID_PSERVER_ERROR_LOG,
  FID_DBMT_PASS,
  FID_DBMT_CUBRID_PASS,
  FID_CONN_LIST,
  FID_AUTO_ADDVOLDB_CONF,
  FID_AUTO_ADDVOLDB_LOG,
  FID_AUTO_BACKUPDB_CONF,
  FID_AUTO_HISTORY_CONF,
  FID_AUTO_UNICASM_CONF,
  FID_AUTO_EXECQUERY_CONF,
  FID_PSVR_DBINFO_TEMP,
  FID_LOCK_CONN_LIST,
  FID_LOCK_PSVR_DBINFO,
  FID_LOCK_SVR_LOG,
  FID_LOCK_DBMT_PASS,
  FID_UC_AUTO_RESTART_LOG,
  FID_PSERVER_PID_LOCK,
  FID_DIAG_ACTIVITY_LOG,
  FID_DIAG_STATUS_TEMPLATE,
  FID_DIAG_ACTIVITY_TEMPLATE,
  FID_DIAG_SERVER_PID,
} T_DBMT_FILE_ID;

typedef struct
{
  T_DBMT_FILE_ID fid;
  char dir_name[PATH_MAX];
  char file_name[32];
} T_DBMT_FILE_INFO;

typedef struct
{
  /*
   *  program name (either fserver or pserver)
   */
  char *szProgname;

  /*
   *  CUBRID environment variables
   */
  char *szCubrid;		/* CUBRID           */
  char *szCubrid_databases;	/* CUBRID_DATABASES */

  char *dbmt_tmp_dir;

  int iFsvr_port;
  int iPsvr_port;
  int iMonitorInterval;
  int iAllow_AdminMultiCon;
  int iAutoStart_UniCAS;

  int hmtab1, hmtab2, hmtab3, hmtab4;
} sys_config;

void sys_config_init (void);
int uReadEnvVariables (char *, FILE *);
int uCheckSystemConfig (FILE *);
int uReadSystemConfig (void);

char *conf_get_dbmt_file (T_DBMT_FILE_ID dbmt_fid, char *buf);
char *conf_get_dbmt_file2 (T_DBMT_FILE_ID dbmt_fid, char *buf);

extern sys_config sco;
extern const char *autobackup_conf_entry[AUTOBACKUP_CONF_ENTRY_NUM];
extern const char *autoaddvol_conf_entry[AUTOADDVOL_CONF_ENTRY_NUM];
extern const char *autohistory_conf_entry[AUTOHISTORY_CONF_ENTRY_NUM];
extern const char *autounicas_conf_entry[AUTOUNICAS_CONF_ENTRY_NUM];

extern int auto_conf_delete (T_DBMT_FILE_ID fid, char *dbname);
extern int auto_conf_rename (T_DBMT_FILE_ID fid, char *src_dbname,
			     char *dest_dbname);

#endif /* _CM_CONFIG_H_ */
