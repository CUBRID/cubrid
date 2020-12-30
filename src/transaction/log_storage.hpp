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

//
// Define storage of logging system
//

#ifndef _LOG_STORAGE_HPP_
#define _LOG_STORAGE_HPP_

#include "file_io.h"
#include "log_lsa.hpp"
#include "release_string.h"
#include "storage_common.h"
#include "system.h"
#include "transaction_global.hpp"
#include "tde.h"

#include <cstdint>

/* Definitions for flags in LOG_HDRPAGE */

/*
 * TDE_ALGORITHM to be applied to the log page
 * Set if any record in the page has to be tde-encrypted
 */
#define LOG_HDRPAGE_FLAG_ENCRYPTED_AES 0x1
#define LOG_HDRPAGE_FLAG_ENCRYPTED_ARIA 0x2

#define LOG_HDRPAGE_FLAG_ENCRYPTED_MASK 0x3

#define LOG_IS_PAGE_TDE_ENCRYPTED(log_page_p) \
  ((log_page_p)->hdr.flags & LOG_HDRPAGE_FLAG_ENCRYPTED_AES \
   || (log_page_p)->hdr.flags & LOG_HDRPAGE_FLAG_ENCRYPTED_ARIA)

const LOG_PAGEID LOGPB_HEADER_PAGE_ID = -9;     /* The first log page in the infinite log sequence. It is always kept
						 * on the active portion of the log. Log records are not stored on this
						 * page. This page is backed up in all archive logs */

const size_t LOGPB_IO_NPAGES = 4;
const size_t LOGPB_BUFFER_NPAGES_LOWER = 128;

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
  short flags;			/* flags */
  int checksum;			/* checksum - currently CRC32 is used to check log page consistency. */
};

/* WARNING:
 * Don't use sizeof(LOG_PAGE) or of any structure that contains it
 * Use macro LOG_PAGESIZE instead.
 * It is also bad idea to allocate a variable for LOG_PAGE on the stack.
 */

typedef struct log_page LOG_PAGE;
struct log_page
{
  /* The log page */
  LOG_HDRPAGE hdr;
  char area[1];
};

const size_t MAXLOGNAME = (30 - 12);

// vacuum blocks
using VACUUM_LOG_BLOCKID = std::int64_t;

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

/*
 * LOG HEADER INFORMATION
 */
typedef struct log_header LOG_HEADER;
struct log_header
{
  /* Log header information */
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
  int perm_status_obsolete;
  /* Here exists 4 bytes */
  LOG_HDR_BKUP_LEVEL_INFO bkinfo[FILEIO_BACKUP_UNDEFINED_LEVEL];
  /* backup specific info for future growth */

  int ha_server_state;
  int ha_file_status;
  LOG_LSA eof_lsa;

  LOG_LSA smallest_lsa_at_last_chkpt;

  // next fields track MVCC info relevant for vacuum
  LOG_LSA mvcc_op_log_lsa;	/* LSA of last MVCC operation log record */
  MVCCID oldest_visible_mvccid;	/* oldest visible MVCCID */
  MVCCID newest_block_mvccid;	/* newest MVCCID for current block */

  INT64 ha_promotion_time;
  INT64 db_restore_time;
  bool mark_will_del;
  bool does_block_need_vacuum;
  bool was_active_log_reset;

  log_header ()
    : magic {'0'}
    , dummy (0)
    , db_creation (0)
    , db_release {'0'}
    , db_compatibility (0.0f)
    , db_iopagesize (0)
    , db_logpagesize (0)
    , is_shutdown (false)
    , next_trid (LOG_SYSTEM_TRANID + 1)
    , mvcc_next_id (MVCCID_NULL)
    , avg_ntrans (0)
    , avg_nlocks (0)
    , npages (0)
    , db_charset (0)
    , was_copied (false)
    , dummy3 (0)
    , dummy4 (0)
    , fpageid (0)
    , append_lsa (NULL_LSA)
    , chkpt_lsa (NULL_LSA)
    , nxarv_pageid (0)
    , nxarv_phy_pageid (0)
    , nxarv_num (0)
    , last_arv_num_for_syscrashes (0)
    , last_deleted_arv_num (0)
    , bkup_level0_lsa (NULL_LSA)
    , bkup_level1_lsa (NULL_LSA)
    , bkup_level2_lsa (NULL_LSA)
    , prefix_name {'0'}
    , has_logging_been_skipped (false)
    , vacuum_last_blockid (0)
    , perm_status_obsolete (0)
    , bkinfo {{0, 0, 0, 0, 0}}
  , ha_server_state (0)
  , ha_file_status (0)
  , eof_lsa (NULL_LSA)
  , smallest_lsa_at_last_chkpt (NULL_LSA)
  , mvcc_op_log_lsa (NULL_LSA)
  , oldest_visible_mvccid (MVCCID_FIRST)
  , newest_block_mvccid (MVCCID_NULL)
  , ha_promotion_time (0)
  , db_restore_time (0)
  , mark_will_del (false)
  , does_block_need_vacuum (false)
  , was_active_log_reset (false)
  {
    //
  }
};



typedef struct log_arv_header LOG_ARV_HEADER;
struct log_arv_header
{
  /* Log archive header information */
  char magic[CUBRID_MAGIC_MAX_LENGTH];	/* Magic value for file/magic Unix utility */
  INT32 dummy;			/* for 8byte align */
  INT64 db_creation;		/* Database creation time. For safety reasons, this value is set on all volumes and the
				 * log. The value is generated by the log manager */
  TRANID next_trid;		/* Next Transaction identifier */
  DKNPAGES npages;		/* Number of pages in the archive log */
  LOG_PAGEID fpageid;		/* Logical pageid at physical location 1 in archive log */
  int arv_num;			/* The archive number */
  INT32 dummy2;			/* Dummy field for 8byte align */

  log_arv_header ()
    : magic {'0'}
    , dummy (0)
    , db_creation (0)
    , next_trid (0)
    , npages (0)
    , fpageid (0)
    , arv_num (0)
    , dummy2 (0)
  {
  }
};
#endif // !_LOG_STORAGE_HPP_
