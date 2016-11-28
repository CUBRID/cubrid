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
#include "perf_monitor.h"

#define FREE			true	/* Free page buffer */
#define DONT_FREE		false	/* Don't free the page buffer */

extern const VPID vpid_Null_vpid;

/* Set a vpid with values of volid and pageid */
#define VPID_SET(vpid_ptr, volid_value, pageid_value)	      \
  do {							      \
    (vpid_ptr)->volid  = (volid_value);			      \
    (vpid_ptr)->pageid = (pageid_value);		      \
  } while(0)

/* Set the vpid to an invalid one */
#define VPID_SET_NULL(vpid_ptr) VPID_SET(vpid_ptr, NULL_VOLID, NULL_PAGEID)

/* copy a VPID */
#define  VPID_COPY(dest_ptr, src_ptr)                      \
  do {							   \
    *(dest_ptr) = *(src_ptr);				   \
  } while (0)

/* vpid1 == vpid2 ? */
#define VPID_EQ(vpid_ptr1, vpid_ptr2)                         \
  ((vpid_ptr1) == (vpid_ptr2) ||                              \
   ((vpid_ptr1)->pageid == (vpid_ptr2)->pageid &&             \
    (vpid_ptr1)->volid  == (vpid_ptr2)->volid))

/* Is vpid NULL ? */
#define VPID_ISNULL(vpid_ptr) ((vpid_ptr)->pageid == NULL_PAGEID)

/* Get page VPID for OID */
#define VPID_GET_FROM_OID(vpid_ptr, oid_ptr) \
  VPID_SET (vpid_ptr, (oid_ptr)->volid, (oid_ptr)->pageid);
/* Check if OID's are on the same page */
#define VPID_EQ_FOR_OIDS(oid_ptr1, oid_ptr2) \
  (((oid_ptr1)->volid == (oid_ptr2)->volid)  \
   && ((oid_ptr1)->pageid == (oid_ptr2)->pageid))

#define PGBUF_PAGE_VPID_AS_ARGS(pg) pgbuf_get_volume_id (pg), pgbuf_get_page_id (pg)
#define PGBUF_PAGE_LSA_AS_ARGS(pg) (long long int) pgbuf_get_lsa (pg)->pageid, (int) pgbuf_get_lsa (pg)->offset

#define PGBUF_PAGE_STATE_MSG(name) name " { VPID = %d|%d, crt_lsa = %lld|%d } "
#define PGBUF_PAGE_STATE_ARGS(pg) PGBUF_PAGE_VPID_AS_ARGS (pg), PGBUF_PAGE_LSA_AS_ARGS (pg)

#define PGBUF_PAGE_MODIFY_MSG(name) name " { VPID = %d|%d, prev_lsa = %lld|%d, crt_lsa = %lld|%d } "
#define PGBUF_PAGE_MODIFY_ARGS(pg, prev_lsa) \
  PGBUF_PAGE_VPID_AS_ARGS (pg), LSA_AS_ARGS (prev_lsa), PGBUF_PAGE_LSA_AS_ARGS (pg)

#define pgbuf_unfix_and_init(thread_p, pgptr) \
  do { \
    pgbuf_unfix ((thread_p), (pgptr)); \
    (pgptr) = NULL; \
  } while (0)

#define pgbuf_ordered_unfix_and_init(thread_p, page, pg_watcher) \
  do { \
    if ((pg_watcher) != NULL) \
      {	\
	assert ((page) == (pg_watcher)->pgptr); \
	pgbuf_ordered_unfix ((thread_p), (pg_watcher)); \
	(pg_watcher)->pgptr = NULL; \
      } \
    else \
      { \
	pgbuf_unfix_and_init ((thread_p), (page)); \
      } \
    (page) = NULL; \
  } while (0)

#define PGBUF_WATCHER_MAGIC_NUMBER 0x12345678
#define PGBUF_ORDERED_NULL_HFID (pgbuf_ordered_null_hfid)

