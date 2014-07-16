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
 * file_manager.h - interface for file manager
 */

#ifndef _FILE_MANAGER_H_
#define _FILE_MANAGER_H_

#ident "$Id$"

#include "config.h"

#include "storage_common.h"
#include "disk_manager.h"
#include "log_manager.h"
#include "oid.h"

#define FILE_DUMP_DES_AREA_SIZE 100

typedef enum
{
  FILE_TRACKER,
  FILE_HEAP,
  FILE_MULTIPAGE_OBJECT_HEAP,
  FILE_BTREE,
  FILE_BTREE_OVERFLOW_KEY,
  FILE_EXTENDIBLE_HASH,
  FILE_EXTENDIBLE_HASH_DIRECTORY,
  FILE_LONGDATA,
  FILE_CATALOG,
  FILE_QUERY_AREA,
  FILE_TMP,
  FILE_TMP_TMP,
  FILE_EITHER_TMP,
  FILE_UNKNOWN_TYPE,
  FILE_HEAP_REUSE_SLOTS,
  FILE_LAST = FILE_HEAP_REUSE_SLOTS
} FILE_TYPE;

typedef enum
{
  FILE_OLD_FILE,
  FILE_NEW_FILE,
  FILE_ERROR
} FILE_IS_NEW_FILE;

/* Set a vfid with values of volid and fileid */
#define VFID_SET(vfid_ptr, volid_value, fileid_value) \
  do {						      \
    (vfid_ptr)->volid  = (volid_value);		      \
    (vfid_ptr)->fileid = (fileid_value);	      \
  } while(0)

/* Set the vpid to an invalid one */
#define VFID_SET_NULL(vfid_ptr) VFID_SET(vfid_ptr, NULL_VOLID, NULL_FILEID)

/* Copy a vfid1 with the values of another vfid2 */
#define VFID_COPY(vfid_ptr1, vfid_ptr2) *(vfid_ptr1) = *(vfid_ptr2)

#define VFID_ISNULL(vfid_ptr) ((vfid_ptr)->fileid == NULL_FILEID)

#define VFID_EQ(vfid_ptr1, vfid_ptr2) \
  ((vfid_ptr1) == (vfid_ptr2) || \
   ((vfid_ptr1)->fileid == (vfid_ptr2)->fileid && \
    (vfid_ptr1)->volid  == (vfid_ptr2)->volid))

/* Heap file descriptor */
typedef struct file_heap_des FILE_HEAP_DES;
struct file_heap_des
{
  OID class_oid;
};

/* Overflow heap file descriptor */
typedef struct file_ovf_heap_des FILE_OVF_HEAP_DES;
struct file_ovf_heap_des
{
  HFID hfid;
};

/* Btree file descriptor */
typedef struct file_btree_des FILE_BTREE_DES;
struct file_btree_des
{
  OID class_oid;
  int attr_id;
};

/* Overflow key file descriptor */
typedef struct file_ovf_btree_des FILE_OVF_BTREE_DES;
struct file_ovf_btree_des
{
  BTID btid;
};

/* Extendible Hash file descriptor */
typedef struct file_ehash_des FILE_EHASH_DES;
struct file_ehash_des
{
  OID class_oid;
  int attr_id;
};

/* LO file descriptor */
typedef struct file_lo_des FILE_LO_DES;
struct file_lo_des
{
  OID oid;
};

/*
 * Description of allocated sectors and pages for a volume. Note that the same
 * volume can be repeated in different sets of allocation. This is needed since
 * we need to record the pages in the order they were allocated
 */
typedef struct file_allocset FILE_ALLOCSET;
struct file_allocset
{
  INT16 volid;			/* Volume identifier */
  int num_sects;		/* Number of sectors */
  int num_pages;		/* Number of pages */
  INT32 curr_sectid;		/* Sector on which last page was allocated.
				   It is used as a guess for next page
				   allocation */
  int num_holes;		/* Indicate the number of identifiers (pages
				   or slots) that can be compacted */
  VPID start_sects_vpid;	/* Starting vpid address for sector table for
				   this allocation set. See below for offset. */
  INT16 start_sects_offset;	/* Offset where the sector table starts at the
				   starting vpid. */
  VPID end_sects_vpid;		/* Ending vpid address for sector table for
				   this allocation set. See below for offset. */
  INT16 end_sects_offset;	/* Offset where the sector table ends at
				   ending vpid. */
  VPID start_pages_vpid;	/* Starting vpid address for page table for
				   this allocation set. See below for offset. */
  INT16 start_pages_offset;	/* Offset where the page table starts at the
				   starting vpid. */
  VPID end_pages_vpid;		/* Ending vpid address for page table for this
				   allocation set. See below for offset. */
  INT16 end_pages_offset;	/* Offset where the page table ends at ending
				   vpid. */
  VPID next_allocset_vpid;	/* Address of next allocation set */
  INT16 next_allocset_offset;	/* Offset of next allocation set at vpid */
};

