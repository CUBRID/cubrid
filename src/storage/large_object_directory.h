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
 * large_object_directory.h - LOM Directory Manager Header
 */

#ifndef _LARGE_OBJECT_DIRECTORY_H_
#define _LARGE_OBJECT_DIRECTORY_H_

#ident "$Id$"

#include "error_manager.h"
#include "storage_common.h"
#include "recovery.h"

/* Large object operation modes */
#define  LARGEOBJMGR_READ_MODE         0
#define  LARGEOBJMGR_WRITE_MODE        1
#define  LARGEOBJMGR_INSERT_MODE       2
#define  LARGEOBJMGR_DELETE_MODE       3
#define  LARGEOBJMGR_APPEND_MODE       4
#define  LARGEOBJMGR_TRUNCATE_MODE     5
#define  LARGEOBJMGR_COMPRESS_MODE     6
#define  LARGEOBJMGR_DIRCOMPRESS_MODE  7

/* Copy a directory entry */
#define LARGEOBJMGR_COPY_DIRENTRY(ent1,ent2)\
  do {\
       (ent1)->slotid = (ent2)->slotid;\
       if ((ent2)->slotid == NULL_SLOTID)\
         (ent1)->u.length = (ent2)->u.length;\
       else {\
              (ent1)->u.vpid.volid  = (ent2)->u.vpid.volid;\
              (ent1)->u.vpid.pageid = (ent2)->u.vpid.pageid;\
       };\
       (ent1)->length = (ent2)->length;\
   } while(0)

/* Regular (data page) directory entry */
#define LARGEOBJMGR_ISREG_DIRENTRY(ent)\
  ((ent)->slotid != NULL_SLOTID)

/* Empty/Unused directory entry */
#define LARGEOBJMGR_ISEMPTY_DIRENTRY(ent)\
  ((ent)->slotid == NULL_SLOTID && (ent)->u.length == 0)

/* Hole directory entry */
#define LARGEOBJMGR_ISHOLE_DIRENTRY(ent)  \
  ((ent)->slotid == NULL_SLOTID && (ent)->u.length > 0)


/* Directory entry length information */
#define LARGEOBJMGR_DIRENTRY_LENGTH(ent) \
  (LARGEOBJMGR_ISREG_DIRENTRY((ent)) ? (ent)->length : (ent)->u.length)

/* Set a directory entry to be empty */
#define LARGEOBJMGR_SET_EMPTY_DIRENTRY(ent) \
  do {\
      (ent)->slotid   = NULL_SLOTID;\
      (ent)->u.length = 0;\
      (ent)->length   = -1;\
     } while(0)

/* Set a directory entry to be hole */
#define LARGEOBJMGR_SET_HOLE_DIRENTRY(ent,len) \
  do {\
      (ent)->slotid   = NULL_SLOTID;\
      (ent)->u.length = (len);\
      (ent)->length   = -1;\
     } while(0)

/* Note: Directory entry deletion is actually update with an empty
         entry. For readability purposes, this macro is provided. */
#define largeobjmgr_delete_dir(thread_p, ds) \
  largeobjmgr_dir_update((thread_p), ds, NULL)

/* Directory structures
 *
 * Note: We keep only one index page that allow us to scan fewer directory
 *       pages when looking for a particular offset.
 *
 * We could have built something like a B+tree road map of non index pages,
 * however, we feel that most of the times, one index page would be enough.
 */
typedef struct largeobjmgr_dirheader LARGEOBJMGR_DIRHEADER;
struct largeobjmgr_dirheader
{
  LOID loid;			/* LOM identifier */
  INT64 tot_length;		/* Total large object length          */
  INT64 pg_tot_length;		/* Total length of data represented by
				 * this page
				 */
  int index_level;		/* Directory level:
				 * if 0,   page is a dir page (no indices),
				 * if > 0, page is an index page to directory
				 * pages.
				 */
  int tot_slot_cnt;		/* Total number of slots that form LO */
  VPID goodvpid_fordata;	/* A hint for a data page with space.
				 * Usually the last allocated data page.
				 */
  int pg_act_idxcnt;		/* Active entry count represented by this
				 * page
				 */
  int pg_lastact_idx;		/* Last active entry index */
  VPID next_vpid;		/* Next directory page identifier */
};

typedef struct largeobjmgr_dirmap_entry LARGEOBJMGR_DIRMAP_ENTRY;
struct largeobjmgr_dirmap_entry
{
  VPID vpid;			/* Directory page identifier */
  INT64 length;			/* Length represented by this index item */
};				/* Directory Index Entry Structure */


typedef struct largeobjmgr_direntry LARGEOBJMGR_DIRENTRY;
struct largeobjmgr_direntry
{
  union
  {
    VPID vpid;			/* Data page identifier */
    int length;			/* HOLE/EMPTY length */
  } u;
  INT16 slotid;			/* Data slot identifier */
  INT16 length;			/* Length of the data in the slot */
};

typedef struct largeobjmgr_firstdir LARGEOBJMGR_FIRSTDIR;
struct largeobjmgr_firstdir
{
  PAGE_PTR pgptr;		/* Points to first directory page.
				 * This directory page may be an index
				 * onto directory pages when
				 * index_level is greater than zero
				 */
  int idx;			/* Index page entry index */
  LARGEOBJMGR_DIRMAP_ENTRY *idxptr;	/* Index page entry pointer */
  LARGEOBJMGR_DIRHEADER *hdr;	/* Header of first directory page */
};

