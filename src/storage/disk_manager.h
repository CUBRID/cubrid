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
 * disk_manager.h - Disk managment module (at server)
 */

#ifndef _DISK_MANAGER_H_
#define _DISK_MANAGER_H_

#ident "$Id$"

#include "config.h"

#include "error_manager.h"
#include "storage_common.h"
#include "recovery.h"

/* Special sector which can steal pages from other sectors */
#define DISK_SECTOR_WITH_ALL_PAGES 0

#define DISK_SECTOR_NPAGES 10	/* Number of pages in a sector */

#define DISK_NULL_PAGEID_WITH_ENOUGH_DISK_PAGES (NULL_PAGEID - 1)

#define DISK_PAGE_BIT  (DB_PAGESIZE * CHAR_BIT)	/* Num of bits per page   */

#define DISK_VOLHEADER_PAGE      0	/* Page of the volume header */

#define OR_VOL_SPACE_INFO_SIZE      (OR_INT_SIZE * 6)

#define OR_PACK_VOL_SPACE_INFO(PTR, INFO)               \
  do {                                                   \
    if (INFO) {                                          \
      PTR = or_pack_int (PTR, ((INFO)->total_pages));    \
      PTR = or_pack_int (PTR, ((INFO)->free_pages));     \
      PTR = or_pack_int (PTR, ((INFO)->max_pages));      \
      PTR = or_pack_int (PTR, ((INFO)->used_data_npages));\
      PTR = or_pack_int (PTR, ((INFO)->used_index_npages));\
      PTR = or_pack_int (PTR, ((INFO)->used_temp_npages));\
    }                                                    \
    else {                                               \
      PTR = or_pack_int (PTR, -1);                       \
      PTR = or_pack_int (PTR, -1);                       \
      PTR = or_pack_int (PTR, -1);                       \
      PTR = or_pack_int (PTR, -1);                       \
      PTR = or_pack_int (PTR, -1);                       \
      PTR = or_pack_int (PTR, -1);                       \
    }                                                    \
  } while (0)

#define OR_UNPACK_VOL_SPACE_INFO(PTR, INFO)             \
  do {                                                   \
    if (INFO) {                                          \
      PTR = or_unpack_int (PTR, &((INFO)->total_pages)); \
      PTR = or_unpack_int (PTR, &((INFO)->free_pages));  \
      PTR = or_unpack_int (PTR, &((INFO)->max_pages));   \
      PTR = or_unpack_int (PTR, &((INFO)->used_data_npages));\
      PTR = or_unpack_int (PTR, &((INFO)->used_index_npages));\
      PTR = or_unpack_int (PTR, &((INFO)->used_temp_npages));\
    }                                   \
    else {                              \
      int dummy;                        \
      PTR = or_unpack_int (PTR, &dummy);\
      PTR = or_unpack_int (PTR, &dummy);\
      PTR = or_unpack_int (PTR, &dummy);\
      PTR = or_unpack_int (PTR, &dummy);\
      PTR = or_unpack_int (PTR, &dummy);\
      PTR = or_unpack_int (PTR, &dummy);\
    }\
  } while (0)


typedef enum
{
  DISK_PAGE_DATA_TYPE = DISK_PERMVOL_DATA_PURPOSE,
  DISK_PAGE_INDEX_TYPE = DISK_PERMVOL_INDEX_PURPOSE,
  DISK_PAGE_TEMP_TYPE = DISK_PERMVOL_TEMP_PURPOSE,
  DISK_PAGE_UNKNOWN_TYPE = DISK_UNKNOWN_PURPOSE
} DISK_PAGE_TYPE;

typedef enum
{
  DISK_DONT_FLUSH,
  DISK_FLUSH,
  DISK_FLUSH_AND_INVALIDATE
} DISK_FLUSH_TYPE;

typedef enum
{
  DISK_INVALID,
  DISK_VALID,
  DISK_ERROR
} DISK_ISVALID;

typedef enum
{
  DISK_CONTIGUOUS_PAGES,
  DISK_NONCONTIGUOUS_PAGES,
  DISK_NONCONTIGUOUS_SPANVOLS_PAGES
} DISK_SETPAGE_TYPE;

