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
  RVDK_CHANGE_CREATION = 3,
  RVDK_RESET_BOOT_HFID = 4,
  RVDK_LINK_PERM_VOLEXT = 5,
  RVDK_INIT_PAGES = 6,
  RVDK_RESERVE_SECTORS = 7,
  RVDK_UNRESERVE_SECTORS = 8,
  RVDK_VOLHEAD_EXPAND = 9,

  RVFL_DESTROY = 10,		/* Use for undo/postpone */
  RVFL_EXPAND = 11,
  RVFL_ALLOC = 12,
  RVFL_DEALLOC = 13,
  RVFL_FHEAD_ALLOC = 14,
  RVFL_FHEAD_DEALLOC = 15,
  RVFL_FHEAD_SET_LAST_USER_PAGE_FTAB = 16,
  RVFL_FHEAD_MARK_DELETE = 17,
  RVFL_FHEAD_STICKY_PAGE = 18,
  RVFL_USER_PAGE_MARK_DELETE = 19,
  RVFL_USER_PAGE_MARK_DELETE_COMPENSATE = 20,
  RVFL_FILEDESC_UPD = 21,
  RVFL_PARTSECT_ALLOC = 22,
  RVFL_PARTSECT_DEALLOC = 23,
  RVFL_EXTDATA_SET_NEXT = 24,
  RVFL_EXTDATA_ADD = 25,
  RVFL_EXTDATA_REMOVE = 26,
  RVFL_EXTDATA_MERGE = 27,
  RVFL_EXTDATA_MERGE_COMPARE_VSID = 28,
  RVFL_EXTDATA_MERGE_COMPARE_TRACK_ITEM = 29,
  RVFL_EXTDATA_UPDATE_ITEM = 30,

  RVHF_CREATE_HEADER = 31,
  RVHF_NEWPAGE = 32,
  RVHF_STATS = 33,
  RVHF_CHAIN = 34,
  RVHF_INSERT = 35,
  RVHF_DELETE = 36,
  RVHF_UPDATE = 37,
  RVHF_REUSE_PAGE = 38,
  RVHF_REUSE_PAGE_REUSE_OID = 39,
  RVHF_MARK_REUSABLE_SLOT = 40,
  RVHF_MVCC_INSERT = 41,
  RVHF_MVCC_DELETE_REC_HOME = 42,
  RVHF_MVCC_DELETE_OVERFLOW = 43,
  RVHF_MVCC_DELETE_REC_NEWHOME = 44,
  RVHF_MVCC_DELETE_MODIFY_HOME = 45,
  RVHF_MVCC_NO_MODIFY_HOME = 46,
  RVHF_UPDATE_NOTIFY_VACUUM = 47,
  RVHF_INSERT_NEWHOME = 48,	/* Same as RVHF_INSERT but no replication */
  RVHF_MVCC_REDISTRIBUTE = 49,
  RVHF_MVCC_UPDATE_OVERFLOW = 50,

  RVOVF_NEWPAGE_INSERT = 51,
  RVOVF_NEWPAGE_LINK = 52,
  RVOVF_PAGE_UPDATE = 53,
  RVOVF_CHANGE_LINK = 54,

  RVEH_REPLACE = 55,
  RVEH_INSERT = 56,
  RVEH_DELETE = 57,
  RVEH_INIT_BUCKET = 58,
  RVEH_CONNECT_BUCKET = 59,
  RVEH_INC_COUNTER = 60,
  RVEH_INIT_DIR = 61,
  RVEH_INIT_NEW_DIR_PAGE = 62,

  RVBT_NDHEADER_UPD = 63,
  RVBT_NDHEADER_INS = 64,
  RVBT_NDRECORD_UPD = 65,
  RVBT_NDRECORD_INS = 66,
  RVBT_NDRECORD_DEL = 67,
  RVBT_DEL_PGRECORDS = 68,
  RVBT_GET_NEWPAGE = 69,
  RVBT_COPYPAGE = 70,
  RVBT_ROOTHEADER_UPD = 71,
  RVBT_UPDATE_OVFID = 72,
  RVBT_INS_PGRECORDS = 73,
  RVBT_MVCC_DELETE_OBJECT = 74,
  RVBT_MVCC_INCREMENTS_UPD = 75,
  RVBT_MVCC_NOTIFY_VACUUM = 76,
  RVBT_LOG_GLOBAL_UNIQUE_STATS_COMMIT = 77,
  RVBT_DELETE_OBJECT_PHYSICAL = 78,
  RVBT_NON_MVCC_INSERT_OBJECT = 79,
  RVBT_MVCC_INSERT_OBJECT = 80,
  RVBT_MVCC_INSERT_OBJECT_UNQ = 81,
  RVBT_RECORD_MODIFY_UNDOREDO = 82,
  RVBT_RECORD_MODIFY_NO_UNDO = 83,
  RVBT_RECORD_MODIFY_COMPENSATE = 84,
  RVBT_REMOVE_UNIQUE_STATS = 85,
  RVBT_DELETE_OBJECT_POSTPONE = 86,
  RVBT_MARK_DELETED = 87,
  RVBT_MARK_DEALLOC_PAGE = 88,

  RVCT_NEWPAGE = 89,
  RVCT_INSERT = 90,
  RVCT_DELETE = 91,
  RVCT_UPDATE = 92,
  RVCT_NEW_OVFPAGE_LOGICAL_UNDO = 93,

  RVLOG_OUTSIDE_LOGICAL_REDO_NOOP = 94,

  RVREPL_DATA_INSERT = 95,
  RVREPL_DATA_UPDATE = 96,
  RVREPL_DATA_DELETE = 97,
  RVREPL_STATEMENT = 98,
  RVREPL_DATA_UPDATE_START = 99,
  RVREPL_DATA_UPDATE_END = 100,

  RVVAC_COMPLETE = 101,
  RVVAC_START_JOB = 102,
  RVVAC_DATA_APPEND_BLOCKS = 103,
  RVVAC_DATA_MODIFY_FIRST_PAGE = 104,
  RVVAC_DATA_INIT_NEW_PAGE = 105,
  RVVAC_DATA_SET_LINK = 106,
  RVVAC_DATA_FINISHED_BLOCKS = 107,
  RVVAC_NOTIFY_DROPPED_FILE = 108,
  RVVAC_DROPPED_FILE_CLEANUP = 109,
  RVVAC_DROPPED_FILE_NEXT_PAGE = 110,
  RVVAC_DROPPED_FILE_ADD = 111,
  RVVAC_DROPPED_FILE_REPLACE = 112,
  RVVAC_HEAP_RECORD_VACUUM = 113,
  RVVAC_HEAP_PAGE_VACUUM = 114,
  RVVAC_REMOVE_OVF_INSID = 115,

  RVES_NOTIFY_VACUUM = 116,

  RVLOC_CLASSNAME_DUMMY = 117,

  RVPGBUF_FLUSH_PAGE = 118,
  RVPGBUF_NEW_PAGE = 119,
  RVPGBUF_DEALLOC = 120,
  RVPGBUF_COMPENSATE_DEALLOC = 121,

  RV_LAST_LOGID = RVPGBUF_COMPENSATE_DEALLOC,

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
   || (idx) == RVBT_MVCC_DELETE_OBJECT \
   || (idx) == RVBT_MVCC_INSERT_OBJECT \
   || (idx) == RVBT_NON_MVCC_INSERT_OBJECT \
   || (idx) == RVBT_MARK_DELETED \
   || (idx) == RVBT_DELETE_OBJECT_POSTPONE \
   || (idx) == RVBT_MVCC_INSERT_OBJECT_UNQ \
   || (idx) == RVBT_MVCC_NOTIFY_VACUUM)

