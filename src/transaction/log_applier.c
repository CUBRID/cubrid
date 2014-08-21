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
 * log_applier.c : main routine of Transaction Log Applier
 *
 */

#ident "$Id$"

#if !defined (WINDOWS)
#include <unistd.h>
#endif
#include <errno.h>
#include <fcntl.h>
#if !defined (WINDOWS)
#include <sys/time.h>
#endif

#include "porting.h"
#include "utility.h"
#include "environment_variable.h"
#include "message_catalog.h"
#include "log_compress.h"
#include "parser.h"
#include "object_print.h"
#include "db.h"
#include "object_accessor.h"
#include "locator_cl.h"
#include "connection_cl.h"
#include "log_applier.h"
#include "network_interface_cl.h"
#include "transform.h"
#include "file_io.h"
#include "memory_hash.h"
#include "schema_manager.h"
#include "log_applier_sql_log.h"
#include "util_func.h"
#if !defined(WINDOWS)
#include "heartbeat.h"
#endif

#if defined(AIX)
#include <procinfo.h>
#include <sys/types.h>
#endif

#define LA_DEFAULT_CACHE_BUFFER_SIZE            100
#define LA_MAX_REPL_ITEM_WITHOUT_RELEASE_PB     50
#define LA_DEFAULT_LOG_PAGE_SIZE                4096
#define LA_GET_PAGE_RETRY_COUNT                 10
#define LA_REPL_LIST_COUNT                      50

#define LA_PAGE_DOESNOT_EXIST                   0
#define LA_PAGE_EXST_IN_ACTIVE_LOG              1
#define LA_PAGE_EXST_IN_ARCHIVE_LOG             2

#define LA_STATUS_BUSY                          1
#define LA_STATUS_IDLE                          0

#define LA_LOCK_SUFFIX                          "_lgla__lock"

#define LA_QUERY_BUF_SIZE                       2048

#define LA_MAX_REPL_ITEMS                       1000

/* for adaptive commit interval */
#define LA_NUM_DELAY_HISTORY                    10
#define LA_MAX_TOLERABLE_DELAY                  2
#define LA_REINIT_COMMIT_INTERVAL               10

#define LA_WS_CULL_MOPS_PER_APPLY               (100000)
#define LA_WS_CULL_MOPS_INTERVAL                (180)
#define LA_WS_CULL_MOPS_PER_APPLY_MIN           (100)
#define LA_WS_CULL_MOPS_INTERVAL_MIN            (2)

#define LA_LOG_IS_IN_ARCHIVE(pageid) \
  ((pageid) < la_Info.act_log.log_hdr->nxarv_pageid)

#define SIZEOF_LA_CACHE_LOG_BUFFER(io_pagesize) \
  (offsetof(LA_CACHE_BUFFER, logpage) + (io_pagesize))

#define LA_LOGAREA_SIZE (la_Info.act_log.db_logpagesize - SSIZEOF(LOG_HDRPAGE))
#define LA_LOG_READ_ADVANCE_WHEN_DOESNT_FIT(result, length, offset, pageid, pgptr) \
  do { \
    if ((offset)+(length) >= LA_LOGAREA_SIZE) { \
      if (((pgptr) = la_get_page(++(pageid))) == NULL) { \
        result = ER_IO_READ; \
      } \
      (offset) = 0; \
    } \
  } while(0)

#define LA_LOG_READ_ALIGN(result, offset, pageid, log_pgptr) \
  do { \
    (offset) = DB_ALIGN((offset), MAX_ALIGNMENT); \
    while ((offset) >= LA_LOGAREA_SIZE) { \
      if (((log_pgptr) = la_get_page(++(pageid))) == NULL) { \
        result = ER_IO_READ; \
      } \
      (offset) -= LA_LOGAREA_SIZE; \
      (offset) = DB_ALIGN((offset), MAX_ALIGNMENT); \
    } \
  } while(0)

#define LA_LOG_READ_ADD_ALIGN(result, add, offset, pageid, log_pgptr) \
  do { \
    (offset) += (add); \
    LA_LOG_READ_ALIGN(result, (offset), (pageid), (log_pgptr));   \
  } while(0)

#define LA_SLEEP(sec, usec) \
 do { \
   struct timeval sleep_time_val; \
   sleep_time_val.tv_sec = (sec); \
   sleep_time_val.tv_usec = (usec); \
   select (0, 0, 0, 0, &sleep_time_val); \
 } while(0)

typedef struct la_cache_buffer LA_CACHE_BUFFER;
struct la_cache_buffer
{
  int fix_count;
  bool recently_free;
  bool in_archive;

  LOG_PAGEID pageid;		/* Logical page of the log */
  LOG_PHY_PAGEID phy_pageid;

  LOG_PAGE logpage;		/* The actual buffered log page */
};

typedef struct la_cache_buffer_area LA_CACHE_BUFFER_AREA;
struct la_cache_buffer_area
{
  LA_CACHE_BUFFER *buffer_area;	/* log buffer area */
  LA_CACHE_BUFFER_AREA *next;	/* next area */
};

typedef struct la_cache_pb LA_CACHE_PB;
struct la_cache_pb
{
  MHT_TABLE *hash_table;	/* hash table for buffers */
  LA_CACHE_BUFFER **log_buffer;	/* buffer pool */
  int num_buffers;		/* # of buffers */
  LA_CACHE_BUFFER_AREA *buffer_area;	/* contignous area of buffers */
};

typedef struct la_act_log LA_ACT_LOG;
struct la_act_log
{
  char path[PATH_MAX];
  int log_vdes;
  LOG_PAGE *hdr_page;
  struct log_header *log_hdr;
  int db_iopagesize;
  int db_logpagesize;
};

typedef struct la_arv_log LA_ARV_LOG;
struct la_arv_log
{
  char path[PATH_MAX];
  int log_vdes;
  LOG_PAGE *hdr_page;
  struct log_arv_header *log_hdr;
  int arv_num;
};

typedef struct la_item LA_ITEM;
struct la_item
{
  LA_ITEM *next;
  LA_ITEM *prev;

  int log_type;
  int item_type;
  char *class_name;
  char *db_user;
  char *ha_sys_prm;
  DB_VALUE key;
  LOG_LSA lsa;			/* the LSA of the replication log record */
  LOG_LSA target_lsa;		/* the LSA of the target log record */
};

typedef struct la_apply LA_APPLY;
struct la_apply
{
  int tranid;
  int num_items;
  bool is_long_trans;
  LOG_LSA start_lsa;
  LOG_LSA last_lsa;
  LA_ITEM *head;
  LA_ITEM *tail;
};

typedef struct la_commit LA_COMMIT;
struct la_commit
{
  LA_COMMIT *next;
  LA_COMMIT *prev;

  int type;			/* transaction state -
				 * LOG_COMMIT or LOG_UNLOCK_COMMIT */
  int tranid;			/* transaction id */
  LOG_LSA log_lsa;		/* LSA of LOG_COMMIT or
				 * LSA of LOG_UNLOCK_COMMIT */
  time_t log_record_time;	/* commit time at the server site */
};

/* Log applier info */
typedef struct la_info LA_INFO;
struct la_info
{
  /* log info */
  char log_path[PATH_MAX];
  char loginf_path[PATH_MAX];
  LA_ACT_LOG act_log;
  LA_ARV_LOG arv_log;
  int last_file_state;
  unsigned long start_vsize;
  time_t start_time;

  /* map info */
  LOG_LSA final_lsa;		/* last processed log lsa */
  LOG_LSA committed_lsa;	/* last committed commit log lsa */
  LOG_LSA committed_rep_lsa;	/* last committed replication log lsa */
  LOG_LSA last_committed_lsa;	/* last committed commit log lsa
				 * at the beginning of the applylogdb */
  LOG_LSA last_committed_rep_lsa;	/* last committed replication log lsa
					 * at the beginning of the applylogdb */

  LA_APPLY **repl_lists;
  int repl_cnt;			/* the # of elements of repl_lists */
  int cur_repl;			/* the index of the current repl_lists */
  int total_rows;		/* the # of rows that were replicated */
  int prev_total_rows;		/* the previous # of total_rows */
  time_t log_record_time;	/* time of the last commit log record */
  LA_COMMIT *commit_head;	/* queue list head */
  LA_COMMIT *commit_tail;	/* queue list tail */
  int last_deleted_archive_num;
  /* last time that one or more archives were deleted */
  time_t last_time_archive_deleted;

  /* slave info */
  char *log_data;
  char *rec_type;
  LOG_ZIP *undo_unzip_ptr;
  LOG_ZIP *redo_unzip_ptr;
  int apply_state;
  int max_mem_size;

  /* master info */
  LA_CACHE_PB *cache_pb;
  int cache_buffer_size;
  bool last_is_end_of_record;
  bool is_end_of_record;
  int last_server_state;
  bool is_role_changed;

  /* db_ha_apply_info */
  LOG_LSA append_lsa;		/* append lsa of active log header */
  LOG_LSA eof_lsa;		/* eof lsa of active log header */
  LOG_LSA required_lsa;		/* start lsa of the first transaction
				 * to be applied */
  unsigned long insert_counter;
  unsigned long update_counter;
  unsigned long delete_counter;
  unsigned long schema_counter;
  unsigned long commit_counter;
  unsigned long fail_counter;
  time_t log_commit_time;
  bool required_lsa_changed;
  int status;
  bool is_apply_info_updated;	/* whether catalog is partially updated or not */

  int num_unflushed_insert;

  /* file lock */
  int log_path_lockf_vdes;
  int db_lockf_vdes;
};

typedef struct la_ovf_first_part LA_OVF_FIRST_PART;
struct la_ovf_first_part
{
  VPID next_vpid;
  int length;
  char data[1];			/* Really more than one */
};

typedef struct la_ovf_rest_parts LA_OVF_REST_PARTS;
struct la_ovf_rest_parts
{
  VPID next_vpid;
  char data[1];			/* Really more than one */
};

/* use overflow page list to reduce memory copy overhead. */
typedef struct la_ovf_page_list LA_OVF_PAGE_LIST;
struct la_ovf_page_list
{
  char *rec_type;		/* record type */
  char *data;			/* overflow page data: header + real data */
  int length;			/* total length of data */
  LA_OVF_PAGE_LIST *next;	/* next page */
};


typedef struct la_ha_apply_info LA_HA_APPLY_INFO;
struct la_ha_apply_info
{
  char db_name[256];
  DB_DATETIME creation_time;
  char copied_log_path[4096];
  LOG_LSA committed_lsa;	/* last committed commit log lsa */
  LOG_LSA committed_rep_lsa;	/* last committed replication log lsa */
  LOG_LSA append_lsa;		/* append lsa of active log header */
  LOG_LSA eof_lsa;		/* eof lsa of active log header */
  LOG_LSA final_lsa;		/* last processed log lsa */
  LOG_LSA required_lsa;		/* start lsa of the first transaction
				 * to be applied */
  DB_DATETIME log_record_time;
  DB_DATETIME log_commit_time;
  DB_DATETIME last_access_time;
  int status;
  INT64 insert_counter;
  INT64 update_counter;
  INT64 delete_counter;
  INT64 schema_counter;
  INT64 commit_counter;
  INT64 fail_counter;
  DB_DATETIME start_time;
};

typedef enum lp_op LP_OP;
enum lp_op
{
  LP_OP_PREFETCH = 0,
  LP_OP_SYNC,
  LP_OP_IGNORE
};
#define LP_HA_APPLIED_INFO_REFRESH_TIME         (50)	/* msec */

typedef struct lp_info LP_INFO;
struct lp_info
{
  LOG_LSA final_lsa;		/* last processed log lsa */
  LOG_LSA la_sync_lsa;		/* wait until applier's final_las == la_sync_lsa */
  LOG_LSA next_lsa;		/* ignore log until prefetcher's final_lsa == next_lsa */
  int page_distance;		/* gap between applier's final_lsa and prefetcher's final_lsa */
  LP_OP op;
  bool need_rollback;
  struct timeval last_ha_applied_info_time;
};

/* Global variable for LA */
LA_INFO la_Info;

/* Global variable for LP */
LP_INFO lp_Info;

static bool la_applier_need_shutdown = false;
static bool la_applier_shutdown_by_signal = false;
static char la_slave_db_name[DB_MAX_IDENTIFIER_LENGTH + 1];
static char la_peer_host[MAXHOSTNAMELEN + 1];

static bool la_enable_sql_logging = false;
static bool la_use_server_side_update_repl = true;

static void la_shutdown_by_signal (void);
static void la_init_ha_apply_info (LA_HA_APPLY_INFO * ha_apply_info);

static LOG_PHY_PAGEID la_log_phypageid (LOG_PAGEID logical_pageid);
static int la_log_io_open (const char *vlabel, int flags, int mode);
static int la_log_io_read (char *vname, int vdes, void *io_pgptr,
			   LOG_PHY_PAGEID pageid, int pagesize);
static int la_log_io_read_with_max_retries (char *vname, int vdes,
					    void *io_pgptr,
					    LOG_PHY_PAGEID pageid,
					    int pagesize, int retries);
static int la_find_archive_num (int *arv_log_num, LOG_PAGEID pageid);
static int la_get_range_of_archive (int arv_log_num, LOG_PAGEID * fpageid,
				    DKNPAGES * npages);
static int la_log_fetch_from_archive (LOG_PAGEID pageid, char *data);
static int la_log_fetch (LOG_PAGEID pageid, LA_CACHE_BUFFER * cache_buffer);
static int la_expand_cache_log_buffer (LA_CACHE_PB * cache_pb, int slb_cnt,
				       int slb_size);
static LA_CACHE_BUFFER *la_cache_buffer_replace (LA_CACHE_PB * cache_pb,
						 LOG_PAGEID pageid,
						 int io_pagesize,
						 int buffer_size);
static LA_CACHE_BUFFER *la_get_page_buffer (LOG_PAGEID pageid);
static LOG_PAGE *la_get_page (LOG_PAGEID pageid);
static void la_release_page_buffer (LOG_PAGEID pageid);
static void la_release_all_page_buffers (LOG_PAGEID except_pageid);
static void la_invalidate_page_buffer (LA_CACHE_BUFFER * cache_buffer);
static void la_decache_page_buffers (LOG_PAGEID from, LOG_PAGEID to);

static int la_find_required_lsa (LOG_LSA * required_lsa);

static int la_get_ha_apply_info (const char *log_path,
				 const char *prefix_name,
				 LA_HA_APPLY_INFO * ha_apply_info);
static int la_insert_ha_apply_info (DB_DATETIME * creation_time);
static int la_update_ha_apply_info_start_time (void);
static int la_get_last_ha_applied_info (void);
static int la_update_ha_last_applied_info (void);

static bool la_ignore_on_error (int errid);
static bool la_retry_on_error (int errid);
static int la_help_sprint_object (MOP mop, char *buffer, int max_length);

static LA_CACHE_PB *la_init_cache_pb (void);
static int la_init_cache_log_buffer (LA_CACHE_PB * cache_pb, int slb_cnt,
				     int slb_size);
static int la_fetch_log_hdr (LA_ACT_LOG * act_log);
static int la_find_log_pagesize (LA_ACT_LOG * act_log,
				 const char *logpath, const char *dbname,
				 bool check_charset);
static bool la_apply_pre (void);
static int la_does_page_exist (LOG_PAGEID pageid);
static int la_init_repl_lists (bool need_realloc);
static bool la_is_repl_lists_empty ();
static LA_APPLY *la_find_apply_list (int tranid);
static void la_log_copy_fromlog (char *rec_type, char *area, int length,
				 LOG_PAGEID log_pageid, PGLENGTH log_offset,
				 LOG_PAGE * log_pgptr);
static LA_ITEM *la_new_repl_item (LOG_LSA * lsa, LOG_LSA * target_lsa);
static void la_add_repl_item (LA_APPLY * apply, LA_ITEM * item);

static LA_ITEM *la_make_repl_item (LOG_PAGE * log_pgptr, int log_type,
				   int tranid, LOG_LSA * lsa);
static void la_unlink_repl_item (LA_APPLY * apply, LA_ITEM * item);
static void la_free_repl_item (LA_APPLY * apply, LA_ITEM * item);
static void la_free_all_repl_items_except_head (LA_APPLY * apply);
static void la_free_all_repl_items (LA_APPLY * apply);
static void la_free_and_add_next_repl_item (LA_APPLY * apply,
					    LA_ITEM * next_item,
					    LOG_LSA * lsa);
static void la_clear_applied_info (LA_APPLY * apply);
static void la_clear_all_repl_and_commit_list (void);

static int la_set_repl_log (LOG_PAGE * log_pgptr, int log_type, int tranid,
			    LOG_LSA * lsa);
static int la_add_unlock_commit_log (int tranid, LOG_LSA * lsa);
static int la_add_abort_log (int tranid, LOG_LSA * lsa);
static time_t la_retrieve_eot_time (LOG_PAGE * pgptr, LOG_LSA * lsa);
static int la_set_commit_log (int tranid, int rectype, LOG_LSA * lsa,
			      time_t log_record_time);
static int la_get_current (OR_BUF * buf, SM_CLASS * sm_class,
			   int bound_bit_flag, DB_OTMPL * def,
			   DB_VALUE * key, int offset_size);
static int la_disk_to_obj (MOBJ classobj, RECDES * record, DB_OTMPL * def,
			   DB_VALUE * key);
static char *la_get_zipped_data (char *undo_data, int undo_length,
				 bool is_diff, bool is_undo_zip,
				 bool is_overflow, char **rec_type,
				 char **data, int *length);
static int la_get_undoredo_diff (LOG_PAGE ** pgptr, LOG_PAGEID * pageid,
				 PGLENGTH * offset, bool * is_undo_zip,
				 char **undo_data, int *undo_length);
static int la_get_log_data (LOG_RECORD_HEADER * lrec, LOG_LSA * lsa,
			    LOG_PAGE * pgptr, unsigned int match_rcvindex,
			    unsigned int *rcvindex, void **logs,
			    char **rec_type, char **data, int *d_length);
static int la_get_overflow_recdes (LOG_RECORD_HEADER * lrec, void *logs,
				   char **area, int *length,
				   unsigned int rcvindex);
static int la_get_next_update_log (LOG_RECORD_HEADER * prev_lrec,
				   LOG_PAGE * pgptr, void **logs,
				   char **rec_type, char **data,
				   int *d_length);
static int la_get_relocation_recdes (LOG_RECORD_HEADER * lrec,
				     LOG_PAGE * pgptr,
				     unsigned int match_rcvindex, void **logs,
				     char **rec_type, char **data,
				     int *d_length);
static int la_get_recdes (LOG_LSA * lsa, LOG_PAGE * pgptr, RECDES * recdes,
			  unsigned int *rcvindex, char *log_data,
			  char *rec_type, bool * ovfyn);

static int la_flush_unflushed_insert (MOP class_mop);
static int la_apply_delete_log (LA_ITEM * item);
static int la_apply_update_log (LA_ITEM * item);
static int la_apply_update_log_server_side (LA_ITEM * item);
static int la_apply_insert_log (LA_ITEM * item);
static int la_update_query_execute (const char *sql, bool au_disable);
static int la_update_query_execute_with_values (const char *sql,
						int arg_count,
						DB_VALUE * vals,
						bool au_disable);
static int la_apply_schema_log (LA_ITEM * item);
static int la_apply_repl_log (int tranid, int rectype, LOG_LSA * commit_lsa,
			      int *total_rows, LOG_PAGEID final_pageid);
static int la_apply_commit_list (LOG_LSA * lsa, LOG_PAGEID final_pageid);
static void la_free_repl_items_by_tranid (int tranid);
static int la_log_record_process (LOG_RECORD_HEADER * lrec,
				  LOG_LSA * final, LOG_PAGE * pg_ptr);
static int la_change_state (void);
static int la_log_commit (bool update_commit_time);
static unsigned long la_get_mem_size (void);
static int la_check_mem_size (void);
static int la_check_time_commit (struct timeval *time,
				 unsigned int threshold);

static void la_init (const char *log_path, const int max_mem_size,
		     BOOT_CLIENT_TYPE type);

static int la_check_duplicated (const char *logpath, const char *dbname,
				int *lockf_vdes, int *last_deleted_arv_num);
static int la_lock_dbname (int *lockf_vdes, char *db_name, char *log_path);
static int la_unlock_dbname (int *lockf_vdes, char *db_name,
			     bool clear_owner);

static void la_shutdown (BOOT_CLIENT_TYPE type);

static int la_remove_archive_logs (const char *db_name,
				   int last_deleted_arv_num, int nxarv_num,
				   int max_arv_count_to_delete);

static LA_ITEM *la_get_next_repl_item (LA_ITEM * item, bool is_long_trans,
				       LOG_LSA * last_lsa);
static LA_ITEM *la_get_next_repl_item_from_list (LA_ITEM * item);
static LA_ITEM *la_get_next_repl_item_from_log (LA_ITEM * item,
						LOG_LSA * last_lsa);

static int la_commit_transaction (void);
static int la_find_last_deleted_arv_num (void);

static bool la_restart_on_bulk_flush_error (int errid);
static char *la_get_hostname_from_log_path (char *log_path);

static void la_delay_replica (time_t eot_time);

static float la_get_avg (int *array, int size);
static void la_get_adaptive_time_commit_interval (int *time_commit_interval,
						  int *delay_hist);

static void lp_init (const char *log_path);
static int lp_init_prefetcher (const char *database_name,
			       const char *log_path);
static int lp_get_log_record (struct log_header *final_log_hdr,
			      LA_CACHE_BUFFER ** log_buf);
static int lp_adjust_prefetcher_speed (void);
static int lp_process_log_record (struct log_header *final_log_hdr,
				  LA_CACHE_BUFFER * log_buf);
static int lp_prefetch_log_record (LOG_RECORD_HEADER * lrec,
				   LOG_LSA * final, LOG_PAGE * pg_ptr);
static int lp_prefetch_update_or_delete (LA_ITEM * item);
static int lp_prefetch_insert (LA_ITEM * item);
static int lp_get_ha_applied_info (bool is_first);
static void *lp_calc_applier_speed_thread_f (void *arg);
/*
 * la_shutdown_by_signal() - When the process catches the SIGTERM signal,
 *                                it does the shutdown process.
 *   return: none
 *
 * Note:
 *        set the "need_shutdown" flag as true, then each threads would
 *        process "shutdown"
 */
static void
la_shutdown_by_signal (void)
{
  la_applier_need_shutdown = true;
  la_applier_shutdown_by_signal = true;

  return;
}

bool
la_force_shutdown (void)
{
  return (la_applier_need_shutdown
	  || la_applier_shutdown_by_signal) ? true : false;
}

static void
la_init_ha_apply_info (LA_HA_APPLY_INFO * ha_apply_info)
{
  memset ((void *) ha_apply_info, 0, sizeof (LA_HA_APPLY_INFO));

  LSA_SET_NULL (&ha_apply_info->committed_lsa);
  LSA_SET_NULL (&ha_apply_info->committed_rep_lsa);
  LSA_SET_NULL (&ha_apply_info->append_lsa);
  LSA_SET_NULL (&ha_apply_info->eof_lsa);
  LSA_SET_NULL (&ha_apply_info->final_lsa);
  LSA_SET_NULL (&ha_apply_info->required_lsa);

  return;
}

/*
 * la_log_phypageid() - get the physical page id from the logical pageid
 *   return: physical page id
 *   logical_pageid : logical page id
 *
 * Note:
 *   active log      0, 1, 2, .... 4999   (total 5,000 pages)
 *   archive log0
 */
static LOG_PHY_PAGEID
la_log_phypageid (LOG_PAGEID logical_pageid)
{
  LOG_PHY_PAGEID phy_pageid;
  if (logical_pageid == LOGPB_HEADER_PAGE_ID)
    {
      phy_pageid = 0;
    }
  else
    {
      LOG_PAGEID tmp_pageid;

      tmp_pageid = logical_pageid - la_Info.act_log.log_hdr->fpageid;
      if (tmp_pageid >= la_Info.act_log.log_hdr->npages)
	{
	  tmp_pageid %= la_Info.act_log.log_hdr->npages;
	}
      else if (tmp_pageid < 0)
	{
	  tmp_pageid = la_Info.act_log.log_hdr->npages -
	    ((-tmp_pageid) % la_Info.act_log.log_hdr->npages);
	}
      tmp_pageid++;
      if (tmp_pageid > la_Info.act_log.log_hdr->npages)
	{
	  tmp_pageid %= la_Info.act_log.log_hdr->npages;
	}

      assert (tmp_pageid <= PAGEID_MAX);
      phy_pageid = (LOG_PHY_PAGEID) tmp_pageid;
    }

  return phy_pageid;
}


/*
 * la_log_io_read() - read a page from the disk
 *   return: error code
 *     vname(in): the volume name of the target file
 *     vdes(in): the volume descriptor of the target file
 *     io_pgptr(out): start pointer to be read
 *     pageid(in): page id to read
 *     pagesize(in): page size to wrea
 *
 * Note:
 *     reads a predefined size of page from the disk
 */
static int
la_log_io_read (char *vname, int vdes,
		void *io_pgptr, LOG_PHY_PAGEID pageid, int pagesize)
{
  return la_log_io_read_with_max_retries (vname, vdes, io_pgptr, pageid,
					  pagesize, -1);
}

/*
 * la_log_io_read_with_max_retries() - read a page from the disk with max retries
 *   return: error code
 *     vname(in): the volume name of the target file
 *     vdes(in): the volume descriptor of the target file
 *     io_pgptr(out): start pointer to be read
 *     pageid(in): page id to read
 *     pagesize(in): page size to wrea
 *     retries(in): read retry count
 *
 * Note:
 *     reads a predefined size of page from the disk
 */
static int
la_log_io_read_with_max_retries (char *vname, int vdes,
				 void *io_pgptr, LOG_PHY_PAGEID pageid,
				 int pagesize, int retries)
{
  int nbytes;
  int remain_bytes = pagesize;
  off64_t offset = ((off64_t) pagesize) * ((off64_t) pageid);
  char *current_ptr = (char *) io_pgptr;

  if (lseek64 (vdes, offset, SEEK_SET) == -1)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_IO_READ, 2, pageid, vname);
      return ER_FAILED;
    }

  while (remain_bytes > 0 && retries != 0)
    {
      retries = (retries > 0) ? retries - 1 : retries;

      /* Read the desired page */
      nbytes = read (vdes, current_ptr, remain_bytes);

      if (nbytes == 0)
	{
	  /*
	   * This is an end of file.
	   * We are trying to read beyond the allocated disk space
	   */
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE,
		  ER_PB_BAD_PAGEID, 2, pageid, vname);
	  /* TODO: wait until exist? */
	  usleep (100 * 1000);
	  continue;
	}
      else if (nbytes < 0)
	{
	  if (errno == EINTR)
	    {
	      continue;
	    }
	  else
	    {
	      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
				   ER_IO_READ, 2, pageid, vname);
	      return ER_FAILED;
	    }
	}

      remain_bytes -= nbytes;
      current_ptr += nbytes;
    }

  if (remain_bytes > 0)
    {
      if (retries <= 0 && er_errid () == ER_PB_BAD_PAGEID)
	{
	  return ER_PB_BAD_PAGEID;
	}
      else
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			       ER_IO_READ, 2, pageid, vname);
	  return ER_FAILED;
	}
    }

  return NO_ERROR;
}

/*
 * la_get_range_of_archive() -
 *   return: NO_ERROR or error code
 *   arv_log_num(in): archive log number
 *   fpageid(out): logical pageid at physical location 1 in archive log
 *   npages(out): number of pages in the archive log
 */
static int
la_get_range_of_archive (int arv_log_num, LOG_PAGEID * fpageid,
			 DKNPAGES * npages)
{
  int error = NO_ERROR;
  struct log_arv_header *log_hdr = NULL;
  int arv_log_vdes = NULL_VOLDES;
  char arv_log_path[PATH_MAX];

  static LOG_PAGE *hdr_page = NULL;

  if (hdr_page == NULL)
    {
      hdr_page = (LOG_PAGE *) malloc (la_Info.act_log.db_logpagesize);
      if (hdr_page == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, la_Info.act_log.db_logpagesize);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
    }

  /* make archive_name */
  fileio_make_log_archive_name (arv_log_path,
				la_Info.log_path,
				la_Info.act_log.log_hdr->prefix_name,
				arv_log_num);

log_reopen:
  /* open the archive file */
  arv_log_vdes = fileio_open (arv_log_path, O_RDONLY, 0);
  if (arv_log_vdes == NULL_VOLDES)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_MOUNT_FAIL, 1,
	      arv_log_path);
      return ER_LOG_MOUNT_FAIL;
    }

  error = la_log_io_read_with_max_retries (arv_log_path, arv_log_vdes,
					   hdr_page, 0,
					   la_Info.act_log.db_logpagesize,
					   10);
  if (error != NO_ERROR)
    {
      fileio_close (arv_log_vdes);

      if (error == ER_PB_BAD_PAGEID)
	{
	  goto log_reopen;
	}
      else
	{
	  er_log_debug (ARG_FILE_LINE,
			"cannot get header from archive %s.", arv_log_path);
	  return ER_LOG_READ;
	}
    }

  log_hdr = (struct log_arv_header *) hdr_page->area;
  *fpageid = log_hdr->fpageid;
  *npages = log_hdr->npages;

  fileio_close (arv_log_vdes);

  return NO_ERROR;
}

/*
 * la_find_archive_num() - get archive number with page ID
 *   return: error code
 *   arv_log_num(in/out): archive log number
 *   pageid(in): requested pageid
 */
static int
la_find_archive_num (int *arv_log_num, LOG_PAGEID pageid)
{
  int error = NO_ERROR;
  int guess_num = 0;
  struct log_arv_header *log_hdr = NULL;
  LOG_PAGEID fpageid;
  DKNPAGES npages;
  int arv_log_vdes = NULL_VOLDES;
  char arv_log_path[PATH_MAX];
  int left;
  int right;

  if (*arv_log_num == -1)
    {
      /* guess */
      guess_num = pageid / la_Info.act_log.log_hdr->npages;
      if (guess_num >= la_Info.act_log.log_hdr->nxarv_num)
	{
	  fileio_make_log_archive_name (arv_log_path,
					la_Info.log_path,
					la_Info.act_log.log_hdr->prefix_name,
					guess_num);
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_LOG_MOUNT_FAIL, 1,
		  arv_log_path);
	  guess_num = la_Info.act_log.log_hdr->nxarv_num - 1;
	}
    }
  else
    {
      /* do not guess, just check */
      guess_num = *arv_log_num;
    }

  left = MAX (0, la_Info.last_deleted_archive_num + 1);
  right = la_Info.act_log.log_hdr->nxarv_num - 1;

  guess_num = MAX (guess_num, left);
  do
    {
      if (la_Info.arv_log.log_vdes != NULL_VOLDES
	  && la_Info.arv_log.log_hdr != NULL
	  && guess_num == la_Info.arv_log.log_hdr->arv_num)
	{
	  log_hdr = la_Info.arv_log.log_hdr;
	  fpageid = log_hdr->fpageid;
	  npages = log_hdr->npages;
	}
      else
	{
	  error = la_get_range_of_archive (guess_num, &fpageid, &npages);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }
	}

      if (pageid >= fpageid && pageid < fpageid + npages)
	{
	  *arv_log_num = guess_num;
	  return NO_ERROR;
	}
      else if (pageid < fpageid)
	{
	  right = guess_num - 1;
	  guess_num = CEIL_PTVDIV ((left + right), 2);
	}
      else if (pageid >= fpageid + npages)
	{
	  left = guess_num + 1;
	  guess_num = CEIL_PTVDIV ((left + right), 2);
	}
    }
  while (guess_num >= 0 && guess_num < la_Info.act_log.log_hdr->nxarv_num
	 && left <= right);

  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_NOTIN_ARCHIVE, 1, pageid);
  return ER_LOG_NOTIN_ARCHIVE;
}

/*
 * la_log_fetch_from_archive() - read the log page from archive
 *   return: error code
 *   pageid: requested pageid
 *   data: fetched data
 */
static int
la_log_fetch_from_archive (LOG_PAGEID pageid, char *data)
{
  int error = NO_ERROR;
  int arv_log_num;
  bool need_guess = true;
  struct log_arv_header *log_hdr = NULL;
  LOG_PAGEID fpageid;
  int npages;

  if (la_Info.arv_log.log_vdes != NULL_VOLDES
      && la_Info.arv_log.log_hdr != NULL)
    {
      log_hdr = la_Info.arv_log.log_hdr;
      fpageid = log_hdr->fpageid;
      npages = log_hdr->npages;

      if (pageid >= fpageid && pageid < fpageid + npages)
	{
	  need_guess = false;
	}
    }

  if (need_guess)
    {
      arv_log_num = -1;
      error = la_find_archive_num (&arv_log_num, pageid);
      if (error < 0)
	{
	  er_log_debug (ARG_FILE_LINE,
			"cannot find archive log for %lld page.",
			(long long int) pageid);
	  return error;
	}
      if (la_Info.arv_log.arv_num != arv_log_num)
	{
	  if (la_Info.arv_log.log_vdes > 0)
	    {
	      fileio_close (la_Info.arv_log.log_vdes);
	      la_Info.arv_log.log_vdes = NULL_VOLDES;
	    }
	  la_Info.arv_log.arv_num = arv_log_num;
	}
    }

log_reopen:
  if (la_Info.arv_log.log_vdes == NULL_VOLDES)
    {
      /* make archive_name */
      fileio_make_log_archive_name (la_Info.arv_log.path,
				    la_Info.log_path,
				    la_Info.act_log.log_hdr->prefix_name,
				    la_Info.arv_log.arv_num);
      /* open the archive file */
      la_Info.arv_log.log_vdes = fileio_open (la_Info.arv_log.path,
					      O_RDONLY, 0);
      if (la_Info.arv_log.log_vdes == NULL_VOLDES)
	{
	  er_log_debug (ARG_FILE_LINE,
			"cannot open %s archive for %lld page.",
			la_Info.arv_log.path, (long long int) pageid);
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			       ER_LOG_MOUNT_FAIL, 1, la_Info.arv_log.path);
	  return ER_LOG_MOUNT_FAIL;
	}
#if defined (LA_VERBOSE_DEBUG)
      else
	{
	  er_log_debug (ARG_FILE_LINE,
			"archive (%s) has been opened for %lld page",
			la_Info.arv_log.path, (long long int) pageid);
	}
#endif
    }

  /* If this is the frist time to read archive log,
   * read the header info of the target archive
   */
  if (la_Info.arv_log.hdr_page == NULL)
    {
      la_Info.arv_log.hdr_page =
	(LOG_PAGE *) malloc (la_Info.act_log.db_logpagesize);
      if (la_Info.arv_log.hdr_page == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, la_Info.act_log.db_logpagesize);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
    }

  if (la_Info.arv_log.log_hdr == NULL
      || strncmp (la_Info.arv_log.log_hdr->magic,
		  CUBRID_MAGIC_LOG_ARCHIVE, CUBRID_MAGIC_MAX_LENGTH) != 0
      || la_Info.arv_log.log_hdr->arv_num != la_Info.arv_log.arv_num)
    {
      error = la_log_io_read_with_max_retries (la_Info.arv_log.path,
					       la_Info.arv_log.log_vdes,
					       la_Info.arv_log.hdr_page, 0,
					       la_Info.act_log.db_logpagesize,
					       10);
      if (error != NO_ERROR)
	{
	  if (error == ER_PB_BAD_PAGEID)
	    {
	      fileio_close (la_Info.arv_log.log_vdes);
	      la_Info.arv_log.log_vdes = NULL_VOLDES;
	      goto log_reopen;
	    }
	  else
	    {
	      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_LOG_READ, 3,
		      pageid, 0, la_Info.arv_log.path);
	      return ER_LOG_READ;

	    }
	}

      la_Info.arv_log.log_hdr =
	(struct log_arv_header *) la_Info.arv_log.hdr_page->area;
    }


  error = la_log_io_read_with_max_retries (la_Info.arv_log.path,
					   la_Info.arv_log.log_vdes, data,
					   (pageid -
					    la_Info.arv_log.log_hdr->fpageid +
					    1),
					   la_Info.act_log.db_logpagesize,
					   10);

  if (error != NO_ERROR)
    {
      if (error == ER_PB_BAD_PAGEID)
	{
	  fileio_close (la_Info.arv_log.log_vdes);
	  la_Info.arv_log.log_vdes = NULL_VOLDES;
	  goto log_reopen;
	}
      else
	{
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_LOG_READ, 3,
		  pageid, pageid - la_Info.arv_log.log_hdr->fpageid + 1,
		  la_Info.arv_log.path);

	  return ER_LOG_READ;
	}
    }

  return error;
}

