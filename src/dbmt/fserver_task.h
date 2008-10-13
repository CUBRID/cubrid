/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * fserver_task.h - 
 */

#ifndef _FSERVER_TASK_H_
#define _FSERVER_TASK_H_

#ident "$Id$"

#ifdef WIN32
#include <io.h>
#endif

#include "nameval.h"

#define DBMT_ERROR_MSG_SIZE	1024

#define INIT_CUBRID_ERROR_FILE(PTR)		\
	do {					\
	  PTR = getenv("CUBRID_ERROR_LOG");	\
	} while (0)

#define SET_TRANSACTION_NO_WAIT_MODE_ENV()			\
  	do {						\
	  putenv("CUBRID_LOCK_TIMEOUT_IN_SECS=1");	\
	  putenv("CUBRID_ISOLATION_LEVEL=TRAN_COMMIT_CLASS_UNCOMMIT_INSTANCE");	\
	} while (0)

typedef enum
{
  TS_UNDEFINED,
  TS_STARTINFO,
  TS_USERINFO,
  TS_CREATEUSER,
  TS_DELETEUSER,
  TS_CREATEDB,
  TS_DELETEDB,
  TS_RENAMEDB,
  TS_STARTDB,
  TS_STOPDB,
  TS_UPDATEUSER,
  TS_DBSPACEINFO,
  TS_CLASSINFO,
  TS_CLASS,
  TS_RENAMECLASS,
  TS_DROPCLASS,
  TS_SETSYSPARAM,
  TS_GETALLSYSPARAM,
  TS_ADDVOLDB,
  TS_CREATECLASS,
  TS_CREATEVCLASS,
  TS_GETLOGINFO,
  TS_VIEWLOG,
  TS_RESETLOG,
  TS_ADDATTRIBUTE,
  TS_DROPATTRIBUTE,
  TS_UPDATEATTRIBUTE,
  TS_ADDCONSTRAINT,
  TS_DROPCONSTRAINT,
  TS_ADDSUPER,
  TS_DROPSUPER,
  TS_GETSUPERCLASSESINFO,
  TS_ADDRESOLUTION,
  TS_DROPRESOLUTION,
  TS_ADDMETHOD,
  TS_DROPMETHOD,
  TS_UPDATEMETHOD,
  TS_ADDMETHODFILE,
  TS_DROPMETHODFILE,
  TS_ADDQUERYSPEC,
  TS_DROPQUERYSPEC,
  TS_CHANGEQUERYSPEC,
  TS_VALIDATEQUERYSPEC,
  TS_VALIDATEVCLASS,
  TS_COPYDB,
  TS_OPTIMIZEDB,
  TS_CHECKDB,
  TS_COMPACTDB,
  TS_BACKUPDBINFO,
  TS_BACKUPDB,
  TS_UNLOADDB,
  TS_UNLOADDBINFO,
  TS_LOADDB,
  TS_GETTRANINFO,
  TS_KILLTRAN,
  TS_LOCKDB,
  TS_GETBACKUPLIST,
  TS_RESTOREDB,
  TS_BACKUPVOLINFO,
  TS_GETDBSIZE,
  TS_GETDBMTUSERINFO,
  TS_DELETEDBMTUSER,
  TS_UPDATEDBMTUSER,
  TS_SETDBMTPASSWD,
  TS_ADDDBMTUSER,
  TS_GETBACKUPINFO,
  TS_ADDBACKUPINFO,
  TS_DELETEBACKUPINFO,
  TS_SETBACKUPINFO,
  TS_GETDBERROR,
  TS_GETAUTOADDVOL,
  TS_SETAUTOADDVOL,
  TS_GENERALDBINFO,
  TS_LOADACCESSLOG,
  TS_DELACCESSLOG,
  TS_DELERRORLOG,
  TS_CHECKDIR,
  TS_AUTOBACKUPDBERRLOG,
  TS_KILL_PROCESS,
  TS_GETENV,
  TS_GETACCESSRIGHT,
  TS_GETADDVOLSTATUS,
  TS_GETHISTORY,
  TS_SETHISTORY,
  TS_GETHISTORYFILELIST,
  TS_READHISTORYFILE,
  TS_CHECKAUTHORITY,
  TS_GETAUTOADDVOLLOG,
  TS2_GETINITUNICASINFO,
  TS2_GETUNICASINFO,
  TS2_STARTUNICAS,
  TS2_STOPUNICAS,
  TS2_GETADMINLOGINFO,
  TS2_GETLOGFILEINFO,
  TS2_ADDBROKER,
  TS2_GETADDBROKERINFO,
  TS2_DELETEBROKER,
  TS2_RENAMEBROKER,
  TS2_GETBROKERSTATUS,
  TS2_GETBROKERCONF,
  TS2_GETBROKERONCONF,
  TS2_SETBROKERCONF,
  TS2_SETBROKERONCONF,
  TS2_STARTBROKER,
  TS2_STOPBROKER,
  TS2_SUSPENDBROKER,
  TS2_RESUMEBROKER,
  TS2_BROKERJOBFIRST,
  TS2_BROKERJOBINFO,
  TS2_ADDBROKERAS,
  TS2_DROPBROKERAS,
  TS2_RESTARTBROKERAS,
  TS2_GETBROKERSTATUSLOG,
  TS2_GETBROKERMCONF,
  TS2_SETBROKERMCONF,
  TS2_GETBROKERASLIMIT,
  TS2_GETBROKERENVINFO,
  TS2_SETBROKERENVINFO,
  TS2_ACCESSLISTADDIP,
  TS2_ACCESSLISTDELETEIP,
  TS2_ACCESSLISTINFO,
  TS_CHECKFILE,
  TS_REGISTERLOCALDB,
  TS_REMOVELOCALDB,
  TS_ADDNEWTRIGGER,
  TS_ALTERTRIGGER,
  TS_DROPTRIGGER,
  TS_GETTRIGGERINFO,
  TS_GETFILE,
  TS_GETAUTOEXECQUERY,
  TS_SETAUTOEXECQUERY,
#ifdef DIAG_DEVEL
  TS_GETDIAGINFO,
  TS_GET_DIAGDATA,
  TS_ADDSTATUSTEMPLATE,
  TS_REMOVESTATUSTEMPLATE,
  TS_UPDATESTATUSTEMPLATE,
#if 0				/* ACTIVITY_PROFILE */
  TS_ADDACTIVITYTEMPLATE,
  TS_REMOVEACTIVITYTEMPLATE,
  TS_UPDATEACTIVITYTEMPLATE,
  TS_GETACTIVITYTEMPLATE,
#endif
  TS_GETSTATUSTEMPLATE,
  TS_GETCASLOGFILELIST,
  TS_ANALYZECASLOG,
  TS_EXECUTECASRUNNER,
  TS_REMOVECASRUNNERTMPFILE,
  TS_GETCASLOGTOPRESULT,
#if 0				/* ACTIVITY_PROFILE */
  TS_ADDACTIVITYLOG,
  TS_UPDATEACTIVITYLOG,
  TS_REMOVEACTIVITYLOG,
  TS_GETACTIVITYLOG,
#endif
#endif
#if 0
  TS2_BROKERALARMMSG,
  TS2_GETBROKERPARAM,
  TS_GETDBDIR,
  TS_GETSYSPARAM,
  TS2_GETUNICASDCONF,
  TS2_GETUNICASDINFO,
  TS_GETUSERINFO,
  TS_POPSPACEINFO,
  TS_SETSYSPARAM2,
  TS2_SETUNICASDCONF,
  TS2_STARTUNICASD,
  TS2_STOPUNICASD,
#endif
  TS_DBMTUSERLOGIN,
  TS_CHANGEOWNER,
  TS_REMOVE_LOG,
} T_TASK_CODE;

