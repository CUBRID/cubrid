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
 * perf_diag.h - diagnostics something
 */

#ifndef _PERF_DIAG_H_
#define _PERF_DIAG_H_

#ident "$Id$"

#include <stdio.h>
#include "system.h"

/* TODO: do separate server and client functionalities... they have little to nothing in common (especially now that
 *       metadata was separated. */

/* todo: what is DIAG/DIAG_DEVEL?? */

/* todo: cm refactoring ==> */
#define MAX_SERVER_THREAD_COUNT         500
/* todo: cm refactoring <== */
#define MAX_SERVER_NAMELENGTH           256
#define SH_MODE 0644

#if defined (DIAG_DEVEL)
#if defined(SERVER_MODE)

typedef enum t_diag_obj_type T_DIAG_OBJ_TYPE;
enum t_diag_obj_type
{
  DIAG_OBJ_TYPE_QUERY_OPEN_PAGE = 0,
  DIAG_OBJ_TYPE_QUERY_OPENED_PAGE = 1,
  DIAG_OBJ_TYPE_QUERY_SLOW_QUERY = 2,
  DIAG_OBJ_TYPE_QUERY_FULL_SCAN = 3,
  DIAG_OBJ_TYPE_CONN_CLI_REQUEST = 4,
  DIAG_OBJ_TYPE_CONN_ABORTED_CLIENTS = 5,
  DIAG_OBJ_TYPE_CONN_CONN_REQ = 6,
  DIAG_OBJ_TYPE_CONN_CONN_REJECT = 7,
  DIAG_OBJ_TYPE_BUFFER_PAGE_READ = 8,
  DIAG_OBJ_TYPE_BUFFER_PAGE_WRITE = 9,
  DIAG_OBJ_TYPE_LOCK_DEADLOCK = 10,
  DIAG_OBJ_TYPE_LOCK_REQUEST = 11
};

typedef enum t_diag_value_settype T_DIAG_VALUE_SETTYPE;
enum t_diag_value_settype
{
  DIAG_VAL_SETTYPE_INC,
  DIAG_VAL_SETTYPE_DEC,
  DIAG_VAL_SETTYPE_SET
};

typedef int (*T_DO_FUNC) (int value, T_DIAG_VALUE_SETTYPE settype, char *err_buf);

typedef struct t_diag_object_table T_DIAG_OBJECT_TABLE;
struct t_diag_object_table
{
  char typestring[32];
  T_DIAG_OBJ_TYPE type;
  T_DO_FUNC func;
};

/* MACRO definition */
#define SET_DIAG_VALUE(DIAG_EXEC_FLAG, ITEM_TYPE, VALUE, SET_TYPE, ERR_BUF)          \
    do {                                                                             \
        if (DIAG_EXEC_FLAG == true) {                                             \
            set_diag_value(ITEM_TYPE , VALUE, SET_TYPE, ERR_BUF);                    \
        }                                                                            \
    } while(0)

#define DIAG_GET_TIME(DIAG_EXEC_FLAG, TIMER)                                         \
    do {                                                                             \
        if (DIAG_EXEC_FLAG == true) {                                             \
            gettimeofday(&TIMER, NULL);                                              \
        }                                                                            \
    } while(0)

#define SET_DIAG_VALUE_SLOW_QUERY(DIAG_EXEC_FLAG, START_TIME, END_TIME, VALUE, SET_TYPE, ERR_BUF)\
    do {                                                                                 \
        if (DIAG_EXEC_FLAG == true) {                                                 \
            struct timeval result = {0,0};                                               \
            ADD_TIMEVAL(result, START_TIME, END_TIME);                                   \
            if (result.tv_sec >= diag_long_query_time)                                   \
                set_diag_value(DIAG_OBJ_TYPE_QUERY_SLOW_QUERY, VALUE, SET_TYPE, ERR_BUF);\
        }                                                                                \
    } while(0)

#define SET_DIAG_VALUE_FULL_SCAN(DIAG_EXEC_FLAG, VALUE, SET_TYPE, ERR_BUF, XASL, SPECP) \
    do {                                                                                \
        if (DIAG_EXEC_FLAG == true) {                                                \
            if (((XASL_TYPE(XASL) == BUILDLIST_PROC) ||                                 \
                 (XASL_TYPE(XASL) == BUILDVALUE_PROC))                                  \
                && ACCESS_SPEC_ACCESS(SPECP) == SEQUENTIAL) {                           \
                set_diag_value(DIAG_OBJ_TYPE_QUERY_FULL_SCAN                            \
                        , 1                                                             \
                        , DIAG_VAL_SETTYPE_INC                                          \
                        , NULL);                                                        \
            }                                                                           \
        }                                                                               \
    } while(0)

extern int diag_long_query_time;
extern bool diag_executediag;

extern bool init_diag_mgr (const char *server_name, int num_thread, char *err_buf);
extern void close_diag_mgr (void);
extern bool set_diag_value (T_DIAG_OBJ_TYPE type, int value, T_DIAG_VALUE_SETTYPE settype, char *err_buf);
#endif /* SERVER_MODE */
#endif /* DIAG_DEVEL */

typedef struct diag_sys_config DIAG_SYS_CONFIG;
struct diag_sys_config
{
  int Executediag;
  int DiagSM_ID_server;
  int server_long_query_time;	/* min 1 sec */
};

typedef struct t_diag_monitor_db_value T_DIAG_MONITOR_DB_VALUE;
struct t_diag_monitor_db_value
{
  INT64 query_open_page;
  INT64 query_opened_page;
  INT64 query_slow_query;
  INT64 query_full_scan;
  INT64 conn_cli_request;
  INT64 conn_aborted_clients;
  INT64 conn_conn_req;
  INT64 conn_conn_reject;
  INT64 buffer_page_write;
  INT64 buffer_page_read;
  INT64 lock_deadlock;
  INT64 lock_request;
};

/* Shared memory data struct */

typedef struct t_shm_diag_info_server T_SHM_DIAG_INFO_SERVER;
struct t_shm_diag_info_server
{
  int magic;
  int num_thread;
  int magic_key;
  char servername[MAX_SERVER_NAMELENGTH];
  T_DIAG_MONITOR_DB_VALUE thread[MAX_SERVER_THREAD_COUNT];
};

#endif /* _PERF_DIAG_H_ */
