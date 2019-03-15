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
 * log implementation common part to server/client modules.
 */

#ifndef _LOG_COMMON_IMPL_H_
#define _LOG_COMMON_IMPL_H_

#include "file_io.h"
#include "log_comm.h"
#include "log_lsa.hpp"
#include "log_record.hpp"
#include "recovery.h"
#include "release_string.h"
#include "storage_common.h"
#include "system.h"

/************************************************************************/
/* Section shared with client... TODO: remove any code accessing log    */
/* module on client. Most are used by log_writer.c and log_applier.c    */
/************************************************************************/

#define LOGPB_HEADER_PAGE_ID             (-9)	/* The first log page in the infinite log sequence. It is always kept
						 * on the active portion of the log. Log records are not stored on this
						 * page. This page is backed up in all archive logs */

#define LOGPB_IO_NPAGES                  4

#define LOGPB_BUFFER_NPAGES_LOWER        128

/*
 * Message id in the set MSGCAT_SET_LOG
 * in the message catalog MSGCAT_CATALOG_CUBRID (file cubrid.msg).
 */
#define MSGCAT_LOG_STARTS                               1
#define MSGCAT_LOG_LOGARCHIVE_NEEDED                    2
#define MSGCAT_LOG_BACKUPINFO_NEEDED                    3
#define MSGCAT_LOG_NEWLOCATION                          4
#define MSGCAT_LOG_LOGINFO_COMMENT                      5
#define MSGCAT_LOG_LOGINFO_COMMENT_ARCHIVE_NONEEDED     6
#define MSGCAT_LOG_LOGINFO_COMMENT_MANY_ARCHIVES_NONEEDED 7
#define MSGCAT_LOG_LOGINFO_COMMENT_FROM_RENAMED         8
#define MSGCAT_LOG_LOGINFO_ARCHIVE                      9
#define MSGCAT_LOG_LOGINFO_KEYWORD_ARCHIVE              10
#define MSGCAT_LOG_LOGINFO_REMOVE_REASON                11
#define MSGCAT_LOG_LOGINFO_ACTIVE                       12
#define MSGCAT_LOG_FINISH_COMMIT                        13
#define MSGCAT_LOG_FINISH_ABORT                         14
#define MSGCAT_LOG_INCOMPLTE_MEDIA_RECOVERY             15
#define MSGCAT_LOG_RESETLOG_DUE_INCOMPLTE_MEDIA_RECOVERY 16
#define MSGCAT_LOG_DATABASE_BACKUP_WAS_TAKEN            17
#define MSGCAT_LOG_MEDIACRASH_NOT_IMPORTANT             18
#define MSGCAT_LOG_DELETE_BKVOLS                        19
#define MSGCAT_LOG_ENTER_Y2_CONFIRM                     20
#define MSGCAT_LOG_BACKUP_HALTED_BY_USER                21
#define MSGCAT_LOG_LOGINFO_ARCHIVES_NEEDED_FOR_RESTORE  22
#define MSGCAT_LOG_LOGINFO_PENDING_ARCHIVES_RELEASED    23
#define MSGCAT_LOG_LOGINFO_NOTPENDING_ARCHIVE_COMMENT   24
#define MSGCAT_LOG_LOGINFO_MULT_NOTPENDING_ARCHIVES_COMMENT 25
#define MSGCAT_LOG_READ_ERROR_DURING_RESTORE            26
#define MSGCAT_LOG_INPUT_RANGE_ERROR                    27
#define MSGCAT_LOG_UPTODATE_ERROR                       28
#define MSGCAT_LOG_LOGINFO_COMMENT_UNUSED_ARCHIVE_NAME	29
#define MSGCAT_LOG_MAX_ARCHIVES_HAS_BEEN_EXCEEDED	30

/*
 * LOG PAGE
 */

typedef struct log_hdrpage LOG_HDRPAGE;
struct log_hdrpage
{
  LOG_PAGEID logical_pageid;	/* Logical pageid in infinite log */
  PGLENGTH offset;		/* Offset of first log record in this page. This may be useful when previous log page
				 * is corrupted and an archive of that page does not exist. Instead of losing the whole
				 * log because of such bad page, we could salvage the log starting at the offset
				 * address, that is, at the next log record */
  short dummy1;			/* Dummy field for 8byte align */
  int checksum;			/* checksum - currently CRC32 is used to check log page consistency. */
};

