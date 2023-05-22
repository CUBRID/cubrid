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
 * recovery.h: recovery functions (at server)
 */

#ifndef _RECOVERY_H_
#define _RECOVERY_H_

#ident "$Id$"

#include "error_manager.h"
#include "log_comm.h"
#include "log_lsa.hpp"
#include "thread_compat.hpp"

#include <stdio.h>

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
  RVDK_EXPAND_VOLUME = 6,
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
  RVFL_EXTDATA_UPDATE_ITEM = 28,
  RVFL_TRACKER_HEAP_MARK_DELETED = 29,
  RVFL_TRACKER_HEAP_REUSE = 30,
  RVFL_TRACKER_UNREGISTER = 31,
  RVFL_FHEAD_CONVERT_FTAB_TO_USER = 32,

  RVHF_CREATE_HEADER = 33,
  RVHF_NEWPAGE = 34,
  RVHF_STATS = 35,
  RVHF_CHAIN = 36,
  RVHF_INSERT = 37,
  RVHF_DELETE = 38,
  RVHF_UPDATE = 39,
  RVHF_REUSE_PAGE = 40,
  RVHF_REUSE_PAGE_REUSE_OID = 41,
  RVHF_MARK_REUSABLE_SLOT = 42,
  RVHF_MVCC_INSERT = 43,
  RVHF_MVCC_DELETE_REC_HOME = 44,
  RVHF_MVCC_DELETE_OVERFLOW = 45,
  RVHF_MVCC_DELETE_REC_NEWHOME = 46,
  RVHF_MVCC_DELETE_MODIFY_HOME = 47,
  RVHF_MVCC_NO_MODIFY_HOME = 48,
  RVHF_UPDATE_NOTIFY_VACUUM = 49,
  RVHF_INSERT_NEWHOME = 50,	/* Same as RVHF_INSERT but no replication */
  RVHF_MVCC_REDISTRIBUTE = 51,
  RVHF_MVCC_UPDATE_OVERFLOW = 52,
  RVHF_MARK_DELETED = 53,

  RVOVF_NEWPAGE_INSERT = 54,	/* required for HA */
  RVOVF_NEWPAGE_LINK = 55,
  RVOVF_PAGE_UPDATE = 56,
  RVOVF_CHANGE_LINK = 57,

  RVEH_REPLACE = 58,
  RVEH_INSERT = 59,
  RVEH_DELETE = 60,
  RVEH_INIT_BUCKET = 61,
  RVEH_CONNECT_BUCKET = 62,
  RVEH_INC_COUNTER = 63,
  RVEH_INIT_DIR = 64,
  RVEH_INIT_NEW_DIR_PAGE = 65,

  RVBT_NDHEADER_UPD = 66,
  RVBT_NDHEADER_INS = 67,
  RVBT_NDRECORD_UPD = 68,
  RVBT_NDRECORD_INS = 69,
  RVBT_NDRECORD_DEL = 70,
  RVBT_DEL_PGRECORDS = 71,
  RVBT_GET_NEWPAGE = 72,
  RVBT_COPYPAGE = 73,
  RVBT_ROOTHEADER_UPD = 74,
  RVBT_UPDATE_OVFID = 75,
  RVBT_INS_PGRECORDS = 76,
  RVBT_MVCC_DELETE_OBJECT = 77,
  RVBT_MVCC_INCREMENTS_UPD = 78,
  RVBT_MVCC_NOTIFY_VACUUM = 79,
  RVBT_LOG_GLOBAL_UNIQUE_STATS_COMMIT = 80,
  RVBT_DELETE_OBJECT_PHYSICAL = 81,
  RVBT_NON_MVCC_INSERT_OBJECT = 82,
  RVBT_MVCC_INSERT_OBJECT = 83,
  RVBT_MVCC_INSERT_OBJECT_UNQ = 84,
  RVBT_RECORD_MODIFY_UNDOREDO = 85,
  RVBT_RECORD_MODIFY_NO_UNDO = 86,
  RVBT_RECORD_MODIFY_COMPENSATE = 87,
  RVBT_REMOVE_UNIQUE_STATS = 88,
  RVBT_DELETE_OBJECT_POSTPONE = 89,
  RVBT_MARK_DELETED = 90,
  RVBT_MARK_DEALLOC_PAGE = 91,

  RVCT_NEWPAGE = 92,
  RVCT_INSERT = 93,
  RVCT_DELETE = 94,
  RVCT_UPDATE = 95,
  RVCT_NEW_OVFPAGE_LOGICAL_UNDO = 96,

  RVLOG_OUTSIDE_LOGICAL_REDO_NOOP = 97,

  RVREPL_DATA_INSERT = 98,
  RVREPL_DATA_UPDATE = 99,
  RVREPL_DATA_DELETE = 100,
  RVREPL_STATEMENT = 101,
  RVREPL_DATA_UPDATE_START = 102,
  RVREPL_DATA_UPDATE_END = 103,

  RVVAC_COMPLETE = 104,
  RVVAC_START_JOB = 105,
  RVVAC_DATA_APPEND_BLOCKS = 106,
  RVVAC_DATA_INIT_NEW_PAGE = 107,
  RVVAC_DATA_SET_LINK = 108,
  RVVAC_DATA_FINISHED_BLOCKS = 109,
  RVVAC_NOTIFY_DROPPED_FILE = 110,
  RVVAC_DROPPED_FILE_CLEANUP = 111,
  RVVAC_DROPPED_FILE_NEXT_PAGE = 112,
  RVVAC_DROPPED_FILE_ADD = 113,
  RVVAC_DROPPED_FILE_REPLACE = 114,
  RVVAC_HEAP_RECORD_VACUUM = 115,
  RVVAC_HEAP_PAGE_VACUUM = 116,
  RVVAC_REMOVE_OVF_INSID = 117,

  RVES_NOTIFY_VACUUM = 118,

  RVLOC_CLASSNAME_DUMMY = 119,

  RVPGBUF_FLUSH_PAGE = 120,
  RVPGBUF_NEW_PAGE = 121,
  RVPGBUF_DEALLOC = 122,
  RVPGBUF_COMPENSATE_DEALLOC = 123,

  RVBT_ONLINE_INDEX_UNDO_TRAN_INSERT = 124,
  RVBT_ONLINE_INDEX_UNDO_TRAN_DELETE = 125,
  RVHF_APPEND_PAGES_TO_HEAP = 126,

  RVPGBUF_SET_TDE_ALGORITHM = 127,
  RVFL_FHEAD_SET_TDE_ALGORITHM = 128,

  RV_LAST_LOGID = RVFL_FHEAD_SET_TDE_ALGORITHM,

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
   || (idx) == RVBT_MVCC_NOTIFY_VACUUM \
   || (idx) == RVBT_ONLINE_INDEX_UNDO_TRAN_DELETE \
   || (idx) == RVBT_ONLINE_INDEX_UNDO_TRAN_INSERT)

#define RCV_IS_LOGICAL_COMPENSATE_MANUAL(idx) \
  (RCV_IS_BTREE_LOGICAL_LOG(idx) \
   || (idx) == RVFL_ALLOC \
   || (idx) == RVFL_USER_PAGE_MARK_DELETE \
   || (idx) == RVPGBUF_DEALLOC \
   || (idx) == RVFL_TRACKER_HEAP_REUSE \
   || (idx) == RVFL_TRACKER_UNREGISTER)
#define RCV_IS_LOGICAL_RUN_POSTPONE_MANUAL(idx) \
  ((idx) == RVFL_DEALLOC \
   || (idx) == RVHF_MARK_DELETED \
   || (idx) == RVBT_DELETE_OBJECT_POSTPONE)

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
   || (idx) == RVES_NOTIFY_VACUUM \
   || (idx) == RVHF_MARK_DELETED \
   || (idx) == RVFL_TRACKER_HEAP_REUSE \
   || (idx) == RVFL_TRACKER_UNREGISTER)

#endif /* _RECOVERY_H_ */
