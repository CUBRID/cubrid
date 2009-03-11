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
  FILE_UNKNOWN_TYPE
} FILE_TYPE;

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

extern int file_manager_initialize (THREAD_ENTRY * thread_p);
extern int file_manager_finalize (THREAD_ENTRY * thread_p);

extern DISK_ISVALID file_new_isvalid (THREAD_ENTRY * thread_p,
				      const VFID * vfid);
extern DISK_ISVALID file_new_isvalid_with_has_undolog (THREAD_ENTRY *
						       thread_p,
						       const VFID * vfid,
						       bool * has_undolog);
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
extern VFID *file_create_queryarea (THREAD_ENTRY * thread_p, VFID * vfid,
				    INT32 exp_numpages, const void *file_des);
extern int file_create_hint_numpages (THREAD_ENTRY * thread_p,
				      INT32 exp_numpages,
				      FILE_TYPE file_type);
extern int file_destroy (THREAD_ENTRY * thread_p, const VFID * vfid);
extern int file_mark_as_deleted (THREAD_ENTRY * thread_p, const VFID * vfid);
extern bool file_does_marked_as_deleted (THREAD_ENTRY * thread_p,
					 const VFID * vfid);
extern FILE_TYPE file_get_type (THREAD_ENTRY * thread_p, const VFID * vfid);
extern int file_get_descriptor (THREAD_ENTRY * thread_p, const VFID * vfid,
				void *area_des, int maxsize);
extern INT32 file_get_numpages (THREAD_ENTRY * thread_p, const VFID * vfid);
extern INT32 file_get_numpages_overhead (THREAD_ENTRY * thread_p,
					 const VFID * vfid);
extern INT32 file_get_numpages_plus_numpages_overhead (THREAD_ENTRY *
						       thread_p,
						       const VFID * vfid,
						       INT32 * numpages,
						       INT32 *
						       overhead_numpages);
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
extern DISK_ISVALID file_isvalid (THREAD_ENTRY * thread_p, const VFID * vfid);
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
							     const VPID *
							     first_alloc_vpid,
							     const INT32 *
							     first_alloc_nthpage,
							     INT32 npages,
							     void *args),
						void *args);
extern VPID *file_alloc_pages_at_volid (THREAD_ENTRY * thread_p,
					const VFID * vfid,
					VPID * first_alloc_vpid, INT32 npages,
					const VPID * near_vpid,
					INT16 desired_volid,
					bool (*fun) (const VFID * vfid,
						     const VPID *
						     first_alloc_vpid,
						     INT32 npages,
						     void *args), void *args);
extern int file_dealloc_page (THREAD_ENTRY * thread_p, const VFID * vfid,
			      VPID * dealloc_vpid);
extern int file_truncate_to_numpages (THREAD_ENTRY * thread_p,
				      const VFID * vfid,
				      INT32 keep_first_npages);

extern int file_tracker_cache_vfid (VFID * vfid);
extern VFID *file_tracker_create (THREAD_ENTRY * thread_p, VFID * vfid);
extern int file_tracker_compress (THREAD_ENTRY * thread_p);

extern int file_typecache_clear (void);

/* This are for debugging purposes */
extern int file_dump (THREAD_ENTRY * thread_p, const VFID * vfid);
extern int file_tracker_dump (THREAD_ENTRY * thread_p);
extern DISK_ISVALID file_tracker_check (THREAD_ENTRY * thread_p);
extern int file_dump_all_capacities (THREAD_ENTRY * thread_p);
extern int file_dump_descriptor (THREAD_ENTRY * thread_p, const VFID * vfid);

extern int file_rv_undo_create_tmp (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern void file_rv_dump_create_tmp (int length_ignore, void *data);

extern void file_rv_dump_ftab_chain (int length_ignore, void *data);
extern void file_rv_dump_fhdr (int length_ignore, void *data);
extern void file_rv_dump_idtab (int length, void *data);
extern void file_rv_dump_allocset (int length_ignore, void *data);

extern int file_rv_undoredo_mark_as_deleted (THREAD_ENTRY * thread_p,
					     LOG_RCV * rcv);
extern void file_rv_dump_marked_as_deleted (int length_ignore, void *data);

extern int file_rv_fhdr_undoredo_expansion (THREAD_ENTRY * thread_p,
					    LOG_RCV * rcv);
extern void file_rv_fhdr_dump_expansion (int length_ignore, void *data);
extern int file_rv_fhdr_add_last_allocset (THREAD_ENTRY * thread_p,
					   LOG_RCV * rcv);
extern int file_rv_fhdr_remove_last_allocset (THREAD_ENTRY * thread_p,
					      LOG_RCV * rcv);
extern int file_rv_fhdr_change_last_allocset (THREAD_ENTRY * thread_p,
					      LOG_RCV * rcv);
extern void file_rv_fhdr_dump_last_allocset (int length_ignore, void *data);
extern int file_rv_fhdr_undoredo_add_pages (THREAD_ENTRY * thread_p,
					    LOG_RCV * rcv);
extern void file_rv_fhdr_dump_add_pages (int length_ignore, void *data);
extern int file_rv_fhdr_delete_pages (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern void file_rv_fhdr_delete_pages_dump (int length_ignore, void *data);
extern int file_rv_fhdr_undoredo_mark_deleted_pages (THREAD_ENTRY * thread_p,
						     LOG_RCV * rcv);
extern void file_rv_fhdr_dump_mark_deleted_pages (int length_ignore,
						  void *data);

extern int file_rv_allocset_undoredo_sector (THREAD_ENTRY * thread_p,
					     LOG_RCV * rcv);
extern void file_rv_allocset_dump_sector (int length_ignore, void *data);
extern int file_rv_allocset_undoredo_page (THREAD_ENTRY * thread_p,
					   LOG_RCV * rcv);
extern void file_rv_allocset_dump_page (int length_ignore, void *data);
extern int file_rv_allocset_undoredo_link (THREAD_ENTRY * thread_p,
					   LOG_RCV * rcv);
extern void file_rv_allocset_dump_link (int length_ignore, void *data);
extern int file_rv_allocset_undoredo_add_pages (THREAD_ENTRY * thread_p,
						LOG_RCV * rcv);
extern void file_rv_allocset_dump_add_pages (int length_ignore, void *data);
extern int file_rv_allocset_undoredo_delete_pages (THREAD_ENTRY * thread_p,
						   LOG_RCV * rcv);
extern void file_rv_allocset_dump_delete_pages (int length_ignore,
						void *data);
extern int file_rv_allocset_undoredo_sectortab (THREAD_ENTRY * thread_p,
						LOG_RCV * rcv);
extern void file_rv_allocset_dump_sectortab (int length_ignore, void *data);

extern int file_rv_descriptor_undoredo_firstrest_nextvpid (THREAD_ENTRY *
							   thread_p,
							   LOG_RCV * rcv);
extern void file_rv_descriptor_dump_firstrest_nextvpid (int length_ignore,
							void *data);
extern int file_rv_descriptor_undoredo_nrest_nextvpid (THREAD_ENTRY *
						       thread_p,
						       LOG_RCV * rcv);
extern void file_rv_descriptor_dump_nrest_nextvpid (int length_ignore,
						    void *data);

extern int file_rv_tracker_undo_register (THREAD_ENTRY * thread_p,
					  LOG_RCV * rcv);
extern void file_rv_tracker_dump_undo_register (int length_ignore,
						void *data);

extern int file_rv_logical_redo_nop (THREAD_ENTRY * thread_p, LOG_RCV * recv);

#endif /* _FILE_MANAGER_H_ */
