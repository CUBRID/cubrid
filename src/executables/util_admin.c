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
 * util_admin.c - a front end of admin utilities
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#ifdef HAVE_GETOPT_H
#include <getopt.h>
#else
#include "getopt.h"
#endif
#include "utility.h"
#include "error_code.h"
#include "util_support.h"
#include "file_io.h"

static UTIL_ARG_MAP ua_Create_Option_Map[] = {
  {OPTION_STRING_TABLE, {0}, {0}},
  {CREATE_PAGES_S, {ARG_INTEGER}, {-1}},
  {CREATE_COMMENT_S, {ARG_STRING}, {0}},
  {CREATE_FILE_PATH_S, {ARG_STRING}, {0}},
  {CREATE_LOG_PATH_S, {ARG_STRING}, {0}},
  {CREATE_LOB_PATH_S, {ARG_STRING}, {0}},
  {CREATE_SERVER_NAME_S, {ARG_STRING}, {0}},
  {CREATE_REPLACE_S, {ARG_BOOLEAN}, {0}},
  {CREATE_MORE_VOLUME_FILE_S, {ARG_STRING}, {0}},
  {CREATE_USER_DEFINITION_FILE_S, {ARG_STRING}, {0}},
  {CREATE_CSQL_INITIALIZATION_FILE_S, {ARG_STRING}, {0}},
  {CREATE_OUTPUT_FILE_S, {ARG_STRING}, {0}},
  {CREATE_VERBOSE_S, {ARG_BOOLEAN}, {0}},
  {CREATE_LOG_PAGE_COUNT_S, {ARG_INTEGER}, {-1}},
  {CREATE_PAGE_SIZE_S, {ARG_INTEGER}, {-1}},
  {CREATE_DB_PAGE_SIZE_S, {ARG_STRING}, {0}},
  {CREATE_LOG_PAGE_SIZE_S, {ARG_STRING}, {0}},
  {CREATE_DB_VOLUME_SIZE_S, {ARG_STRING}, {0}},
  {CREATE_LOG_VOLUME_SIZE_S, {ARG_STRING}, {0}},
  {0, {0}, {0}}
};

static GETOPT_LONG ua_Create_Option[] = {
  {CREATE_PAGES_L, 1, 0, CREATE_PAGES_S},
  {CREATE_COMMENT_L, 1, 0, CREATE_COMMENT_S},
  {CREATE_FILE_PATH_L, 1, 0, CREATE_FILE_PATH_S},
  {CREATE_LOG_PATH_L, 1, 0, CREATE_LOG_PATH_S},
  {CREATE_LOB_PATH_L, 1, 0, CREATE_LOB_PATH_S},
  {CREATE_SERVER_NAME_L, 1, 0, CREATE_SERVER_NAME_S},
  {CREATE_REPLACE_L, 0, 0, CREATE_REPLACE_S},
  {CREATE_MORE_VOLUME_FILE_L, 1, 0, CREATE_MORE_VOLUME_FILE_S},
  {CREATE_USER_DEFINITION_FILE_L, 1, 0, CREATE_USER_DEFINITION_FILE_S},
  {CREATE_CSQL_INITIALIZATION_FILE_L, 1, 0,
   CREATE_CSQL_INITIALIZATION_FILE_S},
  {CREATE_OUTPUT_FILE_L, 1, 0, CREATE_OUTPUT_FILE_S},
  {CREATE_VERBOSE_L, 0, 0, CREATE_VERBOSE_S},
  {CREATE_CHARSET_L, 1, 0, CREATE_CHARSET_S},
  {CREATE_LOG_PAGE_COUNT_L, 1, 0, CREATE_LOG_PAGE_COUNT_S},
  {CREATE_PAGE_SIZE_L, 1, 0, CREATE_PAGE_SIZE_S},
  {CREATE_DB_PAGE_SIZE_L, 1, 0, CREATE_DB_PAGE_SIZE_S},
  {CREATE_DB_VOLUME_SIZE_L, 1, 0, CREATE_DB_VOLUME_SIZE_S},
  {CREATE_LOG_PAGE_SIZE_L, 1, 0, CREATE_LOG_PAGE_SIZE_S},
  {CREATE_LOG_VOLUME_SIZE_L, 1, 0, CREATE_LOG_VOLUME_SIZE_S},
  {0, 0, 0, 0}
};

static UTIL_ARG_MAP ua_Rename_Option_Map[] = {
  {OPTION_STRING_TABLE, {0}, {0}},
  {RENAME_EXTENTED_VOLUME_PATH_S, {ARG_STRING}, {0}},
  {RENAME_CONTROL_FILE_S, {ARG_STRING}, {0}},
  {RENAME_DELETE_BACKUP_S, {ARG_BOOLEAN}, {0}},
  {0, {0}, {0}}
};

static GETOPT_LONG ua_Rename_Option[] = {
  {RENAME_EXTENTED_VOLUME_PATH_L, 1, 0, RENAME_EXTENTED_VOLUME_PATH_S},
  {RENAME_CONTROL_FILE_L, 1, 0, RENAME_CONTROL_FILE_S},
  {RENAME_DELETE_BACKUP_L, 0, 0, RENAME_DELETE_BACKUP_S},
  {0, 0, 0, 0}
};

static UTIL_ARG_MAP ua_Copy_Option_Map[] = {
  {OPTION_STRING_TABLE, {0}, {0}},
  {COPY_SERVER_NAME_S, {ARG_STRING}, {0}},
  {COPY_FILE_PATH_S, {ARG_STRING}, {0}},
  {COPY_LOG_PATH_S, {ARG_STRING}, {0}},
  {COPY_EXTENTED_VOLUME_PATH_S, {ARG_STRING}, {0}},
  {COPY_CONTROL_FILE_S, {ARG_STRING}, {0}},
  {COPY_REPLACE_S, {ARG_BOOLEAN}, {0}},
  {COPY_DELETE_SOURCE_S, {ARG_BOOLEAN}, {0}},
  {COPY_LOB_PATH_S, {ARG_STRING}, {0}},
  {COPY_COPY_LOB_PATH_S, {ARG_BOOLEAN}, {0}},
  {0, {0}, {0}}
};

static GETOPT_LONG ua_Copy_Option[] = {
  {COPY_SERVER_NAME_L, 1, 0, COPY_SERVER_NAME_S},
  {COPY_FILE_PATH_L, 1, 0, COPY_FILE_PATH_S},
  {COPY_LOG_PATH_L, 1, 0, COPY_LOG_PATH_S},
  {COPY_EXTENTED_VOLUME_PATH_L, 1, 0, COPY_EXTENTED_VOLUME_PATH_S},
  {COPY_CONTROL_FILE_L, 1, 0, COPY_CONTROL_FILE_S},
  {COPY_REPLACE_L, 0, 0, COPY_REPLACE_S},
  {COPY_DELETE_SOURCE_L, 0, 0, COPY_DELETE_SOURCE_S},
  {COPY_LOB_PATH_L, 1, 0, COPY_LOB_PATH_S},
  {COPY_COPY_LOB_PATH_L, 0, 0, COPY_COPY_LOB_PATH_S},
  {0, 0, 0, 0}
};

static UTIL_ARG_MAP ua_Delete_Option_Map[] = {
  {OPTION_STRING_TABLE, {0}, {0}},
  {DELETE_OUTPUT_FILE_S, {ARG_STRING}, {0}},
  {DELETE_DELETE_BACKUP_S, {ARG_BOOLEAN}, {0}},
  {0, {0}, {0}}
};

static GETOPT_LONG ua_Delete_Option[] = {
  {DELETE_OUTPUT_FILE_L, 1, 0, DELETE_OUTPUT_FILE_S},
  {DELETE_DELETE_BACKUP_L, 0, 0, DELETE_DELETE_BACKUP_S},
  {0, 0, 0, 0}
};

