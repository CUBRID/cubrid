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
#include "page_buffer.h"
#include "tde.h"

typedef enum
{
  FILE_TRACKER,
  FILE_HEAP,
  FILE_HEAP_REUSE_SLOTS,
  FILE_MULTIPAGE_OBJECT_HEAP,
  FILE_BTREE,
  FILE_BTREE_OVERFLOW_KEY,
  FILE_EXTENDIBLE_HASH,
  FILE_EXTENDIBLE_HASH_DIRECTORY,
  FILE_CATALOG,
  FILE_DROPPED_FILES,
  FILE_VACUUM_DATA,
  FILE_QUERY_AREA,
  FILE_TEMP,
  FILE_UNKNOWN_TYPE,
  FILE_LAST = FILE_UNKNOWN_TYPE
} FILE_TYPE;

/* Set a vfid with values of volid and fileid */
#define VFID_SET(vfid_ptr, volid_value, fileid_value) \
  do { \
    (vfid_ptr)->volid  = (volid_value); \
    (vfid_ptr)->fileid = (fileid_value); \
  } while (0)

/* Set the vpid to an invalid one */
#define VFID_SET_NULL(vfid_ptr) \
  VFID_SET (vfid_ptr, NULL_VOLID, NULL_FILEID)

/* Copy a vfid1 with the values of another vfid2 */
#define VFID_COPY(vfid_ptr1, vfid_ptr2) \
  *(vfid_ptr1) = *(vfid_ptr2)

#define VFID_ISNULL(vfid_ptr) \
  ((vfid_ptr)->fileid == NULL_FILEID)

#define VFID_EQ(vfid_ptr1, vfid_ptr2) \
  ((vfid_ptr1) == (vfid_ptr2) \
   || ((vfid_ptr1)->fileid == (vfid_ptr2)->fileid \
       && (vfid_ptr1)->volid  == (vfid_ptr2)->volid))

/* Heap file descriptor */
typedef struct file_heap_des FILE_HEAP_DES;
struct file_heap_des
{
  OID class_oid;
  HFID hfid;
};

/* Overflow heap file descriptor */
typedef struct file_ovf_heap_des FILE_OVF_HEAP_DES;
struct file_ovf_heap_des
{
  HFID hfid;
  OID class_oid;
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
  OID class_oid;
};

/* Extensible Hash file descriptor */
typedef struct file_ehash_des FILE_EHASH_DES;
struct file_ehash_des
{
  OID class_oid;
  int attr_id;
};

/* Vacuum data file descriptor */
typedef struct file_vacuum_data_des FILE_VACUUM_DATA_DES;
struct file_vacuum_data_des
{
  VPID vpid_first;
};

/* note: if you change file descriptors size, make sure to change disk compatibility version too! */
#define FILE_DESCRIPTORS_SIZE 64
typedef union file_descriptors FILE_DESCRIPTORS;
union file_descriptors
{
  FILE_HEAP_DES heap;
  FILE_OVF_HEAP_DES heap_overflow;
  FILE_BTREE_DES btree;
  FILE_OVF_BTREE_DES btree_key_overflow;	/* TODO: rename FILE_OVF_BTREE_DES */
  FILE_EHASH_DES ehash;
  FILE_VACUUM_DATA_DES vacuum_data;
  char dummy_align[FILE_DESCRIPTORS_SIZE];
};

/* FILE_TABLESPACE: defines the space usage and extensions for files */
typedef struct file_tablespace FILE_TABLESPACE;
struct file_tablespace
{
  INT64 initial_size;
  float expand_ratio;
  int expand_min_size;
  int expand_max_size;
};

typedef int (*FILE_INIT_PAGE_FUNC) (THREAD_ENTRY * thread_p, PAGE_PTR page, void *args);
typedef int (*FILE_MAP_PAGE_FUNC) (THREAD_ENTRY * thread_p, PAGE_PTR * page, bool * stop, void *args);

extern int file_manager_init (void);
extern void file_manager_final (void);

extern int file_create (THREAD_ENTRY * thread_p, FILE_TYPE file_type, FILE_TABLESPACE * tablespace,
			FILE_DESCRIPTORS * des, bool is_temp, bool is_numerable, VFID * vfid);
