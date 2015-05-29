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
     RULE *********************************************

     NEW ENTRIES SHOULD BE ADDED AT THE BOTTON OF THE FILE TO AVOID FULL
     RECOMPILATIONS (e.g., the file can be utimed) and to AVOID OLD DATABASES
     TO BE RECOVERED UNDER OLD FILE
   */
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

  RVFL_CREATE_TMPFILE = 11,
  RVFL_FTAB_CHAIN = 12,
  RVFL_IDSTABLE = 13,
  RVFL_MARKED_DELETED = 14,
  RVFL_ALLOCSET_SECT = 15,
  RVFL_ALLOCSET_PAGETB_ADDRESS = 16,
  RVFL_ALLOCSET_NEW = 17,
  RVFL_ALLOCSET_LINK = 18,
  RVFL_ALLOCSET_ADD_PAGES = 19,
  RVFL_ALLOCSET_DELETE_PAGES = 20,
  RVFL_ALLOCSET_SECT_SHIFT = 21,
  RVFL_ALLOCSET_COPY = 22,
  RVFL_FHDR = 23,
  RVFL_FHDR_ADD_LAST_ALLOCSET = 24,
  RVFL_FHDR_REMOVE_LAST_ALLOCSET = 25,
  RVFL_FHDR_CHANGE_LAST_ALLOCSET = 26,
  RVFL_FHDR_ADD_PAGES = 27,
  RVFL_FHDR_MARK_DELETED_PAGES = 28,
  RVFL_FHDR_DELETE_PAGES = 29,
  RVFL_FHDR_FTB_EXPANSION = 30,
  RVFL_FILEDESC_UPD = 31,
  RVFL_DES_FIRSTREST_NEXTVPID = 32,
  RVFL_DES_NREST_NEXTVPID = 33,
  RVFL_TRACKER_REGISTER = 34,
  RVFL_LOGICAL_NOOP = 35,

  RVHF_CREATE = 36,
  RVHF_NEWPAGE = 37,
  RVHF_STATS = 38,
  RVHF_CHAIN = 39,
  RVHF_INSERT = 40,
  RVHF_DELETE = 41,
  RVHF_DELETE_NEWHOME = 42,	/* Never used. Same as RVHF_DELETE. */
  RVHF_UPDATE = 43,
  RVHF_UPDATE_TYPE = 44,
  RVHF_REUSE_PAGE = 45,

  RVOVF_NEWPAGE_LOGICAL_UNDO = 46,
  RVOVF_NEWPAGE_INSERT = 47,
  RVOVF_NEWPAGE_LINK = 48,
  RVOVF_PAGE_UPDATE = 49,
  RVOVF_CHANGE_LINK = 50,

  RVEH_REPLACE = 51,
  RVEH_INSERT = 52,
  RVEH_DELETE = 53,
  RVEH_INIT_BUCKET = 54,
  RVEH_CONNECT_BUCKET = 55,
  RVEH_INC_COUNTER = 56,

  RVBT_NDHEADER_UPD = 57,
  RVBT_NDHEADER_INS = 58,
  RVBT_NDRECORD_UPD = 59,
  RVBT_NDRECORD_INS = 60,
  RVBT_NDRECORD_DEL = 61,
  RVBT_PUT_PGRECORDS = 62,
  RVBT_DEL_PGRECORDS = 63,
  RVBT_GET_NEWPAGE = 64,
  RVBT_NEW_PGALLOC = 65,
  RVBT_COPYPAGE = 66,
  RVBT_NOOP = 67,
  RVBT_ROOTHEADER_UPD = 68,
  RVBT_UPDATE_OVFID = 69,
  RVBT_INS_PGRECORDS = 70,
  RVBT_CREATE_INDEX = 71,

  RVCT_NEWPAGE = 72,
  RVCT_INSERT = 73,
  RVCT_DELETE = 74,
  RVCT_UPDATE = 75,
  RVCT_NEW_OVFPAGE_LOGICAL_UNDO = 76,

  RVLOM_INSERT = 77,
  RVLOM_DELETE = 78,
  RVLOM_OVERWRITE = 79,
  RVLOM_TAKEOUT = 80,
  RVLOM_PUTIN = 81,
  RVLOM_APPEND = 82,
  RVLOM_SPLIT = 83,
  RVLOM_GET_NEWPAGE = 84,

  RVLOM_DIR_RCV_STATE = 85,
  RVLOM_DIR_PG_REGION = 86,
  RVLOM_DIR_NEW_PG = 87,

  RVLOG_OUTSIDE_LOGICAL_REDO_NOOP = 88,

  RVREPL_DATA_INSERT = 89,
  RVREPL_DATA_UPDATE = 90,
  RVREPL_DATA_DELETE = 91,
  RVREPL_STATEMENT = 92,
  RVREPL_DATA_UPDATE_START = 93,
  RVREPL_DATA_UPDATE_END = 94,

  RVDK_IDDEALLOC_BITMAP_ONLY = 95,
  RVDK_IDDEALLOC_VHDR_ONLY = 96,

  RVHF_CREATE_REUSE_OID = 97,
  RVHF_NEWPAGE_REUSE_OID = 98,
  RVHF_REUSE_PAGE_REUSE_OID = 99,
  RVHF_MARK_REUSABLE_SLOT = 100,

  RVDK_INIT_PAGES = 101,

  RVEH_INIT_DIR = 102,

  RVFL_FILEDESC_INS = 103,

  RVHF_MVCC_INSERT = 104,
  RVHF_MVCC_DELETE_REC_HOME = 105,
  RVHF_MVCC_DELETE_OVERFLOW = 106,
  RVHF_MVCC_DELETE_REC_NEWHOME = 107,
  RVHF_MVCC_DELETE_MODIFY_HOME = 108,
  RVHF_MVCC_DELETE_NO_MODIFY_HOME = 109,
  RVHF_UPDATE_NOTIFY_VACUUM = 110,

  RVOVF_NEWPAGE_DELETE_RELOCATED = 111,

  RVBT_MVCC_DELETE_OBJECT = 112,

  RVVAC_HEAP_PAGE_VACUUM = 113,
  RVVAC_REMOVE_OVF_INSID = 114,
  RVVAC_LOG_BLOCK_REMOVE = 115,
  RVVAC_LOG_BLOCK_APPEND = 116,
  RVVAC_LOG_BLOCK_SAVE = 117,
  RVVAC_UPDATE_OLDEST_MVCCID = 118,
  RVVAC_START_OR_END_JOB = 119,
  RVVAC_DROPPED_FILE_CLEANUP = 120,
  RVVAC_DROPPED_FILE_NEXT_PAGE = 121,
  RVVAC_DROPPED_FILE_ADD = 122,

  RVBT_MVCC_INCREMENTS_UPD = 123,

  RVBT_MVCC_NOTIFY_VACUUM = 124,

  RVES_NOTIFY_VACUUM = 125,

  RVBO_DELVOL = 126,

  RVBT_LOG_GLOBAL_UNIQUE_STATS_COMMIT = 127,

  RVBT_DELETE_OBJECT_PHYSICAL = 128,
  RVBT_NON_MVCC_INSERT_OBJECT = 129,
  RVBT_MVCC_INSERT_OBJECT = 130,
  RVBT_RECORD_MODIFY_NO_UNDO = 131,
  RVBT_RECORD_MODIFY_COMPENSATE = 132,

  RVHF_MVCC_REMOVE_PARTITION_LINK = 133,

  RVBT_DELETE_INDEX = 134,

  RVHF_PARTITION_LINK_FLAG = 135,

  RVLOC_CLASS_RENAME = 136,

  RVHF_INSERT_NEWHOME = 137,	/* Same as RVHF_INSERT but no replication */

  RVFL_POSTPONE_DESTROY_FILE = 138,

  RVBT_MVCC_UPDATE_SAME_KEY = 139,
  RVBT_RECORD_MODIFY_UNDOREDO = 140,

  RV_LAST_LOGID = RVBT_RECORD_MODIFY_UNDOREDO,

  RV_NOT_DEFINED = 999
} LOG_RCVINDEX;

/*
 * RECOVERY STRUCTURE SEEN BY RECOVERY FUNCTIONS
 */
typedef struct log_rcv LOG_RCV;
struct log_rcv
{				/* Recovery information */
  MVCCID mvcc_id;		/* mvcc id */
  PAGE_PTR pgptr;		/* Page to recover. Page should not be free by recovery
				   functions, however it should be set dirty whenever is
				   needed
				 */
  PGLENGTH offset;		/* Offset/slot of data in the above page to recover */
  int length;			/* Length of data */
  const char *data;		/* Replacement data. Pointer becomes invalid once the
				   recovery of the data is finished
				 */
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
   || ((idx) == RVBT_MVCC_UPDATE_SAME_KEY))

#define RCV_IS_LOGICAL_LOG(vpid, idx) \
  ((((vpid)->volid == NULL_VOLID) \
    || ((vpid)->pageid == NULL_PAGEID) \
    || RCV_IS_BTREE_LOGICAL_LOG (idx) \
    || ((idx) == RVBT_MVCC_INCREMENTS_UPD) \
    || ((idx) == RVBT_CREATE_INDEX)) ? true : false)

#endif /* _RECOVERY_H_ */
