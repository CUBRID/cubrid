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
 * recovery.h: recovery functions (at server)
 */

#ifndef _RECOVERY_H_
#define _RECOVERY_H_

#ident "$Id$"

#include <stdio.h>

#include "log_comm.h"
#include "error_manager.h"
#include "thread.h"

typedef enum
{
  /* 
   * RULE *********************************************
   * 
   * NEW ENTRIES SHOULD BE ADDED AT THE BOTTON OF THE FILE TO AVOID FULL RECOMPILATIONS (e.g., the file can be utimed)
   * and to AVOID OLD DATABASES TO BE RECOVERED UNDER OLD FILE */
  RVDK_NEWVOL = 0,
  RVDK_FORMAT = 1,
  RVDK_INITMAP = 2,
  RVDK_VHDR_SCALLOC = 3,
  RVDK_VHDR_PGALLOC = 4,
  RVDK_IDALLOC = 5,
  RVDK_IDDEALLOC_WITH_VOLHEADER = 6,
  RVDK_MAGIC = 7,
  RVDK_CHANGE_CREATION = 8,
  RVDK_RESET_BOOT_HFID = 9,
  RVDK_LINK_PERM_VOLEXT = 10,
  RVDK_IDDEALLOC_BITMAP_ONLY = 11,
  RVDK_IDDEALLOC_VHDR_ONLY = 12,
  RVDK_INIT_PAGES = 13,

  RVFL_CREATE_TMPFILE = 14,
  RVFL_FTAB_CHAIN = 15,
  RVFL_IDSTABLE = 16,
  RVFL_MARKED_DELETED = 17,
  RVFL_ALLOCSET_SECT = 18,
  RVFL_ALLOCSET_PAGETB_ADDRESS = 19,
  RVFL_ALLOCSET_NEW = 20,
  RVFL_ALLOCSET_LINK = 21,
  RVFL_ALLOCSET_ADD_PAGES = 22,
  RVFL_ALLOCSET_DELETE_PAGES = 23,
  RVFL_ALLOCSET_SECT_SHIFT = 24,
  RVFL_ALLOCSET_COPY = 25,
  RVFL_FHDR = 26,
  RVFL_FHDR_ADD_LAST_ALLOCSET = 27,
  RVFL_FHDR_REMOVE_LAST_ALLOCSET = 28,
  RVFL_FHDR_CHANGE_LAST_ALLOCSET = 29,
  RVFL_FHDR_ADD_PAGES = 30,
  RVFL_FHDR_MARK_DELETED_PAGES = 31,
  RVFL_FHDR_DELETE_PAGES = 32,
  RVFL_FHDR_FTB_EXPANSION = 33,
  RVFL_FILEDESC_UPD = 34,
  RVFL_DES_FIRSTREST_NEXTVPID = 35,
  RVFL_DES_NREST_NEXTVPID = 36,
  RVFL_TRACKER_REGISTER = 37,
  RVFL_LOGICAL_NOOP = 38,
  RVFL_FILEDESC_INS = 39,
  RVFL_POSTPONE_DESTROY_FILE = 40,
  RVFL_FHDR_UPDATE_NUM_USER_PAGES = 41,

  RVHF_CREATE_HEADER = 42,
  RVHF_NEWPAGE = 43,
  RVHF_STATS = 44,
  RVHF_CHAIN = 45,
  RVHF_INSERT = 46,
  RVHF_DELETE = 47,
  RVHF_UPDATE = 48,
  RVHF_REUSE_PAGE = 49,
  RVHF_CREATE_HEADER_REUSE_OID = 50,
  RVHF_NEWPAGE_REUSE_OID = 51,
  RVHF_REUSE_PAGE_REUSE_OID = 52,
  RVHF_MARK_REUSABLE_SLOT = 53,
  RVHF_MVCC_INSERT = 54,
  RVHF_MVCC_DELETE_REC_HOME = 55,
  RVHF_MVCC_DELETE_OVERFLOW = 56,
  RVHF_MVCC_DELETE_REC_NEWHOME = 57,
  RVHF_MVCC_DELETE_MODIFY_HOME = 58,
  RVHF_MVCC_DELETE_NO_MODIFY_HOME = 59,
  RVHF_UPDATE_NOTIFY_VACUUM = 60,
  RVHF_MVCC_REMOVE_PARTITION_LINK = 61,	/* Obsolete */
  RVHF_PARTITION_LINK_FLAG = 62,	/* Obsolete */
  RVHF_INSERT_NEWHOME = 63,	/* Same as RVHF_INSERT but no replication */
  RVHF_CREATE = 64,
  RVHF_MVCC_REDISTRIBUTE = 65,

  RVOVF_NEWPAGE_LOGICAL_UNDO = 66,
  RVOVF_NEWPAGE_INSERT = 67,
  RVOVF_NEWPAGE_LINK = 68,
  RVOVF_PAGE_UPDATE = 69,
  RVOVF_CHANGE_LINK = 70,
  RVOVF_NEWPAGE_DELETE_RELOCATED = 71,	/* Obsolete. */

  RVEH_REPLACE = 72,
  RVEH_INSERT = 73,
  RVEH_DELETE = 74,
  RVEH_INIT_BUCKET = 75,
  RVEH_CONNECT_BUCKET = 76,
  RVEH_INC_COUNTER = 77,
  RVEH_INIT_DIR = 78,
  RVEH_INIT_NEW_DIR_PAGE = 79,

  RVBT_NDHEADER_UPD = 80,
  RVBT_NDHEADER_INS = 81,
  RVBT_NDRECORD_UPD = 82,
  RVBT_NDRECORD_INS = 83,
  RVBT_NDRECORD_DEL = 84,
  RVBT_PUT_PGRECORDS = 85,
  RVBT_DEL_PGRECORDS = 86,
  RVBT_GET_NEWPAGE = 87,
  RVBT_NEW_PGALLOC = 88,
  RVBT_COPYPAGE = 89,
  RVBT_ROOTHEADER_UPD = 90,
  RVBT_UPDATE_OVFID = 91,
  RVBT_INS_PGRECORDS = 92,
  RVBT_CREATE_INDEX = 93,
  RVBT_MVCC_DELETE_OBJECT = 94,
  RVBT_MVCC_INCREMENTS_UPD = 95,
  RVBT_MVCC_NOTIFY_VACUUM = 96,
  RVBT_LOG_GLOBAL_UNIQUE_STATS_COMMIT = 97,
  RVBT_DELETE_OBJECT_PHYSICAL = 98,
  RVBT_NON_MVCC_INSERT_OBJECT = 99,
  RVBT_MVCC_INSERT_OBJECT = 100,
  RVBT_RECORD_MODIFY_NO_UNDO = 101,
  RVBT_RECORD_MODIFY_COMPENSATE = 102,
  RVBT_DELETE_INDEX = 103,
  RVBT_MVCC_UPDATE_SAME_KEY = 104,	/* Obsolete. */
  RVBT_RECORD_MODIFY_UNDOREDO = 105,
  RVBT_DELETE_OBJECT_POSTPONE = 106,
  RVBT_MARK_DELETED = 107,
  RVBT_MVCC_INSERT_OBJECT_UNQ = 108,
  RVBT_MARK_DEALLOC_PAGE = 109,

  RVCT_NEWPAGE = 110,
  RVCT_INSERT = 111,
  RVCT_DELETE = 112,
  RVCT_UPDATE = 113,
  RVCT_NEW_OVFPAGE_LOGICAL_UNDO = 114,

  RVLOM_INSERT = 115,
  RVLOM_DELETE = 116,
  RVLOM_OVERWRITE = 117,
  RVLOM_TAKEOUT = 118,
  RVLOM_PUTIN = 119,
  RVLOM_APPEND = 120,
  RVLOM_SPLIT = 121,
  RVLOM_GET_NEWPAGE = 122,
  RVLOM_DIR_RCV_STATE = 123,
  RVLOM_DIR_PG_REGION = 124,
  RVLOM_DIR_NEW_PG = 125,

  RVLOG_OUTSIDE_LOGICAL_REDO_NOOP = 126,

  RVREPL_DATA_INSERT = 127,
  RVREPL_DATA_UPDATE = 128,
  RVREPL_DATA_DELETE = 129,
  RVREPL_STATEMENT = 130,
  RVREPL_DATA_UPDATE_START = 131,
  RVREPL_DATA_UPDATE_END = 132,

  RVVAC_DATA_APPEND_BLOCKS = 133,
  RVVAC_DATA_MODIFY_FIRST_PAGE = 134,
  RVVAC_DATA_INIT_NEW_PAGE = 135,
  RVVAC_DATA_SET_LINK = 136,
  RVVAC_DATA_FINISHED_BLOCKS = 137,
  RVVAC_START_JOB = 138,
  RVVAC_DROPPED_FILE_CLEANUP = 139,
  RVVAC_DROPPED_FILE_NEXT_PAGE = 140,
  RVVAC_DROPPED_FILE_ADD = 141,
  RVVAC_COMPLETE = 142,
  RVVAC_HEAP_RECORD_VACUUM = 143,
  RVVAC_HEAP_PAGE_VACUUM = 144,
  RVVAC_REMOVE_OVF_INSID = 145,

  RVES_NOTIFY_VACUUM = 146,

  RVBO_DELVOL = 147,

  RVLOC_CLASSNAME_DUMMY = 148,

  RVPGBUF_FLUSH_PAGE = 149,

  RVHF_MVCC_UPDATE_OVERFLOW = 150,

  RVPG_REDO_PAGE = 151,

  RVDK_RESERVE_SECTORS = 152,
  RVDK_UNRESERVE_SECTORS = 153,

  RVDK_UPDATE_VOL_USED_SECTORS = 154,

  RVFL_EXPAND = 155,
  RVFL_ALLOC = 156,
  RVFL_PARTSECT_ALLOC = 157,
  RVFL_EXTDATA_SET_NEXT = 158,
  RVFL_EXTDATA_ADD = 159,
  RVFL_EXTDATA_REMOVE = 160,
  RVFL_FHEAD_SET_LAST_USER_PAGE_FTAB = 161,
  RVFL_FHEAD_ALLOC = 162,
  RVFL_DESTROY = 163,		/* Use for undo/postpone */
  RVFL_DEALLOC = 164,
  RVFL_USER_PAGE_MARK_DELETE = 165,
  RVFL_USER_PAGE_MARK_DELETE_COMPENSATE = 166,
  RVFL_PARTSECT_DEALLOC = 167,
  RVFL_EXTDATA_MERGE = 168,
  RVFL_EXTDATA_MERGE_COMPARE_VSID = 169,
  RVFL_FHEAD_DEALLOC = 170,
  RVFL_FHEAD_MARK_DELETE = 171,

  RV_LAST_LOGID = RVFL_FHEAD_MARK_DELETE,

  RV_NOT_DEFINED = 999
} LOG_RCVINDEX;

