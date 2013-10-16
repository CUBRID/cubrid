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
 * heap_file.c - heap file manager
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "porting.h"
#include "heap_file.h"
#include "storage_common.h"
#include "memory_alloc.h"
#include "system_parameter.h"
#include "oid.h"
#include "error_manager.h"
#include "locator.h"
#include "file_io.h"
#include "page_buffer.h"
#include "file_manager.h"
#include "disk_manager.h"
#include "slotted_page.h"
#include "overflow_file.h"
#include "object_representation.h"
#include "object_representation_sr.h"
#include "log_manager.h"
#include "lock_manager.h"
#include "memory_hash.h"
#include "critical_section.h"
#include "boot_sr.h"
#include "locator_sr.h"
#include "btree.h"
#include "thread.h"		/* MAX_NTHRDS */
#include "transform.h"		/* for CT_SERIAL_NAME */
#include "serial.h"
#include "object_primitive.h"
#include "dbtype.h"
#include "db.h"
#include "object_print.h"
#include "xserver_interface.h"
#include "boot_sr.h"
#include "chartype.h"
#include "query_executor.h"
#include "fetch.h"
#include "server_interface.h"

/* For getting and dumping attributes */
#include "language_support.h"

/* For creating multi-column sequence keys (sets) */
#include "set_object.h"

#ifdef SERVER_MODE
#include "connection_error.h"
#endif

/* this must be the last header file included!!! */
#include "dbval.h"

#if !defined(SERVER_MODE)
#define pthread_mutex_init(a, b)
#define pthread_mutex_destroy(a)
#define pthread_mutex_lock(a)   0
#define pthread_mutex_trylock(a)   0
#define pthread_mutex_unlock(a)
static int rv;
#endif /* not SERVER_MODE */


/* ATTRIBUTE LOCATION */

#define OR_FIXED_ATTRIBUTES_OFFSET_BY_OBJ(obj, nvars) \
  (OR_HEADER_SIZE + OR_VAR_TABLE_SIZE_INTERNAL(nvars, OR_GET_OFFSET_SIZE(obj)))

#define HEAP_GUESS_NUM_ATTRS_REFOIDS 100
#define HEAP_GUESS_NUM_INDEXED_ATTRS 100

#define HEAP_CLASSREPR_MAXCACHE	100

#define HEAP_STATS_ENTRY_MHT_EST_SIZE 1000
#define HEAP_STATS_ENTRY_FREELIST_SIZE 1000

/* A good space to accept insertions */
#define HEAP_DROP_FREE_SPACE (int)(DB_PAGESIZE * 0.3)

#define HEAP_DEBUG_SCANCACHE_INITPATTERN (12345)

#define HEAP_ISJUNK_OID(oid) \
  ((oid)->slotid == HEAP_HEADER_AND_CHAIN_SLOTID || \
   (oid)->slotid < 0 || (oid)->volid < 0 || (oid)->pageid < 0)

#if defined(CUBRID_DEBUG)
#define HEAP_ISVALID_OID(oid) \
  (HEAP_ISJUNK_OID(oid)       \
   ? DISK_INVALID             \
   : disk_isvalid_page((oid)->volid, (oid)->pageid))

#define HEAP_DEBUG_ISVALID_SCANRANGE(scan_range) \
  heap_scanrange_isvalid(scan_range)

#else /* CUBRID_DEBUG */
#define HEAP_ISVALID_OID(oid) \
  (HEAP_ISJUNK_OID(oid)       \
   ? DISK_INVALID             \
   : DISK_VALID)

#define HEAP_DEBUG_ISVALID_SCANRANGE(scan_range) (DISK_VALID)

#endif /* !CUBRID_DEBUG */

typedef enum
{
  HEAP_FINDSPACE_FOUND,
  HEAP_FINDSPACE_NOTFOUND,
  HEAP_FINDSPACE_ERROR
} HEAP_FINDSPACE;

/*
 * Prefetching directions
 */

typedef enum
{
  HEAP_DIRECTION_NONE,		/* No prefetching           */
  HEAP_DIRECTION_LEFT,		/* Prefetching at the left  */
  HEAP_DIRECTION_RIGHT,		/* Prefetching at the right */
  HEAP_DIRECTION_BOTH		/* Prefetching at both directions.. left and right */
} HEAP_DIRECTION;

/*
 * Heap file header
 */

#define HEAP_NUM_BEST_SPACESTATS   10

/* calculate an index of best array */
#define HEAP_STATS_NEXT_BEST_INDEX(i)   \
  (((i) + 1) % HEAP_NUM_BEST_SPACESTATS)
#define HEAP_STATS_PREV_BEST_INDEX(i)   \
  (((i) == 0) ? (HEAP_NUM_BEST_SPACESTATS - 1) : ((i) - 1));

typedef struct heap_hdr_stats HEAP_HDR_STATS;
struct heap_hdr_stats
{
  /* the first must be class_oid */
  OID class_oid;
  VFID ovf_vfid;		/* Overflow file identifier (if any)      */
  VPID next_vpid;		/* Next page (i.e., the 2nd page of heap
				 * file)
				 */
  int unfill_space;		/* Stop inserting when page has run below
				 * this. leave it for updates
				 */
  struct
  {
    int num_pages;		/* Estimation of number of heap pages.
				 * Consult file manager if accurate number is
				 * needed
				 */
    int num_recs;		/* Estimation of number of objects in heap */
    float recs_sumlen;		/* Estimation total length of records      */
    int num_other_high_best;	/* Total of other believed known best pages,
				 * which are not included in the best array
				 * and we believe they have at least
				 * HEAP_DROP_FREE_SPACE
				 */
    int num_high_best;		/* Number of pages in the best array that we
				 * believe have at least HEAP_DROP_FREE_SPACE.
				 * When this number goes to zero and there is
				 * at least other HEAP_NUM_BEST_SPACESTATS best
				 * pages, we look for them.
				 */
    int num_substitutions;	/* Number of page substitutions. This will be
				 * used to insert a new second best page into
				 * second best hints.
				 */
    int num_second_best;	/* Number of second best hints. The hints are
				 * in "second_best" array. They are used when
				 * finding new best pages. See the function
				 * "heap_stats_sync_bestspace".
				 */
    int head_second_best;	/* Index of head of second best hints. */
    int tail_second_best;	/* Index of tail of second best hints.
				 * A new second best hint will be stored
				 * on this index.
				 */
    int head;			/* Head of best circular array             */
    VPID last_vpid;
    VPID full_search_vpid;
    VPID second_best[HEAP_NUM_BEST_SPACESTATS];
    HEAP_BESTSPACE best[HEAP_NUM_BEST_SPACESTATS];
  } estimates;			/* Probably, the set of pages with more free
				 * space on the heap. Changes to any values
				 * of this array (either page or the free
				 * space for the page) are not logged since
				 * these values are only used for hints.
				 * These values may not be accurate at any
				 * given time and the entries may contain
				 * duplicated pages.
				 */
  int reserve1_for_future;	/* Nothing reserved for future             */
  int reserve2_for_future;	/* Nothing reserved for future             */
};

typedef struct heap_stats_entry HEAP_STATS_ENTRY;
struct heap_stats_entry
{
  HFID hfid;			/* heap file identifier */
  HEAP_BESTSPACE best;		/* best space info */
  HEAP_STATS_ENTRY *next;
};

typedef struct heap_chain HEAP_CHAIN;
struct heap_chain
{				/* Double-linked */
  /* the first must be class_oid */
  OID class_oid;
  VPID prev_vpid;		/* Previous page */
  VPID next_vpid;		/* Next page     */
};

typedef struct heap_chain_tolast HEAP_CHAIN_TOLAST;
struct heap_chain_tolast
{
  PAGE_PTR hdr_pgptr;
  PAGE_PTR last_pgptr;
  HEAP_HDR_STATS *heap_hdr;
};

#define HEAP_CHK_ADD_UNFOUND_RELOCOIDS 100

typedef struct heap_chk_relocoid HEAP_CHK_RELOCOID;
struct heap_chk_relocoid
{
  OID real_oid;
  OID reloc_oid;
};

typedef struct heap_chkall_relocoids HEAP_CHKALL_RELOCOIDS;
struct heap_chkall_relocoids
{
  MHT_TABLE *ht;		/* Hash table to be used to keep relocated records
				 * The key of hash table is the relocation OID, the date
				 * is the real OID
				 */
  bool verify;
  int max_unfound_reloc;
  int num_unfound_reloc;
  OID *unfound_reloc_oids;	/* The relocation OIDs that have not been
				 * found in hash table
				 */
};

#define DEFAULT_REPR_INCREMENT 16

enum
{ ZONE_VOID = 1, ZONE_FREE = 2, ZONE_LRU = 3 };

typedef struct heap_classrepr_entry HEAP_CLASSREPR_ENTRY;
struct heap_classrepr_entry
{
  pthread_mutex_t mutex;
  int idx;			/* Cache index. Used to pass the index when
				 * a class representation is in the cache */
  int fcnt;			/* How many times this structure has been
				 * fixed. It cannot be deallocated until this
				 * value is zero.  */
  int zone;			/* ZONE_VOID, ZONE_LRU, ZONE_FREE */
  int force_decache;

  THREAD_ENTRY *next_wait_thrd;
  HEAP_CLASSREPR_ENTRY *hash_next;
  HEAP_CLASSREPR_ENTRY *prev;	/* prev. entry in LRU list */
  HEAP_CLASSREPR_ENTRY *next;	/* prev. entry in LRU or free list */

  /* real data */
  OID class_oid;		/* Identifier of the class representation */

  OR_CLASSREP **repr;		/* A particular representation of the class */
  int max_reprid;
  REPR_ID last_reprid;
};

typedef struct heap_classrepr_lock HEAP_CLASSREPR_LOCK;
struct heap_classrepr_lock
{
  OID class_oid;
  HEAP_CLASSREPR_LOCK *lock_next;
  THREAD_ENTRY *next_wait_thrd;
};

typedef struct heap_classrepr_hash HEAP_CLASSREPR_HASH;
struct heap_classrepr_hash
{
  pthread_mutex_t hash_mutex;
  int idx;
  HEAP_CLASSREPR_ENTRY *hash_next;
  HEAP_CLASSREPR_LOCK *lock_next;
};

typedef struct heap_classrepr_LRU_list HEAP_CLASSREPR_LRU_LIST;
struct heap_classrepr_LRU_list
{
  pthread_mutex_t LRU_mutex;
  HEAP_CLASSREPR_ENTRY *LRU_top;
  HEAP_CLASSREPR_ENTRY *LRU_bottom;
};

typedef struct heap_classrepr_free_list HEAP_CLASSREPR_FREE_LIST;
struct heap_classrepr_free_list
{
  pthread_mutex_t free_mutex;
  HEAP_CLASSREPR_ENTRY *free_top;
  int free_cnt;
};

typedef struct heap_classrepr_cache HEAP_CLASSREPR_CACHE;
struct heap_classrepr_cache
{
  int num_entries;
  HEAP_CLASSREPR_ENTRY *area;
  int num_hash;
  HEAP_CLASSREPR_HASH *hash_table;
  HEAP_CLASSREPR_LOCK *lock_table;
  HEAP_CLASSREPR_LRU_LIST LRU_list;
  HEAP_CLASSREPR_FREE_LIST free_list;
  HFID *rootclass_hfid;
#ifdef DEBUG_CLASSREPR_CACHE
  int num_fix_entries;
  pthread_mutex_t num_fix_entries_mutex;
#endif				/* DEBUG_CLASSREPR_CACHE */
};

static HEAP_CLASSREPR_CACHE heap_Classrepr_cache = {
  -1,
  NULL,
  -1,
  NULL,
  NULL,
  {
   PTHREAD_MUTEX_INITIALIZER,
   NULL,
   NULL},
  {
   PTHREAD_MUTEX_INITIALIZER,
   NULL,
   -1},
  NULL
#ifdef DEBUG_CLASSREPR_CACHE
    , 0, PTHREAD_MUTEX_INITIALIZER
#endif /* DEBUG_CLASSREPR_CACHE */
};

#define CLASSREPR_REPR_INCREMENT	10
#define CLASSREPR_HASH_SIZE  (heap_Classrepr_cache.num_entries * 2)
#define REPR_HASH(class_oid) (OID_PSEUDO_KEY(class_oid)%CLASSREPR_HASH_SIZE)

#define HEAP_MAYNEED_DECACHE_GUESSED_LASTREPRS(class_oid, hfid, recdes) \
  do {                                                        \
    if (heap_Classrepr != NULL && (hfid) != NULL) {             \
      if (heap_Classrepr->rootclass_hfid == NULL)               \
	heap_Classrepr->rootclass_hfid = boot_find_root_heap();   \
      if (HFID_EQ((hfid), heap_Classrepr->rootclass_hfid))      \
	(void) heap_classrepr_decache_guessed_last(class_oid);  \
    }                                                         \
  } while (0)

#define HEAP_CHNGUESS_FUDGE_MININDICES (100)
#define HEAP_NBITS_IN_BYTE	     (8)
#define HEAP_NSHIFTS                   (3)	/* For multiplication/division by 8 */
#define HEAP_BITMASK                   (HEAP_NBITS_IN_BYTE - 1)
#define HEAP_NBITS_TO_NBYTES(bit_cnt)  \
  ((unsigned int)((bit_cnt) + HEAP_BITMASK) >> HEAP_NSHIFTS)
#define HEAP_NBYTES_TO_NBITS(byte_cnt) ((unsigned int)(byte_cnt) << HEAP_NSHIFTS)
#define HEAP_NBYTES_CLEARED(byte_ptr, byte_cnt) \
  memset((byte_ptr), '\0', (byte_cnt))
#define HEAP_BYTEOFFSET_OFBIT(bit_num) ((unsigned int)(bit_num) >> HEAP_NSHIFTS)
#define HEAP_BYTEGET(byte_ptr, bit_num) \
  ((unsigned char *)(byte_ptr) + HEAP_BYTEOFFSET_OFBIT(bit_num))

#define HEAP_BITMASK_INBYTE(bit_num)   \
  (1 << ((unsigned int)(bit_num) & HEAP_BITMASK))
#define HEAP_BIT_GET(byte_ptr, bit_num) \
  (*HEAP_BYTEGET(byte_ptr, bit_num) & HEAP_BITMASK_INBYTE(bit_num))
#define HEAP_BIT_SET(byte_ptr, bit_num) \
  (*HEAP_BYTEGET(byte_ptr, bit_num) = \
   *HEAP_BYTEGET(byte_ptr, bit_num) | HEAP_BITMASK_INBYTE(bit_num))
#define HEAP_BIT_CLEAR(byte_ptr, bit_num) \
  (*HEAP_BYTEGET(byte_ptr, bit_num) = \
   *HEAP_BYTEGET(byte_ptr, bit_num) & ~HEAP_BITMASK_INBYTE(bit_num))

typedef struct heap_chnguess_entry HEAP_CHNGUESS_ENTRY;
struct heap_chnguess_entry
{				/* Currently, only classes are cached */
  int idx;			/* Index number of this entry       */
  int chn;			/* Cache coherence number of object */
  bool recently_accessed;	/* Reference value 0/1 used by replacement
				 * clock algorithm
				 */
  OID oid;			/* Identifier of object             */
  unsigned char *bits;		/* Bit index array describing client
				 * transaction indices.
				 * Bit n corresponds to client tran index n
				 * If Bit is ON, we guess that the object is
				 * cached in the workspace of the client.
				 */
};

typedef struct heap_chnguess HEAP_CHNGUESS;
struct heap_chnguess
{
  MHT_TABLE *ht;		/* Hash table for guessing chn           */
  HEAP_CHNGUESS_ENTRY *entries;	/* Pointers to entry structures. More
				 * than one entry
				 */
  unsigned char *bitindex;	/* Bit index array for each entry.
				 * Describe all entries. Each entry is
				 * subdivided into nbytes.
				 */
  bool schema_change;		/* Has the schema been changed           */
  int clock_hand;		/* Clock hand for replacement            */
  int num_entries;		/* Number of guesschn entries            */
  int num_clients;		/* Number of clients in bitindex for each
				 * entry
				 */
  int nbytes;			/* Number of bytes in bitindex. It must
				 * be aligned to multiples of 4 bytes
				 * (integers)
				 */
};

typedef struct heap_stats_bestspace_cache HEAP_STATS_BESTSPACE_CACHE;
struct heap_stats_bestspace_cache
{
  int num_stats_entries;	/* number of cache entries in use */
  MHT_TABLE *hfid_ht;		/* HFID Hash table for best space */
  MHT_TABLE *vpid_ht;		/* VPID Hash table for best space */
  int num_alloc;
  int num_free;
  int free_list_count;		/* number of entries in free */
  HEAP_STATS_ENTRY *free_list;
  pthread_mutex_t bestspace_mutex;
};

static int heap_Maxslotted_reclength;
static int heap_Slotted_overhead = 12;	/* sizeof (SPAGE_SLOT) */
static const int heap_Find_best_page_limit = 100;

static HEAP_CLASSREPR_CACHE *heap_Classrepr = NULL;
static HEAP_CHNGUESS heap_Guesschn_area = { NULL, NULL, NULL, false, 0,
  0, 0, 0
};

static HEAP_CHNGUESS *heap_Guesschn = NULL;

static HEAP_STATS_BESTSPACE_CACHE heap_Bestspace_cache_area =
  { 0, NULL, NULL, 0, 0, 0, NULL, PTHREAD_MUTEX_INITIALIZER };

static HEAP_STATS_BESTSPACE_CACHE *heap_Bestspace = NULL;

static bool heap_is_big_length (int length);
static int heap_scancache_update_hinted_when_lots_space (THREAD_ENTRY *
							 thread_p,
							 HEAP_SCANCACHE *,
							 PAGE_PTR);

static int heap_classrepr_initialize_cache (void);
static int heap_classrepr_finalize_cache (void);
static int heap_classrepr_decache_guessed_last (const OID * class_oid);
#ifdef SERVER_MODE
static int heap_classrepr_lock_class (THREAD_ENTRY * thread_p,
				      HEAP_CLASSREPR_HASH * hash_anchor,
				      OID * class_oid);
static int heap_classrepr_unlock_class (HEAP_CLASSREPR_HASH * hash_anchor,
					OID * class_oid, int need_hash_mutex);
#endif

static int heap_classrepr_find_index_id (OR_CLASSREP * classrepr,
					 BTID * btid);
static int heap_classrepr_dump (THREAD_ENTRY * thread_p, FILE * fp,
				const OID * class_oid,
				const OR_CLASSREP * repr);
#ifdef DEBUG_CLASSREPR_CACHE
static int heap_classrepr_dump_cache (bool simple_dump);
#endif /* DEBUG_CLASSREPR_CACHE */

static int heap_classrepr_entry_reset (HEAP_CLASSREPR_ENTRY * cache_entry);
static int heap_classrepr_entry_remove_from_LRU (HEAP_CLASSREPR_ENTRY *
						 cache_entry);
static HEAP_CLASSREPR_ENTRY *heap_classrepr_entry_alloc (void);
static int heap_classrepr_entry_free (HEAP_CLASSREPR_ENTRY * cache_entry);

static int heap_stats_get_min_freespace (HEAP_HDR_STATS * heap_hdr);
static void heap_stats_update (THREAD_ENTRY * thread_p,
			       PAGE_PTR pgptr, const HFID * hfid,
			       FILE_IS_NEW_FILE is_new_file,
			       int prev_freespace);
static int heap_stats_update_internal (THREAD_ENTRY * thread_p,
				       const HFID * hfid,
				       VPID * lotspace_vpid, int free_space);
static int heap_stats_update_all (THREAD_ENTRY * thread_p, const HFID * hfid,
				  int num_best, HEAP_BESTSPACE * bestspace,
				  int num_other_best, int num_pages,
				  int num_recs, float recs_sumlen);
static void heap_stats_put_second_best (HEAP_HDR_STATS * heap_hdr,
					VPID * vpid);
static int heap_stats_get_second_best (HEAP_HDR_STATS * heap_hdr,
				       VPID * vpid);
static int heap_stats_copy_hdr_to_cache (HEAP_HDR_STATS * heap_hdr,
					 HEAP_SCANCACHE * space_cache);
static int heap_stats_copy_cache_to_hdr (THREAD_ENTRY * thread_p,
					 HEAP_HDR_STATS * heap_hdr,
					 HEAP_SCANCACHE * space_cache);
#if defined(ENABLE_UNUSED_FUNCTION)
static int heap_stats_quick_num_fit_in_bestspace (HEAP_BESTSPACE * bestspace,
						  int num_entries,
						  int unit_size,
						  int unfill_space);
#endif
static HEAP_FINDSPACE heap_stats_find_page_in_bestspace (THREAD_ENTRY *
							 thread_p,
							 const HFID * hfid,
							 HEAP_BESTSPACE *
							 bestspace,
							 int num_entries,
							 int *idx_badspace,
							 int *num_high_best,
							 int *idx_found,
							 int needed_space,
							 HEAP_SCANCACHE *
							 scan_cache,
							 PAGE_PTR * pgptr);
static PAGE_PTR heap_stats_find_best_page (THREAD_ENTRY * thread_p,
					   const HFID * hfid,
					   int needed_space, bool isnew_rec,
					   int newrec_size,
					   HEAP_SCANCACHE * space_cache);
static int heap_stats_sync_bestspace (THREAD_ENTRY * thread_p,
				      const HFID * hfid,
				      HEAP_HDR_STATS * heap_hdr,
				      VPID * hdr_vpid, bool scan_all,
				      bool can_cycle);

static int heap_get_best_estimates_stats (THREAD_ENTRY *
					  thread_p,
					  const HFID * hfid,
					  int *num_best,
					  int *num_other_best, int *num_recs);
static PAGE_PTR heap_get_last_page (THREAD_ENTRY * thread_p,
				    const HFID * hfid,
				    HEAP_HDR_STATS * heap_hdr,
				    HEAP_SCANCACHE * scan_cache,
				    VPID * last_vpid);
static bool heap_link_to_new (THREAD_ENTRY * thread_p, const VFID * vfid,
			      const VPID * new_vpid,
			      HEAP_CHAIN_TOLAST * link);

static bool heap_vpid_init_new (THREAD_ENTRY * thread_p, const VFID * vfid,
				const FILE_TYPE file_type, const VPID * vpid,
				INT32 ignore_npages, void *xchain);
static bool heap_vpid_init_newset (THREAD_ENTRY * thread_p, const VFID * vfid,
				   const FILE_TYPE file_type,
				   const VPID * first_alloc_vpid,
				   const INT32 * first_alloc_nth,
				   INT32 npages, void *xchain);
#if defined(ENABLE_UNUSED_FUNCTION)
static PAGE_PTR heap_vpid_prealloc_set (THREAD_ENTRY * thread_p,
					const HFID * hfid, PAGE_PTR hdr_pgptr,
					HEAP_HDR_STATS * heap_hdr, int npages,
					HEAP_SCANCACHE * scan_cache);
#endif
static PAGE_PTR heap_vpid_alloc (THREAD_ENTRY * thread_p, const HFID * hfid,
				 PAGE_PTR hdr_pgptr,
				 HEAP_HDR_STATS * heap_hdr, int needed_space,
				 HEAP_SCANCACHE * scan_cache,
				 FILE_IS_NEW_FILE is_new_file);
static VPID *heap_vpid_remove (THREAD_ENTRY * thread_p, const HFID * hfid,
			       HEAP_HDR_STATS * heap_hdr, VPID * rm_vpid);
static int heap_vpid_next (const HFID * hfid, PAGE_PTR pgptr,
			   VPID * next_vpid);
static int heap_vpid_prev (const HFID * hfid, PAGE_PTR pgptr,
			   VPID * prev_vpid);

static HFID *heap_create_internal (THREAD_ENTRY * thread_p, HFID * hfid,
				   int exp_npgs, const OID * class_oid,
				   const bool reuse_oid);
static const HFID *heap_reuse (THREAD_ENTRY * thread_p, const HFID * hfid,
			       const OID * class_oid, const bool reuse_oid);
static bool heap_delete_all_page_records (THREAD_ENTRY * thread_p,
					  const VPID * vpid, PAGE_PTR pgptr);
static int heap_reinitialize_page (THREAD_ENTRY * thread_p, PAGE_PTR pgptr,
				   const bool is_header_page);
#if defined(CUBRID_DEBUG)
static DISK_ISVALID heap_hfid_isvalid (HFID * hfid);
static DISK_ISVALID heap_scanrange_isvalid (HEAP_SCANRANGE * scan_range);
#endif /* CUBRID_DEBUG */
static int heap_insert_internal (THREAD_ENTRY * thread_p, const HFID * hfid,
				 OID * oid, RECDES * recdes,
				 HEAP_SCANCACHE * scan_cache,
				 bool ishome_insert, int guess_sumlen);
static PAGE_PTR heap_find_slot_for_insert_with_lock (THREAD_ENTRY * thread_p,
						     const HFID * hfid,
						     HEAP_SCANCACHE *
						     scan_cache, OID * oid,
						     RECDES * recdes,
						     OID * class_oid);
static int heap_insert_with_lock_internal (THREAD_ENTRY * thread_p,
					   const HFID * hfid, OID * oid,
					   OID * class_oid, RECDES * recdes,
					   HEAP_SCANCACHE * scan_cache,
					   bool ishome_insert,
					   int guess_sumlen);
static const OID *heap_delete_internal (THREAD_ENTRY * thread_p,
					const HFID * hfid, const OID * oid,
					HEAP_SCANCACHE * scan_cache,
					bool ishome_delete);

static VFID *heap_ovf_find_vfid (THREAD_ENTRY * thread_p, const HFID * hfid,
				 VFID * ovf_vfid, bool create);
static OID *heap_ovf_insert (THREAD_ENTRY * thread_p, const HFID * hfid,
			     OID * ovf_oid, RECDES * recdes);
static const OID *heap_ovf_update (THREAD_ENTRY * thread_p, const HFID * hfid,
				   const OID * ovf_oid, RECDES * recdes);
static const OID *heap_ovf_delete (THREAD_ENTRY * thread_p, const HFID * hfid,
				   const OID * ovf_oid);
static int heap_ovf_flush (THREAD_ENTRY * thread_p, const OID * ovf_oid);
static int heap_ovf_get_length (THREAD_ENTRY * thread_p, const OID * ovf_oid);
static SCAN_CODE heap_ovf_get (THREAD_ENTRY * thread_p, const OID * ovf_oid,
			       RECDES * recdes, int chn);
static int heap_ovf_get_capacity (THREAD_ENTRY * thread_p,
				  const OID * ovf_oid, int *ovf_len,
				  int *ovf_num_pages, int *ovf_overhead,
				  int *ovf_free_space);

static int heap_scancache_start_internal (THREAD_ENTRY * thread_p,
					  HEAP_SCANCACHE * scan_cache,
					  const HFID * hfid,
					  const OID * class_oid,
					  int cache_last_fix_page,
					  int is_queryscan, int is_indexscan,
					  int lock_hint);
static int heap_scancache_force_modify (THREAD_ENTRY * thread_p,
					HEAP_SCANCACHE * scan_cache);
static int heap_scancache_reset_modify (THREAD_ENTRY * thread_p,
					HEAP_SCANCACHE * scan_cache,
					const HFID * hfid,
					const OID * class_oid);
static int heap_scancache_quick_start_internal (HEAP_SCANCACHE * scan_cache);
static int heap_scancache_quick_end (THREAD_ENTRY * thread_p,
				     HEAP_SCANCACHE * scan_cache);
static int heap_scancache_end_internal (THREAD_ENTRY * thread_p,
					HEAP_SCANCACHE * scan_cache,
					bool scan_state);

static SCAN_CODE heap_get_if_diff_chn (PAGE_PTR pgptr, INT16 slotid,
				       RECDES * recdes, int ispeeking,
				       int chn);
static SCAN_CODE heap_get_internal (THREAD_ENTRY * thread_p, OID * class_oid,
				    const OID * oid, RECDES * recdes,
				    HEAP_SCANCACHE * scan_cache,
				    int ispeeking, int chn);
static int heap_estimate_avg_length (THREAD_ENTRY * thread_p,
				     const HFID * hfid);
static int heap_get_capacity (THREAD_ENTRY * thread_p, const HFID * hfid,
			      int *num_recs, int *num_recs_relocated,
			      int *num_recs_inovf, int *num_pages,
			      int *avg_freespace, int *avg_freespace_nolast,
			      int *avg_reclength, int *avg_overhead);
#if 0				/* TODO: remove unused */
static int heap_moreattr_attrinfo (int attrid,
				   HEAP_CACHE_ATTRINFO * attr_info);
#endif

static int heap_attrinfo_recache_attrepr (HEAP_CACHE_ATTRINFO * attr_info,
					  int islast_reset);
static int heap_attrinfo_recache (THREAD_ENTRY * thread_p, REPR_ID reprid,
				  HEAP_CACHE_ATTRINFO * attr_info);
static int heap_attrinfo_check (const OID * inst_oid,
				HEAP_CACHE_ATTRINFO * attr_info);
static int heap_attrinfo_set_uninitialized (THREAD_ENTRY * thread_p,
					    OID * inst_oid, RECDES * recdes,
					    HEAP_CACHE_ATTRINFO * attr_info);
static int heap_attrinfo_start_refoids (THREAD_ENTRY * thread_p,
					OID * class_oid,
					HEAP_CACHE_ATTRINFO * attr_info);
static int heap_attrinfo_get_disksize (HEAP_CACHE_ATTRINFO * attr_info,
				       int *offset_size_ptr);

static int heap_attrvalue_read (RECDES * recdes, HEAP_ATTRVALUE * value,
				HEAP_CACHE_ATTRINFO * attr_info);

static int heap_midxkey_get_value (RECDES * recdes, OR_ATTRIBUTE * att,
				   DB_VALUE * value,
				   HEAP_CACHE_ATTRINFO * attr_info);
static OR_ATTRIBUTE *heap_locate_attribute (ATTR_ID attrid,
					    HEAP_CACHE_ATTRINFO * attr_info);

static DB_MIDXKEY *heap_midxkey_key_get (RECDES * recdes,
					 DB_MIDXKEY * midxkey,
					 OR_INDEX * index,
					 HEAP_CACHE_ATTRINFO * attrinfo,
					 DB_VALUE * func_res);
static DB_MIDXKEY *heap_midxkey_key_generate (THREAD_ENTRY * thread_p,
					      RECDES * recdes,
					      DB_MIDXKEY * midxkey,
					      int *att_ids,
					      HEAP_CACHE_ATTRINFO * attrinfo,
					      DB_VALUE * func_res,
					      int func_col_id,
					      int func_attr_index_start);

static int heap_dump_hdr (FILE * fp, HEAP_HDR_STATS * heap_hdr);

static int heap_eval_function_index (THREAD_ENTRY * thread_p,
				     FUNCTION_INDEX_INFO * func_index_info,
				     int n_atts, int *att_ids,
				     HEAP_CACHE_ATTRINFO * attr_info,
				     RECDES * recdes, int btid_index,
				     DB_VALUE * result,
				     FUNC_PRED_UNPACK_INFO * func_pred);

static DISK_ISVALID heap_chkreloc_start (HEAP_CHKALL_RELOCOIDS * chk);
static DISK_ISVALID heap_chkreloc_end (HEAP_CHKALL_RELOCOIDS * chk);
static int heap_chkreloc_print_notfound (const void *ignore_reloc_oid,
					 void *ent, void *xchk);
static DISK_ISVALID heap_chkreloc_next (HEAP_CHKALL_RELOCOIDS * chk,
					PAGE_PTR pgptr);

static int heap_chnguess_initialize (void);
static int heap_chnguess_realloc (void);
static int heap_chnguess_finalize (void);
static int heap_chnguess_decache (const OID * oid);
static int heap_chnguess_remove_entry (const void *oid_key, void *ent,
				       void *xignore);

static int heap_stats_bestspace_initialize (void);
static int heap_stats_bestspace_finalize (void);

static int heap_get_spage_type (void);
static bool heap_is_reusable_oid (const FILE_TYPE file_type);
static int heap_rv_redo_newpage_internal (THREAD_ENTRY * thread_p,
					  LOG_RCV * rcv,
					  const bool reuse_oid);

static SCAN_CODE
heap_attrinfo_transform_to_disk_internal (THREAD_ENTRY * thread_p,
					  HEAP_CACHE_ATTRINFO * attr_info,
					  RECDES * old_recdes,
					  RECDES * new_recdes,
					  int lob_create_flag);

static int heap_get_partitions_from_subclasses (THREAD_ENTRY * thread_p,
						const OID * subclasses,
						const OID * class_part_oid,
						const OID * _db_part_oid,
						int *parts_count,
						OR_PARTITION * partitions);
static int heap_class_get_partition_info (THREAD_ENTRY * thread_p,
					  const OID * class_oid,
					  OID * partition_info,
					  HFID * class_hfid,
					  REPR_ID * repr_id);
static int heap_get_partition_attributes (THREAD_ENTRY * thread_p,
					  const OID * cls_oid,
					  ATTR_ID * type_id,
					  ATTR_ID * values_id);
static int heap_get_class_subclasses (THREAD_ENTRY * thread_p,
				      const OID * class_oid, int *count,
				      OID ** subclasses);

static int heap_read_or_partition (THREAD_ENTRY * thread_p,
				   HEAP_CACHE_ATTRINFO * attr_info,
				   HEAP_SCANCACHE * scan_cache,
				   ATTR_ID type_id, ATTR_ID values_id,
				   OR_PARTITION * or_part);
static HEAP_STATS_ENTRY *heap_stats_add_bestspace (THREAD_ENTRY * thread_p,
						   const HFID * hfid,
						   VPID * vpid,
						   int freespace);

static void heap_stats_dump_bestspace_information (FILE * out_fp);
static int heap_stats_print_hash_entry (FILE * outfp, const void *key,
					void *ent, void *ignore);
static unsigned int heap_hash_vpid (const void *key_vpid,
				    unsigned int htsize);
static int heap_compare_vpid (const void *key_vpid1, const void *key_vpid2);
static unsigned int heap_hash_hfid (const void *key_hfid,
				    unsigned int htsize);
static int heap_compare_hfid (const void *key_hfid1, const void *key_hfid2);


/*
 * heap_hash_vpid () - Hash a page identifier
 *   return: hash value
 *   key_vpid(in): VPID to hash
 *   htsize(in): Size of hash table
 */
static unsigned int
heap_hash_vpid (const void *key_vpid, unsigned int htsize)
{
  const VPID *vpid = (VPID *) key_vpid;

  return ((vpid->pageid | ((unsigned int) vpid->volid) << 24) % htsize);
}

/*
 * heap_compare_vpid () - Compare two vpids keys for hashing
 *   return: int (key_vpid1 == key_vpid2 ?)
 *   key_vpid1(in): First key
 *   key_vpid2(in): Second key
 */
static int
heap_compare_vpid (const void *key_vpid1, const void *key_vpid2)
{
  const VPID *vpid1 = (VPID *) key_vpid1;
  const VPID *vpid2 = (VPID *) key_vpid2;

  return VPID_EQ (vpid1, vpid2);
}

/*
 * heap_hash_hfid () - Hash a file identifier
 *   return: hash value
 *   key_hfid(in): HFID to hash
 *   htsize(in): Size of hash table
 */
static unsigned int
heap_hash_hfid (const void *key_hfid, unsigned int htsize)
{
  const HFID *hfid = (HFID *) key_hfid;

  return ((hfid->hpgid | ((unsigned int) hfid->vfid.volid) << 24) % htsize);
}

/*
 * heap_compare_hfid () - Compare two hfids keys for hashing
 *   return: int (key_hfid1 == key_hfid2 ?)
 *   key_hfid1(in): First key
 *   key_hfid2(in): Second key
 */
static int
heap_compare_hfid (const void *key_hfid1, const void *key_hfid2)
{
  const HFID *hfid1 = (HFID *) key_hfid1;
  const HFID *hfid2 = (HFID *) key_hfid2;

  return HFID_EQ (hfid1, hfid2);
}

/*
 * heap_stats_dump_bestspace_information () -
 *   return:
 *
 *     out_fp(in/out):
 */
static void
heap_stats_dump_bestspace_information (FILE * out_fp)
{
  int rc;

  rc = pthread_mutex_lock (&heap_Bestspace->bestspace_mutex);

  mht_dump (out_fp, heap_Bestspace->hfid_ht, false,
	    heap_stats_print_hash_entry, NULL);
  mht_dump (out_fp, heap_Bestspace->vpid_ht, false,
	    heap_stats_print_hash_entry, NULL);

  pthread_mutex_unlock (&heap_Bestspace->bestspace_mutex);
}

/*
 * heap_stats_print_hash_entry()- Print the entry
 *                              Will be used by mht_dump() function
 *  return:
 *
 *   outfp(in):
 *   key(in):
 *   ent(in):
 *   ignore(in):
 */
static int
heap_stats_print_hash_entry (FILE * outfp, const void *key, void *ent,
			     void *ignore)
{
  HEAP_STATS_ENTRY *entry = (HEAP_STATS_ENTRY *) ent;

  fprintf (outfp, "HFID:Volid = %6d, Fileid = %6d, Header-pageid = %6d"
	   ",VPID = %4d|%4d, freespace = %6d\n",
	   entry->hfid.vfid.volid, entry->hfid.vfid.fileid, entry->hfid.hpgid,
	   entry->best.vpid.volid, entry->best.vpid.pageid,
	   entry->best.freespace);

  return true;
}

/*
 * heap_stats_entry_free () - release all memory occupied by an best space
 *   return:  NO_ERROR
 *   data(in): a best space associated with the key
 *   args(in): NULL (not used here, but needed by mht_map)
 */
static int
heap_stats_entry_free (THREAD_ENTRY * thread_p, void *data, void *args)
{
  HEAP_STATS_ENTRY *ent;

  ent = (HEAP_STATS_ENTRY *) data;
  assert_release (ent != NULL);

  if (ent)
    {
      if (heap_Bestspace->free_list_count < HEAP_STATS_ENTRY_FREELIST_SIZE)
	{
	  ent->next = heap_Bestspace->free_list;
	  heap_Bestspace->free_list = ent;

	  heap_Bestspace->free_list_count++;
	}
      else
	{
	  free (ent);

	  heap_Bestspace->num_free++;
	}
    }

  return NO_ERROR;
}

/*
 * heap_stats_add_bestspace () -
 */
static HEAP_STATS_ENTRY *
heap_stats_add_bestspace (THREAD_ENTRY * thread_p, const HFID * hfid,
			  VPID * vpid, int freespace)
{
  HEAP_STATS_ENTRY *ent;

  ent = (HEAP_STATS_ENTRY *) mht_get (heap_Bestspace->vpid_ht, vpid);

  if (ent)
    {
      ent->best.freespace = freespace;
    }
  else
    {
      if (heap_Bestspace->num_stats_entries >=
	  prm_get_integer_value (PRM_ID_HF_MAX_BESTSPACE_ENTRIES))
	{
	  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE,
		  ER_HF_MAX_BESTSPACE_ENTRIES, 1,
		  prm_get_integer_value (PRM_ID_HF_MAX_BESTSPACE_ENTRIES));

	  mnt_hf_stats_bestspace_maxed (thread_p);

	  return NULL;
	}

      if (heap_Bestspace->free_list_count > 0)
	{
	  ent = heap_Bestspace->free_list;
	  heap_Bestspace->free_list = ent->next;
	  ent->next = NULL;

	  heap_Bestspace->free_list_count--;
	}
      else
	{
	  ent = (HEAP_STATS_ENTRY *) malloc (sizeof (HEAP_STATS_ENTRY));

	  heap_Bestspace->num_alloc++;
	}

      assert_release (ent != NULL);

      if (ent)
	{
	  HFID_COPY (&ent->hfid, hfid);
	  ent->best.vpid = *vpid;
	  ent->best.freespace = freespace;
	  ent->next = NULL;

	  if (ent
	      && mht_put (heap_Bestspace->vpid_ht, &ent->best.vpid,
			  ent) == NULL)
	    {
	      assert_release (false);
	      free (ent);
	      ent = NULL;
	    }

	  if (ent
	      && mht_put_new (heap_Bestspace->hfid_ht, &ent->hfid,
			      ent) == NULL)
	    {
	      assert_release (false);
	      (void) mht_rem (heap_Bestspace->vpid_ht, &ent->best.vpid,
			      NULL, NULL);
	      (void) heap_stats_entry_free (NULL, ent, NULL);
	      ent = NULL;
	    }

	  if (ent)
	    {
	      heap_Bestspace->num_stats_entries++;

	      mnt_hf_stats_bestspace_entries (thread_p,
					      heap_Bestspace->
					      num_stats_entries);
	    }
	}
    }

  return ent;
}

/*
 * heap_stats_del_bestspace () -
 */
static int
heap_stats_del_bestspace (THREAD_ENTRY * thread_p, const HFID * hfid,
			  int freespace, int stop_cnt)
{
  void *last;
  HEAP_STATS_ENTRY *ent;
  int del_cnt = 0;

  last = NULL;
  do
    {
      ent = (HEAP_STATS_ENTRY *) mht_get2 (heap_Bestspace->hfid_ht,
					   hfid, &last);
      if (ent == NULL)
	{
	  break;		/* end of hash */
	}

      if (freespace < 0		/* delete all entries of hfid */
	  || ent->best.freespace < freespace)
	{
	  (void) mht_rem2 (heap_Bestspace->hfid_ht, &ent->hfid,
			   ent, NULL, NULL);
	  last = NULL;		/* for mht_get2 */

	  (void) mht_rem (heap_Bestspace->vpid_ht, &ent->best.vpid,
			  NULL, NULL);
	  (void) heap_stats_entry_free (NULL, ent, NULL);

	  del_cnt++;

	  if (del_cnt >= stop_cnt)
	    {
	      break;		/* for speed up */
	    }
	}
    }
  while (ent);

  assert_release (del_cnt <= heap_Bestspace->num_stats_entries);

  heap_Bestspace->num_stats_entries -= del_cnt;

  mnt_hf_stats_bestspace_entries (thread_p,
				  heap_Bestspace->num_stats_entries);

  return del_cnt;
}

/*
 * heap_stats_del_bestspace_by_vpid () -
 */
static int
heap_stats_del_bestspace_by_vpid (THREAD_ENTRY * thread_p, const HFID * hfid,
				  VPID * vpid)
{
  void *last;
  HEAP_STATS_ENTRY *ent;
  int del_cnt = 0;

  last = NULL;
  do
    {
      ent = (HEAP_STATS_ENTRY *) mht_get2 (heap_Bestspace->hfid_ht,
					   hfid, &last);
      if (ent == NULL)
	{
	  break;		/* end of hash */
	}

      if (VPID_EQ (&ent->best.vpid, vpid))
	{
	  (void) mht_rem2 (heap_Bestspace->hfid_ht, &ent->hfid,
			   ent, NULL, NULL);
	  last = NULL;		/* for mht_get2 */

	  (void) mht_rem (heap_Bestspace->vpid_ht, &ent->best.vpid,
			  NULL, NULL);
	  (void) heap_stats_entry_free (NULL, ent, NULL);

	  del_cnt++;
	}
    }
  while (ent);

  assert_release (del_cnt <= 1);

  heap_Bestspace->num_stats_entries -= del_cnt;

  mnt_hf_stats_bestspace_entries (thread_p,
				  heap_Bestspace->num_stats_entries);

  return del_cnt;
}

/*
 * Scan page buffer and latch page manipulation
 */

static PAGE_PTR heap_scan_pb_lock_and_fetch (THREAD_ENTRY * thread_p,
					     VPID * vpid_ptr, int new_page,
					     LOCK lock,
					     HEAP_SCANCACHE * scan_cache);

/*
 * heap_scan_pb_lock_and_fetch () -
 *   return:
 *   vpid_ptr(in):
 *   new_page(in):
 *   lock(in):
 *   scan_cache(in):
 */
static PAGE_PTR
heap_scan_pb_lock_and_fetch (THREAD_ENTRY * thread_p, VPID * vpid_ptr,
			     int new_page, LOCK lock,
			     HEAP_SCANCACHE * scan_cache)
{
  PAGE_PTR pgptr = NULL;
  LOCK page_lock;

  if (scan_cache != NULL)
    {
      if (scan_cache->page_latch == NULL_LOCK)
	{
	  page_lock = NULL_LOCK;
	}
      else
	{
	  assert (scan_cache->page_latch >= NULL_LOCK && lock >= NULL_LOCK);
	  page_lock = lock_Conv[scan_cache->page_latch][lock];
	  assert (page_lock != NA_LOCK);
	}
    }
  else
    {
      page_lock = lock;
    }

  if (page_lock == S_LOCK)
    {
      pgptr = pgbuf_fix (thread_p, vpid_ptr, new_page, PGBUF_LATCH_READ,
			 PGBUF_UNCONDITIONAL_LATCH);
    }
  else
    {
      pgptr = pgbuf_fix (thread_p, vpid_ptr, new_page, PGBUF_LATCH_WRITE,
			 PGBUF_UNCONDITIONAL_LATCH);
    }

  return pgptr;
}

/*
 * heap_is_big_length () -
 *   return: true/false
 *   length(in):
 */
static bool
heap_is_big_length (int length)
{
  return (length > heap_Maxslotted_reclength) ? true : false;
}

/*
 * heap_get_spage_type () -
 *   return: the type of the slotted page of the heap file.
 */
static int
heap_get_spage_type (void)
{
  return ANCHORED_DONT_REUSE_SLOTS;
}

/*
 * heap_is_reusable_oid () -
 *   return: true if the heap file is reuse_oid table
 *   file_type(in): the file type of the heap file
 */
static bool
heap_is_reusable_oid (const FILE_TYPE file_type)
{
  if (file_type == FILE_HEAP)
    {
      return false;
    }
  else if (file_type == FILE_HEAP_REUSE_SLOTS)
    {
      return true;
    }
  else
    {
      assert (false);
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
    }
  return false;
}

/*
 * heap_scancache_update_hinted_when_lots_space () -
 *   return: NO_ERROR
 *   scan_cache(in):
 *   pgptr(in):
 */
static int
heap_scancache_update_hinted_when_lots_space (THREAD_ENTRY *
					      thread_p,
					      HEAP_SCANCACHE * scan_cache,
					      PAGE_PTR pgptr)
{
  VPID *this_vpid;
  int i;
  int ret = NO_ERROR;
  int npages, nrecords, rec_length;
  int freespace;

  this_vpid = pgbuf_get_vpid_ptr (pgptr);
  if (!VPID_EQ (&scan_cache->collect_nxvpid, this_vpid))
    {
      /* This page's statistics was already collected. */
      return NO_ERROR;
    }

  /* We can collect statistics */
  spage_collect_statistics (pgptr, &npages, &nrecords, &rec_length);
  scan_cache->collect_npages += npages;
  scan_cache->collect_nrecs += nrecords;
  scan_cache->collect_recs_sumlen += rec_length;

  freespace = spage_max_space_for_new_record (thread_p, pgptr);

  if (freespace > HEAP_DROP_FREE_SPACE)
    {
      if (scan_cache->collect_nbest < scan_cache->collect_maxbest)
	{
	  i = scan_cache->collect_nbest;
	  scan_cache->collect_best[i].vpid = *this_vpid;
	  scan_cache->collect_best[i].freespace = freespace;
	  scan_cache->collect_nbest++;
	}
      else
	{
	  scan_cache->collect_nother_best++;
	}
    }

  ret = heap_vpid_next (&scan_cache->hfid, pgptr,
			&scan_cache->collect_nxvpid);

  return ret;
}

/* TODO: STL::list for _cache.area */
/*
 * heap_classrepr_initialize_cache () - Initialize the class representation cache
 *   return: NO_ERROR
 */
static int
heap_classrepr_initialize_cache (void)
{
  HEAP_CLASSREPR_ENTRY *cache_entry;
  HEAP_CLASSREPR_LOCK *lock_entry;
  HEAP_CLASSREPR_HASH *hash_entry;
  int i, ret = NO_ERROR;
  size_t size;

  if (heap_Classrepr != NULL)
    {
      ret = heap_classrepr_finalize_cache ();
      if (ret != NO_ERROR)
	{
	  goto exit_on_error;
	}
    }

  /* initialize hash entries table */
  heap_Classrepr_cache.num_entries = HEAP_CLASSREPR_MAXCACHE;

  heap_Classrepr_cache.area =
    (HEAP_CLASSREPR_ENTRY *) malloc (sizeof (HEAP_CLASSREPR_ENTRY)
				     * heap_Classrepr_cache.num_entries);
  if (heap_Classrepr_cache.area == NULL)
    {
      ret = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ret, 1,
	      sizeof (HEAP_CLASSREPR_ENTRY) *
	      heap_Classrepr_cache.num_entries);
      goto exit_on_error;
    }

  cache_entry = heap_Classrepr_cache.area;
  for (i = 0; i < heap_Classrepr_cache.num_entries; i++)
    {
      pthread_mutex_init (&cache_entry[i].mutex, NULL);

      cache_entry[i].idx = i;
      cache_entry[i].fcnt = 0;
      cache_entry[i].zone = ZONE_FREE;
      cache_entry[i].next_wait_thrd = NULL;
      cache_entry[i].hash_next = NULL;
      cache_entry[i].prev = NULL;
      cache_entry[i].next =
	(i <
	 heap_Classrepr_cache.num_entries - 1) ? &cache_entry[i + 1] : NULL;

      cache_entry[i].force_decache = false;

      OID_SET_NULL (&cache_entry[i].class_oid);
      cache_entry[i].max_reprid = DEFAULT_REPR_INCREMENT;
      cache_entry[i].repr = (OR_CLASSREP **)
	malloc (cache_entry[i].max_reprid * sizeof (OR_CLASSREP *));
      if (cache_entry[i].repr == NULL)
	{
	  ret = ER_OUT_OF_VIRTUAL_MEMORY;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ret, 1,
		  cache_entry[i].max_reprid * sizeof (OR_CLASSREP *));
	  goto exit_on_error;
	}
      memset (cache_entry[i].repr, 0,
	      cache_entry[i].max_reprid * sizeof (OR_CLASSREP *));

      cache_entry[i].last_reprid = NULL_REPRID;
    }

  /* initialize hash bucket table */
  heap_Classrepr_cache.num_hash = CLASSREPR_HASH_SIZE;
  heap_Classrepr_cache.hash_table = (HEAP_CLASSREPR_HASH *)
    malloc (heap_Classrepr_cache.num_hash * sizeof (HEAP_CLASSREPR_HASH));
  if (heap_Classrepr_cache.hash_table == NULL)
    {
      ret = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ret, 1,
	      heap_Classrepr_cache.num_hash * sizeof (HEAP_CLASSREPR_HASH));
      goto exit_on_error;
    }

  hash_entry = heap_Classrepr_cache.hash_table;
  for (i = 0; i < heap_Classrepr_cache.num_hash; i++)
    {
      pthread_mutex_init (&hash_entry[i].hash_mutex, NULL);
      hash_entry[i].idx = i;
      hash_entry[i].hash_next = NULL;
      hash_entry[i].lock_next = NULL;
    }

  /* initialize hash lock table */
  size = thread_num_total_threads () * sizeof (HEAP_CLASSREPR_LOCK);
  heap_Classrepr_cache.lock_table = (HEAP_CLASSREPR_LOCK *) malloc (size);
  if (heap_Classrepr_cache.lock_table == NULL)
    {
      ret = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ret, 1, size);
      goto exit_on_error;
    }
  lock_entry = heap_Classrepr_cache.lock_table;
  for (i = 0; i < thread_num_total_threads (); i++)
    {
      OID_SET_NULL (&lock_entry[i].class_oid);
      lock_entry[i].lock_next = NULL;
      lock_entry[i].next_wait_thrd = NULL;
    }

  /* initialize LRU list */

  pthread_mutex_init (&heap_Classrepr_cache.LRU_list.LRU_mutex, NULL);
  heap_Classrepr_cache.LRU_list.LRU_top = NULL;
  heap_Classrepr_cache.LRU_list.LRU_bottom = NULL;

  /* initialize free list */
  pthread_mutex_init (&heap_Classrepr_cache.free_list.free_mutex, NULL);
  heap_Classrepr_cache.free_list.free_top = &heap_Classrepr_cache.area[0];
  heap_Classrepr_cache.free_list.free_cnt = heap_Classrepr_cache.num_entries;

  heap_Classrepr = &heap_Classrepr_cache;

  return ret;

exit_on_error:

  heap_Classrepr_cache.num_entries = 0;

  return (ret == NO_ERROR) ? ER_FAILED : ret;
}

/* TODO: STL::list for _cache.area */
/*
 * heap_classrepr_finalize_cache () - Destroy any cached structures
 *   return: NO_ERROR
 *
 * Note: Any cached representations are deallocated at this moment and
 * the hash table is also removed.
 */
static int
heap_classrepr_finalize_cache (void)
{
  HEAP_CLASSREPR_ENTRY *cache_entry;
  HEAP_CLASSREPR_HASH *hash_entry;
  int i, j;
  int ret = NO_ERROR;

  if (heap_Classrepr == NULL)
    {
      return NO_ERROR;		/* nop */
    }

#ifdef DEBUG_CLASSREPR_CACHE
  ret = heap_classrepr_dump_anyfixed ();
  if (ret != NO_ERROR)
    {
      return ret;
    }
#endif /* DEBUG_CLASSREPR_CACHE */

  /* finalize hash entries table */
  cache_entry = heap_Classrepr_cache.area;
  for (i = 0; i < heap_Classrepr_cache.num_entries; i++)
    {
      pthread_mutex_destroy (&cache_entry[i].mutex);
      for (j = 0; j <= cache_entry[i].last_reprid; j++)
	{
	  if (cache_entry[i].repr[j] != NULL)
	    {
	      or_free_classrep (cache_entry[i].repr[j]);
	      cache_entry[i].repr[j] = NULL;
	    }
	}
      free_and_init (cache_entry[i].repr);
    }
  free_and_init (heap_Classrepr_cache.area);
  heap_Classrepr_cache.num_entries = -1;

  /* finalize hash bucket table */
  hash_entry = heap_Classrepr_cache.hash_table;
  for (i = 0; i < heap_Classrepr_cache.num_hash; i++)
    {
      pthread_mutex_destroy (&hash_entry[i].hash_mutex);
    }
  heap_Classrepr_cache.num_hash = -1;
  free_and_init (heap_Classrepr_cache.hash_table);

  /* finalize hash lock table */
  free_and_init (heap_Classrepr_cache.lock_table);

  /* finalize LRU list */

  pthread_mutex_destroy (&heap_Classrepr_cache.LRU_list.LRU_mutex);

  /* initialize free list */
  pthread_mutex_destroy (&heap_Classrepr_cache.free_list.free_mutex);

  heap_Classrepr = NULL;

  return ret;
}

/*
 * heap_classrepr_entry_reset () -
 *   return: NO_ERROR
 *   cache_entry(in):
 *
 * Note: Reset the given class representation entry.
 */
static int
heap_classrepr_entry_reset (HEAP_CLASSREPR_ENTRY * cache_entry)
{
  int i;
  int ret = NO_ERROR;

  if (cache_entry == NULL)
    {
      return NO_ERROR;		/* nop */
    }

  /* free all classrepr */
  for (i = 0; i <= cache_entry->last_reprid; i++)
    {
      if (cache_entry->repr[i] != NULL)
	{
	  or_free_classrep (cache_entry->repr[i]);
	  cache_entry->repr[i] = NULL;
	}
    }

  cache_entry->force_decache = false;
  OID_SET_NULL (&cache_entry->class_oid);
  if (cache_entry->max_reprid > DEFAULT_REPR_INCREMENT)
    {
      OR_CLASSREP **t;

      t = cache_entry->repr;
      cache_entry->repr = (OR_CLASSREP **)
	malloc (DEFAULT_REPR_INCREMENT * sizeof (OR_CLASSREP *));
      if (cache_entry->repr == NULL)
	{
	  ret = ER_OUT_OF_VIRTUAL_MEMORY;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ret,
		  1, DEFAULT_REPR_INCREMENT * sizeof (OR_CLASSREP *));
	  cache_entry->repr = t;
	}
      else
	{
	  free_and_init (t);
	  cache_entry->max_reprid = DEFAULT_REPR_INCREMENT;
	  memset (cache_entry->repr, 0,
		  DEFAULT_REPR_INCREMENT * sizeof (OR_CLASSREP *));
	}

    }
  cache_entry->last_reprid = NULL_REPRID;

  return ret;
}

/*
 * heap_classrepr_entry_remove_from_LRU () -
 *   return: NO_ERROR
 *   cache_entry(in):
 */
static int
heap_classrepr_entry_remove_from_LRU (HEAP_CLASSREPR_ENTRY * cache_entry)
{
  if (cache_entry)
    {
      if (cache_entry == heap_Classrepr_cache.LRU_list.LRU_top)
	{
	  heap_Classrepr_cache.LRU_list.LRU_top = cache_entry->next;
	}
      else
	{
	  cache_entry->prev->next = cache_entry->next;
	}

      if (cache_entry == heap_Classrepr_cache.LRU_list.LRU_bottom)
	{
	  heap_Classrepr_cache.LRU_list.LRU_bottom = cache_entry->prev;
	}
      else
	{
	  cache_entry->next->prev = cache_entry->prev;
	}
    }

  return NO_ERROR;
}

/* TODO: STL::list for ->prev */
/*
 * heap_classrepr_decache_guessed_last () -
 *   return: NO_ERROR
 *   class_oid(in):
 *
 * Note: Decache the guessed last representations (i.e., that with -1)
 * from the given class.
 *
 * Note: This function should be called when a class is updated.
 *       1: During normal update
 */
static int
heap_classrepr_decache_guessed_last (const OID * class_oid)
{
  HEAP_CLASSREPR_ENTRY *cache_entry, *prev_entry, *cur_entry;
  HEAP_CLASSREPR_HASH *hash_anchor;
  int rv;
  int ret = NO_ERROR;

  if (class_oid != NULL)
    {
      hash_anchor = &heap_Classrepr->hash_table[REPR_HASH (class_oid)];

    search_begin:
      rv = pthread_mutex_lock (&hash_anchor->hash_mutex);

      for (cache_entry = hash_anchor->hash_next; cache_entry != NULL;
	   cache_entry = cache_entry->hash_next)
	{
	  if (OID_EQ (class_oid, &cache_entry->class_oid))
	    {
	      rv = pthread_mutex_trylock (&cache_entry->mutex);
	      if (rv == 0)
		{
		  goto delete_begin;
		}

	      if (rv != EBUSY)
		{
		  ret = ER_CSS_PTHREAD_MUTEX_LOCK;
		  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
				       ret, 0);
		  pthread_mutex_unlock (&hash_anchor->hash_mutex);
		  goto exit_on_error;
		}

	      pthread_mutex_unlock (&hash_anchor->hash_mutex);
	      rv = pthread_mutex_lock (&cache_entry->mutex);

	      /* cache_entry can be used by others. check again */
	      if (!OID_EQ (class_oid, &cache_entry->class_oid))
		{
		  pthread_mutex_unlock (&cache_entry->mutex);
		  goto search_begin;
		}
	      break;
	    }
	}

      /* class_oid cache_entry is not found */
      if (cache_entry == NULL)
	{
	  pthread_mutex_unlock (&hash_anchor->hash_mutex);
	  goto exit_on_error;
	}

      /* hash anchor lock has been released */
      rv = pthread_mutex_lock (&hash_anchor->hash_mutex);

    delete_begin:

      /* delete classrepr from hash chain */
      prev_entry = NULL;
      cur_entry = hash_anchor->hash_next;
      while (cur_entry != NULL)
	{
	  if (cur_entry == cache_entry)
	    {
	      break;
	    }
	  prev_entry = cur_entry;
	  cur_entry = cur_entry->hash_next;
	}

      /* class_oid cache_entry is not found */
      if (cur_entry == NULL)
	{
	  /* This cannot happen */
	  pthread_mutex_unlock (&hash_anchor->hash_mutex);
	  pthread_mutex_unlock (&cache_entry->mutex);

	  goto exit_on_error;
	}

      if (prev_entry == NULL)
	{
	  hash_anchor->hash_next = cur_entry->hash_next;
	}
      else
	{
	  prev_entry->hash_next = cur_entry->hash_next;
	}
      cur_entry->hash_next = NULL;

      pthread_mutex_unlock (&hash_anchor->hash_mutex);

      cache_entry->force_decache = true;

      /* Remove from LRU list */
      if (cache_entry->zone == ZONE_LRU)
	{
	  rv = pthread_mutex_lock (&heap_Classrepr_cache.LRU_list.LRU_mutex);
	  (void) heap_classrepr_entry_remove_from_LRU (cache_entry);
	  pthread_mutex_unlock (&heap_Classrepr_cache.LRU_list.LRU_mutex);
	  cache_entry->zone = ZONE_VOID;
	}
      cache_entry->prev = NULL;
      cache_entry->next = NULL;

      if (cache_entry->fcnt == 0)
	{
	  /* move cache_entry to free_list */
	  ret = heap_classrepr_entry_reset (cache_entry);
	  if (ret == NO_ERROR)
	    {
	      ret = heap_classrepr_entry_free (cache_entry);
	    }
	}

      pthread_mutex_unlock (&cache_entry->mutex);
    }

  return ret;

exit_on_error:

  return (ret == NO_ERROR) ? ER_FAILED : ret;
}

/*
 * heap_classrepr_decache () - Deache any unfixed class representations of
 *                           given class
 *   return: NO_ERROR
 *   class_oid(in):
 *
 * Note: Decache all class representations of given class. If a class
 * is not given all class representations are decached.
 *
 * Note: This function should be called when a class is updated.
 *       1: At the end/beginning of rollback since we do not have any
 *          idea of a heap identifier of rolled back objects and we
 *          expend too much time, searching for the OID, every time we
 *          rolled back an updated object.
 */
int
heap_classrepr_decache (THREAD_ENTRY * thread_p, const OID * class_oid)
{
  int ret;

  ret = heap_classrepr_decache_guessed_last (class_oid);
  if (ret != NO_ERROR)
    {
      return ret;
    }

  if (csect_enter (thread_p, CSECT_HEAP_CHNGUESS, INF_WAIT) != NO_ERROR)
    {
      return ER_FAILED;
    }
  if (heap_Guesschn != NULL && heap_Guesschn->schema_change == false)
    {
      ret = heap_chnguess_decache (class_oid);
    }
  csect_exit (thread_p, CSECT_HEAP_CHNGUESS);

  return ret;
}

/* TODO: STL::list for _cache.area */
/*
 * heap_classrepr_free () - Free a class representation
 *   return: NO_ERROR
 *   classrep(in): The class representation structure
 *   idx_incache(in): An index if the desired class representation is part of
 *                    the cache, otherwise -1 (no part of cache)
 *
 * Note: Free a class representation. If the class representation was
 * part of the class representation cache, the fix count is
 * decremented and the class representation will continue be
 * cached. The representation entry will be subject for
 * replacement when the fix count is zero (no one is using it).
 * If the class representatin was not part of the cache, it is
 * freed.
 */
int
heap_classrepr_free (OR_CLASSREP * classrep, int *idx_incache)
{
  HEAP_CLASSREPR_ENTRY *cache_entry;
  int rv;
  int ret = NO_ERROR;

  if (*idx_incache < 0)
    {
      or_free_classrep (classrep);
      return NO_ERROR;
    }

  cache_entry = &heap_Classrepr_cache.area[*idx_incache];

  rv = pthread_mutex_lock (&cache_entry->mutex);
  cache_entry->fcnt--;
  if (cache_entry->fcnt == 0)
    {
      /*
       * Is this entry declared to be decached
       */
#ifdef DEBUG_CLASSREPR_CACHE
      rv = pthread_mutex_lock (&heap_Classrepr_cache.num_fix_entries_mutex);
      heap_Classrepr_cache.num_fix_entries--;
      pthread_mutex_unlock (&heap_Classrepr_cache.num_fix_entries_mutex);
#endif /* DEBUG_CLASSREPR_CACHE */
      if (cache_entry->force_decache == true)
	{
	  /* cache_entry is already removed from LRU list. */

	  /* move cache_entry to free_list */
	  ret = heap_classrepr_entry_free (cache_entry);
	  if (ret == NO_ERROR)
	    {
	      ret = heap_classrepr_entry_reset (cache_entry);
	    }
	}
      else
	{
	  /* relocate entry to the top of LRU list */
	  if (cache_entry != heap_Classrepr_cache.LRU_list.LRU_top)
	    {
	      rv =
		pthread_mutex_lock (&heap_Classrepr_cache.LRU_list.LRU_mutex);
	      if (cache_entry->zone == ZONE_LRU)
		{
		  /* remove from LRU list */
		  (void) heap_classrepr_entry_remove_from_LRU (cache_entry);
		}

	      /* insert into LRU top */
	      cache_entry->prev = NULL;
	      cache_entry->next = heap_Classrepr_cache.LRU_list.LRU_top;
	      if (heap_Classrepr_cache.LRU_list.LRU_top == NULL)
		{
		  heap_Classrepr_cache.LRU_list.LRU_bottom = cache_entry;
		}
	      else
		{
		  heap_Classrepr_cache.LRU_list.LRU_top->prev = cache_entry;
		}
	      heap_Classrepr_cache.LRU_list.LRU_top = cache_entry;
	      cache_entry->zone = ZONE_LRU;

	      pthread_mutex_unlock (&heap_Classrepr_cache.LRU_list.LRU_mutex);
	    }
	}
    }
  pthread_mutex_unlock (&cache_entry->mutex);
  *idx_incache = -1;

  return ret;
}

#ifdef SERVER_MODE

enum
{ NEED_TO_RETRY = 0, LOCK_ACQUIRED };

/*
 * heap_classrepr_lock_class () - Prevent other threads accessing class_oid
 *                              class representation.
 *   return: ER_FAILED, NEED_TO_RETRY or LOCK_ACQUIRED
 *   hash_anchor(in):
 *   class_oid(in):
 */
static int
heap_classrepr_lock_class (THREAD_ENTRY * thread_p,
			   HEAP_CLASSREPR_HASH * hash_anchor, OID * class_oid)
{
  HEAP_CLASSREPR_LOCK *cur_lock_entry;
  THREAD_ENTRY *cur_thrd_entry;

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
      if (thread_p == NULL)
	{
	  return ER_FAILED;
	}
    }
  cur_thrd_entry = thread_p;

  for (cur_lock_entry = hash_anchor->lock_next; cur_lock_entry != NULL;
       cur_lock_entry = cur_lock_entry->lock_next)
    {
      if (OID_EQ (&cur_lock_entry->class_oid, class_oid))
	{
	  cur_thrd_entry->next_wait_thrd = cur_lock_entry->next_wait_thrd;
	  cur_lock_entry->next_wait_thrd = cur_thrd_entry;

	  thread_lock_entry (cur_thrd_entry);
	  pthread_mutex_unlock (&hash_anchor->hash_mutex);
	  thread_suspend_wakeup_and_unlock_entry (cur_thrd_entry,
						  THREAD_HEAP_CLSREPR_SUSPENDED);

	  if (cur_thrd_entry->resume_status == THREAD_HEAP_CLSREPR_RESUMED)
	    {
	      return NEED_TO_RETRY;	/* traverse hash chain again */
	    }
	  else
	    {
	      /* probably due to an interrupt */
	      assert ((cur_thrd_entry->resume_status ==
		       THREAD_RESUME_DUE_TO_INTERRUPT));
	      return ER_FAILED;
	    }
	}
    }

  cur_lock_entry = &heap_Classrepr_cache.lock_table[cur_thrd_entry->index];
  cur_lock_entry->class_oid = *class_oid;
  cur_lock_entry->next_wait_thrd = NULL;
  cur_lock_entry->lock_next = hash_anchor->lock_next;
  hash_anchor->lock_next = cur_lock_entry;

  pthread_mutex_unlock (&hash_anchor->hash_mutex);

  return LOCK_ACQUIRED;		/* lock acquired. */
}

/*
 * heap_classrepr_unlock_class () -
 *   return: NO_ERROR
 *   hash_anchor(in):
 *   class_oid(in):
 *   need_hash_mutex(in):
 */
static int
heap_classrepr_unlock_class (HEAP_CLASSREPR_HASH * hash_anchor,
			     OID * class_oid, int need_hash_mutex)
{
  HEAP_CLASSREPR_LOCK *prev_lock_entry, *cur_lock_entry;
  THREAD_ENTRY *cur_thrd_entry;
  int rv;

  /* if hash mutex lock is not acquired */
  if (need_hash_mutex)
    {
      rv = pthread_mutex_lock (&hash_anchor->hash_mutex);
    }

  prev_lock_entry = NULL;
  cur_lock_entry = hash_anchor->lock_next;
  while (cur_lock_entry != NULL)
    {
      if (OID_EQ (&cur_lock_entry->class_oid, class_oid))
	{
	  break;
	}
      prev_lock_entry = cur_lock_entry;
      cur_lock_entry = cur_lock_entry->lock_next;
    }

  /* if lock entry is found, remove it from lock list */
  if (cur_lock_entry == NULL)
    {				/* this cannot happen */
      pthread_mutex_unlock (&hash_anchor->hash_mutex);
      return ER_FAILED;
    }

  if (prev_lock_entry == NULL)
    {
      hash_anchor->lock_next = cur_lock_entry->lock_next;
    }
  else
    {
      prev_lock_entry->lock_next = cur_lock_entry->lock_next;
    }
  cur_lock_entry->lock_next = NULL;
  pthread_mutex_unlock (&hash_anchor->hash_mutex);
  for (cur_thrd_entry = cur_lock_entry->next_wait_thrd;
       cur_thrd_entry != NULL;
       cur_thrd_entry = cur_lock_entry->next_wait_thrd)
    {
      cur_lock_entry->next_wait_thrd = cur_thrd_entry->next_wait_thrd;
      cur_thrd_entry->next_wait_thrd = NULL;

      thread_wakeup (cur_thrd_entry, THREAD_HEAP_CLSREPR_RESUMED);
    }

  return NO_ERROR;
}
#endif /* SERVER_MODE */

/* TODO: STL::list for ->prev */
/*
 * heap_classrepr_entry_alloc () -
 *   return:
 */
static HEAP_CLASSREPR_ENTRY *
heap_classrepr_entry_alloc (void)
{
  HEAP_CLASSREPR_HASH *hash_anchor;
  HEAP_CLASSREPR_ENTRY *cache_entry, *prev_entry, *cur_entry;
  int rv;

  cache_entry = NULL;

/* check_free_list: */

  /* 1. Get entry from free list */
  if (heap_Classrepr_cache.free_list.free_top == NULL)
    {
      goto check_LRU_list;
    }

  rv = pthread_mutex_lock (&heap_Classrepr_cache.free_list.free_mutex);
  if (heap_Classrepr_cache.free_list.free_top == NULL)
    {
      pthread_mutex_unlock (&heap_Classrepr_cache.free_list.free_mutex);
      cache_entry = NULL;
    }
  else
    {
      cache_entry = heap_Classrepr_cache.free_list.free_top;
      heap_Classrepr_cache.free_list.free_top = cache_entry->next;
      heap_Classrepr_cache.free_list.free_cnt--;
      pthread_mutex_unlock (&heap_Classrepr_cache.free_list.free_mutex);

      rv = pthread_mutex_lock (&cache_entry->mutex);
      cache_entry->next = NULL;
      cache_entry->zone = ZONE_VOID;

      return cache_entry;
    }

check_LRU_list:
  /* 2. Get entry from LRU list */
  if (heap_Classrepr_cache.LRU_list.LRU_bottom == NULL)
    {
      goto expand_list;
    }

  rv = pthread_mutex_lock (&heap_Classrepr_cache.LRU_list.LRU_mutex);
  for (cache_entry = heap_Classrepr_cache.LRU_list.LRU_bottom;
       cache_entry != NULL; cache_entry = cache_entry->prev)
    {
      if (cache_entry->fcnt == 0)
	{
	  /* remove from LRU list */
	  (void) heap_classrepr_entry_remove_from_LRU (cache_entry);
	  cache_entry->zone = ZONE_VOID;
	  cache_entry->next = cache_entry->prev = NULL;
	  break;
	}
    }
  pthread_mutex_unlock (&heap_Classrepr_cache.LRU_list.LRU_mutex);

  if (cache_entry == NULL)
    {
      goto expand_list;
    }

  rv = pthread_mutex_lock (&cache_entry->mutex);
  /* if some has referenced, retry */
  if (cache_entry->fcnt != 0)
    {
      pthread_mutex_unlock (&cache_entry->mutex);
      goto check_LRU_list;
    }

  /* delete classrepr from hash chain */
  hash_anchor =
    &heap_Classrepr->hash_table[REPR_HASH (&cache_entry->class_oid)];
  rv = pthread_mutex_lock (&hash_anchor->hash_mutex);
  prev_entry = NULL;
  cur_entry = hash_anchor->hash_next;
  while (cur_entry != NULL)
    {
      if (cur_entry == cache_entry)
	{
	  break;
	}
      prev_entry = cur_entry;
      cur_entry = cur_entry->hash_next;
    }

  if (cur_entry == NULL)
    {
      /* This cannot happen */
      pthread_mutex_unlock (&hash_anchor->hash_mutex);
      pthread_mutex_unlock (&cache_entry->mutex);

      return NULL;
    }
  if (prev_entry == NULL)
    {
      hash_anchor->hash_next = cur_entry->hash_next;
    }
  else
    {
      prev_entry->hash_next = cur_entry->hash_next;
    }
  cur_entry->hash_next = NULL;
  pthread_mutex_unlock (&hash_anchor->hash_mutex);

  (void) heap_classrepr_entry_reset (cache_entry);

end:

  return cache_entry;

expand_list:

  /* not supported */
  cache_entry = NULL;
  goto end;
}

/* TODO: STL::list for ->next */
/*
 * heap_classrepr_entry_free () -
 *   return: NO_ERROR
 *   cache_entry(in):
 */
static int
heap_classrepr_entry_free (HEAP_CLASSREPR_ENTRY * cache_entry)
{
  int rv;
  rv = pthread_mutex_lock (&heap_Classrepr_cache.free_list.free_mutex);

  cache_entry->next = heap_Classrepr_cache.free_list.free_top;
  heap_Classrepr_cache.free_list.free_top = cache_entry;
  cache_entry->zone = ZONE_FREE;
  heap_Classrepr_cache.free_list.free_cnt++;

  pthread_mutex_unlock (&heap_Classrepr_cache.free_list.free_mutex);

  return NO_ERROR;
}

/*
 * heap_classrepr_get () - Obtain the desired class representation
 *   return: classrepr
 *   class_oid(in): The class identifier
 *   class_recdes(in): The class recdes (when know) or NULL
 *   reprid(in): Representation of the class or NULL_REPRID for last one
 *   idx_incache(in): An index if the desired class representation is part
 *                    of the cache
 *
 * Note: Obtain the desired class representation for the given class.
 */
OR_CLASSREP *
heap_classrepr_get (THREAD_ENTRY * thread_p, OID * class_oid,
		    RECDES * class_recdes, REPR_ID reprid, int *idx_incache,
		    bool use_last_reprid)
{
  HEAP_CLASSREPR_ENTRY *cache_entry;
  HEAP_CLASSREPR_HASH *hash_anchor;
  OR_CLASSREP *repr = NULL;
  REPR_ID last_reprid;
  int r;

  hash_anchor = &heap_Classrepr->hash_table[REPR_HASH (class_oid)];

  /* search entry with class_oid from hash chain */
search_begin:
  r = pthread_mutex_lock (&hash_anchor->hash_mutex);

  for (cache_entry = hash_anchor->hash_next; cache_entry != NULL;
       cache_entry = cache_entry->hash_next)
    {
      if (OID_EQ (class_oid, &cache_entry->class_oid))
	{
	  r = pthread_mutex_trylock (&cache_entry->mutex);
	  if (r == 0)
	    {
	      pthread_mutex_unlock (&hash_anchor->hash_mutex);
	    }
	  else
	    {
	      if (r != EBUSY)
		{
		  /* some error code */
		  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
				       ER_CSS_PTHREAD_MUTEX_LOCK, 0);
		  pthread_mutex_unlock (&hash_anchor->hash_mutex);
		  return NULL;
		}
	      /* if cache_entry lock is busy. release hash mutex lock
	       * and lock cache_entry lock unconditionally */
	      pthread_mutex_unlock (&hash_anchor->hash_mutex);
	      r = pthread_mutex_lock (&cache_entry->mutex);
	    }
	  /* check if cache_entry is used by others */
	  if (!OID_EQ (class_oid, &cache_entry->class_oid))
	    {
	      pthread_mutex_unlock (&cache_entry->mutex);
	      goto search_begin;
	    }

	  break;
	}
    }

  if (cache_entry == NULL)
    {
#ifdef SERVER_MODE
      /* class_oid was not found. Lock class_oid.
       * heap_classrepr_lock_class () release hash_anchor->hash_lock */
      if (heap_classrepr_lock_class (thread_p, hash_anchor, class_oid) !=
	  LOCK_ACQUIRED)
	{
	  if (r == NEED_TO_RETRY)
	    {
	      goto search_begin;
	    }
	  else
	    {
	      assert (r == ER_FAILED);
	      return NULL;
	    }
	}
#endif

      /* Get free entry */
      cache_entry = heap_classrepr_entry_alloc ();

      if (cache_entry == NULL)
	{
	  /* if all cache entry is busy, allocate memory for repr. */
	  if (class_recdes == NULL)
	    {
	      RECDES peek_recdes;
	      HEAP_SCANCACHE scan_cache;

	      heap_scancache_quick_start (&scan_cache);
	      if (heap_get (thread_p, class_oid, &peek_recdes, &scan_cache,
			    PEEK, NULL_CHN) == S_SUCCESS)
		{
		  if (use_last_reprid == true)
		    {
		      repr = or_get_classrep (&peek_recdes, NULL_REPRID);
		    }
		  else
		    {
		      repr = or_get_classrep (&peek_recdes, reprid);
		    }
		}
	      heap_scancache_end (thread_p, &scan_cache);
	    }
	  else
	    {
	      if (use_last_reprid == true)
		{
		  repr = or_get_classrep (class_recdes, NULL_REPRID);
		}
	      else
		{
		  repr = or_get_classrep (class_recdes, reprid);
		}
	    }
	  *idx_incache = -1;

#ifdef SERVER_MODE
	  /* free lock for class_oid */
	  (void) heap_classrepr_unlock_class (hash_anchor, class_oid, true);
#endif

	  return repr;
	}

      /* New cache entry is acquired. Load class_oid classrepr info. on it */
      if (class_recdes == NULL)
	{
	  RECDES peek_recdes;
	  HEAP_SCANCACHE scan_cache;

	  heap_scancache_quick_start (&scan_cache);
	  if (heap_get (thread_p, class_oid, &peek_recdes, &scan_cache, PEEK,
			NULL_CHN) == S_SUCCESS)
	    {
	      last_reprid = or_class_repid (&peek_recdes);
	      assert (last_reprid > NULL_REPRID);

	      if (use_last_reprid == true || reprid == NULL_REPRID)
		{
		  reprid = last_reprid;
		}

	      repr = or_get_classrep (&peek_recdes, reprid);

	      /* check if cache_entry->repr[last_reprid] is valid. */
	      if (last_reprid >= cache_entry->max_reprid)
		{
		  free_and_init (cache_entry->repr);
		  cache_entry->max_reprid = last_reprid + 1;
		  cache_entry->repr = (OR_CLASSREP **)
		    malloc (cache_entry->max_reprid * sizeof (OR_CLASSREP *));
		  if (cache_entry->repr == NULL)
		    {
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			      ER_OUT_OF_VIRTUAL_MEMORY, 1,
			      cache_entry->max_reprid *
			      sizeof (OR_CLASSREP *));

		      pthread_mutex_unlock (&cache_entry->mutex);
		      (void) heap_classrepr_entry_free (cache_entry);
#ifdef SERVER_MODE
		      (void) heap_classrepr_unlock_class (hash_anchor,
							  class_oid, true);
#endif
		      if (repr != NULL)
			{
			  or_free_classrep (repr);
			}
		      return NULL;
		    }
		  memset (cache_entry->repr, 0,
			  cache_entry->max_reprid * sizeof (OR_CLASSREP *));
		}

	      cache_entry->repr[reprid] = repr;

	      if (reprid != last_reprid)
		{		/* if last repr is not cached */
		  cache_entry->repr[last_reprid] =
		    or_get_classrep (&peek_recdes, last_reprid);
		}
	      cache_entry->last_reprid = last_reprid;
	    }
	  else
	    {
	      repr = NULL;
	    }
	  heap_scancache_end (thread_p, &scan_cache);
	}
      else
	{
	  last_reprid = or_class_repid (class_recdes);
	  assert (last_reprid > NULL_REPRID);

	  if (use_last_reprid == true || reprid == NULL_REPRID)
	    {
	      reprid = last_reprid;
	    }

	  repr = or_get_classrep (class_recdes, reprid);

	  if (last_reprid >= cache_entry->max_reprid)
	    {
	      free_and_init (cache_entry->repr);
	      cache_entry->max_reprid = last_reprid + 1;
	      cache_entry->repr = (OR_CLASSREP **)
		malloc (cache_entry->max_reprid * sizeof (OR_CLASSREP *));
	      if (cache_entry->repr == NULL)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_OUT_OF_VIRTUAL_MEMORY, 1,
			  cache_entry->max_reprid * sizeof (OR_CLASSREP *));

		  pthread_mutex_unlock (&cache_entry->mutex);

		  (void) heap_classrepr_entry_free (cache_entry);
#ifdef SERVER_MODE
		  (void) heap_classrepr_unlock_class (hash_anchor, class_oid,
						      true);
#endif
		  if (repr != NULL)
		    {
		      or_free_classrep (repr);
		    }
		  return NULL;
		}
	      memset (cache_entry->repr, 0,
		      cache_entry->max_reprid * sizeof (OR_CLASSREP *));

	    }

	  cache_entry->repr[reprid] = repr;

	  if (reprid != last_reprid)
	    {
	      cache_entry->repr[last_reprid] =
		or_get_classrep (class_recdes, last_reprid);
	    }
	  cache_entry->last_reprid = last_reprid;
	}

      if (repr == NULL)
	{
	  /* free cache_entry and return NULL */
	  pthread_mutex_unlock (&cache_entry->mutex);

	  (void) heap_classrepr_entry_free (cache_entry);
#ifdef SERVER_MODE
	  (void) heap_classrepr_unlock_class (hash_anchor, class_oid, true);
#endif
	  return NULL;
	}
      cache_entry->fcnt = 1;
      cache_entry->class_oid = *class_oid;
#ifdef DEBUG_CLASSREPR_CACHE
      r = pthread_mutex_lock (&heap_Classrepr_cache.num_fix_entries_mutex);
      heap_Classrepr_cache.num_fix_entries++;
      pthread_mutex_unlock (&heap_Classrepr_cache.num_fix_entries_mutex);

#endif /* DEBUG_CLASSREPR_CACHE */
      *idx_incache = cache_entry->idx;

      /* Add to hash chain, and remove lock for class_oid */
      r = pthread_mutex_lock (&hash_anchor->hash_mutex);
      cache_entry->hash_next = hash_anchor->hash_next;
      hash_anchor->hash_next = cache_entry;

#ifdef SERVER_MODE
      (void) heap_classrepr_unlock_class (hash_anchor, class_oid, false);
#endif
    }
  else
    {
      /* now, we have already cache_entry for class_oid.
       * if it contains repr info for reprid, return it.
       * else load classrepr info for it */

      if (use_last_reprid == true || reprid == NULL_REPRID)
	{
	  reprid = cache_entry->last_reprid;
	}

      assert (reprid > NULL_REPRID);

      /* reprid cannot be greater than cache_entry->last_reprid. */
      repr = cache_entry->repr[reprid];
      if (repr == NULL)
	{
	  /* load repr. info. for reprid of class_oid */
	  if (class_recdes == NULL)
	    {
	      RECDES peek_recdes;
	      HEAP_SCANCACHE scan_cache;

	      heap_scancache_quick_start (&scan_cache);
	      if (heap_get (thread_p, class_oid, &peek_recdes, &scan_cache,
			    PEEK, NULL_CHN) == S_SUCCESS)
		{
		  repr = or_get_classrep (&peek_recdes, reprid);
		}
	      heap_scancache_end (thread_p, &scan_cache);
	    }
	  else
	    {
	      repr = or_get_classrep (class_recdes, reprid);
	    }
	  cache_entry->repr[reprid] = repr;
	}

      if (repr != NULL)
	{
	  cache_entry->fcnt++;
	  *idx_incache = cache_entry->idx;
	}
    }
  pthread_mutex_unlock (&cache_entry->mutex);

  return repr;
}

#ifdef DEBUG_CLASSREPR_CACHE
/*
 * heap_classrepr_dump_cache () - Dump the class representation cache
 *   return: NO_ERROR
 *   simple_dump(in):
 *
 * Note: Dump the class representation cache.
 */
static int
heap_classrepr_dump_cache (bool simple_dump)
{
  OR_CLASSREP *classrepr;
  HEAP_CLASSREPR_ENTRY *cache_entry;
  int i, j;
  int rv;
  int ret = NO_ERROR;

  if (heap_Classrepr == NULL)
    {
      return NO_ERROR;		/* nop */
    }

  (void) fflush (stderr);
  (void) fflush (stdout);

  fprintf (stdout, "*** Class Representation cache dump *** \n");
  fprintf (stdout, " Number of entries = %d, Number of used entries = %d\n",
	   heap_Classrepr->num_entries,
	   heap_Classrepr->num_entries - heap_Classrepr->free_list.free_cnt);

  for (cache_entry = heap_Classrepr->area, i = 0;
       i < heap_Classrepr->num_entries; cache_entry++, i++)
    {
      fprintf (stdout, " \nEntry_id %d\n", cache_entry->idx);

      rv = pthread_mutex_lock (&cache_entry->mutex);
      for (j = 0; j <= cache_entry->last_reprid; j++)
	{
	  classrepr = cache_entry->repr[j];
	  if (classrepr == NULL)
	    {
	      fprintf (stdout, ".....\n");
	      continue;
	    }
	  fprintf (stdout, " Fix count = %d, force_decache = %d\n",
		   cache_entry->fcnt, cache_entry->force_decache);

	  if (simple_dump == true)
	    {
	      fprintf (stdout, " Class_oid = %d|%d|%d, Reprid = %d\n",
		       (int) cache_entry->class_oid.volid,
		       cache_entry->class_oid.pageid,
		       (int) cache_entry->class_oid.slotid,
		       cache_entry->repr[j]->id);
	      fprintf (stdout, " Representation address = %p\n", classrepr);

	    }
	  else
	    {
	      ret = heap_classrepr_dump (&cache_entry->class_oid, classrepr);
	    }
	}

      pthread_mutex_unlock (&cache_entry->mutex);
    }

  return ret;
}
#endif /* DEBUG_CLASSREPR_CACHE */

/*
 * heap_classrepr_dump () - Dump schema of a given class representation
 *   return: NO_ERROR
 *   class_oid(in):
 *   repr(in): The class representation
 *
 * Note: Dump the class representation cache.
 */
static int
heap_classrepr_dump (THREAD_ENTRY * thread_p, FILE * fp,
		     const OID * class_oid, const OR_CLASSREP * repr)
{
  OR_ATTRIBUTE *attrepr;
  int i, k;
  char *classname;
  const char *attr_name;
  DB_VALUE def_dbvalue;
  PR_TYPE *pr_type;
  int disk_length;
  OR_BUF buf;
  bool copy;
  RECDES recdes;		/* Used to obtain attrnames */
  int ret = NO_ERROR;

  /*
   * The class is feteched to print the attribute names.
   *
   * This is needed since the name of the attributes is not contained
   * in the class representation structure.
   */

  recdes.data = NULL;
  recdes.area_size = 0;

  if (repr == NULL)
    {
      goto exit_on_error;
    }

  if (heap_get_alloc (thread_p, class_oid, &recdes) != NO_ERROR)
    {
      goto exit_on_error;
    }

  classname = heap_get_class_name (thread_p, class_oid);
  if (classname == NULL)
    {
      goto exit_on_error;
    }

  fprintf (fp, " Class-OID = %d|%d|%d, Classname = %s, reprid = %d,\n"
	   " Attrs: Tot = %d, Nfix = %d, Nvar = %d, Nshare = %d, Nclass = %d,\n"
	   " Total_length_of_fixattrs = %d,\n",
	   (int) class_oid->volid, class_oid->pageid,
	   (int) class_oid->slotid, classname, repr->id,
	   repr->n_attributes, (repr->n_attributes - repr->n_variable -
				repr->n_shared_attrs - repr->n_class_attrs),
	   repr->n_variable, repr->n_shared_attrs, repr->n_class_attrs,
	   repr->fixed_length);
  free_and_init (classname);

  fprintf (fp, " Attribute Specifications:\n");
  for (i = 0, attrepr = repr->attributes;
       i < repr->n_attributes; i++, attrepr++)
    {

      attr_name = or_get_attrname (&recdes, attrepr->id);
      if (attr_name == NULL)
	{
	  attr_name = "?????";
	}

      fprintf (fp,
	       "\n Attrid = %d, Attrname = %s, type = %s,\n"
	       " location = %d, position = %d,\n",
	       attrepr->id, attr_name, pr_type_name (attrepr->type),
	       attrepr->location, attrepr->position);

      if (!OID_ISNULL (&attrepr->classoid)
	  && !OID_EQ (&attrepr->classoid, class_oid))
	{
	  classname = heap_get_class_name (thread_p, &attrepr->classoid);
	  if (classname == NULL)
	    {
	      goto exit_on_error;
	    }
	  fprintf (fp,
		   " Inherited from Class: oid = %d|%d|%d, Name = %s\n",
		   (int) attrepr->classoid.volid,
		   attrepr->classoid.pageid,
		   (int) attrepr->classoid.slotid, classname);
	  free_and_init (classname);
	}

      if (attrepr->n_btids > 0)
	{
	  fprintf (fp, " Number of Btids = %d,\n", attrepr->n_btids);
	  for (k = 0; k < attrepr->n_btids; k++)
	    {
	      fprintf (fp, " BTID: VFID %d|%d, Root_PGID %d\n",
		       (int) attrepr->btids[k].vfid.volid,
		       attrepr->btids[k].vfid.fileid,
		       attrepr->btids[k].root_pageid);
	    }
	}

      /*
       * Dump the default value if any.
       */
      fprintf (fp, " Default disk value format:\n");
      fprintf (fp, "   length = %d, value = ",
	       attrepr->default_value.val_length);

      if (attrepr->default_value.val_length <= 0)
	{
	  fprintf (fp, "NULL");
	}
      else
	{
	  or_init (&buf, (char *) attrepr->default_value.value,
		   attrepr->default_value.val_length);
	  buf.error_abort = 1;

	  switch (_setjmp (buf.env))
	    {
	    case 0:
	      /* Do not copy the string--just use the pointer.  The pr_ routines
	       * for strings and sets have different semantics for length.
	       * A negative length value for strings means "don't copy the
	       * string, just use the pointer".
	       */

	      disk_length = attrepr->default_value.val_length;
	      copy = (pr_is_set_type (attrepr->type)) ? true : false;
	      pr_type = PR_TYPE_FROM_ID (attrepr->type);
	      if (pr_type)
		{
		  (*(pr_type->data_readval)) (&buf, &def_dbvalue,
					      attrepr->domain, disk_length,
					      copy, NULL, 0);

		  db_value_fprint (stdout, &def_dbvalue);
		  (void) pr_clear_value (&def_dbvalue);
		}
	      else
		{
		  fprintf (fp, "PR_TYPE is NULL");
		}
	      break;
	    default:
	      /*
	       * An error was found during the reading of the attribute value
	       */
	      fprintf (fp, "Error transforming the default value\n");
	      break;
	    }
	}
      fprintf (fp, "\n");
    }

  free_and_init (recdes.data);

  return ret;

exit_on_error:

  if (recdes.data)
    {
      free_and_init (recdes.data);
    }

  fprintf (fp, "Dump has been aborted...");

  return (ret == NO_ERROR) ? ER_FAILED : ret;
}

#ifdef DEBUG_CLASSREPR_CACHE
/*
 * heap_classrepr_dump_anyfixed() - Dump class representation cache if
 *                                   any entry is fixed
 *   return: NO_ERROR
 *
 * Note: The class representation cache is dumped if any cache entry is fixed
 *
 * This is a debugging function that can be used to verify if
 * entries were freed after a set of operations (e.g., a
 * transaction or a API function).
 *
 * Note:
 * This function will not give you good results when there are
 * multiple users in the system (multiprocessing). However, it
 * can be used during shuttdown.
 */
int
heap_classrepr_dump_anyfixed (void)
{
  int ret = NO_ERROR;

  if (heap_Classrepr->num_fix_entries > 0)
    {
      er_log_debug (ARG_FILE_LINE, "heap_classrepr_dump_anyfixed:"
		    " Some entries are fixed\n");
      ret = heap_classrepr_dump_cache (true);
    }

  return ret;
}
#endif /* DEBUG_CLASSREPR_CACHE */

/*
 * heap_stats_get_min_freespace () - Minimal space to consider a page for statistics
 *   return: int minspace
 *   heap_hdr(in): Current header of heap
 *
 * Note: Find the minimal space to consider to continue caching a page
 * for statistics.
 */
static int
heap_stats_get_min_freespace (HEAP_HDR_STATS * heap_hdr)
{
  int min_freespace;

  /*
   * Don't cache as a good space page if page does not have at least
   * unfill_space + one record
   */

  if (heap_hdr->estimates.num_recs > 0)
    {
      min_freespace = (int) (heap_hdr->estimates.recs_sumlen /
			     heap_hdr->estimates.num_recs);

      if (min_freespace < (OR_HEADER_SIZE + 20))
	{
	  min_freespace = OR_HEADER_SIZE + 20;	/* Assume very small records */
	}
    }
  else
    {
      min_freespace = OR_HEADER_SIZE + 20;	/* Assume very small records */
    }

  min_freespace += heap_hdr->unfill_space;

  min_freespace = MIN (min_freespace, HEAP_DROP_FREE_SPACE);

  return min_freespace;
}

/*
 * heap_stats_update () - Update one header hinted page space statistics
 *   return: NO_ERROR
 *   pgptr(in): Page pointer
 *   hfid(in): Object heap file identifier
 *   is_new_file(in): is new file
 *   prev_freespace(in):
 *
 * NOTE: There should be at least HEAP_DROP_FREE_SPACE in order to
 *       insert this page to best hint array.
 *       If we cannot fix a heap header page due to holding it by
 *       others, we will postpone this updating until next deletion.
 *       In this case, unfortunately, if some record is not deleted
 *       from this page in the future, we may not use this page until
 *       heap_stats_sync_bestspace function searches all pages.
 */
static void
heap_stats_update (THREAD_ENTRY * thread_p, PAGE_PTR pgptr, const HFID * hfid,
		   FILE_IS_NEW_FILE is_new_file, int prev_freespace)
{
  VPID *vpid;
  int freespace, error;
  bool need_update;

  freespace =
    spage_get_free_space_without_saving (thread_p, pgptr, &need_update);

  if (prm_get_integer_value (PRM_ID_HF_MAX_BESTSPACE_ENTRIES) > 0)
    {
      int rc;

      /*
       * do can not register vpid of new file.
       * because the vpid can be dealloced when rollback.
       */
      if (prev_freespace < freespace && is_new_file != FILE_NEW_FILE)
	{
	  assert (is_new_file != FILE_ERROR);
	  vpid = pgbuf_get_vpid_ptr (pgptr);
	  assert_release (vpid != NULL);

	  rc = pthread_mutex_lock (&heap_Bestspace->bestspace_mutex);

	  (void) heap_stats_add_bestspace (thread_p, hfid, vpid, freespace);

	  assert (mht_count (heap_Bestspace->vpid_ht) ==
		  mht_count (heap_Bestspace->hfid_ht));

	  pthread_mutex_unlock (&heap_Bestspace->bestspace_mutex);
	}
    }
  else
    {
      if (need_update
	  || (prev_freespace <= HEAP_DROP_FREE_SPACE
	      && freespace > HEAP_DROP_FREE_SPACE))
	{
	  vpid = pgbuf_get_vpid_ptr (pgptr);
	  assert_release (vpid != NULL);

	  error =
	    heap_stats_update_internal (thread_p, hfid, vpid, freespace);
	  if (error != NO_ERROR)
	    {
	      spage_set_need_update_best_hint (thread_p, pgptr, true);
	    }
	  else if (need_update == true)
	    {
	      spage_set_need_update_best_hint (thread_p, pgptr, false);
	    }
	}
      else if (need_update == true)
	{
	  spage_set_need_update_best_hint (thread_p, pgptr, false);
	}
    }
}

/*
 * heap_stats_update_internal () - Update one header hinted page space statistics
 *   return: NO_ERROR
 *   hfid(in): Object heap file identifier
 *   lotspace_vpid(in): Page which has a lot of free space
 *   free_space(in): The free space on the page
 *
 * Note: Update header hinted best space page information. This
 * function is used during deletions and updates when the free
 * space on the page is greater than HEAP_DROP_FREE_SPACE.
 */
static int
heap_stats_update_internal (THREAD_ENTRY * thread_p, const HFID * hfid,
			    VPID * lotspace_vpid, int free_space)
{
  HEAP_HDR_STATS *heap_hdr;	/* Header of heap structure */
  PAGE_PTR hdr_pgptr = NULL;	/* Page pointer to header page */
  VPID vpid;			/* Page-volume identifier */
  RECDES recdes;		/* Header record descriptor */
  LOG_DATA_ADDR addr;		/* Address of logging data */
  int i, best;
  FILE_IS_NEW_FILE is_new_file;
  int ret = NO_ERROR;

  is_new_file = file_is_new_file (thread_p, &(hfid->vfid));
  if (is_new_file == FILE_ERROR)
    {
      return ER_FAILED;
    }

  if (is_new_file == FILE_NEW_FILE
      && log_is_tran_in_system_op (thread_p) == true)
    {
      return NO_ERROR;
    }

  /* Retrieve the header of heap */
  vpid.volid = hfid->vfid.volid;
  vpid.pageid = hfid->hpgid;

  /*
   * We do not want to wait for the following operation.
   * So, if we cannot lock the page return.
   */
  hdr_pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			 PGBUF_CONDITIONAL_LATCH);
  if (hdr_pgptr == NULL)
    {
      /* Page is busy or other type of error */
      goto exit_on_error;
    }

  /*
   * Peek the header record to find statistics for insertion.
   * Update the statistics directly.
   */
  if (spage_get_record (hdr_pgptr, HEAP_HEADER_AND_CHAIN_SLOTID, &recdes,
			PEEK) != S_SUCCESS)
    {
      goto exit_on_error;
    }

  heap_hdr = (HEAP_HDR_STATS *) recdes.data;
  best = heap_hdr->estimates.head;

  if (free_space >= heap_stats_get_min_freespace (heap_hdr))
    {
      /*
       * We do not compare with the current stored values since these values
       * may not be accurate at all. When the given one is supposed to be
       * accurate.
       */

      /*
       * Find a good place to insert this page
       */
      for (i = 0; i < HEAP_NUM_BEST_SPACESTATS; i++)
	{
	  if (VPID_ISNULL (&heap_hdr->estimates.best[best].vpid)
	      || heap_hdr->estimates.best[best].freespace <=
	      HEAP_DROP_FREE_SPACE)
	    {
	      break;
	    }

	  best = HEAP_STATS_NEXT_BEST_INDEX (best);
	}

      if (VPID_ISNULL (&heap_hdr->estimates.best[best].vpid))
	{
	  heap_hdr->estimates.num_high_best++;
	  assert (heap_hdr->estimates.num_high_best <=
		  HEAP_NUM_BEST_SPACESTATS);
	}
      else if (heap_hdr->estimates.best[best].freespace >
	       HEAP_DROP_FREE_SPACE)
	{
	  heap_hdr->estimates.num_other_high_best++;

	  heap_stats_put_second_best (heap_hdr,
				      &heap_hdr->estimates.best[best].vpid);
	}
      /*
       * Now substitute the entry with the new information
       */

      heap_hdr->estimates.best[best].freespace = free_space;
      heap_hdr->estimates.best[best].vpid = *lotspace_vpid;

      heap_hdr->estimates.head = HEAP_STATS_NEXT_BEST_INDEX (best);

      /*
       * The changes to the statistics are not logged. They are fixed
       * automatically sooner or later
       */

      addr.vfid = &hfid->vfid;
      addr.pgptr = hdr_pgptr;
      addr.offset = HEAP_HEADER_AND_CHAIN_SLOTID;
      log_skip_logging (thread_p, &addr);
      pgbuf_set_dirty (thread_p, hdr_pgptr, FREE);
      hdr_pgptr = NULL;
    }
  else
    {
      pgbuf_unfix_and_init (thread_p, hdr_pgptr);
    }

  return ret;

exit_on_error:
  if (hdr_pgptr)
    {
      pgbuf_unfix_and_init (thread_p, hdr_pgptr);
    }

  return (ret == NO_ERROR) ? ER_FAILED : ret;
}

/*
 * heap_stats_update_all () - Update header hinted page space statistics
 *   return: NO_ERROR
 *   hfid(in): Object heap file identifier
 *   num_best(in): Number of best pages given
 *   bestspace(in): Array of best pages along with their freespace
 *   num_other_best(in): The number of other pages that are not given but are
 *                       good best space candidates.
 *   num_pages(in): Number of found heap pages.
 *   num_recs(in): Number of found heap records.
 *   recs_sumlen(in): Total length of found records.
 *
 * Note: Update header hinted best space page information.
 * This function is used during scans and compactions.
 */
static int
heap_stats_update_all (THREAD_ENTRY * thread_p, const HFID * hfid,
		       int num_best, HEAP_BESTSPACE * bestspace,
		       int num_other_best, int num_pages, int num_recs,
		       float recs_sumlen)
{
  HEAP_HDR_STATS *heap_hdr;	/* Header of heap structure    */
  PAGE_PTR hdr_pgptr = NULL;	/* Page pointer to header page */
  VPID vpid;			/* Page-volume identifier      */
  RECDES recdes;		/* Header record descriptor    */
  LOG_DATA_ADDR addr;		/* Address of logging data     */
  int i, best;
  FILE_IS_NEW_FILE is_new_file;
  int ret = NO_ERROR;

  is_new_file = file_is_new_file (thread_p, &(hfid->vfid));
  if (is_new_file == FILE_ERROR)
    {
      return ER_FAILED;
    }

  if (is_new_file == FILE_NEW_FILE
      && log_is_tran_in_system_op (thread_p) == true)
    {
      return NO_ERROR;
    }

  /* Retrieve the header of heap */
  vpid.volid = hfid->vfid.volid;
  vpid.pageid = hfid->hpgid;

  /*
   * We do not want to wait for the following operation.
   * So, if we cannot lock the page return.
   */
  hdr_pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			 PGBUF_CONDITIONAL_LATCH);
  if (hdr_pgptr == NULL)
    {
      /* Page is busy or other type of error */
      goto exit_on_error;
    }

  /*
   * Peek the header record to find statistics for insertion. Update the
   * statistics directly. This is OK, since the header page has been locked
   * in exclusive mode.
   */
  if (spage_get_record (hdr_pgptr, HEAP_HEADER_AND_CHAIN_SLOTID, &recdes,
			PEEK) != S_SUCCESS)
    {
      goto exit_on_error;
    }

  heap_hdr = (HEAP_HDR_STATS *) recdes.data;

  /* Do we need to update the best space statistics */
  if (num_best >= 0 && bestspace != NULL)
    {
      heap_hdr->estimates.num_high_best = num_best;

      best = 0;
      for (i = 0; i < num_best; i++)
	{
	  /*
	   * We do not compare with the current stored values since these values
	   * may not be accurate at all. When the given one is supposed to be
	   * accurate.
	   */

	  heap_hdr->estimates.best[best].freespace = bestspace[i].freespace;
	  heap_hdr->estimates.best[best].vpid = bestspace[i].vpid;

	  best = HEAP_STATS_NEXT_BEST_INDEX (best);
	}

      if (num_best < HEAP_NUM_BEST_SPACESTATS)
	{
	  for (i = best; i < HEAP_NUM_BEST_SPACESTATS; i++)
	    {
	      VPID_SET_NULL (&heap_hdr->estimates.best[i].vpid);
	      heap_hdr->estimates.best[i].freespace = 0;
	    }
	}
      heap_hdr->estimates.head = best;
      assert (heap_hdr->estimates.head >= 0
	      && heap_hdr->estimates.head < HEAP_NUM_BEST_SPACESTATS);
    }

  /* Do we need to update the other best space statistics */
  if (num_other_best >= 0)
    {
      heap_hdr->estimates.num_other_high_best = num_other_best;
    }

  if (num_pages >= heap_hdr->estimates.num_pages
      || num_recs == heap_hdr->estimates.num_recs)
    {
      heap_hdr->estimates.num_pages = num_pages;
      heap_hdr->estimates.num_recs = num_recs;
      heap_hdr->estimates.recs_sumlen = recs_sumlen;
    }
  else if (num_recs > heap_hdr->estimates.num_recs)
    {
      heap_hdr->estimates.num_recs = num_recs;
      heap_hdr->estimates.recs_sumlen = recs_sumlen;
    }
  else if (recs_sumlen > heap_hdr->estimates.recs_sumlen)
    {
      heap_hdr->estimates.recs_sumlen = recs_sumlen;
    }

  addr.vfid = &hfid->vfid;
  addr.pgptr = hdr_pgptr;
  addr.offset = HEAP_HEADER_AND_CHAIN_SLOTID;
  log_skip_logging (thread_p, &addr);
  pgbuf_set_dirty (thread_p, hdr_pgptr, FREE);
  hdr_pgptr = NULL;

  return ret;

exit_on_error:
  if (hdr_pgptr)
    {
      pgbuf_unfix_and_init (thread_p, hdr_pgptr);
    }

  return (ret == NO_ERROR) ? ER_FAILED : ret;
}

/*
 * heap_stats_put_second_best () - Put a free page into second best hint array
 *   return: void
 *   heap_hdr(in): Statistics of heap file
 *   vpid(in): VPID to be added
 *
 * NOTE: A free page is not always inserted to the second best hint array.
 *       Second best hints will be collected for every 1000 pages in order
 *       to increase randomness for "emptying contiguous pages" scenario.
 */
static void
heap_stats_put_second_best (HEAP_HDR_STATS * heap_hdr, VPID * vpid)
{
  int tail;

  if (heap_hdr->estimates.num_substitutions++ % 1000 == 0)
    {
      tail = heap_hdr->estimates.tail_second_best;

      heap_hdr->estimates.second_best[tail] = *vpid;
      heap_hdr->estimates.tail_second_best =
	HEAP_STATS_NEXT_BEST_INDEX (tail);

      if (heap_hdr->estimates.num_second_best == HEAP_NUM_BEST_SPACESTATS)
	{
	  assert (heap_hdr->estimates.head_second_best == tail);
	  heap_hdr->estimates.head_second_best =
	    heap_hdr->estimates.tail_second_best;
	}
      else
	{
	  assert (heap_hdr->estimates.num_second_best <
		  HEAP_NUM_BEST_SPACESTATS);
	  heap_hdr->estimates.num_second_best++;
	}

      /* If both head and tail refer to the same index, the number of second
       * best hints is HEAP_NUM_BEST_SPACESTATS(10).
       */
      assert (heap_hdr->estimates.num_second_best != 0);
      assert ((heap_hdr->estimates.tail_second_best >
	       heap_hdr->estimates.head_second_best)
	      ? ((heap_hdr->estimates.tail_second_best -
		  heap_hdr->estimates.head_second_best) ==
		 heap_hdr->estimates.num_second_best)
	      : ((10 + heap_hdr->estimates.tail_second_best -
		  heap_hdr->estimates.head_second_best) ==
		 heap_hdr->estimates.num_second_best));

      heap_hdr->estimates.num_substitutions = 1;
    }
}

/*
 * heap_stats_put_second_best () - Get a free page from second best hint array
 *   return: NO_ERROR or ER_FAILED
 *   heap_hdr(in): Statistics of heap file
 *   vpid(out): VPID to get
 */
static int
heap_stats_get_second_best (HEAP_HDR_STATS * heap_hdr, VPID * vpid)
{
  int head;

  assert (vpid != NULL);

  if (heap_hdr->estimates.num_second_best == 0)
    {
      assert (heap_hdr->estimates.tail_second_best
	      == heap_hdr->estimates.head_second_best);
      VPID_SET_NULL (vpid);
      return ER_FAILED;
    }

  head = heap_hdr->estimates.head_second_best;

  heap_hdr->estimates.num_second_best--;
  heap_hdr->estimates.head_second_best = HEAP_STATS_NEXT_BEST_INDEX (head);

  /* If both head and tail refer to the same index, the number of second
   * best hints is 0.
   */
  assert (heap_hdr->estimates.num_second_best != HEAP_NUM_BEST_SPACESTATS);
  assert ((heap_hdr->estimates.tail_second_best >=
	   heap_hdr->estimates.head_second_best)
	  ? ((heap_hdr->estimates.tail_second_best -
	      heap_hdr->estimates.head_second_best) ==
	     heap_hdr->estimates.num_second_best)
	  : ((10 + heap_hdr->estimates.tail_second_best -
	      heap_hdr->estimates.head_second_best) ==
	     heap_hdr->estimates.num_second_best));

  *vpid = heap_hdr->estimates.second_best[head];
  return NO_ERROR;
}

/*
 * heap_stats_copy_hdr_to_cache () - Cache the space statistics to scan cache
 *   return: NO_ERROR
 *   heap_hdr(in): Heap header
 *   scan_cache(in/out): Scan cache used to estimate the best space pages
 *
 * Note: Copy the space statistics from the heap header to the scan
 * cache. The scan cache statistics can be used to quickly
 * estimate pages with very good space.
 */
static int
heap_stats_copy_hdr_to_cache (HEAP_HDR_STATS * heap_hdr,
			      HEAP_SCANCACHE * scan_cache)
{
  int i;
  int nbest = 0;

  for (i = 0;
       i < scan_cache->collect_maxbest && i < HEAP_NUM_BEST_SPACESTATS; i++)
    {
      if (heap_hdr->estimates.best[i].freespace > heap_hdr->unfill_space)
	{
	  scan_cache->collect_best[nbest].freespace =
	    heap_hdr->estimates.best[i].freespace;
	  scan_cache->collect_best[nbest].vpid =
	    heap_hdr->estimates.best[i].vpid;
	  nbest++;
	}
    }

  for (i = nbest; i < scan_cache->collect_maxbest; i++)
    {
      VPID_SET_NULL (&scan_cache->collect_best[i].vpid);
      scan_cache->collect_best[i].freespace = 0;
    }

  scan_cache->unfill_space = heap_hdr->unfill_space;
  scan_cache->collect_nbest = nbest;
  /* Consider collect_maxbest < num_high_best */
  scan_cache->collect_nother_best =
    heap_hdr->estimates.num_other_high_best +
    (heap_hdr->estimates.num_high_best - nbest);
  scan_cache->pgptr = NULL;

  return NO_ERROR;
}

/*
 * heap_stats_copy_cache_to_hdr () - Update header space statistics from scan cache
 *   return: NO_ERROR
 *   heap_hdr(in/out): Heap header (Heap header page should be acquired in
 *                     exclusive mode)
 *   scan_cache(in/out): Scan cache used to estimate the best space pages
 *
 * Note: Update header hinted best space statistics from scan cache.
 */
static int
heap_stats_copy_cache_to_hdr (THREAD_ENTRY * thread_p,
			      HEAP_HDR_STATS * heap_hdr,
			      HEAP_SCANCACHE * scan_cache)
{
  int i, j, best, ncopies;
  FILE_IS_NEW_FILE is_new_file;
  int ret = NO_ERROR;

  if (scan_cache != NULL)
    {
      assert (scan_cache->is_new_file != FILE_ERROR);
      is_new_file = scan_cache->is_new_file;
    }
  else
    {
      is_new_file = file_is_new_file (thread_p, &(scan_cache->hfid.vfid));
      if (is_new_file == FILE_ERROR)
	{
	  return ER_FAILED;
	}
    }

  /*
   * Update any previous information recorded in the space cache
   */
  heap_hdr->estimates.num_recs += scan_cache->collect_nrecs;
  heap_hdr->estimates.recs_sumlen += scan_cache->collect_recs_sumlen;
  scan_cache->collect_nrecs = 0;
  scan_cache->collect_recs_sumlen = 0;

  if (is_new_file == FILE_OLD_FILE
      || log_is_tran_in_system_op (thread_p) == false)
    {
      best = 0;
      ncopies = 0;

      /*
       * Assume that all the collect pages were part of number of best and
       * number of other best. So we use "best pages hint" only. The number
       * of other best might not be accurate.
       */
      heap_hdr->estimates.num_other_high_best +=
	heap_hdr->estimates.num_high_best;
      heap_hdr->estimates.num_high_best = 0;

      for (i = 0;
	   i < scan_cache->collect_nbest
	   && ncopies < HEAP_NUM_BEST_SPACESTATS; i++)
	{
	  if (scan_cache->collect_best[i].freespace <= HEAP_DROP_FREE_SPACE)
	    {
	      heap_hdr->estimates.num_other_high_best--;
	      continue;
	    }

	  if (heap_hdr->estimates.best[best].freespace > HEAP_DROP_FREE_SPACE)
	    {
	      heap_hdr->estimates.best[best].freespace =
		scan_cache->collect_best[i].freespace;

	      heap_hdr->estimates.best[best].vpid =
		scan_cache->collect_best[i].vpid;

	      heap_hdr->estimates.num_high_best++;
	    }
	  heap_hdr->estimates.num_other_high_best--;

	  ncopies++;

	  best = HEAP_STATS_NEXT_BEST_INDEX (best);
	}

      if (heap_hdr->estimates.num_other_high_best < 0)
	{
	  heap_hdr->estimates.num_other_high_best = 0;
	}

      if (ncopies < HEAP_NUM_BEST_SPACESTATS)
	{
	  for (i = best; i < HEAP_NUM_BEST_SPACESTATS; i++)
	    {
	      VPID_SET_NULL (&heap_hdr->estimates.best[i].vpid);
	      heap_hdr->estimates.best[i].freespace = 0;
	    }
	}

      heap_hdr->estimates.head = best;
      assert (heap_hdr->estimates.head >= 0
	      && heap_hdr->estimates.head < HEAP_NUM_BEST_SPACESTATS);
    }
  else
    {
      /*
       * We will update only the currently known pages. Assume that the pages
       * are located approximately at the same index.
       */

      for (i = 0, ncopies = 0;
	   (i < scan_cache->collect_nbest
	    && ncopies < HEAP_NUM_BEST_SPACESTATS); i++)
	{
	  for (j = 0, best = i; j < HEAP_NUM_BEST_SPACESTATS; j++)
	    {
	      if (VPID_EQ (&scan_cache->collect_best[i].vpid,
			   &heap_hdr->estimates.best[best].vpid))
		{
		  heap_hdr->estimates.best[best].freespace =
		    scan_cache->collect_best[i].freespace;
		  ncopies++;
		  break;
		}

	      best = HEAP_STATS_NEXT_BEST_INDEX (best);
	    }
	}
    }

  return ret;
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * heap_stats_quick_num_fit_in_bestspace () - Guess the number of unit_size entries that
 *                                  can fit in best space
 *   return: number of units
 *   bestspace(in): Array of best pages along with their freespace
 *                  (The freespace fields may be updated as a SIDE EFFECT)
 *   num_entries(in): Number of estimated entries in best space.
 *   unit_size(in): Units of this size
 *   unfill_space(in): Unfill space on the pages
 *
 * Note: Find the number of units of "unit_size" that can fit in
 * current betsspace.
 */
static int
heap_stats_quick_num_fit_in_bestspace (HEAP_BESTSPACE * bestspace,
				       int num_entries, int unit_size,
				       int unfill_space)
{
  int total_nunits = 0;
  int i;

  if (unit_size <= 0)
    {
      return ER_FAILED;
    }

  for (i = 0; i < num_entries; i++)
    {
      if ((bestspace[i].freespace - unfill_space) >= unit_size)
	{
	  /*
	   * How many min_spaces can fit in this page
	   */
	  total_nunits += (bestspace[i].freespace - unfill_space) / unit_size;
	}
    }

  return total_nunits;
}
#endif

/*
 * heap_stats_find_page_in_bestspace () - Find a page within best space
 * 					  statistics with the needed space
 *   return: HEAP_FINDPSACE (found, not found, or error)
 *   hfid(in): Object heap file identifier
 *   bestspace(in): Array of best pages along with their freespace
 *                  (The freespace fields may be updated as a SIDE EFFECT)
 *   num_entries(in): Number of estimated entries in best space.
 *   idx_badspace(out): An index into best space with no so good space.
 *   num_high_best(in/out): Number of pages in the best space that we believe
 *                          have at least HEAP_DROP_FREE_SPACE.
 *   idx_found(out): Set to the index in the best space where space is found.
 *   needed_space(in): The needed space.
 *   scan_cache(in): Scan cache if any
 *   pgptr(out): Best page with enough space or NULL
 *
 * Note: Search for a page within the best space cache which has the
 * needed space. The free space fields of best space cache along
 * with some other index information are updated (as a side
 * effect) as the best space cache is accessed.
 */
static HEAP_FINDSPACE
heap_stats_find_page_in_bestspace (THREAD_ENTRY * thread_p,
				   const HFID * hfid,
				   HEAP_BESTSPACE * bestspace,
				   int num_entries, int *idx_badspace,
				   int *num_high_best, int *idx_found,
				   int needed_space,
				   HEAP_SCANCACHE * scan_cache,
				   PAGE_PTR * pgptr)
{
  int i;
  int idx_worstspace;
  int worstspace;
  HEAP_FINDSPACE found = HEAP_FINDSPACE_NOTFOUND;
  int old_wait_msecs;
  int bk_errid = NO_ERROR;	/* backup of previous errid */
  HEAP_STATS_ENTRY *ent;
  HEAP_BESTSPACE best;
  void *last;
  int notfound_cnt;
  int rc;

  worstspace = PGLENGTH_MAX;
  idx_worstspace = -1;

  *idx_found = 0;
  *pgptr = NULL;

  /*
   * If a page is busy, don't wait continue looking for other pages in our
   * statistics. This will improve some contentions on the heap at the
   * expenses of storage.
   */

  /* LK_FORCE_ZERO_WAIT doesn't set error when deadlock occurrs */
  old_wait_msecs = xlogtb_reset_wait_msecs (thread_p, LK_FORCE_ZERO_WAIT);

  /* backup previous error */
  bk_errid = er_errid ();
  if (bk_errid != NO_ERROR)
    {
      er_clearid ();
    }

  i = 0;

  notfound_cnt = 0;
  last = NULL;
  while (found == HEAP_FINDSPACE_NOTFOUND)
    {
      best.freespace = -1;	/* init */

      if (prm_get_integer_value (PRM_ID_HF_MAX_BESTSPACE_ENTRIES) > 0)
	{
	  rc = pthread_mutex_lock (&heap_Bestspace->bestspace_mutex);

	  do
	    {
	      ent =
		(HEAP_STATS_ENTRY *) mht_get2 (heap_Bestspace->hfid_ht,
					       hfid, &last);

	      if (ent)
		{
		  if (ent->best.freespace >= needed_space)
		    {
		      best = ent->best;
		      assert_release (best.freespace > 0);
		      break;
		    }

		  notfound_cnt++;
		}
	    }
	  while (ent);

	  pthread_mutex_unlock (&heap_Bestspace->bestspace_mutex);

	  if (best.freespace < 0)
	    {
	      break;		/* end of hash; exit loop */
	    }
	}
      else
	{
	  if (i >= num_entries)
	    {
	      break;		/* exit loop */
	    }

	  if (bestspace[*idx_found].freespace >= needed_space)
	    {
	      best.vpid = bestspace[*idx_found].vpid;
	      best.freespace = bestspace[*idx_found].freespace;
	      assert_release (best.freespace > 0);
	    }
	}

      if (best.freespace > 0)
	{
	  *pgptr = heap_scan_pb_lock_and_fetch (thread_p, &best.vpid,
						OLD_PAGE, X_LOCK, scan_cache);
	  if (*pgptr == NULL)
	    {
	      /*
	       * Either we timeout and we want to continue in this case, or
	       * we have another kind of problem.
	       */
	      switch (er_errid ())
		{
		case NO_ERROR:
		  /*
		   * may return with LOCK_RESUMED_TIMEOUT, but it's not an error
		   */
		  break;

		case ER_LK_PAGE_TIMEOUT:
		  /*
		   * The page is busy, continue instead of waiting.
		   */
		  er_log_debug (ARG_FILE_LINE,
				"heap_stats_find_page_in_bestspace: "
				"heap_scan_pb_lock_and_fetch() "
				"vpid {pagid %d volid %d} "
				"returned ER_LK_PAGE_TIMEOUT",
				best.vpid.pageid, best.vpid.volid);
		  break;

		case ER_INTERRUPTED:
		  found = HEAP_FINDSPACE_ERROR;
		  break;

		default:
		  /*
		   * Something went wrong, we are unable to fetch this page.
		   * Set its space to zero and finish
		   */
		  bestspace[*idx_found].freespace = 0;
		  found = HEAP_FINDSPACE_ERROR;
		  break;
		}
	    }
	  else
	    {
	      /* just refresh the free space of the page */
	      bestspace[*idx_found].freespace =
		spage_max_space_for_new_record (thread_p, *pgptr);

	      if (bestspace[*idx_found].freespace >= needed_space)
		{
		  found = HEAP_FINDSPACE_FOUND;
		}
	      else
		{
		  if (prm_get_integer_value (PRM_ID_HF_MAX_BESTSPACE_ENTRIES)
		      > 0)
		    {
		      /* rest freespace info */
		      rc =
			pthread_mutex_lock (&heap_Bestspace->bestspace_mutex);

		      ent =
			(HEAP_STATS_ENTRY *) mht_get (heap_Bestspace->
						      vpid_ht, &best.vpid);
		      if (ent)
			{
			  ent->best.freespace =
			    bestspace[*idx_found].freespace;

			  notfound_cnt++;
			}

		      pthread_mutex_unlock (&heap_Bestspace->bestspace_mutex);
		    }

		  pgbuf_unfix_and_init (thread_p, *pgptr);
		}
	    }
	}

      if (worstspace > bestspace[*idx_found].freespace)
	{
	  idx_worstspace = *idx_found;
	  worstspace = bestspace[*idx_found].freespace;
	}

      if (found == HEAP_FINDSPACE_NOTFOUND)
	{
	  *idx_found = HEAP_STATS_NEXT_BEST_INDEX (*idx_found);
	}

      i++;
    }

  /*
   * Reset back the timeout value of the transaction
   */
  (void) xlogtb_reset_wait_msecs (thread_p, old_wait_msecs);

  if (bk_errid != NO_ERROR && er_errid () == NO_ERROR)
    {
      /* restore previous error */
      er_setid (bk_errid);
    }

  /*
   * Set the idx_badspace to the index with the smallest free space
   * which may not be accurate. This is used for future lookups (where to
   * start) into the findbest space ring.
   */
  if (idx_worstspace >= 0)
    {
      *idx_badspace = idx_worstspace;
    }

  if (found != HEAP_FINDSPACE_FOUND)
    {
      *idx_found = -1;

      notfound_cnt = INT_MAX;	/* delete all */
    }

  if (prm_get_integer_value (PRM_ID_HF_MAX_BESTSPACE_ENTRIES) > 0)
    {
      if (notfound_cnt > 0)
	{
	  rc = pthread_mutex_lock (&heap_Bestspace->bestspace_mutex);

	  (void) heap_stats_del_bestspace (thread_p, hfid, needed_space,
					   notfound_cnt);

	  assert (mht_count (heap_Bestspace->vpid_ht) ==
		  mht_count (heap_Bestspace->hfid_ht));

	  pthread_mutex_unlock (&heap_Bestspace->bestspace_mutex);
	}
    }

  return found;
}

/*
 * heap_stats_find_best_page () - Find a page with the needed space.
 *   return: pointer to page with enough space or NULL
 *   hfid(in): Object heap file identifier
 *   needed_space(in): The minimal space needed
 *   isnew_rec(in): Are we inserting a new record to the heap ?
 *   newrec_size(in): Size of the new record
 *   scan_cache(in/out): Scan cache used to estimate the best space pages
 *
 * Note: Find a page among the set of best pages of the heap which has
 * the needed space. If we do not find any page, a new page is
 * allocated. The heap header and the scan cache may be updated
 * as a side effect to reflect more accurate space on some of the
 * set of best pages.
 */
static PAGE_PTR
heap_stats_find_best_page (THREAD_ENTRY * thread_p, const HFID * hfid,
			   int needed_space, bool isnew_rec, int newrec_size,
			   HEAP_SCANCACHE * scan_cache)
{
  VPID vpid;			/* Volume and page identifiers */
  PAGE_PTR pgptr = NULL;	/* The page with the best space */
  LOG_DATA_ADDR addr_hdr;	/* Address of logging data */
  RECDES hdr_recdes;		/* Record descriptor to point to
				 * space statistics
				 */
  HEAP_HDR_STATS *heap_hdr;	/* Heap header */
  VPID *hdr_vpidp;
  int idx_found;
  int total_space;
  int try_find, try_sync;
  int num_pages_found;
  FILE_IS_NEW_FILE is_new_file = FILE_ERROR;

  /*
   * Try to use the space cache for as much information as possible to avoid
   * fetching and updating the header page a lot.
   */

  assert ((scan_cache == NULL)
	  || (scan_cache && scan_cache->cache_last_fix_page == false));

  /*
   * Get the heap header in exclusive mode since it is going to be changed.
   *
   * Note: to avoid any possibilities of deadlocks, I should not have any locks
   *       on the heap at this moment.
   *       That is, we must assume that locking the header of the heap in
   *       exclusive mode, the rest of the heap is locked.
   */

  vpid.volid = hfid->vfid.volid;
  vpid.pageid = hfid->hpgid;

  addr_hdr.vfid = &hfid->vfid;
  addr_hdr.offset = HEAP_HEADER_AND_CHAIN_SLOTID;

  addr_hdr.pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			      PGBUF_UNCONDITIONAL_LATCH);
  if (addr_hdr.pgptr == NULL)
    {
      /* something went wrong. Unable to fetch header page */
      return NULL;
    }

  if (spage_get_record (addr_hdr.pgptr, HEAP_HEADER_AND_CHAIN_SLOTID,
			&hdr_recdes, PEEK) != S_SUCCESS)
    {
      pgbuf_unfix_and_init (thread_p, addr_hdr.pgptr);
      return NULL;
    }

  heap_hdr = (HEAP_HDR_STATS *) hdr_recdes.data;

  if (isnew_rec == true)
    {
      heap_hdr->estimates.num_recs += 1;
      if (newrec_size > DB_PAGESIZE)
	{
	  heap_hdr->estimates.num_pages += CEIL_PTVDIV (newrec_size,
							DB_PAGESIZE);
	}
    }
  heap_hdr->estimates.recs_sumlen += (float) newrec_size;

  /* Take into consideration the unfill factor for pages with objects */
  total_space = needed_space + heap_Slotted_overhead + heap_hdr->unfill_space;

  try_find = 0;
  while (true)
    {
      try_find++;
      pgptr = NULL;
      if (heap_stats_find_page_in_bestspace (thread_p, hfid,
					     heap_hdr->estimates.best,
					     HEAP_NUM_BEST_SPACESTATS,
					     &(heap_hdr->estimates.head),
					     &(heap_hdr->estimates.
					       num_high_best), &idx_found,
					     total_space, scan_cache,
					     &pgptr) == HEAP_FINDSPACE_ERROR)
	{
	  pgbuf_unfix_and_init (thread_p, addr_hdr.pgptr);
	  return NULL;
	}

      if (prm_get_integer_value (PRM_ID_HF_MAX_BESTSPACE_ENTRIES) > 0)
	{
	  break;
	}

      if (pgptr != NULL)
	{
	  /* found the page */
	  break;
	}

      if (try_find >= 2
	  || (heap_hdr->estimates.num_other_high_best <= 0
	      && heap_hdr->estimates.num_second_best <= 0))
	{
	  /* We stop to find free pages if:
	   * (1) we have tried to do it twice
	   * (2) it is first trying but we have no hints
	   * Regarding (2), we will find free pages by heap_stats_sync_bestspace
	   * only if we know that a free page exists somewhere.
	   * num_other_high_best means the number of free pages existing somewhere
	   * in the heap file.
	   */
	  break;
	}

      /*
       * The followings will try to find free pages and fill best hints with them.
       */

      if (scan_cache != NULL)
	{
	  assert (scan_cache->is_new_file != FILE_ERROR);
	  is_new_file = scan_cache->is_new_file;
	}
      else
	{
	  is_new_file = file_is_new_file (thread_p, &(hfid->vfid));
	}
      if (is_new_file == FILE_NEW_FILE
	  && log_is_tran_in_system_op (thread_p) == true)
	{
	  log_append_undo_data (thread_p, RVHF_STATS, &addr_hdr,
				sizeof (*heap_hdr), heap_hdr);
	}

      hdr_vpidp = pgbuf_get_vpid_ptr (addr_hdr.pgptr);

      try_sync = 0;
      do
	{
	  try_sync++;
	  er_log_debug (ARG_FILE_LINE, "heap_stats_find_best_page: "
			"call heap_stats_sync_bestspace() "
			"hfid { vfid  { fileid %d volid %d } hpgid %d } "
			"hdr_vpid { pageid %d volid %d } "
			"scan_all %d ",
			hfid->vfid.fileid, hfid->vfid.volid,
			hfid->hpgid, hdr_vpidp->pageid, hdr_vpidp->volid, 0);

	  num_pages_found =
	    heap_stats_sync_bestspace (thread_p, hfid, heap_hdr,
				       hdr_vpidp, false, true);
	  if (num_pages_found < 0)
	    {
	      pgbuf_unfix_and_init (thread_p, addr_hdr.pgptr);
	      return NULL;
	    }
	}
      while (num_pages_found == 0 && try_sync <= 2);

      /* If we cannot find free pages, give up. */
      if (num_pages_found <= 0)
	{
	  break;
	}
    }

  if (pgptr != NULL)
    {
      /*
       * Decrement by only the amount space needed by the caller. Don't
       * include the unfill factor
       */
      heap_hdr->estimates.best[idx_found].freespace -= (needed_space +
							heap_Slotted_overhead);
    }
  else
    {
      /*
       * None of the best pages has the needed space, allocate a new page.
       * Set the head to the index with the smallest free space, which may not
       * be accurate.
       */

      if (scan_cache != NULL)
	{
	  assert (scan_cache->is_new_file != FILE_ERROR);
	  is_new_file = scan_cache->is_new_file;
	}
      else
	{
	  is_new_file = file_is_new_file (thread_p, &(hfid->vfid));
	}
      if (is_new_file == FILE_ERROR)
	{
	  assert_release (false);
	  pgbuf_unfix_and_init (thread_p, addr_hdr.pgptr);
	  return NULL;
	}

      if (is_new_file == FILE_NEW_FILE
	  && log_is_tran_in_system_op (thread_p) == true)
	{
	  log_append_undo_data (thread_p, RVHF_STATS, &addr_hdr,
				sizeof (*heap_hdr), heap_hdr);
	}

      pgptr = heap_vpid_alloc (thread_p, hfid, addr_hdr.pgptr, heap_hdr,
			       total_space, scan_cache, is_new_file);
      assert (pgptr != NULL
	      || er_errid () == ER_INTERRUPTED
	      || er_errid () == ER_FILE_NOT_ENOUGH_PAGES_IN_DATABASE);
    }

  if (scan_cache != NULL)
    {
      /*
       * Update the space cache information to avoid reading the header page
       * at a later point.
       */
      if (heap_stats_copy_hdr_to_cache (heap_hdr, scan_cache) != NO_ERROR)
	{
	  pgbuf_unfix_and_init (thread_p, addr_hdr.pgptr);
	  return NULL;
	}
    }

  log_skip_logging (thread_p, &addr_hdr);
  pgbuf_set_dirty (thread_p, addr_hdr.pgptr, FREE);
  addr_hdr.pgptr = NULL;

  return pgptr;
}

/*
 * heap_stats_sync_bestspace () - Synchronize the statistics of best space
 *   return: the number of pages found
 *   hfid(in): Heap file identifier
 *   heap_hdr(in): Heap header (Heap header page should be acquired in
 *                 exclusive mode)
 *   hdr_vpid(in):
 *   scan_all(in): Scan the whole heap or stop after HEAP_NUM_BEST_SPACESTATS
 *                best pages have been found.
 *   can_cycle(in): True, it allows to go back to beginning of the heap.
 *                 FALSE, don't go back to beginning of the heap. FALSE is used
 *                 when it is known that there is not free space at the
 *                 beginning of heap. For example, it can be used when we
 *                 pre-allocate. pages
 *
 * Note: Synchronize for best space, so that we can reuse heap space as
 * much as possible.
 *
 * Note: This function does not do any logging.
 */
static int
heap_stats_sync_bestspace (THREAD_ENTRY * thread_p, const HFID * hfid,
			   HEAP_HDR_STATS * heap_hdr, VPID * hdr_vpid,
			   bool scan_all, bool can_cycle)
{
  int i, best, num_high_best, num_other_best, start_pos;
  PAGE_PTR pgptr = NULL;
  VPID vpid = { NULL_PAGEID, NULL_VOLID };
  VPID start_vpid = { NULL_PAGEID, NULL_VOLID };
  VPID next_vpid = { NULL_PAGEID, NULL_VOLID };
  VPID stopat_vpid = { NULL_PAGEID, NULL_VOLID };
  int num_pages = 0;
  int num_recs = 0;
  float recs_sumlen = 0.0;
  int free_space = 0;
  int min_freespace;
  int ret = NO_ERROR;
  int npages, nrecords, rec_length;
  int num_iterations = 0, max_iterations;
  HEAP_BESTSPACE *best_pages_hint_p;
  bool iterate_all = false;
  bool search_all = false;
#if defined (CUBRID_DEBUG)
  struct timeval s_tv, e_tv;
  float elapsed;

  gettimeofday (&s_tv, NULL);
#endif /* CUBRID_DEBUG */

  min_freespace = heap_stats_get_min_freespace (heap_hdr);

  best = 0;
  num_high_best = num_other_best = 0;

  if (scan_all != true)
    {
      if (heap_hdr->estimates.num_high_best > 0)
	{
	  /* Use recently inserted one first. */
	  start_pos = HEAP_STATS_PREV_BEST_INDEX (heap_hdr->estimates.head);
	  for (i = 0; i < HEAP_NUM_BEST_SPACESTATS; i++)
	    {
	      if (!VPID_ISNULL (&heap_hdr->estimates.best[start_pos].vpid))
		{
		  next_vpid = heap_hdr->estimates.best[start_pos].vpid;
		  start_vpid = next_vpid;
		  break;
		}

	      start_pos = HEAP_STATS_PREV_BEST_INDEX (start_pos);
	    }
	}
      else
	{
	  /* If there are hint pages in second best array,
	   * we will try to use it first.
	   * Otherwise, we will search all pages in the file.
	   */
	  if (heap_hdr->estimates.num_second_best > 0)
	    {
	      if (heap_stats_get_second_best (heap_hdr, &next_vpid) !=
		  NO_ERROR)
		{
		  /* This should not be happened. */
		  assert (false);
		  search_all = true;
		}
	    }
	  else
	    {
	      search_all = true;
	    }

	  if (search_all == true)
	    {
	      assert (VPID_ISNULL (&next_vpid));
	      next_vpid = heap_hdr->estimates.full_search_vpid;
	    }

	  start_vpid = next_vpid;
	  start_pos = -1;
	}

      if (can_cycle == true)
	{
	  stopat_vpid = next_vpid;
	}
    }

  if (VPID_ISNULL (&next_vpid))
    {
      /*
       * Start from beginning of heap due to lack of statistics.
       */
      next_vpid.volid = hfid->vfid.volid;
      next_vpid.pageid = hfid->hpgid;
      start_vpid = next_vpid;
      start_pos = -1;
      can_cycle = false;
    }

  /*
   * Note that we do not put any locks on the pages that we are scanning
   * since the best space array is only used for hints, and it is OK
   * if it is a little bit wrong.
   */
  best_pages_hint_p = heap_hdr->estimates.best;

  num_iterations = 0;
  max_iterations = MIN ((int) (heap_hdr->estimates.num_pages * 0.2),
			heap_Find_best_page_limit);
  max_iterations = MAX (max_iterations, HEAP_NUM_BEST_SPACESTATS);

  while (!VPID_ISNULL (&next_vpid) || can_cycle == true)
    {
      if (can_cycle == true && VPID_ISNULL (&next_vpid))
	{
	  /*
	   * Go back to beginning of heap looking for good pages with a lot of
	   * free space
	   */
	  next_vpid.volid = hfid->vfid.volid;
	  next_vpid.pageid = hfid->hpgid;
	  can_cycle = false;
	}

      while ((scan_all == true || num_high_best < HEAP_NUM_BEST_SPACESTATS)
	     && !VPID_ISNULL (&next_vpid)
	     && (can_cycle == true || !VPID_EQ (&next_vpid, &stopat_vpid)))
	{
	  if (scan_all == false)
	    {
	      if (++num_iterations > max_iterations)
		{
		  er_log_debug (ARG_FILE_LINE, "heap_stats_sync_bestspace: "
				"num_iterations %d best %d "
				"next_vpid { pageid %d volid %d }\n",
				num_iterations, num_high_best,
				next_vpid.pageid, next_vpid.volid);

		  /* TODO: Do we really need to update the last scanned */
		  /*       in case we found less than 10 pages. */
		  /*       It is obivous we didn't find any pages. */
		  if (start_pos != -1 && num_high_best == 0)
		    {
		      /* Delete a starting VPID. */
		      VPID_SET_NULL (&best_pages_hint_p[start_pos].vpid);
		      best_pages_hint_p[start_pos].freespace = 0;

		      assert (heap_hdr->estimates.num_high_best > 0);
		      heap_hdr->estimates.num_high_best--;
		    }
		  iterate_all = true;
		  break;
		}
	    }

	  vpid = next_vpid;
	  pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ,
			     PGBUF_UNCONDITIONAL_LATCH);
	  if (pgptr == NULL)
	    {
	      break;
	    }

	  ret = heap_vpid_next (hfid, pgptr, &next_vpid);
	  if (ret != NO_ERROR)
	    {
	      pgbuf_unfix_and_init (thread_p, pgptr);
	      break;
	    }

	  spage_collect_statistics (pgptr, &npages, &nrecords, &rec_length);

	  num_pages += npages;
	  num_recs += nrecords;
	  recs_sumlen += rec_length;

	  free_space = spage_max_space_for_new_record (thread_p, pgptr);

	  if (free_space >= min_freespace
	      && free_space > HEAP_DROP_FREE_SPACE)
	    {
	      if (num_high_best < HEAP_NUM_BEST_SPACESTATS)
		{
		  best_pages_hint_p[best].vpid = vpid;
		  best_pages_hint_p[best].freespace = free_space;

		  best = HEAP_STATS_NEXT_BEST_INDEX (best);
		  num_high_best++;
		}
	      else
		{
		  num_other_best++;
		}
	    }

	  pgbuf_unfix_and_init (thread_p, pgptr);
	}

      if (scan_all == false
	  && (iterate_all == true || num_high_best == HEAP_NUM_BEST_SPACESTATS
	      || (can_cycle == false && VPID_EQ (&next_vpid, &stopat_vpid))))
	{
	  if (search_all)
	    {
	      /* Save the last position to be searched next time. */
	      heap_hdr->estimates.full_search_vpid = next_vpid;
	    }
	  break;
	}

      VPID_SET_NULL (&next_vpid);
    }

  er_log_debug (ARG_FILE_LINE, "heap_stats_sync_bestspace: "
		"scans from {%d|%d} to {%d|%d}, num_iterations(%d) "
		"max_iterations(%d) num_high_best(%d)\n",
		start_vpid.volid, start_vpid.pageid, vpid.volid, vpid.pageid,
		num_iterations, max_iterations, num_high_best);

  /* If we have scanned all pages, we should update all statistics
   * even if we have not found any hints.
   * This logic is used to handle "select count(*) from table".
   */
  if (scan_all == false
      && num_high_best == 0 && heap_hdr->estimates.num_second_best == 0)
    {
      return 0;
    }

  if (num_high_best < HEAP_NUM_BEST_SPACESTATS)
    {
      for (i = best; i < HEAP_NUM_BEST_SPACESTATS; i++)
	{
	  VPID_SET_NULL (&best_pages_hint_p[i].vpid);
	  best_pages_hint_p[i].freespace = 0;
	}
    }

  heap_hdr->estimates.head = best;	/* reinit */
  heap_hdr->estimates.num_high_best = num_high_best;
  assert (heap_hdr->estimates.head >= 0
	  && heap_hdr->estimates.head < HEAP_NUM_BEST_SPACESTATS
	  && heap_hdr->estimates.num_high_best <= HEAP_NUM_BEST_SPACESTATS);

  if (scan_all == true || heap_hdr->estimates.num_pages <= num_pages)
    {
      /*
       * We scan the whole heap.
       * Reset its statistics with new found statistics
       */
      heap_hdr->estimates.num_other_high_best = num_other_best;
      heap_hdr->estimates.num_pages = num_pages;
      heap_hdr->estimates.num_recs = num_recs;
      heap_hdr->estimates.recs_sumlen = recs_sumlen;
    }
  else
    {
      /*
       * We did not scan the whole heap.
       * We reset only some of its statistics since we do not have any idea
       * which ones are better the ones that are currently recorded or the ones
       * just found.
       */
      heap_hdr->estimates.num_other_high_best -=
	heap_hdr->estimates.num_high_best;

      if (heap_hdr->estimates.num_other_high_best < num_other_best)
	{
	  heap_hdr->estimates.num_other_high_best = num_other_best;
	}

      if (num_recs > heap_hdr->estimates.num_recs
	  || recs_sumlen > heap_hdr->estimates.recs_sumlen)
	{
	  heap_hdr->estimates.num_pages = num_pages;
	  heap_hdr->estimates.num_recs = num_recs;
	  heap_hdr->estimates.recs_sumlen = recs_sumlen;
	}
    }

#if defined (CUBRID_DEBUG)
  gettimeofday (&e_tv, NULL);
  elapsed = (float) (e_tv.tv_sec - s_tv.tv_sec) * 1000000;
  elapsed += (float) (e_tv.tv_usec - s_tv.tv_usec);
  elapsed /= 1000000;
  er_log_debug (ARG_FILE_LINE,
		"heap_stats_sync_bestspace: elapsed time %.6f", elapsed);
#endif /* CUBRID_DEBUG */

  return num_high_best;
}

/*
 * heap_get_last_page () - Get the last page pointer.
 *   return: PAGE_PTR
 *   hfid(in): Object heap file identifier
 *   heap_hdr(in): The heap header structure
 *   scan_cache(in): Scan cache
 *   last_vpid(out): VPID of the last page
 *
 * Note: The last vpid is saved on heap header. But we do not write log
 *       related to it. So, if the server stops unintentionally, heap header
 *       may have a wrong value of last vpid. This function will protect it.
 *
 *       The page pointer should be unfixed by the caller.
 *
 */
static PAGE_PTR
heap_get_last_page (THREAD_ENTRY * thread_p, const HFID * hfid,
		    HEAP_HDR_STATS * heap_hdr, HEAP_SCANCACHE * scan_cache,
		    VPID * last_vpid)
{
  PAGE_PTR pgptr = NULL;
  RECDES recdes;
  HEAP_CHAIN *chain;

  assert (last_vpid != NULL);
  assert (!VPID_ISNULL (&heap_hdr->estimates.last_vpid));

  *last_vpid = heap_hdr->estimates.last_vpid;

  pgptr = heap_scan_pb_lock_and_fetch (thread_p, last_vpid, OLD_PAGE, X_LOCK,
				       scan_cache);
  if (pgptr == NULL)
    {
      goto exit_on_error;
    }

  if (spage_get_record (pgptr, HEAP_HEADER_AND_CHAIN_SLOTID, &recdes, PEEK) !=
      S_SUCCESS)
    {
      assert (false);
      goto exit_on_error;
    }

  chain = (HEAP_CHAIN *) recdes.data;
  if (!VPID_ISNULL (&chain->next_vpid))
    {
      /*
       * Hint is wrong! We should update last_vpid on heap header.
       */
      pgbuf_unfix_and_init (thread_p, pgptr);

      if (file_find_last_page (thread_p, &hfid->vfid, last_vpid) == NULL)
	{
	  assert (false);
	  goto exit_on_error;
	}

      er_log_debug (ARG_FILE_LINE,
		    "heap_get_last_page: update last vpid from %d|%d to %d|%d\n",
		    heap_hdr->estimates.last_vpid.volid,
		    heap_hdr->estimates.last_vpid.pageid,
		    last_vpid->volid, last_vpid->pageid);

      heap_hdr->estimates.last_vpid = *last_vpid;

      /*
       * Fix a real last page.
       */
      pgptr =
	heap_scan_pb_lock_and_fetch (thread_p, last_vpid, OLD_PAGE, X_LOCK,
				     scan_cache);
      if (pgptr == NULL)
	{
	  goto exit_on_error;
	}
    }
  assert (!VPID_ISNULL (last_vpid));
  assert (pgptr != NULL);
  return pgptr;

exit_on_error:

  if (pgptr != NULL)
    {
      pgbuf_unfix (thread_p, pgptr);
    }

  VPID_SET_NULL (last_vpid);

  return NULL;
}

/*
 * heap_link_to_new () - Chain previous last page to new page
 *   return: bool
 *   vfid(in): File where the new page belongs
 *   new_vpid(in): The new page
 *   link(in): Specifications of previous and header page
 *
 * Note: Link previous page with newly created page.
 */
static bool
heap_link_to_new (THREAD_ENTRY * thread_p, const VFID * vfid,
		  const VPID * new_vpid, HEAP_CHAIN_TOLAST * link)
{
  LOG_DATA_ADDR addr;
  HEAP_CHAIN chain;
  RECDES recdes;
  int sp_success;

  /*
   * Now, Previous page should point to newly allocated page
   */

  /*
   * Update chain next field of previous last page
   * If previous best1 space page is the heap header page, it contains a heap
   * header instead of a chain.
   */

  addr.vfid = vfid;
  addr.offset = HEAP_HEADER_AND_CHAIN_SLOTID;

  if (link->hdr_pgptr == link->last_pgptr)
    {
      /*
       * Previous last page is the heap header page. Update the next_pageid
       * field.
       */

      addr.pgptr = link->hdr_pgptr;


      if (log_is_tran_in_system_op (thread_p) == true)
	{
	  log_append_undo_data (thread_p, RVHF_STATS, &addr,
				sizeof (*(link->heap_hdr)), link->heap_hdr);
	}

      link->heap_hdr->next_vpid = *new_vpid;

      log_append_redo_data (thread_p, RVHF_STATS, &addr,
			    sizeof (*(link->heap_hdr)), link->heap_hdr);

      pgbuf_set_dirty (thread_p, link->hdr_pgptr, DONT_FREE);
    }
  else
    {
      /*
       * Chain the old page to the newly allocated last page.
       */

      addr.pgptr = link->last_pgptr;

      recdes.area_size = recdes.length = sizeof (chain);
      recdes.type = REC_HOME;
      recdes.data = (char *) &chain;

      /* Get the chain record and put it in recdes...which points to chain */

      if (spage_get_record (addr.pgptr, HEAP_HEADER_AND_CHAIN_SLOTID, &recdes,
			    COPY) != S_SUCCESS)
	{
	  /* Unable to obtain chain record */
	  return false;		/* Initialization has failed */
	}


      if (log_is_tran_in_system_op (thread_p) == true)
	{
	  log_append_undo_data (thread_p, RVHF_CHAIN, &addr, recdes.length,
				recdes.data);
	}

      chain.next_vpid = *new_vpid;
      sp_success = spage_update (thread_p, addr.pgptr,
				 HEAP_HEADER_AND_CHAIN_SLOTID, &recdes);
      if (sp_success != SP_SUCCESS)
	{
	  /*
	   * This looks like a system error: size did not change, so why did
	   * it fail?
	   */
	  if (sp_success != SP_ERROR)
	    {
	      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_HEAP_CANNOT_UPDATE_CHAIN_HDR, 4,
		      pgbuf_get_volume_id (addr.pgptr),
		      pgbuf_get_page_id (addr.pgptr), vfid->fileid,
		      pgbuf_get_volume_label (addr.pgptr));
	    }
	  return false;		/* Initialization has failed */
	}

      log_append_redo_data (thread_p, RVHF_CHAIN, &addr, recdes.length,
			    recdes.data);
      pgbuf_set_dirty (thread_p, addr.pgptr, DONT_FREE);
    }

  return true;
}

/*
 * heap_vpid_init_new () - Initialize a newly allocated heap page
 *   return: bool
 *   vfid(in): File where the new page belongs
 *   file_type(in):
 *   new_vpid(in): The new page
 *   ignore_npages(in): Number of contiguous allocated pages
 *                      (Ignored in this function. We allocate only one page)
 *   xlink(in): Chain to next and previous page
 */
static bool
heap_vpid_init_new (THREAD_ENTRY * thread_p, const VFID * vfid,
		    const FILE_TYPE file_type, const VPID * new_vpid,
		    INT32 ignore_npages, void *xlink)
{
  LOG_DATA_ADDR addr;
  HEAP_CHAIN_TOLAST *link;
  HEAP_CHAIN chain;
  RECDES recdes;
  INT16 slotid;
  int sp_success;

  link = (HEAP_CHAIN_TOLAST *) xlink;

  assert (link->heap_hdr != NULL);

  addr.vfid = vfid;
  addr.offset = -1;		/* No header slot is initialized */

  /*
   * fetch and initialize the new page. This page should point to previous
   * page.
   */

  addr.pgptr = pgbuf_fix (thread_p, new_vpid, NEW_PAGE, PGBUF_LATCH_WRITE,
			  PGBUF_UNCONDITIONAL_LATCH);
  if (addr.pgptr == NULL)
    {
      return false;		/* Initialization has failed */
    }

  /* Initialize the page and chain it with the previous last allocated page */
  spage_initialize (thread_p, addr.pgptr, heap_get_spage_type (),
		    HEAP_MAX_ALIGN, SAFEGUARD_RVSPACE);

  /*
   * Add a chain record.
   * Next to NULL and Prev to last allocated page
   */
  COPY_OID (&chain.class_oid, &(link->heap_hdr->class_oid));
  pgbuf_get_vpid (link->last_pgptr, &chain.prev_vpid);
  VPID_SET_NULL (&chain.next_vpid);

  recdes.area_size = recdes.length = sizeof (chain);
  recdes.type = REC_HOME;
  recdes.data = (char *) &chain;

  sp_success = spage_insert (thread_p, addr.pgptr, &recdes, &slotid);
  if (sp_success != SP_SUCCESS || slotid != HEAP_HEADER_AND_CHAIN_SLOTID)
    {
      /*
       * Initialization has failed !!
       */
      if (sp_success != SP_SUCCESS)
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR,
		  0);
	}
      pgbuf_unfix_and_init (thread_p, addr.pgptr);
      return false;		/* Initialization has failed */
    }

  /*
   * We don't need to log before images for undos since allocation of pages is
   * an operation-destiny which does not depend on the transaction except for
   * newly created files. Pages may be shared by multiple concurrent
   * transactions, thus the deallocation cannot be undone. Note that new files
   * and their pages are deallocated when the transactions that create the
   * files are aborted.
   */

  log_append_redo_data (thread_p, heap_is_reusable_oid (file_type) ?
			RVHF_NEWPAGE_REUSE_OID : RVHF_NEWPAGE, &addr,
			recdes.length, recdes.data);
  pgbuf_set_dirty (thread_p, addr.pgptr, FREE);
  addr.pgptr = NULL;

  /*
   * Now, link previous page to newly allocated page
   */
  return heap_link_to_new (thread_p, vfid, new_vpid, link);
}

#define HEAP_GET_NPAGES 5

/*
 * heap_vpid_init_newset () - Initialize a set of allocated pages
 *   return: bool
 *   vfid(in): File where the new page belongs
 *   first_alloc_vpid(in): First allocated page identifier
 *   first_alloc_nth(in): nth page of first allocated page
 *   npages(in): number of allocated pages
 *   ptrs_xlink_scancache(in):
 */
static bool
heap_vpid_init_newset (THREAD_ENTRY * thread_p, const VFID * vfid,
		       const FILE_TYPE file_type,
		       const VPID * first_alloc_vpid,
		       const INT32 * first_alloc_nth, INT32 npages,
		       void *ptrs_xlink_scancache)
{
  void **xlink_scancache;
  HEAP_SCANCACHE *scan_cache;
  HEAP_CHAIN_TOLAST *link;
  HEAP_CHAIN chain;
  LOG_DATA_ADDR addr;
  RECDES recdes;
  INT16 slotid;
  VPID alloc_vpids[HEAP_GET_NPAGES];	/* Go HEAP_GET_NPAGES at a time */
  int alloc_vpids_index = HEAP_GET_NPAGES;
  int sp_success;
  int i;

  xlink_scancache = (void **) ptrs_xlink_scancache;
  link = (HEAP_CHAIN_TOLAST *) (xlink_scancache[0]);
  scan_cache = (HEAP_SCANCACHE *) (xlink_scancache[1]);

  assert (link->heap_hdr != NULL);

  addr.vfid = vfid;
  addr.offset = -1;		/* No header slot is initialized */
  COPY_OID (&chain.class_oid, &(link->heap_hdr->class_oid));

  recdes.area_size = recdes.length = sizeof (chain);
  recdes.type = REC_HOME;
  recdes.data = (char *) &chain;

  pgbuf_get_vpid (link->last_pgptr, &chain.prev_vpid);

  for (i = 0; i < HEAP_GET_NPAGES; i++)
    {
      alloc_vpids[i].volid = NULL_VOLID;
      alloc_vpids[i].pageid = NULL_PAGEID;
    }

  /*
   * fetch and initialize the new pages. Each page should point to previous
   * and next page.
   */

  for (i = 0; i < npages; i++)
    {
      /*
       * Make sure that I can access the current and next page (if any)
       */
      if ((alloc_vpids_index + 1) >= HEAP_GET_NPAGES)
	{
	  /* Get the next set of allocated pages */
	  if (file_find_nthpages (thread_p, vfid, &alloc_vpids[0],
				  *first_alloc_nth + i,
				  ((npages - i) > HEAP_GET_NPAGES ?
				   HEAP_GET_NPAGES : npages - i)) == -1)
	    {
	      return false;
	    }

	  alloc_vpids_index = 0;
	}

      /*
       * Find next pointer if any.
       */
      if ((i + 1) >= npages)
	{
	  VPID_SET_NULL (&chain.next_vpid);
	}
      else
	{
	  chain.next_vpid = alloc_vpids[alloc_vpids_index + 1];
	}

      /*
       * Fetch the page and initialize it by chaining it with previous and next
       * allocated pages
       */

      addr.pgptr = pgbuf_fix (thread_p, &alloc_vpids[alloc_vpids_index],
			      NEW_PAGE, PGBUF_LATCH_WRITE,
			      PGBUF_UNCONDITIONAL_LATCH);
      if (addr.pgptr == NULL)
	{
	  return false;		/* Initialization has failed */
	}

      spage_initialize (thread_p, addr.pgptr, heap_get_spage_type (),
			HEAP_MAX_ALIGN, SAFEGUARD_RVSPACE);

      sp_success = spage_insert (thread_p, addr.pgptr, &recdes, &slotid);
      if (sp_success != SP_SUCCESS || slotid != HEAP_HEADER_AND_CHAIN_SLOTID)
	{
	  /*
	   * Initialization has failed !!
	   */
	  if (sp_success != SP_SUCCESS)
	    {
	      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_GENERIC_ERROR, 0);
	    }
	  pgbuf_unfix_and_init (thread_p, addr.pgptr);
	  return false;		/* Initialization has failed */
	}

      /*
       * We don't need to log before images for undos since allocation of pages
       * is an operation-destiny which does not depend on the transaction except
       * for newly created files. Pages may be shared by multiple concurrent
       * transactions, thus the deallocation cannot be undone. Note that new
       * files and their pages are deallocated when the transactions that create
       * the files are aborted.
       */

      log_append_redo_data (thread_p, heap_is_reusable_oid (file_type) ?
			    RVHF_NEWPAGE_REUSE_OID : RVHF_NEWPAGE, &addr,
			    recdes.length, recdes.data);

      if (scan_cache->collect_nbest < scan_cache->collect_maxbest)
	{
	  scan_cache->collect_best[scan_cache->collect_nbest].vpid =
	    alloc_vpids[alloc_vpids_index];
	  scan_cache->collect_best[scan_cache->collect_nbest].freespace =
	    spage_max_space_for_new_record (thread_p, addr.pgptr);
	  scan_cache->collect_nbest += 1;
	}
      else
	{
	  scan_cache->collect_nother_best += 1;
	}
      pgbuf_set_dirty (thread_p, addr.pgptr, FREE);
      addr.pgptr = NULL;

      /*
       * Now this is the last previous page
       */
      chain.prev_vpid = alloc_vpids[alloc_vpids_index];
      alloc_vpids_index++;
    }

  /*
   * Now, link previous page to first newly allocated page.
   */

  /*
   * Now, link previous page to newly allocated page
   */
  return heap_link_to_new (thread_p, vfid, first_alloc_vpid, link);
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * heap_vpid_prealloc_set () - Preallocate a set of heap pages
 *   return: page ptr to first allocated page
 *   hfid(in): Object heap file identifier
 *   hdr_pgptr(in): The heap page header
 *   heap_hdr(in): The heap header structure
 *   npages(in): Number of pages to preallocate
 *   scan_cache(in): Scan cache
 *
 * Note: Preallocate and initialize a set of heap pages. The heap
 * header is updated to reflect a newly allocated best space page
 */
static char *
heap_vpid_prealloc_set (THREAD_ENTRY * thread_p, const HFID * hfid,
			PAGE_PTR hdr_pgptr, HEAP_HDR_STATS * heap_hdr,
			int npages, HEAP_SCANCACHE * scan_cache)
{
  PAGE_PTR last_pgptr = NULL;
  VPID last_vpid;
  VPID first_alloc_vpid;
  int first_alloc_nthpage;
  PAGE_PTR new_pgptr = NULL;
  LOG_DATA_ADDR addr;
  const VPID *hdr_vpid;
  HEAP_CHAIN *last_chain;
  RECDES recdes;
  int i, best;
  HEAP_CHAIN_TOLAST tolast;
  void *tolast_scancache[2];
  int ret = NO_ERROR;

  addr.vfid = &hfid->vfid;
  addr.offset = HEAP_HEADER_AND_CHAIN_SLOTID;
  hdr_vpid = pgbuf_get_vpid_ptr (hdr_pgptr);

  if (file_find_last_page (thread_p, &hfid->vfid, &last_vpid) == NULL)
    {
      return NULL;
    }

  last_pgptr = heap_scan_pb_lock_and_fetch (thread_p, &last_vpid, OLD_PAGE,
					    X_LOCK, scan_cache);
  if (last_pgptr == NULL)
    {
      return NULL;
    }

  /*
   * Make sure that last page does not point to anything
   */

  if (VPID_EQ (&last_vpid, hdr_vpid))
    {
      /*
       * Last page is the header page
       */
      if (!VPID_ISNULL (&heap_hdr->next_vpid))
	{
	  er_log_debug (ARG_FILE_LINE,
			"heap_vpid_prealloc_set: Last heap page"
			" points to another page\n");
	  pgbuf_unfix_and_init (thread_p, last_pgptr);
	  return NULL;
	}
    }
  else
    {
      if (spage_get_record (last_pgptr, HEAP_HEADER_AND_CHAIN_SLOTID, &recdes,
			    PEEK) != S_SUCCESS)
	{
	  /* Unable to obtain chain record */
	  pgbuf_unfix_and_init (thread_p, last_pgptr);
	  return NULL;
	}
      last_chain = (HEAP_CHAIN *) recdes.data;
      if (!VPID_ISNULL (&last_chain->next_vpid))
	{
	  /*
	   * Last page points to another page. It looks like a system problem
	   * Do nothing.
	   */
	  er_log_debug (ARG_FILE_LINE,
			"heap_vpid_prealloc_set: Last heap page"
			" points to another page\n");
	  pgbuf_unfix_and_init (thread_p, last_pgptr);
	  return NULL;
	}
    }

  /*
   * Prepare initialization fields, so that current page will point to
   * previous page.
   */

  tolast.hdr_pgptr = hdr_pgptr;
  tolast.last_pgptr = last_pgptr;
  tolast.heap_hdr = heap_hdr;

  tolast_scancache[0] = &tolast;
  tolast_scancache[1] = scan_cache;

  if (file_alloc_pages_as_noncontiguous (thread_p, &hfid->vfid,
					 &first_alloc_vpid,
					 &first_alloc_nthpage, npages,
					 &last_vpid, heap_vpid_init_newset,
					 tolast_scancache, NULL) == NULL)
    {
      pgbuf_unfix_and_init (thread_p, last_pgptr);
      return NULL;
    }

  pgbuf_unfix_and_init (thread_p, last_pgptr);

  /*
   * Note: we fetch the page as old since it was initialized during the
   * allocation by heap_vpid_init_newset, therefore, we care about the current
   * content of the page.
   */

  new_pgptr = heap_scan_pb_lock_and_fetch (thread_p, &first_alloc_vpid,
					   OLD_PAGE, X_LOCK, scan_cache);
  if (new_pgptr == NULL)
    {
      /* something went wrong, deallocate the above page and return */
      ret = file_truncate_to_numpages (thread_p, &hfid->vfid,
				       first_alloc_nthpage);
      return NULL;
    }

  /*
   * Now update header statistics for best1 space page
   * The changes to the statistics are not logged. They are fixed
   * automatically sooner or later
   */

  if (file_is_new_file (thread_p, &(hfid->vfid)) == FILE_NEW_FILE
      && log_is_tran_in_system_op (thread_p) == true)
    {
      addr.pgptr = hdr_pgptr;
      log_append_undo_data (thread_p, RVHF_STATS, &addr, sizeof (*heap_hdr),
			    heap_hdr);
    }

  if (scan_cache->collect_nbest > npages
      || scan_cache->collect_nbest >= HEAP_NUM_BEST_SPACESTATS)
    {
      /*
       * Likely, I started with a copy of the statistics, or we allocated
       * a bunch of new pages
       */
      for (i = 0, best = 0;
	   i < npages && best < HEAP_NUM_BEST_SPACESTATS; i++, best++)
	{
	  heap_hdr->estimates.best[best].freespace =
	    scan_cache->collect_best[i].freespace;
	  heap_hdr->estimates.best[best].vpid =
	    scan_cache->collect_best[i].vpid;
	}

      heap_hdr->estimates.num_high_best = best;
      heap_hdr->estimates.num_other_high_best =
	scan_cache->collect_nother_best + scan_cache->collect_nbest - best;
      assert (heap_hdr->estimates.num_high_best <= HEAP_NUM_BEST_SPACESTATS);
    }
  else
    {
      heap_hdr->estimates.num_other_high_best +=
	scan_cache->collect_nother_best;
    }

  /* Set last vpid */
  if (file_find_last_page
      (thread_p, &hfid->vfid, &heap_hdr->estimates.last_vpid) == NULL)
    {
      assert (false);
      return NULL;
    }

  heap_hdr->estimates.num_pages += npages;
  addr.pgptr = hdr_pgptr;
  log_skip_logging_set_lsa (thread_p, &addr);
  pgbuf_set_dirty (thread_p, hdr_pgptr, DONT_FREE);

  return new_pgptr;		/* new_pgptr is lock and fetch */
}
#endif

/*
 * heap_vpid_alloc () - allocate, fetch, and initialize a new page
 *   return: ponter to newly allocated page or NULL
 *   hfid(in): Object heap file identifier
 *   hdr_pgptr(in): The heap page header
 *   heap_hdr(in): The heap header structure
 *   needed_space(in): The minimal space needed on new page
 *   scan_cache(in): Scan cache
 *   is_new_file(in): is new file
 *
 * Note: Allocate and initialize a new heap page. The heap header is
 * updated to reflect a newly allocated best space page and
 * the set of best space pages information may be updated to
 * include the previous best1 space page.
 */
static PAGE_PTR
heap_vpid_alloc (THREAD_ENTRY * thread_p, const HFID * hfid,
		 PAGE_PTR hdr_pgptr, HEAP_HDR_STATS * heap_hdr,
		 int needed_space, HEAP_SCANCACHE * scan_cache,
		 FILE_IS_NEW_FILE is_new_file)
{
  VPID vpid;			/* Volume and page identifiers */
  LOG_DATA_ADDR addr;		/* Address of logging data */
  PAGE_PTR new_pgptr = NULL;
  PAGE_PTR last_pgptr = NULL;
  int best;
  HEAP_CHAIN_TOLAST tolast;
  VPID last_vpid;
  int last_freespace;

  addr.vfid = &hfid->vfid;
  addr.offset = HEAP_HEADER_AND_CHAIN_SLOTID;

  last_pgptr = heap_get_last_page (thread_p, hfid, heap_hdr, scan_cache,
				   &last_vpid);
  if (last_pgptr == NULL)
    {
      /* something went wrong, return error */
      assert (false);
      return NULL;
    }
  assert (!VPID_ISNULL (&last_vpid));

  last_freespace = spage_max_space_for_new_record (thread_p, last_pgptr);
  if (last_freespace > needed_space)
    {
      return last_pgptr;
    }

  /*
   * Now allocate a new page as close as possible to the last allocated page.
   * Note that a new page is allocated when the best1 space page in the
   * statistics is the actual last page on the heap.
   */

  /*
   * Prepare initialization fields, so that current page will point to
   * previous page.
   */

  tolast.hdr_pgptr = hdr_pgptr;
  tolast.last_pgptr = last_pgptr;
  tolast.heap_hdr = heap_hdr;

  if (file_alloc_pages (thread_p, &hfid->vfid, &vpid, 1,
			&last_vpid, heap_vpid_init_new, &tolast) == NULL)
    {
      /* Unable to allocate a new page */
      if (last_pgptr != NULL)
	{
	  pgbuf_unfix_and_init (thread_p, last_pgptr);
	}
      return NULL;
    }

  if (last_pgptr != NULL)
    {
      pgbuf_unfix_and_init (thread_p, last_pgptr);
    }

  /*
   * Note: we fetch the page as old since it was initialized during the
   * allocation by heap_vpid_init_new, therefore, we care about the current
   * content of the page.
   */

  new_pgptr = heap_scan_pb_lock_and_fetch (thread_p, &vpid, OLD_PAGE, X_LOCK,
					   scan_cache);
  if (new_pgptr == NULL)
    {
      /* something went wrong, deallocate the above page and return */
      (void) file_dealloc_page (thread_p, &hfid->vfid, &vpid);
      return NULL;
    }

  /*
   * Now update header statistics for best1 space page.
   * The changes to the statistics are not logged.
   * They are fixed automatically sooner or later.
   */

  /* last page hint */
  heap_hdr->estimates.last_vpid = vpid;
  heap_hdr->estimates.num_pages++;

  best = heap_hdr->estimates.head;
  heap_hdr->estimates.head = HEAP_STATS_NEXT_BEST_INDEX (best);
  if (VPID_ISNULL (&heap_hdr->estimates.best[best].vpid))
    {
      heap_hdr->estimates.num_high_best++;
      assert (heap_hdr->estimates.num_high_best <= HEAP_NUM_BEST_SPACESTATS);
    }
  else
    {
      if (heap_hdr->estimates.best[best].freespace > HEAP_DROP_FREE_SPACE)
	{
	  heap_hdr->estimates.num_other_high_best++;
	  heap_stats_put_second_best (heap_hdr,
				      &heap_hdr->estimates.best[best].vpid);
	}
    }

  heap_hdr->estimates.best[best].vpid = vpid;
  heap_hdr->estimates.best[best].freespace =
    spage_max_space_for_new_record (thread_p, new_pgptr);

  if (is_new_file == FILE_NEW_FILE)
    {
      /* do can not register vpid of new file.
       * because the vpid can be dealloced when rollback.
       */
    }
  else
    {
      assert (is_new_file == FILE_OLD_FILE);
      if (prm_get_integer_value (PRM_ID_HF_MAX_BESTSPACE_ENTRIES) > 0)
	{
	  int rc;

	  rc = pthread_mutex_lock (&heap_Bestspace->bestspace_mutex);

	  (void) heap_stats_add_bestspace (thread_p, hfid, &vpid,
					   DB_PAGESIZE);

	  assert (mht_count (heap_Bestspace->vpid_ht) ==
		  mht_count (heap_Bestspace->hfid_ht));

	  pthread_mutex_unlock (&heap_Bestspace->bestspace_mutex);
	}
    }

  addr.pgptr = hdr_pgptr;
  log_skip_logging_set_lsa (thread_p, &addr);

  return new_pgptr;		/* new_pgptr is lock and fetch */
}

/*
 * heap_vpid_remove () - Deallocate a heap page
 *   return: rm_vpid on success or NULL on error
 *   hfid(in): Object heap file identifier
 *   heap_hdr(in): The heap header stats
 *   rm_vpid(in): Page to remove
 *
 * Note: The given page is removed from the heap. The linked list of heap
 * pages is updated to remove this page, and the heap header may
 * be updated if this page was part of the statistics.
 */
static VPID *
heap_vpid_remove (THREAD_ENTRY * thread_p, const HFID * hfid,
		  HEAP_HDR_STATS * heap_hdr, VPID * rm_vpid)
{
  PAGE_PTR rm_pgptr = NULL;	/* Pointer to page to be removed    */
  RECDES rm_recdes;		/* Record descriptor which holds the
				 * chain of the page to be removed
				 */
  HEAP_CHAIN *rm_chain;		/* Chain information of the page to be
				 * removed
				 */
  VPID vpid;			/* Real identifier of previous page */
  LOG_DATA_ADDR addr;		/* Log address of previous page     */
  RECDES recdes;		/* Record descriptor to page header */
  HEAP_CHAIN chain;		/* Chain to next and prev page      */
  int sp_success;
  int i;

  /*
   * Make sure that this is not the header page since the header page cannot
   * be removed. If the header page is removed.. the heap is gone
   */

  addr.pgptr = NULL;
  if (rm_vpid->pageid == hfid->hpgid && rm_vpid->volid == hfid->vfid.volid)
    {
      er_log_debug (ARG_FILE_LINE, "heap_vpid_remove: Trying to remove header"
		    " page = %d|%d of heap file = %d|%d|%d",
		    (int) rm_vpid->volid, rm_vpid->pageid,
		    (int) hfid->vfid.volid, hfid->vfid.fileid, hfid->hpgid);
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      goto error;
    }

  /* Get the chain record */
  rm_pgptr = heap_scan_pb_lock_and_fetch (thread_p, rm_vpid, OLD_PAGE, X_LOCK,
					  NULL);
  if (rm_pgptr == NULL)
    {
      /* Look like a system error. Unable to obtain chain header record */
      goto error;
    }
  if (spage_get_record (rm_pgptr, HEAP_HEADER_AND_CHAIN_SLOTID, &rm_recdes,
			PEEK) != S_SUCCESS)
    {
      /* Look like a system error. Unable to obtain chain header record */
      goto error;
    }

  rm_chain = (HEAP_CHAIN *) rm_recdes.data;

  /*
   * UPDATE PREVIOUS PAGE
   *
   * Update chain next field of previous last page
   * If previous page is the heap header page, it contains a heap header
   * instead of a chain.
   */

  vpid = rm_chain->prev_vpid;
  addr.vfid = &hfid->vfid;
  addr.offset = HEAP_HEADER_AND_CHAIN_SLOTID;

  addr.pgptr = heap_scan_pb_lock_and_fetch (thread_p, &vpid, OLD_PAGE, X_LOCK,
					    NULL);
  if (addr.pgptr == NULL)
    {
      /* something went wrong, return */
      goto error;
    }

  /*
   * Make sure that the page to be removed is not referenced on the heap
   * statistics
   */

  assert (heap_hdr != NULL);

  /*
   * We cannot break in the following loop since a best page could be
   * duplicated
   */
  for (i = 0; i < HEAP_NUM_BEST_SPACESTATS; i++)
    {
      if (VPID_EQ (&heap_hdr->estimates.best[i].vpid, rm_vpid))
	{
	  VPID_SET_NULL (&heap_hdr->estimates.best[i].vpid);
	  heap_hdr->estimates.best[i].freespace = 0;
	  heap_hdr->estimates.head = i;
	}
    }

  if (VPID_EQ (&heap_hdr->estimates.last_vpid, rm_vpid))
    {
      /* If the page is the last page of the heap file, update the hint */
      heap_hdr->estimates.last_vpid = rm_chain->prev_vpid;
    }

  /*
   * Is previous page the header page ?
   */
  if (vpid.pageid == hfid->hpgid && vpid.volid == hfid->vfid.volid)
    {
      /*
       * PREVIOUS PAGE IS THE HEADER PAGE.
       * It contains a heap header instead of a chain record
       */
      heap_hdr->next_vpid = rm_chain->next_vpid;
    }
  else
    {
      /*
       * PREVIOUS PAGE IS NOT THE HEADER PAGE.
       * It contains a chain...
       * We need to make sure that there is not references to the page to delete
       * in the statistics of the heap header
       */

      /* NOW check the PREVIOUS page */

      if (spage_get_record (addr.pgptr, HEAP_HEADER_AND_CHAIN_SLOTID,
			    &recdes, PEEK) != S_SUCCESS)
	{
	  /* Look like a system error. Unable to obtain header record */
	  goto error;
	}

      /* Copy the chain record to memory.. so we can log the changes */
      memcpy (&chain, recdes.data, sizeof (chain));

      /* Modify the chain of the previous page in memory */
      chain.next_vpid = rm_chain->next_vpid;

      /* Get the chain record */
      recdes.area_size = recdes.length = sizeof (chain);
      recdes.type = REC_HOME;
      recdes.data = (char *) &chain;

      /* Log the desired changes.. and then change the header */
      log_append_undoredo_data (thread_p, RVHF_CHAIN, &addr, sizeof (chain),
				sizeof (chain), recdes.data, &chain);

      /* Now change the record */

      sp_success = spage_update (thread_p, addr.pgptr,
				 HEAP_HEADER_AND_CHAIN_SLOTID, &recdes);
      if (sp_success != SP_SUCCESS)
	{
	  /*
	   * This look like a system error, size did not change, so why did it
	   * fail
	   */
	  if (sp_success != SP_ERROR)
	    {
	      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_GENERIC_ERROR, 0);
	    }
	  goto error;
	}

    }

  /* Now set dirty, free and unlock the previous page */
  pgbuf_set_dirty (thread_p, addr.pgptr, FREE);
  addr.pgptr = NULL;

  /*
   * UPDATE NEXT PAGE
   *
   * Update chain previous field of next page
   */

  if (!(VPID_ISNULL (&rm_chain->next_vpid)))
    {
      vpid = rm_chain->next_vpid;
      addr.offset = HEAP_HEADER_AND_CHAIN_SLOTID;

      addr.pgptr = heap_scan_pb_lock_and_fetch (thread_p, &vpid, OLD_PAGE,
						X_LOCK, NULL);
      if (addr.pgptr == NULL)
	{
	  /* something went wrong, return */
	  goto error;
	}

      /* Get the chain record */
      if (spage_get_record (addr.pgptr, HEAP_HEADER_AND_CHAIN_SLOTID,
			    &recdes, PEEK) != S_SUCCESS)
	{
	  /* Look like a system error. Unable to obtain header record */
	  goto error;
	}

      /* Copy the chain record to memory.. so we can log the changes */
      memcpy (&chain, recdes.data, sizeof (chain));

      /* Modify the chain of the next page in memory */
      chain.prev_vpid = rm_chain->prev_vpid;

      /* Log the desired changes.. and then change the header */
      log_append_undoredo_data (thread_p, RVHF_CHAIN, &addr, sizeof (chain),
				sizeof (chain), recdes.data, &chain);

      /* Now change the record */
      recdes.area_size = recdes.length = sizeof (chain);
      recdes.type = REC_HOME;
      recdes.data = (char *) &chain;

      sp_success = spage_update (thread_p, addr.pgptr,
				 HEAP_HEADER_AND_CHAIN_SLOTID, &recdes);
      if (sp_success != SP_SUCCESS)
	{
	  /*
	   * This look like a system error, size did not change, so why did it
	   * fail
	   */
	  if (sp_success != SP_ERROR)
	    {
	      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_GENERIC_ERROR, 0);
	    }
	  goto error;
	}

      /* Now set dirty, free and unlock the next page */

      pgbuf_set_dirty (thread_p, addr.pgptr, FREE);
      addr.pgptr = NULL;
    }

  /* Free the page to be deallocated and deallocate the page */
  pgbuf_unfix_and_init (thread_p, rm_pgptr);

  if (file_dealloc_page (thread_p, &hfid->vfid, rm_vpid) != NO_ERROR)
    {
      goto error;
    }

  if (prm_get_integer_value (PRM_ID_HF_MAX_BESTSPACE_ENTRIES) > 0)
    {
      int rc;

      rc = pthread_mutex_lock (&heap_Bestspace->bestspace_mutex);

      (void) heap_stats_del_bestspace_by_vpid (thread_p, hfid, rm_vpid);

      assert (mht_count (heap_Bestspace->vpid_ht) ==
	      mht_count (heap_Bestspace->hfid_ht));

      pthread_mutex_unlock (&heap_Bestspace->bestspace_mutex);
    }

  return rm_vpid;

error:
  if (rm_pgptr != NULL)
    {
      pgbuf_unfix_and_init (thread_p, rm_pgptr);
    }
  if (addr.pgptr != NULL)
    {
      pgbuf_unfix_and_init (thread_p, addr.pgptr);
    }

  return NULL;
}

/*
 * heap_vpid_next () - Find next page of heap
 *   return: NO_ERROR
 *   hfid(in): Object heap file identifier
 *   pgptr(in): Current page pointer
 *   next_vpid(in/out): Next volume-page identifier
 *
 * Note: Find the next page of heap file.
 */
static int
heap_vpid_next (const HFID * hfid, PAGE_PTR pgptr, VPID * next_vpid)
{
  HEAP_CHAIN *chain;		/* Chain to next and prev page      */
  HEAP_HDR_STATS *heap_hdr;	/* Header of heap file              */
  RECDES recdes;		/* Record descriptor to page header */
  int ret = NO_ERROR;

  /* Get either the heap header or chain record */
  if (spage_get_record (pgptr, HEAP_HEADER_AND_CHAIN_SLOTID, &recdes, PEEK)
      != S_SUCCESS)
    {
      /* Unable to get header/chain record for the given page */
      VPID_SET_NULL (next_vpid);
      ret = ER_FAILED;
    }
  else
    {
      pgbuf_get_vpid (pgptr, next_vpid);
      /* Is this the header page ? */
      if (next_vpid->pageid == hfid->hpgid
	  && next_vpid->volid == hfid->vfid.volid)
	{
	  heap_hdr = (HEAP_HDR_STATS *) recdes.data;
	  *next_vpid = heap_hdr->next_vpid;
	}
      else
	{
	  chain = (HEAP_CHAIN *) recdes.data;
	  *next_vpid = chain->next_vpid;
	}
    }

  return ret;
}

/*
 * heap_vpid_prev () - Find previous page of heap
 *   return: NO_ERROR
 *   hfid(in): Object heap file identifier
 *   pgptr(in): Current page pointer
 *   prev_vpid(in/out): Previous volume-page identifier
 *
 * Note: Find the previous page of heap file.
 */
static int
heap_vpid_prev (const HFID * hfid, PAGE_PTR pgptr, VPID * prev_vpid)
{
  HEAP_CHAIN *chain;		/* Chain to next and prev page      */
  RECDES recdes;		/* Record descriptor to page header */
  int ret = NO_ERROR;

  /* Get either the header or chain record */
  if (spage_get_record (pgptr, HEAP_HEADER_AND_CHAIN_SLOTID, &recdes, PEEK)
      != S_SUCCESS)
    {
      /* Unable to get header/chain record for the given page */
      VPID_SET_NULL (prev_vpid);
      ret = ER_FAILED;
    }
  else
    {
      pgbuf_get_vpid (pgptr, prev_vpid);
      /* Is this the header page ? */
      if (prev_vpid->pageid == hfid->hpgid
	  && prev_vpid->volid == hfid->vfid.volid)
	{
	  VPID_SET_NULL (prev_vpid);
	}
      else
	{
	  chain = (HEAP_CHAIN *) recdes.data;
	  *prev_vpid = chain->prev_vpid;
	}
    }

  return ret;
}

/*
 * heap_get_best_estimates_stats () - Find the number of best space pages
 *   return: num of set of instances in cluster or -1 in case of error.
 *   hfid(in): Object heap file identifier
 *   num_best(in/out): Number of best pages
 *   num_other_best(in/out): Number of other best space pages.
 *   num_recs(in):
 *
 * Note: Find the number of set of instances in the cluster, and the
 * number of best and the other best space pages.
 */
static int
heap_get_best_estimates_stats (THREAD_ENTRY * thread_p,
			       const HFID * hfid,
			       int *num_best,
			       int *num_other_best, int *num_recs)
{
  HEAP_HDR_STATS *heap_hdr;	/* Header of heap structure           */
  PAGE_PTR hdr_pgptr = NULL;	/* Header page                        */
  VPID vpid;			/* Page-volume identifier             */
  RECDES hdr_recdes;		/* Header record descriptor           */

  *num_best = *num_other_best = *num_recs = 0;

  /* Read the header page */
  vpid.volid = hfid->vfid.volid;
  vpid.pageid = hfid->hpgid;

  hdr_pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ,
			 PGBUF_UNCONDITIONAL_LATCH);
  if (hdr_pgptr == NULL)
    {
      return ER_FAILED;
    }

  if (spage_get_record (hdr_pgptr, HEAP_HEADER_AND_CHAIN_SLOTID, &hdr_recdes,
			PEEK) != S_SUCCESS)
    {
      pgbuf_unfix_and_init (thread_p, hdr_pgptr);
      return ER_FAILED;
    }

  heap_hdr = (HEAP_HDR_STATS *) hdr_recdes.data;
  *num_best = heap_hdr->estimates.num_high_best;
  *num_other_best = heap_hdr->estimates.num_other_high_best;
  *num_recs = heap_hdr->estimates.num_recs;

  if (*num_best < 0)
    {
      *num_best = 0;
    }

  if (*num_other_best < 0)
    {
      *num_other_best = 0;
    }

  pgbuf_unfix_and_init (thread_p, hdr_pgptr);

  return NO_ERROR;
}

/*
 * heap_manager_initialize () -
 *   return: NO_ERROR
 *
 * Note: Initialization process of the heap file module. Find the
 * maximum size of an object that can be inserted in the heap.
 * Objects that overpass this size are stored in overflow.
 */
int
heap_manager_initialize (void)
{
  int ret;

#define HEAP_MAX_FIRSTSLOTID_LENGTH (sizeof (HEAP_HDR_STATS))

  heap_Maxslotted_reclength = (spage_max_record_size ()
			       - HEAP_MAX_FIRSTSLOTID_LENGTH);
  heap_Slotted_overhead = spage_slot_size ();

  /* Initialize the class representation cache */
  ret = heap_chnguess_initialize ();
  if (ret != NO_ERROR)
    {
      return ret;
    }

  ret = heap_classrepr_initialize_cache ();
  if (ret != NO_ERROR)
    {
      return ret;
    }

  /* Initialize best space cache */
  ret = heap_stats_bestspace_initialize ();
  if (ret != NO_ERROR)
    {
      return ret;
    }

  return ret;
}

/*
 * heap_manager_finalize () - Terminate the heap manager
 *   return: NO_ERROR
 * Note: Deallocate any cached structure.
 */
int
heap_manager_finalize (void)
{
  int ret;

  ret = heap_chnguess_finalize ();
  if (ret != NO_ERROR)
    {
      return ret;
    }

  ret = heap_classrepr_finalize_cache ();
  if (ret != NO_ERROR)
    {
      return ret;
    }

  ret = heap_stats_bestspace_finalize ();
  if (ret != NO_ERROR)
    {
      return ret;
    }

  return ret;
}

/*
 * heap_create_internal () - Create a heap file
 *   return: HFID * (hfid on success and NULL on failure)
 *   hfid(in/out): Object heap file identifier.
 *                 All fields in the identifier are set, except the volume
 *                 identifier which should have already been set by the caller.
 *   exp_npgs(in): Expected number of pages
 *   class_oid(in): OID of the class for which the heap will be created.
 *   reuse_oid(in): if true, the OIDs of deleted instances will be reused
 *
 * Note: Creates a heap file on the disk volume associated with
 * hfid->vfid->volid.
 *
 * A set of sectors is allocated to improve locality of the heap.
 * The number of sectors to allocate is estimated from the number
 * of expected pages. The maximum number of allocated sectors is
 * 25% of the total number of sectors in disk. When the number of
 * pages cannot be estimated, a negative value can be passed to
 * indicate so. In this case, no sectors are allocated. The
 * number of expected pages are not allocated at this moment,
 * they are allocated as needs arrives.
 */
static HFID *
heap_create_internal (THREAD_ENTRY * thread_p, HFID * hfid, int exp_npgs,
		      const OID * class_oid, const bool reuse_oid)
{
  HEAP_HDR_STATS heap_hdr;	/* Heap file header            */
  VPID vpid;			/* Volume and page identifiers */
  RECDES recdes;		/* Record descriptor           */
  LOG_DATA_ADDR addr_hdr;	/* Address of logging data     */
  INT16 slotid;
  int sp_success;
  int i;
  FILE_HEAP_DES hfdes;
  const FILE_TYPE file_type = reuse_oid ? FILE_HEAP_REUSE_SLOTS : FILE_HEAP;

  /* create a file descriptor */
  if (class_oid != NULL)
    {
      hfdes.class_oid = *class_oid;
    }
  else
    {
      OID_SET_NULL (&hfdes.class_oid);
    }

  if (prm_get_bool_value (PRM_ID_DONT_REUSE_HEAP_FILE) == false)
    {
      /*
       * Try to reuse an already mark deleted heap file
       */

      vpid.volid = hfid->vfid.volid;
      if (file_reuse_deleted (thread_p, &hfid->vfid, file_type,
			      &hfdes) != NULL
	  && file_find_nthpages (thread_p, &hfid->vfid, &vpid, 0, 1) == 1)
	{
	  hfid->hpgid = vpid.pageid;
	  if (heap_reuse (thread_p, hfid, &hfdes.class_oid,
			  reuse_oid) != NULL)
	    {
	      /* A heap has been reused */
	      return hfid;
	    }
	}

      hfid->vfid.volid = vpid.volid;
    }

  if (exp_npgs < 3)
    {
      exp_npgs = 3;
    }

  /*
   * Create the unstructured file for the heap
   * Create the header for the heap file. The header is used to speed
   * up insertions of objects and to find some simple information about the
   * heap.
   * We do not initialize the page during the allocation since the file is
   * new, and the file is going to be removed in the event of a crash.
   */

  if (file_create (thread_p, &hfid->vfid, exp_npgs, file_type, &hfdes,
		   &vpid, 1) == NULL)
    {
      /* Unable to create the heap file */
      return NULL;
    }

  addr_hdr.pgptr = pgbuf_fix (thread_p, &vpid, NEW_PAGE, PGBUF_LATCH_WRITE,
			      PGBUF_UNCONDITIONAL_LATCH);
  if (addr_hdr.pgptr == NULL)
    {
      /* something went wrong, destroy the file, and return */
      (void) file_destroy (thread_p, &hfid->vfid);
      hfid->vfid.fileid = NULL_FILEID;
      hfid->hpgid = NULL_PAGEID;
      return NULL;
    }
  hfid->hpgid = vpid.pageid;

  if (prm_get_integer_value (PRM_ID_HF_MAX_BESTSPACE_ENTRIES) > 0)
    {
      int del_cnt, rc;

      rc = pthread_mutex_lock (&heap_Bestspace->bestspace_mutex);
      /* delete all entries of hfid. is impossible, set as a defense code */
      del_cnt = heap_stats_del_bestspace (thread_p, hfid, -1 /* no stop */ ,
					  INT_MAX);
      assert (del_cnt == 0);
      assert (mht_count (heap_Bestspace->vpid_ht) ==
	      mht_count (heap_Bestspace->hfid_ht));
      pthread_mutex_unlock (&heap_Bestspace->bestspace_mutex);
    }

  /* Initialize header page */
  spage_initialize (thread_p, addr_hdr.pgptr, heap_get_spage_type (),
		    HEAP_MAX_ALIGN, SAFEGUARD_RVSPACE);

  /* Now insert header */
  COPY_OID (&heap_hdr.class_oid, &hfdes.class_oid);
  VFID_SET_NULL (&heap_hdr.ovf_vfid);
  VPID_SET_NULL (&heap_hdr.next_vpid);

  heap_hdr.unfill_space =
    (int) ((float) DB_PAGESIZE *
	   prm_get_float_value (PRM_ID_HF_UNFILL_FACTOR));

  heap_hdr.estimates.num_pages = 1;
  heap_hdr.estimates.num_recs = 0;
  heap_hdr.estimates.recs_sumlen = 0.0;

  heap_hdr.estimates.best[0].vpid.volid = hfid->vfid.volid;
  heap_hdr.estimates.best[0].vpid.pageid = hfid->hpgid;
  heap_hdr.estimates.best[0].freespace =
    spage_max_space_for_new_record (thread_p, addr_hdr.pgptr);

  heap_hdr.estimates.head = 1;
  for (i = heap_hdr.estimates.head; i < HEAP_NUM_BEST_SPACESTATS; i++)
    {
      VPID_SET_NULL (&heap_hdr.estimates.best[i].vpid);
      heap_hdr.estimates.best[i].freespace = 0;
    }

  heap_hdr.estimates.num_high_best = 1;
  heap_hdr.estimates.num_other_high_best = 0;

  heap_hdr.estimates.num_second_best = 0;
  heap_hdr.estimates.head_second_best = 0;
  heap_hdr.estimates.tail_second_best = 0;
  heap_hdr.estimates.num_substitutions = 0;

  for (i = 0; i < HEAP_NUM_BEST_SPACESTATS; i++)
    {
      VPID_SET_NULL (&heap_hdr.estimates.second_best[i]);
    }

  heap_hdr.estimates.last_vpid.volid = hfid->vfid.volid;
  heap_hdr.estimates.last_vpid.pageid = hfid->hpgid;

  heap_hdr.estimates.full_search_vpid.volid = hfid->vfid.volid;
  heap_hdr.estimates.full_search_vpid.pageid = hfid->hpgid;

  heap_hdr.reserve1_for_future = 0;
  heap_hdr.reserve2_for_future = 0;

  recdes.area_size = recdes.length = sizeof (HEAP_HDR_STATS);
  recdes.type = REC_HOME;
  recdes.data = (char *) &heap_hdr;

  sp_success = spage_insert (thread_p, addr_hdr.pgptr, &recdes, &slotid);
  if (sp_success != SP_SUCCESS || slotid != HEAP_HEADER_AND_CHAIN_SLOTID)
    {
      /* something went wrong, destroy file and return error */
      if (sp_success != SP_SUCCESS)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_HEAP_UNABLE_TO_CREATE_HEAP, 1,
		  fileio_get_volume_label (hfid->vfid.volid, PEEK));
	}

      /* Free the page and release the lock */
      pgbuf_unfix_and_init (thread_p, addr_hdr.pgptr);
      (void) file_destroy (thread_p, &hfid->vfid);
      hfid->vfid.fileid = NULL_FILEID;
      hfid->hpgid = NULL_PAGEID;
      return NULL;
    }
  else
    {
      /*
       * Don't need to log before image (undo) since file and pages of the heap
       * are deallocated during undo (abort).
       */
      addr_hdr.vfid = &hfid->vfid;
      addr_hdr.offset = HEAP_HEADER_AND_CHAIN_SLOTID;
      log_append_redo_data (thread_p,
			    reuse_oid ? RVHF_CREATE_REUSE_OID : RVHF_CREATE,
			    &addr_hdr, sizeof (heap_hdr), &heap_hdr);
      pgbuf_set_dirty (thread_p, addr_hdr.pgptr, FREE);
      addr_hdr.pgptr = NULL;
    }

  return hfid;
}

/*
 * heap_delete_all_page_records () -
 *   return: false if nothing is deleted, otherwise true
 *   vpid(in): the vpid of the page
 *   pgptr(in): PAGE_PTR to the page
 */
static bool
heap_delete_all_page_records (THREAD_ENTRY * thread_p, const VPID * vpid,
			      PAGE_PTR pgptr)
{
  bool something_deleted = false;
  OID oid;
  RECDES recdes;

  assert (pgptr != NULL);
  assert (vpid != NULL);

  oid.volid = vpid->volid;
  oid.pageid = vpid->pageid;
  oid.slotid = NULL_SLOTID;

  while (true)
    {
      if (spage_next_record (pgptr, &oid.slotid, &recdes, PEEK) != S_SUCCESS)
	{
	  break;
	}
      if (oid.slotid == HEAP_HEADER_AND_CHAIN_SLOTID)
	{
	  continue;
	}
      (void) spage_delete (thread_p, pgptr, oid.slotid);
      something_deleted = true;
    }

  return something_deleted;
}

/*
 * heap_reinitialize_page () -
 *   return: NO_ERROR if succeed, otherwise error code
 *   pgptr(in): PAGE_PTR to the page
 *   is_header_page(in): true if the page is the header page
 */
static int
heap_reinitialize_page (THREAD_ENTRY * thread_p, PAGE_PTR pgptr,
			const bool is_header_page)
{
  HEAP_CHAIN tmp_chain;
  HEAP_HDR_STATS tmp_hdr_stats;
  PGSLOTID slotid = NULL_SLOTID;
  RECDES recdes;
  int error_code = NO_ERROR;

  if (spage_get_record (pgptr, HEAP_HEADER_AND_CHAIN_SLOTID, &recdes,
			PEEK) != S_SUCCESS)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      error_code = ER_GENERIC_ERROR;
      goto error_exit;
    }

  if (is_header_page)
    {
      assert (recdes.length == sizeof (HEAP_HDR_STATS));
      tmp_hdr_stats = *(HEAP_HDR_STATS *) recdes.data;
      recdes.data = (char *) &tmp_hdr_stats;
      recdes.area_size = recdes.length = sizeof (tmp_hdr_stats);
      recdes.type = REC_HOME;
    }
  else
    {
      assert (recdes.length == sizeof (HEAP_CHAIN));
      tmp_chain = *(HEAP_CHAIN *) recdes.data;
      recdes.data = (char *) &tmp_chain;
      recdes.area_size = recdes.length = sizeof (tmp_chain);
      recdes.type = REC_HOME;
    }

  spage_initialize (thread_p, pgptr, heap_get_spage_type (),
		    HEAP_MAX_ALIGN, SAFEGUARD_RVSPACE);

  if (spage_insert (thread_p, pgptr, &recdes, &slotid) != SP_SUCCESS
      || slotid != HEAP_HEADER_AND_CHAIN_SLOTID)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      error_code = ER_GENERIC_ERROR;
      goto error_exit;
    }
  else
    {
      /* All is well and the page is now empty. */
    }

  return error_code;

error_exit:
  if (error_code == NO_ERROR)
    {
      error_code = ER_GENERIC_ERROR;
    }
  return error_code;
}

/*
 * heap_reuse () - Reuse a heap
 *   return: HFID * (hfid on success and NULL on failure)
 *   hfid(in): Object heap file identifier.
 *   class_oid(in): OID of the class for which the heap will be created.
 *
 * Note: Clean the given heap file so that it can be reused.
 * Note: The heap file must have been permanently marked as deleted.
 */
static const HFID *
heap_reuse (THREAD_ENTRY * thread_p, const HFID * hfid,
	    const OID * class_oid, const bool reuse_oid)
{
  VFID vfid;
  VPID vpid;			/* Volume and page identifiers */
  PAGE_PTR hdr_pgptr = NULL;	/* Page pointer to header page */
  PAGE_PTR pgptr = NULL;	/* Page pointer                */
  LOG_DATA_ADDR addr;		/* Address of logging data     */
  HEAP_HDR_STATS *heap_hdr = NULL;	/* Header of heap structure    */
  HEAP_CHAIN *chain;		/* Chain to next and prev page */
  RECDES recdes;
  VPID last_vpid;
  int is_header_page;
  int npages = 0;
  int i;
  bool need_update;

  assert (class_oid != NULL && !OID_ISNULL (class_oid));

  VPID_SET_NULL (&last_vpid);
  addr.vfid = &hfid->vfid;

  /*
   * Read the header page.
   * We lock the header page in exclusive mode.
   */

  vpid.volid = hfid->vfid.volid;
  vpid.pageid = hfid->hpgid;
  hdr_pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			 PGBUF_UNCONDITIONAL_LATCH);
  if (hdr_pgptr == NULL)
    {
      return NULL;
    }

  /*
   * Start scanning every page of the heap and removing the objects.
   * Note that, for normal heap files, the slot is not removed since we do not
   * know if the objects are pointed by some other objects in the database.
   * For reusable OID heap files we are certain there can be no references to
   * the objects so we can simply initialize the slotted page.
   */
  /*
   * Note Because the objects of reusable OID heaps are not referenced,
   *      reusing such heaps provides no actual benefit. We might consider
   *      giving up the reuse heap mechanism for reusable OID heaps in the
   *      future.
   */

  while (!(VPID_ISNULL (&vpid)))
    {
      /*
       * Fetch the page
       */
      pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			 PGBUF_UNCONDITIONAL_LATCH);
      if (pgptr == NULL)
	{
	  goto error;
	}

      is_header_page = (hdr_pgptr == pgptr) ? 1 : 0;

      /*
       * Remove all the objects in this page
       */
      if (!reuse_oid)
	{
	  (void) heap_delete_all_page_records (thread_p, &vpid, pgptr);

	  addr.pgptr = pgptr;
	  addr.offset = is_header_page;
	  log_append_redo_data (thread_p, RVHF_REUSE_PAGE, &addr,
				sizeof (*class_oid), class_oid);
	}
      else
	{
	  if (spage_number_of_slots (pgptr) > 1)
	    {
	      if (heap_reinitialize_page (thread_p, pgptr, is_header_page)
		  != NO_ERROR)
		{
		  goto error;
		}
	    }

	  addr.pgptr = pgptr;
	  addr.offset = is_header_page;
	  log_append_redo_data (thread_p, RVHF_REUSE_PAGE_REUSE_OID,
				&addr, sizeof (*class_oid), class_oid);
	}

      if (spage_get_record (pgptr, HEAP_HEADER_AND_CHAIN_SLOTID, &recdes,
			    PEEK) != S_SUCCESS)
	{
	  goto error;
	}
      if (recdes.data == NULL)
	{
	  goto error;
	}

      /* save new class oid in the page. it dirties the page. */
      if (is_header_page)
	{
	  heap_hdr = (HEAP_HDR_STATS *) recdes.data;
	  COPY_OID (&(heap_hdr->class_oid), class_oid);
	}
      else
	{
	  chain = (HEAP_CHAIN *) recdes.data;
	  COPY_OID (&(chain->class_oid), class_oid);
	}

      if (npages < HEAP_NUM_BEST_SPACESTATS)
	{
	  heap_hdr->estimates.best[npages].vpid = vpid;
	  heap_hdr->estimates.best[npages].freespace =
	    spage_get_free_space_without_saving (thread_p, pgptr,
						 &need_update);

	}

      if (prm_get_integer_value (PRM_ID_HF_MAX_BESTSPACE_ENTRIES) > 0)
	{
	  int rc;

	  rc = pthread_mutex_lock (&heap_Bestspace->bestspace_mutex);

	  (void) heap_stats_add_bestspace (thread_p, hfid, &vpid,
					   DB_PAGESIZE);

	  assert (mht_count (heap_Bestspace->vpid_ht) ==
		  mht_count (heap_Bestspace->hfid_ht));

	  pthread_mutex_unlock (&heap_Bestspace->bestspace_mutex);
	}

      npages++;
      last_vpid = vpid;

      /*
       * Find next page to scan and free the current page
       */
      if (heap_vpid_next (hfid, pgptr, &vpid) != NO_ERROR)
	{
	  goto error;
	}

      pgbuf_set_dirty (thread_p, pgptr, FREE);
      pgptr = NULL;
    }

  /*
   * If there is an overflow file for this heap, remove it
   */
  if (heap_ovf_find_vfid (thread_p, hfid, &vfid, false) != NULL)
    {
      (void) file_destroy (thread_p, &vfid);
    }

  /*
   * Reset the statistics. Set statistics for insertion back to first page
   * and reset unfill space according to new parameters
   */
  VFID_SET_NULL (&heap_hdr->ovf_vfid);
  heap_hdr->unfill_space =
    (int) ((float) DB_PAGESIZE *
	   prm_get_float_value (PRM_ID_HF_UNFILL_FACTOR));
  heap_hdr->estimates.num_pages = npages;
  heap_hdr->estimates.num_recs = 0;
  heap_hdr->estimates.recs_sumlen = 0.0;

  if (npages < HEAP_NUM_BEST_SPACESTATS)
    {
      heap_hdr->estimates.num_high_best = npages;
      heap_hdr->estimates.num_other_high_best = 0;
    }
  else
    {
      heap_hdr->estimates.num_high_best = HEAP_NUM_BEST_SPACESTATS;
      heap_hdr->estimates.num_other_high_best =
	npages - HEAP_NUM_BEST_SPACESTATS;
    }

  heap_hdr->estimates.head = 0;
  for (i = npages; i < HEAP_NUM_BEST_SPACESTATS; i++)
    {
      VPID_SET_NULL (&heap_hdr->estimates.best[i].vpid);
      heap_hdr->estimates.best[i].freespace = 0;
    }

  heap_hdr->estimates.last_vpid = last_vpid;

  heap_hdr->reserve1_for_future = 0;
  heap_hdr->reserve2_for_future = 0;

  addr.pgptr = hdr_pgptr;
  addr.offset = HEAP_HEADER_AND_CHAIN_SLOTID;
  log_append_redo_data (thread_p, RVHF_STATS, &addr, sizeof (*heap_hdr),
			heap_hdr);
  pgbuf_set_dirty (thread_p, hdr_pgptr, FREE);
  hdr_pgptr = NULL;

  return hfid;

error:
  if (pgptr != NULL)
    {
      pgbuf_unfix_and_init (thread_p, pgptr);
    }
  if (hdr_pgptr != NULL)
    {
      pgbuf_unfix_and_init (thread_p, hdr_pgptr);
    }

  return NULL;
}

#if defined(CUBRID_DEBUG)
/*
 * heap_hfid_isvalid () -
 *   return:
 *   hfid(in):
 */
static DISK_ISVALID
heap_hfid_isvalid (HFID * hfid)
{
  DISK_ISVALID valid_pg = DISK_VALID;

  if (hfid == NULL || HFID_IS_NULL (hfid))
    {
      return DISK_INVALID;
    }

  valid_pg = disk_isvalid_page (hfid->vfid.volid, hfid->vfid.fileid);
  if (valid_pg == DISK_VALID)
    {
      valid_pg = disk_isvalid_page (hfid->vfid.volid, hfid->hpgid);
    }

  return valid_pg;
}

/*
 * heap_scanrange_isvalid () -
 *   return:
 *   scan_range(in):
 */
static DISK_ISVALID
heap_scanrange_isvalid (HEAP_SCANRANGE * scan_range)
{
  DISK_ISVALID valid_pg = DISK_INVALID;

  if (scan_range != NULL)
    {
      valid_pg = heap_hfid_isvalid (&scan_range->scan_cache.hfid);
    }

  if (valid_pg != DISK_VALID)
    {
      if (valid_pg != DISK_ERROR)
	{
	  er_log_debug (ARG_FILE_LINE,
			" ** SYSTEM ERROR scanrange has not been initialized");
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR,
		  0);
	}
    }

  return valid_pg;
}
#endif /* CUBRID_DEBUG */

/*
 * xheap_create () - Create a heap file
 *   return: int
 *   hfid(in/out): Object heap file identifier.
 *                 All fields in the identifier are set, except the volume
 *                 identifier which should have already been set by the caller.
 *   class_oid(in): OID of the class for which the heap will be created.
 *   reuse_oid(int):
 *
 * Note: Creates an object heap file on the disk volume associated with
 * hfid->vfid->volid.
 */
int
xheap_create (THREAD_ENTRY * thread_p, HFID * hfid, const OID * class_oid,
	      bool reuse_oid)
{
  if (heap_create_internal (thread_p, hfid, -1, class_oid, reuse_oid) == NULL)
    {
      return er_errid ();
    }
  else
    {
      return NO_ERROR;
    }
}

/*
 * xheap_destroy () - Destroy a heap file
 *   return: int
 *   hfid(in): Object heap file identifier.
 *
 * Note: Destroy the heap file associated with the given heap identifier.
 */
int
xheap_destroy (THREAD_ENTRY * thread_p, const HFID * hfid)
{
  VFID vfid;
  int ret;

  if (heap_ovf_find_vfid (thread_p, hfid, &vfid, false) != NULL)
    {
      ret = file_destroy (thread_p, &vfid);
      if (ret != NO_ERROR)
	{
	  return ret;
	}
    }

  ret = file_destroy (thread_p, &hfid->vfid);

  /* delete all entries of hfid */
  if (prm_get_integer_value (PRM_ID_HF_MAX_BESTSPACE_ENTRIES) > 0)
    {
      int rc;

      if (ret == NO_ERROR)
	{
	  rc = pthread_mutex_lock (&heap_Bestspace->bestspace_mutex);

	  (void) heap_stats_del_bestspace (thread_p, hfid, -1 /* no stop */ ,
					   INT_MAX);

	  assert (mht_count (heap_Bestspace->vpid_ht) ==
		  mht_count (heap_Bestspace->hfid_ht));

	  pthread_mutex_unlock (&heap_Bestspace->bestspace_mutex);
	}
    }

  return ret;
}

/*
 * xheap_destroy_newly_created () - Destroy heap if it is a newly created heap
 *   return: NO_ERROR
 *   hfid(in): Object heap file identifier.
 *
 * Note: Destroy the heap file associated with the given heap
 * identifier if it is a newly created heap file.
 */
int
xheap_destroy_newly_created (THREAD_ENTRY * thread_p, const HFID * hfid)
{
  VFID vfid;
  FILE_IS_NEW_FILE is_new_file;
  FILE_TYPE file_type;
  int ret;

  is_new_file = file_is_new_file (thread_p, &(hfid->vfid));
  if (is_new_file == FILE_ERROR)
    {
      return ER_FAILED;
    }

  if (is_new_file == FILE_NEW_FILE)
    {
      return xheap_destroy (thread_p, hfid);
    }

  file_type = file_get_type (thread_p, &(hfid->vfid));
  if (file_type == FILE_HEAP_REUSE_SLOTS)
    {
      return xheap_destroy (thread_p, hfid);
    }

  if (heap_ovf_find_vfid (thread_p, hfid, &vfid, false) != NULL)
    {
      ret = file_mark_as_deleted (thread_p, &vfid);
      if (ret != NO_ERROR)
	{
	  return ret;
	}
    }

  ret = file_mark_as_deleted (thread_p, &hfid->vfid);

  /* delete all entries of hfid */
  if (prm_get_integer_value (PRM_ID_HF_MAX_BESTSPACE_ENTRIES) > 0)
    {
      int rc;

      if (ret == NO_ERROR)
	{
	  rc = pthread_mutex_lock (&heap_Bestspace->bestspace_mutex);

	  (void) heap_stats_del_bestspace (thread_p, hfid, -1 /* no stop */ ,
					   INT_MAX);

	  assert (mht_count (heap_Bestspace->vpid_ht) ==
		  mht_count (heap_Bestspace->hfid_ht));

	  pthread_mutex_unlock (&heap_Bestspace->bestspace_mutex);
	}
    }

  return ret;
}

/*
 * heap_assign_address () - Assign a new location
 *   return: NO_ERROR / ER_FAILED
 *   hfid(in): Object heap file identifier
 *   oid(out): Object identifier.
 *   expected_length(in): Expected length
 *
 * Note: Assign an OID to an object and reserve the expected length for
 * the object. The following rules are observed for the expected length.
 *              1. A negative value is passed when only an approximation of
 *                 the length of the object is known. This approximation is
 *                 taken as the minimal length by this module. This case is
 *                 used when the transformer module (tfcl) skips some fileds
 *                 while walking through the object to find out its length.
 *                 a) Heap manager find the average length of objects in the
 *                    heap.
 *                    If the average length > abs(expected_length)
 *                    The average length is used instead
 *              2. A zero value, heap manager uses the average length of the
 *                 objects in the heap.
 *              3. If length is larger than one page, the size of an OID is
 *                 used since the object is going to be stored in overflow
 *              4. If length is > 0 and smaller than OID_SIZE
 *                 OID_SIZE is used as the expected length.
 */
int
heap_assign_address (THREAD_ENTRY * thread_p, const HFID * hfid, OID * oid,
		     int expected_length)
{
  RECDES recdes;

  if (expected_length <= 0)
    {
      recdes.length = heap_estimate_avg_length (thread_p, hfid);
      if (recdes.length > (-expected_length))
	{
	  expected_length = recdes.length;
	}
      else
	{
	  expected_length = -expected_length;
	}
    }

  /*
   * Use the expected length only when it is larger than the size of an OID
   * and it is smaller than the maximum size of an object that can be stored
   * in the primary area (no in overflow). In any other case, use the the size
   * of an OID as the length.
   */

  recdes.length = ((expected_length > SSIZEOF (OID)
		    && !heap_is_big_length (expected_length))
		   ? expected_length : SSIZEOF (OID));

  recdes.data = NULL;
  recdes.type = REC_ASSIGN_ADDRESS;

  return heap_insert_internal (thread_p, hfid, oid, &recdes, NULL, false,
			       recdes.length);
}

/*
 * heap_assign_address_with_class_oid () - Assign a new location and lock the
 *                                       object
 *   return:
 *   hfid(in):
 *   class_oid(in):
 *   oid(in):
 *   expected_length(in):
 */
int
heap_assign_address_with_class_oid (THREAD_ENTRY * thread_p,
				    const HFID * hfid, OID * class_oid,
				    OID * oid, int expected_length)
{
  RECDES recdes;

  if (expected_length <= 0)
    {
      recdes.length = heap_estimate_avg_length (thread_p, hfid);
      if (recdes.length > (-expected_length))
	{
	  expected_length = recdes.length;
	}
      else
	{
	  expected_length = -expected_length;
	}
    }

  /*
   * Use the expected length only when it is larger than the size of an OID
   * and it is smaller than the maximum size of an object that can be stored
   * in the primary area (no in overflow). In any other case, use the the size
   * of an OID as the length.
   */

  recdes.length = ((expected_length > SSIZEOF (OID)
		    && !heap_is_big_length (expected_length))
		   ? expected_length : SSIZEOF (OID));

  recdes.data = NULL;
  recdes.type = REC_ASSIGN_ADDRESS;

  return heap_insert_with_lock_internal (thread_p, hfid, oid, class_oid,
					 &recdes, NULL, false, recdes.length);
}

/*
 * heap_insert_internal () - Insert a non-multipage object onto heap
 *   return: NO_ERROR / ER_FAILED
 *   hfid(in): Object heap file identifier
 *   oid(out): Object identifier.
 *   recdes(in): Record descriptor
 *   scan_cache(in/out): Scan cache used to estimate the best space pages
 *                       between heap changes.
 *   ishome_insert(in):
 *   guess_sumlen(in):
 *
 * Note: Insert an object that does not expand multiple pages onto the
 * given file heap. The object is inserted by the following algorithm:
 *              1: If the object can be inserted in the best1 space page
 *                 (usually last allocated page of heap) without overpassing
 *                 the reserved space on the page, the object is placed on
 *                 this page.
 *              2: If the object can be inserted in one of the best space
 *                 pages without overpassing the reserved space on the page,
 *                 the object is placed on this page.
 *              3: The object is inserted in a newly allocated page. Don't
 *                 care about reserve space here.
 *
 * Note: This function does not store objects in overflow.
 */
static int
heap_insert_internal (THREAD_ENTRY * thread_p, const HFID * hfid, OID * oid,
		      RECDES * recdes, HEAP_SCANCACHE * scan_cache,
		      bool ishome_insert, int guess_sumlen)
{
  LOG_DATA_ADDR addr;		/* Address of logging data */
  int sp_success;
  bool isnew_rec;
  RECDES *undo_recdes;

  addr.vfid = &hfid->vfid;

  if (recdes->type != REC_NEWHOME)
    {
      isnew_rec = true;
    }
  else
    {
      /*
       * This is an old object (relocated) and we do not have any idea on
       * the difference in length.
       */
      isnew_rec = false;
    }

  OID_SET_NULL (oid);
#if defined(CUBRID_DEBUG)
  if (heap_is_big_length (recdes->length))
    {
      er_log_debug (ARG_FILE_LINE,
		    "heap_insert_internal: This function does not accept"
		    " objects longer than %d. An object of %d was given\n",
		    heap_Maxslotted_reclength, recdes->length);
      return ER_FAILED;
    }
#endif

  addr.pgptr = heap_stats_find_best_page (thread_p, hfid, recdes->length,
					  isnew_rec, guess_sumlen,
					  scan_cache);
  if (addr.pgptr == NULL)
    {
      /* something went wrong. Unable to fetch hinted page. Return */
      return ER_FAILED;
    }

  /* Insert the object */
  sp_success = spage_insert (thread_p, addr.pgptr, recdes, &oid->slotid);
  if (sp_success == SP_SUCCESS)
    {

      RECDES tmp_recdes;
      INT16 bytes_reserved;

      oid->volid = pgbuf_get_volume_id (addr.pgptr);
      oid->pageid = pgbuf_get_page_id (addr.pgptr);

      if (recdes->type == REC_ASSIGN_ADDRESS)
	{
	  bytes_reserved = (INT16) recdes->length;
	  tmp_recdes.type = recdes->type;
	  tmp_recdes.area_size = sizeof (bytes_reserved);
	  tmp_recdes.length = sizeof (bytes_reserved);
	  tmp_recdes.data = (char *) &bytes_reserved;
	  undo_recdes = &tmp_recdes;
	}
      else
	{
	  undo_recdes = recdes;
	}

      /* Log the insertion, set the page dirty, free, and unlock */
      addr.offset = oid->slotid;
      log_append_undoredo_recdes (thread_p, RVHF_INSERT, &addr, NULL,
				  undo_recdes);
      pgbuf_set_dirty (thread_p, addr.pgptr, DONT_FREE);
    }
  else
    {
      if (sp_success != SP_ERROR)
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR,
		  0);
	}
      oid->slotid = NULL_SLOTID;
    }

  /*
   * Cache the page for any future scan modifications
   */

  if (scan_cache != NULL && scan_cache->cache_last_fix_page == true
      && ishome_insert == true)
    {
      scan_cache->pgptr = addr.pgptr;
    }
  else
    {
      pgbuf_unfix_and_init (thread_p, addr.pgptr);
    }

  return NO_ERROR;
}



static PAGE_PTR
heap_find_slot_for_insert_with_lock (THREAD_ENTRY * thread_p,
				     const HFID * hfid,
				     HEAP_SCANCACHE * scan_cache,
				     OID * oid,
				     RECDES * recdes, OID * class_oid)
{
  int slot_id = 0;
  int lk_result;
  int slot_num;
  PAGE_PTR pgptr;

  pgptr = heap_stats_find_best_page (thread_p, hfid, recdes->length,
				     (recdes->type != REC_NEWHOME),
				     recdes->length, scan_cache);
  if (pgptr == NULL)
    {
      return NULL;
    }

  slot_num = spage_number_of_slots (pgptr);

  oid->volid = pgbuf_get_volume_id (pgptr);
  oid->pageid = pgbuf_get_page_id (pgptr);

  /* find REC_DELETED_WILL_REUSE slot or add new slot */
  /* slot_id == slot_num means add new slot */
  for (slot_id = 0; slot_id <= slot_num; slot_id++)
    {
      slot_id = spage_find_free_slot (pgptr, NULL, slot_id);
      oid->slotid = slot_id;

      /* lock the object to be inserted conditionally */
      lk_result = lock_object (thread_p, oid, class_oid, X_LOCK,
			       LK_COND_LOCK);
      if (lk_result == LK_GRANTED)
	{
	  return pgptr;		/* OK */
	}
      else if (lk_result != LK_NOTGRANTED_DUE_TIMEOUT)
	{
#if !defined(NDEBUG)
	  if (lk_result == LK_NOTGRANTED_DUE_ABORTED)
	    {
	      LOG_TDES *tdes = LOG_FIND_CURRENT_TDES (thread_p);
	      assert (tdes->tran_abort_reason ==
		      TRAN_ABORT_DUE_ROLLBACK_ON_ESCALATION);
	    }
	  else
	    {
	      assert (false);	/* unknown locking error */
	    }
#endif
	  OID_SET_NULL (oid);
	  pgbuf_unfix_and_init (thread_p, pgptr);
	  return NULL;
	}
    }

  /* This means there is no lockable (or add) slot even if
   * there is enough space. Impossible case */
  assert (false);
  OID_SET_NULL (oid);

  pgbuf_unfix_and_init (thread_p, pgptr);
  return NULL;
}


/*
 * heap_insert_with_lock_internal () -
 *   return:
 *   hfid(in):
 *   oid(in):
 *   class_oid(in):
 *   recdes(in):
 *   scan_cache(in):
 *   ishome_insert(in):
 *   guess_sumlen(in):
 */
static int
heap_insert_with_lock_internal (THREAD_ENTRY * thread_p, const HFID * hfid,
				OID * oid, OID * class_oid, RECDES * recdes,
				HEAP_SCANCACHE * scan_cache,
				bool ishome_insert, int guess_sumlen)
{
  LOG_DATA_ADDR addr;		/* Address of logging data */
  bool isnew_rec;
  RECDES tmp_recdes, *undo_recdes;
  INT16 bytes_reserved;

  addr.vfid = &hfid->vfid;

  if (recdes->type != REC_NEWHOME)
    {
      isnew_rec = true;
    }
  else
    {
      /*
       * This is an old object (relocated) and we do not have any idea on
       * the difference in length.
       */
      isnew_rec = false;
    }

  OID_SET_NULL (oid);
#if defined(CUBRID_DEBUG)
  if (heap_is_big_length (recdes->length))
    {
      er_log_debug (ARG_FILE_LINE,
		    "heap_insert_internal: This function does not accept"
		    " objects longer than %d. An object of %d was given\n",
		    heap_Maxslotted_reclength, recdes->length);
      return ER_FAILED;
    }
#endif

  /* The class object was already IX-locked during compile time
   * under normal situation.
   * However, with prepare-execute-commit-execute-... scenario,
   * the class object is not properly IX-locked since the previous commit
   * released the entire acquired locks including IX-lock.
   * So we have to make it sure the class object is IX-locked at this moment.
   */
  if (lock_object (thread_p, class_oid, oid_Root_class_oid,
		   IX_LOCK, LK_UNCOND_LOCK) != LK_GRANTED)
    {
      return ER_FAILED;
    }

  addr.pgptr = heap_find_slot_for_insert_with_lock (thread_p, hfid,
						    scan_cache, oid, recdes,
						    class_oid);
  if (addr.pgptr == NULL)
    {
      return ER_FAILED;
    }

  /* insert a original record */
  if (spage_insert_at (thread_p, addr.pgptr, oid->slotid, recdes)
      != SP_SUCCESS)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      oid = NULL;
      goto unfix_end;
    }

  if (recdes->type == REC_ASSIGN_ADDRESS)
    {
      bytes_reserved = (INT16) recdes->length;
      tmp_recdes.type = recdes->type;
      tmp_recdes.area_size = sizeof (bytes_reserved);
      tmp_recdes.length = sizeof (bytes_reserved);
      tmp_recdes.data = (char *) &bytes_reserved;
      undo_recdes = &tmp_recdes;
    }
  else
    {
      undo_recdes = recdes;
    }

  /* Log the insertion, set the page dirty, free, and unlock */
  addr.offset = oid->slotid;
  log_append_undoredo_recdes (thread_p, RVHF_INSERT, &addr, NULL,
			      undo_recdes);
  pgbuf_set_dirty (thread_p, addr.pgptr, DONT_FREE);

unfix_end:
  /*
   * Cache the page for any future scan modifications
   */
  if (scan_cache != NULL && scan_cache->cache_last_fix_page == true
      && ishome_insert == true)
    {
      scan_cache->pgptr = addr.pgptr;
    }
  else				/* unfix the page */
    {
      pgbuf_unfix_and_init (thread_p, addr.pgptr);
    }

  return (oid != NULL) ? NO_ERROR : ER_FAILED;
}

/*
 * heap_insert () - Insert an object onto heap
 *   return: OID *(oid on success or NULL on failure)
 *   hfid(in): Object heap file identifier
 *   class_oid(in):
 *   oid(out): : Object identifier.
 *   recdes(in): recdes: Record descriptor
 *   scan_cache(in/out): Scan cache used to estimate the best space pages
 *                       between heap changes.
 *
 * Note: Insert an object onto the given file heap. The object is
 * inserted using the following algorithm:
 *              1: If the object cannot be inserted in a single page, it is
 *                 inserted in overflow as a multipage object. An overflow
 *                 relocation record is created in the heap as an address map
 *                 to the actual content of the object (the overflow address).
 *              2: If the object can be inserted in the last allocated page
 *                 without overpassing the reserved space on the page, the
 *                 object is placed on this page.
 *              3: If the object can be inserted in the hinted page without
 *                 overpassing the reserved space on the page, the object is
 *       	   placed on this page.
 *              4: The object is inserted in a newly allocated page. Don't
 *                 about reserve space here.
 */
OID *
heap_insert (THREAD_ENTRY * thread_p, const HFID * hfid, OID * class_oid,
	     OID * oid, RECDES * recdes, HEAP_SCANCACHE * scan_cache)
{
  /*
   * If a scan cache for updates is given, make sure that it is for the
   * same heap, otherwise, end the current one and start a new one.
   */
  if (scan_cache != NULL)
    {
      if (scan_cache->debug_initpattern != HEAP_DEBUG_SCANCACHE_INITPATTERN)
	{
	  er_log_debug (ARG_FILE_LINE, "heap_insert: Your scancache is not"
			" initialized");
	  scan_cache = NULL;
	}
      else
	{
	  if (!HFID_EQ (&scan_cache->hfid, hfid)
	      || OID_ISNULL (&scan_cache->class_oid))
	    {
	      if (heap_scancache_reset_modify (thread_p, scan_cache, hfid,
					       class_oid) != NO_ERROR)
		{
		  return NULL;
		}
	    }
	}
    }

  if (heap_is_big_length (recdes->length))
    {
      /* This is a multipage object. It must be stored in overflow */
      OID ovf_oid;
      RECDES map_recdes;

      if (heap_ovf_insert (thread_p, hfid, &ovf_oid, recdes) == NULL)
	{
	  return NULL;
	}

      /* Add a map record to point to the record in overflow */
      map_recdes.type = REC_BIGONE;
      map_recdes.length = sizeof (ovf_oid);
      map_recdes.area_size = sizeof (ovf_oid);
      map_recdes.data = (char *) &ovf_oid;

      if (heap_insert_with_lock_internal (thread_p, hfid, oid, class_oid,
					  &map_recdes, scan_cache, true,
					  recdes->length) != NO_ERROR)
	{
	  /* Something went wrong, delete the overflow record */
	  (void) heap_ovf_delete (thread_p, hfid, &ovf_oid);
	  return NULL;
	}
    }
  else
    {
      recdes->type = REC_HOME;
      if (heap_insert_with_lock_internal (thread_p, hfid, oid, class_oid,
					  recdes, scan_cache, true,
					  recdes->length) != NO_ERROR)
	{
	  return NULL;
	}
    }

  if (heap_Guesschn != NULL && heap_Classrepr->rootclass_hfid != NULL
      && HFID_EQ ((hfid), heap_Classrepr->rootclass_hfid))
    {

      if (log_add_to_modified_class_list (thread_p, oid,
					  UPDATE_STATS_ACTION_KEEP) !=
	  NO_ERROR)
	{
	  return NULL;
	}

      if (csect_enter (thread_p, CSECT_HEAP_CHNGUESS, INF_WAIT) != NO_ERROR)
	{
	  return NULL;
	}

      heap_Guesschn->schema_change = true;
      (void) heap_chnguess_put (thread_p, oid,
				LOG_FIND_THREAD_TRAN_INDEX (thread_p),
				or_chn (recdes));

      csect_exit (thread_p, CSECT_HEAP_CHNGUESS);
    }

  return oid;
}

/*
 * heap_update () - Update an object
 *   return: OID *(oid on success or NULL on failure)
 *   hfid(in): Heap file identifier
 *   class_oid(in):
 *   oid(in): Object identifier
 *   recdes(in): Record descriptor
 *   old(in/out): Flag. Set to true, if content of object has been stored
 *                it is set to false (i.e., only the address was stored)
 *   scan_cache(in/out): Scan cache used to estimate the best space pages
 *                       between heap changes.
 *
 */
const OID *
heap_update (THREAD_ENTRY * thread_p, const HFID * hfid,
	     const OID * class_oid, const OID * oid, RECDES * recdes,
	     bool * old, HEAP_SCANCACHE * scan_cache)
{
  VPID vpid;			/* Volume and page identifiers */
  VPID *vpidptr_incache;
  LOG_DATA_ADDR addr;		/* Address of logging data     */
  LOG_DATA_ADDR forward_addr;	/* Address of forward data     */
  PAGE_PTR hdr_pgptr = NULL;	/* Page pointer to header page */
  INT16 type;
  OID new_forward_oid;
  RECDES new_forward_recdes;
  OID forward_oid;
  RECDES forward_recdes;
  int sp_success;
  DISK_ISVALID oid_valid;

  int again_count = 0;
  int again_max = 20;
  VPID home_vpid;
  VPID newhome_vpid;

  assert (class_oid != NULL && !OID_ISNULL (class_oid));

  HEAP_MAYNEED_DECACHE_GUESSED_LASTREPRS (oid, hfid, recdes);

  if (hfid == NULL)
    {
      if (scan_cache != NULL)
	{
	  hfid = &scan_cache->hfid;
	}
      else
	{
	  er_log_debug (ARG_FILE_LINE,
			"heap_update: Bad interface a heap is needed");
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_UNKNOWN_HEAP, 3,
		  "", NULL_FILEID, NULL_PAGEID);
	  return NULL;
	}
    }

  oid_valid = HEAP_ISVALID_OID (oid);
  if (oid_valid != DISK_VALID)
    {
      if (oid_valid != DISK_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_UNKNOWN_OBJECT, 3,
		  oid->volid, oid->pageid, oid->slotid);
	}
      return NULL;
    }

  *old = true;

try_again:

  addr.vfid = &hfid->vfid;
  forward_addr.vfid = &hfid->vfid;

  addr.pgptr = NULL;
  forward_addr.pgptr = NULL;
  hdr_pgptr = NULL;

  /*
   * Lock and fetch the page where the object is stored.
   */

  vpid.volid = oid->volid;
  vpid.pageid = oid->pageid;

  home_vpid.volid = oid->volid;
  home_vpid.pageid = oid->pageid;

  /*
   * If a scan cache for updates is given, make sure that it is for the
   * same heap, otherwise, end the current scan cache and start a new one.
   */

  if (scan_cache != NULL)
    {
      if (scan_cache->debug_initpattern != HEAP_DEBUG_SCANCACHE_INITPATTERN)
	{
	  er_log_debug (ARG_FILE_LINE, "heap_update: Your scancache is not"
			" initialized");
	  scan_cache = NULL;
	}
      else
	{
	  if (!HFID_EQ (&scan_cache->hfid, hfid)
	      || OID_ISNULL (&scan_cache->class_oid))
	    {
	      if (heap_scancache_reset_modify (thread_p, scan_cache, hfid,
					       class_oid) != NO_ERROR)
		{
		  goto error;
		}
	    }
	}

      /*
       * If the home page of object (OID) is the same as the cached page,
       * we do not need to fetch the page, already in the cache.
       */
      if (scan_cache != NULL
	  && scan_cache->cache_last_fix_page == true
	  && scan_cache->pgptr != NULL)
	{
	  vpidptr_incache = pgbuf_get_vpid_ptr (scan_cache->pgptr);
	  if (VPID_EQ (&vpid, vpidptr_incache))
	    {
	      /* We can skip the fetch operation */
	      addr.pgptr = scan_cache->pgptr;
	    }
	  else
	    {
	      /*
	       * Free the cached page
	       */
	      pgbuf_unfix_and_init (thread_p, scan_cache->pgptr);
	    }
	  /*
	   * Now remove the page from the scan cache. At the end this page or
	   * another one will be cached again.
	   */
	  scan_cache->pgptr = NULL;
	}
    }

  /*
   * If we do not have the home page already fetched, fetch it at this moment
   */

  if (addr.pgptr == NULL)
    {
      addr.pgptr = heap_scan_pb_lock_and_fetch (thread_p, &vpid, OLD_PAGE,
						X_LOCK, scan_cache);
      if (addr.pgptr == NULL)
	{
	  if (er_errid () == ER_PB_BAD_PAGEID)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_HEAP_UNKNOWN_OBJECT, 3, oid->volid, oid->pageid,
		      oid->slotid);
	    }

	  /* something went wrong, return */
	  goto error;
	}
    }

  recdes->type = type = spage_get_record_type (addr.pgptr, oid->slotid);
  if (recdes->type == REC_UNKNOWN)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_UNKNOWN_OBJECT, 3,
	      oid->volid, oid->pageid, oid->slotid);
      goto error;
    }

  switch (type)
    {
    case REC_RELOCATION:
      forward_recdes.data = (char *) &forward_oid;
      forward_recdes.length = sizeof (forward_oid);
      forward_recdes.area_size = sizeof (forward_oid);

      if (spage_get_record (addr.pgptr, oid->slotid, &forward_recdes, COPY)
	  != S_SUCCESS)
	{
	  /* Unable to get relocation record of the object */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_HEAP_BAD_RELOCATION_RECORD, 3, oid->volid, oid->pageid,
		  oid->slotid);
	  goto error;
	}

      /* Lock and fetch the page of new home (relocated/forwarded) record */
      vpid.volid = forward_oid.volid;
      vpid.pageid = forward_oid.pageid;

      newhome_vpid.volid = forward_oid.volid;
      newhome_vpid.pageid = forward_oid.pageid;

      /*
       * To avoid a possible deadlock, make sure that you do not wait on a
       * single lock. If we need to wait, release locks and request them in
       * one operation. In case of failure, the already released locks have
       * been freed.
       *
       * Note: that we have not peeked, so we do not need to fix anything at
       *       this moment.
       */

      forward_addr.pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE,
				      PGBUF_LATCH_WRITE,
				      PGBUF_CONDITIONAL_LATCH);
      if (forward_addr.pgptr == NULL)
	{
	  pgbuf_unfix_and_init (thread_p, addr.pgptr);

	  forward_addr.pgptr = heap_scan_pb_lock_and_fetch (thread_p, &vpid,
							    OLD_PAGE, X_LOCK,
							    scan_cache);
	  if (forward_addr.pgptr == NULL)
	    {
	      if (er_errid () == ER_PB_BAD_PAGEID)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_HEAP_UNKNOWN_OBJECT, 3, forward_oid.volid,
			  forward_oid.pageid, forward_oid.slotid);
		}

	      goto error;
	    }
	  addr.pgptr = pgbuf_fix (thread_p, &home_vpid, OLD_PAGE,
				  PGBUF_LATCH_WRITE, PGBUF_CONDITIONAL_LATCH);
	  if (addr.pgptr == NULL)
	    {
	      pgbuf_unfix_and_init (thread_p, forward_addr.pgptr);

	      if (again_count++ >= again_max)
		{
		  if (er_errid () == ER_PB_BAD_PAGEID)
		    {
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			      ER_HEAP_UNKNOWN_OBJECT, 3, oid->volid,
			      oid->pageid, oid->slotid);
		    }
		  else if (er_errid () == NO_ERROR)
		    {
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			      ER_PAGE_LATCH_ABORTED, 2, home_vpid.volid,
			      home_vpid.pageid);
		    }

		  goto error;
		}
	      else
		{
		  goto try_again;
		}
	    }
	}

#if defined(CUBRID_DEBUG)
      if (spage_get_record_type (forward_addr.pgptr, forward_oid.slotid) !=
	  REC_NEWHOME)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_BAD_OBJECT_TYPE,
		  3, forward_oid.volid, forward_oid.pageid,
		  forward_oid.slotid);
	  goto error;
	}
#endif

      /*
       * Can we move the object back to its home (OID) page ?
       */
      if (heap_is_big_length (recdes->length)
	  || spage_update (thread_p, addr.pgptr, oid->slotid,
			   recdes) != SP_SUCCESS)
	{
	  /*
	   * CANNOT BE RETURNED TO ITS HOME PAGE (OID PAGE)
	   * Try to update the object at relocated home page (content page)
	   */
	  if (heap_is_big_length (recdes->length)
	      || spage_is_updatable (thread_p, forward_addr.pgptr,
				     forward_oid.slotid, recdes) == false)
	    {

	      /* Header of heap */
	      vpid.volid = hfid->vfid.volid;
	      vpid.pageid = hfid->hpgid;

	      hdr_pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE,
				     PGBUF_LATCH_WRITE,
				     PGBUF_CONDITIONAL_LATCH);
	      if (hdr_pgptr == NULL)
		{
		  pgbuf_unfix_and_init (thread_p, addr.pgptr);
		  pgbuf_unfix_and_init (thread_p, forward_addr.pgptr);

		  hdr_pgptr = heap_scan_pb_lock_and_fetch (thread_p, &vpid,
							   OLD_PAGE, X_LOCK,
							   scan_cache);
		  if (hdr_pgptr == NULL)
		    {
		      goto error;
		    }

		  addr.pgptr = pgbuf_fix (thread_p, &home_vpid, OLD_PAGE,
					  PGBUF_LATCH_WRITE,
					  PGBUF_CONDITIONAL_LATCH);
		  if (addr.pgptr == NULL)
		    {
		      pgbuf_unfix_and_init (thread_p, hdr_pgptr);

		      if (again_count++ >= again_max)
			{
			  if (er_errid () == ER_PB_BAD_PAGEID)
			    {
			      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
				      ER_HEAP_UNKNOWN_OBJECT, 3, oid->volid,
				      oid->pageid, oid->slotid);
			    }
			  else if (er_errid () == NO_ERROR)
			    {
			      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
				      ER_PAGE_LATCH_ABORTED, 2,
				      home_vpid.volid, home_vpid.pageid);
			    }
			  goto error;
			}
		      else
			{
			  goto try_again;
			}
		    }
		  forward_addr.pgptr = pgbuf_fix (thread_p, &newhome_vpid,
						  OLD_PAGE, PGBUF_LATCH_WRITE,
						  PGBUF_CONDITIONAL_LATCH);
		  if (forward_addr.pgptr == NULL)
		    {
		      pgbuf_unfix_and_init (thread_p, hdr_pgptr);
		      pgbuf_unfix_and_init (thread_p, addr.pgptr);

		      if (again_count++ >= again_max)
			{
			  if (er_errid () == ER_PB_BAD_PAGEID)
			    {
			      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
				      ER_HEAP_UNKNOWN_OBJECT, 3,
				      forward_oid.volid, forward_oid.pageid,
				      forward_oid.slotid);
			    }
			  else if (er_errid () == NO_ERROR)
			    {
			      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
				      ER_PAGE_LATCH_ABORTED, 2,
				      newhome_vpid.volid,
				      newhome_vpid.pageid);
			    }

			  goto error;
			}
		      else
			{
			  goto try_again;
			}
		    }
		}

	      if (heap_is_big_length (recdes->length))
		{
		  /*
		   * The object has increased in length, it is now a multipage
		   * object. It MUST BE STORED IN OVERFLOW
		   */
		  if (heap_ovf_insert
		      (thread_p, hfid, &new_forward_oid, recdes) == NULL)
		    {
		      goto error;
		    }

		  new_forward_recdes.type = REC_BIGONE;
		}
	      else
		{
		  /*
		   * FIND A NEW HOME PAGE for the object
		   */
		  recdes->type = REC_NEWHOME;
		  if (heap_insert_internal (thread_p, hfid, &new_forward_oid,
					    recdes, scan_cache, false,
					    (recdes->length -
					     spage_get_record_length
					     (forward_addr.pgptr,
					      forward_oid.slotid))) !=
		      NO_ERROR)
		    {
		      /*
		       * Problems finding a new home. Return without any updates
		       */
		      goto error;
		    }
		  new_forward_recdes.type = REC_RELOCATION;
		}

	      /*
	       * Original record (i.e., at home) must point to new relocated
	       * content (either on overflow or on another heap page).
	       */

	      new_forward_recdes.data = (char *) &new_forward_oid;
	      new_forward_recdes.length = sizeof (new_forward_oid);
	      new_forward_recdes.area_size = sizeof (new_forward_oid);

	      sp_success = spage_update (thread_p, addr.pgptr, oid->slotid,
					 &new_forward_recdes);
	      if (sp_success != SP_SUCCESS)
		{
		  /*
		   * This is likely a system error since the length of forward
		   * records are smaller than any other record. Don't do anything
		   * undo the operation
		   */
		  if (sp_success != SP_ERROR)
		    {
		      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
			      ER_GENERIC_ERROR, 0);
		    }
#if defined(CUBRID_DEBUG)
		  er_log_debug (ARG_FILE_LINE,
				"heap_update: ** SYSTEM ERROR ** the"
				" length of relocation records is the smallest"
				" allowed.. slotted update could not fail ...\n");
#endif
		  if (new_forward_recdes.type == REC_BIGONE)
		    {
		      (void) heap_ovf_delete (thread_p, hfid,
					      &new_forward_oid);
		    }
		  else
		    {
		      (void) heap_delete_internal (thread_p, hfid,
						   &new_forward_oid,
						   scan_cache, false);
		    }
		  goto error;
		}
	      if (new_forward_recdes.type == REC_BIGONE)
		{
		  spage_update_record_type (thread_p, addr.pgptr, oid->slotid,
					    REC_BIGONE);
		}

	      /* Log the changes and then set the page dirty */
	      addr.offset = oid->slotid;
	      log_append_undoredo_recdes (thread_p, RVHF_UPDATE, &addr,
					  &forward_recdes,
					  &new_forward_recdes);

	      /* Delete the old new home (i.e., relocated record) */
	      (void) heap_delete_internal (thread_p, hfid, &forward_oid,
					   scan_cache, false);
	      pgbuf_set_dirty (thread_p, forward_addr.pgptr, FREE);
	      forward_addr.pgptr = NULL;
	      pgbuf_set_dirty (thread_p, addr.pgptr, DONT_FREE);
	      pgbuf_unfix_and_init (thread_p, hdr_pgptr);
	    }
	  else
	    {
	      /*
	       * OBJECT CAN BE UPDATED AT RELOCATED HOME PAGE (content page).
	       * We do not need to change relocation record (OID home record).
	       *
	       * We log first, to avoid copying the before image (old) object.
	       * This operation is correct since we already know that there is
	       * space to insert the object
	       */

	      if (spage_get_record (forward_addr.pgptr, forward_oid.slotid,
				    &forward_recdes, PEEK) != S_SUCCESS)
		{
		  /* Unable to keep forward imagen of object for logging */
		  goto error;
		}

	      recdes->type = REC_NEWHOME;
	      forward_addr.offset = forward_oid.slotid;
	      log_append_undoredo_recdes (thread_p, RVHF_UPDATE,
					  &forward_addr, &forward_recdes,
					  recdes);
	      sp_success =
		spage_update (thread_p, forward_addr.pgptr,
			      forward_oid.slotid, recdes);
	      if (sp_success != SP_SUCCESS)
		{
		  /*
		   * This is likely a system error since we have already checked
		   * for space. The page is lock in exclusive mode... How did it
		   * happen?
		   */
		  if (sp_success != SP_ERROR)
		    {
		      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
			      ER_GENERIC_ERROR, 0);
		    }
#if defined(CUBRID_DEBUG)
		  er_log_debug (ARG_FILE_LINE,
				"heap_update: ** SYSTEM_ERROR ** update"
				" operation failed even when have already checked"
				" for space");
#endif
		  goto error;
		}
	      pgbuf_set_dirty (thread_p, forward_addr.pgptr, FREE);
	      forward_addr.pgptr = NULL;
	    }
	}
      else
	{
	  /*
	   * The object was returned to its home (OID) page.
	   * Remove the old relocated record (old new home)
	   */

	  /* Indicate that this is home record */
	  recdes->type = REC_HOME;
	  spage_update_record_type (thread_p, addr.pgptr, oid->slotid,
				    recdes->type);

	  addr.offset = oid->slotid;
	  log_append_undoredo_recdes (thread_p, RVHF_UPDATE, &addr,
				      &forward_recdes, recdes);

	  /* Delete the relocated record (old home) */
	  (void) heap_delete_internal (thread_p, hfid, &forward_oid,
				       scan_cache, false);
	  pgbuf_set_dirty (thread_p, addr.pgptr, DONT_FREE);
	  pgbuf_set_dirty (thread_p, forward_addr.pgptr, FREE);
	  forward_addr.pgptr = NULL;
	}
      break;

    case REC_BIGONE:
      /*
       * The object stored in the heap page is a relocation_overflow record,
       * get the overflow address of the object
       */
      forward_recdes.data = (char *) &forward_oid;
      forward_recdes.length = sizeof (forward_oid);
      forward_recdes.area_size = sizeof (forward_oid);

      if (spage_get_record (addr.pgptr, oid->slotid, &forward_recdes,
			    COPY) != S_SUCCESS)
	{
	  /* Unable to peek overflow address of multipage object */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_HEAP_BAD_RELOCATION_RECORD, 3, oid->volid, oid->pageid,
		  oid->slotid);
	  goto error;
	}


      /* Header of heap */
      vpid.volid = hfid->vfid.volid;
      vpid.pageid = hfid->hpgid;

      hdr_pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			     PGBUF_CONDITIONAL_LATCH);
      if (hdr_pgptr == NULL)
	{
	  pgbuf_unfix_and_init (thread_p, addr.pgptr);

	  hdr_pgptr = heap_scan_pb_lock_and_fetch (thread_p, &vpid, OLD_PAGE,
						   X_LOCK, scan_cache);
	  if (hdr_pgptr == NULL)
	    {
	      goto error;
	    }

	  addr.pgptr = pgbuf_fix (thread_p, &home_vpid, OLD_PAGE,
				  PGBUF_LATCH_WRITE, PGBUF_CONDITIONAL_LATCH);
	  if (addr.pgptr == NULL)
	    {
	      pgbuf_unfix_and_init (thread_p, hdr_pgptr);

	      if (again_count++ >= again_max)
		{
		  if (er_errid () == ER_PB_BAD_PAGEID)
		    {
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			      ER_HEAP_UNKNOWN_OBJECT, 3, oid->volid,
			      oid->pageid, oid->slotid);
		    }
		  else if (er_errid () == NO_ERROR)
		    {
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			      ER_PAGE_LATCH_ABORTED, 2, home_vpid.volid,
			      home_vpid.pageid);
		    }
		  goto error;
		}
	      else
		{
		  goto try_again;
		}
	    }
	}

      /* Is the object still a multipage one ? */
      if (heap_is_big_length (recdes->length))
	{
	  /* Update object in overflow */
	  if (heap_ovf_update (thread_p, hfid, &forward_oid, recdes) == NULL)
	    {
	      goto error;
	    }
	}
      else
	{
	  /*
	   * The object is not a multipage object any longer. Store the object
	   * in the normal heap file
	   */

	  /* Can we return the object to its home ? */

	  if (spage_update (thread_p, addr.pgptr, oid->slotid, recdes) !=
	      SP_SUCCESS)
	    {
	      /*
	       * The object cannot be returned to its home page, relocate the
	       * object
	       */
	      recdes->type = REC_NEWHOME;
	      if (heap_insert_internal (thread_p, hfid, &new_forward_oid,
					recdes, scan_cache, false,
					(heap_ovf_get_length
					 (thread_p,
					  &forward_oid) - recdes->length)) !=
		  NO_ERROR)
		{
		  /* Problems finding a new home. Return without any modifications */
		  goto error;
		}

	      /*
	       * Update the OID relocation record to points to new home
	       */

	      new_forward_recdes.type = REC_RELOCATION;
	      new_forward_recdes.data = (char *) &new_forward_oid;
	      new_forward_recdes.length = sizeof (new_forward_oid);
	      new_forward_recdes.area_size = sizeof (new_forward_oid);

	      sp_success = spage_update (thread_p, addr.pgptr, oid->slotid,
					 &new_forward_recdes);
	      if (sp_success != SP_SUCCESS)
		{
		  /*
		   * This is likely a system error since the length of forward
		   * records are smaller than any other record. Don't do anything
		   * undo the operation
		   */
		  if (sp_success != SP_ERROR)
		    {
		      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
			      ER_GENERIC_ERROR, 0);
		    }
#if defined(CUBRID_DEBUG)
		  er_log_debug (ARG_FILE_LINE,
				"heap_update: ** SYSTEM ERROR ** the"
				" length of relocation records is the smallest"
				" allowed.. slotted update could not fail ...\n");
#endif
		  (void) heap_delete_internal (thread_p, hfid,
					       &new_forward_oid, scan_cache,
					       false);
		  goto error;
		}
	      spage_update_record_type (thread_p, addr.pgptr, oid->slotid,
					new_forward_recdes.type);
	      addr.offset = oid->slotid;
	      log_append_undoredo_recdes (thread_p, RVHF_UPDATE, &addr,
					  &forward_recdes,
					  &new_forward_recdes);
	    }
	  else
	    {
	      /*
	       * The record has been returned to its home
	       */
	      recdes->type = REC_HOME;
	      spage_update_record_type (thread_p, addr.pgptr, oid->slotid,
					recdes->type);
	      addr.offset = oid->slotid;
	      log_append_undoredo_recdes (thread_p, RVHF_UPDATE, &addr,
					  &forward_recdes, recdes);
	    }

	  (void) heap_ovf_delete (thread_p, hfid, &forward_oid);
	  pgbuf_set_dirty (thread_p, addr.pgptr, DONT_FREE);
	}

      pgbuf_unfix_and_init (thread_p, hdr_pgptr);
      break;

    case REC_ASSIGN_ADDRESS:
      /* This is a new object since only the address has been assigned to it */

      *old = false;
      /* Fall thru REC_HOME */

    case REC_HOME:
      /* Does object still fit at home address (OID page) ? */

      if (heap_is_big_length (recdes->length)
	  || spage_is_updatable (thread_p, addr.pgptr, oid->slotid,
				 recdes) == false)
	{
	  /*
	   * DOES NOT FIT ON HOME PAGE (OID page) ANY LONGER,
	   * a new home must be found.
	   */

	  /* Header of heap */
	  vpid.volid = hfid->vfid.volid;
	  vpid.pageid = hfid->hpgid;

	  hdr_pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
				 PGBUF_CONDITIONAL_LATCH);
	  if (hdr_pgptr == NULL)
	    {
	      pgbuf_unfix_and_init (thread_p, addr.pgptr);

	      hdr_pgptr = heap_scan_pb_lock_and_fetch (thread_p, &vpid,
						       OLD_PAGE, X_LOCK,
						       scan_cache);
	      if (hdr_pgptr == NULL)
		{
		  goto error;
		}

	      addr.pgptr = pgbuf_fix (thread_p, &home_vpid, OLD_PAGE,
				      PGBUF_LATCH_WRITE,
				      PGBUF_CONDITIONAL_LATCH);
	      if (addr.pgptr == NULL)
		{
		  pgbuf_unfix_and_init (thread_p, hdr_pgptr);

		  if (again_count++ >= again_max)
		    {
		      if (er_errid () == ER_PB_BAD_PAGEID)
			{
			  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
				  ER_HEAP_UNKNOWN_OBJECT, 3, oid->volid,
				  oid->pageid, oid->slotid);
			}
		      else if (er_errid () == NO_ERROR)
			{
			  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
				  ER_PAGE_LATCH_ABORTED, 2, home_vpid.volid,
				  home_vpid.pageid);
			}
		      goto error;
		    }
		  else
		    {
		      goto try_again;
		    }
		}
	    }

	  if (heap_is_big_length (recdes->length))
	    {
	      /*
	       * Object has became a multipage one.
	       * It must be stored in overflow
	       */
	      if (heap_ovf_insert (thread_p, hfid, &new_forward_oid, recdes)
		  == NULL)
		{
		  goto error;
		}

	      new_forward_recdes.type = REC_BIGONE;
	    }
	  else
	    {
	      /*
	       * Relocate the object. Find a new home
	       */
	      int len;

	      len = recdes->length - spage_get_record_length (addr.pgptr,
							      oid->slotid);
	      recdes->type = REC_NEWHOME;
	      if (heap_insert_internal (thread_p, hfid, &new_forward_oid,
					recdes, scan_cache, false,
					len) != NO_ERROR)
		{
		  /* Problems finding a new home. Return without any updates */
		  goto error;
		}
	      new_forward_recdes.type = REC_RELOCATION;
	    }

	  /*
	   * Original record (i.e., at home) must point to new overflow address
	   * or relocation address.
	   */

	  new_forward_recdes.data = (char *) &new_forward_oid;
	  new_forward_recdes.length = sizeof (new_forward_oid);
	  new_forward_recdes.area_size = sizeof (new_forward_oid);

	  /*
	   * We log first, to avoid copying the before image (old) object,
	   * instead we use the one that was peeked. This operation is fine
	   * since relocation records are the smallest record, thus they can
	   * always replace any object
	   */

	  /*
	   * Peek for the original content of the object. It is peeked using the
	   * forward recdes.
	   */
	  if (spage_get_record (addr.pgptr, oid->slotid, &forward_recdes,
				PEEK) != S_SUCCESS)
	    {
	      /* Unable to peek before image for logging purposes */
	      if (new_forward_recdes.type == REC_BIGONE)
		{
		  (void) heap_ovf_delete (thread_p, hfid, &new_forward_oid);
		}
	      else
		{
		  (void) heap_delete_internal (thread_p, hfid,
					       &new_forward_oid, scan_cache,
					       false);
		}
	      goto error;
	    }

	  addr.offset = oid->slotid;
	  log_append_undoredo_recdes (thread_p, RVHF_UPDATE, &addr,
				      &forward_recdes, &new_forward_recdes);

	  sp_success = spage_update (thread_p, addr.pgptr, oid->slotid,
				     &new_forward_recdes);
	  if (sp_success != SP_SUCCESS)
	    {
	      /*
	       * This is likely a system error since the length of forward
	       * records are smaller than any other record. Don't do anything
	       * undo the operation
	       */
	      if (sp_success != SP_ERROR)
		{
		  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_GENERIC_ERROR, 0);
		}
#if defined(CUBRID_DEBUG)
	      er_log_debug (ARG_FILE_LINE,
			    "heap_update: ** SYSTEM ERROR ** the"
			    " length of relocation records is the smallest"
			    " allowed.. slotted update could not fail ...\n");
#endif
	      if (new_forward_recdes.type == REC_BIGONE)
		{
		  (void) heap_ovf_delete (thread_p, hfid, &new_forward_oid);
		}
	      else
		{
		  (void) heap_delete_internal (thread_p, hfid,
					       &new_forward_oid, scan_cache,
					       false);
		}
	      goto error;
	    }
	  spage_update_record_type (thread_p, addr.pgptr, oid->slotid,
				    new_forward_recdes.type);
	  pgbuf_set_dirty (thread_p, addr.pgptr, DONT_FREE);
	  pgbuf_unfix_and_init (thread_p, hdr_pgptr);
	}
      else
	{
	  /*
	   * The object can be UPDATED AT THE SAME HOME PAGE (OID PAGE)
	   *
	   * We log first, to avoid copying the before image (old) object,
	   * instead we use the one that was peeked. This operation is fine
	   * since we already know that there is space to update
	   */

	  if (spage_get_record (addr.pgptr, oid->slotid, &forward_recdes,
				PEEK) != S_SUCCESS)
	    {
	      /* Unable to peek before image for logging purposes */
	      goto error;
	    }

	  /* For the logging */
	  addr.offset = oid->slotid;
	  recdes->type = REC_HOME;

	  log_append_undoredo_recdes (thread_p, RVHF_UPDATE, &addr,
				      &forward_recdes, recdes);

	  sp_success = spage_update (thread_p, addr.pgptr, oid->slotid,
				     recdes);
	  if (sp_success != SP_SUCCESS)
	    {
	      /*
	       * This is likely a system error since we have already checked
	       * for space
	       */
	      if (sp_success != SP_ERROR)
		{
		  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_GENERIC_ERROR, 0);
		}
#if defined(CUBRID_DEBUG)
	      er_log_debug (ARG_FILE_LINE,
			    "heap_update: ** SYSTEM_ERROR ** update"
			    " operation failed even when have already checked"
			    " for space");
#endif
	      goto error;
	    }
	  spage_update_record_type (thread_p, addr.pgptr, oid->slotid,
				    recdes->type);
	  pgbuf_set_dirty (thread_p, addr.pgptr, DONT_FREE);
	}
      break;

    case REC_NEWHOME:
    case REC_MARKDELETED:
    case REC_DELETED_WILL_REUSE:
    default:
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_BAD_OBJECT_TYPE, 3,
	      oid->volid, oid->pageid, oid->slotid);
      goto error;
    }

  /*
   * Cache the page for any future scan modifications
   */
  if (scan_cache != NULL && scan_cache->cache_last_fix_page == true)
    {
      scan_cache->pgptr = addr.pgptr;
    }
  else
    {
      pgbuf_unfix_and_init (thread_p, addr.pgptr);
    }

  if (heap_Guesschn != NULL && heap_Classrepr->rootclass_hfid != NULL
      && HFID_EQ ((hfid), heap_Classrepr->rootclass_hfid))
    {
      if (log_add_to_modified_class_list (thread_p, oid,
					  UPDATE_STATS_ACTION_KEEP) !=
	  NO_ERROR)
	{
	  goto error;
	}

      if (csect_enter (thread_p, CSECT_HEAP_CHNGUESS, INF_WAIT) != NO_ERROR)
	{
	  goto error;
	}

      heap_Guesschn->schema_change = true;
      (void) heap_chnguess_put (thread_p, (OID *) oid,
				LOG_FIND_THREAD_TRAN_INDEX (thread_p),
				or_chn (recdes));
      csect_exit (thread_p, CSECT_HEAP_CHNGUESS);
    }

  return oid;

error:
  if (addr.pgptr != NULL)
    {
      pgbuf_unfix_and_init (thread_p, addr.pgptr);
    }
  if (forward_addr.pgptr != NULL)
    {
      pgbuf_unfix_and_init (thread_p, forward_addr.pgptr);
    }
  if (hdr_pgptr != NULL)
    {
      pgbuf_unfix_and_init (thread_p, hdr_pgptr);
    }

  return NULL;
}

/*
 * heap_delete () - Delete an object from heap file
 *   return: OID *(oid on success or NULL on failure)
 *   hfid(in): Heap file identifier
 *   oid(in): Object identifier
 *   scan_cache(in/out): Scan cache used to estimate the best space pages
 *                       between heap changes.
 *
 * Note: Delete the object associated with the given OID from the given
 * heap file. If the object has been relocated or stored in
 * overflow, both the relocation and the relocated record are deleted.
 */
const OID *
heap_delete (THREAD_ENTRY * thread_p, const HFID * hfid, const OID * oid,
	     HEAP_SCANCACHE * scan_cache)
{
  int ret = NO_ERROR;

  /*
   * If a scan cache for updates is given, make sure that it is for the
   * same heap, otherwise, end the current one and start a new one.
   */
  if (scan_cache != NULL)
    {
      if (scan_cache->debug_initpattern != HEAP_DEBUG_SCANCACHE_INITPATTERN)
	{
	  er_log_debug (ARG_FILE_LINE, "heap_delete: Your scancache is not"
			" initialized");
	  scan_cache = NULL;
	}
      else
	{
	  if (!HFID_EQ (&scan_cache->hfid, hfid))
	    {
	      /*
	       * A different heap is used, recache the hash
	       */
	      ret =
		heap_scancache_reset_modify (thread_p, scan_cache, hfid,
					     NULL);
	      if (ret != NO_ERROR)
		{
		  return NULL;
		}
	    }
	}
    }
  if (heap_Guesschn != NULL && heap_Classrepr->rootclass_hfid != NULL
      && HFID_EQ ((hfid), heap_Classrepr->rootclass_hfid))
    {

      if (log_add_to_modified_class_list (thread_p, oid,
					  UPDATE_STATS_ACTION_RESET) !=
	  NO_ERROR)
	{
	  return NULL;
	}

      if (csect_enter (thread_p, CSECT_HEAP_CHNGUESS, INF_WAIT) != NO_ERROR)
	{
	  return NULL;
	}

      heap_Guesschn->schema_change = true;
      ret = heap_chnguess_decache (oid);

      csect_exit (thread_p, CSECT_HEAP_CHNGUESS);
    }

  return heap_delete_internal (thread_p, hfid, oid, scan_cache, true);
}

/*
 * heap_delete_internal () - Delete an object from heap file
 *   return: OID *(oid on success or NULL on failure)
 *   hfid(in): Heap file identifier
 *   oid(in): Object identifier
 *   scan_cache(in):
 *   ishome_delete(in):
 *
 * Note: Delete the object associated with the given OID from the given
 * heap file. If the object has been relocated or stored in
 * overflow, both the relocation and the relocated record are deleted.
 *
 * Note: If the heap is an OID reusable heap we are certain its records are
 * not referenced anywhere (i.e. there are no OIDs pointing to the current
 * heap file). We can mark the slot of the deleted record as reusable
 * (REC_DELETED_WILL_REUSE). Referable instances heaps must keep the slot as
 * non-reusable (REC_MARKDELETED) so that the existing references to the
 * deleted object can be resolved to NULL.
 * Marking the slot as reusable cannot be performed immediately because the
 * current transaction might need to rollback and the deleted object must be
 * inserted during the UNDO. The inserted object will be assigned the same OID
 * so we need to "reserve" its slot until the end of the current transaction;
 * the slot can then be marked as reusable. This is why
 * RVHF_MARK_REUSABLE_SLOT is logged using postponed log actions with
 * log_append_postpone ().
 */
static const OID *
heap_delete_internal (THREAD_ENTRY * thread_p, const HFID * hfid,
		      const OID * oid, HEAP_SCANCACHE * scan_cache,
		      bool ishome_delete)
{
  VPID vpid;			/* Volume and page identifiers */
  VPID *vpidptr_incache;
  LOG_DATA_ADDR addr;		/* Address of logging data     */
  LOG_DATA_ADDR forward_addr;
  PAGE_PTR hdr_pgptr = NULL;
  INT16 type;
  int prev_freespace;
  OID forward_oid;
  RECDES forward_recdes;
  RECDES undo_recdes;
  DISK_ISVALID oid_valid;
  int again_count = 0;
  int again_max = 20;
  VPID home_vpid;
  bool reuse_oid = false;
  FILE_TYPE file_type = FILE_UNKNOWN_TYPE;
  FILE_IS_NEW_FILE is_new_file = FILE_ERROR;
  bool need_update;

  oid_valid = HEAP_ISVALID_OID (oid);
  if (oid_valid != DISK_VALID)
    {
      if (oid_valid != DISK_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_UNKNOWN_OBJECT, 3,
		  oid->volid, oid->pageid, oid->slotid);
	}
      return NULL;
    }

  if (scan_cache != NULL)
    {
      assert (HFID_EQ (hfid, &scan_cache->hfid));
      assert (scan_cache->file_type != FILE_UNKNOWN_TYPE);
      assert (scan_cache->is_new_file != FILE_ERROR);

      file_type = scan_cache->file_type;
      is_new_file = scan_cache->is_new_file;
    }
  else
    {
      file_type = file_get_type (thread_p, &hfid->vfid);
      is_new_file = file_is_new_file (thread_p, &hfid->vfid);
    }
  if (file_type != FILE_HEAP && file_type != FILE_HEAP_REUSE_SLOTS)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return NULL;
    }
  reuse_oid = heap_is_reusable_oid (file_type);

try_again:

  addr.vfid = &hfid->vfid;
  forward_addr.vfid = &hfid->vfid;

  addr.pgptr = NULL;
  forward_addr.pgptr = NULL;
  hdr_pgptr = NULL;

  /*
   * Lock and fetch the page where the object is stored.
   */

  vpid.volid = oid->volid;
  vpid.pageid = oid->pageid;

  home_vpid.volid = oid->volid;
  home_vpid.pageid = oid->pageid;

  if (scan_cache != NULL)
    {
      if (scan_cache != NULL && scan_cache->cache_last_fix_page == true
	  && scan_cache->pgptr != NULL)
	{
	  vpidptr_incache = pgbuf_get_vpid_ptr (scan_cache->pgptr);
	  if (VPID_EQ (&vpid, vpidptr_incache))
	    {
	      /* We can skip the fetch operation */
	      addr.pgptr = scan_cache->pgptr;
	    }
	  else
	    {
	      /*
	       * Free the cached page
	       */
	      pgbuf_unfix_and_init (thread_p, scan_cache->pgptr);
	    }
	  /*
	   * Now remove the page from the scan cache. At the end this page or
	   * another one will be cached again.
	   */
	  scan_cache->pgptr = NULL;
	}
    }

  if (addr.pgptr == NULL)
    {
      addr.pgptr = heap_scan_pb_lock_and_fetch (thread_p, &vpid, OLD_PAGE,
						X_LOCK, scan_cache);
      if (addr.pgptr == NULL)
	{
	  if (er_errid () == ER_PB_BAD_PAGEID)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_HEAP_UNKNOWN_OBJECT, 3, oid->volid, oid->pageid,
		      oid->slotid);
	    }

	  /* something went wrong, return */
	  goto error;
	}
    }

  type = spage_get_record_type (addr.pgptr, oid->slotid);
  if (type == REC_UNKNOWN)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_UNKNOWN_OBJECT, 3,
	      oid->volid, oid->pageid, oid->slotid);
      goto error;
    }

  switch (type)
    {
    case REC_RELOCATION:
      /*
       * The object stored on the page is a relocation record. The relocation
       * record is used as a map to find the actual location of the content of
       * the object.
       *
       * To avoid deadlocks, see heap_update. We do not move to a second page
       * until we are done with current page.
       */

      forward_recdes.data = (char *) &forward_oid;
      forward_recdes.length = sizeof (forward_oid);
      forward_recdes.area_size = sizeof (forward_oid);

      if (spage_get_record (addr.pgptr, oid->slotid, &forward_recdes,
			    COPY) != S_SUCCESS)
	{
	  /* Unable to peek relocation record of the object */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_HEAP_BAD_RELOCATION_RECORD, 3, oid->volid, oid->pageid,
		  oid->slotid);
	  goto error;
	}

      /* Lock and fetch the page of new home (relocated/forwarded) record */
      vpid.volid = forward_oid.volid;
      vpid.pageid = forward_oid.pageid;

      /*
       * To avoid a possible deadlock, make sure that you do not wait on a
       * single lock. If we need to wait, release locks and request them in
       * one operation. In case of failure, the already released locks have
       * been freed.
       *
       * Note: that we have not peeked, so we do not need to fix anything at
       *       this moment.
       */

      forward_addr.pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE,
				      PGBUF_LATCH_WRITE,
				      PGBUF_CONDITIONAL_LATCH);
      if (forward_addr.pgptr == NULL)
	{
	  pgbuf_unfix_and_init (thread_p, addr.pgptr);

	  forward_addr.pgptr = heap_scan_pb_lock_and_fetch (thread_p, &vpid,
							    OLD_PAGE, X_LOCK,
							    scan_cache);
	  if (forward_addr.pgptr == NULL)
	    {
	      if (er_errid () == ER_PB_BAD_PAGEID)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_HEAP_UNKNOWN_OBJECT, 3, forward_oid.volid,
			  forward_oid.pageid, forward_oid.slotid);
		}

	      goto error;
	    }
	  addr.pgptr = pgbuf_fix (thread_p, &home_vpid, OLD_PAGE,
				  PGBUF_LATCH_WRITE, PGBUF_CONDITIONAL_LATCH);
	  if (addr.pgptr == NULL)
	    {
	      pgbuf_unfix_and_init (thread_p, forward_addr.pgptr);

	      if (again_count++ >= again_max)
		{
		  if (er_errid () == ER_PB_BAD_PAGEID)
		    {
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			      ER_HEAP_UNKNOWN_OBJECT, 3, oid->volid,
			      oid->pageid, oid->slotid);
		    }
		  else if (er_errid () == NO_ERROR)
		    {
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			      ER_PAGE_LATCH_ABORTED, 2, home_vpid.volid,
			      home_vpid.pageid);
		    }
		  goto error;
		}
	      else
		{
		  goto try_again;
		}
	    }
	}

#if defined(CUBRID_DEBUG)
      if (spage_get_record_type (forward_addr.pgptr, forward_oid.slotid) !=
	  REC_NEWHOME)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_BAD_OBJECT_TYPE,
		  3, forward_oid.volid, forward_oid.pageid,
		  forward_oid.slotid);
	  goto error;
	}
#endif

      /* Remove home and forward (relocated) objects */

      /* Find the contents of the record for logging purposes */
      if (spage_get_record (forward_addr.pgptr, forward_oid.slotid,
			    &undo_recdes, PEEK) != S_SUCCESS)
	{
	  goto error;
	}

      /* Log and delete the object, and set the page dirty */

      /* Remove the home object */
      addr.offset = oid->slotid;
      log_append_undoredo_recdes (thread_p, RVHF_DELETE, &addr,
				  &forward_recdes, NULL);

      prev_freespace =
	spage_get_free_space_without_saving (thread_p, addr.pgptr,
					     &need_update);

      (void) spage_delete (thread_p, addr.pgptr, oid->slotid);

      if (reuse_oid)
	{
	  log_append_postpone (thread_p, RVHF_MARK_REUSABLE_SLOT, &addr, 0,
			       NULL);
	}

      heap_stats_update (thread_p, addr.pgptr, hfid, is_new_file,
			 prev_freespace);

      pgbuf_set_dirty (thread_p, addr.pgptr, DONT_FREE);

      /* Remove the new home object */
      /*
       * TODO The RVHF_DELETE_NEWHOME log record type is currently unused.
       *      Document here why.
       */
      forward_addr.offset = forward_oid.slotid;
      log_append_undoredo_recdes (thread_p, RVHF_DELETE, &forward_addr,
				  &undo_recdes, NULL);

      prev_freespace =
	spage_get_free_space_without_saving (thread_p, forward_addr.pgptr,
					     &need_update);

      (void) spage_delete (thread_p, forward_addr.pgptr, forward_oid.slotid);

      /*
       * It should be safe to mark the new home slot as reusable regardless of
       * the heap type (reusable OID or not) as the relocated record should
       * not be referenced anywhere in the database.
       */
      log_append_postpone (thread_p, RVHF_MARK_REUSABLE_SLOT, &forward_addr,
			   0, NULL);

      heap_stats_update (thread_p, forward_addr.pgptr, hfid, is_new_file,
			 prev_freespace);

      pgbuf_set_dirty (thread_p, forward_addr.pgptr, FREE);
      forward_addr.pgptr = NULL;
      break;

    case REC_BIGONE:
      /*
       * The object stored in the heap page is a relocation_overflow record,
       * get the overflow address of the object
       */


      /* Header of heap */
      vpid.volid = hfid->vfid.volid;
      vpid.pageid = hfid->hpgid;

      hdr_pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			     PGBUF_CONDITIONAL_LATCH);
      if (hdr_pgptr == NULL)
	{
	  pgbuf_unfix_and_init (thread_p, addr.pgptr);

	  hdr_pgptr = heap_scan_pb_lock_and_fetch (thread_p, &vpid, OLD_PAGE,
						   X_LOCK, scan_cache);
	  if (hdr_pgptr == NULL)
	    {
	      goto error;
	    }

	  addr.pgptr = pgbuf_fix (thread_p, &home_vpid, OLD_PAGE,
				  PGBUF_LATCH_WRITE, PGBUF_CONDITIONAL_LATCH);
	  if (addr.pgptr == NULL)
	    {
	      pgbuf_unfix_and_init (thread_p, hdr_pgptr);

	      if (again_count++ >= again_max)
		{
		  if (er_errid () == ER_PB_BAD_PAGEID)
		    {
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			      ER_HEAP_UNKNOWN_OBJECT, 3, oid->volid,
			      oid->pageid, oid->slotid);
		    }
		  else if (er_errid () == NO_ERROR)
		    {
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			      ER_PAGE_LATCH_ABORTED, 2, home_vpid.volid,
			      home_vpid.pageid);
		    }
		  goto error;
		}
	      else
		{
		  goto try_again;
		}
	    }
	}

      forward_recdes.data = (char *) &forward_oid;
      forward_recdes.length = sizeof (forward_oid);
      forward_recdes.area_size = sizeof (forward_oid);

      if (spage_get_record (addr.pgptr, oid->slotid, &forward_recdes,
			    COPY) != S_SUCCESS)
	{
	  /* Unable to peek overflow address of multipage object */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_HEAP_BAD_RELOCATION_RECORD, 3, oid->volid, oid->pageid,
		  oid->slotid);
	  goto error;
	}

      /* Remove the home object */
      addr.offset = oid->slotid;
      log_append_undoredo_recdes (thread_p, RVHF_DELETE, &addr,
				  &forward_recdes, NULL);

      prev_freespace =
	spage_get_free_space_without_saving (thread_p, addr.pgptr,
					     &need_update);

      (void) spage_delete (thread_p, addr.pgptr, oid->slotid);

      if (reuse_oid)
	{
	  log_append_postpone (thread_p, RVHF_MARK_REUSABLE_SLOT, &addr, 0,
			       NULL);
	}

      heap_stats_update (thread_p, addr.pgptr, hfid, is_new_file,
			 prev_freespace);

      pgbuf_set_dirty (thread_p, addr.pgptr, DONT_FREE);

      /* Now remove the forward (relocated/forward) object */

      if (heap_ovf_delete (thread_p, hfid, &forward_oid) == NULL)
	{
	  goto error;
	}

      pgbuf_unfix_and_init (thread_p, hdr_pgptr);
      break;

    case REC_HOME:
    case REC_NEWHOME:
    case REC_ASSIGN_ADDRESS:
      /* Find the content of the record for logging purposes */
      if (spage_get_record (addr.pgptr, oid->slotid, &undo_recdes,
			    PEEK) != S_SUCCESS)
	{
	  /* Unable to peek before image for logging purposes */
	  goto error;
	}

      /* Log and remove the object */
      addr.offset = oid->slotid;
      log_append_undoredo_recdes (thread_p, RVHF_DELETE, &addr, &undo_recdes,
				  NULL);
      prev_freespace =
	spage_get_free_space_without_saving (thread_p, addr.pgptr,
					     &need_update);

      (void) spage_delete (thread_p, addr.pgptr, oid->slotid);

      if (reuse_oid)
	{
	  log_append_postpone (thread_p, RVHF_MARK_REUSABLE_SLOT, &addr, 0,
			       NULL);
	}

      heap_stats_update (thread_p, addr.pgptr, hfid, is_new_file,
			 prev_freespace);

      pgbuf_set_dirty (thread_p, addr.pgptr, DONT_FREE);
      break;

    case REC_MARKDELETED:
    case REC_DELETED_WILL_REUSE:
    default:
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_BAD_OBJECT_TYPE, 3,
	      oid->volid, oid->pageid, oid->slotid);
      goto error;
    }

  /*
   * Cache the page for any future scan modifications
   */
  if (scan_cache != NULL && scan_cache->cache_last_fix_page == true
      && ishome_delete == true)
    {
      scan_cache->pgptr = addr.pgptr;
    }
  else
    {
      pgbuf_unfix_and_init (thread_p, addr.pgptr);
    }

  return oid;

error:
  if (addr.pgptr != NULL)
    {
      pgbuf_unfix_and_init (thread_p, addr.pgptr);
    }
  if (forward_addr.pgptr != NULL)
    {
      pgbuf_unfix_and_init (thread_p, forward_addr.pgptr);
    }
  if (hdr_pgptr != NULL)
    {
      pgbuf_unfix_and_init (thread_p, hdr_pgptr);
    }

  return NULL;
}

/*
 * heap_flush () - Flush all dirty pages where the object resides
 *   return:
 *   oid(in): Object identifier
 *
 * Note: Flush all dirty pages where the object resides.
 */
void
heap_flush (THREAD_ENTRY * thread_p, const OID * oid)
{
  VPID vpid;			/* Volume and page identifiers */
  PAGE_PTR pgptr = NULL;	/* Page pointer                */
  INT16 type;
  OID forward_oid;
  RECDES forward_recdes;
  int ret = NO_ERROR;

  if (HEAP_ISVALID_OID (oid) != DISK_VALID)
    {
      return;
    }

  /*
   * Lock and fetch the page where the object is stored
   */
  vpid.volid = oid->volid;
  vpid.pageid = oid->pageid;
  pgptr = heap_scan_pb_lock_and_fetch (thread_p, &vpid, OLD_PAGE, S_LOCK,
				       NULL);
  if (pgptr == NULL)
    {
      if (er_errid () == ER_PB_BAD_PAGEID)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_UNKNOWN_OBJECT,
		  3, oid->volid, oid->pageid, oid->slotid);
	}
      /* something went wrong, return */
      return;
    }

  type = spage_get_record_type (pgptr, oid->slotid);
  if (type == REC_UNKNOWN)
    {
      goto end;
    }

  /* If this page is dirty flush it */
  (void) pgbuf_flush_with_wal (thread_p, pgptr);

  switch (type)
    {
    case REC_RELOCATION:
      /*
       * The object stored on the page is a relocation record. The relocation
       * record is used as a map to find the actual location of the content of
       * the object.
       */
      forward_recdes.data = (char *) &forward_oid;
      forward_recdes.length = sizeof (forward_oid);
      forward_recdes.area_size = sizeof (forward_oid);

      if (spage_get_record (pgptr, oid->slotid, &forward_recdes, COPY)
	  != S_SUCCESS)
	{
	  /* Unable to get relocation record of the object */
	  goto end;
	}
      pgbuf_unfix_and_init (thread_p, pgptr);

      /* Fetch the new home page */
      vpid.volid = forward_oid.volid;
      vpid.pageid = forward_oid.pageid;

      pgptr = heap_scan_pb_lock_and_fetch (thread_p, &vpid, OLD_PAGE, S_LOCK,
					   NULL);
      if (pgptr == NULL)
	{
	  if (er_errid () == ER_PB_BAD_PAGEID)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_HEAP_UNKNOWN_OBJECT, 3, forward_oid.volid,
		      forward_oid.pageid, forward_oid.slotid);
	    }

	  return;
	}
      (void) pgbuf_flush_with_wal (thread_p, pgptr);
      break;

    case REC_BIGONE:
      /*
       * The object stored in the heap page is a relocation_overflow record,
       * get the overflow address of the object
       */
      forward_recdes.data = (char *) &forward_oid;
      forward_recdes.length = sizeof (forward_oid);
      forward_recdes.area_size = sizeof (forward_oid);

      if (spage_get_record (pgptr, oid->slotid, &forward_recdes, COPY)
	  != S_SUCCESS)
	{
	  /* Unable to peek overflow address of multipage object */
	  goto end;
	}

      pgbuf_unfix_and_init (thread_p, pgptr);
      ret = heap_ovf_flush (thread_p, &forward_oid);
      break;

    case REC_ASSIGN_ADDRESS:
    case REC_HOME:
    case REC_NEWHOME:
    case REC_MARKDELETED:
    case REC_DELETED_WILL_REUSE:
    default:
      break;
    }

end:
  if (pgptr != NULL)
    {
      pgbuf_unfix_and_init (thread_p, pgptr);
    }
}

/*
 * xheap_reclaim_addresses () - Reclaim addresses/OIDs and delete empty pages
 *   return: NO_ERROR
 *   hfid(in): Heap file identifier
 *
 * Note: Reclaim the addresses (OIDs) of deleted objects of the given heap and
 *       delete all the heap pages that are left empty.
 *
 *       This function can be called:
 *    a: When there are no more references to deleted objects of the given
 *       heap. This happens during offline compactdb execution after all the
 *       classes in the schema have been processed by the process_class ()
 *       function that sets the references to deleted objects to NULL.
 *    b: When we are sure there can be no references to any object of the
 *       associated class. This happens during online compactdb execution when
 *       all the classes in the schema are checked to see if can they point to
 *       instances of the current class by checking all their atributes'
 *       domains.
 *
 *       If references to deleted objects were nulled by the current
 *       transaction some recovery problems may happen in the case of a crash
 *       since the reclaiming of the addresses is done without logging (or
 *       very little one) and thus it cannot be fully undone. Some logging is
 *       done to make sure that media recovery will not be impacted. This was
 *       done to avoid a lot of unneeded logging. Thus, if the caller was
 *       setting references to deleted objects to NULL, the caller must commit
 *       his transaction before this function is invoked.
 *
 *      This function must be run:
 *   a: offline, that is, when the user is the only one using the database
 *      system.
 *   b: online while holding an exclusive lock on the associated class.
 */
int
xheap_reclaim_addresses (THREAD_ENTRY * thread_p, const HFID * hfid)
{
  VPID vpid;
  VPID prv_vpid;
  int best;
  PAGE_PTR hdr_pgptr = NULL;
  HEAP_HDR_STATS initial_heap_hdr;
  HEAP_HDR_STATS heap_hdr;
  RECDES hdr_recdes;
  PAGE_PTR pgptr = NULL;
  LOG_DATA_ADDR addr;
  int ret = NO_ERROR;
  int free_space;
  int npages, nrecords, rec_length;
  FILE_IS_NEW_FILE is_new_file;
  bool need_update;

  is_new_file = file_is_new_file (thread_p, &hfid->vfid);
  if (is_new_file == FILE_ERROR)
    {
      goto exit_on_error;
    }
  if (is_new_file == FILE_NEW_FILE)
    {
      /*
       * This function should not be called for new files. It should work for
       * new files but it was only intended to be used by a compactdb tool
       * that does not operate on new classes.
       */
      assert (false);
      goto exit_on_error;
    }

  addr.vfid = &hfid->vfid;
  addr.pgptr = NULL;
  addr.offset = 0;

  vpid.volid = hfid->vfid.volid;
  vpid.pageid = hfid->hpgid;

  hdr_pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			 PGBUF_UNCONDITIONAL_LATCH);
  if (hdr_pgptr == NULL)
    {
      goto exit_on_error;
    }

  hdr_recdes.data = (char *) &heap_hdr;
  hdr_recdes.area_size = sizeof (heap_hdr);

  if (spage_get_record (hdr_pgptr, HEAP_HEADER_AND_CHAIN_SLOTID, &hdr_recdes,
			COPY) != S_SUCCESS
      || file_find_last_page (thread_p, &hfid->vfid, &prv_vpid) == NULL)
    {
      goto exit_on_error;
    }

  /* Copy the header to memory.. so we can log the changes */
  memcpy (&initial_heap_hdr, hdr_recdes.data, sizeof (initial_heap_hdr));

  /*
   * Initialize best estimates
   */
  heap_hdr.estimates.num_pages = 0;
  heap_hdr.estimates.num_recs = 0;
  heap_hdr.estimates.recs_sumlen = 0.0;
  heap_hdr.estimates.num_high_best = 0;
  heap_hdr.estimates.num_other_high_best = 0;
  heap_hdr.estimates.head = 0;

  best = 0;

  while (!(VPID_ISNULL (&prv_vpid)))
    {
      vpid = prv_vpid;
      pgptr = heap_scan_pb_lock_and_fetch (thread_p, &vpid, OLD_PAGE, X_LOCK,
					   NULL);
      if (pgptr == NULL)
	{
	  goto exit_on_error;
	}

      if (heap_vpid_prev (hfid, pgptr, &prv_vpid) != NO_ERROR)
	{
	  pgbuf_unfix_and_init (thread_p, pgptr);

	  goto exit_on_error;
	}

      /*
       * Are there any objects in this page ?
       * Compare against > 1 since every heap page contains a header record
       * (heap header or chain).
       */

      if (spage_number_of_records (pgptr) > 1
	  || (vpid.pageid == hfid->hpgid && vpid.volid == hfid->vfid.volid))
	{
	  if (spage_reclaim (thread_p, pgptr) == true)
	    {
	      addr.pgptr = pgptr;
	      /*
	       * If this function is called correctly (see the notes in the
	       * header comment about the preconditions) we can skip the
	       * logging of spage_reclaim (). Logging for REDO would add many
	       * log records for any compactdb operation and would only
	       * benefit the infrequent scenario of compactdb operations that
	       * crash right at the end. UNDO operations are not absolutely
	       * required because the deleted OIDs should be unreferenced
	       * anyway; there should be no harm in reusing them. Basically,
	       * since the call to spage_reclaim () should leave the database
	       * logically unmodified, neither REDO nor UNDO are required.
	       */
	      log_skip_logging (thread_p, &addr);
	      pgbuf_set_dirty (thread_p, pgptr, DONT_FREE);
	    }
	}

      /*
       * Throw away the page if it doesn't contain any object. The header of
       * the heap cannot be thrown.
       */

      if (!(vpid.pageid == hfid->hpgid && vpid.volid == hfid->vfid.volid)
	  && spage_number_of_records (pgptr) <= 1)
	{
	  /*
	   * This page can be thrown away
	   */
	  pgbuf_unfix_and_init (thread_p, pgptr);
	  if (heap_vpid_remove (thread_p, hfid, &heap_hdr, &vpid) == NULL)
	    {
	      goto exit_on_error;
	    }
	}
      else
	{
	  spage_collect_statistics (pgptr, &npages, &nrecords, &rec_length);
	  heap_hdr.estimates.num_pages += npages;
	  heap_hdr.estimates.num_recs += nrecords;
	  heap_hdr.estimates.recs_sumlen += rec_length;

	  free_space = spage_get_free_space_without_saving (thread_p, pgptr,
							    &need_update);

	  if (free_space > HEAP_DROP_FREE_SPACE
	      && best < HEAP_NUM_BEST_SPACESTATS)
	    {
	      heap_hdr.estimates.best[best].vpid = vpid;
	      heap_hdr.estimates.best[best].freespace = free_space;
	      best++;
	    }
	  else
	    {
	      heap_hdr.estimates.num_other_high_best++;
	    }

	  if (prm_get_integer_value (PRM_ID_HF_MAX_BESTSPACE_ENTRIES) > 0)
	    {
	      int rc;

	      assert (is_new_file == FILE_OLD_FILE);
	      rc = pthread_mutex_lock (&heap_Bestspace->bestspace_mutex);

	      (void) heap_stats_add_bestspace (thread_p, hfid, &vpid,
					       free_space);

	      assert (mht_count (heap_Bestspace->vpid_ht) ==
		      mht_count (heap_Bestspace->hfid_ht));

	      pthread_mutex_unlock (&heap_Bestspace->bestspace_mutex);
	    }

	  pgbuf_unfix_and_init (thread_p, pgptr);
	}
    }

  heap_hdr.estimates.num_high_best = best;
  /*
   * Set the rest of the statistics to NULL
   */
  for (; best < HEAP_NUM_BEST_SPACESTATS; best++)
    {
      VPID_SET_NULL (&heap_hdr.estimates.best[best].vpid);
      heap_hdr.estimates.best[best].freespace = 0;
    }

  /* Log the desired changes.. and then change the header
   * We need to log the header changes in order to always benefit from the
   * updated statistics and in order to avoid referencing deleted pages in the
   * statistics.
   */
  addr.pgptr = hdr_pgptr;
  addr.offset = HEAP_HEADER_AND_CHAIN_SLOTID;
  log_append_undoredo_data (thread_p, RVHF_STATS, &addr,
			    sizeof (HEAP_HDR_STATS), sizeof (HEAP_HDR_STATS),
			    &initial_heap_hdr, hdr_recdes.data);

  /* Now update the statistics */
  if (spage_update (thread_p, hdr_pgptr, HEAP_HEADER_AND_CHAIN_SLOTID,
		    &hdr_recdes) != SP_SUCCESS)
    {
      goto exit_on_error;
    }

  pgbuf_set_dirty (thread_p, hdr_pgptr, FREE);
  hdr_pgptr = NULL;

  return ret;

exit_on_error:

  if (hdr_pgptr)
    {
      pgbuf_unfix_and_init (thread_p, hdr_pgptr);
    }

  return (ret == NO_ERROR
	  && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
}

/*
 * heap_ovf_find_vfid () - Find overflow file identifier
 *   return: ovf_vfid or NULL
 *   hfid(in): Object heap file identifier
 *   ovf_vfid(in/out): Overflow file identifier.
 *   docreate(in): true/false. If true and the overflow file does not
 *                 exist, it is created.
 *
 * Note: Find overflow file identifier. If the overflow file does not
 * exist, it may be created depending of the value of argument create.
 */
static VFID *
heap_ovf_find_vfid (THREAD_ENTRY * thread_p, const HFID * hfid,
		    VFID * ovf_vfid, bool docreate)
{
  HEAP_HDR_STATS *heap_hdr;	/* Header of heap structure    */
  LOG_DATA_ADDR addr_hdr;	/* Address of logging data     */
  VPID vpid;			/* Page-volume identifier      */
  RECDES hdr_recdes;		/* Header record descriptor    */
  int mode;

  addr_hdr.vfid = &hfid->vfid;
  addr_hdr.offset = HEAP_HEADER_AND_CHAIN_SLOTID;

  /* Read the header page */
  vpid.volid = hfid->vfid.volid;
  vpid.pageid = hfid->hpgid;

  mode = (docreate == true ? PGBUF_LATCH_WRITE : PGBUF_LATCH_READ);
  addr_hdr.pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, mode,
			      PGBUF_UNCONDITIONAL_LATCH);
  if (addr_hdr.pgptr == NULL)
    {
      /* something went wrong, return */
      return NULL;
    }

  /* Peek the header record */

  if (spage_get_record (addr_hdr.pgptr, HEAP_HEADER_AND_CHAIN_SLOTID,
			&hdr_recdes, PEEK) != S_SUCCESS)
    {
      pgbuf_unfix_and_init (thread_p, addr_hdr.pgptr);
      return NULL;
    }

  heap_hdr = (HEAP_HDR_STATS *) hdr_recdes.data;
  if (VFID_ISNULL (&heap_hdr->ovf_vfid))
    {
      if (docreate == true)
	{
	  FILE_OVF_HEAP_DES hfdes_ovf;
	  /*
	   * Create the overflow file. Try to create the overflow file in the
	   * same volume where the heap was defined
	   */

	  /*
	   * START A TOP SYSTEM OPERATION
	   */

	  if (log_start_system_op (thread_p) == NULL)
	    {
	      pgbuf_unfix_and_init (thread_p, addr_hdr.pgptr);
	      return NULL;
	    }


	  ovf_vfid->volid = hfid->vfid.volid;
	  /*
	   * At least three pages since a multipage object will take at least
	   * two pages
	   */


	  /* Initialize description of overflow heap file */
	  HFID_COPY (&hfdes_ovf.hfid, hfid);

	  if (file_create (thread_p, ovf_vfid, 3, FILE_MULTIPAGE_OBJECT_HEAP,
			   &hfdes_ovf, NULL, 0) != NULL)
	    {
	      /* Log undo, then redo */
	      log_append_undo_data (thread_p, RVHF_STATS, &addr_hdr,
				    sizeof (*heap_hdr), heap_hdr);
	      VFID_COPY (&heap_hdr->ovf_vfid, ovf_vfid);
	      log_append_redo_data (thread_p, RVHF_STATS, &addr_hdr,
				    sizeof (*heap_hdr), heap_hdr);
	      pgbuf_set_dirty (thread_p, addr_hdr.pgptr, DONT_FREE);

	      if (file_is_new_file (thread_p, &(hfid->vfid)) == FILE_NEW_FILE)
		{
		  log_end_system_op (thread_p,
				     LOG_RESULT_TOPOP_ATTACH_TO_OUTER);
		}
	      else
		{
		  log_end_system_op (thread_p, LOG_RESULT_TOPOP_COMMIT);
		  (void) file_new_declare_as_old (thread_p, ovf_vfid);
		}
	    }
	  else
	    {
	      log_end_system_op (thread_p, LOG_RESULT_TOPOP_ABORT);
	      ovf_vfid = NULL;
	    }
	}
      else
	{
	  ovf_vfid = NULL;
	}
    }
  else
    {
      VFID_COPY (ovf_vfid, &heap_hdr->ovf_vfid);
    }

  pgbuf_unfix_and_init (thread_p, addr_hdr.pgptr);

  return ovf_vfid;
}

/*
 * heap_ovf_insert () - Insert the content of a multipage object in overflow
 *   return: OID *(ovf_oid on success or NULL on failure)
 *   hfid(in): Object heap file identifier
 *   ovf_oid(in/out): Overflow address
 *   recdes(in): Record descriptor
 *
 * Note: Insert the content of a multipage object in overflow.
 */
static OID *
heap_ovf_insert (THREAD_ENTRY * thread_p, const HFID * hfid, OID * ovf_oid,
		 RECDES * recdes)
{
  VFID ovf_vfid;
  VPID ovf_vpid;		/* Address of overflow insertion */

  if (heap_ovf_find_vfid (thread_p, hfid, &ovf_vfid, true) == NULL
      || overflow_insert (thread_p, &ovf_vfid, &ovf_vpid, recdes,
			  NULL) == NULL)
    {
      return NULL;
    }

  ovf_oid->pageid = ovf_vpid.pageid;
  ovf_oid->volid = ovf_vpid.volid;
  ovf_oid->slotid = NULL_SLOTID;	/* Irrelevant */

  return ovf_oid;
}

/*
 * heap_ovf_update () - Update the content of a multipage object
 *   return: OID *(ovf_oid on success or NULL on failure)
 *   hfid(in): Object heap file identifier
 *   ovf_oid(in): Overflow address
 *   recdes(in): Record descriptor
 *
 * Note: Update the content of a multipage object.
 */
static const OID *
heap_ovf_update (THREAD_ENTRY * thread_p, const HFID * hfid,
		 const OID * ovf_oid, RECDES * recdes)
{
  VFID ovf_vfid;
  VPID ovf_vpid;

  if (heap_ovf_find_vfid (thread_p, hfid, &ovf_vfid, false) == NULL)
    {
      return NULL;
    }

  ovf_vpid.pageid = ovf_oid->pageid;
  ovf_vpid.volid = ovf_oid->volid;

  if (overflow_update (thread_p, &ovf_vfid, &ovf_vpid, recdes) == NULL)
    {
      return NULL;
    }
  else
    {
      return ovf_oid;
    }
}

/*
 * heap_ovf_delete () - Delete the content of a multipage object
 *   return: OID *(ovf_oid on success or NULL on failure)
 *   hfid(in): Object heap file identifier
 *   ovf_oid(in): Overflow address
 *
 * Note: Delete the content of a multipage object.
 */
static const OID *
heap_ovf_delete (THREAD_ENTRY * thread_p, const HFID * hfid,
		 const OID * ovf_oid)
{
  VFID ovf_vfid;
  VPID ovf_vpid;

  if (heap_ovf_find_vfid (thread_p, hfid, &ovf_vfid, false) == NULL)
    {
      return NULL;
    }

  ovf_vpid.pageid = ovf_oid->pageid;
  ovf_vpid.volid = ovf_oid->volid;

  if (overflow_delete (thread_p, &ovf_vfid, &ovf_vpid) == NULL)
    {
      return NULL;
    }
  else
    {
      return ovf_oid;
    }

}

/*
 * heap_ovf_flush () - Flush all overflow dirty pages where the object resides
 *   return: NO_ERROR
 *   ovf_oid(in): Overflow address
 *
 * Note: Flush all overflow dirty pages where the object resides.
 */
static int
heap_ovf_flush (THREAD_ENTRY * thread_p, const OID * ovf_oid)
{
  VPID ovf_vpid;

  ovf_vpid.pageid = ovf_oid->pageid;
  ovf_vpid.volid = ovf_oid->volid;
  overflow_flush (thread_p, &ovf_vpid);

  return NO_ERROR;
}

/*
 * heap_ovf_get_length () - Find length of overflow object
 *   return: length
 *   ovf_oid(in): Overflow address
 *
 * Note: The length of the content of a multipage object associated
 * with the given overflow address is returned. In the case of
 * any error, -1 is returned.
 */
static int
heap_ovf_get_length (THREAD_ENTRY * thread_p, const OID * ovf_oid)
{
  VPID ovf_vpid;

  ovf_vpid.pageid = ovf_oid->pageid;
  ovf_vpid.volid = ovf_oid->volid;

  return overflow_get_length (thread_p, &ovf_vpid);
}

/*
 * heap_ovf_get () - get/retrieve the content of a multipage object from overflow
 *   return: SCAN_CODE
 *           (Either of S_SUCCESS, S_DOESNT_FIT, S_END)
 *   ovf_oid(in): Overflow address
 *   recdes(in): Record descriptor
 *   chn(in):
 *
 * Note: The content of a multipage object associated with the given
 * overflow address(oid) is placed into the area pointed to by
 * the record descriptor. If the content of the object does not
 * fit in such an area (i.e., recdes->area_size), an error is
 * returned and a hint of its length is returned as a negative
 * value in recdes->length. The length of the retrieved object is
 * set in the the record descriptor (i.e., recdes->length).
 */
static SCAN_CODE
heap_ovf_get (THREAD_ENTRY * thread_p, const OID * ovf_oid, RECDES * recdes,
	      int chn)
{
  VPID ovf_vpid;
  int rest_length;
  SCAN_CODE scan;

  ovf_vpid.pageid = ovf_oid->pageid;
  ovf_vpid.volid = ovf_oid->volid;

  if (chn != NULL_CHN)
    {
      /*
       * This assumes that most of the time, we have the right cache coherency
       * number and that it is expensive to copy the overflow object to be
       * thrown most of the time. Thus, it is OK to do some extra page look up
       * when failures (it should be OK since the overflow page should be
       * already in the page buffer pool.
       */
      scan = overflow_get_nbytes (thread_p, &ovf_vpid, recdes, 0,
				  OR_HEADER_SIZE, &rest_length);
      if (scan == S_SUCCESS && chn == or_chn (recdes))
	{
	  return S_SUCCESS_CHN_UPTODATE;
	}
    }
  scan = overflow_get (thread_p, &ovf_vpid, recdes);

  return scan;
}

/*
 * heap_ovf_get_capacity () - Find space consumed oveflow object
 *   return: NO_ERROR
 *   ovf_oid(in): Overflow address
 *   ovf_len(out): Length of overflow object
 *   ovf_num_pages(out): Total number of overflow pages
 *   ovf_overhead(out): System overhead for overflow record
 *   ovf_free_space(out): Free space for exapnsion of the overflow rec
 *
 * Note: Find the current storage facts/capacity of given overflow rec
 */
static int
heap_ovf_get_capacity (THREAD_ENTRY * thread_p, const OID * ovf_oid,
		       int *ovf_len, int *ovf_num_pages, int *ovf_overhead,
		       int *ovf_free_space)
{
  VPID ovf_vpid;

  ovf_vpid.pageid = ovf_oid->pageid;
  ovf_vpid.volid = ovf_oid->volid;

  return overflow_get_capacity (thread_p, &ovf_vpid, ovf_len, ovf_num_pages,
				ovf_overhead, ovf_free_space);
}

/*
 * heap_scancache_start_internal () - Start caching information for a heap scan
 *   return: NO_ERROR
 *   scan_cache(in/out): Scan cache
 *   hfid(in): Heap file identifier of the scan cache or NULL
 *             If NULL is given heap_get is the only function that can
 *             be used with the scan cache.
 *   class_oid(in): Class identifier of scan cache
 *                  For any class, NULL or NULL_OID can be given
 *   cache_last_fix_page(in): Wheater or not to cache the last fetched page
 *                            between scan objects ?
 *   is_queryscan(in):
 *   is_indexscan(in):
 *   lock_hint(in):
 *
 */
static int
heap_scancache_start_internal (THREAD_ENTRY * thread_p,
			       HEAP_SCANCACHE * scan_cache, const HFID * hfid,
			       const OID * class_oid, int cache_last_fix_page,
			       int is_queryscan, int is_indexscan,
			       int lock_hint)
{
  LOCK class_lock = NULL_LOCK;
  int ret = NO_ERROR;

  scan_cache->collect_maxbest = 0;
  scan_cache->collect_best = NULL;

  scan_cache->scanid_bit = -1;

  if (class_oid != NULL)
    {
      /*
       * Scanning the instances of a specific class
       */
      scan_cache->class_oid = *class_oid;

      if (is_queryscan == true)
	{
	  /*
	   * Acquire a lock for the heap scan so that the class is not updated
	   * during the scan of the heap. This can happen in transaction isolation
	   * levels that release the locks of the class when the class is read.
	   */
	  if (lock_scan (thread_p, class_oid, is_indexscan, lock_hint,
			 &class_lock, &scan_cache->scanid_bit) != LK_GRANTED)
	    {
	      goto exit_on_error;
	    }
	}
    }
  else
    {
      /*
       * Scanning the instances of any class in the heap
       */
      OID_SET_NULL (&scan_cache->class_oid);
    }


  if (hfid == NULL)
    {
      HFID_SET_NULL (&scan_cache->hfid);
      scan_cache->hfid.vfid.volid = NULL_VOLID;
      scan_cache->file_type = FILE_UNKNOWN_TYPE;
      scan_cache->is_new_file = FILE_ERROR;
    }
  else
    {
#if defined(CUBRID_DEBUG)
      DISK_ISVALID valid_file;

      valid_file = file_isvalid (&hfid->vfid);
      if (valid_file != DISK_VALID)
	{
	  if (class_oid != NULL && is_queryscan == true)
	    {
	      lock_unlock_scan (class_oid, scancache->scanid_bit);
	    }
	  if (valid_file != DISK_ERROR)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_UNKNOWN_HEAP,
		      3, fileio_get_volume_label (hfid->vfid.volid, PEEK),
		      hfid->vfid.fileid, hfid->hpgid);
	    }
	  goto exit_on_error;
	}
#endif
      scan_cache->hfid.vfid.volid = hfid->vfid.volid;
      scan_cache->hfid.vfid.fileid = hfid->vfid.fileid;
      scan_cache->hfid.hpgid = hfid->hpgid;
      scan_cache->file_type = file_get_type (thread_p, &hfid->vfid);
      if (scan_cache->file_type == FILE_UNKNOWN_TYPE)
	{
	  goto exit_on_error;
	}
      scan_cache->is_new_file = file_is_new_file (thread_p, &hfid->vfid);
      if (scan_cache->is_new_file == FILE_ERROR)
	{
	  goto exit_on_error;
	}
    }

  if (hfid != NULL && is_indexscan == false)
    {
      ret = heap_get_best_estimates_stats (thread_p, hfid,
					   &scan_cache->known_nbest,
					   &scan_cache->known_nother_best,
					   &scan_cache->known_nrecs);
      if (ret != NO_ERROR)
	{
	  goto exit_on_error;
	}

      scan_cache->unfill_space = 0;
    }
  else
    {
      scan_cache->unfill_space = 0;
      scan_cache->known_nbest = 0;
      scan_cache->known_nother_best = 0;
      scan_cache->known_nrecs = 0;
    }

  scan_cache->page_latch = S_LOCK;

  scan_cache->cache_last_fix_page = cache_last_fix_page;
  scan_cache->pgptr = NULL;
  scan_cache->area = NULL;
  scan_cache->area_size = -1;
  scan_cache->collect_nxvpid.volid = scan_cache->hfid.vfid.volid;
  scan_cache->collect_nxvpid.pageid = scan_cache->hfid.hpgid;
  scan_cache->collect_npages = 0;
  scan_cache->collect_nrecs = 0;
  scan_cache->collect_recs_sumlen = 0;
  scan_cache->collect_nbest = 0;
  scan_cache->collect_nother_best = 0;
  scan_cache->num_btids = 0;
  scan_cache->index_stat_info = NULL;

  if (is_queryscan == true)
    {
      assert (scan_cache->known_nbest >= 0
	      && scan_cache->known_nbest <= HEAP_NUM_BEST_SPACESTATS);
      scan_cache->collect_maxbest = (HEAP_NUM_BEST_SPACESTATS
				     - scan_cache->known_nbest);
      assert (scan_cache->collect_maxbest >= 0
	      && scan_cache->collect_maxbest <= HEAP_NUM_BEST_SPACESTATS);
      if (scan_cache->collect_maxbest < 0)
	{
	  scan_cache->collect_maxbest = 0;
	}
    }
  else
    {
      /*
       * We are using the scan to insert, update, or delete new instances.
       * Malloc an area to keep space for statistics.
       */
      scan_cache->collect_maxbest = HEAP_NUM_BEST_SPACESTATS;
    }

  if (scan_cache->collect_maxbest > 0)
    {
      scan_cache->collect_best =
	(HEAP_BESTSPACE *) db_private_alloc (thread_p,
					     sizeof
					     (*scan_cache->collect_best) *
					     scan_cache->collect_maxbest);
      if (scan_cache->collect_best == NULL)
	{
	  scan_cache->collect_maxbest = 0;
	}
    }
  else
    {
      scan_cache->collect_best = NULL;
    }

  scan_cache->debug_initpattern = HEAP_DEBUG_SCANCACHE_INITPATTERN;

  return ret;

exit_on_error:

  HFID_SET_NULL (&scan_cache->hfid);
  scan_cache->hfid.vfid.volid = NULL_VOLID;
  OID_SET_NULL (&scan_cache->class_oid);
  scan_cache->unfill_space = 0;
  scan_cache->page_latch = NULL_LOCK;
  scan_cache->cache_last_fix_page = false;
  scan_cache->pgptr = NULL;
  scan_cache->area = NULL;
  scan_cache->area_size = 0;
  scan_cache->known_nrecs = 0;
  scan_cache->known_nbest = 0;
  scan_cache->known_nother_best = 0;
  VPID_SET_NULL (&scan_cache->collect_nxvpid);
  scan_cache->collect_npages = 0;
  scan_cache->collect_nrecs = 0;
  scan_cache->collect_recs_sumlen = 0;
  scan_cache->collect_nbest = 0;
  scan_cache->collect_nother_best = 0;
  scan_cache->num_btids = 0;
  scan_cache->index_stat_info = NULL;
  db_private_free_and_init (thread_p, scan_cache->collect_best);
  scan_cache->collect_maxbest = 0;
  scan_cache->file_type = FILE_UNKNOWN_TYPE;
  scan_cache->is_new_file = FILE_ERROR;
  scan_cache->debug_initpattern = 0;

  return (ret == NO_ERROR
	  && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
}

/*
 * heap_scancache_start () - Start caching information for a heap scan
 *   return: NO_ERROR
 *   scan_cache(in/out): Scan cache
 *   hfid(in): Heap file identifier of the scan cache or NULL
 *             If NULL is given heap_get is the only function that can
 *             be used with the scan cache.
 *   class_oid(in): Class identifier of scan cache
 *                  For any class, NULL or NULL_OID can be given
 *   cache_last_fix_page(in): Wheater or not to cache the last fetched page
 *                            between scan objects ?
 *   is_indexscan(in):
 *   lock_hint(in):
 *
 */
int
heap_scancache_start (THREAD_ENTRY * thread_p, HEAP_SCANCACHE * scan_cache,
		      const HFID * hfid, const OID * class_oid,
		      int cache_last_fix_page, int is_indexscan,
		      int lock_hint)
{
  return heap_scancache_start_internal (thread_p, scan_cache, hfid, class_oid,
					cache_last_fix_page, true,
					is_indexscan, lock_hint);
}

/*
 * heap_scancache_start_modify () - Start caching information for heap
 *                                modifications
 *   return: NO_ERROR
 *   scan_cache(in/out): Scan cache
 *   hfid(in): Heap file identifier of the scan cache or NULL
 *             If NULL is given heap_get is the only function that can
 *             be used with the scan cache.
 *   class_oid(in): Class identifier of scan cache
 *                  For any class, NULL or NULL_OID can be given
 *   op_type(in):
 *
 * Note: A scancache structure is started for heap modifications.
 * The scan_cache structure is used to modify objects of the heap
 * with heap_insert, heap_update, and heap_delete. The scan structure
 * is used to cache information about the latest used page which
 * can be used by the following function to guess where to insert
 * objects, or other updates and deletes on the same page.
 * Good when we are updating things in a sequential way.
 *
 * The heap manager automatically resets the scan_cache structure
 * when it is used with a different heap. That is, the scan_cache
 * is reset with the heap and class of the insertion, update, and
 * delete. Therefore, you could pass NULLs to hfid, and class_oid
 * to this function, but that it is not recommended.
 */
int
heap_scancache_start_modify (THREAD_ENTRY * thread_p,
			     HEAP_SCANCACHE * scan_cache, const HFID * hfid,
			     const OID * class_oid, int op_type)
{
  OR_CLASSREP *classrepr = NULL;
  int classrepr_cacheindex = -1;
  int malloc_size, i;
  int ret = NO_ERROR;

  if (heap_scancache_start_internal (thread_p, scan_cache, hfid, NULL, false,
				     false, false, LOCKHINT_NONE) != NO_ERROR)
    {
      goto exit_on_error;
    }

  if (class_oid != NULL)
    {
      ret = heap_scancache_reset_modify (thread_p, scan_cache, hfid,
					 class_oid);
      if (ret != NO_ERROR)
	{
	  goto exit_on_error;
	}
    }
  else
    {
      scan_cache->page_latch = X_LOCK;
    }

  if (BTREE_IS_MULTI_ROW_OP (op_type) && class_oid != NULL)
    {
      /* get class representation to find the total number of indexes */
      classrepr = heap_classrepr_get (thread_p, (OID *) class_oid, NULL, 0,
				      &classrepr_cacheindex, true);
      if (classrepr == NULL)
	{
	  if (scan_cache->collect_best != NULL)
	    {
	      db_private_free_and_init (thread_p, scan_cache->collect_best);
	    }
	  goto exit_on_error;
	}
      scan_cache->num_btids = classrepr->n_indexes;

      if (scan_cache->num_btids > 0)
	{
	  /* allocate local btree statistical information structure */
	  malloc_size = sizeof (BTREE_UNIQUE_STATS) * scan_cache->num_btids;

	  if (scan_cache->index_stat_info != NULL)
	    {
	      db_private_free (thread_p, scan_cache->index_stat_info);
	    }

	  scan_cache->index_stat_info =
	    (BTREE_UNIQUE_STATS *) db_private_alloc (thread_p, malloc_size);
	  if (scan_cache->index_stat_info == NULL)
	    {
	      ret = ER_OUT_OF_VIRTUAL_MEMORY;
	      if (scan_cache->collect_best != NULL)
		{
		  db_private_free_and_init (thread_p,
					    scan_cache->collect_best);
		}
	      (void) heap_classrepr_free (classrepr, &classrepr_cacheindex);
	      goto exit_on_error;
	    }
	  /* initialize the structure */
	  for (i = 0; i < scan_cache->num_btids; i++)
	    {
	      BTID_COPY (&(scan_cache->index_stat_info[i].btid),
			 &(classrepr->indexes[i].btid));
	      scan_cache->index_stat_info[i].num_nulls = 0;
	      scan_cache->index_stat_info[i].num_keys = 0;
	      scan_cache->index_stat_info[i].num_oids = 0;
	    }
	}

      /* free class representation */
      ret = heap_classrepr_free (classrepr, &classrepr_cacheindex);
      if (ret != NO_ERROR)
	{
	  goto exit_on_error;
	}
    }

  /* In case of SINGLE_ROW_INSERT, SINGLE_ROW_UPDATE,
   * SINGLE_ROW_DELETE, or SINGLE_ROW_MODIFY,
   * the 'num_btids' and 'index_stat_info' of scan cache structure
   * have to be set as 0 and NULL, respectively.
   */

  return ret;

exit_on_error:

  return (ret == NO_ERROR
	  && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
}

/*
 * heap_scancache_force_modify () -
 *   return: NO_ERROR
 *   scan_cache(in):
 */
static int
heap_scancache_force_modify (THREAD_ENTRY * thread_p,
			     HEAP_SCANCACHE * scan_cache)
{
  HEAP_HDR_STATS *heap_hdr;	/* Header of heap structure    */
  PAGE_PTR hdr_pgptr = NULL;	/* Page pointer to header page */
  VPID vpid;			/* Page-volume identifier      */
  RECDES recdes;		/* Header record descriptor    */
  LOG_DATA_ADDR addr;		/* Address of logging data     */

  if (scan_cache == NULL
      || scan_cache->debug_initpattern != HEAP_DEBUG_SCANCACHE_INITPATTERN)
    {
      return NO_ERROR;
    }

  /* Free fetched page */
  if (scan_cache->pgptr != NULL)
    {
      pgbuf_unfix_and_init (thread_p, scan_cache->pgptr);
    }

  if (scan_cache->collect_nbest > 0 && scan_cache->collect_recs_sumlen > 0.0
      && (file_is_new_file (thread_p,
			    &(scan_cache->hfid.vfid)) != FILE_NEW_FILE
	  || log_is_tran_in_system_op (thread_p) != true))
    {
      /* Retrieve the header of heap */
      vpid.volid = scan_cache->hfid.vfid.volid;
      vpid.pageid = scan_cache->hfid.hpgid;

      /*
       * We do not want to wait for the following operation. So, if we cannot
       * lock the page return.
       */

      hdr_pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			     PGBUF_CONDITIONAL_LATCH);
      if (hdr_pgptr == NULL)
	{
	  /* Page is busy or other type of error */
	  return ER_FAILED;
	}

      if (spage_get_record (hdr_pgptr, HEAP_HEADER_AND_CHAIN_SLOTID, &recdes,
			    PEEK) == S_SUCCESS)
	{
	  heap_hdr = (HEAP_HDR_STATS *) recdes.data;
	  if (heap_stats_copy_cache_to_hdr (thread_p, heap_hdr,
					    scan_cache) == NO_ERROR)
	    {
	      addr.vfid = &scan_cache->hfid.vfid;
	      addr.pgptr = hdr_pgptr;
	      addr.offset = HEAP_HEADER_AND_CHAIN_SLOTID;
	      log_skip_logging (thread_p, &addr);
	      pgbuf_set_dirty (thread_p, hdr_pgptr, FREE);
	      hdr_pgptr = NULL;
	    }
	  else
	    {
	      pgbuf_unfix_and_init (thread_p, hdr_pgptr);
	    }
	}
      else
	{
	  pgbuf_unfix_and_init (thread_p, hdr_pgptr);
	}
    }

  return NO_ERROR;
}

/*
 * heap_scancache_reset_modify () - Reset the current caching information
 *   return: NO_ERROR
 *   scan_cache(in/out): Scan cache
 *   hfid(in): Heap file identifier of the scan cache
 *   class_oid(in): Class identifier of scan cache
 *
 * Note: Any page that has been cached under the current scan cache is
 * freed and the scancache structure is reinitialized with the
 * new information.
 */
static int
heap_scancache_reset_modify (THREAD_ENTRY * thread_p,
			     HEAP_SCANCACHE * scan_cache, const HFID * hfid,
			     const OID * class_oid)
{
  int ret;

  ret = heap_scancache_force_modify (thread_p, scan_cache);
  if (ret != NO_ERROR)
    {
      return ret;
    }

  if (class_oid != NULL)
    {
      scan_cache->class_oid = *class_oid;
    }
  else
    {
      OID_SET_NULL (&scan_cache->class_oid);
    }

  if (!HFID_EQ (&scan_cache->hfid, hfid))
    {
      scan_cache->hfid.vfid.volid = hfid->vfid.volid;
      scan_cache->hfid.vfid.fileid = hfid->vfid.fileid;
      scan_cache->hfid.hpgid = hfid->hpgid;

      scan_cache->file_type = file_get_type (thread_p, &hfid->vfid);
      if (scan_cache->file_type == FILE_UNKNOWN_TYPE)
	{
	  return ER_FAILED;
	}
      scan_cache->is_new_file = file_is_new_file (thread_p, &hfid->vfid);
      if (scan_cache->is_new_file == FILE_ERROR)
	{
	  return ER_FAILED;
	}

      ret = heap_get_best_estimates_stats (thread_p, hfid,
					   &scan_cache->known_nbest,
					   &scan_cache->known_nother_best,
					   &scan_cache->known_nrecs);
      if (ret != NO_ERROR)
	{
	  return ret;
	}

      scan_cache->unfill_space = 0;
      scan_cache->collect_nbest = 0;
      scan_cache->collect_nother_best = 0;
      scan_cache->collect_nrecs = 0;
      scan_cache->collect_recs_sumlen = 0;
    }

  scan_cache->page_latch = X_LOCK;

  return ret;
}

/*
 * heap_scancache_quick_start () - Start caching information for a heap scan
 *   return: NO_ERROR
 *   scan_cache(in/out): Scan cache
 *
 * Note: This is a quick way to initialize a scancahe structure. It
 * should be used only when we would like to peek only one object
 * (heap_get). This function will cache the last fetched page by default.
 *
 *  This function was created to avoid some of the overhead
 *  associated with scancahe(e.g., find best pages, lock the heap)
 *  since we are not really scanning the heap.
 *
 *  For other needs/uses, please refer to heap_scancache_start ().
 *
 * Note: Using many scancaches with the cached_fix page option at the
 * same time should be avoided since page buffers are fixed and
 * locked for future references and there is a limit of buffers
 * in the page buffer pool. This is analogous to fetching many
 * pages at the same time. The page buffer pool is expanded when
 * needed, however, developers must pay special attention to
 * avoid this situation.
 */
int
heap_scancache_quick_start (HEAP_SCANCACHE * scan_cache)
{
  heap_scancache_quick_start_internal (scan_cache);

  scan_cache->page_latch = S_LOCK;

  return NO_ERROR;
}

/*
 * heap_scancache_quick_start_modify () - Start caching information
 *                                      for a heap modifications
 *   return: NO_ERROR
 *   scan_cache(in/out): Scan cache
 */
int
heap_scancache_quick_start_modify (HEAP_SCANCACHE * scan_cache)
{
  heap_scancache_quick_start_internal (scan_cache);

  scan_cache->page_latch = X_LOCK;

  return NO_ERROR;
}

/*
 * heap_scancache_quick_start_internal () -
 *
 *   return: NO_ERROR
 *   scan_cache(in/out): Scan cache
 */
static int
heap_scancache_quick_start_internal (HEAP_SCANCACHE * scan_cache)
{
  HFID_SET_NULL (&scan_cache->hfid);
  scan_cache->hfid.vfid.volid = NULL_VOLID;
  OID_SET_NULL (&scan_cache->class_oid);
  scan_cache->unfill_space = 0;
  scan_cache->page_latch = S_LOCK;
  scan_cache->cache_last_fix_page = true;
  scan_cache->pgptr = NULL;
  scan_cache->area = NULL;
  scan_cache->area_size = 0;
  scan_cache->known_nrecs = 0;
  scan_cache->known_nbest = 0;
  scan_cache->known_nother_best = 0;
  VPID_SET_NULL (&scan_cache->collect_nxvpid);
  scan_cache->collect_npages = 0;
  scan_cache->collect_nrecs = 0;
  scan_cache->collect_recs_sumlen = 0;
  scan_cache->collect_nbest = 0;
  scan_cache->collect_maxbest = 0;
  scan_cache->collect_nother_best = 0;
  scan_cache->collect_best = NULL;
  scan_cache->num_btids = 0;
  scan_cache->index_stat_info = NULL;
  scan_cache->file_type = FILE_UNKNOWN_TYPE;
  scan_cache->is_new_file = FILE_ERROR;
  scan_cache->debug_initpattern = HEAP_DEBUG_SCANCACHE_INITPATTERN;

  return NO_ERROR;
}

/*
 * heap_scancache_quick_end () - Stop caching information for a heap scan
 *   return: NO_ERROR
 *   scan_cache(in/out): Scan cache
 *
 * Note: Any fixed heap page on the given scan is freed and any memory
 * allocated by this scan is also freed. The scan_cache structure
 * is undefined.  This function does not update any space statistics.
 */
static int
heap_scancache_quick_end (THREAD_ENTRY * thread_p,
			  HEAP_SCANCACHE * scan_cache)
{
  int ret = NO_ERROR;

  if (scan_cache->debug_initpattern != HEAP_DEBUG_SCANCACHE_INITPATTERN)
    {
      er_log_debug (ARG_FILE_LINE, "heap_scancache_quick_end: Your scancache"
		    " is not initialized");
      ret = ER_GENERIC_ERROR;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ret, 0);
    }
  else
    {
      if (scan_cache->index_stat_info != NULL)
	{
	  /* deallocate memory space allocated for index stat info. */
	  db_private_free_and_init (thread_p, scan_cache->index_stat_info);
	  scan_cache->num_btids = 0;
	}

      if (scan_cache->collect_maxbest > 0)
	{
	  db_private_free_and_init (thread_p, scan_cache->collect_best);
	}

      if (scan_cache->cache_last_fix_page == true)
	{
	  /* Free fetched page */
	  if (scan_cache->pgptr != NULL)
	    {
	      pgbuf_unfix_and_init (thread_p, scan_cache->pgptr);
	    }
	}

      /* Free memory */
      if (scan_cache->area)
	{
	  db_private_free_and_init (thread_p, scan_cache->area);
	}
    }

  HFID_SET_NULL (&scan_cache->hfid);
  scan_cache->hfid.vfid.volid = NULL_VOLID;
  OID_SET_NULL (&scan_cache->class_oid);
  scan_cache->unfill_space = 0;
  scan_cache->page_latch = NULL_LOCK;
  scan_cache->pgptr = NULL;
  scan_cache->area = NULL;
  scan_cache->area_size = 0;
  scan_cache->known_nrecs = 0;
  scan_cache->known_nbest = 0;
  scan_cache->known_nother_best = 0;
  VPID_SET_NULL (&scan_cache->collect_nxvpid);
  scan_cache->collect_npages = 0;
  scan_cache->collect_nrecs = 0;
  scan_cache->collect_recs_sumlen = 0;
  scan_cache->collect_nbest = 0;
  scan_cache->collect_maxbest = 0;
  scan_cache->collect_nother_best = 0;
  scan_cache->collect_best = NULL;
  scan_cache->file_type = FILE_UNKNOWN_TYPE;
  scan_cache->is_new_file = FILE_ERROR;
  scan_cache->debug_initpattern = 0;

  return ret;
}

/*
 * heap_scancache_end_internal () -
 *   return: NO_ERROR
 *   scan_cache(in):
 *   scan_state(in):
 */
static int
heap_scancache_end_internal (THREAD_ENTRY * thread_p,
			     HEAP_SCANCACHE * scan_cache, bool scan_state)
{
  int num_best;
  int num_other_best;
  int ret = NO_ERROR;

  if (scan_cache->debug_initpattern != HEAP_DEBUG_SCANCACHE_INITPATTERN)
    {
      er_log_debug (ARG_FILE_LINE,
		    "heap_scancache_end_internal: Your scancache"
		    " is not initialized");
      return ER_FAILED;
    }

  if (scan_cache->hfid.vfid.volid != NULL_VOLID)
    {
      if (VPID_ISNULL (&scan_cache->collect_nxvpid))
	{
	  /* The number of other high best pages is valid only
	   * if this heap file was fully scanned.
	   */
	  num_other_best =
	    scan_cache->collect_nother_best - scan_cache->known_nbest;
	}
      else
	{
	  /* If heap file was partially scanned, we do not get
	   * the number of other high best pages from scan cache.
	   */
	  num_other_best = -1;
	}

      if (scan_cache->collect_maxbest > 0)
	{
	  if (scan_cache->collect_nbest <= HEAP_NUM_BEST_SPACESTATS)
	    {
	      num_best = scan_cache->collect_nbest;
	    }
	  else
	    {
	      num_best = HEAP_NUM_BEST_SPACESTATS;
	      num_other_best += scan_cache->collect_nbest - num_best;
	    }
	}
      else
	{
	  /* There is no room for high best pages. */
	  num_best = -1;
	}

      if (scan_cache->known_nbest < num_best
	  || scan_cache->known_nother_best < num_other_best
	  || (scan_cache->known_nrecs != scan_cache->collect_nrecs
	      && scan_cache->collect_nrecs > 0))
	{
	  ret = heap_stats_update_all (thread_p, &scan_cache->hfid, num_best,
				       scan_cache->collect_best,
				       num_other_best,
				       scan_cache->collect_npages,
				       scan_cache->collect_nrecs,
				       scan_cache->collect_recs_sumlen);
	}
    }

  if (!OID_ISNULL (&scan_cache->class_oid))
    {
      lock_unlock_scan (thread_p, &scan_cache->class_oid,
			scan_cache->scanid_bit, scan_state);
      OID_SET_NULL (&scan_cache->class_oid);
    }

  ret = heap_scancache_quick_end (thread_p, scan_cache);

  return ret;
}

/*
 * heap_scancache_end () - Stop caching information for a heap scan
 *   return: NO_ERROR
 *   scan_cache(in/out): Scan cache
 *
 * Note: Any fixed heap page on the given scan is freed and any memory
 * allocated by this scan is also freed. The scan_cache structure is undefined.
 */
int
heap_scancache_end (THREAD_ENTRY * thread_p, HEAP_SCANCACHE * scan_cache)
{
  int ret;

  ret = heap_scancache_end_internal (thread_p, scan_cache, END_SCAN);

  return NO_ERROR;
}

/*
 * heap_scancache_end_when_scan_will_resume () -
 *   return:
 *   scan_cache(in):
 */
int
heap_scancache_end_when_scan_will_resume (THREAD_ENTRY * thread_p,
					  HEAP_SCANCACHE * scan_cache)
{
  int ret;

  ret = heap_scancache_end_internal (thread_p, scan_cache, CONTINUE_SCAN);

  return NO_ERROR;
}

/*
 * heap_scancache_end_modify () - End caching information for a heap
 *				  modification cache
 *   return:
 *   scan_cache(in/out): Scan cache
 *
 * Note: Any fixed heap page on the given scan is freed. The heap
 * best find space statistics for the heap are completely updated
 * with the ones stored in the scan cache.
 */
void
heap_scancache_end_modify (THREAD_ENTRY * thread_p,
			   HEAP_SCANCACHE * scan_cache)
{
  int ret;

  ret = heap_scancache_force_modify (thread_p, scan_cache);
  if (ret == NO_ERROR)
    {
      ret = heap_scancache_quick_end (thread_p, scan_cache);
    }
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * heap_hint_expected_num_objects () - Hint for the number of objects to be inserted
 *   return: NO_ERROR
 *   scan_cache(in/out): Scan cache
 *                       Must be previously initialized with heap and class
 *   nobjs(in): Number of expected new objects
 *   avg_objsize(in): Average object size of the new objects or -1 when unknown.
 *
 * Note: The heap associated with the given scancache is prepared to
 * received the known number of new instances of the class
 * associated with the scan cache by preallocating heap pages
 * to hold the instances.
 * Note: It is very important that the heap and class_oid were given
 * to the scan_cache when it was initialized.
 */
int
heap_hint_expected_num_objects (THREAD_ENTRY * thread_p,
				HEAP_SCANCACHE * scan_cache, int nobjs,
				int avg_objsize)
{
  HEAP_HDR_STATS *heap_hdr;	/* Header of heap structure       */
  PAGE_PTR hdr_pgptr = NULL;	/* Page pointer to header page    */
  PAGE_PTR new_pgptr = NULL;
  RECDES hdr_recdes;
  VPID vpid;			/* Volume and page identifiers    */
  int nobj_page;
  int npages;
  int ret = NO_ERROR;
  void *ptr;
  int nunits;

#if 1
  /* from now on, we do not allocate bulk pages */
  return NO_ERROR;
#endif

  if (nobjs <= 0 || scan_cache == NULL)
    {
      return NO_ERROR;
    }

  /*
   * Find the heap header and the last page of heap
   */
  vpid.volid = scan_cache->hfid.vfid.volid;
  vpid.pageid = scan_cache->hfid.hpgid;

  hdr_pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			 PGBUF_UNCONDITIONAL_LATCH);
  if (hdr_pgptr == NULL)
    {
      goto exit_on_error;
    }

  if (spage_get_record (hdr_pgptr, HEAP_HEADER_AND_CHAIN_SLOTID, &hdr_recdes,
			PEEK) != S_SUCCESS)
    {
      goto exit_on_error;
    }
  heap_hdr = (HEAP_HDR_STATS *) hdr_recdes.data;

  /*
   * Now guess the number of needed pages based on the number of new objects
   * and their average size.
   *
   * The given average size is used only when it is larger than the current
   * average size of records in the heap, and it is not larger than the max
   * size of an object in a page (i.e., it is not a BIG record)
   */

  if (heap_is_big_length (avg_objsize))
    {
      avg_objsize = sizeof (OID);
    }

  if (heap_hdr->estimates.num_recs > 0)
    {
      if (avg_objsize <= 0 ||
	  avg_objsize < (heap_hdr->estimates.recs_sumlen /
			 heap_hdr->estimates.num_recs))
	{
	  /*
	   * The given average record size may not be a very good estimate, at
	   * least is smaller than the current average length of objects in the
	   * heap. Let's us use the one on the heap.
	   */
	  avg_objsize = (int) (heap_hdr->estimates.recs_sumlen /
			       heap_hdr->estimates.num_recs);
	}
    }
  else
    {
      /*
       * If the heap does not have any records on it. Assume an average of 20
       * bytes when an average size it is not given.
       *
       * Note that the object length must be at least this long:
       *      OR_HEADER_SIZE + variable table size + bound bits array + values
       * Assume at least 20 bytes for everything but the header size
       */
      HEAP_CACHE_ATTRINFO attr_info;

      if (heap_attrinfo_start (thread_p, &scan_cache->class_oid, -1, NULL,
			       &attr_info) == NO_ERROR)
	{
	  int dummy;

	  npages = heap_attrinfo_get_disksize (&attr_info, &dummy);
	  if (avg_objsize < npages)
	    {
	      avg_objsize = npages;
	    }
	  heap_attrinfo_end (thread_p, &attr_info);
	}
    }

  if (avg_objsize <= OR_HEADER_SIZE + 20)
    {
      avg_objsize = OR_HEADER_SIZE + 20;
    }

  /*
   * Add any type of alignmnet that may be needed
   */
  avg_objsize = DB_ALIGN (avg_objsize, HEAP_MAX_ALIGN);

  /*
   * How many objects can fit in current best space
   */

  nunits = heap_stats_quick_num_fit_in_bestspace (heap_hdr->estimates.best,
						  HEAP_NUM_BEST_SPACESTATS,
						  avg_objsize,
						  heap_hdr->unfill_space);

  if (nunits > 0)
    {
      nobjs -= nunits;
    }

  /*
   * Guess any more objects based in number of other best pages
   */

  if (nobjs > 0 && heap_hdr->estimates.num_other_high_best > 0)
    {
      nobjs -= ((HEAP_DROP_FREE_SPACE / avg_objsize) *
		heap_hdr->estimates.num_other_high_best);
    }

  if (nobjs > 0)
    {
      /*
       * Now guess the number of needed pages guessing the number of objects that
       * can fit in one page.
       */
      nobj_page = (DB_PAGESIZE - heap_hdr->unfill_space -
		   spage_header_size () - sizeof (HEAP_CHAIN) -
		   spage_slot_size ());
      nobj_page = nobj_page / (avg_objsize + spage_slot_size ());
      if (nobj_page > 0)
	{
	  npages = CEIL_PTVDIV (nobjs, nobj_page);
	}
      else
	{
	  npages = 1;
	}
    }
  else
    {
      npages = 0;
    }

  /*
   * Allocate the needed pages if the expected objects do not fit in the
   * current heap space.
   *
   * Find the current last heap page for chaining purposes.
   */

  if (npages > 1)
    {
      /*
       * Grow the collect_best array if needed, but don't let it be too large.
       */
      if (npages > scan_cache->collect_maxbest
	  && scan_cache->collect_maxbest < 400)
	{
	  if (npages > 400)
	    {
	      nobj_page = 400;
	    }
	  else
	    {
	      nobj_page = npages;
	    }

	  ptr = (HEAP_BESTSPACE *)
	    db_private_realloc (thread_p,
				scan_cache->collect_best,
				(sizeof (*scan_cache->collect_best) *
				 nobj_page));
	  if (ptr == NULL)
	    {
	      goto exit_on_error;
	    }

	  scan_cache->collect_best = (HEAP_BESTSPACE *) ptr;
	  scan_cache->collect_maxbest = nobj_page;
	}

      if (scan_cache->collect_nbest <= 0)
	{
	  ret = heap_stats_copy_hdr_to_cache (heap_hdr, scan_cache);
	  if (ret != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	}

      new_pgptr = heap_vpid_prealloc_set (thread_p, &scan_cache->hfid,
					  hdr_pgptr, heap_hdr, npages,
					  scan_cache);
      if (new_pgptr == NULL)
	{
	  goto exit_on_error;
	}

      if (scan_cache->pgptr == NULL
	  && scan_cache->cache_last_fix_page == true)
	{
	  scan_cache->pgptr = new_pgptr;
	}
      else
	{
	  pgbuf_unfix_and_init (thread_p, new_pgptr);
	}
    }

  pgbuf_unfix_and_init (thread_p, hdr_pgptr);

  return ret;

exit_on_error:

  if (hdr_pgptr)
    {
      pgbuf_unfix_and_init (thread_p, hdr_pgptr);
    }

  return (ret == NO_ERROR
	  && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
}
#endif

/*
 * heap_get_if_diff_chn () - Get specified object of the given slotted page when
 *                       its cache coherency number is different
 *   return: SCAN_CODE
 *           (Either of S_SUCCESS,
 *                      S_SUCCESS_CHN_UPTODATE,
 *                      S_DOESNT_FIT,
 *                      S_DOESNT_EXIST)
 *   pgptr(in): Pointer to slotted page
 *   slotid(in): Slot identifier of current record.
 *   recdes(in/out): Pointer to a record descriptor. Will be modified to
 *                   describe the desired record.
 *   ispeeking(in): Indicates whether the record is going to be copied
 *                  (like a copy) or peeked (read at the buffer).
 *   chn(in): Cache coherency number or NULL_CHN
 *
 * Note: If the given CHN is the same as the chn of the specified
 * object in the slotted page, the object may not be placed in
 * the given record descriptor. If the given CHN is NULL_CHN or
 * is not given, then the following process is followed depending
 * upon if we are peeking or not:
 * When ispeeking is PEEK, the desired record is peeked onto the
 * buffer pool. The address of the record descriptor is set
 * to the portion of the buffer pool where the record is stored.
 * For more information on peeking description, see the slotted module.
 *
 * When ispeeking is DONT_PEEK (COPY), the desired record is read
 * onto the area pointed by the record descriptor. If the record
 * does not fit in such an area, the length of the record is
 * returned as a negative value in recdes->length and an error
 * condition is indicated.
 */
static SCAN_CODE
heap_get_if_diff_chn (PAGE_PTR pgptr, INT16 slotid, RECDES * recdes,
		      int ispeeking, int chn)
{
  RECDES chn_recdes;		/* Used when we need to compare the cache
				 * coherency number and we are not peeking
				 */
  SCAN_CODE scan;

  /*
   * Don't retrieve the object when the object has the same cache
   * coherency number given by the caller. That is, the caller has the
   * object cached.
   */

  if (ispeeking == PEEK)
    {
      scan = spage_get_record (pgptr, slotid, recdes, PEEK);
      if (chn != NULL_CHN && scan == S_SUCCESS && chn == or_chn (recdes))
	{
	  scan = S_SUCCESS_CHN_UPTODATE;
	}
    }
  else
    {
      scan = spage_get_record (pgptr, slotid, &chn_recdes, PEEK);
      if (chn != NULL_CHN && scan == S_SUCCESS && chn == or_chn (&chn_recdes))
	{
	  scan = S_SUCCESS_CHN_UPTODATE;
	}
      else
	{
	  /*
	   * Note that we could copy the recdes.data from chn_recdes.data, but
	   * I don't think it is much difference here, and we will have to deal
	   * with all not fit conditions and so on, so we decide to use
	   * spage_get_record instead.
	   */
	  scan = spage_get_record (pgptr, slotid, recdes, COPY);
	}
    }

  return scan;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * heap_get_chn () - Get the chn of the object
 *   return: chn or NULL_CHN
 *   oid(in): Object identifier
 *
 * Note: Find the cache coherency number of the object.
 */
int
heap_get_chn (THREAD_ENTRY * thread_p, const OID * oid)
{
  RECDES recdes;
  HEAP_SCANCACHE scan_cache;
  int chn;

  chn = heap_chnguess_get (thread_p, oid,
			   LOG_FIND_THREAD_TRAN_INDEX (thread_p));

  if (chn == NULL_CHN)
    {
      heap_scancache_quick_start (&scan_cache);
      if (heap_get (thread_p, oid, &recdes, &scan_cache, PEEK, NULL_CHN) ==
	  S_SUCCESS)
	{
	  chn = or_chn (&recdes);
	}
      heap_scancache_end (thread_p, &scan_cache);
    }

  return chn;
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * heap_get () - Retrieve or peek an object
 *   return: SCAN_CODE
 *           (Either of S_SUCCESS,
 *                      S_SUCCESS_CHN_UPTODATE,
 *                      S_DOESNT_FIT,
 *                      S_DOESNT_EXIST,
 *                      S_ERROR)
 *   oid(in): Object identifier
 *   recdes(in/out): Record descriptor
 *   scan_cache(in/out): Scan cache or NULL
 *   ispeeking(in): PEEK when the object is peeked, scan_cache cannot be NULL
 *                  COPY when the object is copied
 *   chn(in):
 *
 */
SCAN_CODE
heap_get (THREAD_ENTRY * thread_p, const OID * oid, RECDES * recdes,
	  HEAP_SCANCACHE * scan_cache, int ispeeking, int chn)
{
  return heap_get_internal (thread_p, NULL, oid, recdes, scan_cache,
			    ispeeking, chn);
}

/*
 * heap_get_internal () - Retrieve or peek an object
 *   return: SCAN_CODE
 *           (Either of S_SUCCESS,
 *                      S_SUCCESS_CHN_UPTODATE,
 *                      S_DOESNT_FIT,
 *                      S_DOESNT_EXIST,
 *                      S_ERROR)
 *   class_oid(out):
 *   oid(in): Object identifier
 *   recdes(in/out): Record descriptor
 *   scan_cache(in/out): Scan cache or NULL
 *   ispeeking(in): PEEK when the object is peeked, scan_cache cannot be NULL
 *                  COPY when the object is copied
 *   chn(in):
 *
 *   Note :
 *   If the type of record is REC_UNKNOWN(MARK_DELETED or DELETED_WILL_REUSE),
 *   this function returns S_DOESNT_EXIST and set warning.
 *   In this case, The caller should verify this.
 *   For example, In function scan_next_scan...
 *   If the current isolation level is uncommit_read and
 *   heap_get returns S_DOESNT_EXIST,
 *   then scan_next_scan ignores current record and continue.
 *   But if the isolation level is higher than uncommit_read,
 *   scan_next_scan returns an error.
 */
static SCAN_CODE
heap_get_internal (THREAD_ENTRY * thread_p, OID * class_oid, const OID * oid,
		   RECDES * recdes, HEAP_SCANCACHE * scan_cache,
		   int ispeeking, int chn)
{
  VPID home_vpid, forward_vpid;
  VPID *vpidptr_incache;
  PAGE_PTR pgptr = NULL;
  PAGE_PTR forward_pgptr = NULL;
  INT16 type;
  OID forward_oid;
  RECDES forward_recdes;
  SCAN_CODE scan;
  DISK_ISVALID oid_valid;
  int again_count = 0;
  int again_max = 20;

#if defined(CUBRID_DEBUG)
  if (scan_cache == NULL && ispeeking == PEEK)
    {
      er_log_debug (ARG_FILE_LINE, "heap_get: Using wrong interface."
		    " scan_cache cannot be NULL when peeking.");
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return S_ERROR;
    }

  if (scan_cache != NULL
      && scan_cache->debug_initpatter != HEAP_DEBUG_SCANCACHE_INITPATTER)
    {
      er_log_debug (ARG_FILE_LINE,
		    "heap_get: Your scancache is not initialized");
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return S_ERROR;
    }
#endif /* CUBRID_DEBUG */

  if (scan_cache == NULL)
    {
      /* It is possible only in case of ispeeking == COPY */
      if (recdes->data == NULL)
	{
	  er_log_debug (ARG_FILE_LINE, "heap_get: Using wrong interface."
			" recdes->area_size cannot be -1.");
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR,
		  0);
	  return S_ERROR;
	}
    }

  oid_valid = HEAP_ISVALID_OID (oid);
  if (oid_valid != DISK_VALID)
    {
      if (oid_valid != DISK_ERROR || er_errid () == NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_UNKNOWN_OBJECT, 3,
		  oid->volid, oid->pageid, oid->slotid);
	}
      return S_DOESNT_EXIST;
    }

try_again:

  home_vpid.volid = oid->volid;
  home_vpid.pageid = oid->pageid;

  /*
   * Use previous scan page whenever possible, otherwise, deallocate the
   * page
   */

  if (scan_cache != NULL && scan_cache->cache_last_fix_page == true
      && scan_cache->pgptr != NULL)
    {
      vpidptr_incache = pgbuf_get_vpid_ptr (scan_cache->pgptr);
      if (VPID_EQ (&home_vpid, vpidptr_incache))
	{
	  /* We can skip the fetch operation */
	  pgptr = scan_cache->pgptr;
	}
      else
	{
	  /* Free the previous scan page and obtain a new page */
	  pgbuf_unfix_and_init (thread_p, scan_cache->pgptr);
	  pgptr = heap_scan_pb_lock_and_fetch (thread_p, &home_vpid, OLD_PAGE,
					       S_LOCK, scan_cache);
	  if (pgptr == NULL)
	    {
	      if (er_errid () == ER_PB_BAD_PAGEID)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_HEAP_UNKNOWN_OBJECT, 3, oid->volid, oid->pageid,
			  oid->slotid);
		}

	      /* something went wrong, return */
	      scan_cache->pgptr = NULL;
	      return S_ERROR;
	    }
	}
      scan_cache->pgptr = NULL;
    }
  else
    {
      pgptr = heap_scan_pb_lock_and_fetch (thread_p, &home_vpid, OLD_PAGE,
					   S_LOCK, scan_cache);
      if (pgptr == NULL)
	{
	  if (er_errid () == ER_PB_BAD_PAGEID)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_HEAP_UNKNOWN_OBJECT, 3, oid->volid, oid->pageid,
		      oid->slotid);
	    }

	  /* something went wrong, return */
	  return S_ERROR;
	}
    }

  if (class_oid != NULL)
    {
      RECDES chain_recdes;
      HEAP_CHAIN *chain;

      if (spage_get_record (pgptr, HEAP_HEADER_AND_CHAIN_SLOTID,
			    &chain_recdes, PEEK) != S_SUCCESS)
	{
	  pgbuf_unfix_and_init (thread_p, pgptr);
	  return S_ERROR;
	}

      chain = (HEAP_CHAIN *) chain_recdes.data;
      COPY_OID (class_oid, &(chain->class_oid));

      /*
       * kludge, rootclass is identified with a NULL class OID but we must
       * substitute the actual OID here - think about this
       */
      if (OID_ISNULL (class_oid))
	{
	  /* rootclass class oid, substitute with global */
	  class_oid->volid = oid_Root_class_oid->volid;
	  class_oid->pageid = oid_Root_class_oid->pageid;
	  class_oid->slotid = oid_Root_class_oid->slotid;
	}
    }

  type = spage_get_record_type (pgptr, oid->slotid);
  if (type == REC_UNKNOWN)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_HEAP_UNKNOWN_OBJECT, 3,
	      oid->volid, oid->pageid, oid->slotid);
      pgbuf_unfix_and_init (thread_p, pgptr);
      return S_DOESNT_EXIST;
    }

  switch (type)
    {
    case REC_RELOCATION:
      /*
       * The record stored on the page is a relocation record, get the new
       * home of the record
       */
      forward_recdes.data = (char *) &forward_oid;
      forward_recdes.length = sizeof (forward_oid);
      forward_recdes.area_size = sizeof (forward_oid);

      scan = spage_get_record (pgptr, oid->slotid, &forward_recdes, COPY);
      if (scan != S_SUCCESS)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_HEAP_BAD_RELOCATION_RECORD, 3, oid->volid, oid->pageid,
		  oid->slotid);
	  pgbuf_unfix_and_init (thread_p, pgptr);
	  return scan;
	}

      /* Fetch the page of relocated (forwarded) record */
      forward_vpid.volid = forward_oid.volid;
      forward_vpid.pageid = forward_oid.pageid;

      /* try to fix forward page conditionally */
      forward_pgptr = pgbuf_fix (thread_p, &forward_vpid, OLD_PAGE,
				 PGBUF_LATCH_READ, PGBUF_CONDITIONAL_LATCH);
      if (forward_pgptr == NULL)
	{
	  pgbuf_unfix_and_init (thread_p, pgptr);

	  /* try to fix forward page unconditionally */
	  forward_pgptr = heap_scan_pb_lock_and_fetch (thread_p,
						       &forward_vpid,
						       OLD_PAGE, S_LOCK,
						       scan_cache);
	  if (forward_pgptr == NULL)
	    {
	      if (er_errid () == ER_PB_BAD_PAGEID)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_HEAP_BAD_RELOCATION_RECORD, 3,
			  forward_oid.volid, forward_oid.pageid,
			  forward_oid.slotid);
		}

	      return S_ERROR;
	    }
	  pgbuf_unfix_and_init (thread_p, forward_pgptr);

	  if (again_count++ >= again_max)
	    {
	      if (er_errid () == ER_PB_BAD_PAGEID)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_HEAP_UNKNOWN_OBJECT, 3, oid->volid,
			  oid->pageid, oid->slotid);
		}
	      else if (er_errid () == NO_ERROR)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_PAGE_LATCH_ABORTED, 2, forward_vpid.volid,
			  forward_vpid.pageid);
		}
	      return S_ERROR;
	    }

	  goto try_again;
	}

      assert (pgptr != NULL && forward_pgptr != NULL);
      assert (spage_get_record_type (forward_pgptr,
				     forward_oid.slotid) == REC_NEWHOME);

      if (ispeeking == COPY && recdes->data == NULL)
	{
	  /* It is guaranteed that scan_cache is not NULL. */
	  if (scan_cache->area == NULL)
	    {
	      /* Allocate an area to hold the object. Assume that
	         the object will fit in two pages for not better estimates.
	       */
	      scan_cache->area_size = DB_PAGESIZE * 2;
	      scan_cache->area = (char *) db_private_alloc (thread_p,
							    scan_cache->
							    area_size);
	      if (scan_cache->area == NULL)
		{
		  scan_cache->area_size = -1;
		  pgbuf_unfix_and_init (thread_p, pgptr);
		  pgbuf_unfix_and_init (thread_p, forward_pgptr);
		  return S_ERROR;
		}
	    }
	  recdes->data = scan_cache->area;
	  recdes->area_size = scan_cache->area_size;
	  /* The allocated space is enough to save the instance. */
	}

      scan = heap_get_if_diff_chn (forward_pgptr, forward_oid.slotid, recdes,
				   ispeeking, chn);
      if (scan_cache != NULL && scan_cache->cache_last_fix_page == true)
	{
	  /* Save the page for a future scan */
	  scan_cache->pgptr = pgptr;
	}
      else
	{
	  pgbuf_unfix_and_init (thread_p, pgptr);
	}

      pgbuf_unfix_and_init (thread_p, forward_pgptr);

      break;

    case REC_ASSIGN_ADDRESS:
      /* Object without content.. only the address has been assigned */
      if (spage_check_slot_owner (thread_p, pgptr, oid->slotid))
	{
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE,
		  ER_HEAP_NODATA_NEWADDRESS, 3, oid->volid, oid->pageid,
		  oid->slotid);
	  scan = S_DOESNT_EXIST;
	}
      else
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_UNKNOWN_OBJECT,
		  3, oid->volid, oid->pageid, oid->slotid);
	  scan = S_ERROR;
	}

      if (scan_cache != NULL && scan_cache->cache_last_fix_page == true)
	{
	  /* Save the page for a future scan */
	  scan_cache->pgptr = pgptr;
	}
      else
	{
	  pgbuf_unfix_and_init (thread_p, pgptr);
	}

      break;

    case REC_HOME:
      if (ispeeking == COPY && recdes->data == NULL)
	{			/* COPY */
	  /* It is guaranteed that scan_cache is not NULL. */
	  if (scan_cache->area == NULL)
	    {
	      /* Allocate an area to hold the object. Assume that
	         the object will fit in two pages for not better estimates.
	       */
	      scan_cache->area_size = DB_PAGESIZE * 2;
	      scan_cache->area = (char *) db_private_alloc (thread_p,
							    scan_cache->
							    area_size);
	      if (scan_cache->area == NULL)
		{
		  scan_cache->area_size = -1;
		  pgbuf_unfix_and_init (thread_p, pgptr);
		  return S_ERROR;
		}
	    }
	  recdes->data = scan_cache->area;
	  recdes->area_size = scan_cache->area_size;
	  /* The allocated space is enough to save the instance. */
	}

      scan = heap_get_if_diff_chn (pgptr, oid->slotid, recdes,
				   ispeeking, chn);
      if (scan_cache != NULL && scan_cache->cache_last_fix_page == true)
	{
	  /* Save the page for a future scan */
	  scan_cache->pgptr = pgptr;
	}
      else
	{
	  pgbuf_unfix_and_init (thread_p, pgptr);
	}
      break;

    case REC_BIGONE:
      /* Get the address of the content of the multipage object in overflow */
      forward_recdes.data = (char *) &forward_oid;
      forward_recdes.length = sizeof (forward_oid);
      forward_recdes.area_size = sizeof (forward_oid);

      scan = spage_get_record (pgptr, oid->slotid, &forward_recdes, COPY);
      if (scan != S_SUCCESS)
	{
	  /* Unable to read overflow address of multipage object */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_HEAP_BAD_RELOCATION_RECORD, 3, oid->volid, oid->pageid,
		  oid->slotid);
	  pgbuf_unfix_and_init (thread_p, pgptr);
	  return scan;
	}
      pgbuf_unfix_and_init (thread_p, pgptr);

      /*
       * Now get the content of the multipage object.
       */

      /* Try to reuse the previously allocated area */
      if (scan_cache != NULL && (ispeeking == PEEK || recdes->data == NULL))
	{
	  if (scan_cache->area == NULL)
	    {
	      /*
	       * Allocate an area to hold the object. Assume that the object
	       * will fit in two pages for not better estimates. We could call
	       * heap_ovf_get_length, but it may be better to just guess and
	       * realloc if needed.
	       * We could also check the estimates for average object length,
	       * but again, it may be expensive and may not be accurate
	       * for this object.
	       */
	      scan_cache->area_size = DB_PAGESIZE * 2;
	      scan_cache->area = (char *) db_private_alloc (thread_p,
							    scan_cache->
							    area_size);
	      if (scan_cache->area == NULL)
		{
		  scan_cache->area_size = -1;
		  return S_ERROR;
		}
	    }
	  recdes->data = scan_cache->area;
	  recdes->area_size = scan_cache->area_size;

	  while ((scan = heap_ovf_get (thread_p, &forward_oid, recdes, chn))
		 == S_DOESNT_FIT)
	    {
	      /*
	       * The object did not fit into such an area, reallocate a new
	       * area
	       */

	      chn = NULL_CHN;	/* To avoid checking again */

	      recdes->area_size = -recdes->length;
	      recdes->data =
		(char *) db_private_realloc (thread_p, scan_cache->area,
					     recdes->area_size);
	      if (recdes->data == NULL)
		{
		  return S_ERROR;
		}
	      scan_cache->area_size = recdes->area_size;
	      scan_cache->area = recdes->data;
	    }
	  if (scan != S_SUCCESS)
	    {
	      recdes->data = NULL;
	    }
	}
      else
	{
	  scan = heap_ovf_get (thread_p, &forward_oid, recdes, chn);
	}

      break;

    case REC_MARKDELETED:
    case REC_DELETED_WILL_REUSE:
    case REC_NEWHOME:
    default:
      scan = S_ERROR;
      pgbuf_unfix_and_init (thread_p, pgptr);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_BAD_OBJECT_TYPE, 3,
	      oid->volid, oid->pageid, oid->slotid);
      break;
    }

  return scan;
}

/*
 * heap_get_with_class_oid () - Retrieve or peek an object and get its class oid
 *   return: SCAN_CODE
 *           (Either of S_SUCCESS, S_DOESNT_FIT, S_DOESNT_EXIST, S_ERROR)
 *   class_oid(out): Class OID for the object
 *   oid(in): Object identifier
 *   recdes(in/out): Record descriptor
 *   scan_cache(in/out): Scan cache or NULL
 *   ispeeking(in): PEEK when the object is peeked, scan_cache cannot be
 *                  NULL COPY when the object is copied
 *
 * Note: Same as heap_get, except that it will also return the class oid
 * for the object.  (see heap_get) description)
 */
SCAN_CODE
heap_get_with_class_oid (THREAD_ENTRY * thread_p, OID * class_oid,
			 const OID * oid, RECDES * recdes,
			 HEAP_SCANCACHE * scan_cache, int ispeeking)
{
  SCAN_CODE scan;

  if (class_oid == NULL)
    {
      assert (false);
      return S_ERROR;
    }

  scan =
    heap_get_internal (thread_p, class_oid, oid, recdes, scan_cache,
		       ispeeking, NULL_CHN);
  if (scan != S_SUCCESS)
    {
      OID_SET_NULL (class_oid);
    }

  return scan;
}

/*
 * heap_next () - Retrieve or peek next object
 *   return: SCAN_CODE (Either of S_SUCCESS, S_DOESNT_FIT, S_END, S_ERROR)
 *   hfid(in):
 *   class_oid(in):
 *   next_oid(in/out): Object identifier of current record.
 *                     Will be set to next available record or NULL_OID when
 *                     there is not one.
 *   recdes(in/out): Pointer to a record descriptor. Will be modified to
 *                   describe the new record.
 *   scan_cache(in/out): Scan cache or NULL
 *   ispeeking(in): PEEK when the object is peeked, scan_cache cannot be NULL
 *                  COPY when the object is copied
 *
 */
SCAN_CODE
heap_next (THREAD_ENTRY * thread_p, const HFID * hfid, OID * class_oid,
	   OID * next_oid, RECDES * recdes, HEAP_SCANCACHE * scan_cache,
	   int ispeeking)
{
  VPID vpid;
  VPID *vpidptr_incache;
  PAGE_PTR pgptr = NULL;
  INT16 type = REC_UNKNOWN;
  OID oid;
  OID forward_oid;
  OID *peek_oid;
  RECDES forward_recdes;
  SCAN_CODE scan = S_ERROR;
  int continue_looking;

#if defined(CUBRID_DEBUG)
  if (scan_cache == NULL && ispeeking == PEEK)
    {
      er_log_debug (ARG_FILE_LINE, "heap_next: Using wrong interface."
		    " scan_cache cannot be NULL when peeking.");
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return S_ERROR;
    }
  if (scan_cache != NULL
      && scan_cache->debug_initpatter != HEAP_DEBUG_SCANCACHE_INITPATTER)
    {
      er_log_debug (ARG_FILE_LINE,
		    "heap_next: Your scancache is not initialized");
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return S_ERROR;
    }
  if (scan_cache != NULL && HFID_IS_NULL (&scan_cache->hfid))
    {
      er_log_debug (ARG_FILE_LINE, "heap_next: scan_cache without heap.."
		    " heap file must be given to heap_scancache_start () when"
		    " scan_cache is used with heap_first, heap_next, heap_prev"
		    " heap_last");
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return S_ERROR;
    }
#endif /* CUBRID_DEBUG */

  if (scan_cache == NULL)
    {
      /* It is possible only in case of ispeeking == COPY */
      if (recdes->data == NULL)
	{
	  er_log_debug (ARG_FILE_LINE, "heap_next: Using wrong interface."
			" recdes->area_size cannot be -1.");
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR,
		  0);
	  return S_ERROR;
	}
    }

  if (scan_cache != NULL)
    {
      hfid = &scan_cache->hfid;
      if (!OID_ISNULL (&scan_cache->class_oid))
	{
	  class_oid = &scan_cache->class_oid;
	}
    }

  if (OID_ISNULL (next_oid))
    {
      /* Retrieve the first object of the heap */
      oid.volid = hfid->vfid.volid;
      oid.pageid = hfid->hpgid;
      oid.slotid = 0;		/* i.e., will get slot 1 */
    }
  else
    {
      oid = *next_oid;
    }

  continue_looking = true;
  while (continue_looking == true)
    {
      continue_looking = false;

      while (true)
	{
	  vpid.volid = oid.volid;
	  vpid.pageid = oid.pageid;

	  /*
	   * Fetch the page where the object of OID is stored. Use previous
	   * scan page whenever possible, otherwise, deallocate the page.
	   */
	  if (scan_cache != NULL)
	    {
	      pgptr = NULL;
	      if (scan_cache->cache_last_fix_page == true
		  && scan_cache->pgptr != NULL)
		{
		  vpidptr_incache = pgbuf_get_vpid_ptr (scan_cache->pgptr);
		  if (VPID_EQ (&vpid, vpidptr_incache))
		    {
		      /* We can skip the fetch operation */
		      pgptr = scan_cache->pgptr;
		      scan_cache->pgptr = NULL;
		    }
		  else
		    {
		      /* Free the previous scan page */
		      pgbuf_unfix_and_init (thread_p, scan_cache->pgptr);
		    }
		}
	      if (pgptr == NULL)
		{
		  pgptr = heap_scan_pb_lock_and_fetch (thread_p, &vpid,
						       OLD_PAGE, S_LOCK,
						       scan_cache);
		  if (pgptr == NULL)
		    {
		      if (er_errid () == ER_PB_BAD_PAGEID)
			{
			  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
				  ER_HEAP_UNKNOWN_OBJECT, 3, oid.volid,
				  oid.pageid, oid.slotid);
			}

		      /* something went wrong, return */
		      scan_cache->pgptr = NULL;
		      return S_ERROR;
		    }
		  if (heap_scancache_update_hinted_when_lots_space (thread_p,
								    scan_cache,
								    pgptr)
		      != NO_ERROR)
		    {
		      pgbuf_unfix_and_init (thread_p, pgptr);
		      /* something went wrong, return */
		      scan_cache->pgptr = NULL;
		      return S_ERROR;
		    }
		}
	    }
	  else
	    {
	      pgptr = heap_scan_pb_lock_and_fetch (thread_p, &vpid, OLD_PAGE,
						   S_LOCK, NULL);
	      if (pgptr == NULL)
		{
		  if (er_errid () == ER_PB_BAD_PAGEID)
		    {
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			      ER_HEAP_UNKNOWN_OBJECT, 3, oid.volid,
			      oid.pageid, oid.slotid);
		    }

		  /* something went wrong, return */
		  return S_ERROR;
		}
	    }

	  /*
	   * Find the next object. Skip relocated records (i.e., new_home
	   * records). This records must be accessed through the relocation
	   * record (i.e., the object).
	   */

	  while (((scan = spage_next_record (pgptr, &oid.slotid,
					     &forward_recdes,
					     PEEK)) == S_SUCCESS)
		 && (oid.slotid == HEAP_HEADER_AND_CHAIN_SLOTID
		     || ((type = spage_get_record_type (pgptr,
							oid.slotid)) ==
			 REC_NEWHOME)
		     || type == REC_ASSIGN_ADDRESS || type == REC_UNKNOWN))
	    {
	      ;			/* Nothing */
	    }

	  if (scan != S_SUCCESS)
	    {
	      if (scan == S_END)
		{
		  /* Find next page of heap */
		  (void) heap_vpid_next (hfid, pgptr, &vpid);
		  pgbuf_unfix_and_init (thread_p, pgptr);
		  oid.volid = vpid.volid;
		  oid.pageid = vpid.pageid;
		  oid.slotid = -1;
		  if (oid.pageid == NULL_PAGEID)
		    {
		      OID_SET_NULL (next_oid);
		      return scan;
		    }
		}
	      else
		{
		  pgbuf_unfix_and_init (thread_p, pgptr);
		  return scan;
		}
	    }
	  else
	    {
	      break;
	    }
	}

      /*
       * A RECORD was found
       * If the next record is a relocation record, get the new home of the
       * record
       */

      switch (type)
	{
	case REC_RELOCATION:
	  /*
	   * The record stored on the page is a relocation record, get the new
	   * home of the record
	   */
	  peek_oid = (OID *) forward_recdes.data;
	  forward_oid = *peek_oid;

	  pgbuf_unfix_and_init (thread_p, pgptr);

	  /* Fetch the page of relocated (forwarded/new home) record */
	  vpid.volid = forward_oid.volid;
	  vpid.pageid = forward_oid.pageid;

	  pgptr = heap_scan_pb_lock_and_fetch (thread_p, &vpid, OLD_PAGE,
					       S_LOCK, scan_cache);
	  if (pgptr == NULL)
	    {
	      /* something went wrong, return */
	      if (er_errid () == ER_PB_BAD_PAGEID)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_HEAP_BAD_RELOCATION_RECORD, 3,
			  oid.volid, oid.pageid, oid.slotid);
		}
	      return S_ERROR;
	    }

#if defined(CUBRID_DEBUG)
	  if (spage_get_record_type (pgptr, forward_oid.slotid) !=
	      REC_NEWHOME)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_HEAP_BAD_OBJECT_TYPE, 3, forward_oid.volid,
		      forward_oid.pageid, forward_oid.slotid);
	      pgbuf_unfix_and_init (thread_p, pgptr);
	      return S_ERROR;
	    }
#endif

	  if (scan_cache != NULL && ispeeking == COPY && recdes->data == NULL)
	    {
	      /* It is guaranteed that scan_cache is not NULL. */
	      if (scan_cache->area == NULL)
		{
		  /* Allocate an area to hold the object. Assume that
		     the object will fit in two pages for not better estimates.
		   */
		  scan_cache->area_size = DB_PAGESIZE * 2;
		  scan_cache->area = (char *) db_private_alloc (thread_p,
								scan_cache->
								area_size);
		  if (scan_cache->area == NULL)
		    {
		      scan_cache->area_size = -1;
		      pgbuf_unfix_and_init (thread_p, pgptr);
		      return S_ERROR;
		    }
		}
	      recdes->data = scan_cache->area;
	      recdes->area_size = scan_cache->area_size;
	      /* The allocated space is enough to save the instance. */
	    }

	  if (scan_cache != NULL && scan_cache->cache_last_fix_page == true)
	    {
	      if (ispeeking == PEEK)
		{
		  scan = spage_get_record (pgptr, forward_oid.slotid, recdes,
					   PEEK);
		}
	      else
		{
		  scan = spage_get_record (pgptr, forward_oid.slotid, recdes,
					   COPY);
		}
	      /* Save the page for a future scan */
	      scan_cache->pgptr = pgptr;
	    }
	  else
	    {
	      scan = spage_get_record (pgptr, forward_oid.slotid, recdes,
				       COPY);
	      pgbuf_unfix_and_init (thread_p, pgptr);
	    }

	  break;

	case REC_BIGONE:
	  /* Get the address of the content of the multipage object */
	  peek_oid = (OID *) forward_recdes.data;
	  forward_oid = *peek_oid;
	  pgbuf_unfix_and_init (thread_p, pgptr);

	  /* Now get the content of the multipage object. */
	  /* Try to reuse the previously allocated area */
	  if (scan_cache != NULL
	      && (ispeeking == PEEK || recdes->data == NULL))
	    {
	      /* It is guaranteed that scan_cache is not NULL. */
	      if (scan_cache->area == NULL)
		{
		  /*
		   * Allocate an area to hold the object. Assume that the object
		   * will fit in two pages for not better estimates.
		   * We could call heap_ovf_get_length, but it may be better
		   * to just guess and realloc if needed. We could also check
		   * the estimates for average object length, but again,
		   * it may be expensive and may not be accurate for the object.
		   */
		  scan_cache->area_size = DB_PAGESIZE * 2;
		  scan_cache->area = (char *) db_private_alloc (thread_p,
								scan_cache->
								area_size);
		  if (scan_cache->area == NULL)
		    {
		      scan_cache->area_size = -1;
		      return S_ERROR;
		    }
		}
	      recdes->data = scan_cache->area;
	      recdes->area_size = scan_cache->area_size;

	      while ((scan = heap_ovf_get (thread_p, &forward_oid, recdes,
					   NULL_CHN)) == S_DOESNT_FIT)
		{
		  /*
		   * The object did not fit into such an area, reallocate a new
		   * area
		   */
		  recdes->area_size = -recdes->length;
		  recdes->data =
		    (char *) db_private_realloc (thread_p, scan_cache->area,
						 recdes->area_size);
		  if (recdes->data == NULL)
		    {
		      return S_ERROR;
		    }
		  scan_cache->area_size = recdes->area_size;
		  scan_cache->area = recdes->data;
		}
	      if (scan != S_SUCCESS)
		{
		  recdes->data = NULL;
		}
	    }
	  else
	    {
	      scan = heap_ovf_get (thread_p, &forward_oid, recdes, NULL_CHN);
	    }

	  break;

	case REC_HOME:
	  if (scan_cache != NULL && ispeeking == COPY && recdes->data == NULL)
	    {
	      /* It is guaranteed that scan_cache is not NULL. */
	      if (scan_cache->area == NULL)
		{
		  /* Allocate an area to hold the object. Assume that
		   * the object will fit in two pages for not better estimates.
		   */
		  scan_cache->area_size = DB_PAGESIZE * 2;
		  scan_cache->area = (char *) db_private_alloc (thread_p,
								scan_cache->
								area_size);
		  if (scan_cache->area == NULL)
		    {
		      scan_cache->area_size = -1;
		      pgbuf_unfix_and_init (thread_p, pgptr);
		      return S_ERROR;
		    }
		}
	      recdes->data = scan_cache->area;
	      recdes->area_size = scan_cache->area_size;
	      /* The allocated space is enough to save the instance. */
	    }

	  if (scan_cache != NULL && scan_cache->cache_last_fix_page == true)
	    {
	      if (ispeeking == PEEK)
		{
		  scan = spage_get_record (pgptr, oid.slotid, recdes, PEEK);
		}
	      else
		{
		  scan = spage_get_record (pgptr, oid.slotid, recdes, COPY);
		}
	      /* Save the page for a future scan */
	      scan_cache->pgptr = pgptr;
	    }
	  else
	    {
	      scan = spage_get_record (pgptr, oid.slotid, recdes, COPY);
	      pgbuf_unfix_and_init (thread_p, pgptr);
	    }

	  break;

	case REC_NEWHOME:
	case REC_MARKDELETED:
	case REC_DELETED_WILL_REUSE:
	default:
	  /* This should never happen */
	  scan = S_ERROR;
	  pgbuf_unfix_and_init (thread_p, pgptr);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_BAD_OBJECT_TYPE,
		  3, oid.volid, oid.pageid, oid.slotid);
	  break;
	}

      if (scan == S_SUCCESS)
	{
	  /*
	   * Make sure that the found object is an instance of the desired
	   * class. If it isn't then continue looking.
	   */
	  if (class_oid == NULL || OID_ISNULL (class_oid) ||
	      !OID_IS_ROOTOID (&oid))
	    {
	      *next_oid = oid;
	    }
	  else
	    {
	      continue_looking = true;
	    }
	}
    }

  return scan;
}

/*
 * heap_prev () - Retrieve or peek previous object
 *   return: SCAN_CODE (Either of S_SUCCESS, S_DOESNT_FIT, S_END, S_ERROR)
 *   hfid(in):
 *   class_oid(in):
 *   prev_oid(in/out): Object identifier of current record.
 *                     Will be set to previous available record or NULL_OID
 *                     when there is not one.
 *   recdes(in/out): Pointer to a record descriptor. Will be modified to
 *                   describe the new record.
 *   scan_cache(in/out): Scan cache or NULL
 *   ispeeking(in): PEEK when the object is peeked, scan_cache cannot be NULL
 *                  COPY when the object is copied
 *
 */
SCAN_CODE
heap_prev (THREAD_ENTRY * thread_p, const HFID * hfid, OID * class_oid,
	   OID * prev_oid, RECDES * recdes, HEAP_SCANCACHE * scan_cache,
	   int ispeeking)
{
  VPID vpid;
  VPID *vpidptr_incache;
  PAGE_PTR pgptr = NULL;
  INT16 type = REC_UNKNOWN;
  OID oid;
  OID forward_oid;
  OID *peek_oid;
  RECDES forward_recdes;
  SCAN_CODE scan = S_ERROR;
  int continue_looking;

#if defined(CUBRID_DEBUG)
  if (scan_cache == NULL && ispeeking == PEEK)
    {
      er_log_debug (ARG_FILE_LINE, "heap_prev: Using wrong interface."
		    " scan_cache cannot be NULL when peeking.");
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return S_ERROR;
    }
  if (scan_cache != NULL
      && scan_cache->debug_initpatter != HEAP_DEBUG_SCANCACHE_INITPATTER)
    {
      er_log_debug (ARG_FILE_LINE,
		    "heap_prev: Your scancache is not initialized");
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return S_ERROR;
    }
  if (scan_cache != NULL && HFID_IS_NULL (&scan_cache->hfid))
    {
      er_log_debug (ARG_FILE_LINE, "heap_prev: scan_cache without heap.."
		    " heap file must be given to heap_scancache_start () when"
		    " scan_cache is used with heap_first, heap_next, heap_prev"
		    " heap_last");
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return S_ERROR;
    }
#endif /* CUBRID_DEBUG */

  if (scan_cache == NULL)
    {
      /* It is possible only in case of ispeeking == COPY */
      if (recdes->data == NULL)
	{
	  er_log_debug (ARG_FILE_LINE, "heap_prev: Using wrong interface."
			" recdes->area_size cannot be -1.");
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR,
		  0);
	  return S_ERROR;
	}
    }

  if (scan_cache != NULL)
    {
      hfid = &scan_cache->hfid;
      if (!OID_ISNULL (&scan_cache->class_oid))
	{
	  class_oid = &scan_cache->class_oid;
	}
    }

  if (OID_ISNULL (prev_oid))
    {
      /* Retrieve the last record of the file. */
      if (file_find_last_page (thread_p, &hfid->vfid, &vpid) != NULL)
	{
	  oid.volid = vpid.volid;
	  oid.pageid = vpid.pageid;
	}
      else
	{
	  oid.volid = NULL_VOLID;
	  oid.pageid = NULL_PAGEID;
	}
      oid.slotid = NULL_SLOTID;
    }
  else
    {
      oid = *prev_oid;
    }

  do
    {
      continue_looking = false;

      while (true)
	{
	  vpid.volid = oid.volid;
	  vpid.pageid = oid.pageid;

	  /*
	   * Fetch the page where the object of OID is stored. Use previous scan
	   * page whenever possible, otherwise, deallocate the page
	   */

	  if (scan_cache != NULL)
	    {
	      pgptr = NULL;
	      if (scan_cache->cache_last_fix_page == true
		  && scan_cache->pgptr != NULL)
		{
		  vpidptr_incache = pgbuf_get_vpid_ptr (scan_cache->pgptr);
		  if (VPID_EQ (&vpid, vpidptr_incache))
		    {
		      /* We can skip the fetch operation */
		      pgptr = scan_cache->pgptr;
		      scan_cache->pgptr = NULL;
		    }
		  else
		    {
		      /* Free the previous scan page */
		      pgbuf_unfix_and_init (thread_p, scan_cache->pgptr);
		    }
		}
	      if (pgptr == NULL)
		{
		  pgptr = heap_scan_pb_lock_and_fetch (thread_p, &vpid,
						       OLD_PAGE, S_LOCK,
						       scan_cache);
		  if (pgptr == NULL)
		    {
		      if (er_errid () == ER_PB_BAD_PAGEID)
			{
			  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
				  ER_HEAP_UNKNOWN_OBJECT, 3, oid.volid,
				  oid.pageid, oid.slotid);
			}

		      /* something went wrong, return */
		      scan_cache->pgptr = NULL;
		      return S_ERROR;
		    }
		  if (heap_scancache_update_hinted_when_lots_space (thread_p,
								    scan_cache,
								    pgptr)
		      != NO_ERROR)
		    {
		      pgbuf_unfix_and_init (thread_p, pgptr);
		      /* something went wrong, return */
		      scan_cache->pgptr = NULL;
		      return S_ERROR;
		    }
		}
	    }
	  else
	    {
	      pgptr = heap_scan_pb_lock_and_fetch (thread_p, &vpid, OLD_PAGE,
						   S_LOCK, NULL);
	      if (pgptr == NULL)
		{
		  if (er_errid () == ER_PB_BAD_PAGEID)
		    {
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			      ER_HEAP_UNKNOWN_OBJECT, 3, oid.volid,
			      oid.pageid, oid.slotid);
		    }

		  /* something went wrong, return */
		  return S_ERROR;
		}
	    }

	  /*
	   * Find the prev record. Skip relocated records. This records must be
	   * accessed through the relocation record.
	   */
	  while ((scan = spage_previous_record (pgptr, &oid.slotid,
						&forward_recdes,
						PEEK)) == S_SUCCESS
		 && (oid.slotid == HEAP_HEADER_AND_CHAIN_SLOTID
		     || ((type = spage_get_record_type (pgptr,
							oid.slotid)) ==
			 REC_NEWHOME)
		     || type == REC_ASSIGN_ADDRESS || type == REC_UNKNOWN))
	    {
	      ;			/* Nothing */
	    }

	  if (scan != S_SUCCESS)
	    {
	      if (scan == S_END)
		{
		  /* Get the previous page */
		  (void) heap_vpid_prev (hfid, pgptr, &vpid);
		  pgbuf_unfix_and_init (thread_p, pgptr);
		  oid.volid = vpid.volid;
		  oid.pageid = vpid.pageid;
		  oid.slotid = NULL_SLOTID;
		  if (oid.pageid == NULL_PAGEID)
		    {
		      OID_SET_NULL (prev_oid);
		      return scan;
		    }
		}
	      else
		{
		  pgbuf_unfix_and_init (thread_p, pgptr);
		  return scan;
		}
	    }
	  else
	    {
	      break;
	    }
	}

      /*
       * If the previous record is a relocation record, get the new home of the
       * record
       */

      switch (type)
	{
	case REC_RELOCATION:
	  /*
	   * The record stored on the page is a relocation record, get the new
	   * home of the record
	   */
	  peek_oid = (OID *) forward_recdes.data;
	  forward_oid = *peek_oid;
	  pgbuf_unfix_and_init (thread_p, pgptr);

	  /* Fetch the page of relocated (forwarded) record */
	  vpid.volid = forward_oid.volid;
	  vpid.pageid = forward_oid.pageid;

	  pgptr = heap_scan_pb_lock_and_fetch (thread_p, &vpid, OLD_PAGE,
					       S_LOCK, scan_cache);
	  if (pgptr == NULL)
	    {
	      /* something went wrong, return */
	      if (er_errid () == ER_PB_BAD_PAGEID)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_HEAP_BAD_RELOCATION_RECORD, 3,
			  oid.volid, oid.pageid, oid.slotid);
		}
	      return S_ERROR;
	    }

#if defined(CUBRID_DEBUG)
	  if (spage_get_record_type (pgptr, forward_oid.slotid) !=
	      REC_NEWHOME)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_HEAP_BAD_OBJECT_TYPE, 3, forward_oid.volid,
		      forward_oid.pageid, forward_oid.slotid);
	      pgbuf_unfix_and_init (thread_p, pgptr);
	      return S_ERROR;
	    }
#endif

	  if (scan_cache != NULL && ispeeking == COPY && recdes->data == NULL)
	    {
	      /* It is guaranteed that scan_cache is not NULL. */
	      if (scan_cache->area == NULL)
		{
		  /* Allocate an area to hold the object. Assume that
		     the object will fit in two pages for not better estimates.
		   */
		  scan_cache->area_size = DB_PAGESIZE * 2;
		  scan_cache->area = (char *) db_private_alloc (thread_p,
								scan_cache->
								area_size);
		  if (scan_cache->area == NULL)
		    {
		      scan_cache->area_size = -1;
		      pgbuf_unfix_and_init (thread_p, pgptr);
		      return S_ERROR;
		    }
		}
	      recdes->data = scan_cache->area;
	      recdes->area_size = scan_cache->area_size;
	      /* The allocated space is enough to save the instance. */
	    }

	  if (scan_cache != NULL && scan_cache->cache_last_fix_page == true)
	    {
	      if (ispeeking == PEEK)
		{
		  scan = spage_get_record (pgptr, forward_oid.slotid, recdes,
					   PEEK);
		}
	      else
		{
		  scan = spage_get_record (pgptr, forward_oid.slotid, recdes,
					   COPY);
		}
	      /* Save the page for a future scan */
	      scan_cache->pgptr = pgptr;
	    }
	  else
	    {
	      scan = spage_get_record (pgptr, forward_oid.slotid, recdes,
				       COPY);
	      pgbuf_unfix_and_init (thread_p, pgptr);
	    }

	  break;

	case REC_BIGONE:
	  /*
	   * Get the address of the content of the multipage object in overflow
	   */
	  peek_oid = (OID *) forward_recdes.data;
	  forward_oid = *peek_oid;
	  pgbuf_unfix_and_init (thread_p, pgptr);

	  /* Now get the content of the multipage object. */
	  /* Try to reuse the previously allocated area */
	  if (scan_cache != NULL
	      && (ispeeking == PEEK || recdes->data == NULL))
	    {
	      /* It is guaranteed that scan_cache is not NULL. */
	      if (scan_cache->area == NULL)
		{
		  /*
		   * Allocate an area to hold the object. Assume that the object
		   * will fit in two pages for not better estimates. We could call
		   * heap_ovf_get_length, but it may be better to just guess and realloc if
		   * needed. We could also check the estimates for average object
		   * length, but again, it may be expensive and may not be accurate
		   * for this object.
		   */
		  scan_cache->area_size = DB_PAGESIZE * 2;
		  scan_cache->area = (char *) db_private_alloc (thread_p,
								scan_cache->
								area_size);
		  if (scan_cache->area == NULL)
		    {
		      scan_cache->area_size = -1;
		      return S_ERROR;
		    }
		}
	      recdes->data = scan_cache->area;
	      recdes->area_size = scan_cache->area_size;

	      while ((scan = heap_ovf_get (thread_p, &forward_oid, recdes,
					   NULL_CHN)) == S_DOESNT_FIT)
		{
		  /*
		   * The object did not fit into such an area, reallocate a new
		   * area
		   */
		  recdes->area_size = -recdes->length;
		  recdes->data =
		    (char *) db_private_realloc (thread_p,
						 scan_cache->area,
						 recdes->area_size);
		  if (recdes->data == NULL)
		    {
		      return S_ERROR;
		    }
		  scan_cache->area_size = recdes->area_size;
		  scan_cache->area = recdes->data;
		}
	      if (scan != S_SUCCESS)
		{
		  recdes->data = NULL;
		}
	    }
	  else
	    {
	      scan = heap_ovf_get (thread_p, &forward_oid, recdes, NULL_CHN);
	    }

	  break;

	case REC_HOME:
	  if (scan_cache != NULL && ispeeking == COPY && recdes->data == NULL)
	    {
	      /* It is guaranteed that scan_cache is not NULL. */
	      if (scan_cache->area == NULL)
		{
		  /* Allocate an area to hold the object. Assume that
		     the object will fit in two pages for not better estimates.
		   */
		  scan_cache->area_size = DB_PAGESIZE * 2;
		  scan_cache->area = (char *) db_private_alloc (thread_p,
								scan_cache->
								area_size);
		  if (scan_cache->area == NULL)
		    {
		      scan_cache->area_size = -1;
		      pgbuf_unfix_and_init (thread_p, pgptr);
		      return S_ERROR;
		    }
		}
	      recdes->data = scan_cache->area;
	      recdes->area_size = scan_cache->area_size;
	      /* The allocated space is enough to save the instance. */
	    }

	  if (scan_cache != NULL && scan_cache->cache_last_fix_page == true)
	    {
	      if (ispeeking == PEEK)
		{
		  scan = spage_get_record (pgptr, oid.slotid, recdes, PEEK);
		}
	      else
		{
		  scan = spage_get_record (pgptr, oid.slotid, recdes, COPY);
		}
	      /* Save the page for a future scan */
	      scan_cache->pgptr = pgptr;
	    }
	  else
	    {
	      scan = spage_get_record (pgptr, oid.slotid, recdes, COPY);
	      pgbuf_unfix_and_init (thread_p, pgptr);
	    }

	  break;

	case REC_NEWHOME:
	case REC_MARKDELETED:
	case REC_DELETED_WILL_REUSE:
	default:
	  /* This should never happen */
	  scan = S_ERROR;
	  pgbuf_unfix_and_init (thread_p, pgptr);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_BAD_OBJECT_TYPE,
		  3, oid.volid, oid.pageid, oid.slotid);
	  break;
	}

      if (scan == S_SUCCESS)
	{
	  /*
	   * Make sure that the found object is an instance of the desired
	   * class. If it isn't then continue looking.
	   */
	  if (class_oid == NULL || OID_ISNULL (class_oid) ||
	      !OID_IS_ROOTOID (&oid))
	    {
	      *prev_oid = oid;
	    }
	  else
	    {
	      continue_looking = true;
	    }
	}
    }
  while (continue_looking == true);

  return scan;
}

/*
 * heap_first () - Retrieve or peek first object of heap
 *   return: SCAN_CODE (Either of S_SUCCESS, S_DOESNT_FIT, S_END, S_ERROR)
 *   hfid(in):
 *   class_oid(in):
 *   oid(in/out): Object identifier of current record.
 *                Will be set to first available record or NULL_OID when there
 *                is not one.
 *   recdes(in/out): Pointer to a record descriptor. Will be modified to
 *                   describe the new record.
 *   scan_cache(in/out): Scan cache or NULL
 *   ispeeking(in): PEEK when the object is peeked, scan_cache cannot be NULL
 *                  COPY when the object is copied
 *
 */
SCAN_CODE
heap_first (THREAD_ENTRY * thread_p, const HFID * hfid, OID * class_oid,
	    OID * oid, RECDES * recdes, HEAP_SCANCACHE * scan_cache,
	    int ispeeking)
{
  /* Retrieve the first record of the file */
  OID_SET_NULL (oid);
  oid->volid = hfid->vfid.volid;

  return heap_next (thread_p, hfid, class_oid, oid, recdes, scan_cache,
		    ispeeking);
}

/*
 * heap_last () - Retrieve or peek last object of heap
 *   return: SCAN_CODE
 *           (Either of S_SUCCESS, S_DOESNT_FIT, S_END,
 *                      S_ERROR)
 *   hfid(in):
 *   class_oid(in):
 *   oid(in/out): Object identifier of current record.
 *                Will be set to last available record or NULL_OID when there is
 *                not one.
 *   recdes(in/out): Pointer to a record descriptor. Will be modified to
 *                   describe the new record.
 *   scan_cache(in/out): Scan cache or NULL
 *   ispeeking(in): PEEK when the object is peeked, scan_cache cannot be NULL
 *                  COPY when the object is copied
 *
 */
SCAN_CODE
heap_last (THREAD_ENTRY * thread_p, const HFID * hfid, OID * class_oid,
	   OID * oid, RECDES * recdes, HEAP_SCANCACHE * scan_cache,
	   int ispeeking)
{
  /* Retrieve the first record of the file */
  OID_SET_NULL (oid);
  oid->volid = hfid->vfid.volid;

  return heap_prev (thread_p, hfid, class_oid, oid, recdes, scan_cache,
		    ispeeking);
}

/*
 * heap_get_alloc () - get/retrieve an object by allocating and freeing area
 *   return: NO_ERROR
 *   oid(in): Object identifier
 *   recdes(in): Record descriptor
 *
 * Note: The object associated with the given OID is copied into the
 * allocated area pointed to by the record descriptor. If the
 * object does not fit in such an area. The area is freed and a
 * new area is allocated to hold the object.
 * The caller is responsible from deallocating the area.
 *
 * Note: The area in the record descriptor is one dynamically allocated
 * with malloc and free with free_and_init.
 */
int
heap_get_alloc (THREAD_ENTRY * thread_p, const OID * oid, RECDES * recdes)
{
  SCAN_CODE scan;
  char *new_area;
  int ret = NO_ERROR;

  if (recdes->data == NULL)
    {
      recdes->area_size = DB_PAGESIZE;	/* assume that only one page is needed */
      recdes->data = (char *) malloc (recdes->area_size);
      if (recdes->data == NULL)
	{
	  ret = ER_OUT_OF_VIRTUAL_MEMORY;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ret, 1,
		  recdes->area_size);
	  goto exit_on_error;
	}
    }

  /* Get the object */
  while ((scan =
	  heap_get (thread_p, oid, recdes, NULL, COPY,
		    NULL_CHN)) != S_SUCCESS)
    {
      if (scan == S_DOESNT_FIT)
	{
	  /* Is more space needed ? */
	  new_area = (char *) realloc (recdes->data, -(recdes->length));
	  if (new_area == NULL)
	    {
	      ret = ER_OUT_OF_VIRTUAL_MEMORY;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ret, 1,
		      -(recdes->length));
	      goto exit_on_error;
	    }
	  recdes->area_size = -recdes->length;
	  recdes->data = new_area;
	}
      else
	{
	  goto exit_on_error;
	}
    }

  return ret;

exit_on_error:

  if (recdes->data != NULL)
    {
      free_and_init (recdes->data);
      recdes->area_size = 0;
    }

  return (ret == NO_ERROR) ? ER_FAILED : ret;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * heap_cmp () - Compare heap object with current content
 *   return: int (> 0 recdes is larger,
 *                     < 0 recdes is smaller, and
 *                     = 0 same)
 *   oid(in): The object to compare
 *   recdes(in): Compare object against this content
 *
 * Note: Compare the heap object against given content in ASCII format.
 */
int
heap_cmp (THREAD_ENTRY * thread_p, const OID * oid, RECDES * recdes)
{
  HEAP_SCANCACHE scan_cache;
  RECDES peek_recdes;
  int compare;

  heap_scancache_quick_start (&scan_cache);
  if (heap_get (thread_p, oid, &peek_recdes, &scan_cache, PEEK, NULL_CHN) !=
      S_SUCCESS)
    {
      compare = 1;
    }
  else if (recdes->length > peek_recdes.length)
    {
      compare = memcmp (recdes->data, peek_recdes.data, peek_recdes.length);
      if (compare == 0)
	{
	  compare = 1;
	}
    }
  else
    {
      compare = memcmp (recdes->data, peek_recdes.data, recdes->length);
      if (compare == 0 && recdes->length != peek_recdes.length)
	{
	  compare = -1;
	}
    }

  heap_scancache_end (thread_p, &scan_cache);

  return compare;
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * heap_scanrange_start () - Initialize a scanrange cursor
 *   return: NO_ERROR
 *   scan_range(in/out): Scan range
 *   hfid(in): Heap file identifier
 *   class_oid(in): Class identifier
 *                  For any class, NULL or NULL_OID can be given
 *   lock_hint(in):
 *
 * Note: A scanrange structure is initialized. The scanrange structure
 * is used to define a scan range (set of objects) and to cache
 * information about the latest fetched page and memory allocated
 * by the scan functions. This information is used in future
 * scans, for example, to avoid hashing for the same page in the
 * page buffer pool or defining another allocation area.
 * The caller is responsible for declaring the end of a scan
 * range so that the fixed pages and allocated memory are freed.
 * Using many scans at the same time should be avoided since page
 * buffers are fixed and locked for future references and there
 * is a limit of buffers in the page buffer pool. This is
 * analogous to fetching many pages at the same time.
 */
int
heap_scanrange_start (THREAD_ENTRY * thread_p, HEAP_SCANRANGE * scan_range,
		      const HFID * hfid, const OID * class_oid, int lock_hint)
{
  int ret = NO_ERROR;

  /* Start the scan cache */
  ret = heap_scancache_start (thread_p, &scan_range->scan_cache, hfid,
			      class_oid, true, false, lock_hint);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  OID_SET_NULL (&scan_range->first_oid);
  scan_range->first_oid.volid = hfid->vfid.volid;
  scan_range->last_oid = scan_range->first_oid;

  return ret;

exit_on_error:

  OID_SET_NULL (&scan_range->first_oid);
  OID_SET_NULL (&scan_range->last_oid);

  return (ret == NO_ERROR
	  && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
}

/*
 * heap_scanrange_end () - End of a scanrange
 *   return:
 *   scan_range(in/out): Scanrange
 *
 * Note: Any fixed heap page on the given scan is freed and any memory
 * allocated by this scan is also freed. The scan_range structure is undefined.
 */
void
heap_scanrange_end (THREAD_ENTRY * thread_p, HEAP_SCANRANGE * scan_range)
{
  /* Finish the scan cache */
  heap_scancache_end (thread_p, &scan_range->scan_cache);
  OID_SET_NULL (&scan_range->first_oid);
  OID_SET_NULL (&scan_range->last_oid);
}

/*
 * heap_scanrange_to_following () - Define the following scanrange
 *   return: SCAN_CODE
 *           (Either of S_SUCCESS, S_END, S_ERROR)
 *   scan_range(in/out): Scanrange
 *   start_oid(in): Desired OID for first element in the scanrange or NULL
 *
 * Note: The range of a scanrange is defined. The scanrange is defined
 * as follows:
 *              a: When start_oid == NULL, the first scanrange object is the
 *                 next object after the last object in the previous scanrange
 *              b: When start_oid is the same as a NULL_OID, the first object
 *                 is the first heap object.
 *              c: The first object in the scanrange is the given object.
 *              The last object in the scanrange is either the first object in
 *              the scanrange or the one after the first object which is not a
 *              relocated or multipage object.
 */
SCAN_CODE
heap_scanrange_to_following (THREAD_ENTRY * thread_p,
			     HEAP_SCANRANGE * scan_range, OID * start_oid)
{
  SCAN_CODE scan;
  RECDES recdes;
  INT16 slotid;
  VPID *vpid;

  if (HEAP_DEBUG_ISVALID_SCANRANGE (scan_range) != DISK_VALID)
    {
      return S_ERROR;
    }

  if (start_oid != NULL)
    {
      if (OID_ISNULL (start_oid))
	{
	  /* Scanrange starts at first heap object */
	  scan = heap_first (thread_p, &scan_range->scan_cache.hfid,
			     &scan_range->scan_cache.class_oid,
			     &scan_range->first_oid, &recdes,
			     &scan_range->scan_cache, PEEK);
	  if (scan != S_SUCCESS)
	    {
	      return scan;
	    }
	}
      else
	{
	  /* Scanrange starts with the given object */
	  scan_range->first_oid = *start_oid;
	  scan = heap_get (thread_p, &scan_range->last_oid, &recdes,
			   &scan_range->scan_cache, PEEK, NULL_CHN);
	  if (scan != S_SUCCESS)
	    {
	      if (scan == S_DOESNT_EXIST)
		{
		  scan = heap_next (thread_p, &scan_range->scan_cache.hfid,
				    &scan_range->scan_cache.class_oid,
				    &scan_range->first_oid, &recdes,
				    &scan_range->scan_cache, PEEK);
		  if (scan != S_SUCCESS)
		    {
		      return scan;
		    }
		}
	      else
		{
		  return scan;
		}
	    }
	}
    }
  else
    {
      /*
       * Scanrange ends with the prior object after the first object in the
       * the previous scanrange
       */
      scan_range->first_oid = scan_range->last_oid;
      scan = heap_next (thread_p, &scan_range->scan_cache.hfid,
			&scan_range->scan_cache.class_oid,
			&scan_range->first_oid, &recdes,
			&scan_range->scan_cache, PEEK);
      if (scan != S_SUCCESS)
	{
	  return scan;
	}
    }


  scan_range->last_oid = scan_range->first_oid;
  if (scan_range->scan_cache.pgptr != NULL
      && (vpid = pgbuf_get_vpid_ptr (scan_range->scan_cache.pgptr)) != NULL
      && (vpid->pageid == scan_range->last_oid.pageid)
      && (vpid->volid == scan_range->last_oid.volid)
      && spage_get_record_type (scan_range->scan_cache.pgptr,
				scan_range->last_oid.slotid) == REC_HOME)
    {
      slotid = scan_range->last_oid.slotid;
      while (true)
	{
	  if (spage_next_record (scan_range->scan_cache.pgptr, &slotid,
				 &recdes, PEEK) != S_SUCCESS
	      || spage_get_record_type (scan_range->scan_cache.pgptr,
					slotid) != REC_HOME)
	    {
	      break;
	    }
	  else
	    {
	      scan_range->last_oid.slotid = slotid;
	    }
	}
    }

  return scan;
}

/*
 * heap_scanrange_to_prior () - Define the prior scanrange
 *   return: SCAN_CODE
 *           (Either of S_SUCCESS, S_END, S_ERROR)
 *   scan_range(in/out): Scanrange
 *   last_oid(in): Desired OID for first element in the scanrange or NULL
 *
 * Note: The range of a scanrange is defined. The scanrange is defined
 * as follows:
 *              a: When last_oid == NULL, the last scanrange object is the
 *                 prior object after the first object in the previous
 *                 scanrange.
 *              b: When last_oid is the same as a NULL_OID, the last object is
 *                 is the last heap object.
 *              c: The last object in the scanrange is the given object.
 *              The first object in the scanrange is either the last object in
 *              the scanrange or the one before the first object which is not
 *              a relocated or multipage object.
 */
SCAN_CODE
heap_scanrange_to_prior (THREAD_ENTRY * thread_p, HEAP_SCANRANGE * scan_range,
			 OID * last_oid)
{
  SCAN_CODE scan;
  RECDES recdes;
  INT16 slotid;

  if (HEAP_DEBUG_ISVALID_SCANRANGE (scan_range) != DISK_VALID)
    {
      return S_ERROR;
    }

  if (last_oid != NULL)
    {
      if (OID_ISNULL (last_oid))
	{
	  /* Scanrange ends at last heap object */
	  scan = heap_last (thread_p, &scan_range->scan_cache.hfid,
			    &scan_range->scan_cache.class_oid,
			    &scan_range->last_oid, &recdes,
			    &scan_range->scan_cache, PEEK);
	  if (scan != S_SUCCESS)
	    {
	      return scan;
	    }
	}
      else
	{
	  /* Scanrange ends with the given object */
	  scan_range->last_oid = *last_oid;
	  scan = heap_get (thread_p, &scan_range->last_oid, &recdes,
			   &scan_range->scan_cache, PEEK, NULL_CHN);
	  if (scan != S_SUCCESS)
	    {
	      if (scan != S_DOESNT_EXIST)
		{
		  scan = heap_prev (thread_p, &scan_range->scan_cache.hfid,
				    &scan_range->scan_cache.class_oid,
				    &scan_range->first_oid, &recdes,
				    &scan_range->scan_cache, PEEK);
		  if (scan != S_SUCCESS)
		    {
		      return scan;
		    }
		}
	    }
	}
    }
  else
    {
      /*
       * Scanrange ends with the prior object after the first object in the
       * the previous scanrange
       */
      scan_range->last_oid = scan_range->first_oid;
      scan = heap_prev (thread_p, &scan_range->scan_cache.hfid,
			&scan_range->scan_cache.class_oid,
			&scan_range->last_oid, &recdes,
			&scan_range->scan_cache, PEEK);
      if (scan != S_SUCCESS)
	{
	  return scan;
	}
    }

  /*
   * Now define the first object for the scanrange. A scanrange range starts
   * when a relocated or multipage object is found or when the last object is
   * the page is found.
   */

  scan_range->first_oid = scan_range->last_oid;
  if (scan_range->scan_cache.pgptr != NULL)
    {
      slotid = scan_range->first_oid.slotid;
      while (true)
	{
	  if (spage_previous_record
	      (scan_range->scan_cache.pgptr, &slotid, &recdes,
	       PEEK) != S_SUCCESS || slotid == HEAP_HEADER_AND_CHAIN_SLOTID
	      || spage_get_record_type (scan_range->scan_cache.pgptr,
					slotid) != REC_HOME)
	    {
	      break;
	    }
	  else
	    {
	      scan_range->first_oid.slotid = slotid;
	    }
	}
    }

  return scan;
}

/*
 * heap_scanrange_next () - Retrieve or peek next object in the scanrange
 *   return: SCAN_CODE
 *           (Either of S_SUCCESS, S_DOESNT_FIT, S_END,
 *                      S_ERROR)
 *   next_oid(in/out): Object identifier of current record.
 *                     Will be set to next available record or NULL_OID when
 *                     there is not one.
 *   recdes(in/out): Pointer to a record descriptor. Will be modified to
 *                   describe the new record.
 *   scan_range(in/out): Scan range ... Cannot be NULL
 *   ispeeking(in): PEEK when the object is peeked,
 *                  COPY when the object is copied
 *
 */
SCAN_CODE
heap_scanrange_next (THREAD_ENTRY * thread_p, OID * next_oid, RECDES * recdes,
		     HEAP_SCANRANGE * scan_range, int ispeeking)
{
  SCAN_CODE scan;

  if (HEAP_DEBUG_ISVALID_SCANRANGE (scan_range) != DISK_VALID)
    {
      return S_ERROR;
    }

  /*
   * If next_oid is less than the first OID in the scanrange.. get the first
   * object
   */

  if (OID_ISNULL (next_oid) || OID_LT (next_oid, &scan_range->first_oid))
    {
      /* Retrieve the first object in the scanrange */
      *next_oid = scan_range->first_oid;
      scan = heap_get (thread_p, next_oid, recdes, &scan_range->scan_cache,
		       ispeeking, NULL_CHN);
      if (scan == S_DOESNT_EXIST)
	{
	  scan = heap_next (thread_p, &scan_range->scan_cache.hfid,
			    &scan_range->scan_cache.class_oid, next_oid,
			    recdes, &scan_range->scan_cache, ispeeking);
	}
      /* Make sure that we did not go overboard */
      if (scan == S_SUCCESS && OID_GT (next_oid, &scan_range->last_oid))
	{
	  OID_SET_NULL (next_oid);
	  scan = S_END;
	}
    }
  else
    {
      /* Make sure that this is not the last OID in the scanrange */
      if (OID_EQ (next_oid, &scan_range->last_oid))
	{
	  OID_SET_NULL (next_oid);
	  scan = S_END;
	}
      else
	{
	  scan = heap_next (thread_p, &scan_range->scan_cache.hfid,
			    &scan_range->scan_cache.class_oid, next_oid,
			    recdes, &scan_range->scan_cache, ispeeking);
	  /* Make sure that we did not go overboard */
	  if (scan == S_SUCCESS && OID_GT (next_oid, &scan_range->last_oid))
	    {
	      OID_SET_NULL (next_oid);
	      scan = S_END;
	    }
	}
    }

  return scan;
}

/*
 * heap_scanrange_prev () - RETRIEVE OR PEEK NEXT OBJECT IN THE SCANRANGE
 *   return:
 * returns/side-effects: SCAN_CODE
 *              (Either of S_SUCCESS, S_DOESNT_FIT, S_END,
 *                         S_ERROR)
 *   prev_oid(in/out): Object identifier of current record.
 *                     Will be set to previous available record or NULL_OID when
 *                     there is not one.
 *   recdes(in/out): Pointer to a record descriptor. Will be modified to
 *                   describe the new record.
 *   scan_range(in/out): Scan range ... Cannot be NULL
 *   ispeeking(in): PEEK when the object is peeked,
 *                  COPY when the object is copied
 *
 */
SCAN_CODE
heap_scanrange_prev (THREAD_ENTRY * thread_p, OID * prev_oid, RECDES * recdes,
		     HEAP_SCANRANGE * scan_range, int ispeeking)
{
  SCAN_CODE scan;

  if (HEAP_DEBUG_ISVALID_SCANRANGE (scan_range) != DISK_VALID)
    {
      return S_ERROR;
    }

  if (OID_ISNULL (prev_oid) || OID_GT (prev_oid, &scan_range->last_oid))
    {
      /* Retrieve the last object in the scanrange */
      *prev_oid = scan_range->last_oid;
      scan = heap_get (thread_p, prev_oid, recdes, &scan_range->scan_cache,
		       ispeeking, NULL_CHN);
      if (scan == S_DOESNT_EXIST)
	{
	  scan = heap_prev (thread_p, &scan_range->scan_cache.hfid,
			    &scan_range->scan_cache.class_oid, prev_oid,
			    recdes, &scan_range->scan_cache, ispeeking);
	}
      /* Make sure that we did not go underboard */
      if (scan == S_SUCCESS && OID_LT (prev_oid, &scan_range->last_oid))
	{
	  OID_SET_NULL (prev_oid);
	  scan = S_END;
	}
    }
  else
    {
      /* Make sure that this is not the first OID in the scanrange */
      if (OID_EQ (prev_oid, &scan_range->first_oid))
	{
	  OID_SET_NULL (prev_oid);
	  scan = S_END;
	}
      else
	{
	  scan = heap_prev (thread_p, &scan_range->scan_cache.hfid,
			    &scan_range->scan_cache.class_oid, prev_oid,
			    recdes, &scan_range->scan_cache, ispeeking);
	  if (scan == S_SUCCESS && OID_LT (prev_oid, &scan_range->last_oid))
	    {
	      OID_SET_NULL (prev_oid);
	      scan = S_END;
	    }
	}
    }

  return scan;
}

/*
 * heap_scanrange_first () - Retrieve or peek first object in the scanrange
 *   return: SCAN_CODE
 *           (Either of S_SUCCESS, S_DOESNT_FIT, S_END,
 *                      S_ERROR)
 *   first_oid(in/out): Object identifier.
 *                      Set to first available record or NULL_OID when there
 *                      is not one.
 *   recdes(in/out): Pointer to a record descriptor. Will be modified to
 *                   describe the new record.
 *   scan_range(in/out): Scan range ... Cannot be NULL
 *   ispeeking(in): PEEK when the object is peeked,
 *                  COPY when the object is copied
 *
 */
SCAN_CODE
heap_scanrange_first (THREAD_ENTRY * thread_p, OID * first_oid,
		      RECDES * recdes, HEAP_SCANRANGE * scan_range,
		      int ispeeking)
{
  SCAN_CODE scan;

  if (HEAP_DEBUG_ISVALID_SCANRANGE (scan_range) != DISK_VALID)
    {
      return S_ERROR;
    }

  /* Retrieve the first object in the scanrange */
  *first_oid = scan_range->first_oid;
  scan = heap_get (thread_p, first_oid, recdes, &scan_range->scan_cache,
		   ispeeking, NULL_CHN);
  if (scan == S_DOESNT_EXIST)
    {
      scan = heap_next (thread_p, &scan_range->scan_cache.hfid,
			&scan_range->scan_cache.class_oid, first_oid,
			recdes, &scan_range->scan_cache, ispeeking);
    }
  /* Make sure that we did not go overboard */
  if (scan == S_SUCCESS && OID_GT (first_oid, &scan_range->last_oid))
    {
      OID_SET_NULL (first_oid);
      scan = S_END;
    }

  return scan;
}

/*
 * heap_scanrange_last () - Retrieve or peek last object in the scanrange
 *   return: SCAN_CODE
 *           (Either of S_SUCCESS, S_DOESNT_FIT, S_END,
 *                      S_ERROR)
 *   last_oid(in/out): Object identifier.
 *                     Set to last available record or NULL_OID when there is
 *                     not one
 *   recdes(in/out): Pointer to a record descriptor. Will be modified to
 *                   describe the new record.
 *   scan_range(in/out): Scan range ... Cannot be NULL
 *   ispeeking(in): PEEK when the object is peeked,
 *                  COPY when the object is copied
 *
 */
SCAN_CODE
heap_scanrange_last (THREAD_ENTRY * thread_p, OID * last_oid, RECDES * recdes,
		     HEAP_SCANRANGE * scan_range, int ispeeking)
{
  SCAN_CODE scan;

  if (HEAP_DEBUG_ISVALID_SCANRANGE (scan_range) != DISK_VALID)
    {
      return S_ERROR;
    }

  /* Retrieve the last object in the scanrange */
  *last_oid = scan_range->last_oid;
  scan = heap_get (thread_p, last_oid, recdes, &scan_range->scan_cache,
		   ispeeking, NULL_CHN);
  if (scan == S_DOESNT_EXIST)
    {
      scan = heap_prev (thread_p, &scan_range->scan_cache.hfid,
			&scan_range->scan_cache.class_oid, last_oid, recdes,
			&scan_range->scan_cache, ispeeking);
    }
  /* Make sure that we did not go underboard */
  if (scan == S_SUCCESS && OID_LT (last_oid, &scan_range->last_oid))
    {
      OID_SET_NULL (last_oid);
      scan = S_END;
    }

  return scan;
}

/*
 * heap_does_exist () - Does object exist?
 *   return: true/false
 *   class_oid(in): Class identifier of object or NULL
 *   oid(in): Object identifier
 *
 * Note: Check if the object associated with the given OID exist.
 * If the class of the object does not exist, the object does not
 * exist either. If the class is not given or a NULL_OID is
 * passed, the function finds the class oid.
 */
bool
heap_does_exist (THREAD_ENTRY * thread_p, OID * class_oid, const OID * oid)
{
  VPID vpid;
  OID tmp_oid;
  PAGE_PTR pgptr = NULL;
  bool doesexist = true;
  INT16 rectype;

  if (HEAP_ISVALID_OID (oid) != DISK_VALID)
    {
      return false;
    }

  /*
   * If the class is not NULL and it is different from the Rootclass,
   * make sure that it exist. Rootclass always exist.. not need to check
   * for it
   */
  if (class_oid != NULL && !OID_EQ (class_oid, oid_Root_class_oid)
      && HEAP_ISVALID_OID (class_oid) != DISK_VALID)
    {
      return false;
    }

  while (doesexist)
    {
      if (oid->slotid == HEAP_HEADER_AND_CHAIN_SLOTID || oid->slotid < 0
	  || oid->pageid < 0 || oid->volid < 0)
	{
	  return false;
	}

      vpid.volid = oid->volid;
      vpid.pageid = oid->pageid;

      /* Fetch the page where the record is stored */

      pgptr = heap_scan_pb_lock_and_fetch (thread_p, &vpid, OLD_PAGE, S_LOCK,
					   NULL);
      if (pgptr == NULL)
	{
	  if (er_errid () == ER_PB_BAD_PAGEID)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_HEAP_UNKNOWN_OBJECT, 3, oid->volid, oid->pageid,
		      oid->slotid);
	    }

	  /* something went wrong, return */
	  return false;
	}

      doesexist = spage_is_slot_exist (pgptr, oid->slotid);
      rectype = spage_get_record_type (pgptr, oid->slotid);
      pgbuf_unfix_and_init (thread_p, pgptr);

      /*
       * Check the class
       */

      if (doesexist && rectype != REC_ASSIGN_ADDRESS)
	{
	  if (class_oid == NULL)
	    {
	      class_oid = &tmp_oid;
	      OID_SET_NULL (class_oid);
	    }

	  if (OID_ISNULL (class_oid))
	    {
	      /*
	       * Caller does not know the class of the object. Get the class
	       * identifier from disk
	       */
	      if (heap_get_class_oid (thread_p, class_oid, oid) == NULL)
		{
		  doesexist = false;
		  break;
		}
	    }

	  /* If doesexist is true, then check its class */
	  if (!OID_IS_ROOTOID (class_oid))
	    {
	      /*
	       * Make sure that the class exist too. Loop with this
	       */
	      oid = class_oid;
	      class_oid = oid_Root_class_oid;
	    }
	  else
	    {
	      break;
	    }
	}
      else
	{
	  break;
	}
    }

  return doesexist;
}

/*
 * heap_get_num_objects () - Count the number of objects
 *   return: number of records or -1 in case of an error
 *   hfid(in): Object heap file identifier
 *   npages(in):
 *   nobjs(in):
 *   avg_length(in):
 *
 * Note: Count the number of objects stored on the given heap.
 * This function is expensive since all pages of the heap are
 * fetched to find the number of objects.
 */
int
heap_get_num_objects (THREAD_ENTRY * thread_p, const HFID * hfid, int *npages,
		      int *nobjs, int *avg_length)
{
  VPID vpid;			/* Page-volume identifier            */
  LOG_DATA_ADDR addr_hdr;	/* Address of logging data           */
  RECDES hdr_recdes;		/* Record descriptor to point to space
				 * statistics */
  HEAP_HDR_STATS *heap_hdr;	/* Heap header                         */

  /*
   * Get the heap header in exclusive mode and call the synchronization to
   * update the statistics of the heap. The number of record/objects is
   * updated.
   */

  vpid.volid = hfid->vfid.volid;
  vpid.pageid = hfid->hpgid;

  addr_hdr.vfid = &hfid->vfid;
  addr_hdr.offset = HEAP_HEADER_AND_CHAIN_SLOTID;

  addr_hdr.pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			      PGBUF_UNCONDITIONAL_LATCH);
  if (addr_hdr.pgptr == NULL)
    {
      /* something went wrong. Unable to fetch header page */
      return ER_FAILED;
    }

  if (spage_get_record (addr_hdr.pgptr, HEAP_HEADER_AND_CHAIN_SLOTID,
			&hdr_recdes, PEEK) != S_SUCCESS)
    {
      pgbuf_unfix_and_init (thread_p, addr_hdr.pgptr);
      return ER_FAILED;
    }

  heap_hdr = (HEAP_HDR_STATS *) hdr_recdes.data;
  if (heap_stats_sync_bestspace (thread_p, hfid, heap_hdr,
				 pgbuf_get_vpid_ptr (addr_hdr.pgptr), true,
				 true) < 0)
    {
      pgbuf_unfix_and_init (thread_p, addr_hdr.pgptr);
      return ER_FAILED;
    }
  *npages = heap_hdr->estimates.num_pages;
  *nobjs = heap_hdr->estimates.num_recs;
  if (*nobjs > 0)
    {
      *avg_length = (int) ((heap_hdr->estimates.recs_sumlen /
			    (float) *nobjs) + 0.9);
    }
  else
    {
      *avg_length = 0;
    }

  log_skip_logging (thread_p, &addr_hdr);
  pgbuf_set_dirty (thread_p, addr_hdr.pgptr, FREE);
  addr_hdr.pgptr = NULL;

  return *nobjs;
}

/*
 * heap_estimate () - Estimate the number of pages, objects, average length
 *   return: number of pages estimated or -1 in case of an error
 *   hfid(in): Object heap file identifier
 *   npages(in):
 *   nobjs(in):
 *   avg_length(in):
 *
 * Note: Estimate the number of pages, objects, and average length of objects.
 */
int
heap_estimate (THREAD_ENTRY * thread_p, const HFID * hfid, int *npages,
	       int *nobjs, int *avg_length)
{
  VPID vpid;			/* Page-volume identifier            */
  PAGE_PTR hdr_pgptr = NULL;	/* Page pointer                      */
  RECDES hdr_recdes;		/* Record descriptor to point to space
				 * statistics
				 */
  HEAP_HDR_STATS *heap_hdr;	/* Heap header                         */

  /*
   * Get the heap header in shared mode since it is an estimation of the
   * number of objects.
   */

  vpid.volid = hfid->vfid.volid;
  vpid.pageid = hfid->hpgid;

  hdr_pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ,
			 PGBUF_UNCONDITIONAL_LATCH);
  if (hdr_pgptr == NULL)
    {
      /* something went wrong. Unable to fetch header page */
      return ER_FAILED;
    }

  if (spage_get_record (hdr_pgptr, HEAP_HEADER_AND_CHAIN_SLOTID, &hdr_recdes,
			PEEK) != S_SUCCESS)
    {
      pgbuf_unfix_and_init (thread_p, hdr_pgptr);
      return ER_FAILED;
    }

  heap_hdr = (HEAP_HDR_STATS *) hdr_recdes.data;
  *npages = heap_hdr->estimates.num_pages;
  *nobjs = heap_hdr->estimates.num_recs;
  if (*nobjs > 0)
    {
      *avg_length = (int) ((heap_hdr->estimates.recs_sumlen /
			    (float) *nobjs) + 0.9);
    }
  else
    {
      *avg_length = 0;
    }

  pgbuf_unfix_and_init (thread_p, hdr_pgptr);

  return *npages;
}

/*
 * heap_estimate_num_objects () - Estimate the number of objects
 *   return: number of records estimated or -1 in case of an error
 *   hfid(in): Object heap file identifier
 *
 * Note: Estimate the number of objects stored on the given heap.
 */
int
heap_estimate_num_objects (THREAD_ENTRY * thread_p, const HFID * hfid)
{
  int ignore_npages;
  int ignore_avg_reclen;
  int nobjs;

  if (heap_estimate (thread_p, hfid, &ignore_npages, &nobjs,
		     &ignore_avg_reclen) == -1)
    {
      return ER_FAILED;
    }

  return nobjs;
}

/*
 * heap_estimate_avg_length () - Estimate the average length of records
 *   return: avg length
 *   hfid(in): Object heap file identifier
 *
 * Note: Estimate the avergae length of the objects stored on the heap.
 * This function is mainly used when we are creating the OID of
 * an object of which we do not know its length. Mainly for
 * loaddb during forward references to other objects.
 */
static int
heap_estimate_avg_length (THREAD_ENTRY * thread_p, const HFID * hfid)
{
  int ignore_npages;
  int ignore_nobjs;
  int avg_reclen;

  if (heap_estimate (thread_p, hfid, &ignore_npages, &ignore_nobjs,
		     &avg_reclen) == -1)
    {
      return ER_FAILED;
    }

  return avg_reclen;
}

/*
 * heap_get_capacity () - Find space consumed by heap
 *   return: NO_ERROR
 *   hfid(in): Object heap file identifier
 *   num_recs(in/out): Total Number of objects
 *   num_recs_relocated(in/out):
 *   num_recs_inovf(in/out):
 *   num_pages(in/out): Total number of heap pages
 *   avg_freespace(in/out): Average free space per page
 *   avg_freespace_nolast(in/out): Average free space per page without taking in
 *                                 consideration last page
 *   avg_reclength(in/out): Average object length
 *   avg_overhead(in/out): Average overhead per page
 *
 * Note: Find the current storage facts/capacity for given heap.
 */
static int
heap_get_capacity (THREAD_ENTRY * thread_p, const HFID * hfid, int *num_recs,
		   int *num_recs_relocated, int *num_recs_inovf,
		   int *num_pages, int *avg_freespace,
		   int *avg_freespace_nolast, int *avg_reclength,
		   int *avg_overhead)
{
  VPID vpid;			/* Page-volume identifier            */
  PAGE_PTR pgptr = NULL;	/* Page pointer to header page       */
  RECDES recdes;		/* Header record descriptor          */
  INT16 slotid;			/* Slot of one object                */
  OID *ovf_oid;
  int last_freespace;
  int ovf_len;
  int ovf_num_pages;
  int ovf_free_space;
  int ovf_overhead;
  int j;
  INT16 type = REC_UNKNOWN;
  int ret = NO_ERROR;

  *num_recs = 0;
  *num_pages = 0;
  *avg_freespace = 0;
  *avg_reclength = 0;
  *avg_overhead = 0;
  *num_recs_relocated = 0;
  *num_recs_inovf = 0;
  last_freespace = 0;

  vpid.volid = hfid->vfid.volid;
  vpid.pageid = hfid->hpgid;

  while (!VPID_ISNULL (&vpid))
    {
      pgptr = heap_scan_pb_lock_and_fetch (thread_p, &vpid, OLD_PAGE, S_LOCK,
					   NULL);
      if (pgptr == NULL)
	{
	  /* something went wrong, return error */
	  goto exit_on_error;
	}

      slotid = -1;
      j = spage_number_of_records (pgptr);

      last_freespace = spage_get_free_space (thread_p, pgptr);

      *num_pages += 1;
      *avg_freespace += last_freespace;
      *avg_overhead += j * spage_slot_size ();

      while ((j--) > 0)
	{
	  if (spage_next_record (pgptr, &slotid, &recdes, PEEK) == S_SUCCESS)
	    {
	      if (slotid != HEAP_HEADER_AND_CHAIN_SLOTID)
		{
		  type = spage_get_record_type (pgptr, slotid);
		  switch (type)
		    {
		    case REC_RELOCATION:
		      *num_recs_relocated += 1;
		      *avg_overhead += spage_get_record_length (pgptr,
								slotid);
		      break;
		    case REC_ASSIGN_ADDRESS:
		    case REC_HOME:
		    case REC_NEWHOME:
		      /*
		       * Note: for newhome (relocated), we are including the length
		       *       and number of records. In the relocation record (above)
		       *       we are just adding the overhead and number of
		       *       reclocation records.
		       *       for assign address, we assume the given size.
		       */
		      *num_recs += 1;
		      *avg_reclength += spage_get_record_length (pgptr,
								 slotid);
		      break;
		    case REC_BIGONE:
		      *num_recs += 1;
		      *num_recs_inovf += 1;
		      *avg_overhead += spage_get_record_length (pgptr,
								slotid);

		      ovf_oid = (OID *) recdes.data;
		      if (heap_ovf_get_capacity (thread_p, ovf_oid, &ovf_len,
						 &ovf_num_pages,
						 &ovf_overhead,
						 &ovf_free_space) == NO_ERROR)
			{
			  *avg_reclength += ovf_len;
			  *num_pages += ovf_num_pages;
			  *avg_freespace += ovf_free_space;
			  *avg_overhead += ovf_overhead;
			}
		      break;
		    case REC_MARKDELETED:
		      /*
		       * TODO Find out and document here why this is added to
		       * the overhead. The record has been deleted so its
		       * length should no longer have any meaning. Perhaps
		       * the length of the slot should have been added instead?
		       */
		      *avg_overhead += spage_get_record_length (pgptr,
								slotid);
		      break;
		    case REC_DELETED_WILL_REUSE:
		    default:
		      break;
		    }
		}
	    }
	}
      (void) heap_vpid_next (hfid, pgptr, &vpid);
      pgbuf_unfix_and_init (thread_p, pgptr);
    }

  if (*num_pages > 0)
    {
      /*
       * Don't take in consideration the last page for free space
       * considerations since the average free space will be contaminated.
       */
      *avg_freespace_nolast = ((*num_pages > 1)
			       ? ((*avg_freespace - last_freespace) /
				  (*num_pages - 1)) : 0);
      *avg_freespace = *avg_freespace / *num_pages;
      *avg_overhead = *avg_overhead / *num_pages;
    }

  if (*num_recs != 0)
    {
      *avg_reclength = *avg_reclength / *num_recs;
    }

  return ret;

exit_on_error:

  return (ret == NO_ERROR
	  && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
}

/*
 * heap_get_class_oid () - Find class oid of given instance
 *   return: OID *(class_oid on success and NULL on failure)
 *   class_oid(out): The Class oid of the instance
 *   oid(in): The Object identifier of the instance
 *
 * Note: Find the class identifier of the given instance.
 */
OID *
heap_get_class_oid (THREAD_ENTRY * thread_p, OID * class_oid, const OID * oid)
{
  RECDES recdes;
  HEAP_SCANCACHE scan_cache;
  DISK_ISVALID oid_valid;

  if (class_oid == NULL)
    {
      assert (false);
      return NULL;
    }

  heap_scancache_quick_start (&scan_cache);
  if (heap_get_with_class_oid (thread_p, class_oid, oid, &recdes,
			       &scan_cache, PEEK) != S_SUCCESS)
    {
      OID_SET_NULL (class_oid);
      class_oid = NULL;
    }
  else
    {
      oid_valid = HEAP_ISVALID_OID (class_oid);
      if (oid_valid != DISK_VALID)
	{
	  if (oid_valid != DISK_ERROR)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_HEAP_UNKNOWN_OBJECT, 3, oid->volid, oid->pageid,
		      oid->slotid);
	    }
	  OID_SET_NULL (class_oid);
	  class_oid = NULL;
	}
    }

  heap_scancache_end (thread_p, &scan_cache);

  return class_oid;
}

/*
 * heap_get_class_name () - Find classname when oid is a class
 *   return: Classname or NULL. The classname space must be
 *           released by the caller.
 *   class_oid(in): The Class Object identifier
 *
 * Note: Find the name of the given class identifier. If the passed OID
 * is not a class, it return NULL.
 *
 * Note: Classname pointer must be released by the caller using free_and_init
 */
char *
heap_get_class_name (THREAD_ENTRY * thread_p, const OID * class_oid)
{
  return heap_get_class_name_alloc_if_diff (thread_p, class_oid, NULL);
}

/*
 * heap_get_class_name_alloc_if_diff () - Get the name of given class
 *                               name is malloc when different than given name
 *   return: guess_classname when it is the real name. Don't need to free.
 *           malloc classname when different from guess_classname.
 *           Must be free by caller (free_and_init)
 *           NULL some kind of error
 *   class_oid(in): The Class Object identifier
 *   guess_classname(in): Guess name of class
 *
 * Note: Find the name of the given class identifier. If the name is
 * the same as the guessed name, the guessed name is returned.
 * Otherwise, an allocated area with the name of the class is
 * returned. If an error is found or the passed OID is not a
 * class, NULL is returned.
 */
char *
heap_get_class_name_alloc_if_diff (THREAD_ENTRY * thread_p,
				   const OID * class_oid,
				   char *guess_classname)
{
  char *classname = NULL;
  char *copy_classname = NULL;
  RECDES recdes;
  HEAP_SCANCACHE scan_cache;
  OID root_oid;

  heap_scancache_quick_start (&scan_cache);
  if (heap_get_with_class_oid (thread_p, &root_oid, class_oid, &recdes,
			       &scan_cache, PEEK) == S_SUCCESS)
    {
      /* Make sure that this is a class */
      if (oid_is_root (&root_oid))
	{
	  classname = or_class_name (&recdes);
	  if (guess_classname == NULL
	      || strcmp (guess_classname, classname) != 0)
	    {
	      /*
	       * The names are different.. return a copy that must be freed.
	       */
	      copy_classname = strdup (classname);
	    }
	  else
	    {
	      /*
	       * The classnames are identical
	       */
	      copy_classname = guess_classname;
	    }
	}
    }
  else
    {
      if (er_errid () == ER_HEAP_NODATA_NEWADDRESS)
	{
	  er_clear ();		/* clear ER_HEAP_NODATA_NEWADDRESS */
	}
    }

  heap_scancache_end (thread_p, &scan_cache);

  return copy_classname;
}

/*
 * heap_get_class_name_of_instance () - Find classname of given instance
 *   return: Classname or NULL. The classname space must be
 *           released by the caller.
 *   inst_oid(in): The instance object identifier
 *
 * Note: Find the class name of the class of given instance identifier.
 *
 * Note: Classname pointer must be released by the caller using free_and_init
 */
char *
heap_get_class_name_of_instance (THREAD_ENTRY * thread_p,
				 const OID * inst_oid)
{
  char *classname = NULL;
  char *copy_classname = NULL;
  RECDES recdes;
  HEAP_SCANCACHE scan_cache;
  OID class_oid;

  heap_scancache_quick_start (&scan_cache);
  if (heap_get_with_class_oid (thread_p, &class_oid, inst_oid, &recdes,
			       &scan_cache, PEEK) == S_SUCCESS)
    {
      if (heap_get (thread_p, &class_oid, &recdes, &scan_cache, PEEK,
		    NULL_CHN) == S_SUCCESS)
	{
	  classname = or_class_name (&recdes);
	  copy_classname = (char *) malloc (strlen (classname) + 1);
	  if (copy_classname == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_OUT_OF_VIRTUAL_MEMORY, 1, strlen (classname) + 1);
	    }
	  else
	    {
	      strcpy (copy_classname, classname);
	    }
	}
    }

  heap_scancache_end (thread_p, &scan_cache);

  return copy_classname;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * heap_get_class_name_with_is_class () - Find if object is a class.
 * if a class, returns its name, otherwise, get the name of its class
 *   return: Classname or NULL. The classname space must be
 *           released by the caller.
 *   oid(in): The Object identifier
 *   isclass(in/out): Set to true is object is a class, otherwise is set to
 *                    false
 *
 * Note: Find if the object associated with given oid is a class.
 * If the object is a class, returns its name, otherwise, returns
 * the name of its class.
 *
 * If the object does not exist or there is another error, NULL
 * is returned as the classname.
 *
 * Note: Classname pointer must be released by the caller using free_and_init
 */
char *
heap_get_class_name_with_is_class (THREAD_ENTRY * thread_p, const OID * oid,
				   int *isclass)
{
  char *classname = NULL;
  char *copy_classname = NULL;
  RECDES recdes;
  HEAP_SCANCACHE scan_cache;
  OID class_oid;

  *isclass = false;

  heap_scancache_quick_start (&scan_cache);
  if (heap_get_with_class_oid (thread_p, &class_oid, oid, &recdes,
			       &scan_cache, PEEK) == S_SUCCESS)
    {
      /*
       * If oid is a class, get its name, otherwise, get the name of its class
       */
      *isclass = OID_IS_ROOTOID (&class_oid);
      if (heap_get (thread_p, ((*isclass == true) ? oid : &class_oid),
		    &recdes, &scan_cache, PEEK, NULL_CHN) == S_SUCCESS)
	{
	  classname = or_class_name (&recdes);
	  copy_classname = (char *) malloc (strlen (classname) + 1);
	  if (copy_classname == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_OUT_OF_VIRTUAL_MEMORY, 1, strlen (classname) + 1);
	    }
	  else
	    {
	      strcpy (copy_classname, classname);
	    }
	}
    }

  heap_scancache_end (thread_p, &scan_cache);

  return copy_classname;
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * heap_attrinfo_start () - Initialize an attribute information structure
 *   return: NO_ERROR
 *   class_oid(in): The class identifier of the instances where values
 *                  attributes values are going to be read.
 *   requested_num_attrs(in): Number of requested attributes
 *                            If <=0 are given, it means interested on ALL.
 *   attrids(in): Array of requested attributes
 *   attr_info(in/out): The attribute information structure
 *
 * Note: Initialize an attribute information structure, so that values
 * of instances can be retrieved based on the desired attributes.
 * If the requested number of attributes is less than zero,
 * all attributes will be assumed instead. In this case
 * the attrids array should be NULL.
 *
 * The attrinfo structure is an structure where values of
 * instances can be read. For example an object is retrieved,
 * then some of its attributes are convereted to dbvalues and
 * placed in this structure.
 *
 * Note: The caller must call heap_attrinfo_end after he is done with
 * attribute information.
 */
int
heap_attrinfo_start (THREAD_ENTRY * thread_p, const OID * class_oid,
		     int requested_num_attrs, const ATTR_ID * attrids,
		     HEAP_CACHE_ATTRINFO * attr_info)
{
  HEAP_ATTRVALUE *value;	/* Disk value Attr info for a particular attr */
  int getall;			/* Want all attribute values */
  int i;
  int ret = NO_ERROR;

  if (requested_num_attrs == 0)
    {
      /* initialize the attrinfo cache and return, there is nothing else to do */
      (void) memset (attr_info, '\0', sizeof (HEAP_CACHE_ATTRINFO));

      /* now set the num_values to -1 which indicates that this is an
       * empty HEAP_CACHE_ATTRINFO and shouldn't be operated on.
       */
      attr_info->num_values = -1;
      return NO_ERROR;
    }

  if (requested_num_attrs < 0)
    {
      getall = true;
    }
  else
    {
      getall = false;
    }

  /*
   * initialize attribute information
   *
   */

  attr_info->class_oid = *class_oid;
  attr_info->last_cacheindex = -1;
  attr_info->read_cacheindex = -1;

  attr_info->last_classrepr = NULL;
  attr_info->read_classrepr = NULL;

  OID_SET_NULL (&attr_info->inst_oid);
  attr_info->inst_chn = NULL_CHN;
  attr_info->values = NULL;
  attr_info->num_values = -1;	/* initialize attr_info */

  /*
   * Find the most recent representation of the instances of the class, and
   * cache the structure that describe the attributes of this representation.
   * At the same time find the default values of attributes, the shared
   * attribute values and the class attribute values.
   */

  attr_info->last_classrepr = heap_classrepr_get (thread_p,
						  &attr_info->class_oid, NULL,
						  0,
						  &attr_info->last_cacheindex,
						  true);
  if (attr_info->last_classrepr == NULL)
    {
      goto exit_on_error;
    }

  /*
   * If the requested attributes is < 0, get all attributes of the last
   * representation.
   */

  if (requested_num_attrs < 0)
    {
      requested_num_attrs = attr_info->last_classrepr->n_attributes;
    }
  else if (requested_num_attrs >
	   (attr_info->last_classrepr->n_attributes +
	    attr_info->last_classrepr->n_shared_attrs +
	    attr_info->last_classrepr->n_class_attrs))
    {
      fprintf (stdout, " XXX There are not that many attributes."
	       " Num_attrs = %d, Num_requested_attrs = %d\n",
	       attr_info->last_classrepr->n_attributes, requested_num_attrs);
      requested_num_attrs = attr_info->last_classrepr->n_attributes +
	attr_info->last_classrepr->n_shared_attrs +
	attr_info->last_classrepr->n_class_attrs;
    }

  if (requested_num_attrs > 0)
    {
      attr_info->values =
	(HEAP_ATTRVALUE *) db_private_alloc (thread_p,
					     requested_num_attrs *
					     sizeof (*(attr_info->values)));
      if (attr_info->values == NULL)
	{
	  goto exit_on_error;
	}
    }
  else
    {
      attr_info->values = NULL;
    }

  attr_info->num_values = requested_num_attrs;

  /*
   * Set the attribute identifier of the desired attributes in the value
   * attribute information, and indicates that the current value is
   * unitialized. That is, it has not been read, set or whatever.
   */

  for (i = 0; i < attr_info->num_values; i++)
    {
      value = &attr_info->values[i];
      if (getall == true)
	{
	  value->attrid = -1;
	}
      else
	{
	  value->attrid = *attrids++;
	}
      value->state = HEAP_UNINIT_ATTRVALUE;
      value->do_increment = 0;
      value->last_attrepr = NULL;
      value->read_attrepr = NULL;
    }

  /*
   * Make last information to be recached for each individual attribute
   * value. Needed for WRITE and Default values
   */

  if (heap_attrinfo_recache_attrepr (attr_info, true) != NO_ERROR)
    {
      goto exit_on_error;
    }

  return ret;

exit_on_error:

  heap_attrinfo_end (thread_p, attr_info);

  return (ret == NO_ERROR
	  && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
}

#if 0				/* TODO: remove unused */
/*
 * heap_moreattr_attrinfo () - Add another attribute to the attribute information
 *                           cache
 *   return: NO_ERROR
 *   attrid(in): The information of the attribute that will be needed
 *   attr_info(in/out): The attribute information structure
 *
 * Note: The given attribute is included as part of the reading or
 * transformation process.
 */
static int
heap_moreattr_attrinfo (int attrid, HEAP_CACHE_ATTRINFO * attr_info)
{
  HEAP_ATTRVALUE *new_values;	/* The new value attribute array                */
  HEAP_ATTRVALUE *value;	/* Disk value Attr info for a particular attr   */
  int i;
  int ret = NO_ERROR;

  /*
   * If we get an empty HEAP_CACHE_ATTRINFO, this is an error.  We can
   * not add more attributes to an improperly initialized HEAP_CACHE_ATTRINFO
   * structure.
   */
  if (attr_info->num_values == -1)
    {
      return ER_FAILED;
    }

  /*
   * Make sure that the attribute is not already included
   */
  for (i = 0; i < attr_info->num_values; i++)
    {
      value = &attr_info->values[i];
      if (value != NULL && value->attrid == attrid)
	{
	  return NO_ERROR;
	}
    }

  /*
   * Resize the value attribute array and set the attribute identifier as
   * as part of the desired attribute list
   */
  i = (attr_info->num_values + 1) * sizeof (*(attr_info->values));

  new_values =
    (HEAP_ATTRVALUE *) db_private_realloc (NULL, attr_info->values, i);
  if (new_values == NULL)
    {
      goto exit_on_error;
    }

  attr_info->values = new_values;

  value = &attr_info->values[attr_info->num_values];
  value->attrid = attrid;
  value->state = HEAP_UNINIT_ATTRVALUE;
  value->last_attrepr = NULL;
  value->read_attrepr = NULL;
  attr_info->num_values++;

  /*
   * Recache attribute representation and get default value specifications
   * for new attribute. The default values are located on the last
   * representation
   */

  if (heap_attrinfo_recache_attrepr (attr_info, true) != NO_ERROR ||
      db_value_domain_init (&value->dbvalue, value->read_attrepr->type,
			    value->read_attrepr->domain->precision,
			    value->read_attrepr->domain->scale) != NO_ERROR)
    {
      attr_info->num_values--;
      value->attrid = -1;
      goto exit_on_error;
    }

end:

  return ret;

exit_on_error:

  if (ret == NO_ERROR)
    {
      ret = er_errid ();
      if (ret == NO_ERROR)
	{
	  ret = ER_FAILED;
	}
    }
  goto end;
}
#endif

/*
 * heap_attrinfo_recache_attrepr () - Recache attribute information for given attrinfo for
 *                     each attribute value
 *   return: NO_ERROR
 *   attr_info(in/out): The attribute information structure
 *   islast_reset(in): Are we resetting information for last representation.
 *
 * Note: Recache the attribute information for given representation
 * identifier of the class in attr_info for each attribute value.
 * That is, set each attribute information to point to disk
 * related attribute information for given representation
 * identifier.
 * When we are resetting information for last representation,
 * attribute values are also initialized.
 */

static int
heap_attrinfo_recache_attrepr (HEAP_CACHE_ATTRINFO * attr_info,
			       int islast_reset)
{
  HEAP_ATTRVALUE *value;	/* Disk value Attr info for a particular attr */
  int num_found_attrs;		/* Num of found attributes                 */
  int srch_num_attrs;		/* Num of attributes that can be searched  */
  int srch_num_shared;		/* Num of shared attrs that can be searched */
  int srch_num_class;		/* Num of class attrs that can be searched */
  OR_ATTRIBUTE *search_attrepr;	/* Information for disk attribute          */
  int i, curr_attr;
  bool isattr_found;
  int ret = NO_ERROR;

  /*
   * Initialize the value domain for dbvalues of all desired attributes
   */
  if (islast_reset == true)
    {
      srch_num_attrs = attr_info->last_classrepr->n_attributes;
    }
  else
    {
      srch_num_attrs = attr_info->read_classrepr->n_attributes;
    }

  /* shared and class attributes must always use the latest representation */
  srch_num_shared = attr_info->last_classrepr->n_shared_attrs;
  srch_num_class = attr_info->last_classrepr->n_class_attrs;

  for (num_found_attrs = 0, curr_attr = 0;
       curr_attr < attr_info->num_values; curr_attr++)
    {
      /*
       * Go over the list of attributes (instance, shared, and class attrs)
       * until the desired attribute is found
       */
      isattr_found = false;
      if (islast_reset == true)
	{
	  search_attrepr = attr_info->last_classrepr->attributes;
	}
      else
	{
	  search_attrepr = attr_info->read_classrepr->attributes;
	}

      value = &attr_info->values[curr_attr];

      if (value->attrid == -1)
	{
	  /* Case that we want all attributes */
	  value->attrid = search_attrepr[curr_attr].id;
	}

      for (i = 0;
	   isattr_found == false && i < srch_num_attrs; i++, search_attrepr++)
	{
	  /*
	   * Is this a desired instance attribute?
	   */
	  if (value->attrid == search_attrepr->id)
	    {
	      /*
	       * Found it.
	       * Initialize the attribute value information
	       */
	      isattr_found = true;
	      value->attr_type = HEAP_INSTANCE_ATTR;
	      if (islast_reset == true)
		{
		  value->last_attrepr = search_attrepr;
		  /*
		   * The server does not work with DB_TYPE_OBJECT but DB_TYPE_OID
		   */
		  if (value->last_attrepr->type == DB_TYPE_OBJECT)
		    {
		      value->last_attrepr->type = DB_TYPE_OID;
		    }

		  if (value->state == HEAP_UNINIT_ATTRVALUE)
		    {
		      db_value_domain_init (&value->dbvalue,
					    value->last_attrepr->type,
					    value->last_attrepr->domain->
					    precision,
					    value->last_attrepr->domain->
					    scale);
		    }
		}
	      else
		{
		  value->read_attrepr = search_attrepr;
		  /*
		   * The server does not work with DB_TYPE_OBJECT but DB_TYPE_OID
		   */
		  if (value->read_attrepr->type == DB_TYPE_OBJECT)
		    {
		      value->read_attrepr->type = DB_TYPE_OID;
		    }
		}

	      num_found_attrs++;
	    }
	}

      /*
       * if the desired attribute was not found in the instance attributes,
       * look for it in the shared attributes.  We always use the last_repr
       * for shared attributes.
       */

      for (i = 0, search_attrepr = attr_info->last_classrepr->shared_attrs;
	   isattr_found == false && i < srch_num_shared;
	   i++, search_attrepr++)
	{
	  /*
	   * Is this a desired shared attribute?
	   */
	  if (value->attrid == search_attrepr->id)
	    {
	      /*
	       * Found it.
	       * Initialize the attribute value information
	       */
	      isattr_found = true;
	      value->attr_type = HEAP_SHARED_ATTR;
	      value->last_attrepr = search_attrepr;
	      /*
	       * The server does not work with DB_TYPE_OBJECT but DB_TYPE_OID
	       */
	      if (value->last_attrepr->type == DB_TYPE_OBJECT)
		{
		  value->last_attrepr->type = DB_TYPE_OID;
		}

	      if (value->state == HEAP_UNINIT_ATTRVALUE)
		{
		  db_value_domain_init (&value->dbvalue,
					value->last_attrepr->type,
					value->last_attrepr->domain->
					precision,
					value->last_attrepr->domain->scale);
		}
	      num_found_attrs++;
	    }
	}

      /*
       * if the desired attribute was not found in the instance/shared atttrs,
       * look for it in the class attributes.  We always use the last_repr
       * for class attributes.
       */

      for (i = 0, search_attrepr = attr_info->last_classrepr->class_attrs;
	   isattr_found == false && i < srch_num_class; i++, search_attrepr++)
	{
	  /*
	   * Is this a desired class attribute?
	   */

	  if (value->attrid == search_attrepr->id)
	    {
	      /*
	       * Found it.
	       * Initialize the attribute value information
	       */
	      isattr_found = true;
	      value->attr_type = HEAP_CLASS_ATTR;
	      if (islast_reset == true)
		{
		  value->last_attrepr = search_attrepr;
		}
	      else
		{
		  value->read_attrepr = search_attrepr;
		}
	      /*
	       * The server does not work with DB_TYPE_OBJECT but DB_TYPE_OID
	       */
	      if (value->last_attrepr->type == DB_TYPE_OBJECT)
		{
		  value->last_attrepr->type = DB_TYPE_OID;
		}

	      if (value->state == HEAP_UNINIT_ATTRVALUE)
		{
		  db_value_domain_init (&value->dbvalue,
					value->last_attrepr->type,
					value->last_attrepr->domain->
					precision,
					value->last_attrepr->domain->scale);
		}
	      num_found_attrs++;
	    }
	}
    }

  if (num_found_attrs != attr_info->num_values && islast_reset == true)
    {
      ret = ER_HEAP_UNKNOWN_ATTRS;
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ret, 1,
	      attr_info->num_values - num_found_attrs);
      goto exit_on_error;
    }

  return ret;

exit_on_error:

  return (ret == NO_ERROR
	  && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
}

/*
 * heap_attrinfo_recache () - Recache attribute information for given attrinfo
 *   return: NO_ERROR
 *   reprid(in): Cache this class representation
 *   attr_info(in/out): The attribute information structure
 *
 * Note: Recache the attribute information for given representation
 * identifier of the class in attr_info. That is, set each
 * attribute information to point to disk related attribute
 * information for given representation identifier.
 */
static int
heap_attrinfo_recache (THREAD_ENTRY * thread_p, REPR_ID reprid,
		       HEAP_CACHE_ATTRINFO * attr_info)
{
  HEAP_ATTRVALUE *value;	/* Disk value Attr info for a particular attr */
  int i;
  int ret = NO_ERROR;

  /*
   * If we do not need to cache anything (case of only clear values and
   * disk repr structure).. return
   */

  if (attr_info->read_classrepr != NULL)
    {
      if (attr_info->read_classrepr->id == reprid)
	{
	  return NO_ERROR;
	}

      /*
       * Do we need to free the current cached disk representation ?
       */
      if (attr_info->read_classrepr != attr_info->last_classrepr)
	{
	  ret = heap_classrepr_free (attr_info->read_classrepr,
				     &attr_info->read_cacheindex);
	  if (ret != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	}
      attr_info->read_classrepr = NULL;
    }

  if (reprid == NULL_REPRID)
    {
      return NO_ERROR;
    }

  if (reprid == attr_info->last_classrepr->id)
    {
      /*
       * Take a short cut
       */
      if (attr_info->values != NULL)
	{
	  for (i = 0; i < attr_info->num_values; i++)
	    {
	      value = &attr_info->values[i];
	      value->read_attrepr = value->last_attrepr;
	    }
	}
      attr_info->read_classrepr = attr_info->last_classrepr;
      attr_info->read_cacheindex = -1;	/* Don't need to free this one */
      return NO_ERROR;
    }

  /*
   * Cache the desired class representation information
   */
  if (attr_info->values != NULL)
    {
      for (i = 0; i < attr_info->num_values; i++)
	{
	  value = &attr_info->values[i];
	  value->read_attrepr = NULL;
	}
    }
  attr_info->read_classrepr =
    heap_classrepr_get (thread_p, &attr_info->class_oid, NULL, reprid,
			&attr_info->read_cacheindex, false);
  if (attr_info->read_classrepr == NULL)
    {
      goto exit_on_error;
    }

  if (heap_attrinfo_recache_attrepr (attr_info, false) != NO_ERROR)
    {
      (void) heap_classrepr_free (attr_info->read_classrepr,
				  &attr_info->read_cacheindex);
      attr_info->read_classrepr = NULL;

      goto exit_on_error;
    }

  return ret;

exit_on_error:

  return (ret == NO_ERROR
	  && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
}

/*
 * heap_attrinfo_end () - Done with attribute information structure
 *   return: void
 *   attr_info(in/out): The attribute information structure
 *
 * Note: Release any memory allocated for attribute information related
 * reading of instances.
 */
void
heap_attrinfo_end (THREAD_ENTRY * thread_p, HEAP_CACHE_ATTRINFO * attr_info)
{
  int ret = NO_ERROR;

  /* check to make sure the attr_info has been used */
  if (attr_info->num_values == -1)
    {
      return;
    }

  /*
   * Free any attribute and class representation information
   */
  ret = heap_attrinfo_clear_dbvalues (attr_info);
  ret = heap_attrinfo_recache (thread_p, NULL_REPRID, attr_info);

  if (attr_info->last_classrepr != NULL)
    {
      ret = heap_classrepr_free (attr_info->last_classrepr,
				 &attr_info->last_cacheindex);
      attr_info->last_classrepr = NULL;
    }

  if (attr_info->values)
    {
      db_private_free_and_init (thread_p, attr_info->values);
    }
  OID_SET_NULL (&attr_info->class_oid);

  /*
   * Bash this so that we ensure that heap_attrinfo_end is idempotent.
   */
  attr_info->num_values = -1;

}

/*
 * heap_attrinfo_clear_dbvalues () - Clear current dbvalues of attribute
 *                                 information
 *   return: NO_ERROR
 *   attr_info(in/out): The attribute information structure
 *
 * Note: Clear any current dbvalues associated with attribute information.
 */
int
heap_attrinfo_clear_dbvalues (HEAP_CACHE_ATTRINFO * attr_info)
{
  HEAP_ATTRVALUE *value;	/* Disk value Attr info for a particular attr */
  OR_ATTRIBUTE *attrepr;	/* Which one current repr of default one      */
  int i;
  int ret = NO_ERROR;

  /* check to make sure the attr_info has been used */
  if (attr_info->num_values == -1)
    {
      return NO_ERROR;
    }

  if (attr_info->values != NULL)
    {
      for (i = 0; i < attr_info->num_values; i++)
	{
	  value = &attr_info->values[i];
	  if (value->state != HEAP_UNINIT_ATTRVALUE)
	    {
	      /*
	       * Was the value set up from a default value or from a representation
	       * of the object
	       */
	      attrepr = ((value->read_attrepr != NULL)
			 ? value->read_attrepr : value->last_attrepr);
	      if (attrepr != NULL)
		{
		  if (pr_clear_value (&value->dbvalue) != NO_ERROR)
		    {
		      ret = ER_FAILED;
		    }
		  value->state = HEAP_UNINIT_ATTRVALUE;
		}
	    }
	}
    }
  OID_SET_NULL (&attr_info->inst_oid);
  attr_info->inst_chn = NULL_CHN;

  return ret;
}

/*
 * heap_attrvalue_read () - Read attribute information of given attribute cache
 *                        and instance
 *   return: NO_ERROR
 *   recdes(in): Instance record descriptor
 *   value(in): Disk value attribute information
 *   attr_info(in/out): The attribute information structure
 *
 * Note: Read the dbvalue of the given value attribute information.
 */
static int
heap_attrvalue_read (RECDES * recdes, HEAP_ATTRVALUE * value,
		     HEAP_CACHE_ATTRINFO * attr_info)
{
  OR_BUF buf;
  PR_TYPE *pr_type;		/* Primitive type array function structure */
  OR_ATTRIBUTE *attrepr;
  char *disk_data = NULL;
  int disk_bound = false;
  int disk_length = -1;
  int ret = NO_ERROR;

  /* Initialize disk value information */
  disk_data = NULL;
  disk_bound = false;
  disk_length = -1;

  /*
   * Does attribute exist in this disk representation?
   */

  if (recdes == NULL || recdes->data == NULL || value->read_attrepr == NULL
      || value->attr_type == HEAP_SHARED_ATTR
      || value->attr_type == HEAP_CLASS_ATTR)
    {
      /*
       * Either the attribute is a shared or class attr, or the attribute
       * does not exist in this disk representation, or we do not have
       * the disk object (recdes), get default value if any...
       */
      attrepr = value->last_attrepr;
      disk_length = value->last_attrepr->default_value.val_length;
      if (disk_length > 0)
	{
	  disk_data = (char *) value->last_attrepr->default_value.value;
	  disk_bound = true;
	}
    }
  else
    {
      attrepr = value->read_attrepr;
      /* Is it a fixed size attribute ? */
      if (value->read_attrepr->is_fixed != 0)
	{
	  /*
	   * A fixed attribute.
	   */
	  if (!OR_FIXED_ATT_IS_UNBOUND (recdes->data,
					attr_info->read_classrepr->n_variable,
					attr_info->read_classrepr->
					fixed_length,
					value->read_attrepr->position))
	    {
	      /*
	       * The fixed attribute is bound. Access its information
	       */
	      disk_data =
		((char *) recdes->data +
		 OR_FIXED_ATTRIBUTES_OFFSET_BY_OBJ (recdes->data,
						    attr_info->
						    read_classrepr->
						    n_variable) +
		 value->read_attrepr->location);
	      disk_length = tp_domain_disk_size (value->read_attrepr->domain);
	      disk_bound = true;
	    }
	}
      else
	{
	  /*
	   * A variable attribute
	   */
	  if (!OR_VAR_IS_NULL (recdes->data, value->read_attrepr->location))
	    {
	      /*
	       * The variable attribute is bound.
	       * Find its location through the variable offset attribute table.
	       */
	      disk_data = ((char *) recdes->data +
			   OR_VAR_OFFSET (recdes->data,
					  value->read_attrepr->location));

	      disk_bound = true;
	      switch (TP_DOMAIN_TYPE (attrepr->domain))
		{
		case DB_TYPE_ELO:	/* need real length */
		case DB_TYPE_BLOB:
		case DB_TYPE_CLOB:
		case DB_TYPE_SET:	/* it may be just a little bit fast */
		case DB_TYPE_MULTISET:
		case DB_TYPE_SEQUENCE:
		  OR_VAR_LENGTH (disk_length, recdes->data,
				 value->read_attrepr->location,
				 attr_info->read_classrepr->n_variable);
		  break;
		default:
		  disk_length = -1;	/* remains can read without disk_length */
		}
	    }
	}
    }

  /*
   * From now on, I should only use attrepr.. it will point to either
   * a current value or a default one
   */

  /*
   * Clear/decache any old value
   */
  if (value->state != HEAP_UNINIT_ATTRVALUE)
    {
      (void) pr_clear_value (&value->dbvalue);
    }


  /*
   * Now make the dbvalue according to the disk data value
   */

  if (disk_data == NULL || disk_bound == false)
    {
      /* Unbound attribute, set it to null value */
      ret = db_value_domain_init (&value->dbvalue, attrepr->type,
				  attrepr->domain->precision,
				  attrepr->domain->scale);
      if (ret != NO_ERROR)
	{
	  goto exit_on_error;
	}
      value->state = HEAP_READ_ATTRVALUE;
    }
  else
    {
      /*
       * Read the value according to disk information that was found
       */
      OR_BUF_INIT2 (buf, disk_data, disk_length);
      buf.error_abort = 1;

      switch (_setjmp (buf.env))
	{
	case 0:
	  /* Do not copy the string--just use the pointer.  The pr_ routines
	   * for strings and sets have different semantics for length.
	   * A negative length value for strings means "don't copy the string,
	   * just use the pointer".
	   * For sets, don't translate the set into memory representation
	   * at this time.  It will only be translated when needed.
	   */
	  pr_type = PR_TYPE_FROM_ID (attrepr->type);
	  if (pr_type)
	    {
	      (*(pr_type->data_readval)) (&buf, &value->dbvalue,
					  attrepr->domain, disk_length, false,
					  NULL, 0);
	    }
	  value->state = HEAP_READ_ATTRVALUE;
	  break;
	default:
	  /*
	   * An error was found during the reading of the attribute value
	   */
	  (void) db_value_domain_init (&value->dbvalue, attrepr->type,
				       attrepr->domain->precision,
				       attrepr->domain->scale);
	  value->state = HEAP_UNINIT_ATTRVALUE;
	  ret = ER_FAILED;
	  break;
	}
    }

  return ret;

exit_on_error:

  return (ret == NO_ERROR
	  && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
}

/*
 * heap_midxkey_get_value () -
 *   return:
 *   recdes(in):
 *   att(in):
 *   value(out):
 *   attr_info(in):
 */
static int
heap_midxkey_get_value (RECDES * recdes, OR_ATTRIBUTE * att,
			DB_VALUE * value, HEAP_CACHE_ATTRINFO * attr_info)
{
  char *disk_data = NULL;
  int disk_bound = false;
  bool found = true;		/* Does attribute(att) exist in
				   this disk representation? */
  int i;
  int error = NO_ERROR;

  /* Initialize disk value information */
  disk_data = NULL;
  db_make_null (value);

  if (recdes != NULL && recdes->data != NULL && att != NULL)
    {
      if (or_rep_id (recdes) != attr_info->last_classrepr->id)
	{
	  found = false;
	  for (i = 0; i < attr_info->read_classrepr->n_attributes; i++)
	    {
	      if (attr_info->read_classrepr->attributes[i].id == att->id)
		{
		  att = &attr_info->read_classrepr->attributes[i];
		  found = true;
		  break;
		}
	    }
	}

      if (found == false)
	{
	  /* It means that the representation has an attribute
	   * which was created after insertion of the record.
	   * In this case, return the default value of the attribute
	   * if it exists.
	   */
	  if (att->default_value.val_length > 0)
	    {
	      disk_data = att->default_value.value;
	    }
	}
      else
	{
	  /* Is it a fixed size attribute ? */
	  if (att->is_fixed != 0)
	    {			/* A fixed attribute.  */
	      if (!OR_FIXED_ATT_IS_UNBOUND (recdes->data,
					    attr_info->read_classrepr->
					    n_variable,
					    attr_info->read_classrepr->
					    fixed_length, att->position))
		{
		  /* The fixed attribute is bound. Access its information */
		  disk_data = ((char *) recdes->data +
			       OR_FIXED_ATTRIBUTES_OFFSET_BY_OBJ
			       (recdes->data,
				attr_info->read_classrepr->n_variable) +
			       att->location);
		}
	    }
	  else
	    {			/* A variable attribute */
	      if (!OR_VAR_IS_NULL (recdes->data, att->location))
		{
		  /* The variable attribute is bound.
		   * Find its location through the variable offset attribute table. */
		  disk_data = ((char *) recdes->data +
			       OR_VAR_OFFSET (recdes->data, att->location));
		}
	    }
	}
    }
  else
    {
      assert (0);
      return ER_FAILED;
    }

  if (disk_data != NULL)
    {
      OR_BUF buf;

      or_init (&buf, disk_data, -1);
      (*(att->domain->type->data_readval)) (&buf, value, att->domain,
					    -1, false, NULL, 0);
    }

  return NO_ERROR;
}

/*
 * heap_attrinfo_read_dbvalues () - Find db_values of desired attributes of given
 *                                instance
 *   return: NO_ERROR
 *   inst_oid(in): The instance oid
 *   recdes(in): The instance Record descriptor
 *   attr_info(in/out): The attribute information structure which describe the
 *                      desired attributes
 *
 * Note: Find DB_VALUES of desired attributes of given instance.
 * The attr_info structure must have already been initialized
 * with the desired attributes.
 *
 * If the inst_oid and the recdes are NULL, then we must be
 * reading only shared and/or class attributes which are found
 * in the last representation.
 */
int
heap_attrinfo_read_dbvalues (THREAD_ENTRY * thread_p, const OID * inst_oid,
			     RECDES * recdes, HEAP_CACHE_ATTRINFO * attr_info)
{
  int i;
  REPR_ID reprid;		/* The disk representation of the object      */
  HEAP_ATTRVALUE *value;	/* Disk value Attr info for a particular attr */
  int ret = NO_ERROR;

  /* check to make sure the attr_info has been used */
  if (attr_info->num_values == -1)
    {
      return NO_ERROR;
    }

  /*
   * Make sure that we have the needed cached representation.
   */

  if (inst_oid != NULL && recdes != NULL)
    {
      reprid = or_rep_id (recdes);

      if (attr_info->read_classrepr == NULL
	  || attr_info->read_classrepr->id != reprid)
	{
	  /* Get the needed representation */
	  ret = heap_attrinfo_recache (thread_p, reprid, attr_info);
	  if (ret != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	}
    }

  /*
   * Go over each attribute and read it
   */

  for (i = 0; i < attr_info->num_values; i++)
    {
      value = &attr_info->values[i];
      ret = heap_attrvalue_read (recdes, value, attr_info);
      if (ret != NO_ERROR)
	{
	  goto exit_on_error;
	}
    }

  /*
   * Cache the information of the instance
   */
  if (inst_oid != NULL && recdes != NULL)
    {
      attr_info->inst_chn = or_chn (recdes);
      attr_info->inst_oid = *inst_oid;
    }

  return ret;

exit_on_error:

  return (ret == NO_ERROR
	  && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
}

/*
 * heap_attrinfo_delete_lob ()
 *   return: NO_ERROR
 *   thread_p(in):
 *   recdes(in): The instance Record descriptor
 *   attr_info(in): The attribute information structure which describe the
 *                  desired attributes
 *
 */
int
heap_attrinfo_delete_lob (THREAD_ENTRY * thread_p,
			  RECDES * recdes, HEAP_CACHE_ATTRINFO * attr_info)
{
  int i;
  HEAP_ATTRVALUE *value;
  int ret = NO_ERROR;

  assert (attr_info && attr_info->num_values > 0);

  /*
   * Make sure that we have the needed cached representation.
   */

  if (recdes != NULL)
    {
      REPR_ID reprid;
      reprid = or_rep_id (recdes);
      if (attr_info->read_classrepr == NULL ||
	  attr_info->read_classrepr->id != reprid)
	{
	  /* Get the needed representation */
	  ret = heap_attrinfo_recache (thread_p, reprid, attr_info);
	  if (ret != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	}
    }

  /*
   * Go over each attribute and delete the data if it's lob type
   */

  for (i = 0; i < attr_info->num_values; i++)
    {
      value = &attr_info->values[i];
      if (value->last_attrepr->type == DB_TYPE_BLOB ||
	  value->last_attrepr->type == DB_TYPE_CLOB)
	{
	  if (value->state == HEAP_UNINIT_ATTRVALUE && recdes != NULL)
	    {
	      ret = heap_attrvalue_read (recdes, value, attr_info);
	      if (ret != NO_ERROR)
		{
		  goto exit_on_error;
		}
	    }
	  if ((value->state == HEAP_READ_ATTRVALUE ||
	       value->state == HEAP_WRITTEN_LOB_ATTRVALUE) &&
	      !db_value_is_null (&value->dbvalue))
	    {
	      DB_ELO *elo;
	      assert (db_value_type (&value->dbvalue) == DB_TYPE_BLOB ||
		      db_value_type (&value->dbvalue) == DB_TYPE_CLOB);
	      elo = db_get_elo (&value->dbvalue);
	      if (elo)
		{
		  ret = db_elo_delete (elo);
		}
	      if (value->state == HEAP_WRITTEN_LOB_ATTRVALUE)
		{
		  value->state = HEAP_WRITTEN_ATTRVALUE;
		}
	    }
	}
    }

  return ret;

exit_on_error:

  return (ret == NO_ERROR
	  && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
}

/*
 * heap_attrinfo_dump () - Dump value of attribute information
 *   return:
 *   attr_info(in): The attribute information structure
 *   dump_schema(in):
 *
 * Note: Dump attribute value of given attribute information.
 */
void
heap_attrinfo_dump (THREAD_ENTRY * thread_p, FILE * fp,
		    HEAP_CACHE_ATTRINFO * attr_info, bool dump_schema)
{
  int i;
  HEAP_ATTRVALUE *value;	/* Disk value Attr info for a particular attr   */
  int ret = NO_ERROR;

  /* check to make sure the attr_info has been used */
  if (attr_info->num_values == -1)
    {
      fprintf (fp, "  Empty attrinfo\n");
      return;
    }

  /*
   * Dump attribute schema information
   */

  if (dump_schema == true)
    {
      ret =
	heap_classrepr_dump (thread_p, fp, &attr_info->class_oid,
			     attr_info->read_classrepr);
    }

  for (i = 0; i < attr_info->num_values; i++)
    {
      value = &attr_info->values[i];
      fprintf (fp, "  Attrid = %d, state = %d, type = %s\n",
	       value->attrid, value->state,
	       pr_type_name (value->read_attrepr->type));
      /*
       * Dump the value in memory format
       */

      fprintf (fp, "  Memory_value_format:\n");
      fprintf (fp, "    value = ");
      db_value_fprint (fp, &value->dbvalue);
      fprintf (fp, "\n\n");
    }

}

/*
 * heap_attrvalue_locate () - Locate disk attribute value information
 *   return: attrvalue or NULL
 *   attrid(in): The desired attribute identifier
 *   attr_info(in/out): The attribute information structure which describe the
 *                      desired attributes
 *
 * Note: Locate the disk attribute value information of an attribute
 * information structure which have been already initialized.
 */
HEAP_ATTRVALUE *
heap_attrvalue_locate (ATTR_ID attrid, HEAP_CACHE_ATTRINFO * attr_info)
{
  HEAP_ATTRVALUE *value;	/* Disk value Attr info for a particular attr */
  int i;

  for (i = 0, value = attr_info->values;
       i < attr_info->num_values; i++, value++)
    {
      if (attrid == value->attrid)
	{
	  return value;
	}
    }

  return NULL;
}

/*
 * heap_locate_attribute () -
 *   return:
 *   attrid(in):
 *   attr_info(in):
 */
static OR_ATTRIBUTE *
heap_locate_attribute (ATTR_ID attrid, HEAP_CACHE_ATTRINFO * attr_info)
{
  HEAP_ATTRVALUE *value;	/* Disk value Attr info for a particular attr */
  int i;

  for (i = 0, value = attr_info->values;
       i < attr_info->num_values; i++, value++)
    {
      if (attrid == value->attrid)
	{
	  /* Some altered attributes might have only
	   * the last representations of them.
	   */
	  return (value->read_attrepr != NULL) ?
	    value->read_attrepr : value->last_attrepr;
	}
    }

  return NULL;
}

/*
 * heap_locate_last_attrepr () -
 *   return:
 *   attrid(in):
 *   attr_info(in):
 */
OR_ATTRIBUTE *
heap_locate_last_attrepr (ATTR_ID attrid, HEAP_CACHE_ATTRINFO * attr_info)
{
  HEAP_ATTRVALUE *value;	/* Disk value Attr info for a particular attr */
  int i;

  for (i = 0, value = attr_info->values;
       i < attr_info->num_values; i++, value++)
    {
      if (attrid == value->attrid)
	{
	  return value->last_attrepr;
	}
    }

  return NULL;
}

/*
 * heap_attrinfo_access () - Access an attribute value which has been already read
 *   return:
 *   attrid(in): The desired attribute identifier
 *   attr_info(in/out): The attribute information structure which describe the
 *                      desired attributes
 *
 * Note: Find DB_VALUE of desired attribute identifier.
 * The dbvalue attributes must have been read by now using the
 * function heap_attrinfo_read_dbvalues ()
 */
DB_VALUE *
heap_attrinfo_access (ATTR_ID attrid, HEAP_CACHE_ATTRINFO * attr_info)
{
  HEAP_ATTRVALUE *value;	/* Disk value Attr info for a particular attr   */

  /* check to make sure the attr_info has been used */
  if (attr_info->num_values == -1)
    {
      return NULL;
    }

  value = heap_attrvalue_locate (attrid, attr_info);
  if (value == NULL || value->state == HEAP_UNINIT_ATTRVALUE)
    {
      er_log_debug (ARG_FILE_LINE,
		    "heap_attrinfo_access: Unknown attrid = %d", attrid);
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return NULL;
    }

  return &value->dbvalue;
}

/*
 * heap_get_class_subclasses () - get OIDs of subclasses for a given class
 * return : error code or NO_ERROR
 * thread_p (in)  :
 * class_oid (in) : OID of the parent class
 * count (out)	  : size of the subclasses array
 * subclasses (out) : array containing OIDs of subclasses
 *
 * Note: The subclasses array is maintained as an array of OID's,
 *	 the last element in the array will satisfy the OID_ISNULL() test.
 *	 The array_size has the number of actual elements allocated in the
 *	 array which may be more than the number of slots that have non-NULL
 *	 OIDs. The function adds the subclass oids to the existing array.
 *	 If the array is not large enough, it is reallocated using realloc.
 */
int
heap_get_class_subclasses (THREAD_ENTRY * thread_p, const OID * class_oid,
			   int *count, OID ** subclasses)
{
  HEAP_SCANCACHE scan_cache;
  RECDES recdes;
  int error = NO_ERROR;

  error = heap_scancache_quick_start (&scan_cache);
  if (error != NO_ERROR)
    {
      return error;
    }

  if (heap_get (thread_p, class_oid, &recdes, &scan_cache, PEEK, NULL_CHN)
      != S_SUCCESS)
    {
      heap_scancache_end (thread_p, &scan_cache);
      return ER_FAILED;
    }

  error = orc_subclasses_from_record (&recdes, count, subclasses);

  heap_scancache_end (thread_p, &scan_cache);

  return error;
}

/*
 * heap_class_get_partition_info () - Get partition information for the class
 *				      identified by class_oid
 * return : error code or NO_ERROR
 * class_oid (in) : class_oid
 * partition_info (in/out) : partition information
 * class_hfid (in/out) : HFID of the partitioned class
 * repr_id  (in/out) : class representation id
 *
 * Note: This function extracts the partition information from a class OID.
 * Partition information is represented by the OID of the tuple from the
 * _db_partition class in which we can find the actual partition info.
 * This OID is stored in the class properties sequence
 *
 * If the class is not a partition or is not a partitioned class,
 * partition_info will be returned as a NULL OID
 */
static int
heap_class_get_partition_info (THREAD_ENTRY * thread_p, const OID * class_oid,
			       OID * partition_oid, HFID * class_hfid,
			       REPR_ID * repr_id)
{
  int error = NO_ERROR;
  RECDES recdes;
  HEAP_SCANCACHE scan_cache;
  OID root_oid;

  assert (partition_oid != NULL);
  assert (class_oid != NULL);

  if (heap_scancache_quick_start (&scan_cache) != NO_ERROR)
    {
      return ER_FAILED;
    }

  if (heap_get_with_class_oid (thread_p, &root_oid, class_oid, &recdes,
			       &scan_cache, PEEK) != S_SUCCESS)
    {
      error = ER_FAILED;
      goto cleanup;
    }

  error = or_class_get_partition_info (&recdes, partition_oid, repr_id);
  if (error != NO_ERROR)
    {
      goto cleanup;
    }

  if (class_hfid != NULL)
    {
      or_class_hfid (&recdes, class_hfid);
    }

cleanup:
  heap_scancache_end (thread_p, &scan_cache);

  return error;
}

/*
 * heap_get_partition_attributes () - get attribute ids for columns of
 *				      _db_partition class
 * return : error code or NO_ERROR
 * thread_p (in)      :
 * cls_oid (in)	      : _db_partition class OID
 * type_id (in/out)   : holder for the type attribute id
 * values_id (in/out) : holder for the values attribute id
 */
static int
heap_get_partition_attributes (THREAD_ENTRY * thread_p, const OID * cls_oid,
			       ATTR_ID * type_id, ATTR_ID * values_id)
{
  int idx_cache = -1;
  RECDES recdes;
  HEAP_SCANCACHE scan;
  HEAP_CACHE_ATTRINFO attr_info;
  int error = NO_ERROR;
  int i = 0;
  const char *attr_name = NULL;
  bool is_scan_cache_started = false, is_attrinfo_started = false;

  if (type_id == NULL || values_id == NULL)
    {
      assert (false);
      error = ER_FAILED;
      goto cleanup;
    }
  *type_id = *values_id = NULL_ATTRID;

  if (heap_scancache_quick_start (&scan) != NO_ERROR)
    {
      error = ER_FAILED;
      goto cleanup;
    }
  is_scan_cache_started = true;

  error = heap_attrinfo_start (thread_p, cls_oid, -1, NULL, &attr_info);
  if (error != NO_ERROR)
    {
      goto cleanup;
    }
  is_attrinfo_started = true;

  if (heap_get (thread_p, cls_oid, &recdes, &scan, PEEK, NULL_CHN)
      != S_SUCCESS)
    {
      error = ER_FAILED;
      goto cleanup;
    }

  for (i = 0;
       i < attr_info.num_values && (*type_id == NULL_ATTRID
				    || *values_id == NULL_ATTRID); i++)
    {
      attr_name = or_get_attrname (&recdes, i);
      if (attr_name == NULL)
	{
	  error = ER_FAILED;
	  goto cleanup;
	}
      if (strcmp (attr_name, "ptype") == 0)
	{
	  *type_id = i;
	}

      if (strcmp (attr_name, "pvalues") == 0)
	{
	  *values_id = i;
	}
    }

  if (*type_id == NULL_ATTRID || *values_id == NULL_ATTRID)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      error = ER_FAILED;
    }

cleanup:
  if (is_attrinfo_started)
    {
      heap_attrinfo_end (thread_p, &attr_info);
    }
  if (is_scan_cache_started)
    {
      heap_scancache_end (thread_p, &scan);
    }
  return error;
}

/*
 * heap_read_or_partition () - read OR_PARTITION information for a class
 * return :
 * thread_p (in)    :
 * attr_info (in)   : attribute cache
 * scan_cache (in)  : scan cache
 * type_id (in)	    : id of the type attribute
 * values_id (in)   : id of the values attribute
 * or_part (in/out) : partition information
 */
static int
heap_read_or_partition (THREAD_ENTRY * thread_p,
			HEAP_CACHE_ATTRINFO * attr_info,
			HEAP_SCANCACHE * scan_cache, ATTR_ID type_id,
			ATTR_ID values_id, OR_PARTITION * or_part)
{
  SCAN_CODE scan_code = S_SUCCESS;
  DB_VALUE *db_val = NULL;
  RECDES recdes;
  int error = NO_ERROR;

  if (or_part == NULL)
    {
      assert (false);
      return ER_FAILED;
    }

  scan_code = heap_get (thread_p, &or_part->db_part_oid, &recdes, scan_cache,
			PEEK, NULL_CHN);
  if (scan_code != S_SUCCESS)
    {
      return ER_FAILED;
    }

  error =
    heap_attrinfo_read_dbvalues (thread_p, &or_part->db_part_oid, &recdes,
				 attr_info);
  if (error != NO_ERROR)
    {
      return error;
    }

  /* get partition type */
  db_val = heap_attrinfo_access (type_id, attr_info);
  if (db_val == NULL)
    {
      error = ER_FAILED;
      goto cleanup;
    }

  if (DB_VALUE_TYPE (db_val) != DB_TYPE_INTEGER)
    {
      assert (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      error = ER_FAILED;
      goto cleanup;
    }

  or_part->partition_type = DB_GET_INT (db_val);
  pr_clear_value (db_val);

  /* get partition limits */
  db_val = heap_attrinfo_access (values_id, attr_info);
  if (!DB_IS_NULL (db_val))
    {
      if (DB_VALUE_TYPE (db_val) != DB_TYPE_SEQUENCE)
	{
	  assert (false);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  error = ER_FAILED;
	  goto cleanup;
	}
      or_part->values = db_set_copy (DB_GET_SEQUENCE (db_val));
    }
  else
    {
      /* hash partition */
      or_part->values = NULL;
    }

cleanup:
  heap_attrinfo_clear_dbvalues (attr_info);
  return error;
}

/*
 * heap_get_partitions_from_subclasses () - Get partition information from a
 *					    list of subclasses
 * return : error code or NO_ERROR
 * thread_p (in)	:
 * subclasses (in)	: subclasses OIDs
 * class_part_oid (in)	: OID of the master class
 * _db_part_oid (in)	: OID of the _db_partition class
 * parts_count (in/out) : number of "useful" elements in parts
 * parts (in/out)	: partitions
 *
 *  Note: Memory for the partition array must be allocated before calling this
 *  function and must be enough to store all partitions. The value from
 *  position 0 in the partitions array will contain information from the
 *  master class
 */
static int
heap_get_partitions_from_subclasses (THREAD_ENTRY * thread_p,
				     const OID * subclasses,
				     const OID * class_part_oid,
				     const OID * _db_part_oid,
				     int *parts_count, OR_PARTITION * parts)
{
  int part_idx = 0, i;
  int error = NO_ERROR;
  HEAP_SCANCACHE scan_cache;
  HEAP_CACHE_ATTRINFO attr_info;
  OID part_info;
  HFID part_hfid;
  bool is_scancache_started = false, is_attrinfo_started = false;
  ATTR_ID type_id = NULL_ATTRID, values_id = NULL_ATTRID;
  REPR_ID repr_id = NULL_REPRID;

  if (parts == NULL)
    {
      assert (false);
      error = ER_FAILED;
      goto cleanup;
    }
  /* Get attribute ids for the attributes we are interested in from the class
   * _db_partition. At this point we are interested in the ptype and pvalues
   * columns. The ptype column represents partition type (LIST, RANGE, HASH)
   * and pvalues represents actual limits for each partition. For the master
   * class, pvalues also contains the serialized REGU_VAR representing the
   * partition expression
   */
  error =
    heap_get_partition_attributes (thread_p, _db_part_oid, &type_id,
				   &values_id);
  if (error != NO_ERROR)
    {
      goto cleanup;
    }
  /* Start a heap scan for _db_partition class */
  error = heap_scancache_quick_start (&scan_cache);
  if (error != NO_ERROR)
    {
      goto cleanup;
    }
  is_scancache_started = true;

  /* start heap attr info for _db_partition class */
  error = heap_attrinfo_start (thread_p, _db_part_oid, -1, NULL, &attr_info);
  if (error != NO_ERROR)
    {
      goto cleanup;
    }
  is_attrinfo_started = true;

  /* first get information for the master class */
  OID_SET_NULL (&(parts[0].class_oid));
  COPY_OID (&(parts[0].db_part_oid), class_part_oid);

  error = heap_read_or_partition (thread_p, &attr_info, &scan_cache,
				  type_id, values_id, &parts[0]);
  if (error != NO_ERROR)
    {
      goto cleanup;
    }

  part_idx = 1;
  /* loop through subclasses and load partition information if the subclass is
     a partition */
  for (i = 0; !OID_ISNULL (&subclasses[i]); i++)
    {
      /* Get partition information from this subclass. part_info will be the
       * OID of the tuple from _db_partition containing partition information
       */
      error =
	heap_class_get_partition_info (thread_p, &subclasses[i], &part_info,
				       &part_hfid, &repr_id);
      if (error != NO_ERROR)
	{
	  goto cleanup;
	}

      if (OID_ISNULL (&part_info))
	{
	  /* this is not a partition, this is a simple subclass */
	  continue;
	}

      COPY_OID (&(parts[part_idx].class_oid), &subclasses[i]);
      COPY_OID (&(parts[part_idx].db_part_oid), &part_info);
      HFID_COPY (&(parts[part_idx].class_hfid), &part_hfid);
      parts[part_idx].rep_id = repr_id;
      error = heap_read_or_partition (thread_p, &attr_info, &scan_cache,
				      type_id, values_id, &parts[part_idx]);
      if (error != NO_ERROR)
	{
	  goto cleanup;
	}
      part_idx++;
    }
  *parts_count = part_idx;

cleanup:
  if (is_attrinfo_started)
    {
      heap_attrinfo_end (thread_p, &attr_info);
    }
  if (is_scancache_started)
    {
      heap_scancache_end (thread_p, &scan_cache);
    }
  if (error != NO_ERROR)
    {
      /* free memory for the values of partitions */
      for (i = 0; i < part_idx; i++)
	{
	  if (parts[i].values != NULL)
	    {
	      db_seq_free (parts[i].values);
	    }
	}
    }
  return error;
}

/*
 * heap_get_class_partitions () - get partitions information for a class
 * return : error code or NO_ERROR
 * thread_p (in)	:
 * class_oid (in)	: class OID
 * parts (in/out)	: partitions information
 * parts_count (in/out)	: number of partitions
 */
int
heap_get_class_partitions (THREAD_ENTRY * thread_p, const OID * class_oid,
			   OR_PARTITION ** parts, int *parts_count)
{
  int subclasses_count = 0;
  OID *subclasses = NULL;
  OID part_info;
  int error = NO_ERROR;
  OID _db_part_oid;
  OR_PARTITION *partitions = NULL;
  REPR_ID class_repr_id = NULL_REPRID;

  OID_SET_NULL (&_db_part_oid);
  /* This class might have partitions and subclasses. In order to get
   * partition information we have to:
   *  1. Get the OIDs for all subclasses
   *  2. Get partition information for all OIDs
   *  3. Build information only for those subclasses which are partitions
   */
  error =
    heap_class_get_partition_info (thread_p, class_oid, &part_info, NULL,
				   &class_repr_id);
  if (error != NO_ERROR)
    {
      goto cleanup;
    }

  if (OID_ISNULL (&part_info))
    {
      /* this class does not have partitions */
      error = NO_ERROR;
      goto cleanup;
    }

  /* Get OIDs for subclasses of class_oid. Some of them will be partitions */
  error = heap_get_class_subclasses (thread_p, class_oid, &subclasses_count,
				     &subclasses);
  if (error != NO_ERROR)
    {
      goto cleanup;
    }
  else if (subclasses_count == 0)
    {
      /* This means that class_oid actually points to a partition and not
       * the master class. We return NO_ERROR here since there's no partition
       * information
       */
      error = NO_ERROR;
      goto cleanup;
    }

  /* part_info is the OID of the tuple from _db_partition class where
   * partition information can be found for this class. We will get the OID of
   * the _db_partition class here because we will need it later
   */
  if (heap_get_class_oid (thread_p, &_db_part_oid, &part_info) == NULL)
    {
      error = ER_FAILED;
      goto cleanup;
    }

  /* Allocate memory for partitions. We allocate more memory than needed here
   * because the call to heap_get_class_subclasses from above actually
   * returned a larger count than the useful information. Also, not all
   * subclasses are necessarily partitions.
   */
  partitions =
    (OR_PARTITION *) db_private_alloc (thread_p,
				       (subclasses_count +
					1) * sizeof (OR_PARTITION));
  if (partitions == NULL)
    {
      error = ER_FAILED;
      goto cleanup;
    }

  error =
    heap_get_partitions_from_subclasses (thread_p, subclasses, &part_info,
					 &_db_part_oid, parts_count,
					 partitions);
  if (error != NO_ERROR)
    {
      goto cleanup;
    }

  /* set root class representation id */
  partitions[0].rep_id = class_repr_id;

  *parts = partitions;

cleanup:
  if (subclasses != NULL)
    {
      free_and_init (subclasses);
    }
  if (error != NO_ERROR && partitions != NULL)
    {
      db_private_free (thread_p, partitions);
      *parts = NULL;
      *parts_count = 0;
    }
  return error;
}

/*
 * heap_get_class_supers () - get OIDs of superclasses of a class
 * return : error code or NO_ERROR
 * thread_p (in)  : thread entry
 * class_oid (in) : OID of the subclass
 * super_oids (in/out) : OIDs of the superclasses
 * count (in/out)      : number of elements in super_oids
 */
int
heap_get_class_supers (THREAD_ENTRY * thread_p, const OID * class_oid,
		       OID ** super_oids, int *count)
{
  HEAP_SCANCACHE scan_cache;
  RECDES recdes;
  int error = NO_ERROR;

  error = heap_scancache_quick_start (&scan_cache);
  if (error != NO_ERROR)
    {
      return error;
    }

  if (heap_get (thread_p, class_oid, &recdes, &scan_cache, PEEK, NULL_CHN)
      != S_SUCCESS)
    {
      heap_scancache_end (thread_p, &scan_cache);
      return ER_FAILED;
    }

  error = orc_superclasses_from_record (&recdes, count, super_oids);

  heap_scancache_end (thread_p, &scan_cache);

  return error;
}

/*
 * heap_attrinfo_check () -
 *   return: NO_ERROR
 *   inst_oid(in): The instance oid
 *   attr_info(in): The attribute information structure which describe the
 *                  desired attributes
 */
static int
heap_attrinfo_check (const OID * inst_oid, HEAP_CACHE_ATTRINFO * attr_info)
{
  int ret = NO_ERROR;

  if (inst_oid != NULL)
    {
      /*
       * The OIDs must be equal
       */
      if (!OID_EQ (&attr_info->inst_oid, inst_oid))
	{
	  if (!OID_ISNULL (&attr_info->inst_oid))
	    {
	      ret = ER_HEAP_WRONG_ATTRINFO;
	      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
		      ret, 6, attr_info->inst_oid.volid,
		      attr_info->inst_oid.pageid, attr_info->inst_oid.slotid,
		      inst_oid->volid, inst_oid->pageid, inst_oid->slotid);
	      goto exit_on_error;
	    }

	  attr_info->inst_oid = *inst_oid;
	}
    }
  else
    {
      if (!OID_ISNULL (&attr_info->inst_oid))
	{
	  ret = ER_HEAP_WRONG_ATTRINFO;
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
		  ret, 6, attr_info->inst_oid.volid,
		  attr_info->inst_oid.pageid, attr_info->inst_oid.slotid,
		  NULL_VOLID, NULL_PAGEID, NULL_SLOTID);
	  goto exit_on_error;
	}
    }

  return ret;

exit_on_error:

  return (ret == NO_ERROR
	  && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
}

/*
 * heap_attrinfo_set () - Set the value of given attribute
 *   return: NO_ERROR
 *   inst_oid(in): The instance oid
 *   attrid(in): The identifier of the attribute to be set
 *   attr_val(in): The memory value of the attribute
 *   attr_info(in/out): The attribute information structure which describe the
 *                      desired attributes
 *
 * Note: Set DB_VALUE of desired attribute identifier.
 */
int
heap_attrinfo_set (const OID * inst_oid, ATTR_ID attrid, DB_VALUE * attr_val,
		   HEAP_CACHE_ATTRINFO * attr_info)
{
  HEAP_ATTRVALUE *value;	/* Disk value Attr info for a particular attr */
  PR_TYPE *pr_type;		/* Primitive type array function structure */
  TP_DOMAIN_STATUS dom_status;
  int ret = NO_ERROR;

  /*
   * check to make sure the attr_info has been used, should never be empty.
   */

  if (attr_info->num_values == -1)
    {
      return ER_FAILED;
    }

  ret = heap_attrinfo_check (inst_oid, attr_info);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  value = heap_attrvalue_locate (attrid, attr_info);
  if (value == NULL)
    {
      goto exit_on_error;
    }

  pr_type = PR_TYPE_FROM_ID (value->last_attrepr->type);
  if (pr_type == NULL)
    {
      goto exit_on_error;
    }

  ret = pr_clear_value (&value->dbvalue);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  ret = db_value_domain_init (&value->dbvalue,
			      value->last_attrepr->type,
			      value->last_attrepr->domain->precision,
			      value->last_attrepr->domain->scale);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  /*
   * As we use "writeval" to do the writing and that function gets
   * enough domain information, we can use non-exact domain matching
   * here to defer the coercion until it is written.
   */
  dom_status = tp_domain_check (value->last_attrepr->domain, attr_val,
				TP_EXACT_MATCH);
  if (dom_status == DOMAIN_COMPATIBLE)
    {
      /*
       * the domains match exactly, set the value and proceed.  Copy
       * the source only if it's a set-valued thing (that's the purpose
       * of the third argument).
       */
      ret = (*(pr_type->setval)) (&value->dbvalue, attr_val,
				  TP_IS_SET_TYPE (pr_type->id));
    }
  else
    {
      /* the domains don't match, must attempt coercion */
      dom_status = tp_value_auto_cast (attr_val, &value->dbvalue,
				       value->last_attrepr->domain);
      if (dom_status != DOMAIN_COMPATIBLE)
	{
	  ret = tp_domain_status_er_set (dom_status, ARG_FILE_LINE,
					 attr_val,
					 value->last_attrepr->domain);
	  assert (er_errid () != NO_ERROR);

	  DB_MAKE_NULL (&value->dbvalue);
	}
    }

  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  value->state = HEAP_WRITTEN_ATTRVALUE;

  return ret;

exit_on_error:

  return (ret == NO_ERROR
	  && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
}

/*
 * heap_attrinfo_set_uninitialized () - Read unitialized attributes
 *   return: NO_ERROR
 *   inst_oid(in): The instance oid
 *   recdes(in): The instance record descriptor
 *   attr_info(in/out): The attribute information structure which describe the
 *                      desired attributes
 *
 * Note: Read the db values of the unitialized attributes from the
 * given recdes. This function is used when we are ready to
 * transform an object that has been updated/inserted in the server.
 * If the object has been updated, recdes must be the old object
 * (the one on disk), so we can set the rest of the uninitialized
 * attributes from the old object.
 * If the object is a new one, recdes should be NULL, since there
 * is not an object on disk, the rest of the unitialized
 * attributes are set from default values.
 */
static int
heap_attrinfo_set_uninitialized (THREAD_ENTRY * thread_p, OID * inst_oid,
				 RECDES * recdes,
				 HEAP_CACHE_ATTRINFO * attr_info)
{
  int i;
  REPR_ID reprid;		/* Representation of object                   */
  HEAP_ATTRVALUE *value;	/* Disk value Attr info for a particular attr */
  int ret = NO_ERROR;

  ret = heap_attrinfo_check (inst_oid, attr_info);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  /*
   * Make sure that we have the needed cached representation.
   */

  if (recdes != NULL)
    {
      reprid = or_rep_id (recdes);
    }
  else
    {
      reprid = attr_info->last_classrepr->id;
    }

  if (attr_info->read_classrepr == NULL ||
      attr_info->read_classrepr->id != reprid)
    {
      /* Get the needed representation */
      ret = heap_attrinfo_recache (thread_p, reprid, attr_info);
      if (ret != NO_ERROR)
	{
	  goto exit_on_error;
	}
    }

  /*
   * Go over the attribute values and set the ones that have not been
   * initialized
   */
  for (i = 0; i < attr_info->num_values; i++)
    {
      value = &attr_info->values[i];
      if (value->state == HEAP_UNINIT_ATTRVALUE)
	{
	  ret = heap_attrvalue_read (recdes, value, attr_info);
	  if (ret != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	}
      else if (value->state == HEAP_WRITTEN_ATTRVALUE &&
	       (value->last_attrepr->type == DB_TYPE_BLOB ||
		value->last_attrepr->type == DB_TYPE_CLOB))
	{
	  DB_VALUE *save;
	  save = db_value_copy (&value->dbvalue);
	  db_value_clear (&value->dbvalue);

	  /* read and delete old value */
	  ret = heap_attrvalue_read (recdes, value, attr_info);
	  if (ret != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	  if (!db_value_is_null (&value->dbvalue))
	    {
	      DB_ELO *elo;

	      assert (db_value_type (&value->dbvalue) == DB_TYPE_BLOB ||
		      db_value_type (&value->dbvalue) == DB_TYPE_CLOB);
	      elo = db_get_elo (&value->dbvalue);
	      if (elo)
		{
		  ret = db_elo_delete (elo);
		}
	      db_value_clear (&value->dbvalue);
	      ret = (ret >= 0 ? NO_ERROR : ret);
	      if (ret != NO_ERROR)
		{
		  goto exit_on_error;
		}
	    }
	  value->state = HEAP_WRITTEN_ATTRVALUE;
	  db_value_clone (save, &value->dbvalue);
	  db_value_free (save);
	}
    }

  if (recdes != NULL)
    {
      attr_info->inst_chn = or_chn (recdes);
    }
  else
    {
      attr_info->inst_chn = -1;
    }

  return ret;

exit_on_error:

  return (ret == NO_ERROR
	  && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
}

/*
 * heap_attrinfo_get_disksize () - Find the disk size needed to transform the object
 *                        represented by attr_info
 *   return: size of the object
 *   attr_info(in/out): The attribute information structure
 *
 * Note: Find the disk size needed to transform the object represented
 * by the attribute information structure.
 */
static int
heap_attrinfo_get_disksize (HEAP_CACHE_ATTRINFO * attr_info,
			    int *offset_size_ptr)
{
  int i, size;
  HEAP_ATTRVALUE *value;	/* Disk value Attr info for a particular attr   */

  *offset_size_ptr = OR_BYTE_SIZE;

re_check:
  size = 0;
  for (i = 0; i < attr_info->num_values; i++)
    {
      value = &attr_info->values[i];

      if (value->last_attrepr->is_fixed != 0)
	{
	  size += tp_domain_disk_size (value->last_attrepr->domain);
	}
      else
	{
	  size += pr_data_writeval_disk_size (&value->dbvalue);
	}
    }

  size += OR_HEADER_SIZE;
  size +=
    OR_VAR_TABLE_SIZE_INTERNAL (attr_info->last_classrepr->n_variable,
				*offset_size_ptr);
  size +=
    OR_BOUND_BIT_BYTES (attr_info->last_classrepr->n_attributes -
			attr_info->last_classrepr->n_variable);

  if (*offset_size_ptr == OR_BYTE_SIZE && size > OR_MAX_BYTE)
    {
      *offset_size_ptr = OR_SHORT_SIZE;	/* 2byte */
      goto re_check;
    }
  if (*offset_size_ptr == OR_SHORT_SIZE && size > OR_MAX_SHORT)
    {
      *offset_size_ptr = BIG_VAR_OFFSET_SIZE;	/* 4byte */
      goto re_check;
    }

  return size;
}

/*
 * heap_attrinfo_transform_to_disk () - Transform to disk an attribute information
 *                               kind of instance
 *   return: SCAN_CODE
 *           (Either of S_SUCCESS, S_DOESNT_FIT,
 *                      S_ERROR)
 *   attr_info(in/out): The attribute information structure
 *   old_recdes(in): where the object's disk format is deposited
 *   new_recdes(in):
 *
 * Note: Transform the object represented by attr_info to disk format
 */
SCAN_CODE
heap_attrinfo_transform_to_disk (THREAD_ENTRY * thread_p,
				 HEAP_CACHE_ATTRINFO * attr_info,
				 RECDES * old_recdes, RECDES * new_recdes)
{
  return heap_attrinfo_transform_to_disk_internal (thread_p, attr_info,
						   old_recdes, new_recdes,
						   LOB_FLAG_INCLUDE_LOB);
}

/*
 * heap_attrinfo_transform_to_disk_except_lob () -
 *                           Transform to disk an attribute information
 *                           kind of instance. Do not create lob.
 *   return: SCAN_CODE
 *           (Either of S_SUCCESS, S_DOESNT_FIT,
 *                      S_ERROR)
 *   attr_info(in/out): The attribute information structure
 *   old_recdes(in): where the object's disk format is deposited
 *   new_recdes(in):
 *
 * Note: Transform the object represented by attr_info to disk format
 */
SCAN_CODE
heap_attrinfo_transform_to_disk_except_lob (THREAD_ENTRY * thread_p,
					    HEAP_CACHE_ATTRINFO * attr_info,
					    RECDES * old_recdes,
					    RECDES * new_recdes)
{
  return heap_attrinfo_transform_to_disk_internal (thread_p, attr_info,
						   old_recdes, new_recdes,
						   LOB_FLAG_EXCLUDE_LOB);
}

/*
 * heap_attrinfo_transform_to_disk_internal () -
 *                         Transform to disk an attribute information
 *                         kind of instance.
 *   return: SCAN_CODE
 *           (Either of S_SUCCESS, S_DOESNT_FIT,
 *                      S_ERROR)
 *   attr_info(in/out): The attribute information structure
 *   old_recdes(in): where the object's disk format is deposited
 *   new_recdes(in):
 *   lob_create_flag(in):
 *
 * Note: Transform the object represented by attr_info to disk format
 */
static SCAN_CODE
heap_attrinfo_transform_to_disk_internal (THREAD_ENTRY * thread_p,
					  HEAP_CACHE_ATTRINFO * attr_info,
					  RECDES * old_recdes,
					  RECDES * new_recdes,
					  int lob_create_flag)
{
  OR_BUF orep, *buf;
  char *ptr_bound, *ptr_varvals;
  HEAP_ATTRVALUE *value;	/* Disk value Attr info for a particular attr */
  DB_VALUE temp_dbvalue;
  PR_TYPE *pr_type;		/* Primitive type array function structure */
  unsigned int repid_bits;
  SCAN_CODE status;
  int i;
  DB_VALUE *dbvalue = NULL;
  int expected_size;
  int offset_size;

  /* check to make sure the attr_info has been used, it should not be empty. */
  if (attr_info->num_values == -1)
    {
      return S_ERROR;
    }

  /*
   * Get any of the values that have not been set/read
   */
  if (heap_attrinfo_set_uninitialized (thread_p, &attr_info->inst_oid,
				       old_recdes, attr_info) != NO_ERROR)
    {
      return S_ERROR;
    }

  /* Start transforming the dbvalues into disk values for the object */
  OR_BUF_INIT2 (orep, new_recdes->data, new_recdes->area_size);
  buf = &orep;

  expected_size = heap_attrinfo_get_disksize (attr_info, &offset_size);

  switch (_setjmp (buf->env))
    {
    case 0:
      status = S_SUCCESS;

      /*
       * Store the representation of the class along with bound bit
       * flag information
       */

      repid_bits = attr_info->last_classrepr->id;
      /*
       * Do we have fixed value attributes ?
       */
      if ((attr_info->last_classrepr->n_attributes
	   - attr_info->last_classrepr->n_variable) != 0)
	{
	  repid_bits |= OR_BOUND_BIT_FLAG;
	}

      /* offset size */
      OR_SET_VAR_OFFSET_SIZE (repid_bits, offset_size);

      or_put_int (buf, repid_bits);

      /*
       * Write CHN. We must increase the current value by one so that clients
       * can detect the change in object. That is, clients will need to
       * refetch the object.
       */

      attr_info->inst_chn++;
      or_put_int (buf, attr_info->inst_chn);

      /*
       * Calculate the pointer address to variable offset attribute table,
       * fixed attributes, and variable attributes
       */

      ptr_bound = OR_GET_BOUND_BITS (buf->buffer,
				     attr_info->last_classrepr->n_variable,
				     attr_info->last_classrepr->fixed_length);

      /*
       * Variable offset table is relative to the beginning of the buffer
       */

      ptr_varvals = ptr_bound +
	OR_BOUND_BIT_BYTES (attr_info->last_classrepr->n_attributes -
			    attr_info->last_classrepr->n_variable);

      /* Need to make sure that the bound array is not past the allocated
       * buffer because OR_ENABLE_BOUND_BIT() will just slam the bound
       * bit without checking the length.
       */

      if (ptr_varvals >= buf->endptr)
	{
	  new_recdes->length = -expected_size;	/* set to negative */
	  return S_DOESNT_FIT;
	}

      for (i = 0; i < attr_info->num_values; i++)
	{
	  value = &attr_info->values[i];
	  dbvalue = &value->dbvalue;
	  pr_type = value->last_attrepr->domain->type;
	  if (pr_type == NULL)
	    {
	      return S_ERROR;
	    }

	  /*
	   * Is this a fixed or variable attribute ?
	   */
	  if (value->last_attrepr->is_fixed != 0)
	    {
	      /*
	       * Fixed attribute
	       * Write the fixed attributes values, if unbound, does not matter
	       * what value is stored. We need to set the appropiate bit in the
	       * bound bit array for fixed attributes. For variable attributes,
	       */
	      buf->ptr =
		buf->buffer + OR_FIXED_ATTRIBUTES_OFFSET_BY_OBJ (buf->buffer,
								 attr_info->
								 last_classrepr->
								 n_variable) +
		value->last_attrepr->location;
	      if (value->do_increment)
		{
		  if (qdata_increment_dbval
		      (dbvalue, dbvalue, value->do_increment) != NO_ERROR)
		    {
		      status = S_ERROR;
		      break;
		    }
		}
	      if (dbvalue == NULL || db_value_is_null (dbvalue) == true)
		{
		  /*
		   * This is an unbound value.
		   *  1) Set any value in the fixed array value table, so we can
		   *     advance to next attribute.
		   *  2) and set the bound bit as unbound
		   */
		  db_value_domain_init (&temp_dbvalue,
					value->last_attrepr->type,
					value->last_attrepr->domain->
					precision,
					value->last_attrepr->domain->scale);
		  dbvalue = &temp_dbvalue;
		  OR_CLEAR_BOUND_BIT (ptr_bound,
				      value->last_attrepr->position);

		  /*
		   * pad the appropriate amount, writeval needs to be modified
		   * to accept a domain so it can perform this padding.
		   */
		  or_pad (buf,
			  tp_domain_disk_size (value->last_attrepr->domain));

		}
	      else
		{
		  /*
		   * Write the value.
		   */
		  OR_ENABLE_BOUND_BIT (ptr_bound,
				       value->last_attrepr->position);
		  (*(pr_type->data_writeval)) (buf, dbvalue);
		}
	    }
	  else
	    {
	      /*
	       * Variable attribute
	       *  1) Set the offset to this value in the variable offset table
	       *  2) Set the value in the variable value portion of the disk
	       *     object (Only if the value is bound)
	       */

	      /*
	       * Write the offset onto the variable offset table and remember
	       * the current pointer to the variable offset table
	       */

	      if (value->do_increment != 0)
		{
		  status = S_ERROR;
		  break;
		}
	      buf->ptr = (char *) (OR_VAR_ELEMENT_PTR (buf->buffer,
						       value->last_attrepr->
						       location));
	      or_put_offset_internal (buf,
				      CAST_BUFLEN (ptr_varvals - buf->buffer),
				      offset_size);

	      if (dbvalue != NULL && db_value_is_null (dbvalue) != true)
		{
		  /*
		   * Now write the value and remember the current pointer
		   * to variable value array for the next element.
		   */
		  buf->ptr = ptr_varvals;

		  if (lob_create_flag == LOB_FLAG_INCLUDE_LOB
		      && value->state == HEAP_WRITTEN_ATTRVALUE
		      && (pr_type->id == DB_TYPE_BLOB
			  || pr_type->id == DB_TYPE_CLOB))
		    {
		      DB_ELO dest_elo, *elo_p;
		      char *save_meta_data, *new_meta_data;
		      int error;

		      assert (db_value_type (dbvalue) == DB_TYPE_BLOB ||
			      db_value_type (dbvalue) == DB_TYPE_CLOB);

		      elo_p = db_get_elo (dbvalue);

		      if (elo_p == NULL)
			{
			  continue;
			}

		      new_meta_data = heap_get_class_name (thread_p,
							   &(attr_info->
							     class_oid));

		      if (new_meta_data == NULL)
			{
			  status = S_ERROR;
			  break;
			}
		      save_meta_data = elo_p->meta_data;
		      elo_p->meta_data = new_meta_data;
		      error = db_elo_copy (db_get_elo (dbvalue), &dest_elo);

		      free_and_init (elo_p->meta_data);
		      elo_p->meta_data = save_meta_data;

		      value->state = HEAP_WRITTEN_LOB_ATTRVALUE;

		      error = (error >= 0 ? NO_ERROR : error);
		      if (error == NO_ERROR)
			{
			  db_value_clear (dbvalue);
			  db_make_elo (dbvalue, pr_type->id, &dest_elo);
			  dbvalue->need_clear = true;
			}
		      else
			{
			  status = S_ERROR;
			  break;
			}
		    }

		  (*(pr_type->data_writeval)) (buf, dbvalue);
		  ptr_varvals = buf->ptr;
		}
	    }
	}

      if (attr_info->last_classrepr->n_variable > 0)
	{
	  /*
	   * The last element of the variable offset table points to the end of
	   * the object. The variable offset array starts with zero, so we can
	   * just access n_variable...
	   */

	  /* Write the offset to the end of the variable attributes table */
	  buf->ptr = ((char *)
		      (OR_VAR_ELEMENT_PTR (buf->buffer,
					   attr_info->last_classrepr->
					   n_variable)));
	  or_put_offset_internal (buf,
				  CAST_BUFLEN (ptr_varvals - buf->buffer),
				  offset_size);
	  buf->ptr = PTR_ALIGN (buf->ptr, INT_ALIGNMENT);
	}

      /* Record the length of the object */
      new_recdes->length = CAST_BUFLEN (ptr_varvals - buf->buffer);
      break;

      /*
       * if the longjmp status was anything other than ER_TF_BUFFER_OVERFLOW,
       * it represents an error condition and er_set will have been called
       */
    case ER_TF_BUFFER_OVERFLOW:

      status = S_DOESNT_FIT;

      if (lob_create_flag == LOB_FLAG_INCLUDE_LOB)
	{
	  for (i = 0; i < attr_info->num_values; i++)
	    {
	      value = &attr_info->values[i];
	      if (value->last_attrepr->type == DB_TYPE_BLOB ||
		  value->last_attrepr->type == DB_TYPE_CLOB)
		{
		  break;
		}
	    }

	  if (i < attr_info->num_values)
	    {
	      if (heap_attrinfo_delete_lob (thread_p, NULL, attr_info) !=
		  NO_ERROR)
		{
		  status = S_ERROR;
		  break;
		}
	    }
	}

      /*
       * Give a hint of the needed space. The hint is given as a negative
       * value in the record descriptor length. Make sure that this length
       * is larger than the current record descriptor area.
       */

      new_recdes->length = -expected_size;	/* set to negative */

      if (new_recdes->area_size > -new_recdes->length)
	{
	  /*
	   * This may be an error. The estimated disk size is smaller
	   * than the current record descriptor area size. For now assume
	   * at least 20% above the current area descriptor. The main problem
	   * is that heap_attrinfo_get_disksize () guess its size as much as
	   * possible
	   */
	  new_recdes->length = -(int) (new_recdes->area_size * 1.20);
	}
      break;

    default:
      status = S_ERROR;
      break;
    }

  return status;
}

/*
 * heap_attrinfo_start_refoids () - Initialize an attribute information structure
 * with attributes that may reference other objects
 *   return: NO_ERROR
 *   class_oid(in): The class identifier of the instances where values
 *                  attributes values are going to be read.
 *   attr_info(in/out): The attribute information structure
 *
 * Note: Initialize an attribute information structure with attributes
 * that may reference other objects (OIDs).
 *
 * Note: The caller must call heap_attrinfo_end after he is done with
 * attribute information.
 */

static int
heap_attrinfo_start_refoids (THREAD_ENTRY * thread_p, OID * class_oid,
			     HEAP_CACHE_ATTRINFO * attr_info)
{
  ATTR_ID guess_attrids[HEAP_GUESS_NUM_ATTRS_REFOIDS];
  ATTR_ID *set_attrids;
  int num_found_attrs;
  OR_CLASSREP *classrepr;
  int classrepr_cacheindex = -1;
  OR_ATTRIBUTE *search_attrepr;
  int i;
  int ret = NO_ERROR;

  attr_info->num_values = -1;

  /*
   * Find the current representation of the class, then scan all its
   * attributes finding the ones that may reference objects
   */

  classrepr = heap_classrepr_get (thread_p, class_oid, NULL, 0,
				  &classrepr_cacheindex, true);
  if (classrepr == NULL)
    {
      return ER_FAILED;
    }

  /*
   * Go over the list of attributes until the desired attributes (OIDs, sets)
   * are found
   */

  if (classrepr->n_attributes > HEAP_GUESS_NUM_ATTRS_REFOIDS)
    {
      set_attrids = (ATTR_ID *) malloc (classrepr->n_attributes
					* sizeof (ATTR_ID));
      if (set_attrids == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, classrepr->n_attributes * sizeof (ATTR_ID));
	  (void) heap_classrepr_free (classrepr, &classrepr_cacheindex);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
    }
  else
    {
      set_attrids = guess_attrids;
    }

  for (i = 0, num_found_attrs = 0; i < classrepr->n_attributes; i++)
    {
      search_attrepr = &classrepr->attributes[i];
      if (tp_domain_references_objects (search_attrepr->domain) == true)
	{
	  set_attrids[num_found_attrs++] = search_attrepr->id;
	}
    }

  ret = heap_attrinfo_start (thread_p, class_oid, num_found_attrs,
			     set_attrids, attr_info);

  if (set_attrids != guess_attrids)
    {
      free_and_init (set_attrids);
    }

  if (ret == NO_ERROR)
    {
      ret = heap_classrepr_free (classrepr, &classrepr_cacheindex);
    }
  else
    {
      (void) heap_classrepr_free (classrepr, &classrepr_cacheindex);
    }

  return ret;
}

/*
 * heap_attrinfo_start_with_index () -
 *   return:
 *   class_oid(in):
 *   class_recdes(in):
 *   attr_info(in):
 *   idx_info(in):
 */
int
heap_attrinfo_start_with_index (THREAD_ENTRY * thread_p, OID * class_oid,
				RECDES * class_recdes,
				HEAP_CACHE_ATTRINFO * attr_info,
				HEAP_IDX_ELEMENTS_INFO * idx_info)
{
  ATTR_ID guess_attrids[HEAP_GUESS_NUM_INDEXED_ATTRS];
  ATTR_ID *set_attrids;
  int num_found_attrs;
  OR_CLASSREP *classrepr = NULL;
  int classrepr_cacheindex = -1;
  OR_ATTRIBUTE *search_attrepr;
  int i, j;
  HEAP_ATTRVALUE *value;	/* Disk value Attr info for a particular attr */
  int *num_btids;
  OR_INDEX *indexp;

  idx_info->has_single_col = 0;
  idx_info->has_multi_col = 0;
  idx_info->num_btids = 0;

  num_btids = &idx_info->num_btids;

  set_attrids = guess_attrids;
  attr_info->num_values = -1;	/* initialize attr_info */

  classrepr = heap_classrepr_get (thread_p, class_oid, class_recdes,
				  0, &classrepr_cacheindex, true);
  if (classrepr == NULL)
    {
      return ER_FAILED;
    }

  if (classrepr->n_attributes > HEAP_GUESS_NUM_INDEXED_ATTRS)
    {
      set_attrids = (ATTR_ID *) malloc (classrepr->n_attributes
					* sizeof (ATTR_ID));
      if (set_attrids == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, classrepr->n_attributes * sizeof (ATTR_ID));
	  (void) heap_classrepr_free (classrepr, &classrepr_cacheindex);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
    }
  else
    {
      set_attrids = guess_attrids;
    }

  /*
   * Read the number of BTID's in this class
   */
  *num_btids = classrepr->n_indexes;

  for (j = 0; j < *num_btids; j++)
    {
      indexp = &classrepr->indexes[j];
      if (indexp->n_atts == 1)
	{
	  idx_info->has_single_col = 1;
	}
      else if (indexp->n_atts > 1)
	{
	  idx_info->has_multi_col = 1;
	}
      /* check for already found both */
      if (idx_info->has_single_col && idx_info->has_multi_col)
	{
	  break;
	}
    }

  /*
   * Go over the list of attrs until all indexed attributes (OIDs, sets)
   * are found
   */
  for (i = 0, num_found_attrs = 0, search_attrepr = classrepr->attributes;
       i < classrepr->n_attributes; i++, search_attrepr++)
    {
      if (search_attrepr->n_btids <= 0)
	{
	  continue;
	}

      if (idx_info->has_single_col)
	{
	  for (j = 0; j < *num_btids; j++)
	    {
	      indexp = &classrepr->indexes[j];
	      if (indexp->n_atts == 1
		  && indexp->atts[0]->id == search_attrepr->id)
		{
		  set_attrids[num_found_attrs++] = search_attrepr->id;
		  break;
		}
	    }
	}
    }				/* for (i = 0 ...) */

  if (idx_info->has_multi_col == 0 && num_found_attrs == 0)
    {
      /* initialize the attrinfo cache and return, there is nothing else to do */
      /* (void) memset(attr_info, '\0', sizeof (HEAP_CACHE_ATTRINFO)); */

      /* now set the num_values to -1 which indicates that this is an
       * empty HEAP_CACHE_ATTRINFO and shouldn't be operated on.
       */
      attr_info->num_values = -1;

      /* free the class representation */
      (void) heap_classrepr_free (classrepr, &classrepr_cacheindex);
    }
  else
    {				/* num_found_attrs > 0 */
      /* initialize attribute information */
      attr_info->class_oid = *class_oid;
      attr_info->last_cacheindex = classrepr_cacheindex;
      attr_info->read_cacheindex = -1;
      attr_info->last_classrepr = classrepr;
      attr_info->read_classrepr = NULL;
      OID_SET_NULL (&attr_info->inst_oid);
      attr_info->inst_chn = NULL_CHN;
      attr_info->num_values = num_found_attrs;

      if (num_found_attrs <= 0)
	{
	  attr_info->values = NULL;
	}
      else
	{
	  attr_info->values = (HEAP_ATTRVALUE *)
	    db_private_alloc (thread_p,
			      (num_found_attrs * sizeof (HEAP_ATTRVALUE)));
	  if (attr_info->values == NULL)
	    {
	      /* free the class representation */
	      (void) heap_classrepr_free (classrepr, &classrepr_cacheindex);
	      attr_info->num_values = -1;
	      goto error;
	    }
	}

      /*
       * Set the attribute identifier of the desired attributes in the value
       * attribute information, and indicates that the current value is
       * unitialized. That is, it has not been read, set or whatever.
       */
      for (i = 0; i < attr_info->num_values; i++)
	{
	  value = &attr_info->values[i];
	  value->attrid = set_attrids[i];
	  value->state = HEAP_UNINIT_ATTRVALUE;
	  value->last_attrepr = NULL;
	  value->read_attrepr = NULL;
	}

      /*
       * Make last information to be recached for each individual attribute
       * value. Needed for WRITE and Default values
       */
      if (heap_attrinfo_recache_attrepr (attr_info, true) != NO_ERROR)
	{
	  /* classrepr will be freed in heap_attrinfo_end */
	  heap_attrinfo_end (thread_p, attr_info);
	  goto error;
	}
    }

  if (set_attrids != guess_attrids)
    {
      free_and_init (set_attrids);
    }

  if (num_found_attrs == 0 && idx_info->has_multi_col)
    {
      return 1;
    }
  else
    {
      return num_found_attrs;
    }

  /* **** */
error:

  if (set_attrids != guess_attrids)
    {
      free_and_init (set_attrids);
    }

  return ER_FAILED;
}

/*
 * heap_classrepr_find_index_id () - Find the indicated index ID from the class repr
 *   return: ID of desired index ot -1 if an error occurred.
 *   classrepr(in): The class representation.
 *   btid(in): The BTID of the interested index.
 *
 * Note: Locate the desired index by matching it with the passed BTID.
 * Return the ID of the index if found.
 */
static int
heap_classrepr_find_index_id (OR_CLASSREP * classrepr, BTID * btid)
{
  int i;
  int id = -1;

  for (i = 0; i < classrepr->n_indexes; i++)
    {
      if (BTID_IS_EQUAL (&(classrepr->indexes[i].btid), btid))
	{
	  id = i;
	  break;
	}
    }

  return id;
}

/*
 * heap_attrinfo_start_with_btid () - Initialize an attribute information structure
 *   return: ID for the index which corresponds to the passed BTID.
 *           If an error occurred, a -1 is returned.
 *   class_oid(in): The class identifier of the instances where values
 *                  attributes values are going to be read.
 *   btid(in): The BTID of the interested index.
 *   attr_info(in/out): The attribute information structure
 *
 * Note: Initialize an attribute information structure, so that values
 * of instances can be retrieved based on the desired attributes.
 *
 * There are currently three functions which can be used to
 * initialize the attribute information structure; heap_attrinfo_start(),
 * heap_attrinfo_start_with_index() and this one.  This function determines
 * which attributes belong to the passed BTID and populate the
 * information structure on those attributes.
 *
 * The attrinfo structure is an structure where values of
 * instances can be read. For example an object is retrieved,
 * then some of its attributes are convereted to dbvalues and
 * placed in this structure.
 *
 * Note: The caller must call heap_attrinfo_end after he is done with
 * attribute information.
 */
int
heap_attrinfo_start_with_btid (THREAD_ENTRY * thread_p, OID * class_oid,
			       BTID * btid, HEAP_CACHE_ATTRINFO * attr_info)
{
  ATTR_ID guess_attrids[HEAP_GUESS_NUM_INDEXED_ATTRS];
  ATTR_ID *set_attrids;
  OR_CLASSREP *classrepr = NULL;
  int i;
  int index_id = -1;
  int classrepr_cacheindex = -1;
  int num_found_attrs = 0;

  /*
   *  We'll start by assuming that the number of attributes will fit into
   *  the preallocated array.
   */
  set_attrids = guess_attrids;

  attr_info->num_values = -1;	/* initialize attr_info */

  /*
   *  Get the class representation so that we can access the indexes.
   */
  classrepr = heap_classrepr_get (thread_p, class_oid, NULL, 0,
				  &classrepr_cacheindex, true);
  if (classrepr == NULL)
    {
      goto error;
    }

  /*
   *  Get the index ID which corresponds to the BTID
   */
  index_id = heap_classrepr_find_index_id (classrepr, btid);
  if (index_id == -1)
    {
      goto error;
    }

  /*
   *  Get the number of attributes associated with this index.
   *  Allocate a new attribute ID array if we have more attributes
   *  than will fit in the pre-allocated array.
   *  Fill the array with the attribute ID's
   *  Free the class representation.
   */
  num_found_attrs = classrepr->indexes[index_id].n_atts;
  if (num_found_attrs > HEAP_GUESS_NUM_INDEXED_ATTRS)
    {
      set_attrids = (ATTR_ID *) malloc (num_found_attrs * sizeof (ATTR_ID));
      if (set_attrids == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_OUT_OF_VIRTUAL_MEMORY, 1,
		  num_found_attrs * sizeof (ATTR_ID));
	  goto error;
	}
    }

  for (i = 0; i < num_found_attrs; i++)
    {
      set_attrids[i] = classrepr->indexes[index_id].atts[i]->id;
    }

  (void) heap_classrepr_free (classrepr, &classrepr_cacheindex);
  classrepr = NULL;

  /*
   *  Get the attribute information for the collected ID's
   */
  if (num_found_attrs > 0)
    {
      if (heap_attrinfo_start
	  (thread_p, class_oid, num_found_attrs, set_attrids,
	   attr_info) != NO_ERROR)
	{
	  goto error;
	}
    }

  /*
   *  Free the attribute ID array if it was dynamically allocated
   */
  if (set_attrids != guess_attrids)
    {
      free_and_init (set_attrids);
    }

  return index_id;

  /* **** */
error:

  if (classrepr)
    {
      (void) heap_classrepr_free (classrepr, &classrepr_cacheindex);
    }

  if (set_attrids != guess_attrids)
    {
      free_and_init (set_attrids);
    }

  return ER_FAILED;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * heap_attrvalue_get_index () -
 *   return:
 *   value_index(in):
 *   attrid(in):
 *   n_btids(in):
 *   btids(in):
 *   idx_attrinfo(in):
 */
DB_VALUE *
heap_attrvalue_get_index (int value_index, ATTR_ID * attrid,
			  int *n_btids, BTID ** btids,
			  HEAP_CACHE_ATTRINFO * idx_attrinfo)
{
  HEAP_ATTRVALUE *value;	/* Disk value Attr info for a particular attr   */

  /* check to make sure the idx_attrinfo has been used, it should never
   * be empty.
   */
  if (idx_attrinfo->num_values == -1)
    {
      return NULL;
    }

  if (value_index > idx_attrinfo->num_values || value_index < 0)
    {
      *n_btids = 0;
      *btids = NULL;
      *attrid = NULL_ATTRID;
      return NULL;
    }
  else
    {
      value = &idx_attrinfo->values[value_index];
      *n_btids = value->last_attrepr->n_btids;
      *btids = value->last_attrepr->btids;
      *attrid = value->attrid;
      return &value->dbvalue;
    }

}
#endif

/*
 * heap_midxkey_key_get () -
 *   return:
 *   recdes(in):
 *   midxkey(in):
 *   index(in):
 *   attrinfo(in):
 */
static DB_MIDXKEY *
heap_midxkey_key_get (RECDES * recdes, DB_MIDXKEY * midxkey,
		      OR_INDEX * index, HEAP_CACHE_ATTRINFO * attrinfo,
		      DB_VALUE * func_res)
{
  char *nullmap_ptr;
  OR_ATTRIBUTE **atts;
  int num_atts, i, k;
  DB_VALUE value;
  OR_BUF buf;
  int error = NO_ERROR;

  num_atts = index->n_atts;
  atts = index->atts;
  if (func_res)
    {
      num_atts = index->func_index_info->attr_index_start + 1;
    }
  assert (PTR_ALIGN (midxkey->buf, INT_ALIGNMENT) == midxkey->buf);

  or_init (&buf, midxkey->buf, -1);

  nullmap_ptr = midxkey->buf;
  or_advance (&buf, pr_midxkey_init_boundbits (nullmap_ptr, num_atts));
  k = 0;
  for (i = 0; i < num_atts && k < num_atts; i++)
    {
      if (index->func_index_info && (i == index->func_index_info->col_id))
	{
	  if (!db_value_is_null (func_res))
	    {
	      TP_DOMAIN *domain =
		tp_domain_resolve_default (func_res->domain.general_info.
					   type);
	      (*(domain->type->index_writeval)) (&buf, func_res);
	      OR_ENABLE_BOUND_BIT (nullmap_ptr, k);
	    }
	  k++;
	}
      if (k == num_atts)
	{
	  break;
	}
      error = heap_midxkey_get_value (recdes, atts[i], &value, attrinfo);
      if (error == NO_ERROR && !db_value_is_null (&value))
	{
	  (*(atts[i]->domain->type->index_writeval)) (&buf, &value);
	  OR_ENABLE_BOUND_BIT (nullmap_ptr, k);
	}
      k++;
    }

  midxkey->size = CAST_BUFLEN (buf.ptr - buf.buffer);
  midxkey->ncolumns = num_atts;
  midxkey->domain = NULL;

  return midxkey;
}

/*
 * heap_midxkey_key_generate () -
 *   return:
 *   recdes(in):
 *   midxkey(in):
 *   att_ids(in):
 *   attrinfo(in):
 */
static DB_MIDXKEY *
heap_midxkey_key_generate (THREAD_ENTRY * thread_p, RECDES * recdes,
			   DB_MIDXKEY * midxkey, int *att_ids,
			   HEAP_CACHE_ATTRINFO * attrinfo,
			   DB_VALUE * func_res, int func_col_id,
			   int func_attr_index_start)
{
  char *nullmap_ptr;
  int num_vals, i, reprid, k;
  OR_ATTRIBUTE *att;
  DB_VALUE value;
  OR_BUF buf;
  int error = NO_ERROR;

  /*
   * Make sure that we have the needed cached representation.
   */

  if (recdes != NULL)
    {
      reprid = or_rep_id (recdes);

      if (attrinfo->read_classrepr == NULL ||
	  attrinfo->read_classrepr->id != reprid)
	{
	  /* Get the needed representation */
	  if (heap_attrinfo_recache (thread_p, reprid, attrinfo) != NO_ERROR)
	    {
	      return NULL;
	    }
	}
    }

  assert (PTR_ALIGN (midxkey->buf, INT_ALIGNMENT) == midxkey->buf);

  or_init (&buf, midxkey->buf, -1);

  nullmap_ptr = midxkey->buf;

  /* On constructing index */
  num_vals = attrinfo->num_values;
  if (func_res)
    {
      num_vals = func_attr_index_start + 1;
    }
  or_advance (&buf, pr_midxkey_init_boundbits (nullmap_ptr, num_vals));
  k = 0;
  for (i = 0; i < num_vals && k < num_vals; i++)
    {
      if (i == func_col_id)
	{
	  if (!db_value_is_null (func_res))
	    {
	      TP_DOMAIN *domain =
		tp_domain_resolve_default (func_res->domain.general_info.
					   type);
	      (*(domain->type->index_writeval)) (&buf, func_res);
	      OR_ENABLE_BOUND_BIT (nullmap_ptr, k);
	    }
	  k++;
	}
      if (k == num_vals)
	{
	  break;
	}
      att = heap_locate_attribute (att_ids[i], attrinfo);

      error = heap_midxkey_get_value (recdes, att, &value, attrinfo);
      if (error == NO_ERROR && !db_value_is_null (&value))
	{
	  (*(att->domain->type->index_writeval)) (&buf, &value);
	  OR_ENABLE_BOUND_BIT (nullmap_ptr, k);
	}
      k++;
    }

  midxkey->size = CAST_BUFLEN (buf.ptr - buf.buffer);
  midxkey->ncolumns = num_vals;
  midxkey->domain = NULL;

  return midxkey;
}

/*
 * heap_attrinfo_generate_key () - Generate a key from the attribute information.
 *   return: Pointer to DB_VALUE containing the key.
 *   n_atts(in): Size of attribute ID array.
 *   att_ids(in): Array of attribute ID's
 *   attr_info(in): Pointer to attribute information structure.  This
 *                  structure contains the BTID's, the attributes and their
 *                  values.
 *   recdes(in):
 *   db_valuep(in): Pointer to a DB_VALUE.  This db_valuep will be used to
 *                  contain the set key in the case of multi-column B-trees.
 *                  It is ignored for single-column B-trees.
 *   buf(in):
 *
 * Note: Return a key for the specified attribute ID's
 *
 * If n_atts=1, the key will be the value of that attribute
 * and we will return a pointer to that DB_VALUE.
 *
 * If n_atts>1, the key will be a sequence of the attribute
 * values.  The set will be constructed and contained with
 * the passed DB_VALUE.  A pointer to this DB_VALUE is returned.
 *
 * It is important for the caller to deallocate this memory
 * by calling pr_clear_value().
 */
DB_VALUE *
heap_attrinfo_generate_key (THREAD_ENTRY * thread_p, int n_atts, int *att_ids,
			    int *atts_prefix_length,
			    HEAP_CACHE_ATTRINFO * attr_info, RECDES * recdes,
			    DB_VALUE * db_valuep, char *buf,
			    FUNCTION_INDEX_INFO * func_index_info)
{
  DB_VALUE *ret_valp;
  DB_VALUE *fi_res = NULL;
  int fi_attr_index_start = -1;
  int fi_col_id = -1;

  assert (DB_IS_NULL (db_valuep));

  if (func_index_info)
    {
      fi_attr_index_start = func_index_info->attr_index_start;
      fi_col_id = func_index_info->col_id;
      if (heap_eval_function_index (thread_p, func_index_info, n_atts,
				    att_ids, attr_info, recdes, -1, db_valuep,
				    NULL) != NO_ERROR)
	{
	  return NULL;
	}
      fi_res = db_valuep;
    }

  /*
   *  Multi-column index.  The key is a sequence of the attribute values.
   *  Return a pointer to the attributes DB_VALUE.
   */
  if ((n_atts > 1 && func_index_info == NULL)
      || (func_index_info && (func_index_info->attr_index_start + 1) > 1))
    {
      DB_MIDXKEY midxkey;
      int midxkey_size = recdes->length;

      if (func_index_info != NULL)
	{
	  /* this will allocate more than it is needed to store the key, but
	     there is no decent way to calculate the correct size */
	  midxkey_size += OR_VALUE_ALIGNED_SIZE (fi_res);
	}

      /* Allocate storage for the buf of midxkey */
      if (midxkey_size > DBVAL_BUFSIZE)
	{
	  midxkey.buf = db_private_alloc (thread_p, midxkey_size);
	  if (midxkey.buf == NULL)
	    {
	      return NULL;
	    }
	}
      else
	{
	  midxkey.buf = buf;
	}

      if (heap_midxkey_key_generate (thread_p, recdes, &midxkey,
				     att_ids, attr_info, fi_res,
				     fi_col_id, fi_attr_index_start) == NULL)
	{
	  return NULL;
	}

      (void) pr_clear_value (db_valuep);

      DB_MAKE_MIDXKEY (db_valuep, &midxkey);

      if (midxkey_size > DBVAL_BUFSIZE)
	{
	  db_valuep->need_clear = true;
	}

      ret_valp = db_valuep;
    }
  else
    {
      /*
       *  Single-column index.  The key is simply the value of the attribute.
       *  Return a pointer to the attributes DB_VALUE.
       */
      if (func_index_info)
	{
	  ret_valp = db_valuep;
	  return ret_valp;
	}

      ret_valp = heap_attrinfo_access (att_ids[0], attr_info);
      if (ret_valp != NULL && atts_prefix_length && n_atts == 1)
	{
	  if (*atts_prefix_length != -1
	      && QSTR_IS_ANY_CHAR_OR_BIT (DB_VALUE_DOMAIN_TYPE (ret_valp)))
	    {
	      /* prefix index */
	      pr_clone_value (ret_valp, db_valuep);
	      db_string_truncate (db_valuep, *atts_prefix_length);
	      ret_valp = db_valuep;
	    }
	}
    }

  return ret_valp;
}

/*
 * heap_attrvalue_get_key () - Get B-tree key from attribute value(s)
 *   return: Pointer to DB_VALUE containing the key.
 *   btid_index(in): Index into an array of BTID's from the OR_CLASSREP
 *                   structure contained in idx_attrinfo.
 *   idx_attrinfo(in): Pointer to attribute information structure.  This
 *                     structure contains the BTID's, the attributes and their
 *                     values.
 *   recdes(in):
 *   btid(in): Pointer to a BTID.  The value of the current BTID
 *             will be returned.
 *   db_value(in): Pointer to a DB_VALUE.  This db_value will be used to
 *                 contain the set key in the case of multi-column B-trees.
 *                 It is ignored for single-column B-trees.
 *   buf(in):
 *   func_preds(in): cached function index expressions
 *
 * Note: Return a B-tree key for the specified B-tree ID.
 *
 * If the specified B-tree ID is associated with a single
 * attribute the key will be the value of that attribute
 * and we will return a pointer to that DB_VALUE.
 *
 * If the BTID is associated with multiple attributes the
 * key will be a set containing the values of the attributes.
 * The set will be constructed and contained within the
 * passed DB_VALUE.  A pointer to this DB_VALUE is returned.
 * It is important for the caller to deallocate this memory
 * by calling pr_clear_value().
 */
DB_VALUE *
heap_attrvalue_get_key (THREAD_ENTRY * thread_p, int btid_index,
			HEAP_CACHE_ATTRINFO * idx_attrinfo, RECDES * recdes,
			BTID * btid, DB_VALUE * db_value, char *buf,
			FUNC_PRED_UNPACK_INFO * func_indx_pred)
{
  OR_INDEX *index;
  int n_atts, reprid;
  DB_VALUE *ret_val = NULL;
  DB_VALUE *fi_res = NULL;

  assert (DB_IS_NULL (db_value));

  /*
   *  check to make sure the idx_attrinfo has been used, it should
   *  never be empty.
   */
  if ((idx_attrinfo->num_values == -1) ||
      (btid_index >= idx_attrinfo->last_classrepr->n_indexes))
    {
      return NULL;
    }

  /*
   * Make sure that we have the needed cached representation.
   */
  if (recdes != NULL)
    {
      reprid = or_rep_id (recdes);

      if (idx_attrinfo->read_classrepr == NULL ||
	  idx_attrinfo->read_classrepr->id != reprid)
	{
	  /* Get the needed representation */
	  if (heap_attrinfo_recache (thread_p, reprid, idx_attrinfo) !=
	      NO_ERROR)
	    {
	      return NULL;
	    }
	}
    }

  index = &(idx_attrinfo->last_classrepr->indexes[btid_index]);
  n_atts = index->n_atts;
  *btid = index->btid;

  /* is function index */
  if (index->func_index_info)
    {
      if (heap_eval_function_index (thread_p, NULL, -1, NULL, idx_attrinfo,
				    recdes, btid_index, db_value,
				    func_indx_pred) != NO_ERROR)
	{
	  return NULL;
	}
      fi_res = db_value;
    }

  /*
   *  Multi-column index.  Construct the key as a sequence of attribute
   *  values.  The sequence is contained in the passed DB_VALUE.  A
   *  pointer to this DB_VALUE is returned.
   */
  if ((n_atts > 1 && recdes != NULL && index->func_index_info == NULL)
      || (index->func_index_info
	  && (index->func_index_info->attr_index_start + 1) > 1))
    {
      DB_MIDXKEY midxkey;
      int midxkey_size = recdes->length;

      if (index->func_index_info != NULL)
	{
	  /* this will allocate more than it is needed to store the key, but
	     there is no decent way to calculate the correct size */
	  midxkey_size += OR_VALUE_ALIGNED_SIZE (fi_res);
	}

      /* Allocate storage for the buf of midxkey */
      if (midxkey_size > DBVAL_BUFSIZE)
	{
	  midxkey.buf = db_private_alloc (thread_p, midxkey_size);
	  if (midxkey.buf == NULL)
	    {
	      return NULL;
	    }
	}
      else
	{
	  midxkey.buf = buf;
	}

      if (heap_midxkey_key_get (recdes, &midxkey, index, idx_attrinfo,
				fi_res) == NULL)
	{
	  return NULL;
	}

      (void) pr_clear_value (db_value);

      DB_MAKE_MIDXKEY (db_value, &midxkey);

      if (midxkey_size > DBVAL_BUFSIZE)
	{
	  db_value->need_clear = true;
	}

      ret_val = db_value;
    }
  else
    {
      /*
       *  Single-column index.  The key is simply the value of the attribute.
       *  Return a pointer to the attributes DB_VALUE.
       */

      /* Find the matching attribute identified by the attribute ID */
      if (fi_res)
	{
	  ret_val = fi_res;
	  return ret_val;
	}
      ret_val = heap_attrinfo_access (index->atts[0]->id, idx_attrinfo);

      if (ret_val != NULL && index->attrs_prefix_length != NULL
	  && index->attrs_prefix_length[0] != -1)
	{
	  if (QSTR_IS_ANY_CHAR_OR_BIT (DB_VALUE_DOMAIN_TYPE (ret_val)))
	    {
	      pr_clone_value (ret_val, db_value);
	      db_string_truncate (db_value, index->attrs_prefix_length[0]);
	      ret_val = db_value;
	    }
	}
    }

  return ret_val;
}

/*
 * heap_indexinfo_get_btid () -
 *   return:
 *   btid_index(in):
 *   attrinfo(in):
 */
BTID *
heap_indexinfo_get_btid (int btid_index, HEAP_CACHE_ATTRINFO * attrinfo)
{
  if (btid_index != -1 && btid_index < attrinfo->last_classrepr->n_indexes)
    {
      return &(attrinfo->last_classrepr->indexes[btid_index].btid);
    }
  else
    {
      return NULL;
    }
}

/*
 * heap_indexinfo_get_num_attrs () -
 *   return:
 *   btid_index(in):
 *   attrinfo(in):
 */
int
heap_indexinfo_get_num_attrs (int btid_index, HEAP_CACHE_ATTRINFO * attrinfo)
{
  if (btid_index != -1 && btid_index < attrinfo->last_classrepr->n_indexes)
    {
      return attrinfo->last_classrepr->indexes[btid_index].n_atts;
    }
  else
    {
      return 0;
    }
}

/*
 * heap_indexinfo_get_attrids () -
 *   return: NO_ERROR
 *   btid_index(in):
 *   attrinfo(in):
 *   attrids(in):
 */
int
heap_indexinfo_get_attrids (int btid_index, HEAP_CACHE_ATTRINFO * attrinfo,
			    ATTR_ID * attrids)
{
  int i;
  int ret = NO_ERROR;

  if (btid_index != -1 && (btid_index < attrinfo->last_classrepr->n_indexes))
    {
      for (i = 0; i < attrinfo->last_classrepr->indexes[btid_index].n_atts;
	   i++)
	{
	  attrids[i] =
	    attrinfo->last_classrepr->indexes[btid_index].atts[i]->id;
	}
    }

  return ret;
}

/*
 * heap_indexinfo_get_attrs_prefix_length () -
 *   return: NO_ERROR
 *   btid_index(in):
 *   attrinfo(in):
 *   keys_prefix_length(in/out):
 */
int
heap_indexinfo_get_attrs_prefix_length (int btid_index,
					HEAP_CACHE_ATTRINFO * attrinfo,
					int *attrs_prefix_length,
					int len_attrs_prefix_length)
{
  int i, length = -1;
  int ret = NO_ERROR;

  if (attrs_prefix_length && len_attrs_prefix_length > 0)
    {
      for (i = 0; i < len_attrs_prefix_length; i++)
	{
	  attrs_prefix_length[i] = -1;
	}
    }

  if (btid_index != -1 && (btid_index < attrinfo->last_classrepr->n_indexes))
    {
      if (attrinfo->last_classrepr->indexes[btid_index].attrs_prefix_length &&
	  attrs_prefix_length)
	{
	  length = MIN (attrinfo->last_classrepr->indexes[btid_index].n_atts,
			len_attrs_prefix_length);
	  for (i = 0; i < length; i++)
	    {
	      attrs_prefix_length[i] =
		attrinfo->last_classrepr->indexes[btid_index].
		attrs_prefix_length[i];
	    }
	}
    }

  return ret;
}

/*
 * heap_get_index_with_name () - get BTID of index with name index_name
 * return : error code or NO_ERROR
 * thread_p (in) :
 * class_oid (in) : class OID
 * index_name (in): index name
 * btid (in/out)  : btid
 */
int
heap_get_index_with_name (THREAD_ENTRY * thread_p, OID * class_oid,
			  const char *index_name, BTID * btid)
{
  OR_CLASSREP *classrep = NULL;
  OR_INDEX *indexp = NULL;
  int idx_in_cache, i;
  int error = NO_ERROR;

  BTID_SET_NULL (btid);

  /* get the class representation so that we can access the indexes */
  classrep = heap_classrepr_get (thread_p, class_oid, NULL, 0,
				 &idx_in_cache, true);
  if (classrep == NULL)
    {
      return ER_FAILED;
    }

  for (i = 0; i < classrep->n_indexes; i++)
    {
      if (strcasecmp (classrep->indexes[i].btname, index_name) == 0)
	{
	  BTID_COPY (btid, &classrep->indexes[i].btid);
	  break;
	}
    }
  if (classrep != NULL)
    {
      error = heap_classrepr_free (classrep, &idx_in_cache);
    }
  return error;
}

/*
 * heap_get_indexinfo_of_btid () -
 *   return: NO_ERROR
 *   class_oid(in):
 *   btid(in):
 *   type(in):
 *   num_attrs(in):
 *   attr_ids(in):
 *   btnamepp(in);
 */
int
heap_get_indexinfo_of_btid (THREAD_ENTRY * thread_p, OID * class_oid,
			    BTID * btid, BTREE_TYPE * type, int *num_attrs,
			    ATTR_ID ** attr_ids,
			    int **attrs_prefix_length, char **btnamepp,
			    int *func_index_col_id)
{
  OR_CLASSREP *classrepp;
  OR_INDEX *indexp;
  int idx_in_cache, i, n = 0;
  int idx;
  int ret = NO_ERROR;

  /* initial value of output parameters */
  if (num_attrs)
    {
      *num_attrs = 0;
    }

  if (attr_ids)
    {
      *attr_ids = NULL;
    }

  if (btnamepp)
    {
      *btnamepp = NULL;
    }

  if (attrs_prefix_length)
    {
      *attrs_prefix_length = NULL;
    }

  if (func_index_col_id)
    {
      *func_index_col_id = -1;
    }

  /* get the class representation so that we can access the indexes */
  classrepp = heap_classrepr_get (thread_p, class_oid, NULL, 0,
				  &idx_in_cache, true);
  if (classrepp == NULL)
    {
      goto exit_on_error;
    }

  /* get the idx of the index which corresponds to the BTID */
  idx = heap_classrepr_find_index_id (classrepp, btid);
  if (idx < 0)
    {
      goto exit_on_error;
    }
  indexp = &classrepp->indexes[idx];

  /* get the type of this index */
  if (type)
    {
      *type = indexp->type;
    }

  /* get the number of attributes associated with this index */
  if (num_attrs)
    {
      *num_attrs = n = indexp->n_atts;
    }
  /* allocate a new attribute ID array */
  if (attr_ids)
    {
      *attr_ids =
	(ATTR_ID *) db_private_alloc (thread_p, n * sizeof (ATTR_ID));

      if (*attr_ids == NULL)
	{
	  goto exit_on_error;
	}

      /* fill the array with the attribute ID's */
      for (i = 0; i < n; i++)
	{
	  (*attr_ids)[i] = indexp->atts[i]->id;
	}
    }

  if (btnamepp)
    {
      *btnamepp = strdup (indexp->btname);
    }

  if (attrs_prefix_length && indexp->type == BTREE_INDEX)
    {
      *attrs_prefix_length =
	(int *) db_private_alloc (thread_p, n * sizeof (int));

      if (*attrs_prefix_length == NULL)
	{
	  goto exit_on_error;
	}

      for (i = 0; i < n; i++)
	{
	  if (indexp->attrs_prefix_length != NULL)
	    {
	      (*attrs_prefix_length)[i] = indexp->attrs_prefix_length[i];
	    }
	  else
	    {
	      (*attrs_prefix_length)[i] = -1;
	    }
	}
    }

  if (func_index_col_id && indexp->func_index_info)
    {
      *func_index_col_id = indexp->func_index_info->col_id;
    }

  /* free the class representation */
  ret = heap_classrepr_free (classrepp, &idx_in_cache);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  return ret;

exit_on_error:

  if (attr_ids && *attr_ids)
    {
      db_private_free_and_init (thread_p, *attr_ids);
    }

  if (btnamepp && *btnamepp)
    {
      free_and_init (*btnamepp);
    }

  if (attrs_prefix_length)
    {
      if (*attrs_prefix_length)
	{
	  db_private_free_and_init (thread_p, *attrs_prefix_length);
	}
      *attrs_prefix_length = NULL;
    }

  if (classrepp)
    {
      (void) heap_classrepr_free (classrepp, &idx_in_cache);
    }

  return (ret == NO_ERROR
	  && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
}

/*
 * heap_get_referenced_by () - Find objects referenced by given object
 *   return: int (object count or -1)
 *   class_oid(in):
 *   obj_oid(in): The object identifier
 *   recdes(in): Object disk representation
 *   max_oid_cnt(in/out): Size of OID list in OIDs
 *   oid_list(in): Set to the array of referenced OIDs
 *                 (This area can be realloc, thus, it should have been
 *                 with malloc)
 *
 * Note: This function finds object identifiers referenced by the
 * given instance. If OID references are stored in the given
 * OID list. If the oid_list is not large enough to hold the
 * number of instances, the area (i.e., oid_list) is expanded
 * using realloc. The number of OID references is returned by the
 * function.
 *
 * Note: The oid_list pointer should be freed by the caller.
 * Note: Nested-sets, that is, set-of-sets inside the object are not traced.
 * Note: This function does not remove duplicate oids from the list, the
 * caller is responsible for checking and removing them if needed.
 */
int
heap_get_referenced_by (THREAD_ENTRY * thread_p, OID * class_oid,
			const OID * obj_oid, RECDES * recdes,
			int *max_oid_cnt, OID ** oid_list)
{
  HEAP_CACHE_ATTRINFO attr_info;
  DB_TYPE dbtype;
  HEAP_ATTRVALUE *value;	/* Disk value Attr info for a particular attr */
  DB_VALUE db_value;
  DB_SET *set;
  OID *oid_ptr;			/* iterator on oid_list    */
  OID *attr_oid;
  int oid_cnt;			/* number of OIDs fetched  */
  int cnt;			/* set element count       */
  int new_max_oid;
  int i, j;			/* loop counters           */

  /*
   * We don't support class references in this function
   */
  if (oid_is_root (class_oid))
    {
      return 0;
    }

  if ((heap_attrinfo_start_refoids (thread_p, class_oid,
				    &attr_info) != NO_ERROR)
      || heap_attrinfo_read_dbvalues (thread_p, obj_oid, recdes,
				      &attr_info) != NO_ERROR)
    {
      goto error;
    }

  if (*oid_list == NULL)
    {
      *max_oid_cnt = 0;
    }
  else if (*max_oid_cnt <= 0)
    {
      /*
       * We better release oid_list since we do not know it size. This may
       * be a bug.
       */
      free_and_init (*oid_list);
      *max_oid_cnt = 0;
    }

  /*
   * Now start searching the attributes that may reference objects
   */
  oid_cnt = 0;
  oid_ptr = *oid_list;

  for (i = 0; i < attr_info.num_values; i++)
    {
      value = &attr_info.values[i];
      dbtype = db_value_type (&value->dbvalue);
      if (dbtype == DB_TYPE_OID
	  && !db_value_is_null (&value->dbvalue)
	  && (attr_oid = db_get_oid (&value->dbvalue)) != NULL
	  && !OID_ISNULL (attr_oid))
	{
	  /*
	   * A simple attribute with reference an object (OID)
	   */
	  if (oid_cnt == *max_oid_cnt)
	    {
	      /*
	       * We need to expand the area to deposit more OIDs.
	       * Use 50% of the current size for expansion and at least 10 OIDs
	       */
	      if (*max_oid_cnt <= 0)
		{
		  *max_oid_cnt = 0;
		  new_max_oid = attr_info.num_values;
		}
	      else
		{
		  new_max_oid = (int) (*max_oid_cnt * 1.5) + 1;
		  if (new_max_oid < attr_info.num_values)
		    {
		      new_max_oid = attr_info.num_values;
		    }
		}

	      if (new_max_oid < 10)
		{
		  new_max_oid = 10;
		}

	      oid_ptr = (OID *) realloc (*oid_list,
					 new_max_oid * sizeof (OID));
	      if (oid_ptr == NULL)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_OUT_OF_VIRTUAL_MEMORY, 1,
			  new_max_oid * sizeof (OID));
		  goto error;
		}

	      /*
	       * Set the pointers and advance to current area pointer
	       */
	      *oid_list = oid_ptr;
	      oid_ptr += *max_oid_cnt;
	      *max_oid_cnt = new_max_oid;
	    }
	  *oid_ptr = *attr_oid;
	  oid_ptr++;
	  oid_cnt++;
	}
      else
	{
	  if (TP_IS_SET_TYPE (dbtype))
	    {
	      /*
	       * A set which may or may nor reference objects (OIDs)
	       * Go through each element of the set
	       */

	      set = db_get_set (&value->dbvalue);
	      cnt = db_set_size (set);

	      for (j = 0; j < cnt; j++)
		{
		  if (db_set_get (set, j, &db_value) != NO_ERROR)
		    {
		      goto error;
		    }

		  dbtype = db_value_type (&db_value);
		  if (dbtype == DB_TYPE_OID && !db_value_is_null (&db_value)
		      && (attr_oid = db_get_oid (&db_value)) != NULL
		      && !OID_ISNULL (attr_oid))
		    {
		      if (oid_cnt == *max_oid_cnt)
			{
			  /*
			   * We need to expand the area to deposit more OIDs.
			   * Use 50% of the current size for expansion.
			   */
			  if (*max_oid_cnt <= 0)
			    {
			      *max_oid_cnt = 0;
			      new_max_oid = attr_info.num_values;
			    }
			  else
			    {
			      new_max_oid = (int) (*max_oid_cnt * 1.5) + 1;
			      if (new_max_oid < attr_info.num_values)
				{
				  new_max_oid = attr_info.num_values;
				}
			    }
			  if (new_max_oid < 10)
			    {
			      new_max_oid = 10;
			    }

			  oid_ptr = (OID *) realloc (*oid_list,
						     new_max_oid *
						     sizeof (OID));
			  if (oid_ptr == NULL)
			    {
			      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
				      ER_OUT_OF_VIRTUAL_MEMORY, 1,
				      new_max_oid * sizeof (OID));
			      goto error;
			    }

			  /*
			   * Set the pointers and advance to current area pointer
			   */
			  *oid_list = oid_ptr;
			  oid_ptr += *max_oid_cnt;
			  *max_oid_cnt = new_max_oid;
			}
		      *oid_ptr = *attr_oid;
		      oid_ptr++;
		      oid_cnt++;
		    }
		}
	    }
	}
    }

  /* free object area if no OIDs were encountered */
  if (oid_cnt == 0)
    /*
     * Unless we check whether *oid_list is NULL,
     * it may cause double-free of oid_list.
     */
    if (*oid_list != NULL)
      {
	free_and_init (*oid_list);
      }

  heap_attrinfo_end (thread_p, &attr_info);

  /* return number of OIDs fetched */
  return oid_cnt;

error:
  /* XXXXXXX */

  free_and_init (*oid_list);
  *max_oid_cnt = 0;
  heap_attrinfo_end (thread_p, &attr_info);

  return ER_FAILED;
}

/*
 * heap_prefetch () - Prefetch objects
 *   return: NO_ERROR
 *           fetch_area is set to point to fetching area
 *   class_oid(in): Class identifier for the instance oid
 *   oid(in): Object that must be fetched if its cached state is invalid
 *   prefetch(in): Prefetch structure
 *
 */
int
heap_prefetch (THREAD_ENTRY * thread_p, OID * class_oid, const OID * oid,
	       LC_COPYAREA_DESC * prefetch)
{
  VPID vpid;
  PAGE_PTR pgptr = NULL;
  int round_length;
  INT16 right_slotid, left_slotid;
  HEAP_DIRECTION direction;
  SCAN_CODE scan;
  int ret = NO_ERROR;

  /*
   * Prefetch other instances (i.e., neighbors) stored on the same page
   * of the given object OID. Relocated instances and instances in overflow are
   * not prefetched, nor instances that do not belong to the given class.
   * Prefetching stop once an error, such as out of space, is encountered.
   */

  vpid.volid = oid->volid;
  vpid.pageid = oid->pageid;

  pgptr = heap_scan_pb_lock_and_fetch (thread_p, &vpid, OLD_PAGE, S_LOCK,
				       NULL);
  if (pgptr == NULL)
    {
      ret = er_errid ();
      if (ret == ER_PB_BAD_PAGEID)
	{
	  ret = ER_HEAP_UNKNOWN_OBJECT;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ret,
		  3, oid->volid, oid->pageid, oid->slotid);
	}

      /*
       * Problems getting the page.. forget about prefetching...
       */
      return ret;
    }

  right_slotid = oid->slotid;
  left_slotid = oid->slotid;
  direction = HEAP_DIRECTION_BOTH;

  while (direction != HEAP_DIRECTION_NONE)
    {
      /*
       * Don't include the desired object again, forwarded instances, nor
       * instances that belong to other classes
       */

      /* Check to the right */
      if (direction == HEAP_DIRECTION_RIGHT
	  || direction == HEAP_DIRECTION_BOTH)
	{
	  scan = spage_next_record (pgptr, &right_slotid, prefetch->recdes,
				    COPY);
	  if (scan == S_SUCCESS
	      && spage_get_record_type (pgptr, right_slotid) == REC_HOME)
	    {
	      prefetch->mobjs->num_objs++;
	      COPY_OID (&((*prefetch->obj)->class_oid), class_oid);
	      (*prefetch->obj)->error_code = NO_ERROR;
	      (*prefetch->obj)->oid.volid = oid->volid;
	      (*prefetch->obj)->oid.pageid = oid->pageid;
	      (*prefetch->obj)->oid.slotid = right_slotid;
	      (*prefetch->obj)->length = prefetch->recdes->length;
	      (*prefetch->obj)->offset = *prefetch->offset;
	      (*prefetch->obj)->operation = LC_FETCH;
	      (*prefetch->obj) =
		LC_NEXT_ONEOBJ_PTR_IN_COPYAREA (*prefetch->obj);
	      round_length =
		DB_ALIGN (prefetch->recdes->length, HEAP_MAX_ALIGN);
	      *prefetch->offset += round_length;
	      prefetch->recdes->data += round_length;
	      prefetch->recdes->area_size -= (round_length +
					      sizeof (*(*prefetch->obj)));
	    }
	  else if (scan != S_SUCCESS)
	    {
	      /* Stop prefetching objects from the right */
	      direction = ((direction == HEAP_DIRECTION_BOTH)
			   ? HEAP_DIRECTION_LEFT : HEAP_DIRECTION_NONE);
	    }
	}

      /* Check to the left */
      if (direction == HEAP_DIRECTION_LEFT
	  || direction == HEAP_DIRECTION_BOTH)
	{
	  scan = spage_previous_record (pgptr, &left_slotid, prefetch->recdes,
					COPY);
	  if (scan == S_SUCCESS && left_slotid != HEAP_HEADER_AND_CHAIN_SLOTID
	      && spage_get_record_type (pgptr, left_slotid) == REC_HOME)
	    {
	      prefetch->mobjs->num_objs++;
	      COPY_OID (&((*prefetch->obj)->class_oid), class_oid);
	      (*prefetch->obj)->error_code = NO_ERROR;
	      (*prefetch->obj)->oid.volid = oid->volid;
	      (*prefetch->obj)->oid.pageid = oid->pageid;
	      (*prefetch->obj)->oid.slotid = left_slotid;
	      (*prefetch->obj)->length = prefetch->recdes->length;
	      (*prefetch->obj)->offset = *prefetch->offset;
	      (*prefetch->obj)->operation = LC_FETCH;
	      (*prefetch->obj) =
		LC_NEXT_ONEOBJ_PTR_IN_COPYAREA (*prefetch->obj);
	      round_length =
		DB_ALIGN (prefetch->recdes->length, HEAP_MAX_ALIGN);
	      *prefetch->offset += round_length;
	      prefetch->recdes->data += round_length;
	      prefetch->recdes->area_size -= (round_length +
					      sizeof (*(*prefetch->obj)));
	    }
	  else if (scan != S_SUCCESS)
	    {
	      /* Stop prefetching objects from the right */
	      direction = ((direction == HEAP_DIRECTION_BOTH)
			   ? HEAP_DIRECTION_RIGHT : HEAP_DIRECTION_NONE);
	    }
	}
    }

  pgbuf_unfix_and_init (thread_p, pgptr);

  return ret;
}

/*
 * heap_check_all_pages () - Validate all pages known by given heap vs file manger
 *   return: DISK_INVALID, DISK_VALID, DISK_ERROR
 *   hfid(in): : Heap identifier
 *
 * Note: Verify that all pages known by the given heap are valid. That
 * is, that they are valid from the point of view of the file manager.
 */
DISK_ISVALID
heap_check_all_pages (THREAD_ENTRY * thread_p, HFID * hfid)
{
  VPID vpid;			/* Page-volume identifier            */
  VPID *vpidptr_ofpgptr;
  PAGE_PTR pgptr = NULL;	/* Page pointer                      */
  HEAP_HDR_STATS *heap_hdr;	/* Header of heap structure          */
  RECDES hdr_recdes;		/* Header record descriptor          */
  DISK_ISVALID valid_pg = DISK_VALID;
  INT32 npages = 0;
  int i;
  HEAP_CHKALL_RELOCOIDS chk;
  HEAP_CHKALL_RELOCOIDS *chk_objs = &chk;


  valid_pg = heap_chkreloc_start (chk_objs);
  if (valid_pg != DISK_VALID)
    {
      chk_objs = NULL;
    }

  /* Scan every page of the heap to find out if they are valid */

  vpid.volid = hfid->vfid.volid;
  vpid.pageid = hfid->hpgid;

  while (!VPID_ISNULL (&vpid) && valid_pg == DISK_VALID)
    {
      npages++;

      valid_pg = file_isvalid_page_partof (thread_p, &vpid, &hfid->vfid);
      if (valid_pg != DISK_VALID)
	{
	  break;
	}

      pgptr = heap_scan_pb_lock_and_fetch (thread_p, &vpid, OLD_PAGE, S_LOCK,
					   NULL);
      if (pgptr == NULL)
	{
	  /* something went wrong, return */
	  valid_pg = DISK_ERROR;
	  break;
	}

      if (heap_vpid_next (hfid, pgptr, &vpid) != NO_ERROR)
	{
	  pgbuf_unfix_and_init (thread_p, pgptr);
	  /* something went wrong, return */
	  valid_pg = DISK_ERROR;
	  break;
	}

      vpidptr_ofpgptr = pgbuf_get_vpid_ptr (pgptr);
      if (VPID_EQ (&vpid, vpidptr_ofpgptr))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_CYCLE, 5,
		  vpid.volid, vpid.pageid, hfid->vfid.volid,
		  hfid->vfid.fileid, hfid->hpgid);
	  VPID_SET_NULL (&vpid);
	  valid_pg = DISK_ERROR;
	}

      if (chk_objs != NULL)
	{
	  valid_pg = heap_chkreloc_next (chk_objs, pgptr);
	}

      pgbuf_unfix_and_init (thread_p, pgptr);
    }

  if (chk_objs != NULL)
    {
      if (valid_pg == DISK_VALID)
	{
	  valid_pg = heap_chkreloc_end (chk_objs);
	}
      else
	{
	  chk_objs->verify = false;
	  (void) heap_chkreloc_end (chk_objs);
	}
    }

  if (valid_pg == DISK_VALID)
    {
      i = file_get_numpages (thread_p, &hfid->vfid);
      if (npages != i)
	{
	  if (i == -1)
	    {
	      valid_pg = DISK_ERROR;
	    }
	  else
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_HEAP_MISMATCH_NPAGES, 5, hfid->vfid.volid,
		      hfid->vfid.fileid, hfid->hpgid, npages, i);
	      valid_pg = DISK_INVALID;
	    }
	}

      /*
       * Check the statistics entries in the header
       */

      /* Fetch the header page of the heap file */
      vpid.volid = hfid->vfid.volid;
      vpid.pageid = hfid->hpgid;

      pgptr = heap_scan_pb_lock_and_fetch (thread_p, &vpid, OLD_PAGE, S_LOCK,
					   NULL);
      if (pgptr == NULL ||
	  spage_get_record (pgptr, HEAP_HEADER_AND_CHAIN_SLOTID, &hdr_recdes,
			    PEEK) != S_SUCCESS)
	{
	  /* Unable to peek heap header record */
	  if (pgptr != NULL)
	    {
	      pgbuf_unfix_and_init (thread_p, pgptr);
	    }

	  return DISK_ERROR;
	}

      heap_hdr = (HEAP_HDR_STATS *) hdr_recdes.data;
      for (i = 0; i < HEAP_NUM_BEST_SPACESTATS; i++)
	{
	  if (!VPID_ISNULL (&heap_hdr->estimates.best[i].vpid))
	    {
	      valid_pg =
		file_isvalid_page_partof (thread_p,
					  &heap_hdr->estimates.best[i].vpid,
					  &hfid->vfid);
	      if (valid_pg != DISK_VALID)
		{
		  break;
		}
	    }
	}

      if (prm_get_integer_value (PRM_ID_HF_MAX_BESTSPACE_ENTRIES) > 0)
	{
	  HEAP_STATS_ENTRY *ent;
	  void *last;
	  int rc;

	  rc = pthread_mutex_lock (&heap_Bestspace->bestspace_mutex);

	  last = NULL;
	  do
	    {
	      ent =
		(HEAP_STATS_ENTRY *) mht_get2 (heap_Bestspace->hfid_ht,
					       hfid, &last);

	      if (ent)
		{
		  assert_release (!VPID_ISNULL (&ent->best.vpid));
#if defined(SA_MODE)
		  if (!VPID_ISNULL (&ent->best.vpid))
		    {
		      valid_pg = file_isvalid_page_partof (thread_p,
							   &ent->best.vpid,
							   &hfid->vfid);
		      if (valid_pg != DISK_VALID)
			{
			  break;
			}
		    }
#endif
		  assert_release (ent->best.freespace > 0);
		}
	    }
	  while (ent);

	  assert (mht_count (heap_Bestspace->vpid_ht) ==
		  mht_count (heap_Bestspace->hfid_ht));

	  pthread_mutex_unlock (&heap_Bestspace->bestspace_mutex);
	}

      pgbuf_unfix_and_init (thread_p, pgptr);

      /* Need to check for the overflow pages.... */
    }

  return valid_pg;
}

DISK_ISVALID
heap_check_heap_file (THREAD_ENTRY * thread_p, HFID * hfid)
{
  FILE_TYPE file_type;
  VPID vpid;
  FILE_HEAP_DES hfdes;
  LOCK class_lock;
  int scanid_bit = -1;
  DISK_ISVALID rv = DISK_VALID;

  file_type = file_get_type (thread_p, &hfid->vfid);
  if (file_type == FILE_UNKNOWN_TYPE ||
      (file_type != FILE_HEAP && file_type != FILE_HEAP_REUSE_SLOTS))
    {
      return DISK_ERROR;
    }

  if (file_find_nthpages (thread_p, &hfid->vfid, &vpid, 0, 1) == 1)
    {
      hfid->hpgid = vpid.pageid;

      if ((file_get_descriptor (thread_p, &hfid->vfid, &hfdes,
				sizeof (FILE_HEAP_DES)) > 0)
	  && !OID_ISNULL (&hfdes.class_oid))
	{

	  if (lock_scan (thread_p, &hfdes.class_oid, true, LOCKHINT_NONE,
			 &class_lock, &scanid_bit) != LK_GRANTED)
	    {
	      return DISK_ERROR;
	    }

	  rv = heap_check_all_pages (thread_p, hfid);
	  lock_unlock_scan (thread_p, &hfdes.class_oid, scanid_bit, END_SCAN);
	}
    }
  else
    {
      return DISK_ERROR;
    }

  return rv;
}

/*
 * heap_check_all_heaps () - Validate all pages of all known heap files
 *   return: DISK_INVALID, DISK_VALID, DISK_ERROR
 *
 * Note: Verify that all pages of all heap files are valid. That is,
 * that they are valid from the point of view of the file manager.
 */
DISK_ISVALID
heap_check_all_heaps (THREAD_ENTRY * thread_p)
{
  int num_files;
  HFID hfid;
  DISK_ISVALID allvalid = DISK_VALID;
  FILE_TYPE file_type;
  int i;

  /* Find number of files */
  num_files = file_get_numfiles (thread_p);
  if (num_files < 0)
    {
      return DISK_ERROR;
    }

  /* Go to each file, check only the heap files */
  for (i = 0; i < num_files && allvalid != DISK_ERROR; i++)
    {
      if (file_find_nthfile (thread_p, &hfid.vfid, i) != 1)
	{
	  break;
	}

      file_type = file_get_type (thread_p, &hfid.vfid);
      if (file_type == FILE_UNKNOWN_TYPE)
	{
	  return DISK_ERROR;
	}
      if (file_type != FILE_HEAP && file_type != FILE_HEAP_REUSE_SLOTS)
	{
	  continue;
	}

      if (heap_check_heap_file (thread_p, &hfid) != DISK_VALID)
	{
	  allvalid = DISK_ERROR;
	}
    }

  return allvalid;
}

/*
 * heap_dump_hdr () - Dump heap file header
 *   return: NO_ERROR
 *   heap_hdr(in): Header structure
 */
static int
heap_dump_hdr (FILE * fp, HEAP_HDR_STATS * heap_hdr)
{
  int i, j;
  int avg_length;
  int ret = NO_ERROR;

  avg_length = ((heap_hdr->estimates.num_recs > 0)
		? (int) ((heap_hdr->estimates.recs_sumlen /
			  (float) heap_hdr->estimates.num_recs) + 0.9) : 0);

  fprintf (fp, "CLASS_OID = %2d|%4d|%2d, ",
	   heap_hdr->class_oid.volid, heap_hdr->class_oid.pageid,
	   heap_hdr->class_oid.slotid);
  fprintf (fp, "OVF_VFID = %4d|%4d, NEXT_VPID = %4d|%4d\n",
	   heap_hdr->ovf_vfid.volid, heap_hdr->ovf_vfid.fileid,
	   heap_hdr->next_vpid.volid, heap_hdr->next_vpid.pageid);
  fprintf (fp, "unfill_space = %4d\n", heap_hdr->unfill_space);
  fprintf (fp, "Estimated: num_pages = %d, num_recs = %d, "
	   " avg reclength = %d\n",
	   heap_hdr->estimates.num_pages,
	   heap_hdr->estimates.num_recs, avg_length);
  fprintf (fp, "Estimated: num high best = %d, "
	   "num others(not in array) high best = %d\n",
	   heap_hdr->estimates.num_high_best,
	   heap_hdr->estimates.num_other_high_best);
  fprintf (fp, "Hint of best set of vpids with head = %d\n",
	   heap_hdr->estimates.head);

  for (j = 0, i = 0; i < HEAP_NUM_BEST_SPACESTATS; j++, i++)
    {
      if (j != 0 && j % 5 == 0)
	{
	  fprintf (fp, "\n");
	}
      fprintf (fp, "%4d|%4d %4d,",
	       heap_hdr->estimates.best[i].vpid.volid,
	       heap_hdr->estimates.best[i].vpid.pageid,
	       heap_hdr->estimates.best[i].freespace);
    }
  fprintf (fp, "\n");

  fprintf (fp,
	   "Second best: num hints = %d, head of hints = %d, tail (next to insert) of hints = %d, num subs = %d\n",
	   heap_hdr->estimates.num_second_best,
	   heap_hdr->estimates.head_second_best,
	   heap_hdr->estimates.tail_second_best,
	   heap_hdr->estimates.num_substitutions);
  for (j = 0, i = 0; i < HEAP_NUM_BEST_SPACESTATS; j++, i++)
    {
      if (j != 0 && j % 5 == 0)
	{
	  fprintf (fp, "\n");
	}
      fprintf (fp, "%4d|%4d,",
	       heap_hdr->estimates.second_best[i].volid,
	       heap_hdr->estimates.second_best[i].pageid);
    }
  fprintf (fp, "\n");

  fprintf (fp, "Last vpid = %4d|%4d\n",
	   heap_hdr->estimates.last_vpid.volid,
	   heap_hdr->estimates.last_vpid.pageid);

  fprintf (fp, "Next full search vpid = %4d|%4d\n",
	   heap_hdr->estimates.full_search_vpid.volid,
	   heap_hdr->estimates.full_search_vpid.pageid);

  return ret;
}

/*
 * heap_dump () - Dump heap file
 *   return:
 *   hfid(in): Heap file identifier
 *   dump_records(in): If true, objects are printed in ascii format, otherwise, the
 *              objects are not printed.
 *
 * Note: Dump a heap file. The objects are printed only when the value
 * of dump_records is true. This function is used for DEBUGGING PURPOSES.
 */
void
heap_dump (THREAD_ENTRY * thread_p, FILE * fp, HFID * hfid, bool dump_records)
{
  VPID vpid;			/* Page-volume identifier            */
  HEAP_HDR_STATS *heap_hdr;	/* Header of heap structure          */
  PAGE_PTR pgptr = NULL;	/* Page pointer                      */
  RECDES hdr_recdes;		/* Header record descriptor          */
  VFID ovf_vfid;
  OID oid;
  HEAP_SCANCACHE scan_cache;
  HEAP_CACHE_ATTRINFO attr_info;
  RECDES peek_recdes;
  FILE_HEAP_DES hfdes;
  int ret = NO_ERROR;

  fprintf (fp, "\n\n*** DUMPING HEAP FILE: ");
  fprintf (fp, "volid = %d, Fileid = %d, Header-pageid = %d ***\n",
	   hfid->vfid.volid, hfid->vfid.fileid, hfid->hpgid);
  (void) file_dump_descriptor (thread_p, fp, &hfid->vfid);

  /* Fetch the header page of the heap file */

  vpid.volid = hfid->vfid.volid;
  vpid.pageid = hfid->hpgid;
  pgptr = heap_scan_pb_lock_and_fetch (thread_p, &vpid, OLD_PAGE, S_LOCK,
				       NULL);
  if (pgptr == NULL)
    {
      /* Unable to fetch heap header page */
      return;
    }

  /* Peek the header record to dump the estadistics */

  if (spage_get_record (pgptr, HEAP_HEADER_AND_CHAIN_SLOTID, &hdr_recdes,
			PEEK) != S_SUCCESS)
    {
      /* Unable to peek heap header record */
      pgbuf_unfix_and_init (thread_p, pgptr);
      return;
    }

  heap_hdr = (HEAP_HDR_STATS *) hdr_recdes.data;
  ret = heap_dump_hdr (fp, heap_hdr);
  if (ret != NO_ERROR)
    {
      pgbuf_unfix_and_init (thread_p, pgptr);
      return;
    }

  VFID_COPY (&ovf_vfid, &heap_hdr->ovf_vfid);
  pgbuf_unfix_and_init (thread_p, pgptr);

  /* now scan every page and dump it */
  vpid.volid = hfid->vfid.volid;
  vpid.pageid = hfid->hpgid;
  while (!VPID_ISNULL (&vpid))
    {
      pgptr = heap_scan_pb_lock_and_fetch (thread_p, &vpid, OLD_PAGE, S_LOCK,
					   NULL);
      if (pgptr == NULL)
	{
	  /* something went wrong, return */
	  return;
	}
      spage_dump (thread_p, fp, pgptr, dump_records);
      (void) heap_vpid_next (hfid, pgptr, &vpid);
      pgbuf_unfix_and_init (thread_p, pgptr);
    }

  /* Dump file table configuration */
  if (file_dump (thread_p, fp, &hfid->vfid) != NO_ERROR)
    {
      return;
    }

  if (!VFID_ISNULL (&ovf_vfid))
    {
      /* There is an overflow file for this heap file */
      fprintf (fp, "\nOVERFLOW FILE INFORMATION FOR HEAP FILE\n\n");
      if (file_dump (thread_p, fp, &ovf_vfid) != NO_ERROR)
	{
	  return;
	}
    }

  /*
   * Dump schema definition
   */

  if (file_get_descriptor (thread_p, &hfid->vfid, &hfdes,
			   sizeof (FILE_HEAP_DES)) > 0
      && !OID_ISNULL (&hfdes.class_oid))
    {

      if (heap_attrinfo_start (thread_p, &hfdes.class_oid, -1, NULL,
			       &attr_info) != NO_ERROR)
	{
	  return;
	}

      ret = heap_classrepr_dump (thread_p, fp, &hfdes.class_oid,
				 attr_info.last_classrepr);
      if (ret != NO_ERROR)
	{
	  heap_attrinfo_end (thread_p, &attr_info);
	  return;
	}

      /* Dump individual Objects */
      if (dump_records == true)
	{
	  if (heap_scancache_start (thread_p, &scan_cache, hfid, NULL, true,
				    false, LOCKHINT_NONE) != NO_ERROR)
	    {
	      /* something went wrong, return */
	      heap_attrinfo_end (thread_p, &attr_info);
	      return;
	    }

	  OID_SET_NULL (&oid);
	  oid.volid = hfid->vfid.volid;

	  while (heap_next (thread_p, hfid, NULL, &oid, &peek_recdes,
			    &scan_cache, PEEK) == S_SUCCESS)
	    {
	      fprintf (fp,
		       "Object-OID = %2d|%4d|%2d,\n"
		       "  Length on disk = %d,\n",
		       oid.volid, oid.pageid, oid.slotid, peek_recdes.length);

	      if (heap_attrinfo_read_dbvalues
		  (thread_p, &oid, &peek_recdes, &attr_info) != NO_ERROR)
		{
		  fprintf (fp, "  Error ... continue\n");
		  continue;
		}
	      heap_attrinfo_dump (thread_p, fp, &attr_info, false);
	    }
	  heap_scancache_end (thread_p, &scan_cache);
	}
      heap_attrinfo_end (thread_p, &attr_info);
    }

  fprintf (fp, "\n\n*** END OF DUMP FOR HEAP FILE ***\n\n");
}

/*
 * heap_dump_all () - Dump all heap files
 *   return:
 *   dump_records(in): If true, objects are printed in ascii format, otherwise, the
 *              objects are not printed.
 */
void
heap_dump_all (THREAD_ENTRY * thread_p, FILE * fp, bool dump_records)
{
  int num_files;
  HFID hfid;
  VPID vpid;
  int i;

  /* Find number of files */
  num_files = file_get_numfiles (thread_p);
  if (num_files <= 0)
    {
      return;
    }

  /* Dump each heap file */
  for (i = 0; i < num_files; i++)
    {
      FILE_TYPE file_type = FILE_UNKNOWN_TYPE;

      if (file_find_nthfile (thread_p, &hfid.vfid, i) != 1)
	{
	  break;
	}

      file_type = file_get_type (thread_p, &hfid.vfid);
      if (file_type != FILE_HEAP && file_type != FILE_HEAP_REUSE_SLOTS)
	{
	  continue;
	}

      if (file_find_nthpages (thread_p, &hfid.vfid, &vpid, 0, 1) == 1)
	{
	  hfid.hpgid = vpid.pageid;
	  heap_dump (thread_p, fp, &hfid, dump_records);
	}
    }
}

/*
 * heap_dump_all_capacities () - Dump the capacities of all heap files.
 *   return:
 */
void
heap_dump_all_capacities (THREAD_ENTRY * thread_p, FILE * fp)
{
  HFID hfid;
  VPID vpid;
  int i;
  int num_files = 0;
  int num_recs = 0;
  int num_recs_relocated = 0;
  int num_recs_inovf = 0;
  int num_pages = 0;
  int avg_freespace = 0;
  int avg_freespace_nolast = 0;
  int avg_reclength = 0;
  int avg_overhead = 0;
  FILE_HEAP_DES hfdes;
  HEAP_CACHE_ATTRINFO attr_info;

  /* Find number of files */
  num_files = file_get_numfiles (thread_p);
  if (num_files <= 0)
    {
      return;
    }

  fprintf (fp, "IO_PAGESIZE = %d, DB_PAGESIZE = %d, Recv_overhead = %d\n",
	   IO_PAGESIZE, DB_PAGESIZE, IO_PAGESIZE - DB_PAGESIZE);

  /* Go to each file, check only the heap files */
  for (i = 0; i < num_files; i++)
    {
      FILE_TYPE file_type = FILE_UNKNOWN_TYPE;

      if (file_find_nthfile (thread_p, &hfid.vfid, i) != 1)
	{
	  break;
	}

      file_type = file_get_type (thread_p, &hfid.vfid);
      if (file_type != FILE_HEAP && file_type != FILE_HEAP_REUSE_SLOTS)
	{
	  continue;
	}

      if (file_find_nthpages (thread_p, &hfid.vfid, &vpid, 0, 1) == 1)
	{
	  hfid.hpgid = vpid.pageid;
	  if (heap_get_capacity (thread_p, &hfid, &num_recs,
				 &num_recs_relocated, &num_recs_inovf,
				 &num_pages, &avg_freespace,
				 &avg_freespace_nolast, &avg_reclength,
				 &avg_overhead) == NO_ERROR)
	    {
	      fprintf (fp,
		       "HFID:%d|%d|%d, Num_recs = %d, Num_reloc_recs = %d,\n"
		       "    Num_recs_inovf = %d, Avg_reclength = %d,\n"
		       "    Num_pages = %d, Avg_free_space_per_page = %d,\n"
		       "    Avg_free_space_per_page_without_lastpage = %d\n"
		       "    Avg_overhead_per_page = %d\n",
		       (int) hfid.vfid.volid, hfid.vfid.fileid,
		       hfid.hpgid, num_recs, num_recs_relocated,
		       num_recs_inovf, avg_reclength, num_pages,
		       avg_freespace, avg_freespace_nolast, avg_overhead);
	      /*
	       * Dump schema definition
	       */
	      if (file_get_descriptor (thread_p, &hfid.vfid, &hfdes,
				       sizeof (FILE_HEAP_DES)) > 0
		  && !OID_ISNULL (&hfdes.class_oid)
		  && heap_attrinfo_start (thread_p, &hfdes.class_oid, -1,
					  NULL, &attr_info) == NO_ERROR)
		{
		  (void) heap_classrepr_dump (thread_p, fp, &hfdes.class_oid,
					      attr_info.last_classrepr);
		  heap_attrinfo_end (thread_p, &attr_info);
		}
	      fprintf (fp, "\n");
	    }
	}
    }
}

/*
 * heap_estimate_num_pages_needed () - Guess the number of pages needed to store a
 *                                set of instances
 *   return: int
 *   total_nobjs(in): Number of object to insert
 *   avg_obj_size(in): Average size of object
 *   num_attrs(in): Number of attributes
 *   num_var_attrs(in): Number of variable attributes
 *
 */
INT32
heap_estimate_num_pages_needed (THREAD_ENTRY * thread_p, int total_nobjs,
				int avg_obj_size, int num_attrs,
				int num_var_attrs)
{
  int nobj_page;
  INT32 npages;


  avg_obj_size += OR_HEADER_SIZE;

  if (num_attrs > 0)
    {
      avg_obj_size += CEIL_PTVDIV (num_attrs, 32) * sizeof (int);
    }
  if (num_var_attrs > 0)
    {
      avg_obj_size += (num_var_attrs + 1) * sizeof (int);
      /* Assume max padding of 3 bytes... */
      avg_obj_size += num_var_attrs * (sizeof (int) - 1);
    }

  avg_obj_size = DB_ALIGN (avg_obj_size, HEAP_MAX_ALIGN);

  /*
   * Find size of page available to store objects:
   * USER_SPACE_IN_PAGES = (DB_PAGESIZE * (1 - unfill_factor)
   *                        - SLOTTED PAGE HDR size overhead
   *                        - link of pages(i.e., sizeof(chain))
   *                        - slot overhead to store the link chain)
   */

  nobj_page =
    ((int) (DB_PAGESIZE * (1 - prm_get_float_value (PRM_ID_HF_UNFILL_FACTOR)))
     - spage_header_size () - sizeof (HEAP_CHAIN) - spage_slot_size ());
  /*
   * Find the number of objects per page
   */

  nobj_page = nobj_page / (avg_obj_size + spage_slot_size ());

  /*
   * Find the number of pages. Add one page for file manager overhead
   */

  if (nobj_page > 0)
    {
      npages = CEIL_PTVDIV (total_nobjs, nobj_page);
      npages += file_guess_numpages_overhead (thread_p, NULL, npages);
    }
  else
    {
      /*
       * Overflow insertion
       */
      npages = overflow_estimate_npages_needed (thread_p, total_nobjs,
						avg_obj_size);

      /*
       * Find number of pages for the indirect record references (OIDs) to
       * overflow records
       */
      nobj_page =
	((int)
	 (DB_PAGESIZE * (1 - prm_get_float_value (PRM_ID_HF_UNFILL_FACTOR))) -
	 spage_header_size () - sizeof (HEAP_CHAIN) - spage_slot_size ());
      nobj_page = nobj_page / (sizeof (OID) + spage_slot_size ());
      /*
       * Now calculate the number of pages
       */
      nobj_page += CEIL_PTVDIV (total_nobjs, nobj_page);
      nobj_page += file_guess_numpages_overhead (thread_p, NULL, nobj_page);
      /*
       * Add the number of overflow pages and non-heap pages
       */
      npages = npages + nobj_page;
    }

  return npages;
}

/*
 *     	Check consistency of heap from the point of view of relocation
 */

/*
 * heap_chkreloc_start () - Start validating consistency of relocated objects in
 *                        heap
 *   return: DISK_VALID, DISK_INVALID, DISK_ERROR
 *   chk(in): Structure for checking relocation objects
 *
 */
static DISK_ISVALID
heap_chkreloc_start (HEAP_CHKALL_RELOCOIDS * chk)
{
  chk->ht = mht_create ("Validate Relocation entries hash table",
			HEAP_CHK_ADD_UNFOUND_RELOCOIDS, oid_hash,
			oid_compare_equals);
  if (chk->ht == NULL)
    {
      chk->ht = NULL;
      chk->unfound_reloc_oids = NULL;
      chk->max_unfound_reloc = -1;
      chk->num_unfound_reloc = -1;
      return DISK_ERROR;
    }

  chk->unfound_reloc_oids =
    (OID *) malloc (sizeof (*chk->unfound_reloc_oids) *
		    HEAP_CHK_ADD_UNFOUND_RELOCOIDS);
  if (chk->unfound_reloc_oids == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      sizeof (*chk->unfound_reloc_oids) *
	      HEAP_CHK_ADD_UNFOUND_RELOCOIDS);

      if (chk->ht != NULL)
	{
	  mht_destroy (chk->ht);
	}

      chk->ht = NULL;
      chk->unfound_reloc_oids = NULL;
      chk->max_unfound_reloc = -1;
      chk->num_unfound_reloc = -1;
      return DISK_ERROR;
    }

  chk->max_unfound_reloc = HEAP_CHK_ADD_UNFOUND_RELOCOIDS;
  chk->num_unfound_reloc = 0;
  chk->verify = true;

  return DISK_VALID;
}

/*
 * heap_chkreloc_end () - Finish validating consistency of relocated objects
 *                      in heap
 *   return: DISK_VALID, DISK_INVALID, DISK_ERROR
 *   chk(in): Structure for checking relocation objects
 *
 * Note: Scanning the unfound_reloc_oid list, remove those entries that
 * are also found in hash table (remove them from unfound_reloc
 * list and from hash table). At the end of the scan, if there
 * are any entries in either hash table or unfound_reloc_oid, the
 * heap is incosistent/corrupted.
 */
static DISK_ISVALID
heap_chkreloc_end (HEAP_CHKALL_RELOCOIDS * chk)
{
  HEAP_CHK_RELOCOID *forward;
  DISK_ISVALID valid_reloc = DISK_VALID;
  int i;

  /*
   * Check for any postponed unfound relocated OIDs that have not been
   * checked or found. If they are not in the hash table, it would be an
   * error. That is, we would have a relocated (content) object without an
   * object pointing to it. (relocation/home).
   */
  if (chk->verify == true)
    {
      for (i = 0; i < chk->num_unfound_reloc; i++)
	{
	  forward = (HEAP_CHK_RELOCOID *) mht_get (chk->ht,
						   &chk->unfound_reloc_oids
						   [i]);
	  if (forward != NULL)
	    {
	      /*
	       * The entry was found.
	       * Remove the entry and the memory space
	       */
	      /* mht_rem() has been updated to take a function and an arg pointer
	       * that can be called on the entry before it is removed.  We may
	       * want to take advantage of that here to free the memory associated
	       * with the entry
	       */
	      if (mht_rem (chk->ht, &chk->unfound_reloc_oids[i], NULL,
			   NULL) != NO_ERROR)
		{
		  valid_reloc = DISK_ERROR;
		}
	      else
		{
		  free_and_init (forward);
		}
	    }
	  else
	    {
	      er_log_debug (ARG_FILE_LINE,
			    "Unable to find relocation/home object"
			    " for relocated_oid=%d|%d|%\n",
			    (int) chk->unfound_reloc_oids[i].volid,
			    chk->unfound_reloc_oids[i].pageid,
			    (int) chk->unfound_reloc_oids[i].slotid);
	      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_GENERIC_ERROR, 0);
	      valid_reloc = DISK_INVALID;
	    }
	}
    }

  /*
   * If there are entries in the hash table, it would be problems. That is,
   * the relocated (content) objects were not found. That is, the home object
   * points to a dangling content object, or what it points is not a
   * relocated (newhome) object.
   */

  if (mht_count (chk->ht) > 0)
    {
      (void) mht_map (chk->ht, heap_chkreloc_print_notfound, chk);
      valid_reloc = DISK_INVALID;
    }

  mht_destroy (chk->ht);
  free_and_init (chk->unfound_reloc_oids);

  return valid_reloc;
}

/*
 * heap_chkreloc_print_notfound () - Print entry that does not have a relocated entry
 *   return: NO_ERROR
 *   ignore_reloc_oid(in): Key (relocated entry to real entry) of hash table
 *   ent(in): The entry associated with key (real oid)
 *   xchk(in): Structure for checking relocation objects
 *
 * Note: Print unfound relocated record information for this home
 * record with relocation address HEAP is inconsistent.
 */
static int
heap_chkreloc_print_notfound (const void *ignore_reloc_oid, void *ent,
			      void *xchk)
{
  HEAP_CHK_RELOCOID *forward = (HEAP_CHK_RELOCOID *) ent;
  HEAP_CHKALL_RELOCOIDS *chk = (HEAP_CHKALL_RELOCOIDS *) xchk;

  if (chk->verify == true)
    {
      er_log_debug (ARG_FILE_LINE, "Unable to find relocated record with"
		    " oid=%d|%d|%d for home object with oid=%d|%d|%d\n",
		    (int) forward->reloc_oid.volid,
		    forward->reloc_oid.pageid,
		    (int) forward->reloc_oid.slotid,
		    (int) forward->real_oid.volid,
		    forward->real_oid.pageid, (int) forward->real_oid.slotid);
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
    }
  /* mht_rem() has been updated to take a function and an arg pointer
   * that can be called on the entry before it is removed.  We may
   * want to take advantage of that here to free the memory associated
   * with the entry
   */
  (void) mht_rem (chk->ht, &forward->reloc_oid, NULL, NULL);
  free_and_init (forward);

  return NO_ERROR;
}

/*
 * heap_chkreloc_next () - Verify consistency of relocation records on page heap
 *   return: DISK_VALID, DISK_INVALID, DISK_ERROR
 *   chk(in): Structure for checking relocation objects
 *   pgptr(in): Page pointer
 *
 * Note: While scanning objects of given page:
 *              1: if a relocation record is found, we check if that record
 *                 has already been seen (i.e., if it is in unfound_relc
 *                 list),
 *                 if it has been seen, we remove the entry from the
 *                 unfound_relc_oid list.
 *                 if it has not been seen, we add an entry to hash table
 *                 from reloc_oid to real_oid
 *                 Note: for optimization reasons, we may not scan the
 *                 unfound_reloc if it is too long, in this case the entry is
 *                 added to hash table.
 *              2: if a newhome (relocated) record is found, we check if the
 *                 real record has already been seen (i.e., check hash table),
 *                 if it has been seen, we remove the entry from hash table
 *                 otherwise, we add an entry into the unfound_reloc list
 */

#define HEAP_CHKRELOC_UNFOUND_SHORT 5

static DISK_ISVALID
heap_chkreloc_next (HEAP_CHKALL_RELOCOIDS * chk, PAGE_PTR pgptr)
{
  HEAP_CHK_RELOCOID *forward;
  INT16 type = REC_UNKNOWN;
  RECDES recdes;
  OID oid;
  OID *peek_oid;
  void *ptr;
  bool found;
  int i;

  if (chk->verify != true)
    {
      return DISK_VALID;
    }

  oid.volid = pgbuf_get_volume_id (pgptr);
  oid.pageid = pgbuf_get_page_id (pgptr);
  oid.slotid = 0;		/* i.e., will get slot 1 */

  while (spage_next_record (pgptr, &oid.slotid, &recdes, PEEK) == S_SUCCESS)
    {
      if (oid.slotid == HEAP_HEADER_AND_CHAIN_SLOTID)
	{
	  continue;
	}
      type = spage_get_record_type (pgptr, oid.slotid);

      switch (type)
	{
	case REC_RELOCATION:
	  /*
	   * The record stored on the page is a relocation record,
	   * get the new home for the record
	   *
	   * If we have already entries waiting to be check and the list is
	   * not that big, check them. Otherwise, wait until the end for the
	   * check since searching the list may be expensive
	   */
	  peek_oid = (OID *) recdes.data;
	  found = false;
	  if (chk->num_unfound_reloc < HEAP_CHKRELOC_UNFOUND_SHORT)
	    {
	      /*
	       * Go a head and check since the list is very short.
	       */
	      for (i = 0; i < chk->num_unfound_reloc; i++)
		{
		  if (OID_EQ (&chk->unfound_reloc_oids[i], peek_oid))
		    {
		      /*
		       * Remove it from the unfound list
		       */
		      if ((i + 1) != chk->num_unfound_reloc)
			{
			  chk->unfound_reloc_oids[i] =
			    chk->unfound_reloc_oids[chk->num_unfound_reloc -
						    1];
			}
		      chk->num_unfound_reloc--;
		      found = true;
		      break;
		    }
		}
	    }
	  if (found == false)
	    {
	      /*
	       * Add it to hash table
	       */
	      forward =
		(HEAP_CHK_RELOCOID *) malloc (sizeof (HEAP_CHK_RELOCOID));
	      if (forward == NULL)
		{
		  /*
		   * Out of memory
		   */
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_OUT_OF_VIRTUAL_MEMORY, 1,
			  sizeof (HEAP_CHK_RELOCOID));

		  return DISK_ERROR;
		}
	      forward->real_oid = oid;
	      forward->reloc_oid = *peek_oid;
	      if (mht_put (chk->ht, &forward->reloc_oid, forward) == NULL)
		{
		  /*
		   * Failure in mht_put
		   */
		  return DISK_ERROR;
		}
	    }
	  break;

	case REC_BIGONE:
	case REC_HOME:
	  break;

	case REC_NEWHOME:
	  /*
	   * Remove the object from hash table or insert the object in unfound
	   * reloc check list.
	   */
	  forward = (HEAP_CHK_RELOCOID *) mht_get (chk->ht, &oid);
	  if (forward != NULL)
	    {
	      /*
	       * The entry was found.
	       * Remove the entry and the memory space
	       */
	      /* mht_rem() has been updated to take a function and an arg pointer
	       * that can be called on the entry before it is removed.  We may
	       * want to take advantage of that here to free the memory associated
	       * with the entry
	       */
	      (void) mht_rem (chk->ht, &forward->reloc_oid, NULL, NULL);
	      free_and_init (forward);
	    }
	  else
	    {
	      /*
	       * The entry is not in hash table.
	       * Add entry into unfound_reloc list
	       */
	      if (chk->max_unfound_reloc <= chk->num_unfound_reloc)
		{
		  /*
		   * Need to realloc the area. Add 100 OIDs to it
		   */
		  i = (sizeof (*chk->unfound_reloc_oids)
		       * (chk->max_unfound_reloc +
			  HEAP_CHK_ADD_UNFOUND_RELOCOIDS));

		  ptr = realloc (chk->unfound_reloc_oids, i);
		  if (ptr == NULL)
		    {
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			      ER_OUT_OF_VIRTUAL_MEMORY, 1, i);
		      return DISK_ERROR;
		    }
		  else
		    {
		      chk->unfound_reloc_oids = (OID *) ptr;
		      chk->max_unfound_reloc +=
			HEAP_CHK_ADD_UNFOUND_RELOCOIDS;
		    }
		}
	      i = chk->num_unfound_reloc++;
	      chk->unfound_reloc_oids[i] = oid;
	    }
	  break;

	case REC_MARKDELETED:
	case REC_DELETED_WILL_REUSE:
	default:
	  break;
	}
    }

  return DISK_VALID;
}

/*
 * Chn guesses for class objects at clients
 */

/*
 * Note: Currently, we do not try to guess chn of instances at clients.
 *       We are just doing it for classes.
 *
 * We do not know if the object is cached on the client side at all, we
 * are just guessing that it is still cached if it was sent to it. This is
 * almost 100% true since classes are avoided during garbage collection.

 * Caller does not know the chn when the client is fetching instances of the
 * class without knowning the class_oid. That does not imply that the
 * class object is not cached on the workspace. The client just did not
 * know the class_oid of the given fetched object. The server finds it and
 * has to decide whether or not to sent the class object. If the server does
 * not send the class object, and the client does not have it; the client will
 * request the class object (another server call)
 */

/*
 * heap_chnguess_initialize () - Initalize structure of chn guesses at clients
 *   return: NO_ERROR
 *
 * Note: Initialize structures used to cache information of CHN guess
 * at client workspaces.
 * Note: We current maintain that information only for classes.
 */
static int
heap_chnguess_initialize (void)
{
  HEAP_CHNGUESS_ENTRY *entry;
  int i;
  int ret = NO_ERROR;

  if (heap_Guesschn != NULL)
    {
      ret = heap_chnguess_finalize ();
      if (ret != NO_ERROR)
	{
	  goto exit_on_error;
	}
    }

  heap_Guesschn_area.schema_change = false;
  heap_Guesschn_area.clock_hand = -1;
  heap_Guesschn_area.num_entries = HEAP_CLASSREPR_MAXCACHE;

  /*
   * Start with at least the fude factor of clients. Make sure that every
   * bit is used.
   */
  heap_Guesschn_area.num_clients = logtb_get_number_of_total_tran_indices ();
  if (heap_Guesschn_area.num_clients < HEAP_CHNGUESS_FUDGE_MININDICES)
    {
      heap_Guesschn_area.num_clients = HEAP_CHNGUESS_FUDGE_MININDICES;
    }

  /* Make sure every single bit is used */
  heap_Guesschn_area.nbytes =
    HEAP_NBITS_TO_NBYTES (heap_Guesschn_area.num_clients);
  heap_Guesschn_area.num_clients =
    HEAP_NBYTES_TO_NBITS (heap_Guesschn_area.nbytes);

  /* Build the hash table from OID to CHN */
  heap_Guesschn_area.ht = mht_create ("Memory hash OID to chn at clients",
				      HEAP_CLASSREPR_MAXCACHE, oid_hash,
				      oid_compare_equals);
  if (heap_Guesschn_area.ht == NULL)
    {
      goto exit_on_error;
    }

  heap_Guesschn_area.entries =
    (HEAP_CHNGUESS_ENTRY *) malloc (sizeof (HEAP_CHNGUESS_ENTRY) *
				    heap_Guesschn_area.num_entries);
  if (heap_Guesschn_area.entries == NULL)
    {
      ret = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ret, 1,
	      sizeof (HEAP_CHNGUESS_ENTRY) * heap_Guesschn_area.num_entries);
      mht_destroy (heap_Guesschn_area.ht);
      goto exit_on_error;
    }

  heap_Guesschn_area.bitindex =
    (unsigned char *) malloc (heap_Guesschn_area.nbytes *
			      heap_Guesschn_area.num_entries);
  if (heap_Guesschn_area.bitindex == NULL)
    {
      ret = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ret, 1,
	      heap_Guesschn_area.nbytes * heap_Guesschn_area.num_entries);
      mht_destroy (heap_Guesschn_area.ht);
      free_and_init (heap_Guesschn_area.entries);
      goto exit_on_error;
    }

  /*
   * Initialize every entry as not recently freed
   */
  for (i = 0; i < heap_Guesschn_area.num_entries; i++)
    {
      entry = &heap_Guesschn_area.entries[i];
      entry->idx = i;
      entry->chn = NULL_CHN;
      entry->recently_accessed = false;
      OID_SET_NULL (&entry->oid);
      entry->bits =
	&heap_Guesschn_area.bitindex[i * heap_Guesschn_area.nbytes];
      HEAP_NBYTES_CLEARED (entry->bits, heap_Guesschn_area.nbytes);
    }
  heap_Guesschn = &heap_Guesschn_area;

  return ret;

exit_on_error:

  return (ret == NO_ERROR) ? ER_FAILED : ret;
}

/*
 * heap_chnguess_realloc () - More clients that currently maintained
 *   return: NO_ERROR
 *
 * Note: Expand the chn_guess structures to support at least the number
 * currently connected clients.
 */
static int
heap_chnguess_realloc (void)
{
  int i;
  unsigned char *save_bitindex;
  int save_nbytes;
  HEAP_CHNGUESS_ENTRY *entry;
  int ret = NO_ERROR;

  if (heap_Guesschn == NULL)
    {
      return heap_chnguess_initialize ();
    }

  /*
   * Save current information, so we can copy them at a alater point
   */
  save_bitindex = heap_Guesschn_area.bitindex;
  save_nbytes = heap_Guesschn_area.nbytes;

  /*
   * Find the number of clients that need to be supported. Avoid small
   * increases since it is undesirable to realloc again. Increase by at least
   * the fudge factor.
   */

  heap_Guesschn->num_clients += HEAP_CHNGUESS_FUDGE_MININDICES;
  i = logtb_get_number_of_total_tran_indices ();

  if (heap_Guesschn->num_clients < i)
    {
      heap_Guesschn->num_clients = i + HEAP_CHNGUESS_FUDGE_MININDICES;
    }

  /* Make sure every single bit is used */
  heap_Guesschn_area.nbytes =
    HEAP_NBITS_TO_NBYTES (heap_Guesschn_area.num_clients);
  heap_Guesschn_area.num_clients =
    HEAP_NBYTES_TO_NBITS (heap_Guesschn_area.nbytes);

  heap_Guesschn_area.bitindex =
    (unsigned char *) malloc (heap_Guesschn_area.nbytes *
			      heap_Guesschn_area.num_entries);
  if (heap_Guesschn_area.bitindex == NULL)
    {
      ret = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ret, 1,
	      heap_Guesschn_area.nbytes * heap_Guesschn_area.num_entries);
      heap_Guesschn_area.bitindex = save_bitindex;
      heap_Guesschn_area.nbytes = save_nbytes;
      heap_Guesschn_area.num_clients = HEAP_NBYTES_TO_NBITS (save_nbytes);
      goto exit_on_error;
    }

  /*
   * Now reset the bits for each entry
   */

  for (i = 0; i < heap_Guesschn_area.num_entries; i++)
    {
      entry = &heap_Guesschn_area.entries[i];
      entry->bits =
	&heap_Guesschn_area.bitindex[i * heap_Guesschn_area.nbytes];
      /*
       * Copy the bits
       */
      memcpy (entry->bits, &save_bitindex[i * save_nbytes], save_nbytes);
      HEAP_NBYTES_CLEARED (&entry->bits[save_nbytes],
			   heap_Guesschn_area.nbytes - save_nbytes);
    }
  /*
   * Now throw previous storage
   */
  free_and_init (save_bitindex);

  return ret;

exit_on_error:

  return (ret == NO_ERROR
	  && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
}

/*
 * heap_chnguess_finalize () - Finish chnguess information
 *   return: NO_ERROR
 *
 * Note: Destroy hash table and memory for entries.
 */
static int
heap_chnguess_finalize (void)
{
  int ret = NO_ERROR;

  if (heap_Guesschn == NULL)
    {
      return NO_ERROR;		/* nop */
    }

  mht_destroy (heap_Guesschn->ht);
  free_and_init (heap_Guesschn->entries);
  free_and_init (heap_Guesschn->bitindex);
  heap_Guesschn->ht = NULL;
  heap_Guesschn->schema_change = false;
  heap_Guesschn->clock_hand = 0;
  heap_Guesschn->num_entries = 0;
  heap_Guesschn->num_clients = 0;
  heap_Guesschn->nbytes = 0;

  heap_Guesschn = NULL;

  return ret;
}

/*
 * heap_stats_bestspace_initialize () - Initialize structure of best space
 *   return: NO_ERROR
 */
static int
heap_stats_bestspace_initialize (void)
{
  int ret = NO_ERROR;

  if (heap_Bestspace != NULL)
    {
      ret = heap_stats_bestspace_finalize ();
      if (ret != NO_ERROR)
	{
	  goto exit_on_error;
	}
    }

  heap_Bestspace = &heap_Bestspace_cache_area;

  pthread_mutex_init (&heap_Bestspace->bestspace_mutex, NULL);

  heap_Bestspace->num_stats_entries = 0;

  heap_Bestspace->hfid_ht =
    mht_create ("Memory hash HFID to {bestspace}",
		HEAP_STATS_ENTRY_MHT_EST_SIZE, heap_hash_hfid,
		heap_compare_hfid);
  if (heap_Bestspace->hfid_ht == NULL)
    {
      goto exit_on_error;
    }

  heap_Bestspace->vpid_ht =
    mht_create ("Memory hash VPID to {bestspace}",
		HEAP_STATS_ENTRY_MHT_EST_SIZE, heap_hash_vpid,
		heap_compare_vpid);
  if (heap_Bestspace->vpid_ht == NULL)
    {
      goto exit_on_error;
    }

  heap_Bestspace->num_alloc = 0;
  heap_Bestspace->num_free = 0;
  heap_Bestspace->free_list_count = 0;
  heap_Bestspace->free_list = NULL;

  return ret;

exit_on_error:

  return (ret == NO_ERROR) ? ER_FAILED : ret;
}

/*
 * heap_stats_bestspace_finalize () - Finish best space information
 *   return: NO_ERROR
 *
 * Note: Destroy hash table and memory for entries.
 */
static int
heap_stats_bestspace_finalize (void)
{
  HEAP_STATS_ENTRY *ent;
  int ret = NO_ERROR;

  if (heap_Bestspace == NULL)
    {
      return NO_ERROR;
    }

  if (heap_Bestspace->vpid_ht != NULL)
    {
      (void) mht_map_no_key (NULL, heap_Bestspace->vpid_ht,
			     heap_stats_entry_free, NULL);
      while (heap_Bestspace->free_list_count > 0)
	{
	  ent = heap_Bestspace->free_list;
	  assert_release (ent != NULL);

	  heap_Bestspace->free_list = ent->next;
	  ent->next = NULL;

	  free (ent);

	  heap_Bestspace->free_list_count--;
	}
      assert_release (heap_Bestspace->free_list == NULL);
    }

  if (heap_Bestspace->vpid_ht != NULL)
    {
      mht_destroy (heap_Bestspace->vpid_ht);
      heap_Bestspace->vpid_ht = NULL;
    }

  if (heap_Bestspace->hfid_ht != NULL)
    {
      mht_destroy (heap_Bestspace->hfid_ht);
      heap_Bestspace->hfid_ht = NULL;
    }

  pthread_mutex_destroy (&heap_Bestspace->bestspace_mutex);

  heap_Bestspace = NULL;

  return ret;
}

/*
 * heap_chnguess_decache () - Decache a specific entry or all entries
 *   return: NO_ERROR
 *   oid(in): oid: class oid or NULL
 *            IF NULL implies all classes
 *
 * Note: Remove from the hash the entry associated with given oid. If
 * oid is NULL, all entries in hash are removed.
 * This function is called when a class is updated or during
 * rollback when a class was changed
 */
static int
heap_chnguess_decache (const OID * oid)
{
  HEAP_CHNGUESS_ENTRY *entry;
  int ret = NO_ERROR;

  if (heap_Guesschn == NULL)
    {
      return NO_ERROR;		/* nop */
    }

  if (oid == NULL)
    {
      (void) mht_map (heap_Guesschn->ht, heap_chnguess_remove_entry, NULL);
    }
  else
    {
      entry = (HEAP_CHNGUESS_ENTRY *) mht_get (heap_Guesschn->ht, oid);
      if (entry != NULL)
	{
	  (void) heap_chnguess_remove_entry (oid, entry, NULL);
	}
    }

  if (heap_Guesschn->schema_change == true && oid == NULL)
    {
      heap_Guesschn->schema_change = false;
    }

  return ret;
}

/*
 * heap_chnguess_remove_entry () - Remove an entry from chnguess hash table
 *   return: NO_ERROR
 *   oid_key(in): Key (oid) of chnguess table
 *   ent(in): The entry of hash table
 *   xignore(in): Extra arguments (currently ignored)
 *
 * Note: Remove from the hash the given entry. The entry is marked as
 * for immediate reuse.
 */
static int
heap_chnguess_remove_entry (const void *oid_key, void *ent, void *xignore)
{
  HEAP_CHNGUESS_ENTRY *entry = (HEAP_CHNGUESS_ENTRY *) ent;

  /* mht_rem() has been updated to take a function and an arg pointer
   * that can be called on the entry before it is removed.  We may
   * want to take advantage of that here to free the memory associated
   * with the entry
   */
  (void) mht_rem (heap_Guesschn->ht, oid_key, NULL, NULL);
  OID_SET_NULL (&entry->oid);
  entry->chn = NULL_CHN;
  entry->recently_accessed = false;
  heap_Guesschn_area.clock_hand = entry->idx;

  return NO_ERROR;
}

#if defined (CUBRID_DEBUG)
/*
 * heap_chnguess_dump () - Dump current chnguess hash table
 *   return:
 *
 * Note: Dump all valid chnguess entries.
 */
void
heap_chnguess_dump (FILE * fp)
{
  int max_tranindex, tran_index, i;
  HEAP_CHNGUESS_ENTRY *entry;

  if (heap_Guesschn != NULL)
    {
      fprintf (fp, "*** Dump of CLASS_OID to CHNGUESS at clients *** \n");
      fprintf (fp, "Schema_change = %d, clock_hand = %d,\n",
	       heap_Guesschn->schema_change, heap_Guesschn->clock_hand);
      fprintf (fp, "Nentries = %d, Nactive_entries = %u,"
	       " maxnum of clients = %d, nbytes = %d\n",
	       heap_Guesschn->num_entries, mht_count (heap_Guesschn->ht),
	       heap_Guesschn->num_clients, heap_Guesschn->nbytes);
      fprintf (fp, "Hash Table = %p, Entries = %p, Bitindex = %p\n",
	       heap_Guesschn->ht, heap_Guesschn->entries,
	       heap_Guesschn->bitindex);

      max_tranindex = logtb_get_number_of_total_tran_indices ();
      for (i = 0; i < heap_Guesschn->num_entries; i++)
	{
	  entry = &heap_Guesschn_area.entries[i];

	  if (!OID_ISNULL (&entry->oid))
	    {
	      fprintf (fp, " \nEntry_id %d", entry->idx);
	      fprintf (fp,
		       "OID = %2d|%4d|%2d, chn = %d, recently_free = %d,",
		       entry->oid.volid, entry->oid.pageid, entry->oid.slotid,
		       entry->chn, entry->recently_accessed);

	      /* Dump one bit at a time */
	      for (tran_index = 0; tran_index < max_tranindex; tran_index++)
		{
		  if (tran_index % 40 == 0)
		    {
		      fprintf (fp, "\n ");
		    }
		  else if (tran_index % 10 == 0)
		    {
		      fprintf (fp, " ");
		    }
		  fprintf (fp, "%d",
			   HEAP_BIT_GET (entry->bits, tran_index) ? 1 : 0);
		}
	      fprintf (fp, "\n");
	    }
	}
    }
}
#endif /* CUBRID_DEBUG */

/*
 * heap_chnguess_get () - Guess chn of given oid for given tran index (at client)
 *   return:
 *   oid(in): OID from where to guess chn at client workspace
 *   tran_index(in): The client transaction index
 *
 * Note: Find/guess the chn of the given OID object at the workspace of
 * given client transaction index
 */
int
heap_chnguess_get (THREAD_ENTRY * thread_p, const OID * oid, int tran_index)
{
  int chn = NULL_CHN;
  HEAP_CHNGUESS_ENTRY *entry;

  if (csect_enter (thread_p, CSECT_HEAP_CHNGUESS, INF_WAIT) != NO_ERROR)
    {
      return NULL_CHN;
    }

  if (heap_Guesschn != NULL)
    {
      if (heap_Guesschn->num_clients <= tran_index)
	{
	  if (heap_chnguess_realloc () != NO_ERROR)
	    {
	      csect_exit (thread_p, CSECT_HEAP_CHNGUESS);
	      return NULL_CHN;
	    }
	}

      /*
       * Do we have this entry in hash table, if we do then check corresponding
       * bit for given client transaction index.
       */

      entry = (HEAP_CHNGUESS_ENTRY *) mht_get (heap_Guesschn->ht, oid);
      if (entry != NULL && HEAP_BIT_GET (entry->bits, tran_index))
	{
	  chn = entry->chn;
	}
    }

  csect_exit (thread_p, CSECT_HEAP_CHNGUESS);

  return chn;
}

/*
 * heap_chnguess_put () - Oid object is in the process of been sent to client
 *   return: chn or NULL_CHN if not cached
 *   oid(in): object oid
 *   tran_index(in): The client transaction index
 *   chn(in): cache coherency number.
 *
 * Note: Cache the information that object oid with chn has been sent
 * to client with trans_index.
 * If the function fails, it returns NULL_CHN. This failure is
 * more like a warning since the chnguess is just a caching structure.
 */
int
heap_chnguess_put (THREAD_ENTRY * thread_p, const OID * oid, int tran_index,
		   int chn)
{
  int i;
  bool can_continue;
  HEAP_CHNGUESS_ENTRY *entry;

  if (heap_Guesschn == NULL)
    {
      return NULL_CHN;
    }

  if (csect_enter (thread_p, CSECT_HEAP_CHNGUESS, INF_WAIT) != NO_ERROR)
    {
      return NULL_CHN;
    }

  if (heap_Guesschn->num_clients <= tran_index)
    {
      if (heap_chnguess_realloc () != NO_ERROR)
	{
	  csect_exit (thread_p, CSECT_HEAP_CHNGUESS);
	  return NULL_CHN;
	}
    }

  /*
   * Is the entry already in the chnguess hash table ?
   */
  entry = (HEAP_CHNGUESS_ENTRY *) mht_get (heap_Guesschn->ht, oid);
  if (entry != NULL)
    {
      /*
       * If the cache coherence number is different reset all client entries
       */
      if (entry->chn != chn)
	{
	  HEAP_NBYTES_CLEARED (entry->bits, heap_Guesschn_area.nbytes);
	  entry->chn = chn;
	}
    }
  else
    {
      /*
       * Replace one of the entries that has not been used for a while.
       * Follow clock replacement algorithm.
       */
      can_continue = true;
      while (entry == NULL && can_continue == true)
	{
	  can_continue = false;
	  for (i = 0; i < heap_Guesschn->num_entries; i++)
	    {
	      /*
	       * Increase the clock to next entry
	       */
	      heap_Guesschn->clock_hand++;
	      if (heap_Guesschn->clock_hand >= heap_Guesschn->num_entries)
		{
		  heap_Guesschn->clock_hand = 0;
		}

	      entry = &heap_Guesschn->entries[heap_Guesschn->clock_hand];
	      if (entry->recently_accessed == true)
		{
		  /*
		   * Set recently freed to false, so it can be replaced in next
		   * if the entry is not referenced
		   */
		  entry->recently_accessed = false;
		  entry = NULL;
		  can_continue = true;
		}
	      else
		{
		  entry->oid = *oid;
		  entry->chn = chn;
		  HEAP_NBYTES_CLEARED (entry->bits,
				       heap_Guesschn_area.nbytes);
		  break;
		}
	    }
	}
    }

  /*
   * Now set the desired client transaction index bit
   */
  if (entry != NULL)
    {
      HEAP_BIT_SET (entry->bits, tran_index);
      entry->recently_accessed = true;
    }
  else
    {
      chn = NULL_CHN;
    }

  csect_exit (thread_p, CSECT_HEAP_CHNGUESS);

  return chn;
}

/*
 * heap_chnguess_clear () - Clear any cached information for given client
 *                        used when client is shutdown
 *   return:
 *   tran_index(in): The client transaction index
 *
 * Note: Clear the transaction index bit for all chnguess entries.
 */
void
heap_chnguess_clear (THREAD_ENTRY * thread_p, int tran_index)
{
  int i;
  HEAP_CHNGUESS_ENTRY *entry;

  if (csect_enter (thread_p, CSECT_HEAP_CHNGUESS, INF_WAIT) != NO_ERROR)
    {
      return;
    }

  if (heap_Guesschn != NULL)
    {
      for (i = 0; i < heap_Guesschn->num_entries; i++)
	{
	  entry = &heap_Guesschn_area.entries[i];
	  if (!OID_ISNULL (&entry->oid))
	    {
	      HEAP_BIT_CLEAR (entry->bits, (unsigned int) tran_index);
	    }
	}
    }

  csect_exit (thread_p, CSECT_HEAP_CHNGUESS);

}

/*
 * Recovery functions
 */

/*
 * heap_rv_redo_newpage_internal () - Redo the statistics or a new page
 *                                    allocation for a heap file
 *   return: int
 *   rcv(in): Recovery structure
 *   reuse_oid(in): whether the heap is OID reusable
 */
static int
heap_rv_redo_newpage_internal (THREAD_ENTRY * thread_p, LOG_RCV * rcv,
			       const bool reuse_oid)
{
  RECDES recdes;
  INT16 slotid;
  int sp_success;

  /* Initialize header page */
  spage_initialize (thread_p, rcv->pgptr, heap_get_spage_type (),
		    HEAP_MAX_ALIGN, SAFEGUARD_RVSPACE);

  /* Now insert first record (either statistics or chain record) */
  recdes.area_size = recdes.length = rcv->length;
  recdes.type = REC_HOME;
  recdes.data = (char *) rcv->data;
  sp_success = spage_insert (thread_p, rcv->pgptr, &recdes, &slotid);
  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);

  if (sp_success != SP_SUCCESS || slotid != HEAP_HEADER_AND_CHAIN_SLOTID)
    {
      if (sp_success != SP_SUCCESS)
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR,
		  0);
	}
      /* something went wrong. Unable to redo initialization of new heap page */
      return er_errid ();
    }

  return NO_ERROR;
}

/*
 * heap_rv_redo_newpage () - Redo the statistics or a new page allocation for
 *                           a heap file
 *   return: int
 *   rcv(in): Recovery structure
 */
int
heap_rv_redo_newpage (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  return heap_rv_redo_newpage_internal (thread_p, rcv, false);
}

/*
 * heap_rv_redo_newpage_reuse_oid () - Redo the statistics or a new page
 *                                     allocation for a heap file with
 *                                     reusable OIDs
 *   return: int
 *   rcv(in): Recovery structure
 */
int
heap_rv_redo_newpage_reuse_oid (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  return heap_rv_redo_newpage_internal (thread_p, rcv, true);
}

/*
 * heap_rv_undoredo_pagehdr () - Recover the header of a heap page
 *                    (either statistics/chain)
 *   return: int
 *   rcv(in): Recovery structure
 *
 * Note: Recover the update of the header or a heap page. The header
 * can be the heap header or a chain header.
 */
int
heap_rv_undoredo_pagehdr (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  RECDES recdes;
  int sp_success;

  recdes.area_size = recdes.length = rcv->length;
  recdes.type = REC_HOME;
  recdes.data = (char *) rcv->data;

  sp_success = spage_update (thread_p, rcv->pgptr,
			     HEAP_HEADER_AND_CHAIN_SLOTID, &recdes);
  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);

  if (sp_success != SP_SUCCESS)
    {
      /* something went wrong. Unable to redo update statistics for chain */
      if (sp_success != SP_ERROR)
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR,
		  0);
	}
      return er_errid ();
    }
  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);

  return NO_ERROR;
}

/*
 * heap_rv_dump_statistics () - Dump statistics recovery information
 *   return: int
 *   ignore_length(in): Length of Recovery Data
 *   data(in): The data being logged
 *
 * Note: Dump statistics recovery information
 */
void
heap_rv_dump_statistics (FILE * fp, int ignore_length, void *data)
{
  int ret = NO_ERROR;

  HEAP_HDR_STATS *heap_hdr;	/* Header of heap structure    */

  heap_hdr = (HEAP_HDR_STATS *) data;
  ret = heap_dump_hdr (fp, heap_hdr);
}

/*
 * heap_rv_dump_chain () - Dump chain recovery information
 *   return: int
 *   ignore_length(in): Length of Recovery Data
 *   data(in): The data being logged
 */
void
heap_rv_dump_chain (FILE * fp, int ignore_length, void *data)
{
  HEAP_CHAIN *chain;

  chain = (HEAP_CHAIN *) data;
  fprintf (fp, "CLASS_OID = %2d|%4d|%2d, "
	   "PREV_VPID = %2d|%4d, NEXT_VPID = %2d|%4d\n",
	   chain->class_oid.volid, chain->class_oid.pageid,
	   chain->class_oid.slotid,
	   chain->prev_vpid.volid, chain->prev_vpid.pageid,
	   chain->next_vpid.volid, chain->next_vpid.pageid);
}

/*
 * heap_rv_redo_insert () - Redo the insertion of an object
 *   return: int
 *   rcv(in): Recovery structure
 *
 * Note: Redo the insertion of an object at a specific location (OID).
 */
int
heap_rv_redo_insert (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  INT16 slotid;
  RECDES recdes;
  int sp_success;

  slotid = rcv->offset;
  recdes.type = *(INT16 *) (rcv->data);
  recdes.data = (char *) (rcv->data) + sizeof (recdes.type);
  recdes.area_size = recdes.length = rcv->length - sizeof (recdes.type);

  if (recdes.type == REC_ASSIGN_ADDRESS)
    {
      /*
       * The data here isn't really the data to be inserted (because there
       * wasn't any); instead it's the number of bytes that were reserved
       * for future insertion.  Change recdes.length to reflect the number
       * of bytes to reserve, but there's no need for a valid recdes.data:
       * spage_insert_for_recovery knows to ignore it in this case.
       */
      recdes.area_size = recdes.length = *(INT16 *) recdes.data;
      recdes.data = NULL;
    }

  sp_success = spage_insert_for_recovery (thread_p, rcv->pgptr, slotid,
					  &recdes);
  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);

  if (sp_success != SP_SUCCESS)
    {
      /* Unable to redo insertion */
      if (sp_success != SP_ERROR)
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR,
		  0);
	}
      return er_errid ();
    }

  return NO_ERROR;
}

/*
 * heap_rv_undo_insert () - Undo the insertion of an object.
 *   return: int
 *   rcv(in): Recovery structure
 *
 * Note: Delete an object for recovery purposes. The OID of the object
 * is reused since the object was never committed.
 */
int
heap_rv_undo_insert (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  INT16 slotid;

  slotid = rcv->offset;
  (void) spage_delete_for_recovery (thread_p, rcv->pgptr, slotid);
  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);

  return NO_ERROR;
}

/*
 * heap_rv_redo_delete () - Redo the deletion of an object
 *   return: int
 *   rcv(in): Recovery structure
 *
 * Note: Redo the deletion of an object.
 * The OID of the object is not reuse since we don't know if the object was a
 * newly created object.
 */
int
heap_rv_redo_delete (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  INT16 slotid;

  slotid = rcv->offset;
  (void) spage_delete (thread_p, rcv->pgptr, slotid);
  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);

  return NO_ERROR;
}

/*
 * heap_rv_redo_delete_newhome () - Redo the deletion of a new home object
 *   return: int
 *   rcv(in): Recovery structure
 *
 * Note: Redo the deletion of an object.
 * The NEW HOME OID is reused since it is not the real OID.
 */
int
heap_rv_redo_delete_newhome (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  return heap_rv_undo_insert (thread_p, rcv);
}

/*
 * heap_rv_redo_mark_reusable_slot () - Marks a deleted slot as reusable; used
 *                                      as a postponed log operation and a
 *                                      REDO function
 *   return: int
 *   rcv(in): Recovery structure
 *
 * Note: Mark (during postponed operation execution)/Redo (during recovery)
 *       the marking of a deleted slot as reusable.
 */
int
heap_rv_redo_mark_reusable_slot (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  INT16 slotid;

  slotid = rcv->offset;
  (void) spage_mark_deleted_slot_as_reusable (thread_p, rcv->pgptr, slotid);
  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);

  return NO_ERROR;
}

/*
 * heap_rv_undo_delete () - Undo the deletion of an object
 *   return: int
 *   rcv(in): Recovery structure
 */
int
heap_rv_undo_delete (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  return heap_rv_redo_insert (thread_p, rcv);
}

/*
 * heap_rv_undoredo_update () - Recover an update either for undo or redo
 *   return: int
 *   rcv(in): Recovery structure
 *
 * Note: Recover an update to an object in a slotted page
 */
int
heap_rv_undoredo_update (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  INT16 slotid;
  RECDES recdes;
  int sp_success;

  slotid = rcv->offset;
  recdes.type = *(INT16 *) (rcv->data);
  recdes.data = (char *) (rcv->data) + sizeof (recdes.type);
  recdes.area_size = recdes.length = rcv->length - sizeof (recdes.type);
  if (recdes.area_size <= 0)
    {
      sp_success = SP_SUCCESS;
    }
  else
    {
      sp_success = spage_update (thread_p, rcv->pgptr, slotid, &recdes);
    }

  if (sp_success != SP_SUCCESS)
    {
      /* Unable to recover update for object */
      pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);
      if (sp_success != SP_ERROR)
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR,
		  0);
	}
      return er_errid ();
    }
  spage_update_record_type (thread_p, rcv->pgptr, slotid, recdes.type);
  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);

  return NO_ERROR;
}

/*
 * heap_rv_undoredo_update_type () - Recover the type of the object/record. used either for
 *                        undo or redo
 *   return: int
 *   rcv(in): Recovery structure
 *
 * Note: Recover an update to an object in a slotted page
 */
int
heap_rv_undoredo_update_type (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  INT16 slotid;
  INT16 type;

  slotid = rcv->offset;
  type = *(INT16 *) (rcv->data);
  spage_update_record_type (thread_p, rcv->pgptr, slotid, type);
  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);

  return NO_ERROR;
}

/*
 * heap_rv_redo_reuse_page () - Redo the deletion of all objects in page for
 *                              reuse purposes
 *   return: int
 *   rcv(in): Recovery structure
 */
int
heap_rv_redo_reuse_page (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  VPID vpid;
  RECDES recdes;
  HEAP_CHAIN *chain;		/* Chain to next and prev page */
  int sp_success;
  const bool is_header_page = ((rcv->offset != 0) ? true : false);

  vpid.volid = pgbuf_get_volume_id (rcv->pgptr);
  vpid.pageid = pgbuf_get_page_id (rcv->pgptr);

  /* We ignore the return value. It should be true (objects were deleted)
   * except for the scenario when the redo actions are applied twice.
   */
  (void) heap_delete_all_page_records (thread_p, &vpid, rcv->pgptr);

  /* At here, do not consider the header of heap.
   * Later redo the update of the header of heap at RVHF_STATS log.
   */
  if (!is_header_page)
    {
      sp_success =
	spage_get_record (rcv->pgptr, HEAP_HEADER_AND_CHAIN_SLOTID, &recdes,
			  PEEK);
      if (sp_success != SP_SUCCESS)
	{
	  /* something went wrong. Unable to redo update class_oid */
	  if (sp_success != SP_ERROR)
	    {
	      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_GENERIC_ERROR, 0);
	    }
	  return er_errid ();
	}

      chain = (HEAP_CHAIN *) recdes.data;
      COPY_OID (&(chain->class_oid), (OID *) (rcv->data));
    }

  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);

  return NO_ERROR;
}

/*
 * heap_rv_redo_reuse_page_reuse_oid () - Redo the deletion of all objects in
 *                                        a reusable oid heap page for reuse
 *                                        purposes
 *   return: int
 *   rcv(in): Recovery structure
 */
int
heap_rv_redo_reuse_page_reuse_oid (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  RECDES recdes;
  HEAP_CHAIN *chain;		/* Chain to next and prev page */
  int sp_success;
  const bool is_header_page = ((rcv->offset != 0) ? true : false);

  (void) heap_reinitialize_page (thread_p, rcv->pgptr, is_header_page);

  /* At here, do not consider the header of heap.
   * Later redo the update of the header of heap at RVHF_STATS log.
   */
  if (!is_header_page)
    {
      sp_success =
	spage_get_record (rcv->pgptr, HEAP_HEADER_AND_CHAIN_SLOTID, &recdes,
			  PEEK);
      if (sp_success != SP_SUCCESS)
	{
	  /* something went wrong. Unable to redo update class_oid */
	  if (sp_success != SP_ERROR)
	    {
	      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_GENERIC_ERROR, 0);
	    }
	  return er_errid ();
	}

      chain = (HEAP_CHAIN *) recdes.data;
      COPY_OID (&(chain->class_oid), (OID *) (rcv->data));
    }

  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);

  return NO_ERROR;
}

/*
 * heap_rv_dump_reuse_page () - Dump reuse page
 *   return: int
 *   ignore_length(in): Length of Recovery Data
 *   ignore_data(in): The data being logged
 *
 * Note: Dump information about reuse of page.
 */
void
heap_rv_dump_reuse_page (FILE * fp, int ignore_length, void *ignore_data)
{
  fprintf (fp, "Delete all objects in page for reuse purposes of page\n");
}

/*
 * xheap_get_class_num_objects_pages () -
 *   return: NO_ERROR
 *   hfid(in):
 *   approximation(in):
 *   nobjs(in):
 *   npages(in):
 */
int
xheap_get_class_num_objects_pages (THREAD_ENTRY * thread_p, const HFID * hfid,
				   int approximation, int *nobjs, int *npages)
{
  int length, num;
  int ret;

  if (approximation)
    {
      num = heap_estimate (thread_p, hfid, npages, nobjs, &length);
    }
  else
    {
      num = heap_get_num_objects (thread_p, hfid, npages, nobjs, &length);
    }

  if (num < 0)
    {
      return (((ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret);
    }

  return NO_ERROR;
}

/*
 * xheap_has_instance () -
 *   return:
 *   hfid(in):
 *   class_oid(in):
 */
int
xheap_has_instance (THREAD_ENTRY * thread_p, const HFID * hfid,
		    OID * class_oid)
{
  OID oid;
  HEAP_SCANCACHE scan_cache;
  RECDES recdes;
  SCAN_CODE r;

  OID_SET_NULL (&oid);

  if (heap_scancache_start (thread_p, &scan_cache, hfid, class_oid, true,
			    false, LOCKHINT_NONE) != NO_ERROR)
    {
      return ER_FAILED;
    }

  r = heap_first (thread_p, hfid, class_oid, &oid, &recdes, &scan_cache,
		  true);
  heap_scancache_end (thread_p, &scan_cache);

  if (r == S_ERROR)
    {
      return ER_FAILED;
    }
  else if (r == S_DOESNT_EXIST || r == S_END)
    {
      return 0;
    }
  else
    {
      return 1;
    }
}

/*
 * heap_get_class_repr_id () -
 *   return:
 *   class_oid(in):
 */
REPR_ID
heap_get_class_repr_id (THREAD_ENTRY * thread_p, OID * class_oid)
{
  OR_CLASSREP *rep;
  REPR_ID id;
  int idx_incache = -1;

  if (!class_oid || !idx_incache)
    {
      return 0;
    }

  rep = heap_classrepr_get (thread_p, class_oid, NULL, 0, &idx_incache, true);
  if (rep == NULL)
    {
      return 0;
    }

  id = rep->id;
  (void) heap_classrepr_free (rep, &idx_incache);

  return id;
}

/*
 * heap_set_autoincrement_value () -
 *   return: NO_ERROR, or ER_code
 *   attr_info(in):
 *   scan_cache(in):
 */
int
heap_set_autoincrement_value (THREAD_ENTRY * thread_p,
			      HEAP_CACHE_ATTRINFO * attr_info,
			      HEAP_SCANCACHE * scan_cache)
{
  int i, idx_in_cache;
  char *classname;
  const char *attr_name;
  RECDES recdes;		/* Used to obtain attribute name */
  char serial_name[AUTO_INCREMENT_SERIAL_NAME_MAX_LENGTH];
  HEAP_ATTRVALUE *value;
  DB_VALUE dbvalue_numeric, *dbvalue, key_val;
  OR_ATTRIBUTE *att;
  OID serial_class_oid;
  LC_FIND_CLASSNAME status;
  OR_CLASSREP *classrep;
  BTID serial_btid;
  DB_DATA_STATUS data_stat;

  if (!attr_info || !scan_cache)
    {
      return ER_FAILED;
    }

  recdes.data = NULL;
  recdes.area_size = 0;

  for (i = 0; i < attr_info->num_values; i++)
    {
      value = &attr_info->values[i];
      dbvalue = &value->dbvalue;
      att = &attr_info->last_classrepr->attributes[i];

      if (att->is_autoincrement && (value->state == HEAP_UNINIT_ATTRVALUE))
	{
	  if (OID_ISNULL (&(att->serial_obj)))
	    {
	      memset (serial_name, '\0', sizeof (serial_name));
	      recdes.data = NULL;
	      recdes.area_size = 0;
	      if (heap_get (thread_p, &(attr_info->class_oid), &recdes,
			    scan_cache, PEEK, NULL_CHN) != S_SUCCESS)
		{
		  return ER_FAILED;
		}

	      classname = heap_get_class_name (thread_p, &(att->classoid));
	      if (classname == NULL)
		{
		  return ER_FAILED;
		}

	      attr_name = or_get_attrname (&recdes, att->id);
	      if (attr_name == NULL)
		{
		  free_and_init (classname);
		  return ER_FAILED;
		}

	      SET_AUTO_INCREMENT_SERIAL_NAME (serial_name, classname,
					      attr_name);

	      free_and_init (classname);

	      if (db_make_varchar (&key_val, DB_MAX_IDENTIFIER_LENGTH,
				   serial_name,
				   strlen (serial_name),
				   LANG_SYS_CODESET, LANG_SYS_COLLATION)
		  != NO_ERROR)
		{
		  return ER_FAILED;
		}

	      status = xlocator_find_class_oid (thread_p, CT_SERIAL_NAME,
						&serial_class_oid, NULL_LOCK);
	      if (status == LC_CLASSNAME_ERROR
		  || status == LC_CLASSNAME_DELETED)
		{
		  return ER_FAILED;
		}

	      classrep = heap_classrepr_get (thread_p, &serial_class_oid,
					     NULL, 0, &idx_in_cache, true);
	      if (classrep == NULL)
		{
		  return ER_FAILED;
		}

	      if (classrep->indexes)
		{
		  BTREE_SEARCH ret;

		  BTID_COPY (&serial_btid, &(classrep->indexes[0].btid));
		  ret = xbtree_find_unique (thread_p, &serial_btid, true,
					    S_SELECT, &key_val,
					    &serial_class_oid,
					    &(att->serial_obj), false);
		  if (heap_classrepr_free (classrep, &idx_in_cache) !=
		      NO_ERROR)
		    {
		      return ER_FAILED;
		    }
		  if (ret != BTREE_KEY_FOUND)
		    {
		      return ER_FAILED;
		    }
		}
	      else
		{
		  (void) heap_classrepr_free (classrep, &idx_in_cache);
		  return ER_FAILED;
		}
	    }

	  if ((att->type == DB_TYPE_SHORT) || (att->type == DB_TYPE_INTEGER)
	      || (att->type == DB_TYPE_BIGINT))
	    {
	      if (xserial_get_next_value (thread_p, &dbvalue_numeric, &att->serial_obj, 0,	/* no cache */
					  1,	/* generate one value */
					  GENERATE_AUTO_INCREMENT,
					  false) != NO_ERROR)
		{
		  return ER_FAILED;
		}

	      if (numeric_db_value_coerce_from_num (&dbvalue_numeric,
						    dbvalue,
						    &data_stat) != NO_ERROR)
		{
		  return ER_FAILED;
		}
	    }
	  else if (att->type == DB_TYPE_NUMERIC)
	    {
	      if (xserial_get_next_value (thread_p, dbvalue, &att->serial_obj, 0,	/* no cache */
					  1,	/* generate one value */
					  GENERATE_AUTO_INCREMENT,
					  false) != NO_ERROR)
		{
		  return ER_FAILED;
		}
	    }

	  value->state = HEAP_READ_ATTRVALUE;
	}
    }

  return NO_ERROR;
}

/*
 * heap_attrinfo_set_uninitialized_global () -
 *   return: NO_ERROR
 *   inst_oid(in):
 *   recdes(in):
 *   attr_info(in):
 */
int
heap_attrinfo_set_uninitialized_global (THREAD_ENTRY * thread_p,
					OID * inst_oid, RECDES * recdes,
					HEAP_CACHE_ATTRINFO * attr_info)
{
  if (attr_info == NULL)
    {
      return ER_FAILED;
    }

  return heap_attrinfo_set_uninitialized (thread_p, inst_oid, recdes,
					  attr_info);
}

/*
 * heap_get_hfid_from_class_oid () - get HFID from class oid
 *   return: error_code
 *   class_oid(in): class oid
 *   hfid(out):  the resulting hfid
 */
int
heap_get_hfid_from_class_oid (THREAD_ENTRY * thread_p, OID * class_oid,
			      HFID * hfid)
{
  int error_code = NO_ERROR;
  RECDES recdes;
  HEAP_SCANCACHE scan_cache;

  if (class_oid == NULL || hfid == NULL)
    {
      return ER_FAILED;
    }

  error_code = heap_scancache_quick_start (&scan_cache);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  if (heap_get (thread_p, class_oid, &recdes, &scan_cache, PEEK,
		NULL_CHN) != S_SUCCESS)
    {
      heap_scancache_end (thread_p, &scan_cache);
      return ER_FAILED;
    }

  orc_class_hfid_from_record (&recdes, hfid);

  error_code = heap_scancache_end (thread_p, &scan_cache);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return error_code;
}

/*
 * heap_compact_pages () - compact all pages from hfid of specified class OID
 *   return: error_code
 *   class_oid(out):  the class oid
 */
int
heap_compact_pages (THREAD_ENTRY * thread_p, OID * class_oid)
{
  int ret = NO_ERROR;
  VPID vpid;
  PAGE_PTR hdr_pgptr = NULL;
  VPID next_vpid;
  PAGE_PTR pgptr = NULL;
  LOG_DATA_ADDR addr;
  int lock_timeout = 2000;
  HFID hfid;

  if (class_oid == NULL)
    {
      return ER_QPROC_INVALID_PARAMETER;
    }

  if (lock_object (thread_p, class_oid, oid_Root_class_oid, IS_LOCK,
		   LK_UNCOND_LOCK) != LK_GRANTED)
    {
      return ER_FAILED;
    }

  ret = heap_get_hfid_from_class_oid (thread_p, class_oid, &hfid);
  if (ret != NO_ERROR || HFID_IS_NULL (&hfid))
    {
      lock_unlock_object (thread_p, class_oid, oid_Root_class_oid,
			  IS_LOCK, true);
      return ret;
    }

  addr.vfid = &hfid.vfid;
  addr.pgptr = NULL;
  addr.offset = 0;

  vpid.volid = hfid.vfid.volid;
  vpid.pageid = hfid.hpgid;

  hdr_pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ,
			 PGBUF_UNCONDITIONAL_LATCH);
  if (hdr_pgptr == NULL)
    {
      lock_unlock_object (thread_p, class_oid, oid_Root_class_oid,
			  IS_LOCK, true);
      ret = ER_FAILED;
      goto exit_on_error;
    }

  lock_unlock_object (thread_p, class_oid, oid_Root_class_oid, IS_LOCK, true);

  next_vpid = vpid;
  while (!VPID_ISNULL (&next_vpid))
    {
      vpid = next_vpid;
      pgptr = heap_scan_pb_lock_and_fetch (thread_p, &vpid, OLD_PAGE, X_LOCK,
					   NULL);
      if (pgptr == NULL)
	{
	  ret = ER_FAILED;
	  goto exit_on_error;
	}

      ret = heap_vpid_next (&hfid, pgptr, &next_vpid);
      if (ret != NO_ERROR)
	{
	  pgbuf_unfix_and_init (thread_p, pgptr);
	  goto exit_on_error;
	}

      if (spage_compact (pgptr) != NO_ERROR)
	{
	  pgbuf_unfix_and_init (thread_p, pgptr);
	  ret = ER_FAILED;
	  goto exit_on_error;
	}

      addr.pgptr = pgptr;
      log_skip_logging (thread_p, &addr);
      pgbuf_set_dirty (thread_p, pgptr, DONT_FREE);
      pgbuf_unfix_and_init (thread_p, pgptr);
    }

  pgbuf_unfix_and_init (thread_p, hdr_pgptr);
  hdr_pgptr = NULL;

  return ret;

exit_on_error:

  if (hdr_pgptr)
    {
      pgbuf_unfix_and_init (thread_p, hdr_pgptr);
    }

  return ret;
}

/*
 * heap_classrepr_dump_all () - dump all representations belongs to a class
 *   return: none
 *   fp(in): file pointer to print out
 *   class_oid(in): class oid to be dumped
 */
void
heap_classrepr_dump_all (THREAD_ENTRY * thread_p, FILE * fp, OID * class_oid)
{
  RECDES peek_recdes;
  HEAP_SCANCACHE scan_cache;
  OR_CLASSREP **rep_all;
  int count, i;
  char *classname;
  bool need_free_classname = false;

  classname = heap_get_class_name (thread_p, class_oid);
  if (classname == NULL)
    {
      classname = (char *) "unknown";
    }
  else
    {
      need_free_classname = true;
    }

  heap_scancache_quick_start (&scan_cache);

  if (heap_get (thread_p, class_oid, &peek_recdes, &scan_cache,
		PEEK, NULL_CHN) == S_SUCCESS)
    {
      rep_all = or_get_all_representation (&peek_recdes, true, &count);
      fprintf (fp, "*** Dumping representations of class %s\n"
	       "    Classname = %s, Class-OID = %d|%d|%d, #Repr = %d\n\n",
	       classname, classname, (int) class_oid->volid,
	       class_oid->pageid, (int) class_oid->slotid, count);

      for (i = 0; i < count; i++)
	{
	  assert (rep_all[i] != NULL);
	  heap_classrepr_dump (thread_p, fp, class_oid, rep_all[i]);
	  or_free_classrep (rep_all[i]);
	}

      fprintf (fp, "\n*** End of dump.\n");
      free_and_init (rep_all);
    }

  heap_scancache_end (thread_p, &scan_cache);

  if (need_free_classname)
    {
      free_and_init (classname);
    }
}

/*
 * heap_get_btid_from_index_name () - gets the BTID of an index using its name
 *				      and OID of class
 *
 *   return: NO_ERROR, or error code
 *   thread_p(in)   : thread context
 *   p_class_oid(in): OID of class
 *   index_name(in) : name of index
 *   p_found_btid(out): the BTREE ID of index
 *
 *  Note : the 'p_found_btid' argument must be a pointer to a BTID value,
 *	   the found BTID is 'BTID_COPY-ed' into it.
 *	   Null arguments are not allowed.
 *	   If an index name is not found, the 'p_found_btid' is returned as
 *	   NULL BTID and no error is set.
 *
 */
int
heap_get_btid_from_index_name (THREAD_ENTRY * thread_p,
			       const OID * p_class_oid,
			       const char *index_name, BTID * p_found_btid)
{
  int error = NO_ERROR;
  int classrepr_cacheindex = -1;
  int idx_cnt;
  OR_CLASSREP *classrepr = NULL;
  OR_INDEX *curr_index = NULL;

  assert (p_found_btid != NULL);
  assert (p_class_oid != NULL);
  assert (index_name != NULL);

  BTID_SET_NULL (p_found_btid);

  /* get the BTID associated from the index name : the only structure
   * containing this info is OR_CLASSREP */

  /* get class representation */
  classrepr = heap_classrepr_get (thread_p, (OID *) p_class_oid, NULL, 0,
				  &classrepr_cacheindex, true);

  if (classrepr == NULL)
    {
      error = er_errid ();
      goto exit;
    }

  /* iterate through indexes looking for index name */
  for (idx_cnt = 0, curr_index = classrepr->indexes;
       idx_cnt < classrepr->n_indexes; idx_cnt++, curr_index++)
    {
      if (curr_index == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_UNEXPECTED, 0);
	  error = ER_UNEXPECTED;
	  goto exit_cleanup;
	}

      if (intl_identifier_casecmp (curr_index->btname, index_name) == 0)
	{
	  BTID_COPY (p_found_btid, &(curr_index->btid));
	  break;
	}
    }

exit_cleanup:
  if (classrepr)
    {
      (void) heap_classrepr_free (classrepr, &classrepr_cacheindex);
    }

exit:
  return error;
}

/*
 * heap_object_upgrade_domain - upgrades a single attibute in an instance from
 *				the domain of current representation to the
 *				domain of the last representation.
 *
 *    return: error code , NO_ERROR if no error occured
 *    thread_p(in) : thread context
 *    upd_scancache(in): scan context
 *    attr_info(in): aatribute info structure
 *    oid(in): the oid of the object to process
 *    att_id(in): attribute id within the class (same as in schema)
 *
 *  Note : this function is used in ALTER CHANGE (with type change syntax)
 */
int
heap_object_upgrade_domain (THREAD_ENTRY * thread_p,
			    HEAP_SCANCACHE * upd_scancache,
			    HEAP_CACHE_ATTRINFO * attr_info, OID * oid,
			    const ATTR_ID att_id)
{
  int i = 0, error = NO_ERROR;
  int updated_flag = 0;
  HEAP_ATTRVALUE *value = NULL;
  int force_count = 0, updated_n_attrs_id = 0;
  ATTR_ID atts_id[1] = { 0 };
  DB_VALUE orig_value;

  DB_MAKE_NULL (&orig_value);

  if (upd_scancache == NULL || attr_info == NULL || oid == NULL)
    {
      error = ER_UNEXPECTED;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      goto exit;
    }

  for (i = 0, value = attr_info->values;
       i < attr_info->num_values; i++, value++)
    {
      TP_DOMAIN *dest_dom = value->last_attrepr->domain;
      bool log_warning = false;
      int warning_code = NO_ERROR;
      DB_TYPE dest_type;
      DB_TYPE src_type = DB_VALUE_DOMAIN_TYPE (&(value->dbvalue));
      int curr_prec = 0;
      int dest_prec = 0;

      dest_type = TP_DOMAIN_TYPE (dest_dom);

      if (att_id != value->attrid)
	{
	  continue;
	}

      if (QSTR_IS_BIT (src_type))
	{
	  curr_prec = DB_GET_STRING_LENGTH (&(value->dbvalue));
	}
      else if (QSTR_IS_ANY_CHAR (src_type))
	{
	  if (TP_DOMAIN_CODESET (dest_dom) == INTL_CODESET_ISO88591)
	    {
	      curr_prec = DB_GET_STRING_SIZE (&(value->dbvalue));
	    }
	  else if (!DB_IS_NULL (&(value->dbvalue)))
	    {
	      curr_prec = DB_GET_STRING_LENGTH (&(value->dbvalue));
	    }
	  else
	    {
	      curr_prec = dest_dom->precision;
	    }
	}

      dest_prec = dest_dom->precision;

      if (QSTR_IS_ANY_CHAR_OR_BIT (src_type) &&
	  QSTR_IS_ANY_CHAR_OR_BIT (dest_type))
	{
	  /* check phase of ALTER TABLE .. CHANGE should not allow changing
	   * the domains from one flavour to another :*/
	  assert ((QSTR_IS_CHAR (src_type) && QSTR_IS_CHAR (dest_type)) ||
		  (!QSTR_IS_CHAR (src_type) && !QSTR_IS_CHAR (dest_type)));

	  assert ((QSTR_IS_NATIONAL_CHAR (src_type)
		   && QSTR_IS_NATIONAL_CHAR (dest_type)) ||
		  (!QSTR_IS_NATIONAL_CHAR (src_type)
		   && !QSTR_IS_NATIONAL_CHAR (dest_type)));

	  assert ((QSTR_IS_BIT (src_type) && QSTR_IS_BIT (dest_type)) ||
		  (!QSTR_IS_BIT (src_type) && !QSTR_IS_BIT (dest_type)));

	  /* check string truncation */
	  if (dest_prec < curr_prec)
	    {
	      if (prm_get_bool_value (PRM_ID_ALTER_TABLE_CHANGE_TYPE_STRICT)
		  == true)
		{
		  error = ER_ALTER_CHANGE_TRUNC_OVERFLOW_NOT_ALLOWED;
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
		  goto exit;
		}
	      else
		{
		  /* allow truncation in cast, just warning */
		  log_warning = true;
		  warning_code = ER_QPROC_SIZE_STRING_TRUNCATED;
		}
	    }
	}

      error = pr_clone_value (&(value->dbvalue), &orig_value);
      if (error != NO_ERROR)
	{
	  goto exit;
	}

      if (TP_IS_CHAR_TYPE (TP_DOMAIN_TYPE (dest_dom))
	  && !(TP_IS_CHAR_TYPE (src_type) || src_type == DB_TYPE_ENUMERATION)
	  && prm_get_bool_value (PRM_ID_ALTER_TABLE_CHANGE_TYPE_STRICT)
	  == false)
	{
	  /* If destination is char/varchar, we need to first cast the value
	   * to a string with no precision, then to destination type with
	   * the desired precision.
	   */
	  TP_DOMAIN *string_dom;
	  if (TP_DOMAIN_TYPE (dest_dom) == DB_TYPE_NCHAR
	      || TP_DOMAIN_TYPE (dest_dom) == DB_TYPE_VARNCHAR)
	    {
	      string_dom = tp_domain_resolve_default (DB_TYPE_VARNCHAR);
	    }
	  else
	    {
	      string_dom = tp_domain_resolve_default (DB_TYPE_VARCHAR);
	    }
	  error = db_value_coerce (&(value->dbvalue), &(value->dbvalue),
				   string_dom);
	}

      if (error == NO_ERROR)
	{
	  error = db_value_coerce (&(value->dbvalue), &(value->dbvalue),
				   dest_dom);
	}
      if (error != NO_ERROR)
	{
	  bool set_default_value = false;
	  bool set_min_value = false;
	  bool set_max_value = false;

	  if (prm_get_bool_value (PRM_ID_ALTER_TABLE_CHANGE_TYPE_STRICT) ==
	      true)
	    {
	      error = ER_ALTER_CHANGE_TRUNC_OVERFLOW_NOT_ALLOWED;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	      goto exit;
	    }

	  if (error == ER_IT_DATA_OVERFLOW)
	    {
	      int is_positive = -1;	/* -1:UNKNOWN, 0:negative, 1:positive */

	      /* determine sign of orginal value: */
	      switch (src_type)
		{
		case DB_TYPE_INTEGER:
		  is_positive = ((DB_GET_INTEGER (&value->dbvalue) >= 0)
				 ? 1 : 0);
		  break;
		case DB_TYPE_SMALLINT:
		  is_positive = ((DB_GET_SHORT (&value->dbvalue) >= 0)
				 ? 1 : 0);
		  break;
		case DB_TYPE_BIGINT:
		  is_positive = ((DB_GET_BIGINT (&value->dbvalue) >= 0)
				 ? 1 : 0);
		  break;
		case DB_TYPE_FLOAT:
		  is_positive = ((DB_GET_FLOAT (&value->dbvalue) >= 0)
				 ? 1 : 0);
		  break;
		case DB_TYPE_DOUBLE:
		  is_positive =
		    ((DB_GET_DOUBLE (&value->dbvalue) >= 0) ? 1 : 0);
		  break;
		case DB_TYPE_NUMERIC:
		  is_positive =
		    numeric_db_value_is_positive (&value->dbvalue);
		  break;
		case DB_TYPE_MONETARY:
		  is_positive =
		    ((DB_GET_MONETARY (&value->dbvalue)->amount >=
		      0) ? 1 : 0);
		  break;

		case DB_TYPE_CHAR:
		case DB_TYPE_VARCHAR:
		case DB_TYPE_NCHAR:
		case DB_TYPE_VARNCHAR:
		  {
		    char *str = DB_PULL_STRING (&(value->dbvalue));
		    char *str_end = str +
		      DB_GET_STRING_LENGTH (&(value->dbvalue));
		    char *p = NULL;

		    /* get the sign in the source string; look directly into
		     * the buffer string, no copy */
		    p = str;
		    while (char_isspace (*p) && p < str_end)
		      {
			p++;
		      }

		    is_positive = ((p < str_end && (*p) == '-') ? 0 : 1);
		    break;
		  }

		default:
		  is_positive = -1;
		  break;
		}

	      if (is_positive == 1)
		{
		  set_max_value = true;
		}
	      else if (is_positive == 0)
		{
		  set_min_value = true;
		}
	      else
		{
		  set_default_value = true;
		}
	    }
	  else
	    {
	      set_default_value = true;
	    }

	  log_warning = true;

	  /* the casted value will be overwriten, so a clear is needed,
	   * here */
	  pr_clear_value (&(value->dbvalue));

	  if (set_max_value)
	    {
	      /* set max value of destination domain */
	      error = db_value_domain_max (&(value->dbvalue),
					   dest_type,
					   dest_prec, dest_dom->scale,
					   dest_dom->codeset,
					   dest_dom->collation_id,
					   &dest_dom->enumeration);
	      if (error != NO_ERROR)
		{
		  /* this should not happen */
		  goto exit;
		}

	      warning_code = ER_ALTER_CHANGE_CAST_FAILED_SET_MAX;
	    }
	  else if (set_min_value)
	    {
	      /* set min value of destination domain */
	      error = db_value_domain_min (&(value->dbvalue),
					   dest_type,
					   dest_prec, dest_dom->scale,
					   dest_dom->codeset,
					   dest_dom->collation_id,
					   &dest_dom->enumeration);
	      if (error != NO_ERROR)
		{
		  /* this should not happen */
		  goto exit;
		}
	      warning_code = ER_ALTER_CHANGE_CAST_FAILED_SET_MIN;
	    }
	  else
	    {
	      assert (set_default_value == true);

	      /* set default value of destination domain */
	      error = db_value_domain_default (&(value->dbvalue),
					       dest_type,
					       dest_prec, dest_dom->scale,
					       dest_dom->codeset,
					       dest_dom->collation_id,
					       &dest_dom->enumeration);
	      if (error != NO_ERROR)
		{
		  /* this should not happen */
		  goto exit;
		}
	      warning_code = ER_ALTER_CHANGE_CAST_FAILED_SET_DEFAULT;
	    }
	}

      if (!DB_IS_NULL (&orig_value))
	{
	  assert (!DB_IS_NULL (&(value->dbvalue)));
	}

      if (log_warning)
	{
	  assert (warning_code != NO_ERROR);
	  if (warning_code == ER_QPROC_SIZE_STRING_TRUNCATED)
	    {
	      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, warning_code, 1,
		      "ALTER TABLE .. CHANGE");
	    }
	  else
	    {
	      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, warning_code, 0);
	    }
	}

      value->state = HEAP_WRITTEN_ATTRVALUE;
      atts_id[updated_n_attrs_id] = value->attrid;
      updated_n_attrs_id++;

      break;
    }

  /* exactly one attribute should be changed */
  assert (updated_n_attrs_id == 1);

  if (updated_n_attrs_id != 1 || attr_info->read_classrepr == NULL ||
      attr_info->last_classrepr == NULL ||
      attr_info->read_classrepr->id >= attr_info->last_classrepr->id)
    {
      error = ER_UNEXPECTED;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      goto exit;
    }

  error =
    locator_attribute_info_force (thread_p, &upd_scancache->hfid, oid, NULL,
				  false, attr_info, atts_id,
				  updated_n_attrs_id, LC_FLUSH_UPDATE,
				  SINGLE_ROW_UPDATE, upd_scancache,
				  &force_count, false,
				  REPL_INFO_TYPE_STMT_NORMAL,
				  DB_NOT_PARTITIONED_CLASS, NULL, NULL);
  if (error != NO_ERROR)
    {
      goto exit;
    }

exit:
  pr_clear_value (&orig_value);
  return error;
}

/*
 * heap_eval_function_index - evaluate the result of the expression used in
 *			      a function index.
 *
 *    thread_p(in) : thread context
 *    func_index_info(in): function index information
 *    n_atts(in): number of attributes involved
 *    att_ids(in): attribute identifiers
 *    attr_info(in): attribute info structure
 *    recdes(in): record descriptor
 *    btid_index(in): id of the function index used
 *    func_pred_cache(in): cached function index expressions
 *    result(out): result of the function expression
 *    return: error code
 */
static int
heap_eval_function_index (THREAD_ENTRY * thread_p,
			  FUNCTION_INDEX_INFO * func_index_info,
			  int n_atts, int *att_ids,
			  HEAP_CACHE_ATTRINFO * attr_info,
			  RECDES * recdes, int btid_index, DB_VALUE * result,
			  FUNC_PRED_UNPACK_INFO * func_pred_cache)
{
  int error = NO_ERROR;
  OR_INDEX *index = NULL;
  char *expr_stream = NULL;
  int expr_stream_size = 0;
  FUNC_PRED *func_pred = NULL;
  void *unpack_info = NULL;
  DB_VALUE *res = NULL;
  int nr_atts;
  ATTR_ID *atts;
  bool atts_free = false, attrinfo_clear = false, attrinfo_end = false;
  HEAP_CACHE_ATTRINFO *cache_attr_info;

  if (func_index_info == NULL && btid_index > -1 && n_atts == -1)
    {
      int i;
      index = &(attr_info->last_classrepr->indexes[btid_index]);
      if (func_pred_cache)
	{
	  func_pred = func_pred_cache->func_pred;
	  cache_attr_info = func_pred->cache_attrinfo;
	}
      else
	{
	  expr_stream = index->func_index_info->expr_stream;
	  expr_stream_size = index->func_index_info->expr_stream_size;
	  nr_atts = index->n_atts;
	  atts = (ATTR_ID *) malloc (nr_atts * sizeof (ATTR_ID));
	  if (atts == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_OUT_OF_VIRTUAL_MEMORY, 1,
		      nr_atts * sizeof (ATTR_ID));
	      error = ER_FAILED;
	      goto end;
	    }
	  atts_free = true;
	  for (i = 0; i < nr_atts; i++)
	    {
	      atts[i] = index->atts[i]->id;
	    }
	  cache_attr_info = attr_info;
	}
    }
  else
    {
      /* load index case */
      expr_stream = func_index_info->expr_stream;
      expr_stream_size = func_index_info->expr_stream_size;
      nr_atts = n_atts;
      atts = att_ids;
      cache_attr_info = ((FUNC_PRED *) func_index_info->expr)->cache_attrinfo;
      func_pred = (FUNC_PRED *) func_index_info->expr;
    }

  if (func_index_info == NULL)
    {
      /* insert case, read the values */
      if (func_pred == NULL)
	{
	  if (stx_map_stream_to_func_pred (NULL, (FUNC_PRED **) & func_pred,
					   expr_stream, expr_stream_size,
					   &unpack_info))
	    {
	      error = ER_FAILED;
	      goto end;
	    }
	  cache_attr_info = func_pred->cache_attrinfo;

	  if (heap_attrinfo_start (thread_p, &attr_info->class_oid, nr_atts,
				   atts, cache_attr_info) != NO_ERROR)
	    {
	      error = ER_FAILED;
	      goto end;
	    }
	  attrinfo_end = true;
	}

      if (heap_attrinfo_read_dbvalues (thread_p, &attr_info->inst_oid,
				       recdes, cache_attr_info) != NO_ERROR)
	{
	  error = ER_FAILED;
	  goto end;
	}
      attrinfo_clear = true;
    }

  error = fetch_peek_dbval (thread_p, func_pred->func_regu, NULL,
			    &cache_attr_info->class_oid,
			    &cache_attr_info->inst_oid, NULL, &res);
  if (error == NO_ERROR)
    {
      pr_clone_value (res, result);
    }

end:
  if (attrinfo_clear)
    {
      heap_attrinfo_clear_dbvalues (cache_attr_info);
    }
  if (attrinfo_end)
    {
      heap_attrinfo_end (thread_p, cache_attr_info);
    }
  if (atts_free)
    {
      free_and_init (atts);
    }
  if (unpack_info)
    {
      (void) qexec_clear_func_pred (thread_p, func_pred);
      stx_free_additional_buff (thread_p, unpack_info);
      stx_free_xasl_unpack_info (unpack_info);
      db_private_free_and_init (thread_p, unpack_info);
    }

  return error;
}

/*
 * heap_init_func_pred_unpack_info () - if function indexes are found,
 *			each function expression is unpacked and cached
 *			in order to be used during bulk inserts
 *			(insert ... select).
 *   return: NO_ERROR, or ER_FAILED
 *   thread_p(in): thread entry
 *   attr_info(in): heap_cache_attrinfo
 *   class_oid(in): the class oid
 *   func_indx_preds(out):
 */
int
heap_init_func_pred_unpack_info (THREAD_ENTRY * thread_p,
				 HEAP_CACHE_ATTRINFO * attr_info,
				 const OID * class_oid,
				 FUNC_PRED_UNPACK_INFO ** func_indx_preds)
{
  OR_FUNCTION_INDEX *fi_info = NULL;
  int n_indexes;
  int i, j;
  int *att_ids = NULL;
  int error_status = NO_ERROR;
  OR_INDEX *idx;
  FUNC_PRED_UNPACK_INFO *fi_preds = NULL;
  int *attr_info_started = NULL;
  size_t size;

  if (attr_info == NULL || class_oid == NULL || func_indx_preds == NULL)
    {
      return ER_FAILED;
    }

  *func_indx_preds = NULL;

  n_indexes = attr_info->last_classrepr->n_indexes;
  for (i = 0; i < n_indexes; i++)
    {
      idx = &(attr_info->last_classrepr->indexes[i]);
      fi_info = idx->func_index_info;
      if (fi_info)
	{
	  if (fi_preds == NULL)
	    {
	      size = n_indexes * sizeof (FUNC_PRED_UNPACK_INFO);
	      fi_preds =
		(FUNC_PRED_UNPACK_INFO *) db_private_alloc (thread_p, size);
	      if (!fi_preds)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_OUT_OF_VIRTUAL_MEMORY, 1, size);
		  error_status = ER_FAILED;
		  goto error;
		}
	      for (j = 0; j < n_indexes; j++)
		{
		  fi_preds[j].func_pred = NULL;
		  fi_preds[j].unpack_info = NULL;
		}

	      size = n_indexes * sizeof (int);
	      attr_info_started = (int *) db_private_alloc (thread_p, size);
	      if (attr_info_started == NULL)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_OUT_OF_VIRTUAL_MEMORY, 1, size);
		  error_status = ER_FAILED;
		  goto error;
		}
	      for (j = 0; j < n_indexes; j++)
		{
		  attr_info_started[j] = 0;
		}
	    }

	  if (stx_map_stream_to_func_pred (thread_p,
					   (FUNC_PRED **) (&(fi_preds[i].
							     func_pred)),
					   fi_info->expr_stream,
					   fi_info->expr_stream_size,
					   &(fi_preds[i].unpack_info)))
	    {
	      error_status = ER_FAILED;
	      return ER_FAILED;
	    }

	  size = idx->n_atts * sizeof (ATTR_ID);
	  att_ids = (ATTR_ID *) db_private_alloc (thread_p, size);
	  if (!att_ids)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_OUT_OF_VIRTUAL_MEMORY, 1, size);
	      error_status = ER_FAILED;
	      goto error;
	    }

	  for (j = 0; j < idx->n_atts; j++)
	    {
	      att_ids[j] = idx->atts[j]->id;
	    }

	  if (heap_attrinfo_start (thread_p, class_oid, idx->n_atts,
				   att_ids,
				   ((FUNC_PRED *) fi_preds[i].func_pred)->
				   cache_attrinfo) != NO_ERROR)
	    {
	      error_status = ER_FAILED;
	      goto error;
	    }

	  attr_info_started[i] = 1;

	  if (att_ids)
	    {
	      db_private_free_and_init (thread_p, att_ids);
	    }
	}
    }

  if (attr_info_started != NULL)
    {
      db_private_free_and_init (thread_p, attr_info_started);
    }

  *func_indx_preds = fi_preds;

  return NO_ERROR;

error:
  if (att_ids)
    {
      db_private_free_and_init (thread_p, att_ids);
    }
  heap_free_func_pred_unpack_info (thread_p, n_indexes,
				   fi_preds, attr_info_started);
  if (attr_info_started != NULL)
    {
      db_private_free_and_init (thread_p, attr_info_started);
    }

  return error_status;
}

/*
 * heap_free_func_pred_unpack_info () -
 *   return:
 *   thread_p(in): thread entry
 *   n_indexes(in): number of indexes
 *   func_indx_preds(in):
 *   attr_info_started(in): array of int (1 if corresponding cache_attrinfo
 *					  must be cleaned, 0 otherwise)
 *			    if null all cache_attrinfo must be cleaned
 */
void
heap_free_func_pred_unpack_info (THREAD_ENTRY * thread_p, int n_indexes,
				 FUNC_PRED_UNPACK_INFO * func_indx_preds,
				 int *attr_info_started)
{
  int i;

  if (func_indx_preds == NULL)
    {
      return;
    }

  for (i = 0; i < n_indexes; i++)
    {
      if (func_indx_preds[i].func_pred)
	{
	  if (attr_info_started == NULL || attr_info_started[i])
	    {
	      assert (((FUNC_PRED *) func_indx_preds[i].func_pred)->
		      cache_attrinfo);
	      (void) heap_attrinfo_end (thread_p,
					((FUNC_PRED *) func_indx_preds[i].
					 func_pred)->cache_attrinfo);
	    }
	  (void) qexec_clear_func_pred (thread_p,
					(FUNC_PRED *) func_indx_preds[i].
					func_pred);
	}

      if (func_indx_preds[i].unpack_info)
	{
	  stx_free_additional_buff (thread_p, func_indx_preds[i].unpack_info);
	  stx_free_xasl_unpack_info (func_indx_preds[i].unpack_info);
	  db_private_free_and_init (thread_p, func_indx_preds[i].unpack_info);
	}
    }
  db_private_free_and_init (thread_p, func_indx_preds);
}
