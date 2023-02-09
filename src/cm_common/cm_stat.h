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
 * cm_stat.h -
 */

#ifndef _CM_STAT_H_
#define _CM_STAT_H_

#ifdef __cplusplus
extern "C"
{
#endif

#if !defined(WINDOWS)
#include <sys/types.h>
#include <sys/param.h>
#include <limits.h>
#include <stdint.h>
#else
  typedef __int64 int64_t;
  typedef unsigned __int64 uint64_t;
#endif

#include <time.h>

#define CM_MIN_ERROR 2000
#define CM_UNKOWN_ERROR 2000
#define CM_GENERAL_ERROR 2000
#define CM_OUT_OF_MEMORY    2001
#define CM_DB_STAT_NOT_FOUND 2002
#define CM_BROKER_STAT_NOT_FOUND 2003
#define CM_ERR_SYSTEM_CALL 2004
#define CM_ENV_VAR_NOT_SET 2005
#define CM_ERR_NULL_POINTER 2006
#define CM_FILE_OPEN_FAILED 2007
#define CM_READ_STATDUMP_INFO_ERROR 2008

#define BROKER_NAME_LEN     64
#define DB_NAME_LEN         64

#if !defined (DO_NOT_USE_CUBRIDENV)
#define BROKER_LOG_DIR  "log/broker"
#define UNICAS_CONF_DIR "conf"
#else
#define BROKER_LOG_DIR  CUBRID_LOGDIR "/broker"
#define UNICAS_CONF_DIR CUBRID_CONFDIR
#endif

#define UNICAS_SQL_LOG_DIR "sql_log"

  typedef struct
  {
    int err_code;
    char err_msg[512];
  } T_CM_ERROR;

  typedef struct
  {
    uint64_t cpu_user;
    uint64_t cpu_kernel;
    uint64_t cpu_idle;
    uint64_t cpu_iowait;
    uint64_t mem_physical_total;
    uint64_t mem_physical_free;
    uint64_t mem_swap_total;
    uint64_t mem_swap_free;
  } T_CM_HOST_STAT;

  typedef struct
  {
    int pid;
    uint64_t cpu_kernel;
    uint64_t cpu_user;
    uint64_t mem_physical;
    uint64_t mem_virtual;	/* size item in statm file */
  } T_CM_PROC_STAT;

  typedef struct
  {
    char name[DB_NAME_LEN];
    T_CM_PROC_STAT stat;
  } T_CM_DB_PROC_STAT;

  typedef struct
  {
    int num_stat;
    T_CM_DB_PROC_STAT *db_stats;
  } T_CM_DB_PROC_STAT_ALL;

  typedef struct
  {
    char br_name[BROKER_NAME_LEN];
    T_CM_PROC_STAT br_stat;
    int ncas;
    T_CM_PROC_STAT cas_stats[1];
  } T_CM_BROKER_PROC_STAT;

  typedef struct
  {
    int num_stat;
    T_CM_BROKER_PROC_STAT **br_stats;
  } T_CM_BROKER_PROC_STAT_ALL;

  typedef struct
  {
    char name[256];
    uint64_t size;
    uint64_t used;
    uint64_t avail;
  } T_CM_DISK_PARTITION_STAT;

  typedef struct
  {
    int num_stat;
    T_CM_DISK_PARTITION_STAT *partitions;
  } T_CM_DISK_PARTITION_STAT_ALL;

  typedef struct
  {
    int id;
    int pid;
    int num_request;
    int as_port;
    char *service_flag;
    char *status;
    time_t last_access_time;
    int psize;
    int num_thr;
    int cpu_time;
    float pcpu;
    char *clt_ip_addr;
    char *clt_appl_name;
    char *request_file;
    char *log_msg;
    char *database_name;
    char *database_host;
    time_t last_connect_time;
    int64_t num_requests_received;
    int64_t num_transactions_processed;
    int64_t num_queries_processed;
    int64_t num_long_queries;
    int64_t num_long_transactions;
    int64_t num_error_queries;
    int64_t num_interrupts;
  } T_CM_CAS_INFO;

  typedef struct
  {
    int id;
    int priority;
    char ipstr[20];
    time_t recv_time;
    char script[32];
    char prgname[32];
  } T_CM_JOB_INFO;

  typedef struct
  {
    char *name;
    char *as_type;
    int pid;
    int port;
    int shm_id;
    int num_as;
    int max_as;
    int min_as;
    int num_job_q;
    int num_thr;
    float pcpu;
    int cpu_time;
    int num_busy_count;
    int num_req;
    int64_t num_tran;
    int64_t num_query;
    int64_t num_long_tran;
    int64_t num_long_query;
    int64_t num_error_query;
    int long_query_time;
    int long_transaction_time;
    char *session_timeout;
    int as_max_size;
    char *keep_connection;
    char log_backup;
    char *sql_log_mode;
    char *access_mode;
    char source_env_flag;
    char access_list_flag;
    int time_to_kill;
    char *status;
    char *auto_add;
    char *log_dir;
  } T_CM_BROKER_INFO;

  typedef struct
  {
    int num_info;
    T_CM_CAS_INFO *as_info;
  } T_CM_CAS_INFO_ALL;
  typedef struct
  {
    int num_info;
    T_CM_JOB_INFO *job_info;
  } T_CM_JOB_INFO_ALL;
  typedef struct
  {
    int num_info;
    T_CM_BROKER_INFO *br_info;
  } T_CM_BROKER_INFO_ALL;

  typedef enum
  {
    UC_FID_ADMIN_LOG,
    UC_FID_UNICAS_CONF,
    UC_FID_CUBRID_CAS_CONF,
    UC_FID_CUBRID_BROKER_CONF
  } T_UNICAS_FILE_ID;


  typedef struct
  {
    char *name;
    char *value;
  } T_CM_BROKER_CONF_ITEM;

  typedef struct
  {
    int num;
    T_CM_BROKER_CONF_ITEM *item;
  } T_CM_BR_CONF;

  typedef struct
  {
    int num_header;
    T_CM_BROKER_CONF_ITEM *header_conf;
    int num_broker;
    T_CM_BR_CONF *br_conf;
  } T_CM_BROKER_CONF;

  typedef struct
  {
    /* Execution statistics for the file io */
    uint64_t file_num_creates;
    uint64_t file_num_removes;
    uint64_t file_num_ioreads;
    uint64_t file_num_iowrites;
    uint64_t file_num_iosynches;
    uint64_t file_num_page_allocs;
    uint64_t file_num_page_deallocs;

    /* Execution statistics for the page buffer manager */
    uint64_t pb_num_fetches;
    uint64_t pb_num_dirties;
    uint64_t pb_num_ioreads;
    uint64_t pb_num_iowrites;
    uint64_t pb_num_hash_anchor_waits;
    uint64_t pb_time_hash_anchor_wait;
    /* peeked stats */
    uint64_t pb_fixed_cnt;
    uint64_t pb_dirty_cnt;
    uint64_t pb_lru1_cnt;
    uint64_t pb_lru2_cnt;
    uint64_t pb_lru3_cnt;
    uint64_t pb_avoid_dealloc_cnt;
    uint64_t pb_avoid_victim_cnt;
    uint64_t pb_victim_cand_cnt;

    /* Execution statistics for the log manager */
    uint64_t log_num_fetches;
    uint64_t log_num_ioreads;
    uint64_t log_num_iowrites;
    uint64_t log_num_appendrecs;
    uint64_t log_num_archives;
    uint64_t log_num_start_checkpoints;
    uint64_t log_num_end_checkpoints;
    uint64_t log_num_wals;
    uint64_t log_num_replacements;
    uint64_t log_num_iowrites_for_replacement;

    /* Execution statistics for the lock manager */
    uint64_t lk_num_acquired_on_pages;
    uint64_t lk_num_acquired_on_objects;
    uint64_t lk_num_converted_on_pages;
    uint64_t lk_num_converted_on_objects;
    uint64_t lk_num_re_requested_on_pages;
    uint64_t lk_num_re_requested_on_objects;
    uint64_t lk_num_waited_on_pages;
    uint64_t lk_num_waited_on_objects;
    uint64_t lk_num_waited_time_on_objects;

    /* Execution statistics for transactions */
    uint64_t tran_num_commits;
    uint64_t tran_num_rollbacks;
    uint64_t tran_num_savepoints;
    uint64_t tran_num_start_topops;
    uint64_t tran_num_end_topops;
    uint64_t tran_num_interrupts;

    /* Execution statistics for the btree manager */
    uint64_t bt_num_inserts;
    uint64_t bt_num_deletes;
    uint64_t bt_num_updates;
    uint64_t bt_num_covered;
    uint64_t bt_num_noncovered;
    uint64_t bt_num_resumes;
    uint64_t bt_num_multi_range_opt;
    uint64_t bt_num_splits;
    uint64_t bt_num_merges;
    uint64_t bt_num_get_stats;

    /* Execution statistics for the heap manager */
    uint64_t heap_num_stats_sync_bestspace;

    /* Execution statistics for the query manager */
    uint64_t qm_num_selects;
    uint64_t qm_num_inserts;
    uint64_t qm_num_deletes;
    uint64_t qm_num_updates;
    uint64_t qm_num_sscans;
    uint64_t qm_num_iscans;
    uint64_t qm_num_lscans;
    uint64_t qm_num_setscans;
    uint64_t qm_num_methscans;
    uint64_t qm_num_nljoins;
    uint64_t qm_num_mjoins;
    uint64_t qm_num_objfetches;
    uint64_t qm_num_holdable_cursors;

    /* Execution statistics for external sort */
    uint64_t sort_num_io_pages;
    uint64_t sort_num_data_pages;

    /* Execution statistics for network communication */
    uint64_t net_num_requests;

    /* flush control stat */
    uint64_t fc_num_log_pages;
    uint64_t fc_num_pages;
    uint64_t fc_tokens;

    /* prior lsa info */
    uint64_t prior_lsa_list_size;
    uint64_t prior_lsa_list_maxed;
    uint64_t prior_lsa_list_removed;

    /* best space info */
    uint64_t hf_stats_bestspace_entries;
    uint64_t hf_stats_bestspace_maxed;

    /* HA replication delay */
    uint64_t ha_repl_delay;

    /* Execution statistics for Plan cache */
    uint64_t pc_num_add;
    uint64_t pc_num_lookup;
    uint64_t pc_num_hit;
    uint64_t pc_num_miss;
    uint64_t pc_num_full;
    uint64_t pc_num_delete;
    uint64_t pc_num_invalid_xasl_id;
    uint64_t pc_num_query_string_hash_entries;
    uint64_t pc_num_xasl_id_hash_entries;
    uint64_t pc_num_class_oid_hash_entries;
    uint64_t vac_num_vacuumed_log_pages;
    uint64_t vac_num_to_vacuum_log_pages;
    uint64_t vac_num_prefetch_requests_log_pages;
    uint64_t vac_num_prefetch_hits_log_pages;

    uint64_t heap_home_inserts;
    uint64_t heap_big_inserts;
    uint64_t heap_assign_inserts;
    uint64_t heap_home_deletes;
    uint64_t heap_home_mvcc_deletes;
    uint64_t heap_home_to_rel_deletes;
    uint64_t heap_home_to_big_deletes;
    uint64_t heap_rel_deletes;
    uint64_t heap_rel_mvcc_deletes;
    uint64_t heap_rel_to_home_deletes;
    uint64_t heap_rel_to_big_deletes;
    uint64_t heap_rel_to_rel_deletes;
    uint64_t heap_big_deletes;
    uint64_t heap_big_mvcc_deletes;
    uint64_t heap_new_ver_inserts;
    uint64_t heap_home_updates;
    uint64_t heap_home_to_rel_updates;
    uint64_t heap_home_to_big_updates;
    uint64_t heap_rel_updates;
    uint64_t heap_rel_to_home_updates;
    uint64_t heap_rel_to_rel_updates;
    uint64_t heap_rel_to_big_updates;
    uint64_t heap_big_updates;
    uint64_t heap_home_vacuums;
    uint64_t heap_big_vacuums;
    uint64_t heap_rel_vacuums;
    uint64_t heap_insid_vacuums;
    uint64_t heap_remove_vacuums;
    uint64_t heap_next_ver_vacuums;

    /* Track heap modify. */
    uint64_t heap_insert_prepare;
    uint64_t heap_insert_execute;
    uint64_t heap_insert_log;
    uint64_t heap_delete_prepare;
    uint64_t heap_delete_execute;
    uint64_t heap_delete_log;
    uint64_t heap_update_prepare;
    uint64_t heap_update_execute;
    uint64_t heap_update_log;
    uint64_t heap_vacuum_prepare;
    uint64_t heap_vacuum_execute;
    uint64_t heap_vacuum_log;

    uint64_t bt_find_unique_cnt;
    uint64_t bt_range_search_cnt;
    uint64_t bt_insert_cnt;
    uint64_t bt_delete_cnt;
    uint64_t bt_mvcc_delete_cnt;
    uint64_t bt_mark_delete_cnt;
    uint64_t bt_update_sk_cnt;
    uint64_t bt_undo_insert_cnt;
    uint64_t bt_undo_delete_cnt;
    uint64_t bt_undo_mvcc_delete_cnt;
    uint64_t bt_undo_update_sk_cnt;
    uint64_t bt_vacuum_cnt;
    uint64_t bt_vacuum_insid_cnt;
    uint64_t bt_vacuum_update_sk_cnt;
    uint64_t bt_fix_ovf_oids_cnt;
    uint64_t bt_unique_rlocks_cnt;
    uint64_t bt_unique_wlocks_cnt;

    uint64_t bt_find_unique;
    uint64_t bt_range_search;
    uint64_t bt_insert;
    uint64_t bt_delete;
    uint64_t bt_mvcc_delete;
    uint64_t bt_mark_delete;
    uint64_t bt_update_sk;
    uint64_t bt_undo_insert;
    uint64_t bt_undo_delete;
    uint64_t bt_undo_mvcc_delete;
    uint64_t bt_undo_update_sk;
    uint64_t bt_vacuum;
    uint64_t bt_vacuum_insid;
    uint64_t bt_vacuum_update_sk;

    uint64_t bt_traverse;
    uint64_t bt_find_unique_traverse;
    uint64_t bt_range_search_traverse;
    uint64_t bt_insert_traverse;
    uint64_t bt_delete_traverse;
    uint64_t bt_mvcc_delete_traverse;
    uint64_t bt_mark_delete_traverse;
    uint64_t bt_update_sk_traverse;
    uint64_t bt_undo_insert_traverse;
    uint64_t bt_undo_delete_traverse;
    uint64_t bt_undo_mvcc_delete_traverse;
    uint64_t bt_undo_update_sk_traverse;
    uint64_t bt_vacuum_traverse;
    uint64_t bt_vacuum_insid_traverse;
    uint64_t bt_vacuum_update_sk_traverse;

    uint64_t bt_fix_ovf_oids;
    uint64_t bt_unique_rlocks;
    uint64_t bt_unique_wlocks;

    uint64_t vac_master;
    uint64_t vac_worker_process_log;
    uint64_t vac_worker_execute;

    /* Other statistics */
    uint64_t pb_hit_ratio;
    /* ((pb_num_fetches - pb_num_ioreads) x 100 / pb_num_fetches) x 100 */

    uint64_t log_hit_ratio;
    /* ((log_num_fetches - log_num_fetch_ioreads) x 100 / log_num_fetches) x 100 */

    uint64_t vacuum_data_hit_ratio;

    uint64_t pb_vacuum_efficiency;

    uint64_t pb_vacuum_fetch_ratio;

    /* MNT_SERVER_EXEC_STATS: pb_page_lock_acquire_time_10usec */
    uint64_t pb_page_lock_acquire_time_msec;
    /* MNT_SERVER_EXEC_STATS: pb_page_hold_acquire_time_10usec */
    uint64_t pb_page_hold_acquire_time_msec;
    /* MNT_SERVER_EXEC_STATS: pb_page_fix_acquire_time_10usec */
    uint64_t pb_page_fix_acquire_time_msec;

    uint64_t pb_page_allocate_time_ratio;
  } T_CM_DB_EXEC_STAT;

  int cm_get_db_proc_stat (const char *db_name, T_CM_DB_PROC_STAT * stat, T_CM_ERROR * err_buf);

  T_CM_DB_PROC_STAT_ALL *cm_get_db_proc_stat_all (T_CM_ERROR * err_buf);
  void cm_db_proc_stat_all_free (T_CM_DB_PROC_STAT_ALL * stat);


  T_CM_BROKER_PROC_STAT *cm_get_broker_proc_stat (const char *broker_name, T_CM_ERROR * err_buf);
  void cm_broker_proc_stat_free (T_CM_BROKER_PROC_STAT * stat);

  T_CM_BROKER_PROC_STAT_ALL *cm_get_broker_proc_stat_all (T_CM_ERROR * err_buf);
  void cm_broker_proc_stat_all_free (T_CM_BROKER_PROC_STAT_ALL * stat);


  int cm_get_host_stat (T_CM_HOST_STAT * stat, T_CM_ERROR * err_buf);

  T_CM_DISK_PARTITION_STAT_ALL *cm_get_host_disk_partition_stat (T_CM_ERROR * err_buf);
  void cm_host_disk_partition_stat_free (T_CM_DISK_PARTITION_STAT_ALL * stat);

  int cm_get_proc_stat (T_CM_PROC_STAT * stat, int pid);

  int cm_get_db_exec_stat (const char *db_name, T_CM_DB_EXEC_STAT * exec_stat, T_CM_ERROR * err_buf);

  int cm_get_cas_info (const char *br_name, T_CM_CAS_INFO_ALL * cas_info_all, T_CM_JOB_INFO_ALL * job_info_all,
		       T_CM_ERROR * err_buf);
  void cm_cas_info_free (T_CM_CAS_INFO_ALL * cas_info_all, T_CM_JOB_INFO_ALL * job_info_all);
  int cm_get_broker_info (T_CM_BROKER_INFO_ALL * broker_info_all, T_CM_ERROR * err_buf);
  void cm_broker_info_free (T_CM_BROKER_INFO_ALL * broker_info_all);

  int cm_broker_env_start (T_CM_ERROR * err_buf);
  int cm_broker_env_stop (T_CM_ERROR * err_buf);
  int cm_broker_as_restart (const char *br_name, int as_index, T_CM_ERROR * err_buf);
  int cm_broker_on (const char *br_name, T_CM_ERROR * err_buf);
  int cm_broker_off (const char *br_name, T_CM_ERROR * err_buf);

  int cm_get_broker_conf (T_CM_BROKER_CONF * dm_uc_conf, int *ret_mst_shmid, T_CM_ERROR * err_buf);
  void cm_broker_conf_free (T_CM_BROKER_CONF * dm_uc_conf);
  char *cm_br_conf_get_value (T_CM_BR_CONF * br_conf, const char *name);
  T_CM_BR_CONF *cm_conf_find_broker (T_CM_BROKER_CONF * uc_conf, char *br_name);
  char *cm_get_broker_file (T_UNICAS_FILE_ID uc_fid, char *buf);
  int cm_del_cas_log (char *br_name, int as_id, T_CM_ERROR * err_buf);
  char *cm_cpu_time_str (int t, char *buf);

#ifdef __cplusplus
}
#endif

#endif				/* _CM_STAT_H_ */
