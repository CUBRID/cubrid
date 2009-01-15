/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

/*
 * repl_agent.h - header file for repl_agent
 */

#ifndef _REPL_AGENT_H_
#define _REPL_AGENT_H_

#ident "$Id$"

#include "config.h"

#include "log_impl.h"
#include "memory_alloc.h"
#include "repl_support.h"
#include "log_compress.h"

#define IP_ADDR_LENGTH                  50
#define DB_NAME_LENGTH                  256
#define MAX_NUM_OF_SLAVES               1
#define MAX_NUM_OF_MASTERS              1
#define SIZE_OF_TRAIL_LOG               DB_SIZEOF(MASTER_MAP)
#define MAX_PAGE_OF_TRAIL_LOG                                                \
         ((MAX_NUM_OF_SLAVES + MAX_NUM_OF_MASTERS) *                         \
          (MAX_NUM_OF_SLAVES + MAX_NUM_OF_MASTERS))

#define SIZEOF_REPL_LOG_BUFFER \
         (offsetof(REPL_LOG_BUFFER, logpage) + minfo->io_pagesize)

#define REPL_RESP_BUFFER_SIZE (minfo->io_pagesize + COMM_RESP_BUF_SIZE)

#define SIZEOF_REPL_CACHE_LOG_BUFFER(io_pagesize)                             \
         (offsetof(REPL_LOG_BUFFER, logpage)                                  \
          + (DB_SIZEOF(REPL_CACHE_LOG_BUFFER) - DB_SIZEOF(REPL_LOG_BUFFER))   \
          + (io_pagesize))

#define REPL_LIST_CNT                   50
	/* # of appply list to be allocated once */

#define REPL_LOG_BUFFER_SIZE            500
	/* the # of elements of replication page buffer */

#define REPL_CACHE_LOG_BUFFER_SIZE      500
	/* the # of elements of replication page cache buffer */

#define REPL_MAX_NUM_CONTIGUOS_BUFFERS(size) \
         ((unsigned int)(INT_MAX / (5*(size))))
	/* the max # of elements of contiguos page cache buffer */

#define REPL_LOG_ARCHIVE_PAGE_COUNT     5000
	/* the count of pages of copy log file */
#define REPL_LOG_COPY_PAGE_COUNT        2
	/* the # of pages to be copied when flush thread archives .. */
#define REPL_PAGE_CNT_PER_LOGIN         1000
	/* the # of pages to be processed in a single connection to the slave
	 */


/*
 * Currenty,
 *   we consider following things with limitation
 *       - repl_agent can cover N slave DBs, but it's not thread safe,
 *         so we have to set MAX_NUM_OF_SLAVES as 1 ..
 *         (After getting the thread safe client libary, we can use just one
 *          repl_agent for multi slaves.. maybe 2007/02 ?)
 *       - repl_agent can cover N master DBs, a thread gets the TR log
 *         from the master for each master DB.
 *         For each Master DB, there would be
 *              % 1 thread for getting TR log from the Master
 *              % 1 thread for flushing TR log to the disk
 *              % N threads for applying TR log to the N slaves
 *       - Between TR RECV thread and FLUSH thread, we use an circular buffer
 *         to share the log pages.
 */

/* The cicular log page buffer to share TR log pages between RECV thread
 * and FLUSH thread for a master DB.
 * The APPLY thread also tries to fetch the target log page from this buffer,
 * if it can't fetch the target page, it searches the cache buffer area.
 * The most cases, the APPLY thread can get the target page from the
 * log page buffer..
 */
typedef struct
{
  LOG_PAGE *log_hdr_buffer;
  struct log_header *log_hdr;
  REPL_LOG_BUFFER **log_buffer;
  int head;
  int tail;
  int log_vdes;			/* file descriptor of the copy log */
  PAGEID start_pageid;		/* start point to copy log  */
  PAGEID min_pageid;		/* min pageid in the buffer */
  PAGEID max_pageid;		/* max pageid in the buffer */
  bool need_shutdown;
  PAGEID read_pageid;
  bool read_invalid_page;
  bool on_demand;
  bool start_to_archive;
  pthread_mutex_t mutex;
  pthread_cond_t read_cond;
  pthread_cond_t write_cond;
  pthread_cond_t end_cond;
} REPL_PB;

/* Cache buffer..
 * When the speed of log record application to the slave is delayed, or
 * the target transaction is very long,  the APPLY thread can't find out
 * the target page in the log buffer area (log buffer area is mainly used by
 * the RECV thread and FLUSH thread to fetch the active log pages from the
 * master..).
 * In this case, the APPLY thread has to read the target log page from the
 * copylog file in the local disk. Cache buffer is used for thease pages.
 */
