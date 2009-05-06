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
#ifdef DIAG_DEVEL
#include "perf_monitor.h"
#endif

#define MAX_INSTALLED_DB 20
#define MAX_UNICAS_PROC 40

#define AUTOBACKUP_CONF_ENTRY_NUM	14
#define AUTOADDVOL_CONF_ENTRY_NUM	7
#define AUTOHISTORY_CONF_ENTRY_NUM	15
#define AUTOUNICAS_CONF_ENTRY_NUM	9
#define AUTOEXECQUERY_CONF_ENTRY_NUM	7

#define DBMT_CONF_DIR                   "conf"
#define DBMT_LOG_DIR                    "log/manager"
#define DBMT_PID_DIR                    "var/manager"
#define DBMT_TMP_DIR                    "tmp"

#define DBMT_CUB_JS_PID                 "cub_js.pid"
#define DBMT_CUB_AUTO_PID               "cub_auto.pid"

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
  FID_PSERVER_PID_LOCK
#ifdef DIAG_DEVEL
    ,
  FID_DIAG_ACTIVITY_LOG,
  FID_DIAG_STATUS_TEMPLATE,
  FID_DIAG_ACTIVITY_TEMPLATE,
  FID_DIAG_SERVER_PID,
#endif
} T_DBMT_FILE_ID;

typedef struct
{
  T_DBMT_FILE_ID fid;
  char dir_name[16];
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
#ifdef DIAG_DEVEL
  DIAG_SYS_CONFIG diag_config;
#endif
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