static int
la_log_fetch (LOG_PAGEID pageid, LA_CACHE_BUFFER * cache_buffer)
{
  int error = NO_ERROR;
  LOG_PHY_PAGEID phy_pageid = NULL_PAGEID;
  int retry = 5;

  assert (cache_buffer);

  /* get the physical page id */
  phy_pageid = la_log_phypageid (pageid);

  if (la_Info.act_log.log_hdr->append_lsa.pageid < pageid)
    {
      /* check it again */
      error = la_fetch_log_hdr (&la_Info.act_log);
      if (error != NO_ERROR)
	{
	  return error;
	}

      /* check it again */
      if (la_Info.act_log.log_hdr->append_lsa.pageid < pageid)
	{
	  return ER_LOG_NOTIN_ARCHIVE;
	}
    }

  do
    {
      /* TODO: refactor read the target page */
      if (LA_LOG_IS_IN_ARCHIVE (pageid))
	{
	  /* read from the archive log file */
	  error = la_log_fetch_from_archive (pageid,
					     (char *) &cache_buffer->logpage);
	  if (error != NO_ERROR)
	    {
	      la_applier_need_shutdown = true;
	      return error;
	    }
	  cache_buffer->in_archive = true;
	}
      else
	{
	  /* read from the active log file */
	  error = la_log_io_read (la_Info.act_log.path,
				  la_Info.act_log.log_vdes,
				  &cache_buffer->logpage,
				  phy_pageid, la_Info.act_log.db_logpagesize);
	  if (error != NO_ERROR)
	    {
	      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE,
		      ER_LOG_READ, 3, pageid, phy_pageid,
		      la_Info.act_log.path);
	      return ER_LOG_READ;
	    }
	  cache_buffer->in_archive = false;
	}

      /* check the fetched page is not the target page ? */
      if (cache_buffer->logpage.hdr.logical_pageid == pageid)
	{
	  break;
	}

      /* if the master generates the log archive,
       * re-fetch the log header, try again */
      usleep (100 * 1000);
      error = la_fetch_log_hdr (&la_Info.act_log);
      if (error != NO_ERROR)
	{
	  return error;
	}
    }
  while (error == NO_ERROR && --retry > 0);

  if (retry <= 0 || la_Info.act_log.log_hdr->append_lsa.pageid < pageid)
    {
#if defined (LA_VERBOSE_DEBUG)
      /* it will nagging you */
      er_log_debug (ARG_FILE_LINE, "log pageid %d is not exist", pageid);
#endif
      return ER_LOG_NOTIN_ARCHIVE;
    }

  /* now here, we got log page : */
  cache_buffer->pageid = pageid;
  cache_buffer->phy_pageid = phy_pageid;

  return error;

}

/*
 * la_expand_cache_log_buffer() - expand cache log buffer
 *   return: NO_ERROR or ER_FAILED
 *   cache_pb : cache page buffer pointer
 *   slb_cnt : the # of cache log buffers per cache page buffer
 *   slb_size : size of CACHE_LOG_BUFFER
 *
 * Note:
 *         : Expand the cache log buffer pool with the given number of buffers.
 *         : If a zero or a negative value is given, the function expands
 *           the cache buffer pool with a default porcentage of the currently
 *           size.
 */
static int
la_expand_cache_log_buffer (LA_CACHE_PB * cache_pb, int slb_cnt, int slb_size)
{
  int error = NO_ERROR;
  int i, size, bufid, total_buffers;
  LA_CACHE_BUFFER_AREA *area = NULL;
  LA_CACHE_BUFFER **slb_log_buffer;

  assert (slb_cnt > 0);
  assert (slb_size > 0);

  size = ((slb_cnt * slb_size) + DB_SIZEOF (LA_CACHE_BUFFER_AREA));
  area = (LA_CACHE_BUFFER_AREA *) malloc (size);
  if (area == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, size);
      error = ER_OUT_OF_VIRTUAL_MEMORY;
      goto error_rtn;
    }
  memset (area, 0, size);

  total_buffers = cache_pb->num_buffers + slb_cnt;
  slb_log_buffer = realloc (cache_pb->log_buffer,
			    total_buffers * DB_SIZEOF (LA_CACHE_BUFFER *));
  if (slb_log_buffer == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, total_buffers * DB_SIZEOF (LA_CACHE_BUFFER *));
      error = ER_OUT_OF_VIRTUAL_MEMORY;
      goto error_rtn;
    }

  area->buffer_area =
    ((LA_CACHE_BUFFER *) ((char *) area + DB_SIZEOF (LA_CACHE_BUFFER_AREA)));
  area->next = cache_pb->buffer_area;
  for (i = 0, bufid = cache_pb->num_buffers; i < slb_cnt; i++, bufid++)
    {
      slb_log_buffer[bufid] =
	(LA_CACHE_BUFFER *) ((char *) area->buffer_area + slb_size * i);
    }

  cache_pb->log_buffer = slb_log_buffer;
  cache_pb->buffer_area = area;
  cache_pb->num_buffers = total_buffers;

  er_log_debug (ARG_FILE_LINE,
		"page buffer cache is expanded. (size=%d).", cache_pb->
		num_buffers);

  return error;

error_rtn:
  if (area)
    {
      free_and_init (area);
    }

  return error;
}

static LA_CACHE_BUFFER *
la_cache_buffer_replace (LA_CACHE_PB * cache_pb, LOG_PAGEID pageid,
			 int io_pagesize, int buffer_size)
{
  int error = NO_ERROR;
  LA_CACHE_BUFFER *cache_buffer = NULL;
  int i, num_recently_free, found = -1;
  static unsigned int last = 0;

  while (found < 0)
    {
      num_recently_free = 0;

      for (i = 0; i < cache_pb->num_buffers; i++)
	{
	  last = ((last + 1) % cache_pb->num_buffers);
	  cache_buffer = cache_pb->log_buffer[last];
	  if (cache_buffer->fix_count == 0)
	    {
	      if (cache_buffer->recently_free == true)
		{
		  cache_buffer->recently_free = false;
		  num_recently_free++;
		}
	      else
		{
		  found = last;
		  break;
		}
	    }
	}

      if (found >= 0)
	{
	  if (cache_buffer->pageid != 0)
	    {
	      (void) mht_rem (cache_pb->hash_table, &cache_buffer->pageid,
			      NULL, NULL);
	    }

	  cache_buffer->fix_count = 0;

	  error = la_log_fetch (pageid, cache_buffer);
	  if (error != NO_ERROR)
	    {
	      cache_buffer->pageid = 0;
	      return NULL;
	    }

	  return cache_buffer;
	}

      if (num_recently_free > 0)
	{
	  continue;
	}

      error =
	la_expand_cache_log_buffer (cache_pb, buffer_size,
				    SIZEOF_LA_CACHE_LOG_BUFFER (io_pagesize));
      if (error != NO_ERROR)
	{
	  return NULL;
	}
    }

  return NULL;
}

static LA_CACHE_BUFFER *
la_get_page_buffer (LOG_PAGEID pageid)
{
  LA_CACHE_PB *cache_pb = la_Info.cache_pb;
  LA_CACHE_BUFFER *cache_buffer = NULL;

  /* find the target page in the cache buffer */
  cache_buffer = (LA_CACHE_BUFFER *) mht_get (cache_pb->hash_table,
					      (void *) &pageid);

  if (cache_buffer == NULL)
    {
      cache_buffer =
	la_cache_buffer_replace (cache_pb, pageid,
				 la_Info.act_log.db_logpagesize,
				 la_Info.cache_buffer_size);

      if (cache_buffer == NULL
	  || cache_buffer->logpage.hdr.logical_pageid != pageid)
	{
	  return NULL;
	}

      (void) mht_rem (cache_pb->hash_table, &cache_buffer->pageid,
		      NULL, NULL);

      if (mht_put (cache_pb->hash_table, &cache_buffer->pageid, cache_buffer)
	  == NULL)
	{
	  return NULL;
	}
    }
  else
    {
      if (cache_buffer->logpage.hdr.logical_pageid != pageid)
	{
	  (void) mht_rem (cache_pb->hash_table, &cache_buffer->pageid,
			  NULL, NULL);
	  return NULL;
	}
    }

  cache_buffer->fix_count++;
  return cache_buffer;
}

static LOG_PAGE *
la_get_page (LOG_PAGEID pageid)
{
  LA_CACHE_BUFFER *cache_buffer = NULL;

  assert (pageid != NULL_PAGEID);
  if (pageid == NULL_PAGEID)
    {
      return NULL;
    }

  do
    {
      cache_buffer = la_get_page_buffer (pageid);
    }
  while (cache_buffer == NULL);	/* we must get this page */

  return &cache_buffer->logpage;
}

/*
 * la_release_page_buffer() - decrease the fix_count of the target buffer
 *   return: none
 *   pageid(in): the target page id
 *
 * Note:
 *   if cache buffer's fix_count < 0 then programing error.
 */
static void
la_release_page_buffer (LOG_PAGEID pageid)
{
  LA_CACHE_PB *cache_pb = la_Info.cache_pb;
  LA_CACHE_BUFFER *cache_buffer = NULL;

  cache_buffer = (LA_CACHE_BUFFER *) mht_get (cache_pb->hash_table,
					      (void *) &pageid);
  if (cache_buffer != NULL)
    {
      if ((--cache_buffer->fix_count) <= 0)
	{
	  cache_buffer->fix_count = 0;
	}
      cache_buffer->recently_free = true;
    }
}

/*
 * la_release_all_page_buffers() - release all page buffers
 *   except_pageid :
 *   return: none
 *
 */
static void
la_release_all_page_buffers (LOG_PAGEID except_pageid)
{
  int i;
  LA_CACHE_PB *cache_pb = la_Info.cache_pb;
  LA_CACHE_BUFFER *cache_buffer = NULL;

  /* find unfix or unused buffer */
  for (i = 0; i < cache_pb->num_buffers; i++)
    {
      cache_buffer = cache_pb->log_buffer[i];
      if (cache_buffer->pageid == except_pageid)
	{
	  continue;
	}

      if (cache_buffer->fix_count > 0)
	{
	  cache_buffer->fix_count = 0;
	  cache_buffer->recently_free = true;
	}
    }
}

/*
 * la_invalidate_page_buffer() - decrease the fix_count and drop the target buffer from cache
 *   return: none
 *   cache_buf(in): cached page buffer
 *
 * Note:
 */
static void
la_invalidate_page_buffer (LA_CACHE_BUFFER * cache_buffer)
{
  LA_CACHE_PB *cache_pb = la_Info.cache_pb;

  if (cache_buffer == NULL)
    {
      return;
    }

  if (cache_buffer->pageid != 0)
    {
      (void) mht_rem (cache_pb->hash_table, &cache_buffer->pageid, NULL,
		      NULL);
    }
  cache_buffer->fix_count = 0;
  cache_buffer->recently_free = false;
  cache_buffer->pageid = 0;
}

static void
la_decache_page_buffers (LOG_PAGEID from, LOG_PAGEID to)
{
  int i;
  LA_CACHE_PB *cache_pb = la_Info.cache_pb;
  LA_CACHE_BUFFER *cache_buffer = NULL;

  for (i = 0; i < cache_pb->num_buffers; i++)
    {
      cache_buffer = cache_pb->log_buffer[i];

      if ((cache_buffer->pageid == NULL_PAGEID)
	  || (cache_buffer->pageid == 0)
	  || (cache_buffer->pageid < from) || (cache_buffer->pageid > to))
	{
	  continue;
	}

      (void) mht_rem (cache_pb->hash_table, &cache_buffer->pageid, NULL,
		      NULL);

      cache_buffer->fix_count = 0;
      cache_buffer->recently_free = false;
      cache_buffer->pageid = 0;
    }

  return;
}

/*
 * la_find_required_lsa() - Find out the lowest required page ID
 *   return: NO_ERROR or error code
 *   required_lsa(in/out) : lowest required LSA
 *
 * Note:
 */
static int
la_find_required_lsa (LOG_LSA * required_lsa)
{
  int error = NO_ERROR;
  int i;
  LOG_LSA lowest_lsa;

  LSA_SET_NULL (&lowest_lsa);

  for (i = 0; i < la_Info.cur_repl; i++)
    {
      if (la_Info.repl_lists[i]->tranid <= 0)
	{
	  continue;
	}
      if (LSA_ISNULL (&lowest_lsa) ||
	  LSA_GT (&lowest_lsa, &la_Info.repl_lists[i]->start_lsa))
	{
	  LSA_COPY (&lowest_lsa, &la_Info.repl_lists[i]->start_lsa);
	}
    }

  if (LSA_ISNULL (&lowest_lsa))
    {
      LSA_COPY (required_lsa, &la_Info.final_lsa);
    }
  else
    {
      LSA_COPY (required_lsa, &lowest_lsa);
    }

  return error;
}

/*
 * la_get_ha_apply_info() -
 *   returns  : error code, if execution failed
 *              number of affected objects, if a success
 *
 * log_path(in) :
 * prefix_name(in) :
 * ha_apply_info(out) :
 * Note:
 */
static int
la_get_ha_apply_info (const char *log_path, const char *prefix_name,
		      LA_HA_APPLY_INFO * ha_apply_info)
{
#define LA_IN_VALUE_COUNT       2
#define LA_OUT_VALUE_COUNT      23

  int res;
  int in_value_idx, out_value_idx;
  DB_VALUE in_value[LA_IN_VALUE_COUNT];
  DB_VALUE out_value[LA_OUT_VALUE_COUNT];
  int i, col_cnt;
  char query_buf[LA_QUERY_BUF_SIZE];
  DB_DATETIME *db_time;
  DB_QUERY_ERROR query_error;
  DB_QUERY_RESULT *result = NULL;

  if (db_find_class (CT_HA_APPLY_INFO_NAME) == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  snprintf (query_buf, sizeof (query_buf), "SELECT "	/* SELECT */
	    "   db_creation_time, "	/* 2 */
	    "   committed_lsa_pageid, "	/* 4 */
	    "   committed_lsa_offset, "	/* 5 */
	    "   committed_rep_pageid, "	/* 6 */
	    "   committed_rep_offset, "	/* 7 */
	    "   append_lsa_pageid, "	/* 8 */
	    "   append_lsa_offset, "	/* 9 */
	    "   eof_lsa_pageid, "	/* 10 */
	    "   eof_lsa_offset, "	/* 11 */
	    "   final_lsa_pageid, "	/* 12 */
	    "   final_lsa_offset, "	/* 13 */
	    "   required_lsa_pageid, "	/* 14 */
	    "   required_lsa_offset, "	/* 15 */
	    "   log_record_time, "	/* 16 */
	    "   log_commit_time, "	/* 17 */
	    "   last_access_time, "	/* 18 */
	    "   insert_counter, "	/* 19 */
	    "   update_counter, "	/* 20 */
	    "   delete_counter, "	/* 21 */
	    "   schema_counter, "	/* 22 */
	    "   commit_counter, "	/* 23 */
	    "   fail_counter, "	/* 24 */
	    "   start_time "	/* 25 */
	    " FROM %s "
	    " WHERE db_name = ? and copied_log_path = ? ;",
	    CT_HA_APPLY_INFO_NAME);

  in_value_idx = 0;
  db_make_varchar (&in_value[in_value_idx++], 255,
		   (char *) prefix_name, strlen (prefix_name),
		   LANG_SYS_CODESET, LANG_SYS_COLLATION);
  db_make_varchar (&in_value[in_value_idx++], 4096,
		   (char *) log_path, strlen (log_path),
		   LANG_SYS_CODESET, LANG_SYS_COLLATION);
  assert_release (in_value_idx == LA_IN_VALUE_COUNT);

  res = db_execute_with_values (query_buf, &result, &query_error,
				in_value_idx, &in_value[0]);
  if (res > 0)
    {
      int pos, error;

      pos = db_query_first_tuple (result);
      switch (pos)
	{
	case DB_CURSOR_SUCCESS:
	  col_cnt = db_query_column_count (result);
	  assert_release (col_cnt == LA_OUT_VALUE_COUNT);

	  error = db_query_get_tuple_valuelist (result, LA_OUT_VALUE_COUNT,
						out_value);
	  if (error != NO_ERROR)
	    {
	      res = error;
	      break;
	    }

	  out_value_idx = 0;

	  /* 1. db_name */
	  strncpy (ha_apply_info->db_name, prefix_name,
		   sizeof (ha_apply_info->db_name) - 1);

	  /* 3. copied_log_path */
	  strncpy (ha_apply_info->copied_log_path, log_path,
		   sizeof (ha_apply_info->copied_log_path) - 1);

	  /* 2. creation time */
	  db_time = DB_GET_DATETIME (&out_value[out_value_idx++]);
	  ha_apply_info->creation_time.date = db_time->date;
	  ha_apply_info->creation_time.time = db_time->time;

	  /* 4 ~ 5. committed_lsa */
	  ha_apply_info->committed_lsa.pageid =
	    DB_GET_BIGINT (&out_value[out_value_idx++]);
	  ha_apply_info->committed_lsa.offset =
	    DB_GET_INTEGER (&out_value[out_value_idx++]);

	  /* 6 ~ 7. committed_rep_lsa */
	  ha_apply_info->committed_rep_lsa.pageid =
	    DB_GET_BIGINT (&out_value[out_value_idx++]);
	  ha_apply_info->committed_rep_lsa.offset =
	    DB_GET_INTEGER (&out_value[out_value_idx++]);

	  /* 8 ~ 9. append_lsa */
	  ha_apply_info->append_lsa.pageid =
	    DB_GET_BIGINT (&out_value[out_value_idx++]);
	  ha_apply_info->append_lsa.offset =
	    DB_GET_INTEGER (&out_value[out_value_idx++]);

	  /* 10 ~ 11. eof_lsa */
	  ha_apply_info->eof_lsa.pageid =
	    DB_GET_BIGINT (&out_value[out_value_idx++]);
	  ha_apply_info->eof_lsa.offset =
	    DB_GET_INTEGER (&out_value[out_value_idx++]);

	  /* 12 ~ 13. final_lsa */
	  ha_apply_info->final_lsa.pageid =
	    DB_GET_BIGINT (&out_value[out_value_idx++]);
	  ha_apply_info->final_lsa.offset =
	    DB_GET_INTEGER (&out_value[out_value_idx++]);

	  /* 14 ~ 15. required_lsa */
	  ha_apply_info->required_lsa.pageid =
	    DB_GET_BIGINT (&out_value[out_value_idx++]);
	  ha_apply_info->required_lsa.offset =
	    DB_GET_INTEGER (&out_value[out_value_idx++]);

	  /* 16. log_record_time */
	  db_time = DB_GET_DATETIME (&out_value[out_value_idx++]);
	  ha_apply_info->log_record_time.date = db_time->date;
	  ha_apply_info->log_record_time.time = db_time->time;

	  /* 17. log_commit_time */
	  db_time = DB_GET_DATETIME (&out_value[out_value_idx++]);
	  ha_apply_info->log_commit_time.date = db_time->date;
	  ha_apply_info->log_commit_time.time = db_time->time;

	  /* 18. last_access_time */
	  db_time = DB_GET_DATETIME (&out_value[out_value_idx++]);
	  ha_apply_info->last_access_time.date = db_time->date;
	  ha_apply_info->last_access_time.time = db_time->time;

	  /* status */
	  ha_apply_info->status = LA_STATUS_IDLE;

	  /* 19 ~ 24. statistics */
	  ha_apply_info->insert_counter =
	    DB_GET_BIGINT (&out_value[out_value_idx++]);
	  ha_apply_info->update_counter =
	    DB_GET_BIGINT (&out_value[out_value_idx++]);
	  ha_apply_info->delete_counter =
	    DB_GET_BIGINT (&out_value[out_value_idx++]);
	  ha_apply_info->schema_counter =
	    DB_GET_BIGINT (&out_value[out_value_idx++]);
	  ha_apply_info->commit_counter =
	    DB_GET_BIGINT (&out_value[out_value_idx++]);
	  ha_apply_info->fail_counter =
	    DB_GET_BIGINT (&out_value[out_value_idx++]);

	  /* 25. start_time */
	  db_time = DB_GET_DATETIME (&out_value[out_value_idx++]);
	  ha_apply_info->start_time.date = db_time->date;
	  ha_apply_info->start_time.time = db_time->time;

	  assert_release (out_value_idx == LA_OUT_VALUE_COUNT);

	  for (i = 0; i < LA_OUT_VALUE_COUNT; i++)
	    {
	      db_value_clear (&out_value[i]);
	    }
	  break;

	case DB_CURSOR_END:
	case DB_CURSOR_ERROR:
	default:
	  res = ER_FAILED;
	  break;
	}
    }

  db_query_end (result);
  for (i = 0; i < in_value_idx; i++)
    {
      db_value_clear (&in_value[i]);
    }

  return res;

#undef LA_IN_VALUE_COUNT
#undef LA_OUT_VALUE_COUNT
}

static int
la_insert_ha_apply_info (DB_DATETIME * creation_time)
{
#define LA_IN_VALUE_COUNT       15

  int res;
  LA_ACT_LOG *act_log;
  int i;
  int in_value_idx;
  DB_VALUE in_value[LA_IN_VALUE_COUNT];
  char query_buf[LA_QUERY_BUF_SIZE];
  const char *msg;
  FILE *fp;

  act_log = &la_Info.act_log;

  snprintf (query_buf, sizeof (query_buf), "INSERT INTO %s "	/* INSERT */
	    "( db_name, "	/* 1 */
	    "  db_creation_time, "	/* 2 */
	    "  copied_log_path, "	/* 3 */
	    "  committed_lsa_pageid, "	/* 4 */
	    "  committed_lsa_offset, "	/* 5 */
	    "  committed_rep_pageid, "	/* 6 */
	    "  committed_rep_offset, "	/* 7 */
	    "  append_lsa_pageid, "	/* 8 */
	    "  append_lsa_offset, "	/* 9 */
	    "  eof_lsa_pageid, "	/* 10 */
	    "  eof_lsa_offset, "	/* 11 */
	    "  final_lsa_pageid, "	/* 12 */
	    "  final_lsa_offset, "	/* 13 */
	    "  required_lsa_pageid, "	/* 14 */
	    "  required_lsa_offset, "	/* 15 */
	    "  log_record_time, "	/* 16 */
	    "  log_commit_time, "	/* 17 */
	    "  last_access_time, "	/* 18 */
	    "  status, "	/* 19 */
	    "  insert_counter, "	/* 20 */
	    "  update_counter, "	/* 21 */
	    "  delete_counter, "	/* 22 */
	    "  schema_counter, "	/* 23 */
	    "  commit_counter, "	/* 24 */
	    "  fail_counter, "	/* 25 */
	    "  start_time ) "	/* 26 */
	    " VALUES ( ?, "	/*  1. db_name */
	    "   ?, "		/*  2. db_creation_time */
	    "   ?, "		/*  3. copied_log_path */
	    "   ?, "		/*  4. committed_lsa_pageid */
	    "   ?, "		/*  5. committed_lsa_offset */
	    "   ?, "		/*  6. committed_rep_pageid */
	    "   ?, "		/*  7. committed_rep_offset */
	    "   ?, "		/*  8. append_lsa_pageid */
	    "   ?, "		/*  9. append_lsa_offset */
	    "   ?, "		/* 10. eof_lsa_pageid */
	    "   ?, "		/* 11. eof_lsa_offset */
	    "   ?, "		/* 12. final_lsa_pageid */
	    "   ?, "		/* 13. final_lsa_offset */
	    "   ?, "		/* 14. required_lsa_pageid */
	    "   ?, "		/* 15. required_lsa_offset */
	    "   SYS_DATETIME, "	/* 16. log_record_time */
	    "   SYS_DATETIME, "	/* 17. log_commit_time */
	    "   SYS_DATETIME, "	/* 18. last_access_time */
	    "   0, "		/* 19. status */
	    "   0, "		/* 20. insert_counter */
	    "   0, "		/* 21. update_counter */
	    "   0, "		/* 22. delete_counter */
	    "   0, "		/* 23. schema_counter */
	    "   0, "		/* 24. commit_counter */
	    "   0, "		/* 25. fail_counter */
	    "   SYS_DATETIME "	/* 26. start_time */
	    "   ) ;", CT_HA_APPLY_INFO_NAME);

  in_value_idx = 0;

  /* 1. db_name */
  db_make_varchar (&in_value[in_value_idx++], 255,
		   act_log->log_hdr->prefix_name,
		   strlen (act_log->log_hdr->prefix_name),
		   LANG_SYS_CODESET, LANG_SYS_COLLATION);

  /* 2. db_creation time */
  db_make_datetime (&in_value[in_value_idx++], creation_time);

  /* 3. copied_log_path */
  db_make_varchar (&in_value[in_value_idx++], 4096, la_Info.log_path,
		   strlen (la_Info.log_path),
		   LANG_SYS_CODESET, LANG_SYS_COLLATION);

  /* 4 ~ 5. committed_lsa */
  db_make_bigint (&in_value[in_value_idx++], la_Info.committed_lsa.pageid);
  db_make_int (&in_value[in_value_idx++], la_Info.committed_lsa.offset);

  /* 6 ~ 7. committed_rep_lsa */
  db_make_bigint (&in_value[in_value_idx++],
		  la_Info.committed_rep_lsa.pageid);
  db_make_int (&in_value[in_value_idx++], la_Info.committed_rep_lsa.offset);

  /* 8 ~ 9. append_lsa */
  db_make_bigint (&in_value[in_value_idx++], la_Info.append_lsa.pageid);
  db_make_int (&in_value[in_value_idx++], la_Info.append_lsa.offset);

  /* 10 ~ 11. eof_lsa */
  db_make_bigint (&in_value[in_value_idx++], la_Info.eof_lsa.pageid);
  db_make_int (&in_value[in_value_idx++], la_Info.eof_lsa.offset);

  /* 12 ~ 13. final_lsa */
  db_make_bigint (&in_value[in_value_idx++], la_Info.final_lsa.pageid);
  db_make_int (&in_value[in_value_idx++], la_Info.final_lsa.offset);

  /* 14 ~ 15. required_lsa */
  db_make_bigint (&in_value[in_value_idx++], la_Info.required_lsa.pageid);
  db_make_int (&in_value[in_value_idx++], la_Info.required_lsa.offset);

  assert_release (in_value_idx == LA_IN_VALUE_COUNT);

  res =
    la_update_query_execute_with_values (query_buf, in_value_idx,
					 &in_value[0], true);
  for (i = 0; i < in_value_idx; i++)
    {
      db_value_clear (&in_value[i]);
    }

  if (res <= 0)
    {
      return res;
    }

  /* create log info */
  msg = msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOG,
			MSGCAT_LOG_LOGINFO_COMMENT);
  if (msg == NULL)
    {
      msg = "COMMENT: %s for database %s\n";
    }
  fp = fopen (la_Info.loginf_path, "w");
  if (fp == NULL)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE,
	      ER_LOG_MOUNT_FAIL, 1, la_Info.loginf_path);
    }
  else
    {
      (void) fprintf (fp, msg, CUBRID_MAGIC_LOG_INFO, la_Info.loginf_path);
      fflush (fp);
      fclose (fp);
    }

  return res;

#undef LA_IN_VALUE_COUNT
}

static int
la_update_ha_apply_info_start_time (void)
{
#define LA_IN_VALUE_COUNT       2
  int res;
  LA_ACT_LOG *act_log;
  int i;
  int in_value_idx;
  DB_VALUE in_value[LA_IN_VALUE_COUNT];
  char query_buf[LA_QUERY_BUF_SIZE];

  act_log = &la_Info.act_log;

  snprintf (query_buf, sizeof (query_buf),
	    "UPDATE %s"
	    " SET start_time = SYS_DATETIME, status = 0"
	    " WHERE db_name = ? AND copied_log_path = ? ;",
	    CT_HA_APPLY_INFO_NAME);

  in_value_idx = 0;
  db_make_varchar (&in_value[in_value_idx++], 255,
		   act_log->log_hdr->prefix_name,
		   strlen (act_log->log_hdr->prefix_name),
		   LANG_SYS_CODESET, LANG_SYS_COLLATION);
  db_make_varchar (&in_value[in_value_idx++], 4096, la_Info.log_path,
		   strlen (la_Info.log_path),
		   LANG_SYS_CODESET, LANG_SYS_COLLATION);
  assert (in_value_idx == LA_IN_VALUE_COUNT);

  res = la_update_query_execute_with_values (query_buf, in_value_idx,
					     &in_value[0], true);
  la_Info.is_apply_info_updated = true;

  for (i = 0; i < in_value_idx; i++)
    {
      db_value_clear (&in_value[i]);
    }

  return res;

#undef LA_IN_VALUE_COUNT
}

static int
la_update_ha_apply_info_log_record_time (time_t new_time)
{
#define LA_IN_VALUE_COUNT       3
  int res;
  char query_buf[LA_QUERY_BUF_SIZE];
  DB_VALUE in_value[LA_IN_VALUE_COUNT];
  DB_DATETIME datetime;
  int i, in_value_idx = 0;

  er_clear ();

  snprintf (query_buf, sizeof (query_buf), "UPDATE %s "	/* UPDATE */
	    " SET "		/* SET */
	    "   log_record_time = ?, "	/* 1 */
	    "   last_access_time = SYS_DATETIME "	/* last_access_time */
	    " WHERE db_name = ? AND copied_log_path = ? ;",	/* 2 ~ 3 */
	    CT_HA_APPLY_INFO_NAME);

  /* 1. log_record_time */
  db_localdatetime (&new_time, &datetime);
  db_make_datetime (&in_value[in_value_idx++], &datetime);

  /* 2. db_name */
  db_make_varchar (&in_value[in_value_idx++], 255,
		   la_Info.act_log.log_hdr->prefix_name,
		   strlen (la_Info.act_log.log_hdr->prefix_name),
		   LANG_SYS_CODESET, LANG_SYS_COLLATION);

  /* 3. copied_log_path */
  db_make_varchar (&in_value[in_value_idx++], 4096, la_Info.log_path,
		   strlen (la_Info.log_path),
		   LANG_SYS_CODESET, LANG_SYS_COLLATION);
  assert_release (in_value_idx == LA_IN_VALUE_COUNT);

  res = la_update_query_execute_with_values (query_buf, in_value_idx,
					     &in_value[0], true);
  if (res == 0)
    {
      /* it means db_ha_apply_info was deleted */
      DB_DATETIME log_db_creation_time;

      db_localdatetime (&la_Info.act_log.log_hdr->db_creation,
			&log_db_creation_time);

      res = la_insert_ha_apply_info (&log_db_creation_time);
      if (res > 0)
	{
	  res =
	    la_update_query_execute_with_values (query_buf, in_value_idx,
						 &in_value[0], true);
	}
    }

  if (res > 0)
    {
      la_Info.log_record_time = new_time;
      la_Info.is_apply_info_updated = true;
    }

  for (i = 0; i < in_value_idx; i++)
    {
      db_value_clear (&in_value[i]);
    }

  return res;
#undef LA_IN_VALUE_COUNT
}

/*
 * la_get_last_ha_applied_info() - get last applied info
 *   return: NO_ERROR or error code
 *
 * Note:
 */
static int
la_get_last_ha_applied_info (void)
{
  int res;
  LA_ACT_LOG *act_log;
  LA_HA_APPLY_INFO apply_info;
  time_t log_db_creation;
  DB_DATETIME log_db_creation_time;
  bool insert_apply_info = false;

  act_log = &la_Info.act_log;

  log_db_creation = act_log->log_hdr->db_creation;
  db_localdatetime (&log_db_creation, &log_db_creation_time);

  res =
    la_get_ha_apply_info (la_Info.log_path, act_log->log_hdr->prefix_name,
			  &apply_info);
  if (res > 0)
    {
      LSA_COPY (&la_Info.committed_lsa, &apply_info.committed_lsa);
      LSA_COPY (&la_Info.committed_rep_lsa, &apply_info.committed_rep_lsa);
      LSA_COPY (&la_Info.append_lsa, &apply_info.append_lsa);
      LSA_COPY (&la_Info.eof_lsa, &apply_info.eof_lsa);
      LSA_COPY (&la_Info.final_lsa, &apply_info.final_lsa);
      LSA_COPY (&la_Info.required_lsa, &apply_info.required_lsa);

      la_Info.insert_counter = apply_info.insert_counter;
      la_Info.update_counter = apply_info.update_counter;
      la_Info.delete_counter = apply_info.delete_counter;
      la_Info.schema_counter = apply_info.schema_counter;
      la_Info.commit_counter = apply_info.commit_counter;
      la_Info.fail_counter = apply_info.fail_counter;

      if ((log_db_creation_time.date != apply_info.creation_time.date)
	  || (log_db_creation_time.time != apply_info.creation_time.time))
	{
	  return ER_FAILED;
	}
    }
  else if (res == 0)
    {
      insert_apply_info = true;
    }
  else
    {
      return res;
    }

  if (LSA_ISNULL (&la_Info.required_lsa))
    {
      LSA_COPY (&la_Info.required_lsa, &act_log->log_hdr->eof_lsa);
    }

  if (LSA_ISNULL (&la_Info.committed_lsa))
    {
      LSA_COPY (&la_Info.committed_lsa, &la_Info.required_lsa);
    }

  if (LSA_ISNULL (&la_Info.committed_rep_lsa))
    {
      LSA_COPY (&la_Info.committed_rep_lsa, &la_Info.required_lsa);
    }

  if (LSA_ISNULL (&la_Info.final_lsa))
    {
      LSA_COPY (&la_Info.final_lsa, &la_Info.required_lsa);
    }

  if (insert_apply_info == true)
    {
      res = la_insert_ha_apply_info (&log_db_creation_time);
    }
  else
    {
      res = la_update_ha_apply_info_start_time ();
    }

  if (res == 0)
    {
      return ER_FAILED;
    }
  else if (res < 0)
    {
      return res;
    }

  (void) db_commit_transaction ();

  LSA_COPY (&la_Info.last_committed_lsa, &la_Info.committed_lsa);
  LSA_COPY (&la_Info.last_committed_rep_lsa, &la_Info.committed_rep_lsa);

  return NO_ERROR;
}

/*
 * la_update_ha_last_applied_info() - update db_ha_apply_info table
 *   returns  : error code, if execution failed
 *              number of affected objects, if a success
 *
 * Note:
 *     called by APPLY thread
 */
