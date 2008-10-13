/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * cmd_exec.h - 
 */

#ifndef _CMD_EXEC_H_
#define _CMD_EXEC_H_

#ident "$Id$"

#include <time.h>

#include "dbmt_porting.h"

#if defined(WINDOWS)
#define DBMT_EXE_EXT		".exe"
#else
#define DBMT_EXE_EXT		""
#endif

#define cmd_commdb_result_free(RESULT)		cmd_result_free(RESULT)
#define cmd_sqlx_result_free(RESULT)		cmd_result_free(RESULT)

#define ERR_MSG_SIZE	1024

#define CUBRID_ERROR_LOG_DIR		"log"

#define USQL_DATABASE_TXT	"databases.txt"
#define USQL_SQLX_INIT		"cubrid.conf"
#define USQL_UNLOAD_EXT_INDEX	"_indexes"
#define USQL_UNLOAD_EXT_TRIGGER	"_trigger"
#define USQL_UNLOAD_EXT_OBJ	"_objects"
#define USQL_UNLOAD_EXT_SCHEMA	"_schema"
#define USQL_SERVER_LOCK_EXT	"_lgat__lock"
#define USQL_ACT_LOG_EXT	"_lgat"
#define USQL_ARC_LOG_EXT	"_lgar"
#define USQL_BACKUP_INFO_EXT	"_bkvinf"
#define USQL_ARC_LOG_EXT_LEN	strlen(USQL_ARC_LOG_EXT)

#define CUBRID_CMD_NAME_LEN	128

#ifdef WIN32
#define CUBRID_DIR_BIN          "bin\\"
#else
#define CUBRID_DIR_BIN          "bin/"
#endif

typedef enum
{
  CUBRID_MODE_CS = 0,
  CUBRID_MODE_SA = 1
} T_CUBRID_MODE;

typedef struct
{
  int volid;
  int total_page;
  int free_page;
  char purpose[16];
  char location[128];
  char vol_name[128];
  time_t date;
} T_SPACEDB_INFO;

typedef struct
{
  int page_size;
  int num_vol;
  T_SPACEDB_INFO *vol_info;
  int num_tmp_vol;
  T_SPACEDB_INFO *tmp_vol_info;
  char err_msg[ERR_MSG_SIZE];
} T_SPACEDB_RESULT;

typedef struct
{
  char db_name[64];
} T_COMMDB_INFO;

typedef struct
{
  int num_result;
  void *result;
  char err_msg[ERR_MSG_SIZE];
} T_CMD_RESULT;

typedef T_CMD_RESULT T_COMMDB_RESULT;
typedef T_CMD_RESULT T_START_SERVER_RESULT;
typedef T_CMD_RESULT T_STOP_SERVER_RESULT;
typedef T_CMD_RESULT T_SQLX_RESULT;

T_COMMDB_RESULT *cmd_commdb ();
T_SPACEDB_RESULT *cmd_spacedb (char *dbname, T_CUBRID_MODE mode);
T_SQLX_RESULT *cmd_sqlx (char *dbname, char *uid, char *passwd,
			 T_CUBRID_MODE mode, char *infile, char *command);
int cmd_start_server (char *dbname, char *err_buf, int err_buf_size);
int cmd_stop_server (char *dbname, char *err_buf, int err_buf_size);
void cmd_start_master ();

char *cubrid_cmd_name (char *buf);
void cmd_result_free (T_CMD_RESULT * res);
void cmd_spacedb_result_free (T_SPACEDB_RESULT * res);
int read_error_file (char *err_file, char *err_buf, int err_buf_size);
int read_sqlx_error_file (char *err_file, char *err_buf, int err_buf_size);

#endif /* _CMD_EXEC_H_ */