#define RCV_IS_LOGICAL_COMPENSATE_MANUAL(idx) \
  (RCV_IS_BTREE_LOGICAL_LOG(idx) \
   || (idx) == RVFL_ALLOC \
   || (idx) == RVFL_USER_PAGE_MARK_DELETE \
   || (idx) == RVPGBUF_DEALLOC)
#define RCV_IS_LOGICAL_RUN_POSTPONE_MANUAL(idx) \
  ((idx) == RVFL_DEALLOC)

#define RCV_IS_LOGICAL_LOG(vpid, idx) \
  (((vpid)->volid == NULL_VOLID) \
   || ((vpid)->pageid == NULL_PAGEID) \
   || RCV_IS_BTREE_LOGICAL_LOG (idx) \
   || (idx) == RVBT_MVCC_INCREMENTS_UPD \
   || (idx) == RVPGBUF_FLUSH_PAGE \
   || (idx) == RVFL_DESTROY \
   || (idx) == RVFL_ALLOC \
   || (idx) == RVFL_DEALLOC \
   || (idx) == RVVAC_NOTIFY_DROPPED_FILE \
   || (idx) == RVPGBUF_DEALLOC \
   || (idx) == RVES_NOTIFY_VACUUM)

#define RCV_IS_NEW_PAGE_INIT(idx) \
  ((idx) == RVPGBUF_NEW_PAGE \
   || (idx) == RVHF_NEWPAGE \
   || (idx) == RVOVF_NEWPAGE_INSERT \
   || (idx) == RVEH_INIT_BUCKET \
   || (idx) == RVEH_INIT_NEW_DIR_PAGE \
   || (idx) == RVEH_INIT_DIR \
   || (idx) == RVBT_GET_NEWPAGE \
   || (idx) == RVCT_NEWPAGE \
   || (idx) == RVVAC_DATA_INIT_NEW_PAGE \
   || (idx) == RVHF_CREATE_HEADER)

#endif /* _RECOVERY_H_ */
