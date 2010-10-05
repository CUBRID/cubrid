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
#include "thread_impl.h"

enum
{ INF_WAIT = -1,		/* INFINITE WAIT */
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
  CSECT_LOCATOR_SR_CLASSNAME_TABLE,	/* Latch for temp classname to classOID entries */
  CSECT_FILE_NEWFILE,		/* Latch related to new file table */
  CSECT_QPROC_QUERY_TABLE,	/* Latch for query manager table */
  CSECT_QPROC_QFILE_PGCNT,	/* Latch for query file page count */
  CSECT_QPROC_XASL_CACHE,	/* Latch for XASL cache (mht: memory hash table) */
  CSECT_QPROC_LIST_CACHE,	/* Latch for query result(list file) cache (mht) */
  CSECT_BOOT_SR_DBPARM,		/* Latch for accessing System Database parameters.
				 * Used during vol creation */
  CSECT_DISK_REFRESH_GOODVOL,	/* Latch for refreshing good volume cache */
  CSECT_CNV_FMT_LEXER,		/* Latch for value/string format translation lexer */
  CSECT_HEAP_CHNGUESS,		/* Latch for schema change */
  CSECT_SPAGE_SAVESPACE,	/* Latch for slotted page saving space */

  CSECT_TRAN_TABLE,		/* Latch for transaction table */
  CSECT_CT_OID_TABLE,
  CSECT_SCANID_BITMAP,
  CSECT_LOG_FLUSH,		/* for 2 flushing (by LFT, by normal thread) */
  CSECT_HA_SERVER_STATE,	/* Latch for HA server mode change */
  CSECT_COMPACTDB_ONE_INSTANCE,	/* Latch for compactdb */
  CSECT_LAST
};

typedef struct css_critical_section
{
  int cs_index;
  const char *name;
  MUTEX_T lock;			/* read/write monitor lock */
  int rwlock;			/* >0 = # readers, <0 = writer, 0 = none */
  unsigned int waiting_writers;	/* # of waiting writers */
  COND_T readers_ok;		/* start waiting readers */
  THREAD_ENTRY *waiting_writers_queue;	/* queue of waiting writers */
  THREAD_ENTRY *waiting_promoters_queue;	/* queue of waiting promoters */
  THREAD_T owner;		/* CS owner writer */
  int tran_index;		/* transaction id acquiring CS */
  unsigned int total_enter;
  unsigned int total_nwaits;	/* total # of waiters */
  struct timeval max_wait;
  struct timeval total_wait;
} CSS_CRITICAL_SECTION;

extern int csect_initialize (void);
extern void cs_clear_tran_index (int tran_index);
extern int csect_finalize (void);

extern int csect_enter (THREAD_ENTRY * thread_p, int cs_index, int wait_secs);
extern int csect_enter_as_reader (THREAD_ENTRY * thread_p, int cs_index,
				  int wait_secs);
extern int csect_demote (THREAD_ENTRY * thread_p, int cs_index,
			 int wait_secs);
extern int csect_promote (THREAD_ENTRY * thread_p, int cs_index,
			  int wait_secs);
extern int csect_exit (int cs_index);

extern int csect_initialize_critical_section (CSS_CRITICAL_SECTION * cs_ptr);
extern int csect_finalize_critical_section (CSS_CRITICAL_SECTION * cs_ptr);
extern int csect_enter_critical_section (THREAD_ENTRY * thread_p,
					 CSS_CRITICAL_SECTION * cs_ptr,
					 int wait_secs);
extern int csect_enter_critical_section_as_reader (THREAD_ENTRY * thread_p,
						   CSS_CRITICAL_SECTION *
						   cs_ptr, int wait_secs);
extern int csect_demote_critical_section (THREAD_ENTRY * thread_p,
					  CSS_CRITICAL_SECTION * cs_ptr,
					  int wait_secs);
extern int csect_promote_critical_section (THREAD_ENTRY * thread_p,
					   CSS_CRITICAL_SECTION * cs_ptr,
					   int wait_secs);
extern int csect_exit_critical_section (CSS_CRITICAL_SECTION * cs_ptr);

extern int csect_check_own (THREAD_ENTRY * thread_p, int cs_index);

extern void csect_dump_statistics (FILE * fp);

#if !defined(SERVER_MODE)
#define csect_initialize_critical_section(a)
#define csect_finalize_critical_section(a)
#define csect_enter(a, b, c) NO_ERROR
#define csect_enter_as_reader(a, b, c) NO_ERROR
#define csect_exit(a)
#define csect_enter_critical_section(a,b,c)
#define csect_exit_critical_section(a)
#endif /* !SERVER_MODE */

#endif /* _CRITICAL_SECTION_H_ */
