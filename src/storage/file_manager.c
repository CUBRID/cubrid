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

#include "critical_section.h"
#if defined(SERVER_MODE)
#include "connection_error.h"
#endif /* SERVER_MODE */

#if !defined(SERVER_MODE)
#define pthread_mutex_init(a, b)
#define pthread_mutex_destroy(a)
#define pthread_mutex_lock(a)	0
#define pthread_mutex_unlock(a)
static int rv;
#endif /* !SERVER_MODE */

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

#define FILE_TYPE_CACHE_SIZE 10
#define FILE_NEXPECTED_NEW_FILES 53	/* Prime number */
#define FILE_PREALLOC_MEMSIZE 20
#define FILE_DESTROY_NUM_MARKED 10

#define CAST_TO_SECTARRAY(asectid_ptr, pgptr, offset)\
  ((asectid_ptr) = (INT32 *)((char *)(pgptr) + (offset)))

#define CAST_TO_PAGEARRAY(apageid_ptr, pgptr, offset)\
  ((apageid_ptr) = (INT32 *)((char *)(pgptr) + (offset)))

#define FILE_SIZEOF_FHDR_WITH_DES_COMMENTS(fhdr_ptr) \
  (SSIZEOF(*fhdr_ptr) - 1 + fhdr_ptr->des.first_length)

#define FILE_CREATE_FILE_TABLE 0
#define FILE_EXPAND_FILE_TABLE 1

#define NUM_HOLES_NEED_COMPACTION (DB_PAGESIZE / sizeof (PAGEID) / 2)

typedef struct file_rest_des FILE_REST_DES;
struct file_rest_des
{
  VPID next_part_vpid;		/* Location of the next part of file
				   description comments */
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
  int num_table_vpids;		/* Number of total pages for file table. The
				   file header and the arrays describing the
				   allocated pages reside in these pages */
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
  VPID start_pages_vpid;	/* Starting vpid address for page table for
				   this allocation set. See below for offset. */
  INT16 start_pages_offset;	/* Offset where the page table starts at the
				   starting vpid. */
  VPID end_pages_vpid;		/* Ending vpid address for page table for this
				   allocation set. See below for offset. */
  INT16 end_pages_offset;	/* Offset where the page table ends at ending
				   vpid. */
  int num_holes;		/* Indicate the number of identifiers (pages
				   or slots) that can be compacted */
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
  VPID end_pages_vpid;		/* Ending vpid address for page table for this
				   allocation set. See below for offset. */
  INT16 end_pages_offset;	/* Offset where the page table ends at ending
				   vpid. */
  int num_pages;		/* Number of pages */
  int num_holes;		/* Indicate the number of identifiers (pages or
				   slots) that can be compacted */
};

typedef struct file_recv_shift_sector_table FILE_RECV_SHIFT_SECTOR_TABLE;
struct file_recv_shift_sector_table
{
  VPID start_sects_vpid;	/* Starting vpid address for page table for
				   this allocation set. See below for offset. */
  INT16 start_sects_offset;	/* Offset where the page table starts at the
				   starting vpid. */
  VPID end_sects_vpid;		/* Ending vpid address for page table for this
				   allocation set. See below for offset. */
  INT16 end_sects_offset;	/* Offset where the page table ends at ending
				   vpid. */
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
  {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
  {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
   NULL, NULL, NULL},
  {NULL, NULL, NULL}
};

static FILE_TRACKER_CACHE *file_Tracker = &file_Tracker_cache;

#if defined(SERVER_MODE)
static pthread_mutex_t file_Type_cache_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t file_Num_mark_deleted_hint_lock =
  PTHREAD_MUTEX_INITIALIZER;
#endif /* SERVER_MODE */

#ifdef FILE_DEBUG
static int file_Debug_newallocset_multiple_of = -10;
#endif /* FILE_DEBUG */

static unsigned int file_new_files_hash_key (const void *hash_key,
					     unsigned int htsize);
static int file_new_files_hash_cmpeq (const void *hash_key1,
				      const void *hash_key2);

static int file_dump_all_newfiles (THREAD_ENTRY * thread_p, FILE * fp,
				   bool tmptmp_only);
static const char *file_type_to_string (FILE_TYPE fstruct_type);
static DISK_VOLPURPOSE file_get_primary_vol_purpose (FILE_TYPE ftype);
static const VFID *file_cache_newfile (THREAD_ENTRY * thread_p,
				       const VFID * vfid,
				       FILE_TYPE file_type);
static INT16 file_find_goodvol (THREAD_ENTRY * thread_p, INT16 hint_volid,
				INT16 undesirable_volid, INT32 exp_numpages,
				DISK_SETPAGE_TYPE setpage_type,
				FILE_TYPE file_type);
static int file_new_declare_as_old_internal (THREAD_ENTRY * thread_p,
					     const VFID * vfid, int tran_id,
					     bool hold_csect);
static FILE_IS_NEW_FILE file_isnew_with_type (THREAD_ENTRY * thread_p,
					      const VFID * vfid,
					      FILE_TYPE * file_type);
static INT32 file_find_good_maxpages (THREAD_ENTRY * thread_p,
				      FILE_TYPE file_type);
static int file_ftabvpid_alloc (THREAD_ENTRY * thread_p, INT16 hint_volid,
				INT32 hint_pageid, VPID * ftb_vpids,
				INT32 num_ftb_pages, FILE_TYPE file_type,
				int create_or_expand_file_table);
static int file_ftabvpid_next (const FILE_HEADER * fhdr,
			       PAGE_PTR current_ftb_pgptr,
			       VPID * next_ftbvpid);
static int file_find_limits (PAGE_PTR ftb_pgptr,
			     const FILE_ALLOCSET * allocset,
			     INT32 ** start_ptr, INT32 ** outside_ptr,
			     int what_table);
static VFID *file_xcreate (THREAD_ENTRY * thread_p, VFID * vfid,
			   INT32 exp_numpages, FILE_TYPE * file_type,
			   const void *file_des, VPID * first_prealloc_vpid,
			   INT32 prealloc_npages);
static int file_calculate_offset (INT16 start_offset, int size,
				  int nelements, INT16 * address_offset,
				  VPID * address_vpid, VPID * ftb_vpids,
				  int num_ftb_pages,
				  int *current_ftb_page_index);

static int file_descriptor_insert (THREAD_ENTRY * thread_p,
				   FILE_HEADER * fhdr, const void *file_des);
static int file_descriptor_update (THREAD_ENTRY * thread_p, const VFID * vfid,
				   const void *xfile_des);
static int file_descriptor_get (THREAD_ENTRY * thread_p,
				const FILE_HEADER * fhdr, void *file_des,
				int maxsize);
static int file_descriptor_destroy_rest_pages (THREAD_ENTRY * thread_p,
					       FILE_HEADER * fhdr);
#if defined (ENABLE_UNUSED_FUNCTION)
static int file_descriptor_find_num_rest_pages (THREAD_ENTRY * thread_p,
						FILE_HEADER * fhdr);
#endif
static int file_descriptor_dump_internal (THREAD_ENTRY * thread_p, FILE * fp,
					  const FILE_HEADER * fhdr);

static PAGE_PTR file_expand_ftab (THREAD_ENTRY * thread_p,
				  PAGE_PTR fhdr_pgptr);

static int file_allocset_nthpage (THREAD_ENTRY * thread_p,
				  const FILE_HEADER * fhdr,
				  const FILE_ALLOCSET * allocset,
				  VPID * nth_vpids, INT32 start_nthpage,
				  INT32 num_desired_pages);
static VPID *file_allocset_look_for_last_page (THREAD_ENTRY * thread_p,
					       const FILE_HEADER * fhdr,
					       VPID * last_vpid);
static INT32 file_allocset_alloc_sector (THREAD_ENTRY * thread_p,
					 PAGE_PTR fhdr_pgptr,
					 PAGE_PTR allocset_pgptr,
					 INT16 allocset_offset,
					 int exp_npages);
static PAGE_PTR file_allocset_expand_sector (THREAD_ENTRY * thread_p,
					     PAGE_PTR fhdr_pgptr,
					     PAGE_PTR allocset_pgptr,
					     INT16 allocset_offset);

static int file_allocset_new_set (THREAD_ENTRY * thread_p,
				  INT16 last_allocset_volid,
				  PAGE_PTR fhdr_pgptr, int exp_numpages,
				  DISK_SETPAGE_TYPE setpage_type);
static int file_allocset_add_set (THREAD_ENTRY * thread_p,
				  PAGE_PTR fhdr_pgptr, INT16 volid);
static INT32 file_allocset_add_pageids (THREAD_ENTRY * thread_p,
					PAGE_PTR fhdr_pgptr,
					PAGE_PTR allocset_pgptr,
					INT16 allocset_offset,
					INT32 alloc_pageid, INT32 npages,
					FILE_ALLOC_VPIDS * alloc_vpids);
static int file_allocset_alloc_pages (THREAD_ENTRY * thread_p,
				      PAGE_PTR fhdr_pgptr,
				      VPID * allocset_vpid,
				      INT16 allocset_offset,
				      VPID * first_new_vpid, INT32 npages,
				      const VPID * near_vpid,
				      FILE_ALLOC_VPIDS * alloc_vpids);
static DISK_ISVALID file_allocset_find_page (THREAD_ENTRY * thread_p,
					     PAGE_PTR fhdr_pgptr,
					     PAGE_PTR allocset_pgptr,
					     INT16 allocset_offset,
					     INT32 pageid,
					     int (*fun) (THREAD_ENTRY *
							 thread_p,
							 FILE_HEADER * fhdr,
							 FILE_ALLOCSET *
							 allocset,
							 PAGE_PTR fhdr_pgptr,
							 PAGE_PTR
							 allocset_pgptr,
							 INT16
							 allocset_offset,
							 PAGE_PTR pgptr,
							 INT32 * aid_ptr,
							 INT32 pageid,
							 void *args),
					     void *args);
static int file_allocset_dealloc_page (THREAD_ENTRY * thread_p,
				       FILE_HEADER * fhdr,
				       FILE_ALLOCSET * allocset,
				       PAGE_PTR fhdr_pgptr,
				       PAGE_PTR allocset_pgptr,
				       INT16 allocset_offset, PAGE_PTR pgptr,
				       INT32 * aid_ptr, INT32 ignore_pageid,
				       void *remove);
static int file_allocset_dealloc_contiguous_pages (THREAD_ENTRY * thread_p,
						   FILE_HEADER * fhdr,
						   FILE_ALLOCSET * allocset,
						   PAGE_PTR fhdr_pgptr,
						   PAGE_PTR allocset_pgptr,
						   INT16 allocset_offset,
						   PAGE_PTR pgptr,
						   INT32 * first_aid_ptr,
						   INT32 ncont_page_entries);
static int file_allocset_remove_pageid (THREAD_ENTRY * thread_p,
					FILE_HEADER * fhdr,
					FILE_ALLOCSET * allocset,
					PAGE_PTR fhdr_pgptr,
					PAGE_PTR allocset_pgptr,
					INT16 allocset_offset, PAGE_PTR pgptr,
					INT32 * aid_ptr, INT32 ignore_pageid,
					void *ignore);
static int file_allocset_remove_contiguous_pages (THREAD_ENTRY * thread_p,
						  FILE_HEADER * fhdr,
						  FILE_ALLOCSET * allocset,
						  PAGE_PTR fhdr_pgptr,
						  PAGE_PTR allocset_pgptr,
						  INT16 allocset_offset,
						  PAGE_PTR pgptr,
						  INT32 * aid_ptr,
						  INT32 num_contpages);
static int file_allocset_remove (THREAD_ENTRY * thread_p, PAGE_PTR fhdr_pgptr,
				 VPID * prev_allocset_vpid,
				 INT16 prev_allocset_offset,
				 VPID * allocset_vpid, INT16 allocset_offset);
static int file_allocset_reuse_last_set (THREAD_ENTRY * thread_p,
					 PAGE_PTR fhdr_pgptr,
					 INT16 new_volid);
static int file_allocset_compact_page_table (THREAD_ENTRY * thread_p,
					     PAGE_PTR fhdr_pgptr,
					     PAGE_PTR allocset_pgptr,
					     INT16 allocset_offset,
					     bool rm_freespace_sectors);
static int file_allocset_shift_sector_table (THREAD_ENTRY * thread_p,
					     PAGE_PTR fhdr_pgptr,
					     PAGE_PTR allocset_pgptr,
					     INT16 allocset_offset,
					     VPID * ftb_vpid,
					     INT16 ftb_offset);
static int file_allocset_compact (THREAD_ENTRY * thread_p,
				  PAGE_PTR fhdr_pgptr,
				  VPID * prev_allocset_vpid,
				  INT16 prev_allocset_offset,
				  VPID * allocset_vpid,
				  INT16 * allocset_offset, VPID * ftb_vpid,
				  INT16 * ftb_offset);
static int file_allocset_find_num_deleted (THREAD_ENTRY * thread_p,
					   FILE_HEADER * fhdr,
					   FILE_ALLOCSET * allocset,
					   int *num_deleted,
					   int *num_marked_deleted);
static int file_allocset_dump (FILE * fp, const FILE_ALLOCSET * allocset,
			       bool doprint_title);
static int file_allocset_dump_tables (THREAD_ENTRY * thread_p, FILE * fp,
				      const FILE_HEADER * fhdr,
				      const FILE_ALLOCSET * allocset);

static int file_compress (THREAD_ENTRY * thread_p, const VFID * vfid,
			  bool do_partial_compaction);
static int file_dump_fhdr (THREAD_ENTRY * thread_p, FILE * fp,
			   const FILE_HEADER * fhdr);
static int file_dump_ftabs (THREAD_ENTRY * thread_p, FILE * fp,
			    const FILE_HEADER * fhdr);

static const VFID *file_tracker_register (THREAD_ENTRY * thread_p,
					  const VFID * vfid);
static const VFID *file_tracker_unregister (THREAD_ENTRY * thread_p,
					    const VFID * vfid);

static int file_mark_deleted_file_list_add (VFID * vfid,
					    const FILE_TYPE file_type);
static int file_mark_deleted_file_list_remove (VFID * vfid,
					       const FILE_TYPE file_type);

static int file_reset_contiguous_temporary_pages (THREAD_ENTRY * thread_p,
						  INT16 volid, INT32 pageid,
						  INT32 num_pages,
						  bool reset_to_temp);
static DISK_ISVALID file_check_deleted (THREAD_ENTRY * thread_p,
					const VFID * vfid);

static FILE_TYPE file_type_cache_check (const VFID * vfid);
static int file_type_cache_add_entry (const VFID * vfid, FILE_TYPE type);
static int file_type_cache_entry_remove (const VFID * vfid);

static int file_tmpfile_cache_initialize (void);
static int file_tmpfile_cache_finalize (void);
static VFID *file_tmpfile_cache_get (THREAD_ENTRY * thread_p, VFID * vfid,
				     FILE_TYPE file_type);
static int file_tmpfile_cache_put (THREAD_ENTRY * thread_p, const VFID * vfid,
				   FILE_TYPE file_type);

static VFID *file_create_tmp_internal (THREAD_ENTRY * thread_p, VFID * vfid,
				       FILE_TYPE * file_type,
				       INT32 exp_numpages,
				       const void *file_des);
static DISK_ISVALID file_check_all_pages (THREAD_ENTRY * thread_p,
					  const VFID * vfid,
					  bool validate_vfid);

static int file_rv_tracker_unregister_logical_undo (THREAD_ENTRY * thread_p,
						    VFID * vfid);
static int file_rv_fhdr_last_allocset_helper (THREAD_ENTRY * thread_p,
					      LOG_RCV * rcv, int delta);

static void file_descriptor_dump_heap (THREAD_ENTRY * thread_p, FILE * fp,
				       const FILE_HEAP_DES * heap_file_des_p);
static void
file_descriptor_dump_multi_page_object_heap (FILE * fp,
					     const FILE_OVF_HEAP_DES *
					     ovf_hfile_des_p);
static void
file_descriptor_dump_btree_overflow_key_file_des (FILE * fp,
						  const FILE_OVF_BTREE_DES
						  * btree_ovf_des_p);
static void file_print_name_of_class (THREAD_ENTRY * thread_p, FILE * fp,
				      const OID * class_oid_p);
static void file_print_class_name_of_instance (THREAD_ENTRY * thread_p,
					       FILE * fp,
					       const OID * inst_oid_p);
static void file_print_name_of_class_with_attrid (THREAD_ENTRY * thread_p,
						  FILE * fp,
						  const OID * class_oid_p,
						  const int attr_id);
static void file_print_class_name_index_name_with_attrid (THREAD_ENTRY *
							  thread_p, FILE * fp,
							  const OID *
							  class_oid_p,
							  const VFID * vfid,
							  const int attr_id);

static int file_descriptor_get_length (const FILE_TYPE file_type);
static void file_descriptor_dump (THREAD_ENTRY * thread_p, FILE * fp,
				  const FILE_TYPE file_type,
				  const void *file_descriptor_p,
				  const VFID * vfid);
static DISK_ISVALID file_make_idsmap_image (THREAD_ENTRY * thread_p,
					    const VFID * vfid,
					    char **vol_ids_map,
					    int nperm_vols);
static DISK_ISVALID set_bitmap (char *vol_ids_map, INT32 pageid);
static DISK_ISVALID file_verify_idsmap_image (THREAD_ENTRY * thread_p,
					      INT16 volid, char *vol_ids_map);
static DISK_PAGE_TYPE file_get_disk_page_type (FILE_TYPE ftype);
static DISK_ISVALID
file_construct_space_info (THREAD_ENTRY * thread_p,
			   VOL_SPACE_INFO * space_info,
			   const VFID * vfid, int nperm_vols);


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

  return (((key->vfid.fileid | ((unsigned int) key->vfid.volid) << 24)
	   + key->tran_index) % htsize);
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

  return (key1->tran_index == key2->tran_index
	  && VFID_EQ (&key1->vfid, &key2->vfid));
}

/*
 * file_manager_initialize () - Initialize the file module
 *   return:
 */
int
file_manager_initialize (THREAD_ENTRY * thread_p)
{
  int ret = NO_ERROR;

  if (file_Tracker->newfiles.mht != NULL
      || file_Tracker->newfiles.head != NULL)
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
		      " ** DB_PAGESIZE must be > %d. Current value is set to %d",
		      sizeof (FILE_HEADER), DB_PAGESIZE);
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
  file_Tracker->newfiles.mht = mht_create ("Newfiles hash table",
					   FILE_NEXPECTED_NEW_FILES,
					   file_new_files_hash_key,
					   file_new_files_hash_cmpeq);
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
file_cache_newfile (THREAD_ENTRY * thread_p, const VFID * vfid,
		    FILE_TYPE file_type)
{
  FILE_NEWFILE *entry;
  FILE_NEW_FILES_HASH_KEY key;
  int tran_index;
  LOG_TDES *tdes;

  /* If the entry already exists (page reused), then remove it
     from the list and allocate a new entry. */
  VFID_COPY (&key.vfid, vfid);
  key.tran_index = tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);

  if (csect_enter (thread_p, CSECT_FILE_NEWFILE, INF_WAIT) != NO_ERROR)
    {
      return NULL;
    }

  entry = (FILE_NEWFILE *) mht_get (file_Tracker->newfiles.mht, &key);
  if (entry != NULL)
    {
      if (file_new_declare_as_old_internal (thread_p, vfid, entry->tran_index,
					    true) != NO_ERROR)
	{
	  csect_exit (thread_p, CSECT_FILE_NEWFILE);
	  return NULL;
	}
    }

  entry = (FILE_NEWFILE *) malloc (sizeof (*entry));
  if (entry == NULL)
    {
      csect_exit (thread_p, CSECT_FILE_NEWFILE);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      sizeof (*entry));
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

  tdes = LOG_FIND_TDES (tran_index);
  if (file_type == FILE_TMP)
    {
      tdes->num_new_tmp_files++;
    }
  else if (file_type == FILE_TMP_TMP)
    {
      tdes->num_new_tmp_tmp_files++;
    }
  tdes->num_new_files++;

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
file_new_declare_as_old_internal (THREAD_ENTRY * thread_p, const VFID * vfid,
				  int tran_id, bool hold_csect)
{
  FILE_NEWFILE *entry, *tmp;
  FILE_NEW_FILES_HASH_KEY key;
  int success = ER_FAILED;
  LOG_TDES *tdes = NULL;

  if (hold_csect == false
      && csect_enter (thread_p, CSECT_FILE_NEWFILE, INF_WAIT) != NO_ERROR)
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
      /* We want to remove only one entry. Use the hash table to locate
         this entry */
      VFID_COPY (&key.vfid, vfid);
      key.tran_index = tran_id;
      entry = (FILE_NEWFILE *) mht_get (file_Tracker->newfiles.mht, &key);
    }

  tdes = LOG_FIND_TDES (tran_id);

  while (entry != NULL)
    {
      if (entry->tran_index == tran_id
	  && (vfid == NULL || VFID_EQ (&entry->vfid, vfid)))
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

	  /* mht_rem() has been updated to take a function and an arg pointer
	   * that can be called on the entry before it is removed.  We may
	   * want to take advantage of that here to free the memory associated
	   * with the entry
	   */
	  (void) mht_rem (file_Tracker->newfiles.mht, tmp, NULL, NULL);

	  if (tmp->file_type == FILE_TMP)
	    {
	      tdes->num_new_tmp_files--;
	    }
	  else if (tmp->file_type == FILE_TMP_TMP)
	    {
	      tdes->num_new_tmp_tmp_files--;
	    }
	  tdes->num_new_files--;

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
  bool ignore;

  return file_is_new_file_with_has_undolog (thread_p, vfid, &ignore);
}

/*
 * file_isnew_with_type () - Find out if the file is a newly created file
 *   return: file_type is set to the files type or error
 *   vfid(in): Complete file identifier (i.e., Volume_id + file_id)
 *   file_type(out): return the file's type
 *
 * Note: A newly created file is one that has been created by an active
 *       transaction. As a side-effect, also return an existing file's type.
 */
static FILE_IS_NEW_FILE
file_isnew_with_type (THREAD_ENTRY * thread_p, const VFID * vfid,
		      FILE_TYPE * file_type)
{
  FILE_NEWFILE *entry;
  FILE_NEW_FILES_HASH_KEY key;
  FILE_IS_NEW_FILE newfile = FILE_OLD_FILE;

  *file_type = FILE_UNKNOWN_TYPE;

  VFID_COPY (&key.vfid, vfid);
  key.tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  if (csect_enter_as_reader (thread_p, CSECT_FILE_NEWFILE, INF_WAIT) !=
      NO_ERROR)
    {
      return FILE_ERROR;
    }

  entry = (FILE_NEWFILE *) mht_get (file_Tracker->newfiles.mht, &key);
  if (entry != NULL)
    {
      newfile = FILE_NEW_FILE;
      *file_type = entry->file_type;
    }

  csect_exit (thread_p, CSECT_FILE_NEWFILE);

  return newfile;
}

/*
 * file_is_new_file_with_has_undolog () - Find out if the file is a newly created file
 *   return: FILE_NEW_FILE if new file, FILE_OLD_FILE if not a new file,
 *           or FILE_ERROR
 *   vfid(in): Complete file identifier (i.e., Volume_id + file_id)
 *   has_undolog(in):
 *
 * Note: A newly created file is one that has been created by an active
 *       transaction. Indicate also if undo logging has been done on this file.
 *
 *       We are assuming that files are not created too frequently. If this
 *       assumption is not correct, it is better to define a hash table to
 *       find out whether or not a file is a new one.
 */
FILE_IS_NEW_FILE
file_is_new_file_with_has_undolog (THREAD_ENTRY * thread_p, const VFID * vfid,
				   bool * has_undolog)
{
  FILE_NEWFILE *entry;
  FILE_NEW_FILES_HASH_KEY key;
  FILE_IS_NEW_FILE newfile = FILE_OLD_FILE;

  *has_undolog = false;

  VFID_COPY (&key.vfid, vfid);
  key.tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);

  if (csect_enter_as_reader (thread_p, CSECT_FILE_NEWFILE, INF_WAIT) !=
      NO_ERROR)
    {
      return FILE_ERROR;
    }

  entry = (FILE_NEWFILE *) mht_get (file_Tracker->newfiles.mht, &key);
  if (entry != NULL)
    {
      newfile = FILE_NEW_FILE;
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

  if (csect_enter (thread_p, CSECT_FILE_NEWFILE, INF_WAIT) != NO_ERROR)
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

      if (entry->tran_index == tran_index
	  && (entry->file_type == FILE_TMP
	      || entry->file_type == FILE_TMP_TMP
	      || entry->file_type == FILE_EITHER_TMP)
	  && (tmp_type == FILE_EITHER_TMP || tmp_type == entry->file_type))
	{
	  p = (FILE_NEWFILE *) malloc (sizeof (*entry));
	  if (p == NULL)
	    {
	      ret = ER_OUT_OF_VIRTUAL_MEMORY;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ret, 1, sizeof (*entry));
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

  if (csect_enter (thread_p, CSECT_FILE_NEWFILE, INF_WAIT) != NO_ERROR)
    {
      return ER_FAILED;
    }
  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);
  if (tdes == NULL)
    {
      assert (false);
      return ER_FAILED;
    }

  key.tran_index = tran_index;
  VFID_COPY (&key.vfid, vfid);

  entry = (FILE_NEWFILE *) mht_get (file_Tracker->newfiles.mht, &key);
  if (entry == NULL)
    {
      csect_exit (thread_p, CSECT_FILE_NEWFILE);
      return NO_ERROR;
    }

  /* decrement temporary files counter */
  if (entry->file_type == FILE_TMP)
    {
      tdes->num_new_tmp_files--;
    }
  else if (entry->file_type == FILE_TMP_TMP)
    {
      tdes->num_new_tmp_tmp_files--;
    }
  tdes->num_new_files--;
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

  if (csect_enter_as_reader (thread_p, CSECT_FILE_NEWFILE, INF_WAIT) !=
      NO_ERROR)
    {
      return ER_FAILED;
    }

  if (file_Tracker->newfiles.head != NULL)
    {
      (void) fprintf (fp, "DUMPING new files..\n");
    }

  /* Deallocate all transient entries of new files */
  for (entry = file_Tracker->newfiles.head; entry != NULL;
       entry = entry->next)
    {
      (void) fprintf (fp, "New File = %d|%d, Type = %s, undolog = %d,\n"
		      " Created by Tran_index = %d, next = %p, prev = %p\n",
		      entry->vfid.fileid, entry->vfid.volid,
		      file_type_to_string (entry->file_type),
		      entry->has_undolog, entry->tran_index, entry->next,
		      entry->prev);
      if (tmptmp_only == false || entry->file_type == FILE_TMP_TMP)
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
    case FILE_LONGDATA:
      return "LONGDATA";
    case FILE_CATALOG:
      return "CATALOG";
    case FILE_DROPPED_FILES:
      return "DROPPED FILES";
    case FILE_VACUUM_DATA:
      return "VACUUM DATA";
    case FILE_QUERY_AREA:
      return "QUERY_AREA";
    case FILE_TMP:
    case FILE_EITHER_TMP:
      return "TEMPORARILY";
    case FILE_TMP_TMP:
      return "TEMPORARILY ON TEMPORARILY VOLUME";
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

  switch (ftype)
    {
    case FILE_TRACKER:
    case FILE_HEAP:
    case FILE_HEAP_REUSE_SLOTS:
    case FILE_MULTIPAGE_OBJECT_HEAP:
    case FILE_CATALOG:
    case FILE_EXTENDIBLE_HASH:
    case FILE_EXTENDIBLE_HASH_DIRECTORY:
    case FILE_LONGDATA:
    case FILE_DROPPED_FILES:
    case FILE_VACUUM_DATA:
      purpose = DISK_PERMVOL_DATA_PURPOSE;
      break;

    case FILE_BTREE:
    case FILE_BTREE_OVERFLOW_KEY:
      purpose = DISK_PERMVOL_INDEX_PURPOSE;
      break;

    case FILE_TMP_TMP:
      purpose = DISK_TEMPVOL_TEMP_PURPOSE;
      break;

    case FILE_QUERY_AREA:
    case FILE_EITHER_TMP:
    case FILE_TMP:
      purpose = DISK_EITHER_TEMP_PURPOSE;
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

  switch (ftype)
    {
    case FILE_TRACKER:
    case FILE_HEAP:
    case FILE_HEAP_REUSE_SLOTS:
    case FILE_MULTIPAGE_OBJECT_HEAP:
    case FILE_CATALOG:
    case FILE_EXTENDIBLE_HASH:
    case FILE_EXTENDIBLE_HASH_DIRECTORY:
    case FILE_LONGDATA:
    case FILE_DROPPED_FILES:
    case FILE_VACUUM_DATA:
      page_type = DISK_PAGE_DATA_TYPE;
      break;

    case FILE_BTREE:
    case FILE_BTREE_OVERFLOW_KEY:
      page_type = DISK_PAGE_INDEX_TYPE;
      break;

    case FILE_TMP_TMP:
    case FILE_QUERY_AREA:
    case FILE_EITHER_TMP:
    case FILE_TMP:
      page_type = DISK_PAGE_TEMP_TYPE;
      break;

    case FILE_UNKNOWN_TYPE:
    default:
      page_type = DISK_PAGE_UNKNOWN_TYPE;
      break;
    }

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
file_find_goodvol (THREAD_ENTRY * thread_p, INT16 hint_volid,
		   INT16 undesirable_volid, INT32 exp_numpages,
		   DISK_SETPAGE_TYPE setpage_type, FILE_TYPE file_type)
{
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

  return disk_find_goodvol (thread_p, hint_volid, undesirable_volid,
			    exp_numpages, setpage_type, vol_purpose);
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
  switch (file_type)
    {
    case FILE_TRACKER:
    case FILE_HEAP:
    case FILE_HEAP_REUSE_SLOTS:
    case FILE_MULTIPAGE_OBJECT_HEAP:
    case FILE_CATALOG:
    case FILE_EXTENDIBLE_HASH:
    case FILE_EXTENDIBLE_HASH_DIRECTORY:
    case FILE_LONGDATA:
    case FILE_DROPPED_FILES:
    case FILE_VACUUM_DATA:
      return disk_get_max_numpages (thread_p, DISK_PERMVOL_DATA_PURPOSE);

    case FILE_BTREE:
    case FILE_BTREE_OVERFLOW_KEY:
      return disk_get_max_numpages (thread_p, DISK_PERMVOL_INDEX_PURPOSE);

    case FILE_TMP_TMP:
      return disk_get_max_numpages (thread_p, DISK_TEMPVOL_TEMP_PURPOSE);

    case FILE_QUERY_AREA:
    case FILE_EITHER_TMP:
    case FILE_TMP:
      /* Either a permanent or temporary volume with temporary purposes */
      return disk_get_max_numpages (thread_p, DISK_EITHER_TEMP_PURPOSE);

    case FILE_UNKNOWN_TYPE:
    default:
      return disk_get_max_numpages (thread_p, DISK_PERMVOL_GENERIC_PURPOSE);
    }
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
file_find_limits (PAGE_PTR ftb_pgptr, const FILE_ALLOCSET * allocset,
		  INT32 ** start_ptr, INT32 ** outside_ptr, int what_table)
{
  VPID *vpid;
  int ret = NO_ERROR;

  vpid = pgbuf_get_vpid_ptr (ftb_pgptr);

  if (what_table == FILE_PAGE_TABLE)
    {
      if (VPID_EQ (vpid, &allocset->start_pages_vpid))
	{
	  CAST_TO_PAGEARRAY (*start_ptr, ftb_pgptr,
			     allocset->start_pages_offset);
	}
      else
	{
	  CAST_TO_PAGEARRAY (*start_ptr, ftb_pgptr, sizeof (FILE_FTAB_CHAIN));
	}

      if (VPID_EQ (vpid, &allocset->end_pages_vpid))
	{
	  CAST_TO_PAGEARRAY (*outside_ptr, ftb_pgptr,
			     allocset->end_pages_offset);
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
	  CAST_TO_SECTARRAY (*start_ptr, ftb_pgptr,
			     allocset->start_sects_offset);
	}
      else
	{
	  CAST_TO_SECTARRAY (*start_ptr, ftb_pgptr, sizeof (FILE_FTAB_CHAIN));
	}

      if (VPID_EQ (vpid, &allocset->end_sects_vpid))
	{
	  CAST_TO_SECTARRAY (*outside_ptr, ftb_pgptr,
			     allocset->end_sects_offset);
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
file_ftabvpid_alloc (THREAD_ENTRY * thread_p, INT16 hint_volid,
		     INT32 hint_pageid, VPID * ftb_vpids, INT32 num_ftb_pages,
		     FILE_TYPE file_type, int create_or_expand_file_table)
{
  INT32 pageid;
  int i;
  VOLID original_hint_volid;
  INT32 original_hint_pageid;
  bool old_check_interrupt;

  /*
   * If the file is of type FILE_TMP, it can have allocated pages which are
   * from permanent or temporary volumes. Its file table pages must be from
   * a permanent volume since they need to be logged to release pages of
   * permanent volumes
   */

  if (file_type == FILE_TMP)
    {
      INT32 free_pages, total_pages;
      INT16 nvols;

      /* Avoid creating new permanent volumes to keep track of temp pages */

      if ((disk_get_all_total_free_numpages (thread_p,
					     DISK_PERMVOL_DATA_PURPOSE,
					     &nvols, &total_pages,
					     &free_pages) > 0
	   && free_pages > num_ftb_pages)
	  || (disk_get_all_total_free_numpages (thread_p,
						DISK_PERMVOL_GENERIC_PURPOSE,
						&nvols, &total_pages,
						&free_pages) > 0
	      && free_pages > num_ftb_pages))
	{
	  file_type = FILE_TRACKER;
	}
      else
	{
	  total_pages = prm_get_integer_value (PRM_ID_BOSR_MAXTMP_PAGES);
	  if (total_pages > 0)
	    {
	      total_pages *= (IO_DEFAULT_PAGE_SIZE / IO_PAGESIZE);
	    }

	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_BO_MAXTEMP_SPACE_HAS_BEEN_EXCEEDED, 1, total_pages);
	  return ER_FAILED;
	}
    }

  original_hint_volid = hint_volid;
  original_hint_pageid = hint_pageid;

  do
    {
      if (hint_volid != NULL_VOLID)
	{
	  bool search_wrap_around;
	  DISK_PAGE_TYPE alloc_page_type =
	    file_get_disk_page_type (file_type);

	  search_wrap_around = (create_or_expand_file_table
				== FILE_CREATE_FILE_TABLE);
	  old_check_interrupt = thread_set_check_interrupt (thread_p, false);
	  pageid = disk_alloc_page (thread_p, hint_volid,
				    DISK_SECTOR_WITH_ALL_PAGES,
				    num_ftb_pages, hint_pageid,
				    search_wrap_around, alloc_page_type);

	  if (pageid != NULL_PAGEID
	      && pageid != DISK_NULL_PAGEID_WITH_ENOUGH_DISK_PAGES)
	    {
	      for (i = 0; i < num_ftb_pages; i++)
		{
		  ftb_vpids->volid = hint_volid;
		  ftb_vpids->pageid = pageid + i;
		  ftb_vpids++;
		}

	      (void) thread_set_check_interrupt (thread_p,
						 old_check_interrupt);

	      return NO_ERROR;
	    }

	  (void) thread_set_check_interrupt (thread_p, old_check_interrupt);
	}

      hint_volid = file_find_goodvol (thread_p, NULL_VOLID, hint_volid,
				      num_ftb_pages, DISK_CONTIGUOUS_PAGES,
				      file_type);

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
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_FILE_NOT_ENOUGH_PAGES_IN_DATABASE, 1, num_ftb_pages);
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
file_ftabvpid_next (const FILE_HEADER * fhdr, PAGE_PTR current_ftb_pgptr,
		    VPID * next_ftbvpid)
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
      chain =
	(FILE_FTAB_CHAIN *) (current_ftb_pgptr + FILE_FTAB_CHAIN_OFFSET);
      *next_ftbvpid = chain->next_ftbvpid;
    }

  if (VPID_EQ (vpid, next_ftbvpid))
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_FILE_FTB_LOOP, 4,
	      fhdr->vfid.fileid, fileio_get_volume_label (fhdr->vfid.volid,
							  PEEK), vpid->volid,
	      vpid->pageid);
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
file_descriptor_insert (THREAD_ENTRY * thread_p, FILE_HEADER * fhdr,
			const void *xfile_des)
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
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ret, 1,
		      sizeof (*set_vpids) * npages);
	      goto exit_on_error;
	    }
	}
      ret = file_ftabvpid_alloc (thread_p, fhdr->vfid.volid,
				 (INT32) fhdr->vfid.fileid, set_vpids,
				 npages, fhdr->type, FILE_CREATE_FILE_TABLE);
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
	      er_log_debug (ARG_FILE_LINE, "file_descriptor_insert:"
			    " ** SYSTEM ERROR calculation of number of pages needed"
			    " to store comments seems incorrect. Need more than %d"
			    " pages", npages);
	      goto exit_on_error;
	    }
#endif /* FILE_DEBUG */

	  addr.pgptr = pgbuf_fix (thread_p, &set_vpids[ipage], NEW_PAGE,
				  PGBUF_LATCH_WRITE,
				  PGBUF_UNCONDITIONAL_LATCH);
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

	  log_append_undo_data (thread_p, RVFL_FILEDESC_INS, &addr,
				sizeof (*rest) - 1 + copy_length,
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

	  log_append_redo_data (thread_p, RVFL_FILEDESC_INS, &addr,
				sizeof (*rest) - 1 + copy_length,
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
file_descriptor_update (THREAD_ENTRY * thread_p, const VFID * vfid,
			const void *xfile_des)
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
      addr.pgptr = pgbuf_fix (thread_p, &next_vpid, OLD_PAGE,
			      PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
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

	  log_append_undo_data (thread_p, RVFL_FILEDESC_UPD, &addr,
				sizeof (fhdr->des) - 1 +
				fhdr->des.first_length, &fhdr->des);

	  next_vpid = fhdr->des.next_part_vpid;

	  old_length -= fhdr->des.first_length;

	  /* Modify the new lengths */
	  fhdr->des.total_length = rest_length;
	  fhdr->des.first_length = copy_length;

	  if (file_des != NULL && copy_length > 0)
	    {
	      memcpy (fhdr->des.piece, file_des, copy_length);
	    }

	  log_append_redo_data (thread_p, RVFL_FILEDESC_UPD, &addr,
				sizeof (fhdr->des) - 1 +
				fhdr->des.first_length, &fhdr->des);
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
		  log_append_undo_data (thread_p, RVFL_FILEDESC_UPD, &addr,
					DB_PAGESIZE, (char *) addr.pgptr);
		  old_length -= DB_PAGESIZE + sizeof (*rest) - 1;
		}
	      else
		{
		  log_append_undo_data (thread_p, RVFL_FILEDESC_UPD, &addr,
					sizeof (*rest) - 1 + old_length,
					(char *) addr.pgptr);
		  old_length = 0;
		}
	    }

	  if (file_des != NULL && copy_length > 0)
	    {
	      memcpy (rest->piece, file_des, copy_length);
	    }
	  log_append_redo_data (thread_p, RVFL_FILEDESC_UPD, &addr,
				sizeof (*rest) - 1 + copy_length,
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
	      if (file_ftabvpid_alloc (thread_p,
				       pgbuf_get_volume_id (addr.pgptr),
				       pgbuf_get_page_id (addr.pgptr),
				       &next_vpid, 1, file_type,
				       FILE_EXPAND_FILE_TABLE) != NO_ERROR)
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
		  log_append_undoredo_data (thread_p,
					    RVFL_DES_FIRSTREST_NEXTVPID,
					    &addr, sizeof (next_vpid),
					    sizeof (next_vpid), &tmp_vpid,
					    &next_vpid);
		  fhdr->des.next_part_vpid = next_vpid;
		}
	      else
		{
		  /* This is part of rest part */
		  log_append_undoredo_data (thread_p, RVFL_DES_NREST_NEXTVPID,
					    &addr, sizeof (next_vpid),
					    sizeof (next_vpid), &tmp_vpid,
					    &next_vpid);
		  rest->next_part_vpid = next_vpid;
		}
	    }
	  pgbuf_set_dirty (thread_p, addr.pgptr, FREE);
	  addr.pgptr = NULL;
	}
      else