typedef struct
{
  MHT_TABLE *hash_table;	/* hash table to find out
				 * the target page directly.
				 */
  REPL_CACHE_LOG_BUFFER **log_buffer;	/* cache log buffer pool */

  int num_buffers;		/* ths # of cache log buffer */
  int clock_hand;		/* clock hand */
  REPL_CACHE_BUFFER_AREA *buffer_area;	/* contiguous area of cache
					 * log buffers
					 */
  pthread_mutex_t mutex;
} REPL_CACHE_PB;

/*
 * The end of log info for copy log flush thread.
 * repl_agent has to know which TR log it should request to the repl_server
 * when it restarts .. (after crash or normal shutdown..)
 * So, it writes the current status of TR log receiving to the copy log file.
 */
typedef struct
{
  PAGEID start_pageid;		/* the start point of replication */
  PAGEID first_pageid;		/* the first page of the active
				 * copy log
				 */
  PAGEID last_pageid;		/* the last page of the active
				 * copy log
				 */
} COPY_LOG;

/* to maintain the connection info */
typedef struct
{
  char dbname[DB_NAME_LENGTH];
  char master_IP[IP_ADDR_LENGTH];
  int portnum;
  char userid[DB_MAX_USER_LENGTH];	/* 32 */
  char passwd[DB_MAX_USER_LENGTH];	/* 32 */
  struct sockaddr_in sock_name;
  int sock_name_len;
  int client_sock;
  char *resp_buffer;
  char req_buf[COMM_REQ_BUF_SIZE];
} CONN_INFO;

/* to maintain the replication log to be applied */
typedef struct repl_item
{
  int type;
  char *class_name;
  DB_VALUE key;
  LOG_LSA lsa;
  struct repl_item *next;
} REPL_ITEM;

/* to maintain the active transactions to be applied */
typedef struct
{
  int tranid;
  LOG_LSA start_lsa;
  REPL_ITEM *repl_head;
  REPL_ITEM *repl_tail;
} REPL_APPLY;

/* REPL_COMMIT maintains the transaction commit sequence.
 * Transaction commit sequence is the same sequence of LOG_UNLOCK_COMMIT log
 * but transaction will apply LOG_COMMIT
 */
typedef struct repl_commit
{
  int type;			/* transaction state -
				 * LOG_COMMIT or LOG_UNLOCK_COMMIT
				 */
  int tranid;			/* transaction id */
  LOG_LSA log_lsa;		/* LSA of LOG_COMMIT or
				 * LSA of LOG_UNLOCK_COMMIT
				 */
  time_t master_time;		/* commit time at the server site */
  struct repl_commit *next;
} REPL_COMMIT;

typedef struct repl_group_info
{
  char *class_name;
  LOG_LSA start_lsa;
} REPL_GROUP_INFO;

/*
 * MASTER_MAP maintains the slave<->master mapping and related trail log
 * for the apply thread. repl_agent maintains the last LOG LSA applied for
 * each slave.
 * This trail logs are flushed to the log trail file.
 *
 */
typedef struct
{
  int m_id;
  int s_id;
  LOG_LSA final_lsa;
  LOG_LSA last_committed_lsa;
  REPL_APPLY **repl_lists;
  int repl_cnt;			/* the # of elements of repl_lists */
  int cur_repl;			/* the index of the current repl_lists */
  bool all_repl;		/* true if all the classes of the
				 * target master will be replicated
				 */
  int total_rows;		/* the # of rows that were replicated */

  /* parameters */
  int perf_poll_interval;	/* How frequently check the delay
				 * time of replication ?
				 */
  bool index_replication;	/* 1 if all the indices of the
				 * master shoule be replicated
				 * to the slave
				 */
  int log_apply_interval;	/* time difference between meeting
				 * the log and applying the log to
				 * the slave for the repl_agent
				 */
  bool for_recovery;		/* 1 if the slave should be
				 * replicated for substitude the
				 * master database when the master
				 * db has some problems
				 */
  int status;			/* repl_agent current status
				 * A : Active
				 * I : Idle
				 * S : Stop
				 * F : First
				 */
  int restart_interval;		/* the # of pages to be processed
				 * in a single connection to the
				 * slave
				 */
  REPL_GROUP_INFO *class_list;	/* replication group */
  int class_count;		/* replication group class count */
  int class_list_size;		/* class_list array size
				 * for hashing
				 */
  REPL_COMMIT *commit_head;	/* queue list head */
  REPL_COMMIT *commit_tail;	/* queue list tail */
} MASTER_MAP;

typedef struct
{
  int dbid;
  char *log_data;
  char *rec_type;
  LOG_ZIP *undo_unzip_ptr;
  LOG_ZIP *redo_unzip_ptr;
  CONN_INFO conn;
  MASTER_MAP masters[MAX_NUM_OF_MASTERS];	/* master ids to be
						 * replicated
						 */
  int m_cnt;			/* the # of masters */
  time_t old_time;
} SLAVE_INFO;