extern int file_create_with_npages (THREAD_ENTRY * thread_p, FILE_TYPE file_type, int npages, FILE_DESCRIPTORS * des,
				    VFID * vfid);
extern int file_create_heap (THREAD_ENTRY * thread_p, bool reuse_oid, const OID * class_oid, VFID * vfid);
extern int file_create_temp (THREAD_ENTRY * thread_p, int npages, VFID * vfid);
extern int file_create_temp_numerable (THREAD_ENTRY * thread_p, int npages, VFID * vfid);
extern int file_create_query_area (THREAD_ENTRY * thread_p, VFID * vfid);
extern int file_create_ehash (THREAD_ENTRY * thread_p, int npages, bool is_tmp, FILE_EHASH_DES * des_ehash,
			      VFID * vfid);
extern int file_create_ehash_dir (THREAD_ENTRY * thread_p, int npages, bool is_tmp, FILE_EHASH_DES * des_ehash,
				  VFID * vfid);

extern void file_postpone_destroy (THREAD_ENTRY * thread_p, const VFID * vfid);
extern int file_destroy (THREAD_ENTRY * thread_p, const VFID * vfid, bool is_temp);
extern int file_temp_retire (THREAD_ENTRY * thread_p, const VFID * vfid);
extern int file_temp_retire_preserved (THREAD_ENTRY * thread_p, const VFID * vfid);

extern int file_init_page_type (THREAD_ENTRY * thread_p, PAGE_PTR page, void *args);
extern int file_init_temp_page_type (THREAD_ENTRY * thread_p, PAGE_PTR page, void *args);
extern int file_alloc (THREAD_ENTRY * thread_p, const VFID * vfid, FILE_INIT_PAGE_FUNC f_init, void *f_init_args,
		       VPID * vpid_out, PAGE_PTR * page_out);
extern int file_alloc_multiple (THREAD_ENTRY * thread_p, const VFID * vfid, FILE_INIT_PAGE_FUNC f_init,
				void *f_init_args, int npages, VPID * vpids_out);
extern int file_alloc_sticky_first_page (THREAD_ENTRY * thread_p, const VFID * vfid, FILE_INIT_PAGE_FUNC f_init,
					 void *f_init_args, VPID * vpid_out, PAGE_PTR * page_out);
