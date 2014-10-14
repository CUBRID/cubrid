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
 * vacuum.h - Vacuuming system (at Server).
 *
 */
#ifndef _VACUUM_H_
#define _VACUUM_H_

#include "dbtype.h"
#include "thread.h"
#include "storage_common.h"
#include "recovery.h"
#include "system_parameter.h"
#include "log_impl.h"

/* Vacuum logging function (can only be used when SERVER_MODE is defined).
 */
#define VACUUM_ER_LOG_NONE		0	/* No logging */
#define VACUUM_ER_LOG_ERROR		1	/* Log vacuum errors */
#define VACUUM_ER_LOG_WARNING		2	/* Log vacuum warnings */
#define VACUUM_ER_LOG_LOGGING		4	/* Log adding MVCC op log
						 * entries.
						 */
#define VACUUM_ER_LOG_BTREE		8	/* Log vacuum b-trees */
#define VACUUM_ER_LOG_HEAP		16	/* Log vacuum heap */
#define VACUUM_ER_LOG_DROPPED_FILES	32	/* Log dropped classes */
#define VACUUM_ER_LOG_VACUUM_DATA	64	/* Log vacuum data */
#define VACUUM_ER_LOG_WORKER		128	/* Log vacuum worker specific
						 * activity.
						 */
#define VACUUM_ER_LOG_MASTER		256	/* Log vacuum master specific
						 * activity.
						 */
#define VACUUM_ER_LOG_RECOVERY		512	/* Log recovery of vacuum data
						 * and dropped classes/indexes
						 */
#define VACUUM_ER_LOG_TOPOPS		1024	/* Log starting/ending system
						 * operations and their
						 * recovery.
						 */

#define VACUUM_ER_LOG_VERBOSE		0xFFFFFFFF	/* Log all activity
							 * related to vacuum.
							 */
#define VACUUM_IS_ER_LOG_LEVEL_SET(er_log_level) \
  (mvcc_Enabled \
   && ((prm_get_integer_value (PRM_ID_ER_LOG_VACUUM) & (er_log_level)) != 0))

#if defined(SERVER_MODE)
#define vacuum_er_log(er_log_level, ...) \
  if (VACUUM_IS_ER_LOG_LEVEL_SET (er_log_level)) \
    _er_log_debug (ARG_FILE_LINE, __VA_ARGS__)
#else
#define vacuum_er_log(er_log_level, ...)
#endif

typedef INT64 VACUUM_LOG_BLOCKID;
#define VACUUM_NULL_LOG_BLOCKID -1

extern bool vacuum_Master_is_process_log_phase;

#define VACUUM_LOG_ADD_DROPPED_FILE_POSTPONE true
#define VACUUM_LOG_ADD_DROPPED_FILE_UNDO false

#define VACUUM_MAX_WORKER_COUNT	  20

/* VACUUM_WORKER_STATE - State of vacuum workers */
typedef enum vacuum_worker_state VACUUM_WORKER_STATE;
enum vacuum_worker_state
{
  VACUUM_WORKER_STATE_INACTIVE,	/* Vacuum worker is inactive */
  VACUUM_WORKER_STATE_PROCESS_LOG,	/* Vacuum worker processes log data */
  VACUUM_WORKER_STATE_EXECUTE,	/* Vacuum worker executes cleanup based
				 * on processed data
				 */
  VACUUM_WORKER_STATE_TOPOP,	/* Vacuum worker started a system operation.
				 */
  VACUUM_WORKER_STATE_RECOVERY	/* Vacuum worker needs to be recovered. */
};

struct log_tdes;
struct log_zip;

/* VACUUM_WORKER - Vacuum worker information */
typedef struct vacuum_worker VACUUM_WORKER;
struct vacuum_worker
{
  VACUUM_WORKER_STATE state;	/* Current worker state */
  INT32 drop_files_version;	/* Last checked dropped files version */
  struct log_tdes *tdes;	/* Transaction descriptor used for system
				 * operations.
				 */

