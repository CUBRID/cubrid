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

/*
 * Disk sectors
 */
/* Special sector which can steal pages from other sectors */
#define DISK_SECTOR_WITH_ALL_PAGES 0

#define VSID_SET_NULL(vsidp) (vsidp)->sectid = NULL_SECTID; (vsidp)->volid = NULL_VOLID
#define VSID_IS_NULL(vsidp) ((vsidp)->sectid == NULL_SECTID || (vsidp)->volid == NULL_VOLID)
#define VSID_COPY(dest, src) *((VSID *) dest) = *((VSID *) src)
#define VSID_EQ(first, second) ((first)->volid == (second)->volid && (first)->sectid == (second)->sectid)

#define DISK_NULL_PAGEID_WITH_ENOUGH_DISK_PAGES (NULL_PAGEID - 1)

#define DISK_PAGE_BIT  (DB_PAGESIZE * CHAR_BIT)	/* Num of bits per page */

#define DISK_VOLHEADER_PAGE      0	/* Page of the volume header */

#define OR_VOL_SPACE_INFO_SIZE      (OR_INT_SIZE * 6)

/* todo: fix me */
#define OR_PACK_VOL_SPACE_INFO(PTR, INFO)               \
  do {                                                   \
    if ((VOL_SPACE_INFO *) (INFO) != NULL) {                                          \
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

/* todo: fix me */
#define OR_UNPACK_VOL_SPACE_INFO(PTR, INFO)             \
  do {                                                   \
    if ((VOL_SPACE_INFO *) (INFO) != NULL) {                                          \
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

/* TODO: Rename for file manager redesign. */
typedef enum
{
  DISK_CONTIGUOUS_PAGES,
  DISK_NONCONTIGUOUS_PAGES,	/* TODO: Remove this */
  DISK_NONCONTIGUOUS_SPANVOLS_PAGES
} DISK_SETPAGE_TYPE;

/* DON'T USE sizeof on this structure.. size if variable */
typedef struct disk_var_header DISK_VAR_HEADER;
struct disk_var_header
{				/* Volume header */
  /* DON'T MOVE THE MAGIC FIELD. IT IS USED BY FILEC */
  char magic[CUBRID_MAGIC_MAX_LENGTH];	/* Magic value for file/magic Unix utility */
  INT16 iopagesize;		/* This was only added for checking purposes. The actual value is stored on the log */
  INT16 volid;			/* Volume identifier */
  DB_VOLPURPOSE purpose;	/* Permanent or temporary volume purpose */
  DB_VOLTYPE type;		/* Permanent or temporary volume type */
  DKNPAGES sect_npgs;		/* Size of sector in pages */
  DKNSECTS nsect_total;		/* Total number of sectors */
  DKNSECTS nsect_max;		/* Maximum number of sectors */
  SECTID hint_allocsect;	/* Hint for next sector to be allocated */
  DKNPAGES stab_npages;		/* Size of sector allocation table in pages */
  PAGEID stab_first_page;	/* First page of sector allocation table */
  PAGEID sys_lastpage;		/* Last system page */
  INT64 db_creation;		/* Database creation time. For safety reasons, this value is set on all volumes and the
				 * log. The value is generated by the log manager */
  INT8 db_charset;		/* charset of database */
  INT8 dummy1;			/* Dummy fields for 8byte align */
  INT8 dummy2;
  INT8 dummy3;
  LOG_LSA chkpt_lsa;		/* Lowest log sequence address to start the recovery process of this volume */
  HFID boot_hfid;		/* System Heap file for booting purposes and multi volumes */
  INT16 next_volid;		/* next volume identifier * */
  INT16 offset_to_vol_fullname;	/* Offset to vol_fullname */
  INT16 offset_to_next_vol_fullname;	/* Offset to next vol_fullname */
  INT16 offset_to_vol_remarks;	/* Offset to vol_remarks */

  char var_fields[1];		/* Variable length fields addresses by the offset Current ordering is: 1) vol_fullname,
				 * 2) next_vol_fullname 3) volume remarks The length is DB_PAGESIZE - offset of
				 * var_fields */

};

typedef struct vol_space_info VOL_SPACE_INFO;
struct vol_space_info
{
  INT32 max_pages;		/* max page count of volume this is not equal to the total_pages, if this volume is
				 * auto extended */
  INT32 total_pages;		/* Total number of pages (no more that 4G) If page size is 4K, this means about 16
				 * trillion bytes on the volume. */
  INT32 free_pages;		/* Number of free pages */
  INT32 used_data_npages;	/* allocated pages for data purpose */
  INT32 used_index_npages;	/* allocated pages for index purpose */
  INT32 used_temp_npages;	/* allocated pages for temp purpose */

  /* new vol space info */
  INT32 n_max_sects;
  INT32 n_total_sects;
  INT32 n_free_sects;
};

#define DISK_SECTS_SIZE(nsects)  ((INT64) nsects * IO_SECTORSIZE)
#define DISK_SECTS_NPAGES(nsects) (nsects * DISK_SECTOR_NPAGES)

extern int disk_unformat (THREAD_ENTRY * thread_p, const char *vol_fullname);

extern INT32 disk_alloc_sector (THREAD_ENTRY * thread_p, INT16 volid, INT32 nsects, int exp_npages);
extern INT32 disk_alloc_special_sector (void);
extern INT32 disk_alloc_page (THREAD_ENTRY * thread_p, INT16 volid, INT32 sectid, INT32 npages, INT32 near_pageid,
			      DISK_PAGE_TYPE alloc_page_type);
extern int disk_dealloc_sector (THREAD_ENTRY * thread_p, INT16 volid, INT32 sectid, INT32 nsects);
extern int disk_dealloc_page (THREAD_ENTRY * thread_p, INT16 volid, INT32 pageid, INT32 npages,
			      DISK_PAGE_TYPE page_type);

extern int disk_set_creation (THREAD_ENTRY * thread_p, INT16 volid, const char *new_vol_fullname,
			      const INT64 * new_dbcreation, const LOG_LSA * new_chkptlsa, bool logchange,
			      DISK_FLUSH_TYPE flush_page);
extern int disk_set_link (THREAD_ENTRY * thread_p, INT16 volid, INT16 next_volid, const char *next_volext_fullname,
			  bool logchange, DISK_FLUSH_TYPE flush);
extern int disk_set_checkpoint (THREAD_ENTRY * thread_p, INT16 volid, const LOG_LSA * log_chkpt_lsa);
extern int disk_set_boot_hfid (THREAD_ENTRY * thread_p, INT16 volid, const HFID * hfid);

extern INT16 xdisk_get_purpose_and_sys_lastpage (THREAD_ENTRY * thread_p, INT16 volid, DISK_VOLPURPOSE * vol_purpose,
						 INT32 * sys_lastpage);
extern int disk_get_checkpoint (THREAD_ENTRY * thread_p, INT16 volid, LOG_LSA * vol_lsa);
extern int disk_get_creation_time (THREAD_ENTRY * thread_p, INT16 volid, INT64 * db_creation);
extern INT32 disk_get_total_numsectors (THREAD_ENTRY * thread_p, INT16 volid);
extern HFID *disk_get_boot_hfid (THREAD_ENTRY * thread_p, INT16 volid, HFID * hfid);
extern char *disk_get_link (THREAD_ENTRY * thread_p, INT16 volid, INT16 * next_volid, char *next_volext_fullname);
extern DISK_ISVALID disk_check (THREAD_ENTRY * thread_p, bool repair);
extern int disk_dump_all (THREAD_ENTRY * thread_p, FILE * fp);
extern int disk_volume_header_start_scan (THREAD_ENTRY * thread_p, int type, DB_VALUE ** arg_values, int arg_cnt,
					  void **ctx);
extern int disk_volume_header_end_scan (THREAD_ENTRY * thread_p, void **ctx);
extern SCAN_CODE disk_volume_header_next_scan (THREAD_ENTRY * thread_p, int cursor, DB_VALUE ** out_values, int out_cnt,
					       void *ctx);

extern int disk_rv_redo_dboutside_newvol (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern int disk_rv_undo_format (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern int disk_rv_redo_format (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern void disk_rv_dump_hdr (FILE * fp, int length_ignore, void *data);
extern int disk_rv_redo_init_map (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern void disk_rv_dump_init_map (FILE * fp, int length_ignore, void *data);
extern int disk_rv_undoredo_set_creation_time (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern void disk_rv_dump_set_creation_time (FILE * fp, int length_ignore, void *data);
extern int disk_rv_undoredo_set_boot_hfid (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern void disk_rv_dump_set_boot_hfid (FILE * fp, int length_ignore, void *data);
extern int disk_rv_undoredo_link (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern void disk_rv_dump_link (FILE * fp, int length_ignore, void *data);

extern int disk_rv_redo_dboutside_init_pages (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern void disk_rv_dump_init_pages (FILE * fp, int length_ignore, void *data);

/************************************************************************/
/*                                                                      */
/* FILE MANAGER REDESIGN                                                */
/*                                                                      */
/************************************************************************/

extern int disk_manager_init (THREAD_ENTRY * thread_p, bool load_form_disk);
extern void disk_manager_final (void);

extern int disk_reserve_sectors (THREAD_ENTRY * thread_p, DB_VOLPURPOSE purpose, DISK_SETPAGE_TYPE reserve_type,
				 VOLID volid_hint, int n_sectors, VSID * reserved_sectors);
extern int disk_rv_reserve_sectors (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern int disk_rv_unreserve_sectors (THREAD_ENTRY * thread_p, LOG_RCV * rcv);

extern DISK_ISVALID disk_is_page_sector_reserved (THREAD_ENTRY * thread_p, VOLID volid, PAGEID pageid);
extern DISK_ISVALID disk_is_page_sector_reserved_with_debug_crash (THREAD_ENTRY * thread_p, VOLID volid, PAGEID pageid,
								   bool debug_crash);

extern int disk_rv_volhead_extend_redo (THREAD_ENTRY * thread_p, LOG_RCV * rcv);

extern int disk_format_first_volume (THREAD_ENTRY * thread_p, const char *full_dbname, const char *dbcomments,
				     DKNPAGES npages);
extern int disk_add_volume_extension (THREAD_ENTRY * thread_p, DB_VOLPURPOSE purpose, DKNPAGES npages,
				      const char *path, const char *name, const char *comments,
				      int max_write_size_in_sec, bool overwrite, VOLID * volid_out);
extern int disk_unreserve_ordered_sectors (THREAD_ENTRY * thread_p, DB_VOLPURPOSE purpose, int nsects, VSID * vsids);

extern void disk_lock_extend (void);
extern void disk_unlock_extend (void);
#if defined (SERVER_MODE)
extern int disk_auto_expand (THREAD_ENTRY * thread_p);
#endif /* SERVER_MODE */
#endif /* _DISK_MANAGER_H_ */
