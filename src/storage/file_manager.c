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
 * file_manager.c - file manager
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <time.h>

#include "porting.h"
#include "file_manager.h"
#include "memory_alloc.h"
#include "storage_common.h"
#include "error_manager.h"
#include "file_io.h"
#include "page_buffer.h"
#include "disk_manager.h"
#include "log_manager.h"
#include "log_impl.h"
#include "lock_manager.h"
#include "system_parameter.h"
#include "boot_sr.h"
#include "memory_hash.h"
#include "environment_variable.h"
#include "xserver_interface.h"
#include "oid.h"
#include "heap_file.h"
#include "bit.h"
#include "util_func.h"

#include "critical_section.h"
#if defined(SERVER_MODE)
#include "connection_error.h"
#endif /* SERVER_MODE */

#if !defined (SERVER_MODE)
#include "transaction_cl.h"
#endif

#include "fault_injection.h"

/************************************************************************/
/* Define structures, globals, and macro's                              */
/************************************************************************/

/************************************************************************/
/* File manager section                                                 */
/************************************************************************/

/************************************************************************/
/* File header section                                                  */
/************************************************************************/

/* FILE_HEADER -
 * This structure keeps meta-data on files in the file header page. The rest of the file header page, based on the type
 * of files, is used by other tables.
 */
typedef struct file_header FILE_HEADER;
struct file_header
{
  INT64 time_creation;		/* Time of file creation. */

  VFID self;			/* Self VFID */
  FILE_TABLESPACE tablespace;	/* The table space definition */
  FILE_DESCRIPTORS descriptor;	/* File descriptor. Depends on file type. */

  /* Page counts. */
  int n_page_total;		/* File total page count. */
  int n_page_user;		/* User page count. */
  int n_page_ftab;		/* Page count used for file tables. */
  int n_page_free;		/* Free page count. Pages that were reserved on disk and can be allocated by user (or
				 * table) in the future. */
  int n_page_mark_delete;	/* used by numerable files to track marked deleted pages */

  /* Sector counts. */
  int n_sector_total;		/* File total sector count. */
  int n_sector_partial;		/* Partially allocated sectors count. */
  int n_sector_full;		/* Fully allocated sectors count. */
  int n_sector_empty;		/* Completely empty sectors count. Empty sectors are also considered partially
				 * allocated (so this is less or equal to n_sector_partial. */

  FILE_TYPE type;		/* File type. */

  INT32 file_flags;		/* File flags. */

  VOLID volid_last_expand;	/* Last volume used for expansion. */

  INT16 offset_to_partial_ftab;	/* Offset to partial sectors table. */
  INT16 offset_to_full_ftab;	/* Offset to full sectors table. */
  INT16 offset_to_user_page_ftab;	/* Offset to user pages table. */

  VPID vpid_sticky_first;	/* VPID of first page (if it is sticky). This page should never be deallocated. */

  /* Temporary files. */
  /* Temporary files are handled differently than permanent files for simplicity. Temporary file pages are never
   * deallocated, they are deallocated when the file is destroyed or reclaimed when file is reset (but kept in cache).
   * Therefore, we can just keep a cursor that tracks the location of last allocated page. This cursor has two
   * components: the last page of partially allocated sectors and the offset to last sector in this page.
   * When the sector becomes full, the cursor is incremented. When all page becomes full, the cursor is moved to next
   * page. */
  VPID vpid_last_temp_alloc;	/* VPID of partial table page last used to allocate a page. */
  int offset_to_last_temp_alloc;	/* Sector offset in partial table last used to allocate a page. */

  /* Numerable files */
  /* Numerable files have an additional property compared to regular files. The order of user page allocation is
   * tracked and the user can get nth page according to this order. To optimize allocations, we keep the VPID of last
   * page of user page table. Newly allocated page is appended here. */
  VPID vpid_last_user_page_ftab;

  /* reserved area for future extension */
  INT32 reserved0;
  INT32 reserved1;
  INT32 reserved2;
  INT32 reserved3;
};

/* Disk size of file header. */
#define FILE_HEADER_ALIGNED_SIZE ((INT16) (DB_ALIGN (sizeof (FILE_HEADER), MAX_ALIGNMENT)))

/* File flags. */
#define FILE_FLAG_NUMERABLE	    0x1	/* Is file numerable */
#define FILE_FLAG_TEMPORARY	    0x2	/* Is file temporary */

#define FILE_IS_NUMERABLE(fh) (((fh)->file_flags & FILE_FLAG_NUMERABLE) != 0)
#define FILE_IS_TEMPORARY(fh) (((fh)->file_flags & FILE_FLAG_TEMPORARY) != 0)

/* Numerable file types. Currently, we used this property for extensible hashes and sort files. */
#define FILE_TYPE_CAN_BE_NUMERABLE(ftype) ((ftype) == FILE_EXTENDIBLE_HASH \
					   || (ftype) == FILE_EXTENDIBLE_HASH_DIRECTORY \
					   || (ftype) == FILE_TEMP)
#define FILE_TYPE_IS_ALWAYS_TEMP(ftype) ((ftype) == FILE_TEMP \
                                         || (ftype) == FILE_QUERY_AREA)
#define FILE_TYPE_IS_SOMETIMES_TEMP(ftype) ((ftype) == FILE_EXTENDIBLE_HASH \
                                            || (ftype) == FILE_EXTENDIBLE_HASH_DIRECTORY)
#define FILE_TYPE_IS_NEVER_TEMP(ftype) (!FILE_TYPE_IS_ALWAYS_TEMP (ftype) && !FILE_TYPE_IS_SOMETIMES_TEMP (ftype))

/* Convert VFID to file header page VPID. */
#define FILE_GET_HEADER_VPID(vfid, vpid) (vpid)->volid = (vfid)->volid; (vpid)->pageid = (vfid)->fileid

/* Get pointer to partial table in file header page */
#define FILE_HEADER_GET_PART_FTAB(fh, parttab) \
  assert ((fh)->offset_to_partial_ftab >= FILE_HEADER_ALIGNED_SIZE && (fh)->offset_to_partial_ftab < DB_PAGESIZE); \
  (parttab) = (FILE_EXTENSIBLE_DATA *) (((char *) fh) + (fh)->offset_to_partial_ftab)
/* Get pointer to full table in file header page */
#define FILE_HEADER_GET_FULL_FTAB(fh, fulltab) \
  assert (!FILE_IS_TEMPORARY (fh)); \
  assert ((fh)->offset_to_full_ftab>= FILE_HEADER_ALIGNED_SIZE && (fh)->offset_to_full_ftab < DB_PAGESIZE); \
  (fulltab) = (FILE_EXTENSIBLE_DATA *) (((char *) fh) + (fh)->offset_to_full_ftab)
/* Get pointer to user page table in file header page */
#define FILE_HEADER_GET_USER_PAGE_FTAB(fh, pagetab) \
  assert (FILE_IS_NUMERABLE (fh)); \
  assert ((fh)->offset_to_user_page_ftab >= FILE_HEADER_ALIGNED_SIZE && (fh)->offset_to_user_page_ftab < DB_PAGESIZE); \
  (pagetab) = (FILE_EXTENSIBLE_DATA *) (((char *) fh) + (fh)->offset_to_user_page_ftab)

/************************************************************************/
/* File extensible data section                                         */
/************************************************************************/

/* File extensible data is a generic format to keep theoretically unlimited data in multiple disk pages. Each page is
 * a component of full data. Each component has a header which keeps a link to next page and useful information about
 * this component. The header is followed by data.
 *
 * The data is considered a set of items. Items have constant size.
 *
 * File extensible data is designed to easily access and manipulate data items. It can be used for multiple purposes
 * (e.g. file tables, file tracker).
 *
 * Items in extensible data can be ordered. The user must provide the right compare function. */

/* FILE_EXTENSIBLE_DATA -
 * This structure is actually the beginning of a extensible data component. It is usually accessed as a pointer in page.
 */
typedef struct file_extensible_data FILE_EXTENSIBLE_DATA;
struct file_extensible_data
{
  VPID vpid_next;
  INT16 max_size;
  INT16 size_of_item;
  INT16 n_items;
};
/* Disk size for extensible data header */
#define FILE_EXTDATA_HEADER_ALIGNED_SIZE (DB_ALIGN (sizeof (FILE_EXTENSIBLE_DATA), MAX_ALIGNMENT))

/* FILE_EXTENSIBLE_DATA_SEARCH_CONTEXT -
 * Helper used for searching an item in extensible data. */
typedef struct file_extensible_data_search_context FILE_EXTENSIBLE_DATA_SEARCH_CONTEXT;
struct file_extensible_data_search_context
{
  const void *item_to_find;	/* Pointer to item to find. */
  int (*compare_func) (const void *, const void *);	/* Compare function used to find the item. */
  bool found;			/* Set to true when item is found. */
  int position;			/* Saves the position of found item in its component */
};

/* Functions for file_extdata_apply_funcs */
typedef int (*FILE_EXTDATA_FUNC) (THREAD_ENTRY * thread_p,
				  const FILE_EXTENSIBLE_DATA * extdata, bool * stop, void *args);
typedef int (*FILE_EXTDATA_ITEM_FUNC) (THREAD_ENTRY * thread_p, const void *data, int index, bool * stop, void *args);

/************************************************************************/
/* Partially allocated sectors section                                  */
/************************************************************************/

/* Partial sector table (full name would be partially allocated sector table, but we will refer to them just by partial
 * sectors) stores reserved sectors and bitmaps for allocated pages in these sectors. Usually, when all sector pages are
 * allocated, the sector is removed from partial sector table. This rule does not apply to temporary files, which for
 * simplicity, only use this partial sector table without moving its content to full table sector.
 * 
 * Each bit in bitmap represents a page, and a page is considered allocated if its bit is set or free if the bit is not
 * set.
 */

/* FILE_ALLOC_BITMAP -
 * Type used to store allocation bitmap for sectors.  */
typedef UINT64 FILE_ALLOC_BITMAP;
#define FILE_FULL_PAGE_BITMAP	    0xFFFFFFFFFFFFFFFF	/* Full allocation bitmap */
#define FILE_EMPTY_PAGE_BITMAP	    0x0000000000000000	/* Empty allocation bitmap */

#define FILE_ALLOC_BITMAP_NBITS ((int) (sizeof (FILE_ALLOC_BITMAP) * CHAR_BIT))

/* FILE_PARTIAL_SECTOR -
 * Structure used by partially allocated sectors table. Store sector VSID and its allocation bitmap. */
typedef struct file_partial_sector FILE_PARTIAL_SECTOR;
struct file_partial_sector
{
  VSID vsid;			/* Important - VSID must be first member of FILE_PARTIAL_SECTOR. Sometimes, the
				 * FILE_PARTIAL_SECTOR pointers in file table are reinterpreted as VSID. */
  FILE_ALLOC_BITMAP page_bitmap;
};
#define FILE_PARTIAL_SECTOR_INITIALIZER { VSID_INITIALIZER, 0 }

/************************************************************************/
/* Utility structures                                                   */
/************************************************************************/

/* Table space default macro's */
#define FILE_TABLESPACE_DEFAULT_RATIO_EXPAND ((float) 0.01)	/* 1% of current size */
#define FILE_TABLESPACE_DEFAULT_MIN_EXPAND (DISK_SECTOR_NPAGES * DB_PAGESIZE);	/* one sector */
#define FILE_TABLESPACE_DEFAULT_MAX_EXPAND (DISK_SECTOR_NPAGES * DB_PAGESIZE * 1024);	/* 1k sectors */

#define FILE_TABLESPACE_FOR_PERM_NPAGES(tabspace, npages) \
  ((FILE_TABLESPACE *) (tabspace))->initial_size = MAX (1, npages) * DB_PAGESIZE; \
  ((FILE_TABLESPACE *) (tabspace))->expand_ratio = FILE_TABLESPACE_DEFAULT_RATIO_EXPAND; \
  ((FILE_TABLESPACE *) (tabspace))->expand_min_size = FILE_TABLESPACE_DEFAULT_MIN_EXPAND; \
  ((FILE_TABLESPACE *) (tabspace))->expand_max_size = FILE_TABLESPACE_DEFAULT_MAX_EXPAND

#define FILE_TABLESPACE_FOR_TEMP_NPAGES(tabspace, npages) \
  ((FILE_TABLESPACE *) (tabspace))->initial_size = MAX (1, npages) * DB_PAGESIZE; \
  ((FILE_TABLESPACE *) (tabspace))->expand_ratio = 0; \
  ((FILE_TABLESPACE *) (tabspace))->expand_min_size = 0; \
  ((FILE_TABLESPACE *) (tabspace))->expand_max_size = 0

/* FILE_VSID_COLLECTOR -
 * Collect sector ID's. File destroy uses it. */
typedef struct file_vsid_collector FILE_VSID_COLLECTOR;
struct file_vsid_collector
{
  VSID *vsids;
  int n_vsids;
};

/* logging */
static bool file_Logging = false;

#define file_log(func, msg, ...) \
  if (file_Logging) \
    _er_log_debug (ARG_FILE_LINE, "FILE " func " (thread=%d tran=%d): " msg "\n", \
                   thread_get_current_entry_index (), LOG_FIND_THREAD_TRAN_INDEX (thread_get_thread_entry_info ()), \
                   __VA_ARGS__)

#define FILE_PERM_TEMP_STRING(is_temp) ((is_temp) ? "temporary" : "permanent")
#define FILE_NUMERABLE_REGULAR_STRING(is_numerable) ((is_numerable) ? "numerable" : "regular")

#define FILE_TABLESPACE_MSG \
  "\ttablespace = { init_size = %d, expand_ratio = %f, expand_min_size = %d, expand_max_size = %d } \n"
#define FILE_TABLESPACE_AS_ARGS(tabspace) \
  (tabspace)->initial_size, (tabspace)->expand_ratio, (tabspace)->expand_min_size, (tabspace)->expand_max_size

#define FILE_HEAD_ALLOC_MSG \
 "\tfile header: \n" \
 "\t\t%s \n" \
 "\t\t%s \n" \
 "\t\tpage: total = %d, user = %d, table = %d, free = %d \n" \
 "\t\tsector: total = %d, partial = %d, full = %d, empty = %d \n"
#define FILE_HEAD_ALLOC_AS_ARGS(fhead) \
  FILE_PERM_TEMP_STRING (FILE_IS_TEMPORARY (fhead)), \
  FILE_NUMERABLE_REGULAR_STRING (FILE_IS_NUMERABLE (fhead)), \
  (fhead)->n_page_total, (fhead)->n_page_user, (fhead)->n_page_ftab, (fhead)->n_page_free, \
  (fhead)->n_sector_total, (fhead)->n_sector_partial, (fhead)->n_sector_full, (fhead)->n_sector_empty

#define FILE_HEAD_FULL_MSG \
  FILE_HEAD_ALLOC_MSG \
  "\t\tvfid = %d|%d, time_creation = %lld, type = %s \n" \
  "\t" FILE_TABLESPACE_MSG \
  "\t\ttable offsets: partial = %d, full = %d, user page = %d \n" \
  "\t\tvpid_sticky_first = %d|%d \n" \
  "\t\tvpid_last_temp_alloc = %d|%d, offset_to_last_temp_alloc=%d \n" \
  "\t\tvpid_last_user_page_ftab = %d|%d \n"
#define FILE_HEAD_FULL_AS_ARGS(fhead) \
  FILE_HEAD_ALLOC_AS_ARGS (fhead), \
  VFID_AS_ARGS (&(fhead)->self), (long long int) fhead->time_creation, file_type_to_string ((fhead)->type), \
  FILE_TABLESPACE_AS_ARGS (&(fhead)->tablespace), \
  (fhead)->offset_to_partial_ftab, (fhead)->offset_to_full_ftab, (fhead)->offset_to_user_page_ftab, \
  VPID_AS_ARGS (&(fhead)->vpid_sticky_first), \
  VPID_AS_ARGS (&(fhead)->vpid_last_temp_alloc), (fhead)->offset_to_last_temp_alloc, \
  VPID_AS_ARGS (&(fhead)->vpid_last_user_page_ftab)

#define FILE_EXTDATA_MSG(name) \
  "\t" name ": { vpid_next = %d|%d, max_size = %d, item_size = %d, n_items = %d } \n"
#define FILE_EXTDATA_AS_ARGS(extdata) \
  VPID_AS_ARGS (&(extdata)->vpid_next), (extdata)->max_size, (extdata)->size_of_item, (extdata)->n_items

#define FILE_PARTSECT_MSG(name) \
  "\t" name ": { vsid = %d|%d, page bitmap = " BIT64_HEXA_PRINT_FORMAT " } \n"
#define FILE_PARTSECT_AS_ARGS(ps) VSID_AS_ARGS (&(ps)->vsid), (long long unsigned int) (ps)->page_bitmap

#define FILE_ALLOC_TYPE_STRING(alloc_type) \
  ((alloc_type) == FILE_ALLOC_USER_PAGE ? "alloc user page" : "alloc table page")

#define FILE_TEMPCACHE_MSG \
  "\ttempcache: \n" \
  "\t\tfile cache: max = %d, numerable = %d, regular = %d, total = %d \n" \
  "\t\tfree entries: max = %d, count = %d \n"
#define FILE_TEMPCACHE_AS_ARGS \
  file_Tempcache->ncached_max, file_Tempcache->ncached_numerable, file_Tempcache->cached_not_numerable, \
  file_Tempcache->ncached_numerable + file_Tempcache->cached_not_numerable, \
  file_Tempcache->nfree_entries_max, file_Tempcache->nfree_entries

#define FILE_TEMPCACHE_ENTRY_MSG "%p, VFID %d|%d, %s"
#define FILE_TEMPCACHE_ENTRY_AS_ARGS(ent) ent, VFID_AS_ARGS (&(ent)->vfid), file_type_to_string ((ent)->ftype)

#define FILE_TRACK_ITEM_MSG "VFID %d|%d, %s"
#define FILE_TRACK_ITEM_AS_ARGS(item) (item)->volid, (item)->fileid, file_type_to_string ((FILE_TYPE) (item)->type)

/************************************************************************/
/* File manipulation section                                            */
/************************************************************************/

/* FILE_ALLOC_TYPE -
 * The internal workings of file manager may need disk pages to hold the header and data tables. these are considered
 * file table pages and must also be tracked besides user pages (pages allocated using file_alloc) */
typedef enum
{
  FILE_ALLOC_USER_PAGE,
  FILE_ALLOC_TABLE_PAGE,
} FILE_ALLOC_TYPE;

#define FILE_RV_DEALLOC_COMPENSATE true
#define FILE_RV_DEALLOC_RUN_POSTPONE false

typedef struct file_ftab_collector FILE_FTAB_COLLECTOR;
struct file_ftab_collector
{
  int npages;
  int nsects;
  FILE_PARTIAL_SECTOR *partsect_ftab;
};
#define FILE_FTAB_COLLECTOR_INITIALIZER { 0, 0, NULL }

/* FILE_MAP_CONTEXT - context variables for file_map_pages function. */
typedef struct file_map_context FILE_MAP_CONTEXT;
struct file_map_context
{
  bool is_partial;
  PGBUF_LATCH_MODE latch_mode;
  PGBUF_LATCH_CONDITION latch_cond;
  FILE_FTAB_COLLECTOR ftab_collector;

  bool stop;

  FILE_MAP_PAGE_FUNC func;
  void *args;
};

/************************************************************************/
/* Numerable files section                                              */
/************************************************************************/

#define FILE_USER_PAGE_MARK_DELETE_FLAG ((PAGEID) 0x80000000)
#define FILE_USER_PAGE_IS_MARKED_DELETED(vpid) ((((VPID *) vpid)->pageid & FILE_USER_PAGE_MARK_DELETE_FLAG) != 0)
#define FILE_USER_PAGE_MARK_DELETED(vpid) ((VPID *) vpid)->pageid |= FILE_USER_PAGE_MARK_DELETE_FLAG
#define FILE_USER_PAGE_CLEAR_MARK_DELETED(vpid) ((VPID *) vpid)->pageid &= ~FILE_USER_PAGE_MARK_DELETE_FLAG

/* FILE_FIND_NTH_CONTEXT -
 * structure used for finding nth page. */
typedef struct file_find_nth_context FILE_FIND_NTH_CONTEXT;
struct file_find_nth_context
{
  VPID *vpid_nth;
  int nth;
};

/************************************************************************/
/* Temporary files section                                              */
/************************************************************************/

/************************************************************************/
/* Temporary cache section                                              */
/************************************************************************/
typedef struct file_tempcache_entry FILE_TEMPCACHE_ENTRY;
struct file_tempcache_entry
{
  VFID vfid;
  FILE_TYPE ftype;

  FILE_TEMPCACHE_ENTRY *next;
};

typedef struct file_tempcache FILE_TEMPCACHE;
struct file_tempcache
{
  FILE_TEMPCACHE_ENTRY *free_entries;	/* preallocated entries free to be use */
  int nfree_entries_max;
  int nfree_entries;

  FILE_TEMPCACHE_ENTRY *cached_not_numerable;	/* cached temporary files */
  FILE_TEMPCACHE_ENTRY *cached_numerable;	/* cached temporary numerable files */
  int ncached_max;
  int ncached_not_numerable;
  int ncached_numerable;

  pthread_mutex_t mutex;
#if !defined (NDEBUG)
  int owner_mutex;
#endif				/* !NDEBUG */

  FILE_TEMPCACHE_ENTRY **tran_files;	/* transaction temporary files */
};

static FILE_TEMPCACHE *file_Tempcache = NULL;

/************************************************************************/
/* File tracker section                                                 */
/************************************************************************/

static VFID file_Tracker_vfid = VFID_INITIALIZER;
static VPID file_Tracker_vpid = VPID_INITIALIZER;

typedef struct file_track_heap_metadata FILE_TRACK_HEAP_METADATA;
struct file_track_heap_metadata
{
  bool is_marked_deleted;
  bool dummy[7];		/* dummy fields to 8 bytes */
};

typedef union file_track_metadata FILE_TRACK_METADATA;
union file_track_metadata
{
  FILE_TRACK_HEAP_METADATA heap;

  INT64 metadata_size_tracker;
};

typedef struct file_track_item FILE_TRACK_ITEM;
struct file_track_item
{
  INT32 fileid;			/* 4 bytes */
  INT16 volid;			/* 2 bytes */
  INT16 type;			/* 2 bytes */
  FILE_TRACK_METADATA metadata;	/* 8 bytes */

  /* total 16 bytes */
};

typedef struct file_tracker_dump_heap_context FILE_TRACKER_DUMP_HEAP_CONTEXT;
struct file_tracker_dump_heap_context
{
  FILE *fp;
  bool dump_records;
};

typedef struct file_track_mark_heap_deleted_context FILE_TRACK_MARK_HEAP_DELETED_CONTEXT;
struct file_track_mark_heap_deleted_context
{
  LOG_LSA ref_lsa;
  bool is_undo;
};

typedef int (*FILE_TRACK_ITEM_FUNC) (THREAD_ENTRY * thread_p, PAGE_PTR page_of_item, FILE_EXTENSIBLE_DATA * extdata,
				     int index_item, bool * stop, void *args);

/************************************************************************/
/* End of structures, globals and macro's                               */
/************************************************************************/

/************************************************************************/
/* Declare static functions.                                            */
/************************************************************************/

/************************************************************************/
/* File manager section                                                 */
/************************************************************************/

/************************************************************************/
/* File header section.                                                 */
/************************************************************************/

STATIC_INLINE void file_header_init (FILE_HEADER * fhead) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void file_header_sanity_check (FILE_HEADER * fhead) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void file_header_alloc (FILE_HEADER * fhead, FILE_ALLOC_TYPE alloc_type,
				      bool was_empty, bool is_full) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void file_header_dealloc (FILE_HEADER * fhead, FILE_ALLOC_TYPE alloc_type,
					bool is_empty, bool was_full) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void file_log_fhead_alloc (THREAD_ENTRY * thread_p, PAGE_PTR page_fhead, FILE_ALLOC_TYPE alloc_type,
					 bool was_empty, bool is_full) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void file_log_fhead_dealloc (THREAD_ENTRY * thread_p, PAGE_PTR page_fhead, FILE_ALLOC_TYPE alloc_type,
					   bool is_empty, bool was_full) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void file_header_update_mark_deleted (THREAD_ENTRY * thread_p,
						    PAGE_PTR page_fhead, int delta) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int file_header_copy (THREAD_ENTRY * thread_p,
				    const VFID * vfid, FILE_HEADER * fhead_copy) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void file_header_dump (THREAD_ENTRY * thread_p, const FILE_HEADER * fhead, FILE * fp)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void file_header_dump_descriptor (THREAD_ENTRY * thread_p, const FILE_HEADER * fhead, FILE * fp)
  __attribute__ ((ALWAYS_INLINE));

/************************************************************************/
/* File extensible data section                                         */
/************************************************************************/

STATIC_INLINE void file_extdata_init (INT16 item_size, INT16 max_size,
				      FILE_EXTENSIBLE_DATA * extdata) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int file_extdata_max_size (const FILE_EXTENSIBLE_DATA * extdata) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int file_extdata_size (const FILE_EXTENSIBLE_DATA * extdata) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void *file_extdata_start (const FILE_EXTENSIBLE_DATA * extdata) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void *file_extdata_end (const FILE_EXTENSIBLE_DATA * extdata) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE bool file_extdata_is_full (const FILE_EXTENSIBLE_DATA * extdata) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE bool file_extdata_is_empty (const FILE_EXTENSIBLE_DATA * extdata) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE INT16 file_extdata_item_count (const FILE_EXTENSIBLE_DATA * extdata) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE INT16 file_extdata_remaining_capacity (const
						     FILE_EXTENSIBLE_DATA * extdata) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void file_extdata_append (FILE_EXTENSIBLE_DATA * extdata,
					const void *append_data) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void file_extdata_append_array (FILE_EXTENSIBLE_DATA * extdata,
					      const void *append_data, INT16 count) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void *file_extdata_at (const FILE_EXTENSIBLE_DATA * extdata, int index) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE bool file_extdata_can_merge (const FILE_EXTENSIBLE_DATA * extdata_src,
					   const FILE_EXTENSIBLE_DATA * extdata_dest) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void file_extdata_merge_unordered (const FILE_EXTENSIBLE_DATA * extdata_src,
						 FILE_EXTENSIBLE_DATA * extdata_dest) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void file_extdata_merge_ordered (const FILE_EXTENSIBLE_DATA * extdata_src,
					       FILE_EXTENSIBLE_DATA * extdata_dest,
					       int (*compare_func) (const void *, const void *))
  __attribute__ ((ALWAYS_INLINE));
static void file_extdata_find_ordered (const FILE_EXTENSIBLE_DATA * extdata, const void *item_to_find,
				       int (*compare_func) (const void *, const void *), bool * found, int *position);
STATIC_INLINE void file_extdata_insert_at (FILE_EXTENSIBLE_DATA * extdata,
					   int position, int count, const void *data) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void file_extdata_remove_at (FILE_EXTENSIBLE_DATA * extdata,
					   int position, int count) __attribute__ ((ALWAYS_INLINE));
static int file_extdata_apply_funcs (THREAD_ENTRY * thread_p, FILE_EXTENSIBLE_DATA * extdata_in,
				     FILE_EXTDATA_FUNC f_extdata, void *f_extdata_args, FILE_EXTDATA_ITEM_FUNC f_item,
				     void *f_item_args, bool for_write, FILE_EXTENSIBLE_DATA ** extdata_out,
				     PAGE_PTR * page_out);
static int file_extdata_item_func_for_search (THREAD_ENTRY * thread_p,
					      const void *item, int index, bool * stop, void *args);
static int file_extdata_func_for_search_ordered (THREAD_ENTRY * thread_p,
						 const FILE_EXTENSIBLE_DATA * extdata, bool * stop, void *args);
static int file_extdata_search_item (THREAD_ENTRY * thread_p, FILE_EXTENSIBLE_DATA ** extdata,
				     const void *item_to_find,
				     int (*compare_func) (const void *, const void *),
				     bool is_ordered, bool for_write, bool * found, int *position,
				     PAGE_PTR * page_extdata);
static int file_extdata_find_not_full (THREAD_ENTRY * thread_p,
				       FILE_EXTENSIBLE_DATA ** extdata, PAGE_PTR * page_out, bool * found);
STATIC_INLINE void file_log_extdata_add (THREAD_ENTRY * thread_p,
					 const FILE_EXTENSIBLE_DATA * extdata, PAGE_PTR page, int position,
					 int count, const void *data) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void file_log_extdata_remove (THREAD_ENTRY * thread_p,
					    const FILE_EXTENSIBLE_DATA * extdata, PAGE_PTR page,
					    int position, int count) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void file_log_extdata_set_next (THREAD_ENTRY * thread_p,
					      const FILE_EXTENSIBLE_DATA * extdata, PAGE_PTR page,
					      VPID * vpid_next) __attribute__ ((ALWAYS_INLINE));
static int file_rv_extdata_merge_redo_internal (THREAD_ENTRY * thread_p,
						LOG_RCV * rcv, int (*compare_func) (const void *, const void *));
STATIC_INLINE void file_log_extdata_merge (THREAD_ENTRY * thread_p, const FILE_EXTENSIBLE_DATA * extdata_dest,
					   PAGE_PTR page_dest, const FILE_EXTENSIBLE_DATA * extdata_src,
					   LOG_RCVINDEX rcvindex) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void file_extdata_update_item (THREAD_ENTRY * thread_p, PAGE_PTR page_extdata, const void *item_newval,
					     int index_item, FILE_EXTENSIBLE_DATA * extdata)
  __attribute__ ((ALWAYS_INLINE));

/************************************************************************/
/* Partially allocated sectors section                                  */
/************************************************************************/

STATIC_INLINE bool file_partsect_is_full (FILE_PARTIAL_SECTOR * partsect) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE bool file_partsect_is_empty (FILE_PARTIAL_SECTOR * partsect) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE bool file_partsect_is_bit_set (FILE_PARTIAL_SECTOR * partsect,
					     int offset) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void file_partsect_set_bit (FILE_PARTIAL_SECTOR * partsect, int offset) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void file_partsect_clear_bit (FILE_PARTIAL_SECTOR * partsect, int offset) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int file_partsect_pageid_to_offset (FILE_PARTIAL_SECTOR * partsect,
						  PAGEID pageid) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE bool file_partsect_alloc (FILE_PARTIAL_SECTOR * partsect,
					VPID * vpid_out, int *offset_out) __attribute__ ((ALWAYS_INLINE));

static int file_rv_partsect_update (THREAD_ENTRY * thread_p, LOG_RCV * rcv, bool set);

/************************************************************************/
/* Utility functions.                                                   */
/************************************************************************/

static int file_compare_vsids (const void *a, const void *b);
static int file_compare_vpids (const void *first, const void *second);
static int file_compare_vfids (const void *first, const void *second);
static int file_compare_track_items (const void *first, const void *second);

static const char *file_type_to_string (FILE_TYPE fstruct_type);
static void file_print_name_of_class (THREAD_ENTRY * thread_p, FILE * fp, const OID * class_oid_p);

/************************************************************************/
/* File manipulation section                                            */
/************************************************************************/

static int file_table_collect_vsid (THREAD_ENTRY * thread_p, const void *item,
				    int index_unused, bool * stop, void *args);
STATIC_INLINE int file_table_collect_all_vsids (THREAD_ENTRY * thread_p, PAGE_PTR page_fhead,
						FILE_VSID_COLLECTOR * collector_out) __attribute__ ((ALWAYS_INLINE));
static int file_perm_expand (THREAD_ENTRY * thread_p, PAGE_PTR page_fhead);
static int file_table_move_partial_sectors_to_header (THREAD_ENTRY * thread_p, PAGE_PTR page_fhead);
static int file_table_add_full_sector (THREAD_ENTRY * thread_p, PAGE_PTR page_fhead, const VSID * vsid);
STATIC_INLINE int file_table_collect_ftab_pages (THREAD_ENTRY * thread_p, PAGE_PTR page_fhead, bool collect_numerable,
						 FILE_FTAB_COLLECTOR * collector_out) __attribute__ ((ALWAYS_INLINE));
static int file_extdata_collect_ftab_pages (THREAD_ENTRY * thread_p, const FILE_EXTENSIBLE_DATA * extdata, bool * stop,
					    void *args);
STATIC_INLINE bool file_table_collector_has_page (FILE_FTAB_COLLECTOR * collector, VPID * vpid)
  __attribute__ ((ALWAYS_INLINE));
static int file_perm_alloc (THREAD_ENTRY * thread_p, PAGE_PTR page_fhead, FILE_ALLOC_TYPE alloc_type,
			    VPID * vpid_alloc_out);
static int file_perm_dealloc (THREAD_ENTRY * thread_p, PAGE_PTR page_fhead, const VPID * vpid_dealloc,
			      FILE_ALLOC_TYPE alloc_type);
STATIC_INLINE int file_extdata_try_merge (THREAD_ENTRY * thread_p, PAGE_PTR page_dest,
					  FILE_EXTENSIBLE_DATA * extdata_dest,
					  int (*compare_func) (const void *, const void *),
					  LOG_RCVINDEX rcvindex, VPID * merged_vpid_out)
  __attribute__ ((ALWAYS_INLINE));
static int file_rv_dealloc_internal (THREAD_ENTRY * thread_p, LOG_RCV * rcv, bool compensate_or_run_postpone);

STATIC_INLINE int file_create_temp_internal (THREAD_ENTRY * thread_p, int npages, FILE_TYPE ftype, bool is_numerable,
					     VFID * vfid_out) __attribute__ ((ALWAYS_INLINE));
static int file_sector_map_pages (THREAD_ENTRY * thread_p, const void *data, int index, bool * stop, void *args);
static DISK_ISVALID file_table_check (THREAD_ENTRY * thread_p, const VFID * vfid, DISK_VOLMAP_CLONE * disk_map_clone);

STATIC_INLINE int file_table_dump (THREAD_ENTRY * thread_p, const FILE_HEADER * fhead, FILE * fp)
  __attribute__ ((ALWAYS_INLINE));
static int file_partial_table_extdata_dump (THREAD_ENTRY * thread_p, const FILE_EXTENSIBLE_DATA * extdata, bool * stop,
					    void *args);
static int file_partial_table_item_dump (THREAD_ENTRY * thread_p, const void *data, int index, bool * stop, void *args);
static int file_full_table_extdata_dump (THREAD_ENTRY * thread_p, const FILE_EXTENSIBLE_DATA * extdata, bool * stop,
					 void *args);
static int file_full_table_item_dump (THREAD_ENTRY * thread_p, const void *data, int index, bool * stop, void *args);
static int file_user_page_table_extdata_dump (THREAD_ENTRY * thread_p, const FILE_EXTENSIBLE_DATA * extdata,
					      bool * stop, void *args);
static int file_user_page_table_item_dump (THREAD_ENTRY * thread_p, const void *data, int index, bool * stop,
					   void *args);
static int file_sector_map_dealloc (THREAD_ENTRY * thread_p, const void *data, int index, bool * stop, void *args);

/************************************************************************/
/* Numerable files section.                                             */
/************************************************************************/

static int file_numerable_add_page (THREAD_ENTRY * thread_p, PAGE_PTR page_fhead, const VPID * vpid);
static int file_extdata_find_nth_vpid (THREAD_ENTRY * thread_p,
				       const FILE_EXTENSIBLE_DATA * extdata, bool * stop, void *args);
static int file_extdata_find_nth_vpid_and_skip_marked (THREAD_ENTRY * thread_p, const void *data, int index,
						       bool * stop, void *args);
static int file_table_check_page_is_in_sectors (THREAD_ENTRY * thread_p, const void *data, int index, bool * stop,
						void *args);

/************************************************************************/
/* Temporary files section                                              */
/************************************************************************/

static int file_temp_alloc (THREAD_ENTRY * thread_p, PAGE_PTR page_fhead, FILE_ALLOC_TYPE alloc_type,
			    VPID * vpid_alloc_out);
STATIC_INLINE int file_temp_set_type (THREAD_ENTRY * thread_p, VFID * vfid,
				      FILE_TYPE ftype) __attribute__ ((ALWAYS_INLINE));
static int file_temp_reset_user_pages (THREAD_ENTRY * thread_p, const VFID * vfid);

/************************************************************************/
/* Temporary cache section                                              */
/************************************************************************/

static int file_tempcache_init (void);
static void file_tempcache_final (void);
STATIC_INLINE void file_tempcache_lock (void) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void file_tempcache_unlock (void) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void file_tempcache_check_lock (void) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void file_tempcache_free_entry_list (FILE_TEMPCACHE_ENTRY ** list) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int file_tempcache_alloc_entry (FILE_TEMPCACHE_ENTRY ** entry) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void file_tempcache_retire_entry (FILE_TEMPCACHE_ENTRY * entry) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int file_tempcache_get (THREAD_ENTRY * thread_p, FILE_TYPE ftype, bool numerable,
				      FILE_TEMPCACHE_ENTRY ** entry) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE bool file_tempcache_put (THREAD_ENTRY * thread_p,
				       FILE_TEMPCACHE_ENTRY * entry) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int file_get_tempcache_entry_index (THREAD_ENTRY * thread_p) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void file_tempcache_cache_or_drop_entries (THREAD_ENTRY * thread_p,
							 FILE_TEMPCACHE_ENTRY ** entries)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE FILE_TEMPCACHE_ENTRY *file_tempcache_pop_tran_file (THREAD_ENTRY * thread_p, const VFID * vfid)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void file_tempcache_push_tran_file (THREAD_ENTRY * thread_p, FILE_TEMPCACHE_ENTRY * entry)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void file_tempcache_dump (FILE * fp) __attribute__ ((ALWAYS_INLINE));

/************************************************************************/
/* File tracker section                                                 */
/************************************************************************/

static int file_tracker_register (THREAD_ENTRY * thread_p, const VFID * vfid, FILE_TYPE ftype,
				  FILE_TRACK_METADATA * metadata);
static int file_tracker_unregister (THREAD_ENTRY * thread_p, const VFID * vfid);
static int file_tracker_apply_to_file (THREAD_ENTRY * thread_p, const VFID * vfid, PGBUF_LATCH_MODE mode,
				       FILE_TRACK_ITEM_FUNC func, void *args);
static int file_tracker_map (THREAD_ENTRY * thread_p, PGBUF_LATCH_MODE latch_mode, FILE_TRACK_ITEM_FUNC func,
			     void *args);
static int file_tracker_item_reuse_heap (THREAD_ENTRY * thread_p, PAGE_PTR page_of_item, FILE_EXTENSIBLE_DATA * extdata,
					 int index_item, bool * stop, void *args);
static int file_tracker_item_mark_heap_deleted (THREAD_ENTRY * thread_p, PAGE_PTR page_of_item,
						FILE_EXTENSIBLE_DATA * extdata, int index_item, bool * stop,
						void *ignore_args);
STATIC_INLINE int file_tracker_get_and_protect (THREAD_ENTRY * thread_p, FILE_TYPE desired_type, FILE_TRACK_ITEM * item,
						OID * class_oid, bool * stop) __attribute__ ((ALWAYS_INLINE));
static int file_tracker_item_dump (THREAD_ENTRY * thread_p, PAGE_PTR page_of_item, FILE_EXTENSIBLE_DATA * extdata,
				   int index_item, bool * stop, void *args);
static int file_tracker_item_dump_capacity (THREAD_ENTRY * thread_p, PAGE_PTR page_of_item,
					    FILE_EXTENSIBLE_DATA * extdata, int index_item, bool * stop, void *args);
static int file_tracker_item_dump_heap (THREAD_ENTRY * thread_p, PAGE_PTR page_of_item, FILE_EXTENSIBLE_DATA * extdata,
					int index_item, bool * stop, void *args);
static int file_tracker_item_dump_heap_capacity (THREAD_ENTRY * thread_p, PAGE_PTR page_of_item,
						 FILE_EXTENSIBLE_DATA * extdata, int index_item, bool * stop,
						 void *args);
static int file_tracker_item_dump_btree_capacity (THREAD_ENTRY * thread_p, PAGE_PTR page_of_item,
						  FILE_EXTENSIBLE_DATA * extdata, int index_item, bool * stop,
						  void *args);
#if defined (SA_MODE)
static int file_tracker_item_check (THREAD_ENTRY * thread_p, PAGE_PTR page_of_item, FILE_EXTENSIBLE_DATA * extdata,
				    int index_item, bool * stop, void *args);
#endif /* SA_MODE */

/************************************************************************/
/* End of static functions                                              */
/************************************************************************/

/************************************************************************/
/* Define functions.                                                    */
/************************************************************************/

/************************************************************************/
/* File manager section                                                 */
/************************************************************************/

/*
 * file_manager_init () - initialize file manager
 */
int
file_manager_init (void)
{
  file_Logging = prm_get_bool_value (PRM_ID_FILE_LOGGING);

  return file_tempcache_init ();
}

/*
 * file_manager_final () - finalize file manager
 */
void
file_manager_final (void)
{
  file_tempcache_final ();
}

/************************************************************************/
/* File header section.                                                 */
/************************************************************************/

/*
 * file_header_init () - initialize file header
 *
 * return      : void
 * fhead (out) : output initialized file header
 */
STATIC_INLINE void
file_header_init (FILE_HEADER * fhead)
{
  VFID_SET_NULL (&fhead->self);

  fhead->tablespace.initial_size = 0;
  fhead->tablespace.expand_ratio = 0;
  fhead->tablespace.expand_min_size = 0;
  fhead->tablespace.expand_max_size = 0;

  fhead->time_creation = 0;
  fhead->type = FILE_UNKNOWN_TYPE;
  fhead->file_flags = 0;
  fhead->volid_last_expand = NULL_VOLDES;
  VPID_SET_NULL (&fhead->vpid_last_temp_alloc);
  fhead->offset_to_last_temp_alloc = NULL_OFFSET;
  VPID_SET_NULL (&fhead->vpid_last_user_page_ftab);
  VPID_SET_NULL (&fhead->vpid_sticky_first);

  fhead->n_page_total = 0;
  fhead->n_page_user = 0;
  fhead->n_page_ftab = 0;
  fhead->n_page_mark_delete = 0;

  fhead->n_sector_total = 0;
  fhead->n_sector_partial = 0;
  fhead->n_sector_full = 0;
  fhead->n_sector_empty = 0;

  fhead->offset_to_partial_ftab = NULL_OFFSET;
  fhead->offset_to_full_ftab = NULL_OFFSET;
  fhead->offset_to_user_page_ftab = NULL_OFFSET;

  fhead->reserved0 = fhead->reserved1 = fhead->reserved2 = fhead->reserved3 = 0;
}

/*
 * file_header_sanity_check () - Debug function used to check the sanity of file header before and after certaion file
 *				 file operations.
 *
 * return     : Void.
 * fhead (in) : File header.
 */
STATIC_INLINE void
file_header_sanity_check (FILE_HEADER * fhead)
{
#if !defined (NDEBUG)
  FILE_EXTENSIBLE_DATA *part_table;
  FILE_EXTENSIBLE_DATA *full_table;

  if (prm_get_bool_value (PRM_ID_FORCE_RESTART_TO_SKIP_RECOVERY))
    {
      /* we cannot guarantee sanity of files headers */
      return;
    }

  assert (!VFID_ISNULL (&fhead->self));

  assert (fhead->n_page_total > 0);
  assert (fhead->n_page_user >= 0);
  assert (fhead->n_page_ftab > 0);
  assert (fhead->n_page_free >= 0);
  assert (fhead->n_page_free + fhead->n_page_user + fhead->n_page_ftab == fhead->n_page_total);
  assert (fhead->n_page_mark_delete >= 0);
  assert (fhead->n_page_mark_delete <= fhead->n_page_user);

  assert (fhead->n_sector_total > 0);
  assert (fhead->n_sector_partial >= 0);
  assert (fhead->n_sector_empty >= 0);
  assert (fhead->n_sector_full >= 0);
  assert (fhead->n_sector_empty <= fhead->n_sector_partial);
  assert (fhead->n_sector_partial + fhead->n_sector_full == fhead->n_sector_total);

  if (fhead->n_page_free == 0)
    {
      assert (fhead->n_sector_total == fhead->n_sector_full);
    }

  FILE_HEADER_GET_PART_FTAB (fhead, part_table);
  if (fhead->n_sector_partial == 0)
    {
      assert (FILE_IS_TEMPORARY (fhead)
	      || (file_extdata_is_empty (part_table) && VPID_ISNULL (&part_table->vpid_next)));
    }
  else
    {
      assert (!file_extdata_is_empty (part_table) || !VPID_ISNULL (&part_table->vpid_next));
    }

  if (!FILE_IS_TEMPORARY (fhead))
    {
      FILE_HEADER_GET_FULL_FTAB (fhead, full_table);
      if (fhead->n_sector_full == 0)
	{
	  assert (file_extdata_is_empty (full_table) && VPID_ISNULL (&full_table->vpid_next));
	}
      else
	{
	  assert (!file_extdata_is_empty (full_table) || !VPID_ISNULL (&full_table->vpid_next));
	}
    }
#endif /* !NDEBUG */
}

/*
 * file_rv_fhead_set_last_user_page_ftab () - Recovery of file header: set the VPID of last page in user page table.
 *
 * return	 : Error code
 * thread_p (in) : Thread entry
 * rcv (in)	 : Recovery data
 */
int
file_rv_fhead_set_last_user_page_ftab (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  PAGE_PTR page_fhead = rcv->pgptr;
  VPID *vpid = (VPID *) rcv->data;
  FILE_HEADER *fhead;

  assert (rcv->pgptr != NULL);
  assert (rcv->length == sizeof (VPID));

  fhead = (FILE_HEADER *) page_fhead;
  file_header_sanity_check (fhead);

  /* the correct VPID is logged. */

  VPID_COPY (&fhead->vpid_last_user_page_ftab, vpid);
  file_header_sanity_check (fhead);

  file_log ("file_rv_fhead_set_last_user_page_ftab",
	    "update vpid_last_user_page_ftab to %d|%d in file %d|%d, "
	    "header page %d|%d, lsa %lld|%d ", VPID_AS_ARGS (vpid),
	    VFID_AS_ARGS (&fhead->self), PGBUF_PAGE_VPID_AS_ARGS (page_fhead), PGBUF_PAGE_LSA_AS_ARGS (page_fhead));

  pgbuf_set_dirty (thread_p, page_fhead, DONT_FREE);
  return NO_ERROR;
}

/*
 * file_header_alloc () - Update stats in file header for page allocation.
 *
 * return	   : Void
 * fhead (in)      : File header
 * alloc_type (in) : User/table page
 * was_empty (in)  : True if sector was empty before allocation.
 * is_full (in)    : True if sector is full after allocation.
 */
STATIC_INLINE void
file_header_alloc (FILE_HEADER * fhead, FILE_ALLOC_TYPE alloc_type, bool was_empty, bool is_full)
{
  assert (fhead != NULL);
  assert (alloc_type == FILE_ALLOC_USER_PAGE || alloc_type == FILE_ALLOC_TABLE_PAGE);
  assert (!was_empty || !is_full);

  fhead->n_page_free--;
  if (alloc_type == FILE_ALLOC_USER_PAGE)
    {
      fhead->n_page_user++;
    }
  else
    {
      fhead->n_page_ftab++;
    }

  if (was_empty)
    {
      fhead->n_sector_empty--;
    }

  if (is_full)
    {
      fhead->n_sector_partial--;
      fhead->n_sector_full++;
    }
}

/*
 * file_header_dealloc () - Update stats in file header for page deallocation.
 *
 * return	   : Void
 * fhead (in)      : File header
 * alloc_type (in) : User/table page
 * is_empty (in)   : True if sector is empty after deallocation.
 * was_full (in)   : True if sector was full before deallocation.
 */
STATIC_INLINE void
file_header_dealloc (FILE_HEADER * fhead, FILE_ALLOC_TYPE alloc_type, bool is_empty, bool was_full)
{
  assert (fhead != NULL);
  assert (alloc_type == FILE_ALLOC_USER_PAGE || alloc_type == FILE_ALLOC_TABLE_PAGE);
  assert (!is_empty || !was_full);

  fhead->n_page_free++;
  if (alloc_type == FILE_ALLOC_USER_PAGE)
    {
      fhead->n_page_user--;
    }
  else
    {
      fhead->n_page_ftab--;
    }

  if (is_empty)
    {
      fhead->n_sector_empty++;
    }

  if (was_full)
    {
      fhead->n_sector_partial++;
      fhead->n_sector_full--;
    }
}

/*
 * file_rv_fhead_alloc () - Recovery for file header when a page is allocated.
 *
 * return	 : Error code
 * thread_p (in) : Thread entry
 * rcv (in)	 : Recovery data
 */
int
file_rv_fhead_alloc (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  PAGE_PTR page_fhead = rcv->pgptr;
  FILE_HEADER *fhead;
  bool is_ftab_page;
  bool is_full;
  bool was_empty;

  assert (rcv->length == sizeof (bool) * 3);

  is_ftab_page = ((bool *) rcv->data)[0];
  was_empty = ((bool *) rcv->data)[1];
  is_full = ((bool *) rcv->data)[2];

  fhead = (FILE_HEADER *) page_fhead;

  file_header_alloc (fhead, is_ftab_page ? FILE_ALLOC_TABLE_PAGE : FILE_ALLOC_USER_PAGE, was_empty, is_full);

  file_log ("file_rv_fhead_alloc",
	    "update header in file %d|%d, header page %d|%d, lsa %lld|%d, "
	    "after %s, was_empty %s, is_full %s \n" FILE_HEAD_ALLOC_MSG,
	    VFID_AS_ARGS (&fhead->self), PGBUF_PAGE_VPID_AS_ARGS (page_fhead),
	    PGBUF_PAGE_LSA_AS_ARGS (page_fhead),
	    FILE_ALLOC_TYPE_STRING (is_ftab_page ? FILE_ALLOC_TABLE_PAGE : FILE_ALLOC_USER_PAGE),
	    was_empty ? "true" : "false", is_full ? "true" : "false", FILE_HEAD_ALLOC_AS_ARGS (fhead));

  pgbuf_set_dirty (thread_p, page_fhead, DONT_FREE);

  return NO_ERROR;
}

/*
 * file_rv_fhead_dealloc () - Recovery for file header when a page is deallocated.
 *
 * return	 : Error code
 * thread_p (in) : Thread entry
 * rcv (in)	 : Recovery data
 */
int
file_rv_fhead_dealloc (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  PAGE_PTR page_fhead = rcv->pgptr;
  FILE_HEADER *fhead;
  bool is_ftab_page;
  bool was_full;
  bool is_empty;

  assert (rcv->length == sizeof (bool) * 3);

  is_ftab_page = ((bool *) rcv->data)[0];
  is_empty = ((bool *) rcv->data)[1];
  was_full = ((bool *) rcv->data)[2];

  fhead = (FILE_HEADER *) page_fhead;

  file_header_dealloc (fhead, is_ftab_page ? FILE_ALLOC_TABLE_PAGE : FILE_ALLOC_USER_PAGE, is_empty, was_full);

  file_log ("file_rv_fhead_dealloc",
	    "update header in file %d|%d, header page %d|%d, lsa %lld|%d, "
	    "after de%s, is_empty %s, was_full %s \n" FILE_HEAD_ALLOC_MSG,
	    VFID_AS_ARGS (&fhead->self), PGBUF_PAGE_VPID_AS_ARGS (page_fhead),
	    PGBUF_PAGE_LSA_AS_ARGS (page_fhead),
	    FILE_ALLOC_TYPE_STRING (is_ftab_page ? FILE_ALLOC_TABLE_PAGE : FILE_ALLOC_USER_PAGE),
	    is_empty ? "true" : "false", was_full ? "true" : "false", FILE_HEAD_ALLOC_AS_ARGS (fhead));

  pgbuf_set_dirty (thread_p, page_fhead, DONT_FREE);

  return NO_ERROR;
}

/*
 * file_log_fhead_alloc () - Log file header statistics update when a page is allocated.
 *
 * return	   : Void.
 * thread_p (in)   : Thread entry.
 * page_fhead (in) : File header page.
 * alloc_type (in) : User/table page
 * was_empty (in)  : True if sector was empty before allocation.
 * is_full (in)    : True if sector is full after allocation.
 */
STATIC_INLINE void
file_log_fhead_alloc (THREAD_ENTRY * thread_p, PAGE_PTR page_fhead, FILE_ALLOC_TYPE alloc_type, bool was_empty,
		      bool is_full)
{
#define LOG_BOOL_COUNT 3
  LOG_DATA_ADDR addr = LOG_DATA_ADDR_INITIALIZER;
  bool is_ftab_page = (alloc_type == FILE_ALLOC_TABLE_PAGE);
  bool log_bools[3];

  assert (page_fhead != NULL);
  assert (alloc_type == FILE_ALLOC_TABLE_PAGE || alloc_type == FILE_ALLOC_USER_PAGE);
  assert (!was_empty || !is_full);

  log_bools[0] = is_ftab_page;
  log_bools[1] = was_empty;
  log_bools[2] = is_full;

  addr.pgptr = page_fhead;
  log_append_undoredo_data (thread_p, RVFL_FHEAD_ALLOC, &addr, sizeof (log_bools), sizeof (log_bools),
			    log_bools, log_bools);
  pgbuf_set_dirty (thread_p, page_fhead, DONT_FREE);

#undef LOG_BOOL_COUNT
}

/*
 * file_log_fhead_dealloc () - Log file header statistics update when a page is deallocated.
 *
 * return	   : Void.
 * thread_p (in)   : Thread entry.
 * page_fhead (in) : File header page.
 * alloc_type (in) : User/table page
 * is_empty (in)   : True if sector is empty after deallocation.
 * was_full (in)   : True if sector was full before deallocation.
 */
STATIC_INLINE void
file_log_fhead_dealloc (THREAD_ENTRY * thread_p, PAGE_PTR page_fhead, FILE_ALLOC_TYPE alloc_type, bool is_empty,
			bool was_full)
{
#define LOG_BOOL_COUNT 3
  LOG_DATA_ADDR addr = LOG_DATA_ADDR_INITIALIZER;
  bool is_ftab_page = (alloc_type == FILE_ALLOC_TABLE_PAGE);
  bool log_bools[3];

  assert (page_fhead != NULL);
  assert (alloc_type == FILE_ALLOC_TABLE_PAGE || alloc_type == FILE_ALLOC_USER_PAGE);
  assert (!is_empty || !was_full);

  log_bools[0] = is_ftab_page;
  log_bools[1] = is_empty;
  log_bools[2] = was_full;

  addr.pgptr = page_fhead;
  log_append_undoredo_data (thread_p, RVFL_FHEAD_DEALLOC, &addr, sizeof (log_bools), sizeof (log_bools),
			    log_bools, log_bools);
  pgbuf_set_dirty (thread_p, page_fhead, DONT_FREE);

#undef LOG_BOOL_COUNT
}

/*
 * file_header_update_mark_deleted () - Update mark deleted count in file header and log change.
 *
 * return	   : Void
 * thread_p (in)   : Thread entry
 * page_fhead (in) : File header page
 * delta (in)	   : Mark deleted delta
 */
STATIC_INLINE void
file_header_update_mark_deleted (THREAD_ENTRY * thread_p, PAGE_PTR page_fhead, int delta)
{
  FILE_HEADER *fhead = (FILE_HEADER *) page_fhead;
  int undo_delta = -delta;

  LOG_LSA save_lsa = *pgbuf_get_lsa (page_fhead);

  fhead->n_page_mark_delete += delta;

  if (!FILE_IS_TEMPORARY (fhead))
    {
      log_append_undoredo_data2 (thread_p, RVFL_FHEAD_MARK_DELETE, NULL,
				 page_fhead, 0, sizeof (undo_delta), sizeof (delta), &undo_delta, &delta);
    }
  pgbuf_set_dirty (thread_p, page_fhead, DONT_FREE);

  file_log ("file_header_update_mark_deleted",
	    "updated n_page_mark_delete by %d to %d in file %d|%d, "
	    "header page %d|%d, prev_lsa %lld|%d, crt_lsa %lld|%d ", delta,
	    fhead->n_page_mark_delete, VFID_AS_ARGS (&fhead->self),
	    PGBUF_PAGE_VPID_AS_ARGS (page_fhead), LSA_AS_ARGS (&save_lsa), PGBUF_PAGE_LSA_AS_ARGS (page_fhead));
}

/*
 * file_rv_header_update_mark_deleted () - Recovery for mark deleted count in file header
 *
 * return	 : NO_ERROR
 * thread_p (in) : Thread entry
 * rcv (in)	 : Recovery data
 */
int
file_rv_header_update_mark_deleted (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  int delta = *(int *) rcv->data;
  PAGE_PTR page_fhead = rcv->pgptr;
  FILE_HEADER *fhead;

  assert (page_fhead != NULL);
  assert (rcv->length == sizeof (int));

  fhead = (FILE_HEADER *) page_fhead;
  fhead->n_page_mark_delete += delta;
  pgbuf_set_dirty (thread_p, page_fhead, DONT_FREE);

  file_log ("file_rv_header_update_mark_deleted",
	    "modified n_page_mark_delete by %d to %d in file %d|%d, "
	    "header page %d|%d, lsa %lld|%d", delta,
	    fhead->n_page_mark_delete, VFID_AS_ARGS (&fhead->self),
	    PGBUF_PAGE_VPID_AS_ARGS (page_fhead), PGBUF_PAGE_LSA_AS_ARGS (page_fhead));

  return NO_ERROR;
}

/*
 * file_header_copy () - get a file header copy
 *
 * return           : ERROR_CODE
 * thread_p (in)    : thread entry
 * vfid (in)        : file identifier
 * fhead_copy (out) : file header copy
 */
STATIC_INLINE int
file_header_copy (THREAD_ENTRY * thread_p, const VFID * vfid, FILE_HEADER * fhead_copy)
{
  VPID vpid_fhead;
  PAGE_PTR page_fhead = NULL;
  FILE_HEADER *fhead = NULL;
  int error_code = NO_ERROR;

  FILE_GET_HEADER_VPID (vfid, &vpid_fhead);
  page_fhead = pgbuf_fix (thread_p, &vpid_fhead, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (page_fhead == NULL)
    {
      ASSERT_ERROR_AND_SET (error_code);
      return error_code;
    }
  fhead = (FILE_HEADER *) page_fhead;
  file_header_sanity_check (fhead);

  *fhead_copy = *fhead;

  pgbuf_unfix (thread_p, page_fhead);
  return NO_ERROR;
}

/*
 * file_header_dump () - dump file header to file
 *
 * return        : void
 * thread_p (in) : thread entry
 * fhead (in)    : file header
 * fp (in)       : output file
 */
STATIC_INLINE void
file_header_dump (THREAD_ENTRY * thread_p, const FILE_HEADER * fhead, FILE * fp)
{
  fprintf (fp, FILE_HEAD_FULL_MSG, FILE_HEAD_FULL_AS_ARGS (fhead));
  file_header_dump_descriptor (thread_p, fhead, fp);
}

/*
 * file_header_dump_descriptor () - dump descriptor in file header
 *
 * return        : void
 * thread_p (in) : thread entry
 * fhead (in)    : file header
 * fp (in)       : output file
 */
STATIC_INLINE void
file_header_dump_descriptor (THREAD_ENTRY * thread_p, const FILE_HEADER * fhead, FILE * fp)
{
  switch (fhead->type)
    {
    case FILE_HEAP:
    case FILE_HEAP_REUSE_SLOTS:
      file_print_name_of_class (thread_p, fp, &fhead->descriptor.heap.class_oid);
      fprintf (fp, "\n");
      break;

    case FILE_MULTIPAGE_OBJECT_HEAP:
      fprintf (fp, "Overflow for HFID: %10d|%5d|%10d\n", HFID_AS_ARGS (&fhead->descriptor.heap_overflow.hfid));
      break;

    case FILE_BTREE:
      {
	BTID btid;
	char *index_name = NULL;
	btid.vfid = fhead->self;
	btid.root_pageid = fhead->vpid_sticky_first.pageid;

	if (heap_get_indexinfo_of_btid (thread_p, &fhead->descriptor.btree.class_oid, &btid, NULL, NULL, NULL, NULL,
					&index_name, NULL) == NO_ERROR)
	  {
	    file_print_name_of_class (thread_p, fp, &fhead->descriptor.btree.class_oid);
	    fprintf (fp, ", %s, ATTRID: %5d \n", index_name != NULL ? index_name : "*UNKNOWN-INDEX*",
		     fhead->descriptor.btree.attr_id);
	  }
      }
      break;

    case FILE_BTREE_OVERFLOW_KEY:
      fprintf (fp, "Overflow keys for BTID: %10d|%5d|%10d\n",
	       BTID_AS_ARGS (&fhead->descriptor.btree_key_overflow.btid));
      break;

    case FILE_EXTENDIBLE_HASH:
    case FILE_EXTENDIBLE_HASH_DIRECTORY:
      file_print_name_of_class (thread_p, fp, &fhead->descriptor.ehash.class_oid);
      fprintf (fp, ", ATTRID: %5d \n", fhead->descriptor.ehash.attr_id);
      break;

    case FILE_TRACKER:
    case FILE_CATALOG:
    case FILE_QUERY_AREA:
    case FILE_TEMP:
    case FILE_UNKNOWN_TYPE:
    case FILE_DROPPED_FILES:
    case FILE_VACUUM_DATA:
    default:
      break;
    }
}

/************************************************************************/
/* File extensible data section.                                        */
/************************************************************************/

/*
 * file_extdata_init () - Initialize extensible data component for the first time.
 *
 * return	  : Void.
 * item_size (in) : Size of an item.
 * max_size (in)  : Desired maximum size for component.
 * extdata (out)  : Output initialized extensible data.
 */
STATIC_INLINE void
file_extdata_init (INT16 item_size, INT16 max_size, FILE_EXTENSIBLE_DATA * extdata)
{
  assert (extdata != NULL);
  assert (item_size > 0);
  assert (max_size > 0);

  VPID_SET_NULL (&extdata->vpid_next);
  extdata->size_of_item = item_size;
  extdata->n_items = 0;

  /* Align to size of item */
  extdata->max_size = DB_ALIGN_BELOW (max_size - FILE_EXTDATA_HEADER_ALIGNED_SIZE, extdata->size_of_item);
  if ((INT16) DB_ALIGN (extdata->max_size, MAX_ALIGNMENT) != extdata->max_size)
    {
      /* We need max alignment */
      extdata->max_size = DB_ALIGN (extdata->max_size - extdata->size_of_item, MAX_ALIGNMENT);
    }
  /* Safe guard: we should fit at least one item. */
  assert (extdata->max_size >= extdata->size_of_item);
}

/*
 * file_extdata_max_size () - Get maximum size of this extensible data including header.
 *
 * return	: Maximum size of extensible data.
 * extdata (in) : Extensible data.
 */
STATIC_INLINE int
file_extdata_max_size (const FILE_EXTENSIBLE_DATA * extdata)
{
  return FILE_EXTDATA_HEADER_ALIGNED_SIZE + extdata->max_size;
}

/*
 * file_extdata_size () - Get current size of this extensible data including header.
 *
 * return	: Current size of extensible data.
 * extdata (in) : Extensible data.
 */
STATIC_INLINE int
file_extdata_size (const FILE_EXTENSIBLE_DATA * extdata)
{
  return FILE_EXTDATA_HEADER_ALIGNED_SIZE + extdata->n_items * extdata->size_of_item;
}

/*
 * file_extdata_start () - Get pointer to first item in extensible data.
 *
 * return	: Pointer to first item.
 * extdata (in) : Extensible data.
 */
STATIC_INLINE void *
file_extdata_start (const FILE_EXTENSIBLE_DATA * extdata)
{
  return ((char *) extdata) + FILE_EXTDATA_HEADER_ALIGNED_SIZE;
}

/*
 * file_extdata_end () - Get pointer to the end of extensible data (after last item).
 *
 * return	: Pointer to end of extensible data.
 * extdata (in) : Extensible data.
 */
STATIC_INLINE void *
file_extdata_end (const FILE_EXTENSIBLE_DATA * extdata)
{
  return ((char *) extdata) + file_extdata_size (extdata);
}

/*
 * file_extdata_is_full () - Is this extensible data full (not enough room foradditional item).
 *
 * return	: True if full, false otherwise.
 * extdata (in) : Extensible data.
 */
STATIC_INLINE bool
file_extdata_is_full (const FILE_EXTENSIBLE_DATA * extdata)
{
  assert (extdata->n_items * extdata->size_of_item <= extdata->max_size);
  return (extdata->n_items + 1) * extdata->size_of_item > extdata->max_size;
}

/*
 * file_extdata_is_empty () - Is this extensible data empty?
 *
 * return	: True if empty, false otherwise.
 * extdata (in) : Extensible data.
 */
STATIC_INLINE bool
file_extdata_is_empty (const FILE_EXTENSIBLE_DATA * extdata)
{
  assert (extdata->n_items >= 0);
  return (extdata->n_items <= 0);
}

/*
 * file_extdata_item_count () - Get the item count in this extensible data.
 *
 * return	: Item count.
 * extdata (in) : Extensible data.
 */
STATIC_INLINE INT16
file_extdata_item_count (const FILE_EXTENSIBLE_DATA * extdata)
{
  return extdata->n_items;
}

/*
 * file_extdata_remaining_capacity () - Get the remaining capacity (in number of items) of this extensible data.
 *
 * return	: Remaining capacity.
 * extdata (in) : Extensible data.
 */
STATIC_INLINE INT16
file_extdata_remaining_capacity (const FILE_EXTENSIBLE_DATA * extdata)
{
  return extdata->max_size / extdata->size_of_item - extdata->n_items;
}

/*
 * file_extdata_append () - Append an item to extensible data. Caller should have checked there is enough room.
 *
 * return	    : Void.
 * extdata (in)     : Extensible data.
 * append_item (in) : Item to append.
 */
STATIC_INLINE void
file_extdata_append (FILE_EXTENSIBLE_DATA * extdata, const void *append_item)
{
  assert (!file_extdata_is_full (extdata));

  memcpy (file_extdata_end (extdata), append_item, extdata->size_of_item);
  extdata->n_items++;
}

/*
 * file_extdata_append_array () - Append an array of items to extensible data. Caller should have checked there is
 *				  enough room.
 *
 * return	     : Void.
 * extdata (in)	     : Extensible data.
 * append_items (in) : Item array.
 * count (in)	     : Item count.
 */
STATIC_INLINE void
file_extdata_append_array (FILE_EXTENSIBLE_DATA * extdata, const void *append_items, INT16 count)
{
  assert (file_extdata_remaining_capacity (extdata) >= count);

  memcpy (file_extdata_end (extdata), append_items, extdata->size_of_item * count);
  extdata->n_items += count;
}

/*
 * file_extdata_at () - Pointer to item at given index.
 *
 * return	: Pointer to item.
 * extdata (in) : Extensible data.
 * index (in)   : Item index.
 */
STATIC_INLINE void *
file_extdata_at (const FILE_EXTENSIBLE_DATA * extdata, int index)
{
  assert (index >= 0 && index <= extdata->n_items);
  return (char *) file_extdata_start (extdata) + extdata->size_of_item * index;
}

/*
 * file_extdata_can_merge () - Check if source extensible data component can be merged into destination extensible data.
 *
 * return	     : True if destination extensible data remaining capacity can cover all items in source extensible
 *		       data. False otherwise.
 * extdata_src (in)  : Source extensible data.
 * extdata_dest (in) : Destination extensible data.
 */
STATIC_INLINE bool
file_extdata_can_merge (const FILE_EXTENSIBLE_DATA * extdata_src, const FILE_EXTENSIBLE_DATA * extdata_dest)
{
  return file_extdata_remaining_capacity (extdata_dest) >= extdata_src->n_items;
}

/*
 * file_extdata_merge_unordered () - Append source extensible data to destination extensible data.
 *
 * return		 : Void.
 * extdata_src (in)	 : Source extensible data.
 * extdata_dest (in/out) : Destination extensible data.
 */
STATIC_INLINE void
file_extdata_merge_unordered (const FILE_EXTENSIBLE_DATA * extdata_src, FILE_EXTENSIBLE_DATA * extdata_dest)
{
  file_extdata_append_array (extdata_dest, file_extdata_start (extdata_src), file_extdata_item_count (extdata_src));
}

/*
 * file_extdata_merge_ordered () - Merge source extensible data into destination extensible data and keep items ordered.
 *
 * return		 : Void.
 * extdata_src (in)	 : Source extensible data.
 * extdata_dest (in/out) : Destination extensible data.
 * compare_func (in)	 : Compare function (to order items).
 */
STATIC_INLINE void
file_extdata_merge_ordered (const FILE_EXTENSIBLE_DATA * extdata_src, FILE_EXTENSIBLE_DATA * extdata_dest,
			    int (*compare_func) (const void *, const void *))
{
  int n_merged = 0;
  char *dest_ptr;
  char *dest_end_ptr;
  const char *src_ptr;
  const char *src_end_ptr;
  const char *src_new_ptr;
  int memsize = 0;

#if !defined (NDEBUG)
  char *debug_dest_end_ptr =
    (char *) file_extdata_end (extdata_dest) + extdata_src->n_items * extdata_src->size_of_item;
#endif /* !NDEBUG */

  /* safe guard: destination has enough capacity to include all source items. */
  assert (file_extdata_remaining_capacity (extdata_dest) >= file_extdata_item_count (extdata_src));

  src_ptr = (const char *) file_extdata_start (extdata_src);
  src_end_ptr = (const char *) file_extdata_end (extdata_src);

  dest_ptr = (char *) file_extdata_start (extdata_dest);
  dest_end_ptr = (char *) file_extdata_end (extdata_dest);

  /* advance source and destination pointers based on item order. Stop when end of destination is reached. */
  while (dest_ptr < dest_end_ptr)
    {
      /* collect all items from source that are smaller than current destination item. */
      for (src_new_ptr = src_ptr; src_new_ptr < src_end_ptr; src_new_ptr += extdata_src->size_of_item)
	{
	  assert (compare_func (src_new_ptr, dest_ptr) != 0);
	  if (compare_func (src_new_ptr, dest_ptr) > 0)
	    {
	      break;
	    }
	}
      if (src_new_ptr > src_ptr)
	{
	  /* move to dest_ptr */
	  memsize = (int) (src_new_ptr - src_ptr);
	  /* make room for new data */
	  memmove (dest_ptr + memsize, dest_ptr, dest_end_ptr - dest_ptr);
	  memcpy (dest_ptr, src_ptr, memsize);

	  dest_ptr += memsize;
	  dest_end_ptr += memsize;

	  src_ptr = src_new_ptr;

	  assert (dest_end_ptr <= debug_dest_end_ptr);
	}
      if (src_ptr >= src_end_ptr)
	{
	  /* source extensible data was consumed. */
	  assert (src_ptr == src_end_ptr);
	  break;
	}
      /* skip all items from destination smaller than current source item */
      for (; dest_ptr < dest_end_ptr; dest_ptr += extdata_dest->size_of_item)
	{
	  assert (compare_func (src_ptr, dest_ptr) != 0);
	  if (compare_func (src_ptr, dest_ptr) <= 0)
	    {
	      break;
	    }
	}
    }

  if (src_ptr < src_end_ptr)
    {
      /* end of destination reached. append remaining source items into destination. */
      assert (dest_ptr == dest_end_ptr);
      memcpy (dest_end_ptr, src_ptr, src_end_ptr - src_ptr);
      assert (dest_end_ptr + (src_end_ptr - src_ptr) == debug_dest_end_ptr);
    }
  else
    {
      /* all source items were moved to destination */
      assert (debug_dest_end_ptr == dest_end_ptr);
    }

  extdata_dest->n_items += extdata_src->n_items;
  assert (debug_dest_end_ptr == file_extdata_end (extdata_dest));
}

/*
 * file_extdata_find_ordered () - Find position for given item. If item does not exist, return the position of first
 *				  bigger item.
 *
 * return	     : Void.
 * extdata (in)	     : Extensible data.
 * item_to_find (in) : Pointer to item to find.
 * compare_func (in) : Compare function used for binary search.
 * found (out)	     : Output true if item was found, false otherwise.
 * position (out)    : Output the right position of item (found or not found).
 */
static void
file_extdata_find_ordered (const FILE_EXTENSIBLE_DATA * extdata, const void *item_to_find,
			   int (*compare_func) (const void *, const void *), bool * found, int *position)
{
  assert (found != NULL);
  assert (position != NULL);

  *position = util_bsearch (item_to_find, file_extdata_start (extdata), file_extdata_item_count (extdata),
			    extdata->size_of_item, compare_func, found);
}

/*
 * file_extdata_insert_at () - Insert items at given position in extensible data.
 *
 * return	 : Void.
 * extdata (in)  : Extensible data.
 * position (in) : Position to insert items.
 * count (in)	 : Item count.
 * data (in)	 : Items to insert.
 */
STATIC_INLINE void
file_extdata_insert_at (FILE_EXTENSIBLE_DATA * extdata, int position, int count, const void *data)
{
  char *copy_at;
  int memmove_size;

  assert (extdata != NULL);
  assert (!file_extdata_is_full (extdata));
  assert (data != NULL);
  assert (position >= 0 && position <= file_extdata_item_count (extdata));

  /* move current items at desired position to the right. */
  memmove_size = (extdata->n_items - position) * extdata->size_of_item;
  copy_at = (char *) file_extdata_at (extdata, position);
  if (memmove_size > 0)
    {
      memmove (copy_at + extdata->size_of_item * count, copy_at, memmove_size);
    }

  /* copy new items at position */
  memcpy (copy_at, data, extdata->size_of_item * count);

  /* update item count */
  extdata->n_items += count;
}

/*
 * file_extdata_remove_at () - Remove items from give position in extensible data.
 *
 * return        : Void.
 * extdata (in)  : Extensible data.
 * position (in) : Position where items must be removed.
 * count (in)	 : Item count.
 */
STATIC_INLINE void
file_extdata_remove_at (FILE_EXTENSIBLE_DATA * extdata, int position, int count)
{
  char *remove_at;
  int memmove_size;

  if (position < 0 || position >= extdata->n_items)
    {
      /* bad index. give up */
      assert_release (false);
      return;
    }

  /* remove items */
  remove_at = (char *) file_extdata_at (extdata, position);
  memmove_size = (extdata->n_items - count - position) * extdata->size_of_item;
  if (memmove_size > 0)
    {
      memmove (remove_at, remove_at + extdata->size_of_item * count, memmove_size);
    }

  /* update item count */
  extdata->n_items -= count;
}

/*
 * file_extdata_apply_funcs () - Process extensible data components and apply functions to each component and/or to each
 *				 item.
 *
 * return		   : Error code.
 * thread_p (in)	   : Thread entry.
 * extdata_in (in)	   : First extensible data component.
 * f_extdata (in)	   : Function to apply for each extensible data component (can be NULL).
 * f_extdata_args (in/out) : Argument for component function.
 * f_item (in)		   : Function to apply for each item (can be NULL).
 * f_item_args (in/out)	   : Argument for item function.
 * for_write (in)          : Should page be fixed for write?
 * extdata_out (out)	   : Output current extensible data component if processing is stopped.
 * page_out (out)	   : Output page of current extensible data component if processing is stopped.
 */
static int
file_extdata_apply_funcs (THREAD_ENTRY * thread_p, FILE_EXTENSIBLE_DATA * extdata_in, FILE_EXTDATA_FUNC f_extdata,
			  void *f_extdata_args, FILE_EXTDATA_ITEM_FUNC f_item, void *f_item_args,
			  bool for_write, FILE_EXTENSIBLE_DATA ** extdata_out, PAGE_PTR * page_out)
{
  int i;
  bool stop = false;		/* forces to stop processing extensible data */
  PAGE_PTR page_extdata = NULL;	/* extensible data page */
  PGBUF_LATCH_MODE latch_mode = for_write ? PGBUF_LATCH_WRITE : PGBUF_LATCH_READ;
  int error_code = NO_ERROR;

  if (page_out != NULL)
    {
      *page_out = NULL;		/* make it sure for an error */
    }

  while (true)
    {
      /* catch infinite loop, if any */
      assert (page_extdata == NULL || !VPID_EQ (pgbuf_get_vpid_ptr (page_extdata), &extdata_in->vpid_next));
      if (f_extdata != NULL)
	{
	  /* apply f_extdata */
	  error_code = f_extdata (thread_p, extdata_in, &stop, f_extdata_args);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      goto exit;
	    }
	  if (stop)
	    {
	      /* stop processing */
	      goto exit;
	    }
	}

      if (f_item != NULL)
	{
	  /* iterate through all items in current page. */
	  for (i = 0; i < file_extdata_item_count (extdata_in); i++)
	    {
	      /* apply f_item */
	      error_code = f_item (thread_p, file_extdata_at (extdata_in, i), i, &stop, f_item_args);
	      if (error_code != NO_ERROR)
		{
		  ASSERT_ERROR ();
		  goto exit;
		}
	      if (stop)
		{
		  goto exit;
		}
	    }
	}

      if (VPID_ISNULL (&extdata_in->vpid_next))
	{
	  /* end of extensible data. */
	  break;
	}

      /* advance to next page */
      if (page_extdata != NULL)
	{
	  pgbuf_unfix_and_init (thread_p, page_extdata);
	}
      page_extdata = pgbuf_fix (thread_p, &extdata_in->vpid_next, OLD_PAGE, latch_mode, PGBUF_UNCONDITIONAL_LATCH);
      if (page_extdata == NULL)
	{
	  ASSERT_ERROR_AND_SET (error_code);
	  goto exit;
	}
      /* get component in next page */
      extdata_in = (FILE_EXTENSIBLE_DATA *) page_extdata;
    }

exit:
  if (stop && page_out != NULL)
    {
      /* output current page */
      *page_out = page_extdata;
    }
  else if (page_extdata != NULL)
    {
      /* unfix current page */
      pgbuf_unfix (thread_p, page_extdata);
    }

  if (stop && extdata_out != NULL)
    {
      /* output current extensible data component */
      *extdata_out = extdata_in;
    }

  return error_code;
}

/*
 * file_extdata_func_for_search_ordered () - Function callable by file_extdata_apply_funcs; binary search for an item
 *					     in ordered extensible data.
 *
 * return	 : NO_ERROR.
 * thread_p (in) : Thread entry.
 * extdata (in)  : Extensible data.
 * stop (out)	 : Output true when item is found and search can be stopped.
 * args (in/out) : Search context.
 */
static int
file_extdata_func_for_search_ordered (THREAD_ENTRY * thread_p, const FILE_EXTENSIBLE_DATA * extdata, bool * stop,
				      void *args)
{
  FILE_EXTENSIBLE_DATA_SEARCH_CONTEXT *search_context = (FILE_EXTENSIBLE_DATA_SEARCH_CONTEXT *) args;

  assert (search_context != NULL);

  file_extdata_find_ordered (extdata, search_context->item_to_find, search_context->compare_func,
			     &search_context->found, &search_context->position);
  if (search_context->found)
    {
      *stop = true;
    }

  return NO_ERROR;
}

/*
 * file_extdata_item_func_for_search () - Function callable by file_extdata_apply_funcs; searches item by comparing
 *					  extensible data item to the item in search context.
 *
 * return	 : NO_ERROR.
 * thread_p (in) : Thread entry.
 * item (in)	 : Item in extensible data.
 * index (in)	 : Index of item in extensible data.
 * stop (out)	 : Output true when item is found and search can be stopped.
 * args (in/out) : Search context.
 */
static int
file_extdata_item_func_for_search (THREAD_ENTRY * thread_p, const void *item, int index, bool * stop, void *args)
{
  FILE_EXTENSIBLE_DATA_SEARCH_CONTEXT *search_context = (FILE_EXTENSIBLE_DATA_SEARCH_CONTEXT *) args;

  if (search_context->compare_func (search_context->item_to_find, item) == 0)
    {
      /* Found */
      search_context->found = true;
      search_context->position = index;
      *stop = true;
    }
  return NO_ERROR;
}

/*
 * file_extdata_search_item () - Search for item in extensible data.
 *
 * return	      : Error code.
 * thread_p (in)      : Thread entry.
 * extdata (in/out)   : Extensible data. First component as input, component where item was found as output.
 * item_to_find (in)  : Searched item.
 * compare_func (in)  : Compare function used to find item.
 * is_ordered (in)    : True if items in extensible are ordered and can be searched using binary search. False otherwise
 * for_write (in)     : Should page be fixed for write?
 * found (out)	      : Output true if item is found in extensible data, false otherwise.
 * position (out)     : Output the position of found item (if found) in its extensible data component.
 * page_extdata (out) : Output page of extensible data component where item is found (if found).
 */
static int
file_extdata_search_item (THREAD_ENTRY * thread_p, FILE_EXTENSIBLE_DATA ** extdata, const void *item_to_find,
			  int (*compare_func) (const void *, const void *),
			  bool is_ordered, bool for_write, bool * found, int *position, PAGE_PTR * page_extdata)
{
  FILE_EXTENSIBLE_DATA_SEARCH_CONTEXT search_context;
  FILE_EXTENSIBLE_DATA *extdata_in = *extdata;
  int error_code = NO_ERROR;

  search_context.item_to_find = item_to_find;
  search_context.compare_func = compare_func;
  search_context.found = false;
  search_context.position = -1;

  if (is_ordered)
    {
      error_code = file_extdata_apply_funcs (thread_p, extdata_in, file_extdata_func_for_search_ordered,
					     &search_context, NULL, NULL, for_write, extdata, page_extdata);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  assert (*page_extdata == NULL);
	  return error_code;
	}
    }
  else
    {
      error_code = file_extdata_apply_funcs (thread_p, extdata_in, NULL, NULL, file_extdata_item_func_for_search,
					     &search_context, for_write, extdata, page_extdata);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  assert (*page_extdata == NULL);
	  return error_code;
	}
    }

  if (found != NULL)
    {
      *found = search_context.found;
    }

  if (position != NULL)
    {
      *position = search_context.position;
    }

  return NO_ERROR;
}

/*
 * file_extdata_find_not_full () - Find an extensible data component that is not full.
 *
 * return	    : Error code.
 * thread_p (in)    : Thread entry.
 * extdata (in/out) : Extensible data. First component as input, component where free space is found as output.
 * page_out (out)   : Output page of component with free space.
 * found (out)	    : Output true if a component with free space is found.
 */
static int
file_extdata_find_not_full (THREAD_ENTRY * thread_p, FILE_EXTENSIBLE_DATA ** extdata, PAGE_PTR * page_out, bool * found)
{
  VPID vpid_next;
  int error_code = NO_ERROR;

  assert (extdata != NULL && *extdata != NULL);
  assert (page_out != NULL);
  assert (found != NULL);

  /* how it works:
   * we are looking for the first extensible data component that still has space for one additional item. the extensible
   * data and page where the space is found is then output.
   *
   * note: the input page is usually NULL. the input extensible data usually belongs to file header. this would unfix
   *       page_out when it advances to next page and we don't want to unfix header page.
   * note: if all extensible data components are full, the last extensible data and page are output. the caller can
   *       then use them to append a new page and a new extensible data component.
   */

  *found = false;

  while (file_extdata_is_full (*extdata))
    {
      VPID_COPY (&vpid_next, &(*extdata)->vpid_next);
      if (VPID_ISNULL (&vpid_next))
	{
	  /* Not found. */
	  return NO_ERROR;
	}

      /* Move to next page */
      if (*page_out != NULL)
	{
	  pgbuf_unfix_and_init (thread_p, *page_out);
	}

      *page_out = pgbuf_fix (thread_p, &vpid_next, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
      if (*page_out == NULL)
	{
	  ASSERT_ERROR_AND_SET (error_code);
	  return error_code;
	}

      *extdata = (FILE_EXTENSIBLE_DATA *) (*page_out);
    }

  /* Found not full */
  *found = true;
  return NO_ERROR;
}

/*
 * file_rv_extdata_set_next () - Recovery function to set extensible data next page.
 *
 * return	 : NO_ERROR.
 * thread_p (in) : Thread entry.
 * rcv (in)	 : Recovery data.
 */
int
file_rv_extdata_set_next (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  PAGE_PTR page_ftab = rcv->pgptr;
  VPID *vpid_next = (VPID *) rcv->data;
  FILE_EXTENSIBLE_DATA *extdata = NULL;

  assert (page_ftab != NULL);
  assert (rcv->length == sizeof (VPID));
  assert (rcv->offset >= 0 && rcv->offset < DB_PAGESIZE);

  extdata = (FILE_EXTENSIBLE_DATA *) (page_ftab + rcv->offset);
  VPID_COPY (&extdata->vpid_next, vpid_next);

  file_log ("file_rv_extdata_set_next",
	    "page %d|%d, lsa %lld|%d, changed extdata link \n"
	    FILE_EXTDATA_MSG ("extdata after"),
	    PGBUF_PAGE_VPID_AS_ARGS (page_ftab), PGBUF_PAGE_LSA_AS_ARGS (page_ftab), FILE_EXTDATA_AS_ARGS (extdata));

  pgbuf_set_dirty (thread_p, page_ftab, DONT_FREE);
  return NO_ERROR;
}

/*
 * file_rv_dump_extdata_set_next () - Dump VPID for recovery of extensible data next page.
 *
 * return	      : Void.
 * fp (in/out)	      : Dump output.
 * ignore_length (in) : Length of recovery data.
 * data (in)	      : Recovery data.
 */
void
file_rv_dump_extdata_set_next (FILE * fp, int ignore_length, void *data)
{
  VPID *vpid_next = (VPID *) data;

  fprintf (fp, "Set extensible data next page to %d|%d.\n", VPID_AS_ARGS (vpid_next));
}

/*
 * file_rv_extdata_add () - Add items to extensible data for recovery.
 *
 * return	 : NO_ERROR
 * thread_p (in) : Thread entry
 * rcv (in)	 : Recovery data
 */
int
file_rv_extdata_add (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  PAGE_PTR page_ftab = rcv->pgptr;
  FILE_EXTENSIBLE_DATA *extdata = NULL;
  int pos, count, offset = 0;

  assert (page_ftab != NULL);
  assert (rcv->offset >= 0 && rcv->offset < DB_PAGESIZE);

  pos = *(int *) (rcv->data + offset);
  offset += sizeof (pos);

  count = *(int *) (rcv->data + offset);
  offset += sizeof (count);

  extdata = (FILE_EXTENSIBLE_DATA *) (page_ftab + rcv->offset);
  assert (rcv->length == offset + extdata->size_of_item * count);

  file_extdata_insert_at (extdata, pos, count, rcv->data + offset);

  file_log ("file_rv_extdata_add",
	    "add %d entries at position %d in page %d|%d, lsa %lld|%d \n"
	    FILE_EXTDATA_MSG ("extdata after"), count, pos,
	    PGBUF_PAGE_VPID_AS_ARGS (page_ftab), PGBUF_PAGE_LSA_AS_ARGS (page_ftab), FILE_EXTDATA_AS_ARGS (extdata));

  pgbuf_set_dirty (thread_p, page_ftab, DONT_FREE);
  return NO_ERROR;
}

/*
 * file_rv_extdata_remove () - Remove items from extensible data for recovery.
 *
 * return	 : NO_ERROR
 * thread_p (in) : Thread entry
 * rcv (in)	 : Recovery data
 */
int
file_rv_extdata_remove (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  PAGE_PTR page_ftab = rcv->pgptr;
  FILE_EXTENSIBLE_DATA *extdata = NULL;
  int pos;
  int count;
  int offset = 0;

  assert (page_ftab != NULL);
  assert (rcv->offset >= 0 && rcv->offset < DB_PAGESIZE);

  pos = *(int *) (rcv->data + offset);
  offset += sizeof (pos);

  count = *(int *) (rcv->data + offset);
  offset += sizeof (count);

  assert (offset == rcv->length);

  extdata = (FILE_EXTENSIBLE_DATA *) (page_ftab + rcv->offset);
  file_extdata_remove_at (extdata, pos, count);

  file_log ("file_rv_extdata_remove",
	    "remove %d entries at position %d in page %d|%d, lsa %lld|%d"
	    FILE_EXTDATA_MSG ("extdata after"), count, pos,
	    PGBUF_PAGE_VPID_AS_ARGS (page_ftab), PGBUF_PAGE_LSA_AS_ARGS (page_ftab), FILE_EXTDATA_AS_ARGS (extdata));

  pgbuf_set_dirty (thread_p, page_ftab, DONT_FREE);
  return NO_ERROR;
}

/*
 * file_rv_dump_extdata_add () - Dump add items to extensible data for recovery
 *
 * return      : Void
 * fp (in)     : Dump output
 * length (in) : Recovery data length
 * data (in)   : Recovery data
 */
void
file_rv_dump_extdata_add (FILE * fp, int length, void *data)
{
  int pos, count, offset = 0;

  pos = *(int *) ((char *) data + offset);
  offset += sizeof (pos);

  count = *(int *) ((char *) data + offset);
  offset += sizeof (count);

  fprintf (fp, "Add to extensible data at position = %d, count = %d.\n", pos, count);
  log_rv_dump_hexa (fp, length - offset, (char *) data + offset);
}

/*
 * file_rv_dump_extdata_remove () - Dump remove items from extensible data for recovery
 *
 * return      : Void
 * fp (in)     : Dump output
 * length (in) : Recovery data length
 * data (in)   : Recovery data
 */
void
file_rv_dump_extdata_remove (FILE * fp, int length, void *data)
{
  int pos, count, offset = 0;

  pos = *(int *) ((char *) data + offset);
  offset += sizeof (pos);

  count = *(int *) ((char *) data + offset);
  offset += sizeof (count);
  assert (length == offset);

  fprintf (fp, "Remove from extensible data at position = %d, count = %d.\n", pos, count);
}

/*
 * file_log_extdata_add () - Log adding items to extensible data
 *
 * return	 : Void
 * thread_p (in) : Thread entry
 * extdata (in)	 : Extensible data
 * page (in)	 : Page of extensible data
 * position (in) : Position in extensible data where items are added
 * count (in)	 : Item count
 * data (in)	 : Item(s)
 */
STATIC_INLINE void
file_log_extdata_add (THREAD_ENTRY * thread_p,
		      const FILE_EXTENSIBLE_DATA * extdata, PAGE_PTR page, int position, int count, const void *data)
{
  LOG_DATA_ADDR addr = LOG_DATA_ADDR_INITIALIZER;
  LOG_CRUMB crumbs[3];

  addr.pgptr = page;
  addr.offset = (PGLENGTH) (((char *) extdata) - page);

  crumbs[0].data = &position;
  crumbs[0].length = sizeof (position);

  crumbs[1].data = &count;
  crumbs[1].length = sizeof (count);

  crumbs[2].data = data;
  crumbs[2].length = extdata->size_of_item * count;

  log_append_undoredo_crumbs (thread_p, RVFL_EXTDATA_ADD, &addr, 2, 3, crumbs, crumbs);
  pgbuf_set_dirty (thread_p, page, DONT_FREE);
}

/*
 * file_log_extdata_remove () - Log removing items from extensible data
 *
 * return	 : Void
 * thread_p (in) : Thread entry
 * extdata (in)	 : Extensible data
 * page (in)	 : Page of extensible data
 * position (in) : Position in extensible data from where items are removed
 * count (in)	 : Item count
 */
STATIC_INLINE void
file_log_extdata_remove (THREAD_ENTRY * thread_p,
			 const FILE_EXTENSIBLE_DATA * extdata, PAGE_PTR page, int position, int count)
{
  LOG_DATA_ADDR addr = LOG_DATA_ADDR_INITIALIZER;
  LOG_CRUMB crumbs[3];

  addr.pgptr = page;
  addr.offset = (PGLENGTH) (((char *) extdata) - page);

  crumbs[0].data = &position;
  crumbs[0].length = sizeof (position);

  crumbs[1].data = &count;
  crumbs[1].length = sizeof (count);

  crumbs[2].data = file_extdata_at (extdata, position);
  crumbs[2].length = extdata->size_of_item * count;

  log_append_undoredo_crumbs (thread_p, RVFL_EXTDATA_REMOVE, &addr, 3, 2, crumbs, crumbs);
  pgbuf_set_dirty (thread_p, page, DONT_FREE);
}

/*
 * file_log_extdata_set_next () - Log setting next page VPID into extensible data
 *
 * return	  : Void
 * thread_p (in)  : Thread entry
 * extdata (in)	  : Extensible data
 * page (in)	  : Page of extensible data
 * vpid_next (in) : New value for next page VPID.
 */
STATIC_INLINE void
file_log_extdata_set_next (THREAD_ENTRY * thread_p,
			   const FILE_EXTENSIBLE_DATA * extdata, PAGE_PTR page, VPID * vpid_next)
{
  LOG_DATA_ADDR addr = LOG_DATA_ADDR_INITIALIZER;
  LOG_LSA save_lsa;

  addr.pgptr = page;
  addr.offset = (PGLENGTH) (((char *) extdata) - page);
  save_lsa = *pgbuf_get_lsa (page);
  log_append_undoredo_data (thread_p, RVFL_EXTDATA_SET_NEXT, &addr,
			    sizeof (VPID), sizeof (VPID), &extdata->vpid_next, vpid_next);

  file_log ("file_log_extdata_set_next",
	    "page %d|%d, prev_lsa %lld|%d, crt_lsa %lld|%d, "
	    "change extdata link to %d|%d, \n"
	    FILE_EXTDATA_MSG ("extdata before"),
	    PGBUF_PAGE_VPID_AS_ARGS (page), LSA_AS_ARGS (&save_lsa),
	    PGBUF_PAGE_LSA_AS_ARGS (page), VPID_AS_ARGS (vpid_next), FILE_EXTDATA_AS_ARGS (extdata));

  pgbuf_set_dirty (thread_p, page, DONT_FREE);
}

/*
 * file_rv_extdata_merge_undo () - Undo recovery of merging extensible data components
 *
 * return	 : NO_ERROR
 * thread_p (in) : Thread entry
 * rcv (in)	 : Recovery data
 */
int
file_rv_extdata_merge_undo (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  FILE_EXTENSIBLE_DATA *extdata_in_page;
  FILE_EXTENSIBLE_DATA *extdata_in_rcv;

  assert (rcv->pgptr != NULL);
  assert (rcv->offset >= 0 && rcv->offset < DB_PAGESIZE);

  /* how it works:
   * before merge, the entire old extensible data is logged for undo. we just need to replace current extensible data
   * with the one in recovery data
   */

  extdata_in_page = (FILE_EXTENSIBLE_DATA *) (rcv->pgptr + rcv->offset);
  extdata_in_rcv = (FILE_EXTENSIBLE_DATA *) rcv->data;

  assert (file_extdata_size (extdata_in_rcv) == rcv->length);

  /* overwrite extdata with recovery. */
  memcpy (extdata_in_page, extdata_in_rcv, rcv->length);
  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);

  file_log ("file_rv_extdata_merge_undo",
	    "page %d|%d lsa %lld|%d" FILE_EXTDATA_MSG ("extdata after"),
	    PGBUF_PAGE_VPID_AS_ARGS (rcv->pgptr),
	    PGBUF_PAGE_LSA_AS_ARGS (rcv->pgptr), FILE_EXTDATA_AS_ARGS (extdata_in_page));

  return NO_ERROR;
}

/*
 * file_rv_extdata_merge_redo_internal () - Redo recovery of merging extensible data components
 *
 * return	     : NO_ERROR
 * thread_p (in)     : Thread entry
 * rcv (in)	     : Recovery data
 * compare_func (in) : NULL for unordered merge, or a compare function for ordered merge
 */
static int
file_rv_extdata_merge_redo_internal (THREAD_ENTRY * thread_p, LOG_RCV * rcv,
				     int (*compare_func) (const void *, const void *))
{
  FILE_EXTENSIBLE_DATA *extdata_in_page;
  FILE_EXTENSIBLE_DATA *extdata_in_rcv;

  assert (rcv->pgptr != NULL);
  assert (rcv->offset >= 0 && rcv->offset < DB_PAGESIZE);

  /* how it works:
   * the source extensible data is logged for redo purpose. on recovery, we need to merge the extensible data components
   * again. the recovery function used for redo will provide the type of merge through compare_func argument.
   */

  extdata_in_page = (FILE_EXTENSIBLE_DATA *) (rcv->pgptr + rcv->offset);
  extdata_in_rcv = (FILE_EXTENSIBLE_DATA *) rcv->data;

  assert (file_extdata_size (extdata_in_rcv) == rcv->length);

  if (compare_func)
    {
      file_extdata_merge_ordered (extdata_in_rcv, extdata_in_page, compare_func);
    }
  else
    {
      file_extdata_merge_unordered (extdata_in_rcv, extdata_in_page);
    }

  file_log ("file_rv_extdata_merge_redo_internal",
	    "page %d|%d, lsa %lld|%d, %s merge \n"
	    FILE_EXTDATA_MSG ("extdata after"),
	    PGBUF_PAGE_VPID_AS_ARGS (rcv->pgptr),
	    PGBUF_PAGE_LSA_AS_ARGS (rcv->pgptr),
	    compare_func ? "ordered" : "unordered", FILE_EXTDATA_AS_ARGS (extdata_in_page));

  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);

  return NO_ERROR;
}

/*
 * file_rv_extdata_merge_redo () - Redo extensible data components merge (unordered)
 *
 * return	     : NO_ERROR
 * thread_p (in)     : Thread entry
 * rcv (in)	     : Recovery data
 */
int
file_rv_extdata_merge_redo (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  return file_rv_extdata_merge_redo_internal (thread_p, rcv, NULL);
}

/*
 * file_rv_extdata_merge_compare_vsid_redo () - Redo extensible data components merge (ordered by file_compare_vsids)
 *
 * return	     : NO_ERROR
 * thread_p (in)     : Thread entry
 * rcv (in)	     : Recovery data
 */
int
file_rv_extdata_merge_compare_vsid_redo (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  return file_rv_extdata_merge_redo_internal (thread_p, rcv, file_compare_vsids);
}

/*
 * file_log_extdata_merge () - Log merging two extensible data components
 *
 * return	     : Void
 * thread_p (in)     : Thread entry
 * extdata_dest (in) : Destination extensible data (for undo recovery)
 * page_dest (in)    : Page of destination extensible data
 * extdata_src (in)  : Source extensible data (for redo recovery)
 * rcvindex (in)     : Recovery index (to know the type of merge, ordered or unordered).
 */
STATIC_INLINE void
file_log_extdata_merge (THREAD_ENTRY * thread_p, const FILE_EXTENSIBLE_DATA * extdata_dest,
			PAGE_PTR page_dest, const FILE_EXTENSIBLE_DATA * extdata_src, LOG_RCVINDEX rcvindex)
{
  LOG_DATA_ADDR addr = LOG_DATA_ADDR_INITIALIZER;

  addr.pgptr = page_dest;
  addr.offset = (PGLENGTH) (((char *) extdata_dest) - page_dest);

  log_append_undoredo_data (thread_p, rcvindex, &addr, file_extdata_size (extdata_dest),
			    file_extdata_size (extdata_src), extdata_dest, extdata_src);
}

/*
 * file_extdata_update_item () - Update extensible data item and log the change.
 *
 * return            : void
 * thread_p (in)     : thread entry
 * page_extdata (in) : page of extensible data
 * item_newval (in)  : new item value
 * index_item (in)   : index to item being updated
 * extdata (in/out)  : extensible data (one item is modified in this function)
 */
STATIC_INLINE void
file_extdata_update_item (THREAD_ENTRY * thread_p, PAGE_PTR page_extdata, const void *item_newval, int index_item,
			  FILE_EXTENSIBLE_DATA * extdata)
{
  char *item_in_page = (char *) file_extdata_at (extdata, index_item);
  PGLENGTH offset_in_page = (PGLENGTH) (item_in_page - page_extdata);

  log_append_undoredo_data2 (thread_p, RVFL_EXTDATA_UPDATE_ITEM, NULL, page_extdata, offset_in_page,
			     extdata->size_of_item, extdata->size_of_item, item_in_page, item_newval);

  memcpy (item_in_page, item_newval, extdata->size_of_item);
  pgbuf_set_dirty (thread_p, page_extdata, DONT_FREE);
}

/************************************************************************/
/* Partially allocated sectors section                                  */
/************************************************************************/

/*
 * file_partsect_is_full () - Is partial sector full?
 *
 * return	 : True if partial sector is full, false otherwise.
 * partsect (in) : Partial sector.
 */
STATIC_INLINE bool
file_partsect_is_full (FILE_PARTIAL_SECTOR * partsect)
{
  return partsect->page_bitmap == FILE_FULL_PAGE_BITMAP;
}

/*
 * file_partsect_is_empty () - Is partial sector empty?
 *
 * return	 : True if partial sector is empty, false otherwise.
 * partsect (in) : Partial sector.
 */
STATIC_INLINE bool
file_partsect_is_empty (FILE_PARTIAL_SECTOR * partsect)
{
  return partsect->page_bitmap == FILE_EMPTY_PAGE_BITMAP;
}

/*
 * file_partsect_is_bit_set () - Is bit in partial sector bitmap set at offset?
 *
 * return	 : True if bit is set, false otherwise.
 * partsect (in) : Partial sector.
 * offset (in)   : Offset to bit.
 */
STATIC_INLINE bool
file_partsect_is_bit_set (FILE_PARTIAL_SECTOR * partsect, int offset)
{
  return bit64_is_set (partsect->page_bitmap, offset);
}

/*
 * file_partsect_set_bit () - Set bit in partial sector bitmap at offset. The bit is expected to be unset.
 *
 * return	 : Void.
 * partsect (in) : Partial sector.
 * offset (in)	 : Offset to bit.
 */
STATIC_INLINE void
file_partsect_set_bit (FILE_PARTIAL_SECTOR * partsect, int offset)
{
  assert (!file_partsect_is_bit_set (partsect, offset));

  partsect->page_bitmap = bit64_set (partsect->page_bitmap, offset);
}

/*
 * file_partsect_clear_bit () - Clear bit in partial sector bitmap at offset. The bit is expected to be set.
 *
 * return	 : Void.
 * partsect (in) : Partial sector.
 * offset (in)	 : Offset to bit.
 */
STATIC_INLINE void
file_partsect_clear_bit (FILE_PARTIAL_SECTOR * partsect, int offset)
{
  assert (file_partsect_is_bit_set (partsect, offset));

  partsect->page_bitmap = bit64_clear (partsect->page_bitmap, offset);
}

/*
 * file_partsect_pageid_to_offset () - Convert pageid to offset in sector bitmap.
 *
 * return	 : Offset to bit in bitmap.
 * partsect (in) : Partial sector.
 * pageid (in)   : Page ID.
 */
STATIC_INLINE int
file_partsect_pageid_to_offset (FILE_PARTIAL_SECTOR * partsect, PAGEID pageid)
{
  assert (SECTOR_FROM_PAGEID (pageid) == partsect->vsid.sectid);

  if (SECTOR_FROM_PAGEID (pageid) != partsect->vsid.sectid)
    {
      return -1;
    }

  return (int) (pageid - SECTOR_FIRST_PAGEID (partsect->vsid.sectid));
}

/*
 * file_partsect_alloc () - Try to allocate a page in partial sector.
 *
 * return	     : True if allocation was successful. False if the partial sector was actually full.
 * partsect (in/out) : Partial sector to allocate a page from. The allocated page bit is set afterwards.
 * vpid_out (out)    : If not NULL, it outputs the VPID of allocated page.
 * offset_out (out)  : If not NULL, it outputs the allocated page bit offset in bitmap.
 */
STATIC_INLINE bool
file_partsect_alloc (FILE_PARTIAL_SECTOR * partsect, VPID * vpid_out, int *offset_out)
{
  int offset_to_zero = bit64_count_trailing_ones (partsect->page_bitmap);

  if (offset_to_zero >= FILE_ALLOC_BITMAP_NBITS)
    {
      assert (file_partsect_is_full (partsect));
      return false;
    }

  assert (offset_to_zero >= 0);

  file_partsect_set_bit (partsect, offset_to_zero);
  if (offset_out)
    {
      *offset_out = offset_to_zero;
    }

  if (vpid_out)
    {
      vpid_out->volid = partsect->vsid.volid;
      vpid_out->pageid = SECTOR_FIRST_PAGEID (partsect->vsid.sectid) + offset_to_zero;
    }

  return true;
}

/*
 * file_rv_partsect_update () - Set/clear bit in partial sector for recovery.
 *
 * return	 : Error code
 * thread_p (in) : Thread entry
 * rcv (in)	 : Recovery data
 * set (in)	 : True if bit should be set, false if bit should be cleared
 */
static int
file_rv_partsect_update (THREAD_ENTRY * thread_p, LOG_RCV * rcv, bool set)
{
  PAGE_PTR page_ftab = rcv->pgptr;
  FILE_PARTIAL_SECTOR *partsect;
  int offset;

  assert (page_ftab != NULL);
  assert (rcv->offset >= 0 && rcv->offset < DB_PAGESIZE);

  offset = *(int *) rcv->data;
  assert (rcv->length == sizeof (offset));

  partsect = (FILE_PARTIAL_SECTOR *) (page_ftab + rcv->offset);
  if (set)
    {
      file_partsect_set_bit (partsect, offset);
    }
  else
    {
      file_partsect_clear_bit (partsect, offset);
    }

  file_log ("file_rv_partsect_update",
	    "recovery partial sector update in page %d|%d prev_lsa %lld|%d: "
	    "%s bit at offset %d, partial sector offset %d \n"
	    FILE_PARTSECT_MSG ("partsect after rcv"),
	    PGBUF_PAGE_VPID_AS_ARGS (rcv->pgptr),
	    PGBUF_PAGE_LSA_AS_ARGS (rcv->pgptr), set ? "set" : "clear",
	    offset, rcv->offset, FILE_PARTSECT_AS_ARGS (partsect));

  pgbuf_set_dirty (thread_p, page_ftab, DONT_FREE);
  return NO_ERROR;
}

/*
 * file_rv_partsect_set () - Set bit in partial sector for recovery.
 *
 * return	 : Error code
 * thread_p (in) : Thread entry
 * rcv (in)	 : Recovery data
 */
int
file_rv_partsect_set (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  return file_rv_partsect_update (thread_p, rcv, true);
}

/*
 * file_rv_partsect_clear () - Clear bit in partial sector for recovery.
 *
 * return	 : Error code
 * thread_p (in) : Thread entry
 * rcv (in)	 : Recovery data
 */
int
file_rv_partsect_clear (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  return file_rv_partsect_update (thread_p, rcv, false);
}

/************************************************************************/
/* Utility functions.                                                   */
/************************************************************************/

/*
 * file_compare_vsids () - Compare two sector identifiers.
 *
 * return      : 1 if first sector is bigger, -1 if first sector is smaller and 0 if sector ids are equal
 * first (in)  : first sector id
 * second (in) : second sector id
 */
static int
file_compare_vsids (const void *first, const void *second)
{
  VSID *first_vsid = (VSID *) first;
  VSID *second_vsid = (VSID *) second;

  if (first_vsid->volid > second_vsid->volid)
    {
      return 1;
    }
  else if (first_vsid->volid < second_vsid->volid)
    {
      return -1;
    }
  return (int) (first_vsid->sectid - second_vsid->sectid);
}

/*
 * file_compare_vpids () - Compare two page identifiers.
 *
 * return      : 1 if first page is bigger, -1 if first page is smaller and 0 if page ids are equal
 * first (in)  : first page id
 * second (in) : second page id
 *
 * note: we need to ignore FILE_USER_PAGE_MARK_DELETED flag.
 */
static int
file_compare_vpids (const void *first, const void *second)
{
  VPID first_vpid = *(VPID *) first;
  VPID second_vpid = *(VPID *) second;

  if (first_vpid.volid == second_vpid.volid)
    {
      FILE_USER_PAGE_CLEAR_MARK_DELETED (&first_vpid);
      FILE_USER_PAGE_CLEAR_MARK_DELETED (&second_vpid);
      return first_vpid.pageid - second_vpid.pageid;
    }
  else
    {
      return first_vpid.volid - second_vpid.volid;
    }
}

/*
 * file_compare_vfids () - Compare two file identifiers.
 *
 * return      : positive if first file is bigger, negative if first file is smaller and 0 if page ids are equal
 * first (in)  : first file id
 * second (in) : second file id
 *
 * note: we need to ignore FILE_USER_PAGE_MARK_DELETED flag.
 */
static int
file_compare_vfids (const void *first, const void *second)
{
  VFID *first_vfid = (VFID *) first;
  VFID *second_vfid = (VFID *) second;

  if (first_vfid->volid > second_vfid->volid)
    {
      return 1;
    }
  if (first_vfid->volid < second_vfid->volid)
    {
      return -1;
    }
  return first_vfid->fileid - second_vfid->fileid;
}

/*
 * file_compare_track_items () - compare two file tracker items
 *
 * return      : positive if first item is bigger, negative if first item is smaller, 0 if items are equal
 * first (in)  : first item
 * second (in) : second item
 */
static int
file_compare_track_items (const void *first, const void *second)
{
  FILE_TRACK_ITEM *first_item = (FILE_TRACK_ITEM *) first;
  FILE_TRACK_ITEM *second_item = (FILE_TRACK_ITEM *) second;

  if (first_item->volid > second_item->volid)
    {
      return 1;
    }
  if (first_item->volid < second_item->volid)
    {
      return -1;
    }
  return first_item->fileid - second_item->fileid;
}

/*
 * file_type_to_string () - Get a string of the given file type
 *   return: string of the file type
 *   fstruct_type(in): The type of the structure
 */
static const char *
file_type_to_string (FILE_TYPE fstruct_type)
{
  switch (fstruct_type)
    {
    case FILE_TRACKER:
      return "TRACKER";
    case FILE_HEAP:
      return "HEAP";
    case FILE_MULTIPAGE_OBJECT_HEAP:
      return "MULTIPAGE_OBJECT_HEAP";
    case FILE_BTREE:
      return "BTREE";
    case FILE_BTREE_OVERFLOW_KEY:
      return "BTREE_OVERFLOW_KEY";
    case FILE_EXTENDIBLE_HASH:
      return "HASH";
    case FILE_EXTENDIBLE_HASH_DIRECTORY:
      return "HASH_DIRECTORY";
    case FILE_CATALOG:
      return "CATALOG";
    case FILE_DROPPED_FILES:
      return "DROPPED FILES";
    case FILE_VACUUM_DATA:
      return "VACUUM DATA";
    case FILE_QUERY_AREA:
      return "QUERY_AREA";
    case FILE_TEMP:
      return "TEMPORARILY";
    case FILE_UNKNOWN_TYPE:
      return "UNKNOWN";
    case FILE_HEAP_REUSE_SLOTS:
      return "HEAP_REUSE_SLOTS";
    }
  return "UNKNOWN";
}

static void
file_print_name_of_class (THREAD_ENTRY * thread_p, FILE * fp, const OID * class_oid_p)
{
  char *class_name_p = NULL;

  if (!OID_ISNULL (class_oid_p))
    {
      class_name_p = heap_get_class_name (thread_p, class_oid_p);
      fprintf (fp, "CLASS_OID: %5d|%10d|%5d (%s)", OID_AS_ARGS (class_oid_p),
	       class_name_p != NULL ? class_name_p : "*UNKNOWN-CLASS*");
      if (class_name_p != NULL)
	{
	  free_and_init (class_name_p);
	}
    }
}

/************************************************************************/
/* File manipulation section.                                           */
/************************************************************************/

/*
 * file_create_with_npages () - Create a permanent file big enough to store a number of pages.
 *
 * return	  : Error code
 * thread_p (in)  : Thread entry
 * file_type (in) : File type
 * npages (in)	  : Number of pages.
 * des (in)	  : File descriptor.
 * vfid (out)	  : File identifier.
 */
int
file_create_with_npages (THREAD_ENTRY * thread_p, FILE_TYPE file_type, int npages, FILE_DESCRIPTORS * des, VFID * vfid)
{
  FILE_TABLESPACE tablespace;

  assert (file_type != FILE_TEMP);

  assert (npages > 0);

  FILE_TABLESPACE_FOR_PERM_NPAGES (&tablespace, npages);

  return file_create (thread_p, file_type, &tablespace, des, false, false, vfid);
}

/*
 * file_create_heap () - Create heap file (permanent, not numerable)
 *
 * return	  : Error code
 * thread_p (in)  : Thread entry
 * npages (in)	  : Number of pages
 * des_heap (in)  : Heap file descriptor
 * reuse_oid (in) : Reuse slots true or false
 * vfid (out)	  : File identifier
 *
 * todo: add tablespace.
 */
int
file_create_heap (THREAD_ENTRY * thread_p, FILE_HEAP_DES * des_heap, bool reuse_oid, VFID * vfid)
{
  FILE_TYPE file_type = reuse_oid ? FILE_HEAP_REUSE_SLOTS : FILE_HEAP;

  assert (des_heap != NULL);

  return file_create_with_npages (thread_p, file_type, 1, (FILE_DESCRIPTORS *) des_heap, vfid);
}

/*
 * file_create_temp_internal () - common function to create files for temporary purpose. always try to use a cached
 *                                temporary file first. if there is no cached entry, create a new file.
 *                                in the end save the temp cache entry in transaction list of temporary files.
 *
 * return            : error code
 * thread_p (in)     : thread entry
 * npages (in)       : desired number of pages (ignored when taking file from cache)
 * ftype (in)        : file type
 * is_numerable (in) : true if file must be numerable, false if should be regular file
 * vfid_out (out)    : VFID of file (obtained from cache or created).
 */
STATIC_INLINE int
file_create_temp_internal (THREAD_ENTRY * thread_p, int npages, FILE_TYPE ftype, bool is_numerable, VFID * vfid_out)
{
  FILE_TABLESPACE tablespace;
  FILE_TEMPCACHE_ENTRY *tempcache_entry = NULL;
  int error_code = NO_ERROR;

  assert (npages > 0);

  error_code = file_tempcache_get (thread_p, ftype, is_numerable, &tempcache_entry);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }
  assert (tempcache_entry != NULL);
  assert (tempcache_entry->ftype == ftype);

  if (VFID_ISNULL (&tempcache_entry->vfid))
    {
      FILE_TABLESPACE_FOR_TEMP_NPAGES (&tablespace, npages);
      error_code = file_create (thread_p, ftype, &tablespace, NULL, true, is_numerable, vfid_out);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  file_tempcache_retire_entry (tempcache_entry);
	  return error_code;
	}
      tempcache_entry->vfid = *vfid_out;
    }
  else
    {
      /* we use the cached file */
      /* what about the number of pages? */
      *vfid_out = tempcache_entry->vfid;
    }

  /* save to transaction temporary file list */
  file_tempcache_push_tran_file (thread_p, tempcache_entry);
  return NO_ERROR;
}

/*
 * file_create_temp () - Create a temporary file.
 *
 * return	 : Error code
 * thread_p (in) : Thread entry
 * npages (in)	 : Number of pages
 * vfid (out)	 : File identifier
 */
int
file_create_temp (THREAD_ENTRY * thread_p, int npages, VFID * vfid)
{
  return file_create_temp_internal (thread_p, npages, FILE_TEMP, false, vfid);
}

/*
 * file_create_temp_numerable () - Create a temporary file with numerable property.
 *
 * return	 : Error code
 * thread_p (in) : Thread entry
 * npages (in)	 : Number of pages
 * vfid (out)	 : File identifier
 */
int
file_create_temp_numerable (THREAD_ENTRY * thread_p, int npages, VFID * vfid)
{
  return file_create_temp_internal (thread_p, npages, FILE_TEMP, true, vfid);
}

/*
 * file_create_query_area () - Create a query area file (temporary, not numerable).
 *
 * return	 : Error code
 * thread_p (in) : Thread entry
 * vfid (out)	 : File identifier
 */
int
file_create_query_area (THREAD_ENTRY * thread_p, VFID * vfid)
{
  return file_create_temp_internal (thread_p, 1, FILE_QUERY_AREA, false, vfid);
}

/*
 * file_create_ehash () - Create a permanent or temporary file for extensible hash table. This file will have the
 *			  numerable property.
 *
 * return	  : Error code
 * thread_p (in)  : Thread entry
 * npages (in)	  : Number of pages
 * is_tmp (in)	  : True if is temporary, false if is permanent
 * des_ehash (in) : Extensible hash descriptor.
 * vfid (out)	  : File identifier.
 */
int
file_create_ehash (THREAD_ENTRY * thread_p, int npages, bool is_tmp, FILE_EHASH_DES * des_ehash, VFID * vfid)
{
  FILE_TABLESPACE tablespace;

  assert (npages > 0);

  /* todo: use temporary file cache? */

  FILE_TABLESPACE_FOR_TEMP_NPAGES (&tablespace, npages);
  return file_create (thread_p, FILE_EXTENDIBLE_HASH, &tablespace, (FILE_DESCRIPTORS *) des_ehash, is_tmp, true, vfid);
}

/*
 * file_create_ehash_dir () - Create a permanent or temporary file for extensible hash directory. This file will have
 *			      the numerable property.
 *
 * return	  : Error code
 * thread_p (in)  : Thread entry
 * npages (in)	  : Number of pages
 * is_tmp (in)	  : True if is temporary, false if is permanent
 * des_ehash (in) : Extensible hash descriptor.
 * vfid (out)	  : File identifier.
 */
int
file_create_ehash_dir (THREAD_ENTRY * thread_p, int npages, bool is_tmp, FILE_EHASH_DES * des_ehash, VFID * vfid)
{
  FILE_TABLESPACE tablespace;

  assert (npages > 0);

  /* todo: use temporary file cache? */

  FILE_TABLESPACE_FOR_TEMP_NPAGES (&tablespace, npages);
  return file_create (thread_p, FILE_EXTENDIBLE_HASH_DIRECTORY, &tablespace, (FILE_DESCRIPTORS *) des_ehash, is_tmp,
		      true, vfid);
}

/*
 * file_create () - Create a new file.
 *
 * return	     : Error code.
 * thread_p (in)     : Thread entry.
 * file_type (in)    : File type.
 * tablespace (in)   : File table space.
 * des (in)	     : File descriptor (based on file type).
 * is_temp (in)	     : True if file should be temporary.
 * is_numerable (in) : True if file should be numerable.
 * vfid (out)	     : Output new file identifier.
 */
int
file_create (THREAD_ENTRY * thread_p, FILE_TYPE file_type,
	     FILE_TABLESPACE * tablespace, FILE_DESCRIPTORS * des, bool is_temp, bool is_numerable, VFID * vfid)
{
  int total_size;
  int n_sectors;
  VSID *vsids_reserved = NULL;
  DB_VOLPURPOSE volpurpose = DISK_UNKNOWN_PURPOSE;
  VSID *vsid_iter = NULL;
  INT16 size = 0;
  VOLID volid_last_expand;

  /* File header vars */
  VPID vpid_fhead = VPID_INITIALIZER;
  PAGE_PTR page_fhead = NULL;
  FILE_HEADER *fhead = NULL;

  /* File table vars */
  int max_size_ftab;
  FILE_PARTIAL_SECTOR *partsect_ftab = NULL;
  VPID vpid_ftab = VPID_INITIALIZER;
  PAGE_PTR page_ftab = NULL;
  bool found_vfid_page = false;
  INT16 offset_ftab = 0;

  /* Partial table. */
  FILE_PARTIAL_SECTOR partsect;

  /* File extensible data */
  FILE_EXTENSIBLE_DATA *extdata_part_ftab = NULL;
  FILE_EXTENSIBLE_DATA *extdata_full_ftab = NULL;
  FILE_EXTENSIBLE_DATA *extdata_user_page_ftab = NULL;

  /* Recovery */
  bool is_sysop_started = false;
  bool do_logging = !is_temp;

  int error_code = NO_ERROR;

  assert (tablespace->initial_size > 0);
  assert (file_type != FILE_TEMP || is_temp);
  assert (vfid != NULL);

  /* Estimate the required size including file header & tables. */
  total_size = tablespace->initial_size;

  if (!is_numerable)
    {
      /* Partial & full sectors tables.
       * The worst case is when all sectors are partially allocated. The required size will be sizeof (SECTOR_ID) and
       * the size of bitmap (in bytes), which counts to 16 bytes, for each sector. The sector size will be at least
       * 4k * 64 => 256KB. So we need to increase the total size of file with 16 bytes per every 256KB, or 1 byte for
       * each 16KB. Then we need again to consider the additional space used by table (another 1 byte for 16KB x 16 KB).
       * This of course translates to an infinite series and we want to simplify that. So, adding 1 byte for each 8KB
       * from the start should cover any space required by file tables.
       */
      max_size_ftab = total_size / 8 / 1024;
      total_size += max_size_ftab;
    }
  else
    {
      /* Partial & full sectors tables + page table.
       * By applying the same logic above, we consider the worst case (which is impossible actually) all sectors are
       * partially allocated (16 bytes per sector) and all pages are allocated by user (64 * 8 byte per sector). This
       * totals to 528 bytes for each 256KB bytes of data. By doubling the estimated size of tables, we need an extra
       * 1 byte for each 256KB / 528 / 2, which is equivalent to 1 byte for each 8/33 KB.
       */
      max_size_ftab = total_size * 33 / 8 / 1024;
      total_size += max_size_ftab;
    }

  /* convert the disk size to number of sectors. */
  n_sectors = CEIL_PTVDIV (total_size, DB_SECTORSIZE);
  assert (n_sectors > 0);
  /* allocate a buffer to store all reserved sectors */
  vsids_reserved = (VSID *) db_private_alloc (thread_p, n_sectors * sizeof (VSID));
  if (vsids_reserved == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, n_sectors * sizeof (VSID));
      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      goto exit;
    }

  if (do_logging)
    {
      /* start a system operation */
      log_sysop_start (thread_p);
      is_sysop_started = true;
    }

  /* reserve sectors on disk */
  volpurpose = is_temp ? DB_TEMPORARY_DATA_PURPOSE : DB_PERMANENT_DATA_PURPOSE;

  file_log ("file_create",
	    "create %s file \n\t%s \n\t%s \n" FILE_TABLESPACE_MSG
	    " \tnsectors = %d", file_type_to_string (file_type),
	    FILE_PERM_TEMP_STRING (is_temp),
	    FILE_NUMERABLE_REGULAR_STRING (is_numerable), FILE_TABLESPACE_AS_ARGS (tablespace), n_sectors);

  error_code = disk_reserve_sectors (thread_p, volpurpose, NULL_VOLID, n_sectors, vsids_reserved);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto exit;
    }
  /* found enough sectors to reserve */

  /* sort sectors by VSID. but before sorting, remember last volume ID used for reservations. */
  volid_last_expand = vsids_reserved[n_sectors - 1].volid;
  qsort (vsids_reserved, n_sectors, sizeof (VSID), file_compare_vsids);

  /* decide on what page to use as file header page (which is going to decide the VFID also). */
#if defined (SERVER_MODE)
  if (file_type == FILE_BTREE || file_type == FILE_HEAP || file_type == FILE_HEAP_REUSE_SLOTS)
    {
      /* we need to consider dropped files in vacuum's list. If we create a file with a duplicate VFID, we can run
       * into problems. */
      VSID *vsid_iter = vsids_reserved;
      VFID vfid_iter;
      VFID found_vfid = VFID_INITIALIZER;
      MVCCID tran_mvccid = logtb_get_current_mvccid (thread_p);
      /* we really have to have the VFID in the same volume as the first allocated page. this means we cannot change
       * volume when we look for a valid VFID. */
      VOLID first_volid = vsids_reserved[0].volid;

      for (vsid_iter = vsids_reserved;
	   vsid_iter < vsids_reserved + n_sectors && VFID_ISNULL (&found_vfid)
	   && vsid_iter->volid == first_volid; vsid_iter++)
	{
	  vfid_iter.volid = vsid_iter->volid;
	  for (vfid_iter.fileid = SECTOR_FIRST_PAGEID (vsid_iter->sectid);
	       vfid_iter.fileid <= SECTOR_LAST_PAGEID (vsid_iter->sectid); vfid_iter.fileid++)
	    {
	      if (!vacuum_is_file_dropped (thread_p, &vfid_iter, tran_mvccid))
		{
		  /* Good we found a file ID that is not considered dropped. */
		  found_vfid = vfid_iter;
		  break;
		}
	    }
	}
      if (VFID_ISNULL (&found_vfid))
	{
	  /* this is ridiculous. */
	  assert_release (false);
	  error_code = ER_FAILED;
	  goto exit;
	}
      *vfid = found_vfid;
    }
  else
#endif /* SERVER_MODE */
    {
      vfid->volid = vsids_reserved->volid;
      vfid->fileid = SECTOR_FIRST_PAGEID (vsids_reserved->sectid);
    }

  assert (!VFID_ISNULL (vfid));
  vpid_fhead.volid = vfid->volid;
  vpid_fhead.pageid = vfid->fileid;

  file_log ("file_create", "chose VFID = %d|%d.", VFID_AS_ARGS (vfid));

  page_fhead = pgbuf_fix (thread_p, &vpid_fhead, NEW_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (page_fhead == NULL)
    {
      ASSERT_ERROR ();
      goto exit;
    }

  memset (page_fhead, DB_PAGESIZE, 0);
  pgbuf_set_page_ptype (thread_p, page_fhead, PAGE_FTAB);
  fhead = (FILE_HEADER *) page_fhead;

  /* initialize header */
  fhead->self = *vfid;
  fhead->tablespace = *tablespace;
  if (des != NULL)
    {
      fhead->descriptor = *des;
    }
  fhead->time_creation = time (NULL);
  fhead->type = file_type;
  fhead->file_flags = 0;
  if (is_numerable)
    {
      fhead->file_flags |= FILE_FLAG_NUMERABLE;
    }
  if (is_temp)
    {
      fhead->file_flags |= FILE_FLAG_TEMPORARY;
    }

  fhead->volid_last_expand = volid_last_expand;
  VPID_SET_NULL (&fhead->vpid_last_temp_alloc);
  fhead->offset_to_last_temp_alloc = NULL_OFFSET;
  VPID_SET_NULL (&fhead->vpid_last_user_page_ftab);
  VPID_SET_NULL (&fhead->vpid_sticky_first);

  fhead->n_page_total = 0;
  fhead->n_page_user = 0;
  fhead->n_page_ftab = 1;	/* file header */
  fhead->n_page_free = 0;
  fhead->n_page_mark_delete = 0;

  fhead->n_sector_total = 0;
  fhead->n_sector_partial = 0;
  fhead->n_sector_full = 0;
  fhead->n_sector_empty = 0;

  fhead->offset_to_partial_ftab = NULL_OFFSET;
  fhead->offset_to_full_ftab = NULL_OFFSET;
  fhead->offset_to_user_page_ftab = NULL_OFFSET;

  fhead->reserved0 = fhead->reserved1 = fhead->reserved2 = fhead->reserved3 = 0;

  /* start with a negative empty sector (because we have allocated header). */
  fhead->n_sector_empty--;

  /* start creating required file tables.
   * file tables depend of the properties of permanent/temporary and numerable.
   * temporary files do not use full table, only partial table. permanent files use both partial and full tables.
   * numerable files (which can be both permanent or temporary) also require user page table.
   */
  offset_ftab = FILE_HEADER_ALIGNED_SIZE;
  if (is_numerable)
    {
      if (is_temp)
	{
	  /* split the header page space into: 1/16 for partial table and 15/16 for user page table */
	  fhead->offset_to_partial_ftab = offset_ftab;
	  assert ((INT16) DB_ALIGN (fhead->offset_to_partial_ftab, MAX_ALIGNMENT) == fhead->offset_to_partial_ftab);
	  size = (DB_PAGESIZE - offset_ftab) / 16;
	  FILE_HEADER_GET_PART_FTAB (fhead, extdata_part_ftab);
	  file_extdata_init (sizeof (FILE_PARTIAL_SECTOR), size, extdata_part_ftab);

	  offset_ftab += file_extdata_max_size (extdata_part_ftab);
	}
      else
	{
	  /* split the header space into three: 1/32 for each partial and full sector tables and the rest (15/16) for
	   * user page table. */

	  /* partial table. */
	  fhead->offset_to_partial_ftab = offset_ftab;
	  assert ((INT16) DB_ALIGN (fhead->offset_to_partial_ftab, MAX_ALIGNMENT) == fhead->offset_to_partial_ftab);
	  size = (DB_PAGESIZE - offset_ftab) / 32;
	  FILE_HEADER_GET_PART_FTAB (fhead, extdata_part_ftab);
	  file_extdata_init (sizeof (FILE_PARTIAL_SECTOR), size, extdata_part_ftab);

	  /* full table. */
	  offset_ftab += file_extdata_max_size (extdata_part_ftab);
	  fhead->offset_to_full_ftab = offset_ftab;
	  assert ((INT16) DB_ALIGN (fhead->offset_to_full_ftab, MAX_ALIGNMENT) == fhead->offset_to_full_ftab);
	  FILE_HEADER_GET_FULL_FTAB (fhead, extdata_full_ftab);
	  file_extdata_init (sizeof (VSID), size, extdata_full_ftab);

	  offset_ftab += file_extdata_max_size (extdata_full_ftab);
	}

      /* user page table - consume remaining space. */
      fhead->offset_to_user_page_ftab = offset_ftab;
      assert ((INT16) DB_ALIGN (fhead->offset_to_user_page_ftab, MAX_ALIGNMENT) == fhead->offset_to_user_page_ftab);
      size = DB_PAGESIZE - offset_ftab;
      FILE_HEADER_GET_USER_PAGE_FTAB (fhead, extdata_user_page_ftab);
      file_extdata_init (sizeof (VPID), size, extdata_user_page_ftab);
    }
  else
    {
      if (is_temp)
	{
	  /* keep only partial table. */
	  fhead->offset_to_partial_ftab = offset_ftab;
	  assert ((INT16) DB_ALIGN (fhead->offset_to_partial_ftab, MAX_ALIGNMENT) == fhead->offset_to_partial_ftab);
	  size = DB_PAGESIZE - offset_ftab;
	  FILE_HEADER_GET_PART_FTAB (fhead, extdata_part_ftab);
	  file_extdata_init (sizeof (FILE_PARTIAL_SECTOR), size, extdata_part_ftab);
	}
      else
	{
	  /* split the header space into two: half for partial table and half for full table. */

	  /* partial table. */
	  fhead->offset_to_partial_ftab = offset_ftab;
	  assert ((INT16) DB_ALIGN (fhead->offset_to_partial_ftab, MAX_ALIGNMENT) == fhead->offset_to_partial_ftab);
	  size = (DB_PAGESIZE - offset_ftab) / 2;
	  FILE_HEADER_GET_PART_FTAB (fhead, extdata_part_ftab);
	  file_extdata_init (sizeof (FILE_PARTIAL_SECTOR), size, extdata_part_ftab);

	  /* full table. */
	  offset_ftab += file_extdata_max_size (extdata_part_ftab);
	  fhead->offset_to_full_ftab = offset_ftab;
	  assert ((INT16) DB_ALIGN (fhead->offset_to_full_ftab, MAX_ALIGNMENT) == fhead->offset_to_full_ftab);
	  size = DB_PAGESIZE - offset_ftab;
	  FILE_HEADER_GET_FULL_FTAB (fhead, extdata_full_ftab);
	  file_extdata_init (sizeof (VSID), size, extdata_full_ftab);
	}
    }
  /* all required tables are created */
  /* all files must have partial table */
  assert (fhead->offset_to_partial_ftab != NULL_OFFSET);

  /* start populating partial table */
  /* partial sectors are initially added to the table in header. if the file is really big, and the table needs to
   * extend on other pages, we will keep track of pages/sectors used for file table using extdata_part_ftab.
   */
  FILE_HEADER_GET_PART_FTAB (fhead, extdata_part_ftab);
  for (vsid_iter = vsids_reserved; vsid_iter < vsids_reserved + n_sectors; vsid_iter++)
    {
      if (file_extdata_is_full (extdata_part_ftab))
	{
	  /* a new page for file table is required */

	  /* allocate a page from partial table. */
	  if (partsect_ftab == NULL)
	    {
	      /* This is first page. */
	      assert (!file_extdata_is_empty (extdata_part_ftab));
	      partsect_ftab = (FILE_PARTIAL_SECTOR *) file_extdata_start (extdata_part_ftab);
	      vpid_ftab.pageid = SECTOR_FIRST_PAGEID (partsect_ftab->vsid.sectid);
	      vpid_ftab.volid = partsect_ftab->vsid.volid;

	      if (!VSID_IS_SECTOR_OF_VPID (&partsect_ftab->vsid, &vpid_fhead))
		{
		  /* another non-empty sector. */
		  fhead->n_sector_empty--;
		}
	      else
		{
		  /* this non-empty sector was already counted */
		}
	    }
	  else if (file_partsect_is_full (partsect_ftab))
	    {
	      /* move to next partial sector. */
	      partsect_ftab++;
	      if ((void *) partsect_ftab >= file_extdata_end (extdata_part_ftab))
		{
		  /* This is not possible! */
		  assert_release (false);
		  error_code = ER_FAILED;
		  goto exit;
		}
	      vpid_ftab.pageid = SECTOR_FIRST_PAGEID (partsect_ftab->vsid.sectid);
	      vpid_ftab.volid = partsect_ftab->vsid.volid;

	      if (!VSID_IS_SECTOR_OF_VPID (&partsect_ftab->vsid, &vpid_fhead))
		{
		  /* another non-empty sector. */
		  fhead->n_sector_empty--;
		}
	      else
		{
		  /* this non-empty sector was already counted */
		}

	      fhead->n_sector_full++;
	    }
	  else
	    {
	      vpid_ftab.pageid++;
	    }

	  if (VPID_EQ (&vpid_fhead, &vpid_ftab))
	    {
	      /* Go to next page. This can't be last page in sector, because the sector bitmap would have been full */
	      assert (file_partsect_is_bit_set (partsect_ftab,
						file_partsect_pageid_to_offset (partsect_ftab, vpid_fhead.pageid)));
	      vpid_ftab.pageid++;
	      found_vfid_page = true;
	    }
	  /* Set bit in sector bitmap */
	  file_partsect_set_bit (partsect_ftab, file_partsect_pageid_to_offset (partsect_ftab, vpid_ftab.pageid));

	  /* Save link in previous page. */
	  extdata_part_ftab->vpid_next = vpid_ftab;
	  if (page_ftab != NULL)
	    {
	      if (do_logging)
		{
		  pgbuf_log_new_page (thread_p, page_ftab, file_extdata_size (extdata_part_ftab), PAGE_FTAB);
		}
	      pgbuf_set_dirty (thread_p, page_ftab, FREE);
	    }
	  page_ftab = pgbuf_fix (thread_p, &vpid_ftab, NEW_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
	  if (page_ftab == NULL)
	    {
	      ASSERT_ERROR_AND_SET (error_code);
	      goto exit;
	    }
	  pgbuf_set_page_ptype (thread_p, page_ftab, PAGE_FTAB);
	  memset (page_ftab, DB_PAGESIZE, 0);
	  extdata_part_ftab = (FILE_EXTENSIBLE_DATA *) page_ftab;
	  file_extdata_init (sizeof (FILE_PARTIAL_SECTOR), DB_PAGESIZE, extdata_part_ftab);

	  fhead->n_page_ftab++;
	}
      assert (!file_extdata_is_full (extdata_part_ftab));

      partsect.vsid = *vsid_iter;
      partsect.page_bitmap = FILE_EMPTY_PAGE_BITMAP;
      if (partsect.vsid.sectid == SECTOR_FROM_PAGEID (vpid_fhead.pageid) && partsect.vsid.volid == vpid_fhead.volid)
	{
	  /* Set bit for file header page. */
	  file_partsect_set_bit (&partsect, file_partsect_pageid_to_offset (&partsect, vpid_fhead.pageid));
	}
      file_extdata_append (extdata_part_ftab, &partsect);
    }

  if (page_ftab != NULL)
    {
      if (do_logging)
	{
	  pgbuf_log_new_page (thread_p, page_ftab, file_extdata_size (extdata_part_ftab), PAGE_FTAB);
	  pgbuf_unfix_and_init (thread_p, page_ftab);
	}
      else
	{
	  pgbuf_set_dirty_and_free (thread_p, page_ftab);
	}
    }

  if (partsect_ftab == NULL)
    {
      /* all partial sectors were fitted in header page */
      assert (fhead->n_sector_full == 0);
    }
  else
    {
      if (file_partsect_is_full (partsect_ftab))
	{
	  partsect_ftab++;
	  fhead->n_sector_full++;
	}
      if (!is_temp && fhead->n_sector_full > 0)
	{
	  /* move sectors fully used by file table to full table */
	  int i;
	  FILE_PARTIAL_SECTOR *partsect_iter;
	  FILE_HEADER_GET_PART_FTAB (fhead, extdata_part_ftab);
	  FILE_HEADER_GET_FULL_FTAB (fhead, extdata_full_ftab);

	  for (i = 0; i < fhead->n_sector_full; i++)
	    {
	      partsect_iter = (FILE_PARTIAL_SECTOR *) file_extdata_at (extdata_part_ftab, i);
	      if (file_extdata_is_full (extdata_full_ftab))
		{
		  /* Not possible. */
		  assert_release (false);
		  error_code = ER_FAILED;
		  goto exit;
		}
	      file_extdata_append (extdata_full_ftab, &partsect_iter->vsid);
	    }
	  /* Remove full sectors from partial table. */
	  file_extdata_remove_at (extdata_part_ftab, 0, fhead->n_sector_full);
	  assert (fhead->n_sector_full == file_extdata_item_count (extdata_full_ftab));
	}
    }

  if (is_temp)
    {
      /* temporary files do not keep separate tables for partial and full sectors. just set last allocation location. */
      VPID_COPY (&fhead->vpid_last_temp_alloc, &vpid_fhead);
      fhead->offset_to_last_temp_alloc = fhead->n_sector_full;
    }

  if (is_numerable)
    {
      /* set last user page table VPID to header */
      fhead->vpid_last_user_page_ftab = vpid_fhead;
    }

  /* set all stats */
  /* sector stats; full stats already counted, empty stats are negative (we need to add partial sectors) */
  fhead->n_sector_total = n_sectors;
  fhead->n_sector_partial = fhead->n_sector_total - fhead->n_sector_full;
  fhead->n_sector_empty += fhead->n_sector_partial;
  /* page stats; file table pages already counted; user pages remain 0 */
  fhead->n_page_total = fhead->n_sector_total * DISK_SECTOR_NPAGES;
  fhead->n_page_free = fhead->n_page_total - fhead->n_page_ftab;

  /* File header ready. */
  file_header_sanity_check (fhead);

  file_log ("file_create", "finished creating file. \n" FILE_HEAD_FULL_MSG, FILE_HEAD_FULL_AS_ARGS (fhead));

  if (do_logging)
    {
      pgbuf_log_new_page (thread_p, page_fhead, DB_PAGESIZE, PAGE_FTAB);
      pgbuf_unfix_and_init (thread_p, page_fhead);
    }
  else
    {
      pgbuf_set_dirty_and_free (thread_p, page_fhead);
    }

  if (!is_temp && file_type != FILE_TRACKER)
    {
      /* add to tracker */
      error_code = file_tracker_register (thread_p, vfid, file_type, NULL);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto exit;
	}
    }

  /* Fall through to exit */
exit:

  if (page_ftab != NULL)
    {
      pgbuf_unfix (thread_p, page_ftab);
    }
  if (page_fhead != NULL)
    {
      pgbuf_unfix (thread_p, page_fhead);
    }

  if (is_sysop_started)
    {
      assert (do_logging);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  log_sysop_abort (thread_p);
	}
      else
	{
	  ASSERT_NO_ERROR ();
	  log_sysop_end_logical_undo (thread_p, RVFL_DESTROY, NULL, sizeof (*vfid), (char *) vfid);
	}
    }

  if (vsids_reserved != NULL)
    {
      /* Deallocate reserved_sectors */
      db_private_free (thread_p, vsids_reserved);
    }

  return error_code;
}

/*
 * file_table_collect_vsid () - Function callable by file_extdata_apply_funcs. Used to collect sector ID's from file
 *				tables.
 *
 * return	     : NO_ERROR
 * thread_p (in)     : Thread entry
 * item (in)	     : Item in extensible data (VSID or FILE_PARTIAL_SECTOR which starts with a VSID)
 * index_unused (in) : Unused
 * stop (out)	     : Unused
 * args (in/out)     : VSID collector
 */
static int
file_table_collect_vsid (THREAD_ENTRY * thread_p, const void *item, int index_unused, bool * stop, void *args)
{
  const VSID *vsid = (VSID *) item;
  FILE_VSID_COLLECTOR *collector = (FILE_VSID_COLLECTOR *) args;

  collector->vsids[collector->n_vsids++] = *vsid;

  return NO_ERROR;
}

/*
 * file_table_collect_all_vsids () - collect all sectors from file table
 *
 * return              : error code
 * thread_p (in)       : thread entry
 * page_fhead (in)     : file header page
 * collector_out (out) : output VSID collector
 */
STATIC_INLINE int
file_table_collect_all_vsids (THREAD_ENTRY * thread_p, PAGE_PTR page_fhead, FILE_VSID_COLLECTOR * collector_out)
{
  FILE_HEADER *fhead;
  FILE_EXTENSIBLE_DATA *extdata_ftab;
  int error_code = NO_ERROR;

  fhead = (FILE_HEADER *) page_fhead;

  collector_out->vsids = (VSID *) db_private_alloc (thread_p, fhead->n_sector_total * sizeof (VSID));
  if (collector_out->vsids == NULL)
    {
      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 1, fhead->n_sector_total * sizeof (VSID));
      return error_code;
    }
  collector_out->n_vsids = 0;

  /* Collect from partial table */
  FILE_HEADER_GET_PART_FTAB (fhead, extdata_ftab);
  error_code = file_extdata_apply_funcs (thread_p, extdata_ftab, NULL, NULL, file_table_collect_vsid, collector_out,
					 false, NULL, NULL);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      db_private_free_and_init (thread_p, collector_out->vsids);
      return error_code;
    }

  if (!FILE_IS_TEMPORARY (fhead))
    {
      /* Collect from full table. */
      FILE_HEADER_GET_FULL_FTAB (fhead, extdata_ftab);
      error_code = file_extdata_apply_funcs (thread_p, extdata_ftab, NULL, NULL, file_table_collect_vsid,
					     collector_out, false, NULL, NULL);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  db_private_free_and_init (thread_p, collector_out->vsids);
	  return error_code;
	}
    }

  if (collector_out->n_vsids != fhead->n_sector_total)
    {
      assert_release (false);
      db_private_free_and_init (thread_p, collector_out->vsids);
      return ER_FAILED;
    }

  qsort (collector_out->vsids, fhead->n_sector_total, sizeof (VSID), file_compare_vsids);

  return NO_ERROR;
}

/*
 * file_sector_map_dealloc () - FILE_EXTDATA_ITEM_FUNC to deallocate user pages
 *
 * return        : error code
 * thread_p (in) : thread entry
 * data (in)     : FILE_PARTIAL_SECTOR * or VSID *
 * index (in)    : unused
 * stop (in)     : unused
 * args (in)     : is_partial
 */
static int
file_sector_map_dealloc (THREAD_ENTRY * thread_p, const void *data, int index, bool * stop, void *args)
{
  bool is_partial = *(bool *) args;
  FILE_PARTIAL_SECTOR partsect = FILE_PARTIAL_SECTOR_INITIALIZER;
  int offset = 0;
  VPID vpid;
  PAGE_PTR page = NULL;
  int error_code = NO_ERROR;

  if (is_partial)
    {
      partsect = *(FILE_PARTIAL_SECTOR *) data;
    }
  else
    {
      partsect.vsid = *(VSID *) data;
    }

  vpid.volid = partsect.vsid.volid;
  for (offset = 0, vpid.pageid = SECTOR_FIRST_PAGEID (partsect.vsid.sectid); offset < DISK_SECTOR_NPAGES;
       offset++, vpid.pageid++)
    {
      if (is_partial && !file_partsect_is_bit_set (&partsect, offset))
	{
	  /* not allocated */
	  continue;
	}

      page = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
      if (page == NULL)
	{
	  ASSERT_ERROR_AND_SET (error_code);
	  return error_code;
	}

      if (pgbuf_get_page_ptype (thread_p, page) == PAGE_FTAB)
	{
	  /* table page, do not deallocate yet */
	  pgbuf_unfix_and_init (thread_p, page);
	  continue;
	}

      error_code = pgbuf_dealloc_page (thread_p, &page);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  pgbuf_unfix (thread_p, page);
	  return error_code;
	}
      assert (page == NULL);
    }

  return NO_ERROR;
}

/*
 * file_destroy () - Destroy file - unreserve all sectors used by file on disk.
 *
 * return	 : Error code
 * thread_p (in) : Thread entry
 * vfid (in)	 : File identifier
 */
int
file_destroy (THREAD_ENTRY * thread_p, const VFID * vfid)
{
  VPID vpid_fhead;
  PAGE_PTR page_fhead;
  FILE_HEADER *fhead = NULL;
  FILE_VSID_COLLECTOR vsid_collector;
  FILE_FTAB_COLLECTOR ftab_collector;
  DB_VOLPURPOSE volpurpose;
  int error_code = NO_ERROR;

  assert (vfid != NULL && !VFID_ISNULL (vfid));

  vsid_collector.vsids = NULL;
  ftab_collector.partsect_ftab = NULL;

  FILE_GET_HEADER_VPID (vfid, &vpid_fhead);
  page_fhead = pgbuf_fix (thread_p, &vpid_fhead, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (page_fhead == NULL)
    {
      assert_release (false);
      error_code = ER_FAILED;
      goto exit;
    }

  fhead = (FILE_HEADER *) page_fhead;
  file_header_sanity_check (fhead);

  assert (FILE_IS_TEMPORARY (fhead) || log_check_system_op_is_started (thread_p));

  error_code = file_table_collect_all_vsids (thread_p, page_fhead, &vsid_collector);
  if (error_code != NO_ERROR)
    {
      assert_release (false);
      goto exit;
    }
  volpurpose = FILE_IS_TEMPORARY (fhead) ? DB_TEMPORARY_DATA_PURPOSE : DB_PERMANENT_DATA_PURPOSE;

  file_log ("file_destroy",
	    "file %d|%d unreserve %d sectors \n" FILE_HEAD_FULL_MSG,
	    VFID_AS_ARGS (vfid), fhead->n_sector_total, FILE_HEAD_FULL_AS_ARGS (fhead));

  if (!FILE_IS_TEMPORARY (fhead))
    {
      /* we need to deallocate pages */
      FILE_EXTENSIBLE_DATA *extdata_ftab = NULL;
      bool is_partial;
      int iter_sects;
      int offset;
      VPID vpid_ftab;
      PAGE_PTR page_ftab = NULL;

      ftab_collector.npages = 0;
      ftab_collector.nsects = 0;
      ftab_collector.partsect_ftab =
	(FILE_PARTIAL_SECTOR *) db_private_alloc (thread_p, fhead->n_page_ftab * sizeof (FILE_PARTIAL_SECTOR));
      if (ftab_collector.partsect_ftab == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
		  fhead->n_page_ftab * sizeof (FILE_PARTIAL_SECTOR));
	  error_code = ER_OUT_OF_VIRTUAL_MEMORY;
	  goto exit;
	}

      FILE_HEADER_GET_PART_FTAB (fhead, extdata_ftab);
      is_partial = true;
      error_code =
	file_extdata_apply_funcs (thread_p, extdata_ftab, file_extdata_collect_ftab_pages, &ftab_collector,
				  file_sector_map_dealloc, &is_partial, true, NULL, NULL);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto exit;
	}

      FILE_HEADER_GET_FULL_FTAB (fhead, extdata_ftab);
      is_partial = false;
      error_code =
	file_extdata_apply_funcs (thread_p, extdata_ftab, file_extdata_collect_ftab_pages, &ftab_collector,
				  file_sector_map_dealloc, &is_partial, true, NULL, NULL);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto exit;
	}

      /* deallocate table pages - other than header page */
      for (iter_sects = 0; iter_sects < ftab_collector.nsects; iter_sects++)
	{
	  vpid_ftab.volid = ftab_collector.partsect_ftab[iter_sects].vsid.volid;
	  for (offset = 0,
	       vpid_ftab.pageid = SECTOR_FIRST_PAGEID (ftab_collector.partsect_ftab[iter_sects].vsid.sectid);
	       offset < DISK_SECTOR_NPAGES; offset++, vpid_ftab.pageid++)
	    {
	      if (file_partsect_is_bit_set (&ftab_collector.partsect_ftab[iter_sects], offset))
		{
		  page_ftab = pgbuf_fix (thread_p, &vpid_ftab, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
		  if (page_ftab == NULL)
		    {
		      ASSERT_ERROR_AND_SET (error_code);
		      goto exit;
		    }
		  error_code = pgbuf_dealloc_page (thread_p, &page_ftab);
		  if (error_code != NO_ERROR)
		    {
		      ASSERT_ERROR ();
		      pgbuf_unfix_and_init (thread_p, page_ftab);
		      goto exit;
		    }
		  assert (page_ftab == NULL);
		}
	    }
	}
      /* deallocate header page */
      error_code = pgbuf_dealloc_page (thread_p, &page_fhead);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto exit;
	}
      assert (page_fhead == NULL);
    }
  else
    {
      pgbuf_unfix_and_init (thread_p, page_fhead);
    }

  if (volpurpose == DB_PERMANENT_DATA_PURPOSE)
    {
      /* first remove from tracker */
      error_code = file_tracker_unregister (thread_p, vfid);
      if (error_code != NO_ERROR)
	{
	  assert_release (false);
	  goto exit;
	}
    }

  /* release occupied sectors on disk */
  error_code = disk_unreserve_ordered_sectors (thread_p, volpurpose, vsid_collector.n_vsids, vsid_collector.vsids);
  if (error_code != NO_ERROR)
    {
      assert_release (false);
      goto exit;
    }

  /* Done */
  assert (error_code == NO_ERROR);

exit:
  if (page_fhead != NULL)
    {
      pgbuf_unfix (thread_p, page_fhead);
    }
  if (vsid_collector.vsids != NULL)
    {
      db_private_free (thread_p, vsid_collector.vsids);
    }
  if (ftab_collector.partsect_ftab != NULL)
    {
      db_private_free (thread_p, ftab_collector.partsect_ftab);
    }
  return error_code;
}

/*
 * file_rv_destroy () - Recovery function used to destroy files.
 *
 * return	 : Error code.
 * thread_p (in) : Thread entry.
 * rcv (in)	 : Recovery data.
 *
 * NOTE: This can be used in one of two contexts:
 *	 1. Logical undo of create file. Should be under a system operation that ends with commit and compensate.
 *	 2. Run postpone for postponed file destroy. Again, this should be under a system operation, but it should end
 *	    with a commit and run postpone (of course).
 */
int
file_rv_destroy (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  VFID *vfid = (VFID *) rcv->data;
  int error_code = NO_ERROR;

  assert (sizeof (*vfid) == rcv->length);

  assert (log_check_system_op_is_started (thread_p));

  error_code = file_destroy (thread_p, vfid);
  if (error_code != NO_ERROR)
    {
      /* Not acceptable. */
      assert_release (false);
      return error_code;
    }

  return NO_ERROR;
}

/*
 * file_postpone_destroy () - Declare intention of destroying file. File will be destroyed on postpone phase, when the
 *			      transaction commit is confirmed. This is the usual (if not only) way of destroying
 *			      permanent files.
 *
 * return	 : Void
 * thread_p (in) : Thread entry
 * vfid (in)	 : File identifier
 */
void
file_postpone_destroy (THREAD_ENTRY * thread_p, const VFID * vfid)
{
  LOG_DATA_ADDR addr = LOG_DATA_ADDR_INITIALIZER;

#if !defined (NDEBUG)
  /* This should be used for permanent files! */
  {
    VPID vpid_fhead;
    PAGE_PTR page_fhead;
    FILE_HEADER *fhead;

    vpid_fhead.volid = vfid->volid;
    vpid_fhead.pageid = vfid->fileid;
    page_fhead = pgbuf_fix (thread_p, &vpid_fhead, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
    if (page_fhead == NULL)
      {
	ASSERT_ERROR ();
	return;
      }
    fhead = (FILE_HEADER *) page_fhead;
    assert (!FILE_IS_TEMPORARY (fhead));
    pgbuf_unfix_and_init (thread_p, page_fhead);
  }
#endif /* !NDEBUG */

  log_append_postpone (thread_p, RVFL_DESTROY, &addr, sizeof (*vfid), vfid);
}

/*
 * file_temp_retire () - retire temporary file. put it in cache is possible or destroy the file.
 *
 * return        : error code
 * thread_p (in) : thread entry
 * vfid (in)     : file identifier
 */
int
file_temp_retire (THREAD_ENTRY * thread_p, const VFID * vfid)
{
  FILE_TEMPCACHE_ENTRY *entry = file_tempcache_pop_tran_file (thread_p, vfid);
  bool save_interrupt;
  int error_code = NO_ERROR;

  if (entry == NULL)
    {
      assert_release (false);
    }
  else if (file_tempcache_put (thread_p, entry))
    {
      /* cached */
      return NO_ERROR;
    }

  /* was not cached. destroy */
  /* don't allow interrupt to avoid file leak */
  save_interrupt = thread_set_check_interrupt (thread_p, false);
  error_code = file_destroy (thread_p, vfid);
  thread_set_check_interrupt (thread_p, save_interrupt);
  if (error_code != NO_ERROR)
    {
      /* we should not have errors */
      assert_release (false);
    }
  return error_code;
}

/*
 * file_rv_perm_expand_undo () - Undo permanent file expansion.
 *
 * return	 : NO_ERROR
 * thread_p (in) : Thread entry
 * rcv (in)	 : Recovery data
 */
int
file_rv_perm_expand_undo (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  PAGE_PTR page_fhead = rcv->pgptr;
  FILE_HEADER *fhead;
  FILE_EXTENSIBLE_DATA *part_table;
  DKNSECTS save_nsects;

  fhead = (FILE_HEADER *) page_fhead;
  file_header_sanity_check (fhead);
  assert (!FILE_IS_TEMPORARY (fhead));

  /* how is this done:
   * when the file was expanded, all newly reserved sectors have been added to partial table in file header, which was
   * empty before.
   * we will empty it back, and update statistics (sector count and free page count). */

  /* empty partial table */
  FILE_HEADER_GET_PART_FTAB (fhead, part_table);
  assert (file_extdata_item_count (part_table) == fhead->n_sector_partial);
  assert (VPID_ISNULL (&part_table->vpid_next));
  part_table->n_items = 0;

  /* update fhead */
  assert (fhead->n_page_free == fhead->n_sector_empty * DISK_SECTOR_NPAGES);
  fhead->n_page_total -= fhead->n_page_free;
  fhead->n_page_free = 0;
  fhead->n_sector_total -= fhead->n_sector_empty;
  save_nsects = fhead->n_sector_empty;
  fhead->n_sector_partial = 0;
  fhead->n_sector_empty = 0;

  file_header_sanity_check (fhead);

  file_log ("file_rv_perm_expand_undo",
	    "removed expanded sectors from partial table and file header in file %d|%d, "
	    "page header %d|%d, lsa %lld|%d, number of sectors %d \n"
	    FILE_HEAD_ALLOC_MSG, VFID_AS_ARGS (&fhead->self),
	    PGBUF_PAGE_VPID_AS_ARGS (page_fhead),
	    PGBUF_PAGE_LSA_AS_ARGS (page_fhead), save_nsects, FILE_HEAD_ALLOC_AS_ARGS (fhead));

  pgbuf_set_dirty (thread_p, page_fhead, DONT_FREE);

  return NO_ERROR;
}

/*
 * file_rv_perm_expand_redo () - Redo permanent file expansion.
 *
 * return	 : Error code
 * thread_p (in) : Thread entry
 * rcv (in)	 : Recovery data
 */
int
file_rv_perm_expand_redo (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  PAGE_PTR page_fhead = rcv->pgptr;
  FILE_HEADER *fhead;
  FILE_EXTENSIBLE_DATA *extdata_part_table;
  int count_vsids;
  VSID *vsids, *vsid_iter;
  FILE_PARTIAL_SECTOR partsect;

  /* how is this done:
   * file expansion means a number of sectors have been added to partial table in file header, which was empty before.
   * recovery data includes the sector ID's of all new sectors.
   * append empty sectors to partial table in file header. */

  vsids = (VSID *) rcv->data;
  count_vsids = rcv->length / sizeof (VSID);
  assert (count_vsids * (int) sizeof (VSID) == rcv->length);

  fhead = (FILE_HEADER *) page_fhead;
  file_header_sanity_check (fhead);
  assert (!FILE_IS_TEMPORARY (fhead));

  FILE_HEADER_GET_PART_FTAB (fhead, extdata_part_table);
  assert (file_extdata_is_empty (extdata_part_table));
  /* bitmaps will be empty */
  partsect.page_bitmap = FILE_EMPTY_PAGE_BITMAP;
  for (vsid_iter = vsids; vsid_iter < vsids + count_vsids; vsid_iter++)
    {
      partsect.vsid = *vsid_iter;
      file_extdata_append (extdata_part_table, &partsect);
    }

  fhead->n_sector_total += count_vsids;
  assert (fhead->n_sector_empty == 0);
  fhead->n_sector_empty = count_vsids;
  assert (fhead->n_sector_partial == 0);
  fhead->n_sector_partial = count_vsids;

  assert (fhead->n_page_free == 0);
  fhead->n_page_free = count_vsids * DISK_SECTOR_NPAGES;
  fhead->n_page_total += fhead->n_page_free;

  file_header_sanity_check (fhead);

  file_log ("file_rv_perm_expand_redo",
	    "recovery expand in file %d|%d, file header %d|%d, lsa %lld|%d \n"
	    FILE_HEAD_ALLOC_MSG FILE_EXTDATA_MSG ("partial table after"),
	    VFID_AS_ARGS (&fhead->self), PGBUF_PAGE_VPID_AS_ARGS (page_fhead),
	    PGBUF_PAGE_LSA_AS_ARGS (page_fhead),
	    FILE_HEAD_ALLOC_AS_ARGS (fhead), FILE_EXTDATA_AS_ARGS (extdata_part_table));

  pgbuf_set_dirty (thread_p, page_fhead, DONT_FREE);
  return NO_ERROR;
}

/*
 * file_perm_expand () - Expand permanent file by reserving new sectors.
 *
 * return	   : Error code
 * thread_p (in)   : Thread entry
 * page_fhead (in) : File header page
 */
static int
file_perm_expand (THREAD_ENTRY * thread_p, PAGE_PTR page_fhead)
{
  FILE_HEADER *fhead = NULL;
  int expand_min_size_in_sectors;
  int expand_max_size_in_sectors;
  int expand_size_in_sectors;
  VSID *vsids_reserved = NULL;
  VSID *vsid_iter = NULL;
  FILE_EXTENSIBLE_DATA *extdata_part_ftab;
  FILE_PARTIAL_SECTOR partsect;
  bool is_sysop_started = false;
  LOG_LSA save_lsa;
  int error_code = NO_ERROR;

  /* caller should have started a system operation */
  assert (log_check_system_op_is_started (thread_p));

  fhead = (FILE_HEADER *) page_fhead;
  file_header_sanity_check (fhead);
  assert (!FILE_IS_TEMPORARY (fhead));

  /* compute desired expansion size. we should consider ratio, minimum size and maximum size. also, maximum size cannot
   * exceed the capacity of partial table in file header. we want to avoid the headache of allocating file table pages
   */
  FILE_HEADER_GET_PART_FTAB (fhead, extdata_part_ftab);
  expand_min_size_in_sectors = MAX (fhead->tablespace.expand_min_size / DB_SECTORSIZE, 1);
  expand_max_size_in_sectors =
    MIN (fhead->tablespace.expand_max_size / DB_SECTORSIZE, file_extdata_remaining_capacity (extdata_part_ftab));
  assert (expand_min_size_in_sectors <= expand_max_size_in_sectors);

  expand_size_in_sectors = (int) ((float) fhead->n_sector_total * fhead->tablespace.expand_ratio);
  expand_size_in_sectors = MAX (expand_size_in_sectors, expand_min_size_in_sectors);
  expand_size_in_sectors = MIN (expand_size_in_sectors, expand_max_size_in_sectors);

  file_log ("file_perm_expand",
	    "expand file %d|%d by %d sectors. \n" FILE_HEAD_ALLOC_MSG
	    FILE_TABLESPACE_MSG, VFID_AS_ARGS (&fhead->self),
	    expand_size_in_sectors, FILE_HEAD_ALLOC_AS_ARGS (fhead), FILE_TABLESPACE_AS_ARGS (&fhead->tablespace));

  /* allocate a buffer to hold the new sectors */
  vsids_reserved = (VSID *) db_private_alloc (thread_p, expand_size_in_sectors * sizeof (VSID));
  if (vsids_reserved == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, expand_size_in_sectors * sizeof (VSID));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  /* before we start, we need to open a new system operation that gets committed. it is hard to undo file expansion,
   * because we would have to search for each VSID we added individually. moreover, it is very likely to need to expand
   * the file again. therefore, we should make the expansion permanent. */
  log_sysop_start (thread_p);
  is_sysop_started = true;

  /* reserve disk sectors */
  error_code =
    disk_reserve_sectors (thread_p, DB_PERMANENT_DATA_PURPOSE, fhead->volid_last_expand, expand_size_in_sectors,
			  vsids_reserved);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto exit;
    }

  /* sort VSID's. */
  qsort (vsids_reserved, expand_size_in_sectors, sizeof (VSID), file_compare_vsids);

  /* save in file header partial table. all sectors will be empty */
  partsect.page_bitmap = FILE_EMPTY_PAGE_BITMAP;
  for (vsid_iter = vsids_reserved; vsid_iter < vsids_reserved + expand_size_in_sectors; vsid_iter++)
    {
      partsect.vsid = *vsid_iter;
      file_extdata_append (extdata_part_ftab, &partsect);
    }

  /* update header stats */
  fhead->n_sector_total += expand_size_in_sectors;
  assert (fhead->n_sector_empty == 0);
  fhead->n_sector_empty = expand_size_in_sectors;
  assert (fhead->n_sector_partial == 0);
  fhead->n_sector_partial = expand_size_in_sectors;

  assert (fhead->n_page_free == 0);
  fhead->n_page_free = expand_size_in_sectors * DISK_SECTOR_NPAGES;
  fhead->n_page_total += fhead->n_page_free;

  save_lsa = *pgbuf_get_lsa (page_fhead);

  /* file extended successfully. log the change. */
  log_append_undoredo_data2 (thread_p, RVFL_EXPAND, NULL, page_fhead, 0, 0,
			     expand_size_in_sectors * sizeof (VSID), NULL, vsids_reserved);

  file_log ("file_perm_expand",
	    "expand file %d|%d, page header %d|%d, prev_lsa %lld|%d, crt_lsa %lld|%d; "
	    "first sector %d|%d \n" FILE_HEAD_ALLOC_MSG
	    FILE_EXTDATA_MSG ("partial table"), VFID_AS_ARGS (&fhead->self),
	    PGBUF_PAGE_VPID_AS_ARGS (page_fhead), LSA_AS_ARGS (&save_lsa),
	    PGBUF_PAGE_LSA_AS_ARGS (page_fhead),
	    VSID_AS_ARGS (vsids_reserved), FILE_HEAD_ALLOC_AS_ARGS (fhead), FILE_EXTDATA_AS_ARGS (extdata_part_ftab));

  pgbuf_set_dirty (thread_p, page_fhead, DONT_FREE);

exit:
  if (is_sysop_started)
    {
      if (error_code != NO_ERROR)
	{
	  log_sysop_abort (thread_p);
	}
      else
	{
	  log_sysop_commit (thread_p);
	}
    }
  if (vsids_reserved != NULL)
    {
      db_private_free (thread_p, vsids_reserved);
    }
  return error_code;
}

/*
 * file_table_move_partial_sectors_to_header () - Move partial sectors from first page of partial table to header
 *						  section of partial table
 *
 * return :
 * THREAD_ENTRY * thread_p (in) :
 * PAGE_PTR page_fhead (in) :
 */
static int
file_table_move_partial_sectors_to_header (THREAD_ENTRY * thread_p, PAGE_PTR page_fhead)
{
  FILE_HEADER *fhead = NULL;
  FILE_EXTENSIBLE_DATA *extdata_part_ftab_head;
  FILE_EXTENSIBLE_DATA *extdata_part_ftab_first;
  PAGE_PTR page_part_ftab_first = NULL;
  int n_items_to_move;
  bool is_sysop_started = false;
  LOG_LSA save_lsa;
  int error_code = NO_ERROR;

  /* how it works:
   * when we have an empty section in header partial table, but we still have partial sectors in other pages, we move
   * some or all partial sectors in first page (all are moved if they fit header section).
   *
   * this is just a relocation of sectors that has no impact on the file structure. we can use a system operation and
   * commit it immediately after the relocation.
   */

  fhead = (FILE_HEADER *) page_fhead;
  file_header_sanity_check (fhead);
  assert (!FILE_IS_TEMPORARY (fhead));

  /* Caller should have checked */
  assert (fhead->n_sector_partial > 0);

  FILE_HEADER_GET_PART_FTAB (fhead, extdata_part_ftab_head);
  if (VPID_ISNULL (&extdata_part_ftab_head->vpid_next))
    {
      /* caller should have checked and we should not be here */
      assert (false);
      error_code = ER_FAILED;
      goto exit;
    }

  /* get first page of partial table */
  page_part_ftab_first =
    pgbuf_fix (thread_p, &extdata_part_ftab_head->vpid_next, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (page_part_ftab_first == NULL)
    {
      ASSERT_ERROR_AND_SET (error_code);
      goto exit;
    }

  /* get partial table extensible data in first page and let's see how many items we can move */
  extdata_part_ftab_first = (FILE_EXTENSIBLE_DATA *) page_part_ftab_first;

  file_log ("file_table_move_partial_sectors_to_header",
	    "file %d|%d \n" FILE_EXTDATA_MSG ("header (destination)")
	    FILE_EXTDATA_MSG ("first page (source)"),
	    VFID_AS_ARGS (&fhead->self),
	    FILE_EXTDATA_AS_ARGS (extdata_part_ftab_head), FILE_EXTDATA_AS_ARGS (extdata_part_ftab_first));

  n_items_to_move = file_extdata_item_count (extdata_part_ftab_first);
  if (n_items_to_move == 0)
    {
      /* not expected. */
      assert_release (false);
      error_code = ER_FAILED;
      goto exit;
    }

  /* get partial table extensible data in header page */
  FILE_HEADER_GET_PART_FTAB (fhead, extdata_part_ftab_head);
  if (!file_extdata_is_empty (extdata_part_ftab_head))
    {
      /* we shouldn't be here. release version will still work. */
      assert (false);
      goto exit;
    }
  /* we cannot move more than the capacity of extensible data in header page */
  n_items_to_move = MIN (n_items_to_move, file_extdata_remaining_capacity (extdata_part_ftab_head));

  /* start a system operation */
  log_sysop_start (thread_p);
  is_sysop_started = true;

  /* copy items to header section */
  file_extdata_append_array (extdata_part_ftab_head,
			     file_extdata_start (extdata_part_ftab_first), (INT16) n_items_to_move);
  save_lsa = *pgbuf_get_lsa (page_fhead);
  /* log changes to extensible data in header page */
  file_log_extdata_add (thread_p, extdata_part_ftab_head, page_fhead, 0,
			n_items_to_move, file_extdata_start (extdata_part_ftab_first));

  file_log ("file_table_move_partial_sectors_to_header",
	    "moved %d items from first page to header page file table. \n"
	    "file %d|%d, header page %d|%d, prev_lsa %lld|%d, crt_lsa %lld|%d \n"
	    FILE_EXTDATA_MSG ("header partial table"), n_items_to_move,
	    VFID_AS_ARGS (&fhead->self), PGBUF_PAGE_VPID_AS_ARGS (page_fhead),
	    LSA_AS_ARGS (&save_lsa), PGBUF_PAGE_LSA_AS_ARGS (page_fhead),
	    FILE_EXTDATA_AS_ARGS (extdata_part_ftab_head));

  /* now remove from first page. if all items have been moved, we can deallocate first page. */
  if (n_items_to_move < file_extdata_item_count (extdata_part_ftab_first))
    {
      /* Remove copied entries. */
      save_lsa = *pgbuf_get_lsa (page_part_ftab_first);
      file_extdata_remove_at (extdata_part_ftab_first, 0, n_items_to_move);
      file_log_extdata_remove (thread_p, extdata_part_ftab_first, page_part_ftab_first, 0, n_items_to_move);

      file_log ("file_table_move_partial_sectors_to_header",
		"removed %d items from first page partial table \n"
		"file %d|%d, page %d|%d, prev_lsa %lld|%d, crt_lsa %lld|%d \n"
		FILE_EXTDATA_MSG ("first page partial table"),
		n_items_to_move, VFID_AS_ARGS (&fhead->self),
		PGBUF_PAGE_VPID_AS_ARGS (page_part_ftab_first),
		LSA_AS_ARGS (&save_lsa),
		PGBUF_PAGE_LSA_AS_ARGS (page_part_ftab_first), FILE_EXTDATA_AS_ARGS (extdata_part_ftab_first));
    }
  else
    {
      /* deallocate the first partial table page. */
      VPID save_next = extdata_part_ftab_head->vpid_next;
      file_log_extdata_set_next (thread_p, extdata_part_ftab_head, page_fhead, &extdata_part_ftab_first->vpid_next);
      VPID_COPY (&extdata_part_ftab_head->vpid_next, &extdata_part_ftab_first->vpid_next);

      file_log ("file_table_move_partial_sectors_to_header",
		"remove first partial table page %d|%d\n", VPID_AS_ARGS (&save_next));

      pgbuf_unfix_and_init (thread_p, page_part_ftab_first);
      error_code = file_perm_dealloc (thread_p, page_fhead, &save_next, FILE_ALLOC_TABLE_PAGE);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto exit;
	}
    }

  /* done */

exit:
  if (page_part_ftab_first != NULL)
    {
      pgbuf_unfix_and_init (thread_p, page_part_ftab_first);
    }

  if (is_sysop_started)
    {
      if (error_code != NO_ERROR)
	{
	  log_sysop_abort (thread_p);
	}
      else
	{
	  log_sysop_commit (thread_p);
	}
    }

  return error_code;
}

/*
 * file_table_add_full_sector () - Add a new sector to full table
 *
 * return	   : Error code
 * thread_p (in)   : Thread entry
 * page_fhead (in) : File header page
 * vsid (in)	   : Sector ID
 */
static int
file_table_add_full_sector (THREAD_ENTRY * thread_p, PAGE_PTR page_fhead, const VSID * vsid)
{
  bool found = false;
  PAGE_PTR page_ftab = NULL;
  FILE_HEADER *fhead = NULL;
  FILE_EXTENSIBLE_DATA *extdata_full_ftab;
  PAGE_PTR page_extdata = NULL;
  LOG_LSA save_lsa;
  int error_code = NO_ERROR;

  /* how it works:
   * add VSID to full table. first we try to find free space in the existing extensible data components. if there is no
   * free space, we allocate a new table page and append it to full table.
   *
   * note: temporary files does not keep a full table. temporary files only keep partial table (and full sectors are
   *       never moved)
   * note: this must be called in the context of a page allocation and should be under a system operation
   */

  assert (page_fhead != NULL);
  assert (vsid != NULL);
  assert (log_check_system_op_is_started (thread_p));

  fhead = (FILE_HEADER *) page_fhead;
  assert (!FILE_IS_TEMPORARY (fhead));

  /* get full table in file header */
  FILE_HEADER_GET_FULL_FTAB (fhead, extdata_full_ftab);
  /* search for full table component with free space */
  error_code = file_extdata_find_not_full (thread_p, &extdata_full_ftab, &page_ftab, &found);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto exit;
    }
  page_extdata = page_ftab != NULL ? page_ftab : page_fhead;
  if (found)
    {
      /* add the new VSID to full table. note that we keep sectors ordered. */
      int pos = -1;

      file_extdata_find_ordered (extdata_full_ftab, vsid, file_compare_vsids, &found, &pos);
      if (found)
	{
	  /* ups, duplicate! */
	  assert_release (false);
	  error_code = ER_FAILED;
	  goto exit;
	}
      file_extdata_insert_at (extdata_full_ftab, pos, 1, vsid);

      /* log the change. */
      save_lsa = *pgbuf_get_lsa (page_extdata);
      file_log_extdata_add (thread_p, extdata_full_ftab, page_extdata, pos, 1, vsid);

      file_log ("file_table_add_full_sector",
		"add sector %d|%d at position %d in file %d|%d, full table page %d|%d, "
		"prev_lsa %lld|%d, crt_lsa %lld|%d, \n"
		FILE_EXTDATA_MSG ("full table component"),
		VSID_AS_ARGS (vsid), pos, VFID_AS_ARGS (&fhead->self),
		PGBUF_PAGE_VPID_AS_ARGS (page_extdata),
		LSA_AS_ARGS (&save_lsa),
		PGBUF_PAGE_LSA_AS_ARGS (page_extdata), FILE_EXTDATA_AS_ARGS (extdata_full_ftab));
    }
  else
    {
      /* no free space. add a new page to full table. */
      VPID vpid_ftab_new;

      error_code = file_perm_alloc (thread_p, page_fhead, FILE_ALLOC_TABLE_PAGE, &vpid_ftab_new);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto exit;
	}

      /* Log and link the page in previous table. */
      file_log_extdata_set_next (thread_p, extdata_full_ftab, page_extdata, &vpid_ftab_new);
      VPID_COPY (&extdata_full_ftab->vpid_next, &vpid_ftab_new);

      if (page_ftab != NULL)
	{
	  /* No longer needed */
	  pgbuf_unfix_and_init (thread_p, page_ftab);
	}

      /* fix new table page */
      page_ftab = pgbuf_fix (thread_p, &vpid_ftab_new, NEW_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
      if (page_ftab == NULL)
	{
	  ASSERT_ERROR_AND_SET (error_code);
	  goto exit;
	}
      pgbuf_set_page_ptype (thread_p, page_ftab, PAGE_FTAB);

      /* init new table extensible data and append the VSID */
      extdata_full_ftab = (FILE_EXTENSIBLE_DATA *) page_ftab;
      file_extdata_init (sizeof (VSID), DB_PAGESIZE, extdata_full_ftab);
      file_extdata_append (extdata_full_ftab, vsid);

      file_log ("file_table_add_full_sector",
		"added sector %d|%d in new full table page %d|%d \n"
		FILE_EXTDATA_MSG ("new extensible data component"),
		VSID_AS_ARGS (vsid), VPID_AS_ARGS (&vpid_ftab_new), FILE_EXTDATA_AS_ARGS (extdata_full_ftab));

      pgbuf_log_new_page (thread_p, page_ftab, file_extdata_size (extdata_full_ftab), PAGE_FTAB);
      pgbuf_unfix_and_init (thread_p, page_ftab);
    }

  /* done */
  assert (error_code == NO_ERROR);

exit:
  if (page_ftab != NULL)
    {
      pgbuf_unfix (thread_p, page_ftab);
    }
  return error_code;
}

/*
 * file_rv_dump_vfid_and_vpid () - Dump recovery data when VFID and VPID are logged (used for several cases).
 *
 * return      : Void
 * fp (in)     : Dump output
 * length (in) : Recovery data length
 * data (in)   : Recovery data
 */
void
file_rv_dump_vfid_and_vpid (FILE * fp, int length, void *data)
{
  VFID *vfid = NULL;
  VPID *vpid = NULL;
  char *rcv_data = (char *) data;
  int offset = 0;

  vfid = (VFID *) (rcv_data + offset);
  offset += sizeof (vfid);

  vpid = (VPID *) (rcv_data + offset);
  offset += sizeof (vpid);

  assert (offset == length);

  fprintf (fp, "VFID = %d|%d \nVPID = %d|%d.\n", VFID_AS_ARGS (vfid), VPID_AS_ARGS (vpid));
}

/*
 * file_perm_alloc () - Allocate a new page in permament file.
 *
 * return		: Error code
 * thread_p (in)	: Thread entry
 * page_fhead (in)	: File header page
 * alloc_type (in)	: User/table page
 * vpid_alloc_out (out) : VPID of allocated page
 */
static int
file_perm_alloc (THREAD_ENTRY * thread_p, PAGE_PTR page_fhead, FILE_ALLOC_TYPE alloc_type, VPID * vpid_alloc_out)
{
  FILE_HEADER *fhead = (FILE_HEADER *) page_fhead;
  FILE_EXTENSIBLE_DATA *extdata_part_ftab = NULL;
  FILE_EXTENSIBLE_DATA *extdata_full_ftab = NULL;
  FILE_PARTIAL_SECTOR *partsect;
  PAGE_PTR page_ftab = NULL;
  int offset_to_alloc_bit;
  bool was_empty = false;
  bool is_full = false;
  LOG_LSA save_lsa;
  int error_code = NO_ERROR;

  /* how it works:
   * the file header (without considering numerable files) is split into two tables: partial table and full table.
   * to allocate new pages, the full is not interesting. to do a very fast allocation, we just pick a page in the first
   * partial sector.
   *
   * if there is no partial sector (all sectors are full), we need to expand the file (by calling file_perm_expand).
   * new sectors are reserved and we can pick a page from these sectors.
   *
   * if the partial sector becomes full, we must move it to full table.
   *
   * note: this is not for temporary files. temporary file allocation is different.
   * note: this does not handle user page table for numerable files.
   * note: this should always be called under a system operation. the system operation should always be committed before
   *       file header page is unfixed.
   *
   * todo: we might consider to do lazy moves to full table. rather than moving the full sector immediately, we let it
   *       in partial table until something happens (e.g. the section of partial table in file header becomes entirely
   *       full).
   */

  assert (log_check_system_op_is_started (thread_p));
  assert (page_fhead != NULL);

  file_log ("file_perm_alloc", "%s", FILE_ALLOC_TYPE_STRING (alloc_type));

  if (fhead->n_page_free == 0)
    {
      /* no free pages. we need to expand file. */
      error_code = file_perm_expand (thread_p, page_fhead);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto exit;
	}
    }
  assert (fhead->n_page_free > 0);
  assert (fhead->n_sector_partial > 0);

  /* get partial table in file header */
  FILE_HEADER_GET_PART_FTAB (fhead, extdata_part_ftab);
  if (file_extdata_is_empty (extdata_part_ftab))
    {
      /* we know we have free pages, so we should have partial sectors in other table pages */
      error_code = file_table_move_partial_sectors_to_header (thread_p, page_fhead);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto exit;
	}
    }

  /* we must have partial sectors in header! */
  assert (!file_extdata_is_empty (extdata_part_ftab));

  /* allocate a new page in first partial sector */
  partsect = (FILE_PARTIAL_SECTOR *) file_extdata_start (extdata_part_ftab);

  /* should not be full */
  assert (!file_partsect_is_full (partsect));

  /* was partial sector actually empty? we keep this as a statistic for now */
  was_empty = file_partsect_is_empty (partsect);

  /* allocate a page in this partial sector */
  if (!file_partsect_alloc (partsect, vpid_alloc_out, &offset_to_alloc_bit))
    {
      /* should not happen, this is a logic error. */
      assert_release (false);
      error_code = ER_FAILED;
      goto exit;
    }

  assert (file_partsect_is_bit_set (partsect, offset_to_alloc_bit));
  save_lsa = *pgbuf_get_lsa (page_fhead);

  /* log allocation */
  log_append_undoredo_data2 (thread_p, RVFL_PARTSECT_ALLOC, NULL, page_fhead,
			     (PGLENGTH) ((char *) partsect - page_fhead),
			     sizeof (offset_to_alloc_bit), sizeof (offset_to_alloc_bit),
			     &offset_to_alloc_bit, &offset_to_alloc_bit);

  file_log ("file_perm_alloc",
	    "allocated page %d|%d in file %d|%d page %d|%d, prev_lsa %lld|%d, crt_lsa %lld|%d, "
	    "set bit at offset %d in partial sector at offset %d \n"
	    FILE_PARTSECT_MSG ("partsect after"),
	    VPID_AS_ARGS (vpid_alloc_out), VFID_AS_ARGS (&fhead->self),
	    PGBUF_PAGE_VPID_AS_ARGS (page_fhead), LSA_AS_ARGS (&save_lsa),
	    PGBUF_PAGE_LSA_AS_ARGS (page_fhead), offset_to_alloc_bit,
	    (PGLENGTH) ((char *) partsect - page_fhead), FILE_PARTSECT_AS_ARGS (partsect));

  is_full = file_partsect_is_full (partsect);

  /* update header statistics */
  file_header_alloc (fhead, alloc_type, was_empty, is_full);
  save_lsa = *pgbuf_get_lsa (page_fhead);
  file_log_fhead_alloc (thread_p, page_fhead, alloc_type, was_empty, is_full);

  file_log ("file_perm_alloc",
	    "update header in file %d|%d, header page %d|%d, prev_lsa %lld|%d, crt_lsa %lld|%d, "
	    "after %s, was_empty = %s, is_full = %s, \n" FILE_HEAD_ALLOC_MSG,
	    VFID_AS_ARGS (&fhead->self), PGBUF_PAGE_VPID_AS_ARGS (page_fhead),
	    LSA_AS_ARGS (&save_lsa), PGBUF_PAGE_LSA_AS_ARGS (page_fhead),
	    FILE_ALLOC_TYPE_STRING (alloc_type), was_empty ? "true" : "false",
	    is_full ? "true" : "false", FILE_HEAD_ALLOC_AS_ARGS (fhead));

  /* we need to first update header and then move full partial sector to full table. we might need a new page and we
   * must know if no free pages are available to expand the file */
  if (is_full)
    {
      /* move to full table. */
      VSID vsid_full;

      assert (file_partsect_is_full (partsect));

      /* save VSID before removing from partial table */
      vsid_full = partsect->vsid;

      /* remove from partial table first. adding to full table may need a new page and it expects to find one in first
       * partial sector. */
      save_lsa = *pgbuf_get_lsa (page_fhead);
      file_log_extdata_remove (thread_p, extdata_part_ftab, page_fhead, 0, 1);
      file_extdata_remove_at (extdata_part_ftab, 0, 1);

      file_log ("file_perm_alloc",
		"removed full partial sector from position 0 in file %d|%d, header page %d|%d, "
		"prev_lsa %lld|%d, crt_lsa %lld|%d, \n"
		FILE_EXTDATA_MSG ("partial table after alloc"),
		VFID_AS_ARGS (&fhead->self),
		PGBUF_PAGE_VPID_AS_ARGS (page_fhead), LSA_AS_ARGS (&save_lsa),
		PGBUF_PAGE_LSA_AS_ARGS (page_fhead), FILE_EXTDATA_AS_ARGS (extdata_part_ftab));

      /* add to full table */
      error_code = file_table_add_full_sector (thread_p, page_fhead, &vsid_full);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto exit;
	}
    }

  /* done */

  assert (error_code == NO_ERROR);

exit:
  if (page_ftab != NULL)
    {
      pgbuf_unfix (thread_p, page_ftab);
    }

  return error_code;
}

/*
 * file_alloc () - Allocate an user page for file.
 *
 * return	  : Error code
 * thread_p (in)  : Thread entry
 * vfid (in)	  : File identifier
 * vpid_out (out) : VPID of page.
 */
int
file_alloc (THREAD_ENTRY * thread_p, const VFID * vfid, VPID * vpid_out)
{
#define UNDO_DATA_SIZE (sizeof (VFID) + sizeof (VPID))
  VPID vpid_fhead;
  PAGE_PTR page_fhead = NULL;
  FILE_HEADER *fhead = NULL;
  bool is_sysop_started = false;
  char undo_log_data_buf[UNDO_DATA_SIZE + MAX_ALIGNMENT];
  char *undo_log_data = PTR_ALIGN (undo_log_data_buf, MAX_ALIGNMENT);
  int error_code = NO_ERROR;

  assert (vfid != NULL && !VFID_ISNULL (vfid));
  assert (vpid_out != NULL);

  VPID_SET_NULL (vpid_out);

  /* fix header */
  FILE_GET_HEADER_VPID (vfid, &vpid_fhead);
  page_fhead = pgbuf_fix (thread_p, &vpid_fhead, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (page_fhead == NULL)
    {
      ASSERT_ERROR_AND_SET (error_code);
      return error_code;
    }

  fhead = (FILE_HEADER *) page_fhead;
  file_header_sanity_check (fhead);

  file_log ("file_alloc", "allocate new %s page. \n" FILE_HEAD_ALLOC_MSG,
	    FILE_PERM_TEMP_STRING (FILE_IS_TEMPORARY (fhead)), FILE_HEAD_ALLOC_AS_ARGS (fhead));

  if (FILE_IS_TEMPORARY (fhead))
    {
      /* allocate page */
      error_code = file_temp_alloc (thread_p, page_fhead, FILE_ALLOC_USER_PAGE, vpid_out);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto exit;
	}
    }
  else
    {
      /* start a nested system operation. we will end it with commit & undo */
      log_sysop_start (thread_p);
      is_sysop_started = true;

      /* allocate page */
      error_code = file_perm_alloc (thread_p, page_fhead, FILE_ALLOC_USER_PAGE, vpid_out);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto exit;
	}

      /* make undo data for system op commit */
      VFID_COPY ((VFID *) undo_log_data, vfid);
      VPID_COPY ((VPID *) (undo_log_data + sizeof (VFID)), vpid_out);
    }

  assert (!VPID_ISNULL (vpid_out));
  file_header_sanity_check (fhead);

  if (FILE_IS_NUMERABLE (fhead))
    {
      /* we also have to add page to user page table */
      error_code = file_numerable_add_page (thread_p, page_fhead, vpid_out);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto exit;
	}
    }

  /* done */
  assert (error_code == NO_ERROR);

exit:
  if (is_sysop_started)
    {
      if (error_code != NO_ERROR)
	{
	  /* abort system operation. */
	  log_sysop_abort (thread_p);
	}
      else
	{
	  /* commit and undo (to deallocate) */
	  log_sysop_end_logical_undo (thread_p, RVFL_ALLOC, NULL, UNDO_DATA_SIZE, undo_log_data);
	}
    }

  if (page_fhead != NULL)
    {
      pgbuf_unfix_and_init (thread_p, page_fhead);
    }

  return error_code;
#undef UNDO_DATA_SIZE
}

/*
 * file_alloc_and_init () - Allocate page and initialize it.
 *
 * return	    : Error code.
 * thread_p (in)    : Thread entry.
 * vfid (in)	    : File identifier.
 * f_init (in)	    : Page init function.
 * f_init_args (in) : Page init arguments.
 * vpid_alloc (out) : VPID of allocated page.
 */
int
file_alloc_and_init (THREAD_ENTRY * thread_p, const VFID * vfid,
		     FILE_INIT_PAGE_FUNC f_init, void *f_init_args, VPID * vpid_alloc)
{
  PAGE_PTR page_alloc = NULL;
  int error_code = NO_ERROR;

  /* allocate new page */
  error_code = file_alloc (thread_p, vfid, vpid_alloc);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  if (f_init)
    {
      /* initialize page */
      page_alloc = pgbuf_fix (thread_p, vpid_alloc, NEW_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
      if (page_alloc == NULL)
	{
	  ASSERT_ERROR_AND_SET (error_code);
	  return error_code;
	}
      error_code = f_init (thread_p, page_alloc, f_init_args);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	}
      pgbuf_unfix (thread_p, page_alloc);
    }

  return error_code;
}

/*
 * file_alloc_multiple () - Allocate multiple pages at once.
 *
 * return           : Error code.
 * thread_p (in)    : Thread entry.
 * vfid (in)        : File identifier.
 * f_init (in)      : New page init function.
 * f_init_args (in) : Arguments for init function.
 * npages (in)      : Number of pages to allocate.
 * vpids_out (out)  : VPIDS for allocated pages.
 */
int
file_alloc_multiple (THREAD_ENTRY * thread_p, const VFID * vfid,
		     FILE_INIT_PAGE_FUNC f_init, void *f_init_args, int npages, VPID * vpids_out)
{
  VPID *vpid_iter;
  VPID local_vpid = VPID_INITIALIZER;
  int iter;
  VPID vpid_fhead;
  PAGE_PTR page_fhead = NULL;
  FILE_HEADER *fhead = NULL;
  bool is_temp;
  int error_code = NO_ERROR;

  assert (vfid != NULL && !VFID_ISNULL (vfid));
  assert (npages >= 1);

  /* fix header */
  FILE_GET_HEADER_VPID (vfid, &vpid_fhead);
  page_fhead = pgbuf_fix (thread_p, &vpid_fhead, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (page_fhead == NULL)
    {
      ASSERT_ERROR_AND_SET (error_code);
      return error_code;
    }

  fhead = (FILE_HEADER *) page_fhead;
  file_header_sanity_check (fhead);
  /* keep header while allocating all pages. we have a great chance to allocate all pages in the same sectors */

  is_temp = FILE_IS_TEMPORARY (fhead);
  if (!is_temp)
    {
      assert (log_check_system_op_is_started (thread_p));

      /* start a system op. we may abort page allocations if an error occurs. */
      log_sysop_start (thread_p);
    }

  /* do not leak pages! if not numerable, it should use all allocated VPIDS */
  assert (FILE_IS_NUMERABLE (fhead) || vpids_out != NULL);

  for (iter = 0; iter < npages; iter++)
    {
      vpid_iter = vpids_out ? vpids_out + iter : &local_vpid;
      error_code = file_alloc_and_init (thread_p, vfid, f_init, f_init_args, vpid_iter);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto exit;
	}
    }
  /* done */
  assert (error_code == NO_ERROR);

exit:
  if (!is_temp)
    {
      assert (log_check_system_op_is_started (thread_p));
      if (error_code == NO_ERROR)
	{
	  /* caller will decide what happens with allocated pages */
	  log_sysop_attach_to_outer (thread_p);
	}
      else
	{
	  /* undo allocations */
	  log_sysop_abort (thread_p);
	}
    }

  if (page_fhead != NULL)
    {
      pgbuf_unfix (thread_p, page_fhead);
    }

  return error_code;
}

/*
 * file_alloc_sticky_first_page () - Allocate first file page and make it sticky. It is usually used as special headers
 *                                   and should never be deallocated.
 *
 * return         : Error code
 * thread_p (in)  : Thread entry
 * vfid (in)      : File identifier
 * vpid_out (out) : Allocated page VPID
 */
int
file_alloc_sticky_first_page (THREAD_ENTRY * thread_p, const VFID * vfid, VPID * vpid_out)
{
  VPID vpid_fhead;
  PAGE_PTR page_fhead = NULL;
  FILE_HEADER *fhead = NULL;
  LOG_LSA save_lsa;
  int error_code = NO_ERROR;

  /* fix header */
  FILE_GET_HEADER_VPID (vfid, &vpid_fhead);
  page_fhead = pgbuf_fix (thread_p, &vpid_fhead, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (page_fhead == NULL)
    {
      ASSERT_ERROR_AND_SET (error_code);
      return error_code;
    }

  fhead = (FILE_HEADER *) page_fhead;
  file_header_sanity_check (fhead);

  assert (fhead->n_page_user == 0);
  assert (VPID_ISNULL (&fhead->vpid_sticky_first));

  error_code = file_alloc (thread_p, vfid, vpid_out);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto exit;
    }

  /* save VPID */
  save_lsa = *pgbuf_get_lsa (page_fhead);
  log_append_undoredo_data2 (thread_p, RVFL_FHEAD_STICKY_PAGE, NULL,
			     page_fhead, 0, sizeof (VPID), sizeof (VPID), &fhead->vpid_sticky_first, vpid_out);
  fhead->vpid_sticky_first = *vpid_out;
  pgbuf_set_dirty (thread_p, page_fhead, DONT_FREE);

  /* done */
  file_header_sanity_check (fhead);
  assert (error_code == NO_ERROR);

  file_log ("file_alloc_sticky_first_page",
	    "set vpid_sticky_first to %d|%d in file %d|%d, header page %d|%d, "
	    "prev_lsa %lld|%d, crt_lsa %lld|%d", VPID_AS_ARGS (vpid_out),
	    VFID_AS_ARGS (vfid), PGBUF_PAGE_VPID_AS_ARGS (page_fhead),
	    LSA_AS_ARGS (&save_lsa), PGBUF_PAGE_LSA_AS_ARGS (page_fhead));

exit:
  if (page_fhead != NULL)
    {
      pgbuf_unfix (thread_p, page_fhead);
    }
  return error_code;
}

/*
 * file_rv_fhead_sticky_page () - Recovery sticky page VPID in file header.
 *
 * return        : NO_ERROR
 * thread_p (in) : Thread entry
 * rcv (in)      : Recovery data
 */
int
file_rv_fhead_sticky_page (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  PAGE_PTR page_fhead = rcv->pgptr;
  FILE_HEADER *fhead = (FILE_HEADER *) page_fhead;
  VPID *vpid = (VPID *) rcv->data;

  assert (rcv->length == sizeof (*vpid));

  fhead->vpid_sticky_first = *vpid;
  pgbuf_set_dirty (thread_p, page_fhead, DONT_FREE);

  file_log ("file_rv_fhead_sticky_page",
	    "set vpid_sticky_first to %d|%d in file %d|%d, header page %d|%d, lsa %lld|%d",
	    VPID_AS_ARGS (vpid), VFID_AS_ARGS (&fhead->self),
	    PGBUF_PAGE_VPID_AS_ARGS (page_fhead), PGBUF_PAGE_LSA_AS_ARGS (page_fhead));
  return NO_ERROR;
}

/*
 * file_get_sticky_first_page () - Get VPID of first page. It should be a sticky page.
 *
 * return         : Error code
 * thread_p (in)  : Thread entry
 * vfid (in)      : File identifier
 * vpid_out (out) : VPID of sticky first page
 */
int
file_get_sticky_first_page (THREAD_ENTRY * thread_p, const VFID * vfid, VPID * vpid_out)
{
  VPID vpid_fhead;
  PAGE_PTR page_fhead = NULL;
  FILE_HEADER *fhead = NULL;
  int error_code = NO_ERROR;

  /* fix header */
  FILE_GET_HEADER_VPID (vfid, &vpid_fhead);
  page_fhead = pgbuf_fix (thread_p, &vpid_fhead, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (page_fhead == NULL)
    {
      ASSERT_ERROR_AND_SET (error_code);
      return error_code;
    }

  fhead = (FILE_HEADER *) page_fhead;
  if (LOG_ISRESTARTED ())
    {
      /* sometimes called before recovery... we cannot guarantee the header is sane at this point. */
      file_header_sanity_check (fhead);
    }

  *vpid_out = fhead->vpid_sticky_first;
  if (VPID_ISNULL (vpid_out))
    {
      assert_release (false);
      pgbuf_unfix (thread_p, page_fhead);
      return ER_FAILED;
    }

  pgbuf_unfix (thread_p, page_fhead);
  return NO_ERROR;
}

/*
 * file_dealloc () - Deallocate a file page.
 *
 * return	       : Error code
 * thread_p (in)       : Thread entry
 * vfid (in)	       : File identifier
 * vpid (in)	       : Page identifier
 * file_type_hint (in) : Hint for file type. Usually caller knows exactly what kind of file he deallocates the page
 *			 from. Since deallocate is different based on page type (permanent/temporary, numerable),
 *			 and we don't always have to fix header page, it is good to tell dealloc what to do from the
 *			 start.
 */
int
file_dealloc (THREAD_ENTRY * thread_p, const VFID * vfid, const VPID * vpid, FILE_TYPE file_type_hint)
{
#define LOG_DATA_SIZE (sizeof (VFID) + sizeof (VPID))
  VPID vpid_fhead;
  PAGE_PTR page_fhead = NULL;
  FILE_HEADER *fhead = NULL;
  char log_data_buffer[LOG_DATA_SIZE + MAX_ALIGNMENT];
  char *log_data = PTR_ALIGN (log_data_buffer, MAX_ALIGNMENT);
  LOG_DATA_ADDR log_addr = LOG_DATA_ADDR_INITIALIZER;
  FILE_EXTENSIBLE_DATA *extdata_user_page_ftab = NULL;
  PAGE_PTR page_ftab = NULL;
  PAGE_PTR page_extdata = NULL;
  bool found = false;
  int pos = -1;
  VPID *vpid_found;
  LOG_LSA save_lsa;
  int error_code = NO_ERROR;

  /* how it works:
   *
   * permanent files: we don't actually deallocate page here. we postpone the deallocation after commit, since we don't
   *                  want anyone to reuse it until we are really sure the page is no longer used.
   *
   * temporary files: we do not deallocate the page.
   *
   * numerable files: we mark the page for 
   */

  /* todo: add known is_temp/is_numerable */

  /* read file type from header if caller doesn't know it. debug always reads the type from file header to check caller
   * is not wrong. */
#if defined (NDEBUG)
  if (file_type_hint == FILE_UNKNOWN_TYPE
      || file_type_hint == FILE_EXTENDIBLE_HASH || file_type_hint == FILE_EXTENDIBLE_HASH_DIRECTORY)
#endif /* NDEBUG */
    {
      /* this should be avoided in release */
      vpid_fhead.volid = vfid->volid;
      vpid_fhead.pageid = vfid->fileid;

      page_fhead = pgbuf_fix (thread_p, &vpid_fhead, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
      if (page_fhead == NULL)
	{
	  ASSERT_ERROR_AND_SET (error_code);
	  goto exit;
	}
      fhead = (FILE_HEADER *) page_fhead;
      assert (file_type_hint == FILE_UNKNOWN_TYPE || file_type_hint == fhead->type);
      file_type_hint = fhead->type;

      assert (!VPID_EQ (&fhead->vpid_sticky_first, vpid));
    }

  if ((fhead != NULL && !FILE_IS_TEMPORARY (fhead)) || file_type_hint != FILE_TEMP)
    {
      /* for all files, we add a postpone record and do the actual deallocation at run postpone. */
      VFID_COPY ((VFID *) log_data, vfid);
      VPID_COPY ((VPID *) (log_data + sizeof (VFID)), vpid);
      log_append_postpone (thread_p, RVFL_DEALLOC, &log_addr, LOG_DATA_SIZE, log_data);

      file_log ("file_dealloc", "file %s %d|%d dealloc vpid %d|%d postponed",
		file_type_to_string (file_type_hint), VFID_AS_ARGS (vfid), VPID_AS_ARGS (vpid));
    }
  else
    {
      /* we do not deallocate pages from temporary files */
    }

  if (!FILE_TYPE_CAN_BE_NUMERABLE (file_type_hint))
    {
      /* we don't need to do anything now. the actual deallocation is postponed (or skipped altogether). */
      goto exit;
    }

  if (page_fhead == NULL)
    {
      vpid_fhead.volid = vfid->volid;
      vpid_fhead.pageid = vfid->fileid;

      page_fhead = pgbuf_fix (thread_p, &vpid_fhead, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
      if (page_fhead == NULL)
	{
	  ASSERT_ERROR_AND_SET (error_code);
	  goto exit;
	}
      fhead = (FILE_HEADER *) page_fhead;
    }

  assert (page_fhead != NULL);
  assert (fhead != NULL);
  assert (!VPID_EQ (&fhead->vpid_sticky_first, vpid));

  if (!FILE_IS_NUMERABLE (fhead))
    {
      /* we don't need to do anything now. the actual deallocation is postponed (or skipped altogether). */
      goto exit;
    }

  /* search for VPID in user page table */
  FILE_HEADER_GET_USER_PAGE_FTAB (fhead, extdata_user_page_ftab);
  error_code = file_extdata_search_item (thread_p, &extdata_user_page_ftab, vpid, file_compare_vpids, false, true,
					 &found, &pos, &page_ftab);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto exit;
    }
  if (!found)
    {
      /* not found?? corrupted table! */
      assert_release (false);
      error_code = ER_FAILED;
      goto exit;
    }

  /* check not marked as deleted */
  vpid_found = (VPID *) file_extdata_at (extdata_user_page_ftab, pos);
  if (FILE_USER_PAGE_IS_MARKED_DELETED (vpid_found))
    {
      /* already marked as deleted? I don't think so! */
      assert_release (false);
      error_code = ER_FAILED;
      goto exit;
    }

  /* mark page as deleted */
  FILE_USER_PAGE_MARK_DELETED (vpid_found);

  page_extdata = page_ftab != NULL ? page_ftab : page_fhead;
  save_lsa = *pgbuf_get_lsa (page_extdata);

  pgbuf_set_dirty (thread_p, page_extdata, DONT_FREE);
  if (!FILE_IS_TEMPORARY (fhead))
    {
      /* log it */
      LOG_DATA_ADDR addr = LOG_DATA_ADDR_INITIALIZER;

      addr.pgptr = page_extdata;
      addr.offset = (PGLENGTH) (((char *) vpid_found) - addr.pgptr);
      log_append_undoredo_data (thread_p, RVFL_USER_PAGE_MARK_DELETE, &addr, LOG_DATA_SIZE, 0, log_data, NULL);
    }

  file_log ("file_dealloc",
	    "marked page %d|%d as deleted in file %d|%d, page %d|%d, prev_lsa %lld|%d, "
	    "crt_lsa %lld_%d, at offset %d ", VPID_AS_ARGS (vpid_found),
	    VFID_AS_ARGS (&fhead->self),
	    PGBUF_PAGE_VPID_AS_ARGS (page_extdata), LSA_AS_ARGS (&save_lsa),
	    PGBUF_PAGE_LSA_AS_ARGS (page_extdata), (PGLENGTH) (((char *) vpid_found) - page_extdata));

  /* update file header */
  file_header_update_mark_deleted (thread_p, page_fhead, 1);

  file_log ("file_dealloc", "file %d|%d marked vpid %|%d as deleted", VFID_AS_ARGS (vfid), VPID_AS_ARGS (vpid));

  /* done */
  assert (error_code != NO_ERROR);

exit:
  if (page_fhead != NULL)
    {
      pgbuf_unfix (thread_p, page_fhead);
    }
  if (page_ftab != NULL)
    {
      pgbuf_unfix (thread_p, page_ftab);
    }

  return error_code;

#undef LOG_DATA_SIZE
}

/*
 * file_extdata_try_merge () - Try to merge extensible data components.
 *
 * return	         : Error code
 * thread_p (in)         : Thread entry
 * page_dest (in)        : Merge destination page
 * extdata_dest (in)     : Merge destination extensible
 * compare_func (in)     : NULL to merge unordered, not NULL to merge ordered
 * rcvindex (in)         : Recovery index used in case merge is executed
 * merged_vpid_out (out) : VPID to be deallocated
 */
STATIC_INLINE int
file_extdata_try_merge (THREAD_ENTRY * thread_p, PAGE_PTR page_dest, FILE_EXTENSIBLE_DATA * extdata_dest,
			int (*compare_func) (const void *, const void *), LOG_RCVINDEX rcvindex, VPID * merged_vpid_out)
{
  FILE_EXTENSIBLE_DATA *extdata_next = NULL;
  VPID vpid_next = extdata_dest->vpid_next;
  PAGE_PTR page_next = NULL;
  int error_code = NO_ERROR;

  assert (extdata_dest != NULL);
  assert (merged_vpid_out != NULL);

  /* how it works:
   *
   * we try to merge next extensible component in the extensible component given as argument.
   * to do the merge, next extensible component should fit current extensible component.
   *
   * note: there can be two types of merges: ordered or unordered. for ordered merge, a compare_func must be provided
   * note: make sure the right recovery index is used. the same merge type and compare functions should be used at
   *       recovery
   */

  VPID_SET_NULL (merged_vpid_out);

  if (VPID_ISNULL (&vpid_next))
    {
      /* no next, nothing to merge */
      return NO_ERROR;
    }

  /* fix next page and get next extensible data component */
  page_next = pgbuf_fix (thread_p, &vpid_next, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (page_next == NULL)
    {
      ASSERT_ERROR_AND_SET (error_code);
      return error_code;
    }
  extdata_next = (FILE_EXTENSIBLE_DATA *) page_next;

  file_log ("file_extdata_try_merge",
	    "\n" FILE_EXTDATA_MSG ("dest") FILE_EXTDATA_MSG ("src"),
	    FILE_EXTDATA_AS_ARGS (extdata_dest), FILE_EXTDATA_AS_ARGS (extdata_next));

  /* can extensible data components be merged? */
  if (file_extdata_can_merge (extdata_next, extdata_dest))
    {
      /* do the merge */
      file_log ("file_extdata_try_merge",
		"page %d|%d prev_lsa %lld|%d \n"
		FILE_EXTDATA_MSG ("dest before merge"),
		PGBUF_PAGE_VPID_AS_ARGS (page_dest),
		PGBUF_PAGE_LSA_AS_ARGS (page_dest), FILE_EXTDATA_AS_ARGS (extdata_dest));

      file_log_extdata_merge (thread_p, extdata_dest, page_dest, extdata_next, rcvindex);
      if (compare_func)
	{
	  file_extdata_merge_ordered (extdata_next, extdata_dest, compare_func);
	}
      else
	{
	  file_extdata_merge_unordered (extdata_next, extdata_dest);
	}

      file_log ("file_extdata_try_merge",
		"page %d|%d crt_lsa %lld|%d \n"
		FILE_EXTDATA_MSG ("dest after merge"),
		PGBUF_PAGE_VPID_AS_ARGS (page_dest),
		PGBUF_PAGE_LSA_AS_ARGS (page_dest), FILE_EXTDATA_AS_ARGS (extdata_dest));

      file_log_extdata_set_next (thread_p, extdata_dest, page_dest, &extdata_next->vpid_next);
      VPID_COPY (&extdata_dest->vpid_next, &extdata_next->vpid_next);

      pgbuf_unfix_and_init (thread_p, page_next);

      /* to deallocate merged page */
      *merged_vpid_out = vpid_next;

      file_log ("file_extdata_try_merge", "merged \n" FILE_EXTDATA_MSG ("dest"), FILE_EXTDATA_AS_ARGS (extdata_dest));
    }
  else
    {
      pgbuf_unfix_and_init (thread_p, page_next);
    }

  return NO_ERROR;
}

/*
 * file_perm_dealloc () - Deallocate page from file tables.
 *
 * return	     : Error code
 * thread_p (in)     : Thread entry
 * page_fhead (in)   : File header page
 * vpid_dealloc (in) : VPID of page being deallocated
 * alloc_type (in)   : User/table page allocation type
 */
static int
file_perm_dealloc (THREAD_ENTRY * thread_p, PAGE_PTR page_fhead, const VPID * vpid_dealloc, FILE_ALLOC_TYPE alloc_type)
{
  FILE_HEADER *fhead = NULL;
  FILE_EXTENSIBLE_DATA *extdata_part_ftab;
  FILE_EXTENSIBLE_DATA *extdata_full_ftab;
  bool found;
  int position;
  PAGE_PTR page_ftab = NULL;
  VSID vsid_dealloc;
  bool is_empty = false;
  bool was_full = false;
  bool is_ftab = (alloc_type == FILE_ALLOC_TABLE_PAGE);
  LOG_DATA_ADDR addr = LOG_DATA_ADDR_INITIALIZER;
  int offset_to_dealloc_bit;
  PAGE_PTR page_dealloc = NULL;
  LOG_LSA save_page_lsa;
  int error_code = NO_ERROR;

  /* how it works:
   * the deallocation should find sector of page and clear the bit of page in sector page bitmap. if the sector was
   * full, it should be moved to partial table.
   *
   * caller should have started a system operation! all table changes should be committed before file header is unfixed.
   *
   * note: this is only for permanent files. temporary files do not deallocate pages.
   * note: this does not handle user page table.
   * note: this should always be called under a system operation.
   */

  assert (log_check_system_op_is_started (thread_p));

  fhead = (FILE_HEADER *) page_fhead;
  assert (!FILE_IS_TEMPORARY (fhead));
  assert (!VPID_EQ (&fhead->vpid_sticky_first, vpid_dealloc));

  /* find page sector in one of the partial or full tables */
  vsid_dealloc.volid = vpid_dealloc->volid;
  vsid_dealloc.sectid = SECTOR_FROM_PAGEID (vpid_dealloc->pageid);

  file_log ("file_perm_dealloc",
	    "file %d|%d de%s %d|%d (search for VSID %d|%d) \n"
	    FILE_HEAD_ALLOC_MSG, VFID_AS_ARGS (&fhead->self),
	    FILE_ALLOC_TYPE_STRING (alloc_type), VPID_AS_ARGS (vpid_dealloc),
	    VSID_AS_ARGS (&vsid_dealloc), FILE_HEAD_ALLOC_AS_ARGS (fhead));

  /* search partial table. */
  FILE_HEADER_GET_PART_FTAB (fhead, extdata_part_ftab);
  error_code = file_extdata_search_item (thread_p, &extdata_part_ftab, &vsid_dealloc, file_compare_vsids, true, true,
					 &found, &position, &page_ftab);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto exit;
    }

  if (found)
    {
      /* clear the bit for page. */
      FILE_PARTIAL_SECTOR *partsect = (FILE_PARTIAL_SECTOR *) file_extdata_at (extdata_part_ftab, position);

      offset_to_dealloc_bit = file_partsect_pageid_to_offset (partsect, vpid_dealloc->pageid);

      file_partsect_clear_bit (partsect, offset_to_dealloc_bit);
      is_empty = file_partsect_is_empty (partsect);

      addr.pgptr = page_ftab != NULL ? page_ftab : page_fhead;
      addr.offset = (PGLENGTH) (((char *) partsect) - addr.pgptr);
      save_page_lsa = *pgbuf_get_lsa (addr.pgptr);

      log_append_undoredo_data (thread_p, RVFL_PARTSECT_DEALLOC, &addr,
				sizeof (offset_to_dealloc_bit), sizeof (offset_to_dealloc_bit),
				&offset_to_dealloc_bit, &offset_to_dealloc_bit);

      pgbuf_set_dirty (thread_p, addr.pgptr, DONT_FREE);

      file_log ("file_perm_dealloc",
		"dealloc page %d|%d in file %d|%d page %d|%d, prev_lsa %lld|%d, crt_lsa %lld|%d, "
		"clear bit at offset %d in partsect at offset %d \n"
		FILE_PARTSECT_MSG ("partsect after"),
		VPID_AS_ARGS (vpid_dealloc), VFID_AS_ARGS (&fhead->self),
		PGBUF_PAGE_VPID_AS_ARGS (addr.pgptr),
		LSA_AS_ARGS (&save_page_lsa),
		PGBUF_PAGE_LSA_AS_ARGS (addr.pgptr), offset_to_dealloc_bit,
		addr.offset, FILE_PARTSECT_AS_ARGS (partsect));

      if (page_ftab != NULL)
	{
	  pgbuf_unfix_and_init (thread_p, page_ftab);
	}
    }
  else
    {
      /* not in partial table. */
      FILE_PARTIAL_SECTOR partsect_new;

      assert (page_ftab == NULL);

      /* search full table */
      was_full = true;
      FILE_HEADER_GET_FULL_FTAB (fhead, extdata_full_ftab);
      error_code = file_extdata_search_item (thread_p, &extdata_full_ftab, &vsid_dealloc, file_compare_vsids, true,
					     true, &found, &position, &page_ftab);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto exit;
	}
      if (!found)
	{
	  /* corrupted file tables. */
	  assert_release (false);
	  error_code = ER_FAILED;
	  goto exit;
	}

      /* move from full table to partial table. */

      /* remove from full table. */
      addr.pgptr = page_ftab != NULL ? page_ftab : page_fhead;
      save_page_lsa = *pgbuf_get_lsa (addr.pgptr);
      file_log_extdata_remove (thread_p, extdata_full_ftab, addr.pgptr, position, 1);
      file_extdata_remove_at (extdata_full_ftab, position, 1);

      file_log ("file_perm_dealloc",
		"removed vsid %d|%d from position %d in file %d|%d, page %d|%d, prev_lsa %lld|%d, "
		"crt_lsa %lld|%d, \n"
		FILE_EXTDATA_MSG ("full table component"),
		VSID_AS_ARGS (&vsid_dealloc), VFID_AS_ARGS (&fhead->self),
		PGBUF_PAGE_VPID_AS_ARGS (addr.pgptr),
		LSA_AS_ARGS (&save_page_lsa),
		PGBUF_PAGE_LSA_AS_ARGS (addr.pgptr), FILE_EXTDATA_AS_ARGS (extdata_full_ftab));

      /* check if full table pages can be merged. */
      if (file_extdata_size (extdata_full_ftab) * 2 < file_extdata_max_size (extdata_full_ftab))
	{
	  VPID vpid_merged;
	  error_code = file_extdata_try_merge (thread_p, addr.pgptr, extdata_full_ftab, file_compare_vsids,
					       RVFL_EXTDATA_MERGE_COMPARE_VSID, &vpid_merged);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      goto exit;
	    }
	  if (!VPID_ISNULL (&vpid_merged))
	    {
	      error_code = file_perm_dealloc (thread_p, page_fhead, &vpid_merged, FILE_ALLOC_TABLE_PAGE);
	      if (error_code != NO_ERROR)
		{
		  ASSERT_ERROR ();
		  return error_code;
		}
	    }
	}

      if (page_ftab != NULL)
	{
	  pgbuf_unfix_and_init (thread_p, page_ftab);
	}

      /* add to partial table. */

      /* create the partial sector structure without the bit for page */
      FILE_HEADER_GET_PART_FTAB (fhead, extdata_part_ftab);
      partsect_new.vsid = vsid_dealloc;
      partsect_new.page_bitmap = FILE_FULL_PAGE_BITMAP;
      offset_to_dealloc_bit = file_partsect_pageid_to_offset (&partsect_new, vpid_dealloc->pageid);
      file_partsect_clear_bit (&partsect_new, offset_to_dealloc_bit);

      /* find free space */
      error_code = file_extdata_find_not_full (thread_p, &extdata_part_ftab, &page_ftab, &found);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto exit;
	}
      /* where is extdata_part_ftab? */
      addr.pgptr = page_ftab != NULL ? page_ftab : page_fhead;

      if (found)
	{
	  /* found free space in table */

	  /* find the correct position for new partial sector to keep the table ordered */
	  file_extdata_find_ordered (extdata_part_ftab, &vsid_dealloc, file_compare_vsids, &found, &position);
	  if (found)
	    {
	      /* ups, duplicate! */
	      assert_release (false);
	      error_code = ER_FAILED;
	      goto exit;
	    }
	  save_page_lsa = *pgbuf_get_lsa (addr.pgptr);
	  file_log_extdata_add (thread_p, extdata_part_ftab, addr.pgptr, position, 1, &partsect_new);
	  file_extdata_insert_at (extdata_part_ftab, position, 1, &partsect_new);

	  file_log ("file_perm_dealloc",
		    "add new partsect at position %d in file %d|%d, page %d|%d, prev_lsa %lld|%d, "
		    "crt_lsa %lld|%d \n"
		    FILE_PARTSECT_MSG ("new partial sector")
		    FILE_EXTDATA_MSG ("partial table component"), position,
		    VFID_AS_ARGS (&fhead->self),
		    PGBUF_PAGE_VPID_AS_ARGS (addr.pgptr),
		    LSA_AS_ARGS (&save_page_lsa),
		    PGBUF_PAGE_LSA_AS_ARGS (addr.pgptr),
		    FILE_PARTSECT_AS_ARGS (&partsect_new), FILE_EXTDATA_AS_ARGS (extdata_part_ftab));

	  if (page_ftab != NULL)
	    {
	      pgbuf_unfix_and_init (thread_p, page_ftab);
	    }
	}
      else
	{
	  /* no free space in partial table. we need to add a new page. */
	  VPID vpid_ftab_new = VPID_INITIALIZER;

	  error_code = file_perm_alloc (thread_p, page_fhead, FILE_ALLOC_TABLE_PAGE, &vpid_ftab_new);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      goto exit;
	    }
	  assert (!VPID_ISNULL (&vpid_ftab_new));

	  /* set next VPID in last extensible component */
	  assert (VPID_ISNULL (&extdata_part_ftab->vpid_next));
	  file_log_extdata_set_next (thread_p, extdata_part_ftab, addr.pgptr, &vpid_ftab_new);
	  VPID_COPY (&extdata_part_ftab->vpid_next, &vpid_ftab_new);
	  if (page_ftab != NULL)
	    {
	      /* no longer needed */
	      pgbuf_unfix_and_init (thread_p, page_ftab);
	    }

	  /* fix new table page */
	  page_ftab = pgbuf_fix (thread_p, &vpid_ftab_new, NEW_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
	  if (page_ftab == NULL)
	    {
	      ASSERT_ERROR_AND_SET (error_code);
	      goto exit;
	    }
	  pgbuf_set_page_ptype (thread_p, page_ftab, PAGE_FTAB);

	  /* init new extensible data and add new partial sector */
	  extdata_part_ftab = (FILE_EXTENSIBLE_DATA *) page_ftab;
	  file_extdata_init (sizeof (FILE_PARTIAL_SECTOR), DB_PAGESIZE, extdata_part_ftab);
	  file_extdata_append (extdata_part_ftab, &partsect_new);
	  pgbuf_log_new_page (thread_p, page_ftab, file_extdata_size (extdata_part_ftab), PAGE_FTAB);

	  file_log ("file_perm_dealloc",
		    "file %d|%d moved to new partial table page %d|%d \n"
		    FILE_PARTSECT_MSG ("new partial sector")
		    FILE_EXTDATA_MSG ("partial table component"),
		    VFID_AS_ARGS (&fhead->self),
		    VPID_AS_ARGS (&vpid_ftab_new),
		    FILE_PARTSECT_AS_ARGS (&partsect_new), FILE_EXTDATA_AS_ARGS (extdata_part_ftab));

	  pgbuf_unfix_and_init (thread_p, page_ftab);
	}
    }

  /* we should have freed any other file table pages */
  assert (page_ftab == NULL);

  /* almost done. We need to update header statistics */
  file_header_dealloc (fhead, alloc_type, is_empty, was_full);
  save_page_lsa = *pgbuf_get_lsa (page_fhead);
  file_log_fhead_dealloc (thread_p, page_fhead, alloc_type, is_empty, was_full);

  file_log ("file_perm_dealloc",
	    "update header in file %d|%d, header page %d|%d, prev_lsa %lld|%d, crt_lsa %lld|%d, "
	    "after de%s, is_empty = %s, was_full = %s, \n"
	    FILE_HEAD_ALLOC_MSG, VFID_AS_ARGS (&fhead->self),
	    PGBUF_PAGE_VPID_AS_ARGS (page_fhead),
	    LSA_AS_ARGS (&save_page_lsa), PGBUF_PAGE_LSA_AS_ARGS (page_fhead),
	    FILE_ALLOC_TYPE_STRING (alloc_type), is_empty ? "true" : "false",
	    was_full ? "true" : "false", FILE_HEAD_ALLOC_AS_ARGS (fhead));

  /* deallocate page */
  page_dealloc = pgbuf_fix (thread_p, vpid_dealloc, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (page_dealloc == NULL)
    {
      ASSERT_ERROR_AND_SET (error_code);
      goto exit;
    }

  error_code = pgbuf_dealloc_page (thread_p, &page_dealloc);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      pgbuf_unfix_and_init (thread_p, page_dealloc);
      goto exit;
    }
  assert (page_dealloc == NULL);

  /* done */
  assert (error_code == NO_ERROR);

exit:
  if (page_ftab != NULL)
    {
      pgbuf_unfix (thread_p, page_ftab);
    }

  return error_code;
}

/*
 * file_rv_dealloc_internal () - Actual deallocation at recovery.
 *
 * return			   : Error code
 * thread_p (in)		   : Thread entry
 * rcv (in)			   : Recovery data
 * compensate_or_run_postpone (in) : Compensate if this is called for undo, run postpone if this is called for postpone.
 */
static int
file_rv_dealloc_internal (THREAD_ENTRY * thread_p, LOG_RCV * rcv, bool compensate_or_run_postpone)
{
  VPID vpid_fhead;
  PAGE_PTR page_fhead = NULL;
  VFID *vfid;
  VPID *vpid_dealloc;
  int offset = 0;
  FILE_HEADER *fhead = NULL;
  bool is_sysop_started = false;
  PAGE_PTR page_dealloc = NULL;
  LOG_LSA save_lsa;
  int error_code = NO_ERROR;

  /* how it works:
   * deallocates a page in either of two contexts:
   * 1. undoing a page allocation.
   * 2. doing (actual) page deallocation on do postpone phase.
   *
   * this should clear the page bit in file tables (partial or full) by calling file_perm_dealloc.
   * also we remove the VPID from user page table.
   */

  /* recovery data: file identifier + page identifier */
  vfid = (VFID *) (rcv->data + offset);
  offset += sizeof (*vfid);

  vpid_dealloc = (VPID *) (rcv->data + offset);
  offset += sizeof (*vpid_dealloc);

  assert (offset == rcv->length);

  /* get file header */
  FILE_GET_HEADER_VPID (vfid, &vpid_fhead);
  page_fhead = pgbuf_fix (thread_p, &vpid_fhead, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (page_fhead == NULL)
    {
      /* should not be interrupted */
      assert_release (false);
      return ER_FAILED;
    }

  fhead = (FILE_HEADER *) page_fhead;
  file_header_sanity_check (fhead);

  if (FILE_IS_TEMPORARY (fhead))
    {
      /* no need to actually deallocate page. should not even be here. */
      assert (false);
      goto exit;
    }

  /* so, we do need a system operation here. the system operation will end, based on context, with commit & compensate
   * or with commit & run postpone. */
  log_sysop_start (thread_p);
  is_sysop_started = true;

  /* clear page bit by calling file_perm_dealloc */
  error_code = file_perm_dealloc (thread_p, page_fhead, vpid_dealloc, FILE_ALLOC_USER_PAGE);
  if (error_code != NO_ERROR)
    {
      /* We cannot allow errors during recovery! */
      assert_release (false);
      error_code = ER_FAILED;
      goto exit;
    }

  if (FILE_IS_NUMERABLE (fhead))
    {
      /* remove VPID from user page table. */
      FILE_EXTENSIBLE_DATA *extdata_user_page_ftab;
      bool found = false;
      int position = -1;
      PAGE_PTR page_ftab = NULL;
      LOG_DATA_ADDR addr = LOG_DATA_ADDR_INITIALIZER;

      /* get user page table */
      FILE_HEADER_GET_USER_PAGE_FTAB (fhead, extdata_user_page_ftab);
      /* search for VPID */
      error_code = file_extdata_search_item (thread_p, &extdata_user_page_ftab, vpid_dealloc, file_compare_vpids,
					     false, true, &found, &position, &page_ftab);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto exit;
	}
      if (!found)
	{
	  /* unexpected. */
	  assert_release (false);
	  error_code = ER_FAILED;
	  if (page_ftab != NULL)
	    {
	      pgbuf_unfix (thread_p, page_ftab);
	    }
	  goto exit;
	}

      /* is mark deleted? */
      if (FILE_USER_PAGE_IS_MARKED_DELETED (file_extdata_at (extdata_user_page_ftab, position)))
	{
	  /* update file header */
	  file_header_update_mark_deleted (thread_p, page_fhead, -1);
	}

      /* remove VPID */
      addr.pgptr = page_ftab != NULL ? page_ftab : page_fhead;
      save_lsa = *pgbuf_get_lsa (addr.pgptr);
      file_log_extdata_remove (thread_p, extdata_user_page_ftab, addr.pgptr, position, 1);
      file_extdata_remove_at (extdata_user_page_ftab, position, 1);

      file_log ("file_rv_dealloc_internal",
		"remove deallocated page %d|%d in file %d|%d, page %d|%d, "
		"prev_lsa %lld|%d, crt_lsa %lld|%d, \n"
		FILE_EXTDATA_MSG ("user page table component"),
		VPID_AS_ARGS (vpid_dealloc), VFID_AS_ARGS (&fhead->self),
		PGBUF_PAGE_VPID_AS_ARGS (addr.pgptr), LSA_AS_ARGS (&save_lsa),
		PGBUF_PAGE_LSA_AS_ARGS (addr.pgptr), FILE_EXTDATA_AS_ARGS (extdata_user_page_ftab));

      /* should we merge pages? */
      if (file_extdata_size (extdata_user_page_ftab) * 2 < file_extdata_max_size (extdata_user_page_ftab))
	{
	  VPID vpid_merged;

	  error_code = file_extdata_try_merge (thread_p, addr.pgptr, extdata_user_page_ftab, NULL, RVFL_EXTDATA_MERGE,
					       &vpid_merged);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      if (page_ftab != NULL)
		{
		  pgbuf_unfix (thread_p, page_ftab);
		}
	      goto exit;
	    }
	  if (!VPID_ISNULL (&vpid_merged))
	    {
	      error_code = file_perm_dealloc (thread_p, page_fhead, &vpid_merged, FILE_ALLOC_TABLE_PAGE);
	      if (error_code != NO_ERROR)
		{
		  ASSERT_ERROR ();
		  return error_code;
		}
	    }
	}

      if (page_ftab != NULL)
	{
	  pgbuf_unfix (thread_p, page_ftab);
	}
    }

  /* done */
  assert (error_code == NO_ERROR);

exit:
  /* system operation must be committed before releasing header page. */
  if (is_sysop_started)
    {
      if (error_code != NO_ERROR)
	{
	  log_sysop_abort (thread_p);
	}
      else
	{
	  if (compensate_or_run_postpone == FILE_RV_DEALLOC_COMPENSATE)
	    {
	      log_sysop_end_logical_compensate (thread_p, &rcv->reference_lsa);
	    }
	  else
	    {
	      log_sysop_end_logical_run_postpone (thread_p, &rcv->reference_lsa);
	    }
	}
    }

  if (page_fhead != NULL)
    {
      pgbuf_unfix (thread_p, page_fhead);
    }
  return error_code;
}

/*
 * file_rv_dealloc_on_undo () - Deallocate the page on undo (we need to use compensate on system op commit)
 *
 * return	 : Error code
 * thread_p (in) : Thread entry
 * rcv (in)	 : Recovery data
 */
int
file_rv_dealloc_on_undo (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  file_log ("file_rv_dealloc_on_undo", "lsa = %lld|%d", LSA_AS_ARGS (&rcv->reference_lsa));

  return file_rv_dealloc_internal (thread_p, rcv, FILE_RV_DEALLOC_COMPENSATE);
}

/*
 * file_rv_dealloc_on_postpone () - Deallocate the page on do postpone (we need to use run postpone on system op commit)
 *
 * return	 : Error code
 * thread_p (in) : Thread entry
 * rcv (in)	 : Recovery data
 */
int
file_rv_dealloc_on_postpone (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  file_log ("file_rv_dealloc_on_postpone", "lsa = %lld|%d", LSA_AS_ARGS (&rcv->reference_lsa));

  return file_rv_dealloc_internal (thread_p, rcv, FILE_RV_DEALLOC_RUN_POSTPONE);
}

/*
 * file_get_num_user_pages () - Output number of user pages in file
 *
 * return                 : Error code
 * thread_p (in)          : Thread entry
 * vfid (in)              : File identifier
 * n_user_pages_out (out) : Output number of user pages
 */
int
file_get_num_user_pages (THREAD_ENTRY * thread_p, const VFID * vfid, int *n_user_pages_out)
{
  VPID vpid_fhead;
  PAGE_PTR page_fhead;
  FILE_HEADER *fhead;
  int error_code = NO_ERROR;

  FILE_GET_HEADER_VPID (vfid, &vpid_fhead);
  page_fhead = pgbuf_fix (thread_p, &vpid_fhead, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (page_fhead == NULL)
    {
      ASSERT_ERROR_AND_SET (error_code);
      return error_code;
    }

  fhead = (FILE_HEADER *) page_fhead;
  file_header_sanity_check (fhead);

  *n_user_pages_out = fhead->n_page_user;
  pgbuf_unfix (thread_p, page_fhead);

  return NO_ERROR;
}

/*
 * file_check_vpid () - check vpid is one of the file's user pages
 *
 * return           : DISK_INVALID if page does not belong to file, DISK_ERROR for errors and DISK_VALID for successful
 *                    check
 * thread_p (in)    : thread entry
 * vfid (in)        : file identifier
 * vpid_lookup (in) : checked VPID
 */
DISK_ISVALID
file_check_vpid (THREAD_ENTRY * thread_p, const VFID * vfid, const VPID * vpid_lookup)
{
  VPID vpid_fhead;
  PAGE_PTR page_fhead;
  FILE_HEADER *fhead;
  PAGE_PTR page_ftab = NULL;
  VSID vsid_lookup;
  bool found;
  int pos;
  FILE_EXTENSIBLE_DATA *extdata_part_ftab;
  FILE_EXTENSIBLE_DATA *extdata_full_ftab;
  FILE_EXTENSIBLE_DATA *extdata_user_page_ftab;
  DISK_ISVALID isvalid = DISK_VALID;

  FILE_GET_HEADER_VPID (vfid, &vpid_fhead);
  page_fhead = pgbuf_fix (thread_p, &vpid_fhead, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (page_fhead == NULL)
    {
      ASSERT_ERROR ();
      return DISK_ERROR;
    }

  fhead = (FILE_HEADER *) page_fhead;
  file_header_sanity_check (fhead);

  /* first search the VPID in sector tables: partial, then full. */
  VSID_FROM_VPID (&vsid_lookup, vpid_lookup);

  FILE_HEADER_GET_PART_FTAB (fhead, extdata_part_ftab);
  if (file_extdata_search_item (thread_p, &extdata_part_ftab, &vsid_lookup, file_compare_vsids, true, false, &found,
				&pos, &page_ftab) != NO_ERROR)
    {
      ASSERT_ERROR ();
      isvalid = DISK_ERROR;
      goto exit;
    }
  if (found)
    {
      FILE_PARTIAL_SECTOR *partsect;

      partsect = (FILE_PARTIAL_SECTOR *) file_extdata_at (extdata_part_ftab, pos);
      if (file_partsect_is_bit_set (partsect, file_partsect_pageid_to_offset (partsect, vpid_lookup->pageid)))
	{
	  /* ok */
	  /* fall through */
	}
      else
	{
	  /* not ok */
	  assert_release (false);
	  isvalid = DISK_INVALID;
	  goto exit;
	}
    }
  else
    {
      /* Search in full table */
      if (page_ftab != NULL)
	{
	  pgbuf_unfix_and_init (thread_p, page_ftab);
	}
      FILE_HEADER_GET_FULL_FTAB (fhead, extdata_full_ftab);
      if (file_extdata_search_item (thread_p, &extdata_full_ftab, &vsid_lookup, file_compare_vsids, true, false,
				    &found, &pos, &page_ftab) != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  isvalid = DISK_ERROR;
	  goto exit;
	}
      if (!found)
	{
	  /* not ok */
	  assert_release (false);
	  isvalid = DISK_INVALID;
	  goto exit;
	}
    }
  if (page_ftab != NULL)
    {
      pgbuf_unfix_and_init (thread_p, page_ftab);
    }

  if (FILE_IS_NUMERABLE (fhead))
    {
      /* Search in user page table */
      VPID *vpid_in_table;

      FILE_HEADER_GET_USER_PAGE_FTAB (fhead, extdata_user_page_ftab);
      if (file_extdata_search_item (thread_p, &extdata_user_page_ftab, vpid_lookup, file_compare_vpids, false, false,
				    &found, &pos, &page_ftab) != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  isvalid = DISK_ERROR;
	  goto exit;
	}
      if (!found)
	{
	  /* not ok */
	  assert_release (false);
	  isvalid = DISK_INVALID;
	  goto exit;
	}
      /* check not marked as deleted */
      vpid_in_table = (VPID *) file_extdata_at (extdata_user_page_ftab, pos);
      if (FILE_USER_PAGE_IS_MARKED_DELETED (vpid_in_table))
	{
	  /* not ok */
	  assert_release (false);
	  isvalid = DISK_INVALID;
	  goto exit;
	}
      /* ok */
    }

  /* page is part of file */
  assert (isvalid == DISK_VALID);

exit:
  if (page_ftab != NULL)
    {
      pgbuf_unfix (thread_p, page_ftab);
    }
  if (page_fhead != NULL)
    {
      pgbuf_unfix (thread_p, page_fhead);
    }
  return isvalid;
}

/*
 * file_get_type () - Get file type for VFID.
 *
 * return          : Error code
 * thread_p (in)   : Thread entry
 * vfid (in)       : File identifier
 * ftype_out (out) : Output file type
 */
int
file_get_type (THREAD_ENTRY * thread_p, const VFID * vfid, FILE_TYPE * ftype_out)
{
  VPID vpid_fhead;
  PAGE_PTR page_fhead = NULL;
  FILE_HEADER *fhead = NULL;

  assert (vfid != NULL && !VFID_ISNULL (vfid));
  assert (ftype_out != NULL);

  /* read from file header */
  FILE_GET_HEADER_VPID (vfid, &vpid_fhead);
  page_fhead = pgbuf_fix (thread_p, &vpid_fhead, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (page_fhead == NULL)
    {
      int error_code = NO_ERROR;

      ASSERT_ERROR_AND_SET (error_code);
      return error_code;
    }

  fhead = (FILE_HEADER *) page_fhead;
  file_header_sanity_check (fhead);

  *ftype_out = fhead->type;
  assert (*ftype_out != FILE_UNKNOWN_TYPE);

  pgbuf_unfix (thread_p, page_fhead);

  return NO_ERROR;
}

/*
 * file_is_temp () - is file temporary?
 *
 * return        : error code
 * thread_p (in) : thread entry
 * vfid (in)     : file identifier
 * is_temp (out) : true for temporary, false otherwise
 */
int
file_is_temp (THREAD_ENTRY * thread_p, const VFID * vfid, bool * is_temp)
{
  VPID vpid_fhead;
  PAGE_PTR page_fhead = NULL;
  FILE_HEADER *fhead = NULL;

  assert (vfid != NULL && !VFID_ISNULL (vfid));
  assert (is_temp != NULL);

  /* read from file header */
  FILE_GET_HEADER_VPID (vfid, &vpid_fhead);
  page_fhead = pgbuf_fix (thread_p, &vpid_fhead, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (page_fhead == NULL)
    {
      int error_code = NO_ERROR;
      ASSERT_ERROR_AND_SET (error_code);
      return error_code;
    }
  fhead = (FILE_HEADER *) page_fhead;
  file_header_sanity_check (fhead);

  *is_temp = FILE_IS_TEMPORARY (fhead);

  pgbuf_unfix (thread_p, page_fhead);

  return NO_ERROR;
}

/*
 * file_table_collect_ftab_pages () - collect file table pages
 *
 * return                 : error code
 * thread_p (in)          : thread entry
 * page_fhead (in)        : file header page
 * collect_numerable (in) : true to collect also user page table, false otherwise
 * collector_out (out)    : output collected table pages
 */
STATIC_INLINE int
file_table_collect_ftab_pages (THREAD_ENTRY * thread_p, PAGE_PTR page_fhead, bool collect_numerable,
			       FILE_FTAB_COLLECTOR * collector_out)
{
  VPID vpid_fhead;
  FILE_HEADER *fhead = NULL;
  FILE_EXTENSIBLE_DATA *extdata_ftab;
  int error_code;

  fhead = (FILE_HEADER *) page_fhead;

  /* init collector */
  collector_out->partsect_ftab =
    (FILE_PARTIAL_SECTOR *) db_private_alloc (thread_p, sizeof (FILE_PARTIAL_SECTOR) * fhead->n_page_ftab);
  if (collector_out->partsect_ftab == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      sizeof (FILE_PARTIAL_SECTOR) * fhead->n_page_ftab);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  /* add page header */
  pgbuf_get_vpid (page_fhead, &vpid_fhead);
  VSID_FROM_VPID (&collector_out->partsect_ftab[0].vsid, &vpid_fhead);
  collector_out->partsect_ftab[0].page_bitmap = FILE_EMPTY_PAGE_BITMAP;
  file_partsect_set_bit (&collector_out->partsect_ftab[0],
			 file_partsect_pageid_to_offset (&collector_out->partsect_ftab[0], vpid_fhead.pageid));
  collector_out->nsects = 1;
  collector_out->npages = 1;

  file_log ("file_temp_reset_user_pages",
	    "init collector with page %d|%d \n "
	    FILE_PARTSECT_MSG ("partsect"), VPID_AS_ARGS (&vpid_fhead),
	    FILE_PARTSECT_AS_ARGS (collector_out->partsect_ftab));

  /* add other pages in partial sector table */
  FILE_HEADER_GET_PART_FTAB (fhead, extdata_ftab);
  error_code = file_extdata_apply_funcs (thread_p, extdata_ftab, file_extdata_collect_ftab_pages, collector_out, NULL,
					 NULL, false, NULL, NULL);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      db_private_free_and_init (thread_p, collector_out->partsect_ftab);
      return error_code;
    }

  if (!FILE_IS_TEMPORARY (fhead))
    {
      /* add other pages from full sector table */
      FILE_HEADER_GET_FULL_FTAB (fhead, extdata_ftab);
      error_code = file_extdata_apply_funcs (thread_p, extdata_ftab, file_extdata_collect_ftab_pages, collector_out,
					     NULL, NULL, false, NULL, NULL);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  db_private_free_and_init (thread_p, collector_out->partsect_ftab);
	  return error_code;
	}
    }

  if (collect_numerable && FILE_IS_NUMERABLE (fhead))
    {
      /* add other pages from user-page table */
      FILE_HEADER_GET_USER_PAGE_FTAB (fhead, extdata_ftab);
      error_code = file_extdata_apply_funcs (thread_p, extdata_ftab, file_extdata_collect_ftab_pages, collector_out,
					     NULL, NULL, false, NULL, NULL);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  db_private_free_and_init (thread_p, collector_out->partsect_ftab);
	  return error_code;
	}
    }

  return NO_ERROR;
}

/*
 * file_extdata_collect_ftab_pages () - FILE_EXTDATA_FUNC to collect pages used for file table
 *
 * return        : NO_ERROR
 * thread_p (in) : thread entry
 * extdata (in)  : extensible data
 * stop (in)     : ignored
 * args (in/out) : FILE_FTAB_COLLECTOR * - file table page collector
 */
static int
file_extdata_collect_ftab_pages (THREAD_ENTRY * thread_p, const FILE_EXTENSIBLE_DATA * extdata, bool * stop, void *args)
{
  FILE_FTAB_COLLECTOR *collect = (FILE_FTAB_COLLECTOR *) args;
  VSID vsid_this;
  int idx_sect = 0;

  if (!LSA_ISNULL (&extdata->vpid_next))
    {
      VSID_FROM_VPID (&vsid_this, &extdata->vpid_next);

      /* find in collected sectors */
      for (idx_sect = 0; idx_sect < collect->nsects; idx_sect++)
	{
	  if (file_compare_vsids (&vsid_this, &collect->partsect_ftab->vsid) == 0)
	    {
	      break;
	    }
	}

      if (idx_sect == collect->nsects)
	{
	  /* not found, append new sector */
	  collect->partsect_ftab->vsid = vsid_this;
	  collect->partsect_ftab->page_bitmap = FILE_EMPTY_PAGE_BITMAP;
	  collect->nsects++;

	  file_log ("file_extdata_collect_ftab_pages",
		    "add new vsid %d|%d, nsects = %d", VSID_AS_ARGS (&vsid_this), collect->nsects);
	}
      file_partsect_set_bit (&collect->partsect_ftab[idx_sect],
			     file_partsect_pageid_to_offset (&collect->partsect_ftab[idx_sect],
							     extdata->vpid_next.pageid));

      file_log ("file_extdata_collect_ftab_pages",
		"collect ftab page %d|%d, \n" FILE_PARTSECT_MSG ("partsect"),
		VPID_AS_ARGS (&extdata->vpid_next), FILE_PARTSECT_AS_ARGS (&collect->partsect_ftab[idx_sect]));
      collect->npages++;
    }

  return NO_ERROR;
}

/*
 * file_table_collector_has_page () - check if page is in collected file table pages
 *
 * return         : true if page is file table page, false otherwise
 * collector (in) : file table pages collector
 * vpid (in)      : page id
 */
STATIC_INLINE bool
file_table_collector_has_page (FILE_FTAB_COLLECTOR * collector, VPID * vpid)
{
  int iter;
  VSID vsid;

  VSID_FROM_VPID (&vsid, vpid);

  for (iter = 0; iter < collector->nsects; iter++)
    {
      if (VSID_EQ (&vsid, &collector->partsect_ftab[iter].vsid))
	{
	  return file_partsect_is_bit_set (&collector->partsect_ftab[iter],
					   file_partsect_pageid_to_offset (&collector->partsect_ftab[iter],
									   vpid->pageid));
	}
    }

  return false;
}

/*
 * file_sector_map_pages () - FILE_EXTDATA_ITEM_FUNC used for mapping a function on all user pages
 *
 * return        : error code
 * thread_p (in) : thread entry
 * data (in)     : FILE_PARTIAL_SECTOR or VSID
 * index (in)    : ignored
 * stop (out)    : output true when to stop the mapping
 * args (in)     : map context
 */
static int
file_sector_map_pages (THREAD_ENTRY * thread_p, const void *data, int index, bool * stop, void *args)
{
  FILE_MAP_CONTEXT *context = (FILE_MAP_CONTEXT *) args;
  FILE_PARTIAL_SECTOR partsect;
  int iter;
  VPID vpid;
  PAGE_PTR page = NULL;
  int error_code = NO_ERROR;

  assert (context != NULL);
  assert (context->stop == false);
  assert (context->func != NULL);
  assert (context->latch_mode == PGBUF_LATCH_WRITE || context->latch_mode == PGBUF_LATCH_READ);
  assert (context->latch_cond == PGBUF_CONDITIONAL_LATCH || context->latch_cond == PGBUF_UNCONDITIONAL_LATCH);

  /* how this works:
   * get all allocated pages from sector in file table. data can be either a partial sector (vsid + allocation bitmap)
   * or a vsid (full sector - all its pages are allocated).
   * for each page, we have to check first it is not a table page. we only want to map the function for user pages.
   *
   * if the page cannot be fixed conditionally, it is simply skipped. careful when using unconditional latch in server
   * mode. there is a risk of dead-latches.
   *
   * map function can keep the page, but it must set the page pointer to NULL.
   */

  /* hack to know this is partial table or full table */
  if (context->is_partial)
    {
      partsect = *(FILE_PARTIAL_SECTOR *) data;
    }
  else
    {
      partsect.vsid = *(VSID *) data;
    }

  vpid.volid = partsect.vsid.volid;
  for (iter = 0, vpid.pageid = SECTOR_FIRST_PAGEID (partsect.vsid.sectid); iter < FILE_ALLOC_BITMAP_NBITS;
       iter++, vpid.pageid++)
    {
      if (context->is_partial && !file_partsect_is_bit_set (&partsect, iter))
	{
	  /* not allocated */
	  continue;
	}

      if (file_table_collector_has_page (&context->ftab_collector, &vpid))
	{
	  /* skip table pages */
	  continue;
	}

      page = pgbuf_fix (thread_p, &vpid, OLD_PAGE, context->latch_mode, context->latch_cond);
      if (page == NULL)
	{
	  if (context->latch_cond == PGBUF_CONDITIONAL_LATCH)
	    {
	      /* exit gracefully */
	      return NO_ERROR;
	    }
	  /* error */
	  ASSERT_ERROR_AND_SET (error_code);
	  return error_code;
	}

      assert (pgbuf_get_page_ptype (thread_p, page) != PAGE_FTAB);

      /* call map function */
      error_code = context->func (thread_p, &page, stop, context->args);
      if (page != NULL)
	{
	  pgbuf_unfix_and_init (thread_p, page);
	}
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}

      if (*stop)
	{
	  /* early out */
	  context->stop = true;
	  return NO_ERROR;
	}
    }

  return NO_ERROR;
}

/*
 * file_map_pages () - map given function on all user pages
 *
 * return          : error code
 * thread_p (in)   : thread entry
 * vfid (in)       : file identifier
 * latch_mode (in) : latch mode
 * latch_cond (in) : latch condition
 * func (in)       : map function
 * args (in)       : map function arguments
 */
int
file_map_pages (THREAD_ENTRY * thread_p, const VFID * vfid, PGBUF_LATCH_MODE latch_mode,
		PGBUF_LATCH_CONDITION latch_cond, FILE_MAP_PAGE_FUNC func, void *args)
{
  VPID vpid_fhead;
  PAGE_PTR page_fhead = NULL;
  FILE_HEADER *fhead = NULL;
  FILE_EXTENSIBLE_DATA *extdata_ftab;
  FILE_MAP_CONTEXT context;
  int error_code = NO_ERROR;

  assert (vfid != NULL && !VFID_ISNULL (vfid));
  assert (latch_mode == PGBUF_LATCH_READ || latch_mode == PGBUF_LATCH_WRITE);
  assert (func != NULL);

  /* how it works:
   * get all user pages in partial sector table and full sector table and apply function. the file_sector_map_pages
   * is first mapped on entries of partial table and then on full table.
   *
   * note that file header is read-latched during the whole time. this means page allocations/deallocations are blocked
   * during the process. careful when using the function in server-mode. it is not advisable to be used for hot and
   * large files.
   *
   * function must be mapped on user pages only. as a first step we have to collect file table pages. while mapping
   * function on pages, these are skipped.
   */

#if defined (SERVER_MODE)
  /* we cannot use unconditional latch. allocations can sometimes keep latch on a file page and then try to lock
   * header/table. we may cause dead latch.
   */
  assert (latch_cond == PGBUF_CONDITIONAL_LATCH);
  latch_cond = PGBUF_CONDITIONAL_LATCH;
#endif /* SERVER_MODE */

  FILE_GET_HEADER_VPID (vfid, &vpid_fhead);
  page_fhead = pgbuf_fix (thread_p, &vpid_fhead, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (page_fhead == NULL)
    {
      ASSERT_ERROR_AND_SET (error_code);
      return error_code;
    }
  fhead = (FILE_HEADER *) page_fhead;
  file_header_sanity_check (fhead);

  /* init map context */
  context.func = func;
  context.args = args;
  context.latch_cond = latch_cond;
  context.latch_mode = latch_mode;
  context.stop = false;
  context.ftab_collector.partsect_ftab = NULL;

  /* collect table pages */
  error_code = file_table_collect_ftab_pages (thread_p, page_fhead, true, &context.ftab_collector);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto exit;
    }

  /* map over partial sectors table */
  FILE_HEADER_GET_PART_FTAB (fhead, extdata_ftab);
  context.is_partial = true;
  error_code = file_extdata_apply_funcs (thread_p, extdata_ftab, NULL, NULL, file_sector_map_pages, &context, false,
					 NULL, NULL);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto exit;
    }
  if (context.stop)
    {
      goto exit;
    }

  if (!FILE_IS_TEMPORARY (fhead))
    {
      /* map over full table */
      context.is_partial = false;
      FILE_HEADER_GET_FULL_FTAB (fhead, extdata_ftab);
      error_code = file_extdata_apply_funcs (thread_p, extdata_ftab, NULL, NULL, file_sector_map_pages, &context,
					     false, NULL, NULL);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto exit;
	}
    }

  assert (error_code == NO_ERROR);

exit:
  if (page_fhead != NULL)
    {
      pgbuf_unfix (thread_p, page_fhead);
    }
  if (context.ftab_collector.partsect_ftab != NULL)
    {
      db_private_free (thread_p, context.ftab_collector.partsect_ftab);
    }

  return error_code;
}

/*
 * file_table_check () - check file table is valid
 *
 * return                  : DISK_INVALID for unexpected errors, DISK_ERROR for expected errors,
 *                           DISK_VALID for successful check
 * thread_p (in)           : thread entry
 * vfid (in)               : file identifier
 * disk_map_clone (in/out) : clone of disk sector table maps used in stand-alone mode only to cross-check reserved
 *                           sectors
 */
static DISK_ISVALID
file_table_check (THREAD_ENTRY * thread_p, const VFID * vfid, DISK_VOLMAP_CLONE * disk_map_clone)
{
  VPID vpid_fhead;
  PAGE_PTR page_fhead = NULL;
  FILE_HEADER *fhead = NULL;
  FILE_VSID_COLLECTOR collector;
#if defined (SA_MODE)
  int iter_vsid;
#endif /* SA_MODE */

  DISK_ISVALID valid = DISK_VALID;
  DISK_ISVALID allvalid = DISK_VALID;
  int error_code = NO_ERROR;

#if defined (SA_MODE)
  assert (disk_map_clone != NULL);
#endif /* SA_MODE */

  FILE_GET_HEADER_VPID (vfid, &vpid_fhead);
  page_fhead = pgbuf_fix (thread_p, &vpid_fhead, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (page_fhead == NULL)
    {
      ASSERT_ERROR ();
      return DISK_ERROR;
    }
  fhead = (FILE_HEADER *) page_fhead;
  file_header_sanity_check (fhead);

  error_code = file_table_collect_all_vsids (thread_p, page_fhead, &collector);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto exit;
    }

  if (FILE_IS_NUMERABLE (fhead))
    {
      /* check all pages in user table belong to collected page */
      FILE_EXTENSIBLE_DATA *extdata_user_page_ftab;

      FILE_HEADER_GET_USER_PAGE_FTAB (fhead, extdata_user_page_ftab);
      error_code = file_extdata_apply_funcs (thread_p, extdata_user_page_ftab, NULL, NULL,
					     file_table_check_page_is_in_sectors, &collector, false, NULL, NULL);
      if (error_code == ER_FAILED)
	{
	  /* set for unexpected cases */
	  allvalid = DISK_INVALID;
	  /* fall through: also check sectors are reserved */
	}
      else if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto exit;
	}
    }

  /* check all sectors are reserved. if the file is very big, this can take a while. don't keep header page fixed */
  pgbuf_unfix_and_init (thread_p, page_fhead);

#if defined (SERVER_MODE)
  valid = disk_check_sectors_are_reserved (thread_p, collector.vsids, collector.n_vsids);
  if (valid != DISK_VALID && allvalid == DISK_VALID)
    {
      allvalid = valid;
    }
#else	/* !SERVER_MODE */		   /* SA_MODE */
  for (iter_vsid = 0; iter_vsid < collector.n_vsids; iter_vsid++)
    {
      valid = disk_map_clone_clear (&collector.vsids[iter_vsid], disk_map_clone);
      if (valid == DISK_INVALID)
	{
	  allvalid = DISK_INVALID;
	}
      else
	{
	  assert (valid == DISK_VALID);
	}
    }
#endif /* SA_MODE */

exit:
  if (page_fhead != NULL)
    {
      pgbuf_unfix (thread_p, page_fhead);
    }

  if (error_code != NO_ERROR && allvalid == DISK_VALID)
    {
      allvalid = DISK_ERROR;
    }

  if (collector.vsids != NULL)
    {
      db_private_free (thread_p, collector.vsids);
    }
  return allvalid;
}

/*
 * file_dump () - file dump
 *
 * return        : error code
 * thread_p (in) : thread entry
 * vfid (in)     : file identifier
 * fp (in)       : output file
 */
int
file_dump (THREAD_ENTRY * thread_p, const VFID * vfid, FILE * fp)
{
  VPID vpid_fhead;
  PAGE_PTR page_fhead = NULL;
  FILE_HEADER *fhead = NULL;
  int error_code = NO_ERROR;

  fprintf (fp, "\n\n Dumping file %d|%d \n", VFID_AS_ARGS (vfid));

  FILE_GET_HEADER_VPID (vfid, &vpid_fhead);
  page_fhead = pgbuf_fix (thread_p, &vpid_fhead, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (page_fhead == NULL)
    {
      ASSERT_ERROR_AND_SET (error_code);
      return error_code;
    }
  fhead = (FILE_HEADER *) page_fhead;

  file_header_dump (thread_p, fhead, fp);

  error_code = file_table_dump (thread_p, fhead, fp);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      pgbuf_unfix (thread_p, page_fhead);
      return error_code;
    }

  pgbuf_unfix (thread_p, page_fhead);
  return NO_ERROR;
}

/*
 * file_table_dump () - dump file table
 *
 * return        : error code
 * thread_p (in) : thread entry
 * fhead (in)    : file header
 * fp (in)       : output file
 */
STATIC_INLINE int
file_table_dump (THREAD_ENTRY * thread_p, const FILE_HEADER * fhead, FILE * fp)
{
  int error_code = NO_ERROR;
  FILE_EXTENSIBLE_DATA *extdata_ftab = NULL;

  fprintf (fp, "FILE TABLE: \n");
  FILE_HEADER_GET_PART_FTAB (fhead, extdata_ftab);
  error_code = file_extdata_apply_funcs (thread_p, extdata_ftab, file_partial_table_extdata_dump, fp,
					 file_partial_table_item_dump, fp, false, NULL, NULL);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  if (!FILE_IS_TEMPORARY (fhead))
    {
      FILE_HEADER_GET_FULL_FTAB (fhead, extdata_ftab);
      error_code = file_extdata_apply_funcs (thread_p, extdata_ftab, file_full_table_extdata_dump, fp,
					     file_full_table_item_dump, fp, false, NULL, NULL);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}
    }

  if (FILE_IS_NUMERABLE (fhead))
    {
      FILE_HEADER_GET_USER_PAGE_FTAB (fhead, extdata_ftab);
      error_code = file_extdata_apply_funcs (thread_p, extdata_ftab, file_full_table_extdata_dump, fp,
					     file_full_table_item_dump, fp, false, NULL, NULL);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}
    }

  return NO_ERROR;
}

/*
 * file_partial_table_extdata_dump () - dump an extensible data from partial table
 *
 * return        : NO_ERROR
 * thread_p (in) : thread entry
 * extdata (in)  : extensible data
 * stop (in)     : not used
 * args (in)     : FILE *
 */
static int
file_partial_table_extdata_dump (THREAD_ENTRY * thread_p, const FILE_EXTENSIBLE_DATA * extdata, bool * stop, void *args)
{
  FILE *fp = (FILE *) args;

  fprintf (fp, FILE_EXTDATA_MSG ("partial table component"), FILE_EXTDATA_AS_ARGS (extdata));
  return NO_ERROR;
}

/*
 * file_partial_table_item_dump () - dump an item from partial table
 *
 * return        : NO_ERROR
 * thread_p (in) : thread entry
 * data (in)     : FILE_PARTIAL_SECTOR *
 * index (in)    : item index
 * stop (in)     : not used
 * args (in)     : FILE *
 */
static int
file_partial_table_item_dump (THREAD_ENTRY * thread_p, const void *data, int index, bool * stop, void *args)
{
  FILE *fp = (FILE *) args;
  FILE_PARTIAL_SECTOR *partsect;
  int iter;
  int line_count;
  VPID vpid;

  partsect = (FILE_PARTIAL_SECTOR *) data;
  fprintf (fp, FILE_PARTSECT_MSG ("partially allocated sector"), FILE_PARTSECT_AS_ARGS (partsect));

  vpid.volid = partsect->vsid.volid;
  fprintf (fp, "\t\t allocated pages:");
  line_count = 0;
  for (iter = 0, vpid.pageid = SECTOR_FIRST_PAGEID (partsect->vsid.sectid); iter < DISK_SECTOR_NPAGES;
       iter++, vpid.pageid++)
    {
      if (file_partsect_is_bit_set (partsect, iter))
	{
	  if (line_count++ % 8 == 0)
	    {
	      fprintf (fp, "\n\t\t\t");
	    }
	  fprintf (fp, "%5d|%10d ", VPID_AS_ARGS (&vpid));
	}
    }

  fprintf (fp, "\n\t\t reserved pages:");
  line_count = 0;
  for (iter = 0, vpid.pageid = SECTOR_FIRST_PAGEID (partsect->vsid.sectid); iter < DISK_SECTOR_NPAGES;
       iter++, vpid.pageid++)
    {
      if (!file_partsect_is_bit_set (partsect, iter))
	{
	  if (line_count++ % 8 == 0)
	    {
	      fprintf (fp, "\n\t\t\t");
	    }
	  fprintf (fp, "%5d|%10d ", VPID_AS_ARGS (&vpid));
	}
    }
  fprintf (fp, "\n");

  return NO_ERROR;
}

/*
 * file_full_table_extdata_dump () - dump an extensible data from full table
 *
 * return        : NO_ERROR
 * thread_p (in) : thread entry
 * extdata (in)  : extensible data
 * stop (in)     : not used
 * args (in)     : FILE *
 */
static int
file_full_table_extdata_dump (THREAD_ENTRY * thread_p, const FILE_EXTENSIBLE_DATA * extdata, bool * stop, void *args)
{
  FILE *fp = (FILE *) args;

  fprintf (fp, FILE_EXTDATA_MSG ("full table component"), FILE_EXTDATA_AS_ARGS (extdata));
  return NO_ERROR;
}

/*
 * file_full_table_item_dump () - dump an item from full table
 *
 * return        : NO_ERROR
 * thread_p (in) : thread entry
 * data (in)     : VSID *
 * index (in)    : item index
 * stop (in)     : not used
 * args (in)     : FILE *
 */
static int
file_full_table_item_dump (THREAD_ENTRY * thread_p, const void *data, int index, bool * stop, void *args)
{
  FILE *fp = (FILE *) args;
  VSID *vsid;
  int iter;
  int line_count;
  VPID vpid;

  vsid = (VSID *) data;
  fprintf (fp, "fully allocated sector: vsid = %d|%d \n", VSID_AS_ARGS (vsid));

  line_count = 0;
  vpid.volid = vsid->volid;
  for (iter = 0, vpid.pageid = SECTOR_FIRST_PAGEID (vsid->sectid); iter < DISK_SECTOR_NPAGES; iter++, vpid.pageid++)
    {
      if (line_count++ % 8 == 0)
	{
	  fprintf (fp, "\n\t\t\t");
	}
      fprintf (fp, "%5d|%10d ", VPID_AS_ARGS (&vpid));
    }
  return NO_ERROR;
}

/*
 * file_user_page_table_extdata_dump () - dump an extensible data from user page table
 *
 * return        : NO_ERROR
 * thread_p (in) : thread entry
 * extdata (in)  : extensible data
 * stop (in)     : not used
 * args (in)     : FILE *
 */
static int
file_user_page_table_extdata_dump (THREAD_ENTRY * thread_p, const FILE_EXTENSIBLE_DATA * extdata, bool * stop,
				   void *args)
{
  FILE *fp = (FILE *) args;

  fprintf (fp, FILE_EXTDATA_MSG ("user page table component"), FILE_EXTDATA_AS_ARGS (extdata));
  return NO_ERROR;
}

/*
 * file_user_page_table_item_dump () - dump an item from user page table
 *
 * return        : NO_ERROR
 * thread_p (in) : thread entry
 * data (in)     : VPID *
 * index (in)    : item index
 * stop (in)     : not used
 * args (in)     : FILE *
 */
static int
file_user_page_table_item_dump (THREAD_ENTRY * thread_p, const void *data, int index, bool * stop, void *args)
{
  FILE *fp = (FILE *) args;
  VPID *vpid = (VPID *) data;

  if (index % 8 == 0)
    {
      fprintf (fp, "\n\t\t\t");
    }
  if (FILE_USER_PAGE_IS_MARKED_DELETED (vpid))
    {
      fprintf (fp, "\n WARNING: page %d|%d is marked as deleted!! \n\t\t\t", VPID_AS_ARGS (vpid));
    }
  fprintf (fp, "%5d|%10d ", VPID_AS_ARGS (vpid));

  return NO_ERROR;
}

/************************************************************************/
/* Numerable files section.                                             */
/************************************************************************/

/*
 * file_numerable_add_page () - Add a page at the end of user page table. This is part of the implementation for
 *				numerable files.
 *
 * return	   : Error code
 * thread_p (in)   : Thread entry
 * page_fhead (in) : File header page
 * vpid (in)	   : VPID of page to add to use page table.
 */
static int
file_numerable_add_page (THREAD_ENTRY * thread_p, PAGE_PTR page_fhead, const VPID * vpid)
{
  FILE_HEADER *fhead = (FILE_HEADER *) page_fhead;
  FILE_EXTENSIBLE_DATA *extdata_user_page_ftab = NULL;
  VPID vpid_fhead;
  PAGE_PTR page_ftab = NULL;
  PAGE_PTR page_extdata = NULL;
  LOG_LSA save_lsa;
  int error_code = NO_ERROR;

  assert (fhead != NULL);
  assert (vpid != NULL && !VPID_ISNULL (vpid));
  assert (FILE_IS_NUMERABLE (fhead));

  /* how it works:
   * we need to add the page at the end of user page table. the order of pages in numerable files is the same as their
   * allocation order, so we always add a page at the end.
   *
   * note: fhead->vpid_last_user_page_ftab is a hint to last page in user page table, where the VPID should be added.
   */

  FILE_GET_HEADER_VPID (&fhead->self, &vpid_fhead);

  /* we have a hint on where the page must be added */
  if (VPID_EQ (&fhead->vpid_last_user_page_ftab, &vpid_fhead))
    {
      /* hint points to file header */
      page_extdata = page_fhead;
      FILE_HEADER_GET_USER_PAGE_FTAB (fhead, extdata_user_page_ftab);
    }
  else
    {
      /* hint points to another table page. */
      page_ftab =
	pgbuf_fix (thread_p, &fhead->vpid_last_user_page_ftab, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
      if (page_ftab == NULL)
	{
	  ASSERT_ERROR_AND_SET (error_code);
	  goto exit;
	}
      page_extdata = page_ftab;
      extdata_user_page_ftab = (FILE_EXTENSIBLE_DATA *) page_ftab;
    }

  file_log ("file_numerable_add_page",
	    "file %d|%d add page %d|%d to user page table \n"
	    FILE_HEAD_FULL_MSG
	    FILE_EXTDATA_MSG ("last user page table component"),
	    VFID_AS_ARGS (&fhead->self), VPID_AS_ARGS (vpid),
	    FILE_HEAD_FULL_AS_ARGS (fhead), FILE_EXTDATA_AS_ARGS (extdata_user_page_ftab));

  if (file_extdata_is_full (extdata_user_page_ftab))
    {
      /* no more room for new pages. allocate a new table page. */
      VPID vpid_ftab_new = VPID_INITIALIZER;

      if (FILE_IS_TEMPORARY (fhead))
	{
	  /* we can have temporary numerable files */
	  error_code = file_temp_alloc (thread_p, page_fhead, FILE_ALLOC_TABLE_PAGE, &vpid_ftab_new);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      goto exit;
	    }
	}
      else
	{
	  error_code = file_perm_alloc (thread_p, page_fhead, FILE_ALLOC_TABLE_PAGE, &vpid_ftab_new);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      goto exit;
	    }
	}
      assert (!VPID_ISNULL (&vpid_ftab_new));

      /* create a link from previous page table. */
      if (!FILE_IS_TEMPORARY (fhead))
	{
	  /* log setting next page */
	  file_log_extdata_set_next (thread_p, extdata_user_page_ftab, page_extdata, &vpid_ftab_new);
	}
      else
	{
	  /* just set dirty */
	  pgbuf_set_dirty (thread_p, page_extdata, DONT_FREE);
	}
      VPID_COPY (&extdata_user_page_ftab->vpid_next, &vpid_ftab_new);
      if (page_ftab != NULL)
	{
	  /* we don't need it anymore */
	  pgbuf_set_dirty_and_free (thread_p, page_ftab);
	}

      /* fix new table page */
      page_ftab = pgbuf_fix (thread_p, &vpid_ftab_new, NEW_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
      if (page_ftab == NULL)
	{
	  ASSERT_ERROR_AND_SET (error_code);
	  goto exit;
	}
      page_extdata = page_ftab;
      pgbuf_set_page_ptype (thread_p, page_ftab, PAGE_FTAB);

      save_lsa = *pgbuf_get_lsa (page_fhead);
      if (!FILE_IS_TEMPORARY (fhead))
	{
	  /* log that we are going to change fhead->vpid_last_page_ftab. */
	  log_append_undoredo_data2 (thread_p, RVFL_FHEAD_SET_LAST_USER_PAGE_FTAB, NULL, page_fhead, 0, sizeof (VPID),
				     sizeof (VPID), &fhead->vpid_last_user_page_ftab, &vpid_ftab_new);
	}

      VPID_COPY (&fhead->vpid_last_user_page_ftab, &vpid_ftab_new);
      pgbuf_set_dirty (thread_p, page_fhead, DONT_FREE);

      file_log ("file_numerable_add_page",
		"file %d|%d added new page %d|%d to user table; "
		"updated vpid_last_user_page_ftab in header page %d|%d, prev_lsa %lld|%d, crt_lsa %lld|%d ",
		VFID_AS_ARGS (&fhead->self), VPID_AS_ARGS (&vpid_ftab_new),
		PGBUF_PAGE_VPID_AS_ARGS (page_fhead), LSA_AS_ARGS (&save_lsa), PGBUF_PAGE_LSA_AS_ARGS (page_fhead));

      /* initialize new page table */
      extdata_user_page_ftab = (FILE_EXTENSIBLE_DATA *) page_ftab;
      file_extdata_init (sizeof (VPID), DB_PAGESIZE, extdata_user_page_ftab);

      if (!FILE_IS_TEMPORARY (fhead))
	{
	  /* log changes. */
	  pgbuf_log_new_page (thread_p, page_ftab, file_extdata_size (extdata_user_page_ftab), PAGE_FTAB);
	}
      else
	{
	  /* just set dirty */
	  pgbuf_set_dirty (thread_p, page_ftab, DONT_FREE);
	}
    }

  assert (!file_extdata_is_full (extdata_user_page_ftab));
  save_lsa = *pgbuf_get_lsa (page_extdata);
  if (!FILE_IS_TEMPORARY (fhead))
    {
      /* log changes */
      file_log_extdata_add (thread_p, extdata_user_page_ftab, page_extdata,
			    file_extdata_item_count (extdata_user_page_ftab), 1, vpid);
    }
  else
    {
      /* just set dirty */
      pgbuf_set_dirty (thread_p, page_extdata, DONT_FREE);
    }
  file_extdata_append (extdata_user_page_ftab, vpid);

  file_log ("file_numerable_add_page",
	    "add page %d|%d to position %d in file %d|%d, page %d|%d, prev_lsa = %lld|%d, crt_lsa = %lld|%d \n"
	    FILE_EXTDATA_MSG ("last user page table component") FILE_HEAD_FULL_MSG, VPID_AS_ARGS (vpid),
	    file_extdata_item_count (extdata_user_page_ftab) - 1, VFID_AS_ARGS (&fhead->self),
	    PGBUF_PAGE_VPID_AS_ARGS (page_extdata), LSA_AS_ARGS (&save_lsa), PGBUF_PAGE_LSA_AS_ARGS (page_extdata),
	    FILE_EXTDATA_AS_ARGS (extdata_user_page_ftab), FILE_HEAD_FULL_AS_ARGS (fhead));

  /* done */
  assert (error_code == NO_ERROR);

exit:
  if (page_ftab != NULL)
    {
      assert (page_ftab != page_fhead);
      pgbuf_unfix (thread_p, page_ftab);
    }

  return error_code;
}

/*
 * file_extdata_find_nth_vpid () - Function callable by file_extdata_apply_funcs. An optimal way of finding nth page
 *				   when there is no page marked as deleted.
 *
 * return	 : NO_ERROR
 * thread_p (in) : Thread entry
 * extdata (in)	 : Extensible data
 * stop (in)	 : Output true when search must be stopped
 * args (in)	 : FILE_FIND_NTH_CONTEXT *
 */
static int
file_extdata_find_nth_vpid (THREAD_ENTRY * thread_p, const FILE_EXTENSIBLE_DATA * extdata, bool * stop, void *args)
{
  FILE_FIND_NTH_CONTEXT *find_nth_context = (FILE_FIND_NTH_CONTEXT *) args;
  int count_vpid = file_extdata_item_count (extdata);

  if (count_vpid <= find_nth_context->nth)
    {
      /* not in this extensible data. continue searching. */
      find_nth_context->nth -= count_vpid;
    }
  else
    {
      /* nth VPID is in this extensible data. get it and stop searching */
      VPID_COPY (find_nth_context->vpid_nth, (VPID *) file_extdata_at (extdata, find_nth_context->nth));
      assert (!FILE_USER_PAGE_IS_MARKED_DELETED (find_nth_context->vpid_nth));
      *stop = true;
    }

  return NO_ERROR;
}

/*
 * file_extdata_find_nth_vpid_and_skip_marked () - Function callable by file_extdata_apply_funcs. Used to find the nth
 *						   VPID not marked as deleted (numerable files).
 *
 * return        : NO_ERROR
 * thread_p (in) : Thread entry
 * data (in)	 : Pointer in user page table (VPID *).
 * index (in)	 : Not used.
 * stop (out)	 : Output true when nth page is reached.
 * args (in/out) : FILE_FIND_NTH_CONTEXT *
 */
static int
file_extdata_find_nth_vpid_and_skip_marked (THREAD_ENTRY * thread_p,
					    const void *data, int index, bool * stop, void *args)
{
  FILE_FIND_NTH_CONTEXT *find_nth_context = (FILE_FIND_NTH_CONTEXT *) args;
  VPID *vpidp = (VPID *) data;

  if (FILE_USER_PAGE_IS_MARKED_DELETED (vpidp))
    {
      /* skip marked deleted */
      return NO_ERROR;
    }

  if (find_nth_context->nth == 0)
    {
      /* found nth */
      *find_nth_context->vpid_nth = *vpidp;
      *stop = true;
    }
  else
    {
      /* update nth */
      find_nth_context->nth--;
    }

  return NO_ERROR;
}

/*
 * file_numerable_find_nth () - Find nth page VPID in numerable file.
 *
 * return	   : Error code
 * thread_p (in)   : Thread entry
 * vfid (in)	   : File identifier
 * nth (in)	   : Index of page
 * auto_alloc (in) : True to allow file extension.
 * vpid_nth (out)  : VPID at index
 */
int
file_numerable_find_nth (THREAD_ENTRY * thread_p, const VFID * vfid, int nth, bool auto_alloc, VPID * vpid_nth)
{
  VPID vpid_fhead;
  PAGE_PTR page_fhead = NULL;
  FILE_HEADER *fhead = NULL;
  FILE_EXTENSIBLE_DATA *extdata_user_page_ftab = NULL;
  FILE_FIND_NTH_CONTEXT find_nth_context;
  int error_code = NO_ERROR;

  assert (vfid != NULL && !VFID_ISNULL (vfid));
  assert (nth >= 0);
  assert (vpid_nth != NULL);

  VPID_SET_NULL (vpid_nth);

  /* how it works:
   * iterate through user page table, skipping pages marked as deleted, and decrement counter until it reaches 0.
   * save the VPID found on the nth position.
   * if there are no marked deleted pages, we can skip entire extensible data components and go directly to nth page.
   */

  /* get file header */
  FILE_GET_HEADER_VPID (vfid, &vpid_fhead);
  page_fhead = pgbuf_fix (thread_p, &vpid_fhead, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (page_fhead == NULL)
    {
      ASSERT_ERROR_AND_SET (error_code);
      goto exit;
    }
  fhead = (FILE_HEADER *) page_fhead;
  file_header_sanity_check (fhead);
  assert (nth < fhead->n_page_user || (auto_alloc && nth == fhead->n_page_user));

  if (auto_alloc && nth == (fhead->n_page_user - fhead->n_page_mark_delete))
    {
      /* we need new page */
      /* todo: can this be simplified? */
      error_code = pgbuf_promote_read_latch (thread_p, &page_fhead, PGBUF_PROMOTE_SHARED_READER);
      if (error_code == ER_PAGE_LATCH_PROMOTE_FAIL)
	{
	  /* re-fix page */
	  pgbuf_unfix (thread_p, page_fhead);
	  page_fhead = pgbuf_fix (thread_p, &vpid_fhead, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
	  if (page_fhead == NULL)
	    {
	      ASSERT_ERROR_AND_SET (error_code);
	      goto exit;
	    }
	  fhead = (FILE_HEADER *) page_fhead;
	  file_header_sanity_check (fhead);
	  if (auto_alloc && nth == (fhead->n_page_user - fhead->n_page_mark_delete))
	    {
	      error_code = file_alloc (thread_p, vfid, vpid_nth);
	      if (error_code != NO_ERROR)
		{
		  ASSERT_ERROR ();
		}
	      goto exit;
	    }
	}
      else if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto exit;
	}
      else if (page_fhead == NULL)
	{
	  ASSERT_ERROR_AND_SET (error_code);
	  goto exit;
	}

      error_code = file_alloc (thread_p, vfid, vpid_nth);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	}

      goto exit;
    }

  /* iterate in user page table */
  FILE_HEADER_GET_USER_PAGE_FTAB (fhead, extdata_user_page_ftab);
  find_nth_context.vpid_nth = vpid_nth;
  find_nth_context.nth = nth;

  if (fhead->n_page_mark_delete > 0)
    {
      /* we don't know where the marked deleted pages are... we need to iterate through all pages and skip the marked
       * deleted. */
      error_code = file_extdata_apply_funcs (thread_p, extdata_user_page_ftab, NULL, NULL,
					     file_extdata_find_nth_vpid_and_skip_marked, &find_nth_context, false,
					     NULL, NULL);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto exit;
	}
    }
  else
    {
      /* we can go directly to the right VPID. */
      error_code = file_extdata_apply_funcs (thread_p, extdata_user_page_ftab, file_extdata_find_nth_vpid,
					     &find_nth_context, NULL, NULL, false, NULL, NULL);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto exit;
	}
    }

  if (VPID_ISNULL (vpid_nth))
    {
      /* should not happen */
      assert_release (false);
      error_code = ER_FAILED;
      goto exit;
    }

  assert (error_code == NO_ERROR);

exit:
  if (page_fhead != NULL)
    {
      pgbuf_unfix (thread_p, page_fhead);
    }

  return error_code;
}

/*
 * file_rv_user_page_mark_delete () - Recover page mark delete.
 *
 * return	 : Error code
 * thread_p (in) : Thread entry
 * rcv (in)	 : Recovery data
 */
int
file_rv_user_page_mark_delete (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  PAGE_PTR page_ftab = rcv->pgptr;
  VPID *vpid_ptr = NULL;

  vpid_ptr = (VPID *) (page_ftab + rcv->offset);
  assert (!FILE_USER_PAGE_IS_MARKED_DELETED (vpid_ptr));
  FILE_USER_PAGE_MARK_DELETED (vpid_ptr);

  file_log ("file_rv_user_page_mark_delete",
	    "marked deleted vpid %d|%d in page %d|%d lsa %lld|%d at offset %d",
	    VPID_AS_ARGS (vpid_ptr), PGBUF_PAGE_VPID_AS_ARGS (page_ftab),
	    PGBUF_PAGE_LSA_AS_ARGS (page_ftab), rcv->offset);

  pgbuf_set_dirty (thread_p, page_ftab, DONT_FREE);
  return NO_ERROR;
}

/*
 * file_rv_user_page_unmark_delete_logical () - Recover page unmark delete (logical)
 *
 * return	 : Error code
 * thread_p (in) : Thread entry
 * rcv (in)	 : Recovery data
 */
int
file_rv_user_page_unmark_delete_logical (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  PAGE_PTR page_fhead;
  PAGE_PTR page_ftab;
  FILE_HEADER *fhead;
  VFID *vfid;
  VPID *vpid;
  VPID *vpid_in_table;
  VPID vpid_fhead;
  FILE_EXTENSIBLE_DATA *extdata_user_page_ftab;
  LOG_DATA_ADDR addr = LOG_DATA_ADDR_INITIALIZER;
  bool found = false;
  int position = -1;
  int offset = 0;
  LOG_LSA save_lsa;
  int error_code = NO_ERROR;

  /* how it works
   * when we mark a page as deleted in user page table and unfix file header, the user page table can be modified by
   * concurrent transactions. if allocations are always appended at the end, deallocations will remove VPID's from
   * user page table and can sometime merge pages. which means that by the time we get to undo our change, the location
   * of this page becomes uncertain.
   *
   * therefore, removing mark for deletion should lookup VPID in table.
   */

  vfid = (VFID *) (rcv->data + offset);
  offset += sizeof (*vfid);

  vpid = (VPID *) (rcv->data + offset);
  offset += sizeof (*vpid);

  assert (offset == rcv->length);

  vpid_fhead.volid = vfid->volid;
  vpid_fhead.pageid = vfid->fileid;
  page_fhead = pgbuf_fix (thread_p, &vpid_fhead, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (page_fhead == NULL)
    {
      /* Should not be interrupted. */
      assert_release (false);
      return ER_FAILED;
    }

  fhead = (FILE_HEADER *) page_fhead;
  file_header_sanity_check (fhead);

  assert (FILE_IS_NUMERABLE (fhead));
  assert (!FILE_IS_TEMPORARY (fhead));

  /* search for VPID in user page table */
  FILE_HEADER_GET_USER_PAGE_FTAB (fhead, extdata_user_page_ftab);
  error_code =
    file_extdata_search_item (thread_p, &extdata_user_page_ftab, vpid,
			      file_compare_vpids, false, true, &found, &position, &page_ftab);
  if (error_code != NO_ERROR)
    {
      /* errors not expected during recovery. */
      assert_release (false);
      goto exit;
    }
  if (!found)
    {
      /* should be found. */
      assert_release (false);
      error_code = ER_FAILED;
      goto exit;
    }

  /* go to VPID in current extensible data */
  vpid_in_table = (VPID *) file_extdata_at (extdata_user_page_ftab, position);
  assert (FILE_USER_PAGE_IS_MARKED_DELETED (vpid_in_table));

  FILE_USER_PAGE_CLEAR_MARK_DELETED (vpid_in_table);
  assert (VPID_EQ (vpid, vpid_in_table));

  /* compensate logging. */
  addr.pgptr = page_ftab != NULL ? page_ftab : page_fhead;
  addr.offset = (PGLENGTH) (((char *) vpid_in_table) - addr.pgptr);
  save_lsa = *pgbuf_get_lsa (addr.pgptr);
  log_append_compensate (thread_p, RVFL_USER_PAGE_MARK_DELETE_COMPENSATE, pgbuf_get_vpid_ptr (addr.pgptr), addr.offset,
			 addr.pgptr, 0, NULL, LOG_FIND_CURRENT_TDES (thread_p));

  file_log ("file_rv_user_page_unmark_delete_logical",
	    "unmark delete vpid %d|%d in file %d|%d, page %d|%d, "
	    "prev_lsa %lld|%d, crt_lsa %lld|%d, at offset %d",
	    VPID_AS_ARGS (vpid_in_table), VFID_AS_ARGS (vfid),
	    PGBUF_PAGE_VPID_AS_ARGS (addr.pgptr), LSA_AS_ARGS (&save_lsa),
	    PGBUF_PAGE_VPID_AS_ARGS (addr.pgptr), addr.offset);

  pgbuf_set_dirty (thread_p, addr.pgptr, DONT_FREE);

  /* done */
  assert (error_code == NO_ERROR);

exit:
  if (page_ftab != NULL)
    {
      pgbuf_unfix (thread_p, page_ftab);
    }
  if (page_fhead != NULL)
    {
      pgbuf_unfix (thread_p, page_fhead);
    }
  return error_code;
}

/*
 * file_rv_user_page_unmark_delete_physical () - Recover page unmark delete (physical)
 *
 * return	 : Error code
 * thread_p (in) : Thread entry
 * rcv (in)	 : Recovery data
 */
int
file_rv_user_page_unmark_delete_physical (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  PAGE_PTR page_ftab = rcv->pgptr;
  VPID *vpid_ptr = NULL;

  /* note: this is used to compensate undo of mark delete. this time the location of VPID is known */

  vpid_ptr = (VPID *) (page_ftab + rcv->offset);
  assert (FILE_USER_PAGE_IS_MARKED_DELETED (vpid_ptr));

  FILE_USER_PAGE_CLEAR_MARK_DELETED (vpid_ptr);

  file_log ("file_rv_user_page_unmark_delete_physical",
	    "unmark delete vpid %d|%d in page %d|%d, lsa %lld|%d, "
	    "at offset %d", VPID_AS_ARGS (vpid_ptr),
	    PGBUF_PAGE_VPID_AS_ARGS (page_ftab), PGBUF_PAGE_LSA_AS_ARGS (page_ftab), rcv->offset);

  pgbuf_set_dirty (thread_p, page_ftab, DONT_FREE);
  return NO_ERROR;
}

/*
 * file_table_check_page_is_in_sectors () - FILE_EXTDATA_ITEM_FUNC to check user page table is in one of the sectors
 *
 * return        : error code
 * thread_p (in) : thread entry
 * data (in)     : user page table entry
 * index (in)    : index of user page table entry
 * stop (in)     : not used
 * args (in)     : FILE_VSID_COLLECTOR *
 */
static int
file_table_check_page_is_in_sectors (THREAD_ENTRY * thread_p, const void *data, int index, bool * stop, void *args)
{
  FILE_VSID_COLLECTOR *collector = (FILE_VSID_COLLECTOR *) args;
  VPID vpid = *(VPID *) data;
  VSID vsid_of_vpid;

  FILE_USER_PAGE_CLEAR_MARK_DELETED (&vpid);
  VSID_FROM_VPID (&vsid_of_vpid, &vpid);

  if (bsearch (&vsid_of_vpid, collector->vsids, collector->n_vsids, sizeof (VSID), file_compare_vsids) == NULL)
    {
      /* not found! */
      assert_release (false);
      return ER_FAILED;
    }
  return NO_ERROR;
}

/*
 * file_numerable_truncate () - truncate numerable files to a smaller number of pages
 *
 * return        : error code
 * thread_p (in) : thread entry
 * vfid (in)     : file identifier
 * npages (in)   : desired number of pages
 */
int
file_numerable_truncate (THREAD_ENTRY * thread_p, const VFID * vfid, DKNPAGES npages)
{
  VPID vpid_fhead;
  PAGE_PTR page_fhead = NULL;
  FILE_HEADER *fhead;
  VPID vpid;
  int error_code = NO_ERROR;

  FILE_GET_HEADER_VPID (vfid, &vpid_fhead);
  page_fhead = pgbuf_fix (thread_p, &vpid_fhead, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (page_fhead == NULL)
    {
      ASSERT_ERROR_AND_SET (error_code);
      return ER_FAILED;
    }
  fhead = (FILE_HEADER *) page_fhead;

  if (!FILE_IS_NUMERABLE (fhead))
    {
      /* cannot truncate */
      assert_release (false);
      error_code = ER_FAILED;
      goto exit;
    }

  if (fhead->n_page_mark_delete != 0)
    {
      /* I am not sure what we should do in this case. We give up truncating and in debug it will crash. */
      assert (false);
      return NO_ERROR;
    }

  while (fhead->n_page_user > npages)
    {
      /* maybe this can be done in a more optimal way... but for now it will have to do */
      error_code = file_numerable_find_nth (thread_p, vfid, npages, false, &vpid);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto exit;
	}
      error_code = file_dealloc (thread_p, vfid, &vpid, fhead->type);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto exit;
	}
    }
  assert (fhead->n_page_user == npages);
  assert (error_code == NO_ERROR);

exit:
  if (page_fhead != NULL)
    {
      pgbuf_unfix (thread_p, page_fhead);
    }
  return error_code;
}

/************************************************************************/
/* Temporary files section.                                             */
/************************************************************************/

/*
 * file_temp_alloc () - Allocate a new page in temporary file.
 *
 * return		: Error code
 * thread_p (in)	: Thread entry
 * page_fhead (in)	: File header page
 * alloc_type (in)	: User/table page
 * vpid_alloc_out (out) : Output allocated page VPID.
 */
static int
file_temp_alloc (THREAD_ENTRY * thread_p, PAGE_PTR page_fhead, FILE_ALLOC_TYPE alloc_type, VPID * vpid_alloc_out)
{
  FILE_HEADER *fhead = (FILE_HEADER *) page_fhead;
  VPID vpid_fhead;
  FILE_EXTENSIBLE_DATA *extdata_part_ftab = NULL;
  PAGE_PTR page_ftab = NULL;
  FILE_PARTIAL_SECTOR *partsect = NULL;
  PAGE_PTR page_extdata = NULL;
  bool was_empty = false;
  bool is_full = false;
  int error_code = NO_ERROR;

  file_header_sanity_check (fhead);
  assert (FILE_IS_TEMPORARY (fhead));
  assert (page_fhead != NULL);
  assert (vpid_alloc_out != NULL);

  /* how it works
   * temporary files, compared to permanent files, have a simplified design. they do not keep two different tables
   * (partial and full). they only keep the partial table, and when sectors become full, they remain in the partial
   * table.
   * the second difference is that temporary files never deallocate pages. since they are temporary, and soon to be
   * freed (or cached for reuse), there is no point in deallocating pages.
   * the simplified design was chosen because temporary files are never logged. and it is hard to undo changes without
   * logging when errors happen (e.g. interrupted transaction).
   *
   * all we have to do to allocate a page is to try to allocate from last used sector. if that sector is full, advance
   * to next sector. if no new sectors, reserve a new one. that's all.
   */

  file_log ("file_temp_alloc", "%s", FILE_ALLOC_TYPE_STRING (alloc_type));

  FILE_GET_HEADER_VPID (&fhead->self, &vpid_fhead);

  /* get page for last allocated partial table */
  if (VPID_EQ (&vpid_fhead, &fhead->vpid_last_temp_alloc))
    {
      /* partial table in header */
      FILE_HEADER_GET_PART_FTAB (fhead, extdata_part_ftab);
    }
  else
    {
      page_ftab =
	pgbuf_fix (thread_p, &fhead->vpid_last_temp_alloc, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
      if (page_ftab == NULL)
	{
	  ASSERT_ERROR_AND_SET (error_code);
	  goto exit;
	}
      extdata_part_ftab = (FILE_EXTENSIBLE_DATA *) page_ftab;
    }

  if (fhead->n_page_free == 0)
    {
      /* expand file by one sector */
      FILE_PARTIAL_SECTOR partsect_new = FILE_PARTIAL_SECTOR_INITIALIZER;

      /* reserve a sector */
      error_code =
	disk_reserve_sectors (thread_p, DB_TEMPORARY_DATA_PURPOSE, fhead->volid_last_expand, 1, &partsect_new.vsid);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto exit;
	}

      file_log ("file_temp_alloc",
		"no free pages" FILE_HEAD_ALLOC_MSG
		"\texpand file with VSID = %d|%d ", FILE_HEAD_ALLOC_AS_ARGS (fhead), VSID_AS_ARGS (&partsect_new.vsid));

      assert (extdata_part_ftab != NULL);
      if (file_extdata_is_full (extdata_part_ftab))
	{
	  /* we need a new page. */
	  /* we will use first page of newly reserved sector. */
	  VPID vpid_ftab_new;
	  PAGE_PTR page_ftab_new = NULL;

	  vpid_ftab_new.volid = partsect_new.vsid.volid;
	  vpid_ftab_new.pageid = SECTOR_FIRST_PAGEID (partsect_new.vsid.sectid);
	  file_partsect_set_bit (&partsect_new, 0);

	  /* fix new file table page */
	  page_ftab_new = pgbuf_fix (thread_p, &vpid_ftab_new, NEW_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
	  if (page_ftab_new == NULL)
	    {
	      ASSERT_ERROR_AND_SET (error_code);

	      /* todo: unreserve sector */
	      goto exit;
	    }
	  pgbuf_set_page_ptype (thread_p, page_ftab_new, PAGE_FTAB);

	  /* set link to previous page. */
	  VPID_COPY (&extdata_part_ftab->vpid_next, &vpid_ftab_new);

	  /* we don't need old file table page anymore */
	  assert (page_ftab != page_fhead);
	  if (page_ftab != NULL)
	    {
	      pgbuf_set_dirty_and_free (thread_p, page_ftab);
	    }

	  VPID_COPY (&fhead->vpid_last_temp_alloc, &vpid_ftab_new);
	  fhead->offset_to_last_temp_alloc = 0;
	  pgbuf_set_dirty (thread_p, page_fhead, DONT_FREE);

	  page_ftab = page_ftab_new;
	  extdata_part_ftab = (FILE_EXTENSIBLE_DATA *) page_ftab;
	  file_extdata_init (sizeof (FILE_PARTIAL_SECTOR), DB_PAGESIZE, extdata_part_ftab);

	  file_log ("file_temp_alloc",
		    "used newly reserved sector's first page %d|%d for partial table.", VPID_AS_ARGS (&vpid_ftab_new));
	}
      assert (!file_extdata_is_full (extdata_part_ftab));
      assert (file_extdata_item_count (extdata_part_ftab) == fhead->offset_to_last_temp_alloc);
      file_extdata_append (extdata_part_ftab, &partsect_new);

      fhead->n_sector_partial++;
      fhead->n_sector_total++;
      fhead->n_page_free += DISK_SECTOR_NPAGES;
      fhead->n_page_total += DISK_SECTOR_NPAGES;
      if (partsect_new.page_bitmap == FILE_EMPTY_PAGE_BITMAP)
	{
	  fhead->n_sector_empty++;
	}
      else
	{
	  fhead->n_page_free--;
	  fhead->n_page_ftab++;
	}

      file_log ("file_temp_alloc",
		"new partial sector added to partial extensible data:\n"
		FILE_PARTSECT_MSG ("newly reserved sector")
		FILE_EXTDATA_MSG ("last partial table component"),
		FILE_PARTSECT_AS_ARGS (&partsect_new), FILE_EXTDATA_AS_ARGS (extdata_part_ftab));

      file_header_sanity_check (fhead);
    }
  assert (fhead->n_page_free > 0);

  if (fhead->offset_to_last_temp_alloc == file_extdata_item_count (extdata_part_ftab))
    {
      VPID vpid_next;

      /* must be full */
      assert (file_extdata_is_full (extdata_part_ftab));
      /* we must have another extensible data */
      assert (!VPID_ISNULL (&extdata_part_ftab->vpid_next));

      /* move allocation cursor to next partial table page */
      vpid_next = extdata_part_ftab->vpid_next;
      if (page_ftab != NULL)
	{
	  pgbuf_unfix_and_init (thread_p, page_ftab);
	}
      page_ftab = pgbuf_fix (thread_p, &vpid_next, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
      if (page_ftab == NULL)
	{
	  ASSERT_ERROR_AND_SET (error_code);
	  goto exit;
	}
      extdata_part_ftab = (FILE_EXTENSIBLE_DATA *) page_ftab;

      fhead->vpid_last_temp_alloc = vpid_next;
      fhead->offset_to_last_temp_alloc = 0;
    }

  assert (extdata_part_ftab != NULL);
  assert (fhead->offset_to_last_temp_alloc < file_extdata_item_count (extdata_part_ftab));
  partsect = (FILE_PARTIAL_SECTOR *) file_extdata_at (extdata_part_ftab, fhead->offset_to_last_temp_alloc);

  /* Allocate the page. */
  was_empty = file_partsect_is_empty (partsect);
  if (!file_partsect_alloc (partsect, vpid_alloc_out, NULL))
    {
      /* full sector? that is unexpected. we must have a logic error. */
      assert_release (false);
      error_code = ER_FAILED;
      goto exit;
    }
  if (file_partsect_is_full (partsect))
    {
      is_full = true;
      fhead->offset_to_last_temp_alloc++;
    }

  file_header_alloc (fhead, alloc_type, was_empty, is_full);
  pgbuf_set_dirty (thread_p, page_fhead, DONT_FREE);

  file_log ("file_temp_alloc", "%s %d|%d successful. \n" FILE_HEAD_FULL_MSG
	    FILE_PARTSECT_MSG ("partial sector after alloc"),
	    FILE_ALLOC_TYPE_STRING (alloc_type),
	    VPID_AS_ARGS (vpid_alloc_out), FILE_HEAD_FULL_AS_ARGS (fhead), FILE_PARTSECT_AS_ARGS (partsect));

  if (page_ftab != NULL)
    {
      assert (page_ftab != page_fhead);
      pgbuf_set_dirty_and_free (thread_p, page_ftab);
    }

  /* done */
  assert (error_code == NO_ERROR);

exit:
  if (page_ftab != NULL)
    {
      pgbuf_unfix_and_init (thread_p, page_ftab);
    }
  return error_code;
}

/*
 * file_temp_set_type () - set new type in existing temporary file
 *
 * return        : error code
 * thread_p (in) : thread entry
 * vfid (in)     : file identifier
 * ftype (in)    : new file type
 */
STATIC_INLINE int
file_temp_set_type (THREAD_ENTRY * thread_p, VFID * vfid, FILE_TYPE ftype)
{
  VPID vpid_fhead;
  PAGE_PTR page_fhead = NULL;
  FILE_HEADER *fhead = NULL;
  int error_code = NO_ERROR;

  FILE_GET_HEADER_VPID (vfid, &vpid_fhead);
  page_fhead = pgbuf_fix (thread_p, &vpid_fhead, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (page_fhead == NULL)
    {
      ASSERT_ERROR_AND_SET (error_code);
      return error_code;
    }
  fhead = (FILE_HEADER *) page_fhead;
  file_header_sanity_check (fhead);

  if (!FILE_IS_TEMPORARY (fhead))
    {
      /* we cannot change type */
      assert_release (false);
      error_code = ER_FAILED;
      goto exit;
    }

  fhead->type = ftype;
  pgbuf_set_dirty (thread_p, page_fhead, DONT_FREE);

exit:
  pgbuf_unfix (thread_p, page_fhead);
  return error_code;
}

/*
 * file_temp_reset_user_pages () - reset all user pages in temporary file.
 *
 * return        : error code
 * thread_p (in) : thread entry
 * vfid (in)     : file identifier
 */
static int
file_temp_reset_user_pages (THREAD_ENTRY * thread_p, const VFID * vfid)
{
  VPID vpid_fhead;
  PAGE_PTR page_fhead = NULL;
  FILE_HEADER *fhead = NULL;
  FILE_FTAB_COLLECTOR collector = FILE_FTAB_COLLECTOR_INITIALIZER;
  FILE_EXTENSIBLE_DATA *extdata_part_ftab = NULL;
  PAGE_PTR page_ftab = NULL;
  FILE_PARTIAL_SECTOR *partsect = NULL;
  int idx_sect = 0;
  VPID vpid_next;
  int nsect_part_new;
  int nsect_full_new;
  int nsect_empty_new;
  FILE_EXTENSIBLE_DATA *extdata_user_page_ftab;
  bool save_interrupt;
  bool found = false;
  int error_code = NO_ERROR;

  /* don't let this be interrupted, because we might ruin the file */
  save_interrupt = thread_set_check_interrupt (thread_p, false);

  FILE_GET_HEADER_VPID (vfid, &vpid_fhead);
  page_fhead = pgbuf_fix (thread_p, &vpid_fhead, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (page_fhead == NULL)
    {
      ASSERT_ERROR_AND_SET (error_code);
      goto exit;
    }
  fhead = (FILE_HEADER *) page_fhead;
  file_header_sanity_check (fhead);

  if (!FILE_IS_TEMPORARY (fhead))
    {
      assert_release (false);
      error_code = ER_FAILED;
      goto exit;
    }

  if (FILE_IS_NUMERABLE (fhead))
    {
      /* reset user page table */
      FILE_HEADER_GET_USER_PAGE_FTAB (fhead, extdata_user_page_ftab);
      VPID_SET_NULL (&extdata_user_page_ftab->vpid_next);
      extdata_user_page_ftab->n_items = 0;
      fhead->vpid_last_user_page_ftab = vpid_fhead;
    }

  /* collect table pages */
  error_code = file_table_collect_ftab_pages (thread_p, page_fhead, false, &collector);
  if (error_code != NO_ERROR)
    {
      assert_release (false);
      goto exit;
    }

  /* count partial, full, empty */
  nsect_full_new = 0;
  for (idx_sect = 0; idx_sect < collector.nsects; idx_sect++)
    {
      if (collector.partsect_ftab[idx_sect].page_bitmap == FILE_FULL_PAGE_BITMAP)
	{
	  nsect_full_new++;
	}
    }
  nsect_part_new = collector.nsects - nsect_full_new;
  nsect_empty_new = fhead->n_sector_total - collector.nsects;
  nsect_part_new += nsect_empty_new;

  /* reset partial table sectors, but leave file table pages allocated */
  page_ftab = page_fhead;
  FILE_HEADER_GET_PART_FTAB (fhead, extdata_part_ftab);
  while (true)
    {
      assert (extdata_part_ftab != NULL);

      for (partsect = (FILE_PARTIAL_SECTOR *) file_extdata_start (extdata_part_ftab);
	   partsect < (FILE_PARTIAL_SECTOR *) file_extdata_end (extdata_part_ftab); partsect++)
	{
	  /* does it have file table pages? */
	  found = false;
	  for (idx_sect = 0; idx_sect < collector.nsects; idx_sect++)
	    {
	      if (file_compare_vsids (&partsect->vsid, &collector.partsect_ftab[idx_sect].vsid) == 0)
		{
		  /* get bitmap from collector */
		  partsect->page_bitmap = collector.partsect_ftab[idx_sect].page_bitmap;

		  /* sector cannot be found again. remove it from collector */
		  if (idx_sect < collector.nsects - 1)
		    {
		      memmove (&collector.partsect_ftab[idx_sect],
			       &collector.partsect_ftab[idx_sect + 1],
			       (collector.nsects - 1 - idx_sect) * sizeof (FILE_PARTIAL_SECTOR));
		    }
		  collector.nsects--;
		  found = true;
		  break;
		}
	    }
	  if (!found)
	    {
	      /* not found in collector, must be empty sector */
	      partsect->page_bitmap = FILE_EMPTY_PAGE_BITMAP;
	    }
	}

      pgbuf_set_dirty (thread_p, page_ftab, DONT_FREE);

      vpid_next = extdata_part_ftab->vpid_next;
      if (page_ftab != page_fhead)
	{
	  pgbuf_unfix (thread_p, page_fhead);
	}
      page_ftab = NULL;
      if (VPID_ISNULL (&vpid_next))
	{
	  break;
	}
      page_ftab = pgbuf_fix (thread_p, &vpid_next, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
      if (page_ftab == NULL)
	{
	  assert_release (false);
	  error_code = ER_FAILED;
	  goto exit;
	}
      extdata_part_ftab = (FILE_EXTENSIBLE_DATA *) page_ftab;
    }
  /* partial table sectors have been reset */

  /* update header */
  fhead->n_sector_empty = nsect_empty_new;
  fhead->n_sector_partial = nsect_part_new;
  fhead->n_sector_full = nsect_full_new;

  fhead->n_page_ftab = collector.npages;
  fhead->n_page_free = fhead->n_page_total - fhead->n_page_ftab;
  fhead->n_page_user = 0;

  /* reset pointers used for allocations */
  fhead->vpid_last_temp_alloc = vpid_fhead;
  fhead->offset_to_last_temp_alloc = 0;

  file_log ("file_temp_reset_user_pages", "finished \n" FILE_HEAD_FULL_MSG, FILE_HEAD_FULL_AS_ARGS (fhead));

  pgbuf_set_dirty (thread_p, page_fhead, DONT_FREE);
  /* done */
  assert (error_code == NO_ERROR);

exit:
  assert (page_ftab == NULL);

  if (page_fhead != NULL)
    {
      pgbuf_unfix (thread_p, page_fhead);
    }

  (void) thread_set_check_interrupt (thread_p, save_interrupt);

  if (collector.partsect_ftab != NULL)
    {
      db_private_free (thread_p, collector.partsect_ftab);
    }

  return error_code;
}

/*
 * file_temp_preserve () - preserve temporary file
 *
 * return :
 * THREAD_ENTRY * thread_p (in) :
 * const VFID * vfid (in) :
 */
void
file_temp_preserve (THREAD_ENTRY * thread_p, const VFID * vfid)
{
  /* to preserve the file, we need to remove it from transaction list */
  FILE_TEMPCACHE_ENTRY *entry = NULL;

  assert (vfid != NULL && !VFID_ISNULL (vfid));

  entry = file_tempcache_pop_tran_file (thread_p, vfid);
  if (entry == NULL)
    {
      assert_release (false);
    }
  else
    {
      file_tempcache_retire_entry (entry);
    }
}

/************************************************************************/
/* Temporary cache section                                              */
/************************************************************************/

/*
 * file_tempcache_init () - init temporary file cache
 *
 * return : ER_OUT_OF_VIRTUAL_MEMORY or NO_ERROR
 */
static int
file_tempcache_init (void)
{
  int memsize = 0;
#if defined (SERVER_MODE)
  int ntrans = logtb_get_number_of_total_tran_indices () + 1;
#else
  int ntrans = 1;
#endif

  assert (file_Tempcache == NULL);

  /* allocate file_Tempcache */
  memsize = sizeof (FILE_TEMPCACHE);
  file_Tempcache = (FILE_TEMPCACHE *) malloc (memsize);
  if (file_Tempcache == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, memsize);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  /* initialize free entry list... used to avoid entry allocation/deallocation */
  file_Tempcache->free_entries = NULL;
  file_Tempcache->nfree_entries_max = ntrans * 8;	/* I set 8 per transaction, maybe there is a better value */
  file_Tempcache->nfree_entries = 0;

  /* initialize temporary file cache. we keep two separate lists for numerable and regular files */
  file_Tempcache->cached_not_numerable = NULL;
  file_Tempcache->cached_numerable = NULL;
  file_Tempcache->ncached_max = prm_get_integer_value (PRM_ID_MAX_ENTRIES_IN_TEMP_FILE_CACHE);
  file_Tempcache->ncached_not_numerable = 0;
  file_Tempcache->ncached_numerable = 0;

  /* initialize mutex used to protect temporary file cache and entry allocation/deallocation */
  pthread_mutex_init (&file_Tempcache->mutex, NULL);
#if !defined (NDEBUG)
  file_Tempcache->owner_mutex = -1;
#endif

  /* allocate transaction temporary files lists */
  memsize = ntrans * sizeof (FILE_TEMPCACHE *);
  file_Tempcache->tran_files = (FILE_TEMPCACHE_ENTRY **) malloc (memsize);
  if (file_Tempcache->tran_files == NULL)
    {
      pthread_mutex_destroy (&file_Tempcache->mutex);
      free_and_init (file_Tempcache);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, memsize);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  memset (file_Tempcache->tran_files, 0, memsize);

  /* all ok */
  return NO_ERROR;
}

/*
 * file_tempcache_final () - free temporary files cache resources
 *
 * return : void
 */
static void
file_tempcache_final (void)
{
  int tran = 0;
  int ntrans;

  if (file_Tempcache == NULL)
    {
      return;
    }

#if defined (SERVER_MODE)
  ntrans = logtb_get_number_of_total_tran_indices ();
#else
  ntrans = 1;
#endif

  file_tempcache_lock ();

  /* free all transaction lists... they should be empty anyway, but be conservative */
  for (tran = 0; tran < ntrans; tran++)
    {
      if (file_Tempcache->tran_files[tran] != NULL)
	{
	  /* should be empty */
	  assert (false);
	  file_tempcache_free_entry_list (&file_Tempcache->tran_files[tran]);
	}
    }
  free_and_init (file_Tempcache->tran_files);

  /* temporary volumes are removed, we don't have to destroy files */
  file_tempcache_free_entry_list (&file_Tempcache->cached_not_numerable);
  file_tempcache_free_entry_list (&file_Tempcache->cached_numerable);

  file_tempcache_free_entry_list (&file_Tempcache->free_entries);

  file_tempcache_unlock ();

  pthread_mutex_destroy (&file_Tempcache->mutex);

  free_and_init (file_Tempcache);
}

/*
 * file_tempcache_free_entry_list () - free entry list and set it to NULL
 *
 * return        : void
 * list (in/out) : list to free. it becomes NULL.
 */
STATIC_INLINE void
file_tempcache_free_entry_list (FILE_TEMPCACHE_ENTRY ** list)
{
  FILE_TEMPCACHE_ENTRY *entry;
  FILE_TEMPCACHE_ENTRY *next;

  file_tempcache_check_lock ();

  for (entry = *list; entry != NULL; entry = next)
    {
      next = entry->next;
      free (entry);
    }
  *list = NULL;
}

/*
 * file_tempcache_alloc_entry () - allocate a new file temporary cache entry
 *
 * return      : error code
 * entry (out) : entry from free entries or newly allocated
 */
STATIC_INLINE int
file_tempcache_alloc_entry (FILE_TEMPCACHE_ENTRY ** entry)
{
  file_tempcache_check_lock ();

  if (file_Tempcache->free_entries != NULL)
    {
      assert (file_Tempcache->nfree_entries > 0);

      *entry = file_Tempcache->free_entries;
      file_Tempcache->free_entries = file_Tempcache->free_entries->next;
      file_Tempcache->nfree_entries--;
    }
  else
    {
      *entry = (FILE_TEMPCACHE_ENTRY *) malloc (sizeof (FILE_TEMPCACHE_ENTRY));
      if (*entry == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (FILE_TEMPCACHE_ENTRY));
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
    }

  (*entry)->next = NULL;
  VFID_SET_NULL (&(*entry)->vfid);
  (*entry)->ftype = FILE_UNKNOWN_TYPE;

  return NO_ERROR;
}

/*
 * file_tempcache_retire_entry () - retire entry to free entry list (if not maxed) or deallocate
 *
 * return     : void
 * entry (in) : retired entry
 */
STATIC_INLINE void
file_tempcache_retire_entry (FILE_TEMPCACHE_ENTRY * entry)
{
  /* we lock to change free entry list */
  file_tempcache_lock ();

  if (file_Tempcache->nfree_entries < file_Tempcache->nfree_entries_max)
    {
      entry->next = file_Tempcache->free_entries;
      file_Tempcache->free_entries = entry;
      file_Tempcache->nfree_entries++;
    }
  else
    {
      free (entry);
    }

  file_tempcache_unlock ();
}

/*
 * file_tempcache_lock () - lock temporary cache mutex
 *
 * return : void
 */
STATIC_INLINE void
file_tempcache_lock (void)
{
  assert (file_Tempcache->owner_mutex != thread_get_current_entry_index ());
  pthread_mutex_lock (&file_Tempcache->mutex);
  assert (file_Tempcache->owner_mutex == -1);
#if !defined (NDEBUG)
  file_Tempcache->owner_mutex = thread_get_current_entry_index ();
#endif /* !NDEBUG */
}

/*
 * file_tempcache_unlock () - unlock temporary cache mutex
 *
 * return : void
 */
STATIC_INLINE void
file_tempcache_unlock (void)
{
  assert (file_Tempcache->owner_mutex == thread_get_current_entry_index ());
#if !defined (NDEBUG)
  file_Tempcache->owner_mutex = -1;
#endif /* !NDEBUG */
  pthread_mutex_unlock (&file_Tempcache->mutex);
}

/*
 * file_tempcache_check_lock () - check temporary cache mutex is locked (for debug)
 *
 * return : void
 */
STATIC_INLINE void
file_tempcache_check_lock (void)
{
  assert (file_Tempcache->owner_mutex == thread_get_current_entry_index ());
}

/*
 * file_tempcache_get () - get a file from temporary file cache
 *
 * return         : error code
 * thread_p (in)  : thread entry
 * ftype (in)     : file type
 * numerable (in) : true for numerable file, false for regular file
 * entry (out)    : always output an temporary cache entry. caller must check entry VFID to find if cached file was used
 */
STATIC_INLINE int
file_tempcache_get (THREAD_ENTRY * thread_p, FILE_TYPE ftype, bool numerable, FILE_TEMPCACHE_ENTRY ** entry)
{
  int error_code = NO_ERROR;

  assert (entry != NULL && *entry == NULL);

  file_tempcache_lock ();

  *entry = numerable ? file_Tempcache->cached_numerable : file_Tempcache->cached_not_numerable;
  if (*entry != NULL && (*entry)->ftype != ftype)
    {
      /* change type */
      error_code = file_temp_set_type (thread_p, &(*entry)->vfid, ftype);
      if (error_code != NO_ERROR)
	{
	  /* could not change it, give up */
	  *entry = NULL;
	}
      else
	{
	  (*entry)->ftype = ftype;
	}
    }

  if (*entry != NULL)
    {
      /* remove from cache */
      if (numerable)
	{
	  assert (*entry == file_Tempcache->cached_numerable);
	  assert (file_Tempcache->ncached_numerable > 0);

	  file_Tempcache->cached_numerable = file_Tempcache->cached_numerable->next;
	  file_Tempcache->ncached_numerable--;
	}
      else
	{
	  assert (*entry == file_Tempcache->cached_not_numerable);
	  assert (file_Tempcache->ncached_not_numerable > 0);

	  file_Tempcache->cached_not_numerable = file_Tempcache->cached_not_numerable->next;
	  file_Tempcache->ncached_not_numerable--;
	}

      (*entry)->next = NULL;

      file_log ("file_tempcache_get",
		"found in cache temporary file entry "
		FILE_TEMPCACHE_ENTRY_MSG ", %s\n" FILE_TEMPCACHE_MSG,
		FILE_TEMPCACHE_ENTRY_AS_ARGS (*entry), numerable ? "numerable" : "regular", FILE_TEMPCACHE_AS_ARGS);

      file_tempcache_unlock ();
      return NO_ERROR;
    }

  /* not from cache, get a new entry */
  error_code = file_tempcache_alloc_entry (entry);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      file_tempcache_unlock ();
      return error_code;
    }

  /* init new entry */
  assert (*entry != NULL);
  (*entry)->next = NULL;
  (*entry)->ftype = ftype;
  VFID_SET_NULL (&(*entry)->vfid);

  file_tempcache_unlock ();

  return NO_ERROR;
}

/*
 * file_tempcache_put () - put entry to file cache (if cache is not full and if file passes vetting)
 *
 * return        : true if file was cached, false otherwise
 * thread_p (in) : thread entry
 * entry (in)    : entry of retired temporary file
 */
STATIC_INLINE bool
file_tempcache_put (THREAD_ENTRY * thread_p, FILE_TEMPCACHE_ENTRY * entry)
{
  FILE_HEADER fhead;

  fhead.n_page_user = -1;
  if (file_header_copy (thread_p, &entry->vfid, &fhead) != NO_ERROR
      || fhead.n_page_user > prm_get_integer_value (PRM_ID_MAX_PAGES_IN_TEMP_FILE_CACHE))
    {
      /* file not valid for cache */
      file_log ("file_tempcache_put",
		"could not cache temporary file " FILE_TEMPCACHE_ENTRY_MSG
		", fhead->n_page_user = %d ", FILE_TEMPCACHE_ENTRY_AS_ARGS (entry), fhead.n_page_user);
      return false;
    }

  /* lock temporary cache */
  file_tempcache_lock ();

  if (file_Tempcache->ncached_not_numerable + file_Tempcache->ncached_numerable < file_Tempcache->nfree_entries_max)
    {
      /* cache not full */
      assert ((file_Tempcache->cached_not_numerable == NULL) == (file_Tempcache->ncached_not_numerable == 0));
      assert ((file_Tempcache->cached_numerable == NULL) == (file_Tempcache->ncached_numerable == 0));

      /* reset file */
      if (file_temp_reset_user_pages (thread_p, &entry->vfid) != NO_ERROR)
	{
	  /* failed to reset file, we cannot cache it */
	  ASSERT_ERROR ();

	  file_log ("file_tempcache_put",
		    "could not cache temporary file " FILE_TEMPCACHE_ENTRY_MSG
		    ", error during file reset", FILE_TEMPCACHE_ENTRY_AS_ARGS (entry));
	  file_tempcache_unlock ();
	  return false;
	}

      /* add numerable temporary file to cached numerable file list, regular file to not numerable list */
      if (FILE_IS_NUMERABLE (&fhead))
	{
	  entry->next = file_Tempcache->cached_numerable;
	  file_Tempcache->cached_numerable = entry;
	  file_Tempcache->ncached_numerable++;
	}
      else
	{
	  entry->next = file_Tempcache->cached_not_numerable;
	  file_Tempcache->cached_not_numerable = entry;
	  file_Tempcache->ncached_not_numerable++;
	}

      file_log ("file_tempcache_put",
		"cached temporary file " FILE_TEMPCACHE_ENTRY_MSG ", %s\n"
		FILE_TEMPCACHE_MSG, FILE_TEMPCACHE_ENTRY_AS_ARGS (entry),
		FILE_IS_NUMERABLE (&fhead) ? "numerable" : "regular", FILE_TEMPCACHE_AS_ARGS);

      file_tempcache_unlock ();

      /* cached */
      return true;
    }
  else
    {
      /* cache full */
      file_log ("file_tempcache_put",
		"could not cache temporary file " FILE_TEMPCACHE_ENTRY_MSG
		", temporary cache is full \n" FILE_TEMPCACHE_MSG,
		FILE_TEMPCACHE_ENTRY_AS_ARGS (entry), FILE_TEMPCACHE_AS_ARGS);
      file_tempcache_unlock ();
      return false;
    }
}

/*
 * file_get_tempcache_entry_index () - returns entry index of tempcache
 *
 * return        : int
 * thread_p (in) : thread entry
 */
STATIC_INLINE int
file_get_tempcache_entry_index (THREAD_ENTRY * thread_p)
{
#if defined (SERVER_MODE)
  return thread_get_current_tran_index ();
#else
  return 0;
#endif
}

/*
 * file_tempcache_drop_tran_temp_files () - drop all temporary files created by current transaction
 *
 * return        : void
 * thread_p (in) : thread entry
 */
void
file_tempcache_drop_tran_temp_files (THREAD_ENTRY * thread_p)
{
  if (file_Tempcache->tran_files[file_get_tempcache_entry_index (thread_p)] != NULL)
    {
      file_log ("file_tempcache_drop_tran_temp_files",
		"drop %d transaction temporary files", file_get_tran_num_temp_files (thread_p));
      file_tempcache_cache_or_drop_entries (thread_p,
					    &file_Tempcache->tran_files[file_get_tempcache_entry_index (thread_p)]);
    }
}

/*
 * file_tempcache_cache_or_drop_entries () - drop all temporary files in the give entry list
 *
 * return        : void
 * thread_p (in) : thread entry
 * entries (in)  : temporary files entry list
 */
STATIC_INLINE void
file_tempcache_cache_or_drop_entries (THREAD_ENTRY * thread_p, FILE_TEMPCACHE_ENTRY ** entries)
{
  FILE_TEMPCACHE_ENTRY *temp_file;
  FILE_TEMPCACHE_ENTRY *next = NULL;
  bool save_interrupt = thread_set_check_interrupt (thread_p, false);

  for (temp_file = *entries; temp_file != NULL; temp_file = next)
    {
      next = temp_file->next;
      temp_file->next = NULL;

      if (!file_tempcache_put (thread_p, temp_file))
	{
	  /* was not cached. destroy the file */
	  file_log ("file_tempcache_cache_or_drop_entries",
		    "drop entry " FILE_TEMPCACHE_ENTRY_MSG, FILE_TEMPCACHE_ENTRY_AS_ARGS (temp_file));
	  if (file_destroy (thread_p, &temp_file->vfid) != NO_ERROR)
	    {
	      /* file is leaked */
	      assert_release (false);
	      /* ignore error and continue free as many files as possible */
	    }
	  file_tempcache_retire_entry (temp_file);
	}
    }
  *entries = NULL;

  (void) thread_set_check_interrupt (thread_p, save_interrupt);
}

/*
 * file_tempcache_pop_tran_file () - pop entry with the given VFID from transaction list
 *
 * return        : popped entry
 * thread_p (in) : thread entry
 * vfid (in)     : file identifier
 */
STATIC_INLINE FILE_TEMPCACHE_ENTRY *
file_tempcache_pop_tran_file (THREAD_ENTRY * thread_p, const VFID * vfid)
{
  FILE_TEMPCACHE_ENTRY **tran_files_p = &file_Tempcache->tran_files[file_get_tempcache_entry_index (thread_p)];
  FILE_TEMPCACHE_ENTRY *entry = NULL, *prev_entry = NULL;

  for (entry = *tran_files_p; entry != NULL; entry = entry->next)
    {
      if (VFID_EQ (&entry->vfid, vfid))
	{
	  /* remove entry from transaction list */
	  if (prev_entry != NULL)
	    {
	      prev_entry->next = entry->next;
	    }
	  else
	    {
	      *tran_files_p = entry->next;
	    }
	  entry->next = NULL;

	  file_log ("file_tempcache_pop_tran_file", "removed entry " FILE_TEMPCACHE_ENTRY_MSG,
		    FILE_TEMPCACHE_ENTRY_AS_ARGS (entry));

	  return entry;
	}
      prev_entry = entry;
    }

  /* should have found it */
  assert_release (false);
  return NULL;
}

/*
 * file_tempcache_push_tran_file () - push temporary file entry to transaction list
 *
 * return        : void
 * thread_p (in) : thread entry
 * entry (in)    : temporary cache entry
 */
STATIC_INLINE void
file_tempcache_push_tran_file (THREAD_ENTRY * thread_p, FILE_TEMPCACHE_ENTRY * entry)
{
  FILE_TEMPCACHE_ENTRY **tran_files_p = &file_Tempcache->tran_files[file_get_tempcache_entry_index (thread_p)];

  entry->next = *tran_files_p;
  *tran_files_p = entry;

  file_log ("file_tempcache_push_tran_file", "pushed entry " FILE_TEMPCACHE_ENTRY_MSG,
	    FILE_TEMPCACHE_ENTRY_AS_ARGS (entry));
}

/*
 * file_get_tran_num_temp_files () - returns the number of temp file entries of the given transaction
 *
 * return        : the number of temp files of the given transaction
 * thread_p (in) : thread entry
 */
int
file_get_tran_num_temp_files (THREAD_ENTRY * thread_p)
{
  FILE_TEMPCACHE_ENTRY **tran_files_p = &file_Tempcache->tran_files[file_get_tempcache_entry_index (thread_p)];
  FILE_TEMPCACHE_ENTRY *entry;
  int num = 0;

  for (entry = *tran_files_p; entry != NULL; entry = entry->next)
    {
      num++;
    }
  return num;
}

/*
 * file_tempcache_dump () - dump temporary files cache
 *
 * return  : void
 * fp (in) : dump output
 */
STATIC_INLINE void
file_tempcache_dump (FILE * fp)
{
  FILE_TEMPCACHE_ENTRY *cached_files;

  file_tempcache_lock ();

  fprintf (fp, "DUMPING file manager's temporary files cache.\n");
  fprintf (fp,
	   "  max files = %d, regular files count = %d, numerable files count = %d.\n\n",
	   file_Tempcache->ncached_max, file_Tempcache->ncached_not_numerable, file_Tempcache->ncached_numerable);

  if (file_Tempcache->cached_not_numerable != NULL)
    {
      fprintf (fp, "  cached regular files: \n");
      for (cached_files = file_Tempcache->cached_not_numerable; cached_files != NULL; cached_files = cached_files->next)
	{
	  fprintf (fp, "    VFID = %d|%d, file type = %s \n",
		   VFID_AS_ARGS (&cached_files->vfid), file_type_to_string (cached_files->ftype));
	}
      fprintf (fp, "\n");
    }
  if (file_Tempcache->cached_numerable != NULL)
    {
      fprintf (fp, "  cached numerable files: \n");
      for (cached_files = file_Tempcache->cached_numerable; cached_files != NULL; cached_files = cached_files->next)
	{
	  fprintf (fp, "    VFID = %d|%d, file type = %s \n",
		   VFID_AS_ARGS (&cached_files->vfid), file_type_to_string (cached_files->ftype));
	}
      fprintf (fp, "\n");
    }

  file_tempcache_unlock ();

  /* todo: to print transaction temporary files we need some kind of synchronization... right now each transaction
   *       manages its own list freely. */
}

/************************************************************************/
/* File tracker section                                                 */
/************************************************************************/

/*
 * file_tracker_create () - create file tracker.
 *
 * return                 : error code
 * thread_p (in)          : thread entry
 * vfid_tracker_out (out) : file tracker VFID
 */
int
file_tracker_create (THREAD_ENTRY * thread_p, VFID * vfid_tracker_out)
{
  PAGE_PTR page_tracker = NULL;
  FILE_EXTENSIBLE_DATA *extdata = NULL;
  int error_code = NO_ERROR;

  /* start sys op */
  log_sysop_start (thread_p);

  error_code = file_create_with_npages (thread_p, FILE_TRACKER, 1, NULL, vfid_tracker_out);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto exit;
    }

  error_code = file_alloc_sticky_first_page (thread_p, vfid_tracker_out, &file_Tracker_vpid);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto exit;
    }

  page_tracker = pgbuf_fix (thread_p, &file_Tracker_vpid, NEW_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (page_tracker == NULL)
    {
      ASSERT_ERROR_AND_SET (error_code);
      goto exit;
    }

  pgbuf_set_page_ptype (thread_p, page_tracker, PAGE_FTAB);
  extdata = (FILE_EXTENSIBLE_DATA *) page_tracker;
  file_extdata_init (sizeof (FILE_TRACK_ITEM), DB_PAGESIZE, extdata);
  log_append_redo_data2 (thread_p, RVPGBUF_NEW_PAGE, NULL, page_tracker, (PGLENGTH) PAGE_FTAB, sizeof (*extdata),
			 page_tracker);
  pgbuf_set_dirty (thread_p, page_tracker, DONT_FREE);

  /* success */
  assert (error_code == NO_ERROR);

exit:

  if (page_tracker != NULL)
    {
      pgbuf_unfix (thread_p, page_tracker);
    }

  if (error_code != NO_ERROR)
    {
      log_sysop_abort (thread_p);
      VFID_SET_NULL (vfid_tracker_out);
      VPID_SET_NULL (&file_Tracker_vpid);
    }
  else
    {
      log_sysop_commit (thread_p);

      VFID_COPY (&file_Tracker_vfid, vfid_tracker_out);
    }
  return error_code;
}

/*
 * file_tracker_load () - load file tracker
 *
 * return        : error code
 * thread_p (in) : thread entry
 * vfid (in)     : file tracker file identifier
 */
int
file_tracker_load (THREAD_ENTRY * thread_p, const VFID * vfid)
{
  int error_code = NO_ERROR;

  assert (vfid != NULL && !VFID_ISNULL (vfid));

  error_code = file_get_sticky_first_page (thread_p, vfid, &file_Tracker_vpid);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  file_Tracker_vfid = *vfid;

  return NO_ERROR;
}

/*
 * file_tracker_register () - register new file in file tracker
 *
 * return        : error code
 * thread_p (in) : thread entry
 * vfid (in)     : file identifier
 * ftype (in)    : file type
 * metadata (in) : meta-data about file (if NULL will be initialized as 0).
 */
static int
file_tracker_register (THREAD_ENTRY * thread_p, const VFID * vfid, FILE_TYPE ftype, FILE_TRACK_METADATA * metadata)
{
  FILE_TRACK_ITEM item;
  FILE_EXTENSIBLE_DATA *extdata = NULL;
  PAGE_PTR page_track_head = NULL;
  PAGE_PTR page_track_other = NULL;
  PAGE_PTR page_extdata = NULL;
  bool found;
  int pos;
  LOG_LSA save_lsa;
  int error_code = NO_ERROR;

  assert (vfid != NULL);
  assert (sizeof (item.metadata) == sizeof (item.metadata.metadata_size_tracker));
  assert (!VPID_ISNULL (&file_Tracker_vpid));
  assert (log_check_system_op_is_started (thread_p));

  item.volid = vfid->volid;
  item.fileid = vfid->fileid;
  item.type = (INT16) ftype;

  if (metadata == NULL)
    {
      /* set 0 */
      item.metadata.metadata_size_tracker = 0;
    }
  else
    {
      /* set given metadata */
      item.metadata = *metadata;
    }

  page_track_head = pgbuf_fix (thread_p, &file_Tracker_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (page_track_head == NULL)
    {
      ASSERT_ERROR_AND_SET (error_code);
      return error_code;
    }
  extdata = (FILE_EXTENSIBLE_DATA *) page_track_head;

  error_code = file_extdata_find_not_full (thread_p, &extdata, &page_track_other, &found);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto exit;
    }
  page_extdata = page_track_other != NULL ? page_track_other : page_track_head;

  if (!found)
    {
      /* allocate a new page */
      VPID vpid_new_page;

      error_code = file_alloc (thread_p, &file_Tracker_vfid, &vpid_new_page);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto exit;
	}

      /* add link to new page */
      file_log_extdata_set_next (thread_p, extdata, page_extdata, &vpid_new_page);
      extdata->vpid_next = vpid_new_page;
      pgbuf_set_dirty (thread_p, page_extdata, DONT_FREE);

      if (page_track_other != NULL)
	{
	  /* no longer needed */
	  pgbuf_unfix_and_init (thread_p, page_track_other);
	}

      /* initialize new page */
      page_track_other = pgbuf_fix (thread_p, &vpid_new_page, NEW_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
      if (page_track_other == NULL)
	{
	  ASSERT_ERROR_AND_SET (error_code);
	  goto exit;
	}
      page_extdata = page_track_other;
      extdata = (FILE_EXTENSIBLE_DATA *) page_extdata;

      pgbuf_set_page_ptype (thread_p, page_extdata, PAGE_FTAB);
      file_extdata_init (sizeof (FILE_TRACK_ITEM), DB_PAGESIZE, extdata);
      log_append_redo_data2 (thread_p, RVPGBUF_NEW_PAGE, NULL, page_extdata, (PGLENGTH) PAGE_FTAB, sizeof (*extdata),
			     page_extdata);
      pgbuf_set_dirty (thread_p, page_extdata, DONT_FREE);
    }

  assert (page_extdata != NULL);
  assert (extdata != NULL);
  assert (extdata == (FILE_EXTENSIBLE_DATA *) page_extdata);
  assert (!file_extdata_is_full (extdata));

  file_extdata_find_ordered (extdata, &item, file_compare_track_items, &found, &pos);
  if (found)
    {
      /* impossible */
      assert_release (false);
      error_code = ER_FAILED;
      goto exit;
    }

  save_lsa = *pgbuf_get_lsa (page_extdata);
  file_extdata_insert_at (extdata, pos, 1, &item);
  file_log_extdata_add (thread_p, extdata, page_extdata, pos, 1, &item);
  pgbuf_set_dirty (thread_p, page_extdata, DONT_FREE);

  file_log ("file_tracker_register", "added " FILE_TRACK_ITEM_MSG ", to page %d|%d, prev_lsa = %lld|%d, "
	    "crt_lsa = %lld|%d, at pos %d ", FILE_TRACK_ITEM_AS_ARGS (&item), PGBUF_PAGE_VPID_AS_ARGS (page_extdata),
	    LSA_AS_ARGS (&save_lsa), PGBUF_PAGE_LSA_AS_ARGS (page_extdata), pos);

  /* success */
  assert (error_code == NO_ERROR);

exit:
  if (page_track_other != NULL)
    {
      pgbuf_unfix (thread_p, page_track_other);
    }

  if (page_track_head != NULL)
    {
      pgbuf_unfix (thread_p, page_track_head);
    }

  return error_code;
}

/*
 * file_tracker_unregister () - unregister file from tracker
 *
 * return        : error code
 * thread_p (in) : thread entry
 * vfid (in)     : file identifier
 */
static int
file_tracker_unregister (THREAD_ENTRY * thread_p, const VFID * vfid)
{
  PAGE_PTR page_track_head = NULL;
  FILE_EXTENSIBLE_DATA *extdata = NULL;
  FILE_TRACK_ITEM item_search;
  bool found;
  int pos;
  PAGE_PTR page_track_other;
  PAGE_PTR page_extdata;
  VPID vpid_merged;
  LOG_LSA save_lsa;
  int error_code = NO_ERROR;

  assert (vfid != NULL && !VFID_ISNULL (vfid));
  assert (log_check_system_op_is_started (thread_p));

  page_track_head = pgbuf_fix (thread_p, &file_Tracker_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (page_track_head == NULL)
    {
      ASSERT_ERROR_AND_SET (error_code);
      return error_code;
    }
  extdata = (FILE_EXTENSIBLE_DATA *) page_track_head;

  item_search.volid = vfid->volid;
  item_search.fileid = vfid->fileid;
  error_code = file_extdata_search_item (thread_p, &extdata, &item_search, file_compare_track_items, true, true,
					 &found, &pos, &page_track_other);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto exit;
    }
  if (!found)
    {
      assert_release (false);
      error_code = ER_FAILED;
      goto exit;
    }

  /* remove from pos */
  page_extdata = page_track_other != NULL ? page_track_other : page_track_head;
  save_lsa = *pgbuf_get_lsa (page_extdata);
  file_log_extdata_remove (thread_p, extdata, page_extdata, pos, 1);
  file_extdata_remove_at (extdata, pos, 1);

  file_log ("file_tracker_unregister", "removed VFID %d|%d from page_extdata %d|%d, prev_lsa = %lld|%d, "
	    "crt_lsa = %lld|%d, at pos %d ", VFID_AS_ARGS (vfid), PGBUF_PAGE_VPID_AS_ARGS (page_extdata),
	    LSA_AS_ARGS (&save_lsa), PGBUF_PAGE_LSA_AS_ARGS (page_extdata), pos);

  /* try to merge pages */
  error_code = file_extdata_try_merge (thread_p, page_extdata, extdata, file_compare_track_items,
				       RVFL_EXTDATA_MERGE_COMPARE_TRACK_ITEM, &vpid_merged);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto exit;
    }

  if (!VPID_ISNULL (&vpid_merged))
    {
      /* merged page. deallocate it */
      file_log ("file_tracker_unregister", "deallocate page %d|%d ", VPID_AS_ARGS (&vpid_merged));

      error_code = file_dealloc (thread_p, &file_Tracker_vfid, &vpid_merged, FILE_TRACKER);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto exit;
	}
    }

  /* success */
  assert (error_code == NO_ERROR);

exit:
  if (page_track_other != NULL)
    {
      pgbuf_unfix (thread_p, page_track_other);
    }
  if (page_track_head != NULL)
    {
      pgbuf_unfix (thread_p, page_track_head);
    }
  return error_code;
}

/*
 * file_rv_extdata_merge_compare_track_item_redo () - Redo extensible data components merge (ordered by
 *                                                    file_compare_track_items)
 *
 * return	     : NO_ERROR
 * thread_p (in)     : Thread entry
 * rcv (in)	     : Recovery data
 */
int
file_rv_extdata_merge_compare_track_item_redo (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  return file_rv_extdata_merge_redo_internal (thread_p, rcv, file_compare_track_items);
}

/*
 * file_tracker_apply_to_file () - search for file tracker item and apply function
 *
 * return        : error code
 * thread_p (in) : thread entry
 * vfid (in)     : file identifier
 * mode (in)     : latch mode for tracker pages
 * func (in)     : apply function for found item
 * args (in)     : arguments for applied function
 */
static int
file_tracker_apply_to_file (THREAD_ENTRY * thread_p, const VFID * vfid, PGBUF_LATCH_MODE mode,
			    FILE_TRACK_ITEM_FUNC func, void *args)
{
  PAGE_PTR page_track_head = NULL;
  PAGE_PTR page_track_other = NULL;
  FILE_EXTENSIBLE_DATA *extdata;
  FILE_TRACK_ITEM item_search;
  bool for_write = (mode == PGBUF_LATCH_WRITE);
  bool found = false;
  int pos = -1;
  FILE_TRACK_ITEM *item_in_page = NULL;
  int error_code = NO_ERROR;

  assert (func != NULL);
  assert (mode == PGBUF_LATCH_READ || mode == PGBUF_LATCH_WRITE);

  page_track_head = pgbuf_fix (thread_p, &file_Tracker_vpid, OLD_PAGE, mode, PGBUF_UNCONDITIONAL_LATCH);
  if (page_track_head == NULL)
    {
      ASSERT_ERROR_AND_SET (error_code);
      return error_code;
    }
  extdata = (FILE_EXTENSIBLE_DATA *) page_track_head;

  item_search.volid = vfid->volid;
  item_search.fileid = vfid->fileid;
  error_code = file_extdata_search_item (thread_p, &extdata, &item_search, file_compare_track_items, true, for_write,
					 &found, &pos, &page_track_other);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto exit;
    }
  if (!found)
    {
      assert_release (false);
      error_code = ER_FAILED;
      goto exit;
    }

  error_code = func (thread_p, page_track_other != NULL ? page_track_other : page_track_head, extdata, pos, NULL, args);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto exit;
    }

  /* success */
  assert (error_code == NO_ERROR);

exit:
  if (page_track_other != NULL)
    {
      pgbuf_unfix (thread_p, page_track_other);
    }
  if (page_track_head != NULL)
    {
      pgbuf_unfix (thread_p, page_track_head);
    }
  return error_code;
}

/*
 * file_tracker_map () - map item function to all tracked files
 *
 * return          : error code
 * thread_p (in)   : thread entry
 * latch_mode (in) : latch mode
 * func (in)       : function called for each item
 * args (in)       : arguments for function
 */
static int
file_tracker_map (THREAD_ENTRY * thread_p, PGBUF_LATCH_MODE latch_mode, FILE_TRACK_ITEM_FUNC func, void *args)
{
  PAGE_PTR page_track_head = NULL;
  PAGE_PTR page_track_other = NULL;
  PAGE_PTR page_extdata = NULL;
  FILE_EXTENSIBLE_DATA *extdata;
  VPID vpid_next = VPID_INITIALIZER;
  bool stop;
  int index_item;
  int error_code = NO_ERROR;

  page_track_head = pgbuf_fix (thread_p, &file_Tracker_vpid, OLD_PAGE, latch_mode, PGBUF_UNCONDITIONAL_LATCH);
  if (page_track_head == NULL)
    {
      ASSERT_ERROR_AND_SET (error_code);
      return error_code;
    }

  page_extdata = page_track_head;
  while (true)
    {
      extdata = (FILE_EXTENSIBLE_DATA *) page_extdata;
      for (index_item = 0; index_item < file_extdata_item_count (extdata); index_item++)
	{
	  error_code = func (thread_p, page_extdata, extdata, index_item, &stop, args);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      goto exit;
	    }
	  if (stop)
	    {
	      /* early out */
	      goto exit;
	    }
	}

      vpid_next = extdata->vpid_next;
      if (page_track_other != NULL)
	{
	  pgbuf_unfix_and_init (thread_p, page_track_other);
	}
      if (VPID_ISNULL (&vpid_next))
	{
	  break;
	}

      page_track_other = pgbuf_fix (thread_p, &vpid_next, OLD_PAGE, latch_mode, PGBUF_UNCONDITIONAL_LATCH);
      if (page_track_other == NULL)
	{
	  ASSERT_ERROR_AND_SET (error_code);
	  goto exit;
	}

      page_extdata = page_track_other;
    }

  /* success */
  assert (error_code == NO_ERROR);

exit:
  if (page_track_other != NULL)
    {
      pgbuf_unfix (thread_p, page_track_other);
    }
  if (page_track_head != NULL)
    {
      pgbuf_unfix (thread_p, page_track_head);
    }
  return error_code;
}

/*
 * file_rv_tracker_reuse_heap () - recover reuse heap file in tracker
 *
 * return        : NO_ERROR
 * thread_p (in) : thread entry
 * rcv (in)      : recovery data
 */
int
file_rv_tracker_reuse_heap (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  FILE_EXTENSIBLE_DATA *extdata = NULL;
  FILE_TRACK_ITEM *item = NULL;

  assert (rcv->length == 0);
  assert (rcv->pgptr != NULL);
  assert (rcv->offset >= 0);

  extdata = (FILE_EXTENSIBLE_DATA *) rcv->pgptr;
  assert (rcv->offset < file_extdata_item_count (extdata));

  item = (FILE_TRACK_ITEM *) file_extdata_at (extdata, rcv->offset);
  assert (item->type == FILE_HEAP);
  assert (item->metadata.heap.is_marked_deleted);

  item->metadata.heap.is_marked_deleted = false;

  file_log ("file_rv_tracker_reuse_heap", "recovery reuse heap " FILE_TRACK_ITEM_MSG ", in "
	    PGBUF_PAGE_STATE_MSG ("tracker page"), FILE_TRACK_ITEM_AS_ARGS (item), PGBUF_PAGE_STATE_ARGS (rcv->pgptr));

  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);
  return NO_ERROR;
}

/*
 * file_tracker_item_reuse_heap () - reuse heap file if marked as deleted
 *
 * return            : error code
 * thread_p (in)     : thread entry
 * page_of_item (in) : page of item
 * extdata (in)      : extensible data
 * index_item (in)   : index of item
 * args (out)        : reused file identifier
 */
static int
file_tracker_item_reuse_heap (THREAD_ENTRY * thread_p, PAGE_PTR page_of_item, FILE_EXTENSIBLE_DATA * extdata,
			      int index_item, bool * stop, void *args)
{
  FILE_TRACK_ITEM *item = (FILE_TRACK_ITEM *) file_extdata_at (extdata, index_item);
  VFID *vfid;
  LOG_LSA save_lsa;

  if (item->type != (INT16) FILE_HEAP)
    {
      return NO_ERROR;
    }
  if (!item->metadata.heap.is_marked_deleted)
    {
      return NO_ERROR;
    }

  /* reuse this heap */
  vfid = (VFID *) args;
  vfid->volid = item->volid;
  vfid->fileid = item->fileid;

  save_lsa = *pgbuf_get_lsa (page_of_item);

  item->metadata.heap.is_marked_deleted = false;
  log_append_undoredo_data2 (thread_p, RVFL_TRACKER_HEAP_REUSE, NULL, page_of_item, index_item, sizeof (*vfid), 0, vfid,
			     NULL);
  pgbuf_set_dirty (thread_p, page_of_item, DONT_FREE);

  file_log ("file_tracker_item_reuse_heap", "reuse heap file %d|%d; tracker page %d|%d, prev_lsa = %lld|%d, "
	    "crt_lsa = %lld|%d, item at pos %d ", VFID_AS_ARGS (vfid), PGBUF_PAGE_VPID_AS_ARGS (page_of_item),
	    LSA_AS_ARGS (&save_lsa), PGBUF_PAGE_LSA_AS_ARGS (page_of_item), index_item);

  /* stop looking */
  *stop = true;
  return NO_ERROR;
}

/*
 * file_tracker_reuse_heap () - search for heap file marked as deleted and reuse.
 *
 * return        : error code
 * thread_p (in) : thread entry
 * vfid_out (in) : VFID of reused file or NULL VFID if no file was found
 */
int
file_tracker_reuse_heap (THREAD_ENTRY * thread_p, VFID * vfid_out)
{
  assert (vfid_out != NULL);

  VFID_SET_NULL (vfid_out);

  return file_tracker_map (thread_p, PGBUF_LATCH_WRITE, file_tracker_item_reuse_heap, vfid_out);
}

/*
 * file_tracker_item_mark_heap_deleted () - FILE_TRACK_ITEM_FUNC to mark heap entry as deleted
 *
 * return            : error code
 * thread_p (in)     : thread entry
 * page_of_item (in) : page of item
 * extdata (in)      : extensible data
 * index_item (in)   : index of item
 * stop (in)         : not used
 * args (in)         : FILE_TRACK_MARK_HEAP_DELETED_CONTEXT *
 */
static int
file_tracker_item_mark_heap_deleted (THREAD_ENTRY * thread_p, PAGE_PTR page_of_item, FILE_EXTENSIBLE_DATA * extdata,
				     int index_item, bool * stop, void *args)
{
  FILE_TRACK_ITEM *item = (FILE_TRACK_ITEM *) file_extdata_at (extdata, index_item);
  FILE_TRACK_MARK_HEAP_DELETED_CONTEXT *context = (FILE_TRACK_MARK_HEAP_DELETED_CONTEXT *) args;
  LOG_LSA save_lsa;

  assert ((FILE_TYPE) item->type == FILE_HEAP);
  assert (!item->metadata.heap.is_marked_deleted);

  item->metadata.heap.is_marked_deleted = true;

  save_lsa = *pgbuf_get_lsa (page_of_item);
  if (context->is_undo)
    {
      log_append_compensate_with_undo_nxlsa (thread_p, RVFL_TRACKER_HEAP_MARK_DELETED,
					     pgbuf_get_vpid_ptr (page_of_item), index_item, page_of_item, 0, NULL,
					     LOG_FIND_CURRENT_TDES (thread_p), &context->ref_lsa);
    }
  else
    {
      LOG_DATA_ADDR addr = LOG_DATA_ADDR_INITIALIZER;
      addr.pgptr = page_of_item;
      addr.offset = index_item;
      log_append_run_postpone (thread_p, RVFL_TRACKER_HEAP_MARK_DELETED, &addr, pgbuf_get_vpid_ptr (page_of_item),
			       0, NULL, &context->ref_lsa);
    }
  pgbuf_set_dirty (thread_p, page_of_item, DONT_FREE);

  file_log ("file_tracker_item_mark_heap_deleted", "mark delete heap file %d|%d; "
	    PGBUF_PAGE_MODIFY_MSG ("tracker page") ", item at pos %d, on %s, ref_lsa = %lld|%d",
	    item->volid, item->fileid, PGBUF_PAGE_MODIFY_ARGS (page_of_item, &save_lsa), index_item,
	    context->is_undo ? "undo" : "postpone", LSA_AS_ARGS (&context->ref_lsa));

  return NO_ERROR;
}

/*
 * file_rv_tracker_mark_heap_deleted () - search for heap file and mark it for delete
 *
 * return        : error code
 * thread_p (in) : thread entry
 * rcv (in)      : recovery data
 * is_undo (in)  : true if called on undo/rollback, false if called on postpone
 */
int
file_rv_tracker_mark_heap_deleted (THREAD_ENTRY * thread_p, LOG_RCV * rcv, bool is_undo)
{
  VFID *vfid = (VFID *) rcv->data;
  FILE_TRACK_MARK_HEAP_DELETED_CONTEXT context;
  int error_code = NO_ERROR;

  assert (rcv->length == sizeof (*vfid));
  assert (!LSA_ISNULL (&rcv->reference_lsa));

  context.is_undo = is_undo;
  context.ref_lsa = rcv->reference_lsa;

  error_code = file_tracker_apply_to_file (thread_p, vfid, PGBUF_LATCH_WRITE, file_tracker_item_mark_heap_deleted,
					   &context);
  if (error_code != NO_ERROR)
    {
      assert_release (false);
    }

  return error_code;
}

/*
 * file_rv_tracker_mark_heap_deleted_compensate_or_run_postpone () - used for recovery as compensate or run postpone
 *                                                                   when heap file is marked as deleted.
 *
 * return        : NO_ERROR
 * thread_p (in) : thread entry
 * rcv (in)      : recovery data
 */
int
file_rv_tracker_mark_heap_deleted_compensate_or_run_postpone (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  FILE_EXTENSIBLE_DATA *extdata = NULL;
  FILE_TRACK_ITEM *item = NULL;

  assert (rcv->length == 0);
  assert (rcv->pgptr != NULL);
  assert (rcv->offset >= 0);

  extdata = (FILE_EXTENSIBLE_DATA *) rcv->pgptr;
  assert (rcv->offset < file_extdata_item_count (extdata));

  item = (FILE_TRACK_ITEM *) file_extdata_at (extdata, rcv->offset);
  assert (item->type == FILE_HEAP);
  assert (!item->metadata.heap.is_marked_deleted);

  item->metadata.heap.is_marked_deleted = true;

  file_log ("file_rv_tracker_mark_heap_deleted_compensate_or_run_postpone", "mark heap deleted" FILE_TRACK_ITEM_MSG
	    ", in " PGBUF_PAGE_STATE_MSG ("tracker page"), FILE_TRACK_ITEM_AS_ARGS (item),
	    PGBUF_PAGE_STATE_ARGS (rcv->pgptr));

  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);
  return NO_ERROR;
}

#if defined (SA_MODE)
/*
 * file_tracker_reclaim_marked_deleted () - reclaim all files marked as deleted. this can work only in stand-alone
 *                                          mode.
 *
 * return        : error code
 * thread_p (in) : thread entry
 */
int
file_tracker_reclaim_marked_deleted (THREAD_ENTRY * thread_p)
{
  PAGE_PTR page_track_head = NULL;
  PAGE_PTR page_track_other = NULL;

  PAGE_PTR page_extdata = NULL;
  FILE_EXTENSIBLE_DATA *extdata = NULL;
  FILE_TRACK_ITEM *item = NULL;
  VPID vpid_next;
  VFID vfid;
  int idx_item;

  VPID vpid_merged = VPID_INITIALIZER;

  LOG_LSA save_lsa;

  int error_code = NO_ERROR;

  assert (!VPID_ISNULL (&file_Tracker_vpid));
  assert (!VFID_ISNULL (&file_Tracker_vfid));

  /* how this works:
   * do two steps:
   * 1. go through all tracker items and identify heap file entires marked as deleted. deallocate the files and remove
   *    the items from tracker.
   * 2. loop through tracker pages and try to merge two-by-two.
   */

  page_track_head = pgbuf_fix (thread_p, &file_Tracker_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (page_track_head == NULL)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  log_sysop_start (thread_p);

  /* first step: process tracker data, search for files marked deleted, destroy them and remove them from tracker */
  page_extdata = page_track_head;

  while (true)
    {
      extdata = (FILE_EXTENSIBLE_DATA *) page_extdata;
      for (idx_item = 0; idx_item < file_extdata_item_count (extdata);)
	{
	  item = (FILE_TRACK_ITEM *) file_extdata_at (extdata, idx_item);
	  if ((FILE_TYPE) item->type == FILE_HEAP && item->metadata.heap.is_marked_deleted)
	    {
	      /* destroy file */
	      vfid.volid = item->volid;
	      vfid.fileid = item->fileid;
	      error_code = file_destroy (thread_p, &vfid);
	      if (error_code != NO_ERROR)
		{
		  ASSERT_ERROR ();
		  goto exit;
		}
	      /* already removed by file_destroy. we don't have to increment idx_item */
	    }
	  else
	    {
	      /* go to next */
	      idx_item++;
	    }
	}

      /* go to next extensible data page */
      vpid_next = extdata->vpid_next;
      if (page_track_other != NULL)
	{
	  pgbuf_unfix_and_init (thread_p, page_track_other);
	}
      if (VPID_ISNULL (&vpid_next))
	{
	  /* no next page */
	  break;
	}
      page_track_other = pgbuf_fix (thread_p, &vpid_next, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
      if (page_track_other == NULL)
	{
	  ASSERT_ERROR_AND_SET (error_code);
	  goto exit;
	}
      /* loop again */
    }

  /* try merges */
  assert (page_track_other == NULL);
  page_extdata = page_track_head;
  while (true)
    {
      extdata = (FILE_EXTENSIBLE_DATA *) page_extdata;
      error_code =
	file_extdata_try_merge (thread_p, page_extdata, extdata, file_compare_track_items,
				RVFL_EXTDATA_MERGE_COMPARE_TRACK_ITEM, &vpid_merged);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto exit;
	}
      if (VPID_ISNULL (&vpid_merged))
	{
	  /* not merged. go to next page */
	  vpid_next = extdata->vpid_next;
	  if (page_track_other != NULL)
	    {
	      pgbuf_unfix_and_init (thread_p, page_track_other);
	    }
	  if (VPID_ISNULL (&vpid_next))
	    {
	      /* no next page */
	      break;
	    }
	  page_track_other = pgbuf_fix (thread_p, &vpid_next, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
	  if (page_track_other == NULL)
	    {
	      ASSERT_ERROR_AND_SET (error_code);
	      goto exit;
	    }
	}
      else
	{
	  /* pages merged. */

	  /* deallocate merged page */
	  error_code = file_dealloc (thread_p, &file_Tracker_vfid, &vpid_next, FILE_TRACKER);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      goto exit;
	    }

	  /* we can try to merge into this page again. fall through without advancing */
	}
    }

  /* finished successfully */
  assert (error_code == NO_ERROR);

exit:

  assert (log_check_system_op_is_started (thread_p));
  if (error_code != NO_ERROR)
    {
      log_sysop_abort (thread_p);
    }
  else
    {
      log_sysop_commit (thread_p);
    }
  if (page_track_other != NULL)
    {
      pgbuf_unfix (thread_p, page_track_other);
    }
  if (page_track_head != NULL)
    {
      pgbuf_unfix (thread_p, page_track_head);
    }
  return error_code;
}
#endif /* SA_MODE */

/*
 * file_tracker_get_and_protect () - get a file from tracker. if we want to get b-tree or heap files, we must first
 *                                   protect them by locking class.
 *
 * return            : error code
 * thread_p (in)     : thread entry
 * desired_type (in) : desired file type. FILE_UNKNOWN_TYPE for any type.
 * item (in)         : tracker item
 * class_oid (out)   : output locked class OID (for b-tree and heap). NULL OID if no class is locked.
 * stop (out)        : output true when item is accepted
 */
STATIC_INLINE int
file_tracker_get_and_protect (THREAD_ENTRY * thread_p, FILE_TYPE desired_type, FILE_TRACK_ITEM * item, OID * class_oid,
			      bool * stop)
{
  VPID vpid_fhead;
  PAGE_PTR page_fhead = NULL;
  FILE_HEADER *fhead = NULL;
  int error_code = NO_ERROR;

  assert (class_oid != NULL && OID_ISNULL (class_oid));

  /* how it works:
   * this is part of the tracker iterate without holding latch on tracker during the entire iteration. however, it can
   * only work if the files processed outside latch are protected from being destroyed. otherwise, resuming is
   * impossible. most file types are not mutable, so they don't need protection. however, for b-tree and heap files we
   * need to read class OID from descriptor and try to lock it (conditionally!). this is a best-effort approach, if the
   * locking fails, we just skip the file. */

  /* check file type is right */
  switch (desired_type)
    {
    case FILE_UNKNOWN_TYPE:
      /* accept any type */
      break;
    case FILE_HEAP:
    case FILE_HEAP_REUSE_SLOTS:
      /* accept heap or heap reuse slots */
      if ((FILE_TYPE) item->type != FILE_HEAP && (FILE_TYPE) item->type != FILE_HEAP_REUSE_SLOTS)
	{
	  /* reject */
	  return NO_ERROR;
	}
      break;
    default:
      /* accept the exact file type */
      if ((FILE_TYPE) item->type != desired_type)
	{
	  /* reject */
	  return NO_ERROR;
	}
      break;
    }

  /* now we need to make sure the file is protected. most types are not mutable (cannot be created or destroyed during
   * run-time), but b-tree and heap files must be protected by lock. */
  if ((FILE_TYPE) item->type == FILE_HEAP)
    {
      /* these files may be marked for delete. check this is not a deleted file */
      if (item->metadata.heap.is_marked_deleted)
	{
	  /* reject */
	  return NO_ERROR;
	}
      /* we need to protect with lock. fall through */
    }
  else if ((FILE_TYPE) item->type == FILE_HEAP_REUSE_SLOTS || (FILE_TYPE) item->type == FILE_BTREE)
    {
      /* we need to protect with lock. fall through */
    }
  else
    {
      /* immutable file types. no protection required */
      *stop = true;
      return NO_ERROR;
    }

  /* we need to fix file header and read the class oid from descriptor */
  vpid_fhead.volid = item->volid;
  vpid_fhead.pageid = item->fileid;
  page_fhead = pgbuf_fix (thread_p, &vpid_fhead, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (page_fhead == NULL)
    {
      ASSERT_ERROR_AND_SET (error_code);
      return error_code;
    }
  fhead = (FILE_HEADER *) page_fhead;
  file_header_sanity_check (fhead);

  /* read class OID */
  if ((FILE_TYPE) item->type == FILE_BTREE)
    {
      *class_oid = fhead->descriptor.btree.class_oid;
    }
  else
    {
      *class_oid = fhead->descriptor.heap.class_oid;
    }
  pgbuf_unfix (thread_p, page_fhead);

  if (OID_ISNULL (class_oid))
    {
      /* this must be boot_Db_parm file; cannot be deleted so we don't need lock. */
      *stop = true;
      return NO_ERROR;
    }

  /* try conditional lock */
  if (lock_object (thread_p, class_oid, oid_Root_class_oid, SCH_S_LOCK, LK_COND_LOCK) != LK_GRANTED)
    {
      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_CANNOT_CHECK_FILE, 5, item->volid, item->fileid,
	      OID_AS_ARGS (class_oid));
      OID_SET_NULL (class_oid);
    }
  else
    {
      /* stop at this file */
      *stop = true;
    }

  /* finished */
  return NO_ERROR;
}

/*
 * file_tracker_interruptable_iterate () - iterate in file tracker and get a new file of desired type
 *
 * return             : error code
 * thread_p (in)      : thread entry
 * desired_ftype (in) : desired type
 * vfid (in)          : file identifier and iterator cursor. iterate must start with a NULL identifier
 * class_oid (in)     : locked class OID (used to protect b-tree and heap files)
 */
int
file_tracker_interruptable_iterate (THREAD_ENTRY * thread_p, FILE_TYPE desired_ftype, VFID * vfid, OID * class_oid)
{
  PAGE_PTR page_track_head = NULL;
  PAGE_PTR page_track_other = NULL;
  PAGE_PTR page_extdata = NULL;
  FILE_EXTENSIBLE_DATA *extdata = NULL;
  FILE_TRACK_ITEM *item;
  bool found = false;
  bool stop = false;
  int idx_item;
  VPID vpid_next;
#if !defined (NDEBUG)
  VFID vfid_prev_cursor = *vfid;
#endif /* !NDEBUG */
  int error_code = NO_ERROR;

  assert (vfid != NULL);
  assert (class_oid != NULL);

  /* how it works:
   * start from given VFID and get a new file of desired type. for b-tree and heap files, we also need to lock their
   * class OID in order to protect them from being removed. otherwise we could not resume in next iteration. */

  page_track_head = pgbuf_fix (thread_p, &file_Tracker_vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (page_track_head == NULL)
    {
      ASSERT_ERROR_AND_SET (error_code);
      return error_code;
    }

  if (!OID_ISNULL (class_oid))
    {
      /* now that we fixed tracker header page, we no longer need lock protection. */
      lock_unlock_object (thread_p, class_oid, oid_Root_class_oid, SCH_S_LOCK, true);
      OID_SET_NULL (class_oid);
    }

  extdata = (FILE_EXTENSIBLE_DATA *) page_track_head;
  if (VFID_ISNULL (vfid))
    {
      /* starting position is first */
      idx_item = 0;
      page_extdata = page_track_head;
    }
  else
    {
      FILE_TRACK_ITEM item_search;
      item_search.volid = vfid->volid;
      item_search.fileid = vfid->fileid;
      error_code =
	file_extdata_search_item (thread_p, &extdata, &item_search, file_compare_track_items, true, false, &found,
				  &idx_item, &page_track_other);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto exit;
	}
      if (!found)
	{
	  /* we are in trouble */
	  assert_release (false);
	  error_code = ER_FAILED;
	  goto exit;
	}
      page_extdata = page_track_other != NULL ? page_track_other : page_track_head;
      /* move to next */
      idx_item++;
    }

  assert (extdata == (FILE_EXTENSIBLE_DATA *) page_extdata);
  /* start iterating until stop is issued */
  while (true)
    {
      for (; idx_item < file_extdata_item_count (extdata); idx_item++)
	{
	  item = (FILE_TRACK_ITEM *) file_extdata_at (extdata, idx_item);
	  error_code = file_tracker_get_and_protect (thread_p, desired_ftype, item, class_oid, &stop);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      goto exit;
	    }
	  if (stop)
	    {
	      vfid->volid = item->volid;
	      vfid->fileid = item->fileid;
	      goto exit;
	    }
	}
      vpid_next = extdata->vpid_next;
      if (page_track_other != NULL)
	{
	  pgbuf_unfix_and_init (thread_p, page_track_other);
	}
      if (VPID_ISNULL (&vpid_next))
	{
	  /* ended */
	  break;
	}
      page_track_other = pgbuf_fix (thread_p, &vpid_next, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
      if (page_track_other == NULL)
	{
	  ASSERT_ERROR_AND_SET (error_code);
	  goto exit;
	}
      page_extdata = page_track_other;
      extdata = (FILE_EXTENSIBLE_DATA *) page_extdata;
      idx_item = 0;
    }

  /* end of tracker */
  VFID_SET_NULL (vfid);
  assert (OID_ISNULL (class_oid));
  assert (error_code == NO_ERROR);

exit:
  if (page_track_other != NULL)
    {
      pgbuf_unfix (thread_p, page_track_other);
    }
  if (page_track_head != NULL)
    {
      pgbuf_unfix (thread_p, page_track_head);
    }

  /* check cursor is not repeated (unless there is an error) */
  assert (error_code != NO_ERROR || VFID_ISNULL (&vfid_prev_cursor) || !VFID_EQ (&vfid_prev_cursor, vfid));

  return error_code;
}

/*
 * file_tracker_item_dump () - FILE_TRACK_ITEM_FUNC to dump file
 *
 * return            : error code
 * thread_p (in)     : thread entry
 * page_of_item (in) : tracker page
 * extdata (in)      : tracker extensible data
 * index_item (in)   : item index
 * stop (in)         : not used
 * args (in)         : FILE *
 */
static int
file_tracker_item_dump (THREAD_ENTRY * thread_p, PAGE_PTR page_of_item, FILE_EXTENSIBLE_DATA * extdata, int index_item,
			bool * stop, void *args)
{
  VFID vfid;
  FILE_TRACK_ITEM *item;
  FILE *fp = (FILE *) args;
  int error_code = NO_ERROR;

  item = (FILE_TRACK_ITEM *) file_extdata_at (extdata, index_item);
  vfid.volid = item->volid;
  vfid.fileid = item->fileid;

  error_code = file_dump (thread_p, &vfid, fp);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  return NO_ERROR;
}

/*
 * file_tracker_dump () - dump all files in file tracker
 *
 * return        : error code
 * thread_p (in) : thread entry
 * fp (in)       : output file
 */
int
file_tracker_dump (THREAD_ENTRY * thread_p, FILE * fp)
{
  fprintf (fp, "\n\n DUMPING TRACKED FILES \n");
  return file_tracker_map (thread_p, PGBUF_LATCH_READ, file_tracker_item_dump, fp);
}

/*
 * file_tracker_item_dump_capacity () - FILE_TRACK_ITEM_FUNC to dump file capacity
 *
 * return            : error code
 * thread_p (in)     : thread entry
 * page_of_item (in) : tracker page
 * extdata (in)      : tracker extensible data
 * index_item (in)   : item index
 * stop (in)         : not used
 * args (in)         : FILE *
 */
static int
file_tracker_item_dump_capacity (THREAD_ENTRY * thread_p, PAGE_PTR page_of_item, FILE_EXTENSIBLE_DATA * extdata,
				 int index_item, bool * stop, void *args)
{
  FILE_TRACK_ITEM *item;
  VPID vpid_fhead;
  PAGE_PTR page_fhead = NULL;
  FILE_HEADER *fhead = NULL;
  FILE *fp = (FILE *) args;
  int error_code = NO_ERROR;

  item = (FILE_TRACK_ITEM *) file_extdata_at (extdata, index_item);

  vpid_fhead.volid = item->volid;
  vpid_fhead.pageid = item->fileid;
  page_fhead = pgbuf_fix (thread_p, &vpid_fhead, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (page_fhead == NULL)
    {
      ASSERT_ERROR_AND_SET (error_code);
      return error_code;
    }
  fhead = (FILE_HEADER *) page_fhead;
  file_header_sanity_check (fhead);

  fprintf (fp, "%4d|%4d %5d  %-22s ", item->volid, item->fileid, fhead->n_page_user, file_type_to_string (fhead->type));
  if ((FILE_TYPE) item->type == FILE_HEAP && item->metadata.heap.is_marked_deleted)
    {
      fprintf (fp, "Marked as deleted... ");
    }

  file_header_dump_descriptor (thread_p, fhead, fp);

  pgbuf_unfix (thread_p, page_fhead);
  return NO_ERROR;
}

/*
 * file_tracker_dump_all_capacities () - dump capacities for all files
 *
 * return        : error code
 * thread_p (in) : thread entry
 * fp (in)       : output file
 */
int
file_tracker_dump_all_capacities (THREAD_ENTRY * thread_p, FILE * fp)
{
  int error_code = NO_ERROR;

  fprintf (fp, "    VFID   npages    type             FDES\n");
  error_code = file_tracker_map (thread_p, PGBUF_LATCH_READ, file_tracker_item_dump_capacity, fp);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  return NO_ERROR;
}

/*
 * file_tracker_item_dump_heap () - FILE_TRACK_ITEM_FUNC to dump heap file
 *
 * return            : error code
 * thread_p (in)     : thread entry
 * page_of_item (in) : tracker page
 * extdata (in)      : tracker extensible data
 * index_item (in)   : item index
 * stop (in)         : not used
 * args (in)         : context
 */
static int
file_tracker_item_dump_heap (THREAD_ENTRY * thread_p, PAGE_PTR page_of_item, FILE_EXTENSIBLE_DATA * extdata,
			     int index_item, bool * stop, void *args)
{
  FILE_TRACK_ITEM *item;
  HFID hfid;
  FILE_TRACKER_DUMP_HEAP_CONTEXT *context = (FILE_TRACKER_DUMP_HEAP_CONTEXT *) args;
  int error_code = NO_ERROR;

  item = (FILE_TRACK_ITEM *) file_extdata_at (extdata, index_item);
  if ((FILE_TYPE) item->type != FILE_HEAP && (FILE_TYPE) item->type != FILE_HEAP_REUSE_SLOTS)
    {
      return NO_ERROR;
    }

  hfid.vfid.volid = item->volid;
  hfid.vfid.fileid = item->fileid;

  error_code = heap_get_hfid_from_vfid (thread_p, &hfid.vfid, &hfid);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  heap_dump (thread_p, context->fp, &hfid, context->dump_records);

  return NO_ERROR;
}

/*
 * file_tracker_dump_all_heap () - dump all heap files
 *
 * return            : error code
 * thread_p (in)     : thread entry
 * fp (in)           : output file
 * dump_records (in) : true to dump records
 */
int
file_tracker_dump_all_heap (THREAD_ENTRY * thread_p, FILE * fp, bool dump_records)
{
  FILE_TRACKER_DUMP_HEAP_CONTEXT context;

  context.fp = fp;
  context.dump_records = dump_records;

  return file_tracker_map (thread_p, PGBUF_LATCH_READ, file_tracker_item_dump_heap, &context);
}

/*
 * file_tracker_item_dump_heap_capacity () - FILE_TRACK_ITEM_FUNC to dump heap file capacity
 *
 * return            : error code
 * thread_p (in)     : thread entry
 * page_of_item (in) : tracker page
 * extdata (in)      : tracker extensible data
 * index_item (in)   : item index
 * stop (in)         : not used
 * args (in)         : context
 */
static int
file_tracker_item_dump_heap_capacity (THREAD_ENTRY * thread_p, PAGE_PTR page_of_item, FILE_EXTENSIBLE_DATA * extdata,
				      int index_item, bool * stop, void *args)
{
  FILE_TRACK_ITEM *item;
  HFID hfid;
  FILE *fp = (FILE *) args;
  INT64 num_recs = 0;
  INT64 num_recs_relocated = 0;
  INT64 num_recs_inovf = 0;
  INT64 num_pages = 0;
  int avg_freespace = 0;
  int avg_freespace_nolast = 0;
  int avg_reclength = 0;
  int avg_overhead = 0;
  int error_code = NO_ERROR;

  item = (FILE_TRACK_ITEM *) file_extdata_at (extdata, index_item);
  if ((FILE_TYPE) item->type != FILE_HEAP && (FILE_TYPE) item->type != FILE_HEAP_REUSE_SLOTS)
    {
      return NO_ERROR;
    }

  hfid.vfid.volid = item->volid;
  hfid.vfid.fileid = item->fileid;

  error_code = heap_get_hfid_from_vfid (thread_p, &hfid.vfid, &hfid);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  error_code = heap_dump_capacity (thread_p, fp, &hfid);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  return NO_ERROR;
}

/*
 * file_tracker_dump_all_heap_capacities () - dump all heap capacities
 *
 * return        : error code
 * thread_p (in) : thread entry
 * fp (in)       : output file
 */
int
file_tracker_dump_all_heap_capacities (THREAD_ENTRY * thread_p, FILE * fp)
{
  fprintf (fp, "IO_PAGESIZE = %d, DB_PAGESIZE = %d, Recv_overhead = %d\n", IO_PAGESIZE, DB_PAGESIZE,
	   IO_PAGESIZE - DB_PAGESIZE);
  return file_tracker_map (thread_p, PGBUF_LATCH_READ, file_tracker_item_dump_heap_capacity, fp);
}

/*
 * file_tracker_item_dump_btree_capacity () - FILE_TRACK_ITEM_FUNC to dump b-tree file capacity
 *
 * return            : error code
 * thread_p (in)     : thread entry
 * page_of_item (in) : tracker page
 * extdata (in)      : tracker extensible data
 * index_item (in)   : item index
 * stop (in)         : not used
 * args (in)         : context
 */
static int
file_tracker_item_dump_btree_capacity (THREAD_ENTRY * thread_p, PAGE_PTR page_of_item, FILE_EXTENSIBLE_DATA * extdata,
				       int index_item, bool * stop, void *args)
{
  FILE_TRACK_ITEM *item;
  BTID btid;
  FILE *fp = (FILE *) args;
  int error_code = NO_ERROR;

  item = (FILE_TRACK_ITEM *) file_extdata_at (extdata, index_item);
  if ((FILE_TYPE) item->type != FILE_BTREE)
    {
      return NO_ERROR;
    }

  /* get btid */
  btid.vfid.volid = item->volid;
  btid.vfid.fileid = item->fileid;

  error_code = btree_get_btid_from_file (thread_p, &btid.vfid, &btid);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  /* dump */
  error_code = btree_dump_capacity (thread_p, fp, &btid);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  return NO_ERROR;
}

/*
 * file_tracker_dump_all_btree_capacities () - dump all b-tree capacities
 *
 * return        : error code
 * thread_p (in) : thread entry
 * fp (in)       : output file
 */
int
file_tracker_dump_all_btree_capacities (THREAD_ENTRY * thread_p, FILE * fp)
{
  return file_tracker_map (thread_p, PGBUF_LATCH_READ, file_tracker_item_dump_btree_capacity, fp);
}

/*
 * file_tracker_check () - check all files have valid tables. stand-alone mode will also cross check with disk sector
 *                         table maps.
 *
 * return        : error code
 * thread_p (in) : thread entry
 */
DISK_ISVALID
file_tracker_check (THREAD_ENTRY * thread_p)
{
  DISK_ISVALID allvalid = DISK_VALID;
  DISK_ISVALID valid = DISK_VALID;
  int error_code = NO_ERROR;

#if defined (SERVER_MODE)
  VFID vfid = VFID_INITIALIZER;
  OID class_oid = OID_INITIALIZER;

  valid = file_table_check (thread_p, &file_Tracker_vfid, NULL);
  if (valid == DISK_ERROR)
    {
      ASSERT_ERROR ();
      return DISK_ERROR;
    }
  else if (valid == DISK_INVALID)
    {
      assert_release (false);
      allvalid = DISK_INVALID;
    }

  while (true)
    {
      error_code = file_tracker_interruptable_iterate (thread_p, FILE_UNKNOWN_TYPE, &vfid, &class_oid);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  allvalid = (allvalid == DISK_VALID) ? DISK_ERROR : allvalid;
	  break;
	}
      if (VFID_ISNULL (&vfid))
	{
	  /* all files processed */
	  break;
	}
      valid = file_table_check (thread_p, &vfid, NULL);
      if (valid == DISK_INVALID)
	{
	  assert (false);
	  allvalid = DISK_INVALID;
	}
      else if (valid == DISK_ERROR)
	{
	  ASSERT_ERROR ();
	  allvalid = (allvalid == DISK_VALID) ? DISK_ERROR : allvalid;
	  break;
	}
    }

  if (!OID_ISNULL (&class_oid))
    {
      lock_unlock_object (thread_p, &class_oid, oid_Root_class_oid, SCH_S_LOCK, true);
    }
#else	/* !SERVER_MODE */		   /* SA_MODE */
  DISK_VOLMAP_CLONE *disk_map_clone = NULL;

  error_code = disk_map_clone_create (thread_p, &disk_map_clone);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return DISK_ERROR;
    }

  valid = file_table_check (thread_p, &file_Tracker_vfid, disk_map_clone);
  if (valid == DISK_INVALID)
    {
      assert_release (false);
      allvalid = DISK_INVALID;
      /* continue checks */
    }
  else if (valid == DISK_ERROR)
    {
      ASSERT_ERROR ();
      return DISK_ERROR;
    }

  error_code = file_tracker_map (thread_p, PGBUF_LATCH_READ, file_tracker_item_check, disk_map_clone);
  if (error_code == ER_FAILED)
    {
      assert_release (false);
      disk_map_clone_free (&disk_map_clone);
      allvalid = DISK_INVALID;
    }
  else if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      disk_map_clone_free (&disk_map_clone);
      return allvalid == DISK_VALID ? DISK_ERROR : allvalid;
    }

  /* check all sectors have been cleared */
  valid = disk_map_clone_check_leaks (disk_map_clone);
  if (valid == DISK_INVALID)
    {
      assert_release (false);
      allvalid = DISK_INVALID;
    }
  else if (valid == DISK_ERROR)
    {
      ASSERT_ERROR ();
      allvalid = allvalid == DISK_VALID ? DISK_ERROR : allvalid;
    }

  disk_map_clone_free (&disk_map_clone);
#endif /* SA_MODE */

  return allvalid;
}

#if defined (SA_MODE)
/*
 * file_tracker_item_check () - check file table and cross-check sector usage with disk sector table maps
 *
 * return            : error code (ER_FAILED for invalid state)
 * thread_p (in)     : thread entry
 * page_of_item (in) : tracker page
 * extdata (in)      : tracker extensible data
 * index_item (in)   : item index
 * stop (in)         : not used
 * args (in/out)     : DISK_VOLMAP_CLONE *
 */
static int
file_tracker_item_check (THREAD_ENTRY * thread_p, PAGE_PTR page_of_item, FILE_EXTENSIBLE_DATA * extdata, int index_item,
			 bool * stop, void *args)
{
  DISK_VOLMAP_CLONE *disk_map_clone = (DISK_VOLMAP_CLONE *) args;
  FILE_TRACK_ITEM *item;
  VFID vfid;
  DISK_ISVALID valid = DISK_VALID;
  int error_code = NO_ERROR;

  item = (FILE_TRACK_ITEM *) file_extdata_at (extdata, index_item);
  vfid.volid = item->volid;
  vfid.fileid = item->fileid;

  valid = file_table_check (thread_p, &vfid, disk_map_clone);
  if (valid == DISK_INVALID)
    {
      assert_release (false);
      return ER_FAILED;
    }
  else if (valid == DISK_ERROR)
    {
      ASSERT_ERROR_AND_SET (error_code);
      return error_code;
    }

  return NO_ERROR;
}
#endif /* SA_MODE */

/************************************************************************/
/* File descriptor section                                              */
/************************************************************************/

/*
 * file_descriptor_get () - get file descriptor from header
 *
 * return        : error code
 * thread_p (in) : thread entry
 * vfid (in)     : file identifier
 * desc_out (in) : output file descriptor
 */
int
file_descriptor_get (THREAD_ENTRY * thread_p, const VFID * vfid, FILE_DESCRIPTORS * desc_out)
{
  VPID vpid_fhead;
  PAGE_PTR page_fhead = NULL;
  FILE_HEADER *fhead;
  int error_code = NO_ERROR;

  FILE_GET_HEADER_VPID (vfid, &vpid_fhead);
  page_fhead = pgbuf_fix (thread_p, &vpid_fhead, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (page_fhead == NULL)
    {
      ASSERT_ERROR_AND_SET (error_code);
      return error_code;
    }
  fhead = (FILE_HEADER *) page_fhead;
  file_header_sanity_check (fhead);

  *desc_out = fhead->descriptor;

  pgbuf_unfix (thread_p, page_fhead);
  return NO_ERROR;
}

/*
 * file_descriptor_update () - Update file descriptor
 *
 * return        : error code
 * thread_p (in) : thread entry
 * vfid (in)     : file identifier
 * des_new (in)  : new file descriptor
 */
int
file_descriptor_update (THREAD_ENTRY * thread_p, const VFID * vfid, void *des_new)
{
  VPID vpid_fhead;
  PAGE_PTR page_fhead = NULL;
  FILE_HEADER *fhead = NULL;
  int error_code = NO_ERROR;

  FILE_GET_HEADER_VPID (vfid, &vpid_fhead);
  page_fhead = pgbuf_fix (thread_p, &vpid_fhead, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (page_fhead == NULL)
    {
      ASSERT_ERROR_AND_SET (error_code);
      return error_code;
    }
  fhead = (FILE_HEADER *) page_fhead;
  file_header_sanity_check (fhead);

  log_append_undoredo_data2 (thread_p, RVFL_FILEDESC_UPD, NULL, page_fhead,
			     (PGLENGTH) ((char *) &fhead->descriptor - page_fhead), sizeof (fhead->descriptor),
			     sizeof (fhead->descriptor), &fhead->descriptor, des_new);

  memcpy (&fhead->descriptor, des_new, sizeof (fhead->descriptor));

  pgbuf_set_dirty (thread_p, page_fhead, FREE);
  return NO_ERROR;
}

/*
 * file_descriptor_dump () - dump file descriptor
 *
 * return        : error code
 * thread_p (in) : thread entry
 * vfid (in)     : file identifier
 * fp (in)       : output file
 */
int
file_descriptor_dump (THREAD_ENTRY * thread_p, const VFID * vfid, FILE * fp)
{
  VPID vpid_fhead;
  PAGE_PTR page_fhead = NULL;
  FILE_HEADER *fhead = NULL;
  int error_code = NO_ERROR;

  FILE_GET_HEADER_VPID (vfid, &vpid_fhead);
  page_fhead = pgbuf_fix (thread_p, &vpid_fhead, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (page_fhead == NULL)
    {
      ASSERT_ERROR_AND_SET (error_code);
      return error_code;
    }
  fhead = (FILE_HEADER *) page_fhead;
  file_header_sanity_check (fhead);

  file_header_dump_descriptor (thread_p, fhead, fp);

  pgbuf_unfix (thread_p, page_fhead);
  return NO_ERROR;
}

/************************************************************************/
/* End of file                                                          */
/************************************************************************/