static int
la_update_ha_last_applied_info (void)
{
#define LA_IN_VALUE_COUNT       22
  int res;
  char query_buf[LA_QUERY_BUF_SIZE];
  DB_VALUE in_value[LA_IN_VALUE_COUNT];
  DB_DATETIME datetime;
  int i, in_value_idx;

  er_clear ();

  snprintf (query_buf, sizeof (query_buf), "UPDATE %s "	/* UPDATE */
	    " SET "		/* SET */
	    "   committed_lsa_pageid = ?, "	/* 1 */
	    "   committed_lsa_offset = ?, "	/* 2 */
	    "   committed_rep_pageid = ?, "	/* 3 */
	    "   committed_rep_offset = ?, "	/* 4 */
	    "   append_lsa_pageid = ?, "	/* 5 */
	    "   append_lsa_offset = ?, "	/* 6 */
	    "   eof_lsa_pageid = ?, "	/* 7 */
	    "   eof_lsa_offset = ?, "	/* 8 */
	    "   final_lsa_pageid = ?, "	/* 9 */
	    "   final_lsa_offset = ?, "	/* 10 */
	    "   required_lsa_pageid = ?, "	/* 11 */
	    "   required_lsa_offset = ?, "	/* 12 */
	    "   log_record_time = IFNULL(?, log_record_time), "	/* 13 */
	    "   log_commit_time = IFNULL(?, log_commit_time), "	/* 14 */
	    "   last_access_time = SYS_DATETIME, "	/* */
	    "   insert_counter = ?, "	/* 15 */
	    "   update_counter = ?, "	/* 16 */
	    "   delete_counter = ?, "	/* 17 */
	    "   schema_counter = ?, "	/* 18 */
	    "   commit_counter = ?, "	/* 19 */
	    "   fail_counter = ? "	/* 20 */
	    " WHERE db_name = ? AND copied_log_path = ? ;",	/* 21 ~ 22 */
	    CT_HA_APPLY_INFO_NAME);

  in_value_idx = 0;

  /* 1 ~ 2. committed_lsa */
  if (LSA_GE (&la_Info.committed_lsa, &la_Info.last_committed_lsa))
    {
      db_make_bigint (&in_value[in_value_idx++],
		      la_Info.committed_lsa.pageid);
      db_make_int (&in_value[in_value_idx++], la_Info.committed_lsa.offset);
    }
  else
    {
      db_make_bigint (&in_value[in_value_idx++],
		      la_Info.last_committed_lsa.pageid);
      db_make_int (&in_value[in_value_idx++],
		   la_Info.last_committed_lsa.offset);
    }

  /* 3 ~ 4. committed_rep_lsa */
  if (LSA_GE (&la_Info.committed_rep_lsa, &la_Info.last_committed_rep_lsa))
    {
      db_make_bigint (&in_value[in_value_idx++],
		      la_Info.committed_rep_lsa.pageid);
      db_make_int (&in_value[in_value_idx++],
		   la_Info.committed_rep_lsa.offset);
    }
  else
    {
      db_make_bigint (&in_value[in_value_idx++],
		      la_Info.last_committed_rep_lsa.pageid);
      db_make_int (&in_value[in_value_idx++],
		   la_Info.last_committed_rep_lsa.offset);
    }

  /* 5 ~ 6. append_lsa */
  db_make_bigint (&in_value[in_value_idx++], la_Info.append_lsa.pageid);
  db_make_int (&in_value[in_value_idx++], la_Info.append_lsa.offset);

  /* 7 ~ 8. eof_lsa */
  db_make_bigint (&in_value[in_value_idx++], la_Info.eof_lsa.pageid);
  db_make_int (&in_value[in_value_idx++], la_Info.eof_lsa.offset);


  /* 9 ~ 10. final_lsa */
  db_make_bigint (&in_value[in_value_idx++], la_Info.final_lsa.pageid);
  db_make_int (&in_value[in_value_idx++], la_Info.final_lsa.offset);

  /* 11 ~ 12. required_lsa */
  db_make_bigint (&in_value[in_value_idx++], la_Info.required_lsa.pageid);
  db_make_int (&in_value[in_value_idx++], la_Info.required_lsa.offset);

  /* 13. log_record_time */
  if (la_Info.log_record_time)
    {
      db_localdatetime (&la_Info.log_record_time, &datetime);
      db_make_datetime (&in_value[in_value_idx++], &datetime);
    }
  else
    {
      db_make_null (&in_value[in_value_idx++]);
    }

  /* 14. log_commit_time */
  if (la_Info.log_commit_time)
    {
      db_localdatetime (&la_Info.log_commit_time, &datetime);
      db_make_datetime (&in_value[in_value_idx++], &datetime);
    }
  else
    {
      db_make_null (&in_value[in_value_idx++]);
    }

  /* 15 ~ 20. counter */
  db_make_bigint (&in_value[in_value_idx++], la_Info.insert_counter);
  db_make_bigint (&in_value[in_value_idx++], la_Info.update_counter);
  db_make_bigint (&in_value[in_value_idx++], la_Info.delete_counter);
  db_make_bigint (&in_value[in_value_idx++], la_Info.schema_counter);
  db_make_bigint (&in_value[in_value_idx++], la_Info.commit_counter);
  db_make_bigint (&in_value[in_value_idx++], la_Info.fail_counter);

  /* 21. db_name */
  db_make_varchar (&in_value[in_value_idx++], 255,
		   la_Info.act_log.log_hdr->prefix_name,
		   strlen (la_Info.act_log.log_hdr->prefix_name),
		   LANG_SYS_CODESET, LANG_SYS_COLLATION);

  /* 22. copied_log_path */
  db_make_varchar (&in_value[in_value_idx++], 4096, la_Info.log_path,
		   strlen (la_Info.log_path),
		   LANG_SYS_CODESET, LANG_SYS_COLLATION);
  assert_release (in_value_idx == LA_IN_VALUE_COUNT);

  res = la_update_query_execute_with_values (query_buf, in_value_idx,
					     &in_value[0], true);
  if (res == 0)
    {
      /* it means db_ha_apply_info was deleted */
      DB_DATETIME log_db_creation_time;

      db_localdatetime (&la_Info.act_log.log_hdr->db_creation,
			&log_db_creation_time);

      res = la_insert_ha_apply_info (&log_db_creation_time);
      if (res > 0)
	{
	  res =
	    la_update_query_execute_with_values (query_buf, in_value_idx,
						 &in_value[0], true);
	}
    }

  for (i = 0; i < in_value_idx; i++)
    {
      db_value_clear (&in_value[i]);
    }

  return res;
#undef LA_IN_VALUE_COUNT
}

static bool
la_ignore_on_error (int errid)
{
  assert_release (errid != NO_ERROR);

  errid = abs (errid);

  if (sysprm_find_err_in_integer_list (PRM_ID_HA_APPLYLOGDB_IGNORE_ERROR_LIST,
				       errid))
    {
      return true;
    }

  return false;
}

/*
 * la_restart_on_bulk_flush_error() -
 *   return: whether to restart or not for a given error
 *
 * Note:
 *     this function is essentially the same as
 *     la_retry_on_error but it is used when checking
 *     error while bulk flushing
 */
static bool
la_restart_on_bulk_flush_error (int errid)
{
  if (la_ignore_on_error (errid))
    {
      return false;
    }

  return la_retry_on_error (errid);
}

static bool
la_retry_on_error (int errid)
{
  assert_release (errid != NO_ERROR);

  if (LA_RETRY_ON_ERROR (errid))
    {
      return true;
    }

  errid = abs (errid);
  if (sysprm_find_err_in_integer_list (PRM_ID_HA_APPLYLOGDB_RETRY_ERROR_LIST,
				       errid))
    {
      return true;
    }

  return false;
}

static int
la_help_sprint_object (MOP mop, char *buffer, int max_length)
{
  int error = NO_ERROR;
  int i;
  char *p, *last_p;
  MOP class_mop;
  SM_CLASS *class_;
  SM_CLASS_CONSTRAINT *cons;
  SM_ATTRIBUTE *attr;
  DB_VALUE value;

  p = buffer;
  last_p = buffer + max_length;

  if (mop->object == NULL)
    {
      return 0;
    }

  class_mop = mop->class_mop;
  if (class_mop == NULL)
    {
      return 0;
    }

  error = au_fetch_class (class_mop, &class_, AU_FETCH_READ, AU_SELECT);
  if (error != NO_ERROR)
    {
      return 0;
    }

  cons = classobj_find_class_primary_key (class_);
  if (cons == NULL)
    {
      return 0;
    }

  if (cons->attributes[1] != NULL)
    {
      /* multi-column key */
      if (p >= last_p)
	{
	  goto end;
	}
      p += snprintf (p, max_length - (p - buffer), "{");
      for (i = 0; ((attr = cons->attributes[i]) != NULL); i++)
	{
	  if (i != 0)
	    {
	      if (p >= last_p)
		{
		  goto end;
		}
	      p += snprintf (p, max_length - (p - buffer), ", ");
	    }

	  if (obj_get (mop, attr->header.name, &value) == NO_ERROR)
	    {
	      if (p >= last_p)
		{
		  pr_clear_value (&value);
		  goto end;
		}
	      p += help_sprint_value (&value, p, max_length - (p - buffer));

	      pr_clear_value (&value);
	    }
	}

      if (p >= last_p)
	{
	  goto end;
	}
      p += snprintf (p, max_length - (p - buffer), "}");
    }
  else
    {
      /* single-column key */
      attr = cons->attributes[0];
      if (attr == NULL)
	{
	  assert (false);
	  return 0;
	}

      if (obj_get (mop, attr->header.name, &value) == NO_ERROR)
	{
	  p += help_sprint_value (&value, p, max_length - (p - buffer));
	  pr_clear_value (&value);
	}
    }

end:
  return (p - buffer);
}

/*
 * la_init_cache_pb() - initialize the cache page buffer area
 *   return: the allocated pointer to a cache page buffer
 *
 * Note:
 */
static LA_CACHE_PB *
la_init_cache_pb (void)
{
  LA_CACHE_PB *cache_pb;

  cache_pb = (LA_CACHE_PB *) malloc (DB_SIZEOF (LA_CACHE_PB));
  if (cache_pb == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      DB_SIZEOF (LA_CACHE_PB));
      return NULL;
    }

  cache_pb->hash_table = NULL;
  cache_pb->log_buffer = NULL;
  cache_pb->num_buffers = 0;
  cache_pb->buffer_area = NULL;

  return (cache_pb);
}

/*
 * la_init_cache_log_buffer() - Initialize the cache log buffer area of
 *                                a cache page buffer
 *   return: NO_ERROR or ER_OUT_OF_VIRTUAL_MEMORY
 *   cache_pb : cache page buffer pointer
 *   slb_cnt : the # of cache log buffers per cache page buffer
 *   slb_size : size of CACHE_LOG_BUFFER
 *
 * Note:
 *         : allocate the cache page buffer area
 *         : the size of page buffer area is determined after reading the
 *           log header, so we split the "initialize" and "allocate" phase.
 */
static int
la_init_cache_log_buffer (LA_CACHE_PB * cache_pb, int slb_cnt, int slb_size)
{
  int error = NO_ERROR;

  error = la_expand_cache_log_buffer (cache_pb, slb_cnt, slb_size);
  if (error != NO_ERROR)
    {
      return error;
    }

  cache_pb->hash_table =
    mht_create ("log applier cache log buffer hash table",
		cache_pb->num_buffers * 8, mht_logpageidhash,
		mht_compare_logpageids_are_equal);
  if (cache_pb->hash_table == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      cache_pb->num_buffers * 8);
      error = ER_OUT_OF_VIRTUAL_MEMORY;
    }

  return error;
}

static int
la_fetch_log_hdr (LA_ACT_LOG * act_log)
{
  int error = NO_ERROR;

  error = la_log_io_read (act_log->path, act_log->log_vdes,
			  (void *) act_log->hdr_page, 0,
			  act_log->db_logpagesize);
  if (error != NO_ERROR)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_LOG_READ, 3, 0, 0, act_log->path);
      return ER_LOG_READ;
    }

  act_log->log_hdr = (struct log_header *) (act_log->hdr_page->area);

  return error;
}

static int
la_find_log_pagesize (LA_ACT_LOG * act_log,
		      const char *logpath, const char *dbname,
		      bool check_charset)
{
  int error = NO_ERROR;

  /* set active log name */
  fileio_make_log_active_name (act_log->path, logpath, dbname);

  /* read active log file to get the io page size */
  /* wait until act_log is opened */
  do
    {
      act_log->log_vdes = fileio_open (act_log->path, O_RDONLY, 0);
      if (act_log->log_vdes == NULL_VOLDES)
	{
	  er_log_debug (ARG_FILE_LINE,
			"Active log file(%s) is not exist. waiting...",
			act_log->path);
	  /* TODO: is it error? */
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			       ER_LOG_MOUNT_FAIL, 1, act_log->path);
	  error = ER_LOG_MOUNT_FAIL;

	  LA_SLEEP (0, 200 * 1000);
	}
      else
	{
	  error = NO_ERROR;
	  break;
	}
    }
  while (la_applier_need_shutdown == false);

  if (error != NO_ERROR)
    {
      return error;
    }

  act_log->hdr_page = (LOG_PAGE *) malloc (LA_DEFAULT_LOG_PAGE_SIZE);
  if (act_log->hdr_page == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_OUT_OF_VIRTUAL_MEMORY, 1, LA_DEFAULT_LOG_PAGE_SIZE);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  do
    {
      error = la_log_io_read (act_log->path, act_log->log_vdes,
			      (char *) act_log->hdr_page, 0,
			      LA_DEFAULT_LOG_PAGE_SIZE);
      if (error != NO_ERROR)
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_LOG_READ, 3, 0, 0, act_log->path);
	  return error;
	}

      act_log->log_hdr = (struct log_header *) act_log->hdr_page->area;

      /* check if the log header is valid */
      if (strncmp (act_log->log_hdr->magic,
		   CUBRID_MAGIC_LOG_ACTIVE, CUBRID_MAGIC_MAX_LENGTH) != 0)
	{
	  /* The active log is formatting by the copylogdb. */
	  er_log_debug (ARG_FILE_LINE,
			"Active log file(%s) isn't prepared. waiting...",
			act_log->path);

	  LA_SLEEP (0, 200 * 1000);
	  continue;
	}

      /* The active log header is corrupted. */
      if (act_log->log_hdr->prefix_name[0] != '\0'
	  && strncmp (act_log->log_hdr->prefix_name,
		      dbname, strlen (dbname)) != 0)
	{
	  la_applier_need_shutdown = true;

	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_LOG_PAGE_CORRUPTED, 1, 0);
	  return ER_LOG_PAGE_CORRUPTED;
	}
      else if (check_charset
	       && act_log->log_hdr->db_charset != lang_charset ())
	{
	  char err_msg[ERR_MSG_SIZE];

	  la_applier_need_shutdown = true;
	  snprintf (err_msg, sizeof (err_msg) - 1,
		    "Active log file(%s) charset is not valid (%s), "
		    "expecting %s.", act_log->path,
		    lang_charset_cubrid_name (act_log->log_hdr->db_charset),
		    lang_charset_cubrid_name (lang_charset ()));
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOC_INIT, 1, err_msg);
	  return ER_LOC_INIT;
	}
      else
	{
	  error = NO_ERROR;
	  break;
	}
    }
  while (la_applier_need_shutdown == false);

  if (error != NO_ERROR)
    {
      return error;
    }

  act_log->db_iopagesize = act_log->log_hdr->db_iopagesize;
  act_log->db_logpagesize = act_log->log_hdr->db_logpagesize;
  /* check iopage size is valid */
  if (act_log->db_logpagesize < IO_MIN_PAGE_SIZE
      || act_log->db_logpagesize > IO_MAX_PAGE_SIZE)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_PAGE_CORRUPTED, 1, 0);
      return ER_LOG_PAGE_CORRUPTED;
    }
  else if (act_log->db_logpagesize > LA_DEFAULT_LOG_PAGE_SIZE)
    {
      act_log->hdr_page = (LOG_PAGE *) realloc (act_log->hdr_page,
						act_log->db_logpagesize);
      act_log->log_hdr = (struct log_header *) act_log->hdr_page->area;
    }

  return error;
}


static bool
la_apply_pre (void)
{
  LSA_COPY (&la_Info.final_lsa, &la_Info.committed_lsa);

  if (la_Info.log_data == NULL)
    {
      la_Info.log_data = (char *) malloc (la_Info.act_log.db_iopagesize);
      if (la_Info.log_data == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_OUT_OF_VIRTUAL_MEMORY, 1, la_Info.act_log.db_iopagesize);
	  return false;
	}
    }

  if (la_Info.rec_type == NULL)
    {
      la_Info.rec_type = (char *) malloc (DB_SIZEOF (INT16));
      if (la_Info.rec_type == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_OUT_OF_VIRTUAL_MEMORY, 1, DB_SIZEOF (INT16));
	  return false;
	}
    }

  if (la_Info.undo_unzip_ptr == NULL)
    {
      la_Info.undo_unzip_ptr =
	log_zip_alloc (la_Info.act_log.db_iopagesize, false);
      if (la_Info.undo_unzip_ptr == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_OUT_OF_VIRTUAL_MEMORY, 1, la_Info.act_log.db_iopagesize);
	  return false;
	}
    }

  if (la_Info.redo_unzip_ptr == NULL)
    {
      la_Info.redo_unzip_ptr =
	log_zip_alloc (la_Info.act_log.db_iopagesize, false);
      if (la_Info.redo_unzip_ptr == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_OUT_OF_VIRTUAL_MEMORY, 1, la_Info.act_log.db_iopagesize);
	  return false;
	}
    }

  return true;
}

/*
 * la_does_page_exist() - check whether page is exist
 *   return:
 *     pageid(in): the target page id
 *
 * Note
 */
static int
la_does_page_exist (LOG_PAGEID pageid)
{
  LA_CACHE_BUFFER *log_buffer;
  int log_exist = LA_PAGE_DOESNOT_EXIST;

  log_buffer = la_get_page_buffer (pageid);
  if (log_buffer != NULL)
    {
      if (log_buffer->pageid == pageid
	  && log_buffer->logpage.hdr.logical_pageid == pageid
	  && log_buffer->logpage.hdr.offset > NULL_OFFSET)
	{
	  log_exist = log_buffer->in_archive ?
	    LA_PAGE_EXST_IN_ARCHIVE_LOG : LA_PAGE_EXST_IN_ACTIVE_LOG;

	  la_release_page_buffer (pageid);
	}
      else
	{
	  la_invalidate_page_buffer (log_buffer);
	}
    }

  return log_exist;
}

/*
 * la_init_repl_lists() - Initialize the replication lists
 *   return: NO_ERROR or error code
 *   need_realloc : yes when realloc
 *
 * Note:
 *         repl_lists is an array of replication items to be applied.
 *         We maintain repl_lists for a transaction.
 *         This function initialize the repl_list.
 */
static int
la_init_repl_lists (bool need_realloc)
{
  int i, j;
  int error = NO_ERROR;

  if (need_realloc == false)
    {
      la_Info.repl_lists =
	malloc ((DB_SIZEOF (LA_APPLY *) * LA_REPL_LIST_COUNT));
      la_Info.repl_cnt = LA_REPL_LIST_COUNT;
      la_Info.cur_repl = 0;
      j = 0;
    }
  else
    {
      la_Info.repl_lists = realloc (la_Info.repl_lists,
				    (DB_SIZEOF (LA_APPLY *) *
				     (LA_REPL_LIST_COUNT +
				      la_Info.repl_cnt)));
      j = la_Info.repl_cnt;
      la_Info.repl_cnt += LA_REPL_LIST_COUNT;
    }

  if (la_Info.repl_lists == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      DB_SIZEOF (LA_APPLY *) * (LA_REPL_LIST_COUNT +
					la_Info.repl_cnt));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  for (i = j; i < la_Info.repl_cnt; i++)
    {
      la_Info.repl_lists[i] = malloc (DB_SIZEOF (LA_APPLY));
      if (la_Info.repl_lists[i] == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_OUT_OF_VIRTUAL_MEMORY, 1, DB_SIZEOF (LA_APPLY));
	  error = ER_OUT_OF_VIRTUAL_MEMORY;
	  break;
	}
      la_Info.repl_lists[i]->tranid = 0;
      la_Info.repl_lists[i]->num_items = 0;
      la_Info.repl_lists[i]->is_long_trans = false;
      LSA_SET_NULL (&la_Info.repl_lists[i]->start_lsa);
      LSA_SET_NULL (&la_Info.repl_lists[i]->last_lsa);
      la_Info.repl_lists[i]->head = NULL;
      la_Info.repl_lists[i]->tail = NULL;
    }

  if (error != NO_ERROR)
    {
      for (j = 0; j < i; j++)
	{
	  free_and_init (la_Info.repl_lists[i]);
	}
      free_and_init (la_Info.repl_lists);
      return error;
    }

  return error;
}

/*
 * la_is_repl_lists_empty() -
 *
 *   return: whether repl_lists is empty or not
 */
static bool
la_is_repl_lists_empty ()
{
  int i;

  for (i = 0; i < la_Info.cur_repl; i++)
    {
      if (la_Info.repl_lists[i]->num_items > 0)
	{
	  return false;
	}
    }
  return true;
}

/*
 * la_find_apply_list() - return the apply list for the target
 *                             transaction id
 *   return: pointer to the target apply list
 *   tranid(in): the target transaction id
 *
 * Note:
 */
static LA_APPLY *
la_find_apply_list (int tranid)
{
  int i;
  for (i = 0; i < la_Info.cur_repl; i++)
    {
      if (la_Info.repl_lists[i]->tranid == tranid)
	{
	  return la_Info.repl_lists[i];
	}
    }
  return NULL;
}

/*
 * la_add_apply_list() - return the apply list for the target
 *                             transaction id
 *   return: pointer to the target apply list
 *   tranid(in): the target transaction id
 *
 * Note:
 *     When we apply the transaction logs to the slave, we have to take them
 *     in turns of commit order.
 *     So, each slave maintains the apply list per transaction.
 *     And an apply list has one or more replication item.
 *     When the APPLY thread meets the "LOG COMMIT" record, it finds out
 *     the apply list of the target transaction, and apply the replication
 *     items to the slave orderly.
 */
static LA_APPLY *
la_add_apply_list (int tranid)
{
  int i;
  int free_index = -1;
  LA_APPLY *find_apply = NULL;

  find_apply = la_find_apply_list (tranid);
  if (find_apply != NULL)
    {
      return find_apply;
    }

  /* find out the matched index */
  for (i = 0; i < la_Info.cur_repl; i++)
    {
      /* retreive the free index  for the laster use */
      if (la_Info.repl_lists[i]->tranid == 0)
	{
	  free_index = i;
	  break;
	}
    }

  /* not matched, but we have free space */
  if (free_index >= 0)
    {
      la_Info.repl_lists[free_index]->tranid = tranid;
      return la_Info.repl_lists[free_index];
    }

  /* not matched, no free space */
  if (la_Info.cur_repl == la_Info.repl_cnt)
    {
      /* array is full --> realloc */
      if (la_init_repl_lists (true) == NO_ERROR)
	{
	  la_Info.repl_lists[la_Info.cur_repl]->tranid = tranid;
	  la_Info.cur_repl++;
	  return la_Info.repl_lists[la_Info.cur_repl - 1];
	}
      return NULL;
    }

  /* mot matched, no free space, array is not full */
  la_Info.repl_lists[la_Info.cur_repl]->tranid = tranid;
  la_Info.cur_repl++;
  return la_Info.repl_lists[la_Info.cur_repl - 1];
}

/*
 * la_log_copy_fromlog() - copy a portion of the log
 *   return: none
 *   rec_type(out)
 *   area: Area where the portion of the log is copied.
 *               (Set as a side effect)
 *   length: the length to copy (type change PGLENGTH -> int)
 *   log_pageid: log page identifier of the log data to copy
 *               (May be set as a side effect)
 *   log_offset: log offset within the log page of the log data to copy
 *               (May be set as a side effect)
 *   log_pgptr: the buffer containing the log page
 *               (May be set as a side effect)
 *
 * Note:
 *   Copy "length" bytes of the log starting at log_pageid,
 *   log_offset onto the given area.
 *
 *   area is set as a side effect.
 *   log_pageid, log_offset, and log_pgptr are set as a side effect.
 */
static void
la_log_copy_fromlog (char *rec_type, char *area, int length,
		     LOG_PAGEID log_pageid, PGLENGTH log_offset,
		     LOG_PAGE * log_pgptr)
{
  int rec_length = (int) sizeof (INT16);
  int copy_length;		/* Length to copy into area */
  int t_length;			/* target length  */
  int area_offset = 0;		/* The area offset */
  int error = NO_ERROR;
  LOG_PAGE *pg;

  pg = log_pgptr;

  /* filter the record type */
  /* NOTES : in case of overflow page, we don't need to fetch the rectype */
  while (rec_type != NULL && rec_length > 0)
    {
      LA_LOG_READ_ADVANCE_WHEN_DOESNT_FIT (error, 0, log_offset, log_pageid,
					   pg);
      if (pg == NULL)
	{
	  /* TODO: huh? what happend */
	  break;
	}

      copy_length = ((log_offset + rec_length <= LA_LOGAREA_SIZE) ?
		     rec_length : LA_LOGAREA_SIZE - log_offset);
      memcpy (rec_type + area_offset, (char *) (pg)->area + log_offset,
	      copy_length);
      rec_length -= copy_length;
      area_offset += copy_length;
      log_offset += copy_length;
      length = length - DB_SIZEOF (INT16);
    }

  area_offset = 0;
  t_length = length;

  /* The log data is not contiguous */
  while (t_length > 0)
    {
      LA_LOG_READ_ADVANCE_WHEN_DOESNT_FIT (error, 0, log_offset, log_pageid,
					   pg);
      if (pg == NULL)
	{
	  /* TODO: huh? what happend */
	  break;
	}
      copy_length = ((log_offset + t_length <= LA_LOGAREA_SIZE) ?
		     t_length : LA_LOGAREA_SIZE - log_offset);
      memcpy (area + area_offset, (char *) (pg)->area + log_offset,
	      copy_length);
      t_length -= copy_length;
      area_offset += copy_length;
      log_offset += copy_length;
    }
}

static LA_ITEM *
la_new_repl_item (LOG_LSA * lsa, LOG_LSA * target_lsa)
{
  LA_ITEM *item;

  item = malloc (DB_SIZEOF (LA_ITEM));
  if (item == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_OUT_OF_VIRTUAL_MEMORY, 1, DB_SIZEOF (LA_ITEM));
      return NULL;
    }

  item->log_type = -1;
  item->item_type = -1;
  item->class_name = NULL;
  item->db_user = NULL;
  item->ha_sys_prm = NULL;
  LSA_COPY (&item->lsa, lsa);
  LSA_COPY (&item->target_lsa, target_lsa);
  item->next = NULL;
  item->prev = NULL;

  return item;
}

/*
 * la_add_repl_item() - add the replication item into the apply list
 *   return: NO_ERROR or error code
 *   apply(in/out): log apply list
 *   lsa(in): the target LSA of the log
 *
 * Note:
 */
static void
la_add_repl_item (LA_APPLY * apply, LA_ITEM * item)
{
  assert (apply);
  assert (item);

  item->next = NULL;
  item->prev = apply->tail;

  if (apply->tail)
    {
      apply->tail->next = item;
    }
  else
    {
      apply->head = item;
    }
  apply->tail = item;

  apply->num_items++;
  return;
}

static LA_ITEM *
la_make_repl_item (LOG_PAGE * log_pgptr, int log_type, int tranid,
		   LOG_LSA * lsa)
{
  int error = NO_ERROR;
  LA_ITEM *item = NULL;
  struct log_replication *repl_log;
  LOG_PAGE *repl_log_pgptr;
  PGLENGTH offset;
  LOG_PAGEID pageid;
  int length;			/* type change PGLENGTH -> int */
  char *ptr;

  char *str_value;
  char *area;

  repl_log_pgptr = log_pgptr;
  pageid = lsa->pageid;
  offset = DB_SIZEOF (LOG_RECORD_HEADER) + lsa->offset;
  length = DB_SIZEOF (struct log_replication);

  LA_LOG_READ_ALIGN (error, offset, pageid, repl_log_pgptr);
  if (error != NO_ERROR)
    {
      return NULL;
    }

  LA_LOG_READ_ADVANCE_WHEN_DOESNT_FIT (error, length, offset, pageid,
				       repl_log_pgptr);
  if (error != NO_ERROR)
    {
      return NULL;
    }

  repl_log =
    (struct log_replication *) ((char *) repl_log_pgptr->area + offset);
  offset += length;
  length = repl_log->length;

  LA_LOG_READ_ALIGN (error, offset, pageid, repl_log_pgptr);
  if (error != NO_ERROR)
    {
      return NULL;
    }

  area = (char *) malloc (length);
  if (area == NULL)
    {
      return NULL;
    }

  (void) la_log_copy_fromlog (NULL, area, length, pageid, offset,
			      repl_log_pgptr);

  item = la_new_repl_item (lsa, &repl_log->lsa);
  if (item == NULL)
    {
      goto error_return;
    }

  switch (log_type)
    {
    case LOG_REPLICATION_DATA:
      ptr = or_unpack_string (area, &item->class_name);
      ptr = or_unpack_mem_value (ptr, &item->key);	/*
							 * domain is only
							 * used to pack/unpack
							 * primary key DB value
							 */
      item->item_type = repl_log->rcvindex;

      break;

    case LOG_REPLICATION_SCHEMA:
      ptr = or_unpack_int (area, &item->item_type);
      ptr = or_unpack_string (ptr, &item->class_name);
      ptr = or_unpack_string (ptr, &str_value);
      db_make_string (&item->key, str_value);
      item->key.need_clear = true;
      ptr = or_unpack_string (ptr, &item->db_user);
      ptr = or_unpack_string (ptr, &item->ha_sys_prm);

      break;

    default:
      /* unknown log type */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      goto error_return;
    }

  item->log_type = log_type;

  if (area)
    {
      free_and_init (area);
    }

  return item;

error_return:
  if (area)
    {
      free_and_init (area);
    }

  if (item)
    {
      if (item->class_name != NULL)
	{
	  db_private_free_and_init (NULL, item->class_name);
	  pr_clear_value (&item->key);
	}

      if (item->db_user != NULL)
	{
	  db_private_free_and_init (NULL, item->db_user);
	}

      if (item->ha_sys_prm != NULL)
	{
	  db_private_free_and_init (NULL, item->ha_sys_prm);
	}

      free_and_init (item);
    }

  return NULL;
}

static void
la_unlink_repl_item (LA_APPLY * apply, LA_ITEM * item)
{
  assert (apply);
  assert (item);

  /* Long transaction case, replication item does not make link */
  if ((item->prev == NULL && apply->head != item)
      || (item->next == NULL && apply->tail != item))
    {
      return;
    }

  if (item->next)
    {
      item->next->prev = item->prev;
    }
  else
    {
      apply->tail = item->prev;
    }

  if (item->prev)
    {
      item->prev->next = item->next;
    }
  else
    {
      apply->head = item->next;
    }

  if ((--apply->num_items) < 0)
    {
      apply->num_items = 0;
    }

  return;
}

static void
la_free_repl_item (LA_APPLY * apply, LA_ITEM * item)
{
  assert (apply);
  assert (item);

  la_unlink_repl_item (apply, item);

  if (item->class_name != NULL)
    {
      db_private_free_and_init (NULL, item->class_name);
      pr_clear_value (&item->key);
    }

  if (item->db_user != NULL)
    {
      db_private_free_and_init (NULL, item->db_user);
    }

  if (item->ha_sys_prm != NULL)
    {
      db_private_free_and_init (NULL, item->ha_sys_prm);
    }

  free_and_init (item);

  return;
}

static void
la_free_all_repl_items_except_head (LA_APPLY * apply)
{
  LA_ITEM *item, *next_item;

  assert (apply);

  if (apply->head)
    {
      item = apply->head->next;
    }
  else
    {
      return;
    }

  for (; item; item = next_item)
    {
      next_item = item->next;

      la_free_repl_item (apply, item);
      item = NULL;
    }

  return;
}

static void
la_free_and_add_next_repl_item (LA_APPLY * apply, LA_ITEM * last_item,
				LOG_LSA * commit_lsa)
{
  LA_ITEM *item, *next_item;
  assert (apply);
  assert (!LSA_ISNULL (commit_lsa));

  if (apply->is_long_trans)
    {
      if (apply->head == NULL && last_item != NULL)
	{
	  la_add_repl_item (apply, last_item);
	}
    }
  else
    {
      for (item = apply->head;
	   (item != NULL) && (LSA_LT (&item->lsa, commit_lsa));
	   item = next_item)
	{
	  next_item = item->next;

	  la_free_repl_item (apply, item);
	  item = NULL;
	}

      apply->head = item;
    }

  return;
}

static void
la_free_all_repl_items (LA_APPLY * apply)
{
  assert (apply);

  la_free_all_repl_items_except_head (apply);

  if (apply->head)
    {
      la_free_repl_item (apply, apply->head);
    }

  apply->num_items = 0;
  apply->is_long_trans = false;
  apply->head = NULL;
  apply->tail = NULL;

  return;
}

static void
la_clear_applied_info (LA_APPLY * apply)
{
  assert (apply);

  la_free_all_repl_items (apply);

  LSA_SET_NULL (&apply->start_lsa);
  LSA_SET_NULL (&apply->last_lsa);
  apply->tranid = 0;

  return;
}

static void
la_clear_all_repl_and_commit_list (void)
{
  int i;

  for (i = 0; i < la_Info.cur_repl; i++)
    {
      la_free_repl_items_by_tranid (la_Info.repl_lists[i]->tranid);
    }

  return;
}

/*
 * la_set_repl_log() - insert the replication item into the apply list
 *   return: NO_ERROR or error code
 *   log_pgptr : pointer to the log page
 *   tranid: the target transaction id
 *   lsa  : the target LSA of the log
 *
 * Note:
 *     APPLY thread traverses the transaction log pages, and finds out the
 *     REPLICATION LOG record. If it meets the REPLICATION LOG record,
 *     it adds that record to the apply list for later use.
 *     When the APPLY thread meets the LOG COMMIT record, it applies the
 *     inserted REPLICAION LOG records to the slave.
 */
