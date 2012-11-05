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
 * page_buffer.h - PAGE BUFFER MANAGMENT MODULE (AT SERVER)
 */

#ifndef _PAGE_BUFFER_H_
#define _PAGE_BUFFER_H_

#ident "$Id$"

#include "config.h"

#include "error_manager.h"
#include "storage_common.h"
#include "disk_manager.h"
#include "lock_manager.h"

#define NEW_PAGE		true	/* New page constant for page fetch */
#define OLD_PAGE		false	/* Old page constant for page fetch */
#define FREE			true	/* Free page buffer */
#define DONT_FREE		false	/* Don't free the page buffer */

/* Set a vpid with values of volid and pageid */
#define VPID_SET(vpid_ptr, volid_value, pageid_value)	      \
  do {							      \
    (vpid_ptr)->volid  = (volid_value);			      \
    (vpid_ptr)->pageid = (pageid_value);		      \
  } while(0)

/* Set the vpid to an invalid one */
#define VPID_SET_NULL(vpid_ptr) VPID_SET(vpid_ptr, NULL_VOLID, NULL_PAGEID)

/* vpid1 == vpid2 ? */
#define VPID_EQ(vpid_ptr1, vpid_ptr2)                         \
  ((vpid_ptr1) == (vpid_ptr2) ||                              \
   ((vpid_ptr1)->pageid == (vpid_ptr2)->pageid &&             \
    (vpid_ptr1)->volid  == (vpid_ptr2)->volid))

/* Is vpid NULL ? */
#define VPID_ISNULL(vpid_ptr) ((vpid_ptr)->pageid == NULL_PAGEID)

#define pgbuf_unfix_and_init(thread_p, pgptr) \
  do { \
    pgbuf_unfix ((thread_p), (pgptr)); \
    (pgptr) = NULL; \
  } while (0)

/* public page latch mode */
enum
{
  PGBUF_NO_LATCH = 10,
  PGBUF_LATCH_READ,
  PGBUF_LATCH_WRITE,
  PGBUF_LATCH_FLUSH,
  PGBUF_LATCH_VICTIM,
  PGBUF_LATCH_INVALID,
  PGBUF_LATCH_FLUSH_INVALID,
  PGBUF_LATCH_VICTIM_INVALID
};

typedef enum
{
  PGBUF_UNCONDITIONAL_LATCH,
  PGBUF_CONDITIONAL_LATCH
} PGBUF_LATCH_CONDITION;

typedef enum
{
  PGBUF_DEBUG_NO_PAGE_VALIDATION,
  PGBUF_DEBUG_PAGE_VALIDATION_FETCH,
  PGBUF_DEBUG_PAGE_VALIDATION_FREE,
  PGBUF_DEBUG_PAGE_VALIDATION_ALL
} PGBUF_DEBUG_PAGE_VALIDATION_LEVEL;

extern unsigned int pgbuf_hash_vpid (const void *key_vpid,
				     unsigned int htsize);
extern int pgbuf_compare_vpid (const void *key_vpid1, const void *key_vpid2);
extern int pgbuf_initialize (void);
extern void pgbuf_finalize (void);
extern PAGE_PTR pgbuf_fix_with_retry (THREAD_ENTRY * thread_p,
				      const VPID * vpid, int newpg, int mode,
				      int retry);
#if !defined(NDEBUG)
#define pgbuf_flush(thread_p, pgptr, free_page) \
	pgbuf_flush_debug(thread_p, pgptr, free_page, __FILE__, __LINE__)
extern PAGE_PTR pgbuf_flush_debug (THREAD_ENTRY * thread_p, PAGE_PTR pgptr,
				   int free_page,
				   const char *caller_file, int caller_line);

#define pgbuf_fix(thread_p, vpid, newpg, requestmode, condition) \
        pgbuf_fix_debug(thread_p, vpid, newpg, requestmode, condition, \
                        __FILE__, __LINE__)
extern PAGE_PTR pgbuf_fix_debug (THREAD_ENTRY * thread_p, const VPID * vpid,
				 int newpg, int requestmode,
				 PGBUF_LATCH_CONDITION condition,
				 const char *caller_file, int caller_line);
#define pgbuf_unfix(thread_p, pgptr) \
	pgbuf_unfix_debug(thread_p, pgptr, __FILE__, __LINE__)
extern void pgbuf_unfix_debug (THREAD_ENTRY * thread_p, PAGE_PTR pgptr,
			       const char *caller_file, int caller_line);
#define pgbuf_invalidate_all(thread_p, volid) \
	pgbuf_invalidate_all_debug(thread_p, volid, __FILE__, __LINE__)
extern int pgbuf_invalidate_all_debug (THREAD_ENTRY * thread_p, VOLID volid,
				       const char *caller_file,
				       int caller_line);

#define pgbuf_invalidate(thread_p, pgptr) \
	pgbuf_invalidate_debug(thread_p, pgptr, __FILE__, __LINE__)
extern int pgbuf_invalidate_debug (THREAD_ENTRY * thread_p, PAGE_PTR pgptr,
				   const char *caller_file, int caller_line);
#else /* NDEBUG */
extern PAGE_PTR pgbuf_flush (THREAD_ENTRY * thread_p, PAGE_PTR pgptr,
			     int free_page);
#define pgbuf_fix(thread_p, vpid, newpg, requestmode, condition) \
        pgbuf_fix_release(thread_p, vpid, newpg, requestmode, condition)
extern PAGE_PTR pgbuf_fix_release (THREAD_ENTRY * thread_p, const VPID * vpid,
				   int newpg, int requestmode,
				   PGBUF_LATCH_CONDITION condition);
