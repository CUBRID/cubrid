/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * rv.h 
 * 									       
 * 	Overview: RECOVERY FUNCTIONS (AT SERVER) -- Interface --	       
 * See .c file for overview and description of the interface functions.	       
 * 									       
 */

#ifndef RV_HEADER_
#define RV_HEADER_

#ident "$Id$"

#include "logcp.h"
#include "error_manager.h"
#include "thread_impl.h"

typedef enum
{
  /*
     RULE ********************************************* 

     NEW ENTRIES SHOULD BE ADDED AT THE BOTTON OF THE FILE TO AVOID FULL
     RECOMPILATIONS (e.g., the file can be utimed) and to AVOID OLD DATABASES
     TO BE RECOVERED UNDER OLD FILE
   */
  RVDK_NEWVOL,
  RVDK_FORMAT,
  RVDK_INITMAP,
  RVDK_VHDR_SCALLOC,
  RVDK_VHDR_PGALLOC,
  RVDK_IDALLOC,
  RVDK_IDDEALLOC_WITH_VOLHEADER,
  RVDK_MAGIC,
  RVDK_CHANGE_CREATION,
  RVDK_RESET_BOOT_HFID,
  RVDK_LINK_PERM_VOLEXT,

  RVFL_CREATE_TMPFILE,
  RVFL_FTAB_CHAIN,
  RVFL_IDSTABLE,
  RVFL_MARKED_DELETED,
  RVFL_ALLOCSET_SECT,
  RVFL_ALLOCSET_PAGETB_ADDRESS,
  RVFL_ALLOCSET_NEW,
  RVFL_ALLOCSET_LINK,
  RVFL_ALLOCSET_ADD_PAGES,
  RVFL_ALLOCSET_DELETE_PAGES,
  RVFL_ALLOCSET_SECT_SHIFT,
  RVFL_ALLOCSET_COPY,
  RVFL_FHDR,
  RVFL_FHDR_ADD_LAST_ALLOCSET,
  RVFL_FHDR_REMOVE_LAST_ALLOCSET,
  RVFL_FHDR_CHANGE_LAST_ALLOCSET,
  RVFL_FHDR_ADD_PAGES,
  RVFL_FHDR_MARK_DELETED_PAGES,
  RVFL_FHDR_DELETE_PAGES,
  RVFL_FHDR_FTB_EXPANSION,
  RVFL_FILEDESC,
  RVFL_DES_FIRSTREST_NEXTVPID,
  RVFL_DES_NREST_NEXTVPID,
  RVFL_TRACKER_REGISTER,
  RVFL_LOGICAL_NOOP,

  RVHF_CREATE,
  RVHF_NEWPAGE,
  RVHF_STATS,
  RVHF_CHAIN,
  RVHF_INSERT,
  RVHF_DELETE,
  RVHF_DELETE_NEWHOME,
  RVHF_UPDATE,
  RVHF_UPDATE_TYPE,
  RVHF_REUSE_PAGE,

  RVOVF_NEWPAGE_LOGICAL_UNDO,
  RVOVF_NEWPAGE_INSERT,
  RVOVF_NEWPAGE_LINK,
  RVOVF_PAGE_UPDATE,
  RVOVF_CHANGE_LINK,

  RVEH_REPLACE,
  RVEH_INSERT,
  RVEH_DELETE,
  RVEH_INIT_BUCKET,
  RVEH_CONNECT_BUCKET,
  RVEH_INC_COUNTER,

  RVBT_NDHEADER_UPD,
  RVBT_NDHEADER_INS,
  RVBT_NDRECORD_UPD,
  RVBT_NDRECORD_INS,
  RVBT_NDRECORD_DEL,
  RVBT_PUT_PGRECORDS,
  RVBT_DEL_PGRECORDS,
  RVBT_GET_NEWPAGE,
  RVBT_NEW_PGALLOC,
  RVBT_KEYVAL_INS,
  RVBT_KEYVAL_DEL,
  RVBT_COPYPAGE,
  RVBT_LFRECORD_DEL,
  RVBT_LFRECORD_KEYINS,
  RVBT_LFRECORD_OIDINS,
  RVBT_NOOP,
  RVBT_ROOTHEADER_UPD,
  RVBT_UPDATE_OVFID,
  RVBT_INS_PGRECORDS,
  RVBT_OID_TRUNCATE,
  RVBT_CREATE_INDEX,

  RVCT_NEWPAGE,
  RVCT_INSERT,
  RVCT_DELETE,
  RVCT_UPDATE,
  RVCT_NEW_OVFPAGE_LOGICAL_UNDO,

  RVLOM_INSERT,
  RVLOM_DELETE,
  RVLOM_OVERWRITE,
  RVLOM_TAKEOUT,
  RVLOM_PUTIN,
  RVLOM_APPEND,
  RVLOM_SPLIT,
  RVLOM_GET_NEWPAGE,

  RVLOM_DIR_RCV_STATE,
  RVLOM_DIR_PG_REGION,
  RVLOM_DIR_NEW_PG,

  RVLOG_OUTSIDE_LOGICAL_REDO_NOOP,

  RVREPL_DATA_INSERT,
  RVREPL_DATA_UPDATE,
  RVREPL_DATA_DELETE,
  RVREPL_SCHEMA
} LOG_RCVINDEX;

/*
 * RECOVERY STRUCTURE SEEN BY RECOVERY FUNCTIONS                 
 */
typedef struct log_rcv LOG_RCV;
struct log_rcv
{				/* Recovery information */
  PAGE_PTR pgptr;		/* Page to recover. Page should not be free by recovery
				   functions, however it should be set dirty whenever is 
				   needed
				 */
  PGLENGTH offset;		/* Offset/slot of data in the above page to recover    */
  int length;			/* Length of data                                      */
  const char *data;		/* Replacement data. Pointer becomes invalid once the
				   recovery of the data is finished
				 */
};

/*
 * STRUCTURE ENTRY OF RECOVERY FUNCTIONS                     
 */

struct rvfun
{
  LOG_RCVINDEX recv_index;	/* For verification   */
  const char *recv_string;
  int (*undofun) (THREAD_ENTRY * thread_p, LOG_RCV * logrcv);	/* Undo function      */
  int (*redofun) (THREAD_ENTRY * thread_p, LOG_RCV * logrcv);	/* Redo function      */
  void (*dump_undofun) (int length, void *data);	/* Dump undo function */
  void (*dump_redofun) (int length, void *data);	/* Dump redo function */
};

extern struct rvfun RV_fun[];

extern const char *rv_rcvindex_string (LOG_RCVINDEX rcvindex);
extern void rv_check_rvfuns (void);

#endif
