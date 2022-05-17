/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */


/*
 * critical_section.h - Definitions for critical section interface.
 */

#ifndef _CRITICAL_SECTION_H_
#define _CRITICAL_SECTION_H_

#ident "$Id$"

#include "dbtype_def.h"
#include "porting.h"		// for pthread, STATIC_CAST
#include "thread_compat.hpp"

#if !defined(WINDOWS)
#include <pthread.h>
#endif /* !WINDOWS */
#if !defined(WINDOWS)
#include <sys/time.h>
#endif /* !WINDOWS */
#if defined (WINDOWS)
#include <WinSock2.h>
#endif // WINDOWS

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error critical_section.h belongs to server or stand-alone modules.
#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

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
  CSECT_WFG = 0,		/* Latch for wait-for-graph */
  CSECT_LOG,			/* Latch for log manager */
  CSECT_LOCATOR_SR_CLASSNAME_TABLE,	/* Latch for classname to classOID entries */
  CSECT_QPROC_QUERY_TABLE,	/* Latch for query manager table */
  CSECT_QPROC_LIST_CACHE,	/* Latch for query result(list file) cache (mht) */
  CSECT_QPROC_QUERY_ENTRY,	/* Latch for query entry */
  CSECT_DISK_CHECK,		/* Block changes on disk cache during check */
  CSECT_CNV_FMT_LEXER,		/* Latch for value/string format translation lexer */
  CSECT_HEAP_CHNGUESS,		/* Latch for schema change */

  CSECT_TRAN_TABLE,		/* Latch for transaction table */
  CSECT_CT_OID_TABLE,
  CSECT_HA_SERVER_STATE,	/* Latch for HA server mode change */
  CSECT_COMPACTDB_ONE_INSTANCE,	/* Latch for compactdb */
  CSECT_ACL,			/* Latch for accessible IP list table */
  CSECT_PARTITION_CACHE,	/* Latch for partitions cache */
  CSECT_EVENT_LOG_FILE,		/* Latch for event log file */
  CSECT_LOG_ARCHIVE,		/* Latch for log_Gl.archive */
  CSECT_ACCESS_STATUS,		/* Latch for user access status */
  CSECT_LAST
};

static const int CRITICAL_SECTION_COUNT = STATIC_CAST (int, CSECT_LAST);

typedef enum
{
  SYNC_TYPE_NONE,

  SYNC_TYPE_CSECT,		/* critical section primitive */
  SYNC_TYPE_RWLOCK,		/* rwlock primitive */
  SYNC_TYPE_RMUTEX,		/* re-entrant mutex */
  SYNC_TYPE_MUTEX,		/* simple mutex */

  SYNC_TYPE_LAST = SYNC_TYPE_MUTEX,
  SYNC_TYPE_COUNT = SYNC_TYPE_LAST,
  SYNC_TYPE_ALL = SYNC_TYPE_LAST
} SYNC_PRIMITIVE_TYPE;

typedef struct sync_stats
{
  SYNC_PRIMITIVE_TYPE type;
  const char *name;

  unsigned int nenter;		/* total number of acquisition. */
  unsigned int nwait;		/* total number of waiting. only available for SYNC_TYPE_CSECT */
  unsigned int nreenter;	/* total number of re-acquisition. available for SYNC_TYPE_CSECT, SYNC_TYPE_RMUTEX */

  struct timeval total_elapsed;	/* total elapsed time to acquire the synchronization primitive */
  struct timeval max_elapsed;	/* max elapsed time to acquire the synchronization primitive */
} SYNC_STATS;

typedef struct sync_critical_section
{
  const char *name;
  int cs_index;
  pthread_mutex_t lock;		/* read/write monitor lock */
  int rwlock;			/* >0 = # readers, <0 = writer, 0 = none */
  unsigned int waiting_readers;	/* # of waiting readers */
  unsigned int waiting_writers;	/* # of waiting writers */
  pthread_cond_t readers_ok;	/* start waiting readers */
  THREAD_ENTRY *waiting_writers_queue;	/* queue of waiting writers */
  THREAD_ENTRY *waiting_promoters_queue;	/* queue of waiting promoters */
  thread_id_t owner;		/* CS owner writer */
  int tran_index;		/* transaction id acquiring CS */
  SYNC_STATS *stats;
} SYNC_CRITICAL_SECTION;

typedef struct sync_rwlock
{
  const char *name;
  pthread_mutex_t read_lock;	/* read lock. Only readers will use it. */
  pthread_mutex_t global_lock;	/* global lock */
  int num_readers;		/* # of readers. Only readers will use it. */
  SYNC_STATS *stats;
} SYNC_RWLOCK;