static int
la_set_repl_log (LOG_PAGE * log_pgptr, int log_type, int tranid,
		 LOG_LSA * lsa)
{
  int error = NO_ERROR;
  LA_APPLY *apply;
  LA_ITEM *item = NULL;

  apply = la_find_apply_list (tranid);
  if (apply == NULL)
    {
      er_log_debug (ARG_FILE_LINE,
		    "fail to find out %d transaction in apply list", tranid);
      return NO_ERROR;
    }

  if (apply->is_long_trans)
    {
      LSA_COPY (&apply->last_lsa, lsa);
      return NO_ERROR;
    }

  if (apply->num_items >= LA_MAX_REPL_ITEMS)
    {
      la_free_all_repl_items_except_head (apply);
      apply->is_long_trans = true;
      LSA_COPY (&apply->last_lsa, lsa);
      return NO_ERROR;
    }

  item = la_make_repl_item (log_pgptr, log_type, tranid, lsa);
  if (item == NULL)
    {
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  la_add_repl_item (apply, item);

  return NO_ERROR;
}

/*
 * la_add_unlock_commit_log() - add the unlock_commit log to the
 *                                   commit list
 *   return: NO_ERROR or error code
 *   tranid: the target transaction id
 *   lsa   : the target LSA of the log
 *
 * Note:
 *     APPLY thread traverses the transaction log pages, and finds out the
 *     REPLICATION LOG record. If it meets the REPLICATION LOG record,
 *     it adds that record to the apply list for later use.
 *     When the APPLY thread meets the LOG COMMIT record, it applies the
 *     inserted REPLICAION LOG records into the slave.
 *     The APPLY thread applies transaction  not in regular sequence of
 *     LOG_COMMIT record, but in sequence of  LOG_UNLOCK_COMMIT record.
 *     When the APPLY thread meet the LOG_UNLOCK_COMMIT record, It doesn't
 *     apply  REPLICATION LOC record to the slave and insert REPLICATION LOC
 *     record into commit list.
 */
static int
la_add_unlock_commit_log (int tranid, LOG_LSA * lsa)
{
  LA_COMMIT *commit;
  int error = NO_ERROR;

  commit = malloc (DB_SIZEOF (LA_COMMIT));
  if (commit == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_OUT_OF_VIRTUAL_MEMORY, 1, DB_SIZEOF (LA_COMMIT));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  commit->prev = NULL;
  commit->next = NULL;
  commit->type = LOG_UNLOCK_COMMIT;
  LSA_COPY (&commit->log_lsa, lsa);
  commit->tranid = tranid;

  if (la_Info.commit_head == NULL && la_Info.commit_tail == NULL)
    {
      la_Info.commit_head = commit;
      la_Info.commit_tail = commit;
    }
  else
    {
      commit->prev = la_Info.commit_tail;
      la_Info.commit_tail->next = commit;
      la_Info.commit_tail = commit;
    }

  return error;
}

static int
la_add_abort_log (int tranid, LOG_LSA * lsa)
{
  int error = NO_ERROR;
  LA_COMMIT *commit;

  error = la_add_unlock_commit_log (tranid, lsa);
  if (error != NO_ERROR)
    {
      return error;
    }

  commit = la_Info.commit_tail;	/* last commit log */
  commit->type = LOG_ABORT;
  commit->log_record_time = 0;

  return error;
}

/*
 * la_retrieve_eot_time() - Retrieve the timestamp of End of Transaction
 *   return: NO_ERROR or error code
 *   log_pgptr : pointer to the log page
 *
 * Note:
 */
static time_t
la_retrieve_eot_time (LOG_PAGE * pgptr, LOG_LSA * lsa)
{
  int error = NO_ERROR;
  struct log_donetime *donetime;
  LOG_PAGEID pageid;
  PGLENGTH offset;
  LOG_PAGE *pg;

  pageid = lsa->pageid;
  offset = DB_SIZEOF (LOG_RECORD_HEADER) + lsa->offset;

  pg = pgptr;

  LA_LOG_READ_ALIGN (error, offset, pageid, pg);
  if (error != NO_ERROR)
    {
      /* cannot get eot time */
      return 0;
    }

  LA_LOG_READ_ADVANCE_WHEN_DOESNT_FIT (error, SSIZEOF (*donetime), offset,
				       pageid, pg);
  if (error != NO_ERROR)
    {
      /* cannot get eot time */
      return 0;
    }
  donetime = (struct log_donetime *) ((char *) pg->area + offset);

  return donetime->at_time;
}

/*
 * la_set_commit_log() - update the unlock_commit log to the commit list
 *   return: commit list count
 *   tranid : the target transaction id
 *   rectype : the target log record type (LOG_COMMIT_TOPOPE | LOG_COMMIT)
 *   lsa : the target LSA of the log
 *
 * Note:
 *     APPLY thread traverses the transaction log pages, and finds out the
 *     REPLICATION LOG record. If it meets the REPLICATION LOG record,
 *     it adds that record to the apply list for later use.
 *     When the APPLY thread meets the LOG COMMIT record, it applies the
 *     inserted REPLICAION LOG records into the slave.
 *     The APPLY thread applies transaction not in sequence of
 *     LOG_COMMIT record, but in regular sequence of LOG_UNLOCK_COMMIT record.
 *     When the APPLY thread meet the LOG_COMMIT record, It applies
 *     REPLICATION LOC record to the slave in regular sequence of commit list.
 *
 * NOTE
 */
static int
la_set_commit_log (int tranid, int rectype, LOG_LSA * lsa,
		   time_t log_record_time)
{
  LA_COMMIT *commit;
  int count = 0;

  commit = la_Info.commit_tail;
  while (commit)
    {
      if (commit->tranid == tranid)
	{
	  commit->type = rectype;
	  commit->log_record_time = log_record_time;
	  count++;
	  break;
	}
      commit = commit->prev;
    }

  return count;
}

/*
 * la_get_current()
 *   return: NO_ERROR or error code
 *
 * Note:
 *     Analyze the record description, get the value for each attribute,
 *     call dbt_put_internal() for update...
 */
static int
la_get_current (OR_BUF * buf, SM_CLASS * sm_class,
		int bound_bit_flag, DB_OTMPL * def, DB_VALUE * key,
		int offset_size)
{
  SM_ATTRIBUTE *att;
  int *vars = NULL;
  int i, j, offset, offset2, pad;
  char *bits, *start, *v_start;
  int rc = NO_ERROR;
  DB_VALUE value;
  int error = NO_ERROR;

  if (sm_class->variable_count)
    {
      vars = (int *) malloc (DB_SIZEOF (int) * sm_class->variable_count);
      if (vars == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, DB_SIZEOF (int) * sm_class->variable_count);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
      offset = or_get_offset_internal (buf, &rc, offset_size);
      for (i = 0; i < sm_class->variable_count; i++)
	{
	  offset2 = or_get_offset_internal (buf, &rc, offset_size);
	  vars[i] = offset2 - offset;
	  offset = offset2;
	}
      buf->ptr = PTR_ALIGN (buf->ptr, INT_ALIGNMENT);
    }

  bits = NULL;
  if (bound_bit_flag)
    {
      /* assume that the buffer is in contiguous memory and that we
       * can seek ahead to the bound bits.  */
      bits = (char *) buf->ptr + sm_class->fixed_size;
    }

  att = sm_class->attributes;
  start = buf->ptr;

  /* process the fixed length column */
  for (i = 0; i < sm_class->fixed_count;
       i++, att = (SM_ATTRIBUTE *) att->header.next)
    {
      if (bits != NULL && !OR_GET_BOUND_BIT (bits, i))
	{
	  /* its a NULL value, skip it */
	  db_make_null (&value);
	  or_advance (buf, tp_domain_disk_size (att->domain));
	}
      else
	{
	  /* read the disk value into the db_value */
	  (*(att->type->data_readval)) (buf, &value, att->domain, -1, true,
					NULL, 0);
	}

      /* skip cache object attribute for foreign key */
      if (att->is_fk_cache_attr)
	{
	  continue;
	}

      /* update the column */
      error = dbt_put_internal (def, att->header.name, &value);
      pr_clear_value (&value);
      if (error != NO_ERROR)
	{
	  if (vars != NULL)
	    {
	      free_and_init (vars);
	    }
	  return error;
	}
    }

  /* round up to a to the end of the fixed block */
  pad = (int) (buf->ptr - start);
  if (pad < sm_class->fixed_size)
    {
      or_advance (buf, sm_class->fixed_size - pad);
    }

  /* skip over the bound bits */
  if (bound_bit_flag)
    {
      or_advance (buf, OR_BOUND_BIT_BYTES (sm_class->fixed_count));
    }

  /* process variable length column */
  v_start = buf->ptr;
  for (i = sm_class->fixed_count, j = 0;
       i < sm_class->att_count && j < sm_class->variable_count;
       i++, j++, att = (SM_ATTRIBUTE *) att->header.next)
    {
      (*(att->type->data_readval)) (buf, &value, att->domain, vars[j], true,
				    NULL, 0);
      v_start += vars[j];
      buf->ptr = v_start;

      /* update the column */
      error = dbt_put_internal (def, att->header.name, &value);
      pr_clear_value (&value);
      if (error != NO_ERROR)
	{
	  free_and_init (vars);
	  return error;
	}
    }

  if (vars != NULL)
    {
      free_and_init (vars);
    }

  return error;
}

/*
 * la_disk_to_obj() - same function with tf_disk_to_obj, but always use
 *                      the current representation.
 *   return: NO_ERROR or error code
 *
 * Note:
 *     Analyze the record description, get the value for each attribute,
 *     call dbt_put_internal() for update...
 */
static int
la_disk_to_obj (MOBJ classobj, RECDES * record, DB_OTMPL * def,
		DB_VALUE * key)
{
  OR_BUF orep, *buf;
  int repid, status;
  SM_CLASS *sm_class;
  unsigned int repid_bits;
  int bound_bit_flag;
  int rc = NO_ERROR;
  int error = NO_ERROR;
  int offset_size;

  /* Kludge, make sure we don't upgrade objects to OID'd during the reading */
  buf = &orep;
  or_init (buf, record->data, record->length);
  buf->error_abort = 1;

  status = setjmp (buf->env);
  if (status == 0)
    {
      sm_class = (SM_CLASS *) classobj;

      /* offset size */
      offset_size = OR_GET_OFFSET_SIZE (buf->ptr);

      repid_bits = or_get_int (buf, &rc);

      (void) or_get_int (buf, &rc);	/* skip chn */

      /* mask out the repid & bound bit flag */
      repid = repid_bits & ~OR_BOUND_BIT_FLAG & ~OR_OFFSET_SIZE_FLAG;

      bound_bit_flag = repid_bits & OR_BOUND_BIT_FLAG;

      error =
	la_get_current (buf, sm_class, bound_bit_flag, def, key, offset_size);
    }
  else
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_GENERIC_ERROR, 0);
      error = ER_GENERIC_ERROR;
    }

  return error;
}

/*
 * la_get_zipped_data () - get zipped data
 *   return: error code
 */
char *
la_get_zipped_data (char *undo_data, int undo_length, bool is_diff,
		    bool is_undo_zip, bool is_overflow, char **rec_type,
		    char **data, int *length)
{
  int temp_length = 0;
  int redo_length = 0;
  int rec_len = 0;

  LOG_ZIP *undo_unzip_data = NULL;
  LOG_ZIP *redo_unzip_data = NULL;

  undo_unzip_data = la_Info.undo_unzip_ptr;
  redo_unzip_data = la_Info.redo_unzip_ptr;

  if (is_diff)
    {
      if (is_undo_zip)
	{
	  undo_length = undo_unzip_data->data_length;
	  redo_length = redo_unzip_data->data_length;

	  (void) log_diff (undo_length,
			   undo_unzip_data->
			   log_data, redo_length, redo_unzip_data->log_data);
	}
      else
	{

	  redo_length = redo_unzip_data->data_length;
	  (void) log_diff (undo_length,
			   undo_data, redo_length, redo_unzip_data->log_data);
	}
    }
  else
    {
      redo_length = redo_unzip_data->data_length;
    }

  if (rec_type)
    {
      rec_len = DB_SIZEOF (INT16);
      *length = redo_length - rec_len;
    }
  else
    {
      *length = redo_length;
    }

  if (is_overflow)
    {
      if (*data)
	{
	  free_and_init (*data);
	}
      *data = malloc (*length);
      if (*data == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, *length);
	  *length = 0;
	  return NULL;
	}
    }

  if (rec_type)
    {
      memcpy (*rec_type, (la_Info.redo_unzip_ptr)->log_data, rec_len);
      memcpy (*data, (la_Info.redo_unzip_ptr)->log_data + rec_len, *length);
    }
  else
    {
      memcpy (*data, (la_Info.redo_unzip_ptr)->log_data, redo_length);
    }

  return *data;
}


/*
 * la_get_undoredo_diff() - get undo/redo diff data
 *   return: next log page pointer
 */
int
la_get_undoredo_diff (LOG_PAGE ** pgptr, LOG_PAGEID * pageid,
		      PGLENGTH * offset, bool * is_undo_zip, char **undo_data,
		      int *undo_length)
{
  int error = NO_ERROR;

  LOG_ZIP *undo_unzip_data = NULL;

  LOG_PAGE *temp_pg;
  LOG_PAGEID temp_pageid;
  PGLENGTH temp_offset;

  undo_unzip_data = la_Info.undo_unzip_ptr;

  temp_pg = *pgptr;
  temp_pageid = *pageid;
  temp_offset = *offset;

  if (ZIP_CHECK (*undo_length))
    {				/* Undo data is Zip Check */
      *is_undo_zip = true;
      *undo_length = GET_ZIP_LEN (*undo_length);
    }

  *undo_data = (char *) malloc (*undo_length);
  if (*undo_data == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_OUT_OF_VIRTUAL_MEMORY, 1, *undo_length);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  /* get undo data for XOR process */
  la_log_copy_fromlog (NULL, *undo_data, *undo_length, *pageid, *offset,
		       *pgptr);

  if (*is_undo_zip && *undo_length > 0)
    {
      if (!log_unzip (undo_unzip_data, *undo_length, *undo_data))
	{
	  free_and_init (*undo_data);

	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_LZO_DECOMPRESS_FAIL,
		  0);
	  return ER_IO_LZO_DECOMPRESS_FAIL;
	}
    }

  LA_LOG_READ_ADD_ALIGN (error, *undo_length, temp_offset,
			 temp_pageid, temp_pg);

  *pgptr = temp_pg;
  *pageid = temp_pageid;
  *offset = temp_offset;
  return error;
}

/*
 * la_get_log_data() - get the data area of log record
 *   return: error code
 *   lrec (in) : target log record
 *   lsa (in) : the LSA of the target log record
 *   pgptr (in) : the start log page pointer
 *   match_rcvindex (in) : index
 *   rcvindex : recovery index to be returned
 *   logs : the specialized log info
 *   rec_type : the type of RECDES
 *   data : the log data
 *   d_length : the length of data
 *
 * Note: get the data area, and rcvindex, length of data for the
 *              given log record
 */
static int
la_get_log_data (LOG_RECORD_HEADER * lrec,
		 LOG_LSA * lsa, LOG_PAGE * pgptr,
		 unsigned int match_rcvindex, unsigned int *rcvindex,
		 void **logs, char **rec_type, char **data, int *d_length)
{
  LOG_PAGE *pg;
  PGLENGTH offset;
  int length;			/* type change PGLENGTH -> int */
  LOG_PAGEID pageid;
  int error = NO_ERROR;
  struct log_undoredo *undoredo;
  struct log_undo *undo;
  struct log_redo *redo;

  bool is_undo_zip = false;
  int rec_len = 0;
  int zip_len = 0;
  int undo_length = 0;
  int redo_length = 0;
  int temp_length = 0;
  char *undo_data = NULL;

  bool is_overflow = false;
  bool is_diff = false;

  pg = pgptr;

  offset = DB_SIZEOF (LOG_RECORD_HEADER) + lsa->offset;
  pageid = lsa->pageid;

  LA_LOG_READ_ALIGN (error, offset, pageid, pg);
  if (error != NO_ERROR)
    {
      return error;
    }

  switch (lrec->type)
    {
    case LOG_UNDOREDO_DATA:
    case LOG_DIFF_UNDOREDO_DATA:
      is_diff = (lrec->type == LOG_DIFF_UNDOREDO_DATA) ? true : false;

      length = DB_SIZEOF (struct log_undoredo);
      LA_LOG_READ_ADVANCE_WHEN_DOESNT_FIT (error, length, offset, pageid, pg);

      if (error == NO_ERROR)
	{
	  undoredo = (struct log_undoredo *) ((char *) pg->area + offset);

	  undo_length = undoredo->ulength;	/* undo log length */
	  temp_length = undoredo->rlength;	/* for the replication, we just need
						 * the redo data */
	  length = GET_ZIP_LEN (undoredo->rlength);

	  if (match_rcvindex == 0
	      || undoredo->data.rcvindex == match_rcvindex)
	    {
	      if (rcvindex)
		{
		  *rcvindex = undoredo->data.rcvindex;
		}
	      if (logs)
		{
		  *logs = (void *) undoredo;
		}
	    }
	  else if (logs)
	    {
	      *logs = (void *) NULL;
	    }

	  LA_LOG_READ_ADD_ALIGN (error, DB_SIZEOF (*undoredo), offset,
				 pageid, pg);
	  if (error == NO_ERROR)
	    {
	      if (is_diff)
		{		/* XOR Redo Data */
		  error =
		    la_get_undoredo_diff (&pg, &pageid, &offset,
					  &is_undo_zip, &undo_data,
					  &undo_length);
		  if (error != NO_ERROR)
		    {
		      if (undo_data != NULL)
			{
			  free_and_init (undo_data);
			}
		      return error;
		    }
		}
	      else
		{
		  LA_LOG_READ_ADD_ALIGN (error, GET_ZIP_LEN (undo_length),
					 offset, pageid, pg);
		}
	    }
	}

      break;

    case LOG_UNDO_DATA:
      length = DB_SIZEOF (struct log_undo);
      LA_LOG_READ_ADVANCE_WHEN_DOESNT_FIT (error, length, offset, pageid, pg);
      if (error == NO_ERROR)
	{
	  undo = (struct log_undo *) ((char *) pg->area + offset);
	  temp_length = undo->length;
	  length = (int) GET_ZIP_LEN (undo->length);

	  if (match_rcvindex == 0 || undo->data.rcvindex == match_rcvindex)
	    {
	      if (logs)
		{
		  *logs = (void *) undo;
		}
	      if (rcvindex)
		{
		  *rcvindex = undo->data.rcvindex;
		}
	    }
	  else if (logs)
	    {
	      *logs = (void *) NULL;
	    }
	  LA_LOG_READ_ADD_ALIGN (error, DB_SIZEOF (*undo), offset, pageid,
				 pg);
	}
      break;

    case LOG_REDO_DATA:
      length = DB_SIZEOF (struct log_redo);
      LA_LOG_READ_ADVANCE_WHEN_DOESNT_FIT (error, length, offset, pageid, pg);
      if (error == NO_ERROR)
	{
	  redo = (struct log_redo *) ((char *) pg->area + offset);
	  temp_length = redo->length;
	  length = GET_ZIP_LEN (redo->length);

	  if (match_rcvindex == 0 || redo->data.rcvindex == match_rcvindex)
	    {
	      if (logs)
		{
		  *logs = (void *) redo;
		}
	      if (rcvindex)
		{
		  *rcvindex = redo->data.rcvindex;
		}
	    }
	  else if (logs)
	    {
	      *logs = (void *) NULL;
	    }
	  LA_LOG_READ_ADD_ALIGN (error, DB_SIZEOF (*redo), offset, pageid,
				 pg);
	}
      break;

    default:
      if (logs)
	{
	  *logs = NULL;
	}

      return error;
    }

  if (error != NO_ERROR)
    {
      if (undo_data != NULL)
	{
	  free_and_init (undo_data);
	}

      return error;
    }

  if (*data == NULL)
    {
      /* general cases, use the pre-allocated buffer */
      *data = malloc (length);
      is_overflow = true;

      if (*data == NULL)
	{
	  *d_length = 0;
	  if (undo_data != NULL)
	    {
	      free_and_init (undo_data);
	    }

	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, length);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
    }

  if (ZIP_CHECK (temp_length))
    {
      zip_len = GET_ZIP_LEN (temp_length);
      /* Get Zip Data */
      la_log_copy_fromlog (NULL, *data, zip_len, pageid, offset, pg);

      if (zip_len != 0)
	{
	  if (!log_unzip (la_Info.redo_unzip_ptr, zip_len, *data))
	    {
	      if (undo_data != NULL)
		{
		  free_and_init (undo_data);
		}
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_IO_LZO_DECOMPRESS_FAIL, 0);
	      return ER_IO_LZO_DECOMPRESS_FAIL;
	    }
	}

      *data =
	la_get_zipped_data (undo_data, undo_length, is_diff, is_undo_zip,
			    is_overflow, rec_type, data, &length);
      if (*data == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	}
    }
  else
    {
      /* Get Redo Data */
      la_log_copy_fromlog (rec_type ? *rec_type : NULL, *data, length,
			   pageid, offset, pg);
    }

  *d_length = length;

  if (undo_data != NULL)
    {
      free_and_init (undo_data);
    }

  return error;
}

/*
 * la_get_overflow_update_recdes() - prepare the overflow page update
 *   return: NO_ERROR or error code
 *
 */
static int
la_get_overflow_recdes (LOG_RECORD_HEADER * log_record, void *logs,
			char **area, int *length, unsigned int rcvindex)
{
  LOG_LSA current_lsa;
  LOG_PAGE *current_log_page;
  LOG_RECORD_HEADER *current_log_record;
  LA_OVF_PAGE_LIST *ovf_list_head = NULL;
  LA_OVF_PAGE_LIST *ovf_list_tail = NULL;
  LA_OVF_PAGE_LIST *ovf_list_data = NULL;
  struct log_redo *redo_log;
  void *log_info;
  VPID *temp_vpid;
  VPID prev_vpid;
  bool first = true;
  int copyed_len;
  int area_len;
  int area_offset;
  int error = NO_ERROR;

  LSA_COPY (&current_lsa, &log_record->prev_tranlsa);
  prev_vpid.pageid = ((struct log_undoredo *) logs)->data.pageid;
  prev_vpid.volid = ((struct log_undoredo *) logs)->data.volid;

  while (!LSA_ISNULL (&current_lsa))
    {
      current_log_page = la_get_page (current_lsa.pageid);
      current_log_record = LOG_GET_LOG_RECORD_HEADER (current_log_page,
						      &current_lsa);

      if (current_log_record->trid != log_record->trid
	  || current_log_record->type == LOG_DUMMY_OVF_RECORD)
	{
	  la_release_page_buffer (current_lsa.pageid);
	  break;
	}
      else if (current_log_record->type == LOG_REDO_DATA)
	{
	  /* process only LOG_REDO_DATA */

	  ovf_list_data =
	    (LA_OVF_PAGE_LIST *) malloc (DB_SIZEOF (LA_OVF_PAGE_LIST));
	  if (ovf_list_data == NULL)
	    {
	      /* malloc failed */
	      while (ovf_list_head)
		{
		  ovf_list_data = ovf_list_head;
		  ovf_list_head = ovf_list_head->next;
		  free_and_init (ovf_list_data->data);
		  free_and_init (ovf_list_data);
		}

	      la_release_page_buffer (current_lsa.pageid);

	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_OUT_OF_VIRTUAL_MEMORY, 1,
		      DB_SIZEOF (LA_OVF_PAGE_LIST));
	      return ER_OUT_OF_VIRTUAL_MEMORY;
	    }

	  memset (ovf_list_data, 0, DB_SIZEOF (LA_OVF_PAGE_LIST));
	  error =
	    la_get_log_data (current_log_record, &current_lsa,
			     current_log_page, rcvindex, NULL,
			     &log_info, NULL,
			     &ovf_list_data->data, &ovf_list_data->length);

	  if (error == NO_ERROR && log_info && ovf_list_data->data)
	    {
	      /* add to linked-list */
	      if (ovf_list_head == NULL)
		{
		  ovf_list_head = ovf_list_tail = ovf_list_data;
		}
	      else
		{
		  ovf_list_data->next = ovf_list_head;
		  ovf_list_head = ovf_list_data;
		}

	      *length += ovf_list_data->length;
	    }
	  else
	    {
	      if (ovf_list_data->data != NULL)
		{
		  free_and_init (ovf_list_data->data);
		}
	      free_and_init (ovf_list_data);
	    }
	}
      la_release_page_buffer (current_lsa.pageid);
      LSA_COPY (&current_lsa, &current_log_record->prev_tranlsa);
    }

  *area = malloc (*length);
  if (*area == NULL)
    {
      /* malloc failed: clear linked-list */
      while (ovf_list_head)
	{
	  ovf_list_data = ovf_list_head;
	  ovf_list_head = ovf_list_head->next;
	  free_and_init (ovf_list_data->data);
	  free_and_init (ovf_list_data);
	}
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      *length);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  /* make record description */
  copyed_len = 0;
  while (ovf_list_head)
    {
      ovf_list_data = ovf_list_head;
      ovf_list_head = ovf_list_head->next;

      if (first)
	{
	  area_offset = offsetof (LA_OVF_FIRST_PART, data);
	  first = false;
	}
      else
	{
	  area_offset = offsetof (LA_OVF_REST_PARTS, data);
	}
      area_len = ovf_list_data->length - area_offset;
      memcpy (*area + copyed_len, ovf_list_data->data + area_offset,
	      area_len);
      copyed_len += area_len;

      free_and_init (ovf_list_data->data);
      free_and_init (ovf_list_data);
    }

  return error;
}

/*
 * la_get_next_update_log() - get the right update log
 *   return: NO_ERROR or error code
 *   prev_lrec(in):  prev log record
 *   pgptr(in):  the start log page pointer
 *   logs(out) : the specialized log info
 *   rec_type(out) : the type of RECDES
 *   data(out) : the log data
 *   d_length(out): the length of data
 *
 * Note:
 *      When the applier meets the REC_ASSIGN_ADDRESS or REC_RELOCATION
 *      record, it should fetch the real UPDATE log record to be processed.
 */
static int
la_get_next_update_log (LOG_RECORD_HEADER * prev_lrec,
			LOG_PAGE * pgptr, void **logs,
			char **rec_type, char **data, int *d_length)
{
  LOG_PAGE *pg;
  LOG_LSA lsa;
  PGLENGTH offset;
  int length;			/* type change PGLENGTH -> int */
  LOG_PAGEID pageid;
  int error = NO_ERROR;
  LOG_RECORD_HEADER *lrec;
  struct log_undoredo *undoredo;
  struct log_undoredo *prev_log;
  int zip_len = 0;
  int temp_length = 0;
  int undo_length = 0;
  int redo_length = 0;

  bool is_undo_zip = false;

  char *undo_data = NULL;
  LOG_ZIP *redo_unzip_data = NULL;
  int rec_len = 0;

  bool is_diff = false;

  pg = pgptr;
  LSA_COPY (&lsa, &prev_lrec->forw_lsa);
  prev_log = *(struct log_undoredo **) logs;

  redo_unzip_data = la_Info.redo_unzip_ptr;

  while (true)
    {
      while (pg && pg->hdr.logical_pageid == lsa.pageid)
	{
	  lrec = LOG_GET_LOG_RECORD_HEADER (pg, &lsa);
	  if (lrec->trid == prev_lrec->trid &&
	      (lrec->type == LOG_UNDOREDO_DATA
	       || lrec->type == LOG_DIFF_UNDOREDO_DATA))
	    {
	      is_diff = (lrec->type == LOG_DIFF_UNDOREDO_DATA) ? true : false;

	      offset = DB_SIZEOF (LOG_RECORD_HEADER) + lsa.offset;
	      pageid = lsa.pageid;
	      LA_LOG_READ_ALIGN (error, offset, pageid, pg);
	      length = DB_SIZEOF (struct log_undoredo);
	      LA_LOG_READ_ADVANCE_WHEN_DOESNT_FIT (error, length, offset,
						   pageid, pg);
	      if (error == NO_ERROR)
		{
		  undoredo =
		    (struct log_undoredo *) ((char *) pg->area + offset);
		  undo_length = undoredo->ulength;
		  temp_length = undoredo->rlength;
		  length = GET_ZIP_LEN (undoredo->rlength);

		  if (undoredo->data.rcvindex == RVHF_UPDATE &&
		      undoredo->data.pageid == prev_log->data.pageid &&
		      undoredo->data.offset == prev_log->data.offset &&
		      undoredo->data.volid == prev_log->data.volid)
		    {
		      LA_LOG_READ_ADD_ALIGN (error, DB_SIZEOF (*undoredo),
					     offset, pageid, pg);
		      if (is_diff)
			{
			  error =
			    la_get_undoredo_diff (&pg, &pageid,
						  &offset, &is_undo_zip,
						  &undo_data, &undo_length);
			  if (error != NO_ERROR)
			    {

			      if (undo_data != NULL)
				{
				  free_and_init (undo_data);
				}
			      return error;
			    }
			}
		      else
			{
			  LA_LOG_READ_ADD_ALIGN (error, GET_ZIP_LEN
						 (undo_length),
						 offset, pageid, pg);
			}

		      if (ZIP_CHECK (temp_length))
			{
			  zip_len = GET_ZIP_LEN (temp_length);
			  la_log_copy_fromlog (NULL, *data, zip_len,
					       pageid, offset, pg);

			  if (zip_len != 0)
			    {
			      if (!log_unzip
				  (redo_unzip_data, zip_len, *data))
				{
				  if (undo_data != NULL)
				    {
				      free_and_init (undo_data);
				    }
				  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
					  ER_IO_LZO_DECOMPRESS_FAIL, 0);
				  return ER_IO_LZO_DECOMPRESS_FAIL;
				}
			    }

			  *data =
			    la_get_zipped_data (undo_data, undo_length,
						is_diff, is_undo_zip, 0,
						rec_type, data, &length);
			  if (*data == NULL)
			    {
			      assert (er_errid () != NO_ERROR);
			      error = er_errid ();
			    }
			}
		      else
			{
			  la_log_copy_fromlog (rec_type ? *rec_type : NULL,
					       *data, length, pageid, offset,
					       pg);
			}

		      *d_length = length;

		      if (undo_data != NULL)
			{
			  free_and_init (undo_data);
			}

		      return error;
		    }
		}
	    }
	  else if (lrec->trid == prev_lrec->trid
		   && (lrec->type == LOG_COMMIT || lrec->type == LOG_ABORT))
	    {
	      return ER_GENERIC_ERROR;
	    }
	  LSA_COPY (&lsa, &lrec->forw_lsa);
	}

      pg = la_get_page (lsa.pageid);
    }

  return error;
}

static int
la_get_relocation_recdes (LOG_RECORD_HEADER * lrec, LOG_PAGE * pgptr,
			  unsigned int match_rcvindex, void **logs,
			  char **rec_type, char **data, int *d_length)
{
  LOG_RECORD_HEADER *tmp_lrec;
  unsigned int rcvindex;
  LOG_PAGE *pg = pgptr;
  LOG_LSA lsa;
  int error = NO_ERROR;

  LSA_COPY (&lsa, &lrec->prev_tranlsa);
  if (!LSA_ISNULL (&lsa))
    {
      pg = la_get_page (lsa.pageid);
      tmp_lrec = LOG_GET_LOG_RECORD_HEADER (pg, &lsa);
      if (tmp_lrec->trid != lrec->trid)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_PAGE_CORRUPTED,
		  1, lsa.pageid);
	  error = ER_LOG_PAGE_CORRUPTED;
	}
      else
	{
	  error =
	    la_get_log_data (tmp_lrec, &lsa, pg,
			     RVHF_INSERT, &rcvindex, logs,
			     rec_type, data, d_length);
	}
      la_release_page_buffer (lsa.pageid);
    }
  else
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_PAGE_CORRUPTED,
	      1, lsa.pageid);
      error = ER_LOG_PAGE_CORRUPTED;
    }

  return error;
}

/*
 * la_get_recdes() - get the record description from the log file
 *   return: NO_ERROR or error code
 *    pgptr: point to the target log page
 *    recdes(out): record description (output)
 *    rcvindex(out): recovery index (output)
 *    log_data: log data area
 *    ovf_yn(out)  : true if the log data is in overflow page
 *
 * Note:
 *     To replicate the data, we have to filter the record descripion
 *     from the log record. This function retrieves the record description
 *     for the given lsa.
 */
static int
la_get_recdes (LOG_LSA * lsa, LOG_PAGE * pgptr,
	       RECDES * recdes, unsigned int *rcvindex,
	       char *log_data, char *rec_type, bool * ovfyn)
{
  LOG_RECORD_HEADER *lrec;
  LOG_PAGE *pg;
  int length;
  int error = NO_ERROR;
  char *area = NULL;
  void *logs = NULL;

  pg = pgptr;
  lrec = LOG_GET_LOG_RECORD_HEADER (pg, lsa);

  error = la_get_log_data (lrec, lsa, pg, 0, rcvindex,
			   &logs, &rec_type, &log_data, &length);

  if (error == NO_ERROR && logs != NULL)
    {
      recdes->type = *(INT16 *) (rec_type);
      recdes->data = log_data;
      recdes->area_size = recdes->length = length;
    }
  else
    {
      er_log_debug (ARG_FILE_LINE,
		    "cannot get log record from LSA(%d|%d)",
		    lsa->pageid, lsa->offset);
      if (error != NO_ERROR)
	{
	  return error;
	}
      else
	{
	  return ER_FAILED;
	}
    }

  /* Now.. we have to process overflow pages */
  length = 0;
  if (*rcvindex == RVOVF_CHANGE_LINK)
    {
      /* if overflow page update */
      error = la_get_overflow_recdes (lrec, logs, &area, &length,
				      RVOVF_PAGE_UPDATE);
      recdes->type = REC_BIGONE;
    }
  else if (recdes->type == REC_BIGONE)
    {
      /* if overflow page insert */
      error = la_get_overflow_recdes (lrec, logs, &area, &length,
				      RVOVF_NEWPAGE_INSERT);
    }
  else if (*rcvindex == RVHF_INSERT && recdes->type == REC_ASSIGN_ADDRESS)
    {
      error = la_get_next_update_log (lrec,
				      pg, &logs, &rec_type,
				      &log_data, &length);
      if (error == NO_ERROR)
	{
	  recdes->type = *(INT16 *) (rec_type);
	  if (recdes->type == REC_BIGONE)
	    {
	      error = la_get_overflow_recdes (lrec, logs, &area, &length,
					      RVOVF_NEWPAGE_INSERT);
	    }
	  else
	    {
	      recdes->data = log_data;
	      recdes->area_size = recdes->length = length;
	      return error;
	    }
	}
      else
	{
	  return error;
	}
    }
  else if (*rcvindex == RVHF_UPDATE && recdes->type == REC_RELOCATION)
    {
      error = la_get_relocation_recdes (lrec, pg, 0,
					&logs, &rec_type, &log_data, &length);
      if (error == NO_ERROR)
	{
	  recdes->type = *(INT16 *) (rec_type);
	  recdes->data = log_data;
	  recdes->area_size = recdes->length = length;
	}
      return error;
    }
  else
    {
      return error;
    }

  if (error != NO_ERROR)
    {
      if (area != NULL)
	{
	  free_and_init (area);
	}
      return error;
    }

  recdes->data = (char *) (area);
  recdes->area_size = recdes->length = length;
  *ovfyn = true;

  return error;
}

static struct log_ha_server_state *
la_get_ha_server_state (LOG_PAGE * pgptr, LOG_LSA * lsa)
{
  struct log_ha_server_state *state = NULL;
  int error = NO_ERROR;
  LOG_PAGEID pageid;
  PGLENGTH offset;
  int length;
  LOG_PAGE *pg;

  pageid = lsa->pageid;
  offset = DB_SIZEOF (LOG_RECORD_HEADER) + lsa->offset;
  pg = pgptr;

  length = DB_SIZEOF (struct log_ha_server_state);
  LA_LOG_READ_ADVANCE_WHEN_DOESNT_FIT (error, length, offset, pageid, pg);
  if (error == NO_ERROR)
    {
      state = (struct log_ha_server_state *) ((char *) pg->area + offset);
    }

  return state;
}

