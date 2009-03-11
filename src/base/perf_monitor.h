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
 * perf_monitor.h - Monitor execution statistics at Client
 */

#ifndef _PERF_MONITOR_H_
#define _PERF_MONITOR_H_

#ident "$Id$"

#include <stdio.h>

#include "memory_alloc.h"

#if defined (SERVER_MODE)
#include "dbtype.h"
#include "connection_defs.h"
#endif /* SERVER_MODE */
#include "thread_impl.h"

/* perf_monitor_1.h */
#include <time.h>
#if !defined(WINDOWS)
#include <sys/time.h>
#endif /* WINDOWS */

/* EXPORTED GLOBAL DEFINITIONS */
#define MAX_DIAG_DATA_VALUE     0xfffffffffffffLL

#define MAX_SERVER_THREAD_COUNT         500
#define MAX_SERVER_NAMELENGTH           256
#define SH_MODE 0644

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
  long long query_open_page;
  long long query_opened_page;
  long long query_slow_query;
  long long query_full_scan;
  long long conn_cli_request;
  long long conn_aborted_clients;
  long long conn_conn_req;
  long long conn_conn_reject;
  long long buffer_page_write;
  long long buffer_page_read;
  long long lock_deadlock;
  long long lock_request;
};

typedef struct t_diag_monitor_cas_value T_DIAG_MONITOR_CAS_VALUE;
struct t_diag_monitor_cas_value
{
  long long reqs_in_interval;
  int active_sessions;
  long long transactions_in_interval;
};

/* Monitor config related structure */

typedef struct monitor_cas_config MONITOR_CAS_CONFIG;
struct monitor_cas_config
{
  char head;
  char body[2];
};

typedef struct monitor_server_config MONITOR_SERVER_CONFIG;
struct monitor_server_config
{
  char head[2];
  char body[8];
};

typedef struct t_client_monitor_config T_CLIENT_MONITOR_CONFIG;
struct t_client_monitor_config
{
  MONITOR_CAS_CONFIG cas;
  MONITOR_SERVER_CONFIG server;
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

enum t_diag_shm_mode
{
  DIAG_SHM_MODE_ADMIN = 0,
  DIAG_SHM_MODE_MONITOR = 1
};
typedef enum t_diag_shm_mode T_DIAG_SHM_MODE;

enum t_diag_server_type
{
  DIAG_SERVER_DB = 00000,
  DIAG_SERVER_CAS = 10000,
  DIAG_SERVER_DRIVER = 20000,
  DIAG_SERVER_RESOURCE = 30000
};
typedef enum t_diag_server_type T_DIAG_SERVER_TYPE;

/* perf_monitor_2.h */
/*
 * Server execution statistic structure
 */
typedef struct mnt_server_exec_stats MNT_SERVER_EXEC_STATS;
struct mnt_server_exec_stats
{
  /* Execution statistics for the page buffer manager */
  unsigned int pb_num_fetches;
  unsigned int pb_num_dirties;
  unsigned int pb_num_ioreads;
  unsigned int pb_num_iowrites;

  /* Execution statistics for the log manager */
  unsigned int log_num_ioreads;
  unsigned int log_num_iowrites;
  unsigned int log_num_appendrecs;
  unsigned int log_num_archives;
  unsigned int log_num_checkpoints;

  /* Execution statistics for the lock manager */
  unsigned int lk_num_acquired_on_pages;
  unsigned int lk_num_acquired_on_objects;
  unsigned int lk_num_converted_on_pages;
  unsigned int lk_num_converted_on_objects;
  unsigned int lk_num_re_requested_on_pages;
  unsigned int lk_num_re_requested_on_objects;
  unsigned int lk_num_waited_on_pages;
  unsigned int lk_num_waited_on_objects;

  /* Execution statistics for adding volumes */
  unsigned int io_num_format_volume;

#if defined (SERVER_MODE)
  MUTEX_T lock;
#endif				/* SERVER_MODE */
};

extern void mnt_server_dump_stats (const MNT_SERVER_EXEC_STATS * stats,
				   FILE * stream);

extern void mnt_get_current_times (time_t * cpu_usr_time,
				   time_t * cpu_sys_time,
				   time_t * elapsed_time);

/* perf_monitor_sky_2.h */
#if !defined(SERVER_MODE)
/* Client execution statistic structure */
typedef struct mnt_exec_stats MNT_EXEC_STATS;
struct mnt_exec_stats
{
  time_t cpu_start_usr_time;
  time_t cpu_start_sys_time;
  time_t elapsed_start_time;
  MNT_SERVER_EXEC_STATS *server_stats;	/* A copy of server statistics */
};

extern int mnt_start_stats (void);
extern void mnt_stop_stats (void);
extern void mnt_reset_stats (void);
extern void mnt_print_stats (FILE * stream);
extern MNT_EXEC_STATS *mnt_get_stats (void);

#endif /* !SERVER_MODE */

/* perf_monitor_earth_1.h */
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

typedef int (*T_DO_FUNC) (int value, T_DIAG_VALUE_SETTYPE settype,
			  char *err_buf);

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

#if !defined(ADD_TIMEVAL)
#define ADD_TIMEVAL(total, start, end) do {	\
  total.tv_usec +=                              \
    (end.tv_usec - start.tv_usec) >= 0 ?        \
      (end.tv_usec-start.tv_usec)               \
    : (1000000 + (end.tv_usec-start.tv_usec));  \
  total.tv_sec +=                               \
    (end.tv_usec - start.tv_usec) >= 0 ?        \
      (end.tv_sec-start.tv_sec)                 \
    : (end.tv_sec-start.tv_sec-1);              \
  total.tv_sec +=                               \
    total.tv_usec/1000000;                      \
  total.tv_usec %= 1000000;                     \
} while(0)
#endif /* ADD_TIMEVAL */

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

