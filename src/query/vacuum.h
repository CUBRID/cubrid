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

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs to server module
#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

#include "dbtype_def.h"
#include "disk_manager.h"
#include "log_impl.h"
#include "recovery.h"
#include "storage_common.h"
#include "system_parameter.h"
#include "thread_entry.hpp"

#include <assert.h>

/* Vacuum logging function (can only be used when SERVER_MODE is defined).
 */
#define VACUUM_ER_LOG_NONE		0	/* No logging */
#define VACUUM_ER_LOG_ERROR		1	/* Log vacuum errors */
#define VACUUM_ER_LOG_WARNING		2	/* Log vacuum warnings */
#define VACUUM_ER_LOG_LOGGING		4	/* Log adding MVCC op log entries. */
#define VACUUM_ER_LOG_BTREE		8	/* Log vacuum b-trees */
#define VACUUM_ER_LOG_HEAP		16	/* Log vacuum heap */
#define VACUUM_ER_LOG_DROPPED_FILES	32	/* Log dropped classes */
#define VACUUM_ER_LOG_VACUUM_DATA	64	/* Log vacuum data */
#define VACUUM_ER_LOG_WORKER		128	/* Log vacuum worker specific activity. */
#define VACUUM_ER_LOG_MASTER		256	/* Log vacuum master specific activity. */
#define VACUUM_ER_LOG_RECOVERY		512	/* Log recovery of vacuum data and dropped classes/indexes */
#define VACUUM_ER_LOG_TOPOPS		1024	/* Log starting/ending system operations and their recovery. */
#define VACUUM_ER_LOG_ARCHIVES		2048	/* Log when archives are removed or when vacuum fails to find archives. */
#define VACUUM_ER_LOG_JOBS		4096	/* Log job generation, interrupt, finish */
#define VACUUM_ER_LOG_FLUSH_DATA	8192	/* Log flushing vacuum data. */

#define VACUUM_ER_LOG_VERBOSE		0xFFFFFFFF	/* Log all activity related to vacuum. */
#define VACUUM_IS_ER_LOG_LEVEL_SET(er_log_level) \
  ((prm_get_integer_value (PRM_ID_ER_LOG_VACUUM) & (er_log_level)) != 0)

#define vacuum_er_log(er_log_level, msg, ...) \
  if (VACUUM_IS_ER_LOG_LEVEL_SET (er_log_level)) \
    _er_log_debug (ARG_FILE_LINE, "VACUUM " LOG_THREAD_TRAN_MSG ": " msg "\n", \
                   LOG_THREAD_TRAN_ARGS (thread_get_thread_entry_info ()), __VA_ARGS__)
#define vacuum_er_log_error(er_log_level, msg, ...) \
  if (VACUUM_IS_ER_LOG_LEVEL_SET (VACUUM_ER_LOG_ERROR | er_log_level)) \
    _er_log_debug (ARG_FILE_LINE, "VACUUM ERROR " LOG_THREAD_TRAN_MSG ": " msg "\n", \
                   LOG_THREAD_TRAN_ARGS (thread_get_thread_entry_info ()), __VA_ARGS__)
#define vacuum_er_log_warning(er_log_level, msg, ...) \
  if (VACUUM_IS_ER_LOG_LEVEL_SET (VACUUM_ER_LOG_WARNING | er_log_level)) \
    _er_log_debug (ARG_FILE_LINE, "VACUUM WARNING " LOG_THREAD_TRAN_MSG ": " msg "\n", \
                   LOG_THREAD_TRAN_ARGS (thread_get_thread_entry_info ()), __VA_ARGS__)

#define VACUUM_LOG_ADD_DROPPED_FILE_POSTPONE true
#define VACUUM_LOG_ADD_DROPPED_FILE_UNDO false

/* number of log pages in each vacuum block */
#define VACUUM_LOG_BLOCK_PAGES_DEFAULT 31

/* VACUUM_WORKER_STATE - State of vacuum workers */
enum vacuum_worker_state
{
  VACUUM_WORKER_STATE_INACTIVE,	/* Vacuum worker is inactive */
  VACUUM_WORKER_STATE_PROCESS_LOG,	/* Vacuum worker processes log data */
  VACUUM_WORKER_STATE_EXECUTE,	/* Vacuum worker executes cleanup based on processed data */
  VACUUM_WORKER_STATE_TOPOP,	/* Vacuum worker started a system operation. */
  VACUUM_WORKER_STATE_RECOVERY	/* Vacuum worker needs to be recovered. */
};
typedef enum vacuum_worker_state VACUUM_WORKER_STATE;