static int
la_flush_unflushed_insert (MOP class_mop)
{
  int error = NO_ERROR;
  WS_FLUSH_ERR *flush_err;
  MOP mop;
  const char *class_name;
  char primary_key[256];
  int length;
  char buf[256];

  if (la_Info.num_unflushed_insert > 0)
    {
      if (class_mop)
	{
	  error =
	    locator_flush_all_instances (class_mop, DONT_DECACHE,
					 LC_CONTINUE_ON_ERROR);
	}
      else
	{
	  error = locator_all_flush (LC_CONTINUE_ON_ERROR);
	}

      if (error == ER_LC_PARTIALLY_FAILED_TO_FLUSH)
	{
	  while (1)
	    {
	      class_name = NULL;

	      flush_err = ws_get_error_from_error_link ();
	      if (flush_err == NULL)
		{
		  break;
		}

	      error = flush_err->error_code;

	      if (class_mop == NULL)
		{
		  class_mop =
		    ws_mop (&flush_err->class_oid, sm_Root_class_mop);
		}
	      mop = ws_mop (&flush_err->oid, class_mop);

	      ws_free_flush_error (flush_err);

	      if (mop == NULL)
		{
		  break;
		}

	      if (mop->class_mop && mop->class_mop->object)
		{
		  class_name =
		    ((SM_CLASS *) mop->class_mop->object)->header.name;
		}

	      if (class_name)
		{
		  length =
		    la_help_sprint_object (mop, primary_key,
					   sizeof (primary_key));
		  if (length > 0)
		    {
		      er_stack_push ();
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			      ER_HA_LA_FAILED_TO_APPLY_INSERT, 3, class_name,
			      primary_key, error);
		      er_stack_pop ();
		    }
		}

	      ws_decache (mop);

	      la_Info.fail_counter++;

	      if (la_restart_on_bulk_flush_error (error))
		{
		  snprintf (buf, sizeof (buf),
			    "applylogdb will reconnect to server due to a failure "
			    "in flushing changes (error:%d)", error);
		  er_stack_push ();
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_HA_GENERIC_ERROR, 1, buf);
		  er_stack_pop ();

		  error = ER_LC_PARTIALLY_FAILED_TO_FLUSH;
		  la_applier_need_shutdown = true;

		  ws_clear_all_errors_of_error_link ();

		  return error;
		}
	    }

	  ws_clear_all_errors_of_error_link ();
	  error = NO_ERROR;
	}
      else if (error != NO_ERROR)
	{
	  la_Info.fail_counter++;
	  ws_clear_all_errors_of_error_link ();
	  return error;
	}

      la_Info.num_unflushed_insert = 0;
    }

  return error;
}

/*
 * la_apply_delete_log() - apply the delete log to the target slave
 *   return: NO_ERROR or error code
 *   item(in): replication item
 *
 * Note:
 */
static int
la_apply_delete_log (LA_ITEM * item)
{
  DB_OBJECT *class_obj;
  MOBJ mclass;
  int error;
  char buf[256];
  char sql_log_err[LINE_MAX];

  error = la_flush_unflushed_insert (NULL);
  if (error != NO_ERROR)
    {
      return error;
    }

  /* find out class object by class name */
  class_obj = db_find_class (item->class_name);
  if (class_obj == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
    }
  else
    {
      /* get class info */
      mclass = locator_fetch_class (class_obj, DB_FETCH_CLREAD_INSTREAD);

      if (la_enable_sql_logging)
	{
	  if (sl_write_delete_sql (item->class_name, mclass, &item->key) !=
	      NO_ERROR)
	    {
	      help_sprint_value (&item->key, buf, 255);
	      snprintf (sql_log_err, sizeof (sql_log_err),
			"failed to write SQL log. class: %s, key: %s",
			item->class_name, buf);

	      er_stack_push ();
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HA_GENERIC_ERROR,
		      1, sql_log_err);
	      er_stack_pop ();
	    }
	}

      error = obj_repl_delete_object_by_pkey (class_obj, &item->key);
      if (error == NO_ERROR)
	{
	  la_Info.delete_counter++;
	}
    }

  if (error != NO_ERROR)
    {
      help_sprint_value (&item->key, buf, 255);
#if defined (LA_VERBOSE_DEBUG)
      er_log_debug (ARG_FILE_LINE,
		    "apply_delete : error %d %s\n\tclass %s key %s\n",
		    error, er_msg (), item->class_name, buf);
#endif
      er_stack_push ();
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_HA_LA_FAILED_TO_APPLY_DELETE, 3, item->class_name, buf,
	      error);
      er_stack_pop ();

      la_Info.fail_counter++;
    }

  return error;
}

/*
 * la_apply_update_log() - apply the update log to the target slave
 *   return: NO_ERROR or error code
 *   item : replication item
 *
 * Note:
 *      Apply the update log to the target slave.
 *      . get the target log page
 *      . get the record description
 *      . fetch the class info
 *      . fetch the target object by pk
 *      . create an existing object template
 *      . transform record description to object, and edit the target object
 *        column by columd.
 *      . finalize the editing of object template - dbt_finish
 */
static int
la_apply_update_log (LA_ITEM * item)
{
  int error = NO_ERROR, au_save;
  DB_OBJECT *class_obj;
  DB_OBJECT *object = NULL;
  DB_OBJECT *new_object = NULL;
  MOBJ mclass;
  LOG_PAGE *pgptr;
  unsigned int rcvindex;
  RECDES recdes;
  DB_OTMPL *inst_tp = NULL;
  bool ovfyn = false;
  LOG_PAGEID old_pageid = -1;
  char buf[256];
  char sql_log_err[LINE_MAX];

  /* get the target log page */
  old_pageid = item->target_lsa.pageid;
  pgptr = la_get_page (old_pageid);
  if (pgptr == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  /* retrieve the target record description */
  error = la_get_recdes (&item->target_lsa, pgptr, &recdes, &rcvindex,
			 la_Info.log_data, la_Info.rec_type, &ovfyn);
  if (error == ER_NET_CANT_CONNECT_SERVER || error == ER_OBJ_NO_CONNECT)
    {
      la_release_page_buffer (old_pageid);
      return ER_NET_CANT_CONNECT_SERVER;
    }
  if (error != NO_ERROR)
    {
      la_release_page_buffer (old_pageid);
      return error;
    }

  if (recdes.type == REC_ASSIGN_ADDRESS || recdes.type == REC_RELOCATION)
    {
      er_log_debug (ARG_FILE_LINE, "apply_update : rectype.type = %d\n",
		    recdes.type);
      la_release_page_buffer (old_pageid);
      return error;
    }
  if (rcvindex != RVHF_UPDATE && rcvindex != RVOVF_CHANGE_LINK)
    {
      er_log_debug (ARG_FILE_LINE, "apply_update : rcvindex = %d\n",
		    rcvindex);
      la_release_page_buffer (old_pageid);
      return error;
    }

  error = la_flush_unflushed_insert (NULL);
  if (error != NO_ERROR)
    {
      la_release_page_buffer (old_pageid);
      return error;
    }

  /* Now, make the MOBJ from the record description */
  class_obj = db_find_class (item->class_name);
  if (class_obj == NULL)
    {
      goto error_rtn;
    }

  /* check existence */
  object = obj_repl_find_object_by_pkey (class_obj, &item->key,
					 AU_FETCH_UPDATE);
  if (object == NULL)
    {
      help_sprint_value (&item->key, buf, 255);
      er_log_debug (ARG_FILE_LINE,
		    "apply_update : cannot find class %s key %s\n",
		    item->class_name, buf);
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto error_rtn;
    }

  /* get class info */
  mclass = locator_fetch_class (class_obj, DB_FETCH_CLREAD_INSTREAD);
  if (mclass == NULL)
    {
      goto error_rtn;
    }

  /* start replication */
  /* NOTE: if the master insert a row, and update it within a transaction,
   *       the replication record points the RVHF_UPDATE record
   */
  AU_SAVE_AND_DISABLE (au_save);

  inst_tp = dbt_edit_object (object);
  if (inst_tp == NULL)
    {
      AU_RESTORE (au_save);

      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto error_rtn;
    }

  /* make object using the record rescription */
  error = la_disk_to_obj (mclass, &recdes, inst_tp, &item->key);
  if (error != NO_ERROR)
    {
      AU_RESTORE (au_save);
      goto error_rtn;
    }

  /* write sql log */
  if (la_enable_sql_logging)
    {
      if (sl_write_update_sql (inst_tp, &item->key) != NO_ERROR)
	{
	  help_sprint_value (&item->key, buf, 255);
	  snprintf (sql_log_err, sizeof (sql_log_err),
		    "failed to write SQL log. class: %s, key: %s",
		    item->class_name, buf);

	  er_stack_push ();
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HA_GENERIC_ERROR,
		  1, sql_log_err);
	  er_stack_pop ();
	}
    }

  /* update object */
  new_object = dbt_finish_object_and_decache_when_failure (inst_tp);
  if (new_object == NULL)
    {
      AU_RESTORE (au_save);
      goto error_rtn;
    }

  la_Info.update_counter++;

  AU_RESTORE (au_save);

  if (ovfyn)
    {
      free_and_init (recdes.data);
    }

  assert (new_object == object);

  /* This prevents from dangling references to serial objects
   * during replication.
   * The typical scenario is to update serials, cull mops which clears
   * the mop up, and then truncate the table which leads updating
   * the serial mop to reset its values.
   */
  ws_release_user_instance (new_object);
  object = new_object = NULL;

  la_release_page_buffer (old_pageid);

  return error;

  /* error */
error_rtn:

  if (error == NO_ERROR)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
    }

  help_sprint_value (&item->key, buf, 255);
#if defined (LA_VERBOSE_DEBUG)
  er_log_debug (ARG_FILE_LINE,
		"apply_update : error %d %s\n\tclass %s key %s\n",
		error, er_msg (), item->class_name, buf);
#endif
  er_stack_push ();
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	  ER_HA_LA_FAILED_TO_APPLY_UPDATE, 3, item->class_name, buf, error);
  er_stack_pop ();

  la_Info.fail_counter++;

  if (ovfyn)
    {
      free_and_init (recdes.data);
    }

  assert (new_object == NULL || new_object == object);
  if (object)
    {
      /* This prevents from dangling references to serial objects
       * during replication.
       * The typical scenario is to update serials, cull mops which clears
       * the mop up, and then truncate the table which leads updating
       * the serial mop to reset its values.
       */
      ws_release_user_instance (object);
      ws_decache (object);
      object = new_object = NULL;
    }

  if (inst_tp)
    {
      dbt_abort_object (inst_tp);
    }

  if (error == ER_NET_CANT_CONNECT_SERVER || error == ER_OBJ_NO_CONNECT)
    {
      error = ER_NET_CANT_CONNECT_SERVER;
    }

  la_release_page_buffer (old_pageid);
  return error;
}

/*
 * la_apply_update_log_server_side() - apply the update log to the target slave using server side update
 *   return: NO_ERROR or error code
 *   item : replication item
 *
 * Note:
 *      Apply the update log to the target slave.
 *      . get the target log page
 *      . get the record description
 *      . fetch the class info
 *      . request server side update
 */
static int
la_apply_update_log_server_side (LA_ITEM * item)
{
  int error = NO_ERROR, au_save;
  unsigned int rcvindex;
  bool ovfyn = false;
  RECDES recdes;
  LOG_PAGE *pgptr = NULL;
  LOG_PAGEID old_pageid = -1;
  DB_OBJECT *class_obj;
  MOBJ mclass;
  DB_OTMPL *inst_tp = NULL;
  char buf[255];
  char sql_log_err[LINE_MAX];

  /* get the target log page */
  old_pageid = item->target_lsa.pageid;
  pgptr = la_get_page (old_pageid);
  if (pgptr == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  /* retrieve the target record description */
  error = la_get_recdes (&item->target_lsa, pgptr, &recdes, &rcvindex,
			 la_Info.log_data, la_Info.rec_type, &ovfyn);
  if (error == ER_NET_CANT_CONNECT_SERVER || error == ER_OBJ_NO_CONNECT)
    {
      la_release_page_buffer (old_pageid);
      return ER_NET_CANT_CONNECT_SERVER;
    }
  if (error != NO_ERROR)
    {
      la_release_page_buffer (old_pageid);
      return error;
    }

  if (recdes.type == REC_ASSIGN_ADDRESS || recdes.type == REC_RELOCATION)
    {
      er_log_debug (ARG_FILE_LINE, "apply_update : rectype.type = %d\n",
		    recdes.type);
      la_release_page_buffer (old_pageid);
      return error;
    }
  if (rcvindex != RVHF_UPDATE && rcvindex != RVOVF_CHANGE_LINK)
    {
      er_log_debug (ARG_FILE_LINE, "apply_update : rcvindex = %d\n",
		    rcvindex);
      la_release_page_buffer (old_pageid);
      return error;
    }

  error = la_flush_unflushed_insert (NULL);
  if (error != NO_ERROR)
    {
      la_release_page_buffer (old_pageid);
      return error;
    }

  class_obj = db_find_class (item->class_name);
  if (class_obj == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      if (error == NO_ERROR)
	{
	  error = ER_FAILED;
	}
      goto end;
    }

  error = obj_repl_update_object (class_obj, &item->key, &recdes);

  /*
   * regardless of the success or failure of obj_repl_update_object,
   * we should write sql log.
   */
  if (la_enable_sql_logging)
    {
      bool sql_logging_failed = false;
      int rc;

      er_stack_push ();
      do
	{
	  mclass = locator_fetch_class (class_obj, DB_FETCH_CLREAD_INSTREAD);
	  if (mclass == NULL)
	    {
	      sql_logging_failed = true;
	      break;
	    }

	  AU_SAVE_AND_DISABLE (au_save);

	  inst_tp = dbt_create_object_internal (class_obj);
	  if (inst_tp == NULL)
	    {
	      sql_logging_failed = true;
	      AU_RESTORE (au_save);
	      break;
	    }

	  rc = la_disk_to_obj (mclass, &recdes, inst_tp, &item->key);
	  if (rc != NO_ERROR)
	    {
	      sql_logging_failed = true;
	      AU_RESTORE (au_save);
	      break;
	    }

	  if (sl_write_update_sql (inst_tp, &item->key) != NO_ERROR)
	    {
	      AU_RESTORE (au_save);
	      sql_logging_failed = true;
	      break;
	    }

	  AU_RESTORE (au_save);
	}
      while (0);
      er_stack_pop ();

      if (sql_logging_failed == true)
	{
	  help_sprint_value (&item->key, buf, 255);
	  snprintf (sql_log_err, sizeof (sql_log_err),
		    "failed to write SQL log. class: %s, key: %s",
		    item->class_name, buf);

	  er_stack_push ();
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HA_GENERIC_ERROR,
		  1, sql_log_err);
	  er_stack_pop ();
	}
    }

end:
  if (error != NO_ERROR)
    {
      help_sprint_value (&item->key, buf, 255);
#if defined (LA_VERBOSE_DEBUG)
      er_log_debug (ARG_FILE_LINE,
		    "apply_update : error %d %s\n\tclass %s key %s\n",
		    error, er_msg (), item->class_name, buf);
#endif
      er_stack_push ();
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_HA_LA_FAILED_TO_APPLY_UPDATE, 3, item->class_name, buf,
	      error);
      er_stack_pop ();

      la_Info.fail_counter++;

      if (error == ER_NET_CANT_CONNECT_SERVER || error == ER_OBJ_NO_CONNECT)
	{
	  error = ER_NET_CANT_CONNECT_SERVER;
	}
    }
  else
    {
      la_Info.update_counter++;
    }

  if (ovfyn)
    {
      free_and_init (recdes.data);
    }

  if (inst_tp)
    {
      if (inst_tp->object)
	{
	  ws_release_user_instance (inst_tp->object);
	  ws_decache (inst_tp->object);
	}
      dbt_abort_object (inst_tp);
    }

  la_release_page_buffer (old_pageid);

  return error;
}

/*
 * la_apply_insert_log() - apply the insert log to the target slave
 *   return: NO_ERROR or error code
 *   item : replication item
 *
 * Note:
 *      Apply the insert log to the target slave.
 *      . get the target log page
 *      . get the record description
 *      . fetch the class info
 *      . create a new obect template
 *      . transform record description to object, and edit the target object
 *        column by columd.
 *      . finalize the editing of object template - dbt_finish
 */
static int
la_apply_insert_log (LA_ITEM * item)
{
  int error = NO_ERROR, au_save;
  DB_OBJECT *class_obj;
  DB_OBJECT *new_object = NULL;
  MOBJ mclass;
  LOG_PAGE *pgptr;
  unsigned int rcvindex;
  RECDES recdes;
  DB_OTMPL *inst_tp = NULL;
  bool ovfyn = false;
  LOG_PAGEID old_pageid = -1;
  char buf[256];
  static char last_inserted_class_name[DB_MAX_IDENTIFIER_LENGTH] = { 0, };
  DB_OBJECT *last_inserted_class_obj;
  char sql_log_err[LINE_MAX];

  /* get the target log page */
  old_pageid = item->target_lsa.pageid;
  pgptr = la_get_page (old_pageid);
  if (pgptr == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  /* retrieve the target record description */
  error = la_get_recdes (&item->target_lsa, pgptr, &recdes, &rcvindex,
			 la_Info.log_data, la_Info.rec_type, &ovfyn);
  if (error == ER_NET_CANT_CONNECT_SERVER || error == ER_OBJ_NO_CONNECT)
    {
      return ER_NET_CANT_CONNECT_SERVER;
    }
  if (error != NO_ERROR)
    {
      return error;
    }

  if (recdes.type == REC_ASSIGN_ADDRESS || recdes.type == REC_RELOCATION)
    {
      er_log_debug (ARG_FILE_LINE, "apply_insert : rectype.type = %d\n",
		    recdes.type);
      la_release_page_buffer (old_pageid);
      return error;
    }
  if (rcvindex != RVHF_INSERT)
    {
      er_log_debug (ARG_FILE_LINE, "apply_insert : rcvindex = %d\n",
		    rcvindex);
      la_release_page_buffer (old_pageid);
      return error;
    }

  if (strcmp (last_inserted_class_name, item->class_name) != 0)
    {
      if (last_inserted_class_name[0] != '\0')
	{
	  last_inserted_class_obj = db_find_class (last_inserted_class_name);
	  error = la_flush_unflushed_insert (last_inserted_class_obj);
	  if (error != NO_ERROR)
	    {
	      la_release_page_buffer (old_pageid);
	      return error;
	    }
	}

      strncpy (last_inserted_class_name, item->class_name,
	       sizeof (last_inserted_class_name) - 1);
    }

  /* Now, make the MOBJ from the record description */
  class_obj = db_find_class (item->class_name);
  if (class_obj == NULL)
    {
      goto error_rtn;
    }

  /* get class info */
  mclass = locator_fetch_class (class_obj, DB_FETCH_CLREAD_INSTREAD);
  if (mclass == NULL)
    {
      goto error_rtn;
    }

  /* start replication */
  /* NOTE: if the master insert a row, and update it within a transaction,
   *       the replication record points the RVHF_UPDATE record
   */
  AU_SAVE_AND_DISABLE (au_save);

  inst_tp = dbt_create_object_internal (class_obj);
  if (inst_tp == NULL)
    {
      AU_RESTORE (au_save);

      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto error_rtn;
    }

  /* make object using the record description */
  error = la_disk_to_obj (mclass, &recdes, inst_tp, &item->key);
  if (error != NO_ERROR)
    {
      AU_RESTORE (au_save);
      goto error_rtn;
    }

  /* write sql log */
  if (la_enable_sql_logging)
    {
      if (sl_write_insert_sql (inst_tp, &item->key) != NO_ERROR)
	{
	  help_sprint_value (&item->key, buf, 255);
	  snprintf (sql_log_err, sizeof (sql_log_err),
		    "failed to write SQL log. class: %s, key: %s",
		    item->class_name, buf);

	  er_stack_push ();
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HA_GENERIC_ERROR,
		  1, sql_log_err);
	  er_stack_pop ();
	}
    }

  /* update object */
  obt_set_bulk_flush (inst_tp);
  new_object = dbt_finish_object_and_decache_when_failure (inst_tp);
  if (new_object == NULL)
    {
      AU_RESTORE (au_save);
      goto error_rtn;
    }
  la_Info.num_unflushed_insert++;
  la_Info.insert_counter++;

  AU_RESTORE (au_save);

  if (ovfyn)
    {
      free_and_init (recdes.data);
    }

  /* This prevents from dangling references to serial objects
   * during replication.
   * The typical scenario is to update serials, cull mops which clears
   * the mop up, and then truncate the table which leads updating
   * the serial mop to reset its values.
   */
  if (new_object)
    {
      ws_release_user_instance (new_object);
    }
  new_object = NULL;

  la_release_page_buffer (old_pageid);
  return error;

  /* error */
error_rtn:

  if (error == NO_ERROR)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
    }

  help_sprint_value (&item->key, buf, 255);
#if defined (LA_VERBOSE_DEBUG)
  er_log_debug (ARG_FILE_LINE,
		"apply_insert : error %d %s\n\tclass %s key %s\n",
		error, er_msg (), item->class_name, buf);
#endif
  er_stack_push ();
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	  ER_HA_LA_FAILED_TO_APPLY_INSERT, 3, item->class_name, buf, error);
  er_stack_pop ();

  la_Info.fail_counter++;

  if (ovfyn)
    {
      free_and_init (recdes.data);
    }

  if (new_object)
    {
      /* This prevents from dangling references to serial objects
       * during replication.
       * The typical scenario is to update serials, cull mops which clears
       * the mop up, and then truncate the table which leads updating
       * the serial mop to reset its values.
       */
      ws_release_user_instance (new_object);
      ws_decache (new_object);
      new_object = NULL;
    }

  if (inst_tp)
    {
      dbt_abort_object (inst_tp);
    }

  if (error == ER_NET_CANT_CONNECT_SERVER || error == ER_OBJ_NO_CONNECT)
    {
      error = ER_NET_CANT_CONNECT_SERVER;
    }

  la_release_page_buffer (old_pageid);
  return error;
}

/*
 * la_update_query_execute()
 *   returns  : error code, if execution failed
 *              number of affected objects, if a success
 *   sql(in)
 */
static int
la_update_query_execute (const char *sql, bool au_disable)
{
  int res, au_save;
  DB_QUERY_RESULT *result;
  DB_QUERY_ERROR query_error;

  er_log_debug (ARG_FILE_LINE, "update_query_execute : %s\n", sql);

  if (au_disable)
    {
      /* in order to update 'db_ha_info', disable authorization temporarily */
      AU_DISABLE (au_save);
    }

  res = db_execute (sql, &result, &query_error);
  if (res >= 0)
    {
      int error;

      error = db_query_end (result);
      if (error != NO_ERROR)
	{
	  res = error;
	}
    }

  if (au_disable)
    {
      AU_ENABLE (au_save);
    }

  return res;
}

/*
 * la_update_query_execute_with_values()
 *   returns  : error code, if execution failed
 *              number of affected objects, if a success
 *   sql(in)
 *   arg_count(in)
 *   vals(in)
 *   au_disable(in)
 */
static int
la_update_query_execute_with_values (const char *sql, int arg_count,
				     DB_VALUE * vals, bool au_disable)
{
  int res, au_save;
  DB_QUERY_RESULT *result;
  DB_QUERY_ERROR query_error;

  if (au_disable)
    {
      /* in order to update 'db_ha_info', disable authorization temporarily */
      AU_DISABLE (au_save);
    }

  res = db_execute_with_values (sql, &result, &query_error, arg_count, vals);
  if (res >= 0)
    {
      int error;

      error = db_query_end (result);
      if (error != NO_ERROR)
	{
	  res = error;
	}
    }

  if (au_disable)
    {
      AU_ENABLE (au_save);
    }

  return res;
}

/*
 * la_apply_schema_log() - apply the schema log to the target slave
 *   return: NO_ERROR or error code
 *   item(in): replication item
 *
 * Note:
 */
static int
la_apply_schema_log (LA_ITEM * item)
{
  char *ddl;
  int error = NO_ERROR, error2 = NO_ERROR;
  const char *error_msg = "";
  DB_OBJECT *user = NULL, *save_user = NULL;
  char buf[256];
  char sql_log_err[LINE_MAX];

  error = la_flush_unflushed_insert (NULL);
  if (error != NO_ERROR)
    {
      return error;
    }

  switch (item->item_type)
    {
    case CUBRID_STMT_CREATE_CLASS:
    case CUBRID_STMT_ALTER_CLASS:
    case CUBRID_STMT_RENAME_CLASS:
    case CUBRID_STMT_DROP_CLASS:

    case CUBRID_STMT_CREATE_INDEX:
    case CUBRID_STMT_ALTER_INDEX:
    case CUBRID_STMT_DROP_INDEX:

    case CUBRID_STMT_CREATE_SERIAL:
    case CUBRID_STMT_ALTER_SERIAL:
    case CUBRID_STMT_DROP_SERIAL:

    case CUBRID_STMT_DROP_DATABASE:

    case CUBRID_STMT_CREATE_STORED_PROCEDURE:
    case CUBRID_STMT_ALTER_STORED_PROCEDURE_OWNER:
    case CUBRID_STMT_DROP_STORED_PROCEDURE:

    case CUBRID_STMT_TRUNCATE:

      /* TODO: check it */
    case CUBRID_STMT_CREATE_USER:
    case CUBRID_STMT_ALTER_USER:
    case CUBRID_STMT_DROP_USER:
    case CUBRID_STMT_GRANT:
    case CUBRID_STMT_REVOKE:

      /* TODO: check it */
    case CUBRID_STMT_CREATE_TRIGGER:
    case CUBRID_STMT_RENAME_TRIGGER:
    case CUBRID_STMT_DROP_TRIGGER:
    case CUBRID_STMT_REMOVE_TRIGGER:
    case CUBRID_STMT_SET_TRIGGER:

    case CUBRID_STMT_UPDATE_STATS:

      /*
       * When we create the schema objects, the object's owner must be changed
       * to the appropriate owner.
       * Special alter statement, non partitioned -> partitioned is the same.
       */
      if ((item->item_type == CUBRID_STMT_CREATE_CLASS
	   || item->item_type == CUBRID_STMT_CREATE_SERIAL
	   || item->item_type == CUBRID_STMT_CREATE_STORED_PROCEDURE
	   || item->item_type == CUBRID_STMT_CREATE_TRIGGER
	   || item->item_type == CUBRID_STMT_ALTER_CLASS)
	  && (item->db_user != NULL && item->db_user[0] != '\0'))
	{
	  user = au_find_user (item->db_user);
	  if (user == NULL)
	    {
	      if (er_errid () == ER_NET_CANT_CONNECT_SERVER
		  || er_errid () == ER_OBJ_NO_CONNECT)
		{
		  error = ER_NET_CANT_CONNECT_SERVER;
		}
	      else
		{
		  assert (er_errid () != NO_ERROR);
		  error = er_errid ();
		}

	      error_msg = er_msg ();
	      break;
	    }

	  /* change owner */
	  save_user = Au_user;
	  er_stack_push ();
	  error = AU_SET_USER (user);
	  er_stack_pop ();
	  if (error != NO_ERROR)
	    {
	      save_user = NULL;
	      /* go on with original user */
	    }
	}

      ddl = db_get_string (&item->key);

      /* write sql log */
      if (la_enable_sql_logging)
	{
	  if (sl_write_schema_sql
	      (item->class_name, item->db_user, item->item_type,
	       ddl, item->ha_sys_prm) != NO_ERROR)
	    {
	      help_sprint_value (&item->key, buf, 255);
	      snprintf (sql_log_err, sizeof (sql_log_err),
			"failed to write SQL log. class: %s, key: %s",
			item->class_name, buf);

	      er_stack_push ();
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HA_GENERIC_ERROR,
		      1, sql_log_err);
	      er_stack_pop ();
	    }
	}

      if (item->ha_sys_prm != NULL)
	{
	  er_log_debug (ARG_FILE_LINE, "la_apply_schema_log : %s\n",
			item->ha_sys_prm);
	  er_stack_push ();
	  error2 = db_set_system_parameters_for_ha_repl (item->ha_sys_prm);
	  if (error2 != NO_ERROR)
	    {
	      snprintf (sql_log_err, sizeof (sql_log_err),
			"failed to change sys prm: %s", item->ha_sys_prm);

	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HA_GENERIC_ERROR,
		      1, sql_log_err);
	    }
	  er_stack_pop ();
	}

      if (la_update_query_execute (ddl, false) < 0)
	{
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	  error_msg = er_msg ();
	  if (error == ER_NET_CANT_CONNECT_SERVER
	      || error == ER_OBJ_NO_CONNECT)
	    {
	      error = ER_NET_CANT_CONNECT_SERVER;
	    }
	}
      else
	{
	  la_Info.schema_counter++;
	}

      if (item->ha_sys_prm != NULL)
	{
	  er_log_debug (ARG_FILE_LINE,
			"la_apply_schema_log : reset sysprm\n");
	  er_stack_push ();
	  error2 =
	    db_reset_system_parameters_from_assignments (item->ha_sys_prm);
	  if (error2 != NO_ERROR)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HA_GENERIC_ERROR,
		      1, "failed to reset sys prm");
	    }
	  er_stack_pop ();
	}

      if (save_user != NULL)
	{
	  er_stack_push ();
	  if (AU_SET_USER (save_user))
	    {
	      er_stack_pop ();
	      /* it can be happened */
	      abort ();
	    }
	  er_stack_pop ();
	}
      break;

    case CUBRID_STMT_DROP_LABEL:
    default:
      return NO_ERROR;
    }

  if (error != NO_ERROR)
    {
      help_sprint_value (&item->key, buf, 255);
#if defined (LA_VERBOSE_DEBUG)
      er_log_debug (ARG_FILE_LINE,
		    "apply_schema : error %d class %s key %s\n", error,
		    item->class_name, buf);
#endif
      er_stack_push ();
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_HA_LA_FAILED_TO_APPLY_SCHEMA, 3, item->class_name, buf,
	      error, error_msg);
      er_stack_pop ();

      la_Info.fail_counter++;
    }
  return error;
}

/*
 * la_apply_repl_log() - apply the log to the target slave
 *   return: NO_ERROR or error code
 *   tranid: the target transaction id
 *   rectype: the target log record type
 *   final_pageid : the final pageid
 *
 * Note:
 *    This function is called when the APPLY thread meets the LOG_COMMIT
 *    record.
 */
static int
la_apply_repl_log (int tranid, int rectype, LOG_LSA * commit_lsa,
		   int *total_rows, LOG_PAGEID final_pageid)
{
  LA_ITEM *item = NULL;
  LA_ITEM *next_item = NULL;
  int error = NO_ERROR;
  int errid;
  LA_APPLY *apply;
  int apply_repl_log_cnt = 0;
  char error_string[1024];
  char buf[256];
  static unsigned int total_repl_items = 0;
  bool release_pb = false;
  bool has_more_commit_items = false;

  apply = la_find_apply_list (tranid);
  if (apply == NULL)
    {
      return NO_ERROR;
    }

  if (rectype == LOG_ABORT)
    {
      la_clear_applied_info (apply);
      return NO_ERROR;
    }

  if (apply->head == NULL || LSA_LE (commit_lsa, &la_Info.last_committed_lsa))
    {
      if (rectype == LOG_COMMIT_TOPOPE)
	{
	  la_free_all_repl_items (apply);
	}
      else
	{
	  la_clear_applied_info (apply);
	}

      return NO_ERROR;
    }

  error = la_lock_dbname (&la_Info.db_lockf_vdes, la_slave_db_name,
			  la_Info.log_path);
  assert_release (error == NO_ERROR);

  item = apply->head;
  while (item)
    {
      error = NO_ERROR;

      total_repl_items++;
      release_pb =
	((total_repl_items % LA_MAX_REPL_ITEM_WITHOUT_RELEASE_PB) == 0)
	? true : false;

      if (final_pageid != NULL_PAGEID && release_pb == true)
	{
	  la_release_all_page_buffers (final_pageid);
	}

      if (LSA_GT (&item->lsa, &la_Info.last_committed_rep_lsa))
	{
	  if ((total_repl_items % LA_MAX_REPL_ITEMS) == 0)
	    {
	      error = la_log_commit (true);
	      if (error != NO_ERROR)
		{
		  goto end;
		}

	      error = la_check_mem_size ();
	      if (error == ER_HA_LA_EXCEED_MAX_MEM_SIZE)
		{
		  goto end;
		}
	    }

	  if (item->log_type == LOG_REPLICATION_DATA)
	    {
	      switch (item->item_type)
		{
		case RVREPL_DATA_UPDATE_START:
		case RVREPL_DATA_UPDATE_END:
		case RVREPL_DATA_UPDATE:
		  if (la_use_server_side_update_repl)
		    {
		      error = la_apply_update_log_server_side (item);
		    }
		  else
		    {
		      error = la_apply_update_log (item);
		    }
		  break;

		case RVREPL_DATA_INSERT:
		  error = la_apply_insert_log (item);
		  break;

		case RVREPL_DATA_DELETE:
		  error = la_apply_delete_log (item);
		  break;

		default:
		  er_log_debug (ARG_FILE_LINE,
				"apply_repl_log : log_type %d item_type %d\n",
				item->log_type, item->item_type);

		  assert_release (false);
		}
	    }
	  else if (item->log_type == LOG_REPLICATION_SCHEMA)
	    {
	      error = la_apply_schema_log (item);
	    }
	  else
	    {
	      er_log_debug (ARG_FILE_LINE,
			    "apply_repl_log : log_type %d item_type\n",
			    item->log_type, item->item_type);

	      assert_release (false);
	    }

	  apply_repl_log_cnt++;

	  if (error == NO_ERROR)
	    {
	      LSA_COPY (&la_Info.committed_rep_lsa, &item->lsa);
	    }
	  else
	    {
	      /* reconnect to server due to error in flushing unflushed insert */
	      if (error == ER_LC_PARTIALLY_FAILED_TO_FLUSH
		  && la_applier_need_shutdown == true)
		{
		  goto end;
		}

	      assert (er_errid () != NO_ERROR);
	      errid = er_errid ();

	      help_sprint_value (&item->key, buf, 255);
	      sprintf (error_string, "[%s,%s] %s",
		       item->class_name, buf, db_error_string (1));
	      er_log_debug (ARG_FILE_LINE,
			    "Internal system failure: %s", error_string);

	      if (errid == ER_NET_CANT_CONNECT_SERVER
		  || errid == ER_OBJ_NO_CONNECT)
		{
		  error = ER_NET_CANT_CONNECT_SERVER;
		  goto end;
		}
	      else if (la_ignore_on_error (errid) == false
		       && la_retry_on_error (errid) == true)
		{
		  snprintf (buf, sizeof (buf),
			    "attempts to try applying failed replication log again. (error:%d)",
			    errid);
		  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE,
			  ER_HA_GENERIC_ERROR, 1, buf);

		  /* try it again */
		  LA_SLEEP (10, 0);
		  continue;
		}
	    }
	}

      next_item =
	la_get_next_repl_item (item, apply->is_long_trans, &apply->last_lsa);
      la_free_repl_item (apply, item);
      item = next_item;

      if ((item != NULL) && LSA_GT (&item->lsa, commit_lsa))
	{
	  assert (rectype == LOG_COMMIT_TOPOPE);
	  has_more_commit_items = true;
	  break;
	}
    }

end:
  *total_rows += apply_repl_log_cnt;

  if (rectype == LOG_COMMIT_TOPOPE)
    {
      if (has_more_commit_items)
	{
	  la_free_and_add_next_repl_item (apply, item, commit_lsa);
	}
      else
	{
	  la_free_all_repl_items (apply);
	}
    }
  else
    {
      la_clear_applied_info (apply);
    }

  return error;
}

