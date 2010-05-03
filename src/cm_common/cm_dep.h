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
 * cm_dep.h - 
 */

#ifndef _CM_DEP_H_
#define _CM_DEP_H_

#ifdef __cplusplus
extern "C"
{
#endif


#include <time.h>

#if !defined(WINDOWS)
#include <sys/types.h>
#include <sys/param.h>
#else
  typedef __int64 int64_t;
#endif

#define EMGR_CUR_VERSION    EMGR_MAKE_VER(3, 0)
#define EMGR_MAKE_VER(MAJOR, MINOR) \
        ((T_EMGR_VERSION) (((MAJOR) << 8) | (MINOR)))
  typedef short T_EMGR_VERSION;


  typedef struct dstring_t
  {
    int dsize;			/* allocated dbuf size */
    int dlen;			/* string length stored in dbuf */
    char *dbuf;
  } dstring;

  dstring *dst_create (void);
  void dst_destroy (dstring * dstr);
  void dst_reset (dstring * dstr);
  int dst_append (dstring * dstr, const char *str, int slen);
  int dst_length (dstring * dstr);
  int dst_size (dstring * dstr);
  char *dst_buffer (dstring * dstr);


#define NV_ADD_DATE		0
#define NV_ADD_TIME		1
#define NV_ADD_DATE_TIME	2

  typedef struct nvp_t
  {
    dstring *name;
    dstring *value;
  } nvpair;

  typedef struct nvplist_t
  {
    nvpair **nvpairs;
    int nvplist_size;		/* allocated list length */
    int nvplist_leng;		/* number of valid nvpairs in nvp_list */

    dstring *listopener;
    dstring *listcloser;

    dstring *delimiter;		/* ":"  */
    dstring *endmarker;		/* "\n" */
  } nvplist;

/* user interface functions */
  nvplist *nv_create (int defsize, const char *lom, const char *lcm,
		      const char *dm, const char *em);
  int nv_init (nvplist * ref, int defsize, const char *lom, const char *lcm,
	       const char *dm, const char *em);
  int nv_lookup (nvplist * ref, int index, char **name, char **value);
  int nv_locate (nvplist * ref, const char *marker, int *index, int *ilen);
  char *nv_get_val (nvplist * ref, const char *name);
  int nv_update_val (nvplist * ref, const char *name, const char *value);
  int nv_update_val_int (nvplist * ref, const char *name, int value);
  void nv_destroy (nvplist * ref);
  int nv_writeto (nvplist * ref, char *filename);
  void nv_reset_nvp (nvplist * ref);
  int nv_add_nvp (nvplist * ref, const char *name, const char *value);
  int nv_add_nvp_int64 (nvplist * ref, const char *name, int64_t value);
  int nv_add_nvp_int (nvplist * ref, const char *name, int value);
  int nv_add_nvp_time (nvplist * ref, const char *name, time_t t,
		       const char *fmt, int flat);
  int nv_add_nvp_float (nvplist * ref, const char *name, float value,
			const char *fmt);
  int nv_readfrom (nvplist * ref, char *filename);

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
#define ERR_TEMPLATE_ALREADY_EXIST 1340
#define ERR_WARNING     1350


  typedef enum
  {
    DB_SERVICE_MODE_NONE = 0,
    DB_SERVICE_MODE_SA = 1,
    DB_SERVICE_MODE_CS = 2
  } T_DB_SERVICE_MODE;



  typedef struct
  {
    char db_name[64];
    unsigned int ha_mode;
  } T_SERVER_STATUS_INFO;

#define ERR_MSG_SIZE       1024
  typedef struct
  {
    int num_result;
    void *result;
    char err_msg[ERR_MSG_SIZE];
  } T_CMD_RESULT;

  typedef T_CMD_RESULT T_SERVER_STATUS_RESULT;

  T_SERVER_STATUS_RESULT *cmd_server_status (void);
#define cmd_servstat_result_free(RESULT)           cmd_result_free(RESULT)
  void cmd_result_free (T_CMD_RESULT * res);

  typedef struct
  {
    int64_t query_open_page;
    int64_t query_opened_page;
    int64_t query_slow_query;
    int64_t query_full_scan;
    int64_t conn_cli_request;
    int64_t conn_aborted_clients;
    int64_t conn_conn_req;
    int64_t conn_conn_reject;
    int64_t buffer_page_write;
    int64_t buffer_page_read;
    int64_t lock_deadlock;
    int64_t lock_request;
  } T_CM_DIAG_MONITOR_DB_VALUE;

  int cm_get_diag_data (T_CM_DIAG_MONITOR_DB_VALUE * ret_result,
			char *db_name, char *mon_db);

  int cm_tsDBMTUserLogin (nvplist * in, nvplist * out, char *_dbmt_error);
  int cm_ts_optimizedb (nvplist * req, nvplist * res, char *_dbmt_error);
  int cm_ts_class_info (nvplist * in, nvplist * out, char *_dbmt_error);
  int cm_ts_class (nvplist * in, nvplist * out, char *_dbmt_error);
  int cm_ts_get_triggerinfo (nvplist * req, nvplist * res, char *_dbmt_error);
  int cm_ts_update_attribute (nvplist * in, nvplist * out, char *_dbmt_error);
  int cm_ts_userinfo (nvplist * in, nvplist * out, char *_dbmt_error);
  int cm_ts_create_user (nvplist * req, nvplist * res, char *_dbmt_error);
  int cm_ts_update_user (nvplist * req, nvplist * res, char *_dbmt_error);
  int cm_ts_delete_user (nvplist * req, nvplist * res, char *_dbmt_error);

  T_DB_SERVICE_MODE uDatabaseMode (char *dbname, int *ha_mode);

  int uIsDatabaseActive (char *dbn);
  int uIsDatabaseActive2 (T_SERVER_STATUS_RESULT * cmd_res, char *dbn);
  int uRetrieveDBLogDirectory (char *dbname, char *target);
  int uReadDBtxtFile (const char *dn, int idx, char *outbuf);
  int get_db_server_pid (char *dbname);
  int run_child (const char *const argv[], int wait_flag,
		 const char *stdin_file, char *stdout_file, char *stderr_file,
		 int *exit_status);

#ifdef __cplusplus
}
#endif



#endif				/* _CM_DEP_H_ */