/* DON'T USE sizeof on this structure.. size if variable */
typedef struct disk_var_header DISK_VAR_HEADER;
struct disk_var_header
{				/* Volume header */
  /* DON'T MOVE THE MAGIC FIELD. IT IS USED BY FILEC */
  char magic[CUBRID_MAGIC_MAX_LENGTH];	/* Magic value for file/magic Unix
					   utility */
  INT16 iopagesize;		/* This was only added for checking purposes.
				   The actual value is stored on the log */
  INT16 volid;			/* Volume identifier */
  DISK_VOLPURPOSE purpose;	/* Main purpose of volume */
  INT32 sect_npgs;		/* Size of sector in pages */
  INT32 total_sects;		/* Total number of sectors */
  INT32 free_sects;		/* Number of free sectors */
  INT32 hint_allocsect;		/* Hint for next sector to be allocated */
  INT32 total_pages;		/* Total number of pages (no more that 4G) If
				   page size is 4K, this means about 16
				   trillion bytes on the volume. */
  INT32 free_pages;		/* Number of free pages */
  INT32 sect_alloctb_npages;	/* Size of sector allocation table in pages */
  INT32 page_alloctb_npages;	/* Size of page allocation table in pages */
  INT32 sect_alloctb_page1;	/* First page of sector allocation table */
  INT32 page_alloctb_page1;	/* First page of page allocation table */
  INT32 sys_lastpage;		/* Last system page */
  INT64 db_creation;		/* Database creation time. For safety reasons,
				   this value is set on all volumes and the
				   log. The value is generated by the log
				   manager */
  INT32 max_npages;		/* max page count of this volume
				 * this is not equal to the total_pages,
				 * if this volume is auto extended */
  INT32 used_data_npages;	/* allocated pages for data purpose */
  INT32 used_index_npages;	/* allocated pages for index purpose */
  INT32 dummy;			/* Dummy field for 8byte align */
  LOG_LSA chkpt_lsa;		/* Lowest log sequence address to start the
				   recovery process of this volume */
  HFID boot_hfid;		/* System Heap file for booting purposes and
				   multi volumes */
  INT16 offset_to_vol_fullname;	/* Offset to vol_fullname */
  INT16 offset_to_next_vol_fullname;	/* Offset to next vol_fullname */
  INT16 offset_to_vol_remarks;	/* Offset to vol_remarks */
  char var_fields[1];		/* Variable length fields addresses by the
				   offset Current ordering is:
				   1) vol_fullname,
				   2) next_vol_fullname
				   3) volume remarks
				   The length is DB_PAGESIZE - offset of
				   var_fields */

};

typedef struct vol_space_info VOL_SPACE_INFO;
struct vol_space_info
{
  INT32 max_pages;		/* max page count of volume
				 * this is not equal to the total_pages,
				 * if this volume is auto extended */
  INT32 total_pages;		/* Total number of pages (no more that 4G) If
				 * page size is 4K, this means about 16
				 * trillion bytes on the volume. */
  INT32 free_pages;		/* Number of free pages */
  INT32 used_data_npages;	/* allocated pages for data purpose */
  INT32 used_index_npages;	/* allocated pages for index purpose */
  INT32 used_temp_npages;	/* allocated pages for temp purpose */
};

extern int disk_goodvol_decache (THREAD_ENTRY * thread_p);
extern bool disk_goodvol_refresh (THREAD_ENTRY * thread_p,
				  int hint_max_nvols);
extern VOLID disk_add_auto_volume_extension (THREAD_ENTRY * thread_p,
					     DKNPAGES min_npages,
					     DISK_SETPAGE_TYPE setpage_type,
					     DISK_VOLPURPOSE vol_purpose);
extern VOLID disk_find_goodvol (THREAD_ENTRY * thread_p, INT16 hint_volid,
				INT16 undesirable_volid, INT32 exp_numpages,
				DISK_SETPAGE_TYPE setpage_type,
				DISK_VOLPURPOSE vol_purpose);