struct log_tdes;
struct log_zip;

/* VACUUM_HEAP_OBJECT - Required information on each object to be vacuumed. */
typedef struct vacuum_heap_object VACUUM_HEAP_OBJECT;
struct vacuum_heap_object
{
  VFID vfid;			/* File ID of heap file. */
  OID oid;			/* Object OID. */
};

enum vacuum_cache_postpone_status
{
  VACUUM_CACHE_POSTPONE_NO,
  VACUUM_CACHE_POSTPONE_YES,
  VACUUM_CACHE_POSTPONE_OVERFLOW
};
typedef enum vacuum_cache_postpone_status VACUUM_CACHE_POSTPONE_STATUS;

typedef struct vacuum_cache_postpone_entry VACUUM_CACHE_POSTPONE_ENTRY;
struct vacuum_cache_postpone_entry
{
  LOG_LSA lsa;
  char *redo_data;
};
#define VACUUM_CACHE_POSTPONE_ENTRIES_MAX_COUNT 10

/* VACUUM_WORKER - Vacuum worker information */
typedef struct vacuum_worker VACUUM_WORKER;
struct vacuum_worker
{
  VACUUM_WORKER_STATE state;	/* Current worker state */
  INT32 drop_files_version;	/* Last checked dropped files version */
  struct log_tdes *tdes;	/* Transaction descriptor used for system operations. */

  /* Buffers that need to be persistent over vacuum jobs (to avoid memory reallocation). */
  struct log_zip *log_zip_p;	/* Zip structure used to unzip log data */

  VACUUM_HEAP_OBJECT *heap_objects;	/* Heap objects collected during a vacuum job. */
  int heap_objects_capacity;	/* Capacity of heap objects buffer. */
  int n_heap_objects;		/* Number of stored heap objects. */

  char *undo_data_buffer;	/* Buffer to save log undo data */
  int undo_data_buffer_capacity;	/* Capacity of log undo data buffer */

  // page buffer private lru list
  int private_lru_index;

  /* Caches postpones to avoid reading them from log after commit top operation with postpone. Otherwise, log critical
   * section may be required which will slow the access on merged index nodes. */
  VACUUM_CACHE_POSTPONE_STATUS postpone_cache_status;
  char *postpone_redo_data_buffer;
  char *postpone_redo_data_ptr;
  VACUUM_CACHE_POSTPONE_ENTRY postpone_cached_entries[VACUUM_CACHE_POSTPONE_ENTRIES_MAX_COUNT];
  int postpone_cached_entries_count;

  char *prefetch_log_buffer;	/* buffer for prefetching log pages */
  LOG_PAGEID prefetch_first_pageid;	/* first prefetched log pageid */
  LOG_PAGEID prefetch_last_pageid;	/* last prefetch log pageid */

  bool allocated_resources;
};

#define VACUUM_MAX_WORKER_COUNT	  50

// inline vacuum functions replacing old macros
STATIC_INLINE VACUUM_WORKER *vacuum_get_vacuum_worker (THREAD_ENTRY * thread_p) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE bool vacuum_is_thread_vacuum (const THREAD_ENTRY * thread_p) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE bool vacuum_is_thread_vacuum_worker (const THREAD_ENTRY * thread_p) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE bool vacuum_is_thread_vacuum_master (const THREAD_ENTRY * thread_p) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE bool vacuum_is_skip_undo_allowed (THREAD_ENTRY * thread_p) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE LOG_TDES *vacuum_get_worker_tdes (THREAD_ENTRY * thread_p) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE VACUUM_WORKER_STATE vacuum_get_worker_state (THREAD_ENTRY * thread_p) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void vacuum_set_worker_state (THREAD_ENTRY * thread_p, VACUUM_WORKER_STATE state)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE bool vacuum_worker_state_is_inactive (THREAD_ENTRY * thread_p) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE bool vacuum_worker_state_is_process_log (THREAD_ENTRY * thread_p) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE bool vacuum_worker_state_is_execute (THREAD_ENTRY * thread_p) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE bool vacuum_worker_state_is_topop (THREAD_ENTRY * thread_p) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE bool vacuum_worker_state_is_recovery (THREAD_ENTRY * thread_p) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE bool vacuum_is_process_log_for_vacuum (THREAD_ENTRY * thread_p) __attribute__ ((ALWAYS_INLINE));

/* Get vacuum worker from thread entry */
VACUUM_WORKER *
vacuum_get_vacuum_worker (THREAD_ENTRY * thread_p)
{
  assert (vacuum_is_thread_vacuum (thread_p));
  return thread_p->vacuum_worker;
}

