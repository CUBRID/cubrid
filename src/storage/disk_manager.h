/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * disk_manager.h - Disk managment module (at server)
 */

#ifndef _DISK_MANAGER_H_
#define _DISK_MANAGER_H_

#ident "$Id$"

#include "config.h"

#include "error_manager.h"
#include "common.h"
#include "recovery.h"

/* Special sector which can steal pages from other sectors */
#define DISK_SECTOR_WITH_ALL_PAGES 0

#define DISK_SECTOR_NPAGES 10	/* Number of pages in a sector */

#define DISK_NULL_PAGEID_WITH_ENOUGH_DISK_PAGES (NULL_PAGEID - 1)

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

extern bool distk_Tempvol_shrink_enable;

#if defined(SERVER_MODE)
/* in xserver.h */
extern INT32 xdisk_get_total_numpages (THREAD_ENTRY * thread_p, INT16 volid);
extern INT32 xdisk_get_free_numpages (THREAD_ENTRY * thread_p, INT16 volid);
extern char *xdisk_get_remarks (THREAD_ENTRY * thread_p, INT16 volid);
extern char *xdisk_get_fullname (THREAD_ENTRY * thread_p, INT16 volid,
                                 char *vol_fullname);
extern DISK_VOLPURPOSE xdisk_get_purpose (THREAD_ENTRY * thread_p, INT16 volid);
extern VOLID xdisk_get_purpose_and_total_free_numpages (THREAD_ENTRY * thread_p, 
                                                        VOLID volid,
							DISK_VOLPURPOSE *
							vol_purpose,
							int *vol_ntotal_pages,
							int *vol_nfree_pages);
#endif /* SERVER_MODE */

extern int disk_goodvol_decache (THREAD_ENTRY * thread_p);
extern bool disk_goodvol_refresh (THREAD_ENTRY * thread_p,
				  int hint_max_nvols);
extern INT16 disk_goodvol_find (THREAD_ENTRY * thread_p, INT16 hint_volid,
				INT16 undesirable_volid, INT32 exp_numpages,
				DISK_VOLPURPOSE vol_purpose,
				DISK_SETPAGE_TYPE setpage_type);
extern bool disk_goodvol_refresh_with_new (THREAD_ENTRY * thread_p,
					   INT16 volid);

extern INT16 disk_format (THREAD_ENTRY * thread_p, const char *dbname,
			  INT16 volid, const char *vol_fullname,
			  const char *vol_remarks, INT32 npages,
			  DISK_VOLPURPOSE vol_purpose);
extern int disk_unformat (THREAD_ENTRY * thread_p, const char *vol_fullname);
extern INT32 disk_expand_tmp (THREAD_ENTRY * thread_p, INT16 volid,
			      INT32 min_pages, INT32 max_pages);
extern INT32 disk_shrink_tmp (THREAD_ENTRY * thread_p, INT16 volid,
			      bool * removed);
extern int disk_reinit_all_tmp (THREAD_ENTRY * thread_p);

extern INT32 disk_alloc_sector (THREAD_ENTRY * thread_p, INT16 volid,
				INT32 nsects);
extern INT32 disk_alloc_special_sector (void);
extern INT32 disk_alloc_page (THREAD_ENTRY * thread_p, INT16 volid,
			      INT32 sectid, INT32 npages, INT32 near_pageid);
extern int disk_dealloc_sector (THREAD_ENTRY * thread_p, INT16 volid,
				INT32 sectid, INT32 nsects);
extern int disk_dealloc_page (THREAD_ENTRY * thread_p, INT16 volid,
			      INT32 pageid, INT32 npages);
extern DISK_ISVALID disk_isvalid_page (THREAD_ENTRY * thread_p, INT16 volid,
				       INT32 pageid);

extern int disk_set_creation (THREAD_ENTRY * thread_p, INT16 volid,
			      const char *new_vol_fullname,
			      const time_t * new_dbcreation,
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
extern INT16 disk_get_first_total_free_numpages (THREAD_ENTRY * thread_p,
						 DISK_VOLPURPOSE purpose,
						 INT32 * ntotal_pages,
						 INT32 * nfree_pgs);
extern INT16 xdisk_get_purpose_and_sys_lastpage (THREAD_ENTRY * thread_p,
						 INT16 volid,
						 DISK_VOLPURPOSE *
						 vol_purpose,
						 INT32 * sys_lastpage);
extern int disk_get_checkpoint (THREAD_ENTRY * thread_p, INT16 volid,
				LOG_LSA * vol_lsa);
extern int disk_get_creation_time (THREAD_ENTRY * thread_p, INT16 volid,
				   time_t * db_creation);
extern INT32 disk_get_total_numsectors (THREAD_ENTRY * thread_p, INT16 volid);
extern INT32 disk_get_overhead_numpages (THREAD_ENTRY * thread_p,
					 INT16 volid);
extern INT32 disk_get_maxcontiguous_numpages (THREAD_ENTRY * thread_p,
					      INT16 volid);
extern HFID *disk_get_boot_hfid (THREAD_ENTRY * thread_p, INT16 volid,
				 HFID * hfid);
extern char *disk_get_link (THREAD_ENTRY * thread_p, INT16 volid,
			    char *next_volext_fullname);
extern int disk_get_temporarytmp_shrink_info (VPID * vpid, bool * decreased);
extern INT32 disk_get_max_numpages (THREAD_ENTRY * thread_p,
				    DISK_VOLPURPOSE vol_purpose);

extern DISK_ISVALID disk_check (THREAD_ENTRY * thread_p, INT16 volid,
				bool repair);
extern int disk_dump_all (THREAD_ENTRY * thread_p);

extern int disk_rv_redo_dboutside_newvol (THREAD_ENTRY * thread_p,
					  LOG_RCV * rcv);
extern int disk_rv_undo_format (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern void disk_rv_dump_hdr (int length_ignore, void *data);
extern int disk_rv_redo_init_map (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern void disk_rv_dump_init_map (int length_ignore, void *data);
extern int disk_vhdr_rv_undoredo_free_sectors (THREAD_ENTRY * thread_p,
					       LOG_RCV * rcv);
extern void disk_vhdr_rv_dump_free_sectors (int length_ignore, void *data);
extern int disk_vhdr_rv_undoredo_free_pages (THREAD_ENTRY * thread_p,
					     LOG_RCV * rcv);
extern void disk_vhdr_rv_dump_free_pages (int length_ignore, void *data);
extern int disk_rv_set_alloctable (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern int disk_rv_clear_alloctable (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern void disk_rv_dump_alloctable (int length_ignore, void *data);
extern int disk_rv_redo_magic (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern void disk_rv_dump_magic (int length_ignore, void *data);
extern int disk_rv_undoredo_set_creation_time (THREAD_ENTRY * thread_p,
					       LOG_RCV * rcv);
extern void disk_rv_dump_set_creation_time (int length_ignore, void *data);
extern int disk_rv_undoredo_set_boot_hfid (THREAD_ENTRY * thread_p,
					   LOG_RCV * rcv);
extern void disk_rv_dump_set_boot_hfid (int length_ignore, void *data);
extern int disk_rv_undoredo_link (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern void disk_rv_dump_link (int length_ignore, void *data);
extern int disk_rv_set_alloctable_with_vhdr (THREAD_ENTRY * thread_p,
					     LOG_RCV * rcv);
extern int disk_rv_clear_alloctable_with_vhdr (THREAD_ENTRY * thread_p,
					       LOG_RCV * rcv);
extern void disk_rv_dump_alloctable_with_vhdr (int length_ignore, void *data);

#endif /* _DISK_MANAGER_H_ */