static UTIL_ARG_MAP ua_Backup_Option_Map[] = {
  {OPTION_STRING_TABLE, {0}, {0}},
  {BACKUP_DESTINATION_PATH_S, {ARG_STRING}, {0}},
  {BACKUP_OUTPUT_FILE_S, {ARG_STRING}, {0}},
  {BACKUP_REMOVE_ARCHIVE_S, {ARG_BOOLEAN}, {0}},
  {BACKUP_LEVEL_S, {ARG_INTEGER}, {FILEIO_BACKUP_FULL_LEVEL}},
  {BACKUP_SA_MODE_S, {ARG_BOOLEAN}, {0}},
  {BACKUP_CS_MODE_S, {ARG_BOOLEAN}, {0}},
  {BACKUP_NO_CHECK_S, {ARG_BOOLEAN}, {0}},
  {BACKUP_THREAD_COUNT_S, {ARG_INTEGER}, {FILEIO_BACKUP_NUM_THREADS_AUTO}},
  {BACKUP_COMPRESS_S, {ARG_BOOLEAN}, {0}},
  {BACKUP_EXCEPT_ACTIVE_LOG_S, {ARG_BOOLEAN}, {0}},
  {BACKUP_SLEEP_MSECS_S, {ARG_INTEGER}, {FILEIO_BACKUP_SLEEP_MSECS_AUTO}},
  {0, {0}, {0}}
};

static GETOPT_LONG ua_Backup_Option[] = {
  {BACKUP_DESTINATION_PATH_L, 1, 0, BACKUP_DESTINATION_PATH_S},
  {BACKUP_REMOVE_ARCHIVE_L, 0, 0, BACKUP_REMOVE_ARCHIVE_S},
  {BACKUP_LEVEL_L, 1, 0, BACKUP_LEVEL_S},
  {BACKUP_OUTPUT_FILE_L, 1, 0, BACKUP_OUTPUT_FILE_S},
  {BACKUP_SA_MODE_L, 0, 0, BACKUP_SA_MODE_S},
  {BACKUP_CS_MODE_L, 0, 0, BACKUP_CS_MODE_S},
  {BACKUP_NO_CHECK_L, 0, 0, BACKUP_NO_CHECK_S},
  {BACKUP_THREAD_COUNT_L, 1, 0, BACKUP_THREAD_COUNT_S},
  {BACKUP_COMPRESS_L, 0, 0, BACKUP_COMPRESS_S},
  {BACKUP_EXCEPT_ACTIVE_LOG_L, 0, 0, BACKUP_EXCEPT_ACTIVE_LOG_S},
  {BACKUP_SLEEP_MSECS_L, 1, 0, BACKUP_SLEEP_MSECS_S},
  {0, 0, 0, 0}
};

static UTIL_ARG_MAP ua_Restore_Option_Map[] = {
  {OPTION_STRING_TABLE, {0}, {0}},
  {RESTORE_UP_TO_DATE_S, {ARG_STRING}, {0}},
  {RESTORE_LIST_S, {ARG_BOOLEAN}, {0}},
  {RESTORE_BACKUP_FILE_PATH_S, {ARG_STRING}, {0}},
  {RESTORE_LEVEL_S, {ARG_INTEGER}, {0}},
  {RESTORE_PARTIAL_RECOVERY_S, {ARG_BOOLEAN}, {0}},
  {RESTORE_OUTPUT_FILE_S, {ARG_STRING}, {0}},
  {RESTORE_USE_DATABASE_LOCATION_PATH_S, {ARG_BOOLEAN}, {0}},
  {0, {0}, {0}}
};

static GETOPT_LONG ua_Restore_Option[] = {
  {RESTORE_UP_TO_DATE_L, 1, 0, RESTORE_UP_TO_DATE_S},
  {RESTORE_LIST_L, 0, 0, RESTORE_LIST_S},
  {RESTORE_BACKUP_FILE_PATH_L, 1, 0, RESTORE_BACKUP_FILE_PATH_S},
  {RESTORE_LEVEL_L, 1, 0, RESTORE_LEVEL_S},
  {RESTORE_PARTIAL_RECOVERY_L, 0, 0, RESTORE_PARTIAL_RECOVERY_S},
  {RESTORE_OUTPUT_FILE_L, 1, 0, RESTORE_OUTPUT_FILE_S},
  {RESTORE_USE_DATABASE_LOCATION_PATH_L, 0, 0,
   RESTORE_USE_DATABASE_LOCATION_PATH_S},
  {0, 0, 0, 0}
};

static UTIL_ARG_MAP ua_Addvol_Option_Map[] = {
  {OPTION_STRING_TABLE, {0}, {0}},
  {ADDVOL_VOLUME_NAME_S, {ARG_STRING}, {0}},
  {ADDVOL_VOLUME_SIZE_S, {ARG_STRING}, {0}},
  {ADDVOL_FILE_PATH_S, {ARG_STRING}, {0}},
  {ADDVOL_COMMENT_S, {ARG_STRING}, {0}},
#if defined(LINUX) || defined(AIX)
  {ADDVOL_PURPOSE_S, {ARG_STRING}, {.p = (void *) "generic"}},
#else
  {ADDVOL_PURPOSE_S, {ARG_STRING}, {(void *) "generic"}},
#endif
  {ADDVOL_SA_MODE_S, {ARG_BOOLEAN}, {0}},
  {ADDVOL_CS_MODE_S, {ARG_BOOLEAN}, {0}},
  {ADDVOL_MAX_WRITESIZE_IN_SEC_S, {ARG_STRING}, {0}},
  {0, {0}, {0}}
};

static GETOPT_LONG ua_Addvol_Option[] = {
  {ADDVOL_VOLUME_NAME_L, 1, 0, ADDVOL_VOLUME_NAME_S},
  {ADDVOL_VOLUME_SIZE_L, 1, 0, ADDVOL_VOLUME_SIZE_S},
  {ADDVOL_FILE_PATH_L, 1, 0, ADDVOL_FILE_PATH_S},
  {ADDVOL_COMMENT_L, 1, 0, ADDVOL_COMMENT_S},
  {ADDVOL_PURPOSE_L, 1, 0, ADDVOL_PURPOSE_S},
  {ADDVOL_SA_MODE_L, 0, 0, ADDVOL_SA_MODE_S},
  {ADDVOL_CS_MODE_L, 0, 0, ADDVOL_CS_MODE_S},
  {ADDVOL_MAX_WRITESIZE_IN_SEC_L, 1, 0, ADDVOL_MAX_WRITESIZE_IN_SEC_S},
  {0, 0, 0, 0}
};

static UTIL_ARG_MAP ua_Space_Option_Map[] = {
  {OPTION_STRING_TABLE, {0}, {0}},
  {SPACE_OUTPUT_FILE_S, {ARG_STRING}, {0}},
  {SPACE_SA_MODE_S, {ARG_BOOLEAN}, {0}},
  {SPACE_CS_MODE_S, {ARG_BOOLEAN}, {0}},
#if defined(LINUX) || defined(AIX)
  {SPACE_SIZE_UNIT_S, {ARG_STRING}, {.p = (void *) "h"}},
#else
  {SPACE_SIZE_UNIT_S, {ARG_STRING}, {(void *) "h"}},
#endif
  {SPACE_SUMMARIZE_S, {ARG_BOOLEAN}, {0}},
  {SPACE_PURPOSE_S, {ARG_BOOLEAN}, {0}},
  {0, {0}, {0}}
};

