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

typedef struct t_diag_monitor_cas_value T_DIAG_MONITOR_CAS_VALUE;
struct t_diag_monitor_cas_value
{
  INT64 reqs_in_interval;
  INT64 transactions_in_interval;
  INT64 query_in_interval;
  int active_sessions;
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

/*
 * Server execution statistic structure
 */
typedef struct mnt_server_exec_stats MNT_SERVER_EXEC_STATS;
struct mnt_server_exec_stats
{
  /* Execution statistics for the file io */
  unsigned int file_num_opens;
  unsigned int file_num_creates;
  unsigned int file_num_ioreads;
  unsigned int file_num_iowrites;
  unsigned int file_num_iosynches;

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

  /* Execution statistics for transactions */
  unsigned int tran_num_commits;
  unsigned int tran_num_rollbacks;
  unsigned int tran_num_savepoints;
  unsigned int tran_num_start_topops;
  unsigned int tran_num_end_topops;
  unsigned int tran_num_interrupts;

  /* Execution statistics for the btree manager */
  unsigned int bt_num_inserts;
  unsigned int bt_num_deletes;
  unsigned int bt_num_updates;

  /* Execution statistics for network communication */
  unsigned int net_num_requests;

#if defined (SERVER_MODE)
  MUTEX_T lock;
#endif				/* SERVER_MODE */
};

typedef struct mnt_server_exec_global_stats MNT_SERVER_EXEC_GLOBAL_STATS;
struct mnt_server_exec_global_stats
{
  /* Execution statistics for the file io */
  UINT64 file_num_opens;
  UINT64 file_num_creates;
  UINT64 file_num_ioreads;
  UINT64 file_num_iowrites;
  UINT64 file_num_iosynches;

  /* Execution statistics for the page buffer manager */
  UINT64 pb_num_fetches;
  UINT64 pb_num_dirties;
  UINT64 pb_num_ioreads;
  UINT64 pb_num_iowrites;

  /* Execution statistics for the log manager */
  UINT64 log_num_ioreads;
  UINT64 log_num_iowrites;
  UINT64 log_num_appendrecs;
  UINT64 log_num_archives;
  UINT64 log_num_checkpoints;

  /* Execution statistics for the lock manager */
  UINT64 lk_num_acquired_on_pages;
  UINT64 lk_num_acquired_on_objects;
  UINT64 lk_num_converted_on_pages;
  UINT64 lk_num_converted_on_objects;
  UINT64 lk_num_re_requested_on_pages;
  UINT64 lk_num_re_requested_on_objects;
  UINT64 lk_num_waited_on_pages;
  UINT64 lk_num_waited_on_objects;

  /* Execution statistics for transactions */
  UINT64 tran_num_commits;
  UINT64 tran_num_rollbacks;
  UINT64 tran_num_savepoints;
  UINT64 tran_num_start_topops;
  UINT64 tran_num_end_topops;
  UINT64 tran_num_interrupts;

  /* Execution statistics for the btree manager */
  UINT64 bt_num_inserts;
  UINT64 bt_num_deletes;
  UINT64 bt_num_updates;