/* WARNING:
 * Don't use sizeof(LOG_PAGE) or of any structure that contains it
 * Use macro LOG_PAGESIZE instead.
 * It is also bad idea to allocate a variable for LOG_PAGE on the stack.
 */

typedef struct log_page LOG_PAGE;
struct log_page
{				/* The log page */
  LOG_HDRPAGE hdr;
  char area[1];
};

/* Uses 0xff to fills up the page, before writing in it. This helps recovery to detect the end of the log in
 * case of log page corruption, caused by partial page flush. Thus, at recovery analysis, we can easily
 * detect the last valid log record - the log record having NULL_LSA (0xff) in its forward address field.
 * If we do not use 0xff, a corrupted log record will be considered valid at recovery, thus affecting
 * the database consistency.
 */
#define LOG_PAGE_INIT_VALUE 0xff

/*
 * This structure encapsulates various information and metrics related
 * to each backup level.
 * Estimates and heuristics are not currently used but are placeholder
 * for the future to avoid changing the physical representation again.
 */
typedef struct log_hdr_bkup_level_info LOG_HDR_BKUP_LEVEL_INFO;
struct log_hdr_bkup_level_info
{
  INT64 bkup_attime;		/* Timestamp when this backup lsa taken */
  INT64 io_baseln_time;		/* time (secs.) to write a single page */
  INT64 io_bkuptime;		/* total time to write the backup */
  int ndirty_pages_post_bkup;	/* number of pages written since the lsa for this backup level. */
  int io_numpages;		/* total number of pages in last backup */
};

#define MAXLOGNAME          (30 - 12)

#define NUM_NORMAL_TRANS (prm_get_integer_value (PRM_ID_CSS_MAX_CLIENTS))
#define NUM_SYSTEM_TRANS 1
#define NUM_NON_SYSTEM_TRANS (css_get_max_conn ())
#define MAX_NTRANS \
  (NUM_NON_SYSTEM_TRANS + NUM_SYSTEM_TRANS)

// vacuum blocks
typedef INT64 VACUUM_LOG_BLOCKID;
#define VACUUM_NULL_LOG_BLOCKID -1

/*
 * LOG HEADER INFORMATION
 */
typedef struct log_header LOG_HEADER;
struct log_header
{				/* Log header information */
  char magic[CUBRID_MAGIC_MAX_LENGTH];	/* Magic value for file/magic Unix utility */
  /* Here exists 3 bytes */
  INT32 dummy;			/* for 8byte align */
  INT64 db_creation;		/* Database creation time. For safety reasons, this value is set on all volumes and the
				 * log. The value is generated by the log manager */
  char db_release[REL_MAX_RELEASE_LENGTH];	/* CUBRID Release */
  /* Here exists 1 byte */
  float db_compatibility;	/* Compatibility of the database against the current release of CUBRID */
  PGLENGTH db_iopagesize;	/* Size of pages in the database. For safety reasons this value is recorded in the log
				 * to make sure that the database is always run with the same page size */
  PGLENGTH db_logpagesize;	/* Size of log pages in the database. */
  bool is_shutdown;		/* Was the log shutdown ? */
  /* Here exists 3 bytes */
  TRANID next_trid;		/* Next Transaction identifier */
  MVCCID mvcc_next_id;		/* Next MVCC ID */
  int avg_ntrans;		/* Number of average transactions */
  int avg_nlocks;		/* Average number of object locks */
  DKNPAGES npages;		/* Number of pages in the active log portion. Does not include the log header page. */
  INT8 db_charset;
  bool was_copied;		/* set to true for copied database; should be reset on first server start */
  INT8 dummy3;			/* Dummy fields for 8byte align */
  INT8 dummy4;
  LOG_PAGEID fpageid;		/* Logical pageid at physical location 1 in active log */
  LOG_LSA append_lsa;		/* Current append location */
  LOG_LSA chkpt_lsa;		/* Lowest log sequence address to start the recovery process */
  LOG_PAGEID nxarv_pageid;	/* Next logical page to archive */
  LOG_PHY_PAGEID nxarv_phy_pageid;	/* Physical location of logical page to archive */
  int nxarv_num;		/* Next log archive number */
  int last_arv_num_for_syscrashes;	/* Last log archive needed for system crashes */
  int last_deleted_arv_num;	/* Last deleted archive number */
  LOG_LSA bkup_level0_lsa;	/* Lsa of backup level 0 */
  LOG_LSA bkup_level1_lsa;	/* Lsa of backup level 1 */
  LOG_LSA bkup_level2_lsa;	/* Lsa of backup level 2 */
  char prefix_name[MAXLOGNAME];	/* Log prefix name */
  bool has_logging_been_skipped;	/* Has logging been skipped ? */
  /* Here exists 5 bytes */
  VACUUM_LOG_BLOCKID vacuum_last_blockid;	/* Last processed blockid needed for vacuum. */
  int perm_status;		/* Reserved for future expansion and permanent status indicators, e.g. to mark
				 * RESTORE_IN_PROGRESS */
  /* Here exists 4 bytes */
  LOG_HDR_BKUP_LEVEL_INFO bkinfo[FILEIO_BACKUP_UNDEFINED_LEVEL];
  /* backup specific info for future growth */