/* File header */
typedef struct file_header FILE_HEADER;
struct file_header
{
  VFID vfid;			/* The File identifier itself, this is used for
				   debugging purposes. */
  INT64 creation;		/* Time of the creation of the file */
  INT16 type;			/* Type of the file such as Heap, B+tree,
				   Extendible hashing, etc */
  INT16 ismark_as_deleted;	/* Is the file marked as deleted ? */
  int num_table_vpids;		/* Number of total pages for file table. The
				   file header and the arrays describing the
				   allocated pages reside in these pages */
  VPID next_table_vpid;		/* Next file table page */
  VPID last_table_vpid;		/* Last file table page */
  int num_user_pages;		/* Number of user allocated pages. It does not
				   include the file table pages */
  int num_user_pages_mrkdelete;	/* Num marked deleted pages */
  int num_allocsets;		/* Number of volume arrays. Each volume array
				   contains information of the volume
				   identifier and the allocated sectors and
				   pages */
  FILE_ALLOCSET allocset;	/* The first allocation set */
  VPID last_allocset_vpid;	/* Address of last allocation set */
  INT16 last_allocset_offset;	/* Offset of last allocation set at vpid */

  struct
  {
    int total_length;		/* Total length of description of file */
    int first_length;		/* Length of the first part */
    VPID next_part_vpid;	/* Location of the rest of file description
				   comments */
    char piece[1];		/* Really more than one */
  } des;
};

/* buffer for saving allocated vpids */
typedef struct file_alloc_vpids FILE_ALLOC_VPIDS;
struct file_alloc_vpids
{
  VPID *vpids;
  int index;
};

extern int file_manager_initialize (THREAD_ENTRY * thread_p);
extern int file_manager_finalize (THREAD_ENTRY * thread_p);

extern FILE_IS_NEW_FILE file_is_new_file (THREAD_ENTRY * thread_p,
					  const VFID * vfid);
extern FILE_IS_NEW_FILE file_is_new_file_with_has_undolog (THREAD_ENTRY *
							   thread_p,
							   const VFID * vfid,
							   bool *
							   has_undolog);
extern int file_new_declare_as_old (THREAD_ENTRY * thread_p,
				    const VFID * vfid);
extern int file_new_set_has_undolog (THREAD_ENTRY * thread_p,
				     const VFID * vfid);
extern int file_new_destroy_all_tmp (THREAD_ENTRY * thread_p,
				     FILE_TYPE tmp_type);

extern VFID *file_create (THREAD_ENTRY * thread_p, VFID * vfid,
			  INT32 exp_numpages, FILE_TYPE file_type,
			  const void *file_des, VPID * first_prealloc_vpid,
			  INT32 prealloc_npages);
extern VFID *file_create_tmp (THREAD_ENTRY * thread_p, VFID * vfid,
			      INT32 exp_numpages, const void *file_des);
extern VFID *file_create_tmp_no_cache (THREAD_ENTRY * thread_p, VFID * vfid,
				       INT32 exp_numpages,
				       const void *file_des);
extern VFID *file_create_queryarea (THREAD_ENTRY * thread_p, VFID * vfid,
				    INT32 exp_numpages, const void *file_des);
extern int file_create_hint_numpages (THREAD_ENTRY * thread_p,
				      INT32 exp_numpages,
				      FILE_TYPE file_type);
extern int file_preserve_temporary (THREAD_ENTRY * thread_p,
				    const VFID * vfid);
extern int file_destroy (THREAD_ENTRY * thread_p, const VFID * vfid);
extern int file_mark_as_deleted (THREAD_ENTRY * thread_p, const VFID * vfid);
extern bool file_does_marked_as_deleted (THREAD_ENTRY * thread_p,
					 const VFID * vfid);
extern FILE_TYPE file_get_type (THREAD_ENTRY * thread_p, const VFID * vfid);
extern int file_get_descriptor (THREAD_ENTRY * thread_p, const VFID * vfid,
				void *area_des, int maxsize);
extern INT32 file_get_numpages (THREAD_ENTRY * thread_p, const VFID * vfid);
#if defined (ENABLE_UNUSED_FUNCTION)
extern INT32 file_get_numpages_overhead (THREAD_ENTRY * thread_p,
					 const VFID * vfid);