#define PGBUF_WATCHER_SET_GROUP(w,hfid) \
  do { \
    if ((hfid) == NULL || (hfid)->vfid.volid == NULL_VOLID \
	|| (hfid)->hpgid == NULL_PAGEID || HFID_IS_NULL (hfid)) \
      { \
	VPID_SET_NULL (&((w)->group_id)); \
      } \
    else \
      { \
	(w)->group_id.volid = (hfid)->vfid.volid; \
	(w)->group_id.pageid = (hfid)->hpgid; \
      } \
  } while (0)

#define PGBUF_WATCHER_COPY_GROUP(w_dst,w_src) \
  do { \
    assert ((w_src) != NULL); \
    assert ((w_dst) != NULL); \
    assert (!VPID_ISNULL (&((w_src)->group_id))); \
    VPID_COPY (&((w_dst)->group_id), &((w_src)->group_id)); \
  } while (0)

#define PGBUF_WATCHER_RESET_RANK(w,rank) \
  do { \
    (w)->initial_rank = (rank); \
  } while (0)

#if !defined(NDEBUG)
#define PGBUF_CLEAR_WATCHER(w) \
  do { \
    (w)->next = NULL; \
    (w)->prev = NULL; \
    (w)->pgptr = NULL; \
    pgbuf_watcher_init_debug ((w), __FILE__, __LINE__, false); \
  } while (0)

#define PGBUF_INIT_WATCHER(w,rank,hfid) \
  do { \
    PGBUF_CLEAR_WATCHER (w); \
    (w)->latch_mode = PGBUF_NO_LATCH; \
    (w)->page_was_unfixed = false; \
    (w)->initial_rank = (rank); \
    (w)->curr_rank = PGBUF_ORDERED_RANK_UNDEFINED; \
    PGBUF_WATCHER_SET_GROUP ((w), (hfid)); \
    (w)->watched_at[0] = '\0'; \
    (w)->magic = PGBUF_WATCHER_MAGIC_NUMBER; \
  } while (0)
#else
#define PGBUF_CLEAR_WATCHER(w) \
  do { \
    (w)->next = NULL; \
    (w)->prev = NULL; \
    (w)->pgptr = NULL; \
  } while (0)

#define PGBUF_INIT_WATCHER(w,rank,hfid) \
  do { \
    PGBUF_CLEAR_WATCHER (w); \
    (w)->latch_mode = PGBUF_NO_LATCH; \
    (w)->page_was_unfixed = false; \
    (w)->initial_rank = (rank); \
    (w)->curr_rank = PGBUF_ORDERED_RANK_UNDEFINED; \
    PGBUF_WATCHER_SET_GROUP ((w), (hfid)); \
  } while (0)
#endif

#define PGBUF_IS_CLEAN_WATCHER(w) (((w) != NULL && (w)->next == NULL \
  && (w)->prev == NULL && (w)->pgptr == NULL) ? true : false)

#define PGBUF_IS_ORDERED_PAGETYPE(ptype) \
  ((ptype) == PAGE_HEAP || (ptype) == PAGE_OVERFLOW)


typedef enum
{
  OLD_PAGE = 0,			/* Fetch page that should be allocated and already existing either in page buffer or on 
				 * disk. Must pass validation test and must be fixed from disk if it doesn't exist in
				 * buffer. */
  NEW_PAGE,			/* Fetch newly allocated page. Must pass validation test but it can be created directly 
				 * in buffer without fixing from disk. */
  OLD_PAGE_IF_EXISTS,		/* Fetch existing page only if is valid and if it exists in page buffer. Page may be
				 * deallocated or flushed and invalidated from buffer, in which case fixing page is not
				 * necessary. */
  OLD_PAGE_PREVENT_DEALLOC,	/* Fetch existing page and mark its memory buffer, to prevent deallocation. */
  OLD_PAGE_DEALLOCATED,		/* Fetch page that has been deallocated. */
} PAGE_FETCH_MODE;

/* public page latch mode */
typedef enum
{
  PGBUF_NO_LATCH = 0,
  PGBUF_LATCH_READ,
  PGBUF_LATCH_WRITE,
  PGBUF_LATCH_FLUSH,
  PGBUF_LATCH_VICTIM,
  PGBUF_LATCH_INVALID,
  PGBUF_LATCH_FLUSH_INVALID,
  PGBUF_LATCH_VICTIM_INVALID
} PGBUF_LATCH_MODE;