  int ha_server_state;
  int ha_file_status;
  LOG_LSA eof_lsa;

  LOG_LSA smallest_lsa_at_last_chkpt;

  LOG_LSA mvcc_op_log_lsa;	/* Used to link log entries for mvcc operations. Vacuum will then process these entries */
  MVCCID last_block_oldest_mvccid;	/* Used to find the oldest MVCCID in a block of log data. */
  MVCCID last_block_newest_mvccid;	/* Used to find the newest MVCCID in a block of log data. */

  INT64 ha_promotion_time;
  INT64 db_restore_time;
  bool mark_will_del;
};
#define LOG_HEADER_INITIALIZER                   \
  {                                              \
     /* magic */                                 \
     {'0'},                                      \
     0, 0,                                       \
     /* db_release */                            \
     {'0'},                                      \
     /* db_compatibility */                      \
     0.0,                                        \
     0, 0, 0,                                    \
     /* next_trid */                             \
     (LOG_SYSTEM_TRANID + 1),                    \
     /* mvcc_id */				 \
     MVCCID_NULL,                                \
     0, 0, 0,					 \
     /* db_charset */				 \
     0,						 \
     /* was_copied */                            \
     false,                                      \
     /* dummy INT8 for align */                  \
     0, 0,                                       \
     /* fpageid */                               \
     0,				                 \
     /* append_lsa */                            \
     {NULL_PAGEID, NULL_OFFSET},                 \
     /* chkpt_lsa */                             \
     {NULL_PAGEID, NULL_OFFSET},                 \
     /* nxarv_pageid */                          \
     0,                                          \
     /* nxarv_phy_pageid */                      \
     0,                                          \
     0, 0, 0,                                    \
     /* bkup_level0_lsa */                       \
     {NULL_PAGEID, NULL_OFFSET},                 \
     /* bkup_level1_lsa */                       \
     {NULL_PAGEID, NULL_OFFSET},                 \
     /* bkup_level2_lsa */                       \
     {NULL_PAGEID, NULL_OFFSET},                 \
     /* prefix_name */                           \
     {'0'},                                      \
     /* has_logging_been_skipped */              \
     false,                                      \
     0, 0,                                       \
     /* bkinfo */                                \
     {{0, 0, 0, 0, 0}},                          \
     0, 0,                                       \
     /* eof_lsa */                               \
     {NULL_PAGEID, NULL_OFFSET},                 \
     /* smallest_lsa_at_last_chkpt */            \
     {NULL_PAGEID, NULL_OFFSET},                 \
     /* mvcc_op_log_lsa */			 \
     {NULL_PAGEID, NULL_OFFSET},                 \
     /* last_block_oldest_mvccid */		 \
     MVCCID_NULL,				 \
     /* last_block_newest_mvccid */		 \
     MVCCID_NULL,				 \
     /* ha_promotion_time */ 			 \
     0, 					 \
     /* db_restore_time */			 \
     0,						 \
     /* mark_will_del */			 \
     false					 \
  }

