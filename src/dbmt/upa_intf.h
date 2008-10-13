/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * upa_intf.h - 
 */

#ifndef	_UPA_INTF_H_
#define	_UPA_INTF_H_

#ident "$Id$"

/*
 * IMPORTED SYSTEM HEADER FILES
 */

#include <time.h>

/*
 * IMPORTED OTHER HEADER FILES
 */

/*
 * EXPORTED DEFINITIONS
 */

#define VIRTUAL_SECURITY_DB "security"
#define VIRTUAL_SECURITY_USER "manager"
#define APID_MANAGEMENT_TOOL "cubridmanager"
#define APID_ANONYMOUS "anonymous"

#define UPA_MAJOR_VERSION	1
#define UPA_MINOR_VERSION	0
#define UPA_PATCH_VERSION	0

#define UPA_USER_DN_MAX_SIZE	512

#define UPA_CLT_TYPE_APPL       	'0'
#define UPA_CLT_TYPE_TOOL       	'1'
#define UPA_CLT_TYPE_NOT_DEFINED	0

/*
 * EXPORTED TYPE DEFINITIONS
 */

typedef struct
{
  char *apid;
  char aptype;
  char *reg_date;
  char *reg_dn;
  char *note;
} T_UPA_APP_INFO;

typedef struct
{
  char *apid;
  char *user_dn;
  char *dbname;
  char *dbuser;
  char *reg_date;
  char *exp_date;
  char *reg_dn;
  char *note;
} T_UPA_USER_INFO;

typedef struct
{
  char *dn;
  char *id;
  time_t reg_date;
  time_t exp_date;
  char *dbname;
  char *name;
  char *note;
} T_UPA_MT_INFO;

typedef void *T_UPA_USER_QUERY_RESULT;
typedef void *T_UPA_MT_QUERY_RESULT;

typedef enum
{
  UPA_USER_DEL_CMD_MIN = 0,
  UPA_USER_DEL_APID_DN_DBNAME = 0,
  UPA_USER_DEL_ALL = 1,
  UPA_USER_DEL_EXPIRE_BEFORE = 2,
  UPA_USER_DEL_EXPIRE_AFTER = 3,
  UPA_USER_DEL_CMD_MAX = 3
} T_UPA_USER_DEL_CMD;

typedef enum
{
  UPA_USER_APID_CMD_MIN = 0,
  UPA_USER_APID_INFO = 0,
  UPA_USER_APID_ADD = 1,
  UPA_USER_APID_DEL = 2,
  UPA_USER_APID_CMD_MAX = 2
} T_UPA_USER_APID_CMD;

typedef enum
{
  UPA_USER_LOG_CMD_MIN = 0,
  UPA_USER_LOG_FILES = 0,
  UPA_USER_LOG_TEXT = 1,
  UPA_USER_LOG_CONF = 2,
  UPA_USER_LOG_CMD_MAX = 2
} T_UPA_USER_LOG_CMD;

typedef struct
{
  char con_key[512];
} T_UPA_KEY;

typedef void *T_UPA_DB_INFO;

typedef struct
{
  char id[64];
  char passwd[64];
  char name[128];
  char dn[512];
  char exp_date[64];
} T_MT_REG_RESULT;

typedef enum
{
  UPA_ER_COMMUNICATION = -2001,
  UPA_ER_VERSION = -2002,
  UPA_ER_NO_MORE_MEMORY = -2003,
  UPA_ER_ARGS = -2004,
  UPA_ER_AUTH = -2005,
  UPA_ER_MT_REG = -2006,
  UPA_ER_FN_CODE = -2007,
  UPA_ER_NOT_SM = -2008,
  UPA_ER_SM_FILE = -2009,
  UPA_ER_RETRIEVE_CERT = -2010,
  UPA_ER_VALIDATE_CERT = -2011,
  UPA_ER_INVALID_KEY = -2012,
  UPA_ER_TMP_FILE = -2013,
  UPA_ER_DBMS = -2014,
  UPA_ER_USER_EXIST = -2015,
  UPA_ER_INVALID_ID_PASSWD = -2016,

  UPA_ER_CONNECT = -3001,
  UPA_ER_NO_MORE_MEMORY_C = -3002,
  UPA_ER_COMMUNICATION_C = -3003,
  UPA_ER_PID_NOT_ALLOWED = -3004,
  UPA_ER_TMP_FILE_C = -3005,
  UPA_ER_ADM_INFO = -3006,
} T_UPA_ERROR;