bool
vacuum_is_thread_vacuum (const THREAD_ENTRY * thread_p)
{
  assert (thread_p != NULL);
  return thread_p != NULL && (thread_p->type == TT_VACUUM_MASTER || thread_p->type == TT_VACUUM_WORKER);
}

bool
vacuum_is_thread_vacuum_worker (const THREAD_ENTRY * thread_p)
{
  assert (thread_p != NULL);
  return thread_p != NULL && thread_p->type == TT_VACUUM_WORKER;
}

bool
vacuum_is_thread_vacuum_master (const THREAD_ENTRY * thread_p)
{
  assert (thread_p != NULL);
  return thread_p != NULL && thread_p->type == TT_VACUUM_MASTER;
}

/* Is thread a vacuum worker and undo logging can be skipped */
// todo: is this really needed?
bool
vacuum_is_skip_undo_allowed (THREAD_ENTRY * thread_p)
{
  if (!vacuum_is_thread_vacuum_worker (thread_p))
    {
      return false;
    }
  return vacuum_get_worker_state (thread_p) != VACUUM_WORKER_STATE_TOPOP;
}

/* Get a vacuum worker's transaction descriptor */
LOG_TDES *
vacuum_get_worker_tdes (THREAD_ENTRY * thread_p)
{
  return vacuum_get_vacuum_worker (thread_p)->tdes;
}

/* Get a vacuum worker's state */
VACUUM_WORKER_STATE
vacuum_get_worker_state (THREAD_ENTRY * thread_p)
{
  return vacuum_get_vacuum_worker (thread_p)->state;
}

/* Set a vacuum worker's state */
void
vacuum_set_worker_state (THREAD_ENTRY * thread_p, VACUUM_WORKER_STATE state)
{
  vacuum_get_vacuum_worker (thread_p)->state = state;
}

bool
vacuum_worker_state_is_inactive (THREAD_ENTRY * thread_p)
{
  return vacuum_get_worker_state (thread_p) == VACUUM_WORKER_STATE_INACTIVE;
}

bool
vacuum_worker_state_is_process_log (THREAD_ENTRY * thread_p)
{
  return vacuum_get_worker_state (thread_p) == VACUUM_WORKER_STATE_PROCESS_LOG;
}

bool
vacuum_worker_state_is_execute (THREAD_ENTRY * thread_p)
{
  return vacuum_get_worker_state (thread_p) == VACUUM_WORKER_STATE_EXECUTE;
}

bool
vacuum_worker_state_is_topop (THREAD_ENTRY * thread_p)
{
  return vacuum_get_worker_state (thread_p) == VACUUM_WORKER_STATE_TOPOP;
}

bool
vacuum_worker_state_is_recovery (THREAD_ENTRY * thread_p)
{
  return vacuum_get_worker_state (thread_p) == VACUUM_WORKER_STATE_RECOVERY;
}

// todo: remove me; check LOG_CS_OWN
bool
vacuum_is_process_log_for_vacuum (THREAD_ENTRY * thread_p)
{
  return vacuum_is_thread_vacuum (thread_p) && vacuum_worker_state_is_process_log (thread_p);
}

//todo: remove me; many references
#define VACUUM_IS_THREAD_VACUUM vacuum_is_thread_vacuum
#define VACUUM_IS_THREAD_VACUUM_WORKER vacuum_is_thread_vacuum_worker
#define VACUUM_IS_THREAD_VACUUM_MASTER vacuum_is_thread_vacuum_master

extern int vacuum_initialize (THREAD_ENTRY * thread_p, int vacuum_log_block_npages, VFID * vacuum_data_vfid,
			      VFID * dropped_files_vfid);
extern void vacuum_finalize (THREAD_ENTRY * thread_p);
extern int vacuum_boot (THREAD_ENTRY * thread_p);
extern void vacuum_stop (THREAD_ENTRY * thread_p);
extern int xvacuum (THREAD_ENTRY * thread_p);
extern MVCCID vacuum_get_global_oldest_active_mvccid (void);

extern int vacuum_create_file_for_vacuum_data (THREAD_ENTRY * thread_p, VFID * vacuum_data_vfid);
extern int vacuum_data_load_and_recover (THREAD_ENTRY * thread_p);
extern VACUUM_LOG_BLOCKID vacuum_get_log_blockid (LOG_PAGEID pageid);
extern void vacuum_produce_log_block_data (THREAD_ENTRY * thread_p, LOG_LSA * start_lsa, MVCCID oldest_mvccid,
					   MVCCID newest_mvccid);