static GETOPT_LONG ua_Space_Option[] = {
  {SPACE_OUTPUT_FILE_L, 1, 0, SPACE_OUTPUT_FILE_S},
  {SPACE_SA_MODE_L, 0, 0, SPACE_SA_MODE_S},
  {SPACE_CS_MODE_L, 0, 0, SPACE_CS_MODE_S},
  {SPACE_SIZE_UNIT_L, 1, 0, SPACE_SIZE_UNIT_S},
  {SPACE_SUMMARIZE_L, 0, 0, SPACE_SUMMARIZE_S},
  {SPACE_PURPOSE_L, 0, 0, SPACE_PURPOSE_S},
  {0, 0, 0, 0}
};

static UTIL_ARG_MAP ua_Lock_Option_Map[] = {
  {OPTION_STRING_TABLE, {0}, {0}},
  {LOCK_OUTPUT_FILE_S, {ARG_STRING}, {0}},
  {0, {0}, {0}}
};

static GETOPT_LONG ua_Lock_Option[] = {
  {LOCK_OUTPUT_FILE_L, 1, 0, LOCK_OUTPUT_FILE_S},
  {0, 0, 0, 0}
};

static UTIL_ARG_MAP ua_Acl_Option_Map[] = {
  {OPTION_STRING_TABLE, {0}, {0}},
  {ACLDB_RELOAD_S, {ARG_BOOLEAN}, {0}},
  {0, {0}, {0}}
};

static GETOPT_LONG ua_Acl_Option[] = {
  {ACLDB_RELOAD_L, 0, 0, ACLDB_RELOAD_S},
  {0, 0, 0, 0}
};

static UTIL_ARG_MAP ua_Optimize_Option_Map[] = {
  {OPTION_STRING_TABLE, {0}, {0}},
  {OPTIMIZE_CLASS_NAME_S, {ARG_STRING}, {0}},
  {0, {0}, {0}}
};

static GETOPT_LONG ua_Optimize_Option[] = {
  {OPTIMIZE_CLASS_NAME_L, 1, 0, OPTIMIZE_CLASS_NAME_S},
  {0, 0, 0, 0}
};

static UTIL_ARG_MAP ua_Install_Option_Map[] = {
  {OPTION_STRING_TABLE, {0}, {0}},
  {INSTALL_SERVER_NAME_S, {ARG_STRING}, {0}},
  {INSTALL_FILE_PATH_S, {ARG_STRING}, {0}},
  {INSTALL_LOG_PATH_S, {ARG_STRING}, {0}},
  {0, {0}, {0}}
};

static GETOPT_LONG ua_Install_Option[] = {
  {INSTALL_SERVER_NAME_L, 1, 0, INSTALL_SERVER_NAME_S},
  {INSTALL_FILE_PATH_L, 1, 0, INSTALL_FILE_PATH_S},
  {INSTALL_LOG_PATH_L, 1, 0, INSTALL_LOG_PATH_S},
  {0, 0, 0, 0}
};

static UTIL_ARG_MAP ua_Diag_Option_Map[] = {
  {OPTION_STRING_TABLE, {0}, {0}},
  {DIAG_DUMP_TYPE_S, {ARG_INTEGER}, {-1}},
  {DIAG_DUMP_RECORDS_S, {ARG_BOOLEAN}, {0}},
  {0, {0}, {0}}
};

static GETOPT_LONG ua_Diag_Option[] = {
  {DIAG_DUMP_TYPE_L, 1, 0, DIAG_DUMP_TYPE_S},
  {DIAG_DUMP_RECORDS_L, 0, 0, DIAG_DUMP_RECORDS_S},
  {0, 0, 0, 0}
};

static UTIL_ARG_MAP ua_Patch_Option_Map[] = {
  {OPTION_STRING_TABLE, {0}, {0}},
  {PATCH_RECREATE_LOG_S, {ARG_BOOLEAN}, {0}},
  {0, {0}, {0}}
};

static GETOPT_LONG ua_Patch_Option[] = {
  {PATCH_RECREATE_LOG_L, 0, 0, PATCH_RECREATE_LOG_S},
  {0, 0, 0, 0}
};

static UTIL_ARG_MAP ua_Check_Option_Map[] = {
  {OPTION_STRING_TABLE, {0}, {0}},
  {CHECK_SA_MODE_S, {ARG_BOOLEAN}, {0}},
  {CHECK_CS_MODE_S, {ARG_BOOLEAN}, {0}},
  {CHECK_INDEXNAME_S, {ARG_STRING}, {0}},
  {CHECK_REPAIR_S, {ARG_BOOLEAN}, {0}},
  {CHECK_INPUT_FILE_S, {ARG_STRING}, {0}},
  {CHECK_CHECK_PREV_LINK_S, {ARG_BOOLEAN}, {0}},
  {CHECK_REPAIR_PREV_LINK_S, {ARG_BOOLEAN}, {0}},
  {CHECK_FILE_TRACKER_S, {ARG_BOOLEAN}, {0}},
  {CHECK_HEAP_ALLHEAPS_S, {ARG_BOOLEAN}, {0}},
  {CHECK_CAT_CONSISTENCY_S, {ARG_BOOLEAN}, {0}},
  {CHECK_BTREE_ALL_BTREES_S, {ARG_BOOLEAN}, {0}},
  {CHECK_LC_CLASSNAMES_S, {ARG_BOOLEAN}, {0}},
  {CHECK_LC_ALLENTRIES_OF_ALLBTREES_S, {ARG_BOOLEAN}, {0}},
  {0, {0}, {0}}
};

static GETOPT_LONG ua_Check_Option[] = {
  {CHECK_SA_MODE_L, 0, 0, CHECK_SA_MODE_S},
  {CHECK_CS_MODE_L, 0, 0, CHECK_CS_MODE_S},
  {CHECK_INDEXNAME_L, 1, 0, CHECK_INDEXNAME_S},
  {CHECK_REPAIR_L, 0, 0, CHECK_REPAIR_S},
  {CHECK_INPUT_FILE_L, 1, 0, CHECK_INPUT_FILE_S},
  {CHECK_CHECK_PREV_LINK_L, 0, 0, CHECK_CHECK_PREV_LINK_S},
  {CHECK_REPAIR_PREV_LINK_L, 0, 0, CHECK_REPAIR_PREV_LINK_S},
  {CHECK_FILE_TRACKER_L, 0, 0, CHECK_FILE_TRACKER_S},
  {CHECK_HEAP_ALLHEAPS_L, 0, 0, CHECK_HEAP_ALLHEAPS_S},
  {CHECK_CAT_CONSISTENCY_L, 0, 0, CHECK_CAT_CONSISTENCY_S},
  {CHECK_BTREE_ALL_BTREES_L, 0, 0, CHECK_BTREE_ALL_BTREES_S},
  {CHECK_LC_CLASSNAMES_L, 0, 0, CHECK_LC_CLASSNAMES_S},
  {CHECK_LC_ALLENTRIES_OF_ALLBTREES_L, 0, 0,
   CHECK_LC_ALLENTRIES_OF_ALLBTREES_S},
  {0, 0, 0, 0}
};

/* alterdbhost option list */
#define ALTERDBHOST_HOST_S                'h'

static UTIL_ARG_MAP ua_Alterdbhost_Option_Map[] = {
  {OPTION_STRING_TABLE, {0}, {0}},
  {ALTERDBHOST_HOST_S, {ARG_STRING}, {0}},
  {0, {0}, {0}}
};

static GETOPT_LONG ua_Alterdbhost_Option[] = {
  {ALTERDBHOST_HOST_L, 1, 0, ALTERDBHOST_HOST_S},
  {0, 0, 0, 0}
};

static UTIL_ARG_MAP ua_Plandump_Option_Map[] = {
  {OPTION_STRING_TABLE, {0}, {0}},
  {PLANDUMP_DROP_S, {ARG_BOOLEAN}, {0}},
  {PLANDUMP_OUTPUT_FILE_S, {ARG_STRING}, {0}},
  {0, {0}, {0}}
};