typedef enum
{
  FSVR_NONE,
  FSVR_SA,
  FSVR_CS,
  FSVR_SA_CS,
  FSVR_UC,
  FSVR_SA_UC
} T_FSVR_TYPE;

typedef int (*T_TASK_FUNC) (nvplist * req, nvplist * res, char *_dbmt_error);

typedef struct
{
  char *task_str;
  int task_code;
  char access_log_flag;
  T_TASK_FUNC task_func;
  T_FSVR_TYPE fsvr_type;
} T_FSERVER_TASK_INFO;


int _tsReadUserCapability (nvplist * ud, char *id, char *err_msg);

int ts_create_class (nvplist * in, nvplist * out, char *_dbmt_error);
int ts_create_vclass (nvplist * in, nvplist * out, char *_dbmt_error);
int ts_userinfo (nvplist * in, nvplist * out, char *_dbmt_error);
int ts_create_user (nvplist * req, nvplist * res, char *_dbmt_error);
int ts_delete_user (nvplist * req, nvplist * res, char *_dbmt_error);
int ts_update_user (nvplist * req, nvplist * res, char *_dbmt_error);
int ts_check_authority (nvplist * in, nvplist * out, char *_dbmt_error);
int ts_class_info (nvplist * in, nvplist * out, char *_dbmt_error);
int ts_class (nvplist * in, nvplist * out, char *_dbmt_error);
int ts_rename_class (nvplist * in, nvplist * out, char *_dbmt_error);
int ts_drop_class (nvplist * in, nvplist * out, char *_dbmt_error);
int ts_add_attribute (nvplist * in, nvplist * out, char *_dbmt_error);
int ts_drop_attribute (nvplist * in, nvplist * out, char *_dbmt_error);
int ts_update_attribute (nvplist * in, nvplist * out, char *_dbmt_error);
int ts_add_constraint (nvplist * in, nvplist * out, char *_dbmt_error);
int ts_drop_constraint (nvplist * in, nvplist * out, char *_dbmt_error);
int ts_add_super (nvplist * in, nvplist * out, char *_dbmt_error);
int ts_drop_super (nvplist * in, nvplist * out, char *_dbmt_error);
int ts_get_superclasses_info (nvplist * in, nvplist * out, char *_dbmt_error);
int ts_add_resolution (nvplist * in, nvplist * out, char *_dbmt_error);
int ts_drop_resolution (nvplist * in, nvplist * out, char *_dbmt_error);
int ts_add_method (nvplist * in, nvplist * out, char *_dbmt_error);
int ts_drop_method (nvplist * in, nvplist * out, char *_dbmt_error);
int ts_update_method (nvplist * in, nvplist * out, char *_dbmt_error);
int ts_add_method_file (nvplist * in, nvplist * out, char *_dbmt_error);
int ts_drop_method_file (nvplist * in, nvplist * out, char *_dbmt_error);
int ts_add_query_spec (nvplist * in, nvplist * out, char *_dbmt_error);
int ts_drop_query_spec (nvplist * in, nvplist * out, char *_dbmt_error);
int ts_change_query_spec (nvplist * in, nvplist * out, char *_dbmt_error);
int ts_validate_query_spec (nvplist * in, nvplist * out, char *_dbmt_error);
int ts_validate_vclass (nvplist * in, nvplist * out, char *_dbmt_error);
int ts2_get_unicas_info (nvplist * in, nvplist * out, char *_dbmt_error);
int ts2_start_unicas (nvplist * in, nvplist * out, char *_dbmt_error);
int ts2_stop_unicas (nvplist * in, nvplist * out, char *_dbmt_error);
int ts2_get_admin_log_info (nvplist * in, nvplist * out, char *_dbmt_error);
int ts2_get_logfile_info (nvplist * in, nvplist * out, char *_dbmt_error);
int ts2_add_broker (nvplist * in, nvplist * out, char *_dbmt_error);
int ts2_get_add_broker_info (nvplist * in, nvplist * out, char *_dbmt_error);
int ts2_get_broker_on_conf (nvplist * in, nvplist * out, char *_dbmt_error);
int ts2_delete_broker (nvplist * in, nvplist * out, char *_dbmt_error);
int ts2_rename_broker (nvplist * in, nvplist * out, char *_dbmt_error);
int ts2_get_broker_status (nvplist * in, nvplist * out, char *_dbmt_error);
int ts2_get_broker_conf (nvplist * in, nvplist * out, char *_dbmt_error);
int ts2_set_broker_conf (nvplist * in, nvplist * out, char *_dbmt_error);
int ts2_set_broker_on_conf (nvplist * in, nvplist * out, char *_dbmt_error);
int ts2_start_broker (nvplist * in, nvplist * out, char *_dbmt_error);
int ts2_stop_broker (nvplist * in, nvplist * out, char *_dbmt_error);
int ts2_suspend_broker (nvplist * in, nvplist * out, char *_dbmt_error);
int ts2_resume_broker (nvplist * in, nvplist * out, char *_dbmt_error);
int ts2_broker_job_first (nvplist * in, nvplist * out, char *_dbmt_error);
int ts2_broker_job_info (nvplist * in, nvplist * out, char *_dbmt_error);
int ts2_add_broker_as (nvplist * in, nvplist * out, char *_dbmt_error);
int ts2_drop_broker_as (nvplist * in, nvplist * out, char *_dbmt_error);
int ts2_restart_broker_as (nvplist * in, nvplist * out, char *_dbmt_error);
int ts2_get_broker_status_log (nvplist * in, nvplist * out,
			       char *_dbmt_error);