typedef enum
{
  PGBUF_UNCONDITIONAL_LATCH,
  PGBUF_CONDITIONAL_LATCH
} PGBUF_LATCH_CONDITION;

typedef enum
{
  PGBUF_PROMOTE_ONLY_READER,
  PGBUF_PROMOTE_SHARED_READER
} PGBUF_PROMOTE_CONDITION;

typedef enum
{
  PGBUF_DEBUG_NO_PAGE_VALIDATION,
  PGBUF_DEBUG_PAGE_VALIDATION_FETCH,
  PGBUF_DEBUG_PAGE_VALIDATION_FREE,
  PGBUF_DEBUG_PAGE_VALIDATION_ALL
} PGBUF_DEBUG_PAGE_VALIDATION_LEVEL;

/* priority of ordered page fix:
 * this allows us to keep pages with more priority fixed even when VPID order
 * would require to make unfix, reorder and fix in VPID order */
typedef enum
{
  PGBUF_ORDERED_HEAP_HDR = 0,
  PGBUF_ORDERED_HEAP_NORMAL,
  PGBUF_ORDERED_HEAP_OVERFLOW,

  PGBUF_ORDERED_RANK_UNDEFINED,
} PGBUF_ORDERED_RANK;

typedef VPID PGBUF_ORDERED_GROUP;

typedef struct pgbuf_watcher PGBUF_WATCHER;
struct pgbuf_watcher
{
  PAGE_PTR pgptr;
  PGBUF_WATCHER *next;
  PGBUF_WATCHER *prev;
  PGBUF_ORDERED_GROUP group_id;	/* VPID of group (HEAP header) */
  unsigned latch_mode:7;
  unsigned page_was_unfixed:1;	/* set true if any refix occurs in this page */
  unsigned initial_rank:4;	/* rank of page at init (before fix) */
  unsigned curr_rank:4;		/* current rank of page (after fix) */
#if !defined (NDEBUG)
  unsigned int magic;
  char watched_at[128];
  char init_at[256];
#endif
};

extern HFID *pgbuf_ordered_null_hfid;

extern unsigned int pgbuf_hash_vpid (const void *key_vpid, unsigned int htsize);
extern int pgbuf_compare_vpid (const void *key_vpid1, const void *key_vpid2);
extern int pgbuf_initialize (void);
extern void pgbuf_finalize (void);
extern PAGE_PTR pgbuf_fix_with_retry (THREAD_ENTRY * thread_p, const VPID * vpid, PAGE_FETCH_MODE fetch_mode,
				      PGBUF_LATCH_MODE request_mode, int retry);
#if !defined(NDEBUG)
#define pgbuf_flush(thread_p, pgptr, free_page) \
	pgbuf_flush_debug(thread_p, pgptr, free_page, __FILE__, __LINE__)
extern PAGE_PTR pgbuf_flush_debug (THREAD_ENTRY * thread_p, PAGE_PTR pgptr, int free_page, const char *caller_file,
				   int caller_line);

#define pgbuf_fix(thread_p, vpid, fetch_mode, requestmode, condition) \
        pgbuf_fix_debug(thread_p, vpid, fetch_mode, requestmode, condition, \
                        __FILE__, __LINE__)
extern PAGE_PTR pgbuf_fix_debug (THREAD_ENTRY * thread_p, const VPID * vpid, PAGE_FETCH_MODE fetch_mode,
				 PGBUF_LATCH_MODE requestmode, PGBUF_LATCH_CONDITION condition, const char *caller_file,
				 int caller_line);
#define pgbuf_ordered_fix(thread_p, req_vpid, fetch_mode, requestmode,\
			  req_watcher) \
        pgbuf_ordered_fix_debug(thread_p, req_vpid, fetch_mode, requestmode, \
			        req_watcher, __FILE__, __LINE__)