#endif
	{
	  /* The content of the object has been copied. We don't need more pages
	     for the descriptor. Deallocate any additional pages */

	  addr.offset = 0;
	  VPID_SET_NULL (&tmp_vpid);
	  if (rest == NULL)
	    {
	      /* This is the first part */
	      log_append_undoredo_data (thread_p, RVFL_DES_FIRSTREST_NEXTVPID,
					&addr, sizeof (next_vpid),
					sizeof (next_vpid), &next_vpid,
					&tmp_vpid);
	      VPID_SET_NULL (&fhdr->des.next_part_vpid);
	    }
	  else
	    {
	      /* This is part of rest part */
	      log_append_undoredo_data (thread_p, RVFL_DES_NREST_NEXTVPID,
					&addr, sizeof (next_vpid),
					sizeof (next_vpid), &next_vpid,
					&tmp_vpid);
	      VPID_SET_NULL (&rest->next_part_vpid);
	    }
	  pgbuf_set_dirty (thread_p, addr.pgptr, FREE);
	  addr.pgptr = NULL;

	  while (!(VPID_ISNULL (&next_vpid)))
	    {
	      addr.pgptr = pgbuf_fix (thread_p, &next_vpid, OLD_PAGE,
				      PGBUF_LATCH_WRITE,
				      PGBUF_UNCONDITIONAL_LATCH);
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
	      (void) file_dealloc_page (thread_p, vfid, &tmp_vpid);
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
file_descriptor_get (THREAD_ENTRY * thread_p, const FILE_HEADER * fhdr,
		     void *xfile_des, int maxsize)
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
      er_log_debug (ARG_FILE_LINE, "file_descriptor_get: **SYSTEM_ERROR:"
		    " First length = %d > total length = %d of file descriptor"
		    " for file VFID = %d|%d\n",
		    fhdr->des.first_length, fhdr->des.total_length,
		    fhdr->vfid.fileid, fhdr->vfid.volid);
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
      pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ,
			 PGBUF_UNCONDITIONAL_LATCH);
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
file_descriptor_destroy_rest_pages (THREAD_ENTRY * thread_p,
				    FILE_HEADER * fhdr)
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
      pgptr = pgbuf_fix (thread_p, &rest_vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			 PGBUF_UNCONDITIONAL_LATCH);
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
	  if (rest_vpid.volid == batch_vpid.volid
	      && rest_vpid.pageid == (batch_vpid.pageid + batch_ndealloc))
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
	      (void) disk_dealloc_page (thread_p, batch_vpid.volid,
					batch_vpid.pageid, batch_ndealloc);
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
      (void) disk_dealloc_page (thread_p, batch_vpid.volid, batch_vpid.pageid,
				batch_ndealloc);
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
file_descriptor_find_num_rest_pages (THREAD_ENTRY * thread_p,
				     FILE_HEADER * fhdr)
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
	  pgptr = pgbuf_fix (thread_p, &rest_vpid, OLD_PAGE, PGBUF_LATCH_READ,
			     PGBUF_UNCONDITIONAL_LATCH);
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
file_descriptor_dump_internal (THREAD_ENTRY * thread_p, FILE * fp,
			       const FILE_HEADER * fhdr)
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
      file_descriptor_dump (thread_p, fp,
			    fhdr->type, fhdr->des.piece, &fhdr->vfid);

    }