int ts2_get_broker_m_conf (nvplist * in, nvplist * out, char *_dbmt_error);
int ts2_set_broker_m_conf (nvplist * in, nvplist * out, char *_dbmt_error);
int ts2_get_broker_as_limit (nvplist * in, nvplist * out, char *_dbmt_error);
int ts2_get_broker_env_info (nvplist * in, nvplist * out, char *_dbmt_error);
int ts2_set_broker_env_info (nvplist * in, nvplist * out, char *_dbmt_error);
int ts2_access_list_add_ip (nvplist * in, nvplist * out, char *_dbmt_error);
int ts2_access_list_delete_ip (nvplist * in, nvplist * out,
			       char *_dbmt_error);
int ts2_access_list_info (nvplist * in, nvplist * out, char *_dbmt_error);
int ts_set_sysparam (nvplist * req, nvplist * res, char *_dbmt_error);
int ts_get_all_sysparam (nvplist * req, nvplist * res, char *_dbmt_error);
int tsCreateDBMTUser (nvplist * req, nvplist * res, char *_dbmt_error);
int tsDeleteDBMTUser (nvplist * req, nvplist * res, char *_dbmt_error);
int tsUpdateDBMTUser (nvplist * req, nvplist * res, char *_dbmt_error);
int tsChangeDBMTUserPasswd (nvplist * req, nvplist * res, char *_dbmt_error);
int tsGetDBMTUserInfo (nvplist * req, nvplist * res, char *_dbmt_error);
int tsCreateDB (nvplist * req, nvplist * res, char *_dbmt_error);
int tsDeleteDB (nvplist * req, nvplist * res, char *_dbmt_error);
int tsRenameDB (nvplist * req, nvplist * res, char *_dbmt_error);
int tsStartDB (nvplist * req, nvplist * res, char *_dbmt_error);
int tsStopDB (nvplist * req, nvplist * res, char *_dbmt_error);
int tsDbspaceInfo (nvplist * req, nvplist * res, char *_dbmt_error);
int tsRunAddvoldb (nvplist * req, nvplist * res, char *_dbmt_error);
int ts_copydb (nvplist * req, nvplist * res, char *_dbmt_error);
int ts_optimizedb (nvplist * req, nvplist * res, char *_dbmt_error);
int ts_checkdb (nvplist * req, nvplist * res, char *_dbmt_error);
int ts_compactdb (nvplist * req, nvplist * res, char *_dbmt_error);
int ts_backupdb (nvplist * req, nvplist * res, char *_dbmt_error);
int ts_unloaddb (nvplist * req, nvplist * res, char *_dbmt_error);
int ts_loaddb (nvplist * req, nvplist * res, char *_dbmt_error);
int ts_restoredb (nvplist * req, nvplist * res, char *_dbmt_error);
int ts_backup_vol_info (nvplist * req, nvplist * res, char *_dbmt_error);
int ts_get_dbsize (nvplist * req, nvplist * res, char *_dbmt_error);
int ts_get_access_right (nvplist * req, nvplist * res, char *_dbmt_error);
int tsGetHistory (nvplist * req, nvplist * res, char *_dbmt_error);
int tsSetHistory (nvplist * req, nvplist * res, char *_dbmt_error);
int tsGetHistoryFileList (nvplist * req, nvplist * res, char *_dbmt_error);
int tsReadHistoryFile (nvplist * req, nvplist * res, char *_dbmt_error);
int tsGetEnvironment (nvplist * req, nvplist * res, char *_dbmt_error);
int ts_startinfo (nvplist * req, nvplist * res, char *_dbmt_error);
int ts_kill_process (nvplist * req, nvplist * res, char *_dbmt_error);
int ts_backupdb_info (nvplist * req, nvplist * res, char *_dbmt_error);
int ts_unloaddb_info (nvplist * req, nvplist * res, char *_dbmt_error);
int ts_get_backup_info (nvplist * req, nvplist * res, char *_dbmt_error);
int ts_set_backup_info (nvplist * req, nvplist * res, char *_dbmt_error);
int ts_add_backup_info (nvplist * req, nvplist * res, char *_dbmt_error);
int ts_delete_backup_info (nvplist * req, nvplist * res, char *_dbmt_error);
int ts_get_log_info (nvplist * req, nvplist * res, char *_dbmt_error);
int ts_view_log (nvplist * req, nvplist * res, char *_dbmt_error);
int ts_reset_log (nvplist * req, nvplist * res, char *_dbmt_error);
int ts_get_db_error (nvplist * req, nvplist * res, char *_dbmt_error);
int ts_get_autostart_db (nvplist * req, nvplist * res, char *_dbmt_error);
int ts_set_autostart_db (nvplist * req, nvplist * res, char *_dbmt_error);
int ts_general_db_info (nvplist * req, nvplist * res, char *_dbmt_error);
int ts_get_auto_add_vol (nvplist * req, nvplist * res, char *_dbmt_error);
int ts_set_auto_add_vol (nvplist * req, nvplist * res, char *_dbmt_error);
int ts_get_addvol_status (nvplist * req, nvplist * res, char *_dbmt_error);
int ts_get_tran_info (nvplist * req, nvplist * res, char *_dbmt_error);
int ts_killtran (nvplist * req, nvplist * res, char *_dbmt_error);
int ts_lockdb (nvplist * req, nvplist * res, char *_dbmt_error);
int ts_get_backup_list (nvplist * req, nvplist * res, char *_dbmt_error);
int ts_load_access_log (nvplist * req, nvplist * res, char *_dbmt_error);
int ts_delete_access_log (nvplist * req, nvplist * res, char *_dbmt_error);
int ts_delete_error_log (nvplist * req, nvplist * res, char *_dbmt_error);
int ts_check_dir (nvplist * req, nvplist * res, char *_dbmt_error);
int ts_get_autobackupdb_error_log (nvplist * req, nvplist * res,
				   char *_dbmt_error);