extern int pgbuf_ordered_fix_debug (THREAD_ENTRY * thread_p, const VPID * req_vpid, PAGE_FETCH_MODE fetch_mode,
				    const PGBUF_LATCH_MODE requestmode, PGBUF_WATCHER * req_watcher,
				    const char *caller_file, int caller_line);

#define pgbuf_promote_read_latch(thread_p, pgptr_p, condition) \
	pgbuf_promote_read_latch_debug(thread_p, pgptr_p, condition, \
				       __FILE__, __LINE__)
extern int pgbuf_promote_read_latch_debug (THREAD_ENTRY * thread_p, PAGE_PTR * pgptr_p,
					   PGBUF_PROMOTE_CONDITION condition, const char *caller_file, int caller_line);

#define pgbuf_fix_without_validation(thread_p, vpid, fetch_mode, \
				     requestmode, condition) \
	pgbuf_fix_without_validation_debug(thread_p, vpid, fetch_mode, \
					   requestmode, condition, \
					   __FILE__, __LINE__)
extern PAGE_PTR pgbuf_fix_without_validation_debug (THREAD_ENTRY * thread_p, const VPID * vpid,
						    PAGE_FETCH_MODE fetch_mode, PGBUF_LATCH_MODE request_mode,
						    PGBUF_LATCH_CONDITION condition, const char *caller_file,
						    int caller_line);
#define pgbuf_unfix(thread_p, pgptr) \
	pgbuf_unfix_debug(thread_p, pgptr, __FILE__, __LINE__)
extern void pgbuf_unfix_debug (THREAD_ENTRY * thread_p, PAGE_PTR pgptr, const char *caller_file, int caller_line);

#define pgbuf_ordered_unfix(thread_p, watcher_object) \
	pgbuf_ordered_unfix_debug(thread_p, watcher_object, \
				  __FILE__, __LINE__)
extern void pgbuf_ordered_unfix_debug (THREAD_ENTRY * thread_p, PGBUF_WATCHER * watcher_object, const char *caller_file,
				       int caller_line);

#define pgbuf_invalidate_all(thread_p, volid) \
	pgbuf_invalidate_all_debug(thread_p, volid, __FILE__, __LINE__)
extern int pgbuf_invalidate_all_debug (THREAD_ENTRY * thread_p, VOLID volid, const char *caller_file, int caller_line);

#define pgbuf_invalidate(thread_p, pgptr) \
	pgbuf_invalidate_debug(thread_p, pgptr, __FILE__, __LINE__)
extern int pgbuf_invalidate_debug (THREAD_ENTRY * thread_p, PAGE_PTR pgptr, const char *caller_file, int caller_line);
#else /* NDEBUG */
extern PAGE_PTR pgbuf_flush (THREAD_ENTRY * thread_p, PAGE_PTR pgptr, int free_page);
#define pgbuf_fix_without_validation(thread_p, vpid, fetch_mode, \
				     requestmode, condition) \
	pgbuf_fix_without_validation_release(thread_p, vpid, fetch_mode, \
					     requestmode, condition)
extern PAGE_PTR pgbuf_fix_without_validation_release (THREAD_ENTRY * thread_p, const VPID * vpid,
						      PAGE_FETCH_MODE fetch_mode, PGBUF_LATCH_MODE request_mode,
						      PGBUF_LATCH_CONDITION condition);
#define pgbuf_fix(thread_p, vpid, fetch_mode, requestmode, condition) \
        pgbuf_fix_release(thread_p, vpid, fetch_mode, requestmode, condition)
extern PAGE_PTR pgbuf_fix_release (THREAD_ENTRY * thread_p, const VPID * vpid, PAGE_FETCH_MODE fetch_mode,
				   PGBUF_LATCH_MODE requestmode, PGBUF_LATCH_CONDITION condition);

#define pgbuf_ordered_fix(thread_p, req_vpid, fetch_mode, requestmode, \
			  req_watcher) \
        pgbuf_ordered_fix_release(thread_p, req_vpid, fetch_mode, requestmode, \
				  req_watcher)

extern int pgbuf_ordered_fix_release (THREAD_ENTRY * thread_p, const VPID * req_vpid, PAGE_FETCH_MODE fetch_mode,
				      const PGBUF_LATCH_MODE requestmode, PGBUF_WATCHER * watcher_object);