/*
 * la_apply_commit_list() - apply the log to the target slave
 *   return: NO_ERROR or error code
 *   lsa   : the target LSA of the log
 *   final_pageid : the final pageid
 *
 * Note:
 *    This function is called when the APPLY thread meets the LOG_COMMIT
 *    record.
 */
static int
la_apply_commit_list (LOG_LSA * lsa, LOG_PAGEID final_pageid)
{
  LA_COMMIT *commit;
  int error = NO_ERROR;

  LSA_SET_NULL (lsa);

  commit = la_Info.commit_head;
  if (commit
      && (commit->type == LOG_COMMIT
	  || commit->type == LOG_COMMIT_TOPOPE || commit->type == LOG_ABORT))
    {
      error =
	la_apply_repl_log (commit->tranid, commit->type, &commit->log_lsa,
			   &la_Info.total_rows, final_pageid);
      if (error != NO_ERROR)
	{
	  er_log_debug (ARG_FILE_LINE,
			"apply_commit_list : error %d while apply_repl_log\n",
			error);
	}

      LSA_COPY (lsa, &commit->log_lsa);

      if (commit->type == LOG_COMMIT)
	{
	  la_Info.log_record_time = commit->log_record_time;
	}

      if (commit->next != NULL)
	{
	  commit->next->prev = NULL;
	}
      la_Info.commit_head = commit->next;
      if (la_Info.commit_head == NULL)
	{
	  la_Info.commit_tail = NULL;
	}

      free_and_init (commit);
    }

  return error;
}

/*
 * la_free_repl_items_by_tranid() - clear replication item using tranid
 *   return: none
 *   tranid: transaction id
 *
 * Note:
 *       clear the applied list area after processing ..
 *       When we meet the LOG_ABORT_TOPOPE or LOG_ABORT record,
 *       we have to clear the replication items of the target transaction.
 *       In case of LOG_ABORT_TOPOPE, the apply list should be preserved
 *       for the later use (so call la_clear_applied_info() using
 *       false as the second argument).
 */
static void
la_free_repl_items_by_tranid (int tranid)
{
  LA_APPLY *apply;
  LA_COMMIT *commit, *commit_next;

  apply = la_find_apply_list (tranid);
  if (apply)
    {
      la_clear_applied_info (apply);
    }

  for (commit = la_Info.commit_head; commit; commit = commit_next)
    {
      commit_next = commit->next;

      if (commit->tranid == tranid)
	{
	  if (commit->next)
	    {
	      commit->next->prev = commit->prev;
	    }
	  else
	    {
	      la_Info.commit_tail = commit->prev;
	    }

	  if (commit->prev)
	    {
	      commit->prev->next = commit->next;
	    }
	  else
	    {
	      la_Info.commit_head = commit->next;
	    }

	  commit->next = NULL;
	  commit->prev = NULL;

	  free_and_init (commit);
	}
    }

  if (la_Info.commit_head == NULL)
    {
      la_Info.commit_tail = NULL;
    }

  return;
}

static LA_ITEM *
la_get_next_repl_item (LA_ITEM * item, bool is_long_trans, LOG_LSA * last_lsa)
{
  if (is_long_trans)
    {
      return la_get_next_repl_item_from_log (item, last_lsa);
    }
  else
    {
      return la_get_next_repl_item_from_list (item);
    }
}

static LA_ITEM *
la_get_next_repl_item_from_list (LA_ITEM * item)
{
  return (item->next);
}

static LA_ITEM *
la_get_next_repl_item_from_log (LA_ITEM * item, LOG_LSA * last_lsa)
{
  LOG_LSA prev_repl_lsa;
  LOG_LSA curr_lsa;
  LOG_PAGE *curr_log_page;
  LOG_RECORD_HEADER *prev_repl_log_record = NULL;
  LOG_RECORD_HEADER *curr_log_record;
  LA_ITEM *next_item = NULL;

  LSA_COPY (&prev_repl_lsa, &item->lsa);
  LSA_COPY (&curr_lsa, &item->lsa);

  while (!LSA_ISNULL (&curr_lsa))
    {
      curr_log_page = la_get_page (curr_lsa.pageid);
      curr_log_record = LOG_GET_LOG_RECORD_HEADER (curr_log_page, &curr_lsa);

      if (prev_repl_log_record == NULL)
	{
	  prev_repl_log_record =
	    (LOG_RECORD_HEADER *) malloc (sizeof (LOG_RECORD_HEADER));
	  if (prev_repl_log_record == NULL)
	    {
	      return NULL;
	    }

	  memcpy (prev_repl_log_record, curr_log_record,
		  sizeof (LOG_RECORD_HEADER));
	}
      if (!LSA_EQ (&curr_lsa, &prev_repl_lsa)
	  && prev_repl_log_record->trid == curr_log_record->trid)
	{
	  if (LSA_GT (&curr_lsa, last_lsa)
	      || curr_log_record->type == LOG_COMMIT
	      || curr_log_record->type == LOG_ABORT
	      || LSA_GE (&curr_lsa, &la_Info.act_log.log_hdr->eof_lsa))
	    {
	      break;
	    }

	  if (curr_log_record->type == LOG_REPLICATION_DATA
	      || curr_log_record->type == LOG_REPLICATION_SCHEMA)
	    {
	      next_item =
		la_make_repl_item (curr_log_page, curr_log_record->type,
				   curr_log_record->trid, &curr_lsa);
	      assert (next_item);

	      break;
	    }

	}
      la_release_page_buffer (curr_lsa.pageid);
      LSA_COPY (&curr_lsa, &curr_log_record->forw_lsa);
    }

  if (prev_repl_log_record)
    {
      free_and_init (prev_repl_log_record);
    }

  return next_item;
}


static int
la_log_record_process (LOG_RECORD_HEADER * lrec,
		       LOG_LSA * final, LOG_PAGE * pg_ptr)
{
  LA_APPLY *apply = NULL;
  int error = NO_ERROR;
  LOG_LSA lsa_apply;
  LOG_LSA required_lsa;
  LOG_PAGEID final_pageid;
  int commit_list_count;
  struct log_ha_server_state *ha_server_state;
  char buffer[256];

  if (lrec->trid == NULL_TRANID ||
      LSA_GT (&lrec->prev_tranlsa, final) || LSA_GT (&lrec->back_lsa, final))
    {
      if (lrec->type != LOG_END_OF_LOG && lrec->type != LOG_DUMMY_FILLPAGE_FORARCHIVE)	/* for backward compatibility */
	{
	  la_applier_need_shutdown = true;

	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_HA_LA_INVALID_REPL_LOG_RECORD, 10,
		  final->pageid, final->offset,
		  lrec->forw_lsa.pageid, lrec->forw_lsa.offset,
		  lrec->back_lsa.pageid, lrec->back_lsa.offset,
		  lrec->trid,
		  lrec->prev_tranlsa.pageid, lrec->prev_tranlsa.offset,
		  lrec->type);
	  return ER_LOG_PAGE_CORRUPTED;
	}
    }

  if ((lrec->type != LOG_END_OF_LOG
       && lrec->type != LOG_DUMMY_HA_SERVER_STATE)
      && (lrec->trid != LOG_SYSTEM_TRANID)
      && (LSA_ISNULL (&lrec->prev_tranlsa)))
    {
      apply = la_add_apply_list (lrec->trid);
      if (apply == NULL)
	{
	  la_applier_need_shutdown = true;

	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	  if (error != NO_ERROR)
	    {
	      return error;
	    }
	  else
	    {
	      return ER_FAILED;
	    }
	}
      if (LSA_ISNULL (&apply->start_lsa))
	{
	  LSA_COPY (&apply->start_lsa, final);
	}
    }

  la_Info.is_end_of_record = false;
  switch (lrec->type)
    {
      /*
       * This record is not generated no more.
       * It's kept for backward compatibility.
       */
    case LOG_DUMMY_FILLPAGE_FORARCHIVE:
      snprintf (buffer, sizeof (buffer),
		"process log record (type:%d). "
		"skip this log page. LSA: %lld|%d",
		lrec->type, (long long int) final->pageid,
		(int) final->offset);
      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_HA_GENERIC_ERROR, 1,
	      buffer);

      final->pageid++;
      final->offset = 0;
      return ER_INTERRUPTED;

    case LOG_END_OF_LOG:
      if (la_does_page_exist (final->pageid + 1)
	  && la_does_page_exist (final->pageid) ==
	  LA_PAGE_EXST_IN_ARCHIVE_LOG)
	{
	  /* when we meet the END_OF_LOG of archive file, skip log page */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_HA_LA_UNEXPECTED_EOF_IN_ARCHIVE_LOG, 2,
		  final->pageid, final->offset);
	  final->pageid++;
	  final->offset = 0;
	}
      else
	{
	  /* we meet the END_OF_LOG */
#if defined (LA_VERBOSE_DEBUG)
	  er_log_debug (ARG_FILE_LINE,
			"reached END_OF_LOG in active log. LSA(%d|%d)",
			final->pageid, final->offset);
#endif
	  la_Info.is_end_of_record = true;
	}
      return ER_INTERRUPTED;

    case LOG_REPLICATION_DATA:
    case LOG_REPLICATION_SCHEMA:
      /* add the replication log to the target transaction */
      error = la_set_repl_log (pg_ptr, lrec->type, lrec->trid, final);
      if (error != NO_ERROR)
	{
	  la_applier_need_shutdown = true;
	  return error;
	}
      break;

    case LOG_UNLOCK_COMMIT:
    case LOG_COMMIT_TOPOPE:
      /* add the repl_list to the commit_list  */
      error = la_add_unlock_commit_log (lrec->trid, final);
      if (error != NO_ERROR)
	{
	  la_applier_need_shutdown = true;
	  return error;
	}

      if (lrec->type != LOG_COMMIT_TOPOPE)
	{
	  break;
	}

    case LOG_COMMIT:
      /* apply the replication log to the slave */
      if (LSA_GT (final, &la_Info.committed_lsa))
	{
	  time_t eot_time;

	  if (lrec->type == LOG_COMMIT_TOPOPE)
	    {
	      eot_time = 0;
	    }
	  else
	    {
	      eot_time = la_retrieve_eot_time (pg_ptr, final);
	    }

	  commit_list_count =
	    la_set_commit_log (lrec->trid, lrec->type, final, eot_time);
	  if (commit_list_count <= 0)
	    {
	      er_log_debug (ARG_FILE_LINE,
			    "Cannot find commit list with Trid %d LSA[%d:%d]",
			    lrec->trid, final->pageid, final->offset);
	      la_free_repl_items_by_tranid (lrec->trid);
	      break;
	    }

	  /* in case of delayed/time-bound replication */
	  if (eot_time != 0)
	    {
	      la_delay_replica (eot_time);
	    }

	  /* make db_ha_apply_info.status busy */
	  if (la_Info.status == LA_STATUS_IDLE)
	    {
	      la_Info.status = LA_STATUS_BUSY;
	    }

	  final_pageid = (pg_ptr) ? pg_ptr->hdr.logical_pageid : NULL_PAGEID;
	  do
	    {
	      error = la_apply_commit_list (&lsa_apply, final_pageid);
	      if (error == ER_NET_CANT_CONNECT_SERVER)
		{
		  switch (er_errid ())
		    {
		    case ER_TM_SERVER_DOWN_UNILATERALLY_ABORTED:
		      break;
		    case ER_LK_UNILATERALLY_ABORTED:
		      break;
		    default:
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			      ER_NET_SERVER_COMM_ERROR, 1,
			      "cannot connect with server");
		      la_applier_need_shutdown = true;
		      return error;
		    }
		}
	      else if (error == ER_HA_LA_EXCEED_MAX_MEM_SIZE)
		{
		  la_applier_need_shutdown = true;
		  return error;
		}
	      else if (error == ER_LC_PARTIALLY_FAILED_TO_FLUSH
		       && la_applier_need_shutdown == true)
		{
		  return error;
		}

	      if (!LSA_ISNULL (&lsa_apply))
		{
		  LSA_COPY (&(la_Info.committed_lsa), &lsa_apply);

		  if (lrec->type == LOG_COMMIT)
		    {
		      la_Info.commit_counter++;
		    }
		}
	    }
	  while (!LSA_ISNULL (&lsa_apply));	/* if lsa_apply is not null then
						 * there is the replication log
						 * applying to the slave
						 */
	}
      else
	{
	  la_free_repl_items_by_tranid (lrec->trid);
	}
      break;

      /* you have to check the ABORT LOG to avoid the memory leak */
    case LOG_UNLOCK_ABORT:
      break;

    case LOG_ABORT:
      error = la_add_abort_log (lrec->trid, final);
      if (error != NO_ERROR)
	{
	  la_applier_need_shutdown = true;
	  return error;
	}
      break;

    case LOG_DUMMY_CRASH_RECOVERY:
      snprintf (buffer, sizeof (buffer),
		"process log record (type:%d). "
		"skip this log record. LSA: %lld|%d",
		lrec->type, (long long int) final->pageid,
		(int) final->offset);
      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_HA_GENERIC_ERROR, 1,
	      buffer);

      LSA_COPY (final, &lrec->forw_lsa);
      return ER_INTERRUPTED;

    case LOG_END_CHKPT:
      break;

    case LOG_DUMMY_HA_SERVER_STATE:
      ha_server_state = la_get_ha_server_state (pg_ptr, final);
      if (ha_server_state == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HA_GENERIC_ERROR, 1,
		  "failed to read LOG_DUMMY_HA_SERVER_STATE");
	  break;
	}

      if (ha_server_state->state != HA_SERVER_STATE_ACTIVE
	  && ha_server_state->state != HA_SERVER_STATE_TO_BE_STANDBY)
	{
	  if (la_Info.db_lockf_vdes != NULL_VOLDES)
	    {
	      snprintf (buffer, sizeof (buffer),
			"the state of HA server (%s@%s) is changed to %s",
			la_slave_db_name, la_peer_host,
			css_ha_server_state_string (ha_server_state->state));
	      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE,
		      ER_HA_GENERIC_ERROR, 1, buffer);

	      la_Info.is_role_changed = true;

	      return ER_INTERRUPTED;
	    }
	}
      else if (la_is_repl_lists_empty ())
	{
	  (void) la_update_ha_apply_info_log_record_time (ha_server_state->
							  at_time);
	  error = la_log_commit (true);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }
	  (void) la_log_commit (true);
	}
      break;

    default:
      break;
    }				/* switch(lrec->type) */

  /*
   * if this is the final record of the archive log..
   * we have to fetch the next page. So, increase the pageid,
   * but we don't know the exact offset of the next record.
   * the offset would be adjusted after getting the next log page
   */
  if (lrec->forw_lsa.pageid == -1 ||
      lrec->type <= LOG_SMALLER_LOGREC_TYPE ||
      lrec->type >= LOG_LARGER_LOGREC_TYPE)
    {
      if (la_does_page_exist (final->pageid) == LA_PAGE_EXST_IN_ARCHIVE_LOG)
	{
	  snprintf (buffer, sizeof (buffer),
		    "process last log record in archive. LSA: %lld|%d",
		    (long long int) final->pageid, (int) final->offset);
	  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE,
		  ER_HA_GENERIC_ERROR, 1, buffer);

	  final->pageid++;
	  final->offset = 0;
	}

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_HA_LA_INVALID_REPL_LOG_RECORD, 10,
	      final->pageid, final->offset,
	      lrec->forw_lsa.pageid, lrec->forw_lsa.offset,
	      lrec->back_lsa.pageid, lrec->back_lsa.offset,
	      lrec->trid,
	      lrec->prev_tranlsa.pageid, lrec->prev_tranlsa.offset,
	      lrec->type);

      return ER_LOG_PAGE_CORRUPTED;
    }

  return NO_ERROR;
}

static char *
la_get_hostname_from_log_path (char *log_path)
{
  char *hostname = NULL, *p;

  if (log_path == NULL)
    {
      return NULL;
    }

  p = log_path;
  p += (strlen (log_path) - 1);

  /* log_path: "path/dbname_hostname/" */
  if (*p == '/')
    {
      p--;
    }

  while (*p != '/')
    {
      p--;
      if (p == log_path)
	{
	  return NULL;
	}
    }

  hostname = strstr (p, la_slave_db_name);
  if (hostname == NULL)
    {
      return NULL;
    }

  hostname += strlen (la_slave_db_name);
  if (*hostname != '_')
    {
      return NULL;
    }

  return hostname + 1;
}

static int
la_change_state (void)
{
  int error = NO_ERROR;
  int new_state = HA_LOG_APPLIER_STATE_NA;
  char buffer[1024];

  if (la_Info.last_server_state == la_Info.act_log.log_hdr->ha_server_state
      && la_Info.last_file_state == la_Info.act_log.log_hdr->ha_file_status
      && la_Info.last_is_end_of_record == la_Info.is_end_of_record)
    {
      /* there are no need to change */
      return NO_ERROR;
    }

  if (la_Info.last_server_state != la_Info.act_log.log_hdr->ha_server_state)
    {
      sprintf (buffer,
	       "change the state of HA server (%s@%s) from '%s' to '%s'",
	       la_slave_db_name, la_peer_host,
	       css_ha_server_state_string (la_Info.last_server_state),
	       css_ha_server_state_string (la_Info.act_log.log_hdr->
					   ha_server_state));
      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_HA_GENERIC_ERROR,
	      1, buffer);
    }

  la_Info.last_server_state = la_Info.act_log.log_hdr->ha_server_state;
  la_Info.last_file_state = la_Info.act_log.log_hdr->ha_file_status;
  la_Info.last_is_end_of_record = la_Info.is_end_of_record;

  /* check log file status */
  if (la_Info.is_end_of_record == true
      && (la_Info.act_log.log_hdr->ha_file_status ==
	  LOG_HA_FILESTAT_SYNCHRONIZED))
    {
      /* check server's state with log header */
      switch (la_Info.act_log.log_hdr->ha_server_state)
	{
	case HA_SERVER_STATE_ACTIVE:
	case HA_SERVER_STATE_TO_BE_STANDBY:
	case HA_SERVER_STATE_TO_BE_ACTIVE:
	  if (la_Info.apply_state != HA_LOG_APPLIER_STATE_WORKING)
	    {
	      /* notify to slave db */
	      new_state = HA_LOG_APPLIER_STATE_WORKING;
	    }
	  break;

	case HA_SERVER_STATE_DEAD:
	case HA_SERVER_STATE_STANDBY:
	case HA_SERVER_STATE_MAINTENANCE:
	  if (la_Info.apply_state != HA_LOG_APPLIER_STATE_DONE)
	    {
	      /* notify to slave db */
	      new_state = HA_LOG_APPLIER_STATE_DONE;

	      /* clear all repl_lists */
	      if (la_Info.act_log.log_hdr->ha_server_state !=
		  HA_SERVER_STATE_DEAD)
		{
		  la_clear_all_repl_and_commit_list ();
		}
	    }
	  break;
	default:
	  er_log_debug (ARG_FILE_LINE, "BUG. Unknown LOG_HA_SRVSTAT (%x)",
			la_Info.act_log.log_hdr->ha_server_state);
	  return ER_FAILED;
	  break;
	}

    }
  else
    {
      switch (la_Info.act_log.log_hdr->ha_server_state)
	{
	case HA_SERVER_STATE_ACTIVE:
	case HA_SERVER_STATE_TO_BE_STANDBY:
	  if (la_Info.apply_state != HA_LOG_APPLIER_STATE_WORKING
	      && la_Info.apply_state != HA_LOG_APPLIER_STATE_RECOVERING)
	    {
	      new_state = HA_LOG_APPLIER_STATE_RECOVERING;
	    }
	  break;
	case HA_SERVER_STATE_TO_BE_ACTIVE:
	case HA_SERVER_STATE_STANDBY:
	case HA_SERVER_STATE_DEAD:
	case HA_SERVER_STATE_MAINTENANCE:
	  if (la_Info.apply_state != HA_LOG_APPLIER_STATE_DONE
	      && la_Info.apply_state != HA_LOG_APPLIER_STATE_RECOVERING)
	    {
	      new_state = HA_LOG_APPLIER_STATE_RECOVERING;
	    }
	  break;
	default:
	  er_log_debug (ARG_FILE_LINE, "BUG. Unknown LOG_HA_SRVSTAT (%x)",
			la_Info.act_log.log_hdr->ha_server_state);
	  return ER_FAILED;
	  break;
	}
    }

  if (new_state != HA_LOG_APPLIER_STATE_NA)
    {
      if (la_Info.apply_state == new_state)
	{
	  return NO_ERROR;
	}

      /* force commit when state is changing */
      error = la_log_commit (true);
      if (error != NO_ERROR)
	{
	  return error;
	}

      error = boot_notify_ha_log_applier_state (new_state);
      if (error == NO_ERROR)
	{
	  snprintf (buffer, sizeof (buffer),
		    "change log apply state from '%s' to '%s'. "
		    "last committed LSA: %lld|%d",
		    css_ha_applier_state_string (la_Info.apply_state),
		    css_ha_applier_state_string (new_state),
		    (long long int) la_Info.committed_lsa.pageid,
		    (int) la_Info.committed_lsa.offset);
	  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE,
		  ER_HA_GENERIC_ERROR, 1, buffer);

	  la_Info.apply_state = new_state;
	}
      else
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_HA_LA_FAILED_TO_CHANGE_STATE, 2,
		  css_ha_applier_state_string (la_Info.apply_state),
		  css_ha_applier_state_string (new_state));
	}
    }

  return error;
}

/*
 * la_log_commit() -
 *   return: NO_ERROR or error code
 */
static int
la_log_commit (bool update_commit_time)
{
  int res;
  int error = NO_ERROR;

  (void) la_find_required_lsa (&la_Info.required_lsa);

  LSA_COPY (&la_Info.append_lsa, &la_Info.act_log.log_hdr->append_lsa);
  LSA_COPY (&la_Info.eof_lsa, &la_Info.act_log.log_hdr->eof_lsa);

  if (update_commit_time)
    {
      la_Info.log_commit_time = time (0);
    }

  error = la_flush_unflushed_insert (NULL);
  if (error != NO_ERROR)
    {
      return error;
    }

  res = la_update_ha_last_applied_info ();
  if (res > 0)
    {
      error = la_commit_transaction ();
    }
  else
    {
      la_Info.fail_counter++;

      er_log_debug (ARG_FILE_LINE,
		    "log applied but cannot update last committed LSA "
		    "(%d|%d)",
		    la_Info.committed_lsa.pageid,
		    la_Info.committed_lsa.offset);
      if (res == ER_NET_CANT_CONNECT_SERVER || res == ER_OBJ_NO_CONNECT)
	{
	  error = ER_NET_CANT_CONNECT_SERVER;
	}
      else
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HA_GENERIC_ERROR,
		  1, "failed to update db_ha_apply_info");
	  error = NO_ERROR;
	}
    }

  return error;
}

/*
 * la_get_mem_size () - get mem size with own pid
 */
static unsigned long
la_get_mem_size (void)
{
  unsigned long vsize = 0;
#if defined(LINUX)
  FILE *fp;

  fp = fopen ("/proc/self/statm", "r");
  if (fp != NULL)
    {
      fscanf (fp, "%lu", &vsize);
      /* page to Kbyte */
      vsize *= (sysconf (_SC_PAGESIZE) / ONE_K);
      fclose (fp);
    }
#elif defined(AIX)
  struct procentry64 entry;
  pid_t pid = getpid ();

  if (getprocs64 (&entry, sizeof (entry), NULL, 0, &pid, 1))
    {
      vsize = (unsigned long) entry.pi_dvm * (sysconf (_SC_PAGESIZE) / ONE_K);
    }
#else
#       error
#endif
  return vsize;
}

static int
la_check_mem_size (void)
{
  int error = NO_ERROR;
  unsigned long vsize;
  unsigned long max_vsize;
  int diff_sec;
  time_t now;

  now = time (NULL);
  diff_sec = now - la_Info.start_time;
  if (diff_sec >= 0 && diff_sec <= HB_START_WAITING_TIME_IN_SECS)
    {
      return NO_ERROR;
    }

  vsize = la_get_mem_size ();
#if defined(AIX)
  max_vsize = MAX ((unsigned long) (la_Info.max_mem_size * ONE_K),
		   (la_Info.start_vsize * 60));
#else
  max_vsize = MAX ((unsigned long) (la_Info.max_mem_size * ONE_K),
		   (la_Info.start_vsize * 2));
#endif
  if (vsize > max_vsize)
    {
      /*
       * vmem size is more than max_mem_size
       * or grow more than 2 times
       */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_HA_LA_EXCEED_MAX_MEM_SIZE, 7,
	      (vsize / ONE_K),
	      la_Info.max_mem_size,
	      (la_Info.start_vsize / ONE_K),
	      la_Info.required_lsa.pageid,
	      la_Info.required_lsa.offset,
	      la_Info.committed_lsa.pageid, la_Info.committed_lsa.offset);
      error = ER_HA_LA_EXCEED_MAX_MEM_SIZE;
    }

  return error;
}

int
la_commit_transaction (void)
{
  int error = NO_ERROR;
  static int last_time = 0;
  static unsigned long long last_applied_item = 0;
  int curr_time;
  int diff_time;
  unsigned long long curr_applied_item;
  unsigned long long diff_applied_item;
  unsigned long ws_cull_mops_interval;
  unsigned long ws_cull_mops_per_apply;
  int start_time;

  if (last_time == 0)
    {
      last_time = time (NULL);
    }

  if (last_applied_item == 0)
    {
      last_applied_item =
	la_Info.insert_counter + la_Info.update_counter +
	la_Info.delete_counter + la_Info.fail_counter;
    }

  error = db_commit_transaction ();
  if (error != NO_ERROR)
    {
      return error;
    }

  curr_time = time (NULL);
  diff_time = curr_time - last_time;

  curr_applied_item =
    la_Info.insert_counter + la_Info.update_counter +
    la_Info.delete_counter + la_Info.fail_counter;
  diff_applied_item = curr_applied_item - last_applied_item;

  start_time = curr_time - la_Info.start_time;
  if (start_time >= 0 && start_time <= HB_START_WAITING_TIME_IN_SECS)
    {
      ws_cull_mops_interval = LA_WS_CULL_MOPS_INTERVAL_MIN;
      ws_cull_mops_per_apply = LA_WS_CULL_MOPS_PER_APPLY_MIN;
    }
  else
    {
      ws_cull_mops_interval = LA_WS_CULL_MOPS_INTERVAL;
      ws_cull_mops_per_apply = LA_WS_CULL_MOPS_PER_APPLY;
    }

  if (diff_time >= ws_cull_mops_interval
      || diff_applied_item >= ws_cull_mops_per_apply)
    {
      ws_filter_dirty ();
      ws_cull_mops ();

      last_time = curr_time;
      last_applied_item = curr_applied_item;
    }

  return error;
}

static int
la_check_time_commit (struct timeval *time_commit, unsigned int threshold)
{
  int error = NO_ERROR;
  struct timeval curtime;
  int diff_msec;
  bool need_commit = false;

  assert (time_commit);

  /* check interval time for commit */
  gettimeofday (&curtime, NULL);
  diff_msec =
    (curtime.tv_sec - time_commit->tv_sec) * 1000 +
    (curtime.tv_usec / 1000 - time_commit->tv_usec / 1000);
  if (diff_msec < 0)
    {
      diff_msec = 0;
    }

  if (threshold < (unsigned int) diff_msec)
    {
      gettimeofday (time_commit, NULL);

      /* check server is connected now */
      error = db_ping_server (0, NULL);
      if (error != NO_ERROR)
	{
	  return ER_NET_CANT_CONNECT_SERVER;
	}

      /* check for # of rows to commit */
      if (la_Info.prev_total_rows != la_Info.total_rows)
	{
	  need_commit = true;
	}

      if (need_commit == true || la_Info.is_apply_info_updated == true)
	{
	  error = la_log_commit (need_commit);
	  if (error == NO_ERROR)
	    {
	      /* sync with new one */
	      la_Info.prev_total_rows = la_Info.total_rows;
	      la_Info.is_apply_info_updated = false;
	    }
	}
      else
	{
	  if (la_Info.status == LA_STATUS_BUSY)
	    {
	      /* make db_ha_apply_info.status idle */
	      la_Info.status = LA_STATUS_IDLE;
	    }
	}
    }

  return error;
}

static int
la_check_duplicated (const char *logpath, const char *dbname,
		     int *lockf_vdes, int *last_deleted_arv_num)
{
  char lock_path[PATH_MAX];
  FILEIO_LOCKF_TYPE lockf_type = FILEIO_NOT_LOCKF;

  sprintf (lock_path, "%s%s%s%s", logpath, FILEIO_PATH_SEPARATOR (logpath),
	   dbname, LA_LOCK_SUFFIX);

  *lockf_vdes = fileio_open (lock_path, O_RDWR | O_CREAT, 0644);
  if (*lockf_vdes == NULL_VOLDES)
    {
      er_log_debug (ARG_FILE_LINE, "unable to open lock_file (%s)",
		    lock_path);
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_IO_MOUNT_FAIL, 1, lock_path);
      return ER_IO_MOUNT_FAIL;
    }

  lockf_type =
    fileio_lock_la_log_path (dbname, lock_path, *lockf_vdes,
			     last_deleted_arv_num);
  if (lockf_type == FILEIO_NOT_LOCKF)
    {
      er_log_debug (ARG_FILE_LINE, "unable to wlock lock_file (%s)",
		    lock_path);
      fileio_close (*lockf_vdes);
      *lockf_vdes = NULL_VOLDES;
      return ER_FAILED;
    }

#if defined (LA_VERBOSE_DEBUG)
  er_log_debug (ARG_FILE_LINE, "last_deleted_arv_num is %d.",
		*last_deleted_arv_num);
#endif

  return NO_ERROR;
}

static int
la_update_last_deleted_arv_num (int lockf_vdes, int last_deleted_arv_num)
{
  char arv_num_str[11];
  int len;

  if (lockf_vdes == NULL_VOLDES || last_deleted_arv_num < 0)
    {
      return ER_FAILED;
    }

  snprintf (arv_num_str, sizeof (arv_num_str), "%-10d", last_deleted_arv_num);

  if ((lseek (lockf_vdes, 0, SEEK_SET) != 0))
    {
      return ER_FAILED;
    }

  len = write (lockf_vdes, arv_num_str, sizeof (arv_num_str) - 1);
  if (len != (sizeof (arv_num_str) - 1))
    {
      return ER_FAILED;
    }

  return NO_ERROR;
}

static int
la_lock_dbname (int *lockf_vdes, char *db_name, char *log_path)
{
  int error = NO_ERROR;
  FILEIO_LOCKF_TYPE result;

  if (*lockf_vdes != NULL_VOLDES)
    {
      return NO_ERROR;
    }

  while (1)
    {
      result = fileio_lock_la_dbname (lockf_vdes, db_name, log_path);
      if (result == FILEIO_LOCKF)
	{
	  break;
	}

      LA_SLEEP (3, 0);
    }

  assert_release ((*lockf_vdes) != NULL_VOLDES);

  la_Info.is_role_changed = false;

  return error;
}


static int
la_unlock_dbname (int *lockf_vdes, char *db_name, bool clear_owner)
{
  int error = NO_ERROR;
  int result;

  if ((*lockf_vdes) == NULL_VOLDES)
    {
      return NO_ERROR;
    }

  result = fileio_unlock_la_dbname (lockf_vdes, db_name, clear_owner);
  if (result == FILEIO_LOCKF)
    {
      return ER_FAILED;
    }

  la_Info.is_role_changed = false;

  if (clear_owner)
    {
      LA_SLEEP (60, 0);
    }

  return error;
}

static void
la_init (const char *log_path, const int max_mem_size, BOOT_CLIENT_TYPE type)
{
  static unsigned long start_vsize = 0;

  if (type == BOOT_CLIENT_LOG_APPLIER)
    {
      er_log_debug (ARG_FILE_LINE, "log applier will be initialized...");
    }
  else
    {
      er_log_debug (ARG_FILE_LINE, "log prefetcher will be initialized...");
    }

  memset (&la_Info, 0, sizeof (la_Info));

  strncpy (la_Info.log_path, log_path, PATH_MAX - 1);

  la_Info.cache_buffer_size = LA_DEFAULT_CACHE_BUFFER_SIZE;
  la_Info.act_log.db_iopagesize = LA_DEFAULT_LOG_PAGE_SIZE;
  la_Info.act_log.db_logpagesize = LA_DEFAULT_LOG_PAGE_SIZE;

  la_Info.act_log.log_vdes = NULL_VOLDES;
  la_Info.arv_log.log_vdes = NULL_VOLDES;

  LSA_SET_NULL (&la_Info.committed_lsa);
  LSA_SET_NULL (&la_Info.committed_rep_lsa);
  LSA_SET_NULL (&la_Info.append_lsa);
  LSA_SET_NULL (&la_Info.eof_lsa);
  LSA_SET_NULL (&la_Info.required_lsa);
  LSA_SET_NULL (&la_Info.final_lsa);
  LSA_SET_NULL (&la_Info.last_committed_lsa);
  LSA_SET_NULL (&la_Info.last_committed_rep_lsa);

  la_Info.last_deleted_archive_num = -1;
  la_Info.is_role_changed = false;
  la_Info.num_unflushed_insert = 0;
  la_Info.is_apply_info_updated = false;

  la_Info.max_mem_size = max_mem_size;
  /* check vsize when it started */
  if (!start_vsize)
    {
      start_vsize = la_get_mem_size ();
    }
  la_Info.start_vsize = start_vsize;
  la_Info.start_time = time (NULL);

  la_Info.last_time_archive_deleted = la_Info.start_time;

  la_Info.db_lockf_vdes = NULL_VOLDES;

  return;
}

static void
la_shutdown (BOOT_CLIENT_TYPE type)
{
  int i;

  if (type == BOOT_CLIENT_LOG_APPLIER)
    {
      er_log_debug (ARG_FILE_LINE, "log applier will be shutting down...");
    }
  else
    {
      er_log_debug (ARG_FILE_LINE, "log prefetcher will be shutting down...");
    }

  /* clean up */
  if (la_Info.arv_log.log_vdes != NULL_VOLDES)
    {
      fileio_close (la_Info.arv_log.log_vdes);
      la_Info.arv_log.log_vdes = NULL_VOLDES;
    }
  if (la_Info.act_log.log_vdes != NULL_VOLDES)
    {
      fileio_close (la_Info.act_log.log_vdes);
      la_Info.act_log.log_vdes = NULL_VOLDES;
    }
  if (la_Info.log_path_lockf_vdes != NULL_VOLDES)
    {
      fileio_close (la_Info.log_path_lockf_vdes);
      la_Info.log_path_lockf_vdes = NULL_VOLDES;
    }

  if (la_Info.db_lockf_vdes != NULL_VOLDES)
    {
      int error;
      bool clear_owner = false;

      error =
	la_unlock_dbname (&la_Info.db_lockf_vdes, la_slave_db_name,
			  clear_owner);
      if (error == NO_ERROR)
	{
	  la_Info.db_lockf_vdes = NULL_VOLDES;
	}
      else
	{
	  assert_release (false);
	}
    }

  if (la_Info.log_data != NULL)
    {
      free_and_init (la_Info.log_data);
    }

  if (la_Info.rec_type != NULL)
    {
      free_and_init (la_Info.rec_type);
    }

  if (la_Info.undo_unzip_ptr != NULL)
    {
      log_zip_free (la_Info.undo_unzip_ptr);
      la_Info.undo_unzip_ptr = NULL;
    }
  if (la_Info.redo_unzip_ptr != NULL)
    {
      log_zip_free (la_Info.redo_unzip_ptr);
      la_Info.redo_unzip_ptr = NULL;
    }

  if (la_Info.cache_pb != NULL)
    {
      if (la_Info.cache_pb->buffer_area != NULL)
	{
	  free_and_init (la_Info.cache_pb->buffer_area);
	}

      if (la_Info.cache_pb->log_buffer != NULL)
	{
	  free_and_init (la_Info.cache_pb->log_buffer);
	}

      if (la_Info.cache_pb->hash_table != NULL)
	{
	  mht_destroy (la_Info.cache_pb->hash_table);
	  la_Info.cache_pb->hash_table = NULL;
	}

      free_and_init (la_Info.cache_pb);
    }

  if (la_Info.repl_lists)
    {
      for (i = 0; i < la_Info.repl_cnt; i++)
	{
	  if (la_Info.repl_lists[i] != NULL)
	    {
	      free_and_init (la_Info.repl_lists[i]);
	    }
	}

      free_and_init (la_Info.repl_lists);
    }

  if (la_Info.act_log.hdr_page)
    {
      free_and_init (la_Info.act_log.hdr_page);
    }
}

