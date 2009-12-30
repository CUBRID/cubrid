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
 * cm_command_execute.h -
 */

#ifndef _CM_COMMAND_EXECUTE_H_
#define _CM_COMMAND_EXECUTE_H_

#ident "$Id$"

#include <time.h>

#include "cm_porting.h"

#if defined(WINDOWS)
#define DBMT_EXE_EXT		".exe"
#else
#define DBMT_EXE_EXT		""
#endif

#define cmd_commdb_result_free(RESULT)		cmd_result_free(RESULT)
#define cmd_csql_result_free(RESULT)		cmd_result_free(RESULT)

#define ERR_MSG_SIZE	1024

#if !defined (DO_NOT_USE_CUBRIDENV)
#define CUBRID_ERROR_LOG_DIR		"log/server"
#else
#define CUBRID_ERROR_LOG_DIR		CUBRID_LOGDIR "/server"
#endif

#define CUBRID_DATABASE_TXT	"databases.txt"
#define CUBRID_CUBRID_CONF	"cubrid.conf"
#define CUBRID_DBMT_CONF	"cm.conf"
#define CUBRID_BROKER_CONF	"cubrid_broker.conf"
#define CUBRID_UNLOAD_EXT_INDEX	"_indexes"
#define CUBRID_UNLOAD_EXT_TRIGGER	"_trigger"
#define CUBRID_UNLOAD_EXT_OBJ	"_objects"
#define CUBRID_UNLOAD_EXT_SCHEMA	"_schema"
#define CUBRID_SERVER_LOCK_EXT	"_lgat__lock"
#define CUBRID_ACT_LOG_EXT	"_lgat"
#define CUBRID_ARC_LOG_EXT	"_lgar"
#define CUBRID_BACKUP_INFO_EXT	"_bkvinf"
#define CUBRID_ARC_LOG_EXT_LEN	strlen(CUBRID_ARC_LOG_EXT)

#define CUBRID_CMD_NAME_LEN	128

#if !defined (DO_NOT_USE_CUBRIDENV)
#if defined(WINDOWS)
#define CUBRID_DIR_BIN          "bin\\"
#else
#define CUBRID_DIR_BIN          "bin/"
#endif
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
typedef T_CMD_RESULT T_CSQL_RESULT;

T_COMMDB_RESULT *cmd_commdb (void);
T_SPACEDB_RESULT *cmd_spacedb (const char *dbname, T_CUBRID_MODE mode);
T_CSQL_RESULT *cmd_csql (char *dbname, char *uid, char *passwd,
			 T_CUBRID_MODE mode, char *infile, char *command);
int cmd_start_server (char *dbname, char *err_buf, int err_buf_size);
int cmd_stop_server (char *dbname, char *err_buf, int err_buf_size);
void cmd_start_master (void);

char *cubrid_cmd_name (char *buf);
void cmd_result_free (T_CMD_RESULT * res);
void cmd_spacedb_result_free (T_SPACEDB_RESULT * res);
int read_error_file (const char *err_file, char *err_buf, int err_buf_size);
int read_error_file2 (char *err_file, char *err_buf, int err_buf_size,
		      int *err_code);
int read_csql_error_file (char *err_file, char *err_buf, int err_buf_size);
#endif /* _CM_COMMAND_EXECUTE_H_ */
