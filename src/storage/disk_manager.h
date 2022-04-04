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
 * disk_manager.h - Disk management module (at server)
 */

#ifndef _DISK_MANAGER_H_
#define _DISK_MANAGER_H_

#ident "$Id$"

#include "config.h"
#include "error_manager.h"
#include "log_lsa.hpp"
#include "recovery.h"
#include "storage_common.h"
#include "thread_compat.hpp"

#define DISK_VOLHEADER_PAGE      0	/* Page of the volume header */

/*
 * Disk sectors
 */

#define VSID_SET_NULL(vsidp) (vsidp)->sectid = NULL_SECTID; (vsidp)->volid = NULL_VOLID
#define VSID_IS_NULL(vsidp) ((vsidp)->sectid == NULL_SECTID || (vsidp)->volid == NULL_VOLID)
#define VSID_COPY(dest, src) *((VSID *) dest) = *((VSID *) src)
#define VSID_EQ(first, second) ((first)->volid == (second)->volid && (first)->sectid == (second)->sectid)

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

typedef struct disk_volume_space_info DISK_VOLUME_SPACE_INFO;
struct disk_volume_space_info
{
  INT32 n_max_sects;
  INT32 n_total_sects;
  INT32 n_free_sects;
};
#define DISK_VOLUME_SPACE_INFO_INITIALIZER { 0, 0, 0 }

#define DISK_SECTS_SIZE(nsects)  ((INT64) (nsects) * IO_SECTORSIZE)
#define DISK_SECTS_NPAGES(nsects) ((nsects) * DISK_SECTOR_NPAGES)
#define DISK_PAGES_TO_SECTS(npages) (CEIL_PTVDIV (npages, DISK_SECTOR_NPAGES))

/* structure used to clone disk sector bitmaps to cross check against file tables */
typedef struct disk_volmap_clone DISK_VOLMAP_CLONE;
struct disk_volmap_clone
{
  int size_map;
  char *map;
};

extern int disk_manager_init (THREAD_ENTRY * thread_p, bool load_form_disk);
extern void disk_manager_final (void);

extern int disk_format_first_volume (THREAD_ENTRY * thread_p, const char *full_dbname, const char *dbcomments,
				     DKNPAGES npages);
extern int disk_add_volume_extension (THREAD_ENTRY * thread_p, DB_VOLPURPOSE purpose, DKNPAGES npages,
				      const char *path, const char *name, const char *comments,
				      int max_write_size_in_sec, bool overwrite, VOLID * volid_out);
extern void disk_lock_extend (void);
extern void disk_unlock_extend (void);
extern int disk_unformat (THREAD_ENTRY * thread_p, const char *vol_fullname);
extern int disk_set_creation (THREAD_ENTRY * thread_p, INT16 volid, const char *new_vol_fullname,
			      const INT64 * new_dbcreation, const LOG_LSA * new_chkptlsa, bool logchange,
			      DISK_FLUSH_TYPE flush_page);
extern int disk_set_link (THREAD_ENTRY * thread_p, INT16 volid, INT16 next_volid, const char *next_volext_fullname,
			  bool logchange, DISK_FLUSH_TYPE flush);
extern int disk_set_checkpoint (THREAD_ENTRY * thread_p, INT16 volid, const LOG_LSA * log_chkpt_lsa);
extern int disk_set_boot_hfid (THREAD_ENTRY * thread_p, INT16 volid, const HFID * hfid);

extern int disk_reserve_sectors (THREAD_ENTRY * thread_p, DB_VOLPURPOSE purpose, VOLID volid_hint, int n_sectors,
				 VSID * reserved_sectors);
extern int disk_unreserve_ordered_sectors (THREAD_ENTRY * thread_p, DB_VOLPURPOSE purpose, int nsects, VSID * vsids);
extern DISK_ISVALID disk_is_page_sector_reserved (THREAD_ENTRY * thread_p, VOLID volid, PAGEID pageid);
extern DISK_ISVALID disk_is_page_sector_reserved_with_debug_crash (THREAD_ENTRY * thread_p, VOLID volid, PAGEID pageid,
								   bool debug_crash);
extern DISK_ISVALID disk_check_sectors_are_reserved (THREAD_ENTRY * thread_p, VSID * vsids, int nsects);

extern INT16 xdisk_get_purpose_and_sys_lastpage (THREAD_ENTRY * thread_p, INT16 volid, DISK_VOLPURPOSE * vol_purpose,
						 INT32 * sys_lastpage);
extern int disk_get_checkpoint (THREAD_ENTRY * thread_p, INT16 volid, LOG_LSA * vol_lsa);
extern int disk_get_creation_time (THREAD_ENTRY * thread_p, INT16 volid, INT64 * db_creation);
extern INT32 disk_get_total_numsectors (THREAD_ENTRY * thread_p, INT16 volid);
extern HFID *disk_get_boot_hfid (THREAD_ENTRY * thread_p, INT16 volid, HFID * hfid);
extern char *disk_get_link (THREAD_ENTRY * thread_p, INT16 volid, INT16 * next_volid, char *next_volext_fullname);
extern DISK_ISVALID disk_check (THREAD_ENTRY * thread_p, bool repair);
extern int disk_dump_all (THREAD_ENTRY * thread_p, FILE * fp);
extern int disk_spacedb (THREAD_ENTRY * thread_p, SPACEDB_ALL * spaceall, SPACEDB_ONEVOL ** spacevols);

extern int disk_volume_header_start_scan (THREAD_ENTRY * thread_p, int type, DB_VALUE ** arg_values, int arg_cnt,
					  void **ctx);
extern int disk_volume_header_end_scan (THREAD_ENTRY * thread_p, void **ctx);
extern SCAN_CODE disk_volume_header_next_scan (THREAD_ENTRY * thread_p, int cursor, DB_VALUE ** out_values, int out_cnt,
					       void *ctx);

extern int disk_compare_vsids (const void *a, const void *b);

#if defined (SA_MODE)
extern int disk_map_clone_create (THREAD_ENTRY * thread_p, DISK_VOLMAP_CLONE ** disk_map_clone);
extern void disk_map_clone_free (DISK_VOLMAP_CLONE ** disk_map_clone);
extern DISK_ISVALID disk_map_clone_clear (VSID * vsid, DISK_VOLMAP_CLONE * disk_map_clone);
extern DISK_ISVALID disk_map_clone_check_leaks (DISK_VOLMAP_CLONE * disk_map_clone);
#endif /* SA_MODE */

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
extern int disk_rv_redo_volume_expand (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern void disk_rv_dump_volume_expand (FILE * fp, int length_ignore, void *data);
extern int disk_rv_reserve_sectors (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern int disk_rv_unreserve_sectors (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern int disk_rv_volhead_extend_redo (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern int disk_rv_volhead_extend_undo (THREAD_ENTRY * thread_p, LOG_RCV * rcv);

#if !defined (NDEBUG)
extern void disk_volheader_check_magic (THREAD_ENTRY * thread_p, const PAGE_PTR page_volheader);
#endif /* !NDEBUG */

extern int disk_sectors_to_extend_npages (const int num_pages);

#endif /* _DISK_MANAGER_H_ */