#if defined (ENABLE_UNUSED_FUNCTION)
  else
    {
      file_des = (char *) malloc (fhdr->des.total_length);
      if (file_des == NULL)
	{
	  ret = ER_OUT_OF_VIRTUAL_MEMORY;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ret,
		  1, fhdr->des.total_length);
	  goto exit_on_error;
	}

      if (file_descriptor_get (thread_p, fhdr, file_des,
			       fhdr->des.total_length) <= 0)
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
	case FILE_LONGDATA:
	  file_descriptor_dump (thread_p, fp, fhdr->type, file_des);
	  break;

	case FILE_CATALOG:
	case FILE_DROPPED_FILES:
	case FILE_VACUUM_DATA:
	case FILE_QUERY_AREA:
	case FILE_TMP:
	case FILE_TMP_TMP:
	case FILE_UNKNOWN_TYPE:
	  /* This does not really exist */
	case FILE_EITHER_TMP:
	  /* This does not really exist */
	default:
	  /* Dump the first part */
	  for (i = 0, dumpfrom = fhdr->des.piece;
	       i < fhdr->des.first_length; i++)
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
		  pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE,
				     PGBUF_LATCH_READ,
				     PGBUF_UNCONDITIONAL_LATCH);
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
file_calculate_offset (INT16 start_offset, int size, int nelements,
		       INT16 * address_offset, VPID * address_vpid,
		       VPID * ftb_vpids, int num_ftb_pages,
		       int *current_ftb_page_index)
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
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_FILE_TABLE_OVERFLOW,
	      5, num_ftb_pages, (idx + 1), ftb_vpids[0].volid,
	      ftb_vpids[0].pageid,
	      fileio_get_volume_label (ftb_vpids[0].volid, PEEK));

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
file_create (THREAD_ENTRY * thread_p, VFID * vfid, INT32 exp_numpages,
	     FILE_TYPE file_type, const void *file_des,
	     VPID * first_prealloc_vpid, INT32 prealloc_npages)
{
  FILE_TYPE newfile_type = file_type;

  return file_xcreate (thread_p, vfid, exp_numpages, &newfile_type, file_des,
		       first_prealloc_vpid, prealloc_npages);
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
file_create_tmp_internal (THREAD_ENTRY * thread_p, VFID * vfid,
			  FILE_TYPE * file_type, INT32 exp_numpages,
			  const void *file_des)
{
  LOG_DATA_ADDR addr;		/* address of logging data */
  bool old_val;

  /* Start a TOP SYSTEM OPERATION.
     This top system operation will be either ABORTED (case of failure) or
     COMMITTED, so that the new file becomes kind of permanent. */
  if (log_start_system_op (thread_p) == NULL)
    {
      VFID_SET_NULL (vfid);
      return NULL;
    }

  old_val = thread_set_check_interrupt (thread_p, false);

  if (file_xcreate (thread_p, vfid, exp_numpages, file_type, file_des,
		    NULL, 0) != NULL)
    {
      log_end_system_op (thread_p, LOG_RESULT_TOPOP_COMMIT);


      if (*file_type != FILE_TMP_TMP && *file_type != FILE_QUERY_AREA)
	{
	  addr.vfid = NULL;
	  addr.pgptr = NULL;
	  addr.offset = 0;
	  log_append_undo_data (thread_p, RVFL_CREATE_TMPFILE, &addr,
				sizeof (*vfid), vfid);
	  /* Force the log to reduce the possibility of unreclaiming a temporary
	     file in the case of system crash. See above */
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

  thread_set_check_interrupt (thread_p, old_val);

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
file_create_tmp (THREAD_ENTRY * thread_p, VFID * vfid, INT32 exp_numpages,
		 const void *file_des)
{
  FILE_TYPE file_type = FILE_EITHER_TMP;

  if (file_tmpfile_cache_get (thread_p, vfid, FILE_TMP_TMP))
    {
      return vfid;
    }

  if (file_tmpfile_cache_get (thread_p, vfid, FILE_TMP))
    {
      return vfid;
    }

  return file_create_tmp_internal (thread_p, vfid, &file_type, exp_numpages,
				   file_des);
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
file_create_tmp_no_cache (THREAD_ENTRY * thread_p, VFID * vfid,
			  INT32 exp_numpages, const void *file_des)
{
  FILE_TYPE file_type = FILE_EITHER_TMP;

  return file_create_tmp_internal (thread_p, vfid, &file_type, exp_numpages,
				   file_des);
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
file_create_queryarea (THREAD_ENTRY * thread_p, VFID * vfid,
		       INT32 exp_numpages, const void *file_des)
{
  FILE_TYPE file_type = FILE_QUERY_AREA;

  return file_tmpfile_cache_get (thread_p, vfid, file_type) ? vfid :
    file_create_tmp_internal (thread_p, vfid, &file_type, exp_numpages,
			      file_des);
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
file_xcreate (THREAD_ENTRY * thread_p, VFID * vfid, INT32 exp_numpages,
	      FILE_TYPE * file_type, const void *file_des,
	      VPID * first_prealloc_vpid, INT32 prealloc_npages)
{
  FILE_HEADER *fhdr;
  FILE_ALLOCSET *allocset;	/* The first allocation set */
  FILE_FTAB_CHAIN *chain;	/* Structure for linking file table
				   pages */
  PAGE_PTR fhdr_pgptr = NULL;
  INT32 *aid_ptr;		/* Pointer to portion of sector
				   identifiers */
  INT32 *outptr;		/* Out of bound pointer. */
  INT32 num_ftb_pages;		/* Number of pages for file table */
  INT32 nsects;
  INT32 sect25;			/* 25% of total number of sectors */
  INT32 sectid = NULL_SECTID;	/* Identifier of first sector for
				   the expected number of pages */
  VPID vpid;
  LOG_DATA_ADDR addr;
  char *logdata;
  VPID *table_vpids = NULL;
  VPID *vpid_ptr;
  int i, ftb_page_index;	/* Index into the allocated file
				   table pages */
  int length;
  DISK_VOLPURPOSE vol_purpose;
  DISK_SETPAGE_TYPE setpage_type;
  int ret = NO_ERROR;

  if (exp_numpages <= 0)
    {
      exp_numpages = 1;
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

  setpage_type = ((prealloc_npages < 0) ?
		  DISK_CONTIGUOUS_PAGES : DISK_NONCONTIGUOUS_SPANVOLS_PAGES);

  i = file_guess_numpages_overhead (thread_p, NULL, exp_numpages);
  exp_numpages += i;
  exp_numpages = MIN (exp_numpages, VOL_MAX_NPAGES (IO_PAGESIZE));

  vpid.volid = file_find_goodvol (thread_p, vfid->volid, NULL_VOLID,
				  exp_numpages, setpage_type, *file_type);
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
      if (exp_numpages <= 2 || vfid->volid == NULL_VOLID
	  || fileio_get_volume_descriptor (vfid->volid) == NULL_VOLDES)
	{
	  /* We could not find a volume with enough pages. An error has already
	     been set by file_find_goodvol (). */
	  goto exit_on_error;
	}
    }

  if (*file_type == FILE_EITHER_TMP)
    {
      vol_purpose = xdisk_get_purpose (thread_p, vfid->volid);
      if (vol_purpose == DISK_UNKNOWN_PURPOSE)
	{
	  goto exit_on_error;
	}
      if (vol_purpose == DISK_TEMPVOL_TEMP_PURPOSE
	  || vol_purpose == DISK_PERMVOL_TEMP_PURPOSE)
	{
	  /* Use volumes with temporary purposes only for every single page of
	     this file */
	  *file_type = FILE_TMP_TMP;
	}
      else
	{
	  *file_type = FILE_TMP;
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
      if (*file_type == FILE_TMP || *file_type == FILE_TMP_TMP ||
	  *file_type == FILE_QUERY_AREA)
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
  length = (length + (sizeof (sectid) * (nsects + FILE_NUM_EMPTY_SECTS))
	    + sizeof (vpid.pageid));

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

  /* Allocate the needed file table pages for the file.
     The first file table page is declared as the file identifier */
  table_vpids = (VPID *) malloc (sizeof (vpid) * num_ftb_pages);
  if (table_vpids == NULL)
    {
      ret = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ret,
	      1, sizeof (vpid) * num_ftb_pages);
      goto exit_on_error;
    }

  ret = file_ftabvpid_alloc (thread_p, vfid->volid, NULL_PAGEID,
			     table_vpids, num_ftb_pages, *file_type,
			     FILE_CREATE_FILE_TABLE);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }
  vfid->volid = table_vpids[0].volid;
  vfid->fileid = table_vpids[0].pageid;

  addr.vfid = vfid;

  /* Allocate the needed sectors for the newly allocated file at the volume
     where the file was allocated. */

  if (nsects > 1)
    {
      if (*file_type == FILE_TMP || *file_type == FILE_TMP_TMP ||
	  *file_type == FILE_QUERY_AREA)
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
      addr.pgptr = pgbuf_fix (thread_p, vpid_ptr, NEW_PAGE, PGBUF_LATCH_WRITE,
			      PGBUF_UNCONDITIONAL_LATCH);
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

      /* DON'T NEED to log before image (undo) since this is a newly created
         file and pages of the file would be deallocated during undo(abort). */

      addr.offset = FILE_FTAB_CHAIN_OFFSET;
      log_append_redo_data (thread_p, RVFL_FTAB_CHAIN, &addr, sizeof (*chain),
			    chain);
      pgbuf_set_dirty (thread_p, addr.pgptr, FREE);
      addr.pgptr = NULL;
    }

  fhdr_pgptr = pgbuf_fix (thread_p, &table_vpids[0], NEW_PAGE,
			  PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
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
      allocset->start_sects_offset =
	(INT16) FILE_SIZEOF_FHDR_WITH_DES_COMMENTS (fhdr);
      allocset->start_sects_offset = DB_ALIGN (allocset->start_sects_offset,
					       DOUBLE_ALIGNMENT);
    }
  else
    {
      ftb_page_index = 1;	/* Next page after header page */
      allocset->start_sects_offset = sizeof (*chain);
    }

  allocset->start_sects_vpid = table_vpids[ftb_page_index];
  allocset->end_sects_vpid = table_vpids[ftb_page_index];

  /* find out end_sects_offset */
  ret = file_calculate_offset (allocset->start_sects_offset,
			       (int) sizeof (sectid), allocset->num_sects,
			       &allocset->end_sects_offset,
			       &allocset->end_sects_vpid, table_vpids,
			       num_ftb_pages, &ftb_page_index);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  /* Calculate positions for page table in the first allocation set */

  allocset->start_pages_vpid = table_vpids[ftb_page_index];

  /* find out start_pages_offset */
  ret = file_calculate_offset (allocset->end_sects_offset,
			       (int) sizeof (sectid),
			       FILE_NUM_EMPTY_SECTS,
			       &allocset->start_pages_offset,
			       &allocset->start_pages_vpid,
			       table_vpids, num_ftb_pages, &ftb_page_index);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  /* The end of the page table is the same as the beginning since there
     is not any allocated pages */
  allocset->end_pages_vpid = allocset->start_pages_vpid;
  allocset->end_pages_offset = allocset->start_pages_offset;

  /* Indicate that this is the last set */

  VPID_SET_NULL (&allocset->next_allocset_vpid);
  allocset->next_allocset_offset = NULL_OFFSET;

  /* DON'T NEED to log before image (undo) since file and pages of file
     would be deallocated during undo(abort). */

  addr.pgptr = fhdr_pgptr;
  addr.offset = FILE_HEADER_OFFSET;
  log_append_redo_data (thread_p, RVFL_FHDR, &addr,
			FILE_SIZEOF_FHDR_WITH_DES_COMMENTS (fhdr), fhdr);
  pgbuf_set_dirty (thread_p, fhdr_pgptr, DONT_FREE);

  /* Initialize the table of sector identifiers */

  if (nsects > 0)
    {
      addr.pgptr = pgbuf_fix (thread_p, &allocset->start_sects_vpid, OLD_PAGE,
			      PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
      if (addr.pgptr == NULL)
	{
	  goto exit_on_error;
	}

      (void) pgbuf_check_page_ptype (thread_p, addr.pgptr, PAGE_FTAB);

      /* Find the location for the sector table */
      ret = file_find_limits (addr.pgptr, allocset, &aid_ptr, &outptr,
			      FILE_SECTOR_TABLE);
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

	      /* DON'T NEED to log before image (undo) since file and pages of file
	         would be deallocated during undo(abort). */
	      log_append_redo_data (thread_p, RVFL_IDSTABLE, &addr,
				    CAST_BUFLEN ((char *) aid_ptr - logdata),
				    logdata);

	      ret = file_ftabvpid_next (fhdr, addr.pgptr, &vpid);
	      if (ret != NO_ERROR)
		{
		  goto exit_on_error;
		}
	      pgbuf_set_dirty (thread_p, addr.pgptr, FREE);
	      addr.pgptr = NULL;

	      addr.pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE,
				      PGBUF_LATCH_WRITE,
				      PGBUF_UNCONDITIONAL_LATCH);
	      if (addr.pgptr == NULL)
		{
		  goto exit_on_error;
		}

	      (void) pgbuf_check_page_ptype (thread_p, addr.pgptr, PAGE_FTAB);

	      ret = file_find_limits (addr.pgptr, allocset,
				      &aid_ptr, &outptr, FILE_SECTOR_TABLE);
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

      /* Don't need to log before image (undo) since file and pages of file
         would be deallocated during undo(abort). */
      log_append_redo_data (thread_p, RVFL_IDSTABLE, &addr,
			    CAST_BUFLEN ((char *) aid_ptr - logdata),
			    logdata);
      pgbuf_set_dirty (thread_p, addr.pgptr, FREE);
      addr.pgptr = NULL;
    }

  /* Register the file and remember that this is a newly created file */
  if ((file_Tracker->vfid != NULL
       && file_tracker_register (thread_p, vfid) == NULL)
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
	  if (file_alloc_pages_at_volid (thread_p, vfid, first_prealloc_vpid,
					 prealloc_npages, NULL,
					 vfid->volid, NULL, NULL) == NULL)
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
	      return file_xcreate (thread_p, vfid, exp_numpages, file_type,
				   file_des, first_prealloc_vpid,
				   prealloc_npages);
	    }
	}
      else
	{
	  /* Second time. If it fails Bye... */
	  prealloc_npages = -prealloc_npages;
	  if (file_alloc_pages_at_volid (thread_p, vfid, first_prealloc_vpid,
					 prealloc_npages, NULL,
					 vfid->volid, NULL, NULL) == NULL)
	    {
	      /* We were unable to allocate the pages on the desired volume. */
	      (void) log_end_system_op (thread_p, LOG_RESULT_TOPOP_ABORT);
	      return NULL;
	    }
	}
    }

  /* The responsability of the creation of the file is given to the outer
     nested level */
  log_end_system_op (thread_p, LOG_RESULT_TOPOP_ATTACH_TO_OUTER);

  return vfid;

exit_on_error:

  /* ABORT THE TOP SYSTEM OPERATION. That is, the creation of the file is
     aborted, all pages that were allocated are deallocated at this point */
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
  VPID allocset_vpid;		/* Page-volume identifier of allocation set */
  VPID nxftb_vpid;		/* Page-volume identifier of file tables
				   pages. Part of allocation sets. */
  VPID curftb_vpid;
  FILE_HEADER *fhdr;
  FILE_ALLOCSET *allocset;	/* The first allocation set */
  PAGE_PTR fhdr_pgptr = NULL;
  PAGE_PTR allocset_pgptr = NULL;
  PAGE_PTR pgptr = NULL;
  INT32 *aid_ptr;		/* Pointer to portion of table of page or
				   sector allocation table */
  INT32 *outptr;		/* Out of bound pointer. */
  INT16 allocset_offset;
  LOG_DATA_ADDR addr;
  INT16 batch_volid;		/* The volume identifier in the batch of
				   contiguous ids */
  INT32 batch_firstid;		/* First sectid in batch */
  INT32 batch_ndealloc;		/* # of sectors to deallocate in the batch */
  FILE_TYPE file_type;
  bool old_val;
  bool pb_invalid_temp_called = false;
  bool out_of_range = false;
  int cached_volid_bound = -1;
  int ret = NO_ERROR;
  DISK_PAGE_TYPE page_type;

  addr.vfid = vfid;

  allocset_vpid.volid = vfid->volid;
  allocset_vpid.pageid = vfid->fileid;

  fhdr_pgptr = pgbuf_fix (thread_p, &allocset_vpid, OLD_PAGE,
			  PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (fhdr_pgptr == NULL)
    {
      return ER_FAILED;
    }

  (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);

  fhdr = (FILE_HEADER *) (fhdr_pgptr + FILE_HEADER_OFFSET);

  file_type = fhdr->type;

  if ((file_type == FILE_TMP_TMP || file_type == FILE_QUERY_AREA)
      && fhdr->num_user_pages <
      prm_get_integer_value (PRM_ID_MAX_PAGES_IN_TEMP_FILE_CACHE))
    {
      if (0 < fhdr->num_user_pages)
	{
	  /* We need to invalidate all the pages set */
	  allocset_offset = offsetof (FILE_HEADER, allocset);
	  while (!VPID_ISNULL (&allocset_vpid))
	    {
	      allocset_pgptr = pgbuf_fix (thread_p, &allocset_vpid, OLD_PAGE,
					  PGBUF_LATCH_WRITE,
					  PGBUF_UNCONDITIONAL_LATCH);
	      if (allocset_pgptr == NULL)
		{
		  pgbuf_unfix_and_init (thread_p, fhdr_pgptr);

		  return ER_FAILED;
		}

	      (void) pgbuf_check_page_ptype (thread_p, allocset_pgptr,
					     PAGE_FTAB);

	      allocset = (FILE_ALLOCSET *) ((char *) allocset_pgptr
					    + allocset_offset);

	      nxftb_vpid = allocset->start_pages_vpid;
	      while (!VPID_ISNULL (&nxftb_vpid))
		{
		  pgptr = pgbuf_fix (thread_p, &nxftb_vpid, OLD_PAGE,
				     PGBUF_LATCH_WRITE,
				     PGBUF_UNCONDITIONAL_LATCH);
		  if (pgptr == NULL)
		    {
		      pgbuf_unfix_and_init (thread_p, allocset_pgptr);
		      pgbuf_unfix_and_init (thread_p, fhdr_pgptr);

		      return ER_FAILED;
		    }

		  (void) pgbuf_check_page_ptype (thread_p, pgptr, PAGE_FTAB);

		  /* Calculate the starting offset and length of the page to
		   * check */
		  if (file_find_limits (pgptr, allocset, &aid_ptr, &outptr,
					FILE_PAGE_TABLE) != NO_ERROR)
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

				  /*
				   * In the case of temporary file on permanent
				   * volumes (i.e., FILE_TMP), set all the pages
				   * as permanent
				   */
				  pgbuf_invalidate_temporary_file
				    (allocset->volid, batch_firstid,
				     batch_ndealloc, true);

				  /* Start again */
				  batch_firstid = *aid_ptr;
				  batch_ndealloc = 1;
				}
			    }
			}
		      else
			{
			  assert (*aid_ptr == NULL_PAGEID
				  || *aid_ptr == NULL_PAGEID_MARKED_DELETED);
			}
		    }

		  if (batch_ndealloc > 0)
		    {
		      /* Deallocate any accumulated pages */

		      /* In the case of temporary file on permanent volumes
		         (i.e., FILE_TMP), set all the pages as permanent */
		      pgbuf_invalidate_temporary_file (allocset->volid,
						       batch_firstid,
						       batch_ndealloc, true);
		    }

		  /* Get next page in the allocation set */
		  if (VPID_EQ (&nxftb_vpid, &allocset->end_pages_vpid))
		    {
		      VPID_SET_NULL (&nxftb_vpid);
		    }
		  else
		    {
		      if (file_ftabvpid_next (fhdr, pgptr, &nxftb_vpid) !=
			  NO_ERROR)
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
		      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
			      ER_FILE_FTB_LOOP, 4, vfid->fileid,
			      fileio_get_volume_label (vfid->volid, PEEK),
			      allocset->next_allocset_vpid.volid,
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
      pb_invalid_temp_called = true;
      if (!out_of_range)
	{
	  if ((file_type == FILE_TMP_TMP || file_type == FILE_QUERY_AREA)
	      && file_tmpfile_cache_put (thread_p, vfid, file_type))
	    {
	      pgbuf_unfix_and_init (thread_p, fhdr_pgptr);

	      return NO_ERROR;
	    }
	}
    }

  pgbuf_unfix_and_init (thread_p, fhdr_pgptr);

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

  old_val = thread_set_check_interrupt (thread_p, false);

  ret = file_type_cache_entry_remove (vfid);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  allocset_vpid.volid = vfid->volid;
  allocset_vpid.pageid = vfid->fileid;

  fhdr_pgptr = pgbuf_fix (thread_p, &allocset_vpid, OLD_PAGE,
			  PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
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
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ret,
	      2, vfid->volid, vfid->fileid);
      goto exit_on_error;
    }

  /* Deallocate all user pages */
  if (fhdr->num_user_pages > 0)
    {
      FILE_RECV_DELETE_PAGES postpone_data;
      int num_user_pages;
      INT32 undo_data, redo_data;

      /* We need to deallocate all the pages and sectors of every allocated
         set */
      allocset_offset = offsetof (FILE_HEADER, allocset);
      while (!VPID_ISNULL (&allocset_vpid))
	{
	  /*
	   * Fetch the page that describe this allocation set even when it has
	   * already been fetched as a file header page. This is done to code
	   * the following easily.
	   */
	  allocset_pgptr = pgbuf_fix (thread_p, &allocset_vpid, OLD_PAGE,
				      PGBUF_LATCH_WRITE,
				      PGBUF_UNCONDITIONAL_LATCH);
	  if (allocset_pgptr == NULL)
	    {
	      goto exit_on_error;
	    }

	  (void) pgbuf_check_page_ptype (thread_p, allocset_pgptr, PAGE_FTAB);

	  allocset = (FILE_ALLOCSET *) ((char *) allocset_pgptr
					+ allocset_offset);

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
	      pgptr = pgbuf_fix (thread_p, &nxftb_vpid, OLD_PAGE,
				 PGBUF_LATCH_WRITE,
				 PGBUF_UNCONDITIONAL_LATCH);
	      if (pgptr == NULL)
		{
		  goto exit_on_error;
		}

	      (void) pgbuf_check_page_ptype (thread_p, pgptr, PAGE_FTAB);

	      /* Calculate the starting offset and length of the page to
	       * check */
	      if (file_find_limits (pgptr, allocset, &aid_ptr, &outptr,
				    FILE_PAGE_TABLE) != NO_ERROR)
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

			      /*
			       * In the case of temporary file on permanent
			       * volumes (i.e., FILE_TMP), set all the pages as
			       * permanent
			       */

			      if (file_type == FILE_TMP)
				{
				  ret = file_reset_contiguous_temporary_pages
				    (thread_p, allocset->volid, batch_firstid,
				     batch_ndealloc, false);
				  if (ret != NO_ERROR)
				    {
				      pgbuf_unfix_and_init (thread_p, pgptr);
				      goto exit_on_error;
				    }
				}

			      if (file_type == FILE_TMP_TMP
				  && !pb_invalid_temp_called)
				{
				  pgbuf_invalidate_temporary_file
				    (allocset->volid, batch_firstid,
				     batch_ndealloc, false);
				}

			      (void) disk_dealloc_page (thread_p,
							allocset->volid,
							batch_firstid,
							batch_ndealloc,
							page_type);
			      /* Start again */
			      batch_firstid = *aid_ptr;
			      batch_ndealloc = 1;
			    }
			}
		    }
		  else
		    {
		      assert (*aid_ptr == NULL_PAGEID
			      || *aid_ptr == NULL_PAGEID_MARKED_DELETED);
		    }
		}

	      if (batch_ndealloc > 0)
		{
		  /* Deallocate any accumulated pages */

		  /* In the case of temporary file on permanent volumes
		     (i.e., FILE_TMP), set all the pages as permanent */
		  if (file_type == FILE_TMP)
		    {
		      ret =
			file_reset_contiguous_temporary_pages (thread_p,
							       allocset->
							       volid,
							       batch_firstid,
							       batch_ndealloc,
							       false);
		      if (ret != NO_ERROR)
			{
			  pgbuf_unfix_and_init (thread_p, pgptr);
			  goto exit_on_error;
			}
		    }

		  if (file_type == FILE_TMP_TMP && !pb_invalid_temp_called)
		    {
		      pgbuf_invalidate_temporary_file (allocset->volid,
						       batch_firstid,
						       batch_ndealloc, false);
		    }

		  (void) disk_dealloc_page (thread_p, allocset->volid,
					    batch_firstid, batch_ndealloc,
					    page_type);
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
		  pgptr = pgbuf_fix (thread_p, &nxftb_vpid, OLD_PAGE,
				     PGBUF_LATCH_WRITE,
				     PGBUF_UNCONDITIONAL_LATCH);
		  if (pgptr == NULL)
		    {
		      goto exit_on_error;
		    }

		  (void) pgbuf_check_page_ptype (thread_p, pgptr, PAGE_FTAB);

		  /* Calculate the starting offset and length of the page to
		     check */
		  ret = file_find_limits (pgptr, allocset, &aid_ptr, &outptr,
					  FILE_SECTOR_TABLE);
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
				  (void) disk_dealloc_sector (thread_p,
							      allocset->volid,
							      batch_firstid,
							      batch_ndealloc);
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
		      (void) disk_dealloc_sector (thread_p, allocset->volid,
						  batch_firstid,
						  batch_ndealloc);
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
		  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
			  ret, 4, vfid->fileid,
			  fileio_get_volume_label (vfid->volid, PEEK),
			  allocset->next_allocset_vpid.volid,
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
      fhdr->num_user_pages_mrkdelete += num_user_pages;

      addr.pgptr = fhdr_pgptr;
      addr.offset = FILE_HEADER_OFFSET;
      undo_data = num_user_pages;
      redo_data = -num_user_pages;
      log_append_undoredo_data (thread_p, RVFL_FHDR_MARK_DELETED_PAGES, &addr,
				sizeof (undo_data), sizeof (redo_data),
				&undo_data, &redo_data);

      postpone_data.deleted_npages = num_user_pages;
      postpone_data.need_compaction = 0;

      log_append_postpone (thread_p, RVFL_FHDR_DELETE_PAGES, &addr,
			   sizeof (postpone_data), &postpone_data);

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
      pgptr = pgbuf_fix (thread_p, &nxftb_vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			 PGBUF_UNCONDITIONAL_LATCH);
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
      if (!(curftb_vpid.volid == vfid->volid
	    && curftb_vpid.pageid == vfid->fileid))
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
	      if (curftb_vpid.pageid == (batch_firstid + batch_ndealloc)
		  && curftb_vpid.volid == batch_volid)
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
		  (void) disk_dealloc_page (thread_p, batch_volid,
					    batch_firstid, batch_ndealloc,
					    page_type);
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
      (void) disk_dealloc_page (thread_p, batch_volid, batch_firstid,
				batch_ndealloc, page_type);
    }

  ret = file_descriptor_destroy_rest_pages (thread_p, fhdr);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  /* Now deallocate the file header page */

  if ((file_type == FILE_TMP || file_type == FILE_TMP_TMP)
      && logtb_is_current_active (thread_p) == true)
    {
      if (file_type == FILE_TMP)
	{
	  addr.vfid = NULL;
	  addr.pgptr = fhdr_pgptr;
	  addr.offset = FILE_HEADER_OFFSET;

	  log_append_undo_data (thread_p, RVFL_FHDR, &addr,
				FILE_SIZEOF_FHDR_WITH_DES_COMMENTS (fhdr),
				fhdr);

	  /* Now reset the page */

	  fhdr->num_table_vpids = 1;
	  VPID_SET_NULL (&fhdr->next_table_vpid);
	  fhdr->last_table_vpid.volid = vfid->volid;
	  fhdr->last_table_vpid.pageid = vfid->fileid;
	  fhdr->num_user_pages = 0;
	  fhdr->num_user_pages_mrkdelete = 0;
	  fhdr->num_allocsets = 0;
	  allocset = &fhdr->allocset;
	  allocset->volid = NULL_VOLID;
	  allocset->num_sects = 0;
	  allocset->num_pages = 0;
	  allocset->curr_sectid = NULL_SECTID;
	  allocset->num_holes = 0;
	  VPID_SET_NULL (&allocset->start_sects_vpid);
	  VPID_SET_NULL (&allocset->end_sects_vpid);
	  VPID_SET_NULL (&allocset->start_pages_vpid);
	  VPID_SET_NULL (&allocset->end_pages_vpid);
	  VPID_SET_NULL (&allocset->next_allocset_vpid);
	  allocset->start_sects_offset = NULL_OFFSET;
	  allocset->end_sects_offset = NULL_OFFSET;
	  allocset->start_pages_offset = NULL_OFFSET;
	  allocset->end_pages_offset = NULL_OFFSET;
	  allocset->next_allocset_offset = NULL_OFFSET;

	  log_append_redo_data (thread_p, RVFL_FHDR, &addr,
				FILE_SIZEOF_FHDR_WITH_DES_COMMENTS (fhdr),
				fhdr);
	  addr.vfid = vfid;
	  pgbuf_set_dirty (thread_p, fhdr_pgptr, FREE);
	  fhdr_pgptr = NULL;

	  if (log_end_system_op (thread_p, LOG_RESULT_TOPOP_COMMIT) !=
	      TRAN_UNACTIVE_COMMITTED)
	    {
	      goto exit_on_error;
	    }
	}
      else
	{
	  /*
	   * This is a TEMPORARY FILE allocated on volumes with temporary
	   * purposes. The file can be completely removed, including its header
	   * page at this moment since there are not any log records associated
	   * with it.
	   */
	  pgbuf_unfix_and_init (thread_p, fhdr_pgptr);

	  /* remove header page */
	  if (disk_dealloc_page (thread_p, vfid->volid, vfid->fileid,
				 1, page_type) != NO_ERROR)
	    {
	      goto exit_on_error;
	    }

	  if (log_end_system_op (thread_p, LOG_RESULT_TOPOP_COMMIT) !=
	      TRAN_UNACTIVE_COMMITTED)
	    {
	      goto exit_on_error;
	    }

	  (void) file_new_declare_as_old (thread_p, vfid);
	}
    }
  else
    {
      /* Normal deletion, attach to outer nested level */
      pgbuf_unfix_and_init (thread_p, fhdr_pgptr);

      /* Now throw away the header page */
      if (disk_dealloc_page (thread_p, vfid->volid, vfid->fileid, 1,
			     page_type) != NO_ERROR)
	{
	  goto exit_on_error;
	}
      (void) file_tracker_unregister (thread_p, vfid);

      /* The responsibility of the removal of the file is given to the outer
         nested level. */
      log_end_system_op (thread_p, LOG_RESULT_TOPOP_ATTACH_TO_OUTER);
      if (log_is_tran_in_system_op (thread_p)
	  && logtb_is_current_active (thread_p) == true)
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

  thread_set_check_interrupt (thread_p, old_val);

  return ret;

exit_on_error:

  if (allocset_pgptr)
    {
      pgbuf_unfix_and_init (thread_p, allocset_pgptr);
    }

  /* ABORT THE TOP SYSTEM OPERATION. That is, the deletion of the file is
     aborted, all pages that were deallocated are undone.. */
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
 *   vfid(in): Complete file identifier
 *
 * Note: The given file is marked as deleted. None of its pages are
 *       deallocated. The deallocation of its pages is done when the file is
 *       destroyed (space reclaimed).
 */
int
file_mark_as_deleted (THREAD_ENTRY * thread_p, const VFID * vfid)
{
  PAGE_PTR fhdr_pgptr = NULL;
  LOG_DATA_ADDR addr;
  VPID vpid;
  INT16 deleted = true;

  addr.vfid = vfid;

  /*
   * Lock the file table header in exclusive mode. Unless, an error is found,
   * the page remains lock until the end of the transaction so that no other
   * transaction may access the destroyed file.
   */
  vpid.volid = vfid->volid;
  vpid.pageid = vfid->fileid;
  fhdr_pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			  PGBUF_UNCONDITIONAL_LATCH);
  if (fhdr_pgptr == NULL)
    {
      return ER_FAILED;
    }

  (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);

  addr.pgptr = fhdr_pgptr;
  addr.offset = FILE_HEADER_OFFSET;
  log_append_postpone (thread_p, RVFL_MARKED_DELETED, &addr, sizeof (deleted),
		       &deleted);

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
  fhdr_pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ,
			  PGBUF_UNCONDITIONAL_LATCH);
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
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

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
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */
  int ret = NO_ERROR;

  rv = pthread_mutex_lock (&file_Type_cache_lock);

  if (file_Type_cache.max < FILE_TYPE_CACHE_SIZE)
    {
      /* There's room at the inn, and we'll just add this to the end of the
         array of the entries. */
      candidate = file_Type_cache.max;
      file_Type_cache.max += 1;
    }
  else
    {
      /* The inn is full, and we have to evict someone.  Search for the
         least recently used entry and kick it out. */
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
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */
  int ret = NO_ERROR;

  rv = pthread_mutex_lock (&file_Type_cache_lock);

  for (i = 0; i < file_Type_cache.max; i++)
    {
      if (VFID_EQ (vfid, &file_Type_cache.entry[i].vfid))
	{
	  file_Type_cache.max -= 1;
	  /* Don't bother pulling down the last entry in the array if it's
	     the one we're removing. */
	  if (file_Type_cache.max > i)
	    {
	      file_Type_cache.entry[i] =
		file_Type_cache.entry[file_Type_cache.max];
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
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */
  int ret = NO_ERROR;

  rv = pthread_mutex_lock (&file_Type_cache_lock);

  file_Type_cache.max = 0;
  file_Type_cache.clock = 0;

  pthread_mutex_unlock (&file_Type_cache_lock);

  return ret;
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
  FILE_HEADER *fhdr;
  VPID vpid;
  FILE_TYPE file_type;

  /*
   * First check to see if this is something we've already looked at
   * recently.  If so, it will save us having to pgbuf_fix the header,
   * which can reduce the pressure on the page buffer pool.
   */
  file_type = file_type_cache_check (vfid);
  if (file_type != FILE_UNKNOWN_TYPE)
    {
      return file_type;
    }

  vpid.volid = vfid->volid;
  vpid.pageid = vfid->fileid;

  fhdr_pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ,
			  PGBUF_UNCONDITIONAL_LATCH);
  if (fhdr_pgptr == NULL)
    {
      return FILE_UNKNOWN_TYPE;
    }

  (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);

  fhdr = (FILE_HEADER *) (fhdr_pgptr + FILE_HEADER_OFFSET);
  file_type = fhdr->type;
  if (file_type_cache_add_entry (vfid, file_type) != NO_ERROR)
    {
      file_type = FILE_UNKNOWN_TYPE;
    }

  pgbuf_unfix_and_init (thread_p, fhdr_pgptr);

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
file_get_descriptor (THREAD_ENTRY * thread_p, const VFID * vfid,
		     void *area_des, int maxsize)
{
  PAGE_PTR fhdr_pgptr = NULL;
  FILE_HEADER *fhdr;
  VPID vpid;

  vpid.volid = vfid->volid;
  vpid.pageid = vfid->fileid;

  fhdr_pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ,
			  PGBUF_UNCONDITIONAL_LATCH);
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

  fhdr_pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ,
			  PGBUF_UNCONDITIONAL_LATCH);
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

  vpid.volid = vfid->volid;
  vpid.pageid = vfid->fileid;

  fhdr_pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ,
			  PGBUF_UNCONDITIONAL_LATCH);
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

  fhdr_pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ,
			  PGBUF_UNCONDITIONAL_LATCH);
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
file_get_numpages_plus_numpages_overhead (THREAD_ENTRY * thread_p,
					  const VFID * vfid,
					  INT32 * numpages,
					  INT32 * overhead_numpages)
{
  PAGE_PTR fhdr_pgptr = NULL;
  FILE_HEADER *fhdr;
  VPID vpid;

  *numpages = -1;
  *overhead_numpages = -1;

  vpid.volid = vfid->volid;
  vpid.pageid = vfid->fileid;

  fhdr_pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ,
			  PGBUF_UNCONDITIONAL_LATCH);
  if (fhdr_pgptr != NULL)
    {
      (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);

      fhdr = (FILE_HEADER *) (fhdr_pgptr + FILE_HEADER_OFFSET);
      *overhead_numpages = file_descriptor_find_num_rest_pages (thread_p,
								fhdr);
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
file_guess_numpages_overhead (THREAD_ENTRY * thread_p, const VFID * vfid,
			      INT32 npages)
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
  /* Don't use more than 10% of the page in sectors.... This is just a guess
     we don't quite know if the special sector will be assigned to us. */
  if ((nsects * SSIZEOF (nsects)) > (DB_PAGESIZE / 10))
    {
      nsects = (DB_PAGESIZE / 10) / sizeof (nsects);
    }

  if (vfid == NULL)
    {
      /* A new file... Get the number of bytes that we need to store.. */
      num_overhead_pages = (((nsects + npages) * sizeof (INT32)) +
			    sizeof (*fhdr));
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

      /* Find how many entries can be stored in the current allocation set
         Lock the file header page in shared mode and then fetch the page. */

      vpid.volid = vfid->volid;
      vpid.pageid = vfid->fileid;
      fhdr_pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ,
			      PGBUF_UNCONDITIONAL_LATCH);
      if (fhdr_pgptr == NULL)
	{
	  return -1;
	}

      (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);

      fhdr = (FILE_HEADER *) (fhdr_pgptr + FILE_HEADER_OFFSET);

      allocset_pgptr = pgbuf_fix (thread_p, &fhdr->last_allocset_vpid,
				  OLD_PAGE, PGBUF_LATCH_READ,
				  PGBUF_UNCONDITIONAL_LATCH);
      if (allocset_pgptr == NULL)
	{
	  pgbuf_unfix_and_init (thread_p, fhdr_pgptr);
	  return -1;
	}

      (void) pgbuf_check_page_ptype (thread_p, allocset_pgptr, PAGE_FTAB);

      allocset = (FILE_ALLOCSET *) ((char *) allocset_pgptr
				    + fhdr->last_allocset_offset);
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
file_allocset_nthpage (THREAD_ENTRY * thread_p, const FILE_HEADER * fhdr,
		       const FILE_ALLOCSET * allocset, VPID * nth_vpids,
		       INT32 start_nthpage, INT32 num_desired_pages)
{
  PAGE_PTR pgptr = NULL;
  VPID vpid;
  INT32 *aid_ptr;		/* Pointer to a portion of allocated pageids
				   table of given allocation set */
  INT32 *outptr;		/* Pointer to outside of portion of allocated
				   pageids */
  int num_returned;		/* Number of returned identifiers */
  int count = 0;
  int ahead;

  /* Start looking at the portion of pageids. The table of pageids for this
     allocation set may be located at several file table pages. */

  num_returned = 0;
  vpid = allocset->start_pages_vpid;
  while (!VPID_ISNULL (&vpid) && num_returned < num_desired_pages)
    {
      pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ,
			 PGBUF_UNCONDITIONAL_LATCH);
      if (pgptr == NULL)
	{
	  return -1;
	}

      (void) pgbuf_check_page_ptype (thread_p, pgptr, PAGE_FTAB);

      /* Calculate the portion of pageids that we can look at current page */
      if (file_find_limits (pgptr, allocset, &aid_ptr, &outptr,
			    FILE_PAGE_TABLE) != NO_ERROR)
	{
	  pgbuf_unfix_and_init (thread_p, pgptr);
	  return -1;
	}

      /* Find the nth page. If there is not any holes in this allocation set,
         we could address the desired page directly. */
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
	      /* The start_nthpage is in this portion of the set of pageids.
	         Advance the pointers to the desired element */
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
	      assert (*aid_ptr == NULL_PAGEID
		      || *aid_ptr == NULL_PAGEID_MARKED_DELETED);
	    }
	}

      /* Get next page */

      if (num_returned == num_desired_pages
	  || VPID_EQ (&vpid, &allocset->end_pages_vpid))
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
file_allocset_look_for_last_page (THREAD_ENTRY * thread_p,
				  const FILE_HEADER * fhdr, VPID * last_vpid)
{
  FILE_ALLOCSET *allocset;
  PAGE_PTR allocset_pgptr = NULL;

  allocset_pgptr = pgbuf_fix (thread_p, &fhdr->last_allocset_vpid, OLD_PAGE,
			      PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (allocset_pgptr == NULL)
    {
      VPID_SET_NULL (last_vpid);
      return NULL;
    }

  (void) pgbuf_check_page_ptype (thread_p, allocset_pgptr, PAGE_FTAB);

  allocset = (FILE_ALLOCSET *) ((char *) allocset_pgptr +
				fhdr->last_allocset_offset);
  if (allocset->num_pages <= 0
      || file_allocset_nthpage (thread_p, fhdr, allocset,
				last_vpid, allocset->num_pages - 1, 1) != 1)
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
file_find_nthpages (THREAD_ENTRY * thread_p, const VFID * vfid,
		    VPID * nth_vpids, INT32 start_nthpage,
		    INT32 num_desired_pages)
{
  FILE_HEADER *fhdr;
  FILE_ALLOCSET *allocset;
  PAGE_PTR fhdr_pgptr = NULL;
  PAGE_PTR allocset_pgptr = NULL;
  INT16 allocset_offset;
  VPID allocset_vpid;
  int num_returned;		/* Number of returned identifiers at
				   each allocation set */
  int total_returned;		/* Number of returned identifiers */
  int count = 0;

  allocset_vpid.volid = vfid->volid;
  allocset_vpid.pageid = vfid->fileid;

  fhdr_pgptr = pgbuf_fix (thread_p, &allocset_vpid, OLD_PAGE,
			  PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (fhdr_pgptr == NULL)
    {
      VPID_SET_NULL (nth_vpids);
      goto exit_on_error;
    }

  (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);

  fhdr = (FILE_HEADER *) (fhdr_pgptr + FILE_HEADER_OFFSET);

  if (start_nthpage < 0 || start_nthpage > fhdr->num_user_pages - 1)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_FILE_NTH_FPAGE_OUT_OF_RANGE, 4, start_nthpage, vfid->fileid,
	      fileio_get_volume_label (vfid->volid, PEEK),
	      fhdr->num_user_pages);
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
      if (file_allocset_look_for_last_page (thread_p, fhdr, nth_vpids) !=
	  NULL)
	{
	  /* The last page has been found */
	  pgbuf_unfix_and_init (thread_p, fhdr_pgptr);
	  return 1;
	}
    }

  /* Start a sequential scan to each of the allocation sets until the
     allocation set that contains the start_nthpage is found */

  allocset_offset = offsetof (FILE_HEADER, allocset);
  total_returned = 0;
  while (!VPID_ISNULL (&allocset_vpid) && total_returned < num_desired_pages)
    {
      /* Fetch the page for the allocation set description */
      allocset_pgptr = pgbuf_fix (thread_p, &allocset_vpid, OLD_PAGE,
				  PGBUF_LATCH_READ,
				  PGBUF_UNCONDITIONAL_LATCH);
      if (allocset_pgptr == NULL)
	{
	  VPID_SET_NULL (nth_vpids);
	  goto exit_on_error;
	}

      (void) pgbuf_check_page_ptype (thread_p, allocset_pgptr, PAGE_FTAB);

      /* Get the allocation set */
      allocset = (FILE_ALLOCSET *) ((char *) allocset_pgptr
				    + allocset_offset);
      if ((count + allocset->num_pages) <= start_nthpage)
	{
	  /* The desired start_nthpage is not in this set, advance to the next
	     allocation set */
	  count += allocset->num_pages;
	}
      else
	{
	  /* The desired start_nthpage is in this set */
	  if (total_returned == 0)
	    {
	      num_returned = file_allocset_nthpage (thread_p, fhdr, allocset,
						    nth_vpids,
						    start_nthpage - count,
						    num_desired_pages);
	      count += start_nthpage - count + num_returned;
	    }
	  else
	    {
	      num_returned = file_allocset_nthpage (thread_p, fhdr, allocset,
						    &nth_vpids
						    [total_returned], 0,
						    (num_desired_pages -
						     total_returned));
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

      if (total_returned >= num_desired_pages
	  || VPID_ISNULL (&allocset->next_allocset_vpid))
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
	      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_FILE_FTB_LOOP, 4, vfid->fileid,
		      fileio_get_volume_label (vfid->volid, PEEK),
		      allocset->next_allocset_vpid.volid,
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
file_find_last_page (THREAD_ENTRY * thread_p, const VFID * vfid,
		     VPID * last_vpid)
{
  VPID vpid;
  FILE_HEADER *fhdr;
  PAGE_PTR fhdr_pgptr = NULL;

  vpid.volid = vfid->volid;
  vpid.pageid = vfid->fileid;

  fhdr_pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ,
			  PGBUF_UNCONDITIONAL_LATCH);
  if (fhdr_pgptr == NULL)
    {
      return NULL;
    }

  (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);

  fhdr = (FILE_HEADER *) (fhdr_pgptr + FILE_HEADER_OFFSET);

  if (fhdr->num_user_pages > 0)
    {
      /* Get it from last allocation set. If the last allocation set does not
         have any allocated pages, we need to perform the sequential scan. */
      if (file_allocset_look_for_last_page (thread_p, fhdr, last_vpid) ==
	  NULL)
	{
	  /* it is possible that the last allocation set does not contain any
	     page, use the normal file_find_nthpages...sequential scan */
	  if (file_find_nthpages (thread_p, vfid, last_vpid,
				  fhdr->num_user_pages - 1, 1) != 1)
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
file_isvalid_page_partof (THREAD_ENTRY * thread_p, const VPID * vpid,
			  const VFID * vfid)
{
  FILE_ALLOCSET *allocset;
  VPID allocset_vpid;
  INT16 allocset_offset;
  PAGE_PTR allocset_pgptr = NULL;
  PAGE_PTR fhdr_pgptr = NULL;
  DISK_ISVALID isfound = DISK_INVALID;
  DISK_ISVALID valid;

  if (VPID_ISNULL (vpid))
    {
      return DISK_INVALID;
    }

  valid = disk_isvalid_page (thread_p, vpid->volid, vpid->pageid);
  if (valid != DISK_VALID)
    {
      if (valid != DISK_ERROR)
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_PB_BAD_PAGEID, 2,
		  vpid->pageid, fileio_get_volume_label (vpid->volid, PEEK));
	}
      return valid;
    }

  allocset_vpid.volid = vfid->volid;
  allocset_vpid.pageid = vfid->fileid;

  fhdr_pgptr = pgbuf_fix (thread_p, &allocset_vpid, OLD_PAGE,
			  PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
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
      allocset_pgptr = pgbuf_fix (thread_p, &allocset_vpid, OLD_PAGE,
				  PGBUF_LATCH_READ,
				  PGBUF_UNCONDITIONAL_LATCH);
      if (allocset_pgptr == NULL)
	{
	  pgbuf_unfix_and_init (thread_p, fhdr_pgptr);
	  return DISK_ERROR;
	}

      (void) pgbuf_check_page_ptype (thread_p, allocset_pgptr, PAGE_FTAB);

      /* Get the allocation set */
      allocset = (FILE_ALLOCSET *) ((char *) allocset_pgptr
				    + allocset_offset);
      if (allocset->volid == vpid->volid)
	{
	  /* The page may be located in this set */
	  isfound = file_allocset_find_page (thread_p, fhdr_pgptr,
					     allocset_pgptr, allocset_offset,
					     vpid->pageid, NULL, NULL);
	}
      if (isfound == DISK_ERROR)
	{
	  pgbuf_unfix_and_init (thread_p, allocset_pgptr);
	  pgbuf_unfix_and_init (thread_p, fhdr_pgptr);
	  return DISK_ERROR;
	}
      else if (isfound == DISK_INVALID)
	{
	  /* We did not find it in the current allocation set.
	     Get the next allocation set */
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
		  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_FILE_FTB_LOOP, 4, vfid->fileid,
			  fileio_get_volume_label (vfid->volid, PEEK),
			  allocset_vpid.volid, allocset_vpid.pageid);
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
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FILE_PAGE_ISNOT_PARTOF, 6,
	      vpid->volid, vpid->pageid,
	      fileio_get_volume_label (vpid->volid, PEEK), vfid->volid,
	      vfid->fileid, fileio_get_volume_label (vfid->volid, PEEK));
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
file_allocset_alloc_sector (THREAD_ENTRY * thread_p, PAGE_PTR fhdr_pgptr,
			    PAGE_PTR allocset_pgptr, INT16 allocset_offset,
			    int exp_npages)
{
  FILE_HEADER *fhdr;
  FILE_ALLOCSET *allocset;
  INT32 *aid_ptr;		/* Pointer to portion of allocated sector
				   identifiers */
  INT32 sectid;			/* Newly allocated sector identifier */
  VPID vpid;
  LOG_DATA_ADDR addr;
  FILE_RECV_ALLOCSET_SECTOR recv_undo, recv_redo;	/* Recovery stuff */
  bool special = false;

  (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);
  (void) pgbuf_check_page_ptype (thread_p, allocset_pgptr, PAGE_FTAB);

  /* Allocation of pages and sectors is done only from the last allocation set.
     This is done since the VPID must be stored in the order of allocation. */

  /* Get the file header and the allocation set */
  fhdr = (FILE_HEADER *) (fhdr_pgptr + FILE_HEADER_OFFSET);

  addr.vfid = &fhdr->vfid;
  addr.pgptr = NULL;
  addr.offset = -1;

  if (fhdr->type == FILE_TMP || fhdr->type == FILE_TMP_TMP)
    {
      special = true;
    }

  allocset = (FILE_ALLOCSET *) ((char *) allocset_pgptr + allocset_offset);

  /* If there is not space to store a new sector identifier at the end of the
     sector table, the sector table for this allocation set is expanded */

  if (allocset->end_sects_offset >= allocset->start_pages_offset
      && VPID_EQ (&allocset->end_sects_vpid, &allocset->start_pages_vpid))
    {
      /* Expand the sector table */
      if (file_allocset_expand_sector (thread_p, fhdr_pgptr, allocset_pgptr,
				       allocset_offset) == NULL)
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
      addr.pgptr = pgbuf_fix (thread_p, &allocset->end_sects_vpid, OLD_PAGE,
			      PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
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
	      /* For now, let us allow to continue the allocation using the
	         special sector. */
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

  /* Allocate the sector and store its identifier onto the table of allocated
     sectors ids for the current allocation set. */

  sectid = ((special == true)
	    ? disk_alloc_special_sector ()
	    : disk_alloc_sector (thread_p, allocset->volid, 1, exp_npages));

  CAST_TO_SECTARRAY (aid_ptr, addr.pgptr, allocset->end_sects_offset);

  /* Log the change to be applied and then update the sector table, and the
     allocation set */

  addr.offset = allocset->end_sects_offset;
  log_append_undoredo_data (thread_p, RVFL_IDSTABLE, &addr, sizeof (*aid_ptr),
			    sizeof (*aid_ptr), aid_ptr, &sectid);

  /* Now update the sector table */
  assert (sectid > NULL_PAGEID);
  *aid_ptr = sectid;

  /* Save the allocation set undo recovery information.
     THEN, update the allocation set */

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
	      || file_ftabvpid_next (fhdr, addr.pgptr, &vpid) != NO_ERROR
	      || VPID_ISNULL (&vpid))
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

  log_append_undoredo_data (thread_p, RVFL_ALLOCSET_SECT, &addr,
			    sizeof (recv_undo), sizeof (recv_redo),
			    &recv_undo, &recv_redo);
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
file_allocset_expand_sector (THREAD_ENTRY * thread_p, PAGE_PTR fhdr_pgptr,
			     PAGE_PTR allocset_pgptr, INT16 allocset_offset)
{
  FILE_HEADER *fhdr;
  FILE_ALLOCSET *allocset;
  PAGE_PTR to_pgptr = NULL;
  PAGE_PTR from_pgptr = NULL;
  INT32 *to_aid_ptr;		/* Pointer to portion of pageid table on
				   to-page */
  INT32 *from_aid_ptr;		/* Pointer to portion of pageid table on
				   from-page */
  INT32 *to_outptr;		/* Out of portion of pageid table on to-page */
  INT32 *from_outptr;		/* Out of portion of pageid table on
				   from-page */
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

  if ((((FILE_NUM_EMPTY_SECTS * SSIZEOF (to_vpid.pageid)) -
	(allocset->num_holes * SSIZEOF (to_vpid.pageid)) +
	allocset->end_pages_offset) >= DB_PAGESIZE)
      && VPID_EQ (&allocset->end_pages_vpid, &fhdr->last_table_vpid))
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
  log_append_undo_data (thread_p, RVFL_ALLOCSET_PAGETB_ADDRESS, &addr,
			sizeof (recv), &recv);

  /* Find the last page where the data will be moved from */
  from_vpid = allocset->end_pages_vpid;
  from_pgptr = pgbuf_fix (thread_p, &from_vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			  PGBUF_UNCONDITIONAL_LATCH);
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
  if (file_find_limits (from_pgptr, allocset, &from_outptr, &from_aid_ptr,
			FILE_PAGE_TABLE) != NO_ERROR)
    {
      pgbuf_unfix_and_init (thread_p, from_pgptr);
      return NULL;
    }

  (void) pgbuf_check_page_ptype (thread_p, from_pgptr, PAGE_FTAB);

  /* Find the page where data will moved to */
  if (((FILE_NUM_EMPTY_SECTS * SSIZEOF (to_vpid.pageid)) -
       (allocset->num_holes * SSIZEOF (to_vpid.pageid)) +
       allocset->end_pages_offset) < DB_PAGESIZE)
    {
      /* Same page as from-page. Fetch the page anyhow, to avoid complicated
         code (i.e., freeing pages) */
      to_vpid = from_vpid;
      to_pgptr = pgbuf_fix (thread_p, &to_vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			    PGBUF_UNCONDITIONAL_LATCH);
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
      allocset->end_pages_offset +=
	(INT16) (nsects * sizeof (to_vpid.pageid));
    }
  else
    {
      /* to-page is the next page pointed by from-page */
      if (file_ftabvpid_next (fhdr, from_pgptr, &to_vpid) != NO_ERROR)
	{
	  pgbuf_unfix_and_init (thread_p, from_pgptr);
	  return NULL;
	}

      to_pgptr = pgbuf_fix (thread_p, &to_vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			    PGBUF_UNCONDITIONAL_LATCH);
      if (to_pgptr == NULL)
	{
	  pgbuf_unfix_and_init (thread_p, from_pgptr);
	  return NULL;
	}

      (void) pgbuf_check_page_ptype (thread_p, to_pgptr, PAGE_FTAB);

      /* Must take in consideration pointers of linked list and empty spaces
         (i.e., NULL_PAGEIDs) of from-page */

      /* How many holes area needed ? */
      nsects = FILE_NUM_EMPTY_SECTS - allocset->num_holes;
      if (nsects < 0)
	{
	  nsects = 0;
	}

      /* Fix the offset of the end of the pageid array */
      allocset->end_pages_vpid = to_vpid;
      allocset->end_pages_offset = (INT16) ((nsects * sizeof (to_vpid.pageid))
					    + sizeof (*chain));
    }

  /*
   * Now start shifting.
   * Note that we are shifting from the right.. so from_outptr is to the left,
   * that is the beginning of portion...
   */

  if (file_find_limits (to_pgptr, allocset, &to_outptr, &to_aid_ptr,
			FILE_PAGE_TABLE) != NO_ERROR)
    {
      pgbuf_unfix_and_init (thread_p, from_pgptr);
      pgbuf_unfix_and_init (thread_p, to_pgptr);
      return NULL;
    }

  /* Before we move anything to the to_page, we need to log whatever is there
     just in case of a crash.. we will need to recover the shift... */

  length = 0;
  addr.pgptr = to_pgptr;
  addr.offset = CAST_BUFLEN ((char *) to_outptr - (char *) to_pgptr);
  log_append_undo_data (thread_p, RVFL_IDSTABLE, &addr,
			CAST_BUFLEN ((char *) to_aid_ptr -
				     (char *) to_outptr), to_outptr);

  while (!(VPID_EQ (&from_vpid, &allocset->end_sects_vpid)
	   && from_outptr >= from_aid_ptr))
    {
      /* Boundary condition on from-page ? */
      if (from_outptr >= from_aid_ptr)
	{
#ifdef FILE_DEBUG
	  if (from_outptr > from_aid_ptr)
	    {
	      er_log_debug (ARG_FILE_LINE, "file_allocset_expand_sector: ***"
			    " Boundary condition system error ***\n");
	    }
#endif /* FILE_DEBUG */
	  /* Use the linked list to find previous page */
	  chain = (FILE_FTAB_CHAIN *) (from_pgptr + FILE_FTAB_CHAIN_OFFSET);
	  from_vpid = chain->prev_ftbvpid;
	  pgbuf_unfix_and_init (thread_p, from_pgptr);

	  from_pgptr = pgbuf_fix (thread_p, &from_vpid, OLD_PAGE,
				  PGBUF_LATCH_WRITE,
				  PGBUF_UNCONDITIONAL_LATCH);
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
	  if (file_find_limits (from_pgptr, allocset, &from_outptr,
				&from_aid_ptr, FILE_PAGE_TABLE) != NO_ERROR)
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
	      er_log_debug (ARG_FILE_LINE, "file_allocset_expand_sector: ***"
			    " Boundary condition system error ***\n");
	    }
#endif /* FILE_DEBUG */
	  if (length != 0)
	    {
	      addr.pgptr = to_pgptr;
	      addr.offset =
		CAST_BUFLEN ((char *) to_aid_ptr - (char *) to_pgptr);
	      log_append_redo_data (thread_p, RVFL_IDSTABLE, &addr, length,
				    to_aid_ptr);
	      length = 0;
	    }

	  /* Use the linked list to find previous page */
	  chain = (FILE_FTAB_CHAIN *) (to_pgptr + FILE_FTAB_CHAIN_OFFSET);
	  to_vpid = chain->prev_ftbvpid;
	  pgbuf_set_dirty (thread_p, to_pgptr, FREE);

	  to_pgptr = pgbuf_fix (thread_p, &to_vpid, OLD_PAGE,
				PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
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
	  if (file_find_limits (to_pgptr, allocset, &to_outptr, &to_aid_ptr,
				FILE_PAGE_TABLE) != NO_ERROR)
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
	  log_append_undo_data (thread_p, RVFL_IDSTABLE, &addr,
				CAST_BUFLEN ((char *) to_aid_ptr -
					     (char *) to_outptr), to_outptr);
	}

      /* Move as much as possible until a boundary condition is reached */
      while (to_outptr < to_aid_ptr && from_outptr < from_aid_ptr)
	{
	  from_aid_ptr--;
	  /* can we compact this entry ?.. if from_aid_ptr is NULL_PAGEID,
	     the netry is compacted */
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
      log_append_redo_data (thread_p, RVFL_IDSTABLE, &addr, length,
			    to_aid_ptr);
    }

  if ((allocset->start_pages_offset ==
       ((char *) to_aid_ptr - (char *) to_pgptr))
      && VPID_EQ (&allocset->start_pages_vpid, &to_vpid))
    {
      /* The file may be corrupted... */
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_FILE_TABLE_CORRUPTED,
	      2, fhdr->vfid.volid, fhdr->vfid.fileid);

      pgbuf_set_dirty (thread_p, to_pgptr, FREE);
      to_pgptr = NULL;
      /* Try to fix the file if the problem is related to number of holes */
      if (allocset->num_holes != 0)
	{
	  /* Give another try after number of holes is set to null */
	  allocset->num_holes = 0;
	  return file_allocset_expand_sector (thread_p, fhdr_pgptr,
					      allocset_pgptr,
					      allocset_offset);
	}
      return NULL;
    }


  allocset->start_pages_vpid = to_vpid;
  allocset->start_pages_offset =
    CAST_BUFLEN ((char *) to_aid_ptr - (char *) to_pgptr);
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
  log_append_redo_data (thread_p, RVFL_ALLOCSET_PAGETB_ADDRESS, &addr,
			sizeof (recv), &recv);

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
  if (file_ftabvpid_alloc (thread_p, fhdr->last_table_vpid.volid,
			   fhdr->last_table_vpid.pageid, &new_ftb_vpid, 1,
			   fhdr->type, FILE_EXPAND_FILE_TABLE) != NO_ERROR)
    {
      return NULL;
    }

  /* Set allocated page as last file table page */
  addr.pgptr = pgbuf_fix (thread_p, &new_ftb_vpid, NEW_PAGE,
			  PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
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
  log_append_redo_data (thread_p, RVFL_FTAB_CHAIN, &addr, sizeof (*chain),
			chain);
  pgbuf_set_dirty (thread_p, addr.pgptr, FREE);
  addr.pgptr = NULL;

  if (!VPID_ISNULL (&fhdr->next_table_vpid))
    {
      /*
       * The previous last page cannot be the file header page.
       * Find the previous last page and modify its chain pointers according to
       * changes
       */
      addr.pgptr = pgbuf_fix (thread_p, &prev_last_ftb_vpid, OLD_PAGE,
			      PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
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
      log_append_undoredo_data (thread_p, RVFL_FTAB_CHAIN, &addr,
				sizeof (*chain), sizeof (*chain),
				&rv_undo_chain, chain);
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
  log_append_undoredo_data (thread_p, RVFL_FHDR_FTB_EXPANSION, &addr,
			    sizeof (recv_undo), sizeof (recv_redo),
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
file_debug_maybe_newset (PAGE_PTR fhdr_pgptr, FILE_HEADER * fhdr,
			 INT32 npages)
{
  FILE_ALLOCSET *allocset;	/* Pointer to an allocation set   */
  PAGE_PTR allocset_pgptr = NULL;	/* Page pointer to allocation set
					 * description
					 */

  if (fhdr->type == FILE_TMP || fhdr->type == FILE_TMP_TMP)
    {
      return;
    }

  /*
   ************************************************************
   * For easy debugging of multi volume in debugging mode
   * create a new allocation set every multiple set of pages
   ************************************************************
   */

  if (file_Debug_newallocset_multiple_of < 0
      && xboot_find_number_permanent_volumes () > 1)
    {
      file_Debug_newallocset_multiple_of =
	-file_Debug_newallocset_multiple_of;
    }

  if (file_Debug_newallocset_multiple_of > 0)
    {
      allocset_pgptr = pgbuf_fix (thread_p, &fhdr->last_allocset_vpid,
				  OLD_PAGE, PGBUF_LATCH_WRITE,
				  PGBUF_UNCONDITIONAL_LATCH);
      if (allocset_pgptr != NULL)
	{
	  (void) pgbuf_check_page_ptype (thread_p, allocset_pgptr, PAGE_FTAB);

	  allocset = (FILE_ALLOCSET *) ((char *) allocset_pgptr +
					fhdr->last_allocset_offset);
	  if (allocset->num_pages > 0
	      && ((allocset->num_pages % file_Debug_newallocset_multiple_of)
		  == 0))
	    {
	      /*
	       * Just for the fun of it, declare that there are not more pages
	       * in this allocation set (i.e., volume)
	       */
	      (void) file_allocset_new_set (allocset->volid, fhdr_pgptr,
					    npages, DISK_NONCONTIGUOUS_PAGES);
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
file_allocset_new_set (THREAD_ENTRY * thread_p, INT16 last_allocset_volid,
		       PAGE_PTR fhdr_pgptr, int exp_numpages,
		       DISK_SETPAGE_TYPE setpage_type)
{
  FILE_HEADER *fhdr;
  INT16 volid;
  int ret = NO_ERROR;

  (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);

  fhdr = (FILE_HEADER *) (fhdr_pgptr + FILE_HEADER_OFFSET);

  /* Find a volume with at least the expected number of pages. */
  volid = file_find_goodvol (thread_p, NULL_VOLID, last_allocset_volid,
			     exp_numpages, setpage_type, fhdr->type);
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
file_allocset_add_set (THREAD_ENTRY * thread_p, PAGE_PTR fhdr_pgptr,
		       INT16 volid)
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

  allocset_pgptr = pgbuf_fix (thread_p, &fhdr->last_allocset_vpid, OLD_PAGE,
			      PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (allocset_pgptr == NULL)
    {
      goto exit_on_error;
    }

  (void) pgbuf_check_page_ptype (thread_p, allocset_pgptr, PAGE_FTAB);

  allocset = (FILE_ALLOCSET *) ((char *) allocset_pgptr +
				fhdr->last_allocset_offset);

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
  new_allocset_pgptr = pgbuf_fix (thread_p, &new_allocset_vpid, OLD_PAGE,
				  PGBUF_LATCH_WRITE,
				  PGBUF_UNCONDITIONAL_LATCH);
  if (new_allocset_pgptr == NULL)
    {
      goto exit_on_error;
    }

  (void) pgbuf_check_page_ptype (thread_p, new_allocset_pgptr, PAGE_FTAB);

  new_allocset = (FILE_ALLOCSET *) ((char *) new_allocset_pgptr +
				    new_allocset_offset);
  new_allocset->volid = volid;
  new_allocset->num_sects = 0;
  new_allocset->num_pages = 0;
  new_allocset->curr_sectid = NULL_SECTID;
  new_allocset->num_holes = 0;

  /* Calculate positions for the sector table. The sector table is located
     immediately after the allocation set */

  if ((new_allocset_offset + SSIZEOF (*new_allocset)) < DB_PAGESIZE)
    {
      new_allocset->start_sects_vpid = new_allocset_vpid;
      new_allocset->start_sects_offset = (new_allocset_offset +
					  sizeof (*new_allocset));
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

  /* Calculate positions for the page table. The page table is located
     immediately after the sector table. */

  new_allocset->start_pages_vpid = new_allocset->end_sects_vpid;
  new_allocset->start_pages_offset =
    new_allocset->end_sects_offset + (FILE_NUM_EMPTY_SECTS * sizeof (INT32));

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
  log_append_redo_data (thread_p, RVFL_ALLOCSET_NEW, &addr,
			sizeof (*new_allocset), new_allocset);

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
  log_append_undoredo_data (thread_p, RVFL_ALLOCSET_LINK, &addr,
			    sizeof (recv_undo), sizeof (recv_redo),
			    &recv_undo, &recv_redo);

  pgbuf_set_dirty (thread_p, allocset_pgptr, FREE);
  allocset_pgptr = NULL;

  /* Now update the file header to indicate that this is the last allocated
     set */

  /* Save the current values for undo purposes */
  recv_undo.next_allocset_vpid = fhdr->last_allocset_vpid;
  recv_undo.next_allocset_offset = fhdr->last_allocset_offset;

  fhdr->last_allocset_vpid = new_allocset_vpid;
  fhdr->last_allocset_offset = new_allocset_offset;
  fhdr->num_allocsets += 1;

  addr.pgptr = fhdr_pgptr;
  addr.offset = FILE_HEADER_OFFSET;
  log_append_undoredo_data (thread_p, RVFL_FHDR_ADD_LAST_ALLOCSET, &addr,
			    sizeof (recv_undo), sizeof (recv_redo),
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
file_allocset_add_pageids (THREAD_ENTRY * thread_p, PAGE_PTR fhdr_pgptr,
			   PAGE_PTR allocset_pgptr, INT16 allocset_offset,
			   INT32 alloc_pageid, INT32 npages,
			   FILE_ALLOC_VPIDS * alloc_vpids)
{
  FILE_HEADER *fhdr;
  FILE_ALLOCSET *allocset;
  VPID vpid;
  PAGE_PTR pgptr = NULL;
  INT32 *aid_ptr;		/* Pointer to a portion of allocated
				   pageids table of given allocation set */
  INT32 *outptr;		/* Pointer to outside of portion of
				   allocated pageids */
  LOG_DATA_ADDR addr;
  char *logdata;
  FILE_RECV_ALLOCSET_PAGES recv_undo;
  FILE_RECV_ALLOCSET_PAGES recv_redo;
  int i, length;

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
  pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
		     PGBUF_UNCONDITIONAL_LATCH);
  if (pgptr == NULL)
    {
      return NULL_PAGEID;
    }

  (void) pgbuf_check_page_ptype (thread_p, pgptr, PAGE_FTAB);

  /* Address end of page table... and the end of page */
  CAST_TO_PAGEARRAY (aid_ptr, pgptr, allocset->end_pages_offset);
  CAST_TO_PAGEARRAY (outptr, pgptr, DB_PAGESIZE);

  /* Note that we do not undo any information on page table since it is fixed
     by undoing the allocation set pointer */

  addr.pgptr = pgptr;
  addr.offset = allocset->end_pages_offset;
  logdata = (char *) aid_ptr;

  /* First log the before images that are planned to be changed on
     current page */

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
	      log_append_redo_data (thread_p, RVFL_IDSTABLE, &addr,
				    CAST_BUFLEN ((char *) aid_ptr - logdata),
				    logdata);
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
	      if (file_expand_ftab (thread_p, fhdr_pgptr) == NULL
		  || file_ftabvpid_next (fhdr, pgptr, &vpid) != NO_ERROR
		  || VPID_ISNULL (&vpid))
		{
		  if (er_errid () == NO_ERROR)
		    {
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			      ER_FILE_TABLE_CORRUPTED, 2, fhdr->vfid.volid,
			      fhdr->vfid.fileid);
		    }

		  pgbuf_unfix_and_init (thread_p, pgptr);
		  return NULL_PAGEID;
		}
	    }

	  /* Free this page and get the new page */
	  pgbuf_unfix_and_init (thread_p, pgptr);
	  pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			     PGBUF_UNCONDITIONAL_LATCH);
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


	  /* First log the before images that are planned to be changed on
	     current page */

	  if (outptr > aid_ptr)
	    {
	      length = (int) ((npages > (outptr - aid_ptr)) ?
			      (outptr - aid_ptr) : (npages - i));
	      log_append_undo_data (thread_p, RVFL_IDSTABLE, &addr,
				    sizeof (npages) * length, logdata);
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
  log_append_redo_data (thread_p, RVFL_IDSTABLE, &addr,
			CAST_BUFLEN ((char *) aid_ptr - logdata), logdata);

  /* Update the allocation set for end of page table and number of pages.
   * Similarly, the file header must be updated. Need to log the info... */

  /* SAVE the information that is going to be change for UNDO purposes */
  recv_undo.curr_sectid = allocset->curr_sectid;
  recv_undo.end_pages_vpid = allocset->end_pages_vpid;
  recv_undo.end_pages_offset = allocset->end_pages_offset;
  recv_undo.num_pages = allocset->num_pages;
  recv_undo.num_holes = allocset->num_holes;

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
  recv_redo.num_pages = allocset->num_pages;
  recv_redo.num_holes = allocset->num_holes;

  /* Now log the changes to the allocation set and set the page where the
     allocation set resides as dirty */

  addr.pgptr = allocset_pgptr;
  addr.offset = allocset_offset;
  log_append_undoredo_data (thread_p, RVFL_ALLOCSET_ADD_PAGES, &addr,
			    sizeof (recv_undo), sizeof (recv_redo),
			    &recv_undo, &recv_redo);

  pgbuf_set_dirty (thread_p, allocset_pgptr, DONT_FREE);

  /* Chnage the file header and log the change */

  npages += fhdr->num_user_pages;	/* Total pages.. the redo */
  addr.pgptr = fhdr_pgptr;
  addr.offset = FILE_HEADER_OFFSET;

  log_append_undoredo_data (thread_p, RVFL_FHDR_ADD_PAGES, &addr,
			    sizeof (fhdr->num_user_pages),
			    sizeof (fhdr->num_user_pages),
			    &fhdr->num_user_pages, &npages);

  fhdr->num_user_pages = npages;
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
file_allocset_alloc_pages (THREAD_ENTRY * thread_p, PAGE_PTR fhdr_pgptr,
			   VPID * allocset_vpid, INT16 allocset_offset,
			   VPID * first_new_vpid, INT32 npages,
			   const VPID * near_vpid,
			   FILE_ALLOC_VPIDS * alloc_vpids)
{
  FILE_HEADER *fhdr;
  FILE_ALLOCSET *allocset;
  PAGE_PTR allocset_pgptr = NULL;
  INT32 near_pageid;		/* Try to allocate the pages near to this
				   page */
  INT32 sectid;			/* Allocate the pages on this sector */
  INT32 *aid_ptr;		/* Pointer to a portion of allocated
				   pageids table of given allocation set */
  INT32 *outptr;		/* Pointer to outside of portion of
				   allocated pageids */
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
  allocset_pgptr = pgbuf_fix (thread_p, allocset_vpid, OLD_PAGE,
			      PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (allocset_pgptr == NULL)
    {
      goto exit_on_error;
    }

  (void) pgbuf_check_page_ptype (thread_p, allocset_pgptr, PAGE_FTAB);

  /* Get the file header and the allocation set */
  fhdr = (FILE_HEADER *) (fhdr_pgptr + FILE_HEADER_OFFSET);

  allocset = (FILE_ALLOCSET *) ((char *) allocset_pgptr + allocset_offset);

  /* Check if the near page is of any use */
  near_pageid = ((near_vpid != NULL && allocset->volid == near_vpid->volid)
		 ? near_vpid->pageid : NULL_PAGEID);

  /* Try to allocate the pages from the last used sector identifier. */
  sectid = allocset->curr_sectid;
  if (sectid == NULL_SECTID)
    {
      sectid = file_allocset_alloc_sector (thread_p, fhdr_pgptr,
					   allocset_pgptr, allocset_offset,
					   npages);
    }

  /*
   * If we cannot allocate the pages within this sector, we must check all
   * the previous allocated sectors. However, we do this only when we think
   * there are enough free pages on such sectors.
   */
  alloc_page_type = file_get_disk_page_type (fhdr->type);

  alloc_pageid = ((sectid != NULL_SECTID)
		  ? disk_alloc_page (thread_p, allocset->volid, sectid,
				     npages,
				     near_pageid, true, alloc_page_type) :
		  DISK_NULL_PAGEID_WITH_ENOUGH_DISK_PAGES);

  if (alloc_pageid == DISK_NULL_PAGEID_WITH_ENOUGH_DISK_PAGES
      && sectid != DISK_SECTOR_WITH_ALL_PAGES
      && (fhdr->type == FILE_BTREE || fhdr->type == FILE_BTREE_OVERFLOW_KEY)
      && ((allocset->num_pages + npages) <= ((allocset->num_sects - 1) *
					     DISK_SECTOR_NPAGES)))
    {
      vpid = allocset->start_sects_vpid;

      /* Go sector by sector until the number of desired pages are found */

      while (!VPID_ISNULL (&vpid))
	{
	  pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			     PGBUF_UNCONDITIONAL_LATCH);
	  if (pgptr == NULL)
	    {
	      /* Unable to fetch file table page. Free the file header page and
	         release its lock */
	      goto exit_on_error;
	    }

	  (void) pgbuf_check_page_ptype (thread_p, pgptr, PAGE_FTAB);

	  /* Find the beginning and the end of the portion of the sector table
	     stored on the current file table page */

	  if (file_find_limits (pgptr, allocset, &aid_ptr, &outptr,
				FILE_SECTOR_TABLE) != NO_ERROR)
	    {
	      goto exit_on_error;
	    }

	  for (; alloc_pageid == DISK_NULL_PAGEID_WITH_ENOUGH_DISK_PAGES
	       && aid_ptr < outptr; aid_ptr++)
	    {
	      sectid = *aid_ptr;
	      alloc_pageid = disk_alloc_page (thread_p, allocset->volid,
					      sectid, npages, near_pageid,
					      true, alloc_page_type);
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

  /* If we were unable to allocate the pages, and there is not any
     unexpected error, allocate a new sector */
  while (alloc_pageid == DISK_NULL_PAGEID_WITH_ENOUGH_DISK_PAGES
	 && sectid != DISK_SECTOR_WITH_ALL_PAGES)
    {
      /* Allocate a new sector for the file */
      sectid = file_allocset_alloc_sector (thread_p, fhdr_pgptr,
					   allocset_pgptr, allocset_offset,
					   npages);
      if (sectid == NULL_SECTID)
	{
	  /* Unable to allocate a new sector for file */

	  /* Free the file header page and release its lock */
	  goto exit_on_error;
	}

      alloc_pageid = disk_alloc_page (thread_p, allocset->volid, sectid,
				      npages, near_pageid, true,
				      alloc_page_type);
#if !defined (NDEBUG)
      if (++retry > 1)
	{
	  er_log_debug (ARG_FILE_LINE,
			"file_allocset_alloc_pages: retry = %d, filetype= %s, "
			"volid = %d, secid = %d, pageid = %d, npages = %d\n",
			retry, file_type_to_string (fhdr->type),
			allocset->volid, sectid, alloc_pageid, npages);
	}
#endif
    }

  /* Store the page identifiers into the array of pageids */

  if (alloc_pageid != NULL_PAGEID
      && alloc_pageid != DISK_NULL_PAGEID_WITH_ENOUGH_DISK_PAGES)
    {
      /* Add the pageids entries */
      if (file_allocset_add_pageids (thread_p, fhdr_pgptr, allocset_pgptr,
				     allocset_offset, alloc_pageid,
				     npages, alloc_vpids) == NULL_PAGEID)
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

  /* In the case of temporary file on permanent volumes (i.e., FILE_TMP), set
     all the pages as temporary to avoid logging for temporary files. */

  if (fhdr->type == FILE_TMP && alloc_pageid != NULL_PAGEID)
    {
      if (file_reset_contiguous_temporary_pages (thread_p,
						 first_new_vpid->volid,
						 first_new_vpid->pageid,
						 npages, true) != NO_ERROR)
	{
	  alloc_pageid = NULL_PAGEID;
	  answer = FILE_ALLOCSET_ALLOC_ERROR;
	}
    }

  mnt_file_page_allocs (thread_p, npages);

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
file_alloc_pages (THREAD_ENTRY * thread_p, const VFID * vfid,
		  VPID * first_alloc_vpid, INT32 npages,
		  const VPID * near_vpid,
		  bool (*fun) (THREAD_ENTRY * thread_p, const VFID * vfid,
			       const FILE_TYPE file_type,
			       const VPID * first_alloc_vpid, INT32 npages,
			       void *args), void *args)
{
  FILE_HEADER *fhdr;
  PAGE_PTR fhdr_pgptr = NULL;
  VPID vpid;
  int allocstate;
  FILE_TYPE file_type;
  FILE_IS_NEW_FILE isfile_new;
  bool old_val = false;
  bool restore_check_interrupt = false;

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
      /* This is a system error. Trying to allocate zero or a negative number
         of pages */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FILE_ALLOC_NOPAGES, 1,
	      npages);
      goto exit_on_error;
    }

  /* Lock the file header page in exclusive mode and then fetch the page. */
  vpid.volid = vfid->volid;
  vpid.pageid = vfid->fileid;
  fhdr_pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			  PGBUF_UNCONDITIONAL_LATCH);
  if (fhdr_pgptr == NULL)
    {
      goto exit_on_error;
    }

  (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);

  fhdr = (FILE_HEADER *) (fhdr_pgptr + FILE_HEADER_OFFSET);

  file_type = fhdr->type;

  if (fhdr->type == FILE_TMP || fhdr->type == FILE_TMP_TMP)
    {
      old_val = thread_set_check_interrupt (thread_p, false);
      restore_check_interrupt = true;
    }

#ifdef FILE_DEBUG
  /* FOR EASY DEBUGGING OF MULTI VOLUME IN DEBUGGING MODE
     CREATE A NEW ALLOCATION SET EVERY MULTIPLE SET OF PAGES */
  file_debug_maybe_newset (fhdr_pgptr, fhdr, npages);
#endif /* FILE_DEBUG */

  /*
   * Find last allocation set to allocate the new pages. If we cannot allocate
   * the new pages into this allocation set, a new allocation set is created.
   * The new allocation set is created into the volume with more free pages.
   */

  while (((allocstate = file_allocset_alloc_pages (thread_p, fhdr_pgptr,
						   &fhdr->last_allocset_vpid,
						   fhdr->last_allocset_offset,
						   first_alloc_vpid, npages,
						   near_vpid, NULL))
	  == FILE_ALLOCSET_ALLOC_ZERO)
	 && first_alloc_vpid->volid != NULL_VOLID)
    {
      /*
       * Unable to create the pages at this volume
       * Create a new allocation set and try to allocate the pages from there
       * If first_alloc_vpid->volid == NULL_VOLID, there was an error...
       */
      if (file_allocset_new_set (thread_p, first_alloc_vpid->volid,
				 fhdr_pgptr, npages,
				 DISK_CONTIGUOUS_PAGES) != NO_ERROR)
	{
	  break;
	}

      /* We need to execute the loop due that disk space can be allocated
         by several transactions */
    }

  if (allocstate != FILE_ALLOCSET_ALLOC_NPAGES)
    {
      if (allocstate == FILE_ALLOCSET_ALLOC_ZERO
	  && er_errid () != ER_INTERRUPTED)
	{
	  if (fhdr->type == FILE_TMP || fhdr->type == FILE_TMP_TMP)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_FILE_NOT_ENOUGH_PAGES_IN_VOLUME, 2,
		      fileio_get_volume_label (vfid->volid, PEEK), npages);
	    }
	  else
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_FILE_NOT_ENOUGH_PAGES_IN_DATABASE, 1, npages);
	    }
	}
      goto exit_on_error;
    }

  if (fun != NULL
      && (*fun) (thread_p, vfid, file_type, first_alloc_vpid, npages, args)
      == false)
    {
      /* We must abort the allocation of the page since the user function
         failed */
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

  if (isfile_new == FILE_NEW_FILE
      && file_type != FILE_TMP && file_type != FILE_TMP_TMP
      && logtb_is_current_active (thread_p) == true)
    {
      log_end_system_op (thread_p, LOG_RESULT_TOPOP_ATTACH_TO_OUTER);
    }
  else
    {
      log_end_system_op (thread_p, LOG_RESULT_TOPOP_COMMIT);
    }

  if (restore_check_interrupt == true)
    {
      thread_set_check_interrupt (thread_p, old_val);
    }

  return first_alloc_vpid;

exit_on_error:

  (void) log_end_system_op (thread_p, LOG_RESULT_TOPOP_ABORT);

  if (fhdr_pgptr)
    {
      pgbuf_unfix_and_init (thread_p, fhdr_pgptr);
    }

  VPID_SET_NULL (first_alloc_vpid);

  if (restore_check_interrupt == true)
    {
      thread_set_check_interrupt (thread_p, old_val);
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
file_alloc_pages_as_noncontiguous (THREAD_ENTRY * thread_p,
				   const VFID * vfid,
				   VPID * first_alloc_vpid,
				   INT32 * first_alloc_nthpage,
				   INT32 npages, const VPID * near_vpid,
				   bool (*fun) (THREAD_ENTRY * thread_p,
						const VFID * vfid,
						const FILE_TYPE file_type,
						const VPID * first_alloc_vpid,
						const INT32 *
						first_alloc_nthpage,
						INT32 npages, void *args),
				   void *args, FILE_ALLOC_VPIDS * alloc_vpids)
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
      /* This is a system error. Trying to allocate zero or a negative number of
         pages */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FILE_ALLOC_NOPAGES, 1,
	      npages);
      goto exit_on_error;
    }

  vpid.volid = vfid->volid;
  vpid.pageid = vfid->fileid;

  fhdr_pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			  PGBUF_UNCONDITIONAL_LATCH);
  if (fhdr_pgptr == NULL)
    {
      goto exit_on_error;
    }

  (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);

  fhdr = (FILE_HEADER *) (fhdr_pgptr + FILE_HEADER_OFFSET);

  file_type = fhdr->type;

  if (file_type == FILE_TMP || file_type == FILE_TMP_TMP)
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

  is_tmp_file = ((file_type == FILE_TMP || file_type == FILE_TMP_TMP
		  || file_type == FILE_EITHER_TMP
		  || file_type == FILE_QUERY_AREA) ? true : false);

  while (npages > 0)
    {
      /*
       * The non temporary files has more than DISK_SECTOR_NPAGES are allocated
       * among sectors.
       * We don't use DISK_SECTOR_WITH_ALL_PAGES way for these files any more.
       */
      allocate_npages = (is_tmp_file ? npages
			 : MIN (npages, DISK_SECTOR_NPAGES));

#ifdef FILE_DEBUG
      /* FOR EASY DEBUGGING OF MULTI VOLUME IN DEBUGGING MODE
         CREATE A NEW ALLOCATION SET EVERY MULTIPLE SET OF PAGES */
      file_debug_maybe_newset (fhdr_pgptr, fhdr, npages);
#endif /* FILE_DEBUG */

      while (((allocstate =
	       file_allocset_alloc_pages (thread_p, fhdr_pgptr,
					  &fhdr->last_allocset_vpid,
					  fhdr->last_allocset_offset, &vpid,
					  allocate_npages, near_vpid,
					  alloc_vpids)) ==
	      FILE_ALLOCSET_ALLOC_ZERO) && vpid.volid != NULL_VOLID)
	{
	  /* guess a simple overhead for storing page identifiers. This may not
	     be very accurate, however, it is OK for more practical purposes */

	  ftb_npages = CEIL_PTVDIV (npages * sizeof (INT32), DB_PAGESIZE);

	  /*
	   * We were unable to allocate the desired number of pages.
	   * Make sure that we have enough non contiguous pages for the file,
	   * before we continue.
	   */

	  max_npages = (file_find_good_maxpages (thread_p, file_type)
			- ftb_npages);

	  /* Do we have enough pages */
	  if (max_npages < npages)
	    {
	      break;
	    }

	  /* Get whatever we can from the current volume, before we try another
	     volume */

	  max_npages =
	    disk_get_maxcontiguous_numpages (thread_p, vpid.volid, npages);

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
	      if (file_allocset_new_set (thread_p, vpid.volid, fhdr_pgptr,
					 npages,
					 DISK_NONCONTIGUOUS_SPANVOLS_PAGES)
		  != NO_ERROR)
		{
		  break;
		}
	    }

	  /* We need to execute the loop due that disk space can be allocated
	     by several transactions */
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
      if (allocstate == FILE_ALLOCSET_ALLOC_ZERO
	  && er_errid () != ER_INTERRUPTED)
	{
	  if (fhdr->type == FILE_TMP || fhdr->type == FILE_TMP_TMP)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_FILE_NOT_ENOUGH_PAGES_IN_VOLUME, 2,
		      fileio_get_volume_label (vfid->volid, PEEK), npages);
	    }
	  else
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_FILE_NOT_ENOUGH_PAGES_IN_DATABASE, 1, npages);
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
      && (*fun) (thread_p, vfid, file_type, first_alloc_vpid,
		 first_alloc_nthpage, requested_npages, args) == false)
    {
      /* We must abort the allocation of the page since the user function
         failed  */
      goto exit_on_error;
    }

  pgbuf_unfix_and_init (thread_p, fhdr_pgptr);

  isfile_new = file_is_new_file (thread_p, vfid);
  if (isfile_new == FILE_ERROR)
    {
      goto exit_on_error;
    }

  if (isfile_new == FILE_NEW_FILE
      && file_type != FILE_TMP && file_type != FILE_TMP_TMP
      && logtb_is_current_active (thread_p) == true)
    {
      log_end_system_op (thread_p, LOG_RESULT_TOPOP_ATTACH_TO_OUTER);
    }
  else
    {
      log_end_system_op (thread_p, LOG_RESULT_TOPOP_COMMIT);
    }

  if (restore_check_interrupt == true)
    {
      thread_set_check_interrupt (thread_p, old_val);
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
      thread_set_check_interrupt (thread_p, old_val);
    }

  return NULL;
}

/*
 * file_alloc_pages_at_volid () - Allocate user pages at specific volume
 *                              identifier
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
file_alloc_pages_at_volid (THREAD_ENTRY * thread_p, const VFID * vfid,
			   VPID * first_alloc_vpid, INT32 npages,
			   const VPID * near_vpid, INT16 desired_volid,
			   bool (*fun) (const VFID * vfid,
					const FILE_TYPE file_type,
					const VPID * first_alloc_vpid,
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

  if (npages <= 0 || desired_volid == NULL_VOLID ||
      fileio_get_volume_descriptor (desired_volid) == NULL_VOLDES)
    {
      /* This is a system error. Trying to allocate zero or a negative number of
         pages */
      if (npages <= 0)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FILE_ALLOC_NOPAGES, 1,
		  npages);
	}
      else
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_FILE_UNKNOWN_VOLID, 1, desired_volid);
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

  fhdr_pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			  PGBUF_UNCONDITIONAL_LATCH);
  if (fhdr_pgptr == NULL)
    {
      goto exit_on_error;
    }

  (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);

  fhdr = (FILE_HEADER *) (fhdr_pgptr + FILE_HEADER_OFFSET);

  file_type = fhdr->type;

  if (file_type == FILE_TMP || file_type == FILE_TMP_TMP)
    {
      old_val = thread_set_check_interrupt (thread_p, false);
      restore_check_interrupt = true;
    }

  /* Must allocate from the desired volume
     make sure that the last allocation set contains the desired volume */
  allocset_pgptr = pgbuf_fix (thread_p, &fhdr->last_allocset_vpid, OLD_PAGE,
			      PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (allocset_pgptr == NULL)
    {
      goto exit_on_error;
    }

  (void) pgbuf_check_page_ptype (thread_p, allocset_pgptr, PAGE_FTAB);

  allocset = (FILE_ALLOCSET *) ((char *) allocset_pgptr +
				fhdr->last_allocset_offset);
  if (allocset->volid != desired_volid
      && file_allocset_add_set (thread_p, fhdr_pgptr,
				desired_volid) != NO_ERROR)
    {
      goto exit_on_error;
    }

  pgbuf_unfix_and_init (thread_p, allocset_pgptr);

  /*
   * Find last allocation set to allocate the new pages. If we cannot allocate
   * the new pages into this allocation set, we must return since need to
   * allocate the pages in the desired volid.
   */

  allocstate = file_allocset_alloc_pages (thread_p, fhdr_pgptr,
					  &fhdr->last_allocset_vpid,
					  fhdr->last_allocset_offset,
					  first_alloc_vpid, npages,
					  near_vpid, NULL);
  if (allocstate == FILE_ALLOCSET_ALLOC_ERROR)
    {
      /* Unable to create the pages at this volume */
      goto exit_on_error;
    }

  if (allocstate == FILE_ALLOCSET_ALLOC_ZERO)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_FILE_NOT_ENOUGH_PAGES_IN_VOLUME, 2,
	      fileio_get_volume_label (desired_volid, PEEK), npages);
      goto exit_on_error;
    }

  if (fun != NULL &&
      (*fun) (vfid, file_type, first_alloc_vpid, npages, args) == false)
    {
      /* We must abort the allocation of the page since the user function
         failed */
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

  if (isfile_new == FILE_NEW_FILE
      && file_type != FILE_TMP && file_type != FILE_TMP_TMP
      && logtb_is_current_active (thread_p) == true)
    {
      log_end_system_op (thread_p, LOG_RESULT_TOPOP_ATTACH_TO_OUTER);
    }
  else
    {
      log_end_system_op (thread_p, LOG_RESULT_TOPOP_COMMIT);
    }

  if (restore_check_interrupt == true)
    {
      thread_set_check_interrupt (thread_p, old_val);
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
      thread_set_check_interrupt (thread_p, old_val);
    }

  return NULL;
}

/*
 * file_find_maxpages_allocable () - Find the likely maximum number of pages that
 *                  number of pages
 *   vfid(in): Complete file identifier
 *
 * Note: Automatic volume extensions are taken in considerations.
 *       These should be takes as a hint due to other concurrent allocations .
 */
INT32
file_find_maxpages_allocable (THREAD_ENTRY * thread_p, const VFID * vfid)
{
  FILE_TYPE file_type;
  INT32 num_pages;

  file_type = file_get_type (thread_p, vfid);
  if (file_type == FILE_UNKNOWN_TYPE)
    {
      return -1;
    }

  num_pages = file_find_good_maxpages (thread_p, file_type);
  num_pages -= file_guess_numpages_overhead (thread_p, vfid, num_pages);
  return num_pages;

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
file_allocset_find_page (THREAD_ENTRY * thread_p, PAGE_PTR fhdr_pgptr,
			 PAGE_PTR allocset_pgptr, INT16 allocset_offset,
			 INT32 pageid, int (*fun) (THREAD_ENTRY * thread_p,
						   FILE_HEADER * fhdr,
						   FILE_ALLOCSET * allocset,
						   PAGE_PTR fhdr_pgptr,
						   PAGE_PTR allocset_pgptr,
						   INT16 allocset_offset,
						   PAGE_PTR pgptr,
						   INT32 * aid_ptr,
						   INT32 pageid,
						   void *args), void *args)
{
  FILE_HEADER *fhdr;
  FILE_ALLOCSET *allocset;
  INT32 *aid_ptr;		/* Pointer to a portion of allocated
				   pageids table of given allocation set */
  INT32 *outptr;		/* Pointer to outside of portion of
				   allocated pageids */
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
      pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			 PGBUF_UNCONDITIONAL_LATCH);
      if (pgptr == NULL)
	{
	  goto exit_on_error;
	}

      (void) pgbuf_check_page_ptype (thread_p, pgptr, PAGE_FTAB);

      /* Calculate the portion of the set that we can look at */
      if (file_find_limits (pgptr, allocset, &aid_ptr, &outptr,
			    FILE_PAGE_TABLE) != NO_ERROR)
	{
	  goto exit_on_error;
	}

      /* We need to do a sequential scan since we do not know where the page is
         stored. The pages are stored by the allocation order */

      for (; aid_ptr < outptr; aid_ptr++)
	{
	  if (pageid == *aid_ptr)
	    {
	      isfound = DISK_VALID;
	      if (fun)
		{
		  if ((*fun) (thread_p, fhdr, allocset, fhdr_pgptr,
			      allocset_pgptr, allocset_offset, pgptr,
			      aid_ptr, pageid, args) != NO_ERROR)
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
file_allocset_dealloc_page (THREAD_ENTRY * thread_p, FILE_HEADER * fhdr,
			    FILE_ALLOCSET * allocset, PAGE_PTR fhdr_pgptr,
			    PAGE_PTR allocset_pgptr, INT16 allocset_offset,
			    PAGE_PTR pgptr, INT32 * aid_ptr,
			    INT32 ignore_pageid, void *args)
{
  int *ret;

  ret = (int *) args;

  *ret = file_allocset_dealloc_contiguous_pages (thread_p, fhdr, allocset,
						 fhdr_pgptr, allocset_pgptr,
						 allocset_offset, pgptr,
						 aid_ptr, 1);
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
file_allocset_dealloc_contiguous_pages (THREAD_ENTRY * thread_p,
					FILE_HEADER * fhdr,
					FILE_ALLOCSET * allocset,
					PAGE_PTR fhdr_pgptr,
					PAGE_PTR allocset_pgptr,
					INT16 allocset_offset,
					PAGE_PTR pgptr,
					INT32 * first_aid_ptr,
					INT32 ncont_page_entries)
{
  bool old_val;
  int ret = NO_ERROR;
  DISK_PAGE_TYPE page_type;

  if (log_start_system_op (thread_p) == NULL)
    {
      return ER_FAILED;
    }

  old_val = thread_set_check_interrupt (thread_p, false);

  /* In the case of temporary file on permanent volumes
     (i.e., FILE_TMP), set all the pages as permanent */

  if (fhdr->type == FILE_TMP)
    {
      ret = file_reset_contiguous_temporary_pages (thread_p, allocset->volid,
						   *first_aid_ptr,
						   ncont_page_entries, false);
    }

  if (ret == NO_ERROR)
    {
      page_type = file_get_disk_page_type (fhdr->type);
      ret = disk_dealloc_page (thread_p, allocset->volid, *first_aid_ptr,
			       ncont_page_entries, page_type);
    }

  if (ret == NO_ERROR)
    {
      ret =
	file_allocset_remove_contiguous_pages (thread_p, fhdr, allocset,
					       fhdr_pgptr, allocset_pgptr,
					       allocset_offset, pgptr,
					       first_aid_ptr,
					       ncont_page_entries);
    }

  if (ret == NO_ERROR)
    {
      log_end_system_op (thread_p, LOG_RESULT_TOPOP_ATTACH_TO_OUTER);
    }
  else
    {
      log_end_system_op (thread_p, LOG_RESULT_TOPOP_ABORT);
    }

  thread_set_check_interrupt (thread_p, old_val);

  mnt_file_page_deallocs (thread_p, ncont_page_entries);

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
file_allocset_remove_pageid (THREAD_ENTRY * thread_p, FILE_HEADER * fhdr,
			     FILE_ALLOCSET * allocset, PAGE_PTR fhdr_pgptr,
			     PAGE_PTR allocset_pgptr, INT16 allocset_offset,
			     PAGE_PTR pgptr, INT32 * aid_ptr,
			     INT32 ignore_pageid, void *ignore)
{
  return file_allocset_remove_contiguous_pages (thread_p, fhdr, allocset,
						fhdr_pgptr, allocset_pgptr,
						allocset_offset, pgptr,
						aid_ptr, 1);
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
file_allocset_remove_contiguous_pages (THREAD_ENTRY * thread_p,
				       FILE_HEADER * fhdr,
				       FILE_ALLOCSET * allocset,
				       PAGE_PTR fhdr_pgptr,
				       PAGE_PTR allocset_pgptr,
				       INT16 allocset_offset,
				       PAGE_PTR pgptr, INT32 * aid_ptr,
				       INT32 num_contpages)
{
  LOG_DATA_ADDR addr;
  int i;
  INT32 prealloc_mem[FILE_PREALLOC_MEMSIZE] = {
    NULL_PAGEID_MARKED_DELETED, NULL_PAGEID_MARKED_DELETED,
    NULL_PAGEID_MARKED_DELETED, NULL_PAGEID_MARKED_DELETED,
    NULL_PAGEID_MARKED_DELETED, NULL_PAGEID_MARKED_DELETED,
    NULL_PAGEID_MARKED_DELETED, NULL_PAGEID_MARKED_DELETED,
    NULL_PAGEID_MARKED_DELETED, NULL_PAGEID_MARKED_DELETED,
    NULL_PAGEID_MARKED_DELETED, NULL_PAGEID_MARKED_DELETED,
    NULL_PAGEID_MARKED_DELETED, NULL_PAGEID_MARKED_DELETED,
    NULL_PAGEID_MARKED_DELETED, NULL_PAGEID_MARKED_DELETED,
    NULL_PAGEID_MARKED_DELETED, NULL_PAGEID_MARKED_DELETED,
    NULL_PAGEID_MARKED_DELETED, NULL_PAGEID_MARKED_DELETED
  };
  INT32 *mem;
  int ret = NO_ERROR;
  INT32 undo_data, redo_data;
  FILE_RECV_DELETE_PAGES postpone_data;

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
	  /* Lack of memory. Do it by recursive calls using the preallocated
	     memory. */
	  while (num_contpages > FILE_PREALLOC_MEMSIZE)
	    {
	      ret =
		file_allocset_remove_contiguous_pages (thread_p, fhdr,
						       allocset, fhdr_pgptr,
						       allocset_pgptr,
						       allocset_offset, pgptr,
						       aid_ptr,
						       FILE_PREALLOC_MEMSIZE);
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
	      mem[i] = NULL_PAGEID_MARKED_DELETED;
	    }
	}
    }

  /* FROM now, set the identifier to NULL_PAGEID_MARKED_DELETED */
  addr.vfid = &fhdr->vfid;
  addr.pgptr = pgptr;
  addr.offset = CAST_BUFLEN ((char *) aid_ptr - (char *) pgptr);
  log_append_undoredo_data (thread_p, RVFL_IDSTABLE, &addr,
			    sizeof (*aid_ptr) * num_contpages,
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

  for (i = 0; i < num_contpages; i++)
    {
      mem[i] = NULL_PAGEID;
    }

  log_append_postpone (thread_p, RVFL_IDSTABLE, &addr,
		       sizeof (*aid_ptr) * num_contpages, mem);

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
  log_append_undoredo_data (thread_p, RVFL_ALLOCSET_DELETE_PAGES, &addr,
			    sizeof (undo_data), sizeof (redo_data),
			    &undo_data, &redo_data);
  pgbuf_set_dirty (thread_p, allocset_pgptr, DONT_FREE);

  fhdr->num_user_pages -= num_contpages;
  fhdr->num_user_pages_mrkdelete += num_contpages;

  addr.pgptr = fhdr_pgptr;
  addr.offset = FILE_HEADER_OFFSET;
  undo_data = num_contpages;
  redo_data = -num_contpages;
  log_append_undoredo_data (thread_p, RVFL_FHDR_MARK_DELETED_PAGES, &addr,
			    sizeof (undo_data), sizeof (redo_data),
			    &undo_data, &redo_data);

  postpone_data.deleted_npages = num_contpages;
  postpone_data.need_compaction = 0;
  if (allocset->num_holes >= NUM_HOLES_NEED_COMPACTION)
    {
      postpone_data.need_compaction = 1;
    }

  log_append_postpone (thread_p, RVFL_FHDR_DELETE_PAGES, &addr,
		       sizeof (postpone_data), &postpone_data);
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
file_allocset_find_num_deleted (THREAD_ENTRY * thread_p, FILE_HEADER * fhdr,
				FILE_ALLOCSET * allocset, int *num_deleted,
				int *num_marked_deleted)
{

  INT32 *aid_ptr;		/* Pointer to a portion of allocated
				   pageids table of given allocation set */
  INT32 *outptr;		/* Pointer to outside of portion of
				   allocated pageids */
  PAGE_PTR pgptr = NULL;
  VPID vpid;

  *num_deleted = 0;
  *num_marked_deleted = 0;

  vpid = allocset->start_pages_vpid;
  while (!VPID_ISNULL (&vpid))
    {
      pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ,
			 PGBUF_UNCONDITIONAL_LATCH);
      if (pgptr == NULL)
	{
	  goto exit_on_error;
	}

      (void) pgbuf_check_page_ptype (thread_p, pgptr, PAGE_FTAB);

      /* Calculate the portion of the set that we can look at */
      if (file_find_limits (pgptr, allocset, &aid_ptr, &outptr,
			    FILE_PAGE_TABLE) != NO_ERROR)
	{
	  goto exit_on_error;
	}

      /* We need to do a sequential scan since we do not know where the page is
         stored. The pages are stored by the allocation order */

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
file_dealloc_page (THREAD_ENTRY * thread_p, const VFID * vfid,
		   VPID * dealloc_vpid)
{
  FILE_ALLOCSET *allocset;
  VPID allocset_vpid;
  INT16 allocset_offset;
  PAGE_PTR allocset_pgptr = NULL;
  PAGE_PTR fhdr_pgptr = NULL;
  FILE_IS_NEW_FILE isfile_new;
  DISK_ISVALID isfound = DISK_INVALID;
  int ret;
  FILE_TYPE file_type;
  bool old_val = false;
  bool restore_check_interrupt = false;

  isfile_new = file_isnew_with_type (thread_p, vfid, &file_type);
  if (isfile_new == FILE_ERROR)
    {
      return ER_FAILED;
    }

  if (logtb_is_current_active (thread_p) == false
      && isfile_new == FILE_NEW_FILE && file_type != FILE_TMP_TMP)
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

      addr.pgptr = pgbuf_fix (thread_p, dealloc_vpid, OLD_PAGE,
			      PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
      if (addr.pgptr != NULL)
	{
	  log_append_redo_data (thread_p, RVFL_LOGICAL_NOOP, &addr, 0, NULL);
	  /* Even though this is a noop, we have to mark the page dirty
	     in order to keep the expensive pgbuf_unfix checks from complaining */
	  pgbuf_set_dirty (thread_p, addr.pgptr, FREE);
	  addr.pgptr = NULL;
	}
      return NO_ERROR;
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

  if (file_type == FILE_TMP || file_type == FILE_TMP_TMP)
    {
      old_val = thread_set_check_interrupt (thread_p, false);
      restore_check_interrupt = true;
    }

  ret = ER_FAILED;

  allocset_vpid.volid = vfid->volid;
  allocset_vpid.pageid = vfid->fileid;

  fhdr_pgptr = pgbuf_fix (thread_p, &allocset_vpid, OLD_PAGE,
			  PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
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
      allocset_pgptr = pgbuf_fix (thread_p, &allocset_vpid, OLD_PAGE,
				  PGBUF_LATCH_WRITE,
				  PGBUF_UNCONDITIONAL_LATCH);
      if (allocset_pgptr == NULL)
	{
	  goto exit_on_error;
	}

      (void) pgbuf_check_page_ptype (thread_p, allocset_pgptr, PAGE_FTAB);

      /* Get the allocation set */
      allocset = (FILE_ALLOCSET *) ((char *) allocset_pgptr
				    + allocset_offset);
      if (allocset->volid == dealloc_vpid->volid)
	{
	  /* The page may be located in this set */
	  isfound = file_allocset_find_page (thread_p, fhdr_pgptr,
					     allocset_pgptr, allocset_offset,
					     dealloc_vpid->pageid,
					     file_allocset_dealloc_page,
					     &ret);
	}
      if (isfound == DISK_ERROR)
	{
	  goto exit_on_error;
	}
      else if (isfound == DISK_INVALID)
	{
	  /* We did not find it in the current allocation set.
	     Get the next allocation set */
	  if (VPID_ISNULL (&allocset->next_allocset_vpid))
	    {
	      if (log_is_in_crash_recovery ())
		{
		  /*
		   * assert check after server up
		   * because logical page dealloc (like, RVBT_NEW_PGALLOC) for undo recovery
		   * could be run twice.
		   */
		  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE,
			  ER_FILE_TABLE_CORRUPTED, 2, vfid->volid,
			  vfid->fileid);
		}
	      else
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_FILE_TABLE_CORRUPTED, 2, vfid->volid,
			  vfid->fileid);

		  assert_release (0);
		}

	      VPID_SET_NULL (&allocset_vpid);
	    }
	  else
	    {
	      if (VPID_EQ (&allocset_vpid, &allocset->next_allocset_vpid)
		  && allocset_offset == allocset->next_allocset_offset)
		{
		  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_FILE_FTB_LOOP, 4, vfid->fileid,
			  fileio_get_volume_label (vfid->volid, PEEK),
			  allocset_vpid.volid, allocset_vpid.pageid);
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

  if (logtb_is_current_active (thread_p) == true)
    {
      log_end_system_op (thread_p, LOG_RESULT_TOPOP_ATTACH_TO_OUTER);
    }
  else
    {
      log_end_system_op (thread_p, LOG_RESULT_TOPOP_COMMIT);
    }

  if (restore_check_interrupt == true)
    {
      thread_set_check_interrupt (thread_p, old_val);
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
      thread_set_check_interrupt (thread_p, old_val);
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
file_truncate_to_numpages (THREAD_ENTRY * thread_p, const VFID * vfid,
			   INT32 keep_first_numpages)
{
  VPID allocset_vpid;		/* Page-volume identifier of allocset */
  VPID ftb_vpid;		/* File table in allocation set */
  FILE_HEADER *fhdr;
  FILE_ALLOCSET *allocset;	/* The first allocation set */
  PAGE_PTR fhdr_pgptr = NULL;
  PAGE_PTR allocset_pgptr = NULL;
  PAGE_PTR pgptr = NULL;
  INT32 *aid_ptr;		/* Pointer to portion of table of page or
				   sector allocation table */
  INT32 *outptr;		/* Out of bound pointer. */
  INT16 allocset_offset;
  INT32 *batch_first_aid_ptr;	/* First aid_ptr in batch of contiguous pages */
  INT32 batch_ndealloc;		/* Number of sectors to deallocate in
				   the batch */
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

  fhdr_pgptr = pgbuf_fix (thread_p, &allocset_vpid, OLD_PAGE,
			  PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
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
	  allocset_pgptr = pgbuf_fix (thread_p, &allocset_vpid, OLD_PAGE,
				      PGBUF_LATCH_WRITE,
				      PGBUF_UNCONDITIONAL_LATCH);
	  if (allocset_pgptr == NULL)
	    {
	      /* Something went wrong */
	      /* Cancel the lock and free the page */
	      goto exit_on_error;
	    }

	  (void) pgbuf_check_page_ptype (thread_p, allocset_pgptr, PAGE_FTAB);

	  allocset = (FILE_ALLOCSET *) ((char *) allocset_pgptr +
					allocset_offset);

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
		  pgptr = pgbuf_fix (thread_p, &ftb_vpid, OLD_PAGE,
				     PGBUF_LATCH_WRITE,
				     PGBUF_UNCONDITIONAL_LATCH);
		  if (pgptr == NULL)
		    {
		      /* Something went wrong */
		      /* Cancel the lock and free the page */
		      goto exit_on_error;
		    }

		  (void) pgbuf_check_page_ptype (thread_p, pgptr, PAGE_FTAB);

		  /* Calculate the starting offset and length of the page to
		     check */
		  ret = file_find_limits (pgptr, allocset, &aid_ptr, &outptr,
					  FILE_PAGE_TABLE);
		  if (ret != NO_ERROR)
		    {
		      pgbuf_unfix_and_init (thread_p, pgptr);

		      /* Something went wrong */
		      /* Cancel the lock and free the page */
		      goto exit_on_error;
		    }

		  /* Deallocate the user pages in this table page of this
		     allocation set. */

		  batch_first_aid_ptr = NULL;
		  batch_ndealloc = 0;

		  for (; aid_ptr < outptr; aid_ptr++)
		    {
		      if (*aid_ptr != NULL_PAGEID
			  && *aid_ptr != NULL_PAGEID_MARKED_DELETED)
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

		      /* Is page contiguous and is it stored contiguous in
		         allocation set */

		      if (*aid_ptr != NULL_PAGEID
			  && *aid_ptr != NULL_PAGEID_MARKED_DELETED
			  && batch_first_aid_ptr != NULL
			  && (*aid_ptr ==
			      *batch_first_aid_ptr + batch_ndealloc))
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

			  ret =
			    file_allocset_dealloc_contiguous_pages (thread_p,
								    fhdr,
								    allocset,
								    fhdr_pgptr,
								    allocset_pgptr,
								    allocset_offset,
								    pgptr,
								    batch_first_aid_ptr,
								    batch_ndealloc);
			  if (ret != NO_ERROR)
			    {
			      pgbuf_unfix_and_init (thread_p, pgptr);
			      goto exit_on_error;
			    }

			  /* Start again */
			  if (*aid_ptr != NULL_PAGEID
			      && *aid_ptr != NULL_PAGEID_MARKED_DELETED)
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
		      /* Deallocate any accumulated pages
		       * We do not care if the page deallocation failed,
		       * since we are truncating the file.. Deallocate as much
		       * as we can.
		       */

		      ret =
			file_allocset_dealloc_contiguous_pages (thread_p,
								fhdr,
								allocset,
								fhdr_pgptr,
								allocset_pgptr,
								allocset_offset,
								pgptr,
								batch_first_aid_ptr,
								batch_ndealloc);
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
		  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_FILE_FTB_LOOP, 4, vfid->fileid,
			  fileio_get_volume_label (vfid->volid, PEEK),
			  allocset->next_allocset_vpid.volid,
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

  /* The responsability of the deallocation of the pages is given to the outer
     nested level */
  log_end_system_op (thread_p, LOG_RESULT_TOPOP_ATTACH_TO_OUTER);

  return ret;

exit_on_error:

  if (allocset_pgptr)
    {
      pgbuf_unfix_and_init (thread_p, allocset_pgptr);
    }

  /* ABORT THE TOP SYSTEM OPERATION. That is, the deletion of the file is
     aborted, all pages that were deallocated are undone..  */
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
file_allocset_remove (THREAD_ENTRY * thread_p, PAGE_PTR fhdr_pgptr,
		      VPID * prev_allocset_vpid, INT16 prev_allocset_offset,
		      VPID * allocset_vpid, INT16 allocset_offset)
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
  INT32 batch_ndealloc;		/* Number of sectors to deallocate in
				   the batch */
  FILE_RECV_ALLOCSET_LINK recv_undo;
  FILE_RECV_ALLOCSET_LINK recv_redo;
  int ret = NO_ERROR;

  (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);

  fhdr = (FILE_HEADER *) (fhdr_pgptr + FILE_HEADER_OFFSET);

  addr.vfid = &fhdr->vfid;

  /* Don't destroy this allocation set if this is the only remaining set in
     the file */

  if (fhdr->num_allocsets == 1)
    {
      goto exit_on_error;
    }

  allocset_pgptr = pgbuf_fix (thread_p, allocset_vpid, OLD_PAGE,
			      PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
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
	  pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			     PGBUF_UNCONDITIONAL_LATCH);
	  if (pgptr == NULL)
	    {
	      ret = ER_FAILED;
	      break;
	    }

	  (void) pgbuf_check_page_ptype (thread_p, pgptr, PAGE_FTAB);

	  /* Calculate the starting offset and length of the page to check */
	  ret = file_find_limits (pgptr, allocset, &aid_ptr, &outptr,
				  FILE_SECTOR_TABLE);
	  if (ret != NO_ERROR)
	    {
	      pgbuf_unfix_and_init (thread_p, pgptr);
	      break;
	    }

	  /* Deallocate all user sectors in this sector table of this allocation
	     set. The sectors are deallocated in batches by their contiguity */

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
			  ret =
			    disk_dealloc_sector (thread_p, allocset->volid,
						 batch_firstid,
						 batch_ndealloc);
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
	      ret = disk_dealloc_sector (thread_p, allocset->volid,
					 batch_firstid, batch_ndealloc);
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

  /* If there was a failure, fisnih at this moment */
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  /* Is this the first allocation set ? */
  if (fhdr->vfid.volid == allocset_vpid->volid
      && fhdr->vfid.fileid == allocset_vpid->pageid
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
  if (VPID_EQ (&fhdr->last_allocset_vpid, allocset_vpid)
      && fhdr->last_allocset_offset == allocset_offset)
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

  /* Now free the page where the new allocation set is removed and modify
     the previous allocation set to point to next available allocation set */

  pgbuf_unfix_and_init (thread_p, allocset_pgptr);

  /* Get the previous allocation set */
  allocset_pgptr = pgbuf_fix (thread_p, prev_allocset_vpid, OLD_PAGE,
			      PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (allocset_pgptr == NULL)
    {
      goto exit_on_error;
    }

  (void) pgbuf_check_page_ptype (thread_p, allocset_pgptr, PAGE_FTAB);

  allocset =
    (FILE_ALLOCSET *) ((char *) allocset_pgptr + prev_allocset_offset);

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
  log_append_undoredo_data (thread_p, RVFL_ALLOCSET_LINK, &addr,
			    sizeof (recv_undo), sizeof (recv_redo),
			    &recv_undo, &recv_redo);

  pgbuf_set_dirty (thread_p, allocset_pgptr, FREE);
  allocset_pgptr = NULL;

  /* If the deleted allocation set was the last allocated set,
     change the header of the file to point to previous allocation set */

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
  log_append_undoredo_data (thread_p, RVFL_FHDR_REMOVE_LAST_ALLOCSET, &addr,
			    sizeof (recv_undo), sizeof (recv_redo),
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
file_allocset_reuse_last_set (THREAD_ENTRY * thread_p, PAGE_PTR fhdr_pgptr,
			      INT16 new_volid)
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
  INT32 batch_ndealloc;		/* Number of sectors to deallocate in
				   the batch */
  int ret = NO_ERROR;

  (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);

  fhdr = (FILE_HEADER *) (fhdr_pgptr + FILE_HEADER_OFFSET);

  addr.vfid = &fhdr->vfid;

  allocset_pgptr = pgbuf_fix (thread_p, &fhdr->last_allocset_vpid, OLD_PAGE,
			      PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (allocset_pgptr == NULL)
    {
      goto exit_on_error;
    }

  (void) pgbuf_check_page_ptype (thread_p, allocset_pgptr, PAGE_FTAB);

  allocset = (FILE_ALLOCSET *) ((char *) allocset_pgptr +
				fhdr->last_allocset_offset);

  /* Deallocate all sectors assigned to this allocation set */

  if (allocset->num_sects > 0)
    {
      vpid = allocset->start_sects_vpid;
      while (!VPID_ISNULL (&vpid))
	{
	  pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			     PGBUF_UNCONDITIONAL_LATCH);
	  if (pgptr == NULL)
	    {
	      goto exit_on_error;
	    }

	  (void) pgbuf_check_page_ptype (thread_p, pgptr, PAGE_FTAB);

	  /* Calculate the starting offset and length of the page to check */
	  ret = file_find_limits (pgptr, allocset, &aid_ptr, &outptr,
				  FILE_SECTOR_TABLE);
	  if (ret != NO_ERROR)
	    {
	      pgbuf_unfix_and_init (thread_p, pgptr);
	      goto exit_on_error;
	    }

	  /* Deallocate all user sectors in this sector table of this allocation
	     set. The sectors are deallocated in batches by their contiguity */

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
			  (void) disk_dealloc_sector (thread_p,
						      allocset->volid,
						      batch_firstid,
						      batch_ndealloc);
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
	      (void) disk_dealloc_sector (thread_p, allocset->volid,
					  batch_firstid, batch_ndealloc);
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

  log_append_undo_data (thread_p, RVFL_ALLOCSET_NEW, &addr,
			sizeof (*allocset), allocset);

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

  log_append_redo_data (thread_p, RVFL_ALLOCSET_NEW, &addr,
			sizeof (*allocset), allocset);
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
file_allocset_compact_page_table (THREAD_ENTRY * thread_p,
				  PAGE_PTR fhdr_pgptr,
				  PAGE_PTR allocset_pgptr,
				  INT16 allocset_offset,
				  bool rm_freespace_sectors)
{
  FILE_HEADER *fhdr;
  FILE_ALLOCSET *allocset;
  FILE_FTAB_CHAIN *chain;	/* Structure for linking file table pages */
  FILE_FTAB_CHAIN rv_undo_chain;	/* Old information before changes to
					   chain of pages */
  VPID to_vpid;
  VPID from_vpid;
  VPID vpid;
  VPID *allocset_vpidptr;
  INT32 *to_aid_ptr;		/* Pageid pointer array for to-page   */
  INT32 *to_outptr;		/* Out of pointer array for to-page   */
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

  if (rm_freespace_sectors == 1
      && (!(VPID_EQ (&allocset->start_pages_vpid, &allocset->end_sects_vpid)
	    && allocset->start_pages_offset == allocset->end_sects_offset))
      && allocset->start_sects_offset < DB_PAGESIZE)
    {
      /* The page table must be moved to the end of the sector table since we do
         not need to leave space for new sectors  */

      /* Get the from information before we reset the to information */
      from_vpid = allocset->start_pages_vpid;
      from_pgptr = pgbuf_fix (thread_p, &from_vpid, OLD_PAGE,
			      PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
      if (from_pgptr == NULL)
	{
	  goto exit_on_error;
	}

      (void) pgbuf_check_page_ptype (thread_p, from_pgptr, PAGE_FTAB);

      /* Find the location for portion of page table */
      ret = file_find_limits (from_pgptr, allocset, &from_aid_ptr,
			      &from_outptr, FILE_PAGE_TABLE);
      if (ret != NO_ERROR)
	{
	  goto exit_on_error;
	}
      /* Now reset the to information and get the to limits */
      allocset->start_pages_vpid = allocset->end_sects_vpid;
      allocset->start_pages_offset = allocset->end_sects_offset;

      to_vpid = allocset->start_pages_vpid;
      to_pgptr = pgbuf_fix (thread_p, &to_vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			    PGBUF_UNCONDITIONAL_LATCH);
      if (to_pgptr == NULL)
	{
	  goto exit_on_error;
	}

      (void) pgbuf_check_page_ptype (thread_p, to_pgptr, PAGE_FTAB);

      /* Find the location for portion of page table */
      ret = file_find_limits (to_pgptr, allocset, &to_aid_ptr, &to_outptr,
			      FILE_PAGE_TABLE);
      if (ret != NO_ERROR)
	{
	  goto exit_on_error;
	}
    }
  else
    {
      /* Start the shift at the first hole (NULL_PAGEID) in the page table */
      to_vpid = allocset->start_pages_vpid;
      to_pgptr = pgbuf_fix (thread_p, &to_vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			    PGBUF_UNCONDITIONAL_LATCH);
      if (to_pgptr == NULL)
	{
	  goto exit_on_error;
	}

      (void) pgbuf_check_page_ptype (thread_p, to_pgptr, PAGE_FTAB);

      /* Find the location for portion of page table */
      ret = file_find_limits (to_pgptr, allocset, &to_aid_ptr, &to_outptr,
			      FILE_PAGE_TABLE);
      if (ret != NO_ERROR)
	{
	  goto exit_on_error;
	}

      /* Nothing to move until we find the first hole */

      length = 0;
      while (length == 0
	     && (!VPID_EQ (&to_vpid, &allocset->end_pages_vpid)
		 || to_aid_ptr <= to_outptr))
	{
	  /* Boundary condition on to-page ? */
	  if (to_aid_ptr >= to_outptr)
	    {
#ifdef FILE_DEBUG
	      if (to_aid_ptr > to_outptr)
		{
		  er_log_debug (ARG_FILE_LINE,
				"file_allocset_compact_page_table:"
				" *** Boundary condition system error ***\n");
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
	      to_pgptr = pgbuf_fix (thread_p, &to_vpid, OLD_PAGE,
				    PGBUF_LATCH_WRITE,
				    PGBUF_UNCONDITIONAL_LATCH);
	      if (to_pgptr == NULL)
		{
		  goto exit_on_error;
		}

	      (void) pgbuf_check_page_ptype (thread_p, to_pgptr, PAGE_FTAB);

	      /* Find the location for portion of page table */
	      ret = file_find_limits (to_pgptr, allocset, &to_aid_ptr,
				      &to_outptr, FILE_PAGE_TABLE);
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

      /* Fetch the page again for the from part, so that our code can be
         written easily */
      from_pgptr = pgbuf_fix (thread_p, &from_vpid, OLD_PAGE,
			      PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
      if (from_pgptr == NULL)
	{
	  goto exit_on_error;
	}

      (void) pgbuf_check_page_ptype (thread_p, from_pgptr, PAGE_FTAB);
    }

  /* Now start the compacting process. Eliminate empty holes (NULL_PAGEIDs) */

  /* Before we move anything to the to_page, we need to log whatever is there
     just in case of a crash.. we will need to recover the shift... */

  to_start_offset = (PGLENGTH) ((char *) to_aid_ptr - (char *) to_pgptr);

  addr.pgptr = to_pgptr;
  addr.offset = to_start_offset;
  log_append_undo_data (thread_p, RVFL_IDSTABLE, &addr,
			CAST_BUFLEN ((char *) to_outptr -
				     (char *) to_aid_ptr), to_aid_ptr);
  length = 0;

  while (!VPID_EQ (&from_vpid, &allocset->end_pages_vpid) ||
	 from_aid_ptr <= from_outptr)
    {
      /* Boundary condition on from-page ? */
      if (from_aid_ptr >= from_outptr)
	{
#ifdef FILE_DEBUG
	  if (from_aid_ptr > from_outptr)
	    {
	      er_log_debug (ARG_FILE_LINE, "file_allocset_compact_page_table:"
			    " *** Boundary condition system error ***\n");
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
	  from_pgptr = pgbuf_fix (thread_p, &from_vpid, OLD_PAGE,
				  PGBUF_LATCH_WRITE,
				  PGBUF_UNCONDITIONAL_LATCH);
	  if (from_pgptr == NULL)
	    {
	      goto exit_on_error;
	    }

	  (void) pgbuf_check_page_ptype (thread_p, from_pgptr, PAGE_FTAB);

	  /* Find the location for portion of page table */
	  ret = file_find_limits (from_pgptr, allocset, &from_aid_ptr,
				  &from_outptr, FILE_PAGE_TABLE);
	  if (ret != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	}

      /* Boundary condition on to-page ? */
      if (to_aid_ptr >= to_outptr)
	{
#ifdef FILE_DEBUG
	  if (from_aid_ptr > from_outptr)
	    {
	      er_log_debug (ARG_FILE_LINE, "file_allocset_compact_page_table:"
			    " *** Boundary condition system error ***\n");
	    }
#endif /* FILE_DEBUG */

	  /* Was the to_page modified ? */
	  if (length != 0)
	    {
	      addr.pgptr = to_pgptr;
	      addr.offset = to_start_offset;
	      CAST_TO_PAGEARRAY (to_aid_ptr, to_pgptr, addr.offset);
	      log_append_redo_data (thread_p, RVFL_IDSTABLE, &addr, length,
				    to_aid_ptr);
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
	  to_pgptr = pgbuf_fix (thread_p, &to_vpid, OLD_PAGE,
				PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
	  if (to_pgptr == NULL)
	    {
	      goto exit_on_error;
	    }

	  (void) pgbuf_check_page_ptype (thread_p, to_pgptr, PAGE_FTAB);

	  /* Find the location for portion of page table */
	  ret = file_find_limits (to_pgptr, allocset, &to_aid_ptr, &to_outptr,
				  FILE_PAGE_TABLE);
	  if (ret != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	  /*
	   * Before we move anything to the to_page, we need to log whatever
	   * is there just in case of a crash.. we will need to recover the
	   * shift...
	   */

	  to_start_offset =
	    (PGLENGTH) ((char *) to_aid_ptr - (char *) to_pgptr);
	  addr.pgptr = to_pgptr;
	  addr.offset = to_start_offset;
	  log_append_undo_data (thread_p, RVFL_IDSTABLE, &addr,
				CAST_BUFLEN ((char *) to_outptr -
					     (char *) to_aid_ptr),
				to_aid_ptr);
	  length = 0;
	}

      if (allocset->num_holes > 0)
	{
	  /* Compact as much as possible until a boundary condition is reached */
	  while (to_aid_ptr < to_outptr && from_aid_ptr < from_outptr)
	    {
	      /* can we compact this entry ?.. if from_apageid_ptr is
	         NULL_PAGEID, the netry is compacted */
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
	  from_length =
	    CAST_BUFLEN ((char *) from_outptr - (char *) from_aid_ptr);
	  to_length = CAST_BUFLEN ((char *) to_outptr - (char *) to_aid_ptr);
	  if (to_length >= from_length)
	    {
	      /* The whole from length can be copied */
	      memmove (to_aid_ptr, from_aid_ptr, from_length);
	      length += from_length;
	      /* Note that we cannot increase the length to the pointer since
	         the length is in bytes and the pointer points to 4 bytes */
	      to_aid_ptr = (INT32 *) ((char *) to_aid_ptr + from_length);
	      from_aid_ptr = (INT32 *) ((char *) from_aid_ptr + from_length);
	    }
	  else
	    {
	      /* Only a portion of the from length can be copied */
	      memmove (to_aid_ptr, from_aid_ptr, to_length);
	      length += to_length;
	      /* Note that we cannot increase the length to the pointer since
	         the length is in bytes and the pointer points to 4 bytes */
	      to_aid_ptr = (INT32 *) ((char *) to_aid_ptr + to_length);
	      from_aid_ptr = (INT32 *) ((char *) from_aid_ptr + to_length);
	    }
	}
    }

  pgbuf_unfix_and_init (thread_p, from_pgptr);

  /* If last allocation set, remove any of the unused file table pages */
  allocset_vpidptr = pgbuf_get_vpid_ptr (allocset_pgptr);
  page_type = file_get_disk_page_type (fhdr->type);

  if (allocset_offset == fhdr->last_allocset_offset
      && VPID_EQ (allocset_vpidptr, &fhdr->last_allocset_vpid)
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
	  from_pgptr = pgbuf_fix (thread_p, &from_vpid, OLD_PAGE,
				  PGBUF_LATCH_WRITE,
				  PGBUF_UNCONDITIONAL_LATCH);
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
	  (void) disk_dealloc_page (thread_p, vpid.volid, vpid.pageid,
				    1, page_type);
	  num_ftb_pages_deleted++;
	}

      if (num_ftb_pages_deleted > 0)
	{
	  /* The file header will be changed to reflect the new file table
	     pages */

	  /* Save the data that is going to be change for undo purposes */
	  recv_ftb_undo.num_table_vpids = fhdr->num_table_vpids;
	  recv_ftb_undo.next_table_vpid = fhdr->next_table_vpid;
	  recv_ftb_undo.last_table_vpid = fhdr->last_table_vpid;

	  /* Update the file header */
	  fhdr->num_table_vpids -= num_ftb_pages_deleted;
	  fhdr->last_table_vpid = to_vpid;
	  if (to_vpid.volid == fhdr->vfid.volid
	      && to_vpid.pageid == fhdr->vfid.fileid)
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
	  log_append_undoredo_data (thread_p, RVFL_FHDR_FTB_EXPANSION, &addr,
				    sizeof (recv_ftb_undo),
				    sizeof (recv_ftb_redo), &recv_ftb_undo,
				    &recv_ftb_redo);

	  pgbuf_set_dirty (thread_p, fhdr_pgptr, DONT_FREE);

	  /* Update the last to_table to point to nothing */
	  if (to_vpid.volid != fhdr->vfid.volid ||
	      to_vpid.pageid != fhdr->vfid.fileid)
	    {
	      /* Header Page does not have a chain. */
	      chain = (FILE_FTAB_CHAIN *) (to_pgptr + FILE_FTAB_CHAIN_OFFSET);
	      memcpy (&rv_undo_chain, chain, sizeof (*chain));

	      VPID_SET_NULL (&chain->next_ftbvpid);
	      addr.pgptr = to_pgptr;
	      addr.offset = FILE_FTAB_CHAIN_OFFSET;
	      log_append_undoredo_data (thread_p, RVFL_FTAB_CHAIN, &addr,
					sizeof (*chain), sizeof (*chain),
					&rv_undo_chain, chain);
	      pgbuf_set_dirty (thread_p, to_pgptr, DONT_FREE);
	    }
	}
    }

  /* Update the allocation set with the end of page table, and log the undo
     and redo information */

  allocset->end_pages_vpid = to_vpid;
  allocset->end_pages_offset =
    (INT16) ((char *) to_aid_ptr - (char *) to_pgptr);
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
  log_append_undoredo_data (thread_p, RVFL_ALLOCSET_PAGETB_ADDRESS, &addr,
			    sizeof (recv_undo), sizeof (recv_redo),
			    &recv_undo, &recv_redo);

  pgbuf_set_dirty (thread_p, allocset_pgptr, DONT_FREE);

  /* Was the to_page modified ? */
  if (length != 0)
    {
      addr.pgptr = to_pgptr;
      addr.offset = to_start_offset;
      CAST_TO_PAGEARRAY (to_aid_ptr, to_pgptr, addr.offset);
      log_append_redo_data (thread_p, RVFL_IDSTABLE, &addr, length,
			    to_aid_ptr);
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
file_allocset_shift_sector_table (THREAD_ENTRY * thread_p,
				  PAGE_PTR fhdr_pgptr,
				  PAGE_PTR allocset_pgptr,
				  INT16 allocset_offset, VPID * ftb_vpid,
				  INT16 ftb_offset)
{
  FILE_HEADER *fhdr;
  FILE_ALLOCSET *allocset;
  VPID to_vpid;
  VPID from_vpid;
  INT32 *to_aid_ptr;		/* Pageid pointer array for to-page   */
  INT32 *from_aid_ptr;		/* Pageid pointer array for from-page */
  INT32 *to_outptr;		/* Out of pointer array for to-page   */
  INT32 *from_outptr;		/* Out of pointer array for from-page */
  PAGE_PTR to_pgptr = NULL;
  PAGE_PTR from_pgptr = NULL;
  int length;
  int to_length, from_length;
  LOG_DATA_ADDR addr;
  FILE_RECV_SHIFT_SECTOR_TABLE recv_undo;
  FILE_RECV_SHIFT_SECTOR_TABLE recv_redo;
  int ret = NO_ERROR;

  (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);
  (void) pgbuf_check_page_ptype (thread_p, allocset_pgptr, PAGE_FTAB);

  fhdr = (FILE_HEADER *) (fhdr_pgptr + FILE_HEADER_OFFSET);

  addr.vfid = &fhdr->vfid;

  allocset = (FILE_ALLOCSET *) ((char *) allocset_pgptr + allocset_offset);

  if (VPID_EQ (ftb_vpid, &allocset->start_sects_vpid)
      && ftb_offset == allocset->start_sects_offset)
    {
      /* Nothing to compact since sectors are not removed during the life of
         a file */
      return NO_ERROR;
    }

  to_vpid = *ftb_vpid;
  from_vpid = allocset->start_sects_vpid;

  to_pgptr = pgbuf_fix (thread_p, &to_vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			PGBUF_UNCONDITIONAL_LATCH);
  if (to_pgptr == NULL)
    {
      goto exit_on_error;
    }

  (void) pgbuf_check_page_ptype (thread_p, to_pgptr, PAGE_FTAB);

  from_pgptr = pgbuf_fix (thread_p, &from_vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			  PGBUF_UNCONDITIONAL_LATCH);
  if (from_pgptr == NULL)
    {
      goto exit_on_error;
    }

  (void) pgbuf_check_page_ptype (thread_p, from_pgptr, PAGE_FTAB);

  CAST_TO_PAGEARRAY (to_aid_ptr, to_pgptr, ftb_offset);
  CAST_TO_PAGEARRAY (to_outptr, to_pgptr, DB_PAGESIZE);

  /* Find the location for the from part. */
  ret = file_find_limits (from_pgptr, allocset, &from_aid_ptr, &from_outptr,
			  FILE_SECTOR_TABLE);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  /* Execute the move of the sector table */
  length = 0;
  while (!VPID_EQ (&from_vpid, &allocset->end_sects_vpid)
	 || from_aid_ptr <= from_outptr)
    {
      from_length = CAST_BUFLEN ((char *) from_outptr
				 - (char *) from_aid_ptr);
      to_length = CAST_BUFLEN ((char *) to_outptr - (char *) to_aid_ptr);

      if (to_length >= from_length)
	{
	  /* Everything from length can be copied */

	  /* Log whatever is going to be copied */
	  addr.pgptr = to_pgptr;
	  addr.offset = (PGLENGTH) ((char *) to_aid_ptr - (char *) to_pgptr);
	  log_append_undo_data (thread_p, RVFL_IDSTABLE, &addr, from_length,
				to_aid_ptr);

	  memmove (to_aid_ptr, from_aid_ptr, from_length);

	  /* Note that we cannot increase the length to the pointer since the
	     the length is in bytes and the pointer points to 4 bytes */
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
	  from_pgptr = pgbuf_fix (thread_p, &from_vpid, OLD_PAGE,
				  PGBUF_LATCH_WRITE,
				  PGBUF_UNCONDITIONAL_LATCH);
	  if (from_pgptr == NULL)
	    {
	      goto exit_on_error;
	    }

	  (void) pgbuf_check_page_ptype (thread_p, from_pgptr, PAGE_FTAB);

	  /* Find the location for the from part. */
	  ret = file_find_limits (from_pgptr, allocset, &from_aid_ptr,
				  &from_outptr, FILE_SECTOR_TABLE);
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
	  log_append_undo_data (thread_p, RVFL_IDSTABLE, &addr, to_length,
				to_aid_ptr);

	  memmove (to_aid_ptr, from_aid_ptr, to_length);

	  /* Note that we cannot increase the length to the pointer since the
	     the length is in bytes and the pointer points to 4 bytes */
	  from_aid_ptr = (INT32 *) ((char *) from_aid_ptr + to_length);
	  length += to_length;

	  /* log whatever was changed on this page.. Everything changes on this
	     page.. no just this step */

	  addr.pgptr = to_pgptr;
	  addr.offset = (VPID_EQ (&to_vpid, &allocset->start_sects_vpid)
			 ? allocset->start_sects_offset
			 : SSIZEOF (FILE_FTAB_CHAIN));
	  CAST_TO_PAGEARRAY (to_aid_ptr, to_pgptr, addr.offset);
	  log_append_redo_data (thread_p, RVFL_IDSTABLE, &addr, length,
				to_aid_ptr);

	  pgbuf_set_dirty (thread_p, to_pgptr, DONT_FREE);
	  length = 0;

	  /* Get the next to page */
	  if (VPID_EQ (&to_vpid, &allocset->end_sects_vpid))
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
	  to_pgptr = pgbuf_fix (thread_p, &to_vpid, OLD_PAGE,
				PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
	  if (to_pgptr == NULL)
	    {
	      goto exit_on_error;
	    }

	  (void) pgbuf_check_page_ptype (thread_p, to_pgptr, PAGE_FTAB);

	  CAST_TO_PAGEARRAY (to_aid_ptr, to_pgptr, sizeof (FILE_FTAB_CHAIN));
	  CAST_TO_PAGEARRAY (to_outptr, to_pgptr, DB_PAGESIZE);
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
  allocset->end_sects_offset =
    (INT16) ((char *) to_aid_ptr - (char *) to_pgptr);

  /* Save the values for redo purposes */
  recv_redo.start_sects_vpid = allocset->start_sects_vpid;
  recv_redo.start_sects_offset = allocset->start_sects_offset;
  recv_redo.end_sects_vpid = allocset->end_sects_vpid;
  recv_redo.end_sects_offset = allocset->end_sects_offset;

  /* Now log the changes */
  addr.pgptr = allocset_pgptr;
  addr.offset = allocset_offset;
  log_append_undoredo_data (thread_p, RVFL_ALLOCSET_SECT_SHIFT, &addr,
			    sizeof (recv_undo), sizeof (recv_redo),
			    &recv_undo, &recv_redo);

  pgbuf_set_dirty (thread_p, allocset_pgptr, DONT_FREE);

  /* Log the last portion of the to page that was changed */

  addr.pgptr = to_pgptr;
  addr.offset = (VPID_EQ (&to_vpid, &allocset->start_sects_vpid)
		 ? allocset->start_sects_offset : SSIZEOF (FILE_FTAB_CHAIN));
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
 *   allocset_vpid(in): Page address where the current allocation set is
 *                      located
 *   allocset_offset(in): Location in allocset page where allocation set is
 *                        located
 *   ftb_vpid(in): The file table page where the allocation set is moved
 *   ftb_offset(in): Offset in the file table page where the allocation set is
 *                   moved
 */
static int
file_allocset_compact (THREAD_ENTRY * thread_p, PAGE_PTR fhdr_pgptr,
		       VPID * prev_allocset_vpid,
		       INT16 prev_allocset_offset, VPID * allocset_vpid,
		       INT16 * allocset_offset, VPID * ftb_vpid,
		       INT16 * ftb_offset)
{
  FILE_HEADER *fhdr;
  PAGE_PTR allocset_pgptr = NULL;
  PAGE_PTR ftb_pgptr = NULL;	/* Pointer to page where the set is
				   compacted */
  FILE_ALLOCSET *allocset;
  bool islast_allocset;
  LOG_DATA_ADDR addr;
  FILE_RECV_ALLOCSET_LINK recv_undo;
  FILE_RECV_ALLOCSET_LINK recv_redo;
  int ret = NO_ERROR;

  (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);

  fhdr = (FILE_HEADER *) (fhdr_pgptr + FILE_HEADER_OFFSET);

  addr.vfid = &fhdr->vfid;

  allocset_pgptr = pgbuf_fix (thread_p, allocset_vpid, OLD_PAGE,
			      PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (allocset_pgptr == NULL)
    {
      goto exit_on_error;
    }

  (void) pgbuf_check_page_ptype (thread_p, allocset_pgptr, PAGE_FTAB);

  allocset = (FILE_ALLOCSET *) ((char *) allocset_pgptr + *allocset_offset);

  if (VPID_EQ (&fhdr->last_allocset_vpid, allocset_vpid)
      && fhdr->last_allocset_offset == *allocset_offset)
    {
      islast_allocset = true;
    }
  else
    {
      islast_allocset = false;
    }

  while (true)
    {
      if (*allocset_offset == *ftb_offset
	  && VPID_EQ (allocset_vpid, ftb_vpid))
	{
	  /* Move the pointers */
	  *ftb_vpid = allocset->start_sects_vpid;
	  *ftb_offset = allocset->start_sects_offset;
	  break;
	}
      else
	{
	  /* Copy the allocation set description */
	  ftb_pgptr = pgbuf_fix (thread_p, ftb_vpid, OLD_PAGE,
				 PGBUF_LATCH_WRITE,
				 PGBUF_UNCONDITIONAL_LATCH);
	  if (ftb_pgptr == NULL)
	    {
	      goto exit_on_error;
	    }

	  (void) pgbuf_check_page_ptype (thread_p, ftb_pgptr, PAGE_FTAB);

	  if ((*ftb_offset + SSIZEOF (*allocset)) >= DB_PAGESIZE)
	    {
	      /* Cannot copy the allocation set at this location.
	         Get next copy page */
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
	      log_append_undoredo_data (thread_p, RVFL_ALLOCSET_COPY, &addr,
					sizeof (*allocset),
					sizeof (*allocset),
					(char *) ftb_pgptr + *ftb_offset,
					allocset);

	      /* Copy the allocation set to current location */
	      memmove ((char *) ftb_pgptr + *ftb_offset, allocset,
		       sizeof (*allocset));
	      pgbuf_set_dirty (thread_p, ftb_pgptr, FREE);
	      ftb_pgptr = NULL;

	      /* Now free the old allocation page, modify the old allocation
	         set to point to new address, and get the new allocation page */
	      pgbuf_unfix_and_init (thread_p, allocset_pgptr);

	      *allocset_vpid = *ftb_vpid;
	      *allocset_offset = *ftb_offset;
	      *ftb_offset += sizeof (*allocset);

	      /* The previous allocation set must point to new address */
	      allocset_pgptr = pgbuf_fix (thread_p, prev_allocset_vpid,
					  OLD_PAGE, PGBUF_LATCH_WRITE,
					  PGBUF_UNCONDITIONAL_LATCH);
	      if (allocset_pgptr == NULL)
		{
		  goto exit_on_error;
		}

	      (void) pgbuf_check_page_ptype (thread_p, allocset_pgptr,
					     PAGE_FTAB);

	      allocset = (FILE_ALLOCSET *) ((char *) allocset_pgptr +
					    prev_allocset_offset);

	      /* Save the informationthat is going to be changed for undo
	         purposes */
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
	      log_append_undoredo_data (thread_p, RVFL_ALLOCSET_LINK, &addr,
					sizeof (recv_undo),
					sizeof (recv_redo), &recv_undo,
					&recv_redo);

	      pgbuf_set_dirty (thread_p, allocset_pgptr, FREE);
	      allocset_pgptr = NULL;

	      /* If this is the last allocation set, change the header */
	      if (islast_allocset == true)
		{
		  /* Save the information that is going to be changed for undo
		     purposes */
		  recv_undo.next_allocset_vpid = fhdr->last_allocset_vpid;
		  recv_undo.next_allocset_offset = fhdr->last_allocset_offset;

		  /* Execute the change */
		  fhdr->last_allocset_vpid = *allocset_vpid;
		  fhdr->last_allocset_offset = *allocset_offset;

		  /* Log the changes */
		  addr.pgptr = fhdr_pgptr;
		  addr.offset = FILE_HEADER_OFFSET;
		  log_append_undoredo_data (thread_p,
					    RVFL_FHDR_CHANGE_LAST_ALLOCSET,
					    &addr, sizeof (recv_undo),
					    sizeof (recv_redo), &recv_undo,
					    &recv_redo);

		  pgbuf_set_dirty (thread_p, fhdr_pgptr, DONT_FREE);
		}

	      /* Now re-read the desired allocation set since we moved to
	         another location */
	      allocset_pgptr = pgbuf_fix (thread_p, allocset_vpid, OLD_PAGE,
					  PGBUF_LATCH_WRITE,
					  PGBUF_UNCONDITIONAL_LATCH);
	      if (allocset_pgptr == NULL)
		{
		  goto exit_on_error;
		}

	      (void) pgbuf_check_page_ptype (thread_p, allocset_pgptr,
					     PAGE_FTAB);

	      allocset = (FILE_ALLOCSET *) ((char *) allocset_pgptr +
					    *allocset_offset);
	      break;
	    }
	}
    }

  if (file_allocset_shift_sector_table (thread_p, fhdr_pgptr, allocset_pgptr,
					*allocset_offset, ftb_vpid,
					*ftb_offset) != NO_ERROR ||
      file_allocset_compact_page_table (thread_p, fhdr_pgptr, allocset_pgptr,
					*allocset_offset,
					((islast_allocset == true)
					 ? false : true)) != NO_ERROR)
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
file_compress (THREAD_ENTRY * thread_p, const VFID * vfid,
	       bool do_partial_compaction)
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

  fhdr_pgptr = pgbuf_fix (thread_p, &allocset_vpid, OLD_PAGE,
			  PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
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
  if (file_tracker_is_registered_vfid (thread_p, vfid) == false)
    {
      assert (false);

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
      allocset_pgptr = pgbuf_fix (thread_p, &allocset_vpid, OLD_PAGE,
				  PGBUF_LATCH_WRITE,
				  PGBUF_UNCONDITIONAL_LATCH);
      if (allocset_pgptr == NULL)
	{
	  ret = ER_FAILED;
	  break;
	}

      (void) pgbuf_check_page_ptype (thread_p, allocset_pgptr, PAGE_FTAB);

      allocset = (FILE_ALLOCSET *) ((char *) allocset_pgptr
				    + allocset_offset);

      if (do_partial_compaction == true
	  && allocset->num_holes < NUM_HOLES_NEED_COMPACTION)
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

	  ret = file_allocset_remove (thread_p, fhdr_pgptr,
				      &prev_allocset_vpid,
				      prev_allocset_offset, &allocset_vpid,
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
	  /* We need to free the page since after the compaction the allocation
	     set may change address */
	  pgbuf_unfix_and_init (thread_p, allocset_pgptr);
	  ret = file_allocset_compact (thread_p, fhdr_pgptr,
				       &prev_allocset_vpid,
				       prev_allocset_offset, &allocset_vpid,
				       &allocset_offset, &ftb_vpid,
				       &ftb_offset);
	  if (ret != NO_ERROR)
	    {
	      break;
	    }

	  prev_allocset_vpid = allocset_vpid;
	  prev_allocset_offset = allocset_offset;

	  /* Find the next allocation set */
	  allocset_pgptr = pgbuf_fix (thread_p, &allocset_vpid, OLD_PAGE,
				      PGBUF_LATCH_WRITE,
				      PGBUF_UNCONDITIONAL_LATCH);
	  if (allocset_pgptr == NULL)
	    {
	      ret = ER_FAILED;
	      break;
	    }

	  (void) pgbuf_check_page_ptype (thread_p, allocset_pgptr, PAGE_FTAB);

	  allocset = (FILE_ALLOCSET *) ((char *) allocset_pgptr
					+ allocset_offset);

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
file_check_all_pages (THREAD_ENTRY * thread_p, const VFID * vfid,
		      bool validate_vfid)
{
  FILE_HEADER *fhdr;
  PAGE_PTR fhdr_pgptr = NULL;
  PAGE_PTR pgptr = NULL;
  VPID set_vpids[FILE_SET_NUMVPIDS];	/* Page-volume identifier.
					   Limited to FILE_SET_NUMVPIDS each time */
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
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FILE_UNKNOWN_FILE,
		      2, vfid->volid, vfid->fileid);
	    }

	  return allvalid;
	}
    }
#endif

  allvalid = DISK_VALID;

  /* Copy the descriptor to a volume-page descriptor */
  set_vpids[0].volid = vfid->volid;
  set_vpids[0].pageid = vfid->fileid;

  fhdr_pgptr = pgbuf_fix (thread_p, &set_vpids[0], OLD_PAGE, PGBUF_LATCH_READ,
			  PGBUF_UNCONDITIONAL_LATCH);
  if (fhdr_pgptr == NULL)
    {
      goto exit_on_error;
    }

  (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);

  fhdr = (FILE_HEADER *) (fhdr_pgptr + FILE_HEADER_OFFSET);

  if (!VFID_EQ (vfid, &fhdr->vfid) ||
      fhdr->num_table_vpids <= 0 || fhdr->num_allocsets <= 0 ||
      fhdr->num_user_pages < 0 || fhdr->num_user_pages_mrkdelete < 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FILE_INCONSISTENT_HEADER,
	      3, vfid->volid, vfid->fileid,
	      fileio_get_volume_label (vfid->volid, PEEK));
      allvalid = DISK_INVALID;
    }

  /* Make sure that all file table pages are consistent */
  while (!VPID_ISNULL (&set_vpids[0]))
    {
      valid =
	disk_isvalid_page (thread_p, set_vpids[0].volid, set_vpids[0].pageid);
      if (valid != DISK_VALID)
	{
	  if (valid == DISK_INVALID)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_FILE_INCONSISTENT_ALLOCATION, 6, set_vpids[0].volid,
		      set_vpids[0].pageid,
		      fileio_get_volume_label (set_vpids[0].volid, PEEK),
		      vfid->volid, vfid->fileid,
		      fileio_get_volume_label (vfid->volid, PEEK));
	    }

	  allvalid = valid;
	  break;
	}

      pgptr = pgbuf_fix (thread_p, &set_vpids[0], OLD_PAGE, PGBUF_LATCH_READ,
			 PGBUF_UNCONDITIONAL_LATCH);
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
					  ((fhdr->num_user_pages - i <
					    FILE_SET_NUMVPIDS) ?
					   fhdr->num_user_pages -
					   i : FILE_SET_NUMVPIDS));
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
	      valid = disk_isvalid_page (thread_p, set_vpids[j].volid,
					 set_vpids[j].pageid);
	      if (valid != DISK_VALID)
		{
		  if (valid == DISK_INVALID)
		    {
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			      ER_FILE_INCONSISTENT_ALLOCATION, 6,
			      set_vpids[j].volid, set_vpids[j].pageid,
			      fileio_get_volume_label (set_vpids[j].volid,
						       PEEK), vfid->volid,
			      vfid->fileid,
			      fileio_get_volume_label (vfid->volid, PEEK));
		    }
		  allvalid = valid;
		  /* Continue looking for more */
		}
	    }
	}

      if (i != fhdr->num_user_pages)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_FILE_INCONSISTENT_EXPECTED_PAGES, 5,
		  fhdr->num_user_pages, i, vfid->volid, vfid->fileid,
		  fileio_get_volume_label (vfid->volid, PEEK));
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

  fhdr_pgptr = pgbuf_fix (thread_p, &allocset_vpid, OLD_PAGE,
			  PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (fhdr_pgptr == NULL)
    {
      return DISK_ERROR;
    }

  (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);

  fhdr = (FILE_HEADER *) (fhdr_pgptr + FILE_HEADER_OFFSET);

  allocset_offset = offsetof (FILE_HEADER, allocset);
  while (!VPID_ISNULL (&allocset_vpid))
    {
      allocset_pgptr = pgbuf_fix (thread_p, &allocset_vpid, OLD_PAGE,
				  PGBUF_LATCH_READ,
				  PGBUF_UNCONDITIONAL_LATCH);
      if (allocset_pgptr == NULL)
	{
	  goto exit_on_error;
	}

      (void) pgbuf_check_page_ptype (thread_p, allocset_pgptr, PAGE_FTAB);

      /* Get the allocation set */
      allocset =
	(FILE_ALLOCSET *) ((char *) allocset_pgptr + allocset_offset);
      if (file_allocset_find_num_deleted (thread_p, fhdr, allocset,
					  &num_deleted,
					  &num_marked_deleted) < 0
	  || allocset->num_holes != num_deleted + num_marked_deleted)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_FILE_ALLOCSET_INCON_EXPECTED_NHOLES, 5,
		  vfid->fileid, vfid->volid,
		  fileio_get_volume_label (vfid->volid, PEEK),
		  num_deleted + num_marked_deleted, allocset->num_holes);
	  valid = DISK_INVALID;
	}
      total_marked_deleted += num_marked_deleted;

      /* Next allocation set */
      if (VPID_EQ (&allocset_vpid, &allocset->next_allocset_vpid)
	  && allocset_offset == allocset->next_allocset_offset)
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_FILE_FTB_LOOP, 4,
		  vfid->fileid, fileio_get_volume_label (vfid->volid, PEEK),
		  allocset->next_allocset_vpid.volid,
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
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_FILE_INCONSISTENT_EXPECTED_MARKED_DEL, 5,
	      vfid->fileid, vfid->volid,
	      fileio_get_volume_label (vfid->volid, PEEK),
	      fhdr->num_user_pages_mrkdelete, total_marked_deleted);
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

  (void) fprintf (fp,
		  "*** FILE HEADER TABLE DUMP FOR FILE IDENTIFIER = %d"
		  " ON VOLUME = %d ***\n", fhdr->vfid.fileid,
		  fhdr->vfid.volid);

  tmp_time = (time_t) fhdr->creation;
  (void) ctime_r (&tmp_time, time_val);
  (void) fprintf (fp,
		  "File_type = %s, Ismark_as_deleted = %s,\n"
		  "Creation_time = %s", file_type_to_string (fhdr->type),
		  (fhdr->ismark_as_deleted == true) ? "true" : "false",
		  time_val);
  (void) fprintf (fp, "File Descriptor comments = ");
  ret = file_descriptor_dump_internal (thread_p, fp, fhdr);
  if (ret != NO_ERROR)
    {
      return ret;
    }
  (void) fprintf (fp, "Num_allocsets = %d, Num_user_pages = %d,"
		  " Num_mark_deleted = %d\n",
		  fhdr->num_allocsets, fhdr->num_user_pages,
		  fhdr->num_user_pages_mrkdelete);
  (void) fprintf (fp, "Num_ftb_pages = %d, Next_ftb_page = %d|%d,"
		  " Last_ftb_page = %d|%d,\n",
		  fhdr->num_table_vpids,
		  fhdr->next_table_vpid.volid, fhdr->next_table_vpid.pageid,
		  fhdr->last_table_vpid.volid, fhdr->last_table_vpid.pageid);
  (void) fprintf (fp, "Last allocset at VPID: %d|%d, offset = %d\n",
		  fhdr->last_allocset_vpid.volid,
		  fhdr->last_allocset_vpid.pageid,
		  fhdr->last_allocset_offset);

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

      ftb_pgptr = pgbuf_fix (thread_p, &ftb_vpid, OLD_PAGE, PGBUF_LATCH_READ,
			     PGBUF_UNCONDITIONAL_LATCH);
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
file_allocset_dump (FILE * fp, const FILE_ALLOCSET * allocset,
		    bool doprint_title)
{
  int ret = NO_ERROR;

  if (doprint_title == true)
    {
      (void) fprintf (fp, "ALLOCATION SET:\n");
    }

  (void) fprintf (fp, "Volid=%d, Num_sects = %d, Num_pages = %d,"
		  " Num_entries_to_compact = %d\n",
		  allocset->volid, allocset->num_sects, allocset->num_pages,
		  allocset->num_holes);

  (void) fprintf (fp, "Next_allocation_set: Page = %d|%d, Offset = %d\n",
		  allocset->next_allocset_vpid.volid,
		  allocset->next_allocset_vpid.pageid,
		  allocset->next_allocset_offset);

  (void) fprintf (fp, "Sector Table (Start): Page = %d|%d, Offset = %d\n",
		  allocset->start_sects_vpid.volid,
		  allocset->start_sects_vpid.pageid,
		  allocset->start_sects_offset);
  (void) fprintf (fp, "               (End): Page = %d|%d, Offset = %d\n",
		  allocset->end_sects_vpid.volid,
		  allocset->end_sects_vpid.pageid,
		  allocset->end_sects_offset);
  (void) fprintf (fp, "          Current_sectid = %d\n",
		  allocset->curr_sectid);

  (void) fprintf (fp, "Page Table   (Start): Page = %d|%d, Offset = %d\n",
		  allocset->start_pages_vpid.volid,
		  allocset->start_pages_vpid.pageid,
		  allocset->start_pages_offset);
  (void) fprintf (fp, "               (End): Page = %d|%d, Offset = %d\n",
		  allocset->end_pages_vpid.volid,
		  allocset->end_pages_vpid.pageid,
		  allocset->end_pages_offset);

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
file_allocset_dump_tables (THREAD_ENTRY * thread_p, FILE * fp,
			   const FILE_HEADER * fhdr,
			   const FILE_ALLOCSET * allocset)
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
      pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ,
			 PGBUF_UNCONDITIONAL_LATCH);
      if (pgptr == NULL)
	{
	  ret = ER_FAILED;
	  break;
	}

      (void) pgbuf_check_page_ptype (thread_p, pgptr, PAGE_FTAB);

      /* Calculate the portion of pageids that we can look at current page */
      ret = file_find_limits (pgptr, allocset, &aid_ptr, &outptr,
			      FILE_SECTOR_TABLE);
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
      (void) fprintf (fp, "WARNING: Number of sectors = %d does not match"
		      " sectors = %d in allocationset header\n",
		      num_aids, allocset->num_sects);
    }

  /* Dump the page table */
  (void) fprintf (fp, "Allocated pages:\n");

  num_out = 0;
  num_aids = 0;

  vpid = allocset->start_pages_vpid;
  while (!VPID_ISNULL (&vpid))
    {
      pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ,
			 PGBUF_UNCONDITIONAL_LATCH);
      if (pgptr == NULL)
	{
	  ret = ER_FAILED;
	  break;
	}

      (void) pgbuf_check_page_ptype (thread_p, pgptr, PAGE_FTAB);

      /* Calculate the portion of pageids that we can look at current page */
      ret = file_find_limits (pgptr, allocset, &aid_ptr, &outptr,
			      FILE_PAGE_TABLE);
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
      (void) fprintf (fp, "WARNING: Number of pages = %d does not match"
		      " pages = %d in allocationset header\n",
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

  fhdr_pgptr = pgbuf_fix (thread_p, &allocset_vpid, OLD_PAGE,
			  PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
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
      allocset_pgptr = pgbuf_fix (thread_p, &allocset_vpid, OLD_PAGE,
				  PGBUF_LATCH_READ,
				  PGBUF_UNCONDITIONAL_LATCH);
      if (allocset_pgptr == NULL)
	{
	  goto exit_on_error;
	}

      setno++;
      /* Get the allocation set */
      allocset =
	(FILE_ALLOCSET *) ((char *) allocset_pgptr + allocset_offset);

      (void) fprintf (fp, "ALLOCATION SET NUM %d located at"
		      "vpid = %d|%d offset = %d:\n",
		      setno, allocset_vpid.volid, allocset_vpid.pageid,
		      allocset_offset);
      ret = file_allocset_dump (fp, allocset, false);
      if (ret != NO_ERROR)
	{
	  goto exit_on_error;
	}

      (void) fprintf (fp, "First page in this allocation set is the"
		      " nthpage = %d\n", num_pages);
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
	      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_FILE_FTB_LOOP, 4, vfid->fileid,
		      fileio_get_volume_label (vfid->volid, PEEK),
		      allocset->next_allocset_vpid.volid,
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

/*
 * file_reset_contiguous_temporary_pages () - Reset LSA of pages of temporary
 *                                          files on permanent volumes as
 *                                          temporary or permanent
 *   return: NO_ERROR
 *   volid(in): The permanent volume where pages are declared as temporary
 *              or permanent
 *   pageid(in): The page in the volume
 *   num_pages(in): Number of contiguous pages to reset
 *   reset_to_temp(in): Wheater to reset to temp or permanent
 *
 * Note: Reset the log sequence address of pages of temporary files on
 *       permanent volumes which are dedicated not for temporary use (i.e.,
 *       files of type FILE_TMP) as either temporary of permanent. The pages are
 *       declared as temporary when they are allocated to the file and as
 *       permanent when they are deallocated. This is done to avoid logging
 *       from temporary files. Watch out for temporary extendible hash which
 *       perform logging.
 */
static int
file_reset_contiguous_temporary_pages (THREAD_ENTRY * thread_p, INT16 volid,
				       INT32 pageid, INT32 num_pages,
				       bool reset_to_temp)
{
  PAGE_PTR pgptr = NULL;
  VPID vpid;
  int i;

  vpid.volid = volid;

  for (i = 0; i < num_pages; i++)
    {
      vpid.pageid = pageid + i;
      pgptr = pgbuf_fix (thread_p, &vpid, NEW_PAGE, PGBUF_LATCH_WRITE,
			 PGBUF_UNCONDITIONAL_LATCH);
      if (pgptr == NULL)
	{
	  return ER_FAILED;
	}

      if (reset_to_temp == true)
	{
	  pgbuf_set_lsa_as_temporary (thread_p, pgptr);
	}
      else
	{
	  pgbuf_set_lsa_as_permanent (thread_p, pgptr);
	}
      pgbuf_invalidate (thread_p, pgptr);
      pgptr = NULL;
    }

  return NO_ERROR;
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

  if (file_create (thread_p, vfid, 0, FILE_TRACKER, NULL, NULL, 0) == NULL)
    {
      goto exit_on_error;
    }

  allocset_vpid.volid = vfid->volid;
  allocset_vpid.pageid = vfid->fileid;

  fhdr_pgptr = pgbuf_fix (thread_p, &allocset_vpid, OLD_PAGE,
			  PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (fhdr_pgptr == NULL)
    {
      goto exit_on_error;
    }

  (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);

  allocset_offset = offsetof (FILE_HEADER, allocset);

  if (file_allocset_compact_page_table (thread_p, fhdr_pgptr, fhdr_pgptr,
					allocset_offset, true) != NO_ERROR)
    {
      goto exit_on_error;
    }

  pgbuf_unfix_and_init (thread_p, fhdr_pgptr);
  /* Register it now. We need to do it here since the file tracker was
     unknown when the file was created */
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

  /* Store the file identifier in the array of pageids */
  if (file_Tracker->vfid == NULL || VFID_ISNULL (vfid) ||
      vfid->fileid == DISK_NULL_PAGEID_WITH_ENOUGH_DISK_PAGES)
    {
      return NULL;
    }

  vol_purpose = xdisk_get_purpose (thread_p, vfid->volid);
  if (vol_purpose == DISK_UNKNOWN_PURPOSE
      || vol_purpose == DISK_TEMPVOL_TEMP_PURPOSE
      || vol_purpose == DISK_PERMVOL_TEMP_PURPOSE)
    {
      /* Temporary file on volumes with temporary purposes are not recorded
         by the tracker. */
      return vfid;
    }

  vpid.volid = file_Tracker->vfid->volid;
  vpid.pageid = file_Tracker->vfid->fileid;

  fhdr_pgptr =
    pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
	       PGBUF_UNCONDITIONAL_LATCH);
  if (fhdr_pgptr == NULL)
    {
      return NULL;
    }

  (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);

  fhdr = (FILE_HEADER *) (fhdr_pgptr + FILE_HEADER_OFFSET);

  /* The following will be associated to the outer transaction
     The log is to allow UNDO.  This is a logical log. */
  addr.vfid = &fhdr->vfid;
  addr.pgptr = NULL;
  addr.offset = 0;
  log_append_undo_data (thread_p, RVFL_TRACKER_REGISTER, &addr,
			sizeof (*vfid), vfid);

  if (log_start_system_op (thread_p) == NULL)
    {
      pgbuf_unfix_and_init (thread_p, fhdr_pgptr);
      return NULL;
    }

  /* We need to know the allocset the file belongs to. */
  allocset_pgptr = pgbuf_fix (thread_p, &fhdr->last_allocset_vpid, OLD_PAGE,
			      PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (allocset_pgptr == NULL)
    {
      goto exit_on_error;
    }

  (void) pgbuf_check_page_ptype (thread_p, allocset_pgptr, PAGE_FTAB);

  allocset = (FILE_ALLOCSET *) ((char *) allocset_pgptr +
				fhdr->last_allocset_offset);
  if (allocset->volid != vfid->volid)
    {
      pgbuf_unfix_and_init (thread_p, allocset_pgptr);
      if (file_allocset_add_set (thread_p, fhdr_pgptr, vfid->volid) !=
	  NO_ERROR)
	{
	  goto exit_on_error;
	}
      /* Now get the new location for the allocation set */
      allocset_pgptr = pgbuf_fix (thread_p, &fhdr->last_allocset_vpid,
				  OLD_PAGE, PGBUF_LATCH_WRITE,
				  PGBUF_UNCONDITIONAL_LATCH);
      if (allocset_pgptr == NULL)
	{
	  goto exit_on_error;
	}

      (void) pgbuf_check_page_ptype (thread_p, allocset_pgptr, PAGE_FTAB);

      allocset = (FILE_ALLOCSET *) ((char *) allocset_pgptr +
				    fhdr->last_allocset_offset);
    }

  if (file_allocset_add_pageids (thread_p, fhdr_pgptr, allocset_pgptr,
				 fhdr->last_allocset_offset,
				 vfid->fileid, 1, NULL) == NULL_PAGEID)
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

  if (file_Tracker->vfid == NULL || VFID_ISNULL (vfid)
      || vfid->fileid == DISK_NULL_PAGEID_WITH_ENOUGH_DISK_PAGES)
    {
      return NULL;
    }

  /* Temporary files on volumes with temporary purposes are not registerd */
  vol_purpose = xdisk_get_purpose (thread_p, vfid->volid);
  if (vol_purpose == DISK_UNKNOWN_PURPOSE
      || vol_purpose == DISK_TEMPVOL_TEMP_PURPOSE
      || vol_purpose == DISK_PERMVOL_TEMP_PURPOSE)
    {
      /* Temporary file on volumes with temporary purposes are not recorded
         by the tracker. */
      return vfid;
    }

  allocset_vpid.volid = file_Tracker->vfid->volid;
  allocset_vpid.pageid = file_Tracker->vfid->fileid;

  fhdr_pgptr = pgbuf_fix (thread_p, &allocset_vpid, OLD_PAGE,
			  PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (fhdr_pgptr == NULL)
    {
      goto exit_on_error;
    }

  (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);

  allocset_offset = offsetof (FILE_HEADER, allocset);

  /* Go over each allocation set until the page is found */
  while (!VPID_ISNULL (&allocset_vpid))
    {
      allocset_pgptr = pgbuf_fix (thread_p, &allocset_vpid, OLD_PAGE,
				  PGBUF_LATCH_WRITE,
				  PGBUF_UNCONDITIONAL_LATCH);
      if (allocset_pgptr == NULL)
	{
	  goto exit_on_error;
	}

      (void) pgbuf_check_page_ptype (thread_p, allocset_pgptr, PAGE_FTAB);

      /* Get the allocation set */
      allocset = (FILE_ALLOCSET *) ((char *) allocset_pgptr
				    + allocset_offset);
      if (allocset->volid == vfid->volid)
	{
	  /* The page may be located in this set */
	  isfound = file_allocset_find_page (thread_p, fhdr_pgptr,
					     allocset_pgptr, allocset_offset,
					     (INT32) (vfid->fileid),
					     file_allocset_remove_pageid,
					     &ignore);
	}
      if (isfound == DISK_ERROR)
	{
	  goto exit_on_error;
	}
      else if (isfound == DISK_INVALID)
	{
	  /* We did not find it in the current allocation set.
	     Get the next allocation set */
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
		  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_FILE_FTB_LOOP, 4, vfid->fileid,
			  fileio_get_volume_label (vfid->volid, PEEK),
			  allocset_vpid.volid, allocset_vpid.pageid);
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

  if (file_Tracker->vfid == NULL)
    {
      VFID_SET_NULL (vfid);
      return -1;
    }

  count =
    file_find_nthpages (thread_p, file_Tracker->vfid, &vpid, nthfile, 1);
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

  if (VFID_ISNULL (vfid) ||
      vfid->fileid == DISK_NULL_PAGEID_WITH_ENOUGH_DISK_PAGES)
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
  int i, j, nperm_vols;
  char **vol_ids_map = NULL;

  if (file_Tracker->vfid == NULL)
    {
      return DISK_ERROR;
    }

  nperm_vols = xboot_find_number_permanent_volumes (thread_p);

  if (nperm_vols <= 0)
    {
      return DISK_ERROR;
    }

  vol_ids_map = (char **) calloc (nperm_vols, sizeof (char *));

  if (vol_ids_map == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, sizeof (char *) * nperm_vols);
      return DISK_ERROR;
    }

  /* phase 1. allocate disk-allocset page memory */
  for (i = 0; i < nperm_vols; i++)
    {
      vpid.volid = i + LOG_DBFIRST_VOLID;
      vpid.pageid = DISK_VOLHEADER_PAGE;

      vhdr_pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ,
			      PGBUF_UNCONDITIONAL_LATCH);

      if (vhdr_pgptr == NULL)
	{
	  allvalid = DISK_ERROR;
	  goto end;
	}

      (void) pgbuf_check_page_ptype (thread_p, vhdr_pgptr, PAGE_VOLHEADER);

      vhdr = (DISK_VAR_HEADER *) vhdr_pgptr;

      if (vhdr->purpose != DISK_PERMVOL_DATA_PURPOSE
	  && vhdr->purpose != DISK_PERMVOL_INDEX_PURPOSE
	  && vhdr->purpose != DISK_PERMVOL_GENERIC_PURPOSE)
	{
	  pgbuf_unfix_and_init (thread_p, vhdr_pgptr);
	  continue;
	}

      vol_ids_map[i] = calloc (vhdr->page_alloctb_npages, IO_PAGESIZE);

      if (vol_ids_map[i] == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, vhdr->page_alloctb_npages * IO_PAGESIZE);
	  pgbuf_unfix_and_init (thread_p, vhdr_pgptr);
	  allvalid = DISK_ERROR;
	  goto end;
	}

      /* Mark disk system pages(volume header page ~ sys_lastpage) */
      for (j = DISK_VOLHEADER_PAGE; j <= vhdr->sys_lastpage; j++)
	{
	  set_bitmap (vol_ids_map[i], j);
	}

      pgbuf_unfix_and_init (thread_p, vhdr_pgptr);
    }

  /* phase 2. construct disk bitmap image with file tracker */
  num_files = file_get_numpages (thread_p, file_Tracker->vfid);
  for (i = 0; i < num_files && allvalid != DISK_ERROR; i += num_found)
    {
      num_found = file_find_nthpages (thread_p, file_Tracker->vfid,
				      &set_vpids[0], i,
				      ((num_files - i < FILE_SET_NUMVPIDS)
				       ? num_files - i : FILE_SET_NUMVPIDS));

      if (num_found <= 0)
	{
	  allvalid = DISK_ERROR;
	  break;
	}

      for (j = 0; j < num_found && allvalid != DISK_ERROR; j++)
	{
	  vfid.volid = set_vpids[j].volid;
	  vfid.fileid = set_vpids[j].pageid;

	  valid =
	    file_make_idsmap_image (thread_p, &vfid, vol_ids_map, nperm_vols);

	  if (valid != DISK_VALID)
	    {
	      allvalid = valid;
	    }

	  if (allvalid != DISK_ERROR)
	    {
	      valid = file_check_deleted (thread_p, &vfid);
	      if (valid != DISK_VALID)
		{
		  allvalid = valid;
		}
	    }
	}
    }

  if (allvalid != DISK_ERROR && i != num_files)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FILE_MISMATCH_NFILES, 2,
	      num_files, i);
      allvalid = DISK_ERROR;
    }

  /* phase 3. cross check idsmap images with disk bitmap */
  for (i = 0; i < nperm_vols && allvalid != DISK_ERROR; i++)
    {
      if (vol_ids_map[i] != NULL)
	{
	  valid =
	    file_verify_idsmap_image (thread_p, i + LOG_DBFIRST_VOLID,
				      vol_ids_map[i]);
	  if (valid != DISK_VALID)
	    {
	      allvalid = valid;
	    }
	}
    }

end:
  if (vol_ids_map)
    {
      for (i = 0; i < nperm_vols; i++)
	{
	  if (vol_ids_map[i])
	    {
	      free_and_init (vol_ids_map[i]);
	    }
	}

      free_and_init (vol_ids_map);
    }
#endif

  return allvalid;
}


/*
 * file_make_idsmap_image () -
 *   return: NO_ERROR
 *   vpid(in):
 */
static DISK_ISVALID
file_make_idsmap_image (THREAD_ENTRY * thread_p,
			const VFID * vfid, char **vol_ids_map, int nperm_vols)
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

  fhdr_pgptr = pgbuf_fix (thread_p, &set_vpids[0], OLD_PAGE, PGBUF_LATCH_READ,
			  PGBUF_UNCONDITIONAL_LATCH);
  if (fhdr_pgptr == NULL)
    {
      return DISK_ERROR;
    }

  (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);

  fhdr = (FILE_HEADER *) (fhdr_pgptr + FILE_HEADER_OFFSET);

  if (!VFID_EQ (vfid, &fhdr->vfid) ||
      fhdr->num_table_vpids <= 0 || fhdr->num_allocsets <= 0 ||
      fhdr->num_user_pages < 0 || fhdr->num_user_pages_mrkdelete < 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FILE_INCONSISTENT_HEADER,
	      3, vfid->volid, vfid->fileid,
	      fileio_get_volume_label (vfid->volid, PEEK));

      valid = DISK_ERROR;
      goto end;
    }

  /* 1. set file table pages to vol_ids_map */
  /* file header page */
  if (vfid->volid < nperm_vols)
    {
      set_bitmap (vol_ids_map[vfid->volid], set_vpids[0].pageid);
    }
  else
    {
      assert_release (vfid->volid < nperm_vols);
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
      if (next_ftable_vpid.volid < nperm_vols)
	{
	  set_bitmap (vol_ids_map[next_ftable_vpid.volid],
		      next_ftable_vpid.pageid);
	}
      else
	{
	  assert_release (next_ftable_vpid.volid < nperm_vols);
	}

      pgptr =
	pgbuf_fix (thread_p, &next_ftable_vpid, OLD_PAGE, PGBUF_LATCH_READ,
		   PGBUF_UNCONDITIONAL_LATCH);

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
      num_found =
	file_find_nthpages (thread_p, vfid, &set_vpids[0], i,
			    ((num_user_pages - i < FILE_SET_NUMVPIDS)
			     ? (num_user_pages - i) : FILE_SET_NUMVPIDS));

      if (num_found <= 0)
	{
	  valid = DISK_ERROR;
	  goto end;
	}

      for (j = 0; j < num_found; j++)
	{
	  if (set_vpids[j].volid < nperm_vols)
	    {
	      set_bitmap (vol_ids_map[set_vpids[j].volid],
			  set_vpids[j].pageid);
	    }
	  else
	    {
	      assert_release (set_vpids[j].volid < nperm_vols);
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
  byte_offset = ((pageid - (page_num * DISK_PAGE_BIT)) / CHAR_BIT)
    + sizeof (FILEIO_PAGE_RESERVED);
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
file_verify_idsmap_image (THREAD_ENTRY * thread_p, INT16 volid,
			  char *vol_ids_map)
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

  vpid.volid = volid;
  vpid.pageid = DISK_VOLHEADER_PAGE;

  vhdr_pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ,
			  PGBUF_UNCONDITIONAL_LATCH);

  if (vhdr_pgptr == NULL)
    {
      return DISK_ERROR;
    }

  (void) pgbuf_check_page_ptype (thread_p, vhdr_pgptr, PAGE_VOLHEADER);

  vhdr = (DISK_VAR_HEADER *) vhdr_pgptr;
  vpid.volid = volid;
  vpid.pageid = vhdr->page_alloctb_page1;

  for (i = 0; i < vhdr->page_alloctb_npages; i++, vpid.pageid++)
    {
      alloc_pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ,
			       PGBUF_UNCONDITIONAL_LATCH);
      if (alloc_pgptr == NULL)
	{
	  return_code = DISK_ERROR;
	  break;
	}

      (void) pgbuf_check_page_ptype (thread_p, alloc_pgptr, PAGE_VOLBITMAP);

      file_offset =
	vol_ids_map + (i * IO_PAGESIZE) + sizeof (FILEIO_PAGE_RESERVED);

      disk_offset = alloc_pgptr;

      if (memcmp (file_offset, disk_offset, DB_PAGESIZE) != 0)
	{
	  /* find out inconsistent pageids */
	  for (j = 0; j < DB_PAGESIZE; j++)
	    {
	      /* compare every byte */
	      if (memcmp (file_offset, disk_offset, 1) != 0)
		{
		  int pageid, byte_offset;
		  char exclusive_or;

		  byte_offset = (i * DB_PAGESIZE) + j;

		  exclusive_or = (*disk_offset) ^ (*file_offset);

		  /* compare every bit */
		  for (k = 0; k < CHAR_BIT; k++)
		    {
		      /* k bit is set ? */
		      if (exclusive_or & (1 << k))
			{
			  /* this bit is different */
			  pageid = byte_offset * CHAR_BIT + k;

			  if (*disk_offset & (1 << k))
			    {
			      /* disk is set & file is not set */
			      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
				      ER_FILE_INCONSISTENT_PAGE_NOT_ALLOCED,
				      2, pageid,
				      fileio_get_volume_label (vpid.volid,
							       PEEK));
			    }
			  else
			    {
			      /* file is set & disk is not set */
			      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
				      ER_FILE_INCONSISTENT_PAGE_ALLOCED,
				      2, pageid,
				      fileio_get_volume_label (vpid.volid,
							       PEEK));
			    }
			  return_code = DISK_INVALID;
			}
		    }
		  assert (return_code == DISK_INVALID);
		  break;
		}
	      file_offset++;
	      disk_offset++;
	    }
	}
      pgbuf_unfix_and_init (thread_p, alloc_pgptr);
    }

  pgbuf_unfix_and_init (thread_p, vhdr_pgptr);

  return return_code;
}

/*
 * file_tracker_is_registered_vfid () -
 *   return:
 *
 *   vfid(in):
 */
bool
file_tracker_is_registered_vfid (THREAD_ENTRY * thread_p, const VFID * vfid)
{
  PAGE_PTR trk_fhdr_pgptr = NULL;
  int num_files;
  VPID set_vpids[FILE_SET_NUMVPIDS];
  int num_found;
  VFID tmp_vfid;
  bool found = false;
  int i, j;

  if (file_Tracker->vfid == NULL)
    {
      return false;
    }

  set_vpids[0].volid = file_Tracker->vfid->volid;
  set_vpids[0].pageid = file_Tracker->vfid->fileid;

  trk_fhdr_pgptr = pgbuf_fix (thread_p, &set_vpids[0], OLD_PAGE,
			      PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (trk_fhdr_pgptr == NULL)
    {
      return false;
    }

  (void) pgbuf_check_page_ptype (thread_p, trk_fhdr_pgptr, PAGE_FTAB);

  found = false;
  num_files = file_get_numpages (thread_p, file_Tracker->vfid);
  for (i = 0; i < num_files && found == false; i += num_found)
    {
      num_found = file_find_nthpages (thread_p, file_Tracker->vfid,
				      &set_vpids[0], i,
				      ((num_files - i < FILE_SET_NUMVPIDS)
				       ? num_files - i : FILE_SET_NUMVPIDS));
      if (num_found < 0)
	{
	  break;
	}

      for (j = 0; j < num_found; j++)
	{
	  tmp_vfid.volid = set_vpids[j].volid;
	  tmp_vfid.fileid = set_vpids[j].pageid;
	  if (VFID_EQ (&tmp_vfid, vfid))
	    {
	      found = true;
	      break;
	    }
	}
    }

  pgbuf_unfix_and_init (thread_p, trk_fhdr_pgptr);

  return found;
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
  VPID set_vpids[FILE_SET_NUMVPIDS];	/* Page-volume identifier. Limited
					   to FILE_SET_NUMVPIDS each time */
  int num_found;		/* Number of files in each cycle */
  VFID vfid;			/* Identifier of a found file */
  DISK_ISVALID valid;
  DISK_ISVALID allvalid = DISK_VALID;
  int i, j;

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

  trk_fhdr_pgptr = pgbuf_fix (thread_p, &set_vpids[0], OLD_PAGE,
			      PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (trk_fhdr_pgptr == NULL)
    {
      return DISK_ERROR;
    }

  (void) pgbuf_check_page_ptype (thread_p, trk_fhdr_pgptr, PAGE_FTAB);

  num_files = file_get_numpages (thread_p, file_Tracker->vfid);
  for (i = 0; i < num_files && allvalid != DISK_ERROR; i += num_found)
    {
      num_found = file_find_nthpages (thread_p, file_Tracker->vfid,
				      &set_vpids[0], i,
				      ((num_files - i < FILE_SET_NUMVPIDS)
				       ? num_files - i : FILE_SET_NUMVPIDS));
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
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FILE_MISMATCH_NFILES, 2,
	      num_files, i);
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
  VPID set_vpids[FILE_SET_NUMVPIDS];	/* Page-volume identifier. Limited
					   to FILE_SET_NUMVPIDS each time */
  int num_found;		/* Number of files in each cycle */
  VFID vfid;			/* Identifier of a found file */
  int i, j;

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

  trk_fhdr_pgptr = pgbuf_fix (thread_p, &set_vpids[0], OLD_PAGE,
			      PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (trk_fhdr_pgptr == NULL)
    {
      return ER_FAILED;
    }

  (void) pgbuf_check_page_ptype (thread_p, trk_fhdr_pgptr, PAGE_FTAB);

  num_files = file_get_numpages (thread_p, file_Tracker->vfid);

  /* Display General Information */
  (void) fprintf (fp, "\n\n DUMPING EACH FILE: Total Num of Files = %d\n",
		  num_files);

  for (i = 0; i < num_files; i += num_found)
    {
      num_found = file_find_nthpages (thread_p, file_Tracker->vfid,
				      &set_vpids[0], i,
				      ((num_files - i < FILE_SET_NUMVPIDS)
				       ? num_files - i : FILE_SET_NUMVPIDS));
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
	      fprintf (fp,
		       "\n**NOTE: Num_alloc_pgs for tracker are number of"
		       " allocated files...\n");
	    }
	}
    }

  if (i != num_files)
    {
      (void) fprintf (fp, "Error: %d expected files, %d found files\n",
		      num_files, i);
    }

  pgbuf_unfix_and_init (thread_p, trk_fhdr_pgptr);

  return file_dump_all_newfiles (thread_p, fp, true);
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
  VPID set_vpids[FILE_SET_NUMVPIDS];	/* Page-volume identifier. Limited
					   to FILE_SET_NUMVPIDS each time */
  int num_found;		/* Number of files in each cycle */
  VFID vfid;			/* Identifier of a found file */
  int i, j;
  int ret = NO_ERROR;

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

  trk_fhdr_pgptr = pgbuf_fix (thread_p, &set_vpids[0], OLD_PAGE,
			      PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (trk_fhdr_pgptr == NULL)
    {
      return ER_FAILED;
    }

  (void) pgbuf_check_page_ptype (thread_p, trk_fhdr_pgptr, PAGE_FTAB);

  /* Compress all the files, but the tracker file at this moment. */
  num_files = file_get_numpages (thread_p, file_Tracker->vfid);
  for (i = 0; i < num_files; i += num_found)
    {
      num_found = file_find_nthpages (thread_p, file_Tracker->vfid,
				      &set_vpids[0], i,
				      ((num_files - i < FILE_SET_NUMVPIDS)
				       ? num_files - i : FILE_SET_NUMVPIDS));
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

  assert (file_Tracker->hint_num_mark_deleted[file_type] >= 0);

  node = (FILE_MARK_DEL_LIST *) malloc (sizeof (FILE_MARK_DEL_LIST));
  if (node == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, sizeof (FILE_MARK_DEL_LIST));
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
file_reuse_deleted (THREAD_ENTRY * thread_p, VFID * vfid,
		    FILE_TYPE file_type, const void *file_des)
{
  PAGE_PTR trk_fhdr_pgptr = NULL;
  PAGE_PTR fhdr_pgptr = NULL;
  FILE_HEADER *fhdr;
  LOG_DATA_ADDR addr;
  VPID set_vpids[FILE_SET_NUMVPIDS];	/* Page-volume identifier.
					   Limited to FILE_SET_NUMVPIDS each time */
  int num_files;		/* Number of known files */
  int num_found;		/* Number of files found in each cycle */
  INT16 clean;
  int i, j;
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */
  VFID tmp_vfid = {
    NULL_FILEID, NULL_VOLID
  };
  VPID tmp_vpid;

  assert (file_type <= FILE_LAST);

  /*
   * If the table has not been scanned, find the number of deleted files.
   * Use this number as a hint in the future to avoid searching the table and
   * the corresponding files for future reuses. We do not use critical
   * sections when this number is increased when files are marked as deleted..
   * Thus, it is used as a good approximation that may fail in some cases..
   */

  rv = pthread_mutex_lock (&file_Num_mark_deleted_hint_lock);

  if (file_Tracker->vfid == NULL ||
      file_Tracker->hint_num_mark_deleted[file_type] == 0)
    {
      VFID_SET_NULL (vfid);
      pthread_mutex_unlock (&file_Num_mark_deleted_hint_lock);
      return NULL;
    }

  if (file_Tracker->hint_num_mark_deleted[file_type] == -1)
    {
      /*
       * We need to lock the tracker header page in exclusive mode to scan for
       * a mark deleted file in a consistent way. For example, we do not want
       * to reuse a marked deleted file twice.
       */
      set_vpids[0].volid = file_Tracker->vfid->volid;
      set_vpids[0].pageid = file_Tracker->vfid->fileid;

      trk_fhdr_pgptr = pgbuf_fix (thread_p, &set_vpids[0], OLD_PAGE,
				  PGBUF_LATCH_WRITE,
				  PGBUF_UNCONDITIONAL_LATCH);
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
	  num_found = file_find_nthpages (thread_p, file_Tracker->vfid,
					  &set_vpids[0], i,
					  ((num_files - i < FILE_SET_NUMVPIDS)
					   ? num_files - i
					   : FILE_SET_NUMVPIDS));
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
	      fhdr_pgptr = pgbuf_fix (thread_p, &set_vpids[j], OLD_PAGE,
				      PGBUF_LATCH_WRITE,
				      PGBUF_UNCONDITIONAL_LATCH);
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
		      if (file_mark_deleted_file_list_add (&fhdr->vfid,
							   fhdr->type)
			  != NO_ERROR)
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

  fhdr_pgptr = pgbuf_fix (thread_p, &tmp_vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			  PGBUF_UNCONDITIONAL_LATCH);
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
  log_append_undoredo_data (thread_p, RVFL_MARKED_DELETED, &addr,
			    sizeof (fhdr->ismark_as_deleted),
			    sizeof (fhdr->ismark_as_deleted),
			    &fhdr->ismark_as_deleted, &clean);
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
  PAGE_PTR trk_fhdr_pgptr = NULL;
  VFID marked_files[FILE_DESTROY_NUM_MARKED];
  VPID vpid;
  int num_files;
  int num_marked = 0;
  int i, nth;
  int ret = NO_ERROR;

  if (file_Tracker->vfid == NULL)
    {
      return ER_FAILED;
    }

  /*
   * We need to lock the tracker header page in shared mode for the duration of
   * the reclaim, so that files are not register or unregister during this
   * operation
   */

  vpid.volid = file_Tracker->vfid->volid;
  vpid.pageid = file_Tracker->vfid->fileid;

  trk_fhdr_pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ,
			      PGBUF_UNCONDITIONAL_LATCH);
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
	  if (file_find_nthpages (thread_p, file_Tracker->vfid, &vpid, i, 1)
	      <= 0)
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

	  if (file_does_marked_as_deleted (thread_p,
					   &marked_files[num_marked]) == true)
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
	      ret =
		file_compress (thread_p, &marked_files[num_marked], false);
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

	  for (i = 0; i < num_marked; i++)
	    {
	      (void) file_destroy (thread_p, &marked_files[i]);
	    }
	}
    }

  if (ret == NO_ERROR)
    {
      ret = file_compress (thread_p, file_Tracker->vfid, false);
    }

  pgbuf_unfix_and_init (thread_p, trk_fhdr_pgptr);

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

      type = file_get_type (thread_p, &vfid);
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
	      size = file_get_descriptor (thread_p, &vfid, file_des,
					  file_des_size);
	    }
	}

      fprintf (fp, "%4d|%4d %5d  %-22s ",
	       vfid.volid, vfid.fileid, num_pages,
	       file_type_to_string (type));
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
file_create_hint_numpages (THREAD_ENTRY * thread_p, INT32 exp_numpages,
			   FILE_TYPE file_type)
{
  int i;

  if (file_type == FILE_TMP)
    {
      file_type = FILE_EITHER_TMP;
    }

  if (exp_numpages <= 0)
    {
      exp_numpages = 1;
    }

  /* Make sure that we will find a good volume with at least the expected
     number of pages */

  i = file_guess_numpages_overhead (thread_p, NULL, exp_numpages);
  (void) file_find_goodvol (thread_p, NULL_VOLID, NULL_VOLID,
			    exp_numpages + i,
			    DISK_NONCONTIGUOUS_SPANVOLS_PAGES, file_type);

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

  if (file_tracker_unregister (thread_p, vfid) == NULL
      && file_Tracker->vfid != NULL)
    {
      /*
       * FOOL the recovery manager so that it won't complain that a media
       * recovery may be needed since the file was unregistered (likely was
       * gone)
       */

      vpid.volid = file_Tracker->vfid->volid;
      vpid.pageid = file_Tracker->vfid->fileid;

      addr.pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			      PGBUF_UNCONDITIONAL_LATCH);
      if (addr.pgptr == NULL)
	{
	  return NO_ERROR;	/* do not permit error */
	}

      (void) pgbuf_check_page_ptype (thread_p, addr.pgptr, PAGE_FTAB);

      addr.vfid = file_Tracker->vfid;
      addr.offset = -1;
      /* Don't need undo we are undoing! */
      log_append_redo_data (thread_p, RVFL_LOGICAL_NOOP, &addr, 0, NULL);
      /* Even though this is a noop, we have to mark the page dirty
         in order to keep the expensive pgbuf_unfix checks from complaining. */
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
      || disk_isvalid_page (thread_p, vfid->volid,
			    (INT32) (vfid->fileid)) != DISK_VALID)
    {
      ret = file_rv_tracker_unregister_logical_undo (thread_p, vfid);
    }
  else
    {
      (void) file_new_declare_as_old (thread_p, vfid);

      if (vfid->volid > xboot_find_number_permanent_volumes (thread_p))
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

	      fhdr_pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE,
				      PGBUF_LATCH_WRITE,
				      PGBUF_UNCONDITIONAL_LATCH);
	      if (fhdr_pgptr == NULL)
		{
		  ret = ER_FAILED;
		}
	      else
		{
		  (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr,
						 PAGE_FTAB);

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
  (void) fprintf (fp, "Undo creation of Tmp vfid: %d|%d\n",
		  vfid->volid, vfid->fileid);
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
  (void) fprintf (fp, "Next_ftb_vpid:%d|%d, Previous_ftb_vpid = %d|%d\n",
		  recv->next_ftbvpid.volid, recv->next_ftbvpid.pageid,
		  recv->prev_ftbvpid.volid, recv->prev_ftbvpid.pageid);
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
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */
  int ret = NO_ERROR;

  (void) pgbuf_check_page_ptype (thread_p, rcv->pgptr, PAGE_FTAB);

  assert (rcv->length == 2 || rcv->length == 4);
  if (rcv->length == 2)
    {
      isdeleted = *(INT16 *) rcv->data;
    }
  else
    {
      /* As safe guard, In old log of RB-8.4.3 or before, the function
       * file_mark_as_deleted() writes 4 bytes as isdeleted flag. */
      isdeleted = (INT16) (*((INT32 *) rcv->data));
    }

  fhdr = (FILE_HEADER *) (rcv->pgptr + FILE_HEADER_OFFSET);

  fhdr->ismark_as_deleted = isdeleted;

  rv = pthread_mutex_lock (&file_Num_mark_deleted_hint_lock);
  if (file_Tracker->hint_num_mark_deleted[fhdr->type] != -1
      && isdeleted == true
      && (fhdr->type == FILE_HEAP || fhdr->type == FILE_HEAP_REUSE_SLOTS))
    {
      ret = file_mark_deleted_file_list_add (&fhdr->vfid, fhdr->type);
      if (ret != NO_ERROR)
	{
	  pthread_mutex_unlock (&file_Num_mark_deleted_hint_lock);
	  return NO_ERROR;	/* do not permit error */
	}
    }
  pthread_mutex_unlock (&file_Num_mark_deleted_hint_lock);

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

  (void) fprintf (fp, "Num_sects = %d, Curr_sectid = %d,\n"
		  "Sector Table end: pageid = %d|%d, offset = %d\n",
		  rvsect->num_sects, rvsect->curr_sectid,
		  rvsect->end_sects_vpid.volid,
		  rvsect->end_sects_vpid.pageid, rvsect->end_sects_offset);
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

  fprintf (fp, "Page Table   (Start): Page = %d|%d, Offset = %d\n",
	   rvstb->start_pages_vpid.volid,
	   rvstb->start_pages_vpid.pageid, rvstb->start_pages_offset);
  fprintf (fp, "               (End): Page = %d|%d, Offset = %d\n",
	   rvstb->end_pages_vpid.volid, rvstb->end_pages_vpid.pageid,
	   rvstb->end_pages_offset);

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
  FILE_RECV_FTB_EXPANSION *rvftb;	/* Recovery information   */
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
  fprintf (fp, "Num_ftb_pages = %d, Next_ftb_page = %d|%d"
	   " Last_ftb_page = %d|%d,\n",
	   rvftb->num_table_vpids,
	   rvftb->next_table_vpid.volid, rvftb->next_table_vpid.pageid,
	   rvftb->last_table_vpid.volid, rvftb->last_table_vpid.pageid);
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

  fprintf (fp, "Next_allocation_set: Page = %d|%d Offset = %d\n",
	   rvlink->next_allocset_vpid.volid,
	   rvlink->next_allocset_vpid.pageid, rvlink->next_allocset_offset);
}

/*
 * file_rv_fhdr_last_allocset_helper () - UNDO/REDO address of last allocation set
 *                                      on file header
 *   return: NO_ERROR
 *   rcv(in): Recovery structure
 */
static int
file_rv_fhdr_last_allocset_helper (THREAD_ENTRY * thread_p, LOG_RCV * rcv,
				   int delta)
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

  fprintf (fp, "Last_allocation_set: Page = %d|%d Offset = %d\n",
	   rvlink->next_allocset_vpid.volid,
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
  allocset->num_pages = rvpgs->num_pages;
  allocset->num_holes = rvpgs->num_holes;

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

  fprintf (fp, " Num_user_pages = %d, Current_sectid = %d\n",
	   rvpgs->num_pages, rvpgs->num_holes);
  fprintf (fp, " Num_entries_to_compact = %d\n", rvpgs->num_holes);
  fprintf (fp, "Page Table   (End): Page = %d|%d Offset = %d\n",
	   rvpgs->end_pages_vpid.volid, rvpgs->end_pages_vpid.pageid,
	   rvpgs->end_pages_offset);
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

  fhdr->num_user_pages = *(INT32 *) rcv->data;

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
  fprintf (fp, "Num_user_pages = %d\n", *(INT32 *) data);
}

/*
 * file_rv_fhdr_undoredo_mark_deleted_pages () - Undo/Redo mark deleted page info at
 *                                    file header
 *   return: NO_ERROR
 *   rcv(in): Recovery structure
 */
int
file_rv_fhdr_undoredo_mark_deleted_pages (THREAD_ENTRY * thread_p,
					  LOG_RCV * rcv)
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
 * file_rv_fhdr_dump_mark_deleted_pages () - Dump undo/Redo information for mark
 *                                         deleted pages at header
 *   return: void
 *   length_ignore(in): Length of Recovery Data
 *   data(in): The data being logged
 */
void
file_rv_fhdr_dump_mark_deleted_pages (FILE * fp, int length_ignore,
				      void *data)
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
file_rv_allocset_undoredo_delete_pages (THREAD_ENTRY * thread_p,
					LOG_RCV * rcv)
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
      rv_pages = (FILE_RECV_DELETE_PAGES *) rcv->data;
      fhdr->num_user_pages_mrkdelete -= rv_pages->deleted_npages;

      if (rv_pages->need_compaction == 1)
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

  fprintf (fp,
	   "Sector Table  (Start): Page = %d|%d, Offset = %d\n",
	   rvsect->start_sects_vpid.volid,
	   rvsect->start_sects_vpid.pageid, rvsect->start_sects_offset);
  fprintf (fp, "               (End): Page = %d|%d, Offset = %d\n",
	   rvsect->end_sects_vpid.volid, rvsect->end_sects_vpid.pageid,
	   rvsect->end_sects_offset);

}

/*
 * file_rv_descriptor_undoredo_firstrest_nextvpid () - UNDO/REDO next vpid filed for file
 *                                    descriptor of header page
 *   return: NO_ERROR
 *   rcv(in): Recovery structure
 */
int
file_rv_descriptor_undoredo_firstrest_nextvpid (THREAD_ENTRY * thread_p,
						LOG_RCV * rcv)
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
file_rv_descriptor_dump_firstrest_nextvpid (FILE * fp, int length_ignore,
					    void *data)
{
  VPID *vpid;

  vpid = (VPID *) data;
  fprintf (fp, "First Rest of file Desc VPID: %d|%d",
	   vpid->volid, vpid->pageid);
}

/*
 * file_rv_descriptor_undoredo_nrest_nextvpid () - UNDO/REDO next vpid filed for file descriptor
 *                                of a rest page
 *   return: NO_ERROR
 *   rcv(in): Recovery structure
 */
int
file_rv_descriptor_undoredo_nrest_nextvpid (THREAD_ENTRY * thread_p,
					    LOG_RCV * rcv)
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
file_rv_descriptor_dump_nrest_nextvpid (FILE * fp, int length_ignore,
					void *data)
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

  file_Tempfile_cache.entry = (FILE_TEMPFILE_CACHE_ENTRY *)
    malloc (sizeof (FILE_TEMPFILE_CACHE_ENTRY) *
	    prm_get_integer_value (PRM_ID_MAX_ENTRIES_IN_TEMP_FILE_CACHE));
  if (file_Tempfile_cache.entry == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      sizeof (FILE_TEMPFILE_CACHE_ENTRY) *
	      prm_get_integer_value (PRM_ID_MAX_ENTRIES_IN_TEMP_FILE_CACHE));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  for (i = 0;
       i < prm_get_integer_value (PRM_ID_MAX_ENTRIES_IN_TEMP_FILE_CACHE) - 1;
       i++)
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

  return NO_ERROR;
}

/*
 * file_tmpfile_cache_get () -
 *   return:
 *   vfid(in):
 *   file_type(in):
 */
static VFID *
file_tmpfile_cache_get (THREAD_ENTRY * thread_p, VFID * vfid,
			FILE_TYPE file_type)
{
  FILE_TEMPFILE_CACHE_ENTRY *p;
  int idx, prev;

  csect_enter (thread_p, CSECT_TEMPFILE_CACHE, INF_WAIT);

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
 * ffileut_tempfile_into_cache () -
 *   return:
 *   vfid(in):
 *   file_type(in):
 */
static int
file_tmpfile_cache_put (THREAD_ENTRY * thread_p, const VFID * vfid,
			FILE_TYPE file_type)
{
  FILE_TEMPFILE_CACHE_ENTRY *p = NULL;

  csect_enter (thread_p, CSECT_TEMPFILE_CACHE, INF_WAIT);

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
    case FILE_LONGDATA:
      return sizeof (FILE_LO_DES);
    case FILE_DROPPED_FILES:
    case FILE_VACUUM_DATA:
    case FILE_TRACKER:
    case FILE_CATALOG:
    case FILE_QUERY_AREA:
    case FILE_TMP:
    case FILE_TMP_TMP:
    case FILE_EITHER_TMP:
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
file_descriptor_dump (THREAD_ENTRY * thread_p, FILE * fp,
		      const FILE_TYPE file_type,
		      const void *file_des_p, const VFID * vfid)
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
      file_descriptor_dump_heap (thread_p, fp,
				 (const FILE_HEAP_DES *) file_des_p);
      break;

    case FILE_MULTIPAGE_OBJECT_HEAP:
      file_descriptor_dump_multi_page_object_heap (fp,
						   (const FILE_OVF_HEAP_DES *)
						   file_des_p);
      break;

    case FILE_BTREE:
      {
	const FILE_BTREE_DES *btree_des_p = (FILE_BTREE_DES *) file_des_p;

	file_print_class_name_index_name_with_attrid (thread_p, fp,
						      &btree_des_p->class_oid,
						      vfid,
						      btree_des_p->attr_id);
	break;
      }

    case FILE_BTREE_OVERFLOW_KEY:
      file_descriptor_dump_btree_overflow_key_file_des (fp,
							(const
							 FILE_OVF_BTREE_DES *)
							file_des_p);
      break;

    case FILE_EXTENDIBLE_HASH:
    case FILE_EXTENDIBLE_HASH_DIRECTORY:
      {
	const FILE_EHASH_DES *ext_hash_des_p = (FILE_EHASH_DES *) file_des_p;

	file_print_name_of_class_with_attrid (thread_p, fp,
					      &ext_hash_des_p->class_oid,
					      ext_hash_des_p->attr_id);
	break;
      }
    case FILE_LONGDATA:
      {
	const FILE_LO_DES *lo_des_p = (FILE_LO_DES *) file_des_p;

	file_print_class_name_of_instance (thread_p, fp, &lo_des_p->oid);
	break;
      }
    case FILE_CATALOG:
    case FILE_QUERY_AREA:
    case FILE_TMP:
    case FILE_TMP_TMP:
    case FILE_EITHER_TMP:
    case FILE_UNKNOWN_TYPE:
    case FILE_DROPPED_FILES:
    case FILE_VACUUM_DATA:
    default:
      fprintf (fp, "....Don't know how to dump desc..\n");
      break;
    }
}

static void
file_descriptor_dump_heap (THREAD_ENTRY * thread_p, FILE * fp,
			   const FILE_HEAP_DES * heap_file_des_p)
{
  file_print_name_of_class (thread_p, fp, &heap_file_des_p->class_oid);
}

static void
file_descriptor_dump_multi_page_object_heap (FILE * fp,
					     const FILE_OVF_HEAP_DES *
					     ovf_hfile_des_p)
{
  fprintf (fp, "Overflow for HFID: %2d|%4d|%4d\n",
	   ovf_hfile_des_p->hfid.vfid.volid,
	   ovf_hfile_des_p->hfid.vfid.fileid, ovf_hfile_des_p->hfid.hpgid);
}

static void
file_descriptor_dump_btree_overflow_key_file_des (FILE * fp,
						  const FILE_OVF_BTREE_DES *
						  btree_ovf_des_p)
{
  fprintf (fp, "Overflow keys for BTID: %2d|%4d|%4d\n",
	   btree_ovf_des_p->btid.vfid.volid,
	   btree_ovf_des_p->btid.vfid.fileid,
	   btree_ovf_des_p->btid.root_pageid);
}

static void
file_print_name_of_class (THREAD_ENTRY * thread_p, FILE * fp,
			  const OID * class_oid_p)
{
  char *class_name_p = NULL;

  if (!OID_ISNULL (class_oid_p))
    {
      class_name_p = heap_get_class_name (thread_p, class_oid_p);
      fprintf (fp, "CLASS_OID:%2d|%4d|%2d (%s)\n",
	       class_oid_p->volid, class_oid_p->pageid, class_oid_p->slotid,
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
file_print_class_name_of_instance (THREAD_ENTRY * thread_p, FILE * fp,
				   const OID * inst_oid_p)
{
  char *class_name_p = NULL;

  if (!OID_ISNULL (inst_oid_p))
    {
      class_name_p = heap_get_class_name_of_instance (thread_p, inst_oid_p);
      fprintf (fp, "CLASS_OID:%2d|%4d|%2d (%s)\n",
	       inst_oid_p->volid, inst_oid_p->pageid, inst_oid_p->slotid,
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
file_print_name_of_class_with_attrid (THREAD_ENTRY * thread_p, FILE * fp,
				      const OID * class_oid_p,
				      const int attr_id)
{
  char *class_name_p = NULL;

  if (!OID_ISNULL (class_oid_p))
    {
      class_name_p = heap_get_class_name (thread_p, class_oid_p);
      fprintf (fp, "CLASS_OID:%2d|%4d|%2d (%s), ATTRID: %2d\n",
	       class_oid_p->volid, class_oid_p->pageid, class_oid_p->slotid,
	       (class_name_p) ? class_name_p : "*UNKNOWN-CLASS*", attr_id);
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
file_print_class_name_index_name_with_attrid (THREAD_ENTRY * thread_p,
					      FILE * fp,
					      const OID * class_oid_p,
					      const VFID * vfid,
					      const int attr_id)
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

  /* index_name
   * root_pageid doesn't matter here, see BTID_IS_EQUAL
   */
  btid.vfid = *vfid;
  btid.root_pageid = NULL_PAGEID;
  if (heap_get_indexinfo_of_btid (thread_p, class_oid_p, &btid,
				  NULL, NULL, NULL, NULL, &index_name_p,
				  NULL) != NO_ERROR)
    {
      goto end;
    }

  /* print */
  fprintf (fp, "CLASS_OID:%2d|%4d|%2d (%s), %s, ATTRID: %2d",
	   class_oid_p->volid, class_oid_p->pageid, class_oid_p->slotid,
	   (class_name_p == NULL) ? "*UNKNOWN-CLASS*" : class_name_p,
	   (index_name_p == NULL) ? "*UNKNOWN-INDEX*" : index_name_p,
	   attr_id);

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
  DISK_ISVALID allvalid = DISK_VALID;
  DISK_ISVALID valid = DISK_VALID;
  PAGE_PTR vhdr_pgptr;
  DISK_VAR_HEADER *vhdr;
  int num_files, num_found;
  VPID set_vpids[FILE_SET_NUMVPIDS], vpid;
  VFID vfid;
  int i, j, nperm_vols;
  VOL_SPACE_INFO *space_info = NULL;

  if (file_Tracker->vfid == NULL)
    {
      return DISK_ERROR;
    }

  nperm_vols = xboot_find_number_permanent_volumes (thread_p);
  if (nperm_vols <= 0)
    {
      return DISK_ERROR;
    }

  space_info = (VOL_SPACE_INFO *) calloc (nperm_vols,
					  sizeof (VOL_SPACE_INFO));
  if (space_info == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, sizeof (VOL_SPACE_INFO) * nperm_vols);
      return DISK_ERROR;
    }

  /* phase 1. construct space_info with file tracker */
  num_files = file_get_numpages (thread_p, file_Tracker->vfid);
  for (i = 0; i < num_files && allvalid != DISK_ERROR; i += num_found)
    {
      num_found = file_find_nthpages (thread_p, file_Tracker->vfid,
				      &set_vpids[0], i,
				      ((num_files - i < FILE_SET_NUMVPIDS)
				       ? num_files - i : FILE_SET_NUMVPIDS));
      if (num_found <= 0)
	{
	  allvalid = DISK_ERROR;
	  break;
	}

      for (j = 0; j < num_found && allvalid != DISK_ERROR; j++)
	{
	  vfid.volid = set_vpids[j].volid;
	  vfid.fileid = set_vpids[j].pageid;

	  valid = file_construct_space_info (thread_p, space_info,
					     &vfid, nperm_vols);
	  if (valid != DISK_VALID)
	    {
	      allvalid = valid;
	    }
	}
    }

  if (allvalid != DISK_ERROR && i != num_files)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FILE_MISMATCH_NFILES, 2,
	      num_files, i);
      allvalid = DISK_ERROR;
    }

  for (i = 0; i < nperm_vols; i++)
    {
      vpid.volid = i + LOG_DBFIRST_VOLID;
      vpid.pageid = DISK_VOLHEADER_PAGE;

      vhdr_pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ,
			      PGBUF_UNCONDITIONAL_LATCH);
      if (vhdr_pgptr == NULL)
	{
	  allvalid = DISK_ERROR;
	  goto end;
	}

      (void) pgbuf_check_page_ptype (thread_p, vhdr_pgptr, PAGE_VOLHEADER);

      vhdr = (DISK_VAR_HEADER *) vhdr_pgptr;

      vhdr->used_data_npages = space_info[i].used_data_npages;
      vhdr->used_index_npages = space_info[i].used_index_npages;

      if (vhdr->purpose == DISK_PERMVOL_DATA_PURPOSE)
	{
	  if (space_info[i].used_index_npages != 0
	      || (space_info[i].used_data_npages + vhdr->sys_lastpage + 1
		  != vhdr->total_pages - vhdr->free_pages))
	    {
	      /* Inconsistency is found */
	      assert_release (space_info[i].used_index_npages == 0
			      && (space_info[i].used_data_npages +
				  vhdr->sys_lastpage + 1 ==
				  vhdr->total_pages - vhdr->free_pages));

	      pgbuf_unfix_and_init (thread_p, vhdr_pgptr);

	      allvalid = DISK_ERROR;
	      goto end;
	    }
	}
      else if (vhdr->purpose == DISK_PERMVOL_INDEX_PURPOSE)
	{
	  if (space_info[i].used_data_npages != 0
	      || (space_info[i].used_index_npages + vhdr->sys_lastpage + 1
		  != vhdr->total_pages - vhdr->free_pages))
	    {
	      /* Inconsistency is found */
	      assert_release (space_info[i].used_data_npages == 0
			      && (space_info[i].used_index_npages +
				  vhdr->sys_lastpage + 1 ==
				  vhdr->total_pages - vhdr->free_pages));

	      pgbuf_unfix_and_init (thread_p, vhdr_pgptr);

	      allvalid = DISK_ERROR;
	      goto end;
	    }
	}
      else if (vhdr->purpose == DISK_PERMVOL_GENERIC_PURPOSE)
	{
	  if ((space_info[i].used_data_npages +
	       space_info[i].used_index_npages +
	       vhdr->sys_lastpage + 1)
	      != vhdr->total_pages - vhdr->free_pages)
	    {
	      /* Inconsistency is found */
	      assert_release ((space_info[i].used_data_npages +
			       space_info[i].used_index_npages +
			       vhdr->sys_lastpage + 1)
			      == vhdr->total_pages - vhdr->free_pages);

	      pgbuf_unfix_and_init (thread_p, vhdr_pgptr);

	      allvalid = DISK_ERROR;
	      goto end;
	    }
	}
      else
	{
	  pgbuf_unfix_and_init (thread_p, vhdr_pgptr);
	  continue;
	}

      pgbuf_set_dirty (thread_p, vhdr_pgptr, FREE);
    }

end:
  if (space_info)
    {
      free_and_init (space_info);
    }

  return allvalid;
#else /* !SA_MODE */
  return DISK_ERROR;
#endif /* !SA_MODE */
}

/*
 * file_construct_space_info () -
 *   return: either: DISK_INVALID, DISK_VALID, DISK_ERROR
 *   space_info(out):
 *   vfid(in):
 *   nperm_vols(in):
 */
static DISK_ISVALID
file_construct_space_info (THREAD_ENTRY * thread_p,
			   VOL_SPACE_INFO * space_info,
			   const VFID * vfid, int nperm_vols)
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

  if (space_info == NULL)
    {
      assert (space_info != NULL);
      return DISK_ERROR;
    }

  set_vpids[0].volid = vfid->volid;
  set_vpids[0].pageid = vfid->fileid;

  fhdr_pgptr = pgbuf_fix (thread_p, &set_vpids[0], OLD_PAGE, PGBUF_LATCH_READ,
			  PGBUF_UNCONDITIONAL_LATCH);
  if (fhdr_pgptr == NULL)
    {
      return DISK_ERROR;
    }

  (void) pgbuf_check_page_ptype (thread_p, fhdr_pgptr, PAGE_FTAB);

  fhdr = (FILE_HEADER *) (fhdr_pgptr + FILE_HEADER_OFFSET);

  page_type = file_get_disk_page_type (fhdr->type);
  if (page_type != DISK_PAGE_DATA_TYPE && page_type != DISK_PAGE_INDEX_TYPE)
    {
      goto end;
    }

  /* 1. set file header page to space_info */
  if (page_type == DISK_PAGE_DATA_TYPE)
    {
      space_info[vfid->volid].used_data_npages++;
    }
  else if (page_type == DISK_PAGE_INDEX_TYPE)
    {
      space_info[vfid->volid].used_index_npages++;
    }

  /* 2. set file table pages to space_info */
  if (file_ftabvpid_next (fhdr, fhdr_pgptr, &next_ftable_vpid) != NO_ERROR)
    {
      valid = DISK_ERROR;
      goto end;
    }

  /* next pages */
  while (!VPID_ISNULL (&next_ftable_vpid))
    {
      if (page_type == DISK_PAGE_DATA_TYPE)
	{
	  space_info[next_ftable_vpid.volid].used_data_npages++;
	}
      else if (page_type == DISK_PAGE_INDEX_TYPE)
	{
	  space_info[next_ftable_vpid.volid].used_index_npages++;
	}

      pgptr = pgbuf_fix (thread_p, &next_ftable_vpid, OLD_PAGE,
			 PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
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

  if (VFID_EQ (vfid, file_Tracker->vfid))
    {
      /* This file is tracker file.
       * User pages in this file will be added later as a file header page.
       */
      goto end;
    }

  num_user_pages = fhdr->num_user_pages;

  /* 3. set user pages */
  for (i = 0; i < num_user_pages; i += num_found)
    {
      num_found =
	file_find_nthpages (thread_p, vfid, &set_vpids[0], i,
			    ((num_user_pages - i < FILE_SET_NUMVPIDS)
			     ? (num_user_pages - i) : FILE_SET_NUMVPIDS));
      if (num_found <= 0)
	{
	  valid = DISK_ERROR;
	  goto end;
	}

      for (j = 0; j < num_found; j++)
	{
	  if (page_type == DISK_PAGE_DATA_TYPE)
	    {
	      space_info[set_vpids[j].volid].used_data_npages++;
	    }
	  else if (page_type == DISK_PAGE_INDEX_TYPE)
	    {
	      space_info[set_vpids[j].volid].used_index_npages++;
	    }
	}
    }

end:
  if (fhdr_pgptr)
    {
      pgbuf_unfix_and_init (thread_p, fhdr_pgptr);
    }

  return valid;
#else /* !SA_MODE */
  return DISK_ERROR;
#endif /* !SA_MODE */
}
