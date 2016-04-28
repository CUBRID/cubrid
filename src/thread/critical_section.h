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
 * critical_section.h - Definitions for critical section interface.
 */

#ifndef _CRITICAL_SECTION_H_
#define _CRITICAL_SECTION_H_

#ident "$Id$"

#if !defined(WINDOWS)
#include <pthread.h>
#endif /* !WINDOWS */

#include "thread.h"
#include "dbtype.h"

enum
{
  INF_WAIT = -1,		/* INFINITE WAIT */
  NOT_WAIT = 0			/* NO WAIT */
};

/*
 * These are the user defined lock definitions. When adding more locks, also
 * add initialization entries in critical_section.c
 */
enum
{
  CSECT_ER_LOG_FILE = 0,	/* Latch for error msg log file */
  CSECT_ER_MSG_CACHE,		/* Latch for error msg cache */
  CSECT_WFG,			/* Latch for wait-for-graph */
  CSECT_LOG,			/* Latch for log manager */
  CSECT_LOCATOR_SR_CLASSNAME_TABLE,	/* Latch for classname to classOID entries */
  CSECT_FILE_NEWFILE,		/* Latch related to new file table */
  CSECT_QPROC_QUERY_TABLE,	/* Latch for query manager table */
  CSECT_QPROC_LIST_CACHE,	/* Latch for query result(list file) cache (mht) */
  CSECT_BOOT_SR_DBPARM,		/* Latch for accessing System Database parameters. Used during vol creation */
  CSECT_DISK_REFRESH_GOODVOL,	/* Latch for refreshing good volume cache */
  CSECT_CNV_FMT_LEXER,		/* Latch for value/string format translation lexer */
  CSECT_HEAP_CHNGUESS,		/* Latch for schema change */

  CSECT_TRAN_TABLE,		/* Latch for transaction table */
  CSECT_CT_OID_TABLE,
  CSECT_HA_SERVER_STATE,	/* Latch for HA server mode change */
  CSECT_COMPACTDB_ONE_INSTANCE,	/* Latch for compactdb */
  CSECT_ACL,			/* Latch for accessible IP list table */
  CSECT_QPROC_FILTER_PRED_CACHE,	/* Latch for PRED XASL cache */
  CSECT_PARTITION_CACHE,	/* Latch for partitions cache */
  CSECT_EVENT_LOG_FILE,		/* Latch for event log file */
  CSECT_CONN_ACTIVE,		/* Latch for Active conn list */
  CSECT_CONN_FREE,		/* Latch for Free conn list */
  CSECT_TEMPFILE_CACHE,		/* Latch for temp file cache */
  CSECT_LOG_PB,			/* Latch for log_Pb */
  CSECT_LOG_ARCHIVE,		/* Latch for log_Gl.archive */
  CSECT_ACCESS_STATUS,		/* Latch for user access status */
  CSECT_LAST
};

#define CRITICAL_SECTION_COUNT  CSECT_LAST

extern const char *csect_Name_conn;
extern const char *csect_Name_tdes;

typedef struct sync_critical_section
{
  int cs_index;
  const char *name;
  pthread_mutex_t lock;		/* read/write monitor lock */
  int rwlock;			/* >0 = # readers, <0 = writer, 0 = none */
  unsigned int waiting_readers;	/* # of waiting readers */
  unsigned int waiting_writers;	/* # of waiting writers */
  pthread_cond_t readers_ok;	/* start waiting readers */
  THREAD_ENTRY *waiting_writers_queue;	/* queue of waiting writers */
  THREAD_ENTRY *waiting_promoters_queue;	/* queue of waiting promoters */
  pthread_t owner;		/* CS owner writer */
  int tran_index;		/* transaction id acquiring CS */
  unsigned int total_enter;
  unsigned int total_nwaits;	/* total # of waiters */
  struct timeval max_wait;
  struct timeval total_wait;
} SYNC_CRITICAL_SECTION;

typedef struct sync_rwlock
{
  pthread_mutex_t read_lock;	/* read lock. Only readers will use it. */
  pthread_mutex_t global_lock;	/* global lock */
  char *name;			/* name strduped - should be freed */
  int num_readers;		/* # of readers. Only readers will use it. */
  int for_trace;		/* SYNC_RWLOCK_TRACE to monitor the SYNC_RWLOCK. It should be a global SYNC_RWLOCK. */
  unsigned int total_enter;
  struct timeval max_wait;
  struct timeval total_wait;
} SYNC_RWLOCK;

#define RWLOCK_TRACE 1
#define RWLOCK_NOT_TRACE 0

extern int csect_initialize (void);
extern int csect_finalize (void);

extern int csect_enter (THREAD_ENTRY * thread_p, int cs_index, int wait_secs);
extern int csect_enter_as_reader (THREAD_ENTRY * thread_p, int cs_index, int wait_secs);
extern int csect_demote (THREAD_ENTRY * thread_p, int cs_index, int wait_secs);
extern int csect_promote (THREAD_ENTRY * thread_p, int cs_index, int wait_secs);
extern int csect_exit (THREAD_ENTRY * thread_p, int cs_index);

extern int csect_initialize_critical_section (SYNC_CRITICAL_SECTION * cs_ptr);
extern int csect_finalize_critical_section (SYNC_CRITICAL_SECTION * cs_ptr);
extern int csect_enter_critical_section (THREAD_ENTRY * thread_p, SYNC_CRITICAL_SECTION * cs_ptr, int wait_secs);
extern int csect_enter_critical_section_as_reader (THREAD_ENTRY * thread_p, SYNC_CRITICAL_SECTION * cs_ptr,
						   int wait_secs);
extern int csect_exit_critical_section (THREAD_ENTRY * thread_p, SYNC_CRITICAL_SECTION * cs_ptr);

extern int csect_check_own (THREAD_ENTRY * thread_p, int cs_index);

extern void csect_dump_statistics (FILE * fp);

extern int csect_start_scan (THREAD_ENTRY * thread_p, int show_type, DB_VALUE ** arg_values, int arg_cnt, void **ctx);

extern int rwlock_initialize (SYNC_RWLOCK * rwlock, const char *name, int for_trace);
extern int rwlock_finalize (SYNC_RWLOCK * rwlock);

extern int rwlock_read_lock (SYNC_RWLOCK * rwlock);
extern int rwlock_read_unlock (SYNC_RWLOCK * rwlock);

extern int rwlock_write_lock (SYNC_RWLOCK * rwlock);
extern int rwlock_write_unlock (SYNC_RWLOCK * rwlock);

extern int rwlock_initialize_rwlock_monitor (void);
extern int rwlock_finalize_rwlock_monitor (void);

extern void rwlock_dump_statistics (FILE * fp);

#if !defined(SERVER_MODE)
#define csect_initialize_critical_section(a)
#define csect_finalize_critical_section(a)
#define csect_enter(a, b, c) NO_ERROR
#define csect_enter_as_reader(a, b, c) NO_ERROR
#define csect_exit(a, b)
#define csect_enter_critical_section(a, b, c)
#define csect_enter_critical_section_as_reader(a, b, c)
#define csect_exit_critical_section(a, b)
#define csect_check_own(a, b) 1
#define csect_start_scan NULL

#define rwlock_initialize(a, b, c) NO_ERROR
#define rwlock_finalize(a) NO_ERROR
#define rwlock_read_lock(a) NO_ERROR
#define rwlock_read_unlock(a) NO_ERROR
#define rwlock_write_lock(a) NO_ERROR
#define rwlock_write_unlock(a) NO_ERROR
#endif /* !SERVER_MODE */

#endif /* _CRITICAL_SECTION_H_ */