  /* Buffers that need to be persistent over vacuum jobs
   * (to avoid memory reallocation).
   */
  struct log_zip *log_zip_p;	/* Zip structure used to unzip log data */

  VPID *page_buffer;		/* Buffer to keep vacuumed pages */
  int page_buffer_capacity;	/* Capacity of vacuumed pages buffer */

  char *undo_data_buffer;	/* Buffer to save log undo data */
  int undo_data_buffer_capacity;	/* Capacity of log undo data buffer */
};

#if defined (SERVER_MODE)
/* Get vacuum worker from thread entry */
#define VACUUM_GET_VACUUM_WORKER(thread_p) \
  ((thread_p) != NULL && (thread_p)->type == TT_VACUUM_WORKER ? \
   (thread_p)->vacuum_worker : NULL)
/* Set vacuum worker to thread entry */
#define VACUUM_SET_VACUUM_WORKER(thread_p, worker) \
  ((thread_p)->vacuum_worker = worker)

/* Is thread a vacuum worker */
#define VACUUM_IS_THREAD_VACUUM_WORKER(thread_p) \
  ((thread_p) != NULL && (thread_p)->type == TT_VACUUM_WORKER)
/* Is thread a vacuum worker and undo logging can be skipped */
#define VACUUM_IS_SKIP_UNDO_ALLOWED(thread_p) \
  (VACUUM_IS_THREAD_VACUUM_WORKER (thread_p) \
  && thread_p->vacuum_worker->state != VACUUM_WORKER_STATE_TOPOP)

/* Get a vacuum worker's transaction descriptor */
#define VACUUM_GET_WORKER_TDES(thread_p) \
  ((thread_p)->vacuum_worker->tdes)
/* Get a vacuum worker's state */
#define VACUUM_GET_WORKER_STATE(thread_p) \
  ((thread_p)->vacuum_worker->state)
/* Set a vacuum worker's state */
#define VACUUM_SET_WORKER_STATE(thread_p, new_state) \
  ((thread_p)->vacuum_worker->state = new_state)

/* Used for recovery to convert current thread to a vacuum worker */
#define VACUUM_CONVERT_THREAD_TO_VACUUM_WORKER(thread_p, worker, save_type) \
  do \
    { \
      if ((thread_p) == NULL) \
	{ \
	  assert (false); \
	} \
      save_type = (thread_p)->type; \
      (thread_p)->type = TT_VACUUM_WORKER; \
      VACUUM_SET_VACUUM_WORKER (thread_p, worker); \
    } while (0)
/* Used for recovery to restore thread to previous state before setting it as
 * vacuum worker.
 */
#define VACUUM_RESTORE_THREAD(thread_p, save_type) \
  do \
    { \
      if ((thread_p) == NULL) \
        { \
	  assert (false); \
	} \
      (thread_p)->type = save_type; \
      VACUUM_SET_VACUUM_WORKER (thread_p, NULL); \
    } while (0)
#else /* SA_MODE */
/* Get SA_MODE vacuum worker */
#define VACUUM_GET_VACUUM_WORKER(thread_p) \
  (vacuum_get_worker_sa_mode ())
/* Set SA_MODE vacuum worker */
#define VACUUM_SET_VACUUM_WORKER(thread_p, worker) \
  (vacuum_set_worker_sa_mode (worker))

/* Is SA_MODE running a vacuum worker's job */
#define VACUUM_IS_THREAD_VACUUM_WORKER(thread_p) \
  (VACUUM_GET_VACUUM_WORKER (thread_p) != NULL)
/* Is SA_MODE running a vacuum worker's job and undo logging can be skipped */
#define VACUUM_IS_SKIP_UNDO_ALLOWED(thread_p) \
  (VACUUM_IS_THREAD_VACUUM_WORKER (thread_p) \
  && VACUUM_GET_WORKER_STATE (thread_p) != VACUUM_WORKER_STATE_TOPOP)

/* Get SA_MODE vacuum worker's transaction descriptor */
#define VACUUM_GET_WORKER_TDES(thread_p) \
  (VACUUM_GET_VACUUM_WORKER (thread_p)->tdes)