int tsGetAutoaddvolLog (nvplist * req, nvplist * res, char *_dbmt_error);
int ts_check_file (nvplist * req, nvplist * res, char *_dbmt_error);
int ts_localdb_operation (nvplist * req, nvplist * res, char *_dbmt_error);
int ts_trigger_operation (nvplist * req, nvplist * res, char *_dbmt_error);
int ts_get_triggerinfo (nvplist * req, nvplist * res, char *_dbmt_error);
int ts_get_file (nvplist * req, nvplist * res, char *_dbmt_error);
int ts_set_autoexec_query (nvplist * req, nvplist * res, char *_dbmt_error);
int ts_get_autoexec_query (nvplist * req, nvplist * res, char *_dbmt_error);
#ifdef DIAG_DEVEL
int ts_get_diaginfo (nvplist * req, nvplist * res, char *_dbmt_error);
int ts_get_diagdata (nvplist * req, nvplist * res, char *diag_error);
int ts_addstatustemplate (nvplist * req, nvplist * res, char *diag_error);
int ts_removestatustemplate (nvplist * req, nvplist * res, char *diag_error);
int ts_updatestatustemplate (nvplist * req, nvplist * res, char *diag_error);
int ts_getstatustemplate (nvplist * req, nvplist * res, char *diag_error);
#if 0				/* ACTIVITY_PROFILE */
int ts_addactivitytemplate (nvplist * req, nvplist * res, char *diag_error);
int ts_removeactivitytemplate (nvplist * req, nvplist * res,
			       char *diag_error);