  /* Execution statistics for network communication */
  UINT64 net_num_requests;
};

extern void mnt_server_dump_stats (const MNT_SERVER_EXEC_STATS * stats,
				   FILE * stream);
extern void mnt_server_dump_global_stats (const MNT_SERVER_EXEC_GLOBAL_STATS
					  * stats, FILE * stream);

extern void mnt_get_current_times (time_t * cpu_usr_time,
				   time_t * cpu_sys_time,
				   time_t * elapsed_time);

#if defined(CS_MODE) || defined(SA_MODE)
/* Client execution statistic structure */
typedef struct mnt_exec_stats MNT_EXEC_STATS;
struct mnt_exec_stats
{
  time_t cpu_start_usr_time;
  time_t cpu_start_sys_time;
  time_t elapsed_start_time;
  MNT_SERVER_EXEC_STATS *server_stats;	/* A copy of server statistics */
  MNT_SERVER_EXEC_GLOBAL_STATS server_global_stats;
};

extern int mnt_start_stats (bool for_all_trans);
extern int mnt_stop_stats (void);
extern void mnt_reset_stats (void);
extern void mnt_reset_global_stats (void);
extern void mnt_print_stats (FILE * stream);
extern void mnt_print_global_stats (FILE * stream);
extern MNT_EXEC_STATS *mnt_get_stats (void);
extern MNT_SERVER_EXEC_GLOBAL_STATS *mnt_get_global_stats (void);
#endif /* CS_MODE || SA_MODE */

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

#ifndef DIFF_TIMEVAL
#define DIFF_TIMEVAL(start, end, elapsed) \
    do { \
      (elapsed).tv_sec = (end).tv_sec - (start).tv_sec; \
      (elapsed).tv_usec = (end).tv_usec - (start).tv_usec; \
      if ((elapsed).tv_usec < 0) \
        { \
          (elapsed).tv_sec--; \
          (elapsed).tv_usec += 1000000; \
        } \
    } while (0)
#endif

#define MONITOR_WAITING_THREAD(elpased) \
    (PRM_MNT_WAITING_THREAD > 0 \
     && ((elpased).tv_sec * 1000 + (elpased).tv_usec / 1000) \
         > PRM_MNT_WAITING_THREAD)

#if defined(SERVER_MODE) || defined (SA_MODE)
extern int mnt_Num_tran_exec_stats;

/*
 * Statistics at file io level
 */
#define mnt_file_opens(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_file_opens(thread_p)
#define mnt_file_creates(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_file_creates(thread_p)
#define mnt_file_ioreads(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_file_ioreads(thread_p)
#define mnt_file_iowrites(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_file_iowrites(thread_p)
#define mnt_file_iosynches(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_file_iosynches(thread_p)

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
 * Transaction Management level
 */
#define mnt_tran_commits(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_tran_commits(thread_p)
#define mnt_tran_rollbacks(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_tran_rollbacks(thread_p)
#define mnt_tran_savepoints(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_tran_savepoints(thread_p)
#define mnt_tran_start_topops(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_tran_start_topops(thread_p)
#define mnt_tran_end_topops(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_tran_end_topops(thread_p)
#define mnt_tran_interrupts(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_tran_interrupts(thread_p)

/*
 * Statistics at btree level
 */
#define mnt_bt_inserts(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_bt_inserts(thread_p)
#define mnt_bt_deletes(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_bt_deletes(thread_p)
#define mnt_bt_updates(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_bt_updates(thread_p)

/*
 * Network Communication level
 */
#define mnt_net_requests(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_net_requests(thread_p)


extern void mnt_server_reflect_local_stats (THREAD_ENTRY * thread_p);
extern MNT_SERVER_EXEC_STATS *mnt_server_get_stats (THREAD_ENTRY * thread_p);

extern int mnt_server_init (int num_tran_indices);
extern void mnt_server_final (void);
extern void mnt_server_print_stats (THREAD_ENTRY * thread_p, FILE * stream);
extern void mnt_x_file_opens (THREAD_ENTRY * thread_p);
extern void mnt_x_file_creates (THREAD_ENTRY * thread_p);
extern void mnt_x_file_ioreads (THREAD_ENTRY * thread_p);
extern void mnt_x_file_iowrites (THREAD_ENTRY * thread_p);
extern void mnt_x_file_iosynches (THREAD_ENTRY * thread_p);
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
extern void mnt_x_tran_commits (THREAD_ENTRY * thread_p);
extern void mnt_x_tran_rollbacks (THREAD_ENTRY * thread_p);
extern void mnt_x_tran_savepoints (THREAD_ENTRY * thread_p);
extern void mnt_x_tran_start_topops (THREAD_ENTRY * thread_p);
extern void mnt_x_tran_end_topops (THREAD_ENTRY * thread_p);
extern void mnt_x_tran_interrupts (THREAD_ENTRY * thread_p);
extern void mnt_x_bt_inserts (THREAD_ENTRY *thread_p);
extern void mnt_x_bt_deletes (THREAD_ENTRY *thread_p);
extern void mnt_x_bt_updates (THREAD_ENTRY *thread_p);
extern void mnt_x_net_requests (THREAD_ENTRY * thread_p);

#else /* SERVER_MODE || SA_MODE */

#define mnt_file_opens(thread_p)
#define mnt_file_creates(thread_p)
#define mnt_file_ioreads(thread_p)
#define mnt_file_iowrites(thread_p)
#define mnt_file_iosynches(thread_p)

#define mnt_pb_fetches(thread_p)
#define mnt_pb_dirties(thread_p)
#define mnt_pb_ioreads(thread_p)
#define mnt_pb_iowrites(thread_p)

#define mnt_log_ioreads(thread_p)
#define mnt_log_iowrites(thread_p)
#define mnt_log_appendrecs(thread_p)
#define mnt_log_archives(thread_p)
#define mnt_log_checkpoints(thread_p)

#define mnt_lk_acquired_on_pages(thread_p)
#define mnt_lk_acquired_on_objects(thread_p)
#define mnt_lk_converted_on_pages(thread_p)
#define mnt_lk_converted_on_objects(thread_p)
#define mnt_lk_re_requested_on_pages(thread_p)
#define mnt_lk_re_requested_on_objects(thread_p)
#define mnt_lk_waited_on_pages(thread_p)
#define mnt_lk_waited_on_objects(thread_p)

#define mnt_tran_commits(thread_p)
#define mnt_tran_rollbacks(thread_p)
#define mnt_tran_savepoints(thread_p)
#define mnt_tran_start_topops(thread_p)
#define mnt_tran_end_topops(thread_p)
#define mnt_tran_interrupts(thread_p)

#define mnt_bt_inserts(thread_p)
#define mnt_bt_deletes(thread_p)
#define mnt_bt_updates(thread_p)

#define mnt_net_requests(hread_p)
#endif /* CS_MODE */

#endif /* _PERF_MONITOR_H_ */