/*
 * la_print_log_header () -
 */
void
la_print_log_header (const char *database_name, struct log_header *hdr,
		     bool verbose)
{
  time_t tloc;
  DB_DATETIME datetime;
  char timebuf[1024];

  tloc = hdr->db_creation;
  db_localdatetime (&tloc, &datetime);
  db_datetime_to_string ((char *) timebuf, 1024, &datetime);

  if (verbose)
    {
      printf ("%-30s : %s\n", "Magic", hdr->magic);
    }
  printf ("%-30s : %s\n", "DB name ", database_name);
  printf ("%-30s : %s (%ld)\n", "DB creation time ", timebuf, tloc);
  printf ("%-30s : %lld | %d\n", "EOF LSA",
	  (long long int) hdr->eof_lsa.pageid, (int) hdr->eof_lsa.offset);
  printf ("%-30s : %lld | %d\n", "Append LSA",
	  (long long int) hdr->append_lsa.pageid,
	  (int) hdr->append_lsa.offset);
  printf ("%-30s : %s\n", "HA server state",
	  css_ha_server_state_string (hdr->ha_server_state));
  if (verbose)
    {
      printf ("%-30s : %s\n", "CUBRID release", hdr->db_release);
      printf ("%-30s : %d\n", "DB iopagesize", hdr->db_iopagesize);
      printf ("%-30s : %d\n", "DB logpagesize", hdr->db_logpagesize);
      printf ("%-30s : %d\n", "Is log shutdown", hdr->is_shutdown);
      printf ("%-30s : %d\n", "Next transaction identifier", hdr->next_trid);
      printf ("%-30s : %d\n", "Number of pages", hdr->npages);
      printf ("%-30s : %d (%s)\n", "Charset", hdr->db_charset,
	      lang_charset_cubrid_name ((int) hdr->db_charset));
      printf ("%-30s : %lld\n", "Logical pageid",
	      (long long int) hdr->fpageid);
      printf ("%-30s : %lld | %d\n", "CHKPT LSA",
	      (long long int) hdr->chkpt_lsa.pageid,
	      (int) hdr->chkpt_lsa.offset);
      printf ("%-30s : %lld\n", "Next archive pageid",
	      (long long int) hdr->nxarv_pageid);
      printf ("%-30s : %lld\n", "Next archive physical pageid",
	      (long long int) hdr->nxarv_phy_pageid);
      printf ("%-30s : %d\n", "Next archive number", hdr->nxarv_num);
      printf ("%-30s : %s\n", "HA file status",
	      logwr_log_ha_filestat_to_string (hdr->ha_file_status));
    }
}

/*
 * la_print_log_arv_header () -
 */
void
la_print_log_arv_header (const char *database_name,
			 struct log_arv_header *hdr, bool verbose)
{
  time_t tloc;
  DB_DATETIME datetime;
  char timebuf[1024];

  tloc = hdr->db_creation;
  db_localdatetime (&tloc, &datetime);
  db_datetime_to_string ((char *) timebuf, 1024, &datetime);

  printf ("%-30s : %s\n", "DB name ", database_name);
  printf ("%-30s : %s (%ld)\n", "DB creation time ", timebuf, tloc);
  if (verbose)
    {
      printf ("%-30s : %d\n", "Next transaction identifier", hdr->next_trid);
      printf ("%-30s : %d\n", "Number of pages", hdr->npages);
      printf ("%-30s : %lld\n", "Logical pageid",
	      (long long int) hdr->fpageid);
    }
  printf ("%-30s : %d\n", "Archive number", hdr->arv_num);
}

/*
 * la_log_page_check() - test the transaction log
 *   return: void
 *   log_path: log path
 *   page_num: test page number
 */
int
la_log_page_check (const char *database_name, const char *log_path,
		   INT64 page_num, bool check_applied_info,
		   bool check_copied_info, bool check_replica_info,
		   bool verbose, LOG_LSA * copied_eof_lsa,
		   LOG_LSA * copied_append_lsa, LOG_LSA * applied_final_lsa)
{
  int error = NO_ERROR;
  int res;
  char *atchar;
  char active_log_path[PATH_MAX];
  char *replica_time_bound_str;

  assert (database_name != NULL);
  assert (log_path != NULL);

  atchar = strchr (database_name, '@');
  if (atchar)
    {
      *atchar = '\0';
    }

  /* init la_Info */
  la_init (log_path, 0, BOOT_CLIENT_LOG_APPLIER);

  if (check_applied_info)
    {
      LA_HA_APPLY_INFO ha_apply_info;
      char timebuf[1024];

      la_init_ha_apply_info (&ha_apply_info);

      res = la_get_ha_apply_info (log_path, database_name, &ha_apply_info);
      if ((res <= 0)
	  || (ha_apply_info.creation_time.date == 0
	      && ha_apply_info.creation_time.time == 0))
	{
	  error = res;

	  goto check_applied_info_end;
	}

      *applied_final_lsa = ha_apply_info.final_lsa;

      printf ("\n *** Applied Info. *** \n");

      if (verbose)
	{
	  db_datetime_to_string ((char *) timebuf, 1024,
				 &ha_apply_info.creation_time);
	  printf ("%-30s : %s\n", "DB creation time", timebuf);

	  printf ("%-30s : %lld | %d\n", "Last committed LSA",
		  (long long int) ha_apply_info.committed_lsa.pageid,
		  (int) ha_apply_info.committed_lsa.offset);
	  printf ("%-30s : %lld | %d\n", "Last committed replog LSA",
		  (long long int) ha_apply_info.committed_rep_lsa.pageid,
		  ha_apply_info.committed_rep_lsa.offset);
	  printf ("%-30s : %lld | %d\n", "Last append LSA",
		  (long long int) ha_apply_info.append_lsa.pageid,
		  ha_apply_info.append_lsa.offset);
	  printf ("%-30s : %lld | %d\n", "Last EOF LSA",
		  (long long int) ha_apply_info.eof_lsa.pageid,
		  ha_apply_info.eof_lsa.offset);
	  printf ("%-30s : %lld | %d\n", "Final LSA",
		  (long long int) ha_apply_info.final_lsa.pageid,
		  ha_apply_info.final_lsa.offset);
	  printf ("%-30s : %lld | %d\n", "Required LSA",
		  (long long int) ha_apply_info.required_lsa.pageid,
		  ha_apply_info.required_lsa.offset);

	  db_datetime_to_string ((char *) timebuf, 1024,
				 &ha_apply_info.log_record_time);
	  printf ("%-30s : %s\n", "Log record time", timebuf);

	  db_datetime_to_string ((char *) timebuf, 1024,
				 &ha_apply_info.log_commit_time);
	  printf ("%-30s : %s\n", "Log committed time", timebuf);

	  db_datetime_to_string ((char *) timebuf, 1024,
				 &ha_apply_info.last_access_time);
	  printf ("%-30s : %s\n", "Last access time", timebuf);
	}

      printf ("%-30s : %ld\n", "Insert count", ha_apply_info.insert_counter);
      printf ("%-30s : %ld\n", "Update count", ha_apply_info.update_counter);
      printf ("%-30s : %ld\n", "Delete count", ha_apply_info.delete_counter);
      printf ("%-30s : %ld\n", "Schema count", ha_apply_info.schema_counter);
      printf ("%-30s : %ld\n", "Commit count", ha_apply_info.commit_counter);
      printf ("%-30s : %ld\n", "Fail count", ha_apply_info.fail_counter);

      if (verbose)
	{
	  db_datetime_to_string ((char *) timebuf, 1024,
				 &ha_apply_info.start_time);
	  printf ("%-30s : %s\n", "Start time", timebuf);
	}

      if (check_replica_info)
	{
	  replica_time_bound_str
	    = prm_get_string_value (PRM_ID_HA_REPLICA_TIME_BOUND);
	  db_datetime_to_string2 ((char *) timebuf, 1024,
				  &ha_apply_info.log_record_time);

	  printf ("\n *** Replica-specific Info. *** \n");
	  if (replica_time_bound_str == NULL)
	    {
	      printf ("%-30s : %d second(s)\n", "Deliberate lag",
		      prm_get_integer_value
		      (PRM_ID_HA_REPLICA_DELAY_IN_SECS));
	    }

	  printf ("%-30s : %s\n", "Last applied log record time", timebuf);
	  if (replica_time_bound_str != NULL)
	    {
	      printf ("%-30s : %s\n", "Will apply log records up to",
		      replica_time_bound_str);
	    }
	}
    }
check_applied_info_end:
  if (error != NO_ERROR)
    {
      printf ("%s\n", db_error_string (3));
    }
  error = NO_ERROR;

  if (check_copied_info)
    {
      /* check active log file */
      memset (active_log_path, 0, PATH_MAX);
      fileio_make_log_active_name ((char *) active_log_path,
				   la_Info.log_path, database_name);
      if (!fileio_is_volume_exist ((const char *) active_log_path))
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			       ER_LOG_MOUNT_FAIL, 1, active_log_path);
	  error = ER_LOG_MOUNT_FAIL;
	  goto check_copied_info_end;
	}

      /* read copied active page */
      error = la_find_log_pagesize (&la_Info.act_log, la_Info.log_path,
				    database_name, false);
      if (error != NO_ERROR)
	{
	  goto check_copied_info_end;
	}

      *copied_eof_lsa = la_Info.act_log.log_hdr->eof_lsa;
      *copied_append_lsa = la_Info.act_log.log_hdr->append_lsa;

      printf ("\n *** Copied Active Info. *** \n");
      la_print_log_header (database_name, la_Info.act_log.log_hdr, verbose);
    }

  if (check_copied_info && (page_num > 1))
    {
      LOG_PAGE *logpage;

      /* get last deleted archive number */
      if (la_Info.last_deleted_archive_num == (-1))
	{
	  la_Info.last_deleted_archive_num = la_find_last_deleted_arv_num ();
	}

      la_Info.log_data = (char *) malloc (la_Info.act_log.db_iopagesize);
      if (la_Info.log_data == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_OUT_OF_VIRTUAL_MEMORY, 1, la_Info.act_log.db_iopagesize);
	  error = ER_OUT_OF_VIRTUAL_MEMORY;
	  goto check_copied_info_end;
	}

      logpage = (LOG_PAGE *) la_Info.log_data;

      if (LA_LOG_IS_IN_ARCHIVE (page_num))
	{
	  /* read from the archive log file */
	  printf ("\n *** Copied Archive Info. *** \n");
	  error = la_log_fetch_from_archive (page_num, (char *) logpage);
	}
      else
	{
	  /* read from the active log file */
	  error =
	    la_log_io_read (la_Info.act_log.path,
			    la_Info.act_log.log_vdes, logpage,
			    la_log_phypageid (page_num),
			    la_Info.act_log.db_logpagesize);
	}

      if (error != NO_ERROR)
	{
	  goto check_copied_info_end;
	}
      else
	{
	  LOG_RECORD_HEADER *lrec;
	  LOG_LSA lsa;

	  if (LA_LOG_IS_IN_ARCHIVE (page_num))
	    {
	      la_print_log_arv_header (database_name,
				       la_Info.arv_log.log_hdr, verbose);
	    }
	  printf ("Log page %lld (phy: %lld pageid: %lld, offset %d)\n",
		  (long long int) page_num,
		  (long long int) la_log_phypageid (page_num),
		  (long long int) logpage->hdr.logical_pageid,
		  logpage->hdr.offset);

	  if (logpage->hdr.offset < 0)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HA_GENERIC_ERROR,
		      1, "Invalid pageid");
	      error = ER_HA_GENERIC_ERROR;
	      goto check_copied_info_end;
	    }

	  lsa.pageid = logpage->hdr.logical_pageid;
	  lsa.offset = logpage->hdr.offset;

	  while (lsa.pageid == page_num)
	    {
	      lrec = LOG_GET_LOG_RECORD_HEADER (logpage, &lsa);

	      printf ("offset:%04d "
		      "(tid:%d bck p:%lld,o:%d frw p:%lld,o:%d type:%d)\n",
		      lsa.offset,
		      lrec->trid,
		      (long long int) lrec->back_lsa.pageid,
		      lrec->back_lsa.offset,
		      (long long int) lrec->forw_lsa.pageid,
		      lrec->forw_lsa.offset, lrec->type);
	      LSA_COPY (&lsa, &lrec->forw_lsa);
	    }
	}
      free_and_init (la_Info.log_data);
    }				/* check_copied_info */

check_copied_info_end:
  if (error != NO_ERROR)
    {
      printf ("%s\n", db_error_string (3));
    }

  return NO_ERROR;
}

void
la_print_delay_info (LOG_LSA working_lsa, LOG_LSA target_lsa,
		     float process_rate)
{
  INT64 delayed_page_count = 0;
  INT64 estimated_delay = 0;

  delayed_page_count = target_lsa.pageid - working_lsa.pageid;

  if (process_rate != 0.0f)
    {
      estimated_delay = (INT64) (delayed_page_count / process_rate);
    }

  printf ("%-30s : %lld\n", "Delayed log page count",
	  (long long int) delayed_page_count);

  if (process_rate == 0.0f)
    {
      printf ("%-30s : - second(s)\n", "Estimated Delay");
    }
  else
    {
      printf ("%-30s : %lld second(s)\n", "Estimated Delay", estimated_delay);
    }
}

int
lp_prefetch_log_file (const char *database_name, const char *log_path)
{
  int error = NO_ERROR;
  struct log_header final_log_hdr;
  LA_CACHE_BUFFER *log_buf = NULL;

  error = lp_init_prefetcher (database_name, log_path);
  if (error != NO_ERROR)
    {
      goto end;
    }

  do
    {
      if (la_apply_pre () == false)
	{
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	  la_applier_need_shutdown = true;
	  break;
	}

      while (!LSA_ISNULL (&lp_Info.final_lsa) && !la_applier_need_shutdown)
	{
	  error = lp_adjust_prefetcher_speed ();
	  if (error != NO_ERROR || la_applier_need_shutdown)
	    {
	      break;
	    }

	  error = lp_get_log_record (&final_log_hdr, &log_buf);
	  if (error != NO_ERROR)
	    {
	      break;
	    }

	  if (log_buf == NULL)
	    {
	      assert (la_applier_need_shutdown);
	      break;
	    }

	  error = lp_process_log_record (&final_log_hdr, log_buf);
	  if (error != NO_ERROR)
	    {
	      break;
	    }

	  if (lp_Info.need_rollback)
	    {
	      /**
               * we rollback transaction to unlock class.
               * if we don't do this, log applier will be hang when ddl is executed.
               */
	      lp_Info.need_rollback = false;
	      db_abort_transaction ();
	    }
	}
    }
  while (!la_applier_need_shutdown);

end:
  la_shutdown (BOOT_CLIENT_LOG_PREFETCHER);

#if !defined(WINDOWS)
  if (hb_Proc_shutdown == true)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HB_PROCESS_EVENT, 2,
	      "Disconnected with the cub_master and will shut itself down",
	      "");
    }
#endif /* ! WINDOWS */

  if (la_applier_shutdown_by_signal == true)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HA_LA_STOPPED_BY_SIGNAL,
	      0);
      error = ER_HA_LA_STOPPED_BY_SIGNAL;
    }

  return error;
}

static void
lp_init (const char *log_path)
{
  la_init (log_path, 0, BOOT_CLIENT_LOG_PREFETCHER);

  LSA_SET_NULL (&lp_Info.final_lsa);
  LSA_SET_NULL (&lp_Info.la_sync_lsa);
  LSA_SET_NULL (&lp_Info.next_lsa);
  lp_Info.page_distance =
    prm_get_integer_value (PRM_ID_HA_PREFETCHLOGDB_PAGE_DISTANCE);
  lp_Info.need_rollback = false;
  lp_Info.op = LP_OP_PREFETCH;
  memset (&lp_Info.last_ha_applied_info_time, 0, sizeof (struct timeval));
}

static int
lp_init_prefetcher (const char *database_name, const char *log_path)
{
  char *p = NULL;
  int retry_cnt = 0;
  int error = NO_ERROR;
  int estimated_page_distance = 0;

  la_applier_need_shutdown = false;

  /* signal processing */
#if defined(WINDOWS)
  (void) os_set_signal_handler (SIGABRT, la_shutdown_by_signal);
  (void) os_set_signal_handler (SIGINT, la_shutdown_by_signal);
  (void) os_set_signal_handler (SIGTERM, la_shutdown_by_signal);
#else /* ! WINDOWS */
  (void) os_set_signal_handler (SIGSTOP, la_shutdown_by_signal);
  (void) os_set_signal_handler (SIGTERM, la_shutdown_by_signal);
  (void) os_set_signal_handler (SIGPIPE, SIG_IGN);
#endif /* ! WINDOWS */

  if (lzo_init () != LZO_E_OK)
    {
      /* may be impossible */
      er_log_debug (ARG_FILE_LINE, "Cannot initialize LZO");
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return ER_GENERIC_ERROR;
    }

  strncpy (la_slave_db_name, database_name, DB_MAX_IDENTIFIER_LENGTH);
  p = strchr (la_slave_db_name, '@');
  if (p)
    {
      *p = '\0';
    }

  p = la_get_hostname_from_log_path (log_path);
  if (p)
    {
      strncpy (la_peer_host, p, MAXHOSTNAMELEN);
    }
  else
    {
      strncpy (la_peer_host, "unknown", MAXHOSTNAMELEN);
    }

  lp_init (log_path);

  /* init cache buffer */
  la_Info.cache_pb = la_init_cache_pb ();
  if (la_Info.cache_pb == NULL)
    {
      er_log_debug (ARG_FILE_LINE, "Cannot initialize cache page buffer");
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  /* get log header info. page size. start_page id, etc */
  error = la_find_log_pagesize (&la_Info.act_log, la_Info.log_path,
				la_slave_db_name, true);
  if (error != NO_ERROR)
    {
      er_log_debug (ARG_FILE_LINE, "Cannot find log page size");
      return error;
    }

  error =
    la_init_cache_log_buffer (la_Info.cache_pb, la_Info.cache_buffer_size,
			      SIZEOF_LA_CACHE_LOG_BUFFER (la_Info.act_log.
							  db_logpagesize));
  if (error != NO_ERROR)
    {
      er_log_debug (ARG_FILE_LINE, "Cannot initialize cache log buffer");
      return error;
    }

  /* find out the last log applied LSA */
  do
    {
      error = lp_get_ha_applied_info (true);
      if (error == NO_ERROR)
	{
	  break;
	}

      er_log_debug (ARG_FILE_LINE, "Cannot find last LSA from DB");
      if (retry_cnt++ > 1)
	{
	  return error;
	}
      else
	{
	  LA_SLEEP (10, 0);
	}
    }
  while (error != NO_ERROR);

  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HA_LA_STARTED, 4,
	  la_Info.required_lsa.pageid,
	  la_Info.required_lsa.offset,
	  la_Info.committed_lsa.pageid,
	  la_Info.committed_lsa.offset,
	  la_Info.committed_rep_lsa.pageid, la_Info.committed_rep_lsa.offset);

  return NO_ERROR;
}

static int
lp_get_log_record (struct log_header *final_log_hdr,
		   LA_CACHE_BUFFER ** log_buf)
{
  int error = NO_ERROR;
  LA_CACHE_BUFFER *tmp_log_buf = NULL;

  while (!LSA_ISNULL (&lp_Info.final_lsa) && !la_applier_need_shutdown)
    {
      /* release all page buffers */
      la_release_all_page_buffers (NULL_PAGEID);

      /* we should fetch final log page from disk not cache buffer */
      la_decache_page_buffers (lp_Info.final_lsa.pageid, LOGPAGEID_MAX);

      error = la_fetch_log_hdr (&la_Info.act_log);
      if (error != NO_ERROR)
	{
	  la_applier_need_shutdown = true;
	  return error;
	}

      memcpy (final_log_hdr, la_Info.act_log.log_hdr,
	      sizeof (struct log_header));

      if (final_log_hdr->eof_lsa.pageid < lp_Info.final_lsa.pageid)
	{
	  LA_SLEEP (0, 100);
	  continue;
	}

      /* get the target page from log */
      tmp_log_buf = la_get_page_buffer (lp_Info.final_lsa.pageid);
      if (tmp_log_buf == NULL)
	{
	  LA_SLEEP (0, 100);
	  continue;
	}

      /* check it and verify it */
      if (tmp_log_buf->logpage.hdr.logical_pageid == lp_Info.final_lsa.pageid)
	{
	  if (tmp_log_buf->logpage.hdr.offset < 0)
	    {
	      la_invalidate_page_buffer (tmp_log_buf);
	      if ((final_log_hdr->ha_file_status ==
		   LOG_HA_FILESTAT_SYNCHRONIZED)
		  && ((lp_Info.final_lsa.pageid + 1) <=
		      final_log_hdr->eof_lsa.pageid)
		  && (la_does_page_exist (lp_Info.final_lsa.pageid + 1)
		      != LA_PAGE_DOESNOT_EXIST))
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_HA_LA_INVALID_REPL_LOG_PAGEID_OFFSET, 10,
			  tmp_log_buf->logpage.hdr.logical_pageid,
			  tmp_log_buf->logpage.hdr.offset,
			  lp_Info.final_lsa.pageid,
			  lp_Info.final_lsa.offset,
			  final_log_hdr->append_lsa.pageid,
			  final_log_hdr->append_lsa.offset,
			  final_log_hdr->eof_lsa.pageid,
			  final_log_hdr->eof_lsa.offset,
			  final_log_hdr->ha_file_status,
			  la_Info.is_end_of_record);

		  /* make sure to target page does not exist */
		  if (la_does_page_exist (lp_Info.final_lsa.pageid) ==
		      LA_PAGE_DOESNOT_EXIST &&
		      lp_Info.final_lsa.pageid <
		      final_log_hdr->eof_lsa.pageid)
		    {
		      er_log_debug (ARG_FILE_LINE,
				    "skip this page (pageid=%lld/%lld/%lld)",
				    (long long int) lp_Info.final_lsa.
				    pageid,
				    (long long int) final_log_hdr->eof_lsa.
				    pageid,
				    (long long int) final_log_hdr->
				    append_lsa.pageid);
		      /* skip it */
		      lp_Info.final_lsa.pageid++;
		      lp_Info.final_lsa.offset = 0;
		      continue;
		    }
		}

	      /* wait a moment and retry it */
	      LA_SLEEP (0, 100);
	      continue;
	    }
	  else
	    {
	      *log_buf = tmp_log_buf;
	      break;
	    }
	}
      else
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_HA_LA_INVALID_REPL_LOG_PAGEID_OFFSET, 10,
		  tmp_log_buf->logpage.hdr.logical_pageid,
		  tmp_log_buf->logpage.hdr.offset,
		  lp_Info.final_lsa.pageid,
		  lp_Info.final_lsa.offset,
		  final_log_hdr->append_lsa.pageid,
		  final_log_hdr->append_lsa.offset,
		  final_log_hdr->eof_lsa.pageid,
		  final_log_hdr->eof_lsa.offset,
		  final_log_hdr->ha_file_status, la_Info.is_end_of_record);

	  la_invalidate_page_buffer (tmp_log_buf);
	  LA_SLEEP (0, 100);
	  continue;
	}
    }

  return NO_ERROR;
}

static int
lp_adjust_prefetcher_speed (void)
{
  int error = NO_ERROR;
  int pageid_diff = 0;
  int next_estimated_pageid = 0;
  static int max_prefetch_page_count = -1;

  if (max_prefetch_page_count < 0)
    {
      max_prefetch_page_count =
	prm_get_integer_value (PRM_ID_HA_PREFETCHLOGDB_MAX_PAGE_COUNT);
    }

  while (la_applier_need_shutdown == false)
    {
      if (LSA_EQ (&lp_Info.final_lsa, &la_Info.eof_lsa))
	{
	  LA_SLEEP (0, 50000);
	}

      error = lp_get_ha_applied_info (false);
      if (error != NO_ERROR)
	{
	  la_applier_need_shutdown = true;
	  return error;
	}

      if (LSA_ISNULL (&lp_Info.la_sync_lsa) == false)
	{
	  er_log_debug (ARG_FILE_LINE,
			"prefetcher is waiting that applier apply ddl. "
			"(pageid:%d, offset:%d)",
			lp_Info.la_sync_lsa.pageid,
			lp_Info.la_sync_lsa.offset);

	  if (LSA_GT (&la_Info.final_lsa, &lp_Info.la_sync_lsa))
	    {
	      LSA_SET_NULL (&lp_Info.la_sync_lsa);
	      lp_Info.op = LP_OP_PREFETCH;
	    }
	  else
	    {
	      LA_SLEEP (0, 50000);
	      continue;
	    }
	}

      pageid_diff = lp_Info.final_lsa.pageid - la_Info.final_lsa.pageid;

      if (LSA_ISNULL (&lp_Info.next_lsa) == false)
	{
	  if (LSA_GE (&lp_Info.final_lsa, &lp_Info.next_lsa))
	    {
	      LSA_SET_NULL (&lp_Info.next_lsa);
	      lp_Info.op = LP_OP_PREFETCH;
	    }

	  break;
	}
      else if (pageid_diff > max_prefetch_page_count)
	{
	  er_log_debug (ARG_FILE_LINE,
			"prefetcher is waiting for applier (limit max_prefetch_page_count). "
			"(pageid:%d, offset:%d)",
			lp_Info.final_lsa.pageid, lp_Info.final_lsa.offset);

	  LA_SLEEP (1, 0);
	  continue;
	}
      else if (pageid_diff < lp_Info.page_distance)
	{
	  next_estimated_pageid = la_Info.final_lsa.pageid
	    + lp_Info.page_distance;
	  if (next_estimated_pageid > la_Info.eof_lsa.pageid)
	    {
	      next_estimated_pageid = la_Info.eof_lsa.pageid;
	    }

	  lp_Info.op = LP_OP_IGNORE;
	  lp_Info.next_lsa.pageid = next_estimated_pageid;
	  lp_Info.next_lsa.offset = 0;
	  break;
	}
      else
	{
	  break;
	}
    }

  return error;
}

static int
lp_process_log_record (struct log_header *final_log_hdr,
		       LA_CACHE_BUFFER * log_buf)
{
  int error = NO_ERROR;
  LOG_LSA prev_final;
  LOG_PAGE *pg_ptr = NULL;
  LOG_RECORD_HEADER *lrec = NULL;
  LOG_LSA old_lsa = { -1, -1 };

  LSA_COPY (&old_lsa, &lp_Info.final_lsa);
  LSA_SET_NULL (&prev_final);
  pg_ptr = &(log_buf->logpage);
  while (lp_Info.final_lsa.pageid == log_buf->pageid
	 && !la_applier_need_shutdown)
    {
      /* adjust the offset when the offset is 0.
       * If we read final log record from the archive,
       * we don't know the exact offset of the next record,
       * In this case, we set the offset as 0, increase the pageid.
       * So, before getting the log record, check the offset and
       * adjust it
       */
      if ((lp_Info.final_lsa.offset == 0)
	  || (lp_Info.final_lsa.offset == NULL_OFFSET))
	{
	  lp_Info.final_lsa.offset = log_buf->logpage.hdr.offset;
	}

      /* check for end of log */
      if (LSA_GE (&lp_Info.final_lsa, &final_log_hdr->eof_lsa))
	{
	  la_Info.is_end_of_record = true;
	  /* it should be refetched and release later */
	  la_invalidate_page_buffer (log_buf);
	  break;
	}
      else if (LSA_GT (&lp_Info.final_lsa, &final_log_hdr->append_lsa))
	{
	  la_invalidate_page_buffer (log_buf);
	  break;
	}

      lrec = LOG_GET_LOG_RECORD_HEADER (pg_ptr, &lp_Info.final_lsa);

      if (!LSA_ISNULL (&prev_final) && !LSA_EQ (&prev_final, &lrec->back_lsa))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_LOG_PAGE_CORRUPTED, 1, lp_Info.final_lsa.pageid);
	  /* it should be refetched and release later */
	  la_invalidate_page_buffer (log_buf);
	  break;
	}

      if (LSA_EQ (&lp_Info.final_lsa, &final_log_hdr->eof_lsa)
	  && lrec->type != LOG_END_OF_LOG)
	{
	  la_invalidate_page_buffer (log_buf);
	  break;
	}

      /* process the log record */
      error = lp_prefetch_log_record (lrec, &lp_Info.final_lsa, pg_ptr);
      if (error != NO_ERROR)
	{
	  /* check connection error */
	  if (error == ER_NET_CANT_CONNECT_SERVER
	      || error == ER_OBJ_NO_CONNECT)
	    {
	      la_applier_need_shutdown = true;
	      return ER_NET_CANT_CONNECT_SERVER;
	    }
	  else if (error == ER_HA_LA_EXCEED_MAX_MEM_SIZE)
	    {
	      la_applier_need_shutdown = true;
	      return error;
	    }

	  if (error == ER_LOG_PAGE_CORRUPTED)
	    {
	      /* it should be refetched and release later */
	      la_invalidate_page_buffer (log_buf);
	    }

	  break;
	}

      if (!LSA_ISNULL (&lrec->forw_lsa)
	  && LSA_GT (&lp_Info.final_lsa, &lrec->forw_lsa))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_LOG_PAGE_CORRUPTED, 1, lp_Info.final_lsa.pageid);
	  /* it should be refetched and release later */
	  la_invalidate_page_buffer (log_buf);
	  break;
	}

      /* set the prev/next record */
      LSA_COPY (&prev_final, &lp_Info.final_lsa);
      LSA_COPY (&lp_Info.final_lsa, &lrec->forw_lsa);
      if (lp_Info.op == LP_OP_SYNC)
	{
	  LSA_COPY (&lp_Info.la_sync_lsa, &lp_Info.final_lsa);
	  break;
	}
    }

  return error;
}

static int
lp_prefetch_log_record (LOG_RECORD_HEADER * lrec, LOG_LSA * final,
			LOG_PAGE * pg_ptr)
{
  int error = NO_ERROR;
  LA_ITEM *item = NULL;
  char buffer[256];

  if (lrec->trid == NULL_TRANID ||
      LSA_GT (&lrec->prev_tranlsa, final) || LSA_GT (&lrec->back_lsa, final))
    {
      if (lrec->type != LOG_END_OF_LOG && lrec->type != LOG_DUMMY_FILLPAGE_FORARCHIVE)	/* for backward compatibility */
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_HA_LA_INVALID_REPL_LOG_RECORD,
		  10, final->pageid, final->offset, lrec->forw_lsa.pageid,
		  lrec->forw_lsa.offset, lrec->back_lsa.pageid,
		  lrec->back_lsa.offset, lrec->trid,
		  lrec->prev_tranlsa.pageid, lrec->prev_tranlsa.offset,
		  lrec->type);
	  return ER_LOG_PAGE_CORRUPTED;
	}
    }

  if (lrec->type == LOG_REPLICATION_DATA)
    {
      if (lp_Info.op == LP_OP_PREFETCH)
	{
	  lp_Info.need_rollback = true;

	  item = la_make_repl_item (pg_ptr, lrec->type, lrec->trid, final);
	  if (item == NULL)
	    {
	      la_applier_need_shutdown = true;
	      return ER_OUT_OF_VIRTUAL_MEMORY;
	    }

	  switch (item->item_type)
	    {
	    case RVREPL_DATA_INSERT:
	      error = lp_prefetch_insert (item);
	      break;
	    case RVREPL_DATA_UPDATE:
	    case RVREPL_DATA_DELETE:
	      error = lp_prefetch_update_or_delete (item);
	      break;
	    }

	  if (item->class_name != NULL)
	    {
	      db_private_free_and_init (NULL, item->class_name);
	      pr_clear_value (&item->key);
	    }

	  if (item->db_user != NULL)
	    {
	      db_private_free_and_init (NULL, item->db_user);
	    }

	  free_and_init (item);
	}
    }
  else if (lrec->type == LOG_REPLICATION_SCHEMA)
    {
      lp_Info.op = LP_OP_SYNC;
    }

  /*
   * if this is the final record of the archive log..
   * we have to fetch the next page. So, increase the pageid,
   * but we don't know the exact offset of the next record.
   * the offset would be adjusted after getting the next log page
   */
  if (lrec->forw_lsa.pageid == -1 ||
      lrec->type <= LOG_SMALLER_LOGREC_TYPE ||
      lrec->type >= LOG_LARGER_LOGREC_TYPE)
    {
      if (la_does_page_exist (final->pageid) == LA_PAGE_EXST_IN_ARCHIVE_LOG)
	{
	  snprintf (buffer, sizeof (buffer),
		    "process last log record in archive. LSA: %lld|%d",
		    (long long int) final->pageid, final->offset);
	  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE,
		  ER_HA_GENERIC_ERROR, 1, buffer);

	  final->pageid++;
	  final->offset = 0;
	}

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_HA_LA_INVALID_REPL_LOG_RECORD, 10,
	      final->pageid, final->offset,
	      lrec->forw_lsa.pageid, lrec->forw_lsa.offset,
	      lrec->back_lsa.pageid, lrec->back_lsa.offset,
	      lrec->trid,
	      lrec->prev_tranlsa.pageid, lrec->prev_tranlsa.offset,
	      lrec->type);

      return ER_LOG_PAGE_CORRUPTED;
    }

  return error;
}

static int
lp_prefetch_update_or_delete (LA_ITEM * item)
{
  int error = NO_ERROR;
  DB_OBJECT *class_obj = NULL;

  class_obj = db_find_class (item->class_name);
  if (class_obj == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      return error;
    }

  error = obj_prefetch_repl_update_or_delete_object (class_obj, &item->key);
  return error;
}