/*
 * RECOVERY STRUCTURE SEEN BY RECOVERY FUNCTIONS
 */
typedef struct log_rcv LOG_RCV;
struct log_rcv
{				/* Recovery information */
  MVCCID mvcc_id;		/* mvcc id */
  PAGE_PTR pgptr;		/* Page to recover. Page should not be free by recovery functions, however it should be 
				 * set dirty whenever is needed */
  PGLENGTH offset;		/* Offset/slot of data in the above page to recover */
  int length;			/* Length of data */
  const char *data;		/* Replacement data. Pointer becomes invalid once the recovery of the data is finished */
  LOG_LSA reference_lsa;	/* Next LSA used by compensate/postpone. */
};

/*
 * STRUCTURE ENTRY OF RECOVERY FUNCTIONS
 */

struct rvfun
{
  LOG_RCVINDEX recv_index;	/* For verification */
  const char *recv_string;
  int (*undofun) (THREAD_ENTRY * thread_p, LOG_RCV * logrcv);
  int (*redofun) (THREAD_ENTRY * thread_p, LOG_RCV * logrcv);
  void (*dump_undofun) (FILE * fp, int length, void *data);
  void (*dump_redofun) (FILE * fp, int length, void *data);
};

extern struct rvfun RV_fun[];

extern const char *rv_rcvindex_string (LOG_RCVINDEX rcvindex);
#if !defined (NDEBUG)
extern void rv_check_rvfuns (void);
#endif /* !NDEBUG */