/* Get SA_MODE vacuum worker's state */
#define VACUUM_GET_WORKER_STATE(thread_p) \
  ((VACUUM_GET_VACUUM_WORKER (thread_p))->state)
/* Set SA_MODE vacuum worker's state */
#define VACUUM_SET_WORKER_STATE(thread_p, new_state) \
  ((VACUUM_GET_VACUUM_WORKER (thread_p))->state = new_state)

/* Set vacuum worker in SA_MODE for recovery */
#define VACUUM_CONVERT_THREAD_TO_VACUUM_WORKER(thread_p, worker, save_type) \
  (VACUUM_SET_VACUUM_WORKER (thread_p, worker))
/* Restore SA_MODE to previous state before setting vacuum worker */
#define VACUUM_RESTORE_THREAD(thread_p, save_type) \
  (VACUUM_SET_VACUUM_WORKER (thread_p, NULL))
#endif /* SA_MODE */

/* Query vacuum worker's state */
#define VACUUM_WORKER_STATE_IS_INACTIVE(thread_p) \
  (VACUUM_GET_WORKER_STATE (thread_p) == VACUUM_WORKER_STATE_INACTIVE)
#define VACUUM_WORKER_STATE_IS_PROCESS_LOG(thread_p) \
  (VACUUM_GET_WORKER_STATE (thread_p) == VACUUM_WORKER_STATE_PROCESS_LOG)
#define VACUUM_WORKER_STATE_IS_EXECUTE(thread_p) \
  (VACUUM_GET_WORKER_STATE (thread_p) == VACUUM_WORKER_STATE_EXECUTE)
#define VACUUM_WORKER_STATE_IS_TOPOP(thread_p) \
  (VACUUM_GET_WORKER_STATE (thread_p) == VACUUM_WORKER_STATE_TOPOP)
#define VACUUM_WORKER_STATE_IS_RECOVERY(thread_p) \
  (VACUUM_GET_WORKER_STATE (thread_p) == VACUUM_WORKER_STATE_RECOVERY)

/* Define VACUUM_IS_PROCESS_LOG_FOR_VACUUM: is current thread either master
 * or worker thread and are they in the state of processing log. If true,
 * locking LOG_CS may be skipped.
 */
#if defined (SERVER_MODE)
#define VACUUM_IS_PROCESS_LOG_FOR_VACUUM(thread_p) \
  (thread_p != NULL \
   && ((thread_p->type == TT_VACUUM_WORKER \
	&& VACUUM_WORKER_STATE_IS_PROCESS_LOG (thread_p)) \
       || (thread_p->type == TT_VACUUM_MASTER \
	   && vacuum_Master_is_process_log_phase)))
#else /* !SERVER_MODE */
#define VACUUM_IS_PROCESS_LOG_FOR_VACUUM(thread_p) false
#endif /* !SERVER_MODE */

#if defined (SA_MODE)
extern VACUUM_WORKER *vacuum_get_worker_sa_mode (void);
extern void vacuum_set_worker_sa_mode (VACUUM_WORKER * worker);
#endif /* SA_MODE */

extern int vacuum_create_file_for_vacuum_data (THREAD_ENTRY * thread_p,
					       int vacuum_data_npages,
					       VFID * vacuum_data_vfid);
extern int vacuum_create_file_for_dropped_files (THREAD_ENTRY * thread_p,
						 VFID * dropped_files_vfid);
extern int vacuum_load_data_from_disk (THREAD_ENTRY * thread_p);
extern int vacuum_load_dropped_files_from_disk (THREAD_ENTRY * thread_p);
extern int vacuum_initialize (THREAD_ENTRY * thread_p,
			      int vacuum_data_npages,
			      VFID * vacuum_data_vfid,
			      VFID * dropped_files_vfid);
extern void vacuum_finalize (THREAD_ENTRY * thread_p);
extern int vacuum_flush_data (THREAD_ENTRY * thread_p, LOG_LSA * flush_to_lsa,
			      LOG_LSA * prev_chkpt_lsa,
			      LOG_LSA * oldest_not_flushed_lsa,
			      bool is_vacuum_data_locked);