typedef struct sync_rmutex
{
  const char *name;
  pthread_mutex_t lock;		/* mutex */
  thread_id_t owner;		/* owner thread id */
  int lock_cnt;			/* # of times that owner enters */
  SYNC_STATS *stats;
} SYNC_RMUTEX;

extern int csect_initialize_static_critical_sections (void);
extern int csect_finalize_static_critical_sections (void);

extern int csect_enter (THREAD_ENTRY * thread_p, int cs_index, int wait_secs);
extern int csect_enter_as_reader (THREAD_ENTRY * thread_p, int cs_index, int wait_secs);
extern int csect_demote (THREAD_ENTRY * thread_p, int cs_index, int wait_secs);
extern int csect_promote (THREAD_ENTRY * thread_p, int cs_index, int wait_secs);
extern int csect_exit (THREAD_ENTRY * thread_p, int cs_index);

#if defined (SERVER_MODE)
extern const char *csect_name_at (int cs_index);
#else // not SERVER_MODE = SA_MODE
static inline const char *
csect_name_at (int cs_index)
{
  return "UNKNOWN";
}
#endif // not SERVER_MODE = SA_MODE
extern int csect_initialize_critical_section (SYNC_CRITICAL_SECTION * cs_ptr, const char *name);
extern int csect_finalize_critical_section (SYNC_CRITICAL_SECTION * cs_ptr);
extern int csect_enter_critical_section (THREAD_ENTRY * thread_p, SYNC_CRITICAL_SECTION * cs_ptr, int wait_secs);
extern int csect_enter_critical_section_as_reader (THREAD_ENTRY * thread_p, SYNC_CRITICAL_SECTION * cs_ptr,
						   int wait_secs);
extern int csect_exit_critical_section (THREAD_ENTRY * thread_p, SYNC_CRITICAL_SECTION * cs_ptr);

extern int csect_check_own (THREAD_ENTRY * thread_p, int cs_index);

extern void csect_dump_statistics (FILE * fp);

extern int csect_start_scan (THREAD_ENTRY * thread_p, int show_type, DB_VALUE ** arg_values, int arg_cnt, void **ctx);

extern int rwlock_initialize (SYNC_RWLOCK * rwlock, const char *name);
extern int rwlock_finalize (SYNC_RWLOCK * rwlock);

extern int rwlock_read_lock (SYNC_RWLOCK * rwlock);
extern int rwlock_read_unlock (SYNC_RWLOCK * rwlock);

extern int rwlock_write_lock (SYNC_RWLOCK * rwlock);
extern int rwlock_write_unlock (SYNC_RWLOCK * rwlock);

extern int sync_initialize_sync_stats (void);
extern int sync_finalize_sync_stats (void);

extern void rwlock_dump_statistics (FILE * fp);

extern int rmutex_initialize (SYNC_RMUTEX * rmutex, const char *name);
extern int rmutex_finalize (SYNC_RMUTEX * rmutex);

extern int rmutex_lock (THREAD_ENTRY * thread_p, SYNC_RMUTEX * rmutex);
extern int rmutex_unlock (THREAD_ENTRY * thread_p, SYNC_RMUTEX * rmutex);

extern void rmutex_dump_statistics (FILE * fp);

extern void sync_dump_statistics (FILE * fp, SYNC_PRIMITIVE_TYPE type);

#if !defined(SERVER_MODE)
#define csect_initialize_critical_section(a, b)
#define csect_finalize_critical_section(a)
#define csect_enter(a, b, c) NO_ERROR
#define csect_enter_as_reader(a, b, c) NO_ERROR
#define csect_exit(a, b)
#define csect_enter_critical_section(a, b, c)
#define csect_enter_critical_section_as_reader(a, b, c)
#define csect_exit_critical_section(a, b)
#define csect_check_own(a, b) 1
#define csect_start_scan NULL

#define rwlock_initialize(a, b) NO_ERROR
#define rwlock_finalize(a) NO_ERROR
#define rwlock_read_lock(a) NO_ERROR
#define rwlock_read_unlock(a) NO_ERROR
#define rwlock_write_lock(a) NO_ERROR
#define rwlock_write_unlock(a) NO_ERROR

#define rmutex_initialize(a, b) NO_ERROR
#define rmutex_finalize(a) NO_ERROR
#define rmutex_lock(a, b) NO_ERROR
#define rmutex_unlock(a, b) NO_ERROR
#endif /* !SERVER_MODE */

#endif /* _CRITICAL_SECTION_H_ */