#define pgbuf_promote_read_latch(thread_p, pgptr_p, condition) \
  pgbuf_promote_read_latch_release(thread_p, pgptr_p, condition)
extern int pgbuf_promote_read_latch_release (THREAD_ENTRY * thread_p, PAGE_PTR * pgptr_p,
					     PGBUF_PROMOTE_CONDITION condition);

extern void pgbuf_unfix (THREAD_ENTRY * thread_p, PAGE_PTR pgptr);

extern void pgbuf_ordered_unfix (THREAD_ENTRY * thread_p, PGBUF_WATCHER * watcher_object);

extern int pgbuf_invalidate_all (THREAD_ENTRY * thread_p, VOLID volid);
extern int pgbuf_invalidate (THREAD_ENTRY * thread_p, PAGE_PTR pgptr);
#endif /* NDEBUG */
extern PAGE_PTR pgbuf_flush_with_wal (THREAD_ENTRY * thread_p, PAGE_PTR pgptr);
#if !defined(NDEBUG)
#define pgbuf_flush_all(thread_p, volid) \
	pgbuf_flush_all_debug(thread_p, volid, __FILE__, __LINE__)
extern int pgbuf_flush_all_debug (THREAD_ENTRY * thread_p, VOLID volid, const char *caller_file, int caller_line);

#define pgbuf_flush_all_unfixed(thread_p, volid) \
	pgbuf_flush_all_unfixed_debug(thread_p, volid, __FILE__, __LINE__)
extern int pgbuf_flush_all_unfixed_debug (THREAD_ENTRY * thread_p, VOLID volid, const char *caller_file,
					  int caller_line);

#define pgbuf_flush_all_unfixed_and_set_lsa_as_null(thread_p, volid) \
	pgbuf_flush_all_unfixed_and_set_lsa_as_null_debug(thread_p, volid, \
							  __FILE__, \
							  __LINE__)
extern int pgbuf_flush_all_unfixed_and_set_lsa_as_null_debug (THREAD_ENTRY * thread_p, VOLID volid,
							      const char *caller_file, int caller_line);

#define pgbuf_flush_victim_candidate(thread_p, flush_ratio) \
	pgbuf_flush_victim_candidate_debug(thread_p, flush_ratio, __FILE__, __LINE__)
extern int pgbuf_flush_victim_candidate_debug (THREAD_ENTRY * thread_p, float flush_ratio, const char *caller_file,
					       int caller_line);

#define pgbuf_flush_checkpoint(thread_p, flush_upto_lsa, prev_chkpt_redo_lsa, smallest_lsa, flushed_page_cnt) \
	pgbuf_flush_checkpoint_debug(thread_p, flush_upto_lsa, prev_chkpt_redo_lsa, smallest_lsa, \
				     flushed_page_cnt, __FILE__, __LINE__)
extern int pgbuf_flush_checkpoint_debug (THREAD_ENTRY * thread_p, const LOG_LSA * flush_upto_lsa,
					 const LOG_LSA * prev_chkpt_redo_lsa, LOG_LSA * smallest_lsa,
					 int *flushed_page_cnt, const char *caller_file, int caller_line);

#define pgbuf_replace_watcher(thread_p, old_watcher, new_watcher) \
  pgbuf_replace_watcher_debug(thread_p, old_watcher, new_watcher, \
			       __FILE__, __LINE__)
extern void pgbuf_replace_watcher_debug (THREAD_ENTRY * thread_p, PGBUF_WATCHER * old_watcher,
					 PGBUF_WATCHER * new_watcher, const char *caller_file, const int caller_line);
#else /* NDEBUG */
extern int pgbuf_flush_all (THREAD_ENTRY * thread_p, VOLID volid);
extern int pgbuf_flush_all_unfixed (THREAD_ENTRY * thread_p, VOLID volid);
extern int pgbuf_flush_all_unfixed_and_set_lsa_as_null (THREAD_ENTRY * thread_p, VOLID volid);
extern int pgbuf_flush_victim_candidate (THREAD_ENTRY * thread_p, float flush_ratio);
extern int pgbuf_flush_checkpoint (THREAD_ENTRY * thread_p, const LOG_LSA * flush_upto_lsa,
				   const LOG_LSA * prev_chkpt_redo_lsa, LOG_LSA * smallest_lsa, int *flushed_page_cnt);