/*
 * EXPORTED FUNCTION PROTOTYPES
 */

extern void upa_init (void);
extern int upa_set_ip_port (char *ip, int port);
extern int upa_login (char *signed_data, int data_size, T_UPA_KEY * key);
extern int upa_login_sm (char *signed_data, int data_size, T_UPA_KEY * key);
extern int upa_login_mt (char *signed_data, int data_size, T_UPA_KEY * key);
extern int upa_key_update (T_UPA_KEY * key, char flag);
extern int upa_key_check (T_UPA_KEY * key, char *dbname, int clt_type,
			  T_UPA_DB_INFO * db_info);
extern int upa_get_db_info (T_UPA_DB_INFO db_info, int index, char **dbname,
			    char **dbuser, char **dbpasswd);
extern int upa_free_db_info (T_UPA_DB_INFO db_info);
extern int upa_logout (T_UPA_KEY * key);
extern char *upa_get_key_str (T_UPA_KEY * key);
extern void upa_make_key (char *key_str, T_UPA_KEY * key);
extern void upa_get_user_dn (T_UPA_KEY * key, char *buf, int buf_size);
extern int upa_mt_reg (char *msg, int msg_size, char **res_msg,
		       int *res_msg_size);
extern int upa_mt_reg2 (T_UPA_KEY * key, int exp_hour, char *dbname,
			char *name, char *note,
			T_MT_REG_RESULT * mt_reg_info);
extern int upa_clr_dbinfo (void);
extern void upa_add_pid (void);
extern int upa_temp_dba_passwd (T_UPA_KEY * key, char *dbname, char *passwd);

#ifdef CUBRID_PKI_AUTH_LIB
extern int upa_login_pid (T_UPA_KEY * key);
#endif

extern int upa_adm_user_add (T_UPA_KEY * key, T_UPA_USER_INFO * user_info);
extern int upa_adm_user_add_file (T_UPA_KEY * key, char *filename,
				  int *num_fail,
				  T_UPA_USER_QUERY_RESULT * result);
extern int upa_adm_user_del (T_UPA_KEY * key, T_UPA_USER_DEL_CMD del_cmd,
			     T_UPA_USER_INFO * user_info);
extern int upa_adm_user_info (T_UPA_KEY * key, T_UPA_USER_INFO * user_info,
			      T_UPA_USER_QUERY_RESULT * result);
extern int upa_adm_user_info_next (T_UPA_USER_QUERY_RESULT result,
				   T_UPA_USER_INFO * user_info);
extern int upa_adm_app_cmd (T_UPA_KEY * key, int subcmd,
			    T_UPA_USER_INFO * app_info,
			    T_UPA_USER_QUERY_RESULT * result);
extern int upa_adm_app_info_next (T_UPA_USER_QUERY_RESULT result,
				  T_UPA_APP_INFO * app_info);
extern int upa_adm_log_cmd (T_UPA_KEY * key, int subcmd,
			    T_UPA_USER_INFO * app_info,
			    T_UPA_USER_QUERY_RESULT * result);
extern int upa_adm_log_next (T_UPA_USER_QUERY_RESULT result, int fldcnt,
			     char *logfld[]);
extern int upa_adm_user_query_result_free (T_UPA_USER_QUERY_RESULT result);
extern int upa_adm_mt_info (T_UPA_KEY * key, int include_expired_id,
			    T_UPA_MT_INFO * mt_info,
			    T_UPA_MT_QUERY_RESULT * result);
extern int upa_adm_mt_info_next (T_UPA_MT_QUERY_RESULT result,
				 T_UPA_MT_INFO * mt_info);
extern int upa_adm_mt_query_result_free (T_UPA_MT_QUERY_RESULT result);
extern int upa_adm_change_sm (T_UPA_KEY * key, char *dn);
extern int upa_key_lock (T_UPA_KEY * key);
extern int upa_key_unlock (T_UPA_KEY * key);

/*
 * EXPORTED VARIABLES
 */

#endif /* _UPA_INTF_H_ */