#define LOGWR_HEADER_INITIALIZER                 \
  {                                              \
     /* magic */                                 \
     {'0'},                                      \
     0, 0,                                       \
     /* db_release */                            \
     {'0'},                                      \
     /* db_compatibility */                      \
     0.0,                                        \
     0, 0, 0,                                    \
     /* next_trid */                             \
     NULL_TRANID,                                \
     /* mvcc_next_id */                          \
     MVCCID_NULL,                                \
     0, 0, 0,					 \
     /* db_charset */				 \
     0,						 \
     /* was_copied */                            \
     false,                                      \
     /* dummy INT8 for align */                  \
     0, 0,                                       \
     /* fpageid */                               \
     0,				                 \
     /* append_lsa */                            \
     {NULL_PAGEID, NULL_OFFSET},                 \
     /* chkpt_lsa */                             \
     {NULL_PAGEID, NULL_OFFSET},                 \
     /* nxarv_pageid */                          \
     NULL_PAGEID,                                \
     /* nxarv_phy_pageid */                      \
     NULL_PAGEID,                                \
     -1, -1, -1,                                 \
     /* bkup_level0_lsa */                       \
     {NULL_PAGEID, NULL_OFFSET},                 \
     /* bkup_level1_lsa */                       \
     {NULL_PAGEID, NULL_OFFSET},                 \
     /* bkup_level2_lsa */                       \
     {NULL_PAGEID, NULL_OFFSET},                 \
     /* prefix_name */                           \
     {'0'},                                      \
     /* has_logging_been_skipped */              \
     false,                                      \
     0, 0,                                       \
     /* bkinfo */                                \
     {{0, 0, 0, 0, 0}},                          \
     0, 0,                                       \
     /* eof_lsa */                               \
     {NULL_PAGEID, NULL_OFFSET},                 \
     /* smallest_lsa_at_last_chkpt */            \
     {NULL_PAGEID, NULL_OFFSET},                 \
     /* mvcc_op_log_lsa */			 \
     {NULL_PAGEID, NULL_OFFSET},                 \
     /* last_block_oldest_mvccid */		 \
     MVCCID_NULL,				 \
     /* last_block_newest_mvccid */		 \
     MVCCID_NULL,				 \
     /* ha_promotion_time */ 			 \
     0, 					 \
     /* db_restore_time */			 \
     0,						 \
     /* mark_will_del */			 \
     false					 \
  }

enum logwr_mode
{
  LOGWR_MODE_ASYNC = 1,
  LOGWR_MODE_SEMISYNC,
  LOGWR_MODE_SYNC
};
typedef enum logwr_mode LOGWR_MODE;
#define LOGWR_COPY_FROM_FIRST_PHY_PAGE_MASK	(0x80000000)

typedef struct background_archiving_info BACKGROUND_ARCHIVING_INFO;
struct background_archiving_info
{
  LOG_PAGEID start_page_id;
  LOG_PAGEID current_page_id;
  LOG_PAGEID last_sync_pageid;
  int vdes;
};
#define BACKGROUND_ARCHIVING_INFO_INITIALIZER \
  { NULL_PAGEID, NULL_PAGEID, NULL_PAGEID, NULL_VOLDES }

typedef struct log_bgarv_header LOG_BGARV_HEADER;
struct log_bgarv_header
{				/* Background log archive header information */
  char magic[CUBRID_MAGIC_MAX_LENGTH];

  INT32 dummy;
  INT64 db_creation;

  LOG_PAGEID start_page_id;
  LOG_PAGEID current_page_id;
  LOG_PAGEID last_sync_pageid;
};
#define LOG_BGARV_HEADER_INITIALIZER \
  { /* magic */ {'0'}, 0, 0, NULL_PAGEID, NULL_PAGEID, NULL_PAGEID }

typedef struct log_arv_header LOG_ARV_HEADER;
struct log_arv_header
{				/* Log archive header information */
  char magic[CUBRID_MAGIC_MAX_LENGTH];	/* Magic value for file/magic Unix utility */
  INT32 dummy;			/* for 8byte align */
  INT64 db_creation;		/* Database creation time. For safety reasons, this value is set on all volumes and the
				 * log. The value is generated by the log manager */
  TRANID next_trid;		/* Next Transaction identifier */
  DKNPAGES npages;		/* Number of pages in the archive log */
  LOG_PAGEID fpageid;		/* Logical pageid at physical location 1 in archive log */
  int arv_num;			/* The archive number */
  INT32 dummy2;			/* Dummy field for 8byte align */
};
#define LOG_ARV_HEADER_INITIALIZER \
  { /* magic */ {'0'}, 0, 0, 0, 0, 0, 0, 0 }

