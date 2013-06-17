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

#include "thread.h"

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
 * If members are added or removed in this structure then changes must be made
 * also in MNT_SIZE_OF_SERVER_EXEC_STATS, STAT_SIZE_MEMORY and
 * MNT_SERVER_EXEC_STATS_SIZEOF
 */
typedef struct mnt_server_exec_stats MNT_SERVER_EXEC_STATS;
struct mnt_server_exec_stats
{
  /* Execution statistics for the file io */
  UINT64 file_num_creates;
  UINT64 file_num_removes;
  UINT64 file_num_ioreads;
  UINT64 file_num_iowrites;
  UINT64 file_num_iosynches;

  /* Execution statistics for the page buffer manager */
  UINT64 pb_num_fetches;
  UINT64 pb_num_dirties;
  UINT64 pb_num_ioreads;
  UINT64 pb_num_iowrites;
  UINT64 pb_num_victims;
  UINT64 pb_num_replacements;

  /* Execution statistics for the log manager */
  UINT64 log_num_ioreads;
  UINT64 log_num_iowrites;
  UINT64 log_num_appendrecs;
  UINT64 log_num_archives;
  UINT64 log_num_start_checkpoints;
  UINT64 log_num_end_checkpoints;
  UINT64 log_num_wals;

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
  UINT64 bt_num_covered;
  UINT64 bt_num_noncovered;
  UINT64 bt_num_resumes;
  UINT64 bt_num_multi_range_opt;
  UINT64 bt_num_splits;
  UINT64 bt_num_merges;

  /* Execution statistics for the query manager */
  UINT64 qm_num_selects;
  UINT64 qm_num_inserts;
  UINT64 qm_num_deletes;
  UINT64 qm_num_updates;
  UINT64 qm_num_sscans;
  UINT64 qm_num_iscans;
  UINT64 qm_num_lscans;
  UINT64 qm_num_setscans;
  UINT64 qm_num_methscans;
  UINT64 qm_num_nljoins;
  UINT64 qm_num_mjoins;
  UINT64 qm_num_objfetches;
  UINT64 qm_num_holdable_cursors;

  /* Execution statistics for external sort */
  UINT64 sort_num_io_pages;
  UINT64 sort_num_data_pages;

  /* Execution statistics for network communication */
  UINT64 net_num_requests;

  /* flush control stat */
  UINT64 fc_num_pages;
  UINT64 fc_num_log_pages;
  UINT64 fc_tokens;

  /* prior lsa info */
  UINT64 prior_lsa_list_size;	/* kbytes */
  UINT64 prior_lsa_list_maxed;
  UINT64 prior_lsa_list_removed;

  /* best space info */
  UINT64 hf_num_stats_entries;
  UINT64 hf_num_stats_maxed;

  /* Other statistics */
  UINT64 pb_hit_ratio;
  /* ((pb_num_fetches - pb_num_ioreads) x 100 / pb_num_fetches) x 100 */

  /* This must be kept as last member. Otherwise the
   * MNT_SERVER_EXEC_STATS_SIZEOF macro must be modified */
  bool enable_local_stat;	/* used for local stats */
};

/* number of field of MNT_SERVER_EXEC_STATS structure */
#define MNT_SIZE_OF_SERVER_EXEC_STATS 66

/* The exact size of mnt_server_exec_stats structure */
#define MNT_SERVER_EXEC_STATS_SIZEOF \
  (offsetof (MNT_SERVER_EXEC_STATS, enable_local_stat) + sizeof (bool))

extern void mnt_server_dump_stats (const MNT_SERVER_EXEC_STATS * stats,
				   FILE * stream, const char *substr);

extern void mnt_server_dump_stats_to_buffer (const MNT_SERVER_EXEC_STATS *
					     stats, char *buffer,
					     int buf_size,
					     const char *substr);

extern void mnt_get_current_times (time_t * cpu_usr_time,
				   time_t * cpu_sys_time,
				   time_t * elapsed_time);

extern int mnt_calc_diff_stats (MNT_SERVER_EXEC_STATS * stats_diff,
				MNT_SERVER_EXEC_STATS * new_stats,
				MNT_SERVER_EXEC_STATS * old_stats);