extern VACUUM_LOG_BLOCKID vacuum_get_log_blockid (LOG_PAGEID pageid);
extern void vacuum_produce_log_block_data (THREAD_ENTRY * thread_p,
					   LOG_LSA * start_lsa,
					   MVCCID oldest_mvccid,
					   MVCCID newest_mvccid);
extern int vacuum_consume_buffer_log_blocks (THREAD_ENTRY * thread_p,
					     bool ignore_duplicates);
extern LOG_PAGEID vacuum_data_get_first_log_pageid (THREAD_ENTRY * thread_p);
extern LOG_PAGEID vacuum_data_get_last_log_pageid (THREAD_ENTRY * thread_p);

extern int vacuum_rv_redo_vacuum_heap_page (THREAD_ENTRY * thread_p,
					    LOG_RCV * rcv);
extern int vacuum_rv_redo_remove_bigone (THREAD_ENTRY * thread_p,
					 LOG_RCV * rcv);
extern int vacuum_rv_undo_remove_bigone (THREAD_ENTRY * thread_p,
					 LOG_RCV * rcv);
extern void vacuum_rv_redo_remove_bigone_dump (FILE * fp, int length,
					       void *data);
extern void vacuum_rv_undo_remove_bigone_dump (FILE * fp, int length,
					       void *data);
extern int vacuum_rv_redo_remove_ovf_insid (THREAD_ENTRY * thread_p,
					    LOG_RCV * rcv);
extern int vacuum_rv_redo_remove_data_entries (THREAD_ENTRY * thread_p,
					       LOG_RCV * rcv);
extern int vacuum_rv_redo_append_block_data (THREAD_ENTRY * thread_p,
					     LOG_RCV * rcv);
extern int vacuum_rv_redo_update_block_data (THREAD_ENTRY * thread_p,
					     LOG_RCV * rcv);
extern int vacuum_rv_redo_save_blocks (THREAD_ENTRY * thread_p,
				       LOG_RCV * rcv);

extern void vacuum_set_vacuum_data_lsa (THREAD_ENTRY * thread_p,
					LOG_LSA * vacuum_data_lsa,
					LOG_RCVINDEX rcvindex);
extern void vacuum_get_vacuum_data_lsa (THREAD_ENTRY * thread_p,
					LOG_LSA * vacuum_data_lsa);

extern bool vacuum_is_file_dropped (THREAD_ENTRY * thread_p, VFID * vfid,
				    MVCCID mvccid);
extern int vacuum_notify_dropped_file (THREAD_ENTRY * thread_p, LOG_RCV * rcv,
				       LOG_LSA * postpone_ref_lsa);
extern int vacuum_rv_undoredo_add_dropped_file (THREAD_ENTRY * thread_p,
						LOG_RCV * rcv);
extern void vacuum_log_add_dropped_file (THREAD_ENTRY * thread_p,
					 const VFID * vfid,
					 bool postpone_or_undo);
extern int vacuum_rv_redo_cleanup_dropped_files (THREAD_ENTRY * thread_p,
						 LOG_RCV * rcv);
extern int vacuum_rv_set_next_page_dropped_files (THREAD_ENTRY * thread_p,
						  LOG_RCV * rcv);

extern int xvacuum (THREAD_ENTRY * thread_p, int num_classes,
		    OID * class_oids);

extern int vacuum_compare_dropped_files_version (INT32 version_a,
						 INT32 version_b);

extern VACUUM_WORKER *vacuum_rv_get_worker_by_trid (THREAD_ENTRY * thread_p,
						    TRANID trid);
extern void vacuum_rv_finish_worker_recovery (THREAD_ENTRY * thread_p,
					      TRANID trid);

extern bool vacuum_is_page_of_vacuum_data (VPID * vpid);

#if defined (SERVER_MODE)
extern void vacuum_master_start (void);
extern void vacuum_start_new_job (THREAD_ENTRY * thread_p);
#endif /* SERVER_MODE */

#endif /* _VACUUM_H_ */