/* there can be following transitions in transient lobs

   -------------------------------------------------------------------------
   | 	       locator  | created               | deleted		   |
   |--------------------|-----------------------|--------------------------|
   | in     | transient | LOB_TRANSIENT_CREATED i LOB_UNKNOWN		   |
   | tran   |-----------|-----------------------|--------------------------|
   |        | permanent | LOB_PERMANENT_CREATED | LOB_PERMANENT_DELETED    |
   |--------------------|-----------------------|--------------------------|
   | out of | transient | LOB_UNKNOWN		| LOB_UNKNOWN		   |
   | tran   |-----------|-----------------------|--------------------------|
   |        | permanent | LOB_UNKNOWN 		| LOB_TRANSIENT_DELETED    |
   -------------------------------------------------------------------------

   s1: create a transient locator and delete it
       LOB_TRANSIENT_CREATED -> LOB_UNKNOWN

   s2: create a transient locator and bind it to a row in table
       LOB_TRANSIENT_CREATED -> LOB_PERMANENT_CREATED

   s3: bind a transient locator to a row and delete the locator
       LOB_PERMANENT_CREATED -> LOB_PERMANENT_DELETED

   s4: delete a locator to be create out of transaction
       LOB_UNKNOWN -> LOB_TRANSIENT_DELETED

 */
enum lob_locator_state
{
  LOB_UNKNOWN,
  LOB_TRANSIENT_CREATED,
  LOB_TRANSIENT_DELETED,
  LOB_PERMANENT_CREATED,
  LOB_PERMANENT_DELETED,
  LOB_NOT_FOUND
};
typedef enum lob_locator_state LOB_LOCATOR_STATE;

enum LOG_HA_FILESTAT
{
  LOG_HA_FILESTAT_CLEAR = 0,
  LOG_HA_FILESTAT_ARCHIVED = 1,
  LOG_HA_FILESTAT_SYNCHRONIZED = 2
};

/*
 * NOTE: NULL_VOLID generally means a bad volume identifier
 *       Negative volume identifiers are used to identify auxiliary files and
 *       volumes (e.g., logs, backups)
 */

#define LOG_MAX_DBVOLID          (VOLID_MAX - 1)

/* Volid of database.txt */
#define LOG_DBTXT_VOLID          (SHRT_MIN + 1)
#define LOG_DBFIRST_VOLID        0

/* Volid of volume information */
#define LOG_DBVOLINFO_VOLID      (LOG_DBFIRST_VOLID - 5)
/* Volid of info log */
#define LOG_DBLOG_INFO_VOLID     (LOG_DBFIRST_VOLID - 4)
/* Volid of backup info log */
#define LOG_DBLOG_BKUPINFO_VOLID (LOG_DBFIRST_VOLID - 3)
/* Volid of active log */
#define LOG_DBLOG_ACTIVE_VOLID   (LOG_DBFIRST_VOLID - 2)
/* Volid of background archive logs */
#define LOG_DBLOG_BG_ARCHIVE_VOLID  (LOG_DBFIRST_VOLID - 21)
/* Volid of archive logs */
#define LOG_DBLOG_ARCHIVE_VOLID  (LOG_DBFIRST_VOLID - 20)
/* Volid of copies */
#define LOG_DBCOPY_VOLID         (LOG_DBFIRST_VOLID - 19)
/* Volid of double write buffer */
#define LOG_DBDWB_VOLID		 (LOG_DBFIRST_VOLID - 22)

/*
 * Specify up to int bits of permanent status indicators.
 * Restore in progress is the only one so far, the rest are reserved
 * for future use.  Note these must be specified and used as mask values
 * to test and set individual bits.
 */
enum LOG_PSTATUS
{
  LOG_PSTAT_CLEAR = 0x00,
  LOG_PSTAT_BACKUP_INPROGRESS = 0x01,	/* only one backup at a time */
  LOG_PSTAT_RESTORE_INPROGRESS = 0x02,	/* unset upon successful restore */
  LOG_PSTAT_HDRFLUSH_INPPROCESS = 0x04	/* need to flush log header */
};

typedef struct tran_query_exec_info TRAN_QUERY_EXEC_INFO;
struct tran_query_exec_info
{
  char *wait_for_tran_index_string;
  float query_time;
  float tran_time;
  char *query_stmt;
  char *sql_id;
  XASL_ID xasl_id;
};

#define LOG_SYSTEM_TRAN_INDEX 0	/* The recovery & vacuum worker system transaction index. */
#define LOG_SYSTEM_TRANID     0	/* The recovery & vacuum worker system transaction. */

#if !defined (NDEBUG) && !defined (WINDOWS)
extern int logtb_collect_local_clients (int **local_client_pids);
#endif /* !defined (NDEBUG) && !defined (WINDOWS) */

/************************************************************************/
/* End of part shared with client.                                      */
/************************************************************************/

#endif // _LOG_COMMON_IMPL_H_