extern INT32 file_get_numpages_plus_numpages_overhead (THREAD_ENTRY *
						       thread_p,
						       const VFID * vfid,
						       INT32 * numpages,
						       INT32 *
						       overhead_numpages);
#endif
extern int file_get_numfiles (THREAD_ENTRY * thread_p);
extern INT32 file_guess_numpages_overhead (THREAD_ENTRY * thread_p,
					   const VFID * vfid, INT32 npages);
extern int file_find_nthpages (THREAD_ENTRY * thread_p, const VFID * vfid,
			       VPID * nth_vpids, INT32 start_nthpage,
			       INT32 num_desired_pages);
extern VPID *file_find_last_page (THREAD_ENTRY * thread_p, const VFID * vfid,
				  VPID * last_vpid);
extern INT32 file_find_maxpages_allocable (THREAD_ENTRY * thread_p,
					   const VFID * vfid);
extern int file_find_nthfile (THREAD_ENTRY * thread_p, VFID * vfid,
			      int nthfile);
#if defined(CUBRID_DEBUG)
extern DISK_ISVALID file_isvalid (THREAD_ENTRY * thread_p, const VFID * vfid);
#endif
extern DISK_ISVALID file_isvalid_page_partof (THREAD_ENTRY * thread_p,
					      const VPID * vpid,
					      const VFID * vfid);
extern VFID *file_reuse_deleted (THREAD_ENTRY * thread_p, VFID * vfid,
				 FILE_TYPE file_type, const void *file_des);
extern int file_reclaim_all_deleted (THREAD_ENTRY * thread_p);
extern VPID *file_alloc_pages (THREAD_ENTRY * thread_p, const VFID * vfid,
			       VPID * first_alloc_vpid, INT32 npages,
			       const VPID * near_vpid,
			       bool (*fun) (THREAD_ENTRY * thread_p,
					    const VFID * vfid,
					    const FILE_TYPE file_type,
					    const VPID * first_alloc_vpid,
					    INT32 npages, void *args),
			       void *args);
extern VPID *file_alloc_pages_as_noncontiguous (THREAD_ENTRY * thread_p,
						const VFID * vfid,
						VPID * first_alloc_vpid,
						INT32 * first_alloc_nthpage,
						INT32 npages,
						const VPID * near_vpid,
						bool (*fun) (THREAD_ENTRY *
							     thread_p,
							     const VFID *
							     vfid,
							     const FILE_TYPE
							     file_type,
							     const VPID *
							     first_alloc_vpid,
							     const INT32 *
							     first_alloc_nthpage,
							     INT32 npages,
							     void *args),
						void *args,
						FILE_ALLOC_VPIDS *
						alloc_vpids);
extern VPID *file_alloc_pages_at_volid (THREAD_ENTRY * thread_p,
					const VFID * vfid,
					VPID * first_alloc_vpid, INT32 npages,
					const VPID * near_vpid,
					INT16 desired_volid,
					bool (*fun) (const VFID * vfid,
						     const FILE_TYPE
						     file_type,
						     const VPID *
						     first_alloc_vpid,
						     INT32 npages,
						     void *args), void *args);
extern int file_dealloc_page (THREAD_ENTRY * thread_p, const VFID * vfid,
			      VPID * dealloc_vpid);
extern int file_truncate_to_numpages (THREAD_ENTRY * thread_p,
				      const VFID * vfid,
				      INT32 keep_first_npages);
extern DISK_ISVALID
file_update_used_pages_of_vol_header (THREAD_ENTRY * thread_p);

extern int file_tracker_cache_vfid (VFID * vfid);
extern VFID *file_get_tracker_vfid (void);
extern VFID *file_tracker_create (THREAD_ENTRY * thread_p, VFID * vfid);
extern int file_tracker_compress (THREAD_ENTRY * thread_p);

extern int file_typecache_clear (void);

/* This are for debugging purposes */
extern int file_dump (THREAD_ENTRY * thread_p, FILE * fp, const VFID * vfid);
extern int file_tracker_dump (THREAD_ENTRY * thread_p, FILE * fp);
extern DISK_ISVALID file_tracker_check (THREAD_ENTRY * thread_p);
extern bool file_tracker_is_registered_vfid (THREAD_ENTRY * thread_p,
					     const VFID * vfid);
extern DISK_ISVALID file_tracker_cross_check_with_disk_idsmap (THREAD_ENTRY *
							       thread_p);
extern int file_dump_all_capacities (THREAD_ENTRY * thread_p, FILE * fp);
extern int file_dump_descriptor (THREAD_ENTRY * thread_p, FILE * fp,
				 const VFID * vfid);