extern void pgbuf_replace_watcher (THREAD_ENTRY * thread_p, PGBUF_WATCHER * old_watcher, PGBUF_WATCHER * new_watcher);
#endif /* NDEBUG */
extern void *pgbuf_copy_to_area (THREAD_ENTRY * thread_p, const VPID * vpid, int start_offset, int length, void *area,
				 bool do_fetch);
extern void *pgbuf_copy_from_area (THREAD_ENTRY * thread_p, const VPID * vpid, int start_offset, int length, void *area,
				   bool do_fetch);

extern void pgbuf_set_dirty (THREAD_ENTRY * thread_p, PAGE_PTR pgptr, int free_page);
#define pgbuf_set_dirty_and_free(thread_p, pgptr) pgbuf_set_dirty (thread_p, pgptr, FREE); pgptr = NULL

extern LOG_LSA *pgbuf_get_lsa (PAGE_PTR pgptr);
extern int pgbuf_page_has_changed (PAGE_PTR pgptr, LOG_LSA * ref_lsa);
extern const LOG_LSA *pgbuf_set_lsa (THREAD_ENTRY * thread_p, PAGE_PTR pgptr, const LOG_LSA * lsa_ptr);
extern void pgbuf_reset_temp_lsa (PAGE_PTR pgptr);
extern void pgbuf_get_vpid (PAGE_PTR pgptr, VPID * vpid);
extern VPID *pgbuf_get_vpid_ptr (PAGE_PTR pgptr);
extern PGBUF_LATCH_MODE pgbuf_get_latch_mode (PAGE_PTR pgptr);
extern PAGEID pgbuf_get_page_id (PAGE_PTR pgptr);
extern PAGE_TYPE pgbuf_get_page_ptype (THREAD_ENTRY * thread_p, PAGE_PTR pgptr);
extern VOLID pgbuf_get_volume_id (PAGE_PTR pgptr);
extern const char *pgbuf_get_volume_label (PAGE_PTR pgptr);
extern void pgbuf_refresh_max_permanent_volume_id (VOLID volid);
extern VOLID pgbuf_get_max_permanent_volume_id (void);
extern void pgbuf_cache_permanent_volume_for_temporary (VOLID volid);
extern void pgbuf_force_to_check_for_interrupts (void);
extern bool pgbuf_is_log_check_for_interrupts (THREAD_ENTRY * thread_p);
extern void pgbuf_unfix_all (THREAD_ENTRY * thread_p);
extern void pgbuf_set_lsa_as_temporary (THREAD_ENTRY * thread_p, PAGE_PTR pgptr);
extern void pgbuf_set_lsa_as_permanent (THREAD_ENTRY * thread_p, PAGE_PTR pgptr);
extern void pgbuf_set_page_ptype (THREAD_ENTRY * thread_p, PAGE_PTR pgptr, PAGE_TYPE ptype);
extern bool pgbuf_is_lsa_temporary (PAGE_PTR pgptr);
extern void pgbuf_invalidate_temporary_file (VOLID volid, PAGEID first_pageid, DKNPAGES npages, bool need_invalidate);
extern bool pgbuf_check_page_ptype (THREAD_ENTRY * thread_p, PAGE_PTR pgptr, PAGE_TYPE ptype);
extern bool pgbuf_check_page_type_no_error (THREAD_ENTRY * thread_p, PAGE_PTR pgptr, PAGE_TYPE ptype);
extern DISK_ISVALID pgbuf_is_valid_page (THREAD_ENTRY * thread_p, const VPID * vpid, bool no_error,
					 DISK_ISVALID (*fun) (const VPID * vpid, void *args), void *args);

#if defined(CUBRID_DEBUG)
extern void pgbuf_dump_if_any_fixed (void);
#endif