extern bool disk_goodvol_refresh_with_new (THREAD_ENTRY * thread_p,
					   INT16 volid);

extern INT16 disk_format (THREAD_ENTRY * thread_p, const char *dbname,
			  INT16 volid, DBDEF_VOL_EXT_INFO * ext_info);
extern int disk_unformat (THREAD_ENTRY * thread_p, const char *vol_fullname);
extern INT32 disk_expand_tmp (THREAD_ENTRY * thread_p, INT16 volid,
			      INT32 min_pages, INT32 max_pages);
extern int disk_expand_perm (THREAD_ENTRY * thread_p, INT16 volid,
			     INT32 npages);
extern VOLID disk_cache_get_auto_extend_volid (THREAD_ENTRY * thread_p);
extern int disk_cache_set_auto_extend_volid (THREAD_ENTRY * thread_p,
					     VOLID volid);
extern int disk_reinit_all_tmp (THREAD_ENTRY * thread_p);

extern INT32 disk_alloc_sector (THREAD_ENTRY * thread_p, INT16 volid,
				INT32 nsects, int exp_npages);
extern INT32 disk_alloc_special_sector (void);
extern INT32 disk_alloc_page (THREAD_ENTRY * thread_p, INT16 volid,
			      INT32 sectid, INT32 npages, INT32 near_pageid,
			      bool search_wrap_around,
			      DISK_PAGE_TYPE alloc_page_type);
extern int disk_dealloc_sector (THREAD_ENTRY * thread_p, INT16 volid,
				INT32 sectid, INT32 nsects);
extern int disk_dealloc_page (THREAD_ENTRY * thread_p, INT16 volid,
			      INT32 pageid, INT32 npages,
			      DISK_PAGE_TYPE page_type);
extern DISK_ISVALID disk_isvalid_page (THREAD_ENTRY * thread_p, INT16 volid,
				       INT32 pageid);

extern int disk_set_creation (THREAD_ENTRY * thread_p, INT16 volid,
			      const char *new_vol_fullname,
			      const INT64 * new_dbcreation,
			      const LOG_LSA * new_chkptlsa,
			      bool logchange, DISK_FLUSH_TYPE flush_page);
extern int disk_set_link (THREAD_ENTRY * thread_p, INT16 volid,
			  const char *next_volext_fullname,
			  bool logchange, DISK_FLUSH_TYPE flush);
extern int disk_set_checkpoint (THREAD_ENTRY * thread_p, INT16 volid,
				const LOG_LSA * log_chkpt_lsa);
extern int disk_set_boot_hfid (THREAD_ENTRY * thread_p, INT16 volid,
			       const HFID * hfid);
extern int disk_set_alloctables (DISK_VOLPURPOSE vol_purpose,
				 INT32 total_sects, INT32 total_pages,
				 INT32 * sect_alloctb_npages,
				 INT32 * page_alloctb_npages,
				 INT32 * sect_alloctb_page1,
				 INT32 * page_alloctb_page1,
				 INT32 * sys_lastpage);

extern INT16 disk_get_all_total_free_numpages (THREAD_ENTRY * thread_p,
					       DISK_VOLPURPOSE vol_purpose,
					       INT16 * nvols,
					       int *total_pages,
					       int *free_pages);
#if defined(ENABLE_UNUSED_FUNCTION)
extern INT16 disk_get_first_total_free_numpages (THREAD_ENTRY * thread_p,
						 DISK_VOLPURPOSE purpose,
						 INT32 * ntotal_pages,
						 INT32 * nfree_pgs);
#endif /* ENABLE_UNUSED_FUNCTION */
extern INT16 xdisk_get_purpose_and_sys_lastpage (THREAD_ENTRY * thread_p,
						 INT16 volid,
						 DISK_VOLPURPOSE *
						 vol_purpose,
						 INT32 * sys_lastpage);
extern INT32 disk_get_num_overhead_for_newvol (INT32 npages);
extern int disk_get_checkpoint (THREAD_ENTRY * thread_p, INT16 volid,
				LOG_LSA * vol_lsa);
extern int disk_get_creation_time (THREAD_ENTRY * thread_p, INT16 volid,
				   INT64 * db_creation);