#if defined(CS_MODE) || defined(SA_MODE)
/* Client execution statistic structure */
typedef struct mnt_client_stat_info MNT_CLIENT_STAT_INFO;
struct mnt_client_stat_info
{
  time_t cpu_start_usr_time;
  time_t cpu_start_sys_time;
  time_t elapsed_start_time;
  MNT_SERVER_EXEC_STATS *base_server_stats;
  MNT_SERVER_EXEC_STATS *current_server_stats;
  MNT_SERVER_EXEC_STATS *old_global_stats;
  MNT_SERVER_EXEC_STATS *current_global_stats;
};

extern bool mnt_Iscollecting_stats;

extern int mnt_start_stats (bool for_all_trans);
extern int mnt_stop_stats (void);
extern void mnt_reset_stats (void);
extern void mnt_print_stats (FILE * stream);
extern void mnt_print_global_stats (FILE * stream, bool cumulative,
				    const char *substr);
extern MNT_SERVER_EXEC_STATS *mnt_get_stats (void);
extern MNT_SERVER_EXEC_STATS *mnt_get_global_stats (void);
extern int mnt_get_global_diff_stats (MNT_SERVER_EXEC_STATS * diff_stats);
#endif /* CS_MODE || SA_MODE */

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
#endif /* DIAG_DEVEL */

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

#define ADD_TIMEVAL(total, start, end) do {     \
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

#define TO_MSEC(elapsed) \
  ((int)((elapsed.tv_sec * 1000) + (int) (elapsed.tv_usec / 1000)))

#if defined (EnableThreadMonitoring)
#define MONITOR_WAITING_THREAD(elpased) \
    (prm_get_integer_value (PRM_ID_MNT_WAITING_THREAD) > 0 \
     && ((elpased).tv_sec * 1000 + (elpased).tv_usec / 1000) \
         > prm_get_integer_value (PRM_ID_MNT_WAITING_THREAD))
#endif

#if defined(SERVER_MODE) || defined (SA_MODE)
extern int mnt_Num_tran_exec_stats;

/*
 * Statistics at file io level
 */
#define mnt_file_creates(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_file_creates(thread_p)
#define mnt_file_removes(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_file_removes(thread_p)
#define mnt_file_ioreads(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_file_ioreads(thread_p)
#define mnt_file_iowrites(thread_p, num_pages) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_file_iowrites(thread_p, num_pages)
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
#define mnt_pb_iowrites(thread_p, num_pages) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_pb_iowrites(thread_p, num_pages)
#define mnt_pb_victims(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_pb_victims(thread_p)
#define mnt_pb_replacements(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_pb_replacements(thread_p)

/*
 * Statistics at log level
 */
#define mnt_log_ioreads(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_log_ioreads(thread_p)
#define mnt_log_iowrites(thread_p, num_log_pages) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_log_iowrites(thread_p, num_log_pages)
#define mnt_log_appendrecs(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_log_appendrecs(thread_p)
#define mnt_log_archives(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_log_archives(thread_p)
#define mnt_log_start_checkpoints(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_log_start_checkpoints(thread_p)
#define mnt_log_end_checkpoints(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_log_end_checkpoints(thread_p)
#define mnt_log_wals(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_log_wals(thread_p)

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
#define mnt_bt_covered(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_bt_covered(thread_p)
#define mnt_bt_noncovered(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_bt_noncovered(thread_p)
#define mnt_bt_resumes(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_bt_resumes(thread_p)
#define mnt_bt_multi_range_opt(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_bt_multi_range_opt(thread_p)
#define mnt_bt_splits(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_bt_splits(thread_p)
#define mnt_bt_merges(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_bt_merges(thread_p)

/* Execution statistics for the query manager */
#define mnt_qm_selects(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_qm_selects(thread_p)
#define mnt_qm_inserts(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_qm_inserts(thread_p)
#define mnt_qm_deletes(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_qm_deletes(thread_p)
#define mnt_qm_updates(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_qm_updates(thread_p)
#define mnt_qm_sscans(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_qm_sscans(thread_p)
#define mnt_qm_iscans(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_qm_iscans(thread_p)
#define mnt_qm_lscans(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_qm_lscans(thread_p)
#define mnt_qm_setscans(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_qm_setscans(thread_p)
#define mnt_qm_methscans(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_qm_methscans(thread_p)
#define mnt_qm_nljoins(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_qm_nljoins(thread_p)
#define mnt_qm_mjoins(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_qm_mjoins(thread_p)
#define mnt_qm_objfetches(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_qm_objfetches(thread_p)
#define mnt_qm_holdable_cursor(thread_p, num_cursors) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_qm_holdable_cursor(thread_p, num_cursors)