extern bool pgbuf_has_perm_pages_fixed (THREAD_ENTRY * thread_p);
extern void pgbuf_ordered_set_dirty_and_free (THREAD_ENTRY * thread_p, PGBUF_WATCHER * pg_watcher);
extern int pgbuf_get_condition_for_ordered_fix (const VPID * vpid_new_page, const VPID * vpid_fixed_page,
						const HFID * hfid);
#if !defined(NDEBUG)
extern void pgbuf_watcher_init_debug (PGBUF_WATCHER * watcher, const char *caller_file, const int caller_line,
				      bool add);
extern bool pgbuf_is_page_fixed_by_thread (THREAD_ENTRY * thread_p, VPID * vpid_p);
#endif

#if !defined (NDEBUG)
#define pgbuf_attach_watcher(...) \
  pgbuf_attach_watcher_debug (__VA_ARGS__, ARG_FILE_LINE)

extern void pgbuf_attach_watcher_debug (THREAD_ENTRY * thread_p, PAGE_PTR pgptr, PGBUF_LATCH_MODE latch_mode,
					HFID * hfid, PGBUF_WATCHER * watcher, const char *file, const int line);
#else /* NDEBUG */
extern void pgbuf_attach_watcher (THREAD_ENTRY * thread_p, PAGE_PTR pgptr, PGBUF_LATCH_MODE latch_mode, HFID * hfid,
				  PGBUF_WATCHER * watcher);
#endif /* NDEBUG */

extern bool pgbuf_has_any_waiters (PAGE_PTR pgptr);
extern bool pgbuf_has_any_non_vacuum_waiters (PAGE_PTR pgptr);
extern bool pgbuf_has_prevent_dealloc (PAGE_PTR pgptr);
extern void pgbuf_peek_stats (UINT64 * fixed_cnt, UINT64 * dirty_cnt, UINT64 * lru1_cnt, UINT64 * lru2_cnt,
			      UINT64 * aint_cnt, UINT64 * avoid_dealloc_cnt, UINT64 * avoid_victim_cnt,
			      UINT64 * victim_cand_cnt);

extern int pgbuf_flush_control_from_dirty_ratio (void);

extern int pgbuf_rv_flush_page (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern void pgbuf_rv_flush_page_dump (FILE * fp, int length, void *data);

extern int pgbuf_get_fix_count (PAGE_PTR pgptr);
extern int pgbuf_get_hold_count (THREAD_ENTRY * thread_p);

extern PERF_PAGE_TYPE pgbuf_get_page_type_for_stat (PAGE_PTR pgptr);

extern void pgbuf_log_new_page (THREAD_ENTRY * thread_p, PAGE_PTR page_new, int data_size, PAGE_TYPE ptype_new);
extern int pgbuf_rv_new_page_redo (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern int pgbuf_rv_new_page_undo (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern int pgbuf_dealloc_page (THREAD_ENTRY * thread_p, PAGE_PTR * page_dealloc);
extern int pgbuf_rv_dealloc_redo (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern int pgbuf_rv_dealloc_undo (THREAD_ENTRY * thread_p, LOG_RCV * rcv);

extern int pgbuf_fix_if_not_deallocated_with_caller (THREAD_ENTRY * thead_p, const VPID * vpid,
						     PGBUF_LATCH_MODE latch_mode, PGBUF_LATCH_CONDITION latch_condition,
						     PAGE_PTR * page, const char *caller_file, int caller_line);
#if defined (NDEBUG)
#define pgbuf_fix_if_not_deallocated(thread_p, vpid, latch_mode, latch_condition, page) \
  pgbuf_fix_if_not_deallocated_with_caller (thread_p, vpid, latch_mode, latch_condition, page, NULL, 0)
#else /* !NDEBUG */
#define pgbuf_fix_if_not_deallocated(thread_p, vpid, latch_mode, latch_condition, page) \
  pgbuf_fix_if_not_deallocated_with_caller (thread_p, vpid, latch_mode, latch_condition, page, ARG_FILE_LINE)
#endif /* !NDEBUG */

#endif /* _PAGE_BUFFER_H_ */