static GETOPT_LONG ua_Plandump_Option[] = {
  {PLANDUMP_DROP_L, 0, 0, PLANDUMP_DROP_S},
  {PLANDUMP_OUTPUT_FILE_L, 1, 0, PLANDUMP_OUTPUT_FILE_S},
  {0, 0, 0, 0}
};

static UTIL_ARG_MAP ua_Killtran_Option_Map[] = {
  {OPTION_STRING_TABLE, {0}, {0}},
  {KILLTRAN_KILL_TRANSACTION_INDEX_S, {ARG_STRING}, {0}},
#if defined(LINUX) || defined(AIX)
  {KILLTRAN_KILL_USER_NAME_S, {ARG_STRING}, {.p = (void *) ""}},
  {KILLTRAN_KILL_HOST_NAME_S, {ARG_STRING}, {.p = (void *) ""}},
  {KILLTRAN_KILL_PROGRAM_NAME_S, {ARG_STRING}, {.p = (void *) ""}},
  {KILLTRAN_KILL_SQL_ID_S, {ARG_STRING}, {0}},
  {KILLTRAN_DBA_PASSWORD_S, {ARG_STRING}, {.p = (void *) ""}},
#else
  {KILLTRAN_KILL_USER_NAME_S, {ARG_STRING}, {(void *) ""}},
  {KILLTRAN_KILL_HOST_NAME_S, {ARG_STRING}, {(void *) ""}},
  {KILLTRAN_KILL_PROGRAM_NAME_S, {ARG_STRING}, {(void *) ""}},
  {KILLTRAN_KILL_SQL_ID_S, {ARG_STRING}, {0}},
  {KILLTRAN_DBA_PASSWORD_S, {ARG_STRING}, {(void *) ""}},
#endif
  {KILLTRAN_DISPLAY_INFORMATION_S, {ARG_BOOLEAN}, {0}},
  {KILLTRAN_DISPLAY_QUERY_INFO_S, {ARG_BOOLEAN}, {0}},
  {KILLTRAN_FORCE_S, {ARG_BOOLEAN}, {0}},
  {0, {0}, {0}}
};

static GETOPT_LONG ua_Killtran_Option[] = {
  {KILLTRAN_KILL_TRANSACTION_INDEX_L, 1, 0,
   KILLTRAN_KILL_TRANSACTION_INDEX_S},
  {KILLTRAN_KILL_USER_NAME_L, 1, 0, KILLTRAN_KILL_USER_NAME_S},
  {KILLTRAN_KILL_HOST_NAME_L, 1, 0, KILLTRAN_KILL_HOST_NAME_S},
  {KILLTRAN_KILL_PROGRAM_NAME_L, 1, 0, KILLTRAN_KILL_PROGRAM_NAME_S},
  {KILLTRAN_KILL_SQL_ID_L, 1, 0, KILLTRAN_KILL_SQL_ID_S},
  {KILLTRAN_DBA_PASSWORD_L, 1, 0, KILLTRAN_DBA_PASSWORD_S},
  {KILLTRAN_DISPLAY_INFORMATION_L, 0, 0, KILLTRAN_DISPLAY_INFORMATION_S},
  {KILLTRAN_DISPLAY_QUERY_INFO_L, 0, 0, KILLTRAN_DISPLAY_QUERY_INFO_S},
  {KILLTRAN_FORCE_L, 0, 0, KILLTRAN_FORCE_S},
  {0, 0, 0, 0}
};

static UTIL_ARG_MAP ua_Tranlist_Option_Map[] = {
  {OPTION_STRING_TABLE, {0}, {0}},
  {TRANLIST_USER_S, {ARG_STRING}, {0}},
  {TRANLIST_PASSWORD_S, {ARG_STRING}, {0}},
  {TRANLIST_SUMMARY_S, {ARG_BOOLEAN}, {0}},
  {TRANLIST_SORT_KEY_S, {ARG_INTEGER}, {0}},
  {TRANLIST_REVERSE_S, {ARG_BOOLEAN}, {0}},
  {0, {0}, {0}}
};

static GETOPT_LONG ua_Tranlist_Option[] = {
  {TRANLIST_USER_L, 1, 0, TRANLIST_USER_S},
  {TRANLIST_PASSWORD_L, 1, 0, TRANLIST_PASSWORD_S},
  {TRANLIST_SUMMARY_L, 0, 0, TRANLIST_SUMMARY_S},
  {TRANLIST_SORT_KEY_L, 1, 0, TRANLIST_SORT_KEY_S},
  {TRANLIST_REVERSE_L, 0, 0, TRANLIST_REVERSE_S},
  {0, 0, 0, 0}
};

static UTIL_ARG_MAP ua_Prefetchlogdb_Option_Map[] = {
  {OPTION_STRING_TABLE, {ARG_INTEGER}, {0}},
  {PREFETCH_LOG_PATH_S, {ARG_STRING}, {0}},
  {0, {0}, {0}}
};

static GETOPT_LONG ua_Prefetchlogdb_Option[] = {
  {PREFETCH_LOG_PATH_L, 1, 0, PREFETCH_LOG_PATH_S},
  {0, 0, 0, 0}
};

static UTIL_ARG_MAP ua_Load_Option_Map[] = {
  {OPTION_STRING_TABLE, {0}, {0}},
  {LOAD_USER_S, {ARG_STRING}, {0}},
  {LOAD_PASSWORD_S, {ARG_STRING}, {0}},
  {LOAD_CHECK_ONLY_S, {ARG_BOOLEAN}, {0}},
  {LOAD_LOAD_ONLY_S, {ARG_BOOLEAN}, {0}},
  {LOAD_ESTIMATED_SIZE_S, {ARG_INTEGER}, {0}},
  {LOAD_VERBOSE_S, {ARG_BOOLEAN}, {0}},
  {LOAD_NO_STATISTICS_S, {ARG_BOOLEAN}, {0}},
  {LOAD_PERIODIC_COMMIT_S, {ARG_INTEGER}, {0}},
  {LOAD_NO_OID_S, {ARG_BOOLEAN}, {0}},
  {LOAD_SCHEMA_FILE_S, {ARG_STRING}, {0}},
  {LOAD_INDEX_FILE_S, {ARG_STRING}, {0}},
  {LOAD_IGNORE_LOGGING_S, {ARG_BOOLEAN}, {0}},
  {LOAD_DATA_FILE_S, {ARG_STRING}, {0}},
  {LOAD_ERROR_CONTROL_FILE_S, {ARG_STRING}, {0}},
  {LOAD_IGNORE_CLASS_S, {ARG_STRING}, {0}},
  {LOAD_CS_MODE_S, {ARG_BOOLEAN}, {0}},
  {LOAD_SA_MODE_S, {ARG_BOOLEAN}, {1}},
  {LOAD_TABLE_NAME_S, {ARG_STRING}, {0}},
  {LOAD_COMPARE_STORAGE_ORDER_S, {ARG_BOOLEAN}, {0}},
  {0, {0}, {0}}
};