extern int vacuum_consume_buffer_log_blocks (THREAD_ENTRY * thread_p);
extern LOG_PAGEID vacuum_min_log_pageid_to_keep (THREAD_ENTRY * thread_p);
extern bool vacuum_is_safe_to_remove_archives (void);
extern void vacuum_notify_server_crashed (LOG_LSA * recovery_lsa);
extern void vacuum_notify_server_shutdown (void);
extern int vacuum_rv_redo_vacuum_complete (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern int vacuum_rv_redo_initialize_data_page (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern int vacuum_rv_redo_data_finished (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern void vacuum_rv_redo_data_finished_dump (FILE * fp, int length, void *data);
extern int vacuum_rv_undoredo_data_set_link (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern void vacuum_rv_undoredo_data_set_link_dump (FILE * fp, int length, void *data);
extern int vacuum_rv_redo_append_data (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern void vacuum_rv_redo_append_data_dump (FILE * fp, int length, void *data);

extern void vacuum_cache_log_postpone_redo_data (THREAD_ENTRY * thread_p, char *data_header, char *rcv_data,
						 int rcv_data_length);
extern void vacuum_cache_log_postpone_lsa (THREAD_ENTRY * thread_p, LOG_LSA * lsa);
extern bool vacuum_do_postpone_from_cache (THREAD_ENTRY * thread_p, LOG_LSA * start_postpone_lsa);
extern int vacuum_rv_redo_start_job (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern VACUUM_WORKER *vacuum_rv_get_worker_by_trid (THREAD_ENTRY * thread_p, TRANID trid);
extern void vacuum_rv_finish_worker_recovery (THREAD_ENTRY * thread_p, TRANID trid);

extern int vacuum_heap_page (THREAD_ENTRY * thread_p, VACUUM_HEAP_OBJECT * heap_objects, int n_heap_objects,
			     MVCCID threshold_mvccid, HFID * hfid, bool * reusable, bool was_interrupted);
extern int vacuum_rv_redo_vacuum_heap_page (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern int vacuum_rv_redo_remove_ovf_insid (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern int vacuum_rv_undo_vacuum_heap_record (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern int vacuum_rv_redo_vacuum_heap_record (THREAD_ENTRY * thread_p, LOG_RCV * rcv);

extern int vacuum_create_file_for_dropped_files (THREAD_ENTRY * thread_p, VFID * dropped_files_vfid);
extern int vacuum_load_dropped_files_from_disk (THREAD_ENTRY * thread_p);
extern int vacuum_is_file_dropped (THREAD_ENTRY * thread_p, bool * is_file_dropped, VFID * vfid, MVCCID mvccid);
extern int vacuum_rv_notify_dropped_file (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern void vacuum_log_add_dropped_file (THREAD_ENTRY * thread_p, const VFID * vfid, const OID * class_oid,
					 bool postpone_or_undo);
extern int vacuum_rv_redo_add_dropped_file (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern int vacuum_rv_undo_add_dropped_file (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern int vacuum_rv_replace_dropped_file (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern int vacuum_rv_redo_cleanup_dropped_files (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern int vacuum_rv_set_next_page_dropped_files (THREAD_ENTRY * thread_p, LOG_RCV * rcv);

extern DISK_ISVALID vacuum_check_not_vacuumed_recdes (THREAD_ENTRY * thread_p, OID * oid, OID * class_oid,
						      RECDES * recdes, int btree_node_type);
extern DISK_ISVALID vacuum_check_not_vacuumed_rec_header (THREAD_ENTRY * thread_p, OID * oid, OID * class_oid,
							  MVCC_REC_HEADER * rec_header, int btree_node_type);
extern bool vacuum_is_mvccid_vacuumed (MVCCID id);
extern int vacuum_rv_check_at_undo (THREAD_ENTRY * thread_p, PAGE_PTR pgptr, INT16 slotid, INT16 rec_type);

extern void vacuum_log_last_blockid (THREAD_ENTRY * thread_p);

extern void vacuum_rv_convert_thread_to_vacuum (THREAD_ENTRY * thread_p, TRANID trid, thread_type & save_type);
extern void vacuum_restore_thread (THREAD_ENTRY * thread_p, thread_type save_type);

extern int vacuum_rv_es_nop (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
#if defined (SERVER_MODE)
extern void vacuum_notify_es_deleted (THREAD_ENTRY * thread_p, const char *uri);
#endif /* SERVER_MODE */

extern int vacuum_reset_data_after_copydb (THREAD_ENTRY * thread_p);
#endif /* _VACUUM_H_ */