extern INT32 disk_get_total_numsectors (THREAD_ENTRY * thread_p, INT16 volid);
#if defined(ENABLE_UNUSED_FUNCTION)
extern INT32 disk_get_overhead_numpages (THREAD_ENTRY * thread_p,
					 INT16 volid);
#endif /* ENABLE_UNUSED_FUNCTION */
extern INT32 disk_get_maxcontiguous_numpages (THREAD_ENTRY * thread_p,
					      INT16 volid, INT32 max_npages);
extern HFID *disk_get_boot_hfid (THREAD_ENTRY * thread_p, INT16 volid,
				 HFID * hfid);
extern char *disk_get_link (THREAD_ENTRY * thread_p, INT16 volid,
			    char *next_volext_fullname);
extern INT32 disk_get_max_numpages (THREAD_ENTRY * thread_p,
				    DISK_VOLPURPOSE vol_purpose);
extern DISK_ISVALID disk_check (THREAD_ENTRY * thread_p, INT16 volid,
				bool repair);
extern int disk_dump_all (THREAD_ENTRY * thread_p, FILE * fp);

extern int disk_rv_redo_dboutside_newvol (THREAD_ENTRY * thread_p,
					  LOG_RCV * rcv);
extern int disk_rv_undo_format (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern void disk_rv_dump_hdr (FILE * fp, int length_ignore, void *data);
extern int disk_rv_redo_init_map (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern void disk_rv_dump_init_map (FILE * fp, int length_ignore, void *data);
extern int disk_vhdr_rv_undoredo_free_sectors (THREAD_ENTRY * thread_p,
					       LOG_RCV * rcv);
extern void disk_vhdr_rv_dump_free_sectors (FILE * fp, int length_ignore,
					    void *data);
extern int disk_vhdr_rv_undoredo_free_pages (THREAD_ENTRY * thread_p,
					     LOG_RCV * rcv);
extern void disk_vhdr_rv_dump_free_pages (FILE * fp, int length_ignore,
					  void *data);
extern int disk_rv_set_alloctable (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern int disk_rv_clear_alloctable (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern void disk_rv_dump_alloctable (FILE * fp, int length_ignore,
				     void *data);
extern int disk_rv_redo_magic (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern void disk_rv_dump_magic (FILE * fp, int length_ignore, void *data);
extern int disk_rv_undoredo_set_creation_time (THREAD_ENTRY * thread_p,
					       LOG_RCV * rcv);
extern void disk_rv_dump_set_creation_time (FILE * fp, int length_ignore,
					    void *data);
extern int disk_rv_undoredo_set_boot_hfid (THREAD_ENTRY * thread_p,
					   LOG_RCV * rcv);
extern void disk_rv_dump_set_boot_hfid (FILE * fp, int length_ignore,
					void *data);
extern int disk_rv_undoredo_link (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern void disk_rv_dump_link (FILE * fp, int length_ignore, void *data);
extern int disk_rv_alloctable_with_volheader (THREAD_ENTRY * thread_p,
					      LOG_RCV * rcv,
					      LOG_LSA * ref_lsa);
extern void disk_rv_dump_alloctable_with_vhdr (FILE * fp, int length_ignore,
					       void *data);
extern int disk_rv_set_alloctable_bitmap_only (THREAD_ENTRY * thread_p,
					       LOG_RCV * rcv);
extern int disk_rv_clear_alloctable_bitmap_only (THREAD_ENTRY * thread_p,
						 LOG_RCV * rcv);

extern int disk_rv_set_alloctable_vhdr_only (THREAD_ENTRY * thread_p,
					     LOG_RCV * rcv);
extern int disk_rv_clear_alloctable_vhdr_only (THREAD_ENTRY * thread_p,
					       LOG_RCV * rcv);
extern int disk_rv_redo_dboutside_init_pages (THREAD_ENTRY * thread_p,
					      LOG_RCV * rcv);
extern void disk_rv_dump_init_pages (FILE * fp, int length_ignore,
				     void *data);

#endif /* _DISK_MANAGER_H_ */