static GETOPT_LONG ua_Load_Option[] = {
  {LOAD_USER_L, 1, 0, LOAD_USER_S},
  {LOAD_PASSWORD_L, 1, 0, LOAD_PASSWORD_S},
  {LOAD_CHECK_ONLY_L, 0, 0, LOAD_CHECK_ONLY_S},
  {LOAD_LOAD_ONLY_L, 0, 0, LOAD_LOAD_ONLY_S},
  {LOAD_ESTIMATED_SIZE_L, 1, 0, LOAD_ESTIMATED_SIZE_S},
  {LOAD_VERBOSE_L, 0, 0, LOAD_VERBOSE_S},
  {LOAD_NO_STATISTICS_L, 0, 0, LOAD_NO_STATISTICS_S},
  {LOAD_PERIODIC_COMMIT_L, 1, 0, LOAD_PERIODIC_COMMIT_S},
  {LOAD_NO_OID_L, 0, 0, LOAD_NO_OID_S},
  {LOAD_SCHEMA_FILE_L, 1, 0, LOAD_SCHEMA_FILE_S},
  {LOAD_INDEX_FILE_L, 1, 0, LOAD_INDEX_FILE_S},
  {LOAD_IGNORE_LOGGING_L, 0, 0, LOAD_IGNORE_LOGGING_S},
  {LOAD_DATA_FILE_L, 1, 0, LOAD_DATA_FILE_S},
  {LOAD_ERROR_CONTROL_FILE_L, 1, 0, LOAD_ERROR_CONTROL_FILE_S},
  {LOAD_IGNORE_CLASS_L, 1, 0, LOAD_IGNORE_CLASS_S},
  {LOAD_CS_MODE_L, 0, 0, LOAD_CS_MODE_S},
  {LOAD_SA_MODE_L, 0, 0, LOAD_SA_MODE_S},
  {LOAD_TABLE_NAME_L, 1, 0, LOAD_TABLE_NAME_S},
  {LOAD_COMPARE_STORAGE_ORDER_L, 0, 0, LOAD_COMPARE_STORAGE_ORDER_S},
  {0, 0, 0, 0}
};

static UTIL_ARG_MAP ua_Unload_Option_Map[] = {
  {OPTION_STRING_TABLE, {0}, {0}},
  {UNLOAD_INPUT_CLASS_FILE_S, {ARG_STRING}, {0}},
  {UNLOAD_INCLUDE_REFERENCE_S, {ARG_BOOLEAN}, {0}},
  {UNLOAD_INPUT_CLASS_ONLY_S, {ARG_BOOLEAN}, {0}},
  {UNLOAD_LO_COUNT_S, {ARG_INTEGER}, {0}},
  {UNLOAD_ESTIMATED_SIZE_S, {ARG_INTEGER}, {0}},
  {UNLOAD_CACHED_PAGES_S, {ARG_INTEGER}, {100}},
  {UNLOAD_OUTPUT_PATH_S, {ARG_STRING}, {0}},
  {UNLOAD_SCHEMA_ONLY_S, {ARG_BOOLEAN}, {0}},
  {UNLOAD_DATA_ONLY_S, {ARG_BOOLEAN}, {0}},
  {UNLOAD_OUTPUT_PREFIX_S, {ARG_STRING}, {0}},
  {UNLOAD_HASH_FILE_S, {ARG_STRING}, {0}},
  {UNLOAD_VERBOSE_S, {ARG_BOOLEAN}, {0}},
  {UNLOAD_USE_DELIMITER_S, {ARG_BOOLEAN}, {0}},
  {UNLOAD_SA_MODE_S, {ARG_BOOLEAN}, {0}},
  {UNLOAD_CS_MODE_S, {ARG_BOOLEAN}, {0}},
  {UNLOAD_DATAFILE_PER_CLASS_S, {ARG_BOOLEAN}, {0}},
  {UNLOAD_USER_S, {ARG_STRING}, {0}},
  {UNLOAD_PASSWORD_S, {ARG_STRING}, {0}},
  {UNLOAD_KEEP_STORAGE_ORDER_S, {ARG_BOOLEAN}, {0}},
  {0, {0}, {0}}
};

static GETOPT_LONG ua_Unload_Option[] = {
  {UNLOAD_INPUT_CLASS_FILE_L, 1, 0, UNLOAD_INPUT_CLASS_FILE_S},
  {UNLOAD_INCLUDE_REFERENCE_L, 0, 0, UNLOAD_INCLUDE_REFERENCE_S},
  {UNLOAD_INPUT_CLASS_ONLY_L, 0, 0, UNLOAD_INPUT_CLASS_ONLY_S},
  {UNLOAD_LO_COUNT_L, 1, 0, UNLOAD_LO_COUNT_S},
  {UNLOAD_ESTIMATED_SIZE_L, 1, 0, UNLOAD_ESTIMATED_SIZE_S},
  {UNLOAD_CACHED_PAGES_L, 1, 0, UNLOAD_CACHED_PAGES_S},
  {UNLOAD_OUTPUT_PATH_L, 1, 0, UNLOAD_OUTPUT_PATH_S},
  {UNLOAD_SCHEMA_ONLY_L, 0, 0, UNLOAD_SCHEMA_ONLY_S},
  {UNLOAD_DATA_ONLY_L, 0, 0, UNLOAD_DATA_ONLY_S},
  {UNLOAD_OUTPUT_PREFIX_L, 1, 0, UNLOAD_OUTPUT_PREFIX_S},
  {UNLOAD_HASH_FILE_L, 1, 0, UNLOAD_HASH_FILE_S},
  {UNLOAD_VERBOSE_L, 0, 0, UNLOAD_VERBOSE_S},
  {UNLOAD_USE_DELIMITER_L, 0, 0, UNLOAD_USE_DELIMITER_S},
  {UNLOAD_DATAFILE_PER_CLASS_L, 0, 0, UNLOAD_DATAFILE_PER_CLASS_S},
  {UNLOAD_SA_MODE_L, 0, 0, UNLOAD_SA_MODE_S},
  {UNLOAD_CS_MODE_L, 0, 0, UNLOAD_CS_MODE_S},
  {UNLOAD_USER_L, 1, 0, LOAD_USER_S},
  {UNLOAD_PASSWORD_L, 1, 0, LOAD_PASSWORD_S},
  {UNLOAD_KEEP_STORAGE_ORDER_L, 0, 0, UNLOAD_KEEP_STORAGE_ORDER_S},
  {0, 0, 0, 0}
};

static UTIL_ARG_MAP ua_Compact_Option_Map[] = {
  {OPTION_STRING_TABLE, {0}, {0}},
  {COMPACT_VERBOSE_S, {ARG_BOOLEAN}, {0}},
  {COMPACT_INPUT_CLASS_FILE_S, {ARG_STRING}, {0}},
  {COMPACT_SA_MODE_S, {ARG_BOOLEAN}, {0}},
  {COMPACT_CS_MODE_S, {ARG_BOOLEAN}, {0}},
  {COMPACT_PAGES_COMMITED_ONCE_S, {ARG_INTEGER}, {10}},
  {COMPACT_DELETE_OLD_REPR_S, {ARG_BOOLEAN}, {0}},
  {COMPACT_INSTANCE_LOCK_TIMEOUT_S, {ARG_INTEGER}, {2}},
  {COMPACT_CLASS_LOCK_TIMEOUT_S, {ARG_INTEGER}, {10}},
  {0, {0}, {0}}
};

static GETOPT_LONG ua_Compact_Option[] = {
  {COMPACT_VERBOSE_L, 0, 0, COMPACT_VERBOSE_S},
  {COMPACT_INPUT_CLASS_FILE_L, 1, 0, COMPACT_INPUT_CLASS_FILE_S},
  {COMPACT_SA_MODE_L, 0, 0, COMPACT_SA_MODE_S},
  {COMPACT_CS_MODE_L, 0, 0, COMPACT_CS_MODE_S},
  {COMPACT_PAGES_COMMITED_ONCE_L, 1, 0, COMPACT_PAGES_COMMITED_ONCE_S},
  {COMPACT_DELETE_OLD_REPR_L, 0, 0, COMPACT_DELETE_OLD_REPR_S},
  {COMPACT_INSTANCE_LOCK_TIMEOUT_L, 1, 0, COMPACT_INSTANCE_LOCK_TIMEOUT_S},
  {COMPACT_CLASS_LOCK_TIMEOUT_L, 1, 0, COMPACT_CLASS_LOCK_TIMEOUT_S},
  {0, 0, 0, 0}
};