#define RCV_IS_BTREE_LOGICAL_LOG(idx) \
  ((idx) == RVBT_DELETE_OBJECT_PHYSICAL \
   || ((idx) == RVBT_MVCC_DELETE_OBJECT) \
   || ((idx) == RVBT_MVCC_INSERT_OBJECT) \
   || ((idx) == RVBT_NON_MVCC_INSERT_OBJECT) \
   || ((idx) == RVBT_MARK_DELETED) \
   || ((idx) == RVBT_DELETE_OBJECT_POSTPONE) \
   || ((idx) == RVBT_MVCC_INSERT_OBJECT_UNQ))

#define RCV_IS_LOGICAL_COMPENSATE_MANUAL(idx) \
  (RCV_IS_BTREE_LOGICAL_LOG(idx) \
   || (idx) == RVFL_ALLOC \
   || (idx) == RVFL_USER_PAGE_MARK_DELETE)
#define RCV_IS_LOGICAL_RUN_POSTPONE_MANUAL(idx) \
  ((idx) == RVFL_DEALLOC)

#define RCV_IS_LOGICAL_LOG(vpid, idx) \
  (((vpid)->volid == NULL_VOLID) \
   || ((vpid)->pageid == NULL_PAGEID) \
   || RCV_IS_BTREE_LOGICAL_LOG (idx) \
   || ((idx) == RVBT_MVCC_INCREMENTS_UPD) \
   || ((idx) == RVBT_CREATE_INDEX) \
   || ((idx) == RVFL_POSTPONE_DESTROY_FILE) /* TODO: Remove me */\
   || ((idx) == RVPGBUF_FLUSH_PAGE) \
   || ((idx) == RVFL_DESTROY) \
   || ((idx) == RVFL_ALLOC) \
   || ((idx) == RVFL_DEALLOC))

#endif /* _RECOVERY_H_ */
