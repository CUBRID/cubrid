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
  RVHF_DELETE_NEWHOME = 42,
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
  RVBT_KEYVAL_INS = 66,
  RVBT_KEYVAL_DEL = 67,
  RVBT_COPYPAGE = 68,
  /* Never use this recovery index anymore. Only for backward compatibility */
  RVBT_LFRECORD_DEL = 69,
  RVBT_LFRECORD_KEYINS = 70,
  /* Never use this recovery index anymore. Only for backward compatibility */
  RVBT_LFRECORD_OIDINS = 71,
  RVBT_NOOP = 72,
  RVBT_ROOTHEADER_UPD = 73,
  RVBT_UPDATE_OVFID = 74,
  RVBT_INS_PGRECORDS = 75,
  /* Never use this recovery index anymore. Only for backward compatibility */
  RVBT_OID_TRUNCATE = 76,
  RVBT_CREATE_INDEX = 77,

  RVCT_NEWPAGE = 78,
  RVCT_INSERT = 79,
  RVCT_DELETE = 80,
  RVCT_UPDATE = 81,
  RVCT_NEW_OVFPAGE_LOGICAL_UNDO = 82,

  RVLOM_INSERT = 83,
  RVLOM_DELETE = 84,
  RVLOM_OVERWRITE = 85,
  RVLOM_TAKEOUT = 86,
  RVLOM_PUTIN = 87,
  RVLOM_APPEND = 88,
  RVLOM_SPLIT = 89,
  RVLOM_GET_NEWPAGE = 90,

  RVLOM_DIR_RCV_STATE = 91,
  RVLOM_DIR_PG_REGION = 92,
  RVLOM_DIR_NEW_PG = 93,

  RVLOG_OUTSIDE_LOGICAL_REDO_NOOP = 94,

  RVREPL_DATA_INSERT = 95,
  RVREPL_DATA_UPDATE = 96,
  RVREPL_DATA_DELETE = 97,
  RVREPL_SCHEMA = 98,
  RVREPL_DATA_UPDATE_START = 99,
  RVREPL_DATA_UPDATE_END = 100,

  RVDK_IDDEALLOC_BITMAP_ONLY = 101,
  RVDK_IDDEALLOC_VHDR_ONLY = 102,

  RVHF_CREATE_REUSE_OID = 103,
  RVHF_NEWPAGE_REUSE_OID = 104,
  RVHF_REUSE_PAGE_REUSE_OID = 105,
  RVHF_MARK_REUSABLE_SLOT = 106,

  RVBT_KEYVAL_INS_LFRECORD_KEYINS = 107,
  RVBT_KEYVAL_INS_LFRECORD_OIDINS = 108,
  RVBT_KEYVAL_DEL_LFRECORD_DEL = 109,
  RVBT_KEYVAL_DEL_NDRECORD_UPD = 110,
  RVBT_KEYVAL_DEL_NDHEADER_UPD = 111,
  RVBT_KEYVAL_DEL_OID_TRUNCATE = 112,	/* unused */

  RVDK_INIT_PAGES = 113,

  RVEH_INIT_DIR = 114,

  RVFL_FILEDESC_INS = 115,

  RVHF_MVCC_INSERT = 116,
  RVHF_MVCC_DELETE = 117,
  RVHF_MVCC_DELETE_RELOCATED = 118,
  RVHF_MVCC_DELETE_RELOCATION = 119,
  RVHF_MVCC_MODIFY_RELOCATION_LINK = 120,
  RVOVF_NEWPAGE_DELETE_RELOCATED = 121,

  RVBT_KEYVAL_INS_LFRECORD_MVCC_DELID = 122,

  RVBT_KEYVAL_DEL_RECORD_MVCC_DELID = 123,

  RVBT_KEYVAL_MVCC_INS = 124,
  RVBT_KEYVAL_MVCC_INS_LFRECORD_KEYINS = 125,
  RVBT_KEYVAL_MVCC_INS_LFRECORD_OIDINS = 126,

  RVVAC_REMOVE_HEAP_OIDS = 127,
  RVVAC_REMOVE_OVF_INSID = 128,
  RVVAC_LOG_BLOCK_REMOVE = 129,
  RVVAC_LOG_BLOCK_APPEND = 130,
  RVVAC_LOG_BLOCK_MODIFY = 131,
  RVVAC_DROPPED_FILE_CLEANUP = 132,
  RVVAC_DROPPED_FILE_NEXT_PAGE = 133,
  RVVAC_DROPPED_FILE_ADD = 134,

  RVBT_MVCC_INCREMENTS_UPD = 135,

  RVBT_MVCC_NOTIFY_VACUUM = 136,

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
#if defined(CUBRID_DEBUG)
extern void rv_check_rvfuns (void);
#endif /* CUBRID_DEBUG */

#define RCV_IS_LOGICAL_LOG(vpid, idx) \
  ((((vpid)->volid == NULL_VOLID) \
    || ((vpid)->pageid == NULL_PAGEID) \
    || ((idx) == RVBT_KEYVAL_INS_LFRECORD_KEYINS) \
    || ((idx) == RVBT_KEYVAL_INS_LFRECORD_OIDINS) \
    || ((idx) == RVBT_KEYVAL_INS) \
    || ((idx) == RVBT_KEYVAL_DEL) \
    || ((idx) == RVBT_KEYVAL_DEL_LFRECORD_DEL) \
    || ((idx) == RVBT_KEYVAL_DEL_NDRECORD_UPD) \
    || ((idx) == RVBT_KEYVAL_DEL_NDHEADER_UPD) \
    || ((idx) == RVBT_KEYVAL_DEL_OID_TRUNCATE) \
    || ((idx) == RVBT_KEYVAL_INS_LFRECORD_MVCC_DELID) \
    || ((idx) == RVBT_KEYVAL_DEL_RECORD_MVCC_DELID) \
    || ((idx) == RVBT_KEYVAL_MVCC_INS) \
    || ((idx) == RVBT_KEYVAL_MVCC_INS_LFRECORD_KEYINS) \
    || ((idx) == RVBT_KEYVAL_MVCC_INS_LFRECORD_OIDINS) \
    || ((idx) == RVBT_MVCC_INCREMENTS_UPD) \
    || ((idx) == RVBT_MVCC_NOTIFY_VACUUM)) ? true : false)

#endif /* _RECOVERY_H_ */