extern bool init_diag_mgr (const char *server_name, int num_thread,
			   char *err_buf);
extern void close_diag_mgr (void);
extern bool set_diag_value (T_DIAG_OBJ_TYPE type, int value,
			    T_DIAG_VALUE_SETTYPE settype, char *err_buf);
#endif /* SERVER_MODE */

/* perf_monitor_earth_2.h */
#if !defined(CS_MODE)
extern int mnt_Num_tran_exec_stats;
/*
 * Statistics at page level
 */
#define mnt_pb_fetches(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_pb_fetches(thread_p)
#define mnt_pb_dirties(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_pb_dirties(thread_p)
#define mnt_pb_ioreads(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_pb_ioreads(thread_p)
#define mnt_pb_iowrites(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_pb_iowrites(thread_p)

/*
 * Statistics at log level
 */
#define mnt_log_ioreads(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_log_ioreads(thread_p)
#define mnt_log_iowrites(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_log_iowrites(thread_p)
#define mnt_log_appendrecs(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_log_appendrecs(thread_p)
#define mnt_log_archives(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_log_archives(thread_p)
#define mnt_log_checkpoints(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_log_checkpoints(thread_p)

/*
 * Statistics at lock level
 */
#define mnt_lk_acquired_on_pages(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_lk_acquired_on_pages(thread_p)
#define mnt_lk_acquired_on_objects(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_lk_acquired_on_objects(thread_p)
#define mnt_lk_converted_on_pages(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_lk_converted_on_pages(thread_p)
#define mnt_lk_converted_on_objects(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_lk_converted_on_objects(thread_p)
#define mnt_lk_re_requested_on_pages(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_lk_re_requested_on_pages(thread_p)
#define mnt_lk_re_requested_on_objects(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_lk_re_requested_on_objects(thread_p)
#define mnt_lk_waited_on_pages(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_lk_waited_on_pages(thread_p)
#define mnt_lk_waited_on_objects(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_lk_waited_on_objects(thread_p)

/*
 * Disk allocation level
 */
#define mnt_io_format_vols(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_io_format_vols(thread_p)


extern MNT_SERVER_EXEC_STATS *mnt_server_get_stats (THREAD_ENTRY * thread_p);
#if defined(SERVER_MODE)
extern int xmnt_server_start_stats (THREAD_ENTRY * thread_p);
extern void xmnt_server_stop_stats (THREAD_ENTRY * thread_p);
extern void xmnt_server_copy_stats (THREAD_ENTRY * thread_p,
				    MNT_SERVER_EXEC_STATS * to_stats);
extern void xmnt_server_reset_stats (THREAD_ENTRY * thread_p);
#endif /* SERVER_MODE */

extern int mnt_server_init (int num_tran_indices);
extern void mnt_server_final (void);
extern void mnt_server_print_stats (THREAD_ENTRY * thread_p, FILE * stream);

extern void mnt_x_pb_fetches (THREAD_ENTRY * thread_p);
extern void mnt_x_pb_dirties (THREAD_ENTRY * thread_p);
extern void mnt_x_pb_ioreads (THREAD_ENTRY * thread_p);
extern void mnt_x_pb_iowrites (THREAD_ENTRY * thread_p);
extern void mnt_x_log_ioreads (THREAD_ENTRY * thread_p);
extern void mnt_x_log_iowrites (THREAD_ENTRY * thread_p);
extern void mnt_x_log_appendrecs (THREAD_ENTRY * thread_p);
extern void mnt_x_log_archives (THREAD_ENTRY * thread_p);
extern void mnt_x_log_checkpoints (THREAD_ENTRY * thread_p);
extern void mnt_x_lk_acquired_on_pages (THREAD_ENTRY * thread_p);
extern void mnt_x_lk_acquired_on_objects (THREAD_ENTRY * thread_p);
extern void mnt_x_lk_converted_on_pages (THREAD_ENTRY * thread_p);
extern void mnt_x_lk_converted_on_objects (THREAD_ENTRY * thread_p);
extern void mnt_x_lk_re_requested_on_pages (THREAD_ENTRY * thread_p);
extern void mnt_x_lk_re_requested_on_objects (THREAD_ENTRY * thread_p);
extern void mnt_x_lk_waited_on_pages (THREAD_ENTRY * thread_p);
extern void mnt_x_lk_waited_on_objects (THREAD_ENTRY * thread_p);
extern void mnt_x_io_format_vols (THREAD_ENTRY * thread_p);
#endif /* !CS_MODE */

#endif /* _PERF_MONITOR_H_ */
