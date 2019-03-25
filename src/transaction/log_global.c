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
 * log_global.c -
 */

#ident "$Id$"

#include "config.h"
#include "file_io.h"
#include "log_append.hpp"
#include "log_archives.hpp"
#include "log_impl.h"
#include "log_storage.hpp"
#include "log_writer.h"
#include "porting.h"
#include "storage_common.h"

#include <limits.h>
#include <stddef.h>
#include <stdlib.h>

/* Variables */
#if !defined(SERVER_MODE)
/* Index onto transaction table for current thread of execution (client) */
int log_Tran_index = -1;
#endif /* !SERVER_MODE */

LOG_GLOBAL log_Gl = {
  /* trantable */
  TRANTABLE_INITIALIZER,

  /* append */
  log_append_info (),

  /* prior info */
  LOG_PRIOR_LSA_INFO_INITIALIZER,

  /* hdr */
  log_header (),

  /* archive */
  log_archives (),

  /* run_nxchkpt_atpageid */
  NULL_PAGEID,
#if defined(SERVER_MODE)
  /* flushed_lsa_lower_bound */
  {NULL_PAGEID, NULL_OFFSET},
  /* chkpt_lsa_lock */
  PTHREAD_MUTEX_INITIALIZER,
#endif /* SERVER_MODE */
  /* chkpt_redo_lsa */
  {NULL_PAGEID, NULL_OFFSET},
  /* chkpt_every_npages */
  INT_MAX,
  /* rcv_phase */
  LOG_RECOVERY_ANALYSIS_PHASE,
  /* rcv_phase_lsa */
  {NULL_PAGEID, NULL_OFFSET},
#if defined(SERVER_MODE)
  /* backup_in_progress */
  false,
#else /* SERVER_MODE */
  /* final_restored_lsa */
  {NULL_PAGEID, NULL_OFFSET},
#endif /* SERVER_MODE */

  /* loghdr_pgptr */
  NULL,

  /* flush info */
  {0, 0, NULL
#if defined(SERVER_MODE)
   , PTHREAD_MUTEX_INITIALIZER
#endif /* SERVER_MODE */
   },

  /* group_commit_info */
  LOG_GROUP_COMMIT_INFO_INITIALIZER,

  /* log writer info */
  new logwr_info (),

  /* background archiving info */
  background_archiving_info (),

  /* mvcctable */
  MVCCTABLE_INITIALIZER,

  /* unique_stats_table */
  GLOBAL_UNIQUE_STATS_TABLE_INITIALIZER
};

/* Name of the database and logs */
char log_Path[PATH_MAX];
char log_Archive_path[PATH_MAX];
char log_Prefix[PATH_MAX];

const char *log_Db_fullname = NULL;
char log_Name_active[PATH_MAX];
char log_Name_info[PATH_MAX];
char log_Name_bkupinfo[PATH_MAX];
char log_Name_volinfo[PATH_MAX];
char log_Name_bg_archive[PATH_MAX];
char log_Name_removed_archive[PATH_MAX];

// *INDENT-OFF*
log_global::~log_global ()
{
  delete writer_info;
}
// *INDENT-ON*