extern void pgbuf_unfix (THREAD_ENTRY * thread_p, PAGE_PTR pgptr);
extern int pgbuf_invalidate_all (THREAD_ENTRY * thread_p, VOLID volid);
extern int pgbuf_invalidate (THREAD_ENTRY * thread_p, PAGE_PTR pgptr);
#endif /* NDEBUG */
extern PAGE_PTR pgbuf_flush_with_wal (THREAD_ENTRY * thread_p,
				      PAGE_PTR pgptr);
#if !defined(NDEBUG)
#define pgbuf_flush_all(thread_p, volid) \
	pgbuf_flush_all_debug(thread_p, volid, __FILE__, __LINE__)
extern int pgbuf_flush_all_debug (THREAD_ENTRY * thread_p, VOLID volid,
				  const char *caller_file, int caller_line);

#define pgbuf_flush_all_unfixed(thread_p, volid) \
	pgbuf_flush_all_unfixed_debug(thread_p, volid, __FILE__, __LINE__)
extern int pgbuf_flush_all_unfixed_debug (THREAD_ENTRY * thread_p,
					  VOLID volid,
					  const char *caller_file,
					  int caller_line);

#define pgbuf_flush_all_unfixed_and_set_lsa_as_null(thread_p, volid) \
	pgbuf_flush_all_unfixed_and_set_lsa_as_null_debug(thread_p, volid, \
							  __FILE__, \
							  __LINE__)
extern int pgbuf_flush_all_unfixed_and_set_lsa_as_null_debug (THREAD_ENTRY *
							      thread_p,
							      VOLID volid,
							      const char
							      *caller_file,
							      int
							      caller_line);

#define pgbuf_flush_victim_candidate(thread_p, flush_ratio) \
	pgbuf_flush_victim_candidate_debug(thread_p, flush_ratio, __FILE__, __LINE__)
extern int pgbuf_flush_victim_candidate_debug (THREAD_ENTRY * thread_p,
					       float flush_ratio,
					       const char *caller_file,
					       int caller_line);

#define pgbuf_flush_checkpoint(thread_p, flush_upto_lsa, prev_chkpt_redo_lsa, smallest_lsa) \
	pgbuf_flush_checkpoint_debug(thread_p, flush_upto_lsa, prev_chkpt_redo_lsa, smallest_lsa, \
				      __FILE__, __LINE__)
extern int pgbuf_flush_checkpoint_debug (THREAD_ENTRY * thread_p,
					 const LOG_LSA * flush_upto_lsa,
					 const LOG_LSA *
					 prev_chkpt_redo_lsa,
					 LOG_LSA * smallest_lsa,
					 const char *caller_file,
					 int caller_line);
#else /* NDEBUG */
extern int pgbuf_flush_all (THREAD_ENTRY * thread_p, VOLID volid);
extern int pgbuf_flush_all_unfixed (THREAD_ENTRY * thread_p, VOLID volid);
extern int pgbuf_flush_all_unfixed_and_set_lsa_as_null (THREAD_ENTRY *
							thread_p,
							VOLID volid);
extern int pgbuf_flush_victim_candidate (THREAD_ENTRY * thread_p,
					 float flush_ratio);
extern int pgbuf_flush_checkpoint (THREAD_ENTRY * thread_p,
				   const LOG_LSA * flush_upto_lsa,
				   const LOG_LSA * prev_chkpt_redo_lsa,
				   LOG_LSA * smallest_lsa);
#endif /* NDEBUG */
extern void *pgbuf_copy_to_area (THREAD_ENTRY * thread_p, const VPID * vpid,
				 int start_offset, int length, void *area,
				 bool do_fetch);
extern void *pgbuf_copy_from_area (THREAD_ENTRY * thread_p, const VPID * vpid,
				   int start_offset, int length, void *area,
				   bool do_fetch);
extern void pgbuf_set_dirty (THREAD_ENTRY * thread_p, PAGE_PTR pgptr,
			     int free_page);
extern LOG_LSA *pgbuf_get_lsa (PAGE_PTR pgptr);
extern const LOG_LSA *pgbuf_set_lsa (THREAD_ENTRY * thread_p, PAGE_PTR pgptr,
				     const LOG_LSA * lsa_ptr);
extern void pgbuf_reset_temp_lsa (PAGE_PTR pgptr);
extern void pgbuf_get_vpid (PAGE_PTR pgptr, VPID * vpid);
extern VPID *pgbuf_get_vpid_ptr (PAGE_PTR pgptr);
extern PAGEID pgbuf_get_page_id (PAGE_PTR pgptr);
extern VOLID pgbuf_get_volume_id (PAGE_PTR pgptr);
extern const char *pgbuf_get_volume_label (PAGE_PTR pgptr);
extern void pgbuf_refresh_max_permanent_volume_id (VOLID volid);
extern void pgbuf_cache_permanent_volume_for_temporary (VOLID volid);
extern void pgbuf_force_to_check_for_interrupts (void);
extern bool pgbuf_is_log_check_for_interrupts (THREAD_ENTRY * thread_p);
extern void pgbuf_unfix_all (THREAD_ENTRY * thread_p);
extern void pgbuf_set_lsa_as_temporary (THREAD_ENTRY * thread_p,
					PAGE_PTR pgptr);
extern void pgbuf_set_lsa_as_permanent (THREAD_ENTRY * thread_p,
					PAGE_PTR pgptr);
extern bool pgbuf_is_lsa_temporary (PAGE_PTR pgptr);
extern void pgbuf_invalidate_temporary_file (VOLID volid, PAGEID first_pageid,
					     DKNPAGES npages,
					     bool need_invalidate);
#if defined(CUBRID_DEBUG)
extern void pgbuf_dump_if_any_fixed (void);
#endif

#endif /* _PAGE_BUFFER_H_ */