static int
lp_prefetch_insert (LA_ITEM * item)
{
  int error = NO_ERROR;
  unsigned int rcvindex;
  bool ovfyn = false;
  RECDES recdes;
  LOG_PAGE *pgptr = NULL;
  LOG_PAGEID old_pageid = -1;
  DB_OBJECT *class_obj = NULL;
  OID *class_oid;

  /* get the target log page */
  old_pageid = item->target_lsa.pageid;
  pgptr = la_get_page (old_pageid);
  if (pgptr == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  /* retrieve the target record description */
  error = la_get_recdes (&item->target_lsa, pgptr, &recdes, &rcvindex,
			 la_Info.log_data, la_Info.rec_type, &ovfyn);
  if (error != NO_ERROR)
    {
      la_release_page_buffer (old_pageid);
      return error;
    }

  if (recdes.type == REC_ASSIGN_ADDRESS || recdes.type == REC_RELOCATION)
    {
      er_log_debug (ARG_FILE_LINE, "prefetch_insert : rectype.type = %d\n",
		    recdes.type);
      la_release_page_buffer (old_pageid);
      return error;
    }
  if (rcvindex != RVHF_INSERT && rcvindex != RVOVF_CHANGE_LINK)
    {
      er_log_debug (ARG_FILE_LINE, "prefetch_insert : rcvindex = %d\n",
		    rcvindex);
      la_release_page_buffer (old_pageid);
      return error;
    }

  class_obj = db_find_class (item->class_name);
  if (class_obj == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      return error;
    }

  class_oid = ws_oid (class_obj);

  error = locator_prefetch_repl_insert (class_oid, &recdes);
  return error;
}

static int
lp_get_ha_applied_info (bool is_first)
{
  int error = NO_ERROR;
  int res;
  LA_ACT_LOG *act_log;
  LA_HA_APPLY_INFO apply_info;
  act_log = &la_Info.act_log;
  struct timeval curtime;
  int diff_msec;

  gettimeofday (&curtime, NULL);
  diff_msec =
    (curtime.tv_sec -
     lp_Info.last_ha_applied_info_time.tv_sec) * 1000 +
    (curtime.tv_usec / 1000 -
     lp_Info.last_ha_applied_info_time.tv_usec / 1000);

  if (lp_Info.last_ha_applied_info_time.tv_sec != 0
      && diff_msec > 0 && diff_msec < LP_HA_APPLIED_INFO_REFRESH_TIME)
    {
      return NO_ERROR;
    }

  res =
    la_get_ha_apply_info (la_Info.log_path, act_log->log_hdr->prefix_name,
			  &apply_info);
  if (res <= 0)
    {
      er_stack_push ();
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_HA_GENERIC_ERROR, 1, "failed to get db_ha_apply_info");
      er_stack_pop ();

      return ER_FAILED;

    }

  LSA_COPY (&la_Info.committed_lsa, &apply_info.committed_lsa);
  LSA_COPY (&la_Info.committed_rep_lsa, &apply_info.committed_rep_lsa);
  LSA_COPY (&la_Info.append_lsa, &apply_info.append_lsa);
  LSA_COPY (&la_Info.eof_lsa, &apply_info.eof_lsa);
  LSA_COPY (&la_Info.final_lsa, &apply_info.final_lsa);
  if (is_first == true)
    {
      LSA_COPY (&lp_Info.final_lsa, &apply_info.final_lsa);
      lp_Info.final_lsa.pageid += 64;
      lp_Info.final_lsa.offset = 0;
    }
  LSA_COPY (&la_Info.required_lsa, &apply_info.required_lsa);

  la_Info.insert_counter = apply_info.insert_counter;
  la_Info.update_counter = apply_info.update_counter;
  la_Info.delete_counter = apply_info.delete_counter;
  la_Info.schema_counter = apply_info.schema_counter;
  la_Info.commit_counter = apply_info.commit_counter;
  la_Info.fail_counter = apply_info.fail_counter;

  if (LSA_ISNULL (&la_Info.required_lsa))
    {
      LSA_COPY (&la_Info.required_lsa, &act_log->log_hdr->eof_lsa);
    }

  if (LSA_ISNULL (&la_Info.committed_lsa))
    {
      LSA_COPY (&la_Info.committed_lsa, &la_Info.required_lsa);
    }

  if (LSA_ISNULL (&la_Info.committed_rep_lsa))
    {
      LSA_COPY (&la_Info.committed_rep_lsa, &la_Info.required_lsa);
    }

  if (is_first == true && LSA_ISNULL (&la_Info.final_lsa))
    {
      LSA_COPY (&la_Info.final_lsa, &la_Info.required_lsa);
    }

  LSA_COPY (&la_Info.last_committed_lsa, &la_Info.committed_lsa);
  LSA_COPY (&la_Info.last_committed_rep_lsa, &la_Info.committed_rep_lsa);

  memcpy (&lp_Info.last_ha_applied_info_time, &curtime,
	  sizeof (struct timeval));

  return error;
}

/*
 * la_remove_archive_logs() -
 *   return: int
 *
 *   db_name(in):
 *   last_deleted_arv_num(in):
 *   nxarv_num(in):
 *   max_count_arv_to_delete(in): max number of archive files to remove
 */
int
la_remove_archive_logs (const char *db_name, int last_deleted_arv_num,
			int nxarv_num, int max_arv_count_to_delete)
{
  int error = NO_ERROR;
  int log_max_archives =
    prm_get_integer_value (PRM_ID_HA_COPY_LOG_MAX_ARCHIVES);
  const char *info_reason, *catmsg;
  char archive_name[PATH_MAX] = { '\0', }, archive_name_first[PATH_MAX];
  int first_arv_num_to_delete = -1;
  int last_arv_num_to_delete = -1;
  int required_arv_num = -1;
  int max_arv_count;
  int current_arv_count;
  int i;

  if (LA_LOG_IS_IN_ARCHIVE (la_Info.required_lsa.pageid))
    {
      error = la_find_archive_num (&required_arv_num,
				   la_Info.required_lsa.pageid);
      if (error != NO_ERROR)
	{
	  return last_deleted_arv_num;
	}
      max_arv_count = MAX (log_max_archives, nxarv_num - required_arv_num);
    }
  else
    {
      max_arv_count = log_max_archives;
    }

  current_arv_count = nxarv_num - last_deleted_arv_num - 1;
  if (current_arv_count > max_arv_count)
    {
      first_arv_num_to_delete = last_deleted_arv_num + 1;
      last_arv_num_to_delete = nxarv_num - max_arv_count - 1;
      if ((last_arv_num_to_delete < 0)
	  || (last_arv_num_to_delete < first_arv_num_to_delete))
	{
	  return last_deleted_arv_num;
	}

      if (max_arv_count_to_delete <
	  last_arv_num_to_delete - first_arv_num_to_delete + 1)
	{
	  last_arv_num_to_delete =
	    first_arv_num_to_delete + max_arv_count_to_delete - 1;
	}

      for (i = first_arv_num_to_delete; i <= last_arv_num_to_delete; i++)
	{
	  fileio_make_log_archive_name (archive_name, la_Info.log_path,
					db_name, i);
	  fileio_unformat (NULL, archive_name);
	}

      info_reason = msgcat_message (MSGCAT_CATALOG_CUBRID,
				    MSGCAT_SET_LOG,
				    MSGCAT_LOG_MAX_ARCHIVES_HAS_BEEN_EXCEEDED);
      if (info_reason == NULL)
	{
	  info_reason = "Number of active log archives has been exceeded"
	    " the max desired number.";
	}
      catmsg = msgcat_message (MSGCAT_CATALOG_CUBRID,
			       MSGCAT_SET_LOG,
			       MSGCAT_LOG_LOGINFO_REMOVE_REASON);
      if (catmsg == NULL)
	{
	  catmsg = "REMOVE: %d %s to \n%d %s.\nREASON: %s\n";
	}
      if (first_arv_num_to_delete == last_arv_num_to_delete)
	{
	  log_dump_log_info (la_Info.loginf_path, false, catmsg,
			     first_arv_num_to_delete, archive_name,
			     last_arv_num_to_delete, archive_name,
			     info_reason);
	}
      else
	{
	  fileio_make_log_archive_name (archive_name_first,
					la_Info.log_path, db_name,
					first_arv_num_to_delete);
	  log_dump_log_info (la_Info.loginf_path, false, catmsg,
			     first_arv_num_to_delete, archive_name_first,
			     last_arv_num_to_delete, archive_name,
			     info_reason);
	}
      return last_arv_num_to_delete;
    }

  return last_deleted_arv_num;
}

static int
la_find_last_deleted_arv_num (void)
{
  int arv_log_num;
  char arv_log_path[PATH_MAX];
  int arv_log_vdes = NULL_VOLDES;

  arv_log_num = la_Info.act_log.log_hdr->nxarv_num - 1;
  while (arv_log_num >= 0)
    {
      /* make archive_name */
      fileio_make_log_archive_name (arv_log_path,
				    la_Info.log_path,
				    la_Info.act_log.log_hdr->prefix_name,
				    arv_log_num);

      /* open the archive file */
      arv_log_vdes = fileio_open (arv_log_path, O_RDONLY, 0);
      if (arv_log_vdes == NULL_VOLDES)
	{
	  break;
	}

      fileio_close (arv_log_vdes);
      arv_log_num--;
    }

  return arv_log_num;
}

static float
la_get_avg (int *array, int size)
{
  int i, total = 0;

  assert (size > 0);

  for (i = 0; i < size; i++)
    {
      total += array[i];
    }

  return (float) total / size;
}

/*
 * la_get_adaptive_time_commit_interval () - adjust commit interval
 *                                      based on the replication delay
 *   time_commit_interval(in/out): commit interval
 *   delay_hist(in): delay history
 *   return: none
 */
static void
la_get_adaptive_time_commit_interval (int *time_commit_interval,
				      int *delay_hist)
{
  int delay;
  int max_commit_interval;
  float avg_delay;
  static int delay_hist_idx = 0;

  if (la_Info.log_record_time != 0)
    {
      delay = time (NULL) - la_Info.log_record_time;
    }
  else
    {
      return;
    }

  max_commit_interval =
    prm_get_integer_value (PRM_ID_HA_APPLYLOGDB_MAX_COMMIT_INTERVAL_IN_MSECS);

  if (delay > LA_MAX_TOLERABLE_DELAY)
    {
      *time_commit_interval = max_commit_interval;
    }
  else if (delay == 0)
    {
      *time_commit_interval /= 2;
    }
  /* check if delay history is filled up */
  else if (delay_hist[delay_hist_idx] >= 0)
    {
      avg_delay = la_get_avg (delay_hist, LA_NUM_DELAY_HISTORY);

      if (delay < avg_delay)
	{
	  *time_commit_interval /= 2;
	}
      else if (delay > avg_delay)
	{
	  *time_commit_interval *= 2;

	  if (*time_commit_interval == 0)
	    {
	      *time_commit_interval = LA_REINIT_COMMIT_INTERVAL;
	    }
	  else if (*time_commit_interval > max_commit_interval)
	    {
	      *time_commit_interval = max_commit_interval;
	    }
	}
    }

  delay_hist[delay_hist_idx++] = delay;
  delay_hist_idx %= LA_NUM_DELAY_HISTORY;

  return;
}

/*
 * la_apply_log_file() - apply the transaction log to the slave
 *   return: int
 *   database_name: apply database
 *   log_path: log volume path for apply
 *   max_mem_size: maximum memory size
 *
 * Note:
 *      The main routine.
 *         1. Initialize
 *            . signal process
 *            . get the log file name & IO page size
 *         2. body (loop) - process the request
 *            . catch the request
 *            . if shutdown request --> process
 */
int
la_apply_log_file (const char *database_name, const char *log_path,
		   const int max_mem_size)
{
  int error = NO_ERROR;
  struct log_header final_log_hdr;
  LA_CACHE_BUFFER *log_buf = NULL;
  LOG_PAGE *pg_ptr;
  LOG_RECORD_HEADER *lrec = NULL;
  LOG_LSA old_lsa = { -1, -1 };
  LOG_LSA prev_final;
  struct timeval time_commit;
  char *s;
  int last_nxarv_num = 0;
  bool clear_owner;
  int now = 0, last_eof_time = 0;
  LOG_LSA last_eof_lsa;
  int time_commit_interval;
  int delay_hist[LA_NUM_DELAY_HISTORY];
  int i;
  int remove_arv_interval_in_secs;
  int max_arv_count_to_delete = 0;

  assert (database_name != NULL);
  assert (log_path != NULL);

  la_applier_need_shutdown = false;

  /* signal processing */
#if defined(WINDOWS)
  (void) os_set_signal_handler (SIGABRT, la_shutdown_by_signal);
  (void) os_set_signal_handler (SIGINT, la_shutdown_by_signal);
  (void) os_set_signal_handler (SIGTERM, la_shutdown_by_signal);
#else /* ! WINDOWS */
  (void) os_set_signal_handler (SIGSTOP, la_shutdown_by_signal);
  (void) os_set_signal_handler (SIGTERM, la_shutdown_by_signal);
  (void) os_set_signal_handler (SIGPIPE, SIG_IGN);
#endif /* ! WINDOWS */

  if (lzo_init () != LZO_E_OK)
    {
      /* may be impossible */
      er_log_debug (ARG_FILE_LINE, "Cannot initialize LZO");
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return ER_GENERIC_ERROR;
    }

  strncpy (la_slave_db_name, database_name, DB_MAX_IDENTIFIER_LENGTH);
  s = strchr (la_slave_db_name, '@');
  if (s)
    {
      *s = '\0';
    }

  s = la_get_hostname_from_log_path (log_path);
  if (s)
    {
      strncpy (la_peer_host, s, MAXHOSTNAMELEN);
    }
  else
    {
      strncpy (la_peer_host, "unknown", MAXHOSTNAMELEN);
    }

  /* init la_Info */
  la_init (log_path, max_mem_size, BOOT_CLIENT_LOG_APPLIER);

  if (prm_get_bool_value (PRM_ID_HA_SQL_LOGGING))
    {
      if (sl_init (la_slave_db_name, log_path) != NO_ERROR)
	{
	  er_stack_push ();
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HA_GENERIC_ERROR,
		  1, "Failed to initialize SQL logger");
	  er_stack_pop ();
	}
      else
	{
	  la_enable_sql_logging = true;
	}
    }

  la_use_server_side_update_repl =
    prm_get_bool_value (PRM_ID_HA_REPL_ENABLE_SERVER_SIDE_UPDATE);

  error = la_check_duplicated (la_Info.log_path, la_slave_db_name,
			       &la_Info.log_path_lockf_vdes,
			       &la_Info.last_deleted_archive_num);
  if (error != NO_ERROR)
    {
      return error;
    }

  /* init cache buffer */
  la_Info.cache_pb = la_init_cache_pb ();
  if (la_Info.cache_pb == NULL)
    {
      er_log_debug (ARG_FILE_LINE, "Cannot initialize cache page buffer");
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  /* get log header info. page size. start_page id, etc */
  error = la_find_log_pagesize (&la_Info.act_log, la_Info.log_path,
				la_slave_db_name, true);
  if (error != NO_ERROR)
    {
      er_log_debug (ARG_FILE_LINE, "Cannot find log page size");
      return error;
    }

  error =
    la_init_cache_log_buffer (la_Info.cache_pb, la_Info.cache_buffer_size,
			      SIZEOF_LA_CACHE_LOG_BUFFER (la_Info.act_log.
							  db_logpagesize));
  if (error != NO_ERROR)
    {
      er_log_debug (ARG_FILE_LINE, "Cannot initialize cache log buffer");
      return error;
    }

  /* get log info path */
  fileio_make_log_info_name (la_Info.loginf_path, la_Info.log_path,
			     la_slave_db_name);

  /* get last deleted archive number */
  if (la_Info.last_deleted_archive_num == (-1))
    {
      la_Info.last_deleted_archive_num = la_find_last_deleted_arv_num ();
    }

  remove_arv_interval_in_secs =
    prm_get_integer_value (PRM_ID_REMOVE_LOG_ARCHIVES_INTERVAL);

  /* find out the last log applied LSA */
  error = la_get_last_ha_applied_info ();
  if (error != NO_ERROR)
    {
      er_stack_push ();
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HA_GENERIC_ERROR,
	      1, "Failed to initialize db_ha_apply_info");
      er_stack_pop ();
      return error;
    }

  for (i = 0; i < LA_NUM_DELAY_HISTORY; i++)
    {
      delay_hist[i] = -1;
    }
  time_commit_interval =
    prm_get_integer_value (PRM_ID_HA_APPLYLOGDB_MAX_COMMIT_INTERVAL_IN_MSECS);

  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HA_LA_STARTED, 6,
	  la_Info.required_lsa.pageid,
	  la_Info.required_lsa.offset,
	  la_Info.committed_lsa.pageid,
	  la_Info.committed_lsa.offset,
	  la_Info.committed_rep_lsa.pageid, la_Info.committed_rep_lsa.offset);

  /* initialize final_lsa */
  LSA_COPY (&la_Info.committed_lsa, &la_Info.required_lsa);

  gettimeofday (&time_commit, NULL);
  last_eof_time = time (NULL);
  LSA_SET_NULL (&last_eof_lsa);

  /* start the main loop */
  do
    {
      int retry_count = 0;

      /* get next LSA to be processed */
      if (la_apply_pre () == false)
	{
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	  la_applier_need_shutdown = true;
	  break;
	}

      /* start loop for apply */
      while (!LSA_ISNULL (&la_Info.final_lsa)
	     && la_applier_need_shutdown == false)
	{
	  /* release all page buffers */
	  la_release_all_page_buffers (NULL_PAGEID);

	  /* we should fetch final log page from disk not cache buffer */
	  la_decache_page_buffers (la_Info.final_lsa.pageid, LOGPAGEID_MAX);

	  error = la_fetch_log_hdr (&la_Info.act_log);
	  if (error != NO_ERROR)
	    {
	      la_applier_need_shutdown = true;
	      break;
	    }

	  if (last_nxarv_num == 0)
	    {
	      last_nxarv_num = la_Info.act_log.log_hdr->nxarv_num;
	    }

	  if (remove_arv_interval_in_secs == 0)
	    {
	      if (last_nxarv_num != la_Info.act_log.log_hdr->nxarv_num)
		{
		  max_arv_count_to_delete = INT_MAX;
		}
	    }
	  else if (time (NULL) - la_Info.last_time_archive_deleted >
		   remove_arv_interval_in_secs)
	    {
	      max_arv_count_to_delete = 1;
	    }

	  if (max_arv_count_to_delete > 0)
	    {
	      la_Info.last_deleted_archive_num =
		la_remove_archive_logs (la_slave_db_name,
					la_Info.last_deleted_archive_num,
					la_Info.act_log.log_hdr->nxarv_num,
					max_arv_count_to_delete);
	      if (la_Info.last_deleted_archive_num >= 0)
		{
		  (void) la_update_last_deleted_arv_num (la_Info.
							 log_path_lockf_vdes,
							 la_Info.
							 last_deleted_archive_num);
		}
	      last_nxarv_num = la_Info.act_log.log_hdr->nxarv_num;

	      la_Info.last_time_archive_deleted = time (NULL);
	      max_arv_count_to_delete = 0;
	    }

	  memcpy (&final_log_hdr, la_Info.act_log.log_hdr,
		  sizeof (struct log_header));

	  if (prm_get_integer_value
	      (PRM_ID_HA_APPLYLOGDB_LOG_WAIT_TIME_IN_SECS) >= 0)
	    {
	      if (final_log_hdr.ha_server_state == HA_SERVER_STATE_DEAD
		  && LSA_EQ (&last_eof_lsa, &final_log_hdr.eof_lsa))
		{
		  now = time (NULL);
		  assert_release (now >= last_eof_time);

		  if ((now - last_eof_time) >=
		      prm_get_integer_value
		      (PRM_ID_HA_APPLYLOGDB_LOG_WAIT_TIME_IN_SECS))
		    {
		      clear_owner = true;
		      error =
			la_unlock_dbname (&la_Info.db_lockf_vdes,
					  la_slave_db_name, clear_owner);
		      assert_release (error == NO_ERROR);
		    }
		}
	      else
		{
		  last_eof_time = time (NULL);
		}

	      LSA_COPY (&last_eof_lsa, &final_log_hdr.eof_lsa);
	    }

	  /* check log hdr's master state */
	  if (la_Info.apply_state == HA_LOG_APPLIER_STATE_DONE
	      && (final_log_hdr.ha_server_state !=
		  HA_SERVER_STATE_ACTIVE)
	      && (final_log_hdr.ha_server_state !=
		  HA_SERVER_STATE_TO_BE_STANDBY))
	    {
	      /* if there's no replication log to be applied,
	       * we should release dbname lock */
	      clear_owner = true;
	      error =
		la_unlock_dbname (&la_Info.db_lockf_vdes, la_slave_db_name,
				  clear_owner);
	      assert_release (error == NO_ERROR);

	      if (final_log_hdr.ha_server_state != HA_SERVER_STATE_DEAD)
		{
		  LSA_COPY (&la_Info.committed_lsa, &la_Info.final_lsa);
		}

	      if (LSA_GE (&la_Info.final_lsa, &la_Info.committed_lsa))
		{
		  er_log_debug (ARG_FILE_LINE,
				"lowest required page id is %lld",
				(long long int) la_Info.committed_lsa.pageid);

		  error = la_log_commit (false);
		  if (error == ER_NET_CANT_CONNECT_SERVER
		      || error == ER_OBJ_NO_CONNECT)
		    {
		      er_log_debug (ARG_FILE_LINE,
				    "we lost connection with DB server.");
		      la_applier_need_shutdown = true;
		      break;
		    }
		  else if (error == ER_LC_PARTIALLY_FAILED_TO_FLUSH
			   && la_applier_need_shutdown == true)
		    {
		      break;
		    }
		}

	      la_Info.apply_state = HA_LOG_APPLIER_STATE_RECOVERING;

	      LA_SLEEP (1, 0);
	      continue;
	    }

	  if (final_log_hdr.eof_lsa.pageid < la_Info.final_lsa.pageid)
	    {
	      usleep (100 * 1000);
	      continue;
	    }

	  /* get the target page from log */
	  log_buf = la_get_page_buffer (la_Info.final_lsa.pageid);
	  LSA_COPY (&old_lsa, &la_Info.final_lsa);

	  if (log_buf == NULL)
	    {
	      if (la_applier_need_shutdown == true)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_LOG_PAGE_CORRUPTED, 1, la_Info.final_lsa.pageid);
		  error = ER_LOG_PAGE_CORRUPTED;
		  break;
		}

	      /* it can be happend when log file is not synced yet */
	      if (final_log_hdr.ha_file_status !=
		  LOG_HA_FILESTAT_SYNCHRONIZED)
		{
		  er_log_debug (ARG_FILE_LINE,
				"requested pageid (%lld) is not yet exist",
				(long long int) la_Info.final_lsa.pageid);
		  usleep (300 * 1000);
		  continue;
		}
	      /* request page is greater then append_lsa.(in log_header) */
	      else if (final_log_hdr.append_lsa.pageid <
		       la_Info.final_lsa.pageid)
		{
		  er_log_debug (ARG_FILE_LINE,
				"requested pageid (%lld) is greater than "
				"append_las.pageid (%lld) in log header",
				(long long int) la_Info.final_lsa.pageid,
				(long long int) final_log_hdr.append_lsa.
				pageid);
		  usleep (100 * 1000);
		  continue;
		}

	      er_log_debug (ARG_FILE_LINE,
			    "requested pageid (%lld) may be corrupted ",
			    (long long int) la_Info.final_lsa.pageid);

	      if (retry_count++ < LA_GET_PAGE_RETRY_COUNT)
		{
		  er_log_debug (ARG_FILE_LINE, "but retry again...",
				la_Info.final_lsa.pageid);
		  usleep (300 * 1000 + (retry_count * 100));
		  continue;
		}

	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_LOG_PAGE_CORRUPTED, 1, la_Info.final_lsa.pageid);
	      error = ER_LOG_PAGE_CORRUPTED;
	      la_applier_need_shutdown = true;
	      break;
	    }
	  else
	    {
	      retry_count = 0;
	    }

	  /* check it and verify it */
	  if (log_buf->logpage.hdr.logical_pageid == la_Info.final_lsa.pageid)
	    {
	      if (log_buf->logpage.hdr.offset < 0)
		{
		  la_invalidate_page_buffer (log_buf);
		  if ((final_log_hdr.ha_file_status ==
		       LOG_HA_FILESTAT_SYNCHRONIZED)
		      && ((la_Info.final_lsa.pageid + 1) <=
			  final_log_hdr.eof_lsa.pageid)
		      && (la_does_page_exist (la_Info.final_lsa.pageid + 1)
			  != LA_PAGE_DOESNOT_EXIST))
		    {
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			      ER_HA_LA_INVALID_REPL_LOG_PAGEID_OFFSET, 10,
			      log_buf->logpage.hdr.logical_pageid,
			      log_buf->logpage.hdr.offset,
			      la_Info.final_lsa.pageid,
			      la_Info.final_lsa.offset,
			      final_log_hdr.append_lsa.pageid,
			      final_log_hdr.append_lsa.offset,
			      final_log_hdr.eof_lsa.pageid,
			      final_log_hdr.eof_lsa.offset,
			      final_log_hdr.ha_file_status,
			      la_Info.is_end_of_record);

		      /* make sure to target page does not exist */
		      if (la_does_page_exist (la_Info.final_lsa.pageid) ==
			  LA_PAGE_DOESNOT_EXIST &&
			  la_Info.final_lsa.pageid <
			  final_log_hdr.eof_lsa.pageid)
			{
			  er_log_debug (ARG_FILE_LINE,
					"skip this page (pageid=%lld/%lld/%lld)",
					(long long int) la_Info.final_lsa.
					pageid,
					(long long int) final_log_hdr.eof_lsa.
					pageid,
					(long long int) final_log_hdr.
					append_lsa.pageid);
			  /* skip it */
			  la_Info.final_lsa.pageid++;
			  la_Info.final_lsa.offset = 0;
			  continue;
			}
		    }

#if defined (LA_VERBOSE_DEBUG)
		  er_log_debug (ARG_FILE_LINE,
				"refetch this page... (pageid=%lld/%lld/%lld)",
				(long long int) la_Info.final_lsa.pageid,
				(long long int) final_log_hdr.eof_lsa.pageid,
				(long long int) final_log_hdr.append_lsa.
				pageid);
#endif
		  /* wait a moment and retry it */
		  usleep (100 * 1000);
		  continue;
		}
	      else
		{
		  /* we get valid page */
		}
	    }
	  else
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_HA_LA_INVALID_REPL_LOG_PAGEID_OFFSET, 10,
		      log_buf->logpage.hdr.logical_pageid,
		      log_buf->logpage.hdr.offset,
		      la_Info.final_lsa.pageid, la_Info.final_lsa.offset,
		      final_log_hdr.append_lsa.pageid,
		      final_log_hdr.append_lsa.offset,
		      final_log_hdr.eof_lsa.pageid,
		      final_log_hdr.eof_lsa.offset,
		      final_log_hdr.ha_file_status, la_Info.is_end_of_record);

	      la_invalidate_page_buffer (log_buf);
	      /* TODO: continue? error ? just sleep and continue? */
	      usleep (100 * 1000);

	      continue;
	    }

	  /* apply it */
	  LSA_SET_NULL (&prev_final);
	  pg_ptr = &(log_buf->logpage);

	  while (la_Info.final_lsa.pageid == log_buf->pageid
		 && la_applier_need_shutdown == false)
	    {
	      /* adjust the offset when the offset is 0.
	       * If we read final log record from the archive,
	       * we don't know the exact offset of the next record,
	       * In this case, we set the offset as 0, increase the pageid.
	       * So, before getting the log record, check the offset and
	       * adjust it
	       */
	      if ((la_Info.final_lsa.offset == 0)
		  || (la_Info.final_lsa.offset == NULL_OFFSET))
		{
		  la_Info.final_lsa.offset = log_buf->logpage.hdr.offset;
		}

	      /* check for end of log */
	      if (LSA_GT (&la_Info.final_lsa, &final_log_hdr.eof_lsa))
		{
#if defined (LA_VERBOSE_DEBUG)
		  er_log_debug (ARG_FILE_LINE,
				"this page is grater than eof_lsa. (%lld|%d) > "
				"eof (%lld|%d). appended (%lld|%d)",
				(long long int) la_Info.final_lsa.pageid,
				la_Info.final_lsa.offset,
				(long long int) final_log_hdr.eof_lsa.pageid,
				final_log_hdr.eof_lsa.offset,
				(long long int) final_log_hdr.append_lsa.
				pageid, final_log_hdr.append_lsa.offset);
#endif
		  la_Info.is_end_of_record = true;
		  /* it should be refetched and release later */
		  la_invalidate_page_buffer (log_buf);
		  break;
		}
	      else if (LSA_GT (&la_Info.final_lsa, &final_log_hdr.append_lsa))
		{
		  la_invalidate_page_buffer (log_buf);
		  break;
		}

	      lrec = LOG_GET_LOG_RECORD_HEADER (pg_ptr, &la_Info.final_lsa);

	      if (!LSA_ISNULL (&prev_final)
		  && !LSA_EQ (&prev_final, &lrec->back_lsa))
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_LOG_PAGE_CORRUPTED, 1, la_Info.final_lsa.pageid);
		  error = ER_LOG_PAGE_CORRUPTED;
		  la_shutdown (BOOT_CLIENT_LOG_APPLIER);
		  return error;
		}

	      if (LSA_EQ (&la_Info.final_lsa, &final_log_hdr.eof_lsa)
		  && lrec->type != LOG_END_OF_LOG)
		{
		  la_Info.is_end_of_record = true;
		  la_invalidate_page_buffer (log_buf);
		  break;
		}

	      /* process the log record */
	      error =
		la_log_record_process (lrec, &la_Info.final_lsa, pg_ptr);
	      if (error != NO_ERROR)
		{
		  /* check connection error */
		  if (error == ER_NET_CANT_CONNECT_SERVER
		      || error == ER_OBJ_NO_CONNECT)
		    {
		      la_shutdown (BOOT_CLIENT_LOG_APPLIER);
		      return ER_NET_CANT_CONNECT_SERVER;
		    }
		  else if (error == ER_HA_LA_EXCEED_MAX_MEM_SIZE)
		    {
		      la_shutdown (BOOT_CLIENT_LOG_APPLIER);
		      return error;
		    }
		  else if (error == ER_LC_PARTIALLY_FAILED_TO_FLUSH
			   && la_applier_need_shutdown == true)
		    {
		      la_shutdown (BOOT_CLIENT_LOG_APPLIER);
		      return error;
		    }

		  if (error == ER_LOG_PAGE_CORRUPTED)
		    {
		      if (la_applier_need_shutdown == true)
			{
			  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
				  ER_LOG_PAGE_CORRUPTED, 1,
				  la_Info.final_lsa.pageid);
			  error = ER_LOG_PAGE_CORRUPTED;
			  break;
			}
		      else
			{
			  /* it should be refetched and release later */
			  la_invalidate_page_buffer (log_buf);
			}
		    }

		  break;
		}

	      if (!LSA_ISNULL (&lrec->forw_lsa)
		  && LSA_GT (&la_Info.final_lsa, &lrec->forw_lsa))
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_LOG_PAGE_CORRUPTED, 1, la_Info.final_lsa.pageid);
		  error = ER_LOG_PAGE_CORRUPTED;
		  la_shutdown (BOOT_CLIENT_LOG_APPLIER);
		  return error;
		}

	      /* set the prev/next record */
	      LSA_COPY (&prev_final, &la_Info.final_lsa);
	      LSA_COPY (&la_Info.final_lsa, &lrec->forw_lsa);
	    }

	  /* commit */
	  la_get_adaptive_time_commit_interval (&time_commit_interval,
						delay_hist);

	  error = la_check_time_commit (&time_commit, time_commit_interval);
	  if (error != NO_ERROR)
	    {
	      /* check connection error */
	      if (error == ER_NET_CANT_CONNECT_SERVER ||
		  error == ER_OBJ_NO_CONNECT)
		{
		  er_log_debug (ARG_FILE_LINE,
				"we have lost connection with DB server.");
		  la_applier_need_shutdown = true;
		  break;
		}
	      else if (error == ER_LC_PARTIALLY_FAILED_TO_FLUSH
		       && la_applier_need_shutdown == true)
		{
		  break;
		}
	    }

	  error = la_check_mem_size ();
	  if (error == ER_HA_LA_EXCEED_MAX_MEM_SIZE)
	    {
	      la_applier_need_shutdown = true;
	      break;
	    }

	  /* check and change state */
	  error = la_change_state ();
	  if (error != NO_ERROR)
	    {
	      la_applier_need_shutdown = true;
	      break;
	    }

	  if (la_Info.final_lsa.pageid >= final_log_hdr.eof_lsa.pageid
	      || la_Info.final_lsa.pageid >= final_log_hdr.append_lsa.pageid
	      || la_Info.is_end_of_record == true)
	    {
	      /* it should be refetched and release */
	      la_invalidate_page_buffer (log_buf);
	    }

	  if (la_Info.is_role_changed == true)
	    {
	      clear_owner = true;
	      error =
		la_unlock_dbname (&la_Info.db_lockf_vdes, la_slave_db_name,
				  clear_owner);
	      assert_release (error == NO_ERROR);
	    }

	  /* there is no something new */
	  if (LSA_EQ (&old_lsa, &la_Info.final_lsa))
	    {
	      usleep (100 * 1000);
	      continue;
	    }
	}			/* while (!LSA_ISNULL (&la_Info.final_lsa)
				 * && la_applier_need_shutdown == false)   */
    }
  while (la_applier_need_shutdown == false);

  la_shutdown (BOOT_CLIENT_LOG_APPLIER);

#if !defined(WINDOWS)
  if (hb_Proc_shutdown == true)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HB_PROCESS_EVENT, 2,
	      "Disconnected with the cub_master and will shut itself down",
	      "");
    }
#endif /* ! WINDOWS */

  if (la_applier_shutdown_by_signal == true)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HA_LA_STOPPED_BY_SIGNAL,
	      0);
      error = ER_HA_LA_STOPPED_BY_SIGNAL;
    }

  return error;
}

/*
 * la_delay_replica () -
 */
static void
la_delay_replica (time_t eot_time)
{
  static int ha_mode = -1;
  static int replica_delay = -1;
  static time_t replica_time_bound = -1;
  static char *replica_time_bound_str = (void *) -1;
  char buffer[LINE_MAX];

  if (ha_mode < HA_MODE_OFF)
    {
      ha_mode = prm_get_integer_value (PRM_ID_HA_MODE);
    }

  if (replica_delay < 0)
    {
      replica_delay = prm_get_integer_value (PRM_ID_HA_REPLICA_DELAY_IN_SECS);
    }

  if (replica_time_bound_str == (void *) -1)
    {
      replica_time_bound_str =
	prm_get_string_value (PRM_ID_HA_REPLICA_TIME_BOUND);
    }

  if (ha_mode == HA_MODE_REPLICA)
    {
      if (replica_time_bound_str != NULL)
	{
	  if (replica_time_bound == -1)
	    {
	      replica_time_bound =
		util_str_to_time_since_epoch (replica_time_bound_str);
	      assert (replica_time_bound != 0);
	    }

	  if (eot_time >= replica_time_bound)
	    {
	      la_log_commit (true);

	      snprintf (buffer, sizeof (buffer),
			"applylogdb paused since it reached "
			"a log record committed on master at %s or later.\n"
			"Adjust or remove %s and restart applylogdb to resume",
			replica_time_bound_str,
			prm_get_name (PRM_ID_HA_REPLICA_TIME_BOUND));
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HA_GENERIC_ERROR,
		      1, buffer);

	      /* applylogdb waits indefinitely */
	      select (0, NULL, NULL, NULL, NULL);
	    }
	}
      else if (replica_delay > 0)
	{
	  while ((time (NULL) - eot_time) < replica_delay)
	    {
	      LA_SLEEP (0, 100 * 1000);
	    }
	}
    }
}