static UTIL_ARG_MAP ua_Paramdump_Option_Map[] = {
  {OPTION_STRING_TABLE, {ARG_INTEGER}, {0}},
  {PARAMDUMP_OUTPUT_FILE_S, {ARG_STRING}, {0}},
  {PARAMDUMP_BOTH_S, {ARG_BOOLEAN}, {0}},
  {PARAMDUMP_SA_MODE_S, {ARG_BOOLEAN}, {0}},
  {PARAMDUMP_CS_MODE_S, {ARG_BOOLEAN}, {0}},
  {0, {0}, {0}}
};

static GETOPT_LONG ua_Paramdump_Option[] = {
  {PARAMDUMP_OUTPUT_FILE_L, 1, 0, PARAMDUMP_OUTPUT_FILE_S},
  {PARAMDUMP_BOTH_L, 0, 0, PARAMDUMP_BOTH_S},
  {PARAMDUMP_SA_MODE_L, 0, 0, PARAMDUMP_SA_MODE_S},
  {PARAMDUMP_CS_MODE_L, 0, 0, PARAMDUMP_CS_MODE_S},
  {0, 0, 0, 0}
};

static UTIL_ARG_MAP ua_Statdump_Option_Map[] = {
  {OPTION_STRING_TABLE, {ARG_INTEGER}, {0}},
  {STATDUMP_OUTPUT_FILE_S, {ARG_STRING}, {0}},
  {STATDUMP_INTERVAL_S, {ARG_INTEGER}, {0}},
  {STATDUMP_CUMULATIVE_S, {ARG_BOOLEAN}, {0}},
  {STATDUMP_SUBSTR_S, {ARG_STRING}, {0}},
  {0, {0}, {0}}
};

static GETOPT_LONG ua_Statdump_Option[] = {
  {STATDUMP_OUTPUT_FILE_L, 1, 0, STATDUMP_OUTPUT_FILE_S},
  {STATDUMP_INTERVAL_L, 1, 0, STATDUMP_INTERVAL_S},
  {STATDUMP_CUMULATIVE_L, 0, 0, STATDUMP_CUMULATIVE_S},
  {STATDUMP_SUBSTR_L, 1, 0, STATDUMP_SUBSTR_S},
  {0, 0, 0, 0}
};

static UTIL_ARG_MAP ua_Changemode_Option_Map[] = {
  {OPTION_STRING_TABLE, {ARG_INTEGER}, {0}},
  {CHANGEMODE_MODE_S, {ARG_STRING}, {0}},
  {CHANGEMODE_FORCE_S, {ARG_BOOLEAN}, {0}},
  {CHANGEMODE_TIMEOUT_S, {ARG_INTEGER}, {-1}},
  {0, {0}, {0}}
};

static GETOPT_LONG ua_Changemode_Option[] = {
  {CHANGEMODE_MODE_L, 1, 0, CHANGEMODE_MODE_S},
  {CHANGEMODE_FORCE_L, 0, 0, CHANGEMODE_FORCE_S},
  {CHANGEMODE_TIMEOUT_L, 1, 0, CHANGEMODE_TIMEOUT_S},
  {0, 0, 0, 0}
};

static UTIL_ARG_MAP ua_Copylog_Option_Map[] = {
  {OPTION_STRING_TABLE, {ARG_INTEGER}, {0}},
  {COPYLOG_LOG_PATH_S, {ARG_STRING}, {0}},
  {COPYLOG_MODE_S, {ARG_STRING}, {0}},
#if defined(LINUX) || defined(AIX)
  {COPYLOG_START_PAGEID_S, {ARG_BIGINT}, {.l = (-2L)}},
#else
  {COPYLOG_START_PAGEID_S, {ARG_BIGINT}, {(INT64) (-2L)}},
#endif
  {0, {0}, {0}}
};

static GETOPT_LONG ua_Copylog_Option[] = {
  {COPYLOG_LOG_PATH_L, 1, 0, COPYLOG_LOG_PATH_S},
  {COPYLOG_MODE_L, 1, 0, COPYLOG_MODE_S},
  {COPYLOG_START_PAGEID_L, 1, 0, COPYLOG_START_PAGEID_S},
  {0, 0, 0, 0}
};

static UTIL_ARG_MAP ua_Applylog_Option_Map[] = {
  {OPTION_STRING_TABLE, {ARG_INTEGER}, {0}},
  {APPLYLOG_LOG_PATH_S, {ARG_STRING}, {0}},
  {APPLYLOG_MAX_MEM_SIZE_S, {ARG_INTEGER}, {0}},
  {0, {0}, {0}}
};

static GETOPT_LONG ua_Applylog_Option[] = {
  {APPLYLOG_LOG_PATH_L, 1, 0, APPLYLOG_LOG_PATH_S},
  {APPLYLOG_MAX_MEM_SIZE_L, 1, 0, APPLYLOG_MAX_MEM_SIZE_S},
  {0, 0, 0, 0}
};

static UTIL_ARG_MAP ua_ApplyInfo_Option_Map[] = {
  {OPTION_STRING_TABLE, {ARG_INTEGER}, {0}},
  {APPLYINFO_COPIED_LOG_PATH_S, {ARG_STRING}, {0}},
#if defined(LINUX) || defined(AIX)
  {APPLYINFO_PAGE_S, {ARG_BIGINT}, {.l = (-1L)}},
#else
  {APPLYINFO_PAGE_S, {ARG_BIGINT}, {(INT64) (-1L)}},
#endif
  {APPLYINFO_REMOTE_NAME_S, {ARG_STRING}, {0}},
  {APPLYINFO_APPLIED_INFO_S, {ARG_BOOLEAN}, {0}},
  {APPLYINFO_VERBOSE_S, {ARG_BOOLEAN}, {0}},
  {APPLYINFO_INTERVAL_S, {ARG_INTEGER}, {0}},
  {0, {0}, {0}}
};

static GETOPT_LONG ua_ApplyInfo_Option[] = {
  {APPLYINFO_COPIED_LOG_PATH_L, 1, 0, APPLYINFO_COPIED_LOG_PATH_S},
  {APPLYINFO_PAGE_L, 1, 0, APPLYINFO_PAGE_S},
  {APPLYINFO_REMOTE_NAME_L, 1, 0, APPLYINFO_REMOTE_NAME_S},
  {APPLYINFO_APPLIED_INFO_L, 0, 0, APPLYINFO_APPLIED_INFO_S},
  {APPLYINFO_VERBOSE_L, 0, 0, APPLYINFO_VERBOSE_S},
  {APPLYINFO_INTERVAL_L, 1, 0, APPLYINFO_INTERVAL_S},
  {0, 0, 0, 0}
};

static UTIL_ARG_MAP ua_GenLocale_Map[] = {
  {OPTION_STRING_TABLE, {0}, {0}},
  {GENLOCALE_INPUT_PATH_S, {ARG_STRING}, {0}},
  {GENLOCALE_VERBOSE_S, {ARG_BOOLEAN}, {0}},
  {0, {0}, {0}}
};

static GETOPT_LONG ua_GenLocale_Option[] = {
  {GENLOCALE_INPUT_PATH_L, 1, 0, GENLOCALE_INPUT_PATH_S},
  {GENLOCALE_VERBOSE_L, 0, 0, APPLYINFO_VERBOSE_S},
  {0, 0, 0, 0}
};