int ts_updateactivitytemplate (nvplist * req, nvplist * res,
			       char *diag_error);
int ts_getactivitytemplate (nvplist * req, nvplist * res, char *diag_error);
#endif
int ts_getcaslogfilelist (nvplist * req, nvplist * res, char *diag_error);
int ts_analyzecaslog (nvplist * req, nvplist * res, char *diag_error);
int ts_executecasrunner (nvplist * req, nvplist * res, char *diag_error);
int ts_removecasrunnertmpfile (nvplist * req, nvplist * res,
			       char *diag_error);
int ts_getcaslogtopresult (nvplist * cli_request, nvplist * cli_response,
			   char *diag_error);
#if 0				/* ACTIVITY_PROFILE */
int ts_addactivitylog (nvplist * cli_request, nvplist * cli_response,
		       char *diag_error);
int ts_updateactivitylog (nvplist * cli_request, nvplist * cli_response,
			  char *diag_error);
int ts_removeactivitylog (nvplist * cli_request, nvplist * cli_response,
			  char *diag_error);
int ts_getactivitylog (nvplist * cli_request, nvplist * cli_response,
		       char *diag_error);
#endif
#endif
int ts_get_ldb_class_att (nvplist * req, nvplist * res, char *_dbmt_error);
int tsDBMTUserLogin (nvplist * req, nvplist * res, char *_dbmt_error);
int ts_change_owner (nvplist * in, nvplist * out, char *_dbmt_error);
int ts_remove_log (nvplist * in, nvplist * out, char *_dbmt_error);

#endif /* _FSERVER_TASK_H_ */