/* execution statistics for external sort */
#define mnt_sort_io_pages(thread_p) \
  if (mnt_Num_tran_exec_stats > 0 && thread_get_sort_stats_active(thread_p)) \
    mnt_x_sort_io_pages(thread_p)
#define mnt_sort_data_pages(thread_p) \
  if (mnt_Num_tran_exec_stats > 0 && thread_get_sort_stats_active(thread_p)) \
    mnt_x_sort_data_pages(thread_p)

/* Prior LSA */
#define mnt_prior_lsa_list_size(thread_p, list_size) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_prior_lsa_list_size(thread_p, list_size)
#define mnt_prior_lsa_list_maxed(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_prior_lsa_list_maxed(thread_p)
#define mnt_prior_lsa_list_removed(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_prior_lsa_list_removed(thread_p)

/* Heap best space info */
#define mnt_hf_stats_bestspace_entries(thread_p, num_entries) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_hf_stats_bestspace_entries(thread_p, num_entries)
#define mnt_hf_stats_bestspace_maxed(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_hf_stats_bestspace_maxed(thread_p)

/* Statistics at Flush Control */
#define mnt_fc_stats(thread_p, num_pages, num_overflows, tokens) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_fc_stats(thread_p, num_pages, num_overflows, tokens)

/*
 * Network Communication level
 */
#define mnt_net_requests(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_net_requests(thread_p)

extern MNT_SERVER_EXEC_STATS *mnt_server_get_stats (THREAD_ENTRY * thread_p);
extern bool mnt_server_is_stats_on (THREAD_ENTRY * thread_p);

extern int mnt_server_init (int num_tran_indices);
extern void mnt_server_final (void);
#if defined(ENABLE_UNUSED_FUNCTION)
extern void mnt_server_print_stats (THREAD_ENTRY * thread_p, FILE * stream);
#endif
extern void mnt_x_file_creates (THREAD_ENTRY * thread_p);
extern void mnt_x_file_removes (THREAD_ENTRY * thread_p);
extern void mnt_x_file_ioreads (THREAD_ENTRY * thread_p);
extern void mnt_x_file_iowrites (THREAD_ENTRY * thread_p, int num_pages);
extern void mnt_x_file_iosynches (THREAD_ENTRY * thread_p);
extern void mnt_x_pb_fetches (THREAD_ENTRY * thread_p);
extern void mnt_x_pb_dirties (THREAD_ENTRY * thread_p);
extern void mnt_x_pb_ioreads (THREAD_ENTRY * thread_p);
extern void mnt_x_pb_iowrites (THREAD_ENTRY * thread_p, int num_pages);
extern void mnt_x_pb_victims (THREAD_ENTRY * thread_p);
extern void mnt_x_pb_replacements (THREAD_ENTRY * thread_p);
extern void mnt_x_log_ioreads (THREAD_ENTRY * thread_p);
extern void mnt_x_log_iowrites (THREAD_ENTRY * thread_p, int num_log_pages);
extern void mnt_x_log_appendrecs (THREAD_ENTRY * thread_p);
extern void mnt_x_log_archives (THREAD_ENTRY * thread_p);
extern void mnt_x_log_start_checkpoints (THREAD_ENTRY * thread_p);
extern void mnt_x_log_end_checkpoints (THREAD_ENTRY * thread_p);
extern void mnt_x_log_wals (THREAD_ENTRY * thread_p);
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
extern void mnt_x_bt_inserts (THREAD_ENTRY * thread_p);
extern void mnt_x_bt_deletes (THREAD_ENTRY * thread_p);
extern void mnt_x_bt_updates (THREAD_ENTRY * thread_p);
extern void mnt_x_bt_covered (THREAD_ENTRY * thread_p);
extern void mnt_x_bt_noncovered (THREAD_ENTRY * thread_p);
extern void mnt_x_bt_resumes (THREAD_ENTRY * thread_p);
extern void mnt_x_bt_multi_range_opt (THREAD_ENTRY * thread_p);
extern void mnt_x_bt_splits (THREAD_ENTRY * thread_p);
extern void mnt_x_bt_merges (THREAD_ENTRY * thread_p);
extern void mnt_x_qm_selects (THREAD_ENTRY * thread_p);
extern void mnt_x_qm_inserts (THREAD_ENTRY * thread_p);
extern void mnt_x_qm_deletes (THREAD_ENTRY * thread_p);
extern void mnt_x_qm_updates (THREAD_ENTRY * thread_p);
extern void mnt_x_qm_sscans (THREAD_ENTRY * thread_p);
extern void mnt_x_qm_iscans (THREAD_ENTRY * thread_p);
extern void mnt_x_qm_lscans (THREAD_ENTRY * thread_p);
extern void mnt_x_qm_setscans (THREAD_ENTRY * thread_p);
extern void mnt_x_qm_methscans (THREAD_ENTRY * thread_p);
extern void mnt_x_qm_nljoins (THREAD_ENTRY * thread_p);
extern void mnt_x_qm_mjoins (THREAD_ENTRY * thread_p);
extern void mnt_x_qm_objfetches (THREAD_ENTRY * thread_p);
extern void mnt_x_qm_holdable_cursor (THREAD_ENTRY * thread_p,
				      int num_cursors);