static UTIL_ARG_MAP ua_DumpLocale_Map[] = {
  {OPTION_STRING_TABLE, {0}, {0}},
  {DUMPLOCALE_INPUT_PATH_S, {ARG_STRING}, {0}},
  {DUMPLOCALE_CALENDAR_S, {ARG_BOOLEAN}, {0}},
  {DUMPLOCALE_NUMBERING_S, {ARG_BOOLEAN}, {0}},
  {DUMPLOCALE_ALPHABET_S, {ARG_STRING}, {0}},
  {DUMPLOCALE_IDENTIFIER_ALPHABET_S, {ARG_STRING}, {0}},
  {DUMPLOCALE_COLLATION_S, {ARG_BOOLEAN}, {0}},
  {DUMPLOCALE_WEIGHT_ORDER_S, {ARG_BOOLEAN}, {0}},
  {DUMPLOCALE_START_VALUE_S, {ARG_INTEGER}, {0}},
  {DUMPLOCALE_END_VALUE_S, {ARG_INTEGER}, {0}},
  {DUMPLOCALE_NORMALIZATION_S, {ARG_BOOLEAN}, {0}},
  {DUMPLOCALE_CONSOLE_CONV_S, {ARG_BOOLEAN}, {0}},
  {0, {0}, {0}}
};

static GETOPT_LONG ua_DumpLocale_Option[] = {
  {DUMPLOCALE_INPUT_PATH_L, 1, 0, DUMPLOCALE_INPUT_PATH_S},
  {DUMPLOCALE_CALENDAR_L, 0, 0, DUMPLOCALE_CALENDAR_S},
  {DUMPLOCALE_NUMBERING_L, 0, 0, DUMPLOCALE_NUMBERING_S},
  {DUMPLOCALE_ALPHABET_L, 1, 0, DUMPLOCALE_ALPHABET_S},
  {DUMPLOCALE_IDENTIFIER_ALPHABET_L, 1, 0, DUMPLOCALE_IDENTIFIER_ALPHABET_S},
  {DUMPLOCALE_COLLATION_L, 0, 0, DUMPLOCALE_COLLATION_S},
  {DUMPLOCALE_WEIGHT_ORDER_L, 0, 0, DUMPLOCALE_WEIGHT_ORDER_S},
  {DUMPLOCALE_START_VALUE_L, 1, 0, DUMPLOCALE_START_VALUE_S},
  {DUMPLOCALE_END_VALUE_L, 1, 0, DUMPLOCALE_END_VALUE_S},
  {DUMPLOCALE_NORMALIZATION_L, 0, 0, DUMPLOCALE_NORMALIZATION_S},
  {DUMPLOCALE_CONSOLE_CONV_L, 0, 0, DUMPLOCALE_CONSOLE_CONV_S},
  {0, 0, 0, 0}
};

static UTIL_ARG_MAP ua_SyncCollDB_Map[] = {
  {OPTION_STRING_TABLE, {0}, {0}},
  {SYNCCOLL_CHECK_S, {ARG_BOOLEAN}, {0}},
  {SYNCCOLL_FORCESYNC_S, {ARG_BOOLEAN}, {0}},
  {0, {0}, {0}}
};

static GETOPT_LONG ua_SyncCollDB_Option[] = {
  {SYNCCOLL_CHECK_L, 0, 0, SYNCCOLL_CHECK_S},
  {SYNCCOLL_FORCESYNC_L, 0, 0, SYNCCOLL_FORCESYNC_S},
  {0, 0, 0, 0}
};

static UTIL_MAP ua_Utility_Map[] = {
  {CREATEDB, SA_ONLY, 2, UTIL_OPTION_CREATEDB, "createdb",
   ua_Create_Option, ua_Create_Option_Map},
  {RENAMEDB, SA_ONLY, 2, UTIL_OPTION_RENAMEDB, "renamedb",
   ua_Rename_Option, ua_Rename_Option_Map},
  {COPYDB, SA_ONLY, 2, UTIL_OPTION_COPYDB, "copydb",
   ua_Copy_Option, ua_Copy_Option_Map},
  {DELETEDB, SA_ONLY, 1, UTIL_OPTION_DELETEDB, "deletedb",
   ua_Delete_Option, ua_Delete_Option_Map},
  {BACKUPDB, SA_CS, 1, UTIL_OPTION_BACKUPDB, "backupdb",
   ua_Backup_Option, ua_Backup_Option_Map},
  {RESTOREDB, SA_ONLY, 1, UTIL_OPTION_RESTOREDB, "restoredb",
   ua_Restore_Option, ua_Restore_Option_Map},
  {ADDVOLDB, SA_CS, 2, UTIL_OPTION_ADDVOLDB, "addvoldb",
   ua_Addvol_Option, ua_Addvol_Option_Map},
  {SPACEDB, SA_CS, 1, UTIL_OPTION_SPACEDB, "spacedb",
   ua_Space_Option, ua_Space_Option_Map},
  {LOCKDB, CS_ONLY, 1, UTIL_OPTION_LOCKDB, "lockdb",
   ua_Lock_Option, ua_Lock_Option_Map},
  {KILLTRAN, CS_ONLY, 1, UTIL_OPTION_KILLTRAN, "killtran",
   ua_Killtran_Option, ua_Killtran_Option_Map},
  {OPTIMIZEDB, SA_ONLY, 1, UTIL_OPTION_OPTIMIZEDB, "optimizedb",
   ua_Optimize_Option, ua_Optimize_Option_Map},
  {INSTALLDB, SA_ONLY, 1, UTIL_OPTION_INSTALLDB, "installdb",
   ua_Install_Option, ua_Install_Option_Map},
  {DIAGDB, SA_ONLY, 1, UTIL_OPTION_DIAGDB, "diagdb",
   ua_Diag_Option, ua_Diag_Option_Map},
  {PATCHDB, SA_ONLY, 1, UTIL_OPTION_PATCHDB, "patchdb",
   ua_Patch_Option, ua_Patch_Option_Map},
  {CHECKDB, SA_CS, 1, UTIL_OPTION_CHECKDB, "checkdb",
   ua_Check_Option, ua_Check_Option_Map},
  {ALTERDBHOST, SA_ONLY, 1, UTIL_OPTION_ALTERDBHOST, "alterdbhost",
   ua_Alterdbhost_Option, ua_Alterdbhost_Option_Map},
  {PLANDUMP, CS_ONLY, 1, UTIL_OPTION_PLANDUMP, "plandump",
   ua_Plandump_Option, ua_Plandump_Option_Map},
  {ESTIMATE_DATA, SA_ONLY, 2, UTIL_OPTION_ESTIMATE_DATA, "estimatedb_data", 0,
   0},
  {ESTIMATE_INDEX, SA_ONLY, 2, UTIL_OPTION_ESTIMATE_INDEX, "edtimatedb_index",
   0, 0},
  {LOADDB, SA_CS, 1, UTIL_OPTION_LOADDB, "loaddb_user", ua_Load_Option,
   ua_Load_Option_Map},
  {UNLOADDB, SA_CS, 1, UTIL_OPTION_UNLOADDB, "unloaddb",
   ua_Unload_Option, ua_Unload_Option_Map},
  {COMPACTDB, SA_CS, 1, UTIL_OPTION_COMPACTDB, "compactdb",
   ua_Compact_Option, ua_Compact_Option_Map},
  {PARAMDUMP, SA_CS, 1, UTIL_OPTION_PARAMDUMP, "paramdump",
   ua_Paramdump_Option, ua_Paramdump_Option_Map},
  {STATDUMP, CS_ONLY, 1, UTIL_OPTION_STATDUMP, "statdump",
   ua_Statdump_Option, ua_Statdump_Option_Map},
  {CHANGEMODE, CS_ONLY, 1, UTIL_OPTION_CHANGEMODE, "changemode",
   ua_Changemode_Option, ua_Changemode_Option_Map},
  {COPYLOGDB, CS_ONLY, 1, UTIL_OPTION_COPYLOGDB, "copylogdb",
   ua_Copylog_Option, ua_Copylog_Option_Map},
  {APPLYLOGDB, CS_ONLY, 1, UTIL_OPTION_APPLYLOGDB, "applylogdb",
   ua_Applylog_Option, ua_Applylog_Option_Map},
  {APPLYINFO, CS_ONLY, 1, UTIL_OPTION_APPLYINFO, "applyinfo",
   ua_ApplyInfo_Option, ua_ApplyInfo_Option_Map},
  {ACLDB, CS_ONLY, 1, UTIL_OPTION_ACLDB, "acldb",
   ua_Acl_Option, ua_Acl_Option_Map},
  {GENLOCALE, SA_ONLY, 1, UTIL_OPTION_GENERATE_LOCALE, "genlocale",
   ua_GenLocale_Option, ua_GenLocale_Map},
  {DUMPLOCALE, SA_ONLY, 1, UTIL_OPTION_DUMP_LOCALE, "dumplocale",
   ua_DumpLocale_Option, ua_DumpLocale_Map},
  {SYNCCOLLDB, SA_ONLY, 1, UTIL_OPTION_SYNCCOLLDB, "synccolldb",
   ua_SyncCollDB_Option, ua_SyncCollDB_Map},
  {TRANLIST, CS_ONLY, 1, UTIL_OPTION_TRANLIST, "tranlist",
   ua_Tranlist_Option, ua_Tranlist_Option_Map},
  {PREFETCHLOGDB, CS_ONLY, 1, UTIL_OPTION_PREFETCHLOGDB, "prefetchlogdb",
   ua_Prefetchlogdb_Option, ua_Prefetchlogdb_Option_Map},
  {-1, -1, 0, 0, 0, 0, 0}
};

