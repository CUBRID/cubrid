/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * file_name.h - 
 */

#ifndef _FILE_NAME_H_
#define _FILE_NAME_H_

#ident "$Id$"

#include "porting.h"

#define APPL_SERVER_UTS_C_NAME		"vas"
#define APPL_SERVER_UTS_W_NAME		"was"
#define APPL_SERVER_AM_NAME		"ams"
#define APPL_SERVER_CAS_NAME		"cub_cas"
#define APPL_SERVER_UPLOAD_NAME		"uls"
#define APPL_SERVER_CAS_U52_NAME	"cas_u52"

#define NAME_BROKER			"Tbroker"
#define NAME_CAS_BROKER			"cub_broker"
#ifdef WIN32
#define NAME_CAS_BROKER2		"Cbroker2"
#define NAME_UC_SHM			"broker_shm"
#endif

#define CUBRID_CONF_DIR			"conf"
#define CUBRID_TMP_DIR			"tmp"
#define CUBRID_VAR_DIR			"var"
#define CUBRID_SOCK_DIR			"var/CUBRID_SOCK"
#define CUBRID_ASPID_DIR		"var/as_pid"
#define CUBRID_LOG_DIR			"log"
#define CUBRID_SQL_LOG_DIR		"sql_log"
#define CUBRID_ERR_DIR		        CUBRID_LOG_DIR

#ifdef DISPATCHER
#define ERROR_MSG_FILE			"uw_er.msg"
#endif

#define SQL_LOG2_DIR			"query"

/* default values */
#define DEFAULT_LOG_DIR			CUBRID_LOG_DIR
#define DEFAULT_DOC_ROOT_DIR		DEFAULT_LOG_DIR
#define DEFAULT_FILE_UPLOAD_TEMP_DIR	CUBRID_TMP_DIR

typedef enum t_cubrid_file_id T_CUBRID_FILE_ID;
enum t_cubrid_file_id
{
  FID_CUBRID_BROKER_CONF,
  FID_UV_ERR_MSG,
  FID_V3_OUTFILE_DIR,
  FID_CAS_TMPGLO_DIR,
  FID_CAS_TMP_DIR,
  FID_SOCK_DIR,
  FID_AS_PID_DIR,
  FID_ADMIND_PID,
  FID_SQL_LOG_DIR,
  FID_ADMIND_LOG,
  FID_MONITORD_LOG,
  FID_ER_HTML,
  FID_CUBRID_ERR_DIR
};

typedef struct t_cubrid_file_info T_CUBRID_FILE_INFO;
struct t_cubrid_file_info
{
  int fid;
  char dir_name[PATH_MAX];
  char file_name[PATH_MAX];
};

extern void set_cubrid_home (void);
extern void set_cubrid_file (T_CUBRID_FILE_ID fid, char *value);
extern char *get_cubrid_file (T_CUBRID_FILE_ID fid, char *buf);
extern char *get_cubrid_home (void);
extern char *getenv_cubrid_broker (void);

#endif /* _FILE_NAME_H_ */