typedef struct master_info
{
  int dbid;
  CONN_INFO conn;
  int agentid;			/* agent id from the master */
  char copylog_path[FILE_PATH_LENGTH];	/* 256 */
  int io_pagesize;
  REPL_PB *pb;			/* log page buffer */
  REPL_CACHE_PB *cache_pb;	/* cache log page buffer pool */
  COPY_LOG copy_log;		/* copy log info */
  int copylog_size;		/* the # of pages of active
				 * copy log
				 */
  int cache_buffer_size;	/* the # of pages of the
				 * cache buffer
				 */
  int log_buffer_size;		/* the # of pages of the
				 * log buffer
				 */
  bool is_end_of_record;	/* exist end of record in current page */
} MASTER_INFO;

typedef struct repl_dump_node
{
  int tranid;
  int type;
  struct repl_dump_node *prev;
  struct repl_dump_node *next;
} REPL_DUMP_NODE;

extern pthread_mutex_t file_Mutex;	/* mutex lock to access the
					 * error log file */
extern pthread_mutex_t error_Mutex;

extern MASTER_INFO **mInfo;
extern SLAVE_INFO **sInfo;
extern FILE *err_Log_fp;
extern int repl_Slave_num;
extern int repl_Master_num;
extern int trail_File_vdes;
extern int create_Arv;
extern int retry_Connect;
extern const char *dist_Dbname;
extern const char *dist_Passwd;
extern int perf_Commit_msec;
extern int agent_status_port;


extern int repl_ag_get_next_log (SLAVE_INFO * sinfo, int s_idx, int m_idx);
extern void repl_ag_set_final_page (int dbid, PAGEID pageid);
extern int repl_alloc_copy_pb (int size, int dbid);
extern int repl_ag_get_log_header (int dbid, bool first);
extern int repl_ag_get_master_info_index (int m_id);
extern bool repl_ag_does_page_exist (PAGEID pageid, int m_idx);
extern bool repl_ag_is_in_archive (PAGEID pageid, int m_idx);
extern bool repl_ag_valid_page (LOG_PAGE * log_page, int m_idx);
extern REPL_LOG_BUFFER *repl_ag_get_page_buffer (PAGEID pageid, int m_idx);
extern void repl_ag_release_page_buffer (PAGEID pageid, int m_idx);
extern time_t repl_ag_retrieve_eot_time (LOG_PAGE * log_pgptr, LOG_LSA * lsa,
					 int idx);
extern int repl_ag_add_unlock_commit_log (int tranid, LOG_LSA * lsa, int idx);
extern int repl_ag_set_commit_log (int tranid, LOG_LSA * lsa, int idx,
				   time_t master_time);
extern int
repl_ag_log_perf_info (char *master_dbname, int tranid,
		       const time_t * master_time, const time_t * slave_time);
extern int repl_ag_apply_commit_list (LOG_LSA * lsa, int idx,
				      time_t * old_time);
extern void repl_ag_apply_abort (int idx, int tranid, time_t master_time,
				 time_t * old_time);
extern int repl_ag_apply_repl_log (int tranid, int m_idx, int *total_rows);
extern int repl_ag_set_repl_log (LOG_PAGE * log_pgptr, int log_type,
				 int tranid, LOG_LSA * lsa, int m_idx);
extern int repl_ag_append_partition_class_to_repl_group (SLAVE_INFO * sinfo,
							 int m_idx);
extern SLAVE_INFO *repl_ag_get_slave_info (int *s_idx);
extern int repl_ag_sock_init (int m_idx);
extern void repl_ag_sock_shutdown (int m_idx);
extern int repl_ag_srv_sock_init (void);
extern void repl_ag_srv_sock_shutdown (void);
extern int repl_ag_srv_wait_request (void);
extern int repl_ag_sock_reset_recv_buf (int client_sock, int size);
extern int repl_ag_sock_request_next_log_page (int m_idx, PAGEID pageid,
					       bool from_disk, int *result,
					       bool * in_archive);
extern int repl_ag_sock_request_log_hdr (int m_idx);
extern int repl_ag_sock_request_agent_info (int m_idx);
extern REPL_PB *repl_init_pb (void);
extern int repl_ag_thread_init (void);
extern void repl_ag_thread_end (void);
extern int repl_reconnect (char *dbname, char *userid, char *passwd,
			   bool first);
extern int repl_init_log_buffer (REPL_PB * pb, int lb_cnt, int lb_size);
extern int repl_init_cache_log_buffer (REPL_CACHE_PB * cache_pb,
				       int slb_cnt, int slb_size,
				       int def_buf_size);
extern void repl_ag_clear_repl_item_by_tranid (SLAVE_INFO * sinfo, int tranid,
					       int idx);
extern bool repl_ag_is_idle (SLAVE_INFO * sinfo, int idx);
extern REPL_APPLY *repl_ag_find_apply_list (SLAVE_INFO * sinfo,
					    int tranid, int idx);
extern void repl_ag_clear_repl_item (REPL_APPLY * repl_list);
extern void repl_ag_log_dump (int log_fd, int io_pagesize);

#endif /* _REPL_AGENT_H_ */
