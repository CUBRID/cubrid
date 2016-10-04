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

#include "critical_section.h"
#if defined(SERVER_MODE)
#include "connection_error.h"
#endif /* SERVER_MODE */

#if !defined (SERVER_MODE)
#include "transaction_cl.h"
#endif

#include "fault_injection.h"

#define FILE_SET_NUMVPIDS     10
#define FILE_NUM_EMPTY_SECTS  500

#define FILE_HEADER_OFFSET      0
#define FILE_FTAB_CHAIN_OFFSET  0

#define FILE_PAGE_TABLE       1
#define FILE_SECTOR_TABLE     0

#define FILE_ALLOCSET_ALLOC_NPAGES 1
#define FILE_ALLOCSET_ALLOC_ZERO   2
#define FILE_ALLOCSET_ALLOC_ERROR  3

#define NULL_PAGEID_MARKED_DELETED (NULL_PAGEID - 1)

#define FILE_TYPE_CACHE_SIZE 512
#define FILE_NEXPECTED_NEW_FILES 509	/* Prime number */
#define FILE_PREALLOC_MEMSIZE 20
#define FILE_DESTROY_NUM_MARKED 10

#define CAST_TO_SECTARRAY(asectid_ptr, pgptr, offset)\
  ((asectid_ptr) = (INT32 *)((char *)(pgptr) + (offset)))

#define CAST_TO_PAGEARRAY(apageid_ptr, pgptr, offset)\
  ((apageid_ptr) = (INT32 *)((char *)(pgptr) + (offset)))

#define FILE_SIZEOF_FHDR_WITH_DES_COMMENTS(fhdr_ptr) \
  (SSIZEOF(*fhdr_ptr) - 1 + fhdr_ptr->des.first_length)


#define NUM_HOLES_NEED_COMPACTION (DB_PAGESIZE / sizeof (PAGEID) / 2)

typedef struct file_rest_des FILE_REST_DES;
struct file_rest_des
{
  VPID next_part_vpid;		/* Location of the next part of file description comments */
  char piece[1];		/* The part */
};

/* TODO: list for file_ftab_chain */
typedef struct file_ftab_chain FILE_FTAB_CHAIN;
struct file_ftab_chain
{				/* Double link chain for file table pages */
  VPID next_ftbvpid;		/* Next table page */
  VPID prev_ftbvpid;		/* Previous table page */
};

/* The following structure is used to identify new files. */

/* TODO: list for file_newfile */
typedef struct file_newfile FILE_NEWFILE;
struct file_newfile
{
  VFID vfid;			/* Volume_file identifier */
  int tran_index;		/* Transaction of entry */
  FILE_TYPE file_type;		/* Type of new file */
  bool has_undolog;		/* Has undo log been done on this file ? */
  struct file_newfile *next;	/* Next new file entry */
  struct file_newfile *prev;	/* previous new file entry */
};

typedef struct file_allnew_files FILE_ALLNEW_FILES;
struct file_allnew_files
{
  MHT_TABLE *mht;		/* Hash table for quick access of new files */
  FILE_NEWFILE *head;		/* Head for the list of new files */
  FILE_NEWFILE *tail;		/* Tail for the list of new files */
};

/*
 * During commit/abort, file_new_destroy_all_tmp() is called before
 * file_new_declare_as_old(). Therefore, same VFID can be reused by different
 * transaction.
 * So, we must compare tran_index as well as VFID
 */
typedef struct file_new_files_hash_key FILE_NEW_FILES_HASH_KEY;
struct file_new_files_hash_key
{
  VFID vfid;
  int tran_index;
};

/* Recovery structures */

typedef struct file_recv_ftb_expansion FILE_RECV_FTB_EXPANSION;
struct file_recv_ftb_expansion
{
  int num_table_vpids;		/* Number of total pages for file table. The file header and the arrays describing the
				 * allocated pages reside in these pages */
  VPID next_table_vpid;		/* Next file table page */
  VPID last_table_vpid;		/* Last file table page */
};

typedef struct file_recv_allocset_sector FILE_RECV_ALLOCSET_SECTOR;
struct file_recv_allocset_sector
{
  INT32 num_sects;		/* Number of allocated sectors */
  INT32 curr_sectid;		/* Current sector identifier */
  VPID end_sects_vpid;		/* Ending vpid address for sector table */
  INT16 end_sects_offset;	/* Offset where the sector table ends */
};

typedef struct file_recv_expand_sector FILE_RECV_EXPAND_SECTOR;
struct file_recv_expand_sector
{
  VPID start_pages_vpid;	/* Starting vpid address for page table for this allocation set. See below for offset. */
  INT16 start_pages_offset;	/* Offset where the page table starts at the starting vpid. */
  VPID end_pages_vpid;		/* Ending vpid address for page table for this allocation set. See below for offset. */
  INT16 end_pages_offset;	/* Offset where the page table ends at ending vpid. */
  int num_holes;		/* Indicate the number of identifiers (pages or slots) that can be compacted */
};

typedef struct file_recv_allocset_link FILE_RECV_ALLOCSET_LINK;
struct file_recv_allocset_link
{
  VPID next_allocset_vpid;	/* Address of next allocation set */
  INT16 next_allocset_offset;	/* Offset of next allocation set at vpid */
};

typedef struct file_recv_allocset_pages FILE_RECV_ALLOCSET_PAGES;
struct file_recv_allocset_pages
{
  INT32 curr_sectid;		/* Current sector identifier */
  VPID end_pages_vpid;		/* Ending vpid address for page table for this allocation set. See below for offset. */
  INT16 end_pages_offset;	/* Offset where the page table ends at ending vpid. */
  int num_pages;		/* Number of pages */
};

typedef struct file_recv_shift_sector_table FILE_RECV_SHIFT_SECTOR_TABLE;
struct file_recv_shift_sector_table
{
  VPID start_sects_vpid;	/* Starting vpid address for page table for this allocation set. See below for offset. */
  INT16 start_sects_offset;	/* Offset where the page table starts at the starting vpid. */
  VPID end_sects_vpid;		/* Ending vpid address for page table for this allocation set. See below for offset. */
  INT16 end_sects_offset;	/* Offset where the page table ends at ending vpid. */
};

typedef struct file_recv_delete_pages FILE_RECV_DELETE_PAGES;
struct file_recv_delete_pages
{
  int deleted_npages;
  int need_compaction;
};

typedef struct file_tempfile_cache_entry FILE_TEMPFILE_CACHE_ENTRY;
struct file_tempfile_cache_entry
{
  int idx;
  FILE_TYPE type;
  VFID vfid;
  int next_entry;
};

typedef struct file_tempfile_cache FILE_TEMPFILE_CACHE;
struct file_tempfile_cache
{
  int free_idx;
  int first_idx;
  FILE_TEMPFILE_CACHE_ENTRY *entry;
};

/* TODO: STL::vector for file_Type_cache.entry */
typedef struct file_type_cache FILE_TYPE_CACHE;
struct file_type_cache
{
  struct
  {
    VFID vfid;
    FILE_TYPE file_type;
    int timestamp;
  } entry[FILE_TYPE_CACHE_SIZE];
  int max;
  int clock;
};

typedef struct file_mark_del_list FILE_MARK_DEL_LIST;
struct file_mark_del_list
{
  VFID vfid;
  struct file_mark_del_list *next;
};

typedef struct file_tracker_cache FILE_TRACKER_CACHE;
struct file_tracker_cache
{
  VFID *vfid;
  int hint_num_mark_deleted[FILE_LAST + 1];
  FILE_MARK_DEL_LIST *mrk_del_list[FILE_LAST + 1];
  FILE_ALLNEW_FILES newfiles;
};

static FILE_TEMPFILE_CACHE file_Tempfile_cache;
/* TODO: STL::vector for file_Type_cache.entry */
static FILE_TYPE_CACHE file_Type_cache;
static VFID file_Tracker_vfid = { NULL_FILEID, NULL_VOLID };

static FILE_TRACKER_CACHE file_Tracker_cache = {
  NULL,
  {-1,				/* FILE_TRACKER */
   -1,				/* FILE_HEAP */
   -1,				/* FILE_MULTIPAGE_OBJECT_HEAP */
   -1,				/* FILE_BTREE */
   -1,				/* FILE_BTREE_OVERFLOW_KEY */
   -1,				/* FILE_EXTENDIBLE_HASH */
   -1,				/* FILE_EXTENDIBLE_HASH_DIRECTORY */
   -1,				/* FILE_CATALOG */
   -1,				/* FILE_DROPPED_FILES */
   -1,				/* FILE_VACUUM_DATA */
   -1,				/* FILE_QUERY_AREA */
   -1,				/* FILE_TEMP */
   -1,				/* FILE_UNKNOWN_TYPE */
   0 /* FILE_HEAP_REUSE_SLOTS - do not mark, just destroy */ },
  {NULL,			/* FILE_TRACKER */
   NULL,			/* FILE_HEAP */
   NULL,			/* FILE_MULTIPAGE_OBJECT_HEAP */
   NULL,			/* FILE_BTREE */
   NULL,			/* FILE_BTREE_OVERFLOW_KEY */
   NULL,			/* FILE_EXTENDIBLE_HASH */
   NULL,			/* FILE_EXTENDIBLE_HASH_DIRECTORY */
   NULL,			/* FILE_CATALOG */
   NULL,			/* FILE_DROPPED_FILES */
   NULL,			/* FILE_VACUUM_DATA */
   NULL,			/* FILE_QUERY_AREA */
   NULL,			/* FILE_TEMP */
   NULL,			/* FILE_UNKNOWN_TYPE */
   NULL /* FILE_HEAP_REUSE_SLOTS - do not mark, just destroy */ },
  {NULL, NULL, NULL}
};

static FILE_TRACKER_CACHE *file_Tracker = &file_Tracker_cache;

static pthread_mutex_t file_Type_cache_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t file_Num_mark_deleted_hint_lock = PTHREAD_MUTEX_INITIALIZER;

#ifdef FILE_DEBUG
static int file_Debug_newallocset_multiple_of = -10;
#endif /* FILE_DEBUG */

static unsigned int file_new_files_hash_key (const void *hash_key, unsigned int htsize);
static int file_new_files_hash_cmpeq (const void *hash_key1, const void *hash_key2);

static int file_dump_all_newfiles (THREAD_ENTRY * thread_p, FILE * fp, bool tmptmp_only);
static const char *file_type_to_string (FILE_TYPE fstruct_type);
static DISK_VOLPURPOSE file_get_primary_vol_purpose (FILE_TYPE ftype);
static const VFID *file_cache_newfile (THREAD_ENTRY * thread_p, const VFID * vfid, FILE_TYPE file_type);
static INT16 file_find_goodvol (THREAD_ENTRY * thread_p, INT16 hint_volid, INT16 undesirable_volid, INT32 exp_numpages,
				DISK_SETPAGE_TYPE setpage_type, FILE_TYPE file_type);
static int file_new_declare_as_old_internal (THREAD_ENTRY * thread_p, const VFID * vfid, int tran_id, bool hold_csect);
static INT32 file_find_good_maxpages (THREAD_ENTRY * thread_p, FILE_TYPE file_type);
static int file_ftabvpid_alloc (THREAD_ENTRY * thread_p, INT16 hint_volid, INT32 hint_pageid, VPID * ftb_vpids,
				INT32 num_ftb_pages, FILE_TYPE file_type);
static int file_ftabvpid_next (const FILE_HEADER * fhdr, PAGE_PTR current_ftb_pgptr, VPID * next_ftbvpid);
static int file_find_limits (PAGE_PTR ftb_pgptr, const FILE_ALLOCSET * allocset, INT32 ** start_ptr,
			     INT32 ** outside_ptr, int what_table);
static VFID *file_xcreate (THREAD_ENTRY * thread_p, VFID * vfid, INT32 exp_numpages, FILE_TYPE * file_type,
			   const void *file_des, VPID * first_prealloc_vpid, INT32 prealloc_npages);
static int file_xdestroy (THREAD_ENTRY * thread_p, const VFID * vfid, bool pb_invalid_temp_called);
static int file_destroy_internal (THREAD_ENTRY * thread_p, const VFID * vfid, bool put_cache);
static int file_calculate_offset (INT16 start_offset, int size, int nelements, INT16 * address_offset,
				  VPID * address_vpid, VPID * ftb_vpids, int num_ftb_pages,
				  int *current_ftb_page_index);

static int file_descriptor_insert (THREAD_ENTRY * thread_p, FILE_HEADER * fhdr, const void *file_des);
static int file_descriptor_update (THREAD_ENTRY * thread_p, const VFID * vfid, const void *xfile_des);
static int file_descriptor_get (THREAD_ENTRY * thread_p, const FILE_HEADER * fhdr, void *file_des, int maxsize);
static int file_descriptor_destroy_rest_pages (THREAD_ENTRY * thread_p, FILE_HEADER * fhdr);
#if defined (ENABLE_UNUSED_FUNCTION)
static int file_descriptor_find_num_rest_pages (THREAD_ENTRY * thread_p, FILE_HEADER * fhdr);
#endif
static int file_descriptor_dump_internal (THREAD_ENTRY * thread_p, FILE * fp, const FILE_HEADER * fhdr);

static PAGE_PTR file_expand_ftab (THREAD_ENTRY * thread_p, PAGE_PTR fhdr_pgptr);

static int file_allocset_nthpage (THREAD_ENTRY * thread_p, const FILE_HEADER * fhdr, const FILE_ALLOCSET * allocset,
				  VPID * nth_vpids, INT32 start_nthpage, INT32 num_desired_pages);
static VPID *file_allocset_look_for_last_page (THREAD_ENTRY * thread_p, const FILE_HEADER * fhdr, VPID * last_vpid);
static INT32 file_allocset_alloc_sector (THREAD_ENTRY * thread_p, PAGE_PTR fhdr_pgptr, PAGE_PTR allocset_pgptr,
					 INT16 allocset_offset, int exp_npages);
static PAGE_PTR file_allocset_expand_sector (THREAD_ENTRY * thread_p, PAGE_PTR fhdr_pgptr, PAGE_PTR allocset_pgptr,
					     INT16 allocset_offset);

static int file_allocset_new_set (THREAD_ENTRY * thread_p, INT16 last_allocset_volid, PAGE_PTR fhdr_pgptr,
				  int exp_numpages, DISK_SETPAGE_TYPE setpage_type);
static int file_allocset_add_set (THREAD_ENTRY * thread_p, PAGE_PTR fhdr_pgptr, INT16 volid);
static INT32 file_allocset_add_pageids (THREAD_ENTRY * thread_p, PAGE_PTR fhdr_pgptr, PAGE_PTR allocset_pgptr,
					INT16 allocset_offset, INT32 alloc_pageid, INT32 npages,
					FILE_ALLOC_VPIDS * alloc_vpids);
static int file_allocset_alloc_pages (THREAD_ENTRY * thread_p, PAGE_PTR fhdr_pgptr, VPID * allocset_vpid,
				      INT16 allocset_offset, VPID * first_new_vpid, INT32 npages,
				      const VPID * near_vpid, FILE_ALLOC_VPIDS * alloc_vpids);
static VPID *file_alloc_pages_internal (THREAD_ENTRY * thread_p, const VFID * vfid, VPID * first_alloc_vpid,
					INT32 npages, const VPID * near_vpid, FILE_TYPE * p_file_type, int with_sys_op,
					bool (*fun) (THREAD_ENTRY * thread_p, const VFID * vfid,
						     const FILE_TYPE file_type, const VPID * first_alloc_vpid,
						     INT32 npages, void *args), void *args);
static DISK_ISVALID file_allocset_find_page (THREAD_ENTRY * thread_p, PAGE_PTR fhdr_pgptr, PAGE_PTR allocset_pgptr,
					     INT16 allocset_offset, INT32 pageid, int (*fun) (THREAD_ENTRY * thread_p,
											      FILE_HEADER * fhdr,
											      FILE_ALLOCSET * allocset,
											      PAGE_PTR fhdr_pgptr,
											      PAGE_PTR allocset_pgptr,
											      INT16 allocset_offset,
											      PAGE_PTR pgptr,
											      INT32 * aid_ptr,
											      INT32 pageid, void *args),
					     void *args);
static int file_allocset_dealloc_page (THREAD_ENTRY * thread_p, FILE_HEADER * fhdr, FILE_ALLOCSET * allocset,
				       PAGE_PTR fhdr_pgptr, PAGE_PTR allocset_pgptr, INT16 allocset_offset,
				       PAGE_PTR pgptr, INT32 * aid_ptr, INT32 ignore_pageid, void *remove);
static int file_allocset_dealloc_contiguous_pages (THREAD_ENTRY * thread_p, FILE_HEADER * fhdr,
						   FILE_ALLOCSET * allocset, PAGE_PTR fhdr_pgptr,
						   PAGE_PTR allocset_pgptr, INT16 allocset_offset, PAGE_PTR pgptr,
						   INT32 * first_aid_ptr, INT32 ncont_page_entries);
static int file_allocset_remove_pageid (THREAD_ENTRY * thread_p, FILE_HEADER * fhdr, FILE_ALLOCSET * allocset,
					PAGE_PTR fhdr_pgptr, PAGE_PTR allocset_pgptr, INT16 allocset_offset,
					PAGE_PTR pgptr, INT32 * aid_ptr, INT32 ignore_pageid, void *ignore);
static int file_allocset_remove_contiguous_pages (THREAD_ENTRY * thread_p, FILE_HEADER * fhdr, FILE_ALLOCSET * allocset,
						  PAGE_PTR fhdr_pgptr, PAGE_PTR allocset_pgptr, INT16 allocset_offset,
						  PAGE_PTR pgptr, INT32 * aid_ptr, INT32 num_contpages);
static int file_allocset_remove (THREAD_ENTRY * thread_p, PAGE_PTR fhdr_pgptr, VPID * prev_allocset_vpid,
				 INT16 prev_allocset_offset, VPID * allocset_vpid, INT16 allocset_offset);
static int file_allocset_reuse_last_set (THREAD_ENTRY * thread_p, PAGE_PTR fhdr_pgptr, INT16 new_volid);
static int file_allocset_compact_page_table (THREAD_ENTRY * thread_p, PAGE_PTR fhdr_pgptr, PAGE_PTR allocset_pgptr,
					     INT16 allocset_offset, bool rm_freespace_sectors);
static int file_allocset_shift_sector_table (THREAD_ENTRY * thread_p, PAGE_PTR fhdr_pgptr, PAGE_PTR allocset_pgptr,
					     INT16 allocset_offset, VPID * ftb_vpid, INT16 ftb_offset);
static int file_allocset_compact (THREAD_ENTRY * thread_p, PAGE_PTR fhdr_pgptr, VPID * prev_allocset_vpid,
				  INT16 prev_allocset_offset, VPID * allocset_vpid, INT16 * allocset_offset,
				  VPID * ftb_vpid, INT16 * ftb_offset);
static int file_allocset_find_num_deleted (THREAD_ENTRY * thread_p, FILE_HEADER * fhdr, FILE_ALLOCSET * allocset,
					   int *num_deleted, int *num_marked_deleted);
static int file_allocset_dump (FILE * fp, const FILE_ALLOCSET * allocset, bool doprint_title);
static int file_allocset_dump_tables (THREAD_ENTRY * thread_p, FILE * fp, const FILE_HEADER * fhdr,
				      const FILE_ALLOCSET * allocset);

static int file_compress (THREAD_ENTRY * thread_p, const VFID * vfid, bool do_partial_compaction);
static int file_dump_fhdr (THREAD_ENTRY * thread_p, FILE * fp, const FILE_HEADER * fhdr);
static int file_dump_ftabs (THREAD_ENTRY * thread_p, FILE * fp, const FILE_HEADER * fhdr);

static const VFID *file_tracker_register (THREAD_ENTRY * thread_p, const VFID * vfid);
static const VFID *file_tracker_unregister (THREAD_ENTRY * thread_p, const VFID * vfid);

static int file_mark_deleted_file_list_add (VFID * vfid, const FILE_TYPE file_type);
static int file_mark_deleted_file_list_remove (VFID * vfid, const FILE_TYPE file_type);

static DISK_ISVALID file_check_deleted (THREAD_ENTRY * thread_p, const VFID * vfid);

static FILE_TYPE file_type_cache_check (const VFID * vfid);
static int file_type_cache_add_entry (const VFID * vfid, FILE_TYPE type);
static int file_type_cache_entry_remove (const VFID * vfid);
static FILE_TYPE file_get_type_internal (THREAD_ENTRY * thread_p, const VFID * vfid, PAGE_PTR fhdr_pgptr);

static int file_tmpfile_cache_initialize (void);
static int file_tmpfile_cache_finalize (void);
static VFID *file_tmpfile_cache_get (THREAD_ENTRY * thread_p, VFID * vfid, FILE_TYPE file_type);
static int file_tmpfile_cache_put (THREAD_ENTRY * thread_p, const VFID * vfid, FILE_TYPE file_type);

static VFID *file_create_tmp_internal (THREAD_ENTRY * thread_p, VFID * vfid, FILE_TYPE * file_type, INT32 exp_numpages,
				       const void *file_des);
static DISK_ISVALID file_check_all_pages (THREAD_ENTRY * thread_p, const VFID * vfid, bool validate_vfid);

static int file_rv_tracker_unregister_logical_undo (THREAD_ENTRY * thread_p, VFID * vfid);
static int file_rv_fhdr_last_allocset_helper (THREAD_ENTRY * thread_p, LOG_RCV * rcv, int delta);

static void file_descriptor_dump_heap (THREAD_ENTRY * thread_p, FILE * fp, const FILE_HEAP_DES * heap_file_des_p);
static void file_descriptor_dump_multi_page_object_heap (FILE * fp, const FILE_OVF_HEAP_DES * ovf_hfile_des_p);
static void file_descriptor_dump_btree (THREAD_ENTRY * thread_p, FILE * fp, const FILE_BTREE_DES * btree_des_p,
					const VFID * vfid);
static void file_descriptor_dump_btree_overflow_key (FILE * fp, const FILE_OVF_BTREE_DES * btree_ovf_des_p);
static void file_descriptor_dump_extendible_hash (THREAD_ENTRY * thread_p, FILE * fp,
						  const FILE_EHASH_DES * ext_hash_des_p);
static void file_print_name_of_class (THREAD_ENTRY * thread_p, FILE * fp, const OID * class_oid_p);
static void file_print_class_name_of_instance (THREAD_ENTRY * thread_p, FILE * fp, const OID * inst_oid_p);
static void file_print_name_of_class_with_attrid (THREAD_ENTRY * thread_p, FILE * fp, const OID * class_oid_p,
						  const int attr_id);
static void file_print_class_name_index_name_with_attrid (THREAD_ENTRY * thread_p, FILE * fp, const OID * class_oid_p,
							  const VFID * vfid, const int attr_id);

static int file_descriptor_get_length (const FILE_TYPE file_type);
static void file_descriptor_dump (THREAD_ENTRY * thread_p, FILE * fp, const FILE_TYPE file_type,
				  const void *file_descriptor_p, const VFID * vfid);
static DISK_ISVALID file_make_idsmap_image (THREAD_ENTRY * thread_p, const VFID * vfid, char **vol_ids_map,
					    int last_vol);
static DISK_ISVALID set_bitmap (char *vol_ids_map, INT32 pageid);
static DISK_ISVALID file_verify_idsmap_image (THREAD_ENTRY * thread_p, INT16 volid, char *vol_ids_map);
static DISK_PAGE_TYPE file_get_disk_page_type (FILE_TYPE ftype);
static DISK_ISVALID file_construct_space_info (THREAD_ENTRY * thread_p, VOL_SPACE_INFO * space_info, const VFID * vfid);


/* check not use */
//#if 0
///*
// * file_vfid_hash () - Hash a volume page identifier
// *   return:  hash value
// *   key_vfid(in): VPID to hash
// *   htsize(in): Size of hash table
// */
//unsigned int
//file_vfid_hash (const void *key_vfid, unsigned int htsize)
//{
//  const VFID *vfid = key_vfid;
//
//  return ((vfid->fileid | ((unsigned int) vfid->volid) << 24) % htsize);
//}
//
///*
// * file_vfid_hash_cmpeq () - Compare two vpids keys for hashing
// *   return:
// *   key_vfid1(in): First key
// *   key_vfid2(in): Second key
// */
//int
//file_vfid_hash_cmpeq (const void *key_vfid1, const void *key_vfid2)
//{
//  const VFID *vfid1 = key_vfid1;
//  const VFID *vfid2 = key_vfid2;
//
//  return VFID_EQ (vfid1, vfid2);
//}
//#endif

/*
 * file_new_files_hash_key () -
 *   return:
 *   hash_key(in):
 *   htsize(in):
 */
static unsigned int
file_new_files_hash_key (const void *hash_key, unsigned int htsize)
{
  FILE_NEW_FILES_HASH_KEY *key;

  key = (FILE_NEW_FILES_HASH_KEY *) hash_key;

  return (((key->vfid.fileid | ((unsigned int) key->vfid.volid) << 24) + key->tran_index) % htsize);
}

/*
 * file_new_files_hash_cmpeq () -
 *   return:
 *   hash_key1(in):
 *   hash_key2(in):
 */
static int
file_new_files_hash_cmpeq (const void *hash_key1, const void *hash_key2)
{
  FILE_NEW_FILES_HASH_KEY *key1, *key2;

  key1 = (FILE_NEW_FILES_HASH_KEY *) hash_key1;
  key2 = (FILE_NEW_FILES_HASH_KEY *) hash_key2;

  return (key1->tran_index == key2->tran_index && VFID_EQ (&key1->vfid, &key2->vfid));
}

/*
 * file_manager_initialize () - Initialize the file module
 *   return:
 */
int
file_manager_initialize (THREAD_ENTRY * thread_p)
{
  int ret = NO_ERROR;

  /* todo: fix me */
  if (true)
    {
      return NO_ERROR;
    }

  if (file_Tracker->newfiles.mht != NULL || file_Tracker->newfiles.head != NULL)
    {
      (void) file_manager_finalize (thread_p);
    }

#ifdef FILE_DEBUG
  {
    const char *env_value;
    int value;

    env_value = envvar_get ("FL_SHIFT_VOLUMES_AT_MULTIPLE_OF");
    if (env_value != NULL)
      {
	value = atoi (env_value);
	if (value <= 0)
	  {
	    file_Debug_newallocset_multiple_of = 0;
	  }
	else
	  {
	    file_Debug_newallocset_multiple_of = -value;
	  }
      }

    if ((int) DB_PAGESIZE < sizeof (FILE_HEADER))
      {
	er_log_debug (ARG_FILE_LINE,
		      "file_manager_initialize: **SYSTEM_ERROR AT COMPILE TIME"
		      " ** DB_PAGESIZE must be > %d. Current value is set to %d", sizeof (FILE_HEADER), DB_PAGESIZE);
	exit (EXIT_FAILURE);
      }
  }
#endif /* FILE_DEBUG */


  if (csect_enter (thread_p, CSECT_FILE_NEWFILE, INF_WAIT) != NO_ERROR)
    {
      return ER_FAILED;
    }

  ret = file_typecache_clear ();
  if (ret != NO_ERROR)
    {
      csect_exit (thread_p, CSECT_FILE_NEWFILE);

      return ret;
    }

  file_Tracker->newfiles.head = NULL;
  file_Tracker->newfiles.tail = NULL;
  file_Tracker->newfiles.mht =
    mht_create ("Newfiles hash table", FILE_NEXPECTED_NEW_FILES, file_new_files_hash_key, file_new_files_hash_cmpeq);
  if (file_Tracker->newfiles.mht == NULL)
    {
      ret = ER_FAILED;
      csect_exit (thread_p, CSECT_FILE_NEWFILE);

      return ret;
    }

  ret = file_tmpfile_cache_initialize ();

  csect_exit (thread_p, CSECT_FILE_NEWFILE);

  return ret;
}

/*
 * file_manager_finalize () - Terminates the file module
 *   return: NO_ERROR
 */
int
file_manager_finalize (THREAD_ENTRY * thread_p)
{
  FILE_NEWFILE *entry, *tmp;
  int ret = NO_ERROR;

  if (csect_enter (thread_p, CSECT_FILE_NEWFILE, INF_WAIT) != NO_ERROR)
    {
      return ER_FAILED;
    }

  /* Destroy hash table */
  if (file_Tracker->newfiles.mht != NULL)
    {
      mht_destroy (file_Tracker->newfiles.mht);
      file_Tracker->newfiles.mht = NULL;
    }

  /* Deallocate all transient entries of new files */
  entry = file_Tracker->newfiles.head;
  while (entry != NULL)
    {
      tmp = entry;
      entry = entry->next;
      free_and_init (tmp);
    }
  file_Tracker->newfiles.head = NULL;
  file_Tracker->newfiles.tail = NULL;

  ret = file_typecache_clear ();

  file_tmpfile_cache_finalize ();

  csect_exit (thread_p, CSECT_FILE_NEWFILE);

  return ret;
}

/*
 * file_cache_newfile () - Cache/remember a newly created file
 *   return: vfid on success and NULL on failure
 *   vfid(in): Complete file identifier (i.e., Volume_id + file_id)
 *   file_type(in): Temporary or permanent file
 */
static const VFID *
file_cache_newfile (THREAD_ENTRY * thread_p, const VFID * vfid, FILE_TYPE file_type)
{
  FILE_NEWFILE *entry;
  FILE_NEW_FILES_HASH_KEY key;
  int tran_index;
  LOG_TDES *tdes;

  /* todo: remove me */
  assert (false);

  /* If the entry already exists (page reused), then remove it from the list and allocate a new entry. */
  VFID_COPY (&key.vfid, vfid);
  key.tran_index = tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);

  if (csect_enter (thread_p, CSECT_FILE_NEWFILE, INF_WAIT) != NO_ERROR)
    {
      return NULL;
    }

  entry = (FILE_NEWFILE *) mht_get (file_Tracker->newfiles.mht, &key);
  if (entry != NULL)
    {
      if (file_new_declare_as_old_internal (thread_p, vfid, entry->tran_index, true) != NO_ERROR)
	{
	  csect_exit (thread_p, CSECT_FILE_NEWFILE);
	  return NULL;
	}
    }

  entry = (FILE_NEWFILE *) malloc (sizeof (*entry));
  if (entry == NULL)
    {
      csect_exit (thread_p, CSECT_FILE_NEWFILE);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (*entry));
      return NULL;
    }

  VFID_COPY (&entry->vfid, vfid);
  entry->tran_index = tran_index;
  entry->file_type = file_type;
  entry->has_undolog = false;

  /* Add the entry to the list and hash table */
  entry->next = NULL;
  entry->prev = file_Tracker->newfiles.tail;

  /* Last entry should point to this one and new tail should be defined */
  if (file_Tracker->newfiles.tail != NULL)
    {
      file_Tracker->newfiles.tail->next = entry;
    }
  else
    {
      /* There is not any entries. Define the head at this point. */
      file_Tracker->newfiles.head = entry;
    }

  /* New tail */
  file_Tracker->newfiles.tail = entry;

  (void) mht_put (file_Tracker->newfiles.mht, entry, entry);

  csect_exit (thread_p, CSECT_FILE_NEWFILE);

  return vfid;
}

/*
 * file_new_declare_as_old_internal () - Decache a file
 *   return:
 *   vfid(in): Complete file identifier (i.e., Volume_id + file_id) or NULL
 *   tran_id(in): Transaction used for finding the correct entry
 *
 * Note: The file associated with the given vfid is declared as an old file.
 *       If vfid is equal to NULL, all new files of the transaction are
 *       declared as old files
 */
static int
file_new_declare_as_old_internal (THREAD_ENTRY * thread_p, const VFID * vfid, int tran_id, bool hold_csect)
{
  FILE_NEWFILE *entry, *tmp;
  FILE_NEW_FILES_HASH_KEY key;
  int success = ER_FAILED;
  LOG_TDES *tdes = NULL;

  /* todo: remove me */
  assert (false);

  if (hold_csect == false && csect_enter (thread_p, CSECT_FILE_NEWFILE, INF_WAIT) != NO_ERROR)
    {
      return ER_FAILED;
    }

  if (vfid == NULL)
    {
      /* Search the list for all new files of current transaction */
      entry = file_Tracker->newfiles.head;
      success = NO_ERROR;	/* They may not be any entries */
    }
  else
    {
      /* We want to remove only one entry. Use the hash table to locate this entry */
      VFID_COPY (&key.vfid, vfid);
      key.tran_index = tran_id;
      entry = (FILE_NEWFILE *) mht_get (file_Tracker->newfiles.mht, &key);
    }

  tdes = LOG_FIND_TDES (tran_id);

  while (entry != NULL)
    {
      if (entry->tran_index == tran_id && (vfid == NULL || VFID_EQ (&entry->vfid, vfid)))
	{
	  success = NO_ERROR;

	  /* Remove the entry from the list */
	  if (entry->prev != NULL)
	    {
	      entry->prev->next = entry->next;
	    }
	  else
	    {
	      file_Tracker->newfiles.head = entry->next;
	    }

	  if (entry->next != NULL)
	    {
	      entry->next->prev = entry->prev;
	    }
	  else
	    {
	      file_Tracker->newfiles.tail = entry->prev;
	    }

	  /* Next entry */
	  tmp = entry;
	  entry = entry->next;

	  /* mht_rem() has been updated to take a function and an arg pointer that can be called on the entry before it 
	   * is removed.  We may want to take advantage of that here to free the memory associated with the entry */
	  (void) mht_rem (file_Tracker->newfiles.mht, tmp, NULL, NULL);

	  free_and_init (tmp);

	  if (vfid != NULL)
	    {
	      /* Only one entry */
	      break;
	    }
	}
      else
	{
	  /* Next entry */
	  entry = entry->next;
	}
    }

  if (hold_csect == false)
    {
      csect_exit (thread_p, CSECT_FILE_NEWFILE);
    }

  return success;
}

/*
 * file_new_declare_as_old () - Decache a file
 *   return:
 *   vfid(in): Complete file identifier (i.e., Volume_id + file_id) or NULL
 *
 * Note: The file associated with the given vfid is declared as an old file.
 *       If vfid is equal to NULL, all new files of the transaction are
 *       declared as old files.
 */
int
file_new_declare_as_old (THREAD_ENTRY * thread_p, const VFID * vfid)
{
  int tran_index;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);

  return file_new_declare_as_old_internal (thread_p, vfid, tran_index, false);
}

/*
 * file_is_new_file () - Find out if the file is a newly created file
 *   return: FILE_NEW_FILE if new file, FILE_OLD_FILE if not a new file,
 *           or FILE_ERROR
 *   vfid(in): Complete file identifier (i.e., Volume_id + file_id)
 *
 * Note: A newly created file is one that has been created by an active
 *       transaction.
 *
 *       We are assuming that files are not created too frequently. If this
 *       assumption is not correct, it is better to define a hash table to
 *       find out whether or not a file is a new one.
 */
FILE_IS_NEW_FILE
file_is_new_file (THREAD_ENTRY * thread_p, const VFID * vfid)
{
  FILE_TYPE dummy_ftype;
  bool dummy_has_undolog;

  return file_is_new_file_ext (thread_p, vfid, &dummy_ftype, &dummy_has_undolog);
}

/*
 * file_is_new_file_ext () - Find out if the file is a newly created file
 *   return: file_type is set to the files type or error
 *   vfid(in): Complete file identifier (i.e., Volume_id + file_id)
 *   file_type(out): return the file's type
 *   has_undolog(out):
 *
 * Note: A newly created file is one that has been created by an active
 *       transaction. As a side-effect, also return an existing file's type
 *       and has_undolog flag.
 */
FILE_IS_NEW_FILE
file_is_new_file_ext (THREAD_ENTRY * thread_p, const VFID * vfid, FILE_TYPE * file_type, bool * has_undolog)
{
  FILE_NEWFILE *entry;
  FILE_NEW_FILES_HASH_KEY key;
  FILE_IS_NEW_FILE newfile = FILE_OLD_FILE;

  /* todo: remove me */
  assert (false);

  *file_type = FILE_UNKNOWN_TYPE;
  *has_undolog = false;

  VFID_COPY (&key.vfid, vfid);
  key.tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  if (csect_enter_as_reader (thread_p, CSECT_FILE_NEWFILE, INF_WAIT) != NO_ERROR)
    {
      return FILE_ERROR;
    }

  entry = (FILE_NEWFILE *) mht_get (file_Tracker->newfiles.mht, &key);
  if (entry != NULL)
    {
      newfile = FILE_NEW_FILE;
      *file_type = entry->file_type;
      *has_undolog = entry->has_undolog;
    }

  csect_exit (thread_p, CSECT_FILE_NEWFILE);

  return newfile;
}

/*
 * file_new_set_has_undolog () -Undo log has been done on new file
 *   return: NO_ERROR
 *   vfid(in): Complete file identifier (i.e., Volume_id + file_id)
 *
 * Note: We declare that undo logging has been performed to this file.
 *       From now on, undo logging should not be skipped any more.
 */
int
file_new_set_has_undolog (THREAD_ENTRY * thread_p, const VFID * vfid)
{
  FILE_NEWFILE *entry;
  FILE_NEW_FILES_HASH_KEY key;
  int ret = NO_ERROR;

  /* todo: remove me */
  assert (false);

  VFID_COPY (&key.vfid, vfid);
  key.tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);

  if (csect_enter (thread_p, CSECT_FILE_NEWFILE, INF_WAIT) != NO_ERROR)
    {
      return ER_FAILED;
    }

  entry = (FILE_NEWFILE *) mht_get (file_Tracker->newfiles.mht, &key);
  if (entry == NULL)
    {
      ret = ER_FAILED;
    }
  else
    {
      entry->has_undolog = true;
    }

  csect_exit (thread_p, CSECT_FILE_NEWFILE);

  return ret;
}

/*
 * file_new_destroy_all_tmp () - Delete all temporary new files
 *   return: void
 *   tmp_type(in):
 *
 * Note: All temporary new files of the current transaction are deleted from
 *       the system.
 */
int
file_new_destroy_all_tmp (THREAD_ENTRY * thread_p, FILE_TYPE tmp_type)
{
  FILE_NEWFILE *entry, *next_entry;
  FILE_NEWFILE *p, *delete_list;
  int tran_index;
  int ret = NO_ERROR;

  delete_list = NULL;

  /* todo: fix me */
  assert (false);

  if (csect_enter_as_reader (thread_p, CSECT_FILE_NEWFILE, INF_WAIT) != NO_ERROR)
    {
      return ER_FAILED;
    }

  /* Search the list */
  next_entry = file_Tracker->newfiles.head;
  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);

  while (next_entry != NULL)
    {
      entry = next_entry;
      /* Get next entry before current entry is invalidated */
      next_entry = entry->next;

      if (entry->tran_index == tran_index && tmp_type == entry->file_type)
	{
	  assert (entry->file_type == FILE_TEMP);

	  p = (FILE_NEWFILE *) malloc (sizeof (*entry));
	  if (p == NULL)
	    {
	      ret = ER_OUT_OF_VIRTUAL_MEMORY;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ret, 1, sizeof (*entry));
	    }
	  else
	    {
	      p->vfid.fileid = entry->vfid.fileid;
	      p->vfid.volid = entry->vfid.volid;

	      p->next = delete_list;
	      delete_list = p;
	    }
	}
    }

  csect_exit (thread_p, CSECT_FILE_NEWFILE);

  entry = delete_list;
  while (entry != NULL)
    {
      delete_list = delete_list->next;
      (void) file_destroy (thread_p, &entry->vfid);
      free_and_init (entry);
      entry = delete_list;
    }

  return ret;
}

/*
 * file_preserve_temporary () - remove temporary file from the control of the
 *				file manager
 * return : error code or NO_ERROR
 * thread_p (in)  : current thread
 * vfid (in)	  : file identifier
 */
int
file_preserve_temporary (THREAD_ENTRY * thread_p, const VFID * vfid)
{
  FILE_NEW_FILES_HASH_KEY key;
  FILE_NEWFILE *entry = NULL;
  int tran_index = NULL_TRAN_INDEX;
  LOG_TDES *tdes = NULL;

  /* todo: fix me */
  assert (false);

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);
  if (tdes == NULL)
    {
      assert (false);
      return ER_FAILED;
    }

  key.tran_index = tran_index;
  VFID_COPY (&key.vfid, vfid);

  if (csect_enter (thread_p, CSECT_FILE_NEWFILE, INF_WAIT) != NO_ERROR)
    {
      return ER_FAILED;
    }

  entry = (FILE_NEWFILE *) mht_get (file_Tracker->newfiles.mht, &key);
  if (entry == NULL)
    {
      csect_exit (thread_p, CSECT_FILE_NEWFILE);
      return NO_ERROR;
    }

  /* remove it from the table */
  mht_rem (file_Tracker->newfiles.mht, &key, NULL, NULL);

  /* remove it from the linked list */
  if (entry->prev != NULL)
    {
      entry->prev->next = entry->next;
    }
  else
    {
      file_Tracker->newfiles.head = entry->next;
    }

  if (entry->next != NULL)
    {
      entry->next->prev = entry->prev;
    }
  else
    {
      file_Tracker->newfiles.tail = entry->prev;
    }

  /* we don't need this entry anymore */
  free_and_init (entry);

  csect_exit (thread_p, CSECT_FILE_NEWFILE);
  return NO_ERROR;
}

/*
 * file_dump_all_newfiles () - Dump all new files
 *   return: NO_ERROR
 *   tmptmp_only(in): Dump only temporary files on temporary volumes ?
 *
 * Note: if we want to dump only temporary files on volumes with temporary
 *       purpose, it can be indicated by given argument.
 */
static int
file_dump_all_newfiles (THREAD_ENTRY * thread_p, FILE * fp, bool tmptmp_only)
{
  FILE_NEWFILE *entry;
  int ret = NO_ERROR;

  /* todo: fix me */
  assert (false);

  if (csect_enter_as_reader (thread_p, CSECT_FILE_NEWFILE, INF_WAIT) != NO_ERROR)
    {
      return ER_FAILED;
    }

  if (file_Tracker->newfiles.head != NULL)
    {
      (void) fprintf (fp, "DUMPING new files..\n");
    }

  /* Deallocate all transient entries of new files */
  for (entry = file_Tracker->newfiles.head; entry != NULL; entry = entry->next)
    {
      (void) fprintf (fp,
		      "New File = %d|%d, Type = %s, undolog = %d,\n"
		      " Created by Tran_index = %d, next = %p, prev = %p\n", entry->vfid.fileid, entry->vfid.volid,
		      file_type_to_string (entry->file_type), entry->has_undolog, entry->tran_index, entry->next,
		      entry->prev);
      if (tmptmp_only == false || entry->file_type == FILE_TEMP)
	{
	  ret = file_dump (thread_p, fp, &entry->vfid);
	  if (ret != NO_ERROR)
	    {
	      break;
	    }
	}
    }

  csect_exit (thread_p, CSECT_FILE_NEWFILE);

  return ret;
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

/*
 * file_get_primary_vol_purpose () - Get vol_purpose of the given file type
 *   return:
 *   ftype(in):
 */
static DISK_VOLPURPOSE
file_get_primary_vol_purpose (FILE_TYPE ftype)
{
  DISK_VOLPURPOSE purpose;

  assert (false);

  switch (ftype)
    {
    case FILE_TRACKER:
    case FILE_HEAP:
    case FILE_HEAP_REUSE_SLOTS:
    case FILE_MULTIPAGE_OBJECT_HEAP:
    case FILE_CATALOG:
    case FILE_EXTENDIBLE_HASH:
    case FILE_EXTENDIBLE_HASH_DIRECTORY:
    case FILE_DROPPED_FILES:
    case FILE_VACUUM_DATA:
    case FILE_BTREE:
    case FILE_BTREE_OVERFLOW_KEY:
      purpose = DB_PERMANENT_DATA_PURPOSE;
      break;

    case FILE_TEMP:
    case FILE_QUERY_AREA:
      /* purpose = DISK_EITHER_TEMP_PURPOSE; TODO: Remove me */
      purpose = DB_TEMPORARY_DATA_PURPOSE;
      break;

    case FILE_UNKNOWN_TYPE:
    default:
      purpose = DISK_UNKNOWN_PURPOSE;
      break;
    }

  return purpose;
}


/*
 * file_get_disk_page_type ()
 *   return:
 *   ftype(in):
 */
static DISK_PAGE_TYPE
file_get_disk_page_type (FILE_TYPE ftype)
{
  DISK_PAGE_TYPE page_type;

  /* todo */
  return DISK_PAGE_UNKNOWN_TYPE;

  return page_type;
}

/*
 * file_find_goodvol () - Find a volume with at least the expected number of free
 *                 pages
 *   return: volid or NULL_VOLID
 *   hint_volid(in): Use this volume identifier as a hint
 *   undesirable_volid(in):
 *   exp_numpages(in): Expected number of pages
 *   setpage_type(in): Type of the set of needed pages
 *   file_type(in): Type of the file
 */
static INT16
file_find_goodvol (THREAD_ENTRY * thread_p, INT16 hint_volid, INT16 undesirable_volid, INT32 exp_numpages,
		   DISK_SETPAGE_TYPE setpage_type, FILE_TYPE file_type)
{
#if 0
  DISK_VOLPURPOSE vol_purpose;

  if (exp_numpages <= 0)
    {
      exp_numpages = 1;
    }

  vol_purpose = file_get_primary_vol_purpose (file_type);
  if (vol_purpose == DISK_UNKNOWN_PURPOSE)
    {
      assert (false);
      return hint_volid;
    }

  return disk_find_goodvol (thread_p, hint_volid, undesirable_volid, exp_numpages, setpage_type, vol_purpose);
#endif /* 0 */
  return NULL_VOLID;
}

/*
 * file_find_good_maxpages () - Find the maximum number of pages that can be
 *                          allocated for a file of given type
 *   return: number of pages
 *   file_type(in): Type of the file
 *
 * Note: These should be takes as a hint due to other concurrent allocations.
 */
static INT32
file_find_good_maxpages (THREAD_ENTRY * thread_p, FILE_TYPE file_type)
{
  return 0;
}

/*
 * file_find_limits () - Find the limits of the portion of sector/page table
 *                     described by current file table page
 *   return: NO_ERROR
 *   ftb_pgptr(in): Current page that contain portion of either the sector or
 *                  page table
 *   allocset(in): The allocation set which describe the table
 *   start_ptr(out): Start pointer address of portion of table
 *   outside_ptr(out): pointer to the outside of portion table
 *   what_table(in):
 */
static int
file_find_limits (PAGE_PTR ftb_pgptr, const FILE_ALLOCSET * allocset, INT32 ** start_ptr, INT32 ** outside_ptr,
		  int what_table)
{
  VPID *vpid;
  int ret = NO_ERROR;

  vpid = pgbuf_get_vpid_ptr (ftb_pgptr);

  if (what_table == FILE_PAGE_TABLE)
    {
      if (VPID_EQ (vpid, &allocset->start_pages_vpid))
	{
	  CAST_TO_PAGEARRAY (*start_ptr, ftb_pgptr, allocset->start_pages_offset);
	}
      else
	{
	  CAST_TO_PAGEARRAY (*start_ptr, ftb_pgptr, sizeof (FILE_FTAB_CHAIN));
	}

      if (VPID_EQ (vpid, &allocset->end_pages_vpid))
	{
	  CAST_TO_PAGEARRAY (*outside_ptr, ftb_pgptr, allocset->end_pages_offset);
	}
      else
	{
	  CAST_TO_PAGEARRAY (*outside_ptr, ftb_pgptr, DB_PAGESIZE);
	}
    }
  else
    {
      if (VPID_EQ (vpid, &allocset->start_sects_vpid))
	{
	  CAST_TO_SECTARRAY (*start_ptr, ftb_pgptr, allocset->start_sects_offset);
	}
      else
	{
	  CAST_TO_SECTARRAY (*start_ptr, ftb_pgptr, sizeof (FILE_FTAB_CHAIN));
	}

      if (VPID_EQ (vpid, &allocset->end_sects_vpid))
	{
	  CAST_TO_SECTARRAY (*outside_ptr, ftb_pgptr, allocset->end_sects_offset);
	}
      else
	{
	  CAST_TO_SECTARRAY (*outside_ptr, ftb_pgptr, DB_PAGESIZE);
	}
    }

  return ret;
}

/*
 * file_ftabvpid_alloc () - Allocate a set of file table pages
 *   return:
 *   hint_volid(in): Use this volume identifier as a hint for the allocation
 *   hint_pageid(in): Use this page in the hinted volume for allocation as
 *                    close as this page
 *   ftb_vpids(in): An array of num_ftb_pages VPID elements
 *   num_ftb_pages(in): Number of table pages to allocate
 *   file_type(in): File type
 */
static int
file_ftabvpid_alloc (THREAD_ENTRY * thread_p, INT16 hint_volid, INT32 hint_pageid, VPID * ftb_vpids,
		     INT32 num_ftb_pages, FILE_TYPE file_type)
{
  INT32 pageid;
  int i;
  VOLID original_hint_volid;
  INT32 original_hint_pageid;
  bool old_check_interrupt;


  original_hint_volid = hint_volid;
  original_hint_pageid = hint_pageid;

  do
    {
      if (hint_volid != NULL_VOLID)
	{
	  DISK_PAGE_TYPE alloc_page_type = file_get_disk_page_type (file_type);

	  old_check_interrupt = thread_set_check_interrupt (thread_p, false);
	  pageid =
	    disk_alloc_page (thread_p, hint_volid, DISK_SECTOR_WITH_ALL_PAGES, num_ftb_pages, hint_pageid,
			     alloc_page_type);

	  if (pageid != NULL_PAGEID && pageid != DISK_NULL_PAGEID_WITH_ENOUGH_DISK_PAGES)
	    {
	      for (i = 0; i < num_ftb_pages; i++)
		{
		  ftb_vpids->volid = hint_volid;
		  ftb_vpids->pageid = pageid + i;
		  ftb_vpids++;
		}

	      (void) thread_set_check_interrupt (thread_p, old_check_interrupt);

	      return NO_ERROR;
	    }

	  (void) thread_set_check_interrupt (thread_p, old_check_interrupt);
	}

      hint_volid =
	file_find_goodvol (thread_p, NULL_VOLID, hint_volid, num_ftb_pages, DISK_CONTIGUOUS_PAGES, file_type);

      if (original_hint_volid == hint_volid)
	{
	  /* temporary volume can be expanded */
	  hint_pageid = original_hint_pageid;
	}
      else
	{
	  hint_pageid = NULL_PAGEID;
	}
    }
  while (hint_volid != NULL_VOLID);

  if (hint_volid == NULL_VOLID && er_errid () != ER_INTERRUPTED)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FILE_NOT_ENOUGH_PAGES_IN_DATABASE, 1, num_ftb_pages);
    }

  return ER_FAILED;
}

/* TODO: list for file_ftab_chain */
/*
 * file_ftabvpid_next () - Find the next page of file table located after the
 *                      given file table page
 *   return: NO_ERROR, next_ftbvpid which has been set as a side effect
 *   fhdr(in): Pointer to file header
 *   current_ftb_pgptr(in): Pointer to current page of file table
 *   next_ftbvpid(out): VPID identifier of next page of file table
 */
static int
file_ftabvpid_next (const FILE_HEADER * fhdr, PAGE_PTR current_ftb_pgptr, VPID * next_ftbvpid)
{
  VPID *vpid;			/* The vpid of current page of file table */
  FILE_FTAB_CHAIN *chain;	/* Structure for linking file table pages */
  int ret = NO_ERROR;

  vpid = pgbuf_get_vpid_ptr (current_ftb_pgptr);
  if (vpid->volid == fhdr->vfid.volid && vpid->pageid == fhdr->vfid.fileid)
    {
      *next_ftbvpid = fhdr->next_table_vpid;
    }
  else
    {
      chain = (FILE_FTAB_CHAIN *) (current_ftb_pgptr + FILE_FTAB_CHAIN_OFFSET);
      *next_ftbvpid = chain->next_ftbvpid;
    }

  if (VPID_EQ (vpid, next_ftbvpid))
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_FILE_FTB_LOOP, 4, fhdr->vfid.fileid,
	      fileio_get_volume_label (fhdr->vfid.volid, PEEK), vpid->volid, vpid->pageid);
      VPID_SET_NULL (next_ftbvpid);
    }

  return ret;
}

/*
 * file_descriptor_insert () - Insert the descriptor comments for this file
 *   return:
 *   fhdr(out): File header
 *   xfile_des(in): The comments to be included in the file descriptor
 *
 * Note: The descriptor comments are inserted in the file header page. If they
 *       do not fit in the file header page, the comments are split in pieces.
 *       One piece is inserted in the file header and the rest in specific
 *       comments pages.
 */
static int
file_descriptor_insert (THREAD_ENTRY * thread_p, FILE_HEADER * fhdr, const void *xfile_des)
{
  const char *file_des = (char *) xfile_des;
  int copy_length;
  VPID one_vpid;
  VPID *set_vpids = NULL;
#if defined (ENABLE_UNUSED_FUNCTION)
  int rest_length;
  INT32 npages;
  int ipage;
  FILE_REST_DES *rest;
#endif
  LOG_DATA_ADDR addr;
  int ret = NO_ERROR;

  addr.vfid = &fhdr->vfid;

  if (file_des != NULL)
    {
      fhdr->des.total_length = file_descriptor_get_length (fhdr->type);
    }
  else
    {
      fhdr->des.total_length = 0;
    }

  /* How big can the first chunk of comments can be ? */
  copy_length = DB_PAGESIZE - sizeof (*fhdr) + 1;

  assert (copy_length > fhdr->des.total_length);

  /* Do we need to split the file descriptor comments into several pieces ? */
  if (copy_length >= fhdr->des.total_length || file_des == NULL)
    {
      /* One piece */
      fhdr->des.first_length = fhdr->des.total_length;
      if (file_des != NULL)
	{
	  memcpy (fhdr->des.piece, file_des, fhdr->des.total_length);
	}
      VPID_SET_NULL (&fhdr->des.next_part_vpid);
      /* NOTE: The logging of the first piece is done at the creation function */
    }
#if defined (ENABLE_UNUSED_FUNCTION)
  else
    {
      /* Several pieces */

      /* Store the first piece */
      fhdr->des.first_length = copy_length;
      memcpy (fhdr->des.piece, file_des, copy_length);
      file_des += copy_length;
      rest_length = fhdr->des.total_length - copy_length;

      /* Note: The logging of the first piece is done at the creation function */

      /* 
       * Then, store the rest of the pieces.
       * 1) Find size of each piece
       * 2) Get number of pages to store the pieces
       * 3) Allocate the set of pages
       * 4) Copy the description
       */

      copy_length = DB_PAGESIZE - sizeof (*rest) + 1;
      npages = CEIL_PTVDIV (rest_length, copy_length);
      if (npages == 1)
	{
	  set_vpids = &one_vpid;
	}
      else
	{
	  set_vpids = (VPID *) malloc (sizeof (*set_vpids) * npages);
	  if (set_vpids == NULL)
	    {
	      ret = ER_OUT_OF_VIRTUAL_MEMORY;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ret, 1, sizeof (*set_vpids) * npages);
	      goto exit_on_error;
	    }
	}
      ret = file_ftabvpid_alloc (thread_p, fhdr->vfid.volid, (INT32) fhdr->vfid.fileid, set_vpids, npages, fhdr->type);
      if (ret != NO_ERROR)
	{
	  goto exit_on_error;
	}

      /* Link to first rest page */
      fhdr->des.next_part_vpid = set_vpids[0];

      ipage = -1;
      while (rest_length > 0)
	{
	  ipage++;
#ifdef FILE_DEBUG
	  if (npages < (ipage + 1))
	    {
	      er_log_debug (ARG_FILE_LINE,
			    "file_descriptor_insert: ** SYSTEM ERROR calculation of number of pages needed"
			    " to store comments seems incorrect. Need more than %d pages", npages);
	      goto exit_on_error;
	    }
#endif /* FILE_DEBUG */

	  addr.pgptr = pgbuf_fix (thread_p, &set_vpids[ipage], NEW_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
	  if (addr.pgptr == NULL)
	    {
	      goto exit_on_error;
	    }

	  (void) pgbuf_set_page_ptype (thread_p, addr.pgptr, PAGE_FTAB);

	  addr.offset = 0;

	  rest = (FILE_REST_DES *) addr.pgptr;

	  /* Copy as much as you can */

	  if (rest_length < copy_length)
	    {
	      copy_length = rest_length;
	    }

	  /* 
	   * This undo log is not needed unless we are creating the file in a
	   * second/third nested system operation. It is up to the log manager
	   * to log or not.
	   */

	  log_append_undo_data (thread_p, RVFL_FILEDESC_INS, &addr, sizeof (*rest) - 1 + copy_length,
				(char *) addr.pgptr);

	  if ((ipage + 1) == npages)
	    {
	      VPID_SET_NULL (&rest->next_part_vpid);
	    }
	  else
	    {
	      rest->next_part_vpid = set_vpids[ipage + 1];
	    }

	  memcpy (rest->piece, file_des, copy_length);
	  file_des += copy_length;
	  rest_length -= copy_length;

	  log_append_redo_data (thread_p, RVFL_FILEDESC_INS, &addr, sizeof (*rest) - 1 + copy_length,
				(char *) addr.pgptr);
	  pgbuf_set_dirty (thread_p, addr.pgptr, FREE);
	  addr.pgptr = NULL;
	}
    }

end:
#endif

  if (set_vpids != &one_vpid)
    {
      free_and_init (set_vpids);
    }

  return ret;

#if defined (ENABLE_UNUSED_FUNCTION)
exit_on_error:

  fhdr->des.total_length = -1;
  fhdr->des.first_length = -1;
  VPID_SET_NULL (&fhdr->des.next_part_vpid);

  if (ret == NO_ERROR)
    {
      ret = ER_FAILED;
    }
  goto end;
#endif
}

/*
 * file_descriptor_update () - Update the descriptor comments for this file
 *   return:
 *   vfid(out): File header
 *   xfile_des(in): The comments to be included in the file descriptor
 *
 * Note: The desciptor comments are inserted in the file header page. If they
 *       do not fit in the file header page, the comments are split in pieces.
 *       One piece is inserted in the file header and the rest in specific
 *       comments pages.
 */
static int
file_descriptor_update (THREAD_ENTRY * thread_p, const VFID * vfid, const void *xfile_des)
{
  const char *file_des = (char *) xfile_des;
  int old_length = 0;
  int rest_length;
  int copy_length = 0;
  VPID next_vpid;
  VPID tmp_vpid;
  FILE_REST_DES *rest;
  FILE_HEADER *fhdr = NULL;
  FILE_TYPE file_type = FILE_UNKNOWN_TYPE;
  LOG_DATA_ADDR addr;
  bool isnewpage = false;

  addr.vfid = vfid;

  next_vpid.volid = vfid->volid;
  next_vpid.pageid = vfid->fileid;
  rest = NULL;
  rest_length = 1;

  while (rest_length > 0)
    {
      addr.pgptr = pgbuf_fix (thread_p, &next_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
      if (addr.pgptr == NULL)
	{
	  return ER_FAILED;
	}

      (void) pgbuf_check_page_ptype (thread_p, addr.pgptr, PAGE_FTAB);

      /* Log before and after images */

      /* Is this the first page ? */
      assert (fhdr == NULL);

      if (fhdr == NULL)
	{
	  /* This is the first part. */
	  fhdr = (FILE_HEADER *) (addr.pgptr + FILE_HEADER_OFFSET);

	  file_type = fhdr->type;
	  if (file_des != NULL)
	    {
	      rest_length = file_descriptor_get_length (fhdr->type);
	    }
	  else
	    {
	      rest_length = 0;
	    }

	  /* How big can the first chunk of comments can be ? */
	  copy_length = DB_PAGESIZE - sizeof (*fhdr) + 1;
	  if (copy_length > rest_length)
	    {
	      /* One piece */
	      copy_length = rest_length;
	    }

	  old_length = fhdr->des.total_length;

	  /* Log before image */
	  addr.offset = offsetof (FILE_HEADER, des);

	  log_append_undo_data (thread_p, RVFL_FILEDESC_UPD, &addr, sizeof (fhdr->des) - 1 + fhdr->des.first_length,
				&fhdr->des);

	  next_vpid = fhdr->des.next_part_vpid;

	  old_length -= fhdr->des.first_length;

	  /* Modify the new lengths */
	  fhdr->des.total_length = rest_length;
	  fhdr->des.first_length = copy_length;

	  if (file_des != NULL && copy_length > 0)
	    {
	      memcpy (fhdr->des.piece, file_des, copy_length);
	    }

	  log_append_redo_data (thread_p, RVFL_FILEDESC_UPD, &addr, sizeof (fhdr->des) - 1 + fhdr->des.first_length,
				&fhdr->des);
	}
#if defined (ENABLE_UNUSED_FUNCTION)
      else
	{
	  /* Rest of pages */
	  addr.offset = 0;

	  rest = (FILE_REST_DES *) addr.pgptr;

	  if (isnewpage == true)
	    {
	      VPID_SET_NULL (&next_vpid);
	    }
	  else
	    {
	      next_vpid = rest->next_part_vpid;
	    }


	  copy_length = DB_PAGESIZE - sizeof (*rest) + 1;

	  if (copy_length > rest_length)
	    {
	      copy_length = rest_length;
	    }

	  /* Log before image */
	  if (old_length > 0)
	    {
	      if ((old_length + SSIZEOF (*rest) - 1) > DB_PAGESIZE)
		{
		  log_append_undo_data (thread_p, RVFL_FILEDESC_UPD, &addr, DB_PAGESIZE, (char *) addr.pgptr);
		  old_length -= DB_PAGESIZE + sizeof (*rest) - 1;
		}
	      else
		{
		  log_append_undo_data (thread_p, RVFL_FILEDESC_UPD, &addr, sizeof (*rest) - 1 + old_length,
					(char *) addr.pgptr);
		  old_length = 0;
		}
	    }

	  if (file_des != NULL && copy_length > 0)
	    {
	      memcpy (rest->piece, file_des, copy_length);
	    }
	  log_append_redo_data (thread_p, RVFL_FILEDESC_UPD, &addr, sizeof (*rest) - 1 + copy_length,
				(char *) addr.pgptr);
	}
#endif

      if (file_des != NULL)
	{
	  file_des += copy_length;
	}

      rest_length -= copy_length;
      assert (rest_length == 0);

#if defined (ENABLE_UNUSED_FUNCTION)
      if (rest_length > 0)
	{
	  /* Need more pages... Get next page */
	  if (VPID_ISNULL (&next_vpid))
	    {
	      /* We need to allocate a new page */
	      if (file_ftabvpid_alloc (thread_p, pgbuf_get_volume_id (addr.pgptr), pgbuf_get_page_id (addr.pgptr),
				       &next_vpid, 1, file_type) != NO_ERROR)
		{
		  /* Something went wrong */
		  pgbuf_set_dirty (thread_p, addr.pgptr, FREE);
		  addr.pgptr = NULL;
		  return ER_FAILED;
		}

	      VPID_SET_NULL (&tmp_vpid);
	      isnewpage = true;	/* So that its link can be set to NULL */

	      if (rest == NULL)
		{
		  /* This is the first part */
		  log_append_undoredo_data (thread_p, RVFL_DES_FIRSTREST_NEXTVPID, &addr, sizeof (next_vpid),
					    sizeof (next_vpid), &tmp_vpid, &next_vpid);
		  fhdr->des.next_part_vpid = next_vpid;
		}
	      else
		{
		  /* This is part of rest part */
		  log_append_undoredo_data (thread_p, RVFL_DES_NREST_NEXTVPID, &addr, sizeof (next_vpid),
					    sizeof (next_vpid), &tmp_vpid, &next_vpid);
		  rest->next_part_vpid = next_vpid;
		}
	    }
	  pgbuf_set_dirty (thread_p, addr.pgptr, FREE);
	  addr.pgptr = NULL;
	}
      else
#endif
	{
	  /* The content of the object has been copied. We don't need more pages for the descriptor. Deallocate any
	   * additional pages */

	  addr.offset = 0;
	  VPID_SET_NULL (&tmp_vpid);
	  if (rest == NULL)
	    {
	      /* This is the first part */
	      log_append_undoredo_data (thread_p, RVFL_DES_FIRSTREST_NEXTVPID, &addr, sizeof (next_vpid),
					sizeof (next_vpid), &next_vpid, &tmp_vpid);
	      VPID_SET_NULL (&fhdr->des.next_part_vpid);
	    }
	  else
	    {
	      /* This is part of rest part */
	      log_append_undoredo_data (thread_p, RVFL_DES_NREST_NEXTVPID, &addr, sizeof (next_vpid),
					sizeof (next_vpid), &next_vpid, &tmp_vpid);
	      VPID_SET_NULL (&rest->next_part_vpid);
	    }
	  pgbuf_set_dirty (thread_p, addr.pgptr, FREE);
	  addr.pgptr = NULL;

	  while (!(VPID_ISNULL (&next_vpid)))
	    {
	      addr.pgptr = pgbuf_fix (thread_p, &next_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
	      if (addr.pgptr == NULL)
		{
		  return ER_FAILED;
		}

	      (void) pgbuf_check_page_ptype (thread_p, addr.pgptr, PAGE_FTAB);

	      tmp_vpid = next_vpid;
	      rest = (FILE_REST_DES *) addr.pgptr;
	      next_vpid = rest->next_part_vpid;
	      if (pgbuf_invalidate (thread_p, addr.pgptr) != NO_ERROR)
		{
		  return ER_FAILED;
		}
	      addr.pgptr = NULL;
	      (void) file_dealloc_page (thread_p, vfid, &tmp_vpid, file_type);
	    }
	}
    }

  return NO_ERROR;
}

/*
 * file_descriptor_get () - Get descriptor comments of file
 *   return: length
 *   fhdr(in): File header
 *   xfile_des(out): The comments included in the file descriptor
 *   maxsize(in):
 *
 * Note: Fetch the descriptor comments for this file into file_des The length
 *       of the file descriptor is returned. If the desc_commnets area is not
 *       large enough, the comments are not retrieved, an a negative value is
 *       returned. The absolute value of the return value can be used to get
 *       a bigger* area and call again.
 */
static int
file_descriptor_get (THREAD_ENTRY * thread_p, const FILE_HEADER * fhdr, void *xfile_des, int maxsize)
{
  char *file_des = (char *) xfile_des;
#if defined (ENABLE_UNUSED_FUNCTION)
  VPID vpid;
  PAGE_PTR pgptr = NULL;
  int rest_length, copy_length;
  FILE_REST_DES *rest;
#endif

  if (maxsize < fhdr->des.total_length || fhdr->des.total_length <= 0)
    {
      /* Does not fit */
      MEM_REGION_INIT (file_des, maxsize);
      return (-fhdr->des.total_length);
    }

  /* First part */
  if (maxsize < fhdr->des.first_length)
    {
      /* This is an error.. we have already check for the total length */
      er_log_debug (ARG_FILE_LINE,
		    "file_descriptor_get: **SYSTEM_ERROR: First length = %d > total length = %d of file descriptor"
		    " for file VFID = %d|%d\n", fhdr->des.first_length, fhdr->des.total_length, fhdr->vfid.fileid,
		    fhdr->vfid.volid);
      file_des[0] = '\0';
      return 0;
    }

  memcpy (file_des, fhdr->des.piece, fhdr->des.first_length);
  file_des += fhdr->des.first_length;

  if (VPID_ISNULL (&fhdr->des.next_part_vpid))
    {
      return fhdr->des.total_length;
    }

  assert (false);

#if defined (ENABLE_UNUSED_FUNCTION)
  /* The rest if any ... */
  rest_length = fhdr->des.total_length - fhdr->des.first_length;
  copy_length = DB_PAGESIZE - sizeof (*rest) + 1;
  vpid = fhdr->des.next_part_vpid;

  while (rest_length > 0 && !VPID_ISNULL (&vpid))
    {
      pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
      if (pgptr == NULL)
	{
	  file_des[0] = '\0';
	  return 0;
	}

      (void) pgbuf_check_page_ptype (thread_p, pgptr, PAGE_FTAB);

      rest = (FILE_REST_DES *) pgptr;
      /* Copy as much as you can */

      if (rest_length < copy_length)
	{
	  copy_length = rest_length;
	}

      memcpy (file_des, rest->piece, copy_length);
      file_des += copy_length;
      rest_length -= copy_length;
      vpid = rest->next_part_vpid;
      pgbuf_unfix_and_init (thread_p, pgptr);
    }
#endif

  return fhdr->des.total_length;
}

/*
 * file_descriptor_destroy_rest_pages () - Destory rest of pages of file descriptor
 *   return:
 *   fhdr(in): File header
 *
 * Note: If the descriptor comments are stored in several pages. The rest of
 *       the pages are removed.
 */
static int
file_descriptor_destroy_rest_pages (THREAD_ENTRY * thread_p, FILE_HEADER * fhdr)
{
#if defined (ENABLE_UNUSED_FUNCTION)
  VPID rest_vpid;
  PAGE_PTR pgptr = NULL;
  FILE_REST_DES *rest;
  VPID batch_vpid;
  int batch_ndealloc;
#endif

  if (VPID_ISNULL (&fhdr->des.next_part_vpid))
    {
      return NO_ERROR;		/* nop */
    }

  assert (false);

#if defined (ENABLE_UNUSED_FUNCTION)
  /* Any rest pages ? */

  VPID_SET_NULL (&batch_vpid);
  rest_vpid = fhdr->des.next_part_vpid;
  /* Deallocate the pages is set of contiguous pages */
  batch_ndealloc = 0;

  while (!VPID_ISNULL (&rest_vpid))
    {
      pgptr = pgbuf_fix (thread_p, &rest_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
      if (pgptr == NULL)
	{
	  return ER_FAILED;
	}

      (void) pgbuf_check_page_ptype (thread_p, pgptr, PAGE_FTAB);

      rest = (FILE_REST_DES *) pgptr;
      if (batch_ndealloc == 0)
	{
	  /* Start accumulating contiguous pages */
	  batch_vpid = rest->next_part_vpid;
	  batch_ndealloc = 1;
	}
      else
	{
	  /* Is page contiguous ? */
	  if (rest_vpid.volid == batch_vpid.volid && rest_vpid.pageid == (batch_vpid.pageid + batch_ndealloc))
	    {
	      /* contiguous */
	      batch_ndealloc++;
	    }
	  else
	    {
	      /* 
	       * This is not a contiguous page.
	       * Deallocate any previous pages and start accumulating
	       * contiguous pages again.
	       * We do not care if the page deallocation failed,
	       * since we are destroying the file.. Deallocate as much
	       * as we can.
	       */
	      (void) disk_dealloc_page (thread_p, batch_vpid.volid, batch_vpid.pageid, batch_ndealloc);
	      /* Start again */
	      batch_vpid = rest->next_part_vpid;
	      batch_ndealloc = 1;
	    }
	}
      rest_vpid = rest->next_part_vpid;
      pgbuf_unfix_and_init (thread_p, pgptr);
    }

  if (batch_ndealloc > 0)
    {
      (void) disk_dealloc_page (thread_p, batch_vpid.volid, batch_vpid.pageid, batch_ndealloc);
    }
#endif

  return NO_ERROR;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * file_descriptor_find_num_rest_pages () - Find th enumber of rest pages needed to store
 *                            the current descriptor of file
 *   return: number of rest pages or -1 in case of error
 *   fhdr(in): File header
 */
static int
file_descriptor_find_num_rest_pages (THREAD_ENTRY * thread_p, FILE_HEADER * fhdr)
{
  VPID rest_vpid;
  PAGE_PTR pgptr = NULL;
  FILE_REST_DES *rest;
  int num_rest_pages = 0;

  /* Any rest pages ? */
  if (!VPID_ISNULL (&fhdr->des.next_part_vpid))
    {
      rest_vpid = fhdr->des.next_part_vpid;

      while (!VPID_ISNULL (&rest_vpid))
	{
	  pgptr = pgbuf_fix (thread_p, &rest_vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
	  if (pgptr == NULL)
	    {
	      return -1;
	    }

	  (void) pgbuf_check_page_ptype (thread_p, pgptr, PAGE_FTAB);

	  num_rest_pages++;
	  rest = (FILE_REST_DES *) pgptr;
	  rest_vpid = rest->next_part_vpid;
	  pgbuf_unfix_and_init (thread_p, pgptr);
	}
    }

  return num_rest_pages;
}
#endif

/*
 * file_descriptor_dump_internal () - Dump the descriptor comments for this file
 *   return: void
 *   fhdr(out): File header
 */
static int
file_descriptor_dump_internal (THREAD_ENTRY * thread_p, FILE * fp, const FILE_HEADER * fhdr)
{
#if defined (ENABLE_UNUSED_FUNCTION)
  int rest_length, dump_length;
  const char *dumpfrom;
  char *file_des;
  int i;
  FILE_REST_DES *rest;
  VPID vpid;
#endif
  PAGE_PTR pgptr = NULL;
  int ret = NO_ERROR;

  if (fhdr->des.total_length <= 0)
    {
      fprintf (fp, "\n");
      return NO_ERROR;
    }

  assert (fhdr->des.total_length == fhdr->des.first_length);

  if (fhdr->des.total_length == fhdr->des.first_length)
    {
      file_descriptor_dump (thread_p, fp, fhdr->type, fhdr->des.piece, &fhdr->vfid);

    }
#if defined (ENABLE_UNUSED_FUNCTION)
  else
    {
      file_des = (char *) malloc (fhdr->des.total_length);
      if (file_des == NULL)
	{
	  ret = ER_OUT_OF_VIRTUAL_MEMORY;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ret, 1, fhdr->des.total_length);
	  goto exit_on_error;
	}

      if (file_descriptor_get (thread_p, fhdr, file_des, fhdr->des.total_length) <= 0)
	{
	  free_and_init (file_des);
	  goto exit_on_error;
	}

      switch (fhdr->type)
	{
	case FILE_HEAP:
	case FILE_HEAP_REUSE_SLOTS:
	case FILE_MULTIPAGE_OBJECT_HEAP:
	case FILE_TRACKER:
	case FILE_BTREE:
	case FILE_BTREE_OVERFLOW_KEY:
	case FILE_EXTENDIBLE_HASH:
	case FILE_EXTENDIBLE_HASH_DIRECTORY:
	  break;

	case FILE_CATALOG:
	case FILE_DROPPED_FILES:
	case FILE_VACUUM_DATA:
	case FILE_QUERY_AREA:
	case FILE_TEMP:
	case FILE_UNKNOWN_TYPE:
	  /* This does not really exist */
	default:
	  /* Dump the first part */
	  for (i = 0, dumpfrom = fhdr->des.piece; i < fhdr->des.first_length; i++)
	    {
	      (void) fputc (*dumpfrom++, fp);
	    }

	  /* The rest if any */
	  if (!VPID_ISNULL (&fhdr->des.next_part_vpid))
	    {
	      rest_length = fhdr->des.total_length - fhdr->des.first_length;
	      dump_length = DB_PAGESIZE - sizeof (*rest) + 1;
	      vpid = fhdr->des.next_part_vpid;
	      while (rest_length > 0 && !VPID_ISNULL (&vpid))
		{
		  pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
		  if (pgptr == NULL)
		    {
		      free_and_init (file_des);
		      goto exit_on_error;
		    }

		  (void) pgbuf_check_page_ptype (thread_p, pgptr, PAGE_FTAB);

		  rest = (FILE_REST_DES *) pgptr;
		  /* Dump as much as you can */

		  if (rest_length < dump_length)
		    {
		      dump_length = rest_length;
		    }

		  for (i = 0, dumpfrom = rest->piece; i < dump_length; i++)
		    {
		      (void) fputc (*dumpfrom++, fp);
		    }
		  vpid = rest->next_part_vpid;
		  pgbuf_unfix_and_init (thread_p, pgptr);
		}
	    }
	  fprintf (fp, "\n");
	  break;
	}
      free_and_init (file_des);
    }
#endif

  return ret;

#if defined (ENABLE_UNUSED_FUNCTION)
exit_on_error:

  if (pgptr)
    {
      pgbuf_unfix_and_init (thread_p, pgptr);
    }

  if (ret == NO_ERROR)
    {
      ret = ER_FAILED;
    }

  return ret;
#endif
}

/*
 * file_calculate_offset () - Calculate the location of either
 *                                   the start/end of either the page or
 *                                   sector table during the creation of the file
 *   return:
 *   start_offset(in): start position
 *   size(in): size per element to increase
 *   nelements(in): the number of elements to increase
 *   address_offset(out): Calculated offset
 *   address_vpid(out): The page where we our outside in our calculation
 *   ftb_vpids(in): Array of file table pages
 *   num_ftb_pages(in): the number of elements in ftb_vpids
 *   current_ftb_page_index(out): Index into the ftb_vpids of current
 *                                address_vpid
 *
 * Note: Used only during file_create
 */
static int
file_calculate_offset (INT16 start_offset, int size, int nelements, INT16 * address_offset, VPID * address_vpid,
		       VPID * ftb_vpids, int num_ftb_pages, int *current_ftb_page_index)
{
  int ret = NO_ERROR;
  int offset;
  int idx;

  offset = start_offset + (size * nelements);

  if (offset < DB_PAGESIZE)
    {
      /* the calculated offset is in the page, no need to fix it up. */
      *address_offset = (INT16) offset;
      return ret;
    }

  /* 
   * Fix the location of either the start/end of either the page or sector
   * table. Remove first page from the offset. Then, calculate the offset
   * taking in consideration chain pointers among pages of file table
   */

  offset -= DB_PAGESIZE;

  /* If the page is not in the first table, we must increase the chain */
  if (!VPID_EQ (address_vpid, &ftb_vpids[0]))
    {
      offset += sizeof (FILE_FTAB_CHAIN);
    }

  /* Recalculate how many pages ahead */

  idx = CEIL_PTVDIV (1 + offset, (DB_PAGESIZE - sizeof (FILE_FTAB_CHAIN)));
  idx += *current_ftb_page_index;

  if (num_ftb_pages <= idx)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FILE_TABLE_OVERFLOW, 5, num_ftb_pages, (idx + 1), ftb_vpids[0].volid,
	      ftb_vpids[0].pageid, fileio_get_volume_label (ftb_vpids[0].volid, PEEK));

      return ER_FILE_TABLE_OVERFLOW;
    }

  *current_ftb_page_index = idx;

  *address_vpid = ftb_vpids[*current_ftb_page_index];
  offset %= DB_PAGESIZE - sizeof (FILE_FTAB_CHAIN);
  offset += sizeof (FILE_FTAB_CHAIN);

  /* now, offset points to the last page or sector table */
  *address_offset = (INT16) offset;

  return ret;
}

/*
 * file_create_check_not_dropped () - Create a file that is not 
 *
 * return		     : NULL if file creation failed, VFID of file if
 *			       creation was successful.
 * thread_p (in)	     : Thread entry.
 * vfid (out)		     : Output VFID of created file.
 * exp_numpages (in)	     : Expected number of pages.
 * file_type (in)	     : File type.
 * file_des (in)	     : Descriptor for file.
 * first_prealloc_vpid (out) : Output VPID of first page in file.
 * prealloc_npages (in)	     : Number of pages to allocate.
 *
 * This function is required for concurrent create/drop files (heap and index)
 * done by very long transactions. Vacuum tracks dropped files and marks them
 * with current mvcc_next_id in order to avoid accessing them any further.
 * However, a long transaction may reuse VFID and its MVCCID may be less than
 * the MVCCID used to mark the dropped file.
 * This scenario is not really likely to happen in production. However, to
 * suppress it in our tests, we try to create several files until one is not
 * considered dropped by vacuum.
 */
VFID *
file_create_check_not_dropped (THREAD_ENTRY * thread_p, VFID * vfid, INT32 exp_numpages, FILE_TYPE file_type,
			       const void *file_des, VPID * first_prealloc_vpid, INT32 prealloc_npages)
{
#if defined (SERVER_MODE)
#define FILE_NOT_DROPPED_CREATE_RETRIES 256
  VFID created_files[FILE_NOT_DROPPED_CREATE_RETRIES];
  int n_created_files = 0;
  int i;
  MVCCID tran_mvccid = logtb_get_current_mvccid (thread_p);
  VFID *return_vfid = NULL;

  assert (file_type == FILE_BTREE || file_type == FILE_HEAP || file_type == FILE_HEAP_REUSE_SLOTS);

  while (n_created_files < FILE_NOT_DROPPED_CREATE_RETRIES)
    {
      if (file_create (thread_p, &created_files[n_created_files], exp_numpages, file_type, file_des,
		       first_prealloc_vpid, prealloc_npages) == NULL)
	{
	  /* Failed to create a new file. */
	  break;
	}
      if (!vacuum_is_file_dropped (thread_p, &created_files[n_created_files], tran_mvccid))
	{
	  /* Successfully created not dropped file. */
	  VFID_COPY (vfid, &created_files[n_created_files]);
	  return_vfid = vfid;
	  break;
	}
      /* Created a file but paired with current transaction MVCCID it will be considered dropped by vacuum. Retry. */
      n_created_files++;
    }
  /* Destroy all previously created files. */
  for (i = 0; i < n_created_files; i++)
    {
      file_destroy (thread_p, &created_files[i]);
      /* remove the file from new file cache. Since we are in a top operation, file_destroy didn't change the file as
       * OLD_FILE. */
      file_new_declare_as_old (thread_p, &created_files[i]);
    }

  /* Return result: NULL if create files, VFID of file if successful. */
  if (n_created_files == FILE_NOT_DROPPED_CREATE_RETRIES)
    {
      /* Very unlikely to happen, but let's set an error. */
      /* If this assert is ever hit, it is really ridiculous. We tried 100 times. Find a better way of handling the
       * case (do not deallocated file header page until it is removed from vacuum's list of dropped files - just an
       * idea). */
      assert (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
    }
  return return_vfid;
#else	/* !SERVER_MODE */		   /* SA_MODE */
  return file_create (thread_p, vfid, exp_numpages, file_type, file_des, first_prealloc_vpid, prealloc_npages);
#endif /* SA_MODE */
}

/*
 * file_create () - Create an unstructured file in a volume
 *   return: vfid or NULL in case of error
 *   vfid(out): Complete file identifier (i.e., Volume_id + file_id)
 *   exp_numpages(in): Expected number of pages
 *   file_type(in): Type of file
 *   file_des(in): Add the follwing file description to the file
 *   first_prealloc_vpid(out): An array of VPIDs for preallocated pages or NULL
 *   prealloc_npages(in): Number of pages to allocate
 *
 * Note: Create an unstructured file in a volume that hopefully has at least
 *       exp_numpages free pages. The volume indicated in vfid->volid is used
 *       as a hint for the allocation of the file. A set of sectors is
 *       allocated to improve locality of the file. The sectors allocated are
 *       estimated from the number of expected pages. The maximum number of
 *       allocated sectors is 25%* of the total number of sectors in disk.
 *       Note the number of expected pages are not allocated at this point,
 *       they are allocated as needs arise.
 *
 *       The expected number of pages can be estimated in some cases, for
 *       example, when a B+tree is created, the minimal number of pages needed
 *       can be estimated from the loading file. When the number of expected
 *       pages cannot be estimated a zero or negative value can be passed.
 *       Neither in this case nor when the expected number of pages is smaller
 *       than the number of pages within a sector, are sectors allocated.
 */
VFID *
file_create (THREAD_ENTRY * thread_p, VFID * vfid, INT32 exp_numpages, FILE_TYPE file_type, const void *file_des,
	     VPID * first_prealloc_vpid, INT32 prealloc_npages)
{
  FILE_TYPE newfile_type = file_type;

  return file_xcreate (thread_p, vfid, exp_numpages, &newfile_type, file_des, first_prealloc_vpid, prealloc_npages);
}

/*
 * file_create_tmp_internal () -
 *   return:
 *   vfid(in):
 *   file_type(in):
 *   exp_numpages(in):
 *   file_des(in):
 */
static VFID *
file_create_tmp_internal (THREAD_ENTRY * thread_p, VFID * vfid, FILE_TYPE * file_type, INT32 exp_numpages,
			  const void *file_des)
{
  LOG_DATA_ADDR addr;		/* address of logging data */
  bool old_val, rv;

  /* Start a TOP SYSTEM OPERATION. This top system operation will be either ABORTED (case of failure) or COMMITTED, so
   * that the new file becomes kind of permanent. */
  if (log_start_system_op (thread_p) == NULL)
    {
      VFID_SET_NULL (vfid);
      return NULL;
    }

  old_val = thread_set_check_interrupt (thread_p, false);

  if (file_xcreate (thread_p, vfid, exp_numpages, file_type, file_des, NULL, 0) != NULL)
    {
      log_end_system_op (thread_p, LOG_RESULT_TOPOP_COMMIT);

      assert (file_get_numpages (thread_p, vfid) == 0);

      if (*file_type != FILE_TEMP && *file_type != FILE_QUERY_AREA)
	{
	  addr.vfid = NULL;
	  addr.pgptr = NULL;
	  addr.offset = 0;
	  log_append_undo_data (thread_p, RVFL_CREATE_TMPFILE, &addr, sizeof (*vfid), vfid);
	  /* Force the log to reduce the possibility of unreclaiming a temporary file in the case of system crash. See
	   * above */
	  LOG_CS_ENTER (thread_p);
	  logpb_flush_pages_direct (thread_p);
	  LOG_CS_EXIT (thread_p);
	}
    }
  else
    {
      /* Something went wrong.. Abort the system operation */
      (void) log_end_system_op (thread_p, LOG_RESULT_TOPOP_ABORT);
      vfid = NULL;
    }

  rv = thread_set_check_interrupt (thread_p, old_val);

  return vfid;
}

/*
 * file_create_tmp () - Create a new unstructured temporary file
 *   return: vfid or NULL in case of error
 *   vfid(out): complete file identifier (i.e., volume_id + file_id)
 *   exp_numpages(in): expected number of pages to be allocated
 *   file_des(in):
 *
 * Note: Create an unstructured temporary file in a volume that hopefully has
 *       at least exp_numpages free pages. The volume indicated in vfid->volid
 *       is used as a hint for the allocation of the file. A set of sectors is
 *       allocated to improve locality of the file. The sectors allocated are
 *       estimated from the number of expected pages. The maximum number of
 *       allocated sectors is 25% of the total number of sectors in disk.
 *       Note the number of expected pages are not allocated at this point,
 *       they are allocated as needs arise. The file is destroyed when the file
 *       is deleted by the transaction that created the file or when this
 *       transaction is finished (either committed or aborted). That is, the
 *       life of a temporary file is not longer that the life of the
 *       transaction that creates the file.
 */
VFID *
file_create_tmp (THREAD_ENTRY * thread_p, VFID * vfid, INT32 exp_numpages, const void *file_des)
{
  FILE_TYPE file_type = FILE_TEMP;

  if (file_tmpfile_cache_get (thread_p, vfid, FILE_TEMP))
    {
      return vfid;
    }

  return file_create_tmp_internal (thread_p, vfid, &file_type, exp_numpages, file_des);
}

/*
 * file_create_tmp_no_cache () - Create a new unstructured temporary file
 *   return: vfid or NULL in case of error
 *   vfid(out): complete file identifier (i.e., volume_id + file_id)
 *   exp_numpages(in): expected number of pages to be allocated
 *   file_des(in):
 *
 * Note: Unlike file_create_tmp, it always creates a temporary file (does
 *	 not use files from cache). See file_create_tmp for more details.
 */
VFID *
file_create_tmp_no_cache (THREAD_ENTRY * thread_p, VFID * vfid, INT32 exp_numpages, const void *file_des)
{
  FILE_TYPE file_type = FILE_TEMP;

  return file_create_tmp_internal (thread_p, vfid, &file_type, exp_numpages, file_des);
}

/*
 * file_destroy_cached_tmp () - Drop cached temp files in temporary temp file
 *   return: NO_ERROR
 *   volid(in):
 */
int
file_destroy_cached_tmp (THREAD_ENTRY * thread_p, VOLID volid)
{
  FILE_TEMPFILE_CACHE_ENTRY *p;
  int idx, prev;

  assert (false);

  if (csect_enter (thread_p, CSECT_TEMPFILE_CACHE, INF_WAIT) != NO_ERROR)
    {
      return ER_FAILED;
    }

  idx = file_Tempfile_cache.first_idx;
  prev = -1;
  while (idx != -1)
    {
      p = &file_Tempfile_cache.entry[idx];
      if (p->vfid.volid == volid)
	{
	  if (prev == -1)
	    {
	      file_Tempfile_cache.first_idx = p->next_entry;
	    }
	  else
	    {
	      file_Tempfile_cache.entry[prev].next_entry = p->next_entry;
	    }

	  p->next_entry = file_Tempfile_cache.free_idx;
	  file_Tempfile_cache.free_idx = p->idx;

	  (void) file_destroy_without_reuse (thread_p, &p->vfid);
	}
      prev = idx;
      idx = p->next_entry;
    }

  csect_exit (thread_p, CSECT_TEMPFILE_CACHE);

  return NO_ERROR;
}

/*
 * file_create_queryarea () - Create a new unstructured query area file
 *   return: vfid or NULL in case of error
 *   vfid(out): complete file identifier (i.e., volume_id + file_id)
 *   exp_numpages(in): expected number of pages to be allocated
 *   file_des(in):
 *
 * Note: Purpose of this type of file is to retain it beyond transaction
 *       boundary, e.g. XASL stream cache file. Any other aspects are same
 *       with file_create_tmp()
 */
VFID *
file_create_queryarea (THREAD_ENTRY * thread_p, VFID * vfid, INT32 exp_numpages, const void *file_des)
{
  FILE_TYPE file_type = FILE_QUERY_AREA;
  VFID *tmpfile_vfid = NULL;
  VPID first_page_vpid, vpid;
  PAGE_PTR fhdr_pgptr = NULL;
  FILE_HEADER *fhdr;

  tmpfile_vfid = file_tmpfile_cache_get (thread_p, vfid, file_type);

  if (tmpfile_vfid == NULL)
    {
      VPID_SET_NULL (&first_page_vpid);

      tmpfile_vfid = file_create_tmp_internal (thread_p, vfid, &file_type, exp_numpages, file_des);
      if (tmpfile_vfid != NULL)
	{
	  if (file_alloc_pages (thread_p, tmpfile_vfid, &first_page_vpid, 1, NULL, NULL, NULL) == NULL)
	    {
	      goto exit_on_error;
	    }
	}
      else
	{
	  goto exit_on_error;
	}

      assert (!VPID_ISNULL (&first_page_vpid));

      vpid.volid = tmpfile_vfid->volid;
      vpid.pageid = tmpfile_vfid->fileid;
      fhdr_pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
      if (fhdr_pgptr == NULL)
	{
	  goto exit_on_error;
	}

      (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);
      fhdr = (FILE_HEADER *) (fhdr_pgptr + FILE_HEADER_OFFSET);

      assert (VPID_ISNULL (&fhdr->first_alloc_vpid));

      VPID_COPY (&fhdr->first_alloc_vpid, &first_page_vpid);

      pgbuf_set_dirty (thread_p, fhdr_pgptr, FREE);
    }

  return tmpfile_vfid;

exit_on_error:

  if (tmpfile_vfid != NULL)
    {
      /* Don't keep an immature temp file in the temp file cache */
      (void) file_destroy_internal (thread_p, tmpfile_vfid, false);
    }

  return NULL;
}

/* TODO: list for file_ftab_chain */
/*
 * file_xcreate () - Create an unstructured file in a volume
 *   return: vfid or NULL in case of error
 *   vfid(out): Complete file identifier (i.e., Volume_id + file_id)
 *   exp_numpages(in): Expected number of pages
 *   file_type(in): Type of file
 *   file_des(in): Add the follwing file description to the file
 *   first_prealloc_vpid(out): An array of VPIDs for preallocated pages or NULL
 *   prealloc_npages(in): Number of pages to allocate
 *
 * Note: Create an unstructured file in a volume that hopefully has at least
 *       exp_numpages free pages. The volume indicated in vfid->volid is used
 *       as a hint for the allocation of the file. A set of sectors is
 *       allocated to improve locality of the file. The sectors allocated are
 *       estimated from the number of expected pages. The maximum number of
 *       allocated sectors is 25% of the total number of sectors in disk.
 *       Note the number of expected pages are not allocated at this point,
 *       they are allocated as needs arise.
 *
 *       The expected number of pages can be estimated in some cases, for
 *       example, when a B+tree is created, the minimal number of pages needed
 *       can be estimated from the loading file. When the number of expected
 *       pages cannot be estimated (a zero or negative value can be passed).
 *       Neither in this case nor when the expected number of pages is smaller
 *       than the number of pages within a sector, are sectors allocated.
 */
static VFID *
file_xcreate (THREAD_ENTRY * thread_p, VFID * vfid, INT32 exp_numpages, FILE_TYPE * file_type, const void *file_des,
	      VPID * first_prealloc_vpid, INT32 prealloc_npages)
{
  FILE_HEADER *fhdr;
  FILE_ALLOCSET *allocset;	/* The first allocation set */
  FILE_FTAB_CHAIN *chain;	/* Structure for linking file table pages */
  PAGE_PTR fhdr_pgptr = NULL;
  INT32 *aid_ptr;		/* Pointer to portion of sector identifiers */
  INT32 *outptr;		/* Out of bound pointer. */
  INT32 num_ftb_pages;		/* Number of pages for file table */
  INT32 nsects;
  INT32 sect25;			/* 25% of total number of sectors */
  INT32 sectid = NULL_SECTID;	/* Identifier of first sector for the expected number of pages */
  VPID vpid;
  LOG_DATA_ADDR addr;
  char *logdata;
  VPID *table_vpids = NULL;
  VPID *vpid_ptr;
  VPID tmp_vpid;
  int i, ftb_page_index;	/* Index into the allocated file table pages */
  int length;
  DISK_VOLPURPOSE vol_purpose;
  DISK_SETPAGE_TYPE setpage_type;
  int ret = NO_ERROR;

  assert (false);

  if (exp_numpages <= 0)
    {
      exp_numpages = 1;
    }

  if (*file_type == FILE_BTREE || *file_type == FILE_HEAP)
    {
      if (prealloc_npages == 0)
	{
	  assert (false);
	  prealloc_npages = 1;
	  first_prealloc_vpid = &tmp_vpid;
	}
    }

  /* 
   * Start a TOP SYSTEM OPERATION.
   * This top system operation will be either ABORTED (case of failure) or
   * its actions will be attached to its outer parent
   */
  if (log_start_system_op (thread_p) == NULL)
    {
      VFID_SET_NULL (vfid);
      return NULL;
    }

  setpage_type = ((prealloc_npages < 0) ? DISK_CONTIGUOUS_PAGES : DISK_NONCONTIGUOUS_SPANVOLS_PAGES);

  i = file_guess_numpages_overhead (thread_p, NULL, exp_numpages);
  exp_numpages += i;
  exp_numpages = MIN (exp_numpages, VOL_MAX_NPAGES (IO_PAGESIZE));

  vpid.volid = file_find_goodvol (thread_p, vfid->volid, NULL_VOLID, exp_numpages, setpage_type, *file_type);
  if (vpid.volid != NULL_VOLID)
    {
      vfid->volid = vpid.volid;
    }
  else
    {
      /* 
       * We could not find volumes with the expected number of pages, and we
       * we were not able to create a new volume.
       * Quit if we request few pages, Otherwise, continue using the hinted
       * volume
       */
      if (exp_numpages <= 2 || vfid->volid == NULL_VOLID || fileio_get_volume_descriptor (vfid->volid) == NULL_VOLDES)
	{
	  /* We could not find a volume with enough pages. An error has already been set by file_find_goodvol (). */
	  goto exit_on_error;
	}
    }

  if (*file_type == FILE_TEMP)
    {
      vol_purpose = xdisk_get_purpose (thread_p, vfid->volid);
      if (vol_purpose == DISK_UNKNOWN_PURPOSE)
	{
	  goto exit_on_error;
	}

      if (vol_purpose == DB_TEMPORARY_DATA_PURPOSE)
	{
	  /* Use volumes with temporary purposes only for every single page of this file */
	  *file_type = FILE_TEMP;
	}
      else
	{
	  assert (0);
	  goto exit_on_error;
	}
    }

  vfid->fileid = NULL_FILEID;

  /* 
   * Estimate the number of sectors needed for the expected number of pages.
   * Sectors are pre-allocated for locality reasons. More sectors can be
   * allocated at any time, for example, when more than the expected number of
   * pages are allocated.
   */

  if (exp_numpages > DISK_SECTOR_NPAGES)
    {
      if (*file_type == FILE_TEMP || *file_type == FILE_QUERY_AREA)
	{
	  /* We are going to allocate the special sector */
	  nsects = 1;
	}
      else
	{
	  nsects = CEIL_PTVDIV (exp_numpages, DISK_SECTOR_NPAGES);
	}
      /* 
       * Don't allocate more than 25% of the total number of sectors of main
       * volume. Or more than the number of sectors that can be addressed in
       * a single page. Note later on they will be increased as needed.
       */

      if ((nsects * sizeof (sectid)) > PGLENGTH_MAX)
	{
	  /* Too many sectors to start with.. */
	  nsects = PGLENGTH_MAX / sizeof (sectid);

	  if (nsects > DB_PAGESIZE)
	    {
	      nsects -= DB_PAGESIZE;
	    }
	}

      sect25 = disk_get_total_numsectors (thread_p, vfid->volid) / 4;
      if (nsects > 1 && nsects > sect25)
	{
	  nsects = sect25;
	}
    }
  else
    {
      nsects = 0;
    }

  if (file_des != NULL)
    {
      length = file_descriptor_get_length (*file_type);
      length += DOUBLE_ALIGNMENT;
    }
  else
    {
      length = 0;
    }

  /* Estimate the length to write at this moment */
  length = (length + (sizeof (sectid) * (nsects + FILE_NUM_EMPTY_SECTS)) + sizeof (vpid.pageid));

  /* Subtract the first page. Then, calculate the number of needed pages */
  length -= DB_PAGESIZE - sizeof (*fhdr);

  if (length > 0)
    {
      i = DB_PAGESIZE - sizeof (FILE_FTAB_CHAIN);
      num_ftb_pages = 1 + CEIL_PTVDIV (length, i);
    }
  else
    {
      num_ftb_pages = 1;
    }

  /* Allocate the needed file table pages for the file. The first file table page is declared as the file identifier */
  table_vpids = (VPID *) malloc (sizeof (vpid) * num_ftb_pages);
  if (table_vpids == NULL)
    {
      ret = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ret, 1, sizeof (vpid) * num_ftb_pages);
      goto exit_on_error;
    }

  ret = file_ftabvpid_alloc (thread_p, vfid->volid, NULL_PAGEID, table_vpids, num_ftb_pages, *file_type);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }
  vfid->volid = table_vpids[0].volid;
  vfid->fileid = table_vpids[0].pageid;

  addr.vfid = vfid;

  /* Allocate the needed sectors for the newly allocated file at the volume where the file was allocated. */

  if (nsects > 1)
    {
      if (*file_type == FILE_TEMP || *file_type == FILE_QUERY_AREA)
	{
	  /* We are going to allocate the special sector */
	  sectid = disk_alloc_special_sector ();
	  nsects = 1;
	}
      else
	{
	  sectid = disk_alloc_sector (thread_p, vfid->volid, nsects, -1);
	  if (sectid == DISK_SECTOR_WITH_ALL_PAGES)
	    {
	      nsects = 1;
	    }
	}
    }
  else
    {
      sectid = NULL_SECTID;
    }

  /* 
   * Double link the file table pages
   * Skip the header page from this loop since a header page does not have
   * the same type of chain. It has the header of the chain.
   */

  for (ftb_page_index = 1; ftb_page_index < num_ftb_pages; ftb_page_index++)
    {
      vpid_ptr = &table_vpids[ftb_page_index];
      addr.pgptr = pgbuf_fix (thread_p, vpid_ptr, NEW_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
      if (addr.pgptr == NULL)
	{
	  goto exit_on_error;
	}

      (void) pgbuf_set_page_ptype (thread_p, addr.pgptr, PAGE_FTAB);

      /* Chain forward and backward */
      chain = (FILE_FTAB_CHAIN *) (addr.pgptr + FILE_FTAB_CHAIN_OFFSET);

      if ((ftb_page_index + 1) == num_ftb_pages)
	{
	  VPID_SET_NULL (&chain->next_ftbvpid);
	}
      else
	{
	  chain->next_ftbvpid = table_vpids[ftb_page_index + 1];
	}

      chain->prev_ftbvpid = table_vpids[ftb_page_index - 1];

      /* DON'T NEED to log before image (undo) since this is a newly created file and pages of the file would be
       * deallocated during undo(abort). */

      addr.offset = FILE_FTAB_CHAIN_OFFSET;
      log_append_redo_data (thread_p, RVFL_FTAB_CHAIN, &addr, sizeof (*chain), chain);
      pgbuf_set_dirty (thread_p, addr.pgptr, FREE);
      addr.pgptr = NULL;
    }

  fhdr_pgptr = pgbuf_fix (thread_p, &table_vpids[0], NEW_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (fhdr_pgptr == NULL)
    {
      goto exit_on_error;
    }

  (void) pgbuf_set_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);

  /* Initialize file header */

  fhdr = (FILE_HEADER *) (fhdr_pgptr + FILE_HEADER_OFFSET);

  VFID_COPY (&fhdr->vfid, vfid);
  fhdr->type = (INT16) (*file_type);
  fhdr->creation = time (NULL);
  fhdr->ismark_as_deleted = false;

  fhdr->num_table_vpids = num_ftb_pages;
  if (num_ftb_pages > 1)
    {
      fhdr->next_table_vpid = table_vpids[1];
    }
  else
    {
      VPID_SET_NULL (&fhdr->next_table_vpid);
    }

  fhdr->last_table_vpid = table_vpids[num_ftb_pages - 1];

  fhdr->num_user_pages = 0;
  fhdr->num_user_pages_mrkdelete = 0;
  fhdr->num_allocsets = 1;

  /* The last allocated set is defined in the header page */
  fhdr->last_allocset_vpid = table_vpids[0];
  fhdr->last_allocset_offset = offsetof (FILE_HEADER, allocset);

  VPID_SET_NULL (&fhdr->first_alloc_vpid);

  /* Add the description comments */
  ret = file_descriptor_insert (thread_p, fhdr, file_des);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  /* Initialize first allocation set */

  allocset = &fhdr->allocset;
  allocset->volid = vfid->volid;
  allocset->num_sects = nsects;
  allocset->num_pages = 0;
  allocset->curr_sectid = sectid;
  allocset->num_holes = 0;

  /* Calculate positions for sector table in the first allocation set */

  if (FILE_SIZEOF_FHDR_WITH_DES_COMMENTS (fhdr) < DB_PAGESIZE)
    {
      ftb_page_index = 0;	/* In header page */
      allocset->start_sects_offset = (INT16) FILE_SIZEOF_FHDR_WITH_DES_COMMENTS (fhdr);
      allocset->start_sects_offset = DB_ALIGN (allocset->start_sects_offset, DOUBLE_ALIGNMENT);
    }
  else
    {
      ftb_page_index = 1;	/* Next page after header page */
      allocset->start_sects_offset = sizeof (*chain);
    }

  allocset->start_sects_vpid = table_vpids[ftb_page_index];
  allocset->end_sects_vpid = table_vpids[ftb_page_index];

  /* find out end_sects_offset */
  ret =
    file_calculate_offset (allocset->start_sects_offset, (int) sizeof (sectid), allocset->num_sects,
			   &allocset->end_sects_offset, &allocset->end_sects_vpid, table_vpids, num_ftb_pages,
			   &ftb_page_index);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  /* Calculate positions for page table in the first allocation set */

  allocset->start_pages_vpid = table_vpids[ftb_page_index];

  /* find out start_pages_offset */
  ret =
    file_calculate_offset (allocset->end_sects_offset, (int) sizeof (sectid), FILE_NUM_EMPTY_SECTS,
			   &allocset->start_pages_offset, &allocset->start_pages_vpid, table_vpids, num_ftb_pages,
			   &ftb_page_index);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  /* The end of the page table is the same as the beginning since there is not any allocated pages */
  allocset->end_pages_vpid = allocset->start_pages_vpid;
  allocset->end_pages_offset = allocset->start_pages_offset;

  /* Indicate that this is the last set */

  VPID_SET_NULL (&allocset->next_allocset_vpid);
  allocset->next_allocset_offset = NULL_OFFSET;

  /* DON'T NEED to log before image (undo) since file and pages of file would be deallocated during undo(abort). */

  addr.pgptr = fhdr_pgptr;
  addr.offset = FILE_HEADER_OFFSET;
  log_append_redo_data (thread_p, RVFL_FHDR, &addr, FILE_SIZEOF_FHDR_WITH_DES_COMMENTS (fhdr), fhdr);
  pgbuf_set_dirty (thread_p, fhdr_pgptr, DONT_FREE);

  /* Initialize the table of sector identifiers */

  if (nsects > 0)
    {
      addr.pgptr =
	pgbuf_fix (thread_p, &allocset->start_sects_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
      if (addr.pgptr == NULL)
	{
	  goto exit_on_error;
	}

      (void) pgbuf_check_page_ptype (thread_p, addr.pgptr, PAGE_FTAB);

      /* Find the location for the sector table */
      ret = file_find_limits (addr.pgptr, allocset, &aid_ptr, &outptr, FILE_SECTOR_TABLE);
      if (ret != NO_ERROR)
	{
	  pgbuf_unfix_and_init (thread_p, addr.pgptr);
	  goto exit_on_error;
	}

      addr.offset = allocset->start_sects_offset;
      logdata = (char *) aid_ptr;

      for (i = 0; i < nsects; i++)
	{
	  if (aid_ptr >= outptr)
	    {
	      /* Next file table page */

	      /* DON'T NEED to log before image (undo) since file and pages of file would be deallocated during
	       * undo(abort). */
	      log_append_redo_data (thread_p, RVFL_IDSTABLE, &addr, CAST_BUFLEN ((char *) aid_ptr - logdata), logdata);

	      ret = file_ftabvpid_next (fhdr, addr.pgptr, &vpid);
	      if (ret != NO_ERROR)
		{
		  goto exit_on_error;
		}
	      pgbuf_set_dirty (thread_p, addr.pgptr, FREE);
	      addr.pgptr = NULL;

	      addr.pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
	      if (addr.pgptr == NULL)
		{
		  goto exit_on_error;
		}

	      (void) pgbuf_check_page_ptype (thread_p, addr.pgptr, PAGE_FTAB);

	      ret = file_find_limits (addr.pgptr, allocset, &aid_ptr, &outptr, FILE_SECTOR_TABLE);
	      if (ret != NO_ERROR)
		{
		  pgbuf_unfix_and_init (thread_p, addr.pgptr);
		  goto exit_on_error;
		}
	      addr.offset = sizeof (*chain);
	      logdata = (char *) aid_ptr;
	    }

	  assert (NULL_PAGEID <= (sectid + i));	/* FIXME: upper bound */
	  *aid_ptr++ = sectid + i;
	}

      /* Don't need to log before image (undo) since file and pages of file would be deallocated during undo(abort). */
      log_append_redo_data (thread_p, RVFL_IDSTABLE, &addr, CAST_BUFLEN ((char *) aid_ptr - logdata), logdata);
      pgbuf_set_dirty (thread_p, addr.pgptr, FREE);
      addr.pgptr = NULL;
    }

  /* Register the file and remember that this is a newly created file */
  if ((file_Tracker->vfid != NULL && file_tracker_register (thread_p, vfid) == NULL)
      || file_cache_newfile (thread_p, vfid, *file_type) == NULL)
    {
      goto exit_on_error;
    }

  /* Free the header page. */
  pgbuf_unfix_and_init (thread_p, fhdr_pgptr);
  free_and_init (table_vpids);

  if (prealloc_npages != 0)
    {
      if (prealloc_npages > 0)
	{
	  /* First time */
	  if (file_alloc_pages_at_volid (thread_p, vfid, first_prealloc_vpid, prealloc_npages, NULL, vfid->volid, NULL,
					 NULL) == NULL)
	    {
	      /* 
	       * We were unable to allocate the pages on the desired volume. In
	       * this case we need to make sure that the file is created in a
	       * volume with enough pages to preallocate the desired pages. For
	       * now, we can ignore the hint of expected pages. We would be
	       * looking for a volume with contiguous pages.
	       *
	       * Undo whatever we have done to this file, and try again.
	       */
	      (void) log_end_system_op (thread_p, LOG_RESULT_TOPOP_ABORT);
	      exp_numpages = prealloc_npages;
	      prealloc_npages = -prealloc_npages;
	      return file_xcreate (thread_p, vfid, exp_numpages, file_type, file_des, first_prealloc_vpid,
				   prealloc_npages);
	    }
	}
      else
	{
	  /* Second time. If it fails Bye... */
	  prealloc_npages = -prealloc_npages;
	  if (file_alloc_pages_at_volid (thread_p, vfid, first_prealloc_vpid, prealloc_npages, NULL, vfid->volid, NULL,
					 NULL) == NULL)
	    {
	      /* We were unable to allocate the pages on the desired volume. */
	      (void) log_end_system_op (thread_p, LOG_RESULT_TOPOP_ABORT);
	      return NULL;
	    }
	}

      assert (!VPID_ISNULL (first_prealloc_vpid));

      vpid.volid = vfid->volid;
      vpid.pageid = vfid->fileid;
      fhdr_pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
      if (fhdr_pgptr == NULL)
	{
	  goto exit_on_error;
	}

      (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);
      fhdr = (FILE_HEADER *) (fhdr_pgptr + FILE_HEADER_OFFSET);

      assert (VPID_ISNULL (&fhdr->first_alloc_vpid));

      VPID_COPY (&fhdr->first_alloc_vpid, first_prealloc_vpid);

      addr.vfid = vfid;
      addr.pgptr = fhdr_pgptr;
      addr.offset = FILE_HEADER_OFFSET;

      /* DON'T NEED to log before image (undo) since this is a newly created file and pages of the file would be
       * deallocated during undo(abort). */

      log_append_redo_data (thread_p, RVFL_FHDR, &addr, FILE_SIZEOF_FHDR_WITH_DES_COMMENTS (fhdr), fhdr);

      pgbuf_set_dirty (thread_p, fhdr_pgptr, FREE);
      addr.pgptr = NULL;
    }

  /* The responsability of the creation of the file is given to the outer nested level */
  log_end_system_op (thread_p, LOG_RESULT_TOPOP_ATTACH_TO_OUTER);

  return vfid;

exit_on_error:

  /* ABORT THE TOP SYSTEM OPERATION. That is, the creation of the file is aborted, all pages that were allocated are
   * deallocated at this point */
  (void) log_end_system_op (thread_p, LOG_RESULT_TOPOP_ABORT);

  if (fhdr_pgptr != NULL)
    {
      pgbuf_unfix_and_init (thread_p, fhdr_pgptr);
    }

  if (table_vpids != NULL)
    {
      free_and_init (table_vpids);
    }

  VFID_SET_NULL (vfid);

  if (ret == NO_ERROR)
    {
      ret = ER_FAILED;
    }

  return NULL;
}

/*
 * file_destroy () - Destroy a file
 *   return:
 *   vfid(in): Complete file identifier
 *
 * Note: The pages and sectors assigned to the given file are deallocated and
 *       the file is removed.
 */
int
file_destroy (THREAD_ENTRY * thread_p, const VFID * vfid)
{
  return file_destroy_internal (thread_p, vfid, true);
}

static int
file_destroy_internal (THREAD_ENTRY * thread_p, const VFID * vfid, bool put_cache)
{
  VPID allocset_vpid;		/* Page-volume identifier of allocation set */
  VPID nxftb_vpid;		/* Page-volume identifier of file tables pages. Part of allocation sets. */
  FILE_HEADER *fhdr;
  FILE_ALLOCSET *allocset;	/* The first allocation set */
  PAGE_PTR fhdr_pgptr = NULL;
  PAGE_PTR allocset_pgptr = NULL;
  PAGE_PTR pgptr = NULL;
  INT32 *aid_ptr;		/* Pointer to portion of table of page or sector allocation table */
  INT32 *outptr;		/* Out of bound pointer. */
  INT16 allocset_offset;
  LOG_DATA_ADDR addr;

  INT32 batch_firstid;		/* First sectid in batch */
  INT32 batch_ndealloc;		/* # of sectors to deallocate in the batch */
  FILE_TYPE file_type;
  bool pb_invalid_temp_called = false;

  addr.vfid = vfid;

  allocset_vpid.volid = vfid->volid;
  allocset_vpid.pageid = vfid->fileid;

  fhdr_pgptr = pgbuf_fix (thread_p, &allocset_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (fhdr_pgptr == NULL)
    {
      return ER_FAILED;
    }

  (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);

  fhdr = (FILE_HEADER *) (fhdr_pgptr + FILE_HEADER_OFFSET);

  file_type = fhdr->type;

  if ((file_type == FILE_TEMP || file_type == FILE_QUERY_AREA)
      && fhdr->num_user_pages < prm_get_integer_value (PRM_ID_MAX_PAGES_IN_TEMP_FILE_CACHE))
    {
      if (0 < fhdr->num_user_pages)
	{
	  /* We need to invalidate all the pages set */
	  allocset_offset = offsetof (FILE_HEADER, allocset);
	  while (!VPID_ISNULL (&allocset_vpid))
	    {
	      allocset_pgptr =
		pgbuf_fix (thread_p, &allocset_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
	      if (allocset_pgptr == NULL)
		{
		  pgbuf_unfix_and_init (thread_p, fhdr_pgptr);

		  return ER_FAILED;
		}

	      (void) pgbuf_check_page_ptype (thread_p, allocset_pgptr, PAGE_FTAB);

	      allocset = (FILE_ALLOCSET *) ((char *) allocset_pgptr + allocset_offset);

	      nxftb_vpid = allocset->start_pages_vpid;
	      while (!VPID_ISNULL (&nxftb_vpid))
		{
		  pgptr = pgbuf_fix (thread_p, &nxftb_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
		  if (pgptr == NULL)
		    {
		      pgbuf_unfix_and_init (thread_p, allocset_pgptr);
		      pgbuf_unfix_and_init (thread_p, fhdr_pgptr);

		      return ER_FAILED;
		    }

		  (void) pgbuf_check_page_ptype (thread_p, pgptr, PAGE_FTAB);

		  /* Calculate the starting offset and length of the page to check */
		  if (file_find_limits (pgptr, allocset, &aid_ptr, &outptr, FILE_PAGE_TABLE) != NO_ERROR)
		    {
		      pgbuf_unfix_and_init (thread_p, pgptr);
		      pgbuf_unfix_and_init (thread_p, allocset_pgptr);
		      pgbuf_unfix_and_init (thread_p, fhdr_pgptr);

		      return ER_FAILED;
		    }

		  batch_firstid = NULL_PAGEID;
		  batch_ndealloc = 0;

		  for (; aid_ptr < outptr; aid_ptr++)
		    {
		      if (*aid_ptr > NULL_PAGEID)
			{
			  if (batch_ndealloc == 0)
			    {
			      /* Start accumulating contiguous pages */
			      batch_firstid = *aid_ptr;
			      batch_ndealloc = 1;
			    }
			  else
			    {
			      /* Is page contiguous ? */
			      if (*aid_ptr == batch_firstid + batch_ndealloc)
				{
				  /* contiguous */
				  batch_ndealloc++;
				}
			      else
				{
				  /* 
				   * This is not a contiguous page.
				   * Deallocate any previous pages and start
				   * accumulating contiguous pages again.
				   * We do not care if the page deallocation
				   * failed, since we are destroying the file..
				   * Deallocate as much as we can.
				   */

				  pgbuf_invalidate_temporary_file (allocset->volid, batch_firstid, batch_ndealloc,
								   true);

				  /* Start again */
				  batch_firstid = *aid_ptr;
				  batch_ndealloc = 1;
				}
			    }
			}
		      else
			{
			  assert (*aid_ptr == NULL_PAGEID || *aid_ptr == NULL_PAGEID_MARKED_DELETED);
			}
		    }

		  if (batch_ndealloc > 0)
		    {
		      /* Deallocate any accumulated pages */

		      pgbuf_invalidate_temporary_file (allocset->volid, batch_firstid, batch_ndealloc, true);
		    }

		  /* Get next page in the allocation set */
		  if (VPID_EQ (&nxftb_vpid, &allocset->end_pages_vpid))
		    {
		      VPID_SET_NULL (&nxftb_vpid);
		    }
		  else
		    {
		      if (file_ftabvpid_next (fhdr, pgptr, &nxftb_vpid) != NO_ERROR)
			{
			  pgbuf_unfix_and_init (thread_p, pgptr);
			  return ER_FAILED;
			}
		    }
		  pgbuf_unfix_and_init (thread_p, pgptr);
		}

	      if (VPID_ISNULL (&allocset->next_allocset_vpid))
		{
		  VPID_SET_NULL (&allocset_vpid);
		  allocset_offset = -1;
		}
	      else
		{
		  if (VPID_EQ (&allocset_vpid, &allocset->next_allocset_vpid)
		      && allocset_offset == allocset->next_allocset_offset)
		    {
		      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_FILE_FTB_LOOP, 4, vfid->fileid,
			      fileio_get_volume_label (vfid->volid, PEEK), allocset->next_allocset_vpid.volid,
			      allocset->next_allocset_vpid.pageid);
		      VPID_SET_NULL (&allocset_vpid);
		      allocset_offset = -1;
		    }
		  else
		    {
		      allocset_vpid = allocset->next_allocset_vpid;
		      allocset_offset = allocset->next_allocset_offset;
		    }
		}
	      pgbuf_unfix_and_init (thread_p, allocset_pgptr);
	    }
	}

      assert (file_type == FILE_TEMP || file_type == FILE_QUERY_AREA);

      pb_invalid_temp_called = true;

      if (put_cache)
	{
	  if (file_tmpfile_cache_put (thread_p, vfid, file_type))
	    {
	      pgbuf_unfix_and_init (thread_p, fhdr_pgptr);
	      return NO_ERROR;
	    }
	}
    }

  pgbuf_unfix_and_init (thread_p, fhdr_pgptr);

  return file_xdestroy (thread_p, vfid, pb_invalid_temp_called);
}

/*
 * file_rv_postpone_destroy_file () - Execute file destroy during postpone.
 *				      It was added in order to avoid double
 *				      page deallocations because of vacuum
 *				      workers.
 *
 * return	 : Error code.
 * thread_p (in) : Thread entry.
 * rcv (in)	 : Recovery data (file VFID).
 */
int
file_rv_postpone_destroy_file (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  VFID *vfid;
  BTID btid;
  int error_code = NO_ERROR;
  bool is_btid = false;
  GLOBAL_UNIQUE_STATS *stats = NULL;
  int num_oids, num_keys = -1, num_nulls;
  LF_TRAN_ENTRY *t_entry;

  assert (false);

  if (rcv->length == sizeof (*vfid))
    {
      vfid = (VFID *) rcv->data;
    }
  else
    {
      assert (rcv->length == OR_BTID_ALIGNED_SIZE);
      OR_GET_BTID (rcv->data, &btid);
      vfid = &btid.vfid;
      is_btid = true;
    }

  /* Start postpone type system operation (overrides the normal system operation). */
  log_start_postpone_system_op (thread_p, &rcv->reference_lsa);

  /* We need to unregister file before deallocating its pages. */
  if (file_tracker_unregister (thread_p, vfid) == NULL)
    {
      assert_release (false);
      log_end_system_op (thread_p, LOG_RESULT_TOPOP_ABORT);
      return ER_FAILED;
    }

  if (is_btid)
    {
      /* save global statistics before delete */
      t_entry = thread_get_tran_entry (thread_p, THREAD_TS_GLOBAL_UNIQUE_STATS);
      lf_hash_find (t_entry, &log_Gl.unique_stats_table.unique_stats_hash, &btid, (void **) &stats);
      if (stats)
	{
	  num_oids = stats->unique_stats.num_oids;
	  num_keys = stats->unique_stats.num_keys;
	  num_nulls = stats->unique_stats.num_nulls;
	  pthread_mutex_unlock (&stats->mutex);
	}

      /* delete global statistics */
      (void) logtb_delete_global_unique_stats (thread_p, &btid);
    }

  /* Destroy file */
  error_code = file_destroy (thread_p, vfid);
  if (error_code != NO_ERROR)
    {
      assert_release (false);

      if (is_btid)
	{
	  if (num_keys != -1)
	    {
	      /* restore global statistics for debug purpose */
	      stats = NULL;
	      lf_hash_find_or_insert (t_entry, &log_Gl.unique_stats_table.unique_stats_hash, &btid, (void **) &stats,
				      NULL);
	      stats->unique_stats.num_oids = num_oids;
	      stats->unique_stats.num_keys = num_keys;
	      stats->unique_stats.num_nulls = num_nulls;
	      pthread_mutex_unlock (&stats->mutex);
	    }
	}

      log_end_system_op (thread_p, LOG_RESULT_TOPOP_ABORT);
      return error_code;
    }

  /* Success. */
  log_end_system_op (thread_p, LOG_RESULT_TOPOP_COMMIT);
  return NO_ERROR;
}

/*
 * file_destroy_without_reuse () - Destroy a file not regarding reuse file
 *   return:
 *   vfid(in): Complete file identifier
 *
 * Note: The pages and sectors assigned to the given file are deallocated and
 *       the file is removed.
 */
int
file_destroy_without_reuse (THREAD_ENTRY * thread_p, const VFID * vfid)
{
  return file_xdestroy (thread_p, vfid, false);
}


static int
file_xdestroy (THREAD_ENTRY * thread_p, const VFID * vfid, bool pb_invalid_temp_called)
{
  VPID allocset_vpid;		/* Page-volume identifier of allocation set */
  VPID nxftb_vpid;		/* Page-volume identifier of file tables pages. Part of allocation sets. */
  VPID curftb_vpid;
  FILE_HEADER *fhdr;
  FILE_ALLOCSET *allocset;	/* The first allocation set */
  PAGE_PTR fhdr_pgptr = NULL;
  PAGE_PTR allocset_pgptr = NULL;
  PAGE_PTR pgptr = NULL;
  INT32 *aid_ptr;		/* Pointer to portion of table of page or sector allocation table */
  INT32 *outptr;		/* Out of bound pointer. */
  INT16 allocset_offset;
  LOG_DATA_ADDR addr;
  INT16 batch_volid;		/* The volume identifier in the batch of contiguous ids */
  INT32 batch_firstid;		/* First sectid in batch */
  INT32 batch_ndealloc;		/* # of sectors to deallocate in the batch */
  FILE_TYPE file_type;
  bool old_val;
  int ret = NO_ERROR;
  int rv;
  DISK_PAGE_TYPE page_type;
  LOG_TDES *tdes = LOG_FIND_CURRENT_TDES (thread_p);

  assert (false);

  /* 
   * Start a TOP SYSTEM OPERATION.
   * This top system operation will be either ABORTED (case of failure) or
   * its actions will be attached to its outer parent. That is, the file
   * cannot be destroyed half way.
   */
  if (log_start_system_op (thread_p) == NULL)
    {
      return ER_FAILED;
    }

  assert (tdes != NULL);

  /* Safe guard: postpone system operation is allowed only if transaction state is
   * TRAN_UNACTIVE_COMMITTED_WITH_POSTPONE. */
  assert (tdes->topops.type != LOG_TOPOPS_POSTPONE || tdes->state == TRAN_UNACTIVE_COMMITTED_WITH_POSTPONE);

  old_val = thread_set_check_interrupt (thread_p, false);

  ret = file_type_cache_entry_remove (vfid);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  allocset_vpid.volid = vfid->volid;
  allocset_vpid.pageid = vfid->fileid;

  fhdr_pgptr = pgbuf_fix (thread_p, &allocset_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (fhdr_pgptr == NULL)
    {
      goto exit_on_error;
    }

  (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);

  fhdr = (FILE_HEADER *) (fhdr_pgptr + FILE_HEADER_OFFSET);
  file_type = fhdr->type;
  page_type = file_get_disk_page_type (file_type);

  if (!VFID_EQ (vfid, &fhdr->vfid))
    {
      /* Header of file seems to be corrupted */
      ret = ER_FILE_TABLE_CORRUPTED;
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ret, 2, vfid->volid, vfid->fileid);
      goto exit_on_error;
    }

  /* Deallocate all user pages */
  if (fhdr->num_user_pages > 0)
    {
      FILE_RECV_DELETE_PAGES postpone_data;
      int num_user_pages;
      INT32 undo_data, redo_data;

      /* We need to deallocate all the pages and sectors of every allocated set */
      allocset_offset = offsetof (FILE_HEADER, allocset);
      while (!VPID_ISNULL (&allocset_vpid))
	{
	  /* 
	   * Fetch the page that describe this allocation set even when it has
	   * already been fetched as a file header page. This is done to code
	   * the following easily.
	   */
	  allocset_pgptr = pgbuf_fix (thread_p, &allocset_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
	  if (allocset_pgptr == NULL)
	    {
	      goto exit_on_error;
	    }

	  (void) pgbuf_check_page_ptype (thread_p, allocset_pgptr, PAGE_FTAB);

	  allocset = (FILE_ALLOCSET *) ((char *) allocset_pgptr + allocset_offset);

	  /* Deallocate the pages in this allocation set */

	  nxftb_vpid = allocset->start_pages_vpid;
	  while (!VPID_ISNULL (&nxftb_vpid))
	    {
	      /* 
	       * Fetch the page where the portion of the page table is located.
	       * Note that we are fetching the page even when it has been
	       * fetched previously as an allocation set. This is done to make
	       * the following code easy to write.
	       */
	      pgptr = pgbuf_fix (thread_p, &nxftb_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
	      if (pgptr == NULL)
		{
		  goto exit_on_error;
		}

	      (void) pgbuf_check_page_ptype (thread_p, pgptr, PAGE_FTAB);

	      /* Calculate the starting offset and length of the page to check */
	      if (file_find_limits (pgptr, allocset, &aid_ptr, &outptr, FILE_PAGE_TABLE) != NO_ERROR)
		{
		  pgbuf_unfix_and_init (thread_p, pgptr);
		  goto exit_on_error;
		}

	      /* 
	       * Deallocate all user pages in this table page of this allocation
	       * set. The sectors are deallocated in batches by their contiguity
	       */
	      batch_firstid = NULL_PAGEID;
	      batch_ndealloc = 0;

	      for (; aid_ptr < outptr; aid_ptr++)
		{
		  if (*aid_ptr > NULL_PAGEID)
		    {
		      if (batch_ndealloc == 0)
			{
			  /* Start accumulating contiguous pages */
			  batch_firstid = *aid_ptr;
			  batch_ndealloc = 1;
			}
		      else
			{
			  /* Is page contiguous ? */
			  if (*aid_ptr == batch_firstid + batch_ndealloc)
			    {
			      /* contiguous */
			      batch_ndealloc++;
			    }
			  else
			    {
			      /* 
			       * This is not a contiguous page.
			       * Deallocate any previous pages and start
			       * accumulating contiguous pages again.
			       * We do not care if the page deallocation failed,
			       * since we are destroying the file.. Deallocate
			       * as much as we can.
			       */

			      if (file_type == FILE_TEMP && !pb_invalid_temp_called)
				{
				  pgbuf_invalidate_temporary_file (allocset->volid, batch_firstid, batch_ndealloc,
								   false);
				}

			      (void) disk_dealloc_page (thread_p, allocset->volid, batch_firstid, batch_ndealloc,
							page_type);
			      /* Start again */
			      batch_firstid = *aid_ptr;
			      batch_ndealloc = 1;
			    }
			}
		    }
		  else
		    {
		      assert (*aid_ptr == NULL_PAGEID || *aid_ptr == NULL_PAGEID_MARKED_DELETED);
		    }
		}

	      if (batch_ndealloc > 0)
		{
		  /* Deallocate any accumulated pages */

		  if (file_type == FILE_TEMP && !pb_invalid_temp_called)
		    {
		      pgbuf_invalidate_temporary_file (allocset->volid, batch_firstid, batch_ndealloc, false);
		    }

		  (void) disk_dealloc_page (thread_p, allocset->volid, batch_firstid, batch_ndealloc, page_type);
		}

	      /* Get next page in the allocation set */
	      if (VPID_EQ (&nxftb_vpid, &allocset->end_pages_vpid))
		{
		  VPID_SET_NULL (&nxftb_vpid);
		}
	      else
		{
		  ret = file_ftabvpid_next (fhdr, pgptr, &nxftb_vpid);
		  if (ret != NO_ERROR)
		    {
		      pgbuf_unfix_and_init (thread_p, pgptr);
		      goto exit_on_error;
		    }
		}
	      pgbuf_unfix_and_init (thread_p, pgptr);
	    }

	  /* Deallocate the sectors in this allocation set */

	  if (allocset->num_sects > 0)
	    {
	      nxftb_vpid = allocset->start_sects_vpid;
	      while (!VPID_ISNULL (&nxftb_vpid))
		{
		  pgptr = pgbuf_fix (thread_p, &nxftb_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
		  if (pgptr == NULL)
		    {
		      goto exit_on_error;
		    }

		  (void) pgbuf_check_page_ptype (thread_p, pgptr, PAGE_FTAB);

		  /* Calculate the starting offset and length of the page to check */
		  ret = file_find_limits (pgptr, allocset, &aid_ptr, &outptr, FILE_SECTOR_TABLE);
		  if (ret != NO_ERROR)
		    {
		      pgbuf_unfix_and_init (thread_p, pgptr);
		      goto exit_on_error;
		    }

		  /* 
		   * Deallocate all user sectors in this table page of this
		   * allocation set. The sectors are deallocated in batches by
		   * their contiguity
		   */

		  batch_firstid = NULL_SECTID;
		  batch_ndealloc = 0;

		  for (; aid_ptr < outptr; aid_ptr++)
		    {
		      if (*aid_ptr != NULL_SECTID)
			{
			  if (batch_ndealloc == 0)
			    {
			      /* Start accumulating contiguous sectors */
			      batch_firstid = *aid_ptr;
			      batch_ndealloc = 1;
			    }
			  else
			    {
			      /* Is sector contiguous ? */
			      if (*aid_ptr == batch_firstid + batch_ndealloc)
				{
				  /* contiguous */
				  batch_ndealloc++;
				}
			      else
				{
				  /* 
				   * This is not a contiguous sector.
				   * Deallocate any previous sectors and start
				   * accumulating contiguous sectors again.
				   * We do not care if the sector deallocation
				   * failed, since we are destroying the file..
				   * Deallocate as much as we can.
				   */
				  (void) disk_dealloc_sector (thread_p, allocset->volid, batch_firstid, batch_ndealloc);
				  /* Start again */
				  batch_firstid = *aid_ptr;
				  batch_ndealloc = 1;
				}
			    }
			}
		    }

		  if (batch_ndealloc > 0)
		    {
		      /* Deallocate any accumulated sectors */
		      (void) disk_dealloc_sector (thread_p, allocset->volid, batch_firstid, batch_ndealloc);
		    }

		  /* Get next page */
		  if (VPID_EQ (&nxftb_vpid, &allocset->end_sects_vpid))
		    {
		      VPID_SET_NULL (&nxftb_vpid);
		    }
		  else
		    {
		      ret = file_ftabvpid_next (fhdr, pgptr, &nxftb_vpid);
		      if (ret != NO_ERROR)
			{
			  pgbuf_unfix_and_init (thread_p, pgptr);
			  goto exit_on_error;
			}
		    }
		  pgbuf_unfix_and_init (thread_p, pgptr);
		}
	    }

	  /* Next allocation set */

	  if (VPID_ISNULL (&allocset->next_allocset_vpid))
	    {
	      VPID_SET_NULL (&allocset_vpid);
	      allocset_offset = -1;
	    }
	  else
	    {
	      if (VPID_EQ (&allocset_vpid, &allocset->next_allocset_vpid)
		  && allocset_offset == allocset->next_allocset_offset)
		{
		  ret = ER_FILE_FTB_LOOP;
		  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ret, 4, vfid->fileid,
			  fileio_get_volume_label (vfid->volid, PEEK), allocset->next_allocset_vpid.volid,
			  allocset->next_allocset_vpid.pageid);
		  VPID_SET_NULL (&allocset_vpid);
		  allocset_offset = -1;
		}
	      else
		{
		  allocset_vpid = allocset->next_allocset_vpid;
		  allocset_offset = allocset->next_allocset_offset;
		}
	    }
	  pgbuf_unfix_and_init (thread_p, allocset_pgptr);
	}

      num_user_pages = fhdr->num_user_pages;
      fhdr->num_user_pages = 0;

      addr.vfid = vfid;
      addr.pgptr = fhdr_pgptr;
      addr.offset = FILE_HEADER_OFFSET;
      undo_data = num_user_pages;
      redo_data = -num_user_pages;

      if (tdes->topops.type != LOG_TOPOPS_POSTPONE)
	{
	  fhdr->num_user_pages_mrkdelete += num_user_pages;

	  log_append_undoredo_data (thread_p, RVFL_FHDR_MARK_DELETED_PAGES, &addr, sizeof (undo_data),
				    sizeof (redo_data), &undo_data, &redo_data);

	  /* Add postpone to compress. */
	  postpone_data.deleted_npages = num_user_pages;
	  postpone_data.need_compaction = 0;

	  log_append_postpone (thread_p, RVFL_FHDR_DELETE_PAGES, &addr, sizeof (postpone_data), &postpone_data);
	}
      else
	{
	  log_append_undoredo_data (thread_p, RVFL_FHDR_UPDATE_NUM_USER_PAGES, &addr, sizeof (undo_data),
				    sizeof (redo_data), &undo_data, &redo_data);
	}

      pgbuf_set_dirty (thread_p, fhdr_pgptr, DONT_FREE);
    }

  /* 
   * Deallocate all file table pages except the file header page which is
   * deallocated at the very end. Try to deallocate the pages in batched by
   * their contiguity
   */

  nxftb_vpid.volid = vfid->volid;
  nxftb_vpid.pageid = vfid->fileid;

  batch_volid = NULL_VOLID;
  batch_firstid = NULL_PAGEID;
  batch_ndealloc = 0;

  while (!VPID_ISNULL (&nxftb_vpid))
    {
      pgptr = pgbuf_fix (thread_p, &nxftb_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
      if (pgptr == NULL)
	{
	  goto exit_on_error;
	}

      (void) pgbuf_check_page_ptype (thread_p, pgptr, PAGE_FTAB);

      /* Find next file table page */
      curftb_vpid = nxftb_vpid;
      ret = file_ftabvpid_next (fhdr, pgptr, &nxftb_vpid);
      if (ret != NO_ERROR)
	{
	  pgbuf_unfix_and_init (thread_p, pgptr);
	  goto exit_on_error;
	}
      pgbuf_unfix_and_init (thread_p, pgptr);

      /* Don't deallocate the file header page here */
      if (!(curftb_vpid.volid == vfid->volid && curftb_vpid.pageid == vfid->fileid))
	{
	  if (batch_ndealloc == 0)
	    {
	      /* Start accumulating contiguous pages */
	      batch_volid = curftb_vpid.volid;
	      batch_firstid = curftb_vpid.pageid;
	      batch_ndealloc = 1;
	    }
	  else
	    {
	      /* is this page contiguous to previous page ? */
	      if (curftb_vpid.pageid == (batch_firstid + batch_ndealloc) && curftb_vpid.volid == batch_volid)
		{
		  /* contiguous */
		  batch_ndealloc++;
		}
	      else
		{
		  /* 
		   * This is not a contiguous page.
		   * Deallocate any previous pages and start accumulating
		   * contiguous pages again. We do not care if the page
		   * deallocation failed, since we are destroying the file..
		   * Deallocate as much as we can.
		   */
		  (void) disk_dealloc_page (thread_p, batch_volid, batch_firstid, batch_ndealloc, page_type);
		  /* Start again */
		  batch_volid = curftb_vpid.volid;
		  batch_firstid = curftb_vpid.pageid;
		  batch_ndealloc = 1;
		}
	    }
	}
    }
  if (batch_ndealloc > 0)
    {
      (void) disk_dealloc_page (thread_p, batch_volid, batch_firstid, batch_ndealloc, page_type);
    }

  ret = file_descriptor_destroy_rest_pages (thread_p, fhdr);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  /* Now deallocate the file header page */

  if (file_type == FILE_TEMP && logtb_is_current_active (thread_p) == true)
    {
      /* 
       * This is a TEMPORARY FILE allocated on volumes with temporary
       * purposes. The file can be completely removed, including its header
       * page at this moment since there are not any log records associated
       * with it.
       */
      pgbuf_unfix_and_init (thread_p, fhdr_pgptr);

      /* remove header page */
      if (disk_dealloc_page (thread_p, vfid->volid, vfid->fileid, 1, page_type) != NO_ERROR)
	{
	  goto exit_on_error;
	}

      if (log_end_system_op (thread_p, LOG_RESULT_TOPOP_COMMIT) != TRAN_UNACTIVE_COMMITTED)
	{
	  goto exit_on_error;
	}

      (void) file_new_declare_as_old (thread_p, vfid);
    }
  else
    {
      /* Normal deletion, attach to outer nested level */
      pgbuf_unfix_and_init (thread_p, fhdr_pgptr);

      /* Now throw away the header page */
      if (disk_dealloc_page (thread_p, vfid->volid, vfid->fileid, 1, page_type) != NO_ERROR)
	{
	  goto exit_on_error;
	}
      if (tdes->topops.type != LOG_TOPOPS_POSTPONE)
	{
	  /* If this was postpone, it must be already unregistered. */
	  (void) file_tracker_unregister (thread_p, vfid);
	}

      /* The responsibility of the removal of the file is given to the outer nested level. */
      log_end_system_op (thread_p, LOG_RESULT_TOPOP_ATTACH_TO_OUTER);
      if (log_is_tran_in_system_op (thread_p) && logtb_is_current_active (thread_p) == true)
	{
	  /* 
	   * Note: we do not declare a new file as nolonger new at this level
	   *       since we do not have the authority to declare the deletion
	   *       of the file as committed. That is, we could have a partial
	   *       rolled back.
	   */
	  ;
	}
      else
	{
	  (void) file_new_declare_as_old (thread_p, vfid);
	}
    }

end:

  rv = thread_set_check_interrupt (thread_p, old_val);

  return ret;

exit_on_error:

  if (allocset_pgptr)
    {
      pgbuf_unfix_and_init (thread_p, allocset_pgptr);
    }

  /* ABORT THE TOP SYSTEM OPERATION. That is, the deletion of the file is aborted, all pages that were deallocated are
   * undone.. */
  (void) log_end_system_op (thread_p, LOG_RESULT_TOPOP_ABORT);

  if (fhdr_pgptr)
    {
      pgbuf_unfix_and_init (thread_p, fhdr_pgptr);
    }

  if (ret == NO_ERROR)
    {
      ret = ER_FAILED;
    }
  goto end;
}

/*
 * file_mark_as_deleted () - Mark a file as deleted
 *   return:
 *   thread_p(in):
 *   vfid(in): Complete file identifier
 *   class_oid(in): class OID
 *
 * Note: The given file is marked as deleted. None of its pages are
 *       deallocated. The deallocation of its pages is done when the file is
 *       destroyed (space reclaimed).
 */
int
file_mark_as_deleted (THREAD_ENTRY * thread_p, const VFID * vfid, const OID * class_oid)
{
  PAGE_PTR fhdr_pgptr = NULL;
  LOG_DATA_ADDR addr;
  VPID vpid;
  int deleted = 1;
  char buffer[OR_INT_SIZE + OR_OID_SIZE + MAX_ALIGNMENT];
  char *buffer_p = NULL;

  addr.vfid = vfid;

  /* 
   * Lock the file table header in exclusive mode. Unless, an error is found,
   * the page remains lock until the end of the transaction so that no other
   * transaction may access the destroyed file.
   */
  vpid.volid = vfid->volid;
  vpid.pageid = vfid->fileid;
  fhdr_pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (fhdr_pgptr == NULL)
    {
      return ER_FAILED;
    }

  (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);

  addr.pgptr = fhdr_pgptr;
  addr.offset = FILE_HEADER_OFFSET;

  buffer_p = PTR_ALIGN (buffer, MAX_ALIGNMENT);

  OR_PUT_INT (buffer_p, deleted);
  OR_PUT_OID (buffer_p + OR_INT_SIZE, class_oid);

  log_append_postpone (thread_p, RVFL_MARKED_DELETED, &addr, OR_INT_SIZE + OR_OID_SIZE, buffer_p);

  pgbuf_unfix_and_init (thread_p, fhdr_pgptr);

  return NO_ERROR;
}

/*
 * file_does_marked_as_deleted () - Find if the given file is marked as deleted
 *   return: true or false
 *   vfid(in): Complete file identifier
 */
bool
file_does_marked_as_deleted (THREAD_ENTRY * thread_p, const VFID * vfid)
{
  FILE_HEADER *fhdr;
  PAGE_PTR fhdr_pgptr = NULL;
  VPID vpid;
  bool deleted;

  /* 
   * Lock the file table header in shared mode. Unless, an error is found,
   * the page remains lock until the end of the transaction so that no other
   * transaction may access the destroyed file.
   */
  vpid.volid = vfid->volid;
  vpid.pageid = vfid->fileid;
  fhdr_pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (fhdr_pgptr == NULL)
    {
      return false;
    }

  (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);

  fhdr = (FILE_HEADER *) (fhdr_pgptr + FILE_HEADER_OFFSET);
  deleted = fhdr->ismark_as_deleted ? true : false;
  pgbuf_unfix_and_init (thread_p, fhdr_pgptr);

  return deleted;
}

/* TODO: STL::vector for file_Type_cache.entry */
/*
 * file_type_cache_check () - Find type of the given file if it has already been
 *                          cached
 *   return: file_type
 *   vfid(in): Complete file identifier
 */
static FILE_TYPE
file_type_cache_check (const VFID * vfid)
{
  FILE_TYPE file_type = FILE_UNKNOWN_TYPE;
  int i;
  int rv;

  rv = pthread_mutex_lock (&file_Type_cache_lock);

  for (i = 0; i < file_Type_cache.max; i++)
    {
      if (VFID_EQ (vfid, &file_Type_cache.entry[i].vfid))
	{
	  file_Type_cache.entry[i].timestamp = file_Type_cache.clock++;
	  file_type = file_Type_cache.entry[i].file_type;
	  break;
	}
    }

  pthread_mutex_unlock (&file_Type_cache_lock);

  return file_type;
}

/* TODO: STL::vector for file_Type_cache.entry */
/*
 * file_type_cache_add_entry () - Adds an entry to the cache
 *   return: NO_ERROR
 *   vfid(in): Complete file identifier
 *   type(in): File type
 *
 * Note: This may cause the eviction of a previous entry if the cache is full.
 */
static int
file_type_cache_add_entry (const VFID * vfid, FILE_TYPE type)
{
  int candidate;
  int rv;
  int ret = NO_ERROR;

  rv = pthread_mutex_lock (&file_Type_cache_lock);

  if (file_Type_cache.max < FILE_TYPE_CACHE_SIZE)
    {
      /* There's room at the inn, and we'll just add this to the end of the array of the entries. */
      candidate = file_Type_cache.max;
      file_Type_cache.max += 1;
    }
  else
    {
      /* The inn is full, and we have to evict someone.  Search for the least recently used entry and kick it out. */
      int i, timestamp;

      candidate = 0;
      timestamp = file_Type_cache.entry[0].timestamp;
      for (i = 1; i < FILE_TYPE_CACHE_SIZE; i++)
	{
	  if (file_Type_cache.entry[i].timestamp < timestamp)
	    {
	      candidate = i;
	      timestamp = file_Type_cache.entry[i].timestamp;
	    }
	}
    }

  file_Type_cache.entry[candidate].vfid = *vfid;
  file_Type_cache.entry[candidate].file_type = type;
  file_Type_cache.entry[candidate].timestamp = file_Type_cache.clock++;

  pthread_mutex_unlock (&file_Type_cache_lock);

  return ret;
}

/* TODO: STL::vector for file_Type_cache.entry */
/*
 * file_type_cache_entry_remove () - Removes the cache entry associated with the
 *                                 given vfid
 *   return: NO_ERROR
 *   vfid(in): Complete file identifier
 *
 * Note: Does so by "pulling down" the last entry in the cache array to
 *       overwrite the selected entry (if present, or if not already the last
 *       entry in the array).
 */
static int
file_type_cache_entry_remove (const VFID * vfid)
{
  int i;
  int rv;
  int ret = NO_ERROR;

  rv = pthread_mutex_lock (&file_Type_cache_lock);

  for (i = 0; i < file_Type_cache.max; i++)
    {
      if (VFID_EQ (vfid, &file_Type_cache.entry[i].vfid))
	{
	  file_Type_cache.max -= 1;
	  /* Don't bother pulling down the last entry in the array if it's the one we're removing. */
	  if (file_Type_cache.max > i)
	    {
	      file_Type_cache.entry[i] = file_Type_cache.entry[file_Type_cache.max];
	    }

	  break;
	}
    }

  pthread_mutex_unlock (&file_Type_cache_lock);

  return ret;
}

/*
 * file_typecache_clear () - Clear out the file type cache
 *   return: NO_ERROR
 *
 * Note: Ought to be private, but needs to be called at abort time (from
 *       log_abort_local) because it's possible to have files destroyed then
 *       without calling file_destroy, and so there's a possibility of leaving
 *       stale info in the cache unless we clobber all of it.  Could probably
 *       still be private if we had a way of registering anonymous callbacks
 *       with log_abort_local.
 */
int
file_typecache_clear (void)
{
  int rv;
  int ret = NO_ERROR;

  rv = pthread_mutex_lock (&file_Type_cache_lock);

  file_Type_cache.max = 0;
  file_Type_cache.clock = 0;

  pthread_mutex_unlock (&file_Type_cache_lock);

  return ret;
}

/*
 * file_get_type_internal () - helper function to retrieve file_type
 *   return: file_type
 *   vfid(in): Complete file identifier
 *   fhdr_pgptr(in): file header pgptr
 */
static FILE_TYPE
file_get_type_internal (THREAD_ENTRY * thread_p, const VFID * vfid, PAGE_PTR fhdr_pgptr)
{
  FILE_HEADER *fhdr;
  FILE_TYPE file_type;

  assert (fhdr_pgptr != NULL);

  (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);

  fhdr = (FILE_HEADER *) (fhdr_pgptr + FILE_HEADER_OFFSET);
  file_type = fhdr->type;

  if (file_type_cache_add_entry (vfid, file_type) != NO_ERROR)
    {
      file_type = FILE_UNKNOWN_TYPE;
    }

  return file_type;
}

/*
 * file_get_type () - Find type of the given file
 *   return: file_type
 *   vfid(in): Complete file identifier
 */
FILE_TYPE
file_get_type (THREAD_ENTRY * thread_p, const VFID * vfid)
{
  PAGE_PTR fhdr_pgptr = NULL;
  VPID vpid;
  FILE_TYPE file_type;

  assert (false);

  /* First check to see if this is something we've already looked at recently.  If so, it will save us having to
   * pgbuf_fix the header, which can reduce the pressure on the page buffer pool. */
  file_type = file_type_cache_check (vfid);
  if (file_type != FILE_UNKNOWN_TYPE)
    {
      return file_type;
    }

  vpid.volid = vfid->volid;
  vpid.pageid = vfid->fileid;

  fhdr_pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (fhdr_pgptr == NULL)
    {
      return FILE_UNKNOWN_TYPE;
    }

  file_type = file_get_type_internal (thread_p, vfid, fhdr_pgptr);

  pgbuf_unfix_and_init (thread_p, fhdr_pgptr);

  return file_type;
}

/*
 * file_get_type_by_fhdr_pgptr () - Find type of the given file by fhdr pgptr
 *   return: file_type
 *   vfid(in): Complete file identifier
 *   fhdr_pgtr(in): file hdr pgptr
 */
FILE_TYPE
file_get_type_by_fhdr_pgptr (THREAD_ENTRY * thread_p, const VFID * vfid, PAGE_PTR fhdr_pgptr)
{
  FILE_TYPE file_type;

  assert (false);
  assert (fhdr_pgptr != NULL);

  /* First check to see if this is something we've already looked at recently.  If so, it will save us having to
   * pgbuf_fix the header, which can reduce the pressure on the page buffer pool. */
  file_type = file_type_cache_check (vfid);
  if (file_type != FILE_UNKNOWN_TYPE)
    {
      return file_type;
    }

  file_type = file_get_type_internal (thread_p, vfid, fhdr_pgptr);

  return file_type;
}

/*
 * file_get_descriptor () - Get the file descriptor associated with the given file
 *   return: Actual size of file descriptor
 *   vfid(in): Complete file identifier
 *   area_des(out): The area where the file description is placed
 *   maxsize(in): Max size of file descriptor area
 *
 * Note: If the file descriptor does not fit in the given area, the needed
 *       size is returned as a negative value
 */
int
file_get_descriptor (THREAD_ENTRY * thread_p, const VFID * vfid, void *area_des, int maxsize)
{
  PAGE_PTR fhdr_pgptr = NULL;
  FILE_HEADER *fhdr;
  VPID vpid;

  vpid.volid = vfid->volid;
  vpid.pageid = vfid->fileid;

  fhdr_pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (fhdr_pgptr != NULL)
    {
      (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);

      fhdr = (FILE_HEADER *) (fhdr_pgptr + FILE_HEADER_OFFSET);
      maxsize = file_descriptor_get (thread_p, fhdr, area_des, maxsize);

      pgbuf_unfix_and_init (thread_p, fhdr_pgptr);
    }
  else
    {
      maxsize = 0;
    }

  return maxsize;
}

/*
 * file_dump_descriptor () - Dump the file descriptor associated with given file
 *   return: NO_ERROR
 *   vfid(in): Complete file identifier
 */
int
file_dump_descriptor (THREAD_ENTRY * thread_p, FILE * fp, const VFID * vfid)
{
  PAGE_PTR fhdr_pgptr = NULL;
  FILE_HEADER *fhdr;
  VPID vpid;
  int ret;

  vpid.volid = vfid->volid;
  vpid.pageid = vfid->fileid;

  fhdr_pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (fhdr_pgptr == NULL)
    {
      return ER_FAILED;
    }

  fhdr = (FILE_HEADER *) (fhdr_pgptr + FILE_HEADER_OFFSET);
  ret = file_descriptor_dump_internal (thread_p, fp, fhdr);

  pgbuf_unfix_and_init (thread_p, fhdr_pgptr);

  return ret;
}

/*
 * file_get_numpages () - Returns the number of allocated pages for the given file
 *   return: Number of pages or -1 in case of error
 *   vfid(in): Complete file identifier
 */
INT32
file_get_numpages (THREAD_ENTRY * thread_p, const VFID * vfid)
{
  PAGE_PTR fhdr_pgptr = NULL;
  FILE_HEADER *fhdr;
  VPID vpid;
  INT32 num_pgs = -1;

  assert (false);

  vpid.volid = vfid->volid;
  vpid.pageid = vfid->fileid;

  fhdr_pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (fhdr_pgptr)
    {
      (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);

      fhdr = (FILE_HEADER *) (fhdr_pgptr + FILE_HEADER_OFFSET);
      num_pgs = fhdr->num_user_pages;

      pgbuf_unfix_and_init (thread_p, fhdr_pgptr);
    }

  return num_pgs;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * file_get_numpages_overhead () - Find the number of overhead pages used for the
 *                           given file
 *   return: Number of overhead pages or -1 in case of error
 *   vfid(in): Complete file identifier
 */
INT32
file_get_numpages_overhead (THREAD_ENTRY * thread_p, const VFID * vfid)
{
  PAGE_PTR fhdr_pgptr = NULL;
  FILE_HEADER *fhdr;
  VPID vpid;
  INT32 num_pgs = -1;

  vpid.volid = vfid->volid;
  vpid.pageid = vfid->fileid;

  fhdr_pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (fhdr_pgptr != NULL)
    {
      (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);

      fhdr = (FILE_HEADER *) (fhdr_pgptr + FILE_HEADER_OFFSET);
      num_pgs = file_descriptor_find_num_rest_pages (thread_p, fhdr);
      if (num_pgs >= 0)
	{
	  num_pgs += fhdr->num_table_vpids;
	}

      pgbuf_unfix_and_init (thread_p, fhdr_pgptr);
    }

  return num_pgs;
}

/*
 * file_get_numpages_plus_numpages_overhead () - Find the number of overhead pages used
 *                                        for the given file
 *   return: Number of pages + Num of overhead pages or -1 in case of error
 *   vfid(in): Complete file identifier
 *   numpages(out): Number of pages
 *   overhead_numpages(out): Num of overhead pages
 */
INT32
file_get_numpages_plus_numpages_overhead (THREAD_ENTRY * thread_p, const VFID * vfid, INT32 * numpages,
					  INT32 * overhead_numpages)
{
  PAGE_PTR fhdr_pgptr = NULL;
  FILE_HEADER *fhdr;
  VPID vpid;

  *numpages = -1;
  *overhead_numpages = -1;

  vpid.volid = vfid->volid;
  vpid.pageid = vfid->fileid;

  fhdr_pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (fhdr_pgptr != NULL)
    {
      (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);

      fhdr = (FILE_HEADER *) (fhdr_pgptr + FILE_HEADER_OFFSET);
      *overhead_numpages = file_descriptor_find_num_rest_pages (thread_p, fhdr);
      if (*overhead_numpages >= 0)
	{
	  *overhead_numpages += fhdr->num_table_vpids;
	  *numpages = fhdr->num_user_pages;
	}

      pgbuf_unfix_and_init (thread_p, fhdr_pgptr);
    }

  if (*numpages < 0 || *overhead_numpages < 0)
    {
      return -1;
    }

  return *numpages + *overhead_numpages;
}
#endif

/*
 * file_get_first_alloc_vpid () - get the page identifier of the first allocated page of
 *                   the given file
 *   return: pageid or NULL_PAGEID
 *   vfid(in): Complete file identifier
 *   first_vpid(out):
 */
VPID *
file_get_first_alloc_vpid (THREAD_ENTRY * thread_p, const VFID * vfid, VPID * first_vpid)
{
  VPID vpid;
  FILE_HEADER *fhdr;
  PAGE_PTR fhdr_pgptr = NULL;

  vpid.volid = vfid->volid;
  vpid.pageid = vfid->fileid;

  fhdr_pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (fhdr_pgptr == NULL)
    {
      return NULL;
    }

  (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);

  fhdr = (FILE_HEADER *) (fhdr_pgptr + FILE_HEADER_OFFSET);

  assert (!VPID_ISNULL (&fhdr->first_alloc_vpid));
  VPID_COPY (first_vpid, &fhdr->first_alloc_vpid);

  pgbuf_unfix_and_init (thread_p, fhdr_pgptr);

  return first_vpid;
}

/*
 * file_alloc_iterator_init () -
 *   return: iterator or NULL
 *   vfid(in): Complete file identifier
 *   iter(out):
 */
FILE_ALLOC_ITERATOR *
file_alloc_iterator_init (THREAD_ENTRY * thread_p, VFID * vfid, FILE_ALLOC_ITERATOR * iter)
{
  INT32 npages = 0;
  VPID vpid;

  assert (vfid);
  assert (iter);

  npages = file_get_numpages (thread_p, vfid);

  iter->vfid = vfid;
  iter->current_index = -1;
  iter->num_pages = npages;
  VPID_SET_NULL (&iter->current_vpid);

  if (npages > 0)
    {
      if (file_get_first_alloc_vpid (thread_p, vfid, &vpid) != NULL)
	{
	  VPID_COPY (&iter->current_vpid, &vpid);
	  iter->current_index = 0;
	}
    }

  return iter->current_index == 0 ? iter : NULL;
}

/*
 * file_alloc_iterator_get_current_page () -
 *   return: pageid or NULL_PAGEID
 *   iter(in):
 *   vpid(out):
 */
VPID *
file_alloc_iterator_get_current_page (THREAD_ENTRY * thread_p, FILE_ALLOC_ITERATOR * iter, VPID * vpid)
{
  assert (iter);
  assert (vpid);

  VPID_SET_NULL (vpid);

  if (iter->current_index < 0 || VPID_ISNULL (&iter->current_vpid))
    {
      return NULL;
    }
  else
    {
      VPID_COPY (vpid, &iter->current_vpid);
    }

  return vpid;
}

/*
 * file_alloc_iterator_next () -
 *   return: iterator or NULL
 *   iter(in):
 */
FILE_ALLOC_ITERATOR *
file_alloc_iterator_next (THREAD_ENTRY * thread_p, FILE_ALLOC_ITERATOR * iter)
{
  VPID vpid;
  int num_pages;

  assert (iter);

  num_pages = file_get_numpages (thread_p, iter->vfid);

  if (iter->current_index < 0 || iter->num_pages != num_pages || iter->current_index >= iter->num_pages)
    {
      VPID_SET_NULL (&iter->current_vpid);
      return NULL;
    }
  else
    {
      iter->current_index++;

      if (iter->current_index == iter->num_pages)
	{
	  VPID_SET_NULL (&iter->current_vpid);
	  return NULL;
	}

      if (file_find_nthpages (thread_p, iter->vfid, &vpid, iter->current_index, 1) == 1)
	{
	  VPID_COPY (&iter->current_vpid, &vpid);
	}
      else
	{
	  VPID_SET_NULL (&iter->current_vpid);
	}
    }

  return iter;
}

/*
 * file_guess_numpages_overhead () - Guess the number of additonal overhead
 *                                     pages that are needed to store the given
 *                                     number of pages
 *   return: Number of overhead pages or -1 in case of error
 *   vfid(in): Complete file identifier OR NULL
 *   npages(in): Number of pages that we are guessing for allocation
 *
 * Note: This is only an approximation, the actual number of pages will depend
 *       upon on from what volume and sectors the pages are allocated. The
 *       function assumes that all pages can be allocated in a single volume.
 *       It is likely that the approximation will not be off more that one
 *       page.
 */
INT32
file_guess_numpages_overhead (THREAD_ENTRY * thread_p, const VFID * vfid, INT32 npages)
{
  PAGE_PTR fhdr_pgptr = NULL;
  FILE_HEADER *fhdr;
  FILE_ALLOCSET *allocset;
  PAGE_PTR allocset_pgptr = NULL;

  VPID vpid;
  int nsects;
  INT32 num_overhead_pages = 0;

  if (npages <= 0)
    {
      return 0;
    }

  nsects = CEIL_PTVDIV (npages, DISK_SECTOR_NPAGES);
  /* Don't use more than 10% of the page in sectors.... This is just a guess we don't quite know if the special sector
   * will be assigned to us. */
  if ((nsects * SSIZEOF (nsects)) > (DB_PAGESIZE / 10))
    {
      nsects = (DB_PAGESIZE / 10) / sizeof (nsects);
    }

  if (vfid == NULL)
    {
      /* A new file... Get the number of bytes that we need to store.. */
      num_overhead_pages = (((nsects + npages) * sizeof (INT32)) + sizeof (*fhdr));
    }
  else
    {
      num_overhead_pages = disk_get_total_numsectors (thread_p, vfid->volid);
      if (num_overhead_pages < nsects)
	{
	  nsects = num_overhead_pages;
	}

      /* Get the number of bytes that we need to store */
      num_overhead_pages = (nsects + npages) * sizeof (INT32);

      /* Find how many entries can be stored in the current allocation set Lock the file header page in shared mode and 
       * then fetch the page. */

      vpid.volid = vfid->volid;
      vpid.pageid = vfid->fileid;
      fhdr_pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
      if (fhdr_pgptr == NULL)
	{
	  return -1;
	}

      (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);

      fhdr = (FILE_HEADER *) (fhdr_pgptr + FILE_HEADER_OFFSET);

      allocset_pgptr =
	pgbuf_fix (thread_p, &fhdr->last_allocset_vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
      if (allocset_pgptr == NULL)
	{
	  pgbuf_unfix_and_init (thread_p, fhdr_pgptr);
	  return -1;
	}

      (void) pgbuf_check_page_ptype (thread_p, allocset_pgptr, PAGE_FTAB);

      allocset = (FILE_ALLOCSET *) ((char *) allocset_pgptr + fhdr->last_allocset_offset);
      num_overhead_pages = DB_PAGESIZE - allocset->end_pages_offset;
      if (num_overhead_pages < 0)
	{
	  num_overhead_pages = 0;
	}

      pgbuf_unfix_and_init (thread_p, allocset_pgptr);
      pgbuf_unfix_and_init (thread_p, fhdr_pgptr);
    }

  return CEIL_PTVDIV (num_overhead_pages, DB_PAGESIZE);
}

/*
 * file_allocset_nthpage () - Find nth allocated pageid of allocated set
 *   return: number of returned vpids or -1 on error
 *   fhdr(in): Pointer to file header
 *   allocset(in): pointer to a The first nth page desired in the allocation set
 *   num_desired_pages(in): The number of page identfiers that at desired
 *
 * Note: Find a set (num_desired_pages) of page identifiers starting at
 *       start_nthpage for the given allocation set. The number of returned
 *       identifiers is indicated in the return value. The numbering of pages
 *       start with zero.
 */
static int
file_allocset_nthpage (THREAD_ENTRY * thread_p, const FILE_HEADER * fhdr, const FILE_ALLOCSET * allocset,
		       VPID * nth_vpids, INT32 start_nthpage, INT32 num_desired_pages)
{
  PAGE_PTR pgptr = NULL;
  VPID vpid;
  INT32 *aid_ptr;		/* Pointer to a portion of allocated pageids table of given allocation set */
  INT32 *outptr;		/* Pointer to outside of portion of allocated pageids */
  int num_returned;		/* Number of returned identifiers */
  int count = 0;
  int ahead;

  /* Start looking at the portion of pageids. The table of pageids for this allocation set may be located at several
   * file table pages. */

  num_returned = 0;
  vpid = allocset->start_pages_vpid;
  while (!VPID_ISNULL (&vpid) && num_returned < num_desired_pages)
    {
      pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
      if (pgptr == NULL)
	{
	  return -1;
	}

      (void) pgbuf_check_page_ptype (thread_p, pgptr, PAGE_FTAB);

      /* Calculate the portion of pageids that we can look at current page */
      if (file_find_limits (pgptr, allocset, &aid_ptr, &outptr, FILE_PAGE_TABLE) != NO_ERROR)
	{
	  pgbuf_unfix_and_init (thread_p, pgptr);
	  return -1;
	}

      /* Find the nth page. If there is not any holes in this allocation set, we could address the desired page
       * directly. */
      if (allocset->num_holes == 0 && num_returned == 0)
	{
	  /* We can address the pageid directly */
	  if ((start_nthpage - count) > (outptr - aid_ptr))
	    {
	      /* 
	       * The start_nthpage is not in this portion of the set of pageids.
	       * Advance the pointers with the number of elements in this pointer
	       * array
	       */
	      ahead = CAST_BUFLEN (outptr - aid_ptr);
	      count += ahead;
	      aid_ptr += ahead;
	    }
	  else
	    {
	      /* The start_nthpage is in this portion of the set of pageids. Advance the pointers to the desired
	       * element */
	      ahead = start_nthpage - count;
	      count += ahead;
	      aid_ptr += ahead;
	    }
	}

      for (; num_returned < num_desired_pages && aid_ptr < outptr; aid_ptr++)
	{
	  if (*aid_ptr > NULL_PAGEID)
	    {
	      if (count >= start_nthpage)
		{
		  nth_vpids[num_returned].volid = allocset->volid;
		  nth_vpids[num_returned].pageid = *aid_ptr;
		  num_returned += 1;
		}
	      count++;
	    }
	  else
	    {
	      assert (*aid_ptr == NULL_PAGEID || *aid_ptr == NULL_PAGEID_MARKED_DELETED);
	    }
	}

      /* Get next page */

      if (num_returned == num_desired_pages || VPID_EQ (&vpid, &allocset->end_pages_vpid))
	{
	  VPID_SET_NULL (&vpid);
	}
      else
	{
	  if (file_ftabvpid_next (fhdr, pgptr, &vpid) != NO_ERROR)
	    {
	      pgbuf_unfix_and_init (thread_p, pgptr);
	      return -1;
	    }
	}
      pgbuf_unfix_and_init (thread_p, pgptr);
    }

  return num_returned;
}

/*
 * file_allocset_look_for_last_page () - Find the last page of the last allocation set
 *   return: last_vpid or NULL when there is not a page in allocation set
 *   fhdr(in): Pointer to file header
 *   last_vpid(out): last allocated page identifier
 */
static VPID *
file_allocset_look_for_last_page (THREAD_ENTRY * thread_p, const FILE_HEADER * fhdr, VPID * last_vpid)
{
  FILE_ALLOCSET *allocset;
  PAGE_PTR allocset_pgptr = NULL;

  allocset_pgptr =
    pgbuf_fix (thread_p, &fhdr->last_allocset_vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (allocset_pgptr == NULL)
    {
      VPID_SET_NULL (last_vpid);
      return NULL;
    }

  (void) pgbuf_check_page_ptype (thread_p, allocset_pgptr, PAGE_FTAB);

  allocset = (FILE_ALLOCSET *) ((char *) allocset_pgptr + fhdr->last_allocset_offset);
  if (allocset->num_pages <= 0
      || file_allocset_nthpage (thread_p, fhdr, allocset, last_vpid, allocset->num_pages - 1, 1) != 1)
    {
      VPID_SET_NULL (last_vpid);
      pgbuf_unfix_and_init (thread_p, allocset_pgptr);
      return NULL;
    }

  pgbuf_unfix_and_init (thread_p, allocset_pgptr);
  return last_vpid;
}

/*
 * file_find_nthpages () - Find volume page identifier of nth page of given file
 *   return: number of returned vpids, or -1 on error
 *   vfid(in): Complete file identifier
 *   nth_vpids(out): Array of of at least num_desired_pages VPIDs
 *   start_nthpage(in): The first desired nth page
 *   num_desired_pages(in): The number of page identfiers that at desired
 *
 * Note: Find a set, num_desired_pages, of volume-page identifiers starting at
 *       the start_nthpage of the given file. The pages are ordered by their
 *       allocation. For example, the 5th page is the 5th allocated page if no
 *       pages have been deallocated. If page does not exist, NULL is returned.
 *       The numbering of pages start with zero.
 */
int
file_find_nthpages (THREAD_ENTRY * thread_p, const VFID * vfid, VPID * nth_vpids, INT32 start_nthpage,
		    INT32 num_desired_pages)
{
  FILE_HEADER *fhdr;
  FILE_ALLOCSET *allocset;
  PAGE_PTR fhdr_pgptr = NULL;
  PAGE_PTR allocset_pgptr = NULL;
  INT16 allocset_offset;
  VPID allocset_vpid;
  int num_returned;		/* Number of returned identifiers at each allocation set */
  int total_returned;		/* Number of returned identifiers */
  int count = 0;

  allocset_vpid.volid = vfid->volid;
  allocset_vpid.pageid = vfid->fileid;

  assert (false);

  fhdr_pgptr = pgbuf_fix (thread_p, &allocset_vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (fhdr_pgptr == NULL)
    {
      VPID_SET_NULL (nth_vpids);
      goto exit_on_error;
    }

  (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);

  fhdr = (FILE_HEADER *) (fhdr_pgptr + FILE_HEADER_OFFSET);

  if (start_nthpage < 0 || start_nthpage > fhdr->num_user_pages - 1)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FILE_NTH_FPAGE_OUT_OF_RANGE, 4, start_nthpage, vfid->fileid,
	      fileio_get_volume_label (vfid->volid, PEEK), fhdr->num_user_pages);
      VPID_SET_NULL (nth_vpids);
      goto exit_on_error;
    }

  if (start_nthpage == fhdr->num_user_pages - 1 && num_desired_pages == 1)
    {
      /* 
       * Looking for last page. Check if the last allocation set contains
       * the last page. If it does, the last allocation set does not hold any
       * page and we need to execute the sequential scan to find the desired
       * page
       */
      if (file_allocset_look_for_last_page (thread_p, fhdr, nth_vpids) != NULL)
	{
	  /* The last page has been found */
	  pgbuf_unfix_and_init (thread_p, fhdr_pgptr);
	  return 1;
	}
    }

  /* Start a sequential scan to each of the allocation sets until the allocation set that contains the start_nthpage is 
   * found */

  allocset_offset = offsetof (FILE_HEADER, allocset);
  total_returned = 0;
  while (!VPID_ISNULL (&allocset_vpid) && total_returned < num_desired_pages)
    {
      /* Fetch the page for the allocation set description */
      allocset_pgptr = pgbuf_fix (thread_p, &allocset_vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
      if (allocset_pgptr == NULL)
	{
	  VPID_SET_NULL (nth_vpids);
	  goto exit_on_error;
	}

      (void) pgbuf_check_page_ptype (thread_p, allocset_pgptr, PAGE_FTAB);

      /* Get the allocation set */
      allocset = (FILE_ALLOCSET *) ((char *) allocset_pgptr + allocset_offset);
      if ((count + allocset->num_pages) <= start_nthpage)
	{
	  /* The desired start_nthpage is not in this set, advance to the next allocation set */
	  count += allocset->num_pages;
	}
      else
	{
	  /* The desired start_nthpage is in this set */
	  if (total_returned == 0)
	    {
	      num_returned = file_allocset_nthpage (thread_p, fhdr, allocset, nth_vpids, start_nthpage - count,
						    num_desired_pages);
	      count += start_nthpage - count + num_returned;
	    }
	  else
	    {
	      num_returned = file_allocset_nthpage (thread_p, fhdr, allocset, &nth_vpids[total_returned], 0,
						    (num_desired_pages - total_returned));
	      count += num_returned;
	    }
	  if (num_returned < 0)
	    {
	      total_returned = num_returned;
	      pgbuf_unfix_and_init (thread_p, allocset_pgptr);
	      VPID_SET_NULL (nth_vpids);
	      break;
	    }
	  total_returned += num_returned;
	}

      if (total_returned >= num_desired_pages || VPID_ISNULL (&allocset->next_allocset_vpid))
	{
	  VPID_SET_NULL (&allocset_vpid);
	  allocset_offset = -1;
	}
      else
	{
	  /* Next allocation set */
	  if (VPID_EQ (&allocset_vpid, &allocset->next_allocset_vpid)
	      && allocset_offset == allocset->next_allocset_offset)
	    {
	      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_FILE_FTB_LOOP, 4, vfid->fileid,
		      fileio_get_volume_label (vfid->volid, PEEK), allocset->next_allocset_vpid.volid,
		      allocset->next_allocset_vpid.pageid);
	      VPID_SET_NULL (&allocset_vpid);
	      allocset_offset = -1;
	    }
	  else
	    {
	      allocset_vpid = allocset->next_allocset_vpid;
	      allocset_offset = allocset->next_allocset_offset;
	    }
	}
      pgbuf_unfix_and_init (thread_p, allocset_pgptr);
    }

  pgbuf_unfix_and_init (thread_p, fhdr_pgptr);

  return total_returned;

exit_on_error:

  if (allocset_pgptr)
    {
      pgbuf_unfix_and_init (thread_p, allocset_pgptr);
    }

  if (fhdr_pgptr)
    {
      pgbuf_unfix_and_init (thread_p, fhdr_pgptr);
    }

  return -1;
}

/*
 * file_find_last_page () - Find the page identifier of the last allocated page of
 *                   the given file
 *   return: pageid or NULL_PAGEID
 *   vfid(in): Complete file identifier
 *   last_vpid(in):
 */
VPID *
file_find_last_page (THREAD_ENTRY * thread_p, const VFID * vfid, VPID * last_vpid)
{
  VPID vpid;
  FILE_HEADER *fhdr;
  PAGE_PTR fhdr_pgptr = NULL;

  vpid.volid = vfid->volid;
  vpid.pageid = vfid->fileid;

  fhdr_pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (fhdr_pgptr == NULL)
    {
      return NULL;
    }

  (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);

  fhdr = (FILE_HEADER *) (fhdr_pgptr + FILE_HEADER_OFFSET);

  if (fhdr->num_user_pages > 0)
    {
      /* Get it from last allocation set. If the last allocation set does not have any allocated pages, we need to
       * perform the sequential scan. */
      if (file_allocset_look_for_last_page (thread_p, fhdr, last_vpid) == NULL)
	{
	  /* it is possible that the last allocation set does not contain any page, use the normal
	   * file_find_nthpages...sequential scan */
	  if (file_find_nthpages (thread_p, vfid, last_vpid, fhdr->num_user_pages - 1, 1) != 1)
	    {
	      VPID_SET_NULL (last_vpid);
	      pgbuf_unfix_and_init (thread_p, fhdr_pgptr);
	      return NULL;
	    }
	}
    }
  else
    {
      VPID_SET_NULL (last_vpid);
      pgbuf_unfix_and_init (thread_p, fhdr_pgptr);
      return NULL;
    }

  pgbuf_unfix_and_init (thread_p, fhdr_pgptr);
  return last_vpid;
}

/*
 * file_isvalid_page_partof () - Find if the given page is a valid page for the given
 *                       file
 *   return: either: DISK_INVALID, DISK_VALID, DISK_ERROR
 *   vpid(in): The page identifier
 *   vfid(in): The file identifier
 *
 * Note: That is, check that the page is valid and that if it belongs to given
 *       file. The function assumes that the file vfid is valid.
 */
DISK_ISVALID
file_isvalid_page_partof (THREAD_ENTRY * thread_p, const VPID * vpid, const VFID * vfid)
{
  FILE_ALLOCSET *allocset;
  VPID allocset_vpid;
  INT16 allocset_offset;
  PAGE_PTR allocset_pgptr = NULL;
  PAGE_PTR fhdr_pgptr = NULL;
  DISK_ISVALID isfound = DISK_INVALID;
  DISK_ISVALID valid;

  /* todo: update for new design */
  if (true)
    {
      return DISK_VALID;
    }

  if (VPID_ISNULL (vpid))
    {
      return DISK_INVALID;
    }

  valid = disk_is_page_sector_reserved (thread_p, vpid->volid, vpid->pageid);
  if (valid != DISK_VALID)
    {
      if (valid != DISK_ERROR)
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_PB_BAD_PAGEID, 2, vpid->pageid,
		  fileio_get_volume_label (vpid->volid, PEEK));
	}
      return valid;
    }

  allocset_vpid.volid = vfid->volid;
  allocset_vpid.pageid = vfid->fileid;

  fhdr_pgptr = pgbuf_fix (thread_p, &allocset_vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (fhdr_pgptr == NULL)
    {
      return DISK_ERROR;
    }

  (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);

  allocset_offset = offsetof (FILE_HEADER, allocset);

  /* Go over each allocation set until the file is found */
  while (!VPID_ISNULL (&allocset_vpid))
    {
      /* Fetch the file for the allocation set description */
      allocset_pgptr = pgbuf_fix (thread_p, &allocset_vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
      if (allocset_pgptr == NULL)
	{
	  pgbuf_unfix_and_init (thread_p, fhdr_pgptr);
	  return DISK_ERROR;
	}

      (void) pgbuf_check_page_ptype (thread_p, allocset_pgptr, PAGE_FTAB);

      /* Get the allocation set */
      allocset = (FILE_ALLOCSET *) ((char *) allocset_pgptr + allocset_offset);
      if (allocset->volid == vpid->volid)
	{
	  /* The page may be located in this set */
	  isfound =
	    file_allocset_find_page (thread_p, fhdr_pgptr, allocset_pgptr, allocset_offset, vpid->pageid, NULL, NULL);
	}
      if (isfound == DISK_ERROR)
	{
	  pgbuf_unfix_and_init (thread_p, allocset_pgptr);
	  pgbuf_unfix_and_init (thread_p, fhdr_pgptr);
	  return DISK_ERROR;
	}
      else if (isfound == DISK_INVALID)
	{
	  /* We did not find it in the current allocation set. Get the next allocation set */
	  if (VPID_ISNULL (&allocset->next_allocset_vpid))
	    {
	      VPID_SET_NULL (&allocset_vpid);
	      allocset_offset = -1;
	    }
	  else
	    {
	      if (VPID_EQ (&allocset_vpid, &allocset->next_allocset_vpid)
		  && allocset_offset == allocset->next_allocset_offset)
		{
		  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_FILE_FTB_LOOP, 4, vfid->fileid,
			  fileio_get_volume_label (vfid->volid, PEEK), allocset_vpid.volid, allocset_vpid.pageid);
		  VPID_SET_NULL (&allocset_vpid);
		  allocset_offset = -1;
		}
	      else
		{
		  allocset_vpid = allocset->next_allocset_vpid;
		  allocset_offset = allocset->next_allocset_offset;
		}
	    }
	}
      else
	{
	  VPID_SET_NULL (&allocset_vpid);
	  allocset_offset = -1;
	}
      pgbuf_unfix_and_init (thread_p, allocset_pgptr);
    }

  pgbuf_unfix_and_init (thread_p, fhdr_pgptr);

  if (isfound == DISK_VALID)
    {
      return DISK_VALID;
    }
  else
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FILE_PAGE_ISNOT_PARTOF, 6, vpid->volid, vpid->pageid,
	      fileio_get_volume_label (vpid->volid, PEEK), vfid->volid, vfid->fileid,
	      fileio_get_volume_label (vfid->volid, PEEK));
      return DISK_INVALID;
    }
}

/*
 * file_allocset_alloc_sector () - Allocate a new sector for the given
 *                               allocation set
 *   return: Sector identifier
 *   fhdr_pgptr(in): Page pointer to file header
 *   allocset_pgptr(out): Pointer to a page where allocation set is located
 *   allocset_offset(in): Location in allocset_pgptr where allocatio set is
 *                        located
 *   exp_npages(in): Expected pages that sector will have
 *
 * Note: The sector identifier is stored onto the sector table of the given
 *       allocation set.
 *
 *       Allocation of pages and sector is only done for the last allocation
 *       set. This is a consequence of the ordering of pages
 */
static INT32
file_allocset_alloc_sector (THREAD_ENTRY * thread_p, PAGE_PTR fhdr_pgptr, PAGE_PTR allocset_pgptr,
			    INT16 allocset_offset, int exp_npages)
{
  FILE_HEADER *fhdr;
  FILE_ALLOCSET *allocset;
  INT32 *aid_ptr;		/* Pointer to portion of allocated sector identifiers */
  INT32 sectid;			/* Newly allocated sector identifier */
  VPID vpid;
  LOG_DATA_ADDR addr;
  FILE_RECV_ALLOCSET_SECTOR recv_undo, recv_redo;	/* Recovery stuff */
  bool special = false;

  (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);
  (void) pgbuf_check_page_ptype (thread_p, allocset_pgptr, PAGE_FTAB);

  /* Allocation of pages and sectors is done only from the last allocation set. This is done since the VPID must be
   * stored in the order of allocation. */

  /* Get the file header and the allocation set */
  fhdr = (FILE_HEADER *) (fhdr_pgptr + FILE_HEADER_OFFSET);

  addr.vfid = &fhdr->vfid;
  addr.pgptr = NULL;
  addr.offset = -1;

  if (fhdr->type == FILE_TEMP)
    {
      special = true;
    }

  allocset = (FILE_ALLOCSET *) ((char *) allocset_pgptr + allocset_offset);

  /* If there is not space to store a new sector identifier at the end of the sector table, the sector table for this
   * allocation set is expanded */

  if (allocset->end_sects_offset >= allocset->start_pages_offset
      && VPID_EQ (&allocset->end_sects_vpid, &allocset->start_pages_vpid))
    {
      /* Expand the sector table */
      if (file_allocset_expand_sector (thread_p, fhdr_pgptr, allocset_pgptr, allocset_offset) == NULL)
	{
	  if (fhdr->num_user_pages_mrkdelete > 0)
	    {
	      return DISK_SECTOR_WITH_ALL_PAGES;
	    }
	  else
	    {
	      return NULL_SECTID;
	    }
	}
    }

  /* Find the end of the table of sector identifiers */
  while (!VPID_ISNULL (&allocset->end_sects_vpid))
    {
      addr.pgptr =
	pgbuf_fix (thread_p, &allocset->end_sects_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
      if (addr.pgptr == NULL)
	{
	  return NULL_SECTID;
	}

      (void) pgbuf_check_page_ptype (thread_p, addr.pgptr, PAGE_FTAB);

      if (allocset->end_sects_offset >= DB_PAGESIZE)
	{
	  /* 
	   * In general this cannot happen, unless the advance of the sector table
	   * to the next FTB was not set to next FTB for some type of error.
	   * (e.g.., interrupt)
	   */
	  if (file_ftabvpid_next (fhdr, addr.pgptr, &vpid) != NO_ERROR)
	    {
	      pgbuf_unfix_and_init (thread_p, addr.pgptr);
	      return NULL_SECTID;
	    }

	  pgbuf_unfix_and_init (thread_p, addr.pgptr);

	  if (!VPID_ISNULL (&vpid))
	    {
	      allocset->end_sects_vpid = vpid;
	      allocset->end_sects_offset = sizeof (FILE_FTAB_CHAIN);
	    }
	  else
	    {
	      /* For now, let us allow to continue the allocation using the special sector. */
	      return DISK_SECTOR_WITH_ALL_PAGES;
	    }
	}
      else
	{
	  break;
	}
    }

  if (addr.pgptr == NULL)
    {
      return NULL_SECTID;
    }

  /* Allocate the sector and store its identifier onto the table of allocated sectors ids for the current allocation
   * set. */

  sectid =
    ((special == true) ? disk_alloc_special_sector () : disk_alloc_sector (thread_p, allocset->volid, 1, exp_npages));

  CAST_TO_SECTARRAY (aid_ptr, addr.pgptr, allocset->end_sects_offset);

  /* Log the change to be applied and then update the sector table, and the allocation set */

  addr.offset = allocset->end_sects_offset;
  log_append_undoredo_data (thread_p, RVFL_IDSTABLE, &addr, sizeof (*aid_ptr), sizeof (*aid_ptr), aid_ptr, &sectid);

  /* Now update the sector table */
  assert (sectid > NULL_PAGEID);
  *aid_ptr = sectid;

  /* Save the allocation set undo recovery information. THEN, update the allocation set */

  /* We will need undo */
  recv_undo.num_sects = allocset->num_sects;
  recv_undo.curr_sectid = allocset->curr_sectid;
  recv_undo.end_sects_offset = allocset->end_sects_offset;
  recv_undo.end_sects_vpid = allocset->end_sects_vpid;

  allocset->num_sects++;
  allocset->curr_sectid = sectid;
  allocset->end_sects_offset += sizeof (sectid);
  if (allocset->end_sects_offset >= DB_PAGESIZE)
    {
      VPID_SET_NULL (&vpid);

      /* 
       * The sector table will continue at next page of the file table.
       * We expect addr.pgptr to be currently pointing to the last page
       * of this sector. If an ftb expansion succeeds, yet a second
       * next_ftbvpid still returns NULL, then something is wrong with
       * the ftb structure.
       */
      if (file_ftabvpid_next (fhdr, addr.pgptr, &vpid) != NO_ERROR)
	{
	  pgbuf_unfix_and_init (thread_p, addr.pgptr);
	  return NULL_SECTID;
	}

      if (VPID_ISNULL (&vpid))
	{
	  if (file_expand_ftab (thread_p, fhdr_pgptr) == NULL
	      || file_ftabvpid_next (fhdr, addr.pgptr, &vpid) != NO_ERROR || VPID_ISNULL (&vpid))
	    {
	      pgbuf_unfix_and_init (thread_p, addr.pgptr);
	      return NULL_SECTID;
	    }
	}

      allocset->end_sects_vpid = vpid;
      allocset->end_sects_offset = sizeof (FILE_FTAB_CHAIN);
    }

  pgbuf_set_dirty (thread_p, addr.pgptr, FREE);
  addr.pgptr = NULL;

  /* Log the redo changes to the header file and set the header file dirty */
  recv_redo.num_sects = allocset->num_sects;
  recv_redo.curr_sectid = allocset->curr_sectid;
  recv_redo.end_sects_offset = allocset->end_sects_offset;
  recv_redo.end_sects_vpid = allocset->end_sects_vpid;

  addr.pgptr = allocset_pgptr;
  addr.offset = allocset_offset;

  log_append_undoredo_data (thread_p, RVFL_ALLOCSET_SECT, &addr, sizeof (recv_undo), sizeof (recv_redo), &recv_undo,
			    &recv_redo);
  pgbuf_set_dirty (thread_p, allocset_pgptr, DONT_FREE);

  return sectid;
}

/* TODO: list for file_ftab_chain */
/*
 * file_allocset_expand_sector () - Expand the sector table by at least
 *                                   FILE_NUM_EMPTY_SECTS
 *   return: fhdr_pgptr on success and NULL on failure
 *   fhdr_pgptr(in): Page pointer to file header
 *   allocset_pgptr(out): Pointer to a page where allocation set is located
 *   allocset_offset(in): Location in allocset_pgptr where allocatio set is
 *                        located
 *
 * Note: if the allocation set area is very long (several pages), this
 *       operation is very expensive since everything is moved by
 *       FILE_NUM_EMPTY_SECTS.
 */
static PAGE_PTR
file_allocset_expand_sector (THREAD_ENTRY * thread_p, PAGE_PTR fhdr_pgptr, PAGE_PTR allocset_pgptr,
			     INT16 allocset_offset)
{
  FILE_HEADER *fhdr;
  FILE_ALLOCSET *allocset;
  PAGE_PTR to_pgptr = NULL;
  PAGE_PTR from_pgptr = NULL;
  INT32 *to_aid_ptr;		/* Pointer to portion of pageid table on to-page */
  INT32 *from_aid_ptr;		/* Pointer to portion of pageid table on from-page */
  INT32 *to_outptr;		/* Out of portion of pageid table on to-page */
  INT32 *from_outptr;		/* Out of portion of pageid table on from-page */
  FILE_FTAB_CHAIN *chain;	/* Structure for linking ftable pages */
  VPID to_vpid;
  VPID from_vpid;
  LOG_DATA_ADDR addr;
  INT32 nsects;			/* Number of sector spaces */
  int length;
  FILE_RECV_EXPAND_SECTOR recv;	/* Recovery information */

  (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);
  (void) pgbuf_check_page_ptype (thread_p, allocset_pgptr, PAGE_FTAB);

  /* 
   * An expansion of a sector table can be done only for the last allocation
   * set. This is a consequence since allocations are done at the end due to
   * the ordering of pages.
   */

  /* Get the file header and the allocation set */
  fhdr = (FILE_HEADER *) (fhdr_pgptr + FILE_HEADER_OFFSET);

  addr.vfid = &fhdr->vfid;


  if (fhdr->num_user_pages_mrkdelete > 0)
    {
      /* Cannot move the page table at this moment */
      return NULL;
    }

  allocset = (FILE_ALLOCSET *) ((char *) allocset_pgptr + allocset_offset);

  if ((((FILE_NUM_EMPTY_SECTS * SSIZEOF (to_vpid.pageid)) - (allocset->num_holes * SSIZEOF (to_vpid.pageid)) +
	allocset->end_pages_offset) >= DB_PAGESIZE) && VPID_EQ (&allocset->end_pages_vpid, &fhdr->last_table_vpid))
    {
      if (file_expand_ftab (thread_p, fhdr_pgptr) == NULL)
	{
	  return NULL;		/* It failed */
	}
    }

  /* 
   * Log the values of the allocation set that are going to be changes.
   * This is done for UNDO purposes. This is needed since if there is a
   * failure in the middle of a shift, we need to recover from this shifty.
   * Otherwise, the page and or sector table may remain corrupted
   */

  recv.start_pages_vpid = allocset->start_pages_vpid;
  recv.end_pages_vpid = allocset->end_pages_vpid;
  recv.start_pages_offset = allocset->start_pages_offset;
  recv.end_pages_offset = allocset->end_pages_offset;
  recv.num_holes = allocset->num_holes;

  addr.pgptr = allocset_pgptr;
  addr.offset = allocset_offset;
  log_append_undo_data (thread_p, RVFL_ALLOCSET_PAGETB_ADDRESS, &addr, sizeof (recv), &recv);

  /* Find the last page where the data will be moved from */
  from_vpid = allocset->end_pages_vpid;
  from_pgptr = pgbuf_fix (thread_p, &from_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (from_pgptr == NULL)
    {
      return NULL;
    }

  (void) pgbuf_check_page_ptype (thread_p, from_pgptr, PAGE_FTAB);

  /* 
   * Calculate the starting offset and length of allocation area in from
   * page. Note that we move from the right.. so from_outptr is to the
   * left..that is the beginning of portion...
   */
  if (file_find_limits (from_pgptr, allocset, &from_outptr, &from_aid_ptr, FILE_PAGE_TABLE) != NO_ERROR)
    {
      pgbuf_unfix_and_init (thread_p, from_pgptr);
      return NULL;
    }

  (void) pgbuf_check_page_ptype (thread_p, from_pgptr, PAGE_FTAB);

  /* Find the page where data will moved to */
  if (((FILE_NUM_EMPTY_SECTS * SSIZEOF (to_vpid.pageid)) - (allocset->num_holes * SSIZEOF (to_vpid.pageid)) +
       allocset->end_pages_offset) < DB_PAGESIZE)
    {
      /* Same page as from-page. Fetch the page anyhow, to avoid complicated code (i.e., freeing pages) */
      to_vpid = from_vpid;
      to_pgptr = pgbuf_fix (thread_p, &to_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
      if (to_pgptr == NULL)
	{
	  pgbuf_unfix_and_init (thread_p, from_pgptr);
	  return NULL;
	}

      (void) pgbuf_check_page_ptype (thread_p, to_pgptr, PAGE_FTAB);

      /* How many sector elements are needed ? */
      nsects = FILE_NUM_EMPTY_SECTS - allocset->num_holes;
      if (nsects < 0)
	{
	  nsects = 0;
	}

      /* Fix the offset which indicate the end of the pageids */
      allocset->end_pages_offset += (INT16) (nsects * sizeof (to_vpid.pageid));
    }
  else
    {
      /* to-page is the next page pointed by from-page */
      if (file_ftabvpid_next (fhdr, from_pgptr, &to_vpid) != NO_ERROR)
	{
	  pgbuf_unfix_and_init (thread_p, from_pgptr);
	  return NULL;
	}

      to_pgptr = pgbuf_fix (thread_p, &to_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
      if (to_pgptr == NULL)
	{
	  pgbuf_unfix_and_init (thread_p, from_pgptr);
	  return NULL;
	}

      (void) pgbuf_check_page_ptype (thread_p, to_pgptr, PAGE_FTAB);

      /* Must take in consideration pointers of linked list and empty spaces (i.e., NULL_PAGEIDs) of from-page */

      /* How many holes area needed ? */
      nsects = FILE_NUM_EMPTY_SECTS - allocset->num_holes;
      if (nsects < 0)
	{
	  nsects = 0;
	}

      /* Fix the offset of the end of the pageid array */
      allocset->end_pages_vpid = to_vpid;
      allocset->end_pages_offset = (INT16) ((nsects * sizeof (to_vpid.pageid)) + sizeof (*chain));
    }

  /* 
   * Now start shifting.
   * Note that we are shifting from the right.. so from_outptr is to the left,
   * that is the beginning of portion...
   */

  if (file_find_limits (to_pgptr, allocset, &to_outptr, &to_aid_ptr, FILE_PAGE_TABLE) != NO_ERROR)
    {
      pgbuf_unfix_and_init (thread_p, from_pgptr);
      pgbuf_unfix_and_init (thread_p, to_pgptr);
      return NULL;
    }

  /* Before we move anything to the to_page, we need to log whatever is there just in case of a crash.. we will need to 
   * recover the shift... */

  length = 0;
  addr.pgptr = to_pgptr;
  addr.offset = CAST_BUFLEN ((char *) to_outptr - (char *) to_pgptr);
  log_append_undo_data (thread_p, RVFL_IDSTABLE, &addr, CAST_BUFLEN ((char *) to_aid_ptr - (char *) to_outptr),
			to_outptr);

  while (!(VPID_EQ (&from_vpid, &allocset->end_sects_vpid) && from_outptr >= from_aid_ptr))
    {
      /* Boundary condition on from-page ? */
      if (from_outptr >= from_aid_ptr)
	{
#ifdef FILE_DEBUG
	  if (from_outptr > from_aid_ptr)
	    {
	      er_log_debug (ARG_FILE_LINE, "file_allocset_expand_sector: *** Boundary condition system error ***\n");
	    }
#endif /* FILE_DEBUG */
	  /* Use the linked list to find previous page */
	  chain = (FILE_FTAB_CHAIN *) (from_pgptr + FILE_FTAB_CHAIN_OFFSET);
	  from_vpid = chain->prev_ftbvpid;
	  pgbuf_unfix_and_init (thread_p, from_pgptr);

	  from_pgptr = pgbuf_fix (thread_p, &from_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
	  if (from_pgptr == NULL)
	    {
	      pgbuf_unfix_and_init (thread_p, to_pgptr);
	      return NULL;
	    }

	  (void) pgbuf_check_page_ptype (thread_p, from_pgptr, PAGE_FTAB);

	  /* 
	   * Calculate the starting offset and length of allocation area in from
	   * page.
	   * Note that we are shifting from the right.. so from_outptr is to the
	   * left, that is the beginning of portion...
	   */
	  if (file_find_limits (from_pgptr, allocset, &from_outptr, &from_aid_ptr, FILE_PAGE_TABLE) != NO_ERROR)
	    {
	      pgbuf_unfix_and_init (thread_p, from_pgptr);
	      pgbuf_unfix_and_init (thread_p, to_pgptr);
	      return NULL;
	    }
	}

      /* Boundary condition on to-page ? */
      if (to_outptr >= to_aid_ptr)
	{
#ifdef FILE_DEBUG
	  if (to_outptr > to_aid_ptr)
	    {
	      er_log_debug (ARG_FILE_LINE, "file_allocset_expand_sector: *** Boundary condition system error ***\n");
	    }
#endif /* FILE_DEBUG */
	  if (length != 0)
	    {
	      addr.pgptr = to_pgptr;
	      addr.offset = CAST_BUFLEN ((char *) to_aid_ptr - (char *) to_pgptr);
	      log_append_redo_data (thread_p, RVFL_IDSTABLE, &addr, length, to_aid_ptr);
	      length = 0;
	    }

	  /* Use the linked list to find previous page */
	  chain = (FILE_FTAB_CHAIN *) (to_pgptr + FILE_FTAB_CHAIN_OFFSET);
	  to_vpid = chain->prev_ftbvpid;
	  pgbuf_set_dirty (thread_p, to_pgptr, FREE);

	  to_pgptr = pgbuf_fix (thread_p, &to_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
	  if (to_pgptr == NULL)
	    {
	      pgbuf_unfix_and_init (thread_p, from_pgptr);
	      return NULL;
	    }

	  (void) pgbuf_check_page_ptype (thread_p, to_pgptr, PAGE_FTAB);

	  /* 
	   * Calculate the starting offset and length of allocation area in from
	   * page.
	   * Note that we are shifting from the right.. so to_outptr is to the
	   * left, that is the beginning of portion...
	   */
	  if (file_find_limits (to_pgptr, allocset, &to_outptr, &to_aid_ptr, FILE_PAGE_TABLE) != NO_ERROR)
	    {
	      pgbuf_unfix_and_init (thread_p, from_pgptr);
	      pgbuf_unfix_and_init (thread_p, to_pgptr);
	      return NULL;
	    }

	  /* 
	   * Before we move anything to the to_page, we need to log whatever is
	   * there just in case of a crash.. we will need to recover the shift.
	   */

	  addr.pgptr = to_pgptr;
	  addr.offset = CAST_BUFLEN ((char *) to_outptr - (char *) to_pgptr);
	  log_append_undo_data (thread_p, RVFL_IDSTABLE, &addr, CAST_BUFLEN ((char *) to_aid_ptr - (char *) to_outptr),
				to_outptr);
	}

      /* Move as much as possible until a boundary condition is reached */
      while (to_outptr < to_aid_ptr && from_outptr < from_aid_ptr)
	{
	  from_aid_ptr--;
	  /* can we compact this entry ?.. if from_aid_ptr is NULL_PAGEID, the netry is compacted */
	  if (*from_aid_ptr != NULL_PAGEID)
	    {
	      length += sizeof (*to_aid_ptr);
	      to_aid_ptr--;

	      assert (NULL_PAGEID < *from_aid_ptr);
	      *to_aid_ptr = *from_aid_ptr;
	    }
	}
    }

  pgbuf_unfix_and_init (thread_p, from_pgptr);

  if (length != 0)
    {
      addr.pgptr = to_pgptr;
      addr.offset = CAST_BUFLEN ((char *) to_aid_ptr - (char *) to_pgptr);
      log_append_redo_data (thread_p, RVFL_IDSTABLE, &addr, length, to_aid_ptr);
    }

  if ((allocset->start_pages_offset == ((char *) to_aid_ptr - (char *) to_pgptr))
      && VPID_EQ (&allocset->start_pages_vpid, &to_vpid))
    {
      /* The file may be corrupted... */
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_FILE_TABLE_CORRUPTED, 2, fhdr->vfid.volid, fhdr->vfid.fileid);

      pgbuf_set_dirty (thread_p, to_pgptr, FREE);
      to_pgptr = NULL;
      /* Try to fix the file if the problem is related to number of holes */
      if (allocset->num_holes != 0)
	{
	  /* Give another try after number of holes is set to null */
	  allocset->num_holes = 0;
	  return file_allocset_expand_sector (thread_p, fhdr_pgptr, allocset_pgptr, allocset_offset);
	}
      return NULL;
    }


  allocset->start_pages_vpid = to_vpid;
  allocset->start_pages_offset = CAST_BUFLEN ((char *) to_aid_ptr - (char *) to_pgptr);
  allocset->num_holes = 0;
  pgbuf_set_dirty (thread_p, to_pgptr, FREE);
  to_pgptr = NULL;

  /* Log the changes made to the allocation set for REDO purposes */
  recv.start_pages_vpid = allocset->start_pages_vpid;
  recv.end_pages_vpid = allocset->end_pages_vpid;
  recv.start_pages_offset = allocset->start_pages_offset;
  recv.end_pages_offset = allocset->end_pages_offset;
  recv.num_holes = allocset->num_holes;

  addr.pgptr = allocset_pgptr;
  addr.offset = allocset_offset;
  log_append_redo_data (thread_p, RVFL_ALLOCSET_PAGETB_ADDRESS, &addr, sizeof (recv), &recv);

  pgbuf_set_dirty (thread_p, allocset_pgptr, DONT_FREE);

  return fhdr_pgptr;
}

/* TODO: list for file_ftab_chain */
/*
 * file_expand_ftab () - Expand file table description for file
 *   return: fhdr_pgptr on success and NULL on failure
 *   fhdr_pgptr(in): Page pointer to file header table
 *
 * Note: Increase the size of the file table by one page. The file table is
 *       used to keep track of the allocated sectors and pages
 */
static PAGE_PTR
file_expand_ftab (THREAD_ENTRY * thread_p, PAGE_PTR fhdr_pgptr)
{
  FILE_HEADER *fhdr;
  FILE_FTAB_CHAIN *chain;	/* Structure for linking file table pages */
  FILE_FTAB_CHAIN rv_undo_chain;	/* Before image of chain */
  VPID prev_last_ftb_vpid;	/* Previous last page for file table */
  VPID new_ftb_vpid;		/* Newly createad last page for file table */
  LOG_DATA_ADDR addr;
  FILE_RECV_FTB_EXPANSION recv_undo;
  FILE_RECV_FTB_EXPANSION recv_redo;

  (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);

  fhdr = (FILE_HEADER *) (fhdr_pgptr + FILE_HEADER_OFFSET);
  addr.vfid = &fhdr->vfid;

  /* Allocate a new page for file table in any volume */
  if (file_ftabvpid_alloc (thread_p, fhdr->last_table_vpid.volid, fhdr->last_table_vpid.pageid, &new_ftb_vpid, 1,
			   fhdr->type) != NO_ERROR)
    {
      return NULL;
    }

  /* Set allocated page as last file table page */
  addr.pgptr = pgbuf_fix (thread_p, &new_ftb_vpid, NEW_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (addr.pgptr == NULL)
    {
      /* 
       * Something went wrong, unable to fetch recently allocated page.
       * NOTE: The page is deallocated when the top system operation is
       *       aborted due to the failure.
       */
      return NULL;
    }

  (void) pgbuf_set_page_ptype (thread_p, addr.pgptr, PAGE_FTAB);

  prev_last_ftb_vpid = fhdr->last_table_vpid;

  /* Set the chain pointers on the newly allocated page */
  chain = (FILE_FTAB_CHAIN *) (addr.pgptr + FILE_FTAB_CHAIN_OFFSET);
  VPID_SET_NULL (&chain->next_ftbvpid);
  chain->prev_ftbvpid = fhdr->last_table_vpid;

  /* 
   * Log the changes done to the file table chain.
   *
   * We DO NOT HAVE an undo log since this is a new page. If abort,
   * the page will be deallocated. Note that we are in a top system
   * operation created by the file manager, at the end the top operation
   * is going to be committed, aborted, or attached.
   */

  addr.offset = FILE_FTAB_CHAIN_OFFSET;
  log_append_redo_data (thread_p, RVFL_FTAB_CHAIN, &addr, sizeof (*chain), chain);
  pgbuf_set_dirty (thread_p, addr.pgptr, FREE);
  addr.pgptr = NULL;

  if (!VPID_ISNULL (&fhdr->next_table_vpid))
    {
      /* 
       * The previous last page cannot be the file header page.
       * Find the previous last page and modify its chain pointers according to
       * changes
       */
      addr.pgptr = pgbuf_fix (thread_p, &prev_last_ftb_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
      if (addr.pgptr == NULL)
	{
	  /* 
	   * Something went wrong, unable to fetch the page.. return and indicate
	   * failure.
	   * NOTE: The page is deallocated when the top system operation is
	   *       aborted due to the failure.
	   */
	  return NULL;
	}

      (void) pgbuf_check_page_ptype (thread_p, addr.pgptr, PAGE_FTAB);

      chain = (FILE_FTAB_CHAIN *) (addr.pgptr + FILE_FTAB_CHAIN_OFFSET);
      memcpy (&rv_undo_chain, chain, sizeof (*chain));

      chain->next_ftbvpid = new_ftb_vpid;
      addr.offset = FILE_FTAB_CHAIN_OFFSET;	/* Chain is at offset zero */
      log_append_undoredo_data (thread_p, RVFL_FTAB_CHAIN, &addr, sizeof (*chain), sizeof (*chain), &rv_undo_chain,
				chain);
      pgbuf_set_dirty (thread_p, addr.pgptr, FREE);
      addr.pgptr = NULL;
    }

  /* Now update file header */

  /* Save data that is going to be changed for log undo purposes */
  recv_undo.num_table_vpids = fhdr->num_table_vpids;
  recv_undo.next_table_vpid = fhdr->next_table_vpid;
  recv_undo.last_table_vpid = fhdr->last_table_vpid;

  fhdr->num_table_vpids++;
  fhdr->last_table_vpid = new_ftb_vpid;

  /* Is this the second file table page ? */
  if (VPID_ISNULL (&fhdr->next_table_vpid))
    {
      fhdr->next_table_vpid = new_ftb_vpid;
    }

  recv_redo.num_table_vpids = fhdr->num_table_vpids;
  recv_redo.next_table_vpid = fhdr->next_table_vpid;
  recv_redo.last_table_vpid = fhdr->last_table_vpid;

  addr.pgptr = fhdr_pgptr;
  addr.offset = FILE_HEADER_OFFSET;
  log_append_undoredo_data (thread_p, RVFL_FHDR_FTB_EXPANSION, &addr, sizeof (recv_undo), sizeof (recv_redo),
			    &recv_undo, &recv_redo);
  pgbuf_set_dirty (thread_p, fhdr_pgptr, DONT_FREE);

  return fhdr_pgptr;
}

#ifdef FILE_DEBUG
/*
 * file_debug_maybe_newset () - Create a new allocation set every multiple set
 *                            of pages
 *   return: void
 *   fhdr_pgptr(in): Page pointer to file header table
 *   fhdr(in): File header
 *   npages(in): Number of pages that we are allocating
 *
 * Note: For easy debugging of multi volume, this function can be called to
 *       create a new allocation set every multiple set of pages.
 *       The functions does not care about contiguous or non contiguous pages.
 *       It will only create another allocation set every multiple set of
 *       pages.
 */
static void
file_debug_maybe_newset (PAGE_PTR fhdr_pgptr, FILE_HEADER * fhdr, INT32 npages)
{
  FILE_ALLOCSET *allocset;	/* Pointer to an allocation set */
  PAGE_PTR allocset_pgptr = NULL;	/* Page pointer to allocation set description */

  if (fhdr->type == FILE_TEMP)
    {
      return;
    }

  /* 
   ************************************************************
   * For easy debugging of multi volume in debugging mode
   * create a new allocation set every multiple set of pages
   ************************************************************
   */

  if (file_Debug_newallocset_multiple_of < 0 && xboot_find_number_permanent_volumes () > 1)
    {
      file_Debug_newallocset_multiple_of = -file_Debug_newallocset_multiple_of;
    }

  if (file_Debug_newallocset_multiple_of > 0)
    {
      allocset_pgptr =
	pgbuf_fix (thread_p, &fhdr->last_allocset_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
      if (allocset_pgptr != NULL)
	{
	  (void) pgbuf_check_page_ptype (thread_p, allocset_pgptr, PAGE_FTAB);

	  allocset = (FILE_ALLOCSET *) ((char *) allocset_pgptr + fhdr->last_allocset_offset);
	  if (allocset->num_pages > 0 && ((allocset->num_pages % file_Debug_newallocset_multiple_of) == 0))
	    {
	      /* 
	       * Just for the fun of it, declare that there are not more pages
	       * in this allocation set (i.e., volume)
	       */
	      (void) file_allocset_new_set (allocset->volid, fhdr_pgptr, npages, DISK_NONCONTIGUOUS_PAGES);
	    }
	  pgbuf_unfix_and_init (thread_p, allocset_pgptr);
	}
    }
}
#endif /* FILE_DEBUG */

/*
 * file_allocset_new_set () - Create a new allocation for allocation of future
 *                         pages
 *   return: NO_ERROR
 *   last_allocset_volid(in): Don't use this volid as an allocated set
 *   fhdr_pgptr(in): Page pointer to file header table
 *   exp_numpages(in): Exected number of pages needed
 *   setpage_type(in): Type of the set of needed pages
 *
 * Note: It is recommended that the new set describes a volume that has at
 *       least npages free.
 *       The other allocation sets will not be used for allocation purposes
 *       anymore. However, several allocation sets can describe the same
 *       volume.
 */
static int
file_allocset_new_set (THREAD_ENTRY * thread_p, INT16 last_allocset_volid, PAGE_PTR fhdr_pgptr, int exp_numpages,
		       DISK_SETPAGE_TYPE setpage_type)
{
  FILE_HEADER *fhdr;
  INT16 volid;
  int ret = NO_ERROR;

  (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);

  fhdr = (FILE_HEADER *) (fhdr_pgptr + FILE_HEADER_OFFSET);

  /* Find a volume with at least the expected number of pages. */
  volid = file_find_goodvol (thread_p, NULL_VOLID, last_allocset_volid, exp_numpages, setpage_type, fhdr->type);
  if (volid == NULL_VOLID)
    {
      return ER_FAILED;
    }

  return file_allocset_add_set (thread_p, fhdr_pgptr, volid);
}

/*
 * file_allocset_add_set () - Add a new allocation set
 *   return:
 *   fhdr_pgptr(in): Page pointer to file header table
 *   volid(in): volid
 *
 * Note: It is recommended that the new set describes a volume that has at
 *       least npages free.
 *       The other allocation sets will not be used for allocation purposes
 *       anymore. However, several allocation sets can describe the same
 *       volume.
 */
static int
file_allocset_add_set (THREAD_ENTRY * thread_p, PAGE_PTR fhdr_pgptr, INT16 volid)
{
  FILE_HEADER *fhdr;
  FILE_ALLOCSET *allocset;
  PAGE_PTR allocset_pgptr = NULL;
  PAGE_PTR new_allocset_pgptr = NULL;
  INT16 new_allocset_offset;
  VPID new_allocset_vpid;
  FILE_ALLOCSET *new_allocset;
  LOG_DATA_ADDR addr;
  FILE_RECV_ALLOCSET_LINK recv_undo;
  FILE_RECV_ALLOCSET_LINK recv_redo;
  int ret = NO_ERROR;

  (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);

  fhdr = (FILE_HEADER *) (fhdr_pgptr + FILE_HEADER_OFFSET);
  addr.vfid = &fhdr->vfid;

  allocset_pgptr =
    pgbuf_fix (thread_p, &fhdr->last_allocset_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (allocset_pgptr == NULL)
    {
      goto exit_on_error;
    }

  (void) pgbuf_check_page_ptype (thread_p, allocset_pgptr, PAGE_FTAB);

  allocset = (FILE_ALLOCSET *) ((char *) allocset_pgptr + fhdr->last_allocset_offset);

  /* 
   * Try to reuse the last allocation set if all its pages are deleted and
   * there is not a possiblility of a rollback to that allocation set by
   * any transaction including itself...That is, compact when there is not
   * pages marked as deleted
   */
  if (allocset->num_pages == 0 && fhdr->num_user_pages_mrkdelete == 0)
    {
      ret = file_allocset_reuse_last_set (thread_p, fhdr_pgptr, volid);
      if (ret != NO_ERROR)
	{
	  goto exit_on_error;
	}
    }

  if ((allocset->end_pages_offset + SSIZEOF (*allocset)) < DB_PAGESIZE)
    {
      new_allocset_vpid = allocset->end_pages_vpid;
      new_allocset_offset = allocset->end_pages_offset;
    }
  else
    {
      if (file_expand_ftab (thread_p, fhdr_pgptr) == NULL)
	{
	  goto exit_on_error;
	}
      new_allocset_vpid = fhdr->last_table_vpid;
      new_allocset_offset = sizeof (FILE_FTAB_CHAIN);
    }

  /* Initialize the new allocation set */
  new_allocset_pgptr = pgbuf_fix (thread_p, &new_allocset_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (new_allocset_pgptr == NULL)
    {
      goto exit_on_error;
    }

  (void) pgbuf_check_page_ptype (thread_p, new_allocset_pgptr, PAGE_FTAB);

  new_allocset = (FILE_ALLOCSET *) ((char *) new_allocset_pgptr + new_allocset_offset);
  new_allocset->volid = volid;
  new_allocset->num_sects = 0;
  new_allocset->num_pages = 0;
  new_allocset->curr_sectid = NULL_SECTID;
  new_allocset->num_holes = 0;

  /* Calculate positions for the sector table. The sector table is located immediately after the allocation set */

  if ((new_allocset_offset + SSIZEOF (*new_allocset)) < DB_PAGESIZE)
    {
      new_allocset->start_sects_vpid = new_allocset_vpid;
      new_allocset->start_sects_offset = (new_allocset_offset + sizeof (*new_allocset));
    }
  else
    {
      if (file_expand_ftab (thread_p, fhdr_pgptr) == NULL)
	{
	  goto exit_on_error;
	}
      new_allocset->start_sects_vpid = fhdr->last_table_vpid;
      new_allocset->start_sects_offset = sizeof (FILE_FTAB_CHAIN);
    }

  new_allocset->end_sects_vpid = new_allocset->start_sects_vpid;
  new_allocset->end_sects_offset = new_allocset->start_sects_offset;

  /* Calculate positions for the page table. The page table is located immediately after the sector table. */

  new_allocset->start_pages_vpid = new_allocset->end_sects_vpid;
  new_allocset->start_pages_offset = new_allocset->end_sects_offset + (FILE_NUM_EMPTY_SECTS * sizeof (INT32));

  if (new_allocset->start_pages_offset >= DB_PAGESIZE)
    {
      /* The page table should start at a newly created file table. */
      if (file_expand_ftab (thread_p, fhdr_pgptr) == NULL)
	{
	  goto exit_on_error;
	}
      new_allocset->start_pages_vpid = fhdr->last_table_vpid;
      new_allocset->start_pages_offset = sizeof (FILE_FTAB_CHAIN);
    }

  new_allocset->end_pages_vpid = new_allocset->start_pages_vpid;
  new_allocset->end_pages_offset = new_allocset->start_pages_offset;

  /* 
   * Indicate that this is the new allocation set.
   *
   * DON'T NEED UNDO since the allocation set will be undone by the
   * next undo log record. That is, someone will need to point to the
   * allocation set before it takes effect.
   */

  VPID_SET_NULL (&new_allocset->next_allocset_vpid);
  new_allocset->next_allocset_offset = NULL_OFFSET;

  addr.pgptr = new_allocset_pgptr;
  addr.offset = new_allocset_offset;
  log_append_redo_data (thread_p, RVFL_ALLOCSET_NEW, &addr, sizeof (*new_allocset), new_allocset);

  pgbuf_set_dirty (thread_p, new_allocset_pgptr, FREE);
  new_allocset_pgptr = NULL;

  /* Update previous allocation set to point to newly created allocation set */

  /* Save the current values for undo purposes */
  recv_undo.next_allocset_vpid = allocset->next_allocset_vpid;
  recv_undo.next_allocset_offset = allocset->next_allocset_offset;

  /* Link them */
  allocset->next_allocset_vpid = new_allocset_vpid;
  allocset->next_allocset_offset = new_allocset_offset;

  /* Redo stuff */
  recv_redo.next_allocset_vpid = allocset->next_allocset_vpid;
  recv_redo.next_allocset_offset = allocset->next_allocset_offset;

  /* Now log the needed information */
  addr.pgptr = allocset_pgptr;
  addr.offset = fhdr->last_allocset_offset;
  log_append_undoredo_data (thread_p, RVFL_ALLOCSET_LINK, &addr, sizeof (recv_undo), sizeof (recv_redo), &recv_undo,
			    &recv_redo);

  pgbuf_set_dirty (thread_p, allocset_pgptr, FREE);
  allocset_pgptr = NULL;

  /* Now update the file header to indicate that this is the last allocated set */

  /* Save the current values for undo purposes */
  recv_undo.next_allocset_vpid = fhdr->last_allocset_vpid;
  recv_undo.next_allocset_offset = fhdr->last_allocset_offset;

  fhdr->last_allocset_vpid = new_allocset_vpid;
  fhdr->last_allocset_offset = new_allocset_offset;
  fhdr->num_allocsets += 1;

  addr.pgptr = fhdr_pgptr;
  addr.offset = FILE_HEADER_OFFSET;
  log_append_undoredo_data (thread_p, RVFL_FHDR_ADD_LAST_ALLOCSET, &addr, sizeof (recv_undo), sizeof (recv_redo),
			    &recv_undo, &recv_redo);
  pgbuf_set_dirty (thread_p, fhdr_pgptr, DONT_FREE);

  return ret;

exit_on_error:

  if (allocset_pgptr)
    {
      pgbuf_unfix_and_init (thread_p, allocset_pgptr);
    }

  if (new_allocset_pgptr)
    {
      pgbuf_unfix_and_init (thread_p, new_allocset_pgptr);
    }

  if (ret == NO_ERROR)
    {
      ret = ER_FAILED;
    }

  return ret;
}

/*
 * file_allocset_add_pageids () - Add allocated page identifiers to the page table
 *                             of the given allocation set
 *   return: alloc_pageid or NULL_PAGEID when error
 *   fhdr_pgptr(in): Page pointer to file header
 *   allocset_pgptr(out): Pointer to a page where allocation set is located
 *   allocset_offset(in): Location in allocset_pgptr where allocation set is
 *                        located
 *   alloc_pageid(in): Identifier of first page to record
 *   npages(in): Number of contiguous pages to record
 *   alloc_vpids(out): buffer for saving allocated vpids
 *
 * Note: The allocation set and the file header page are also updated to
 *       include the number of allocated pages.
 */
static INT32
file_allocset_add_pageids (THREAD_ENTRY * thread_p, PAGE_PTR fhdr_pgptr, PAGE_PTR allocset_pgptr, INT16 allocset_offset,
			   INT32 alloc_pageid, INT32 npages, FILE_ALLOC_VPIDS * alloc_vpids)
{
  FILE_HEADER *fhdr;
  FILE_ALLOCSET *allocset;
  VPID vpid;
  PAGE_PTR pgptr = NULL;
  INT32 *aid_ptr;		/* Pointer to a portion of allocated pageids table of given allocation set */
  INT32 *outptr;		/* Pointer to outside of portion of allocated pageids */
  LOG_DATA_ADDR addr;
  char *logdata;
  FILE_RECV_ALLOCSET_PAGES recv_undo;
  FILE_RECV_ALLOCSET_PAGES recv_redo;
  int i, length;
  INT32 undo_data, redo_data;

  (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);
  (void) pgbuf_check_page_ptype (thread_p, allocset_pgptr, PAGE_FTAB);

  if (npages <= 0)
    {
      return NULL_PAGEID;
    }

  /* 
   * Allocation of pages and sector is done only from the last allocation set.
   * This is done since the array of page identifiers must be stored in the
   * order of allocation.
   */

  /* Get the file header and the allocation set */
  fhdr = (FILE_HEADER *) (fhdr_pgptr + FILE_HEADER_OFFSET);

  addr.vfid = &fhdr->vfid;

  allocset = (FILE_ALLOCSET *) ((char *) allocset_pgptr + allocset_offset);

  /* Append the page identifiers at the end of the page table */

  vpid = allocset->end_pages_vpid;
  pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (pgptr == NULL)
    {
      return NULL_PAGEID;
    }

  (void) pgbuf_check_page_ptype (thread_p, pgptr, PAGE_FTAB);

  /* Address end of page table... and the end of page */
  CAST_TO_PAGEARRAY (aid_ptr, pgptr, allocset->end_pages_offset);
  CAST_TO_PAGEARRAY (outptr, pgptr, DB_PAGESIZE);

  /* Note that we do not undo any information on page table since it is fixed by undoing the allocation set pointer */

  addr.pgptr = pgptr;
  addr.offset = allocset->end_pages_offset;
  logdata = (char *) aid_ptr;

  /* First log the before images that are planned to be changed on current page */

  if (outptr > aid_ptr)
    {
      length = CAST_BUFLEN (outptr - aid_ptr);
      length = (int) (sizeof (npages) * MIN (npages, length));
      log_append_undo_data (thread_p, RVFL_IDSTABLE, &addr, length, logdata);
    }

  for (i = 0; i < npages; i++)
    {
      if (aid_ptr >= outptr)
	{
	  if ((char *) aid_ptr != logdata)
	    {
	      log_append_redo_data (thread_p, RVFL_IDSTABLE, &addr, CAST_BUFLEN ((char *) aid_ptr - logdata), logdata);
	      pgbuf_set_dirty (thread_p, pgptr, DONT_FREE);
	    }
	  if (file_ftabvpid_next (fhdr, pgptr, &vpid) != NO_ERROR)
	    {
	      pgbuf_unfix_and_init (thread_p, pgptr);
	      return NULL_PAGEID;
	    }
	  if (VPID_ISNULL (&vpid))
	    {
	      /* 
	       * The page table will continue at next page of file table.
	       * We expect pgptr to be currently pointing to the last ftb page.
	       * If an ftb expansion succeeds, yet a second next_ftbvpid still
	       * returns NULL, then something is wrong with the ftb structure.
	       */
	      if (file_expand_ftab (thread_p, fhdr_pgptr) == NULL || file_ftabvpid_next (fhdr, pgptr, &vpid) != NO_ERROR
		  || VPID_ISNULL (&vpid))
		{
		  if (er_errid () == NO_ERROR)
		    {
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FILE_TABLE_CORRUPTED, 2, fhdr->vfid.volid,
			      fhdr->vfid.fileid);
		    }

		  pgbuf_unfix_and_init (thread_p, pgptr);
		  return NULL_PAGEID;
		}
	    }

	  /* Free this page and get the new page */
	  pgbuf_unfix_and_init (thread_p, pgptr);
	  pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
	  if (pgptr == NULL)
	    {
	      return NULL_PAGEID;
	    }

	  (void) pgbuf_check_page_ptype (thread_p, pgptr, PAGE_FTAB);

	  CAST_TO_PAGEARRAY (aid_ptr, pgptr, sizeof (FILE_FTAB_CHAIN));
	  CAST_TO_PAGEARRAY (outptr, pgptr, DB_PAGESIZE);

	  addr.pgptr = pgptr;
	  addr.offset = sizeof (FILE_FTAB_CHAIN);
	  logdata = (char *) aid_ptr;


	  /* First log the before images that are planned to be changed on current page */

	  if (outptr > aid_ptr)
	    {
	      length = (int) (((npages - i) > (outptr - aid_ptr)) ? (outptr - aid_ptr) : (npages - i));
	      log_append_undo_data (thread_p, RVFL_IDSTABLE, &addr, sizeof (npages) * length, logdata);
	    }
	}

      /* Store the identifier */
      assert (NULL_PAGEID < (alloc_pageid + i));
      *aid_ptr++ = alloc_pageid + i;
      if (alloc_vpids != NULL)
	{
	  alloc_vpids->vpids[alloc_vpids->index].volid = allocset->volid;
	  alloc_vpids->vpids[alloc_vpids->index].pageid = alloc_pageid + i;
	  alloc_vpids->index++;
	}
    }

  /* Log the pageids stored on this page */
  log_append_redo_data (thread_p, RVFL_IDSTABLE, &addr, CAST_BUFLEN ((char *) aid_ptr - logdata), logdata);

  /* Update the allocation set for end of page table and number of pages. Similarly, the file header must be updated.
   * Need to log the info... The number of holes does not changed. */

  /* SAVE the information that is going to be change for UNDO purposes */
  recv_undo.curr_sectid = allocset->curr_sectid;
  recv_undo.end_pages_vpid = allocset->end_pages_vpid;
  recv_undo.end_pages_offset = allocset->end_pages_offset;
  recv_undo.num_pages = -npages;

  /* Now CHANGE IT */
  allocset->end_pages_vpid = vpid;
  allocset->end_pages_offset = (INT16) ((char *) aid_ptr - (char *) pgptr);
  allocset->num_pages += npages;

  /* Free the page table */
  pgbuf_set_dirty (thread_p, pgptr, FREE);
  pgptr = NULL;

  /* SAVE the information that has been changed for REDO purposes */
  recv_redo.curr_sectid = allocset->curr_sectid;
  recv_redo.end_pages_vpid = allocset->end_pages_vpid;
  recv_redo.end_pages_offset = allocset->end_pages_offset;
  recv_redo.num_pages = npages;

  /* Now log the changes to the allocation set and set the page where the allocation set resides as dirty */

  addr.pgptr = allocset_pgptr;
  addr.offset = allocset_offset;
  log_append_undoredo_data (thread_p, RVFL_ALLOCSET_ADD_PAGES, &addr, sizeof (recv_undo), sizeof (recv_redo),
			    &recv_undo, &recv_redo);

  pgbuf_set_dirty (thread_p, allocset_pgptr, DONT_FREE);

  /* Change the file header and log the change */

  undo_data = -npages;
  redo_data = npages;
  addr.pgptr = fhdr_pgptr;
  addr.offset = FILE_HEADER_OFFSET;

  log_append_undoredo_data (thread_p, RVFL_FHDR_ADD_PAGES, &addr, sizeof (fhdr->num_user_pages),
			    sizeof (fhdr->num_user_pages), &undo_data, &redo_data);

  fhdr->num_user_pages += npages;
  pgbuf_set_dirty (thread_p, fhdr_pgptr, DONT_FREE);

  return alloc_pageid;
}

/*
 * file_allocset_alloc_pages () - Allocate pages in given allocation set
 *   return:
 *   fhdr_pgptr(in): Page pointer to file header
 *   allocset_vpid(in): Page where the allocation set is located
 *   allocset_offset(in): Location in allocset_pgptr where allocation set is
 *                        located
 *   first_new_vpid(out): The identifier of the allocated page
 *   npages(in): Number of pages to allocate
 *   near_vpid(in): Near vpid identifier. Hint only, it may be ignored
 *   alloc_vpids(out): buffer for saving allocated vpids
 *
 * Note: Allocate the closest "npages" contiguous free pages to the "near_vpid"
 *       page for the given allocation set. This function may allocate sectors
 *       automatically. If there are not enough "npages" contiguous free pages,
 *       a NULL_PAGEID is returned and an error condition code is flagged.
 */
static int
file_allocset_alloc_pages (THREAD_ENTRY * thread_p, PAGE_PTR fhdr_pgptr, VPID * allocset_vpid, INT16 allocset_offset,
			   VPID * first_new_vpid, INT32 npages, const VPID * near_vpid, FILE_ALLOC_VPIDS * alloc_vpids)
{
  FILE_HEADER *fhdr;
  FILE_ALLOCSET *allocset;
  PAGE_PTR allocset_pgptr = NULL;
  INT32 near_pageid;		/* Try to allocate the pages near to this page */
  INT32 sectid;			/* Allocate the pages on this sector */
  INT32 *aid_ptr;		/* Pointer to a portion of allocated pageids table of given allocation set */
  INT32 *outptr;		/* Pointer to outside of portion of allocated pageids */
  PAGE_PTR pgptr = NULL;
  VPID vpid;
  INT32 alloc_pageid;		/* Identifier of allocated page */
  int answer = FILE_ALLOCSET_ALLOC_ZERO;
#if !defined (NDEBUG)
  int retry = 0;
#endif
  DISK_PAGE_TYPE alloc_page_type;

  (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);

  /* 
   * Allocation of pages and sectors is done only from the last allocation set.
   * This is done since the array of page identifiers must be stored in the
   * order of allocation.
   */
  allocset_pgptr = pgbuf_fix (thread_p, allocset_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (allocset_pgptr == NULL)
    {
      goto exit_on_error;
    }

  (void) pgbuf_check_page_ptype (thread_p, allocset_pgptr, PAGE_FTAB);

  /* Get the file header and the allocation set */
  fhdr = (FILE_HEADER *) (fhdr_pgptr + FILE_HEADER_OFFSET);

  allocset = (FILE_ALLOCSET *) ((char *) allocset_pgptr + allocset_offset);

  /* Check if the near page is of any use */
  near_pageid = ((near_vpid != NULL && allocset->volid == near_vpid->volid) ? near_vpid->pageid : NULL_PAGEID);

  /* Try to allocate the pages from the last used sector identifier. */
  sectid = allocset->curr_sectid;
  if (sectid == NULL_SECTID)
    {
      sectid = file_allocset_alloc_sector (thread_p, fhdr_pgptr, allocset_pgptr, allocset_offset, npages);
    }

  /* 
   * If we cannot allocate the pages within this sector, we must check all
   * the previous allocated sectors. However, we do this only when we think
   * there are enough free pages on such sectors.
   */
  alloc_page_type = file_get_disk_page_type (fhdr->type);

  alloc_pageid = ((sectid != NULL_SECTID)
		  ? disk_alloc_page (thread_p, allocset->volid, sectid, npages, near_pageid, alloc_page_type)
		  : DISK_NULL_PAGEID_WITH_ENOUGH_DISK_PAGES);

  if (alloc_pageid == DISK_NULL_PAGEID_WITH_ENOUGH_DISK_PAGES && sectid != DISK_SECTOR_WITH_ALL_PAGES
      && (fhdr->type == FILE_BTREE || fhdr->type == FILE_BTREE_OVERFLOW_KEY)
      && ((allocset->num_pages + npages) <= ((allocset->num_sects - 1) * DISK_SECTOR_NPAGES)))
    {
      vpid = allocset->start_sects_vpid;

      /* Go sector by sector until the number of desired pages are found */

      while (!VPID_ISNULL (&vpid))
	{
	  pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
	  if (pgptr == NULL)
	    {
	      /* Unable to fetch file table page. Free the file header page and release its lock */
	      goto exit_on_error;
	    }

	  (void) pgbuf_check_page_ptype (thread_p, pgptr, PAGE_FTAB);

	  /* Find the beginning and the end of the portion of the sector table stored on the current file table page */

	  if (file_find_limits (pgptr, allocset, &aid_ptr, &outptr, FILE_SECTOR_TABLE) != NO_ERROR)
	    {
	      goto exit_on_error;
	    }

	  for (; alloc_pageid == DISK_NULL_PAGEID_WITH_ENOUGH_DISK_PAGES && aid_ptr < outptr; aid_ptr++)
	    {
	      sectid = *aid_ptr;
	      alloc_pageid = disk_alloc_page (thread_p, allocset->volid, sectid, npages, near_pageid, alloc_page_type);
	    }

	  /* Was page found ? */
	  if (alloc_pageid == DISK_NULL_PAGEID_WITH_ENOUGH_DISK_PAGES)
	    {
	      /* Find next page of sector array */
	      if (VPID_EQ (&vpid, &allocset->end_sects_vpid))
		{
		  VPID_SET_NULL (&vpid);
		}
	      else
		{
		  if (file_ftabvpid_next (fhdr, pgptr, &vpid) != NO_ERROR)
		    {
		      goto exit_on_error;
		    }
		}
	    }
	  else
	    {
	      /* Last sector used for allocation */
	      if (alloc_pageid != NULL_PAGEID)
		{
		  allocset->curr_sectid = sectid;
		}

	      VPID_SET_NULL (&vpid);	/* This will get out of the while */
	    }
	  pgbuf_unfix_and_init (thread_p, pgptr);
	}
    }

  /* If we were unable to allocate the pages, and there is not any unexpected error, allocate a new sector */
  while (alloc_pageid == DISK_NULL_PAGEID_WITH_ENOUGH_DISK_PAGES && sectid != DISK_SECTOR_WITH_ALL_PAGES)
    {
      /* Allocate a new sector for the file */
      sectid = file_allocset_alloc_sector (thread_p, fhdr_pgptr, allocset_pgptr, allocset_offset, npages);
      if (sectid == NULL_SECTID)
	{
	  /* Unable to allocate a new sector for file */

	  /* Free the file header page and release its lock */
	  goto exit_on_error;
	}

      alloc_pageid = disk_alloc_page (thread_p, allocset->volid, sectid, npages, near_pageid, alloc_page_type);
#if !defined (NDEBUG)
      if (++retry > 1)
	{
	  er_log_debug (ARG_FILE_LINE,
			"file_allocset_alloc_pages: retry = %d, filetype= %s, "
			"volid = %d, secid = %d, pageid = %d, npages = %d\n", retry, file_type_to_string (fhdr->type),
			allocset->volid, sectid, alloc_pageid, npages);
	}
#endif
    }

  /* Store the page identifiers into the array of pageids */

  if (alloc_pageid != NULL_PAGEID && alloc_pageid != DISK_NULL_PAGEID_WITH_ENOUGH_DISK_PAGES)
    {
      /* Add the pageids entries */
      if (file_allocset_add_pageids (thread_p, fhdr_pgptr, allocset_pgptr, allocset_offset, alloc_pageid, npages,
				     alloc_vpids) == NULL_PAGEID)
	{
	  /* 
	   * Something went wrong, return
	   * NOTE: The allocated pages are deallocated when the top system
	   *       operation is aborted due to the failure.
	   */
	  alloc_pageid = NULL_PAGEID;
	  answer = FILE_ALLOCSET_ALLOC_ERROR;
	}
    }
  else
    {
      /* Unable to allocate the pages for this allocation set */
      alloc_pageid = NULL_PAGEID;
    }

  if (alloc_pageid != NULL_PAGEID)
    {
      first_new_vpid->volid = allocset->volid;
      first_new_vpid->pageid = alloc_pageid;
      answer = FILE_ALLOCSET_ALLOC_NPAGES;
    }
  else
    {
      VPID_SET_NULL (first_new_vpid);
      first_new_vpid->volid = allocset->volid;	/* Hint for undesirable volid */
    }

  pgbuf_unfix_and_init (thread_p, allocset_pgptr);

  perfmon_add_stat (thread_p, PSTAT_FILE_NUM_PAGE_ALLOCS, npages);

  return answer;

exit_on_error:

  if (pgptr)
    {
      pgbuf_unfix_and_init (thread_p, pgptr);
    }

  if (allocset_pgptr)
    {
      pgbuf_unfix_and_init (thread_p, allocset_pgptr);
    }

  VPID_SET_NULL (first_new_vpid);
  return FILE_ALLOCSET_ALLOC_ERROR;
}

/*
 * file_alloc_pages () - Allocate a user page
 *   return: first_alloc_vpid or NULL
 *   vfid(in): Complete file identifier.
 *   first_alloc_vpid(in): Identifier of first contiguous allocated pages
 *   npages(in): Number of pages to allocate
 *   near_vpid(in): Allocate the pages as close as the value of this parameter.
 *                  Hint only, it may be ignored.
 *   fun(in): Function to be called to initialize the page
 *   args(in): Additional arguments to be passed to fun
 *
 */
VPID *
file_alloc_pages (THREAD_ENTRY * thread_p, const VFID * vfid, VPID * first_alloc_vpid, INT32 npages,
		  const VPID * near_vpid, bool (*fun) (THREAD_ENTRY * thread_p, const VFID * vfid,
						       const FILE_TYPE file_type, const VPID * first_alloc_vpid,
						       INT32 npages, void *args), void *args)
{
  return file_alloc_pages_internal (thread_p, vfid, first_alloc_vpid, npages, near_vpid, NULL,
				    FILE_WITHOUT_OUTER_SYSTEM_OP, fun, args);
}

/*
 * file_alloc_pages_with_outer_sys_op () - Allocate a user page
 *   return: first_alloc_vpid or NULL
 *   vfid(in): Complete file identifier.
 *   first_alloc_vpid(in): Identifier of first contiguous allocated pages
 *   npages(in): Number of pages to allocate
 *   near_vpid(in): Allocate the pages as close as the value of this parameter.
 *                  Hint only, it may be ignored.
 *   p_file_type(out): File type
 *   fun(in): Function to be called to initialize the page
 *   args(in): Additional arguments to be passed to fun
 *
 */
VPID *
file_alloc_pages_with_outer_sys_op (THREAD_ENTRY * thread_p, const VFID * vfid, VPID * first_alloc_vpid, INT32 npages,
				    const VPID * near_vpid, FILE_TYPE * p_file_type,
				    bool (*fun) (THREAD_ENTRY * thread_p, const VFID * vfid, const FILE_TYPE file_type,
						 const VPID * first_alloc_vpid, INT32 npages, void *args), void *args)
{
  return file_alloc_pages_internal (thread_p, vfid, first_alloc_vpid, npages, near_vpid, p_file_type,
				    FILE_WITH_OUTER_SYSTEM_OP, fun, args);
}

/*
 * file_alloc_pages () - Allocate a user page
 *   return: first_alloc_vpid or NULL
 *   vfid(in): Complete file identifier.
 *   first_alloc_vpid(in): Identifier of first contiguous allocated pages
 *   npages(in): Number of pages to allocate
 *   near_vpid(in): Allocate the pages as close as the value of this parameter.
 *                  Hint only, it may be ignored.
 *   p_file_type(out): File type
 *   with_sys_op(in):
 *   fun(in): Function to be called to initialize the page
 *   args(in): Additional arguments to be passed to fun
 *
 */
static VPID *
file_alloc_pages_internal (THREAD_ENTRY * thread_p, const VFID * vfid, VPID * first_alloc_vpid, INT32 npages,
			   const VPID * near_vpid, FILE_TYPE * p_file_type, int with_sys_op,
			   bool (*fun) (THREAD_ENTRY * thread_p, const VFID * vfid, const FILE_TYPE file_type,
					const VPID * first_alloc_vpid, INT32 npages, void *args), void *args)
{
  FILE_HEADER *fhdr;
  PAGE_PTR fhdr_pgptr = NULL;
  VPID vpid;
  int allocstate;
  FILE_TYPE file_type;
  FILE_IS_NEW_FILE isfile_new;
  bool old_val = false;
  bool restore_check_interrupt = false;
  bool rv;

  assert (false);

  /* 
   * Start a TOP SYSTEM OPERATION.
   * This top system operation will be either ABORTED (case of failure) or
   * its actions will be attached to its outer parent (new file) or committed
   * (old file)
   */

  if (with_sys_op == FILE_WITHOUT_OUTER_SYSTEM_OP)
    {
      if (log_start_system_op (thread_p) == NULL)
	{
	  VPID_SET_NULL (first_alloc_vpid);
	  return NULL;
	}
    }

  if (npages <= 0)
    {
      /* This is a system error. Trying to allocate zero or a negative number of pages */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FILE_ALLOC_NOPAGES, 1, npages);
      goto exit_on_error;
    }

  /* Lock the file header page in exclusive mode and then fetch the page. */
  vpid.volid = vfid->volid;
  vpid.pageid = vfid->fileid;
  fhdr_pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (fhdr_pgptr == NULL)
    {
      goto exit_on_error;
    }

  (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);

  fhdr = (FILE_HEADER *) (fhdr_pgptr + FILE_HEADER_OFFSET);

  file_type = fhdr->type;

  if (fhdr->type == FILE_TEMP)
    {
      old_val = thread_set_check_interrupt (thread_p, false);
      restore_check_interrupt = true;
    }

#ifdef FILE_DEBUG
  /* FOR EASY DEBUGGING OF MULTI VOLUME IN DEBUGGING MODE CREATE A NEW ALLOCATION SET EVERY MULTIPLE SET OF PAGES */
  file_debug_maybe_newset (fhdr_pgptr, fhdr, npages);
#endif /* FILE_DEBUG */

  /* 
   * Find last allocation set to allocate the new pages. If we cannot allocate
   * the new pages into this allocation set, a new allocation set is created.
   * The new allocation set is created into the volume with more free pages.
   */

  while (((allocstate = file_allocset_alloc_pages (thread_p, fhdr_pgptr, &fhdr->last_allocset_vpid,
						   fhdr->last_allocset_offset, first_alloc_vpid, npages, near_vpid,
						   NULL)) == FILE_ALLOCSET_ALLOC_ZERO)
	 && first_alloc_vpid->volid != NULL_VOLID)
    {
      /* 
       * Unable to create the pages at this volume
       * Create a new allocation set and try to allocate the pages from there
       * If first_alloc_vpid->volid == NULL_VOLID, there was an error...
       */
      if (file_allocset_new_set (thread_p, first_alloc_vpid->volid, fhdr_pgptr, npages,
				 DISK_CONTIGUOUS_PAGES) != NO_ERROR)
	{
	  break;
	}

      /* We need to execute the loop due that disk space can be allocated by several transactions */
    }

  if (allocstate != FILE_ALLOCSET_ALLOC_NPAGES)
    {
      if (allocstate == FILE_ALLOCSET_ALLOC_ZERO && er_errid () != ER_INTERRUPTED)
	{
	  if (fhdr->type == FILE_TEMP)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FILE_NOT_ENOUGH_PAGES_IN_VOLUME, 2,
		      fileio_get_volume_label (vfid->volid, PEEK), npages);
	    }
	  else
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FILE_NOT_ENOUGH_PAGES_IN_DATABASE, 1, npages);
	    }
	}
      goto exit_on_error;
    }

  if (fun != NULL && (*fun) (thread_p, vfid, file_type, first_alloc_vpid, npages, args) == false)
    {
      /* We must abort the allocation of the page since the user function failed */
      goto exit_on_error;
    }

  pgbuf_unfix_and_init (thread_p, fhdr_pgptr);

  /* 
   * Allocation of pages of old files are committed, so that they can be made
   * available to any transaction. Allocation of pages of new files are not
   * committed until the transaction commits
   */

  if (with_sys_op == FILE_WITHOUT_OUTER_SYSTEM_OP)
    {
      isfile_new = file_is_new_file (thread_p, vfid);
      if (isfile_new == FILE_ERROR)
	{
	  goto exit_on_error;
	}

      if (isfile_new == FILE_NEW_FILE && file_type != FILE_TEMP && logtb_is_current_active (thread_p) == true)
	{
	  log_end_system_op (thread_p, LOG_RESULT_TOPOP_ATTACH_TO_OUTER);
	}
      else
	{
	  log_end_system_op (thread_p, LOG_RESULT_TOPOP_COMMIT);
	}
    }

  if (restore_check_interrupt == true)
    {
      rv = thread_set_check_interrupt (thread_p, old_val);
    }

  if (p_file_type)
    {
      *p_file_type = file_type;
    }

  return first_alloc_vpid;

exit_on_error:

  if (with_sys_op == FILE_WITHOUT_OUTER_SYSTEM_OP)
    {
      (void) log_end_system_op (thread_p, LOG_RESULT_TOPOP_ABORT);
    }

  if (fhdr_pgptr)
    {
      pgbuf_unfix_and_init (thread_p, fhdr_pgptr);
    }

  VPID_SET_NULL (first_alloc_vpid);

  if (restore_check_interrupt == true)
    {
      rv = thread_set_check_interrupt (thread_p, old_val);
    }

  return NULL;
}

/*
 * file_alloc_pages_as_noncontiguous () - Allocate non contiguous pages
 *   return: first_alloc_vpid or NULL
 *   vfid(in): Complete file identifier
 *   first_alloc_vpid(out): Identifier of first contiguous allocated pages
 *   first_alloc_nthpage(out): Set to the nthpage order of the first page
 *                            allocated
 *   npages(in): Number of pages to allocate
 *   near_vpid(in): Allocate the pages as close as the value of this parameter.
 *                  Hint only, it may be ignored.
 *   fun(in): Function to be called to initialize the page
 *   args(in): Additional arguments to be passed to fun
 *   alloc_vpids(out): buffer for saving allocated vpids
 *
 * Note: Allocate "npages" free pages for the given file. The pages are
 *       allocated as contiguous as possibe. The pages may be allocated from
 *       any available volume. If there are not "npages" free pages, a NULL
 *       value is returned and an error condition code is flagged.
 *
 *       If we were able to allocate the requested number of pages, fun is
 *       called to initialize those pages. Fun must initialize the page and
 *       log any information to re-initialize the page in the case of system
 *       failures. If fun, returns false, the allocation is aborted.
 *       If a function is not passed (i.e., fun == NULL), the pages will not be
 *       initialized and the caller is fully responsible for initializing and
 *       interpreting the pages correctly, even in the presence of failures.
 *       In this case the caller must watch out for a window between the commit
 *       (top action) of an allocation of pages of old files and a system crash
 *       before the logging of the initialization of the page. After the crash
 *       the caller is still responsible for initializing and interpreting such
 *       page. Therefore, it is better to pass a function to initialize a page
 *       in most cases. Some exceptions could be new files since the file is
 *       removed if rollback or system crash, and non permanent content of
 *       pages (e.g., query list manager, sort manager).
 */
VPID *
file_alloc_pages_as_noncontiguous (THREAD_ENTRY * thread_p, const VFID * vfid, VPID * first_alloc_vpid,
				   INT32 * first_alloc_nthpage, INT32 npages, const VPID * near_vpid,
				   bool (*fun) (THREAD_ENTRY * thread_p, const VFID * vfid, const FILE_TYPE file_type,
						const VPID * first_alloc_vpid, const INT32 * first_alloc_nthpage,
						INT32 npages, void *args), void *args, FILE_ALLOC_VPIDS * alloc_vpids)
{
  FILE_HEADER *fhdr;
  PAGE_PTR fhdr_pgptr = NULL;
  VPID vpid;
  int allocstate = FILE_ALLOCSET_ALLOC_ZERO;
  INT32 allocate_npages;
  INT32 requested_npages = npages;
  INT32 max_npages;
  INT32 ftb_npages = 1;
  FILE_TYPE file_type;
  FILE_IS_NEW_FILE isfile_new;
  bool old_val = false;
  bool restore_check_interrupt = false;
  bool is_tmp_file;
  bool rv;

  assert (false);

  /* 
   * Start a TOP SYSTEM OPERATION.
   * This top system operation will be either ABORTED (case of failure) or
   * its actions will be attached to its outer parent (new file) or committed
   * (old file)
   */

  if (log_start_system_op (thread_p) == NULL)
    {
      VPID_SET_NULL (first_alloc_vpid);
      return NULL;
    }

  if (npages <= 0)
    {
      /* This is a system error. Trying to allocate zero or a negative number of pages */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FILE_ALLOC_NOPAGES, 1, npages);
      goto exit_on_error;
    }

  vpid.volid = vfid->volid;
  vpid.pageid = vfid->fileid;

  fhdr_pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (fhdr_pgptr == NULL)
    {
      goto exit_on_error;
    }

  (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);

  fhdr = (FILE_HEADER *) (fhdr_pgptr + FILE_HEADER_OFFSET);

  file_type = fhdr->type;

  if (file_type == FILE_TEMP)
    {
      old_val = thread_set_check_interrupt (thread_p, false);
      restore_check_interrupt = true;
    }

  /* 
   * Find last allocation set to allocate the new pages. The pages are
   * allocated as contiguous as possible. That is, they are allocated in
   * chunks. If we cannot allocate all the pages in this allocation set, a
   * new allocation set is created to allocate the rest of the pages.
   */

  VPID_SET_NULL (first_alloc_vpid);
  *first_alloc_nthpage = fhdr->num_user_pages;

  is_tmp_file = ((file_type == FILE_TEMP || file_type == FILE_QUERY_AREA) ? true : false);

  while (npages > 0)
    {
      /* 
       * The non temporary files has more than DISK_SECTOR_NPAGES are allocated
       * among sectors.
       * We don't use DISK_SECTOR_WITH_ALL_PAGES way for these files any more.
       */
      allocate_npages = (is_tmp_file ? npages : MIN (npages, DISK_SECTOR_NPAGES));

#ifdef FILE_DEBUG
      /* FOR EASY DEBUGGING OF MULTI VOLUME IN DEBUGGING MODE CREATE A NEW ALLOCATION SET EVERY MULTIPLE SET OF PAGES */
      file_debug_maybe_newset (fhdr_pgptr, fhdr, npages);
#endif /* FILE_DEBUG */

      while (((allocstate = file_allocset_alloc_pages (thread_p, fhdr_pgptr, &fhdr->last_allocset_vpid,
						       fhdr->last_allocset_offset, &vpid, allocate_npages, near_vpid,
						       alloc_vpids)) == FILE_ALLOCSET_ALLOC_ZERO)
	     && vpid.volid != NULL_VOLID)
	{
	  /* guess a simple overhead for storing page identifiers. This may not be very accurate, however, it is OK for 
	   * more practical purposes */

	  ftb_npages = CEIL_PTVDIV (npages * sizeof (INT32), DB_PAGESIZE);

	  /* 
	   * We were unable to allocate the desired number of pages.
	   * Make sure that we have enough non contiguous pages for the file,
	   * before we continue.
	   */

	  max_npages = (file_find_good_maxpages (thread_p, file_type) - ftb_npages);

	  /* Do we have enough pages */
	  if (max_npages < npages)
	    {
	      break;
	    }

	  /* Get whatever we can from the current volume, before we try another volume */

	  /*max_npages = disk_get_maxcontiguous_numpages (thread_p, vpid.volid, npages); */

	  if (max_npages > 0 && max_npages < allocate_npages)
	    {
	      allocate_npages = max_npages;
	    }
	  else
	    {
	      /* 
	       * Unable to create the pages at this volume
	       * Create a new allocation set and try to allocate the pages from
	       * there. If vpid->volid == NULL_VOLID, there was an error...
	       */
	      if (file_allocset_new_set (thread_p, vpid.volid, fhdr_pgptr, npages,
					 DISK_NONCONTIGUOUS_SPANVOLS_PAGES) != NO_ERROR)
		{
		  break;
		}
	    }

	  /* We need to execute the loop due that disk space can be allocated by several transactions */
	}

      if (allocstate == FILE_ALLOCSET_ALLOC_NPAGES)
	{
	  npages -= allocate_npages;

	  if (VPID_ISNULL (first_alloc_vpid))
	    {
	      *first_alloc_vpid = vpid;
	    }
	}
      else
	{
	  break;
	}
    }

  if (allocstate != FILE_ALLOCSET_ALLOC_NPAGES)
    {
      if (allocstate == FILE_ALLOCSET_ALLOC_ZERO && er_errid () != ER_INTERRUPTED)
	{
	  if (fhdr->type == FILE_TEMP)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FILE_NOT_ENOUGH_PAGES_IN_VOLUME, 2,
		      fileio_get_volume_label (vfid->volid, PEEK), npages);
	    }
	  else
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FILE_NOT_ENOUGH_PAGES_IN_DATABASE, 1, npages);
	    }
	}

      goto exit_on_error;
    }

  /* 
   * Allocation of pages of old files are committed, so that they can be made
   * available to any transaction. Allocation of pages of new files are not
   * committed until the transaction commits
   */

  /* Initialize the pages */

  if (fun != NULL
      && (*fun) (thread_p, vfid, file_type, first_alloc_vpid, first_alloc_nthpage, requested_npages, args) == false)
    {
      /* We must abort the allocation of the page since the user function failed */
      goto exit_on_error;
    }

  pgbuf_unfix_and_init (thread_p, fhdr_pgptr);

  isfile_new = file_is_new_file (thread_p, vfid);
  if (isfile_new == FILE_ERROR)
    {
      goto exit_on_error;
    }

  if (isfile_new == FILE_NEW_FILE && file_type != FILE_TEMP && logtb_is_current_active (thread_p) == true)
    {
      log_end_system_op (thread_p, LOG_RESULT_TOPOP_ATTACH_TO_OUTER);
    }
  else
    {
      log_end_system_op (thread_p, LOG_RESULT_TOPOP_COMMIT);
    }

  if (restore_check_interrupt == true)
    {
      rv = thread_set_check_interrupt (thread_p, old_val);
    }

  return first_alloc_vpid;

exit_on_error:

  (void) log_end_system_op (thread_p, LOG_RESULT_TOPOP_ABORT);

  if (fhdr_pgptr)
    {
      pgbuf_unfix_and_init (thread_p, fhdr_pgptr);
    }

  *first_alloc_nthpage = -1;
  VPID_SET_NULL (first_alloc_vpid);

  if (restore_check_interrupt == true)
    {
      rv = thread_set_check_interrupt (thread_p, old_val);
    }

  return NULL;
}

/*
 * file_alloc_pages_at_volid () - Allocate user pages at specific volume identifier
 *   return:
 *   vfid(in): Complete file identifier
 *   first_alloc_vpid(out): Identifier of first contiguous allocated pages
 *   npages(in): Number of pages to allocate
 *   near_vpid(in): Allocate the pages as close as the value of this parameter.
 *                  Hint only, it may be ignored
 *   desired_volid(in): Allocate the pages only on this specific volume
 *   fun(in): Function to be called to initialize the page
 *   args(in): Additional arguments to be passed to fun
 *
 * Note: Allocate "npages" free pages for the given file. The pages are
 *       allocated as contiguous as possibe. The pages may be allocated from
 *       any available volume. If there are not "npages" free pages, a NULL
 *       value is returned and an error condition code is flagged.
 *
 *       If we were able to allocate the requested number of pages, fun is
 *       called to initialize those pages. Fun must initialize the page and
 *       log any information to re-initialize the page in the case of system
 *       failures. If fun, returns false, the allocation is aborted.
 *       If a function is not passed (i.e., fun == NULL), the pages will not be
 *       initialized and the caller is fully responsible for initializing and
 *       interpreting the pages correctly, even in the presence of failures.
 *       In this case the caller must watch out for a window between the commit
 *       (top action) of an allocation of pages of old files and a system crash
 *       before the logging of the initialization of the page. After the crash
 *       the caller is still responsible for initializing and interpreting such
 *       page. Therefore, it is better to pass a function to initialize a page
 *       in most cases. Some exceptions could be new files since the file is
 *       removed if rollback or system crash, and non permanent content of
 *       pages (e.g., query list manager, sort manager).
 */
VPID *
file_alloc_pages_at_volid (THREAD_ENTRY * thread_p, const VFID * vfid, VPID * first_alloc_vpid, INT32 npages,
			   const VPID * near_vpid, INT16 desired_volid,
			   bool (*fun) (const VFID * vfid, const FILE_TYPE file_type, const VPID * first_alloc_vpid,
					INT32 npages, void *args), void *args)
{
  FILE_HEADER *fhdr;
  PAGE_PTR fhdr_pgptr = NULL;
  FILE_ALLOCSET *allocset;
  PAGE_PTR allocset_pgptr = NULL;
  VPID vpid;
  FILE_TYPE file_type;
  FILE_IS_NEW_FILE isfile_new;
  int allocstate;
  bool old_val = false;
  bool restore_check_interrupt = false;
  bool rv;

  if (npages <= 0 || desired_volid == NULL_VOLID || fileio_get_volume_descriptor (desired_volid) == NULL_VOLDES)
    {
      /* This is a system error. Trying to allocate zero or a negative number of pages */
      if (npages <= 0)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FILE_ALLOC_NOPAGES, 1, npages);
	}
      else
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_FILE_UNKNOWN_VOLID, 1, desired_volid);
	}
      goto exit_on_error;
    }

  /* 
   * Start a TOP SYSTEM OPERATION.
   * This top system operation will be either ABORTED (case of failure) or
   * its actions will be attached to its outer parent (new file) or committed
   * (old file)
   */

  if (log_start_system_op (thread_p) == NULL)
    {
      VPID_SET_NULL (first_alloc_vpid);
      return NULL;
    }

  vpid.volid = vfid->volid;
  vpid.pageid = vfid->fileid;

  fhdr_pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (fhdr_pgptr == NULL)
    {
      goto exit_on_error;
    }

  (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);

  fhdr = (FILE_HEADER *) (fhdr_pgptr + FILE_HEADER_OFFSET);

  file_type = fhdr->type;

  if (file_type == FILE_TEMP)
    {
      old_val = thread_set_check_interrupt (thread_p, false);
      restore_check_interrupt = true;
    }

  /* Must allocate from the desired volume make sure that the last allocation set contains the desired volume */
  allocset_pgptr =
    pgbuf_fix (thread_p, &fhdr->last_allocset_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (allocset_pgptr == NULL)
    {
      goto exit_on_error;
    }

  (void) pgbuf_check_page_ptype (thread_p, allocset_pgptr, PAGE_FTAB);

  allocset = (FILE_ALLOCSET *) ((char *) allocset_pgptr + fhdr->last_allocset_offset);
  if (allocset->volid != desired_volid && file_allocset_add_set (thread_p, fhdr_pgptr, desired_volid) != NO_ERROR)
    {
      goto exit_on_error;
    }

  pgbuf_unfix_and_init (thread_p, allocset_pgptr);

  /* 
   * Find last allocation set to allocate the new pages. If we cannot allocate
   * the new pages into this allocation set, we must return since need to
   * allocate the pages in the desired volid.
   */

  allocstate =
    file_allocset_alloc_pages (thread_p, fhdr_pgptr, &fhdr->last_allocset_vpid, fhdr->last_allocset_offset,
			       first_alloc_vpid, npages, near_vpid, NULL);
  if (allocstate == FILE_ALLOCSET_ALLOC_ERROR)
    {
      /* Unable to create the pages at this volume */
      goto exit_on_error;
    }

  if (allocstate == FILE_ALLOCSET_ALLOC_ZERO)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FILE_NOT_ENOUGH_PAGES_IN_VOLUME, 2,
	      fileio_get_volume_label (desired_volid, PEEK), npages);
      goto exit_on_error;
    }

  if (fun != NULL && (*fun) (vfid, file_type, first_alloc_vpid, npages, args) == false)
    {
      /* We must abort the allocation of the page since the user function failed */
      goto exit_on_error;
    }

  pgbuf_unfix_and_init (thread_p, fhdr_pgptr);

  /* 
   * Allocation of pages of old files are committed, so that they can be made
   * available to any transaction. Allocation of pages of new files are not
   * committed until the transaction commits
   */

  isfile_new = file_is_new_file (thread_p, vfid);
  if (isfile_new == FILE_ERROR)
    {
      goto exit_on_error;
    }

  if (isfile_new == FILE_NEW_FILE && file_type != FILE_TEMP && logtb_is_current_active (thread_p) == true)
    {
      log_end_system_op (thread_p, LOG_RESULT_TOPOP_ATTACH_TO_OUTER);
    }
  else
    {
      log_end_system_op (thread_p, LOG_RESULT_TOPOP_COMMIT);
    }

  if (restore_check_interrupt == true)
    {
      rv = thread_set_check_interrupt (thread_p, old_val);
    }

  return first_alloc_vpid;

exit_on_error:

  (void) log_end_system_op (thread_p, LOG_RESULT_TOPOP_ABORT);

  if (fhdr_pgptr)
    {
      pgbuf_unfix_and_init (thread_p, fhdr_pgptr);
    }

  if (allocset_pgptr)
    {
      pgbuf_unfix_and_init (thread_p, allocset_pgptr);
    }

  VPID_SET_NULL (first_alloc_vpid);

  if (restore_check_interrupt == true)
    {
      rv = thread_set_check_interrupt (thread_p, old_val);
    }

  return NULL;
}

/*
 * file_allocset_find_page () - Find the given page from the given allocation set
 *   return: DISK_VALID if page was found, DISK_INVALID if page was not found,
 *           DISK_ERROR if error occurred
 *   fhdr_pgptr(in): Page pointer to file header
 *   allocset_pgptr(out): Pointer to a page where allocation set is located
 *   allocset_offset(in): Location in allocset_pgptr where allocation set is
 *                        located
 *   pageid(in): The page to find
 *   fun(in): Function to be call if the entry is found
 *   args(in): Additional arguments to be passed to fun
 *
 * Note: If the page identifier is found the "fun" function is invoked with all
 *       information related to the file, its allocation set, and the pointer
 *       to the entry. If the page is not found, DISK_INVALID is returned.
 */
static DISK_ISVALID
file_allocset_find_page (THREAD_ENTRY * thread_p, PAGE_PTR fhdr_pgptr, PAGE_PTR allocset_pgptr, INT16 allocset_offset,
			 INT32 pageid, int (*fun) (THREAD_ENTRY * thread_p, FILE_HEADER * fhdr,
						   FILE_ALLOCSET * allocset, PAGE_PTR fhdr_pgptr,
						   PAGE_PTR allocset_pgptr, INT16 allocset_offset, PAGE_PTR pgptr,
						   INT32 * aid_ptr, INT32 pageid, void *args), void *args)
{
  FILE_HEADER *fhdr;
  FILE_ALLOCSET *allocset;
  INT32 *aid_ptr;		/* Pointer to a portion of allocated pageids table of given allocation set */
  INT32 *outptr;		/* Pointer to outside of portion of allocated pageids */
  PAGE_PTR pgptr = NULL;
  VPID vpid;
  DISK_ISVALID isfound = DISK_INVALID;

  (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);
  (void) pgbuf_check_page_ptype (thread_p, allocset_pgptr, PAGE_FTAB);

  fhdr = (FILE_HEADER *) (fhdr_pgptr + FILE_HEADER_OFFSET);

  allocset = (FILE_ALLOCSET *) ((char *) allocset_pgptr + allocset_offset);

  vpid = allocset->start_pages_vpid;
  while (isfound == DISK_INVALID && !VPID_ISNULL (&vpid))
    {
      pgptr =
	pgbuf_fix (thread_p, &vpid, OLD_PAGE, fun ? PGBUF_LATCH_WRITE : PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
      if (pgptr == NULL)
	{
	  goto exit_on_error;
	}

      (void) pgbuf_check_page_ptype (thread_p, pgptr, PAGE_FTAB);

      /* Calculate the portion of the set that we can look at */
      if (file_find_limits (pgptr, allocset, &aid_ptr, &outptr, FILE_PAGE_TABLE) != NO_ERROR)
	{
	  goto exit_on_error;
	}

      /* We need to do a sequential scan since we do not know where the page is stored. The pages are stored by the
       * allocation order */

      for (; aid_ptr < outptr; aid_ptr++)
	{
	  if (pageid == *aid_ptr)
	    {
	      isfound = DISK_VALID;
	      if (fun)
		{
		  if ((*fun) (thread_p, fhdr, allocset, fhdr_pgptr, allocset_pgptr, allocset_offset, pgptr, aid_ptr,
			      pageid, args) != NO_ERROR)
		    {
		      isfound = DISK_ERROR;
		    }
		}
	      break;
	    }
	}

      if (isfound == DISK_INVALID)
	{
	  /* Get next page */
	  if (VPID_EQ (&vpid, &allocset->end_pages_vpid))
	    {
	      VPID_SET_NULL (&vpid);
	    }
	  else
	    {
	      if (file_ftabvpid_next (fhdr, pgptr, &vpid) != NO_ERROR)
		{
		  goto exit_on_error;
		}
	    }
	}

      pgbuf_unfix_and_init (thread_p, pgptr);
    }

  return isfound;

exit_on_error:

  if (pgptr)
    {
      pgbuf_unfix_and_init (thread_p, pgptr);
    }

  return DISK_ERROR;
}

/*
 * file_allocset_dealloc_page () - Deallocate the given page from the given
 *                               allocation set
 *   return:
 *   fhdr(in): Pointer to file header
 *   allocset(in): The allocation set which describe the table
 *   fhdr_pgptr(in): Page pointer to file header
 *   allocset_pgptr(out): Pointer to a page where allocation set is located
 *   allocset_offset(in): Location in allocset_pgptr where allocation set is
 *                        located
 *   pgptr(in): Pointer to page where the pageid entry is located
 *   aid_ptr(in): Pointer to the pageid entry in the pgptr page
 *   ignore_pageid(in): The page to remove
 *   success(out): Wheater or not the deallocation took place
 *
 * Note: If the page does not belong to this allocation set, ER_FAILED is
 *       returned
 */
static int
file_allocset_dealloc_page (THREAD_ENTRY * thread_p, FILE_HEADER * fhdr, FILE_ALLOCSET * allocset, PAGE_PTR fhdr_pgptr,
			    PAGE_PTR allocset_pgptr, INT16 allocset_offset, PAGE_PTR pgptr, INT32 * aid_ptr,
			    INT32 ignore_pageid, void *args)
{
  int *ret;

  ret = (int *) args;

  *ret = file_allocset_dealloc_contiguous_pages (thread_p, fhdr, allocset, fhdr_pgptr, allocset_pgptr, allocset_offset,
						 pgptr, aid_ptr, 1);
  return *ret;
}

/*
 * file_allocset_dealloc_contiguous_pages () - Deallocate the given set of contiguous
 *                                    pages and entries from the given
 *                                    allocation set
 *   return: NO_ERROR
 *   fhdr(in): Pointer to file header
 *   allocset(in): The allocation set which describe the table
 *   fhdr_pgptr(in): Page pointer to file header
 *   allocset_pgptr(out): Pointer to a page where allocation set is located
 *   allocset_offset(in): Location in allocset_pgptr where allocation set is
 *                        located
 *   pgptr(in): Pointer to page where the pageid entries are located
 *   first_aid_ptr(out): Pointer to the first pageid entry in the pgptr page
 *   ncont_page_entries(in): Number of contiguous entries
 */
static int
file_allocset_dealloc_contiguous_pages (THREAD_ENTRY * thread_p, FILE_HEADER * fhdr, FILE_ALLOCSET * allocset,
					PAGE_PTR fhdr_pgptr, PAGE_PTR allocset_pgptr, INT16 allocset_offset,
					PAGE_PTR pgptr, INT32 * first_aid_ptr, INT32 ncont_page_entries)
{
  int ret = NO_ERROR;
  DISK_PAGE_TYPE page_type;
  bool old_val, rv;

  if (log_start_system_op (thread_p) == NULL)
    {
      return ER_FAILED;
    }

  old_val = thread_set_check_interrupt (thread_p, false);

  if (ret == NO_ERROR)
    {
      page_type = file_get_disk_page_type (fhdr->type);
      ret = disk_dealloc_page (thread_p, allocset->volid, *first_aid_ptr, ncont_page_entries, page_type);
    }

  if (ret == NO_ERROR)
    {
      ret = file_allocset_remove_contiguous_pages (thread_p, fhdr, allocset, fhdr_pgptr, allocset_pgptr,
						   allocset_offset, pgptr, first_aid_ptr, ncont_page_entries);
    }

  if (ret == NO_ERROR)
    {
      log_end_system_op (thread_p, LOG_RESULT_TOPOP_ATTACH_TO_OUTER);
    }
  else
    {
      log_end_system_op (thread_p, LOG_RESULT_TOPOP_ABORT);
    }

  rv = thread_set_check_interrupt (thread_p, old_val);

  perfmon_add_stat (thread_p, PSTAT_FILE_NUM_PAGE_DEALLOCS, ncont_page_entries);

  return ret;
}

/*
 * file_allocset_remove_pageid () - Remove the given page identifier from the page
 *                           allocation table of the given allocation set
 *   return:
 *   fhdr(in): Pointer to file header
 *   allocset(in): The allocation set which describe the table
 *   fhdr_pgptr(in): Page pointer to file header
 *   allocset_pgptr(out): Pointer to a page where allocation set is located
 *   allocset_offset(in): Location in allocset_pgptr where allocation set is
 *                        located
 *   pgptr(in): Pointer to page where the pageid entries are located
 *   aid_ptr(in): Pointer to the pageid entry in the pgptr page
 *   ignore_pageid(in): The page to remove
 *   ignore(in):
 */
static int
file_allocset_remove_pageid (THREAD_ENTRY * thread_p, FILE_HEADER * fhdr, FILE_ALLOCSET * allocset, PAGE_PTR fhdr_pgptr,
			     PAGE_PTR allocset_pgptr, INT16 allocset_offset, PAGE_PTR pgptr, INT32 * aid_ptr,
			     INT32 ignore_pageid, void *ignore)
{
  return file_allocset_remove_contiguous_pages (thread_p, fhdr, allocset, fhdr_pgptr, allocset_pgptr, allocset_offset,
						pgptr, aid_ptr, 1);
}

/*
 * file_allocset_remove_contiguous_pages () - Remove the given page identifier from the page
 *                               allocation table of the given allocation set
 *   return: NO_ERROR
 *   fhdr(in): Pointer to file header
 *   allocset(in): The allocation set which describe the table
 *   fhdr_pgptr(in): Page pointer to file header
 *   allocset_pgptr(out): Pointer to a page where allocation set is located
 *   allocset_offset(in): Location in allocset_pgptr where allocation set is
 *                        located
 *   pgptr(in): Pointer to page where the pageid entries are located
 *   aid_ptr(in): Pointer to the pageid entry in the pgptr page
 *   num_contpages(in): Number of contiguous page entries to remove
 */
static int
file_allocset_remove_contiguous_pages (THREAD_ENTRY * thread_p, FILE_HEADER * fhdr, FILE_ALLOCSET * allocset,
				       PAGE_PTR fhdr_pgptr, PAGE_PTR allocset_pgptr, INT16 allocset_offset,
				       PAGE_PTR pgptr, INT32 * aid_ptr, INT32 num_contpages)
{
  LOG_DATA_ADDR addr;
  int i;
  INT32 *mem;
  int ret = NO_ERROR;
  INT32 undo_data, redo_data;
  FILE_RECV_DELETE_PAGES postpone_data;
  LOG_TDES *tdes = LOG_FIND_CURRENT_TDES (thread_p);
  bool use_postpone = tdes->topops.type != LOG_TOPOPS_POSTPONE;
  const PAGEID delete_pageid_value = use_postpone ? NULL_PAGEID_MARKED_DELETED : NULL_PAGEID;
  INT32 prealloc_mem[FILE_PREALLOC_MEMSIZE] = {
    delete_pageid_value, delete_pageid_value,
    delete_pageid_value, delete_pageid_value,
    delete_pageid_value, delete_pageid_value,
    delete_pageid_value, delete_pageid_value,
    delete_pageid_value, delete_pageid_value,
    delete_pageid_value, delete_pageid_value,
    delete_pageid_value, delete_pageid_value,
    delete_pageid_value, delete_pageid_value,
    delete_pageid_value, delete_pageid_value,
    delete_pageid_value, delete_pageid_value
  };

  /* Safe guard: postpone system operation is allowed only if transaction state is
   * TRAN_UNACTIVE_COMMITTED_WITH_POSTPONE. */
  assert (tdes->topops.type != LOG_TOPOPS_POSTPONE || tdes->state == TRAN_UNACTIVE_COMMITTED_WITH_POSTPONE);

  (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);
  (void) pgbuf_check_page_ptype (thread_p, allocset_pgptr, PAGE_FTAB);

  if (num_contpages <= FILE_PREALLOC_MEMSIZE)
    {
      mem = prealloc_mem;
    }
  else
    {
      mem = (INT32 *) malloc (sizeof (*aid_ptr) * num_contpages);
      if (mem == NULL)
	{
	  /* Lack of memory. Do it by recursive calls using the preallocated memory. */
	  while (num_contpages > FILE_PREALLOC_MEMSIZE)
	    {
	      ret = file_allocset_remove_contiguous_pages (thread_p, fhdr, allocset, fhdr_pgptr, allocset_pgptr,
							   allocset_offset, pgptr, aid_ptr, FILE_PREALLOC_MEMSIZE);
	      if (ret != NO_ERROR)
		{
		  goto exit_on_error;
		}

	      aid_ptr += FILE_PREALLOC_MEMSIZE;
	      num_contpages -= FILE_PREALLOC_MEMSIZE;
	    }
	  mem = prealloc_mem;
	}
      else
	{
	  for (i = 0; i < num_contpages; i++)
	    {
	      mem[i] = delete_pageid_value;
	    }
	}
    }

  /* FROM now, set the identifier to NULL_PAGEID_MARKED_DELETED */
  addr.vfid = &fhdr->vfid;
  addr.pgptr = pgptr;
  addr.offset = CAST_BUFLEN ((char *) aid_ptr - (char *) pgptr);
  log_append_undoredo_data (thread_p, RVFL_IDSTABLE, &addr, sizeof (*aid_ptr) * num_contpages,
			    sizeof (*aid_ptr) * num_contpages, aid_ptr, mem);
  /* 
   * The actual deallocation is done after the transaction commits.
   *
   * The following MUST be DONE EXACTLY this way since log_append_postpone is
   * postponed at commit time...If we are committing at this moment, it will
   * run immediately. Don't use any tricks here such as using the page as
   * temporary area.
   */

  memcpy (aid_ptr, mem, sizeof (*aid_ptr) * num_contpages);

  if (use_postpone)
    {
      for (i = 0; i < num_contpages; i++)
	{
	  mem[i] = NULL_PAGEID;
	}
      log_append_postpone (thread_p, RVFL_IDSTABLE, &addr, sizeof (*aid_ptr) * num_contpages, mem);
    }

  if (mem != prealloc_mem)
    {
      free_and_init (mem);
    }

  pgbuf_set_dirty (thread_p, pgptr, DONT_FREE);

  allocset->num_pages -= num_contpages;
  allocset->num_holes += num_contpages;

  addr.pgptr = allocset_pgptr;
  addr.offset = allocset_offset;
  undo_data = num_contpages;
  redo_data = -num_contpages;
  log_append_undoredo_data (thread_p, RVFL_ALLOCSET_DELETE_PAGES, &addr, sizeof (undo_data), sizeof (redo_data),
			    &undo_data, &redo_data);
  pgbuf_set_dirty (thread_p, allocset_pgptr, DONT_FREE);

  fhdr->num_user_pages -= num_contpages;

  addr.pgptr = fhdr_pgptr;
  addr.offset = FILE_HEADER_OFFSET;
  undo_data = num_contpages;
  redo_data = -num_contpages;

  if (use_postpone)
    {
      fhdr->num_user_pages_mrkdelete += num_contpages;

      log_append_undoredo_data (thread_p, RVFL_FHDR_MARK_DELETED_PAGES, &addr, sizeof (undo_data), sizeof (redo_data),
				&undo_data, &redo_data);

      postpone_data.deleted_npages = num_contpages;
      postpone_data.need_compaction = 0;
      if (allocset->num_holes >= (int) NUM_HOLES_NEED_COMPACTION)
	{
	  postpone_data.need_compaction = 1;
	}

      log_append_postpone (thread_p, RVFL_FHDR_DELETE_PAGES, &addr, sizeof (postpone_data), &postpone_data);
    }
  else
    {
      log_append_undoredo_data (thread_p, RVFL_FHDR_UPDATE_NUM_USER_PAGES, &addr, sizeof (undo_data),
				sizeof (redo_data), &undo_data, &redo_data);

      if (VACUUM_IS_THREAD_VACUUM (thread_p) && allocset->num_holes >= (int) NUM_HOLES_NEED_COMPACTION
	  && fhdr->num_user_pages_mrkdelete == 0)
	{
	  /* Compress file. */
	  VFID vfid;
	  VPID *fhdr_vpidp = pgbuf_get_vpid_ptr (fhdr_pgptr);

	  vfid.volid = fhdr_vpidp->volid;
	  vfid.fileid = fhdr_vpidp->pageid;
	  ret = file_compress (thread_p, &vfid, false);
	  if (ret != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	}
    }
  pgbuf_set_dirty (thread_p, fhdr_pgptr, DONT_FREE);

  return ret;

exit_on_error:

  if (ret == NO_ERROR)
    {
      ret = ER_FAILED;
    }

  return ret;
}

/*
 * file_allocset_find_num_deleted () - Find the number of deleted and marked deleted
 *                           pages in the page allocation table
 *   return: num_deleted
 *   fhdr(in): File header of file
 *   allocset(in): The interested allocation set
 *   num_deleted(in): number of deleted pages
 *   num_marked_deleted(in): number of marked deleted pages
 */
static int
file_allocset_find_num_deleted (THREAD_ENTRY * thread_p, FILE_HEADER * fhdr, FILE_ALLOCSET * allocset, int *num_deleted,
				int *num_marked_deleted)
{
  INT32 *aid_ptr;		/* Pointer to a portion of allocated pageids table of given allocation set */
  INT32 *outptr;		/* Pointer to outside of portion of allocated pageids */
  PAGE_PTR pgptr = NULL;
  VPID vpid;

  *num_deleted = 0;
  *num_marked_deleted = 0;

  vpid = allocset->start_pages_vpid;
  while (!VPID_ISNULL (&vpid))
    {
      pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
      if (pgptr == NULL)
	{
	  goto exit_on_error;
	}

      (void) pgbuf_check_page_ptype (thread_p, pgptr, PAGE_FTAB);

      /* Calculate the portion of the set that we can look at */
      if (file_find_limits (pgptr, allocset, &aid_ptr, &outptr, FILE_PAGE_TABLE) != NO_ERROR)
	{
	  goto exit_on_error;
	}

      /* We need to do a sequential scan since we do not know where the page is stored. The pages are stored by the
       * allocation order */

      for (; aid_ptr < outptr; aid_ptr++)
	{
	  if (*aid_ptr == NULL_PAGEID)
	    {
	      *num_deleted += 1;
	    }
	  else if (*aid_ptr == NULL_PAGEID_MARKED_DELETED)
	    {
	      *num_marked_deleted += 1;
	    }
	}
      /* Get next page */
      if (VPID_EQ (&vpid, &allocset->end_pages_vpid))
	{
	  VPID_SET_NULL (&vpid);
	}
      else
	{
	  if (file_ftabvpid_next (fhdr, pgptr, &vpid) != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	}
      pgbuf_unfix_and_init (thread_p, pgptr);
    }

  return *num_deleted;

exit_on_error:

  if (pgptr)
    {
      pgbuf_unfix_and_init (thread_p, pgptr);
    }

  *num_deleted = -1;
  *num_marked_deleted = -1;

  return -1;
}

/*
 * file_dealloc_page () - Deallocate the given page for the given file identifier
 *   return:
 *   vfid(in): Complete file identifier
 *   dealloc_vpid(in): Identifier of page to deallocate
 */
int
file_dealloc_page (THREAD_ENTRY * thread_p, const VFID * vfid, VPID * dealloc_vpid, FILE_TYPE file_type)
{
  FILE_ALLOCSET *allocset;
  VPID allocset_vpid;
  INT16 allocset_offset;
  PAGE_PTR allocset_pgptr = NULL;
  PAGE_PTR fhdr_pgptr = NULL;
  FILE_IS_NEW_FILE isfile_new;
  DISK_ISVALID isfound = DISK_INVALID;
  int ret;
  FILE_TYPE file_type_from_cache;
  bool old_val = false;
  bool restore_check_interrupt = false;
  bool retrieve_from_cache;
  bool dummy_has_undo_log;
  bool rv;

  assert (false);

  if (FI_TEST (thread_p, FI_TEST_BTREE_MANAGER_PAGE_DEALLOC_FAIL, 1) != NO_ERROR)
    {
      return ER_FAILED;
    }

#if defined (NDEBUG)
  /* Release build: access cache only when caller does not provide file_type */
  retrieve_from_cache = (file_type == FILE_UNKNOWN_TYPE);
#else
  /* Debugging build: always check */
  retrieve_from_cache = true;
#endif

  if (retrieve_from_cache)
    {
      isfile_new = file_is_new_file_ext (thread_p, vfid, &file_type_from_cache, &dummy_has_undo_log);
      if (isfile_new == FILE_ERROR)
	{
	  return ER_FAILED;
	}

      assert (file_type == FILE_UNKNOWN_TYPE || (file_type != FILE_TEMP && file_type_from_cache != FILE_TEMP)
	      || (file_type == FILE_TEMP && file_type_from_cache == FILE_TEMP));

      file_type = file_type_from_cache;
    }

  if (logtb_is_current_active (thread_p) == false && file_type != FILE_TEMP)
    {
      if (retrieve_from_cache == false)
	{
	  isfile_new = file_is_new_file (thread_p, vfid);
	  if (isfile_new == FILE_ERROR)
	    {
	      return ER_FAILED;
	    }
	}

      if (isfile_new == FILE_NEW_FILE)
	{
	  /* 
	   * Pages of new files are removed by disk manager during the rollback
	   * process that we are currently executing (i.e., the transaction is not
	   * active at this moment).
	   * Don't need undo we are undoing!
	   */

	  LOG_DATA_ADDR addr;

	  addr.vfid = vfid;
	  addr.offset = -1;

	  addr.pgptr = pgbuf_fix (thread_p, dealloc_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
	  if (addr.pgptr != NULL)
	    {
	      log_append_redo_data (thread_p, RVFL_LOGICAL_NOOP, &addr, 0, NULL);
	      /* Even though this is a noop, we have to mark the page dirty in order to keep the expensive pgbuf_unfix
	       * checks from complaining. */
	      /* As of now, if there is no logic flaw and if transaction successfully commits, the page should no
	       * longer be used (until reallocated) and its BCB can be invalidated. */
	      pgbuf_set_dirty (thread_p, addr.pgptr, DONT_FREE);
	      pgbuf_invalidate (thread_p, addr.pgptr);
	      addr.pgptr = NULL;
	    }
	  return NO_ERROR;
	}
    }

  /* 
   * Start a TOP SYSTEM OPERATION.
   * This top system operation will be either ABORTED (case of failure) or
   * its actions will be attached to its outer parent
   */

  if (log_start_system_op (thread_p) == NULL)
    {
      return ER_FAILED;
    }

  if (file_type == FILE_TEMP)
    {
      old_val = thread_set_check_interrupt (thread_p, false);
      restore_check_interrupt = true;
    }

  ret = ER_FAILED;

  allocset_vpid.volid = vfid->volid;
  allocset_vpid.pageid = vfid->fileid;

  fhdr_pgptr = pgbuf_fix (thread_p, &allocset_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (fhdr_pgptr == NULL)
    {
      goto exit_on_error;
    }

  (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);

  allocset_offset = offsetof (FILE_HEADER, allocset);
  /* Go over each allocation set until the page is found */
  while (!VPID_ISNULL (&allocset_vpid))
    {
      /* Fetch the page for the allocation set description */
      allocset_pgptr = pgbuf_fix (thread_p, &allocset_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
      if (allocset_pgptr == NULL)
	{
	  goto exit_on_error;
	}

      (void) pgbuf_check_page_ptype (thread_p, allocset_pgptr, PAGE_FTAB);

      /* Get the allocation set */
      allocset = (FILE_ALLOCSET *) ((char *) allocset_pgptr + allocset_offset);
      if (allocset->volid == dealloc_vpid->volid)
	{
	  /* The page may be located in this set */
	  isfound =
	    file_allocset_find_page (thread_p, fhdr_pgptr, allocset_pgptr, allocset_offset, dealloc_vpid->pageid,
				     file_allocset_dealloc_page, &ret);
	}
      if (isfound == DISK_ERROR)
	{
	  goto exit_on_error;
	}
      else if (isfound == DISK_INVALID)
	{
	  /* We did not find it in the current allocation set. Get the next allocation set */
	  if (VPID_ISNULL (&allocset->next_allocset_vpid))
	    {
	      if (log_is_in_crash_recovery ())
		{
		  /* 
		   * assert check after server up
		   * because logical page dealloc (like, RVBT_NEW_PGALLOC) for undo recovery
		   * could be run twice.
		   */
		  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_FILE_TABLE_CORRUPTED, 2, vfid->volid, vfid->fileid);
		}
	      else
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FILE_TABLE_CORRUPTED, 2, vfid->volid, vfid->fileid);

		  assert_release (0);
		}

	      VPID_SET_NULL (&allocset_vpid);
	    }
	  else
	    {
	      if (VPID_EQ (&allocset_vpid, &allocset->next_allocset_vpid)
		  && allocset_offset == allocset->next_allocset_offset)
		{
		  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_FILE_FTB_LOOP, 4, vfid->fileid,
			  fileio_get_volume_label (vfid->volid, PEEK), allocset_vpid.volid, allocset_vpid.pageid);
		  VPID_SET_NULL (&allocset_vpid);
		}
	      else
		{
		  allocset_vpid = allocset->next_allocset_vpid;
		  allocset_offset = allocset->next_allocset_offset;
		}
	    }
	}
      else
	{
	  VPID_SET_NULL (&allocset_vpid);
	}
      pgbuf_unfix_and_init (thread_p, allocset_pgptr);
    }

  pgbuf_unfix_and_init (thread_p, fhdr_pgptr);

  log_end_system_op (thread_p, LOG_RESULT_TOPOP_ATTACH_TO_OUTER);

  if (restore_check_interrupt == true)
    {
      rv = thread_set_check_interrupt (thread_p, old_val);
    }

  return ret;

exit_on_error:

  (void) log_end_system_op (thread_p, LOG_RESULT_TOPOP_ABORT);

  if (fhdr_pgptr)
    {
      pgbuf_unfix_and_init (thread_p, fhdr_pgptr);
    }

  if (allocset_pgptr)
    {
      pgbuf_unfix_and_init (thread_p, allocset_pgptr);
    }

  if (restore_check_interrupt == true)
    {
      rv = thread_set_check_interrupt (thread_p, old_val);
    }

  if (ret == NO_ERROR)
    {
      ret = ER_FAILED;
    }

  return ret;
}

/*
 * file_truncate_to_numpages () - Truncate the file to the given number of pages
 *   return: NO_ERROR
 *   vfid(in): Complete file identifier
 *   keep_first_numpages(in): Number of pages to keep
 *
 * Note: The given file is truncated to the given number of pages. If the file
 *       has more than the number of given pages, those last pages are
 *       deallocated.
 */
int
file_truncate_to_numpages (THREAD_ENTRY * thread_p, const VFID * vfid, INT32 keep_first_numpages)
{
  VPID allocset_vpid;		/* Page-volume identifier of allocset */
  VPID ftb_vpid;		/* File table in allocation set */
  FILE_HEADER *fhdr;
  FILE_ALLOCSET *allocset;	/* The first allocation set */
  PAGE_PTR fhdr_pgptr = NULL;
  PAGE_PTR allocset_pgptr = NULL;
  PAGE_PTR pgptr = NULL;
  INT32 *aid_ptr;		/* Pointer to portion of table of page or sector allocation table */
  INT32 *outptr;		/* Out of bound pointer. */
  INT16 allocset_offset;
  INT32 *batch_first_aid_ptr;	/* First aid_ptr in batch of contiguous pages */
  INT32 batch_ndealloc;		/* Number of sectors to deallocate in the batch */
  int ret = NO_ERROR;

  /* 
   * Start a TOP SYSTEM OPERATION.
   * This top system operation will be either ABORTED (case of failure) or
   * its actions will be attached to its outer parent. That is, the file
   * cannot be destroyed half way.
   */

  if (log_start_system_op (thread_p) == NULL)
    {
      return ER_FAILED;
    }

  allocset_vpid.volid = vfid->volid;
  allocset_vpid.pageid = vfid->fileid;

  fhdr_pgptr = pgbuf_fix (thread_p, &allocset_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (fhdr_pgptr == NULL)
    {
      goto exit_on_error;
    }

  (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);

  fhdr = (FILE_HEADER *) (fhdr_pgptr + FILE_HEADER_OFFSET);

  if (fhdr->num_user_pages > keep_first_numpages)
    {
      allocset_offset = offsetof (FILE_HEADER, allocset);

      while (!VPID_ISNULL (&allocset_vpid))
	{
	  /* 
	   * Fetch the page that describe this allocation set even when it has
	   * already been fetched as a header page. This is done to code the
	   * following easily.
	   */
	  allocset_pgptr = pgbuf_fix (thread_p, &allocset_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
	  if (allocset_pgptr == NULL)
	    {
	      /* Something went wrong */
	      /* Cancel the lock and free the page */
	      goto exit_on_error;
	    }

	  (void) pgbuf_check_page_ptype (thread_p, allocset_pgptr, PAGE_FTAB);

	  allocset = (FILE_ALLOCSET *) ((char *) allocset_pgptr + allocset_offset);

	  if (allocset->num_pages <= keep_first_numpages)
	    {
	      keep_first_numpages -= allocset->num_pages;
	    }
	  else
	    {
	      ftb_vpid = allocset->start_pages_vpid;
	      while (!VPID_ISNULL (&ftb_vpid))
		{
		  /* 
		   * Fetch the page where the portion of the page table is
		   * located. Note that we are fetching the page even when it
		   * has been fetched previously as an allocation set. This is
		   * done to make the following code easy to write.
		   */
		  pgptr = pgbuf_fix (thread_p, &ftb_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
		  if (pgptr == NULL)
		    {
		      /* Something went wrong */
		      /* Cancel the lock and free the page */
		      goto exit_on_error;
		    }

		  (void) pgbuf_check_page_ptype (thread_p, pgptr, PAGE_FTAB);

		  /* Calculate the starting offset and length of the page to check */
		  ret = file_find_limits (pgptr, allocset, &aid_ptr, &outptr, FILE_PAGE_TABLE);
		  if (ret != NO_ERROR)
		    {
		      pgbuf_unfix_and_init (thread_p, pgptr);

		      /* Something went wrong */
		      /* Cancel the lock and free the page */
		      goto exit_on_error;
		    }

		  /* Deallocate the user pages in this table page of this allocation set. */

		  batch_first_aid_ptr = NULL;
		  batch_ndealloc = 0;

		  for (; aid_ptr < outptr; aid_ptr++)
		    {
		      if (*aid_ptr != NULL_PAGEID && *aid_ptr != NULL_PAGEID_MARKED_DELETED)
			{
			  if (keep_first_numpages > 0)
			    {
			      keep_first_numpages--;
			      continue;
			    }

			  if (batch_ndealloc == 0)
			    {
			      /* Start accumulating contiguous pages */
			      batch_first_aid_ptr = aid_ptr;
			      batch_ndealloc = 1;
			      continue;
			    }
			}
		      else if (batch_ndealloc == 0)
			{
			  continue;
			}

		      /* Is page contiguous and is it stored contiguous in allocation set */

		      if (*aid_ptr != NULL_PAGEID && *aid_ptr != NULL_PAGEID_MARKED_DELETED
			  && batch_first_aid_ptr != NULL && (*aid_ptr == *batch_first_aid_ptr + batch_ndealloc))
			{
			  /* contiguous */
			  batch_ndealloc++;
			}
		      else if (batch_first_aid_ptr != NULL)
			{
			  /* 
			   * This is not a contiguous page.
			   * Deallocate any previous pages and start
			   * accumulating contiguous pages again.
			   * We do not care if the page deallocation failed,
			   * since we are truncating the file.. Deallocate as
			   * much as we can.
			   */

			  ret = file_allocset_dealloc_contiguous_pages (thread_p, fhdr, allocset, fhdr_pgptr,
									allocset_pgptr, allocset_offset, pgptr,
									batch_first_aid_ptr, batch_ndealloc);
			  if (ret != NO_ERROR)
			    {
			      pgbuf_unfix_and_init (thread_p, pgptr);
			      goto exit_on_error;
			    }

			  /* Start again */
			  if (*aid_ptr != NULL_PAGEID && *aid_ptr != NULL_PAGEID_MARKED_DELETED)
			    {
			      batch_first_aid_ptr = aid_ptr;
			      batch_ndealloc = 1;
			    }
			  else
			    {
			      batch_first_aid_ptr = NULL;
			      batch_ndealloc = 0;
			    }

			}
		    }

		  if (batch_ndealloc > 0 && batch_first_aid_ptr != NULL)
		    {
		      /* Deallocate any accumulated pages We do not care if the page deallocation failed, since we are
		       * truncating the file.. Deallocate as much as we can. */

		      ret = file_allocset_dealloc_contiguous_pages (thread_p, fhdr, allocset, fhdr_pgptr,
								    allocset_pgptr, allocset_offset, pgptr,
								    batch_first_aid_ptr, batch_ndealloc);
		      if (ret != NO_ERROR)
			{
			  pgbuf_unfix_and_init (thread_p, pgptr);
			  goto exit_on_error;
			}
		    }

		  /* Get next page */
		  if (VPID_EQ (&ftb_vpid, &allocset->end_pages_vpid))
		    {
		      VPID_SET_NULL (&ftb_vpid);
		    }
		  else
		    {
		      ret = file_ftabvpid_next (fhdr, pgptr, &ftb_vpid);
		      if (ret != NO_ERROR)
			{
			  pgbuf_unfix_and_init (thread_p, pgptr);
			  goto exit_on_error;
			}
		    }

		  pgbuf_unfix_and_init (thread_p, pgptr);
		}
	    }

	  /* Next allocation set */
	  if (VPID_ISNULL (&allocset->next_allocset_vpid))
	    {
	      VPID_SET_NULL (&allocset_vpid);
	      allocset_offset = -1;
	    }
	  else
	    {
	      if (VPID_EQ (&allocset_vpid, &allocset->next_allocset_vpid)
		  && allocset_offset == allocset->next_allocset_offset)
		{
		  /* System error. It looks like we are in a loop */
		  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_FILE_FTB_LOOP, 4, vfid->fileid,
			  fileio_get_volume_label (vfid->volid, PEEK), allocset->next_allocset_vpid.volid,
			  allocset->next_allocset_vpid.pageid);
		  VPID_SET_NULL (&allocset_vpid);
		  allocset_offset = -1;
		}
	      else
		{
		  allocset_vpid = allocset->next_allocset_vpid;
		  allocset_offset = allocset->next_allocset_offset;
		}
	    }
	  pgbuf_unfix_and_init (thread_p, allocset_pgptr);
	}
    }

  pgbuf_unfix_and_init (thread_p, fhdr_pgptr);

  /* The responsability of the deallocation of the pages is given to the outer nested level */
  log_end_system_op (thread_p, LOG_RESULT_TOPOP_ATTACH_TO_OUTER);

  return ret;

exit_on_error:

  if (allocset_pgptr)
    {
      pgbuf_unfix_and_init (thread_p, allocset_pgptr);
    }

  /* ABORT THE TOP SYSTEM OPERATION. That is, the deletion of the file is aborted, all pages that were deallocated are
   * undone..  */
  (void) log_end_system_op (thread_p, LOG_RESULT_TOPOP_ABORT);

  if (fhdr_pgptr)
    {
      pgbuf_unfix_and_init (thread_p, fhdr_pgptr);
    }

  if (ret == NO_ERROR)
    {
      ret = ER_FAILED;
    }

  return ret;
}

/*
 * file_allocset_remove () - Remove given allocation set
 *   return: NO_ERROR
 *   fhdr_pgptr(in): Page pointer to file header
 *   prev_allocset_vpid(in): Page address where the previous allocation set is
 *                           located
 *   prev_allocset_offset(in): Offset in page address where the previous
 *                             allocation set is located
 *   allocset_vpid(in): Page address where the current allocation set is
 *                      located
 *   allocset_offset(in): Location in allocset page where allocation set is
 *                        located
 *
 * Note: If this is the only allocation set in the file, the deletion is
 *       ignored
 */
static int
file_allocset_remove (THREAD_ENTRY * thread_p, PAGE_PTR fhdr_pgptr, VPID * prev_allocset_vpid,
		      INT16 prev_allocset_offset, VPID * allocset_vpid, INT16 allocset_offset)
{
  FILE_HEADER *fhdr;
  PAGE_PTR allocset_pgptr = NULL;
  FILE_ALLOCSET *allocset;
  bool islast_allocset;
  LOG_DATA_ADDR addr;
  VPID vpid;
  PAGE_PTR pgptr = NULL;
  INT32 *aid_ptr;		/* Pointer to portion of sector table */
  INT32 *outptr;		/* Out of bound pointer. */
  INT32 batch_firstid;		/* First sectid in batch */
  INT32 batch_ndealloc;		/* Number of sectors to deallocate in the batch */
  FILE_RECV_ALLOCSET_LINK recv_undo;
  FILE_RECV_ALLOCSET_LINK recv_redo;
  int ret = NO_ERROR;

  (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);

  fhdr = (FILE_HEADER *) (fhdr_pgptr + FILE_HEADER_OFFSET);

  addr.vfid = &fhdr->vfid;

  /* Don't destroy this allocation set if this is the only remaining set in the file */

  if (fhdr->num_allocsets == 1)
    {
      goto exit_on_error;
    }

  allocset_pgptr = pgbuf_fix (thread_p, allocset_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (allocset_pgptr == NULL)
    {
      goto exit_on_error;
    }

  (void) pgbuf_check_page_ptype (thread_p, allocset_pgptr, PAGE_FTAB);

  allocset = (FILE_ALLOCSET *) ((char *) allocset_pgptr + allocset_offset);

  /* Deallocate all sectors assigned to this allocation set */

  if (allocset->num_sects > 0)
    {
      vpid = allocset->start_sects_vpid;
      while (!VPID_ISNULL (&vpid) && ret == NO_ERROR)
	{
	  pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
	  if (pgptr == NULL)
	    {
	      ret = ER_FAILED;
	      break;
	    }

	  (void) pgbuf_check_page_ptype (thread_p, pgptr, PAGE_FTAB);

	  /* Calculate the starting offset and length of the page to check */
	  ret = file_find_limits (pgptr, allocset, &aid_ptr, &outptr, FILE_SECTOR_TABLE);
	  if (ret != NO_ERROR)
	    {
	      pgbuf_unfix_and_init (thread_p, pgptr);
	      break;
	    }

	  /* Deallocate all user sectors in this sector table of this allocation set. The sectors are deallocated in
	   * batches by their contiguity */

	  batch_firstid = NULL_SECTID;
	  batch_ndealloc = 0;

	  for (; aid_ptr < outptr && ret == NO_ERROR; aid_ptr++)
	    {
	      if (*aid_ptr != NULL_SECTID)
		{
		  if (batch_ndealloc == 0)
		    {
		      /* Start accumulating contiguous sectors */
		      batch_firstid = *aid_ptr;
		      batch_ndealloc = 1;
		    }
		  else
		    {
		      /* Is sector contiguous ? */
		      if (*aid_ptr == (batch_firstid + batch_ndealloc))
			{
			  /* contiguous */
			  batch_ndealloc++;
			}
		      else
			{
			  /* 
			   * This is not a contiguous sector.
			   * Deallocate any previous sectors and start
			   * accumulating contiguous sectors again.
			   * We do not care if the sector deallocation failed,
			   * since we are destroying the file.. Deallocate as
			   * much as we can.
			   */
			  ret = disk_dealloc_sector (thread_p, allocset->volid, batch_firstid, batch_ndealloc);
			  /* Start again */
			  batch_firstid = *aid_ptr;
			  batch_ndealloc = 1;
			}
		    }
		}
	    }

	  if (batch_ndealloc > 0 && ret == NO_ERROR)
	    {
	      /* Deallocate any accumulated sectors */
	      ret = disk_dealloc_sector (thread_p, allocset->volid, batch_firstid, batch_ndealloc);
	    }

	  /* Get next page */
	  if (VPID_EQ (&vpid, &allocset->end_sects_vpid))
	    {
	      VPID_SET_NULL (&vpid);
	    }
	  else
	    {
	      ret = file_ftabvpid_next (fhdr, pgptr, &vpid);
	      if (ret != NO_ERROR)
		{
		  pgbuf_unfix_and_init (thread_p, pgptr);
		  break;
		}
	    }
	  pgbuf_unfix_and_init (thread_p, pgptr);
	}
    }

  /* If there was a failure, finish at this moment */
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  /* Is this the first allocation set ? */
  if (fhdr->vfid.volid == allocset_vpid->volid && fhdr->vfid.fileid == allocset_vpid->pageid
      && (int) offsetof (FILE_HEADER, allocset) == allocset_offset)
    {
      pgbuf_unfix_and_init (thread_p, allocset_pgptr);
      goto end;
    }

  /* 
   * Note: that we did not need to modify the allocation set. If undo, the
   *       deallocation of the sectors are undo. If redo they are destroyed
   */

  /* Is this the last allocation set ? */
  if (VPID_EQ (&fhdr->last_allocset_vpid, allocset_vpid) && fhdr->last_allocset_offset == allocset_offset)
    {
      islast_allocset = true;
    }
  else
    {
      islast_allocset = false;
    }

  /* Get the address of next allocation set */
  *allocset_vpid = allocset->next_allocset_vpid;
  allocset_offset = allocset->next_allocset_offset;

  /* Now free the page where the new allocation set is removed and modify the previous allocation set to point to next
   * available allocation set */

  pgbuf_unfix_and_init (thread_p, allocset_pgptr);

  /* Get the previous allocation set */
  allocset_pgptr = pgbuf_fix (thread_p, prev_allocset_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (allocset_pgptr == NULL)
    {
      goto exit_on_error;
    }

  (void) pgbuf_check_page_ptype (thread_p, allocset_pgptr, PAGE_FTAB);

  allocset = (FILE_ALLOCSET *) ((char *) allocset_pgptr + prev_allocset_offset);

  /* Save the information that is going to be changed for undo purposes */
  recv_undo.next_allocset_vpid = allocset->next_allocset_vpid;
  recv_undo.next_allocset_offset = allocset->next_allocset_offset;

  /* Perform the change */
  allocset->next_allocset_vpid = *allocset_vpid;
  allocset->next_allocset_offset = allocset_offset;

  /* Get the information changed for redo purposes */
  recv_redo.next_allocset_vpid = allocset->next_allocset_vpid;
  recv_redo.next_allocset_offset = allocset->next_allocset_offset;

  /* Now log the information */
  addr.pgptr = allocset_pgptr;
  addr.offset = prev_allocset_offset;
  log_append_undoredo_data (thread_p, RVFL_ALLOCSET_LINK, &addr, sizeof (recv_undo), sizeof (recv_redo), &recv_undo,
			    &recv_redo);

  pgbuf_set_dirty (thread_p, allocset_pgptr, FREE);
  allocset_pgptr = NULL;

  /* If the deleted allocation set was the last allocated set, change the header of the file to point to previous
   * allocation set */

  fhdr->num_allocsets -= 1;

  if (islast_allocset == true)
    {
      /* Header must point to previous allocation set */
      recv_undo.next_allocset_vpid = fhdr->last_allocset_vpid;
      recv_undo.next_allocset_offset = fhdr->last_allocset_offset;

      /* Get the information changed for redo purposes */
      recv_redo.next_allocset_vpid = *prev_allocset_vpid;
      recv_redo.next_allocset_offset = prev_allocset_offset;

      /* Execute the change */
      fhdr->last_allocset_vpid = *prev_allocset_vpid;
      fhdr->last_allocset_offset = prev_allocset_offset;
    }
  else
    {
      /* Header will continue pointing to the same last allocation set */
      recv_undo.next_allocset_vpid = fhdr->last_allocset_vpid;
      recv_undo.next_allocset_offset = fhdr->last_allocset_offset;

      recv_redo.next_allocset_vpid = fhdr->last_allocset_vpid;
      recv_redo.next_allocset_offset = fhdr->last_allocset_offset;
    }

  /* Log the changes */
  addr.pgptr = fhdr_pgptr;
  addr.offset = FILE_HEADER_OFFSET;
  log_append_undoredo_data (thread_p, RVFL_FHDR_REMOVE_LAST_ALLOCSET, &addr, sizeof (recv_undo), sizeof (recv_redo),
			    &recv_undo, &recv_redo);
  pgbuf_set_dirty (thread_p, fhdr_pgptr, DONT_FREE);

end:

  return ret;

exit_on_error:

  if (allocset_pgptr)
    {
      pgbuf_unfix_and_init (thread_p, allocset_pgptr);
    }

  if (ret == NO_ERROR)
    {
      ret = ER_FAILED;
    }

  return ret;
}

/* TODO: return val logic is modified by ksseo - DO NOT DELETE ME */
/*
 * file_allocset_reuse_last_set () - The last set is deallocated and its body is
 *                                redefined with the given volume
 *   return:
 *   fhdr_pgptr(in): Page pointer to file header
 *   new_volid(in): Set will use this volid from now on
 */
static int
file_allocset_reuse_last_set (THREAD_ENTRY * thread_p, PAGE_PTR fhdr_pgptr, INT16 new_volid)
{
  FILE_HEADER *fhdr;
  FILE_ALLOCSET *allocset;
  PAGE_PTR allocset_pgptr = NULL;
  LOG_DATA_ADDR addr;
  VPID vpid;
  PAGE_PTR pgptr = NULL;
  INT32 *aid_ptr;		/* Pointer to portion of sector table */
  INT32 *outptr;		/* Out of bound pointer. */
  INT32 batch_firstid;		/* First sectid in batch */
  INT32 batch_ndealloc;		/* Number of sectors to deallocate in the batch */
  int ret = NO_ERROR;

  (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);

  fhdr = (FILE_HEADER *) (fhdr_pgptr + FILE_HEADER_OFFSET);

  addr.vfid = &fhdr->vfid;

  allocset_pgptr =
    pgbuf_fix (thread_p, &fhdr->last_allocset_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (allocset_pgptr == NULL)
    {
      goto exit_on_error;
    }

  (void) pgbuf_check_page_ptype (thread_p, allocset_pgptr, PAGE_FTAB);

  allocset = (FILE_ALLOCSET *) ((char *) allocset_pgptr + fhdr->last_allocset_offset);

  /* Deallocate all sectors assigned to this allocation set */

  if (allocset->num_sects > 0)
    {
      vpid = allocset->start_sects_vpid;
      while (!VPID_ISNULL (&vpid))
	{
	  pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
	  if (pgptr == NULL)
	    {
	      goto exit_on_error;
	    }

	  (void) pgbuf_check_page_ptype (thread_p, pgptr, PAGE_FTAB);

	  /* Calculate the starting offset and length of the page to check */
	  ret = file_find_limits (pgptr, allocset, &aid_ptr, &outptr, FILE_SECTOR_TABLE);
	  if (ret != NO_ERROR)
	    {
	      pgbuf_unfix_and_init (thread_p, pgptr);
	      goto exit_on_error;
	    }

	  /* Deallocate all user sectors in this sector table of this allocation set. The sectors are deallocated in
	   * batches by their contiguity */

	  batch_firstid = NULL_SECTID;
	  batch_ndealloc = 0;

	  for (; aid_ptr < outptr; aid_ptr++)
	    {
	      if (*aid_ptr != NULL_SECTID)
		{
		  if (batch_ndealloc == 0)
		    {
		      /* Start accumulating contiguous sectors */
		      batch_firstid = *aid_ptr;
		      batch_ndealloc = 1;
		    }
		  else
		    {
		      /* Is sector contiguous ? */
		      if (*aid_ptr == (batch_firstid + batch_ndealloc))
			{
			  /* contiguous */
			  batch_ndealloc++;
			}
		      else
			{
			  /* 
			   * This is not a contiguous sector.
			   * Deallocate any previous sectors and start
			   * accumulating contiguous sectors again.
			   * We do not care if the sector deallocation failed,
			   * since we are destroying the file.. Deallocate as
			   * much as we can.
			   */
			  (void) disk_dealloc_sector (thread_p, allocset->volid, batch_firstid, batch_ndealloc);
			  /* Start again */
			  batch_firstid = *aid_ptr;
			  batch_ndealloc = 1;
			}
		    }
		}
	    }

	  if (batch_ndealloc > 0)
	    {
	      /* Deallocate any accumulated sectors */
	      (void) disk_dealloc_sector (thread_p, allocset->volid, batch_firstid, batch_ndealloc);
	    }

	  /* Get next page */
	  if (VPID_EQ (&vpid, &allocset->end_sects_vpid))
	    {
	      VPID_SET_NULL (&vpid);
	    }
	  else
	    {
	      ret = file_ftabvpid_next (fhdr, pgptr, &vpid);
	      if (ret != NO_ERROR)
		{
		  pgbuf_unfix_and_init (thread_p, pgptr);
		  goto exit_on_error;
		}
	    }
	  pgbuf_unfix_and_init (thread_p, pgptr);
	}
    }

  /* Reinitialize the sector with new information */

  addr.pgptr = allocset_pgptr;
  addr.offset = fhdr->last_allocset_offset;

  log_append_undo_data (thread_p, RVFL_ALLOCSET_NEW, &addr, sizeof (*allocset), allocset);

  allocset->volid = new_volid;
  allocset->num_sects = 0;
  allocset->num_pages = 0;
  allocset->curr_sectid = NULL_SECTID;
  allocset->num_holes = 0;

  /* Beginning address of sector and page allocation table is left the same */

  allocset->end_sects_vpid = allocset->start_sects_vpid;
  allocset->end_sects_offset = allocset->start_sects_offset;

  allocset->end_pages_vpid = allocset->start_pages_vpid;
  allocset->end_pages_offset = allocset->start_pages_offset;

  log_append_redo_data (thread_p, RVFL_ALLOCSET_NEW, &addr, sizeof (*allocset), allocset);
  pgbuf_set_dirty (thread_p, allocset_pgptr, FREE);
  allocset_pgptr = NULL;

  return ret;

exit_on_error:

  if (allocset_pgptr)
    {
      pgbuf_unfix_and_init (thread_p, allocset_pgptr);
    }

  if (ret == NO_ERROR)
    {
      ret = ER_FAILED;
    }

  return ret;
}

/* TODO: list for file_ftab_chain */
/*
 * file_allocset_compact_page_table () - Compact the page allocation table for
 *                                     the given allocation set
 *   return: NO_ERROR
 *   fhdr_pgptr(in): Page pointer to file header
 *   allocset_pgptr(out): Pointer to a page where allocation set is located
 *   allocset_offset(in): Location in allocset page where allocation set is
 *                        located
 *   rm_freespace_sectors(in): Remove free space for future sectors. Space for
 *                             future sectors should left only for the last
 *                             allocation set since page allocation is always
 *                             done from the last allocation set
 *
 * Note: The sector table may also be compacted when we do not need to leave
 *       space for future sectors. The free space for future sectors is located
 *       after the end of the sector table and before the start of the page
 *       table.
 */
static int
file_allocset_compact_page_table (THREAD_ENTRY * thread_p, PAGE_PTR fhdr_pgptr, PAGE_PTR allocset_pgptr,
				  INT16 allocset_offset, bool rm_freespace_sectors)
{
  FILE_HEADER *fhdr;
  FILE_ALLOCSET *allocset;
  FILE_FTAB_CHAIN *chain;	/* Structure for linking file table pages */
  FILE_FTAB_CHAIN rv_undo_chain;	/* Old information before changes to chain of pages */
  VPID to_vpid;
  VPID from_vpid;
  VPID vpid;
  VPID *allocset_vpidptr;
  INT32 *to_aid_ptr;		/* Pageid pointer array for to-page */
  INT32 *to_outptr;		/* Out of pointer array for to-page */
  PAGE_PTR to_pgptr = NULL;
  PGLENGTH to_start_offset;
  INT32 *from_aid_ptr;		/* Pageid pointer array for from-page */
  INT32 *from_outptr;		/* Out of pointer array for from-page */
  PAGE_PTR from_pgptr = NULL;
  int length;
  int to_length, from_length;
  int num_ftb_pages_deleted;
  LOG_DATA_ADDR addr;
  FILE_RECV_EXPAND_SECTOR recv_undo;
  FILE_RECV_EXPAND_SECTOR recv_redo;
  FILE_RECV_FTB_EXPANSION recv_ftb_undo;
  FILE_RECV_FTB_EXPANSION recv_ftb_redo;
  int ret = NO_ERROR;
  DISK_PAGE_TYPE page_type;

  (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);
  (void) pgbuf_check_page_ptype (thread_p, allocset_pgptr, PAGE_FTAB);

  fhdr = (FILE_HEADER *) (fhdr_pgptr + FILE_HEADER_OFFSET);

  addr.vfid = &fhdr->vfid;

  allocset = (FILE_ALLOCSET *) ((char *) allocset_pgptr + allocset_offset);
  /* Do we need to compact anything ? */
  if (allocset->num_holes <= 0)
    {
      /* we may not need to compact anything */
      if (rm_freespace_sectors == 0
	  || (VPID_EQ (&allocset->end_sects_vpid, &allocset->start_pages_vpid)
	      && allocset->end_sects_offset == allocset->start_pages_offset))
	{
	  /* Nothing to compact */
	  return NO_ERROR;
	}
    }

  /* Save allocation set information that WILL be changed, for UNDO purposes */
  recv_undo.start_pages_vpid = allocset->start_pages_vpid;
  recv_undo.end_pages_vpid = allocset->end_pages_vpid;
  recv_undo.start_pages_offset = allocset->start_pages_offset;
  recv_undo.end_pages_offset = allocset->end_pages_offset;
  recv_undo.num_holes = allocset->num_holes;

  if (rm_freespace_sectors == true
      && (!VPID_EQ (&allocset->start_pages_vpid, &allocset->end_sects_vpid)
	  || allocset->start_pages_offset != allocset->end_sects_offset) && allocset->start_sects_offset < DB_PAGESIZE)
    {
      /* The page table must be moved to the end of the sector table since we do not need to leave space for new
       * sectors */

      /* Get the from information before we reset the to information */
      from_vpid = allocset->start_pages_vpid;
      from_pgptr = pgbuf_fix (thread_p, &from_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
      if (from_pgptr == NULL)
	{
	  goto exit_on_error;
	}

      (void) pgbuf_check_page_ptype (thread_p, from_pgptr, PAGE_FTAB);

      /* Find the location for portion of page table */
      ret = file_find_limits (from_pgptr, allocset, &from_aid_ptr, &from_outptr, FILE_PAGE_TABLE);
      if (ret != NO_ERROR)
	{
	  goto exit_on_error;
	}
      /* Now reset the to information and get the to limits */
      allocset->start_pages_vpid = allocset->end_sects_vpid;
      allocset->start_pages_offset = allocset->end_sects_offset;

      to_vpid = allocset->start_pages_vpid;
      to_pgptr = pgbuf_fix (thread_p, &to_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
      if (to_pgptr == NULL)
	{
	  goto exit_on_error;
	}

      (void) pgbuf_check_page_ptype (thread_p, to_pgptr, PAGE_FTAB);

      /* Find the location for portion of page table */
      ret = file_find_limits (to_pgptr, allocset, &to_aid_ptr, &to_outptr, FILE_PAGE_TABLE);
      if (ret != NO_ERROR)
	{
	  goto exit_on_error;
	}
    }
  else
    {
      /* Start the shift at the first hole (NULL_PAGEID) in the page table */
      to_vpid = allocset->start_pages_vpid;
      to_pgptr = pgbuf_fix (thread_p, &to_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
      if (to_pgptr == NULL)
	{
	  goto exit_on_error;
	}

      (void) pgbuf_check_page_ptype (thread_p, to_pgptr, PAGE_FTAB);

      /* Find the location for portion of page table */
      ret = file_find_limits (to_pgptr, allocset, &to_aid_ptr, &to_outptr, FILE_PAGE_TABLE);
      if (ret != NO_ERROR)
	{
	  goto exit_on_error;
	}

      /* Nothing to move until we find the first hole */

      length = 0;
      while (length == 0 && (!VPID_EQ (&to_vpid, &allocset->end_pages_vpid) || to_aid_ptr <= to_outptr))
	{
	  /* Boundary condition on to-page ? */
	  if (to_aid_ptr >= to_outptr)
	    {
#ifdef FILE_DEBUG
	      if (to_aid_ptr > to_outptr)
		{
		  er_log_debug (ARG_FILE_LINE,
				"file_allocset_compact_page_table: *** Boundary condition system error ***\n");
		}
#endif /* FILE_DEBUG */
	      /* Get next page */
	      if (VPID_EQ (&to_vpid, &allocset->end_pages_vpid))
		{
		  break;
		}
	      else
		{
		  ret = file_ftabvpid_next (fhdr, to_pgptr, &to_vpid);
		  if (ret != NO_ERROR)
		    {
		      goto exit_on_error;
		    }
		}

	      pgbuf_unfix_and_init (thread_p, to_pgptr);
	      to_pgptr = pgbuf_fix (thread_p, &to_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
	      if (to_pgptr == NULL)
		{
		  goto exit_on_error;
		}

	      (void) pgbuf_check_page_ptype (thread_p, to_pgptr, PAGE_FTAB);

	      /* Find the location for portion of page table */
	      ret = file_find_limits (to_pgptr, allocset, &to_aid_ptr, &to_outptr, FILE_PAGE_TABLE);
	      if (ret != NO_ERROR)
		{
		  goto exit_on_error;
		}
	    }

	  while (to_aid_ptr < to_outptr)
	    {
	      if (*to_aid_ptr == NULL_PAGEID)
		{
		  /* The first hole (NULL_PAGEID) has been found */
		  length = 1;
		  break;
		}
	      else
		{
		  to_aid_ptr++;
		}
	    }
	}

      /* Find the location for the from part. Same location than to_page */

      from_vpid = to_vpid;
      from_aid_ptr = to_aid_ptr;
      from_outptr = to_outptr;

      /* Fetch the page again for the from part, so that our code can be written easily */
      from_pgptr = pgbuf_fix (thread_p, &from_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
      if (from_pgptr == NULL)
	{
	  goto exit_on_error;
	}

      (void) pgbuf_check_page_ptype (thread_p, from_pgptr, PAGE_FTAB);
    }

  /* Now start the compacting process. Eliminate empty holes (NULL_PAGEIDs) */

  /* Before we move anything to the to_page, we need to log whatever is there just in case of a crash.. we will need to 
   * recover the shift... */

  to_start_offset = (PGLENGTH) ((char *) to_aid_ptr - (char *) to_pgptr);

  addr.pgptr = to_pgptr;
  addr.offset = to_start_offset;
  log_append_undo_data (thread_p, RVFL_IDSTABLE, &addr, CAST_BUFLEN (((char *) to_outptr - (char *) to_aid_ptr)),
			to_aid_ptr);
  pgbuf_set_dirty (thread_p, to_pgptr, DONT_FREE);	/* to make it sure */
  length = 0;

  while (!VPID_EQ (&from_vpid, &allocset->end_pages_vpid) || from_aid_ptr <= from_outptr)
    {
      /* Boundary condition on from-page ? */
      if (from_aid_ptr >= from_outptr)
	{
#ifdef FILE_DEBUG
	  if (from_aid_ptr > from_outptr)
	    {
	      er_log_debug (ARG_FILE_LINE,
			    "file_allocset_compact_page_table: *** Boundary condition system error ***\n");
	    }
#endif /* FILE_DEBUG */
	  /* Get next page */
	  if (VPID_EQ (&from_vpid, &allocset->end_pages_vpid))
	    {
	      break;
	    }
	  else
	    {
	      ret = file_ftabvpid_next (fhdr, from_pgptr, &from_vpid);
	      if (ret != NO_ERROR)
		{
		  goto exit_on_error;
		}
	    }

	  pgbuf_unfix_and_init (thread_p, from_pgptr);
	  from_pgptr = pgbuf_fix (thread_p, &from_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
	  if (from_pgptr == NULL)
	    {
	      goto exit_on_error;
	    }

	  (void) pgbuf_check_page_ptype (thread_p, from_pgptr, PAGE_FTAB);

	  /* Find the location for portion of page table */
	  ret = file_find_limits (from_pgptr, allocset, &from_aid_ptr, &from_outptr, FILE_PAGE_TABLE);
	  if (ret != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	}

      /* Boundary condition on to-page ? */
      if (to_aid_ptr >= to_outptr)
	{
#ifdef FILE_DEBUG
	  if (to_aid_ptr > to_outptr)
	    {
	      er_log_debug (ARG_FILE_LINE,
			    "file_allocset_compact_page_table: *** Boundary condition system error ***\n");
	    }
#endif /* FILE_DEBUG */

	  /* Was the to_page modified ? */
	  if (length != 0)
	    {
	      addr.pgptr = to_pgptr;
	      addr.offset = to_start_offset;
	      CAST_TO_PAGEARRAY (to_aid_ptr, to_pgptr, addr.offset);
	      log_append_redo_data (thread_p, RVFL_IDSTABLE, &addr, length, to_aid_ptr);
	      pgbuf_set_dirty (thread_p, to_pgptr, DONT_FREE);
	    }

	  /* Get next page */
	  if (VPID_EQ (&to_vpid, &allocset->end_pages_vpid))
	    {
	      break;
	    }
	  else
	    {
	      ret = file_ftabvpid_next (fhdr, to_pgptr, &to_vpid);
	      if (ret != NO_ERROR)
		{
		  goto exit_on_error;
		}
	    }

	  pgbuf_unfix_and_init (thread_p, to_pgptr);
	  to_pgptr = pgbuf_fix (thread_p, &to_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
	  if (to_pgptr == NULL)
	    {
	      goto exit_on_error;
	    }

	  (void) pgbuf_check_page_ptype (thread_p, to_pgptr, PAGE_FTAB);

	  /* Find the location for portion of page table */
	  ret = file_find_limits (to_pgptr, allocset, &to_aid_ptr, &to_outptr, FILE_PAGE_TABLE);
	  if (ret != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	  /* 
	   * Before we move anything to the to_page, we need to log whatever
	   * is there just in case of a crash.. we will need to recover the
	   * shift...
	   */

	  to_start_offset = (PGLENGTH) ((char *) to_aid_ptr - (char *) to_pgptr);
	  addr.pgptr = to_pgptr;
	  addr.offset = to_start_offset;
	  log_append_undo_data (thread_p, RVFL_IDSTABLE, &addr, CAST_BUFLEN ((char *) to_outptr - (char *) to_aid_ptr),
				to_aid_ptr);
	  pgbuf_set_dirty (thread_p, to_pgptr, DONT_FREE);	/* to make it sure */
	  length = 0;
	}

      if (allocset->num_holes > 0)
	{
	  /* Compact as much as possible until a boundary condition is reached */
	  while (to_aid_ptr < to_outptr && from_aid_ptr < from_outptr)
	    {
	      /* can we compact this entry ?.. if from_apageid_ptr is NULL_PAGEID, the netry is compacted */
	      if (*from_aid_ptr != NULL_PAGEID)
		{
		  length += sizeof (*to_aid_ptr);
		  *to_aid_ptr = *from_aid_ptr;
		  to_aid_ptr++;
		}
	      from_aid_ptr++;
	    }
	}
      else
	{
	  /* Don't need to check for holes. We can copy directly */
	  from_length = CAST_BUFLEN ((char *) from_outptr - (char *) from_aid_ptr);
	  to_length = CAST_BUFLEN ((char *) to_outptr - (char *) to_aid_ptr);
	  if (to_length >= from_length)
	    {
	      /* The whole from length can be copied */
	      memmove (to_aid_ptr, from_aid_ptr, from_length);
	      length += from_length;
	      /* Note that we cannot increase the length to the pointer since the length is in bytes and the pointer
	       * points to 4 bytes */
	      to_aid_ptr = (INT32 *) ((char *) to_aid_ptr + from_length);
	      from_aid_ptr = (INT32 *) ((char *) from_aid_ptr + from_length);
	    }
	  else
	    {
	      /* Only a portion of the from length can be copied */
	      memmove (to_aid_ptr, from_aid_ptr, to_length);
	      length += to_length;
	      /* Note that we cannot increase the length to the pointer since the length is in bytes and the pointer
	       * points to 4 bytes */
	      to_aid_ptr = (INT32 *) ((char *) to_aid_ptr + to_length);
	      from_aid_ptr = (INT32 *) ((char *) from_aid_ptr + to_length);
	    }
	}
    }

  pgbuf_unfix_and_init (thread_p, from_pgptr);

  /* If last allocation set, remove any of the unused file table pages */
  allocset_vpidptr = pgbuf_get_vpid_ptr (allocset_pgptr);
  page_type = file_get_disk_page_type (fhdr->type);

  if (allocset_offset == fhdr->last_allocset_offset && VPID_EQ (allocset_vpidptr, &fhdr->last_allocset_vpid)
      && (!VPID_EQ (&to_vpid, &allocset->end_pages_vpid)))
    {
      /* Last allocation set and not at the end of last file table */

      ret = file_ftabvpid_next (fhdr, to_pgptr, &from_vpid);
      if (ret != NO_ERROR)
	{
	  goto exit_on_error;
	}
      num_ftb_pages_deleted = 0;

      while (!VPID_ISNULL (&from_vpid))
	{
	  from_pgptr = pgbuf_fix (thread_p, &from_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
	  if (from_pgptr == NULL)
	    {
	      goto exit_on_error;
	    }

	  (void) pgbuf_check_page_ptype (thread_p, from_pgptr, PAGE_FTAB);

	  vpid = from_vpid;

	  /* Next file table page */
	  ret = file_ftabvpid_next (fhdr, from_pgptr, &from_vpid);
	  if (ret != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	  pgbuf_unfix_and_init (thread_p, from_pgptr);

	  /* Does not matter the return value of disk_dealloc_page */
	  (void) disk_dealloc_page (thread_p, vpid.volid, vpid.pageid, 1, page_type);
	  num_ftb_pages_deleted++;
	}

      if (num_ftb_pages_deleted > 0)
	{
	  /* The file header will be changed to reflect the new file table pages */

	  /* Save the data that is going to be change for undo purposes */
	  recv_ftb_undo.num_table_vpids = fhdr->num_table_vpids;
	  recv_ftb_undo.next_table_vpid = fhdr->next_table_vpid;
	  recv_ftb_undo.last_table_vpid = fhdr->last_table_vpid;

	  /* Update the file header */
	  fhdr->num_table_vpids -= num_ftb_pages_deleted;
	  fhdr->last_table_vpid = to_vpid;
	  if (to_vpid.volid == fhdr->vfid.volid && to_vpid.pageid == fhdr->vfid.fileid)
	    {
	      VPID_SET_NULL (&fhdr->next_table_vpid);
	    }

	  /* Get the changes for redo purposes */
	  recv_ftb_redo.num_table_vpids = fhdr->num_table_vpids;
	  recv_ftb_redo.next_table_vpid = fhdr->next_table_vpid;
	  recv_ftb_redo.last_table_vpid = fhdr->last_table_vpid;

	  /* Log the changes */
	  addr.pgptr = fhdr_pgptr;
	  addr.offset = FILE_HEADER_OFFSET;
	  log_append_undoredo_data (thread_p, RVFL_FHDR_FTB_EXPANSION, &addr, sizeof (recv_ftb_undo),
				    sizeof (recv_ftb_redo), &recv_ftb_undo, &recv_ftb_redo);

	  pgbuf_set_dirty (thread_p, fhdr_pgptr, DONT_FREE);

	  /* Update the last to_table to point to nothing */
	  if (to_vpid.volid != fhdr->vfid.volid || to_vpid.pageid != fhdr->vfid.fileid)
	    {
	      /* Header Page does not have a chain. */
	      chain = (FILE_FTAB_CHAIN *) (to_pgptr + FILE_FTAB_CHAIN_OFFSET);
	      memcpy (&rv_undo_chain, chain, sizeof (*chain));

	      VPID_SET_NULL (&chain->next_ftbvpid);
	      addr.pgptr = to_pgptr;
	      addr.offset = FILE_FTAB_CHAIN_OFFSET;
	      log_append_undoredo_data (thread_p, RVFL_FTAB_CHAIN, &addr, sizeof (*chain), sizeof (*chain),
					&rv_undo_chain, chain);
	      pgbuf_set_dirty (thread_p, to_pgptr, DONT_FREE);
	    }
	}
    }

  /* Update the allocation set with the end of page table, and log the undo and redo information */

  allocset->end_pages_vpid = to_vpid;
  allocset->end_pages_offset = (INT16) ((char *) to_aid_ptr - (char *) to_pgptr);
  allocset->num_holes = 0;

  /* save changes for redo purposes */
  recv_redo.start_pages_vpid = allocset->start_pages_vpid;
  recv_redo.end_pages_vpid = allocset->end_pages_vpid;
  recv_redo.start_pages_offset = allocset->start_pages_offset;
  recv_redo.end_pages_offset = allocset->end_pages_offset;
  recv_redo.num_holes = allocset->num_holes;

  /* Log the undo and redo of the allocation set */
  addr.pgptr = allocset_pgptr;
  addr.offset = allocset_offset;
  log_append_undoredo_data (thread_p, RVFL_ALLOCSET_PAGETB_ADDRESS, &addr, sizeof (recv_undo), sizeof (recv_redo),
			    &recv_undo, &recv_redo);

  pgbuf_set_dirty (thread_p, allocset_pgptr, DONT_FREE);

  /* Was the to_page modified ? */
  if (length != 0)
    {
      addr.pgptr = to_pgptr;
      addr.offset = to_start_offset;
      CAST_TO_PAGEARRAY (to_aid_ptr, to_pgptr, addr.offset);
      log_append_redo_data (thread_p, RVFL_IDSTABLE, &addr, length, to_aid_ptr);
      pgbuf_set_dirty (thread_p, to_pgptr, FREE);
      to_pgptr = NULL;
    }
  else
    {
      pgbuf_unfix_and_init (thread_p, to_pgptr);
    }

  return ret;

exit_on_error:

  if (from_pgptr)
    {
      pgbuf_unfix_and_init (thread_p, from_pgptr);
    }

  if (to_pgptr)
    {
      pgbuf_unfix_and_init (thread_p, to_pgptr);
    }

  if (ret == NO_ERROR)
    {
      ret = ER_FAILED;
    }

  return ret;
}

/*
 * file_allocset_shift_sector_table () - Move the sector table to given left file
 *                                     table page
 *   return: NO_ERROR
 *   fhdr_pgptr(in): Page pointer to file header
 *   allocset_pgptr(out): Pointer to a page where allocation set is located
 *   allocset_offset(in): Location in allocset page where allocation set is
 *                        located
 *   ftb_vpid(in): The file table page where the secotr table is copied
 *   ftb_offset(in): Where in the copy file table area
 */
static int
file_allocset_shift_sector_table (THREAD_ENTRY * thread_p, PAGE_PTR fhdr_pgptr, PAGE_PTR allocset_pgptr,
				  INT16 allocset_offset, VPID * ftb_vpid, INT16 ftb_offset)
{
  FILE_HEADER *fhdr;
  FILE_ALLOCSET *allocset;
  VPID to_vpid;
  VPID from_vpid;
  INT32 *to_aid_ptr;		/* Pageid pointer array for to-page */
  INT32 *from_aid_ptr;		/* Pageid pointer array for from-page */
  INT32 *to_outptr;		/* Out of pointer array for to-page */
  INT32 *from_outptr;		/* Out of pointer array for from-page */
  PAGE_PTR to_pgptr = NULL;
  PAGE_PTR from_pgptr = NULL;
  int length;
  int to_length, from_length;
  LOG_DATA_ADDR addr;
  FILE_RECV_SHIFT_SECTOR_TABLE recv_undo;
  FILE_RECV_SHIFT_SECTOR_TABLE recv_redo;
  int ret = NO_ERROR;
  INT16 redo_offset;

  (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);
  (void) pgbuf_check_page_ptype (thread_p, allocset_pgptr, PAGE_FTAB);

  fhdr = (FILE_HEADER *) (fhdr_pgptr + FILE_HEADER_OFFSET);

  addr.vfid = &fhdr->vfid;

  allocset = (FILE_ALLOCSET *) ((char *) allocset_pgptr + allocset_offset);

  if (VPID_EQ (ftb_vpid, &allocset->start_sects_vpid) && ftb_offset == allocset->start_sects_offset)
    {
      /* Nothing to compact since sectors are not removed during the life of a file */
      return NO_ERROR;
    }

  to_vpid = *ftb_vpid;
  from_vpid = allocset->start_sects_vpid;

  to_pgptr = pgbuf_fix (thread_p, &to_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (to_pgptr == NULL)
    {
      goto exit_on_error;
    }

  (void) pgbuf_check_page_ptype (thread_p, to_pgptr, PAGE_FTAB);

  from_pgptr = pgbuf_fix (thread_p, &from_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (from_pgptr == NULL)
    {
      goto exit_on_error;
    }

  (void) pgbuf_check_page_ptype (thread_p, from_pgptr, PAGE_FTAB);

  CAST_TO_PAGEARRAY (to_aid_ptr, to_pgptr, ftb_offset);
  CAST_TO_PAGEARRAY (to_outptr, to_pgptr, DB_PAGESIZE);
  redo_offset = ftb_offset;

  /* Find the location for the from part. */
  ret = file_find_limits (from_pgptr, allocset, &from_aid_ptr, &from_outptr, FILE_SECTOR_TABLE);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  /* Execute the move of the sector table */
  length = 0;
  while (!VPID_EQ (&from_vpid, &allocset->end_sects_vpid) || from_aid_ptr <= from_outptr)
    {
      from_length = CAST_BUFLEN ((char *) from_outptr - (char *) from_aid_ptr);
      to_length = CAST_BUFLEN ((char *) to_outptr - (char *) to_aid_ptr);

      if (to_length >= from_length)
	{
	  /* Everything from length can be copied */

	  /* Log whatever is going to be copied */
	  addr.pgptr = to_pgptr;
	  addr.offset = (PGLENGTH) ((char *) to_aid_ptr - (char *) to_pgptr);
	  log_append_undo_data (thread_p, RVFL_IDSTABLE, &addr, from_length, to_aid_ptr);

	  memmove (to_aid_ptr, from_aid_ptr, from_length);

	  /* Note that we cannot increase the length to the pointer since the the length is in bytes and the pointer
	   * points to 4 bytes */
	  to_aid_ptr = (INT32 *) ((char *) to_aid_ptr + from_length);
	  length += from_length;

	  /* Get the next from page */
	  if (VPID_EQ (&from_vpid, &allocset->end_sects_vpid))
	    {
	      break;
	    }
	  else
	    {
	      ret = file_ftabvpid_next (fhdr, from_pgptr, &from_vpid);
	      if (ret != NO_ERROR)
		{
		  goto exit_on_error;
		}
	    }

	  pgbuf_unfix_and_init (thread_p, from_pgptr);
	  from_pgptr = pgbuf_fix (thread_p, &from_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
	  if (from_pgptr == NULL)
	    {
	      goto exit_on_error;
	    }

	  (void) pgbuf_check_page_ptype (thread_p, from_pgptr, PAGE_FTAB);

	  /* Find the location for the from part. */
	  ret = file_find_limits (from_pgptr, allocset, &from_aid_ptr, &from_outptr, FILE_SECTOR_TABLE);
	  if (ret != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	}
      else
	{
	  /* Only a portion of the from length can be copied */

	  /* Log whatever is going to be copied */
	  addr.pgptr = to_pgptr;
	  addr.offset = (PGLENGTH) ((char *) to_aid_ptr - (char *) to_pgptr);
	  log_append_undo_data (thread_p, RVFL_IDSTABLE, &addr, to_length, to_aid_ptr);

	  memmove (to_aid_ptr, from_aid_ptr, to_length);

	  /* Note that we cannot increase the length to the pointer since the the length is in bytes and the pointer
	   * points to 4 bytes */
	  from_aid_ptr = (INT32 *) ((char *) from_aid_ptr + to_length);
	  length += to_length;

	  /* log whatever was changed on this page.. Everything changes on this page.. no just this step */

	  addr.pgptr = to_pgptr;
	  addr.offset = redo_offset;
	  CAST_TO_PAGEARRAY (to_aid_ptr, to_pgptr, addr.offset);
	  log_append_redo_data (thread_p, RVFL_IDSTABLE, &addr, length, to_aid_ptr);

	  pgbuf_set_dirty (thread_p, to_pgptr, DONT_FREE);
	  length = 0;

	  /* Get the next to page */
	  ret = file_ftabvpid_next (fhdr, to_pgptr, &to_vpid);
	  if (ret != NO_ERROR)
	    {
	      goto exit_on_error;
	    }

	  pgbuf_unfix_and_init (thread_p, to_pgptr);
	  to_pgptr = pgbuf_fix (thread_p, &to_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
	  if (to_pgptr == NULL)
	    {
	      goto exit_on_error;
	    }

	  (void) pgbuf_check_page_ptype (thread_p, to_pgptr, PAGE_FTAB);

	  CAST_TO_PAGEARRAY (to_aid_ptr, to_pgptr, sizeof (FILE_FTAB_CHAIN));
	  CAST_TO_PAGEARRAY (to_outptr, to_pgptr, DB_PAGESIZE);
	  redo_offset = sizeof (FILE_FTAB_CHAIN);
	}
    }

  pgbuf_unfix_and_init (thread_p, from_pgptr);

  /* Update the allocation set with the new address of sector table */

  /* Save the values that are going to be changed for undo purposes */

  recv_undo.start_sects_vpid = allocset->start_sects_vpid;
  recv_undo.start_sects_offset = allocset->start_sects_offset;
  recv_undo.end_sects_vpid = allocset->end_sects_vpid;
  recv_undo.end_sects_offset = allocset->end_sects_offset;

  /* Now execute the changes */
  allocset->start_sects_vpid = *ftb_vpid;
  allocset->start_sects_offset = ftb_offset;
  allocset->end_sects_vpid = to_vpid;
  allocset->end_sects_offset = (INT16) ((char *) to_aid_ptr - (char *) to_pgptr);

  /* Save the values for redo purposes */
  recv_redo.start_sects_vpid = allocset->start_sects_vpid;
  recv_redo.start_sects_offset = allocset->start_sects_offset;
  recv_redo.end_sects_vpid = allocset->end_sects_vpid;
  recv_redo.end_sects_offset = allocset->end_sects_offset;

  /* Now log the changes */
  addr.pgptr = allocset_pgptr;
  addr.offset = allocset_offset;
  log_append_undoredo_data (thread_p, RVFL_ALLOCSET_SECT_SHIFT, &addr, sizeof (recv_undo), sizeof (recv_redo),
			    &recv_undo, &recv_redo);

  pgbuf_set_dirty (thread_p, allocset_pgptr, DONT_FREE);

  /* Log the last portion of the to page that was changed */

  addr.pgptr = to_pgptr;
  addr.offset = redo_offset;
  CAST_TO_PAGEARRAY (to_aid_ptr, to_pgptr, addr.offset);
  log_append_redo_data (thread_p, RVFL_IDSTABLE, &addr, length, to_aid_ptr);

  pgbuf_set_dirty (thread_p, to_pgptr, FREE);
  to_pgptr = NULL;

  return ret;

exit_on_error:

  if (from_pgptr)
    {
      pgbuf_unfix_and_init (thread_p, from_pgptr);
    }

  if (to_pgptr)
    {
      pgbuf_unfix_and_init (thread_p, to_pgptr);
    }

  if (ret == NO_ERROR)
    {
      ret = ER_FAILED;
    }

  return ret;
}

/*
 * file_allocset_compact () - Compact the given allocation set
 *   return: NO_ERROR;
 *   fhdr_pgptr(in): Page pointer to file header
 *   prev_allocset_vpid(in): Page address where the previous allocation set is
 *                           located
 *   prev_allocset_offset(in): Offset in page address where the previous
 *                             allocation set is located
 *   allocset_vpid(in/out): Page address where the current allocation set is
 *                          located
 *   allocset_offset(in/out): Location in allocset page where allocation set is
 *                            located
 *   ftb_vpid(in/out): The file table page where the allocation set is moved
 *   ftb_offset(in/out): Offset in the file table page where the allocation set is
 *                       moved
 */
static int
file_allocset_compact (THREAD_ENTRY * thread_p, PAGE_PTR fhdr_pgptr, VPID * prev_allocset_vpid,
		       INT16 prev_allocset_offset, VPID * allocset_vpid, INT16 * allocset_offset, VPID * ftb_vpid,
		       INT16 * ftb_offset)
{
  FILE_HEADER *fhdr;
  PAGE_PTR allocset_pgptr = NULL;
  PAGE_PTR ftb_pgptr = NULL;	/* Pointer to page where the set is compacted */
  FILE_ALLOCSET *allocset;
  bool islast_allocset;
  LOG_DATA_ADDR addr;
  FILE_RECV_ALLOCSET_LINK recv_undo;
  FILE_RECV_ALLOCSET_LINK recv_redo;
  int ret = NO_ERROR;

  (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);

  fhdr = (FILE_HEADER *) (fhdr_pgptr + FILE_HEADER_OFFSET);

  addr.vfid = &fhdr->vfid;

  allocset_pgptr = pgbuf_fix (thread_p, allocset_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (allocset_pgptr == NULL)
    {
      goto exit_on_error;
    }

  (void) pgbuf_check_page_ptype (thread_p, allocset_pgptr, PAGE_FTAB);

  allocset = (FILE_ALLOCSET *) ((char *) allocset_pgptr + *allocset_offset);

  if (VPID_EQ (&fhdr->last_allocset_vpid, allocset_vpid) && fhdr->last_allocset_offset == *allocset_offset)
    {
      islast_allocset = true;
    }
  else
    {
      islast_allocset = false;
    }

  while (true)
    {
      if (*allocset_offset == *ftb_offset && VPID_EQ (allocset_vpid, ftb_vpid))
	{
	  /* Move the pointers */
	  *ftb_vpid = allocset->start_sects_vpid;
	  *ftb_offset = allocset->start_sects_offset;
	  break;
	}
      else
	{
	  /* Copy the allocation set description */
	  ftb_pgptr = pgbuf_fix (thread_p, ftb_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
	  if (ftb_pgptr == NULL)
	    {
	      goto exit_on_error;
	    }

	  (void) pgbuf_check_page_ptype (thread_p, ftb_pgptr, PAGE_FTAB);

	  if ((*ftb_offset + SSIZEOF (*allocset)) >= DB_PAGESIZE)
	    {
	      /* Cannot copy the allocation set at this location. Get next copy page */
	      ret = file_ftabvpid_next (fhdr, ftb_pgptr, ftb_vpid);
	      if (ret != NO_ERROR)
		{
		  goto exit_on_error;
		}
	      *ftb_offset = sizeof (FILE_FTAB_CHAIN);
	      pgbuf_unfix_and_init (thread_p, ftb_pgptr);
	    }
	  else
	    {
	      /* Now log the changes */
	      addr.pgptr = ftb_pgptr;
	      addr.offset = *ftb_offset;
	      log_append_undoredo_data (thread_p, RVFL_ALLOCSET_COPY, &addr, sizeof (*allocset), sizeof (*allocset),
					(char *) ftb_pgptr + *ftb_offset, allocset);

	      /* Copy the allocation set to current location */
	      memmove ((char *) ftb_pgptr + *ftb_offset, allocset, sizeof (*allocset));
	      pgbuf_set_dirty (thread_p, ftb_pgptr, FREE);
	      ftb_pgptr = NULL;

	      /* Now free the old allocation page, modify the old allocation set to point to new address, and get the
	       * new allocation page */
	      pgbuf_unfix_and_init (thread_p, allocset_pgptr);

	      *allocset_vpid = *ftb_vpid;
	      *allocset_offset = *ftb_offset;
	      *ftb_offset += sizeof (*allocset);

	      /* The previous allocation set must point to new address */
	      allocset_pgptr =
		pgbuf_fix (thread_p, prev_allocset_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
	      if (allocset_pgptr == NULL)
		{
		  goto exit_on_error;
		}

	      (void) pgbuf_check_page_ptype (thread_p, allocset_pgptr, PAGE_FTAB);

	      allocset = (FILE_ALLOCSET *) ((char *) allocset_pgptr + prev_allocset_offset);

	      /* Save the information that is going to be changed for undo purposes */
	      recv_undo.next_allocset_vpid = allocset->next_allocset_vpid;
	      recv_undo.next_allocset_offset = allocset->next_allocset_offset;

	      /* Perform the change */
	      allocset->next_allocset_vpid = *allocset_vpid;
	      allocset->next_allocset_offset = *allocset_offset;

	      /* Get the information changed for redo purposes */
	      recv_redo.next_allocset_vpid = allocset->next_allocset_vpid;
	      recv_redo.next_allocset_offset = allocset->next_allocset_offset;

	      /* Now log the information */
	      addr.pgptr = allocset_pgptr;
	      addr.offset = prev_allocset_offset;
	      log_append_undoredo_data (thread_p, RVFL_ALLOCSET_LINK, &addr, sizeof (recv_undo), sizeof (recv_redo),
					&recv_undo, &recv_redo);

	      pgbuf_set_dirty (thread_p, allocset_pgptr, FREE);
	      allocset_pgptr = NULL;

	      /* If this is the last allocation set, change the header */
	      if (islast_allocset == true)
		{
		  /* Save the information that is going to be changed for undo purposes */
		  recv_undo.next_allocset_vpid = fhdr->last_allocset_vpid;
		  recv_undo.next_allocset_offset = fhdr->last_allocset_offset;

		  /* Execute the change */
		  fhdr->last_allocset_vpid = *allocset_vpid;
		  fhdr->last_allocset_offset = *allocset_offset;

		  /* Log the changes */
		  addr.pgptr = fhdr_pgptr;
		  addr.offset = FILE_HEADER_OFFSET;
		  log_append_undoredo_data (thread_p, RVFL_FHDR_CHANGE_LAST_ALLOCSET, &addr, sizeof (recv_undo),
					    sizeof (recv_redo), &recv_undo, &recv_redo);

		  pgbuf_set_dirty (thread_p, fhdr_pgptr, DONT_FREE);
		}

	      /* Now re-read the desired allocation set since we moved to another location */
	      allocset_pgptr =
		pgbuf_fix (thread_p, allocset_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
	      if (allocset_pgptr == NULL)
		{
		  goto exit_on_error;
		}

	      (void) pgbuf_check_page_ptype (thread_p, allocset_pgptr, PAGE_FTAB);

	      allocset = (FILE_ALLOCSET *) (((char *) allocset_pgptr + *allocset_offset));
	      break;
	    }
	}
    }

  if (file_allocset_shift_sector_table (thread_p, fhdr_pgptr, allocset_pgptr, *allocset_offset, ftb_vpid,
					*ftb_offset) != NO_ERROR
      || file_allocset_compact_page_table (thread_p, fhdr_pgptr, allocset_pgptr, *allocset_offset,
					   !islast_allocset) != NO_ERROR)
    {
      goto exit_on_error;
    }

  *ftb_vpid = allocset->end_pages_vpid;
  *ftb_offset = allocset->end_pages_offset;
  pgbuf_unfix_and_init (thread_p, allocset_pgptr);

  return ret;

exit_on_error:

  if (ftb_pgptr)
    {
      pgbuf_unfix_and_init (thread_p, ftb_pgptr);
    }

  if (allocset_pgptr)
    {
      pgbuf_unfix_and_init (thread_p, allocset_pgptr);
    }

  if (ret == NO_ERROR)
    {
      ret = ER_FAILED;
    }

  return ret;
}

/*
 * file_compress () - Compress each allocation set of given file
 *   return: NO_ERROR
 *   vfid(in): Complete file identifier
 */
static int
file_compress (THREAD_ENTRY * thread_p, const VFID * vfid, bool do_partial_compaction)
{
  FILE_HEADER *fhdr;
  FILE_ALLOCSET *allocset;
  PAGE_PTR fhdr_pgptr = NULL;
  PAGE_PTR allocset_pgptr = NULL;
  INT16 allocset_offset;
  VPID allocset_vpid;
  VPID prev_allocset_vpid;
  INT16 prev_allocset_offset;
  VPID ftb_vpid;
  INT16 ftb_offset;
  int ret = NO_ERROR;

  /* 
   * Start a TOP SYSTEM OPERATION.
   * This top system operation will be either ABORTED (case of failure) or
   * COMMITTED. That is, it is run independently of the transaction
   */

  if (log_start_system_op (thread_p) == NULL)
    {
      return ER_FAILED;
    }

  allocset_vpid.volid = vfid->volid;
  allocset_vpid.pageid = vfid->fileid;

  fhdr_pgptr = pgbuf_fix (thread_p, &allocset_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (fhdr_pgptr == NULL)
    {
      goto exit_on_error;
    }

  (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);

  fhdr = (FILE_HEADER *) (fhdr_pgptr + FILE_HEADER_OFFSET);

  if (fhdr->num_user_pages_mrkdelete > 0)
    {
      goto exit_on_error;
    }

  /* Start compacting each allocation set */

  allocset_offset = offsetof (FILE_HEADER, allocset);

  VPID_SET_NULL (&prev_allocset_vpid);
  prev_allocset_offset = -1;
  ftb_vpid = allocset_vpid;
  ftb_offset = allocset_offset;

  while (!VPID_ISNULL (&allocset_vpid))
    {
      /* Find the next allocation set */
      allocset_pgptr = pgbuf_fix (thread_p, &allocset_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
      if (allocset_pgptr == NULL)
	{
	  ret = ER_FAILED;
	  break;
	}

      (void) pgbuf_check_page_ptype (thread_p, allocset_pgptr, PAGE_FTAB);

      allocset = (FILE_ALLOCSET *) ((char *) allocset_pgptr + allocset_offset);

      if (do_partial_compaction == true && allocset->num_holes < (int) NUM_HOLES_NEED_COMPACTION)
	{
	  prev_allocset_vpid = allocset_vpid;
	  prev_allocset_offset = allocset_offset;

	  allocset_vpid = allocset->next_allocset_vpid;
	  allocset_offset = allocset->next_allocset_offset;

	  ftb_vpid = allocset->end_pages_vpid;
	  ftb_offset = allocset->end_pages_offset;

	  pgbuf_unfix_and_init (thread_p, allocset_pgptr);
	  continue;
	}

      /* Don't remove the first allocation set */
      if (allocset->num_pages == 0 && prev_allocset_offset != -1)
	{
	  VPID next_allocset_vpid;
	  INT16 next_allocset_offset;

	  next_allocset_vpid = allocset->next_allocset_vpid;
	  next_allocset_offset = allocset->next_allocset_offset;
	  pgbuf_unfix_and_init (thread_p, allocset_pgptr);

	  ret = file_allocset_remove (thread_p, fhdr_pgptr, &prev_allocset_vpid, prev_allocset_offset, &allocset_vpid,
				      allocset_offset);
	  if (ret != NO_ERROR)
	    {
	      break;
	    }

	  allocset_vpid = next_allocset_vpid;
	  allocset_offset = next_allocset_offset;
	}
      else
	{
	  /* We need to free the page since after the compaction the allocation set may change address */
	  pgbuf_unfix_and_init (thread_p, allocset_pgptr);
	  ret = file_allocset_compact (thread_p, fhdr_pgptr, &prev_allocset_vpid, prev_allocset_offset, &allocset_vpid,
				       &allocset_offset, &ftb_vpid, &ftb_offset);
	  if (ret != NO_ERROR)
	    {
	      break;
	    }

	  prev_allocset_vpid = allocset_vpid;
	  prev_allocset_offset = allocset_offset;

	  /* Find the next allocation set */
	  allocset_pgptr = pgbuf_fix (thread_p, &allocset_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
	  if (allocset_pgptr == NULL)
	    {
	      ret = ER_FAILED;
	      break;
	    }

	  (void) pgbuf_check_page_ptype (thread_p, allocset_pgptr, PAGE_FTAB);

	  allocset = (FILE_ALLOCSET *) ((char *) allocset_pgptr + allocset_offset);

	  allocset_vpid = allocset->next_allocset_vpid;
	  allocset_offset = allocset->next_allocset_offset;
	  pgbuf_unfix_and_init (thread_p, allocset_pgptr);
	}
    }

  pgbuf_unfix_and_init (thread_p, fhdr_pgptr);

  if (ret == NO_ERROR)
    {
      log_end_system_op (thread_p, LOG_RESULT_TOPOP_COMMIT);
    }
  else
    {
      log_end_system_op (thread_p, LOG_RESULT_TOPOP_ABORT);
    }

  return ret;

exit_on_error:

  if (allocset_pgptr)
    {
      pgbuf_unfix_and_init (thread_p, allocset_pgptr);
    }

  log_end_system_op (thread_p, LOG_RESULT_TOPOP_ABORT);

  if (fhdr_pgptr)
    {
      pgbuf_unfix_and_init (thread_p, fhdr_pgptr);
    }

  if (ret == NO_ERROR)
    {
      ret = ER_FAILED;
    }
  return ret;
}

/*
 * file_check_all_pages () - Check if all file pages are valid
 *   return: either: DISK_INVALID, DISK_VALID, DISK_ERROR
 *   vfid(in): Complete file identifier
 *   validate_vfid(in): Wheater or not to validate the file identifier
 *                      Must of the caller should have this filed ON, the file
 *                      tracker can skip the validation of the file identifier
 *
 * Note: Check that every known page and sector (both user and system) are
 *       actually allocated according to disk manager.
 *       This function is used for debugging purposes.
 */
static DISK_ISVALID
file_check_all_pages (THREAD_ENTRY * thread_p, const VFID * vfid, bool validate_vfid)
{
  FILE_HEADER *fhdr;
  PAGE_PTR fhdr_pgptr = NULL;
  PAGE_PTR pgptr = NULL;
  VPID set_vpids[FILE_SET_NUMVPIDS];	/* Page-volume identifier. Limited to FILE_SET_NUMVPIDS each time */
  int num_found;		/* Number of pages found in each cycle */
  DISK_ISVALID valid = DISK_VALID;
  DISK_ISVALID allvalid;
  int i, j;

#if defined (ENABLE_UNUSED_FUNCTION)
  if (validate_vfid == true)
    {
      allvalid = file_isvalid (thread_p, vfid);
      if (allvalid != DISK_VALID)
	{
	  if (allvalid == DISK_INVALID)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FILE_UNKNOWN_FILE, 2, vfid->volid, vfid->fileid);
	    }

	  return allvalid;
	}
    }
#endif

  allvalid = DISK_VALID;

  /* Copy the descriptor to a volume-page descriptor */
  set_vpids[0].volid = vfid->volid;
  set_vpids[0].pageid = vfid->fileid;

  fhdr_pgptr = pgbuf_fix (thread_p, &set_vpids[0], OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (fhdr_pgptr == NULL)
    {
      goto exit_on_error;
    }

  (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);

  fhdr = (FILE_HEADER *) (fhdr_pgptr + FILE_HEADER_OFFSET);

  if (!VFID_EQ (vfid, &fhdr->vfid) || fhdr->num_table_vpids <= 0 || fhdr->num_allocsets <= 0 || fhdr->num_user_pages < 0
      || fhdr->num_user_pages_mrkdelete < 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FILE_INCONSISTENT_HEADER, 3, vfid->volid, vfid->fileid,
	      fileio_get_volume_label (vfid->volid, PEEK));
      allvalid = DISK_INVALID;
    }

  /* Make sure that all file table pages are consistent */
  while (!VPID_ISNULL (&set_vpids[0]))
    {
      valid = disk_is_page_sector_reserved (thread_p, set_vpids[0].volid, set_vpids[0].pageid);
      if (valid != DISK_VALID)
	{
	  if (valid == DISK_INVALID)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FILE_INCONSISTENT_ALLOCATION, 6, set_vpids[0].volid,
		      set_vpids[0].pageid, fileio_get_volume_label (set_vpids[0].volid, PEEK), vfid->volid,
		      vfid->fileid, fileio_get_volume_label (vfid->volid, PEEK));
	    }

	  allvalid = valid;
	  break;
	}

      pgptr = pgbuf_fix (thread_p, &set_vpids[0], OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
      if (pgptr == NULL)
	{
	  goto exit_on_error;
	}

      (void) pgbuf_check_page_ptype (thread_p, pgptr, PAGE_FTAB);

      if (file_ftabvpid_next (fhdr, pgptr, &set_vpids[0]) != NO_ERROR)
	{
	  goto exit_on_error;
	}

      pgbuf_unfix_and_init (thread_p, pgptr);
    }

  if (allvalid == DISK_VALID)
    {
      for (i = 0; i < fhdr->num_user_pages; i += num_found)
	{
	  num_found = file_find_nthpages (thread_p, vfid, &set_vpids[0], i,
					  ((fhdr->num_user_pages - i < FILE_SET_NUMVPIDS)
					   ? fhdr->num_user_pages - i : FILE_SET_NUMVPIDS));
	  if (num_found <= 0)
	    {
	      if (num_found == -1)
		{
		  /* set error */
		}
	      break;
	    }

	  for (j = 0; j < num_found; j++)
	    {
	      valid = disk_is_page_sector_reserved (thread_p, set_vpids[j].volid, set_vpids[j].pageid);
	      if (valid != DISK_VALID)
		{
		  if (valid == DISK_INVALID)
		    {
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FILE_INCONSISTENT_ALLOCATION, 6, set_vpids[j].volid,
			      set_vpids[j].pageid, fileio_get_volume_label (set_vpids[j].volid, PEEK), vfid->volid,
			      vfid->fileid, fileio_get_volume_label (vfid->volid, PEEK));
		    }
		  allvalid = valid;
		  /* Continue looking for more */
		}
	    }
	}

      if (i != fhdr->num_user_pages)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FILE_INCONSISTENT_EXPECTED_PAGES, 5, fhdr->num_user_pages, i,
		  vfid->volid, vfid->fileid, fileio_get_volume_label (vfid->volid, PEEK));
	  allvalid = DISK_ERROR;
	}
    }

  pgbuf_unfix_and_init (thread_p, fhdr_pgptr);

  if (allvalid == DISK_VALID)
    {
      valid = file_check_deleted (thread_p, vfid);
    }

  if (valid != DISK_VALID)
    {
      allvalid = valid;
    }

  return allvalid;

exit_on_error:

  if (pgptr)
    {
      pgbuf_unfix_and_init (thread_p, pgptr);
    }

  if (fhdr_pgptr)
    {
      pgbuf_unfix_and_init (thread_p, fhdr_pgptr);
    }

  return DISK_ERROR;
}

/*
 * file_check_deleted () - Check that the number of deleted and marked deleted
 *                       pages matches against the header information
 *   return: either: DISK_INVALID, DISK_VALID, DISK_ERROR
 *   vfid(in): Complete file identifier
 *
 * note: This function is used for debugging purposes.
 */
static DISK_ISVALID
file_check_deleted (THREAD_ENTRY * thread_p, const VFID * vfid)
{
  FILE_HEADER *fhdr;
  FILE_ALLOCSET *allocset;
  PAGE_PTR fhdr_pgptr = NULL;
  PAGE_PTR allocset_pgptr = NULL;
  INT16 allocset_offset;
  VPID allocset_vpid;
  int num_deleted;
  int total_marked_deleted, num_marked_deleted;
  DISK_ISVALID valid;

  total_marked_deleted = 0;
  valid = DISK_VALID;

  allocset_vpid.volid = vfid->volid;
  allocset_vpid.pageid = vfid->fileid;

  fhdr_pgptr = pgbuf_fix (thread_p, &allocset_vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (fhdr_pgptr == NULL)
    {
      return DISK_ERROR;
    }

  (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);

  fhdr = (FILE_HEADER *) (fhdr_pgptr + FILE_HEADER_OFFSET);

  allocset_offset = offsetof (FILE_HEADER, allocset);
  while (!VPID_ISNULL (&allocset_vpid))
    {
      allocset_pgptr = pgbuf_fix (thread_p, &allocset_vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
      if (allocset_pgptr == NULL)
	{
	  goto exit_on_error;
	}

      (void) pgbuf_check_page_ptype (thread_p, allocset_pgptr, PAGE_FTAB);

      /* Get the allocation set */
      allocset = (FILE_ALLOCSET *) ((char *) allocset_pgptr + allocset_offset);
      if (file_allocset_find_num_deleted (thread_p, fhdr, allocset, &num_deleted, &num_marked_deleted) < 0
	  || allocset->num_holes != num_deleted + num_marked_deleted)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FILE_ALLOCSET_INCON_EXPECTED_NHOLES, 5, vfid->fileid,
		  vfid->volid, fileio_get_volume_label (vfid->volid, PEEK), num_deleted + num_marked_deleted,
		  allocset->num_holes);
	  valid = DISK_INVALID;
	}
      total_marked_deleted += num_marked_deleted;

      /* Next allocation set */
      if (VPID_EQ (&allocset_vpid, &allocset->next_allocset_vpid) && allocset_offset == allocset->next_allocset_offset)
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_FILE_FTB_LOOP, 4, vfid->fileid,
		  fileio_get_volume_label (vfid->volid, PEEK), allocset->next_allocset_vpid.volid,
		  allocset->next_allocset_vpid.pageid);
	  VPID_SET_NULL (&allocset_vpid);
	  allocset_offset = -1;
	  valid = DISK_INVALID;
	}
      else
	{
	  allocset_vpid = allocset->next_allocset_vpid;
	  allocset_offset = allocset->next_allocset_offset;
	}
      pgbuf_unfix_and_init (thread_p, allocset_pgptr);
    }

#if defined(SA_MODE)
  if (fhdr->num_user_pages_mrkdelete != total_marked_deleted)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FILE_INCONSISTENT_EXPECTED_MARKED_DEL, 5, vfid->fileid, vfid->volid,
	      fileio_get_volume_label (vfid->volid, PEEK), fhdr->num_user_pages_mrkdelete, total_marked_deleted);
      valid = DISK_INVALID;
    }

  assert (fhdr->num_user_pages_mrkdelete == 0 && total_marked_deleted == 0);
#endif

  pgbuf_unfix_and_init (thread_p, fhdr_pgptr);

  return valid;

exit_on_error:

  if (allocset_pgptr)
    {
      pgbuf_unfix_and_init (thread_p, allocset_pgptr);
    }

  if (fhdr_pgptr)
    {
      pgbuf_unfix_and_init (thread_p, fhdr_pgptr);
    }

  return DISK_ERROR;
}

/*
 * file_dump_fhdr () - Dump the given file header
 *   return: NO_ERROR
 *   fhdr(in): Dump the given file header
 */
static int
file_dump_fhdr (THREAD_ENTRY * thread_p, FILE * fp, const FILE_HEADER * fhdr)
{

  char time_val[CTIME_MAX];
  int ret = NO_ERROR;
  time_t tmp_time;

  (void) fprintf (fp, "*** FILE HEADER TABLE DUMP FOR FILE IDENTIFIER = %d ON VOLUME = %d ***\n", fhdr->vfid.fileid,
		  fhdr->vfid.volid);

  tmp_time = (time_t) fhdr->creation;
  (void) ctime_r (&tmp_time, time_val);
  (void) fprintf (fp, "File_type = %s, Ismark_as_deleted = %s,\nCreation_time = %s",
		  file_type_to_string (fhdr->type), (fhdr->ismark_as_deleted == true) ? "true" : "false", time_val);
  (void) fprintf (fp, "File Descriptor comments = ");
  ret = file_descriptor_dump_internal (thread_p, fp, fhdr);
  if (ret != NO_ERROR)
    {
      return ret;
    }
  (void) fprintf (fp, "Num_allocsets = %d, Num_user_pages = %d, Num_mark_deleted = %d\n", fhdr->num_allocsets,
		  fhdr->num_user_pages, fhdr->num_user_pages_mrkdelete);
  (void) fprintf (fp, "Num_ftb_pages = %d, Next_ftb_page = %d|%d, Last_ftb_page = %d|%d,\n", fhdr->num_table_vpids,
		  fhdr->next_table_vpid.volid, fhdr->next_table_vpid.pageid, fhdr->last_table_vpid.volid,
		  fhdr->last_table_vpid.pageid);
  (void) fprintf (fp, "Last allocset at VPID: %d|%d, offset = %d\n", fhdr->last_allocset_vpid.volid,
		  fhdr->last_allocset_vpid.pageid, fhdr->last_allocset_offset);

  return ret;
}

/*
 * file_dump_ftabs () - Dump the identifier of pages of file table
 *   return: NO_ERROR
 *   fhdr(in): Pointer to file header
 */
static int
file_dump_ftabs (THREAD_ENTRY * thread_p, FILE * fp, const FILE_HEADER * fhdr)
{
  PAGE_PTR ftb_pgptr = NULL;
  VPID ftb_vpid;
  int num_out;			/* Number of identifier that has been printed */
  int ret = NO_ERROR;

  (void) fprintf (fp, "FILE TABLE PAGES:\n");

  ftb_vpid.volid = fhdr->vfid.volid;
  ftb_vpid.pageid = fhdr->vfid.fileid;
  num_out = 0;

  while (!VPID_ISNULL (&ftb_vpid))
    {
      if (num_out >= 6)
	{
	  (void) fprintf (fp, "\n");
	  num_out = 1;
	}
      else
	{
	  num_out++;
	}

      (void) fprintf (fp, "%d|%d ", ftb_vpid.volid, ftb_vpid.pageid);

      ftb_pgptr = pgbuf_fix (thread_p, &ftb_vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
      if (ftb_pgptr == NULL)
	{
	  return ER_FAILED;
	}

      (void) pgbuf_check_page_ptype (thread_p, ftb_pgptr, PAGE_FTAB);

      ret = file_ftabvpid_next (fhdr, ftb_pgptr, &ftb_vpid);
      if (ret != NO_ERROR)
	{
	  pgbuf_unfix_and_init (thread_p, ftb_pgptr);
	  return ret;
	}

      pgbuf_unfix_and_init (thread_p, ftb_pgptr);
    }
  (void) fprintf (fp, "\n");

  return ret;
}

/*
 * file_allocset_dump () - Dump information of given allocation set
 *   return: NO_ERROR
 *   allocset(in): Pointer to allocation set
 *   doprint_title(in): wheater or not to print a title
 */
static int
file_allocset_dump (FILE * fp, const FILE_ALLOCSET * allocset, bool doprint_title)
{
  int ret = NO_ERROR;

  if (doprint_title == true)
    {
      (void) fprintf (fp, "ALLOCATION SET:\n");
    }

  (void) fprintf (fp, "Volid=%d, Num_sects = %d, Num_pages = %d, Num_entries_to_compact = %d\n", allocset->volid,
		  allocset->num_sects, allocset->num_pages, allocset->num_holes);

  (void) fprintf (fp, "Next_allocation_set: Page = %d|%d, Offset = %d\n", allocset->next_allocset_vpid.volid,
		  allocset->next_allocset_vpid.pageid, allocset->next_allocset_offset);

  (void) fprintf (fp, "Sector Table (Start): Page = %d|%d, Offset = %d\n", allocset->start_sects_vpid.volid,
		  allocset->start_sects_vpid.pageid, allocset->start_sects_offset);
  (void) fprintf (fp, "               (End): Page = %d|%d, Offset = %d\n", allocset->end_sects_vpid.volid,
		  allocset->end_sects_vpid.pageid, allocset->end_sects_offset);
  (void) fprintf (fp, "          Current_sectid = %d\n", allocset->curr_sectid);

  (void) fprintf (fp, "Page Table   (Start): Page = %d|%d, Offset = %d\n", allocset->start_pages_vpid.volid,
		  allocset->start_pages_vpid.pageid, allocset->start_pages_offset);
  (void) fprintf (fp, "               (End): Page = %d|%d, Offset = %d\n", allocset->end_pages_vpid.volid,
		  allocset->end_pages_vpid.pageid, allocset->end_pages_offset);

  return ret;
}

/*
 * file_allocset_dump_tables () - Dump the sector and page table of the given
 *                              allocation set
 *   return: NO_ERROR
 *   fhdr(in): Pointer to file header
 *   allocset(in): Pointer to allocation set
 */
static int
file_allocset_dump_tables (THREAD_ENTRY * thread_p, FILE * fp, const FILE_HEADER * fhdr, const FILE_ALLOCSET * allocset)
{
  PAGE_PTR pgptr = NULL;
  INT32 *aid_ptr;		/* Pointer to portion of sector/page table */
  INT32 *outptr;		/* Out of portion of table */
  VPID vpid;
  int num_out;			/* Number of identifier that has been printed */
  int num_aids;
  int ret = NO_ERROR;

  /* Dump the sector table */
  (void) fprintf (fp, "Allocated Sectors:\n");

  num_out = 0;
  num_aids = 0;

  vpid = allocset->start_sects_vpid;
  while (!VPID_ISNULL (&vpid))
    {
      pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
      if (pgptr == NULL)
	{
	  ret = ER_FAILED;
	  break;
	}

      (void) pgbuf_check_page_ptype (thread_p, pgptr, PAGE_FTAB);

      /* Calculate the portion of pageids that we can look at current page */
      ret = file_find_limits (pgptr, allocset, &aid_ptr, &outptr, FILE_SECTOR_TABLE);
      if (ret != NO_ERROR)
	{
	  pgbuf_unfix_and_init (thread_p, pgptr);
	  break;
	}

      for (; aid_ptr < outptr; aid_ptr++)
	{
	  if (*aid_ptr != NULL_SECTID)
	    {
	      num_aids++;
	      if (num_out >= 7)
		{
		  (void) fprintf (fp, "\n");
		  num_out = 1;
		}
	      else
		{
		  num_out++;
		}

	      (void) fprintf (fp, "%10d ", *aid_ptr);
	    }
	}

      /* Get next page */
      if (VPID_EQ (&vpid, &allocset->end_sects_vpid))
	{
	  VPID_SET_NULL (&vpid);
	}
      else
	{
	  ret = file_ftabvpid_next (fhdr, pgptr, &vpid);
	  if (ret != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	}

      pgbuf_unfix_and_init (thread_p, pgptr);
    }
  (void) fprintf (fp, "\n");

  if (allocset->num_sects != num_aids)
    {
      (void) fprintf (fp, "WARNING: Number of sectors = %d does not match sectors = %d in allocationset header\n",
		      num_aids, allocset->num_sects);
    }

  /* Dump the page table */
  (void) fprintf (fp, "Allocated pages:\n");

  num_out = 0;
  num_aids = 0;

  vpid = allocset->start_pages_vpid;
  while (!VPID_ISNULL (&vpid))
    {
      pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
      if (pgptr == NULL)
	{
	  ret = ER_FAILED;
	  break;
	}

      (void) pgbuf_check_page_ptype (thread_p, pgptr, PAGE_FTAB);

      /* Calculate the portion of pageids that we can look at current page */
      ret = file_find_limits (pgptr, allocset, &aid_ptr, &outptr, FILE_PAGE_TABLE);
      if (ret != NO_ERROR)
	{
	  pgbuf_unfix_and_init (thread_p, pgptr);
	  break;
	}

      for (; aid_ptr < outptr; aid_ptr++)
	{
	  if (num_out >= 7)
	    {
	      (void) fprintf (fp, "\n");
	      num_out = 1;
	    }
	  else
	    {
	      num_out++;
	    }

	  if (*aid_ptr == NULL_PAGEID)
	    {
	      (void) fprintf (fp, "       DEL ");
	    }
	  else if (*aid_ptr == NULL_PAGEID_MARKED_DELETED)
	    {
	      (void) fprintf (fp, "    MRKDEL ");
	    }
	  else
	    {
	      (void) fprintf (fp, "%10d ", *aid_ptr);
	      num_aids++;
	    }
	}

      /* Get next page */
      if (VPID_EQ (&vpid, &allocset->end_pages_vpid))
	{
	  VPID_SET_NULL (&vpid);
	}
      else
	{
	  ret = file_ftabvpid_next (fhdr, pgptr, &vpid);
	  if (ret != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	}

      pgbuf_unfix_and_init (thread_p, pgptr);
    }
  (void) fprintf (fp, "\n");

  if (allocset->num_pages != num_aids)
    {
      (void) fprintf (fp, "WARNING: Number of pages = %d does not match pages = %d in allocationset header\n",
		      num_aids, allocset->num_pages);
    }

  (void) fprintf (fp, "\n");

  return ret;

exit_on_error:

  if (pgptr)
    {
      pgbuf_unfix_and_init (thread_p, pgptr);
    }

  if (ret == NO_ERROR)
    {
      ret = ER_FAILED;
    }

  return ret;
}

/*
 * file_dump () - Dump all information realted to the file
 *   return: void
 *   vfid(in): Complete file identifier
 */
int
file_dump (THREAD_ENTRY * thread_p, FILE * fp, const VFID * vfid)
{
  FILE_HEADER *fhdr;
  FILE_ALLOCSET *allocset;
  PAGE_PTR fhdr_pgptr = NULL;
  PAGE_PTR allocset_pgptr = NULL;
  INT16 allocset_offset;
  VPID allocset_vpid;
  int num_pages;
  int setno;
  int ret = NO_ERROR;

  /* Copy the descriptor to a volume-page descriptor */
  allocset_vpid.volid = vfid->volid;
  allocset_vpid.pageid = vfid->fileid;

  fhdr_pgptr = pgbuf_fix (thread_p, &allocset_vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (fhdr_pgptr == NULL)
    {
      goto exit_on_error;
    }

  fhdr = (FILE_HEADER *) (fhdr_pgptr + FILE_HEADER_OFFSET);

  /* Display General Information */
  (void) fprintf (fp, "\n\n");

  /* Dump the header */
  ret = file_dump_fhdr (thread_p, fp, fhdr);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  /* Dump all page identifiers of file table */
  ret = file_dump_ftabs (thread_p, fp, fhdr);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  /* Dump each of the allocation set */

  allocset_offset = offsetof (FILE_HEADER, allocset);
  num_pages = 0;
  setno = 0;

  while (!VPID_ISNULL (&allocset_vpid))
    {
      allocset_pgptr = pgbuf_fix (thread_p, &allocset_vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
      if (allocset_pgptr == NULL)
	{
	  goto exit_on_error;
	}

      setno++;
      /* Get the allocation set */
      allocset = (FILE_ALLOCSET *) ((char *) allocset_pgptr + allocset_offset);

      (void) fprintf (fp, "ALLOCATION SET NUM %d located at vpid = %d|%d offset = %d:\n", setno, allocset_vpid.volid,
		      allocset_vpid.pageid, allocset_offset);
      ret = file_allocset_dump (fp, allocset, false);
      if (ret != NO_ERROR)
	{
	  goto exit_on_error;
	}

      (void) fprintf (fp, "First page in this allocation set is the nthpage = %d\n", num_pages);
      ret = file_allocset_dump_tables (thread_p, fp, fhdr, allocset);
      if (ret != NO_ERROR)
	{
	  goto exit_on_error;
	}

      num_pages += allocset->num_pages;

      /* Next allocation set */
      if (VPID_ISNULL (&allocset->next_allocset_vpid))
	{
	  VPID_SET_NULL (&allocset_vpid);
	  allocset_offset = -1;
	}
      else
	{
	  if (VPID_EQ (&allocset_vpid, &allocset->next_allocset_vpid)
	      && allocset_offset == allocset->next_allocset_offset)
	    {
	      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_FILE_FTB_LOOP, 4, vfid->fileid,
		      fileio_get_volume_label (vfid->volid, PEEK), allocset->next_allocset_vpid.volid,
		      allocset->next_allocset_vpid.pageid);
	      VPID_SET_NULL (&allocset_vpid);
	      allocset_offset = -1;
	    }
	  else
	    {
	      allocset_vpid = allocset->next_allocset_vpid;
	      allocset_offset = allocset->next_allocset_offset;
	    }
	}
      pgbuf_unfix_and_init (thread_p, allocset_pgptr);
    }
  (void) fprintf (fp, "\n\n");

  pgbuf_unfix_and_init (thread_p, fhdr_pgptr);
  (void) file_check_all_pages (thread_p, vfid, false);

  return ret;

exit_on_error:

  if (allocset_pgptr)
    {
      pgbuf_unfix_and_init (thread_p, allocset_pgptr);
    }

  if (fhdr_pgptr)
    {
      pgbuf_unfix_and_init (thread_p, fhdr_pgptr);
    }

  if (ret == NO_ERROR)
    {
      ret = ER_FAILED;
    }

  return ret;
}

/* Tracker related functions */

/*
 * file_tracker_cache_vfid () - Remember the file tracker identifier
 *   return: NO_ERROR
 *   vfid(in): Value of track identifier
 */
int
file_tracker_cache_vfid (VFID * vfid)
{
  int ret = NO_ERROR;

  VFID_COPY (&file_Tracker_vfid, vfid);

  file_Tracker->vfid = &file_Tracker_vfid;

  return ret;
}

VFID *
file_get_tracker_vfid (void)
{
  return file_Tracker->vfid;
}

/*
 * file_tracker_create () - Create a file tracker
 *   return: vfid or NULL in case of error
 *   vfid(out): Complete file identifier
 *
 * Note: Create the system file tracker in the volume identified by the value
 *       of vfid->volid. This file is used to keep track of allocated files.
 */
VFID *
file_tracker_create (THREAD_ENTRY * thread_p, VFID * vfid)
{
  PAGE_PTR fhdr_pgptr = NULL;
  VPID allocset_vpid;
  INT16 allocset_offset;

  file_Tracker->vfid = NULL;

  if (flre_create_with_npages (thread_p, FILE_TRACKER, 1, NULL, vfid) != NO_ERROR)
    {
      return NULL;
    }
  else
    {
      return vfid;
    }

  if (file_create (thread_p, vfid, 0, FILE_TRACKER, NULL, NULL, 0) == NULL)
    {
      goto exit_on_error;
    }

  allocset_vpid.volid = vfid->volid;
  allocset_vpid.pageid = vfid->fileid;

  fhdr_pgptr = pgbuf_fix (thread_p, &allocset_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (fhdr_pgptr == NULL)
    {
      goto exit_on_error;
    }

  (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);

  allocset_offset = offsetof (FILE_HEADER, allocset);

  if (file_allocset_compact_page_table (thread_p, fhdr_pgptr, fhdr_pgptr, allocset_offset, true) != NO_ERROR)
    {
      goto exit_on_error;
    }

  pgbuf_unfix_and_init (thread_p, fhdr_pgptr);
  /* Register it now. We need to do it here since the file tracker was unknown when the file was created */
  if (file_tracker_cache_vfid (vfid) != NO_ERROR)
    {
      goto exit_on_error;
    }
  if (file_tracker_register (thread_p, vfid) == NULL)
    {
      goto exit_on_error;
    }

  return vfid;

exit_on_error:

  if (fhdr_pgptr)
    {
      pgbuf_unfix_and_init (thread_p, fhdr_pgptr);
    }

  (void) file_destroy (thread_p, vfid);
  file_Tracker->vfid = NULL;
  VFID_SET_NULL (vfid);

  return NULL;
}

/*
 * file_tracker_register () - Register a newly created file in the tracker file
 *   return: vfid or NULL in case of error
 *   vfid(in): The newly created file
 */
static const VFID *
file_tracker_register (THREAD_ENTRY * thread_p, const VFID * vfid)
{
  FILE_HEADER *fhdr;
  FILE_ALLOCSET *allocset;
  PAGE_PTR fhdr_pgptr = NULL;
  PAGE_PTR allocset_pgptr = NULL;
  VPID vpid;
  DISK_VOLPURPOSE vol_purpose;
  LOG_DATA_ADDR addr;

  /* todo: fix me */
  if (true)
    {
      return vfid;
    }

  /* Store the file identifier in the array of pageids */
  if (file_Tracker->vfid == NULL || VFID_ISNULL (vfid) || vfid->fileid == DISK_NULL_PAGEID_WITH_ENOUGH_DISK_PAGES)
    {
      return NULL;
    }

  vol_purpose = xdisk_get_purpose (thread_p, vfid->volid);
  if (vol_purpose == DISK_UNKNOWN_PURPOSE || vol_purpose == DB_TEMPORARY_DATA_PURPOSE)
    {
      /* Temporary file on volumes with temporary purposes are not recorded by the tracker. */
      return vfid;
    }

  vpid.volid = file_Tracker->vfid->volid;
  vpid.pageid = file_Tracker->vfid->fileid;

  fhdr_pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (fhdr_pgptr == NULL)
    {
      return NULL;
    }

  (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);

  fhdr = (FILE_HEADER *) (fhdr_pgptr + FILE_HEADER_OFFSET);

  /* The following will be associated to the outer transaction The log is to allow UNDO.  This is a logical log. */
  addr.vfid = &fhdr->vfid;
  addr.pgptr = NULL;
  addr.offset = 0;
  log_append_undo_data (thread_p, RVFL_TRACKER_REGISTER, &addr, sizeof (*vfid), vfid);

  if (log_start_system_op (thread_p) == NULL)
    {
      pgbuf_unfix_and_init (thread_p, fhdr_pgptr);
      return NULL;
    }

  /* We need to know the allocset the file belongs to. */
  allocset_pgptr =
    pgbuf_fix (thread_p, &fhdr->last_allocset_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (allocset_pgptr == NULL)
    {
      goto exit_on_error;
    }

  (void) pgbuf_check_page_ptype (thread_p, allocset_pgptr, PAGE_FTAB);

  allocset = (FILE_ALLOCSET *) ((char *) allocset_pgptr + fhdr->last_allocset_offset);
  if (allocset->volid != vfid->volid)
    {
      pgbuf_unfix_and_init (thread_p, allocset_pgptr);
      if (file_allocset_add_set (thread_p, fhdr_pgptr, vfid->volid) != NO_ERROR)
	{
	  goto exit_on_error;
	}
      /* Now get the new location for the allocation set */
      allocset_pgptr =
	pgbuf_fix (thread_p, &fhdr->last_allocset_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
      if (allocset_pgptr == NULL)
	{
	  goto exit_on_error;
	}

      (void) pgbuf_check_page_ptype (thread_p, allocset_pgptr, PAGE_FTAB);

      allocset = (FILE_ALLOCSET *) ((char *) allocset_pgptr + fhdr->last_allocset_offset);
    }

  if (file_allocset_add_pageids (thread_p, fhdr_pgptr, allocset_pgptr, fhdr->last_allocset_offset, vfid->fileid, 1,
				 NULL) == NULL_PAGEID)
    {
      goto exit_on_error;
    }

  log_end_system_op (thread_p, LOG_RESULT_TOPOP_COMMIT);

  pgbuf_unfix_and_init (thread_p, allocset_pgptr);
  pgbuf_unfix_and_init (thread_p, fhdr_pgptr);

  return vfid;

exit_on_error:

  if (allocset_pgptr != NULL)
    {
      pgbuf_unfix_and_init (thread_p, allocset_pgptr);
    }

  (void) log_end_system_op (thread_p, LOG_RESULT_TOPOP_ABORT);

  if (fhdr_pgptr != NULL)
    {
      pgbuf_unfix_and_init (thread_p, fhdr_pgptr);
    }

  return NULL;
}

/*
 * file_tracker_unregister () - Unregister a file from the tracker file
 *   return: vfid or NULL in case of error
 *   vfid(in): The newly created file
 */
static const VFID *
file_tracker_unregister (THREAD_ENTRY * thread_p, const VFID * vfid)
{
  FILE_ALLOCSET *allocset;
  VPID allocset_vpid;
  INT16 allocset_offset;
  PAGE_PTR allocset_pgptr = NULL;
  PAGE_PTR fhdr_pgptr = NULL;
  DISK_VOLPURPOSE vol_purpose;
  DISK_ISVALID isfound = DISK_INVALID;
  int ignore;

  /* todo: fix me */
  if (true)
    {
      return vfid;
    }

  if (file_Tracker->vfid == NULL || VFID_ISNULL (vfid) || vfid->fileid == DISK_NULL_PAGEID_WITH_ENOUGH_DISK_PAGES)
    {
      return NULL;
    }

  /* Temporary files on volumes with temporary purposes are not registerd */
  vol_purpose = xdisk_get_purpose (thread_p, vfid->volid);
  if (vol_purpose == DISK_UNKNOWN_PURPOSE || vol_purpose == DB_TEMPORARY_DATA_PURPOSE)
    {
      /* Temporary file on volumes with temporary purposes are not recorded by the tracker. */
      return vfid;
    }

  allocset_vpid.volid = file_Tracker->vfid->volid;
  allocset_vpid.pageid = file_Tracker->vfid->fileid;

  fhdr_pgptr = pgbuf_fix (thread_p, &allocset_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (fhdr_pgptr == NULL)
    {
      goto exit_on_error;
    }

  (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);

  allocset_offset = offsetof (FILE_HEADER, allocset);

  /* Go over each allocation set until the page is found */
  while (!VPID_ISNULL (&allocset_vpid))
    {
      allocset_pgptr = pgbuf_fix (thread_p, &allocset_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
      if (allocset_pgptr == NULL)
	{
	  goto exit_on_error;
	}

      (void) pgbuf_check_page_ptype (thread_p, allocset_pgptr, PAGE_FTAB);

      /* Get the allocation set */
      allocset = (FILE_ALLOCSET *) ((char *) allocset_pgptr + allocset_offset);
      if (allocset->volid == vfid->volid)
	{
	  /* The page may be located in this set */
	  isfound =
	    file_allocset_find_page (thread_p, fhdr_pgptr, allocset_pgptr, allocset_offset, (INT32) (vfid->fileid),
				     file_allocset_remove_pageid, &ignore);
	}
      if (isfound == DISK_ERROR)
	{
	  goto exit_on_error;
	}
      else if (isfound == DISK_INVALID)
	{
	  /* We did not find it in the current allocation set. Get the next allocation set */
	  if (VPID_ISNULL (&allocset->next_allocset_vpid))
	    {
	      VPID_SET_NULL (&allocset_vpid);
	      allocset_offset = -1;
	    }
	  else
	    {
	      if (VPID_EQ (&allocset_vpid, &allocset->next_allocset_vpid)
		  && allocset_offset == allocset->next_allocset_offset)
		{
		  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_FILE_FTB_LOOP, 4, vfid->fileid,
			  fileio_get_volume_label (vfid->volid, PEEK), allocset_vpid.volid, allocset_vpid.pageid);
		  VPID_SET_NULL (&allocset_vpid);
		  allocset_offset = -1;
		}
	      else
		{
		  allocset_vpid = allocset->next_allocset_vpid;
		  allocset_offset = allocset->next_allocset_offset;
		}
	    }
	}
      else
	{
	  VPID_SET_NULL (&allocset_vpid);
	}
      pgbuf_unfix_and_init (thread_p, allocset_pgptr);
    }

  pgbuf_unfix_and_init (thread_p, fhdr_pgptr);

  return ((isfound == DISK_VALID) ? vfid : NULL);

exit_on_error:

  if (allocset_pgptr)
    {
      pgbuf_unfix_and_init (thread_p, allocset_pgptr);
    }

  if (fhdr_pgptr)
    {
      pgbuf_unfix_and_init (thread_p, fhdr_pgptr);
    }

  return NULL;
}

/*
 * file_get_numfiles () - Returns the number of created files even if they are
 *                  marked as deleted
 *   return: Number of files or -1 in case of error
 */
int
file_get_numfiles (THREAD_ENTRY * thread_p)
{
  if (file_Tracker->vfid == NULL)
    {
      return -1;
    }

  /* todo: fix me */
  if (true)
    {
      return 0;
    }

  return file_get_numpages (thread_p, file_Tracker->vfid);
}

/*
 * file_find_nthfile () - Find the file identifier associated with the nth file
 *   return: number of returned vfids, or -1 on error
 *   vfid(out): Complete file identifier
 *   nthfile(in): The desired nth file. The files are ordered by their creation
 *
 * Note: The files are ordered by their creation. For example, the 5th file is
 *       the 5th created file, if no files have been removed.
 *       The numbering of files start with zero.
 */
int
file_find_nthfile (THREAD_ENTRY * thread_p, VFID * vfid, int nthfile)
{
  VPID vpid;
  int count;

  /* todo: fix me */
  if (true)
    {
      VFID_SET_NULL (vfid);
      return NO_ERROR;
    }

  if (file_Tracker->vfid == NULL)
    {
      VFID_SET_NULL (vfid);
      return -1;
    }

  count = file_find_nthpages (thread_p, file_Tracker->vfid, &vpid, nthfile, 1);
  if (count == -1)
    {
      VFID_SET_NULL (vfid);
      return -1;
    }

  if (count == 1)
    {
      vfid->volid = vpid.volid;
      vfid->fileid = (FILEID) vpid.pageid;
      return 1;
    }

  VFID_SET_NULL (vfid);

  return count;
}

#if defined(CUBRID_DEBUG)
/*
 * file_isvalid () - Find if the given file is a valid file
 *   return: either: DISK_INVALID, DISK_VALID, DISK_ERROR
 *   vfid(in): The file identifier
 */
DISK_ISVALID
file_isvalid (THREAD_ENTRY * thread_p, const VFID * vfid)
{
  VPID vpid;
  DISK_ISVALID valid;

  if (VFID_ISNULL (vfid) || vfid->fileid == DISK_NULL_PAGEID_WITH_ENOUGH_DISK_PAGES)
    {
      return DISK_INVALID;
    }

  if (file_Tracker->vfid == NULL)
    {
      return DISK_ERROR;
    }

  vpid.volid = vfid->volid;
  vpid.pageid = (INT32) (vfid->fileid);

  valid = file_isvalid_page_partof (thread_p, &vpid, file_Tracker->vfid);
  if (valid == DISK_VALID)
    {
      if (file_does_marked_as_deleted (thread_p, vfid) == true)
	{
	  valid = DISK_INVALID;
	}
    }

  return valid;
}
#endif

/*
 * file_tracker_cross_check_with_disk_idsmap () -
 *   1. Construct disk allocset image with file tracker.
 *   2. Compares it with real disk allocset.
 *   3. Check deleted pages of every file(file_check_deleted).
 *
 *   return: either: DISK_INVALID, DISK_VALID, DISK_ERROR
 *
 * Note:
 */
DISK_ISVALID
file_tracker_cross_check_with_disk_idsmap (THREAD_ENTRY * thread_p)
{
  DISK_ISVALID allvalid = DISK_VALID;

#if defined (SA_MODE)
  DISK_ISVALID valid = DISK_VALID;
  PAGE_PTR vhdr_pgptr;
  DISK_VAR_HEADER *vhdr;
  int num_files, num_found;
  VPID set_vpids[FILE_SET_NUMVPIDS], vpid;
  VFID vfid;
  int i, j, last_perm_vol;
  char **vol_ids_map = NULL;

  /* todo: fix me */
  return allvalid;
#endif

  return allvalid;
}


/*
 * file_make_idsmap_image () -
 *   return: NO_ERROR
 *   vpid(in):
 */
static DISK_ISVALID
file_make_idsmap_image (THREAD_ENTRY * thread_p, const VFID * vfid, char **vol_ids_map, int last_perm_vol)
{
  DISK_ISVALID valid = DISK_VALID;
#if defined (SA_MODE)
  FILE_HEADER *fhdr;
  PAGE_PTR fhdr_pgptr = NULL;
  PAGE_PTR pgptr = NULL;
  VPID set_vpids[FILE_SET_NUMVPIDS];
  int num_found;
  int i, j;
  VPID next_ftable_vpid;
  int num_user_pages;

  if (vol_ids_map == NULL)
    {
      assert (vol_ids_map != NULL);
      return DISK_ERROR;
    }

  set_vpids[0].volid = vfid->volid;
  set_vpids[0].pageid = vfid->fileid;

  fhdr_pgptr = pgbuf_fix (thread_p, &set_vpids[0], OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (fhdr_pgptr == NULL)
    {
      return DISK_ERROR;
    }

  (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);

  fhdr = (FILE_HEADER *) (fhdr_pgptr + FILE_HEADER_OFFSET);

  if (!VFID_EQ (vfid, &fhdr->vfid) || fhdr->num_table_vpids <= 0 || fhdr->num_allocsets <= 0 || fhdr->num_user_pages < 0
      || fhdr->num_user_pages_mrkdelete < 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FILE_INCONSISTENT_HEADER, 3, vfid->volid, vfid->fileid,
	      fileio_get_volume_label (vfid->volid, PEEK));

      valid = DISK_ERROR;
      goto end;
    }

  /* 1. set file table pages to vol_ids_map */
  /* file header page */
  if (vfid->volid <= last_perm_vol)
    {
      set_bitmap (vol_ids_map[vfid->volid], set_vpids[0].pageid);
    }
  else
    {
      assert_release (vfid->volid <= last_perm_vol);
    }

  if (file_ftabvpid_next (fhdr, fhdr_pgptr, &next_ftable_vpid) != NO_ERROR)
    {
      valid = DISK_ERROR;
      goto end;
    }

  assert (next_ftable_vpid.pageid == fhdr->next_table_vpid.pageid
	  && next_ftable_vpid.volid == fhdr->next_table_vpid.volid);

  /* next pages */
  while (!VPID_ISNULL (&next_ftable_vpid))
    {
      if (next_ftable_vpid.volid <= last_perm_vol)
	{
	  set_bitmap (vol_ids_map[next_ftable_vpid.volid], next_ftable_vpid.pageid);
	}
      else
	{
	  assert_release (next_ftable_vpid.volid <= last_perm_vol);
	}

      pgptr = pgbuf_fix (thread_p, &next_ftable_vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);

      if (pgptr == NULL)
	{
	  valid = DISK_ERROR;
	  goto end;
	}

      (void) pgbuf_check_page_ptype (thread_p, pgptr, PAGE_FTAB);

      if (file_ftabvpid_next (fhdr, pgptr, &next_ftable_vpid) != NO_ERROR)
	{
	  pgbuf_unfix_and_init (thread_p, pgptr);

	  valid = DISK_ERROR;
	  goto end;
	}

      pgbuf_unfix_and_init (thread_p, pgptr);
    }

  num_user_pages = fhdr->num_user_pages;

  /* 2. set user pages */
  for (i = 0; i < num_user_pages; i += num_found)
    {
      num_found = file_find_nthpages (thread_p, vfid, &set_vpids[0], i,
				      ((num_user_pages - i < FILE_SET_NUMVPIDS)
				       ? (num_user_pages - i) : FILE_SET_NUMVPIDS));

      if (num_found <= 0)
	{
	  valid = DISK_ERROR;
	  goto end;
	}

      for (j = 0; j < num_found; j++)
	{
	  if (set_vpids[j].volid <= last_perm_vol)
	    {
	      set_bitmap (vol_ids_map[set_vpids[j].volid], set_vpids[j].pageid);
	    }
	  else
	    {
	      assert_release (set_vpids[j].volid <= last_perm_vol);
	    }
	}
    }

end:
  if (fhdr_pgptr)
    {
      pgbuf_unfix_and_init (thread_p, fhdr_pgptr);
    }

#endif

  return valid;
}

/*
 * set_bitmap () -
 *   return: NO_ERROR
 *   vol_ids_map(in):
 *   pageid(in):
 */
static DISK_ISVALID
set_bitmap (char *vol_ids_map, INT32 pageid)
{
  char *at_chptr;
  int page_num, byte_offset, bit_offset;

  if (vol_ids_map == NULL)
    {
      assert_release (vol_ids_map != NULL);
      return DISK_INVALID;
    }

  page_num = pageid / DISK_PAGE_BIT;
  byte_offset = ((pageid - (page_num * DISK_PAGE_BIT)) / CHAR_BIT) + sizeof (FILEIO_PAGE_RESERVED);
  bit_offset = pageid % CHAR_BIT;
  at_chptr = vol_ids_map + (page_num * IO_PAGESIZE) + byte_offset;

  *at_chptr |= (1 << bit_offset);

  return DISK_VALID;
}

/*
 * file_verify_idsmap_image () -
 *   return: NO_ERROR
 *   vpid(in):
 */
static DISK_ISVALID
file_verify_idsmap_image (THREAD_ENTRY * thread_p, INT16 volid, char *vol_ids_map)
{
  DISK_ISVALID return_code = DISK_VALID;
  DISK_VAR_HEADER *vhdr;
  VPID vpid;
  PAGE_PTR vhdr_pgptr = NULL;
  PAGE_PTR alloc_pgptr = NULL;
  int i, j, k;
  char *file_offset, *disk_offset;

  if (vol_ids_map == NULL)
    {
      assert_release (vol_ids_map != NULL);
      return DISK_ERROR;
    }
  /* todo */
  return DISK_VALID;
}

/*
 * file_tracker_check () - Check that all allocated pages of each known file are
 *                       actually allocated according to disk manager
 *   return: either: DISK_INVALID, DISK_VALID, DISK_ERROR
 *
 * Note: This function is used for debugging purposes.
 */
DISK_ISVALID
file_tracker_check (THREAD_ENTRY * thread_p)
{
  PAGE_PTR trk_fhdr_pgptr = NULL;
  int num_files;
  VPID set_vpids[FILE_SET_NUMVPIDS];	/* Page-volume identifier. Limited to FILE_SET_NUMVPIDS each time */
  int num_found;		/* Number of files in each cycle */
  VFID vfid;			/* Identifier of a found file */
  DISK_ISVALID valid;
  DISK_ISVALID allvalid = DISK_VALID;
  int i, j;

  /* todo: fix me */
  if (true)
    {
      return DISK_VALID;
    }

  if (file_Tracker->vfid == NULL)
    {
      return DISK_ERROR;
    }

  /* 
   * We need to lock the tracker header page in shared mode for the duration of
   * the verification, so that files are not register or unregister during this
   * operation
   */

  set_vpids[0].volid = file_Tracker->vfid->volid;
  set_vpids[0].pageid = file_Tracker->vfid->fileid;

  trk_fhdr_pgptr = pgbuf_fix (thread_p, &set_vpids[0], OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (trk_fhdr_pgptr == NULL)
    {
      return DISK_ERROR;
    }

  (void) pgbuf_check_page_ptype (thread_p, trk_fhdr_pgptr, PAGE_FTAB);

  num_files = file_get_numpages (thread_p, file_Tracker->vfid);
  for (i = 0; i < num_files && allvalid != DISK_ERROR; i += num_found)
    {
      num_found = file_find_nthpages (thread_p, file_Tracker->vfid, &set_vpids[0], i,
				      ((num_files - i < FILE_SET_NUMVPIDS) ? num_files - i : FILE_SET_NUMVPIDS));
      if (num_found <= 0)
	{
	  if (num_found == -1)
	    {
	      /* set error */
	    }
	  break;
	}

      for (j = 0; j < num_found && allvalid != DISK_ERROR; j++)
	{
	  vfid.volid = set_vpids[j].volid;
	  vfid.fileid = set_vpids[j].pageid;

	  valid = file_check_all_pages (thread_p, &vfid, false);
	  if (valid != DISK_VALID)
	    {
	      allvalid = valid;
	    }
	}
    }

  if (allvalid != DISK_ERROR && i != num_files)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FILE_MISMATCH_NFILES, 2, num_files, i);
      allvalid = DISK_INVALID;
    }

  pgbuf_unfix_and_init (thread_p, trk_fhdr_pgptr);

  return allvalid;
}

/*
 * file_tracker_dump () - Dump information about all registered files
 *   return: void
 */
int
file_tracker_dump (THREAD_ENTRY * thread_p, FILE * fp)
{
  PAGE_PTR trk_fhdr_pgptr = NULL;
  int num_files;
  VPID set_vpids[FILE_SET_NUMVPIDS];	/* Page-volume identifier. Limited to FILE_SET_NUMVPIDS each time */
  int num_found;		/* Number of files in each cycle */
  VFID vfid;			/* Identifier of a found file */
  int i, j;

  /* todo: fix me */
  if (true)
    {
      return NO_ERROR;
    }

  if (file_Tracker->vfid == NULL)
    {
      return ER_FAILED;
    }

  /* 
   * We need to lock the tracker header page in shared mode for the duration of
   * the dump, so that files are not register or unregister during this
   * operation
   */

  set_vpids[0].volid = file_Tracker->vfid->volid;
  set_vpids[0].pageid = file_Tracker->vfid->fileid;

  trk_fhdr_pgptr = pgbuf_fix (thread_p, &set_vpids[0], OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (trk_fhdr_pgptr == NULL)
    {
      return ER_FAILED;
    }

  (void) pgbuf_check_page_ptype (thread_p, trk_fhdr_pgptr, PAGE_FTAB);

  num_files = file_get_numpages (thread_p, file_Tracker->vfid);

  /* Display General Information */
  (void) fprintf (fp, "\n\n DUMPING EACH FILE: Total Num of Files = %d\n", num_files);

  for (i = 0; i < num_files; i += num_found)
    {
      num_found = file_find_nthpages (thread_p, file_Tracker->vfid, &set_vpids[0], i,
				      ((num_files - i < FILE_SET_NUMVPIDS) ? num_files - i : FILE_SET_NUMVPIDS));
      if (num_found <= 0)
	{
	  break;
	}

      for (j = 0; j < num_found; j++)
	{
	  vfid.volid = set_vpids[j].volid;
	  vfid.fileid = set_vpids[j].pageid;
	  if (file_dump (thread_p, fp, &vfid) != NO_ERROR)
	    {
	      break;
	    }

	  if (VFID_EQ (&vfid, file_Tracker->vfid))
	    {
	      fprintf (fp, "\n**NOTE: Num_alloc_pgs for tracker are number of allocated files...\n");
	    }
	}
    }

  if (i != num_files)
    {
      (void) fprintf (fp, "Error: %d expected files, %d found files\n", num_files, i);
    }

  pgbuf_unfix_and_init (thread_p, trk_fhdr_pgptr);

  /* todo: dump the cache */
  return NO_ERROR;
}

/*
 * file_tracker_compress () - Compress file table of all files
 *   return: NO_ERROR
 */
int
file_tracker_compress (THREAD_ENTRY * thread_p)
{
  PAGE_PTR trk_fhdr_pgptr = NULL;
  int num_files;
  VPID set_vpids[FILE_SET_NUMVPIDS];	/* Page-volume identifier. Limited to FILE_SET_NUMVPIDS each time */
  int num_found;		/* Number of files in each cycle */
  VFID vfid;			/* Identifier of a found file */
  int i, j;
  int ret = NO_ERROR;

  /* todo: fix me */
  if (true)
    {
      return NO_ERROR;
    }

  if (file_Tracker->vfid == NULL)
    {
      return ER_FAILED;
    }

  /* 
   * We need to lock the tracker header page in exclusive mode for the
   * duration of the verification, so that files are not register or
   * unregister during this operation. The tracker structure is going to
   * be compressed as well.
   */

  set_vpids[0].volid = file_Tracker->vfid->volid;
  set_vpids[0].pageid = file_Tracker->vfid->fileid;

  trk_fhdr_pgptr = pgbuf_fix (thread_p, &set_vpids[0], OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (trk_fhdr_pgptr == NULL)
    {
      return ER_FAILED;
    }

  (void) pgbuf_check_page_ptype (thread_p, trk_fhdr_pgptr, PAGE_FTAB);

  /* Compress all the files, but the tracker file at this moment. */
  num_files = file_get_numpages (thread_p, file_Tracker->vfid);
  for (i = 0; i < num_files; i += num_found)
    {
      num_found = file_find_nthpages (thread_p, file_Tracker->vfid, &set_vpids[0], i,
				      ((num_files - i < FILE_SET_NUMVPIDS) ? num_files - i : FILE_SET_NUMVPIDS));
      if (num_found <= 0)
	{
	  ret = ER_FAILED;
	  break;
	}

      for (j = 0; j < num_found; j++)
	{
	  vfid.volid = set_vpids[j].volid;
	  vfid.fileid = set_vpids[j].pageid;

	  if (VFID_EQ (&vfid, file_Tracker->vfid))
	    {
	      continue;
	    }

	  ret = file_compress (thread_p, &vfid, false);
	  if (ret != NO_ERROR)
	    {
	      break;
	    }
	}
    }

  /* Now compress the tracker file itself */
  if (ret == NO_ERROR)
    {
      ret = file_compress (thread_p, file_Tracker->vfid, false);
    }

  pgbuf_unfix_and_init (thread_p, trk_fhdr_pgptr);

  return ret;
}

/*
 * file_mark_deleted_file_list_add () -
 *   return:
 *   vfid(in):
 *   file_type(in):
 */
static int
file_mark_deleted_file_list_add (VFID * vfid, const FILE_TYPE file_type)
{
  FILE_MARK_DEL_LIST *node;

  /* todo: fix me */
  if (true)
    {
      return NO_ERROR;
    }

  assert (file_type != FILE_HEAP_REUSE_SLOTS);
  assert (file_Tracker->hint_num_mark_deleted[file_type] >= 0);

  node = (FILE_MARK_DEL_LIST *) malloc (sizeof (FILE_MARK_DEL_LIST));
  if (node == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (FILE_MARK_DEL_LIST));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  VFID_COPY (&node->vfid, vfid);

  node->next = file_Tracker->mrk_del_list[file_type];	/* push at top */
  file_Tracker->mrk_del_list[file_type] = node;
  file_Tracker->hint_num_mark_deleted[file_type]++;

  return NO_ERROR;
}

/*
 * file_mark_deleted_file_list_remove () -
 *   return:
 *   vfid(in):
 *   file_type(in):
 */
static int
file_mark_deleted_file_list_remove (VFID * vfid, const FILE_TYPE file_type)
{
  FILE_MARK_DEL_LIST *node;

  /* todo: fix me */
  if (true)
    {
      return NO_ERROR;
    }

  assert (file_type != FILE_HEAP_REUSE_SLOTS);

  if (file_Tracker->mrk_del_list[file_type] == NULL)
    {
      return ER_FAILED;
    }

  node = file_Tracker->mrk_del_list[file_type];	/* pop at top */
  file_Tracker->mrk_del_list[file_type] = node->next;
  VFID_COPY (vfid, &node->vfid);
  free_and_init (node);
  file_Tracker->hint_num_mark_deleted[file_type]--;

  return NO_ERROR;
}

/*
 * file_reuse_deleted () - Reuse a mark deleted file of the given type
 *   return: vfid or NULL when there is not a file marked as deleted
 *   vfid(out): Complete file identifier
 *   file_type(in): Type of file
 *   file_des(in): Add the follwing file description to the file
 *
 * note: The file is declared as not deleted. The caller is responsible for
 *       cleaning the contents of the mark deleted file.
 */
VFID *
file_reuse_deleted (THREAD_ENTRY * thread_p, VFID * vfid, FILE_TYPE file_type, const void *file_des)
{
  PAGE_PTR trk_fhdr_pgptr = NULL;
  PAGE_PTR fhdr_pgptr = NULL;
  FILE_HEADER *fhdr;
  LOG_DATA_ADDR addr;
  VPID set_vpids[FILE_SET_NUMVPIDS];	/* Page-volume identifier. Limited to FILE_SET_NUMVPIDS each time */
  int num_files;		/* Number of known files */
  int num_found;		/* Number of files found in each cycle */
  INT16 clean;
  int i, j;
  int rv;
  VFID tmp_vfid = {
    NULL_FILEID, NULL_VOLID
  };
  VPID tmp_vpid;

  /* todo: fix me */
  if (true)
    {
      return NULL;
    }

  assert (file_type != FILE_HEAP_REUSE_SLOTS);
  assert (file_type <= FILE_LAST);

  /* 
   * If the table has not been scanned, find the number of deleted files.
   * Use this number as a hint in the future to avoid searching the table and
   * the corresponding files for future reuses. We do not use critical
   * sections when this number is increased when files are marked as deleted..
   * Thus, it is used as a good approximation that may fail in some cases..
   */

  rv = pthread_mutex_lock (&file_Num_mark_deleted_hint_lock);

  if (file_Tracker->vfid == NULL || file_Tracker->hint_num_mark_deleted[file_type] == 0)
    {
      VFID_SET_NULL (vfid);
      pthread_mutex_unlock (&file_Num_mark_deleted_hint_lock);
      return NULL;
    }

  if (file_Tracker->hint_num_mark_deleted[file_type] == -1)
    {
      assert_release (file_type != FILE_HEAP_REUSE_SLOTS);

      /* 
       * We need to lock the tracker header page in exclusive mode to scan for
       * a mark deleted file in a consistent way. For example, we do not want
       * to reuse a marked deleted file twice.
       */
      set_vpids[0].volid = file_Tracker->vfid->volid;
      set_vpids[0].pageid = file_Tracker->vfid->fileid;

      trk_fhdr_pgptr = pgbuf_fix (thread_p, &set_vpids[0], OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
      if (trk_fhdr_pgptr == NULL)
	{
	  goto exit_on_error;
	}

      (void) pgbuf_check_page_ptype (thread_p, trk_fhdr_pgptr, PAGE_FTAB);

      num_files = file_get_numpages (thread_p, file_Tracker->vfid);

      /* Find a mark deleted file */
      file_Tracker->hint_num_mark_deleted[file_type] = 0;

      for (i = 0; i < num_files; i += num_found)
	{
	  num_found = file_find_nthpages (thread_p, file_Tracker->vfid, &set_vpids[0], i,
					  ((num_files - i < FILE_SET_NUMVPIDS) ? num_files - i : FILE_SET_NUMVPIDS));
	  if (num_found <= 0)
	    {
	      break;
	    }

	  for (j = 0; j < num_found; j++)
	    {
	      /* 
	       * Lock the file table header in exclusive mode. Unless, an error
	       * is found, the page remains lock until the end of the
	       * transaction so that no other transaction may access the
	       * destroyed file.
	       */
	      fhdr_pgptr = pgbuf_fix (thread_p, &set_vpids[j], OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
	      if (fhdr_pgptr == NULL)
		{
		  continue;
		}

	      (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);

	      fhdr = (FILE_HEADER *) (fhdr_pgptr + FILE_HEADER_OFFSET);

	      if (fhdr->ismark_as_deleted == true)
		{
		  if (((FILE_TYPE) fhdr->type) == file_type)
		    {
		      if (file_mark_deleted_file_list_add (&fhdr->vfid, fhdr->type) != NO_ERROR)
			{
			  goto exit_on_error;
			}
		    }
		}
	      pgbuf_unfix_and_init (thread_p, fhdr_pgptr);
	    }
	}
      pgbuf_unfix_and_init (thread_p, trk_fhdr_pgptr);
    }

  if (file_Tracker->hint_num_mark_deleted[file_type] <= 0)
    {
      goto exit_on_error;
    }

  if (file_mark_deleted_file_list_remove (&tmp_vfid, file_type) != NO_ERROR)
    {
      pthread_mutex_unlock (&file_Num_mark_deleted_hint_lock);
      VFID_SET_NULL (vfid);
      return NULL;
    }

  tmp_vpid.volid = tmp_vfid.volid;
  tmp_vpid.pageid = tmp_vfid.fileid;

  fhdr_pgptr = pgbuf_fix (thread_p, &tmp_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (fhdr_pgptr == NULL)
    {
      goto exit_on_error;
    }

  (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);

  fhdr = (FILE_HEADER *) (fhdr_pgptr + FILE_HEADER_OFFSET);

  /* fhdr update */
  addr.vfid = &fhdr->vfid;
  addr.pgptr = fhdr_pgptr;
  addr.offset = FILE_HEADER_OFFSET;

  clean = false;
  log_append_undoredo_data (thread_p, RVFL_MARKED_DELETED, &addr, sizeof (fhdr->ismark_as_deleted),
			    sizeof (fhdr->ismark_as_deleted), &fhdr->ismark_as_deleted, &clean);
  fhdr->ismark_as_deleted = clean;
  VFID_COPY (vfid, &fhdr->vfid);
  (void) file_descriptor_update (thread_p, vfid, file_des);

  pgbuf_set_dirty (thread_p, fhdr_pgptr, FREE);
  fhdr_pgptr = NULL;
  pthread_mutex_unlock (&file_Num_mark_deleted_hint_lock);

  return vfid;

exit_on_error:

  if (fhdr_pgptr)
    {
      pgbuf_unfix_and_init (thread_p, fhdr_pgptr);
    }

  if (trk_fhdr_pgptr)
    {
      pgbuf_unfix_and_init (thread_p, trk_fhdr_pgptr);
    }

  VFID_SET_NULL (vfid);
  pthread_mutex_unlock (&file_Num_mark_deleted_hint_lock);

  return NULL;
}

/*
 * file_reclaim_all_deleted () - Reclaim space of mark deleted files
 *   return: NO_ERROR
 *
 * Note: This function must be called when there are not more references to
 *       any page of marked deleted files.
 */
int
file_reclaim_all_deleted (THREAD_ENTRY * thread_p)
{
  PGBUF_LATCH_MODE latch_mode = PGBUF_LATCH_READ;
  PAGE_PTR trk_fhdr_pgptr = NULL;
  VFID marked_files[FILE_DESTROY_NUM_MARKED];
  VPID vpid;
  int num_files;
  int num_marked = 0;
  int i, nth;
  int ret;
  bool latch_promoted = false;

  /* todo: fix me */
  if (true)
    {
      return NO_ERROR;
    }

  if (file_Tracker->vfid == NULL)
    {
      return ER_FAILED;
    }

  /* 
   * We need to lock the tracker header page in shared mode for the duration of
   * the reclaim, so that files are not register or unregister during this
   * operation
   */
restart:
  ret = NO_ERROR;

  vpid.volid = file_Tracker->vfid->volid;
  vpid.pageid = file_Tracker->vfid->fileid;

  trk_fhdr_pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, latch_mode, PGBUF_UNCONDITIONAL_LATCH);
  if (trk_fhdr_pgptr == NULL)
    {
      return ER_FAILED;
    }

  (void) pgbuf_check_page_ptype (thread_p, trk_fhdr_pgptr, PAGE_FTAB);

  nth = 0;
  num_files = file_get_numpages (thread_p, file_Tracker->vfid);

  /* Destroy all marked as deleted files */

  while (nth < num_files && ret == NO_ERROR)
    {
      num_marked = 0;
      for (i = nth; i < num_files && ret == NO_ERROR; i++)
	{
	  if (file_find_nthpages (thread_p, file_Tracker->vfid, &vpid, i, 1) <= 0)
	    {
	      nth = num_files + 1;
	      break;
	    }
	  marked_files[num_marked].volid = vpid.volid;
	  marked_files[num_marked].fileid = vpid.pageid;

	  if (VFID_EQ (file_Tracker->vfid, &marked_files[num_marked]))
	    {
	      continue;
	    }

	  if (file_does_marked_as_deleted (thread_p, &marked_files[num_marked]) == true)
	    {
	      /* Remember this file for destruction */
	      num_marked++;

	      /* Can we keep more.. ? */
	      if (num_marked >= FILE_DESTROY_NUM_MARKED)
		{
		  break;
		}
	    }
	  else
	    {
	      ret = file_compress (thread_p, &marked_files[num_marked], false);
	      if (ret != NO_ERROR)
		{
		  break;
		}
	    }
	}

      if (ret == NO_ERROR)
	{
	  nth = i + 1 - num_marked;
	  num_files -= num_marked;

	  if (num_marked > 0)
	    {
	      if (!latch_promoted)
		{
		  ret = pgbuf_promote_read_latch (thread_p, &trk_fhdr_pgptr, PGBUF_PROMOTE_SHARED_READER);
		  if (ret == ER_PAGE_LATCH_PROMOTE_FAIL)
		    {
		      pgbuf_unfix_and_init (thread_p, trk_fhdr_pgptr);

		      latch_mode = PGBUF_LATCH_WRITE;
		      latch_promoted = true;
		      goto restart;
		    }
		  else if (ret != NO_ERROR || trk_fhdr_pgptr == NULL)
		    {
		      /* unfix trk_fhdr_pgptr if needed */
		      ret = (ret == NO_ERROR) ? ER_FAILED : ret;
		      break;
		    }

		  latch_promoted = true;
		}

	      for (i = 0; i < num_marked; i++)
		{
		  (void) file_destroy (thread_p, &marked_files[i]);
		}
	    }
	}
    }

  if (trk_fhdr_pgptr)
    {
      /* release latch here, file_compress will request write latch later */
      pgbuf_unfix_and_init (thread_p, trk_fhdr_pgptr);
    }

  if (ret == NO_ERROR)
    {
      ret = file_compress (thread_p, file_Tracker->vfid, false);
    }

  return ret;
}

/*
 * file_dump_all_capacities () - Dump the capacities of all files
 *   return: NO_ERROR
 */
int
file_dump_all_capacities (THREAD_ENTRY * thread_p, FILE * fp)
{
  int num_files;
  VFID vfid;
  int num_pages;
  FILE_TYPE type;
  char area[FILE_DUMP_DES_AREA_SIZE];
  char *file_des;
  int file_des_size;
  int size;
  int i;

  int error_code = NO_ERROR;

  /* Find number of files */
  num_files = file_get_numfiles (thread_p);
  if (num_files <= 0)
    {
      return ER_FAILED;
    }

  file_des = area;
  file_des_size = FILE_DUMP_DES_AREA_SIZE;

  fprintf (fp, "    VFID   npages    type             FDES\n");

  /* Find the specifications of each file */
  for (i = 0; i < num_files; i++)
    {
      if (file_find_nthfile (thread_p, &vfid, i) != 1)
	{
	  break;
	}

      error_code = flre_get_type (thread_p, &vfid, &type);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}
      num_pages = file_get_numpages (thread_p, &vfid);
      size = file_get_descriptor (thread_p, &vfid, file_des, file_des_size);
      if (size < 0)
	{
	  if (file_des != area)
	    {
	      free_and_init (file_des);
	    }

	  file_des_size = -size;
	  file_des = (char *) malloc (file_des_size);
	  if (file_des == NULL)
	    {
	      file_des = area;
	      file_des_size = FILE_DUMP_DES_AREA_SIZE;
	    }
	  else
	    {
	      size = file_get_descriptor (thread_p, &vfid, file_des, file_des_size);
	    }
	}

      fprintf (fp, "%4d|%4d %5d  %-22s ", vfid.volid, vfid.fileid, num_pages, file_type_to_string (type));
      if (file_does_marked_as_deleted (thread_p, &vfid) == true)
	{
	  fprintf (fp, "Marked as deleted...");
	}

      if (size > 0)
	{
	  file_descriptor_dump (thread_p, fp, type, file_des, &vfid);
	}
      else
	{
	  fprintf (fp, "\n");
	}
    }

  if (file_des != area)
    {
      free_and_init (file_des);
    }

  return NO_ERROR;
}

/*
 * file_create_hint_numpages () - A hint of pages that may be needed in the short future
 *   return: NO_ERROR
 *   exp_numpages(in): Expected number of pages
 *   file_type(in): Type of file
 *
 * Note: Create an unstructured file in a volume that hopefully has at least
 *       exp_numpages free pages. The volume indicated in vfid->volid is used
 *       as a hint for the allocation of the file. A set of sectors is
 *       allocated to improve locality of the file. The sectors allocated are
 *       estimated from the number of expected pages. The maximum number of
 *       allocated sectors is 25% of the total number of sectors in disk.
 *       Note the number of expected pages are not allocated at this point,
 *       they are allocated as needs arise.
 *
 *       The expected number of pages can be estimated in some cases, for
 *       example, when a B+tree is created, the minimal number of pages needed
 *       can be estimated from the loading file. When the number of expected
 *       pages cannot be estimated a zero or negative value can be passed.
 *       Neither in this case nor when the expected number of pages is smaller
 *       than the number of pages within a sector, are sectors allocated.
 */
int
file_create_hint_numpages (THREAD_ENTRY * thread_p, INT32 exp_numpages, FILE_TYPE file_type)
{
  int i;

  if (exp_numpages <= 0)
    {
      exp_numpages = 1;
    }

  /* Make sure that we will find a good volume with at least the expected number of pages */

  i = file_guess_numpages_overhead (thread_p, NULL, exp_numpages);
  (void) file_find_goodvol (thread_p, NULL_VOLID, NULL_VOLID, exp_numpages + i, DISK_NONCONTIGUOUS_SPANVOLS_PAGES,
			    file_type);

  return NO_ERROR;
}


/* Recovery functions */

/*
 * file_rv_tracker_unregister_logical_undo () -
 *   return: NO_ERROR
 *   vfid(in): Complete file identifier
 */
static int
file_rv_tracker_unregister_logical_undo (THREAD_ENTRY * thread_p, VFID * vfid)
{
  LOG_DATA_ADDR addr;
  VPID vpid;

  if (file_tracker_unregister (thread_p, vfid) == NULL && file_Tracker->vfid != NULL)
    {
      /* 
       * FOOL the recovery manager so that it won't complain that a media
       * recovery may be needed since the file was unregistered (likely was
       * gone)
       */

      vpid.volid = file_Tracker->vfid->volid;
      vpid.pageid = file_Tracker->vfid->fileid;

      addr.pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
      if (addr.pgptr == NULL)
	{
	  return NO_ERROR;	/* do not permit error */
	}

      (void) pgbuf_check_page_ptype (thread_p, addr.pgptr, PAGE_FTAB);

      addr.vfid = file_Tracker->vfid;
      addr.offset = -1;
      /* Don't need undo we are undoing! */
      log_append_redo_data (thread_p, RVFL_LOGICAL_NOOP, &addr, 0, NULL);
      /* Even though this is a noop, we have to mark the page dirty in order to keep the expensive pgbuf_unfix checks
       * from complaining. */
      pgbuf_set_dirty (thread_p, addr.pgptr, FREE);
      addr.pgptr = NULL;
    }

  return NO_ERROR;

}

/*
 * file_rv_undo_create_tmp () - Undo the creation of a temporarily file
 *   return: NO_ERROR
 *   rcv(in): Recovery structure
 *
 * Note: The temporary file is destroyed completely
 */
int
file_rv_undo_create_tmp (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  VPID vpid;
  VFID *vfid;
  PAGE_PTR fhdr_pgptr = NULL;
  FILE_HEADER *fhdr;
  int ret = NO_ERROR;

  vfid = (VFID *) rcv->data;

  if (fileio_get_volume_descriptor (vfid->volid) == NULL_VOLDES
      || disk_is_page_sector_reserved (thread_p, vfid->volid, (INT32) (vfid->fileid)) != DISK_VALID)
    {
      ret = file_rv_tracker_unregister_logical_undo (thread_p, vfid);
    }
  else
    {
      (void) file_new_declare_as_old (thread_p, vfid);

      if (vfid->volid > xboot_find_last_permanent (thread_p))
	{
	  /* 
	   * File in a temporary volume.
	   * Don't do anything during the restart process since the volumes are
	   * going to be removed anyway.
	   */
	  if (!BO_IS_SERVER_RESTARTED ())
	    {
	      ret = ER_FAILED;
	    }
	  else
	    {
	      vpid.volid = vfid->volid;
	      vpid.pageid = vfid->fileid;

	      fhdr_pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
	      if (fhdr_pgptr == NULL)
		{
		  ret = ER_FAILED;
		}
	      else
		{
		  (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);

		  fhdr = (FILE_HEADER *) (fhdr_pgptr + FILE_HEADER_OFFSET);
		  if (!VFID_EQ (vfid, &fhdr->vfid))
		    {
		      ret = ER_FAILED;
		    }
		  pgbuf_unfix_and_init (thread_p, fhdr_pgptr);
		}
	    }
	}

      if (ret != NO_ERROR || file_destroy (thread_p, vfid) != NO_ERROR)
	{
	  ret = file_rv_tracker_unregister_logical_undo (thread_p, vfid);
	}
    }

  return NO_ERROR;		/* do not permit error */
}

/*
 * file_rv_dump_create_tmp () - Dump the information to undo the creation
 *                                of a temporary file
 *   return: void
 *   length_ignore(in): Length of Recovery Data
 *   data(in): The data being logged
 */
void
file_rv_dump_create_tmp (FILE * fp, int length_ignore, void *data)
{
  VFID *vfid;

  vfid = (VFID *) data;
  (void) fprintf (fp, "Undo creation of Tmp vfid: %d|%d\n", vfid->volid, vfid->fileid);
}

/* TODO: list for file_ftab_chain */

/*
 * file_rv_redo_ftab_chain () -
 *   return: NO_ERROR
 *   rcv(in): Recovery structure
 */
int
file_rv_redo_ftab_chain (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  (void) pgbuf_set_page_ptype (thread_p, rcv->pgptr, PAGE_FTAB);

  return log_rv_copy_char (thread_p, rcv);
}

/*
 * file_rv_dump_ftab_chain () - Dump redo information to double link file table
 *                          pages
 *   return: void
 *   length_ignore(in): Length of Recovery Data
 *   data(in): The data being logged
 */
void
file_rv_dump_ftab_chain (FILE * fp, int length_ignore, void *data)
{
  FILE_FTAB_CHAIN *recv;	/* Recovery information for double link chain */

  recv = (FILE_FTAB_CHAIN *) data;
  (void) fprintf (fp, "Next_ftb_vpid:%d|%d, Previous_ftb_vpid = %d|%d\n", recv->next_ftbvpid.volid,
		  recv->next_ftbvpid.pageid, recv->prev_ftbvpid.volid, recv->prev_ftbvpid.pageid);
}

/*
 * file_rv_redo_fhdr () -
 *   return: NO_ERROR
 *   rcv(in): Recovery structure
 */
int
file_rv_redo_fhdr (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  (void) pgbuf_set_page_ptype (thread_p, rcv->pgptr, PAGE_FTAB);

  return log_rv_copy_char (thread_p, rcv);
}

/*
 * file_rv_dump_fhdr () - Dump file header recovery information
 *   return: void
 *   length_ignore(in): Length of Recovery Data
 *   data(in): The data being logged
 */
void
file_rv_dump_fhdr (FILE * fp, int length_ignore, void *data)
{
  int ret;

  ret = file_dump_fhdr (NULL, fp, (FILE_HEADER *) data);
  if (ret != NO_ERROR)
    {
      return;
    }
}

/*
 * file_rv_dump_idtab () - Dump sector/page table recovery information
 *   return: void
 *   length(in):
 *   data(in): The data being logged
 */
void
file_rv_dump_idtab (FILE * fp, int length, void *data)
{
  int i;
  INT32 *aid_ptr;		/* Pointer to portion of sector/page table */

  aid_ptr = (INT32 *) data;
  length = length / sizeof (*aid_ptr);
  for (i = 0; i < length; i++, aid_ptr++)
    {
      (void) fprintf (fp, "%d ", *aid_ptr);
    }
  (void) fprintf (fp, "\n");
}

/*
 * file_rv_undoredo_mark_as_deleted () - Recover undo/redo from mark deletion
 *   return: NO_ERROR
 *   rcv(in): Recovery structure
 */
int
file_rv_undoredo_mark_as_deleted (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  FILE_HEADER *fhdr;
  INT16 isdeleted;
  int rv;
  int ret = NO_ERROR;
  int offset = 0;
  OID class_oid;

  (void) pgbuf_check_page_ptype (thread_p, rcv->pgptr, PAGE_FTAB);
  OID_SET_NULL (&class_oid);

  assert (rcv->length == 2 || rcv->length == 4 || rcv->length == OR_INT_SIZE + OR_OID_SIZE);

  if (rcv->length == OR_INT_SIZE + OR_OID_SIZE)
    {
      /* the file has been deleted */
      isdeleted = (INT16) OR_GET_INT (rcv->data);
      offset += OR_INT_SIZE;

      assert (offset < rcv->length);

      OR_GET_OID (rcv->data + offset, &class_oid);
      offset += OR_OID_SIZE;

      assert (offset == rcv->length);
    }
  else
    {
      /* the file will no longer be deleted - rollback the is no need to save the class OID in this case, since the
       * entry will not be removed from the class OID->HFID cache */
      if (rcv->length == 2)
	{
	  isdeleted = *(INT16 *) rcv->data;
	}
      else
	{
	  /* As safe guard, In old log of RB-8.4.3 or before, the function file_mark_as_deleted() writes 4 bytes as
	   * isdeleted flag. */
	  isdeleted = (INT16) (*((INT32 *) rcv->data));
	}
    }

  fhdr = (FILE_HEADER *) (rcv->pgptr + FILE_HEADER_OFFSET);

  fhdr->ismark_as_deleted = isdeleted;

  rv = pthread_mutex_lock (&file_Num_mark_deleted_hint_lock);
  if (file_Tracker->hint_num_mark_deleted[fhdr->type] != -1 && isdeleted == true && fhdr->type == FILE_HEAP)
    {
      assert (fhdr->type != FILE_HEAP_REUSE_SLOTS);

      ret = file_mark_deleted_file_list_add (&fhdr->vfid, fhdr->type);
      if (ret != NO_ERROR)
	{
	  pthread_mutex_unlock (&file_Num_mark_deleted_hint_lock);
	  return NO_ERROR;	/* do not permit error */
	}
    }
  pthread_mutex_unlock (&file_Num_mark_deleted_hint_lock);

  if (isdeleted && !OID_ISNULL (&class_oid))
    {
      (void) heap_delete_hfid_from_cache (thread_p, &class_oid);
    }

  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);

  return NO_ERROR;
}

/*
 * file_rv_dump_marked_as_deleted () - Dump information to recover a file from mark
 *                              deletion
 *   return: void
 *   length_ignore(in): Length of Recovery Data
 *   data(in): The data being logged
 */
void
file_rv_dump_marked_as_deleted (FILE * fp, int length_ignore, void *data)
{
  (void) fprintf (fp, "Marked_deleted = %d\n", *(INT16 *) data);
}

/*
 * file_rv_allocset_undoredo_sector () - Recover sector data on allocation set
 *   return: NO_ERROR
 *   rcv(in): Recovery structure
 */
int
file_rv_allocset_undoredo_sector (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  FILE_RECV_ALLOCSET_SECTOR *rvsect;
  FILE_ALLOCSET *allocset;

  (void) pgbuf_check_page_ptype (thread_p, rcv->pgptr, PAGE_FTAB);

  rvsect = (FILE_RECV_ALLOCSET_SECTOR *) rcv->data;
  allocset = (FILE_ALLOCSET *) ((char *) rcv->pgptr + rcv->offset);

  allocset->num_sects = rvsect->num_sects;
  allocset->curr_sectid = rvsect->curr_sectid;
  allocset->end_sects_offset = rvsect->end_sects_offset;
  allocset->end_sects_vpid = rvsect->end_sects_vpid;

  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);

  return NO_ERROR;		/* do not permit error */
}

/*
 * file_rv_allocset_dump_sector () - Dump allocset sector recovery information
 *   return: void
 *   length_ignore(in): Length of Recovery Data
 *   data(in): The data being logged
 */
void
file_rv_allocset_dump_sector (FILE * fp, int length_ignore, void *data)
{
  FILE_RECV_ALLOCSET_SECTOR *rvsect;

  rvsect = (FILE_RECV_ALLOCSET_SECTOR *) data;

  (void) fprintf (fp, "Num_sects = %d, Curr_sectid = %d,\nSector Table end: pageid = %d|%d, offset = %d\n",
		  rvsect->num_sects, rvsect->curr_sectid, rvsect->end_sects_vpid.volid, rvsect->end_sects_vpid.pageid,
		  rvsect->end_sects_offset);
}

/*
 * file_rv_allocset_undoredo_page () - UNDO/REDO sector expansion information
 *                                    on allocation set
 *   return: NO_ERROR
 *   rcv(in): Recovery structure
 */
int
file_rv_allocset_undoredo_page (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  FILE_RECV_EXPAND_SECTOR *rvstb;
  FILE_ALLOCSET *allocset;

  (void) pgbuf_check_page_ptype (thread_p, rcv->pgptr, PAGE_FTAB);

  rvstb = (FILE_RECV_EXPAND_SECTOR *) rcv->data;
  allocset = (FILE_ALLOCSET *) ((char *) rcv->pgptr + rcv->offset);

  allocset->start_pages_vpid = rvstb->start_pages_vpid;
  allocset->end_pages_vpid = rvstb->end_pages_vpid;
  allocset->start_pages_offset = rvstb->start_pages_offset;
  allocset->end_pages_offset = rvstb->end_pages_offset;
  allocset->num_holes = rvstb->num_holes;

  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);

  return NO_ERROR;		/* do not permit error */
}

/*
 * file_rv_allocset_dump_page () - Dump redo sector expansion
 *                                         information on allocation set
 *   return: void
 *   length_ignore(in): Length of Recovery Data
 *   data(in): The data being logged
 */
void
file_rv_allocset_dump_page (FILE * fp, int length_ignore, void *data)
{
  FILE_RECV_EXPAND_SECTOR *rvstb;

  rvstb = (FILE_RECV_EXPAND_SECTOR *) data;

  fprintf (fp, "Page Table   (Start): Page = %d|%d, Offset = %d\n", rvstb->start_pages_vpid.volid,
	   rvstb->start_pages_vpid.pageid, rvstb->start_pages_offset);
  fprintf (fp, "               (End): Page = %d|%d, Offset = %d\n", rvstb->end_pages_vpid.volid,
	   rvstb->end_pages_vpid.pageid, rvstb->end_pages_offset);

  fprintf (fp, " Num_entries_to_compact = %d\n", rvstb->num_holes);
}

/*
 * file_rv_fhdr_undoredo_expansion () - Recover file table expansion at file header
 *   return: NO_ERROR
 *   rcv(in): Recovery structure
 */
int
file_rv_fhdr_undoredo_expansion (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  FILE_RECV_FTB_EXPANSION *rvftb;	/* Recovery information */
  FILE_HEADER *fhdr;		/* Pointer to file header */

  (void) pgbuf_check_page_ptype (thread_p, rcv->pgptr, PAGE_FTAB);

  rvftb = (FILE_RECV_FTB_EXPANSION *) rcv->data;
  fhdr = (FILE_HEADER *) (rcv->pgptr + FILE_HEADER_OFFSET);
  fhdr->num_table_vpids = rvftb->num_table_vpids;
  fhdr->next_table_vpid = rvftb->next_table_vpid;
  fhdr->last_table_vpid = rvftb->last_table_vpid;

  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);

  return NO_ERROR;		/* do not permit error */
}

/*
 * file_rv_fhdr_dump_expansion () -  Dump redo file table expansion
 *                                     information at header
 *   return: void
 *   length_ignore(in): Length of Recovery Data
 *   data(in): The data being logged
 */
void
file_rv_fhdr_dump_expansion (FILE * fp, int length_ignore, void *data)
{
  FILE_RECV_FTB_EXPANSION *rvftb;

  rvftb = (FILE_RECV_FTB_EXPANSION *) data;
  fprintf (fp, "Num_ftb_pages = %d, Next_ftb_page = %d|%d Last_ftb_page = %d|%d,\n", rvftb->num_table_vpids,
	   rvftb->next_table_vpid.volid, rvftb->next_table_vpid.pageid, rvftb->last_table_vpid.volid,
	   rvftb->last_table_vpid.pageid);
}

/*
 * file_rv_dump_allocset () - Dump allocation set recovery information
 *   return: void
 *   length_ignore(in): Length of Recovery Data
 *   data(in): The data being logged
 */
void
file_rv_dump_allocset (FILE * fp, int length_ignore, void *data)
{
  int ret;

  ret = file_allocset_dump (fp, (FILE_ALLOCSET *) data, true);
  if (ret != NO_ERROR)
    {
      return;
    }
}

/*
 * file_rv_allocset_undoredo_link () -  REDO chain links of allocation set
 *   return: NO_ERROR
 *   rcv(in): Recovery structure
 */
int
file_rv_allocset_undoredo_link (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  FILE_RECV_ALLOCSET_LINK *rvlink;
  FILE_ALLOCSET *allocset;

  (void) pgbuf_check_page_ptype (thread_p, rcv->pgptr, PAGE_FTAB);

  rvlink = (FILE_RECV_ALLOCSET_LINK *) rcv->data;
  allocset = (FILE_ALLOCSET *) ((char *) rcv->pgptr + rcv->offset);

  allocset->next_allocset_vpid = rvlink->next_allocset_vpid;
  allocset->next_allocset_offset = rvlink->next_allocset_offset;

  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);

  return NO_ERROR;		/* do not permit error */
}

/*
 * file_rv_allocset_dump_link () - Dump allocset sector recovery information
 *   return: void
 *   length_ignore(in): Length of Recovery Data
 *   data(in): The data being logged
 */
void
file_rv_allocset_dump_link (FILE * fp, int length_ignore, void *data)
{
  FILE_RECV_ALLOCSET_LINK *rvlink;	/* Link related information */

  rvlink = (FILE_RECV_ALLOCSET_LINK *) data;

  fprintf (fp, "Next_allocation_set: Page = %d|%d Offset = %d\n", rvlink->next_allocset_vpid.volid,
	   rvlink->next_allocset_vpid.pageid, rvlink->next_allocset_offset);
}

/*
 * file_rv_fhdr_last_allocset_helper () - UNDO/REDO address of last allocation set
 *                                      on file header
 *   return: NO_ERROR
 *   rcv(in): Recovery structure
 */
static int
file_rv_fhdr_last_allocset_helper (THREAD_ENTRY * thread_p, LOG_RCV * rcv, int delta)
{
  FILE_RECV_ALLOCSET_LINK *rvlink;
  FILE_HEADER *fhdr;

  (void) pgbuf_check_page_ptype (thread_p, rcv->pgptr, PAGE_FTAB);

  rvlink = (FILE_RECV_ALLOCSET_LINK *) rcv->data;
  fhdr = (FILE_HEADER *) (rcv->pgptr + FILE_HEADER_OFFSET);

  fhdr->last_allocset_vpid = rvlink->next_allocset_vpid;
  fhdr->last_allocset_offset = rvlink->next_allocset_offset;

  fhdr->num_allocsets += delta;

  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);

  return NO_ERROR;		/* do not permit error */
}

/*
 * file_rv_fhdr_add_last_allocset () - REDO address of last allocation set on
 *                                   file header
 *   return: NO_ERROR
 *   rcv(in): Recovery structure
 */
int
file_rv_fhdr_add_last_allocset (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  return file_rv_fhdr_last_allocset_helper (thread_p, rcv, 1);
}

/*
 * file_rv_fhdr_remove_last_allocset () - REDO address of last allocation set on
 *                                      file header
 *   return: NO_ERROR
 *   rcv(in): Recovery structure
 */
int
file_rv_fhdr_remove_last_allocset (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  return file_rv_fhdr_last_allocset_helper (thread_p, rcv, -1);
}

/*
 * file_rv_fhdr_change_last_allocset () - REDO address of last allocation set on
 *                                      file header
 *   return: NO_ERROR
 *   rcv(in): Recovery structure
 */
int
file_rv_fhdr_change_last_allocset (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  FILE_RECV_ALLOCSET_LINK *rvlink;
  FILE_HEADER *fhdr;

  (void) pgbuf_check_page_ptype (thread_p, rcv->pgptr, PAGE_FTAB);

  rvlink = (FILE_RECV_ALLOCSET_LINK *) rcv->data;
  fhdr = (FILE_HEADER *) (rcv->pgptr + FILE_HEADER_OFFSET);

  fhdr->last_allocset_vpid = rvlink->next_allocset_vpid;
  fhdr->last_allocset_offset = rvlink->next_allocset_offset;

  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);

  return NO_ERROR;		/* do not permit error */
}

/*
 * file_rv_fhdr_dump_last_allocset () - Dump allocset sector recovery information
 *   return: void
 *   length_ignore(in): Length of Recovery Data
 *   data(in): The data being logged
 */
void
file_rv_fhdr_dump_last_allocset (FILE * fp, int length_ignore, void *data)
{
  FILE_RECV_ALLOCSET_LINK *rvlink;

  rvlink = (FILE_RECV_ALLOCSET_LINK *) data;

  fprintf (fp, "Last_allocation_set: Page = %d|%d Offset = %d\n", rvlink->next_allocset_vpid.volid,
	   rvlink->next_allocset_vpid.pageid, rvlink->next_allocset_offset);
}

/*
 * file_rv_allocset_undoredo_add_pages () -  Recover allocset information related to
 *                                allocation of pages
 *   return: NO_ERROR
 *   rcv(in): Recovery structure
 */
int
file_rv_allocset_undoredo_add_pages (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  FILE_RECV_ALLOCSET_PAGES *rvpgs;
  FILE_ALLOCSET *allocset;

  (void) pgbuf_check_page_ptype (thread_p, rcv->pgptr, PAGE_FTAB);

  rvpgs = (FILE_RECV_ALLOCSET_PAGES *) rcv->data;
  allocset = (FILE_ALLOCSET *) ((char *) rcv->pgptr + rcv->offset);

  allocset->curr_sectid = rvpgs->curr_sectid;
  allocset->end_pages_vpid = rvpgs->end_pages_vpid;
  allocset->end_pages_offset = rvpgs->end_pages_offset;
  allocset->num_pages += rvpgs->num_pages;

  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);

  return NO_ERROR;		/* do not permit error */
}

/*
 * file_rv_allocset_dump_add_pages () -  Dump allocset information related to
 *                                     allocation of pages
 *   return: void
 *   length_ignore(in): Length of Recovery Data
 *   data(in): The data being logged
 */
void
file_rv_allocset_dump_add_pages (FILE * fp, int length_ignore, void *data)
{
  FILE_RECV_ALLOCSET_PAGES *rvpgs;

  rvpgs = (FILE_RECV_ALLOCSET_PAGES *) data;

  fprintf (fp, " Npages = %d added, Current_sectid = %d\n", rvpgs->num_pages, rvpgs->curr_sectid);
  fprintf (fp, "Page Table   (End): Page = %d|%d Offset = %d\n", rvpgs->end_pages_vpid.volid,
	   rvpgs->end_pages_vpid.pageid, rvpgs->end_pages_offset);
}

/*
 * file_rv_fhdr_undoredo_add_pages () -  REDO header page information
 *   return: NO_ERROR
 *   rcv(in): Recovery structure
 */
int
file_rv_fhdr_undoredo_add_pages (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  FILE_HEADER *fhdr;

  (void) pgbuf_check_page_ptype (thread_p, rcv->pgptr, PAGE_FTAB);

  fhdr = (FILE_HEADER *) (rcv->pgptr + FILE_HEADER_OFFSET);

  fhdr->num_user_pages += *(INT32 *) rcv->data;

  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);

  return NO_ERROR;		/* do not permit error */
}

/*
 * file_rv_fhdr_dump_add_pages () - Dump file header page recovery information
 *   return: void
 *   length_ignore(in): Length of Recovery Data
 *   data(in): The data being logged
 */
void
file_rv_fhdr_dump_add_pages (FILE * fp, int length_ignore, void *data)
{
  fprintf (fp, "Num_user_pages added = %d\n", *(INT32 *) data);
}

/*
 * file_rv_fhdr_undoredo_mark_deleted_pages () - Undo/Redo mark deleted page info at
 *                                    file header
 *   return: NO_ERROR
 *   rcv(in): Recovery structure
 */
int
file_rv_fhdr_undoredo_mark_deleted_pages (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  FILE_HEADER *fhdr;
  INT32 npages;

  (void) pgbuf_check_page_ptype (thread_p, rcv->pgptr, PAGE_FTAB);

  npages = *(INT32 *) rcv->data;
  fhdr = (FILE_HEADER *) (rcv->pgptr + FILE_HEADER_OFFSET);

  fhdr->num_user_pages += npages;
  fhdr->num_user_pages_mrkdelete -= npages;

  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);

  return NO_ERROR;		/* do not permit error */
}

/*
 * file_rv_fhdr_undoredo_update_num_user_pages () - Undo/redo update num user
 *						    pages.
 *
 * return	 : NO_ERROR.
 * thread_p (in) : Thread entry.
 * rcv (in)	 : Recovery data.
 */
int
file_rv_fhdr_undoredo_update_num_user_pages (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  FILE_HEADER *fhdr;
  INT32 npages;

  (void) pgbuf_check_page_ptype (thread_p, rcv->pgptr, PAGE_FTAB);

  npages = *(INT32 *) rcv->data;
  fhdr = (FILE_HEADER *) (rcv->pgptr + FILE_HEADER_OFFSET);

  fhdr->num_user_pages += npages;

  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);

  return NO_ERROR;		/* do not permit error */
}

/*
 * file_rv_fhdr_dump_mark_deleted_pages () - Dump undo/Redo information for mark
 *                                         deleted pages at header
 *   return: void
 *   length_ignore(in): Length of Recovery Data
 *   data(in): The data being logged
 */
void
file_rv_fhdr_dump_mark_deleted_pages (FILE * fp, int length_ignore, void *data)
{
  fprintf (fp, "Npages = %d mark as deleted\n", abs (*(INT32 *) data));
}

/*
 * file_rv_allocset_undoredo_delete_pages () - Undo/Redo the deallocation of a set of
 *                                  pages allocation set.
 *   return: NO_ERROR
 *   rcv(in): Recovery structure
 */
int
file_rv_allocset_undoredo_delete_pages (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  FILE_ALLOCSET *allocset;
  INT32 npages;

  (void) pgbuf_check_page_ptype (thread_p, rcv->pgptr, PAGE_FTAB);

  npages = *(INT32 *) rcv->data;

  allocset = (FILE_ALLOCSET *) ((char *) rcv->pgptr + rcv->offset);
  allocset->num_pages += npages;
  allocset->num_holes -= npages;
  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);

  return NO_ERROR;		/* do not permit error */
}

/*
 * file_rv_allocset_dump_delete_pages () - Dump Redo information of the
 *                                       deallocation of pages
 *   return: void
 *   length_ignore(in): Length of Recovery Data
 *   data(in): The data being logged
 */
void
file_rv_allocset_dump_delete_pages (FILE * fp, int length_ignore, void *data)
{
  fprintf (fp, "Npages = %d deleted\n", abs (*(INT32 *) data));
}

/*
 * file_rv_fhdr_delete_pages () - REDO header page information for deleted pages
 *   return: NO_ERROR
 *   rcv(in): Recovery structure
 */
int
file_rv_fhdr_delete_pages (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  FILE_HEADER *fhdr;
  FILE_RECV_DELETE_PAGES *rv_pages;
  VPID *vpid;
  VFID vfid;

  (void) pgbuf_check_page_ptype (thread_p, rcv->pgptr, PAGE_FTAB);

  fhdr = (FILE_HEADER *) (rcv->pgptr + FILE_HEADER_OFFSET);

  if (rcv->length < (int) sizeof (FILE_RECV_DELETE_PAGES))
    {
      fhdr->num_user_pages_mrkdelete -= *(INT32 *) rcv->data;
    }
  else
    {
      LOG_TDES *tdes = LOG_FIND_CURRENT_TDES (thread_p);
      rv_pages = (FILE_RECV_DELETE_PAGES *) rcv->data;
      fhdr->num_user_pages_mrkdelete -= rv_pages->deleted_npages;
      if (rv_pages->need_compaction == 1 && fhdr->num_user_pages_mrkdelete == 0 && log_is_in_crash_recovery () == false)
	{
	  vpid = pgbuf_get_vpid_ptr (rcv->pgptr);
	  vfid.volid = vpid->volid;
	  vfid.fileid = vpid->pageid;

	  (void) file_compress (thread_p, &vfid, true);
	}
    }

  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);

  return NO_ERROR;		/* do not permit error */
}

/*
 * file_rv_fhdr_delete_pages_dump () - Dump file header page recovery information
 *   return: void
 *   length_ignore(in): Length of Recovery Data
 *   data(in): The data being logged
 */
void
file_rv_fhdr_delete_pages_dump (FILE * fp, int length, void *data)
{
  FILE_RECV_DELETE_PAGES *rv_data;

  if (length < (int) sizeof (FILE_RECV_DELETE_PAGES))
    {
      fprintf (fp, "Num_user_pages deleted = %d\n", *(INT32 *) data);
    }
  else
    {
      rv_data = (FILE_RECV_DELETE_PAGES *) data;
      fprintf (fp, "Num_user_pages deleted = %d\n", rv_data->deleted_npages);
      fprintf (fp, "Need_compaction = %d\n", rv_data->need_compaction);
    }
}

/*
 * file_rv_allocset_undoredo_sectortab () - REDO sector address information on
 *                                    allocation set
 *   return: NO_ERROR
 *   rcv(in): Recovery structure
 */
int
file_rv_allocset_undoredo_sectortab (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  FILE_RECV_SHIFT_SECTOR_TABLE *rvsect;
  FILE_ALLOCSET *allocset;

  (void) pgbuf_check_page_ptype (thread_p, rcv->pgptr, PAGE_FTAB);

  rvsect = (FILE_RECV_SHIFT_SECTOR_TABLE *) rcv->data;
  allocset = (FILE_ALLOCSET *) ((char *) rcv->pgptr + rcv->offset);

  allocset->start_sects_vpid = rvsect->start_sects_vpid;
  allocset->end_sects_vpid = rvsect->end_sects_vpid;
  allocset->start_sects_offset = rvsect->start_sects_offset;
  allocset->end_sects_offset = rvsect->end_sects_offset;

  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);

  return NO_ERROR;		/* do not permit error */
}

/*
 * file_rv_allocset_dump_sectortab () - Dump sector address information on
 *                                         allocation set
 *   return: void
 *   length_ignore(in): Length of Recovery Data
 *   data(in): The data being logged
 */
void
file_rv_allocset_dump_sectortab (FILE * fp, int length_ignore, void *data)
{
  FILE_RECV_SHIFT_SECTOR_TABLE *rvsect;

  rvsect = (FILE_RECV_SHIFT_SECTOR_TABLE *) data;

  fprintf (fp, "Sector Table  (Start): Page = %d|%d, Offset = %d\n", rvsect->start_sects_vpid.volid,
	   rvsect->start_sects_vpid.pageid, rvsect->start_sects_offset);
  fprintf (fp, "               (End): Page = %d|%d, Offset = %d\n", rvsect->end_sects_vpid.volid,
	   rvsect->end_sects_vpid.pageid, rvsect->end_sects_offset);

}

/*
 * file_rv_descriptor_undoredo_firstrest_nextvpid () - UNDO/REDO next vpid filed for file
 *                                    descriptor of header page
 *   return: NO_ERROR
 *   rcv(in): Recovery structure
 */
int
file_rv_descriptor_undoredo_firstrest_nextvpid (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  FILE_HEADER *fhdr;

  (void) pgbuf_check_page_ptype (thread_p, rcv->pgptr, PAGE_FTAB);

  fhdr = (FILE_HEADER *) (rcv->pgptr + FILE_HEADER_OFFSET);

  fhdr->des.next_part_vpid = *((VPID *) (rcv->data));

  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);

  return NO_ERROR;		/* do not permit error */
}

/*
 * file_rv_descriptor_dump_firstrest_nextvpid () - Dump undo/redo first rest
 *                                         description VPID page identifier
 *   return: void
 *   length_ignore(in): Length of Recovery Data
 *   data(in): The data being logged
 */
void
file_rv_descriptor_dump_firstrest_nextvpid (FILE * fp, int length_ignore, void *data)
{
  VPID *vpid;

  vpid = (VPID *) data;
  fprintf (fp, "First Rest of file Desc VPID: %d|%d", vpid->volid, vpid->pageid);
}

/*
 * file_rv_descriptor_undoredo_nrest_nextvpid () - UNDO/REDO next vpid filed for file descriptor
 *                                of a rest page
 *   return: NO_ERROR
 *   rcv(in): Recovery structure
 */
int
file_rv_descriptor_undoredo_nrest_nextvpid (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  FILE_REST_DES *rest;

  rest = (FILE_REST_DES *) rcv->pgptr;
  rest->next_part_vpid = *((VPID *) (rcv->data));

  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);

  return NO_ERROR;		/* do not permit error */
}

/*
 * file_rv_descriptor_dump_nrest_nextvpid () - Dump undo/redo first rest description
 *                                     VPID page identifier
 *   return: void
 *   length_ignore(in): Length of Recovery Data
 *   data(in): The data being logged
 */
void
file_rv_descriptor_dump_nrest_nextvpid (FILE * fp, int length_ignore, void *data)
{
  VPID *vpid;

  vpid = (VPID *) data;
  fprintf (fp, "N Rest of file Desc VPID: %d|%d", vpid->volid, vpid->pageid);
}

/*
 * file_rv_descriptor_redo_insert () -
 *   return: NO_ERROR
 *   rcv(in): Recovery structure
 */
int
file_rv_descriptor_redo_insert (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  (void) pgbuf_set_page_ptype (thread_p, rcv->pgptr, PAGE_FTAB);

  return log_rv_copy_char (thread_p, rcv);
}

/*
 * file_rv_tracker_undo_register () - Undo the registration of a file
 *   return: NO_ERROR
 *   rcv(in): Recovery structure
 */
int
file_rv_tracker_undo_register (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  VFID *vfid;
  int ret;

  vfid = (VFID *) rcv->data;
  ret = file_rv_tracker_unregister_logical_undo (thread_p, vfid);

  return NO_ERROR;		/* do not permit error */
}

/*
 * file_rv_tracker_dump_undo_register () - Dump the information to undo the
 *                                       register of a file
 *   return: void
 *   length_ignore(in): Length of Recovery Data
 *   data(in): The data being logged
 */
void
file_rv_tracker_dump_undo_register (FILE * fp, int length_ignore, void *data)
{
  VFID *vfid;

  vfid = (VFID *) data;
  fprintf (fp, "VFID: %d|%d\n", vfid->volid, vfid->fileid);
}

/*
 * file_rv_logical_redo_nop () - Noop recover
 *   return: NO_ERROR
 *   recv(in): Recovery structure
 *
 * Note: Does nothing. This is used to fool the recovery manager when doing
 *       a logical UNDO which fails. For example, unregistering a file that
 *       has been already registered.
 */
int
file_rv_logical_redo_nop (THREAD_ENTRY * thread_p, LOG_RCV * recv)
{
  pgbuf_set_dirty (thread_p, recv->pgptr, DONT_FREE);

  return NO_ERROR;		/* do not permit error */
}

/*
 * file_tmpfile_cache_initialize () -
 *   return: void
 */
static int
file_tmpfile_cache_initialize (void)
{
  int i;

  assert (false);

  file_Tempfile_cache.entry =
    (FILE_TEMPFILE_CACHE_ENTRY *) malloc (sizeof (FILE_TEMPFILE_CACHE_ENTRY) *
					  prm_get_integer_value (PRM_ID_MAX_ENTRIES_IN_TEMP_FILE_CACHE));
  if (file_Tempfile_cache.entry == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      sizeof (FILE_TEMPFILE_CACHE_ENTRY) * prm_get_integer_value (PRM_ID_MAX_ENTRIES_IN_TEMP_FILE_CACHE));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  for (i = 0; i < prm_get_integer_value (PRM_ID_MAX_ENTRIES_IN_TEMP_FILE_CACHE) - 1; i++)
    {
      file_Tempfile_cache.entry[i].idx = i;
      file_Tempfile_cache.entry[i].type = FILE_UNKNOWN_TYPE;
      VFID_SET_NULL (&file_Tempfile_cache.entry[i].vfid);
      file_Tempfile_cache.entry[i].next_entry = i + 1;
    }
  file_Tempfile_cache.entry[i].idx = i;
  file_Tempfile_cache.entry[i].next_entry = -1;

  file_Tempfile_cache.free_idx = 0;
  file_Tempfile_cache.first_idx = -1;

  return NO_ERROR;
}

/*
 * file_tmpfile_cache_finalize () -
 *   return: NO_ERROR
 */
static int
file_tmpfile_cache_finalize (void)
{
  free_and_init (file_Tempfile_cache.entry);

  assert (false);

  return NO_ERROR;
}

/*
 * file_tmpfile_cache_get () -
 *   return:
 *   vfid(in):
 *   file_type(in):
 */
static VFID *
file_tmpfile_cache_get (THREAD_ENTRY * thread_p, VFID * vfid, FILE_TYPE file_type)
{
  FILE_TEMPFILE_CACHE_ENTRY *p;
  int idx, prev;
  int rv;

  assert (false);

  rv = csect_enter (thread_p, CSECT_TEMPFILE_CACHE, INF_WAIT);

  idx = file_Tempfile_cache.first_idx;
  prev = -1;
  while (idx != -1)
    {
      p = &file_Tempfile_cache.entry[idx];
      if (p->type == file_type)
	{
	  if (prev == -1)
	    {
	      file_Tempfile_cache.first_idx = p->next_entry;
	    }
	  else
	    {
	      file_Tempfile_cache.entry[prev].next_entry = p->next_entry;
	    }

	  p->next_entry = file_Tempfile_cache.free_idx;
	  file_Tempfile_cache.free_idx = p->idx;

	  VFID_COPY (vfid, &p->vfid);

	  file_cache_newfile (thread_p, vfid, file_type);
	  break;
	}
      prev = idx;
      idx = p->next_entry;
    }

  csect_exit (thread_p, CSECT_TEMPFILE_CACHE);

  return (idx == -1) ? NULL : vfid;
}

/*
 * file_tmpfile_cache_put () -
 *   return:
 *   vfid(in):
 *   file_type(in):
 */
static int
file_tmpfile_cache_put (THREAD_ENTRY * thread_p, const VFID * vfid, FILE_TYPE file_type)
{
  FILE_TEMPFILE_CACHE_ENTRY *p = NULL;
  int rv;

  assert (false);

  rv = csect_enter (thread_p, CSECT_TEMPFILE_CACHE, INF_WAIT);

  if (file_Tempfile_cache.free_idx != -1)
    {
      p = &file_Tempfile_cache.entry[file_Tempfile_cache.free_idx];
      file_Tempfile_cache.free_idx = p->next_entry;

      p->next_entry = file_Tempfile_cache.first_idx;
      file_Tempfile_cache.first_idx = p->idx;

      VFID_COPY (&p->vfid, vfid);
      p->type = file_type;

      (void) file_new_declare_as_old (thread_p, vfid);
    }

  csect_exit (thread_p, CSECT_TEMPFILE_CACHE);

  return (p == NULL) ? 0 : 1;
}

/*
 * file_descriptor_get_length():
 *
 *   returns: the size of the file descriptor of the given file type.
 *   file_type(IN): file_type
 *
 */
static int
file_descriptor_get_length (const FILE_TYPE file_type)
{
  switch (file_type)
    {
    case FILE_HEAP:
    case FILE_HEAP_REUSE_SLOTS:
      return sizeof (FILE_HEAP_DES);
    case FILE_MULTIPAGE_OBJECT_HEAP:
      return sizeof (FILE_OVF_HEAP_DES);
    case FILE_BTREE:
      return sizeof (FILE_BTREE_DES);
    case FILE_BTREE_OVERFLOW_KEY:
      return sizeof (FILE_OVF_BTREE_DES);
    case FILE_EXTENDIBLE_HASH:
    case FILE_EXTENDIBLE_HASH_DIRECTORY:
      return sizeof (FILE_EHASH_DES);
    case FILE_DROPPED_FILES:
    case FILE_VACUUM_DATA:
    case FILE_TRACKER:
    case FILE_CATALOG:
    case FILE_QUERY_AREA:
    case FILE_TEMP:
    case FILE_UNKNOWN_TYPE:
    default:
      return 0;
    }
}

/*
 * file_descriptor_dump():
 *      dump the file descriptor of the given file type.
 *
 *   returns: none
 *   file_type(IN): file_type
 *   file_des_p(IN): ptr to the file descritor
 *
 */

static void
file_descriptor_dump (THREAD_ENTRY * thread_p, FILE * fp, const FILE_TYPE file_type, const void *file_des_p,
		      const VFID * vfid)
{
  if (file_des_p == NULL)
    {
      return;
    }

  switch (file_type)
    {
    case FILE_TRACKER:
      break;

    case FILE_HEAP:
    case FILE_HEAP_REUSE_SLOTS:
      file_descriptor_dump_heap (thread_p, fp, (const FILE_HEAP_DES *) file_des_p);
      break;

    case FILE_MULTIPAGE_OBJECT_HEAP:
      file_descriptor_dump_multi_page_object_heap (fp, (const FILE_OVF_HEAP_DES *) file_des_p);
      break;

    case FILE_BTREE:
      file_descriptor_dump_btree (thread_p, fp, (const FILE_BTREE_DES *) file_des_p, vfid);
      break;

    case FILE_BTREE_OVERFLOW_KEY:
      file_descriptor_dump_btree_overflow_key (fp, (const FILE_OVF_BTREE_DES *) file_des_p);
      break;

    case FILE_EXTENDIBLE_HASH:
    case FILE_EXTENDIBLE_HASH_DIRECTORY:
      file_descriptor_dump_extendible_hash (thread_p, fp, (const FILE_EHASH_DES *) file_des_p);
      break;
    case FILE_CATALOG:
    case FILE_QUERY_AREA:
    case FILE_TEMP:
    case FILE_UNKNOWN_TYPE:
    case FILE_DROPPED_FILES:
    case FILE_VACUUM_DATA:
    default:
      fprintf (fp, "....Don't know how to dump desc..\n");
      break;
    }
}

static void
file_descriptor_dump_heap (THREAD_ENTRY * thread_p, FILE * fp, const FILE_HEAP_DES * heap_file_des_p)
{
  file_print_name_of_class (thread_p, fp, &heap_file_des_p->class_oid);
}

static void
file_descriptor_dump_multi_page_object_heap (FILE * fp, const FILE_OVF_HEAP_DES * ovf_hfile_des_p)
{
  fprintf (fp, "Overflow for HFID: %2d|%4d|%4d\n", ovf_hfile_des_p->hfid.vfid.volid, ovf_hfile_des_p->hfid.vfid.fileid,
	   ovf_hfile_des_p->hfid.hpgid);
}

static void
file_descriptor_dump_btree (THREAD_ENTRY * thread_p, FILE * fp, const FILE_BTREE_DES * btree_des_p, const VFID * vfid)
{
  file_print_class_name_index_name_with_attrid (thread_p, fp, &btree_des_p->class_oid, vfid, btree_des_p->attr_id);
}

static void
file_descriptor_dump_btree_overflow_key (FILE * fp, const FILE_OVF_BTREE_DES * btree_ovf_des_p)
{
  fprintf (fp, "Overflow keys for BTID: %2d|%4d|%4d\n", btree_ovf_des_p->btid.vfid.volid,
	   btree_ovf_des_p->btid.vfid.fileid, btree_ovf_des_p->btid.root_pageid);
}

static void
file_descriptor_dump_extendible_hash (THREAD_ENTRY * thread_p, FILE * fp, const FILE_EHASH_DES * ext_hash_des_p)
{
  file_print_name_of_class_with_attrid (thread_p, fp, &ext_hash_des_p->class_oid, ext_hash_des_p->attr_id);
}

static void
file_print_name_of_class (THREAD_ENTRY * thread_p, FILE * fp, const OID * class_oid_p)
{
  char *class_name_p = NULL;

  if (!OID_ISNULL (class_oid_p))
    {
      class_name_p = heap_get_class_name (thread_p, class_oid_p);
      fprintf (fp, "CLASS_OID:%2d|%4d|%2d (%s)\n", class_oid_p->volid, class_oid_p->pageid, class_oid_p->slotid,
	       (class_name_p) ? class_name_p : "*UNKNOWN-CLASS*");
      if (class_name_p)
	{
	  free_and_init (class_name_p);
	}
    }
  else
    {
      fprintf (fp, "\n");
    }
}

static void
file_print_class_name_of_instance (THREAD_ENTRY * thread_p, FILE * fp, const OID * inst_oid_p)
{
  char *class_name_p = NULL;

  if (!OID_ISNULL (inst_oid_p))
    {
      class_name_p = heap_get_class_name_of_instance (thread_p, inst_oid_p);
      fprintf (fp, "CLASS_OID:%2d|%4d|%2d (%s)\n", inst_oid_p->volid, inst_oid_p->pageid, inst_oid_p->slotid,
	       (class_name_p) ? class_name_p : "*UNKNOWN-CLASS*");
      if (class_name_p)
	{
	  free_and_init (class_name_p);
	}
    }
  else
    {
      fprintf (fp, "\n");
    }
}

static void
file_print_name_of_class_with_attrid (THREAD_ENTRY * thread_p, FILE * fp, const OID * class_oid_p, const int attr_id)
{
  char *class_name_p = NULL;

  if (!OID_ISNULL (class_oid_p))
    {
      class_name_p = heap_get_class_name (thread_p, class_oid_p);
      fprintf (fp, "CLASS_OID:%2d|%4d|%2d (%s), ATTRID: %2d\n", class_oid_p->volid, class_oid_p->pageid,
	       class_oid_p->slotid, (class_name_p) ? class_name_p : "*UNKNOWN-CLASS*", attr_id);
      if (class_name_p)
	{
	  free_and_init (class_name_p);
	}
    }
  else
    {
      fprintf (fp, "\n");
    }
}

/*
 * file_print_class_name_index_name_with_attrid () -\
 * return: void
 * thread_p(in):
 * fp(in):
 * class_oid_p(in):
 * vfid(in):
 * attr_id(in);
 *
 * Note:
 */
static void
file_print_class_name_index_name_with_attrid (THREAD_ENTRY * thread_p, FILE * fp, const OID * class_oid_p,
					      const VFID * vfid, const int attr_id)
{
  char *class_name_p = NULL;
  char *index_name_p = NULL;
  BTID btid;

  assert (fp != NULL && class_oid_p != NULL && vfid != NULL);

  if (OID_ISNULL (class_oid_p) || VFID_ISNULL (vfid))
    {
      goto end;
    }

  /* class_name */
  class_name_p = heap_get_class_name (thread_p, class_oid_p);

  /* index_name root_pageid doesn't matter here, see BTID_IS_EQUAL */
  btid.vfid = *vfid;
  btid.root_pageid = NULL_PAGEID;
  if (heap_get_indexinfo_of_btid (thread_p, (OID *) class_oid_p, &btid, NULL, NULL, NULL, NULL, &index_name_p, NULL) !=
      NO_ERROR)
    {
      goto end;
    }

  /* print */
  fprintf (fp, "CLASS_OID:%2d|%4d|%2d (%s), %s, ATTRID: %2d", class_oid_p->volid, class_oid_p->pageid,
	   class_oid_p->slotid, (class_name_p == NULL) ? "*UNKNOWN-CLASS*" : class_name_p,
	   (index_name_p == NULL) ? "*UNKNOWN-INDEX*" : index_name_p, attr_id);

end:

  fprintf (fp, "\n");

  /* cleanup */
  if (class_name_p != NULL)
    {
      free_and_init (class_name_p);
    }

  if (index_name_p != NULL)
    {
      free_and_init (index_name_p);
    }
}

/*
 * file_update_used_pages_of_vol_header () -
 *   This function is used in migration tool (migration_91_to_92).
 *   Calculates used_data_npages and used_index_npages
 *   of each volume with fileTracker and Updates volume header.
 *
 *   return: either: DISK_INVALID, DISK_VALID, DISK_ERROR
 *
 * Note:
 */
DISK_ISVALID
file_update_used_pages_of_vol_header (THREAD_ENTRY * thread_p)
{
#if defined (SA_MODE)
  /* the data/index/temp is not tracked anymore. */
  /* todo: remove me */
  return DISK_VALID;
#else /* !SA_MODE */
  return DISK_ERROR;
#endif /* !SA_MODE */
}

/*
 * file_construct_space_info () -
 *   return: either: DISK_INVALID, DISK_VALID, DISK_ERROR
 *   space_info(out):
 *   vfid(in):
 */
static DISK_ISVALID
file_construct_space_info (THREAD_ENTRY * thread_p, VOL_SPACE_INFO * space_info, const VFID * vfid)
{
#if defined (SA_MODE)
  DISK_ISVALID valid = DISK_VALID;
  FILE_HEADER *fhdr;
  PAGE_PTR fhdr_pgptr = NULL;
  PAGE_PTR pgptr = NULL;
  VPID set_vpids[FILE_SET_NUMVPIDS];
  int num_found;
  int i, j;
  VPID next_ftable_vpid;
  int num_user_pages;
  DISK_PAGE_TYPE page_type;

  /* todo: fix me */
  return DISK_VALID;
#else /* !SA_MODE */
  return DISK_ERROR;
#endif /* !SA_MODE */
}

/************************************************************************/
/*                                                                      */
/* FILE MANAGER REDESIGN                                                */
/*                                                                      */
/************************************************************************/

/* TODO:
 * x. Currently, we had some issue with vacuum workers and nested system operations that could be committed.
 *    We fixed it by forcing all nested system operations open by vacuum workers to be attached to outer. Then top
 *    system operation was committed. However, with the new file manager design, this can become a problem.
 *    Allocate, deallocate routines can change the partial and full sector tables. If the file header page is released
 *    before committing changes, concurrent threads are free to do their own page allocations/deallocations, modifying
 *    the file table structures. To undo original changes then, we have to do "logical" undo, because there is no
 *    guarantee the modified sector is not relocated to another file table page.
 *    That alone would not be a problem, but it logical undo records does not work well under a system operation.
 *    We would have to first log the logical undo operation and then consider all cases of partial redo in that
 *    system transaction. For instance, when we want to move a full sector to partial sectors in two steps - let's say
 *    we first delete the entry in full table and then we append a new entry to partial sectors - we need to consider
 *    three cases: no changes were made (and undo is not necessary), the entry was only removed from full table (and
 *    undo does not have to remove the entry from partial table) or all changes were made and undo must relocate the
 *    entry back from partial to full table. This example is rather trivial, and is theoretically possible to handle it.
 *    However, in some cases, we might have to also allocate/deallocate file table pages in the process, which makes
 *    logical undo impractical.
 *
 *    The most practical way is to hold the file header until the system operation is committed and changes are
 *    considered final. Page allocations are committed immediately anyway, while page deallocations are executed during
 *    run postpone and also committed immediately.
 *
 *    So, file_alloc_page and file_dealloc_page will output the file header page. The caller has the responsibility to
 *    keep the file header fixed until the system operation si committed and release it afterwards.
 * x. improve merging extdata.
 */

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
/* TODO: Rename as FILE_HEADER */
typedef struct flre_header FLRE_HEADER;
struct flre_header
{
  VFID self;			/* Self VFID */
  FILE_TABLESPACE tablespace;	/* The table space definition */
  FILE_DESCRIPTORS descriptor;	/* File descriptor. Depends on file type. */

  INT64 time_creation;		/* Time of file creation. */

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

  VOLID volid_last_expand;	/* Last volume used for expansion. */

  INT32 file_flags;		/* File flags. */

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
};

/* Disk size of file header. */
#define FILE_HEADER_ALIGNED_SIZE ((INT16) (DB_ALIGN (sizeof (FLRE_HEADER), MAX_ALIGNMENT)))

/* TODO: Add flags. */
#define FILE_FLAG_NUMERABLE	    0x1	/* Is file numerable */
#define FILE_FLAG_TEMPORARY	    0x2	/* Is file temporary */
#define FILE_FLAG_MARK_DELETED	    0x4	/* TODO: see if this is necessary. */

#define FILE_IS_NUMERABLE(fh) (((fh)->file_flags & FILE_FLAG_NUMERABLE) != 0)
#define FILE_IS_TEMPORARY(fh) (((fh)->file_flags & FILE_FLAG_TEMPORARY) != 0)

/* Numerable file types. Currently, we used this property for extensible hashes and sort files. */
#define FILE_TYPE_CAN_BE_NUMERABLE(ftype) ((ftype) == FILE_EXTENDIBLE_HASH \
					   || (ftype) == FILE_EXTENDIBLE_HASH_DIRECTORY \
					   || (ftype) == FILE_TEMP)

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
typedef int (*FILE_EXTDATA_FUNC) (THREAD_ENTRY * thread_p, const FILE_EXTENSIBLE_DATA * extdata, bool * stop,
				  void *args);
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
  "\t\tvpid_last_user_page_ftab = %d \n"
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
#define FILE_PARTSECT_AS_ARGS(ps) VSID_AS_ARGS (&(ps)->vsid), (ps)->page_bitmap

#define FILE_ALLOC_TYPE_STRING(alloc_type) \
  ((alloc_type) == FILE_ALLOC_USER_PAGE ? "alloc user page" : "alloc table page")

#define FILE_TEMPCACHE_MSG \
  "\ttempcache: \n" \
  "\t\tfile cache: max = %d, numerable = %d, regular = %d, total = %d \n" \
  "\t\tfree entries: max = %d, count = %d \n"
#define FILE_TEMPCACHE_AS_ARGS \
  flre_Tempcache->ncached_max, flre_Tempcache->ncached_numerable, flre_Tempcache->cached_not_numerable, \
  flre_Tempcache->ncached_numerable + flre_Tempcache->cached_not_numerable, \
  flre_Tempcache->nfree_entries_max, flre_Tempcache->nfree_entries

#define FILE_TEMPCACHE_ENTRY_MSG "%p, VFID %d|%d, %s"
#define FILE_TEMPCACHE_ENTRY_AS_ARGS(ent) ent, VFID_AS_ARGS (&(ent)->vfid), file_type_to_string ((ent)->ftype)

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
typedef struct flre_tempcache_entry FLRE_TEMPCACHE_ENTRY;
struct flre_tempcache_entry
{
  VFID vfid;
  FILE_TYPE ftype;

  FLRE_TEMPCACHE_ENTRY *next;
};

typedef struct flre_tempcache FLRE_TEMPCACHE;
struct flre_tempcache
{
  FLRE_TEMPCACHE_ENTRY *free_entries;	/* preallocated entries free to be use */
  int nfree_entries_max;
  int nfree_entries;

  FLRE_TEMPCACHE_ENTRY *cached_not_numerable;	/* cached temporary files */
  FLRE_TEMPCACHE_ENTRY *cached_numerable;	/* cached temporary numerable files */
  int ncached_max;
  int ncached_not_numerable;
  int ncached_numerable;

  pthread_mutex_t mutex;
#if !defined (NDEBUG)
  int owner_mutex;
#endif				/* !NDEBUG */

  FLRE_TEMPCACHE_ENTRY **tran_files;	/* transaction temporary files */
};

static FLRE_TEMPCACHE *flre_Tempcache = NULL;

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

STATIC_INLINE void file_header_sanity_check (FLRE_HEADER * fhead) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void file_header_alloc (FLRE_HEADER * fhead, FILE_ALLOC_TYPE alloc_type, bool was_empty, bool is_full)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void file_header_dealloc (FLRE_HEADER * fhead, FILE_ALLOC_TYPE alloc_type, bool is_empty, bool was_full)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void file_log_fhead_alloc (THREAD_ENTRY * thread_p, PAGE_PTR page_fhead, FILE_ALLOC_TYPE alloc_type,
					 bool was_empty, bool is_full) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void file_log_fhead_dealloc (THREAD_ENTRY * thread_p, PAGE_PTR page_fhead, FILE_ALLOC_TYPE alloc_type,
					   bool is_empty, bool was_full) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void file_header_update_mark_deleted (THREAD_ENTRY * thread_p, PAGE_PTR page_fhead, int delta)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int file_header_copy (THREAD_ENTRY * thread_p, const VFID * vfid, FLRE_HEADER * fhead_copy)
  __attribute__ ((ALWAYS_INLINE));

/************************************************************************/
/* File extensible data section                                         */
/************************************************************************/

STATIC_INLINE void file_extdata_init (INT16 item_size, INT16 max_size, FILE_EXTENSIBLE_DATA * extdata)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int file_extdata_max_size (const FILE_EXTENSIBLE_DATA * extdata) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int file_extdata_size (const FILE_EXTENSIBLE_DATA * extdata) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void *file_extdata_start (const FILE_EXTENSIBLE_DATA * extdata) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void *file_extdata_end (const FILE_EXTENSIBLE_DATA * extdata) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE bool file_extdata_is_full (const FILE_EXTENSIBLE_DATA * extdata) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE bool file_extdata_is_empty (const FILE_EXTENSIBLE_DATA * extdata) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE INT16 file_extdata_item_count (const FILE_EXTENSIBLE_DATA * extdata) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE INT16 file_extdata_remaining_capacity (const FILE_EXTENSIBLE_DATA * extdata)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void file_extdata_append (FILE_EXTENSIBLE_DATA * extdata, const void *append_data)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void file_extdata_append_array (FILE_EXTENSIBLE_DATA * extdata, const void *append_data, INT16 count)
  __attribute__ ((ALWAYS_INLINE));
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
STATIC_INLINE void file_extdata_insert_at (FILE_EXTENSIBLE_DATA * extdata, int position, int count, const void *data)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void file_extdata_remove_at (FILE_EXTENSIBLE_DATA * extdata, int position, int count)
  __attribute__ ((ALWAYS_INLINE));
static int file_extdata_apply_funcs (THREAD_ENTRY * thread_p, FILE_EXTENSIBLE_DATA * extdata_in,
				     FILE_EXTDATA_FUNC f_extdata, void *f_extdata_args, FILE_EXTDATA_ITEM_FUNC f_item,
				     void *f_item_args, bool for_write, FILE_EXTENSIBLE_DATA ** extdata_out,
				     PAGE_PTR * page_out);
static int file_extdata_item_func_for_search (THREAD_ENTRY * thread_p, const void *item, int index, bool * stop,
					      void *args);
static int file_extdata_func_for_search_ordered (THREAD_ENTRY * thread_p, const FILE_EXTENSIBLE_DATA * extdata,
						 bool * stop, void *args);
static int file_extdata_search_item (THREAD_ENTRY * thread_p, FILE_EXTENSIBLE_DATA ** extdata,
				     const void *item_to_find, int (*compare_func) (const void *, const void *),
				     bool is_ordered, bool for_write, bool * found, int *position,
				     PAGE_PTR * page_extdata);
static int file_extdata_find_not_full (THREAD_ENTRY * thread_p, FILE_EXTENSIBLE_DATA ** extdata, PAGE_PTR * page_out,
				       bool * found);
STATIC_INLINE void file_log_extdata_add (THREAD_ENTRY * thread_p, const FILE_EXTENSIBLE_DATA * extdata, PAGE_PTR page,
					 int position, int count, const void *data) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void file_log_extdata_remove (THREAD_ENTRY * thread_p, const FILE_EXTENSIBLE_DATA * extdata,
					    PAGE_PTR page, int position, int count) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void file_log_extdata_set_next (THREAD_ENTRY * thread_p, const FILE_EXTENSIBLE_DATA * extdata,
					      PAGE_PTR page, VPID * vpid_next) __attribute__ ((ALWAYS_INLINE));
static int file_rv_extdata_merge_redo_internal (THREAD_ENTRY * thread_p, LOG_RCV * rcv,
						int (*compare_func) (const void *, const void *));
STATIC_INLINE void file_log_extdata_merge (THREAD_ENTRY * thread_p, const FILE_EXTENSIBLE_DATA * extdata_dest,
					   PAGE_PTR page_dest, const FILE_EXTENSIBLE_DATA * extdata_src,
					   LOG_RCVINDEX rcvindex) __attribute__ ((ALWAYS_INLINE));

/************************************************************************/
/* Partially allocated sectors section                                  */
/************************************************************************/

STATIC_INLINE bool file_partsect_is_full (FILE_PARTIAL_SECTOR * partsect) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE bool file_partsect_is_empty (FILE_PARTIAL_SECTOR * partsect) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE bool file_partsect_is_bit_set (FILE_PARTIAL_SECTOR * partsect, int offset)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void file_partsect_set_bit (FILE_PARTIAL_SECTOR * partsect, int offset) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void file_partsect_clear_bit (FILE_PARTIAL_SECTOR * partsect, int offset) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int file_partsect_pageid_to_offset (FILE_PARTIAL_SECTOR * partsect, PAGEID pageid)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE bool file_partsect_alloc (FILE_PARTIAL_SECTOR * partsect, VPID * vpid_out, int *offset_out)
  __attribute__ ((ALWAYS_INLINE));

static int file_rv_partsect_update (THREAD_ENTRY * thread_p, LOG_RCV * rcv, bool set);

/************************************************************************/
/* Utility functions.                                                   */
/************************************************************************/

static int file_compare_vsids (const void *a, const void *b);
static int file_compare_vpids (const void *first, const void *second);

/************************************************************************/
/* File manipulation section                                            */
/************************************************************************/

static int file_table_collect_vsid (THREAD_ENTRY * thread_p, const void *item, int index_unused, bool * stop,
				    void *args);
static int file_perm_expand (THREAD_ENTRY * thread_p, PAGE_PTR page_fhead);
static int file_table_move_partial_sectors_to_header (THREAD_ENTRY * thread_p, PAGE_PTR page_fhead);
static int file_table_add_full_sector (THREAD_ENTRY * thread_p, PAGE_PTR page_fhead, const VSID * vsid);
static int file_table_collect_ftab_pages (THREAD_ENTRY * thread_p, const FILE_EXTENSIBLE_DATA * extdata, bool * stop,
					  void *args);
static int file_perm_alloc (THREAD_ENTRY * thread_p, PAGE_PTR page_fhead, FILE_ALLOC_TYPE alloc_type,
			    VPID * vpid_alloc_out);
static int file_perm_dealloc (THREAD_ENTRY * thread_p, PAGE_PTR page_fhead, const VPID * vpid_dealloc,
			      FILE_ALLOC_TYPE alloc_type);
STATIC_INLINE int file_table_try_extdata_merge (THREAD_ENTRY * thread_p, PAGE_PTR page_fhead, PAGE_PTR page_dest,
						FILE_EXTENSIBLE_DATA * extdata_dest,
						int (*compare_func) (const void *, const void *), LOG_RCVINDEX rcvindex)
  __attribute__ ((ALWAYS_INLINE));
static int file_rv_dealloc_internal (THREAD_ENTRY * thread_p, LOG_RCV * rcv, bool compensate_or_run_postpone);

STATIC_INLINE int flre_create_temp_internal (THREAD_ENTRY * thread_p, int npages, FILE_TYPE ftype, bool is_numerable,
					     VFID * vfid_out) __attribute__ ((ALWAYS_INLINE));

/************************************************************************/
/* Numerable files section.                                             */
/************************************************************************/

static int file_numerable_add_page (THREAD_ENTRY * thread_p, PAGE_PTR page_fhead, const VPID * vpid);
static int file_extdata_find_nth_vpid (THREAD_ENTRY * thread_p, const FILE_EXTENSIBLE_DATA * extdata, bool * stop,
				       void *args);
static int file_extdata_find_nth_vpid_and_skip_marked (THREAD_ENTRY * thread_p, const void *data, int index,
						       bool * stop, void *args);

/************************************************************************/
/* Temporary files section                                               */
/************************************************************************/

static int file_temp_alloc (THREAD_ENTRY * thread_p, PAGE_PTR page_fhead, FILE_ALLOC_TYPE alloc_type,
			    VPID * vpid_alloc_out);
STATIC_INLINE int flre_temp_set_type (THREAD_ENTRY * thread_p, VFID * vfid, FILE_TYPE ftype)
  __attribute__ ((ALWAYS_INLINE));
static int file_temp_reset_user_pages (THREAD_ENTRY * thread_p, const VFID * vfid);

/************************************************************************/
/* Temporary cache section                                               */
/************************************************************************/

static int flre_tempcache_init (void);
static void flre_tempcache_final (void);
STATIC_INLINE void flre_tempcache_lock (void) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void flre_tempcache_unlock (void) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void flre_tempcache_check_lock (void) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void flre_tempcache_free_entry_list (FLRE_TEMPCACHE_ENTRY ** list) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int flre_tempcache_alloc_entry (FLRE_TEMPCACHE_ENTRY ** entry) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void flre_tempcache_retire_entry (FLRE_TEMPCACHE_ENTRY * entry) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int flre_tempcache_get (THREAD_ENTRY * thread_p, FILE_TYPE ftype, bool numerable,
				      FLRE_TEMPCACHE_ENTRY ** entry) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE bool flre_tempcache_put (THREAD_ENTRY * thread_p, FLRE_TEMPCACHE_ENTRY * entry)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void file_tempcache_cache_or_drop_entries (THREAD_ENTRY * thread_p, FLRE_TEMPCACHE_ENTRY ** entries)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE FLRE_TEMPCACHE_ENTRY *flre_tempcache_pop_tran_file (THREAD_ENTRY * thread_p, const VFID * vfid)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void flre_tempcache_push_tran_file (THREAD_ENTRY * thread_p, FLRE_TEMPCACHE_ENTRY * entry)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void flre_tempcache_dump (FILE * fp) __attribute__ ((ALWAYS_INLINE));

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
 * flre_manager_init () - initialize file manager
 */
int
flre_manager_init (void)
{
  file_Logging = prm_get_bool_value (PRM_ID_FILE_LOGGING);

  return flre_tempcache_init ();
}

/*
 * flre_manager_final () - finalize file manager
 */
void
flre_manager_final (void)
{
  flre_tempcache_final ();
}

/************************************************************************/
/* File header section.                                                 */
/************************************************************************/

/*
 * file_header_sanity_check () - Debug function used to check the sanity of file header before and after certaion file
 *				 file operations.
 *
 * return     : Void.
 * fhead (in) : File header.
 */
STATIC_INLINE void
file_header_sanity_check (FLRE_HEADER * fhead)
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
  FLRE_HEADER *fhead;

  assert (rcv->pgptr != NULL);
  assert (rcv->length == sizeof (VPID));

  fhead = (FLRE_HEADER *) page_fhead;
  file_header_sanity_check (fhead);

  /* the correct VPID is logged. */

  VPID_COPY (&fhead->vpid_last_user_page_ftab, vpid);
  file_header_sanity_check (fhead);

  file_log ("file_rv_fhead_set_last_user_page_ftab", "update vpid_last_user_page_ftab to %d|%d in file %d|%d, "
	    "header page %d|%d, lsa %lld|%d ", VPID_AS_ARGS (vpid), VFID_AS_ARGS (&fhead->self),
	    PGBUF_PAGE_VPID_AS_ARGS (page_fhead), PGBUF_PAGE_LSA_AS_ARGS (page_fhead));

  pgbuf_set_dirty (thread_p, page_fhead, DONT_FREE);
  return NO_ERROR;
}

/*
 * file_header_dealloc () - Update stats in file header for page allocation.
 *
 * return	   : Void
 * fhead (in)      : File header
 * alloc_type (in) : User/table page
 * was_empty (in)  : True if sector was empty before allocation.
 * is_full (in)    : True if sector is full after allocation.
 */
STATIC_INLINE void
file_header_alloc (FLRE_HEADER * fhead, FILE_ALLOC_TYPE alloc_type, bool was_empty, bool is_full)
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
file_header_dealloc (FLRE_HEADER * fhead, FILE_ALLOC_TYPE alloc_type, bool is_empty, bool was_full)
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
  FLRE_HEADER *fhead;
  bool is_ftab_page;
  bool is_full;
  bool was_empty;

  assert (rcv->length == sizeof (bool) * 3);
  is_ftab_page = ((bool *) rcv->data)[0];
  was_empty = ((bool *) rcv->data)[1];
  is_full = ((bool *) rcv->data)[2];

  fhead = (FLRE_HEADER *) page_fhead;

  file_header_alloc (fhead, is_ftab_page ? FILE_ALLOC_TABLE_PAGE : FILE_ALLOC_USER_PAGE, was_empty, is_full);

  file_log ("file_rv_fhead_alloc", "update header in file %d|%d, header page %d|%d, lsa %lld|%d, "
	    "after %s, was_empty %s, is_full %s \n" FILE_HEAD_ALLOC_MSG,
	    VFID_AS_ARGS (&fhead->self), PGBUF_PAGE_VPID_AS_ARGS (page_fhead), PGBUF_PAGE_LSA_AS_ARGS (page_fhead),
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
  FLRE_HEADER *fhead;
  bool is_ftab_page;
  bool was_full;
  bool is_empty;

  assert (rcv->length == sizeof (bool) * 3);
  is_ftab_page = ((bool *) rcv->data)[0];
  is_empty = ((bool *) rcv->data)[1];
  was_full = ((bool *) rcv->data)[2];

  fhead = (FLRE_HEADER *) page_fhead;

  file_header_dealloc (fhead, is_ftab_page ? FILE_ALLOC_TABLE_PAGE : FILE_ALLOC_USER_PAGE, is_empty, was_full);

  file_log ("file_rv_fhead_dealloc", "update header in file %d|%d, header page %d|%d, lsa %lld|%d, "
	    "after de%s, is_empty %s, was_full %s \n" FILE_HEAD_ALLOC_MSG,
	    VFID_AS_ARGS (&fhead->self), PGBUF_PAGE_VPID_AS_ARGS (page_fhead), PGBUF_PAGE_LSA_AS_ARGS (page_fhead),
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
  log_append_undoredo_data (thread_p, RVFL_FHEAD_ALLOC, &addr, sizeof (log_bools), sizeof (log_bools), log_bools,
			    log_bools);
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
  log_append_undoredo_data (thread_p, RVFL_FHEAD_DEALLOC, &addr, sizeof (log_bools), sizeof (log_bools), log_bools,
			    log_bools);
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
  FLRE_HEADER *fhead = (FLRE_HEADER *) page_fhead;
  int undo_delta = -delta;

  LOG_LSA save_lsa = *pgbuf_get_lsa (page_fhead);

  fhead->n_page_mark_delete += delta;

  if (!FILE_IS_TEMPORARY (fhead))
    {
      log_append_undoredo_data2 (thread_p, RVFL_FHEAD_MARK_DELETE, NULL, page_fhead, 0, sizeof (undo_delta),
				 sizeof (delta), &undo_delta, &delta);
    }
  pgbuf_set_dirty (thread_p, page_fhead, DONT_FREE);

  file_log ("file_header_update_mark_deleted", "updated n_page_mark_delete by %d to %d in file %d|%d, "
	    "header page %d|%d, prev_lsa %lld|%d, crt_lsa %lld|%d ", delta, fhead->n_page_mark_delete,
	    VFID_AS_ARGS (&fhead->self), PGBUF_PAGE_VPID_AS_ARGS (page_fhead), LSA_AS_ARGS (&save_lsa),
	    PGBUF_PAGE_LSA_AS_ARGS (page_fhead));
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
  FLRE_HEADER *fhead;

  assert (page_fhead != NULL);
  assert (rcv->length == sizeof (int));

  fhead = (FLRE_HEADER *) page_fhead;
  fhead->n_page_mark_delete += delta;
  pgbuf_set_dirty (thread_p, page_fhead, DONT_FREE);

  file_log ("file_rv_header_update_mark_deleted", "modified n_page_mark_delete by %d to %d in file %d|%d, "
	    "header page %d|%d, lsa %lld|%d", delta, fhead->n_page_mark_delete, VFID_AS_ARGS (&fhead->self),
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
file_header_copy (THREAD_ENTRY * thread_p, const VFID * vfid, FLRE_HEADER * fhead_copy)
{
  VPID vpid_fhead;
  PAGE_PTR page_fhead = NULL;
  FLRE_HEADER *fhead = NULL;

  int error_code = NO_ERROR;

  FILE_GET_HEADER_VPID (vfid, &vpid_fhead);
  page_fhead = pgbuf_fix (thread_p, &vpid_fhead, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (page_fhead == NULL)
    {
      ASSERT_ERROR_AND_SET (error_code);
      return error_code;
    }
  fhead = (FLRE_HEADER *) page_fhead;
  file_header_sanity_check (fhead);

  *fhead_copy = *fhead;

  pgbuf_unfix (thread_p, page_fhead);
  return NO_ERROR;
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
  while (dest_ptr <= dest_end_ptr)
    {
      /* collect all items from source that are smaller than current destination item. */
      for (src_new_ptr = src_ptr; src_new_ptr <= src_end_ptr; src_ptr += extdata_src->size_of_item)
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

	  assert (dest_end_ptr <= debug_dest_end_ptr);
	}
      src_ptr = src_new_ptr;
      if (src_ptr == src_end_ptr)
	{
	  /* source extensible data was consumed. */
	  break;
	}
      /* skip all items from destination smaller than current source item */
      for (; dest_ptr <= dest_end_ptr; dest_ptr += extdata_dest->size_of_item)
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
  int min = 0;
  int max = file_extdata_item_count (extdata) - 1;
  int mid = 0;
  int compare = 0;
  void *item_at_mid = NULL;

  assert (found != NULL);
  assert (position != NULL);

  *found = false;
  *position = -1;

  /* binary search */
  /* keep searching while range is still valid. */
  while (min <= max)
    {
      /* get range midpoint. */
      mid = (min + max) / 2;

      /* get mid item */
      item_at_mid = file_extdata_at (extdata, mid);

      /* compare mid item to given item */
      compare = compare_func (item_at_mid, item_to_find);
      if (compare == 0)
	{
	  /* found given item */
	  *found = true;
	  *position = mid;
	  return;
	}

      if (compare > 0)
	{
	  /* mid is greater. search in lower range. */
	  max = mid - 1;
	}
      else
	{
	  /* mid is smaller. search in upper range. also increment mid just in case the loop is exited; mid will point
	   * to first bigger item. */
	  min = ++mid;
	}
    }
  /* not found. mid is currently the position of first item bigger than given item. */
  *found = false;
  *position = mid;

  /* TODO: we can create a generic function of this binary search with position. It is also used at least twice in
   *       b-tree. */
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

  while (true)
    {
      if (f_extdata)
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
			  int (*compare_func) (const void *, const void *), bool is_ordered, bool for_write,
			  bool * found, int *position, PAGE_PTR * page_extdata)
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
      error_code =
	file_extdata_apply_funcs (thread_p, extdata_in, file_extdata_func_for_search_ordered, &search_context, NULL,
				  NULL, for_write, extdata, page_extdata);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  assert (*page_extdata == NULL);
	  return error_code;
	}
    }
  else
    {
      error_code =
	file_extdata_apply_funcs (thread_p, extdata_in, NULL, NULL, file_extdata_item_func_for_search, &search_context,
				  for_write, extdata, page_extdata);
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
   *       then use them to append a new page and a new extenible data component.
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

  file_log ("file_rv_extdata_set_next", "page %d|%d, lsa %lld|%d, changed extdata link \n"
	    FILE_EXTDATA_MSG ("extdata after"), PGBUF_PAGE_VPID_AS_ARGS (page_ftab), PGBUF_PAGE_LSA_AS_ARGS (page_ftab),
	    FILE_EXTDATA_AS_ARGS (extdata));

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

  file_log ("file_rv_extdata_add", "add %d entries at position %d in page %d|%d, lsa %lld|%d \n"
	    FILE_EXTDATA_MSG ("extdata after"), count, pos, PGBUF_PAGE_VPID_AS_ARGS (page_ftab),
	    PGBUF_PAGE_LSA_AS_ARGS (page_ftab), FILE_EXTDATA_AS_ARGS (extdata));

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

  file_log ("file_rv_extdata_remove", "remove %d entries at position %d in page %d|%d, lsa %lld|%d"
	    FILE_EXTDATA_MSG ("extdata after"), count, pos, PGBUF_PAGE_VPID_AS_ARGS (page_ftab),
	    PGBUF_PAGE_LSA_AS_ARGS (page_ftab), FILE_EXTDATA_AS_ARGS (extdata));

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

  fprintf (fp, "Remove from extensible data at position = %d, count = %d.", pos, count);
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
file_log_extdata_add (THREAD_ENTRY * thread_p, const FILE_EXTENSIBLE_DATA * extdata, PAGE_PTR page, int position,
		      int count, const void *data)
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
  crumbs[2].length = extdata->size_of_item;

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
file_log_extdata_remove (THREAD_ENTRY * thread_p, const FILE_EXTENSIBLE_DATA * extdata, PAGE_PTR page, int position,
			 int count)
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
  crumbs[2].length = extdata->size_of_item;

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
file_log_extdata_set_next (THREAD_ENTRY * thread_p, const FILE_EXTENSIBLE_DATA * extdata, PAGE_PTR page,
			   VPID * vpid_next)
{
  LOG_DATA_ADDR addr = LOG_DATA_ADDR_INITIALIZER;
  LOG_LSA save_lsa;

  addr.pgptr = page;
  addr.offset = (PGLENGTH) (((char *) extdata) - page);
  save_lsa = *pgbuf_get_lsa (page);
  log_append_undoredo_data (thread_p, RVFL_EXTDATA_SET_NEXT, &addr, sizeof (VPID), sizeof (VPID), &extdata->vpid_next,
			    vpid_next);

  file_log ("file_log_extdata_set_next", "page %d|%d, prev_lsa %lld|%d, crt_lsa %lld|%d, "
	    "change extdata link to %d|%d, \n" FILE_EXTDATA_MSG ("extdata before"),
	    PGBUF_PAGE_VPID_AS_ARGS (page), LSA_AS_ARGS (&save_lsa), PGBUF_PAGE_LSA_AS_ARGS (page),
	    VPID_AS_ARGS (vpid_next), FILE_EXTDATA_AS_ARGS (extdata));

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

  file_log ("file_rv_extdata_merge_undo", "page %d|%d lsa %lld|%d" FILE_EXTDATA_MSG ("extdata after"),
	    PGBUF_PAGE_VPID_AS_ARGS (rcv->pgptr), PGBUF_PAGE_LSA_AS_ARGS (rcv->pgptr),
	    FILE_EXTDATA_AS_ARGS (extdata_in_page));

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

  file_log ("file_rv_extdata_merge_redo_internal", "page %d|%d, lsa %lld|%d, %s merge \n"
	    FILE_EXTDATA_MSG ("extdata after"), PGBUF_PAGE_VPID_AS_ARGS (rcv->pgptr),
	    PGBUF_PAGE_LSA_AS_ARGS (rcv->pgptr), compare_func ? "ordered" : "unordered",
	    FILE_EXTDATA_AS_ARGS (extdata_in_page));

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
file_log_extdata_merge (THREAD_ENTRY * thread_p, const FILE_EXTENSIBLE_DATA * extdata_dest, PAGE_PTR page_dest,
			const FILE_EXTENSIBLE_DATA * extdata_src, LOG_RCVINDEX rcvindex)
{
  LOG_DATA_ADDR addr = LOG_DATA_ADDR_INITIALIZER;

  addr.pgptr = page_dest;
  addr.offset = (PGLENGTH) (((char *) extdata_dest) - page_dest);

  log_append_undoredo_data (thread_p, rcvindex, &addr, file_extdata_size (extdata_dest),
			    file_extdata_size (extdata_src), extdata_dest, extdata_src);
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

  file_log ("file_rv_partsect_update", "recovery partial sector update in page %d|%d prev_lsa %lld|%d: "
	    "%s bit at offset %d, partial sector offset %d \n" FILE_PARTSECT_MSG ("partsect after rcv"),
	    PGBUF_PAGE_VPID_AS_ARGS (rcv->pgptr), PGBUF_PAGE_LSA_AS_ARGS (rcv->pgptr), set ? "set" : "clear",
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

/************************************************************************/
/* File manipulation section.                                           */
/************************************************************************/

/*
 * flre_create_with_npages () - Create a permanent file big enough to store a number of pages.
 *
 * return	  : Error code
 * thread_p (in)  : Thread entry
 * file_type (in) : File type
 * npages (in)	  : Number of pages.
 * des (in)	  : File descriptor.
 * vfid (out)	  : File identifier.
 */
int
flre_create_with_npages (THREAD_ENTRY * thread_p, FILE_TYPE file_type, int npages, FILE_DESCRIPTORS * des, VFID * vfid)
{
  FILE_TABLESPACE tablespace;

  assert (file_type != FILE_TEMP);

  assert (npages > 0);

  FILE_TABLESPACE_FOR_PERM_NPAGES (&tablespace, npages);

  return flre_create (thread_p, file_type, &tablespace, des, false, false, vfid);
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
flre_create_heap (THREAD_ENTRY * thread_p, FILE_HEAP_DES * des_heap, bool reuse_oid, VFID * vfid)
{
  FILE_TYPE file_type = reuse_oid ? FILE_HEAP_REUSE_SLOTS : FILE_HEAP;

  assert (des_heap != NULL);

  return flre_create_with_npages (thread_p, file_type, 1, (FILE_DESCRIPTORS *) des_heap, vfid);
}

/*
 * flre_create_temp_internal () - common function to create files for temporary purpose. always try to use a cached
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
flre_create_temp_internal (THREAD_ENTRY * thread_p, int npages, FILE_TYPE ftype, bool is_numerable, VFID * vfid_out)
{
  FILE_TABLESPACE tablespace;
  FLRE_TEMPCACHE_ENTRY *tempcache_entry = NULL;

  int error_code = NO_ERROR;

  assert (npages > 0);

  error_code = flre_tempcache_get (thread_p, ftype, is_numerable, &tempcache_entry);
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
      error_code = flre_create (thread_p, ftype, &tablespace, NULL, true, is_numerable, vfid_out);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  flre_tempcache_retire_entry (tempcache_entry);
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
  flre_tempcache_push_tran_file (thread_p, tempcache_entry);
  return NO_ERROR;
}

/*
 * flre_create_temp () - Create a temporary file.
 *
 * return	 : Error code
 * thread_p (in) : Thread entry
 * npages (in)	 : Number of pages
 * vfid (out)	 : File identifier
 */
int
flre_create_temp (THREAD_ENTRY * thread_p, int npages, VFID * vfid)
{
  return flre_create_temp_internal (thread_p, npages, FILE_TEMP, false, vfid);
}

/*
 * flre_create_temp_numerable () - Create a temporary file with numerable property.
 *
 * return	 : Error code
 * thread_p (in) : Thread entry
 * npages (in)	 : Number of pages
 * vfid (out)	 : File identifier
 */
int
flre_create_temp_numerable (THREAD_ENTRY * thread_p, int npages, VFID * vfid)
{
  return flre_create_temp_internal (thread_p, npages, FILE_TEMP, true, vfid);
}

/*
 * flre_create_query_area () - Create a query area file (temporary, not numerable).
 *
 * return	 : Error code
 * thread_p (in) : Thread entry
 * vfid (out)	 : File identifier
 */
int
flre_create_query_area (THREAD_ENTRY * thread_p, VFID * vfid)
{
  return flre_create_temp_internal (thread_p, 1, FILE_QUERY_AREA, false, vfid);
}

/*
 * flre_create_ehash () - Create a permanent or temporary file for extensible hash table. This file will have the
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
flre_create_ehash (THREAD_ENTRY * thread_p, int npages, bool is_tmp, FILE_EHASH_DES * des_ehash, VFID * vfid)
{
  FILE_TABLESPACE tablespace;

  assert (npages > 0);

  /* todo: use temporary file cache? */

  FILE_TABLESPACE_FOR_TEMP_NPAGES (&tablespace, npages);
  return flre_create (thread_p, FILE_EXTENDIBLE_HASH, &tablespace, (FILE_DESCRIPTORS *) des_ehash, is_tmp, true, vfid);
}


/*
 * flre_create_ehash_dir () - Create a permanent or temporary file for extensible hash directory. This file will have
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
flre_create_ehash_dir (THREAD_ENTRY * thread_p, int npages, bool is_tmp, FILE_EHASH_DES * des_ehash, VFID * vfid)
{
  FILE_TABLESPACE tablespace;

  assert (npages > 0);

  /* todo: use temporary file cache? */

  FILE_TABLESPACE_FOR_TEMP_NPAGES (&tablespace, npages);
  return flre_create (thread_p, FILE_EXTENDIBLE_HASH_DIRECTORY, &tablespace, (FILE_DESCRIPTORS *) des_ehash, is_tmp,
		      true, vfid);
}

/* TODO: Rename as file_create. */
/*
 * flre_create () - Create a new file.
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
flre_create (THREAD_ENTRY * thread_p, FILE_TYPE file_type, FILE_TABLESPACE * tablespace, FILE_DESCRIPTORS * des,
	     bool is_temp, bool is_numerable, VFID * vfid)
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
  FLRE_HEADER *fhead = NULL;

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

  file_log ("file_create", "create %s file \n\t%s \n\t%s \n" FILE_TABLESPACE_MSG " \tnsectors = %d",
	    file_type_to_string (file_type), FILE_PERM_TEMP_STRING (is_temp),
	    FILE_NUMERABLE_REGULAR_STRING (is_numerable), FILE_TABLESPACE_AS_ARGS (tablespace), n_sectors);

  error_code =
    disk_reserve_sectors (thread_p, volpurpose, DISK_NONCONTIGUOUS_SPANVOLS_PAGES, NULL_VOLID, n_sectors,
			  vsids_reserved);
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
	   vsid_iter < vsids_reserved + n_sectors && VFID_ISNULL (&found_vfid) && vsid_iter->volid == first_volid;
	   vsid_iter++)
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
#else /* SA_MODE */
  {
    vfid->volid = vsids_reserved->volid;
    vfid->fileid = SECTOR_FIRST_PAGEID (vsids_reserved->sectid);
  }
#endif /* SA_MODE */
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
  fhead = (FLRE_HEADER *) page_fhead;

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
	  extdata_part_ftab->vpid_next = vpid_fhead;
	  if (page_ftab != NULL)
	    {
	      if (do_logging)
		{
		  log_append_redo_page (thread_p, page_ftab, file_extdata_size (extdata_part_ftab), PAGE_FTAB);
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
	  log_append_redo_page (thread_p, page_ftab, file_extdata_size (extdata_part_ftab), PAGE_FTAB);
	}
      pgbuf_unfix_and_init (thread_p, page_ftab);
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
      log_append_redo_page (thread_p, page_fhead, DB_PAGESIZE, PAGE_FTAB);
    }
  pgbuf_set_dirty (thread_p, page_fhead, DONT_FREE);

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
	  log_sysop_end_logical_undo (thread_p, RVFL_DESTROY, sizeof (*vfid), (char *) vfid);
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
 * flre_destroy () - Destroy file - unreserve all sectors used by file on disk.
 *
 * return	 : Error code
 * thread_p (in) : Thread entry
 * vfid (in)	 : File identifier
 */
int
flre_destroy (THREAD_ENTRY * thread_p, const VFID * vfid)
{
  VPID vpid_fhead;
  PAGE_PTR page_fhead;
  FLRE_HEADER *fhead = NULL;
  VSID *vsids = NULL;
  FILE_VSID_COLLECTOR vsid_collector;
  FILE_EXTENSIBLE_DATA *extdata_part_ftab;
  FILE_EXTENSIBLE_DATA *extdata_full_ftab;

  int error_code = NO_ERROR;

  assert (vfid != NULL && !VFID_ISNULL (vfid));

  FILE_GET_HEADER_VPID (vfid, &vpid_fhead);
  page_fhead = pgbuf_fix (thread_p, &vpid_fhead, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (page_fhead == NULL)
    {
      assert_release (false);
      error_code = ER_FAILED;
      goto exit;
    }

  fhead = (FLRE_HEADER *) page_fhead;
  file_header_sanity_check (fhead);

  assert (FILE_IS_TEMPORARY (fhead) || log_check_system_op_is_started (thread_p));

  /* Allocate enough space to collect all sectors. */
  vsids = (VSID *) db_private_alloc (thread_p, fhead->n_sector_total * sizeof (VSID));
  if (vsids == NULL)
    {
      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 1, fhead->n_sector_total * sizeof (VSID));
      goto exit;
    }
  vsid_collector.vsids = vsids;
  vsid_collector.n_vsids = 0;

  /* Collect from partial table */
  FILE_HEADER_GET_PART_FTAB (fhead, extdata_part_ftab);
  error_code =
    file_extdata_apply_funcs (thread_p, extdata_part_ftab, NULL, NULL, file_table_collect_vsid, &vsid_collector, false,
			      NULL, NULL);
  if (error_code != NO_ERROR)
    {
      assert_release (false);
      goto exit;
    }

  if (!FILE_IS_TEMPORARY (fhead))
    {
      /* Collect from full table. */
      FILE_HEADER_GET_FULL_FTAB (fhead, extdata_full_ftab);
      error_code =
	file_extdata_apply_funcs (thread_p, extdata_full_ftab, NULL, NULL, file_table_collect_vsid, &vsid_collector,
				  false, NULL, NULL);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto exit;
	}
    }

  if (vsid_collector.n_vsids != fhead->n_sector_total)
    {
      assert_release (false);
      error_code = ER_FAILED;
      goto exit;
    }
  qsort (vsids, fhead->n_sector_total, sizeof (VSID), file_compare_vsids);

  file_log ("flre_destroy", "file %d|%d unreserve %d sectors \n" FILE_HEAD_FULL_MSG,
	    VFID_AS_ARGS (vfid), fhead->n_sector_total, FILE_HEAD_FULL_AS_ARGS (fhead));

  pgbuf_unfix_and_init (thread_p, page_fhead);

  error_code =
    disk_unreserve_ordered_sectors (thread_p,
				    FILE_IS_TEMPORARY (fhead) ? DB_TEMPORARY_DATA_PURPOSE : DB_PERMANENT_DATA_PURPOSE,
				    vsid_collector.n_vsids, vsids);
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
  if (vsids != NULL)
    {
      db_private_free (thread_p, vsids);
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

  error_code = flre_destroy (thread_p, vfid);
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
flre_postpone_destroy (THREAD_ENTRY * thread_p, const VFID * vfid)
{
  LOG_DATA_ADDR addr = LOG_DATA_ADDR_INITIALIZER;

#if !defined (NDEBUG)
  /* This should be used for permanent files! */
  {
    VPID vpid_fhead;
    PAGE_PTR page_fhead;
    FLRE_HEADER *fhead;

    vpid_fhead.volid = vfid->volid;
    vpid_fhead.pageid = vfid->fileid;
    page_fhead = pgbuf_fix (thread_p, &vpid_fhead, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
    if (page_fhead == NULL)
      {
	ASSERT_ERROR ();
	return;
      }
    fhead = (FLRE_HEADER *) page_fhead;
    assert (!FILE_IS_TEMPORARY (fhead));
    pgbuf_unfix_and_init (thread_p, page_fhead);
  }
#endif /* !NDEBUG */

  log_append_postpone (thread_p, RVFL_DESTROY, &addr, sizeof (*vfid), vfid);
}

/*
 * flre_temp_retire () - retire temporary file. put it in cache is possible or destroy the file.
 *
 * return        : error code
 * thread_p (in) : thread entry
 * vfid (in)     : file identifier
 */
int
flre_temp_retire (THREAD_ENTRY * thread_p, const VFID * vfid)
{
  FLRE_TEMPCACHE_ENTRY *entry = flre_tempcache_pop_tran_file (thread_p, vfid);
  bool save_interrupt;
  int error_code = NO_ERROR;

  if (entry == NULL)
    {
      assert_release (false);
    }
  else if (flre_tempcache_put (thread_p, entry))
    {
      /* cached */
      return NO_ERROR;
    }
  /* was not cached. destroy */
  /* don't allow interrupt to avoid file leak */
  save_interrupt = thread_set_check_interrupt (thread_p, false);
  error_code = flre_destroy (thread_p, vfid);
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
  FLRE_HEADER *fhead;
  FILE_EXTENSIBLE_DATA *part_table;

  DKNSECTS save_nsects;

  fhead = (FLRE_HEADER *) page_fhead;
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

  file_log ("file_rv_perm_expand_undo", "removed expanded sectors from partial table and file header in file %d|%d, "
	    "page header %d|%d, lsa %lld|%d, number of sectors %d \n" FILE_HEAD_ALLOC_MSG,
	    VFID_AS_ARGS (&fhead->self), PGBUF_PAGE_VPID_AS_ARGS (page_fhead), PGBUF_PAGE_LSA_AS_ARGS (page_fhead),
	    save_nsects, FILE_HEAD_ALLOC_AS_ARGS (fhead));

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
  FLRE_HEADER *fhead;
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

  fhead = (FLRE_HEADER *) page_fhead;
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

  file_log ("file_rv_perm_expand_redo", "recovery expand in file %d|%d, file header %d|%d, lsa %lld|%d \n"
	    FILE_HEAD_ALLOC_MSG FILE_EXTDATA_MSG ("partial table after"),
	    VFID_AS_ARGS (&fhead->self), PGBUF_PAGE_VPID_AS_ARGS (page_fhead), PGBUF_PAGE_LSA_AS_ARGS (page_fhead),
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
  FLRE_HEADER *fhead = NULL;
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

  fhead = (FLRE_HEADER *) page_fhead;
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

  file_log ("file_perm_expand", "expand file %d|%d by %d sectors. \n" FILE_HEAD_ALLOC_MSG FILE_TABLESPACE_MSG,
	    VFID_AS_ARGS (&fhead->self), expand_size_in_sectors, FILE_HEAD_ALLOC_AS_ARGS (fhead),
	    FILE_TABLESPACE_AS_ARGS (&fhead->tablespace));

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
    disk_reserve_sectors (thread_p, DB_PERMANENT_DATA_PURPOSE, DISK_NONCONTIGUOUS_SPANVOLS_PAGES,
			  fhead->volid_last_expand, expand_size_in_sectors, vsids_reserved);
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
  log_append_undoredo_data2 (thread_p, RVFL_EXPAND, NULL, page_fhead, 0, 0, expand_size_in_sectors * sizeof (VSID),
			     NULL, vsids_reserved);

  file_log ("file_perm_expand", "expand file %d|%d, page header %d|%d, prev_lsa %lld|%d, crt_lsa %lld|%d; "
	    "first sector %d|%d \n" FILE_HEAD_ALLOC_MSG FILE_EXTDATA_MSG ("partial table"),
	    VFID_AS_ARGS (&fhead->self), PGBUF_PAGE_VPID_AS_ARGS (page_fhead), LSA_AS_ARGS (&save_lsa),
	    PGBUF_PAGE_LSA_AS_ARGS (page_fhead), VSID_AS_ARGS (vsids_reserved), FILE_HEAD_ALLOC_AS_ARGS (fhead),
	    FILE_EXTDATA_AS_ARGS (extdata_part_ftab));

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
  FLRE_HEADER *fhead = NULL;
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

  fhead = (FLRE_HEADER *) page_fhead;
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

  file_log ("file_table_move_partial_sectors_to_header", "file %d|%d \n" FILE_EXTDATA_MSG ("header (destination)")
	    FILE_EXTDATA_MSG ("first page (source)"), VFID_AS_ARGS (&fhead->self),
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
  file_extdata_append_array (extdata_part_ftab_head, file_extdata_start (extdata_part_ftab_first),
			     (INT16) n_items_to_move);
  save_lsa = *pgbuf_get_lsa (page_fhead);
  /* log changes to extensible data in header page */
  file_log_extdata_add (thread_p, extdata_part_ftab_head, page_fhead, 0, n_items_to_move,
			file_extdata_start (extdata_part_ftab_first));

  file_log ("file_table_move_partial_sectors_to_header", "moved %d items from first page to header page file table. \n"
	    "file %d|%d, header page %d|%d, prev_lsa %lld|%d, crt_lsa %lld|%d \n"
	    FILE_EXTDATA_MSG ("header partial table"), n_items_to_move, VFID_AS_ARGS (&fhead->self),
	    PGBUF_PAGE_VPID_AS_ARGS (page_fhead), LSA_AS_ARGS (&save_lsa), PGBUF_PAGE_LSA_AS_ARGS (page_fhead),
	    FILE_EXTDATA_AS_ARGS (extdata_part_ftab_head));

  /* now remove from first page. if all items have been moved, we can deallocate first page. */
  if (n_items_to_move < file_extdata_item_count (extdata_part_ftab_first))
    {
      /* Remove copied entries. */
      save_lsa = *pgbuf_get_lsa (page_part_ftab_first);
      file_extdata_remove_at (extdata_part_ftab_first, 0, n_items_to_move);
      file_log_extdata_remove (thread_p, extdata_part_ftab_first, page_part_ftab_first, 0, n_items_to_move);

      file_log ("file_table_move_partial_sectors_to_header", "removed %d items from first page partial table \n"
		"file %d|%d, page %d|%d, prev_lsa %lld|%d, crt_lsa %lld|%d \n"
		FILE_EXTDATA_MSG ("first page partial table"), n_items_to_move, VFID_AS_ARGS (&fhead->self),
		PGBUF_PAGE_VPID_AS_ARGS (page_part_ftab_first), LSA_AS_ARGS (&save_lsa),
		PGBUF_PAGE_LSA_AS_ARGS (page_part_ftab_first), FILE_EXTDATA_AS_ARGS (extdata_part_ftab_first));
    }
  else
    {
      /* deallocate the first partial table page. */
      VPID save_next = extdata_part_ftab_head->vpid_next;
      file_log_extdata_set_next (thread_p, extdata_part_ftab_head, page_fhead, &extdata_part_ftab_first->vpid_next);
      VPID_COPY (&extdata_part_ftab_head->vpid_next, &extdata_part_ftab_first->vpid_next);

      file_log ("file_table_move_partial_sectors_to_header", "remove first partial table page %d|%d\n",
		VPID_AS_ARGS (&save_next));

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
  FLRE_HEADER *fhead = NULL;
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

  fhead = (FLRE_HEADER *) page_fhead;
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

      file_log ("file_table_add_full_sector", "add sector %d|%d at position %d in file %d|%d, full table page %d|%d, "
		"prev_lsa %lld|%d, crt_lsa %lld|%d, \n" FILE_EXTDATA_MSG ("full table component"),
		VSID_AS_ARGS (vsid), pos, VFID_AS_ARGS (&fhead->self), PGBUF_PAGE_VPID_AS_ARGS (page_extdata),
		LSA_AS_ARGS (&save_lsa), PGBUF_PAGE_LSA_AS_ARGS (page_extdata),
		FILE_EXTDATA_AS_ARGS (extdata_full_ftab));
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

      file_log ("file_table_add_full_sector", "added sector %d|%d in new full table page %d|%d \n"
		FILE_EXTDATA_MSG ("new extensible data component"), VSID_AS_ARGS (vsid), VPID_AS_ARGS (&vpid_ftab_new),
		FILE_EXTDATA_AS_ARGS (extdata_full_ftab));

      log_append_redo_page (thread_p, page_ftab, file_extdata_size (extdata_full_ftab), PAGE_FTAB);
      pgbuf_set_dirty_and_free (thread_p, page_ftab);
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
  FLRE_HEADER *fhead = (FLRE_HEADER *) page_fhead;
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
			     (PGLENGTH) ((char *) partsect - page_fhead), sizeof (offset_to_alloc_bit),
			     sizeof (offset_to_alloc_bit), &offset_to_alloc_bit, &offset_to_alloc_bit);

  file_log ("file_perm_alloc", "allocated page %d|%d in file %d|%d page %d|%d, prev_lsa %lld|%d, crt_lsa %lld|%d, "
	    "set bit at offset %d in partial sector at offset %d \n" FILE_PARTSECT_MSG ("partsect after"),
	    VPID_AS_ARGS (vpid_alloc_out), VFID_AS_ARGS (&fhead->self), PGBUF_PAGE_VPID_AS_ARGS (page_fhead),
	    LSA_AS_ARGS (&save_lsa), PGBUF_PAGE_LSA_AS_ARGS (page_fhead), offset_to_alloc_bit,
	    (PGLENGTH) ((char *) partsect - page_fhead), FILE_PARTSECT_AS_ARGS (partsect));

  is_full = file_partsect_is_full (partsect);

  /* update header statistics */
  file_header_alloc (fhead, alloc_type, was_empty, is_full);
  save_lsa = *pgbuf_get_lsa (page_fhead);
  file_log_fhead_alloc (thread_p, page_fhead, alloc_type, was_empty, is_full);

  file_log ("file_perm_alloc", "update header in file %d|%d, header page %d|%d, prev_lsa %lld|%d, crt_lsa %lld|%d, "
	    "after %s, was_empty = %s, is_full = %s, \n" FILE_HEAD_ALLOC_MSG,
	    VFID_AS_ARGS (&fhead->self), PGBUF_PAGE_VPID_AS_ARGS (page_fhead), LSA_AS_ARGS (&save_lsa),
	    PGBUF_PAGE_LSA_AS_ARGS (page_fhead), FILE_ALLOC_TYPE_STRING (alloc_type),
	    was_empty ? "true" : "false", is_full ? "true" : "false", FILE_HEAD_ALLOC_AS_ARGS (fhead));

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

      file_log ("file_perm_alloc", "removed full partial sector from position 0 in file %d|%d, header page %d|%d, "
		"prev_lsa %lld|%d, crt_lsa %lld|%d, \n" FILE_EXTDATA_MSG ("partial table after alloc"),
		VFID_AS_ARGS (&fhead->self), PGBUF_PAGE_VPID_AS_ARGS (page_fhead), LSA_AS_ARGS (&save_lsa),
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
flre_alloc (THREAD_ENTRY * thread_p, const VFID * vfid, VPID * vpid_out)
{
#define UNDO_DATA_SIZE (sizeof (VFID) + sizeof (VPID))
  VPID vpid_fhead;
  PAGE_PTR page_fhead = NULL;
  FLRE_HEADER *fhead = NULL;

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

  fhead = (FLRE_HEADER *) page_fhead;
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
	  log_sysop_end_logical_undo (thread_p, RVFL_ALLOC, UNDO_DATA_SIZE, undo_log_data);
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
flre_alloc_and_init (THREAD_ENTRY * thread_p, const VFID * vfid, FILE_INIT_PAGE_FUNC f_init, void *f_init_args,
		     VPID * vpid_alloc)
{
  PAGE_PTR page_alloc = NULL;
  int error_code;

  error_code = flre_alloc (thread_p, vfid, vpid_alloc);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  page_alloc = pgbuf_fix (thread_p, vpid_alloc, NEW_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (page_alloc == NULL)
    {
      ASSERT_ERROR_AND_SET (error_code);
      return error_code;
    }

  if (f_init)
    {
      error_code = f_init (thread_p, page_alloc, f_init_args);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	}
    }
  pgbuf_unfix (thread_p, page_alloc);

  return error_code;
}

/*
 * flre_alloc_multiple () - Allocate multiple pages at once.
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
flre_alloc_multiple (THREAD_ENTRY * thread_p, const VFID * vfid, FILE_INIT_PAGE_FUNC f_init, void *f_init_args,
		     int npages, VPID * vpids_out)
{
  VPID *vpid_iter;
  VPID local_vpid = VPID_INITIALIZER;
  int iter;

  VPID vpid_fhead;
  PAGE_PTR page_fhead = NULL;
  FLRE_HEADER *fhead = NULL;

  int error_code = NO_ERROR;

  assert (vfid != NULL && !VFID_ISNULL (vfid));
  assert (npages >= 1);

  assert (log_check_system_op_is_started (thread_p));

  /* fix header */
  FILE_GET_HEADER_VPID (vfid, &vpid_fhead);
  page_fhead = pgbuf_fix (thread_p, &vpid_fhead, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (page_fhead == NULL)
    {
      ASSERT_ERROR_AND_SET (error_code);
      return error_code;
    }
  fhead = (FLRE_HEADER *) page_fhead;
  file_header_sanity_check (fhead);
  /* keep header while allocating all pages. we have a great chance to allocate all pages in the same sectors */

  /* start a system op. we may abort page allocations if an error occurs. */
  log_sysop_start (thread_p);

  /* do not leak pages! if not numerable, it should use all allocated VPIDS */
  assert (FILE_IS_NUMERABLE (fhead) || vpids_out != NULL);

  for (iter = 0; iter < npages; iter++)
    {
      vpid_iter = vpids_out ? vpids_out + iter : &local_vpid;
      error_code = flre_alloc_and_init (thread_p, vfid, f_init, f_init_args, vpid_iter);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto exit;
	}
    }
  /* done */
  assert (error_code == NO_ERROR);

exit:

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

  if (page_fhead != NULL)
    {
      pgbuf_unfix (thread_p, page_fhead);
    }
  return error_code;
}

/*
 * flre_alloc_sticky_first_page () - Allocate first file page and make it sticky. It is usually used as special headers
 *                                   and should never be deallocated.
 *
 * return         : Error code
 * thread_p (in)  : Thread entry
 * vfid (in)      : File identifier
 * vpid_out (out) : Allocated page VPID
 */
int
flre_alloc_sticky_first_page (THREAD_ENTRY * thread_p, const VFID * vfid, VPID * vpid_out)
{
  VPID vpid_fhead;
  PAGE_PTR page_fhead = NULL;
  FLRE_HEADER *fhead = NULL;

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

  fhead = (FLRE_HEADER *) page_fhead;
  file_header_sanity_check (fhead);

  assert (fhead->n_page_user == 0);
  assert (VPID_ISNULL (&fhead->vpid_sticky_first));

  error_code = flre_alloc (thread_p, vfid, vpid_out);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto exit;
    }

  /* save VPID */
  save_lsa = *pgbuf_get_lsa (page_fhead);
  log_append_undoredo_data2 (thread_p, RVFL_FHEAD_STICKY_PAGE, NULL, page_fhead, 0, sizeof (VPID), sizeof (VPID),
			     &fhead->vpid_sticky_first, vpid_out);
  fhead->vpid_sticky_first = *vpid_out;
  pgbuf_set_dirty (thread_p, page_fhead, DONT_FREE);

  /* done */
  file_header_sanity_check (fhead);
  assert (error_code == NO_ERROR);

  file_log ("flre_alloc_sticky_first_page", "set vpid_sticky_first to %d|%d in file %d|%d, header page %d|%d, "
	    "prev_lsa %lld|%d, crt_lsa %lld|%d", VPID_AS_ARGS (vpid_out), VFID_AS_ARGS (vfid),
	    PGBUF_PAGE_VPID_AS_ARGS (page_fhead), LSA_AS_ARGS (&save_lsa), PGBUF_PAGE_LSA_AS_ARGS (page_fhead));

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
  FLRE_HEADER *fhead = (FLRE_HEADER *) page_fhead;
  VPID *vpid = (VPID *) rcv->data;

  assert (rcv->length == sizeof (*vpid));

  fhead->vpid_sticky_first = *vpid;
  pgbuf_set_dirty (thread_p, page_fhead, DONT_FREE);

  file_log ("file_rv_fhead_sticky_page", "set vpid_sticky_first to %d|%d in file %d|%d, header page %d|%d, lsa %lld|%d",
	    VPID_AS_ARGS (vpid), VFID_AS_ARGS (&fhead->self), PGBUF_PAGE_VPID_AS_ARGS (page_fhead),
	    PGBUF_PAGE_LSA_AS_ARGS (page_fhead));
  return NO_ERROR;
}

/*
 * flre_get_sticky_first_page () - Get VPID of first page. It should be a sticky page.
 *
 * return         : Error code
 * thread_p (in)  : Thread entry
 * vfid (in)      : File identifier
 * vpid_out (out) : VPID of sticky first page
 */
int
flre_get_sticky_first_page (THREAD_ENTRY * thread_p, const VFID * vfid, VPID * vpid_out)
{
  VPID vpid_fhead;
  PAGE_PTR page_fhead = NULL;
  FLRE_HEADER *fhead = NULL;

  int error_code = NO_ERROR;

  /* fix header */
  FILE_GET_HEADER_VPID (vfid, &vpid_fhead);
  page_fhead = pgbuf_fix (thread_p, &vpid_fhead, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (page_fhead == NULL)
    {
      ASSERT_ERROR_AND_SET (error_code);
      return error_code;
    }

  fhead = (FLRE_HEADER *) page_fhead;
  if (LOG_ISRESTARTED ())
    {
      /* sometimes called before recovery... we cannot guarantee the header is sane at this point. */
      file_header_sanity_check (fhead);
    }

  *vpid_out = fhead->vpid_sticky_first;
  if (VPID_ISNULL (vpid_out))
    {
      assert_release (false);
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
flre_dealloc (THREAD_ENTRY * thread_p, const VFID * vfid, const VPID * vpid, FILE_TYPE file_type_hint)
{
#define LOG_DATA_SIZE (sizeof (VFID) + sizeof (VPID))
  VPID vpid_fhead;
  PAGE_PTR page_fhead = NULL;
  FLRE_HEADER *fhead = NULL;
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
  if (file_type_hint == FILE_UNKNOWN_TYPE || file_type_hint == FILE_EXTENDIBLE_HASH
      || file_type_hint == FILE_EXTENDIBLE_HASH_DIRECTORY)
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
      fhead = (FLRE_HEADER *) page_fhead;
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

      file_log ("file_dealloc", "file %s %d|%d dealloc vpid %|%d postponed", file_type_to_string (file_type_hint),
		VFID_AS_ARGS (vfid), VPID_AS_ARGS (vpid));
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
      fhead = (FLRE_HEADER *) page_fhead;
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
  error_code =
    file_extdata_search_item (thread_p, &extdata_user_page_ftab, vpid, file_compare_vpids, false, true, &found, &pos,
			      &page_ftab);
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

  file_log ("file_dealloc", "marked page %d|%d as deleted in file %d|%d, page %d|%d, prev_lsa %lld|%d, "
	    "crt_lsa %lld_%d, at offset %d ", VPID_AS_ARGS (vpid_found), VFID_AS_ARGS (&fhead->self),
	    PGBUF_PAGE_VPID_AS_ARGS (page_extdata), LSA_AS_ARGS (&save_lsa), PGBUF_PAGE_LSA_AS_ARGS (page_extdata),
	    (PGLENGTH) (((char *) vpid_found) - page_extdata));

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
 * file_table_try_extdata_merge () - Try to merge extensible data components in file tables.
 *
 * return	     : Error code
 * thread_p (in)     : Thread entry
 * page_fhead (in)   : File header page
 * page_dest (in)    : Merge destination page
 * extdata_dest (in) : Merge destination extensible 
 * compare_func (in) : NULL to merge unordered, not NULL to merge ordered
 * rcvindex (in)     : Recovery index used in case merge is executed
 */
STATIC_INLINE int
file_table_try_extdata_merge (THREAD_ENTRY * thread_p, PAGE_PTR page_fhead, PAGE_PTR page_dest,
			      FILE_EXTENSIBLE_DATA * extdata_dest, int (*compare_func) (const void *, const void *),
			      LOG_RCVINDEX rcvindex)
{
  FILE_EXTENSIBLE_DATA *extdata_next = NULL;
  VPID vpid_next = extdata_dest->vpid_next;
  PAGE_PTR page_next = NULL;

  int error_code = NO_ERROR;

  /* how it works:
   *
   * we try to merge next extensible component in the extensible component given as argument.
   * to do the merge, next extensible component should fit current extensible component.
   *
   * note: there can be two types of merges: ordered or unordered. for ordered merge, a compare_func must be provided
   * note: make sure the right recovery index is used. the same merge type and compare functions should be used at
   *       recovery
   */

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

  file_log ("file_table_try_extdata_merge", "\n" FILE_EXTDATA_MSG ("dest") FILE_EXTDATA_MSG ("src"),
	    FILE_EXTDATA_AS_ARGS (extdata_dest), FILE_EXTDATA_AS_ARGS (extdata_next));

  /* can extensible data components be merged? */
  if (file_extdata_can_merge (extdata_next, extdata_dest))
    {
      /* do the merge */
      file_log ("file_table_try_extdata_merge", "page %d|%d prev_lsa %lld|%d \n" FILE_EXTDATA_MSG ("dest before merge"),
		PGBUF_PAGE_VPID_AS_ARGS (page_dest), PGBUF_PAGE_LSA_AS_ARGS (page_dest),
		FILE_EXTDATA_AS_ARGS (extdata_dest));

      file_log_extdata_merge (thread_p, extdata_dest, page_dest, extdata_next, rcvindex);
      if (compare_func)
	{
	  file_extdata_merge_ordered (extdata_next, extdata_dest, compare_func);
	}
      else
	{
	  file_extdata_merge_unordered (extdata_next, extdata_dest);
	}

      file_log ("file_table_try_extdata_merge", "page %d|%d crt_lsa %lld|%d \n" FILE_EXTDATA_MSG ("dest after merge"),
		PGBUF_PAGE_VPID_AS_ARGS (page_dest), PGBUF_PAGE_LSA_AS_ARGS (page_dest),
		FILE_EXTDATA_AS_ARGS (extdata_dest));

      file_log_extdata_set_next (thread_p, extdata_dest, page_dest, &extdata_next->vpid_next);
      VPID_COPY (&extdata_dest->vpid_next, &extdata_next->vpid_next);

      pgbuf_unfix_and_init (thread_p, page_next);

      /* deallocate merged page */
      error_code = file_perm_dealloc (thread_p, page_fhead, &vpid_next, FILE_ALLOC_TABLE_PAGE);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}

      file_log ("file_table_try_extdata_merge", "merged \n" FILE_EXTDATA_MSG ("dest"),
		FILE_EXTDATA_AS_ARGS (extdata_dest));
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
  FLRE_HEADER *fhead = NULL;
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

  fhead = (FLRE_HEADER *) page_fhead;
  assert (!FILE_IS_TEMPORARY (fhead));
  assert (!VPID_EQ (&fhead->vpid_sticky_first, vpid_dealloc));

  /* find page sector in one of the partial or full tables */
  vsid_dealloc.volid = vpid_dealloc->volid;
  vsid_dealloc.sectid = SECTOR_FROM_PAGEID (vpid_dealloc->pageid);

  file_log ("file_perm_dealloc", "file %d|%d de%s %d|%d (search for VSID %d|%d) \n" FILE_HEAD_ALLOC_MSG,
	    VFID_AS_ARGS (&fhead->self), FILE_ALLOC_TYPE_STRING (alloc_type), VPID_AS_ARGS (vpid_dealloc),
	    VSID_AS_ARGS (&vsid_dealloc), FILE_HEAD_ALLOC_AS_ARGS (fhead));

  /* search partial table. */
  FILE_HEADER_GET_PART_FTAB (fhead, extdata_part_ftab);
  error_code =
    file_extdata_search_item (thread_p, &extdata_part_ftab, &vsid_dealloc, file_compare_vsids, true, true, &found,
			      &position, &page_ftab);
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

      log_append_undoredo_data (thread_p, RVFL_PARTSECT_DEALLOC, &addr, sizeof (offset_to_dealloc_bit),
				sizeof (offset_to_dealloc_bit), &offset_to_dealloc_bit, &offset_to_dealloc_bit);

      pgbuf_set_dirty (thread_p, addr.pgptr, DONT_FREE);

      file_log ("file_perm_dealloc", "dealloc page %d|%d in file %d|%d page %d|%d, prev_lsa %lld|%d, crt_lsa %lld|%d, "
		"clear bit at offset %d in partsect at offset %d \n" FILE_PARTSECT_MSG ("partsect after"),
		VPID_AS_ARGS (vpid_dealloc), VFID_AS_ARGS (&fhead->self), PGBUF_PAGE_VPID_AS_ARGS (addr.pgptr),
		LSA_AS_ARGS (&save_page_lsa), PGBUF_PAGE_LSA_AS_ARGS (addr.pgptr), offset_to_dealloc_bit,
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
      error_code =
	file_extdata_search_item (thread_p, &extdata_full_ftab, &vsid_dealloc, file_compare_vsids, true, true, &found,
				  &position, &page_ftab);
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

      file_log ("file_perm_dealloc", "removed vsid %d|%d from position %d in file %d|%d, page %d|%d, prev_lsa %lld|%d, "
		"crt_lsa %lld|%d, \n" FILE_EXTDATA_MSG ("full table component"), VSID_AS_ARGS (&vsid_dealloc),
		VFID_AS_ARGS (&fhead->self), PGBUF_PAGE_VPID_AS_ARGS (addr.pgptr), LSA_AS_ARGS (&save_page_lsa),
		PGBUF_PAGE_LSA_AS_ARGS (addr.pgptr), FILE_EXTDATA_AS_ARGS (extdata_full_ftab));

      /* check if full table pages can be merged. */
      /* todo: tweak the condition for trying merges. */
      if (file_extdata_size (extdata_full_ftab) * 2 < file_extdata_max_size (extdata_full_ftab))
	{
	  error_code =
	    file_table_try_extdata_merge (thread_p, page_fhead, addr.pgptr, extdata_full_ftab, file_compare_vsids,
					  RVFL_EXTDATA_MERGE_COMPARE_VSID);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      goto exit;
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

	  file_log ("file_perm_dealloc", "add new partsect at position %d in file %d|%d, page %d|%d, prev_lsa %lld|%d, "
		    "crt_lsa %lld|%d \n" FILE_PARTSECT_MSG ("new partial sector")
		    FILE_EXTDATA_MSG ("partial table component"), position, VFID_AS_ARGS (&fhead->self),
		    PGBUF_PAGE_VPID_AS_ARGS (addr.pgptr), LSA_AS_ARGS (&save_page_lsa),
		    PGBUF_PAGE_LSA_AS_ARGS (addr.pgptr), FILE_PARTSECT_AS_ARGS (&partsect_new),
		    FILE_EXTDATA_AS_ARGS (extdata_part_ftab));

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
	  log_append_redo_page (thread_p, page_ftab, file_extdata_size (extdata_part_ftab), PAGE_FTAB);

	  file_log ("file_perm_dealloc", "file %d|%d moved to new partial table page %d|%d \n"
		    FILE_PARTSECT_MSG ("new partial sector") FILE_EXTDATA_MSG ("partial table component"),
		    VFID_AS_ARGS (&fhead->self), VPID_AS_ARGS (&vpid_ftab_new), FILE_PARTSECT_AS_ARGS (&partsect_new),
		    FILE_EXTDATA_AS_ARGS (extdata_part_ftab));

	  pgbuf_unfix_and_init (thread_p, page_ftab);
	}
    }

  /* we should have freed any other file table pages */
  assert (page_ftab == NULL);

  /* almost done. We need to update header statistics */
  file_header_dealloc (fhead, alloc_type, is_empty, was_full);
  save_page_lsa = *pgbuf_get_lsa (page_fhead);
  file_log_fhead_dealloc (thread_p, page_fhead, alloc_type, is_empty, was_full);

  /* done */
  assert (error_code == NO_ERROR);

  file_log ("file_perm_dealloc", "update header in file %d|%d, header page %d|%d, prev_lsa %lld|%d, crt_lsa %lld|%d, "
	    "after de%s, is_empty = %s, was_full = %s, \n" FILE_HEAD_ALLOC_MSG,
	    VFID_AS_ARGS (&fhead->self), PGBUF_PAGE_VPID_AS_ARGS (page_fhead), LSA_AS_ARGS (&save_page_lsa),
	    PGBUF_PAGE_LSA_AS_ARGS (page_fhead), FILE_ALLOC_TYPE_STRING (alloc_type),
	    is_empty ? "true" : "false", was_full ? "true" : "false", FILE_HEAD_ALLOC_AS_ARGS (fhead));

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
  FLRE_HEADER *fhead = NULL;
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

  fhead = (FLRE_HEADER *) page_fhead;
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
      error_code =
	file_extdata_search_item (thread_p, &extdata_user_page_ftab, vpid_dealloc, file_compare_vpids, false, true,
				  &found, &position, &page_ftab);
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

      file_log ("file_rv_dealloc_internal", "remove deallocated page %d|%d in file %d|%d, page %d|%d, "
		"prev_lsa %lld|%d, crt_lsa %lld|%d, \n" FILE_EXTDATA_MSG ("user page table component"),
		VPID_AS_ARGS (vpid_dealloc), VFID_AS_ARGS (&fhead->self), PGBUF_PAGE_VPID_AS_ARGS (addr.pgptr),
		LSA_AS_ARGS (&save_lsa), PGBUF_PAGE_LSA_AS_ARGS (addr.pgptr),
		FILE_EXTDATA_AS_ARGS (extdata_user_page_ftab));

      /* should we merge pages? */
      /* todo: tweak the condition for trying merges */
      if (file_extdata_size (extdata_user_page_ftab) * 2 < file_extdata_max_size (extdata_user_page_ftab))
	{
	  error_code =
	    file_table_try_extdata_merge (thread_p, page_fhead, addr.pgptr, extdata_user_page_ftab, NULL,
					  RVFL_EXTDATA_MERGE);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      if (page_ftab != NULL)
		{
		  pgbuf_unfix (thread_p, page_ftab);
		}
	      goto exit;
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
flre_get_num_user_pages (THREAD_ENTRY * thread_p, const VFID * vfid, int *n_user_pages_out)
{
  VPID vpid_fhead;
  PAGE_PTR page_fhead;
  FLRE_HEADER *fhead;

  int error_code = NO_ERROR;

  FILE_GET_HEADER_VPID (vfid, &vpid_fhead);
  page_fhead = pgbuf_fix (thread_p, &vpid_fhead, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (page_fhead == NULL)
    {
      ASSERT_ERROR_AND_SET (error_code);
      return error_code;
    }

  fhead = (FLRE_HEADER *) page_fhead;
  file_header_sanity_check (fhead);

  *n_user_pages_out = fhead->n_page_user;
  pgbuf_unfix (thread_p, page_fhead);
  return NO_ERROR;
}

DISK_ISVALID
flre_check_vpid (THREAD_ENTRY * thread_p, const VFID * vfid, const VPID * vpid_lookup)
{
  VPID vpid_fhead;
  PAGE_PTR page_fhead;
  FLRE_HEADER *fhead;

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

  fhead = (FLRE_HEADER *) page_fhead;
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
      if (file_extdata_search_item (thread_p, &extdata_full_ftab, &vsid_lookup, file_compare_vsids, true, false, &found,
				    &pos, &page_ftab) != NO_ERROR)
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
  return DISK_ERROR;
}

/*
 * flre_get_type () - Get file type for VFID.
 *
 * return          : Error code
 * thread_p (in)   : Thread entry
 * vfid (in)       : File identifier
 * ftype_out (out) : Output file type
 */
int
flre_get_type (THREAD_ENTRY * thread_p, const VFID * vfid, FILE_TYPE * ftype_out)
{
  VPID vpid_fhead;
  PAGE_PTR page_fhead = NULL;
  FLRE_HEADER *fhead = NULL;

  assert (vfid != NULL && !VFID_ISNULL (vfid));
  assert (ftype_out != NULL);

  *ftype_out = file_type_cache_check (vfid);
  if (*ftype_out != FILE_UNKNOWN_TYPE)
    {
      return NO_ERROR;
    }

  /* read from file header */
  FILE_GET_HEADER_VPID (vfid, &vpid_fhead);
  page_fhead = pgbuf_fix (thread_p, &vpid_fhead, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (page_fhead == NULL)
    {
      int error_code = NO_ERROR;
      ASSERT_ERROR_AND_SET (error_code);
      return error_code;
    }

  fhead = (FLRE_HEADER *) page_fhead;
  file_header_sanity_check (fhead);

  *ftype_out = fhead->type;
  assert (*ftype_out != FILE_UNKNOWN_TYPE);

  pgbuf_unfix (thread_p, page_fhead);
  return NO_ERROR;
}

/*
 * file_table_collect_ftab_pages () - FILE_EXTDATA_FUNC to collect pages used for file table
 *
 * return        : NO_ERROR
 * thread_p (in) : thread entry
 * extdata (in)  : extensible data
 * stop (in)     : ignored
 * args (in/out) : FILE_FTAB_COLLECTOR * - file table page collector
 */
static int
file_table_collect_ftab_pages (THREAD_ENTRY * thread_p, const FILE_EXTENSIBLE_DATA * extdata, bool * stop, void *args)
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

	  file_log ("file_table_collect_ftab_pages", "add new vsid %d|%d, nsects = %d", VSID_AS_ARGS (&vsid_this),
		    collect->nsects);
	}
      file_partsect_set_bit (&collect->partsect_ftab[idx_sect],
			     file_partsect_pageid_to_offset (&collect->partsect_ftab[idx_sect],
							     extdata->vpid_next.pageid));

      file_log ("file_table_collect_ftab_pages", "collect ftab page %d|%d, \n" FILE_PARTSECT_MSG ("partsect"),
		VPID_AS_ARGS (&extdata->vpid_next), FILE_PARTSECT_AS_ARGS (&collect->partsect_ftab[idx_sect]));
      collect->npages++;
    }
  return NO_ERROR;
}

/************************************************************************/
/* Numerable files section.                                              */
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
  FLRE_HEADER *fhead = (FLRE_HEADER *) page_fhead;
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
      page_extdata = page_fhead;
      extdata_user_page_ftab = (FILE_EXTENSIBLE_DATA *) page_ftab;
    }

  file_log ("file_numerable_add_page", "file %d|%d add page %d|%d to user page table \n" FILE_HEAD_FULL_MSG
	    FILE_EXTDATA_MSG ("last user page table component"), VFID_AS_ARGS (&fhead->self), VPID_AS_ARGS (vpid),
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

      file_log ("file_numerable_add_page", "file %d|%d added new page %d|%d to user table; "
		"updated vpid_last_user_page_ftab in header page %d|%d, prev_lsa %lld|%d, crt_lsa %lld|%d ",
		VFID_AS_ARGS (&fhead->self), VPID_AS_ARGS (&vpid_ftab_new), PGBUF_PAGE_VPID_AS_ARGS (page_fhead),
		LSA_AS_ARGS (&save_lsa), PGBUF_PAGE_LSA_AS_ARGS (page_fhead));

      /* initialize new page table */
      extdata_user_page_ftab = (FILE_EXTENSIBLE_DATA *) page_ftab;
      file_extdata_init (sizeof (VPID), DB_PAGESIZE, extdata_user_page_ftab);

      if (!FILE_IS_TEMPORARY (fhead))
	{
	  /* log changes. */
	  log_append_redo_page (thread_p, page_ftab, file_extdata_size (extdata_user_page_ftab), PAGE_FTAB);
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

  file_log ("file_numerable_add_page", "add page %d|%d to position %d in file %d|%d, page %d|%d, prev_lsa = %lld|%d, "
	    "crt_lsa = %lld|%d \n" FILE_EXTDATA_MSG ("last user page table component") FILE_HEAD_FULL_MSG,
	    VPID_AS_ARGS (vpid), file_extdata_item_count (extdata_user_page_ftab) - 1, VFID_AS_ARGS (&fhead->self),
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
file_extdata_find_nth_vpid_and_skip_marked (THREAD_ENTRY * thread_p, const void *data, int index, bool * stop,
					    void *args)
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
flre_numerable_find_nth (THREAD_ENTRY * thread_p, const VFID * vfid, int nth, bool auto_alloc, VPID * vpid_nth)
{
  VPID vpid_fhead;
  PAGE_PTR page_fhead = NULL;
  FLRE_HEADER *fhead = NULL;

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
  fhead = (FLRE_HEADER *) page_fhead;
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
	  fhead = (FLRE_HEADER *) page_fhead;
	  file_header_sanity_check (fhead);
	  if (auto_alloc && nth == (fhead->n_page_user - fhead->n_page_mark_delete))
	    {
	      error_code = flre_alloc (thread_p, vfid, vpid_nth);
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
      error_code = flre_alloc (thread_p, vfid, vpid_nth);
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
      error_code =
	file_extdata_apply_funcs (thread_p, extdata_user_page_ftab, NULL, NULL,
				  file_extdata_find_nth_vpid_and_skip_marked, &find_nth_context, false, NULL, NULL);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto exit;
	}
    }
  else
    {
      /* we can go directly to the right VPID. */
      error_code =
	file_extdata_apply_funcs (thread_p, extdata_user_page_ftab, file_extdata_find_nth_vpid, &find_nth_context, NULL,
				  NULL, false, NULL, NULL);
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

  file_log ("file_rv_user_page_mark_delete", "marked deleted vpid %d|%d in page %d|%d lsa %lld|%d at offset %d",
	    VPID_AS_ARGS (vpid_ptr), PGBUF_PAGE_VPID_AS_ARGS (page_ftab), PGBUF_PAGE_LSA_AS_ARGS (page_ftab),
	    rcv->offset);

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
  FLRE_HEADER *fhead;
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

  fhead = (FLRE_HEADER *) page_fhead;
  file_header_sanity_check (fhead);

  assert (FILE_IS_NUMERABLE (fhead));
  assert (!FILE_IS_TEMPORARY (fhead));

  /* search for VPID in user page table */
  FILE_HEADER_GET_USER_PAGE_FTAB (fhead, extdata_user_page_ftab);
  error_code =
    file_extdata_search_item (thread_p, &extdata_user_page_ftab, vpid, file_compare_vpids, false, true, &found,
			      &position, &page_ftab);
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

  file_log ("file_rv_user_page_unmark_delete_logical", "unmark delete vpid %d|%d in file %d|%d, page %d|%d, "
	    "prev_lsa %lld|%d, crt_lsa %lld|%d, at offset %d", VPID_AS_ARGS (vpid_in_table), VFID_AS_ARGS (vfid),
	    PGBUF_PAGE_VPID_AS_ARGS (addr.pgptr), LSA_AS_ARGS (&save_lsa), PGBUF_PAGE_VPID_AS_ARGS (addr.pgptr),
	    addr.offset);

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

  file_log ("file_rv_user_page_unmark_delete_physical", "unmark delete vpid %d|%d in page %d|%d, lsa %lld|%d, "
	    "at offset %d", VPID_AS_ARGS (vpid_ptr), PGBUF_PAGE_VPID_AS_ARGS (page_ftab),
	    PGBUF_PAGE_LSA_AS_ARGS (page_ftab), rcv->offset);

  pgbuf_set_dirty (thread_p, page_ftab, DONT_FREE);
  return NO_ERROR;
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
  FLRE_HEADER *fhead = (FLRE_HEADER *) page_fhead;
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
	disk_reserve_sectors (thread_p, DB_TEMPORARY_DATA_PURPOSE, DISK_NONCONTIGUOUS_SPANVOLS_PAGES,
			      fhead->volid_last_expand, 1, &partsect_new.vsid);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto exit;
	}

      file_log ("file_temp_alloc", "no free pages" FILE_HEAD_ALLOC_MSG "\texpand file with VSID = %d|%d ",
		FILE_HEAD_ALLOC_AS_ARGS (fhead), VSID_AS_ARGS (&partsect_new.vsid));

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
	  page_ftab = page_ftab_new;

	  file_log ("file_temp_alloc", "used newly reserved sector's first page %d|%d for partial table.",
		    VPID_AS_ARGS (&vpid_ftab_new));
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

      file_log ("file_temp_alloc", "new partial sector added to partial extensible data:\n"
		FILE_PARTSECT_MSG ("newly reserved sector") FILE_EXTDATA_MSG ("last partial table component"),
		FILE_PARTSECT_AS_ARGS (&partsect_new), FILE_EXTDATA_AS_ARGS (extdata_part_ftab));

      file_header_sanity_check (fhead);
    }
  assert (fhead->n_page_free > 0);

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

  file_log ("file_temp_alloc", "%s %d|%d successful." FILE_HEAD_FULL_MSG
	    FILE_PARTSECT_MSG ("partial sector after alloc"), FILE_ALLOC_TYPE_STRING (alloc_type),
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
 * flre_temp_set_type () - set new type in existing temporary file
 *
 * return        : error code
 * thread_p (in) : thread entry
 * vfid (in)     : file identifier
 * ftype (in)    : new file type
 */
STATIC_INLINE int
flre_temp_set_type (THREAD_ENTRY * thread_p, VFID * vfid, FILE_TYPE ftype)
{
  VPID vpid_fhead;
  PAGE_PTR page_fhead = NULL;
  FLRE_HEADER *fhead = NULL;

  int error_code = NO_ERROR;

  FILE_GET_HEADER_VPID (vfid, &vpid_fhead);
  page_fhead = pgbuf_fix (thread_p, &vpid_fhead, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (page_fhead == NULL)
    {
      ASSERT_ERROR_AND_SET (error_code);
      return error_code;
    }
  fhead = (FLRE_HEADER *) page_fhead;
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
  FLRE_HEADER *fhead = NULL;

  FILE_FTAB_COLLECTOR collector = FILE_FTAB_COLLECTOR_INITIALIZER;
  FILE_EXTENSIBLE_DATA *extdata_part_ftab;
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
  fhead = (FLRE_HEADER *) page_fhead;
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

  /* reset partial table */
  FILE_HEADER_GET_PART_FTAB (fhead, extdata_part_ftab);

  /* collect file table pages */
  collector.partsect_ftab =
    (FILE_PARTIAL_SECTOR *) db_private_alloc (thread_p, fhead->n_page_ftab * sizeof (FILE_PARTIAL_SECTOR));
  if (collector.partsect_ftab == NULL)
    {
      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 1, fhead->n_page_ftab * sizeof (FILE_PARTIAL_SECTOR));
      goto exit;
    }
  /* add page header */
  VSID_FROM_VPID (&collector.partsect_ftab[0].vsid, &vpid_fhead);
  collector.partsect_ftab[0].page_bitmap = FILE_EMPTY_PAGE_BITMAP;
  file_partsect_set_bit (&collector.partsect_ftab[0],
			 file_partsect_pageid_to_offset (&collector.partsect_ftab[0], vpid_fhead.pageid));
  collector.nsects = 1;
  collector.npages = 1;

  file_log ("file_temp_reset_user_pages", "init collector with page %d|%d \n " FILE_PARTSECT_MSG ("partsect"),
	    VPID_AS_ARGS (&vpid_fhead), FILE_PARTSECT_AS_ARGS (collector.partsect_ftab));

  /* add other pages in partial sector table */
  error_code =
    file_extdata_apply_funcs (thread_p, extdata_part_ftab, file_table_collect_ftab_pages, &collector, NULL, NULL, false,
			      NULL, NULL);
  if (error_code != NO_ERROR)
    {
      assert_release (false);
      error_code = ER_FAILED;
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
		      memmove (&collector.partsect_ftab[idx_sect], &collector.partsect_ftab[idx_sect + 1],
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
 * flre_temp_preserve () - preserve temporary file
 *
 * return :
 * THREAD_ENTRY * thread_p (in) :
 * const VFID * vfid (in) :
 */
void
flre_temp_preserve (THREAD_ENTRY * thread_p, const VFID * vfid)
{
  /* to preserve the file, we need to remove it from transaction list */
  FLRE_TEMPCACHE_ENTRY *entry = flre_tempcache_pop_tran_file (thread_p, vfid);
  if (entry == NULL)
    {
      assert_release (false);
    }
  else
    {
      flre_tempcache_retire_entry (entry);
    }
}

/************************************************************************/
/* Temporary cache section                                              */
/************************************************************************/

/*
 * flre_tempcache_init () - init temporary file cache
 *
 * return : ER_OUT_OF_VIRTUAL_MEMORY or NO_ERROR
 */
static int
flre_tempcache_init (void)
{
  int memsize = 0;
#if defined (SERVER_MODE)
  int ntrans = logtb_get_number_of_total_tran_indices () + 1;
#else
  int ntrans = 1;
#endif

  assert (flre_Tempcache == NULL);

  /* allocate flre_Tempcache */
  memsize = sizeof (FLRE_TEMPCACHE);
  flre_Tempcache = (FLRE_TEMPCACHE *) malloc (memsize);
  if (flre_Tempcache == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, memsize);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  /* initialize free entry list... used to avoid entry allocation/deallocation */
  flre_Tempcache->free_entries = NULL;
  flre_Tempcache->nfree_entries_max = ntrans * 8;	/* I set 8 per transaction, maybe there is a better value */
  flre_Tempcache->nfree_entries = 0;

  /* initialize temporary file cache. we keep two separate lists for numerable and regular files */
  flre_Tempcache->cached_not_numerable = NULL;
  flre_Tempcache->cached_numerable = NULL;
  flre_Tempcache->ncached_max = prm_get_integer_value (PRM_ID_MAX_ENTRIES_IN_TEMP_FILE_CACHE);
  flre_Tempcache->ncached_not_numerable = 0;
  flre_Tempcache->ncached_numerable = 0;

  /* initialize mutex used to protect temporary file cache and entry allocation/deallocation */
  pthread_mutex_init (&flre_Tempcache->mutex, NULL);
#if !defined (NDEBUG)
  flre_Tempcache->owner_mutex = -1;
#endif

  /* allocate transaction temporary files lists */
  memsize = ntrans * sizeof (FLRE_TEMPCACHE *);
  flre_Tempcache->tran_files = (FLRE_TEMPCACHE_ENTRY **) malloc (memsize);
  if (flre_Tempcache->tran_files == NULL)
    {
      pthread_mutex_destroy (&flre_Tempcache->mutex);
      free_and_init (flre_Tempcache);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, memsize);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  memset (flre_Tempcache->tran_files, 0, memsize);

  /* all ok */
  return NO_ERROR;
}

/*
 * flre_tempcache_final () - free temporary files cache resources
 *
 * return : void
 */
static void
flre_tempcache_final (void)
{
  if (flre_Tempcache)
    {
      int tran = 0;
#if defined (SERVER_MODE)
      int ntrans = logtb_get_number_of_total_tran_indices ();
#else
      int ntrans = 1;
#endif

      flre_tempcache_lock ();

      /* free all transaction lists... they should be empty anyway, but be conservative */
      for (tran = 0; tran < ntrans; tran++)
	{
	  if (flre_Tempcache->tran_files[tran] != NULL)
	    {
	      /* should be empty */
	      assert (false);
	      flre_tempcache_free_entry_list (&flre_Tempcache->tran_files[tran]);
	    }
	}
      free_and_init (flre_Tempcache->tran_files);

      /* temporary volumes are removed, we don't have to destroy files */
      flre_tempcache_free_entry_list (&flre_Tempcache->cached_not_numerable);
      flre_tempcache_free_entry_list (&flre_Tempcache->cached_numerable);

      flre_tempcache_free_entry_list (&flre_Tempcache->free_entries);

      flre_tempcache_unlock ();

      pthread_mutex_destroy (&flre_Tempcache->mutex);

      free_and_init (flre_Tempcache);
    }
}

/*
 * flre_tempcache_free_entry_list () - free entry list and set it to NULL
 *
 * return        : void
 * list (in/out) : list to free. it becomes NULL.
 */
STATIC_INLINE void
flre_tempcache_free_entry_list (FLRE_TEMPCACHE_ENTRY ** list)
{
  FLRE_TEMPCACHE_ENTRY *entry;
  FLRE_TEMPCACHE_ENTRY *next;

  flre_tempcache_check_lock ();

  for (entry = *list; entry != NULL; entry = next)
    {
      next = entry->next;
      free (entry);
    }
  *list = NULL;
}

/*
 * flre_tempcache_alloc_entry () - allocate a new file temporary cache entry
 *
 * return      : error code
 * entry (out) : entry from free entries or newly allocated
 */
STATIC_INLINE int
flre_tempcache_alloc_entry (FLRE_TEMPCACHE_ENTRY ** entry)
{
  flre_tempcache_check_lock ();
  if (flre_Tempcache->free_entries != NULL)
    {
      assert (flre_Tempcache->nfree_entries > 0);

      *entry = flre_Tempcache->free_entries;
      flre_Tempcache->free_entries = flre_Tempcache->free_entries->next;
      flre_Tempcache->nfree_entries--;
    }
  else
    {
      *entry = (FLRE_TEMPCACHE_ENTRY *) malloc (sizeof (FLRE_TEMPCACHE_ENTRY));
      if (*entry == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (FLRE_TEMPCACHE_ENTRY));
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
    }
  (*entry)->next = NULL;
  VFID_SET_NULL (&(*entry)->vfid);
  (*entry)->ftype = FILE_UNKNOWN_TYPE;
  return NO_ERROR;
}

/*
 * flre_tempcache_retire_entry () - retire entry to free entry list (if not maxed) or deallocate
 *
 * return     : void
 * entry (in) : retired entry
 */
STATIC_INLINE void
flre_tempcache_retire_entry (FLRE_TEMPCACHE_ENTRY * entry)
{
  /* we lock to change free entry list */
  flre_tempcache_lock ();
  if (flre_Tempcache->nfree_entries < flre_Tempcache->nfree_entries_max)
    {
      entry->next = flre_Tempcache->free_entries;
      flre_Tempcache->free_entries = entry;
      flre_Tempcache->nfree_entries++;
    }
  else
    {
      free (entry);
    }
  flre_tempcache_unlock ();
}

/*
 * flre_tempcache_lock () - lock temporary cache mutex
 *
 * return : void
 */
STATIC_INLINE void
flre_tempcache_lock (void)
{
  assert (flre_Tempcache->owner_mutex != thread_get_current_entry_index ());
  pthread_mutex_lock (&flre_Tempcache->mutex);
  assert (flre_Tempcache->owner_mutex == -1);
#if !defined (NDEBUG)
  flre_Tempcache->owner_mutex = thread_get_current_entry_index ();
#endif /* !NDEBUG */
}

/*
 * flre_tempcache_unlock () - unlock temporary cache mutex
 *
 * return : void
 */
STATIC_INLINE void
flre_tempcache_unlock (void)
{
  assert (flre_Tempcache->owner_mutex == thread_get_current_entry_index ());
#if !defined (NDEBUG)
  flre_Tempcache->owner_mutex = -1;
#endif /* !NDEBUG */
  pthread_mutex_unlock (&flre_Tempcache->mutex);
}

/*
 * flre_tempcache_check_lock () - check temporary cache mutex is locked (for debug)
 *
 * return : void
 */
STATIC_INLINE void
flre_tempcache_check_lock (void)
{
  assert (flre_Tempcache->owner_mutex == thread_get_current_entry_index ());
}

/*
 * flre_tempcache_get () - get a file from temporary file cache
 *
 * return         : error code
 * thread_p (in)  : thread entry
 * ftype (in)     : file type
 * numerable (in) : true for numerable file, false for regular file
 * entry (out)    : always output an temporary cache entry. caller must check entry VFID to find if cached file was used
 */
STATIC_INLINE int
flre_tempcache_get (THREAD_ENTRY * thread_p, FILE_TYPE ftype, bool numerable, FLRE_TEMPCACHE_ENTRY ** entry)
{
  int error_code = NO_ERROR;

  assert (entry != NULL && *entry == NULL);

  flre_tempcache_lock ();

  *entry = numerable ? flre_Tempcache->cached_numerable : flre_Tempcache->cached_not_numerable;
  if (*entry != NULL && (*entry)->ftype != ftype)
    {
      /* change type */
      error_code = flre_temp_set_type (thread_p, &(*entry)->vfid, ftype);
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
	  assert (*entry == flre_Tempcache->cached_numerable);
	  assert (flre_Tempcache->ncached_numerable > 0);
	  flre_Tempcache->cached_numerable = flre_Tempcache->cached_numerable->next;
	  flre_Tempcache->ncached_numerable--;
	}
      else
	{
	  assert (*entry == flre_Tempcache->cached_not_numerable);
	  assert (flre_Tempcache->ncached_not_numerable > 0);
	  flre_Tempcache->cached_not_numerable = flre_Tempcache->cached_not_numerable->next;
	  flre_Tempcache->ncached_not_numerable--;
	}
      (*entry)->next = NULL;

      file_log ("flre_tempcache_get", "found in cache temporary file entry " FILE_TEMPCACHE_ENTRY_MSG ", %s\n"
		FILE_TEMPCACHE_MSG, FILE_TEMPCACHE_ENTRY_AS_ARGS (*entry), numerable ? "numerable" : "regular",
		FILE_TEMPCACHE_AS_ARGS);

      flre_tempcache_unlock ();
      return NO_ERROR;
    }

  /* not from cache, get a new entry */
  error_code = flre_tempcache_alloc_entry (entry);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      flre_tempcache_unlock ();
      return error_code;
    }
  /* init new entry */
  assert (*entry != NULL);
  (*entry)->next = NULL;
  (*entry)->ftype = ftype;
  VFID_SET_NULL (&(*entry)->vfid);
  flre_tempcache_unlock ();
  return NO_ERROR;
}

/*
 * flre_tempcache_put () - put entry to file cache (if cache is not full and if file passes vetting)
 *
 * return        : true if file was cached, false otherwise
 * thread_p (in) : thread entry
 * entry (in)    : entry of retired temporary file
 */
STATIC_INLINE bool
flre_tempcache_put (THREAD_ENTRY * thread_p, FLRE_TEMPCACHE_ENTRY * entry)
{
  FLRE_HEADER fhead;

  fhead.n_page_user = -1;
  if (file_header_copy (thread_p, &entry->vfid, &fhead) != NO_ERROR
      || fhead.n_page_user > prm_get_integer_value (PRM_ID_MAX_PAGES_IN_TEMP_FILE_CACHE))
    {
      /* file not valid for cache */
      file_log ("flre_tempcache_put", "could not cache temporary file " FILE_TEMPCACHE_ENTRY_MSG
		", fhead->n_page_user = %d ", FILE_TEMPCACHE_ENTRY_AS_ARGS (entry), fhead.n_page_user);
      return false;
    }

  /* lock temporary cache */
  flre_tempcache_lock ();

  if (flre_Tempcache->ncached_not_numerable + flre_Tempcache->ncached_numerable < flre_Tempcache->nfree_entries_max)
    {
      /* cache not full */
      assert ((flre_Tempcache->cached_not_numerable == NULL) == (flre_Tempcache->ncached_not_numerable == 0));
      assert ((flre_Tempcache->cached_numerable == NULL) == (flre_Tempcache->ncached_numerable == 0));

      /* reset file */
      if (file_temp_reset_user_pages (thread_p, &entry->vfid) != NO_ERROR)
	{
	  /* failed to reset file, we cannot cache it */
	  ASSERT_ERROR ();

	  file_log ("flre_tempcache_put", "could not cache temporary file " FILE_TEMPCACHE_ENTRY_MSG
		    ", error during file reset", FILE_TEMPCACHE_ENTRY_AS_ARGS (entry));
	  flre_tempcache_unlock ();
	  return false;
	}

      /* add numerable temporary file to cached numerable file list, regular file to not numerable list */
      if (FILE_IS_NUMERABLE (&fhead))
	{
	  entry->next = flre_Tempcache->cached_numerable;
	  flre_Tempcache->cached_numerable = entry;
	  flre_Tempcache->ncached_numerable++;
	}
      else
	{
	  entry->next = flre_Tempcache->cached_not_numerable;
	  flre_Tempcache->cached_not_numerable = entry;
	  flre_Tempcache->ncached_not_numerable++;
	}

      file_log ("flre_tempcache_put", "cached temporary file " FILE_TEMPCACHE_ENTRY_MSG ", %s\n" FILE_TEMPCACHE_MSG,
		FILE_TEMPCACHE_ENTRY_AS_ARGS (entry), FILE_IS_NUMERABLE (&fhead) ? "numerable" : "regular",
		FILE_TEMPCACHE_AS_ARGS);

      flre_tempcache_unlock ();

      /* cached */
      return true;
    }
  else
    {
      /* cache full */
      file_log ("flre_tempcache_put", "could not cache temporary file " FILE_TEMPCACHE_ENTRY_MSG
		", temporary cache is full \n" FILE_TEMPCACHE_MSG, FILE_TEMPCACHE_ENTRY_AS_ARGS (entry),
		FILE_TEMPCACHE_AS_ARGS);
      flre_tempcache_unlock ();
      return false;
    }
}

/*
 * flre_tempcache_drop_tran_temp_files () - drop all temporary files created by current transaction
 *
 * return        : void
 * thread_p (in) : thread entry
 */
void
flre_tempcache_drop_tran_temp_files (THREAD_ENTRY * thread_p)
{
#if defined (SERVER_MODE)
  file_tempcache_cache_or_drop_entries (thread_p, &flre_Tempcache->tran_files[thread_get_current_tran_index ()]);
  file_log ("flre_tempcache_drop_tran_temp_files", "transaction %d", thread_get_current_tran_index ());
#else
  file_tempcache_cache_or_drop_entries (thread_p, &flre_Tempcache->tran_files[0]);
  file_log ("flre_tempcache_drop_tran_temp_files", "");
#endif
}

/*
 * file_tempcache_cache_or_drop_entries () - drop all temporary files in the give entry list
 *
 * return        : void
 * thread_p (in) : thread entry
 * entries (in)  : temporary files entry list
 */
STATIC_INLINE void
file_tempcache_cache_or_drop_entries (THREAD_ENTRY * thread_p, FLRE_TEMPCACHE_ENTRY ** entries)
{
  FLRE_TEMPCACHE_ENTRY *temp_file;
  FLRE_TEMPCACHE_ENTRY *next = NULL;
  bool save_interrupt = thread_set_check_interrupt (thread_p, false);

  for (temp_file = *entries; temp_file != NULL; temp_file = next)
    {
      next = temp_file->next;
      temp_file->next = NULL;

      if (!flre_tempcache_put (thread_p, temp_file))
	{
	  /* was not cached. destroy the file */
	  file_log ("file_tempcache_cache_or_drop_entries", "drop entry " FILE_TEMPCACHE_ENTRY_MSG,
		    FILE_TEMPCACHE_ENTRY_AS_ARGS (temp_file));
	  if (flre_destroy (thread_p, &temp_file->vfid) != NO_ERROR)
	    {
	      /* file is leaked */
	      assert_release (false);
	      /* ignore error and continue free as many files as possible */
	    }
	  flre_tempcache_retire_entry (temp_file);
	}
    }
  *entries = NULL;

  (void) thread_set_check_interrupt (thread_p, save_interrupt);
}

/*
 * flre_tempcache_pop_tran_file () - pop entry with the given VFID from transaction list
 *
 * return        : popped entry
 * thread_p (in) : thread entry
 * vfid (in)     : file identifier
 */
STATIC_INLINE FLRE_TEMPCACHE_ENTRY *
flre_tempcache_pop_tran_file (THREAD_ENTRY * thread_p, const VFID * vfid)
{
  FLRE_TEMPCACHE_ENTRY **tran_files_p =
#if defined (SERVER_MODE)
    &flre_Tempcache->tran_files[thread_get_current_tran_index ()];
#else
    &flre_Tempcache->tran_files[0];
#endif
  FLRE_TEMPCACHE_ENTRY *entry = NULL, *prev_entry = NULL;

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

	  file_log ("flre_tempcache_pop_tran_file", "tran %d removed entry " FILE_TEMPCACHE_ENTRY_MSG,
		    tran_files_p - flre_Tempcache->tran_files, FILE_TEMPCACHE_ENTRY_AS_ARGS (entry));

	  return entry;
	}
    }
  /* should have found it */
  assert_release (false);
  return NULL;
}

/*
 * flre_tempcache_push_tran_file () - push temporary file entry to transaction list
 *
 * return        : void
 * thread_p (in) : thread entry
 * entry (in)    : temporary cache entry
 */
STATIC_INLINE void
flre_tempcache_push_tran_file (THREAD_ENTRY * thread_p, FLRE_TEMPCACHE_ENTRY * entry)
{
  FLRE_TEMPCACHE_ENTRY **tran_files_p =
#if defined (SERVER_MODE)
    &flre_Tempcache->tran_files[thread_get_current_tran_index ()];
#else
    &flre_Tempcache->tran_files[0];
#endif

  entry->next = *tran_files_p;
  *tran_files_p = entry;

  file_log ("flre_tempcache_push_tran_file", "pushed entry " FILE_TEMPCACHE_ENTRY_MSG " to tran %d temporary files \n",
	    FILE_TEMPCACHE_ENTRY_AS_ARGS (entry), tran_files_p - flre_Tempcache->tran_files);
}

int
flre_get_tran_num_temp_files (THREAD_ENTRY * thread_p)
{
  FLRE_TEMPCACHE_ENTRY **tran_files_p =
#if defined (SERVER_MODE)
    &flre_Tempcache->tran_files[thread_get_current_tran_index ()];
#else
    &flre_Tempcache->tran_files[0];
#endif
  FLRE_TEMPCACHE_ENTRY *entry;
  int num = 0;

  for (entry = *tran_files_p; entry != NULL; entry = entry->next)
    {
      num++;
    }
  return num;
}

/*
 * flre_tempcache_dump () - dump temporary files cache
 *
 * return  : void
 * fp (in) : dump output
 */
STATIC_INLINE void
flre_tempcache_dump (FILE * fp)
{
  FLRE_TEMPCACHE_ENTRY *cached_files;
  flre_tempcache_lock ();

  fprintf (fp, "DUMPING file manager's temporary files cache.\n");
  fprintf (fp, "  max files = %d, regular files count = %d, numerable files count = %d.\n\n",
	   flre_Tempcache->ncached_max, flre_Tempcache->ncached_not_numerable, flre_Tempcache->ncached_numerable);

  if (flre_Tempcache->cached_not_numerable != NULL)
    {
      fprintf (fp, "  cached regular files: \n");
      for (cached_files = flre_Tempcache->cached_not_numerable; cached_files != NULL; cached_files = cached_files->next)
	{
	  fprintf (fp, "    VFID = %d|%d, file type = %s \n", VFID_AS_ARGS (&cached_files->vfid),
		   file_type_to_string (cached_files->ftype));
	}
      fprintf (fp, "\n");
    }
  if (flre_Tempcache->cached_numerable != NULL)
    {
      fprintf (fp, "  cached numerable files: \n");
      for (cached_files = flre_Tempcache->cached_numerable; cached_files != NULL; cached_files = cached_files->next)
	{
	  fprintf (fp, "    VFID = %d|%d, file type = %s \n", VFID_AS_ARGS (&cached_files->vfid),
		   file_type_to_string (cached_files->ftype));
	}
      fprintf (fp, "\n");
    }

  flre_tempcache_unlock ();

  /* todo: to print transaction temporary files we need some kind of synchronization... right now each transaction
   *       manages its own list freely. */
}

/************************************************************************/
/* End of file                                                          */
/************************************************************************/

/* todo: I think I was wrong and this is not necessary. remove it when you're sure */
int
file_rv_undo_dealloc (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  VPID vpid_fhead;
  PAGE_PTR page_fhead;
  FLRE_HEADER *fhead;

  VFID *vfid;
  VPID *vpid_realloc;
  FILE_ALLOC_TYPE alloc_type;
  int offset = 0;

  VSID vsid_realloc;
  FILE_EXTENSIBLE_DATA *extdata_part_ftab;
  bool found = false;
  int position = -1;
  PAGE_PTR page_ftab = NULL;
  FILE_PARTIAL_SECTOR *partsect = NULL;
  int offset_to_realloc_bit;

  LOG_DATA_ADDR addr = LOG_DATA_ADDR_INITIALIZER;
  bool is_sysop_started = false;

  bool is_full;
  bool was_empty;

  LOG_LSA save_page_lsa;

  int error_code = NO_ERROR;

  vfid = (VFID *) (rcv->data + offset);
  offset += sizeof (VFID);

  vpid_realloc = (VPID *) (rcv->data + offset);
  offset += sizeof (VPID);

  alloc_type = *(FILE_ALLOC_TYPE *) (rcv->data + offset);
  offset += sizeof (alloc_type);

  assert (offset == rcv->length);

  FILE_GET_HEADER_VPID (vfid, &vpid_fhead);
  page_fhead = pgbuf_fix (thread_p, &vpid_fhead, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (page_fhead == NULL)
    {
      assert_release (false);
      error_code = ER_FAILED;
      goto exit;
    }

  fhead = (FLRE_HEADER *) page_fhead;
  assert (!FILE_IS_TEMPORARY (fhead));

  vsid_realloc.volid = vpid_realloc->volid;
  vsid_realloc.sectid = SECTOR_FROM_PAGEID (vpid_realloc->pageid);

  /* Search for sector in partial sectors. */
  FILE_HEADER_GET_PART_FTAB (fhead, extdata_part_ftab);
  error_code =
    file_extdata_search_item (thread_p, &extdata_part_ftab, &vsid_realloc, file_compare_vsids, true, true, &found,
			      &position, &page_ftab);
  if (error_code != NO_ERROR)
    {
      assert_release (false);
      goto exit;
    }
  if (!found)
    {
      assert_release (false);
      error_code = ER_FAILED;
      goto exit;
    }

  /* Start system op */
  log_sysop_start (thread_p);
  is_sysop_started = true;

  partsect = (FILE_PARTIAL_SECTOR *) file_extdata_at (extdata_part_ftab, position);
  was_empty = file_partsect_is_empty (partsect);
  offset_to_realloc_bit = file_partsect_pageid_to_offset (partsect, vpid_realloc->pageid);
  file_partsect_set_bit (partsect, offset_to_realloc_bit);

  addr.pgptr = page_ftab != NULL ? page_ftab : page_fhead;
  addr.offset = (PGLENGTH) ((char *) partsect - addr.pgptr);
  save_page_lsa = *pgbuf_get_lsa (addr.pgptr);
  log_append_undoredo_data (thread_p, RVFL_PARTSECT_ALLOC, &addr, sizeof (offset_to_realloc_bit),
			    sizeof (offset_to_realloc_bit), &offset_to_realloc_bit, &offset_to_realloc_bit);

  file_log ("file_rv_undo_dealloc", "realloc page %d|%d in file %d|%d page %d|%d, prev_lsa %lld|%d, crt_lsa %lld|%d, "
	    "set bit at offset %d in partsect at offset %d" FILE_PARTSECT_MSG ("partsect after"),
	    VPID_AS_ARGS (vpid_realloc), VFID_AS_ARGS (vfid), PGBUF_PAGE_VPID_AS_ARGS (addr.pgptr),
	    LSA_AS_ARGS (&save_page_lsa), PGBUF_PAGE_LSA_AS_ARGS (addr.pgptr), offset_to_realloc_bit, addr.offset,
	    FILE_PARTSECT_AS_ARGS (partsect));

  pgbuf_set_dirty (thread_p, addr.pgptr, DONT_FREE);

  is_full = file_partsect_is_full (partsect);

  /* update file header statistics. */
  file_header_alloc (fhead, alloc_type, was_empty, is_full);
  save_page_lsa = *pgbuf_get_lsa (page_fhead);
  file_log_fhead_alloc (thread_p, page_fhead, alloc_type, was_empty, is_full);

  file_log ("file_rv_undo_dealloc", "update header in file %d|%d, header page %d|%d, prev_lsa %lld|%d, "
	    "crt_lsa %lld|%d, after re%s, was_empty = %s, is_full = %s, \n" FILE_HEAD_ALLOC_MSG,
	    VFID_AS_ARGS (&fhead->self), PGBUF_PAGE_VPID_AS_ARGS (page_fhead), LSA_AS_ARGS (&save_page_lsa),
	    PGBUF_PAGE_LSA_AS_ARGS (page_fhead), FILE_ALLOC_TYPE_STRING (alloc_type),
	    was_empty ? "true" : "false", is_full ? "true" : "false", FILE_HEAD_ALLOC_AS_ARGS (fhead));

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
      save_page_lsa = *pgbuf_get_lsa (addr.pgptr);
      file_log_extdata_remove (thread_p, extdata_part_ftab, addr.pgptr, position, 1);
      file_extdata_remove_at (extdata_part_ftab, position, 1);

      file_log ("file_rv_undo_dealloc", "removed full partsect from partial table in file %d|%d, page %d|%d, "
		"prev_lsa %lld|%d, crt_lsa %lld|%d \n" FILE_EXTDATA_MSG ("partial table component"),
		VFID_AS_ARGS (vfid), PGBUF_PAGE_VPID_AS_ARGS (addr.pgptr), LSA_AS_ARGS (&save_page_lsa),
		PGBUF_PAGE_LSA_AS_ARGS (addr.pgptr), FILE_EXTDATA_AS_ARGS (extdata_part_ftab));

      /* todo: try to merge with next page? */

      if (page_ftab != NULL)
	{
	  /* we don't need this anymore */
	  pgbuf_unfix_and_init (thread_p, page_ftab);
	}

      /* add to full table */
      error_code = file_table_add_full_sector (thread_p, page_fhead, &vsid_full);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto exit;
	}
    }

  assert (error_code == NO_ERROR);

exit:

  if (page_ftab != NULL)
    {
      pgbuf_unfix_and_init (thread_p, page_ftab);
    }

  if (page_fhead != NULL)
    {
      pgbuf_unfix_and_init (thread_p, page_fhead);
    }

  return error_code;
}