extern int file_get_sticky_first_page (THREAD_ENTRY * thread_p, const VFID * vfid, VPID * vpid_out);
extern int file_rv_set_tde_algorithm (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern int file_get_tde_algorithm (THREAD_ENTRY * thread_p, const VFID * vfid, PGBUF_LATCH_CONDITION fix_head_cond,
				   TDE_ALGORITHM * tde_algo);
extern int file_apply_tde_algorithm (THREAD_ENTRY * thread_p, const VFID * vfid, const TDE_ALGORITHM tde_algo);
extern int file_dealloc (THREAD_ENTRY * thread_p, const VFID * vfid, const VPID * vpid, FILE_TYPE file_type_hint);

extern int file_get_num_user_pages (THREAD_ENTRY * thread_p, const VFID * vfid, int *n_user_pages_out);
extern int file_get_num_total_user_pages (THREAD_ENTRY * thread_p, OID * class_oid, int *n_user_pages_out,
					  bool * partition_yn);
extern DISK_ISVALID file_check_vpid (THREAD_ENTRY * thread_p, const VFID * vfid, const VPID * vpid_lookup);
extern int file_get_type (THREAD_ENTRY * thread_p, const VFID * vfid, FILE_TYPE * ftype_out);
extern int file_is_temp (THREAD_ENTRY * thread_p, const VFID * vfid, bool * is_temp);
extern int file_map_pages (THREAD_ENTRY * thread_p, const VFID * vfid, PGBUF_LATCH_MODE latch_mode,
			   PGBUF_LATCH_CONDITION latch_cond, FILE_MAP_PAGE_FUNC func, void *args);
extern int file_dump (THREAD_ENTRY * thread_p, const VFID * vfid, FILE * fp);
extern int file_spacedb (THREAD_ENTRY * thread_p, SPACEDB_FILES * spacedb);

extern int file_numerable_find_nth (THREAD_ENTRY * thread_p, const VFID * vfid, int nth, bool auto_alloc,
				    FILE_INIT_PAGE_FUNC f_init, void *f_init_args, VPID * vpid_nth);
extern int file_numerable_truncate (THREAD_ENTRY * thread_p, const VFID * vfid, DKNPAGES npages);

extern void file_tempcache_drop_tran_temp_files (THREAD_ENTRY * thread_p);

extern void file_temp_preserve (THREAD_ENTRY * thread_p, const VFID * vfid);
extern int file_get_tran_num_temp_files (THREAD_ENTRY * thread_p);

extern int file_tracker_create (THREAD_ENTRY * thread_p, VFID * vfid_tracker_out);
extern int file_tracker_load (THREAD_ENTRY * thread_p, const VFID * vfid);
extern int file_tracker_reuse_heap (THREAD_ENTRY * thread_p, const OID * class_oid, HFID * hfid_out);
extern int file_tracker_interruptable_iterate (THREAD_ENTRY * thread_p, FILE_TYPE desired_ftype, VFID * vfid,
					       OID * class_oid);
extern DISK_ISVALID file_tracker_check (THREAD_ENTRY * thread_p);
extern int file_tracker_dump (THREAD_ENTRY * thread_p, FILE * fp);
extern int file_tracker_dump_all_capacities (THREAD_ENTRY * thread_p, FILE * fp);
extern int file_tracker_dump_all_heap (THREAD_ENTRY * thread_p, FILE * fp, bool dump_records);
extern int file_tracker_dump_all_heap_capacities (THREAD_ENTRY * thread_p, FILE * fp);
extern int file_tracker_dump_all_btree_capacities (THREAD_ENTRY * thread_p, FILE * fp);
#if defined (SA_MODE)
extern int file_tracker_reclaim_marked_deleted (THREAD_ENTRY * thread_p);
#endif /* SA_MODE */

extern int file_descriptor_get (THREAD_ENTRY * thread_p, const VFID * vfid, FILE_DESCRIPTORS * desc_out);
extern int file_descriptor_update (THREAD_ENTRY * thread_p, const VFID * vfid, void *des_new);
extern int file_descriptor_dump (THREAD_ENTRY * thread_p, const VFID * vfid, FILE * fp);

extern const char *file_type_to_string (FILE_TYPE fstruct_type);

/* Recovery stuff */
extern int file_rv_destroy (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern int file_rv_perm_expand_redo (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern int file_rv_perm_expand_undo (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern int file_rv_partsect_set (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern int file_rv_partsect_clear (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern int file_rv_extdata_set_next (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern int file_rv_extdata_add (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern int file_rv_extdata_remove (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern int file_rv_fhead_set_last_user_page_ftab (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern int file_rv_fhead_alloc (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern int file_rv_fhead_dealloc (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern int file_rv_fhead_convert_ftab_to_user_page (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern int file_rv_fhead_convert_user_to_ftab_page (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern int file_rv_user_page_mark_delete (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern int file_rv_user_page_unmark_delete_logical (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern int file_rv_user_page_unmark_delete_physical (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern int file_rv_extdata_merge (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern int file_rv_dealloc_on_undo (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern int file_rv_dealloc_on_postpone (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern int file_rv_header_update_mark_deleted (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern int file_rv_fhead_sticky_page (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern int file_rv_tracker_unregister_undo (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern int file_rv_tracker_mark_heap_deleted (THREAD_ENTRY * thread_p, LOG_RCV * rcv, bool is_undo);
extern int file_rv_tracker_mark_heap_deleted_compensate_or_run_postpone (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern int file_rv_tracker_reuse_heap (THREAD_ENTRY * thread_p, LOG_RCV * rcv);

/* Recovery dump stuff */
extern void file_rv_dump_vfid_and_vpid (FILE * fp, int length, void *data);
extern void file_rv_dump_extdata_set_next (FILE * fp, int length, void *data);
extern void file_rv_dump_extdata_add (FILE * fp, int length, void *data);
extern void file_rv_dump_extdata_remove (FILE * fp, int length, void *data);
#endif /* _FILE_MANAGER_H_ */