extern int file_rv_undo_create_tmp (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern void file_rv_dump_create_tmp (FILE * fp, int length_ignore,
				     void *data);
extern int file_rv_redo_ftab_chain (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern void file_rv_dump_ftab_chain (FILE * fp, int length_ignore,
				     void *data);
extern int file_rv_redo_fhdr (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern void file_rv_dump_fhdr (FILE * fp, int length_ignore, void *data);
extern void file_rv_dump_idtab (FILE * fp, int length, void *data);
extern void file_rv_dump_allocset (FILE * fp, int length_ignore, void *data);

extern int file_rv_undoredo_mark_as_deleted (THREAD_ENTRY * thread_p,
					     LOG_RCV * rcv);
extern void file_rv_dump_marked_as_deleted (FILE * fp, int length_ignore,
					    void *data);

extern int file_rv_fhdr_undoredo_expansion (THREAD_ENTRY * thread_p,
					    LOG_RCV * rcv);
extern void file_rv_fhdr_dump_expansion (FILE * fp, int length_ignore,
					 void *data);
extern int file_rv_fhdr_add_last_allocset (THREAD_ENTRY * thread_p,
					   LOG_RCV * rcv);
extern int file_rv_fhdr_remove_last_allocset (THREAD_ENTRY * thread_p,
					      LOG_RCV * rcv);
extern int file_rv_fhdr_change_last_allocset (THREAD_ENTRY * thread_p,
					      LOG_RCV * rcv);
extern void file_rv_fhdr_dump_last_allocset (FILE * fp, int length_ignore,
					     void *data);
extern int file_rv_fhdr_undoredo_add_pages (THREAD_ENTRY * thread_p,
					    LOG_RCV * rcv);
extern void file_rv_fhdr_dump_add_pages (FILE * fp, int length_ignore,
					 void *data);
extern int file_rv_fhdr_delete_pages (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern void file_rv_fhdr_delete_pages_dump (FILE * fp, int length,
					    void *data);
extern int file_rv_fhdr_undoredo_mark_deleted_pages (THREAD_ENTRY * thread_p,
						     LOG_RCV * rcv);
extern void file_rv_fhdr_dump_mark_deleted_pages (FILE * fp,
						  int length_ignore,
						  void *data);

extern int file_rv_allocset_undoredo_sector (THREAD_ENTRY * thread_p,
					     LOG_RCV * rcv);
extern void file_rv_allocset_dump_sector (FILE * fp, int length_ignore,
					  void *data);
extern int file_rv_allocset_undoredo_page (THREAD_ENTRY * thread_p,
					   LOG_RCV * rcv);
extern void file_rv_allocset_dump_page (FILE * fp, int length_ignore,
					void *data);
extern int file_rv_allocset_undoredo_link (THREAD_ENTRY * thread_p,
					   LOG_RCV * rcv);
extern void file_rv_allocset_dump_link (FILE * fp, int length_ignore,
					void *data);
extern int file_rv_allocset_undoredo_add_pages (THREAD_ENTRY * thread_p,
						LOG_RCV * rcv);
extern void file_rv_allocset_dump_add_pages (FILE * fp, int length_ignore,
					     void *data);
extern int file_rv_allocset_undoredo_delete_pages (THREAD_ENTRY * thread_p,
						   LOG_RCV * rcv);
extern void file_rv_allocset_dump_delete_pages (FILE * fp, int length_ignore,
						void *data);
extern int file_rv_allocset_undoredo_sectortab (THREAD_ENTRY * thread_p,
						LOG_RCV * rcv);
extern void file_rv_allocset_dump_sectortab (FILE * fp, int length_ignore,
					     void *data);

extern int
file_rv_descriptor_undoredo_firstrest_nextvpid (THREAD_ENTRY * thread_p,
						LOG_RCV * rcv);
extern void file_rv_descriptor_dump_firstrest_nextvpid (FILE * fp,
							int length_ignore,
							void *data);
extern int
file_rv_descriptor_undoredo_nrest_nextvpid (THREAD_ENTRY * thread_p,
					    LOG_RCV * rcv);
extern void file_rv_descriptor_dump_nrest_nextvpid (FILE * fp,
						    int length_ignore,
						    void *data);
extern int file_rv_descriptor_redo_insert (THREAD_ENTRY * thread_p,
					   LOG_RCV * rcv);

extern int file_rv_tracker_undo_register (THREAD_ENTRY * thread_p,
					  LOG_RCV * rcv);
extern void file_rv_tracker_dump_undo_register (FILE * fp, int length_ignore,
						void *data);

extern int file_rv_logical_redo_nop (THREAD_ENTRY * thread_p, LOG_RCV * recv);

#endif /* _FILE_MANAGER_H_ */