typedef struct largeobjmgr_curdir LARGEOBJMGR_CURDIR;
struct largeobjmgr_curdir
{
  PAGE_PTR pgptr;		/* Directory page pointer */
  int idx;			/* Directory page entry index */
  LARGEOBJMGR_DIRENTRY *idxptr;	/* Directory page entry pointer */
  LARGEOBJMGR_DIRHEADER *hdr;	/* Header of directory page */
};

typedef struct largeobjmgr_dirstate LARGEOBJMGR_DIRSTATE;
struct largeobjmgr_dirstate
{
  int opr_mode;			/* LOM operation mode */
  int index_level;		/* if > 0, the first directory page is
				 * an index map page which help us to
				 * speed looking for an specific offset
				 * onto directory pages.
				 */
  SCAN_POSITION pos;		/* Directory state position */
  INT64 tot_length;		/* Current total length of large object */
  INT64 lo_offset;		/* Current large object offset */
  VPID goodvpid_fordata;	/* A hint for a data page with space.
				 * Usually the last allocated data page.
				 */
  LARGEOBJMGR_FIRSTDIR firstdir;	/* Index page */
  LARGEOBJMGR_CURDIR curdir;	/* Directory page */
};

typedef struct largeobjmgr_dirstate_pos LARGEOBJMGR_DIRSTATE_POS;
struct largeobjmgr_dirstate_pos
{
  int opr_mode;			/* LOM operation mode */
  SCAN_POSITION pos;		/* Directory state position */
  INT64 lo_offset;		/* Current large object offset */
  VPID firstdir_vpid;		/* Index page identifier */
  int firstdir_idx;		/* Index page entry index */
  VPID curdir_vpid;		/* Directory page identifier */
  int curdir_idx;		/* Directory page entry index */
};				/* Directory state position structure */

extern void largeobjmgr_reset_last_alloc_page (THREAD_ENTRY * thread_p,
					       LARGEOBJMGR_DIRSTATE * ds,
					       VPID * vpid_ptr);
extern int largeobjmgr_dir_get_vpids (THREAD_ENTRY * thread_p, LOID * loid,
				      VPID ** dir_vpid_list,
				      int *dir_vpid_cnt, bool * index_exists);
extern SCAN_CODE largeobjmgr_dir_scan_next (THREAD_ENTRY * thread_p,
					    LARGEOBJMGR_DIRSTATE * ds);
extern int largeobjmgr_dir_insert (THREAD_ENTRY * thread_p,
				   LARGEOBJMGR_DIRSTATE * ds,
				   LARGEOBJMGR_DIRENTRY * X_ent_ptr,
				   int ins_ent_cnt);
extern int largeobjmgr_dir_update (THREAD_ENTRY * thread_p,
				   LARGEOBJMGR_DIRSTATE * ds,
				   LARGEOBJMGR_DIRENTRY * X);
extern void largeobjmgr_dir_dump (THREAD_ENTRY * thread_p, FILE * fp,
				  LOID * loid);
extern bool largeobjmgr_dir_check (THREAD_ENTRY * thread_p, LOID * loid);
extern SCAN_CODE largeobjmgr_dir_open (THREAD_ENTRY * thread_p, LOID * loid,
				       INT64 offset, int opr_mode,
				       LARGEOBJMGR_DIRSTATE * ds);
extern void largeobjmgr_dir_close (THREAD_ENTRY * thread_p,
				   LARGEOBJMGR_DIRSTATE * ds);
extern INT64 largeobjmgr_dir_get_lolength (THREAD_ENTRY * thread_p,
					   LOID * loid);
extern SCAN_CODE largeobjmgr_skip_empty_entries (THREAD_ENTRY * thread_p,
						 LARGEOBJMGR_DIRSTATE * ds);
extern void largeobjmgr_dir_get_pos (LARGEOBJMGR_DIRSTATE * ds,
				     LARGEOBJMGR_DIRSTATE_POS * ds_pos);
extern int largeobjmgr_dir_put_pos (THREAD_ENTRY * thread_p,
				    LARGEOBJMGR_DIRSTATE * ds,
				    LARGEOBJMGR_DIRSTATE_POS * ds_pos);
extern int largeobjmgr_dir_compress (THREAD_ENTRY * thread_p, LOID * loid);
extern void largeobjmgr_init_dir_pagecnt (int data_pgcnt, int *dir_pgcnt,
					  int *dir_ind_pgcnt);
extern int largeobjmgr_dir_create (THREAD_ENTRY * thread_p, LOID * loid,
				   int length, int dir_ind_pgcnt,
				   int dir_pgcnt, int data_pgcnt,
				   int max_data_slot_size);

extern int largeobjmgr_rv_dir_rcv_state_undoredo (THREAD_ENTRY * thread_p,
						  LOG_RCV * recv);
extern void largeobjmgr_rv_dir_rcv_state_dump (FILE * fp, int length,
					       void *data);
extern int largeobjmgr_rv_dir_page_region_undoredo (THREAD_ENTRY * thread_p,
						    LOG_RCV * recv);
extern int largeobjmgr_rv_dir_new_page_undo (THREAD_ENTRY * thread_p,
					     LOG_RCV * recv);
extern int largeobjmgr_rv_dir_new_page_redo (THREAD_ENTRY * thread_p,
					     LOG_RCV * recv);

#endif /* _LARGE_OBJECT_DIRECTORY_H_ */