static const char *util_get_library_name (int utility_index);
static int util_get_function_name (const char **function_name,
				   const char *utility_name);
static int util_get_utility_index (int *utility_index,
				   const char *utility_name);
static void print_admin_usage (const char *argv0);
static void print_admin_version (const char *argv0);

/*
 * util_admin_usage - display an usage of this utility
 *
 * return:
 *
 * NOTE:
 */
static void
print_admin_usage (const char *argv0)
{
  typedef void (*ADMIN_USAGE) (const char *);

  DSO_HANDLE util_sa_library;
  DSO_HANDLE symbol;
  ADMIN_USAGE admin_usage;

  utility_load_library (&util_sa_library, LIB_UTIL_SA_NAME);
  if (util_sa_library == NULL)
    {
      utility_load_print_error (stderr);
      return;
    }
  utility_load_symbol (util_sa_library, &symbol,
		       UTILITY_ADMIN_USAGE_FUNC_NAME);
  if (symbol == NULL)
    {
      utility_load_print_error (stderr);
      return;
    }

  admin_usage = (ADMIN_USAGE) symbol;
  (*admin_usage) (argv0);
}

/*
 * util_admin_version - display a version of this utility
 *
 * return:
 *
 * NOTE:
 */
static void
print_admin_version (const char *argv0)
{
  typedef void (*ADMIN_VERSION) (const char *);

  DSO_HANDLE util_sa_library;
  DSO_HANDLE symbol;
  ADMIN_VERSION admin_version;

  utility_load_library (&util_sa_library, LIB_UTIL_SA_NAME);
  if (util_sa_library == NULL)
    {
      utility_load_print_error (stderr);
      return;
    }
  utility_load_symbol (util_sa_library, &symbol,
		       UTILITY_ADMIN_VERSION_FUNC_NAME);
  if (symbol == NULL)
    {
      utility_load_print_error (stderr);
      return;
    }

  admin_version = (ADMIN_VERSION) symbol;
  (*admin_version) (argv0);
}

/*
 * main() - a administrator utility's entry point
 *
 * return: EXIT_SUCCESS/EXIT_FAILURE
 *
 * NOTE:
 */
int
main (int argc, char *argv[])
{
  int status;
  DSO_HANDLE library_handle, symbol_handle;
  UTILITY_FUNCTION loaded_function;
  int utility_index;
  const char *library_name;
  bool is_valid_arg = true;

  if (argc > 1 && strcmp (argv[1], "--version") == 0)
    {
      print_admin_version (argv[0]);
      return EXIT_SUCCESS;
    }

  if (argc < 2
      || util_get_utility_index (&utility_index, argv[1]) != NO_ERROR)
    {
      goto print_usage;
    }

  if (util_parse_argument (&ua_Utility_Map[utility_index], argc - 1, &argv[1])
      != NO_ERROR)
    {
      is_valid_arg = false;
      argc = 2;
    }

  library_name = util_get_library_name (utility_index);
  status = utility_load_library (&library_handle, library_name);
  if (status == NO_ERROR)
    {
      const char *symbol_name;
      status = util_get_function_name (&symbol_name, argv[1]);
      if (status != NO_ERROR)
	{
	  goto print_usage;
	}

      status =
	utility_load_symbol (library_handle, &symbol_handle, symbol_name);
      if (status == NO_ERROR)
	{
	  UTIL_FUNCTION_ARG util_func_arg;
	  util_func_arg.arg_map = ua_Utility_Map[utility_index].arg_map;
	  util_func_arg.command_name =
	    ua_Utility_Map[utility_index].utility_name;
	  util_func_arg.argv0 = argv[0];
	  util_func_arg.argv = argv;
	  util_func_arg.valid_arg = is_valid_arg;
	  loaded_function = (UTILITY_FUNCTION) symbol_handle;
	  status = (*loaded_function) (&util_func_arg);
	}
      else
	{
	  utility_load_print_error (stderr);
	  goto error_exit;
	}
    }
  else
    {
      utility_load_print_error (stderr);
      goto error_exit;
    }
  return status;
print_usage:
  print_admin_usage (argv[0]);
error_exit:
  return EXIT_FAILURE;
}

/*
 * get_lib_util_name - check executable utility mode
 *
 * return:
 *
 * NOTE:
 */
static const char *
util_get_library_name (int utility_index)
{
  int utility_type = ua_Utility_Map[utility_index].utility_type;
  UTIL_ARG_MAP *arg_map = ua_Utility_Map[utility_index].arg_map;

  switch (utility_type)
    {
    case SA_ONLY:
      return LIB_UTIL_SA_NAME;
    case CS_ONLY:
      return LIB_UTIL_CS_NAME;
    case SA_CS:
      {
	int i;
	for (i = 0; arg_map[i].arg_ch; i++)
	  {
	    int key = arg_map[i].arg_ch;
	    if ((key == 'C' || key == LOAD_CS_MODE_S)
		&& arg_map[i].arg_value.p != NULL)
	      {
		return LIB_UTIL_CS_NAME;
	      }
	    if ((key == 'S' || key == LOAD_SA_MODE_S)
		&& arg_map[i].arg_value.p != NULL)
	      {
		return LIB_UTIL_SA_NAME;
	      }
	  }
      }
    }
  return LIB_UTIL_CS_NAME;
}

/*
 * util_get_function_name - get an utility name by a function name
 *
 * return:
 *
 * NOTE:
 */
static int
util_get_function_name (const char **function_name, const char *utility_name)
{
  int i;
  for (i = 0; ua_Utility_Map[i].utility_index != -1; i++)
    {
      if (strcasecmp (ua_Utility_Map[i].utility_name, utility_name) == 0)
	{
	  (*function_name) = ua_Utility_Map[i].function_name;
	  return NO_ERROR;
	}
    }
  return ER_GENERIC_ERROR;
}

/*
 * util_get_utility_index - get an index of the utility by the name
 *
 * return: utility index
 */
static int
util_get_utility_index (int *utility_index, const char *utility_name)
{
  int i;
  for (i = 0, *utility_index = -1; ua_Utility_Map[i].utility_index != -1; i++)
    {
      if (strcasecmp (ua_Utility_Map[i].utility_name, utility_name) == 0)
	{
	  *utility_index = ua_Utility_Map[i].utility_index;
	  break;
	}
    }

  return *utility_index == -1 ? ER_GENERIC_ERROR : NO_ERROR;
}