extern void mnt_x_sort_io_pages (THREAD_ENTRY * thread_p);
extern void mnt_x_sort_data_pages (THREAD_ENTRY * thread_p);
extern void mnt_x_net_requests (THREAD_ENTRY * thread_p);

extern void mnt_x_prior_lsa_list_size (THREAD_ENTRY * thread_p,
				       unsigned int list_size);
extern void mnt_x_prior_lsa_list_maxed (THREAD_ENTRY * thread_p);
extern void mnt_x_prior_lsa_list_removed (THREAD_ENTRY * thread_p);

extern void mnt_x_hf_stats_bestspace_entries (THREAD_ENTRY * thread_p,
					      unsigned int num_entries);
extern void mnt_x_hf_stats_bestspace_maxed (THREAD_ENTRY * thread_p);

extern void mnt_x_fc_stats (THREAD_ENTRY * thread_p, unsigned int num_pages,
			    unsigned int num_log_pages, unsigned int tokens);
extern UINT64 mnt_x_get_stats_and_clear (THREAD_ENTRY * thread_p,
					 const char *stat_name);
#else /* SERVER_MODE || SA_MODE */

#define mnt_file_creates(thread_p)
#define mnt_file_removes(thread_p)
#define mnt_file_ioreads(thread_p)
#define mnt_file_iowrites(thread_p, num_pages)
#define mnt_file_iosynches(thread_p)

#define mnt_pb_fetches(thread_p)
#define mnt_pb_dirties(thread_p)
#define mnt_pb_ioreads(thread_p)
#define mnt_pb_iowrites(thread_p, num_pages)
#define mnt_pb_victims(thread_p)
#define mnt_pb_replacements(thread_p)

#define mnt_log_ioreads(thread_p)
#define mnt_log_iowrites(thread_p, num_log_pages)
#define mnt_log_appendrecs(thread_p)
#define mnt_log_archives(thread_p)
#define mnt_log_start_checkpoints(thread_p)
#define mnt_log_end_checkpoints(thread_p)
#define mnt_log_wals(thread_p)

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
#define mnt_bt_covered(thread_p)
#define mnt_bt_noncovered(thread_p)
#define mnt_bt_resumes(thread_p)
#define mnt_bt_multi_range_opt(thread_p)

#define mnt_qm_selects(thread_p)
#define mnt_qm_inserts(thread_p)
#define mnt_qm_deletes(thread_p)
#define mnt_qm_updates(thread_p)
#define mnt_qm_sscans(thread_p)
#define mnt_qm_iscans(thread_p)
#define mnt_qm_lscans(thread_p)
#define mnt_qm_setscans(thread_p)
#define mnt_qm_methscans(thread_p)
#define mnt_qm_nljoins(thread_p)
#define mnt_qm_mjoins(thread_p)
#define mnt_qm_objfetches(thread_p)
#define mnt_qm_holdable_cursor(thread_p, num_cursors)

#define mnt_net_requests(thread_p)

#define mnt_prior_lsa_list_size (thread_p, list_size)
#define mnt_prior_lsa_list_maxed (thread_p)
#define mnt_prior_lsa_list_removed (thread_p)

#define mnt_hf_stats_bestspace_entries (thread_p, num_entries)
#define mnt_hf_stats_bestspace_maxed (thread_p)

#define mnt_fc_stats (thread_p, num_pages, num_log_pages, num_tokens)
#endif /* CS_MODE */

#endif /* _PERF_MONITOR_H_ */
